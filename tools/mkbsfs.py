#!/usr/bin/env python3
"""Build a BSFS partition image from a directory tree. See docs/BSFS.md.

usage: mkbsfs.py <tree> <image> [--size MIB] [--dir PATH]... [--symlink PATH=TARGET]...
"""
import argparse
import os
import struct

BLOCK = 4096
INODE = 128
DIRENT = 64
DIRECT = 12
PTRS = BLOCK // 4
INODE_COUNT = 4096
NAME_MAX = 57
TYPE_FILE, TYPE_DIR, TYPE_SYMLINK = 1, 2, 3
PERM_DEFAULT = 0o755


class Symlink:
    def __init__(self, target):
        self.target = target


def read_tree(path):
    tree = {}
    for name in sorted(os.listdir(path)):
        child = os.path.join(path, name)
        if os.path.islink(child):
            tree[name] = Symlink(os.readlink(child))
        elif os.path.isdir(child):
            tree[name] = read_tree(child)
        else:
            with open(child, "rb") as f:
                tree[name] = f.read()
    return tree


def merge(tree, extra):
    for name, value in extra.items():
        if isinstance(value, dict) and isinstance(tree.get(name), dict):
            merge(tree[name], value)
        else:
            tree[name] = value


def insert(tree, path, value):
    parts = path.strip("/").split("/")
    for part in parts[:-1]:
        tree = tree.setdefault(part, {})
    if value is not None or parts[-1] not in tree:
        tree[parts[-1]] = {} if value is None else value


class Image:
    def __init__(self, size):
        self.total = size // BLOCK
        self.ibitmap_start = 1
        self.ibitmap_blocks = (INODE_COUNT + BLOCK * 8 - 1) // (BLOCK * 8)
        self.bbitmap_start = self.ibitmap_start + self.ibitmap_blocks
        self.bbitmap_blocks = (self.total + BLOCK * 8 - 1) // (BLOCK * 8)
        self.itable_start = self.bbitmap_start + self.bbitmap_blocks
        self.itable_blocks = INODE_COUNT * INODE // BLOCK
        self.data_start = self.itable_start + self.itable_blocks
        self.disk = bytearray(self.total * BLOCK)
        self.next_block = self.data_start
        self.next_inode = 1  # inode 0 is never used

    def alloc_block(self):
        if self.next_block >= self.total:
            raise SystemExit("mkbsfs: image is full")
        self.next_block += 1
        return self.next_block - 1

    def alloc_inode(self):
        if self.next_inode >= INODE_COUNT:
            raise SystemExit("mkbsfs: out of inodes")
        self.next_inode += 1
        return self.next_inode - 1

    def put_block(self, number, data):
        self.disk[number * BLOCK:number * BLOCK + len(data)] = data

    def put_table(self, pointers):
        block = self.alloc_block()
        self.put_block(block, struct.pack(f"<{len(pointers)}I", *pointers))
        return block

    def store(self, data):
        """Write data; return (direct pointers, indirect, double indirect)."""
        blocks = []
        for offset in range(0, len(data), BLOCK):
            block = self.alloc_block()
            self.put_block(block, data[offset:offset + BLOCK])
            blocks.append(block)

        direct = blocks[:DIRECT] + [0] * (DIRECT - min(DIRECT, len(blocks)))
        rest = blocks[DIRECT:]
        indirect = self.put_table(rest[:PTRS]) if rest else 0
        rest = rest[PTRS:]
        double = 0
        if rest:
            if len(rest) > PTRS * PTRS:
                raise SystemExit("mkbsfs: file too large")
            tables = [self.put_table(rest[i:i + PTRS]) for i in range(0, len(rest), PTRS)]
            double = self.put_table(tables)
        return direct, indirect, double

    def put_inode(self, ino, kind, data):
        direct, indirect, double = self.store(data)
        raw = struct.pack("<HHIQQ12III48x", kind, PERM_DEFAULT, 1, len(data), 0, *direct, indirect, double)
        offset = self.itable_start * BLOCK + ino * INODE
        self.disk[offset:offset + INODE] = raw

    def add(self, node, ino=None):
        """Write node and its children; return (inode, type)."""
        if ino is None:
            ino = self.alloc_inode()
        if isinstance(node, dict):
            entries = bytearray()
            for name in sorted(node):
                encoded = name.encode("ascii")
                if len(encoded) > NAME_MAX:
                    raise SystemExit(f"mkbsfs: name too long: {name}")
                child, kind = self.add(node[name])
                entries += struct.pack("<IBB58s", child, kind, len(encoded), encoded)
            # Directories occupy whole blocks so free slots can be reused.
            padded = max(1, (len(entries) + BLOCK - 1) // BLOCK) * BLOCK
            self.put_inode(ino, TYPE_DIR, bytes(entries.ljust(padded, b"\0")))
            return ino, TYPE_DIR
        if isinstance(node, Symlink):
            self.put_inode(ino, TYPE_SYMLINK, node.target.encode("ascii"))
            return ino, TYPE_SYMLINK
        self.put_inode(ino, TYPE_FILE, node)
        return ino, TYPE_FILE

    @staticmethod
    def bitmap(used, bits):
        data = bytearray((bits + 7) // 8)
        for i in range(used):
            data[i // 8] |= 1 << (i % 8)
        return data

    def finish(self):
        self.put_block(self.ibitmap_start, self.bitmap(self.next_inode, INODE_COUNT))
        self.put_block(self.bbitmap_start, self.bitmap(self.next_block, self.total))
        super_block = struct.pack(
            "<15I", 0x53465342, 1, BLOCK, self.total, INODE_COUNT,
            self.total - self.next_block, INODE_COUNT - self.next_inode,
            self.ibitmap_start, self.ibitmap_blocks,
            self.bbitmap_start, self.bbitmap_blocks,
            self.itable_start, self.itable_blocks, self.data_start, 1,
        )
        self.put_block(0, super_block)
        return self.disk


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("tree")
    parser.add_argument("image")
    parser.add_argument("--size", type=int, default=64, help="size in MiB")
    parser.add_argument("--dir", action="append", default=[])
    parser.add_argument("--symlink", action="append", default=[])
    parser.add_argument("--overlay", help="another tree merged on top, if it exists")
    args = parser.parse_args()

    tree = read_tree(args.tree)
    if args.overlay and os.path.isdir(args.overlay):
        merge(tree, read_tree(args.overlay))
    for path in args.dir:
        insert(tree, path, None)
    for spec in args.symlink:
        path, target = spec.split("=", 1)
        insert(tree, path, Symlink(target))

    image = Image(args.size * 1024 * 1024)
    root = image.alloc_inode()
    image.add(tree, root)
    with open(args.image, "wb") as f:
        f.write(image.finish())


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Copy /userspace out of an existing BShitOS disk image into a directory tree.

usage: carryover.py <disk-image> <output-directory>

Used by the build so rebuilding the OS keeps the user's files. A missing or
unreadable image just produces an empty tree.
"""
import os
import shutil
import struct
import sys

SECTOR = 512
BLOCK = 4096
INODE = 128
DIRENT = 64
DIRECT = 12
PTRS = BLOCK // 4
MAGIC = 0x53465342
BSFS_TYPE = bytes.fromhex("F5B5F5B500005342465342534849 5421".replace(" ", ""))
TYPE_FILE, TYPE_DIR, TYPE_SYMLINK = 1, 2, 3
KEEP = "userspace"


class Disk:
    def __init__(self, data, offset):
        self.data = data
        self.offset = offset
        sb = struct.unpack_from("<15I", data, offset)
        if sb[0] != MAGIC:
            raise ValueError("no BSFS superblock")
        self.inode_table = sb[11]

    def block(self, number):
        start = self.offset + number * BLOCK
        return self.data[start:start + BLOCK]

    def inode(self, number):
        start = self.offset + self.inode_table * BLOCK + number * INODE
        kind, _, _, size, _ = struct.unpack_from("<HHIQQ", self.data, start)
        pointers = struct.unpack_from("<14I", self.data, start + 24)
        return kind, size, pointers

    def blocks_of(self, pointers, count):
        direct = list(pointers[:DIRECT])
        out = direct[:count]
        if len(out) < count and pointers[12]:
            out += list(struct.unpack("<%dI" % PTRS, self.block(pointers[12])))[:count - len(out)]
        if len(out) < count and pointers[13]:
            for table in struct.unpack("<%dI" % PTRS, self.block(pointers[13])):
                if len(out) >= count or not table:
                    break
                out += list(struct.unpack("<%dI" % PTRS, self.block(table)))[:count - len(out)]
        return out

    def read(self, number):
        _, size, pointers = self.inode(number)
        count = (size + BLOCK - 1) // BLOCK
        data = b"".join(self.block(b) if b else bytes(BLOCK) for b in self.blocks_of(pointers, count))
        return data[:size]

    def entries(self, number):
        data = self.read(number)
        for offset in range(0, len(data) - DIRENT + 1, DIRENT):
            ino, kind, length = struct.unpack_from("<IBB", data, offset)
            if ino:
                yield data[offset + 6:offset + 6 + length].decode("ascii", "replace"), ino, kind


def find_partition(data):
    count, size = struct.unpack_from("<II", data, SECTOR + 80)
    entries_lba = struct.unpack_from("<Q", data, SECTOR + 72)[0]
    for i in range(min(count, 128)):
        entry = entries_lba * SECTOR + i * size
        if data[entry:entry + 16] == BSFS_TYPE:
            return struct.unpack_from("<Q", data, entry + 32)[0] * SECTOR
    raise ValueError("no BSFS partition")


def extract(disk, number, path):
    os.makedirs(path, exist_ok=True)
    for name, ino, kind in disk.entries(number):
        if "/" in name or name in (".", ".."):
            continue
        target = os.path.join(path, name)
        if kind == TYPE_DIR:
            extract(disk, ino, target)
        elif kind == TYPE_SYMLINK:
            os.symlink(disk.read(ino).decode("ascii", "replace"), target)
        elif kind == TYPE_FILE:
            with open(target, "wb") as f:
                f.write(disk.read(ino))


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    image, output = sys.argv[1:]
    shutil.rmtree(output, ignore_errors=True)
    os.makedirs(output)
    if not os.path.exists(image):
        return
    try:
        with open(image, "rb") as f:
            data = f.read()
        disk = Disk(data, find_partition(data))
        for name, ino, kind in disk.entries(1):
            if name == KEEP and kind == TYPE_DIR:
                extract(disk, ino, os.path.join(output, KEEP))
                print(f"carryover: kept /{KEEP} from {image}")
    except (ValueError, struct.error, OSError) as error:
        print(f"carryover: could not read {image} ({error}); starting fresh")


if __name__ == "__main__":
    main()

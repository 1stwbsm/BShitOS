#!/usr/bin/env python3
"""Convert a statically linked ELF32 into a .BS program or .blib library.

usage: mkbs.py <input.elf> <output> [--lib] [--need NAME.blib]...
"""
import argparse
import struct

PT_LOAD = 1
NAME_SIZE = 32


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--lib", action="store_true")
    parser.add_argument("--need", action="append", default=[])
    args = parser.parse_args()

    with open(args.input, "rb") as f:
        elf = f.read()
    if elf[:6] != b"\x7fELF\x01\x01":
        raise SystemExit("mkbs: input is not a 32-bit little-endian ELF")

    entry, phoff = struct.unpack_from("<II", elf, 24)
    phentsize, phnum = struct.unpack_from("<HH", elf, 42)
    segments = []
    for i in range(phnum):
        kind, offset, vaddr, _, filesz, memsz, _, _ = struct.unpack_from("<8I", elf, phoff + i * phentsize)
        if kind == PT_LOAD and memsz:
            segments.append((vaddr, offset, filesz, memsz))
    if not segments:
        raise SystemExit("mkbs: nothing to load")

    load = min(s[0] for s in segments)
    file_end = max(s[0] + s[2] for s in segments)
    mem_end = max(s[0] + s[3] for s in segments)
    image = bytearray(file_end - load)
    for vaddr, offset, filesz, _ in segments:
        image[vaddr - load:vaddr - load + filesz] = elf[offset:offset + filesz]

    magic = b"BLIB" if args.lib else b"BSX1"
    header = struct.pack("<4s7I", magic, 1, load, len(image), mem_end - load, entry, len(args.need), 0)
    needs = b""
    for name in args.need:
        if len(name) >= NAME_SIZE:
            raise SystemExit(f"mkbs: library name too long: {name}")
        needs += name.encode("ascii").ljust(NAME_SIZE, b"\0")

    with open(args.output, "wb") as f:
        f.write(header + needs + image)


if __name__ == "__main__":
    main()

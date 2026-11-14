#!/usr/bin/env python3
"""Build a GPT disk image: a FAT32 EFI System Partition plus a BSFS partition.

usage: mkimage.py <esp-directory> <bsfs-image> <output-image>

Needs no root, loop devices or mtools. Names must fit FAT 8.3 and are stored in
upper case.
"""
import os
import struct
import sys
import uuid
import zlib

SECTOR = 512
ESP_START_LBA = 2048
GPT_ENTRY_SECTORS = 32  # 128 entries * 128 bytes
MIN_ESP_BYTES = 64 * 1024 * 1024
HEADROOM_BYTES = 16 * 1024 * 1024
RESERVED_SECTORS = 32
NUM_FATS = 2
FAT_END = 0x0FFFFFFF
ATTR_DIR = 0x10
ATTR_FILE = 0x20
FAT_DATE = ((2026 - 1980) << 9) | (1 << 5) | 1  # fixed, for reproducible images
ESP_TYPE = uuid.UUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B")
BSFS_TYPE = uuid.UUID("B5F5B5F5-0000-4253-4653-425348495421")
NAMESPACE = uuid.UUID("6f3c1d52-5b7e-4f0e-9d7a-0b5f4e8a1c11")


def short_name(name):
    stem, _, ext = name.upper().partition(".")
    if not stem or len(stem) > 8 or len(ext) > 3 or "." in ext:
        raise SystemExit(f"mkimage: '{name}' does not fit FAT 8.3")
    return stem.ljust(8).encode("ascii") + ext.ljust(3).encode("ascii")


def dir_entry(name11, attr, cluster, size):
    return struct.pack(
        "<11sBBBHHHHHHHI", name11, attr, 0, 0, 0, FAT_DATE, FAT_DATE,
        cluster >> 16, 0, FAT_DATE, cluster & 0xFFFF, size,
    )


class Fat32:
    def __init__(self, sectors):
        self.sectors = sectors
        fat_sectors = ((sectors - RESERVED_SECTORS + 2) * 4 + SECTOR - 1) // SECTOR
        self.fat_sectors = fat_sectors
        self.clusters = sectors - RESERVED_SECTORS - NUM_FATS * fat_sectors
        if self.clusters < 65525:
            raise SystemExit("mkimage: partition too small for FAT32")
        self.image = bytearray(sectors * SECTOR)
        self.fat = [0] * (self.clusters + 2)
        self.fat[0] = 0x0FFFFFF8
        self.fat[1] = FAT_END
        self.next_free = 2

    def alloc(self, count):
        """Reserve a contiguous chain of clusters and return the first."""
        first = self.next_free
        if first + count > len(self.fat):
            raise SystemExit("mkimage: partition full")
        for cluster in range(first, first + count - 1):
            self.fat[cluster] = cluster + 1
        self.fat[first + count - 1] = FAT_END
        self.next_free += count
        return first

    def write(self, first_cluster, data):
        offset = (RESERVED_SECTORS + NUM_FATS * self.fat_sectors + first_cluster - 2) * SECTOR
        self.image[offset:offset + len(data)] = data

    def add_dir(self, path, cluster, parent):
        """Fill a directory whose own clusters are already allocated."""
        entries = []
        if parent is not None:
            entries.append(dir_entry(b".          ", ATTR_DIR, cluster, 0))
            entries.append(dir_entry(b"..         ", ATTR_DIR, parent, 0))

        for name in sorted(os.listdir(path)):
            child = os.path.join(path, name)
            if os.path.isdir(child):
                count = dir_clusters(child, top=False)
                first = self.alloc(count)
                self.add_dir(child, first, cluster if parent is not None else 0)
                entries.append(dir_entry(short_name(name), ATTR_DIR, first, 0))
            else:
                with open(child, "rb") as f:
                    data = f.read()
                first = self.alloc((len(data) + SECTOR - 1) // SECTOR) if data else 0
                if data:
                    self.write(first, data)
                entries.append(dir_entry(short_name(name), ATTR_FILE, first, len(data)))

        self.write(cluster, b"".join(entries))

    def finish(self, hidden_sectors):
        bpb = struct.pack(
            "<3s8sHBHBHHBHHHIIIHHIHH12sBBBI11s8s",
            b"\xEB\x58\x90", b"BSHITOS ", SECTOR, 1, RESERVED_SECTORS, NUM_FATS, 0, 0,
            0xF8, 0, 63, 255, hidden_sectors, self.sectors, self.fat_sectors, 0, 0, 2,
            1, 6, bytes(12), 0x80, 0, 0x29, 0x42534254, b"BSHITOS    ", b"FAT32   ",
        )
        boot = bytearray(SECTOR)
        boot[:len(bpb)] = bpb
        boot[510:512] = b"\x55\xAA"

        info = bytearray(SECTOR)
        info[0:4] = struct.pack("<I", 0x41615252)
        info[484:496] = struct.pack("<III", 0x61417272, self.clusters + 2 - self.next_free, self.next_free)
        info[508:512] = struct.pack("<I", 0xAA550000)

        for base in (0, 6):
            self.image[base * SECTOR:(base + 1) * SECTOR] = boot
            self.image[(base + 1) * SECTOR:(base + 2) * SECTOR] = info

        table = struct.pack(f"<{len(self.fat)}I", *self.fat)
        for n in range(NUM_FATS):
            offset = (RESERVED_SECTORS + n * self.fat_sectors) * SECTOR
            self.image[offset:offset + len(table)] = table
        return self.image


def dir_clusters(path, top):
    entries = len(os.listdir(path)) + (0 if top else 2)
    return max(1, (entries * 32 + SECTOR - 1) // SECTOR)


def tree_bytes(path):
    total = 0
    for root, _, files in os.walk(path):
        total += sum(os.path.getsize(os.path.join(root, f)) for f in files)
    return total


def gpt_header(lba, backup, first, last, disk, entries_lba, entries_crc):
    fields = [b"EFI PART", 0x00010000, 92, 0, 0, lba, backup, first, last, disk.bytes_le,
              entries_lba, 128, 128, entries_crc]
    layout = "<8sIIIIQQQQ16sQIII"
    crc = zlib.crc32(struct.pack(layout, *fields))
    fields[3] = crc
    return struct.pack(layout, *fields).ljust(SECTOR, b"\0")


def partition_entry(kind, name, first, count):
    return struct.pack("<16s16sQQQ72s", kind.bytes_le, uuid.uuid5(NAMESPACE, name).bytes_le,
                       first, first + count - 1, 0, name.encode("utf-16-le"))


def build_disk(esp, esp_sectors, bsfs):
    bsfs_sectors = len(bsfs) // SECTOR
    bsfs_lba = ESP_START_LBA + esp_sectors
    bsfs_lba = (bsfs_lba + ESP_START_LBA - 1) // ESP_START_LBA * ESP_START_LBA  # 1 MiB aligned
    total = bsfs_lba + bsfs_sectors + 1 + GPT_ENTRY_SECTORS
    first_usable = 2 + GPT_ENTRY_SECTORS
    last_usable = total - 2 - GPT_ENTRY_SECTORS
    disk_guid = uuid.uuid5(NAMESPACE, "disk")

    entries = (partition_entry(ESP_TYPE, "EFI System", ESP_START_LBA, esp_sectors) +
               partition_entry(BSFS_TYPE, "BShitOS", bsfs_lba, bsfs_sectors)).ljust(128 * 128, b"\0")
    entries_crc = zlib.crc32(entries)

    mbr = bytearray(SECTOR)
    mbr[446:462] = struct.pack("<BBBBBBBBII", 0, 0, 2, 0, 0xEE, 0xFF, 0xFF, 0xFF, 1,
                               min(total - 1, 0xFFFFFFFF))
    mbr[510:512] = b"\x55\xAA"

    disk = bytearray(total * SECTOR)
    disk[0:SECTOR] = mbr
    disk[SECTOR:2 * SECTOR] = gpt_header(1, total - 1, first_usable, last_usable, disk_guid, 2, entries_crc)
    disk[2 * SECTOR:2 * SECTOR + len(entries)] = entries
    disk[ESP_START_LBA * SECTOR:(ESP_START_LBA + esp_sectors) * SECTOR] = esp
    disk[bsfs_lba * SECTOR:(bsfs_lba + bsfs_sectors) * SECTOR] = bsfs
    backup_entries = total - 1 - GPT_ENTRY_SECTORS
    disk[backup_entries * SECTOR:(backup_entries * SECTOR) + len(entries)] = entries
    disk[(total - 1) * SECTOR:] = gpt_header(total - 1, 1, first_usable, last_usable, disk_guid,
                                             backup_entries, entries_crc)
    return disk


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    source, bsfs_path, output = sys.argv[1:]
    with open(bsfs_path, "rb") as f:
        bsfs = f.read()

    esp_bytes = max(MIN_ESP_BYTES, tree_bytes(source) * 2 + HEADROOM_BYTES)
    esp_sectors = esp_bytes // SECTOR

    fat = Fat32(esp_sectors)
    root = fat.alloc(dir_clusters(source, top=True))
    fat.add_dir(source, root, None)
    disk = build_disk(fat.finish(ESP_START_LBA), esp_sectors, bsfs)

    # Replace atomically so a VM using the old image keeps a consistent disk.
    temporary = output + ".tmp"
    with open(temporary, "wb") as f:
        f.write(disk)
    os.replace(temporary, output)


if __name__ == "__main__":
    main()

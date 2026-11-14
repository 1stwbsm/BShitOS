#!/usr/bin/env python3
"""Build a bootable live ISO of BShitOS (UEFI El Torito, no-emulation).

usage: mkiso.py <esp-directory> <bsfs-image> <output-iso>

The boot image is a FAT filesystem holding BSBOOT and a copy of the BSFS root
at \\LIVE\\ROOTFS.IMG, which BSBOOT loads into RAM.

ESP.IMG, a plain file on the disc, is the EFI System Partition for an installed
disk (BSBOOT and \\RES, made for LBA 2048). The installer copies it as is; it is
stored without its trailing zeros, and its FAT boot sector gives the full size.
"""
import os
import shutil
import struct
import sys
import tempfile

import mkimage

SECTOR = 2048
FAT_SECTOR = 512
PVD_LBA = 16
BOOT_RECORD_LBA = 17
TERMINATOR_LBA = 18
CATALOG_LBA = 19
PATH_L_LBA = 20
PATH_M_LBA = 21
ROOT_LBA = 22
IMAGE_LBA = 23
BOOT_IMAGE_NAME = b"EFIBOOT.IMG;1"
ESP_IMAGE_NAME = b"ESP.IMG;1"  # sorts after EFIBOOT.IMG, as ISO9660 requires
FLAG_DIRECTORY = 2
DATE = bytes([126, 1, 1, 0, 0, 0, 0])
LONG_DATE = b"0" * 16 + b"\0"


def both16(value):
    return struct.pack("<H", value) + struct.pack(">H", value)


def both32(value):
    return struct.pack("<I", value) + struct.pack(">I", value)


def directory_record(name, extent, size, flags):
    length = 33 + len(name)
    if length % 2:
        length += 1
    record = struct.pack("<BB", length, 0) + both32(extent) + both32(size) + DATE
    record += struct.pack("<BBB", flags, 0, 0) + both16(1) + struct.pack("<B", len(name)) + name
    return record.ljust(length, b"\0")


def build_boot_image(esp_directory, bsfs_path):
    """Returns the bytes of a FAT32 image containing the ESP plus the live root."""
    with tempfile.TemporaryDirectory() as staging:
        tree = os.path.join(staging, "tree")
        shutil.copytree(esp_directory, tree)
        os.makedirs(os.path.join(tree, "LIVE"), exist_ok=True)
        shutil.copyfile(bsfs_path, os.path.join(tree, "LIVE", "ROOTFS.IMG"))

        size = max(mkimage.MIN_ESP_BYTES, mkimage.tree_bytes(tree) * 2 + mkimage.HEADROOM_BYTES)
        fat = mkimage.Fat32(size // FAT_SECTOR)
        root = fat.alloc(mkimage.dir_clusters(tree, top=True))
        fat.add_dir(tree, root, None)
        return bytes(fat.finish(0))


def build_install_esp(esp_directory):
    """The installed disk's ESP, as mkimage.py builds it, minus trailing zeros."""
    size = max(mkimage.MIN_ESP_BYTES, mkimage.tree_bytes(esp_directory) * 2 + mkimage.HEADROOM_BYTES)
    fat = mkimage.Fat32(size // FAT_SECTOR)
    root = fat.alloc(mkimage.dir_clusters(esp_directory, top=True))
    fat.add_dir(esp_directory, root, None)
    return bytes(fat.finish(mkimage.ESP_START_LBA)).rstrip(b"\0")


def boot_catalog():
    validation = bytearray(32)
    validation[0] = 1
    validation[4:28] = b"BShitOS".ljust(24, b" ")
    validation[30] = 0x55
    validation[31] = 0xAA
    total = sum(struct.unpack("<16H", bytes(validation)))
    validation[28:30] = struct.pack("<H", (-total) & 0xFFFF)

    # A sector count below 2 tells firmware to use the whole image.
    entry = struct.pack("<BBHBBHI", 0x88, 0, 0, 0, 0, 1, IMAGE_LBA).ljust(32, b"\0")
    return bytes(validation) + entry


def primary_descriptor(total_sectors, path_table_size, root_record):
    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = b" " * 32
    pvd[40:72] = b"BSHITOS".ljust(32)
    pvd[80:88] = both32(total_sectors)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(SECTOR)
    pvd[132:140] = both32(path_table_size)
    pvd[140:144] = struct.pack("<I", PATH_L_LBA)
    pvd[148:152] = struct.pack(">I", PATH_M_LBA)
    pvd[156:190] = root_record
    pvd[190:813] = b" " * 623
    for offset in (813, 830, 847, 864):
        pvd[offset:offset + 17] = LONG_DATE
    pvd[881] = 1
    return pvd


def path_table(big_endian):
    order = ">" if big_endian else "<"
    return struct.pack("<BB", 1, 0) + struct.pack(order + "IH", ROOT_LBA, 1) + b"\0\0"


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    esp_directory, bsfs_path, output = sys.argv[1:]

    image = build_boot_image(esp_directory, bsfs_path)
    image_sectors = (len(image) + SECTOR - 1) // SECTOR
    install_esp = build_install_esp(esp_directory)
    esp_lba = IMAGE_LBA + image_sectors
    total = esp_lba + (len(install_esp) + SECTOR - 1) // SECTOR

    root = directory_record(b"\0", ROOT_LBA, SECTOR, FLAG_DIRECTORY)
    root += directory_record(b"\1", ROOT_LBA, SECTOR, FLAG_DIRECTORY)
    root += directory_record(BOOT_IMAGE_NAME, IMAGE_LBA, len(image), 0)
    root += directory_record(ESP_IMAGE_NAME, esp_lba, len(install_esp), 0)
    root_for_pvd = directory_record(b"\0", ROOT_LBA, SECTOR, FLAG_DIRECTORY)

    boot_record = bytearray(SECTOR)
    boot_record[0] = 0
    boot_record[1:6] = b"CD001"
    boot_record[6] = 1
    boot_record[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\0")
    boot_record[71:75] = struct.pack("<I", CATALOG_LBA)

    terminator = bytearray(SECTOR)
    terminator[0] = 255
    terminator[1:6] = b"CD001"
    terminator[6] = 1

    disc = bytearray(total * SECTOR)

    def put(lba, data):
        disc[lba * SECTOR:lba * SECTOR + len(data)] = data

    put(PVD_LBA, primary_descriptor(total, len(path_table(False)), root_for_pvd))
    put(BOOT_RECORD_LBA, boot_record)
    put(TERMINATOR_LBA, terminator)
    put(CATALOG_LBA, boot_catalog())
    put(PATH_L_LBA, path_table(False))
    put(PATH_M_LBA, path_table(True))
    put(ROOT_LBA, root)
    put(IMAGE_LBA, image)
    put(esp_lba, install_esp)

    temporary = output + ".tmp"
    with open(temporary, "wb") as f:
        f.write(disc)
    os.replace(temporary, output)


if __name__ == "__main__":
    main()

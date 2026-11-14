# BSFS: the Bullshit File System (version 1)

All integers are little-endian. Blocks are 4096 bytes and block numbers are
relative to the start of the partition. The partition's GPT type GUID is
`B5F5B5F5-0000-4253-4653-425348495421`.

| Blocks | Contents |
|---|---|
| 0 | Superblock |
| `ibitmap_start` | Inode bitmap (bit set = used, LSB first) |
| `bbitmap_start` | Block bitmap, one bit for every block on the partition |
| `itable_start` | Inode table, 32 inodes per block |
| `data_start` | File, directory, symlink and indirect blocks |

Inode 0 is never used and inode 1 is the root directory.

## Inode (128 bytes)
`type:u16 perm:u16 links:u32 size:u64 mtime:u64 direct:u32[12] indirect:u32
double_indirect:u32 uid:u32 gid:u32 reserved:40 bytes`. Types: 1 file, 2 directory,
3 symlink. `perm` holds rwx bits for owner, group and others (`0755`); the kernel
enforces them for everyone but uid 0. uid and gid were reserved bytes before, so
older images read as owned by root.
A block pointer of 0 means a hole, which reads as zeroes.

## Directories
A directory's data is an array of 64-byte entries:
`ino:u32 type:u8 name_len:u8 name:58 bytes` (NUL-terminated, so at most 57
characters). `ino == 0` marks a free slot. There are no `.` or `..` entries;
paths are normalised before they reach the filesystem.

## Symlinks
A symlink's data is its target. A target may name a volume with `volume:/path`:
`/boot/efi` is `esp:/`, the FAT32 boot partition.

# Executable formats

`.BS` programs and `.blib` libraries share a 32-byte header:
`magic:4 version:u32 load:u32 file_size:u32 mem_size:u32 entry:u32
need_count:u32 reserved:u32`, with magic `BSX1` or `BLIB`. It is followed by
`need_count` 32-byte library names (`.BS` only), then `file_size` bytes loaded
at `load`. Memory up to `mem_size` is zeroed.

Programs load in the 4 MiB window at `0x40000000`, with the stack at its top.
Libraries are prelinked at fixed addresses in the window at `0x40400000`, so no
relocation happens at load time. Each process gets its own copy of their data.

# BShitOS

A 32-bit hobby operating system written in C, from the bootloader up.

**Try it in your browser: https://1stwbsm.github.io/BShitOS/**

- **BSBOOT**: a UEFI (IA32) bootloader
- **BSFS**: its own filesystem ([docs/BSFS.md](docs/BSFS.md))
- **The kernel**: paging, ring 3 processes and `int 0x80` system calls, plus drivers
  for the console, framebuffer, PS/2, AHCI, USB and networking
- **bsh**: the shell, along with programs in the `.BS` format and the shared `libbs.blib`
- **Desk**: a windowed graphical environment (type `desk`)
- **web**: a text web browser with HTTPS

## Building

You need gcc with 32-bit support, GNU ld and Python 3. QEMU with the i386 OVMF/edk2
firmware runs it.

```
./STARTVM.sh          # build and boot out/BShitOS.img in QEMU
./STARTVM.sh --live   # boot the live ISO instead
make                  # just build the disk image
```

Files in `/userspace` carry over when you rebuild the disk image.

### Windows

Run `STARTVM.bat`. It uses the QEMU bundled in `tools/qemu-windows`, so you don't
need to install anything. On the first run it downloads the latest disk image that
CI built. `STARTVM.bat --update` replaces it later, which wipes `/userspace`.

## The web version

`src/emu` is BShitEmu, an i686 PC emulator written in C and compiled to WebAssembly.
It provides what BShitOS needs: the processor and paging, a framebuffer, a PS/2
keyboard and mouse, the PIT timer and speaker, the CMOS clock and COM1. It loads the
kernel the way BSBOOT does, then boots the root filesystem as a live ramdisk. The page
itself is a single canvas.

```
make web                                  # needs zig 0.13 for the wasm build
cd out/web && python3 -m http.server 8000
```

GitHub Pages is rebuilt from `main` by `.github/workflows/pages.yml`.
In the browser, changes are lost when you close the page.

## License

BShitOS is free software under the GNU General Public License v3.0; see
[LICENSE](LICENSE). The bundled QEMU in `tools/qemu-windows` keeps its own licenses;
see its [README](tools/qemu-windows/README.txt).

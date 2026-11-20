# BShitOS

A 32-bit hobby operating system written in C, from the bootloader up.

**Try it in your browser: https://1stwbsm.github.io/BShitOS/**

- **BSBOOT**: a UEFI (IA32) bootloader, and a BIOS one (a boot sector plus a real-mode
  stage 2) on the same disk, so images boot on both
- **Runs on a 486**: the TTY needs only a 386-class CPU, 4 MiB of RAM, a legacy BIOS, an
  IDE disk and VGA text mode. The kernel is compiled for i386 and uses PSE, the TSC and
  CPUID only when they exist. Programs are demand-paged in 4 KiB pages. BDeskUI starts
  wherever the machine has a VESA framebuffer and 16 MiB or more.
- **BSFS**: its own filesystem ([docs/BSFS.md](docs/BSFS.md))
- **The kernel**: paging, ring 3 processes and `int 0x80` system calls, plus drivers
  for the console, framebuffer, PS/2, AHCI, USB and networking
- **bsh**: the shell. It colours the line as you type (unknown commands show red before
  you hit Enter), completes with Tab, filters Up/Down history by what you've typed,
  searches history with Ctrl+R, and suggests a fix for typos. It also has `;` `&&` `||`,
  quoting, `$VARS`, globs, aliases, `.bsh` scripts with `$1..$9`, and `~/.bshrc`.
  `help` prints the cheat sheet.
- **Programs**: the `.BS` executable format and the shared `libbs.blib`
- **Users**: accounts with Unix-style owner/group/other permissions, enforced by the
  kernel for everyone but root. Passwords are PBKDF2-SHA256 hashes in a root-only
  `/conf/shadow`, checked by the kernel. Comes with `login`, `su`, `passwd`,
  `useradd`, `userdel`, `chmod`, `chown`, `id` and `ls -l`.
- **Installer**: `su -c install` on the live ISO puts BShitOS on a SATA disk. It
  partitions the disk (GPT, ESP, BSFS filling the rest), copies the system and
  creates your account and a root password.
- **BDeskUI**: the operating environment on top of the TTY, started at login by
  `/conf/autoexec.bsh` wherever there's a graphics mode. It has icon tiles, windows
  with V/^/X title buttons, and a taskbar with the B launcher, where every window gets
  [name | ^ | X]. Windows are glossy chrome held in heavy outlines with hard shadows.
  The "night" skin is the default; aqua, meadow, dusk and concrete are the others.
  BDeskUI fills any screen at a whole-number scale (1920x1080 is 640x360 at 3x,
  1280x800 is 640x400 at 2x). "to TTY" in the B menu drops you to the shell, and
  BSconf can turn the autostart off.
- **Graceful shutdown**: `poweroff`, `reboot` and the power button ask every program
  to exit first (BDeskUI offers to save unsaved work), then the disks are flushed.
- **web**: a text web browser with HTTPS

## Building

You need gcc with 32-bit support, GNU ld and Python 3. QEMU with the i386 OVMF/edk2
firmware runs it.

```
./STARTVM.sh            # build and boot out/BShitOS.img in QEMU
./STARTVM.sh --live     # boot the live ISO, with a blank out/target.img to install onto
./STARTVM.sh --target   # boot whatever got installed onto out/target.img
./STARTVM.sh --486      # the low end: a 486 with 8 MiB, BIOS, IDE, VGA text
make                    # just build the disk image
```

Files in `/userspace` carry over when you rebuild the disk image. That image logs
in as `user` with an empty password, and root's password is empty too; set real
ones with `passwd`. The live ISO logs straight in as `user`.

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

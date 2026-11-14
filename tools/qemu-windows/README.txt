QEMU for Windows, bundled so STARTVM.bat runs BShitOS without installing anything.

This is qemu-system-i386 (QEMU 11.1.3) plus exactly the DLLs it loads, taken
unmodified from MSYS2's UCRT64 packages. PACKAGES.txt lists every package a file
here came from, with its version. share/ holds the x86 firmware it needs,
including the edk2 (OVMF) UEFI firmware that BSBOOT boots on.

Licenses
  QEMU is GPL-2.0; the libraries are under their own licenses (GPL, LGPL, MIT,
  BSD and others). The license files shipped with each package are in licenses/,
  and edk2's are in share/edk2-licenses.txt.

Source code
  The source for every package in PACKAGES.txt, at that exact version, is
  published by MSYS2:
    source archives:  https://repo.msys2.org/mingw/sources/
    build recipes:    https://github.com/msys2/MINGW-packages
  QEMU itself:        https://www.qemu.org/download/#source

Updating
  Download the new mingw-w64-ucrt-x86_64-qemu package and its dependencies from
  MSYS2, take bin/qemu-system-i386.exe, the DLLs it imports (follow the import
  tables, not guesswork), the share/qemu files above and the license folders,
  then update PACKAGES.txt.

@echo off
rem Boots BShitOS in the QEMU bundled in tools\qemu-windows. Nothing to install.
rem
rem   STARTVM.bat            boot out\BShitOS.img (downloaded on first run)
rem   STARTVM.bat --update   replace out\BShitOS.img with the latest build
rem   STARTVM.bat -- ARGS    pass ARGS straight to QEMU
rem
rem BSHIT_AUDIODEV overrides the audio backend, e.g. set BSHIT_AUDIODEV=none,id=snd
rem If you build the image yourself (Linux or WSL: make), it is used as-is.
setlocal
cd /d "%~dp0"

set "QEMU=tools\qemu-windows"
set "IMAGE=out\BShitOS.img"
set "VARS=build\efivars.fd"
set "DISK_URL=https://1stwbsm.github.io/BShitOS/BShitOS-disk.zip"

if not exist "%QEMU%\qemu-system-i386.exe" (
    echo STARTVM: %QEMU%\qemu-system-i386.exe is missing. Clone the whole repository.
    exit /b 1
)

if /i "%~1"=="--update" (
    if exist "%IMAGE%" (
        echo This replaces %IMAGE%. Files you made in /userspace will be gone.
        choice /m "Continue"
        if errorlevel 2 exit /b 1
        del "%IMAGE%"
    )
    shift
)
if "%~1"=="--" shift

if not exist "%IMAGE%" (
    echo STARTVM: no disk image yet, downloading the latest build...
    if not exist out mkdir out
    curl.exe -fL --progress-bar -o out\BShitOS-disk.zip "%DISK_URL%" || goto :download_failed
    tar.exe -xf out\BShitOS-disk.zip -C out || goto :download_failed
    del out\BShitOS-disk.zip
)

if not exist build mkdir build
if not exist "%VARS%" copy /y "%QEMU%\share\edk2-i386-vars.fd" "%VARS%" >nul

if not defined BSHIT_AUDIODEV set "BSHIT_AUDIODEV=dsound,id=snd"

rem cmd's shift does not change %*, so rebuild the remaining arguments.
set "ARGS="
:collect
if "%~1"=="" goto :run
set ARGS=%ARGS% %1
shift
goto :collect

:run
"%QEMU%\qemu-system-i386.exe" -L "%QEMU%\share" ^
    -name BShitOS ^
    -machine q35,pcspk-audiodev=snd ^
    -accel tcg ^
    -m 512M ^
    -display sdl ^
    -drive if=pflash,format=raw,unit=0,readonly=on,file="%QEMU%\share\edk2-i386-code.fd" ^
    -drive if=pflash,format=raw,unit=1,file="%VARS%" ^
    -drive if=none,id=disk,format=raw,file="%IMAGE%" ^
    -device ide-hd,drive=disk,bootindex=1 ^
    -netdev user,id=net0 ^
    -device e1000,netdev=net0 ^
    -device qemu-xhci,id=xhci ^
    -audiodev %BSHIT_AUDIODEV% ^
    -device ich9-intel-hda,addr=0x1b ^
    -device hda-output,audiodev=snd ^
    -serial stdio ^
    %ARGS%
exit /b %errorlevel%

:download_failed
echo STARTVM: could not download or unpack %DISK_URL%
echo Check your connection, or build the image yourself and put it at %IMAGE%.
if exist out\BShitOS-disk.zip del out\BShitOS-disk.zip
exit /b 1

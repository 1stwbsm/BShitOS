/*
 * The web version of BShitOS: runs BShitEmu (bshitos.wasm), boots the BShitOS
 * kernel with the root filesystem as a live ramdisk, and shows the framebuffer
 * on the page's only element, the canvas.
 */
"use strict";

const canvas = document.querySelector("canvas");
const screen = canvas.getContext("2d");

// Layout, done here so the page itself stays a bare canvas.
document.documentElement.style.cssText = "height:100%;background:#000";
document.body.style.cssText = "margin:0;height:100%;display:flex;align-items:center;justify-content:center;overflow:hidden";
canvas.style.cssText = "image-rendering:pixelated;outline:none;max-width:100vw;max-height:100vh;aspect-ratio:1280/800";
canvas.tabIndex = 0;

function fit() {
    const scale = Math.min(innerWidth / canvas.width, innerHeight / canvas.height);
    // Whole-number scaling keeps the font crisp when the window is big enough.
    const s = scale >= 1 ? Math.floor(scale) : scale;
    canvas.style.width = canvas.width * s + "px";
    canvas.style.height = canvas.height * s + "px";
}
addEventListener("resize", fit);
fit();

function message(lines, color) {
    screen.fillStyle = "#000";
    screen.fillRect(0, 0, canvas.width, canvas.height);
    screen.font = "16px monospace";
    screen.fillStyle = color;
    lines.forEach((line, i) => screen.fillText(line, 16, 32 + i * 22));
}

// ---- Speaker: the PIT channel 2 square wave ----------------------------------------

let audio = null, oscillator = null;

function tone(hz) {
    if (!audio) {
        if (!hz) return;
        audio = new AudioContext();
    }
    if (oscillator) {
        oscillator.stop();
        oscillator = null;
    }
    if (hz >= 20 && hz <= 20000) {
        const gain = audio.createGain();
        gain.gain.value = 0.08;
        gain.connect(audio.destination);
        oscillator = audio.createOscillator();
        oscillator.type = "square";
        oscillator.frequency.value = hz;
        oscillator.connect(gain);
        oscillator.start();
    }
}

// ---- Keyboard: event.code to scancode set 1 (above 0xFF means an E0 prefix) -------

const SCANCODES = {
    Escape: 0x01, Digit1: 0x02, Digit2: 0x03, Digit3: 0x04, Digit4: 0x05, Digit5: 0x06,
    Digit6: 0x07, Digit7: 0x08, Digit8: 0x09, Digit9: 0x0A, Digit0: 0x0B, Minus: 0x0C,
    Equal: 0x0D, Backspace: 0x0E, Tab: 0x0F, KeyQ: 0x10, KeyW: 0x11, KeyE: 0x12,
    KeyR: 0x13, KeyT: 0x14, KeyY: 0x15, KeyU: 0x16, KeyI: 0x17, KeyO: 0x18, KeyP: 0x19,
    BracketLeft: 0x1A, BracketRight: 0x1B, Enter: 0x1C, ControlLeft: 0x1D, KeyA: 0x1E,
    KeyS: 0x1F, KeyD: 0x20, KeyF: 0x21, KeyG: 0x22, KeyH: 0x23, KeyJ: 0x24, KeyK: 0x25,
    KeyL: 0x26, Semicolon: 0x27, Quote: 0x28, Backquote: 0x29, ShiftLeft: 0x2A,
    Backslash: 0x2B, KeyZ: 0x2C, KeyX: 0x2D, KeyC: 0x2E, KeyV: 0x2F, KeyB: 0x30,
    KeyN: 0x31, KeyM: 0x32, Comma: 0x33, Period: 0x34, Slash: 0x35, ShiftRight: 0x36,
    NumpadMultiply: 0x37, AltLeft: 0x38, Space: 0x39, CapsLock: 0x3A, F1: 0x3B, F2: 0x3C,
    F3: 0x3D, F4: 0x3E, F5: 0x3F, F6: 0x40, F7: 0x41, F8: 0x42, F9: 0x43, F10: 0x44,
    Numpad7: 0x47, Numpad8: 0x48, Numpad9: 0x49, NumpadSubtract: 0x4A, Numpad4: 0x4B,
    Numpad5: 0x4C, Numpad6: 0x4D, NumpadAdd: 0x4E, Numpad1: 0x4F, Numpad2: 0x50,
    Numpad3: 0x51, Numpad0: 0x52, NumpadDecimal: 0x53, IntlBackslash: 0x56, F11: 0x57,
    F12: 0x58, NumpadEnter: 0x1C, // the kernel reads both Enters the same way
    ControlRight: 0x11D, NumpadDivide: 0x135,
    AltRight: 0x138, Home: 0x147, ArrowUp: 0x148, PageUp: 0x149, ArrowLeft: 0x14B,
    ArrowRight: 0x14D, End: 0x14F, ArrowDown: 0x150, PageDown: 0x151, Insert: 0x152,
    Delete: 0x153,
};

// ---- The machine ----------------------------------------------------------------------

const STATUS_RUNNING = 0, STATUS_HALTED = 1, STATUS_CRASHED = 2, STATUS_REBOOT = 3;
const decoder = new TextDecoder();
let emu, memory, serial = "";

function cString(address) {
    const bytes = new Uint8Array(memory.buffer, address);
    return decoder.decode(bytes.subarray(0, bytes.indexOf(0)));
}

const host = {
    host_now: () => performance.now(),
    host_date: (out) => {
        const d = new Date(), v = new Uint32Array(memory.buffer, out, 6);
        v.set([d.getUTCFullYear(), d.getUTCMonth() + 1, d.getUTCDate(),
               d.getUTCHours(), d.getUTCMinutes(), d.getUTCSeconds()]);
    },
    // COM1 goes to the browser console, a line at a time.
    host_serial: (byte) => {
        if (byte === 10) {
            console.log(serial);
            serial = "";
        } else if (byte !== 13) {
            serial += String.fromCharCode(byte);
        }
    },
    host_tone: tone,
};

async function fetchBytes(name) {
    const response = await fetch(name);
    if (!response.ok) throw new Error(`${name}: ${response.status} ${response.statusText}`);
    return new Uint8Array(await response.arrayBuffer());
}

async function start() {
    message(["BShitEmu: loading BShitOS..."], "#aaa");
    const [module, kernel, disk] = await Promise.all([
        WebAssembly.instantiateStreaming(fetch("bshitos.wasm"), { env: host }),
        fetchBytes("bskern.elf"),
        fetchBytes("root.img"),
    ]);
    emu = module.instance.exports;
    memory = emu.memory;
    new Uint8Array(memory.buffer, emu.emu_staging(), kernel.length).set(kernel);
    new Uint8Array(memory.buffer, emu.emu_ram() + emu.emu_ramdisk_base(), disk.length).set(disk);
    const result = emu.emu_boot(kernel.length, disk.length);
    if (result < 0) throw new Error(`the machine refused to boot (${result})`);

    const pixels = new ImageData(
        new Uint8ClampedArray(memory.buffer, emu.emu_framebuffer(), canvas.width * canvas.height * 4),
        canvas.width, canvas.height);

    function frame() {
        // Leave room in each 16 ms frame for the browser.
        const status = emu.emu_run(document.hidden ? 4 : 11);
        if (emu.emu_dirty())
            screen.putImageData(pixels, 0, 0);
        if (status === STATUS_RUNNING) {
            requestAnimationFrame(frame);
        } else if (status === STATUS_REBOOT) {
            tone(0);
            location.reload();
        } else if (status === STATUS_HALTED) {
            tone(0);
            message(["BShitOS has stopped. Reload the page to start it again."], "#aaa");
        } else {
            tone(0);
            message(["BShitOS crashed the machine:", cString(emu.emu_crash_message()),
                     "", "Reload the page to start again."], "#f66");
        }
    }
    requestAnimationFrame(frame);

    canvas.addEventListener("keydown", (e) => key(e, true));
    canvas.addEventListener("keyup", (e) => key(e, false));
    canvas.addEventListener("blur", releaseKeys);
    canvas.focus();
}

const held = new Set();

function key(e, down) {
    const code = SCANCODES[e.code];
    if (code === undefined) return;
    e.preventDefault(); // keep Tab, Ctrl+W, Backspace and the like inside BShitOS
    if (down) held.add(code); else held.delete(code);
    emu.emu_key(code, down ? 1 : 0);
}

// A key released while the page had no focus would otherwise stay down forever.
function releaseKeys() {
    for (const code of held) emu.emu_key(code, 0);
    held.clear();
}

// ---- Mouse: a PS/2 mouse reports movement, so the pointer is locked to the canvas ---

let buttons = 0, moveX = 0, moveY = 0;

function mouseButtons(e) {
    return (e.buttons & 1 ? 1 : 0) | (e.buttons & 2 ? 2 : 0);
}

canvas.addEventListener("mousedown", (e) => {
    canvas.focus();
    if (document.pointerLockElement !== canvas) {
        canvas.requestPointerLock();
        return; // the click that captures the mouse is not passed on
    }
    buttons = mouseButtons(e);
    emu.emu_mouse(0, 0, buttons);
});
addEventListener("mouseup", (e) => {
    if (document.pointerLockElement !== canvas) return;
    buttons = mouseButtons(e);
    emu.emu_mouse(0, 0, buttons);
});
addEventListener("mousemove", (e) => {
    if (document.pointerLockElement !== canvas || !emu) return;
    // Movement is in CSS pixels; scale it to framebuffer pixels.
    // Leftover fractions carry over so slow movements are not lost.
    const scale = canvas.width / canvas.getBoundingClientRect().width;
    moveX += e.movementX * scale;
    moveY += e.movementY * scale;
    const dx = Math.trunc(moveX), dy = Math.trunc(moveY);
    moveX -= dx;
    moveY -= dy;
    if (dx || dy) emu.emu_mouse(dx, dy, buttons);
});
canvas.addEventListener("contextmenu", (e) => e.preventDefault());
document.addEventListener("pointerlockchange", () => {
    if (document.pointerLockElement !== canvas && buttons && emu) {
        buttons = 0;
        emu.emu_mouse(0, 0, 0);
    }
});

start().catch((error) => message(["BShitEmu could not start BShitOS:", String(error.message || error)], "#f66"));

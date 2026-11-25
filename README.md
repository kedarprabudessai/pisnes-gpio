# PiSNES-GPIO (Raspberry Pi Zero Standalone SNES Emulator)

A lightweight, low-latency fork of **PiSNES** (SNES9x 1.39) tailored for the **Raspberry Pi Zero (BCM2835 ARMv6)** running **Raspberry Pi OS (Legacy, 32-bit) Bullseye Lite**.

This fork avoids RetroArch/RetroPie overhead, runs directly from the Linux console via **pure Broadcom DispmanX hardware scaling**, and adds **direct memory-mapped GPIO button polling** (`/dev/gpiomem`) so you can wire arcade/handheld momentary pushbuttons straight to the 40-pin header alongside USB gamepads and keyboards.

---

## Key Modifications & Optimizations

1. **Pure DispmanX Hardware Video Backend (`unix/graphics.cpp`, `unix/unix.cpp`)**
   - Replaced the legacy Broadcom EGL/GLES2 backend (which broke on Raspberry Pi OS Bullseye due to missing `/opt/vc/lib/libEGL` and Mesa incompatibilities) with direct **VideoCore IV DispmanX (`VC_IMAGE_RGB565`)** hardware scaling.
   - Zero 3D-driver overhead, crisp 4:3 aspect-ratio scaling in hardware, and no `eglInitialize` errors.
2. **Direct Memory-Mapped GPIO Input (`unix/gpio_buttons.cpp`, `unix/gpio_buttons.h`)**
   - Reads `GPLEV0` directly from `/dev/gpiomem` in a single 32-bit register read (`< 100 ns`), with zero kernel polling daemon or `uinput` lag.
   - Automatically configures pins as inputs and enables BCM2835 internal pull-up resistors (`GPPUD` / `GPPUDCLK0`) on startup.
   - Integrated into both the standalone emulator (`snes9x`) and the frontend menu (`snes9x.gui`), with clean handle release before game launch.
3. **CPU & Frame-Pacing Optimization (`unix/unix.cpp`)**
   - Replaced the 100% CPU busy-wait spin loop in `S9xSyncSpeed()` with `usleep()` pacing and proper lag reset after startup/snapshot loads.
   - Compiled with Raspberry Pi Zero ARM11 tuning: `-march=armv6zk -mcpu=arm1176jzf-s -mtune=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard`.
4. **Resilient ALSA Audio Handling (`unix/unix.cpp`)**
   - Added automatic fallback from `default` to `plughw:0,0` (Raspberry Pi HDMI audio).
   - Prevented null-pointer crashes if no audio device is detected—gracefully mutes audio and continues running.
5. **Frontend & Modern GCC Compatibility (`unix/frontend.cpp`, `apu.cpp`, `Makefile`)**
   - Fixed modern GCC narrowing conversion errors in `apu.cpp`.
   - Fixed a `scandir` `free(namelist[n])` out-of-bounds bug and `joy_buttons` bounds handling (`QUIT=99`).
   - Added terminal diagnostics when `roms/` is empty or when launching games, plus support for launching ROMs with **A**, **B**, or **Start**.

---

## Recommended OS & `/boot/config.txt` Setup

Tested on **Raspberry Pi OS (Legacy, 32-bit) Bullseye Lite**. (Newer Bookworm releases removed Broadcom DispmanX userland libraries.)

Edit `/boot/config.txt` on the Pi:
```bash
sudo nano /boot/config.txt
```

Set the following options:
```ini
# Use Fake KMS so Broadcom DispmanX hardware compositor remains active on Bullseye
dtoverlay=vc4-fkms-v3d

# Allocate sufficient GPU memory for DispmanX resources
gpu_mem=128

# Enable Raspberry Pi HDMI audio
dtparam=audio=on

# Enable boot-time internal pull-ups on all default GPIO button pins
gpio=4,5,6,9,10,11,13,17,19,21,22,26,27=pu
```

Ensure your user has GPU/GPIO permissions, then reboot:
```bash
sudo usermod -a -G video,render,gpio $USER
sudo reboot
```

---

## Building from Source (on Raspberry Pi Zero)

1. Install dependencies:
   ```bash
   sudo apt-get update
   sudo apt-get install -y git build-essential libsdl1.2-dev libglib2.0-dev libasound2-dev libraspberrypi-dev libjpeg-dev zlib1g-dev
   ```
2. Clone and compile:
   ```bash
   git clone https://github.com/kedarprabudessai/pisnes-gpio.git
   cd pisnes-gpio
   make clean
   make
   ```
   This builds two binaries:
   - `./snes9x` — Standalone SNES emulator
   - `./snes9x.gui` — Lightweight ROM selection menu

---

## Adding ROMs & Running

Place `.smc`, `.sfc`, or `.zip` SNES ROMs into the `roms/` folder (for example, via `scp` from your computer):
```bash
scp /path/to/your_game.smc <user>@<pi-ip>:~/pisnes-gpio/roms/
```

Run the GUI menu:
```bash
cd ~/pisnes-gpio
./snes9x.gui
```

Or launch a ROM directly:
```bash
./snes9x "roms/Super Mario World (U) [!].smc"
```

> **Note on SSH vs Local Keyboard:** When launched over SSH, SDL reads keyboard input from the SSH terminal rather than a USB keyboard plugged into the Pi. To use a USB keyboard attached to the Pi, run the command directly on the Pi's local TV/monitor console (`tty1`). USB gamepads (`/dev/input/js0`) and GPIO buttons work from both SSH and local console.

---

## Controls & GPIO Wiring

### 1. GPIO Pinout (Direct Button-to-GND)

Connect momentary pushbuttons between each **GPIO Pin** and any **GND** pin (active-low). No external pull-up resistors are needed. See [`GPIO_WIRING_AND_BUILD.md`](GPIO_WIRING_AND_BUILD.md) for full details and pin configuration in `snes9x.cfg` (`[GPIO]` section).

| SNES Button | BCM GPIO | Physical Pin |
| :--- | :--- | :--- |
| **D-Pad UP** | GPIO 6 | Pin 31 |
| **D-Pad DOWN** | GPIO 13 | Pin 33 |
| **D-Pad LEFT** | GPIO 19 | Pin 35 |
| **D-Pad RIGHT** | GPIO 26 | Pin 37 |
| **A** | GPIO 4 | Pin 7 |
| **B** | GPIO 17 | Pin 11 |
| **X** | GPIO 27 | Pin 13 |
| **Y** | GPIO 22 | Pin 15 |
| **L** | GPIO 10 | Pin 19 |
| **R** | GPIO 9 | Pin 21 |
| **START** | GPIO 11 | Pin 23 |
| **SELECT** | GPIO 5 | Pin 29 |
| **QUIT** | GPIO 21 | Pin 40 |

Pressing **QUIT** (GPIO 21) or **START + SELECT** together exits the game or menu. Set `Enabled=0` under `[GPIO]` in `snes9x.cfg` if you want to disable GPIO polling temporarily.

### 2. USB Controller & Keyboard

- **USB Controller:** Supported out of the box via SDL (`/dev/input/js0`), pre-mapped to an Xbox 360 layout under `[Joystick]` in `snes9x.cfg`. To inspect/remap your controller's button IDs, install `joystick` (`sudo apt install -y joystick`) and run `jstest /dev/input/js0`.
- **Keyboard Defaults:**
  - **Arrow keys:** D-Pad
  - **`c` / `d` / `Enter`:** B / A / Start (any of these launches a selected ROM in `snes9x.gui`)
  - **`s` / `x` / `a` / `f`:** X / Y / L / R
  - **`Tab`:** Select
  - **`Backspace`:** Turbo
  - **`Esc`:** Quit
  - **`F1`–`F4` / `Shift + F1`–`F4`:** Quickload / Quicksave

---

## Troubleshooting

- **Black screen or DispmanX display failure on Bullseye:** Verify `dtoverlay=vc4-fkms-v3d` (not `vc4-kms-v3d`) and `gpu_mem=128` are set in `/boot/config.txt`, then reboot.
- **No HDMI audio:** Ensure `dtparam=audio=on` is in `/boot/config.txt`, run `sudo modprobe snd-bcm2835`, and verify a playback device appears with `aplay -l`.
- **GUI exits immediately after ~5 seconds:** Make sure you have at least one `.smc`, `.sfc`, or `.zip` ROM inside `~/pisnes-gpio/roms/`.

---

## Credits

- **SNES9x Team** — Original SNES9x 1.39 emulator
- **Squid (`squidrpi/pisnes`)** — Original Raspberry Pi PiSNES port
- **GPIO / DispmanX / Pi Zero Bullseye Port** — `kedarprabudessai`

# PiSNES Direct GPIO & Performance Guide for Raspberry Pi Zero

This modified version of PiSNES includes **direct memory-mapped GPIO button polling** via `/dev/gpiomem` and multiple low-level performance improvements tailored specifically for the Raspberry Pi Zero (ARMv6 BCM2835).

---

## 1. GPIO Pinout & Button Mapping

Buttons should be momentary pushbuttons connected between the designated **GPIO pin** and any **GND** pin. No external resistors are needed when internal pull-up resistors are enabled.

| SNES Button | BCM GPIO | Physical Header Pin | Ground Pin (Physical) |
| :--- | :--- | :--- | :--- |
| **D-Pad UP** | GPIO 6 | Pin 31 | Pin 30 or 34 (GND) |
| **D-Pad DOWN** | GPIO 13 | Pin 33 | Pin 34 (GND) |
| **D-Pad LEFT** | GPIO 19 | Pin 35 | Pin 39 (GND) |
| **D-Pad RIGHT** | GPIO 26 | Pin 37 | Pin 39 (GND) |
| **Button A** | GPIO 4 | Pin 7 | Pin 9 (GND) |
| **Button B** | GPIO 17 | Pin 11 | Pin 9 or 14 (GND) |
| **Button X** | GPIO 27 | Pin 13 | Pin 14 (GND) |
| **Button Y** | GPIO 22 | Pin 15 | Pin 20 (GND) |
| **L Shoulder** | GPIO 10 | Pin 19 | Pin 20 (GND) |
| **R Shoulder** | GPIO 9 | Pin 21 | Pin 25 (GND) |
| **START** | GPIO 11 | Pin 23 | Pin 25 (GND) |
| **SELECT** | GPIO 5 | Pin 29 | Pin 30 (GND) |
| **QUIT** | GPIO 21 | Pin 40 | Pin 39 (GND) |

> **Note:** Pressing **START + SELECT** simultaneously will also exit the emulator back to the console or GUI.

---

## 2. Enabling Internal Pull-Up Resistors

To ensure inputs don't float without requiring physical resistors, add this single line to `/boot/config.txt` (or `/boot/firmware/config.txt` on newer Raspberry Pi OS releases):

```ini
gpio=4,5,6,9,10,11,13,17,19,21,22,26,27=pu
```

---

## 3. Configuration (`snes9x.cfg`)

The `[GPIO]` section in `snes9x.cfg` allows custom pin assignments or disabling GPIO input:

```ini
[GPIO]
Enabled=1
UP=6
DOWN=13
LEFT=19
RIGHT=26
A=4
B=17
X=27
Y=22
L=10
R=9
START=11
SELECT=5
QUIT=21
```

---

## 4. Building on Raspberry Pi Zero

### Step 1: Install Build Dependencies
On the Pi Zero (Raspberry Pi OS / DietPi):
```bash
sudo apt-get update
sudo apt-get install -y build-essential libsdl1.2-dev libglib2.0-dev libasound2-dev libraspberrypi-dev libjpeg-dev zlib1g-dev
```

### Step 2: Compile
In the repository directory:
```bash
make clean
make
```

This compiles both:
* `snes9x`: The core standalone emulator.
* `snes9x.gui`: The lightweight graphical ROM selector.

### Step 3: Run
```bash
# Direct ROM launch
./snes9x /path/to/game.smc

# Or launch the ROM selector menu
./snes9x.gui
```

---

## 5. Summary of Optimizations Added

1. **Sub-millisecond GPIO Input:** Reads `GPLEV0` directly from the BCM2835 register map via `/dev/gpiomem` in $< 100 \text{ ns}$.
2. **CPU Spin-Wait Elimination:** Fixed the busy-wait loop in `S9xSyncSpeed` that previously pegged the Pi Zero CPU at 100%.
3. **OpenGL Pipeline Optimization:** Removed redundant per-frame `glTexParameteri` texture filter resets in `gles2_DrawQuad`.
4. **Compiler Tuning:** Targeted `-march=armv6zk -mcpu=arm1176jzf-s -mtune=arm1176jzf-s` for the ARM11 8-stage pipeline.

# gpioctl

Command-line GPIO control for the **Raspberry Pi 5**, using **physical header pin numbers** (1–40).

- Turn pins on/off/toggle, set input/output with pull-up/down
- Read a pin once or watch it for changes
- Timer-based triggers (delay, pulse length, repeat, interval)
- `list` shows all 40 header pins with type, alternate function and live mode/level
- Written in plain C, no extra libraries. Uses `pinctrl` (preinstalled on Raspberry Pi OS Bookworm), so pin states persist after the program exits

## Install

### Prebuilt binary (recommended)
Download from the [Releases](../../releases) page. The binaries are static, so they run on any Raspberry Pi OS version.

| Raspberry Pi OS | File |
|---|---|
| 64-bit (default on Pi 5) | `gpioctl-linux-aarch64` |
| 32-bit | `gpioctl-linux-armhf` |

```bash
# On the Pi (64-bit OS). Replace v1.0.0 with the latest release tag.
wget https://github.com/itsnavin/gpioctl/releases/download/v1.0.0/gpioctl-linux-aarch64
sudo install -m 755 gpioctl-linux-aarch64 /usr/local/bin/gpioctl
gpioctl --version
```
Check which OS you have with `uname -m` (`aarch64` = 64-bit, `armv7l` = 32-bit).

### Build from source on the Pi
```bash
sudo apt install build-essential
make
sudo make install          # installs to /usr/local/bin/gpioctl
```
Or without make: `gcc -O2 -Wall -o gpioctl gpioctl.c`

### Cross compile from a Linux PC
```bash
sudo apt install gcc-aarch64-linux-gnu gcc-arm-linux-gnueabihf
make CROSS_COMPILE=aarch64-linux-gnu-                 # 64-bit Pi OS
make CROSS_COMPILE=aarch64-linux-gnu- STATIC=1        # static binary
make CROSS_COMPILE=arm-linux-gnueabihf-               # 32-bit Pi OS
make release                                          # both static builds + checksums in ./dist
```

### Requirements
- Raspberry Pi 5 with Raspberry Pi OS Bookworm
- `pinctrl` (`sudo apt install raspi-utils` if missing)
- Run as a user in the `gpio` group, or use `sudo`

## Usage

`<pin>` is always the **physical header pin number** (1–40). Options may come before or after the pin. Exit code is `0` on success and `1` on error.

```
gpioctl on <pin>
gpioctl off <pin>
gpioctl toggle <pin>
gpioctl mode <pin> in|out [--pull up|down|none]
gpioctl read <pin> [-w|--watch]
gpioctl status [pin]
gpioctl list
gpioctl timer <pin> -d <sec> [--delay <sec>] [--state on|off]
                    [-r <count, 0=forever>] [-i <sec>]
gpioctl --version
gpioctl --help
```

### `on` / `off` / `toggle`
Sets the pin as output and drives it HIGH/LOW, or flips its current level.
```
$ gpioctl on 11
Pin 11 (GPIO17) -> ON
$ gpioctl toggle 11
Pin 11 (GPIO17) -> OFF
```

### `mode <pin> in|out [--pull up|down|none]`
| Argument | Meaning |
|---|---|
| `out` | Output, starts OFF |
| `in` | Input |
| `--pull up` / `down` / `none` | Internal pull resistor for inputs (default `none`) |
```
$ gpioctl mode 15 in --pull up
Pin 15 (GPIO22) set as INPUT (pull up)
```

### `read <pin> [-w|--watch]`
Prints `0` or `1`. With `--watch`, checks every 50 ms and prints a timestamped line on each change until Ctrl+C.
```
$ gpioctl read 15
1
$ gpioctl read 15 --watch
Watching pin 15 (GPIO22). Ctrl+C to stop.
09:40:28  Pin 15 = 1
09:40:29  Pin 15 = 0
```

### `status [pin]`
Compact table of one GPIO pin, or all 28 GPIO pins.
```
$ gpioctl status 11
 PIN   GPIO  MODE  LEVEL
  11 GPIO17    op    LOW
```

### `list`
All 40 header pins with type, alternate function and live mode/level (power/GND rows show `—`).
```
$ gpioctl list
PIN  GPIO    TYPE  FUNCTION         MODE  LEVEL
---- ------- ----- ---------------- ----- -----
1    —       3.3V  Power            —     —
2    —       5V    Power            —     —
3    GPIO2   GPIO  I2C1 SDA         ip    LOW
...
40   GPIO21  GPIO  PCM DOUT         ip    LOW
```

### `timer <pin> -d <sec> [options]`
| Option | Default | Meaning |
|---|---|---|
| `-d`, `--duration <sec>` | required | Time in the triggered state (decimals allowed) |
| `--delay <sec>` | `0` | Wait before the first trigger |
| `--state on\|off` | `on` | Triggered state (`off` = active-low, e.g. relay boards) |
| `-r`, `--repeat <n>` | `1` | Number of cycles, `0` = forever |
| `-i`, `--interval <sec>` | `1` | Rest time between cycles |

Ctrl+C or `kill` always returns the pin to its resting state.
```
$ gpioctl timer 11 -d 1
Pin 11 (GPIO17) -> ON for 1s
Pin 11 -> OFF  (cycle 1)
Timer finished.

$ gpioctl timer 11 -d 0.5 -i 0.5 -r 0     # blink forever
```

### Errors
```
$ gpioctl on 1
Error: Physical pin 1 is 3.3V, not a GPIO.
$ gpioctl on 41
Error: Invalid physical pin '41'. Use 1-40.
$ gpioctl on 27
Warning: pins 27/28 (GPIO0/1) are reserved for HAT EEPROM.
```

## Project layout
| File | Purpose |
|---|---|
| `gpioctl.c` | CLI and `pinctrl` backend |
| `gpio_pins.h` | Physical pin → GPIO map and 40-pin info table |
| `Makefile` | Native, cross and release builds |
| `contrib/github-release.yml` | Optional GitHub Actions workflow (see below) |

## Making a release
Build the binaries locally and attach them to a GitHub release:
```bash
make release VERSION=1.0.1      # creates ./dist with binaries, tarballs and SHA256SUMS
```

To automate this, enable the bundled GitHub Actions workflow:
```bash
mkdir -p .github/workflows
git mv contrib/github-release.yml .github/workflows/release.yml
git commit -m "Enable release workflow" && git push
git tag v1.0.1 && git push origin v1.0.1    # builds and publishes the release
```

## Safety
GPIO pins are **3.3 V only** and can supply only a few mA. Use a transistor or relay module for relays, motors or LED strips.

## License
MIT

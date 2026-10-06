# M5Dial Servo PWM Controller

PlatformIO/Arduino firmware for the M5Stack M5Dial that drives a standard
hobby servo (0-100% output, mapped to an **850us-1750us** pulse at 50Hz) via
Port B, and runs an **automated servo test** that checks for sticking using
an INA219 current sensor.

Pressing the ring button starts the automated test (see "Automated test
sequence" below): the servo steps down to the bottom limit, dwells, steps up
to the top limit, dwells, and the arc is painted with red/amber/green current-draw
bands plus a "STICKING"/"OK" verdict at each end. If neither end is sticking,
the display shows "OK TO FIT CAMERA" at the end. Outside a test, the servo
can still be positioned manually with the rotary ring (speed-based
acceleration) or by dragging the on-screen arc.

> The ring button no longer resets output to a default % - that function has
> been replaced by starting the test.

## Hardware

- **Board:** M5Dial (ESP32-S3FN8, 8MB flash, no PSRAM, 240x240 round
  GC9A01 touchscreen, FT3267 touch controller).
- **Servo signal:** Port B pin **G1 = GPIO1**. Connect the servo's signal
  wire there, and power/ground from Port B's 5V/GND pins. A single micro or
  standard servo should be fine off the M5Dial's regulator; if it browns out
  or resets under load, power the servo from a separate 5V supply and just
  share ground.
- **Rotary ring (encoder):** GPIO41/GPIO40, quadrature, 16 detents/rev (4
  quadrature counts per detent).
- **Ring push-button:** `M5Dial.BtnA`, physically GPIO42 (pressing the dial
  down). Starts the automated test.
- **Current sensor:** INA219 on Port A (I2C: SDA = GPIO13, SCL = GPIO15),
  address 0x40, monitoring the servo's supply current. Shown near the top
  of the display. See `include/config.h` for the shunt resistance / max
  current calibration inputs if using a non-standard breakout.
- Reference: [M5Dial docs](https://docs.m5stack.com/en/core/M5Dial),
  [M5Unified](https://github.com/m5stack/M5Unified),
  [M5GFX](https://github.com/m5stack/M5GFX).

## Framework choice

The M5Dial/M5Unified/M5GFX libraries only support the **Arduino** framework
(their `library.json` declares `"frameworks": ["arduino"]`), so this project
uses PlatformIO with `framework = arduino` rather than raw ESP-IDF. Arduino
itself runs on top of ESP-IDF under the hood. There is no official
`m5stack-dial` PlatformIO board entry, so `platformio.ini` targets the
generic `esp32-s3-devkitc-1` board with explicit flash size / no-PSRAM
overrides that match the M5Dial's actual hardware.

## Project structure

```
include/
  config.h       Pin assignments and all tunable constants (servo pulse
                  range, acceleration curve, current thresholds, test
                  timings)
  lv_conf.h       LVGL configuration (color depth, fonts, memory)
src/
  main.cpp        setup()/loop(), wires input sources to the servo + UI
  display_driver. Bridges LVGL <-> M5GFX (flush callback) and
    h / .cpp       M5Dial's touch controller (LVGL input device)
  servo.h / .cpp  LEDC-based 50Hz PWM driver, percent -> pulse width
  encoder_input.  Reads the rotary ring, applies the acceleration
    h / .cpp       curve, returns a percent delta per poll
  ina219.h / .cpp INA219 current sensor driver (Port A, I2C via M5Unified's
                  Ex_I2C), polled from main and shown on the display
  current_level.h Green/amber/red classification of a current reading
  test_sequence.  Automated test state machine: steps, dwells, per-step
    h / .cpp       and dwell peak tracking, STICKING/OK verdicts. main feeds
                  it INA219 readings and handles sensor faults
  ui.h / .cpp     Builds the 180-degree arc gauge, the two custom-drawn test
                  bands, verdict/message labels and readouts, and reports
                  touch-drag changes back to main via a callback
```

## Key design decisions

- **Servo travel limits:** bottom (0%) = **850us**, top (100%) =
  **1750us** (`SERVO_PULSE_MIN_US` / `SERVO_PULSE_MAX_US`). Previously
  600us-2000us in code (and 1000-2000us in older versions of this document).
- **Startup value:** always **100% (top limit, 1750us)** on boot, with no
  persistence to flash - avoids flash wear / stale-state surprises on a
  device that moves a physical mechanism. 100% is also where the test
  starts. "OK TO FIT CAMERA" is *not* shown after boot, only after a test
  has passed.
- **Rotation direction:** clockwise increases % by default. If it's reversed
  on your unit, flip `ENCODER_INVERT_DIRECTION` in `include/config.h`.
- **Touch input:** the arc gauge is an LVGL `lv_arc` driven by a custom
  screen-level touch handler (LVGL 8.4's built-in arc dragging is broken for
  a 180-360 deg sweep - see `ui.cpp`). Dragging sets the target percent.
- **Acceleration:** encoder step size per detent scales with how fast the
  ring is turned (`ENCODER_ACCEL_TABLE` in `include/config.h`) - currently
  3%/detent for slow turns up to 40%/detent for very fast flicks.
- **PWM:** ESP32 LEDC peripheral, 50Hz carrier, 12-bit duty resolution
  (~4.9us pulse-width granularity), configured once in `servo_init()`.
- **Current colour bands** (one set of thresholds in `config.h`, used for
  the live mA readout at the top of the screen, the arc bands, and the
  end-of-travel verdicts):

  | Current                                | Colour |
  |----------------------------------------|--------|
  | below `CURRENT_AMBER_MA` (200mA)       | green  |
  | `CURRENT_AMBER_MA` (200mA) up to 399mA | amber  |
  | `CURRENT_RED_MA` (400mA) and above     | red    |

  The live readout used to show white below 200mA; it now shows green.

## Automated test sequence

Started by pressing the ring button (`BtnA`). Timings, the sample interval
and the current thresholds are all in `include/config.h`
(`TEST_PREPOSITION_MS = 1000`, `TEST_STEP_COUNT = 5`,
`TEST_STEP_SETTLE_MS = 1000`, `TEST_STEP_READ_WINDOW_MS = 200`,
`TEST_STEP_GAP_MS = 100`, `TEST_DWELL_MS = 2000`,
`TEST_DWELL_SETTLE_MS = 1000`, `TEST_CURRENT_POLL_MS = 20`,
`CURRENT_AMBER_MA = 200`, `CURRENT_RED_MA = 400`).

1. **Start (button press).**
   - Clear the previous test's bands, verdicts, fault message and "OK TO
     FIT CAMERA" message.
   - Re-initialise the INA219 and take a reading. Re-initialising means a
     sensor connected after boot is picked up, and one that lost its
     calibration in a power blip (it would then read a false 0mA) is
     recalibrated.
   - If that fails, the test does **not** start. A **"SENSOR FAULT"**
     message is shown (red) and **stays on screen** until a later button
     press finds the sensor working and starts a test. Manual control keeps
     working.
   - Otherwise the test starts from the **top position (100%)**. If the
     servo isn't already at 100%, immediately set the output to 100% and
     **wait 1 s** before starting to step down.
2. **Step down** from 100% to 0% in **5 discrete steps**: 80%, 60%, 40%,
   20%, 0%. At each step:
   1. Send the servo straight to the step's position (no ramping).
   2. **Pause 1 s**.
   3. **Take a reading:** the **peak** current over the last 200 ms of that
      pause (the INA219 is read every 20 ms during a test, so about 10
      samples). Colour the 20% of the arc travelled in that step (green /
      amber / red, per the colour table above). If there were no readings
      at all, the segment is drawn dark grey.
   4. **Pause 100 ms**, then move to the next step.

   Stepping down takes about 5.5 s.
3. **Dwell at the bottom (850us) for 2 s**, straight after the last step's
   100 ms pause. The first 1 s lets the servo settle and isn't measured. Record the **peak** current over the **last
   1 s**, then show a verdict below the bottom (left) end of the arc:
   - below 200mA: **"OK"** in green
   - 200mA-399mA: **"STICKING"** in amber
   - 400mA and above: **"STICKING"** in red
4. **Step up** from 0% to 100% the same way: 20%, 40%, 60%, 80%, 100%.
   The step-up colours go in a **second band inside the step-down band**
   (closer to the centre).
5. **Dwell at the top (1750us) for 2 s**, record the peak over the last
   1 s and show the verdict below the top (right) end of the arc, using the
   same rules as step 3.
6. **End.** The servo stays at the top limit.
   - If **both** ends are OK: show **"OK TO FIT CAMERA"** in bright green.
     This replaces the old "OK to Fit Bracket" message.
   - If **either** end is STICKING: do not show "OK TO FIT CAMERA".

**During the test:**
- Button presses, ring turns and touch drags are **ignored**.
- The arc's normal position indicator (fill + knob) stays visible and
  follows the servo as it moves.
- The live mA readout keeps updating (still refreshed every 200 ms).
- If the INA219 stops responding partway through, the test **stops
  immediately**. That's when every read has failed for
  `INA219_STALE_TIMEOUT_MS` (2 s, the same rule as the live readout), or
  when a dwell's 1 s measuring window got no readings. The servo holds its
  current position, the "SENSOR FAULT" message is shown, no further
  verdicts or "OK TO FIT CAMERA" are shown, and manual control comes back.
  Any bands or verdict already drawn stay on screen.

**After the test:**
- Manual control (ring and touch) comes back.
- The coloured bands and STICKING/OK labels **stay on screen until the
  operator presses the button again**, which starts a new test.
- If the test passed, "OK TO FIT CAMERA" is hidden while the servo is
  anywhere other than the top limit. It shows again when the operator
  returns the servo to 100% (1750us).
- A failed test never shows the message, wherever the servo is moved.

**Screen layout** (240px round display, `ui.cpp`): main arc radius 93-111px.
The step-down band is at 79-86px and the step-up band at 70-77px, which
keeps them just inside the arc's knob. The bands are custom-drawn on one
LVGL object, and only the new segment is redrawn each time. The live mA
readout sits 15px below the inner band, so the top of the screen isn't
cramped. Below it, from top to bottom: the big %, the "SERVO OUTPUT"
caption and the pulse readout (y ~92-182). The verdict labels are 52px
below the arc's end points, clear of the caption and the pulse readout.
"OK TO FIT CAMERA" / "SENSOR FAULT" appear near the bottom (y ~186-202).

## Build / flash

```
pio run              # build
pio run -t upload     # flash over USB-C
pio device monitor     # serial console (115200 baud)
```

Last verified clean build (0 errors, 0 warnings): RAM 35.7% (116836/327680
bytes), Flash 23.5% (785639/3342336 bytes).

`pio` isn't on PATH on this machine - run it as
`C:\pio\penv\Scripts\pio.exe`.

### Toolchain note (Windows)

`platformio.ini` points `platform` at the
[pioarduino](https://github.com/pioarduino/platform-espressif32) fork
instead of the official PlatformIO registry `espressif32` platform: the
official one is stuck on Arduino-ESP32 core 2.0.x, which lacks the pin-based
`ledcAttach()`/`ledcWrite()` API and full C++17 support (`std::clamp`) that
this codebase uses. pioarduino requires Git on PATH to fetch its framework
packages.

On Windows, pioarduino's combined esp32-arduino-libs archive (all chip
variants bundled together) contains paths deep enough to exceed the default
260-character `MAX_PATH`, which breaks extraction if PlatformIO's core dir
is the default `%USERPROFILE%\.platformio`. This machine has
`PLATFORMIO_CORE_DIR` set to `C:\pio` (a short path) to work around it - if
setting up a new machine and hitting a `FileNotFoundError` during
`esp32-arduino-libs` extraction, either do the same or enable Windows
long-path support instead.

The project directory itself lives on a Google Drive sync mount (`G:\My
Drive\...`), whose virtual filesystem driver adds heavy per-file latency to
the many small stat/timestamp checks SCons does on every build - a no-op
incremental build there took ~26s instead of the ~1-2s typical on local
disk, and a real single-file edit-compile-link cycle took ~50-70s. Bulk
sequential I/O (e.g. `robocopy`) is not the bottleneck - a 350MB copy of the
existing build cache off the drive took under 3 seconds - it's specifically
the volume of small metadata operations across hundreds of object/header
files that's slow through the cloud-sync layer. `platformio.ini`'s
`[platformio]` section redirects `workspace_dir` (build output + downloaded
libraries) to `C:/pio/workspace/m5dial-servo-pwm` and `build_cache_dir` to
`C:/pio/build_cache`, leaving only source/config on Google Drive; this cut
a real edit-compile-link cycle to ~13-14s. Like `PLATFORMIO_CORE_DIR`, this
is machine-specific - adjust or remove that section on another machine.

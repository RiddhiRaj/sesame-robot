# Sesame wiring plan: Waveshare ESP32-S3-Zero + PCA9685

This is a custom wiring plan for building Sesame with the parts currently
available:

- Waveshare ESP32-S3 mini board (assumed to be the **ESP32-S3-Zero or
  ESP32-S3-Zero-M**)
- PCA9685 16-channel PWM/servo controller (the board described as the "PCA PWM
  motor controlling module")
- DC-DC buck converter
- DC-power-to-USB-A adapter

It is based on the repository as inspected on 2026-07-26. It does **not** apply
unchanged to another Waveshare S3 product, an unbranded "S3 Super Mini", or a
DC motor driver that merely has a similar name.

## Read this conclusion first

The recommended hardware arrangement is:

```text
DC source
   |
   +-- fuse (recommended) -- power switch -- buck converter set to 5.0 V
                                               |
                                               +-- 5 V --> PCA9685 V+ terminal
                                               |             |
                                               |             +--> 8 servos
                                               |
                                               +-- 5 V --> Waveshare 5V pin

Waveshare 3V3 ------------------------------------> PCA9685 VCC
Waveshare GPIO8 (SDA) ----------------------------> PCA9685 SDA
                      +----------------------------> OLED SDA
Waveshare GPIO9 (SCL) ----------------------------> PCA9685 SCL
                      +----------------------------> OLED SCL
Waveshare 3V3 ------------------------------------> OLED VCC

All grounds: DC/buck GND = Waveshare GND = PCA9685 GND = OLED GND
```

The PCA9685 route is electrically cleaner than sending eight servo signals
directly from the ESP32, and it gives the servos a proper power-distribution
header. However, **the firmware in this repository does not presently support
the PCA9685**. It uses `ESP32Servo` and eight direct GPIOs. Both
`sesame-firmware-main.ino` and `sesame-motor-tester.ino` must be adapted to use
a PCA9685 library before this recommended wiring can move a servo.

Do not complete permanent soldering until the board identity, power source,
buck rating, and PCA board labels have all been checked.

## 1. Confirm the exact boards

### Waveshare controller

This guide assumes the board is marked **ESP32-S3-Zero** or
**ESP32-S3-Zero-M**. Compare it with the official pinout:

- [Waveshare ESP32-S3-Zero product documentation](https://docs.waveshare.com/ESP32-S3-Zero)
- [Official pinout image](https://docs.waveshare.com/assets/images/ESP32-S3-Zero-Pinout-10a4868b32f9da9005e87b2d9d84574e.webp)
- [Official schematic](https://files.waveshare.com/wiki/ESP32-S3-Zero/ESP32-S3-Zero-Sch.pdf)

Important facts from Waveshare:

- External power goes into the pad marked `5V`; its accepted input is
  3.7-6 V.
- The board's 3.3 V regulator is rated for at most 800 mA. It is for the ESP32
  and low-power logic, **not the servos**.
- GPIO21 is already connected to the onboard WS2812 RGB LED.
- GPIO33-GPIO37 are not broken out and are reserved for PSRAM.
- GPIO43/TX and GPIO44/RX are the default serial pins.
- The board uses native USB and may need BOOT held while connecting USB to
  enter download mode.

Stop and find the board's exact product page if its silkscreen says anything
else.

### PCA board

This guide assumes a common **PCA9685 16-channel, 12-bit PWM/servo driver**
breakout with these labels:

- logic/header side: `GND`, `OE`, `SCL`, `SDA`, `VCC`, `V+`
- separate two-pin servo-power terminal: `V+` and `GND`
- servo headers numbered 0-15, with rows marked signal/PWM, `V+`, and `GND`

If the module is an L298N, TB6612, PCA995x, or any board without the labels
above, it is not the module covered here.

`VCC` and `V+` are different:

- `VCC` powers the PCA9685 logic and its I2C pull-ups. Connect it to ESP32
  **3V3**.
- `V+` supplies the servos. Connect it to the external regulated **5 V servo
  rail**.

Never bridge `VCC` to `V+`.

## 2. Parts still needed

The parts listed in the request are not enough for a complete Sesame. At
minimum, check or obtain:

| Part | Need | Notes |
| --- | ---: | --- |
| MG90S 180-degree metal-gear micro servos | 8 | The repository suggests buying 10 so there are spares. |
| 0.96-inch 128x64 SSD1306 I2C OLED | 1 | Expected address is `0x3C`. Stock firmware deliberately stops during setup if it cannot initialize the display. |
| Regulated power source for the buck input | 1 | Battery or DC supply; voltage must be inside the buck's input range. |
| Buck converter capable of 5 V at 3 A continuously | 1 | 3 A is the repository minimum, not merely a brief advertised peak rating. More current margin is desirable. |
| 1000 uF electrolytic capacitor, 10 V or higher | 1 | Strongly recommended across PCA `V+` and GND near the servo headers. Observe polarity. |
| Multimeter | 1 | Mandatory for setting the buck and checking polarity before connecting electronics. |
| 22 AWG silicone wire | as needed | 5 V and ground trunk wiring. |
| 30 AWG wire | as needed | SDA, SCL, and other low-current signals. |
| Proper connectors/headers or small protoboard | as needed | Do not depend on twisted bare wires. |
| USB-A-to-USB-C **data** cable | 1 | Needed to flash the Waveshare board; some USB cables are charge-only. |
| Power switch, fuse, heat-shrink, zip ties | as needed | The switch is optional for bench testing but recommended in the robot. |
| Printed shell, servo hardware, and screws | full set | See the repository BOM and build guide. |

Do not substitute continuous-rotation servos for the required positional
180-degree servos.

## 3. Decide whether the available power hardware is actually suitable

The words "buck converter" and "DC power to USB-A adapter" do not establish
their voltage, current, polarity, or topology.

Before wiring, read the labels or datasheets and record:

```text
DC source output:       _____ V DC, _____ A
Buck input range:       _____ to _____ V
Buck continuous output: _____ V, _____ A
USB-A adapter output:   _____ V, _____ A
```

Rules:

1. The source voltage must be within the buck converter's stated input range.
2. The source and buck must both supply at least the repository's 5 V/3 A
   requirement.
3. A basic buck converter cannot produce a stable 5 V from a 5 V input; it
   needs adequate voltage headroom. Check its specification.
4. Adjust the buck to **5.0 V with a multimeter before connecting the ESP32,
   PCA9685, OLED, or servos**.
5. A USB-A adapter is not automatically a servo supply. Do not route eight
   servos through the ESP32's USB connector or 3.3 V regulator.
6. Never connect raw battery voltage to the PCA9685 servo `V+`, ESP32 `5V`,
   ESP32 `3V3`, or OLED.
7. Do not charge lithium cells through this wiring. Use a charger intended for
   the exact cell chemistry and pack configuration.

The DC-to-USB-A adapter can be set aside for the first build. It is useful for
powering the ESP32 through USB only if it provides regulated 5 V and has a
suitable USB-A-to-USB-C cable. It does not replace the heavy 5 V connection
from the buck to the PCA9685 servo-power terminal.

## 4. Chosen ESP32-S3-Zero pins

The ESP32-S3 can route I2C through its GPIO matrix, so this plan deliberately
uses:

| Function | Waveshare pin | Why |
| --- | --- | --- |
| I2C SDA | GPIO8 | Exposed, ordinary GPIO, and not assigned to an onboard device on this board. |
| I2C SCL | GPIO9 | Exposed, ordinary GPIO, and not assigned to an onboard device on this board. |
| PCA logic supply | 3V3 | Keeps I2C pull-ups and PWM logic at ESP32-safe 3.3 V. |
| Controller input supply | 5V | Accepts the regulated 5 V branch from the buck. |
| Common reference | GND | Must join every supply and signal ground. |

Pins intentionally avoided:

- GPIO0, GPIO3, GPIO45, GPIO46: ESP32-S3 boot-strapping pins
- GPIO19, GPIO20: native USB/JTAG
- GPIO21: Waveshare onboard RGB LED
- GPIO33-GPIO37: unavailable/reserved for PSRAM
- GPIO43, GPIO44: default serial TX/RX

The Waveshare silkscreen number is the ESP32 **GPIO number**, not an Arduino
header position such as D8. Connect to pads marked `8` and `9`.

## 5. Exact low-voltage wiring

Make these connections with all power disconnected:

| From | To | Wire |
| --- | --- | --- |
| Waveshare `3V3` | PCA9685 `VCC` | logic power |
| Waveshare `GND` | PCA9685 logic-side `GND` | common ground |
| Waveshare GPIO8 | PCA9685 `SDA` | I2C data |
| Waveshare GPIO9 | PCA9685 `SCL` | I2C clock |
| Waveshare GPIO8 | OLED `SDA` | shared I2C data |
| Waveshare GPIO9 | OLED `SCL` | shared I2C clock |
| Waveshare `3V3` | OLED `VCC` | safest default for an unknown SSD1306 breakout |
| Waveshare `GND` | OLED `GND` | common ground |

Leave PCA9685 `OE` unconnected unless the particular module documentation says
otherwise. Common breakouts pull it low, enabling outputs. If the module does
not have that pull-down, connect `OE` to GND.

The SSD1306 at `0x3C` and a default PCA9685 at `0x40` can share this bus. Leave
all PCA9685 address solder jumpers open for address `0x40`.

## 6. Exact power wiring

Use 22 AWG wire for the main 5 V and ground paths. Keep the buck-to-PCA wires
short.

With the source disconnected:

1. Source positive -> fuse -> switch input.
2. Switch output -> buck `IN+`.
3. Source negative -> buck `IN-`.
4. Buck `OUT+` -> PCA9685 servo-power terminal `V+`.
5. Buck `OUT-` -> PCA9685 servo-power terminal GND.
6. Branch buck `OUT+` -> Waveshare `5V`.
7. Branch buck `OUT-` -> Waveshare `GND`.
8. Install the electrolytic capacitor across PCA9685 `V+` and GND:
   capacitor `+` to `V+`, capacitor stripe/`-` to GND.

The PCA9685 logic GND and servo-terminal GND normally share the same copper on
the module, but wire the grounds explicitly as shown. A signal has no valid
reference without common ground.

### USB programming configuration

For the safest initial test:

- Power the Waveshare from the computer's USB-C cable.
- Power PCA `V+` and the servos from the buck.
- Join buck/PCA GND to Waveshare GND.
- Temporarily leave the buck-to-Waveshare `5V` branch disconnected.

This prevents accidental back-feeding between the computer USB supply and the
buck. After programming, disconnect USB before restoring the buck-to-Waveshare
`5V` branch for standalone operation.

## 7. Servo connectors and channel order

For a typical MG90S:

- brown or black = GND
- red = 5 V / `V+`
- orange, yellow, or white = PWM signal

Check the markings printed beside the PCA9685 headers rather than assuming
their row order. Reversing a servo plug can damage it.

Use this exact channel order because it matches
`firmware/movement-sequences.h`:

| PCA channel | Firmware name | Printed joint group |
| ---: | --- | --- |
| 0 | R1 | right hip joint |
| 1 | R2 | right hip joint |
| 2 | L1 | left hip joint |
| 3 | L2 | left hip joint |
| 4 | R4 | right leg joint |
| 5 | R3 | right leg joint |
| 6 | L3 | left leg joint |
| 7 | L4 | left leg joint |

The numbering is not a simple R1-R4/L1-L4 sequence. Label every servo lead
before installing it. Use the repository's
`docs/build-guide/assets/sesame-angle-guide.png` to identify the precise
position and orientation on the assembled robot.

## 8. Firmware consequence of choosing PCA9685

The repository currently declares:

```cpp
Servo servos[8];
const int servoPins[8] = {1, 2, 4, 6, 8, 10, 13, 14};
```

It then calls `servos[i].attach(...)` and `servos[channel].write(...)`. Those
calls cannot control a PCA9685 over I2C. Merely changing `servoPins` will not
work.

A PCA-compatible firmware variant must:

1. Set `I2C_SDA` to 8 and `I2C_SCL` to 9.
2. Add a PCA9685 driver library, normally `Adafruit PWM Servo Driver Library`.
3. Initialize the PCA9685 at default I2C address `0x40` and 50 Hz.
4. Replace ESP32Servo attachment with PCA channel initialization.
5. Replace `servos[channel].write(adjustedAngle)` inside `setServoAngle()` with
   angle-to-pulse conversion and a PCA channel write.
6. Adapt the debugging motor tester too. The stock tester is also direct-GPIO
   only.
7. Preserve the repository channel order `0..7 =
   R1,R2,L1,L2,R4,R3,L3,L4`.

The existing firmware uses approximately 732-2929 microsecond pulses over
0-180 degrees. At 50 Hz on a 12-bit PCA9685, that is approximately count
150-600. These wide endpoints must be treated as provisional: begin at 90
degrees with the horns detached and reduce the range if any servo buzzes,
stalls, or strikes a mechanical stop.

Do not connect all eight installed joints and then discover the firmware has
not been ported. First use a PCA-specific tester with one loose servo.

## 9. Direct-GPIO fallback (works with the firmware architecture)

If PCA firmware is not ready, omit the PCA9685 and use a protoboard with
separate 5 V/GND servo rails, following the repository's hand-wired approach.
For the Waveshare ESP32-S3-Zero, a conflict-free proposed mapping is:

```cpp
#define I2C_SDA 8
#define I2C_SCL 9
const int servoPins[8] = {1, 2, 4, 5, 6, 7, 10, 11};
```

This retains the same channel order:

```text
GPIO1=R1, GPIO2=R2, GPIO4=L1, GPIO5=L2,
GPIO6=R4, GPIO7=R3, GPIO10=L3, GPIO11=L4
```

Only servo **signal** wires go to those GPIOs. Servo red wires still go to the
external 5 V rail and servo brown/black wires go to the common ground rail.
Never power a servo from `3V3`.

This mapping is a repository-specific proposal, not a mapping published by
Waveshare. It has been selected from exposed ordinary GPIOs while avoiding the
board and ESP32-S3 conflicts listed above.

## 10. Safe assembly and test order

Follow this order; do not start with all eight servos connected.

### A. Test the power system alone

1. Disconnect every electronic board.
2. Check resistance/continuity between the future 5 V and GND rails. They must
   not be shorted.
3. Connect the DC source to the buck.
4. Set the buck output to 5.0 V using the multimeter.
5. Power-cycle it and confirm polarity and voltage again.
6. If possible, verify the output does not collapse under a suitable test
   load.
7. Disconnect the source.

### B. Test controller and I2C without servos

1. Wire Waveshare, PCA logic, and OLED as in section 5.
2. Leave PCA servo `V+` disconnected.
3. Power the Waveshare through USB.
4. In Arduino IDE, select `ESP32-S3-Zero` if available; otherwise follow
   Waveshare's current board instructions. Enable **USB CDC On Boot**.
5. Run an I2C scanner using GPIO8/GPIO9.
6. Confirm both `0x3C` (OLED) and `0x40` (PCA9685) appear.
7. If only one appears, turn power off and check SDA/SCL, VCC, address jumpers,
   and common ground.

### C. Test one loose servo

1. Keep every horn/joint detached.
2. Connect one known-good servo to PCA channel 0 with polarity checked.
3. Apply external 5 V to PCA `V+`.
4. Use PCA-compatible test firmware to command only 90 degrees.
5. Check for smooth centering, no excessive buzzing, no hot wiring, and stable
   ESP32 operation.
6. Test a limited movement around 90 degrees before attempting the full range.
7. Repeat for all servos one at a time and label them.

### D. Assemble and calibrate

1. Install and route motors according to `docs/build-guide/README.md`.
2. With horns still detached, center all eight channels.
3. Attach joints only while servos are holding their documented calibration
   position.
4. Test Rest and Stand before walking.
5. Start with a higher `motorCurrentDelay` if the supply browns out.

Immediately switch off if the ESP32 resets, the OLED flickers, a servo stalls,
wires or connectors heat up, or the buck voltage falls substantially.

## 11. Pre-power checklist

- [ ] Controller silkscreen is ESP32-S3-Zero or ESP32-S3-Zero-M.
- [ ] PCA board really is a PCA9685 servo breakout.
- [ ] Buck input range includes the DC source voltage.
- [ ] Source and buck are rated for at least 5 V/3 A continuous output.
- [ ] Buck output measures 5.0 V with correct polarity.
- [ ] PCA `VCC` is connected to 3V3, not servo `V+`.
- [ ] PCA servo `V+` is connected to regulated 5 V, not raw battery voltage.
- [ ] OLED is connected to 3V3.
- [ ] GPIO8 is SDA and GPIO9 is SCL in both wiring and firmware.
- [ ] Every ground is common.
- [ ] No servo is powered from the ESP32 3V3 pin.
- [ ] Electrolytic capacitor polarity is correct.
- [ ] Servo plugs match the PCA's printed signal/V+/GND rows.
- [ ] No exposed conductor can touch another pad when the cover is fitted.
- [ ] Horns and joints are detached for first centering.
- [ ] USB and external 5 V are not accidentally back-feeding each other.

## 12. Why this differs from the repository's original diagram

The original DIY wiring diagram is for a Lolin ESP32-S2 Mini and directly
assigns eight GPIOs to eight servos. Its pin numbers cannot be copied onto a
Waveshare ESP32-S3-Zero. The custom Sesame distro-board pin maps also apply
only to those PCBs.

The repository otherwise establishes the relevant system requirements:

- eight 180-degree MG90S servos
- one 128x64 SSD1306 OLED at `0x3C`
- a 5 V supply with at least 3 A available
- 22 AWG power wiring and 30 AWG signal wiring
- all joints detached during initial servo calibration
- a stagger delay between servo movements to reduce rail collapse

The PCA9685 preserves the eight logical servo channels while moving PWM timing
off the ESP32. The OLED and PCA9685 can share I2C because their default
addresses differ.

## Sources

Repository references:

- `hardware/bom/README.md`
- `docs/wiring-guide/README.md`
- `docs/build-guide/README.md`
- `firmware/README.md`
- `firmware/sesame-firmware-main.ino`
- `firmware/debugging-firmware/sesame-motor-tester.ino`
- `firmware/movement-sequences.h`

External primary/manufacturer references:

- [Waveshare ESP32-S3-Zero documentation and pinout](https://docs.waveshare.com/ESP32-S3-Zero)
- [Waveshare ESP32-S3-Zero schematic](https://files.waveshare.com/wiki/ESP32-S3-Zero/ESP32-S3-Zero-Sch.pdf)
- [Espressif ESP32-S3 GPIO documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/gpio.html)
- [Espressif ESP32-S3 hardware design/strapping-pin guidance](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html)
- [NXP PCA9685 product page and datasheet](https://www.nxp.com/products/power-drivers/lighting-driver-and-controller-ics/led-drivers/16-channel-12-bit-pwm-fm-plus-ic-bus-led-driver%3APCA9685)
- [Adafruit PCA9685 breakout power and connection guide](https://learn.adafruit.com/16-channel-pwm-servo-driver?view=all)


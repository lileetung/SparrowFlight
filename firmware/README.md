# Sparrow firmware 1.0.0

Flight firmware for the [Sparrow flight controller 1.0](../hardware/flight-controller/): a Seeed XIAO ESP32-S3 Plus with an MPU-6050 (GY-521) and a downward VL53L0X (GY-530). It runs angle mode at 500 Hz, a ToF altitude hold that follows the surface below, button take-off and velocity mode, auto-landing, a per-motor trim measured at the first battery boot and stored, a blackbox log in flash, and a Bluetooth beacon so the app can list nearby drones. With the camera expansion board of a XIAO ESP32-S3 Sense fitted to the Plus (or a whole Sense in its place), it also streams the camera to the app.

| | |
|---|---|
| Build system | [PlatformIO](https://platformio.org/), one environment `xiao` (board `seeed_xiao_esp32s3`) |
| Framework | Arduino-ESP32 2.0.17, from PlatformIO's `espressif32` platform 7.0.1; the code uses the 2.x LEDC API, and the camera driver (esp32-camera) that comes with it |
| Libraries | `pololu/VL53L0X` 1.3.1, `h2zero/NimBLE-Arduino` 1.4.3; the platform and library versions are pinned in `platformio.ini` |
| Works with | flight controller 1.x, [SparrowFlight](../software/README.md) for iPhone |
| Licence | [GPL-3.0-or-later](LICENSE) |

The drone is flown with **SparrowFlight**; how to connect and fly is in [software/README.md](../software/README.md). The stock ESP-Drone app also flies it in thrust mode, because the firmware accepts ESP-Drone's commander packets on the same IP address and port. That app has no take-off button, telemetry, parameters or camera view, and the firmware reads 15 degrees from the app as full stick (larger values are clamped).

## Files

| Path | Contents |
|---|---|
| `src/main.cpp` | sensors, attitude estimate, rate PID, mixer, state machine, link, commands, calibration, boot sequence |
| `src/auto_land.h` | height and vertical-speed estimate (ToF, accelerometer-aided), altitude hold, auto-landing |
| `src/vel_mode.h` | velocity mode: take-off, stick as climb rate, landing by stick |
| `src/control_math.h` | expo, X mixer with roll/pitch priority |
| `src/trim_math.h` | motor trim from battery drops |
| `src/soft_i2c.h` | bit-banged I2C with a time bound on every call, used for the IMU and ToF in flight |
| `src/blackbox.h`, `src/blackbox.cpp` | flight log in fixed flash slots, erased ahead and written by a background task |
| `src/camera_stream.h`, `src/camera_stream.cpp` | camera video over UDP (the camera expansion board of the XIAO ESP32-S3 Sense) |
| `src/dhcp_unicast.h`, `src/dhcp_unicast.cpp` | DHCP replies sent to the phone's own WiFi address instead of broadcast, see [Link](#link) |
| `src/net_send.h`, `src/net_send.cpp` | the task on core 0 that sends telemetry and replies, so the control loop does not wait for the network |
| `tools/` | host tools, see [Tools](#tools) |

## Build and flash

1. Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html), or VS Code with the PlatformIO extension.
2. Turn SW1 off, then connect the XIAO's USB-C to a computer. The battery may stay plugged in.
3. Build and flash from the `firmware` folder:

   ```sh
   cd firmware
   pio run -t upload -t monitor
   ```

   The port is found automatically. With several boards connected, add `--upload-port /dev/cu.usbmodemXXXX` (Linux: `/dev/ttyACM0`). If the upload cannot connect, hold the XIAO's B (boot) button while plugging in USB, upload, then press R (reset).
4. To type commands later, open the monitor with `pio device monitor --echo` (115200 baud).

The same build runs on a XIAO ESP32-S3 Plus and a XIAO ESP32-S3 Sense; a board without the Sense expansion board simply has no camera.

### USB, battery and chargers

The [flight controller README](../hardware/flight-controller/README.md#xiao-pins) explains why SW1 must be off before you connect USB. The firmware side:

- **A computer on USB means a dry run.** The firmware looks for a USB host for 1.5 s at power-up, and again whenever it is idle on the ground. With a host, everything is computed and reported, but the motor pins stay low until the next power-up.
- **A battery under 3.3 V at power-up also means a dry run**, without USB. The firmware takes the highest reading of the 1.5 s it spends looking for a USB host, so a connector that bounces as the battery is plugged in does not count as a flat battery.
- **A USB charger or power bank is not a computer.** It sends no USB frames, so with a battery connected the board boots as on battery, even with SW1 off (the motor pads are wired straight to the battery). After 3 s of LED warning each motor spins in turn. Keep clear of the propellers, or unplug the battery before you use a charger.

### Check what is flashed

On USB the monitor prints, within about 2 s of a reset:

```text
# sparrow firmware 1.0.0, vbat 4.06 V, USB host yes: dry run, motors stay off
```

followed by the boot log (the last 8 boots: reset reason, battery or USB, battery voltage), the WiFi channel, the sensor and calibration status, and then one `T,` telemetry line every 50 ms. If you missed the boot lines, send `get`. Its first line names the version and the build time, so you can tell two builds apart, and its second line what the board has:

```text
# firmware 1.0.0, built Oct 30 2026 17:05:12
# hardware imu ok, tof ok, camera OV3660, blackbox ok
```

`get` also works over WiFi, from the app's console. Check it after every flash, before you fly.

## Power-up on battery

Put the drone down on flat ground, turn SW1 on, and keep your hands off until the LED blinks slowly. Fit the propellers first, except for the first power-up of a new build (see [Calibration](#calibration), step 5).

| Step | XIAO LED | What happens |
|---|---|---|
| 1 | solid | USB host check (1.5 s); sensors, WiFi access point and Bluetooth start |
| 2 | blinks for 3 s | hands off: the motors are about to spin |
| 3 | solid | each motor spins briefly at 15%, M1 to M4, about 1 s in all: the motors work and the drone is ready. It flies the motor trim stored in flash. With no trim stored yet (a new board, or after `trim reset`), each motor spins alone at 45% instead, about 6 s in all, and the motor trim is measured and stored |
| 4 | solid | gyro calibration: keep still; it repeats while the frame moves. Then the camera starts (with the camera expansion board), after the motor spin so its current does not affect a trim measured there |
| 5 | slow blink | ready, waiting for the app |

If a trim measured at boot is out of range, it is not stored: the LED blinks fast for 5 s, the drone flies without a trim, and the next battery boot measures again. After changing propellers or motors, or after a crash, measure it again with `trim measure` (see [Calibration](#calibration), step 6). On USB, steps 2 and 3 are skipped.

| XIAO LED | Meaning |
|---|---|
| slow blink (0.5 s on, 0.5 s off) | ready, no app linked |
| solid | booting, app linked (stick packets arriving), or flying |
| blinks for 3 s (0.25 s on, 0.25 s off) | hands off: the motors are about to spin (power-up, `trim measure`) |
| fast blink (0.1 s) | killed; battery under 3.3 V; motor trim rejected (5 s); take-off refused because of the IMU or the attitude (2 s) |

LED D2 on the board only shows that SW1 is on with a battery connected.

**Battery.** At take-off, a resting battery under 3.80 V gives the event `# battery low: … land soon`; the take-off still goes ahead. In the air the LED blinks fast below 3.3 V (filtered, under load). The firmware does not land by itself on a low battery: land when you see it.

## Flying

The [app README](../software/README.md#flight-modes) describes both flight modes from the pilot's side. The app uses velocity mode (its default, Button take-off) unless Thrust is chosen in its settings, and always thrust mode on a drone without a ToF. What the firmware does:

- **Thrust mode** (commander packets, as ESP-Drone sends them). With the link up and thrust at 0 for 0.3 s, thrust above 0 takes off. The stick drives the motors until the altitude hold takes over, about 100 mm below the stick's hold height: thrust up to 50% holds `hold_min` (300 mm) above the surface, 80% and above `hold_max` (800 mm), linear in between. Thrust 0 starts the auto-landing.
- **Without a ToF** only thrust mode flies, with no altitude hold and no auto-landing: thrust 0 stops the motors at once, so bring the drone down to the ground first.
- **Velocity mode** (the app's button take-off). `takeoff` waits up to 1 s for a centred left stick, then climbs to `takeoff_mm` (500 mm); if the ToF does not see the frame leave the ground within 6 s, the motors stop. The stick then sets the climb rate, up to 300 mm/s, between `hold_min` and `hold_max`; centred, it holds the height. `land`, or the stick held down at `hold_min` for 1.5 s, lands; pushing the stick up during that landing stops it.
- **The hold follows the surface below**: onto a table or a bed and off it again, the drone ends at the hold height above whatever is under it. The VL53L0X runs its library default ranging profile and reads to about 1.2 m.
- **Auto-landing** descends at about 200 mm/s and stops the motors at touchdown, when the height stops coming down below 120 mm for 1 s, or after 15 s. While the link is up the app still steers, so you can move away from a wall.
- **Yaw** takes the app's yaw stick as it comes, through `yaw_cmd_expo` and `yaw_cmd_max`; the deadband is the app's.

Safety stops:

| Situation | Result |
|---|---|
| no stick packets for 0.5 s (link lost) | auto-landing with the attitude held level; motors off if not yet airborne |
| packets of the other flight mode while flying | auto-landing |
| tilted more than 60 degrees for 0.2 s while flying | motors off |
| stick released, or landing, while tilted more than 30 degrees | motors off |
| no ToF height for 5 s while holding or landing | thrust ramps to zero over 1 s |
| take-off with no IMU reading in the last 0.1 s (`# take-off refused: no IMU`) | take-off refused |
| take-off with the frame tilted more than 30 degrees, the accelerometer not near 1 g, or the attitude estimate 10 degrees off the accelerometer | take-off refused |
| velocity-mode take-off without a ToF | take-off refused |

## Link

| Item | Value |
|---|---|
| WiFi | access point `SPARROW_<MAC>` (the 12 hex digits of its MAC address), password `12345678`, up to 3 devices; channel 6 |
| Drone IP address | `192.168.43.42`; DHCP hands out 192.168.43.x with no gateway and no DNS server |
| UDP 2390, to the drone | stick packets every 50 ms, each followed by a 1-byte checksum (8-bit sum of the packet) |
| UDP 2391, both ways | text commands in, up to 127 bytes each; replies and events (`#` lines) and telemetry (`T,` lines, 20 Hz, 53 fields) out |
| UDP 81, both ways | camera video, only with the camera expansion board, see [Camera](#camera) |
| Bluetooth LE | non-connectable advertising, on the ground only; it stops while an app is linked |

Stick packets, little-endian:

| Mode | Layout |
|---|---|
| thrust | `0x30`, float roll [deg], float pitch [deg], float yaw rate [deg/s], uint16 thrust (0-65535) |
| velocity | `0x70`, `0x04`, float roll [deg], float pitch [deg], float yaw rate [deg/s], float vertical speed [m/s, up positive] |

Signs: roll + is right, pitch - is forward, yaw + is clockwise seen from above.

A datagram longer than the firmware reads (127 bytes on 2391, 64 on 2390) is cut, and the rest is dropped; the port keeps working. Over USB a command line is cut at 120 characters.

Every device that sends anything to UDP 2391 receives telemetry and replies until it has been silent for 3 s, up to 3 devices at once; `hello` keeps a device on the list without doing anything else; a client that only listens can send it every second. A task on core 0 sends telemetry and replies, so a slow WiFi moment does not stall the control loop. Replies are queued and go in order; of telemetry only the newest line per device waits, replacing an older one not sent yet. A backlog of telemetry kept the WiFi driver's queue full while a phone was away scanning other channels, and the drone's ARP answers, which need room in it too, did not get out. When the driver has no buffer for a moment (errno 12), a datagram is tried again 2 ms later, up to 3 times in all; sent only once, one telemetry line in six was lost in flight. `get` reports the task (`# net send task: …`, with how many went only after a retry).

**WiFi channel.** The access point always uses channel 6 (`WIFI_CHANNEL` in `src/main.cpp`; change it there if channel 6 is busy where you fly). `get` shows each phone's signal as the drone hears it:

```text
# WiFi channel 6, 1 stations at dBm -65, 12 ARP greetings
```

**ARP greeting.** While no app is sending stick packets, the drone sends each phone on its access point an ARP request for the phone's own address once a second. An iPhone that has left 5 ARP requests for the drone unanswered (for example while it scans other channels right after joining) sends nothing to the drone for 20 s; an ARP request from the drone ends that wait at once, and also gives the phone the drone's MAC before the app sends anything. The count is in the WiFi line of `get`.

**No gateway.** The access point gives a phone an address but no gateway and no DNS server, so the phone treats the drone's WiFi as a local network: it stays on it for the drone and uses mobile data for the internet. With a gateway, an iPhone would count the network as WiFi without internet and switch back to a known network, dropping the link.

**DHCP replies to the phone.** The DHCP server built into ESP-IDF sends its replies (offer, ack, nak) to the WiFi broadcast address. A broadcast frame is not acknowledged or sent again, so a phone that dozes or scans another channel just after joining can miss it: on 3 Nov 2026 an iPhone got its address only after asking four times, about 10 s after joining, and iOS gave up on the network. The firmware sends each reply to the phone's own WiFi address instead (`src/dhcp_unicast.cpp`), so it is acknowledged and repeated like any other frame; the IP destination stays 255.255.255.255.

**Bluetooth beacon.** It advertises every 200-250 ms while the drone is on the ground and no app is sending stick packets. As soon as an app links, the advertising stops, so in the air nothing shares the radio with the WiFi link; the Bluetooth controller stays on. The name is the WiFi SSID. The scan response carries manufacturer data: company ID `0xFFFF`, `SP`, layout 1, one build byte that the app maps to the firmware version, the battery in mV (uint16, little-endian), and the state (0 idle, 1 flying, 2 landing, 3 killed). The battery reading is refreshed every 5 s. `get` shows `# Bluetooth beacon on|off`.

## Camera

With the expansion board of a XIAO ESP32-S3 Sense, fitted to the Plus or on a whole Sense, the firmware streams its OV3660 camera to SparrowFlight, which shows it behind the sticks. A board without the expansion board reports `camera none` and flies the same.

- **Protocol.** The app sends `video <frames shown> <frames lost> <longest gap between two shown frames, ms>` to UDP port 81 every 0.5 s. The drone answers with every JPEG frame cut into datagrams of up to 1400 bytes, each behind an 8-byte header: `S`, `V`, layout 1, chunk index, chunk count, 0, frame number (uint16, little-endian). A lost datagram loses only its frame; nothing waits for a retransmission. `video off`, or 2 s without `video`, stops the stream. One viewer at a time: whoever sent the latest `video`.
- **Only while someone watches.** The camera starts on the first `video` and goes to sleep 5 s after the viewer leaves (sensor in software power-down, clock off, driver released). Streaming makes the XIAO hot; asleep, the board runs about as cool as without a camera.
- **Settings.** `cam_res` (1 = 320x240, the default, 2 = 480x320, 3 = 640x480), saved, ground only. At most 15 frames a second go to the app, and the JPEG quality is fixed at 12: 6-10 KB a frame at 320x240. The OV3660 reads out top to bottom flipped, so the firmware flips it back when it starts the camera.
- **Timing.** The camera and its interrupts run on core 0, below the WiFi and network tasks; the control loop has core 1 to itself. The camera clock uses LEDC timer 3, channel 7, clear of the motors' channels 0-3.
- **`get`** reports the camera (`# camera OV3660 320x240 q12 max 15 fps on UDP :81, capturing, …`), the viewer with what the app reports it saw, and the chip temperature in the three ranges of the ESP32-S3's own sensor (uncalibrated: compare a range only with itself).
- **`camtest [s]`**, best run over USB: the board watches its own stream for s seconds (30 by default), as the app would, so the stream, the sleep and the wake can be checked without a phone and without radio traffic. `get` then shows the result.

## Commands

Type them in the serial monitor (end with Enter) or in the app's console; over WiFi each command is one UDP datagram to port 2391, of at most 127 bytes. Replies start with `#`. Commands marked *ground* work only while the drone is idle or killed; `bb` commands also need USB.

| Command | Effect | Stored in flash |
|---|---|---|
| `kill` | motors off at once (KILLED); back to idle once the app has held the left stick centred (thrust 0 in thrust mode) for 1 s, counted from the kill | |
| `takeoff` | velocity mode only: take off to `takeoff_mm` (app linked in velocity mode, left stick centred) | |
| `land` | start the auto-landing | |
| `get` | version and build time, hardware, every parameter, unit calibration; on the ground also take-off, hold and height-estimate state, Bluetooth, WiFi channel, phone signal and ARP greetings, send task, camera, blackbox, chip temperature, battery and hover estimate, I2C error counts, and the timing of the last flight's slowest loop. Several `get` arriving together are answered once | |
| `hw` | one line: `# hardware imu ok\|missing, tof ok\|missing, camera <sensor>\|none\|starting, blackbox ok\|failed`. `imu` is whether any IMU read has answered since the boot, `tof` whether the VL53L0X started at boot; the camera reads `starting` until it has been probed | |
| `params` | one line per parameter: `# param <name> lo=<min> hi=<max> default=<default> store=<ram\|saved\|trim-save> int=<0\|1>` | |
| `set <name> <value>` | change a parameter, see [Parameters](#parameters) | saved parameters |
| `imu` | one reading: acceleration and its magnitude, tilt from the accelerometer, attitude, rates, vertical acceleration | |
| `cal` | *ground*: measure the gyro bias again (keep still); it is also measured at every boot | |
| `level` | *ground*: store the roll/pitch zero, frame resting on a level surface; also recalibrates the gyro | yes |
| `trim` | motor trim (status, measured and raw), attitude offset, level status, battery drops. The status is `stored`, `measured` (at this boot or by `trim measure`), `not stored` or `reset` (the next battery boot measures it), or `rejected (…)` | |
| `trim measure` | *ground*, battery boot, **propellers on**, hands off: the LED blinks for 3 s, then each motor spins alone at 45%, M1 to M4, about 9 s in all, including the 3 s blink; a plausible motor trim is stored and shown as `trim` shows it, an implausible one is refused with the reason (`# trim measure rejected: …`) and the trim in use is kept. Telemetry and the link carry on meanwhile; other commands except `get`, `hw`, `params`, `trim` and `imu` are refused, and `kill` stops it. See [Calibration](#calibration), step 6 | yes |
| `trim noload` | battery boot, idle, **propellers off**: measure each motor's battery drop at 45% without a propeller | yes |
| `trim save` | store `roll_off` and `pitch_off` (and the motor trim, if one is stored) | yes |
| `trim reset` | erase the stored motor trim (back to 1.000); the next battery boot measures it | erases |
| `unitcal` | show the unit calibration and whether it comes from flash or the defaults | |
| `unitcal raw` | *ground*: mean raw accelerometer over 250 samples, used by `six_face.py` | |
| `unitcal acc <bx> <by> <bz> <sx> <sy> <sz>` | *ground*: store the accelerometer bias [g] and scale per axis | yes |
| `unitcal vbat <volts>` | *ground*, battery connected: scale the battery reading to a meter reading | yes |
| `unitcal tof` | *ground*: store the ToF reading at rest (48 ranges; refused above 4 mm standard deviation) | yes |
| `unitcal reset` | *ground*: erase the unit calibration and use the defaults | erases |
| `camtest [s]` | with the camera expansion board, best run over USB: the board watches its own video for s seconds (default 30), see [Camera](#camera) | |
| `bb list` | *ground*, USB: list the stored flight logs | |
| `bb dump [seq]` | *ground*, USB: print one flight log as CSV, the newest by default | |
| `bb erase` | *ground*, USB: delete all flight logs | erases |
| `hello` | no reply; keeps this device on the telemetry list | |

## Parameters

`set <name> <value>` replies `# <name> = <value> (RAM only)` or `(saved)`. A value outside its range is refused, and integer parameters are rounded. `get` lists the current values; `params` lists each one's range, default and storage, which the app uses for its parameter pages. `params` names three storage kinds:

- **`ram`**: lost at power-off. None of the parameters below uses it.
- **`saved`**: stored in flash and read back at boot. A flash write pauses both cores for a moment, so these change only on the ground; in the air `set` replies `# <name> refused: not on the ground`.
- **`trim-save`**: RAM until `trim save` stores it with the motor trim; it can be changed in the air.

| Name | Default | Range | Storage | Meaning |
|---|---|---|---|---|
| `angle_cmd_max` | 10 | 0-30 | saved | tilt at full stick [deg] |
| `angle_cmd_expo` | 0.70 | 0-1 | saved | roll/pitch stick expo (half stick is about 2.4 degrees) |
| `yaw_cmd_max` | 50 | 0-250 | saved | yaw rate at full stick [deg/s] |
| `yaw_cmd_expo` | 0.60 | 0-1 | saved | yaw stick expo |
| `takeoff_mm` | 500 | 200-1100 | saved | velocity mode: take-off height [mm] |
| `hold_min` | 300 | 200-1100 | saved | lowest hold height above the surface [mm]; not above `hold_max` |
| `hold_max` | 800 | 200-1100 | saved | highest hold height [mm]; not below `hold_min` |
| `hover_ref` | 55 | 40-70 | saved | hover thrust [%] with the battery at 3.40 V under hover load; the starting point for the hold and the landing |
| `cam_res` | 1 | 1-3 | saved | camera resolution: 1 = 320x240, 2 = 480x320, 3 = 640x480 (whole number) |
| `roll_off` | 0 | -5 to 5 | trim-save | roll offset added to the attitude target [deg]; positive is right side down |
| `pitch_off` | 0 | -5 to 5 | trim-save | pitch offset added to the attitude target [deg]; negative is nose down |

`hold_min` above `hold_max` is refused (`# hold_min refused: above hold_max (800)`), and so is `hold_max` below `hold_min`; the value stays as it was.

The motor trim is not a parameter: one multiplier per motor, measured at the first battery boot and again with `trim measure` (see [Calibration](#calibration), step 6), kept in flash, shown by `trim` and erased with `trim reset`.

Fixed in the firmware (change them in `src/main.cpp`, or `src/camera_stream.h` for the camera):

- the rate PID gains, in % duty per deg/s: roll P/I/D 0.18/0.35/0.0018, pitch 0.15/0.35/0.0014, yaw P/I 0.30/0.40 and no yaw D (`RATE_KP`, `RATE_KI`, `RATE_KD`);
- the angle loop gain (5 deg/s of rate per degree), the integral limit (15% duty per axis) and the roll/pitch integral starting at 30 mm of ToF height;
- the MPU-6050 low-pass filter at 42 Hz;
- the height estimate: always accelerometer-aided, with a time constant of 0.5 s; after an accelerometer fault it runs on the ToF alone until the next take-off;
- the VL53L0X ranging profile: the library default, about 1.2 m;
- the WiFi channel: 6, see [Link](#link);
- the camera: at most 15 frames a second, JPEG quality 12.

The firmware applies no yaw stick deadband; SparrowFlight has its own.

## Calibration

Until a board stores its own calibration, the firmware uses built-in defaults for the accelerometer bias and scale, the battery-reading multiplier, the ToF ground reading and the no-propeller motor drops; `unitcal` then reports `default (first airframe)`. Calibrate every new build once, in this order, with the drone fully assembled (board mounted, legs on). The values live in the XIAO's flash, so they stay with the board; flashing new firmware keeps them.

| Step | Stored by | Redo when |
|---|---|---|
| 1. Accelerometer, six faces | `tools/six_face.py` (`unitcal acc`) | new board; IMU replaced or re-soldered; `imu` at rest reads more than about 0.02 g away from 1.00 g |
| 2. Level | `level` | after step 1; after changing the board mounting, grommets or legs |
| 3. ToF ground reading | `unitcal tof` | after changing the mounting, legs or ToF module |
| 4. Battery reading | `unitcal vbat <volts>` | new board |
| 5. Motor drops without propellers | `trim noload` | after changing a motor |
| 6. Motor trim | the first battery boot; `trim measure` | after changing propellers or motors; after a crash |

The gyro bias is measured at every boot. The level is also checked at every boot and after every 2 s of resting still on the ground; a reading within 5 degrees of the stored `level` is used until power-off, a larger one is ignored as "frame not flat" (`trim` shows which).

**1. Accelerometer.** The firmware uses the accelerometer for the attitude, for the vertical speed of the altitude hold, and for checks that refuse a take-off outside 0.8-1.2 g. With SW1 off and USB connected, rest the drone still on each of its six faces in turn and capture each one; the face names are free:

```sh
cd firmware
uv run --no-project tools/six_face.py clear
uv run --no-project tools/six_face.py capture flat
uv run --no-project tools/six_face.py capture upside-down
uv run --no-project tools/six_face.py capture nose-down
uv run --no-project tools/six_face.py capture nose-up
uv run --no-project tools/six_face.py capture left-down
uv run --no-project tools/six_face.py capture right-down
uv run --no-project tools/six_face.py solve --apply
```

A capture whose readings spread by more than 0.02 g (the frame moved) is refused: capture that face again. The captures are kept in `logs/six-face.json` (`list` shows them). `solve` takes, per axis, the faces with the largest and smallest reading as +1 g and -1 g, computes bias = (pos + neg) / 2 and scale = 2 / (pos - neg), and with `--apply` stores them with `unitcal acc`.

**2. Level and 3. ToF ground reading.** Rest the drone on its legs on a flat, level, light-coloured surface (white paper works), still, then send `level` and then `unitcal tof`.

**4. Battery reading.** SW1 off, USB to the computer, battery plugged in (a dry run, so the motors stay off). Measure the battery with a multimeter and send the reading, for example `unitcal vbat 4.12`. The firmware averages its own reading over 1 s and scales it to yours.

**5. Motor drops without propellers.** The motor trim compares each motor's battery drop with a propeller against its drop without one.
1. Remove all four propellers and power up on battery, without USB. On a board with no motor trim stored yet, the boot measurement is rejected without propellers (the LED blinks fast for 5 s); that is expected.
2. Join the drone's WiFi and send `trim noload` from the app's console. Each motor spins in turn, about 6 s in all.
3. Fit the propellers and measure the motor trim (step 6).

**6. Motor trim.** Each motor's load (drop with propeller minus drop without) gives a multiplier that evens out the four motors, applied at 60% of its size. It is measured at the first battery boot with no trim stored, and stored in flash; every later battery boot uses it. Measure it again with `trim measure` after changing propellers or motors, after a crash, and after step 5: propellers on, the drone on flat ground, hands off. The LED blinks for 3 s, then each motor spins alone at 45%, M1 to M4, and the reply is the new trim, as `trim` shows it. If any load is below 0.01 V or any multiplier falls outside 0.7-1.3, the result is rejected and the LED blinks fast for 5 s: `trim measure` says why and keeps the trim in use, and a boot stores nothing, so the next battery boot tries again. `trim reset` erases the stored trim.

**Check.** `get` ends with `# unitcal flash on SPARROW_…` and your values. On a level surface, `imu` reads about 1.00 g with roll and pitch near 0.

Two adjustments may help after the first flights:

- **`hover_ref`**: another battery, propellers or added weight changes the hover thrust. The hold and the landing adapt in flight, within 8 points of the estimate made from it and the battery voltage. It is saved, so change it on the ground, for example `set hover_ref 58`.
- **`roll_off`, `pitch_off`**: a drone that drifts steadily one way in a hover can be trimmed by a fraction of a degree (for example a negative `pitch_off` if it drifts backwards), then stored with `trim save`.

## Blackbox

During every battery flight the firmware keeps the last 15 s at 50 Hz in RAM, and saves them to flash once the motors have stopped (`# BLACKBOX saved`). USB dry runs are not recorded. Each log notes what ended the flight (`KILL_COMMAND`, `KILL_TILT`, `LAND_THROTTLE`, `LAND_LINK`, `LAND_TOF`). The current format is 3: the CSV header names every column. Download a log over USB with `tools/blackbox.py`.

**How it is stored.** The code runs from the same flash chip, and while the chip erases or writes, both cores stop. With a file system, each save erased 66 KB sector after sector (about 55 ms each), and for 1-2 s after every landing the WiFi got no time: the app's telemetry stopped. So the blackbox works the way dedicated flight loggers do, with the slow part done ahead and apart:

- The `spiffs` data partition (1.5 MB) is split into 22 fixed slots of 68 KB, one flight each, used in turn. No file system.
- A task on core 0 keeps the next 2 slots erased, one 4 KB sector at a time with 200 ms between, so the longest pause is one sector erase.
- At landing the log is copied to PSRAM (a queue of 3), and the recording is free for the next flight at once. The task then writes it into an erased slot one 256-byte page at a time (about 1 ms each, 2 ms apart), so it pauses the loop by about 1 ms at a time; the header goes last, so a slot written halfway (power cut) does not count.
- The task touches the flash only on the ground with the motors off (not during a motor trim measurement). A log not yet written when the drone takes off waits in PSRAM for the next landing. Switching off within a second or two of landing loses that log.
- The newest 20 flights are kept (2 of the 22 slots are kept erased). `bb erase` drops every log by programming a mark in it, with no erase; the slots are erased again when their turn comes.

`get` shows the state: `# blackbox: 22 slots, 2 erased ahead, 0 waiting, 3 saved, 0 failed, 0 dropped (queue full), 0 flash errors`. The `bb` commands are refused while a log is waiting.

## Tools

The Python tools are single-file scripts that declare their own dependencies, so `uv` runs them without any setup. Run them from `firmware/`; the examples write to `logs/`, which git ignores. Both look for `/dev/cu.usbmodem*` (macOS); elsewhere pass `--port`.

```sh
cd firmware
mkdir -p logs
```

| Tool | Link | Use |
|---|---|---|
| `tools/blackbox.py` | USB | download a flight log |
| `tools/six_face.py` | USB | six-face accelerometer calibration, see [Calibration](#calibration) |

**blackbox.py.** Connect USB with the drone idle (a dry run), then:

```sh
uv run --no-project tools/blackbox.py logs/blackbox.csv            # the newest log
uv run --no-project tools/blackbox.py logs/blackbox.csv --seq N    # log N, numbers from "bb list"
```

It prints the stored logs first, then saves the chosen one as CSV.

## Safety

- **Propellers off** for the first power-up on battery, for `trim noload`, and whenever you work on the drone with the battery connected. The boot spin runs M1 to M4 in turn, which makes a good first check: seen from above with the nose (the USB end) ahead, M1 rear left and M3 front right must spin counter-clockwise, M2 front left and M4 rear right clockwise.
- **Every battery boot spins the motors**, propellers on or not, after the 3 s LED warning, and so does `trim measure`, 3 s after the command. Put the drone down on flat ground and keep clear until the LED blinks slowly. A charger on USB counts as a battery boot.
- **Emergency stop** stops the motors at once and the drone falls: the **Emergency stop** button in the app, or `kill` from the console.
- **Without a ToF**, releasing the thrust stick stops the motors at once: there is no auto-landing.
- The firmware does not land by itself on a low battery; land when the LED blinks fast in the air.
- LiPo batteries can catch fire when damaged, short-circuited or overcharged; charge them with a suitable charger and never leave them unattended. Fly away from people and follow local drone regulations. The designs and software are provided without warranty.

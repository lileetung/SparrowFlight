# SparrowFlight app

SparrowFlight is the iPhone app that flies Sparrow over WiFi.

The app is distributed through the App Store. Its source code is not part of this repository.

**Download:** [SparrowFlight on the App Store](https://apps.apple.com/app/id6829419743).

## Requirements

- iPhone with iOS 18 or later, held in landscape
- Bluetooth on (used to list nearby drones), WiFi on
- Sparrow firmware 1.0.0 or later

## Features

- List of nearby drones over Bluetooth, and one-tap joining of the drone's WiFi
- A check of the drone after connecting, before the controls appear
- Two flight modes, chosen in Settings: button take-off and landing (velocity mode), or thrust with the stick setting the hold height
- The camera picture behind the sticks, on a drone built with a XIAO ESP32-S3 Sense
- Live telemetry and events: height, state, battery, link
- Flight parameters with their ranges, defaults and storage, read and changed from the app
- A console for firmware commands
- Flight logs for every connection

## Connecting

1. Tap **Choose a drone**. The list shows nearby drones with their 4-character ID, firmware version, battery and signal strength. iOS does not let apps scan for WiFi, so the list comes from the drone's Bluetooth advertising; the first time, iOS asks for Bluetooth permission. A drone advertises while it is on the ground and no app is flying it.
2. Tap a drone. iOS asks to join its WiFi network (`SPARROW_<MAC>`, password `12345678`); tap Join. The first time, iOS then asks whether the app may find devices on the local network; tap Allow. The app then waits until the drone has given the phone an address (a few seconds, at most 10) and sends only over the drone's WiFi, never over mobile data.
3. The app checks the drone. All items are listed at once, those that can stop a flight first, and each is ticked as soon as the drone has told it, usually all within a second:

   | Item | Source |
   |---|---|
   | Local network | telemetry arriving means iOS allows it; sends refused while the phone is on the drone's network mean the Local Network permission is still being asked or is off |
   | Firmware | the first line of `get` (the app asks every 2 s until the drone answers) |
   | Telemetry | `T,` lines arriving |
   | IMU | the firmware's hardware report (`hw`, also the second line of `get`) |
   | Battery | the battery voltage in the telemetry, and whether the motors are enabled |
   | ToF, camera | the hardware report; a camera still starting is asked again with `hw` |

   The camera picture does not wait for the check: it appears as soon as the drone sends it.

   The title reads **Checking**, then **Ready**. The sticks and the take-off button work only once the check has passed; **Emergency stop** works whenever the app is connected. Without an IMU the drone cannot fly, and the check stops at **Cannot fly**. It also stops there when a battery is in but the firmware has kept the motors off: a computer was on USB at power-up, or the battery read below 3.3 V. The battery item then reads **Motors off, restart it**; without a ToF the app continues in thrust mode; a missing camera does not stop it. Items that have not answered after 10 s count as missing. If the drone answers later (for example while it is still calibrating its gyro after power-up), the check carries on by itself.

   When sending fails or the check stops, **Reconnect** under the list connects to the same drone again. The first time, iOS asks for Local Network access; tap Allow. If it was turned off, the check shows **Local network: Not allowed** with a **Settings** button that opens the app's page in Settings, where Local Network can be turned on. Once it is allowed the link comes up by itself.

Settings > **Advanced link** takes the IP address of another device on the network the phone is on; the main screen then connects straight to it, without the Bluetooth list or joining a drone's WiFi. The drone itself is at `192.168.43.42`, the default; leaving the field empty means the drone.

## Flight modes

Choose the mode in Settings > Flying > **Flight mode**: **Button take-off** (the default) or **Thrust**. It cannot be changed while flying. Button take-off needs a ToF: on a drone without one, the app flies thrust mode whichever is chosen.

| | Button take-off (velocity mode, the default) | Thrust |
|---|---|---|
| Take-off | tap **Take off**; the drone climbs to the take-off height (default 50 cm) and holds it | push the left stick up |
| Left stick up and down | climb or sink at up to 0.3 m/s; release to hold the height | the stick position sets the hold height: the lowest hold height at 50% thrust and below, the highest at 80% (the top), linear in between. Without a ToF: plain thrust, 80% at the top, no hold |
| Landing | tap **Land**, or hold the stick fully down at the lowest height for 1.5 s | release the left stick and the drone lands by itself. Without a ToF the motors stop at once, so bring the drone down to the ground first |

In thrust mode with a ToF, the left stick's thrust line is marked at the lowest hold height (50%), halfway and the highest (80%), in cm; once the drone holds the height, the state in the centre of the screen reads **Holding** and the height.

The take-off height and the lowest and highest hold heights are parameters (Settings > Flying links to them).

## Controls

- **Right stick:** roll and pitch, centred where you touch. Full stick tilts the drone 10 degrees by default (the firmware's `angle_cmd_max`), with expo so that half stick is about 2.4 degrees.
- **Left stick:** climb and yaw, centred where you touch; in thrust mode the touch point is thrust 0, and pushing up sets the hold height (with a ToF) or the thrust (without one). Yaw is locked by default (Settings > Sticks > **Lock yaw**). Unlocked, the yaw stick has a deadband (40% by default, set in Settings > Sticks once yaw is unlocked) so that a thumb moving up and down does not turn the drone; this is the only yaw deadband, the firmware adds none.
- **Emergency stop:** the solid dark red round button (**STOP**) in the bottom-left corner, under the left stick, so the left thumb reaches it at once. It stops the motors at once, and the app repeats it every 0.1 s until the drone confirms. Only the flying screen has it: while the console is open (for example while **Calibrate motors** spins the propellers), swipe the console down first. The drone is ready again once the left stick has been centred (thrust 0 in thrust mode) for 1 s after the press.
- **Console:** every reply from the drone and every command sent, the three calibrations as buttons, each explained and confirmed before it is sent (**Calibrate IMU**, `level`: the frame still on a flat surface; **Calibrate ToF**, `unitcal tof`: the drone still on its legs on a flat, light-coloured surface; **Calibrate motors**, `trim measure`: propellers on, hands off), and a field for any command, up to 120 bytes, such as `get`, `imu` or `trim`.
- If the app goes to the background it disconnects, and the firmware starts an automatic landing after 0.5 s without stick packets.
- If telemetry stops for 5 s (15 s while flying), the app disconnects and returns to the drone list, which then offers **Reconnect** with the drone's ID to connect to it again.
- The top bar reads **No telemetry** (with a warning vibration) once telemetry has stopped for 1 s in the air, or 3 s on the ground: on the ground short gaps are normal (the phone scans other channels just after joining the drone's WiFi), and the drone does not wait for the app anyway, landing 0.5 s after the stick packets stop.

## Camera

A drone built with a XIAO ESP32-S3 Sense streams its camera over UDP port 81, and the app shows the picture behind the sticks: fitted to the screen without cropping and dimmed, so the lines and text on top stay readable. After 1.5 s without a new frame the picture is cleared, so a frozen frame is never taken for live video. The camera on the drone runs only while the app is showing it.

Settings > Camera turns the picture on or off and links to the camera resolution parameter (`cam_res`; the drone sends at most 15 frames a second). The console shows the current resolution and frame rate. A drone without a camera flies as usual.

## Parameters

The parameter pages list the drone's parameters in four groups, each with its name, current value and a short explanation: stick feel (`angle_cmd_max`, `angle_cmd_expo`, `yaw_cmd_max`, `yaw_cmd_expo`), take-off and height hold (`takeoff_mm`, `hold_min`, `hold_max`, `hover_ref`), camera (`cam_res`) and attitude trim (`roll_off`, `pitch_off`). The firmware reports each parameter's range, default and where it is kept (`params`): saved on the drone, or saved with `trim save`, which the app sends after the drone has taken a new attitude trim. Tap a parameter to change it: edit the value at the top, then tap **Save**, or **Cancel**; when nothing is being changed, **Reset** puts the default in, to save the same way. The drone's answer shows under the value. Saved parameters change only on the ground, and the pages are unavailable while flying. Settings in the app itself (language, flight mode, sticks, camera on or off) stay on the phone.

## Flight logs

Each connection writes two files to the app's `logs` folder, which you can open in the Files app under On My iPhone > SparrowFlight > logs:

- `<firmware>-<ID>-<MMDD>-<HHMM>.csv`: telemetry, 53 columns
- `<firmware>-<ID>-<MMDD>-<HHMM>.events.txt`: events and the commands the app sent; the second line gives the phone's address on the drone's network and how long after joining it came

## Privacy and support

The app collects no personal data: see the [privacy policy](PRIVACY.md). For help and for reporting a problem, see [SUPPORT.md](../SUPPORT.md). Both are linked from Settings > About in the app.

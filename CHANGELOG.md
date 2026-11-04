# Changelog

Hardware uses `MAJOR.MINOR`: MAJOR changes break compatibility (mounting holes, connectors, pin assignments), MINOR
changes are compatible fixes. Firmware uses semantic versioning: MAJOR when the link protocol changes incompatibly,
MINOR for new features, PATCH for fixes.

Versions are written without a `v`. Git tags name the part and add a `v`: `frame-v1.0`, `fc-v1.0`,
`firmware-v1.0.0`; the hardware file names carry it too (`sparrow-fc-v1.0-bom.csv`).

## Frame 1.0

First public release, CNC-milled in 1.5 mm carbon fibre and flown. Snap-on landing gear through a 1.65 mm flex slit,
openings of plate thickness + 0.2 mm with R0.75 corners, 2.2 x 12 mm battery-strap slots, and a 30.5 mm M2
flight-controller hole pattern.

## Flight controller 1.0

First public release, made by JLCPCB (2 layers, Economic PCBA), assembled and flown. XIAO ESP32-S3 Plus on two 7-pin
rows, GY-521 MPU-6050 soldered flat on the bottom, GY-530 VL53L0X on the top, TPS63020 buck-boost, SW1 slide switch on
the regulator enable with an indicator LED, four low-side brushed-motor drivers.

The boards flown so far were made from the same copper, drill and solder-mask data. Only the silkscreen artwork
differs: the published files carry the Sparrow logo on the top, between the GY-530 and the XIAO, the version marking
"v1.0" on the top at the edge opposite SW1, and the "sparrow" wordmark on the bottom.

Known issues:
- The IMU and ToF lose I2C readings when the XIAO sits in a socket and moves. Solder the XIAO directly into the board.
- Standard JST PH connectors are rated for about 2 A, while four motors at full throttle draw several amps. Check the
  battery lead for voltage drop and heat.

## Firmware 1.0.0

First public release, for flight controller 1.0, flown with the SparrowFlight iOS app.

- Angle mode at 500 Hz, rate PID and X mixer with roll/pitch priority.
- ToF altitude hold with an accelerometer-aided height estimate; the hold follows the surface below.
- Velocity mode with button take-off and landing; thrust mode as in ESP-Drone, and thrust mode alone on a board without
  a ToF.
- Auto-landing on stick release or link loss, with app steering on the way down; safety stops on tilt, crash and lost ToF;
  take-off refused without an IMU.
- Motor trim measured at the first battery boot and with `trim measure`, kept in flash; per-unit calibration in flash;
  blackbox log of the last 15 s of each flight.
- Camera video from the camera expansion board of a XIAO ESP32-S3 Sense, fitted to the Plus or on a whole Sense, over
  UDP port 81, at most 15 frames a second, captured only while the app is watching.
- WiFi access point `SPARROW_<MAC>` on channel 6, with DHCP that offers no gateway, so a phone stays on it and keeps
  mobile data for the internet. DHCP replies go to the phone's own WiFi address, not broadcast, so a phone that dozes
  just after joining still gets them.
- Telemetry and replies to up to 3 devices, sent by a task on core 0 so the control loop does not wait for the network.
- Bluetooth advertising on the ground so the app can list nearby drones; it stops while an app is linked.
- Commands `get`, `hw` (which sensors the board has) and `params` (each parameter's range, default and storage).
- Eleven parameters: stick feel, take-off and hold heights, hover thrust and camera resolution are saved in flash and
  change only on the ground; the roll and pitch offsets are stored with `trim save`. The rate gains, the WiFi channel,
  the ToF ranging profile and the camera frame rate are fixed, and the motor trim is measured, not set.


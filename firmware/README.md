# Firmware

The flight firmware for Sparrow, version 1.0.0: PlatformIO, Arduino on the ESP32-S3. Its source will be published here under GPL-3.0.

What it does:

- angle mode, with stick shaping set by parameters
- ToF altitude hold that follows the surface below
- button take-off and auto-landing, and velocity mode (the left stick asks for a vertical speed)
- an automatic landing when the stick packets stop for 0.5 s
- a per-motor trim measured once and kept in flash
- an on-board blackbox log of the last flights
- Bluetooth advertising, so the app can list nearby drones
- camera video to the app on a XIAO ESP32-S3 Sense

The app talks to it over the drone's WiFi: ESP-Drone's commander packets on UDP port 2390, Sparrow's text protocol (telemetry, events and parameters) on UDP port 2391, and camera video on UDP port 81.

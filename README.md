<p align="center">
  <img src="docs/images/sparrow-logo.png" width="200" alt="Sparrow logo: a pixel-art sparrow in flight">
</p>

<h1 align="center">SparrowFlight</h1>

<p align="center">An open-source micro quadcopter, and the iPhone app that flies it</p>

<p align="center">English | <a href="README.zh-TW.md">繁體中文</a></p>

SparrowFlight is the project behind **Sparrow**, a 100 mm carbon-fibre quadcopter with its own 40 x 40 mm flight controller built on a Seeed XIAO ESP32-S3. Sparrow is flown over WiFi from an iPhone with the **SparrowFlight** app.

<p align="center">
  <img src="docs/images/sparrow-front.jpg" width="380" alt="Sparrow from the front: carbon-fibre frame, four coreless motors with 55 mm propellers, and a 1S battery strapped on top">
  <img src="docs/images/sparrow-back.jpg" width="380" alt="Sparrow from below: the flight controller with the XIAO ESP32-S3 and the downward ToF sensor">
</p>

## Test flight

<a href="docs/videos/test-flight.mp4"><img src="docs/images/test-flight.gif" width="240" alt="Sparrow taking off from a rooftop and hovering, flown from an iPhone"></a>

A test flight on a rooftop in October 2026: choosing the drone in the app, joining its WiFi, taking off and hovering. The full clip (36 s) is [docs/videos/test-flight.mp4](docs/videos/test-flight.mp4).

## Status

| Part | Version | Status |
|---|---|---|
| SparrowFlight, the iPhone app | 1.0.0 | coming to the App Store; see [software/](software/README.md) |
| Frame | v1.0 | flying; files being prepared for [hardware/](hardware/README.md) |
| Flight controller | v1.0 | flying; files being prepared for [hardware/](hardware/README.md) |
| Firmware | 1.0.0 | flying; source being prepared for [firmware/](firmware/README.md) |

The frame, the flight controller and the firmware will be published in this repository under the licences listed in [LICENSE.md](LICENSE.md).

## What it is

| | |
|---|---|
| Frame | 100 x 100 mm, 1.5 mm carbon fibre, snap-on landing gear |
| Flight controller | 40 x 40 mm, 2 layers, hung under the frame on silicone grommets |
| MCU | Seeed XIAO ESP32-S3 Plus, or a XIAO ESP32-S3 Sense for camera video |
| Sensors | MPU-6050 IMU (GY-521 module), VL53L0X ToF rangefinder facing down (GY-530 module) |
| Power | 1S LiPo with PH2.0 plug, TPS63020 buck-boost, slide power switch |
| Motors | 4 x 720 coreless brushed motors, 55 mm propellers, low-side MOSFET drivers |
| Link | WiFi access point on the drone, Bluetooth advertising so the app can find it |
| Flight time | about 1.5-2 minutes on a 400 mAh 20C battery |

Firmware features: angle mode, ToF altitude hold that follows the surface below, button take-off and auto-landing, velocity mode, a per-motor trim measured once and kept in flash, an on-board blackbox log, Bluetooth advertising so the app can find nearby drones, and camera video to the app with a XIAO ESP32-S3 Sense.

## The app

<img src="docs/images/app-flying.jpg" width="720" alt="The SparrowFlight app while flying: the climb and yaw stick on the left, the roll and pitch stick on the right, the height of 62 cm in the middle, the Land button and the STOP button">

SparrowFlight lists nearby drones over Bluetooth, joins the chosen drone's WiFi with one tap, and checks the drone before the sticks appear. It flies with button take-off and landing or with the thrust stick setting the hold height, shows the drone's camera behind the sticks, and edits the flight parameters. A console and a flight log for every connection help with tuning. The app collects no personal data.

See [software/README.md](software/README.md) for the full guide, and the [privacy policy](software/PRIVACY.md).

## Repository

| Folder | Contents |
|---|---|
| [`software/`](software/) | the SparrowFlight app: guide and privacy policy (the app itself is not open source) |
| [`hardware/`](hardware/) | the frame and the flight controller (to be published) |
| [`firmware/`](firmware/) | the flight firmware (to be published) |
| [`docs/`](docs/) | photos and a test flight video |

## Support

Questions and problem reports: see [SUPPORT.md](SUPPORT.md).

## Safety

LiPo batteries can catch fire when damaged, shorted or overcharged; charge them with a proper charger and never leave them unattended. Spinning propellers cut. Remove the propellers when you work on the drone with the battery connected, fly away from people, and follow your local drone regulations. The designs and software come with no warranty.

## Name

The names "SparrowFlight" and "Sparrow" and the Sparrow logo are not covered by the licences in this repository. Once the designs are published, you may build, modify and share Sparrow under their licences, but do not present your version as the official one.

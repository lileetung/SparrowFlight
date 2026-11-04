<p align="center">
  <img src="docs/images/sparrow-logo.png" width="200" alt="Sparrow logo: a pixel-art sparrow in flight">
</p>

<h1 align="center">SparrowFlight</h1>

<p align="center">Build your own micro quadcopter</p>

<p align="center">English | <a href="README.zh-TW.md">繁體中文</a></p>

SparrowFlight is a micro quadcopter platform designed to be built from widely available parts. Its drone, Sparrow, combines off-the-shelf modules (a Seeed XIAO ESP32-S3, a GY-521 IMU and a GY-530 time-of-flight sensor) with a two-layer flight controller that standard PCB services can manufacture and assemble, a carbon-fibre frame cut from a single 100 x 100 mm sheet, and common 720 coreless motors. It is intended for makers, students and educators who want a small drone they can build, understand and modify.

The flight controller (CERN-OHL-S-2.0) and the firmware (GPL-3.0-or-later) are open source; the frame is published under the non-commercial CC BY-NC-SA 4.0 licence because it is adapted from LiteWing. Their files and build instructions are in [hardware/](hardware/README.md) and [firmware/](firmware/README.md). Sparrow is flown with the iOS app SparrowFlight.

<p align="center">
  <img src="docs/images/sparrow-3d-top.png" width="380" alt="3D render of Sparrow from above: the carbon-fibre frame with its four snap-on legs, four coreless motors held in O-type double-flanged cable grommets with 55 mm propellers, and the flight controller hung underneath">
  <img src="docs/images/sparrow-3d-bottom.png" width="380" alt="3D render of Sparrow from below: the flight controller on four silicone grommets under the frame, with the XIAO ESP32-S3 and the GY-530 ToF sensor facing the ground">
</p>

<p align="center"><sub>3D renders; the GY-521 between the flight controller and the frame, the antenna, the battery and the wires are not shown. Photos of the built drone are in <a href="docs/photos/README.md">docs/photos</a>.</sub></p>

## Highlights

- **Off-the-shelf parts.** The microcontroller and the sensors are breakout modules sold by common electronics suppliers; the motors, propellers and battery are standard micro-drone parts.
- **Height hold.** A downward time-of-flight sensor holds the height above the surface below, and drives button take-off and automatic landing.
- **Phone control over WiFi.** The drone runs its own access point and advertises over Bluetooth, and it also accepts ESP-Drone's commander packets.
- **Flight log on board.** A blackbox records recent flights in flash for later analysis.

## Demo

<a href="docs/videos/README.md"><img src="docs/images/test-flight.gif" width="240" alt="Sparrow taking off and hovering, flown from an iPhone"></a>

Full clip (30 s): [docs/videos](docs/videos/README.md).

## Specifications and parts

Sparrow is built from parts sold by common electronics and hobby shops. JLCPCB makes and assembles the flight controller from the files in [hardware/flight-controller/](hardware/flight-controller/README.md), and a carbon-fibre CNC shop cuts the frame from [its DXF](hardware/frame/README.md).

| Part | Specification | Where to buy |
|---|---|---|
| Frame | 100 x 100 mm, 1.5 mm 3K carbon fibre, four snap-on landing-gear legs, cut from one DXF | Shopee (carbon-fibre CNC shop) |
| Flight controller | 40 x 40 mm, 2 layers; TPS63020 buck-boost converter, slide power switch and four low-side MOSFET motor drivers; hung under the frame on silicone grommets | JLCPCB |
| [Seeed XIAO ESP32-S3 Plus](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/) | flight computer with its WiFi antenna: control loop, motor outputs, WiFi access point and Bluetooth for the app; the camera expansion board of a XIAO ESP32-S3 Sense, fitted to the Plus, adds a downward camera | Shopee |
| GY-521 ([MPU-6050](https://invensense.tdk.com/products/motion-tracking/6-axis/mpu-6050/)) | 6-axis IMU (gyroscope and accelerometer) for the attitude estimate | Jin-Hua Electronics |
| GY-530 ([VL53L0X](https://www.st.com/en/imaging-and-photonics-solutions/vl53l0x.html)) | downward time-of-flight rangefinder for height hold | Jin-Hua Electronics |
| 720 coreless brushed motors | 7 x 20 mm, 1 mm shaft; two run clockwise and two counter-clockwise | Jin-Hua Electronics |
| Propellers | 55 mm for 1 mm shafts, in clockwise and counter-clockwise pairs | Shopee |
| Battery | 1S LiPo, 400 mAh 20C, PH2.0 plug; about 3.5-4 minutes of flight | Shopee |
| Mounting parts | O-type double-flanged cable grommets as motor collars (7 mm inside), M2 x 14 mm screws, M2 nylon lock nuts, 2.6 x 5 x 1 mm nylon washers, M2 silicone grommets, a battery strap up to 12 mm wide | Shopee |

Shops: [JLCPCB](https://jlcpcb.com/), [Jin-Hua Electronics](https://www.jin-hua.com.tw/) and [Shopee](https://shopee.tw/), the last two in Taiwan.

## Build your own

1. **Frame:** send [the DXF](hardware/frame/README.md) to a carbon-fibre CNC shop and ask for 1.5 mm 3K plate.
2. **Flight controller:** order the board from JLCPCB, assembled, with the [Gerber, BOM and pick-and-place files](hardware/flight-controller/README.md#ordering-from-jlcpcb).
3. **Parts:** buy the modules, motors, propellers, battery and mounting parts in the table above.
4. **Assembly:** solder the modules and the motors to the board ([flight controller](hardware/flight-controller/README.md#assembly)), then fit the motors and the board to the frame ([frame](hardware/frame/README.md#assembly)).
5. **Firmware:** build it and flash it over USB with PlatformIO ([firmware](firmware/README.md#build-and-flash)).
6. **Fly:** install [SparrowFlight](https://apps.apple.com/app/id6829419743) and follow the [app guide](software/README.md).

## App

<img src="docs/images/app-flying.jpg" width="720" alt="The SparrowFlight app while flying: the climb and yaw stick on the left, the roll and pitch stick on the right, the height of 62 cm in the middle, the Land button and the STOP button">

SparrowFlight for iOS discovers nearby drones over Bluetooth, joins the selected drone's WiFi network, and checks the drone before enabling the controls. It offers button take-off and landing or thrust-stick height control, shows the drone's camera behind the sticks, and edits flight parameters. A console and per-connection flight logs support tuning. The app collects no personal data.

Available on the [App Store](https://apps.apple.com/app/id6829419743). Documentation: [app guide](software/README.md) and [privacy policy](software/PRIVACY.md).

## Support and contributing

For questions and problem reports, see [SUPPORT.md](SUPPORT.md). How to contribute is described in [CONTRIBUTING.md](CONTRIBUTING.md), and everyone taking part is expected to follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## Licence

The frame is licensed under CC BY-NC-SA 4.0 (adapted from [LiteWing](https://github.com/Circuit-Digest/LiteWing)), the flight controller under CERN-OHL-S-2.0 and the firmware under GPL-3.0-or-later, each together with the READMEs and images in its folder. The rest of the documentation and the photos are under CC BY-SA 4.0. See [LICENSE.md](LICENSE.md).

The names "SparrowFlight" and "Sparrow" and the Sparrow logo are not covered by these licences. Derived versions may be built and shared under their licences, but must not be presented as official.

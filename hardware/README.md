# Hardware

Sparrow's frame and flight controller.

| Part | Version | Files | Licence |
|---|---|---|---|
| [Frame](frame/README.md) | 1.0 | DXF for CNC milling, 1.5 mm carbon fibre | [CC BY-NC-SA 4.0](frame/LICENSE) |
| [Flight controller](flight-controller/README.md) | 1.0 | EasyEDA Pro source, schematic, Gerber, BOM, pick-and-place | [CERN-OHL-S-2.0](flight-controller/LICENSE) |

**Frame:** 100 x 100 mm, 1.5 mm carbon fibre, with snap-on landing gear and rings for four 720 coreless motors on 55 mm propellers. It is adapted from [LiteWing](https://github.com/Circuit-Digest/LiteWing) by Circuit Digest; see [its attribution](frame/ATTRIBUTION.md).

**Flight controller:** 40 x 40 mm, 2 layers, hung under the frame on silicone grommets. It carries a Seeed XIAO ESP32-S3 Plus (the camera expansion board of a XIAO ESP32-S3 Sense adds a camera), an MPU-6050 IMU on a GY-521 module, a downward VL53L0X ToF rangefinder on a GY-530 module, a TPS63020 buck-boost for the 1S battery, and low-side MOSFET drivers for the four brushed motors.

<p>
  <img src="frame/sparrow-frame-v1.0-3d.png" width="380" alt="3D render of the assembled frame: the carbon-fibre plate with the four landing-gear legs snapped onto the motor rings">
  <img src="flight-controller/sparrow-fc-v1.0-3d-top.png" width="380" alt="3D render of the flight controller as JLCPCB delivers it, with the SMD parts and the sparrow logo">
</p>

Photos of the built parts are in [docs/photos](../docs/photos/README.md).

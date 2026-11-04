# Licences

Each kind of work in SparrowFlight has its own licence. The frame, flight-controller and firmware folders each hold their licence text, and the READMEs and images in a folder are under that folder's licence.

| Path | Licence | In short |
|---|---|---|
| `hardware/frame/` | [CC BY-NC-SA 4.0](hardware/frame/LICENSE) | Copy and modify it, credit SparrowFlight and LiteWing, no commercial use, share changes under the same licence. Required because the frame is adapted from LiteWing ([attribution](hardware/frame/ATTRIBUTION.md)). |
| `hardware/flight-controller/` | [CERN-OHL-S-2.0](hardware/flight-controller/LICENSE) | Make, modify and sell boards; if you distribute boards or modified designs, make the complete design sources available under the same licence. |
| `firmware/` | [GPL-3.0-or-later](firmware/LICENSE) | Modify and distribute the firmware; if you distribute it, including inside a product, provide its source under GPL-3.0 or a later version. |
| `docs/` and the remaining files | [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) | Copy and modify with credit, share changes under the same licence. |

The names "SparrowFlight" and "Sparrow" and the Sparrow logo are not licensed under any of these terms.

Third-party parts keep their own licences: the XIAO ESP32-S3, GY-521 and GY-530 modules are bought, not part of this design, and the firmware's PlatformIO libraries (VL53L0X by Pololu, NimBLE-Arduino) are fetched at build time.

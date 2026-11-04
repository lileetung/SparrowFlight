# Attribution

The body and arm outline of the Sparrow frame is adapted from **LiteWing V2.6.C** by Circuit Digest / Semicon Lab:
https://github.com/Circuit-Digest/LiteWing (folder `hardware/LieWingV2.6.C`, commit
be00f916ec33d5a4a76cdd4e53558e346165428c).

The landing gear follows LiteWing's leg interface: the arm-slot and motor-ring-notch positions of LiteWing V2.6.C, and
the leg outline of LiteWing V2.5C (`hardware/LiteWingV2.5C/PCB_Legs_Gerber/LiteWingLegs.zip`, same repository).

LiteWing is licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0).
As a derivative work, the Sparrow frame is released under the same licence; see [LICENSE](LICENSE).

## Changes from LiteWing

- Closed 9 mm motor rings instead of the C-shaped motor clips.
- The landing gear snaps on through a 1.65 mm flex slit (LiteWing: 0.45 mm). All openings are plate thickness + 0.2 mm
  with R0.75 corners, and the frame ends are moved inward to make up for the rounded corners, so the legs keep
  LiteWing's notch depth.
- 1.5 mm carbon fibre instead of a PCB; the legs are separate pieces nested in the same 100 x 100 mm sheet.
- 2.2 x 12 mm battery-strap slots at x = +-19.9 mm and a 30.5 mm M2 flight-controller hole pattern.
- No front T piece, rear frame or small motor slots, and no electronics: the frame is a bare plate and the flight
  controller is a separate board.

The rest of the SparrowFlight project (flight controller, firmware and app) does not use LiteWing material.

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 lileetung
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial"]
# ///

import argparse
import glob
import json
import pathlib
import re
import sys
import time

import serial

MAX_SPREAD_G = 0.02


def default_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("no /dev/cu.usbmodem* port; is the drone on USB?")
    return ports[0]


def command(port, line, pattern, timeout=8.0):
    with serial.Serial(port, 115200, timeout=0.1) as s:
        time.sleep(0.3)
        s.reset_input_buffer()
        s.write((line + "\n").encode())
        deadline = time.time() + timeout
        while time.time() < deadline:
            text = s.readline().decode(errors="replace").strip()
            if text.startswith("#"):
                print(text)
            m = re.search(pattern, text)
            if m:
                return m
    sys.exit(f"no reply to '{line}'")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", choices=["capture", "solve", "list", "clear"])
    ap.add_argument("name", nargs="?", help="face name for capture")
    ap.add_argument("--file", default="logs/six-face.json", help="captures (default %(default)s)")
    ap.add_argument("--port", default=None)
    ap.add_argument("--apply", action="store_true", help="solve: store the result with unitcal acc")
    args = ap.parse_args()
    path = pathlib.Path(args.file)
    faces = json.loads(path.read_text()) if path.exists() else {}

    if args.action == "clear":
        path.unlink(missing_ok=True)
        print(f"cleared {path}")
        return
    if args.action == "list":
        for name, v in faces.items():
            print(f"{name:12s} {v[0]:+.4f} {v[1]:+.4f} {v[2]:+.4f}")
        return
    if args.action == "capture":
        if not args.name:
            sys.exit("capture needs a face name")
        m = command(args.port or default_port(), "unitcal raw",
                    r"unitcal raw (\S+) (\S+) (\S+) g spread (\S+)")
        v, spread = [float(m.group(i)) for i in (1, 2, 3)], float(m.group(4))
        if spread > MAX_SPREAD_G:
            sys.exit(f"spread {spread:.4f} g > {MAX_SPREAD_G}: the frame moved, capture again")
        faces[args.name] = v
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(faces, indent=1))
        print(f"{args.name}: {v[0]:+.4f} {v[1]:+.4f} {v[2]:+.4f} g ({len(faces)} faces in {path})")
        return

    if len(faces) < 6:
        sys.exit(f"only {len(faces)} faces captured, need 6")
    bias, scale, used = [], [], set()
    for k, axis in enumerate("xyz"):
        pos = max(faces, key=lambda n: faces[n][k])
        neg = min(faces, key=lambda n: faces[n][k])
        p, n = faces[pos][k], faces[neg][k]
        if p < 0.7 or n > -0.7:
            sys.exit(f"{axis}: no face reads about +1 g and -1 g ({pos} {p:+.3f}, {neg} {n:+.3f})")
        used |= {pos, neg}
        bias.append((p + n) / 2)
        scale.append(2 / (p - n))
        print(f"{axis}: +1 g from {pos} ({p:+.4f}), -1 g from {neg} ({n:+.4f}) -> "
              f"bias {bias[-1]:+.6f} scale {scale[-1]:.6f}")
    if len(used) != 6:
        sys.exit("each axis needs its own two faces; check the captures with 'list'")
    cmd = "unitcal acc " + " ".join(f"{b:.6f}" for b in bias) + " " + " ".join(f"{s:.6f}" for s in scale)
    print(cmd)
    if args.apply:
        command(args.port or default_port(), cmd, r"unitcal (flash|refused)")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 lileetung
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial"]
# ///

import argparse
import glob
import pathlib
import sys
import time

import serial


def default_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        raise SystemExit("No /dev/cu.usbmodem* found; connect the flight controller over USB first")
    if len(ports) > 1:
        raise SystemExit("Several USB serial ports found; choose one with --port: " + ", ".join(ports))
    return ports[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output", nargs="?", help="output CSV (default: named with the date and time)")
    parser.add_argument("--port", help="for example /dev/cu.usbmodem101")
    parser.add_argument("--seq", type=int, default=0, help="seq to download (default: the latest; see bb list)")
    parser.add_argument("--timeout", type=float, default=30.0, help="seconds to wait for the download")
    args = parser.parse_args()

    port = args.port or default_port()
    output = pathlib.Path(
        args.output or time.strftime("blackbox-%Y%m%d-%H%M%S.csv")
    )
    print(f"Connecting to {port}, requesting {'seq ' + str(args.seq) if args.seq else 'the latest'} blackbox...")

    rows = []
    fields = None
    metadata = None
    deadline = time.monotonic() + args.timeout
    with serial.Serial(port, 115200, timeout=0.25) as device:
        time.sleep(1.0)
        device.reset_input_buffer()
        device.write(b"bb list\n")
        device.flush()
        time.sleep(0.15)
        device.write(f"bb dump {args.seq}\n".encode() if args.seq else b"bb dump\n")
        device.flush()

        while time.monotonic() < deadline:
            raw = device.readline()
            if not raw:
                continue
            line = raw.decode(errors="replace").strip()
            if line.startswith("# BB slot=") or line.startswith("# BB fs="):
                print(line)
            if line.startswith("# BB_BEGIN"):
                metadata = line
                print(line)
            elif line.startswith("# BB_FIELDS,"):
                fields = line.removeprefix("# BB_FIELDS,")
            elif line.startswith("B,"):
                rows.append(line[2:])
            elif line.startswith("# BB_END"):
                print(line)
                break
            elif line == "# no valid blackbox log":
                raise SystemExit("The flight controller holds no valid blackbox record")
        else:
            raise SystemExit("Download timed out; check that the flight controller is in USB dry-run with the motors stopped")

    if not metadata or not fields or not rows:
        raise SystemExit("The blackbox data received is incomplete; nothing was written")
    output.write_text(fields + "\n" + "\n".join(rows) + "\n", encoding="utf-8")
    print(f"Saved {len(rows)} rows (about {len(rows) * 0.02:.1f} s) to {output}")


if __name__ == "__main__":
    try:
        main()
    except serial.SerialException as error:
        print(f"serial error: {error}", file=sys.stderr)
        raise SystemExit(1) from error

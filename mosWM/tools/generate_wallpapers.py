#!/usr/bin/env python3
"""Convert the supplied JPEG wallpapers to bounded-size QOI files."""

from pathlib import Path
import struct
import sys

from PIL import Image


MAX_DIMENSION = 1600


def encode_qoi(image):
    width, height = image.size
    pixels = image.convert("RGB").tobytes()
    output = bytearray(b"qoif")
    output.extend(struct.pack(">IIBB", width, height, 3, 0))
    index = [(0, 0, 0, 0)] * 64
    previous = (0, 0, 0, 255)
    run = 0
    for offset in range(0, len(pixels), 3):
        pixel = (*pixels[offset:offset + 3], 255)
        if pixel == previous:
            run += 1
            if run == 62 or offset + 3 == len(pixels):
                output.append(0xC0 | (run - 1))
                run = 0
            continue
        if run:
            output.append(0xC0 | (run - 1))
            run = 0
        slot = (pixel[0] * 3 + pixel[1] * 5 + pixel[2] * 7 + 255 * 11) % 64
        if index[slot] == pixel:
            output.append(slot)
        else:
            index[slot] = pixel
            dr = pixel[0] - previous[0]
            dg = pixel[1] - previous[1]
            db = pixel[2] - previous[2]
            if -2 <= dr <= 1 and -2 <= dg <= 1 and -2 <= db <= 1:
                output.append(0x40 | ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2))
            else:
                dr_dg = dr - dg
                db_dg = db - dg
                if -32 <= dg <= 31 and -8 <= dr_dg <= 7 and -8 <= db_dg <= 7:
                    output.extend((0x80 | (dg + 32), ((dr_dg + 8) << 4) | (db_dg + 8)))
                else:
                    output.extend((0xFE, pixel[0], pixel[1], pixel[2]))
        previous = pixel
    output.extend(b"\x00\x00\x00\x00\x00\x00\x00\x01")
    return output


def main():
    root = Path(__file__).resolve().parents[2]
    source_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "use"
    output_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else root / "mosWM/assets/wallpapers"
    output_dir.mkdir(parents=True, exist_ok=True)
    images = sorted(source_dir.glob("*.jpg"))
    if not images:
        raise SystemExit(f"No JPEG wallpapers found in {source_dir}")
    for source in images:
        with Image.open(source) as image:
            image.thumbnail((MAX_DIMENSION, MAX_DIMENSION), Image.Resampling.LANCZOS)
            target = output_dir / f"{source.stem}.qoi"
            target.write_bytes(encode_qoi(image))
            print(f"{source.name} -> {target.name} ({image.width}x{image.height})")


if __name__ == "__main__":
    main()
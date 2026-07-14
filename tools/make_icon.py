"""Build a compact PNG-compressed Windows ICO from a square source image."""

from __future__ import annotations

import argparse
import io
import struct
from pathlib import Path

from PIL import Image, ImageDraw


SIZES = (16, 24, 32, 48, 64, 128, 256)


def quantized_png(image: Image.Image, size: int) -> bytes:
    resized = image.resize((size, size), Image.Resampling.LANCZOS)
    colors = 128 if size < 128 else 192
    quantized = resized.quantize(colors=colors, method=Image.Quantize.FASTOCTREE)
    output = io.BytesIO()
    quantized.save(output, "PNG", optimize=True)
    return output.getvalue()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("--png", type=Path, required=True)
    parser.add_argument("--ico", type=Path, required=True)
    args = parser.parse_args()

    image = Image.open(args.source).convert("RGBA")
    image = image.resize((1024, 1024), Image.Resampling.LANCZOS)

    # The generated tile already has rounded corners. A deterministic mask removes
    # the white canvas around it without touching the white cursor in the emblem.
    mask = Image.new("L", image.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, 1023, 1023), radius=178, fill=255)
    image.putalpha(mask)

    args.png.parent.mkdir(parents=True, exist_ok=True)
    preview = image.resize((256, 256), Image.Resampling.LANCZOS)
    preview.quantize(colors=192, method=Image.Quantize.FASTOCTREE).save(
        args.png, "PNG", optimize=True
    )

    frames = [(size, quantized_png(image, size)) for size in SIZES]
    offset = 6 + 16 * len(frames)
    entries: list[bytes] = []
    for size, data in frames:
        encoded_size = 0 if size == 256 else size
        entries.append(
            struct.pack(
                "<BBBBHHII",
                encoded_size,
                encoded_size,
                0,
                0,
                1,
                32,
                len(data),
                offset,
            )
        )
        offset += len(data)

    args.ico.write_bytes(
        struct.pack("<HHH", 0, 1, len(frames))
        + b"".join(entries)
        + b"".join(data for _, data in frames)
    )


if __name__ == "__main__":
    main()


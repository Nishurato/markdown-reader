"""Generate fixed fixtures or collect process-isolated Markdown Reader samples."""

import argparse
import json
import statistics
import struct
import subprocess
import time
from pathlib import Path

DOCUMENT_BYTES = 1024 * 1024
PARAGRAPH = b"Markdown Reader measures text layout and scrolling in a reproducible document. "


def write_bitmap(path, seed):
    width, height = 384, 256
    pixels = bytes(
        value
        for y in range(height)
        for x in range(width)
        for value in ((x + seed * 23) % 256, (y + seed * 41) % 256, (x + y + seed * 17) % 256)
    )
    header = struct.pack("<2sIHHI", b"BM", 54 + len(pixels), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(pixels), 0, 0, 0, 0)
    path.write_bytes(header + info + pixels)


def write_document(path, image_names):
    content = bytearray(b"# Markdown Reader benchmark\n\n")
    for section in range(48):
        content.extend(f"## Section {section + 1:02d}\n\n".encode("ascii"))
        if section < len(image_names):
            content.extend(f"![Fixture {section + 1}](images/{image_names[section]})\n\n".encode("ascii"))
        content.extend(b"- First item\n- Second item\n\n1. Numbered item\n\n")
        content.extend(b"| Key | Value |\n| --- | --- |\n| Layout | Qt Widgets |\n\n")
        content.extend(b"```cpp\nint value = 42;\n```\n\n")
        content.extend((PARAGRAPH * 8 + b"\n\n") * 30)
    remaining = DOCUMENT_BYTES - len(content) - 1
    if remaining < 0:
        raise ValueError("Fixture exceeds the target size")
    paragraph = PARAGRAPH * 8 + b"\n\n"
    content.extend((paragraph * (remaining // len(paragraph) + 1))[:remaining])
    content.extend(b"\n")
    path.write_bytes(content)


def generate(directory):
    images = directory / "images"
    images.mkdir(parents=True, exist_ok=True)
    image_names = [f"fixture-{i}.bmp" for i in range(1, 5)]
    for i, name in enumerate(image_names, 1):
        write_bitmap(images / name, i)
    write_document(directory / "mixed.md", [])
    write_document(directory / "with-images.md", image_names)
    print(f"Markdown: {DOCUMENT_BYTES} bytes per file")
    print(f"Images: {sum((images / name).stat().st_size for name in image_names)} bytes total")


def collect(executable, document, runs, warmups):
    samples = []
    command = [str(executable.resolve()), str(document.resolve())]
    for index in range(warmups + runs):
        launch_ms = time.time_ns() // 1_000_000
        result = subprocess.run(
            command, check=True, capture_output=True, text=True, timeout=180,
        )
        sample = json.loads(result.stdout)
        sample["launch_to_paint_ms"] = sample.pop("first_paint_epoch_ms") - launch_ms
        if any(value < 0 for value in sample.values()):
            raise RuntimeError(f"Unavailable metric: {sample}")
        if index >= warmups:
            samples.append(sample)
    median = {key: statistics.median(row[key] for row in samples) for key in samples[0]}
    return {"document": document.name, "samples": samples, "median": median}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)
    generator = subcommands.add_parser("generate")
    generator.add_argument("directory", type=Path)
    runner = subcommands.add_parser("run")
    runner.add_argument("executable", type=Path)
    runner.add_argument("document", type=Path)
    runner.add_argument("--runs", type=int, default=5)
    runner.add_argument("--warmups", type=int, default=1)
    args = parser.parse_args()
    if args.command == "generate":
        generate(args.directory)
    elif args.runs < 1 or args.warmups < 0:
        parser.error("runs must be positive and warmups must be nonnegative")
    else:
        try:
            report = collect(args.executable, args.document, args.runs, args.warmups)
        except subprocess.CalledProcessError as error:
            parser.exit(1, error.stderr or str(error))
        print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()

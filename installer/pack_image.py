#!/usr/bin/env python3
"""Pack installer disk images as zero-block streams for fast UEFI staging."""
import argparse
import struct
from pathlib import Path

MAGIC = b"SNZBLK1\0"
BLOCK_SIZE = 64 * 1024
HEADER = struct.Struct("<8sQI")


def pack(source: Path, target: Path) -> None:
    size = source.stat().st_size
    target.parent.mkdir(parents=True, exist_ok=True)
    with source.open("rb") as src, target.open("wb") as dst:
        dst.write(HEADER.pack(MAGIC, size, BLOCK_SIZE))
        while block := src.read(BLOCK_SIZE):
            if not any(block):
                dst.write(b"\0")
            else:
                dst.write(b"\1")
                dst.write(block)


def unpack(source: Path, target: Path) -> None:
    with source.open("rb") as src:
        header = src.read(HEADER.size)
        if len(header) != HEADER.size:
            raise ValueError("truncated block-stream header")
        magic, size, block_size = HEADER.unpack(header)
        if magic != MAGIC or not size or block_size != BLOCK_SIZE:
            raise ValueError("invalid block-stream header")
        target.parent.mkdir(parents=True, exist_ok=True)
        remaining = size
        with target.open("wb") as dst:
            while remaining:
                tag = src.read(1)
                if not tag:
                    raise ValueError("truncated block-stream payload")
                count = min(block_size, remaining)
                if tag == b"\0":
                    dst.write(bytes(count))
                elif tag == b"\1":
                    block = src.read(count)
                    if len(block) != count:
                        raise ValueError("truncated raw block")
                    dst.write(block)
                else:
                    raise ValueError("unknown block-stream tag")
                remaining -= count
            if src.read(1):
                raise ValueError("trailing bytes after block-stream payload")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("pack", "unpack"))
    parser.add_argument("source", type=Path)
    parser.add_argument("target", type=Path)
    args = parser.parse_args()
    if args.command == "pack":
        pack(args.source, args.target)
    else:
        unpack(args.source, args.target)


if __name__ == "__main__":
    main()

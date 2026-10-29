#!/usr/bin/env python3
"""Pack installer disk images as zero-block streams for fast UEFI staging."""
import argparse
import os
import struct
from pathlib import Path
import tempfile

MAGIC = b"SNZBLK1\0"
BLOCK_SIZE = 64 * 1024
HEADER = struct.Struct("<8sQI")


def pack(source: Path, target: Path) -> None:
    source = source.resolve(strict=True)
    target = target.resolve(strict=False)
    if source == target:
        raise ValueError("source and target must be different files")
    size = source.stat().st_size
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=target.name + ".", suffix=".tmp", dir=target.parent)
    try:
        with os.fdopen(fd, "wb") as dst, source.open("rb") as src:
            dst.write(HEADER.pack(MAGIC, size, BLOCK_SIZE))
            while block := src.read(BLOCK_SIZE):
                if not any(block):
                    dst.write(b"\0")
                else:
                    dst.write(b"\1")
                    dst.write(block)
            dst.flush()
            os.fsync(dst.fileno())
        os.replace(temporary, target)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def unpack(source: Path, target: Path) -> None:
    source = source.resolve(strict=True)
    target = target.resolve(strict=False)
    if source == target:
        raise ValueError("source and target must be different files")
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=target.name + ".", suffix=".tmp", dir=target.parent)
    try:
        with os.fdopen(fd, "wb") as dst, source.open("rb") as src:
            header = src.read(HEADER.size)
            if len(header) != HEADER.size:
                raise ValueError("truncated block-stream header")
            magic, size, block_size = HEADER.unpack(header)
            if magic != MAGIC or not size or block_size != BLOCK_SIZE:
                raise ValueError("invalid block-stream header")
            remaining = size
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
            dst.flush()
            os.fsync(dst.fileno())
        os.replace(temporary, target)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


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

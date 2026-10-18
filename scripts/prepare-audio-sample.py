#!/usr/bin/env python3
"""Download a public-domain WAV, convert one second to HDA-ready PCM, and embed it."""
import io
import struct
import sys
import wave
from pathlib import Path

DST_RATE = 48000
SECONDS = 1
out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/audio_sample.h")
source = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("build/audio-sample.wav")
downloaded = source.read_bytes()

with wave.open(io.BytesIO(downloaded), "rb") as src:
    if (src.getnchannels(), src.getsampwidth(), src.getcomptype()) != (2, 2, "NONE"):
        raise SystemExit("Input sample must be uncompressed 16-bit stereo PCM")
    SRC_RATE = src.getframerate()
    if SRC_RATE < 8000 or SRC_RATE > 192000:
        raise SystemExit(f"Unsupported input sample rate: {SRC_RATE}")
    frames = src.readframes(min(SRC_RATE * SECONDS, src.getnframes()))

count = len(frames) // 4
samples = struct.unpack("<" + "h" * (count * 2), frames)
out_frames = []
# Linear resampling: the kernel receives ordinary 48 kHz S16 stereo WAV PCM.
for i in range(DST_RATE * SECONDS):
    pos_num = i * SRC_RATE
    left = pos_num // DST_RATE
    frac = pos_num % DST_RATE
    right = min(left + 1, count - 1)
    for ch in range(2):
        a = samples[left * 2 + ch]
        b = samples[right * 2 + ch]
        value = (a * (DST_RATE - frac) + b * frac) // DST_RATE
        value = max(-32768, min(32767, value))
        out_frames.append(value)

pcm = struct.pack("<" + "h" * len(out_frames), *out_frames)
wav_io = io.BytesIO()
with wave.open(wav_io, "wb") as dst:
    dst.setnchannels(2)
    dst.setsampwidth(2)
    dst.setframerate(DST_RATE)
    dst.writeframes(pcm)
wav_bytes = wav_io.getvalue()

with out.open("w", encoding="ascii") as f:
    f.write("/* Generated from a downloaded public-domain WAV sample; do not edit. */\n")
    f.write("#pragma once\n#include <stdint.h>\n")
    f.write("static const uint8_t sentinel_wav[] =\n")
    for i in range(0, len(wav_bytes), 32):
        f.write('    "' + ''.join(f"\\x{b:02x}" for b in wav_bytes[i:i+32]) + '"\n')
    f.write(";\n")
    f.write("#define sentinel_wav_start sentinel_wav\n")
    f.write("#define sentinel_wav_end (sentinel_wav + sizeof(sentinel_wav) - 1u)\n")

print(f"Downloaded WAV prepared: {len(wav_bytes)} bytes; 1 second, 48 kHz, 16-bit stereo -> {out}")

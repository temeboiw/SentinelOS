#!/usr/bin/env python3
"""Download a public-domain WAV and embed its first second as HDA PCM."""
import sys
import urllib.request
import wave
import io
from pathlib import Path

URL = "https://pnb-website-bucket.s3.us-east-2.amazonaws.com/samples/audio/wav/sample-audio-10s-16bit-44khz.wav"
RATE = 44100
FRAMES = RATE  # Keep the embedded sample small: one second.
out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/audio_sample.h")

request = urllib.request.Request(URL, headers={"User-Agent": "SentinelOS-audio-test/1.0"})
with urllib.request.urlopen(request, timeout=30) as response:
    raw = response.read()

with wave.open(io.BytesIO(raw), "rb") as wav:
    if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getcomptype()) != (2, 2, RATE, "NONE"):
        raise SystemExit("Downloaded WAV is not uncompressed 44.1 kHz, 16-bit stereo PCM")
    pcm = wav.readframes(min(FRAMES, wav.getnframes()))

if not pcm:
    raise SystemExit("Downloaded WAV contains no PCM samples")

# HDA plays the downloaded PCM directly; no tone is synthesized in the kernel.
with out.open("w", encoding="ascii") as f:
    f.write("/* Generated from the downloaded public-domain WAV sample. */\n")
    f.write("#pragma once\n#include <stdint.h>\n")
    f.write(f"#define HDA_SAMPLE_BYTES {len(pcm)}u\n")
    f.write("#define HDA_SAMPLE_RATE_CODE 0x4011u /* 44.1 kHz, 16-bit stereo */\n")
    f.write("static const uint8_t hda_sample_pcm[] =\n")
    for i in range(0, len(pcm), 32):
        f.write('    "' + ''.join(f"\\x{b:02x}" for b in pcm[i:i+32]) + '"\n')
    f.write(";\n")
print(f"Prepared downloaded WAV PCM: {len(pcm)} bytes, 44.1 kHz, 16-bit stereo -> {out}")

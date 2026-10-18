#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WAV="$ROOT/build/sentinel-audio.wav"

echo "==> Building SentinelOS with a downloaded WAV sample and launching HDA playback capture"
"$ROOT/scripts/build.sh" --run

echo "==> Checking captured WAV: $WAV"
python3 - "$WAV" <<'PY'
import pathlib, struct, sys
p = pathlib.Path(sys.argv[1])
if not p.is_file():
    raise SystemExit("FAIL: QEMU did not create sentinel-audio.wav")
b = p.read_bytes()
if len(b) < 44 or b[:4] != b"RIFF" or b[8:12] != b"WAVE":
    raise SystemExit(f"FAIL: not a valid RIFF/WAVE file ({len(b)} bytes)")
data = b.find(b"data")
if data < 0 or data + 8 > len(b):
    raise SystemExit("FAIL: WAV has no data chunk")
n = struct.unpack_from("<I", b, data + 4)[0]
if n == 0:
    raise SystemExit("FAIL: WAV data chunk is empty; no captured PCM")
pcm = b[data + 8:min(data + 8 + n, len(b))]
if not any(pcm):
    raise SystemExit("FAIL: PCM data is all zero")
print(f"PASS: WAV header valid, file={len(b)} bytes, PCM captured={len(pcm)} bytes")
print("Note: this checks that QEMU captured nonzero audio from the embedded WAV sample; it does not yet prove audible playback on physical hardware.")
PY

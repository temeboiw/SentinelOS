#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "==> HDA audible playback test using the downloaded WAV sample"
echo "==> QEMU will send guest audio to the PulseAudio server at 127.0.0.1."
echo "==> Start a PulseAudio-compatible server in Termux before running this test."
echo "==> No WAV recording or audio-capture validation is performed."
exec "$ROOT/scripts/build.sh" --run

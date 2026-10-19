
#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
B="$ROOT/build"
DISKIMG="$B/magic-disk.img"
VARS="$B/vars.fd"

# Display selection:
# auto   = VNC in proot/headless environments; window on a desktop
# window = force a graphical QEMU window
# vnc    = force VNC
MODE="${SENTINELOS_DISPLAY:-auto}"

# Detect common Termux/proot environment indicators.
IN_PROOT=0

if [[ -n "${PROOT_TMP_DIR:-}" ||
      -n "${PROOT_LOADER:-}" ||
      -n "${PROOT_BELONGS:-}" ]]; then
    IN_PROOT=1
elif [[ "${PREFIX:-}" == *"/com.termux/"* ||
        "${PREFIX:-}" == "/data/data/com.termux"* ]]; then
    IN_PROOT=1
elif grep -qi proot /proc/self/mountinfo 2>/dev/null; then
    IN_PROOT=1
fi

# Select the display automatically.
if [[ "$MODE" == "auto" ]]; then
    if (( IN_PROOT )); then
        MODE="vnc"
    elif [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
        MODE="window"
    else
        MODE="vnc"
    fi
fi

case "$MODE" in
    window)
        DISPLAY_ARGS=(-display gtk)
        echo "SentinelOS: graphical QEMU window"
        ;;
    vnc)
        # Localhost-only by default for security.
        VNC_BIND="${SENTINELOS_VNC_BIND:-127.0.0.1}"
        DISPLAY_ARGS=(-display none -vnc "${VNC_BIND}:0")
        echo "SentinelOS: VNC at ${VNC_BIND}:5900"
        ;;
    *)
        echo "Error: invalid SENTINELOS_DISPLAY mode: $MODE" >&2
        echo "Use auto, window, or vnc." >&2
        exit 1
        ;;
esac

# Find OVMF firmware.
OVMF_CODE=""

for c in \
    /usr/share/OVMF/OVMF_CODE.fd \
    /usr/share/OVMF/OVMF_CODE_4M.fd \
    /usr/share/edk2/ovmf/OVMF_CODE.fd \
    /usr/share/edk2-ovmf/x64/OVMF_CODE.fd
do
    if [[ -f "$c" ]]; then
        OVMF_CODE="$c"
        break
    fi
done

if [[ -z "$OVMF_CODE" ]]; then
    echo "Error: OVMF firmware not found." >&2
    exit 1
fi

# Verify required files.
if [[ ! -f "$DISKIMG" ]]; then
    echo "Error: disk image not found: $DISKIMG" >&2
    echo "Build SentinelOS first." >&2
    exit 1
fi

# Create writable UEFI variables if missing.
if [[ ! -f "$VARS" ]]; then
    dd if=/dev/zero of="$VARS" bs=1M count=2 status=none
fi

echo "SentinelOS: starting QEMU"
echo "Machine: Q35 | RAM: 1024 MiB | Network: QEMU user-mode NAT (E1000)"

exec qemu-system-x86_64 \
    -machine q35 \
    -m 1024 \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file="$VARS" \
    -drive file="$DISKIMG",format=raw,if=none,id=d0 \
    -device virtio-blk-pci,drive=d0 \
    -netdev user,id=net0,ipv6=off \
    -device e1000,netdev=net0,mac=52:54:00:12:34:56 \
    -audiodev "wav,id=audio0,path=$B/sentinel-audio.wav" \
    -device ich9-intel-hda,id=hda,debug=0 \
    -device hda-duplex,bus=hda.0,audiodev=audio0 \
    "${DISPLAY_ARGS[@]}" \
    -serial mon:stdio \
    -no-reboot

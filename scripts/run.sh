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

# The default is the broadly compatible 2D VirtIO-VGA device. Set
# SENTINELOS_GPU=none to exercise the firmware-framebuffer fallback.
# Opt in to VirGL only when QEMU and the host graphics stack support OpenGL.
GPU_ARGS=(-vga none -device virtio-vga)
if [[ "${SENTINELOS_GPU:-virtio}" == "none" ]]; then
    if [[ "${SENTINELOS_VIRGL:-0}" == "1" ]]; then
        echo "Error: SENTINELOS_GPU=none conflicts with SENTINELOS_VIRGL=1." >&2
        exit 1
    fi
    GPU_ARGS=()
    echo "SentinelOS: using QEMU's default VGA device (GPU fallback test)"
fi

if [[ "${SENTINELOS_VIRGL:-0}" == "1" ]]; then
    GPU_ARGS=(-vga none -device virtio-vga-gl)

    if [[ "$MODE" == "vnc" ]]; then
        # QEMU's egl-headless renderer is designed to pair with VNC/SPICE:
        # VNC carries the desktop image while EGL provides the OpenGL context
        # required by virtio-vga-gl/VirGL. Host EGL/GBM support is still needed.
        VNC_BIND="${SENTINELOS_VNC_BIND:-127.0.0.1}"
        DISPLAY_ARGS=(-display egl-headless -vnc "${VNC_BIND}:0")
        echo "SentinelOS: VNC + VirGL 3D (egl-headless; VNC at ${VNC_BIND}:5900; host EGL/GBM required)"
    else
        # GTK/SDL window backends require a host graphical display.
        VIRGL_BACKEND="${SENTINELOS_VIRGL_BACKEND:-gtk}"
        case "$VIRGL_BACKEND" in
            gtk|sdl) DISPLAY_ARGS=(-display "$VIRGL_BACKEND,gl=on") ;;
            *)
                echo "Error: unsupported SENTINELOS_VIRGL_BACKEND: $VIRGL_BACKEND" >&2
                echo "Choose gtk or sdl." >&2
                exit 1
                ;;
        esac
        echo "SentinelOS: requesting VirGL-capable VirtIO-VGA (backend=$VIRGL_BACKEND; host OpenGL required)"
    fi
fi

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
echo "Machine: Q35 | RAM: 1024 MiB"

exec qemu-system-x86_64 \
    -machine q35 \
    -smp 4 \
    -m 1024 \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file="$VARS" \
    -drive file="$DISKIMG",format=raw,if=none,id=d0 \
    -device virtio-blk-pci,drive=d0 \
    "${GPU_ARGS[@]}" \
    -audiodev "wav,id=audio0,path=$B/sentinel-audio.wav" \
    -device ich9-intel-hda,id=hda,debug=0 \
    -device hda-duplex,bus=hda.0,audiodev=audio0 \
    "${DISPLAY_ARGS[@]}" \
    -serial stdio \
    -no-reboot

#!/bin/bash
# Baut FFmpeg mit LibreELEC-Patches für Rockchip RK356X (ODROID M1)
# Aktiviert: V4L2 Request API, DRM Prime, HEVC rkvdec
#
# Ausführen: bash build-ffmpeg-rk356x.sh
# Installiert nach: /usr/local

set -euo pipefail

FFMPEG_VERSION="8.1"
FFMPEG_DIR="ffmpeg-${FFMPEG_VERSION}"
LIBREELEC_DIR="libreelec-patches"
INSTALL_PREFIX="/usr/local"
JOBS=$(nproc)

echo "=== FFmpeg ${FFMPEG_VERSION} für RK356X bauen ==="
echo "CPU-Kerne: $JOBS"
echo "Installiert nach: $INSTALL_PREFIX"
echo ""

# 1. Abhängigkeiten
echo "--- Abhängigkeiten installieren ---"
apt-get install -y \
    git wget \
    build-essential pkg-config \
    libdrm-dev \
    libudev-dev \
    libv4l-dev \
    libasound2-dev \
    libssl-dev \
    libx264-dev \
    libx265-dev \
    libvpx-dev \
    libmp3lame-dev \
    libopus-dev \
    libvorbis-dev \
    libfreetype6-dev \
    nasm yasm

echo ""

# 2. LibreELEC Patches holen
echo "--- LibreELEC Patches holen ---"
if [[ -d "$LIBREELEC_DIR" ]]; then
    echo "Bereits vorhanden, aktualisiere..."
    git -C "$LIBREELEC_DIR" pull --ff-only
else
    git clone --depth=1 \
        https://github.com/LibreELEC/LibreELEC.tv.git \
        "$LIBREELEC_DIR"
fi

PATCH_BASE="$LIBREELEC_DIR/packages/multimedia/ffmpeg/patches"

echo ""

# 3. FFmpeg herunterladen
echo "--- FFmpeg ${FFMPEG_VERSION} herunterladen ---"
if [[ ! -d "$FFMPEG_DIR" ]]; then
    wget -c "https://ffmpeg.org/releases/${FFMPEG_DIR}.tar.bz2"
    tar xf "${FFMPEG_DIR}.tar.bz2"
else
    echo "Bereits entpackt."
fi

cd "$FFMPEG_DIR"

echo ""

# 4. Patches anwenden (Reihenfolge für RK356X laut LibreELEC package.mk)
echo "--- Patches anwenden (RK356X) ---"

apply_patch() {
    local patch="$1"
    if [[ -f "$patch" ]]; then
        echo "  Anwenden: $(basename $patch)"
        patch -p1 --forward --batch < "$patch" || {
            echo "  [WARN] Patch bereits angewendet oder fehlgeschlagen: $(basename $patch)"
        }
    else
        echo "  [FEHLT] $patch"
    fi
}

apply_dir() {
    local dir="$1"
    if [[ -d "$dir" ]]; then
        for p in "$dir"/*.patch; do
            [[ -f "$p" ]] && apply_patch "$p"
        done
    fi
}

# Basis-Patches (immer)
apply_dir "../$PATCH_BASE/postproc"
apply_dir "../$PATCH_BASE/libreelec"

# RK356X spezifisch (laut package.mk Zeile 35)
apply_patch "../$PATCH_BASE/v4l2-request/ffmpeg-001-v4l2-request.patch"
# detlev-Patch erfordert v4l2_ctrl_hevc_ext_sps_st_rps — fehlt im Armbian-Kernel
# Kernel-Patch wäre nötig, aber riskant (Header, GPIO, W1, I2C, NetworkManager etc.)
# apply_patch "../$PATCH_BASE/detlev/ffmpeg-0001-wip-hevc-Add-support-for-sps_st_rps-control.patch"
apply_patch "../$PATCH_BASE/v4l2-drmprime/ffmpeg-001-v4l2-drmprime.patch"
apply_dir  "../$PATCH_BASE/vf-deinterlace-v4l2m2m"

echo ""

# 5. Konfigurieren
echo "--- Konfigurieren ---"
./configure \
    --prefix="$INSTALL_PREFIX" \
    --enable-shared \
    --disable-static \
    --enable-gpl \
    --enable-nonfree \
    --enable-v4l2-request \
    --enable-v4l2_m2m \
    --disable-vaapi \
    --enable-libdrm \
    --enable-libudev \
    --enable-neon \
    --enable-libx264 \
    --enable-libx265 \
    --enable-libvpx \
    --enable-libmp3lame \
    --enable-libopus \
    --enable-libvorbis \
    --enable-libfreetype \
    --enable-openssl \
    --enable-pthreads \
    --disable-debug \
    --disable-doc \
    2>&1 | tail -20

echo ""

# 6. Bauen
echo "--- Bauen (${JOBS} Kerne) ---"
make -j"$JOBS"

echo ""

# 7. Installieren
echo "--- Installieren nach $INSTALL_PREFIX ---"
make install
ldconfig

echo ""
echo "=== Fertig ==="
echo ""
echo "FFmpeg Version:"
"$INSTALL_PREFIX/bin/ffmpeg" -version 2>&1 | head -3
echo ""
echo "V4L2 Request API verfügbar:"
"$INSTALL_PREFIX/bin/ffmpeg" -hwaccels 2>/dev/null | grep -i "v4l2\|drm" || echo "  (nicht gefunden — Patches prüfen)"
echo ""
echo "Nächster Schritt: softhddevice-drm-gles gegen neues FFmpeg neu bauen:"
echo "  PKG_CONFIG_PATH=$INSTALL_PREFIX/lib/pkgconfig make -C <softhddevice-dir>"

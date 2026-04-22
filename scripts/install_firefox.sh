





set -euo pipefail

DISK="${1:-disk.img}"
TMPDIR="$(mktemp -d /tmp/krypx-firefox.XXXXXX)"
trap 'rm -rf "$TMPDIR"' EXIT

ALPINE_REPO="https://dl-cdn.alpinelinux.org/alpine/v3.19/main/x86_64"
ALPINE_COMM="https://dl-cdn.alpinelinux.org/alpine/v3.19/community/x86_64"





echo "[FIREFOX] Diretório temporário: $TMPDIR"
echo "[FIREFOX] Baixando APKINDEX para resolver versoes..."

curl -sfL "$ALPINE_REPO/APKINDEX.tar.gz"  -o "$TMPDIR/APKINDEX_main.tar.gz"  || true
curl -sfL "$ALPINE_COMM/APKINDEX.tar.gz"  -o "$TMPDIR/APKINDEX_comm.tar.gz"  || true


pkg_ver() {
    local name="$1"
    local idx_file="$2"
    tar -xzf "$idx_file" -O APKINDEX 2>/dev/null | \
        awk -v pkg="$name" '
            /^P:/ { cur = substr($0,3) }
            /^V:/ { if (cur == pkg) print substr($0,3) }
        ' | head -1
}


declare -a PACKAGES=(
    "firefox-esr:comm"
    "musl:main"
    "libstdc++:main"
    "gtk+3.0:main"
    "glib:main"
    "pango:main"
    "atk:main"
    "cairo:main"
    "gdk-pixbuf:main"
    "libx11:main"
    "libxext:main"
    "libxrender:main"
    "libxi:main"
    "libxinerama:main"
    "libxrandr:main"
    "libxdamage:main"
    "libxfixes:main"
    "libxcomposite:main"
    "libxcb:main"
    "xcb-util:main"
    "xcb-util-renderutil:main"
    "xcb-util-keysyms:main"
    "xcb-util-image:main"
    "xcb-util-wm:main"
    "dbus:main"
    "dbus-libs:main"
    "fontconfig:main"
    "freetype:main"
    "harfbuzz:main"
    "pixman:main"
    "libpng:main"
    "libjpeg-turbo:main"
    "zlib:main"
    "bzip2:main"
    "xz:main"
    "nss:main"
    "nspr:main"
    "libgcc:main"
    "libuuid:main"
    "util-linux-misc:main"
    "alsa-lib:main"
    "pulseaudio-libs:main"
    "wayland-libs-client:main"
    "libdrm:main"
    "mesa:main"
    "mesa-gl:main"
    "libepoxy:main"
    "at-spi2-core:main"
)


extract_apk() {
    local apkfile="$1"
    echo "  [EXTRACT] $(basename "$apkfile")"
    
    mkdir -p "$TMPDIR/rootfs"
    
    if tar -tzf "$apkfile" &>/dev/null; then
        tar -xzf "$apkfile" -C "$TMPDIR/rootfs" --exclude='.PKGINFO' \
            --exclude='.SIGN.*' --exclude='.INSTALL' --exclude='.post-*' \
            2>/dev/null || true
    fi
}

download_pkg() {
    local name="$1"
    local repo="$2"
    local repo_url
    if [ "$repo" = "main" ]; then
        repo_url="$ALPINE_REPO"
        idx="$TMPDIR/APKINDEX_main.tar.gz"
    else
        repo_url="$ALPINE_COMM"
        idx="$TMPDIR/APKINDEX_comm.tar.gz"
    fi

    local ver
    ver="$(pkg_ver "$name" "$idx" 2>/dev/null || echo "")"
    if [ -z "$ver" ]; then
        echo "  [WARN] versao de $name nao encontrada, tentando sem versao..."
        
        local url="$repo_url/$name.apk"
        curl -sfL "$url" -o "$TMPDIR/${name}.apk" 2>/dev/null && return
        echo "  [SKIP] $name nao disponivel"
        return
    fi

    local apkname="${name}-${ver}.apk"
    local url="$repo_url/$apkname"
    local outfile="$TMPDIR/$apkname"

    echo "  [GET] $apkname"
    if ! curl -sfL "$url" -o "$outfile" 2>/dev/null; then
        echo "  [WARN] falha ao baixar $apkname"
        return
    fi
    extract_apk "$outfile"
}

echo "[FIREFOX] Baixando pacotes..."
for pkg_repo in "${PACKAGES[@]}"; do
    name="${pkg_repo%%:*}"
    repo="${pkg_repo
    download_pkg "$name" "$repo" || true
done

echo "[FIREFOX] Organizando estrutura de diretorios..."
ROOTFS="$TMPDIR/rootfs"


mkdir -p "$ROOTFS/bin" "$ROOTFS/lib" "$ROOTFS/usr/bin" "$ROOTFS/usr/lib"
mkdir -p "$ROOTFS/etc/fonts" "$ROOTFS/tmp" "$ROOTFS/var/db/kpkg"
mkdir -p "$ROOTFS/packages" "$ROOTFS/home/root"


mkdir -p "$ROOTFS/tmp/.X11-unix"


cat > "$ROOTFS/etc/fonts/fonts.conf" << 'FONTS_EOF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>/usr/share/fonts</dir>
  <cachedir>/tmp/fontconfig</cachedir>
</fontconfig>
FONTS_EOF


cat > "$ROOTFS/usr/bin/firefox" << 'WRAPPER_EOF'

export DISPLAY=:0
export HOME=/home/root
export GTK_THEME=Adwaita:dark
export MOZ_DISABLE_CONTENT_SANDBOX=1
export MOZ_DISABLE_RDD_SANDBOX=1
export MOZ_DISABLE_SOCKET_PROCESS_SANDBOX=1
export MOZ_DISABLE_GPU_SANDBOX=1
export MOZ_DISABLE_GMP_SANDBOX=1
exec /usr/lib/firefox-esr/firefox-esr "$@"
WRAPPER_EOF
chmod +x "$ROOTFS/usr/bin/firefox"

echo "[FIREFOX] Copiando para disk.img com mtools..."


if [ ! -f "$DISK" ]; then
    echo "[DISK] Criando disk.img..."
    dd if=/dev/zero of="$DISK" bs=1M count=2048 status=none
    mkdosfs -F32 "$DISK"
fi


export MTOOLS_NO_VFAT=1

copy_dir_to_disk() {
    local src="$1"
    local dst="$2"

    find "$src" -type f | while read -r f; do
        local rel="${f
        local dstpath="$dst$rel"
        local dstdir
        dstdir="$(dirname "$dstpath")"

        
        mmd -i "$DISK" -D s "::$dstdir" 2>/dev/null || true

        
        mcopy -i "$DISK" -D s "$f" "::$dstpath" 2>/dev/null || true
    done
}

echo "[FIREFOX] Copiando arquivos para disk.img..."
copy_dir_to_disk "$ROOTFS" ""

echo ""
echo "=================================================="
echo " Firefox instalado no disk.img!"
echo " Execute: make run-firefox"
echo " No terminal do Krypx: firefox"
echo "=================================================="

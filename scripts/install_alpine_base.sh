


















set -euo pipefail

DISK="${1:-disk.img}"
TMPDIR="$(mktemp -d /tmp/krypx-alpine.XXXXXX)"
trap 'rm -rf "$TMPDIR"' EXIT


ALPINE_VERSION="3.19.1"
ALPINE_ARCH="x86_64"
ALPINE_URL="https://dl-cdn.alpinelinux.org/alpine/v3.19/releases/${ALPINE_ARCH}/alpine-minirootfs-${ALPINE_VERSION}-${ALPINE_ARCH}.tar.gz"
ALPINE_TAR="$TMPDIR/alpine-minirootfs.tar.gz"

echo "======================================================"
echo " Krypx Linux Subsystem — Alpine ${ALPINE_VERSION} x86_64"
echo "======================================================"
echo ""
echo "[1/4] Baixando Alpine minirootfs (~3 MB)..."
curl -fL --progress-bar "$ALPINE_URL" -o "$ALPINE_TAR"

echo "[2/4] Extraindo rootfs..."
mkdir -p "$TMPDIR/rootfs"
tar -xzf "$ALPINE_TAR" -C "$TMPDIR/rootfs" 2>/dev/null || true

ROOTFS="$TMPDIR/rootfs"

echo "[3/4] Configurando ambiente Alpine..."


mkdir -p "$ROOTFS/etc/apk"
cat > "$ROOTFS/etc/apk/repositories" << 'EOF'
https://dl-cdn.alpinelinux.org/alpine/v3.19/main
https://dl-cdn.alpinelinux.org/alpine/v3.19/community
EOF


cat > "$ROOTFS/etc/resolv.conf" << 'EOF'
nameserver 8.8.8.8
nameserver 1.1.1.1
EOF


cat > "$ROOTFS/etc/passwd" << 'EOF'
root:x:0:0:root:/root:/bin/sh
EOF


cat > "$ROOTFS/etc/group" << 'EOF'
root:x:0:root
EOF


mkdir -p "$ROOTFS/etc/profile.d"
cat > "$ROOTFS/etc/profile.d/krypx.sh" << 'EOF'
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export HOME=/root
export TERM=vt100
export DISPLAY=:0
export PS1='\u@krypx:\w\$ '
EOF


mkdir -p "$ROOTFS/root"
mkdir -p "$ROOTFS/tmp"
mkdir -p "$ROOTFS/var/run"
mkdir -p "$ROOTFS/proc"
mkdir -p "$ROOTFS/sys"
mkdir -p "$ROOTFS/dev"


cat > "$ROOTFS/init" << 'EOF'

exec /bin/sh
EOF
chmod +x "$ROOTFS/init"


cd "$ROOTFS/bin"
for applet in sh ash ls cat cp mv rm mkdir rmdir ln echo pwd grep sed awk \
              find wget curl tar gzip gunzip bzip2 xz ps kill sleep date \
              chmod chown chgrp stat du df free top uname hostname env; do
    if [ ! -e "$applet" ] && [ -f busybox ]; then
        ln -sf busybox "$applet" 2>/dev/null || true
    fi
done
cd - > /dev/null

echo "[4/4] Copiando rootfs para disk.img (via mtools)..."


if [ ! -f "$DISK" ]; then
    echo "  Criando disk.img (2 GB, FAT32)..."
    dd if=/dev/zero of="$DISK" bs=1M count=2048 status=none
    mkdosfs -F32 "$DISK"
fi

export MTOOLS_NO_VFAT=1
TOTAL=0

copy_tree() {
    local src="$1"
    local dst_prefix="$2"

    find "$src" | while read -r item; do
        local rel="${item
        [ -z "$rel" ] && continue
        local dst="::${dst_prefix}${rel}"

        if [ -d "$item" ]; then
            mmd -i "$DISK" -D s "$dst" 2>/dev/null || true
        elif [ -f "$item" ]; then
            local dir_part
            dir_part="$(dirname "${dst_prefix}${rel}")"
            mmd -i "$DISK" -D s "::${dir_part}" 2>/dev/null || true
            mcopy -i "$DISK" -D o "$item" "$dst" 2>/dev/null || true
            printf '.' >&2
        fi
    done
}

echo "  Copiando arquivos (pode levar alguns minutos)..."
copy_tree "$ROOTFS" ""
echo ""

echo ""
echo "======================================================"
echo " Alpine Linux instalado no disk.img!"
echo ""
echo " No terminal do Krypx:"
echo "   apk update              → atualizar lista de pacotes"
echo "   apk add firefox         → instalar Firefox"
echo "   apk add python3         → instalar Python"
echo "   apk add git             → instalar Git"
echo "   wget http://...         → baixar arquivos"
echo ""
echo " Execute: make run-compat (com disco) ou make run-firefox"
echo "======================================================"

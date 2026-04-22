













set -euo pipefail

SRC="${1:?Uso: $0 <diretorio> <saida.kpkg>}"
OUT="${2:?Uso: $0 <diretorio> <saida.kpkg>}"

if [ ! -d "$SRC" ]; then
    echo "ERRO: $SRC nao e um diretorio" >&2
    exit 1
fi


write_u32() {
    local v=$1
    printf "\\x$(printf '%02x' $((v & 0xFF)))"
    printf "\\x$(printf '%02x' $(((v >> 8) & 0xFF)))"
    printf "\\x$(printf '%02x' $(((v >> 16) & 0xFF)))"
    printf "\\x$(printf '%02x' $(((v >> 24) & 0xFF)))"
}


write_str() {
    local s="$1"
    local len="$2"
    local slen=${
    printf '%s' "$s"
    local pad=$((len - slen))
    for ((i=0; i<pad; i++)); do printf '\x00'; done
}


mapfile -t FILES < <(find "$SRC" -type f | sort)
N="${

echo "[MKPKG] $N arquivos em $SRC"

{
    
    printf 'KPKG'           
    write_u32 1              
    write_u32 "$N"           

    for f in "${FILES[@]}"; do
        rel="${f
        
        path="/$rel"
        size=$(stat -c%s "$f")
        mode=$(stat -c%a "$f")  
        mode_dec=$((8

        write_str "$path" 256
        write_u32 "$mode_dec"
        write_u32 "$size"
        cat "$f"

        echo "  + $path ($size bytes)" >&2
    done
} > "$OUT"

echo "[MKPKG] Criado: $OUT ($(stat -c%s "$OUT") bytes)"

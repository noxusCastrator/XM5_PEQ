#!/bin/bash
# Build the U5 MIPS payload: compile the vendored components + U5 glue, link at a fixed base with
# payload.ld, verify there are no undefined symbols and no leftover relocations, and emit payload.bin,
# the map and the disassembly. Read-only w.r.t. firmware. Run under WSL: bash scripts/build.sh
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
TASKS=$(cd "$HERE/.." && pwd)
TR="$TASKS/T1-toolchain/toolchain/root"
export LD_LIBRARY_PATH="$TR/usr/lib/x86_64-linux-gnu"
GCC="$TR/usr/bin/mipsel-linux-gnu-gcc-12"
OBJCOPY="$TR/usr/bin/mipsel-linux-gnu-objcopy"
OBJDUMP="$TR/usr/bin/mipsel-linux-gnu-objdump"
READELF="$TR/usr/bin/mipsel-linux-gnu-readelf"
NM="$TR/usr/bin/mipsel-linux-gnu-nm"

V="$HERE/vendor"; S="$HERE/src"; B="$HERE/build"; OUT="$HERE/payload"
mkdir -p "$B" "$OUT"

TGT=(-march=mips32r2 -mabi=32 -EL -mhard-float -mfpxx -mno-odd-spreg -mnan=2008)
CFLAGS=(-Os "${TGT[@]}" -mno-abicalls -fno-pic -fno-pie -G0 -ffreestanding -fno-builtin
  -fno-stack-protector -fno-common -D_FORTIFY_SOURCE=0
  -I"$S" -I"$S/compat" -I"$V"
  '-DPEQ_PLATFORM_HEADER="xm5_peq_platform.h"'
  -Wall -Wextra -Wno-unused-parameter)

echo "== compile"
OBJS=()
for f in peq controller view native_files peq_adapter peq_awtk_view; do
  "$GCC" "${CFLAGS[@]}" -c "$V/$f.c" -o "$B/$f.o"; OBJS+=("$B/$f.o")
done
for f in runtime idlepool binder layer_owner; do
  "$GCC" "${CFLAGS[@]}" -c "$S/$f.c" -o "$B/$f.o"; OBJS+=("$B/$f.o")
done
for a in trampoline thunks; do
  "$GCC" "${CFLAGS[@]}" -c "$S/$a.S" -o "$B/$a.o"; OBJS+=("$B/$a.o")
done

echo "== link (fixed base, no libc, non-PIC)"
"$GCC" "${TGT[@]}" -nostdlib -nostartfiles -mno-abicalls -fno-pic -no-pie \
  -Wl,--build-id=none -Wl,-T,"$S/payload.ld" -Wl,-Map,"$B/payload.map" \
  "${OBJS[@]}" -o "$B/payload.elf"

echo "== verify: undefined symbols (expect none)"
UND=$("$NM" -u "$B/payload.elf" || true)
if [ -n "$UND" ]; then echo "FAIL undefined symbols:"; echo "$UND"; exit 2; fi
echo "  none"

echo "== verify: relocations (expect none)"
REL=$("$READELF" -r "$B/payload.elf" | grep -vE 'There are no relocations|^$' || true)
if [ -n "$REL" ]; then echo "FAIL leftover relocations:"; echo "$REL"; exit 3; fi
echo "  none"

echo "== verify: entry address == 0x00820000"
ENT=$("$NM" "$B/payload.elf" | awk '$3=="u5_payload_entry"{print $1}')
echo "  u5_payload_entry = 0x$ENT"
[ "$ENT" = "00820000" ] || { echo "FAIL entry not at payload base"; exit 4; }

echo "== verify: ELF flags (nan2008, fpxx)"
"$READELF" -h "$B/payload.elf" | grep -E 'Flags'
"$READELF" -A "$B/payload.elf" | grep -E 'ISA:|FP ABI' || true

echo "== emit payload.bin + map + disassembly"
"$OBJCOPY" -O binary "$B/payload.elf" "$OUT/payload.bin"
"$OBJDUMP" -d "$B/payload.elf" > "$OUT/payload.dis"
cp "$B/payload.map" "$OUT/payload.map"

MEMEND=$("$NM" "$B/payload.elf" | awk '$3=="__u5_mem_end"{print $1}')
FILEEND=$("$NM" "$B/payload.elf" | awk '$3=="__u5_file_end"{print $1}')
BINSZ=$(wc -c < "$OUT/payload.bin")
echo "== sizes"
echo "  base      = 0x00820000"
echo "  file_end  = 0x$FILEEND"
echo "  mem_end   = 0x$MEMEND"
echo "  payload.bin bytes = $BINSZ"
{
  echo "base=0x00820000"
  echo "filesz=$BINSZ"
  echo "memsz=$((0x$MEMEND - 0x00820000))"
} > "$OUT/sizes.txt"
echo "BUILD OK"

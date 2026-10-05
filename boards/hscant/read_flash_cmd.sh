#!/usr/bin/env bash
#
# read_flash_cmd.sh - Read the HPM5300 1MB (XPI0 NOR) flash using ONLY the
#                     stock command-line tool `hpm_manufacturing_cmd`.
#
# IMPORTANT: hpm_manufacturing_cmd has NO baud-rate option. Its serial port is
# hard-wired to 115200 bps (there is no --baud, and "-p <port>:<baud>" is
# treated as a file name). Higher rates such as 1M/1152000 can only be reached
# with the ROM Client SDK - see tools/read_flash.sh.
#
# Flow (all in one tool invocation, so the XPI0 config stays valid):
#   1. write-memory 0x0   0x200 [[0xfcf90002,0x6,0x1000,0x0]]
#        -> write the HPM5300 NOR config option into RAM at 0x200
#   2. config-memory 0x10000 0x200
#        -> configure XPI0 using that RAM config
#   3. read-memory  0x10000 <address> <size> <file>
#        -> dump the requested range
#
# Usage:
#   ./read_flash_cmd.sh [port] [output] [address] [size]
#
#   port    serial port of the ROM ISP, default: /dev/ttyACM0
#   output  output file,                    default: ./hpm5300_flash_1M.bin
#   address flash start address,            default: 0x80000000
#   size    number of bytes to read,        default: 0x100000 (1 MB)
#
# Environment overrides:
#   HPM_MFG_DIR   manufacturing tool install dir,
#                 default: /opt/toolchain/HPMicro_Manufacturing_Tool_v0.7.0
#   HPM_TIMEOUT   per-command timeout in ms, default: 60000
#
# Note: at 115200 bps a full 1 MB read takes several minutes. Read a smaller
# size first (e.g. 0x10000) to sanity-check the connection.
#
set -euo pipefail

PORT="${1:-/dev/ttyACM0}"
OUT="${2:-$PWD/hpm5300_flash_1M.bin}"
ADDR="${3:-0x80000000}"
SIZE="${4:-0x100000}"
MFG_DIR="${HPM_MFG_DIR:-/opt/toolchain/HPMicro_Manufacturing_Tool_v0.7.0}"
TIMEOUT="${HPM_TIMEOUT:-60000}"
CMD="$MFG_DIR/hpm_manufacturing_cmd"

# hpm_manufacturing_cmd is fixed at 115200 bps.
BAUD=115200

if [ ! -x "$CMD" ]; then
    echo "[!] hpm_manufacturing_cmd not found: $CMD" >&2
    echo "    set HPM_MFG_DIR to your HPMicro_Manufacturing_Tool install directory." >&2
    exit 1
fi

echo "[*] tool   : $CMD"
echo "[*] baud   : $BAUD  (fixed by the command-line tool)"
echo "[*] port   : $PORT"
echo "[*] range  : $ADDR + $SIZE"
echo "[*] output : $OUT"

# Pre-check: make sure the ROM answers at 115200. A previous 1M/SDK run may
# have left the ROM at another baud rate until the board is power-cycled.
# NOTE: capture the output first - piping straight into `grep -q` under
# `set -o pipefail` kills the tool with SIGPIPE and fails the pipeline even
# when the pattern matches.
pre="$("$CMD" -p "$PORT" -t 4000 -r "query-rte 1" 2>&1 || true)"
if ! grep -q "Baud Rate" <<<"$pre"; then
    echo "[!] no response on $PORT at $BAUD bps." >&2
    echo "    If a previous run switched the ROM baud (e.g. to 1000000), power-cycle" >&2
    echo "    the board to return the ROM to 115200, or use tools/read_flash.sh." >&2
    exit 1
fi

"$CMD" -p "$PORT" -t "$TIMEOUT" \
    -r "write-memory 0x0 0x200 [[0xfcf90002,0x6,0x1000,0x0]]" \
    -r "config-memory 0x10000 0x200" \
    -r "read-memory 0x10000 $ADDR $SIZE $OUT"

echo "[*] done: $OUT ($(stat -c%s "$OUT" 2>/dev/null || echo '?') bytes)"

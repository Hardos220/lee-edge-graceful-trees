#!/usr/bin/env bash
# Builds bin/gentreeg (plain nauty gentreeg), bin/egrun (gentreeg +
# labeling search), bin/egverify and bin/egcheck (independent checker,
# fed by gentreeg text or by gentreeg in-process), bin/egsolve and bin/zrcount.
# Downloads nauty 2.9.3 into third_party/ on the first run.
# Usage: bash build.sh            (cc, or a portable clang in ../_tools if present)
#        CC=gcc bash build.sh     (any C11 compiler)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
NAUTY="$ROOT/third_party/nauty2_9_3"
DEFAULT_CC="$ROOT/../_tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang"
[ -x "$DEFAULT_CC" ] || [ -x "$DEFAULT_CC.exe" ] || DEFAULT_CC=cc
CC="${CC:-$DEFAULT_CC}"
[ -d "$NAUTY" ] || bash "$ROOT/third_party/fetch_nauty.sh"
# x86-64-v3 (AVX2/BMI2, any CPU since ~2015) keeps the binaries portable to other PCs.
CFLAGS="${CFLAGS:--O3 -march=x86-64-v3}"
EXE=""
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;; esac

if [ ! -f "$NAUTY/nauty.h" ]; then
  (cd "$NAUTY" && CC="$CC" ./configure >/dev/null)
fi

NAUTYSRC="$NAUTY/gtools.c $NAUTY/nauty.c $NAUTY/nautil.c $NAUTY/naugraph.c $NAUTY/schreier.c $NAUTY/naurng.c"
mkdir -p "$ROOT/bin"

"$CC" $CFLAGS -w -I"$NAUTY" -o "$ROOT/bin/gentreeg$EXE" "$NAUTY/gentreeg.c" $NAUTYSRC
"$CC" $CFLAGS -w -I"$NAUTY" -I"$ROOT/src" -DOUTPROC=eg_outproc -DGENTREEG_MAIN=gentreeg_main \
      -o "$ROOT/bin/egrun$EXE" "$NAUTY/gentreeg.c" $NAUTYSRC "$ROOT/src/egrun.c" "$ROOT/src/egsearch.c"
"$CC" $CFLAGS -Wall -Wextra -o "$ROOT/bin/egverify$EXE" "$ROOT/src/egverify.c"
# egcheck: the same independent checker, fed by gentreeg in-process (no text pipe)
"$CC" $CFLAGS -w -I"$NAUTY" -DWITH_GENTREEG -DOUTPROC=check_outproc -DGENTREEG_MAIN=gentreeg_main \
      -o "$ROOT/bin/egcheck$EXE" "$NAUTY/gentreeg.c" $NAUTYSRC "$ROOT/src/egverify.c"
# egsolve: re-solves given trees with other seeds; zrcount: exhaustive zero-vertex counts (small n)
"$CC" $CFLAGS -Wall -I"$ROOT/src" -o "$ROOT/bin/egsolve$EXE" "$ROOT/src/egsolve.c" "$ROOT/src/egsearch.c"
"$CC" $CFLAGS -Wall -Wextra -o "$ROOT/bin/zrcount$EXE" "$ROOT/src/zrcount.c"

# warnings check for our own search code (nauty sources are compiled with -w)
"$CC" $CFLAGS -Wall -Wextra -fsyntax-only -I"$ROOT/src" "$ROOT/src/egsearch.c" "$ROOT/src/egrun.c"
"$CC" $CFLAGS -Wall -Wextra -fsyntax-only -DWITH_GENTREEG "$ROOT/src/egverify.c"
echo "built: $(ls "$ROOT/bin" | tr '\n' ' ')"

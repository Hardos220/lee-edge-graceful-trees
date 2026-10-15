#!/usr/bin/env bash
# Downloads nauty 2.9.3 (Brendan McKay and Adolfo Piperno, Apache License 2.0)
# into third_party/nauty2_9_3 and checks the SHA-256 of the archive.
# The programs use gentreeg.c and the nauty library sources unchanged.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
TGZ="$DIR/nauty2_9_3.tar.gz"
SHA=9fc4edae04f88a0f5883985be3b39cf7f898fd6cc96e96b9ee25452743cc1b5b

if [ ! -f "$TGZ" ]; then
  curl -fL -o "$TGZ" https://users.cecs.anu.edu.au/~bdm/nauty/nauty2_9_3.tar.gz \
    || curl -fL -o "$TGZ" https://pallini.di.uniroma1.it/nauty2_9_3.tar.gz
fi
echo "$SHA  $TGZ" | sha256sum -c -
tar -xzf "$TGZ" -C "$DIR"
echo "nauty 2.9.3 in $DIR/nauty2_9_3"

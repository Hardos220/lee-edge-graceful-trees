#!/usr/bin/env bash
# Downloads nauty 2.9.3 (Brendan McKay and Adolfo Piperno, Apache License 2.0)
# into third_party/nauty2_9_3 and checks the SHA-256 of the archive.
# The programs use gentreeg.c and the nauty library sources unchanged.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
TGZ="$DIR/nauty2_9_3.tar.gz"
SHA=9fc4edae04f88a0f5883985be3b39cf7f898fd6cc96e96b9ee25452743cc1b5b

if [ ! -f "$TGZ" ]; then
  # the official site first, then the mirror; give up on a site that stalls
  for url in https://users.cecs.anu.edu.au/~bdm/nauty/nauty2_9_3.tar.gz \
             https://pallini.di.uniroma1.it/nauty2_9_3.tar.gz; do
    if curl -fL --connect-timeout 30 --speed-limit 20000 --speed-time 30 -o "$TGZ.part" "$url"; then
      mv "$TGZ.part" "$TGZ"
      break
    fi
    rm -f "$TGZ.part"
  done
  [ -f "$TGZ" ] || { echo "could not download nauty 2.9.3" >&2; exit 1; }
fi
echo "$SHA  $TGZ" | sha256sum -c -
tar -xzf "$TGZ" -C "$DIR"
echo "nauty 2.9.3 in $DIR/nauty2_9_3"

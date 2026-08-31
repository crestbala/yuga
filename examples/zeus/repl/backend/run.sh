#!/bin/sh
# Native gRPC-Web API on 127.0.0.1:8081
set -e
HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
d=$HERE
REPO=
while [ "$d" != / ]; do
  if [ -f "$d/Makefile" ] && [ -d "$d/packages/compiler/src" ]; then
    REPO=$d
    break
  fi
  d=$(CDPATH= cd -- "$d/.." && pwd)
done
if [ -z "$REPO" ]; then
  echo "run.sh: could not find the yuga repo (Makefile + packages/compiler/src/)" >&2
  exit 1
fi
if [ ! -x "$REPO/bin/yugac" ]; then
  echo "run.sh: building yugac"
  make -C "$REPO" -j4
fi
mkdir -p "$HERE/build" "$HERE/tmp"
echo "backend: compiling server.yuga"
"$REPO/bin/yugac" "$HERE/server.yuga" -o "$HERE/build/server"
export YUGA_YUGAC="$REPO/bin/yugac"
export YUGA_REPL_TMP="$HERE/tmp"
echo "backend: http://127.0.0.1:8081  (Playground.Run)"
exec "$HERE/build/server"

#!/bin/sh
# Start backend (:8082) then frontend (:5175). Ctrl-C stops both.
#
#   ./run.sh            web UI
#   ./run.sh macos      Cocoa (starts backend if needed)
#   ./run.sh backend    API only
#   ./run.sh frontend   Vite only (needs backend)
set -e
HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)

variant=${1:-web}
case $variant in
  macos) exec "$HERE/macos/run.sh" ;;
  backend) exec "$HERE/backend/run.sh" ;;
  frontend) exec "$HERE/frontend/run.sh" ;;
  web|"") ;;
  *)
    echo "www/run.sh: unknown variant '$variant' (web backend frontend macos)" >&2
    exit 1
    ;;
esac

BACK_PID=

cleanup() {
  if [ -n "$BACK_PID" ]; then
    kill "$BACK_PID" 2>/dev/null || true
    wait "$BACK_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

"$HERE/backend/run.sh" &
BACK_PID=$!

n=0
while [ "$n" -lt 80 ]; do
  if nc -z 127.0.0.1 8082 >/dev/null 2>&1; then
    break
  fi
  if ! kill -0 "$BACK_PID" 2>/dev/null; then
    echo "run.sh: backend exited before it listened on :8082" >&2
    exit 1
  fi
  n=$((n + 1))
  sleep 0.1
done
if [ "$n" -ge 80 ]; then
  echo "run.sh: timed out waiting for backend on :8082" >&2
  exit 1
fi

echo "docs: UI http://127.0.0.1:5175   API http://127.0.0.1:8082"
exec "$HERE/frontend/run.sh"

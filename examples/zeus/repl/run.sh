#!/bin/sh
# Start backend (:8081) then frontend (:5174). Ctrl-C stops both.
set -e
HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
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
  if nc -z 127.0.0.1 8081 >/dev/null 2>&1; then
    break
  fi
  if ! kill -0 "$BACK_PID" 2>/dev/null; then
    echo "run.sh: backend exited before it listened on :8081" >&2
    exit 1
  fi
  n=$((n + 1))
  sleep 0.1
done
if [ "$n" -ge 80 ]; then
  echo "run.sh: timed out waiting for backend on :8081" >&2
  exit 1
fi

echo "example: UI http://127.0.0.1:5174   API http://127.0.0.1:8081"
exec "$HERE/frontend/run.sh"

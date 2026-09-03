#!/bin/sh
# WebSocket demo: native push server (:8080) + wasm frontend (:5173).
# Ctrl-C stops both. Open http://127.0.0.1:5173 and watch the ticks land.
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
  if nc -z 127.0.0.1 8080 >/dev/null 2>&1; then
    break
  fi
  if ! kill -0 "$BACK_PID" 2>/dev/null; then
    echo "run.sh: backend exited before it listened on :8080" >&2
    exit 1
  fi
  n=$((n + 1))
  sleep 0.1
done
if [ "$n" -ge 80 ]; then
  echo "run.sh: backend did not listen on :8080 in time" >&2
  exit 1
fi

echo "frontend: http://127.0.0.1:5173  (ws proxied to :8080)"
cd "$HERE/frontend"
if [ ! -d node_modules ]; then
  echo "frontend: npm install"
  npm install --no-audit --no-fund
fi
exec npx vite

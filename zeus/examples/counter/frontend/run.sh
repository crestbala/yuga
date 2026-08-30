#!/bin/sh
# Vite + wasm UI on http://127.0.0.1:5173  (proxies POST /Counter/* to :8080)
set -e
HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
cd "$HERE"
if [ ! -d node_modules ]; then
  echo "frontend: npm install"
  npm install
fi
echo "frontend: http://127.0.0.1:5173  (needs backend on :8080)"
exec npm run dev

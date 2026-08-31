#!/bin/sh
# Vite + wasm UI on http://127.0.0.1:5174  (proxies POST /Playground/* to :8081)
set -e
HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
cd "$HERE"
if [ ! -d node_modules ]; then
  echo "frontend: npm install"
  npm install
fi
echo "frontend: http://127.0.0.1:5174  (needs backend on :8081; edit Yuga, Run is gRPC)"
exec npm run dev

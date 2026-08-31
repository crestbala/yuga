#!/bin/sh
# Vite + wasm UI on http://127.0.0.1:5175  (proxies POST /Docs/* to :8082)
set -e
HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
cd "$HERE"
if [ ! -d node_modules ]; then
  echo "frontend: npm install"
  npm install
fi
echo "frontend: http://127.0.0.1:5175  (needs backend on :8082)"
exec npm run dev

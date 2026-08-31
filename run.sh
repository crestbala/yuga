#!/bin/sh
# Run any example in this repo.
#
#   ./run.sh                      list what is runnable
#   ./run.sh fizzbuzz             a language example (examples/language/)
#   ./run.sh dashboard            a Zeus app (examples/zeus/), Cocoa desktop
#   ./run.sh dashboard ios        ...on another host: native | wasm32 | ios | android
#   ./run.sh counter              the full-stack example (backend :8080 + web UI :5173)
#   ./run.sh counter macos        ...as a native/ios/android client
#
# Names are the file/directory stem. `counter` is both a language example and
# the full-stack Zeus example, so it is disambiguated as language/counter and
# zeus/counter; a bare name that matches exactly one example works directly.
#
# GUI examples open a real window and servers block until Ctrl-C. To render one
# frame and exit instead (what `make test` does), set ZEUS_HEADLESS=1 for Zeus
# apps or MAYA_HEADLESS=1 for the maya 3D examples.
set -e

HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
LANGDIR=$HERE/examples/language
ZEUSDIR=$HERE/examples/zeus
YUGAC=$HERE/bin/yugac

die() { echo "run.sh: $*" >&2; exit 1; }

list() {
  echo "language examples  (./run.sh <name>)"
  for f in "$LANGDIR"/*.yuga; do
    [ -e "$f" ] || continue
    printf '  %s\n' "$(basename "$f" .yuga)"
  done
  echo
  echo "zeus apps          (./run.sh <name> [native|wasm32|ios|android])"
  for d in "$ZEUSDIR"/*/; do
    d=${d%/}
    name=$(basename "$d")
    [ -f "$d/$name.yuga" ] || continue
    printf '  %s\n' "$name"
  done
  echo
  echo "full stack         (./run.sh counter [web|backend|macos|ios|android])"
  [ -d "$ZEUSDIR/counter" ] && echo "  counter"
  echo
  echo "playground         (./run.sh repl [web|backend|frontend|macos])"
  echo "  repl               edit Yuga in the browser; Run is Playground.Run over gRPC-Web"
  echo
  echo "docs               (./run.sh www [web|backend|frontend|macos])"
  echo "  www                Zeus docs; UI :5175, Docs.Page :8082"
}

ensure_yugac() {
  if [ ! -x "$YUGAC" ]; then
    echo "run.sh: building the compiler"
    make -C "$HERE" -j4
  fi
}

# The full-stack example ships its own per-host launchers.
run_counter_stack() {
  variant=${1:-web}
  case $variant in
    web|"")   exec "$ZEUSDIR/counter/run.sh" ;;
    backend)  exec "$ZEUSDIR/counter/backend/run.sh" ;;
    frontend) exec "$ZEUSDIR/counter/frontend/run.sh" ;;
    macos)    exec "$ZEUSDIR/counter/macos/run.sh" ;;
    ios)      exec "$ZEUSDIR/counter/ios/run.sh" ;;
    android)  exec "$ZEUSDIR/counter/android/run.sh" ;;
    *) die "unknown counter variant '$variant' (web backend frontend macos ios android)" ;;
  esac
}

run_repl_stack() {
  variant=${1:-web}
  case $variant in
    web|"")   exec "$ZEUSDIR/repl/run.sh" ;;
    backend)  exec "$ZEUSDIR/repl/backend/run.sh" ;;
    frontend) exec "$ZEUSDIR/repl/frontend/run.sh" ;;
    macos)    exec "$ZEUSDIR/repl/macos/run.sh" ;;
    *) die "unknown repl variant '$variant' (web backend frontend macos)" ;;
  esac
}

run_zeus_app() {
  name=$1
  target=${2:-native}
  ensure_yugac
  case $target in
    native) exec "$YUGAC" --run "$ZEUSDIR/$name/$name.yuga" ;;
    wasm32|wasm|ios|android)
      exec "$YUGAC" "--target=$target" --run "$ZEUSDIR/$name/$name.yuga" ;;
    *) die "unknown target '$target' (native wasm32 ios android)" ;;
  esac
}

run_language() {
  name=$1
  ensure_yugac
  # oob.yuga exists to prove the bounds check traps, so a nonzero exit from the
  # program is the expected outcome. A compile error is still a real failure,
  # so build and run as separate steps rather than using --run.
  if [ "$name" = oob ]; then
    out=$LANGDIR/build/oob
    mkdir -p "$LANGDIR/build"
    "$YUGAC" "$LANGDIR/oob.yuga" -o "$out" || die "oob.yuga failed to compile"
    echo "run.sh: oob.yuga is expected to trap on an out-of-bounds index"
    if "$out"; then
      die "oob.yuga did not trap"
    fi
    echo "run.sh: trapped as expected"
    exit 0
  fi
  exec "$YUGAC" --run "$LANGDIR/$name.yuga"
}

[ $# -eq 0 ] && { list; exit 0; }

what=$1
shift

case $what in
  list|-l|--list|-h|--help) list; exit 0 ;;
  www|docs) exec "$HERE/www/run.sh" "$@" ;;
  language/*) run_language "${what#language/}" ;;
  repl|zeus/repl) run_repl_stack "$@" ;;
  zeus/counter)  run_counter_stack "$@" ;;
  zeus/*)     run_zeus_app "${what#zeus/}" "$@" ;;
esac

# Bare name: resolve it, and refuse if it is ambiguous.
is_lang=0; is_zeus=0
[ -f "$LANGDIR/$what.yuga" ] && is_lang=1
{ [ -f "$ZEUSDIR/$what/$what.yuga" ] || [ -d "$ZEUSDIR/$what" ]; } && is_zeus=1

if [ "$is_lang" = 1 ] && [ "$is_zeus" = 1 ]; then
  die "'$what' is ambiguous; use language/$what or zeus/$what"
fi

if [ "$is_lang" = 1 ]; then
  run_language "$what"
elif [ "$is_zeus" = 1 ]; then
  if [ "$what" = counter ]; then
    run_counter_stack "$@"
  elif [ "$what" = repl ]; then
    run_repl_stack "$@"
  else
    run_zeus_app "$what" "$@"
  fi
else
  echo "run.sh: no example named '$what'" >&2
  echo >&2
  list >&2
  exit 1
fi

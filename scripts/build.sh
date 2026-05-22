#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="schwung-ambiotica-builder"

if [ -z "$CROSS_PREFIX" ] && [ ! -f "/.dockerenv" ]; then
    echo "=== Building Ambiotica Module (via Docker) ==="
    if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
        docker build -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile" "$REPO_ROOT"
    fi
    docker run --rm \
        -v "$REPO_ROOT:/build" \
        -u "$(id -u):$(id -g)" \
        -w /build \
        "$IMAGE_NAME" \
        ./scripts/build.sh
    exit 0
fi

CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"
cd "$REPO_ROOT"

mkdir -p build dist/ambiotica

${CROSS_PREFIX}gcc -O3 -shared -fPIC \
    -march=armv8-a -mtune=cortex-a72 \
    -ffast-math -fomit-frame-pointer -fno-stack-protector \
    -DNDEBUG \
    -Isrc/dsp \
    src/dsp/plugin.c \
    -o build/ambiotica.so \
    -lm

cp src/module.json dist/ambiotica/module.json
[ -f src/help.json ] && cp src/help.json dist/ambiotica/help.json
cp build/ambiotica.so dist/ambiotica/ambiotica.so
[ -f LICENSE ] && cp LICENSE dist/ambiotica/LICENSE
chmod +x dist/ambiotica/ambiotica.so

cd dist
tar -czvf ambiotica-module.tar.gz ambiotica/
echo "OK: dist/ambiotica-module.tar.gz"

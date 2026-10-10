#!/usr/bin/env bash
set -euo pipefail

IMAGE_NAME="${DOCKER_IMAGE:-ghcr.io/nixxoq/telegacy-build-env:latest}"
TARGET="${1:-both}"

echo "=== Building Telegacy via Docker ($IMAGE_NAME) ==="

docker run --rm \
    -v "$(pwd):/workspace" \
    -w /workspace \
    "$IMAGE_NAME" \
    ./scripts/build.sh "$TARGET"


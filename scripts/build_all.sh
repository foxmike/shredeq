#!/usr/bin/env bash
set -euo pipefail

# Run from the repository root: ./scripts/build_all.sh

echo "==> Configuring ShredEQ (preset: linux-relwithdebinfo)..."
cmake --preset linux-relwithdebinfo

echo "==> Building ShredEQ (RelWithDebInfo)..."
cmake --build --preset linux-relwithdebinfo

echo "==> Build complete."

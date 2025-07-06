#!/bin/bash
# Script to vendor AGC source code from upstream

set -e

echo "Vendoring AGC source code..."

# Create a temporary directory
TEMP_DIR=$(mktemp -d)
AGC_DIR="agc"

# Clean up on exit
trap "rm -rf $TEMP_DIR" EXIT

# Clone AGC repository with submodules
echo "Cloning AGC repository with submodules..."
git clone --recurse-submodules --depth 1 --branch v3.2.1 https://github.com/refresh-bio/agc.git "$TEMP_DIR/agc"

# Remove existing AGC directory if it exists
if [ -d "$AGC_DIR" ]; then
    echo "Removing existing AGC directory..."
    rm -rf "$AGC_DIR"
fi

# Copy AGC source to vendor directory
echo "Copying AGC source..."
cp -r "$TEMP_DIR/agc" "$AGC_DIR"

# Remove all .git directories to make it plain source
echo "Removing .git directories..."
find "$AGC_DIR" -name ".git" -type d -exec rm -rf {} + 2>/dev/null || true
find "$AGC_DIR" -name ".gitignore" -type f -delete 2>/dev/null || true
find "$AGC_DIR" -name ".gitmodules" -type f -delete 2>/dev/null || true

echo "AGC has been vendored successfully!"
echo "The library will be built automatically when you run 'cargo build'."
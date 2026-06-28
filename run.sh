#!/bin/bash
# Automated Build and Run script

# Ensure we are in the script's directory
cd "$(dirname "$0")"

# Create src directory if it doesn't exist
mkdir -p src

# Create and enter build directory
mkdir -p build
cd build

echo "[1/3] Configuring project with CMake..."
cmake ..

echo "[2/3] Building application..."
make -j$(nproc)

if [ $? -eq 0 ]; then
    echo "[3/3] Launching trading_app..."
    # Using stdbuf to ensure logs are written to files/console immediately
    # without waiting for a buffer to fill.
    stdbuf -oL ./trading_app
else
    echo "[ERROR] Build failed. Check the logs above."
fi
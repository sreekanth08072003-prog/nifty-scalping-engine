#!/bin/bash
# build.sh - Automated build script for Angel One Trading Engine

echo "[BUILD] Compiling Trading Engine..."

if [ ! -f "src/main.cpp" ]; then
    echo "[ERROR] src/main.cpp not found. Ensure you are running this from the project root."
    exit 1
fi

# Remove old binary to ensure we don't run stale code
rm -f build/trading_engine

mkdir -p build

g++ -std=c++17 -O3 \
    -DBOOST_SYSTEM_NO_LIB \
    src/main.cpp \
    -Iinclude \
    -I/usr/include \
    -L/usr/lib/x86_64-linux-gnu \
    -lcurl -lssl -lcrypto -lpthread -lboost_thread -lboost_atomic -lboost_chrono \
    -o build/trading_engine

if [ $? -eq 0 ]; then
    echo "[SUCCESS] Build complete: ./build/trading_engine"
else
    echo "[ERROR] Compilation failed"
    exit 1
fi
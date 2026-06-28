#!/bin/bash
# Install all system dependencies required for the trading app
echo "[INFO] Installing system dependencies..."
sudo apt-get update
sudo apt-get install -y build-essential cmake libboost-all-dev libcurl4-openssl-dev libssl-dev liboath-dev nlohmann-json3-dev
echo "[SUCCESS] Dependencies installed."
chmod +x run.sh 2>/dev/null
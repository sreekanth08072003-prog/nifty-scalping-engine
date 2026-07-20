#!/bin/bash

# Ensure we are in the correct directory
cd "$(dirname "$0")"
if [ ! -d "src" ]; then
    echo "[ERROR] Please run this script from inside the auto-trading-app directory."
    exit 1
fi

# Load credentials from available env files
if [ -f trading.env ]; then
    export $(grep -v '^#' trading.env | xargs)
    echo "[SYSTEM] Credentials loaded from trading.env"
elif [ -f .env ]; then
    export $(grep -v '^#' .env | xargs)
    echo "[SYSTEM] Credentials loaded from .env"
else
    echo "[ERROR] No environment file (.env or trading.env) found!"
    exit 1
fi

# Automatically detect IP addresses
export LOCAL_IP=$(hostname -I | awk '{print $1}')
export PUBLIC_IP=$(curl -s -4 https://api.ipify.org)

# Set default Master Contract URL if not already set in .env
export ANGEL_MASTER_URL=${ANGEL_MASTER_URL:-"https://margincalculator.angelbroking.com/OpenApi_0.1/static/allExchangeData.json"}
echo "[SYSTEM] Using Master Contract URL: $ANGEL_MASTER_URL"

# Execute the application
if [ ! -f "./build/trading_engine" ]; then
    echo "[INFO] Binary not found. Compiling now..."
    chmod +x build.sh
    ./build.sh || exit 1
fi

./build/trading_engine
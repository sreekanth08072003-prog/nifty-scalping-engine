# Angel One Nifty Scalping Engine

A high-frequency scalping engine designed for the Angel One SmartAPI. It monitors Nifty 50 Spot and executes OTM/ATM option trades based on Order Flow Imbalance (OFI).

## Features
- **Real-time Analysis**: Uses WebSockets for live ticks.
- **Statistical Edge**: Z-Score based signal detection.
- **Risk Management**: Hard stops for daily PnL and position timeouts.
- **Secure**: Credentials handled via environment variables.

## Prerequisites
- C++17 Compiler
- Boost (Beast/Asio)
- libcurl
- OpenSSL
- [nlohmann/json](https://github.com/nlohmann/json)
- [liboath](https://www.nongnu.org/oath-toolkit/) (for TOTP)

## Environment Variables
Create a `trading.env` file (ignored by git) and add:
```bash
export ANGEL_CLIENT_ID="YOUR_ID"
export ANGEL_PASSWORD="YOUR_PASSWORD"
export ANGEL_API_KEY="YOUR_API_KEY"
export ANGEL_TOTP_SEED="YOUR_BASE32_SEED"
```

## Build Instructions
```bash
mkdir build && cd build
cmake ..
make
```

## Running the App
```bash
source trading.env
./AutoTradingApp
```
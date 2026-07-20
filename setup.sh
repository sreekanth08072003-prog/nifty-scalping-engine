#!/bin/bash
# Install C++ build essentials and libraries
sudo apt-get update
sudo apt-get install -y build-essential cmake libboost-all-dev libssl-dev libcurl4-openssl-dev nlohmann-json3-dev git rsync
echo "Setup complete. Dependencies installed."
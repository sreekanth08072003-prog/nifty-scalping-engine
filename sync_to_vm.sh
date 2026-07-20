#!/bin/bash
# sync_to_vm.sh - Efficiently syncs local Cloud Shell changes to the e2 VM

# --- CONFIGURATION ---
VM_NAME="instance-20260629-195954"
ZONE="asia-south1-c"
PROJECT_ID=$(gcloud config get-value project 2>/dev/null || echo $GOOGLE_CLOUD_PROJECT)
REMOTE_PATH="~/auto-trading-app"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 0. Ensure rsync is installed (Fix for "command not found")
if ! command -v rsync &> /dev/null; then
    echo "[INFO] rsync not found. Installing..."
    sudo apt-get update && sudo apt-get install -y rsync
fi

# 1. Ensure gcloud SSH config is set up
# This creates SSH aliases like 'vm-name.zone.project'
echo "[INFO] Updating gcloud SSH configuration..."
gcloud compute config-ssh --quiet

# 1.5 Ensure remote directory exists
echo "[INFO] Ensuring remote directory $REMOTE_PATH exists on $VM_NAME..."
gcloud compute ssh "$VM_NAME" --zone="$ZONE" --command="mkdir -p $REMOTE_PATH" --quiet

# 2. Sync using rsync
# It excludes build artifacts and git history to keep the transfer lightning fast.
echo "[INFO] Syncing files to $VM_NAME..."
rsync -avz -e "gcloud compute ssh --zone=$ZONE" \
    --exclude='build/' \
    --exclude='.git/' \
    --exclude='CMakeCache.txt' \
    --exclude='*.o' \
    --exclude='a.out' \
    --exclude='trading_engine' \
    --exclude='.session_heartbeat' \
    "$SCRIPT_DIR/" "${VM_NAME}:${REMOTE_PATH}/"

# 3. Check exit status
if [ $? -eq 0 ]; then
    echo "[SUCCESS] Changes successfully synced to the VM."
else
    echo "[ERROR] Sync failed. Please check the network/SSH errors above."
fi
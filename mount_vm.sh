#!/bin/bash
# mount_vm.sh - Connects Cloud Shell to your Compute Engine VM

# --- CONFIGURATION ---
VM_NAME="your-vm-name"  # Replace with your VM name
ZONE="your-vm-zone"      # Replace with your VM zone (e.g., us-central1-a)
REMOTE_PATH="/home/sreekanth08072003/auto-trading-app"
LOCAL_MOUNT="$HOME/vm-project"

# 1. Install sshfs if not present
if ! command -v sshfs &> /dev/null; then
    echo "[INFO] Installing sshfs..."
    sudo apt-get update && sudo apt-get install -y sshfs
fi

# 2. Create local mount point
mkdir -p $LOCAL_MOUNT

# 3. Unmount in case it was already mounted
mountpoint -q $LOCAL_MOUNT && fusermount -u $LOCAL_MOUNT 2>/dev/null

# 4. Get VM IP and Mount
VM_IP=$(gcloud compute instances describe $VM_NAME --zone $ZONE --format='get(networkInterfaces[0].accessConfigs[0].natIP)')

echo "[INFO] Mounting $VM_NAME ($VM_IP) to $LOCAL_MOUNT..."
sshfs -o IdentityFile=~/.ssh/google_compute_engine -o StrictHostKeyChecking=no $USER@$VM_IP:$REMOTE_PATH $LOCAL_MOUNT

echo "[SUCCESS] Files integrated! Open the '$LOCAL_MOUNT' folder in your Cloud Shell Editor."
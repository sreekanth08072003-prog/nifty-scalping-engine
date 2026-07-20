#!/bin/bash
# setup_remote_ssh.sh - Configures SSH aliases for the Remote Explorer

echo "[INFO] Updating ~/.ssh/config with Google Cloud VM aliases..."

# This command creates aliases for all your VMs in the format:
# [INSTANCE_NAME].[ZONE].[PROJECT_ID]
gcloud compute config-ssh --quiet

echo "[SUCCESS] Configuration updated. You can now use Remote Explorer to connect."
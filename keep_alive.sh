#!/bin/bash
# Run this in a second terminal tab to help keep the Cloud Shell session active.

echo "[INFO] Heartbeat started. Keep this tab visible to prevent session timeout."
while true; do
  echo "Keep-Alive Heartbeat: $(date)"
  # Perform a small disk write to signal activity
  touch .session_heartbeat
  sleep 60
done
#!/bin/bash
echo "========== SYSTEM CHECK =========="

libraries=("curl" "ssl" "crypto" "boost_system" "pthread")
missing=0

for lib in "${libraries[@]}"; do
    ld -l$lib --verbose > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "[ERROR] Library missing: $lib"
        missing=$((missing + 1))
    else
        echo "[OK] Found: $lib"
    fi
done

if [ $missing -eq 0 ]; then
    echo "Result: Environment is READY for compilation."
    exit 0
else
    echo "Result: Environment is BROKEN. Run: sudo apt-get install libboost-all-dev libcurl4-openssl-dev libssl-dev"
    exit 1
fi
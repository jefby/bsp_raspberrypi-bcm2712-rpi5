#!/bin/bash

# 检查是否传递了 --clean 参数
CLEAN_BUILD=false
if [[ "$1" == "--clean" ]]; then
    CLEAN_BUILD=true
fi

SRC_DIR=$(pwd)
echo "Building QNX Neutrino RTOS for Raspberry Pi 5"
echo "Source directory: $SRC_DIR"
cd ~/qnx800
source qnxsdp-env.sh
cd $SRC_DIR

if $CLEAN_BUILD; then
    echo "Performing clean build..."
    make clean || { echo "Clean failed!"; exit 1; }
fi

make || { echo "Build failed!"; exit 1; }
echo "Build completed."

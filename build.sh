#!/bin/bash

SRC_DIR=$(pwd)
echo "Building QNX Neutrino RTOS for Raspberry Pi 5"
echo "Source directory: $SRC_DIR"
cd ~/qnx800
source qnxsdp-env.sh
cd $SRC_DIR
make  || { echo "Build failed!"; exit 1; }
echo "Build completed."

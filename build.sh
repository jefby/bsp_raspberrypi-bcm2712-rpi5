#!/bin/bash

# 检查是否传递了 --clean 参数
CLEAN_BUILD=false
if [[ "$1" == "--clean" ]]; then
    CLEAN_BUILD=true
fi
START_TIME=$(date +%s)
SRC_DIR=$(pwd)
echo "Building QNX Neutrino RTOS for Raspberry Pi 5"
echo "Source directory: $SRC_DIR"
cd ~/qnx800
source qnxsdp-env.sh
cd $SRC_DIR

COMMIT_HASH=$(git rev-parse --short HEAD)
echo "Build commit: $COMMIT_HASH"  >> version.txt
BUILD_TIME=`date +"%Y-%m-%d %H:%M:%S" `
echo "Build time: $BUILD_TIME"  >> version.txt

if $CLEAN_BUILD; then
    echo "Performing clean build..."
    make clean || { echo "Clean failed!"; exit 1; }
fi

make || { echo "Build failed!"; exit 1; }

# 编译 OTA 客户端
echo "Building OTA client..."
cd src/ota
make clean
make
make install
cd ../..
echo "OTA client build completed."


cd images/
cp ifs-rpi5.bin ifs-rpi5_B.bin
cd ../

END_TIME=$(date +%s)
ELAPSED_TIME=$((END_TIME - START_TIME))
echo "Build time: $ELAPSED_TIME seconds"
echo "Build completed."

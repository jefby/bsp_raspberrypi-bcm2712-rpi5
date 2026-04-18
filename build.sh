#!/bin/bash

# 检查是否传递了 --clean 参数
CLEAN_BUILD=true
# if [[ "$1" == "--clean" ]]; then
#     CLEAN_BUILD=true
# fi
START_TIME=$(date +%s)
SRC_DIR=$(pwd)
echo "Building QNX Neutrino RTOS for Raspberry Pi 5"
echo "Source directory: $SRC_DIR"
cd ~/qnx800
source qnxsdp-env.sh
cd $SRC_DIR

# 生成构建信息
COMMIT_HASH=$(git rev-parse --short HEAD)
echo "Build commit: $COMMIT_HASH"  > build_info.txt
# BUILD_TIME=`date +"%Y-%m-%d %H:%M:%S"`
BUILD_TIME=`date`
echo "Build time: $BUILD_TIME"  >> build_info.txt

if $CLEAN_BUILD; then
    echo "Performing clean build..."
    make clean || { echo "Clean failed!"; exit 1; }
fi

# 编译 OTA 客户端
echo "Building OTA client..."
cd $SRC_DIR/src/ota
make clean
make
make install
echo "OTA client build completed."

cd $SRC_DIR
# 创建 IFS 镜像
make || { echo "Build failed!"; exit 1; }

# 复制 IFS 镜像
cd images/
cp ifs-rpi5.bin ifs-rpi5_B.bin
cd ../

END_TIME=$(date +%s)
ELAPSED_TIME=$((END_TIME - START_TIME))
echo "Build time: $ELAPSED_TIME seconds"
echo "Build completed."

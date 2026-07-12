#!/bin/bash
# 发布新 IFS 到 OTA 服务器（包装 auto_local_new.sh）
# 用法：先 build.sh 成功，再执行本脚本
#   bash build.sh
#   bash auto_deploy_new.sh [seq]
#
# seq: 本周构建序号，默认 1 → 版本 YYYY.WW.seq

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec bash "${SCRIPT_DIR}/auto_local_new.sh" "$@"

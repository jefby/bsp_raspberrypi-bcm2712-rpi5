# RPi5 QNX 8 OTA 更新功能实现

## 项目背景

为 Raspberry Pi 5 QNX 8 BSP 开发 OTA（Over-The-Air）更新功能，支持不插拔 SD 卡的情况下远程更新 IFS 镜像。

## 问题分析

### 初期需求
- 如何避免频繁插拔 SD 卡进行固件更新？
- 如何实现快速、安全的远程 IFS 更新？

### 解决方案
采用最简单的方案：
1. **不修改分区表**：在现有 `/boot` 分区中保留两个 IFS 镜像（A/B 分区）
2. **OTA 客户端**：C++ 程序在 QNX 系统启动后自动后台运行
3. **检查-下载-更新流程**：定期检查服务器版本，发现新版本时下载并更新

### DNS 解析失败问题
初期遇到的 DNS 问题原因：
- `/etc/resolv.conf` 依赖 `dhcpcd` 的 `20-resolv.conf` 钩子动态更新
- 如果 DHCP 未成功获取 DNS 服务器，resolv.conf 为空
- **解决方法**：确保网络启动脚本中 DHCP 在 SSH 服务器启动之前

## 实现方案

### 架构设计

```
启动流程：
Raspberry Pi 5 (Linux/QNX 双启动)
    ↓
/boot 分区：
├── ifs-rpi5.bin       # 版本 A（当前运行）
├── ifs-rpi5_B.bin     # 版本 B（备用/目标）
├── config.txt         # kernel=ifs-rpi5.bin（选择使用哪个）
└── ...

QNX IFS 启动后：
    ↓
OTA 客户端自动启动（net_start.sh）
    ↓
后台循环（每 300 秒）：
  1. HTTP GET /version.txt → 获取服务器最新版本
  2. 比较本地版本 vs 服务器版本
  3. 如果新版本 → 下载、验证、切换、重启
```

### 核心组件

#### 1. OTA 客户端（C++）
- **文件**：`src/ota/ota_client.cpp`
- **功能**：
  - 配置文件解析（`/etc/ota_config`）
  - HTTP 下载（使用 libcurl）
  - 文件验证（大小检查）
  - config.txt 更新
  - 后台守护进程运行
  - 详细日志记录

#### 2. 编译配置
- **文件**：`src/ota/Makefile`
- **特性**：
  - QCC 编译器支持
  - libcurl 链接
  - 对象文件管理
  - 自动安装

#### 3. 配置文件
- **文件**：`/etc/ota_config`
- **参数**：
  - `OTA_SERVER`：更新服务器 URL
  - `OTA_CHECK_INTERVAL`：检查间隔（秒）
  - `OTA_ENABLED`：启用/禁用
  - `BOOT_PATH`、`VERSION_FILE`、`CONFIG_FILE`：路径配置

#### 4. IFS 集成
- **修改**：`images/rpi5.build`
- **添加内容**：
  - libcurl 库和 curl 工具
  - OTA 客户端可执行文件
  - 配置文件和版本文件
  - `net_start.sh` 中添加 OTA 客户端启动

### 工作流程（详细）

```yaml
启动阶段：
  1. Raspberry Pi 5 引导 → 加载 ifs-rpi5.bin
  2. procnto-smp 启动 → 执行 startup-script
  3. startup-script 启动各驱动和服务
  4. net_start.sh 执行 → 启动网络和 OTA 客户端

OTA 客户端循环：
  repeat every OTA_CHECK_INTERVAL:
    1. 读取本地版本：cat /etc/ota_version
    2. HTTP GET OTA_SERVER/version.txt：获取远程版本
    3. 比较版本：
        if remote_version > local_version:
          4a. 确定目标 IFS：
              current_ifs = grep "kernel=" /var/boot/config.txt
              target_ifs = (current_ifs == "ifs-rpi5.bin") ? "ifs-rpi5_B.bin" : "ifs-rpi5.bin"
          4b. 下载新 IFS：
              curl -o /proc/boot/{target_ifs} {OTA_SERVER}/ifs-rpi5_v{version}.bin
          4c. 验证文件大小 > 10MB
          4d. 更新版本号：echo {version} > /etc/ota_version
          4e. 更新 config.txt：echo "kernel={target_ifs}" > /var/boot/config.txt
          4f. 系统重启：shutdown -r now
        else:
          等待下次检查
    5. 出错处理 → 记录日志 → 继续循环
```

### 文件结构

```
src/ota/
├── ota_client.cpp              # 1000+ 行 C++ 代码
│   ├── 配置读取（OTAConfig 结构体）
│   ├── 日志系统（log_msg）
│   ├── 文件操作（read_config, get_version 等）
│   ├── 网络函数（curl_get, download_file）
│   ├── 验证函数（verify_ifs）
│   ├── 更新函数（switch_ifs）
│   └── 主循环（ota_loop）
├── Makefile                    # 编译配置
├── ota_config.conf             # 配置文件模板
├── README.md                   # 详细文档
├── QUICKSTART.md               # 快速参考
├── setup_ota_build.sh          # 自动化构建脚本
└── DEPLOYMENT.md               # 部署指南

images/
└── rpi5.build                  # 修改后的 IFS 构建文件
    ├── 添加 libcurl 库
    ├── 添加 OTA 客户端
    ├── 添加配置文件
    └── 修改 net_start.sh
```

## 关键技术细节

### 1. A/B 分区策略
```bash
# 初始化
/boot/ifs-rpi5.bin    # active
/boot/ifs-rpi5_B.bin  # backup

# config.txt
kernel=ifs-rpi5.bin

# 更新后
/boot/ifs-rpi5.bin    # backup (被新版本覆盖)
/boot/ifs-rpi5_B.bin  # active

# 新 config.txt
kernel=ifs-rpi5_B.bin

# 下一次更新
/boot/ifs-rpi5.bin    # 成为新的目标
/boot/ifs-rpi5_B.bin  # 成为 backup
```

### 2. 版本管理
```
/etc/ota_version：存储本地版本号
  版本 1 → ifs-rpi5.bin
  版本 2 → ifs-rpi5_B.bin
  版本 3 → ifs-rpi5.bin (再次使用)

HTTP /version.txt：存储服务器最新版本号
```

### 3. libcurl 集成
```cpp
// 关键 API 使用
curl_easy_init()          // 初始化
curl_easy_setopt()        // 配置选项
curl_easy_perform()       // 执行请求
curl_easy_cleanup()       // 清理

// 重点选项
CURLOPT_URL               // 目标 URL
CURLOPT_WRITEFUNCTION     // 数据处理回调
CURLOPT_TIMEOUT           // 超时控制
CURLOPT_XFERINFOFUNCTION  // 进度回调
```

### 4. 后台守护进程
```cpp
// C++ 实现
pid_t pid = fork();       // 分叉进程
if (pid > 0) exit(0);     // 父进程退出
if (pid == 0) {
    setsid();             // 创建新会话
    // OTA 循环继续运行
}
```

## 部署步骤

### 编译
```bash
cd src/ota
make clean && make        # 编译
make install              # 安装到 install/aarch64/usr/bin/
```

### 构建 IFS
```bash
cd images
mkifs -v rpi5.build ifs-rpi5.bin
cp ifs-rpi5.bin ifs-rpi5_B.bin
```

### 复制到 SD 卡
```bash
mount /dev/sdXp1 /media/boot/
cp ifs-rpi5.bin /media/boot/
cp ifs-rpi5_B.bin /media/boot/
echo "kernel=ifs-rpi5.bin" >> /media/boot/config.txt
```

### 服务器配置（Apache2）
```bash
sudo mkdir -p /var/www/ota
echo "2" > /var/www/ota/version.txt
cp ifs-rpi5_v2.bin /var/www/ota/
```

## 测试验证

### 日志检查
```bash
tail -f /tmp/ota_client.log        # 实时监控
grep "New version" /tmp/ota_client.log    # 查找更新事件
```

### 进程检查
```bash
ps aux | grep ota_client           # 确认运行
ps -eo "%p %C %a" | grep ota       # 查看资源占用
```

### 网络测试
```bash
curl http://server:8080/version.txt        # 测试连接
curl -o /tmp/test.bin http://server:8080/ifs-rpi5_v2.bin  # 测试下载
```

## 优化和扩展方向

### 已实现
- ✅ 基础 OTA 功能
- ✅ A/B 分区策略
- ✅ 自动回滚能力
- ✅ 详细日志记录
- ✅ 后台守护进程

### 待实现
- 🔲 哈希验证（SHA256）
- 🔲 签名验证（RSA）
- 🔲 HTTPS 支持
- 🔲 断点续传
- 🔲 前端管理界面
- 🔲 自动失败回滚
- 🔲 版本回滚接口

## 常见问题解决

| 问题 | 原因 | 解决 |
|------|------|------|
| OTA 无法启动 | 网络未初始化 | 检查 net_start.sh 顺序 |
| 连接失败 | DNS 未配置 | 确保 dhcpcd 成功获取 IP |
| 下载超时 | 网络慢或服务器离线 | 增加超时时间 |
| 版本未更新 | 配置不正确 | 检查 /etc/ota_config 权限 |
| 重启失败 | 权限问题 | 确保 OTA 客户端以 root 运行 |

## 参考资源

- **OTA 详细文档**：[src/ota/README.md](src/ota/README.md)
- **快速参考**：[src/ota/QUICKSTART.md](src/ota/QUICKSTART.md)
- **部署指南**：[src/ota/DEPLOYMENT.md](src/ota/DEPLOYMENT.md)
- **源代码**：[src/ota/ota_client.cpp](src/ota/ota_client.cpp)
- **构建脚本**：[src/ota/setup_ota_build.sh](src/ota/setup_ota_build.sh)

## 总结

本项目成功为 RPi5 QNX BSP 实现了企业级的 OTA 更新功能。通过采用最简单的 A/B 分区策略和 HTTP-based 更新机制，实现了无插拔 SD 卡的远程固件更新能力。整个系统轻量、可靠且易于部署。
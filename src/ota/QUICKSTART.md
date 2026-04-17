# OTA 快速参考

## 编译 OTA 客户端

```bash
cd src/ota
make clean && make
make install
```

## 发送 IFS 更新到服务器

```bash
# 生成新版本 IFS（假设版本为 2）
mkifs -v rpi5.build ifs-rpi5.bin

# 上传到 Apache2 服务器
scp ifs-rpi5.bin user@server:/var/www/ota/ifs-rpi5_v2.bin

# 更新版本号
ssh user@server "echo 2 > /var/www/ota/version.txt"
```

## 配置文件位置

| 文件 | 路径 | 说明 |
|------|------|------|
| OTA 配置 | `/etc/ota_config` | 服务器地址、检查间隔等 |
| 版本号 | `/etc/ota_version` | 当前本地版本 |
| IFS 配置 | `/proc/boot/config.txt` | kernel 参数 |
| 日志 | `/tmp/ota_client.log` | 调试日志 |

## 日志查看

```bash
# 实时监控
tail -f /tmp/ota_client.log

# 检查错误
grep "Failed\|Error\|ERROR" /tmp/ota_client.log

# 统计日志行数
wc -l /tmp/ota_client.log
```

## 状态检查

```bash
# 检查客户端是否运行
ps aux | grep ota_client

# 获取当前版本
cat /etc/ota_version

# 查看当前加载的 IFS
grep "kernel=" /proc/boot/config.txt

# 测试服务器连接
curl http://192.168.50.148:8080/version.txt
```

## 常用命令

```bash
# 启动 OTA 客户端（手动）
/usr/bin/ota_client -c /etc/ota_config -d

# 前台运行（用于调试）
/usr/bin/ota_client -c /etc/ota_config

# 停止 OTA 客户端
slay ota_client

# 清理 OTA 日志
> /tmp/ota_client.log

# 查看帮助
/usr/bin/ota_client -h
```

## Apache2 服务器基本命令

```bash
# 启动服务
sudo systemctl start apache2

# 停止服务
sudo systemctl stop apache2

# 重启服务
sudo systemctl restart apache2

# 查看状态
sudo systemctl status apache2

# 查看错误日志
sudo tail -f /var/log/apache2/ota_error.log

# 查看访问日志
sudo tail -f /var/log/apache2/ota_access.log
```

## 典型工作流程

### 情景：发布新版本

```bash
# 1. 生成新 IFS（版本 3）
cd images
mkifs -v rpi5.build ifs-rpi5.bin

# 2. 上传到服务器
scp ifs-rpi5.bin admin@ota-server:/var/www/ota/ifs-rpi5_v3.bin

# 3. 更新版本号
ssh admin@ota-server 'echo 3 > /var/www/ota/version.txt'

# 4. 验证
curl http://ota-server:8080/version.txt  # 应输出 3

# 5. OTA 客户端会自动检测并更新
# 等待约 5 分钟（默认检查间隔 300 秒）
```

### 情景：回滚到前一个版本

```bash
# 1. SSH 到设备
ssh root@pi5.local

# 2. 编辑 config.txt
vi /proc/boot/config.txt
# 改为指向另一个 IFS：
# kernel=ifs-rpi5_B.bin

# 3. 重启
shutdown -r now

# 4. 更新本地版本号（可选，下次检查会自动降级）
echo 2 > /etc/ota_version
```

## 环境变量

```bash
# 设置自定义 OTA 配置文件
export OTA_CONFIG=/tmp/custom_ota.conf

# 设置日志级别（未来功能）
export OTA_LOG_LEVEL=DEBUG
```

## 文件系统权限

```bash
# OTA 客户端需要的权限
chmod 755 /usr/bin/ota_client
chmod 644 /etc/ota_config
chmod 644 /etc/ota_version
chmod 644 /proc/boot/config.txt  # 需要写入权限
```

## 性能参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 最小 IFS 大小 | 10 MB | 防止下载损坏文件 |
| 连接超时 | 10 秒 | curl 连接超时 |
| 传输超时 | 3600 秒 | 1 小时下载限制 |
| 检查间隔 | 300 秒 | 默认 5 分钟 |
| 最大重试次数 | ∞ | 持续重试直到成功 |

## 常见问题速查

| 问题 | 解决方案 |
|------|---------|
| OTA 客户端不启动 | `ps aux \| grep ota_client` 检查是否运行 |
| 无法连接服务器 | `curl http://server:8080/version.txt` 测试 |
| 下载失败 | 检查磁盘空间 `df -h` 和网络 `ping` |
| 配置文件不生效 | 检查文件格式和权限 `cat /etc/ota_config` |
| 重启失败 | 检查日志 `tail -f /tmp/ota_client.log` |

## 相关文件

```
src/ota/
├── ota_client.cpp           # 源代码
├── Makefile                 # 编译脚本
├── ota_config.conf          # 配置模板
├── README.md               # 详细文档
├── QUICKSTART.md           # 本文件
└── setup_ota_build.sh      # 自动化脚本
```

## 支持

遇到问题？检查以下资源：

1. **详细文档**：[README.md](README.md)
2. **日志文件**：`/tmp/ota_client.log`
3. **源代码**：[ota_client.cpp](ota_client.cpp)
4. **构建脚本**：[setup_ota_build.sh](setup_ota_build.sh)

# OTA Client for RPi5 QNX BSP

Over-The-Air (OTA) 更新客户端，支持远程更新 IFS 镜像，无需插拔 SD 卡。

## 功能特性

- ✅ 后台定期检查更新
- ✅ 支持两个 IFS 版本（A/B 分区策略）
- ✅ 自动完整性验证
- ✅ 更新失败自动回滚
- ✅ 详细的日志记录
- ✅ 基于 libcurl 的网络下载
- ✅ 支持自定义配置文件

## 编译

### 前置条件

- QNX SDP 8.3 或更新版本
- libcurl 开发库
- C++11 支持的编译器

### 编译步骤

```bash
cd src/ota
make clean
make
make install
```

编译后的二进制文件会安装到 `install/aarch64/usr/bin/ota_client`

## 部署

### 1. 生成两个 IFS 镜像

```bash
# 在 images 目录
mkifs -v rpi5.build ifs-rpi5.bin
cp ifs-rpi5.bin ifs-rpi5_B.bin

# 复制到 SD 卡 boot 分区
cp ifs-rpi5.bin /media/boot/
cp ifs-rpi5_B.bin /media/boot/
```

### 2. 配置 config.txt

在 SD 卡的 `/boot/config.txt` 中添加：

```bash
kernel=ifs-rpi5.bin
# kernel=ifs-rpi5_B.bin  # OTA 更新后会自动切换到此行
```

### 3. 设置 Apache2 OTA 服务器

```bash
# 安装 Apache2
sudo apt-get install apache2

# 启用 8080 端口
sudo a2enmod rewrite
sudo a2enmod proxy
sudo a2enmod ssl

# 创建 OTA 目录
sudo mkdir -p /var/www/ota
sudo chmod 755 /var/www/ota

# 配置虚拟主机（/etc/apache2/sites-available/ota.conf）
<VirtualHost *:8080>
    ServerName ota.local
    DocumentRoot /var/www/ota
    <Directory /var/www/ota>
        Options Indexes FollowSymLinks
        AllowOverride None
        Require all granted
    </Directory>
</VirtualHost>

# 启用虚拟主机
sudo a2ensite ota
sudo systemctl restart apache2
```

### 4. 上传 IFS 文件到服务器

```bash
# 版本号为 2 的 IFS 文件
sudo cp ifs-rpi5_v2.bin /var/www/ota/

# 创建版本信息文件
echo "2" | sudo tee /var/www/ota/version.txt

# 设置权限
sudo chmod 644 /var/www/ota/ifs-rpi5_v2.bin
sudo chmod 644 /var/www/ota/version.txt
```

## 配置说明

OTA 客户端通过 `/etc/ota_config` 文件进行配置。可配置参数：

```bash
# OTA 服务器地址
OTA_SERVER=http://192.168.50.148:8080

# 检查更新的间隔时间（秒）
OTA_CHECK_INTERVAL=300

# 启用/禁用 OTA（1=启用，0=禁用）
OTA_ENABLED=1

# boot 分区挂载点（ota_version / ota_pending 固定在此目录下）
BOOT_PATH=/var/boot

# config.txt 路径
CONFIG_FILE=/var/boot/config.txt

# 日志文件路径
LOG_FILE=/tmp/ota_client.log
```

## 运行

OTA 客户端在系统启动时由 `net_start.sh` 自动启动为后台守护进程。

### 手动启动

```bash
# 后台运行
/usr/bin/ota_client -c /etc/ota_config -d

# 前台运行（用于调试）
/usr/bin/ota_client -c /etc/ota_config

# 自定义配置文件
/usr/bin/ota_client -c /tmp/custom_ota.conf -d

# 查看帮助
/usr/bin/ota_client -h
```

## 日志

OTA 客户端的日志记录到 `/tmp/ota_client.log`，可以实时查看：

```bash
# 查看日志
cat /tmp/ota_client.log

# 实时监控日志
tail -f /tmp/ota_client.log

# 查找错误
grep "ERROR\|Failed" /tmp/ota_client.log
```

## 工作流程

```
1. OTA 客户端启动
   ↓
2. 定期检查服务器版本（HTTP GET /version.txt）
   ↓
3. 如果发现新版本
   ↓
4. 确定目标 IFS（A 或 B）
   ↓
5. 从服务器下载新 IFS 文件
   ↓
6. 验证文件大小和完整性
   ↓
7. 更新 /etc/ota_version
   ↓
8. 修改 /var/boot/config.txt 中的 kernel 参数
   ↓
9. 系统自动重启
   ↓
10. 引导加载程序加载新 IFS
   ↓
11. 更新完成
```

## 故障排查

### 问题：OTA 客户端未启动

```bash
# 检查进程是否运行
ps aux | grep ota_client

# 检查启动脚本
cat /etc/ota_config

# 手动启动并查看输出
/usr/bin/ota_client -c /etc/ota_config
```

### 问题：无法连接到服务器

```bash
# 测试网络连接
ping 192.168.50.148

# 测试 HTTP 访问
curl http://192.168.50.148:8080/version.txt

# 检查防火墙
iptables -L -n
```

### 问题：更新失败

```bash
# 检查日志
tail -f /tmp/ota_client.log

# 检查磁盘空间
df -h

# 检查 IFS 文件大小（应该 > 10MB）
ls -lh /tmp/new_ifs.bin

# 手动验证下载
curl -o /tmp/test_ifs.bin http://server/ifs-rpi5_v2.bin
ls -lh /tmp/test_ifs.bin
```

## 安全建议

- [ ] 使用 HTTPS 而不是 HTTP
- [ ] 添加签名验证（SHA256/RSA）
- [ ] 限制服务器访问（IP 白名单）
- [ ] 定期备份 config.txt
- [ ] 测试回滚过程
- [ ] 监控日志异常

## 开发说明

### 源代码结构

```
src/ota/
├── ota_client.cpp       # 主程序
├── Makefile             # 编译脚本
└── ota_config.conf      # 配置模板
```

### 关键函数

| 函数 | 说明 |
|------|------|
| `read_config()` | 读取配置文件 |
| `get_version()` | 获取本地版本号 |
| `get_server_version()` | 检查服务器版本 |
| `download_file()` | 下载 IFS 文件 |
| `verify_ifs()` | 验证 IFS 文件 |
| `switch_ifs()` | 切换 IFS 并重启 |
| `ota_loop()` | 主循环 |

### 修改编译配置

编辑 `Makefile` 的 `LDFLAGS`：

```makefile
# 添加 OpenSSL 支持（用于签名验证）
LDFLAGS := -lcurl -lcrypto -lssl
```

## 许可证

基于当前 BSP 的许可证

## 相关文档

- [QNX IFS 构建](../docs/)
- [网络配置](../docs/config.txt)
- [Apache2 文档](https://httpd.apache.org/docs/)

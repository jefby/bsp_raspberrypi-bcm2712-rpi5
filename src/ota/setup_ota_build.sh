#!/bin/bash
# setup_ota_build.sh - OTA 构建和部署脚本

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
IMAGES_DIR="$PROJECT_ROOT/images"
INSTALL_DIR="$PROJECT_ROOT/install"

echo "=========================================="
echo "RPi5 QNX OTA 构建脚本"
echo "=========================================="

# 1. 编译 OTA 客户端
echo ""
echo "[1/4] 编译 OTA 客户端..."
cd "$SCRIPT_DIR"
make clean
make
make install
echo "✓ OTA 客户端编译完成"

# 2. 验证编译产物
echo ""
echo "[2/4] 验证编译产物..."
OTA_BIN="$INSTALL_DIR/aarch64/usr/bin/ota_client"
if [ -f "$OTA_BIN" ]; then
    echo "✓ 找到 OTA 客户端: $OTA_BIN"
    ls -lh "$OTA_BIN"
else
    echo "✗ 未找到 OTA 客户端二进制文件"
    exit 1
fi

# 3. 创建 IFS 镜像
echo ""
echo "[3/4] 创建 IFS 镜像..."
cd "$IMAGES_DIR"

if command -v mkifs &> /dev/null; then
    echo "生成 ifs-rpi5.bin..."
    mkifs -v rpi5.build ifs-rpi5.bin
    
    echo "生成 ifs-rpi5_B.bin..."
    cp ifs-rpi5.bin ifs-rpi5_B.bin
    
    echo "✓ IFS 镜像创建完成"
    ls -lh ifs-rpi5*.bin
else
    echo "⚠ mkifs 未找到，请确保已安装 QNX SDP"
    echo "   可以在 Windows 上手动运行 mkifs 或在 WSL 中安装"
fi

# 4. 生成信息总结
echo ""
echo "[4/4] 生成部署说明..."
cat > "$SCRIPT_DIR/DEPLOYMENT.md" << 'EOF'
# OTA 部署步骤

## 准备工作

### 1. SD 卡准备
```bash
# 在 Linux/Mac 上
# 请确保已插入 SD 卡

# 检查设备名称（例如：/dev/sdb）
lsblk

# 卸载分区（如果自动挂载了）
umount /media/*/boot
umount /media/*/rootfs

# 使用 Raspberry Pi Imager 或 dd 刷入 Raspberry Pi OS
```

### 2. 复制 IFS 文件到 boot 分区
```bash
# 挂载 boot 分区
mount /dev/sdXp1 /media/boot/

# 复制 IFS 文件
cp images/ifs-rpi5.bin /media/boot/
cp images/ifs-rpi5_B.bin /media/boot/

# 验证
ls -lh /media/boot/ifs-rpi5*.bin
```

### 3. 配置 config.txt
```bash
# 编辑 boot 分区的 config.txt
# 在文件末尾添加：
echo "kernel=ifs-rpi5.bin" >> /media/boot/config.txt
```

### 4. 卸载并等待同步
```bash
sync
umount /media/boot/
```

## Apache2 服务器配置（在更新服务器上）

### 1. 安装 Apache2
```bash
sudo apt-get install apache2
sudo systemctl enable apache2
```

### 2. 创建 OTA 目录
```bash
sudo mkdir -p /var/www/ota
sudo chmod 755 /var/www/ota
```

### 3. 配置虚拟主机
```bash
sudo tee /etc/apache2/sites-available/ota.conf << 'EOV'
Listen 8080
<VirtualHost *:8080>
    ServerName ota.local
    DocumentRoot /var/www/ota
    <Directory /var/www/ota>
        Options Indexes FollowSymLinks
        AllowOverride All
        Require all granted
    </Directory>
    ErrorLog ${APACHE_LOG_DIR}/ota_error.log
    CustomLog ${APACHE_LOG_DIR}/ota_access.log combined
</VirtualHost>
EOV

sudo a2ensite ota
sudo systemctl restart apache2
```

### 4. 上传 IFS 和版本信息
```bash
# 版本 2 的 IFS（第一次更新）
sudo cp ifs-rpi5.bin /var/www/ota/ifs-rpi5_v2.bin

# 版本文件
echo "2" | sudo tee /var/www/ota/version.txt

# 权限
sudo chmod 644 /var/www/ota/*
```

### 5. 验证服务器
```bash
# 本地测试
curl http://localhost:8080/version.txt  # 应输出：2
curl -I http://localhost:8080/ifs-rpi5_v2.bin  # HTTP 200
```

## 启动树莓派

### 第一次启动
```
1. 将 SD 卡插入树莓派
2. 连接电源和网络
3. 系统会从 Raspberry Pi OS 启动
4. 如果需要，使用 Raspberry Pi Imager 切换到 QNX
5. 或通过修改 config.txt 切换到 QNX IFS
```

### QNX 系统启动
```
1. QNX 加载 Raspberry Pi OS 中的 ifs-rpi5.bin
2. OTA 客户端自动在后台启动（net_start.sh 中）
3. 定期检查 Apache2 服务器的版本
```

## 测试更新过程

### 1. 检查 OTA 客户端状态
```bash
# 在树莓派的QNX系统中
ps aux | grep ota_client

# 查看日志
cat /tmp/ota_client.log
tail -f /tmp/ota_client.log
```

### 2. 模拟更新
```bash
# 在 Apache2 服务器上
# 上传新版本的 IFS
sudo cp ifs-rpi5.bin /var/www/ota/ifs-rpi5_v3.bin

# 更新版本号
echo "3" | sudo tee /var/www/ota/version.txt

# 刷新权限
sudo chmod 644 /var/www/ota/*
```

### 3. 观察更新过程
```bash
# SSH 连接到树莓派，监控日志
ssh user@pi5.local

# 查看 OTA 日志
tail -f /tmp/ota_client.log

# 等待系统自动重启（大约 300 秒后开始检查）
```

### 4. 验证更新成功
```bash
# 重启后，检查当前版本
cat /etc/ota_version  # 应输出：3

# 检查当前 IFS
cat /var/boot/config.txt  # kernel=ifs-rpi5_B.bin 或 ifs-rpi5.bin
```

## 故障排查

### OTA 客户端无法启动
```bash
# 检查配置文件
cat /etc/ota_config

# 手动启动 OTA 客户端
/usr/bin/ota_client -c /etc/ota_config

# 查看错误信息
/usr/bin/ota_client -c /etc/ota_config 2>&1 | head -20
```

### 无法连接到服务器
```bash
# 检查网络连接
ping 8.8.8.8

# 检查特定服务器
ping <ota_server_ip>

# 测试 HTTP 连接
curl -v http://<ota_server>:8080/version.txt

# 检查防火墙
iptables -L -n | grep 8080
```

### 更新卡住
```bash
# 检查日志
tail -f /tmp/ota_client.log

# 检查磁盘空间
df -h

# 手动杀死 OTA 进程（如需要）
slay ota_client

# 手动重启
shutdown -v
```

## 回滚操作

如果更新出现问题，可以手动回滚：

```bash
# 编辑 /var/boot/config.txt
# 改为指向另一个 IFS 文件

# 或直接启动时按住按钮、修改 config.txt，然后重启
```

## 生产部署建议

- [ ] 添加签名验证（SHA256）
- [ ] 使用 HTTPS 而不是 HTTP
- [ ] 实施访问控制（IP 白名单）
- [ ] 定期备份配置文件
- [ ] 监控日志和错误率
- [ ] 分阶段更新（先小量，再全量）
- [ ] 保留紧急回滚计划

## 参考文档

- [OTA 客户端详细文档](README.md)
- [QNX 官方文档](https://blackberry.qnx.com/)
- [Apache2 文档](https://httpd.apache.org/docs/)
EOF

echo "✓ 部署说明生成完成: $SCRIPT_DIR/DEPLOYMENT.md"

echo ""
echo "=========================================="
echo "构建完成！"
echo "=========================================="
echo ""
echo "后续步骤："
echo "1. 查看部署说明: cat $SCRIPT_DIR/DEPLOYMENT.md"
echo "2. 将 IFS 文件复制到 SD 卡 boot 分区"
echo "3. 在服务器上部署 Apache2 和 OTA 文件"
echo "4. 启动树莓派进行 OTA 测试"
echo ""

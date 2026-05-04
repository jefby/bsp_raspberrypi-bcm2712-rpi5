# OTA Client 流程梳理

> 基于 `src/ota/ota_client.cpp` + `images/rpi5.build` 整理，版本日期：2026-05-04

---

## 1. 启动链

```
RPi5 上电
  └─ startup-bcm2712-rpi5（硬件初始化）
      └─ procnto-smp（QNX 微内核）
          └─ startup-script（rpi5.build 内嵌脚本）
              ├─ 驱动依次启动（WDT / PCI / UART / SPI / SDMMC / USB）
              ├─ .storage-server.sh（devf-ram → /var/run /var/db）
              ├─ net_start.sh          ← OTA 在此启动
              ├─ customize_startup.sh
              └─ ksh（交互 shell）
```

### net_start.sh 关键顺序

```
1. gpio-rp1 set 32 op pd dh        # 拉低以太网复位引脚
2. io-sock（加载网卡驱动）
3. dhcpcd -bqq                      # 后台获取 IP + DNS（resolv.conf 由钩子写入）
4. .ssh-server.sh                   # 生成 SSH 密钥 → 启动 sshd
5. ota_client -c /etc/ota_config -d # 以守护进程模式启动 OTA
6. devc-pty + qconn                 # 调试支持
```

> **顺序关键**：dhcpcd 必须先于 ota_client，否则 DNS 未就绪导致域名解析失败。

---

## 2. main() 初始化流程

```
main()
  ├─ 设置默认配置（内存中）
  │     server_url      = http://192.168.50.148:8080
  │     check_interval  = 300 s
  │     boot_path       = /var/boot
  │     version_file    = /var/boot/ota_version   ← 固定跟随 boot_path
  │     config_file     = /var/boot/config.txt
  │     log_file        = /tmp/ota_client.log
  │     ota_config_path = /etc/ota_config
  │
  ├─ 检查 /var/boot 是否已挂载（/tmp/mounts 临时文件）
  │     已挂载 → 跳过
  │     未挂载 → mount -t dos /dev/sd0t12 /var/boot
  │               失败 → 直接退出（return 1）
  │
  ├─ 解析命令行参数
  │     -c <file>  覆盖配置文件路径
  │     -d         守护进程模式（当前未实现 fork，由 & 后台化）
  │     -h         打印帮助并退出
  │
  ├─ read_config("/etc/ota_config")   # 用文件值覆盖默认值
  │     注意：BOOT_PATH 会同时更新 version_file（硬绑定）
  │           VERSION_FILE 配置项被忽略
  │
  └─ std::thread(ota_worker).join()   # 进入主循环，永不返回
```

---

## 3. OTA 主循环（ota_loop）

```
ota_loop()
  ├─ 打印启动日志（server / interval）
  ├─ 若 OTA_ENABLED=0 → 直接 return
  │
  └─ while(true)
        ├─ [1] check_and_update_config()   # Config OTA（可选热重载）
        ├─ [2] get_version()               # 读本地版本
        ├─ [3] get_server_version()        # HTTP GET /version.txt
        │       失败 → sleep(interval) → continue
        │
        ├─ [4] is_newer_version(server, local)？
        │       否 → sleep(interval) → continue
        │       是 → 进入更新流程
        │
        ├─ [5] 确定目标槽
        │       get_active_ifs() → 解析 /var/boot/config.txt 中 kernel= 行
        │       active==IFS_A → target=IFS_B，反之亦然
        │
        ├─ [6] 构造下载 URL
        │       filename = ifs-rpi5_v{server_version}.bin
        │       url      = {server_url}/{filename}
        │       dest     = /var/boot/{target_ifs}
        │
        ├─ [7] download_file(url, dest)
        │       失败 → sleep(interval) → continue（不删文件，curl 失败时已删）
        │
        ├─ [8] verify_ifs(dest)
        │       检查文件存在 且 size ≥ 10 MB
        │       失败 → remove_file(dest) → sleep(interval) → continue
        │
        ├─ [9] write_version(server_version)
        │       写入 /var/boot/ota_version
        │       失败 → remove_file(dest) → continue（放弃本次更新）
        │
        ├─ [10] switch_ifs(target_ifs)
        │         a. 备份 config.txt → config.txt.bak
        │         b. 逐行读取 config.txt，替换 kernel= 行为新槽
        │            （其余行原样保留，不破坏 RPi5 其他 config 选项）
        │         c. 写回 config.txt
        │         d. sleep 10 s → system("shutdown -v")  # 重启
        │
        └─ break（switch_ifs 成功后退出循环）
```

---

## 4. Config OTA 子流程（check_and_update_config）

```
check_and_update_config()
  ├─ curl GET {server_url}/ota_config → remote_content
  │   失败或空 → return false（静默，不影响 IFS OTA）
  │
  ├─ 比较 remote_content vs 本地文件内容
  │   相同 → return false（无需更新）
  │
  ├─ 写临时文件 /tmp/ota_config.new（用于热重载）
  │
  ├─ 尝试持久化
  │     写 ota_config_path 成功 → 同时备份旧配置为 .bak
  │     写失败（/etc 只读）     → 降级写 /tmp/ota_config
  │
  └─ read_config("/tmp/ota_config.new")   # 热重载到 g_config，立即生效
     g_config.ota_config_path = persist_path
```

---

## 5. A/B 槽切换逻辑

| 轮次 | 当前 kernel= | 目标槽 | 操作 |
|------|-------------|--------|------|
| 第 1 次更新 | `ifs-rpi5.bin`（A） | `ifs-rpi5_B.bin`（B） | 覆盖 B，重启加载 B |
| 第 2 次更新 | `ifs-rpi5_B.bin`（B） | `ifs-rpi5.bin`（A） | 覆盖 A，重启加载 A |
| 第 N 次更新 | 交替 | 交替 | 始终覆盖非活跃槽 |

> 版本文件 `/var/boot/ota_version` 在覆盖前写入，确保重启后不重复下载。

---

## 6. 版本号格式

| 格式 | 示例 | 说明 |
|------|------|------|
| `YYYY.WW.N` | `2026.18.1` | 新格式（Tesla 风格）：年.ISO周.序号 |
| `N`（整数） | `3` | 旧格式兼容，解析为 `{year=0, week=0, seq=N}` |

新格式版本始终大于旧格式版本（year/week > 0 vs 0）。  
比较规则：先比 year → week → seq，均使用整数大小。

---

## 7. 关键路径一览

| 路径 | 说明 |
|------|------|
| `/var/boot/` | FAT 分区挂载点（SD 卡 `/dev/sd0t12`） |
| `/var/boot/ifs-rpi5.bin` | IFS 槽 A |
| `/var/boot/ifs-rpi5_B.bin` | IFS 槽 B |
| `/var/boot/config.txt` | RPi5 引导配置（`kernel=` 选择槽） |
| `/var/boot/ota_version` | 本地已安装版本号 |
| `/etc/ota_config` | OTA 配置（只读 IFS 内嵌，可由 Config OTA 覆盖到 /tmp） |
| `/tmp/ota_client.log` | 运行日志 |
| `/tmp/ota_config` | Config OTA 降级持久化路径（/etc 只读时） |

---

## 8. 错误处理策略

| 阶段 | 失败行为 |
|------|---------|
| /var/boot 挂载失败 | 进程退出（return 1） |
| 获取服务器版本失败 | 记录日志，sleep，重试 |
| 下载失败 | curl 内部删除临时文件，记录日志，下轮重试 |
| 文件校验失败（< 10 MB） | 删除文件，跳过本轮 |
| 写版本文件失败 | 删除已下载文件，放弃本次更新 |
| switch_ifs 写 config.txt 失败 | 记录日志，版本已写但未重启（需人工介入） |
| Config OTA 任何步骤失败 | 静默忽略，不影响 IFS OTA 主流程 |
| 循环内未捕获异常 | catch(exception) 记录日志，sleep，继续循环 |

---

## 9. 待完善项

- [ ] SHA256 / 数字签名验证（当前仅检查文件大小）
- [ ] HTTPS 支持（当前 libcurl 配置无 SSL 验证）
- [ ] 断点续传（`CURLOPT_RESUME_FROM`）
- [ ] 下载失败后的自动回滚（switch_ifs 成功但新镜像无法启动时）
- [ ] `/var/boot` 挂载重试逻辑（当前失败即退出）
- [ ] `-d` 参数的真正 fork/setsid 守护进程化（当前依赖 shell `&`）

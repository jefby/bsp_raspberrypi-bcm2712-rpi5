# QNX 8 (RPi5) devinfo 输出解析

## 来源

在 RPi5 上运行的 QNX 8 系统中执行 `devinfo` 命令得到的设备树拓扑输出：

```
nexus0
  cryptosoft0
  armv8crypto0
  ofwbus0
    simplebus0
      syscon_generic_dev0
    simplebus1
      pcib0
        simplebus2
          cgem0
            miibus0
              brgphy0
      syscon_generic_dev1
  iousb0
    uhub0
    uhub1
```

## 逐项解读

```
nexus0                          # 根总线控制器（nexus），所有硬件设备的顶层挂载点
  cryptosoft0                   # 软件加密 provider（无硬件加速时的 fallback 实现，AES/SHA 等算法的纯软件版本）
  armv8crypto0                  # ARMv8 硬件加密扩展（Cortex-A76 自带的 AES/SHA 指令集加速，性能优于 cryptosoft）

  ofwbus0                       # Open Firmware Bus —— 由设备树（DTB）解析出来的总线，
                                #   QNX 通过它读取 .dtb 来枚举 SoC 内部各模块
    simplebus0                  # 一个"简单总线"节点（DT 中 compatible="simple-bus" 的内存映射总线）
      syscon_generic_dev0       #   通用系统控制器（syscon），通常对应电源管理/时钟/复用寄存器这类小型寄存器块

    simplebus1                  # 另一条简单总线
      pcib0                     #   PCIe 主控制器/桥（BCM2712 通过 PCIe 连接 RP1 南桥芯片）
        simplebus2              #     PCIe 链路对端的总线 —— 实际上就是 RP1 芯片内部暴露出来的总线
          cgem0                 #       Cadence GEM 千兆以太网 MAC 控制器（RP1 集成的网卡）
            miibus0             #         MII 总线（MAC 与 PHY 之间的管理接口）
              brgphy0           #           Broadcom 以太网 PHY 芯片驱动（物理层收发器）
      syscon_generic_dev1       #   另一个通用系统控制器实例

  iousb0                        # USB 主控制器（RP1 提供的 xHCI/USB 控制器）
    uhub0                       #   USB 根集线器 0（对应一组 USB 端口，如 USB 3.0）
    uhub1                       #   USB 根集线器 1（对应另一组端口，如 USB 2.0）
```

## 关键结论

1. **`ofwbus0` 是核心枢纽**：它表明 QNX 是通过解析设备树（DTB）来识别硬件的，而非硬编码探测，这是 ARM SoC 的标准做法。

2. **网卡和 USB 都挂在 `pcib0` / `iousb0` 之下，并经由 PCIe 总线**：印证了树莓派 5 的架构特点——CPU（BCM2712）本身只提供很少的外设接口，绝大部分 I/O（千兆网口、USB、PCIe 插槽等）都是由南桥芯片 **RP1** 通过 PCIe 链路扩展出来的。`simplebus2 → cgem0/miibus0/brgphy0` 和 `iousb0` 实际上就是 RP1 内部的网络与 USB 子系统。

3. **加密模块软硬两套实现并存**（`armv8crypto0` 硬件加速 + `cryptosoft0` 软件兜底）：如果后续要在 OTA 客户端中实现镜像哈希/签名校验（参见 [CLAUDE.md](../CLAUDE.md) 中"待实现"的 SHA256/RSA 项），系统层面已具备调用硬件加密加速的能力，可优先选用更快的 `armv8crypto0`。

4. **未见存储控制器**（如 SD/eMMC、SATA）出现在该树中：若需排查 OTA 相关的 `/boot`、`/proc/boot` 访问问题，需另行确认存储设备是否被正确枚举（可能在输出的其他部分，或被截断未显示）。

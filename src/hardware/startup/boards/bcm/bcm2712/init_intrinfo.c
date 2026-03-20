/*
 * Notice on Software Maturity and Quality
 *
 * The software included in this repository is classified under our Software Maturity Standard as Experimental Software - Software Quality and Maturity Level (SQML) 1.
 *
 * As defined in the QNX Development License Agreement, Experimental Software represents early-stage deliverables intended for evaluation or proof-of-concept purposes.
 *
 * SQML 1 indicates that this software is provided without one or more of the following:
 *     - Formal requirements
 *     - Formal design or architecture
 *     - Formal testing
 *     - Formal support
 *     - Formal documentation
 *     - Certifications of any type
 *     - End-of-Life or End-of-Support policy
 *
 * Additionally, this software is not monitored or scanned under our Cybersecurity Management Standard.
 *
 * No warranties, guarantees, or claims are offered at this SQML level.
 */

/*
 * Copyright (c) 2020,2022,2024 BlackBerry Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file may contain contributions from others, either as
 * contributors under the License or as licensors under other terms.
 * Please review this entire file for other proprietary rights or license
 * notices, as well as the QNX Development Suite License Guide at
 * http://licensing.qnx.com/license-guide/ for other information.
 * $
 */

#include <startup.h>

#define GICD_PADDR          (0x107fff9000)
#define GICC_PADDR          (0x107fffa000)

/* Pi 5 PCIe extension port supports 8 IRQs mapped to GICv2 interrupts 287~295 */
#define PCIE_EXT_IRQ_0      (287)
#define PCIE_EXT_IRQ_NUM    8

void init_intrinfo(void)
{
	int32_t i;

    /*
     * Initialise GIC
     */
     ///和硬件有关，bcm2712其内置的中断控制器版本是 GIC-400，
     ///它符合 ARM Generic Interrupt Controller (GIC) Architecture Specification v2.0 (GICv2)。
     /**
     由于 BCM2712 坚持使用 GICv2，开发者需要注意：
     - 寄存器访问：GICv2 主要通过 Memory-Mapped I/O (MMIO) 访问；
		而 GICv3 引入了系统寄存器访问方式（System Registers），效率更高。
	 - 中断数量：GICv2 最多支持 8 个 CPU 核心，这对于 BCM2712 的四核架构来说绰绰有余。
	 - MSI 支持：GICv2 原生不支持 MSI（Message Signaled Interrupts），在处理 PCIe 设备时，
		通常需要额外的 MSI 适配器（如 GIC-v2m）或使用传统的引脚中断。
     */
    gic_v2_init(GICD_PADDR, GICC_PADDR);

    /*
     * Set Pi 5 PCIe extension port IRQs to Edge-Triggered
     */
    for (i = 0; i < PCIE_EXT_IRQ_NUM; i++) {
        gic_v2_set_intr_trig_mode(PCIE_EXT_IRQ_0 + i, 1);
    }
}

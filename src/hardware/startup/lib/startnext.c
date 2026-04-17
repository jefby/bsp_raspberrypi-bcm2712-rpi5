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
 * $QNXLicenseC:
 * Copyright 2008, QNX Software Systems. 
 * 
 * Licensed under the Apache License, Version 2.0 (the "License"). You 
 * may not reproduce, modify or distribute this software except in 
 * compliance with the License. You may obtain a copy of the License 
 * at: http://www.apache.org/licenses/LICENSE-2.0 
 * 
 * Unless required by applicable law or agreed to in writing, software 
 * distributed under the License is distributed on an "AS IS" basis, 
 * WITHOUT WARRANTIES OF ANY KIND, either express or implied.
 *
 * This file may contain contributions from others, either as 
 * contributors under the License or as licensors under other terms.  
 * Please review this entire file for other proprietary rights or license 
 * notices, as well as the QNX Development Suite License Guide at 
 * http://licensing.qnx.com/license-guide/ for other information.
 * $
 */





#include <startup.h>
#include "hw/uefi.h"

void
startnext() {
	///其中 first_bootstrap_start_vaddr 是从 IFS（Image FileSystem）加载的 procnto 内核的入口虚拟地址。
	uintptr_t const eip = first_bootstrap_start_vaddr;

	if(debug_flag) {
		kprintf("\nSystem page at phys:%P user:%v kern:%v\n", (paddr_t)syspage_paddr,
			lsp.system_private.p->user_syspageptr, lsp.system_private.p->kern_syspageptr);
		kprintf("Starting next program at v%v\n", eip);
	}
	if(eip == ~(uintptr_t)0) {
		crash("No next program to start\n");
	}

	uefi_exit_boot_services();
	///cpu_startnext它将系统页（syspage）传递给内核（通过 x0 寄存器），
	///然后跳转到 procnto。内核接管后，启动程序的代码不再执行。
	///eip寄存器存储着我们cpu要读取指令的地址
	cpu_startnext(eip, 0);
}

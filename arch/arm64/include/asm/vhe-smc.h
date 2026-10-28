/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VHE_SMC_H
#define __ASM_VHE_SMC_H

/* Private conduit frame, shared by the assembly and failure reporter. */
#define SMC_HCR		256
#define SMC_VBAR2	264
#define SMC_ELR2		272
#define SMC_SPSR2	280
#define SMC_DAIF		288
#define SMC_NZCV		296
#define SMC_SCTLR1	304
#define SMC_VBAR1	312
#define SMC_SP1		320
#define SMC_ELR1		328
#define SMC_SPSR1	336
#define SMC_RETURN_PA	344
#define SMC_FAULT	352
#define SMC_ESR2		360
#define SMC_FAR2		368
#define SMC_HPFAR2	376
#define SMC_HOST_PSTATE	384

#define SMC_DIAG_ARGS	400
#define SMC_DIAG_PHASE	464
#define SMC_DIAG_REASON	472
#define SMC_DIAG_VECTOR	480
#define SMC_DIAG_ESR2	488
#define SMC_DIAG_ELR2	496
#define SMC_DIAG_SPSR2	504
#define SMC_DIAG_FAR2	512
#define SMC_DIAG_HPFAR2	520
#define SMC_DIAG_HCR	528
#define SMC_DIAG_DAIF	536
#define SMC_DIAG_SP1	544
#define SMC_DIAG_ESR1	552
#define SMC_DIAG_ELR1	560
#define SMC_DIAG_SPSR1	568
#define SMC_DIAG_FAR1	576
#define SMC_DIAG_SCTLR1	584
#define SMC_DIAG_VBAR1	592
#define SMC_DIAG_CURRENTEL 600
#define SMC_FRAME_SIZE	608

#define SMC_PHASE_CAPTURE	1
#define SMC_PHASE_EL1		2
#define SMC_PHASE_RETURN		3

#define SMC_BAD_CAPTURE_MODE	1
#define SMC_BAD_CAPTURE_ESR	2
#define SMC_BAD_RETURN_ESR	3
#define SMC_BAD_RETURN_ELR	4
#define SMC_BAD_RETURN_MODE	5
#define SMC_BAD_VECTOR		6

#define SMC_CAPTURE_HVC		0x4d52
#define SMC_RETURN_HVC		0x4d53
#define SMC_FAULT_HVC_BASE	0x4d60

#ifndef __ASSEMBLY__
#include <linux/types.h>
void __exynos_vhe_smc_report(const u64 *frame);
#endif

#endif

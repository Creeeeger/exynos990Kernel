/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SMC_H
#define __ASM_SMC_H

#if defined(CONFIG_ARM64_VHE) && defined(CONFIG_SOC_EXYNOS9830)
#define ARM64_EXYNOS_VHE_SMC
#endif

#ifdef __ASSEMBLY__

	.macro arm64_smc
#ifdef ARM64_EXYNOS_VHE_SMC
	stp	xzr, x30, [sp, #-16]!
	bl	__exynos_vhe_smc_el1
	ldp	xzr, x30, [sp], #16
#else
	smc	#0
#endif
	.endm

/* The vector-copy code rebases this direct BL. An indirect call here would
 * run before the branch-predictor firmware mitigation has completed.
 */
	.macro arm64_smc_hyp
#ifdef ARM64_EXYNOS_VHE_SMC
	stp	x16, x30, [sp, #-16]!
	mrs	x16, hcr_el2
	tbz	x16, #34, .Lsmc_raw\@
	bl	__exynos_vhe_smc_el1
	b	.Lsmc_done\@
.Lsmc_raw\@:
	smc	#0
.Lsmc_done\@:
	ldp	x16, x30, [sp], #16
#else
	smc	#0
#endif
	.endm

#else /* __ASSEMBLY__ */

#ifdef ARM64_EXYNOS_VHE_SMC
extern void __exynos_vhe_smc_el1(void);
#define ARM64_SMC_INST \
	"stp xzr, x30, [sp, #-16]!\n\t" \
	"bl __exynos_vhe_smc_el1\n\t" \
	"ldp xzr, x30, [sp], #16\n\t"
#else
#define ARM64_SMC_INST "smc #0\n\t"
#endif

#endif /* __ASSEMBLY__ */
#endif /* __ASM_SMC_H */

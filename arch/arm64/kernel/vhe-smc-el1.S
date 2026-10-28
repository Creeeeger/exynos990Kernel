/* SPDX-License-Identifier: GPL-2.0 */
/* Samsung's monitor requires an EL1 caller. Never change the monitor's policy.
 * The EL1 payload executes from its physical address with translation off;
 * it neither borrows a guest's mappings nor accesses the host stack.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/asm-bug.h>
#include <asm/kvm_arm.h>
#include <asm/ptrace.h>
#include <asm/sysreg.h>

#include <asm/vhe-smc.h>

	.macro smc_save_results
	stp	x0, x1, [sp, #0]
	stp	x2, x3, [sp, #16]
	stp	x4, x5, [sp, #32]
	stp	x6, x7, [sp, #48]
	stp	x8, x9, [sp, #64]
	stp	x10, x11, [sp, #80]
	stp	x12, x13, [sp, #96]
	stp	x14, x15, [sp, #112]
	stp	x16, x17, [sp, #128]
	.endm

	.macro smc_load_results
	ldp	x0, x1, [sp, #0]
	ldp	x2, x3, [sp, #16]
	ldp	x4, x5, [sp, #32]
	ldp	x6, x7, [sp, #48]
	ldp	x8, x9, [sp, #64]
	ldp	x10, x11, [sp, #80]
	ldp	x12, x13, [sp, #96]
	ldp	x14, x15, [sp, #112]
	ldp	x16, x17, [sp, #128]
	.endm

	.text
ENTRY(__exynos_vhe_smc_el1)
	sub	sp, sp, #SMC_FRAME_SIZE
	smc_save_results
	stp	x18, x19, [sp, #144]
	stp	x20, x21, [sp, #160]
	stp	x22, x23, [sp, #176]
	stp	x24, x25, [sp, #192]
	stp	x26, x27, [sp, #208]
	stp	x28, x29, [sp, #224]
	str	x30, [sp, #240]
	mrs	x16, nzcv
	str	x16, [sp, #SMC_NZCV]
	mrs	x16, daif
	str	x16, [sp, #SMC_DAIF]
	mrs	x16, CurrentEL
	cmp	x16, #CurrentEL_EL2
	b.ne	.Lsmc_direct
	mrs	x16, hcr_el2
	tbz	x16, #34, .Lsmc_direct
	msr	daifset, #0xf
	str	x16, [sp, #SMC_HCR]
	mrs	x16, vbar_el2
	str	x16, [sp, #SMC_VBAR2]
	mrs	x16, elr_el2
	str	x16, [sp, #SMC_ELR2]
	mrs	x16, spsr_el2
	str	x16, [sp, #SMC_SPSR2]
	mrs	x16, esr_el2
	str	x16, [sp, #SMC_ESR2]
	mrs	x16, far_el2
	str	x16, [sp, #SMC_FAR2]
	mrs	x16, hpfar_el2
	str	x16, [sp, #SMC_HPFAR2]
	/* EL12 aliases access guest EL1 state without VHE redirection. */
	mrs_s	x16, sys_reg(3, 5, 1, 0, 0)
	str	x16, [sp, #SMC_SCTLR1]
	mrs_s	x16, sys_reg(3, 5, 12, 0, 0)
	str	x16, [sp, #SMC_VBAR1]
	mrs	x16, sp_el1
	str	x16, [sp, #SMC_SP1]
	mrs_s	x16, sys_reg(3, 5, 4, 0, 1)
	str	x16, [sp, #SMC_ELR1]
	mrs_s	x16, sys_reg(3, 5, 4, 0, 0)
	str	x16, [sp, #SMC_SPSR1]
	str	xzr, [sp, #SMC_FAULT]
	str	xzr, [sp, #SMC_HOST_PSTATE]
	ldp	x16, x17, [sp, #0]
	stp	x16, x17, [sp, #SMC_DIAG_ARGS]
	ldp	x16, x17, [sp, #16]
	stp	x16, x17, [sp, #SMC_DIAG_ARGS + 16]
	ldp	x16, x17, [sp, #32]
	stp	x16, x17, [sp, #SMC_DIAG_ARGS + 32]
	ldp	x16, x17, [sp, #48]
	stp	x16, x17, [sp, #SMC_DIAG_ARGS + 48]
	mov	x16, #SMC_PHASE_CAPTURE
	str	x16, [sp, #SMC_DIAG_PHASE]
	str	xzr, [sp, #SMC_DIAG_REASON]

	ldr_l	x16, kimage_voffset
	adr_l	x17, .Lsmc_el1_payload
	sub	x17, x17, x16
	msr	elr_el2, x17
	add	x17, x17, #8
	str	x17, [sp, #SMC_RETURN_PA]
	adr_l	x17, .Lsmc_el1_vectors
	sub	x17, x17, x16
	msr_s	sys_reg(3, 5, 12, 0, 0), x17
	/* No EL1 stack is used, including on an unexpected EL1 exception. */
	msr	sp_el1, xzr
	mov_q	x17, SCTLR_EL1_RES1
	msr_s	sys_reg(3, 5, 1, 0, 0), x17
	adr_l	x17, .Lsmc_el2_vectors
	msr	vbar_el2, x17
	isb
	/* Capture the complete host PSTATE, including PAN/UAO/SSBS. Reading
	 * individual optional status registers would miss future extensions.
	 */
	hvc	#SMC_CAPTURE_HVC
.Lsmc_capture:
	mrs	x16, spsr_el2
	str	x16, [sp, #SMC_HOST_PSTATE]
	and	x16, x16, #0x1f
	cmp	x16, #PSR_MODE_EL2h
	b.ne	.Lsmc_bad_capture_mode
	mrs	x16, esr_el2
	mov_q	x17, ((0x16 << 26) | (1 << 25) | SMC_CAPTURE_HVC)
	cmp	x16, x17
	b.ne	.Lsmc_bad_capture_esr
	ldr	x16, [sp, #SMC_HOST_PSTATE]
	mov_q	x17, 0xf00003c0
	bic	x16, x16, x17
	ldr	x17, [sp, #SMC_NZCV]
	orr	x16, x16, x17
	ldr	x17, [sp, #SMC_DAIF]
	orr	x16, x16, x17
	str	x16, [sp, #SMC_HOST_PSTATE]
	ldr	x17, [sp, #SMC_RETURN_PA]
	sub	x17, x17, #8
	msr	elr_el2, x17
	mov	x17, #(PSR_MODE_EL1h | PSR_D_BIT | PSR_A_BIT | PSR_I_BIT | PSR_F_BIT)
	msr	spsr_el2, x17
	/* Keep EL2's host translations and permit the physical EL1 payload.
	 * Leave asynchronous exceptions at EL1, where its DAIF masks apply.
	 * Routing them to EL2 bypasses those masks while executing at EL1.
	 * They remain pending until the original host state is restored.
	 */
	mov_q	x17, (HCR_E2H | HCR_RW)
	dsb	sy
	msr	hcr_el2, x17
	isb
	mov	x16, #SMC_PHASE_EL1
	str	x16, [sp, #SMC_DIAG_PHASE]
	smc_load_results
	eret

.Lsmc_direct:
	smc_load_results
	smc	#0
	smc_save_results
	b	.Lsmc_restore_gprs

.Lsmc_el2_return:
	smc_save_results
	mov	x16, #SMC_PHASE_RETURN
	str	x16, [sp, #SMC_DIAG_PHASE]
	mrs	x16, esr_el2
	mov_q	x17, ((0x16 << 26) | (1 << 25) | SMC_RETURN_HVC)
	cmp	x16, x17
	b.ne	.Lsmc_bad_return_esr
	mrs	x16, elr_el2
	ldr	x17, [sp, #SMC_RETURN_PA]
	cmp	x16, x17
	b.ne	.Lsmc_bad_return_elr
	mrs	x16, spsr_el2
	and	x16, x16, #0x1f
	cmp	x16, #PSR_MODE_EL1h
	b.eq	.Lsmc_restore_state
	mov	x16, #SMC_BAD_RETURN_MODE
	b	.Lsmc_bad_return
.Lsmc_bad_capture_mode:
	mov	x16, #SMC_BAD_CAPTURE_MODE
	b	.Lsmc_bad_return
.Lsmc_bad_capture_esr:
	mov	x16, #SMC_BAD_CAPTURE_ESR
	b	.Lsmc_bad_return
.Lsmc_bad_return_esr:
	mov	x16, #SMC_BAD_RETURN_ESR
	b	.Lsmc_bad_return
.Lsmc_bad_return_elr:
	mov	x16, #SMC_BAD_RETURN_ELR
.Lsmc_bad_return:
	str	x16, [sp, #SMC_DIAG_REASON]
	/* Snapshot the original exception before restoring any host metadata. */
	mrs	x16, esr_el2
	str	x16, [sp, #SMC_DIAG_ESR2]
	mrs	x16, elr_el2
	str	x16, [sp, #SMC_DIAG_ELR2]
	mrs	x16, spsr_el2
	str	x16, [sp, #SMC_DIAG_SPSR2]
	mrs	x16, far_el2
	str	x16, [sp, #SMC_DIAG_FAR2]
	mrs	x16, hpfar_el2
	str	x16, [sp, #SMC_DIAG_HPFAR2]
	mrs	x16, hcr_el2
	str	x16, [sp, #SMC_DIAG_HCR]
	mrs	x16, daif
	str	x16, [sp, #SMC_DIAG_DAIF]
	mrs	x16, CurrentEL
	str	x16, [sp, #SMC_DIAG_CURRENTEL]
	mrs	x16, sp_el1
	str	x16, [sp, #SMC_DIAG_SP1]
	mrs_s	x16, sys_reg(3, 5, 5, 2, 0)
	str	x16, [sp, #SMC_DIAG_ESR1]
	mrs_s	x16, sys_reg(3, 5, 4, 0, 1)
	str	x16, [sp, #SMC_DIAG_ELR1]
	mrs_s	x16, sys_reg(3, 5, 4, 0, 0)
	str	x16, [sp, #SMC_DIAG_SPSR1]
	mrs_s	x16, sys_reg(3, 5, 6, 0, 0)
	str	x16, [sp, #SMC_DIAG_FAR1]
	mrs_s	x16, sys_reg(3, 5, 1, 0, 0)
	str	x16, [sp, #SMC_DIAG_SCTLR1]
	mrs_s	x16, sys_reg(3, 5, 12, 0, 0)
	str	x16, [sp, #SMC_DIAG_VBAR1]
	mov	x16, #1
	str	x16, [sp, #SMC_FAULT]
	b	.Lsmc_restore_state
.Lsmc_el2_fault:
	smc_save_results
	mov	x16, #SMC_BAD_VECTOR
	b	.Lsmc_bad_return

.Lsmc_restore_state:
	ldr	x16, [sp, #SMC_VBAR1]
	msr_s	sys_reg(3, 5, 12, 0, 0), x16
	ldr	x16, [sp, #SMC_SP1]
	msr	sp_el1, x16
	ldr	x16, [sp, #SMC_ELR1]
	msr_s	sys_reg(3, 5, 4, 0, 1), x16
	ldr	x16, [sp, #SMC_SPSR1]
	msr_s	sys_reg(3, 5, 4, 0, 0), x16
	ldr	x16, [sp, #SMC_SCTLR1]
	msr_s	sys_reg(3, 5, 1, 0, 0), x16
	dsb	sy
	ldr	x16, [sp, #SMC_HCR]
	msr	hcr_el2, x16
	isb
	ldr	x16, [sp, #SMC_VBAR2]
	msr	vbar_el2, x16
	ldr	x16, [sp, #SMC_ELR2]
	msr	elr_el2, x16
	ldr	x16, [sp, #SMC_SPSR2]
	msr	spsr_el2, x16
	ldr	x16, [sp, #SMC_ESR2]
	msr	esr_el2, x16
	ldr	x16, [sp, #SMC_FAR2]
	msr	far_el2, x16
	ldr	x16, [sp, #SMC_HPFAR2]
	msr	hpfar_el2, x16
	isb
	ldr	x16, [sp, #SMC_FAULT]
	cbnz	x16, .Lsmc_fault
	/* ERET restores the complete original PSTATE. Restore the caller's
	 * exception metadata again afterwards, without changing any flags.
	 */
	ldr	x16, [sp, #SMC_HOST_PSTATE]
	msr	spsr_el2, x16
	adr_l	x16, .Lsmc_return_at_el2
	msr	elr_el2, x16
	eret
.Lsmc_return_at_el2:
	ldr	x16, [sp, #SMC_ELR2]
	msr	elr_el2, x16
	ldr	x16, [sp, #SMC_SPSR2]
	msr	spsr_el2, x16
	ldr	x16, [sp, #SMC_ESR2]
	msr	esr_el2, x16
	ldr	x16, [sp, #SMC_FAR2]
	msr	far_el2, x16
	ldr	x16, [sp, #SMC_HPFAR2]
	msr	hpfar_el2, x16
.Lsmc_restore_gprs:
	ldp	x18, x19, [sp, #144]
	ldp	x20, x21, [sp, #160]
	ldp	x22, x23, [sp, #176]
	ldp	x24, x25, [sp, #192]
	ldp	x26, x27, [sp, #208]
	ldp	x28, x29, [sp, #224]
	ldr	x30, [sp, #240]
	ldr	x16, [sp, #SMC_NZCV]
	msr	nzcv, x16
	ldr	x16, [sp, #SMC_DAIF]
	msr	daif, x16
	smc_load_results
	add	sp, sp, #SMC_FRAME_SIZE
	ret
.Lsmc_fault:
	/* The reporter uses the saved fault, not the restored live registers. */
	ldp	x18, x19, [sp, #144]
	ldp	x20, x21, [sp, #160]
	ldp	x22, x23, [sp, #176]
	ldp	x24, x25, [sp, #192]
	ldp	x26, x27, [sp, #208]
	ldp	x28, x29, [sp, #224]
	mov	x0, sp
	bl	__exynos_vhe_smc_report
	ASM_BUG()
ENDPROC(__exynos_vhe_smc_el1)
	NOKPROBE(__exynos_vhe_smc_el1)

	/* All branches within the EL1 payload are position independent. */
	.align 11
.Lsmc_el1_vectors:
	.set .Lsmc_el1_index, 0
	.rept 16
	hvc	#(SMC_FAULT_HVC_BASE + .Lsmc_el1_index)
	b	.
	.balign 128
	.set .Lsmc_el1_index, .Lsmc_el1_index + 1
	.endr
.Lsmc_el1_payload:
	smc	#0
	hvc	#SMC_RETURN_HVC
	b	.

	/* Preserve all exception registers while identifying the vector slot. */
	.macro smc_el2_vector index, target
	stp	x16, x17, [sp, #-16]!
	mov	x16, #\index
	str	x16, [sp, #SMC_DIAG_VECTOR + 16]
	ldp	x16, x17, [sp], #16
	b	\target
	.balign 128
	.endm

	.align 11
.Lsmc_el2_vectors:
	smc_el2_vector 0, .Lsmc_el2_fault
	smc_el2_vector 1, .Lsmc_el2_fault
	smc_el2_vector 2, .Lsmc_el2_fault
	smc_el2_vector 3, .Lsmc_el2_fault
	smc_el2_vector 4, .Lsmc_capture
	smc_el2_vector 5, .Lsmc_el2_fault
	smc_el2_vector 6, .Lsmc_el2_fault
	smc_el2_vector 7, .Lsmc_el2_fault
	smc_el2_vector 8, .Lsmc_el2_return
	smc_el2_vector 9, .Lsmc_el2_fault
	smc_el2_vector 10, .Lsmc_el2_fault
	smc_el2_vector 11, .Lsmc_el2_fault
	smc_el2_vector 12, .Lsmc_el2_fault
	smc_el2_vector 13, .Lsmc_el2_fault
	smc_el2_vector 14, .Lsmc_el2_fault
	smc_el2_vector 15, .Lsmc_el2_fault

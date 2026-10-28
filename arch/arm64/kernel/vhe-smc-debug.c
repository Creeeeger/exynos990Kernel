// SPDX-License-Identifier: GPL-2.0
/* Report saved conduit failures after the original host state is restored. */
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/percpu.h>
#include <asm/vhe-smc.h>

static DEFINE_PER_CPU(bool, smc_failure_reported);

static __always_inline u64 smc_frame(const u64 *frame, unsigned int offset)
{
	return frame[offset / sizeof(*frame)];
}

void notrace __exynos_vhe_smc_report(const u64 *frame)
{
	static const char * const reasons[] = {
		"unset", "capture mode", "capture ESR", "return ESR",
		"return ELR", "return mode", "unexpected EL2 vector",
	};
	u64 reason = smc_frame(frame, SMC_DIAG_REASON);
	u64 vector = smc_frame(frame, SMC_DIAG_VECTOR);
	u64 esr = smc_frame(frame, SMC_DIAG_ESR2);
	u64 el1_vector = esr & 0xffff;

	/* Panic notifiers can themselves issue SMCs. Keep the first report
	 * on each CPU, without re-entering printk through another failure.
	 */
	if (__this_cpu_read(smc_failure_reported))
		return;
	__this_cpu_write(smc_failure_reported, true);
	pr_emerg("VHE-SMC failure: cpu=%u phase=%llu reason=%llu (%s) vector=%llu caller=%pS\n",
		raw_smp_processor_id(), smc_frame(frame, SMC_DIAG_PHASE), reason,
		reason < ARRAY_SIZE(reasons) ? reasons[reason] : "unknown",
		vector, (void *)frame[30]);
	pr_emerg("VHE-SMC request: x0=%016llx x1=%016llx x2=%016llx x3=%016llx\n",
		smc_frame(frame, SMC_DIAG_ARGS), smc_frame(frame, SMC_DIAG_ARGS + 8),
		smc_frame(frame, SMC_DIAG_ARGS + 16), smc_frame(frame, SMC_DIAG_ARGS + 24));
	pr_emerg("VHE-SMC request: x4=%016llx x5=%016llx x6=%016llx x7=%016llx\n",
		smc_frame(frame, SMC_DIAG_ARGS + 32), smc_frame(frame, SMC_DIAG_ARGS + 40),
		smc_frame(frame, SMC_DIAG_ARGS + 48), smc_frame(frame, SMC_DIAG_ARGS + 56));
	pr_emerg("VHE-SMC raw EL2: ESR=%016llx EC=%02llx ELR=%016llx SPSR=%016llx\n",
		esr, (esr >> 26) & 0x3f, smc_frame(frame, SMC_DIAG_ELR2),
		smc_frame(frame, SMC_DIAG_SPSR2));
	pr_emerg("VHE-SMC raw EL2: FAR=%016llx HPFAR=%016llx HCR=%016llx DAIF=%016llx CurrentEL=%llx\n",
		smc_frame(frame, SMC_DIAG_FAR2), smc_frame(frame, SMC_DIAG_HPFAR2),
		smc_frame(frame, SMC_DIAG_HCR), smc_frame(frame, SMC_DIAG_DAIF),
		smc_frame(frame, SMC_DIAG_CURRENTEL));
	pr_emerg("VHE-SMC raw EL1: ESR=%016llx ELR=%016llx SPSR=%016llx FAR=%016llx\n",
		smc_frame(frame, SMC_DIAG_ESR1), smc_frame(frame, SMC_DIAG_ELR1),
		smc_frame(frame, SMC_DIAG_SPSR1), smc_frame(frame, SMC_DIAG_FAR1));
	pr_emerg("VHE-SMC raw EL1: SCTLR=%016llx VBAR=%016llx SP=%016llx\n",
		smc_frame(frame, SMC_DIAG_SCTLR1), smc_frame(frame, SMC_DIAG_VBAR1),
		smc_frame(frame, SMC_DIAG_SP1));
	pr_emerg("VHE-SMC expected: return-pa=%016llx host-pstate=%016llx saved-hcr=%016llx saved-vbar=%016llx\n",
		smc_frame(frame, SMC_RETURN_PA), smc_frame(frame, SMC_HOST_PSTATE),
		smc_frame(frame, SMC_HCR), smc_frame(frame, SMC_VBAR2));
	pr_emerg("VHE-SMC result: x0=%016llx x1=%016llx x2=%016llx x3=%016llx\n",
		frame[0], frame[1], frame[2], frame[3]);
	if (((esr >> 26) & 0x3f) == 0x16 &&
	    el1_vector >= SMC_FAULT_HVC_BASE && el1_vector < SMC_FAULT_HVC_BASE + 16)
		pr_emerg("VHE-SMC original EL1 exception vector=%llu\n",
			el1_vector - SMC_FAULT_HVC_BASE);
}
NOKPROBE_SYMBOL(__exynos_vhe_smc_report);

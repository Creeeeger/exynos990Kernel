Samsung security integration
============================

The exynos9830 production configuration enables Samsung UH/FASTUH RKP, KDP,
JOPP, FIVE, PROCA/GAF, DEFEX, DSMS, HDM, MST, Knox networking and SDP/DDAR.
Storage encryption uses the newer block inline encryption and Exynos FMP
implementation, including dm-default-key; Samsung Diskcipher is not restored.
FMP and kernel crypto FIPS integrity passes run after JOPP instrumentation.
The ARM64 BPF JIT adds the same JOPP entry marker to generated programs before
making their memory read-only, preserving Android networking under CFP.

Build with LLVM 18 or newer and the signed policies for the installed ROM::

    KERNEL_TOOLCHAIN_BIN=/usr/lib/llvm-18/bin JOBS=24 bash build.sh \
        --model y2s --ksu y --recovery n \
        --dpolicy /path/to/vendor/etc/dpolicy \
        --dpolicy-system /path/to/system/dpolicy_system

The build verifies policy signatures and checks the resolved kernel config.
Outputs include the kernel config and SHA256SUMS alongside boot.img and the
flashable archive. A successful compile is not a device boot test: Samsung
hypervisor/TEE firmware and the ROM policies must match the device. Enabling
kernel encryption support does not itself encrypt existing userdata; Android's
fstab and vold configuration control use of that support.

DEFEX enforcement remains active on unlocked boots. Its integrity, immutable
and trusted-map features enforce their policies instead of running permissively.
The manager authenticated by KernelSU, KernelSU-allowlisted UIDs and tasks in
the KernelSU SELinux domain receive an explicit DEFEX exception. Neither UID 0
alone nor a filename grants an exception. Temporary KernelSU credential
overrides use per-task KDP copies, and SELinux enforcement updates use the
Samsung protected update path. KernelSU remains built in and uses manual hooks;
kprobes remain disabled to avoid patching RKP-protected kernel text.

KernelSU-Next is vendored as ordinary source in this kernel repository,
including the Samsung integration changes and upstream license. Normal clones
contain all required KSU files; no submodule initialization or KSU fork is
needed. The drivers/kernelsu symlink points to KernelSU/kernel. The distinct
KernelSU directory lets Git switch normally to older branches that still
use the KernelSU-Next submodule; any retained submodule checkout is ignored
and is not used by this branch.
KernelSU/UPSTREAM records the source revisions, and kernel/version.mk
inside that directory pins version 33321 and tag v3.4.0-legacy independently
of the parent kernel's Git history or the availability of Git metadata.

The KernelSU base is upstream legacy revision 0b17f24246db (2026-10-17),
whose nearest release tag is v3.4.0-legacy and which includes subsequent
legacy fixes and 3.4.2 backports. Samsung compatibility patches are applied
on top. The optional upstream preinit runner is unavailable with manual
hooks: it requires syscall-table interception, which this RKP integration
does not enable. KPERFMON uses its existing logging stub in standalone
checkouts without Samsung's Android logging headers and generator.

Alternative LSMs, debug/test modes and obsolete encryption backends are not
enabled indiscriminately. This platform has no TPM trusted-key backend. FIVE's
legacy TEE/ICD/TZIC drivers are excluded by the selected PROCA S-OS backend.
The imported MZ sources require unavailable vendor-hook and migration APIs,
and Kumiho contains only a Kconfig declaration; neither was connected to the
imported Samsung build. They are not represented as working protections.

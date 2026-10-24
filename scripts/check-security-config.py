#!/usr/bin/env python3
"""Reject builds where Kconfig dropped a required production protection."""

from pathlib import Path
import argparse
import re

REQUIRED = """
AUDIT SECURITY SECURITY_SELINUX SECURITY_SELINUX_ALWAYS_ENFORCE
UH FASTUH_RKP UH_RKP_FIMC_CHECK KDP_CRED KDP_NS KDP_DMAP UH_LKM_BLOCK
UH_PDBT RKP_CFP RKP_CFP_JOPP
FIVE FIVE_TRUSTED_KEYRING PROCA PROCA_S_OS GAF GAF_V6 SECURITY_DSMS
SECURITY_DEFEX HDM MST_LDO MST_TEEGRIS KNOX_NCM NETPM SDP DDAR FSCRYPT_SDP
TZDEV SENSORS_FPRINT_SECURE SPU_VERIFY
CRYPTO_FIPS EXYRNG_FIPS_COMPLIANCE EXYNOS_FMP EXYNOS_FMP_FIPS
FS_ENCRYPTION FS_ENCRYPTION_INLINE_CRYPT EXT4_FS_ENCRYPTION
F2FS_FS_ENCRYPTION BLK_INLINE_ENCRYPTION SCSI_UFS_CRYPTO DM_CRYPT
DM_DEFAULT_KEY ECRYPT_FS ECRYPTFS_FEK_INTEGRITY DM_VERITY FS_VERITY
SECCOMP SECCOMP_FILTER RANDOMIZE_BASE ARM64_PAN STACKPROTECTOR_STRONG
STRICT_KERNEL_RWX STRICT_MODULE_RWX HARDENED_USERCOPY
SLAB_FREELIST_RANDOM SLAB_FREELIST_HARDENED CFI_CLANG SHADOW_CALL_STACK
INIT_STACK_ALL_ZERO INIT_ON_ALLOC_DEFAULT_ON INIT_ON_FREE_DEFAULT_ON
MODULE_SIG MODULE_SIG_FORCE ARM64_SSBD HARDEN_BRANCH_PREDICTOR
MITIGATE_SPECTRE_BRANCH_HISTORY UNMAP_KERNEL_AT_EL0
SECURITY_DMESG_RESTRICT SECURITY_PERF_EVENTS_RESTRICT SECURITYFS
SECURITY_NETWORK_XFRM SECURITY_PATH SECURITY_YAMA IMA IMA_APPRAISE EVM
DM_INTEGRITY SECONDARY_TRUSTED_KEYRING SYSTEM_BLACKLIST_KEYRING
BPF BPF_SYSCALL CGROUP_BPF BPF_UNPRIV_DEFAULT_OFF
""".split()
FORBIDDEN = """
CRYPTO_DISKCIPHER SECURITY_SELINUX_ALWAYS_PERMISSIVE CFI_PERMISSIVE
CRYPTO_MANAGER_DISABLE_TESTS HARDENED_USERCOPY_FALLBACK INIT_STACK_NONE
KPROBES
""".split()


def check(path, requested=None):
    values = dict(re.findall(r"^CONFIG_(\w+)=(.*)$", path.read_text(), re.M))
    wanted = None if requested is None else dict(re.findall(
        r"^CONFIG_(\w+)=(.*)$", requested.read_text(), re.M))
    required = REQUIRED if wanted is None else [name for name in REQUIRED if wanted.get(name) == "y"]
    forbidden = FORBIDDEN if wanted is None else [name for name in FORBIDDEN if wanted.get(name) not in ("y", "m")]
    errors = [f"CONFIG_{name} must be y" for name in required if values.get(name) != "y"]
    errors += [f"CONFIG_{name} must be disabled" for name in forbidden if values.get(name) in ("y", "m")]
    if values.get("UH_RKP") == "y" or values.get("FASTUH_RKP") == "y":
        if values.get("BPF_JIT_ALWAYS_ON") == "y":
            errors.append("CONFIG_BPF_JIT_ALWAYS_ON prevents the interpreter fallback required by RKP")
    if values.get("KSU") == "y":
        ksu_required = ["KSU_MANUAL_HOOK"]
        if wanted is None or values.get("UH") == "y":
            ksu_required.append("KSU_SAMSUNG_SECURITY")
        if values.get("FASTUH_RKP") == "y":
            ksu_required.append("KSU_RKP_SELINUX_HOOKS")
        for name in ksu_required:
            if values.get(name) != "y":
                errors.append(f"CONFIG_{name} is required with KernelSU")
        for name in ("KSU_KPROBES_HOOK", "KSU_SYSCALL_TABLE_HOOK",
                     "KSU_DISABLE_MANAGER", "KSU_DISABLE_POLICY"):
            if values.get(name) == "y":
                errors.append(f"CONFIG_{name} is incompatible with the Samsung KernelSU integration")
    return errors


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", nargs="?", type=Path, default=Path("out/.config"))
    parser.add_argument("--defconfig", type=Path,
                        help="Validate the protections explicitly requested by this test profile")
    args = parser.parse_args()
    errors = check(args.config, args.defconfig)
    if errors:
        raise SystemExit("Security configuration check failed:\n" + "\n".join(errors))
    print(f"Verified requested security settings: {args.config}")

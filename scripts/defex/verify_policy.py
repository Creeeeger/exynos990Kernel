#!/usr/bin/env python3
"""Verify a DEFEX policy with the same double-SHA256 RSA scheme as the kernel."""

import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile


def verify(policy, certificate):
    data = policy.read_bytes()
    if not 256 < len(data) <= 256 * 1024:
        raise ValueError("invalid DEFEX policy size")
    if data[39:55] != b"DEFEX_RULES_FILE":
        raise ValueError("policy does not contain an integrity-enabled DEFEX tree")
    with tempfile.TemporaryDirectory(prefix="defex-verify-") as tmp:
        tmp = Path(tmp)
        public_key = subprocess.check_output([
            "openssl", "x509", "-inform", "DER", "-in", str(certificate),
            "-pubkey", "-noout",
        ])
        (tmp / "public.pem").write_bytes(public_key)
        (tmp / "signature").write_bytes(data[-256:])
        (tmp / "digest").write_bytes(hashlib.sha256(data[:-256]).digest())
        subprocess.run([
            "openssl", "dgst", "-sha256", "-verify", str(tmp / "public.pem"),
            "-signature", str(tmp / "signature"), str(tmp / "digest"),
        ], check=True, capture_output=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("policy", type=Path)
    parser.add_argument("--certificate", type=Path, default=Path(__file__).resolve().parents[2] /
                        "security/samsung/defex_lsm/cert/x509_five_user.der")
    args = parser.parse_args()
    try:
        verify(args.policy, args.certificate)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"DEFEX policy verification failed: {error}\n")
    print(f"DEFEX policy signature verified: {args.policy}")

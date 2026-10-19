/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_NEON_INTRINSICS_H
#define __ASM_NEON_INTRINSICS_H

/* Clang uses long for its 64-bit vector scalars; the kernel uses long long. */
#include <linux/types.h>
#ifndef __GENKSYMS__
#define int64_t __neon_int64_t
#define uint64_t __neon_uint64_t
#include <arm_neon.h>
#undef int64_t
#undef uint64_t
#else
typedef unsigned long __neon_uint64_t;
typedef unsigned long uint64x2_t __attribute__((vector_size(16)));
#endif

#endif

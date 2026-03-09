/*
 * PPC Tiger Compatibility Header
 * Provides missing definitions for Mac OS X 10.4 Tiger on PowerPC
 *
 * Include this via: -include ppc_tiger_compat.h
 * in all GCC invocations on Tiger
 *
 * Opus 4.6
 */

#ifndef PPC_TIGER_COMPAT_H
#define PPC_TIGER_COMPAT_H

/* Tiger is Darwin 8.x */
#ifndef __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__
#define __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ 1040
#endif

/* PPC is big-endian */
#ifndef __BIG_ENDIAN__
#define __BIG_ENDIAN__ 1
#endif
#define BYTE_ORDER BIG_ENDIAN

/* stdint types that may be missing on Tiger's GCC 4.0.1 */
#include <stdint.h>
#include <sys/types.h>

/* Atomics - Tiger doesn't have <stdatomic.h> or OSAtomic on PPC */
#ifndef __STDC_NO_ATOMICS__
#define __STDC_NO_ATOMICS__ 1
#endif

/* Use GCC builtins for atomics if available (GCC 4.0.1 has some) */
#ifndef __sync_val_compare_and_swap
/* GCC 4.0.1 has __sync builtins for PPC */
#endif

/* Thread-local storage - Tiger's GCC 4.0.1 supports __thread on PPC */
#ifndef thread_local
#define thread_local __thread
#endif

/* Missing POSIX definitions on Tiger */
#ifndef _POSIX_TIMERS
#define _POSIX_TIMERS (-1)  /* Not supported on Tiger */
#endif

/* Tiger doesn't have clock_gettime - use gettimeofday wrapper */
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

#include <sys/time.h>
#include <time.h>

static inline int clock_gettime(int clk_id, struct timespec *tp) {
    struct timeval tv;
    (void)clk_id;
    gettimeofday(&tv, NULL);
    tp->tv_sec = tv.tv_sec;
    tp->tv_nsec = tv.tv_usec * 1000;
    return 0;
}
#endif

/* AltiVec / SIMD - always available on G4/G5 */
#ifdef __ALTIVEC__
#include <altivec.h>
#endif

/* Endian conversion macros */
#include <machine/endian.h>

#ifndef htobe16
#define htobe16(x) (x)  /* No-op on big-endian */
#define htobe32(x) (x)
#define htobe64(x) (x)
#define htole16(x) __builtin_bswap16(x)
#define htole32(x) __builtin_bswap32(x)
#define htole64(x) __builtin_bswap64(x)
#define be16toh(x) (x)
#define be32toh(x) (x)
#define be64toh(x) (x)
#define le16toh(x) __builtin_bswap16(x)
#define le32toh(x) __builtin_bswap32(x)
#define le64toh(x) __builtin_bswap64(x)
#endif

/* GCC 4.0.1 may not have __builtin_bswap16 */
#ifndef __builtin_bswap16
static inline uint16_t __ppc_bswap16(uint16_t x) {
    return (x >> 8) | (x << 8);
}
#define __builtin_bswap16 __ppc_bswap16
#endif

/* Missing from Tiger: strnlen */
#include <string.h>
#ifndef strnlen
static inline size_t ppc_strnlen(const char *s, size_t maxlen) {
    size_t len = 0;
    while (len < maxlen && s[len]) len++;
    return len;
}
#define strnlen ppc_strnlen
#endif

/* Rust type mappings - ensure these match what rustc_ppc generates */
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float    f32;
typedef double   f64;
typedef size_t   usize;
typedef ssize_t  isize;

#endif /* PPC_TIGER_COMPAT_H */

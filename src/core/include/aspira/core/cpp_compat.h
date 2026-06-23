/**
 * @file cpp_compat.h
 * @brief C++ compatibility mappings for C11 atomic types.
 *        Include this BEFORE any C headers that use stdatomic.h types.
 */
#ifndef ASPIRA_CPP_COMPAT_H
#define ASPIRA_CPP_COMPAT_H

#ifdef __cplusplus

#include <atomic>
#include <cstdint>

/* Map C11 atomic types to C++ std::atomic equivalents.
   This MUST match the ABI layout of the C types. */
#define atomic_bool               std::atomic<bool>
#define atomic_int                std::atomic<int>
#define atomic_uint_least64_t     std::atomic<uint_least64_t>
#define atomic_uint_least32_t     std::atomic<uint_least32_t>

/* C11 _Atomic() not needed — we use the typedefs above */

#endif /* __cplusplus */
#endif /* ASPIRA_CPP_COMPAT_H */

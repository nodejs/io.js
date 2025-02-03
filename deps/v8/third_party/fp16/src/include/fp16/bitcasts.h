#pragma once
#ifndef FP16_BITCASTS_H
#define FP16_BITCASTS_H

#if defined(__cplusplus) && (__cplusplus >= 201103L)
	#include <cstdint>
#elif !defined(__OPENCL_VERSION__)
	#include <stdint.h>
#endif

#if defined(__INTEL_COMPILER)
	#include <immintrin.h>
#endif

#if defined(_MSC_VER) && !defined(__clang__) && (defined(_M_ARM) || defined(_M_ARM64))
	#include <intrin.h>
#endif


static inline float fp32_from_bits(uint32_t w) {
#if defined(__OPENCL_VERSION__)
	return as_float(w);
#elif defined(__CUDA_ARCH__)
	return __uint_as_float((unsigned int) w);
#elif defined(__INTEL_COMPILER)
	return _castu32_f32(w);
#elif defined(_MSC_VER) && !defined(__clang__) && (defined(_M_ARM) || defined(_M_ARM64))
	return _CopyFloatFromInt32((__int32) w);
#else
	union {
		uint32_t as_bits;
		float as_value;
	} fp32 = { w };
	return fp32.as_value;
#endif
}

static inline uint32_t fp32_to_bits(float f) {
#if defined(__OPENCL_VERSION__)
	return as_uint(f);
#elif defined(__CUDA_ARCH__)
	return (uint32_t) __float_as_uint(f);
#elif defined(__INTEL_COMPILER)
	return _castf32_u32(f);
#elif defined(_MSC_VER) && !defined(__clang__) && (defined(_M_ARM) || defined(_M_ARM64))
	return (uint32_t) _CopyInt32FromFloat(f);
#else
	union {
		float as_value;
		uint32_t as_bits;
	} fp32 = { f };
	return fp32.as_bits;
#endif
}

static inline double fp64_from_bits(uint64_t w) {
#if defined(__OPENCL_VERSION__)
	return as_double(w);
#elif defined(__CUDA_ARCH__)
	return __longlong_as_double((long long) w);
#elif defined(__INTEL_COMPILER)
	return _castu64_f64(w);
#elif defined(_MSC_VER) && !defined(__clang__) && (defined(_M_ARM) || defined(_M_ARM64))
	return _CopyDoubleFromInt64((__int64) w);
#else
	union {
		uint64_t as_bits;
		double as_value;
	} fp64 = { w };
	return fp64.as_value;
#endif
}

static inline uint64_t fp64_to_bits(double f) {
#if defined(__OPENCL_VERSION__)
	return as_ulong(f);
#elif defined(__CUDA_ARCH__)
	return (uint64_t) __double_as_longlong(f);
#elif defined(__INTEL_COMPILER)
	return _castf64_u64(f);
#elif defined(_MSC_VER) && !defined(__clang__) && (defined(_M_ARM) || defined(_M_ARM64))
	return (uint64_t) _CopyInt64FromDouble(f);
#else
	union {
		double as_value;
		uint64_t as_bits;
	} fp64 = { f };
	return fp64.as_bits;
#endif
}

#endif /* FP16_BITCASTS_H */

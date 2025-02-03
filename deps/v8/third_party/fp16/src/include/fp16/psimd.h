#pragma once
#ifndef FP16_PSIMD_H
#define FP16_PSIMD_H

#if defined(__cplusplus) && (__cplusplus >= 201103L)
	#include <cstdint>
#elif !defined(__OPENCL_VERSION__)
	#include <stdint.h>
#endif

#include <psimd.h>


PSIMD_INTRINSIC psimd_f32 fp16_ieee_to_fp32_psimd(psimd_u16 half) {
	const psimd_u32 word = (psimd_u32) psimd_interleave_lo_u16(psimd_zero_u16(), half);

	const psimd_u32 sign = word & psimd_splat_u32(UINT32_C(0x80000000));
	const psimd_u32 shr3_nonsign = (word + word) >> psimd_splat_u32(4);

	const psimd_u32 exp_offset = psimd_splat_u32(UINT32_C(0x70000000));
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) || defined(__GNUC__) && !defined(__STRICT_ANSI__)
	const psimd_f32 exp_scale = psimd_splat_f32(0x1.0p-112f);
#else
	const psimd_f32 exp_scale = psimd_splat_f32(fp32_from_bits(UINT32_C(0x7800000)));
#endif
	const psimd_f32 norm_nonsign = psimd_mul_f32((psimd_f32) (shr3_nonsign + exp_offset), exp_scale);

	const psimd_u16 magic_mask = psimd_splat_u16(UINT16_C(0x3E80));
	const psimd_f32 magic_bias = psimd_splat_f32(0.25f);
	const psimd_f32 denorm_nonsign = psimd_sub_f32((psimd_f32) psimd_interleave_lo_u16(half + half, magic_mask), magic_bias);

	const psimd_s32 denorm_cutoff = psimd_splat_s32(INT32_C(0x00800000));
	const psimd_s32 denorm_mask = (psimd_s32) shr3_nonsign < denorm_cutoff;
	return (psimd_f32) (sign | (psimd_s32) psimd_blend_f32(denorm_mask, denorm_nonsign, norm_nonsign));
}

PSIMD_INTRINSIC psimd_f32x2 fp16_ieee_to_fp32x2_psimd(psimd_u16 half) {
	const psimd_u32 word_lo = (psimd_u32) psimd_interleave_lo_u16(psimd_zero_u16(), half);
	const psimd_u32 word_hi = (psimd_u32) psimd_interleave_hi_u16(psimd_zero_u16(), half);

	const psimd_u32 sign_mask = psimd_splat_u32(UINT32_C(0x80000000));
	const psimd_u32 sign_lo = word_lo & sign_mask;
	const psimd_u32 sign_hi = word_hi & sign_mask;
	const psimd_u32 shr3_nonsign_lo = (word_lo + word_lo) >> psimd_splat_u32(4);
	const psimd_u32 shr3_nonsign_hi = (word_hi + word_hi) >> psimd_splat_u32(4);

	const psimd_u32 exp_offset = psimd_splat_u32(UINT32_C(0x70000000));
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) || defined(__GNUC__) && !defined(__STRICT_ANSI__)
	const psimd_f32 exp_scale = psimd_splat_f32(0x1.0p-112f);
#else
	const psimd_f32 exp_scale = psimd_splat_f32(fp32_from_bits(UINT32_C(0x7800000)));
#endif
	const psimd_f32 norm_nonsign_lo = psimd_mul_f32((psimd_f32) (shr3_nonsign_lo + exp_offset), exp_scale);
	const psimd_f32 norm_nonsign_hi = psimd_mul_f32((psimd_f32) (shr3_nonsign_hi + exp_offset), exp_scale);

	const psimd_u16 magic_mask = psimd_splat_u16(UINT16_C(0x3E80));
	const psimd_u16 shl1_half = half + half;
	const psimd_f32 magic_bias = psimd_splat_f32(0.25f);
	const psimd_f32 denorm_nonsign_lo = psimd_sub_f32((psimd_f32) psimd_interleave_lo_u16(shl1_half, magic_mask), magic_bias);
	const psimd_f32 denorm_nonsign_hi = psimd_sub_f32((psimd_f32) psimd_interleave_hi_u16(shl1_half, magic_mask), magic_bias);

	const psimd_s32 denorm_cutoff = psimd_splat_s32(INT32_C(0x00800000));
	const psimd_s32 denorm_mask_lo = (psimd_s32) shr3_nonsign_lo < denorm_cutoff;
	const psimd_s32 denorm_mask_hi = (psimd_s32) shr3_nonsign_hi < denorm_cutoff;

	psimd_f32x2 result;
	result.lo = (psimd_f32) (sign_lo | (psimd_s32) psimd_blend_f32(denorm_mask_lo, denorm_nonsign_lo, norm_nonsign_lo));
	result.hi = (psimd_f32) (sign_hi | (psimd_s32) psimd_blend_f32(denorm_mask_hi, denorm_nonsign_hi, norm_nonsign_hi));
	return result;
}

PSIMD_INTRINSIC psimd_f32 fp16_alt_to_fp32_psimd(psimd_u16 half) {
	const psimd_u32 word = (psimd_u32) psimd_interleave_lo_u16(psimd_zero_u16(), half);

	const psimd_u32 sign = word & psimd_splat_u32(INT32_C(0x80000000));
	const psimd_u32 shr3_nonsign = (word + word) >> psimd_splat_u32(4);

#if 0
	const psimd_s32 exp112_offset = psimd_splat_s32(INT32_C(0x38000000));
	const psimd_s32 nonsign_bits = (psimd_s32) shr3_nonsign + exp112_offset;
	const psimd_s32 exp1_offset = psimd_splat_s32(INT32_C(0x00800000));
	const psimd_f32 two_nonsign = (psimd_f32) (nonsign_bits + exp1_offset);
	const psimd_s32 exp113_offset = exp112_offset | exp1_offset;
	return (psimd_f32) (sign | (psimd_s32) psimd_sub_f32(two_nonsign, (psimd_f32) psimd_max_s32(nonsign_bits, exp113_offset)));
#else
	const psimd_u32 exp_offset = psimd_splat_u32(UINT32_C(0x38000000));
	const psimd_f32 nonsign = (psimd_f32) (shr3_nonsign + exp_offset);
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) || defined(__GNUC__) && !defined(__STRICT_ANSI__)
	const psimd_f32 denorm_bias = psimd_splat_f32(0x1.0p-14f);
#else
	const psimd_f32 denorm_bias = psimd_splat_f32(fp32_from_bits(UINT32_C(0x38800000)));
#endif
	return (psimd_f32) (sign | (psimd_s32) psimd_sub_f32(psimd_add_f32(nonsign, nonsign), psimd_max_f32(nonsign, denorm_bias)));
#endif
}

PSIMD_INTRINSIC psimd_f32x2 fp16_alt_to_fp32x2_psimd(psimd_u16 half) {
	const psimd_u32 word_lo = (psimd_u32) psimd_interleave_lo_u16(psimd_zero_u16(), half);
	const psimd_u32 word_hi = (psimd_u32) psimd_interleave_hi_u16(psimd_zero_u16(), half);

	const psimd_u32 sign_mask = psimd_splat_u32(UINT32_C(0x80000000));
	const psimd_u32 sign_lo = word_lo & sign_mask;
	const psimd_u32 sign_hi = word_hi & sign_mask;
	const psimd_u32 shr3_nonsign_lo = (word_lo + word_lo) >> psimd_splat_u32(4);
	const psimd_u32 shr3_nonsign_hi = (word_hi + word_hi) >> psimd_splat_u32(4);

#if 1
	const psimd_s32 exp112_offset = psimd_splat_s32(INT32_C(0x38000000));
	const psimd_s32 nonsign_bits_lo = (psimd_s32) shr3_nonsign_lo + exp112_offset;
	const psimd_s32 nonsign_bits_hi = (psimd_s32) shr3_nonsign_hi + exp112_offset;
	const psimd_s32 exp1_offset = psimd_splat_s32(INT32_C(0x00800000));
	const psimd_f32 two_nonsign_lo = (psimd_f32) (nonsign_bits_lo + exp1_offset);
	const psimd_f32 two_nonsign_hi = (psimd_f32) (nonsign_bits_hi + exp1_offset);
	const psimd_s32 exp113_offset = exp1_offset | exp112_offset;
	psimd_f32x2 result;
	result.lo = (psimd_f32) (sign_lo | (psimd_s32) psimd_sub_f32(two_nonsign_lo, (psimd_f32) psimd_max_s32(nonsign_bits_lo, exp113_offset)));
	result.hi = (psimd_f32) (sign_hi | (psimd_s32) psimd_sub_f32(two_nonsign_hi, (psimd_f32) psimd_max_s32(nonsign_bits_hi, exp113_offset)));
	return result;
#else
	const psimd_u32 exp_offset = psimd_splat_u32(UINT32_C(0x38000000));
	const psimd_f32 nonsign_lo = (psimd_f32) (shr3_nonsign_lo + exp_offset);
	const psimd_f32 nonsign_hi = (psimd_f32) (shr3_nonsign_hi + exp_offset);
	const psimd_f32 denorm_bias = psimd_splat_f32(0x1.0p-14f);
	psimd_f32x2 result;
	result.lo = (psimd_f32) (sign_lo | (psimd_s32) psimd_sub_f32(psimd_add_f32(nonsign_lo, nonsign_lo), psimd_max_f32(nonsign_lo, denorm_bias)));
	result.hi = (psimd_f32) (sign_hi | (psimd_s32) psimd_sub_f32(psimd_add_f32(nonsign_hi, nonsign_hi), psimd_max_f32(nonsign_hi, denorm_bias)));
	return result;
#endif
}

#endif /* FP16_PSIMD_H */

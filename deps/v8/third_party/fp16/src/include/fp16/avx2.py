from peachpy import *
from peachpy.x86_64 import *


def fp16_alt_xmm_to_fp32_ymm(xmm_half):
	ymm_half = YMMRegister()
	VPERMQ(ymm_half, xmm_half.as_ymm, 0b01010000)

	ymm_zero = YMMRegister()
	VPXOR(ymm_zero.as_xmm, ymm_zero.as_xmm, ymm_zero.as_xmm)

	ymm_word = YMMRegister()
	VPUNPCKLWD(ymm_word, ymm_zero, ymm_half)

	ymm_shl1_half = YMMRegister()
	VPADDW(ymm_shl1_half, ymm_half, ymm_half)

	ymm_shl1_nonsign = YMMRegister()
	VPADDD(ymm_shl1_nonsign, ymm_word, ymm_word)

	sign_mask = Constant.float32x8(-0.0)

	ymm_sign = YMMRegister()
	VANDPS(ymm_sign, ymm_word, sign_mask)

	ymm_shr3_nonsign = YMMRegister()
	VPSRLD(ymm_shr3_nonsign, ymm_shl1_nonsign, 4)

	exp_offset = Constant.uint32x8(0x38000000)

	ymm_norm_nonsign = YMMRegister()
	VPADDD(ymm_norm_nonsign, ymm_shr3_nonsign, exp_offset)

	magic_mask = Constant.uint16x16(0x3E80)
	ymm_denorm_nonsign = YMMRegister()
	VPUNPCKLWD(ymm_denorm_nonsign, ymm_shl1_half, magic_mask)

	magic_bias = Constant.float32x8(0.25)
	VSUBPS(ymm_denorm_nonsign, ymm_denorm_nonsign, magic_bias)

	ymm_denorm_cutoff = YMMRegister()
	VMOVDQA(ymm_denorm_cutoff, Constant.uint32x8(0x00800000))
	
	ymm_denorm_mask = YMMRegister()
	VPCMPGTD(ymm_denorm_mask, ymm_denorm_cutoff, ymm_shr3_nonsign)

	ymm_nonsign = YMMRegister()
	VBLENDVPS(ymm_nonsign, ymm_norm_nonsign, ymm_denorm_nonsign, ymm_denorm_mask)

	ymm_float = YMMRegister()
	VORPS(ymm_float, ymm_nonsign, ymm_sign)

	return ymm_float

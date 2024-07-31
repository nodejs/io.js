from peachpy import *
from peachpy.x86_64 import *


def fp16_alt_xmm_to_fp32_xmm(xmm_half):
	xmm_zero = XMMRegister()
	VPXOR(xmm_zero, xmm_zero, xmm_zero)

	xmm_word = XMMRegister()
	VPUNPCKLWD(xmm_word, xmm_zero, xmm_half)

	xmm_shl1_half = XMMRegister()
	VPADDW(xmm_shl1_half, xmm_half, xmm_half)

	xmm_shl1_nonsign = XMMRegister()
	VPADDD(xmm_shl1_nonsign, xmm_word, xmm_word)

	sign_mask = Constant.float32x4(-0.0)

	xmm_sign = XMMRegister()
	VANDPS(xmm_sign, xmm_word, sign_mask)

	xmm_shr3_nonsign = XMMRegister()
	VPSRLD(xmm_shr3_nonsign, xmm_shl1_nonsign, 4)

	exp_offset = Constant.uint32x4(0x38000000)

	xmm_norm_nonsign = XMMRegister()
	VPADDD(xmm_norm_nonsign, xmm_shr3_nonsign, exp_offset)

	magic_mask = Constant.uint16x8(0x3E80)
	xmm_denorm_nonsign = XMMRegister()
	VPUNPCKLWD(xmm_denorm_nonsign, xmm_shl1_half, magic_mask)

	magic_bias = Constant.float32x4(0.25)
	VSUBPS(xmm_denorm_nonsign, xmm_denorm_nonsign, magic_bias)

	xmm_denorm_cutoff = XMMRegister()
	VMOVDQA(xmm_denorm_cutoff, Constant.uint32x4(0x00800000))
	
	xmm_denorm_mask = XMMRegister()
	VPCMPGTD(xmm_denorm_mask, xmm_denorm_cutoff, xmm_shr3_nonsign)

	xmm_nonsign = XMMRegister()
	VBLENDVPS(xmm_nonsign, xmm_norm_nonsign, xmm_denorm_nonsign, xmm_denorm_mask)

	xmm_float = XMMRegister()
	VORPS(xmm_float, xmm_nonsign, xmm_sign)

	return xmm_float

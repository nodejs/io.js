// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_RISCV_EXTENSION_RISCV_V_H_
#define V8_CODEGEN_RISCV_EXTENSION_RISCV_V_H_

#include "src/codegen/assembler.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/riscv/base-assembler-riscv.h"
#include "src/codegen/riscv/constant-riscv-v.h"
#include "src/codegen/riscv/register-riscv.h"

namespace v8 {
namespace internal {

class AssemblerRISCVV : public AssemblerRiscvBase {
 public:
  // RVV
  static int32_t GenZimm(VSew vsew, Vlmul vlmul, TailAgnosticType tail = tu,
                         MaskAgnosticType mask = mu) {
    return (mask << 7) | (tail << 6) | ((vsew & 0x7) << 3) | (vlmul & 0x7);
  }

  void vl(VRegister vd, Register rs1, uint8_t lumop, VSew vsew,
          MaskType mask = NoMask);
  void vls(VRegister vd, Register rs1, Register rs2, VSew vsew,
           MaskType mask = NoMask);
  void vlx(VRegister vd, Register rs1, VRegister vs3, VSew vsew,
           MaskType mask = NoMask);

  void vs(VRegister vd, Register rs1, uint8_t sumop, VSew vsew,
          MaskType mask = NoMask);
  void vss(VRegister vd, Register rs1, Register rs2, VSew vsew,
           MaskType mask = NoMask);
  void vsx(VRegister vd, Register rs1, VRegister vs3, VSew vsew,
           MaskType mask = NoMask);

  void vsu(VRegister vd, Register rs1, VRegister vs3, VSew vsew,
           MaskType mask = NoMask);

#define SegInstr(OP)  \
  void OP##seg2(ARG); \
  void OP##seg3(ARG); \
  void OP##seg4(ARG); \
  void OP##seg5(ARG); \
  void OP##seg6(ARG); \
  void OP##seg7(ARG); \
  void OP##seg8(ARG);

#define ARG \
  VRegister vd, Register rs1, uint8_t lumop, VSew vsew, MaskType mask = NoMask

  SegInstr(vl) SegInstr(vs)
#undef ARG

#define ARG \
  VRegister vd, Register rs1, Register rs2, VSew vsew, MaskType mask = NoMask

      SegInstr(vls) SegInstr(vss)
#undef ARG

#define ARG \
  VRegister vd, Register rs1, VRegister rs2, VSew vsew, MaskType mask = NoMask

          SegInstr(vsx) SegInstr(vlx)
#undef ARG
#undef SegInstr

      // RVV Vector Arithmetic Instruction
      void vmv_vv(VRegister vd, VRegister vs1);
  void vmv_vx(VRegister vd, Register rs1);
  void vmv_vi(VRegister vd, uint8_t simm5);
  void vmv_xs(Register rd, VRegister vs2);
  void vmv_sx(VRegister vd, Register rs1);
  void vmerge_vv(VRegister vd, VRegister vs1, VRegister vs2);
  void vmerge_vx(VRegister vd, Register rs1, VRegister vs2);
  void vmerge_vi(VRegister vd, uint8_t imm5, VRegister vs2);

  void vredmaxu_vs(VRegister vd, VRegister vs2, VRegister vs1,
                   MaskType mask = NoMask);
  void vredmax_vs(VRegister vd, VRegister vs2, VRegister vs1,
                  MaskType mask = NoMask);
  void vredmin_vs(VRegister vd, VRegister vs2, VRegister vs1,
                  MaskType mask = NoMask);
  void vredminu_vs(VRegister vd, VRegister vs2, VRegister vs1,
                   MaskType mask = NoMask);

  void vadc_vv(VRegister vd, VRegister vs1, VRegister vs2);
  void vadc_vx(VRegister vd, Register rs1, VRegister vs2);
  void vadc_vi(VRegister vd, uint8_t imm5, VRegister vs2);

  void vmadc_vv(VRegister vd, VRegister vs1, VRegister vs2);
  void vmadc_vx(VRegister vd, Register rs1, VRegister vs2);
  void vmadc_vi(VRegister vd, uint8_t imm5, VRegister vs2);

  void vfmv_vf(VRegister vd, FPURegister fs1);
  void vfmv_fs(FPURegister fd, VRegister vs2);
  void vfmv_sf(VRegister vd, FPURegister fs);
  void vfmerge_vf(VRegister vd, FPURegister fs1, VRegister vs2);

  void vwaddu_wx(VRegister vd, VRegister vs2, Register rs1,
                 MaskType mask = NoMask);
  void vid_v(VRegister vd, MaskType mask = Mask);

#define DEFINE_OPIVV(name, funct6)                           \
  void name##_vv(VRegister vd, VRegister vs2, VRegister vs1, \
                 MaskType mask = NoMask);

#define DEFINE_OPIVX(name, funct6)                          \
  void name##_vx(VRegister vd, VRegister vs2, Register rs1, \
                 MaskType mask = NoMask);

#define DEFINE_OPIVI(name, funct6)                         \
  void name##_vi(VRegister vd, VRegister vs2, int8_t imm5, \
                 MaskType mask = NoMask);

#define DEFINE_OPMVV(name, funct6)                           \
  void name##_vv(VRegister vd, VRegister vs2, VRegister vs1, \
                 MaskType mask = NoMask);

#define DEFINE_OPMVX(name, funct6)                          \
  void name##_vx(VRegister vd, VRegister vs2, Register rs1, \
                 MaskType mask = NoMask);

#define DEFINE_OPFVV(name, funct6)                           \
  void name##_vv(VRegister vd, VRegister vs2, VRegister vs1, \
                 MaskType mask = NoMask);

#define DEFINE_OPFWV(name, funct6)                           \
  void name##_wv(VRegister vd, VRegister vs2, VRegister vs1, \
                 MaskType mask = NoMask);

#define DEFINE_OPFRED(name, funct6)                          \
  void name##_vs(VRegister vd, VRegister vs2, VRegister vs1, \
                 MaskType mask = NoMask);

#define DEFINE_OPFVF(name, funct6)                             \
  void name##_vf(VRegister vd, VRegister vs2, FPURegister fs1, \
                 MaskType mask = NoMask);

#define DEFINE_OPFWF(name, funct6)                             \
  void name##_wf(VRegister vd, VRegister vs2, FPURegister fs1, \
                 MaskType mask = NoMask);

#define DEFINE_OPFVV_FMA(name, funct6)                       \
  void name##_vv(VRegister vd, VRegister vs1, VRegister vs2, \
                 MaskType mask = NoMask);

#define DEFINE_OPFVF_FMA(name, funct6)                         \
  void name##_vf(VRegister vd, FPURegister fs1, VRegister vs2, \
                 MaskType mask = NoMask);

#define DEFINE_OPMVV_VIE(name) \
  void name(VRegister vd, VRegister vs2, MaskType mask = NoMask);

  DEFINE_OPIVV(vadd, VADD_FUNCT6)
  DEFINE_OPIVX(vadd, VADD_FUNCT6)
  DEFINE_OPIVI(vadd, VADD_FUNCT6)
  DEFINE_OPIVV(vsub, VSUB_FUNCT6)
  DEFINE_OPIVX(vsub, VSUB_FUNCT6)
  DEFINE_OPMVX(vdiv, VDIV_FUNCT6)
  DEFINE_OPMVX(vdivu, VDIVU_FUNCT6)
  DEFINE_OPMVX(vmul, VMUL_FUNCT6)
  DEFINE_OPMVX(vmulhu, VMULHU_FUNCT6)
  DEFINE_OPMVX(vmulhsu, VMULHSU_FUNCT6)
  DEFINE_OPMVX(vmulh, VMULH_FUNCT6)
  DEFINE_OPMVV(vdiv, VDIV_FUNCT6)
  DEFINE_OPMVV(vdivu, VDIVU_FUNCT6)
  DEFINE_OPMVV(vmul, VMUL_FUNCT6)
  DEFINE_OPMVV(vmulhu, VMULHU_FUNCT6)
  DEFINE_OPMVV(vmulhsu, VMULHSU_FUNCT6)
  DEFINE_OPMVV(vmulh, VMULH_FUNCT6)
  DEFINE_OPMVV(vwmul, VWMUL_FUNCT6)
  DEFINE_OPMVV(vwmulu, VWMULU_FUNCT6)
  DEFINE_OPMVV(vwaddu, VWADDU_FUNCT6)
  DEFINE_OPMVV(vwadd, VWADD_FUNCT6)
  DEFINE_OPMVV(vcompress, VCOMPRESS_FUNCT6)
  DEFINE_OPIVX(vsadd, VSADD_FUNCT6)
  DEFINE_OPIVV(vsadd, VSADD_FUNCT6)
  DEFINE_OPIVI(vsadd, VSADD_FUNCT6)
  DEFINE_OPIVX(vsaddu, VSADDU_FUNCT6)
  DEFINE_OPIVV(vsaddu, VSADDU_FUNCT6)
  DEFINE_OPIVI(vsaddu, VSADDU_FUNCT6)
  DEFINE_OPIVX(vssub, VSSUB_FUNCT6)
  DEFINE_OPIVV(vssub, VSSUB_FUNCT6)
  DEFINE_OPIVX(vssubu, VSSUBU_FUNCT6)
  DEFINE_OPIVV(vssubu, VSSUBU_FUNCT6)
  DEFINE_OPIVX(vrsub, VRSUB_FUNCT6)
  DEFINE_OPIVI(vrsub, VRSUB_FUNCT6)
  DEFINE_OPIVV(vminu, VMINU_FUNCT6)
  DEFINE_OPIVX(vminu, VMINU_FUNCT6)
  DEFINE_OPIVV(vmin, VMIN_FUNCT6)
  DEFINE_OPIVX(vmin, VMIN_FUNCT6)
  DEFINE_OPIVV(vmaxu, VMAXU_FUNCT6)
  DEFINE_OPIVX(vmaxu, VMAXU_FUNCT6)
  DEFINE_OPIVV(vmax, VMAX_FUNCT6)
  DEFINE_OPIVX(vmax, VMAX_FUNCT6)
  DEFINE_OPIVV(vand, VAND_FUNCT6)
  DEFINE_OPIVX(vand, VAND_FUNCT6)
  DEFINE_OPIVI(vand, VAND_FUNCT6)
  DEFINE_OPIVV(vor, VOR_FUNCT6)
  DEFINE_OPIVX(vor, VOR_FUNCT6)
  DEFINE_OPIVI(vor, VOR_FUNCT6)
  DEFINE_OPIVV(vxor, VXOR_FUNCT6)
  DEFINE_OPIVX(vxor, VXOR_FUNCT6)
  DEFINE_OPIVI(vxor, VXOR_FUNCT6)
  DEFINE_OPIVV(vrgather, VRGATHER_FUNCT6)
  DEFINE_OPIVX(vrgather, VRGATHER_FUNCT6)
  DEFINE_OPIVI(vrgather, VRGATHER_FUNCT6)

  DEFINE_OPIVX(vslidedown, VSLIDEDOWN_FUNCT6)
  DEFINE_OPIVI(vslidedown, VSLIDEDOWN_FUNCT6)
  DEFINE_OPMVX(vslide1down, VSLIDEDOWN_FUNCT6)
  DEFINE_OPFVF(vfslide1down, VSLIDEDOWN_FUNCT6)
  DEFINE_OPIVX(vslideup, VSLIDEUP_FUNCT6)
  DEFINE_OPIVI(vslideup, VSLIDEUP_FUNCT6)
  DEFINE_OPMVX(vslide1up, VSLIDEUP_FUNCT6)
  DEFINE_OPFVF(vfslide1up, VSLIDEUP_FUNCT6)

  DEFINE_OPIVV(vmseq, VMSEQ_FUNCT6)
  DEFINE_OPIVX(vmseq, VMSEQ_FUNCT6)
  DEFINE_OPIVI(vmseq, VMSEQ_FUNCT6)

  DEFINE_OPIVV(vmsne, VMSNE_FUNCT6)
  DEFINE_OPIVX(vmsne, VMSNE_FUNCT6)
  DEFINE_OPIVI(vmsne, VMSNE_FUNCT6)

  DEFINE_OPIVV(vmsltu, VMSLTU_FUNCT6)
  DEFINE_OPIVX(vmsltu, VMSLTU_FUNCT6)

  DEFINE_OPIVV(vmslt, VMSLT_FUNCT6)
  DEFINE_OPIVX(vmslt, VMSLT_FUNCT6)

  DEFINE_OPIVV(vmsle, VMSLE_FUNCT6)
  DEFINE_OPIVX(vmsle, VMSLE_FUNCT6)
  DEFINE_OPIVI(vmsle, VMSLE_FUNCT6)

  DEFINE_OPIVV(vmsleu, VMSLEU_FUNCT6)
  DEFINE_OPIVX(vmsleu, VMSLEU_FUNCT6)
  DEFINE_OPIVI(vmsleu, VMSLEU_FUNCT6)

  DEFINE_OPIVI(vmsgt, VMSGT_FUNCT6)
  DEFINE_OPIVX(vmsgt, VMSGT_FUNCT6)

  DEFINE_OPIVI(vmsgtu, VMSGTU_FUNCT6)
  DEFINE_OPIVX(vmsgtu, VMSGTU_FUNCT6)

  DEFINE_OPIVV(vsrl, VSRL_FUNCT6)
  DEFINE_OPIVX(vsrl, VSRL_FUNCT6)
  DEFINE_OPIVI(vsrl, VSRL_FUNCT6)

  DEFINE_OPIVV(vsra, VSRA_FUNCT6)
  DEFINE_OPIVX(vsra, VSRA_FUNCT6)
  DEFINE_OPIVI(vsra, VSRA_FUNCT6)

  DEFINE_OPIVV(vsll, VSLL_FUNCT6)
  DEFINE_OPIVX(vsll, VSLL_FUNCT6)
  DEFINE_OPIVI(vsll, VSLL_FUNCT6)

  DEFINE_OPIVV(vsmul, VSMUL_FUNCT6)
  DEFINE_OPIVX(vsmul, VSMUL_FUNCT6)

  DEFINE_OPFVV(vfadd, VFADD_FUNCT6)
  DEFINE_OPFVF(vfadd, VFADD_FUNCT6)
  DEFINE_OPFVV(vfsub, VFSUB_FUNCT6)
  DEFINE_OPFVF(vfsub, VFSUB_FUNCT6)
  DEFINE_OPFVV(vfdiv, VFDIV_FUNCT6)
  DEFINE_OPFVF(vfdiv, VFDIV_FUNCT6)
  DEFINE_OPFVV(vfmul, VFMUL_FUNCT6)
  DEFINE_OPFVF(vfmul, VFMUL_FUNCT6)

  // Vector Widening Floating-Point Add/Subtract Instructions
  DEFINE_OPFVV(vfwadd, VFWADD_FUNCT6)
  DEFINE_OPFVF(vfwadd, VFWADD_FUNCT6)
  DEFINE_OPFVV(vfwsub, VFWSUB_FUNCT6)
  DEFINE_OPFVF(vfwsub, VFWSUB_FUNCT6)
  DEFINE_OPFWV(vfwadd, VFWADD_W_FUNCT6)
  DEFINE_OPFWF(vfwadd, VFWADD_W_FUNCT6)
  DEFINE_OPFWV(vfwsub, VFWSUB_W_FUNCT6)
  DEFINE_OPFWF(vfwsub, VFWSUB_W_FUNCT6)

  // Vector Widening Floating-Point Reduction Instructions
  DEFINE_OPFRED(vfwredusum, VFWREDUSUM_FUNCT6)
  DEFINE_OPFRED(vfwredosum, VFWREDOSUM_FUNCT6)

  // Vector Widening Floating-Point Multiply
  DEFINE_OPFVV(vfwmul, VFWMUL_FUNCT6)
  DEFINE_OPFVF(vfwmul, VFWMUL_FUNCT6)

  DEFINE_OPFVV(vmfeq, VMFEQ_FUNCT6)
  DEFINE_OPFVV(vmfne, VMFNE_FUNCT6)
  DEFINE_OPFVV(vmflt, VMFLT_FUNCT6)
  DEFINE_OPFVV(vmfle, VMFLE_FUNCT6)
  DEFINE_OPFVV(vfmax, VMFMAX_FUNCT6)
  DEFINE_OPFVV(vfmin, VMFMIN_FUNCT6)
  DEFINE_OPFRED(vfredmax, VFREDMAX_FUNCT6)

  DEFINE_OPFVV(vfsngj, VFSGNJ_FUNCT6)
  DEFINE_OPFVF(vfsngj, VFSGNJ_FUNCT6)
  DEFINE_OPFVV(vfsngjn, VFSGNJN_FUNCT6)
  DEFINE_OPFVF(vfsngjn, VFSGNJN_FUNCT6)
  DEFINE_OPFVV(vfsngjx, VFSGNJX_FUNCT6)
  DEFINE_OPFVF(vfsngjx, VFSGNJX_FUNCT6)

  // Vector Single-Width Floating-Point Fused Multiply-Add Instructions
  DEFINE_OPFVV_FMA(vfmadd, VFMADD_FUNCT6)
  DEFINE_OPFVF_FMA(vfmadd, VFMADD_FUNCT6)
  DEFINE_OPFVV_FMA(vfmsub, VFMSUB_FUNCT6)
  DEFINE_OPFVF_FMA(vfmsub, VFMSUB_FUNCT6)
  DEFINE_OPFVV_FMA(vfmacc, VFMACC_FUNCT6)
  DEFINE_OPFVF_FMA(vfmacc, VFMACC_FUNCT6)
  DEFINE_OPFVV_FMA(vfmsac, VFMSAC_FUNCT6)
  DEFINE_OPFVF_FMA(vfmsac, VFMSAC_FUNCT6)
  DEFINE_OPFVV_FMA(vfnmadd, VFNMADD_FUNCT6)
  DEFINE_OPFVF_FMA(vfnmadd, VFNMADD_FUNCT6)
  DEFINE_OPFVV_FMA(vfnmsub, VFNMSUB_FUNCT6)
  DEFINE_OPFVF_FMA(vfnmsub, VFNMSUB_FUNCT6)
  DEFINE_OPFVV_FMA(vfnmacc, VFNMACC_FUNCT6)
  DEFINE_OPFVF_FMA(vfnmacc, VFNMACC_FUNCT6)
  DEFINE_OPFVV_FMA(vfnmsac, VFNMSAC_FUNCT6)
  DEFINE_OPFVF_FMA(vfnmsac, VFNMSAC_FUNCT6)

  // Vector Widening Floating-Point Fused Multiply-Add Instructions
  DEFINE_OPFVV_FMA(vfwmacc, VFWMACC_FUNCT6)
  DEFINE_OPFVF_FMA(vfwmacc, VFWMACC_FUNCT6)
  DEFINE_OPFVV_FMA(vfwnmacc, VFWNMACC_FUNCT6)
  DEFINE_OPFVF_FMA(vfwnmacc, VFWNMACC_FUNCT6)
  DEFINE_OPFVV_FMA(vfwmsac, VFWMSAC_FUNCT6)
  DEFINE_OPFVF_FMA(vfwmsac, VFWMSAC_FUNCT6)
  DEFINE_OPFVV_FMA(vfwnmsac, VFWNMSAC_FUNCT6)
  DEFINE_OPFVF_FMA(vfwnmsac, VFWNMSAC_FUNCT6)

  // Vector Narrowing Fixed-Point Clip Instructions
  DEFINE_OPIVV(vnclip, VNCLIP_FUNCT6)
  DEFINE_OPIVX(vnclip, VNCLIP_FUNCT6)
  DEFINE_OPIVI(vnclip, VNCLIP_FUNCT6)
  DEFINE_OPIVV(vnclipu, VNCLIPU_FUNCT6)
  DEFINE_OPIVX(vnclipu, VNCLIPU_FUNCT6)
  DEFINE_OPIVI(vnclipu, VNCLIPU_FUNCT6)

  // Vector Integer Extension
  DEFINE_OPMVV_VIE(vzext_vf8)
  DEFINE_OPMVV_VIE(vsext_vf8)
  DEFINE_OPMVV_VIE(vzext_vf4)
  DEFINE_OPMVV_VIE(vsext_vf4)
  DEFINE_OPMVV_VIE(vzext_vf2)
  DEFINE_OPMVV_VIE(vsext_vf2)

#undef DEFINE_OPIVI
#undef DEFINE_OPIVV
#undef DEFINE_OPIVX
#undef DEFINE_OPMVV
#undef DEFINE_OPMVX
#undef DEFINE_OPFVV
#undef DEFINE_OPFWV
#undef DEFINE_OPFVF
#undef DEFINE_OPFWF
#undef DEFINE_OPFVV_FMA
#undef DEFINE_OPFVF_FMA
#undef DEFINE_OPMVV_VIE
#undef DEFINE_OPFRED

#define DEFINE_VFUNARY(name, funct6, vs1)                          \
  void name(VRegister vd, VRegister vs2, MaskType mask = NoMask) { \
    GenInstrV(funct6, OP_FVV, vd, vs1, vs2, mask);                 \
  }

  DEFINE_VFUNARY(vfcvt_xu_f_v, VFUNARY0_FUNCT6, VFCVT_XU_F_V)
  DEFINE_VFUNARY(vfcvt_x_f_v, VFUNARY0_FUNCT6, VFCVT_X_F_V)
  DEFINE_VFUNARY(vfcvt_f_x_v, VFUNARY0_FUNCT6, VFCVT_F_X_V)
  DEFINE_VFUNARY(vfcvt_f_xu_v, VFUNARY0_FUNCT6, VFCVT_F_XU_V)
  DEFINE_VFUNARY(vfwcvt_xu_f_v, VFUNARY0_FUNCT6, VFWCVT_XU_F_V)
  DEFINE_VFUNARY(vfwcvt_x_f_v, VFUNARY0_FUNCT6, VFWCVT_X_F_V)
  DEFINE_VFUNARY(vfwcvt_f_x_v, VFUNARY0_FUNCT6, VFWCVT_F_X_V)
  DEFINE_VFUNARY(vfwcvt_f_xu_v, VFUNARY0_FUNCT6, VFWCVT_F_XU_V)
  DEFINE_VFUNARY(vfwcvt_f_f_v, VFUNARY0_FUNCT6, VFWCVT_F_F_V)

  DEFINE_VFUNARY(vfncvt_f_f_w, VFUNARY0_FUNCT6, VFNCVT_F_F_W)
  DEFINE_VFUNARY(vfncvt_x_f_w, VFUNARY0_FUNCT6, VFNCVT_X_F_W)
  DEFINE_VFUNARY(vfncvt_xu_f_w, VFUNARY0_FUNCT6, VFNCVT_XU_F_W)

  DEFINE_VFUNARY(vfclass_v, VFUNARY1_FUNCT6, VFCLASS_V)
  DEFINE_VFUNARY(vfsqrt_v, VFUNARY1_FUNCT6, VFSQRT_V)
  DEFINE_VFUNARY(vfrsqrt7_v, VFUNARY1_FUNCT6, VFRSQRT7_V)
  DEFINE_VFUNARY(vfrec7_v, VFUNARY1_FUNCT6, VFREC7_V)
#undef DEFINE_VFUNARY

  void vnot_vv(VRegister dst, VRegister src, MaskType mask = NoMask) {
    vxor_vi(dst, src, -1, mask);
  }

  void vneg_vv(VRegister dst, VRegister src, MaskType mask = NoMask) {
    vrsub_vx(dst, src, zero_reg, mask);
  }

  void vfneg_vv(VRegister dst, VRegister src, MaskType mask = NoMask) {
    vfsngjn_vv(dst, src, src, mask);
  }
  void vfabs_vv(VRegister dst, VRegister src, MaskType mask = NoMask) {
    vfsngjx_vv(dst, src, src, mask);
  }
  void vfirst_m(Register rd, VRegister vs2, MaskType mask = NoMask);

  void vcpop_m(Register rd, VRegister vs2, MaskType mask = NoMask);

  void vmslt_vi(VRegister vd, VRegister vs1, int8_t imm5,
                MaskType mask = NoMask) {
    DCHECK(imm5 >= -15 && imm5 <= 16);
    vmsle_vi(vd, vs1, imm5 - 1, mask);
  }

  void vmsltu_vi(VRegister vd, VRegister vs1, int8_t imm5,
                 MaskType mask = NoMask) {
    DCHECK(imm5 >= 1 && imm5 <= 16);
    vmsleu_vi(vd, vs1, imm5 - 1, mask);
  }

 protected:
  void vsetvli(Register rd, Register rs1, VSew vsew, Vlmul vlmul,
               TailAgnosticType tail = tu, MaskAgnosticType mask = mu);

  void vsetivli(Register rd, uint8_t uimm, VSew vsew, Vlmul vlmul,
                TailAgnosticType tail = tu, MaskAgnosticType mask = mu);

  inline void vsetvlmax(Register rd, VSew vsew, Vlmul vlmul,
                        TailAgnosticType tail = tu,
                        MaskAgnosticType mask = mu) {
    vsetvli(rd, zero_reg, vsew, vlmul, tu, mu);
  }

  inline void vsetvl(VSew vsew, Vlmul vlmul, TailAgnosticType tail = tu,
                     MaskAgnosticType mask = mu) {
    vsetvli(zero_reg, zero_reg, vsew, vlmul, tu, mu);
  }

  void vsetvl(Register rd, Register rs1, Register rs2);

  // ----------------------------RVV------------------------------------------
  // vsetvl
  void GenInstrV(Register rd, Register rs1, Register rs2);
  // vsetvli
  void GenInstrV(Register rd, Register rs1, uint32_t zimm);
  // OPIVV OPFVV OPMVV
  void GenInstrV(uint8_t funct6, Opcode opcode, VRegister vd, VRegister vs1,
                 VRegister vs2, MaskType mask = NoMask);
  void GenInstrV(uint8_t funct6, Opcode opcode, VRegister vd, int8_t vs1,
                 VRegister vs2, MaskType mask = NoMask);
  void GenInstrV(uint8_t funct6, Opcode opcode, VRegister vd, VRegister vs2,
                 MaskType mask = NoMask);
  // OPMVV OPFVV
  void GenInstrV(uint8_t funct6, Opcode opcode, Register rd, VRegister vs1,
                 VRegister vs2, MaskType mask = NoMask);
  // OPFVV
  void GenInstrV(uint8_t funct6, Opcode opcode, FPURegister fd, VRegister vs1,
                 VRegister vs2, MaskType mask = NoMask);

  // OPIVX OPMVX
  void GenInstrV(uint8_t funct6, Opcode opcode, VRegister vd, Register rs1,
                 VRegister vs2, MaskType mask = NoMask);
  // OPFVF
  void GenInstrV(uint8_t funct6, Opcode opcode, VRegister vd, FPURegister fs1,
                 VRegister vs2, MaskType mask = NoMask);
  // OPMVX
  void GenInstrV(uint8_t funct6, Register rd, Register rs1, VRegister vs2,
                 MaskType mask = NoMask);
  // OPIVI
  void GenInstrV(uint8_t funct6, VRegister vd, int8_t simm5, VRegister vs2,
                 MaskType mask = NoMask);

  // VL VS
  void GenInstrV(BaseOpcode opcode, uint8_t width, VRegister vd, Register rs1,
                 uint8_t umop, MaskType mask, uint8_t IsMop, bool IsMew,
                 uint8_t Nf);

  void GenInstrV(BaseOpcode opcode, uint8_t width, VRegister vd, Register rs1,
                 Register rs2, MaskType mask, uint8_t IsMop, bool IsMew,
                 uint8_t Nf);
  // VL VS AMO
  void GenInstrV(BaseOpcode opcode, uint8_t width, VRegister vd, Register rs1,
                 VRegister vs2, MaskType mask, uint8_t IsMop, bool IsMew,
                 uint8_t Nf);
  // vmv_xs vcpop_m vfirst_m
  void GenInstrV(uint8_t funct6, Opcode opcode, Register rd, uint8_t vs1,
                 VRegister vs2, MaskType mask);
};

class LoadStoreLaneParams {
 public:
  int sz;
  uint8_t laneidx;

  LoadStoreLaneParams(MachineRepresentation rep, uint8_t laneidx);

 private:
  LoadStoreLaneParams(uint8_t laneidx, int sz, int lanes)
      : sz(sz), laneidx(laneidx % lanes) {}
};
}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_RISCV_EXTENSION_RISCV_V_H_

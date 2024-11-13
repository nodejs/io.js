// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#ifndef V8_CODEGEN_X64_FMA_INSTR_H_
#define V8_CODEGEN_X64_FMA_INSTR_H_

#define FMA_SD_INSTRUCTION_LIST(V)    \
  V(vfmadd132sd, 66, 0F, 38, W1, 99)  \
  V(vfmadd213sd, 66, 0F, 38, W1, a9)  \
  V(vfmadd231sd, 66, 0F, 38, W1, b9)  \
  V(vfmsub132sd, 66, 0F, 38, W1, 9b)  \
  V(vfmsub213sd, 66, 0F, 38, W1, ab)  \
  V(vfmsub231sd, 66, 0F, 38, W1, bb)  \
  V(vfnmadd132sd, 66, 0F, 38, W1, 9d) \
  V(vfnmadd213sd, 66, 0F, 38, W1, ad) \
  V(vfnmadd231sd, 66, 0F, 38, W1, bd) \
  V(vfnmsub132sd, 66, 0F, 38, W1, 9f) \
  V(vfnmsub213sd, 66, 0F, 38, W1, af) \
  V(vfnmsub231sd, 66, 0F, 38, W1, bf)

#define FMA_SS_INSTRUCTION_LIST(V)    \
  V(vfmadd132ss, 66, 0F, 38, W0, 99)  \
  V(vfmadd213ss, 66, 0F, 38, W0, a9)  \
  V(vfmadd231ss, 66, 0F, 38, W0, b9)  \
  V(vfmsub132ss, 66, 0F, 38, W0, 9b)  \
  V(vfmsub213ss, 66, 0F, 38, W0, ab)  \
  V(vfmsub231ss, 66, 0F, 38, W0, bb)  \
  V(vfnmadd132ss, 66, 0F, 38, W0, 9d) \
  V(vfnmadd213ss, 66, 0F, 38, W0, ad) \
  V(vfnmadd231ss, 66, 0F, 38, W0, bd) \
  V(vfnmsub132ss, 66, 0F, 38, W0, 9f) \
  V(vfnmsub213ss, 66, 0F, 38, W0, af) \
  V(vfnmsub231ss, 66, 0F, 38, W0, bf)

#define FMA_PS_INSTRUCTION_LIST(V)    \
  V(vfmadd132ps, 66, 0F, 38, W0, 98)  \
  V(vfmadd213ps, 66, 0F, 38, W0, a8)  \
  V(vfmadd231ps, 66, 0F, 38, W0, b8)  \
  V(vfnmadd132ps, 66, 0F, 38, W0, 9c) \
  V(vfnmadd213ps, 66, 0F, 38, W0, ac) \
  V(vfnmadd231ps, 66, 0F, 38, W0, bc)

#define FMA_PD_INSTRUCTION_LIST(V)    \
  V(vfmadd132pd, 66, 0F, 38, W1, 98)  \
  V(vfmadd213pd, 66, 0F, 38, W1, a8)  \
  V(vfmadd231pd, 66, 0F, 38, W1, b8)  \
  V(vfnmadd132pd, 66, 0F, 38, W1, 9c) \
  V(vfnmadd213pd, 66, 0F, 38, W1, ac) \
  V(vfnmadd231pd, 66, 0F, 38, W1, bc)

#define FMA_INSTRUCTION_LIST(V) \
  FMA_SD_INSTRUCTION_LIST(V)    \
  FMA_SS_INSTRUCTION_LIST(V)    \
  FMA_PS_INSTRUCTION_LIST(V)    \
  FMA_PD_INSTRUCTION_LIST(V)

#endif  // V8_CODEGEN_X64_FMA_INSTR_H_

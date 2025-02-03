// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// PRESUBMIT_INTENTIONALLY_MISSING_INCLUDE_GUARD

#ifndef V8_CODEGEN_CODE_STUB_ASSEMBLER_MACROS_DEFINED
#error \
    "Assembler macros not defined. Did you forget to #include \"src/codegen/define-code-stub-assembler-macros.inc\" in this file?"
#endif

#undef CSA_CHECK
#undef CSA_HOLE_SECURITY_CHECK
#ifdef DEBUG
#undef CSA_DCHECK_0_ARGS
#undef CSA_DCHECK_1_ARG
#undef CSA_DCHECK_2_ARGS
#undef SWITCH_CSA_DCHECK_ARGS
#undef CSA_DCHECK_ARGS
#undef CSA_DEBUG_INFO
#endif
#undef CSA_DCHECK
#undef CSA_DCHECK_JS_ARGC_EQ
#undef BIND
#undef TYPED_VARIABLE_DEF
#undef TYPED_VARIABLE_CONSTRUCTOR
#undef TVARIABLE
#undef TVARIABLE_CONSTRUCTOR
#undef CSA_SLOW_DCHECK
#undef CSA_SBXCHECK

#undef V8_CODEGEN_CODE_STUB_ASSEMBLER_MACROS_DEFINED

// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// PRESUBMIT_INTENTIONALLY_MISSING_INCLUDE_GUARD

// This file undefines Turboshaft's assembler macros. Include this file after
// your reducer and don't forget to include 'define-assembler-macros.inc' before.

#ifndef V8_COMPILER_TURBOSHAFT_ASSEMBLER_MACROS_DEFINED
#error \
    "Assembler macros not defined. Did you forget to #include \"define-assembler-macros.inc\" in this file?"
#endif

#undef __

#undef Assert

#undef BIND
#undef BIND_LOOP
#undef BREAK
#undef CONTINUE
#undef ELSE
#undef FOREACH
#undef FOREACH_IMPL_2
#undef FOREACH_IMPL_3
#undef GOTO
#undef GOTO_IF
#undef GOTO_IF_NOT
#undef IF
#undef IF_NOT
#undef LIKELY
#undef REDUCE
#undef REDUCE_INPUT_GRAPH
#undef TSA_DCHECK
#undef UNLIKELY
#undef WHILE

#undef FORCE_UNROLL_LOOP
#undef SUPPRESSED_DANGLING_ELSE_WARNING_IF
#undef SUPPRESSED_DANGLING_ELSE_WARNING_ELSE
#undef NO_SHADOW
#undef RE_SHADOW

#undef V8_COMPILER_TURBOSHAFT_ASSEMBLER_MACROS_DEFINED

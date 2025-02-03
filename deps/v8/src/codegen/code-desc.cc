// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/code-desc.h"

#include "src/codegen/assembler-inl.h"

namespace v8 {
namespace internal {

// static
void CodeDesc::Initialize(CodeDesc* desc, Assembler* assembler,
                          int safepoint_table_offset, int handler_table_offset,
                          int constant_pool_offset, int code_comments_offset,
                          int builtin_jump_table_info_offset,
                          int reloc_info_offset) {
  desc->buffer = assembler->buffer_start();
  desc->buffer_size = assembler->buffer_size();
  desc->instr_size = assembler->instruction_size();

  desc->builtin_jump_table_info_offset = builtin_jump_table_info_offset;
  desc->builtin_jump_table_info_size =
      desc->instr_size - builtin_jump_table_info_offset;

  desc->code_comments_offset = code_comments_offset;
  desc->code_comments_size =
      desc->builtin_jump_table_info_offset - code_comments_offset;

  desc->constant_pool_offset = constant_pool_offset;
  desc->constant_pool_size = desc->code_comments_offset - constant_pool_offset;

  desc->handler_table_offset = handler_table_offset;
  desc->handler_table_size = desc->constant_pool_offset - handler_table_offset;

  desc->safepoint_table_offset = safepoint_table_offset;
  desc->safepoint_table_size =
      desc->handler_table_offset - safepoint_table_offset;

  desc->reloc_offset = reloc_info_offset;
  desc->reloc_size = desc->buffer_size - reloc_info_offset;

  desc->unwinding_info_size = 0;
  desc->unwinding_info = nullptr;

  desc->origin = assembler;

  CodeDesc::Verify(desc);
}

#ifdef DEBUG
// static
void CodeDesc::Verify(const CodeDesc* desc) {
  // Zero-size code objects upset the system.
  DCHECK_GT(desc->instr_size, 0);
  DCHECK_NOT_NULL(desc->buffer);

  // Instruction area layout invariants.
  DCHECK_GE(desc->safepoint_table_size, 0);
  DCHECK_EQ(desc->safepoint_table_size + desc->safepoint_table_offset,
            desc->handler_table_offset);
  DCHECK_GE(desc->handler_table_size, 0);
  DCHECK_EQ(desc->handler_table_size + desc->handler_table_offset,
            desc->constant_pool_offset);
  DCHECK_GE(desc->constant_pool_size, 0);
  DCHECK_EQ(desc->constant_pool_size + desc->constant_pool_offset,
            desc->code_comments_offset);
  DCHECK_GE(desc->code_comments_size, 0);
  DCHECK_EQ(desc->code_comments_size + desc->code_comments_offset,
            desc->builtin_jump_table_info_offset);
  DCHECK_GE(desc->builtin_jump_table_info_size, 0);
  DCHECK_EQ(
      desc->builtin_jump_table_info_size + desc->builtin_jump_table_info_offset,
      desc->instr_size);

  DCHECK_GE(desc->reloc_offset, 0);
  DCHECK_GE(desc->reloc_size, 0);
  DCHECK_GE(desc->unwinding_info_size, 0);
}
#endif

}  // namespace internal
}  // namespace v8

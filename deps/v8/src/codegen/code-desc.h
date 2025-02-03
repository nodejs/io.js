// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_CODE_DESC_H_
#define V8_CODEGEN_CODE_DESC_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {

// A CodeDesc describes a buffer holding instructions and relocation
// information. The instructions start at the beginning of the buffer
// and grow forward, the relocation information starts at the end of
// the buffer and grows backward. Inlined metadata sections may exist
// at the end of the instructions.
//
//  |<--------------- buffer_size ----------------------------------->|
//  |<---------------- instr_size ------------->|      |<-reloc_size->|
//  |--------------+----------------------------+------+--------------|
//  | instructions |         data               | free |  reloc info  |
//  +--------------+----------------------------+------+--------------+

// TODO(jgruber): Add a single chokepoint for specifying the instruction area
// layout (i.e. the order of inlined metadata fields).
// TODO(jgruber): Systematically maintain inlined metadata offsets and sizes
// to simplify CodeDesc initialization.

class CodeDesc {
 public:
  static void Initialize(CodeDesc* desc, Assembler* assembler,
                         int safepoint_table_offset, int handler_table_offset,
                         int constant_pool_offset, int code_comments_offset,
                         int builtin_jump_table_info_offset,
                         int reloc_info_offset);

#ifdef DEBUG
  static void Verify(const CodeDesc* desc);
#else
  inline static void Verify(const CodeDesc* desc) {}
#endif

 public:
  uint8_t* buffer = nullptr;
  int buffer_size = 0;

  // The instruction area contains executable code plus inlined metadata.

  int instr_size = 0;

  // Metadata packed into the instructions area.

  int safepoint_table_offset = 0;
  int safepoint_table_size = 0;

  int handler_table_offset = 0;
  int handler_table_size = 0;

  int constant_pool_offset = 0;
  int constant_pool_size = 0;

  int code_comments_offset = 0;
  int code_comments_size = 0;

  int builtin_jump_table_info_offset = 0;
  int builtin_jump_table_info_size = 0;

  // TODO(jgruber,v8:11036): Remove these functions once CodeDesc fields have
  // been made consistent with InstructionStream layout.
  int body_size() const { return instr_size + unwinding_info_size; }
  int instruction_size() const { return safepoint_table_offset; }
  int metadata_size() const { return body_size() - instruction_size(); }
  int safepoint_table_offset_relative() const {
    return safepoint_table_offset - instruction_size();
  }
  int handler_table_offset_relative() const {
    return handler_table_offset - instruction_size();
  }
  int constant_pool_offset_relative() const {
    return constant_pool_offset - instruction_size();
  }
  int code_comments_offset_relative() const {
    return code_comments_offset - instruction_size();
  }
  int builtin_jump_table_info_offset_relative() const {
    return builtin_jump_table_info_offset - instruction_size();
  }

  // Relocation info is located at the end of the buffer and not part of the
  // instructions area.

  int reloc_offset = 0;
  int reloc_size = 0;

  // Unwinding information.

  uint8_t* unwinding_info = nullptr;
  int unwinding_info_size = 0;
  int unwinding_info_offset_relative() const {
    // TODO(jgruber,v8:11036): Remove this function once unwinding_info setup
    // is more consistent with other metadata tables.
    return builtin_jump_table_info_offset_relative() +
           builtin_jump_table_info_size;
  }

  Assembler* origin = nullptr;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_CODE_DESC_H_

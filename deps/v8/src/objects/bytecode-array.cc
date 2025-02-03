// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/bytecode-array.h"

#include <iomanip>

#include "src/codegen/handler-table.h"
#include "src/codegen/source-position-table.h"
#include "src/common/globals.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-decoder.h"
#include "src/objects/bytecode-array-inl.h"
#include "src/utils/memcopy.h"

namespace v8 {
namespace internal {

int BytecodeArray::SourcePosition(int offset) const {
  int position = 0;
  if (!HasSourcePositionTable()) return position;
  for (SourcePositionTableIterator it(
           source_position_table(kAcquireLoad),
           SourcePositionTableIterator::kJavaScriptOnly,
           SourcePositionTableIterator::kDontSkipFunctionEntry);
       !it.done() && it.code_offset() <= offset; it.Advance()) {
    position = it.source_position().ScriptOffset();
  }
  return position;
}

int BytecodeArray::SourceStatementPosition(int offset) const {
  int position = 0;
  if (!HasSourcePositionTable()) return position;
  for (SourcePositionTableIterator it(source_position_table(kAcquireLoad));
       !it.done() && it.code_offset() <= offset; it.Advance()) {
    if (it.is_statement()) {
      position = it.source_position().ScriptOffset();
    }
  }
  return position;
}

void BytecodeArray::PrintJson(std::ostream& os) {
  DisallowGarbageCollection no_gc;

  Address base_address = GetFirstBytecodeAddress();
  BytecodeArray handle_storage = *this;
  Handle<BytecodeArray> handle(reinterpret_cast<Address*>(&handle_storage));
  interpreter::BytecodeArrayIterator iterator(handle);
  bool first_data = true;

  os << "{\"data\": [";

  while (!iterator.done()) {
    if (!first_data) os << ", ";
    Address current_address = base_address + iterator.current_offset();
    first_data = false;

    os << "{\"offset\":" << iterator.current_offset() << ", \"disassembly\":\"";
    interpreter::BytecodeDecoder::Decode(
        os, reinterpret_cast<uint8_t*>(current_address), false);

    if (interpreter::Bytecodes::IsJump(iterator.current_bytecode())) {
      os << " (" << iterator.GetJumpTargetOffset() << ")";
    }

    if (interpreter::Bytecodes::IsSwitch(iterator.current_bytecode())) {
      os << " {";
      bool first_entry = true;
      for (interpreter::JumpTableTargetOffset entry :
           iterator.GetJumpTableTargetOffsets()) {
        if (!first_entry) os << ", ";
        first_entry = false;
        os << entry.target_offset;
      }
      os << "}";
    }

    os << "\"}";
    iterator.Advance();
  }

  os << "]";

  int constant_pool_length = constant_pool()->length();
  if (constant_pool_length > 0) {
    os << ", \"constantPool\": [";
    for (int i = 0; i < constant_pool_length; i++) {
      Tagged<Object> object = constant_pool()->get(i);
      if (i > 0) os << ", ";
      os << "\"" << object << "\"";
    }
    os << "]";
  }

  os << "}";
}

void BytecodeArray::Disassemble(std::ostream& os) {
  DisallowGarbageCollection no_gc;
  // Storage for backing the handle passed to the iterator. This handle won't be
  // updated by the gc, but that's ok because we've disallowed GCs anyway.
  BytecodeArray handle_storage = *this;
  Handle<BytecodeArray> handle(reinterpret_cast<Address*>(&handle_storage));
  Disassemble(handle, os);
}

// static
void BytecodeArray::Disassemble(Handle<BytecodeArray> handle,
                                std::ostream& os) {
  DisallowGarbageCollection no_gc;

  os << "Parameter count " << handle->parameter_count() << "\n";
  os << "Register count " << handle->register_count() << "\n";
  os << "Frame size " << handle->frame_size() << "\n";

  Address base_address = handle->GetFirstBytecodeAddress();
  SourcePositionTableIterator source_positions(handle->SourcePositionTable());

  interpreter::BytecodeArrayIterator iterator(handle);
  while (!iterator.done()) {
    if (!source_positions.done() &&
        iterator.current_offset() == source_positions.code_offset()) {
      os << std::setw(5) << source_positions.source_position().ScriptOffset();
      os << (source_positions.is_statement() ? " S> " : " E> ");
      source_positions.Advance();
    } else {
      os << "         ";
    }
    Address current_address = base_address + iterator.current_offset();
    os << reinterpret_cast<const void*>(current_address) << " @ "
       << std::setw(4) << iterator.current_offset() << " : ";
    interpreter::BytecodeDecoder::Decode(
        os, reinterpret_cast<uint8_t*>(current_address));
    if (interpreter::Bytecodes::IsJump(iterator.current_bytecode())) {
      Address jump_target = base_address + iterator.GetJumpTargetOffset();
      os << " (" << reinterpret_cast<void*>(jump_target) << " @ "
         << iterator.GetJumpTargetOffset() << ")";
    }
    if (interpreter::Bytecodes::IsSwitch(iterator.current_bytecode())) {
      os << " {";
      bool first_entry = true;
      for (interpreter::JumpTableTargetOffset entry :
           iterator.GetJumpTableTargetOffsets()) {
        if (first_entry) {
          first_entry = false;
        } else {
          os << ",";
        }
        os << " " << entry.case_value << ": @" << entry.target_offset;
      }
      os << " }";
    }
    os << std::endl;
    iterator.Advance();
  }

  os << "Constant pool (size = " << handle->constant_pool()->length() << ")\n";
#ifdef OBJECT_PRINT
  if (handle->constant_pool()->length() > 0) {
    Print(handle->constant_pool(), os);
  }
#endif

  os << "Handler Table (size = " << handle->handler_table()->length() << ")\n";
#ifdef ENABLE_DISASSEMBLER
  if (handle->handler_table()->length() > 0) {
    HandlerTable table(*handle);
    table.HandlerTableRangePrint(os);
  }
#endif

  Tagged<TrustedByteArray> source_position_table =
      handle->SourcePositionTable();
  os << "Source Position Table (size = " << source_position_table->length()
     << ")\n";
#ifdef OBJECT_PRINT
  if (source_position_table->length() > 0) {
    os << Brief(source_position_table) << std::endl;
  }
#endif
}

void BytecodeArray::CopyBytecodesTo(Tagged<BytecodeArray> to) {
  BytecodeArray from = *this;
  DCHECK_EQ(from->length(), to->length());
  CopyBytes(reinterpret_cast<uint8_t*>(to->GetFirstBytecodeAddress()),
            reinterpret_cast<uint8_t*>(from->GetFirstBytecodeAddress()),
            from->length());
}

}  // namespace internal
}  // namespace v8

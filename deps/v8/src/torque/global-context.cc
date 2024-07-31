// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/global-context.h"

namespace v8 {
namespace internal {
namespace torque {

GlobalContext::GlobalContext(Ast ast)
    : collect_language_server_data_(false),
      collect_kythe_data_(false),
      force_assert_statements_(false),
      annotate_ir_(false),
      ast_(std::move(ast)) {
  CurrentScope::Scope current_scope(nullptr);
  CurrentSourcePosition::Scope current_source_position(
      SourcePosition{CurrentSourceFile::Get(), LineAndColumn::Invalid(),
                     LineAndColumn::Invalid()});
  default_namespace_ =
      RegisterDeclarable(std::make_unique<Namespace>(kBaseNamespaceName));
}

TargetArchitecture::TargetArchitecture(bool force_32bit)
    : tagged_size_(force_32bit ? sizeof(int32_t) : kTaggedSize),
      raw_ptr_size_(force_32bit ? sizeof(int32_t) : kSystemPointerSize),
      smi_tag_and_shift_size_(
          kSmiTagSize + (force_32bit ? SmiTagging<kApiInt32Size>::kSmiShiftSize
                                     : kSmiShiftSize)),
      external_ptr_size_(force_32bit ? sizeof(int32_t)
                                     : kExternalPointerSlotSize),
      indirect_ptr_size_(force_32bit ? sizeof(int32_t) : kIndirectPointerSize) {
}

}  // namespace torque
}  // namespace internal
}  // namespace v8

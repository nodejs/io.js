// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/visitors.h"

#include "src/codegen/reloc-info-inl.h"

#ifdef DEBUG
#include "src/objects/instruction-stream-inl.h"
#include "src/objects/smi.h"
#endif  // DEBUG

namespace v8 {
namespace internal {

const char* RootVisitor::RootName(Root root) {
  switch (root) {
#define ROOT_CASE(root_id, description) \
  case Root::root_id:                   \
    return description;
    ROOT_ID_LIST(ROOT_CASE)
#undef ROOT_CASE
    case Root::kNumberOfRoots:
      break;
  }
  UNREACHABLE();
}

void ObjectVisitor::VisitRelocInfo(Tagged<InstructionStream> host,
                                   RelocIterator* it) {
  // RelocInfo iteration is only valid for fully-initialized InstructionStream
  // objects. Callers must ensure this.
  DCHECK(host->IsFullyInitialized());
  for (; !it->done(); it->next()) {
    it->rinfo()->Visit(host, this);
  }
}

}  // namespace internal
}  // namespace v8

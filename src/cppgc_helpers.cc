#include "cppgc_helpers.h"
#include "env-inl.h"

namespace node {

void CppgcWrapperList::Cleanup() {
  for (auto handle : *this) {
    handle->Clean();
  }
}

void CppgcWrapperList::MemoryInfo(MemoryTracker* tracker) const {
  for (auto handle : *this) {
    tracker->Track(handle);
  }
}

}  // namespace node
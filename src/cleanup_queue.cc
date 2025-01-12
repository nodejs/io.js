#include "cleanup_queue.h"  // NOLINT(build/include_inline)
#include <algorithm>
#include <ranges>
#include <vector>
#include "cleanup_queue-inl.h"

namespace node {

std::vector<CleanupQueue::CleanupHookCallback> CleanupQueue::GetOrdered()
    const {
  // Copy into a vector, since we can't sort an unordered_set in-place.
  std::vector callbacks(cleanup_hooks_.begin(), cleanup_hooks_.end());
  // We can't erase the copied elements from `cleanup_hooks_` yet, because we
  // need to be able to check whether they were un-scheduled by another hook.

  // Sort in descending order so that the most recently inserted callbacks are
  // run first.
  std::ranges::sort(callbacks, std::greater());

  return callbacks;
}

void CleanupQueue::Drain() {
  for (const CleanupHookCallback& cb : GetOrdered()) {
    if (!cleanup_hooks_.contains(cb)) {
      // This hook was removed from the `cleanup_hooks_` set during another
      // hook that was run earlier. Nothing to do here.
      continue;
    }

    cb.fn_(cb.arg_);
    cleanup_hooks_.erase(cb);
  }
}

size_t CleanupQueue::CleanupHookCallback::Hash::operator()(
    const CleanupHookCallback& cb) const {
  return std::hash<void*>()(cb.arg_);
}

bool CleanupQueue::CleanupHookCallback::Equal::operator()(
    const CleanupHookCallback& a, const CleanupHookCallback& b) const {
  return a.fn_ == b.fn_ && a.arg_ == b.arg_;
}

}  // namespace node

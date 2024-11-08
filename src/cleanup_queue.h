#ifndef SRC_CLEANUP_QUEUE_H_
#define SRC_CLEANUP_QUEUE_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "memory_tracker.h"

namespace node {

class CleanupQueue : public MemoryRetainer {
 public:
  typedef void (*Callback)(void*);

  CleanupQueue() = default;

  // Not copyable.
  CleanupQueue(const CleanupQueue&) = delete;

  SET_MEMORY_INFO_NAME(CleanupQueue)
  SET_NO_MEMORY_INFO()
  inline size_t SelfSize() const override;

  inline bool empty() const;

  inline void Add(Callback cb, void* arg);
  inline void Remove(Callback cb, void* arg);
  void Drain();

 private:
  class CleanupHookCallback {
   public:
    CleanupHookCallback(Callback fn,
                        void* arg,
                        uint64_t insertion_order_counter)
        : fn_(fn),
          arg_(arg),
          insertion_order_counter_(insertion_order_counter) {}

    // Only hashes `arg_`, since that is usually enough to identify the hook.
    struct Hash {
      size_t operator()(const CleanupHookCallback& cb) const;
    };

    // Compares by `fn_` and `arg_` being equal.
    struct Equal {
      bool operator()(const CleanupHookCallback& a,
                      const CleanupHookCallback& b) const;
    };

   private:
    friend class CleanupQueue;
    Callback fn_;
    void* arg_;

    // We keep track of the insertion order for these objects, so that we can
    // call the callbacks in reverse order when we are cleaning up.
    uint64_t insertion_order_counter_;
  };

  std::vector<CleanupHookCallback> GetOrdered() const;

  // Use an unordered_set, so that we have efficient insertion and removal.
  std::unordered_set<CleanupHookCallback,
                     CleanupHookCallback::Hash,
                     CleanupHookCallback::Equal>
      cleanup_hooks_;
  uint64_t cleanup_hook_counter_ = 0;
};

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_CLEANUP_QUEUE_H_

#ifndef SRC_NODE_MEM_INL_H_
#define SRC_NODE_MEM_INL_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "node_mem.h"
#include "node_internals.h"

namespace node {
namespace mem {

template <typename Class, typename AllocatorStruct>
AllocatorStruct NgLibMemoryManager<Class, AllocatorStruct>::MakeAllocator() {
  return AllocatorStruct {
    static_cast<void*>(static_cast<Class*>(this)),
    MallocImpl,
    FreeImpl,
    CallocImpl,
    ReallocImpl
  };
}

template <typename Class, typename T>
void* NgLibMemoryManager<Class, T>::ReallocImpl(void* ptr,
                                             size_t size,
                                             void* user_data) {
  Class* manager = static_cast<Class*>(user_data);

  size_t previous_size = 0;
  char* original_ptr = nullptr;

  // We prepend each allocated buffer with a size_t containing the full
  // size of the allocation.
  if (size > 0) size += sizeof(size_t);

  if (ptr != nullptr) {
    // We are free()ing or re-allocating.
    original_ptr = static_cast<char*>(ptr) - sizeof(size_t);
    previous_size = *reinterpret_cast<size_t*>(original_ptr);
    // This means we called StopTracking() on this pointer before.
    if (previous_size == 0) {
      // Fall back to the standard Realloc() function.
      char* ret = UncheckedRealloc(original_ptr, size);
      if (ret != nullptr)
        ret += sizeof(size_t);
      return ret;
    }
  }

  manager->CheckAllocatedSize(previous_size);

  char* mem = UncheckedRealloc(original_ptr, size);

  if (mem != nullptr) {
    // Adjust the memory info counter.
    // TODO(addaleax): Avoid the double bookkeeping we do with
    // current_nghttp2_memory_ + ExternalMemoryAccounter
    // and provide versions of our memory allocation utilities that take an
    // Environment*/Isolate* parameter and call the V8 method transparently.
    const int64_t new_size = size - previous_size;
    manager->IncreaseAllocatedSize(new_size);
    manager->env()->external_memory_accounter()->Increase(
        manager->env()->isolate(), new_size);
    *reinterpret_cast<size_t*>(mem) = size;
    mem += sizeof(size_t);
  } else if (size == 0) {
    manager->DecreaseAllocatedSize(previous_size);
    manager->env()->external_memory_accounter()->Decrease(
        manager->env()->isolate(), previous_size);
  }
  return mem;
}

template <typename Class, typename T>
void* NgLibMemoryManager<Class, T>::MallocImpl(size_t size, void* user_data) {
  return ReallocImpl(nullptr, size, user_data);
}

template <typename Class, typename T>
void NgLibMemoryManager<Class, T>::FreeImpl(void* ptr, void* user_data) {
  if (ptr == nullptr) return;
  CHECK_NULL(ReallocImpl(ptr, 0, user_data));
}

template <typename Class, typename T>
void* NgLibMemoryManager<Class, T>::CallocImpl(size_t nmemb,
                                            size_t size,
                                            void* user_data) {
  size_t real_size = MultiplyWithOverflowCheck(nmemb, size);
  void* mem = MallocImpl(real_size, user_data);
  if (mem != nullptr)
    memset(mem, 0, real_size);
  return mem;
}

template <typename Class, typename T>
void NgLibMemoryManager<Class, T>::StopTrackingMemory(void* ptr) {
  size_t* original_ptr = reinterpret_cast<size_t*>(
      static_cast<char*>(ptr) - sizeof(size_t));
  Class* manager = static_cast<Class*>(this);
  manager->DecreaseAllocatedSize(*original_ptr);
  manager->env()->external_memory_accounter()->Decrease(
      manager->env()->isolate(), *original_ptr);
  *original_ptr = 0;
}

}  // namespace mem
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_MEM_INL_H_

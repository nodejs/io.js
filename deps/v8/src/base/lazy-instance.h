// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The LazyInstance<Type, Traits> class manages a single instance of Type,
// which will be lazily created on the first time it's accessed.  This class is
// useful for places you would normally use a function-level static, but you
// need to have guaranteed thread-safety.  The Type constructor will only ever
// be called once, even if two threads are racing to create the object.  Get()
// and Pointer() will always return the same, completely initialized instance.
//
// LazyInstance is completely thread safe, assuming that you create it safely.
// The class was designed to be POD initialized, so it shouldn't require a
// static constructor.  It really only makes sense to declare a LazyInstance as
// a global variable using the LAZY_INSTANCE_INITIALIZER initializer.
//
// LazyInstance is similar to Singleton, except it does not have the singleton
// property.  You can have multiple LazyInstance's of the same type, and each
// will manage a unique instance.  It also preallocates the space for Type, as
// to avoid allocating the Type instance on the heap.  This may help with the
// performance of creating the instance, and reducing heap fragmentation.  This
// requires that Type be a complete type so we can determine the size. See
// notes for advanced users below for more explanations.
//
// Example usage:
//   static LazyInstance<MyClass>::type my_instance = LAZY_INSTANCE_INITIALIZER;
//   void SomeMethod() {
//     my_instance.Get().SomeMethod();  // MyClass::SomeMethod()
//
//     MyClass* ptr = my_instance.Pointer();
//     ptr->DoDoDo();  // MyClass::DoDoDo
//   }
//
// Additionally you can override the way your instance is constructed by
// providing your own trait:
// Example usage:
//   struct MyCreateTrait {
//     static void Construct(void* allocated_ptr) {
//       new (allocated_ptr) MyClass(/* extra parameters... */);
//     }
//   };
//   static LazyInstance<MyClass, MyCreateTrait>::type my_instance =
//      LAZY_INSTANCE_INITIALIZER;
//
// WARNINGS:
// - This implementation of LazyInstance IS THREAD-SAFE by default. See
//   SingleThreadInitOnceTrait if you don't care about thread safety.
// - Lazy initialization comes with a cost. Make sure that you don't use it on
//   critical path. Consider adding your initialization code to a function
//   which is explicitly called once.
//
// Notes for advanced users:
// LazyInstance can actually be used in two different ways:
//
// - "Static mode" which is the default mode since it is the most efficient
//   (no extra heap allocation). In this mode, the instance is statically
//   allocated (stored in the global data section at compile time).
//   The macro LAZY_STATIC_INSTANCE_INITIALIZER (= LAZY_INSTANCE_INITIALIZER)
//   must be used to initialize static lazy instances.
//
// - "Dynamic mode". In this mode, the instance is dynamically allocated and
//   constructed (using new) by default. This mode is useful if you have to
//   deal with some code already allocating the instance for you (e.g.
//   OS::Mutex() which returns a new private OS-dependent subclass of Mutex).
//   The macro LAZY_DYNAMIC_INSTANCE_INITIALIZER must be used to initialize
//   dynamic lazy instances.

#ifndef V8_BASE_LAZY_INSTANCE_H_
#define V8_BASE_LAZY_INSTANCE_H_

#include <type_traits>

#include "src/base/macros.h"
#include "src/base/once.h"

namespace v8 {
namespace base {

#define LAZY_STATIC_INSTANCE_INITIALIZER { V8_ONCE_INIT, { {} } }
#define LAZY_DYNAMIC_INSTANCE_INITIALIZER { V8_ONCE_INIT, 0 }

// Default to static mode.
#define LAZY_INSTANCE_INITIALIZER LAZY_STATIC_INSTANCE_INITIALIZER


template <typename T>
struct LeakyInstanceTrait {
  static void Destroy(T* /* instance */) {}
};


// Traits that define how an instance is allocated and accessed.


template <typename T>
struct StaticallyAllocatedInstanceTrait {
  using StorageType = char[sizeof(T)];
  using AlignmentType = T;

  static T* MutableInstance(StorageType* storage) {
    return reinterpret_cast<T*>(storage);
  }

  template <typename ConstructTrait>
  static void InitStorageUsingTrait(StorageType* storage) {
    ConstructTrait::Construct(storage);
  }
};


template <typename T>
struct DynamicallyAllocatedInstanceTrait {
  using StorageType = T*;
  using AlignmentType = T*;

  static T* MutableInstance(StorageType* storage) {
    return *storage;
  }

  template <typename CreateTrait>
  static void InitStorageUsingTrait(StorageType* storage) {
    *storage = CreateTrait::Create();
  }
};


template <typename T>
struct DefaultConstructTrait {
  // Constructs the provided object which was already allocated.
  static void Construct(void* allocated_ptr) { new (allocated_ptr) T(); }
};


template <typename T>
struct DefaultCreateTrait {
  static T* Create() {
    return new T();
  }
};


struct ThreadSafeInitOnceTrait {
  template <typename Function, typename Storage>
  static void Init(OnceType* once, Function function, Storage storage) {
    CallOnce(once, function, storage);
  }
};


// Initialization trait for users who don't care about thread-safety.
struct SingleThreadInitOnceTrait {
  template <typename Function, typename Storage>
  static void Init(OnceType* once, Function function, Storage storage) {
    if (*once == ONCE_STATE_UNINITIALIZED) {
      function(storage);
      *once = ONCE_STATE_DONE;
    }
  }
};


// TODO(pliard): Handle instances destruction (using global destructors).
template <typename T, typename AllocationTrait, typename CreateTrait,
          typename InitOnceTrait, typename DestroyTrait  /* not used yet. */>
struct LazyInstanceImpl {
 public:
  using StorageType = typename AllocationTrait::StorageType;
  using AlignmentType = typename AllocationTrait::AlignmentType;

 private:
  static void InitInstance(void* storage) {
    AllocationTrait::template InitStorageUsingTrait<CreateTrait>(
        static_cast<StorageType*>(storage));
  }

  void Init() const {
    InitOnceTrait::Init(&once_, &InitInstance, static_cast<void*>(&storage_));
  }

 public:
  T* Pointer() {
    Init();
    return AllocationTrait::MutableInstance(&storage_);
  }

  const T& Get() const {
    Init();
    return *AllocationTrait::MutableInstance(&storage_);
  }

  mutable OnceType once_;
  alignas(AlignmentType) mutable StorageType storage_;
};


template <typename T,
          typename CreateTrait = DefaultConstructTrait<T>,
          typename InitOnceTrait = ThreadSafeInitOnceTrait,
          typename DestroyTrait = LeakyInstanceTrait<T> >
struct LazyStaticInstance {
  using type = LazyInstanceImpl<T, StaticallyAllocatedInstanceTrait<T>,
                                CreateTrait, InitOnceTrait, DestroyTrait>;
};


template <typename T,
          typename CreateTrait = DefaultConstructTrait<T>,
          typename InitOnceTrait = ThreadSafeInitOnceTrait,
          typename DestroyTrait = LeakyInstanceTrait<T> >
struct LazyInstance {
  // A LazyInstance is a LazyStaticInstance.
  using type = typename LazyStaticInstance<T, CreateTrait, InitOnceTrait,
                                           DestroyTrait>::type;
};


template <typename T,
          typename CreateTrait = DefaultCreateTrait<T>,
          typename InitOnceTrait = ThreadSafeInitOnceTrait,
          typename DestroyTrait = LeakyInstanceTrait<T> >
struct LazyDynamicInstance {
  using type = LazyInstanceImpl<T, DynamicallyAllocatedInstanceTrait<T>,
                                CreateTrait, InitOnceTrait, DestroyTrait>;
};

// LeakyObject<T> wraps an object of type T, which is initialized in the
// constructor but never destructed. Thus LeakyObject<T> is trivially
// destructible and can be used in static (lazily initialized) variables.
template <typename T>
class LeakyObject {
 public:
  template <typename... Args>
  explicit LeakyObject(Args&&... args) {
    new (storage_) T(std::forward<Args>(args)...);
  }

  LeakyObject(const LeakyObject&) = delete;
  LeakyObject& operator=(const LeakyObject&) = delete;

  T* get() { return reinterpret_cast<T*>(storage_); }

 private:
  alignas(T) char storage_[sizeof(T)];
};

// Define a function which returns a pointer to a lazily initialized and never
// destructed object of type T.
#define DEFINE_LAZY_LEAKY_OBJECT_GETTER(T, FunctionName, ...) \
  T* FunctionName() {                                         \
    static ::v8::base::LeakyObject<T> object{__VA_ARGS__};    \
    return object.get();                                      \
  }

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_LAZY_INSTANCE_H_

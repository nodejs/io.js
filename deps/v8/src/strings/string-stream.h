// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_STRINGS_STRING_STREAM_H_
#define V8_STRINGS_STRING_STREAM_H_

#include <memory>

#include "src/base/small-vector.h"
#include "src/base/strings.h"
#include "src/base/vector.h"
#include "src/handles/handles.h"
#include "src/objects/objects.h"
#include "src/objects/tagged.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

// Forward declarations.
class ByteArray;

class StringAllocator {
 public:
  virtual ~StringAllocator() = default;
  // Allocate a number of bytes.
  virtual char* allocate(unsigned bytes) = 0;
  // Allocate a larger number of bytes and copy the old buffer to the new one.
  // bytes is an input and output parameter passing the old size of the buffer
  // and returning the new size.  If allocation fails then we return the old
  // buffer and do not increase the size.
  virtual char* grow(unsigned* bytes) = 0;
};

// Normal allocator uses new[] and delete[].
class HeapStringAllocator final : public StringAllocator {
 public:
  ~HeapStringAllocator() override { DeleteArray(space_); }
  char* allocate(unsigned bytes) override;
  char* grow(unsigned* bytes) override;

 private:
  char* space_;
};

class FixedStringAllocator final : public StringAllocator {
 public:
  FixedStringAllocator(char* buffer, unsigned length)
      : buffer_(buffer), length_(length) {}
  ~FixedStringAllocator() override = default;
  FixedStringAllocator(const FixedStringAllocator&) = delete;
  FixedStringAllocator& operator=(const FixedStringAllocator&) = delete;

  char* allocate(unsigned bytes) override;
  char* grow(unsigned* bytes) override;

 private:
  char* buffer_;
  unsigned length_;
};

template <std::size_t kInlineSize>
class SmallStringOptimizedAllocator final : public StringAllocator {
 public:
  using SmallVector = base::SmallVector<char, kInlineSize>;

  explicit SmallStringOptimizedAllocator(SmallVector* vector) V8_NOEXCEPT
      : vector_(vector) {}

  char* allocate(unsigned bytes) override {
    vector_->resize_no_init(bytes);
    return vector_->data();
  }

  char* grow(unsigned* bytes) override {
    unsigned new_bytes = *bytes * 2;
    // Check for overflow.
    if (new_bytes <= *bytes) {
      return vector_->data();
    }
    vector_->resize_no_init(new_bytes);
    *bytes = new_bytes;
    return vector_->data();
  }

 private:
  SmallVector* vector_;
};

class StringStream final {
  class FmtElm final {
   public:
    FmtElm(int value) : FmtElm(INT) {  // NOLINT
      data_.u_int_ = value;
    }
    explicit FmtElm(double value) : FmtElm(DOUBLE) {  // NOLINT
      data_.u_double_ = value;
    }
    FmtElm(const char* value) : FmtElm(C_STR) {  // NOLINT
      data_.u_c_str_ = value;
    }
    FmtElm(const base::Vector<const base::uc16>& value)  // NOLINT
        : FmtElm(LC_STR) {
      data_.u_lc_str_ = &value;
    }
    template <typename T>
    FmtElm(Tagged<T> value) : FmtElm(OBJ) {  // NOLINT
      data_.u_obj_ = value.ptr();
    }
    template <typename T>
    FmtElm(Handle<T> value) : FmtElm(HANDLE) {  // NOLINT
      data_.u_handle_ = value.location();
    }
    FmtElm(void* value) : FmtElm(POINTER) {  // NOLINT
      data_.u_pointer_ = value;
    }

   private:
    friend class StringStream;
    enum Type { INT, DOUBLE, C_STR, LC_STR, OBJ, HANDLE, POINTER };

#ifdef DEBUG
    Type type_;
    explicit FmtElm(Type type) : type_(type) {}
#else
    explicit FmtElm(Type) {}
#endif

    union {
      int u_int_;
      double u_double_;
      const char* u_c_str_;
      const base::Vector<const base::uc16>* u_lc_str_;
      Address u_obj_;
      Address* u_handle_;
      void* u_pointer_;
    } data_;
  };

 public:
  enum ObjectPrintMode { kPrintObjectConcise, kPrintObjectVerbose };
  explicit StringStream(StringAllocator* allocator,
                        ObjectPrintMode object_print_mode = kPrintObjectVerbose)
      : allocator_(allocator),
        object_print_mode_(object_print_mode),
        capacity_(kInitialCapacity),
        length_(0),
        buffer_(allocator_->allocate(kInitialCapacity)) {
    buffer_[0] = 0;
  }

  bool Put(char c);
  bool Put(Tagged<String> str);
  bool Put(Tagged<String> str, int start, int end);
  void Add(const char* format) { Add(base::CStrVector(format)); }
  void Add(base::Vector<const char> format) {
    Add(format, base::Vector<FmtElm>());
  }

  template <typename... Args>
  void Add(const char* format, Args... args) {
    Add(base::CStrVector(format), args...);
  }

  template <typename... Args>
  void Add(base::Vector<const char> format, Args... args) {
    FmtElm elems[]{args...};
    Add(format, base::ArrayVector(elems));
  }

  // Getting the message out.
  void OutputToFile(FILE* out);
  void OutputToStdOut() { OutputToFile(stdout); }
  void Log(Isolate* isolate);
  DirectHandle<String> ToString(Isolate* isolate);
  std::unique_ptr<char[]> ToCString() const;
  int length() const { return length_; }

  // Object printing support.
  void PrintName(Tagged<Object> o);
  void PrintFixedArray(Tagged<FixedArray> array, unsigned int limit);
  void PrintByteArray(Tagged<ByteArray> ba);
  void PrintUsingMap(Isolate* isolate, Tagged<JSObject> js_object);
  void PrintPrototype(Isolate* isolate, Tagged<JSFunction> fun,
                      Tagged<Object> receiver);
  void PrintSecurityTokenIfChanged(Isolate* isolate,
                                   Tagged<JSFunction> function);
  void PrintFunction(Isolate* isolate, Tagged<JSFunction> function,
                     Tagged<Object> receiver);

  // Reset the stream.
  void Reset() {
    length_ = 0;
    buffer_[0] = 0;
  }

  // Mentioned object cache support.
  void PrintMentionedObjectCache(Isolate* isolate);
  V8_EXPORT_PRIVATE static void ClearMentionedObjectCache(Isolate* isolate);
#ifdef DEBUG
  bool IsMentionedObjectCacheClear(Isolate* isolate);
#endif

  static const int kInitialCapacity = 16;

 private:
  void Add(base::Vector<const char> format, base::Vector<FmtElm> elms);
  void PrintObject(Tagged<Object> obj);

  StringAllocator* allocator_;
  ObjectPrintMode object_print_mode_;
  unsigned capacity_;
  unsigned length_;  // does not include terminating 0-character
  char* buffer_;

  bool full() const { return (capacity_ - length_) == 1; }
  int space() const { return capacity_ - length_; }

  DISALLOW_IMPLICIT_CONSTRUCTORS(StringStream);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_STRINGS_STRING_STREAM_H_

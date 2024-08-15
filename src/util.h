// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef SRC_UTIL_H_
#define SRC_UTIL_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "uv.h"
#include "v8.h"

#include "node.h"
#include "node_exit_code.h"

#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <array>
#include <bit>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef __GNUC__
#define MUST_USE_RESULT __attribute__((warn_unused_result))
#else
#define MUST_USE_RESULT
#endif

namespace node {

#ifdef _WIN32
/* MAX_PATH is in characters, not bytes. Make sure we have enough headroom. */
#define PATH_MAX_BYTES (MAX_PATH * 4)
#else
#define PATH_MAX_BYTES (PATH_MAX)
#endif

// These should be used in our code as opposed to the native
// versions as they abstract out some platform and or
// compiler version specific functionality
// malloc(0) and realloc(ptr, 0) have implementation-defined behavior in
// that the standard allows them to either return a unique pointer or a
// nullptr for zero-sized allocation requests.  Normalize by always using
// a nullptr.
template <typename T>
inline T* UncheckedRealloc(T* pointer, size_t n);
template <typename T>
inline T* UncheckedMalloc(size_t n);
template <typename T>
inline T* UncheckedCalloc(size_t n);

// Same things, but aborts immediately instead of returning nullptr when
// no memory is available.
template <typename T>
inline T* Realloc(T* pointer, size_t n);
template <typename T>
inline T* Malloc(size_t n);
template <typename T>
inline T* Calloc(size_t n);

inline char* Malloc(size_t n);
inline char* Calloc(size_t n);
inline char* UncheckedMalloc(size_t n);
inline char* UncheckedCalloc(size_t n);

template <typename T>
inline T MultiplyWithOverflowCheck(T a, T b);

namespace per_process {
// Tells whether the per-process V8::Initialize() is called and
// if it is safe to call v8::Isolate::TryGetCurrent().
extern bool v8_initialized;
}  // namespace per_process

// Used by the allocation functions when allocation fails.
// Thin wrapper around v8::Isolate::LowMemoryNotification() that checks
// whether V8 is initialized.
void LowMemoryNotification();

// The reason that Assert() takes a struct argument instead of individual
// const char*s is to ease instruction cache pressure in calls from CHECK.
struct AssertionInfo {
  const char* file_line;  // filename:line
  const char* message;
  const char* function;
};

// This indirectly calls backtrace so it can not be marked as [[noreturn]].
void NODE_EXTERN_PRIVATE Assert(const AssertionInfo& info);
void DumpNativeBacktrace(FILE* fp);
void DumpJavaScriptBacktrace(FILE* fp);

// Windows 8+ does not like abort() in Release mode
#ifdef _WIN32
#define ABORT_NO_BACKTRACE() _exit(static_cast<int>(node::ExitCode::kAbort))
#else
#define ABORT_NO_BACKTRACE() abort()
#endif

// Caller of this macro must not be marked as [[noreturn]]. Printing of
// backtraces may not work correctly in [[noreturn]] functions because
// when generating code for them the compiler can choose not to
// maintain the frame pointers or link registers that are necessary for
// correct backtracing.
// `ABORT` must be a macro and not a [[noreturn]] function to make sure the
// backtrace is correct.
#define ABORT()                                                                \
  do {                                                                         \
    node::DumpNativeBacktrace(stderr);                                         \
    node::DumpJavaScriptBacktrace(stderr);                                     \
    fflush(stderr);                                                            \
    ABORT_NO_BACKTRACE();                                                      \
  } while (0)

#define ERROR_AND_ABORT(expr)                                                  \
  do {                                                                         \
    /* Make sure that this struct does not end up in inline code, but      */  \
    /* rather in a read-only data section when modifying this code.        */  \
    static const node::AssertionInfo error_and_abort_args = {                  \
        __FILE__ ":" STRINGIFY(__LINE__), #expr, PRETTY_FUNCTION_NAME};        \
    node::Assert(error_and_abort_args);                                        \
    /* `node::Assert` doesn't return. Add an [[noreturn]] abort() here to  */  \
    /* make the compiler happy about no return value in the caller         */  \
    /* function when calling ERROR_AND_ABORT.                              */  \
    ABORT_NO_BACKTRACE();                                                      \
  } while (0)

#ifdef __GNUC__
#define LIKELY(expr) __builtin_expect(!!(expr), 1)
#define UNLIKELY(expr) __builtin_expect(!!(expr), 0)
#define PRETTY_FUNCTION_NAME __PRETTY_FUNCTION__
#else
#define LIKELY(expr) expr
#define UNLIKELY(expr) expr
#if defined(_MSC_VER)
#define PRETTY_FUNCTION_NAME __FUNCSIG__
#else
#define PRETTY_FUNCTION_NAME ""
#endif
#endif

#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)

#define CHECK(expr)                                                           \
  do {                                                                        \
    if (UNLIKELY(!(expr))) {                                                  \
      ERROR_AND_ABORT(expr);                                                  \
    }                                                                         \
  } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_GE(a, b) CHECK((a) >= (b))
#define CHECK_GT(a, b) CHECK((a) > (b))
#define CHECK_LE(a, b) CHECK((a) <= (b))
#define CHECK_LT(a, b) CHECK((a) < (b))
#define CHECK_NE(a, b) CHECK((a) != (b))
#define CHECK_NULL(val) CHECK((val) == nullptr)
#define CHECK_NOT_NULL(val) CHECK((val) != nullptr)
#define CHECK_IMPLIES(a, b) CHECK(!(a) || (b))

#ifdef DEBUG
  #define DCHECK(expr) CHECK(expr)
  #define DCHECK_EQ(a, b) CHECK((a) == (b))
  #define DCHECK_GE(a, b) CHECK((a) >= (b))
  #define DCHECK_GT(a, b) CHECK((a) > (b))
  #define DCHECK_LE(a, b) CHECK((a) <= (b))
  #define DCHECK_LT(a, b) CHECK((a) < (b))
  #define DCHECK_NE(a, b) CHECK((a) != (b))
  #define DCHECK_NULL(val) CHECK((val) == nullptr)
  #define DCHECK_NOT_NULL(val) CHECK((val) != nullptr)
  #define DCHECK_IMPLIES(a, b) CHECK(!(a) || (b))
#else
  #define DCHECK(expr)
  #define DCHECK_EQ(a, b)
  #define DCHECK_GE(a, b)
  #define DCHECK_GT(a, b)
  #define DCHECK_LE(a, b)
  #define DCHECK_LT(a, b)
  #define DCHECK_NE(a, b)
  #define DCHECK_NULL(val)
  #define DCHECK_NOT_NULL(val)
  #define DCHECK_IMPLIES(a, b)
#endif


#define UNREACHABLE(...)                                                      \
  ERROR_AND_ABORT("Unreachable code reached" __VA_OPT__(": ") __VA_ARGS__)

// ECMA-262, 15th edition, 21.1.2.6. Number.MAX_SAFE_INTEGER (2^53-1)
constexpr int64_t kMaxSafeJsInteger = 9007199254740991;

inline bool IsSafeJsInt(v8::Local<v8::Value> v);

// TAILQ-style intrusive list node.
template <typename T>
class ListNode;

// TAILQ-style intrusive list head.
template <typename T, ListNode<T> (T::*M)>
class ListHead;

template <typename T>
class ListNode {
 public:
  inline ListNode();
  inline ~ListNode();
  inline void Remove();
  inline bool IsEmpty() const;

  ListNode(const ListNode&) = delete;
  ListNode& operator=(const ListNode&) = delete;

 private:
  template <typename U, ListNode<U> (U::*M)> friend class ListHead;
  friend int GenDebugSymbols();
  ListNode* prev_;
  ListNode* next_;
};

template <typename T, ListNode<T> (T::*M)>
class ListHead {
 public:
  class Iterator {
   public:
    inline T* operator*() const;
    inline const Iterator& operator++();
    inline bool operator!=(const Iterator& that) const;

   private:
    friend class ListHead;
    inline explicit Iterator(ListNode<T>* node);
    ListNode<T>* node_;
  };

  inline ListHead() = default;
  inline ~ListHead();
  inline void PushBack(T* element);
  inline void PushFront(T* element);
  inline bool IsEmpty() const;
  inline T* PopFront();
  inline Iterator begin() const;
  inline Iterator end() const;

  ListHead(const ListHead&) = delete;
  ListHead& operator=(const ListHead&) = delete;

 private:
  friend int GenDebugSymbols();
  ListNode<T> head_;
};

// The helper is for doing safe downcasts from base types to derived types.
template <typename Inner, typename Outer>
class ContainerOfHelper {
 public:
  inline ContainerOfHelper(Inner Outer::*field, Inner* pointer);
  template <typename TypeName>
  inline operator TypeName*() const;
 private:
  Outer* const pointer_;
};

// Calculate the address of the outer (i.e. embedding) struct from
// the interior pointer to a data member.
template <typename Inner, typename Outer>
constexpr ContainerOfHelper<Inner, Outer> ContainerOf(Inner Outer::*field,
                                                      Inner* pointer);

class KVStore {
 public:
  KVStore() = default;
  virtual ~KVStore() = default;
  KVStore(const KVStore&) = delete;
  KVStore& operator=(const KVStore&) = delete;
  KVStore(KVStore&&) = delete;
  KVStore& operator=(KVStore&&) = delete;

  virtual v8::MaybeLocal<v8::String> Get(v8::Isolate* isolate,
                                         v8::Local<v8::String> key) const = 0;
  virtual v8::Maybe<std::string> Get(const char* key) const = 0;
  virtual void Set(v8::Isolate* isolate,
                   v8::Local<v8::String> key,
                   v8::Local<v8::String> value) = 0;
  virtual int32_t Query(v8::Isolate* isolate,
                        v8::Local<v8::String> key) const = 0;
  virtual int32_t Query(const char* key) const = 0;
  virtual void Delete(v8::Isolate* isolate, v8::Local<v8::String> key) = 0;
  virtual v8::Local<v8::Array> Enumerate(v8::Isolate* isolate) const = 0;

  virtual std::shared_ptr<KVStore> Clone(v8::Isolate* isolate) const;
  virtual v8::Maybe<bool> AssignFromObject(v8::Local<v8::Context> context,
                                           v8::Local<v8::Object> entries);
  v8::Maybe<bool> AssignToObject(v8::Isolate* isolate,
                                 v8::Local<v8::Context> context,
                                 v8::Local<v8::Object> object);

  static std::shared_ptr<KVStore> CreateMapKVStore();
};

// Convenience wrapper around v8::String::NewFromOneByte().
inline v8::Local<v8::String> OneByteString(v8::Isolate* isolate,
                                           const char* data,
                                           int length = -1);

// For the people that compile with -funsigned-char.
inline v8::Local<v8::String> OneByteString(v8::Isolate* isolate,
                                           const signed char* data,
                                           int length = -1);

inline v8::Local<v8::String> OneByteString(v8::Isolate* isolate,
                                           const unsigned char* data,
                                           int length = -1);

// Used to be a macro, hence the uppercase name.
template <int N>
inline v8::Local<v8::String> FIXED_ONE_BYTE_STRING(
    v8::Isolate* isolate,
    const char(&data)[N]) {
  return OneByteString(isolate, data, N - 1);
}

template <std::size_t N>
inline v8::Local<v8::String> FIXED_ONE_BYTE_STRING(
    v8::Isolate* isolate,
    const std::array<char, N>& arr) {
  return OneByteString(isolate, arr.data(), N - 1);
}

// tolower() is locale-sensitive.  Use ToLower() instead.
inline char ToLower(char c);
inline std::string ToLower(const std::string& in);

// toupper() is locale-sensitive.  Use ToUpper() instead.
inline char ToUpper(char c);
inline std::string ToUpper(const std::string& in);

// strcasecmp() is locale-sensitive.  Use StringEqualNoCase() instead.
inline bool StringEqualNoCase(const char* a, const char* b);

// strncasecmp() is locale-sensitive.  Use StringEqualNoCaseN() instead.
inline bool StringEqualNoCaseN(const char* a, const char* b, size_t length);

template <typename T, size_t N>
constexpr size_t arraysize(const T (&)[N]) {
  return N;
}

template <typename T, size_t N>
constexpr size_t strsize(const T (&)[N]) {
  return N - 1;
}

// Allocates an array of member type T. For up to kStackStorageSize items,
// the stack is used, otherwise malloc().
template <typename T, size_t kStackStorageSize = 1024>
class MaybeStackBuffer {
 public:
  const T* out() const {
    return buf_;
  }

  T* out() {
    return buf_;
  }

  // operator* for compatibility with `v8::String::(Utf8)Value`
  T* operator*() {
    return buf_;
  }

  const T* operator*() const {
    return buf_;
  }

  T& operator[](size_t index) {
    CHECK_LT(index, length());
    return buf_[index];
  }

  const T& operator[](size_t index) const {
    CHECK_LT(index, length());
    return buf_[index];
  }

  size_t length() const {
    return length_;
  }

  // Current maximum capacity of the buffer with which SetLength() can be used
  // without first calling AllocateSufficientStorage().
  size_t capacity() const {
    return capacity_;
  }

  // Make sure enough space for `storage` entries is available.
  // This method can be called multiple times throughout the lifetime of the
  // buffer, but once this has been called Invalidate() cannot be used.
  // Content of the buffer in the range [0, length()) is preserved.
  void AllocateSufficientStorage(size_t storage);

  void SetLength(size_t length) {
    // capacity() returns how much memory is actually available.
    CHECK_LE(length, capacity());
    length_ = length;
  }

  void SetLengthAndZeroTerminate(size_t length) {
    // capacity() returns how much memory is actually available.
    CHECK_LE(length + 1, capacity());
    SetLength(length);

    // T() is 0 for integer types, nullptr for pointers, etc.
    buf_[length] = T();
  }

  // Make dereferencing this object return nullptr.
  // This method can be called multiple times throughout the lifetime of the
  // buffer, but once this has been called AllocateSufficientStorage() cannot
  // be used.
  void Invalidate() {
    CHECK(!IsAllocated());
    capacity_ = 0;
    length_ = 0;
    buf_ = nullptr;
  }

  // If the buffer is stored in the heap rather than on the stack.
  bool IsAllocated() const {
    return !IsInvalidated() && buf_ != buf_st_;
  }

  // If Invalidate() has been called.
  bool IsInvalidated() const {
    return buf_ == nullptr;
  }

  // Release ownership of the malloc'd buffer.
  // Note: This does not free the buffer.
  void Release() {
    CHECK(IsAllocated());
    buf_ = buf_st_;
    length_ = 0;
    capacity_ = arraysize(buf_st_);
  }

  MaybeStackBuffer()
      : length_(0), capacity_(arraysize(buf_st_)), buf_(buf_st_) {
    // Default to a zero-length, null-terminated buffer.
    buf_[0] = T();
  }

  explicit MaybeStackBuffer(size_t storage) : MaybeStackBuffer() {
    AllocateSufficientStorage(storage);
  }

  ~MaybeStackBuffer() {
    if (IsAllocated())
      free(buf_);
  }

  inline std::basic_string<T> ToString() const { return {out(), length()}; }
  inline std::basic_string_view<T> ToStringView() const {
    return {out(), length()};
  }

 private:
  size_t length_;
  // capacity of the malloc'ed buf_
  size_t capacity_;
  T* buf_;
  T buf_st_[kStackStorageSize];
};

// Provides access to an ArrayBufferView's storage, either the original,
// or for small data, a copy of it. This object's lifetime is bound to the
// original ArrayBufferView's lifetime.
template <typename T, size_t kStackStorageSize = 64>
class ArrayBufferViewContents {
 public:
  ArrayBufferViewContents() = default;

  ArrayBufferViewContents(const ArrayBufferViewContents&) = delete;
  void operator=(const ArrayBufferViewContents&) = delete;

  explicit inline ArrayBufferViewContents(v8::Local<v8::Value> value);
  explicit inline ArrayBufferViewContents(v8::Local<v8::Object> value);
  explicit inline ArrayBufferViewContents(v8::Local<v8::ArrayBufferView> abv);
  inline void Read(v8::Local<v8::ArrayBufferView> abv);
  inline void ReadValue(v8::Local<v8::Value> buf);

  inline bool WasDetached() const { return was_detached_; }
  inline const T* data() const { return data_; }
  inline size_t length() const { return length_; }

 private:
  // Declaring operator new and delete as deleted is not spec compliant.
  // Therefore, declare them private instead to disable dynamic alloc.
  void* operator new(size_t size);
  void* operator new[](size_t size);
  void operator delete(void*, size_t);
  void operator delete[](void*, size_t);

  T stack_storage_[kStackStorageSize];
  T* data_ = nullptr;
  size_t length_ = 0;
  bool was_detached_ = false;
};

class Utf8Value : public MaybeStackBuffer<char> {
 public:
  explicit Utf8Value(v8::Isolate* isolate, v8::Local<v8::Value> value);

  inline std::string ToString() const { return std::string(out(), length()); }
  inline std::string_view ToStringView() const {
    return std::string_view(out(), length());
  }

  inline bool operator==(const char* a) const { return strcmp(out(), a) == 0; }
  inline bool operator!=(const char* a) const { return !(*this == a); }
};

class TwoByteValue : public MaybeStackBuffer<uint16_t> {
 public:
  explicit TwoByteValue(v8::Isolate* isolate, v8::Local<v8::Value> value);
};

class BufferValue : public MaybeStackBuffer<char> {
 public:
  explicit BufferValue(v8::Isolate* isolate, v8::Local<v8::Value> value);

  inline std::string ToString() const { return std::string(out(), length()); }
  inline std::string_view ToStringView() const {
    return std::string_view(out(), length());
  }
};

#define SPREAD_BUFFER_ARG(val, name)                                           \
  CHECK((val)->IsArrayBufferView());                                           \
  v8::Local<v8::ArrayBufferView> name = (val).As<v8::ArrayBufferView>();       \
  const size_t name##_offset = name->ByteOffset();                             \
  const size_t name##_length = name->ByteLength();                             \
  char* const name##_data =                                                    \
      static_cast<char*>(name->Buffer()->Data()) + name##_offset;              \
  if (name##_length > 0) CHECK_NE(name##_data, nullptr);

// Use this when a variable or parameter is unused in order to explicitly
// silence a compiler warning about that.
template <typename T> inline void USE(T&&) {}

template <typename Fn>
struct OnScopeLeaveImpl {
  Fn fn_;
  bool active_;

  explicit OnScopeLeaveImpl(Fn&& fn) : fn_(std::move(fn)), active_(true) {}
  ~OnScopeLeaveImpl() { if (active_) fn_(); }

  OnScopeLeaveImpl(const OnScopeLeaveImpl& other) = delete;
  OnScopeLeaveImpl& operator=(const OnScopeLeaveImpl& other) = delete;
  OnScopeLeaveImpl(OnScopeLeaveImpl&& other)
    : fn_(std::move(other.fn_)), active_(other.active_) {
    other.active_ = false;
  }
};

// Run a function when exiting the current scope. Used like this:
// auto on_scope_leave = OnScopeLeave([&] {
//   // ... run some code ...
// });
template <typename Fn>
inline MUST_USE_RESULT OnScopeLeaveImpl<Fn> OnScopeLeave(Fn&& fn) {
  return OnScopeLeaveImpl<Fn>{std::move(fn)};
}

// Simple RAII wrapper for contiguous data that uses malloc()/free().
template <typename T>
struct MallocedBuffer {
  T* data;
  size_t size;

  T* release() {
    T* ret = data;
    data = nullptr;
    return ret;
  }

  void Truncate(size_t new_size) {
    CHECK_LE(new_size, size);
    size = new_size;
  }

  void Realloc(size_t new_size) {
    Truncate(new_size);
    data = UncheckedRealloc(data, new_size);
  }

  bool is_empty() const { return data == nullptr; }

  MallocedBuffer() : data(nullptr), size(0) {}
  explicit MallocedBuffer(size_t size) : data(Malloc<T>(size)), size(size) {}
  MallocedBuffer(T* data, size_t size) : data(data), size(size) {}
  MallocedBuffer(MallocedBuffer&& other) : data(other.data), size(other.size) {
    other.data = nullptr;
  }
  MallocedBuffer& operator=(MallocedBuffer&& other) {
    this->~MallocedBuffer();
    return *new(this) MallocedBuffer(std::move(other));
  }
  ~MallocedBuffer() {
    free(data);
  }
  MallocedBuffer(const MallocedBuffer&) = delete;
  MallocedBuffer& operator=(const MallocedBuffer&) = delete;
};

template <typename T>
class NonCopyableMaybe {
 public:
  NonCopyableMaybe() : empty_(true) {}
  explicit NonCopyableMaybe(T&& value)
      : empty_(false),
        value_(std::move(value)) {}

  bool IsEmpty() const {
    return empty_;
  }

  const T* get() const {
    return empty_ ? nullptr : &value_;
  }

  const T* operator->() const {
    CHECK(!empty_);
    return &value_;
  }

  T&& Release() {
    CHECK_EQ(empty_, false);
    empty_ = true;
    return std::move(value_);
  }

 private:
  bool empty_;
  T value_;
};

// Test whether some value can be called with ().
template <typename T, typename = void>
struct is_callable : std::is_function<T> { };

template <typename T>
struct is_callable<T, typename std::enable_if<
    std::is_same<decltype(void(&T::operator())), void>::value
    >::type> : std::true_type { };

template <typename T, void (*function)(T*)>
struct FunctionDeleter {
  void operator()(T* pointer) const { function(pointer); }
  typedef std::unique_ptr<T, FunctionDeleter> Pointer;
};

template <typename T, void (*function)(T*)>
using DeleteFnPtr = typename FunctionDeleter<T, function>::Pointer;

// Convert a v8::Array into an std::vector using the callback-based API.
// This can be faster than calling Array::Get() repeatedly when the array
// has more than 2 entries.
// Note that iterating over an array in C++ and performing operations on each
// element in a C++ loop is still slower than iterating over the array in JS
// and calling into native in the JS loop repeatedly on each element,
// as of V8 11.9.
inline v8::Maybe<void> FromV8Array(v8::Local<v8::Context> context,
                                   v8::Local<v8::Array> js_array,
                                   std::vector<v8::Global<v8::Value>>* out);
std::vector<std::string_view> SplitString(const std::string_view in,
                                          const std::string_view delim);

inline v8::MaybeLocal<v8::Value> ToV8Value(v8::Local<v8::Context> context,
                                           std::string_view str,
                                           v8::Isolate* isolate = nullptr);
template <typename T, typename test_for_number =
    typename std::enable_if<std::numeric_limits<T>::is_specialized, bool>::type>
inline v8::MaybeLocal<v8::Value> ToV8Value(v8::Local<v8::Context> context,
                                           const T& number,
                                           v8::Isolate* isolate = nullptr);
template <typename T>
inline v8::MaybeLocal<v8::Value> ToV8Value(v8::Local<v8::Context> context,
                                           const std::vector<T>& vec,
                                           v8::Isolate* isolate = nullptr);
template <typename T>
inline v8::MaybeLocal<v8::Value> ToV8Value(v8::Local<v8::Context> context,
                                           const std::set<T>& set,
                                           v8::Isolate* isolate = nullptr);
template <typename T, typename U>
inline v8::MaybeLocal<v8::Value> ToV8Value(v8::Local<v8::Context> context,
                                           const std::unordered_map<T, U>& map,
                                           v8::Isolate* isolate = nullptr);

// These macros expects a `Isolate* isolate` and a `Local<Context> context`
// to be in the scope.
#define READONLY_PROPERTY(obj, name, value)                                    \
  do {                                                                         \
    obj->DefineOwnProperty(                                                    \
           context, FIXED_ONE_BYTE_STRING(isolate, name), value, v8::ReadOnly) \
        .Check();                                                              \
  } while (0)

#define READONLY_DONT_ENUM_PROPERTY(obj, name, var)                            \
  do {                                                                         \
    obj->DefineOwnProperty(                                                    \
           context,                                                            \
           OneByteString(isolate, name),                                       \
           var,                                                                \
           static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum))    \
        .Check();                                                              \
  } while (0)

#define READONLY_FALSE_PROPERTY(obj, name)                                     \
  READONLY_PROPERTY(obj, name, v8::False(isolate))

#define READONLY_TRUE_PROPERTY(obj, name)                                      \
  READONLY_PROPERTY(obj, name, v8::True(isolate))

#define READONLY_STRING_PROPERTY(obj, name, str)                               \
  READONLY_PROPERTY(obj, name, ToV8Value(context, str).ToLocalChecked())

// Variation on NODE_DEFINE_CONSTANT that sets a String value.
#define NODE_DEFINE_STRING_CONSTANT(target, name, constant)                    \
  do {                                                                         \
    v8::Isolate* isolate = target->GetIsolate();                               \
    v8::Local<v8::String> constant_name =                                      \
        v8::String::NewFromUtf8(isolate, name).ToLocalChecked();               \
    v8::Local<v8::String> constant_value =                                     \
        v8::String::NewFromUtf8(isolate, constant).ToLocalChecked();           \
    v8::PropertyAttribute constant_attributes =                                \
        static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete);     \
    target                                                                     \
        ->DefineOwnProperty(isolate->GetCurrentContext(),                      \
                            constant_name,                                     \
                            constant_value,                                    \
                            constant_attributes)                               \
        .Check();                                                              \
  } while (0)

constexpr inline bool IsLittleEndian() {
  return std::endian::native == std::endian::little;
}

constexpr inline bool IsBigEndian() {
  return std::endian::native == std::endian::big;
}

static_assert(IsLittleEndian() || IsBigEndian(),
              "Node.js does not support mixed-endian systems");

class SlicedArguments : public MaybeStackBuffer<v8::Local<v8::Value>> {
 public:
  inline explicit SlicedArguments(
      const v8::FunctionCallbackInfo<v8::Value>& args, size_t start = 0);
};

// Convert a v8::PersistentBase, e.g. v8::Global, to a Local, with an extra
// optimization for strong persistent handles.
class PersistentToLocal {
 public:
  // If persistent.IsWeak() == false, then do not call persistent.Reset()
  // while the returned Local<T> is still in scope, it will destroy the
  // reference to the object.
  template <class TypeName>
  static inline v8::Local<TypeName> Default(
      v8::Isolate* isolate,
      const v8::PersistentBase<TypeName>& persistent) {
    if (persistent.IsWeak()) {
      return PersistentToLocal::Weak(isolate, persistent);
    } else {
      return PersistentToLocal::Strong(persistent);
    }
  }

  // Unchecked conversion from a non-weak Persistent<T> to Local<T>,
  // use with care!
  //
  // Do not call persistent.Reset() while the returned Local<T> is still in
  // scope, it will destroy the reference to the object.
  template <class TypeName>
  static inline v8::Local<TypeName> Strong(
      const v8::PersistentBase<TypeName>& persistent) {
    DCHECK(!persistent.IsWeak());
    return *reinterpret_cast<v8::Local<TypeName>*>(
        const_cast<v8::PersistentBase<TypeName>*>(&persistent));
  }

  template <class TypeName>
  static inline v8::Local<TypeName> Weak(
      v8::Isolate* isolate,
      const v8::PersistentBase<TypeName>& persistent) {
    return v8::Local<TypeName>::New(isolate, persistent);
  }
};

// Can be used as a key for std::unordered_map when lookup performance is more
// important than size and the keys are statically used to avoid redundant hash
// computations.
class FastStringKey {
 public:
  constexpr explicit FastStringKey(std::string_view name);

  struct Hash {
    constexpr size_t operator()(const FastStringKey& key) const;
  };
  constexpr bool operator==(const FastStringKey& other) const;

  constexpr std::string_view as_string_view() const;

 private:
  static constexpr size_t HashImpl(std::string_view str);

  const std::string_view name_;
  const size_t cached_hash_;
};

// Like std::static_pointer_cast but for unique_ptr with the default deleter.
template <typename T, typename U>
std::unique_ptr<T> static_unique_pointer_cast(std::unique_ptr<U>&& ptr) {
  return std::unique_ptr<T>(static_cast<T*>(ptr.release()));
}

#define MAYBE_FIELD_PTR(ptr, field) ptr == nullptr ? nullptr : &(ptr->field)

// Returns a non-zero code if it fails to open or read the file,
// aborts if it fails to close the file.
int ReadFileSync(std::string* result, const char* path);
// Reads all contents of a FILE*, aborts if it fails.
std::vector<char> ReadFileSync(FILE* fp);

v8::Local<v8::FunctionTemplate> NewFunctionTemplate(
    v8::Isolate* isolate,
    v8::FunctionCallback callback,
    v8::Local<v8::Signature> signature = v8::Local<v8::Signature>(),
    v8::ConstructorBehavior behavior = v8::ConstructorBehavior::kAllow,
    v8::SideEffectType side_effect = v8::SideEffectType::kHasSideEffect,
    const v8::CFunction* c_function = nullptr);

// Convenience methods for NewFunctionTemplate().
void SetMethod(v8::Local<v8::Context> context,
               v8::Local<v8::Object> that,
               const std::string_view name,
               v8::FunctionCallback callback);
// Similar to SetProtoMethod but without receiver signature checks.
void SetMethod(v8::Isolate* isolate,
               v8::Local<v8::Template> that,
               const std::string_view name,
               v8::FunctionCallback callback);

void SetFastMethod(v8::Isolate* isolate,
                   v8::Local<v8::Template> that,
                   const std::string_view name,
                   v8::FunctionCallback slow_callback,
                   const v8::CFunction* c_function);
void SetFastMethod(v8::Local<v8::Context> context,
                   v8::Local<v8::Object> that,
                   const std::string_view name,
                   v8::FunctionCallback slow_callback,
                   const v8::CFunction* c_function);
void SetFastMethod(v8::Isolate* isolate,
                   v8::Local<v8::Template> that,
                   const std::string_view name,
                   v8::FunctionCallback slow_callback,
                   const v8::MemorySpan<const v8::CFunction>& methods);
void SetFastMethodNoSideEffect(v8::Isolate* isolate,
                               v8::Local<v8::Template> that,
                               const std::string_view name,
                               v8::FunctionCallback slow_callback,
                               const v8::CFunction* c_function);
void SetFastMethodNoSideEffect(v8::Local<v8::Context> context,
                               v8::Local<v8::Object> that,
                               const std::string_view name,
                               v8::FunctionCallback slow_callback,
                               const v8::CFunction* c_function);
void SetFastMethodNoSideEffect(
    v8::Isolate* isolate,
    v8::Local<v8::Template> that,
    const std::string_view name,
    v8::FunctionCallback slow_callback,
    const v8::MemorySpan<const v8::CFunction>& methods);
void SetProtoMethod(v8::Isolate* isolate,
                    v8::Local<v8::FunctionTemplate> that,
                    const std::string_view name,
                    v8::FunctionCallback callback);

void SetInstanceMethod(v8::Isolate* isolate,
                       v8::Local<v8::FunctionTemplate> that,
                       const std::string_view name,
                       v8::FunctionCallback callback);

// Safe variants denote the function has no side effects.
void SetMethodNoSideEffect(v8::Local<v8::Context> context,
                           v8::Local<v8::Object> that,
                           const std::string_view name,
                           v8::FunctionCallback callback);
void SetProtoMethodNoSideEffect(v8::Isolate* isolate,
                                v8::Local<v8::FunctionTemplate> that,
                                const std::string_view name,
                                v8::FunctionCallback callback);
void SetMethodNoSideEffect(v8::Isolate* isolate,
                           v8::Local<v8::Template> that,
                           const std::string_view name,
                           v8::FunctionCallback callback);

enum class SetConstructorFunctionFlag {
  NONE,
  SET_CLASS_NAME,
};

void SetConstructorFunction(v8::Local<v8::Context> context,
                            v8::Local<v8::Object> that,
                            const char* name,
                            v8::Local<v8::FunctionTemplate> tmpl,
                            SetConstructorFunctionFlag flag =
                                SetConstructorFunctionFlag::SET_CLASS_NAME);

void SetConstructorFunction(v8::Local<v8::Context> context,
                            v8::Local<v8::Object> that,
                            v8::Local<v8::String> name,
                            v8::Local<v8::FunctionTemplate> tmpl,
                            SetConstructorFunctionFlag flag =
                                SetConstructorFunctionFlag::SET_CLASS_NAME);

void SetConstructorFunction(v8::Isolate* isolate,
                            v8::Local<v8::Template> that,
                            const char* name,
                            v8::Local<v8::FunctionTemplate> tmpl,
                            SetConstructorFunctionFlag flag =
                                SetConstructorFunctionFlag::SET_CLASS_NAME);

void SetConstructorFunction(v8::Isolate* isolate,
                            v8::Local<v8::Template> that,
                            v8::Local<v8::String> name,
                            v8::Local<v8::FunctionTemplate> tmpl,
                            SetConstructorFunctionFlag flag =
                                SetConstructorFunctionFlag::SET_CLASS_NAME);

// Like RAIIIsolate, except doesn't enter the isolate while it's in scope.
class RAIIIsolateWithoutEntering {
 public:
  explicit RAIIIsolateWithoutEntering(const SnapshotData* data = nullptr);
  ~RAIIIsolateWithoutEntering();

  v8::Isolate* get() const { return isolate_; }

 private:
  std::unique_ptr<v8::ArrayBuffer::Allocator> allocator_;
  v8::Isolate* isolate_;
};

// Simple RAII class to spin up a v8::Isolate instance and enter it
// immediately.
class RAIIIsolate {
 public:
  explicit RAIIIsolate(const SnapshotData* data = nullptr);
  ~RAIIIsolate();

  v8::Isolate* get() const { return isolate_.get(); }

 private:
  RAIIIsolateWithoutEntering isolate_;
  v8::Isolate::Scope isolate_scope_;
};

std::string DetermineSpecificErrorType(Environment* env,
                                       v8::Local<v8::Value> input);

v8::Maybe<int32_t> GetValidatedFd(Environment* env, v8::Local<v8::Value> input);
v8::Maybe<int> GetValidFileMode(Environment* env,
                                v8::Local<v8::Value> input,
                                uv_fs_type type);

// Returns true if OS==Windows and filename ends in .bat or .cmd,
// case insensitive.
inline bool IsWindowsBatchFile(const char* filename);

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_UTIL_H_

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

#ifndef SRC_UTIL_INL_H_
#define SRC_UTIL_INL_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <cmath>
#include <cstring>
#include <locale>
#include <regex>  // NOLINT(build/c++11)
#include "node_revert.h"
#include "util.h"

#define CHAR_TEST(bits, name, expr)                                           \
  template <typename T>                                                       \
  bool name(const T ch) {                                                     \
    static_assert(sizeof(ch) >= (bits) / 8,                                   \
                  "Character must be wider than " #bits " bits");             \
    return (expr);                                                            \
  }

namespace node {

template <typename T>
ListNode<T>::ListNode() : prev_(this), next_(this) {}

template <typename T>
ListNode<T>::~ListNode() {
  Remove();
}

template <typename T>
void ListNode<T>::Remove() {
  prev_->next_ = next_;
  next_->prev_ = prev_;
  prev_ = this;
  next_ = this;
}

template <typename T>
bool ListNode<T>::IsEmpty() const {
  return prev_ == this;
}

template <typename T, ListNode<T> (T::*M)>
ListHead<T, M>::Iterator::Iterator(ListNode<T>* node) : node_(node) {}

template <typename T, ListNode<T> (T::*M)>
T* ListHead<T, M>::Iterator::operator*() const {
  return ContainerOf(M, node_);
}

template <typename T, ListNode<T> (T::*M)>
const typename ListHead<T, M>::Iterator&
ListHead<T, M>::Iterator::operator++() {
  node_ = node_->next_;
  return *this;
}

template <typename T, ListNode<T> (T::*M)>
bool ListHead<T, M>::Iterator::operator!=(const Iterator& that) const {
  return node_ != that.node_;
}

template <typename T, ListNode<T> (T::*M)>
ListHead<T, M>::~ListHead() {
  while (IsEmpty() == false)
    head_.next_->Remove();
}

template <typename T, ListNode<T> (T::*M)>
void ListHead<T, M>::PushBack(T* element) {
  ListNode<T>* that = &(element->*M);
  head_.prev_->next_ = that;
  that->prev_ = head_.prev_;
  that->next_ = &head_;
  head_.prev_ = that;
}

template <typename T, ListNode<T> (T::*M)>
void ListHead<T, M>::PushFront(T* element) {
  ListNode<T>* that = &(element->*M);
  head_.next_->prev_ = that;
  that->prev_ = &head_;
  that->next_ = head_.next_;
  head_.next_ = that;
}

template <typename T, ListNode<T> (T::*M)>
bool ListHead<T, M>::IsEmpty() const {
  return head_.IsEmpty();
}

template <typename T, ListNode<T> (T::*M)>
T* ListHead<T, M>::PopFront() {
  if (IsEmpty())
    return nullptr;
  ListNode<T>* node = head_.next_;
  node->Remove();
  return ContainerOf(M, node);
}

template <typename T, ListNode<T> (T::*M)>
typename ListHead<T, M>::Iterator ListHead<T, M>::begin() const {
  return Iterator(head_.next_);
}

template <typename T, ListNode<T> (T::*M)>
typename ListHead<T, M>::Iterator ListHead<T, M>::end() const {
  return Iterator(const_cast<ListNode<T>*>(&head_));
}

template <typename Inner, typename Outer>
constexpr uintptr_t OffsetOf(Inner Outer::*field) {
  return reinterpret_cast<uintptr_t>(&(static_cast<Outer*>(nullptr)->*field));
}

template <typename Inner, typename Outer>
ContainerOfHelper<Inner, Outer>::ContainerOfHelper(Inner Outer::*field,
                                                   Inner* pointer)
    : pointer_(
        reinterpret_cast<Outer*>(
            reinterpret_cast<uintptr_t>(pointer) - OffsetOf(field))) {}

template <typename Inner, typename Outer>
template <typename TypeName>
ContainerOfHelper<Inner, Outer>::operator TypeName*() const {
  return static_cast<TypeName*>(pointer_);
}

template <typename Inner, typename Outer>
constexpr ContainerOfHelper<Inner, Outer> ContainerOf(Inner Outer::*field,
                                                      Inner* pointer) {
  return ContainerOfHelper<Inner, Outer>(field, pointer);
}

inline v8::Local<v8::String> OneByteString(v8::Isolate* isolate,
                                           const char* data,
                                           int length) {
  return v8::String::NewFromOneByte(isolate,
                                    reinterpret_cast<const uint8_t*>(data),
                                    v8::NewStringType::kNormal,
                                    length).ToLocalChecked();
}

inline v8::Local<v8::String> OneByteString(v8::Isolate* isolate,
                                           const signed char* data,
                                           int length) {
  return v8::String::NewFromOneByte(isolate,
                                    reinterpret_cast<const uint8_t*>(data),
                                    v8::NewStringType::kNormal,
                                    length).ToLocalChecked();
}

inline v8::Local<v8::String> OneByteString(v8::Isolate* isolate,
                                           const unsigned char* data,
                                           int length) {
  return v8::String::NewFromOneByte(
             isolate, data, v8::NewStringType::kNormal, length)
      .ToLocalChecked();
}

inline v8::Local<v8::String> OneByteString(v8::Isolate* isolate,
                                           std::string_view str) {
  return OneByteString(isolate, str.data(), str.size());
}

char ToLower(char c) {
  return std::tolower(c, std::locale::classic());
}

std::string ToLower(const std::string& in) {
  std::string out(in.size(), 0);
  for (size_t i = 0; i < in.size(); ++i)
    out[i] = ToLower(in[i]);
  return out;
}

char ToUpper(char c) {
  return std::toupper(c, std::locale::classic());
}

std::string ToUpper(const std::string& in) {
  std::string out(in.size(), 0);
  for (size_t i = 0; i < in.size(); ++i)
    out[i] = ToUpper(in[i]);
  return out;
}

bool StringEqualNoCase(const char* a, const char* b) {
  while (ToLower(*a) == ToLower(*b++)) {
    if (*a++ == '\0')
      return true;
  }
  return false;
}

bool StringEqualNoCaseN(const char* a, const char* b, size_t length) {
  for (size_t i = 0; i < length; i++) {
    if (ToLower(a[i]) != ToLower(b[i]))
      return false;
    if (a[i] == '\0')
      return true;
  }
  return true;
}

template <typename T>
inline T MultiplyWithOverflowCheck(T a, T b) {
  auto ret = a * b;
  if (a != 0)
    CHECK_EQ(b, ret / a);

  return ret;
}

// These should be used in our code as opposed to the native
// versions as they abstract out some platform and or
// compiler version specific functionality.
// malloc(0) and realloc(ptr, 0) have implementation-defined behavior in
// that the standard allows them to either return a unique pointer or a
// nullptr for zero-sized allocation requests.  Normalize by always using
// a nullptr.
template <typename T>
T* UncheckedRealloc(T* pointer, size_t n) {
  size_t full_size = MultiplyWithOverflowCheck(sizeof(T), n);

  if (full_size == 0) {
    free(pointer);
    return nullptr;
  }

  void* allocated = realloc(pointer, full_size);

  if (allocated == nullptr) [[unlikely]] {
    // Tell V8 that memory is low and retry.
    LowMemoryNotification();
    allocated = realloc(pointer, full_size);
  }

  return static_cast<T*>(allocated);
}

// As per spec realloc behaves like malloc if passed nullptr.
template <typename T>
inline T* UncheckedMalloc(size_t n) {
  return UncheckedRealloc<T>(nullptr, n);
}

template <typename T>
inline T* UncheckedCalloc(size_t n) {
  if (MultiplyWithOverflowCheck(sizeof(T), n) == 0) return nullptr;
  return static_cast<T*>(calloc(n, sizeof(T)));
}

template <typename T>
inline T* Realloc(T* pointer, size_t n) {
  T* ret = UncheckedRealloc(pointer, n);
  CHECK_IMPLIES(n > 0, ret != nullptr);
  return ret;
}

template <typename T>
inline T* Malloc(size_t n) {
  T* ret = UncheckedMalloc<T>(n);
  CHECK_IMPLIES(n > 0, ret != nullptr);
  return ret;
}

template <typename T>
inline T* Calloc(size_t n) {
  T* ret = UncheckedCalloc<T>(n);
  CHECK_IMPLIES(n > 0, ret != nullptr);
  return ret;
}

// Shortcuts for char*.
inline char* Malloc(size_t n) { return Malloc<char>(n); }
inline char* Calloc(size_t n) { return Calloc<char>(n); }
inline char* UncheckedMalloc(size_t n) { return UncheckedMalloc<char>(n); }
inline char* UncheckedCalloc(size_t n) { return UncheckedCalloc<char>(n); }

// This is a helper in the .cc file so including util-inl.h doesn't include more
// headers than we really need to.
void ThrowErrStringTooLong(v8::Isolate* isolate);

struct ArrayIterationData {
  std::vector<v8::Global<v8::Value>>* out;
  v8::Isolate* isolate = nullptr;
};

inline v8::Array::CallbackResult PushItemToVector(uint32_t index,
                                                  v8::Local<v8::Value> element,
                                                  void* data) {
  auto vec = static_cast<ArrayIterationData*>(data)->out;
  auto isolate = static_cast<ArrayIterationData*>(data)->isolate;
  vec->push_back(v8::Global<v8::Value>(isolate, element));
  return v8::Array::CallbackResult::kContinue;
}

v8::Maybe<void> FromV8Array(v8::Local<v8::Context> context,
                            v8::Local<v8::Array> js_array,
                            std::vector<v8::Global<v8::Value>>* out) {
  uint32_t count = js_array->Length();
  out->reserve(count);
  ArrayIterationData data{out, context->GetIsolate()};
  return js_array->Iterate(context, PushItemToVector, &data);
}

v8::MaybeLocal<v8::Value> ToV8Value(v8::Local<v8::Context> context,
                                    std::string_view str,
                                    v8::Isolate* isolate) {
  if (isolate == nullptr) isolate = context->GetIsolate();
  if (str.size() >= static_cast<size_t>(v8::String::kMaxLength)) [[unlikely]] {
    // V8 only has a TODO comment about adding an exception when the maximum
    // string size is exceeded.
    ThrowErrStringTooLong(isolate);
    return v8::MaybeLocal<v8::Value>();
  }

  return v8::String::NewFromUtf8(
             isolate, str.data(), v8::NewStringType::kNormal, str.size())
      .FromMaybe(v8::Local<v8::String>());
}

template <typename T>
v8::MaybeLocal<v8::Value> ToV8Value(v8::Local<v8::Context> context,
                                    const std::vector<T>& vec,
                                    v8::Isolate* isolate) {
  if (isolate == nullptr) isolate = context->GetIsolate();
  v8::EscapableHandleScope handle_scope(isolate);

  MaybeStackBuffer<v8::Local<v8::Value>, 128> arr(vec.size());
  arr.SetLength(vec.size());
  for (size_t i = 0; i < vec.size(); ++i) {
    if (!ToV8Value(context, vec[i], isolate).ToLocal(&arr[i]))
      return v8::MaybeLocal<v8::Value>();
  }

  return handle_scope.Escape(v8::Array::New(isolate, arr.out(), arr.length()));
}

template <typename T>
v8::MaybeLocal<v8::Value> ToV8Value(v8::Local<v8::Context> context,
                                    const std::set<T>& set,
                                    v8::Isolate* isolate) {
  if (isolate == nullptr) isolate = context->GetIsolate();
  v8::Local<v8::Set> set_js = v8::Set::New(isolate);
  v8::HandleScope handle_scope(isolate);

  for (const T& entry : set) {
    v8::Local<v8::Value> value;
    if (!ToV8Value(context, entry, isolate).ToLocal(&value))
      return {};
    if (set_js->Add(context, value).IsEmpty())
      return {};
  }

  return set_js;
}

template <typename T, typename U>
v8::MaybeLocal<v8::Value> ToV8Value(v8::Local<v8::Context> context,
                                    const std::unordered_map<T, U>& map,
                                    v8::Isolate* isolate) {
  if (isolate == nullptr) isolate = context->GetIsolate();
  v8::EscapableHandleScope handle_scope(isolate);

  v8::Local<v8::Map> ret = v8::Map::New(isolate);
  for (const auto& item : map) {
    v8::Local<v8::Value> first, second;
    if (!ToV8Value(context, item.first, isolate).ToLocal(&first) ||
        !ToV8Value(context, item.second, isolate).ToLocal(&second) ||
        ret->Set(context, first, second).IsEmpty()) {
      return v8::MaybeLocal<v8::Value>();
    }
  }

  return handle_scope.Escape(ret);
}

template <typename T, typename >
v8::MaybeLocal<v8::Value> ToV8Value(v8::Local<v8::Context> context,
                                    const T& number,
                                    v8::Isolate* isolate) {
  if (isolate == nullptr) isolate = context->GetIsolate();

  using Limits = std::numeric_limits<T>;
  // Choose Uint32, Int32, or Double depending on range checks.
  // These checks should all collapse at compile time.
  if (static_cast<uint32_t>(Limits::max()) <=
          std::numeric_limits<uint32_t>::max() &&
      static_cast<uint32_t>(Limits::min()) >=
          std::numeric_limits<uint32_t>::min() && Limits::is_exact) {
    return v8::Integer::NewFromUnsigned(isolate, static_cast<uint32_t>(number));
  }

  if (static_cast<int32_t>(Limits::max()) <=
          std::numeric_limits<int32_t>::max() &&
      static_cast<int32_t>(Limits::min()) >=
          std::numeric_limits<int32_t>::min() && Limits::is_exact) {
    return v8::Integer::New(isolate, static_cast<int32_t>(number));
  }

  return v8::Number::New(isolate, static_cast<double>(number));
}

SlicedArguments::SlicedArguments(
    const v8::FunctionCallbackInfo<v8::Value>& args, size_t start) {
  const size_t length = static_cast<size_t>(args.Length());
  if (start >= length) return;
  const size_t size = length - start;

  AllocateSufficientStorage(size);
  for (size_t i = 0; i < size; ++i)
    (*this)[i] = args[i + start];
}

template <typename T, size_t kStackStorageSize>
void MaybeStackBuffer<T, kStackStorageSize>::AllocateSufficientStorage(
    size_t storage) {
  CHECK(!IsInvalidated());
  if (storage > capacity()) {
    bool was_allocated = IsAllocated();
    T* allocated_ptr = was_allocated ? buf_ : nullptr;
    buf_ = Realloc(allocated_ptr, storage);
    capacity_ = storage;
    if (!was_allocated && length_ > 0)
      memcpy(buf_, buf_st_, length_ * sizeof(buf_[0]));
  }

  length_ = storage;
}

template <typename T, size_t S>
ArrayBufferViewContents<T, S>::ArrayBufferViewContents(
    v8::Local<v8::Value> value) {
  DCHECK(value->IsArrayBufferView() || value->IsSharedArrayBuffer() ||
         value->IsArrayBuffer());
  ReadValue(value);
}

template <typename T, size_t S>
ArrayBufferViewContents<T, S>::ArrayBufferViewContents(
    v8::Local<v8::Object> value) {
  CHECK(value->IsArrayBufferView());
  Read(value.As<v8::ArrayBufferView>());
}

template <typename T, size_t S>
ArrayBufferViewContents<T, S>::ArrayBufferViewContents(
    v8::Local<v8::ArrayBufferView> abv) {
  Read(abv);
}

template <typename T, size_t S>
void ArrayBufferViewContents<T, S>::Read(v8::Local<v8::ArrayBufferView> abv) {
  static_assert(sizeof(T) == 1, "Only supports one-byte data at the moment");
  length_ = abv->ByteLength();
  if (length_ > sizeof(stack_storage_) || abv->HasBuffer()) {
    data_ = static_cast<T*>(abv->Buffer()->Data()) + abv->ByteOffset();
  } else {
    abv->CopyContents(stack_storage_, sizeof(stack_storage_));
    data_ = stack_storage_;
  }
}

template <typename T, size_t S>
void ArrayBufferViewContents<T, S>::ReadValue(v8::Local<v8::Value> buf) {
  static_assert(sizeof(T) == 1, "Only supports one-byte data at the moment");
  DCHECK(buf->IsArrayBufferView() || buf->IsSharedArrayBuffer() ||
         buf->IsArrayBuffer());

  if (buf->IsArrayBufferView()) {
    Read(buf.As<v8::ArrayBufferView>());
  } else if (buf->IsArrayBuffer()) {
    auto ab = buf.As<v8::ArrayBuffer>();
    length_ = ab->ByteLength();
    data_ = static_cast<T*>(ab->Data());
    was_detached_ = ab->WasDetached();
  } else {
    CHECK(buf->IsSharedArrayBuffer());
    auto sab = buf.As<v8::SharedArrayBuffer>();
    length_ = sab->ByteLength();
    data_ = static_cast<T*>(sab->Data());
  }
}

// ECMA-262, 15th edition, 21.1.2.5. Number.isSafeInteger
inline bool IsSafeJsInt(v8::Local<v8::Value> v) {
  if (!v->IsNumber()) return false;
  double v_d = v.As<v8::Number>()->Value();
  if (std::isnan(v_d)) return false;
  if (std::isinf(v_d)) return false;
  if (std::trunc(v_d) != v_d) return false;  // not int
  if (std::abs(v_d) <= static_cast<double>(kMaxSafeJsInteger)) return true;
  return false;
}

constexpr size_t FastStringKey::HashImpl(std::string_view str) {
  // Low-quality hash (djb2), but just fine for current use cases.
  size_t h = 5381;
  for (const char c : str) {
    h = h * 33 + c;
  }
  return h;
}

constexpr size_t FastStringKey::Hash::operator()(
    const FastStringKey& key) const {
  return key.cached_hash_;
}

constexpr bool FastStringKey::operator==(const FastStringKey& other) const {
  return name_ == other.name_;
}

constexpr FastStringKey::FastStringKey(std::string_view name)
    : name_(name), cached_hash_(HashImpl(name)) {}

constexpr std::string_view FastStringKey::as_string_view() const {
  return name_;
}

// Inline so the compiler can fully optimize it away on Unix platforms.
bool IsWindowsBatchFile(const char* filename) {
#ifdef _WIN32
  std::string file_with_extension = filename;
  // Regex to match the last extension part after the last dot, ignoring
  // trailing spaces and dots
  std::regex extension_regex(R"(\.([a-zA-Z0-9]+)\s*[\.\s]*$)");
  std::smatch match;
  std::string extension;

  if (std::regex_search(file_with_extension, match, extension_regex)) {
    extension = ToLower(match[1].str());
  }

  return !extension.empty() && (extension == "cmd" || extension == "bat");
#else
  return false;
#endif  // _WIN32
}

#ifdef _WIN32
inline std::wstring ConvertToWideString(const std::string& str,
                                        UINT code_page) {
  int size_needed = MultiByteToWideChar(
      code_page, 0, &str[0], static_cast<int>(str.size()), nullptr, 0);
  std::wstring wstrTo(size_needed, 0);
  MultiByteToWideChar(code_page,
                      0,
                      &str[0],
                      static_cast<int>(str.size()),
                      &wstrTo[0],
                      size_needed);
  return wstrTo;
}
#endif  // _WIN32

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_UTIL_INL_H_

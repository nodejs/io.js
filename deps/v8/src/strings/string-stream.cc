// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/strings/string-stream.h"

#include <memory>

#include "src/base/vector.h"
#include "src/handles/handles-inl.h"
#include "src/logging/log.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/prototype.h"

namespace v8 {
namespace internal {

static const int kMentionedObjectCacheMaxSize = 256;

char* HeapStringAllocator::allocate(unsigned bytes) {
  space_ = NewArray<char>(bytes);
  return space_;
}

char* FixedStringAllocator::allocate(unsigned bytes) {
  CHECK_LE(bytes, length_);
  return buffer_;
}

char* FixedStringAllocator::grow(unsigned* old) {
  *old = length_;
  return buffer_;
}

bool StringStream::Put(char c) {
  if (full()) return false;
  DCHECK(length_ < capacity_);
  // Since the trailing '\0' is not accounted for in length_ fullness is
  // indicated by a difference of 1 between length_ and capacity_. Thus when
  // reaching a difference of 2 we need to grow the buffer.
  if (length_ == capacity_ - 2) {
    unsigned new_capacity = capacity_;
    char* new_buffer = allocator_->grow(&new_capacity);
    if (new_capacity > capacity_) {
      capacity_ = new_capacity;
      buffer_ = new_buffer;
    } else {
      // Reached the end of the available buffer.
      DCHECK_GE(capacity_, 5);
      length_ = capacity_ - 1;  // Indicate fullness of the stream.
      buffer_[length_ - 4] = '.';
      buffer_[length_ - 3] = '.';
      buffer_[length_ - 2] = '.';
      buffer_[length_ - 1] = '\n';
      buffer_[length_] = '\0';
      return false;
    }
  }
  buffer_[length_] = c;
  buffer_[length_ + 1] = '\0';
  length_++;
  return true;
}

// A control character is one that configures a format element.  For
// instance, in %.5s, .5 are control characters.
static bool IsControlChar(char c) {
  switch (c) {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '.':
    case '-':
      return true;
    default:
      return false;
  }
}

void StringStream::Add(base::Vector<const char> format,
                       base::Vector<FmtElm> elms) {
  // If we already ran out of space then return immediately.
  if (full()) return;
  int offset = 0;
  int elm = 0;
  while (offset < format.length()) {
    if (format[offset] != '%' || elm == elms.length()) {
      Put(format[offset]);
      offset++;
      continue;
    }
    // Read this formatting directive into a temporary buffer
    base::EmbeddedVector<char, 24> temp;
    int format_length = 0;
    // Skip over the whole control character sequence until the
    // format element type
    temp[format_length++] = format[offset++];
    while (offset < format.length() && IsControlChar(format[offset]))
      temp[format_length++] = format[offset++];
    if (offset >= format.length()) return;
    char type = format[offset];
    temp[format_length++] = type;
    temp[format_length] = '\0';
    offset++;
    FmtElm current = elms[elm++];
    switch (type) {
      case 's': {
        DCHECK_EQ(FmtElm::C_STR, current.type_);
        const char* value = current.data_.u_c_str_;
        Add(value);
        break;
      }
      case 'w': {
        DCHECK_EQ(FmtElm::LC_STR, current.type_);
        base::Vector<const base::uc16> value = *current.data_.u_lc_str_;
        for (int i = 0; i < value.length(); i++)
          Put(static_cast<char>(value[i]));
        break;
      }
      case 'o': {
        DCHECK_EQ(FmtElm::OBJ, current.type_);
        Tagged<Object> obj(current.data_.u_obj_);
        PrintObject(obj);
        break;
      }
      case 'k': {
        DCHECK_EQ(FmtElm::INT, current.type_);
        int value = current.data_.u_int_;
        if (0x20 <= value && value <= 0x7F) {
          Put(value);
        } else if (value <= 0xFF) {
          Add("\\x%02x", value);
        } else {
          Add("\\u%04x", value);
        }
        break;
      }
      case 'i':
      case 'd':
      case 'u':
      case 'x':
      case 'c':
      case 'X': {
        int value = current.data_.u_int_;
        base::EmbeddedVector<char, 24> formatted;
        int length = SNPrintF(formatted, temp.begin(), value);
        Add(base::Vector<const char>(formatted.begin(), length));
        break;
      }
      case 'f':
      case 'g':
      case 'G':
      case 'e':
      case 'E': {
        double value = current.data_.u_double_;
        int inf = std::isinf(value);
        if (inf == -1) {
          Add("-inf");
        } else if (inf == 1) {
          Add("inf");
        } else if (std::isnan(value)) {
          Add("nan");
        } else {
          base::EmbeddedVector<char, 28> formatted;
          SNPrintF(formatted, temp.begin(), value);
          Add(formatted.begin());
        }
        break;
      }
      case 'p': {
        void* value = current.data_.u_pointer_;
        base::EmbeddedVector<char, 20> formatted;
        SNPrintF(formatted, temp.begin(), value);
        Add(formatted.begin());
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  // Verify that the buffer is 0-terminated
  DCHECK_EQ(buffer_[length_], '\0');
}

void StringStream::PrintObject(Tagged<Object> o) {
  ShortPrint(o, this);
  if (IsString(o)) {
    if (Cast<String>(o)->length() <= String::kMaxShortPrintLength) {
      return;
    }
  } else if (IsNumber(o) || IsOddball(o)) {
    return;
  }
  if (IsHeapObject(o) && object_print_mode_ == kPrintObjectVerbose) {
    // TODO(delphick): Consider whether we can get the isolate without using
    // TLS.
    Isolate* isolate = Isolate::Current();
    DebugObjectCache* debug_object_cache =
        isolate->string_stream_debug_object_cache();
    for (size_t i = 0; i < debug_object_cache->size(); i++) {
      if (*(*debug_object_cache)[i] == o) {
        Add("#%d#", static_cast<int>(i));
        return;
      }
    }
    if (debug_object_cache->size() < kMentionedObjectCacheMaxSize) {
      Add("#%d#", static_cast<int>(debug_object_cache->size()));
      debug_object_cache->push_back(handle(Cast<HeapObject>(o), isolate));
    } else {
      Add("@%p", o);
    }
  }
}

std::unique_ptr<char[]> StringStream::ToCString() const {
  char* str = NewArray<char>(length_ + 1);
  MemCopy(str, buffer_, length_);
  str[length_] = '\0';
  return std::unique_ptr<char[]>(str);
}

void StringStream::Log(Isolate* isolate) {
  LOG(isolate, StringEvent("StackDump", buffer_));
}

void StringStream::OutputToFile(FILE* out) {
  // Dump the output to stdout, but make sure to break it up into
  // manageable chunks to avoid losing parts of the output in the OS
  // printing code. This is a problem on Windows in particular; see
  // the VPrint() function implementations in platform-win32.cc.
  unsigned position = 0;
  for (unsigned next; (next = position + 2048) < length_; position = next) {
    char save = buffer_[next];
    buffer_[next] = '\0';
    internal::PrintF(out, "%s", &buffer_[position]);
    buffer_[next] = save;
  }
  internal::PrintF(out, "%s", &buffer_[position]);
}

Handle<String> StringStream::ToString(Isolate* isolate) {
  return isolate->factory()
      ->NewStringFromUtf8(base::Vector<const char>(buffer_, length_))
      .ToHandleChecked();
}

void StringStream::ClearMentionedObjectCache(Isolate* isolate) {
  isolate->set_string_stream_current_security_token(Tagged<Object>());
  if (isolate->string_stream_debug_object_cache() == nullptr) {
    isolate->set_string_stream_debug_object_cache(new DebugObjectCache());
  }
  isolate->string_stream_debug_object_cache()->clear();
}

#ifdef DEBUG
bool StringStream::IsMentionedObjectCacheClear(Isolate* isolate) {
  return object_print_mode_ == kPrintObjectConcise ||
         isolate->string_stream_debug_object_cache()->size() == 0;
}
#endif

bool StringStream::Put(Tagged<String> str) {
  return Put(str, 0, str->length());
}

bool StringStream::Put(Tagged<String> str, int start, int end) {
  StringCharacterStream stream(str, start);
  for (int i = start; i < end && stream.HasMore(); i++) {
    uint16_t c = stream.GetNext();
    if (c >= 127 || c < 32) {
      c = '?';
    }
    if (!Put(static_cast<char>(c))) {
      return false;  // Output was truncated.
    }
  }
  return true;
}

void StringStream::PrintName(Tagged<Object> name) {
  if (IsString(name)) {
    Tagged<String> str = Cast<String>(name);
    if (str->length() > 0) {
      Put(str);
    } else {
      Add("/* anonymous */");
    }
  } else {
    Add("%o", name);
  }
}

void StringStream::PrintUsingMap(Tagged<JSObject> js_object) {
  Tagged<Map> map = js_object->map();
  Tagged<DescriptorArray> descs =
      map->instance_descriptors(js_object->GetIsolate());
  for (InternalIndex i : map->IterateOwnDescriptors()) {
    PropertyDetails details = descs->GetDetails(i);
    if (details.location() == PropertyLocation::kField) {
      DCHECK_EQ(PropertyKind::kData, details.kind());
      Tagged<Object> key = descs->GetKey(i);
      if (IsString(key) || IsNumber(key)) {
        int len = 3;
        if (IsString(key)) {
          len = Cast<String>(key)->length();
        }
        for (; len < 18; len++) Put(' ');
        if (IsString(key)) {
          Put(Cast<String>(key));
        } else {
          ShortPrint(key);
        }
        Add(": ");
        FieldIndex index = FieldIndex::ForDescriptor(map, i);
        Tagged<Object> value = js_object->RawFastPropertyAt(index);
        Add("%o\n", value);
      }
    }
  }
}

void StringStream::PrintFixedArray(Tagged<FixedArray> array,
                                   unsigned int limit) {
  ReadOnlyRoots roots = array->GetReadOnlyRoots();
  for (unsigned int i = 0; i < 10 && i < limit; i++) {
    Tagged<Object> element = array->get(i);
    if (IsTheHole(element, roots)) continue;
    for (int len = 1; len < 18; len++) {
      Put(' ');
    }
    Add("%d: %o\n", i, array->get(i));
  }
  if (limit >= 10) {
    Add("                  ...\n");
  }
}

void StringStream::PrintByteArray(Tagged<ByteArray> byte_array) {
  unsigned int limit = byte_array->length();
  for (unsigned int i = 0; i < 10 && i < limit; i++) {
    uint8_t b = byte_array->get(i);
    Add("             %d: %3d 0x%02x", i, b, b);
    if (b >= ' ' && b <= '~') {
      Add(" '%c'", b);
    } else if (b == '\n') {
      Add(" '\n'");
    } else if (b == '\r') {
      Add(" '\r'");
    } else if (b >= 1 && b <= 26) {
      Add(" ^%c", b + 'A' - 1);
    }
    Add("\n");
  }
  if (limit >= 10) {
    Add("                  ...\n");
  }
}

void StringStream::PrintMentionedObjectCache(Isolate* isolate) {
  if (object_print_mode_ == kPrintObjectConcise) return;
  DebugObjectCache* debug_object_cache =
      isolate->string_stream_debug_object_cache();
  Add("-- ObjectCacheKey --\n\n");
  for (size_t i = 0; i < debug_object_cache->size(); i++) {
    Tagged<HeapObject> printee = *(*debug_object_cache)[i];
    Add(" #%d# %p: ", static_cast<int>(i),
        reinterpret_cast<void*>(printee.ptr()));
    ShortPrint(printee, this);
    Add("\n");
    if (IsJSObject(printee)) {
      if (IsJSPrimitiveWrapper(printee)) {
        Add("           value(): %o\n",
            Cast<JSPrimitiveWrapper>(printee)->value());
      }
      PrintUsingMap(Cast<JSObject>(printee));
      if (IsJSArray(printee)) {
        Tagged<JSArray> array = Cast<JSArray>(printee);
        if (array->HasObjectElements()) {
          unsigned int limit = Cast<FixedArray>(array->elements())->length();
          unsigned int length = static_cast<uint32_t>(
              Object::NumberValue(Cast<JSArray>(array)->length()));
          if (length < limit) limit = length;
          PrintFixedArray(Cast<FixedArray>(array->elements()), limit);
        }
      }
    } else if (IsByteArray(printee)) {
      PrintByteArray(Cast<ByteArray>(printee));
    } else if (IsFixedArray(printee)) {
      unsigned int limit = Cast<FixedArray>(printee)->length();
      PrintFixedArray(Cast<FixedArray>(printee), limit);
    }
  }
}

void StringStream::PrintSecurityTokenIfChanged(Tagged<JSFunction> fun) {
  Tagged<Object> token = fun->native_context()->security_token();
  Isolate* isolate = fun->GetIsolate();
  // Use SafeEquals because the cached token might be a stale pointer.
  if (token.SafeEquals(isolate->string_stream_current_security_token())) {
    Add("Security context: %o\n", token);
    isolate->set_string_stream_current_security_token(token);
  }
}

void StringStream::PrintFunction(Tagged<JSFunction> fun,
                                 Tagged<Object> receiver) {
  PrintPrototype(fun, receiver);
}

void StringStream::PrintPrototype(Tagged<JSFunction> fun,
                                  Tagged<Object> receiver) {
  Tagged<Object> name = fun->shared()->Name();
  bool print_name = false;
  Isolate* isolate = fun->GetIsolate();
  if (IsNullOrUndefined(receiver, isolate) || IsTheHole(receiver, isolate) ||
      IsJSProxy(receiver) || IsWasmObject(receiver)) {
    print_name = true;
  } else if (!isolate->context().is_null()) {
    if (!IsJSObject(receiver)) {
      receiver =
          Object::GetPrototypeChainRootMap(receiver, isolate)->prototype();
    }

    for (PrototypeIterator iter(isolate, Cast<JSObject>(receiver),
                                kStartAtReceiver);
         !iter.IsAtEnd(); iter.Advance()) {
      if (!IsJSObject(iter.GetCurrent())) break;
      Tagged<Object> key = iter.GetCurrent<JSObject>()->SlowReverseLookup(fun);
      if (!IsUndefined(key, isolate)) {
        if (!IsString(name) || !IsString(key) ||
            !Cast<String>(name)->Equals(Cast<String>(key))) {
          print_name = true;
        }
        if (IsString(name) && Cast<String>(name)->length() == 0) {
          print_name = false;
        }
        name = key;
        break;
      }
    }
  }
  PrintName(name);
  // Also known as - if the name in the function doesn't match the name under
  // which it was looked up.
  if (print_name) {
    Add("(aka ");
    PrintName(fun->shared()->Name());
    Put(')');
  }
}

char* HeapStringAllocator::grow(unsigned* bytes) {
  unsigned new_bytes = *bytes * 2;
  // Check for overflow.
  if (new_bytes <= *bytes) {
    return space_;
  }
  char* new_space = NewArray<char>(new_bytes);
  if (new_space == nullptr) {
    return space_;
  }
  MemCopy(new_space, space_, *bytes);
  *bytes = new_bytes;
  DeleteArray(space_);
  space_ = new_space;
  return new_space;
}

}  // namespace internal
}  // namespace v8

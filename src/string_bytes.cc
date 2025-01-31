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

#include "string_bytes.h"

#include "env-inl.h"
#include "nbytes.h"
#include "node_buffer.h"
#include "node_errors.h"
#include "simdutf.h"
#include "util.h"

#include <climits>
#include <cstring>  // memcpy

#include <algorithm>

// When creating strings >= this length v8's gc spins up and consumes
// most of the execution time. For these cases it's more performant to
// use external string resources.
#define EXTERN_APEX 0xFBEE9

namespace node {

using v8::HandleScope;
using v8::Isolate;
using v8::Just;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::String;
using v8::Value;

namespace {

template <typename ResourceType, typename TypeName>
class ExternString: public ResourceType {
 public:
  ~ExternString() override {
    free(const_cast<TypeName*>(data_));
    isolate()->AdjustAmountOfExternalAllocatedMemory(-byte_length());
  }

  const TypeName* data() const override {
    return data_;
  }

  size_t length() const override {
    return length_;
  }

  int64_t byte_length() const {
    return length() * sizeof(*data());
  }

  static MaybeLocal<Value> NewFromCopy(Isolate* isolate,
                                       const TypeName* data,
                                       size_t length,
                                       Local<Value>* error) {
    if (length == 0)
      return String::Empty(isolate);

    if (length < EXTERN_APEX)
      return NewSimpleFromCopy(isolate, data, length, error);

    TypeName* new_data = node::UncheckedMalloc<TypeName>(length);
    if (new_data == nullptr) {
      *error = node::ERR_MEMORY_ALLOCATION_FAILED(isolate);
      return MaybeLocal<Value>();
    }
    memcpy(new_data, data, length * sizeof(*new_data));

    return ExternString<ResourceType, TypeName>::New(isolate,
                                                     new_data,
                                                     length,
                                                     error);
  }

  // uses "data" for external resource, and will be free'd on gc
  static MaybeLocal<Value> New(Isolate* isolate,
                               TypeName* data,
                               size_t length,
                               Local<Value>* error) {
    if (length == 0)
      return String::Empty(isolate);

    if (length < EXTERN_APEX) {
      MaybeLocal<Value> str = NewSimpleFromCopy(isolate, data, length, error);
      free(data);
      return str;
    }

    ExternString* h_str = new ExternString<ResourceType, TypeName>(isolate,
                                                                   data,
                                                                   length);
    MaybeLocal<Value> str = NewExternal(isolate, h_str);
    isolate->AdjustAmountOfExternalAllocatedMemory(h_str->byte_length());

    if (str.IsEmpty()) {
      delete h_str;
      *error = node::ERR_STRING_TOO_LONG(isolate);
      return MaybeLocal<Value>();
    }

    return str.ToLocalChecked();
  }

  inline Isolate* isolate() const { return isolate_; }

 private:
  ExternString(Isolate* isolate, const TypeName* data, size_t length)
    : isolate_(isolate), data_(data), length_(length) { }
  static MaybeLocal<Value> NewExternal(Isolate* isolate,
                                       ExternString* h_str);

  // This method does not actually create ExternString instances.
  static MaybeLocal<Value> NewSimpleFromCopy(Isolate* isolate,
                                             const TypeName* data,
                                             size_t length,
                                             Local<Value>* error);

  Isolate* isolate_;
  const TypeName* data_;
  size_t length_;
};


typedef ExternString<String::ExternalOneByteStringResource,
                     char> ExternOneByteString;
typedef ExternString<String::ExternalStringResource,
                     uint16_t> ExternTwoByteString;


template <>
MaybeLocal<Value> ExternOneByteString::NewExternal(
    Isolate* isolate, ExternOneByteString* h_str) {
  return String::NewExternalOneByte(isolate, h_str).FromMaybe(Local<Value>());
}


template <>
MaybeLocal<Value> ExternTwoByteString::NewExternal(
    Isolate* isolate, ExternTwoByteString* h_str) {
  return String::NewExternalTwoByte(isolate, h_str).FromMaybe(Local<Value>());
}

template <>
MaybeLocal<Value> ExternOneByteString::NewSimpleFromCopy(Isolate* isolate,
                                                         const char* data,
                                                         size_t length,
                                                         Local<Value>* error) {
  MaybeLocal<String> str =
      String::NewFromOneByte(isolate,
                             reinterpret_cast<const uint8_t*>(data),
                             v8::NewStringType::kNormal,
                             length);
  if (str.IsEmpty()) {
    *error = node::ERR_STRING_TOO_LONG(isolate);
    return MaybeLocal<Value>();
  }
  return str.ToLocalChecked();
}


template <>
MaybeLocal<Value> ExternTwoByteString::NewSimpleFromCopy(Isolate* isolate,
                                                         const uint16_t* data,
                                                         size_t length,
                                                         Local<Value>* error) {
  MaybeLocal<String> str =
      String::NewFromTwoByte(isolate,
                             data,
                             v8::NewStringType::kNormal,
                             length);
  if (str.IsEmpty()) {
    *error = node::ERR_STRING_TOO_LONG(isolate);
    return MaybeLocal<Value>();
  }
  return str.ToLocalChecked();
}

}  // anonymous namespace

size_t StringBytes::WriteUCS2(
    Isolate* isolate, char* buf, size_t buflen, Local<String> str, int flags) {
  uint16_t* const dst = reinterpret_cast<uint16_t*>(buf);

  size_t max_chars = buflen / sizeof(*dst);
  if (max_chars == 0) {
    return 0;
  }

  uint16_t* const aligned_dst = nbytes::AlignUp(dst, sizeof(*dst));
  size_t nchars;
  if (aligned_dst == dst) {
    nchars = str->Write(isolate, dst, 0, max_chars, flags);
    return nchars * sizeof(*dst);
  }

  CHECK_EQ(reinterpret_cast<uintptr_t>(aligned_dst) % sizeof(*dst), 0);

  // Write all but the last char
  max_chars = std::min(max_chars, static_cast<size_t>(str->Length()));
  if (max_chars == 0) {
    return 0;
  }
  nchars = str->Write(isolate, aligned_dst, 0, max_chars - 1, flags);
  CHECK_EQ(nchars, max_chars - 1);

  // Shift everything to unaligned-left
  memmove(dst, aligned_dst, nchars * sizeof(*dst));

  // One more char to be written
  uint16_t last;
  CHECK_EQ(str->Write(isolate, &last, nchars, 1, flags), 1);
  memcpy(buf + nchars * sizeof(*dst), &last, sizeof(last));
  nchars++;

  return nchars * sizeof(*dst);
}

size_t StringBytes::Write(Isolate* isolate,
                          char* buf,
                          size_t buflen,
                          Local<Value> val,
                          enum encoding encoding) {
  HandleScope scope(isolate);
  size_t nbytes;

  CHECK(val->IsString() == true);
  Local<String> str = val.As<String>();
  String::ValueView input_view(isolate, str);

  int flags = String::HINT_MANY_WRITES_EXPECTED |
              String::NO_NULL_TERMINATION |
              String::REPLACE_INVALID_UTF8;

  switch (encoding) {
    case ASCII:
    case LATIN1:
      if (input_view.is_one_byte()) {
        nbytes = std::min(buflen, static_cast<size_t>(input_view.length()));
        memcpy(buf, input_view.data8(), nbytes);
      } else {
        uint8_t* const dst = reinterpret_cast<uint8_t*>(buf);
        nbytes = str->WriteOneByte(isolate, dst, 0, buflen, flags);
      }
      break;

    case BUFFER:
    case UTF8:
      nbytes = str->WriteUtf8(isolate, buf, buflen, nullptr, flags);
      break;

    case UCS2: {
      nbytes = WriteUCS2(isolate, buf, buflen, str, flags);

      // Node's "ucs2" encoding wants LE character data stored in
      // the Buffer, so we need to reorder on BE platforms.  See
      // https://nodejs.org/api/buffer.html regarding Node's "ucs2"
      // encoding specification
      if constexpr (IsBigEndian()) CHECK(nbytes::SwapBytes16(buf, nbytes));

      break;
    }

    case BASE64URL:
      if (input_view.is_one_byte()) {  // 8-bit case
        size_t written_len = buflen;
        auto result = simdutf::base64_to_binary_safe(
            reinterpret_cast<const char*>(input_view.data8()),
            input_view.length(),
            buf,
            written_len,
            simdutf::base64_url);
        if (result.error == simdutf::error_code::SUCCESS) {
          nbytes = written_len;
        } else {
          // The input does not follow the WHATWG forgiving-base64 specification
          // (adapted for base64url with + and / replaced by - and _).
          // https://infra.spec.whatwg.org/#forgiving-base64-decode
          nbytes = nbytes::Base64Decode(
              buf,
              buflen,
              reinterpret_cast<const char*>(input_view.data8()),
              input_view.length());
        }
      } else {
        String::Value value(isolate, str);
        size_t written_len = buflen;
        auto result = simdutf::base64_to_binary_safe(
            reinterpret_cast<const char16_t*>(*value),
            value.length(),
            buf,
            written_len,
            simdutf::base64_url);
        if (result.error == simdutf::error_code::SUCCESS) {
          nbytes = written_len;
        } else {
          // The input does not follow the WHATWG forgiving-base64 specification
          // (adapted for base64url with + and / replaced by - and _).
          // https://infra.spec.whatwg.org/#forgiving-base64-decode
          nbytes = nbytes::Base64Decode(buf, buflen, *value, value.length());
        }
      }
      break;

    case BASE64: {
      if (input_view.is_one_byte()) {  // 8-bit case
        size_t written_len = buflen;
        auto result = simdutf::base64_to_binary_safe(
            reinterpret_cast<const char*>(input_view.data8()),
            input_view.length(),
            buf,
            written_len);
        if (result.error == simdutf::error_code::SUCCESS) {
          nbytes = written_len;
        } else {
          // The input does not follow the WHATWG forgiving-base64 specification
          // https://infra.spec.whatwg.org/#forgiving-base64-decode
          nbytes = nbytes::Base64Decode(
              buf,
              buflen,
              reinterpret_cast<const char*>(input_view.data8()),
              input_view.length());
        }
      } else {
        String::Value value(isolate, str);
        size_t written_len = buflen;
        auto result = simdutf::base64_to_binary_safe(
            reinterpret_cast<const char16_t*>(*value),
            value.length(),
            buf,
            written_len);
        if (result.error == simdutf::error_code::SUCCESS) {
          nbytes = written_len;
        } else {
          // The input does not follow the WHATWG base64 specification
          // https://infra.spec.whatwg.org/#forgiving-base64-decode
          nbytes = nbytes::Base64Decode(buf, buflen, *value, value.length());
        }
      }
      break;
    }
    case HEX:
      if (input_view.is_one_byte()) {
        nbytes =
            nbytes::HexDecode(buf,
                              buflen,
                              reinterpret_cast<const char*>(input_view.data8()),
                              input_view.length());
      } else {
        String::Value value(isolate, str);
        nbytes = nbytes::HexDecode(buf, buflen, *value, value.length());
      }
      break;

    default:
      UNREACHABLE("unknown encoding");
  }

  return nbytes;
}

// Quick and dirty size calculation
// Will always be at least big enough, but may have some extra
// UTF8 can be as much as 3x the size, Base64 can have 1-2 extra bytes
Maybe<size_t> StringBytes::StorageSize(Isolate* isolate,
                                       Local<Value> val,
                                       enum encoding encoding) {
  HandleScope scope(isolate);

  if (Buffer::HasInstance(val) && (encoding == BUFFER || encoding == LATIN1)) {
    return Just(Buffer::Length(val));
  }

  Local<String> str;
  if (!val->ToString(isolate->GetCurrentContext()).ToLocal(&str))
    return Nothing<size_t>();
  String::ValueView view(isolate, str);
  size_t data_size = 0;

  switch (encoding) {
    case ASCII:
    case LATIN1:
      data_size = view.length();
      break;

    case BUFFER:
    case UTF8:
      // A single UCS2 codepoint never takes up more than 3 utf8 bytes.
      // It is an exercise for the caller to decide when a string is
      // long enough to justify calling Size() instead of StorageSize()
      data_size = 3 * view.length();
      break;

    case UCS2:
      data_size = view.length() * sizeof(uint16_t);
      break;

    case BASE64URL:
      data_size = simdutf::base64_length_from_binary(view.length(),
                                                     simdutf::base64_url);
      break;

    case BASE64:
      data_size = simdutf::base64_length_from_binary(view.length());
      break;

    case HEX:
      CHECK(view.length() % 2 == 0 && "invalid hex string length");
      data_size = view.length() / 2;
      break;

    default:
      UNREACHABLE("unknown encoding");
  }

  return Just(data_size);
}

Maybe<size_t> StringBytes::Size(Isolate* isolate,
                                Local<Value> val,
                                enum encoding encoding) {
  HandleScope scope(isolate);

  if (Buffer::HasInstance(val) && (encoding == BUFFER || encoding == LATIN1))
    return Just(Buffer::Length(val));

  Local<String> str;
  if (!val->ToString(isolate->GetCurrentContext()).ToLocal(&str))
    return Nothing<size_t>();
  String::ValueView view(isolate, str);

  switch (encoding) {
    case ASCII:
    case LATIN1:
      return Just<size_t>(view.length());

    case BUFFER:
    case UTF8:
      if (view.is_one_byte()) {
        return Just<size_t>(simdutf::utf8_length_from_latin1(
            reinterpret_cast<const char*>(view.data8()), view.length()));
      }
      return Just<size_t>(simdutf::utf8_length_from_utf16(
          reinterpret_cast<const char16_t*>(view.data16()), view.length()));

    case UCS2:
      return Just(view.length() * sizeof(uint16_t));

    case BASE64URL: {
      return Just(simdutf::base64_length_from_binary(view.length(),
                                                     simdutf::base64_url));
    }

    case BASE64: {
      return Just(simdutf::base64_length_from_binary(view.length()));
    }

    case HEX:
      return Just<size_t>(view.length() / 2);
  }

  UNREACHABLE();
}

#define CHECK_BUFLEN_IN_RANGE(len)                                    \
  do {                                                                \
    if ((len) > Buffer::kMaxLength) {                                 \
      *error = node::ERR_BUFFER_TOO_LARGE(isolate);                   \
      return MaybeLocal<Value>();                                     \
    }                                                                 \
  } while (0)


MaybeLocal<Value> StringBytes::Encode(Isolate* isolate,
                                      const char* buf,
                                      size_t buflen,
                                      enum encoding encoding,
                                      Local<Value>* error) {
  CHECK_BUFLEN_IN_RANGE(buflen);

  if (!buflen && encoding != BUFFER) {
    return String::Empty(isolate);
  }

  MaybeLocal<String> val;

  switch (encoding) {
    case BUFFER:
      {
        auto maybe_buf = Buffer::Copy(isolate, buf, buflen);
        Local<v8::Object> buf;
        if (!maybe_buf.ToLocal(&buf)) {
          *error = node::ERR_MEMORY_ALLOCATION_FAILED(isolate);
        }
        return buf;
      }

    case ASCII:
      if (simdutf::validate_ascii_with_errors(buf, buflen).error) {
        // The input contains non-ASCII bytes.
        char* out = node::UncheckedMalloc(buflen);
        if (out == nullptr) {
          *error = node::ERR_MEMORY_ALLOCATION_FAILED(isolate);
          return MaybeLocal<Value>();
        }
        nbytes::ForceAscii(buf, out, buflen);
        return ExternOneByteString::New(isolate, out, buflen, error);
      } else {
        return ExternOneByteString::NewFromCopy(isolate, buf, buflen, error);
      }

    case UTF8:
      {
        val = String::NewFromUtf8(isolate,
                                  buf,
                                  v8::NewStringType::kNormal,
                                  buflen);
        Local<String> str;
        if (!val.ToLocal(&str)) {
          *error = node::ERR_STRING_TOO_LONG(isolate);
        }
        return str;
      }

    case LATIN1:
      return ExternOneByteString::NewFromCopy(isolate, buf, buflen, error);

    case BASE64: {
      size_t dlen = simdutf::base64_length_from_binary(buflen);
      char* dst = node::UncheckedMalloc(dlen);
      if (dst == nullptr) {
        *error = node::ERR_MEMORY_ALLOCATION_FAILED(isolate);
        return MaybeLocal<Value>();
      }

      size_t written = simdutf::binary_to_base64(buf, buflen, dst);
      CHECK_EQ(written, dlen);

      return ExternOneByteString::New(isolate, dst, dlen, error);
    }

    case BASE64URL: {
      size_t dlen =
          simdutf::base64_length_from_binary(buflen, simdutf::base64_url);
      char* dst = node::UncheckedMalloc(dlen);
      if (dst == nullptr) {
        *error = node::ERR_MEMORY_ALLOCATION_FAILED(isolate);
        return MaybeLocal<Value>();
      }

      size_t written =
          simdutf::binary_to_base64(buf, buflen, dst, simdutf::base64_url);
      CHECK_EQ(written, dlen);

      return ExternOneByteString::New(isolate, dst, dlen, error);
    }

    case HEX: {
      size_t dlen = buflen * 2;
      char* dst = node::UncheckedMalloc(dlen);
      if (dst == nullptr) {
        *error = node::ERR_MEMORY_ALLOCATION_FAILED(isolate);
        return MaybeLocal<Value>();
      }
      size_t written = nbytes::HexEncode(buf, buflen, dst, dlen);
      CHECK_EQ(written, dlen);

      return ExternOneByteString::New(isolate, dst, dlen, error);
    }

    case UCS2: {
      size_t str_len = buflen / 2;
      if constexpr (IsBigEndian()) {
        uint16_t* dst = node::UncheckedMalloc<uint16_t>(str_len);
        if (str_len != 0 && dst == nullptr) {
          *error = node::ERR_MEMORY_ALLOCATION_FAILED(isolate);
          return MaybeLocal<Value>();
        }
        for (size_t i = 0, k = 0; k < str_len; i += 2, k += 1) {
          // The input is in *little endian*, because that's what Node.js
          // expects, so the high byte comes after the low byte.
          const uint8_t hi = static_cast<uint8_t>(buf[i + 1]);
          const uint8_t lo = static_cast<uint8_t>(buf[i + 0]);
          dst[k] = static_cast<uint16_t>(hi) << 8 | lo;
        }
        return ExternTwoByteString::New(isolate, dst, str_len, error);
      }
      if (reinterpret_cast<uintptr_t>(buf) % 2 != 0) {
        // Unaligned data still means we can't directly pass it to V8.
        char* dst = node::UncheckedMalloc(buflen);
        if (dst == nullptr) {
          *error = node::ERR_MEMORY_ALLOCATION_FAILED(isolate);
          return MaybeLocal<Value>();
        }
        memcpy(dst, buf, buflen);
        return ExternTwoByteString::New(
            isolate, reinterpret_cast<uint16_t*>(dst), str_len, error);
      }
      return ExternTwoByteString::NewFromCopy(
          isolate, reinterpret_cast<const uint16_t*>(buf), str_len, error);
    }

    default:
      UNREACHABLE("unknown encoding");
  }
}


MaybeLocal<Value> StringBytes::Encode(Isolate* isolate,
                                      const uint16_t* buf,
                                      size_t buflen,
                                      Local<Value>* error) {
  if (buflen == 0) return String::Empty(isolate);
  CHECK_BUFLEN_IN_RANGE(buflen);

  // Node's "ucs2" encoding expects LE character data inside a
  // Buffer, so we need to reorder on BE platforms.  See
  // https://nodejs.org/api/buffer.html regarding Node's "ucs2"
  // encoding specification
  if constexpr (IsBigEndian()) {
    uint16_t* dst = node::UncheckedMalloc<uint16_t>(buflen);
    if (dst == nullptr) {
      *error = node::ERR_MEMORY_ALLOCATION_FAILED(isolate);
      return MaybeLocal<Value>();
    }
    size_t nbytes = buflen * sizeof(uint16_t);
    memcpy(dst, buf, nbytes);
    CHECK(nbytes::SwapBytes16(reinterpret_cast<char*>(dst), nbytes));
    return ExternTwoByteString::New(isolate, dst, buflen, error);
  } else {
    return ExternTwoByteString::NewFromCopy(isolate, buf, buflen, error);
  }
}

MaybeLocal<Value> StringBytes::Encode(Isolate* isolate,
                                      const char* buf,
                                      enum encoding encoding,
                                      Local<Value>* error) {
  const size_t len = strlen(buf);
  return Encode(isolate, buf, len, encoding, error);
}

}  // namespace node

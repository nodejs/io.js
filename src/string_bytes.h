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

#ifndef SRC_STRING_BYTES_H_
#define SRC_STRING_BYTES_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

// Decodes a v8::Local<v8::String> or Buffer to a raw char*

#include "v8.h"
#include "env-inl.h"

#include <string>

namespace node {

class StringBytes {
 public:
  class InlineDecoder : public MaybeStackBuffer<char> {
   public:
    inline v8::Maybe<void> Decode(Environment* env,
                                  v8::Local<v8::String> string,
                                  enum encoding enc) {
      size_t storage;
      if (!StringBytes::StorageSize(env->isolate(), string, enc).To(&storage))
        return v8::Nothing<void>();
      AllocateSufficientStorage(storage);
      const size_t length =
          StringBytes::Write(env->isolate(), out(), storage, string, enc);

      // No zero terminator is included when using this method.
      SetLength(length);
      return v8::JustVoid();
    }

    inline size_t size() const { return length(); }
  };

  // Fast, but can be 2 bytes oversized for Base64, and
  // as much as triple UTF-8 strings <= 65536 chars in length
  static v8::Maybe<size_t> StorageSize(v8::Isolate* isolate,
                                       v8::Local<v8::Value> val,
                                       enum encoding enc);

  // Precise byte count, but slightly slower for Base64 and
  // very much slower for UTF-8
  static v8::Maybe<size_t> Size(v8::Isolate* isolate,
                                v8::Local<v8::Value> val,
                                enum encoding enc);

  // Write the bytes from the string or buffer into the char*
  // returns the number of bytes written, which will always be
  // <= buflen.  Use StorageSize/Size first to know how much
  // memory to allocate.
  static size_t Write(v8::Isolate* isolate,
                      char* buf,
                      size_t buflen,
                      v8::Local<v8::Value> val,
                      enum encoding enc);

  // Take the bytes in the src, and turn it into a Buffer or String.
  static v8::MaybeLocal<v8::Value> Encode(v8::Isolate* isolate,
                                          const char* buf,
                                          size_t buflen,
                                          enum encoding encoding,
                                          v8::Local<v8::Value>* error);

  // Warning: This reverses endianness on BE platforms, even though the
  // signature using uint16_t implies that it should not.
  // However, the brokenness is already public API and can't therefore
  // be changed easily.
  static v8::MaybeLocal<v8::Value> Encode(v8::Isolate* isolate,
                                          const uint16_t* buf,
                                          size_t buflen,
                                          v8::Local<v8::Value>* error);

  static v8::MaybeLocal<v8::Value> Encode(v8::Isolate* isolate,
                                          const char* buf,
                                          enum encoding encoding,
                                          v8::Local<v8::Value>* error);

 private:
  static size_t WriteUCS2(v8::Isolate* isolate,
                          char* buf,
                          size_t buflen,
                          v8::Local<v8::String> str,
                          int flags);
};

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_STRING_BYTES_H_

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

#ifndef SRC_NODE_I18N_H_
#define SRC_NODE_I18N_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#if defined(NODE_HAVE_I18N_SUPPORT)

#include "base_object.h"
#include "env.h"
#include "util.h"
#include "v8.h"

#include <unicode/ucnv.h>

#include <string>

namespace node {
namespace i18n {

bool InitializeICUDirectory(const std::string& path, std::string* error);

void SetDefaultTimeZone(const char* tzid);

enum class idna_mode {
  // Default mode for maximum compatibility.
  kDefault,
  // Ignore all errors in IDNA conversion, if possible.
  kLenient,
  // Enforce STD3 rules (UseSTD3ASCIIRules) and DNS length restrictions
  // (VerifyDnsLength). Corresponds to `beStrict` flag in the "domain to ASCII"
  // algorithm.
  kStrict
};

struct ConverterDeleter {
  void operator()(UConverter* pointer) const { ucnv_close(pointer); }
};
using ConverterPointer = std::unique_ptr<UConverter, ConverterDeleter>;

class Converter {
 public:
  explicit Converter(const char* name, const char* sub = nullptr);
  explicit Converter(UConverter* converter, const char* sub = nullptr);

  UConverter* conv() const { return conv_.get(); }

  size_t max_char_size() const;
  size_t min_char_size() const;
  void reset();
  void set_subst_chars(const char* sub = nullptr);

 private:
  ConverterPointer conv_;
};

class ConverterObject : public BaseObject, Converter {
 public:
  enum ConverterFlags {
    CONVERTER_FLAGS_FLUSH      = 0x1,
    CONVERTER_FLAGS_FATAL      = 0x2,
    CONVERTER_FLAGS_IGNORE_BOM = 0x4,
    CONVERTER_FLAGS_UNICODE    = 0x8,
    CONVERTER_FLAGS_BOM_SEEN   = 0x10,
  };

  static void Create(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Decode(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Has(const v8::FunctionCallbackInfo<v8::Value>& args);

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(ConverterObject)
  SET_SELF_SIZE(ConverterObject)

 protected:
  ConverterObject(Environment* env,
                  v8::Local<v8::Object> wrap,
                  UConverter* converter,
                  int flags,
                  const char* sub = nullptr);

  void set_bom_seen(bool seen) {
    if (seen)
      flags_ |= CONVERTER_FLAGS_BOM_SEEN;
    else
      flags_ &= ~CONVERTER_FLAGS_BOM_SEEN;
  }

  bool bom_seen() const {
    return (flags_ & CONVERTER_FLAGS_BOM_SEEN) == CONVERTER_FLAGS_BOM_SEEN;
  }

  bool unicode() const {
    return (flags_ & CONVERTER_FLAGS_UNICODE) == CONVERTER_FLAGS_UNICODE;
  }

  bool ignore_bom() const {
    return (flags_ & CONVERTER_FLAGS_IGNORE_BOM) == CONVERTER_FLAGS_IGNORE_BOM;
  }

 private:
  int flags_ = 0;
};

}  // namespace i18n
}  // namespace node

#endif  // NODE_HAVE_I18N_SUPPORT

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_I18N_H_

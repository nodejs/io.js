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

/*
 * notes: by srl295
 *  - When in NODE_HAVE_SMALL_ICU mode, ICU is linked against "stub" (null) data
 *     ( stubdata/libicudata.a ) containing nothing, no data, and it's also
 *    linked against a "small" data file which the SMALL_ICUDATA_ENTRY_POINT
 *    macro names. That's the "english+root" data.
 *
 *    If icu_data_path is non-null, the user has provided a path and we assume
 *    it goes somewhere useful. We set that path in ICU, and exit.
 *    If icu_data_path is null, they haven't set a path and we want the
 *    "english+root" data.  We call
 *       udata_setCommonData(SMALL_ICUDATA_ENTRY_POINT,...)
 *    to load up the english+root data.
 *
 *  - when NOT in NODE_HAVE_SMALL_ICU mode, ICU is linked directly with its full
 *    data. All of the variables and command line options for changing data at
 *    runtime are disabled, as they wouldn't fully override the internal data.
 *    See:  http://bugs.icu-project.org/trac/ticket/10924
 */


#include "node_i18n.h"
#include "node_external_reference.h"
#include "simdutf.h"

#if defined(NODE_HAVE_I18N_SUPPORT)

#include "base_object-inl.h"
#include "node.h"
#include "node_buffer.h"
#include "node_errors.h"
#include "node_internals.h"
#include "string_bytes.h"
#include "util-inl.h"
#include "v8.h"

#include <unicode/putil.h>
#include <unicode/timezone.h>
#include <unicode/uchar.h>
#include <unicode/uclean.h>
#include <unicode/ucnv.h>
#include <unicode/ulocdata.h>
#include <unicode/urename.h>
#include <unicode/utf16.h>
#include <unicode/utypes.h>
#include <unicode/uvernum.h>
#include <unicode/uversion.h>
#include "nbytes.h"

#ifdef NODE_HAVE_SMALL_ICU
#include <unicode/udata.h>

/* if this is defined, we have a 'secondary' entry point.
   compare following to utypes.h defs for U_ICUDATA_ENTRY_POINT */
#define SMALL_ICUDATA_ENTRY_POINT \
  SMALL_DEF2(U_ICU_VERSION_MAJOR_NUM, U_LIB_SUFFIX_C_NAME)
#define SMALL_DEF2(major, suff) SMALL_DEF(major, suff)
#ifndef U_LIB_SUFFIX_C_NAME
#define SMALL_DEF(major, suff) icusmdt##major##_dat
#else
#define SMALL_DEF(major, suff) icusmdt##suff##major##_dat
#endif

extern "C" const char U_DATA_API SMALL_ICUDATA_ENTRY_POINT[];
#endif

namespace node {

using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Int32;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Object;
using v8::ObjectTemplate;
using v8::String;
using v8::Value;

namespace i18n {
namespace {

template <typename T>
MaybeLocal<Object> ToBufferEndian(Environment* env, MaybeStackBuffer<T>* buf) {
  MaybeLocal<Object> ret = Buffer::New(env, buf);
  if (ret.IsEmpty())
    return ret;

  static_assert(sizeof(T) == 1 || sizeof(T) == 2,
                "Currently only one- or two-byte buffers are supported");
  if constexpr (sizeof(T) > 1 && IsBigEndian()) {
    SPREAD_BUFFER_ARG(ret.ToLocalChecked(), retbuf);
    CHECK(nbytes::SwapBytes16(retbuf_data, retbuf_length));
  }

  return ret;
}

// One-Shot Converters

void CopySourceBuffer(MaybeStackBuffer<UChar>* dest,
                      const char* data,
                      const size_t length,
                      const size_t length_in_chars) {
  dest->AllocateSufficientStorage(length_in_chars);
  char* dst = reinterpret_cast<char*>(**dest);
  memcpy(dst, data, length);
  if constexpr (IsBigEndian()) {
    CHECK(nbytes::SwapBytes16(dst, length));
  }
}

typedef MaybeLocal<Object> (*TranscodeFunc)(Environment* env,
                                            const char* fromEncoding,
                                            const char* toEncoding,
                                            const char* source,
                                            const size_t source_length,
                                            UErrorCode* status);

MaybeLocal<Object> Transcode(Environment* env,
                             const char* fromEncoding,
                             const char* toEncoding,
                             const char* source,
                             const size_t source_length,
                             UErrorCode* status) {
  MaybeLocal<Object> ret;
  MaybeStackBuffer<char> result;
  Converter to(toEncoding);
  Converter from(fromEncoding);

  size_t sublen = ucnv_getMinCharSize(to.conv());
  std::string sub(sublen, '?');
  to.set_subst_chars(sub.c_str());

  const uint32_t limit = source_length * to.max_char_size();
  result.AllocateSufficientStorage(limit);
  char* target = *result;
  ucnv_convertEx(to.conv(), from.conv(), &target, target + limit,
                 &source, source + source_length, nullptr, nullptr,
                 nullptr, nullptr, true, true, status);
  if (U_SUCCESS(*status)) {
    result.SetLength(target - &result[0]);
    ret = ToBufferEndian(env, &result);
  }
  return ret;
}

MaybeLocal<Object> TranscodeLatin1ToUcs2(Environment* env,
                                         const char* fromEncoding,
                                         const char* toEncoding,
                                         const char* source,
                                         const size_t source_length,
                                         UErrorCode* status) {
  MaybeStackBuffer<UChar> destbuf(source_length);
  auto actual_length =
      simdutf::convert_latin1_to_utf16le(source, source_length, destbuf.out());
  if (actual_length == 0) {
    *status = U_INVALID_CHAR_FOUND;
    return {};
  }

  return Buffer::New(env, &destbuf);
}

MaybeLocal<Object> TranscodeFromUcs2(Environment* env,
                                     const char* fromEncoding,
                                     const char* toEncoding,
                                     const char* source,
                                     const size_t source_length,
                                     UErrorCode* status) {
  MaybeStackBuffer<UChar> sourcebuf;
  MaybeLocal<Object> ret;
  Converter to(toEncoding);

  std::string sub(to.min_char_size(), '?');
  to.set_subst_chars(sub.c_str());

  const size_t length_in_chars = source_length / sizeof(UChar);
  CopySourceBuffer(&sourcebuf, source, source_length, length_in_chars);
  MaybeStackBuffer<char> destbuf(length_in_chars);
  const uint32_t len = ucnv_fromUChars(to.conv(), *destbuf, length_in_chars,
                                       *sourcebuf, length_in_chars, status);
  if (U_SUCCESS(*status)) {
    destbuf.SetLength(len);
    ret = ToBufferEndian(env, &destbuf);
  }
  return ret;
}

MaybeLocal<Object> TranscodeUcs2FromUtf8(Environment* env,
                                         const char* fromEncoding,
                                         const char* toEncoding,
                                         const char* source,
                                         const size_t source_length,
                                         UErrorCode* status) {
  size_t expected_utf16_length =
      simdutf::utf16_length_from_utf8(source, source_length);
  MaybeStackBuffer<UChar> destbuf(expected_utf16_length);
  auto actual_length =
      simdutf::convert_utf8_to_utf16le(source, source_length, destbuf.out());

  if (actual_length == 0) {
    *status = U_INVALID_CHAR_FOUND;
    return {};
  }

  return Buffer::New(env, &destbuf);
}

MaybeLocal<Object> TranscodeUtf8FromUcs2(Environment* env,
                                         const char* fromEncoding,
                                         const char* toEncoding,
                                         const char* source,
                                         const size_t source_length,
                                         UErrorCode* status) {
  const size_t length_in_chars = source_length / sizeof(UChar);
  size_t expected_utf8_length = simdutf::utf8_length_from_utf16le(
      reinterpret_cast<const char16_t*>(source), length_in_chars);

  MaybeStackBuffer<char> destbuf(expected_utf8_length);
  auto actual_length = simdutf::convert_utf16le_to_utf8(
      reinterpret_cast<const char16_t*>(source),
      length_in_chars,
      destbuf.out());

  if (actual_length == 0) {
    *status = U_INVALID_CHAR_FOUND;
    return {};
  }

  return Buffer::New(env, &destbuf);
}

constexpr const char* EncodingName(const enum encoding encoding) {
  switch (encoding) {
    case ASCII: return "us-ascii";
    case LATIN1: return "iso8859-1";
    case UCS2: return "utf16le";
    case UTF8: return "utf-8";
    default: return nullptr;
  }
}

constexpr bool SupportedEncoding(const enum encoding encoding) {
  switch (encoding) {
    case ASCII:
    case LATIN1:
    case UCS2:
    case UTF8: return true;
    default: return false;
  }
}

void Transcode(const FunctionCallbackInfo<Value>&args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();
  UErrorCode status = U_ZERO_ERROR;
  MaybeLocal<Object> result;

  ArrayBufferViewContents<char> input(args[0]);
  const enum encoding fromEncoding = ParseEncoding(isolate, args[1], BUFFER);
  const enum encoding toEncoding = ParseEncoding(isolate, args[2], BUFFER);

  if (SupportedEncoding(fromEncoding) && SupportedEncoding(toEncoding)) {
    TranscodeFunc tfn = &Transcode;
    switch (fromEncoding) {
      case ASCII:
      case LATIN1:
        if (toEncoding == UCS2) tfn = &TranscodeLatin1ToUcs2;
        break;
      case UTF8:
        if (toEncoding == UCS2)
          tfn = &TranscodeUcs2FromUtf8;
        break;
      case UCS2:
        switch (toEncoding) {
          case UCS2:
            tfn = &Transcode;
            break;
          case UTF8:
            tfn = &TranscodeUtf8FromUcs2;
            break;
          default:
            tfn = &TranscodeFromUcs2;
        }
        break;
      default:
        // This should not happen because of the SupportedEncoding checks
        ABORT();
    }

    result = tfn(env, EncodingName(fromEncoding), EncodingName(toEncoding),
                 input.data(), input.length(), &status);
  } else {
    status = U_ILLEGAL_ARGUMENT_ERROR;
  }

  if (result.IsEmpty())
    return args.GetReturnValue().Set(status);

  return args.GetReturnValue().Set(result.ToLocalChecked());
}

void ICUErrorName(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(args[0]->IsInt32());
  UErrorCode status = static_cast<UErrorCode>(args[0].As<Int32>()->Value());
  args.GetReturnValue().Set(
      String::NewFromUtf8(env->isolate(),
                          u_errorName(status)).ToLocalChecked());
}

}  // anonymous namespace

Converter::Converter(const char* name, const char* sub) {
  UErrorCode status = U_ZERO_ERROR;
  UConverter* conv = ucnv_open(name, &status);
  CHECK(U_SUCCESS(status));
  conv_.reset(conv);
  set_subst_chars(sub);
}

Converter::Converter(UConverter* converter, const char* sub)
    : conv_(converter) {
  set_subst_chars(sub);
}

void Converter::set_subst_chars(const char* sub) {
  CHECK(conv_);
  UErrorCode status = U_ZERO_ERROR;
  if (sub != nullptr) {
    ucnv_setSubstChars(conv_.get(), sub, strlen(sub), &status);
    CHECK(U_SUCCESS(status));
  }
}

void Converter::reset() {
  ucnv_reset(conv_.get());
}

size_t Converter::min_char_size() const {
  CHECK(conv_);
  return ucnv_getMinCharSize(conv_.get());
}

size_t Converter::max_char_size() const {
  CHECK(conv_);
  return ucnv_getMaxCharSize(conv_.get());
}

void ConverterObject::Has(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK_GE(args.Length(), 1);
  Utf8Value label(env->isolate(), args[0]);

  UErrorCode status = U_ZERO_ERROR;
  ConverterPointer conv(ucnv_open(*label, &status));
  args.GetReturnValue().Set(!!U_SUCCESS(status));
}

void ConverterObject::Create(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  Local<ObjectTemplate> t = env->i18n_converter_template();
  Local<Object> obj;
  if (!t->NewInstance(env->context()).ToLocal(&obj)) return;

  CHECK_GE(args.Length(), 2);
  Utf8Value label(env->isolate(), args[0]);
  int flags = args[1]->Uint32Value(env->context()).ToChecked();
  bool fatal =
      (flags & CONVERTER_FLAGS_FATAL) == CONVERTER_FLAGS_FATAL;

  UErrorCode status = U_ZERO_ERROR;
  UConverter* conv = ucnv_open(*label, &status);
  if (U_FAILURE(status))
    return;

  if (fatal) {
    status = U_ZERO_ERROR;
    ucnv_setToUCallBack(conv, UCNV_TO_U_CALLBACK_STOP,
                        nullptr, nullptr, nullptr, &status);
  }

  auto converter = new ConverterObject(env, obj, conv, flags);
  size_t sublen = ucnv_getMinCharSize(conv);
  std::string sub(sublen, '?');
  converter->set_subst_chars(sub.c_str());

  args.GetReturnValue().Set(obj);
}

void ConverterObject::Decode(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK_GE(args.Length(), 4);  // Converter, Buffer, Flags, Encoding

  ConverterObject* converter;
  ASSIGN_OR_RETURN_UNWRAP(&converter, args[0].As<Object>());

  if (!(args[1]->IsArrayBuffer() || args[1]->IsSharedArrayBuffer() ||
        args[1]->IsArrayBufferView())) {
    return node::THROW_ERR_INVALID_ARG_TYPE(
        env->isolate(),
        "The \"input\" argument must be an instance of SharedArrayBuffer, "
        "ArrayBuffer or ArrayBufferView.");
  }

  ArrayBufferViewContents<char> input(args[1]);
  int flags = args[2]->Uint32Value(env->context()).ToChecked();

  CHECK(args[3]->IsString());
  Local<String> from_encoding = args[3].As<String>();

  UErrorCode status = U_ZERO_ERROR;
  MaybeStackBuffer<UChar> result;

  UBool flush = (flags & CONVERTER_FLAGS_FLUSH) == CONVERTER_FLAGS_FLUSH;

  // When flushing the final chunk, the limit is the maximum
  // of either the input buffer length or the number of pending
  // characters times the min char size, multiplied by 2 as unicode may
  // take up to 2 UChars to encode a character
  size_t limit = 2 * converter->min_char_size() *
      (!flush ?
          input.length() :
          std::max(
              input.length(),
              static_cast<size_t>(
                  ucnv_toUCountPending(converter->conv(), &status))));
  status = U_ZERO_ERROR;

  if (limit > 0)
    result.AllocateSufficientStorage(limit);

  auto cleanup = OnScopeLeave([&]() {
    if (flush) {
      // Reset the converter state.
      converter->set_bom_seen(false);
      converter->reset();
    }
  });

  const char* source = input.data();
  size_t source_length = input.length();

  UChar* target = *result;
  ucnv_toUnicode(converter->conv(),
                 &target,
                 target + limit,
                 &source,
                 source + source_length,
                 nullptr,
                 flush,
                 &status);

  if (U_SUCCESS(status)) {
    bool omit_initial_bom = false;
    if (limit > 0) {
      result.SetLength(target - &result[0]);
      if (result.length() > 0 &&
          converter->unicode() &&
          !converter->ignore_bom() &&
          !converter->bom_seen()) {
        // If the very first result in the stream is a BOM, and we are not
        // explicitly told to ignore it, then we mark it for discarding.
        if (result[0] == 0xFEFF)
          omit_initial_bom = true;
        converter->set_bom_seen(true);
      }
    }

    Local<Value> error;
    UChar* output = result.out();
    size_t beginning = 0;
    size_t length = result.length() * sizeof(UChar);

    if (omit_initial_bom) {
      // Perform `ret = ret.slice(2)`.
      beginning += 2;
      length -= 2;
    }

    char* value = reinterpret_cast<char*>(output) + beginning;

    if constexpr (IsBigEndian()) {
      CHECK(nbytes::SwapBytes16(value, length));
    }

    MaybeLocal<Value> encoded =
        StringBytes::Encode(env->isolate(), value, length, UCS2, &error);

    Local<Value> ret;
    if (encoded.ToLocal(&ret)) {
      args.GetReturnValue().Set(ret);
      return;
    }
  }

  node::THROW_ERR_ENCODING_INVALID_ENCODED_DATA(
      env->isolate(),
      "The encoded data was not valid for encoding %s",
      *node::Utf8Value(env->isolate(), from_encoding));
}

ConverterObject::ConverterObject(
    Environment* env,
    Local<Object> wrap,
    UConverter* converter,
    int flags,
    const char* sub)
    : BaseObject(env, wrap),
      Converter(converter, sub),
      flags_(flags) {
  MakeWeak();

  switch (ucnv_getType(converter)) {
    case UCNV_UTF8:
    case UCNV_UTF16_BigEndian:
    case UCNV_UTF16_LittleEndian:
      flags_ |= CONVERTER_FLAGS_UNICODE;
      break;
    default: {
      // Fall through
    }
  }
}

bool InitializeICUDirectory(const std::string& path, std::string* error) {
  UErrorCode status = U_ZERO_ERROR;
  if (path.empty()) {
#ifdef NODE_HAVE_SMALL_ICU
    // install the 'small' data.
    udata_setCommonData(&SMALL_ICUDATA_ENTRY_POINT, &status);
#else  // !NODE_HAVE_SMALL_ICU
    // no small data, so nothing to do.
#endif  // !NODE_HAVE_SMALL_ICU
  } else {
    u_setDataDirectory(path.c_str());
    u_init(&status);
  }
  if (status == U_ZERO_ERROR) {
    return true;
  }

  *error = u_errorName(status);
  return false;
}

void SetDefaultTimeZone(const char* tzid) {
  size_t tzidlen = strlen(tzid) + 1;
  UErrorCode status = U_ZERO_ERROR;
  MaybeStackBuffer<UChar, 256> id(tzidlen);
  u_charsToUChars(tzid, id.out(), tzidlen);
  // This is threadsafe:
  ucal_setDefaultTimeZone(id.out(), &status);
  CHECK(U_SUCCESS(status));
}

// This is similar to wcwidth except that it takes the current unicode
// character properties database into consideration, allowing it to
// correctly calculate the column widths of things like emoji's and
// newer wide characters. wcwidth, on the other hand, uses a fixed
// algorithm that does not take things like emoji into proper
// consideration.
//
// TODO(TimothyGu): Investigate Cc (C0/C1 control codes). Both VTE (used by
// GNOME Terminal) and Konsole don't consider them to be zero-width (see refs
// below), and when printed in VTE it is Narrow. However GNOME Terminal doesn't
// allow it to be input. Linux's PTY terminal prints control characters as
// Narrow rhombi.
//
// TODO(TimothyGu): Investigate Hangul jamo characters. Medial vowels and final
// consonants are 0-width when combined with initial consonants; otherwise they
// are technically Wide. But many terminals (including Konsole and
// VTE/GLib-based) implement all medials and finals as 0-width.
//
// Refs: https://eev.ee/blog/2015/09/12/dark-corners-of-unicode/#combining-characters-and-character-width
// Refs: https://github.com/GNOME/glib/blob/79e4d4c6be/glib/guniprop.c#L388-L420
// Refs: https://github.com/KDE/konsole/blob/8c6a5d13c0/src/konsole_wcwidth.cpp#L101-L223
static int GetColumnWidth(UChar32 codepoint,
                          bool ambiguous_as_full_width = false) {
  // UCHAR_EAST_ASIAN_WIDTH is the Unicode property that identifies a
  // codepoint as being full width, wide, ambiguous, neutral, narrow,
  // or halfwidth.
  const int eaw = u_getIntPropertyValue(codepoint, UCHAR_EAST_ASIAN_WIDTH);
  switch (eaw) {
    case U_EA_FULLWIDTH:
    case U_EA_WIDE:
      return 2;
    case U_EA_AMBIGUOUS:
      // See: http://www.unicode.org/reports/tr11/#Ambiguous for details
      if (ambiguous_as_full_width) {
        return 2;
      }
      // If ambiguous_as_full_width is false:
      [[fallthrough]];
    case U_EA_NEUTRAL:
      if (u_hasBinaryProperty(codepoint, UCHAR_EMOJI_PRESENTATION)) {
        return 2;
      }
      [[fallthrough]];
    case U_EA_HALFWIDTH:
    case U_EA_NARROW:
    default:
      const auto zero_width_mask = U_GC_CC_MASK |  // C0/C1 control code
                                  U_GC_CF_MASK |  // Format control character
                                  U_GC_ME_MASK |  // Enclosing mark
                                  U_GC_MN_MASK;   // Nonspacing mark
      if (codepoint != 0x00AD &&  // SOFT HYPHEN is Cf but not zero-width
          ((U_MASK(u_charType(codepoint)) & zero_width_mask) ||
          u_hasBinaryProperty(codepoint, UCHAR_EMOJI_MODIFIER))) {
        return 0;
      }
      return 1;
  }
}

// Returns the column width for the given String.
static void GetStringWidth(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(args[0]->IsString());

  bool ambiguous_as_full_width = args[1]->IsTrue();
  bool expand_emoji_sequence = !args[2]->IsBoolean() || args[2]->IsTrue();

  TwoByteValue value(env->isolate(), args[0]);
  // reinterpret_cast is required by windows to compile
  UChar* str = reinterpret_cast<UChar*>(*value);
  static_assert(sizeof(*str) == sizeof(**value),
                "sizeof(*str) == sizeof(**value)");
  UChar32 c = 0;
  UChar32 p;
  size_t n = 0;
  uint32_t width = 0;

  while (n < value.length()) {
    p = c;
    U16_NEXT(str, n, value.length(), c);
    // Don't count individual emoji codepoints that occur within an
    // emoji sequence. This is not necessarily foolproof. Some
    // environments display emoji sequences in the appropriate
    // condensed form (as a single emoji glyph), other environments
    // may not understand an emoji sequence and will display each
    // individual emoji separately. When this happens, the width
    // calculated will be off, and there's no reliable way of knowing
    // in advance if a particular sequence is going to be supported.
    // The expand_emoji_sequence option allows the caller to skip this
    // check and count each code within an emoji sequence separately.
    // https://www.unicode.org/reports/tr51/tr51-16.html#Emoji_ZWJ_Sequences
    if (!expand_emoji_sequence &&
        n > 0 && p == 0x200d &&  // 0x200d == ZWJ (zero width joiner)
        (u_hasBinaryProperty(c, UCHAR_EMOJI_PRESENTATION) ||
         u_hasBinaryProperty(c, UCHAR_EMOJI_MODIFIER))) {
      continue;
    }
    width += GetColumnWidth(c, ambiguous_as_full_width);
  }
  args.GetReturnValue().Set(width);
}

static void CreatePerIsolateProperties(IsolateData* isolate_data,
                                       Local<ObjectTemplate> target) {
  Isolate* isolate = isolate_data->isolate();

  SetMethod(isolate, target, "getStringWidth", GetStringWidth);

  // One-shot converters
  SetMethod(isolate, target, "icuErrName", ICUErrorName);
  SetMethod(isolate, target, "transcode", Transcode);

  // ConverterObject
  {
    Local<FunctionTemplate> t = NewFunctionTemplate(isolate, nullptr);
    t->InstanceTemplate()->SetInternalFieldCount(
        ConverterObject::kInternalFieldCount);
    Local<String> converter_string =
        FIXED_ONE_BYTE_STRING(isolate, "Converter");
    t->SetClassName(converter_string);
    isolate_data->set_i18n_converter_template(t->InstanceTemplate());
  }

  SetMethod(isolate, target, "getConverter", ConverterObject::Create);
  SetMethod(isolate, target, "decode", ConverterObject::Decode);
  SetMethod(isolate, target, "hasConverter", ConverterObject::Has);
}

void CreatePerContextProperties(Local<Object> target,
                                Local<Value> unused,
                                Local<Context> context,
                                void* priv) {}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(GetStringWidth);
  registry->Register(ICUErrorName);
  registry->Register(Transcode);
  registry->Register(ConverterObject::Create);
  registry->Register(ConverterObject::Decode);
  registry->Register(ConverterObject::Has);
}

}  // namespace i18n
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(icu, node::i18n::CreatePerContextProperties)
NODE_BINDING_PER_ISOLATE_INIT(icu, node::i18n::CreatePerIsolateProperties)
NODE_BINDING_EXTERNAL_REFERENCE(icu, node::i18n::RegisterExternalReferences)

#endif  // NODE_HAVE_I18N_SUPPORT

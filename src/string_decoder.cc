#include "string_decoder.h"  // NOLINT(build/include_inline)
#include "string_decoder-inl.h"

#include "env-inl.h"
#include "node_buffer.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "string_bytes.h"
#include "util.h"

using v8::Array;
using v8::ArrayBufferView;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Object;
using v8::String;
using v8::Value;

namespace node {

namespace {

MaybeLocal<String> MakeString(Isolate* isolate,
                              const char* data,
                              size_t length,
                              enum encoding encoding) {
  Local<Value> error;
  MaybeLocal<Value> ret;
  if (encoding == UTF8) {
    MaybeLocal<String> utf8_string;
    if (length <= static_cast<size_t>(v8::String::kMaxLength)) {
      utf8_string = String::NewFromUtf8(
          isolate, data, v8::NewStringType::kNormal, length);
    }
    if (utf8_string.IsEmpty()) {
      isolate->ThrowException(node::ERR_STRING_TOO_LONG(isolate));
      return MaybeLocal<String>();
    } else {
      return utf8_string;
    }
  } else {
    ret = StringBytes::Encode(
        isolate,
        data,
        length,
        encoding,
        &error);
  }

  if (ret.IsEmpty()) {
    CHECK(!error.IsEmpty());
    isolate->ThrowException(error);
  }

  DCHECK(ret.IsEmpty() || ret.ToLocalChecked()->IsString());
  return ret.FromMaybe(Local<Value>()).As<String>();
}

}  // anonymous namespace


MaybeLocal<String> StringDecoder::DecodeData(Isolate* isolate,
                                             const char* data,
                                             size_t* nread_ptr) {
  Local<String> prepend, body;

  size_t nread = *nread_ptr;

  if (Encoding() == UTF8 ||
      Encoding() == UCS2 ||
      Encoding() == BASE64 ||
      Encoding() == BASE64URL) {
    // See if we want bytes to finish a character from the previous
    // chunk; if so, copy the new bytes to the missing bytes buffer
    // and create a small string from it that is to be prepended to the
    // main body.
    if (MissingBytes() > 0) {
      // There are never more bytes missing than the pre-calculated maximum.
      CHECK_LE(MissingBytes() + BufferedBytes(),
               kIncompleteCharactersEnd);
      if (Encoding() == UTF8) {
        // For UTF-8, we need special treatment to align with the V8 decoder:
        // If an incomplete character is found at a chunk boundary, we use
        // its remainder and pass it to V8 as-is.
        for (size_t i = 0; i < nread && i < MissingBytes(); ++i) {
          if ((data[i] & 0xC0) != 0x80) {
            // This byte is not a continuation byte even though it should have
            // been one. We stop decoding of the incomplete character at this
            // point (but still use the rest of the incomplete bytes from this
            // chunk) and assume that the new, unexpected byte starts a new one.
            state_[kMissingBytes] = 0;
            memcpy(IncompleteCharacterBuffer() + BufferedBytes(), data, i);
            state_[kBufferedBytes] += i;
            data += i;
            nread -= i;
            break;
          }
        }
      }

      size_t found_bytes =
          std::min(nread, static_cast<size_t>(MissingBytes()));
      memcpy(IncompleteCharacterBuffer() + BufferedBytes(),
             data,
             found_bytes);
      // Adjust the two buffers.
      data += found_bytes;
      nread -= found_bytes;

      state_[kMissingBytes] -= found_bytes;
      state_[kBufferedBytes] += found_bytes;

      if (LIKELY(MissingBytes() == 0)) {
        // If no more bytes are missing, create a small string that we
        // will later prepend.
        if (!MakeString(isolate,
                        IncompleteCharacterBuffer(),
                        BufferedBytes(),
                        Encoding()).ToLocal(&prepend)) {
          return MaybeLocal<String>();
        }

        *nread_ptr += BufferedBytes();
        // No more buffered bytes.
        state_[kBufferedBytes] = 0;
      }
    }

    // It could be that trying to finish the previous chunk already
    // consumed all data that we received in this chunk.
    if (UNLIKELY(nread == 0)) {
      body = !prepend.IsEmpty() ? prepend : String::Empty(isolate);
      prepend = Local<String>();
    } else {
      // If not, that means is no character left to finish at this point.
      DCHECK_EQ(MissingBytes(), 0);
      DCHECK_EQ(BufferedBytes(), 0);

      // See whether there is a character that we may have to cut off and
      // finish when receiving the next chunk.
      if (Encoding() == UTF8 && data[nread - 1] & 0x80) {
        // This is UTF-8 encoded data and we ended on a non-ASCII UTF-8 byte.
        // This means we'll need to figure out where the character to which
        // the byte belongs begins.
        for (size_t i = nread - 1; ; --i) {
          DCHECK_LT(i, nread);
          state_[kBufferedBytes]++;
          if ((data[i] & 0xC0) == 0x80) {
            // This byte does not start a character (a "trailing" byte).
            if (state_[kBufferedBytes] >= 4 || i == 0) {
              // We either have more then 4 trailing bytes (which means
              // the current character would not be inside the range for
              // valid Unicode, and in particular cannot be represented
              // through JavaScript's UTF-16-based approach to strings), or the
              // current buffer does not contain the start of an UTF-8 character
              // at all. Either way, this is invalid UTF8 and we can just
              // let the engine's decoder handle it.
              state_[kBufferedBytes] = 0;
              break;
            }
          } else {
            // Found the first byte of a UTF-8 character. By looking at the
            // upper bits we can tell how long the character *should* be.
            if ((data[i] & 0xE0) == 0xC0) {
              state_[kMissingBytes] = 2;
            } else if ((data[i] & 0xF0) == 0xE0) {
              state_[kMissingBytes] = 3;
            } else if ((data[i] & 0xF8) == 0xF0) {
              state_[kMissingBytes] = 4;
            } else {
              // This lead byte would indicate a character outside of the
              // representable range.
              state_[kBufferedBytes] = 0;
              break;
            }

            if (BufferedBytes() >= MissingBytes()) {
              // Received more or exactly as many trailing bytes than the lead
              // character would indicate. In the "==" case, we have valid
              // data and don't need to slice anything off;
              // in the ">" case, this is invalid UTF-8 anyway.
              state_[kMissingBytes] = 0;
              state_[kBufferedBytes] = 0;
            }

            state_[kMissingBytes] -= state_[kBufferedBytes];
            break;
          }
        }
      } else if (Encoding() == UCS2) {
        if ((nread % 2) == 1) {
          // We got half a codepoint, and need the second byte of it.
          state_[kBufferedBytes] = 1;
          state_[kMissingBytes] = 1;
        } else if ((data[nread - 1] & 0xFC) == 0xD8) {
          // Half a split UTF-16 character.
          state_[kBufferedBytes] = 2;
          state_[kMissingBytes] = 2;
        }
      } else if (Encoding() == BASE64 || Encoding() == BASE64URL) {
        state_[kBufferedBytes] = nread % 3;
        if (state_[kBufferedBytes] > 0)
          state_[kMissingBytes] = 3 - BufferedBytes();
      }

      if (BufferedBytes() > 0) {
        // Copy the requested number of buffered bytes from the end of the
        // input into the incomplete character buffer.
        nread -= BufferedBytes();
        *nread_ptr -= BufferedBytes();
        memcpy(IncompleteCharacterBuffer(), data + nread, BufferedBytes());
      }

      if (nread > 0) {
        if (!MakeString(isolate, data, nread, Encoding()).ToLocal(&body))
          return MaybeLocal<String>();
      } else {
        body = String::Empty(isolate);
      }
    }

    if (prepend.IsEmpty()) {
      return body;
    } else {
      return String::Concat(isolate, prepend, body);
    }
  } else {
    CHECK(Encoding() == ASCII || Encoding() == HEX || Encoding() == LATIN1);
    return MakeString(isolate, data, nread, Encoding());
  }
}

MaybeLocal<String> StringDecoder::FlushData(Isolate* isolate) {
  if (Encoding() == ASCII || Encoding() == HEX || Encoding() == LATIN1) {
    CHECK_EQ(MissingBytes(), 0);
    CHECK_EQ(BufferedBytes(), 0);
  }

  if (Encoding() == UCS2 && BufferedBytes() % 2 == 1) {
    // Ignore a single trailing byte, like the JS decoder does.
    state_[kMissingBytes]--;
    state_[kBufferedBytes]--;
  }

  if (BufferedBytes() == 0)
    return String::Empty(isolate);

  MaybeLocal<String> ret =
      MakeString(isolate,
                 IncompleteCharacterBuffer(),
                 BufferedBytes(),
                 Encoding());

  state_[kMissingBytes] = 0;
  state_[kBufferedBytes] = 0;

  return ret;
}

namespace {

void DecodeData(const FunctionCallbackInfo<Value>& args) {
  StringDecoder* decoder =
      reinterpret_cast<StringDecoder*>(Buffer::Data(args[0]));
  CHECK_NOT_NULL(decoder);

  CHECK(args[1]->IsArrayBufferView());
  ArrayBufferViewContents<char> content(args[1].As<ArrayBufferView>());
  size_t length = content.length();

  MaybeLocal<String> ret =
      decoder->DecodeData(args.GetIsolate(), content.data(), &length);
  if (!ret.IsEmpty())
    args.GetReturnValue().Set(ret.ToLocalChecked());
}

void FlushData(const FunctionCallbackInfo<Value>& args) {
  StringDecoder* decoder =
      reinterpret_cast<StringDecoder*>(Buffer::Data(args[0]));
  CHECK_NOT_NULL(decoder);
  MaybeLocal<String> ret = decoder->FlushData(args.GetIsolate());
  if (!ret.IsEmpty())
    args.GetReturnValue().Set(ret.ToLocalChecked());
}

void InitializeStringDecoder(Local<Object> target,
                             Local<Value> unused,
                             Local<Context> context,
                             void* priv) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();

#define SET_DECODER_CONSTANT(name)                                            \
  target->Set(context,                                                        \
              FIXED_ONE_BYTE_STRING(isolate, #name),                          \
              Integer::New(isolate, StringDecoder::name)).FromJust()

  SET_DECODER_CONSTANT(kIncompleteCharactersStart);
  SET_DECODER_CONSTANT(kIncompleteCharactersEnd);
  SET_DECODER_CONSTANT(kMissingBytes);
  SET_DECODER_CONSTANT(kBufferedBytes);
  SET_DECODER_CONSTANT(kEncodingField);
  SET_DECODER_CONSTANT(kNumFields);

  Local<Array> encodings = Array::New(isolate);
#define ADD_TO_ENCODINGS_ARRAY(cname, jsname)                                 \
  encodings->Set(context,                                                     \
                 static_cast<int32_t>(cname),                                 \
                 FIXED_ONE_BYTE_STRING(isolate, jsname)).FromJust()
  ADD_TO_ENCODINGS_ARRAY(ASCII, "ascii");
  ADD_TO_ENCODINGS_ARRAY(UTF8, "utf8");
  ADD_TO_ENCODINGS_ARRAY(BASE64, "base64");
  ADD_TO_ENCODINGS_ARRAY(BASE64URL, "base64url");
  ADD_TO_ENCODINGS_ARRAY(UCS2, "utf16le");
  ADD_TO_ENCODINGS_ARRAY(HEX, "hex");
  ADD_TO_ENCODINGS_ARRAY(BUFFER, "buffer");
  ADD_TO_ENCODINGS_ARRAY(LATIN1, "latin1");

  target->Set(context,
              FIXED_ONE_BYTE_STRING(isolate, "encodings"),
              encodings).Check();

  target->Set(context,
              FIXED_ONE_BYTE_STRING(isolate, "kSize"),
              Integer::New(isolate, sizeof(StringDecoder))).Check();

  SetMethod(context, target, "decode", DecodeData);
  SetMethod(context, target, "flush", FlushData);
}

}  // anonymous namespace

void RegisterStringDecoderExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(DecodeData);
  registry->Register(FlushData);
}

}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(string_decoder,
                                    node::InitializeStringDecoder)
NODE_BINDING_EXTERNAL_REFERENCE(string_decoder,
                                node::RegisterStringDecoderExternalReferences)

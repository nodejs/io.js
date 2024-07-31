#ifndef SRC_STRING_DECODER_H_
#define SRC_STRING_DECODER_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "node.h"

namespace node {

class StringDecoder {
 public:
  StringDecoder() { state_[kEncodingField] = BUFFER; }
  inline enum encoding Encoding() const;

  inline char* IncompleteCharacterBuffer();
  inline unsigned MissingBytes() const;
  inline unsigned BufferedBytes() const;

  // Decode a string from the specified encoding.
  // The value pointed to by `nread` will be modified to reflect that
  // less data may have been read because it ended on an incomplete character
  // and more data may have been read because a previously incomplete character
  // was finished.
  v8::MaybeLocal<v8::String> DecodeData(v8::Isolate* isolate,
                                        const char* data,
                                        size_t* nread);
  // Flush an incomplete character. For character encodings like UTF8 this
  // means printing replacement characters, buf for e.g. Base64 the returned
  // string contains more data.
  v8::MaybeLocal<v8::String> FlushData(v8::Isolate* isolate);

  enum Fields {
    kIncompleteCharactersStart = 0,
    kIncompleteCharactersEnd = 4,
    kMissingBytes = 4,
    kBufferedBytes = 5,
    kEncodingField = 6,
    kNumFields = 7
  };

 private:
  uint8_t state_[kNumFields] = {};
};

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif   // SRC_STRING_DECODER_H_

#ifndef SRC_STRING_DECODER_INL_H_
#define SRC_STRING_DECODER_INL_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "string_decoder.h"

namespace node {

enum encoding StringDecoder::Encoding() const {
  return static_cast<enum encoding>(state_[kEncodingField]);
}

unsigned StringDecoder::BufferedBytes() const {
  return state_[kBufferedBytes];
}

unsigned StringDecoder::MissingBytes() const {
  return state_[kMissingBytes];
}

char* StringDecoder::IncompleteCharacterBuffer() {
  return reinterpret_cast<char*>(state_ + kIncompleteCharactersStart);
}


}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif   // SRC_STRING_DECODER_INL_H_

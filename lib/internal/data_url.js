'use strict';

const {
  RegExpPrototypeExec,
  RegExpPrototypeSymbolReplace,
  StringFromCharCodeApply,
  StringPrototypeCharCodeAt,
  StringPrototypeIndexOf,
  StringPrototypeSlice,
  TypedArrayPrototypeSubarray,
  Uint8Array,
} = primordials;

const assert = require('internal/assert');
const { Buffer } = require('buffer');
const { MIMEType } = require('internal/mime');

let encoder;
function lazyEncoder() {
  if (encoder === undefined) {
    const { TextEncoder } = require('internal/encoding');
    encoder = new TextEncoder();
  }

  return encoder;
}

const ASCII_WHITESPACE_REPLACE_REGEX = /[\u0009\u000A\u000C\u000D\u0020]/g // eslint-disable-line

// https://fetch.spec.whatwg.org/#data-url-processor
/** @param {URL} dataURL */
function dataURLProcessor(dataURL) {
  // 1. Assert: dataURL's scheme is "data".
  assert(dataURL.protocol === 'data:');

  // 2. Let input be the result of running the URL
  // serializer on dataURL with exclude fragment
  // set to true.
  let input = URLSerializer(dataURL, true);

  // 3. Remove the leading "data:" string from input.
  input = StringPrototypeSlice(input, 5);

  // 4. Let position point at the start of input.
  const position = { position: 0 };

  // 5. Let mimeType be the result of collecting a
  // sequence of code points that are not equal
  // to U+002C (,), given position.
  let mimeType = collectASequenceOfCodePointsFast(
    ',',
    input,
    position,
  );

  // 6. Strip leading and trailing ASCII whitespace
  // from mimeType.
  // Undici implementation note: we need to store the
  // length because if the mimetype has spaces removed,
  // the wrong amount will be sliced from the input in
  // step #9
  const mimeTypeLength = mimeType.length;
  mimeType = removeASCIIWhitespace(mimeType, true, true);

  // 7. If position is past the end of input, then
  // return failure
  if (position.position >= input.length) {
    return 'failure';
  }

  // 8. Advance position by 1.
  position.position++;

  // 9. Let encodedBody be the remainder of input.
  const encodedBody = StringPrototypeSlice(input, mimeTypeLength + 1);

  // 10. Let body be the percent-decoding of encodedBody.
  let body = stringPercentDecode(encodedBody);

  // 11. If mimeType ends with U+003B (;), followed by
  // zero or more U+0020 SPACE, followed by an ASCII
  // case-insensitive match for "base64", then:
  if (RegExpPrototypeExec(/;(\u0020){0,}base64$/i, mimeType) !== null) {
    // 1. Let stringBody be the isomorphic decode of body.
    const stringBody = isomorphicDecode(body);

    // 2. Set body to the forgiving-base64 decode of
    // stringBody.
    body = forgivingBase64(stringBody);

    // 3. If body is failure, then return failure.
    if (body === 'failure') {
      return 'failure';
    }

    // 4. Remove the last 6 code points from mimeType.
    mimeType = StringPrototypeSlice(mimeType, 0, -6);

    // 5. Remove trailing U+0020 SPACE code points from mimeType,
    // if any.
    mimeType = RegExpPrototypeSymbolReplace(/(\u0020)+$/, mimeType, '');

    // 6. Remove the last U+003B (;) code point from mimeType.
    mimeType = StringPrototypeSlice(mimeType, 0, -1);
  }

  // 12. If mimeType starts with U+003B (;), then prepend
  // "text/plain" to mimeType.
  if (mimeType[0] === ';') {
    mimeType = 'text/plain' + mimeType;
  }

  // 13. Let mimeTypeRecord be the result of parsing
  // mimeType.
  // 14. If mimeTypeRecord is failure, then set
  // mimeTypeRecord to text/plain;charset=US-ASCII.
  let mimeTypeRecord;

  try {
    mimeTypeRecord = new MIMEType(mimeType);
  } catch {
    mimeTypeRecord = new MIMEType('text/plain;charset=US-ASCII');
  }

  // 15. Return a new data: URL struct whose MIME
  // type is mimeTypeRecord and body is body.
  // https://fetch.spec.whatwg.org/#data-url-struct
  return { mimeType: mimeTypeRecord, body };
}

// https://url.spec.whatwg.org/#concept-url-serializer
/**
 * @param {URL} url
 * @param {boolean} excludeFragment
 */
function URLSerializer(url, excludeFragment = false) {
  const { href } = url;

  if (!excludeFragment) {
    return href;
  }

  const hashLength = url.hash.length;
  const serialized = hashLength === 0 ? href : StringPrototypeSlice(href, 0, href.length - hashLength);

  if (!hashLength && href[href.length - 1] === '#') {
    return StringPrototypeSlice(serialized, 0, -1);
  }

  return serialized;
}

/**
 * A faster collectASequenceOfCodePoints that only works when comparing a single character.
 * @param {string} char
 * @param {string} input
 * @param {{ position: number }} position
 */
function collectASequenceOfCodePointsFast(char, input, position) {
  const idx = StringPrototypeIndexOf(input, char, position.position);
  const start = position.position;

  if (idx === -1) {
    position.position = input.length;
    return StringPrototypeSlice(input, start);
  }

  position.position = idx;
  return StringPrototypeSlice(input, start, position.position);
}

// https://url.spec.whatwg.org/#string-percent-decode
/** @param {string} input */
function stringPercentDecode(input) {
  // 1. Let bytes be the UTF-8 encoding of input.
  const bytes = lazyEncoder().encode(input);

  // 2. Return the percent-decoding of bytes.
  return percentDecode(bytes);
}

/**
 * @param {number} byte
 */
function isHexCharByte(byte) {
  // 0-9 A-F a-f
  return (byte >= 0x30 && byte <= 0x39) || (byte >= 0x41 && byte <= 0x46) || (byte >= 0x61 && byte <= 0x66);
}

/**
 * @param {number} byte
 */
function hexByteToNumber(byte) {
  return (
    // 0-9
    byte >= 0x30 && byte <= 0x39 ?
      (byte - 48) :
    // Convert to uppercase
    // ((byte & 0xDF) - 65) + 10
      ((byte & 0xDF) - 55)
  );
}

// https://url.spec.whatwg.org/#percent-decode
/** @param {Uint8Array} input */
function percentDecode(input) {
  const length = input.length;
  // 1. Let output be an empty byte sequence.
  /** @type {Uint8Array} */
  const output = new Uint8Array(length);
  let j = 0;
  // 2. For each byte byte in input:
  for (let i = 0; i < length; ++i) {
    const byte = input[i];

    // 1. If byte is not 0x25 (%), then append byte to output.
    if (byte !== 0x25) {
      output[j++] = byte;

    // 2. Otherwise, if byte is 0x25 (%) and the next two bytes
    // after byte in input are not in the ranges
    // 0x30 (0) to 0x39 (9), 0x41 (A) to 0x46 (F),
    // and 0x61 (a) to 0x66 (f), all inclusive, append byte
    // to output.
    } else if (
      byte === 0x25 &&
      !(isHexCharByte(input[i + 1]) && isHexCharByte(input[i + 2]))
    ) {
      output[j++] = 0x25;

    // 3. Otherwise:
    } else {
      // 1. Let bytePoint be the two bytes after byte in input,
      // decoded, and then interpreted as hexadecimal number.
      // 2. Append a byte whose value is bytePoint to output.
      output[j++] = (hexByteToNumber(input[i + 1]) << 4) | hexByteToNumber(input[i + 2]);

      // 3. Skip the next two bytes in input.
      i += 2;
    }
  }

  // 3. Return output.
  return length === j ? output : TypedArrayPrototypeSubarray(output, 0, j);
}

// https://infra.spec.whatwg.org/#forgiving-base64-decode
/** @param {string} data */
function forgivingBase64(data) {
  // 1. Remove all ASCII whitespace from data.
  data = RegExpPrototypeSymbolReplace(ASCII_WHITESPACE_REPLACE_REGEX, data, '');

  let dataLength = data.length;
  // 2. If data's code point length divides by 4 leaving
  // no remainder, then:
  if (dataLength % 4 === 0) {
    // 1. If data ends with one or two U+003D (=) code points,
    // then remove them from data.
    if (data[dataLength - 1] === '=') {
      --dataLength;
      if (data[dataLength - 1] === '=') {
        --dataLength;
      }
    }
  }

  // 3. If data's code point length divides by 4 leaving
  // a remainder of 1, then return failure.
  if (dataLength % 4 === 1) {
    return 'failure';
  }

  // 4. If data contains a code point that is not one of
  //  U+002B (+)
  //  U+002F (/)
  //  ASCII alphanumeric
  // then return failure.
  if (RegExpPrototypeExec(/[^+/0-9A-Za-z]/, data.length === dataLength ? data : StringPrototypeSlice(data, 0, dataLength)) !== null) {
    return 'failure';
  }

  const buffer = Buffer.from(data, 'base64');
  return new Uint8Array(buffer.buffer, buffer.byteOffset, buffer.byteLength);
}

/**
 * @see https://infra.spec.whatwg.org/#ascii-whitespace
 * @param {number} char
 */
function isASCIIWhitespace(char) {
  // "\r\n\t\f "
  return char === 0x00d || char === 0x00a || char === 0x009 || char === 0x00c || char === 0x020;
}

/**
 * @see https://infra.spec.whatwg.org/#strip-leading-and-trailing-ascii-whitespace
 * @param {string} str
 * @param {boolean} [leading=true]
 * @param {boolean} [trailing=true]
 */
function removeASCIIWhitespace(str, leading = true, trailing = true) {
  return removeChars(str, leading, trailing, isASCIIWhitespace);
}

/**
 * @param {string} str
 * @param {boolean} leading
 * @param {boolean} trailing
 * @param {(charCode: number) => boolean} predicate
 */
function removeChars(str, leading, trailing, predicate) {
  let lead = 0;
  let trail = str.length - 1;

  if (leading) {
    while (lead < str.length && predicate(StringPrototypeCharCodeAt(str, lead))) lead++;
  }

  if (trailing) {
    while (trail > 0 && predicate(StringPrototypeCharCodeAt(str, trail))) trail--;
  }

  return lead === 0 && trail === str.length - 1 ? str : StringPrototypeSlice(str, lead, trail + 1);
}

/**
 * @see https://infra.spec.whatwg.org/#isomorphic-decode
 * @param {Uint8Array} input
 * @returns {string}
 */
function isomorphicDecode(input) {
  // 1. To isomorphic decode a byte sequence input, return a string whose code point
  //    length is equal to input's length and whose code points have the same values
  //    as the values of input's bytes, in the same order.
  const length = input.length;
  if ((2 << 15) - 1 > length) {
    return StringFromCharCodeApply(input);
  }
  let result = ''; let i = 0;
  let addition = (2 << 15) - 1;
  while (i < length) {
    if (i + addition > length) {
      addition = length - i;
    }
    result += StringFromCharCodeApply(TypedArrayPrototypeSubarray(input, i, i += addition));
  }
  return result;
}

module.exports = {
  dataURLProcessor,
};

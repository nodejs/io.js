'use strict';

// An implementation of the WHATWG Encoding Standard
// https://encoding.spec.whatwg.org

const {
  Boolean,
  ObjectDefineProperties,
  ObjectGetOwnPropertyDescriptors,
  ObjectSetPrototypeOf,
  ObjectValues,
  SafeMap,
  StringPrototypeSlice,
  Symbol,
  SymbolToStringTag,
  Uint8Array,
} = primordials;

const {
  ERR_ENCODING_NOT_SUPPORTED,
  ERR_INVALID_ARG_TYPE,
  ERR_INVALID_THIS,
  ERR_NO_ICU,
} = require('internal/errors').codes;
const kHandle = Symbol('handle');
const kFlags = Symbol('flags');
const kEncoding = Symbol('encoding');
const kDecoder = Symbol('decoder');
const kEncoder = Symbol('encoder');
const kFatal = Symbol('kFatal');
const kUTF8FastPath = Symbol('kUTF8FastPath');
const kLatin1FastPath = Symbol('kLatin1FastPath');
const kIgnoreBOM = Symbol('kIgnoreBOM');

const {
  getConstructorOf,
  customInspectSymbol: inspect,
  kEmptyObject,
  kEnumerableProperty,
} = require('internal/util');

const {
  isAnyArrayBuffer,
  isArrayBufferView,
  isUint8Array,
} = require('internal/util/types');

const {
  validateString,
  validateObject,
  kValidateObjectAllowObjectsAndNull,
} = require('internal/validators');
const binding = internalBinding('encoding_binding');
const {
  encodeInto,
  encodeIntoResults,
  encodeUtf8String,
  decodeUTF8,
  decodeLatin1,
} = binding;

const { Buffer } = require('buffer');

function validateEncoder(obj) {
  if (obj == null || obj[kEncoder] !== true)
    throw new ERR_INVALID_THIS('TextEncoder');
}

function validateDecoder(obj) {
  if (obj == null || obj[kDecoder] !== true)
    throw new ERR_INVALID_THIS('TextDecoder');
}

const CONVERTER_FLAGS_FLUSH = 0x1;
const CONVERTER_FLAGS_FATAL = 0x2;
const CONVERTER_FLAGS_IGNORE_BOM = 0x4;

const empty = new Uint8Array(0);

const encodings = new SafeMap([
  ['unicode-1-1-utf-8', 'utf-8'],
  ['unicode11utf8', 'utf-8'],
  ['unicode20utf8', 'utf-8'],
  ['utf8', 'utf-8'],
  ['utf-8', 'utf-8'],
  ['x-unicode20utf8', 'utf-8'],
  ['866', 'ibm866'],
  ['cp866', 'ibm866'],
  ['csibm866', 'ibm866'],
  ['ibm866', 'ibm866'],
  ['csisolatin2', 'iso-8859-2'],
  ['iso-8859-2', 'iso-8859-2'],
  ['iso-ir-101', 'iso-8859-2'],
  ['iso8859-2', 'iso-8859-2'],
  ['iso88592', 'iso-8859-2'],
  ['iso_8859-2', 'iso-8859-2'],
  ['iso_8859-2:1987', 'iso-8859-2'],
  ['l2', 'iso-8859-2'],
  ['latin2', 'iso-8859-2'],
  ['csisolatin3', 'iso-8859-3'],
  ['iso-8859-3', 'iso-8859-3'],
  ['iso-ir-109', 'iso-8859-3'],
  ['iso8859-3', 'iso-8859-3'],
  ['iso88593', 'iso-8859-3'],
  ['iso_8859-3', 'iso-8859-3'],
  ['iso_8859-3:1988', 'iso-8859-3'],
  ['l3', 'iso-8859-3'],
  ['latin3', 'iso-8859-3'],
  ['csisolatin4', 'iso-8859-4'],
  ['iso-8859-4', 'iso-8859-4'],
  ['iso-ir-110', 'iso-8859-4'],
  ['iso8859-4', 'iso-8859-4'],
  ['iso88594', 'iso-8859-4'],
  ['iso_8859-4', 'iso-8859-4'],
  ['iso_8859-4:1988', 'iso-8859-4'],
  ['l4', 'iso-8859-4'],
  ['latin4', 'iso-8859-4'],
  ['csisolatincyrillic', 'iso-8859-5'],
  ['cyrillic', 'iso-8859-5'],
  ['iso-8859-5', 'iso-8859-5'],
  ['iso-ir-144', 'iso-8859-5'],
  ['iso8859-5', 'iso-8859-5'],
  ['iso88595', 'iso-8859-5'],
  ['iso_8859-5', 'iso-8859-5'],
  ['iso_8859-5:1988', 'iso-8859-5'],
  ['arabic', 'iso-8859-6'],
  ['asmo-708', 'iso-8859-6'],
  ['csiso88596e', 'iso-8859-6'],
  ['csiso88596i', 'iso-8859-6'],
  ['csisolatinarabic', 'iso-8859-6'],
  ['ecma-114', 'iso-8859-6'],
  ['iso-8859-6', 'iso-8859-6'],
  ['iso-8859-6-e', 'iso-8859-6'],
  ['iso-8859-6-i', 'iso-8859-6'],
  ['iso-ir-127', 'iso-8859-6'],
  ['iso8859-6', 'iso-8859-6'],
  ['iso88596', 'iso-8859-6'],
  ['iso_8859-6', 'iso-8859-6'],
  ['iso_8859-6:1987', 'iso-8859-6'],
  ['csisolatingreek', 'iso-8859-7'],
  ['ecma-118', 'iso-8859-7'],
  ['elot_928', 'iso-8859-7'],
  ['greek', 'iso-8859-7'],
  ['greek8', 'iso-8859-7'],
  ['iso-8859-7', 'iso-8859-7'],
  ['iso-ir-126', 'iso-8859-7'],
  ['iso8859-7', 'iso-8859-7'],
  ['iso88597', 'iso-8859-7'],
  ['iso_8859-7', 'iso-8859-7'],
  ['iso_8859-7:1987', 'iso-8859-7'],
  ['sun_eu_greek', 'iso-8859-7'],
  ['csiso88598e', 'iso-8859-8'],
  ['csisolatinhebrew', 'iso-8859-8'],
  ['hebrew', 'iso-8859-8'],
  ['iso-8859-8', 'iso-8859-8'],
  ['iso-8859-8-e', 'iso-8859-8'],
  ['iso-ir-138', 'iso-8859-8'],
  ['iso8859-8', 'iso-8859-8'],
  ['iso88598', 'iso-8859-8'],
  ['iso_8859-8', 'iso-8859-8'],
  ['iso_8859-8:1988', 'iso-8859-8'],
  ['visual', 'iso-8859-8'],
  ['csiso88598i', 'iso-8859-8-i'],
  ['iso-8859-8-i', 'iso-8859-8-i'],
  ['logical', 'iso-8859-8-i'],
  ['csisolatin6', 'iso-8859-10'],
  ['iso-8859-10', 'iso-8859-10'],
  ['iso-ir-157', 'iso-8859-10'],
  ['iso8859-10', 'iso-8859-10'],
  ['iso885910', 'iso-8859-10'],
  ['l6', 'iso-8859-10'],
  ['latin6', 'iso-8859-10'],
  ['iso-8859-13', 'iso-8859-13'],
  ['iso8859-13', 'iso-8859-13'],
  ['iso885913', 'iso-8859-13'],
  ['iso-8859-14', 'iso-8859-14'],
  ['iso8859-14', 'iso-8859-14'],
  ['iso885914', 'iso-8859-14'],
  ['csisolatin9', 'iso-8859-15'],
  ['iso-8859-15', 'iso-8859-15'],
  ['iso8859-15', 'iso-8859-15'],
  ['iso885915', 'iso-8859-15'],
  ['iso_8859-15', 'iso-8859-15'],
  ['l9', 'iso-8859-15'],
  ['iso-8859-16', 'iso-8859-16'],
  ['cskoi8r', 'koi8-r'],
  ['koi', 'koi8-r'],
  ['koi8', 'koi8-r'],
  ['koi8-r', 'koi8-r'],
  ['koi8_r', 'koi8-r'],
  ['koi8-ru', 'koi8-u'],
  ['koi8-u', 'koi8-u'],
  ['csmacintosh', 'macintosh'],
  ['mac', 'macintosh'],
  ['macintosh', 'macintosh'],
  ['x-mac-roman', 'macintosh'],
  ['dos-874', 'windows-874'],
  ['iso-8859-11', 'windows-874'],
  ['iso8859-11', 'windows-874'],
  ['iso885911', 'windows-874'],
  ['tis-620', 'windows-874'],
  ['windows-874', 'windows-874'],
  ['cp1250', 'windows-1250'],
  ['windows-1250', 'windows-1250'],
  ['x-cp1250', 'windows-1250'],
  ['cp1251', 'windows-1251'],
  ['windows-1251', 'windows-1251'],
  ['x-cp1251', 'windows-1251'],
  ['ansi_x3.4-1968', 'windows-1252'],
  ['ascii', 'windows-1252'],
  ['cp1252', 'windows-1252'],
  ['cp819', 'windows-1252'],
  ['csisolatin1', 'windows-1252'],
  ['ibm819', 'windows-1252'],
  ['iso-8859-1', 'windows-1252'],
  ['iso-ir-100', 'windows-1252'],
  ['iso8859-1', 'windows-1252'],
  ['iso88591', 'windows-1252'],
  ['iso_8859-1', 'windows-1252'],
  ['iso_8859-1:1987', 'windows-1252'],
  ['l1', 'windows-1252'],
  ['latin1', 'windows-1252'],
  ['us-ascii', 'windows-1252'],
  ['windows-1252', 'windows-1252'],
  ['x-cp1252', 'windows-1252'],
  ['cp1253', 'windows-1253'],
  ['windows-1253', 'windows-1253'],
  ['x-cp1253', 'windows-1253'],
  ['cp1254', 'windows-1254'],
  ['csisolatin5', 'windows-1254'],
  ['iso-8859-9', 'windows-1254'],
  ['iso-ir-148', 'windows-1254'],
  ['iso8859-9', 'windows-1254'],
  ['iso88599', 'windows-1254'],
  ['iso_8859-9', 'windows-1254'],
  ['iso_8859-9:1989', 'windows-1254'],
  ['l5', 'windows-1254'],
  ['latin5', 'windows-1254'],
  ['windows-1254', 'windows-1254'],
  ['x-cp1254', 'windows-1254'],
  ['cp1255', 'windows-1255'],
  ['windows-1255', 'windows-1255'],
  ['x-cp1255', 'windows-1255'],
  ['cp1256', 'windows-1256'],
  ['windows-1256', 'windows-1256'],
  ['x-cp1256', 'windows-1256'],
  ['cp1257', 'windows-1257'],
  ['windows-1257', 'windows-1257'],
  ['x-cp1257', 'windows-1257'],
  ['cp1258', 'windows-1258'],
  ['windows-1258', 'windows-1258'],
  ['x-cp1258', 'windows-1258'],
  ['x-mac-cyrillic', 'x-mac-cyrillic'],
  ['x-mac-ukrainian', 'x-mac-cyrillic'],
  ['chinese', 'gbk'],
  ['csgb2312', 'gbk'],
  ['csiso58gb231280', 'gbk'],
  ['gb2312', 'gbk'],
  ['gb_2312', 'gbk'],
  ['gb_2312-80', 'gbk'],
  ['gbk', 'gbk'],
  ['iso-ir-58', 'gbk'],
  ['x-gbk', 'gbk'],
  ['gb18030', 'gb18030'],
  ['big5', 'big5'],
  ['big5-hkscs', 'big5'],
  ['cn-big5', 'big5'],
  ['csbig5', 'big5'],
  ['x-x-big5', 'big5'],
  ['cseucpkdfmtjapanese', 'euc-jp'],
  ['euc-jp', 'euc-jp'],
  ['x-euc-jp', 'euc-jp'],
  ['csiso2022jp', 'iso-2022-jp'],
  ['iso-2022-jp', 'iso-2022-jp'],
  ['csshiftjis', 'shift_jis'],
  ['ms932', 'shift_jis'],
  ['ms_kanji', 'shift_jis'],
  ['shift-jis', 'shift_jis'],
  ['shift_jis', 'shift_jis'],
  ['sjis', 'shift_jis'],
  ['windows-31j', 'shift_jis'],
  ['x-sjis', 'shift_jis'],
  ['cseuckr', 'euc-kr'],
  ['csksc56011987', 'euc-kr'],
  ['euc-kr', 'euc-kr'],
  ['iso-ir-149', 'euc-kr'],
  ['korean', 'euc-kr'],
  ['ks_c_5601-1987', 'euc-kr'],
  ['ks_c_5601-1989', 'euc-kr'],
  ['ksc5601', 'euc-kr'],
  ['ksc_5601', 'euc-kr'],
  ['windows-949', 'euc-kr'],
  ['csiso2022kr', 'replacement'],
  ['hz-gb-2312', 'replacement'],
  ['iso-2022-cn', 'replacement'],
  ['iso-2022-cn-ext', 'replacement'],
  ['iso-2022-kr', 'replacement'],
  ['replacement', 'replacement'],
  ['unicodefffe', 'utf-16be'],
  ['utf-16be', 'utf-16be'],
  ['csunicode', 'utf-16le'],
  ['iso-10646-ucs-2', 'utf-16le'],
  ['ucs-2', 'utf-16le'],
  ['unicode', 'utf-16le'],
  ['unicodefeff', 'utf-16le'],
  ['utf-16le', 'utf-16le'],
  ['utf-16', 'utf-16le'],
  ['x-user-defined', 'x-user-defined'],
]);

// Unfortunately, String.prototype.trim also removes non-ascii whitespace,
// so we have to do this manually
function trimAsciiWhitespace(label) {
  let s = 0;
  let e = label.length;
  while (s < e && (
    label[s] === '\u0009' ||
    label[s] === '\u000a' ||
    label[s] === '\u000c' ||
    label[s] === '\u000d' ||
    label[s] === '\u0020')) {
    s++;
  }
  while (e > s && (
    label[e - 1] === '\u0009' ||
    label[e - 1] === '\u000a' ||
    label[e - 1] === '\u000c' ||
    label[e - 1] === '\u000d' ||
    label[e - 1] === '\u0020')) {
    e--;
  }
  return StringPrototypeSlice(label, s, e);
}

function getEncodingFromLabel(label) {
  const enc = encodings.get(label);
  if (enc !== undefined) return enc;
  return encodings.get(trimAsciiWhitespace(label.toLowerCase()));
}

class TextEncoder {
  constructor() {
    this[kEncoder] = true;
  }

  get encoding() {
    validateEncoder(this);
    return 'utf-8';
  }

  encode(input = '') {
    validateEncoder(this);
    return encodeUtf8String(`${input}`);
  }

  encodeInto(src, dest) {
    validateEncoder(this);
    validateString(src, 'src');
    if (!dest || !isUint8Array(dest))
      throw new ERR_INVALID_ARG_TYPE('dest', 'Uint8Array', dest);

    encodeInto(src, dest);
    // We need to read from the binding here since the buffer gets refreshed
    // from the snapshot.
    const { 0: read, 1: written } = encodeIntoResults;
    return { read, written };
  }

  [inspect](depth, opts) {
    validateEncoder(this);
    if (typeof depth === 'number' && depth < 0)
      return this;
    const ctor = getConstructorOf(this);
    const obj = { __proto__: {
      constructor: ctor === null ? TextEncoder : ctor,
    } };
    obj.encoding = this.encoding;
    // Lazy to avoid circular dependency
    return require('internal/util/inspect').inspect(obj, opts);
  }
}

ObjectDefineProperties(
  TextEncoder.prototype, {
    'encode': kEnumerableProperty,
    'encodeInto': kEnumerableProperty,
    'encoding': kEnumerableProperty,
    [SymbolToStringTag]: { __proto__: null, configurable: true, value: 'TextEncoder' },
  });

const TextDecoder =
  internalBinding('config').hasIntl ?
    makeTextDecoderICU() :
    makeTextDecoderJS();

function makeTextDecoderICU() {
  const {
    decode: _decode,
    getConverter,
  } = internalBinding('icu');

  class TextDecoder {
    constructor(encoding = 'utf-8', options = kEmptyObject) {
      encoding = `${encoding}`;
      validateObject(options, 'options', kValidateObjectAllowObjectsAndNull);

      const enc = getEncodingFromLabel(encoding);
      if (enc === undefined)
        throw new ERR_ENCODING_NOT_SUPPORTED(encoding);

      let flags = 0;
      if (options !== null) {
        flags |= options.fatal ? CONVERTER_FLAGS_FATAL : 0;
        flags |= options.ignoreBOM ? CONVERTER_FLAGS_IGNORE_BOM : 0;
      }

      this[kDecoder] = true;
      this[kFlags] = flags;
      this[kEncoding] = enc;
      this[kIgnoreBOM] = Boolean(options?.ignoreBOM);
      this[kFatal] = Boolean(options?.fatal);
      // Only support fast path for UTF-8.
      this[kUTF8FastPath] = enc === 'utf-8';
      this[kLatin1FastPath] = enc === 'windows-1252';
      this[kHandle] = undefined;

      if (!this[kUTF8FastPath] && !this[kLatin1FastPath]) {
        this.#prepareConverter();
      }
    }

    #prepareConverter() {
      if (this[kHandle] !== undefined) return;
      const handle = getConverter(this[kEncoding], this[kFlags]);
      if (handle === undefined)
        throw new ERR_ENCODING_NOT_SUPPORTED(this[kEncoding]);
      this[kHandle] = handle;
    }

    decode(input = empty, options = kEmptyObject) {
      validateDecoder(this);

      this[kUTF8FastPath] &&= !(options?.stream);
      this[kLatin1FastPath] &&= !(options?.stream);

      if (this[kUTF8FastPath]) {
        return decodeUTF8(input, this[kIgnoreBOM], this[kFatal]);
      }

      if (this[kLatin1FastPath]) {
        return decodeLatin1(input, this[kIgnoreBOM], this[kFatal]);
      }

      this.#prepareConverter();

      validateObject(options, 'options', kValidateObjectAllowObjectsAndNull);

      let flags = 0;
      if (options !== null)
        flags |= options.stream ? 0 : CONVERTER_FLAGS_FLUSH;

      return _decode(this[kHandle], input, flags, this.encoding);
    }
  }

  return TextDecoder;
}

function makeTextDecoderJS() {
  let StringDecoder;
  function lazyStringDecoder() {
    if (StringDecoder === undefined)
      ({ StringDecoder } = require('string_decoder'));
    return StringDecoder;
  }

  const kBOMSeen = Symbol('BOM seen');

  function hasConverter(encoding) {
    return encoding === 'utf-8' || encoding === 'utf-16le';
  }

  class TextDecoder {
    constructor(encoding = 'utf-8', options = kEmptyObject) {
      encoding = `${encoding}`;
      validateObject(options, 'options', kValidateObjectAllowObjectsAndNull);

      const enc = getEncodingFromLabel(encoding);
      if (enc === undefined || !hasConverter(enc))
        throw new ERR_ENCODING_NOT_SUPPORTED(encoding);

      let flags = 0;
      if (options !== null) {
        if (options.fatal) {
          throw new ERR_NO_ICU('"fatal" option');
        }
        flags |= options.ignoreBOM ? CONVERTER_FLAGS_IGNORE_BOM : 0;
      }

      this[kDecoder] = true;
      // StringDecoder will normalize WHATWG encoding to Node.js encoding.
      this[kHandle] = new (lazyStringDecoder())(enc);
      this[kFlags] = flags;
      this[kEncoding] = enc;
      this[kBOMSeen] = false;
    }

    decode(input = empty, options = kEmptyObject) {
      validateDecoder(this);
      if (isAnyArrayBuffer(input)) {
        try {
          input = Buffer.from(input);
        } catch {
          input = empty;
        }
      } else if (isArrayBufferView(input)) {
        try {
          input = Buffer.from(input.buffer, input.byteOffset,
                              input.byteLength);
        } catch {
          input = empty;
        }
      } else {
        throw new ERR_INVALID_ARG_TYPE('input',
                                       ['ArrayBuffer', 'ArrayBufferView'],
                                       input);
      }
      validateObject(options, 'options', kValidateObjectAllowObjectsAndNull);

      if (this[kFlags] & CONVERTER_FLAGS_FLUSH) {
        this[kBOMSeen] = false;
      }

      if (options !== null && options.stream) {
        this[kFlags] &= ~CONVERTER_FLAGS_FLUSH;
      } else {
        this[kFlags] |= CONVERTER_FLAGS_FLUSH;
      }

      let result = this[kFlags] & CONVERTER_FLAGS_FLUSH ?
        this[kHandle].end(input) :
        this[kHandle].write(input);

      if (result.length > 0 &&
          !this[kBOMSeen] &&
          !(this[kFlags] & CONVERTER_FLAGS_IGNORE_BOM)) {
        // If the very first result in the stream is a BOM, and we are not
        // explicitly told to ignore it, then we discard it.
        if (result[0] === '\ufeff') {
          result = StringPrototypeSlice(result, 1);
        }
        this[kBOMSeen] = true;
      }

      return result;
    }
  }

  return TextDecoder;
}

// Mix in some shared properties.
const sharedProperties = ObjectGetOwnPropertyDescriptors({
  get encoding() {
    validateDecoder(this);
    return this[kEncoding];
  },

  get fatal() {
    validateDecoder(this);
    return (this[kFlags] & CONVERTER_FLAGS_FATAL) === CONVERTER_FLAGS_FATAL;
  },

  get ignoreBOM() {
    validateDecoder(this);
    return (this[kFlags] & CONVERTER_FLAGS_IGNORE_BOM) ===
              CONVERTER_FLAGS_IGNORE_BOM;
  },

  [inspect](depth, opts) {
    validateDecoder(this);
    if (typeof depth === 'number' && depth < 0)
      return this;
    const constructor = getConstructorOf(this) || TextDecoder;
    const obj = { __proto__: { constructor } };
    obj.encoding = this.encoding;
    obj.fatal = this.fatal;
    obj.ignoreBOM = this.ignoreBOM;
    if (opts.showHidden) {
      obj[kFlags] = this[kFlags];
      obj[kHandle] = this[kHandle];
    }
    // Lazy to avoid circular dependency
    const { inspect } = require('internal/util/inspect');
    return `${constructor.name} ${inspect(obj)}`;
  },
});
const propertiesValues = ObjectValues(sharedProperties);
for (let i = 0; i < propertiesValues.length; i++) {
  // We want to use null-prototype objects to not rely on globally mutable
  // %Object.prototype%.
  ObjectSetPrototypeOf(propertiesValues[i], null);
}
sharedProperties[inspect].enumerable = false;

ObjectDefineProperties(TextDecoder.prototype, {
  decode: kEnumerableProperty,
  ...sharedProperties,
  [SymbolToStringTag]: {
    __proto__: null,
    configurable: true,
    value: 'TextDecoder',
  },
});

module.exports = {
  getEncodingFromLabel,
  TextDecoder,
  TextEncoder,
};

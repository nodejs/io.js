'use strict';

const {
  FunctionPrototypeCall,
  ObjectDefineProperty,
  RegExpPrototypeExec,
  SafeMap,
  SafeStringPrototypeSearch,
  StringPrototypeCharAt,
  StringPrototypeIndexOf,
  StringPrototypeSlice,
  StringPrototypeToLowerCase,
  SymbolIterator,
} = primordials;
const {
  ERR_INVALID_MIME_SYNTAX,
} = require('internal/errors').codes;

const NOT_HTTP_TOKEN_CODE_POINT = /[^!#$%&'*+\-.^_`|~A-Za-z0-9]/g;
const NOT_HTTP_QUOTED_STRING_CODE_POINT = /[^\t\u0020-~\u0080-\u00FF]/g;

const END_BEGINNING_WHITESPACE = /[^\r\n\t ]|$/;
const START_ENDING_WHITESPACE = /[\r\n\t ]*$/;

function toASCIILower(str) {
  let result = '';
  for (let i = 0; i < str.length; i++) {
    const char = str[i];

    result += char >= 'A' && char <= 'Z' ?
      StringPrototypeToLowerCase(char) :
      char;
  }
  return result;
}

const SOLIDUS = '/';
const SEMICOLON = ';';

function parseTypeAndSubtype(str) {
  // Skip only HTTP whitespace from start
  let position = SafeStringPrototypeSearch(str, END_BEGINNING_WHITESPACE);
  // read until '/'
  const typeEnd = StringPrototypeIndexOf(str, SOLIDUS, position);
  const trimmedType = typeEnd === -1 ?
    StringPrototypeSlice(str, position) :
    StringPrototypeSlice(str, position, typeEnd);
  const invalidTypeIndex = SafeStringPrototypeSearch(trimmedType,
                                                     NOT_HTTP_TOKEN_CODE_POINT);
  if (trimmedType === '' || invalidTypeIndex !== -1 || typeEnd === -1) {
    throw new ERR_INVALID_MIME_SYNTAX('type', str, invalidTypeIndex);
  }
  // skip type and '/'
  position = typeEnd + 1;
  const type = toASCIILower(trimmedType);
  // read until ';'
  const subtypeEnd = StringPrototypeIndexOf(str, SEMICOLON, position);
  const rawSubtype = subtypeEnd === -1 ?
    StringPrototypeSlice(str, position) :
    StringPrototypeSlice(str, position, subtypeEnd);
  position += rawSubtype.length;
  if (subtypeEnd !== -1) {
    // skip ';'
    position += 1;
  }
  const trimmedSubtype = StringPrototypeSlice(
    rawSubtype,
    0,
    SafeStringPrototypeSearch(rawSubtype, START_ENDING_WHITESPACE));
  const invalidSubtypeIndex = SafeStringPrototypeSearch(trimmedSubtype,
                                                        NOT_HTTP_TOKEN_CODE_POINT);
  if (trimmedSubtype === '' || invalidSubtypeIndex !== -1) {
    throw new ERR_INVALID_MIME_SYNTAX('subtype', str, invalidSubtypeIndex);
  }
  const subtype = toASCIILower(trimmedSubtype);
  return [
    type,
    subtype,
    position,
  ];
}

const EQUALS_SEMICOLON_OR_END = /[;=]|$/;
const QUOTED_VALUE_PATTERN = /^(?:([\\]$)|[\\][\s\S]|[^"])*(?:(")|$)/u;

function removeBackslashes(str) {
  let ret = '';
  // We stop at str.length - 1 because we want to look ahead one character.
  let i;
  for (i = 0; i < str.length - 1; i++) {
    const c = str[i];
    if (c === '\\') {
      i++;
      ret += str[i];
    } else {
      ret += c;
    }
  }
  // We add the last character if we didn't skip to it.
  if (i === str.length - 1) {
    ret += str[i];
  }
  return ret;
}


function escapeQuoteOrSolidus(str) {
  let result = '';
  for (let i = 0; i < str.length; i++) {
    const char = str[i];
    result += (char === '"' || char === '\\') ? `\\${char}` : char;
  }
  return result;
}

const encode = (value) => {
  if (value.length === 0) return '""';
  const encode = SafeStringPrototypeSearch(value, NOT_HTTP_TOKEN_CODE_POINT) !== -1;
  if (!encode) return value;
  const escaped = escapeQuoteOrSolidus(value);
  return `"${escaped}"`;
};

class MIMEParams {
  #data = new SafeMap();
  // We set the flag the MIMEParams instance as processed on initialization
  // to defer the parsing of a potentially large string.
  #processed = true;
  #string = null;

  /**
   * Used to instantiate a MIMEParams object within the MIMEType class and
   * to allow it to be parsed lazily.
   */
  static instantiateMimeParams(str) {
    const instance = new MIMEParams();
    instance.#string = str;
    instance.#processed = false;
    return instance;
  }

  delete(name) {
    this.#parse();
    this.#data.delete(name);
  }

  get(name) {
    this.#parse();
    const data = this.#data;
    if (data.has(name)) {
      return data.get(name);
    }
    return null;
  }

  has(name) {
    this.#parse();
    return this.#data.has(name);
  }

  set(name, value) {
    this.#parse();
    const data = this.#data;
    name = `${name}`;
    value = `${value}`;
    const invalidNameIndex = SafeStringPrototypeSearch(name, NOT_HTTP_TOKEN_CODE_POINT);
    if (name.length === 0 || invalidNameIndex !== -1) {
      throw new ERR_INVALID_MIME_SYNTAX(
        'parameter name',
        name,
        invalidNameIndex,
      );
    }
    const invalidValueIndex = SafeStringPrototypeSearch(
      value,
      NOT_HTTP_QUOTED_STRING_CODE_POINT);
    if (invalidValueIndex !== -1) {
      throw new ERR_INVALID_MIME_SYNTAX(
        'parameter value',
        value,
        invalidValueIndex,
      );
    }
    data.set(name, value);
  }

  *entries() {
    this.#parse();
    yield* this.#data.entries();
  }

  *keys() {
    this.#parse();
    yield* this.#data.keys();
  }

  *values() {
    this.#parse();
    yield* this.#data.values();
  }

  toString() {
    this.#parse();
    let ret = '';
    for (const { 0: key, 1: value } of this.#data) {
      const encoded = encode(value);
      // Ensure they are separated
      if (ret.length) ret += ';';
      ret += `${key}=${encoded}`;
    }
    return ret;
  }

  // Used to act as a friendly class to stringifying stuff
  // not meant to be exposed to users, could inject invalid values
  #parse() {
    if (this.#processed) return;  // already parsed
    const paramsMap = this.#data;
    let position = 0;
    const str = this.#string;
    const endOfSource = SafeStringPrototypeSearch(
      StringPrototypeSlice(str, position),
      START_ENDING_WHITESPACE,
    ) + position;
    while (position < endOfSource) {
      // Skip any whitespace before parameter
      position += SafeStringPrototypeSearch(
        StringPrototypeSlice(str, position),
        END_BEGINNING_WHITESPACE,
      );
      // Read until ';' or '='
      const afterParameterName = SafeStringPrototypeSearch(
        StringPrototypeSlice(str, position),
        EQUALS_SEMICOLON_OR_END,
      ) + position;
      const parameterString = toASCIILower(
        StringPrototypeSlice(str, position, afterParameterName),
      );
      position = afterParameterName;
      // If we found a terminating character
      if (position < endOfSource) {
        // Safe to use because we never do special actions for surrogate pairs
        const char = StringPrototypeCharAt(str, position);
        // Skip the terminating character
        position += 1;
        // Ignore parameters without values
        if (char === ';') {
          continue;
        }
      }
      // If we are at end of the string, it cannot have a value
      if (position >= endOfSource) break;
      // Safe to use because we never do special actions for surrogate pairs
      const char = StringPrototypeCharAt(str, position);
      let parameterValue = null;
      if (char === '"') {
        // Handle quoted-string form of values
        // skip '"'
        position += 1;
        // Find matching closing '"' or end of string
        //   use $1 to see if we terminated on unmatched '\'
        //   use $2 to see if we terminated on a matching '"'
        //   so we can skip the last char in either case
        const insideMatch = RegExpPrototypeExec(
          QUOTED_VALUE_PATTERN,
          StringPrototypeSlice(str, position));
        position += insideMatch[0].length;
        // Skip including last character if an unmatched '\' or '"' during
        // unescape
        const inside = insideMatch[1] || insideMatch[2] ?
          StringPrototypeSlice(insideMatch[0], 0, -1) :
          insideMatch[0];
        // Unescape '\' quoted characters
        parameterValue = removeBackslashes(inside);
        // If we did have an unmatched '\' add it back to the end
        if (insideMatch[1]) parameterValue += '\\';
      } else {
        // Handle the normal parameter value form
        const valueEnd = StringPrototypeIndexOf(str, SEMICOLON, position);
        const rawValue = valueEnd === -1 ?
          StringPrototypeSlice(str, position) :
          StringPrototypeSlice(str, position, valueEnd);
        position += rawValue.length;
        const trimmedValue = StringPrototypeSlice(
          rawValue,
          0,
          SafeStringPrototypeSearch(rawValue, START_ENDING_WHITESPACE),
        );
        // Ignore parameters without values
        if (trimmedValue === '') continue;
        parameterValue = trimmedValue;
      }
      if (
        parameterString !== '' &&
        SafeStringPrototypeSearch(parameterString,
                                  NOT_HTTP_TOKEN_CODE_POINT) === -1 &&
        SafeStringPrototypeSearch(parameterValue,
                                  NOT_HTTP_QUOTED_STRING_CODE_POINT) === -1 &&
        paramsMap.has(parameterString) === false
      ) {
        paramsMap.set(parameterString, parameterValue);
      }
      position++;
    }
    this.#data = paramsMap;
    this.#processed = true;
  }
}
const MIMEParamsStringify = MIMEParams.prototype.toString;
ObjectDefineProperty(MIMEParams.prototype, SymbolIterator, {
  __proto__: null,
  configurable: true,
  value: MIMEParams.prototype.entries,
  writable: true,
});
ObjectDefineProperty(MIMEParams.prototype, 'toJSON', {
  __proto__: null,
  configurable: true,
  value: MIMEParamsStringify,
  writable: true,
});

const { instantiateMimeParams } = MIMEParams;
delete MIMEParams.instantiateMimeParams;

class MIMEType {
  #type;
  #subtype;
  #parameters;
  constructor(string) {
    string = `${string}`;
    const data = parseTypeAndSubtype(string);
    this.#type = data[0];
    this.#subtype = data[1];
    this.#parameters = instantiateMimeParams(StringPrototypeSlice(string, data[2]));
  }

  get type() {
    return this.#type;
  }

  set type(v) {
    v = `${v}`;
    const invalidTypeIndex = SafeStringPrototypeSearch(v, NOT_HTTP_TOKEN_CODE_POINT);
    if (v.length === 0 || invalidTypeIndex !== -1) {
      throw new ERR_INVALID_MIME_SYNTAX('type', v, invalidTypeIndex);
    }
    this.#type = toASCIILower(v);
  }

  get subtype() {
    return this.#subtype;
  }

  set subtype(v) {
    v = `${v}`;
    const invalidSubtypeIndex = SafeStringPrototypeSearch(v, NOT_HTTP_TOKEN_CODE_POINT);
    if (v.length === 0 || invalidSubtypeIndex !== -1) {
      throw new ERR_INVALID_MIME_SYNTAX('subtype', v, invalidSubtypeIndex);
    }
    this.#subtype = toASCIILower(v);
  }

  get essence() {
    return `${this.#type}/${this.#subtype}`;
  }

  get params() {
    return this.#parameters;
  }

  toString() {
    let ret = `${this.#type}/${this.#subtype}`;
    const paramStr = FunctionPrototypeCall(MIMEParamsStringify, this.#parameters);
    if (paramStr.length) ret += `;${paramStr}`;
    return ret;
  }
}
ObjectDefineProperty(MIMEType.prototype, 'toJSON', {
  __proto__: null,
  configurable: true,
  value: MIMEType.prototype.toString,
  writable: true,
});

module.exports = {
  MIMEParams,
  MIMEType,
};

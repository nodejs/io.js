'use strict'

const { types, inspect } = require('node:util')
const { markAsUncloneable } = require('node:worker_threads')
const { toUSVString } = require('../../core/util')

const UNDEFINED = 1
const BOOLEAN = 2
const STRING = 3
const SYMBOL = 4
const NUMBER = 5
const BIGINT = 6
const NULL = 7
const OBJECT = 8 // function and object

const FunctionPrototypeSymbolHasInstance = Function.call.bind(Function.prototype[Symbol.hasInstance])

/** @type {import('../../../types/webidl').Webidl} */
const webidl = {
  converters: {},
  util: {},
  errors: {},
  is: {}
}

webidl.errors.exception = function (message) {
  return new TypeError(`${message.header}: ${message.message}`)
}

webidl.errors.conversionFailed = function (context) {
  const plural = context.types.length === 1 ? '' : ' one of'
  const message =
    `${context.argument} could not be converted to` +
    `${plural}: ${context.types.join(', ')}.`

  return webidl.errors.exception({
    header: context.prefix,
    message
  })
}

webidl.errors.invalidArgument = function (context) {
  return webidl.errors.exception({
    header: context.prefix,
    message: `"${context.value}" is an invalid ${context.type}.`
  })
}

// https://webidl.spec.whatwg.org/#implements
webidl.brandCheck = function (V, I) {
  if (!FunctionPrototypeSymbolHasInstance(I, V)) {
    const err = new TypeError('Illegal invocation')
    err.code = 'ERR_INVALID_THIS' // node compat.
    throw err
  }
}

webidl.brandCheckMultiple = function (List) {
  const prototypes = List.map((c) => webidl.util.MakeTypeAssertion(c))

  return (V) => {
    if (prototypes.every(typeCheck => !typeCheck(V))) {
      const err = new TypeError('Illegal invocation')
      err.code = 'ERR_INVALID_THIS' // node compat.
      throw err
    }
  }
}

webidl.argumentLengthCheck = function ({ length }, min, ctx) {
  if (length < min) {
    throw webidl.errors.exception({
      message: `${min} argument${min !== 1 ? 's' : ''} required, ` +
               `but${length ? ' only' : ''} ${length} found.`,
      header: ctx
    })
  }
}

webidl.illegalConstructor = function () {
  throw webidl.errors.exception({
    header: 'TypeError',
    message: 'Illegal constructor'
  })
}

webidl.util.MakeTypeAssertion = function (I) {
  return (O) => FunctionPrototypeSymbolHasInstance(I, O)
}

// https://tc39.es/ecma262/#sec-ecmascript-data-types-and-values
webidl.util.Type = function (V) {
  switch (typeof V) {
    case 'undefined': return UNDEFINED
    case 'boolean': return BOOLEAN
    case 'string': return STRING
    case 'symbol': return SYMBOL
    case 'number': return NUMBER
    case 'bigint': return BIGINT
    case 'function':
    case 'object': {
      if (V === null) {
        return NULL
      }

      return OBJECT
    }
  }
}

webidl.util.Types = {
  UNDEFINED,
  BOOLEAN,
  STRING,
  SYMBOL,
  NUMBER,
  BIGINT,
  NULL,
  OBJECT
}

webidl.util.TypeValueToString = function (o) {
  switch (webidl.util.Type(o)) {
    case UNDEFINED: return 'Undefined'
    case BOOLEAN: return 'Boolean'
    case STRING: return 'String'
    case SYMBOL: return 'Symbol'
    case NUMBER: return 'Number'
    case BIGINT: return 'BigInt'
    case NULL: return 'Null'
    case OBJECT: return 'Object'
  }
}

webidl.util.markAsUncloneable = markAsUncloneable || (() => {})

// https://webidl.spec.whatwg.org/#abstract-opdef-converttoint
webidl.util.ConvertToInt = function (V, bitLength, signedness, opts) {
  let upperBound
  let lowerBound

  // 1. If bitLength is 64, then:
  if (bitLength === 64) {
    // 1. Let upperBound be 2^53 − 1.
    upperBound = Math.pow(2, 53) - 1

    // 2. If signedness is "unsigned", then let lowerBound be 0.
    if (signedness === 'unsigned') {
      lowerBound = 0
    } else {
      // 3. Otherwise let lowerBound be −2^53 + 1.
      lowerBound = Math.pow(-2, 53) + 1
    }
  } else if (signedness === 'unsigned') {
    // 2. Otherwise, if signedness is "unsigned", then:

    // 1. Let lowerBound be 0.
    lowerBound = 0

    // 2. Let upperBound be 2^bitLength − 1.
    upperBound = Math.pow(2, bitLength) - 1
  } else {
    // 3. Otherwise:

    // 1. Let lowerBound be -2^bitLength − 1.
    lowerBound = Math.pow(-2, bitLength) - 1

    // 2. Let upperBound be 2^bitLength − 1 − 1.
    upperBound = Math.pow(2, bitLength - 1) - 1
  }

  // 4. Let x be ? ToNumber(V).
  let x = Number(V)

  // 5. If x is −0, then set x to +0.
  if (x === 0) {
    x = 0
  }

  // 6. If the conversion is to an IDL type associated
  //    with the [EnforceRange] extended attribute, then:
  if (opts?.enforceRange === true) {
    // 1. If x is NaN, +∞, or −∞, then throw a TypeError.
    if (
      Number.isNaN(x) ||
      x === Number.POSITIVE_INFINITY ||
      x === Number.NEGATIVE_INFINITY
    ) {
      throw webidl.errors.exception({
        header: 'Integer conversion',
        message: `Could not convert ${webidl.util.Stringify(V)} to an integer.`
      })
    }

    // 2. Set x to IntegerPart(x).
    x = webidl.util.IntegerPart(x)

    // 3. If x < lowerBound or x > upperBound, then
    //    throw a TypeError.
    if (x < lowerBound || x > upperBound) {
      throw webidl.errors.exception({
        header: 'Integer conversion',
        message: `Value must be between ${lowerBound}-${upperBound}, got ${x}.`
      })
    }

    // 4. Return x.
    return x
  }

  // 7. If x is not NaN and the conversion is to an IDL
  //    type associated with the [Clamp] extended
  //    attribute, then:
  if (!Number.isNaN(x) && opts?.clamp === true) {
    // 1. Set x to min(max(x, lowerBound), upperBound).
    x = Math.min(Math.max(x, lowerBound), upperBound)

    // 2. Round x to the nearest integer, choosing the
    //    even integer if it lies halfway between two,
    //    and choosing +0 rather than −0.
    if (Math.floor(x) % 2 === 0) {
      x = Math.floor(x)
    } else {
      x = Math.ceil(x)
    }

    // 3. Return x.
    return x
  }

  // 8. If x is NaN, +0, +∞, or −∞, then return +0.
  if (
    Number.isNaN(x) ||
    (x === 0 && Object.is(0, x)) ||
    x === Number.POSITIVE_INFINITY ||
    x === Number.NEGATIVE_INFINITY
  ) {
    return 0
  }

  // 9. Set x to IntegerPart(x).
  x = webidl.util.IntegerPart(x)

  // 10. Set x to x modulo 2^bitLength.
  x = x % Math.pow(2, bitLength)

  // 11. If signedness is "signed" and x ≥ 2^bitLength − 1,
  //    then return x − 2^bitLength.
  if (signedness === 'signed' && x >= Math.pow(2, bitLength) - 1) {
    return x - Math.pow(2, bitLength)
  }

  // 12. Otherwise, return x.
  return x
}

// https://webidl.spec.whatwg.org/#abstract-opdef-integerpart
webidl.util.IntegerPart = function (n) {
  // 1. Let r be floor(abs(n)).
  const r = Math.floor(Math.abs(n))

  // 2. If n < 0, then return -1 × r.
  if (n < 0) {
    return -1 * r
  }

  // 3. Otherwise, return r.
  return r
}

webidl.util.Stringify = function (V) {
  const type = webidl.util.Type(V)

  switch (type) {
    case SYMBOL:
      return `Symbol(${V.description})`
    case OBJECT:
      return inspect(V)
    case STRING:
      return `"${V}"`
    default:
      return `${V}`
  }
}

// https://webidl.spec.whatwg.org/#es-sequence
webidl.sequenceConverter = function (converter) {
  return (V, prefix, argument, Iterable) => {
    // 1. If Type(V) is not Object, throw a TypeError.
    if (webidl.util.Type(V) !== OBJECT) {
      throw webidl.errors.exception({
        header: prefix,
        message: `${argument} (${webidl.util.Stringify(V)}) is not iterable.`
      })
    }

    // 2. Let method be ? GetMethod(V, @@iterator).
    /** @type {Generator} */
    const method = typeof Iterable === 'function' ? Iterable() : V?.[Symbol.iterator]?.()
    const seq = []
    let index = 0

    // 3. If method is undefined, throw a TypeError.
    if (
      method === undefined ||
      typeof method.next !== 'function'
    ) {
      throw webidl.errors.exception({
        header: prefix,
        message: `${argument} is not iterable.`
      })
    }

    // https://webidl.spec.whatwg.org/#create-sequence-from-iterable
    while (true) {
      const { done, value } = method.next()

      if (done) {
        break
      }

      seq.push(converter(value, prefix, `${argument}[${index++}]`))
    }

    return seq
  }
}

// https://webidl.spec.whatwg.org/#es-to-record
webidl.recordConverter = function (keyConverter, valueConverter) {
  return (O, prefix, argument) => {
    // 1. If Type(O) is not Object, throw a TypeError.
    if (webidl.util.Type(O) !== OBJECT) {
      throw webidl.errors.exception({
        header: prefix,
        message: `${argument} ("${webidl.util.TypeValueToString(O)}") is not an Object.`
      })
    }

    // 2. Let result be a new empty instance of record<K, V>.
    const result = {}

    if (!types.isProxy(O)) {
      // 1. Let desc be ? O.[[GetOwnProperty]](key).
      const keys = [...Object.getOwnPropertyNames(O), ...Object.getOwnPropertySymbols(O)]

      for (const key of keys) {
        const keyName = webidl.util.Stringify(key)

        // 1. Let typedKey be key converted to an IDL value of type K.
        const typedKey = keyConverter(key, prefix, `Key ${keyName} in ${argument}`)

        // 2. Let value be ? Get(O, key).
        // 3. Let typedValue be value converted to an IDL value of type V.
        const typedValue = valueConverter(O[key], prefix, `${argument}[${keyName}]`)

        // 4. Set result[typedKey] to typedValue.
        result[typedKey] = typedValue
      }

      // 5. Return result.
      return result
    }

    // 3. Let keys be ? O.[[OwnPropertyKeys]]().
    const keys = Reflect.ownKeys(O)

    // 4. For each key of keys.
    for (const key of keys) {
      // 1. Let desc be ? O.[[GetOwnProperty]](key).
      const desc = Reflect.getOwnPropertyDescriptor(O, key)

      // 2. If desc is not undefined and desc.[[Enumerable]] is true:
      if (desc?.enumerable) {
        // 1. Let typedKey be key converted to an IDL value of type K.
        const typedKey = keyConverter(key, prefix, argument)

        // 2. Let value be ? Get(O, key).
        // 3. Let typedValue be value converted to an IDL value of type V.
        const typedValue = valueConverter(O[key], prefix, argument)

        // 4. Set result[typedKey] to typedValue.
        result[typedKey] = typedValue
      }
    }

    // 5. Return result.
    return result
  }
}

webidl.interfaceConverter = function (TypeCheck, name) {
  return (V, prefix, argument) => {
    if (!TypeCheck(V)) {
      throw webidl.errors.exception({
        header: prefix,
        message: `Expected ${argument} ("${webidl.util.Stringify(V)}") to be an instance of ${name}.`
      })
    }

    return V
  }
}

webidl.dictionaryConverter = function (converters) {
  return (dictionary, prefix, argument) => {
    const dict = {}

    if (dictionary != null && webidl.util.Type(dictionary) !== OBJECT) {
      throw webidl.errors.exception({
        header: prefix,
        message: `Expected ${dictionary} to be one of: Null, Undefined, Object.`
      })
    }

    for (const options of converters) {
      const { key, defaultValue, required, converter } = options

      if (required === true) {
        if (dictionary == null || !Object.hasOwn(dictionary, key)) {
          throw webidl.errors.exception({
            header: prefix,
            message: `Missing required key "${key}".`
          })
        }
      }

      let value = dictionary?.[key]
      const hasDefault = defaultValue !== undefined

      // Only use defaultValue if value is undefined and
      // a defaultValue options was provided.
      if (hasDefault && value === undefined) {
        value = defaultValue()
      }

      // A key can be optional and have no default value.
      // When this happens, do not perform a conversion,
      // and do not assign the key a value.
      if (required || hasDefault || value !== undefined) {
        value = converter(value, prefix, `${argument}.${key}`)

        if (
          options.allowedValues &&
          !options.allowedValues.includes(value)
        ) {
          throw webidl.errors.exception({
            header: prefix,
            message: `${value} is not an accepted type. Expected one of ${options.allowedValues.join(', ')}.`
          })
        }

        dict[key] = value
      }
    }

    return dict
  }
}

webidl.nullableConverter = function (converter) {
  return (V, prefix, argument) => {
    if (V === null) {
      return V
    }

    return converter(V, prefix, argument)
  }
}

webidl.is.ReadableStream = webidl.util.MakeTypeAssertion(ReadableStream)
webidl.is.Blob = webidl.util.MakeTypeAssertion(Blob)
webidl.is.URLSearchParams = webidl.util.MakeTypeAssertion(URLSearchParams)
webidl.is.File = webidl.util.MakeTypeAssertion(globalThis.File ?? require('node:buffer').File)
webidl.is.URL = webidl.util.MakeTypeAssertion(URL)
webidl.is.AbortSignal = webidl.util.MakeTypeAssertion(AbortSignal)
webidl.is.MessagePort = webidl.util.MakeTypeAssertion(MessagePort)

// https://webidl.spec.whatwg.org/#es-DOMString
webidl.converters.DOMString = function (V, prefix, argument, opts) {
  // 1. If V is null and the conversion is to an IDL type
  //    associated with the [LegacyNullToEmptyString]
  //    extended attribute, then return the DOMString value
  //    that represents the empty string.
  if (V === null && opts?.legacyNullToEmptyString) {
    return ''
  }

  // 2. Let x be ? ToString(V).
  if (typeof V === 'symbol') {
    throw webidl.errors.exception({
      header: prefix,
      message: `${argument} is a symbol, which cannot be converted to a DOMString.`
    })
  }

  // 3. Return the IDL DOMString value that represents the
  //    same sequence of code units as the one the
  //    ECMAScript String value x represents.
  return String(V)
}

// https://webidl.spec.whatwg.org/#es-ByteString
webidl.converters.ByteString = function (V, prefix, argument) {
  // 1. Let x be ? ToString(V).
  if (typeof V === 'symbol') {
    throw webidl.errors.exception({
      header: prefix,
      message: `${argument} is a symbol, which cannot be converted to a ByteString.`
    })
  }

  const x = String(V)

  // 2. If the value of any element of x is greater than
  //    255, then throw a TypeError.
  for (let index = 0; index < x.length; index++) {
    if (x.charCodeAt(index) > 255) {
      throw new TypeError(
        'Cannot convert argument to a ByteString because the character at ' +
        `index ${index} has a value of ${x.charCodeAt(index)} which is greater than 255.`
      )
    }
  }

  // 3. Return an IDL ByteString value whose length is the
  //    length of x, and where the value of each element is
  //    the value of the corresponding element of x.
  return x
}

// https://webidl.spec.whatwg.org/#es-USVString
// TODO: rewrite this so we can control the errors thrown
webidl.converters.USVString = toUSVString

// https://webidl.spec.whatwg.org/#es-boolean
webidl.converters.boolean = function (V) {
  // 1. Let x be the result of computing ToBoolean(V).
  const x = Boolean(V)

  // 2. Return the IDL boolean value that is the one that represents
  //    the same truth value as the ECMAScript Boolean value x.
  return x
}

// https://webidl.spec.whatwg.org/#es-any
webidl.converters.any = function (V) {
  return V
}

// https://webidl.spec.whatwg.org/#es-long-long
webidl.converters['long long'] = function (V, prefix, argument) {
  // 1. Let x be ? ConvertToInt(V, 64, "signed").
  const x = webidl.util.ConvertToInt(V, 64, 'signed', undefined, prefix, argument)

  // 2. Return the IDL long long value that represents
  //    the same numeric value as x.
  return x
}

// https://webidl.spec.whatwg.org/#es-unsigned-long-long
webidl.converters['unsigned long long'] = function (V, prefix, argument) {
  // 1. Let x be ? ConvertToInt(V, 64, "unsigned").
  const x = webidl.util.ConvertToInt(V, 64, 'unsigned', undefined, prefix, argument)

  // 2. Return the IDL unsigned long long value that
  //    represents the same numeric value as x.
  return x
}

// https://webidl.spec.whatwg.org/#es-unsigned-long
webidl.converters['unsigned long'] = function (V, prefix, argument) {
  // 1. Let x be ? ConvertToInt(V, 32, "unsigned").
  const x = webidl.util.ConvertToInt(V, 32, 'unsigned', undefined, prefix, argument)

  // 2. Return the IDL unsigned long value that
  //    represents the same numeric value as x.
  return x
}

// https://webidl.spec.whatwg.org/#es-unsigned-short
webidl.converters['unsigned short'] = function (V, prefix, argument, opts) {
  // 1. Let x be ? ConvertToInt(V, 16, "unsigned").
  const x = webidl.util.ConvertToInt(V, 16, 'unsigned', opts, prefix, argument)

  // 2. Return the IDL unsigned short value that represents
  //    the same numeric value as x.
  return x
}

// https://webidl.spec.whatwg.org/#idl-ArrayBuffer
webidl.converters.ArrayBuffer = function (V, prefix, argument, opts) {
  // 1. If Type(V) is not Object, or V does not have an
  //    [[ArrayBufferData]] internal slot, then throw a
  //    TypeError.
  // see: https://tc39.es/ecma262/#sec-properties-of-the-arraybuffer-instances
  // see: https://tc39.es/ecma262/#sec-properties-of-the-sharedarraybuffer-instances
  if (
    webidl.util.Type(V) !== OBJECT ||
    !types.isAnyArrayBuffer(V)
  ) {
    throw webidl.errors.conversionFailed({
      prefix,
      argument: `${argument} ("${webidl.util.Stringify(V)}")`,
      types: ['ArrayBuffer']
    })
  }

  // 2. If the conversion is not to an IDL type associated
  //    with the [AllowShared] extended attribute, and
  //    IsSharedArrayBuffer(V) is true, then throw a
  //    TypeError.
  if (opts?.allowShared === false && types.isSharedArrayBuffer(V)) {
    throw webidl.errors.exception({
      header: 'ArrayBuffer',
      message: 'SharedArrayBuffer is not allowed.'
    })
  }

  // 3. If the conversion is not to an IDL type associated
  //    with the [AllowResizable] extended attribute, and
  //    IsResizableArrayBuffer(V) is true, then throw a
  //    TypeError.
  if (V.resizable || V.growable) {
    throw webidl.errors.exception({
      header: 'ArrayBuffer',
      message: 'Received a resizable ArrayBuffer.'
    })
  }

  // 4. Return the IDL ArrayBuffer value that is a
  //    reference to the same object as V.
  return V
}

webidl.converters.TypedArray = function (V, T, prefix, name, opts) {
  // 1. Let T be the IDL type V is being converted to.

  // 2. If Type(V) is not Object, or V does not have a
  //    [[TypedArrayName]] internal slot with a value
  //    equal to T’s name, then throw a TypeError.
  if (
    webidl.util.Type(V) !== OBJECT ||
    !types.isTypedArray(V) ||
    V.constructor.name !== T.name
  ) {
    throw webidl.errors.conversionFailed({
      prefix,
      argument: `${name} ("${webidl.util.Stringify(V)}")`,
      types: [T.name]
    })
  }

  // 3. If the conversion is not to an IDL type associated
  //    with the [AllowShared] extended attribute, and
  //    IsSharedArrayBuffer(V.[[ViewedArrayBuffer]]) is
  //    true, then throw a TypeError.
  if (opts?.allowShared === false && types.isSharedArrayBuffer(V.buffer)) {
    throw webidl.errors.exception({
      header: 'ArrayBuffer',
      message: 'SharedArrayBuffer is not allowed.'
    })
  }

  // 4. If the conversion is not to an IDL type associated
  //    with the [AllowResizable] extended attribute, and
  //    IsResizableArrayBuffer(V.[[ViewedArrayBuffer]]) is
  //    true, then throw a TypeError.
  if (V.buffer.resizable || V.buffer.growable) {
    throw webidl.errors.exception({
      header: 'ArrayBuffer',
      message: 'Received a resizable ArrayBuffer.'
    })
  }

  // 5. Return the IDL value of type T that is a reference
  //    to the same object as V.
  return V
}

webidl.converters.DataView = function (V, prefix, name, opts) {
  // 1. If Type(V) is not Object, or V does not have a
  //    [[DataView]] internal slot, then throw a TypeError.
  if (webidl.util.Type(V) !== OBJECT || !types.isDataView(V)) {
    throw webidl.errors.exception({
      header: prefix,
      message: `${name} is not a DataView.`
    })
  }

  // 2. If the conversion is not to an IDL type associated
  //    with the [AllowShared] extended attribute, and
  //    IsSharedArrayBuffer(V.[[ViewedArrayBuffer]]) is true,
  //    then throw a TypeError.
  if (opts?.allowShared === false && types.isSharedArrayBuffer(V.buffer)) {
    throw webidl.errors.exception({
      header: 'ArrayBuffer',
      message: 'SharedArrayBuffer is not allowed.'
    })
  }

  // 3. If the conversion is not to an IDL type associated
  //    with the [AllowResizable] extended attribute, and
  //    IsResizableArrayBuffer(V.[[ViewedArrayBuffer]]) is
  //    true, then throw a TypeError.
  if (V.buffer.resizable || V.buffer.growable) {
    throw webidl.errors.exception({
      header: 'ArrayBuffer',
      message: 'Received a resizable ArrayBuffer.'
    })
  }

  // 4. Return the IDL DataView value that is a reference
  //    to the same object as V.
  return V
}

webidl.converters['sequence<ByteString>'] = webidl.sequenceConverter(
  webidl.converters.ByteString
)

webidl.converters['sequence<sequence<ByteString>>'] = webidl.sequenceConverter(
  webidl.converters['sequence<ByteString>']
)

webidl.converters['record<ByteString, ByteString>'] = webidl.recordConverter(
  webidl.converters.ByteString,
  webidl.converters.ByteString
)

webidl.converters.Blob = webidl.interfaceConverter(webidl.is.Blob, 'Blob')

webidl.converters.AbortSignal = webidl.interfaceConverter(
  webidl.is.AbortSignal,
  'AbortSignal'
)

module.exports = {
  webidl
}

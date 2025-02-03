'use strict';

const {
  ArrayPrototypePush,
  ArrayPrototypeToSorted,
  MathAbs,
  MathMax,
  MathMin,
  MathPow,
  MathSign,
  MathTrunc,
  NumberIsNaN,
  NumberMAX_SAFE_INTEGER,
  NumberMIN_SAFE_INTEGER,
  ObjectAssign,
  ObjectPrototypeIsPrototypeOf,
  SafeSet,
  String,
  Symbol,
  SymbolIterator,
  TypeError,
} = primordials;

const {
  codes: {
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
  },
} = require('internal/errors');
const { kEmptyObject } = require('internal/util');

const converters = { __proto__: null };

const UNDEFINED = Symbol('undefined');
const BOOLEAN = Symbol('boolean');
const STRING = Symbol('string');
const SYMBOL = Symbol('symbol');
const NUMBER = Symbol('number');
const BIGINT = Symbol('bigint');
const NULL = Symbol('null');
const OBJECT = Symbol('object');

/**
 * @see https://webidl.spec.whatwg.org/#es-any
 * @param {any} V
 * @returns {any}
 */
converters.any = (V) => {
  return V;
};

converters.object = (V, opts = kEmptyObject) => {
  if (type(V) !== OBJECT) {
    throw makeException(
      'is not an object',
      kEmptyObject,
    );
  }
  return V;
};

// https://webidl.spec.whatwg.org/#abstract-opdef-integerpart
const integerPart = MathTrunc;

/* eslint-disable node-core/non-ascii-character */
// Round x to the nearest integer, choosing the even integer if it lies halfway
// between two, and choosing +0 rather than -0.
// This is different from Math.round, which rounds to the next integer in the
// direction of +∞ when the fraction portion is exactly 0.5.
/* eslint-enable node-core/non-ascii-character */
function evenRound(x) {
  // Convert -0 to +0.
  const i = integerPart(x) + 0;
  const reminder = MathAbs(x % 1);
  const sign = MathSign(i);
  if (reminder === 0.5) {
    return i % 2 === 0 ? i : i + sign;
  }
  const r = reminder < 0.5 ? i : i + sign;
  // Convert -0 to +0.
  if (r === 0) {
    return 0;
  }
  return r;
}

function pow2(exponent) {
  // << operates on 32 bit signed integers.
  if (exponent < 31) {
    return 1 << exponent;
  }
  if (exponent === 31) {
    return 0x8000_0000;
  }
  if (exponent === 32) {
    return 0x1_0000_0000;
  }
  return MathPow(2, exponent);
}

// https://tc39.es/ecma262/#eqn-modulo
// The notation “x modulo y” computes a value k of the same sign as y.
function modulo(x, y) {
  const r = x % y;
  // Convert -0 to +0.
  if (r === 0) {
    return 0;
  }
  return r;
}

// https://webidl.spec.whatwg.org/#abstract-opdef-converttoint
function convertToInt(name, value, bitLength, options = kEmptyObject) {
  const { signed = false, enforceRange = false, clamp = false } = options;

  let upperBound;
  let lowerBound;
  // 1. If bitLength is 64, then:
  if (bitLength === 64) {
    // 1.1. Let upperBound be 2^53 − 1.
    upperBound = NumberMAX_SAFE_INTEGER;
    // 1.2. If signedness is "unsigned", then let lowerBound be 0.
    // 1.3. Otherwise let lowerBound be −2^53 + 1.
    lowerBound = !signed ? 0 : NumberMIN_SAFE_INTEGER;
  } else if (!signed) {
    // 2. Otherwise, if signedness is "unsigned", then:
    // 2.1. Let lowerBound be 0.
    // 2.2. Let upperBound be 2^bitLength − 1.
    lowerBound = 0;
    upperBound = pow2(bitLength) - 1;
  } else {
    // 3. Otherwise:
    // 3.1. Let lowerBound be -2^(bitLength − 1).
    // 3.2. Let upperBound be 2^(bitLength − 1) − 1.
    lowerBound = -pow2(bitLength - 1);
    upperBound = pow2(bitLength - 1) - 1;
  }

  // 4. Let x be ? ToNumber(V).
  let x = +value;
  // 5. If x is −0, then set x to +0.
  if (x === 0) {
    x = 0;
  }

  // 6. If the conversion is to an IDL type associated with the [EnforceRange]
  // extended attribute, then:
  if (enforceRange) {
    // 6.1. If x is NaN, +∞, or −∞, then throw a TypeError.
    if (NumberIsNaN(x) || x === Infinity || x === -Infinity) {
      throw new ERR_INVALID_ARG_VALUE(name, x);
    }
    // 6.2. Set x to IntegerPart(x).
    x = integerPart(x);

    // 6.3. If x < lowerBound or x > upperBound, then throw a TypeError.
    if (x < lowerBound || x > upperBound) {
      throw new ERR_INVALID_ARG_VALUE(name, x);
    }

    // 6.4. Return x.
    return x;
  }

  // 7. If x is not NaN and the conversion is to an IDL type associated with
  // the [Clamp] extended attribute, then:
  if (clamp && !NumberIsNaN(x)) {
    // 7.1. Set x to min(max(x, lowerBound), upperBound).
    x = MathMin(MathMax(x, lowerBound), upperBound);

    // 7.2. Round x to the nearest integer, choosing the even integer if it
    // lies halfway between two, and choosing +0 rather than −0.
    x = evenRound(x);

    // 7.3. Return x.
    return x;
  }

  // 8. If x is NaN, +0, +∞, or −∞, then return +0.
  if (NumberIsNaN(x) || x === 0 || x === Infinity || x === -Infinity) {
    return 0;
  }

  // 9. Set x to IntegerPart(x).
  x = integerPart(x);

  // 10. Set x to x modulo 2^bitLength.
  x = modulo(x, pow2(bitLength));

  // 11. If signedness is "signed" and x ≥ 2^(bitLength − 1), then return x −
  // 2^bitLength.
  if (signed && x >= pow2(bitLength - 1)) {
    return x - pow2(bitLength);
  }

  // 12. Otherwise, return x.
  return x;
}

/**
 * @see https://webidl.spec.whatwg.org/#es-DOMString
 * @param {any} V
 * @returns {string}
 */
converters.DOMString = function DOMString(V) {
  if (typeof V === 'symbol') {
    throw new ERR_INVALID_ARG_VALUE('value', V);
  }

  return String(V);
};

converters['sequence<object>'] = createSequenceConverter(converters.object);

function codedTypeError(message, errorProperties = kEmptyObject) {
  // eslint-disable-next-line no-restricted-syntax
  const err = new TypeError(message);
  ObjectAssign(err, errorProperties);
  return err;
}

function makeException(message, opts = kEmptyObject) {
  const prefix = opts.prefix ? opts.prefix + ': ' : '';
  const context = opts.context?.length === 0 ?
    '' : (opts.context ?? 'Value') + ' ';
  return codedTypeError(
    `${prefix}${context}${message}`,
    { code: opts.code || 'ERR_INVALID_ARG_TYPE' },
  );
}

function createEnumConverter(name, values) {
  const E = new SafeSet(values);

  return function(V, opts = kEmptyObject) {
    const S = String(V);

    if (!E.has(S)) {
      throw makeException(
        `value '${S}' is not a valid enum value of type ${name}.`,
        { __proto__: null, ...opts, code: 'ERR_INVALID_ARG_VALUE' });
    }

    return S;
  };
}

// https://tc39.es/ecma262/#sec-ecmascript-data-types-and-values
function type(V) {
  switch (typeof V) {
    case 'undefined':
      return UNDEFINED;
    case 'boolean':
      return BOOLEAN;
    case 'number':
      return NUMBER;
    case 'string':
      return STRING;
    case 'symbol':
      return SYMBOL;
    case 'bigint':
      return BIGINT;
    case 'object': // Fall through
    case 'function': // Fall through
    default:
      if (V === null) {
        return NULL;
      }
      // Per ES spec, typeof returns an implementation-defined value that is not
      // any of the existing ones for uncallable non-standard exotic objects.
      // Yet Type() which the Web IDL spec depends on returns Object for such
      // cases. So treat the default case as an object.
      return OBJECT;
  }
}

// https://webidl.spec.whatwg.org/#js-dictionary
function createDictionaryConverter(members) {
  // The spec requires us to operate the members of a dictionary in
  // lexicographical order. We are doing this in the outer scope to
  // reduce the overhead that could happen in the returned function.
  const sortedMembers = ArrayPrototypeToSorted(members, (a, b) => {
    if (a.key === b.key) {
      return 0;
    }
    return a.key < b.key ? -1 : 1;
  });

  return function(
    V,
    opts = kEmptyObject,
  ) {
    if (V != null && type(V) !== OBJECT) {
      throw makeException(
        'cannot be converted to a dictionary',
        opts,
      );
    }

    const idlDict = { __proto__: null };
    for (let i = 0; i < sortedMembers.length; i++) {
      const member = sortedMembers[i];
      const key = member.key;
      let jsMemberValue;
      if (V == null) {
        jsMemberValue = undefined;
      } else {
        jsMemberValue = V[key];
      }

      if (jsMemberValue !== undefined) {
        const memberContext = opts.context ? `${key} in ${opts.context}` : `${key}`;
        const converter = member.converter;
        const idlMemberValue = converter(
          jsMemberValue,
          {
            __proto__: null,
            prefix: opts.prefix,
            context: memberContext,
          },
        );
        idlDict[key] = idlMemberValue;
      } else if (typeof member.defaultValue === 'function') {
        const idlMemberValue = member.defaultValue();
        idlDict[key] = idlMemberValue;
      } else if (member.required) {
        throw makeException(
          `cannot be converted because of the missing '${key}'`,
          opts,
        );
      }
    }

    return idlDict;
  };
}

// https://webidl.spec.whatwg.org/#es-sequence
function createSequenceConverter(converter) {
  return function(V, opts = kEmptyObject) {
    if (type(V) !== OBJECT) {
      throw makeException(
        'can not be converted to sequence.',
        opts);
    }
    const iter = V?.[SymbolIterator]?.();
    if (iter === undefined) {
      throw makeException(
        'can not be converted to sequence.',
        opts);
    }
    const array = [];
    while (true) {
      const res = iter?.next?.();
      if (res === undefined) {
        throw makeException(
          'can not be converted to sequence.',
          opts);
      }
      if (res.done === true) break;
      const val = converter(res.value, {
        __proto__: null,
        ...opts,
        context: `${opts.context}[${array.length}]`,
      });
      ArrayPrototypePush(array, val);
    };
    return array;
  };
}

// https://webidl.spec.whatwg.org/#js-interface
function createInterfaceConverter(name, I) {
  return (V, opts = kEmptyObject) => {
    // 1. If V implements I, then return the IDL interface type value that
    //    represents a reference to that platform object.
    if (ObjectPrototypeIsPrototypeOf(I, V)) return V;
    // 2. Throw a TypeError.
    throw new ERR_INVALID_ARG_TYPE(
      typeof opts.context === 'string' ? opts.context : 'value', name, V,
    );
  };
}


module.exports = {
  type,
  converters,
  convertToInt,
  createEnumConverter,
  createInterfaceConverter,
  createSequenceConverter,
  createDictionaryConverter,
  evenRound,
  makeException,
};

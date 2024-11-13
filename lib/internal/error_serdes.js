'use strict';

const {
  ArrayPrototypeForEach,
  Error,
  EvalError,
  FunctionPrototypeCall,
  ObjectAssign,
  ObjectCreate,
  ObjectDefineProperty,
  ObjectGetOwnPropertyDescriptor,
  ObjectGetOwnPropertyNames,
  ObjectGetPrototypeOf,
  ObjectKeys,
  ObjectPrototypeToString,
  RangeError,
  ReferenceError,
  SafeSet,
  StringFromCharCode,
  StringPrototypeSubstring,
  SymbolFor,
  SymbolToStringTag,
  SyntaxError,
  TypeError,
  TypedArrayPrototypeGetBuffer,
  TypedArrayPrototypeGetByteLength,
  TypedArrayPrototypeGetByteOffset,
  URIError,
} = primordials;

const { Buffer } = require('buffer');
const { inspect: { custom: customInspectSymbol } } = require('util');

const kSerializedError = 0;
const kSerializedObject = 1;
const kInspectedError = 2;
const kInspectedSymbol = 3;
const kCustomInspectedObject = 4;

const kSymbolStringLength = 'Symbol('.length;

const errors = {
  Error, TypeError, RangeError, URIError, SyntaxError, ReferenceError, EvalError,
};
const errorConstructorNames = new SafeSet(ObjectKeys(errors));

function TryGetAllProperties(object, target = object) {
  const all = { __proto__: null };
  if (object === null)
    return all;
  ObjectAssign(all,
               TryGetAllProperties(ObjectGetPrototypeOf(object), target));
  const keys = ObjectGetOwnPropertyNames(object);
  ArrayPrototypeForEach(keys, (key) => {
    let descriptor;
    try {
      // TODO: create a null-prototype descriptor with needed properties only
      descriptor = ObjectGetOwnPropertyDescriptor(object, key);
    } catch { return; }
    const getter = descriptor.get;
    if (getter && key !== '__proto__') {
      try {
        descriptor.value = FunctionPrototypeCall(getter, target);
        delete descriptor.get;
        delete descriptor.set;
      } catch {
        // Continue regardless of error.
      }
    }
    if (key === 'cause') {
      descriptor.value = serializeError(descriptor.value);
      all[key] = descriptor;
    } else if ('value' in descriptor &&
            typeof descriptor.value !== 'function' && typeof descriptor.value !== 'symbol') {
      all[key] = descriptor;
    }
  });
  return all;
}

function GetConstructors(object) {
  const constructors = [];

  for (let current = object;
    current !== null;
    current = ObjectGetPrototypeOf(current)) {
    const desc = ObjectGetOwnPropertyDescriptor(current, 'constructor');
    if (desc?.value) {
      ObjectDefineProperty(constructors, constructors.length, {
        __proto__: null,
        value: desc.value, enumerable: true,
      });
    }
  }

  return constructors;
}

function GetName(object) {
  const desc = ObjectGetOwnPropertyDescriptor(object, 'name');
  return desc?.value;
}

let internalUtilInspect;
function inspect(...args) {
  internalUtilInspect ??= require('internal/util/inspect');
  return internalUtilInspect.inspect(...args);
}

let serialize;
function serializeError(error) {
  serialize ??= require('v8').serialize;
  if (typeof error === 'symbol') {
    return Buffer.from(StringFromCharCode(kInspectedSymbol) + inspect(error), 'utf8');
  }
  try {
    if (typeof error === 'object' &&
        ObjectPrototypeToString(error) === '[object Error]') {
      const constructors = GetConstructors(error);
      for (let i = 0; i < constructors.length; i++) {
        const name = GetName(constructors[i]);
        if (errorConstructorNames.has(name)) {
          const serialized = serialize({
            constructor: name,
            properties: TryGetAllProperties(error),
          });
          return Buffer.concat([Buffer.from([kSerializedError]), serialized]);
        }
      }
    }
  } catch {
    // Continue regardless of error.
  }
  try {
    if (error != null && customInspectSymbol in error) {
      return Buffer.from(StringFromCharCode(kCustomInspectedObject) + inspect(error), 'utf8');
    }
  } catch {
    // Continue regardless of error.
  }
  try {
    const serialized = serialize(error);
    return Buffer.concat([Buffer.from([kSerializedObject]), serialized]);
  } catch {
    // Continue regardless of error.
  }
  return Buffer.from(StringFromCharCode(kInspectedError) + inspect(error), 'utf8');
}

function fromBuffer(error) {
  return Buffer.from(TypedArrayPrototypeGetBuffer(error),
                     TypedArrayPrototypeGetByteOffset(error) + 1,
                     TypedArrayPrototypeGetByteLength(error) - 1);
}

let deserialize;
function deserializeError(error) {
  deserialize ??= require('v8').deserialize;
  switch (error[0]) {
    case kSerializedError: {
      const { constructor, properties } = deserialize(error.subarray(1));
      const ctor = errors[constructor];
      ObjectDefineProperty(properties, SymbolToStringTag, {
        __proto__: null,
        value: { __proto__: null, value: 'Error', configurable: true },
        enumerable: true,
      });
      if ('cause' in properties && 'value' in properties.cause) {
        properties.cause.value = deserializeError(properties.cause.value);
      }
      return ObjectCreate(ctor.prototype, properties);
    }
    case kSerializedObject:
      return deserialize(error.subarray(1));
    case kInspectedError:
      return fromBuffer(error).toString('utf8');
    case kInspectedSymbol: {
      const buf = fromBuffer(error);
      return SymbolFor(StringPrototypeSubstring(buf.toString('utf8'), kSymbolStringLength, buf.length - 1));
    }
    case kCustomInspectedObject:
      return {
        __proto__: null,
        [customInspectSymbol]: () => fromBuffer(error).toString('utf8'),
      };
  }
  require('assert').fail('This should not happen');
}

module.exports = { serializeError, deserializeError };

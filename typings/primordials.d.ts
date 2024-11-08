type UncurryThis<T extends (this: unknown, ...args: unknown[]) => unknown> =
  (self: ThisParameterType<T>, ...args: Parameters<T>) => ReturnType<T>;
type UncurryThisStaticApply<T extends (this: unknown, ...args: unknown[]) => unknown> =
  (self: ThisParameterType<T>, args: Parameters<T>) => ReturnType<T>;
type StaticApply<T extends (this: unknown, ...args: unknown[]) => unknown> =
  (args: Parameters<T>) => ReturnType<T>;

type UncurryMethod<O, K extends keyof O, T = O> =
  O[K] extends (this: infer U, ...args: infer A) => infer R
    ? (self: unknown extends U ? T : U, ...args: A) => R
    : never;
type UncurryMethodApply<O, K extends keyof O, T = O> =
  O[K] extends (this: infer U, ...args: infer A) => infer R
    ? (self: unknown extends U ? T : U, args: A) => R
    : never;

type UncurryGetter<O, K extends keyof O, T = O> =
  O[K] extends infer V ? (self: T) => V : never;
type UncurrySetter<O, K extends keyof O, T = O> =
  O[K] extends infer V ? (self: T, value: V) => void : never;

type TypedArrayContentType<T extends TypedArray> = T extends { [k: number]: infer V } ? V : never;

/**
 * Primordials are a way to safely use globals without fear of global mutation
 * Generally, this means removing `this` parameter usage and instead using
 * a regular parameter:
 *
 * @example
 *
 * ```js
 * 'thing'.startsWith('hello');
 * ```
 *
 * becomes
 *
 * ```js
 * primordials.StringPrototypeStartsWith('thing', 'hello')
 * ```
 */
declare namespace primordials {
  export function uncurryThis<T extends (...args: unknown[]) => unknown>(fn: T): UncurryThis<T>;
  export function makeSafe<T extends NewableFunction>(unsafe: NewableFunction, safe: T): T;

  export import decodeURI = globalThis.decodeURI;
  export import decodeURIComponent = globalThis.decodeURIComponent;
  export import encodeURI = globalThis.encodeURI;
  export import encodeURIComponent = globalThis.encodeURIComponent;
  export const JSONParse: typeof JSON.parse
  export const JSONStringify: typeof JSON.stringify
  export const MathAbs: typeof Math.abs
  export const MathAcos: typeof Math.acos
  export const MathAcosh: typeof Math.acosh
  export const MathAsin: typeof Math.asin
  export const MathAsinh: typeof Math.asinh
  export const MathAtan: typeof Math.atan
  export const MathAtanh: typeof Math.atanh
  export const MathAtan2: typeof Math.atan2
  export const MathCeil: typeof Math.ceil
  export const MathCbrt: typeof Math.cbrt
  export const MathExpm1: typeof Math.expm1
  export const MathClz32: typeof Math.clz32
  export const MathCos: typeof Math.cos
  export const MathCosh: typeof Math.cosh
  export const MathExp: typeof Math.exp
  export const MathFloor: typeof Math.floor
  export const MathFround: typeof Math.fround
  export const MathHypot: typeof Math.hypot
  export const MathImul: typeof Math.imul
  export const MathLog: typeof Math.log
  export const MathLog1p: typeof Math.log1p
  export const MathLog2: typeof Math.log2
  export const MathLog10: typeof Math.log10
  export const MathMax: typeof Math.max
  export const MathMaxApply: StaticApply<typeof Math.max>
  export const MathMin: typeof Math.min
  export const MathPow: typeof Math.pow
  export const MathRandom: typeof Math.random
  export const MathRound: typeof Math.round
  export const MathSign: typeof Math.sign
  export const MathSin: typeof Math.sin
  export const MathSinh: typeof Math.sinh
  export const MathSqrt: typeof Math.sqrt
  export const MathTan: typeof Math.tan
  export const MathTanh: typeof Math.tanh
  export const MathTrunc: typeof Math.trunc
  export const MathE: typeof Math.E
  export const MathLN10: typeof Math.LN10
  export const MathLN2: typeof Math.LN2
  export const MathLOG10E: typeof Math.LOG10E
  export const MathLOG2E: typeof Math.LOG2E
  export const MathPI: typeof Math.PI
  export const MathSQRT1_2: typeof Math.SQRT1_2
  export const MathSQRT2: typeof Math.SQRT2
  export const ReflectDefineProperty: typeof Reflect.defineProperty
  export const ReflectDeleteProperty: typeof Reflect.deleteProperty
  export const ReflectApply: typeof Reflect.apply
  export const ReflectConstruct: typeof Reflect.construct
  export const ReflectGet: typeof Reflect.get
  export const ReflectGetOwnPropertyDescriptor: typeof Reflect.getOwnPropertyDescriptor
  export const ReflectGetPrototypeOf: typeof Reflect.getPrototypeOf
  export const ReflectHas: typeof Reflect.has
  export const ReflectIsExtensible: typeof Reflect.isExtensible
  export const ReflectOwnKeys: typeof Reflect.ownKeys
  export const ReflectPreventExtensions: typeof Reflect.preventExtensions
  export const ReflectSet: typeof Reflect.set
  export const ReflectSetPrototypeOf: typeof Reflect.setPrototypeOf
  export import AggregateError = globalThis.AggregateError;
  export const AggregateErrorPrototype: typeof AggregateError.prototype
  export import Array = globalThis.Array;
  export const ArrayPrototype: typeof Array.prototype
  export const ArrayIsArray: typeof Array.isArray
  export const ArrayFrom: typeof Array.from
  export const ArrayFromAsync: typeof Array.fromAsync
  export const ArrayOf: typeof Array.of
  export const ArrayPrototypeConcat: UncurryThis<typeof Array.prototype.concat>
  export const ArrayPrototypeCopyWithin: UncurryThis<typeof Array.prototype.copyWithin>
  export const ArrayPrototypeFill: UncurryThis<typeof Array.prototype.fill>
  export const ArrayPrototypeFind: UncurryThis<typeof Array.prototype.find>
  export const ArrayPrototypeFindIndex: UncurryThis<typeof Array.prototype.findIndex>
  export const ArrayPrototypeLastIndexOf: UncurryThis<typeof Array.prototype.lastIndexOf>
  export const ArrayPrototypePop: UncurryThis<typeof Array.prototype.pop>
  export const ArrayPrototypePush: UncurryThis<typeof Array.prototype.push>
  export const ArrayPrototypePushApply: UncurryThisStaticApply<typeof Array.prototype.push>
  export const ArrayPrototypeReverse: UncurryThis<typeof Array.prototype.reverse>
  export const ArrayPrototypeShift: UncurryThis<typeof Array.prototype.shift>
  export const ArrayPrototypeUnshift: UncurryThis<typeof Array.prototype.unshift>
  export const ArrayPrototypeUnshiftApply: UncurryThisStaticApply<typeof Array.prototype.unshift>
  export const ArrayPrototypeSlice: UncurryThis<typeof Array.prototype.slice>
  export const ArrayPrototypeSort: UncurryThis<typeof Array.prototype.sort>
  export const ArrayPrototypeSplice: UncurryThis<typeof Array.prototype.splice>
  export const ArrayPrototypeToSorted: UncurryThis<typeof Array.prototype.toSorted>
  export const ArrayPrototypeIncludes: UncurryThis<typeof Array.prototype.includes>
  export const ArrayPrototypeIndexOf: UncurryThis<typeof Array.prototype.indexOf>
  export const ArrayPrototypeJoin: UncurryThis<typeof Array.prototype.join>
  export const ArrayPrototypeKeys: UncurryThis<typeof Array.prototype.keys>
  export const ArrayPrototypeEntries: UncurryThis<typeof Array.prototype.entries>
  export const ArrayPrototypeValues: UncurryThis<typeof Array.prototype.values>
  export const ArrayPrototypeForEach: UncurryThis<typeof Array.prototype.forEach>
  export const ArrayPrototypeFilter: UncurryThis<typeof Array.prototype.filter>
  export const ArrayPrototypeFlat: UncurryThis<typeof Array.prototype.flat>
  export const ArrayPrototypeFlatMap: UncurryThis<typeof Array.prototype.flatMap>
  export const ArrayPrototypeMap: UncurryThis<typeof Array.prototype.map>
  export const ArrayPrototypeEvery: UncurryThis<typeof Array.prototype.every>
  export const ArrayPrototypeSome: UncurryThis<typeof Array.prototype.some>
  export const ArrayPrototypeReduce: UncurryThis<typeof Array.prototype.reduce>
  export const ArrayPrototypeReduceRight: UncurryThis<typeof Array.prototype.reduceRight>
  export const ArrayPrototypeToLocaleString: UncurryThis<typeof Array.prototype.toLocaleString>
  export const ArrayPrototypeToString: UncurryThis<typeof Array.prototype.toString>
  export const ArrayPrototypeSymbolIterator: UncurryMethod<typeof Array.prototype, typeof Symbol.iterator>;
  export import ArrayBuffer = globalThis.ArrayBuffer;
  export const ArrayBufferPrototype: typeof ArrayBuffer.prototype
  export const ArrayBufferIsView: typeof ArrayBuffer.isView
  export const ArrayBufferPrototypeGetDetached: UncurryThis<typeof ArrayBuffer.prototype.detached>
  export const ArrayBufferPrototypeSlice: UncurryThis<typeof ArrayBuffer.prototype.slice>
  export const ArrayBufferPrototypeTransfer: UncurryThis<typeof ArrayBuffer.prototype.transfer>
  export const ArrayBufferPrototypeGetByteLength: UncurryGetter<typeof ArrayBuffer.prototype , "byteLength">;
  export const AsyncIteratorPrototype: AsyncIterable<any>;
  export import BigInt = globalThis.BigInt;
  export const BigIntPrototype: typeof BigInt.prototype
  export const BigIntAsUintN: typeof BigInt.asUintN
  export const BigIntAsIntN: typeof BigInt.asIntN
  export const BigIntPrototypeToLocaleString: UncurryThis<typeof BigInt.prototype.toLocaleString>
  export const BigIntPrototypeToString: UncurryThis<typeof BigInt.prototype.toString>
  export const BigIntPrototypeValueOf: UncurryThis<typeof BigInt.prototype.valueOf>
  export import BigInt64Array = globalThis.BigInt64Array;
  export const BigInt64ArrayPrototype: typeof BigInt64Array.prototype
  export const BigInt64ArrayBYTES_PER_ELEMENT: typeof BigInt64Array.BYTES_PER_ELEMENT
  export import BigUint64Array = globalThis.BigUint64Array;
  export const BigUint64ArrayPrototype: typeof BigUint64Array.prototype
  export const BigUint64ArrayBYTES_PER_ELEMENT: typeof BigUint64Array.BYTES_PER_ELEMENT
  export import Boolean = globalThis.Boolean;
  export const BooleanPrototype: typeof Boolean.prototype
  export const BooleanPrototypeToString: UncurryThis<typeof Boolean.prototype.toString>
  export const BooleanPrototypeValueOf: UncurryThis<typeof Boolean.prototype.valueOf>
  export import DataView = globalThis.DataView;
  export const DataViewPrototype: typeof DataView.prototype
  export const DataViewPrototypeGetInt8: UncurryThis<typeof DataView.prototype.getInt8>
  export const DataViewPrototypeSetInt8: UncurryThis<typeof DataView.prototype.setInt8>
  export const DataViewPrototypeGetUint8: UncurryThis<typeof DataView.prototype.getUint8>
  export const DataViewPrototypeSetUint8: UncurryThis<typeof DataView.prototype.setUint8>
  export const DataViewPrototypeGetInt16: UncurryThis<typeof DataView.prototype.getInt16>
  export const DataViewPrototypeSetInt16: UncurryThis<typeof DataView.prototype.setInt16>
  export const DataViewPrototypeGetUint16: UncurryThis<typeof DataView.prototype.getUint16>
  export const DataViewPrototypeSetUint16: UncurryThis<typeof DataView.prototype.setUint16>
  export const DataViewPrototypeGetInt32: UncurryThis<typeof DataView.prototype.getInt32>
  export const DataViewPrototypeSetInt32: UncurryThis<typeof DataView.prototype.setInt32>
  export const DataViewPrototypeGetUint32: UncurryThis<typeof DataView.prototype.getUint32>
  export const DataViewPrototypeSetUint32: UncurryThis<typeof DataView.prototype.setUint32>
  export const DataViewPrototypeGetFloat32: UncurryThis<typeof DataView.prototype.getFloat32>
  export const DataViewPrototypeSetFloat32: UncurryThis<typeof DataView.prototype.setFloat32>
  export const DataViewPrototypeGetFloat64: UncurryThis<typeof DataView.prototype.getFloat64>
  export const DataViewPrototypeSetFloat64: UncurryThis<typeof DataView.prototype.setFloat64>
  export const DataViewPrototypeGetBigInt64: UncurryThis<typeof DataView.prototype.getBigInt64>
  export const DataViewPrototypeSetBigInt64: UncurryThis<typeof DataView.prototype.setBigInt64>
  export const DataViewPrototypeGetBigUint64: UncurryThis<typeof DataView.prototype.getBigUint64>
  export const DataViewPrototypeSetBigUint64: UncurryThis<typeof DataView.prototype.setBigUint64>
  export const DataViewPrototypeGetBuffer: UncurryGetter<typeof DataView.prototype, "buffer">;
  export const DataViewPrototypeGetByteLength: UncurryGetter<typeof DataView.prototype, "byteLength">;
  export const DataViewPrototypeGetByteOffset: UncurryGetter<typeof DataView.prototype, "byteOffset">;
  export import Date = globalThis.Date;
  export const DatePrototype: typeof Date.prototype
  export const DateNow: typeof Date.now
  export const DateParse: typeof Date.parse
  export const DateUTC: typeof Date.UTC
  export const DatePrototypeToString: UncurryThis<typeof Date.prototype.toString>
  export const DatePrototypeToDateString: UncurryThis<typeof Date.prototype.toDateString>
  export const DatePrototypeToTimeString: UncurryThis<typeof Date.prototype.toTimeString>
  export const DatePrototypeToISOString: UncurryThis<typeof Date.prototype.toISOString>
  export const DatePrototypeToUTCString: UncurryThis<typeof Date.prototype.toUTCString>
  export const DatePrototypeGetDate: UncurryThis<typeof Date.prototype.getDate>
  export const DatePrototypeSetDate: UncurryThis<typeof Date.prototype.setDate>
  export const DatePrototypeGetDay: UncurryThis<typeof Date.prototype.getDay>
  export const DatePrototypeGetFullYear: UncurryThis<typeof Date.prototype.getFullYear>
  export const DatePrototypeSetFullYear: UncurryThis<typeof Date.prototype.setFullYear>
  export const DatePrototypeGetHours: UncurryThis<typeof Date.prototype.getHours>
  export const DatePrototypeSetHours: UncurryThis<typeof Date.prototype.setHours>
  export const DatePrototypeGetMilliseconds: UncurryThis<typeof Date.prototype.getMilliseconds>
  export const DatePrototypeSetMilliseconds: UncurryThis<typeof Date.prototype.setMilliseconds>
  export const DatePrototypeGetMinutes: UncurryThis<typeof Date.prototype.getMinutes>
  export const DatePrototypeSetMinutes: UncurryThis<typeof Date.prototype.setMinutes>
  export const DatePrototypeGetMonth: UncurryThis<typeof Date.prototype.getMonth>
  export const DatePrototypeSetMonth: UncurryThis<typeof Date.prototype.setMonth>
  export const DatePrototypeGetSeconds: UncurryThis<typeof Date.prototype.getSeconds>
  export const DatePrototypeSetSeconds: UncurryThis<typeof Date.prototype.setSeconds>
  export const DatePrototypeGetTime: UncurryThis<typeof Date.prototype.getTime>
  export const DatePrototypeSetTime: UncurryThis<typeof Date.prototype.setTime>
  export const DatePrototypeGetTimezoneOffset: UncurryThis<typeof Date.prototype.getTimezoneOffset>
  export const DatePrototypeGetUTCDate: UncurryThis<typeof Date.prototype.getUTCDate>
  export const DatePrototypeSetUTCDate: UncurryThis<typeof Date.prototype.setUTCDate>
  export const DatePrototypeGetUTCDay: UncurryThis<typeof Date.prototype.getUTCDay>
  export const DatePrototypeGetUTCFullYear: UncurryThis<typeof Date.prototype.getUTCFullYear>
  export const DatePrototypeSetUTCFullYear: UncurryThis<typeof Date.prototype.setUTCFullYear>
  export const DatePrototypeGetUTCHours: UncurryThis<typeof Date.prototype.getUTCHours>
  export const DatePrototypeSetUTCHours: UncurryThis<typeof Date.prototype.setUTCHours>
  export const DatePrototypeGetUTCMilliseconds: UncurryThis<typeof Date.prototype.getUTCMilliseconds>
  export const DatePrototypeSetUTCMilliseconds: UncurryThis<typeof Date.prototype.setUTCMilliseconds>
  export const DatePrototypeGetUTCMinutes: UncurryThis<typeof Date.prototype.getUTCMinutes>
  export const DatePrototypeSetUTCMinutes: UncurryThis<typeof Date.prototype.setUTCMinutes>
  export const DatePrototypeGetUTCMonth: UncurryThis<typeof Date.prototype.getUTCMonth>
  export const DatePrototypeSetUTCMonth: UncurryThis<typeof Date.prototype.setUTCMonth>
  export const DatePrototypeGetUTCSeconds: UncurryThis<typeof Date.prototype.getUTCSeconds>
  export const DatePrototypeSetUTCSeconds: UncurryThis<typeof Date.prototype.setUTCSeconds>
  export const DatePrototypeValueOf: UncurryThis<typeof Date.prototype.valueOf>
  export const DatePrototypeToJSON: UncurryThis<typeof Date.prototype.toJSON>
  export const DatePrototypeToLocaleString: UncurryThis<typeof Date.prototype.toLocaleString>
  export const DatePrototypeToLocaleDateString: UncurryThis<typeof Date.prototype.toLocaleDateString>
  export const DatePrototypeToLocaleTimeString: UncurryThis<typeof Date.prototype.toLocaleTimeString>
  export const DatePrototypeSymbolToPrimitive: UncurryMethod<typeof Date.prototype, typeof Symbol.toPrimitive>;
  export import Error = globalThis.Error;
  export const ErrorPrototype: typeof Error.prototype
  // @ts-ignore
  export const ErrorCaptureStackTrace: typeof Error.captureStackTrace
  export const ErrorPrototypeToString: UncurryThis<typeof Error.prototype.toString>
  export import EvalError = globalThis.EvalError;
  export const EvalErrorPrototype: typeof EvalError.prototype
  export import Float32Array = globalThis.Float32Array;
  export const Float32ArrayPrototype: typeof Float32Array.prototype
  export const Float32ArrayBYTES_PER_ELEMENT: typeof Float32Array.BYTES_PER_ELEMENT
  export import Float64Array = globalThis.Float64Array;
  export const Float64ArrayPrototype: typeof Float64Array.prototype
  export const Float64ArrayBYTES_PER_ELEMENT: typeof Float64Array.BYTES_PER_ELEMENT
  export import Function = globalThis.Function;
  export const FunctionLength: typeof Function.length
  export const FunctionName: typeof Function.name
  export const FunctionPrototype: typeof Function.prototype
  export const FunctionPrototypeApply: UncurryThis<typeof Function.prototype.apply>
  export const FunctionPrototypeBind: UncurryThis<typeof Function.prototype.bind>
  export const FunctionPrototypeCall: UncurryThis<typeof Function.prototype.call>
  export const FunctionPrototypeToString: UncurryThis<typeof Function.prototype.toString>
  export import Int16Array = globalThis.Int16Array;
  export const Int16ArrayPrototype: typeof Int16Array.prototype
  export const Int16ArrayBYTES_PER_ELEMENT: typeof Int16Array.BYTES_PER_ELEMENT
  export import Int32Array = globalThis.Int32Array;
  export const Int32ArrayPrototype: typeof Int32Array.prototype
  export const Int32ArrayBYTES_PER_ELEMENT: typeof Int32Array.BYTES_PER_ELEMENT
  export import Int8Array = globalThis.Int8Array;
  export const Int8ArrayPrototype: typeof Int8Array.prototype
  export const Int8ArrayBYTES_PER_ELEMENT: typeof Int8Array.BYTES_PER_ELEMENT
  export import Map = globalThis.Map;
  export const MapPrototype: typeof Map.prototype
  export const MapPrototypeGet: UncurryThis<typeof Map.prototype.get>
  export const MapPrototypeSet: UncurryThis<typeof Map.prototype.set>
  export const MapPrototypeHas: UncurryThis<typeof Map.prototype.has>
  export const MapPrototypeDelete: UncurryThis<typeof Map.prototype.delete>
  export const MapPrototypeClear: UncurryThis<typeof Map.prototype.clear>
  export const MapPrototypeEntries: UncurryThis<typeof Map.prototype.entries>
  export const MapPrototypeForEach: UncurryThis<typeof Map.prototype.forEach>
  export const MapPrototypeKeys: UncurryThis<typeof Map.prototype.keys>
  export const MapPrototypeValues: UncurryThis<typeof Map.prototype.values>
  export const MapPrototypeGetSize: UncurryGetter<typeof Map.prototype, "size">;
  export import Number = globalThis.Number;
  export const NumberPrototype: typeof Number.prototype
  export const NumberIsFinite: typeof Number.isFinite
  export const NumberIsInteger: typeof Number.isInteger
  export const NumberIsNaN: typeof Number.isNaN
  export const NumberIsSafeInteger: typeof Number.isSafeInteger
  export const NumberParseFloat: typeof Number.parseFloat
  export const NumberParseInt: typeof Number.parseInt
  export const NumberMAX_VALUE: typeof Number.MAX_VALUE
  export const NumberMIN_VALUE: typeof Number.MIN_VALUE
  export const NumberNaN: typeof Number.NaN
  export const NumberNEGATIVE_INFINITY: typeof Number.NEGATIVE_INFINITY
  export const NumberPOSITIVE_INFINITY: typeof Number.POSITIVE_INFINITY
  export const NumberMAX_SAFE_INTEGER: typeof Number.MAX_SAFE_INTEGER
  export const NumberMIN_SAFE_INTEGER: typeof Number.MIN_SAFE_INTEGER
  export const NumberEPSILON: typeof Number.EPSILON
  export const NumberPrototypeToExponential: UncurryThis<typeof Number.prototype.toExponential>
  export const NumberPrototypeToFixed: UncurryThis<typeof Number.prototype.toFixed>
  export const NumberPrototypeToPrecision: UncurryThis<typeof Number.prototype.toPrecision>
  export const NumberPrototypeToString: UncurryThis<typeof Number.prototype.toString>
  export const NumberPrototypeValueOf: UncurryThis<typeof Number.prototype.valueOf>
  export const NumberPrototypeToLocaleString: UncurryThis<typeof Number.prototype.toLocaleString>
  export import Object = globalThis.Object;
  export const ObjectPrototype: typeof Object.prototype
  export const ObjectAssign: typeof Object.assign
  export const ObjectGetOwnPropertyDescriptor: typeof Object.getOwnPropertyDescriptor
  export const ObjectGetOwnPropertyDescriptors: typeof Object.getOwnPropertyDescriptors
  export const ObjectGetOwnPropertyNames: typeof Object.getOwnPropertyNames
  export const ObjectGetOwnPropertySymbols: typeof Object.getOwnPropertySymbols
  export const ObjectIs: typeof Object.is
  export const ObjectPreventExtensions: typeof Object.preventExtensions
  export const ObjectSeal: typeof Object.seal
  export const ObjectCreate: typeof Object.create
  export const ObjectDefineProperties: typeof Object.defineProperties
  export const ObjectDefineProperty: typeof Object.defineProperty
  export const ObjectFreeze: typeof Object.freeze
  export const ObjectGetPrototypeOf: typeof Object.getPrototypeOf
  export const ObjectSetPrototypeOf: typeof Object.setPrototypeOf
  export const ObjectIsExtensible: typeof Object.isExtensible
  export const ObjectIsFrozen: typeof Object.isFrozen
  export const ObjectIsSealed: typeof Object.isSealed
  export const ObjectKeys: typeof Object.keys
  export const ObjectEntries: typeof Object.entries
  export const ObjectFromEntries: typeof Object.fromEntries
  export const ObjectValues: typeof Object.values
  export const ObjectPrototypeHasOwnProperty: UncurryThis<typeof Object.prototype.hasOwnProperty>
  export const ObjectPrototypeIsPrototypeOf: UncurryThis<typeof Object.prototype.isPrototypeOf>
  export const ObjectPrototypePropertyIsEnumerable: UncurryThis<typeof Object.prototype.propertyIsEnumerable>
  export const ObjectPrototypeToString: UncurryThis<typeof Object.prototype.toString>
  export const ObjectPrototypeValueOf: UncurryThis<typeof Object.prototype.valueOf>
  export const ObjectPrototypeToLocaleString: UncurryThis<typeof Object.prototype.toLocaleString>
  export import RangeError = globalThis.RangeError;
  export const RangeErrorPrototype: typeof RangeError.prototype
  export import ReferenceError = globalThis.ReferenceError;
  export const ReferenceErrorPrototype: typeof ReferenceError.prototype
  export import RegExp = globalThis.RegExp;
  export const RegExpPrototype: typeof RegExp.prototype
  export const RegExpPrototypeExec: UncurryThis<typeof RegExp.prototype.exec>
  export const RegExpPrototypeCompile: UncurryThis<typeof RegExp.prototype.compile>
  export const RegExpPrototypeToString: UncurryThis<typeof RegExp.prototype.toString>
  export const RegExpPrototypeTest: UncurryThis<typeof RegExp.prototype.test>
  export const RegExpPrototypeGetDotAll: UncurryGetter<typeof RegExp.prototype, "dotAll">;
  export const RegExpPrototypeGetFlags: UncurryGetter<typeof RegExp.prototype, "flags">;
  export const RegExpPrototypeGetGlobal: UncurryGetter<typeof RegExp.prototype, "global">;
  export const RegExpPrototypeGetIgnoreCase: UncurryGetter<typeof RegExp.prototype, "ignoreCase">;
  export const RegExpPrototypeGetMultiline: UncurryGetter<typeof RegExp.prototype, "multiline">;
  export const RegExpPrototypeGetSource: UncurryGetter<typeof RegExp.prototype, "source">;
  export const RegExpPrototypeGetSticky: UncurryGetter<typeof RegExp.prototype, "sticky">;
  export const RegExpPrototypeGetUnicode: UncurryGetter<typeof RegExp.prototype, "unicode">;
  export import Set = globalThis.Set;
  export const SetLength: typeof Set.length
  export const SetName: typeof Set.name
  export const SetPrototype: typeof Set.prototype
  export const SetPrototypeHas: UncurryThis<typeof Set.prototype.has>
  export const SetPrototypeAdd: UncurryThis<typeof Set.prototype.add>
  export const SetPrototypeDelete: UncurryThis<typeof Set.prototype.delete>
  export const SetPrototypeClear: UncurryThis<typeof Set.prototype.clear>
  export const SetPrototypeEntries: UncurryThis<typeof Set.prototype.entries>
  export const SetPrototypeForEach: UncurryThis<typeof Set.prototype.forEach>
  export const SetPrototypeValues: UncurryThis<typeof Set.prototype.values>
  export const SetPrototypeKeys: UncurryThis<typeof Set.prototype.keys>
  export const SetPrototypeGetSize: UncurryGetter<typeof Set.prototype, "size">;
  export import String = globalThis.String;
  export const StringLength: typeof String.length
  export const StringName: typeof String.name
  export const StringPrototype: typeof String.prototype
  export const StringFromCharCode: typeof String.fromCharCode
  export const StringFromCharCodeApply: StaticApply<typeof String.fromCharCode>
  export const StringFromCodePoint: typeof String.fromCodePoint
  export const StringFromCodePointApply: StaticApply<typeof String.fromCodePoint>
  export const StringRaw: typeof String.raw
  export const StringPrototypeAnchor: UncurryThis<typeof String.prototype.anchor>
  export const StringPrototypeBig: UncurryThis<typeof String.prototype.big>
  export const StringPrototypeBlink: UncurryThis<typeof String.prototype.blink>
  export const StringPrototypeBold: UncurryThis<typeof String.prototype.bold>
  export const StringPrototypeCharAt: UncurryThis<typeof String.prototype.charAt>
  export const StringPrototypeCharCodeAt: UncurryThis<typeof String.prototype.charCodeAt>
  export const StringPrototypeCodePointAt: UncurryThis<typeof String.prototype.codePointAt>
  export const StringPrototypeConcat: UncurryThis<typeof String.prototype.concat>
  export const StringPrototypeEndsWith: UncurryThis<typeof String.prototype.endsWith>
  export const StringPrototypeFontcolor: UncurryThis<typeof String.prototype.fontcolor>
  export const StringPrototypeFontsize: UncurryThis<typeof String.prototype.fontsize>
  export const StringPrototypeFixed: UncurryThis<typeof String.prototype.fixed>
  export const StringPrototypeIncludes: UncurryThis<typeof String.prototype.includes>
  export const StringPrototypeIndexOf: UncurryThis<typeof String.prototype.indexOf>
  export const StringPrototypeItalics: UncurryThis<typeof String.prototype.italics>
  export const StringPrototypeLastIndexOf: UncurryThis<typeof String.prototype.lastIndexOf>
  export const StringPrototypeLink: UncurryThis<typeof String.prototype.link>
  export const StringPrototypeLocaleCompare: UncurryThis<typeof String.prototype.localeCompare>
  export const StringPrototypeMatch: UncurryThis<typeof String.prototype.match>
  export const StringPrototypeMatchAll: UncurryThis<typeof String.prototype.matchAll>
  export const StringPrototypeNormalize: UncurryThis<typeof String.prototype.normalize>
  export const StringPrototypePadEnd: UncurryThis<typeof String.prototype.padEnd>
  export const StringPrototypePadStart: UncurryThis<typeof String.prototype.padStart>
  export const StringPrototypeRepeat: UncurryThis<typeof String.prototype.repeat>
  export const StringPrototypeReplace: UncurryThis<typeof String.prototype.replace>
  export const StringPrototypeSearch: UncurryThis<typeof String.prototype.search>
  export const StringPrototypeSlice: UncurryThis<typeof String.prototype.slice>
  export const StringPrototypeSmall: UncurryThis<typeof String.prototype.small>
  export const StringPrototypeSplit: UncurryThis<typeof String.prototype.split>
  export const StringPrototypeStrike: UncurryThis<typeof String.prototype.strike>
  export const StringPrototypeSub: UncurryThis<typeof String.prototype.sub>
  export const StringPrototypeSubstr: UncurryThis<typeof String.prototype.substr>
  export const StringPrototypeSubstring: UncurryThis<typeof String.prototype.substring>
  export const StringPrototypeSup: UncurryThis<typeof String.prototype.sup>
  export const StringPrototypeStartsWith: UncurryThis<typeof String.prototype.startsWith>
  export const StringPrototypeToString: UncurryThis<typeof String.prototype.toString>
  export const StringPrototypeTrim: UncurryThis<typeof String.prototype.trim>
  export const StringPrototypeTrimStart: UncurryThis<typeof String.prototype.trimStart>
  export const StringPrototypeTrimLeft: UncurryThis<typeof String.prototype.trimLeft>
  export const StringPrototypeTrimEnd: UncurryThis<typeof String.prototype.trimEnd>
  export const StringPrototypeTrimRight: UncurryThis<typeof String.prototype.trimRight>
  export const StringPrototypeToLocaleLowerCase: UncurryThis<typeof String.prototype.toLocaleLowerCase>
  export const StringPrototypeToLocaleUpperCase: UncurryThis<typeof String.prototype.toLocaleUpperCase>
  export const StringPrototypeToLowerCase: UncurryThis<typeof String.prototype.toLowerCase>
  export const StringPrototypeToUpperCase: UncurryThis<typeof String.prototype.toUpperCase>
  export const StringPrototypeToWellFormed: UncurryThis<typeof String.prototype.toWellFormed>
  export const StringPrototypeValueOf: UncurryThis<typeof String.prototype.valueOf>
  export const StringPrototypeReplaceAll: UncurryThis<typeof String.prototype.replaceAll>
  export import Symbol = globalThis.Symbol;
  export const SymbolPrototype: typeof Symbol.prototype
  export const SymbolFor: typeof Symbol.for
  export const SymbolKeyFor: typeof Symbol.keyFor
  export const SymbolAsyncDispose: typeof Symbol.asyncDispose
  export const SymbolAsyncIterator: typeof Symbol.asyncIterator
  export const SymbolDispose: typeof Symbol.dispose
  export const SymbolHasInstance: typeof Symbol.hasInstance
  export const SymbolIsConcatSpreadable: typeof Symbol.isConcatSpreadable
  export const SymbolIterator: typeof Symbol.iterator
  export const SymbolMatch: typeof Symbol.match
  export const SymbolMatchAll: typeof Symbol.matchAll
  export const SymbolReplace: typeof Symbol.replace
  export const SymbolSearch: typeof Symbol.search
  export const SymbolSpecies: typeof Symbol.species
  export const SymbolSplit: typeof Symbol.split
  export const SymbolToPrimitive: typeof Symbol.toPrimitive
  export const SymbolToStringTag: typeof Symbol.toStringTag
  export const SymbolUnscopables: typeof Symbol.unscopables
  export const SymbolPrototypeToString: UncurryThis<typeof Symbol.prototype.toString>
  export const SymbolPrototypeValueOf: UncurryThis<typeof Symbol.prototype.valueOf>
  export const SymbolPrototypeSymbolToPrimitive: UncurryMethod<typeof Symbol.prototype, typeof Symbol.toPrimitive, symbol | Symbol>;
  export const SymbolPrototypeGetDescription: UncurryGetter<typeof Symbol.prototype, "description", symbol | Symbol>;
  export import SyntaxError = globalThis.SyntaxError;
  export const SyntaxErrorPrototype: typeof SyntaxError.prototype
  export import TypeError = globalThis.TypeError;
  export const TypeErrorPrototype: typeof TypeError.prototype
  export function TypedArrayFrom<T extends TypedArray>(
    constructor: new (length: number) => T,
    source:
      | Iterable<TypedArrayContentType<T>>
      | ArrayLike<TypedArrayContentType<T>>,
  ): T;
  export function TypedArrayFrom<T extends TypedArray, U, THIS_ARG = undefined>(
    constructor: new (length: number) => T,
    source: Iterable<U> | ArrayLike<U>,
    mapfn: (
      this: THIS_ARG,
      value: U,
      index: number,
    ) => TypedArrayContentType<T>,
    thisArg?: THIS_ARG,
  ): T;
  export function TypedArrayOf<T extends TypedArray>(
    constructor: new (length: number) => T,
    ...items: readonly TypedArrayContentType<T>[]
  ): T;
  export function TypedArrayOfApply<T extends TypedArray>(
    constructor: new (length: number) => T,
    items: readonly TypedArrayContentType<T>[],
  ): T;
  export const TypedArrayPrototypeGetBuffer: UncurryGetter<TypedArray, "buffer">;
  export const TypedArrayPrototypeGetByteLength: UncurryGetter<TypedArray, "byteLength">;
  export const TypedArrayPrototypeGetByteOffset: UncurryGetter<TypedArray, "byteOffset">;
  export const TypedArrayPrototypeGetLength: UncurryGetter<TypedArray, "length">;
  export function TypedArrayPrototypeGetSymbolToStringTag(self: unknown):
    | 'Int8Array'
    | 'Int16Array'
    | 'Int32Array'
    | 'Uint8Array'
    | 'Uint16Array'
    | 'Uint32Array'
    | 'Uint8ClampedArray'
    | 'BigInt64Array'
    | 'BigUint64Array'
    | 'Float32Array'
    | 'Float64Array'
    | undefined;
  export import URIError = globalThis.URIError;
  export const URIErrorPrototype: typeof URIError.prototype
  export import Uint16Array = globalThis.Uint16Array;
  export const Uint16ArrayPrototype: typeof Uint16Array.prototype
  export const Uint16ArrayBYTES_PER_ELEMENT: typeof Uint16Array.BYTES_PER_ELEMENT
  export import Uint32Array = globalThis.Uint32Array;
  export const Uint32ArrayPrototype: typeof Uint32Array.prototype
  export const Uint32ArrayBYTES_PER_ELEMENT: typeof Uint32Array.BYTES_PER_ELEMENT
  export import Uint8Array = globalThis.Uint8Array;
  export const Uint8ArrayPrototype: typeof Uint8Array.prototype
  export const Uint8ArrayBYTES_PER_ELEMENT: typeof Uint8Array.BYTES_PER_ELEMENT
  export import Uint8ClampedArray = globalThis.Uint8ClampedArray;
  export const Uint8ClampedArrayPrototype: typeof Uint8ClampedArray.prototype
  export const Uint8ClampedArrayBYTES_PER_ELEMENT: typeof Uint8ClampedArray.BYTES_PER_ELEMENT
  export import WeakMap = globalThis.WeakMap;
  export const WeakMapPrototype: typeof WeakMap.prototype
  export const WeakMapPrototypeDelete: UncurryThis<typeof WeakMap.prototype.delete>
  export const WeakMapPrototypeGet: UncurryThis<typeof WeakMap.prototype.get>
  export const WeakMapPrototypeSet: UncurryThis<typeof WeakMap.prototype.set>
  export const WeakMapPrototypeHas: UncurryThis<typeof WeakMap.prototype.has>
  export import WeakSet = globalThis.WeakSet;
  export const WeakSetPrototype: typeof WeakSet.prototype
  export const WeakSetPrototypeDelete: UncurryThis<typeof WeakSet.prototype.delete>
  export const WeakSetPrototypeHas: UncurryThis<typeof WeakSet.prototype.has>
  export const WeakSetPrototypeAdd: UncurryThis<typeof WeakSet.prototype.add>
  export import Promise = globalThis.Promise;
  export const PromisePrototype: typeof Promise.prototype
  export const PromiseAll: typeof Promise.all
  export const PromiseRace: typeof Promise.race
  export const PromiseResolve: typeof Promise.resolve
  export const PromiseReject: typeof Promise.reject
  export const PromiseAllSettled: typeof Promise.allSettled
  export const PromiseAny: typeof Promise.any
  export const PromisePrototypeThen: UncurryThis<typeof Promise.prototype.then>
  export const PromisePrototypeCatch: UncurryThis<typeof Promise.prototype.catch>
  export const PromisePrototypeFinally: UncurryThis<typeof Promise.prototype.finally>
  export const PromiseWithResolvers: typeof Promise.withResolvers
  export import Proxy = globalThis.Proxy
  import _globalThis = globalThis
  export { _globalThis as globalThis }
}

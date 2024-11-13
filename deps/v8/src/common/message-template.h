// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_MESSAGE_TEMPLATE_H_
#define V8_COMMON_MESSAGE_TEMPLATE_H_

#include "src/base/logging.h"

namespace v8 {
namespace internal {

#define MESSAGE_TEMPLATES(T)                                                   \
  /* Error */                                                                  \
  T(None, "")                                                                  \
  T(ConflictingPrivateName,                                                    \
    "Operation is ambiguous because there are more than one private name"      \
    "'%' on the object")                                                       \
  T(CyclicProto, "Cyclic __proto__ value")                                     \
  T(Debugger, "Debugger: %")                                                   \
  T(DebuggerLoading, "Error loading debugger")                                 \
  T(DefaultOptionsMissing, "Internal % error. Default options are missing.")   \
  T(DeletePrivateField, "Private fields can not be deleted")                   \
  T(PlaceholderOnly, "%")                                                      \
  T(UncaughtException, "Uncaught %")                                           \
  T(Unsupported, "Not supported")                                              \
  T(WrongServiceType, "Internal error, wrong service type: %")                 \
  T(WrongValueType, "Internal error. Wrong value type.")                       \
  T(IcuError, "Internal error. Icu error.")                                    \
  /* TypeError */                                                              \
  T(ApplyNonFunction,                                                          \
    "Function.prototype.apply was called on %, which is % and not a "          \
    "function")                                                                \
  T(ArgumentsDisallowedInInitializerAndStaticBlock,                            \
    "'arguments' is not allowed in class field initializer or static "         \
    "initialization block")                                                    \
  T(ArgumentIsNonObject, "% argument must be an object")                       \
  T(ArgumentIsNonString, "% argument must be a string")                        \
  T(ArrayBufferDetachKeyDoesntMatch,                                           \
    "Provided key doesn't match [[ArrayBufferDetachKey]]")                     \
  T(ArrayBufferTooShort,                                                       \
    "Derived ArrayBuffer constructor created a buffer which was too small")    \
  T(ArrayBufferSpeciesThis,                                                    \
    "ArrayBuffer subclass returned this from species constructor")             \
  T(AwaitNotInAsyncContext,                                                    \
    "await is only valid in async functions and the top level bodies of "      \
    "modules")                                                                 \
  T(AwaitNotInDebugEvaluate,                                                   \
    "await can not be used when evaluating code "                              \
    "while paused in the debugger")                                            \
  T(AtomicsMutexNotOwnedByCurrentThread,                                       \
    "Atomics.Mutex is not owned by the current agent")                         \
  T(AtomicsOperationNotAllowed, "% cannot be called in this context")          \
  T(BadRoundingType, "RoundingType is not fractionDigits")                     \
  T(BadSortComparisonFunction,                                                 \
    "The comparison function must be either a function or undefined: %")       \
  T(BigIntFromNumber,                                                          \
    "The number % cannot be converted to a BigInt because it is not an "       \
    "integer")                                                                 \
  T(BigIntFromObject, "Cannot convert % to a BigInt")                          \
  T(BigIntMixedTypes,                                                          \
    "Cannot mix BigInt and other types, use explicit conversions")             \
  T(BigIntSerializeJSON, "Do not know how to serialize a BigInt")              \
  T(BigIntShr, "BigInts have no unsigned right shift, use >> instead")         \
  T(BigIntToNumber, "Cannot convert a BigInt value to a number")               \
  T(CalledNonCallable, "% is not a function")                                  \
  T(CalledOnNonObject, "% called on non-object")                               \
  T(CalledOnNullOrUndefined, "% called on null or undefined")                  \
  T(CallShadowRealmEvaluateThrew, "ShadowRealm evaluate threw (%)")            \
  T(CallSiteExpectsFunction,                                                   \
    "CallSite expects wasm object as first or function as second argument, "   \
    "got <%, %>")                                                              \
  T(CallSiteMethod, "CallSite method % expects CallSite as receiver")          \
  T(CallSiteMethodUnsupportedInShadowRealm,                                    \
    "CallSite method % is unsupported inside ShadowRealms")                    \
  T(CallWrappedFunctionThrew, "WrappedFunction threw (%)")                     \
  T(CannotBeShared, "% cannot be shared")                                      \
  T(CannotConvertToPrimitive, "Cannot convert object to primitive value")      \
  T(CannotPreventExt, "Cannot prevent extensions")                             \
  T(CannotFreeze, "Cannot freeze")                                             \
  T(CannotFreezeArrayBufferView,                                               \
    "Cannot freeze array buffer views with elements")                          \
  T(CannotSeal, "Cannot seal")                                                 \
  T(CannotWrap, "Cannot wrap target callable (%)")                             \
  T(CircularStructure, "Converting circular structure to JSON%")               \
  T(ConstructAbstractClass, "Abstract class % not directly constructable")     \
  T(ConstAssign, "Assignment to constant variable.")                           \
  T(ConstructorClassField, "Classes may not have a field named 'constructor'") \
  T(ConstructorNonCallable,                                                    \
    "Class constructor % cannot be invoked without 'new'")                     \
  T(AnonymousConstructorNonCallable,                                           \
    "Class constructors cannot be invoked without 'new'")                      \
  T(ConstructorNotFunction, "Constructor % requires 'new'")                    \
  T(ConstructorNotReceiver, "The .constructor property is not an object")      \
  T(CurrencyCode, "Currency code is required with currency style.")            \
  T(CyclicModuleDependency, "Detected cycle while resolving name '%' in '%'")  \
  T(DataViewNotArrayBuffer,                                                    \
    "First argument to DataView constructor must be an ArrayBuffer")           \
  T(DateType, "this is not a Date object.")                                    \
  T(DebuggerFrame, "Debugger: Invalid frame index.")                           \
  T(DebuggerType, "Debugger: Parameters have wrong types.")                    \
  T(DeclarationMissingInitializer, "Missing initializer in % declaration")     \
  T(DefineDisallowed, "Cannot define property %, object is not extensible")    \
  T(DefineDisallowedFixedLayout,                                               \
    "Cannot define property %, object has fixed layout")                       \
  T(DetachedOperation, "Cannot perform % on a detached ArrayBuffer")           \
  T(DoNotUse, "Do not use %; %")                                               \
  T(DuplicateTemplateProperty, "Object template has duplicate property '%'")   \
  T(ExtendsValueNotConstructor,                                                \
    "Class extends value % is not a constructor or null")                      \
  T(FirstArgumentNotRegExp,                                                    \
    "First argument to % must not be a regular expression")                    \
  T(FunctionBind, "Bind must be called on a function")                         \
  T(GeneratorRunning, "Generator is already running")                          \
  T(IllegalInvocation, "Illegal invocation")                                   \
  T(ImmutablePrototypeSet,                                                     \
    "Immutable prototype object '%' cannot have their prototype set")          \
  T(ImportAssertDeprecated,                                                    \
    "'assert' is deprecated in import statements and support will be removed " \
    "in %; use 'with' instead")                                                \
  T(ImportAssertionDuplicateKey, "Import assertion has duplicate key '%'")     \
  T(ImportCallNotNewExpression, "Cannot use new with import")                  \
  T(ImportOutsideModule, "Cannot use import statement outside a module")       \
  T(ImportMetaOutsideModule, "Cannot use 'import.meta' outside a module")      \
  T(ImportMissingSpecifier, "import() requires a specifier")                   \
  T(ImportShadowRealmRejected, "Cannot import in ShadowRealm (%)")             \
  T(IncompatibleMethodReceiver, "Method % called on incompatible receiver %")  \
  T(InstanceofNonobjectProto,                                                  \
    "Function has non-object prototype '%' in instanceof check")               \
  T(InvalidArgument, "invalid_argument")                                       \
  T(InvalidArgumentForTemporal, "Invalid argument for Temporal %")             \
  T(InvalidInOperatorUse, "Cannot use 'in' operator to search for '%' in %")   \
  T(InvalidRawJsonValue, "Invalid value for JSON.rawJSON")                     \
  T(InvalidRegExpExecResult,                                                   \
    "RegExp exec method returned something other than an Object or null")      \
  T(InvalidUnit, "Invalid unit argument for %() '%'")                          \
  T(IsNotNumber, "Type of '%' must be 'number', found '%'")                    \
  T(IterableYieldedNonString, "Iterable yielded % which is not a string")      \
  T(IteratorReduceNoInitial,                                                   \
    "Reduce of a done iterator with no initial value")                         \
  T(IteratorResultNotAnObject, "Iterator result % is not an object")           \
  T(SpreadIteratorSymbolNonCallable,                                           \
    "Spread syntax requires ...iterable[Symbol.iterator] to be a function")    \
  T(FirstArgumentIteratorSymbolNonCallable,                                    \
    "% requires that the property of the first argument, "                     \
    "items[Symbol.iterator], when exists, be a function")                      \
  T(FirstArgumentAsyncIteratorSymbolNonCallable,                               \
    "% requires that the property of the first argument, "                     \
    "items[Symbol.asyncIterator], when exists, be a function")                 \
  T(IteratorValueNotAnObject, "Iterator value % is not an entry object")       \
  T(KeysMethodInvalid, "Result of the keys method is not an object")           \
  T(LanguageID, "Language ID should be string or object.")                     \
  T(LocaleNotEmpty,                                                            \
    "First argument to Intl.Locale constructor can't be empty or missing")     \
  T(LocaleBadParameters, "Incorrect locale information provided")              \
  T(ListFormatBadParameters, "Incorrect ListFormat information provided")      \
  T(MapperFunctionNonCallable, "flatMap mapper function is not callable")      \
  T(MethodInvokedOnWrongType, "Method invoked on an object that is not %.")    \
  T(NoAccess, "no access")                                                     \
  T(NonCallableInInstanceOfCheck,                                              \
    "Right-hand side of 'instanceof' is not callable")                         \
  T(NonCoercible, "Cannot destructure '%' as it is %.")                        \
  T(NonCoercibleWithProperty,                                                  \
    "Cannot destructure property '%' of '%' as it is %.")                      \
  T(NonExtensibleProto, "% is not extensible")                                 \
  T(NonObjectAssertOption, "The 'assert' option must be an object")            \
  T(NonObjectInInstanceOfCheck,                                                \
    "Right-hand side of 'instanceof' is not an object")                        \
  T(NonObjectPrivateNameAccess, "Cannot access private name % from %")         \
  T(NonObjectPropertyLoad, "Cannot read properties of %")                      \
  T(NonObjectPropertyLoadWithProperty,                                         \
    "Cannot read properties of % (reading '%')")                               \
  T(NonObjectPropertyStore, "Cannot set properties of %")                      \
  T(NonObjectPropertyStoreWithProperty,                                        \
    "Cannot set properties of % (setting '%')")                                \
  T(NonObjectImportArgument,                                                   \
    "The second argument to import() must be an object")                       \
  T(NonStringImportAssertionValue, "Import assertion value must be a string")  \
  T(NoSetterInCallback, "Cannot set property % of % which has only a getter")  \
  T(NotAnIterator, "% is not an iterator")                                     \
  T(PromiseNewTargetUndefined,                                                 \
    "Promise constructor cannot be invoked without 'new'")                     \
  T(NotConstructor, "% is not a constructor")                                  \
  T(NotDateObject, "this is not a Date object.")                               \
  T(NotGeneric, "% requires that 'this' be a %")                               \
  T(NotCallable, "% is not a function")                                        \
  T(NotCallableOrIterable,                                                     \
    "% is not a function or its return value is not iterable")                 \
  T(NotCallableOrAsyncIterable,                                                \
    "% is not a function or its return value is not async iterable")           \
  T(NotFiniteNumber, "Value need to be finite number for %()")                 \
  T(NotIterable, "% is not iterable")                                          \
  T(NotIterableNoSymbolLoad, "% is not iterable (cannot read property %)")     \
  T(NotAsyncIterable, "% is not async iterable")                               \
  T(NotPropertyName, "% is not a valid property name")                         \
  T(NotTypedArray, "this is not a typed array.")                               \
  T(NotSuperConstructor, "Super constructor % of % is not a constructor")      \
  T(NotSuperConstructorAnonymousClass,                                         \
    "Super constructor % of anonymous class is not a constructor")             \
  T(NotIntegerTypedArray, "% is not an integer typed array.")                  \
  T(NotInt32OrBigInt64TypedArray,                                              \
    "% is not an int32 or BigInt64 typed array.")                              \
  T(NotSharedTypedArray, "% is not a shared typed array.")                     \
  T(ObjectFixedLayout, "Cannot add property %, object has fixed layout")       \
  T(ObjectGetterExpectingFunction,                                             \
    "Object.prototype.__defineGetter__: Expecting function")                   \
  T(ObjectGetterCallable, "Getter must be a function: %")                      \
  T(ObjectNotExtensible, "Cannot add property %, object is not extensible")    \
  T(ObjectSetterExpectingFunction,                                             \
    "Object.prototype.__defineSetter__: Expecting function")                   \
  T(ObjectSetterCallable, "Setter must be a function: %")                      \
  T(OrdinaryFunctionCalledAsConstructor,                                       \
    "Function object that's not a constructor was created with new")           \
  T(PromiseCyclic, "Chaining cycle detected for promise %")                    \
  T(PromiseExecutorAlreadyInvoked,                                             \
    "Promise executor has already been invoked with non-undefined arguments")  \
  T(PromiseNonCallable, "Promise resolve or reject function is not callable")  \
  T(PropertyDescObject, "Property description must be an object: %")           \
  T(PropertyNotFunction,                                                       \
    "'%' returned for property '%' of object '%' is not a function")           \
  T(ProtoObjectOrNull, "Object prototype may only be an Object or null: %")    \
  T(PrototypeParentNotAnObject,                                                \
    "Class extends value does not have valid prototype property %")            \
  T(ProxyConstructNonObject,                                                   \
    "'construct' on proxy: trap returned non-object ('%')")                    \
  T(ProxyDefinePropertyNonConfigurable,                                        \
    "'defineProperty' on proxy: trap returned truish for defining "            \
    "non-configurable property '%' which is either non-existent or "           \
    "configurable in the proxy target")                                        \
  T(ProxyDefinePropertyNonConfigurableWritable,                                \
    "'defineProperty' on proxy: trap returned truish for defining "            \
    "non-configurable property '%' which cannot be non-writable, unless "      \
    "there exists a corresponding non-configurable, non-writable own "         \
    "property of the target object.")                                          \
  T(ProxyDefinePropertyNonExtensible,                                          \
    "'defineProperty' on proxy: trap returned truish for adding property '%' " \
    " to the non-extensible proxy target")                                     \
  T(ProxyDefinePropertyIncompatible,                                           \
    "'defineProperty' on proxy: trap returned truish for adding property '%' " \
    " that is incompatible with the existing property in the proxy target")    \
  T(ProxyDeletePropertyNonConfigurable,                                        \
    "'deleteProperty' on proxy: trap returned truish for property '%' which "  \
    "is non-configurable in the proxy target")                                 \
  T(ProxyDeletePropertyNonExtensible,                                          \
    "'deleteProperty' on proxy: trap returned truish for property '%' but "    \
    "the proxy target is non-extensible")                                      \
  T(ProxyGetNonConfigurableData,                                               \
    "'get' on proxy: property '%' is a read-only and "                         \
    "non-configurable data property on the proxy target but the proxy "        \
    "did not return its actual value (expected '%' but got '%')")              \
  T(ProxyGetNonConfigurableAccessor,                                           \
    "'get' on proxy: property '%' is a non-configurable accessor "             \
    "property on the proxy target and does not have a getter function, but "   \
    "the trap did not return 'undefined' (got '%')")                           \
  T(ProxyGetOwnPropertyDescriptorIncompatible,                                 \
    "'getOwnPropertyDescriptor' on proxy: trap returned descriptor for "       \
    "property '%' that is incompatible with the existing property in the "     \
    "proxy target")                                                            \
  T(ProxyGetOwnPropertyDescriptorInvalid,                                      \
    "'getOwnPropertyDescriptor' on proxy: trap returned neither object nor "   \
    "undefined for property '%'")                                              \
  T(ProxyGetOwnPropertyDescriptorNonConfigurable,                              \
    "'getOwnPropertyDescriptor' on proxy: trap reported non-configurability "  \
    "for property '%' which is either non-existent or configurable in the "    \
    "proxy target")                                                            \
  T(ProxyGetOwnPropertyDescriptorNonConfigurableWritable,                      \
    "'getOwnPropertyDescriptor' on proxy: trap reported non-configurable "     \
    "and writable for property '%' which is non-configurable, non-writable "   \
    "in the proxy target")                                                     \
  T(ProxyGetOwnPropertyDescriptorNonExtensible,                                \
    "'getOwnPropertyDescriptor' on proxy: trap returned undefined for "        \
    "property '%' which exists in the non-extensible proxy target")            \
  T(ProxyGetOwnPropertyDescriptorUndefined,                                    \
    "'getOwnPropertyDescriptor' on proxy: trap returned undefined for "        \
    "property '%' which is non-configurable in the proxy target")              \
  T(ProxyGetPrototypeOfInvalid,                                                \
    "'getPrototypeOf' on proxy: trap returned neither object nor null")        \
  T(ProxyGetPrototypeOfNonExtensible,                                          \
    "'getPrototypeOf' on proxy: proxy target is non-extensible but the "       \
    "trap did not return its actual prototype")                                \
  T(ProxyHasNonConfigurable,                                                   \
    "'has' on proxy: trap returned falsish for property '%' which exists in "  \
    "the proxy target as non-configurable")                                    \
  T(ProxyHasNonExtensible,                                                     \
    "'has' on proxy: trap returned falsish for property '%' but the proxy "    \
    "target is not extensible")                                                \
  T(ProxyIsExtensibleInconsistent,                                             \
    "'isExtensible' on proxy: trap result does not reflect extensibility of "  \
    "proxy target (which is '%')")                                             \
  T(ProxyNonObject,                                                            \
    "Cannot create proxy with a non-object as target or handler")              \
  T(ProxyOwnKeysMissing,                                                       \
    "'ownKeys' on proxy: trap result did not include '%'")                     \
  T(ProxyOwnKeysNonExtensible,                                                 \
    "'ownKeys' on proxy: trap returned extra keys but proxy target is "        \
    "non-extensible")                                                          \
  T(ProxyOwnKeysDuplicateEntries,                                              \
    "'ownKeys' on proxy: trap returned duplicate entries")                     \
  T(ProxyPreventExtensionsExtensible,                                          \
    "'preventExtensions' on proxy: trap returned truish but the proxy target " \
    "is extensible")                                                           \
  T(ProxyPrivate, "Cannot pass private property name to proxy trap")           \
  T(ProxyRevoked, "Cannot perform '%' on a proxy that has been revoked")       \
  T(ProxySetFrozenData,                                                        \
    "'set' on proxy: trap returned truish for property '%' which exists in "   \
    "the proxy target as a non-configurable and non-writable data property "   \
    "with a different value")                                                  \
  T(ProxySetFrozenAccessor,                                                    \
    "'set' on proxy: trap returned truish for property '%' which exists in "   \
    "the proxy target as a non-configurable and non-writable accessor "        \
    "property without a setter")                                               \
  T(ProxySetPrototypeOfNonExtensible,                                          \
    "'setPrototypeOf' on proxy: trap returned truish for setting a new "       \
    "prototype on the non-extensible proxy target")                            \
  T(ProxyTrapReturnedFalsish, "'%' on proxy: trap returned falsish")           \
  T(ProxyTrapReturnedFalsishFor,                                               \
    "'%' on proxy: trap returned falsish for property '%'")                    \
  T(RedefineDisallowed, "Cannot redefine property: %")                         \
  T(RedefineExternalArray,                                                     \
    "Cannot redefine a property of an object with external array elements")    \
  T(ReduceNoInitial, "Reduce of empty array with no initial value")            \
  T(RegExpFlags,                                                               \
    "Cannot supply flags when constructing one RegExp from another")           \
  T(RegExpNonObject, "% getter called on non-object %")                        \
  T(RegExpNonRegExp, "% getter called on non-RegExp object")                   \
  T(RegExpGlobalInvokedOnNonGlobal,                                            \
    "% called with a non-global RegExp argument")                              \
  T(RelativeDateTimeFormatterBadParameters,                                    \
    "Incorrect RelativeDateTimeFormatter provided")                            \
  T(ResolverNotAFunction, "Promise resolver % is not a function")              \
  T(ReturnMethodNotCallable, "The iterator's 'return' method is not callable") \
  T(SizeIsNaN, "The .size property is NaN")                                    \
  T(ShadowRealmErrorStackNonString,                                            \
    "Error stack is not a string in ShadowRealm (%)")                          \
  T(ShadowRealmErrorStackThrows,                                               \
    "Error stack getter threw in ShadowRealm (%)")                             \
  T(SharedArrayBufferTooShort,                                                 \
    "Derived SharedArrayBuffer constructor created a buffer which was too "    \
    "small")                                                                   \
  T(SharedArrayBufferSpeciesThis,                                              \
    "SharedArrayBuffer subclass returned this from species constructor")       \
  T(SharedStructTypeRegistryMismatch,                                          \
    "SharedStructType registered as '%' does not match")                       \
  T(StaticPrototype,                                                           \
    "Classes may not have a static property named 'prototype'")                \
  T(StrictDeleteProperty, "Cannot delete property '%' of %")                   \
  T(StrictPoisonPill,                                                          \
    "'caller', 'callee', and 'arguments' properties may not be accessed on "   \
    "strict mode functions or the arguments objects for calls to them")        \
  T(StrictReadOnlyProperty,                                                    \
    "Cannot assign to read only property '%' of % '%'")                        \
  T(StrictCannotCreateProperty, "Cannot create property '%' on % '%'")         \
  T(StringMatchAllNullOrUndefinedFlags,                                        \
    "The .flags property of the argument to String.prototype.matchAll cannot " \
    "be null or undefined")                                                    \
  T(SymbolIteratorInvalid,                                                     \
    "Result of the Symbol.iterator method is not an object")                   \
  T(SymbolAsyncIteratorInvalid,                                                \
    "Result of the Symbol.asyncIterator method is not an object")              \
  T(SymbolKeyFor, "% is not a symbol")                                         \
  T(SymbolToNumber, "Cannot convert a Symbol value to a number")               \
  T(SymbolToString, "Cannot convert a Symbol value to a string")               \
  T(ThrowMethodMissing, "The iterator does not provide a 'throw' method.")     \
  T(TopLevelAwaitStalled, "Top-level await promise never resolved")            \
  T(UndefinedOrNullToObject, "Cannot convert undefined or null to object")     \
  T(UsingAssign, "Assignment to using variable.")                              \
  T(ValueAndAccessor,                                                          \
    "Invalid property descriptor. Cannot both specify accessors and a value "  \
    "or writable attribute, %")                                                \
  T(VarRedeclaration, "Identifier '%' has already been declared")              \
  T(VarNotAllowedInEvalScope,                                                  \
    "Identifier '%' cannot be declared with 'var' in current evaluation "      \
    "scope, consider trying 'let' instead")                                    \
  T(WrongArgs, "%: Arguments list has wrong type")                             \
  /* ReferenceError */                                                         \
  T(NotDefined, "% is not defined")                                            \
  T(SuperAlreadyCalled, "Super constructor may only be called once")           \
  T(AccessedUninitializedVariable, "Cannot access '%' before initialization")  \
  T(UnsupportedSuper, "Unsupported reference to 'super'")                      \
  T(AccessedUnavailableVariable, "Cannot access '%' from debugger")            \
  T(DisposableStackIsDisposed,                                                 \
    "Cannot call % on an already-disposed DisposableStack")                    \
  /* RangeError */                                                             \
  T(BigIntDivZero, "Division by zero")                                         \
  T(BigIntTooBig, "Maximum BigInt size exceeded")                              \
  T(CantSetOptionXWhenYIsUsed, "Can't set option % when % is used")            \
  T(DateRange, "Provided date is not in valid range.")                         \
  T(ExpectedLocation,                                                          \
    "Expected letters optionally connected with underscores or hyphens for "   \
    "a location, got %")                                                       \
  T(InvalidArrayBufferLength, "Invalid array buffer length")                   \
  T(InvalidArrayBufferMaxLength, "Invalid array buffer max length")            \
  T(InvalidArrayBufferResizeLength, "%: Invalid length parameter")             \
  T(ArrayBufferAllocationFailed, "Array buffer allocation failed")             \
  T(Invalid, "Invalid % : %")                                                  \
  T(InvalidArrayLength, "Invalid array length")                                \
  T(InvalidAtomicAccessIndex, "Invalid atomic access index")                   \
  T(InvalidCalendar, "Invalid calendar specified: %")                          \
  T(InvalidCodePoint, "Invalid code point %")                                  \
  T(InvalidCountValue, "Invalid count value: %")                               \
  T(InvalidDataViewAccessorOffset,                                             \
    "Offset is outside the bounds of the DataView")                            \
  T(InvalidDataViewLength, "Invalid DataView length %")                        \
  T(InvalidOffset, "Start offset % is outside the bounds of the buffer")       \
  T(InvalidHint, "Invalid hint: %")                                            \
  T(InvalidIndex, "Invalid value: not (convertible to) a safe integer")        \
  T(InvalidLanguageTag, "Invalid language tag: %")                             \
  T(InvalidWeakMapKey, "Invalid value used as weak map key")                   \
  T(InvalidWeakSetValue, "Invalid value used in weak set")                     \
  T(InvalidShadowRealmEvaluateSourceText, "Invalid value used as source text") \
  T(InvalidStringLength, "Invalid string length")                              \
  T(InvalidTimeValue, "Invalid time value")                                    \
  T(InvalidTimeValueForTemporal, "Invalid time value for Temporal %")          \
  T(InvalidTimeZone, "Invalid time zone specified: %")                         \
  T(InvalidTypedArrayAlignment, "% of % should be a multiple of %")            \
  T(InvalidTypedArrayIndex, "Invalid typed array index")                       \
  T(InvalidTypedArrayLength, "Invalid typed array length: %")                  \
  T(LetInLexicalBinding, "let is disallowed as a lexically bound name")        \
  T(LocaleMatcher, "Illegal value for localeMatcher:%")                        \
  T(MaximumFractionDigitsNotEqualMinimumFractionDigits,                        \
    "maximumFractionDigits not equal to minimumFractionDigits")                \
  T(NormalizationForm, "The normalization form should be one of %.")           \
  T(OutOfMemory, "%: Out of memory")                                           \
  T(ZeroDigitNumericSeparator,                                                 \
    "Numeric separator can not be used after leading 0.")                      \
  T(NumberFormatRange, "% argument must be between 0 and 100")                 \
  T(TrailingNumericSeparator,                                                  \
    "Numeric separators are not allowed at the end of numeric literals")       \
  T(ContinuousNumericSeparator,                                                \
    "Only one underscore is allowed as numeric separator")                     \
  T(PropertyValueOutOfRange, "% value is out of range.")                       \
  T(StackOverflow, "Maximum call stack size exceeded")                         \
  T(ToPrecisionFormatRange,                                                    \
    "toPrecision() argument must be between 1 and 100")                        \
  T(ToRadixFormatRange, "toString() radix argument must be between 2 and 36")  \
  T(SharedArraySizeOutOfRange, "SharedArray length out of range")              \
  T(StructFieldCountOutOfRange,                                                \
    "Struct field count out of range (maximum of 999 allowed)")                \
  T(TypedArraySetOffsetOutOfBounds, "offset is out of bounds")                 \
  T(TypedArraySetSourceTooLarge, "Source is too large")                        \
  T(TypedArrayTooLargeToSort,                                                  \
    "Custom comparefn not supported for huge TypedArrays")                     \
  T(ValueOutOfRange, "Value % out of range for % options property %")          \
  T(CollectionGrowFailed, "% maximum size exceeded")                           \
  T(MustBePositive, "% must be positive")                                      \
  T(ArgumentIsNotUndefinedOrNonNegativeInteger,                                \
    "% argument must be undefined or a non-negative integer")                  \
  /* SyntaxError */                                                            \
  T(AmbiguousExport,                                                           \
    "The requested module '%' contains conflicting star exports for name '%'") \
  T(BadGetterArity, "Getter must not have any formal parameters.")             \
  T(BadSetterArity, "Setter must have exactly one formal parameter.")          \
  T(BigIntInvalidString, "Invalid BigInt string")                              \
  T(ConstructorIsAccessor, "Class constructor may not be an accessor")         \
  T(ConstructorIsGenerator, "Class constructor may not be a generator")        \
  T(ConstructorIsAsync, "Class constructor may not be an async method")        \
  T(ConstructorIsPrivate, "Class constructor may not be a private method")     \
  T(DerivedConstructorReturnedNonObject,                                       \
    "Derived constructors may only return object or undefined")                \
  T(DuplicateConstructor, "A class may only have one constructor")             \
  T(DuplicateExport, "Duplicate export of '%'")                                \
  T(DuplicateProto,                                                            \
    "Duplicate __proto__ fields are not allowed in object literals")           \
  T(ForInOfLoopInitializer,                                                    \
    "% loop variable declaration may not have an initializer.")                \
  T(ForOfLet, "The left-hand side of a for-of loop may not start with 'let'.") \
  T(ForOfAsync, "The left-hand side of a for-of loop may not be 'async'.")     \
  T(ForInOfLoopMultiBindings,                                                  \
    "Invalid left-hand side in % loop: Must have a single binding.")           \
  T(GeneratorInSingleStatementContext,                                         \
    "Generators can only be declared at the top level or inside a block.")     \
  T(AsyncFunctionInSingleStatementContext,                                     \
    "Async functions can only be declared at the top level or inside a "       \
    "block.")                                                                  \
  T(IllegalBreak, "Illegal break statement")                                   \
  T(ModuleExportNameWithoutFromClause,                                         \
    "String literal module export names must be followed by a 'from' clause")  \
  T(NoIterationStatement,                                                      \
    "Illegal continue statement: no surrounding iteration statement")          \
  T(IllegalContinue,                                                           \
    "Illegal continue statement: '%' does not denote an iteration statement")  \
  T(IllegalLanguageModeDirective,                                              \
    "Illegal '%' directive in function with non-simple parameter list")        \
  T(IllegalReturn, "Illegal return statement")                                 \
  T(IntrinsicWithSpread, "Intrinsic calls do not support spread arguments")    \
  T(InvalidRestBindingPattern,                                                 \
    "`...` must be followed by an identifier in declaration contexts")         \
  T(InvalidPropertyBindingPattern, "Illegal property in declaration context")  \
  T(InvalidRestAssignmentPattern,                                              \
    "`...` must be followed by an assignable reference in assignment "         \
    "contexts")                                                                \
  T(InvalidEscapedReservedWord, "Keyword must not contain escaped characters") \
  T(InvalidEscapedMetaProperty, "'%' must not contain escaped characters")     \
  T(InvalidLhsInAssignment, "Invalid left-hand side in assignment")            \
  T(InvalidCoverInitializedName, "Invalid shorthand property initializer")     \
  T(InvalidDestructuringTarget, "Invalid destructuring assignment target")     \
  T(InvalidLhsInFor, "Invalid left-hand side in for-loop")                     \
  T(InvalidLhsInPostfixOp,                                                     \
    "Invalid left-hand side expression in postfix operation")                  \
  T(InvalidLhsInPrefixOp,                                                      \
    "Invalid left-hand side expression in prefix operation")                   \
  T(InvalidModuleExportName,                                                   \
    "Invalid module export name: contains unpaired surrogate")                 \
  T(InvalidRegExpFlags, "Invalid flags supplied to RegExp constructor '%'")    \
  T(InvalidOrUnexpectedToken, "Invalid or unexpected token")                   \
  T(InvalidPrivateBrandInstance, "Receiver must be an instance of class %")    \
  T(InvalidPrivateBrandStatic, "Receiver must be class %")                     \
  T(InvalidPrivateBrandReinitialization,                                       \
    "Cannot initialize private methods of class % twice on the same object")   \
  T(InvalidPrivateFieldReinitialization,                                       \
    "Cannot initialize % twice on the same object")                            \
  T(InvalidPrivateFieldResolution,                                             \
    "Private field '%' must be declared in an enclosing class")                \
  T(InvalidPrivateMemberRead,                                                  \
    "Cannot read private member % from an object whose class did not declare " \
    "it")                                                                      \
  T(InvalidPrivateMemberWrite,                                                 \
    "Cannot write private member % to an object whose class did not declare "  \
    "it")                                                                      \
  T(InvalidPrivateMethodWrite, "Private method '%' is not writable")           \
  T(InvalidPrivateGetterAccess, "'%' was defined without a getter")            \
  T(InvalidPrivateSetterAccess, "'%' was defined without a setter")            \
  T(InvalidSizeValue, "'%' is an invalid size")                                \
  T(InvalidUnusedPrivateStaticMethodAccessedByDebugger,                        \
    "Unused static private method '%' cannot be accessed at debug time")       \
  T(InvalidUsingInForInLoop, "Invalid 'using' in for-in loop")                 \
  T(JsonParseUnexpectedEOS, "Unexpected end of JSON input")                    \
  T(JsonParseUnexpectedTokenNumber,                                            \
    "Unexpected number in JSON at position % (line % column %)")               \
  T(JsonParseUnexpectedTokenString,                                            \
    "Unexpected string in JSON at position % (line % column %)")               \
  T(JsonParseUnterminatedString,                                               \
    "Unterminated string in JSON at position % (line % column %)")             \
  T(JsonParseExpectedPropNameOrRBrace,                                         \
    "Expected property name or '}' in JSON at position % (line % column %)")   \
  T(JsonParseExpectedCommaOrRBrack,                                            \
    "Expected ',' or ']' after array element in JSON at position % (line % "   \
    "column %)")                                                               \
  T(JsonParseExpectedCommaOrRBrace,                                            \
    "Expected ',' or '}' after property value in JSON at position "            \
    "% (line % column %)")                                                     \
  T(JsonParseExpectedDoubleQuotedPropertyName,                                 \
    "Expected double-quoted property name in JSON at position % (line % "      \
    "column %)")                                                               \
  T(JsonParseExponentPartMissingNumber,                                        \
    "Exponent part is missing a number in JSON at position % (line % column "  \
    "%)")                                                                      \
  T(JsonParseExpectedColonAfterPropertyName,                                   \
    "Expected ':' after property name in JSON at position % (line % column "   \
    "%)")                                                                      \
  T(JsonParseUnterminatedFractionalNumber,                                     \
    "Unterminated fractional number in JSON at position % (line % column %)")  \
  T(JsonParseUnexpectedNonWhiteSpaceCharacter,                                 \
    "Unexpected non-whitespace character after JSON at position "              \
    "% (line % column %)")                                                     \
  T(JsonParseBadEscapedCharacter,                                              \
    "Bad escaped character in JSON at position % (line % column %)")           \
  T(JsonParseBadControlCharacter,                                              \
    "Bad control character in string literal in JSON at position % (line % "   \
    "column %)")                                                               \
  T(JsonParseBadUnicodeEscape,                                                 \
    "Bad Unicode escape in JSON at position % (line % column %)")              \
  T(JsonParseNoNumberAfterMinusSign,                                           \
    "No number after minus sign in JSON at position % (line % column %)")      \
  T(JsonParseShortString, "\"%\" is not valid JSON")                           \
  T(JsonParseUnexpectedTokenShortString,                                       \
    "Unexpected token '%', \"%\" is not valid JSON")                           \
  T(JsonParseUnexpectedTokenSurroundStringWithContext,                         \
    "Unexpected token '%', ...\"%\"... is not valid JSON")                     \
  T(JsonParseUnexpectedTokenEndStringWithContext,                              \
    "Unexpected token '%', ...\"%\" is not valid JSON")                        \
  T(JsonParseUnexpectedTokenStartStringWithContext,                            \
    "Unexpected token '%', \"%\"... is not valid JSON")                        \
  T(LabelRedeclaration, "Label '%' has already been declared")                 \
  T(LabelledFunctionDeclaration,                                               \
    "Labelled function declaration not allowed as the body of a control flow " \
    "structure")                                                               \
  T(MalformedArrowFunParamList, "Malformed arrow function parameter list")     \
  T(MalformedRegExp, "Invalid regular expression: /%/%: %")                    \
  T(MalformedRegExpFlags, "Invalid regular expression flags")                  \
  T(ModuleExportUndefined, "Export '%' is not defined in module")              \
  T(MissingFunctionName, "Function statements require a function name")        \
  T(HtmlCommentInModule, "HTML comments are not allowed in modules")           \
  T(MultipleDefaultsInSwitch,                                                  \
    "More than one default clause in switch statement")                        \
  T(NewlineAfterThrow, "Illegal newline after throw")                          \
  T(NoCatchOrFinally, "Missing catch or finally after try")                    \
  T(ParamAfterRest, "Rest parameter must be last formal parameter")            \
  T(FlattenPastSafeLength,                                                     \
    "Flattening % elements on an array-like of length % "                      \
    "is disallowed, as the total surpasses 2**53-1")                           \
  T(PushPastSafeLength,                                                        \
    "Pushing % elements on an array-like of length % "                         \
    "is disallowed, as the total surpasses 2**53-1")                           \
  T(ElementAfterRest, "Rest element must be last element")                     \
  T(BadSetterRestParameter,                                                    \
    "Setter function argument must not be a rest parameter")                   \
  T(ParamDupe, "Duplicate parameter name not allowed in this context")         \
  T(ArgStringTerminatesParametersEarly,                                        \
    "Arg string terminates parameters early")                                  \
  T(UnexpectedEndOfArgString, "Unexpected end of arg string")                  \
  T(RestDefaultInitializer,                                                    \
    "Rest parameter may not have a default initializer")                       \
  T(RuntimeWrongNumArgs, "Runtime function given wrong number of arguments")   \
  T(SuperNotCalled,                                                            \
    "Must call super constructor in derived class before accessing 'this' or " \
    "returning from derived constructor")                                      \
  T(SingleFunctionLiteral, "Single function literal required")                 \
  T(SloppyFunction,                                                            \
    "In non-strict mode code, functions can only be declared at top level, "   \
    "inside a block, or as the body of an if statement.")                      \
  T(SpeciesNotConstructor,                                                     \
    "object.constructor[Symbol.species] is not a constructor")                 \
  T(StrictDelete, "Delete of an unqualified identifier in strict mode.")       \
  T(StrictEvalArguments, "Unexpected eval or arguments in strict mode")        \
  T(StrictFunction,                                                            \
    "In strict mode code, functions can only be declared at top level or "     \
    "inside a block.")                                                         \
  T(StrictOctalLiteral, "Octal literals are not allowed in strict mode.")      \
  T(StrictDecimalWithLeadingZero,                                              \
    "Decimals with leading zeros are not allowed in strict mode.")             \
  T(StrictOctalEscape,                                                         \
    "Octal escape sequences are not allowed in strict mode.")                  \
  T(Strict8Or9Escape, "\\8 and \\9 are not allowed in strict mode.")           \
  T(StrictWith, "Strict mode code may not include a with statement")           \
  T(TemplateOctalLiteral,                                                      \
    "Octal escape sequences are not allowed in template strings.")             \
  T(Template8Or9Escape, "\\8 and \\9 are not allowed in template strings.")    \
  T(ThisFormalParameter, "'this' is not a valid formal parameter name")        \
  T(AwaitBindingIdentifier,                                                    \
    "'await' is not a valid identifier name in an async function")             \
  T(AwaitExpressionFormalParameter,                                            \
    "Illegal await-expression in formal parameters of async function")         \
  T(TooManyArguments,                                                          \
    "Too many arguments in function call (only 65535 allowed)")                \
  T(TooManyParameters,                                                         \
    "Too many parameters in function definition (only 65534 allowed)")         \
  T(TooManyProperties, "Too many properties to enumerate")                     \
  T(TooManySpreads,                                                            \
    "Literal containing too many nested spreads (up to 65534 allowed)")        \
  T(TooManyVariables, "Too many variables declared (only 4194303 allowed)")    \
  T(TooManyElementsInPromiseCombinator,                                        \
    "Too many elements passed to Promise.%")                                   \
  T(TypedArrayTooShort,                                                        \
    "Derived TypedArray constructor created an array which was too small")     \
  T(UnexpectedEOS, "Unexpected end of input")                                  \
  T(UnexpectedPrivateField, "Unexpected private field")                        \
  T(UnexpectedReserved, "Unexpected reserved word")                            \
  T(UnexpectedStrictReserved, "Unexpected strict mode reserved word")          \
  T(UnexpectedSuper, "'super' keyword unexpected here")                        \
  T(UnexpectedNewTarget, "new.target expression is not allowed here")          \
  T(UnexpectedTemplateString, "Unexpected template string")                    \
  T(UnexpectedToken, "Unexpected token '%'")                                   \
  T(UnexpectedTokenUnaryExponentiation,                                        \
    "Unary operator used immediately before exponentiation expression. "       \
    "Parenthesis must be used to disambiguate operator precedence")            \
  T(UnexpectedTokenIdentifier, "Unexpected identifier '%'")                    \
  T(UnexpectedTokenNumber, "Unexpected number")                                \
  T(UnexpectedTokenString, "Unexpected string")                                \
  T(UnexpectedTokenRegExp, "Unexpected regular expression")                    \
  T(UnexpectedLexicalDeclaration,                                              \
    "Lexical declaration cannot appear in a single-statement context")         \
  T(UnknownLabel, "Undefined label '%'")                                       \
  T(UnresolvableExport,                                                        \
    "The requested module '%' does not provide an export named '%'")           \
  T(UnterminatedArgList, "missing ) after argument list")                      \
  T(UnterminatedRegExp, "Invalid regular expression: missing /")               \
  T(UnterminatedTemplate, "Unterminated template literal")                     \
  T(UnterminatedTemplateExpr, "Missing } in template expression")              \
  T(FoundNonCallableHasInstance, "Found non-callable @@hasInstance")           \
  T(InvalidHexEscapeSequence, "Invalid hexadecimal escape sequence")           \
  T(InvalidUnicodeEscapeSequence, "Invalid Unicode escape sequence")           \
  T(UndefinedUnicodeCodePoint, "Undefined Unicode code-point")                 \
  T(YieldInParameter, "Yield expression not allowed in formal parameter")      \
  /* EvalError */                                                              \
  T(CodeGenFromStrings, "%")                                                   \
  T(NoSideEffectDebugEvaluate, "Possible side-effect in debug-evaluate")       \
  /* URIError */                                                               \
  T(URIMalformed, "URI malformed")                                             \
  /* Wasm errors (currently Error) */                                          \
  T(WasmTrapUnreachable, "unreachable")                                        \
  T(WasmTrapMemOutOfBounds, "memory access out of bounds")                     \
  T(WasmTrapUnalignedAccess, "operation does not support unaligned accesses")  \
  T(WasmTrapDivByZero, "divide by zero")                                       \
  T(WasmTrapDivUnrepresentable, "divide result unrepresentable")               \
  T(WasmTrapRemByZero, "remainder by zero")                                    \
  T(WasmTrapFloatUnrepresentable, "float unrepresentable in integer range")    \
  T(WasmTrapTableOutOfBounds, "table index is out of bounds")                  \
  T(WasmTrapFuncSigMismatch, "null function or function signature mismatch")   \
  T(WasmTrapMultiReturnLengthMismatch, "multi-return length mismatch")         \
  T(WasmTrapJSTypeError, "type incompatibility when transforming from/to JS")  \
  T(WasmTrapDataSegmentOutOfBounds, "data segment out of bounds")              \
  T(WasmTrapElementSegmentOutOfBounds, "element segment out of bounds")        \
  T(WasmTrapRethrowNull, "rethrowing null value")                              \
  T(WasmTrapNullDereference, "dereferencing a null pointer")                   \
  T(WasmTrapIllegalCast, "illegal cast")                                       \
  T(WasmTrapArrayOutOfBounds, "array element access out of bounds")            \
  T(WasmTrapArrayTooLarge, "requested new array is too large")                 \
  T(WasmTrapStringInvalidUtf8, "invalid UTF-8 string")                         \
  T(WasmTrapStringInvalidWtf8, "invalid WTF-8 string")                         \
  T(WasmTrapStringOffsetOutOfBounds, "string offset out of bounds")            \
  T(WasmTrapBadSuspender,                                                      \
    "attempting to suspend without a WebAssembly.promising export")            \
  T(WasmTrapStringIsolatedSurrogate,                                           \
    "Failed to encode string as UTF-8: contains unpaired surrogate")           \
  T(WasmTrapSuspendJSFrames, "trying to suspend JS frames")                    \
  T(WasmExceptionError, "wasm exception")                                      \
  T(WasmObjectsAreOpaque, "WebAssembly objects are opaque")                    \
  /* Asm.js validation related */                                              \
  T(AsmJsInvalid, "Invalid asm.js: %")                                         \
  T(AsmJsCompiled, "Converted asm.js to WebAssembly: %")                       \
  T(AsmJsInstantiated, "Instantiated asm.js: %")                               \
  T(AsmJsLinkingFailed, "Linking failure in asm.js: %")                        \
  /* DataCloneError messages */                                                \
  T(DataCloneError, "% could not be cloned.")                                  \
  T(DataCloneErrorOutOfMemory, "Data cannot be cloned, out of memory.")        \
  T(DataCloneErrorDetachedArrayBuffer,                                         \
    "An ArrayBuffer is detached and could not be cloned.")                     \
  T(DataCloneErrorNonDetachableArrayBuffer,                                    \
    "ArrayBuffer is not detachable and could not be cloned.")                  \
  T(DataCloneErrorSharedArrayBufferTransferred,                                \
    "A SharedArrayBuffer could not be cloned. SharedArrayBuffer must not be "  \
    "transferred.")                                                            \
  T(DataCloneDeserializationError, "Unable to deserialize cloned data.")       \
  T(DataCloneDeserializationVersionError,                                      \
    "Unable to deserialize cloned data due to invalid or unsupported "         \
    "version.")                                                                \
  /* Builtins-Trace Errors */                                                  \
  T(TraceEventCategoryError, "Trace event category must be a string.")         \
  T(TraceEventNameError, "Trace event name must be a string.")                 \
  T(TraceEventNameLengthError,                                                 \
    "Trace event name must not be an empty string.")                           \
  T(TraceEventPhaseError, "Trace event phase must be a number.")               \
  T(TraceEventIDError, "Trace event id must be a number.")                     \
  /* Weak refs */                                                              \
  T(InvalidWeakRefsUnregisterToken, "Invalid unregisterToken ('%')")           \
  T(WeakRefsCleanupMustBeCallable,                                             \
    "FinalizationRegistry: cleanup must be callable")                          \
  T(InvalidWeakRefsRegisterTarget,                                             \
    "FinalizationRegistry.prototype.register: invalid target")                 \
  T(WeakRefsRegisterTargetAndHoldingsMustNotBeSame,                            \
    "FinalizationRegistry.prototype.register: target and holdings must not "   \
    "be same")                                                                 \
  T(InvalidWeakRefsWeakRefConstructorTarget, "WeakRef: invalid target")        \
  T(OptionalChainingNoNew, "Invalid optional chain from new expression")       \
  T(OptionalChainingNoSuper, "Invalid optional chain from super property")     \
  T(OptionalChainingNoTemplate, "Invalid tagged template on optional chain")   \
  /* AggregateError */                                                         \
  T(AllPromisesRejected, "All promises were rejected")                         \
  T(CannotDeepFreezeObject, "Cannot DeepFreeze object of type %")              \
  T(CannotDeepFreezeValue, "Cannot DeepFreeze non-const value %")              \
  /* SuppressedError */                                                        \
  T(SuppressedErrorDuringDisposal, "An error was suppressed during disposal")  \
  T(ExpectAnObjectWithUsing,                                                   \
    "An object is expected with `using` "                                      \
    "declarations")

enum class MessageTemplate {
#define TEMPLATE(NAME, STRING) k##NAME,
  MESSAGE_TEMPLATES(TEMPLATE)
#undef TEMPLATE
      kMessageCount
};

inline MessageTemplate MessageTemplateFromInt(int message_id) {
  DCHECK_LT(static_cast<unsigned>(message_id),
            static_cast<unsigned>(MessageTemplate::kMessageCount));
  return static_cast<MessageTemplate>(message_id);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_COMMON_MESSAGE_TEMPLATE_H_

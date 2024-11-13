// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INIT_HEAP_SYMBOLS_H_
#define V8_INIT_HEAP_SYMBOLS_H_

#ifdef V8_INTL_SUPPORT
#define INTERNALIZED_STRING_LIST_GENERATOR_INTL(V, _)               \
  V(_, adoptText_string, "adoptText")                               \
  V(_, approximatelySign_string, "approximatelySign")               \
  V(_, baseName_string, "baseName")                                 \
  V(_, accounting_string, "accounting")                             \
  V(_, breakType_string, "breakType")                               \
  V(_, calendars_string, "calendars")                               \
  V(_, cardinal_string, "cardinal")                                 \
  V(_, caseFirst_string, "caseFirst")                               \
  V(_, ceil_string, "ceil")                                         \
  V(_, compare_string, "compare")                                   \
  V(_, collation_string, "collation")                               \
  V(_, collations_string, "collations")                             \
  V(_, compact_string, "compact")                                   \
  V(_, compactDisplay_string, "compactDisplay")                     \
  V(_, currency_string, "currency")                                 \
  V(_, currencyDisplay_string, "currencyDisplay")                   \
  V(_, currencySign_string, "currencySign")                         \
  V(_, dateStyle_string, "dateStyle")                               \
  V(_, dateTimeField_string, "dateTimeField")                       \
  V(_, dayPeriod_string, "dayPeriod")                               \
  V(_, daysDisplay_string, "daysDisplay")                           \
  V(_, decimal_string, "decimal")                                   \
  V(_, dialect_string, "dialect")                                   \
  V(_, digital_string, "digital")                                   \
  V(_, direction_string, "direction")                               \
  V(_, endRange_string, "endRange")                                 \
  V(_, engineering_string, "engineering")                           \
  V(_, exceptZero_string, "exceptZero")                             \
  V(_, expand_string, "expand")                                     \
  V(_, exponentInteger_string, "exponentInteger")                   \
  V(_, exponentMinusSign_string, "exponentMinusSign")               \
  V(_, exponentSeparator_string, "exponentSeparator")               \
  V(_, fallback_string, "fallback")                                 \
  V(_, first_string, "first")                                       \
  V(_, firstDay_string, "firstDay")                                 \
  V(_, firstDayOfWeek_string, "firstDayOfWeek")                     \
  V(_, floor_string, "floor")                                       \
  V(_, format_string, "format")                                     \
  V(_, fraction_string, "fraction")                                 \
  V(_, fractionalDigits_string, "fractionalDigits")                 \
  V(_, fractionalSecond_string, "fractionalSecond")                 \
  V(_, full_string, "full")                                         \
  V(_, granularity_string, "granularity")                           \
  V(_, grapheme_string, "grapheme")                                 \
  V(_, group_string, "group")                                       \
  V(_, h11_string, "h11")                                           \
  V(_, h12_string, "h12")                                           \
  V(_, h23_string, "h23")                                           \
  V(_, h24_string, "h24")                                           \
  V(_, halfCeil_string, "halfCeil")                                 \
  V(_, halfEven_string, "halfEven")                                 \
  V(_, halfExpand_string, "halfExpand")                             \
  V(_, halfFloor_string, "halfFloor")                               \
  V(_, halfTrunc_string, "halfTrunc")                               \
  V(_, hour12_string, "hour12")                                     \
  V(_, hourCycle_string, "hourCycle")                               \
  V(_, hourCycles_string, "hourCycles")                             \
  V(_, hoursDisplay_string, "hoursDisplay")                         \
  V(_, ideo_string, "ideo")                                         \
  V(_, ignorePunctuation_string, "ignorePunctuation")               \
  V(_, Invalid_Date_string, "Invalid Date")                         \
  V(_, integer_string, "integer")                                   \
  V(_, isWordLike_string, "isWordLike")                             \
  V(_, kana_string, "kana")                                         \
  V(_, language_string, "language")                                 \
  V(_, languageDisplay_string, "languageDisplay")                   \
  V(_, lessPrecision_string, "lessPrecision")                       \
  V(_, letter_string, "letter")                                     \
  V(_, list_string, "list")                                         \
  V(_, literal_string, "literal")                                   \
  V(_, locale_string, "locale")                                     \
  V(_, loose_string, "loose")                                       \
  V(_, lower_string, "lower")                                       \
  V(_, ltr_string, "ltr")                                           \
  V(_, maximumFractionDigits_string, "maximumFractionDigits")       \
  V(_, maximumSignificantDigits_string, "maximumSignificantDigits") \
  V(_, microsecondsDisplay_string, "microsecondsDisplay")           \
  V(_, millisecondsDisplay_string, "millisecondsDisplay")           \
  V(_, min2_string, "min2")                                         \
  V(_, minimalDays_string, "minimalDays")                           \
  V(_, minimumFractionDigits_string, "minimumFractionDigits")       \
  V(_, minimumIntegerDigits_string, "minimumIntegerDigits")         \
  V(_, minimumSignificantDigits_string, "minimumSignificantDigits") \
  V(_, minus_0, "-0")                                               \
  V(_, minusSign_string, "minusSign")                               \
  V(_, minutesDisplay_string, "minutesDisplay")                     \
  V(_, monthsDisplay_string, "monthsDisplay")                       \
  V(_, morePrecision_string, "morePrecision")                       \
  V(_, nan_string, "nan")                                           \
  V(_, nanosecondsDisplay_string, "nanosecondsDisplay")             \
  V(_, narrowSymbol_string, "narrowSymbol")                         \
  V(_, negative_string, "negative")                                 \
  V(_, never_string, "never")                                       \
  V(_, none_string, "none")                                         \
  V(_, notation_string, "notation")                                 \
  V(_, normal_string, "normal")                                     \
  V(_, numberingSystem_string, "numberingSystem")                   \
  V(_, numberingSystems_string, "numberingSystems")                 \
  V(_, numeric_string, "numeric")                                   \
  V(_, ordinal_string, "ordinal")                                   \
  V(_, percentSign_string, "percentSign")                           \
  V(_, plusSign_string, "plusSign")                                 \
  V(_, quarter_string, "quarter")                                   \
  V(_, region_string, "region")                                     \
  V(_, relatedYear_string, "relatedYear")                           \
  V(_, roundingMode_string, "roundingMode")                         \
  V(_, roundingPriority_string, "roundingPriority")                 \
  V(_, rtl_string, "rtl")                                           \
  V(_, scientific_string, "scientific")                             \
  V(_, secondsDisplay_string, "secondsDisplay")                     \
  V(_, segment_string, "segment")                                   \
  V(_, SegmentIterator_string, "Segment Iterator")                  \
  V(_, Segments_string, "Segments")                                 \
  V(_, sensitivity_string, "sensitivity")                           \
  V(_, sep_string, "sep")                                           \
  V(_, shared_string, "shared")                                     \
  V(_, signDisplay_string, "signDisplay")                           \
  V(_, standard_string, "standard")                                 \
  V(_, startRange_string, "startRange")                             \
  V(_, strict_string, "strict")                                     \
  V(_, stripIfInteger_string, "stripIfInteger")                     \
  V(_, style_string, "style")                                       \
  V(_, term_string, "term")                                         \
  V(_, textInfo_string, "textInfo")                                 \
  V(_, timeStyle_string, "timeStyle")                               \
  V(_, timeZones_string, "timeZones")                               \
  V(_, timeZoneName_string, "timeZoneName")                         \
  V(_, trailingZeroDisplay_string, "trailingZeroDisplay")           \
  V(_, trunc_string, "trunc")                                       \
  V(_, two_digit_string, "2-digit")                                 \
  V(_, type_string, "type")                                         \
  V(_, unknown_string, "unknown")                                   \
  V(_, upper_string, "upper")                                       \
  V(_, usage_string, "usage")                                       \
  V(_, useGrouping_string, "useGrouping")                           \
  V(_, unitDisplay_string, "unitDisplay")                           \
  V(_, weekday_string, "weekday")                                   \
  V(_, weekend_string, "weekend")                                   \
  V(_, weeksDisplay_string, "weeksDisplay")                         \
  V(_, weekInfo_string, "weekInfo")                                 \
  V(_, yearName_string, "yearName")                                 \
  V(_, yearsDisplay_string, "yearsDisplay")
#else  // V8_INTL_SUPPORT
#define INTERNALIZED_STRING_LIST_GENERATOR_INTL(V, _)
#endif  // V8_INTL_SUPPORT

// Internalized strings to be allocated early on the read only heap and early in
// the roots table. Used to give this string a RootIndex < 32.
#define EXTRA_IMPORTANT_INTERNALIZED_STRING_LIST_GENERATOR(V, _) \
  V(_, empty_string, "")

// Internalized strings to be allocated early on the read only heap
#define IMPORTANT_INTERNALIZED_STRING_LIST_GENERATOR(V, _) \
  V(_, length_string, "length")                            \
  V(_, prototype_string, "prototype")                      \
  V(_, name_string, "name")                                \
  V(_, enumerable_string, "enumerable")                    \
  V(_, configurable_string, "configurable")                \
  V(_, value_string, "value")                              \
  V(_, writable_string, "writable")

#define NOT_IMPORTANT_INTERNALIZED_STRING_LIST_GENERATOR(V, _)                \
  INTERNALIZED_STRING_LIST_GENERATOR_INTL(V, _)                               \
  V(_, add_string, "add")                                                     \
  V(_, AggregateError_string, "AggregateError")                               \
  V(_, always_string, "always")                                               \
  V(_, anonymous_string, "anonymous")                                         \
  V(_, apply_string, "apply")                                                 \
  V(_, Arguments_string, "Arguments")                                         \
  V(_, arguments_string, "arguments")                                         \
  V(_, arguments_to_string, "[object Arguments]")                             \
  V(_, Array_string, "Array")                                                 \
  V(_, array_to_string, "[object Array]")                                     \
  V(_, ArrayBuffer_string, "ArrayBuffer")                                     \
  V(_, ArrayIterator_string, "Array Iterator")                                \
  V(_, as_string, "as")                                                       \
  V(_, assert_string, "assert")                                               \
  V(_, async_string, "async")                                                 \
  V(_, AtomicsCondition_string, "Atomics.Condition")                          \
  V(_, AtomicsMutex_string, "Atomics.Mutex")                                  \
  V(_, auto_string, "auto")                                                   \
  V(_, BigInt_string, "BigInt")                                               \
  V(_, bigint_string, "bigint")                                               \
  V(_, BigInt64Array_string, "BigInt64Array")                                 \
  V(_, BigUint64Array_string, "BigUint64Array")                               \
  V(_, bind_string, "bind")                                                   \
  V(_, blank_string, "blank")                                                 \
  V(_, Boolean_string, "Boolean")                                             \
  V(_, boolean_string, "boolean")                                             \
  V(_, boolean_to_string, "[object Boolean]")                                 \
  V(_, bound__string, "bound ")                                               \
  V(_, buffer_string, "buffer")                                               \
  V(_, byte_length_string, "byteLength")                                      \
  V(_, byte_offset_string, "byteOffset")                                      \
  V(_, CompileError_string, "CompileError")                                   \
  V(_, calendar_string, "calendar")                                           \
  V(_, callee_string, "callee")                                               \
  V(_, caller_string, "caller")                                               \
  V(_, cause_string, "cause")                                                 \
  V(_, character_string, "character")                                         \
  V(_, code_string, "code")                                                   \
  V(_, column_string, "column")                                               \
  V(_, computed_string, "<computed>")                                         \
  V(_, conjunction_string, "conjunction")                                     \
  V(_, console_string, "console")                                             \
  V(_, constrain_string, "constrain")                                         \
  V(_, construct_string, "construct")                                         \
  V(_, current_string, "current")                                             \
  V(_, Date_string, "Date")                                                   \
  V(_, date_to_string, "[object Date]")                                       \
  V(_, dateAdd_string, "dateAdd")                                             \
  V(_, dateFromFields_string, "dateFromFields")                               \
  V(_, dateUntil_string, "dateUntil")                                         \
  V(_, day_string, "day")                                                     \
  V(_, dayOfWeek_string, "dayOfWeek")                                         \
  V(_, dayOfYear_string, "dayOfYear")                                         \
  V(_, days_string, "days")                                                   \
  V(_, daysInMonth_string, "daysInMonth")                                     \
  V(_, daysInWeek_string, "daysInWeek")                                       \
  V(_, daysInYear_string, "daysInYear")                                       \
  V(_, default_string, "default")                                             \
  V(_, defineProperty_string, "defineProperty")                               \
  V(_, deleteProperty_string, "deleteProperty")                               \
  V(_, detached_string, "detached")                                           \
  V(_, disjunction_string, "disjunction")                                     \
  V(_, done_string, "done")                                                   \
  V(_, dot_brand_string, ".brand")                                            \
  V(_, dot_catch_string, ".catch")                                            \
  V(_, dot_default_string, ".default")                                        \
  V(_, dot_for_string, ".for")                                                \
  V(_, dot_generator_object_string, ".generator_object")                      \
  V(_, dot_home_object_string, ".home_object")                                \
  V(_, dot_new_target_string, ".new.target")                                  \
  V(_, dot_result_string, ".result")                                          \
  V(_, dot_repl_result_string, ".repl_result")                                \
  V(_, dot_static_home_object_string, ".static_home_object")                  \
  V(_, dot_string, ".")                                                       \
  V(_, dot_switch_tag_string, ".switch_tag")                                  \
  V(_, dotAll_string, "dotAll")                                               \
  V(_, Error_string, "Error")                                                 \
  V(_, EvalError_string, "EvalError")                                         \
  V(_, element_string, "element")                                             \
  V(_, epochMicroseconds_string, "epochMicroseconds")                         \
  V(_, epochMilliseconds_string, "epochMilliseconds")                         \
  V(_, epochNanoseconds_string, "epochNanoseconds")                           \
  V(_, epochSeconds_string, "epochSeconds")                                   \
  V(_, era_string, "era")                                                     \
  V(_, eraYear_string, "eraYear")                                             \
  V(_, error_string, "error")                                                 \
  V(_, errors_string, "errors")                                               \
  V(_, error_to_string, "[object Error]")                                     \
  V(_, eval_string, "eval")                                                   \
  V(_, exception_string, "exception")                                         \
  V(_, exec_string, "exec")                                                   \
  V(_, false_string, "false")                                                 \
  V(_, fields_string, "fields")                                               \
  V(_, FinalizationRegistry_string, "FinalizationRegistry")                   \
  V(_, flags_string, "flags")                                                 \
  V(_, Float16Array_string, "Float16Array")                                   \
  V(_, Float32Array_string, "Float32Array")                                   \
  V(_, Float64Array_string, "Float64Array")                                   \
  V(_, fractionalSecondDigits_string, "fractionalSecondDigits")               \
  V(_, from_string, "from")                                                   \
  V(_, Function_string, "Function")                                           \
  V(_, function_native_code_string, "function () { [native code] }")          \
  V(_, function_string, "function")                                           \
  V(_, function_to_string, "[object Function]")                               \
  V(_, Generator_string, "Generator")                                         \
  V(_, get_space_string, "get ")                                              \
  V(_, get_string, "get")                                                     \
  V(_, getOffsetNanosecondsFor_string, "getOffsetNanosecondsFor")             \
  V(_, getOwnPropertyDescriptor_string, "getOwnPropertyDescriptor")           \
  V(_, getPossibleInstantsFor_string, "getPossibleInstantsFor")               \
  V(_, getPrototypeOf_string, "getPrototypeOf")                               \
  V(_, global_string, "global")                                               \
  V(_, globalThis_string, "globalThis")                                       \
  V(_, groups_string, "groups")                                               \
  V(_, growable_string, "growable")                                           \
  V(_, has_string, "has")                                                     \
  V(_, hasIndices_string, "hasIndices")                                       \
  V(_, hour_string, "hour")                                                   \
  V(_, hours_string, "hours")                                                 \
  V(_, hoursInDay_string, "hoursInDay")                                       \
  V(_, ignoreCase_string, "ignoreCase")                                       \
  V(_, id_string, "id")                                                       \
  V(_, illegal_access_string, "illegal access")                               \
  V(_, illegal_argument_string, "illegal argument")                           \
  V(_, inLeapYear_string, "inLeapYear")                                       \
  V(_, index_string, "index")                                                 \
  V(_, indices_string, "indices")                                             \
  V(_, Infinity_string, "Infinity")                                           \
  V(_, infinity_string, "infinity")                                           \
  V(_, input_string, "input")                                                 \
  V(_, instance_members_initializer_string, "<instance_members_initializer>") \
  V(_, Int16Array_string, "Int16Array")                                       \
  V(_, Int32Array_string, "Int32Array")                                       \
  V(_, Int8Array_string, "Int8Array")                                         \
  V(_, isExtensible_string, "isExtensible")                                   \
  V(_, iso8601_string, "iso8601")                                             \
  V(_, isoDay_string, "isoDay")                                               \
  V(_, isoHour_string, "isoHour")                                             \
  V(_, isoMicrosecond_string, "isoMicrosecond")                               \
  V(_, isoMillisecond_string, "isoMillisecond")                               \
  V(_, isoMinute_string, "isoMinute")                                         \
  V(_, isoMonth_string, "isoMonth")                                           \
  V(_, isoNanosecond_string, "isoNanosecond")                                 \
  V(_, isoSecond_string, "isoSecond")                                         \
  V(_, isoYear_string, "isoYear")                                             \
  V(_, Iterator_string, "Iterator")                                           \
  V(_, jsMemoryEstimate_string, "jsMemoryEstimate")                           \
  V(_, jsMemoryRange_string, "jsMemoryRange")                                 \
  V(_, keys_string, "keys")                                                   \
  V(_, largestUnit_string, "largestUnit")                                     \
  V(_, lastIndex_string, "lastIndex")                                         \
  V(_, let_string, "let")                                                     \
  V(_, line_string, "line")                                                   \
  V(_, linear_string, "linear")                                               \
  V(_, LinkError_string, "LinkError")                                         \
  V(_, long_string, "long")                                                   \
  V(_, Map_string, "Map")                                                     \
  V(_, MapIterator_string, "Map Iterator")                                    \
  V(_, max_byte_length_string, "maxByteLength")                               \
  V(_, medium_string, "medium")                                               \
  V(_, mergeFields_string, "mergeFields")                                     \
  V(_, message_string, "message")                                             \
  V(_, meta_string, "meta")                                                   \
  V(_, minus_Infinity_string, "-Infinity")                                    \
  V(_, microsecond_string, "microsecond")                                     \
  V(_, microseconds_string, "microseconds")                                   \
  V(_, millisecond_string, "millisecond")                                     \
  V(_, milliseconds_string, "milliseconds")                                   \
  V(_, minute_string, "minute")                                               \
  V(_, minutes_string, "minutes")                                             \
  V(_, Module_string, "Module")                                               \
  V(_, month_string, "month")                                                 \
  V(_, monthDayFromFields_string, "monthDayFromFields")                       \
  V(_, months_string, "months")                                               \
  V(_, monthsInYear_string, "monthsInYear")                                   \
  V(_, monthCode_string, "monthCode")                                         \
  V(_, multiline_string, "multiline")                                         \
  V(_, NaN_string, "NaN")                                                     \
  V(_, nanosecond_string, "nanosecond")                                       \
  V(_, nanoseconds_string, "nanoseconds")                                     \
  V(_, narrow_string, "narrow")                                               \
  V(_, native_string, "native")                                               \
  V(_, new_target_string, ".new.target")                                      \
  V(_, NFC_string, "NFC")                                                     \
  V(_, NFD_string, "NFD")                                                     \
  V(_, NFKC_string, "NFKC")                                                   \
  V(_, NFKD_string, "NFKD")                                                   \
  V(_, not_equal_string, "not-equal")                                         \
  V(_, null_string, "null")                                                   \
  V(_, null_to_string, "[object Null]")                                       \
  V(_, Number_string, "Number")                                               \
  V(_, number_string, "number")                                               \
  V(_, number_to_string, "[object Number]")                                   \
  V(_, Object_string, "Object")                                               \
  V(_, object_string, "object")                                               \
  V(_, object_to_string, "[object Object]")                                   \
  V(_, Object_prototype_string, "Object.prototype")                           \
  V(_, offset_string, "offset")                                               \
  V(_, offsetNanoseconds_string, "offsetNanoseconds")                         \
  V(_, ok_string, "ok")                                                       \
  V(_, one_string, "1")                                                       \
  V(_, other_string, "other")                                                 \
  V(_, overflow_string, "overflow")                                           \
  V(_, ownKeys_string, "ownKeys")                                             \
  V(_, percent_string, "percent")                                             \
  V(_, plainDate_string, "plainDate")                                         \
  V(_, plainTime_string, "plainTime")                                         \
  V(_, position_string, "position")                                           \
  V(_, preventExtensions_string, "preventExtensions")                         \
  V(_, private_constructor_string, "#constructor")                            \
  V(_, Promise_string, "Promise")                                             \
  V(_, promise_string, "promise")                                             \
  V(_, proto_string, "__proto__")                                             \
  V(_, proxy_string, "proxy")                                                 \
  V(_, Proxy_string, "Proxy")                                                 \
  V(_, query_colon_string, "(?:)")                                            \
  V(_, RangeError_string, "RangeError")                                       \
  V(_, raw_json_string, "rawJSON")                                            \
  V(_, raw_string, "raw")                                                     \
  V(_, ReferenceError_string, "ReferenceError")                               \
  V(_, ReflectGet_string, "Reflect.get")                                      \
  V(_, ReflectHas_string, "Reflect.has")                                      \
  V(_, RegExp_string, "RegExp")                                               \
  V(_, regexp_to_string, "[object RegExp]")                                   \
  V(_, reject_string, "reject")                                               \
  V(_, relativeTo_string, "relativeTo")                                       \
  V(_, resizable_string, "resizable")                                         \
  V(_, ResizableArrayBuffer_string, "ResizableArrayBuffer")                   \
  V(_, return_string, "return")                                               \
  V(_, revoke_string, "revoke")                                               \
  V(_, roundingIncrement_string, "roundingIncrement")                         \
  V(_, RuntimeError_string, "RuntimeError")                                   \
  V(_, WebAssemblyException_string, "WebAssembly.Exception")                  \
  V(_, WebAssemblyModule_string, "WebAssembly.Module")                        \
  V(_, Script_string, "Script")                                               \
  V(_, script_string, "script")                                               \
  V(_, second_string, "second")                                               \
  V(_, seconds_string, "seconds")                                             \
  V(_, short_string, "short")                                                 \
  V(_, Set_string, "Set")                                                     \
  V(_, sentence_string, "sentence")                                           \
  V(_, set_space_string, "set ")                                              \
  V(_, set_string, "set")                                                     \
  V(_, SetIterator_string, "Set Iterator")                                    \
  V(_, setPrototypeOf_string, "setPrototypeOf")                               \
  V(_, ShadowRealm_string, "ShadowRealm")                                     \
  V(_, SharedArray_string, "SharedArray")                                     \
  V(_, SharedArrayBuffer_string, "SharedArrayBuffer")                         \
  V(_, SharedStruct_string, "SharedStruct")                                   \
  V(_, sign_string, "sign")                                                   \
  V(_, size_string, "size")                                                   \
  V(_, smallestUnit_string, "smallestUnit")                                   \
  V(_, source_string, "source")                                               \
  V(_, sourceText_string, "sourceText")                                       \
  V(_, stack_string, "stack")                                                 \
  V(_, stackTraceLimit_string, "stackTraceLimit")                             \
  V(_, static_initializer_string, "<static_initializer>")                     \
  V(_, sticky_string, "sticky")                                               \
  V(_, String_string, "String")                                               \
  V(_, string_string, "string")                                               \
  V(_, string_to_string, "[object String]")                                   \
  V(_, suppressed_string, "suppressed")                                       \
  V(_, SuppressedError_string, "SuppressedError")                             \
  V(_, Symbol_iterator_string, "Symbol.iterator")                             \
  V(_, Symbol_match_all_string, "Symbol.matchAll")                            \
  V(_, Symbol_replace_string, "Symbol.replace")                               \
  V(_, symbol_species_string, "[Symbol.species]")                             \
  V(_, Symbol_species_string, "Symbol.species")                               \
  V(_, Symbol_split_string, "Symbol.split")                                   \
  V(_, Symbol_string, "Symbol")                                               \
  V(_, symbol_string, "symbol")                                               \
  V(_, SyntaxError_string, "SyntaxError")                                     \
  V(_, target_string, "target")                                               \
  V(_, this_function_string, ".this_function")                                \
  V(_, this_string, "this")                                                   \
  V(_, throw_string, "throw")                                                 \
  V(_, timed_out_string, "timed-out")                                         \
  V(_, timeZone_string, "timeZone")                                           \
  V(_, toJSON_string, "toJSON")                                               \
  V(_, toString_string, "toString")                                           \
  V(_, true_string, "true")                                                   \
  V(_, total_string, "total")                                                 \
  V(_, TypeError_string, "TypeError")                                         \
  V(_, Uint16Array_string, "Uint16Array")                                     \
  V(_, Uint32Array_string, "Uint32Array")                                     \
  V(_, Uint8Array_string, "Uint8Array")                                       \
  V(_, Uint8ClampedArray_string, "Uint8ClampedArray")                         \
  V(_, undefined_string, "undefined")                                         \
  V(_, undefined_to_string, "[object Undefined]")                             \
  V(_, unicode_string, "unicode")                                             \
  V(_, unicodeSets_string, "unicodeSets")                                     \
  V(_, unit_string, "unit")                                                   \
  V(_, URIError_string, "URIError")                                           \
  V(_, UTC_string, "UTC")                                                     \
  V(_, WeakMap_string, "WeakMap")                                             \
  V(_, WeakRef_string, "WeakRef")                                             \
  V(_, WeakSet_string, "WeakSet")                                             \
  V(_, week_string, "week")                                                   \
  V(_, weeks_string, "weeks")                                                 \
  V(_, weekOfYear_string, "weekOfYear")                                       \
  V(_, with_string, "with")                                                   \
  V(_, word_string, "word")                                                   \
  V(_, yearMonthFromFields_string, "yearMonthFromFields")                     \
  V(_, year_string, "year")                                                   \
  V(_, years_string, "years")                                                 \
  V(_, zero_string, "0")

#define INTERNALIZED_STRING_LIST_GENERATOR(V, _)           \
  EXTRA_IMPORTANT_INTERNALIZED_STRING_LIST_GENERATOR(V, _) \
  IMPORTANT_INTERNALIZED_STRING_LIST_GENERATOR(V, _)       \
  NOT_IMPORTANT_INTERNALIZED_STRING_LIST_GENERATOR(V, _)

// Symbols to be allocated early on the read only heap
#define IMPORTANT_PRIVATE_SYMBOL_LIST_GENERATOR(V, _) \
  V(_, not_mapped_symbol)                             \
  V(_, uninitialized_symbol)                          \
  V(_, megamorphic_symbol)                            \
  V(_, elements_transition_symbol)                    \
  V(_, mega_dom_symbol)

#define NOT_IMPORTANT_PRIVATE_SYMBOL_LIST_GENERATOR(V, _) \
  V(_, array_buffer_wasm_memory_symbol)                   \
  V(_, call_site_info_symbol)                             \
  V(_, class_fields_symbol)                               \
  V(_, class_positions_symbol)                            \
  V(_, error_end_pos_symbol)                              \
  V(_, error_message_symbol)                              \
  V(_, error_script_symbol)                               \
  V(_, error_stack_symbol)                                \
  V(_, error_start_pos_symbol)                            \
  V(_, frozen_symbol)                                     \
  V(_, interpreter_trampoline_symbol)                     \
  V(_, native_context_index_symbol)                       \
  V(_, nonextensible_symbol)                              \
  V(_, promise_debug_message_symbol)                      \
  V(_, promise_forwarding_handler_symbol)                 \
  V(_, promise_handled_by_symbol)                         \
  V(_, promise_awaited_by_symbol)                         \
  V(_, regexp_result_names_symbol)                        \
  V(_, regexp_result_regexp_input_symbol)                 \
  V(_, regexp_result_regexp_last_index_symbol)            \
  V(_, sealed_symbol)                                     \
  V(_, shared_struct_map_elements_template_symbol)        \
  V(_, shared_struct_map_registry_key_symbol)             \
  V(_, strict_function_transition_symbol)                 \
  V(_, template_literal_function_literal_id_symbol)       \
  V(_, template_literal_slot_id_symbol)                   \
  V(_, wasm_exception_tag_symbol)                         \
  V(_, wasm_exception_values_symbol)                      \
  V(_, wasm_uncatchable_symbol)                           \
  V(_, wasm_wrapped_object_symbol)                        \
  V(_, wasm_debug_proxy_cache_symbol)                     \
  V(_, wasm_debug_proxy_names_symbol)

#define PRIVATE_SYMBOL_LIST_GENERATOR(V, _)     \
  IMPORTANT_PRIVATE_SYMBOL_LIST_GENERATOR(V, _) \
  NOT_IMPORTANT_PRIVATE_SYMBOL_LIST_GENERATOR(V, _)

#define PUBLIC_SYMBOL_LIST_GENERATOR(V, _)                \
  V(_, async_iterator_symbol, Symbol.asyncIterator)       \
  V(_, intl_fallback_symbol, IntlLegacyConstructedSymbol) \
  V(_, match_symbol, Symbol.match)                        \
  V(_, search_symbol, Symbol.search)                      \
  V(_, unscopables_symbol, Symbol.unscopables)            \
  V(_, dispose_symbol, Symbol.dispose)                    \
  V(_, async_dispose_symbol, Symbol.asyncDispose)

// Well-Known Symbols are "Public" symbols, which have a bit set which causes
// them to produce an undefined value when a load results in a failed access
// check. Because this behaviour is not specified properly as of yet, it only
// applies to a subset of spec-defined Well-Known Symbols.
#define WELL_KNOWN_SYMBOL_LIST_GENERATOR(V, _)  \
  V(_, has_instance_symbol, Symbol.hasInstance) \
  V(_, to_string_tag_symbol, Symbol.toStringTag)

// Custom list of Names that can cause protector invalidations.
// These Names have to be allocated consecutively for fast checks,
#define INTERNALIZED_STRING_FOR_PROTECTOR_LIST_GENERATOR(V, _) \
  V(_, constructor_string, "constructor")                      \
  V(_, next_string, "next")                                    \
  V(_, resolve_string, "resolve")                              \
  V(_, then_string, "then")                                    \
  V(_, valueOf_string, "valueOf")

// Note that the description string should be part of the internalized
// string roots to make sure we don't accidentally end up allocating the
// description in between the symbols during deserialization.
#define SYMBOL_FOR_PROTECTOR_LIST_GENERATOR(V, _) \
  V(_, iterator_symbol, Symbol.iterator)          \
  V(_, match_all_symbol, Symbol.matchAll)         \
  V(_, replace_symbol, Symbol.replace)            \
  V(_, species_symbol, Symbol.species)            \
  V(_, split_symbol, Symbol.split)

#define PUBLIC_SYMBOL_FOR_PROTECTOR_LIST_GENERATOR(V, _) \
  V(_, to_primitive_symbol, Symbol.toPrimitive)

#define WELL_KNOWN_SYMBOL_FOR_PROTECTOR_LIST_GENERATOR(V, _) \
  V(_, is_concat_spreadable_symbol, Symbol.isConcatSpreadable)

#define MC_INCREMENTAL_SCOPES(F)                                   \
  /* MC_INCREMENTAL is the top-level incremental marking scope. */ \
  F(MC_INCREMENTAL)                                                \
  F(MC_INCREMENTAL_EMBEDDER_TRACING)                               \
  F(MC_INCREMENTAL_EXTERNAL_EPILOGUE)                              \
  F(MC_INCREMENTAL_EXTERNAL_PROLOGUE)                              \
  F(MC_INCREMENTAL_FINALIZE)                                       \
  F(MC_INCREMENTAL_LAYOUT_CHANGE)                                  \
  F(MC_INCREMENTAL_START)                                          \
  F(MC_INCREMENTAL_SWEEPING)

#define MINOR_MS_INCREMENTAL_SCOPES(F) F(MINOR_MS_INCREMENTAL_START)

#define TOP_MC_SCOPES(F) \
  F(MC_CLEAR)            \
  F(MC_EPILOGUE)         \
  F(MC_EVACUATE)         \
  F(MC_FINISH)           \
  F(MC_MARK)             \
  F(MC_PROLOGUE)         \
  F(MC_SWEEP)

#define TOP_MINOR_MS_SCOPES(F) \
  F(MINOR_MS_CLEAR)            \
  F(MINOR_MS_FINISH)           \
  F(MINOR_MS_MARK)             \
  F(MINOR_MS_SWEEP)

#define MINOR_MS_MAIN_THREAD_SCOPES(F)      \
  F(MINOR_MARK_SWEEPER)                     \
  F(MINOR_MS)                               \
  TOP_MINOR_MS_SCOPES(F)                    \
  F(MINOR_MS_CLEAR_STRING_FORWARDING_TABLE) \
  F(MINOR_MS_CLEAR_STRING_TABLE)            \
  F(MINOR_MS_CLEAR_WEAK_GLOBAL_HANDLES)     \
  F(MINOR_MS_COMPLETE_SWEEP_ARRAY_BUFFERS)  \
  F(MINOR_MS_COMPLETE_SWEEPING)             \
  F(MINOR_MS_MARK_FINISH_INCREMENTAL)       \
  F(MINOR_MS_MARK_PARALLEL)                 \
  F(MINOR_MS_MARK_INCREMENTAL_SEED)         \
  F(MINOR_MS_MARK_SEED)                     \
  F(MINOR_MS_MARK_TRACED_HANDLES)           \
  F(MINOR_MS_MARK_CONSERVATIVE_STACK)       \
  F(MINOR_MS_MARK_CLOSURE_PARALLEL)         \
  F(MINOR_MS_MARK_CLOSURE)                  \
  F(MINOR_MS_MARK_EMBEDDER_PROLOGUE)        \
  F(MINOR_MS_MARK_EMBEDDER_TRACING)         \
  F(MINOR_MS_MARK_VERIFY)                   \
  F(MINOR_MS_INCREMENTAL_STEP)              \
  F(MINOR_MS_SWEEP_NEW)                     \
  F(MINOR_MS_SWEEP_NEW_LO)                  \
  F(MINOR_MS_SWEEP_UPDATE_STRING_TABLE)     \
  F(MINOR_MS_SWEEP_START_JOBS)              \
  F(MINOR_MS_FINISH_SWEEP_ARRAY_BUFFERS)    \
  F(MINOR_MS_FINISH_ENSURE_CAPACITY)

#define SCAVENGER_MAIN_THREAD_SCOPES(F)              \
  F(SCAVENGER)                                       \
  F(SCAVENGER_COMPLETE_SWEEP_ARRAY_BUFFERS)          \
  F(SCAVENGER_FREE_REMEMBERED_SET)                   \
  F(SCAVENGER_SCAVENGE)                              \
  F(SCAVENGER_SCAVENGE_WEAK_GLOBAL_HANDLES_IDENTIFY) \
  F(SCAVENGER_SCAVENGE_WEAK_GLOBAL_HANDLES_PROCESS)  \
  F(SCAVENGER_SCAVENGE_PARALLEL)                     \
  F(SCAVENGER_SCAVENGE_PARALLEL_PHASE)               \
  F(SCAVENGER_SCAVENGE_ROOTS)                        \
  F(SCAVENGER_SCAVENGE_STACK_ROOTS)                  \
  F(SCAVENGER_SCAVENGE_UPDATE_REFS)                  \
  F(SCAVENGER_SCAVENGE_WEAK)                         \
  F(SCAVENGER_SCAVENGE_FINALIZE)                     \
  F(SCAVENGER_SWEEP_ARRAY_BUFFERS)

#define MC_MAIN_THREAD_SCOPES(F)                 \
  F(MARK_COMPACTOR)                              \
  TOP_MC_SCOPES(F)                               \
  F(MC_CLEAR_DEPENDENT_CODE)                     \
  F(MC_CLEAR_EXTERNAL_STRING_TABLE)              \
  F(MC_CLEAR_STRING_FORWARDING_TABLE)            \
  F(MC_CLEAR_FLUSHABLE_BYTECODE)                 \
  F(MC_CLEAR_FLUSHED_JS_FUNCTIONS)               \
  F(MC_CLEAR_JOIN_JOB)                           \
  F(MC_CLEAR_JS_WEAK_REFERENCES)                 \
  F(MC_CLEAR_MAPS)                               \
  F(MC_CLEAR_SLOTS_BUFFER)                       \
  F(MC_CLEAR_STRING_TABLE)                       \
  F(MC_CLEAR_WEAK_COLLECTIONS)                   \
  F(MC_CLEAR_WEAK_GLOBAL_HANDLES)                \
  F(MC_CLEAR_WEAK_LISTS)                         \
  F(MC_CLEAR_WEAK_REFERENCES_FILTER_NON_TRIVIAL) \
  F(MC_CLEAR_WEAK_REFERENCES_JOIN_FILTER_JOB)    \
  F(MC_CLEAR_WEAK_REFERENCES_NON_TRIVIAL)        \
  F(MC_CLEAR_WEAK_REFERENCES_TRIVIAL)            \
  F(MC_SWEEP_EXTERNAL_POINTER_TABLE)             \
  F(MC_SWEEP_TRUSTED_POINTER_TABLE)              \
  F(MC_SWEEP_CODE_POINTER_TABLE)                 \
  F(MC_SWEEP_JS_DISPATCH_TABLE)                  \
  F(MC_COMPLETE_SWEEP_ARRAY_BUFFERS)             \
  F(MC_COMPLETE_SWEEPING)                        \
  F(MC_EVACUATE_CANDIDATES)                      \
  F(MC_EVACUATE_CLEAN_UP)                        \
  F(MC_EVACUATE_COPY)                            \
  F(MC_EVACUATE_COPY_PARALLEL)                   \
  F(MC_EVACUATE_EPILOGUE)                        \
  F(MC_EVACUATE_PROLOGUE)                        \
  F(MC_EVACUATE_REBALANCE)                       \
  F(MC_EVACUATE_UPDATE_POINTERS)                 \
  F(MC_EVACUATE_UPDATE_POINTERS_CLIENT_HEAPS)    \
  F(MC_EVACUATE_UPDATE_POINTERS_PARALLEL)        \
  F(MC_EVACUATE_UPDATE_POINTERS_SLOTS_MAIN)      \
  F(MC_EVACUATE_UPDATE_POINTERS_TO_NEW_ROOTS)    \
  F(MC_EVACUATE_UPDATE_POINTERS_WEAK)            \
  F(MC_EVACUATE_UPDATE_POINTERS_POINTER_TABLES)  \
  F(MC_FINISH_SWEEP_ARRAY_BUFFERS)               \
  F(MC_MARK_CLIENT_HEAPS)                        \
  F(MC_MARK_EMBEDDER_PROLOGUE)                   \
  F(MC_MARK_EMBEDDER_TRACING)                    \
  F(MC_MARK_FINISH_INCREMENTAL)                  \
  F(MC_MARK_FULL_CLOSURE_PARALLEL)               \
  F(MC_MARK_FULL_CLOSURE_PARALLEL_JOIN)          \
  F(MC_MARK_FULL_CLOSURE_SERIAL)                 \
  F(MC_MARK_RETAIN_MAPS)                         \
  F(MC_MARK_ROOTS)                               \
  F(MC_MARK_FULL_CLOSURE)                        \
  F(MC_MARK_WEAK_CLOSURE_EPHEMERON_MARKING)      \
  F(MC_MARK_WEAK_CLOSURE_EPHEMERON_LINEAR)       \
  F(MC_MARK_VERIFY)                              \
  F(MC_SWEEP_CODE)                               \
  F(MC_SWEEP_CODE_LO)                            \
  F(MC_SWEEP_LO)                                 \
  F(MC_SWEEP_MAP)                                \
  F(MC_SWEEP_NEW)                                \
  F(MC_SWEEP_NEW_LO)                             \
  F(MC_SWEEP_OLD)                                \
  F(MC_SWEEP_SHARED)                             \
  F(MC_SWEEP_SHARED_LO)                          \
  F(MC_SWEEP_TRUSTED)                            \
  F(MC_SWEEP_TRUSTED_LO)                         \
  F(MC_SWEEP_START_JOBS)                         \
  F(MC_WEAKNESS_HANDLING)

#define TRACER_SCOPES(F)                 \
  MC_INCREMENTAL_SCOPES(F)               \
  MINOR_MS_INCREMENTAL_SCOPES(F)         \
  F(HEAP_EMBEDDER_TRACING_EPILOGUE)      \
  F(HEAP_EPILOGUE)                       \
  F(HEAP_EPILOGUE_REDUCE_NEW_SPACE)      \
  F(HEAP_EPILOGUE_SAFEPOINT)             \
  F(HEAP_EXTERNAL_EPILOGUE)              \
  F(HEAP_EXTERNAL_NEAR_HEAP_LIMIT)       \
  F(HEAP_EXTERNAL_PROLOGUE)              \
  F(HEAP_EXTERNAL_SECOND_PASS_CALLBACKS) \
  F(HEAP_EXTERNAL_WEAK_GLOBAL_HANDLES)   \
  F(HEAP_PROLOGUE)                       \
  F(HEAP_PROLOGUE_SAFEPOINT)             \
  MC_MAIN_THREAD_SCOPES(F)               \
  MINOR_MS_MAIN_THREAD_SCOPES(F)         \
  F(SAFEPOINT)                           \
  SCAVENGER_MAIN_THREAD_SCOPES(F)        \
  F(TIME_TO_GLOBAL_SAFEPOINT)            \
  F(TIME_TO_SAFEPOINT)                   \
  F(UNPARK)                              \
  F(YOUNG_ARRAY_BUFFER_SWEEP)            \
  F(FULL_ARRAY_BUFFER_SWEEP)             \
  F(CONSERVATIVE_STACK_SCANNING)

#define TRACER_BACKGROUND_SCOPES(F)         \
  /* FIRST_BACKGROUND_SCOPE = */            \
  F(BACKGROUND_YOUNG_ARRAY_BUFFER_SWEEP)    \
  F(BACKGROUND_FULL_ARRAY_BUFFER_SWEEP)     \
  F(BACKGROUND_COLLECTION)                  \
  F(BACKGROUND_UNPARK)                      \
  F(BACKGROUND_SAFEPOINT)                   \
  F(MC_BACKGROUND_EVACUATE_COPY)            \
  F(MC_BACKGROUND_EVACUATE_UPDATE_POINTERS) \
  F(MC_BACKGROUND_MARKING)                  \
  F(MC_BACKGROUND_SWEEPING)                 \
  F(MINOR_MS_BACKGROUND_MARKING)            \
  F(MINOR_MS_BACKGROUND_SWEEPING)           \
  F(MINOR_MS_BACKGROUND_MARKING_CLOSURE)    \
  /* LAST_BACKGROUND_SCOPE = */             \
  F(SCAVENGER_BACKGROUND_SCAVENGE_PARALLEL)

#define TRACER_YOUNG_EPOCH_SCOPES(F)     \
  F(YOUNG_ARRAY_BUFFER_SWEEP)            \
  F(BACKGROUND_YOUNG_ARRAY_BUFFER_SWEEP) \
  MINOR_MS_INCREMENTAL_SCOPES(F)         \
  MINOR_MS_MAIN_THREAD_SCOPES(F)         \
  F(MINOR_MS_BACKGROUND_MARKING)         \
  F(MINOR_MS_BACKGROUND_SWEEPING)        \
  F(MINOR_MS_BACKGROUND_MARKING_CLOSURE) \
  SCAVENGER_MAIN_THREAD_SCOPES(F)        \
  F(SCAVENGER_BACKGROUND_SCAVENGE_PARALLEL)

#endif  // V8_INIT_HEAP_SYMBOLS_H_

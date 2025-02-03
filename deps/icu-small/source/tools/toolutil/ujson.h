// © 2024 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
#ifndef __UJSON_H__
#define __UJSON_H__

/*
  Without this code, the output if the JSON library code
  throws an exception would look like:
  terminate called after throwing an instance of 'nlohmann::json_abi_v3_11_3::detail::parse_error'
  what():  [json.exception.parse_error.101] parse error at line 1, column 1: attempting to parse an empty input; check that your input string or stream contains the expected JSON
Aborted (core dumped)

  (for example, if one of the JSON tests files contains an error or a file doesn't exist.)

  With this code, the output is:

  JSON exception thrown; modify tools/ctestfw//ujson.h to get diagnostics.
  Exiting immediately.

  The entire #if block can be commented out in order to temporarily enable exceptions
  and get a better parse error message (temporarily, while debugging).
 */

// Disable exceptions in JSON parser

#if _HAS_EXCEPTIONS == 0
#define JSON_TRY_USER if(true)
#define JSON_CATCH_USER(exception) if(false)
#define JSON_THROW_USER(exception) { \
    printf("JSON exception thrown; modify tools/toolutil/ujson.h to get diagnostics.\n\
Exiting immediately.\n"); \
    exit(1); \
}
#endif

#include "json-json.hpp"

#endif /* __UJSON_H__ */

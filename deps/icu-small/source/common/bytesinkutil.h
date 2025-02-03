// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

// bytesinkutil.h
// created: 2017sep14 Markus W. Scherer

#ifndef BYTESINKUTIL_H
#define BYTESINKUTIL_H

#include <type_traits>

#include "unicode/utypes.h"
#include "unicode/bytestream.h"
#include "unicode/edits.h"
#include "charstr.h"
#include "cmemory.h"
#include "uassert.h"
#include "ustr_imp.h"

U_NAMESPACE_BEGIN

class ByteSink;
class Edits;

class U_COMMON_API CharStringByteSink : public ByteSink {
public:
    CharStringByteSink(CharString* dest);
    ~CharStringByteSink() override;

    CharStringByteSink() = delete;
    CharStringByteSink(const CharStringByteSink&) = delete;
    CharStringByteSink& operator=(const CharStringByteSink&) = delete;

    void Append(const char* bytes, int32_t n) override;

    char* GetAppendBuffer(int32_t min_capacity,
                          int32_t desired_capacity_hint,
                          char* scratch,
                          int32_t scratch_capacity,
                          int32_t* result_capacity) override;

private:
    CharString& dest_;
};

// CharString doesn't provide the public API that StringByteSink requires a
// string class to have so this template specialization replaces the default
// implementation of StringByteSink<CharString> with CharStringByteSink.
template<>
class StringByteSink<CharString> : public CharStringByteSink {
 public:
  StringByteSink(CharString* dest) : CharStringByteSink(dest) { }
  StringByteSink(CharString* dest, int32_t /*initialAppendCapacity*/) : CharStringByteSink(dest) { }
};

class U_COMMON_API ByteSinkUtil {
public:
    ByteSinkUtil() = delete;  // all static

    /** (length) bytes were mapped to valid (s16, s16Length). */
    static UBool appendChange(int32_t length,
                              const char16_t *s16, int32_t s16Length,
                              ByteSink &sink, Edits *edits, UErrorCode &errorCode);

    /** The bytes at [s, limit[ were mapped to valid (s16, s16Length). */
    static UBool appendChange(const uint8_t *s, const uint8_t *limit,
                              const char16_t *s16, int32_t s16Length,
                              ByteSink &sink, Edits *edits, UErrorCode &errorCode);

    /** (length) bytes were mapped/changed to valid code point c. */
    static void appendCodePoint(int32_t length, UChar32 c, ByteSink &sink, Edits *edits = nullptr);

    /** The few bytes at [src, nextSrc[ were mapped/changed to valid code point c. */
    static inline void appendCodePoint(const uint8_t *src, const uint8_t *nextSrc, UChar32 c,
                                       ByteSink &sink, Edits *edits = nullptr) {
        appendCodePoint(static_cast<int32_t>(nextSrc - src), c, sink, edits);
    }

    /** Append the two-byte character (U+0080..U+07FF). */
    static void appendTwoBytes(UChar32 c, ByteSink &sink);

    static UBool appendUnchanged(const uint8_t *s, int32_t length,
                                 ByteSink &sink, uint32_t options, Edits *edits,
                                 UErrorCode &errorCode) {
        if (U_FAILURE(errorCode)) { return false; }
        if (length > 0) { appendNonEmptyUnchanged(s, length, sink, options, edits); }
        return true;
    }

    static UBool appendUnchanged(const uint8_t *s, const uint8_t *limit,
                                 ByteSink &sink, uint32_t options, Edits *edits,
                                 UErrorCode &errorCode);

    /**
     * Calls a lambda that writes to a ByteSink with a CheckedArrayByteSink
     * and then returns through u_terminateChars(), in order to implement
     * the classic ICU4C C API writing to a fix sized buffer on top of a
     * contemporary C++ API.
     *
     * @param buffer receiving buffer
     * @param capacity capacity of receiving buffer
     * @param lambda that gets called with the sink as an argument
     * @param status set to U_BUFFER_OVERFLOW_ERROR on overflow
     * @return number of bytes written, or needed (in case of overflow)
     * @internal
     */
    template <typename F,
              typename = std::enable_if_t<
                  std::is_invocable_r_v<void, F, ByteSink&, UErrorCode&>>>
    static int32_t viaByteSinkToTerminatedChars(char* buffer, int32_t capacity,
                                                F&& lambda,
                                                UErrorCode& status) {
        if (U_FAILURE(status)) { return 0; }
        CheckedArrayByteSink sink(buffer, capacity);
        lambda(sink, status);
        if (U_FAILURE(status)) { return 0; }

        int32_t reslen = sink.NumberOfBytesAppended();

        if (sink.Overflowed()) {
            status = U_BUFFER_OVERFLOW_ERROR;
            return reslen;
        }

        return u_terminateChars(buffer, capacity, reslen, &status);
    }

    /**
     * Calls a lambda that writes to a ByteSink with a CharStringByteSink and
     * then returns a CharString, in order to implement a contemporary C++ API
     * on top of a C/C++ compatibility ByteSink API.
     *
     * @param lambda that gets called with the sink as an argument
     * @param status to check and report
     * @return the resulting string, or an empty string (in case of error)
     * @internal
     */
    template <typename F,
              typename = std::enable_if_t<
                  std::is_invocable_r_v<void, F, ByteSink&, UErrorCode&>>>
    static CharString viaByteSinkToCharString(F&& lambda, UErrorCode& status) {
        if (U_FAILURE(status)) { return {}; }
        CharString result;
        CharStringByteSink sink(&result);
        lambda(sink, status);
        return result;
    }

private:
    static void appendNonEmptyUnchanged(const uint8_t *s, int32_t length,
                                        ByteSink &sink, uint32_t options, Edits *edits);
};

U_NAMESPACE_END

#endif //BYTESINKUTIL_H

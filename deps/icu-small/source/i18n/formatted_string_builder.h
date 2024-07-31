// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMBER_STRINGBUILDER_H__
#define __NUMBER_STRINGBUILDER_H__


#include <cstdint>
#include <type_traits>

#include "cstring.h"
#include "uassert.h"
#include "fphdlimp.h"

U_NAMESPACE_BEGIN

class FormattedValueStringBuilderImpl;

/**
 * A StringBuilder optimized for formatting. It implements the following key
 * features beyond a UnicodeString:
 *
 * <ol>
 * <li>Efficient prepend as well as append.
 * <li>Keeps track of Fields in an efficient manner.
 * </ol>
 *
 * See also FormattedValueStringBuilderImpl.
 *
 * @author sffc (Shane Carr)
 */
class U_I18N_API FormattedStringBuilder : public UMemory {
  private:
    static const int32_t DEFAULT_CAPACITY = 40;

    template<typename T>
    union ValueOrHeapArray {
        T value[DEFAULT_CAPACITY];
        struct {
            T *ptr;
            int32_t capacity;
        } heap;
    };

  public:
    FormattedStringBuilder();

    ~FormattedStringBuilder();

    FormattedStringBuilder(const FormattedStringBuilder &other);

    // Convention: bottom 4 bits for field, top 4 bits for field category.
    // Field category 0 implies the number category so that the number field
    // literals can be directly passed as a Field type.
    // Exported as U_I18N_API so it can be used by other exports on Windows.
    struct U_I18N_API Field {
        uint8_t bits;

        Field() = default;
        constexpr Field(uint8_t category, uint8_t field);

        inline UFieldCategory getCategory() const;
        inline int32_t getField() const;
        inline bool isNumeric() const;
        inline bool isUndefined() const;
        inline bool operator==(const Field& other) const;
        inline bool operator!=(const Field& other) const;
    };

    FormattedStringBuilder &operator=(const FormattedStringBuilder &other);

    int32_t length() const;

    int32_t codePointCount() const;

    inline char16_t charAt(int32_t index) const {
        U_ASSERT(index >= 0);
        U_ASSERT(index < fLength);
        return getCharPtr()[fZero + index];
    }

    inline Field fieldAt(int32_t index) const {
        U_ASSERT(index >= 0);
        U_ASSERT(index < fLength);
        return getFieldPtr()[fZero + index];
    }

    UChar32 getFirstCodePoint() const;

    UChar32 getLastCodePoint() const;

    UChar32 codePointAt(int32_t index) const;

    UChar32 codePointBefore(int32_t index) const;

    FormattedStringBuilder &clear();

    /** Appends a UTF-16 code unit. */
    inline int32_t appendChar16(char16_t codeUnit, Field field, UErrorCode& status) {
        // appendCodePoint handles both code units and code points.
        return insertCodePoint(fLength, codeUnit, field, status);
    }

    /** Inserts a UTF-16 code unit. Note: insert at index 0 is very efficient. */
    inline int32_t insertChar16(int32_t index, char16_t codeUnit, Field field, UErrorCode& status) {
        // insertCodePoint handles both code units and code points.
        return insertCodePoint(index, codeUnit, field, status);
    }

    /** Appends a Unicode code point. */
    inline int32_t appendCodePoint(UChar32 codePoint, Field field, UErrorCode &status) {
        return insertCodePoint(fLength, codePoint, field, status);
    }

    /** Inserts a Unicode code point. Note: insert at index 0 is very efficient. */
    int32_t insertCodePoint(int32_t index, UChar32 codePoint, Field field, UErrorCode &status);

    /** Appends a string. */
    inline int32_t append(const UnicodeString &unistr, Field field, UErrorCode &status) {
        return insert(fLength, unistr, field, status);
    }

    /** Inserts a string. Note: insert at index 0 is very efficient. */
    int32_t insert(int32_t index, const UnicodeString &unistr, Field field, UErrorCode &status);

    /** Inserts a substring. Note: insert at index 0 is very efficient.
     *
     * @param start Start index of the substring of unistr to be inserted.
     * @param end End index of the substring of unistr to be inserted (exclusive).
     */
    int32_t insert(int32_t index, const UnicodeString &unistr, int32_t start, int32_t end, Field field,
                   UErrorCode &status);

    /** Deletes a substring and then inserts a string at that same position.
     * Similar to JavaScript Array.prototype.splice().
     *
     * @param startThis Start of the span to delete.
     * @param endThis End of the span to delete (exclusive).
     * @param unistr The string to insert at the deletion position.
     * @param startOther Start index of the substring of unistr to be inserted.
     * @param endOther End index of the substring of unistr to be inserted (exclusive).
     */
    int32_t splice(int32_t startThis, int32_t endThis,  const UnicodeString &unistr,
                   int32_t startOther, int32_t endOther, Field field, UErrorCode& status);

    /** Appends a formatted string. */
    int32_t append(const FormattedStringBuilder &other, UErrorCode &status);

    /** Inserts a formatted string. Note: insert at index 0 is very efficient. */
    int32_t insert(int32_t index, const FormattedStringBuilder &other, UErrorCode &status);

    /**
     * Ensures that the string buffer contains a NUL terminator. The NUL terminator does
     * not count toward the string length. Any further changes to the string (insert or
     * append) may invalidate the NUL terminator.
     *
     * You should call this method after the formatted string is completely built if you
     * plan to return a pointer to the string from a C API.
     */
    void writeTerminator(UErrorCode& status);

    /**
     * Gets a "safe" UnicodeString that can be used even after the FormattedStringBuilder is destructed.
     */
    UnicodeString toUnicodeString() const;

    /**
     * Gets an "unsafe" UnicodeString that is valid only as long as the FormattedStringBuilder is alive and
     * unchanged. Slightly faster than toUnicodeString().
     */
    UnicodeString toTempUnicodeString() const;

    UnicodeString toDebugString() const;

    const char16_t *chars() const;

    bool contentEquals(const FormattedStringBuilder &other) const;

    bool containsField(Field field) const;

  private:
    bool fUsingHeap = false;
    ValueOrHeapArray<char16_t> fChars;
    ValueOrHeapArray<Field> fFields;
    int32_t fZero = DEFAULT_CAPACITY / 2;
    int32_t fLength = 0;

    inline char16_t *getCharPtr() {
        return fUsingHeap ? fChars.heap.ptr : fChars.value;
    }

    inline const char16_t *getCharPtr() const {
        return fUsingHeap ? fChars.heap.ptr : fChars.value;
    }

    inline Field *getFieldPtr() {
        return fUsingHeap ? fFields.heap.ptr : fFields.value;
    }

    inline const Field *getFieldPtr() const {
        return fUsingHeap ? fFields.heap.ptr : fFields.value;
    }

    inline int32_t getCapacity() const {
        return fUsingHeap ? fChars.heap.capacity : DEFAULT_CAPACITY;
    }

    int32_t prepareForInsert(int32_t index, int32_t count, UErrorCode &status);

    int32_t prepareForInsertHelper(int32_t index, int32_t count, UErrorCode &status);

    int32_t remove(int32_t index, int32_t count);

    friend class FormattedValueStringBuilderImpl;
};

static_assert(
    // std::is_pod<> is deprecated.
    std::is_standard_layout_v<FormattedStringBuilder::Field> &&
        std::is_trivial_v<FormattedStringBuilder::Field>,
    "Field should be a POD type for efficient initialization");

constexpr FormattedStringBuilder::Field::Field(uint8_t category, uint8_t field)
    : bits((
        U_ASSERT(category <= 0xf),
        U_ASSERT(field <= 0xf),
        static_cast<uint8_t>((category << 4) | field)
    )) {}

/**
 * Internal constant for the undefined field for use in FormattedStringBuilder.
 */
constexpr FormattedStringBuilder::Field kUndefinedField = {UFIELD_CATEGORY_UNDEFINED, 0};

/**
 * Internal field to signal "numeric" when fields are not supported in NumberFormat.
 */
constexpr FormattedStringBuilder::Field kGeneralNumericField = {UFIELD_CATEGORY_UNDEFINED, 1};

inline UFieldCategory FormattedStringBuilder::Field::getCategory() const {
    return static_cast<UFieldCategory>(bits >> 4);
}

inline int32_t FormattedStringBuilder::Field::getField() const {
    return bits & 0xf;
}

inline bool FormattedStringBuilder::Field::isNumeric() const {
    return getCategory() == UFIELD_CATEGORY_NUMBER || *this == kGeneralNumericField;
}

inline bool FormattedStringBuilder::Field::isUndefined() const {
    return getCategory() == UFIELD_CATEGORY_UNDEFINED;
}

inline bool FormattedStringBuilder::Field::operator==(const Field& other) const {
    return bits == other.bits;
}

inline bool FormattedStringBuilder::Field::operator!=(const Field& other) const {
    return bits != other.bits;
}

U_NAMESPACE_END


#endif //__NUMBER_STRINGBUILDER_H__

#endif /* #if !UCONFIG_NO_FORMATTING */

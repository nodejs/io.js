// © 2019 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

// lsr.h
// created: 2019may08 Markus W. Scherer

#ifndef __LSR_H__
#define __LSR_H__

#include "unicode/stringpiece.h"
#include "unicode/utypes.h"
#include "unicode/uobject.h"
#include "cstring.h"

U_NAMESPACE_BEGIN

struct LSR final : public UMemory {
    static constexpr int32_t REGION_INDEX_LIMIT = 1001 + 26 * 26;

    static constexpr int32_t EXPLICIT_LSR = 7;
    static constexpr int32_t EXPLICIT_LANGUAGE = 4;
    static constexpr int32_t EXPLICIT_SCRIPT = 2;
    static constexpr int32_t EXPLICIT_REGION = 1;
    static constexpr int32_t IMPLICIT_LSR = 0;
    static constexpr int32_t DONT_CARE_FLAGS = 0;

    const char *language;
    const char *script;
    const char *region;
    char *owned = nullptr;
    /** Index for region, 0 if ill-formed. @see indexForRegion */
    int32_t regionIndex = 0;
    int32_t flags = 0;
    /** Only set for LSRs that will be used in a hash table. */
    int32_t hashCode = 0;

    LSR() : language("und"), script(""), region("") {}

    /** Constructor which aliases all subtag pointers. */
    LSR(const char *lang, const char *scr, const char *r, int32_t f) :
            language(lang),  script(scr), region(r),
            regionIndex(indexForRegion(region)), flags(f) {}
    /**
     * Constructor which prepends the prefix to the language and script,
     * copies those into owned memory, and aliases the region.
     */
    LSR(char prefix, const char *lang, const char *scr, const char *r, int32_t f,
        UErrorCode &errorCode);
    LSR(StringPiece lang, StringPiece scr, StringPiece r, int32_t f,
        UErrorCode &errorCode);
    LSR(LSR &&other) noexcept;
    LSR(const LSR &other) = delete;
    inline ~LSR() {
        // Pure inline code for almost all instances.
        if (owned != nullptr) {
            deleteOwned();
        }
    }

    LSR &operator=(LSR &&other) noexcept;
    LSR &operator=(const LSR &other) = delete;

    /**
     * Returns a positive index (>0) for a well-formed region code.
     * Do not rely on a particular region->index mapping; it may change.
     * Returns 0 for ill-formed strings.
     */
    static int32_t indexForRegion(const char *region);

    UBool isEquivalentTo(const LSR &other) const;
    bool operator==(const LSR &other) const;

    inline bool operator!=(const LSR &other) const {
        return !operator==(other);
    }

    LSR &setHashCode();

private:
    void deleteOwned();
};

U_NAMESPACE_END

#endif  // __LSR_H__

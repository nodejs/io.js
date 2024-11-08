// © 2021 and later: Unicode, Inc. and others.
// License & terms of use: https://www.unicode.org/copyright.html

// emojiprops.cpp
// created: 2021sep04 Markus W. Scherer

#include "unicode/utypes.h"
#include "unicode/uchar.h"
#include "unicode/ucharstrie.h"
#include "unicode/ucptrie.h"
#include "unicode/udata.h"
#include "unicode/ustringtrie.h"
#include "unicode/utf16.h"
#include "emojiprops.h"
#include "ucln.h"
#include "ucln_cmn.h"
#include "umutex.h"
#include "uset_imp.h"

U_NAMESPACE_BEGIN

namespace {

EmojiProps *singleton = nullptr;
icu::UInitOnce emojiInitOnce {};

UBool U_CALLCONV emojiprops_cleanup() {
    delete singleton;
    singleton = nullptr;
    emojiInitOnce.reset();
    return true;
}

void U_CALLCONV initSingleton(UErrorCode &errorCode) {
    if (U_FAILURE(errorCode)) { return; }
    singleton = new EmojiProps(errorCode);
    if (singleton == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
    } else if (U_FAILURE(errorCode)) {
        delete singleton;
        singleton = nullptr;
    }
    ucln_common_registerCleanup(UCLN_COMMON_EMOJIPROPS, emojiprops_cleanup);
}

// TODO: turn this into a shared helper function
// Requires the major version to match, and then requires at least the minor version.
UBool udata_isAcceptableMajorMinor(
        const UDataInfo &info, const char16_t *dataFormat, uint8_t major, uint8_t minor) {
    return
        info.size >= 20 &&
        info.isBigEndian == U_IS_BIG_ENDIAN &&
        info.charsetFamily == U_CHARSET_FAMILY &&
        info.dataFormat[0] == dataFormat[0] &&
        info.dataFormat[1] == dataFormat[1] &&
        info.dataFormat[2] == dataFormat[2] &&
        info.dataFormat[3] == dataFormat[3] &&
        info.formatVersion[0] == major &&
        info.formatVersion[1] >= minor;
}

}  // namespace

EmojiProps::~EmojiProps() {
    udata_close(memory);
    ucptrie_close(cpTrie);
}

const EmojiProps *
EmojiProps::getSingleton(UErrorCode &errorCode) {
    if (U_FAILURE(errorCode)) { return nullptr; }
    umtx_initOnce(emojiInitOnce, &initSingleton, errorCode);
    return singleton;
}

UBool U_CALLCONV
EmojiProps::isAcceptable(void * /*context*/, const char * /*type*/, const char * /*name*/,
                         const UDataInfo *pInfo) {
    return udata_isAcceptableMajorMinor(*pInfo, u"Emoj", 1, 0);
}

void
EmojiProps::load(UErrorCode &errorCode) {
    memory = udata_openChoice(nullptr, "icu", "uemoji", isAcceptable, this, &errorCode);
    if (U_FAILURE(errorCode)) { return; }
    const uint8_t* inBytes = static_cast<const uint8_t*>(udata_getMemory(memory));
    const int32_t* inIndexes = reinterpret_cast<const int32_t*>(inBytes);
    int32_t indexesLength = inIndexes[IX_CPTRIE_OFFSET] / 4;
    if (indexesLength <= IX_RGI_EMOJI_ZWJ_SEQUENCE_TRIE_OFFSET) {
        errorCode = U_INVALID_FORMAT_ERROR;  // Not enough indexes.
        return;
    }

    int32_t i = IX_CPTRIE_OFFSET;
    int32_t offset = inIndexes[i++];
    int32_t nextOffset = inIndexes[i];
    cpTrie = ucptrie_openFromBinary(UCPTRIE_TYPE_FAST, UCPTRIE_VALUE_BITS_8,
                                    inBytes + offset, nextOffset - offset, nullptr, &errorCode);
    if (U_FAILURE(errorCode)) {
        return;
    }

    for (i = IX_BASIC_EMOJI_TRIE_OFFSET; i <= IX_RGI_EMOJI_ZWJ_SEQUENCE_TRIE_OFFSET; ++i) {
        offset = inIndexes[i];
        nextOffset = inIndexes[i + 1];
        // Set/leave nullptr if there is no UCharsTrie.
        const char16_t* p = nextOffset > offset ? reinterpret_cast<const char16_t*>(inBytes + offset) : nullptr;
        stringTries[getStringTrieIndex(i)] = p;
    }
}

void
EmojiProps::addPropertyStarts(const USetAdder *sa, UErrorCode & /*errorCode*/) const {
    // Add the start code point of each same-value range of the trie.
    UChar32 start = 0, end;
    uint32_t value;
    while ((end = ucptrie_getRange(cpTrie, start, UCPMAP_RANGE_NORMAL, 0,
                                   nullptr, nullptr, &value)) >= 0) {
        sa->add(sa->set, start);
        start = end + 1;
    }
}

UBool
EmojiProps::hasBinaryProperty(UChar32 c, UProperty which) {
    UErrorCode errorCode = U_ZERO_ERROR;
    const EmojiProps *ep = getSingleton(errorCode);
    return U_SUCCESS(errorCode) && ep->hasBinaryPropertyImpl(c, which);
}

UBool
EmojiProps::hasBinaryPropertyImpl(UChar32 c, UProperty which) const {
    if (which < UCHAR_EMOJI || UCHAR_RGI_EMOJI < which) {
        return false;
    }
    // Note: UCHAR_REGIONAL_INDICATOR is a single, hardcoded range implemented elsewhere.
    static constexpr int8_t bitFlags[] = {
        BIT_EMOJI,                  // UCHAR_EMOJI=57
        BIT_EMOJI_PRESENTATION,     // UCHAR_EMOJI_PRESENTATION=58
        BIT_EMOJI_MODIFIER,         // UCHAR_EMOJI_MODIFIER=59
        BIT_EMOJI_MODIFIER_BASE,    // UCHAR_EMOJI_MODIFIER_BASE=60
        BIT_EMOJI_COMPONENT,        // UCHAR_EMOJI_COMPONENT=61
        -1,                         // UCHAR_REGIONAL_INDICATOR=62
        -1,                         // UCHAR_PREPENDED_CONCATENATION_MARK=63
        BIT_EXTENDED_PICTOGRAPHIC,  // UCHAR_EXTENDED_PICTOGRAPHIC=64
        BIT_BASIC_EMOJI,            // UCHAR_BASIC_EMOJI=65
        -1,                         // UCHAR_EMOJI_KEYCAP_SEQUENCE=66
        -1,                         // UCHAR_RGI_EMOJI_MODIFIER_SEQUENCE=67
        -1,                         // UCHAR_RGI_EMOJI_FLAG_SEQUENCE=68
        -1,                         // UCHAR_RGI_EMOJI_TAG_SEQUENCE=69
        -1,                         // UCHAR_RGI_EMOJI_ZWJ_SEQUENCE=70
        BIT_BASIC_EMOJI,            // UCHAR_RGI_EMOJI=71
    };
    int32_t bit = bitFlags[which - UCHAR_EMOJI];
    if (bit < 0) {
        return false;  // not a property that we support in this function
    }
    uint8_t bits = UCPTRIE_FAST_GET(cpTrie, UCPTRIE_8, c);
    return (bits >> bit) & 1;
}

UBool
EmojiProps::hasBinaryProperty(const char16_t *s, int32_t length, UProperty which) {
    UErrorCode errorCode = U_ZERO_ERROR;
    const EmojiProps *ep = getSingleton(errorCode);
    return U_SUCCESS(errorCode) && ep->hasBinaryPropertyImpl(s, length, which);
}

UBool
EmojiProps::hasBinaryPropertyImpl(const char16_t *s, int32_t length, UProperty which) const {
    if (s == nullptr && length != 0) { return false; }
    if (length <= 0 && (length == 0 || *s == 0)) { return false; }  // empty string
    // The caller should have delegated single code points to hasBinaryProperty(c, which).
    if (which < UCHAR_BASIC_EMOJI || UCHAR_RGI_EMOJI < which) {
        return false;
    }
    UProperty firstProp = which, lastProp = which;
    if (which == UCHAR_RGI_EMOJI) {
        // RGI_Emoji is the union of the other emoji properties of strings.
        firstProp = UCHAR_BASIC_EMOJI;
        lastProp = UCHAR_RGI_EMOJI_ZWJ_SEQUENCE;
    }
    for (int32_t prop = firstProp; prop <= lastProp; ++prop) {
        const char16_t *trieUChars = stringTries[prop - UCHAR_BASIC_EMOJI];
        if (trieUChars != nullptr) {
            UCharsTrie trie(trieUChars);
            UStringTrieResult result = trie.next(s, length);
            if (USTRINGTRIE_HAS_VALUE(result)) {
                return true;
            }
        }
    }
    return false;
}

void
EmojiProps::addStrings(const USetAdder *sa, UProperty which, UErrorCode &errorCode) const {
    if (U_FAILURE(errorCode)) { return; }
    if (which < UCHAR_BASIC_EMOJI || UCHAR_RGI_EMOJI < which) {
        return;
    }
    UProperty firstProp = which, lastProp = which;
    if (which == UCHAR_RGI_EMOJI) {
        // RGI_Emoji is the union of the other emoji properties of strings.
        firstProp = UCHAR_BASIC_EMOJI;
        lastProp = UCHAR_RGI_EMOJI_ZWJ_SEQUENCE;
    }
    for (int32_t prop = firstProp; prop <= lastProp; ++prop) {
        const char16_t *trieUChars = stringTries[prop - UCHAR_BASIC_EMOJI];
        if (trieUChars != nullptr) {
            UCharsTrie::Iterator iter(trieUChars, 0, errorCode);
            while (iter.next(errorCode)) {
                const UnicodeString &s = iter.getString();
                sa->addString(sa->set, s.getBuffer(), s.length());
            }
        }
    }
}

U_NAMESPACE_END

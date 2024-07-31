// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMBER_MODIFIERS_H__
#define __NUMBER_MODIFIERS_H__

#include <algorithm>
#include <cstdint>
#include "unicode/uniset.h"
#include "unicode/simpleformatter.h"
#include "standardplural.h"
#include "formatted_string_builder.h"
#include "number_types.h"

U_NAMESPACE_BEGIN
namespace number::impl {

/**
 * The canonical implementation of {@link Modifier}, containing a prefix and suffix string.
 * TODO: This is not currently being used by real code and could be removed.
 */
class U_I18N_API ConstantAffixModifier : public Modifier, public UObject {
  public:
    ConstantAffixModifier(const UnicodeString &prefix, const UnicodeString &suffix, Field field,
                          bool strong)
            : fPrefix(prefix), fSuffix(suffix), fField(field), fStrong(strong) {}

    int32_t apply(FormattedStringBuilder &output, int32_t leftIndex, int32_t rightIndex,
                  UErrorCode &status) const override;

    int32_t getPrefixLength() const override;

    int32_t getCodePointCount() const override;

    bool isStrong() const override;

    bool containsField(Field field) const override;

    void getParameters(Parameters& output) const override;

    bool strictEquals(const Modifier& other) const override;

  private:
    UnicodeString fPrefix;
    UnicodeString fSuffix;
    Field fField;
    bool fStrong;
};

/**
 * The second primary implementation of {@link Modifier}, this one consuming a {@link SimpleFormatter}
 * pattern.
 */
class U_I18N_API SimpleModifier : public Modifier, public UMemory {
  public:
    SimpleModifier(const SimpleFormatter &simpleFormatter, Field field, bool strong);

    SimpleModifier(const SimpleFormatter &simpleFormatter, Field field, bool strong,
                   const Modifier::Parameters parameters);

    // Default constructor for LongNameHandler.h
    SimpleModifier();

    int32_t apply(FormattedStringBuilder &output, int32_t leftIndex, int32_t rightIndex,
                  UErrorCode &status) const override;

    int32_t getPrefixLength() const override;

    int32_t getCodePointCount() const override;

    bool isStrong() const override;

    bool containsField(Field field) const override;

    void getParameters(Parameters& output) const override;

    bool strictEquals(const Modifier& other) const override;

    /**
     * TODO: This belongs in SimpleFormatterImpl. The only reason I haven't moved it there yet is because
     * FormattedStringBuilder is an internal class and SimpleFormatterImpl feels like it should not depend on it.
     *
     * <p>
     * Formats a value that is already stored inside the StringBuilder <code>result</code> between the indices
     * <code>startIndex</code> and <code>endIndex</code> by inserting characters before the start index and after the
     * end index.
     *
     * <p>
     * This is well-defined only for patterns with exactly one argument.
     *
     * @param result
     *            The StringBuilder containing the value argument.
     * @param startIndex
     *            The left index of the value within the string builder.
     * @param endIndex
     *            The right index of the value within the string builder.
     * @return The number of characters (UTF-16 code points) that were added to the StringBuilder.
     */
    int32_t
    formatAsPrefixSuffix(FormattedStringBuilder& result, int32_t startIndex, int32_t endIndex,
                         UErrorCode& status) const;

    /**
     * TODO: Like above, this belongs with the rest of the SimpleFormatterImpl code.
     * I put it here so that the SimpleFormatter uses in FormattedStringBuilder are near each other.
     *
     * <p>
     * Applies the compiled two-argument pattern to the FormattedStringBuilder.
     *
     * <p>
     * This method is optimized for the case where the prefix and suffix are often empty, such as
     * in the range pattern like "{0}-{1}".
     */
    static int32_t
    formatTwoArgPattern(const SimpleFormatter& compiled, FormattedStringBuilder& result,
                        int32_t index, int32_t* outPrefixLength, int32_t* outSuffixLength,
                        Field field, UErrorCode& status);

  private:
    UnicodeString fCompiledPattern;
    Field fField;
    bool fStrong = false;
    int32_t fPrefixLength = 0;
    int32_t fSuffixOffset = -1;
    int32_t fSuffixLength = 0;
    Modifier::Parameters fParameters;
};

/**
 * An implementation of {@link Modifier} that allows for multiple types of fields in the same modifier. Constructed
 * based on the contents of two {@link FormattedStringBuilder} instances (one for the prefix, one for the suffix).
 */
class U_I18N_API ConstantMultiFieldModifier : public Modifier, public UMemory {
  public:
    ConstantMultiFieldModifier(
            const FormattedStringBuilder &prefix,
            const FormattedStringBuilder &suffix,
            bool overwrite,
            bool strong,
            const Modifier::Parameters parameters)
      : fPrefix(prefix),
        fSuffix(suffix),
        fOverwrite(overwrite),
        fStrong(strong),
        fParameters(parameters) {}

    ConstantMultiFieldModifier(
            const FormattedStringBuilder &prefix,
            const FormattedStringBuilder &suffix,
            bool overwrite,
            bool strong)
      : fPrefix(prefix),
        fSuffix(suffix),
        fOverwrite(overwrite),
        fStrong(strong) {}

    int32_t apply(FormattedStringBuilder &output, int32_t leftIndex, int32_t rightIndex,
                  UErrorCode &status) const override;

    int32_t getPrefixLength() const override;

    int32_t getCodePointCount() const override;

    bool isStrong() const override;

    bool containsField(Field field) const override;

    void getParameters(Parameters& output) const override;

    bool strictEquals(const Modifier& other) const override;

  protected:
    // NOTE: In Java, these are stored as array pointers. In C++, the FormattedStringBuilder is stored by
    // value and is treated internally as immutable.
    FormattedStringBuilder fPrefix;
    FormattedStringBuilder fSuffix;
    bool fOverwrite;
    bool fStrong;
    Modifier::Parameters fParameters;
};

/** Identical to {@link ConstantMultiFieldModifier}, but supports currency spacing. */
class U_I18N_API CurrencySpacingEnabledModifier : public ConstantMultiFieldModifier {
  public:
    /** Safe code path */
    CurrencySpacingEnabledModifier(
            const FormattedStringBuilder &prefix,
            const FormattedStringBuilder &suffix,
            bool overwrite,
            bool strong,
            const DecimalFormatSymbols &symbols,
            UErrorCode &status);

    int32_t apply(FormattedStringBuilder &output, int32_t leftIndex, int32_t rightIndex,
                  UErrorCode &status) const override;

    /** Unsafe code path */
    static int32_t
    applyCurrencySpacing(FormattedStringBuilder &output, int32_t prefixStart, int32_t prefixLen,
                         int32_t suffixStart, int32_t suffixLen, const DecimalFormatSymbols &symbols,
                         UErrorCode &status);

  private:
    UnicodeSet fAfterPrefixUnicodeSet;
    UnicodeString fAfterPrefixInsert;
    UnicodeSet fBeforeSuffixUnicodeSet;
    UnicodeString fBeforeSuffixInsert;

    enum EAffix {
        PREFIX, SUFFIX
    };

    enum EPosition {
        IN_CURRENCY, IN_NUMBER
    };

    /** Unsafe code path */
    static int32_t applyCurrencySpacingAffix(FormattedStringBuilder &output, int32_t index, EAffix affix,
                                             const DecimalFormatSymbols &symbols, UErrorCode &status);

    static UnicodeSet
    getUnicodeSet(const DecimalFormatSymbols &symbols, EPosition position, EAffix affix,
                  UErrorCode &status);

    static UnicodeString
    getInsertString(const DecimalFormatSymbols &symbols, EAffix affix, UErrorCode &status);
};

/** A Modifier that does not do anything. */
class U_I18N_API EmptyModifier : public Modifier, public UMemory {
  public:
    explicit EmptyModifier(bool isStrong) : fStrong(isStrong) {}

    int32_t apply(FormattedStringBuilder &output, int32_t leftIndex, int32_t rightIndex,
                  UErrorCode &status) const override {
        (void)output;
        (void)leftIndex;
        (void)rightIndex;
        (void)status;
        return 0;
    }

    int32_t getPrefixLength() const override {
        return 0;
    }

    int32_t getCodePointCount() const override {
        return 0;
    }

    bool isStrong() const override {
        return fStrong;
    }

    bool containsField(Field field) const override {
        (void)field;
        return false;
    }

    void getParameters(Parameters& output) const override {
        output.obj = nullptr;
    }

    bool strictEquals(const Modifier& other) const override {
        return other.getCodePointCount() == 0;
    }

  private:
    bool fStrong;
};

/** An adopting Modifier store that varies by signum but not plural form. */
class U_I18N_API AdoptingSignumModifierStore : public UMemory {
  public:
    virtual ~AdoptingSignumModifierStore();

    AdoptingSignumModifierStore() = default;

    // No copying!
    AdoptingSignumModifierStore(const AdoptingSignumModifierStore &other) = delete;
    AdoptingSignumModifierStore& operator=(const AdoptingSignumModifierStore& other) = delete;

    // Moving is OK
    AdoptingSignumModifierStore(AdoptingSignumModifierStore &&other) noexcept {
        *this = std::move(other);
    }
    AdoptingSignumModifierStore& operator=(AdoptingSignumModifierStore&& other) noexcept;

    /** Take ownership of the Modifier and slot it in at the given Signum. */
    void adoptModifier(Signum signum, const Modifier* mod) {
        U_ASSERT(mods[signum] == nullptr);
        mods[signum] = mod;
    }

    inline const Modifier*& operator[](Signum signum) {
        return mods[signum];
    }
    inline Modifier const* operator[](Signum signum) const {
        return mods[signum];
    }

  private:
    const Modifier* mods[SIGNUM_COUNT] = {};
};

/**
 * This implementation of ModifierStore adopts Modifier pointers.
 */
class U_I18N_API AdoptingModifierStore : public ModifierStore, public UMemory {
  public:
    static constexpr StandardPlural::Form DEFAULT_STANDARD_PLURAL = StandardPlural::OTHER;

    AdoptingModifierStore() = default;

    // No copying!
    AdoptingModifierStore(const AdoptingModifierStore &other) = delete;

    // Moving is OK
    AdoptingModifierStore(AdoptingModifierStore &&other) = default;

    /** Sets the modifiers for a specific plural form. */
    void adoptSignumModifierStore(StandardPlural::Form plural, AdoptingSignumModifierStore other) {
        mods[plural] = std::move(other);
    }

    /** Sets the modifiers for the default plural form. */
    void adoptSignumModifierStoreNoPlural(AdoptingSignumModifierStore other) {
        mods[DEFAULT_STANDARD_PLURAL] = std::move(other);
    }

    /** Returns a reference to the modifier; no ownership change. */
    const Modifier *getModifier(Signum signum, StandardPlural::Form plural) const override {
        const Modifier* modifier = mods[plural][signum];
        if (modifier == nullptr && plural != DEFAULT_STANDARD_PLURAL) {
            modifier = mods[DEFAULT_STANDARD_PLURAL][signum];
        }
        return modifier;
    }

    /** Returns a reference to the modifier; no ownership change. */
    const Modifier *getModifierWithoutPlural(Signum signum) const {
        return mods[DEFAULT_STANDARD_PLURAL][signum];
    }

  private:
    // NOTE: mods is zero-initialized (to nullptr)
    AdoptingSignumModifierStore mods[StandardPlural::COUNT] = {};
};

} // namespace number::impl
U_NAMESPACE_END


#endif //__NUMBER_MODIFIERS_H__

#endif /* #if !UCONFIG_NO_FORMATTING */

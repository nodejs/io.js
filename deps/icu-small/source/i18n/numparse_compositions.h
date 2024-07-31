// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __SOURCE_NUMPARSE_COMPOSITIONS__
#define __SOURCE_NUMPARSE_COMPOSITIONS__

#include "numparse_types.h"

U_NAMESPACE_BEGIN

// Export an explicit template instantiation of the MaybeStackArray that is used as a data member of ArraySeriesMatcher.
// When building DLLs for Windows this is required even though no direct access to the MaybeStackArray leaks out of the i18n library.
// (See digitlst.h, pluralaffix.h, datefmt.h, and others for similar examples.)
#if U_PF_WINDOWS <= U_PLATFORM && U_PLATFORM <= U_PF_CYGWIN
template class U_I18N_API MaybeStackArray<const numparse::impl::NumberParseMatcher*, 3>;
#endif

namespace numparse::impl {

/**
 * Base class for AnyMatcher and SeriesMatcher.
 */
// Exported as U_I18N_API for tests
class U_I18N_API CompositionMatcher : public NumberParseMatcher {
  protected:
    // No construction except by subclasses!
    CompositionMatcher() = default;

    // To be overridden by subclasses (used for iteration):
    virtual const NumberParseMatcher* const* begin() const = 0;

    // To be overridden by subclasses (used for iteration):
    virtual const NumberParseMatcher* const* end() const = 0;
};


// NOTE: AnyMatcher is no longer being used. The previous definition is shown below.
// The implementation can be found in SVN source control, deleted around March 30, 2018.
///**
// * Composes a number of matchers, and succeeds if any of the matchers succeed. Always greedily chooses
// * the first matcher in the list to succeed.
// *
// * NOTE: In C++, this is a base class, unlike ICU4J, which uses a factory-style interface.
// *
// * @author sffc
// * @see SeriesMatcher
// */
//class AnyMatcher : public CompositionMatcher {
//  public:
//    bool match(StringSegment& segment, ParsedNumber& result, UErrorCode& status) const override;
//
//    bool smokeTest(const StringSegment& segment) const override;
//
//    void postProcess(ParsedNumber& result) const override;
//
//  protected:
//    // No construction except by subclasses!
//    AnyMatcher() = default;
//};


/**
 * Composes a number of matchers, running one after another. Matches the input string only if all of the
 * matchers in the series succeed. Performs greedy matches within the context of the series.
 *
 * @author sffc
 * @see AnyMatcher
 */
// Exported as U_I18N_API for tests
class U_I18N_API SeriesMatcher : public CompositionMatcher {
  public:
    bool match(StringSegment& segment, ParsedNumber& result, UErrorCode& status) const override;

    bool smokeTest(const StringSegment& segment) const override;

    void postProcess(ParsedNumber& result) const override;

    virtual int32_t length() const = 0;

  protected:
    // No construction except by subclasses!
    SeriesMatcher() = default;
};

/**
 * An implementation of SeriesMatcher that references an array of matchers.
 *
 * The object adopts the array, but NOT the matchers contained inside the array.
 */
// Exported as U_I18N_API for tests
class U_I18N_API ArraySeriesMatcher : public SeriesMatcher {
  public:
    ArraySeriesMatcher();  // WARNING: Leaves the object in an unusable state

    typedef MaybeStackArray<const NumberParseMatcher*, 3> MatcherArray;

    /** The array is std::move'd */
    ArraySeriesMatcher(MatcherArray& matchers, int32_t matchersLen);

    UnicodeString toString() const override;

    int32_t length() const override;

  protected:
    const NumberParseMatcher* const* begin() const override;

    const NumberParseMatcher* const* end() const override;

  private:
    MatcherArray fMatchers;
    int32_t fMatchersLen;
};

} // namespace numparse::impl

U_NAMESPACE_END

#endif //__SOURCE_NUMPARSE_COMPOSITIONS__
#endif /* #if !UCONFIG_NO_FORMATTING */

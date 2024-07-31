// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 **********************************************************************
 *   Copyright (C) 1999-2016, International Business Machines
 *   Corporation and others.  All Rights Reserved.
 **********************************************************************
 *   Date        Name        Description
 *   11/17/99    aliu        Creation.
 **********************************************************************
 */

#include "utypeinfo.h"  // for 'typeid' to work

#include "unicode/utypes.h"

#if !UCONFIG_NO_TRANSLITERATION

#include "unicode/putil.h"
#include "unicode/translit.h"
#include "unicode/locid.h"
#include "unicode/msgfmt.h"
#include "unicode/rep.h"
#include "unicode/resbund.h"
#include "unicode/unifilt.h"
#include "unicode/uniset.h"
#include "unicode/uscript.h"
#include "unicode/strenum.h"
#include "unicode/utf16.h"
#include "cpdtrans.h"
#include "nultrans.h"
#include "rbt_data.h"
#include "rbt_pars.h"
#include "rbt.h"
#include "transreg.h"
#include "name2uni.h"
#include "nortrans.h"
#include "remtrans.h"
#include "titletrn.h"
#include "tolowtrn.h"
#include "toupptrn.h"
#include "uni2name.h"
#include "brktrans.h"
#include "esctrn.h"
#include "unesctrn.h"
#include "tridpars.h"
#include "anytrans.h"
#include "util.h"
#include "hash.h"
#include "mutex.h"
#include "ucln_in.h"
#include "uassert.h"
#include "cmemory.h"
#include "cstring.h"
#include "uinvchar.h"

static const char16_t TARGET_SEP  = 0x002D; /*-*/
static const char16_t ID_DELIM    = 0x003B; /*;*/
static const char16_t VARIANT_SEP = 0x002F; // '/'

/**
 * Prefix for resource bundle key for the display name for a
 * transliterator.  The ID is appended to this to form the key.
 * The resource bundle value should be a String.
 */
static const char RB_DISPLAY_NAME_PREFIX[] = "%Translit%%";

/**
 * Prefix for resource bundle key for the display name for a
 * transliterator SCRIPT.  The ID is appended to this to form the key.
 * The resource bundle value should be a String.
 */
static const char RB_SCRIPT_DISPLAY_NAME_PREFIX[] = "%Translit%";

/**
 * Resource bundle key for display name pattern.
 * The resource bundle value should be a String forming a
 * MessageFormat pattern, e.g.:
 * "{0,choice,0#|1#{1} Transliterator|2#{1} to {2} Transliterator}".
 */
static const char RB_DISPLAY_NAME_PATTERN[] = "TransliteratorNamePattern";

/**
 * Resource bundle key for the list of RuleBasedTransliterator IDs.
 * The resource bundle value should be a String[] with each element
 * being a valid ID.  The ID will be appended to RB_RULE_BASED_PREFIX
 * to obtain the class name in which the RB_RULE key will be sought.
 */
static const char RB_RULE_BASED_IDS[] = "RuleBasedTransliteratorIDs";

/**
 * The mutex controlling access to registry object.
 */
static icu::UMutex registryMutex;

/**
 * System transliterator registry; non-null when initialized.
 */
static icu::TransliteratorRegistry* registry = nullptr;

// Macro to check/initialize the registry. ONLY USE WITHIN
// MUTEX. Avoids function call when registry is initialized.
#define HAVE_REGISTRY(status) (registry!=0 || initializeRegistry(status))

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_ABSTRACT_RTTI_IMPLEMENTATION(Transliterator)

/**
 * Return true if the given UTransPosition is valid for text of
 * the given length.
 */
static inline UBool positionIsValid(UTransPosition& index, int32_t len) {
    return !(index.contextStart < 0 ||
             index.start < index.contextStart ||
             index.limit < index.start ||
             index.contextLimit < index.limit ||
             len < index.contextLimit);
}

/**
 * Default constructor.
 * @param theID the string identifier for this transliterator
 * @param theFilter the filter.  Any character for which
 * <tt>filter.contains()</tt> returns <tt>false</tt> will not be
 * altered by this transliterator.  If <tt>filter</tt> is
 * <tt>null</tt> then no filtering is applied.
 */
Transliterator::Transliterator(const UnicodeString& theID,
                               UnicodeFilter* adoptedFilter) :
    UObject(), ID(theID), filter(adoptedFilter),
    maximumContextLength(0)
{
    // NUL-terminate the ID string, which is a non-aliased copy.
    ID.append((char16_t)0);
    ID.truncate(ID.length()-1);
}

/**
 * Destructor.
 */
Transliterator::~Transliterator() {
    delete filter;
}

/**
 * Copy constructor.
 */
Transliterator::Transliterator(const Transliterator& other) :
    UObject(other), ID(other.ID), filter(nullptr),
    maximumContextLength(other.maximumContextLength)
{
    // NUL-terminate the ID string, which is a non-aliased copy.
    ID.append((char16_t)0);
    ID.truncate(ID.length()-1);

    if (other.filter != nullptr) {
        // We own the filter, so we must have our own copy
        filter = other.filter->clone();
    }
}

Transliterator* Transliterator::clone() const {
    return nullptr;
}

/**
 * Assignment operator.
 */
Transliterator& Transliterator::operator=(const Transliterator& other) {
    if (this == &other) { return *this; }  // self-assignment: no-op
    ID = other.ID;
    // NUL-terminate the ID string
    ID.getTerminatedBuffer();

    maximumContextLength = other.maximumContextLength;
    adoptFilter(other.filter == nullptr ? nullptr : other.filter->clone());
    return *this;
}

/**
 * Transliterates a segment of a string.  <code>Transliterator</code> API.
 * @param text the string to be transliterated
 * @param start the beginning index, inclusive; <code>0 <= start
 * <= limit</code>.
 * @param limit the ending index, exclusive; <code>start <= limit
 * <= text.length()</code>.
 * @return the new limit index, or -1
 */
int32_t Transliterator::transliterate(Replaceable& text,
                                      int32_t start, int32_t limit) const {
    if (start < 0 ||
        limit < start ||
        text.length() < limit) {
        return -1;
    }

    UTransPosition offsets;
    offsets.contextStart= start;
    offsets.contextLimit = limit;
    offsets.start = start;
    offsets.limit = limit;
    filteredTransliterate(text, offsets, false, true);
    return offsets.limit;
}

/**
 * Transliterates an entire string in place. Convenience method.
 * @param text the string to be transliterated
 */
void Transliterator::transliterate(Replaceable& text) const {
    transliterate(text, 0, text.length());
}

/**
 * Transliterates the portion of the text buffer that can be
 * transliterated unambiguosly after new text has been inserted,
 * typically as a result of a keyboard event.  The new text in
 * <code>insertion</code> will be inserted into <code>text</code>
 * at <code>index.contextLimit</code>, advancing
 * <code>index.contextLimit</code> by <code>insertion.length()</code>.
 * Then the transliterator will try to transliterate characters of
 * <code>text</code> between <code>index.start</code> and
 * <code>index.contextLimit</code>.  Characters before
 * <code>index.start</code> will not be changed.
 *
 * <p>Upon return, values in <code>index</code> will be updated.
 * <code>index.contextStart</code> will be advanced to the first
 * character that future calls to this method will read.
 * <code>index.start</code> and <code>index.contextLimit</code> will
 * be adjusted to delimit the range of text that future calls to
 * this method may change.
 *
 * <p>Typical usage of this method begins with an initial call
 * with <code>index.contextStart</code> and <code>index.contextLimit</code>
 * set to indicate the portion of <code>text</code> to be
 * transliterated, and <code>index.start == index.contextStart</code>.
 * Thereafter, <code>index</code> can be used without
 * modification in future calls, provided that all changes to
 * <code>text</code> are made via this method.
 *
 * <p>This method assumes that future calls may be made that will
 * insert new text into the buffer.  As a result, it only performs
 * unambiguous transliterations.  After the last call to this
 * method, there may be untransliterated text that is waiting for
 * more input to resolve an ambiguity.  In order to perform these
 * pending transliterations, clients should call {@link
 * #finishKeyboardTransliteration} after the last call to this
 * method has been made.
 * 
 * @param text the buffer holding transliterated and untransliterated text
 * @param index an array of three integers.
 *
 * <ul><li><code>index.contextStart</code>: the beginning index,
 * inclusive; <code>0 <= index.contextStart <= index.contextLimit</code>.
 *
 * <li><code>index.contextLimit</code>: the ending index, exclusive;
 * <code>index.contextStart <= index.contextLimit <= text.length()</code>.
 * <code>insertion</code> is inserted at
 * <code>index.contextLimit</code>.
 *
 * <li><code>index.start</code>: the next character to be
 * considered for transliteration; <code>index.contextStart <=
 * index.start <= index.contextLimit</code>.  Characters before
 * <code>index.start</code> will not be changed by future calls
 * to this method.</ul>
 *
 * @param insertion text to be inserted and possibly
 * transliterated into the translation buffer at
 * <code>index.contextLimit</code>.  If <code>null</code> then no text
 * is inserted.
 * @see #START
 * @see #LIMIT
 * @see #CURSOR
 * @see #handleTransliterate
 * @exception IllegalArgumentException if <code>index</code>
 * is invalid
 */
void Transliterator::transliterate(Replaceable& text,
                                   UTransPosition& index,
                                   const UnicodeString& insertion,
                                   UErrorCode &status) const {
    _transliterate(text, index, &insertion, status);
}

/**
 * Transliterates the portion of the text buffer that can be
 * transliterated unambiguosly after a new character has been
 * inserted, typically as a result of a keyboard event.  This is a
 * convenience method; see {@link
 * #transliterate(Replaceable, int[], String)} for details.
 * @param text the buffer holding transliterated and
 * untransliterated text
 * @param index an array of three integers.  See {@link
 * #transliterate(Replaceable, int[], String)}.
 * @param insertion text to be inserted and possibly
 * transliterated into the translation buffer at
 * <code>index.contextLimit</code>.
 * @see #transliterate(Replaceable, int[], String)
 */
void Transliterator::transliterate(Replaceable& text,
                                   UTransPosition& index,
                                   UChar32 insertion,
                                   UErrorCode& status) const {
    UnicodeString str(insertion);
    _transliterate(text, index, &str, status);
}

/**
 * Transliterates the portion of the text buffer that can be
 * transliterated unambiguosly.  This is a convenience method; see
 * {@link #transliterate(Replaceable, int[], String)} for
 * details.
 * @param text the buffer holding transliterated and
 * untransliterated text
 * @param index an array of three integers.  See {@link
 * #transliterate(Replaceable, int[], String)}.
 * @see #transliterate(Replaceable, int[], String)
 */
void Transliterator::transliterate(Replaceable& text,
                                   UTransPosition& index,
                                   UErrorCode& status) const {
    _transliterate(text, index, nullptr, status);
}

/**
 * Finishes any pending transliterations that were waiting for
 * more characters.  Clients should call this method as the last
 * call after a sequence of one or more calls to
 * <code>transliterate()</code>.
 * @param text the buffer holding transliterated and
 * untransliterated text.
 * @param index the array of indices previously passed to {@link
 * #transliterate}
 */
void Transliterator::finishTransliteration(Replaceable& text,
                                           UTransPosition& index) const {
    if (!positionIsValid(index, text.length())) {
        return;
    }

    filteredTransliterate(text, index, false, true);
}

/**
 * This internal method does keyboard transliteration.  If the
 * 'insertion' is non-null then we append it to 'text' before
 * proceeding.  This method calls through to the pure virtual
 * framework method handleTransliterate() to do the actual
 * work.
 */
void Transliterator::_transliterate(Replaceable& text,
                                    UTransPosition& index,
                                    const UnicodeString* insertion,
                                    UErrorCode &status) const {
    if (U_FAILURE(status)) {
        return;
    }

    if (!positionIsValid(index, text.length())) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

//    int32_t originalStart = index.contextStart;
    if (insertion != nullptr) {
        text.handleReplaceBetween(index.limit, index.limit, *insertion);
        index.limit += insertion->length();
        index.contextLimit += insertion->length();
    }

    if (index.limit > 0 &&
        U16_IS_LEAD(text.charAt(index.limit - 1))) {
        // Oops, there is a dangling lead surrogate in the buffer.
        // This will break most transliterators, since they will
        // assume it is part of a pair.  Don't transliterate until
        // more text comes in.
        return;
    }

    filteredTransliterate(text, index, true, true);

#if 0
    // TODO
    // I CAN'T DO what I'm attempting below now that the Kleene star
    // operator is supported.  For example, in the rule

    //   ([:Lu:]+) { x } > $1;

    // what is the maximum context length?  getMaximumContextLength()
    // will return 1, but this is just the length of the ante context
    // part of the pattern string -- 1 character, which is a standin
    // for a Quantifier, which contains a StringMatcher, which
    // contains a UnicodeSet.

    // There is a complicated way to make this work again, and that's
    // to add a "maximum left context" protocol into the
    // UnicodeMatcher hierarchy.  At present I'm not convinced this is
    // worth it.

    // ---

    // The purpose of the code below is to keep the context small
    // while doing incremental transliteration.  When part of the left
    // context (between contextStart and start) is no longer needed,
    // we try to advance contextStart past that portion.  We use the
    // maximum context length to do so.
    int32_t newCS = index.start;
    int32_t n = getMaximumContextLength();
    while (newCS > originalStart && n-- > 0) {
        --newCS;
        newCS -= U16_LENGTH(text.char32At(newCS)) - 1;
    }
    index.contextStart = uprv_max(newCS, originalStart);
#endif
}

/**
 * This method breaks up the input text into runs of unfiltered
 * characters.  It passes each such run to
 * <subclass>.handleTransliterate().  Subclasses that can handle the
 * filter logic more efficiently themselves may override this method.
 *
 * All transliteration calls in this class go through this method.
 */
void Transliterator::filteredTransliterate(Replaceable& text,
                                           UTransPosition& index,
                                           UBool incremental,
                                           UBool rollback) const {
    // Short circuit path for transliterators with no filter in
    // non-incremental mode.
    if (filter == nullptr && !rollback) {
        handleTransliterate(text, index, incremental);
        return;
    }

    //----------------------------------------------------------------------
    // This method processes text in two groupings:
    //
    // RUNS -- A run is a contiguous group of characters which are contained
    // in the filter for this transliterator (filter.contains(ch) == true).
    // Text outside of runs may appear as context but it is not modified.
    // The start and limit Position values are narrowed to each run.
    //
    // PASSES (incremental only) -- To make incremental mode work correctly,
    // each run is broken up into n passes, where n is the length (in code
    // points) of the run.  Each pass contains the first n characters.  If a
    // pass is completely transliterated, it is committed, and further passes
    // include characters after the committed text.  If a pass is blocked,
    // and does not transliterate completely, then this method rolls back
    // the changes made during the pass, extends the pass by one code point,
    // and tries again.
    //----------------------------------------------------------------------
    
    // globalLimit is the limit value for the entire operation.  We
    // set index.limit to the end of each unfiltered run before
    // calling handleTransliterate(), so we need to maintain the real
    // value of index.limit here.  After each transliteration, we
    // update globalLimit for insertions or deletions that have
    // happened.
    int32_t globalLimit = index.limit;
    
    // If there is a non-null filter, then break the input text up.  Say the
    // input text has the form:
    //   xxxabcxxdefxx
    // where 'x' represents a filtered character (filter.contains('x') ==
    // false).  Then we break this up into:
    //   xxxabc xxdef xx
    // Each pass through the loop consumes a run of filtered
    // characters (which are ignored) and a subsequent run of
    // unfiltered characters (which are transliterated).
    
    for (;;) {

        if (filter != nullptr) {
            // Narrow the range to be transliterated to the first segment
            // of unfiltered characters at or after index.start.

            // Advance past filtered chars
            UChar32 c;
            while (index.start < globalLimit &&
                   !filter->contains(c=text.char32At(index.start))) {
                index.start += U16_LENGTH(c);
            }

            // Find the end of this run of unfiltered chars
            index.limit = index.start;
            while (index.limit < globalLimit &&
                   filter->contains(c=text.char32At(index.limit))) {
                index.limit += U16_LENGTH(c);
            }
        }

        // Check to see if the unfiltered run is empty.  This only
        // happens at the end of the string when all the remaining
        // characters are filtered.
        if (index.limit == index.start) {
            // assert(index.start == globalLimit);
            break;
        }

        // Is this run incremental?  If there is additional
        // filtered text (if limit < globalLimit) then we pass in
        // an incremental value of false to force the subclass to
        // complete the transliteration for this run.
        UBool isIncrementalRun =
            (index.limit < globalLimit ? false : incremental);
        
        int32_t delta;

        // Implement rollback.  To understand the need for rollback,
        // consider the following transliterator:
        //
        //  "t" is "a > A;"
        //  "u" is "A > b;"
        //  "v" is a compound of "t; NFD; u" with a filter [:Ll:]
        //
        // Now apply "c" to the input text "a".  The result is "b".  But if
        // the transliteration is done incrementally, then the NFD holds
        // things up after "t" has already transformed "a" to "A".  When
        // finishTransliterate() is called, "A" is _not_ processed because
        // it gets excluded by the [:Ll:] filter, and the end result is "A"
        // -- incorrect.  The problem is that the filter is applied to a
        // partially-transliterated result, when we only want it to apply to
        // input text.  Although this example hinges on a compound
        // transliterator containing NFD and a specific filter, it can
        // actually happen with any transliterator which may do a partial
        // transformation in incremental mode into characters outside its
        // filter.
        //
        // To handle this, when in incremental mode we supply characters to
        // handleTransliterate() in several passes.  Each pass adds one more
        // input character to the input text.  That is, for input "ABCD", we
        // first try "A", then "AB", then "ABC", and finally "ABCD".  If at
        // any point we block (upon return, start < limit) then we roll
        // back.  If at any point we complete the run (upon return start ==
        // limit) then we commit that run.

        if (rollback && isIncrementalRun) {

            int32_t runStart = index.start;
            int32_t runLimit = index.limit;
            int32_t runLength =  runLimit - runStart;

            // Make a rollback copy at the end of the string
            int32_t rollbackOrigin = text.length();
            text.copy(runStart, runLimit, rollbackOrigin);

            // Variables reflecting the commitment of completely
            // transliterated text.  passStart is the runStart, advanced
            // past committed text.  rollbackStart is the rollbackOrigin,
            // advanced past rollback text that corresponds to committed
            // text.
            int32_t passStart = runStart;
            int32_t rollbackStart = rollbackOrigin;

            // The limit for each pass; we advance by one code point with
            // each iteration.
            int32_t passLimit = index.start;

            // Total length, in 16-bit code units, of uncommitted text.
            // This is the length to be rolled back.
            int32_t uncommittedLength = 0;

            // Total delta (change in length) for all passes
            int32_t totalDelta = 0;

            // PASS MAIN LOOP -- Start with a single character, and extend
            // the text by one character at a time.  Roll back partial
            // transliterations and commit complete transliterations.
            for (;;) {
                // Length of additional code point, either one or two
                int32_t charLength = U16_LENGTH(text.char32At(passLimit));
                passLimit += charLength;
                if (passLimit > runLimit) {
                    break;
                }
                uncommittedLength += charLength;

                index.limit = passLimit;

                // Delegate to subclass for actual transliteration.  Upon
                // return, start will be updated to point after the
                // transliterated text, and limit and contextLimit will be
                // adjusted for length changes.
                handleTransliterate(text, index, true);

                delta = index.limit - passLimit; // change in length

                // We failed to completely transliterate this pass.
                // Roll back the text.  Indices remain unchanged; reset
                // them where necessary.
                if (index.start != index.limit) {
                    // Find the rollbackStart, adjusted for length changes
                    // and the deletion of partially transliterated text.
                    int32_t rs = rollbackStart + delta - (index.limit - passStart);

                    // Delete the partially transliterated text
                    text.handleReplaceBetween(passStart, index.limit, UnicodeString());

                    // Copy the rollback text back
                    text.copy(rs, rs + uncommittedLength, passStart);

                    // Restore indices to their original values
                    index.start = passStart;
                    index.limit = passLimit;
                    index.contextLimit -= delta;
                }

                // We did completely transliterate this pass.  Update the
                // commit indices to record how far we got.  Adjust indices
                // for length change.
                else {
                    // Move the pass indices past the committed text.
                    passStart = passLimit = index.start;

                    // Adjust the rollbackStart for length changes and move
                    // it past the committed text.  All characters we've
                    // processed to this point are committed now, so zero
                    // out the uncommittedLength.
                    rollbackStart += delta + uncommittedLength;
                    uncommittedLength = 0;

                    // Adjust indices for length changes.
                    runLimit += delta;
                    totalDelta += delta;
                }
            }

            // Adjust overall limit and rollbackOrigin for insertions and
            // deletions.  Don't need to worry about contextLimit because
            // handleTransliterate() maintains that.
            rollbackOrigin += totalDelta;
            globalLimit += totalDelta;

            // Delete the rollback copy
            text.handleReplaceBetween(rollbackOrigin, rollbackOrigin + runLength, UnicodeString());

            // Move start past committed text
            index.start = passStart;
        }

        else {
            // Delegate to subclass for actual transliteration.
            int32_t limit = index.limit;
            handleTransliterate(text, index, isIncrementalRun);
            delta = index.limit - limit; // change in length

            // In a properly written transliterator, start == limit after
            // handleTransliterate() returns when incremental is false.
            // Catch cases where the subclass doesn't do this, and throw
            // an exception.  (Just pinning start to limit is a bad idea,
            // because what's probably happening is that the subclass
            // isn't transliterating all the way to the end, and it should
            // in non-incremental mode.)
            if (!incremental && index.start != index.limit) {
                // We can't throw an exception, so just fudge things
                index.start = index.limit;
            }

            // Adjust overall limit for insertions/deletions.  Don't need
            // to worry about contextLimit because handleTransliterate()
            // maintains that.
            globalLimit += delta;
        }

        if (filter == nullptr || isIncrementalRun) {
            break;
        }

        // If we did completely transliterate this
        // run, then repeat with the next unfiltered run.
    }

    // Start is valid where it is.  Limit needs to be put back where
    // it was, modulo adjustments for deletions/insertions.
    index.limit = globalLimit;
}

void Transliterator::filteredTransliterate(Replaceable& text,
                                           UTransPosition& index,
                                           UBool incremental) const {
    filteredTransliterate(text, index, incremental, false);
}

/**
 * Method for subclasses to use to set the maximum context length.
 * @see #getMaximumContextLength
 */
void Transliterator::setMaximumContextLength(int32_t maxContextLength) {
    maximumContextLength = maxContextLength;
}

/**
 * Returns a programmatic identifier for this transliterator.
 * If this identifier is passed to <code>getInstance()</code>, it
 * will return this object, if it has been registered.
 * @see #registerInstance
 * @see #getAvailableIDs
 */
const UnicodeString& Transliterator::getID() const {
    return ID;
}

/**
 * Returns a name for this transliterator that is appropriate for
 * display to the user in the default locale.  See {@link
 * #getDisplayName(Locale)} for details.
 */
UnicodeString& U_EXPORT2 Transliterator::getDisplayName(const UnicodeString& ID,
                                              UnicodeString& result) {
    return getDisplayName(ID, Locale::getDefault(), result);
}

/**
 * Returns a name for this transliterator that is appropriate for
 * display to the user in the given locale.  This name is taken
 * from the locale resource data in the standard manner of the
 * <code>java.text</code> package.
 *
 * <p>If no localized names exist in the system resource bundles,
 * a name is synthesized using a localized
 * <code>MessageFormat</code> pattern from the resource data.  The
 * arguments to this pattern are an integer followed by one or two
 * strings.  The integer is the number of strings, either 1 or 2.
 * The strings are formed by splitting the ID for this
 * transliterator at the first TARGET_SEP.  If there is no TARGET_SEP, then the
 * entire ID forms the only string.
 * @param inLocale the Locale in which the display name should be
 * localized.
 * @see java.text.MessageFormat
 */
UnicodeString& U_EXPORT2 Transliterator::getDisplayName(const UnicodeString& id,
                                              const Locale& inLocale,
                                              UnicodeString& result) {
    UErrorCode status = U_ZERO_ERROR;

    ResourceBundle bundle(U_ICUDATA_TRANSLIT, inLocale, status);

    // Suspend checking status until later...

    result.truncate(0);

    // Normalize the ID
    UnicodeString source, target, variant;
    UBool sawSource;
    TransliteratorIDParser::IDtoSTV(id, source, target, variant, sawSource);
    if (target.length() < 1) {
        // No target; malformed id
        return result;
    }
    if (variant.length() > 0) { // Change "Foo" to "/Foo"
        variant.insert(0, VARIANT_SEP);
    }
    UnicodeString ID(source);
    ID.append(TARGET_SEP).append(target).append(variant);

    // build the char* key
    if (uprv_isInvariantUString(ID.getBuffer(), ID.length())) {
        char key[200];
        uprv_strcpy(key, RB_DISPLAY_NAME_PREFIX);
        int32_t length=(int32_t)uprv_strlen(RB_DISPLAY_NAME_PREFIX);
        ID.extract(0, (int32_t)(sizeof(key)-length), key+length, (int32_t)(sizeof(key)-length), US_INV);

        // Try to retrieve a UnicodeString from the bundle.
        UnicodeString resString = bundle.getStringEx(key, status);

        if (U_SUCCESS(status) && resString.length() != 0) {
            return result = resString; // [sic] assign & return
        }

#if !UCONFIG_NO_FORMATTING
        // We have failed to get a name from the locale data.  This is
        // typical, since most transliterators will not have localized
        // name data.  The next step is to retrieve the MessageFormat
        // pattern from the locale data and to use it to synthesize the
        // name from the ID.

        status = U_ZERO_ERROR;
        resString = bundle.getStringEx(RB_DISPLAY_NAME_PATTERN, status);

        if (U_SUCCESS(status) && resString.length() != 0) {
            MessageFormat msg(resString, inLocale, status);
            // Suspend checking status until later...

            // We pass either 2 or 3 Formattable objects to msg.
            Formattable args[3];
            int32_t nargs;
            args[0].setLong(2); // # of args to follow
            args[1].setString(source);
            args[2].setString(target);
            nargs = 3;

            // Use display names for the scripts, if they exist
            UnicodeString s;
            length=(int32_t)uprv_strlen(RB_SCRIPT_DISPLAY_NAME_PREFIX);
            for (int j=1; j<=2; ++j) {
                status = U_ZERO_ERROR;
                uprv_strcpy(key, RB_SCRIPT_DISPLAY_NAME_PREFIX);
                args[j].getString(s);
                if (uprv_isInvariantUString(s.getBuffer(), s.length())) {
                    s.extract(0, sizeof(key)-length-1, key+length, (int32_t)sizeof(key)-length-1, US_INV);

                    resString = bundle.getStringEx(key, status);

                    if (U_SUCCESS(status)) {
                        args[j] = resString;
                    }
                }
            }

            status = U_ZERO_ERROR;
            FieldPosition pos; // ignored by msg
            msg.format(args, nargs, result, pos, status);
            if (U_SUCCESS(status)) {
                result.append(variant);
                return result;
            }
        }
#endif
    }

    // We should not reach this point unless there is something
    // wrong with the build or the RB_DISPLAY_NAME_PATTERN has
    // been deleted from the root RB_LOCALE_ELEMENTS resource.
    result = ID;
    return result;
}

/**
 * Returns the filter used by this transliterator, or <tt>null</tt>
 * if this transliterator uses no filter.  Caller musn't delete
 * the result!
 */
const UnicodeFilter* Transliterator::getFilter() const {
    return filter;
}

/**
 * Returns the filter used by this transliterator, or
 * <tt>nullptr</tt> if this transliterator uses no filter.  The
 * caller must eventually delete the result.  After this call,
 * this transliterator's filter is set to <tt>nullptr</tt>.
 */
UnicodeFilter* Transliterator::orphanFilter() {
    UnicodeFilter *result = filter;
    filter = nullptr;
    return result;
}

/**
 * Changes the filter used by this transliterator.  If the filter
 * is set to <tt>null</tt> then no filtering will occur.
 *
 * <p>Callers must take care if a transliterator is in use by
 * multiple threads.  The filter should not be changed by one
 * thread while another thread may be transliterating.
 */
void Transliterator::adoptFilter(UnicodeFilter* filterToAdopt) {
    delete filter;
    filter = filterToAdopt;
}

/**
 * Returns this transliterator's inverse.  See the class
 * documentation for details.  This implementation simply inverts
 * the two entities in the ID and attempts to retrieve the
 * resulting transliterator.  That is, if <code>getID()</code>
 * returns "A-B", then this method will return the result of
 * <code>getInstance("B-A")</code>, or <code>null</code> if that
 * call fails.
 *
 * <p>This method does not take filtering into account.  The
 * returned transliterator will have no filter.
 *
 * <p>Subclasses with knowledge of their inverse may wish to
 * override this method.
 *
 * @return a transliterator that is an inverse, not necessarily
 * exact, of this transliterator, or <code>null</code> if no such
 * transliterator is registered.
 * @see #registerInstance
 */
Transliterator* Transliterator::createInverse(UErrorCode& status) const {
    UParseError parseError;
    return Transliterator::createInstance(ID, UTRANS_REVERSE,parseError,status);
}

Transliterator* U_EXPORT2
Transliterator::createInstance(const UnicodeString& ID,
                                UTransDirection dir,
                                UErrorCode& status)
{
    UParseError parseError;
    return createInstance(ID, dir, parseError, status);
}

/**
 * Returns a <code>Transliterator</code> object given its ID.
 * The ID must be either a system transliterator ID or a ID registered
 * using <code>registerInstance()</code>.
 *
 * @param ID a valid ID, as enumerated by <code>getAvailableIDs()</code>
 * @return A <code>Transliterator</code> object with the given ID
 * @see #registerInstance
 * @see #getAvailableIDs
 * @see #getID
 */
Transliterator* U_EXPORT2
Transliterator::createInstance(const UnicodeString& ID,
                                UTransDirection dir,
                                UParseError& parseError,
                                UErrorCode& status)
{
    if (U_FAILURE(status)) {
        return nullptr;
    }

    UnicodeString canonID;
    UVector list(status);
    if (U_FAILURE(status)) {
        return nullptr;
    }

    UnicodeSet* globalFilter = nullptr;
    // TODO add code for parseError...currently unused, but
    // later may be used by parsing code...
    if (!TransliteratorIDParser::parseCompoundID(ID, dir, canonID, list, globalFilter)) {
        status = U_INVALID_ID;
        delete globalFilter;
        return nullptr;
    }
    LocalPointer<UnicodeSet> lpGlobalFilter(globalFilter);
    
    TransliteratorIDParser::instantiateList(list, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    
    U_ASSERT(list.size() > 0);
    Transliterator* t = nullptr;
    
    if (list.size() > 1 || canonID.indexOf(ID_DELIM) >= 0) {
        // [NOTE: If it's a compoundID, we instantiate a CompoundTransliterator even if it only
        // has one child transliterator.  This is so that toRules() will return the right thing
        // (without any inactive ID), but our main ID still comes out correct.  That is, if we
        // instantiate "(Lower);Latin-Greek;", we want the rules to come out as "::Latin-Greek;"
        // even though the ID is "(Lower);Latin-Greek;".
        t = new CompoundTransliterator(list, parseError, status);
    }
    else {
        t = (Transliterator*)list.elementAt(0);
    }
    // Check null pointer
    if (t != nullptr) {
        t->setID(canonID);
        if (lpGlobalFilter.isValid()) {
            t->adoptFilter(lpGlobalFilter.orphan());
        }
    }
    else if (U_SUCCESS(status)) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
    return t;
}

/**
 * Create a transliterator from a basic ID.  This is an ID
 * containing only the forward direction source, target, and
 * variant.
 * @param id a basic ID of the form S-T or S-T/V.
 * @return a newly created Transliterator or null if the ID is
 * invalid.
 */
Transliterator* Transliterator::createBasicInstance(const UnicodeString& id,
                                                    const UnicodeString* canon) {
    UParseError pe;
    UErrorCode ec = U_ZERO_ERROR;
    TransliteratorAlias* alias = nullptr;
    Transliterator* t = nullptr;

    umtx_lock(&registryMutex);
    if (HAVE_REGISTRY(ec)) {
        t = registry->get(id, alias, ec);
    }
    umtx_unlock(&registryMutex);

    if (U_FAILURE(ec)) {
        delete t;
        delete alias;
        return nullptr;
    }

    // We may have not gotten a transliterator:  Because we can't
    // instantiate a transliterator from inside TransliteratorRegistry::
    // get() (that would deadlock), we sometimes pass back an alias.  This
    // contains the data we need to finish the instantiation outside the
    // registry mutex.  The alias may, in turn, generate another alias, so
    // we handle aliases in a loop.  The max times through the loop is two.
    // [alan]
    while (alias != nullptr) {
        U_ASSERT(t==0);
        // Rule-based aliases are handled with TransliteratorAlias::
        // parse(), followed by TransliteratorRegistry::reget().
        // Other aliases are handled with TransliteratorAlias::create().
        if (alias->isRuleBased()) {
            // Step 1. parse
            TransliteratorParser parser(ec);
            alias->parse(parser, pe, ec);
            delete alias;
            alias = nullptr;

            // Step 2. reget
            umtx_lock(&registryMutex);
            if (HAVE_REGISTRY(ec)) {
                t = registry->reget(id, parser, alias, ec);
            }
            umtx_unlock(&registryMutex);

            // Step 3. Loop back around!
        } else {
            t = alias->create(pe, ec);
            delete alias;
            alias = nullptr;
            break;
        }
        if (U_FAILURE(ec)) {
            delete t;
            delete alias;
            t = nullptr;
            break;
        }
    }

    if (t != nullptr && canon != nullptr) {
        t->setID(*canon);
    }

    return t;
}

/**
 * Returns a <code>Transliterator</code> object constructed from
 * the given rule string.  This will be a RuleBasedTransliterator,
 * if the rule string contains only rules, or a
 * CompoundTransliterator, if it contains ID blocks, or a
 * NullTransliterator, if it contains ID blocks which parse as
 * empty for the given direction.
 */
Transliterator* U_EXPORT2
Transliterator::createFromRules(const UnicodeString& ID,
                                const UnicodeString& rules,
                                UTransDirection dir,
                                UParseError& parseError,
                                UErrorCode& status)
{
    Transliterator* t = nullptr;

    TransliteratorParser parser(status);
    parser.parse(rules, dir, parseError, status);

    if (U_FAILURE(status)) {
        return nullptr;
    }

    // NOTE: The logic here matches that in TransliteratorRegistry.
    if (parser.idBlockVector.size() == 0 && parser.dataVector.size() == 0) {
        t = new NullTransliterator();
    }
    else if (parser.idBlockVector.size() == 0 && parser.dataVector.size() == 1) {
        t = new RuleBasedTransliterator(ID, (TransliterationRuleData*)parser.dataVector.orphanElementAt(0), true);
    }
    else if (parser.idBlockVector.size() == 1 && parser.dataVector.size() == 0) {
        // idBlock, no data -- this is an alias.  The ID has
        // been munged from reverse into forward mode, if
        // necessary, so instantiate the ID in the forward
        // direction.
        if (parser.compoundFilter != nullptr) {
            UnicodeString filterPattern;
            parser.compoundFilter->toPattern(filterPattern, false);
            t = createInstance(filterPattern + UnicodeString(ID_DELIM)
                    + *((UnicodeString*)parser.idBlockVector.elementAt(0)), UTRANS_FORWARD, parseError, status);
        }
        else
            t = createInstance(*((UnicodeString*)parser.idBlockVector.elementAt(0)), UTRANS_FORWARD, parseError, status);


        if (t != nullptr) {
            t->setID(ID);
        }
    }
    else {
        UVector transliterators(status);
        // TODO ICU-21701 missing U_FAILURE check here.
        //      Error and nullptr checking through this whole block looks suspect.
        int32_t passNumber = 1;

        int32_t limit = parser.idBlockVector.size();
        if (parser.dataVector.size() > limit)
            limit = parser.dataVector.size();

        for (int32_t i = 0; i < limit; i++) {
            if (i < parser.idBlockVector.size()) {
                UnicodeString* idBlock = (UnicodeString*)parser.idBlockVector.elementAt(i);
                if (!idBlock->isEmpty()) {
                    Transliterator* temp = createInstance(*idBlock, UTRANS_FORWARD, parseError, status);
                    if (U_FAILURE(status)) {
                        delete temp;
                        return nullptr;
                    }
                    if (temp != nullptr && typeid(*temp) != typeid(NullTransliterator)) {
                        transliterators.addElement(temp, status);
                        if (U_FAILURE(status)) {
                            delete temp;
                            return nullptr;
                        }
                    } else {
                        delete temp;
                    }
                }
            }
            if (!parser.dataVector.isEmpty()) {
                TransliterationRuleData* data = (TransliterationRuleData*)parser.dataVector.orphanElementAt(0);
                // TODO: Should passNumber be turned into a decimal-string representation (1 -> "1")?
                RuleBasedTransliterator* temprbt = new RuleBasedTransliterator(UnicodeString(CompoundTransliterator::PASS_STRING) + UnicodeString(passNumber++),
                        data, true);
                // Check if nullptr before adding it to transliterators to avoid future usage of nullptr pointer.
                if (temprbt == nullptr) {
                    if (U_SUCCESS(status)) {
                        status = U_MEMORY_ALLOCATION_ERROR;
                    }
                    return t;
                }
                transliterators.addElement(temprbt, status);
                if (U_FAILURE(status)) {
                    delete temprbt;
                    return t;
                }
                // TODO: ICU-21701 the transliterators vector will leak its contents if anything goes wrong.
                //       Under normal operation, the CompoundTransliterator constructor adopts the
                //       the contents of the vector.
            }
        }

        t = new CompoundTransliterator(transliterators, passNumber - 1, parseError, status);
        // Null pointer check
        if (t != nullptr) {
            t->setID(ID);
            t->adoptFilter(parser.orphanCompoundFilter());
        }
    }
    if (U_SUCCESS(status) && t == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
    return t;
}

UnicodeString& Transliterator::toRules(UnicodeString& rulesSource,
                                       UBool escapeUnprintable) const {
    // The base class implementation of toRules munges the ID into
    // the correct format.  That is: foo => ::foo
    if (escapeUnprintable) {
        rulesSource.truncate(0);
        UnicodeString id = getID();
        for (int32_t i=0; i<id.length();) {
            UChar32 c = id.char32At(i);
            if (!ICU_Utility::escapeUnprintable(rulesSource, c)) {
                rulesSource.append(c);
            }
            i += U16_LENGTH(c);
        }
    } else {
        rulesSource = getID();
    }
    // KEEP in sync with rbt_pars
    rulesSource.insert(0, UNICODE_STRING_SIMPLE("::"));
    rulesSource.append(ID_DELIM);
    return rulesSource;
}

int32_t Transliterator::countElements() const {
    const CompoundTransliterator* ct = dynamic_cast<const CompoundTransliterator*>(this);
    return ct != nullptr ? ct->getCount() : 0;
}

const Transliterator& Transliterator::getElement(int32_t index, UErrorCode& ec) const {
    if (U_FAILURE(ec)) {
        return *this;
    }
    const CompoundTransliterator* cpd = dynamic_cast<const CompoundTransliterator*>(this);
    int32_t n = (cpd == nullptr) ? 1 : cpd->getCount();
    if (index < 0 || index >= n) {
        ec = U_INDEX_OUTOFBOUNDS_ERROR;
        return *this;
    } else {
        return (n == 1) ? *this : cpd->getTransliterator(index);
    }
}

UnicodeSet& Transliterator::getSourceSet(UnicodeSet& result) const {
    handleGetSourceSet(result);
    if (filter != nullptr) {
        UnicodeSet* filterSet = dynamic_cast<UnicodeSet*>(filter);
        UBool deleteFilterSet = false;
        // Most, but not all filters will be UnicodeSets.  Optimize for
        // the high-runner case.
        if (filterSet == nullptr) {
            filterSet = new UnicodeSet();
            // Check null pointer
            if (filterSet == nullptr) {
                return result;
            }
            deleteFilterSet = true;
            filter->addMatchSetTo(*filterSet);
        }
        result.retainAll(*filterSet);
        if (deleteFilterSet) {
            delete filterSet;
        }
    }
    return result;
}

void Transliterator::handleGetSourceSet(UnicodeSet& result) const {
    result.clear();
}

UnicodeSet& Transliterator::getTargetSet(UnicodeSet& result) const {
    return result.clear();
}

// For public consumption
void U_EXPORT2 Transliterator::registerFactory(const UnicodeString& id,
                                     Transliterator::Factory factory,
                                     Transliterator::Token context) {
    Mutex lock(&registryMutex);
    UErrorCode ec = U_ZERO_ERROR;
    if (HAVE_REGISTRY(ec)) {
        _registerFactory(id, factory, context);
    }
}

// To be called only by Transliterator subclasses that are called
// to register themselves by initializeRegistry().
void Transliterator::_registerFactory(const UnicodeString& id,
                                      Transliterator::Factory factory,
                                      Transliterator::Token context) {
    UErrorCode ec = U_ZERO_ERROR;
    registry->put(id, factory, context, true, ec);
}

// To be called only by Transliterator subclasses that are called
// to register themselves by initializeRegistry().
void Transliterator::_registerSpecialInverse(const UnicodeString& target,
                                             const UnicodeString& inverseTarget,
                                             UBool bidirectional) {
    UErrorCode status = U_ZERO_ERROR;
    TransliteratorIDParser::registerSpecialInverse(target, inverseTarget, bidirectional, status);
}

/**
 * Registers a instance <tt>obj</tt> of a subclass of
 * <code>Transliterator</code> with the system.  This object must
 * implement the <tt>clone()</tt> method.  When
 * <tt>getInstance()</tt> is called with an ID string that is
 * equal to <tt>obj.getID()</tt>, then <tt>obj.clone()</tt> is
 * returned.
 *
 * @param obj an instance of subclass of
 * <code>Transliterator</code> that defines <tt>clone()</tt>
 * @see #getInstance
 * @see #unregister
 */
void U_EXPORT2 Transliterator::registerInstance(Transliterator* adoptedPrototype) {
    Mutex lock(&registryMutex);
    UErrorCode ec = U_ZERO_ERROR;
    if (HAVE_REGISTRY(ec)) {
        _registerInstance(adoptedPrototype);
    }
}

void Transliterator::_registerInstance(Transliterator* adoptedPrototype) {
    UErrorCode ec = U_ZERO_ERROR;
    registry->put(adoptedPrototype, true, ec);
}

void U_EXPORT2 Transliterator::registerAlias(const UnicodeString& aliasID,
                                             const UnicodeString& realID) {
    Mutex lock(&registryMutex);
    UErrorCode ec = U_ZERO_ERROR;
    if (HAVE_REGISTRY(ec)) {
        _registerAlias(aliasID, realID);
    }
}

void Transliterator::_registerAlias(const UnicodeString& aliasID,
                                    const UnicodeString& realID) {
    UErrorCode ec = U_ZERO_ERROR;
    registry->put(aliasID, realID, false, true, ec);
}

/**
 * Unregisters a transliterator or class.  This may be either
 * a system transliterator or a user transliterator or class.
 * 
 * @param ID the ID of the transliterator or class
 * @see #registerInstance

 */
void U_EXPORT2 Transliterator::unregister(const UnicodeString& ID) {
    Mutex lock(&registryMutex);
    UErrorCode ec = U_ZERO_ERROR;
    if (HAVE_REGISTRY(ec)) {
        registry->remove(ID);
    }
}

/**
 * == OBSOLETE - remove in ICU 3.4 ==
 * Return the number of IDs currently registered with the system.
 * To retrieve the actual IDs, call getAvailableID(i) with
 * i from 0 to countAvailableIDs() - 1.
 */
int32_t U_EXPORT2 Transliterator::countAvailableIDs() {
    int32_t retVal = 0;
    Mutex lock(&registryMutex);
    UErrorCode ec = U_ZERO_ERROR;
    if (HAVE_REGISTRY(ec)) {
        retVal = registry->countAvailableIDs();
    }
    return retVal;
}

/**
 * == OBSOLETE - remove in ICU 3.4 ==
 * Return the index-th available ID.  index must be between 0
 * and countAvailableIDs() - 1, inclusive.  If index is out of
 * range, the result of getAvailableID(0) is returned.
 */
const UnicodeString& U_EXPORT2 Transliterator::getAvailableID(int32_t index) {
    const UnicodeString* result = nullptr;
    umtx_lock(&registryMutex);
    UErrorCode ec = U_ZERO_ERROR;
    if (HAVE_REGISTRY(ec)) {
        result = &registry->getAvailableID(index);
    }
    umtx_unlock(&registryMutex);
    U_ASSERT(result != nullptr); // fail if no registry
    return *result;
}

StringEnumeration* U_EXPORT2 Transliterator::getAvailableIDs(UErrorCode& ec) {
    if (U_FAILURE(ec)) return nullptr;
    StringEnumeration* result = nullptr;
    umtx_lock(&registryMutex);
    if (HAVE_REGISTRY(ec)) {
        result = registry->getAvailableIDs();
    }
    umtx_unlock(&registryMutex);
    if (result == nullptr) {
        ec = U_INTERNAL_TRANSLITERATOR_ERROR;
    }
    return result;
}

int32_t U_EXPORT2 Transliterator::countAvailableSources() {
    Mutex lock(&registryMutex);
    UErrorCode ec = U_ZERO_ERROR;
    return HAVE_REGISTRY(ec) ? _countAvailableSources() : 0;
}

UnicodeString& U_EXPORT2 Transliterator::getAvailableSource(int32_t index,
                                                  UnicodeString& result) {
    Mutex lock(&registryMutex);
    UErrorCode ec = U_ZERO_ERROR;
    if (HAVE_REGISTRY(ec)) {
        _getAvailableSource(index, result);
    }
    return result;
}

int32_t U_EXPORT2 Transliterator::countAvailableTargets(const UnicodeString& source) {
    Mutex lock(&registryMutex);
    UErrorCode ec = U_ZERO_ERROR;
    return HAVE_REGISTRY(ec) ? _countAvailableTargets(source) : 0;
}

UnicodeString& U_EXPORT2 Transliterator::getAvailableTarget(int32_t index,
                                                  const UnicodeString& source,
                                                  UnicodeString& result) {
    Mutex lock(&registryMutex);
    UErrorCode ec = U_ZERO_ERROR;
    if (HAVE_REGISTRY(ec)) {
        _getAvailableTarget(index, source, result);
    }
    return result;
}

int32_t U_EXPORT2 Transliterator::countAvailableVariants(const UnicodeString& source,
                                               const UnicodeString& target) {
    Mutex lock(&registryMutex);
    UErrorCode ec = U_ZERO_ERROR;
    return HAVE_REGISTRY(ec) ? _countAvailableVariants(source, target) : 0;
}

UnicodeString& U_EXPORT2 Transliterator::getAvailableVariant(int32_t index,
                                                   const UnicodeString& source,
                                                   const UnicodeString& target,
                                                   UnicodeString& result) {
    Mutex lock(&registryMutex);
    UErrorCode ec = U_ZERO_ERROR;
    if (HAVE_REGISTRY(ec)) {
        _getAvailableVariant(index, source, target, result);
    }
    return result;
}

int32_t Transliterator::_countAvailableSources() {
    return registry->countAvailableSources();
}

UnicodeString& Transliterator::_getAvailableSource(int32_t index,
                                                  UnicodeString& result) {
    return registry->getAvailableSource(index, result);
}

int32_t Transliterator::_countAvailableTargets(const UnicodeString& source) {
    return registry->countAvailableTargets(source);
}

UnicodeString& Transliterator::_getAvailableTarget(int32_t index,
                                                  const UnicodeString& source,
                                                  UnicodeString& result) {
    return registry->getAvailableTarget(index, source, result);
}

int32_t Transliterator::_countAvailableVariants(const UnicodeString& source,
                                               const UnicodeString& target) {
    return registry->countAvailableVariants(source, target);
}

UnicodeString& Transliterator::_getAvailableVariant(int32_t index,
                                                   const UnicodeString& source,
                                                   const UnicodeString& target,
                                                   UnicodeString& result) {
    return registry->getAvailableVariant(index, source, target, result);
}

#ifdef U_USE_DEPRECATED_TRANSLITERATOR_API

/**
 * Method for subclasses to use to obtain a character in the given
 * string, with filtering.
 * @deprecated the new architecture provides filtering at the top
 * level.  This method will be removed Dec 31 2001.
 */
char16_t Transliterator::filteredCharAt(const Replaceable& text, int32_t i) const {
    char16_t c;
    const UnicodeFilter* localFilter = getFilter();
    return (localFilter == 0) ? text.charAt(i) :
        (localFilter->contains(c = text.charAt(i)) ? c : (char16_t)0xFFFE);
}

#endif

/**
 * If the registry is initialized, return true.  If not, initialize it
 * and return true.  If the registry cannot be initialized, return
 * false (rare).
 *
 * IMPORTANT: Upon entry, registryMutex must be LOCKED.  The entire
 * initialization is done with the lock held.  There is NO REASON to
 * unlock, since no other thread that is waiting on the registryMutex
 * cannot itself proceed until the registry is initialized.
 */
UBool Transliterator::initializeRegistry(UErrorCode &status) {
    if (registry != nullptr) {
        return true;
    }

    registry = new TransliteratorRegistry(status);
    if (registry == nullptr || U_FAILURE(status)) {
        delete registry;
        registry = nullptr;
        return false; // can't create registry, no recovery
    }

    /* The following code parses the index table located in
     * icu/data/translit/root.txt.  The index is an n x 4 table
     * that follows this format:
     *  <id>{
     *      file{
     *          resource{"<resource>"}
     *          direction{"<direction>"}
     *      }
     *  }
     *  <id>{
     *      internal{
     *          resource{"<resource>"}
     *          direction{"<direction"}
     *       }
     *  }
     *  <id>{
     *      alias{"<getInstanceArg"}
     *  }
     * <id> is the ID of the system transliterator being defined.  These
     * are public IDs enumerated by Transliterator.getAvailableIDs(),
     * unless the second field is "internal".
     * 
     * <resource> is a ResourceReader resource name.  Currently these refer
     * to file names under com/ibm/text/resources.  This string is passed
     * directly to ResourceReader, together with <encoding>.
     * 
     * <direction> is either "FORWARD" or "REVERSE".
     * 
     * <getInstanceArg> is a string to be passed directly to
     * Transliterator.getInstance().  The returned Transliterator object
     * then has its ID changed to <id> and is returned.
     *
     * The extra blank field on "alias" lines is to make the array square.
     */
    //static const char translit_index[] = "translit_index";

    UErrorCode lstatus = U_ZERO_ERROR;
    UResourceBundle *bundle, *transIDs, *colBund;
    bundle = ures_open(U_ICUDATA_TRANSLIT, nullptr/*open default locale*/, &lstatus);
    transIDs = ures_getByKey(bundle, RB_RULE_BASED_IDS, nullptr, &lstatus);
    const UnicodeString T_PART = UNICODE_STRING_SIMPLE("-t-");

    int32_t row, maxRows;
    if (lstatus == U_MEMORY_ALLOCATION_ERROR) {
        delete registry;
        registry = nullptr;
        status = U_MEMORY_ALLOCATION_ERROR;
        return false;
    }
    if (U_SUCCESS(lstatus)) {
        maxRows = ures_getSize(transIDs);
        for (row = 0; row < maxRows; row++) {
            colBund = ures_getByIndex(transIDs, row, nullptr, &lstatus);
            if (U_SUCCESS(lstatus)) {
                UnicodeString id(ures_getKey(colBund), -1, US_INV);
                if(id.indexOf(T_PART) != -1) {
                    ures_close(colBund);
                    continue;
                }
                UResourceBundle* res = ures_getNextResource(colBund, nullptr, &lstatus);
                const char* typeStr = ures_getKey(res);
                char16_t type;
                u_charsToUChars(typeStr, &type, 1);

                if (U_SUCCESS(lstatus)) {
                    int32_t len = 0;
                    const char16_t *resString;
                    switch (type) {
                    case 0x66: // 'f'
                    case 0x69: // 'i'
                        // 'file' or 'internal';
                        // row[2]=resource, row[3]=direction
                        {
                            
                            resString = ures_getStringByKey(res, "resource", &len, &lstatus);
                            UBool visible = (type == 0x0066 /*f*/);
                            UTransDirection dir = 
                                (ures_getUnicodeStringByKey(res, "direction", &lstatus).charAt(0) ==
                                 0x0046 /*F*/) ?
                                UTRANS_FORWARD : UTRANS_REVERSE;
                            registry->put(id, UnicodeString(true, resString, len), dir, true, visible, lstatus);
                        }
                        break;
                    case 0x61: // 'a'
                        // 'alias'; row[2]=createInstance argument
                        resString = ures_getString(res, &len, &lstatus);
                        registry->put(id, UnicodeString(true, resString, len), true, true, lstatus);
                        break;
                    }
                }
                ures_close(res);
            }
            ures_close(colBund);
        }
    }

    ures_close(transIDs);
    ures_close(bundle);

    // Manually add prototypes that the system knows about to the
    // cache.  This is how new non-rule-based transliterators are
    // added to the system.
    
    // This is to allow for null pointer check
    NullTransliterator* tempNullTranslit = new NullTransliterator();
    LowercaseTransliterator* tempLowercaseTranslit = new LowercaseTransliterator();
    UppercaseTransliterator* tempUppercaseTranslit = new UppercaseTransliterator();
    TitlecaseTransliterator* tempTitlecaseTranslit = new TitlecaseTransliterator();
    UnicodeNameTransliterator* tempUnicodeTranslit = new UnicodeNameTransliterator();
    NameUnicodeTransliterator* tempNameUnicodeTranslit = new NameUnicodeTransliterator();
#if !UCONFIG_NO_BREAK_ITERATION
     // TODO: could or should these transliterators be referenced polymorphically once constructed?
     BreakTransliterator* tempBreakTranslit         = new BreakTransliterator();
#endif
    // Check for null pointers
    if (tempNullTranslit == nullptr || tempLowercaseTranslit == nullptr || tempUppercaseTranslit == nullptr ||
        tempTitlecaseTranslit == nullptr || tempUnicodeTranslit == nullptr || 
#if !UCONFIG_NO_BREAK_ITERATION
        tempBreakTranslit == nullptr ||
#endif
        tempNameUnicodeTranslit == nullptr )
    {
        delete tempNullTranslit;
        delete tempLowercaseTranslit;
        delete tempUppercaseTranslit;
        delete tempTitlecaseTranslit;
        delete tempUnicodeTranslit;
        delete tempNameUnicodeTranslit;
#if !UCONFIG_NO_BREAK_ITERATION
        delete tempBreakTranslit;
#endif
        // Since there was an error, remove registry
        delete registry;
        registry = nullptr;

        status = U_MEMORY_ALLOCATION_ERROR;
        return 0;
    }

    registry->put(tempNullTranslit, true, status);
    registry->put(tempLowercaseTranslit, true, status);
    registry->put(tempUppercaseTranslit, true, status);
    registry->put(tempTitlecaseTranslit, true, status);
    registry->put(tempUnicodeTranslit, true, status);
    registry->put(tempNameUnicodeTranslit, true, status);
#if !UCONFIG_NO_BREAK_ITERATION
    registry->put(tempBreakTranslit, false, status);   // false means invisible.
#endif

    RemoveTransliterator::registerIDs(); // Must be within mutex
    EscapeTransliterator::registerIDs();
    UnescapeTransliterator::registerIDs();
    NormalizationTransliterator::registerIDs();
    AnyTransliterator::registerIDs();

    _registerSpecialInverse(UNICODE_STRING_SIMPLE("Null"),
                            UNICODE_STRING_SIMPLE("Null"), false);
    _registerSpecialInverse(UNICODE_STRING_SIMPLE("Upper"),
                            UNICODE_STRING_SIMPLE("Lower"), true);
    _registerSpecialInverse(UNICODE_STRING_SIMPLE("Title"),
                            UNICODE_STRING_SIMPLE("Lower"), false);

    ucln_i18n_registerCleanup(UCLN_I18N_TRANSLITERATOR, utrans_transliterator_cleanup);

    return true;
}

U_NAMESPACE_END

// Defined in transreg.h:

/**
 * Release all static memory held by transliterator.  This will
 * necessarily invalidate any rule-based transliterators held by the
 * user, because RBTs hold pointers to common data objects.
 */
U_CFUNC UBool utrans_transliterator_cleanup() {
    U_NAMESPACE_USE
    TransliteratorIDParser::cleanup();
    if (registry) {
        delete registry;
        registry = nullptr;
    }
    return true;
}

#endif /* #if !UCONFIG_NO_TRANSLITERATION */

//eof

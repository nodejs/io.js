// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (C) 1999-2011, International Business Machines
*   Corporation and others.  All Rights Reserved.
**********************************************************************
*   Date        Name        Description
*   11/17/99    aliu        Creation.
**********************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_TRANSLITERATION

#include "unicode/unifilt.h"
#include "unicode/uniset.h"
#include "cpdtrans.h"
#include "uvector.h"
#include "tridpars.h"
#include "cmemory.h"

// keep in sync with Transliterator
//static const char16_t ID_SEP   = 0x002D; /*-*/
static const char16_t ID_DELIM = 0x003B; /*;*/
static const char16_t NEWLINE  = 10;

static const char16_t COLON_COLON[] = {0x3A, 0x3A, 0}; //"::"

U_NAMESPACE_BEGIN

const char16_t CompoundTransliterator::PASS_STRING[] = { 0x0025, 0x0050, 0x0061, 0x0073, 0x0073, 0 }; // "%Pass"

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(CompoundTransliterator)

/**
 * Constructs a new compound transliterator given an array of
 * transliterators.  The array of transliterators may be of any
 * length, including zero or one, however, useful compound
 * transliterators have at least two components.
 * @param transliterators array of <code>Transliterator</code>
 * objects
 * @param transliteratorCount The number of
 * <code>Transliterator</code> objects in transliterators.
 * @param filter the filter.  Any character for which
 * <tt>filter.contains()</tt> returns <tt>false</tt> will not be
 * altered by this transliterator.  If <tt>filter</tt> is
 * <tt>null</tt> then no filtering is applied.
 */
CompoundTransliterator::CompoundTransliterator(
                           Transliterator* const transliterators[],
                           int32_t transliteratorCount,
                           UnicodeFilter* adoptedFilter) :
    Transliterator(joinIDs(transliterators, transliteratorCount), adoptedFilter),
    trans(nullptr), count(0), numAnonymousRBTs(0) {
    setTransliterators(transliterators, transliteratorCount);
}

/**
 * Splits an ID of the form "ID;ID;..." into a compound using each
 * of the IDs. 
 * @param id of above form
 * @param forward if false, does the list in reverse order, and
 * takes the inverse of each ID.
 */
CompoundTransliterator::CompoundTransliterator(const UnicodeString& id,
                              UTransDirection direction,
                              UnicodeFilter* adoptedFilter,
                              UParseError& /*parseError*/,
                              UErrorCode& status) :
    Transliterator(id, adoptedFilter),
    trans(nullptr), numAnonymousRBTs(0) {
    // TODO add code for parseError...currently unused, but
    // later may be used by parsing code...
    init(id, direction, true, status);
}

CompoundTransliterator::CompoundTransliterator(const UnicodeString& id,
                              UParseError& /*parseError*/,
                              UErrorCode& status) :
    Transliterator(id, nullptr), // set filter to 0 here!
    trans(nullptr), numAnonymousRBTs(0) {
    // TODO add code for parseError...currently unused, but
    // later may be used by parsing code...
    init(id, UTRANS_FORWARD, true, status);
}


/**
 * Private constructor for use of TransliteratorAlias
 */
CompoundTransliterator::CompoundTransliterator(const UnicodeString& newID,
                                              UVector& list,
                                              UnicodeFilter* adoptedFilter,
                                              int32_t anonymousRBTs,
                                              UParseError& /*parseError*/,
                                              UErrorCode& status) :
    Transliterator(newID, adoptedFilter),
    trans(nullptr), numAnonymousRBTs(anonymousRBTs)
{
    init(list, UTRANS_FORWARD, false, status);
}

/**
 * Private constructor for Transliterator from a vector of
 * transliterators.  The caller is responsible for fixing up the
 * ID.
 */
CompoundTransliterator::CompoundTransliterator(UVector& list,
                                               UParseError& /*parseError*/,
                                               UErrorCode& status) :
    Transliterator(UnicodeString(), nullptr),
    trans(nullptr), numAnonymousRBTs(0)
{
    // TODO add code for parseError...currently unused, but
    // later may be used by parsing code...
    init(list, UTRANS_FORWARD, false, status);
    // assume caller will fixup ID
}

CompoundTransliterator::CompoundTransliterator(UVector& list,
                                               int32_t anonymousRBTs,
                                               UParseError& /*parseError*/,
                                               UErrorCode& status) :
    Transliterator(UnicodeString(), nullptr),
    trans(nullptr), numAnonymousRBTs(anonymousRBTs)
{
    init(list, UTRANS_FORWARD, false, status);
}

/**
 * Finish constructing a transliterator: only to be called by
 * constructors.  Before calling init(), set trans and filter to nullptr.
 * @param id the id containing ';'-separated entries
 * @param direction either FORWARD or REVERSE
 * @param idSplitPoint the index into id at which the
 * adoptedSplitTransliterator should be inserted, if there is one, or
 * -1 if there is none.
 * @param adoptedSplitTransliterator a transliterator to be inserted
 * before the entry at offset idSplitPoint in the id string.  May be
 * nullptr to insert no entry.
 * @param fixReverseID if true, then reconstruct the ID of reverse
 * entries by calling getID() of component entries.  Some constructors
 * do not require this because they apply a facade ID anyway.
 * @param status the error code indicating success or failure
 */
void CompoundTransliterator::init(const UnicodeString& id,
                                  UTransDirection direction,
                                  UBool fixReverseID,
                                  UErrorCode& status) {
    // assert(trans == 0);

    if (U_FAILURE(status)) {
        return;
    }

    UVector list(status);
    UnicodeSet* compoundFilter = nullptr;
    UnicodeString regenID;
    if (!TransliteratorIDParser::parseCompoundID(id, direction,
                                      regenID, list, compoundFilter)) {
        status = U_INVALID_ID;
        delete compoundFilter;
        return;
    }

    TransliteratorIDParser::instantiateList(list, status);

    init(list, direction, fixReverseID, status);

    if (compoundFilter != nullptr) {
        adoptFilter(compoundFilter);
    }
}

/**
 * Finish constructing a transliterator: only to be called by
 * constructors.  Before calling init(), set trans and filter to nullptr.
 * @param list a vector of transliterator objects to be adopted.  It
 * should NOT be empty.  The list should be in declared order.  That
 * is, it should be in the FORWARD order; if direction is REVERSE then
 * the list order will be reversed.
 * @param direction either FORWARD or REVERSE
 * @param fixReverseID if true, then reconstruct the ID of reverse
 * entries by calling getID() of component entries.  Some constructors
 * do not require this because they apply a facade ID anyway.
 * @param status the error code indicating success or failure
 */
void CompoundTransliterator::init(UVector& list,
                                  UTransDirection direction,
                                  UBool fixReverseID,
                                  UErrorCode& status) {
    // assert(trans == 0);

    // Allocate array
    if (U_SUCCESS(status)) {
        count = list.size();
        trans = static_cast<Transliterator**>(uprv_malloc(count * sizeof(Transliterator*)));
        /* test for nullptr */
        if (trans == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
    }

    if (U_FAILURE(status) || trans == nullptr) {
         // assert(trans == 0);
        return;
    }

    // Move the transliterators from the vector into an array.
    // Reverse the order if necessary.
    int32_t i;
    for (i=0; i<count; ++i) {
        int32_t j = (direction == UTRANS_FORWARD) ? i : count - 1 - i;
        trans[i] = static_cast<Transliterator*>(list.elementAt(j));
    }

    // If the direction is UTRANS_REVERSE then we may need to fix the
    // ID.
    if (direction == UTRANS_REVERSE && fixReverseID) {
        UnicodeString newID;
        for (i=0; i<count; ++i) {
            if (i > 0) {
                newID.append(ID_DELIM);
            }
            newID.append(trans[i]->getID());
        }
        setID(newID);
    }

    computeMaximumContextLength();
}

/**
 * Return the IDs of the given list of transliterators, concatenated
 * with ID_DELIM delimiting them.  Equivalent to the perlish expression
 * join(ID_DELIM, map($_.getID(), transliterators).
 */
UnicodeString CompoundTransliterator::joinIDs(Transliterator* const transliterators[],
                                              int32_t transCount) {
    UnicodeString id;
    for (int32_t i=0; i<transCount; ++i) {
        if (i > 0) {
            id.append(ID_DELIM);
        }
        id.append(transliterators[i]->getID());
    }
    return id; // Return temporary
}

/**
 * Copy constructor.
 */
CompoundTransliterator::CompoundTransliterator(const CompoundTransliterator& t) :
    Transliterator(t), trans(nullptr), count(0), numAnonymousRBTs(-1) {
    *this = t;
}

/**
 * Destructor
 */
CompoundTransliterator::~CompoundTransliterator() {
    freeTransliterators();
}

void CompoundTransliterator::freeTransliterators() {
    if (trans != nullptr) {
        for (int32_t i=0; i<count; ++i) {
            delete trans[i];
        }
        uprv_free(trans);
    }
    trans = nullptr;
    count = 0;
}

/**
 * Assignment operator.
 */
CompoundTransliterator& CompoundTransliterator::operator=(
                                             const CompoundTransliterator& t)
{
    if (this == &t) { return *this; }  // self-assignment: no-op
    Transliterator::operator=(t);
    int32_t i = 0;
    UBool failed = false;
    if (trans != nullptr) {
        for (i=0; i<count; ++i) {
            delete trans[i];
            trans[i] = nullptr;
        }
    }
    if (t.count > count) {
        if (trans != nullptr) {
            uprv_free(trans);
        }
        trans = static_cast<Transliterator**>(uprv_malloc(t.count * sizeof(Transliterator*)));
    }
    count = t.count;
    if (trans != nullptr) {
        for (i=0; i<count; ++i) {
            trans[i] = t.trans[i]->clone();
            if (trans[i] == nullptr) {
                failed = true;
                break;
            }
        }
    }

    // if memory allocation failed delete backwards trans array
    if (failed && i > 0) {
        int32_t n;
        for (n = i-1; n >= 0; n--) {
            uprv_free(trans[n]);
            trans[n] = nullptr;
        }
    }
    numAnonymousRBTs = t.numAnonymousRBTs;
    return *this;
}

/**
 * Transliterator API.
 */
CompoundTransliterator* CompoundTransliterator::clone() const {
    return new CompoundTransliterator(*this);
}

/**
 * Returns the number of transliterators in this chain.
 * @return number of transliterators in this chain.
 */
int32_t CompoundTransliterator::getCount() const {
    return count;
}

/**
 * Returns the transliterator at the given index in this chain.
 * @param index index into chain, from 0 to <code>getCount() - 1</code>
 * @return transliterator at the given index
 */
const Transliterator& CompoundTransliterator::getTransliterator(int32_t index) const {
    return *trans[index];
}

void CompoundTransliterator::setTransliterators(Transliterator* const transliterators[],
                                                int32_t transCount) {
    Transliterator** a = static_cast<Transliterator**>(uprv_malloc(transCount * sizeof(Transliterator*)));
    if (a == nullptr) {
        return;
    }
    int32_t i = 0;
    UBool failed = false;
    for (i=0; i<transCount; ++i) {
        a[i] = transliterators[i]->clone();
        if (a[i] == nullptr) {
            failed = true;
            break;
        }
    }
    if (failed && i > 0) {
        int32_t n;
        for (n = i-1; n >= 0; n--) {
            uprv_free(a[n]);
            a[n] = nullptr;
        }
        return;
    }
    adoptTransliterators(a, transCount);
}

void CompoundTransliterator::adoptTransliterators(Transliterator* adoptedTransliterators[],
                                                  int32_t transCount) {
    // First free trans[] and set count to zero.  Once this is done,
    // orphan the filter.  Set up the new trans[].
    freeTransliterators();
    trans = adoptedTransliterators;
    count = transCount;
    computeMaximumContextLength();
    setID(joinIDs(trans, count));
}

/**
 * Append c to buf, unless buf is empty or buf already ends in c.
 */
static void _smartAppend(UnicodeString& buf, char16_t c) {
    if (buf.length() != 0 &&
        buf.charAt(buf.length() - 1) != c) {
        buf.append(c);
    }
}

UnicodeString& CompoundTransliterator::toRules(UnicodeString& rulesSource,
                                               UBool escapeUnprintable) const {
    // We do NOT call toRules() on our component transliterators, in
    // general.  If we have several rule-based transliterators, this
    // yields a concatenation of the rules -- not what we want.  We do
    // handle compound RBT transliterators specially -- those for which
    // compoundRBTIndex >= 0.  For the transliterator at compoundRBTIndex,
    // we do call toRules() recursively.
    rulesSource.truncate(0);
    if (numAnonymousRBTs >= 1 && getFilter() != nullptr) {
        // If we are a compound RBT and if we have a global
        // filter, then emit it at the top.
        UnicodeString pat;
        rulesSource.append(COLON_COLON, 2).append(getFilter()->toPattern(pat, escapeUnprintable)).append(ID_DELIM);
    }
    for (int32_t i=0; i<count; ++i) {
        UnicodeString rule;

        // Anonymous RuleBasedTransliterators (inline rules and
        // ::BEGIN/::END blocks) are given IDs that begin with
        // "%Pass": use toRules() to write all the rules to the output
        // (and insert "::Null;" if we have two in a row)
        if (trans[i]->getID().startsWith(PASS_STRING, 5)) {
            trans[i]->toRules(rule, escapeUnprintable);
            if (numAnonymousRBTs > 1 && i > 0 && trans[i - 1]->getID().startsWith(PASS_STRING, 5))
                rule = UNICODE_STRING_SIMPLE("::Null;") + rule;

        // we also use toRules() on CompoundTransliterators (which we
        // check for by looking for a semicolon in the ID)-- this gets
        // the list of their child transliterators output in the right
        // format
        } else if (trans[i]->getID().indexOf(ID_DELIM) >= 0) {
            trans[i]->toRules(rule, escapeUnprintable);

        // for everything else, use Transliterator::toRules()
        } else {
            trans[i]->Transliterator::toRules(rule, escapeUnprintable);
        }
        _smartAppend(rulesSource, NEWLINE);
        rulesSource.append(rule);
        _smartAppend(rulesSource, ID_DELIM);
    }
    return rulesSource;
}

/**
 * Implement Transliterator framework
 */
void CompoundTransliterator::handleGetSourceSet(UnicodeSet& result) const {
    UnicodeSet set;
    result.clear();
    for (int32_t i=0; i<count; ++i) {
    result.addAll(trans[i]->getSourceSet(set));
    // Take the example of Hiragana-Latin.  This is really
    // Hiragana-Katakana; Katakana-Latin.  The source set of
    // these two is roughly [:Hiragana:] and [:Katakana:].
    // But the source set for the entire transliterator is
    // actually [:Hiragana:] ONLY -- that is, the first
    // non-empty source set.

    // This is a heuristic, and not 100% reliable.
    if (!result.isEmpty()) {
        break;
    }
    }
}

/**
 * Override Transliterator framework
 */
UnicodeSet& CompoundTransliterator::getTargetSet(UnicodeSet& result) const {
    UnicodeSet set;
    result.clear();
    for (int32_t i=0; i<count; ++i) {
    // This is a heuristic, and not 100% reliable.
    result.addAll(trans[i]->getTargetSet(set));
    }
    return result;
}

/**
 * Implements {@link Transliterator#handleTransliterate}.
 */
void CompoundTransliterator::handleTransliterate(Replaceable& text, UTransPosition& index,
                                                 UBool incremental) const {
    /* Call each transliterator with the same contextStart and
     * start, but with the limit as modified
     * by preceding transliterators.  The start index must be
     * reset for each transliterator to give each a chance to
     * transliterate the text.  The initial contextStart index is known
     * to still point to the same place after each transliterator
     * is called because each transliterator will not change the
     * text between contextStart and the initial start index.
     *
     * IMPORTANT: After the first transliterator, each subsequent
     * transliterator only gets to transliterate text committed by
     * preceding transliterators; that is, the start (output
     * value) of transliterator i becomes the limit (input value)
     * of transliterator i+1.  Finally, the overall limit is fixed
     * up before we return.
     *
     * Assumptions we make here:
     * (1) contextStart <= start <= limit <= contextLimit <= text.length()
     * (2) start <= start' <= limit'  ;cursor doesn't move back
     * (3) start <= limit'            ;text before cursor unchanged
     * - start' is the value of start after calling handleKT
     * - limit' is the value of limit after calling handleKT
     */
    
    /**
     * Example: 3 transliterators.  This example illustrates the
     * mechanics we need to implement.  C, S, and L are the contextStart,
     * start, and limit.  gl is the globalLimit.  contextLimit is
     * equal to limit throughout.
     *
     * 1. h-u, changes hex to Unicode
     *
     *    4  7  a  d  0      4  7  a
     *    abc/u0061/u    =>  abca/u    
     *    C  S       L       C   S L   gl=f->a
     *
     * 2. upup, changes "x" to "XX"
     *
     *    4  7  a       4  7  a
     *    abca/u    =>  abcAA/u    
     *    C  SL         C    S   
     *                       L    gl=a->b
     * 3. u-h, changes Unicode to hex
     *
     *    4  7  a        4  7  a  d  0  3
     *    abcAA/u    =>  abc/u0041/u0041/u    
     *    C  S L         C              S
     *                                  L   gl=b->15
     * 4. return
     *
     *    4  7  a  d  0  3
     *    abc/u0041/u0041/u    
     *    C S L
     */

    if (count < 1) {
        index.start = index.limit;
        return; // Short circuit for empty compound transliterators
    }

    // compoundLimit is the limit value for the entire compound
    // operation.  We overwrite index.limit with the previous
    // index.start.  After each transliteration, we update
    // compoundLimit for insertions or deletions that have happened.
    int32_t compoundLimit = index.limit;

    // compoundStart is the start for the entire compound
    // operation.
    int32_t compoundStart = index.start;
    
    int32_t delta = 0; // delta in length

    // Give each transliterator a crack at the run of characters.
    // See comments at the top of the method for more detail.
    for (int32_t i=0; i<count; ++i) {
        index.start = compoundStart; // Reset start
        int32_t limit = index.limit;
        
        if (index.start == index.limit) {
            // Short circuit for empty range
            break;
        }

        trans[i]->filteredTransliterate(text, index, incremental);
        
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

        // Cumulative delta for insertions/deletions
        delta += index.limit - limit;
        
        if (incremental) {
            // In the incremental case, only allow subsequent
            // transliterators to modify what has already been
            // completely processed by prior transliterators.  In the
            // non-incrmental case, allow each transliterator to
            // process the entire text.
            index.limit = index.start;
        }
    }

    compoundLimit += delta;

    // Start is good where it is -- where the last transliterator left
    // it.  Limit needs to be put back where it was, modulo
    // adjustments for deletions/insertions.
    index.limit = compoundLimit;
}

/**
 * Sets the length of the longest context required by this transliterator.
 * This is <em>preceding</em> context.
 */
void CompoundTransliterator::computeMaximumContextLength() {
    int32_t max = 0;
    for (int32_t i=0; i<count; ++i) {
        int32_t len = trans[i]->getMaximumContextLength();
        if (len > max) {
            max = len;
        }
    }
    setMaximumContextLength(max);
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_TRANSLITERATION */

/* eof */

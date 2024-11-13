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

#include "unicode/utypes.h"

#if !UCONFIG_NO_TRANSLITERATION

#include "unicode/uobject.h"
#include "unicode/parseerr.h"
#include "unicode/parsepos.h"
#include "unicode/putil.h"
#include "unicode/uchar.h"
#include "unicode/ustring.h"
#include "unicode/uniset.h"
#include "unicode/utf16.h"
#include "cstring.h"
#include "funcrepl.h"
#include "hash.h"
#include "quant.h"
#include "rbt.h"
#include "rbt_data.h"
#include "rbt_pars.h"
#include "rbt_rule.h"
#include "strmatch.h"
#include "strrepl.h"
#include "unicode/symtable.h"
#include "tridpars.h"
#include "uvector.h"
#include "hash.h"
#include "patternprops.h"
#include "util.h"
#include "cmemory.h"
#include "uprops.h"
#include "putilimp.h"

// Operators
#define VARIABLE_DEF_OP ((char16_t)0x003D) /*=*/
#define FORWARD_RULE_OP ((char16_t)0x003E) /*>*/
#define REVERSE_RULE_OP ((char16_t)0x003C) /*<*/
#define FWDREV_RULE_OP  ((char16_t)0x007E) /*~*/ // internal rep of <> op

// Other special characters
#define QUOTE             ((char16_t)0x0027) /*'*/
#define ESCAPE            ((char16_t)0x005C) /*\*/
#define END_OF_RULE       ((char16_t)0x003B) /*;*/
#define RULE_COMMENT_CHAR ((char16_t)0x0023) /*#*/

#define SEGMENT_OPEN       ((char16_t)0x0028) /*(*/
#define SEGMENT_CLOSE      ((char16_t)0x0029) /*)*/
#define CONTEXT_ANTE       ((char16_t)0x007B) /*{*/
#define CONTEXT_POST       ((char16_t)0x007D) /*}*/
#define CURSOR_POS         ((char16_t)0x007C) /*|*/
#define CURSOR_OFFSET      ((char16_t)0x0040) /*@*/
#define ANCHOR_START       ((char16_t)0x005E) /*^*/
#define KLEENE_STAR        ((char16_t)0x002A) /***/
#define ONE_OR_MORE        ((char16_t)0x002B) /*+*/
#define ZERO_OR_ONE        ((char16_t)0x003F) /*?*/

#define DOT                ((char16_t)46)     /*.*/

static const char16_t DOT_SET[] = { // "[^[:Zp:][:Zl:]\r\n$]";
    91, 94, 91, 58, 90, 112, 58, 93, 91, 58, 90,
    108, 58, 93, 92, 114, 92, 110, 36, 93, 0
};

// A function is denoted &Source-Target/Variant(text)
#define FUNCTION           ((char16_t)38)     /*&*/

// Aliases for some of the syntax characters. These are provided so
// transliteration rules can be expressed in XML without clashing with
// XML syntax characters '<', '>', and '&'.
#define ALT_REVERSE_RULE_OP ((char16_t)0x2190) // Left Arrow
#define ALT_FORWARD_RULE_OP ((char16_t)0x2192) // Right Arrow
#define ALT_FWDREV_RULE_OP  ((char16_t)0x2194) // Left Right Arrow
#define ALT_FUNCTION        ((char16_t)0x2206) // Increment (~Greek Capital Delta)

// Special characters disallowed at the top level
static const char16_t ILLEGAL_TOP[] = {41,0}; // ")"

// Special characters disallowed within a segment
static const char16_t ILLEGAL_SEG[] = {123,125,124,64,0}; // "{}|@"

// Special characters disallowed within a function argument
static const char16_t ILLEGAL_FUNC[] = {94,40,46,42,43,63,123,125,124,64,0}; // "^(.*+?{}|@"

// By definition, the ANCHOR_END special character is a
// trailing SymbolTable.SYMBOL_REF character.
// private static final char ANCHOR_END       = '$';

static const char16_t gOPERATORS[] = { // "=><"
    VARIABLE_DEF_OP, FORWARD_RULE_OP, REVERSE_RULE_OP,
    ALT_FORWARD_RULE_OP, ALT_REVERSE_RULE_OP, ALT_FWDREV_RULE_OP,
    0
};

static const char16_t HALF_ENDERS[] = { // "=><;"
    VARIABLE_DEF_OP, FORWARD_RULE_OP, REVERSE_RULE_OP,
    ALT_FORWARD_RULE_OP, ALT_REVERSE_RULE_OP, ALT_FWDREV_RULE_OP,
    END_OF_RULE,
    0
};

// These are also used in Transliterator::toRules()
static const int32_t ID_TOKEN_LEN = 2;
static const char16_t   ID_TOKEN[]   = { 0x3A, 0x3A }; // ':', ':'

/*
commented out until we do real ::BEGIN/::END functionality
static const int32_t BEGIN_TOKEN_LEN = 5;
static const char16_t BEGIN_TOKEN[] = { 0x42, 0x45, 0x47, 0x49, 0x4e }; // 'BEGIN'

static const int32_t END_TOKEN_LEN = 3;
static const char16_t END_TOKEN[] = { 0x45, 0x4e, 0x44 }; // 'END'
*/

U_NAMESPACE_BEGIN

//----------------------------------------------------------------------
// BEGIN ParseData
//----------------------------------------------------------------------

/**
 * This class implements the SymbolTable interface.  It is used
 * during parsing to give UnicodeSet access to variables that
 * have been defined so far.  Note that it uses variablesVector,
 * _not_ data.setVariables.
 */
class ParseData : public UMemory, public SymbolTable {
public:
    const TransliterationRuleData* data; // alias

    const UVector* variablesVector; // alias

    const Hashtable* variableNames; // alias

    ParseData(const TransliterationRuleData* data = nullptr,
              const UVector* variablesVector = nullptr,
              const Hashtable* variableNames = nullptr);

    virtual ~ParseData();

    virtual const UnicodeString* lookup(const UnicodeString& s) const override;

    virtual const UnicodeFunctor* lookupMatcher(UChar32 ch) const override;

    virtual UnicodeString parseReference(const UnicodeString& text,
                                         ParsePosition& pos, int32_t limit) const override;
    /**
     * Return true if the given character is a matcher standin or a plain
     * character (non standin).
     */
    UBool isMatcher(UChar32 ch);

    /**
     * Return true if the given character is a replacer standin or a plain
     * character (non standin).
     */
    UBool isReplacer(UChar32 ch);

private:
    ParseData(const ParseData &other); // forbid copying of this class
    ParseData &operator=(const ParseData &other); // forbid copying of this class
};

ParseData::ParseData(const TransliterationRuleData* d,
                     const UVector* sets,
                     const Hashtable* vNames) :
    data(d), variablesVector(sets), variableNames(vNames) {}

ParseData::~ParseData() {}

/**
 * Implement SymbolTable API.
 */
const UnicodeString* ParseData::lookup(const UnicodeString& name) const {
    return static_cast<const UnicodeString*>(variableNames->get(name));
}

/**
 * Implement SymbolTable API.
 */
const UnicodeFunctor* ParseData::lookupMatcher(UChar32 ch) const {
    // Note that we cannot use data.lookupSet() because the
    // set array has not been constructed yet.
    const UnicodeFunctor* set = nullptr;
    int32_t i = ch - data->variablesBase;
    if (i >= 0 && i < variablesVector->size()) {
        int32_t j = ch - data->variablesBase;
        set = (j < variablesVector->size()) ?
            static_cast<UnicodeFunctor*>(variablesVector->elementAt(j)) : nullptr;
    }
    return set;
}

/**
 * Implement SymbolTable API.  Parse out a symbol reference
 * name.
 */
UnicodeString ParseData::parseReference(const UnicodeString& text,
                                        ParsePosition& pos, int32_t limit) const {
    int32_t start = pos.getIndex();
    int32_t i = start;
    UnicodeString result;
    while (i < limit) {
        char16_t c = text.charAt(i);
        if ((i==start && !u_isIDStart(c)) || !u_isIDPart(c)) {
            break;
        }
        ++i;
    }
    if (i == start) { // No valid name chars
        return result; // Indicate failure with empty string
    }
    pos.setIndex(i);
    text.extractBetween(start, i, result);
    return result;
}

UBool ParseData::isMatcher(UChar32 ch) {
    // Note that we cannot use data.lookup() because the
    // set array has not been constructed yet.
    int32_t i = ch - data->variablesBase;
    if (i >= 0 && i < variablesVector->size()) {
        UnicodeFunctor* f = static_cast<UnicodeFunctor*>(variablesVector->elementAt(i));
        return f != nullptr && f->toMatcher() != nullptr;
    }
    return true;
}

/**
 * Return true if the given character is a replacer standin or a plain
 * character (non standin).
 */
UBool ParseData::isReplacer(UChar32 ch) {
    // Note that we cannot use data.lookup() because the
    // set array has not been constructed yet.
    int i = ch - data->variablesBase;
    if (i >= 0 && i < variablesVector->size()) {
        UnicodeFunctor* f = static_cast<UnicodeFunctor*>(variablesVector->elementAt(i));
        return f != nullptr && f->toReplacer() != nullptr;
    }
    return true;
}

//----------------------------------------------------------------------
// BEGIN RuleHalf
//----------------------------------------------------------------------

/**
 * A class representing one side of a rule.  This class knows how to
 * parse half of a rule.  It is tightly coupled to the method
 * RuleBasedTransliterator.Parser.parseRule().
 */
class RuleHalf : public UMemory {

public:

    UnicodeString text;

    int32_t cursor; // position of cursor in text
    int32_t ante;   // position of ante context marker '{' in text
    int32_t post;   // position of post context marker '}' in text

    // Record the offset to the cursor either to the left or to the
    // right of the key.  This is indicated by characters on the output
    // side that allow the cursor to be positioned arbitrarily within
    // the matching text.  For example, abc{def} > | @@@ xyz; changes
    // def to xyz and moves the cursor to before abc.  Offset characters
    // must be at the start or end, and they cannot move the cursor past
    // the ante- or postcontext text.  Placeholders are only valid in
    // output text.  The length of the ante and post context is
    // determined at runtime, because of supplementals and quantifiers.
    int32_t cursorOffset; // only nonzero on output side

    // Position of first CURSOR_OFFSET on _right_.  This will be -1
    // for |@, -2 for |@@, etc., and 1 for @|, 2 for @@|, etc.
    int32_t cursorOffsetPos;

    UBool anchorStart;
    UBool anchorEnd;

    /**
     * The segment number from 1..n of the next '(' we see
     * during parsing; 1-based.
     */
    int32_t nextSegmentNumber;

    TransliteratorParser& parser;

    //--------------------------------------------------
    // Methods

    RuleHalf(TransliteratorParser& parser);
    ~RuleHalf();

    int32_t parse(const UnicodeString& rule, int32_t pos, int32_t limit, UErrorCode& status);

    int32_t parseSection(const UnicodeString& rule, int32_t pos, int32_t limit,
                         UnicodeString& buf,
                         const UnicodeString& illegal,
                         UBool isSegment,
                         UErrorCode& status);

    /**
     * Remove context.
     */
    void removeContext();

    /**
     * Return true if this half looks like valid output, that is, does not
     * contain quantifiers or other special input-only elements.
     */
    UBool isValidOutput(TransliteratorParser& parser);

    /**
     * Return true if this half looks like valid input, that is, does not
     * contain functions or other special output-only elements.
     */
    UBool isValidInput(TransliteratorParser& parser);

    int syntaxError(UErrorCode code,
                    const UnicodeString& rule,
                    int32_t start,
                    UErrorCode& status) {
        return parser.syntaxError(code, rule, start, status);
    }

private:
    // Disallowed methods; no impl.
    RuleHalf(const RuleHalf&);
    RuleHalf& operator=(const RuleHalf&);
};

RuleHalf::RuleHalf(TransliteratorParser& p) :
    parser(p)
{
    cursor = -1;
    ante = -1;
    post = -1;
    cursorOffset = 0;
    cursorOffsetPos = 0;
    anchorStart = anchorEnd = false;
    nextSegmentNumber = 1;
}

RuleHalf::~RuleHalf() {
}

/**
 * Parse one side of a rule, stopping at either the limit,
 * the END_OF_RULE character, or an operator.
 * @return the index after the terminating character, or
 * if limit was reached, limit
 */
int32_t RuleHalf::parse(const UnicodeString& rule, int32_t pos, int32_t limit, UErrorCode& status) {
    int32_t start = pos;
    text.truncate(0);
    pos = parseSection(rule, pos, limit, text, UnicodeString(true, ILLEGAL_TOP, -1), false, status);

    if (cursorOffset > 0 && cursor != cursorOffsetPos) {
        return syntaxError(U_MISPLACED_CURSOR_OFFSET, rule, start, status);
    }
    
    return pos;
}
 
/**
 * Parse a section of one side of a rule, stopping at either
 * the limit, the END_OF_RULE character, an operator, or a
 * segment close character.  This method parses both a
 * top-level rule half and a segment within such a rule half.
 * It calls itself recursively to parse segments and nested
 * segments.
 * @param buf buffer into which to accumulate the rule pattern
 * characters, either literal characters from the rule or
 * standins for UnicodeMatcher objects including segments.
 * @param illegal the set of special characters that is illegal during
 * this parse.
 * @param isSegment if true, then we've already seen a '(' and
 * pos on entry points right after it.  Accumulate everything
 * up to the closing ')', put it in a segment matcher object,
 * generate a standin for it, and add the standin to buf.  As
 * a side effect, update the segments vector with a reference
 * to the segment matcher.  This works recursively for nested
 * segments.  If isSegment is false, just accumulate
 * characters into buf.
 * @return the index after the terminating character, or
 * if limit was reached, limit
 */
int32_t RuleHalf::parseSection(const UnicodeString& rule, int32_t pos, int32_t limit,
                               UnicodeString& buf,
                               const UnicodeString& illegal,
                               UBool isSegment, UErrorCode& status) {
    int32_t start = pos;
    ParsePosition pp;
    UnicodeString scratch;
    UBool done = false;
    int32_t quoteStart = -1; // Most recent 'single quoted string'
    int32_t quoteLimit = -1;
    int32_t varStart = -1; // Most recent $variableReference
    int32_t varLimit = -1;
    int32_t bufStart = buf.length();
    
    while (pos < limit && !done) {
        // Since all syntax characters are in the BMP, fetching
        // 16-bit code units suffices here.
        char16_t c = rule.charAt(pos++);
        if (PatternProps::isWhiteSpace(c)) {
            // Ignore whitespace.  Note that this is not Unicode
            // spaces, but Java spaces -- a subset, representing
            // whitespace likely to be seen in code.
            continue;
        }
        if (u_strchr(HALF_ENDERS, c) != nullptr) {
            if (isSegment) {
                // Unclosed segment
                return syntaxError(U_UNCLOSED_SEGMENT, rule, start, status);
            }
            break;
        }
        if (anchorEnd) {
            // Text after a presumed end anchor is a syntax err
            return syntaxError(U_MALFORMED_VARIABLE_REFERENCE, rule, start, status);
        }
        if (UnicodeSet::resemblesPattern(rule, pos-1)) {
            pp.setIndex(pos-1); // Backup to opening '['
            buf.append(parser.parseSet(rule, pp, status));
            if (U_FAILURE(status)) {
                return syntaxError(U_MALFORMED_SET, rule, start, status);
            }
            pos = pp.getIndex();                    
            continue;
        }
        // Handle escapes
        if (c == ESCAPE) {
            if (pos == limit) {
                return syntaxError(U_TRAILING_BACKSLASH, rule, start, status);
            }
            UChar32 escaped = rule.unescapeAt(pos); // pos is already past '\\'
            if (escaped == static_cast<UChar32>(-1)) {
                return syntaxError(U_MALFORMED_UNICODE_ESCAPE, rule, start, status);
            }
            if (!parser.checkVariableRange(escaped)) {
                return syntaxError(U_VARIABLE_RANGE_OVERLAP, rule, start, status);
            }
            buf.append(escaped);
            continue;
        }
        // Handle quoted matter
        if (c == QUOTE) {
            int32_t iq = rule.indexOf(QUOTE, pos);
            if (iq == pos) {
                buf.append(c); // Parse [''] outside quotes as [']
                ++pos;
            } else {
                /* This loop picks up a run of quoted text of the
                 * form 'aaaa' each time through.  If this run
                 * hasn't really ended ('aaaa''bbbb') then it keeps
                 * looping, each time adding on a new run.  When it
                 * reaches the final quote it breaks.
                 */
                quoteStart = buf.length();
                for (;;) {
                    if (iq < 0) {
                        return syntaxError(U_UNTERMINATED_QUOTE, rule, start, status);
                    }
                    scratch.truncate(0);
                    rule.extractBetween(pos, iq, scratch);
                    buf.append(scratch);
                    pos = iq+1;
                    if (pos < limit && rule.charAt(pos) == QUOTE) {
                        // Parse [''] inside quotes as [']
                        iq = rule.indexOf(QUOTE, pos+1);
                        // Continue looping
                    } else {
                        break;
                    }
                }
                quoteLimit = buf.length();

                for (iq=quoteStart; iq<quoteLimit; ++iq) {
                    if (!parser.checkVariableRange(buf.charAt(iq))) {
                        return syntaxError(U_VARIABLE_RANGE_OVERLAP, rule, start, status);
                    }
                }
            }
            continue;
        }

        if (!parser.checkVariableRange(c)) {
            return syntaxError(U_VARIABLE_RANGE_OVERLAP, rule, start, status);
        }

        if (illegal.indexOf(c) >= 0) {
            syntaxError(U_ILLEGAL_CHARACTER, rule, start, status);
        }

        switch (c) {
                    
        //------------------------------------------------------
        // Elements allowed within and out of segments
        //------------------------------------------------------
        case ANCHOR_START:
            if (buf.length() == 0 && !anchorStart) {
                anchorStart = true;
            } else {
              return syntaxError(U_MISPLACED_ANCHOR_START,
                                 rule, start, status);
            }
          break;
        case SEGMENT_OPEN:
            {
                // bufSegStart is the offset in buf to the first
                // character of the segment we are parsing.
                int32_t bufSegStart = buf.length();
                
                // Record segment number now, since nextSegmentNumber
                // will be incremented during the call to parseSection
                // if there are nested segments.
                int32_t segmentNumber = nextSegmentNumber++; // 1-based
                
                // Parse the segment
                pos = parseSection(rule, pos, limit, buf, UnicodeString(true, ILLEGAL_SEG, -1), true, status);
                
                // After parsing a segment, the relevant characters are
                // in buf, starting at offset bufSegStart.  Extract them
                // into a string matcher, and replace them with a
                // standin for that matcher.
                StringMatcher* m =
                    new StringMatcher(buf, bufSegStart, buf.length(),
                                      segmentNumber, *parser.curData);
                if (m == nullptr) {
                    return syntaxError(U_MEMORY_ALLOCATION_ERROR, rule, start, status);
                }
                
                // Record and associate object and segment number
                parser.setSegmentObject(segmentNumber, m, status);
                buf.truncate(bufSegStart);
                buf.append(parser.getSegmentStandin(segmentNumber, status));
            }
            break;
        case FUNCTION:
        case ALT_FUNCTION:
            {
                int32_t iref = pos;
                TransliteratorIDParser::SingleID* single =
                    TransliteratorIDParser::parseFilterID(rule, iref);
                // The next character MUST be a segment open
                if (single == nullptr ||
                    !ICU_Utility::parseChar(rule, iref, SEGMENT_OPEN)) {
                    return syntaxError(U_INVALID_FUNCTION, rule, start, status);
                }
                
                Transliterator *t = single->createInstance();
                delete single;
                if (t == nullptr) {
                    return syntaxError(U_INVALID_FUNCTION, rule, start, status);
                }
                
                // bufSegStart is the offset in buf to the first
                // character of the segment we are parsing.
                int32_t bufSegStart = buf.length();
                
                // Parse the segment
                pos = parseSection(rule, iref, limit, buf, UnicodeString(true, ILLEGAL_FUNC, -1), true, status);
                
                // After parsing a segment, the relevant characters are
                // in buf, starting at offset bufSegStart.
                UnicodeString output;
                buf.extractBetween(bufSegStart, buf.length(), output);
                FunctionReplacer *r =
                    new FunctionReplacer(t, new StringReplacer(output, parser.curData));
                if (r == nullptr) {
                    return syntaxError(U_MEMORY_ALLOCATION_ERROR, rule, start, status);
                }
                
                // Replace the buffer contents with a stand-in
                buf.truncate(bufSegStart);
                buf.append(parser.generateStandInFor(r, status));
            }
            break;
        case SymbolTable::SYMBOL_REF:
            // Handle variable references and segment references "$1" .. "$9"
            {
                // A variable reference must be followed immediately
                // by a Unicode identifier start and zero or more
                // Unicode identifier part characters, or by a digit
                // 1..9 if it is a segment reference.
                if (pos == limit) {
                    // A variable ref character at the end acts as
                    // an anchor to the context limit, as in perl.
                    anchorEnd = true;
                    break;
                }
                // Parse "$1" "$2" .. "$9" .. (no upper limit)
                c = rule.charAt(pos);
                int32_t r = u_digit(c, 10);
                if (r >= 1 && r <= 9) {
                    r = ICU_Utility::parseNumber(rule, pos, 10);
                    if (r < 0) {
                        return syntaxError(U_UNDEFINED_SEGMENT_REFERENCE,
                                           rule, start, status);
                    }
                    buf.append(parser.getSegmentStandin(r, status));
                } else {
                    pp.setIndex(pos);
                    UnicodeString name = parser.parseData->
                                    parseReference(rule, pp, limit);
                    if (name.length() == 0) {
                        // This means the '$' was not followed by a
                        // valid name.  Try to interpret it as an
                        // end anchor then.  If this also doesn't work
                        // (if we see a following character) then signal
                        // an error.
                        anchorEnd = true;
                        break;
                    }
                    pos = pp.getIndex();
                    // If this is a variable definition statement,
                    // then the LHS variable will be undefined.  In
                    // that case appendVariableDef() will append the
                    // special placeholder char variableLimit-1.
                    varStart = buf.length();
                    parser.appendVariableDef(name, buf, status);
                    varLimit = buf.length();
                }
            }
            break;
        case DOT:
            buf.append(parser.getDotStandIn(status));
            break;
        case KLEENE_STAR:
        case ONE_OR_MORE:
        case ZERO_OR_ONE:
            // Quantifiers.  We handle single characters, quoted strings,
            // variable references, and segments.
            //  a+      matches  aaa
            //  'foo'+  matches  foofoofoo
            //  $v+     matches  xyxyxy if $v == xy
            //  (seg)+  matches  segsegseg
            {
                if (isSegment && buf.length() == bufStart) {
                    // The */+ immediately follows '('
                    return syntaxError(U_MISPLACED_QUANTIFIER, rule, start, status);
                }

                int32_t qstart, qlimit;
                // The */+ follows an isolated character or quote
                // or variable reference
                if (buf.length() == quoteLimit) {
                    // The */+ follows a 'quoted string'
                    qstart = quoteStart;
                    qlimit = quoteLimit;
                } else if (buf.length() == varLimit) {
                    // The */+ follows a $variableReference
                    qstart = varStart;
                    qlimit = varLimit;
                } else {
                    // The */+ follows a single character, possibly
                    // a segment standin
                    qstart = buf.length() - 1;
                    qlimit = qstart + 1;
                }

                UnicodeFunctor *m =
                    new StringMatcher(buf, qstart, qlimit, 0, *parser.curData);
                if (m == nullptr) {
                    return syntaxError(U_MEMORY_ALLOCATION_ERROR, rule, start, status);
                }
                int32_t min = 0;
                int32_t max = Quantifier::MAX;
                switch (c) {
                case ONE_OR_MORE:
                    min = 1;
                    break;
                case ZERO_OR_ONE:
                    min = 0;
                    max = 1;
                    break;
                // case KLEENE_STAR:
                //    do nothing -- min, max already set
                }
                m = new Quantifier(m, min, max);
                if (m == nullptr) {
                    return syntaxError(U_MEMORY_ALLOCATION_ERROR, rule, start, status);
                }
                buf.truncate(qstart);
                buf.append(parser.generateStandInFor(m, status));
            }
            break;

        //------------------------------------------------------
        // Elements allowed ONLY WITHIN segments
        //------------------------------------------------------
        case SEGMENT_CLOSE:
            // assert(isSegment);
            // We're done parsing a segment.
            done = true;
            break;

        //------------------------------------------------------
        // Elements allowed ONLY OUTSIDE segments
        //------------------------------------------------------
        case CONTEXT_ANTE:
            if (ante >= 0) {
                return syntaxError(U_MULTIPLE_ANTE_CONTEXTS, rule, start, status);
            }
            ante = buf.length();
            break;
        case CONTEXT_POST:
            if (post >= 0) {
                return syntaxError(U_MULTIPLE_POST_CONTEXTS, rule, start, status);
            }
            post = buf.length();
            break;
        case CURSOR_POS:
            if (cursor >= 0) {
                return syntaxError(U_MULTIPLE_CURSORS, rule, start, status);
            }
            cursor = buf.length();
            break;
        case CURSOR_OFFSET:
            if (cursorOffset < 0) {
                if (buf.length() > 0) {
                    return syntaxError(U_MISPLACED_CURSOR_OFFSET, rule, start, status);
                }
                --cursorOffset;
            } else if (cursorOffset > 0) {
                if (buf.length() != cursorOffsetPos || cursor >= 0) {
                    return syntaxError(U_MISPLACED_CURSOR_OFFSET, rule, start, status);
                }
                ++cursorOffset;
            } else {
                if (cursor == 0 && buf.length() == 0) {
                    cursorOffset = -1;
                } else if (cursor < 0) {
                    cursorOffsetPos = buf.length();
                    cursorOffset = 1;
                } else {
                    return syntaxError(U_MISPLACED_CURSOR_OFFSET, rule, start, status);
                }
            }
            break;


        //------------------------------------------------------
        // Non-special characters
        //------------------------------------------------------
        default:
            // Disallow unquoted characters other than [0-9A-Za-z]
            // in the printable ASCII range.  These characters are
            // reserved for possible future use.
            if (c >= 0x0021 && c <= 0x007E &&
                !((c >= 0x0030/*'0'*/ && c <= 0x0039/*'9'*/) ||
                  (c >= 0x0041/*'A'*/ && c <= 0x005A/*'Z'*/) ||
                  (c >= 0x0061/*'a'*/ && c <= 0x007A/*'z'*/))) {
                return syntaxError(U_UNQUOTED_SPECIAL, rule, start, status);
            }
            buf.append(c);
            break;
        }
    }

    return pos;
}

/**
 * Remove context.
 */
void RuleHalf::removeContext() {
    //text = text.substring(ante < 0 ? 0 : ante,
    //                      post < 0 ? text.length() : post);
    if (post >= 0) {
        text.remove(post);
    }
    if (ante >= 0) {
        text.removeBetween(0, ante);
    }
    ante = post = -1;
    anchorStart = anchorEnd = false;
}

/**
 * Return true if this half looks like valid output, that is, does not
 * contain quantifiers or other special input-only elements.
 */
UBool RuleHalf::isValidOutput(TransliteratorParser& transParser) {
    for (int32_t i=0; i<text.length(); ) {
        UChar32 c = text.char32At(i);
        i += U16_LENGTH(c);
        if (!transParser.parseData->isReplacer(c)) {
            return false;
        }
    }
    return true;
}

/**
 * Return true if this half looks like valid input, that is, does not
 * contain functions or other special output-only elements.
 */
UBool RuleHalf::isValidInput(TransliteratorParser& transParser) {
    for (int32_t i=0; i<text.length(); ) {
        UChar32 c = text.char32At(i);
        i += U16_LENGTH(c);
        if (!transParser.parseData->isMatcher(c)) {
            return false;
        }
    }
    return true;
}

//----------------------------------------------------------------------
// PUBLIC API
//----------------------------------------------------------------------

/**
 * Constructor.
 */
TransliteratorParser::TransliteratorParser(UErrorCode &statusReturn) :
dataVector(statusReturn),
idBlockVector(statusReturn),
variablesVector(statusReturn),
segmentObjects(statusReturn)
{
    idBlockVector.setDeleter(uprv_deleteUObject);
    curData = nullptr;
    compoundFilter = nullptr;
    parseData = nullptr;
    variableNames.setValueDeleter(uprv_deleteUObject);
}

/**
 * Destructor.
 */
TransliteratorParser::~TransliteratorParser() {
    while (!dataVector.isEmpty())
        delete static_cast<TransliterationRuleData*>(dataVector.orphanElementAt(0));
    delete compoundFilter;
    delete parseData;
    while (!variablesVector.isEmpty())
        delete static_cast<UnicodeFunctor*>(variablesVector.orphanElementAt(0));
}

void
TransliteratorParser::parse(const UnicodeString& rules,
                            UTransDirection transDirection,
                            UParseError& pe,
                            UErrorCode& ec) {
    if (U_SUCCESS(ec)) {
        parseRules(rules, transDirection, ec);
        pe = parseError;
    }
}

/**
 * Return the compound filter parsed by parse().  Caller owns result.
 */ 
UnicodeSet* TransliteratorParser::orphanCompoundFilter() {
    UnicodeSet* f = compoundFilter;
    compoundFilter = nullptr;
    return f;
}

//----------------------------------------------------------------------
// Private implementation
//----------------------------------------------------------------------

/**
 * Parse the given string as a sequence of rules, separated by newline
 * characters ('\n'), and cause this object to implement those rules.  Any
 * previous rules are discarded.  Typically this method is called exactly
 * once, during construction.
 * @exception IllegalArgumentException if there is a syntax error in the
 * rules
 */
void TransliteratorParser::parseRules(const UnicodeString& rule,
                                      UTransDirection theDirection,
                                      UErrorCode& status)
{
    // Clear error struct
    uprv_memset(&parseError, 0, sizeof(parseError));
    parseError.line = parseError.offset = -1;

    UBool parsingIDs = true;
    int32_t ruleCount = 0;
    
    while (!dataVector.isEmpty()) {
        delete static_cast<TransliterationRuleData*>(dataVector.orphanElementAt(0));
    }
    if (U_FAILURE(status)) {
        return;
    }

    idBlockVector.removeAllElements();
    curData = nullptr;
    direction = theDirection;
    ruleCount = 0;

    delete compoundFilter;
    compoundFilter = nullptr;

    while (!variablesVector.isEmpty()) {
        delete static_cast<UnicodeFunctor*>(variablesVector.orphanElementAt(0));
    }
    variableNames.removeAll();
    parseData = new ParseData(nullptr, &variablesVector, &variableNames);
    if (parseData == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }

    dotStandIn = static_cast<char16_t>(-1);

    UnicodeString *tempstr = nullptr; // used for memory allocation error checking
    UnicodeString str; // scratch
    UnicodeString idBlockResult;
    int32_t pos = 0;
    int32_t limit = rule.length();

    // The compound filter offset is an index into idBlockResult.
    // If it is 0, then the compound filter occurred at the start,
    // and it is the offset to the _start_ of the compound filter
    // pattern.  Otherwise it is the offset to the _limit_ of the
    // compound filter pattern within idBlockResult.
    compoundFilter = nullptr;
    int32_t compoundFilterOffset = -1;

    while (pos < limit && U_SUCCESS(status)) {
        char16_t c = rule.charAt(pos++);
        if (PatternProps::isWhiteSpace(c)) {
            // Ignore leading whitespace.
            continue;
        }
        // Skip lines starting with the comment character
        if (c == RULE_COMMENT_CHAR) {
            pos = rule.indexOf(static_cast<char16_t>(0x000A) /*\n*/, pos) + 1;
            if (pos == 0) {
                break; // No "\n" found; rest of rule is a comment
            }
            continue; // Either fall out or restart with next line
        }

        // skip empty rules
        if (c == END_OF_RULE)
            continue;

        // keep track of how many rules we've seen
        ++ruleCount;
        
        // We've found the start of a rule or ID.  c is its first
        // character, and pos points past c.
        --pos;
        // Look for an ID token.  Must have at least ID_TOKEN_LEN + 1
        // chars left.
        if ((pos + ID_TOKEN_LEN + 1) <= limit &&
                rule.compare(pos, ID_TOKEN_LEN, ID_TOKEN) == 0) {
            pos += ID_TOKEN_LEN;
            c = rule.charAt(pos);
            while (PatternProps::isWhiteSpace(c) && pos < limit) {
                ++pos;
                c = rule.charAt(pos);
            }

            int32_t p = pos;
            
            if (!parsingIDs) {
                if (curData != nullptr) {
                    U_ASSERT(!dataVector.hasDeleter());
                    if (direction == UTRANS_FORWARD)
                        dataVector.addElement(curData, status);
                    else
                        dataVector.insertElementAt(curData, 0, status);
                    if (U_FAILURE(status)) {
                        delete curData;
                    }
                    curData = nullptr;
                }
                parsingIDs = true;
            }

            TransliteratorIDParser::SingleID* id =
                TransliteratorIDParser::parseSingleID(rule, p, direction, status);
            if (p != pos && ICU_Utility::parseChar(rule, p, END_OF_RULE)) {
                // Successful ::ID parse.

                if (direction == UTRANS_FORWARD) {
                    idBlockResult.append(id->canonID).append(END_OF_RULE);
                } else {
                    idBlockResult.insert(0, END_OF_RULE);
                    idBlockResult.insert(0, id->canonID);
                }

            } else {
                // Couldn't parse an ID.  Try to parse a global filter
                int32_t withParens = -1;
                UnicodeSet* f = TransliteratorIDParser::parseGlobalFilter(rule, p, direction, withParens, nullptr);
                if (f != nullptr) {
                    if (ICU_Utility::parseChar(rule, p, END_OF_RULE)
                        && (direction == UTRANS_FORWARD) == (withParens == 0))
                    {
                        if (compoundFilter != nullptr) {
                            // Multiple compound filters
                            syntaxError(U_MULTIPLE_COMPOUND_FILTERS, rule, pos, status);
                            delete f;
                        } else {
                            compoundFilter = f;
                            compoundFilterOffset = ruleCount;
                        }
                    } else {
                        delete f;
                    }
                } else {
                    // Invalid ::id
                    // Can be parsed as neither an ID nor a global filter
                    syntaxError(U_INVALID_ID, rule, pos, status);
                }
            }
            delete id;
            pos = p;
        } else {
            if (parsingIDs) {
                tempstr = new UnicodeString(idBlockResult);
                // nullptr pointer check
                if (tempstr == nullptr) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                    return;
                }
                U_ASSERT(idBlockVector.hasDeleter());
                if (direction == UTRANS_FORWARD)
                    idBlockVector.adoptElement(tempstr, status);
                else
                    idBlockVector.insertElementAt(tempstr, 0, status);
                if (U_FAILURE(status)) {
                    return;
                }
                idBlockResult.remove();
                parsingIDs = false;
                curData = new TransliterationRuleData(status);
                // nullptr pointer check
                if (curData == nullptr) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                    return;
                }
                parseData->data = curData;

                // By default, rules use part of the private use area
                // E000..F8FF for variables and other stand-ins.  Currently
                // the range F000..F8FF is typically sufficient.  The 'use
                // variable range' pragma allows rule sets to modify this.
                setVariableRange(0xF000, 0xF8FF, status);
            }

            if (resemblesPragma(rule, pos, limit)) {
                int32_t ppp = parsePragma(rule, pos, limit, status);
                if (ppp < 0) {
                    syntaxError(U_MALFORMED_PRAGMA, rule, pos, status);
                }
                pos = ppp;
            // Parse a rule
            } else {
                pos = parseRule(rule, pos, limit, status);
            }
        }
    }

    if (parsingIDs && idBlockResult.length() > 0) {
        tempstr = new UnicodeString(idBlockResult);
        // nullptr pointer check
        if (tempstr == nullptr) {
            // TODO: Testing, forcing this path, shows many memory leaks. ICU-21701
            //       intltest translit/TransliteratorTest/TestInstantiation
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        if (direction == UTRANS_FORWARD)
            idBlockVector.adoptElement(tempstr, status);
        else
            idBlockVector.insertElementAt(tempstr, 0, status);
        if (U_FAILURE(status)) {
            return;
        }
    }
    else if (!parsingIDs && curData != nullptr) {
        if (direction == UTRANS_FORWARD) {
            dataVector.addElement(curData, status);
        } else {
            dataVector.insertElementAt(curData, 0, status);
        }
        if (U_FAILURE(status)) {
            delete curData;
            curData = nullptr;
        }
    }
    
    if (U_SUCCESS(status)) {
        // Convert the set vector to an array
        int32_t i, dataVectorSize = dataVector.size();
        for (i = 0; i < dataVectorSize; i++) {
            TransliterationRuleData* data = static_cast<TransliterationRuleData*>(dataVector.elementAt(i));
            data->variablesLength = variablesVector.size();
            if (data->variablesLength == 0) {
                data->variables = nullptr;
            } else {
                data->variables = static_cast<UnicodeFunctor**>(uprv_malloc(data->variablesLength * sizeof(UnicodeFunctor*)));
                // nullptr pointer check
                if (data->variables == nullptr) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                    return;
                }
                data->variablesAreOwned = (i == 0);
            }

            for (int32_t j = 0; j < data->variablesLength; j++) {
                data->variables[j] =
                    static_cast<UnicodeFunctor *>(variablesVector.elementAt(j));
            }
            
            data->variableNames.removeAll();
            int32_t p = UHASH_FIRST;
            const UHashElement* he = variableNames.nextElement(p);
            while (he != nullptr) {
                UnicodeString* tempus = static_cast<UnicodeString*>(he->value.pointer)->clone();
                if (tempus == nullptr) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                    return;
                }
                data->variableNames.put(*static_cast<UnicodeString*>(he->key.pointer),
                    tempus, status);
                he = variableNames.nextElement(p);
            }
        }
        variablesVector.removeAllElements();   // keeps them from getting deleted when we succeed

        // Index the rules
        if (compoundFilter != nullptr) {
            if ((direction == UTRANS_FORWARD && compoundFilterOffset != 1) ||
                (direction == UTRANS_REVERSE && compoundFilterOffset != ruleCount)) {
                status = U_MISPLACED_COMPOUND_FILTER;
            }
        }        

        for (i = 0; i < dataVectorSize; i++) {
            TransliterationRuleData* data = static_cast<TransliterationRuleData*>(dataVector.elementAt(i));
            data->ruleSet.freeze(parseError, status);
        }
        if (idBlockVector.size() == 1 && static_cast<UnicodeString*>(idBlockVector.elementAt(0))->isEmpty()) {
            idBlockVector.removeElementAt(0);
        }
    }
}

/**
 * Set the variable range to [start, end] (inclusive).
 */
void TransliteratorParser::setVariableRange(int32_t start, int32_t end, UErrorCode& status) {
    if (start > end || start < 0 || end > 0xFFFF) {
        status = U_MALFORMED_PRAGMA;
        return;
    }
    
    curData->variablesBase = static_cast<char16_t>(start);
    if (dataVector.size() == 0) {
        variableNext = static_cast<char16_t>(start);
        variableLimit = static_cast<char16_t>(end + 1);
    }
}

/**
 * Assert that the given character is NOT within the variable range.
 * If it is, return false.  This is necessary to ensure that the
 * variable range does not overlap characters used in a rule.
 */
UBool TransliteratorParser::checkVariableRange(UChar32 ch) const {
    return !(ch >= curData->variablesBase && ch < variableLimit);
}

/**
 * Set the maximum backup to 'backup', in response to a pragma
 * statement.
 */
void TransliteratorParser::pragmaMaximumBackup(int32_t /*backup*/) {
    //TODO Finish
}

/**
 * Begin normalizing all rules using the given mode, in response
 * to a pragma statement.
 */
void TransliteratorParser::pragmaNormalizeRules(UNormalizationMode /*mode*/) {
    //TODO Finish
}

static const char16_t PRAGMA_USE[] = {0x75,0x73,0x65,0x20,0}; // "use "

static const char16_t PRAGMA_VARIABLE_RANGE[] = {0x7E,0x76,0x61,0x72,0x69,0x61,0x62,0x6C,0x65,0x20,0x72,0x61,0x6E,0x67,0x65,0x20,0x23,0x20,0x23,0x7E,0x3B,0}; // "~variable range # #~;"

static const char16_t PRAGMA_MAXIMUM_BACKUP[] = {0x7E,0x6D,0x61,0x78,0x69,0x6D,0x75,0x6D,0x20,0x62,0x61,0x63,0x6B,0x75,0x70,0x20,0x23,0x7E,0x3B,0}; // "~maximum backup #~;"

static const char16_t PRAGMA_NFD_RULES[] = {0x7E,0x6E,0x66,0x64,0x20,0x72,0x75,0x6C,0x65,0x73,0x7E,0x3B,0}; // "~nfd rules~;"

static const char16_t PRAGMA_NFC_RULES[] = {0x7E,0x6E,0x66,0x63,0x20,0x72,0x75,0x6C,0x65,0x73,0x7E,0x3B,0}; // "~nfc rules~;"

/**
 * Return true if the given rule looks like a pragma.
 * @param pos offset to the first non-whitespace character
 * of the rule.
 * @param limit pointer past the last character of the rule.
 */
UBool TransliteratorParser::resemblesPragma(const UnicodeString& rule, int32_t pos, int32_t limit) {
    // Must start with /use\s/i
    return ICU_Utility::parsePattern(rule, pos, limit, UnicodeString(true, PRAGMA_USE, 4), nullptr) >= 0;
}

/**
 * Parse a pragma.  This method assumes resemblesPragma() has
 * already returned true.
 * @param pos offset to the first non-whitespace character
 * of the rule.
 * @param limit pointer past the last character of the rule.
 * @return the position index after the final ';' of the pragma,
 * or -1 on failure.
 */
int32_t TransliteratorParser::parsePragma(const UnicodeString& rule, int32_t pos, int32_t limit, UErrorCode& status) {
    int32_t array[2];
    
    // resemblesPragma() has already returned true, so we
    // know that pos points to /use\s/i; we can skip 4 characters
    // immediately
    pos += 4;
    
    // Here are the pragmas we recognize:
    // use variable range 0xE000 0xEFFF;
    // use maximum backup 16;
    // use nfd rules;
    // use nfc rules;
    int p = ICU_Utility::parsePattern(rule, pos, limit, UnicodeString(true, PRAGMA_VARIABLE_RANGE, -1), array);
    if (p >= 0) {
        setVariableRange(array[0], array[1], status);
        return p;
    }
    
    p = ICU_Utility::parsePattern(rule, pos, limit, UnicodeString(true, PRAGMA_MAXIMUM_BACKUP, -1), array);
    if (p >= 0) {
        pragmaMaximumBackup(array[0]);
        return p;
    }
    
    p = ICU_Utility::parsePattern(rule, pos, limit, UnicodeString(true, PRAGMA_NFD_RULES, -1), nullptr);
    if (p >= 0) {
        pragmaNormalizeRules(UNORM_NFD);
        return p;
    }
    
    p = ICU_Utility::parsePattern(rule, pos, limit, UnicodeString(true, PRAGMA_NFC_RULES, -1), nullptr);
    if (p >= 0) {
        pragmaNormalizeRules(UNORM_NFC);
        return p;
    }
    
    // Syntax error: unable to parse pragma
    return -1;
}

/**
 * MAIN PARSER.  Parse the next rule in the given rule string, starting
 * at pos.  Return the index after the last character parsed.  Do not
 * parse characters at or after limit.
 *
 * Important:  The character at pos must be a non-whitespace character
 * that is not the comment character.
 *
 * This method handles quoting, escaping, and whitespace removal.  It
 * parses the end-of-rule character.  It recognizes context and cursor
 * indicators.  Once it does a lexical breakdown of the rule at pos, it
 * creates a rule object and adds it to our rule list.
 */
int32_t TransliteratorParser::parseRule(const UnicodeString& rule, int32_t pos, int32_t limit, UErrorCode& status) {
    // Locate the left side, operator, and right side
    int32_t start = pos;
    char16_t op = 0;
    int32_t i;

    // Set up segments data
    segmentStandins.truncate(0);
    segmentObjects.removeAllElements();

    // Use pointers to automatics to make swapping possible.
    RuleHalf _left(*this), _right(*this);
    RuleHalf* left = &_left;
    RuleHalf* right = &_right;

    undefinedVariableName.remove();
    pos = left->parse(rule, pos, limit, status);
    if (U_FAILURE(status)) {
        return start;
    }

    if (pos == limit || u_strchr(gOPERATORS, (op = rule.charAt(--pos))) == nullptr) {
        return syntaxError(U_MISSING_OPERATOR, rule, start, status);
    }
    ++pos;

    // Found an operator char.  Check for forward-reverse operator.
    if (op == REVERSE_RULE_OP &&
        (pos < limit && rule.charAt(pos) == FORWARD_RULE_OP)) {
        ++pos;
        op = FWDREV_RULE_OP;
    }

    // Translate alternate op characters.
    switch (op) {
    case ALT_FORWARD_RULE_OP:
        op = FORWARD_RULE_OP;
        break;
    case ALT_REVERSE_RULE_OP:
        op = REVERSE_RULE_OP;
        break;
    case ALT_FWDREV_RULE_OP:
        op = FWDREV_RULE_OP;
        break;
    }

    pos = right->parse(rule, pos, limit, status);
    if (U_FAILURE(status)) {
        return start;
    }

    if (pos < limit) {
        if (rule.charAt(--pos) == END_OF_RULE) {
            ++pos;
        } else {
            // RuleHalf parser must have terminated at an operator
            return syntaxError(U_UNQUOTED_SPECIAL, rule, start, status);
        }
    }

    if (op == VARIABLE_DEF_OP) {
        // LHS is the name.  RHS is a single character, either a literal
        // or a set (already parsed).  If RHS is longer than one
        // character, it is either a multi-character string, or multiple
        // sets, or a mixture of chars and sets -- syntax error.

        // We expect to see a single undefined variable (the one being
        // defined).
        if (undefinedVariableName.length() == 0) {
            // "Missing '$' or duplicate definition"
            return syntaxError(U_BAD_VARIABLE_DEFINITION, rule, start, status);
        }
        if (left->text.length() != 1 || left->text.charAt(0) != variableLimit) {
            // "Malformed LHS"
            return syntaxError(U_MALFORMED_VARIABLE_DEFINITION, rule, start, status);
        }
        if (left->anchorStart || left->anchorEnd ||
            right->anchorStart || right->anchorEnd) {
            return syntaxError(U_MALFORMED_VARIABLE_DEFINITION, rule, start, status);
        } 
        // We allow anything on the right, including an empty string.
        UnicodeString* value = new UnicodeString(right->text);
        // nullptr pointer check
        if (value == nullptr) {
            return syntaxError(U_MEMORY_ALLOCATION_ERROR, rule, start, status);
        }
        variableNames.put(undefinedVariableName, value, status);
        ++variableLimit;
        return pos;
    }

    // If this is not a variable definition rule, we shouldn't have
    // any undefined variable names.
    if (undefinedVariableName.length() != 0) {
        return syntaxError(// "Undefined variable $" + undefinedVariableName,
                    U_UNDEFINED_VARIABLE,
                    rule, start, status);
    }

    // Verify segments
    if (segmentStandins.length() > segmentObjects.size()) {
        syntaxError(U_UNDEFINED_SEGMENT_REFERENCE, rule, start, status);
    }
    for (i=0; i<segmentStandins.length(); ++i) {
        if (segmentStandins.charAt(i) == 0) {
            syntaxError(U_INTERNAL_TRANSLITERATOR_ERROR, rule, start, status); // will never happen
        }
    }
    for (i=0; i<segmentObjects.size(); ++i) {
        if (segmentObjects.elementAt(i) == nullptr) {
            syntaxError(U_INTERNAL_TRANSLITERATOR_ERROR, rule, start, status); // will never happen
        }
    }
    
    // If the direction we want doesn't match the rule
    // direction, do nothing.
    if (op != FWDREV_RULE_OP &&
        ((direction == UTRANS_FORWARD) != (op == FORWARD_RULE_OP))) {
        return pos;
    }

    // Transform the rule into a forward rule by swapping the
    // sides if necessary.
    if (direction == UTRANS_REVERSE) {
        left = &_right;
        right = &_left;
    }

    // Remove non-applicable elements in forward-reverse
    // rules.  Bidirectional rules ignore elements that do not
    // apply.
    if (op == FWDREV_RULE_OP) {
        right->removeContext();
        left->cursor = -1;
        left->cursorOffset = 0;
    }

    // Normalize context
    if (left->ante < 0) {
        left->ante = 0;
    }
    if (left->post < 0) {
        left->post = left->text.length();
    }

    // Context is only allowed on the input side.  Cursors are only
    // allowed on the output side.  Segment delimiters can only appear
    // on the left, and references on the right.  Cursor offset
    // cannot appear without an explicit cursor.  Cursor offset
    // cannot place the cursor outside the limits of the context.
    // Anchors are only allowed on the input side.
    if (right->ante >= 0 || right->post >= 0 || left->cursor >= 0 ||
        (right->cursorOffset != 0 && right->cursor < 0) ||
        // - The following two checks were used to ensure that the
        // - the cursor offset stayed within the ante- or postcontext.
        // - However, with the addition of quantifiers, we have to
        // - allow arbitrary cursor offsets and do runtime checking.
        //(right->cursorOffset > (left->text.length() - left->post)) ||
        //(-right->cursorOffset > left->ante) ||
        right->anchorStart || right->anchorEnd ||
        !left->isValidInput(*this) || !right->isValidOutput(*this) ||
        left->ante > left->post) {

        return syntaxError(U_MALFORMED_RULE, rule, start, status);
    }

    // Flatten segment objects vector to an array
    UnicodeFunctor** segmentsArray = nullptr;
    if (segmentObjects.size() > 0) {
        segmentsArray = static_cast<UnicodeFunctor**>(uprv_malloc(segmentObjects.size() * sizeof(UnicodeFunctor*)));
        // Null pointer check
        if (segmentsArray == nullptr) {
            return syntaxError(U_MEMORY_ALLOCATION_ERROR, rule, start, status);
        }
        segmentObjects.toArray(reinterpret_cast<void**>(segmentsArray));
    }
    TransliterationRule* temptr = new TransliterationRule(
            left->text, left->ante, left->post,
            right->text, right->cursor, right->cursorOffset,
            segmentsArray,
            segmentObjects.size(),
            left->anchorStart, left->anchorEnd,
            curData,
            status);
    //Null pointer check
    if (temptr == nullptr) {
        uprv_free(segmentsArray);
        return syntaxError(U_MEMORY_ALLOCATION_ERROR, rule, start, status);
    }

    curData->ruleSet.addRule(temptr, status);

    return pos;
}

/**
 * Called by main parser upon syntax error.  Search the rule string
 * for the probable end of the rule.  Of course, if the error is that
 * the end of rule marker is missing, then the rule end will not be found.
 * In any case the rule start will be correctly reported.
 * @param msg error description
 * @param rule pattern string
 * @param start position of first character of current rule
 */
int32_t TransliteratorParser::syntaxError(UErrorCode parseErrorCode,
                                          const UnicodeString& rule,
                                          int32_t pos,
                                          UErrorCode& status)
{
    parseError.offset = pos;
    parseError.line = 0 ; /* we are not using line numbers */
    
    // for pre-context
    const int32_t LEN = U_PARSE_CONTEXT_LEN - 1;
    int32_t start = uprv_max(pos - LEN, 0);
    int32_t stop  = pos;
    
    rule.extract(start,stop-start,parseError.preContext);
    //null terminate the buffer
    parseError.preContext[stop-start] = 0;
    
    //for post-context
    start = pos;
    stop  = uprv_min(pos + LEN, rule.length());
    
    rule.extract(start,stop-start,parseError.postContext);
    //null terminate the buffer
    parseError.postContext[stop-start]= 0;

    status = parseErrorCode;
    return pos;

}

/**
 * Parse a UnicodeSet out, store it, and return the stand-in character
 * used to represent it.
 */
char16_t TransliteratorParser::parseSet(const UnicodeString& rule,
                                          ParsePosition& pos,
                                          UErrorCode& status) {
    UnicodeSet* set = new UnicodeSet(rule, pos, USET_IGNORE_SPACE, parseData, status);
    // Null pointer check
    if (set == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return static_cast<char16_t>(0x0000); // Return empty character with error.
    }
    set->compact();
    return generateStandInFor(set, status);
}

/**
 * Generate and return a stand-in for a new UnicodeFunctor.  Store
 * the matcher (adopt it).
 */
char16_t TransliteratorParser::generateStandInFor(UnicodeFunctor* adopted, UErrorCode& status) {
    // assert(obj != null);
    
    // Look up previous stand-in, if any.  This is a short list
    // (typical n is 0, 1, or 2); linear search is optimal.
    for (int32_t i=0; i<variablesVector.size(); ++i) {
        if (variablesVector.elementAt(i) == adopted) { // [sic] pointer comparison
            return static_cast<char16_t>(curData->variablesBase + i);
        }
    }
    
    if (variableNext >= variableLimit) {
        delete adopted;
        status = U_VARIABLE_RANGE_EXHAUSTED;
        return 0;
    }
    variablesVector.addElement(adopted, status);
    if (U_FAILURE(status)) {
        delete adopted;
        return 0;
    }
    return variableNext++;
}

/**
 * Return the standin for segment seg (1-based).
 */
char16_t TransliteratorParser::getSegmentStandin(int32_t seg, UErrorCode& status) {
    // Special character used to indicate an empty spot
    char16_t empty = curData->variablesBase - 1;
    while (segmentStandins.length() < seg) {
        segmentStandins.append(empty);
    }
    char16_t c = segmentStandins.charAt(seg-1);
    if (c == empty) {
        if (variableNext >= variableLimit) {
            status = U_VARIABLE_RANGE_EXHAUSTED;
            return 0;
        }
        c = variableNext++;
        // Set a placeholder in the primary variables vector that will be
        // filled in later by setSegmentObject().  We know that we will get
        // called first because setSegmentObject() will call us.
        variablesVector.addElement((void*) nullptr, status);
        segmentStandins.setCharAt(seg-1, c);
    }
    return c;
}

/**
 * Set the object for segment seg (1-based).
 */
void TransliteratorParser::setSegmentObject(int32_t seg, StringMatcher* adopted, UErrorCode& status) {
    // Since we call parseSection() recursively, nested
    // segments will result in segment i+1 getting parsed
    // and stored before segment i; be careful with the
    // vector handling here.
    if (segmentObjects.size() < seg) {
        segmentObjects.setSize(seg, status);
    }
    if (U_FAILURE(status)) {
        return;
    }
    int32_t index = getSegmentStandin(seg, status) - curData->variablesBase;
    if (segmentObjects.elementAt(seg-1) != nullptr ||
        variablesVector.elementAt(index) != nullptr) {
        // should never happen
        if (U_SUCCESS(status)) {status = U_INTERNAL_TRANSLITERATOR_ERROR;}
        return;
    }
    // Note: neither segmentObjects or variablesVector has an object deleter function.
    segmentObjects.setElementAt(adopted, seg-1);
    variablesVector.setElementAt(adopted, index);
}

/**
 * Return the stand-in for the dot set.  It is allocated the first
 * time and reused thereafter.
 */
char16_t TransliteratorParser::getDotStandIn(UErrorCode& status) {
    if (dotStandIn == static_cast<char16_t>(-1)) {
        UnicodeSet* tempus = new UnicodeSet(UnicodeString(true, DOT_SET, -1), status);
        // Null pointer check.
        if (tempus == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return static_cast<char16_t>(0x0000);
        }
        dotStandIn = generateStandInFor(tempus, status);
    }
    return dotStandIn;
}

/**
 * Append the value of the given variable name to the given
 * UnicodeString.
 */
void TransliteratorParser::appendVariableDef(const UnicodeString& name,
                                                  UnicodeString& buf,
                                                  UErrorCode& status) {
    const UnicodeString* s = static_cast<const UnicodeString*>(variableNames.get(name));
    if (s == nullptr) {
        // We allow one undefined variable so that variable definition
        // statements work.  For the first undefined variable we return
        // the special placeholder variableLimit-1, and save the variable
        // name.
        if (undefinedVariableName.length() == 0) {
            undefinedVariableName = name;
            if (variableNext >= variableLimit) {
                // throw new RuntimeException("Private use variables exhausted");
                status = U_ILLEGAL_ARGUMENT_ERROR;
                return;
            }
            buf.append(--variableLimit);
        } else {
            //throw new IllegalArgumentException("Undefined variable $"
            //                                   + name);
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
    } else {
        buf.append(*s);
    }
}

/**
 * Glue method to get around access restrictions in C++.
 */
/*Transliterator* TransliteratorParser::createBasicInstance(const UnicodeString& id, const UnicodeString* canonID) {
    return Transliterator::createBasicInstance(id, canonID);
}*/

U_NAMESPACE_END

U_CAPI int32_t
utrans_stripRules(const char16_t *source, int32_t sourceLen, char16_t *target, UErrorCode *status) {
    U_NAMESPACE_USE

    //const char16_t *sourceStart = source;
    const char16_t *targetStart = target;
    const char16_t *sourceLimit = source+sourceLen;
    char16_t *targetLimit = target+sourceLen;
    UChar32 c = 0;
    UBool quoted = false;
    int32_t index;

    uprv_memset(target, 0, sourceLen*U_SIZEOF_UCHAR);

    /* read the rules into the buffer */
    while (source < sourceLimit)
    {
        index=0;
        U16_NEXT_UNSAFE(source, index, c);
        source+=index;
        if(c == QUOTE) {
            quoted = !quoted;
        }
        else if (!quoted) {
            if (c == RULE_COMMENT_CHAR) {
                /* skip comments and all preceding spaces */
                while (targetStart < target && *(target - 1) == 0x0020) {
                    target--;
                }
                do {
                    if (source == sourceLimit) {
                        c = U_SENTINEL;
                        break;
                    }
                    c = *(source++);
                }
                while (c != CR && c != LF);
                if (c < 0) {
                    break;
                }
            }
            else if (c == ESCAPE && source < sourceLimit) {
                UChar32   c2 = *source;
                if (c2 == CR || c2 == LF) {
                    /* A backslash at the end of a line. */
                    /* Since we're stripping lines, ignore the backslash. */
                    source++;
                    continue;
                }
                if (c2 == 0x0075 && source+5 < sourceLimit) { /* \u seen. \U isn't unescaped. */
                    int32_t escapeOffset = 0;
                    UnicodeString escapedStr(source, 5);
                    c2 = escapedStr.unescapeAt(escapeOffset);

                    if (c2 == (UChar32)0xFFFFFFFF || escapeOffset == 0)
                    {
                        *status = U_PARSE_ERROR;
                        return 0;
                    }
                    if (!PatternProps::isWhiteSpace(c2) && !u_iscntrl(c2) && !u_ispunct(c2)) {
                        /* It was escaped for a reason. Write what it was suppose to be. */
                        source+=5;
                        c = c2;
                    }
                }
                else if (c2 == QUOTE) {
                    /* \' seen. Make sure we don't do anything when we see it again. */
                    quoted = !quoted;
                }
            }
        }
        if (c == CR || c == LF)
        {
            /* ignore spaces carriage returns, and all leading spaces on the next line.
            * and line feed unless in the form \uXXXX
            */
            quoted = false;
            while (source < sourceLimit) {
                c = *(source);
                if (c != CR && c != LF && c != 0x0020) {
                    break;
                }
                source++;
            }
            continue;
        }

        /* Append char16_t * after dissembling if c > 0xffff*/
        index=0;
        U16_APPEND_UNSAFE(target, index, c);
        target+=index;
    }
    if (target < targetLimit) {
        *target = 0;
    }
    return (int32_t)(target-targetStart);
}

#endif /* #if !UCONFIG_NO_TRANSLITERATION */

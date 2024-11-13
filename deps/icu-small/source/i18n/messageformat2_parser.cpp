// © 2024 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#if !UCONFIG_NO_MF2

#include "messageformat2_errors.h"
#include "messageformat2_macros.h"
#include "messageformat2_parser.h"
#include "uvector.h" // U_ASSERT

U_NAMESPACE_BEGIN

namespace message2 {

using namespace pluralimpl;

using namespace data_model;

/*
    The `ERROR()` macro sets a syntax error in the context
    and sets the offset in `parseError` to `index`. It does not alter control flow.
*/
#define ERROR(errorCode)                                                                                \
    if (!errors.hasSyntaxError()) {                                                                     \
        setParseError(parseError, index);                                                               \
        errors.addSyntaxError(errorCode);                                                               \
    }

#define ERROR_AT(errorCode, i)                                                                          \
    if (!errors.hasSyntaxError()) {                                                                     \
        setParseError(parseError, i);                                                                   \
        errors.addSyntaxError(errorCode);                                                               \
    }

// Increments the line number and updates the "characters seen before
// current line" count in `parseError`, iff `peek()` is a newline
void Parser::maybeAdvanceLine() {
    if (peek() == LF) {
        parseError.line++;
        // add 1 to index to get the number of characters seen so far
        // (including the newline)
        parseError.lengthBeforeCurrentLine = index + 1;
    }
}

/*
    Signals an error and returns either if `parseError` already denotes an
    error, or `index` is out of bounds for the string `source`
*/
#define CHECK_BOUNDS(errorCode)                                                            \
    if (!inBounds()) {                                                                     \
        ERROR(errorCode);                                                                  \
        return;                                                                            \
    }
#define CHECK_BOUNDS_1(errorCode)                                                          \
    if (!inBounds(1)) {                                                                    \
        ERROR_AT(errorCode, index + 1);                                                    \
        return;                                                                            \
    }

// -------------------------------------
// Helper functions

static void copyContext(const UChar in[U_PARSE_CONTEXT_LEN], UChar out[U_PARSE_CONTEXT_LEN]) {
    for (int32_t i = 0; i < U_PARSE_CONTEXT_LEN; i++) {
        out[i] = in[i];
        if (in[i] == '\0') {
            break;
        }
    }
}

/* static */ void Parser::translateParseError(const MessageParseError &messageParseError, UParseError &parseError) {
    parseError.line = messageParseError.line;
    parseError.offset = messageParseError.offset;
    copyContext(messageParseError.preContext, parseError.preContext);
    copyContext(messageParseError.postContext, parseError.postContext);
}

/* static */ void Parser::setParseError(MessageParseError &parseError, uint32_t index) {
    // Translate absolute to relative offset
    parseError.offset = index                               // Start with total number of characters seen
                      - parseError.lengthBeforeCurrentLine; // Subtract all characters before the current line
    // TODO: Fill this in with actual pre and post-context
    parseError.preContext[0] = 0;
    parseError.postContext[0] = 0;
}

// -------------------------------------
// Predicates

// Returns true if `c` is in the interval [`first`, `last`]
static bool inRange(UChar32 c, UChar32 first, UChar32 last) {
    U_ASSERT(first < last);
    return c >= first && c <= last;
}

/*
  The following helper predicates should exactly match nonterminals in the MessageFormat 2 grammar:

  `isContentChar()`   : `content-char`
  `isTextChar()`      : `text-char`
  `isAlpha()`         : `ALPHA`
  `isDigit()`         : `DIGIT`
  `isNameStart()`     : `name-start`
  `isNameChar()`      : `name-char`
  `isUnquotedStart()` : `unquoted-start`
  `isQuotedChar()`    : `quoted-char`
  `isWhitespace()`    : `s`
*/

static bool isContentChar(UChar32 c) {
    return inRange(c, 0x0001, 0x0008)    // Omit NULL, HTAB and LF
           || inRange(c, 0x000B, 0x000C) // Omit CR
           || inRange(c, 0x000E, 0x001F) // Omit SP
           || inRange(c, 0x0021, 0x002D) // Omit '.'
           || inRange(c, 0x002F, 0x003F) // Omit '@'
           || inRange(c, 0x0041, 0x005B) // Omit '\'
           || inRange(c, 0x005D, 0x007A) // Omit { | }
           || inRange(c, 0x007E, 0xD7FF) // Omit surrogates
           || inRange(c, 0xE000, 0x10FFFF);
}

// See `s` in the MessageFormat 2 grammar
inline bool isWhitespace(UChar32 c) {
    switch (c) {
    case SPACE:
    case HTAB:
    case CR:
    case LF:
    case IDEOGRAPHIC_SPACE:
        return true;
    default:
        return false;
    }
}

static bool isTextChar(UChar32 c) {
    return isContentChar(c)
        || isWhitespace(c)
        || c == PERIOD
        || c == AT
        || c == PIPE;
}

static bool isAlpha(UChar32 c) { return inRange(c, 0x0041, 0x005A) || inRange(c, 0x0061, 0x007A); }

static bool isDigit(UChar32 c) { return inRange(c, 0x0030, 0x0039); }

static bool isNameStart(UChar32 c) {
    return isAlpha(c) || c == UNDERSCORE || inRange(c, 0x00C0, 0x00D6) || inRange(c, 0x00D8, 0x00F6) ||
           inRange(c, 0x00F8, 0x02FF) || inRange(c, 0x0370, 0x037D) || inRange(c, 0x037F, 0x1FFF) ||
           inRange(c, 0x200C, 0x200D) || inRange(c, 0x2070, 0x218F) || inRange(c, 0x2C00, 0x2FEF) ||
           inRange(c, 0x3001, 0xD7FF) || inRange(c, 0xF900, 0xFDCF) || inRange(c, 0xFDF0, 0xFFFD) ||
           inRange(c, 0x10000, 0xEFFFF);
}

static bool isNameChar(UChar32 c) {
    return isNameStart(c) || isDigit(c) || c == HYPHEN || c == PERIOD || c == 0x00B7 ||
           inRange(c, 0x0300, 0x036F) || inRange(c, 0x203F, 0x2040);
}

static bool isUnquotedStart(UChar32 c) {
    return isNameStart(c) || isDigit(c) || c == HYPHEN || c == PERIOD || c == 0x00B7 ||
           inRange(c, 0x0300, 0x036F) || inRange(c, 0x203F, 0x2040);
}

static bool isQuotedChar(UChar32 c) {
    return isContentChar(c)
        || isWhitespace(c)
        || c == PERIOD
        || c == AT
        || c == LEFT_CURLY_BRACE
        || c == RIGHT_CURLY_BRACE;
}

static bool isEscapableChar(UChar32 c) {
    return c == PIPE
        || c == BACKSLASH
        || c == LEFT_CURLY_BRACE
        || c == RIGHT_CURLY_BRACE;
}

// Returns true iff `c` can begin a `function` nonterminal
static bool isFunctionStart(UChar32 c) {
    switch (c) {
    case COLON: {
        return true;
    }
    default: {
        return false;
    }
    }
}

// Returns true iff `c` can begin an `annotation` nonterminal
static bool isAnnotationStart(UChar32 c) {
    return isFunctionStart(c);
}

// Returns true iff `c` can begin a `literal` nonterminal
static bool isLiteralStart(UChar32 c) {
    return (c == PIPE || isNameStart(c) || c == HYPHEN || isDigit(c));
}

// Returns true iff `c` can begin a `key` nonterminal
static bool isKeyStart(UChar32 c) {
    return (c == ASTERISK || isLiteralStart(c));
}

bool Parser::isDeclarationStart() {
    return (peek() == ID_LOCAL[0]
            && inBounds(1)
            && peek(1) == ID_LOCAL[1])
        || (peek() == ID_INPUT[0]
            && inBounds(1)
            && peek(1) == ID_INPUT[1]);
}

// -------------------------------------
// Parsing functions


/*
  TODO: Since handling the whitespace ambiguities needs to be repeated
  in several different places and is hard to factor out,
  it probably would be better to replace the parser with a lexer + parser
  to separate tokenizing from parsing, which would simplify the code significantly.
  This has the disadvantage that there is no token grammar for MessageFormat,
  so one would have to be invented that isn't a component of the spec.
 */

/*
    This is a recursive-descent scannerless parser that,
    with a few exceptions, uses 1 character of lookahead.

    This may not be an exhaustive list, as the additions of attributes and reserved
    statements introduced several new ambiguities.

All but three of the exceptions involve ambiguities about the meaning of whitespace.
One ambiguity not involving whitespace is:
identifier -> namespace ":" name
vs.
identifier -> name

`namespace` and `name` can't be distinguished without arbitrary lookahead.
(For how this is handled, see parseIdentifier())

The second ambiguity not involving whitespace is:
complex-message -> *(declaration[s]) complex-body
                -> declaration *(declaration[s]) complex-body
                -> declaration complex-body
                -> reserved-statement complex-body
                -> .foo {$x} .match // ...
When processing the '.', arbitrary lookahead is required to distinguish the
arbitrary-length unsupported keyword from `.match`.
(For how this is handled, see parseDeclarations()).

The third ambiguity not involving whitespace is:
complex-message -> *(declaration [s]) complex-body
                -> reserved-statement *(declaration [s]) complex-body
                -> reserved-statement complex-body
                -> reserved-statement quotedPattern
                -> reserved-keyword [s reserved-body] 1*([s] expression) quoted-pattern
                -> reserved-keyword expression quoted-pattern
 Example: .foo {1} {{1}}

 Without lookahead, the opening '{' of the quoted pattern can't be distinguished
 from the opening '{' of another expression in the unsupported statement.
 (Though this only requires 1 character of lookahead.)

 Otherwise:

There are at least seven ambiguities in the grammar that can't be resolved with finite
lookahead (since whitespace sequences can be arbitrarily long). They are resolved
with a form of backtracking (early exit). No state needs to be saved/restored
since whitespace doesn't affect the shape of the resulting parse tree, so it's
not true backtracking.

In addition, the grammar has been refactored
in a semantics-preserving way in some cases to make the code easier to structure.

First: variant = when 1*(s key) [s] pattern
   Example: when k     {a}
   When reading the first space after 'k', it's ambiguous whether it's the
   required space before another key, or the optional space before `pattern`.
 (See comments in parseNonEmptyKeys())

Second: expression = "{" [s] (((literal / variable) [s annotation]) / annotation) [s] "}"
        annotation = (function *(s option)) / reserved
   Example: {:f    }
   When reading the first space after 'f', it's ambiguous whether it's the
   required space before an option, or the optional trailing space after an options list
   (in this case, the options list is empty).
 (See comments in parseOptions() -- handling this case also meant it was easier to base
  the code on a slightly refactored grammar, which should be semantically equivalent.)

Third: expression = "{" [s] (((literal / variable) [s annotation]) / annotation) [s] "}"
        annotation = (function *(s option)) / reserved
   Example: {@a }
   Similar to the previous case; see comments in parseReserved()

Fourth: expression = "{" [s] (((literal / variable) [s annotation]) / annotation) [s] "}"
   Example: {|foo|   }
   When reading the first space after the '|', it's ambiguous whether it's the required
   space before an annotation, or the optional trailing space before the '}'.
  (See comments in parseLiteralOrVariableWithAnnotation(); handling this case relies on
  the same grammar refactoring as the second exception.)

    Most functions match a non-terminal in the grammar, except as explained
    in comments.

Fifth: matcher = match-statement 1*([s] variant)
               -> match 1 *([s] selector) 1*([s] variant)
    Example: match {42} * {{_}}
 When reading the space after the first '}', it's unclear whether
 it's the optional space before another selector, or the optional space
 before a variant.

Sixth: annotation-expression = "{" [s] annotation *(s attribute) [s] "}"
       -> "{" [s] function *(s attribute) [s] "}"
       -> "{" [s] ":" identifier *(s option) *(s attribute) [s] "}"
       -> "{" [s] ":" identifier s attribute *(s attribute) [s] "}"

     Example: {:func @foo}
(Note: the same ambiguity is present with variable-expression and literal-expression)

Seventh:


When parsing the space, it's unclear whether it's the optional space before an
option, or the optional space before an attribute.

 Unless otherwise noted in a comment, all helper functions that take
    a `source` string, an `index` unsigned int, and an `errorCode` `UErrorCode`
    have the precondition:
      `index` < `len()`
    and the postcondition:
      `U_FAILURE(errorCode)` || `index < `len()`
*/

/*
  No pre, no post.
  A message may end with whitespace, so `index` may equal `len()` on exit.
*/
void Parser::parseWhitespaceMaybeRequired(bool required, UErrorCode& errorCode) {
    bool sawWhitespace = false;

    // The loop exits either when we consume all the input,
    // or when we see a non-whitespace character.
    while (true) {
        // Check if all input has been consumed
        if (!inBounds()) {
            // If whitespace isn't required -- or if we saw it already --
            // then the caller is responsible for checking this case and
            // setting an error if necessary.
            if (!required || sawWhitespace) {
                // Not an error.
                return;
            }
            // Otherwise, whitespace is required; the end of the input has
            // been reached without whitespace. This is an error.
            ERROR(errorCode);
            return;
        }

        // Input remains; process the next character if it's whitespace,
        // exit the loop otherwise
        if (isWhitespace(peek())) {
            sawWhitespace = true;
            // Increment line number in parse error if we consume a newline
            maybeAdvanceLine();
            next();
        } else {
            break;
        }
    }

    if (!sawWhitespace && required) {
        ERROR(errorCode);
    }
}

/*
  No pre, no post, for the same reason as `parseWhitespaceMaybeRequired()`.
*/
void Parser::parseRequiredWhitespace(UErrorCode& errorCode) {
    parseWhitespaceMaybeRequired(true, errorCode);
    normalizedInput += SPACE;
}

/*
  No pre, no post, for the same reason as `parseWhitespaceMaybeRequired()`.
*/
void Parser::parseOptionalWhitespace(UErrorCode& errorCode) {
    parseWhitespaceMaybeRequired(false, errorCode);
}

// Consumes a single character, signaling an error if `peek()` != `c`
// No postcondition -- a message can end with a '}' token
void Parser::parseToken(UChar32 c, UErrorCode& errorCode) {
    CHECK_BOUNDS(errorCode);

    if (peek() == c) {
        next();
        normalizedInput += c;
        return;
    }
    // Next character didn't match -- error out
    ERROR(errorCode);
}

/*
   Consumes a fixed-length token, signaling an error if the token isn't a prefix of
   the string beginning at `peek()`
   No postcondition -- a message can end with a '}' token
*/
void Parser::parseToken(const std::u16string_view& token, UErrorCode& errorCode) {
    U_ASSERT(inBounds());

    int32_t tokenPos = 0;
    while (tokenPos < static_cast<int32_t>(token.length())) {
        if (peek() != token[tokenPos]) {
            ERROR(errorCode);
            return;
        }
        normalizedInput += token[tokenPos];
        next();
        tokenPos++;
    }
}

/*
   Consumes optional whitespace, possibly advancing `index` to `index'`,
   then consumes a fixed-length token (signaling an error if the token isn't a prefix of
   the string beginning at `source[index']`),
   then consumes optional whitespace again
*/
void Parser::parseTokenWithWhitespace(const std::u16string_view& token, UErrorCode& errorCode) {
    // No need for error check or bounds check before parseOptionalWhitespace
    parseOptionalWhitespace(errorCode);
    // Establish precondition
    CHECK_BOUNDS(errorCode);
    parseToken(token, errorCode);
    parseOptionalWhitespace(errorCode);
    // Guarantee postcondition
    CHECK_BOUNDS(errorCode);
}

/*
   Consumes optional whitespace, possibly advancing `index` to `index'`,
   then consumes a single character (signaling an error if it doesn't match
   `source[index']`),
   then consumes optional whitespace again
*/
void Parser::parseTokenWithWhitespace(UChar32 c, UErrorCode& errorCode) {
    // No need for error check or bounds check before parseOptionalWhitespace(errorCode)
    parseOptionalWhitespace(errorCode);
    // Establish precondition
    CHECK_BOUNDS(errorCode);
    parseToken(c, errorCode);
    parseOptionalWhitespace(errorCode);
    // Guarantee postcondition
    CHECK_BOUNDS(errorCode);
}

/*
  Consumes a non-empty sequence of `name-char`s, the first of which is
  also a `name-start`.
  that begins with a character `start` such that `isNameStart(start)`.

  Returns this sequence.

  (Matches the `name` nonterminal in the grammar.)
*/
UnicodeString Parser::parseName(UErrorCode& errorCode) {
    UnicodeString name;

    U_ASSERT(inBounds());

    if (!isNameStart(peek())) {
        ERROR(errorCode);
        return name;
    }

    while (isNameChar(peek())) {
        UChar32 c = peek();
        name += c;
        normalizedInput += c;
        next();
        if (!inBounds()) {
            ERROR(errorCode);
            break;
        }
    }
    return name;
}

/*
  Consumes a '$' followed by a `name`, returning a VariableName
  with `name` as its name

  (Matches the `variable` nonterminal in the grammar.)
*/
VariableName Parser::parseVariableName(UErrorCode& errorCode) {
    VariableName result;

    U_ASSERT(inBounds());
    // If the '$' is missing, we don't want a binding
    // for this variable to be created.
    bool valid = peek() == DOLLAR;
    parseToken(DOLLAR, errorCode);
    if (!inBounds()) {
        ERROR(errorCode);
        return result;
    }
    UnicodeString varName = parseName(errorCode);
    // Set the name to "" if the variable wasn't
    // declared correctly
    if (!valid) {
        varName.remove();
    }
    return VariableName(varName);
}

/*
  Corresponds to the `identifier` nonterminal in the grammar
*/
UnicodeString Parser::parseIdentifier(UErrorCode& errorCode) {
    U_ASSERT(inBounds());

    UnicodeString result;
    // The following is a hack to get around ambiguity in the grammar:
    // identifier -> namespace ":" name
    // vs.
    // identifier -> name
    // can't be distinguished without arbitrary lookahead.
    // Instead, we treat the production as:
    // identifier -> namespace *(":"name)
    // and then check for multiple colons.

    // Parse namespace
    result += parseName(errorCode);
    int32_t firstColon = -1;
    while (inBounds() && peek() == COLON) {
        // Parse ':' separator
        if (firstColon == -1) {
            firstColon = index;
        }
        parseToken(COLON, errorCode);
        result += COLON;
        // Check for message ending with something like "foo:"
        if (!inBounds()) {
            ERROR(errorCode);
        } else {
            // Parse name part
            result += parseName(errorCode);
        }
    }

    // If there's at least one ':', scan from the first ':'
    // to the end of the name to check for multiple ':'s
    if (firstColon != -1) {
        for (int32_t i = firstColon + 1; i < result.length(); i++) {
            if (result[i] == COLON) {
                ERROR_AT(errorCode, i);
                return {};
            }
        }
    }

    return result;
}

/*
  Consumes a reference to a function, matching the ": identifier"
  in the `function` nonterminal in the grammar.

  Returns the function name.
*/
FunctionName Parser::parseFunction(UErrorCode& errorCode) {
    U_ASSERT(inBounds());
    if (!isFunctionStart(peek())) {
        ERROR(errorCode);
        return FunctionName();
    }

    normalizedInput += peek();
    next(); // Consume the function start character
    if (!inBounds()) {
        ERROR(errorCode);
        return FunctionName();
    }
    return parseIdentifier(errorCode);
}


/*
  Precondition: peek() == BACKSLASH

  Consume an escaped character.
  Corresponds to `escaped-char` in the grammar.

  No postcondition (a message can end with an escaped char)
*/
UnicodeString Parser::parseEscapeSequence(UErrorCode& errorCode) {
    U_ASSERT(inBounds());
    U_ASSERT(peek() == BACKSLASH);
    normalizedInput += BACKSLASH;
    next(); // Skip the initial backslash
    UnicodeString str;
    if (inBounds()) {
        // Expect a '{', '|' or '}'
        switch (peek()) {
        case LEFT_CURLY_BRACE:
        case RIGHT_CURLY_BRACE:
        case PIPE:
        case BACKSLASH: {
            /* Append to the output string */
            str += peek();
            /* Update normalizedInput */
            normalizedInput += peek();
            /* Consume the character */
            next();
            return str;
        }
        default: {
            // No other characters are allowed here
            break;
        }
        }
    }
   // If control reaches here, there was an error
   ERROR(errorCode);
   return str;
}


/*
  Consume and return a quoted literal, matching the `literal` nonterminal in the grammar.
*/
Literal Parser::parseQuotedLiteral(UErrorCode& errorCode) {
    bool error = false;

    UnicodeString contents;
    if (U_SUCCESS(errorCode)) {
        // Parse the opening '|'
        parseToken(PIPE, errorCode);
        if (!inBounds()) {
            ERROR(errorCode);
            error = true;
        } else {
            // Parse the contents
            bool done = false;
            while (!done) {
                if (peek() == BACKSLASH) {
                    contents += parseEscapeSequence(errorCode);
                } else if (isQuotedChar(peek())) {
                    contents += peek();
                    // Handle cases like:
                    // |}{| -- we want to escape everywhere that
                    // can be escaped, to make round-trip checking
                    // easier -- so this case normalizes to
                    // |\}\{|
                    if (isEscapableChar(peek())) {
                        normalizedInput += BACKSLASH;
                    }
                    normalizedInput += peek();
                    next(); // Consume this character
                    maybeAdvanceLine();
                } else {
                    // Assume the sequence of literal characters ends here
                    done = true;
                }
                if (!inBounds()) {
                    ERROR(errorCode);
                    error = true;
                    break;
                }
            }
        }
    }

    if (error) {
        return {};
    }

    // Parse the closing '|'
    parseToken(PIPE, errorCode);

    return Literal(true, contents);
}

// Parse (1*DIGIT)
UnicodeString Parser::parseDigits(UErrorCode& errorCode) {
    if (U_FAILURE(errorCode)) {
        return {};
    }

    U_ASSERT(isDigit(peek()));

    UnicodeString contents;
    do {
        contents += peek();
        normalizedInput += peek();
        next();
        if (!inBounds()) {
            ERROR(errorCode);
            return {};
        }
    } while (isDigit(peek()));

    return contents;
}
/*
  Consume and return an unquoted literal, matching the `unquoted` nonterminal in the grammar.
*/
Literal Parser::parseUnquotedLiteral(UErrorCode& errorCode) {
    if (U_FAILURE(errorCode)) {
        return {};
    }

    // unquoted -> name
    if (isNameStart(peek())) {
        return Literal(false, parseName(errorCode));
    }

    // unquoted -> number
    // Parse the contents
    UnicodeString contents;

    // Parse the sign
    if (peek() == HYPHEN) {
        contents += peek();
        normalizedInput += peek();
        next();
    }
    if (!inBounds()) {
        ERROR(errorCode);
        return {};
    }

    // Parse the integer part
    if (peek() == ((UChar32)0x0030) /* 0 */) {
        contents += peek();
        normalizedInput += peek();
        next();
    } else if (isDigit(peek())) {
        contents += parseDigits(errorCode);
    } else {
        // Error -- nothing else can start a number literal
        ERROR(errorCode);
        return {};
    }

    // Parse the decimal point if present
    if (peek() == PERIOD) {
        contents += peek();
        normalizedInput += peek();
        next();
        if (!inBounds()) {
            ERROR(errorCode);
            return {};
        }
        // Parse the fraction part
        if (isDigit(peek())) {
            contents += parseDigits(errorCode);
        } else {
            // '.' not followed by digit is a parse error
            ERROR(errorCode);
            return {};
        }
    }

    if (!inBounds()) {
        ERROR(errorCode);
        return {};
    }

    // Parse the exponent part if present
    if (peek() == UPPERCASE_E || peek() == LOWERCASE_E) {
        contents += peek();
        normalizedInput += peek();
        next();
        if (!inBounds()) {
            ERROR(errorCode);
            return {};
        }
        // Parse sign if present
        if (peek() == PLUS || peek() == HYPHEN) {
            contents += peek();
            normalizedInput += peek();
            next();
            if (!inBounds()) {
                ERROR(errorCode);
                return {};
            }
        }
        // Parse exponent digits
        if (!isDigit(peek())) {
            ERROR(errorCode);
            return {};
        }
        contents += parseDigits(errorCode);
    }

    return Literal(false, contents);
}

/*
  Consume and return a literal, matching the `literal` nonterminal in the grammar.
*/
Literal Parser::parseLiteral(UErrorCode& errorCode) {
    Literal result;
    if (!inBounds()) {
        ERROR(errorCode);
    } else {
        if (peek() == PIPE) {
            result = parseQuotedLiteral(errorCode);
        } else {
            result = parseUnquotedLiteral(errorCode);
        }
        // Guarantee postcondition
        if (!inBounds()) {
            ERROR(errorCode);
        }
    }

    return result;
}

/*
  Consume a @name-value pair, matching the `attribute` nonterminal in the grammar.

  Adds the option to `options`
*/
template<class T>
void Parser::parseAttribute(AttributeAdder<T>& attrAdder, UErrorCode& errorCode) {
    U_ASSERT(inBounds());

    U_ASSERT(peek() == AT);
    // Consume the '@'
    parseToken(AT, errorCode);

    // Parse LHS
    UnicodeString lhs = parseIdentifier(errorCode);

    // Prepare to "backtrack" to resolve ambiguity
    // about whether whitespace precedes another
    // attribute, or the '=' sign
    int32_t savedIndex = index;
    parseOptionalWhitespace(errorCode);

    Operand rand;
    if (peek() == EQUALS) {
        // Parse '='
        parseTokenWithWhitespace(EQUALS, errorCode);

        UnicodeString rhsStr;
        // Parse RHS, which is either a literal or variable
        switch (peek()) {
        case DOLLAR: {
            rand = Operand(parseVariableName(errorCode));
            break;
        }
        default: {
            // Must be a literal
            rand = Operand(parseLiteral(errorCode));
            break;
        }
        }
        U_ASSERT(!rand.isNull());
    } else {
        // attribute -> "@" identifier [[s] "=" [s]]
        // Use null operand, which `rand` is already set to
        // "Backtrack" by restoring the whitespace (if there was any)
        index = savedIndex;
    }

    attrAdder.addAttribute(lhs, std::move(rand), errorCode);
}

/*
  Consume a name-value pair, matching the `option` nonterminal in the grammar.

  Adds the option to `optionList`
*/
template<class T>
void Parser::parseOption(OptionAdder<T>& addOption, UErrorCode& errorCode) {
    U_ASSERT(inBounds());

    // Parse LHS
    UnicodeString lhs = parseIdentifier(errorCode);

    // Parse '='
    parseTokenWithWhitespace(EQUALS, errorCode);

    UnicodeString rhsStr;
    Operand rand;
    // Parse RHS, which is either a literal or variable
    switch (peek()) {
    case DOLLAR: {
        rand = Operand(parseVariableName(errorCode));
        break;
    }
    default: {
        // Must be a literal
        rand = Operand(parseLiteral(errorCode));
        break;
    }
    }
    U_ASSERT(!rand.isNull());

    // Finally, add the key=value mapping
    // Use a local error code, check for duplicate option error and
    // record it as with other errors
    UErrorCode status = U_ZERO_ERROR;
    addOption.addOption(lhs, std::move(rand), status);
    if (U_FAILURE(status)) {
      U_ASSERT(status == U_MF_DUPLICATE_OPTION_NAME_ERROR);
      errors.setDuplicateOptionName(errorCode);
    }
}

/*
  Note: there are multiple overloads of parseOptions() for parsing
  options within markup, vs. within an expression, vs. parsing
  attributes. This should be refactored. TODO
 */

/*
  Consume optional whitespace followed by a sequence of options
  (possibly empty), separated by whitespace
*/
template <class T>
void Parser::parseOptions(OptionAdder<T>& addOption, UErrorCode& errorCode) {
    // Early exit if out of bounds -- no more work is possible
    CHECK_BOUNDS(errorCode);

/*
Arbitrary lookahead is required to parse option lists. To see why, consider
these rules from the grammar:

expression = "{" [s] (((literal / variable) [s annotation]) / annotation) [s] "}"
annotation = (function *(s option)) / reserved

And this example:
{:foo  }

Derivation:
expression -> "{" [s] (((literal / variable) [s annotation]) / annotation) [s] "}"
           -> "{" [s] annotation [s] "}"
           -> "{" [s] ((function *(s option)) / reserved) [s] "}"
           -> "{" [s] function *(s option) [s] "}"

In this example, knowing whether to expect a '}' or the start of another option
after the whitespace would require arbitrary lookahead -- in other words, which
rule should we apply?
    *(s option) -> s option *(s option)
  or
    *(s option) ->

The same would apply to the example {:foo k=v } (note the trailing space after "v").

This is addressed using a form of backtracking and (to make the backtracking easier
to apply) a slight refactoring to the grammar.

This code is written as if the grammar is:
  expression = "{" [s] (((literal / variable) ([s] / [s annotation])) / annotation) "}"
  annotation = (function *(s option) [s]) / (reserved [s])

Parsing the `*(s option) [s]` sequence can be done within `parseOptions()`, meaning
that `parseExpression()` can safely require a '}' after `parseOptions()` finishes.

Note that when "backtracking" really just means early exit, since only whitespace
is involved and there's no state to save.

There is a separate but similar ambiguity as to whether the space precedes
an option or an attribute.
*/

    while(true) {
        // If the next character is not whitespace, that means we've already
        // parsed the entire options list (which may have been empty) and there's
        // no trailing whitespace. In that case, exit.
        if (!isWhitespace(peek())) {
            break;
        }
        int32_t firstWhitespace = index;

        // In any case other than an empty options list, there must be at least
        // one whitespace character.
        parseRequiredWhitespace(errorCode);
        // Restore precondition
        CHECK_BOUNDS(errorCode);

        // If a name character follows, then at least one more option remains
        // in the list.
        // Otherwise, we've consumed all the options and any trailing whitespace,
        // and can exit.
        // Note that exiting is sort of like backtracking: "(s option)" doesn't apply,
        // so we back out to [s].
        if (!isNameStart(peek())) {
            // We've consumed all the options (meaning that either we consumed non-empty
            // whitespace, or consumed at least one option.)
            // Done.
            // Remove the required whitespace from normalizedInput
            normalizedInput.truncate(normalizedInput.length() - 1);
            // "Backtrack" so as to leave the optional whitespace there
            // when parsing attributes
            index = firstWhitespace;
            break;
        }
        parseOption(addOption, errorCode);
    }
}

/*
  Consume optional whitespace followed by a sequence of attributes
  (possibly empty), separated by whitespace
*/
template<class T>
void Parser::parseAttributes(AttributeAdder<T>& attrAdder, UErrorCode& errorCode) {

    // Early exit if out of bounds -- no more work is possible
    if (!inBounds()) {
        ERROR(errorCode);
        return;
    }

/*
Arbitrary lookahead is required to parse attribute lists, similarly to option lists.
(See comment in parseOptions()).
*/

    while(true) {
        // If the next character is not whitespace, that means we've already
        // parsed the entire attributes list (which may have been empty) and there's
        // no trailing whitespace. In that case, exit.
        if (!isWhitespace(peek())) {
            break;
        }

        // In any case other than an empty attributes list, there must be at least
        // one whitespace character.
        parseRequiredWhitespace(errorCode);
        // Restore precondition
        if (!inBounds()) {
            ERROR(errorCode);
            break;
        }

        // If an '@' follows, then at least one more attribute remains
        // in the list.
        // Otherwise, we've consumed all the attributes and any trailing whitespace,
        // and can exit.
        // Note that exiting is sort of like backtracking: "(s attributes)" doesn't apply,
        // so we back out to [s].
        if (peek() != AT) {
            // We've consumed all the attributes (meaning that either we consumed non-empty
            // whitespace, or consumed at least one attribute.)
            // Done.
            // Remove the whitespace from normalizedInput
            normalizedInput.truncate(normalizedInput.length() - 1);
            break;
        }
        parseAttribute(attrAdder, errorCode);
    }
}

/*
  Consume a function call, matching the `annotation`
  nonterminal in the grammar

  Returns an `Operator` representing this (a reserved is a parse error)
*/
Operator Parser::parseAnnotation(UErrorCode& status) {
    U_ASSERT(inBounds());
    Operator::Builder ratorBuilder(status);
    if (U_FAILURE(status)) {
        return {};
    }
    if (isFunctionStart(peek())) {
        // Consume the function name
        FunctionName func = parseFunction(status);
        ratorBuilder.setFunctionName(std::move(func));

        OptionAdder<Operator::Builder> addOptions(ratorBuilder);
        // Consume the options (which may be empty)
        parseOptions(addOptions, status);
    } else {
        ERROR(status);
    }
    return ratorBuilder.build(status);
}

/*
  Consume a literal or variable (depending on `isVariable`),
  followed by either required whitespace followed by an annotation,
  or optional whitespace.
*/
void Parser::parseLiteralOrVariableWithAnnotation(bool isVariable,
                                                  Expression::Builder& builder,
                                                  UErrorCode& status) {
    CHECK_ERROR(status);

    U_ASSERT(inBounds());

    Operand rand;
    if (isVariable) {
        rand = Operand(parseVariableName(status));
    } else {
        rand = Operand(parseLiteral(status));
    }

    builder.setOperand(std::move(rand));

/*
Parsing a literal or variable with an optional annotation requires arbitrary lookahead.
To see why, consider this rule from the grammar:

expression = "{" [s] (((literal / variable) [s annotation]) / annotation) [s] "}"

And this example:

{|foo|   }

Derivation:
expression -> "{" [s] (((literal / variable) [s annotation]) / annotation) [s] "}"
           -> "{" [s] ((literal / variable) [s annotation]) [s] "}"
           -> "{" [s] (literal [s annotation]) [s] "}"

When reading the ' ' after the second '|', it's ambiguous whether that's the required
space before an annotation, or the optional space before the '}'.

To make this ambiguity easier to handle, this code is based on the same grammar
refactoring for the `expression` nonterminal that `parseOptions()` relies on. See
the comment in `parseOptions()` for details.
*/

    if (isWhitespace(peek())) {
      int32_t firstWhitespace = index;

      // If the next character is whitespace, either [s annotation] or [s] applies
      // (the character is either the required space before an annotation, or optional
      // trailing space after the literal or variable). It's still ambiguous which
      // one does apply.
      parseOptionalWhitespace(status);
      // Restore precondition
      CHECK_BOUNDS(status);

      // This next check resolves the ambiguity between [s annotation] and [s]
      bool isSAnnotation = isAnnotationStart(peek());

      if (isSAnnotation) {
        normalizedInput += SPACE;
      }

      if (isSAnnotation) {
        // The previously consumed whitespace precedes an annotation
        builder.setOperator(parseAnnotation(status));
      } else {
          // Either there's a right curly brace (will be consumed by the caller),
          // or there's an error and the trailing whitespace should be
          // handled by the caller. However, this is not an error
          // here because we're just parsing `literal [s annotation]`.
          index = firstWhitespace;
      }
    } else {
      // Either there was never whitespace, or
      // the previously consumed whitespace is the optional trailing whitespace;
      // either the next character is '}' or the error will be handled by parseExpression.
      // Do nothing, since the operand was already set
    }

    // At the end of this code, the next character should either be '}',
    // whitespace followed by a '}',
    // or end-of-input
}

/*
  Consume an expression, matching the `expression` nonterminal in the grammar
*/

static void exprFallback(Expression::Builder& exprBuilder) {
    // Construct a literal consisting just of  The U+FFFD REPLACEMENT CHARACTER
    // per https://github.com/unicode-org/message-format-wg/blob/main/spec/formatting.md#fallback-resolution
    exprBuilder.setOperand(Operand(Literal(false, UnicodeString(REPLACEMENT))));
}

static Expression exprFallback(UErrorCode& status) {
    Expression result;
    if (U_SUCCESS(status)) {
        Expression::Builder exprBuilder(status);
        if (U_SUCCESS(status)) {
            // Construct a literal consisting just of  The U+FFFD REPLACEMENT CHARACTER
            // per https://github.com/unicode-org/message-format-wg/blob/main/spec/formatting.md#fallback-resolution
            exprBuilder.setOperand(Operand(Literal(false, UnicodeString(REPLACEMENT))));
            UErrorCode status = U_ZERO_ERROR;
            result = exprBuilder.build(status);
            // An operand was set, so there can't be an error
            U_ASSERT(U_SUCCESS(status));
        }
    }
    return result;
}

Expression Parser::parseExpression(UErrorCode& status) {
    if (U_FAILURE(status)) {
        return {};
    }

    // Early return if out of input -- no more work is possible
    U_ASSERT(inBounds());

    // Parse opening brace
    parseToken(LEFT_CURLY_BRACE, status);
    // Optional whitespace after opening brace
    parseOptionalWhitespace(status);

    Expression::Builder exprBuilder(status);
    // Restore precondition
    if (!inBounds()) {
        exprFallback(exprBuilder);
    } else {
        // literal '|', variable '$' or annotation
        switch (peek()) {
        case PIPE: {
            // Quoted literal
            parseLiteralOrVariableWithAnnotation(false, exprBuilder, status);
            break;
        }
        case DOLLAR: {
            // Variable
            parseLiteralOrVariableWithAnnotation(true, exprBuilder, status);
            break;
        }
        default: {
            if (isAnnotationStart(peek())) {
                Operator rator = parseAnnotation(status);
                exprBuilder.setOperator(std::move(rator));
            } else if (isUnquotedStart(peek())) {
                // Unquoted literal
                parseLiteralOrVariableWithAnnotation(false, exprBuilder, status);
            } else {
                // Not a literal, variable or annotation -- error out
                ERROR(status);
                exprFallback(exprBuilder);
                break;
            }
            break;
        }
        }
    }

    // Parse attributes
    AttributeAdder<Expression::Builder> attrAdder(exprBuilder);
    parseAttributes(attrAdder, status);

    // Parse optional space
    // (the last [s] in e.g. "{" [s] literal [s annotation] *(s attribute) [s] "}")
    parseOptionalWhitespace(status);

    // Either an operand or operator (or both) must have been set already,
    // so there can't be an error
    UErrorCode localStatus = U_ZERO_ERROR;
    Expression result = exprBuilder.build(localStatus);
    U_ASSERT(U_SUCCESS(localStatus));

    // Check for end-of-input and missing '}'
    if (!inBounds()) {
        ERROR(status);
    } else {
        // Otherwise, it's safe to check for the '}'
        parseToken(RIGHT_CURLY_BRACE, status);
    }
    return result;
}

/*
  Parse a .local declaration, matching the `local-declaration`
  production in the grammar
*/
void Parser::parseLocalDeclaration(UErrorCode& status) {
    // End-of-input here would be an error; even empty
    // declarations must be followed by a body
    CHECK_BOUNDS(status);

    parseToken(ID_LOCAL, status);
    parseRequiredWhitespace(status);

    // Restore precondition
    CHECK_BOUNDS(status);
    VariableName lhs = parseVariableName(status);
    parseTokenWithWhitespace(EQUALS, status);
    // Restore precondition before calling parseExpression()
    CHECK_BOUNDS(status);

    Expression rhs = parseExpression(status);

    // Add binding from lhs to rhs, unless there was an error
    // (This ensures that if there was a correct lhs but a
    // parse error in rhs, the fallback for uses of the
    // lhs will be its own name rather than the rhs)
    /* This affects the behavior of this test case, which the spec
       is ambiguous about:

       .local $bar {|foo|} {{{$bar}}}

       Should `$bar` still be bound to a value although
       its declaration is syntactically incorrect (missing the '=')?
       This code says no, but it needs to change if
       https://github.com/unicode-org/message-format-wg/issues/703
       is resolved differently.
    */
    CHECK_ERROR(status);
    if (!errors.hasSyntaxError()) {
        dataModel.addBinding(Binding(std::move(lhs), std::move(rhs)), status);
        // Check if status is U_DUPLICATE_DECLARATION_ERROR
        // and add that as an internal error if so
        if (status == U_MF_DUPLICATE_DECLARATION_ERROR) {
            status = U_ZERO_ERROR;
            errors.addError(StaticErrorType::DuplicateDeclarationError, status);
        }
    }
}

/*
  Parse an .input declaration, matching the `local-declaration`
  production in the grammar
*/
void Parser::parseInputDeclaration(UErrorCode& status) {
    // End-of-input here would be an error; even empty
    // declarations must be followed by a body
    CHECK_BOUNDS(status);

    parseToken(ID_INPUT, status);
    parseOptionalWhitespace(status);

    // Restore precondition before calling parseExpression()
    CHECK_BOUNDS(status);

    // Save the index for error diagnostics
    int32_t exprIndex = index;
    Expression rhs = parseExpression(status);

    // Here we have to check that the rhs is a variable-expression
    if (!rhs.getOperand().isVariable()) {
        // This case is a syntax error; report it at the beginning
        // of the expression
        ERROR_AT(status, exprIndex);
        return;
    }

    VariableName lhs = rhs.getOperand().asVariable();

    // Add binding from lhs to rhs
    // This just adds a new local variable that shadows the message
    // argument referred to, which is harmless.
    // When evaluating the RHS, the new local is not in scope
    // and the message argument will be correctly referred to.
    CHECK_ERROR(status);
    if (!errors.hasSyntaxError()) {
        dataModel.addBinding(Binding::input(std::move(lhs), std::move(rhs), status), status);
        // Check if status is U_MF_DUPLICATE_DECLARATION_ERROR
        // and add that as an internal error if so
        if (status == U_MF_DUPLICATE_DECLARATION_ERROR) {
            status = U_ZERO_ERROR;
            errors.addError(StaticErrorType::DuplicateDeclarationError, status);
        }
    }
}

/*
  Consume a possibly-empty sequence of declarations separated by whitespace;
  each declaration matches the `declaration` nonterminal in the grammar

  Builds up an environment representing those declarations
*/
void Parser::parseDeclarations(UErrorCode& status) {
    // End-of-input here would be an error; even empty
    // declarations must be followed by a body
    CHECK_BOUNDS(status);

    while (peek() == PERIOD) {
        CHECK_BOUNDS_1(status);
        if (peek(1) == ID_LOCAL[1]) {
            parseLocalDeclaration(status);
        } else if (peek(1) == ID_INPUT[1]) {
            parseInputDeclaration(status);
        } else {
            // Done parsing declarations
            break;
        }

        // Avoid looping infinitely
        CHECK_ERROR(status);

        parseOptionalWhitespace(status);
        // Restore precondition
        CHECK_BOUNDS(status);
    }
}

/*
  Consume a text character
  matching the `text-char` nonterminal in the grammar

  No postcondition (a message can end with a text-char)
*/
UnicodeString Parser::parseTextChar(UErrorCode& status) {
    UnicodeString str;
    if (!inBounds() || !(isTextChar(peek()))) {
        // Error -- text-char is expected here
        ERROR(status);
    } else {
        // See comment in parseQuotedLiteral()
        if (isEscapableChar(peek())) {
            normalizedInput += BACKSLASH;
        }
        normalizedInput += peek();
        str += peek();
        next();
        maybeAdvanceLine();
    }
    return str;
}

/*
  Consume an `nmtoken`, `literal`, or the string "*", matching
  the `key` nonterminal in the grammar
*/
Key Parser::parseKey(UErrorCode& status) {
    U_ASSERT(inBounds());

    Key k; // wildcard by default
    // Literal | '*'
    switch (peek()) {
    case ASTERISK: {
        next();
        normalizedInput += ASTERISK;
        // Guarantee postcondition
        if (!inBounds()) {
            ERROR(status);
            return k;
        }
        break;
    }
    default: {
        // Literal
        k = Key(parseLiteral(status));
        break;
    }
    }
    return k;
}

/*
  Consume a non-empty sequence of `key`s separated by whitespace

  Takes ownership of `keys`
*/
SelectorKeys Parser::parseNonEmptyKeys(UErrorCode& status) {
    SelectorKeys result;

    if (U_FAILURE(status)) {
        return result;
    }

    U_ASSERT(inBounds());

/*
Arbitrary lookahead is required to parse key lists. To see why, consider
this rule from the grammar:

variant = key *(s key) [s] quoted-pattern

And this example:
when k1 k2   {a}

Derivation:
   variant -> key *(s key) [s] quoted-pattern
           -> key s key *(s key) quoted-pattern

After matching ' ' to `s` and 'k2' to `key`, it would require arbitrary lookahead
to know whether to expect the start of a pattern or the start of another key.
In other words: is the second whitespace sequence the required space in *(s key),
or the optional space in [s] quoted-pattern?

This is addressed using "backtracking" (similarly to `parseOptions()`).
*/

    SelectorKeys::Builder keysBuilder(status);
    if (U_FAILURE(status)) {
        return result;
    }

    // Since the first key is required, it's simplest to parse it separately.
    keysBuilder.add(parseKey(status), status);

    // Restore precondition
    if (!inBounds()) {
        ERROR(status);
        return result;
    }

    // We've seen at least one whitespace-key pair, so now we can parse
    // *(s key) [s]
    while (peek() != LEFT_CURLY_BRACE || isWhitespace(peek())) { // Try to recover from errors
        bool wasWhitespace = isWhitespace(peek());
        parseRequiredWhitespace(status);
        if (!wasWhitespace) {
            // Avoid infinite loop when parsing something like:
            // when * @{!...
            next();
        }

        // Restore precondition
        if (!inBounds()) {
            ERROR(status);
            return result;
        }

        // At this point, it's ambiguous whether we are inside (s key) or [s].
        // This check resolves that ambiguity.
        if (peek() == LEFT_CURLY_BRACE) {
            // A pattern follows, so what we just parsed was the optional
            // trailing whitespace. All the keys have been parsed.

            // Unpush the whitespace from `normalizedInput`
            normalizedInput.truncate(normalizedInput.length() - 1);
            break;
        }
        keysBuilder.add(parseKey(status), status);
    }

    return keysBuilder.build(status);
}

Pattern Parser::parseQuotedPattern(UErrorCode& status) {
    U_ASSERT(inBounds());

    parseToken(LEFT_CURLY_BRACE, status);
    parseToken(LEFT_CURLY_BRACE, status);
    Pattern p = parseSimpleMessage(status);
    parseToken(RIGHT_CURLY_BRACE, status);
    parseToken(RIGHT_CURLY_BRACE, status);
    return p;
}

/*
  Consume a `placeholder`, matching the nonterminal in the grammar
  No postcondition (a markup can end a message)
*/
Markup Parser::parseMarkup(UErrorCode& status) {
    U_ASSERT(inBounds(1));

    U_ASSERT(peek() == LEFT_CURLY_BRACE);

    Markup::Builder builder(status);
    if (U_FAILURE(status)) {
        return {};
    }

    // Consume the '{'
    next();
    normalizedInput += LEFT_CURLY_BRACE;
    parseOptionalWhitespace(status);
    bool closing = false;
    switch (peek()) {
    case NUMBER_SIGN: {
        // Open or standalone; consume the '#'
        normalizedInput += peek();
        next();
        break;
    }
    case SLASH: {
        // Closing
        normalizedInput += peek();
        closing = true;
        next();
        break;
    }
    default: {
        ERROR(status);
        return {};
    }
    }

    // Parse the markup identifier
    builder.setName(parseIdentifier(status));

    // Parse the options, which must begin with a ' '
    // if present
    if (inBounds() && isWhitespace(peek())) {
        OptionAdder<Markup::Builder> optionAdder(builder);
        parseOptions(optionAdder, status);
    }

    // Parse the attributes, which also must begin
    // with a ' '
    if (inBounds() && isWhitespace(peek())) {
        AttributeAdder<Markup::Builder> attrAdder(builder);
        parseAttributes(attrAdder, status);
    }

    parseOptionalWhitespace(status);

    bool standalone = false;
    // Check if this is a standalone or not
    if (!closing) {
        if (inBounds() && peek() == SLASH) {
            standalone = true;
            normalizedInput += SLASH;
            next();
        }
    }

    parseToken(RIGHT_CURLY_BRACE, status);

    if (standalone) {
        builder.setStandalone();
    } else if (closing) {
        builder.setClose();
    } else {
        builder.setOpen();
    }

    return builder.build(status);
}

/*
  Consume a `placeholder`, matching the nonterminal in the grammar
  No postcondition (a placeholder can end a message)
*/
std::variant<Expression, Markup> Parser::parsePlaceholder(UErrorCode& status) {
    U_ASSERT(peek() == LEFT_CURLY_BRACE);

    if (!inBounds()) {
        ERROR(status);
        return exprFallback(status);
    }

    // Need to look ahead arbitrarily since whitespace
    // can appear before the '{' and '#'
    // in markup
    int32_t tempIndex = 1;
    bool isMarkup = false;
    while (inBounds(1)) {
        UChar32 c = peek(tempIndex);
        if (c == NUMBER_SIGN || c == SLASH) {
            isMarkup = true;
            break;
        }
        if (!isWhitespace(c)){
            break;
        }
        tempIndex++;
    }

    if (isMarkup) {
        return parseMarkup(status);
    }
    return parseExpression(status);
}

/*
  Consume a `simple-message`, matching the nonterminal in the grammar
  Postcondition: `index == len()` or U_FAILURE(status);
  for a syntactically correct message, this will consume the entire input
*/
Pattern Parser::parseSimpleMessage(UErrorCode& status) {
    Pattern::Builder result(status);

    if (U_SUCCESS(status)) {
        Expression expression;
        while (inBounds()) {
            switch (peek()) {
            case LEFT_CURLY_BRACE: {
                // Must be placeholder
                std::variant<Expression, Markup> piece = parsePlaceholder(status);
                if (std::holds_alternative<Expression>(piece)) {
                    Expression expr = *std::get_if<Expression>(&piece);
                    result.add(std::move(expr), status);
                } else {
                    Markup markup = *std::get_if<Markup>(&piece);
                    result.add(std::move(markup), status);
                }
                break;
            }
            case BACKSLASH: {
                // Must be escaped-char
                result.add(parseEscapeSequence(status), status);
                break;
            }
            case RIGHT_CURLY_BRACE: {
                // Distinguish unescaped '}' from end of quoted pattern
                break;
            }
            default: {
                // Must be text-char
                result.add(parseTextChar(status), status);
                break;
            }
            }
            if (peek() == RIGHT_CURLY_BRACE) {
                // End of quoted pattern
                break;
            }
            // Don't loop infinitely
            if (errors.hasSyntaxError()) {
                break;
            }
        }
    }
    return result.build(status);
}


/*
  Consume a `selectors` (matching the nonterminal in the grammar),
  followed by a non-empty sequence of `variant`s (matching the nonterminal
  in the grammar) preceded by whitespace
  No postcondition (on return, `index` might equal `len()` with no syntax error
  because a message can end with a variant)
*/
void Parser::parseSelectors(UErrorCode& status) {
    CHECK_ERROR(status);

    U_ASSERT(inBounds());

    parseToken(ID_MATCH, status);

    bool empty = true;
    // Parse selectors
    // "Backtracking" is required here. It's not clear if whitespace is
    // (`[s]` selector) or (`[s]` variant)
    while (isWhitespace(peek()) || peek() == LEFT_CURLY_BRACE) {
        parseOptionalWhitespace(status);
        // Restore precondition
        CHECK_BOUNDS(status);
        if (peek() != LEFT_CURLY_BRACE) {
            // This is not necessarily an error, but rather,
            // means the whitespace we parsed was the optional
            // whitespace preceding the first variant, not the
            // optional whitespace preceding a subsequent expression.
            break;
        }
        Expression expression;
        expression = parseExpression(status);
        empty = false;

        dataModel.addSelector(std::move(expression), status);
        CHECK_ERROR(status);
    }

    // At least one selector is required
    if (empty) {
        ERROR(status);
        return;
    }

    #define CHECK_END_OF_INPUT                     \
        if (!inBounds()) {                         \
            break;                                 \
        }                                          \

    // Parse variants
    while (isWhitespace(peek()) || isKeyStart(peek())) {
        // Trailing whitespace is allowed
        parseOptionalWhitespace(status);
        if (!inBounds()) {
            return;
        }

        // At least one key is required
        SelectorKeys keyList(parseNonEmptyKeys(status));

        CHECK_ERROR(status);

        // parseNonEmptyKeys() consumes any trailing whitespace,
        // so the pattern can be consumed next.

        // Restore precondition before calling parsePattern()
        // (which must return a non-null value)
        CHECK_BOUNDS(status);
        Pattern rhs = parseQuotedPattern(status);

        dataModel.addVariant(std::move(keyList), std::move(rhs), status);

        // Restore the precondition, *without* erroring out if we've
        // reached the end of input. That's because it's valid for the
        // message to end with a variant that has no trailing whitespace.
        // Why do we need to check this condition twice inside the loop?
        // Because if we don't check it here, the `isWhitespace()` call in
        // the loop head will read off the end of the input string.
        CHECK_END_OF_INPUT
    }
}

/*
  Consume a `body` (matching the nonterminal in the grammar),
  No postcondition (on return, `index` might equal `len()` with no syntax error,
  because a message can end with a body (trailing whitespace is optional)
*/

void Parser::errorPattern(UErrorCode& status) {
    errors.addSyntaxError(status);
    // Set to empty pattern
    Pattern::Builder result = Pattern::Builder(status);
    CHECK_ERROR(status);

    // If still in bounds, then add the remaining input as a single text part
    // to the pattern
    /*
      TODO: this behavior isn't documented in the spec, but it comes from
      https://github.com/messageformat/messageformat/blob/e0087bff312d759b67a9129eac135d318a1f0ce7/packages/mf2-messageformat/src/__fixtures/test-messages.json#L236
      and a pending pull request https://github.com/unicode-org/message-format-wg/pull/462 will clarify
      whether this is the intent behind the spec
     */
    UnicodeString partStr(LEFT_CURLY_BRACE);
    while (inBounds()) {
        partStr += peek();
        next();
    }
    // Add curly braces around the entire output (same comment as above)
    partStr += RIGHT_CURLY_BRACE;
    result.add(std::move(partStr), status);
    dataModel.setPattern(result.build(status));
}

void Parser::parseBody(UErrorCode& status) {
    CHECK_ERROR(status);

    // Out-of-input is a syntax warning
    if (!inBounds()) {
        errorPattern(status);
        return;
    }

    // Body must be either a pattern or selectors
    switch (peek()) {
    case LEFT_CURLY_BRACE: {
        // Pattern
        dataModel.setPattern(parseQuotedPattern(status));
        break;
    }
    case ID_MATCH[0]: {
        // Selectors
        parseSelectors(status);
        return;
    }
    default: {
        ERROR(status);
        errorPattern(status);
        return;
    }
    }
}

// -------------------------------------
// Parses the source pattern.

void Parser::parse(UParseError &parseErrorResult, UErrorCode& status) {
    CHECK_ERROR(status);

    bool complex = false;
    // First, "look ahead" to determine if this is a simple or complex
    // message. To do that, check the first non-whitespace character.
    while (inBounds(index) && isWhitespace(peek())) {
        next();
    }

    // Message can be empty, so we need to only look ahead
    // if we know it's non-empty
    if (inBounds()) {
        if (peek() == PERIOD
            || (inBounds(1)
                && peek() == LEFT_CURLY_BRACE
                && peek(1) == LEFT_CURLY_BRACE)) {
            complex = true;
        }
    }
    // Reset index
    index = 0;

    // Message can be empty, so we need to only look ahead
    // if we know it's non-empty
    if (complex) {
        parseOptionalWhitespace(status);
        parseDeclarations(status);
        parseBody(status);
        parseOptionalWhitespace(status);
    } else {
        // Simple message
        // For normalization, quote the pattern
        normalizedInput += LEFT_CURLY_BRACE;
        normalizedInput += LEFT_CURLY_BRACE;
        dataModel.setPattern(parseSimpleMessage(status));
        normalizedInput += RIGHT_CURLY_BRACE;
        normalizedInput += RIGHT_CURLY_BRACE;
    }

    CHECK_ERROR(status);

    // There are no errors; finally, check that the entire input was consumed
    if (!allConsumed()) {
        ERROR(status);
    }

    // Finally, copy the relevant fields of the internal `MessageParseError`
    // into the `UParseError` argument
    translateParseError(parseError, parseErrorResult);
}

Parser::~Parser() {}

} // namespace message2
U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_MF2 */

#endif /* #if !UCONFIG_NO_FORMATTING */


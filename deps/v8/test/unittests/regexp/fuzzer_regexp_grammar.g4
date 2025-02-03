// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

grammar fuzzer_regexp_grammar;

////////////////////////////////////////////////////////////////////////////////
// Non RegExp specific rules
////////////////////////////////////////////////////////////////////////////////

SourceCharacter
    : .
    ;

IdentifierStartChar
    // \p{ID_Start} is not supported by RE2. We overapproximate.
    : [\p{Lu}\p{Ll}\p{Lt}\p{Lm}\p{Lo}\p{Nl}]
    | '$'
    | '_'
    ;

IdentifierPartChar
    // \p{ID_Continue} is not supported by RE2. We overapproximate.
    : [\p{Lu}\p{Ll}\p{Lt}\p{Lm}\p{Lo}\p{Nl}\p{Mn}\p{Mc}\p{Nd}\p{Pc}]
    | '$'
    | [\x{200C}\x{200D}]
    ;

AsciiLetter
    : [a-zA-Z]
    ;

NumericLiteralSeparator
    : '_'
    ;

DecimalDigits
    : DecimalDigit+ (NumericLiteralSeparator ? DecimalDigit+)*
    ;

DecimalDigit
    : [0-9]
    ;

NonZeroDigit
    : [1-9]
    ;

HexDigit
    : [0-9a-fA-F]
    ;

HexDigits
    : (HexDigit (NumericLiteralSeparator HexDigit) ? ) +
    ;

CodePoint
    : HexDigits
    ;

HexEscapeSequence
    : 'u' Hex4Digits
    | 'u{' CodePoint '}'
    ;

Hex4Digits
    : HexDigit HexDigit HexDigit HexDigit
    ;

////////////////////////////////////////////////////////////////////////////////
// Regular Expression Grammar
////////////////////////////////////////////////////////////////////////////////

// Pattern[UnicodeMode, UnicodeSetsMode, N] ::
//    Disjunction[?UnicodeMode, ?UnicodeSetsMode, ?N]

pattern
    : Disjunction
    ;

// Disjunction[UnicodeMode, UnicodeSetsMode, N] ::
//    Alternative[?UnicodeMode, ?UnicodeSetsMode, ?N]
//    Alternative[?UnicodeMode, ?UnicodeSetsMode, ?N] | Disjunction[?UnicodeMode, ?UnicodeSetsMode, ?N]

Disjunction
    : Alternative+
    ;

// Alternative[UnicodeMode, UnicodeSetsMode, N] ::
//    [empty]
//    Alternative[?UnicodeMode, ?UnicodeSetsMode, ?N] Term[?UnicodeMode, ?UnicodeSetsMode, ?N]

Alternative
    : Term+
    ;

// Term[UnicodeMode, UnicodeSetsMode, N] ::
//    Assertion[?UnicodeMode, ?UnicodeSetsMode, ?N]
//    Atom[?UnicodeMode, ?UnicodeSetsMode, ?N]
//    Atom[?UnicodeMode, ?UnicodeSetsMode, ?N] Quantifier

Term
    : Assertion
    | Atom Quantifier ?
    ;

// Assertion[UnicodeMode, UnicodeSetsMode, N] ::
//    ^
//    $
//    \b
//    \B
//    (?= Disjunction[?UnicodeMode, ?UnicodeSetsMode, ?N] )
//    (?! Disjunction[?UnicodeMode, ?UnicodeSetsMode, ?N] )
//    (?<= Disjunction[?UnicodeMode, ?UnicodeSetsMode, ?N] )
//    (?<! Disjunction[?UnicodeMode, ?UnicodeSetsMode, ?N] )

Assertion
    : [^$]
    | [\\] 'b'
    | [\\] 'B'
    | '(?=' Disjunction ')'
    | '(?!' Disjunction ')'
    | '(?<=' Disjunction ')'
    | '(?<!' Disjunction ')'
    ;

// Quantifier ::
//    QuantifierPrefix
//    QuantifierPrefix ?

Quantifier
    : QuantifierPrefix '?' ?
    ;

// QuantifierPrefix ::
//    *
//    +
//    ?
//    { DecimalDigits[~Sep] }
//    { DecimalDigits[~Sep] ,}
//    { DecimalDigits[~Sep] , DecimalDigits[~Sep] }

QuantifierPrefix
    : [*+?]
    | '{' DecimalDigits ',' ? DecimalDigits ? '}'
    ;

// Atom[UnicodeMode, UnicodeSetsMode, N] ::
//    PatternCharacter
//    .
//    \ AtomEscape[?UnicodeMode, ?N]
//    CharacterClass[?UnicodeMode, ?UnicodeSetsMode]
//    ( GroupSpecifier[?UnicodeMode]opt Disjunction[?UnicodeMode, ?UnicodeSetsMode, ?N] )
//    (?: Disjunction[?UnicodeMode, ?UnicodeSetsMode, ?N] )

Atom
    : PatternCharacter
    | '.'
    | [\\] AtomEscape
    | CharacterClass
    | '(' GroupSpecifier ? Disjunction ')'
    | '(?:' Disjunction ')'
    ;

// SyntaxCharacter :: one of
//    ^ $ \ . * + ? ( ) [ ] { } |

SyntaxCharacter
    : [^$\\.*+?()[\]{}|]
    ;

// PatternCharacter ::
//    SourceCharacter but not SyntaxCharacter

PatternCharacter
    : SourceCharacter
    ;

// AtomEscape[UnicodeMode, N] ::
//    DecimalEscape
//    CharacterClassEscape[?UnicodeMode]
//    CharacterEscape[?UnicodeMode]
//    [+N] k GroupName[?UnicodeMode]

AtomEscape
    : DecimalEscape
    | CharacterClassEscape
    | CharacterEscape
    | 'k' GroupName
    ;

// CharacterEscape[UnicodeMode] ::
//    ControlEscape
//    c AsciiLetter
//    0 [lookahead ∉ DecimalDigit]
//    HexEscapeSequence
//    RegExpUnicodeEscapeSequence[?UnicodeMode]
//    IdentityEscape[?UnicodeMode]

CharacterEscape
    : ControlEscape
    | 'c' AsciiLetter
    | '0'
    | HexEscapeSequence
    | RegExpUnicodeEscapeSequence
    | IdentityEscape
    ;

// ControlEscape :: one of
//    f n r t v

ControlEscape
    : [fnrtv]
    ;

// GroupSpecifier[UnicodeMode] ::
//    ? GroupName[?UnicodeMode]

GroupSpecifier
    : '?' GroupName
    ;

// GroupName[UnicodeMode] ::
//    < RegExpIdentifierName[?UnicodeMode] >

GroupName
    : '<' RegExpIdentifierName '>'
    ;

// RegExpIdentifierName[UnicodeMode] ::
//    RegExpIdentifierStart[?UnicodeMode]
//    RegExpIdentifierName[?UnicodeMode] RegExpIdentifierPart[?UnicodeMode]

RegExpIdentifierName
    : RegExpIdentifierStart RegExpIdentifierPart +
    ;

// RegExpIdentifierStart[UnicodeMode] ::
//    IdentifierStartChar
//    \ RegExpUnicodeEscapeSequence[+UnicodeMode]
//    [~UnicodeMode] UnicodeLeadSurrogate UnicodeTrailSurrogate

RegExpIdentifierStart
    : IdentifierStartChar
    | [\\] RegExpUnicodeEscapeSequence
    | UnicodeLeadSurrogate UnicodeTrailSurrogate
    ;

// RegExpIdentifierPart[UnicodeMode] ::
//     IdentifierPartChar
//    \ RegExpUnicodeEscapeSequence[+UnicodeMode]
//    [~UnicodeMode] UnicodeLeadSurrogate UnicodeTrailSurrogate

RegExpIdentifierPart
    : IdentifierPartChar
    | [\\] RegExpUnicodeEscapeSequence
    | UnicodeLeadSurrogate UnicodeTrailSurrogate
    ;

// RegExpUnicodeEscapeSequence[UnicodeMode] ::
//    [+UnicodeMode] u HexLeadSurrogate \u HexTrailSurrogate
//    [+UnicodeMode] u HexLeadSurrogate
//    [+UnicodeMode] u HexTrailSurrogate
//    [+UnicodeMode] u HexNonSurrogate
//    [~UnicodeMode] u Hex4Digits
//    [+UnicodeMode] u{ CodePoint }

RegExpUnicodeEscapeSequence
    : 'u' HexLeadSurrogate '\\u' HexTrailSurrogate
    | 'u' HexLeadSurrogate
    | 'u' HexTrailSurrogate
    | 'u' HexNonSurrogate
    | 'u' Hex4Digits
    | 'u{' CodePoint '}'
    ;

// UnicodeLeadSurrogate ::
//    any Unicode code point in the inclusive interval from U+D800 to U+DBFF

UnicodeLeadSurrogate
    : [\x{D800}-\x{DBFF}]
    ;

// UnicodeTrailSurrogate ::
//    any Unicode code point in the inclusive interval from U+DC00 to U+DFFF

UnicodeTrailSurrogate
    : [\x{DC00}-\x{DFFF}]
    ;

// HexLeadSurrogate ::
//    Hex4Digits but only if the MV of Hex4Digits is in the inclusive interval from 0xD800 to 0xDBFF

HexLeadSurrogate
    : Hex4Digits
    ;

// HexTrailSurrogate ::
//    Hex4Digits but only if the MV of Hex4Digits is in the inclusive interval from 0xDC00 to 0xDFFF

HexTrailSurrogate
    : Hex4Digits
    ;

// HexNonSurrogate ::
//    Hex4Digits but only if the MV of Hex4Digits is not in the inclusive interval from 0xD800 to 0xDFFF

HexNonSurrogate
    : Hex4Digits
    ;

// IdentityEscape[UnicodeMode] ::
//    [+UnicodeMode] SyntaxCharacter
//    [+UnicodeMode] /
//    [~UnicodeMode] SourceCharacter but not UnicodeIDContinue

IdentityEscape
    : SyntaxCharacter
    | '/'
    | SourceCharacter
    ;

// DecimalEscape ::
//    NonZeroDigit DecimalDigits[~Sep]opt [lookahead ∉ DecimalDigit]

DecimalEscape
    : NonZeroDigit DecimalDigits ?
    ;

// CharacterClassEscape[UnicodeMode] ::
//    d
//    D
//    s
//    S
//    w
//    W
//    [+UnicodeMode] p{ UnicodePropertyValueExpression }
//    [+UnicodeMode] P{ UnicodePropertyValueExpression }

CharacterClassEscape
    : [dDsSwW]
    | [pP] '{' UnicodePropertyValueExpression '}'
    ;

// UnicodePropertyValueExpression ::
//    UnicodePropertyName = UnicodePropertyValue
//    LoneUnicodePropertyNameOrValue

UnicodePropertyValueExpression
    : UnicodePropertyName '=' UnicodePropertyValue
    | LoneUnicodePropertyNameOrValue
    ;

// UnicodePropertyName ::
//    UnicodePropertyNameCharacters

UnicodePropertyName
    : UnicodePropertyNameCharacters
    ;

// UnicodePropertyNameCharacters ::
//    UnicodePropertyNameCharacter UnicodePropertyNameCharactersopt

UnicodePropertyNameCharacters
    : UnicodePropertyNameCharacter+
    ;

// UnicodePropertyValue ::
//    UnicodePropertyValueCharacters

UnicodePropertyValue
    : UnicodePropertyValueCharacters
    ;

// LoneUnicodePropertyNameOrValue ::
//    UnicodePropertyValueCharacters

LoneUnicodePropertyNameOrValue
    : UnicodePropertyValueCharacters
    ;

// UnicodePropertyValueCharacters ::
//    UnicodePropertyValueCharacter UnicodePropertyValueCharactersopt

UnicodePropertyValueCharacters
    : UnicodePropertyValueCharacter+
    ;

// UnicodePropertyValueCharacter ::
//    UnicodePropertyNameCharacter
//    DecimalDigit

UnicodePropertyValueCharacter
    : UnicodePropertyNameCharacter
    | DecimalDigit
    ;

// UnicodePropertyNameCharacter ::
//    AsciiLetter
//    _

UnicodePropertyNameCharacter
    : AsciiLetter
    | '-'
    ;

// CharacterClass[UnicodeMode, UnicodeSetsMode] ::
//    [ [lookahead ≠ ^] ClassContents[?UnicodeMode, ?UnicodeSetsMode] ]
//    [^ ClassContents[?UnicodeMode, ?UnicodeSetsMode] ]

CharacterClass
    : '[' '^' ? ClassContents ']'
    ;

// ClassContents[UnicodeMode, UnicodeSetsMode] ::
//    [empty]
//    [~UnicodeSetsMode] NonemptyClassRanges[?UnicodeMode]
//    [+UnicodeSetsMode] ClassSetExpression

ClassContents
    : NonemptyClassRanges ?
    | ClassSetExpression ?
    ;

// NonemptyClassRanges[UnicodeMode] ::
//    ClassAtom[?UnicodeMode]
//    ClassAtom[?UnicodeMode] NonemptyClassRangesNoDash[?UnicodeMode]
//    ClassAtom[?UnicodeMode] - ClassAtom[?UnicodeMode] ClassContents[?UnicodeMode, ~UnicodeSetsMode]

NonemptyClassRanges
    : ClassAtom
    | ClassAtom NonemptyClassRangesNoDash
    | ClassAtom '-' ClassAtom ClassContents
    ;

// NonemptyClassRangesNoDash[UnicodeMode] ::
//    ClassAtom[?UnicodeMode]
//    ClassAtomNoDash[?UnicodeMode] NonemptyClassRangesNoDash[?UnicodeMode]
//    ClassAtomNoDash[?UnicodeMode] - ClassAtom[?UnicodeMode] ClassContents[?UnicodeMode, ~UnicodeSetsMode]

NonemptyClassRangesNoDash
    : ClassAtom
    | ClassAtomNoDash NonemptyClassRangesNoDash
    | ClassAtomNoDash '-' ClassAtom ClassContents
    ;

// ClassAtom[UnicodeMode] ::
//    -
//    ClassAtomNoDash[?UnicodeMode]

ClassAtom
    : '-'
    | ClassAtomNoDash
    ;

// ClassAtomNoDash[UnicodeMode] ::
//    SourceCharacter but not one of \ or ] or -
//    \ ClassEscape[?UnicodeMode]

ClassAtomNoDash
    // We can't model 'none of...' easily. So just overapproximate.
    : SourceCharacter
    | [\\] ClassEscape
    ;

// ClassEscape[UnicodeMode] ::
//    b
//    [+UnicodeMode] -
//    CharacterClassEscape[?UnicodeMode]
//    CharacterEscape[?UnicodeMode]

ClassEscape
    : 'b'
    | '-'
    | CharacterClassEscape
    | CharacterEscape
    ;

// ClassSetExpression ::
//    ClassUnion
//    ClassIntersection
//    ClassSubtraction

ClassSetExpression
    : ClassUnion
    | ClassIntersection
    | ClassSubtraction
    ;

// ClassUnion ::
//    ClassSetRange ClassUnionopt
//    ClassSetOperand ClassUnionopt

ClassUnion
    : ClassSetRange ClassUnion ?
    | ClassSetOperand ClassUnion ?
    ;

// ClassIntersection ::
//    ClassSetOperand && [lookahead ≠ &] ClassSetOperand
//    ClassIntersection && [lookahead ≠ &] ClassSetOperand

ClassIntersection
    : ClassSetOperand ('&&' ClassSetOperand) +
    ;

// ClassSubtraction ::
//    ClassSetOperand -- ClassSetOperand
//    ClassSubtraction -- ClassSetOperand

ClassSubtraction
    : ClassSetOperand ('--' ClassSetOperand) +
    ;

// ClassSetRange ::
//    ClassSetCharacter - ClassSetCharacter

ClassSetRange
    : ClassSetCharacter '-' ClassSetCharacter
    ;

// ClassSetOperand ::
//    NestedClass
//    ClassStringDisjunction
//    ClassSetCharacter

ClassSetOperand
    : NestedClass
    | ClassStringDisjunction
    | ClassSetCharacter
    ;

// NestedClass ::
//    [ [lookahead ≠ ^] ClassContents[+UnicodeMode, +UnicodeSetsMode] ]
//    [^ ClassContents[+UnicodeMode, +UnicodeSetsMode] ]
//    \ CharacterClassEscape[+UnicodeMode]

NestedClass
    : '[' '^' ? ClassContents ']'
    | [\\] CharacterClassEscape
    ;

// ClassStringDisjunction ::
//    \q{ ClassStringDisjunctionContents }

ClassStringDisjunction
    : [\\] 'q{' ClassStringDisjunctionContents '}'
    ;

// ClassStringDisjunctionContents ::
//    ClassString
//    ClassString | ClassStringDisjunctionContents

ClassStringDisjunctionContents
    : ClassString+
    ;

// ClassString ::
//    [empty]
//    NonEmptyClassString

ClassString
    : NonEmptyClassString
//    |
    ;

// NonEmptyClassString ::
//    ClassSetCharacter NonEmptyClassStringopt

NonEmptyClassString
    : ClassSetCharacter NonEmptyClassString ?
    ;

// ClassSetCharacter ::
//    [lookahead ∉ ClassSetReservedDoublePunctuator] SourceCharacter but not ClassSetSyntaxCharacter
//    \ CharacterEscape[+UnicodeMode]
//    \ ClassSetReservedPunctuator
//    \b

ClassSetCharacter
    : SourceCharacter
    | [\\] CharacterEscape
    | [\\] ClassSetReservedPunctuator
    | [\\] 'b'
    ;

// ClassSetReservedDoublePunctuator :: one of
//    && !! ## $$ %% ** ++ ,, .. :: ;; << == >> ?? @@ ^^ `` ~~

ClassSetReservedDoublePunctuator
    : '&&'
    | '!!'
    | '##'
    | '$$'
    | '%%'
    | '**'
    | '++'
    | ',,'
    | '..'
    | '::'
    | ';;'
    | '<<'
    | '=='
    | '>>'
    | '??'
    | '@@'
    | '^^'
    | '``'
    | '~~'
    ;

// ClassSetSyntaxCharacter :: one of
//    ( ) [ ] { } / - \ |

ClassSetSyntaxCharacter
    : [()[\]{}/\\|-]
    ;

// ClassSetReservedPunctuator :: one of
//    & - ! # % , : ; < = > @ ` ~

ClassSetReservedPunctuator
    : [&!#%,:;<=>@`~-]
    ;

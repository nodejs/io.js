// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 1998-2015, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*
* File parse.cpp
*
* Modification History:
*
*   Date          Name          Description
*   05/26/99     stephen       Creation.
*   02/25/00     weiv          Overhaul to write udata
*   5/10/01      Ram           removed ustdio dependency
*   06/10/2001  Dominic Ludlam <dom@recoil.org> Rewritten
*******************************************************************************
*/

// Safer use of UnicodeString.
#include <cstdint>
#include "unicode/umachine.h"
#ifndef UNISTR_FROM_CHAR_EXPLICIT
#   define UNISTR_FROM_CHAR_EXPLICIT explicit
#endif

// Less important, but still a good idea.
#ifndef UNISTR_FROM_STRING_EXPLICIT
#   define UNISTR_FROM_STRING_EXPLICIT explicit
#endif

#include <assert.h>
#include "parse.h"
#include "errmsg.h"
#include "uhash.h"
#include "cmemory.h"
#include "cstring.h"
#include "uinvchar.h"
#include "read.h"
#include "ustr.h"
#include "reslist.h"
#include "rbt_pars.h"
#include "genrb.h"
#include "unicode/normalizer2.h"
#include "unicode/stringpiece.h"
#include "unicode/unistr.h"
#include "unicode/ustring.h"
#include "unicode/uscript.h"
#include "unicode/utf16.h"
#include "unicode/putil.h"
#include "charstr.h"
#include "collationbuilder.h"
#include "collationdata.h"
#include "collationdatareader.h"
#include "collationdatawriter.h"
#include "collationfastlatinbuilder.h"
#include "collationinfo.h"
#include "collationroot.h"
#include "collationruleparser.h"
#include "collationtailoring.h"
#include <stdio.h>
#include "writesrc.h"

/* Number of tokens to read ahead of the current stream position */
#define MAX_LOOKAHEAD   3

#define CR               0x000D
#define LF               0x000A
#define SPACE            0x0020
#define TAB              0x0009
#define ESCAPE           0x005C
#define HASH             0x0023
#define QUOTE            0x0027
#define ZERO             0x0030
#define STARTCOMMAND     0x005B
#define ENDCOMMAND       0x005D
#define OPENSQBRACKET    0x005B
#define CLOSESQBRACKET   0x005D

#define ICU4X_DIACRITIC_BASE  0x0300
#define ICU4X_DIACRITIC_LIMIT 0x034F

using icu::CharString;
using icu::LocalMemory;
using icu::LocalPointer;
using icu::LocalUCHARBUFPointer;
using icu::StringPiece;
using icu::UnicodeString;

struct Lookahead
{
     enum   ETokenType type;
     struct UString    value;
     struct UString    comment;
     uint32_t          line;
};

/* keep in sync with token defines in read.h */
const char *tokenNames[TOK_TOKEN_COUNT] =
{
     "string",             /* A string token, such as "MonthNames" */
     "'{'",                 /* An opening brace character */
     "'}'",                 /* A closing brace character */
     "','",                 /* A comma */
     "':'",                 /* A colon */

     "<end of file>",     /* End of the file has been reached successfully */
     "<end of line>"
};

/* Just to store "TRUE" */
//static const char16_t trueValue[] = {0x0054, 0x0052, 0x0055, 0x0045, 0x0000};

typedef struct {
    struct Lookahead  lookahead[MAX_LOOKAHEAD + 1];
    uint32_t          lookaheadPosition;
    UCHARBUF         *buffer;
    struct SRBRoot *bundle;
    const char     *inputdir;
    uint32_t        inputdirLength;
    const char     *outputdir;
    uint32_t        outputdirLength;
    const char     *filename;
    UBool           makeBinaryCollation;
    UBool           omitCollationRules;
    UBool           icu4xMode;
} ParseState;

typedef struct SResource *
ParseResourceFunction(ParseState* state, char *tag, uint32_t startline, const struct UString* comment, UErrorCode *status);

static struct SResource *parseResource(ParseState* state, char *tag, const struct UString *comment, UErrorCode *status);

/* The nature of the lookahead buffer:
   There are MAX_LOOKAHEAD + 1 slots, used as a circular buffer.  This provides
   MAX_LOOKAHEAD lookahead tokens and a slot for the current token and value.
   When getToken is called, the current pointer is moved to the next slot and the
   old slot is filled with the next token from the reader by calling getNextToken.
   The token values are stored in the slot, which means that token values don't
   survive a call to getToken, ie.

   UString *value;

   getToken(&value, nullptr, status);
   getToken(nullptr,   nullptr, status);       bad - value is now a different string
*/
static void
initLookahead(ParseState* state, UCHARBUF *buf, UErrorCode *status)
{
    static uint32_t initTypeStrings = 0;
    uint32_t i;

    if (!initTypeStrings)
    {
        initTypeStrings = 1;
    }

    state->lookaheadPosition   = 0;
    state->buffer              = buf;

    resetLineNumber();

    for (i = 0; i < MAX_LOOKAHEAD; i++)
    {
        state->lookahead[i].type = getNextToken(state->buffer, &state->lookahead[i].value, &state->lookahead[i].line, &state->lookahead[i].comment, status);
        if (U_FAILURE(*status))
        {
            return;
        }
    }

    *status = U_ZERO_ERROR;
}

static void
cleanupLookahead(ParseState* state)
{
    uint32_t i;
    for (i = 0; i <= MAX_LOOKAHEAD; i++)
    {
        ustr_deinit(&state->lookahead[i].value);
        ustr_deinit(&state->lookahead[i].comment);
    }

}

static enum ETokenType
getToken(ParseState* state, struct UString **tokenValue, struct UString* comment, uint32_t *linenumber, UErrorCode *status)
{
    enum ETokenType result;
    uint32_t          i;

    result = state->lookahead[state->lookaheadPosition].type;

    if (tokenValue != nullptr)
    {
        *tokenValue = &state->lookahead[state->lookaheadPosition].value;
    }

    if (linenumber != nullptr)
    {
        *linenumber = state->lookahead[state->lookaheadPosition].line;
    }

    if (comment != nullptr)
    {
        ustr_cpy(comment, &(state->lookahead[state->lookaheadPosition].comment), status);
    }

    i = (state->lookaheadPosition + MAX_LOOKAHEAD) % (MAX_LOOKAHEAD + 1);
    state->lookaheadPosition = (state->lookaheadPosition + 1) % (MAX_LOOKAHEAD + 1);
    ustr_setlen(&state->lookahead[i].comment, 0, status);
    ustr_setlen(&state->lookahead[i].value, 0, status);
    state->lookahead[i].type = getNextToken(state->buffer, &state->lookahead[i].value, &state->lookahead[i].line, &state->lookahead[i].comment, status);

    /* printf("getToken, returning %s\n", tokenNames[result]); */

    return result;
}

static enum ETokenType
peekToken(ParseState* state, uint32_t lookaheadCount, struct UString **tokenValue, uint32_t *linenumber, struct UString *comment, UErrorCode *status)
{
    uint32_t i = (state->lookaheadPosition + lookaheadCount) % (MAX_LOOKAHEAD + 1);

    if (U_FAILURE(*status))
    {
        return TOK_ERROR;
    }

    if (lookaheadCount >= MAX_LOOKAHEAD)
    {
        *status = U_INTERNAL_PROGRAM_ERROR;
        return TOK_ERROR;
    }

    if (tokenValue != nullptr)
    {
        *tokenValue = &state->lookahead[i].value;
    }

    if (linenumber != nullptr)
    {
        *linenumber = state->lookahead[i].line;
    }

    if(comment != nullptr){
        ustr_cpy(comment, &(state->lookahead[state->lookaheadPosition].comment), status);
    }

    return state->lookahead[i].type;
}

static void
expect(ParseState* state, enum ETokenType expectedToken, struct UString **tokenValue, struct UString *comment, uint32_t *linenumber, UErrorCode *status)
{
    uint32_t        line;

    enum ETokenType token = getToken(state, tokenValue, comment, &line, status);

    if (linenumber != nullptr)
    {
        *linenumber = line;
    }

    if (U_FAILURE(*status))
    {
        return;
    }

    if (token != expectedToken)
    {
        *status = U_INVALID_FORMAT_ERROR;
        error(line, "expecting %s, got %s", tokenNames[expectedToken], tokenNames[token]);
    }
    else
    {
        *status = U_ZERO_ERROR;
    }
}

static char *getInvariantString(ParseState* state, uint32_t *line, struct UString *comment,
                                int32_t &stringLength, UErrorCode *status)
{
    struct UString *tokenValue;
    char           *result;

    expect(state, TOK_STRING, &tokenValue, comment, line, status);

    if (U_FAILURE(*status))
    {
        return nullptr;
    }

    if(!uprv_isInvariantUString(tokenValue->fChars, tokenValue->fLength)) {
        *status = U_INVALID_FORMAT_ERROR;
        error(*line, "invariant characters required for table keys, binary data, etc.");
        return nullptr;
    }

    result = static_cast<char *>(uprv_malloc(tokenValue->fLength+1));

    if (result == nullptr)
    {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }

    u_UCharsToChars(tokenValue->fChars, result, tokenValue->fLength+1);
    stringLength = tokenValue->fLength;
    return result;
}

static struct SResource *
parseUCARules(ParseState* state, char *tag, uint32_t startline, const struct UString* /*comment*/, UErrorCode *status)
{
    struct SResource *result = nullptr;
    struct UString   *tokenValue;
    FileStream       *file          = nullptr;
    char              filename[256] = { '\0' };
    char              cs[128]       = { '\0' };
    uint32_t          line;
    UBool quoted = false;
    UCHARBUF *ucbuf=nullptr;
    UChar32   c     = 0;
    const char* cp  = nullptr;
    char16_t *pTarget     = nullptr;
    char16_t *target      = nullptr;
    char16_t *targetLimit = nullptr;
    int32_t size = 0;

    expect(state, TOK_STRING, &tokenValue, nullptr, &line, status);

    if(isVerbose()){
        printf(" %s at line %i \n", tag == nullptr ? "(null)" : tag, static_cast<int>(startline));
    }

    if (U_FAILURE(*status))
    {
        return nullptr;
    }
    /* make the filename including the directory */
    if (state->inputdir != nullptr)
    {
        uprv_strcat(filename, state->inputdir);

        if (state->inputdir[state->inputdirLength - 1] != U_FILE_SEP_CHAR)
        {
            uprv_strcat(filename, U_FILE_SEP_STRING);
        }
    }

    u_UCharsToChars(tokenValue->fChars, cs, tokenValue->fLength);

    expect(state, TOK_CLOSE_BRACE, nullptr, nullptr, nullptr, status);

    if (U_FAILURE(*status))
    {
        return nullptr;
    }
    uprv_strcat(filename, cs);

    if(state->omitCollationRules) {
        return res_none();
    }

    ucbuf = ucbuf_open(filename, &cp, getShowWarning(),false, status);

    if (U_FAILURE(*status)) {
        error(line, "An error occurred while opening the input file %s\n", filename);
        return nullptr;
    }

    /* We allocate more space than actually required
    * since the actual size needed for storing UChars
    * is not known in UTF-8 byte stream
    */
    size        = ucbuf_size(ucbuf) + 1;
    pTarget = static_cast<char16_t*>(uprv_malloc(U_SIZEOF_UCHAR * size));
    uprv_memset(pTarget, 0, size*U_SIZEOF_UCHAR);
    target      = pTarget;
    targetLimit = pTarget+size;

    /* read the rules into the buffer */
    while (target < targetLimit)
    {
        c = ucbuf_getc(ucbuf, status);
        if(c == QUOTE) {
            quoted = static_cast<UBool>(!quoted);
        }
        /* weiv (06/26/2002): adding the following:
         * - preserving spaces in commands [...]
         * - # comments until the end of line
         */
        if (c == STARTCOMMAND && !quoted)
        {
            /* preserve commands
             * closing bracket will be handled by the
             * append at the end of the loop
             */
            while(c != ENDCOMMAND) {
                U_APPEND_CHAR32_ONLY(c, target);
                c = ucbuf_getc(ucbuf, status);
            }
        }
        else if (c == HASH && !quoted) {
            /* skip comments */
            while(c != CR && c != LF) {
                c = ucbuf_getc(ucbuf, status);
            }
            continue;
        }
        else if (c == ESCAPE)
        {
            c = unescape(ucbuf, status);

            if (c == static_cast<UChar32>(U_ERR))
            {
                uprv_free(pTarget);
                T_FileStream_close(file);
                return nullptr;
            }
        }
        else if (!quoted && (c == SPACE || c == TAB || c == CR || c == LF))
        {
            /* ignore spaces carriage returns
            * and line feed unless in the form \uXXXX
            */
            continue;
        }

        /* Append char16_t * after dissembling if c > 0xffff*/
        if (c != static_cast<UChar32>(U_EOF))
        {
            U_APPEND_CHAR32_ONLY(c, target);
        }
        else
        {
            break;
        }
    }

    /* terminate the string */
    if(target < targetLimit){
        *target = 0x0000;
    }

    result = string_open(state->bundle, tag, pTarget, static_cast<int32_t>(target - pTarget), nullptr, status);


    ucbuf_close(ucbuf);
    uprv_free(pTarget);
    T_FileStream_close(file);

    return result;
}

static struct SResource *
parseTransliterator(ParseState* state, char *tag, uint32_t startline, const struct UString* /*comment*/, UErrorCode *status)
{
    struct SResource *result = nullptr;
    struct UString   *tokenValue;
    FileStream       *file          = nullptr;
    char              filename[256] = { '\0' };
    char              cs[128]       = { '\0' };
    uint32_t          line;
    UCHARBUF *ucbuf=nullptr;
    const char* cp  = nullptr;
    char16_t *pTarget     = nullptr;
    const char16_t *pSource     = nullptr;
    int32_t size = 0;

    expect(state, TOK_STRING, &tokenValue, nullptr, &line, status);

    if(isVerbose()){
        printf(" %s at line %i \n", tag == nullptr ? "(null)" : tag, static_cast<int>(startline));
    }

    if (U_FAILURE(*status))
    {
        return nullptr;
    }
    /* make the filename including the directory */
    if (state->inputdir != nullptr)
    {
        uprv_strcat(filename, state->inputdir);

        if (state->inputdir[state->inputdirLength - 1] != U_FILE_SEP_CHAR)
        {
            uprv_strcat(filename, U_FILE_SEP_STRING);
        }
    }

    u_UCharsToChars(tokenValue->fChars, cs, tokenValue->fLength);

    expect(state, TOK_CLOSE_BRACE, nullptr, nullptr, nullptr, status);

    if (U_FAILURE(*status))
    {
        return nullptr;
    }
    uprv_strcat(filename, cs);


    ucbuf = ucbuf_open(filename, &cp, getShowWarning(),false, status);

    if (U_FAILURE(*status)) {
        error(line, "An error occurred while opening the input file %s\n", filename);
        return nullptr;
    }

    /* We allocate more space than actually required
    * since the actual size needed for storing UChars
    * is not known in UTF-8 byte stream
    */
    pSource = ucbuf_getBuffer(ucbuf, &size, status);
    pTarget = static_cast<char16_t*>(uprv_malloc(U_SIZEOF_UCHAR * (size + 1)));
    uprv_memset(pTarget, 0, size*U_SIZEOF_UCHAR);

#if !UCONFIG_NO_TRANSLITERATION
    size = utrans_stripRules(pSource, size, pTarget, status);
#else
    size = 0;
    fprintf(stderr, " Warning: writing empty transliteration data ( UCONFIG_NO_TRANSLITERATION ) \n");
#endif
    result = string_open(state->bundle, tag, pTarget, size, nullptr, status);

    ucbuf_close(ucbuf);
    uprv_free(pTarget);
    T_FileStream_close(file);

    return result;
}
static ArrayResource* dependencyArray = nullptr;

static struct SResource *
parseDependency(ParseState* state, char *tag, uint32_t startline, const struct UString* comment, UErrorCode *status)
{
    struct SResource *result = nullptr;
    struct SResource *elem = nullptr;
    struct UString   *tokenValue;
    uint32_t          line;
    char              filename[256] = { '\0' };
    char              cs[128]       = { '\0' };

    expect(state, TOK_STRING, &tokenValue, nullptr, &line, status);

    if(isVerbose()){
        printf(" %s at line %i \n", tag == nullptr ? "(null)" : tag, static_cast<int>(startline));
    }

    if (U_FAILURE(*status))
    {
        return nullptr;
    }
    /* make the filename including the directory */
    if (state->outputdir != nullptr)
    {
        uprv_strcat(filename, state->outputdir);

        if (state->outputdir[state->outputdirLength - 1] != U_FILE_SEP_CHAR)
        {
            uprv_strcat(filename, U_FILE_SEP_STRING);
        }
    }

    u_UCharsToChars(tokenValue->fChars, cs, tokenValue->fLength);

    if (U_FAILURE(*status))
    {
        return nullptr;
    }
    uprv_strcat(filename, cs);
    if(!T_FileStream_file_exists(filename)){
        if(isStrict()){
            error(line, "The dependency file %s does not exist. Please make sure it exists.\n",filename);
        }else{
            warning(line, "The dependency file %s does not exist. Please make sure it exists.\n",filename);
        }
    }
    if(dependencyArray==nullptr){
        dependencyArray = array_open(state->bundle, "%%DEPENDENCY", nullptr, status);
    }
    if(tag!=nullptr){
        result = string_open(state->bundle, tag, tokenValue->fChars, tokenValue->fLength, comment, status);
    }
    elem = string_open(state->bundle, nullptr, tokenValue->fChars, tokenValue->fLength, comment, status);

    dependencyArray->add(elem);

    if (U_FAILURE(*status))
    {
        return nullptr;
    }
    expect(state, TOK_CLOSE_BRACE, nullptr, nullptr, nullptr, status);
    return result;
}
static struct SResource *
parseString(ParseState* state, char *tag, uint32_t startline, const struct UString* comment, UErrorCode *status)
{
    struct UString   *tokenValue;
    struct SResource *result = nullptr;

/*    if (tag != nullptr && uprv_strcmp(tag, "%%UCARULES") == 0)
    {
        return parseUCARules(tag, startline, status);
    }*/
    if(isVerbose()){
        printf(" string %s at line %i \n", tag == nullptr ? "(null)" : tag, static_cast<int>(startline));
    }
    expect(state, TOK_STRING, &tokenValue, nullptr, nullptr, status);

    if (U_SUCCESS(*status))
    {
        /* create the string now - tokenValue doesn't survive a call to getToken (and therefore
        doesn't survive expect either) */

        result = string_open(state->bundle, tag, tokenValue->fChars, tokenValue->fLength, comment, status);
        if(U_SUCCESS(*status) && result) {
            expect(state, TOK_CLOSE_BRACE, nullptr, nullptr, nullptr, status);

            if (U_FAILURE(*status))
            {
                res_close(result);
                return nullptr;
            }
        }
    }

    return result;
}

static struct SResource *
parseAlias(ParseState* state, char *tag, uint32_t startline, const struct UString *comment, UErrorCode *status)
{
    struct UString   *tokenValue;
    struct SResource *result  = nullptr;

    expect(state, TOK_STRING, &tokenValue, nullptr, nullptr, status);

    if(isVerbose()){
        printf(" alias %s at line %i \n", tag == nullptr ? "(null)" : tag, static_cast<int>(startline));
    }

    if (U_SUCCESS(*status))
    {
        /* create the string now - tokenValue doesn't survive a call to getToken (and therefore
        doesn't survive expect either) */

        result = alias_open(state->bundle, tag, tokenValue->fChars, tokenValue->fLength, comment, status);

        expect(state, TOK_CLOSE_BRACE, nullptr, nullptr, nullptr, status);

        if (U_FAILURE(*status))
        {
            res_close(result);
            return nullptr;
        }
    }

    return result;
}

#if !UCONFIG_NO_COLLATION

namespace {

struct SResource* resLookup(struct SResource* res, const char* key) {
    if (res == res_none() || !res->isTable()) {
        return nullptr;
    }

    TableResource *list = static_cast<TableResource *>(res);
    SResource *current = list->fFirst;
    while (current != nullptr) {
        if (uprv_strcmp(((list->fRoot->fKeys) + (current->fKey)), key) == 0) {
            return current;
        }
        current = current->fNext;
    }
    return nullptr;
}

class GenrbImporter : public icu::CollationRuleParser::Importer {
public:
    GenrbImporter(const char *in, const char *out) : inputDir(in), outputDir(out) {}
    virtual ~GenrbImporter();
    virtual void getRules(
            const char *localeID, const char *collationType,
            UnicodeString &rules,
            const char *&errorReason, UErrorCode &errorCode) override;

private:
    const char *inputDir;
    const char *outputDir;
};

GenrbImporter::~GenrbImporter() {}

void
GenrbImporter::getRules(
        const char *localeID, const char *collationType,
        UnicodeString &rules,
        const char *& /*errorReason*/, UErrorCode &errorCode) {
    CharString filename(localeID, errorCode);
    for(int32_t i = 0; i < filename.length(); i++){
        if(filename[i] == '-'){
            filename.data()[i] = '_';
        }
    }
    filename.append(".txt", errorCode);
    if (U_FAILURE(errorCode)) {
        return;
    }
    CharString inputDirBuf;
    CharString openFileName;
    if(inputDir == nullptr) {
        const char *filenameBegin = uprv_strrchr(filename.data(), U_FILE_SEP_CHAR);
        if (filenameBegin != nullptr) {
            /*
             * When a filename ../../../data/root.txt is specified,
             * we presume that the input directory is ../../../data
             * This is very important when the resource file includes
             * another file, like UCARules.txt or thaidict.brk.
             */
            StringPiece dir = filename.toStringPiece();
            const char *filenameLimit = filename.data() + filename.length();
            dir.remove_suffix(static_cast<int32_t>(filenameLimit - filenameBegin));
            inputDirBuf.append(dir, errorCode);
            inputDir = inputDirBuf.data();
        }
    }else{
        int32_t dirlen = static_cast<int32_t>(uprv_strlen(inputDir));

        if((filename[0] != U_FILE_SEP_CHAR) && (inputDir[dirlen-1] !='.')) {
            /*
             * append the input dir to openFileName if the first char in
             * filename is not file separator char and the last char input directory is  not '.'.
             * This is to support :
             * genrb -s. /home/icu/data
             * genrb -s. icu/data
             * The user cannot mix notations like
             * genrb -s. /icu/data --- the absolute path specified. -s redundant
             * user should use
             * genrb -s. icu/data  --- start from CWD and look in icu/data dir
             */
            openFileName.append(inputDir, dirlen, errorCode);
            if(inputDir[dirlen-1] != U_FILE_SEP_CHAR) {
                openFileName.append(U_FILE_SEP_CHAR, errorCode);
            }
        }
    }
    openFileName.append(filename, errorCode);
    if(U_FAILURE(errorCode)) {
        return;
    }
    // printf("GenrbImporter::getRules(%s, %s) reads %s\n", localeID, collationType, openFileName.data());
    const char* cp = "";
    LocalUCHARBUFPointer ucbuf(
            ucbuf_open(openFileName.data(), &cp, getShowWarning(), true, &errorCode));
    if(errorCode == U_FILE_ACCESS_ERROR) {
        fprintf(stderr, "couldn't open file %s\n", openFileName.data());
        return;
    }
    if (ucbuf.isNull() || U_FAILURE(errorCode)) {
        fprintf(stderr, "An error occurred processing file %s. Error: %s\n", openFileName.data(), u_errorName(errorCode));
        return;
    }

    /* Parse the data into an SRBRoot */
    LocalPointer<SRBRoot> data(
            parse(ucbuf.getAlias(), inputDir, outputDir, filename.data(), false, false, false, &errorCode));
    if (U_FAILURE(errorCode)) {
        return;
    }

    struct SResource *root = data->fRoot;
    struct SResource *collations = resLookup(root, "collations");
    if (collations != nullptr) {
      struct SResource *collation = resLookup(collations, collationType);
      if (collation != nullptr) {
        struct SResource *sequence = resLookup(collation, "Sequence");
        if (sequence != nullptr && sequence->isString()) {
          // No string pointer aliasing so that we need not hold onto the resource bundle.
          StringResource *sr = static_cast<StringResource *>(sequence);
          rules = sr->fString;
        }
      }
    }
}

// Quick-and-dirty escaping function.
// Assumes that we are on an ASCII-based platform.
void
escape(const char16_t *s, char *buffer, size_t n) {
    int32_t length = u_strlen(s);
    int32_t i = 0;
    for (;;) {
        UChar32 c;
        U16_NEXT(s, i, length, c);
        if (c == 0) {
            *buffer = 0;
            return;
        } else if (0x20 <= c && c <= 0x7e) {
            // printable ASCII
            *buffer++ = static_cast<char>(c); // assumes ASCII-based platform
        } else {
            buffer += snprintf(buffer, n, "\\u%04X", static_cast<int>(c));
        }
    }
}

}  // namespace

static FILE*
openTOML(const char* outputdir, const char* name, const char* collationType, const char* structType, UErrorCode *status) {
    CharString baseName;
    baseName.append(name, *status);
    baseName.append("_", *status);
    baseName.append(collationType, *status);
    baseName.append("_", *status);
    baseName.append(structType, *status);

    CharString outFileName;
    if (outputdir && *outputdir) {
        outFileName.append(outputdir, *status).ensureEndsWithFileSeparator(*status);
    }
    outFileName.append(baseName, *status);
    outFileName.append(".toml", *status);
    if (U_FAILURE(*status)) {
        return nullptr;
    }

    FILE* f = fopen(outFileName.data(), "w");
    if (!f) {
        *status = U_FILE_ACCESS_ERROR;
        return nullptr;
    }
    usrc_writeFileNameGeneratedBy(f, "#", baseName.data(), "genrb -X");

    return f;
}

static void
writeCollationMetadataTOML(const char* outputdir, const char* name, const char* collationType, const uint32_t metadataBits, UErrorCode *status) {
    FILE* f = openTOML(outputdir, name, collationType, "meta", status);
    if (!f) {
        return;
    }
    // printf("writeCollationMetadataTOML %s %s\n", name, collationType);
    fprintf(f, "bits = 0x%X\n", metadataBits);
    fclose(f);
}

static UChar32
writeCollationDiacriticsTOML(const char* outputdir, const char* name, const char* collationType, const icu::CollationData* data, UErrorCode *status) {
    UChar32 limit = ICU4X_DIACRITIC_LIMIT;
    FILE* f = openTOML(outputdir, name, collationType, "dia", status);
    if (!f) {
        return limit;
    }
    // printf("writeCollationDiacriticsTOML %s %s\n", name, collationType);
    uint16_t secondaries[ICU4X_DIACRITIC_LIMIT-ICU4X_DIACRITIC_BASE];
    for (UChar32 c = ICU4X_DIACRITIC_BASE; c < ICU4X_DIACRITIC_LIMIT; ++c) {
        uint16_t secondary = 0;
        uint32_t ce32 = data->getCE32(c);
        if (ce32 == icu::Collation::FALLBACK_CE32) {
            ce32 = data->base->getCE32(c);
        }
        if (c == 0x0340 || c == 0x0341 || c == 0x0343 || c == 0x0344) {
            // These never occur in NFD data
        } else if (!icu::Collation::isSimpleOrLongCE32(ce32)) {
            if (uprv_strcmp(name, "root") == 0) {
                printf("UNSUPPORTED DIACRITIC CE32 in root: TAG: %X CE32: %X char: %X\n", icu::Collation::tagFromCE32(ce32), ce32, c);
                fclose(f);
                *status = U_INTERNAL_PROGRAM_ERROR;
                return limit;
            }
            limit = c;
            break;
        } else {
            uint64_t ce = static_cast<uint64_t>(icu::Collation::ceFromCE32(ce32));
            if ((ce & 0xFFFFFFFF0000FFFF) != static_cast<uint64_t>(icu::Collation::COMMON_TERTIARY_CE)) {
                // Not a CE where only the secondary weight differs from the expected
                // pattern.
                limit = c;
                break;
            }
            secondary = static_cast<uint16_t>(ce >> 16);
        }
        secondaries[c - ICU4X_DIACRITIC_BASE] = secondary;

    }
    usrc_writeArray(f, "secondaries = [\n  ", secondaries, 16, limit-ICU4X_DIACRITIC_BASE, "  ", "\n]\n");
    fclose(f);
    return limit;
}

static void
writeCollationReorderingTOML(const char* outputdir, const char* name, const char* collationType, const icu::CollationSettings* settings, UErrorCode *status) {
    FILE* f = openTOML(outputdir, name, collationType, "reord", status);
    if (!f) {
        return;
    }
    // printf("writeCollationReorderingTOML %s %s\n", name, collationType);
    fprintf(f, "min_high_no_reorder = 0x%X\n", settings->minHighNoReorder);
    usrc_writeArray(f, "reorder_table = [\n  ", settings->reorderTable, 8, 256, "  ", "\n]\n");
    usrc_writeArray(f, "reorder_ranges = [\n  ", settings->reorderRanges, 32, settings->reorderRangesLength, "  ", "\n]\n");
    fclose(f);
}


static void
writeCollationJamoTOML(const char* outputdir, const char* name, const char* collationType, const icu::CollationData* data, UErrorCode *status) {
    FILE* f = openTOML(outputdir, name, collationType, "jamo", status);
    if (!f) {
        printf("writeCollationJamoTOML FAILED TO OPEN FILE %s %s\n", name, collationType);
        return;
    }
    uint32_t jamo[0x1200-0x1100];
    for (UChar32 c = 0x1100; c < 0x1200; ++c) {
        uint32_t ce32 = data->getCE32(c);
        if (ce32 == icu::Collation::FALLBACK_CE32) {
            ce32 = data->base->getCE32(c);
        }
        // Can't reject complex CE32s, because search collations have expansions.
        // These expansions refer to the tailoring, which foils the reuse of the
        // these jamo tables.
        // XXX Figure out what to do. Perhaps instead of having Latin mini expansions,
        // there should be Hangul mini expansions.
        // XXX in any case, validate that modern jamo are self-contained.
        jamo[c - 0x1100] = ce32;

    }
    usrc_writeArray(f, "ce32s = [\n  ", jamo, 32, 0x1200-0x1100, "  ", "\n]\n");
    fclose(f);
}

static UBool
convertTrie(const void *context, UChar32 start, UChar32 end, uint32_t value) {
    if (start >= 0x1100 && start < 0x1200 && end >= 0x1100 && end < 0x1200) {
        // Range entirely in conjoining jamo block.
        return true;
    }
    icu::IcuToolErrorCode status("genrb: convertTrie");
    umutablecptrie_setRange((UMutableCPTrie*)context, start, end, value, status);
    return !U_FAILURE(*status);
}

static void
writeCollationDataTOML(const char* outputdir, const char* name, const char* collationType, const icu::CollationData* data, UBool root, UChar32 diacriticLimit, UErrorCode *status) {
    FILE* f = openTOML(outputdir, name, collationType, "data", status);
    if (!f) {
        return;
    }
    // printf("writeCollationDataTOML %s %s\n", name, collationType);

    icu::UnicodeSet tailoringSet;

    if (data->base) {
        tailoringSet.addAll(*(data->unsafeBackwardSet));
        tailoringSet.removeAll(*(data->base->unsafeBackwardSet));
    } else {
        tailoringSet.addAll(*(data->unsafeBackwardSet));
    }

    // Use the same value for out-of-range and default in the hope of not having to allocate
    // different blocks, since ICU4X never does out-of-range queries.
    uint32_t trieDefault = root ? icu::Collation::UNASSIGNED_CE32 : icu::Collation::FALLBACK_CE32;
    icu::LocalUMutableCPTriePointer builder(umutablecptrie_open(trieDefault, trieDefault, status));

    utrie2_enum(data->trie, nullptr, &convertTrie, builder.getAlias());

    // If the diacritic table was cut short, copy CE32s between the lowered
    // limit and the max limit from the root to the tailoring. As of June 2022,
    // no collation in CLDR needs this.
    for (UChar32 c = diacriticLimit; c < ICU4X_DIACRITIC_LIMIT; ++c) {
        if (c == 0x0340 || c == 0x0341 || c == 0x0343 || c == 0x0344) {
            // These never occur in NFD data.
            continue;
        }
        uint32_t ce32 = data->getCE32(c);
        if (ce32 == icu::Collation::FALLBACK_CE32) {
            ce32 = data->base->getCE32(c);
            umutablecptrie_set(builder.getAlias(), c, ce32, status);
        }
    }

    // Ensure that the range covered by the diacritic table isn't duplicated
    // in the trie.
    for (UChar32 c = ICU4X_DIACRITIC_BASE; c < diacriticLimit; ++c) {
        if (umutablecptrie_get(builder.getAlias(), c) != trieDefault) {
            umutablecptrie_set(builder.getAlias(), c, trieDefault, status);
        }
    }

    icu::LocalUCPTriePointer utrie(umutablecptrie_buildImmutable(
    builder.getAlias(),
    UCPTRIE_TYPE_SMALL,
    UCPTRIE_VALUE_BITS_32,
    status));
    usrc_writeArray(f, "contexts = [\n  ", data->contexts, 16, data->contextsLength, "  ", "\n]\n");
    usrc_writeArray(f, "ce32s = [\n  ", data->ce32s, 32, data->ce32sLength, "  ", "\n]\n");
    usrc_writeArray(f, "ces = [\n  ", data->ces, 64, data->cesLength, "  ", "\n]\n");
    fprintf(f, "[trie]\n");
    usrc_writeUCPTrie(f, "trie", utrie.getAlias(), UPRV_TARGET_SYNTAX_TOML);

    fclose(f);
}

static void
writeCollationSpecialPrimariesTOML(const char* outputdir, const char* name, const char* collationType, const icu::CollationData* data, UErrorCode *status) {
    FILE* f = openTOML(outputdir, name, collationType, "prim", status);
    if (!f) {
        return;
    }
    // printf("writeCollationSpecialPrimariesTOML %s %s\n", name, collationType);

    uint16_t lastPrimaries[4];
    for (int32_t i = 0; i < 4; ++i) {
        // getLastPrimaryForGroup subtracts one from a 16-bit value, so we add one
        // back to get a value that fits in 16 bits.
        lastPrimaries[i] = static_cast<uint16_t>((data->getLastPrimaryForGroup(UCOL_REORDER_CODE_FIRST + i) + 1) >> 16);
    }

    uint32_t numericPrimary = data->numericPrimary;
    if (numericPrimary & 0xFFFFFF) {
        printf("Lower 24 bits set in numeric primary");
        *status = U_INTERNAL_PROGRAM_ERROR;
        return;
    }

    usrc_writeArray(f, "last_primaries = [\n  ", lastPrimaries, 16, 4, "  ", "\n]\n");
    fprintf(f, "numeric_primary = 0x%X\n", numericPrimary >> 24);
    fclose(f);
}

static void
writeCollationTOML(const char* outputdir, const char* name, const char* collationType, const icu::CollationData* data, const icu::CollationSettings* settings, UErrorCode *status) {
    UBool tailored = false;
    UBool tailoredDiacritics = false;
    UBool lithuanianDotAbove = (uprv_strcmp(name, "lt") == 0);
    UBool reordering = false;
    UBool isRoot = uprv_strcmp(name, "root") == 0;
    UChar32 diacriticLimit = ICU4X_DIACRITIC_LIMIT;
    if (!data->base && isRoot) {
        diacriticLimit = writeCollationDiacriticsTOML(outputdir, name, collationType, data, status);
        if (U_FAILURE(*status)) {
            return;
        }
        writeCollationJamoTOML(outputdir, name, collationType, data, status);
        if (U_FAILURE(*status)) {
            return;
        }
        writeCollationSpecialPrimariesTOML(outputdir, name, collationType, data, status);
        if (U_FAILURE(*status)) {
            return;
        }
    } else if (data->base && !lithuanianDotAbove) {
        for (UChar32 c = ICU4X_DIACRITIC_BASE; c < ICU4X_DIACRITIC_LIMIT; ++c) {
            if (c == 0x0340 || c == 0x0341 || c == 0x0343 || c == 0x0344) {
                // These never occur in NFD data.
                continue;
            }
            uint32_t ce32 = data->getCE32(c);
            if ((ce32 != icu::Collation::FALLBACK_CE32) && (ce32 != data->base->getCE32(c))) {
                tailoredDiacritics = true;
                diacriticLimit = writeCollationDiacriticsTOML(outputdir, name, collationType, data, status);
                if (U_FAILURE(*status)) {
                    return;
                }
                break;
            }
        }
    }

    if (settings->hasReordering()) {
        reordering = true;
        // Note: There are duplicate reorderings. Expecting the ICU4X provider
        // to take care of deduplication.
        writeCollationReorderingTOML(outputdir, name, collationType, settings, status);
        if (U_FAILURE(*status)) {
            return;
        }
    }

    // Write collation data if either base is non-null or the name is root.
    // Languages that only reorder scripts are otherwise root-like and have
    // null base.
    if (data->base || isRoot) {
        tailored = !isRoot;
        writeCollationDataTOML(outputdir, name, collationType, data, (!data->base && isRoot), diacriticLimit, status);
        if (U_FAILURE(*status)) {
            return;
        }
    }

    uint32_t maxVariable = static_cast<uint32_t>(settings->getMaxVariable());
    if (maxVariable >= 4) {
        printf("Max variable out of range");
        *status = U_INTERNAL_PROGRAM_ERROR;
        return;
    }

    uint32_t metadataBits = maxVariable;
    if (tailored) {
        metadataBits |= (1 << 3);
    }
    if (tailoredDiacritics) {
        metadataBits |= (1 << 4);
    }
    if (reordering) {
        metadataBits |= (1 << 5);
    }
    if (lithuanianDotAbove) {
        metadataBits |= (1 << 6);
    }
    if ((settings->options & icu::CollationSettings::BACKWARD_SECONDARY) != 0) {
        metadataBits |= (1 << 7);
    }
    if (settings->getAlternateHandling() == UCOL_SHIFTED) {
        metadataBits |= (1 << 8);
    }
    switch (settings->getCaseFirst()) {
        case UCOL_OFF:
            break;
        case UCOL_UPPER_FIRST:
            metadataBits |= (1 << 9);
            metadataBits |= (1 << 10);
            break;
        case UCOL_LOWER_FIRST:
            metadataBits |= (1 << 9);
            break;
        default:
            *status = U_INTERNAL_PROGRAM_ERROR;
            return;
    }

    writeCollationMetadataTOML(outputdir, name, collationType, metadataBits, status);
}

#endif  // !UCONFIG_NO_COLLATION

static TableResource *
addCollation(ParseState* state, TableResource  *result, const char *collationType,
             uint32_t startline, UErrorCode *status)
{
    // TODO: Use LocalPointer for result, or make caller close it when there is a failure.
    struct SResource  *member = nullptr;
    struct UString    *tokenValue;
    struct UString     comment;
    enum   ETokenType  token;
    char               subtag[1024];
    UnicodeString      rules;
    UBool              haveRules = false;
    UVersionInfo       version;
    uint32_t           line;

    /* '{' . (name resource)* '}' */
    version[0]=0; version[1]=0; version[2]=0; version[3]=0;

    for (;;)
    {
        ustr_init(&comment);
        token = getToken(state, &tokenValue, &comment, &line, status);

        if (token == TOK_CLOSE_BRACE)
        {
            break;
        }

        if (token != TOK_STRING)
        {
            res_close(result);
            *status = U_INVALID_FORMAT_ERROR;

            if (token == TOK_EOF)
            {
                error(startline, "unterminated table");
            }
            else
            {
                error(line, "Unexpected token %s", tokenNames[token]);
            }

            return nullptr;
        }

        u_UCharsToChars(tokenValue->fChars, subtag, u_strlen(tokenValue->fChars) + 1);

        if (U_FAILURE(*status))
        {
            res_close(result);
            return nullptr;
        }

        member = parseResource(state, subtag, nullptr, status);

        if (U_FAILURE(*status))
        {
            res_close(result);
            return nullptr;
        }
        if (result == nullptr)
        {
            // Ignore the parsed resources, continue parsing.
        }
        else if (uprv_strcmp(subtag, "Version") == 0 && member->isString())
        {
            StringResource *sr = static_cast<StringResource *>(member);
            char     ver[40];
            int32_t length = sr->length();

            if (length >= UPRV_LENGTHOF(ver))
            {
                length = UPRV_LENGTHOF(ver) - 1;
            }

            sr->fString.extract(0, length, ver, UPRV_LENGTHOF(ver), US_INV);
            u_versionFromString(version, ver);

            result->add(member, line, *status);
            member = nullptr;
        }
        else if(uprv_strcmp(subtag, "%%CollationBin")==0)
        {
            /* discard duplicate %%CollationBin if any*/
        }
        else if (uprv_strcmp(subtag, "Sequence") == 0 && member->isString())
        {
            StringResource *sr = static_cast<StringResource *>(member);
            rules = sr->fString;
            haveRules = true;
            // Defer building the collator until we have seen
            // all sub-elements of the collation table, including the Version.
            /* in order to achieve smaller data files, we can direct genrb */
            /* to omit collation rules */
            if(!state->omitCollationRules) {
                result->add(member, line, *status);
                member = nullptr;
            }
        }
        else  // Just copy non-special items.
        {
            result->add(member, line, *status);
            member = nullptr;
        }
        res_close(member);  // TODO: use LocalPointer
        if (U_FAILURE(*status))
        {
            res_close(result);
            return nullptr;
        }
    }

    if (!haveRules) { return result; }

#if UCONFIG_NO_COLLATION || UCONFIG_NO_FILE_IO
    warning(line, "Not building collation elements because of UCONFIG_NO_COLLATION and/or UCONFIG_NO_FILE_IO, see uconfig.h");
    (void)collationType;
#else
    // CLDR ticket #3949, ICU ticket #8082:
    // Do not build collation binary data for for-import-only "private" collation rule strings.
    if (uprv_strncmp(collationType, "private-", 8) == 0) {
        if(isVerbose()) {
            printf("Not building %s~%s collation binary\n", state->filename, collationType);
        }
        return result;
    }

    if(!state->makeBinaryCollation) {
        if(isVerbose()) {
            printf("Not building %s~%s collation binary\n", state->filename, collationType);
        }
        return result;
    }
    UErrorCode intStatus = U_ZERO_ERROR;
    UParseError parseError;
    uprv_memset(&parseError, 0, sizeof(parseError));
    GenrbImporter importer(state->inputdir, state->outputdir);
    const icu::CollationTailoring *base = icu::CollationRoot::getRoot(intStatus);
    if(U_FAILURE(intStatus)) {
        error(line, "failed to load root collator (ucadata.icu) - %s", u_errorName(intStatus));
        res_close(result);
        return nullptr;  // TODO: use LocalUResourceBundlePointer for result
    }
    icu::CollationBuilder builder(base, state->icu4xMode, intStatus);
    if(state->icu4xMode || (uprv_strncmp(collationType, "search", 6) == 0)) {
        builder.disableFastLatin();  // build fast-Latin table unless search collator or ICU4X
    }
    LocalPointer<icu::CollationTailoring> t(
            builder.parseAndBuild(rules, version, &importer, &parseError, intStatus));
    if(U_FAILURE(intStatus)) {
        const char *reason = builder.getErrorReason();
        if(reason == nullptr) { reason = ""; }
        error(line, "CollationBuilder failed at %s~%s/Sequence rule offset %ld: %s  %s",
                state->filename, collationType,
                static_cast<long>(parseError.offset), u_errorName(intStatus), reason);
        if(parseError.preContext[0] != 0 || parseError.postContext[0] != 0) {
            // Print pre- and post-context.
            char preBuffer[100], postBuffer[100];
            escape(parseError.preContext, preBuffer, sizeof(preBuffer));
            escape(parseError.postContext, postBuffer, sizeof(postBuffer));
            error(line, "  error context: \"...%s\" ! \"%s...\"", preBuffer, postBuffer);
        }
        if(isStrict() || t.isNull()) {
            *status = intStatus;
            res_close(result);
            return nullptr;
        }
    }
    if (state->icu4xMode) {
        char *nameWithoutSuffix = static_cast<char *>(uprv_malloc(uprv_strlen(state->filename) + 1));
        if (nameWithoutSuffix == nullptr) {
            *status = U_MEMORY_ALLOCATION_ERROR;
            res_close(result);
            return nullptr;
        }
        uprv_strcpy(nameWithoutSuffix, state->filename);
        *uprv_strrchr(nameWithoutSuffix, '.') = 0;

        writeCollationTOML(state->outputdir, nameWithoutSuffix, collationType, t->data, t->settings, status);
        uprv_free(nameWithoutSuffix);
    }
    icu::LocalMemory<uint8_t> buffer;
    int32_t capacity = 100000;
    uint8_t *dest = buffer.allocateInsteadAndCopy(capacity);
    if(dest == nullptr) {
        fprintf(stderr, "memory allocation (%ld bytes) for file contents failed\n",
                static_cast<long>(capacity));
        *status = U_MEMORY_ALLOCATION_ERROR;
        res_close(result);
        return nullptr;
    }
    int32_t indexes[icu::CollationDataReader::IX_TOTAL_SIZE + 1];
    int32_t totalSize = icu::CollationDataWriter::writeTailoring(
            *t, *t->settings, indexes, dest, capacity, intStatus);
    if(intStatus == U_BUFFER_OVERFLOW_ERROR) {
        intStatus = U_ZERO_ERROR;
        capacity = totalSize;
        dest = buffer.allocateInsteadAndCopy(capacity);
        if(dest == nullptr) {
            fprintf(stderr, "memory allocation (%ld bytes) for file contents failed\n",
                    static_cast<long>(capacity));
            *status = U_MEMORY_ALLOCATION_ERROR;
            res_close(result);
            return nullptr;
        }
        totalSize = icu::CollationDataWriter::writeTailoring(
                *t, *t->settings, indexes, dest, capacity, intStatus);
    }
    if(U_FAILURE(intStatus)) {
        fprintf(stderr, "CollationDataWriter::writeTailoring() failed: %s\n",
                u_errorName(intStatus));
        res_close(result);
        return nullptr;
    }
    if(isVerbose()) {
        printf("%s~%s collation tailoring part sizes:\n", state->filename, collationType);
        icu::CollationInfo::printSizes(totalSize, indexes);
        if(t->settings->hasReordering()) {
            printf("%s~%s collation reordering ranges:\n", state->filename, collationType);
            icu::CollationInfo::printReorderRanges(
                    *t->data, t->settings->reorderCodes, t->settings->reorderCodesLength);
        }
#if 0  // debugging output
    } else {
        printf("%s~%s collation tailoring part sizes:\n", state->filename, collationType);
        icu::CollationInfo::printSizes(totalSize, indexes);
#endif
    }
    struct SResource *collationBin = bin_open(state->bundle, "%%CollationBin", totalSize, dest, nullptr, nullptr, status);
    result->add(collationBin, line, *status);
    if (U_FAILURE(*status)) {
        res_close(result);
        return nullptr;
    }
#endif
    return result;
}

static UBool
keepCollationType(const char * /*type*/) {
    return true;
}

static struct SResource *
parseCollationElements(ParseState* state, char *tag, uint32_t startline, UBool newCollation, UErrorCode *status)
{
    TableResource  *result = nullptr;
    struct SResource  *member = nullptr;
    struct UString    *tokenValue;
    struct UString     comment;
    enum   ETokenType  token;
    char               subtag[1024], typeKeyword[1024];
    uint32_t           line;

    result = table_open(state->bundle, tag, nullptr, status);

    if (result == nullptr || U_FAILURE(*status))
    {
        return nullptr;
    }
    if(isVerbose()){
        printf(" collation elements %s at line %i \n", tag == nullptr ? "(null)" : tag, static_cast<int>(startline));
    }
    if(!newCollation) {
        return addCollation(state, result, "(no type)", startline, status);
    }
    else {
        for(;;) {
            ustr_init(&comment);
            token = getToken(state, &tokenValue, &comment, &line, status);

            if (token == TOK_CLOSE_BRACE)
            {
                return result;
            }

            if (token != TOK_STRING)
            {
                res_close(result);
                *status = U_INVALID_FORMAT_ERROR;

                if (token == TOK_EOF)
                {
                    error(startline, "unterminated table");
                }
                else
                {
                    error(line, "Unexpected token %s", tokenNames[token]);
                }

                return nullptr;
            }

            u_UCharsToChars(tokenValue->fChars, subtag, u_strlen(tokenValue->fChars) + 1);

            if (U_FAILURE(*status))
            {
                res_close(result);
                return nullptr;
            }

            if (uprv_strcmp(subtag, "default") == 0)
            {
                member = parseResource(state, subtag, nullptr, status);

                if (U_FAILURE(*status))
                {
                    res_close(result);
                    return nullptr;
                }

                result->add(member, line, *status);
            }
            else
            {
                token = peekToken(state, 0, &tokenValue, &line, &comment, status);
                /* this probably needs to be refactored or recursively use the parser */
                /* first we assume that our collation table won't have the explicit type */
                /* then, we cannot handle aliases */
                if(token == TOK_OPEN_BRACE) {
                    token = getToken(state, &tokenValue, &comment, &line, status);
                    TableResource *collationRes;
                    if (keepCollationType(subtag)) {
                        collationRes = table_open(state->bundle, subtag, nullptr, status);
                    } else {
                        collationRes = nullptr;
                    }
                    // need to parse the collation data regardless
                    collationRes = addCollation(state, collationRes, subtag, startline, status);
                    if (collationRes != nullptr) {
                        result->add(collationRes, startline, *status);
                    }
                } else if(token == TOK_COLON) { /* right now, we'll just try to see if we have aliases */
                    /* we could have a table too */
                    token = peekToken(state, 1, &tokenValue, &line, &comment, status);
                    u_UCharsToChars(tokenValue->fChars, typeKeyword, u_strlen(tokenValue->fChars) + 1);
                    if(uprv_strcmp(typeKeyword, "alias") == 0) {
                        member = parseResource(state, subtag, nullptr, status);
                        if (U_FAILURE(*status))
                        {
                            res_close(result);
                            return nullptr;
                        }

                        result->add(member, line, *status);
                    } else {
                        res_close(result);
                        *status = U_INVALID_FORMAT_ERROR;
                        return nullptr;
                    }
                } else {
                    res_close(result);
                    *status = U_INVALID_FORMAT_ERROR;
                    return nullptr;
                }
            }

            /*member = string_open(bundle, subtag, tokenValue->fChars, tokenValue->fLength, status);*/

            /*expect(TOK_CLOSE_BRACE, nullptr, nullptr, status);*/

            if (U_FAILURE(*status))
            {
                res_close(result);
                return nullptr;
            }
        }
    }
}

/* Necessary, because CollationElements requires the bundle->fRoot member to be present which,
   if this weren't special-cased, wouldn't be set until the entire file had been processed. */
static struct SResource *
realParseTable(ParseState* state, TableResource *table, char *tag, uint32_t startline, UErrorCode *status)
{
    struct SResource  *member = nullptr;
    struct UString    *tokenValue=nullptr;
    struct UString    comment;
    enum   ETokenType token;
    char              subtag[1024];
    uint32_t          line;
    UBool             readToken = false;

    /* '{' . (name resource)* '}' */

    if(isVerbose()){
        printf(" parsing table %s at line %i \n", tag == nullptr ? "(null)" : tag, static_cast<int>(startline));
    }
    for (;;)
    {
        ustr_init(&comment);
        token = getToken(state, &tokenValue, &comment, &line, status);

        if (token == TOK_CLOSE_BRACE)
        {
            if (!readToken && isVerbose()) {
                warning(startline, "Encountered empty table");
            }
            return table;
        }

        if (token != TOK_STRING)
        {
            *status = U_INVALID_FORMAT_ERROR;

            if (token == TOK_EOF)
            {
                error(startline, "unterminated table");
            }
            else
            {
                error(line, "unexpected token %s", tokenNames[token]);
            }

            return nullptr;
        }

        if(uprv_isInvariantUString(tokenValue->fChars, -1)) {
            u_UCharsToChars(tokenValue->fChars, subtag, u_strlen(tokenValue->fChars) + 1);
        } else {
            *status = U_INVALID_FORMAT_ERROR;
            error(line, "invariant characters required for table keys");
            return nullptr;
        }

        if (U_FAILURE(*status))
        {
            error(line, "parse error. Stopped parsing tokens with %s", u_errorName(*status));
            return nullptr;
        }

        member = parseResource(state, subtag, &comment, status);

        if (member == nullptr || U_FAILURE(*status))
        {
            error(line, "parse error. Stopped parsing resource with %s", u_errorName(*status));
            return nullptr;
        }

        table->add(member, line, *status);

        if (U_FAILURE(*status))
        {
            error(line, "parse error. Stopped parsing table with %s", u_errorName(*status));
            return nullptr;
        }
        readToken = true;
        ustr_deinit(&comment);
   }

    /* not reached */
    /* A compiler warning will appear if all paths don't contain a return statement. */
/*     *status = U_INTERNAL_PROGRAM_ERROR;
     return nullptr;*/
}

static struct SResource *
parseTable(ParseState* state, char *tag, uint32_t startline, const struct UString *comment, UErrorCode *status)
{
    if (tag != nullptr && uprv_strcmp(tag, "CollationElements") == 0)
    {
        return parseCollationElements(state, tag, startline, false, status);
    }
    if (tag != nullptr && uprv_strcmp(tag, "collations") == 0)
    {
        return parseCollationElements(state, tag, startline, true, status);
    }
    if(isVerbose()){
        printf(" table %s at line %i \n", tag == nullptr ? "(null)" : tag, static_cast<int>(startline));
    }

    TableResource *result = table_open(state->bundle, tag, comment, status);

    if (result == nullptr || U_FAILURE(*status))
    {
        return nullptr;
    }
    return realParseTable(state, result, tag, startline,  status);
}

static struct SResource *
parseArray(ParseState* state, char *tag, uint32_t startline, const struct UString *comment, UErrorCode *status)
{
    struct SResource  *member = nullptr;
    struct UString    *tokenValue;
    struct UString    memberComments;
    enum   ETokenType token;
    UBool             readToken = false;

    ArrayResource  *result = array_open(state->bundle, tag, comment, status);

    if (result == nullptr || U_FAILURE(*status))
    {
        return nullptr;
    }
    if(isVerbose()){
        printf(" array %s at line %i \n", tag == nullptr ? "(null)" : tag, static_cast<int>(startline));
    }

    ustr_init(&memberComments);

    /* '{' . resource [','] '}' */
    for (;;)
    {
        /* reset length */
        ustr_setlen(&memberComments, 0, status);

        /* check for end of array, but don't consume next token unless it really is the end */
        token = peekToken(state, 0, &tokenValue, nullptr, &memberComments, status);


        if (token == TOK_CLOSE_BRACE)
        {
            getToken(state, nullptr, nullptr, nullptr, status);
            if (!readToken) {
                warning(startline, "Encountered empty array");
            }
            break;
        }

        if (token == TOK_EOF)
        {
            res_close(result);
            *status = U_INVALID_FORMAT_ERROR;
            error(startline, "unterminated array");
            return nullptr;
        }

        /* string arrays are a special case */
        if (token == TOK_STRING)
        {
            getToken(state, &tokenValue, &memberComments, nullptr, status);
            member = string_open(state->bundle, nullptr, tokenValue->fChars, tokenValue->fLength, &memberComments, status);
        }
        else
        {
            member = parseResource(state, nullptr, &memberComments, status);
        }

        if (member == nullptr || U_FAILURE(*status))
        {
            res_close(result);
            return nullptr;
        }

        result->add(member);

        /* eat optional comma if present */
        token = peekToken(state, 0, nullptr, nullptr, nullptr, status);

        if (token == TOK_COMMA)
        {
            getToken(state, nullptr, nullptr, nullptr, status);
        }

        if (U_FAILURE(*status))
        {
            res_close(result);
            return nullptr;
        }
        readToken = true;
    }

    ustr_deinit(&memberComments);
    return result;
}

static struct SResource *
parseIntVector(ParseState* state, char *tag, uint32_t startline, const struct UString *comment, UErrorCode *status)
{
    enum   ETokenType  token;
    char              *string;
    int32_t            value;
    UBool              readToken = false;
    char              *stopstring;
    struct UString     memberComments;

    IntVectorResource *result = intvector_open(state->bundle, tag, comment, status);

    if (result == nullptr || U_FAILURE(*status))
    {
        return nullptr;
    }

    if(isVerbose()){
        printf(" vector %s at line %i \n", tag == nullptr ? "(null)" : tag, static_cast<int>(startline));
    }
    ustr_init(&memberComments);
    /* '{' . string [','] '}' */
    for (;;)
    {
        ustr_setlen(&memberComments, 0, status);

        /* check for end of array, but don't consume next token unless it really is the end */
        token = peekToken(state, 0, nullptr, nullptr,&memberComments, status);

        if (token == TOK_CLOSE_BRACE)
        {
            /* it's the end, consume the close brace */
            getToken(state, nullptr, nullptr, nullptr, status);
            if (!readToken) {
                warning(startline, "Encountered empty int vector");
            }
            ustr_deinit(&memberComments);
            return result;
        }

        int32_t stringLength;
        string = getInvariantString(state, nullptr, nullptr, stringLength, status);

        if (U_FAILURE(*status))
        {
            res_close(result);
            return nullptr;
        }

        /* For handling illegal char in the Intvector */
        value = uprv_strtoul(string, &stopstring, 0);/* make intvector support decimal,hexdigit,octal digit ranging from -2^31-2^32-1*/
        int32_t len = static_cast<int32_t>(stopstring - string);

        if(len==stringLength)
        {
            result->add(value, *status);
            uprv_free(string);
            token = peekToken(state, 0, nullptr, nullptr, nullptr, status);
        }
        else
        {
            uprv_free(string);
            *status=U_INVALID_CHAR_FOUND;
        }

        if (U_FAILURE(*status))
        {
            res_close(result);
            return nullptr;
        }

        /* the comma is optional (even though it is required to prevent the reader from concatenating
        consecutive entries) so that a missing comma on the last entry isn't an error */
        if (token == TOK_COMMA)
        {
            getToken(state, nullptr, nullptr, nullptr, status);
        }
        readToken = true;
    }

    /* not reached */
    /* A compiler warning will appear if all paths don't contain a return statement. */
/*    intvector_close(result, status);
    *status = U_INTERNAL_PROGRAM_ERROR;
    return nullptr;*/
}

static struct SResource *
parseBinary(ParseState* state, char *tag, uint32_t startline, const struct UString *comment, UErrorCode *status)
{
    uint32_t line;
    int32_t stringLength;
    LocalMemory<char> string(getInvariantString(state, &line, nullptr, stringLength, status));
    if (string.isNull() || U_FAILURE(*status))
    {
        return nullptr;
    }

    expect(state, TOK_CLOSE_BRACE, nullptr, nullptr, nullptr, status);
    if (U_FAILURE(*status))
    {
        return nullptr;
    }

    if(isVerbose()){
        printf(" binary %s at line %i \n", tag == nullptr ? "(null)" : tag, static_cast<int>(startline));
    }

    LocalMemory<uint8_t> value;
    int32_t count = 0;
    if (stringLength > 0 && value.allocateInsteadAndCopy(stringLength) == nullptr)
    {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }

    char toConv[3] = {'\0', '\0', '\0'};
    for (int32_t i = 0; i < stringLength;)
    {
        // Skip spaces (which may have been line endings).
        char c0 = string[i++];
        if (c0 == ' ') { continue; }
        if (i == stringLength) {
            *status=U_INVALID_CHAR_FOUND;
            error(line, "Encountered invalid binary value (odd number of hex digits)");
            return nullptr;
        }
        toConv[0] = c0;
        toConv[1] = string[i++];

        char *stopstring;
        value[count++] = static_cast<uint8_t>(uprv_strtoul(toConv, &stopstring, 16));
        uint32_t len = static_cast<uint32_t>(stopstring - toConv);

        if(len!=2)
        {
            *status=U_INVALID_CHAR_FOUND;
            error(line, "Encountered invalid binary value (not all pairs of hex digits)");
            return nullptr;
        }
    }

    if (count == 0) {
        warning(startline, "Encountered empty binary value");
        return bin_open(state->bundle, tag, 0, nullptr, "", comment, status);
    } else {
        return bin_open(state->bundle, tag, count, value.getAlias(), nullptr, comment, status);
    }
}

static struct SResource *
parseInteger(ParseState* state, char *tag, uint32_t startline, const struct UString *comment, UErrorCode *status)
{
    struct SResource *result = nullptr;
    int32_t           value;
    char             *string;
    char             *stopstring;

    int32_t stringLength;
    string = getInvariantString(state, nullptr, nullptr, stringLength, status);

    if (string == nullptr || U_FAILURE(*status))
    {
        return nullptr;
    }

    expect(state, TOK_CLOSE_BRACE, nullptr, nullptr, nullptr, status);

    if (U_FAILURE(*status))
    {
        uprv_free(string);
        return nullptr;
    }

    if(isVerbose()){
        printf(" integer %s at line %i \n", tag == nullptr ? "(null)" : tag, static_cast<int>(startline));
    }

    if (stringLength == 0)
    {
        warning(startline, "Encountered empty integer. Default value is 0.");
    }

    /* Allow integer support for hexdecimal, octal digit and decimal*/
    /* and handle illegal char in the integer*/
    value = uprv_strtoul(string, &stopstring, 0);
    int32_t len = static_cast<int32_t>(stopstring - string);
    if(len==stringLength)
    {
        result = int_open(state->bundle, tag, value, comment, status);
    }
    else
    {
        *status=U_INVALID_CHAR_FOUND;
    }
    uprv_free(string);

    return result;
}

static struct SResource *
parseImport(ParseState* state, char *tag, uint32_t startline, const struct UString* comment, UErrorCode *status)
{
    uint32_t          line;
    int32_t stringLength;
    LocalMemory<char> filename(getInvariantString(state, &line, nullptr, stringLength, status));
    if (U_FAILURE(*status))
    {
        return nullptr;
    }

    expect(state, TOK_CLOSE_BRACE, nullptr, nullptr, nullptr, status);

    if (U_FAILURE(*status))
    {
        return nullptr;
    }

    if(isVerbose()){
        printf(" import %s at line %i \n", tag == nullptr ? "(null)" : tag, static_cast<int>(startline));
    }

    /* Open the input file for reading */
    CharString fullname;
    if (state->inputdir != nullptr) {
        fullname.append(state->inputdir, *status);
    }
    fullname.appendPathPart(filename.getAlias(), *status);
    if (U_FAILURE(*status)) {
        return nullptr;
    }

    FileStream *file = T_FileStream_open(fullname.data(), "rb");
    if (file == nullptr)
    {
        error(line, "couldn't open input file %s", filename.getAlias());
        *status = U_FILE_ACCESS_ERROR;
        return nullptr;
    }

    int32_t len  = T_FileStream_size(file);
    LocalMemory<uint8_t> data;
    if(data.allocateInsteadAndCopy(len) == nullptr)
    {
        *status = U_MEMORY_ALLOCATION_ERROR;
        T_FileStream_close (file);
        return nullptr;
    }

    /* int32_t numRead = */ T_FileStream_read(file, data.getAlias(), len);
    T_FileStream_close (file);

    return bin_open(state->bundle, tag, len, data.getAlias(), fullname.data(), comment, status);
}

static struct SResource *
parseInclude(ParseState* state, char *tag, uint32_t startline, const struct UString* comment, UErrorCode *status)
{
    struct SResource *result;
    int32_t           len=0;
    char             *filename;
    uint32_t          line;
    char16_t *pTarget     = nullptr;

    UCHARBUF *ucbuf;
    char     *fullname = nullptr;
    const char* cp = nullptr;
    const char16_t* uBuffer = nullptr;

    int32_t stringLength;
    filename = getInvariantString(state, &line, nullptr, stringLength, status);

    if (U_FAILURE(*status))
    {
        return nullptr;
    }

    expect(state, TOK_CLOSE_BRACE, nullptr, nullptr, nullptr, status);

    if (U_FAILURE(*status))
    {
        uprv_free(filename);
        return nullptr;
    }

    if(isVerbose()){
        printf(" include %s at line %i \n", tag == nullptr ? "(null)" : tag, static_cast<int>(startline));
    }

    fullname = static_cast<char*>(uprv_malloc(state->inputdirLength + stringLength + 2));
    /* test for nullptr */
    if(fullname == nullptr)
    {
        *status = U_MEMORY_ALLOCATION_ERROR;
        uprv_free(filename);
        return nullptr;
    }

    if(state->inputdir!=nullptr){
        if (state->inputdir[state->inputdirLength - 1] != U_FILE_SEP_CHAR)
        {

            uprv_strcpy(fullname, state->inputdir);

            fullname[state->inputdirLength]      = U_FILE_SEP_CHAR;
            fullname[state->inputdirLength + 1] = '\0';

            uprv_strcat(fullname, filename);
        }
        else
        {
            uprv_strcpy(fullname, state->inputdir);
            uprv_strcat(fullname, filename);
        }
    }else{
        uprv_strcpy(fullname,filename);
    }

    ucbuf = ucbuf_open(fullname, &cp,getShowWarning(),false,status);

    if (U_FAILURE(*status)) {
        error(line, "couldn't open input file %s\n", filename);
        return nullptr;
    }

    uBuffer = ucbuf_getBuffer(ucbuf,&len,status);
    result = string_open(state->bundle, tag, uBuffer, len, comment, status);

    ucbuf_close(ucbuf);

    uprv_free(pTarget);

    uprv_free(filename);
    uprv_free(fullname);

    return result;
}





U_STRING_DECL(k_type_string,    "string",    6);
U_STRING_DECL(k_type_binary,    "binary",    6);
U_STRING_DECL(k_type_bin,       "bin",       3);
U_STRING_DECL(k_type_table,     "table",     5);
U_STRING_DECL(k_type_table_no_fallback,     "table(nofallback)",         17);
U_STRING_DECL(k_type_int,       "int",       3);
U_STRING_DECL(k_type_integer,   "integer",   7);
U_STRING_DECL(k_type_array,     "array",     5);
U_STRING_DECL(k_type_alias,     "alias",     5);
U_STRING_DECL(k_type_intvector, "intvector", 9);
U_STRING_DECL(k_type_import,    "import",    6);
U_STRING_DECL(k_type_include,   "include",   7);

/* Various non-standard processing plugins that create one or more special resources. */
U_STRING_DECL(k_type_plugin_uca_rules,      "process(uca_rules)",        18);
U_STRING_DECL(k_type_plugin_collation,      "process(collation)",        18);
U_STRING_DECL(k_type_plugin_transliterator, "process(transliterator)",   23);
U_STRING_DECL(k_type_plugin_dependency,     "process(dependency)",       19);

typedef enum EResourceType
{
    RESTYPE_UNKNOWN,
    RESTYPE_STRING,
    RESTYPE_BINARY,
    RESTYPE_TABLE,
    RESTYPE_TABLE_NO_FALLBACK,
    RESTYPE_INTEGER,
    RESTYPE_ARRAY,
    RESTYPE_ALIAS,
    RESTYPE_INTVECTOR,
    RESTYPE_IMPORT,
    RESTYPE_INCLUDE,
    RESTYPE_PROCESS_UCA_RULES,
    RESTYPE_PROCESS_COLLATION,
    RESTYPE_PROCESS_TRANSLITERATOR,
    RESTYPE_PROCESS_DEPENDENCY,
    RESTYPE_RESERVED
} EResourceType;

static struct {
    const char *nameChars;   /* only used for debugging */
    const char16_t *nameUChars;
    ParseResourceFunction *parseFunction;
} gResourceTypes[] = {
    {"Unknown", nullptr, nullptr},
    {"string", k_type_string, parseString},
    {"binary", k_type_binary, parseBinary},
    {"table", k_type_table, parseTable},
    {"table(nofallback)", k_type_table_no_fallback, nullptr}, /* parseFunction will never be called */
    {"integer", k_type_integer, parseInteger},
    {"array", k_type_array, parseArray},
    {"alias", k_type_alias, parseAlias},
    {"intvector", k_type_intvector, parseIntVector},
    {"import", k_type_import, parseImport},
    {"include", k_type_include, parseInclude},
    {"process(uca_rules)", k_type_plugin_uca_rules, parseUCARules},
    {"process(collation)", k_type_plugin_collation, nullptr /* not implemented yet */},
    {"process(transliterator)", k_type_plugin_transliterator, parseTransliterator},
    {"process(dependency)", k_type_plugin_dependency, parseDependency},
    {"reserved", nullptr, nullptr}
};

void initParser()
{
    U_STRING_INIT(k_type_string,    "string",    6);
    U_STRING_INIT(k_type_binary,    "binary",    6);
    U_STRING_INIT(k_type_bin,       "bin",       3);
    U_STRING_INIT(k_type_table,     "table",     5);
    U_STRING_INIT(k_type_table_no_fallback,     "table(nofallback)",         17);
    U_STRING_INIT(k_type_int,       "int",       3);
    U_STRING_INIT(k_type_integer,   "integer",   7);
    U_STRING_INIT(k_type_array,     "array",     5);
    U_STRING_INIT(k_type_alias,     "alias",     5);
    U_STRING_INIT(k_type_intvector, "intvector", 9);
    U_STRING_INIT(k_type_import,    "import",    6);
    U_STRING_INIT(k_type_include,   "include",   7);

    U_STRING_INIT(k_type_plugin_uca_rules,      "process(uca_rules)",        18);
    U_STRING_INIT(k_type_plugin_collation,      "process(collation)",        18);
    U_STRING_INIT(k_type_plugin_transliterator, "process(transliterator)",   23);
    U_STRING_INIT(k_type_plugin_dependency,     "process(dependency)",       19);
}

static inline UBool isTable(enum EResourceType type) {
    return type == RESTYPE_TABLE || type == RESTYPE_TABLE_NO_FALLBACK;
}

static enum EResourceType
parseResourceType(ParseState* state, UErrorCode *status)
{
    struct UString        *tokenValue;
    struct UString        comment;
    enum   EResourceType  result = RESTYPE_UNKNOWN;
    uint32_t              line=0;
    ustr_init(&comment);
    expect(state, TOK_STRING, &tokenValue, &comment, &line, status);

    if (U_FAILURE(*status))
    {
        return RESTYPE_UNKNOWN;
    }

    *status = U_ZERO_ERROR;

    /* Search for normal types */
    result=RESTYPE_UNKNOWN;
    while ((result = static_cast<EResourceType>(result + 1)) < RESTYPE_RESERVED) {
        if (u_strcmp(tokenValue->fChars, gResourceTypes[result].nameUChars) == 0) {
            break;
        }
    }
    /* Now search for the aliases */
    if (u_strcmp(tokenValue->fChars, k_type_int) == 0) {
        result = RESTYPE_INTEGER;
    }
    else if (u_strcmp(tokenValue->fChars, k_type_bin) == 0) {
        result = RESTYPE_BINARY;
    }
    else if (result == RESTYPE_RESERVED) {
        char tokenBuffer[1024];
        u_austrncpy(tokenBuffer, tokenValue->fChars, sizeof(tokenBuffer));
        tokenBuffer[sizeof(tokenBuffer) - 1] = 0;
        *status = U_INVALID_FORMAT_ERROR;
        error(line, "unknown resource type '%s'", tokenBuffer);
    }

    return result;
}

/* parse a non-top-level resource */
static struct SResource *
parseResource(ParseState* state, char *tag, const struct UString *comment, UErrorCode *status)
{
    enum   ETokenType      token;
    enum   EResourceType  resType = RESTYPE_UNKNOWN;
    ParseResourceFunction *parseFunction = nullptr;
    struct UString        *tokenValue;
    uint32_t                 startline;
    uint32_t                 line;


    token = getToken(state, &tokenValue, nullptr, &startline, status);

    if(isVerbose()){
        printf(" resource %s at line %i \n", tag == nullptr ? "(null)" : tag, static_cast<int>(startline));
    }

    /* name . [ ':' type ] '{' resource '}' */
    /* This function parses from the colon onwards.  If the colon is present, parse the
    type then try to parse a resource of that type.  If there is no explicit type,
    work it out using the lookahead tokens. */
    switch (token)
    {
    case TOK_EOF:
        *status = U_INVALID_FORMAT_ERROR;
        error(startline, "Unexpected EOF encountered");
        return nullptr;

    case TOK_ERROR:
        *status = U_INVALID_FORMAT_ERROR;
        return nullptr;

    case TOK_COLON:
        resType = parseResourceType(state, status);
        expect(state, TOK_OPEN_BRACE, &tokenValue, nullptr, &startline, status);

        if (U_FAILURE(*status))
        {
            return nullptr;
        }

        break;

    case TOK_OPEN_BRACE:
        break;

    default:
        *status = U_INVALID_FORMAT_ERROR;
        error(startline, "syntax error while reading a resource, expected '{' or ':'");
        return nullptr;
    }


    if (resType == RESTYPE_UNKNOWN)
    {
        /* No explicit type, so try to work it out.  At this point, we've read the first '{'.
        We could have any of the following:
        { {         => array (nested)
        { :/}       => array
        { string ,  => string array

        { string {  => table

        { string :/{    => table
        { string }      => string
        */

        token = peekToken(state, 0, nullptr, &line, nullptr,status);

        if (U_FAILURE(*status))
        {
            return nullptr;
        }

        if (token == TOK_OPEN_BRACE || token == TOK_COLON ||token ==TOK_CLOSE_BRACE )
        {
            resType = RESTYPE_ARRAY;
        }
        else if (token == TOK_STRING)
        {
            token = peekToken(state, 1, nullptr, &line, nullptr, status);

            if (U_FAILURE(*status))
            {
                return nullptr;
            }

            switch (token)
            {
            case TOK_COMMA:         resType = RESTYPE_ARRAY;  break;
            case TOK_OPEN_BRACE:    resType = RESTYPE_TABLE;  break;
            case TOK_CLOSE_BRACE:   resType = RESTYPE_STRING; break;
            case TOK_COLON:         resType = RESTYPE_TABLE;  break;
            default:
                *status = U_INVALID_FORMAT_ERROR;
                error(line, "Unexpected token after string, expected ',', '{' or '}'");
                return nullptr;
            }
        }
        else
        {
            *status = U_INVALID_FORMAT_ERROR;
            error(line, "Unexpected token after '{'");
            return nullptr;
        }

        /* printf("Type guessed as %s\n", resourceNames[resType]); */
    } else if(resType == RESTYPE_TABLE_NO_FALLBACK) {
        *status = U_INVALID_FORMAT_ERROR;
        error(startline, "error: %s resource type not valid except on top bundle level", gResourceTypes[resType].nameChars);
        return nullptr;
    }


    /* We should now know what we need to parse next, so call the appropriate parser
    function and return. */
    parseFunction = gResourceTypes[resType].parseFunction;
    if (parseFunction != nullptr) {
        return parseFunction(state, tag, startline, comment, status);
    }
    else {
        *status = U_INTERNAL_PROGRAM_ERROR;
        error(startline, "internal error: %s resource type found and not handled", gResourceTypes[resType].nameChars);
    }

    return nullptr;
}

/* parse the top-level resource */
struct SRBRoot *
parse(UCHARBUF *buf, const char *inputDir, const char *outputDir, const char *filename,
      UBool makeBinaryCollation, UBool omitCollationRules, UBool icu4xMode, UErrorCode *status)
{
    struct UString    *tokenValue;
    struct UString    comment;
    uint32_t           line;
    enum EResourceType bundleType;
    enum ETokenType    token;
    ParseState state;
    uint32_t i;


    for (i = 0; i < MAX_LOOKAHEAD + 1; i++)
    {
        ustr_init(&state.lookahead[i].value);
        ustr_init(&state.lookahead[i].comment);
    }

    initLookahead(&state, buf, status);

    state.inputdir       = inputDir;
    state.inputdirLength = state.inputdir != nullptr ? static_cast<uint32_t>(uprv_strlen(state.inputdir)) : 0;
    state.outputdir       = outputDir;
    state.outputdirLength = state.outputdir != nullptr ? static_cast<uint32_t>(uprv_strlen(state.outputdir)) : 0;
    state.filename = filename;
    state.makeBinaryCollation = makeBinaryCollation;
    state.omitCollationRules = omitCollationRules;
    state.icu4xMode = icu4xMode;

    ustr_init(&comment);
    expect(&state, TOK_STRING, &tokenValue, &comment, nullptr, status);

    state.bundle = new SRBRoot(&comment, false, *status);

    if (state.bundle == nullptr || U_FAILURE(*status))
    {
        delete state.bundle;

        return nullptr;
    }


    state.bundle->setLocale(tokenValue->fChars, *status);

    /* The following code is to make Empty bundle work no matter with :table specifer or not */
    token = getToken(&state, nullptr, nullptr, &line, status);
    if(token==TOK_COLON) {
        *status=U_ZERO_ERROR;
        bundleType=parseResourceType(&state, status);

        if(isTable(bundleType))
        {
            expect(&state, TOK_OPEN_BRACE, nullptr, nullptr, &line, status);
        }
        else
        {
            *status=U_PARSE_ERROR;
             error(line, "parse error. Stopped parsing with %s", u_errorName(*status));
        }
    }
    else
    {
        /* not a colon */
        if(token==TOK_OPEN_BRACE)
        {
            *status=U_ZERO_ERROR;
            bundleType=RESTYPE_TABLE;
        }
        else
        {
            /* neither colon nor open brace */
            *status=U_PARSE_ERROR;
            bundleType=RESTYPE_UNKNOWN;
            error(line, "parse error, did not find open-brace '{' or colon ':', stopped with %s", u_errorName(*status));
        }
    }

    if (U_FAILURE(*status))
    {
        delete state.bundle;
        return nullptr;
    }

    if(bundleType==RESTYPE_TABLE_NO_FALLBACK) {
        /*
         * Parse a top-level table with the table(nofallback) declaration.
         * This is the same as a regular table, but also sets the
         * URES_ATT_NO_FALLBACK flag in indexes[URES_INDEX_ATTRIBUTES] .
         */
        state.bundle->fNoFallback=true;
    }
    /* top-level tables need not handle special table names like "collations" */
    assert(!state.bundle->fIsPoolBundle);
    assert(state.bundle->fRoot->fType == URES_TABLE);
    TableResource *rootTable = static_cast<TableResource *>(state.bundle->fRoot);
    realParseTable(&state, rootTable, nullptr, line, status);
    if(dependencyArray!=nullptr){
        rootTable->add(dependencyArray, 0, *status);
        dependencyArray = nullptr;
    }
   if (U_FAILURE(*status))
    {
        delete state.bundle;
        res_close(dependencyArray);
        return nullptr;
    }

    if (getToken(&state, nullptr, nullptr, &line, status) != TOK_EOF)
    {
        warning(line, "extraneous text after resource bundle (perhaps unmatched braces)");
        if(isStrict()){
            *status = U_INVALID_FORMAT_ERROR;
            return nullptr;
        }
    }

    cleanupLookahead(&state);
    ustr_deinit(&comment);
    return state.bundle;
}

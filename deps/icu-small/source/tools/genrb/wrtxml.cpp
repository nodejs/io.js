// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 2002-2015, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*
* File wrtxml.cpp
*
* Modification History:
*
*   Date        Name        Description
*   10/01/02    Ram         Creation.
*   02/07/08    Spieth      Correct XLIFF generation on EBCDIC platform
*
*******************************************************************************
*/

// Safer use of UnicodeString.
#ifndef UNISTR_FROM_CHAR_EXPLICIT
#   define UNISTR_FROM_CHAR_EXPLICIT explicit
#endif

// Less important, but still a good idea.
#ifndef UNISTR_FROM_STRING_EXPLICIT
#   define UNISTR_FROM_STRING_EXPLICIT explicit
#endif

#include "reslist.h"
#include "unewdata.h"
#include "unicode/ures.h"
#include "errmsg.h"
#include "filestrm.h"
#include "cstring.h"
#include "unicode/ucnv.h"
#include "genrb.h"
#include "rle.h"
#include "uhash.h"
#include "uresimp.h"
#include "unicode/ustring.h"
#include "unicode/uchar.h"
#include "ustr.h"
#include "prscmnts.h"
#include "unicode/unistr.h"
#include "unicode/utf8.h"
#include "unicode/utf16.h"
#include <time.h>

U_NAMESPACE_USE

static int tabCount = 0;

static FileStream* out=nullptr;
static struct SRBRoot* srBundle ;
static const char* outDir = nullptr;
static const char* enc ="";
static UConverter* conv = nullptr;

const char* const* ISOLanguages;
const char* const* ISOCountries;
const char* textExt = ".txt";
const char* xliffExt = ".xlf";

static int32_t write_utf8_file(FileStream* fileStream, UnicodeString outString)
{
    UErrorCode status = U_ZERO_ERROR;
    int32_t len = 0;

    // preflight to get the destination buffer size
    u_strToUTF8(nullptr,
                0,
                &len,
                toUCharPtr(outString.getBuffer()),
                outString.length(),
                &status);

    // allocate the buffer
    char* dest = static_cast<char*>(uprv_malloc(len));
    status = U_ZERO_ERROR;

    // convert the data
    u_strToUTF8(dest,
                len,
                &len,
                toUCharPtr(outString.getBuffer()),
                outString.length(),
                &status);

    // write data to out file
    int32_t ret = T_FileStream_write(fileStream, dest, len);
    uprv_free(dest);
    return (ret);
}

/*write indentation for formatting*/
static void write_tabs(FileStream* os){
    int i=0;
    for(;i<=tabCount;i++){
        write_utf8_file(os,UnicodeString("    "));
    }
}

/*get ID for each element. ID is globally unique.*/
static char* getID(const char* id, const char* curKey, char* result) {
    if(curKey == nullptr) {
        result = static_cast<char*>(uprv_malloc(sizeof(char) * uprv_strlen(id) + 1));
        uprv_memset(result, 0, sizeof(char)*uprv_strlen(id) + 1);
        uprv_strcpy(result, id);
    } else {
        result = static_cast<char*>(uprv_malloc(sizeof(char) * (uprv_strlen(id) + 1 + uprv_strlen(curKey)) + 1));
        uprv_memset(result, 0, sizeof(char)*(uprv_strlen(id) + 1 + uprv_strlen(curKey)) + 1);
        if(id[0]!='\0'){
            uprv_strcpy(result, id);
            uprv_strcat(result, "_");
        }
        uprv_strcat(result, curKey);
    }
    return result;
}

/*compute CRC for binary code*/
/* The code is from  http://www.theorem.com/java/CRC32.java
 * Calculates the CRC32 - 32 bit Cyclical Redundancy Check
 * <P> This check is used in numerous systems to verify the integrity
 * of information.  It's also used as a hashing function.  Unlike a regular
 * checksum, it's sensitive to the order of the characters.
 * It produces a 32 bit
 *
 * @author Michael Lecuyer (mjl@theorem.com)
 * @version 1.1 August 11, 1998
 */

/* ICU is not endian portable, because ICU data generated on big endian machines can be
 * ported to big endian machines but not to little endian machines and vice versa. The
 * conversion is not portable across platforms with different endianness.
 */

uint32_t computeCRC(const char *ptr, uint32_t len, uint32_t lastcrc){
    int32_t crc;
    uint32_t temp1;
    uint32_t temp2;

    int32_t crc_ta[256];
    int i = 0;
    int j = 0;
    uint32_t crc2 = 0;

#define CRC32_POLYNOMIAL 0xEDB88320

    /*build crc table*/
    for (i = 0; i <= 255; i++) {
        crc2 = i;
        for (j = 8; j > 0; j--) {
            if ((crc2 & 1) == 1) {
                crc2 = (crc2 >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                crc2 >>= 1;
            }
        }
        crc_ta[i] = crc2;
    }

    crc = lastcrc;
    while(len--!=0) {
        temp1 = static_cast<uint32_t>(crc) >> 8;
        temp2 = crc_ta[(crc^*ptr) & 0xFF];
        crc = temp1^temp2;
        ptr++;
    }
    return(crc);
}

static void strnrepchr(char* src, int32_t srcLen, char s, char r){
    int32_t i = 0;
    for(i=0;i<srcLen;i++){
        if(src[i]==s){
            src[i]=r;
        }
    }
}
/* Parse the filename, and get its language information.
 * If it fails to get the language information from the filename,
 * use "en" as the default value for language
 */
static char* parseFilename(const char* id, char* /*lang*/) {
    int idLen = static_cast<int>(uprv_strlen(id));
    char* localeID = static_cast<char*>(uprv_malloc(idLen+1));
    int pos = 0;
    int canonCapacity = 0;
    char* canon = nullptr;
    int canonLen = 0;
    /*int i;*/
    UErrorCode status = U_ZERO_ERROR;
    const char *ext = uprv_strchr(id, '.');

    if(ext != nullptr){
        pos = static_cast<int>(ext - id);
    } else {
        pos = idLen;
    }
    uprv_memcpy(localeID, id, pos);
    localeID[pos]=0; /* NUL terminate the string */

    canonCapacity =pos*3;
    canon = static_cast<char*>(uprv_malloc(canonCapacity));
    canonLen = uloc_canonicalize(localeID, canon, canonCapacity, &status);

    if(U_FAILURE(status)){
        fprintf(stderr, "Could not canonicalize the locale ID: %s. Error: %s\n", localeID, u_errorName(status));
        exit(status);
    }
    strnrepchr(canon, canonLen, '_', '-');
    return canon;
}

static const char* xmlHeader = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
#if 0
static const char* bundleStart = "<xliff version = \"1.2\" "
                                        "xmlns='urn:oasis:names:tc:xliff:document:1.2' "
                                        "xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
                                        "xsi:schemaLocation='urn:oasis:names:tc:xliff:document:1.2 xliff-core-1.2-transitional.xsd'>\n";
#else
static const char* bundleStart = "<xliff version = \"1.1\" "
                                        "xmlns='urn:oasis:names:tc:xliff:document:1.1' "
                                        "xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
                                        "xsi:schemaLocation='urn:oasis:names:tc:xliff:document:1.1 http://www.oasis-open.org/committees/xliff/documents/xliff-core-1.1.xsd'>\n";
#endif
static const char* bundleEnd   = "</xliff>\n";

void res_write_xml(struct SResource *res, const char* id, const char* language, UBool isTopLevel, UErrorCode *status);

static char* convertAndEscape(char** pDest, int32_t destCap, int32_t* destLength,
                              const char16_t* src, int32_t srcLen, UErrorCode* status){
    int32_t srcIndex=0;
    char* dest=nullptr;
    char* temp=nullptr;
    int32_t destLen=0;
    UChar32 c = 0;

    if(status==nullptr || U_FAILURE(*status) || pDest==nullptr  || srcLen==0 || src == nullptr){
        return nullptr;
    }
    dest =*pDest;
    if(dest==nullptr || destCap <=0){
        destCap = srcLen * 8;
        dest = static_cast<char*>(uprv_malloc(sizeof(char) * destCap));
        if(dest==nullptr){
            *status=U_MEMORY_ALLOCATION_ERROR;
            return nullptr;
        }
    }

    dest[0]=0;

    while(srcIndex<srcLen){
        U16_NEXT(src, srcIndex, srcLen, c);

        if (U16_IS_LEAD(c) || U16_IS_TRAIL(c)) {
            *status = U_ILLEGAL_CHAR_FOUND;
            fprintf(stderr, "Illegal Surrogate! \n");
            uprv_free(dest);
            return nullptr;
        }

        if((destLen+U8_LENGTH(c)) < destCap){

            /* ASCII Range */
            if(c <=0x007F){
                switch(c) {
                case '\x26':
                    uprv_strcpy(dest+( destLen),"\x26\x61\x6d\x70\x3b"); /* &amp;*/
                    destLen += static_cast<int32_t>(uprv_strlen("\x26\x61\x6d\x70\x3b"));
                    break;
                case '\x3c':
                    uprv_strcpy(dest+(destLen),"\x26\x6c\x74\x3b"); /* &lt;*/
                    destLen += static_cast<int32_t>(uprv_strlen("\x26\x6c\x74\x3b"));
                    break;
                case '\x3e':
                    uprv_strcpy(dest+(destLen),"\x26\x67\x74\x3b"); /* &gt;*/
                    destLen += static_cast<int32_t>(uprv_strlen("\x26\x67\x74\x3b"));
                    break;
                case '\x22':
                    uprv_strcpy(dest+(destLen),"\x26\x71\x75\x6f\x74\x3b"); /* &quot;*/
                    destLen += static_cast<int32_t>(uprv_strlen("\x26\x71\x75\x6f\x74\x3b"));
                    break;
                case '\x27':
                    uprv_strcpy(dest+(destLen),"\x26\x61\x70\x6f\x73\x3b"); /* &apos; */
                    destLen += static_cast<int32_t>(uprv_strlen("\x26\x61\x70\x6f\x73\x3b"));
                    break;

                 /* Disallow C0 controls except TAB, CR, LF*/
                case 0x00:
                case 0x01:
                case 0x02:
                case 0x03:
                case 0x04:
                case 0x05:
                case 0x06:
                case 0x07:
                case 0x08:
                /*case 0x09:*/
                /*case 0x0A: */
                case 0x0B:
                case 0x0C:
                /*case 0x0D:*/
                case 0x0E:
                case 0x0F:
                case 0x10:
                case 0x11:
                case 0x12:
                case 0x13:
                case 0x14:
                case 0x15:
                case 0x16:
                case 0x17:
                case 0x18:
                case 0x19:
                case 0x1A:
                case 0x1B:
                case 0x1C:
                case 0x1D:
                case 0x1E:
                case 0x1F:
                    *status = U_ILLEGAL_CHAR_FOUND;
                    fprintf(stderr, "Illegal Character \\u%04X!\n", static_cast<int>(c));
                    uprv_free(dest);
                    return nullptr;
                default:
                    dest[destLen++] = static_cast<char>(c);
                }
            }else{
                UBool isError = false;
                U8_APPEND((unsigned char*)dest,destLen,destCap,c,isError);
                if(isError){
                    *status = U_ILLEGAL_CHAR_FOUND;
                    fprintf(stderr, "Illegal Character \\U%08X!\n", static_cast<int>(c));
                    uprv_free(dest);
                    return nullptr;
                }
            }
        }else{
            destCap += destLen;

            temp = static_cast<char*>(uprv_malloc(sizeof(char) * destCap));
            if(temp==nullptr){
                *status=U_MEMORY_ALLOCATION_ERROR;
                uprv_free(dest);
                return nullptr;
            }
            uprv_memmove(temp,dest,destLen);
            destLen=0;
            uprv_free(dest);
            dest=temp;
            temp=nullptr;
        }

    }
    *destLength = destLen;
    return dest;
}

#define ASTERISK 0x002A
#define SPACE    0x0020
#define CR       0x000A
#define LF       0x000D
#define AT_SIGN  0x0040

#if UCONFIG_NO_REGULAR_EXPRESSIONS==0
static void
trim(char **src, int32_t *len){

    char *s = nullptr;
    int32_t i = 0;
    if(src == nullptr || *src == nullptr){
        return;
    }
    s = *src;
    /* trim from the end */
    for( i=(*len-1); i>= 0; i--){
        switch(s[i]){
        case ASTERISK:
        case SPACE:
        case CR:
        case LF:
            s[i] = 0;
            continue;
        default:
            break;
        }
        break;

    }
    *len = i+1;
}

static void
print(char16_t* src, int32_t srcLen,const char *tagStart,const char *tagEnd,  UErrorCode *status){
    int32_t bufCapacity   = srcLen*4;
    char *buf       = nullptr;
    int32_t bufLen = 0;

    if(U_FAILURE(*status)){
        return;
    }

    buf = static_cast<char*>(uprv_malloc(bufCapacity));
    if (buf == nullptr) {
        fprintf(stderr, "Could not allocate memory!!");
        exit(U_MEMORY_ALLOCATION_ERROR);
    }
    buf = convertAndEscape(&buf, bufCapacity, &bufLen, src, srcLen,status);
    if(U_SUCCESS(*status)){
        trim(&buf,&bufLen);
        write_utf8_file(out,UnicodeString(tagStart));
        write_utf8_file(out,UnicodeString(buf, bufLen, "UTF-8"));
        write_utf8_file(out,UnicodeString(tagEnd));
        write_utf8_file(out,UnicodeString("\n"));

    }
}
#endif

static void
printNoteElements(const UString *src, UErrorCode *status){

#if UCONFIG_NO_REGULAR_EXPRESSIONS==0 /* donot compile when no RegularExpressions are available */

    int32_t capacity = 0;
    char16_t* note = nullptr;
    int32_t noteLen = 0;
    int32_t count = 0,i;

    if(src == nullptr){
        return;
    }

    capacity = src->fLength;
    note = static_cast<char16_t*>(uprv_malloc(U_SIZEOF_UCHAR * capacity));

    count = getCount(src->fChars,src->fLength, UPC_NOTE, status);
    if(U_FAILURE(*status)){
        uprv_free(note);
        return;
    }
    for(i=0; i < count; i++){
        noteLen =  getAt(src->fChars,src->fLength, &note, capacity, i, UPC_NOTE, status);
        if(U_FAILURE(*status)){
            uprv_free(note);
            return;
        }
        if(noteLen > 0){
            write_tabs(out);
            print(note, noteLen,"<note>", "</note>", status);
        }
    }
    uprv_free(note);
#else

    fprintf(stderr, "Warning: Could not output comments to XLIFF file. ICU has been built without RegularExpression support.\n");

#endif /* UCONFIG_NO_REGULAR_EXPRESSIONS */

}

static void printAttribute(const char *name, const char *value, int32_t /*len*/)
{
    write_utf8_file(out, UnicodeString(" "));
    write_utf8_file(out, UnicodeString(name));
    write_utf8_file(out, UnicodeString(" = \""));
    write_utf8_file(out, UnicodeString(value));
    write_utf8_file(out, UnicodeString("\""));
}

#if UCONFIG_NO_REGULAR_EXPRESSIONS==0 /* donot compile when no RegularExpressions are available */
static void printAttribute(const char *name, const UnicodeString value, int32_t /*len*/)
{
    write_utf8_file(out, UnicodeString(" "));
    write_utf8_file(out, UnicodeString(name));
    write_utf8_file(out, UnicodeString(" = \""));
    write_utf8_file(out, value);
    write_utf8_file(out, UnicodeString("\""));
}
#endif

static void
printComments(struct UString *src, const char *resName, UBool printTranslate, UErrorCode *status){

#if UCONFIG_NO_REGULAR_EXPRESSIONS==0 /* donot compile when no RegularExpressions are available */

    if(status==nullptr || U_FAILURE(*status)){
        return;
    }

    int32_t capacity = src->fLength + 1;
    char* buf = nullptr;
    int32_t bufLen = 0;
    char16_t* desc = static_cast<char16_t*>(uprv_malloc(U_SIZEOF_UCHAR * capacity));
    char16_t* trans = static_cast<char16_t*>(uprv_malloc(U_SIZEOF_UCHAR * capacity));

    int32_t descLen = 0, transLen=0;
    if(desc==nullptr || trans==nullptr){
        *status = U_MEMORY_ALLOCATION_ERROR;
        uprv_free(desc);
        uprv_free(trans);
        return;
    }
    // TODO: make src const, stop modifying it in-place, make printContainer() take const resource, etc.
    src->fLength = removeCmtText(src->fChars, src->fLength, status);
    descLen  = getDescription(src->fChars,src->fLength, &desc, capacity, status);
    transLen = getTranslate(src->fChars,src->fLength, &trans, capacity, status);

    /* first print translate attribute */
    if(transLen > 0){
        if(printTranslate){
            /* print translate attribute */
            buf = convertAndEscape(&buf, 0, &bufLen, trans, transLen, status);
            if(U_SUCCESS(*status)){
                printAttribute("translate", UnicodeString(buf, bufLen, "UTF-8"), bufLen);
                write_utf8_file(out,UnicodeString(">\n"));
            }
        }else if(getShowWarning()){
            fprintf(stderr, "Warning: Translate attribute for resource %s cannot be set. XLIFF prohibits it.\n", resName);
            /* no translate attribute .. just close the tag */
            write_utf8_file(out,UnicodeString(">\n"));
        }
    }else{
        /* no translate attribute .. just close the tag */
        write_utf8_file(out,UnicodeString(">\n"));
    }

    if(descLen > 0){
        write_tabs(out);
        print(desc, descLen, "<!--", "-->", status);
    }

    uprv_free(desc);
    uprv_free(trans);
#else

    fprintf(stderr, "Warning: Could not output comments to XLIFF file. ICU has been built without RegularExpression support.\n");

#endif /* UCONFIG_NO_REGULAR_EXPRESSIONS */

}

/*
 * Print out a containing element, like:
 * <trans-unit id = "blah" resname = "blah" restype = "x-id-alias" translate = "no">
 * <group id "calendar_gregorian" resname = "gregorian" restype = "x-icu-array">
 */
static char *printContainer(SResource *res, const char *container, const char *restype, const char *mimetype, const char *id, UErrorCode *status)
{
    const char *resname = nullptr;
    char *sid = nullptr;

    write_tabs(out);

    resname = res->getKeyString(srBundle);
    if (resname != nullptr && *resname != 0) {
        sid = getID(id, resname, sid);
    } else {
        sid = getID(id, nullptr, sid);
    }

    write_utf8_file(out, UnicodeString("<"));
    write_utf8_file(out, UnicodeString(container));
    printAttribute("id", sid, static_cast<int32_t>(uprv_strlen(sid)));

    if (resname != nullptr) {
        printAttribute("resname", resname, static_cast<int32_t>(uprv_strlen(resname)));
    }

    if (mimetype != nullptr) {
        printAttribute("mime-type", mimetype, static_cast<int32_t>(uprv_strlen(mimetype)));
    }

    if (restype != nullptr) {
        printAttribute("restype", restype, static_cast<int32_t>(uprv_strlen(restype)));
    }

    tabCount += 1;
    if (res->fComment.fLength > 0) {
        /* printComments will print the closing ">\n" */
        printComments(&res->fComment, resname, true, status);
    } else {
        write_utf8_file(out, UnicodeString(">\n"));
    }

    return sid;
}

/* Writing Functions */

static const char *trans_unit = "trans-unit";
static const char *close_trans_unit = "</trans-unit>\n";
static const char *source = "<source>";
static const char *close_source = "</source>\n";
static const char *group = "group";
static const char *close_group = "</group>\n";

static const char *bin_unit = "bin-unit";
static const char *close_bin_unit = "</bin-unit>\n";
static const char *bin_source = "<bin-source>\n";
static const char *close_bin_source = "</bin-source>\n";
static const char *external_file = "<external-file";
/*static const char *close_external_file = "</external-file>\n";*/
static const char *internal_file = "<internal-file";
static const char *close_internal_file = "</internal-file>\n";

static const char *application_mimetype = "application"; /* add "/octet-stream"? */

static const char *alias_restype     = "x-icu-alias";
static const char *array_restype     = "x-icu-array";
static const char *binary_restype    = "x-icu-binary";
static const char *integer_restype   = "x-icu-integer";
static const char *intvector_restype = "x-icu-intvector";
static const char *table_restype     = "x-icu-table";

static void
string_write_xml(StringResource *res, const char* id, const char* /*language*/, UErrorCode *status) {

    char *sid = nullptr;
    char* buf = nullptr;
    int32_t bufLen = 0;

    if(status==nullptr || U_FAILURE(*status)){
        return;
    }

    sid = printContainer(res, trans_unit, nullptr, nullptr, id, status);

    write_tabs(out);

    write_utf8_file(out, UnicodeString(source));

    buf = convertAndEscape(&buf, 0, &bufLen, res->getBuffer(), res->length(), status);

    if (U_FAILURE(*status)) {
        uprv_free(buf);
        uprv_free(sid);
        return;
    }

    write_utf8_file(out, UnicodeString(buf, bufLen, "UTF-8"));
    write_utf8_file(out, UnicodeString(close_source));

    printNoteElements(&res->fComment, status);

    tabCount -= 1;
    write_tabs(out);

    write_utf8_file(out, UnicodeString(close_trans_unit));

    uprv_free(buf);
    uprv_free(sid);
}

static void
alias_write_xml(AliasResource *res, const char* id, const char* /*language*/, UErrorCode *status) {
    char *sid = nullptr;
    char* buf = nullptr;
    int32_t bufLen=0;

    sid = printContainer(res, trans_unit, alias_restype, nullptr, id, status);

    write_tabs(out);

    write_utf8_file(out, UnicodeString(source));

    buf = convertAndEscape(&buf, 0, &bufLen, res->getBuffer(), res->length(), status);

    if(U_FAILURE(*status)){
        uprv_free(buf);
        uprv_free(sid);
        return;
    }
    write_utf8_file(out, UnicodeString(buf, bufLen, "UTF-8"));
    write_utf8_file(out, UnicodeString(close_source));

    printNoteElements(&res->fComment, status);

    tabCount -= 1;
    write_tabs(out);

    write_utf8_file(out, UnicodeString(close_trans_unit));

    uprv_free(buf);
    uprv_free(sid);
}

static void
array_write_xml(ArrayResource *res, const char* id, const char* language, UErrorCode *status) {
    char* sid = nullptr;
    int index = 0;

    struct SResource *current = nullptr;

    sid = printContainer(res, group, array_restype, nullptr, id, status);

    current = res->fFirst;

    while (current != nullptr) {
        char c[256] = {0};
        char* subId = nullptr;

        itostr(c, index, 10, 0);
        index += 1;
        subId = getID(sid, c, subId);

        res_write_xml(current, subId, language, false, status);
        uprv_free(subId);
        subId = nullptr;

        if(U_FAILURE(*status)){
            uprv_free(sid);
            return;
        }

        current = current->fNext;
    }

    tabCount -= 1;
    write_tabs(out);
    write_utf8_file(out, UnicodeString(close_group));

    uprv_free(sid);
}

static void
intvector_write_xml(IntVectorResource *res, const char* id, const char* /*language*/, UErrorCode *status) {
    char* sid = nullptr;
    char* ivd = nullptr;
    uint32_t i=0;
    uint32_t len=0;
    char buf[256] = {'0'};

    sid = printContainer(res, group, intvector_restype, nullptr, id, status);

    for(i = 0; i < res->fCount; i += 1) {
        char c[256] = {0};

        itostr(c, i, 10, 0);
        ivd = getID(sid, c, ivd);
        len = itostr(buf, res->fArray[i], 10, 0);

        write_tabs(out);
        write_utf8_file(out, UnicodeString("<"));
        write_utf8_file(out, UnicodeString(trans_unit));

        printAttribute("id", ivd, static_cast<int32_t>(uprv_strlen(ivd)));
        printAttribute("restype", integer_restype, static_cast<int32_t>(strlen(integer_restype)));

        write_utf8_file(out, UnicodeString(">\n"));

        tabCount += 1;
        write_tabs(out);
        write_utf8_file(out, UnicodeString(source));

        write_utf8_file(out, UnicodeString(buf, len));

        write_utf8_file(out, UnicodeString(close_source));
        tabCount -= 1;
        write_tabs(out);
        write_utf8_file(out, UnicodeString(close_trans_unit));

        uprv_free(ivd);
        ivd = nullptr;
    }

    tabCount -= 1;
    write_tabs(out);

    write_utf8_file(out, UnicodeString(close_group));
    uprv_free(sid);
    sid = nullptr;
}

static void
int_write_xml(IntResource *res, const char* id, const char* /*language*/, UErrorCode *status) {
    char* sid = nullptr;
    char buf[256] = {0};
    uint32_t len = 0;

    sid = printContainer(res, trans_unit, integer_restype, nullptr, id, status);

    write_tabs(out);

    write_utf8_file(out, UnicodeString(source));

    len = itostr(buf, res->fValue, 10, 0);
    write_utf8_file(out, UnicodeString(buf, len));

    write_utf8_file(out, UnicodeString(close_source));

    printNoteElements(&res->fComment, status);

    tabCount -= 1;
    write_tabs(out);

    write_utf8_file(out, UnicodeString(close_trans_unit));

    uprv_free(sid);
    sid = nullptr;
}

static void
bin_write_xml(BinaryResource *res, const char* id, const char* /*language*/, UErrorCode *status) {
    const char* m_type = application_mimetype;
    char* sid = nullptr;
    uint32_t crc = 0xFFFFFFFF;

    char fileName[1024] ={0};
    int32_t tLen = outDir == nullptr ? 0 : static_cast<int32_t>(uprv_strlen(outDir));
    char* fn = static_cast<char*>(uprv_malloc(sizeof(char) * (tLen + 1024 +
                                                    (res->fFileName !=nullptr ?
                                                    uprv_strlen(res->fFileName) :0))));
    const char* ext = nullptr;

    char* f = nullptr;

    fn[0]=0;

    if(res->fFileName != nullptr){
        uprv_strcpy(fileName, res->fFileName);
        f = uprv_strrchr(fileName, '\\');

        if (f != nullptr) {
            f++;
        } else {
            f = fileName;
        }

        ext = uprv_strrchr(fileName, '.');

        if (ext == nullptr) {
            fprintf(stderr, "Error: %s is an unknown binary filename type.\n", fileName);
            exit(U_ILLEGAL_ARGUMENT_ERROR);
        }

        if(uprv_strcmp(ext, ".jpg")==0 || uprv_strcmp(ext, ".jpeg")==0 || uprv_strcmp(ext, ".gif")==0 ){
            m_type = "image";
        } else if(uprv_strcmp(ext, ".wav")==0 || uprv_strcmp(ext, ".au")==0 ){
            m_type = "audio";
        } else if(uprv_strcmp(ext, ".avi")==0 || uprv_strcmp(ext, ".mpg")==0 || uprv_strcmp(ext, ".mpeg")==0){
            m_type = "video";
        } else if(uprv_strcmp(ext, ".txt")==0 || uprv_strcmp(ext, ".text")==0){
            m_type = "text";
        }

        sid = printContainer(res, bin_unit, binary_restype, m_type, id, status);

        write_tabs(out);

        write_utf8_file(out, UnicodeString(bin_source));

        tabCount+= 1;
        write_tabs(out);

        write_utf8_file(out, UnicodeString(external_file));
        printAttribute("href", f, static_cast<int32_t>(uprv_strlen(f)));
        write_utf8_file(out, UnicodeString("/>\n"));
        tabCount -= 1;
        write_tabs(out);

        write_utf8_file(out, UnicodeString(close_bin_source));

        printNoteElements(&res->fComment, status);
        tabCount -= 1;
        write_tabs(out);
        write_utf8_file(out, UnicodeString(close_bin_unit));
    } else {
        char temp[256] = {0};
        uint32_t i = 0;
        int32_t len=0;

        sid = printContainer(res, bin_unit, binary_restype, m_type, id, status);

        write_tabs(out);
        write_utf8_file(out, UnicodeString(bin_source));

        tabCount += 1;
        write_tabs(out);

        write_utf8_file(out, UnicodeString(internal_file));
        printAttribute("form", application_mimetype, static_cast<int32_t>(uprv_strlen(application_mimetype)));

        while(i <res->fLength){
            len = itostr(temp, res->fData[i], 16, 2);
            crc = computeCRC(temp, len, crc);
            i++;
        }

        len = itostr(temp, crc, 10, 0);
        printAttribute("crc", temp, len);

        write_utf8_file(out, UnicodeString(">"));

        i = 0;
        while(i <res->fLength){
            len = itostr(temp, res->fData[i], 16, 2);
            write_utf8_file(out, UnicodeString(temp));
            i += 1;
        }

        write_utf8_file(out, UnicodeString(close_internal_file));

        tabCount -= 2;
        write_tabs(out);

        write_utf8_file(out, UnicodeString(close_bin_source));
        printNoteElements(&res->fComment, status);

        tabCount -= 1;
        write_tabs(out);
        write_utf8_file(out, UnicodeString(close_bin_unit));

        uprv_free(sid);
        sid = nullptr;
    }

    uprv_free(fn);
}



static void
table_write_xml(TableResource *res, const char* id, const char* language, UBool isTopLevel, UErrorCode *status) {

    struct SResource *current = nullptr;
    char* sid = nullptr;

    if (U_FAILURE(*status)) {
        return ;
    }

    sid = printContainer(res, group, table_restype, nullptr, id, status);

    if(isTopLevel) {
        sid[0] = '\0';
    }

    current = res->fFirst;

    while (current != nullptr) {
        res_write_xml(current, sid, language, false, status);

        if(U_FAILURE(*status)){
            uprv_free(sid);
            return;
        }

        current = current->fNext;
    }

    tabCount -= 1;
    write_tabs(out);

    write_utf8_file(out, UnicodeString(close_group));

    uprv_free(sid);
    sid = nullptr;
}

void
res_write_xml(struct SResource *res, const char* id,  const char* language, UBool isTopLevel, UErrorCode *status) {

    if (U_FAILURE(*status)) {
        return ;
    }

    if (res != nullptr) {
        switch (res->fType) {
        case URES_STRING:
             string_write_xml    (static_cast<StringResource *>(res), id, language, status);
             return;

        case URES_ALIAS:
             alias_write_xml     (static_cast<AliasResource *>(res), id, language, status);
             return;

        case URES_INT_VECTOR:
             intvector_write_xml (static_cast<IntVectorResource *>(res), id, language, status);
             return;

        case URES_BINARY:
             bin_write_xml       (static_cast<BinaryResource *>(res), id, language, status);
             return;

        case URES_INT:
             int_write_xml       (static_cast<IntResource *>(res), id, language, status);
             return;

        case URES_ARRAY:
             array_write_xml     (static_cast<ArrayResource *>(res), id, language, status);
             return;

        case URES_TABLE:
             table_write_xml     (static_cast<TableResource *>(res), id, language, isTopLevel, status);
             return;

        default:
            break;
        }
    }

    *status = U_INTERNAL_PROGRAM_ERROR;
}

void
bundle_write_xml(struct SRBRoot *bundle, const char *outputDir,const char* outputEnc, const char* filename,
                  char *writtenFilename, int writtenFilenameLen,
                  const char* language, const char* outFileName, UErrorCode *status) {

    char* xmlfileName = nullptr;
    char* outputFileName = nullptr;
    char* originalFileName = nullptr;
    const char* fileStart = "<file xml:space = \"preserve\" source-language = \"";
    const char* file1 = "\" datatype = \"x-icu-resource-bundle\" ";
    const char* file2 = "original = \"";
    const char* file4 = "\" date = \"";
    const char* fileEnd = "</file>\n";
    const char* headerStart = "<header>\n";
    const char* headerEnd = "</header>\n";
    const char* bodyStart = "<body>\n";
    const char* bodyEnd = "</body>\n";

    const char *tool_start = "<tool";
    const char *tool_id = "genrb-" GENRB_VERSION "-icu-" U_ICU_VERSION;
    const char *tool_name = "genrb";

    char* temp = nullptr;
    char* lang = nullptr;
    const char* pos = nullptr;
    int32_t first, index;
    time_t currTime;
    char timeBuf[128];

    outDir = outputDir;

    srBundle = bundle;

    pos = uprv_strrchr(filename, '\\');
    if(pos != nullptr) {
        first = static_cast<int32_t>(pos - filename + 1);
    } else {
        first = 0;
    }
    index = static_cast<int32_t>(uprv_strlen(filename) - uprv_strlen(textExt) - first);
    originalFileName = static_cast<char*>(uprv_malloc(sizeof(char) * index + 1));
    uprv_memset(originalFileName, 0, sizeof(char)*index+1);
    uprv_strncpy(originalFileName, filename + first, index);

    if(uprv_strcmp(originalFileName, srBundle->fLocale) != 0) {
        fprintf(stdout, "Warning: The file name is not same as the resource name!\n");
    }

    temp = originalFileName;
    originalFileName = static_cast<char*>(uprv_malloc(sizeof(char) * (uprv_strlen(temp) + uprv_strlen(textExt)) + 1));
    uprv_memset(originalFileName, 0, sizeof(char)* (uprv_strlen(temp)+uprv_strlen(textExt)) + 1);
    uprv_strcat(originalFileName, temp);
    uprv_strcat(originalFileName, textExt);
    uprv_free(temp);
    temp = nullptr;


    if (language == nullptr) {
/*        lang = parseFilename(filename, lang);
        if (lang == nullptr) {*/
            /* now check if locale name is valid or not
             * this is to cater for situation where
             * pegasusServer.txt contains
             *
             * en{
             *      ..
             * }
             */
             lang = parseFilename(srBundle->fLocale, lang);
             /*
              * Neither  the file name nor the table name inside the
              * txt file contain a valid country and language codes
              * throw an error.
              * pegasusServer.txt contains
              *
              * testelements{
              *     ....
              * }
              */
             if(lang==nullptr){
                 fprintf(stderr, "Error: The file name and table name do not contain a valid language code. Please use -l option to specify it.\n");
                 exit(U_ILLEGAL_ARGUMENT_ERROR);
             }
       /* }*/
    } else {
        lang = static_cast<char*>(uprv_malloc(sizeof(char) * uprv_strlen(language) + 1));
        uprv_memset(lang, 0, sizeof(char)*uprv_strlen(language) +1);
        uprv_strcpy(lang, language);
    }

    if(outFileName) {
        outputFileName = static_cast<char*>(uprv_malloc(sizeof(char) * uprv_strlen(outFileName) + 1));
        uprv_memset(outputFileName, 0, sizeof(char)*uprv_strlen(outFileName) + 1);
        uprv_strcpy(outputFileName,outFileName);
    } else {
        outputFileName = static_cast<char*>(uprv_malloc(sizeof(char) * uprv_strlen(srBundle->fLocale) + 1));
        uprv_memset(outputFileName, 0, sizeof(char)*uprv_strlen(srBundle->fLocale) + 1);
        uprv_strcpy(outputFileName,srBundle->fLocale);
    }

    if(outputDir) {
        xmlfileName = static_cast<char*>(uprv_malloc(sizeof(char) * (uprv_strlen(outputDir) + uprv_strlen(outputFileName) + uprv_strlen(xliffExt) + 1) + 1));
        uprv_memset(xmlfileName, 0, sizeof(char)*(uprv_strlen(outputDir)+ uprv_strlen(outputFileName) + uprv_strlen(xliffExt) + 1) +1);
    } else {
        xmlfileName = static_cast<char*>(uprv_malloc(sizeof(char) * (uprv_strlen(outputFileName) + uprv_strlen(xliffExt)) + 1));
        uprv_memset(xmlfileName, 0, sizeof(char)*(uprv_strlen(outputFileName) + uprv_strlen(xliffExt)) +1);
    }

    if(outputDir){
        uprv_strcpy(xmlfileName, outputDir);
        if(outputDir[uprv_strlen(outputDir)-1] !=U_FILE_SEP_CHAR){
            uprv_strcat(xmlfileName,U_FILE_SEP_STRING);
        }
    }
    uprv_strcat(xmlfileName,outputFileName);
    uprv_strcat(xmlfileName,xliffExt);

    if (writtenFilename) {
        uprv_strncpy(writtenFilename, xmlfileName, writtenFilenameLen);
    }

    if (U_FAILURE(*status)) {
        goto cleanup_bundle_write_xml;
    }

    out= T_FileStream_open(xmlfileName,"w");

    if(out==nullptr){
        *status = U_FILE_ACCESS_ERROR;
        goto cleanup_bundle_write_xml;
    }
    write_utf8_file(out, UnicodeString(xmlHeader));

    if(outputEnc && *outputEnc!='\0'){
        /* store the output encoding */
        enc = outputEnc;
        conv=ucnv_open(enc,status);
        if(U_FAILURE(*status)){
            goto cleanup_bundle_write_xml;
        }
    }
    write_utf8_file(out, UnicodeString(bundleStart));
    write_tabs(out);
    write_utf8_file(out, UnicodeString(fileStart));
    /* check if lang and language are the same */
    if(language != nullptr && uprv_strcmp(lang, srBundle->fLocale)!=0){
        fprintf(stderr,"Warning: The top level tag in the resource and language specified are not the same. Please check the input.\n");
    }
    write_utf8_file(out, UnicodeString(lang));
    write_utf8_file(out, UnicodeString(file1));
    write_utf8_file(out, UnicodeString(file2));
    write_utf8_file(out, UnicodeString(originalFileName));
    write_utf8_file(out, UnicodeString(file4));

    time(&currTime);
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&currTime));
    write_utf8_file(out, UnicodeString(timeBuf));
    write_utf8_file(out, UnicodeString("\">\n"));

    tabCount += 1;
    write_tabs(out);
    write_utf8_file(out, UnicodeString(headerStart));

    tabCount += 1;
    write_tabs(out);

    write_utf8_file(out, UnicodeString(tool_start));
    printAttribute("tool-id", tool_id, static_cast<int32_t>(uprv_strlen(tool_id)));
    printAttribute("tool-name", tool_name, static_cast<int32_t>(uprv_strlen(tool_name)));
    write_utf8_file(out, UnicodeString("/>\n"));

    tabCount -= 1;
    write_tabs(out);

    write_utf8_file(out, UnicodeString(headerEnd));

    write_tabs(out);
    tabCount += 1;

    write_utf8_file(out, UnicodeString(bodyStart));


    res_write_xml(bundle->fRoot, bundle->fLocale, lang, true, status);

    tabCount -= 1;
    write_tabs(out);

    write_utf8_file(out, UnicodeString(bodyEnd));
    tabCount--;
    write_tabs(out);
    write_utf8_file(out, UnicodeString(fileEnd));
    tabCount--;
    write_tabs(out);
    write_utf8_file(out, UnicodeString(bundleEnd));
    T_FileStream_close(out);

    ucnv_close(conv);

cleanup_bundle_write_xml:
    uprv_free(originalFileName);
    uprv_free(lang);
    if(xmlfileName != nullptr) {
        uprv_free(xmlfileName);
    }
    if(outputFileName != nullptr){
        uprv_free(outputFileName);
    }
}

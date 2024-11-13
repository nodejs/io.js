// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 1998-2016, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*
* File ucbuf.cpp
*
* Modification History:
*
*   Date        Name        Description
*   05/10/01    Ram         Creation.
*******************************************************************************
*/

#include "unicode/utypes.h"
#include "unicode/putil.h"
#include "unicode/uchar.h"
#include "unicode/ucnv.h"
#include "unicode/ucnv_err.h"
#include "unicode/ustring.h"
#include "unicode/utf16.h"
#include "filestrm.h"
#include "cstring.h"
#include "cmemory.h"
#include "ustrfmt.h"
#include "ucbuf.h"
#include <stdio.h>

#if !UCONFIG_NO_CONVERSION


#define MAX_IN_BUF 1000
#define MAX_U_BUF 1500
#define CONTEXT_LEN 20

struct UCHARBUF {
    char16_t* buffer;
    char16_t* currentPos;
    char16_t* bufLimit;
    int32_t bufCapacity;
    int32_t remaining;
    int32_t signatureLength;
    FileStream* in;
    UConverter* conv;
    UBool showWarning; /* makes this API not produce any errors */
    UBool isBuffered;
};

U_CAPI UBool U_EXPORT2
ucbuf_autodetect_fs(FileStream* in, const char** cp, UConverter** conv, int32_t* signatureLength, UErrorCode* error){
    char start[8];
    int32_t numRead;

    char16_t target[1]={ 0 };
    char16_t* pTarget;
    const char* pStart;

    /* read a few bytes */
    numRead=T_FileStream_read(in, start, sizeof(start));

    *cp = ucnv_detectUnicodeSignature(start, numRead, signatureLength, error);
    
    /* unread the bytes beyond what was consumed for U+FEFF */
    T_FileStream_rewind(in);
    if (*signatureLength > 0) {
        T_FileStream_read(in, start, *signatureLength);
    }

    if(*cp==nullptr){
        *conv =nullptr;
        return false;
    }

    /* open the converter for the detected Unicode charset */
    *conv = ucnv_open(*cp,error);

    /* convert and ignore initial U+FEFF, and the buffer overflow */
    pTarget = target;
    pStart = start;
    ucnv_toUnicode(*conv, &pTarget, target+1, &pStart, start+*signatureLength, nullptr, false, error);
    *signatureLength = (int32_t)(pStart - start);
    if(*error==U_BUFFER_OVERFLOW_ERROR) {
        *error=U_ZERO_ERROR;
    }

    /* verify that we successfully read exactly U+FEFF */
    if(U_SUCCESS(*error) && (pTarget!=(target+1) || target[0]!=0xfeff)) {
        *error=U_INTERNAL_PROGRAM_ERROR;
    }


    return true; 
}
static UBool ucbuf_isCPKnown(const char* cp){
    if(ucnv_compareNames("UTF-8",cp)==0){
        return true;
    }
    if(ucnv_compareNames("UTF-16BE",cp)==0){
        return true;
    }
    if(ucnv_compareNames("UTF-16LE",cp)==0){
        return true;
    }
    if(ucnv_compareNames("UTF-16",cp)==0){
        return true;
    }
    if(ucnv_compareNames("UTF-32",cp)==0){
        return true;
    }
    if(ucnv_compareNames("UTF-32BE",cp)==0){
        return true;
    }
    if(ucnv_compareNames("UTF-32LE",cp)==0){
        return true;
    }
    if(ucnv_compareNames("SCSU",cp)==0){
        return true;
    }
    if(ucnv_compareNames("BOCU-1",cp)==0){
        return true;
    }
    if(ucnv_compareNames("UTF-7",cp)==0){
        return true;
    }
    return false;
}

U_CAPI FileStream * U_EXPORT2
ucbuf_autodetect(const char* fileName, const char** cp,UConverter** conv, int32_t* signatureLength,UErrorCode* error){
    FileStream* in=nullptr;
    if(error==nullptr || U_FAILURE(*error)){
        return nullptr;
    }
    if(conv==nullptr || cp==nullptr || fileName==nullptr){
        *error = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }
    /* open the file */
    in= T_FileStream_open(fileName,"rb");
    
    if(in == nullptr){
        *error=U_FILE_ACCESS_ERROR;
        return nullptr;
    }

    if(ucbuf_autodetect_fs(in,cp,conv,signatureLength,error)) {
        return in;
    } else {
        ucnv_close(*conv);
        *conv=nullptr;
        T_FileStream_close(in);
        return nullptr;
    }
}

/* fill the uchar buffer */
static UCHARBUF*
ucbuf_fillucbuf( UCHARBUF* buf,UErrorCode* error){
    char16_t* pTarget=nullptr;
    char16_t* target=nullptr;
    const char* source=nullptr;
    char  carr[MAX_IN_BUF] = {'\0'};
    char* cbuf =  carr;
    int32_t inputRead=0;
    int32_t outputWritten=0;
    int32_t offset=0;
    const char* sourceLimit =nullptr;
    int32_t cbufSize=0;
    pTarget = buf->buffer;
    /* check if we arrived here without exhausting the buffer*/
    if(buf->currentPos<buf->bufLimit){
        offset = static_cast<int32_t>(buf->bufLimit - buf->currentPos);
        memmove(buf->buffer,buf->currentPos,offset* sizeof(char16_t));
    }

#ifdef UCBUF_DEBUG
    memset(pTarget+offset,0xff,sizeof(char16_t)*(MAX_IN_BUF-offset));
#endif
    if(buf->isBuffered){
        cbufSize = MAX_IN_BUF;
        /* read the file */
        inputRead=T_FileStream_read(buf->in,cbuf,cbufSize-offset);
        buf->remaining-=inputRead;
        
    }else{
        cbufSize = T_FileStream_size(buf->in);
        cbuf = static_cast<char*>(uprv_malloc(cbufSize));
        if (cbuf == nullptr) {
        	*error = U_MEMORY_ALLOCATION_ERROR;
        	return nullptr;
        }
        inputRead= T_FileStream_read(buf->in,cbuf,cbufSize);
        buf->remaining-=inputRead;
    }

    /* just to be sure...*/
    if ( 0 == inputRead )
       buf->remaining = 0;

    target=pTarget;
    /* convert the bytes */
    if(buf->conv){
        /* set the callback to stop */
        UConverterToUCallback toUOldAction ;
        void* toUOldContext;
        void* toUNewContext=nullptr;
        ucnv_setToUCallBack(buf->conv,
           UCNV_TO_U_CALLBACK_STOP,
           toUNewContext,
           &toUOldAction,
           (const void**)&toUOldContext,
           error);
        /* since state is saved in the converter we add offset to source*/
        target = pTarget+offset;
        source = cbuf;
        sourceLimit = source + inputRead;
        ucnv_toUnicode(buf->conv,&target,target+(buf->bufCapacity-offset),
                        &source,sourceLimit,nullptr,
                        static_cast<UBool>(buf->remaining == 0), error);

        if(U_FAILURE(*error)){
            char context[CONTEXT_LEN+1];
            char preContext[CONTEXT_LEN+1];
            char postContext[CONTEXT_LEN+1];
            int8_t len = CONTEXT_LEN;
            int32_t start=0;
            int32_t stop =0;
            int32_t pos =0;
            /* use erro1 to preserve the error code */
            UErrorCode error1 =U_ZERO_ERROR;
            
            if( buf->showWarning==true){
                fprintf(stderr,"\n###WARNING: Encountered abnormal bytes while"
                               " converting input stream to target encoding: %s\n",
                               u_errorName(*error));
            }


            /* now get the context chars */
            ucnv_getInvalidChars(buf->conv,context,&len,&error1);
            context[len]= 0 ; /* null terminate the buffer */

            pos = static_cast<int32_t>(source - cbuf - len);

            /* for pre-context */
            start = (pos <=CONTEXT_LEN)? 0 : (pos - (CONTEXT_LEN-1));
            stop  = pos-len;

            memcpy(preContext,cbuf+start,stop-start);
            /* null terminate the buffer */
            preContext[stop-start] = 0;

            /* for post-context */
            start = pos+len;
            stop = static_cast<int32_t>(pos + CONTEXT_LEN <= sourceLimit - cbuf ? pos + (CONTEXT_LEN - 1) : sourceLimit - cbuf);

            memcpy(postContext,source,stop-start);
            /* null terminate the buffer */
            postContext[stop-start] = 0;

            if(buf->showWarning ==true){
                /* print out the context */
                fprintf(stderr,"\tPre-context: %s\n",preContext);
                fprintf(stderr,"\tContext: %s\n",context);
                fprintf(stderr,"\tPost-context: %s\n", postContext);
            }

            /* reset the converter */
            ucnv_reset(buf->conv);

            /* set the call back to substitute
             * and restart conversion
             */
            ucnv_setToUCallBack(buf->conv,
               UCNV_TO_U_CALLBACK_SUBSTITUTE,
               toUNewContext,
               &toUOldAction,
               (const void**)&toUOldContext,
               &error1);

            /* reset source and target start positions */
            target = pTarget+offset;
            source = cbuf;

            /* re convert */
            ucnv_toUnicode(buf->conv,&target,target+(buf->bufCapacity-offset),
                            &source,sourceLimit,nullptr,
                            static_cast<UBool>(buf->remaining == 0), &error1);

        }
        outputWritten = static_cast<int32_t>(target - pTarget);

#ifdef UCBUF_DEBUG
        {
            int i;
            target = pTarget;
            for(i=0;i<numRead;i++){
              /*  printf("%c", (char)(*target++));*/
            }
        }
#endif

    }else{
        u_charsToUChars(cbuf,target+offset,inputRead);
        outputWritten=((buf->remaining>cbufSize)? cbufSize:inputRead+offset);
    }
    buf->currentPos = pTarget;
    buf->bufLimit=pTarget+outputWritten;
    *buf->bufLimit=0; /*NUL terminate*/
    if(cbuf!=carr){
        uprv_free(cbuf);
    }
    return buf;
}



/* get a char16_t from the stream*/
U_CAPI int32_t U_EXPORT2
ucbuf_getc(UCHARBUF* buf,UErrorCode* error){
    if(error==nullptr || U_FAILURE(*error)){
        return false;
    }
    if(buf->currentPos>=buf->bufLimit){
        if(buf->remaining==0){
            return U_EOF;
        }
        buf=ucbuf_fillucbuf(buf,error);
        if(U_FAILURE(*error)){
            return U_EOF;
        }
    }

    return *(buf->currentPos++);
}

/* get a UChar32 from the stream*/
U_CAPI int32_t U_EXPORT2
ucbuf_getc32(UCHARBUF* buf,UErrorCode* error){
    int32_t retVal = U_EOF;
    if(error==nullptr || U_FAILURE(*error)){
        return false;
    }
    if(buf->currentPos+1>=buf->bufLimit){
        if(buf->remaining==0){
            return U_EOF;
        }
        buf=ucbuf_fillucbuf(buf,error);
        if(U_FAILURE(*error)){
            return U_EOF;
        }
    }
    if(U16_IS_LEAD(*(buf->currentPos))){
        retVal=U16_GET_SUPPLEMENTARY(buf->currentPos[0],buf->currentPos[1]);
        buf->currentPos+=2;
    }else{
        retVal = *(buf->currentPos++);
    }
    return retVal;
}

/* u_unescapeAt() callback to return a char16_t*/
static char16_t U_CALLCONV
_charAt(int32_t offset, void *context) {
    return static_cast<UCHARBUF*>(context)->currentPos[offset];
}

/* getc and escape it */
U_CAPI int32_t U_EXPORT2
ucbuf_getcx32(UCHARBUF* buf,UErrorCode* error) {
    int32_t length;
    int32_t offset;
    UChar32 c32,c1,c2;
    if(error==nullptr || U_FAILURE(*error)){
        return false;
    }
    /* Fill the buffer if it is empty */
    if (buf->currentPos >=buf->bufLimit-2) {
        ucbuf_fillucbuf(buf,error);
    }

    /* Get the next character in the buffer */
    if (buf->currentPos < buf->bufLimit) {
        c1 = *(buf->currentPos)++;
    } else {
        c1 = U_EOF;
    }

    c2 = *(buf->currentPos);

    /* If it isn't a backslash, return it */
    if (c1 != 0x005C) {
        return c1;
    }

    /* Determine the amount of data in the buffer */
    length = (int32_t)(buf->bufLimit - buf->currentPos);

    /* The longest escape sequence is \Uhhhhhhhh; make sure
       we have at least that many characters */
    if (length < 10) {

        /* fill the buffer */
        ucbuf_fillucbuf(buf,error);
        length = (int32_t)(buf->bufLimit - buf->buffer);
    }

    /* Process the escape */
    offset = 0;
    c32 = u_unescapeAt(_charAt, &offset, length, (void*)buf);

    /* check if u_unescapeAt unescaped and converted
     * to c32 or not
     */
    if(c32==(UChar32)0xFFFFFFFF){
        if(buf->showWarning) {
            char context[CONTEXT_LEN+1];
            int32_t len = CONTEXT_LEN;
            if(length < len) {
                len = length; 
            }
            context[len]= 0 ; /* null terminate the buffer */
            u_UCharsToChars( buf->currentPos, context, len);
            fprintf(stderr,"Bad escape: [%c%s]...\n", (int)c1, context);
        }
        *error= U_ILLEGAL_ESCAPE_SEQUENCE;
        return c1;
    }else if(c32!=c2 || (c32==0x0075 && c2==0x0075 && c1==0x005C) /* for \u0075 c2=0x0075 and c32==0x0075*/){
        /* Update the current buffer position */
        buf->currentPos += offset;
    }else{
        /* unescaping failed so we just return
         * c1 and not consume the buffer
         * this is useful for rules with escapes
         * in resource bundles
         * eg: \' \\ \"
         */
        return c1;
    }

    return c32;
}

U_CAPI UCHARBUF* U_EXPORT2
ucbuf_open(const char* fileName,const char** cp,UBool showWarning, UBool buffered, UErrorCode* error){

    FileStream* in = nullptr; 
    int32_t fileSize=0;
    const char* knownCp;
    if(error==nullptr || U_FAILURE(*error)){
        return nullptr;
    }
    if(cp==nullptr || fileName==nullptr){
        *error = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }
    if (!uprv_strcmp(fileName, "-")) {
        in = T_FileStream_stdin();
    }else{ 
        in = T_FileStream_open(fileName, "rb");
    }
    
    if(in!=nullptr){
        UCHARBUF* buf =(UCHARBUF*) uprv_malloc(sizeof(UCHARBUF));
        fileSize = T_FileStream_size(in);
        if(buf == nullptr){
            *error = U_MEMORY_ALLOCATION_ERROR;
            T_FileStream_close(in);
            return nullptr;
        }
        buf->in=in;
        buf->conv=nullptr;
        buf->showWarning = showWarning;
        buf->isBuffered = buffered;
        buf->signatureLength=0;
        if(*cp==nullptr || **cp=='\0'){
            /* don't have code page name... try to autodetect */
            ucbuf_autodetect_fs(in,cp,&buf->conv,&buf->signatureLength,error);
        }else if(ucbuf_isCPKnown(*cp)){
            /* discard BOM */
            ucbuf_autodetect_fs(in,&knownCp,&buf->conv,&buf->signatureLength,error);
        }
        if(U_SUCCESS(*error) && buf->conv==nullptr) {
            buf->conv=ucnv_open(*cp,error);
        }
        if(U_FAILURE(*error)){
            ucnv_close(buf->conv);
            uprv_free(buf);
            T_FileStream_close(in);
            return nullptr;
        }
        
        if((buf->conv==nullptr) && (buf->showWarning==true)){
            fprintf(stderr,"###WARNING: No converter defined. Using codepage of system.\n");
        }
        buf->remaining=fileSize-buf->signatureLength;
        if(buf->isBuffered){
            buf->bufCapacity=MAX_U_BUF;
        }else{
            buf->bufCapacity=buf->remaining+buf->signatureLength+1/*for terminating nul*/;               
        }
        buf->buffer=(char16_t*) uprv_malloc(U_SIZEOF_UCHAR * buf->bufCapacity );
        if (buf->buffer == nullptr) {
            *error = U_MEMORY_ALLOCATION_ERROR;
            ucbuf_close(buf);
            return nullptr;
        }
        buf->currentPos=buf->buffer;
        buf->bufLimit=buf->buffer;
        if(U_FAILURE(*error)){
            fprintf(stderr, "Could not open codepage [%s]: %s\n", *cp, u_errorName(*error));
            ucbuf_close(buf);
            return nullptr;
        }
        ucbuf_fillucbuf(buf,error);
        if(U_FAILURE(*error)){
            ucbuf_close(buf);
            return nullptr;
        }
        return buf;
    }
    *error =U_FILE_ACCESS_ERROR;
    return nullptr;
}



/* TODO: this method will fail if at the
 * beginning of buffer and the uchar to unget
 * is from the previous buffer. Need to implement
 * system to take care of that situation.
 */
U_CAPI void U_EXPORT2
ucbuf_ungetc(int32_t c,UCHARBUF* buf){
    /* decrement currentPos pointer
     * if not at the beginning of buffer
     */
    if(buf->currentPos!=buf->buffer){
        if(*(buf->currentPos-1)==c){
            buf->currentPos--;
        } else {
            /* ungetc failed - did not match. */
        }
    } else {
       /* ungetc failed - beginning of buffer. */
    }
}

/* frees the resources of char16_t* buffer */
static void
ucbuf_closebuf(UCHARBUF* buf){
    uprv_free(buf->buffer);
    buf->buffer = nullptr;
}

/* close the buf and release resources*/
U_CAPI void U_EXPORT2
ucbuf_close(UCHARBUF* buf){
    if(buf!=nullptr){
        if(buf->conv){
            ucnv_close(buf->conv);
        }
        T_FileStream_close(buf->in);
        ucbuf_closebuf(buf);
        uprv_free(buf);
    }
}

/* rewind the buf and file stream */
U_CAPI void U_EXPORT2
ucbuf_rewind(UCHARBUF* buf,UErrorCode* error){
    if(error==nullptr || U_FAILURE(*error)){
        return;
    }
    if(buf){
        buf->currentPos=buf->buffer;
        buf->bufLimit=buf->buffer;
        T_FileStream_rewind(buf->in);
        buf->remaining=T_FileStream_size(buf->in)-buf->signatureLength;

        ucnv_resetToUnicode(buf->conv);
        if(buf->signatureLength>0) {
            char16_t target[1]={ 0 };
            char16_t* pTarget;
            char start[8];
            const char* pStart;
            int32_t numRead;

            /* read the signature bytes */
            numRead=T_FileStream_read(buf->in, start, buf->signatureLength);

            /* convert and ignore initial U+FEFF, and the buffer overflow */
            pTarget = target;
            pStart = start;
            ucnv_toUnicode(buf->conv, &pTarget, target+1, &pStart, start+numRead, nullptr, false, error);
            if(*error==U_BUFFER_OVERFLOW_ERROR) {
                *error=U_ZERO_ERROR;
            }

            /* verify that we successfully read exactly U+FEFF */
            if(U_SUCCESS(*error) && (numRead!=buf->signatureLength || pTarget!=(target+1) || target[0]!=0xfeff)) {
                *error=U_INTERNAL_PROGRAM_ERROR;
            }
        }
    }
}


U_CAPI int32_t U_EXPORT2
ucbuf_size(UCHARBUF* buf){
    if(buf){
        if(buf->isBuffered){
            return (T_FileStream_size(buf->in)-buf->signatureLength)/ucnv_getMinCharSize(buf->conv);
        }else{
            return (int32_t)(buf->bufLimit - buf->buffer);
        }
    }
    return 0;
}

U_CAPI const char16_t* U_EXPORT2
ucbuf_getBuffer(UCHARBUF* buf,int32_t* len,UErrorCode* error){
    if(error==nullptr || U_FAILURE(*error)){
        return nullptr;
    }
    if(buf==nullptr || len==nullptr){
        *error = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }
    *len = (int32_t)(buf->bufLimit - buf->buffer);
    return buf->buffer;
}

U_CAPI const char* U_EXPORT2
ucbuf_resolveFileName(const char* inputDir, const char* fileName, char* target, int32_t* len, UErrorCode* status){
    int32_t requiredLen = 0;
    int32_t dirlen =  0;
    int32_t filelen = 0;
    if(status==nullptr || U_FAILURE(*status)){
        return nullptr;
    }

    if(inputDir == nullptr || fileName == nullptr || len==nullptr || (target==nullptr && *len>0)){
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }


    dirlen  = (int32_t)uprv_strlen(inputDir);
    filelen = (int32_t)uprv_strlen(fileName);
    if(inputDir[dirlen-1] != U_FILE_SEP_CHAR) {
        requiredLen = dirlen + filelen + 2;
        if((*len < requiredLen) || target==nullptr){
            *len = requiredLen;
            *status = U_BUFFER_OVERFLOW_ERROR;
            return nullptr;
        }

        target[0] = '\0';
        /*
         * append the input dir to openFileName if the first char in 
         * filename is not file separation char and the last char input directory is  not '.'.
         * This is to support :
         * genrb -s. /home/icu/data
         * genrb -s. icu/data
         * The user cannot mix notations like
         * genrb -s. /icu/data --- the absolute path specified. -s redundant
         * user should use
         * genrb -s. icu/data  --- start from CWD and look in icu/data dir
         */
        if( (fileName[0] != U_FILE_SEP_CHAR) && (inputDir[dirlen-1] !='.')){
            uprv_strcpy(target, inputDir);
            target[dirlen]     = U_FILE_SEP_CHAR;
        }
        target[dirlen + 1] = '\0';
    } else {
        requiredLen = dirlen + filelen + 1;
        if((*len < requiredLen) || target==nullptr){
            *len = requiredLen;
            *status = U_BUFFER_OVERFLOW_ERROR;
            return nullptr;
        }
        
        uprv_strcpy(target, inputDir);
    }

    uprv_strcat(target, fileName);
    return target;
}
/*
 * Unicode TR 13 says any of the below chars is
 * a new line char in a readline function in addition
 * to CR+LF combination which needs to be 
 * handled separately
 */
static UBool ucbuf_isCharNewLine(char16_t c){
    switch(c){
    case 0x000A: /* LF  */
    case 0x000D: /* CR  */
    case 0x000C: /* FF  */
    case 0x0085: /* NEL */
    case 0x2028: /* LS  */
    case 0x2029: /* PS  */
        return true;
    default:
        return false;
    }
}

U_CAPI const char16_t* U_EXPORT2
ucbuf_readline(UCHARBUF* buf,int32_t* len,UErrorCode* err){
    char16_t* temp = buf->currentPos;
    char16_t* savePos =nullptr;
    char16_t c=0x0000;
    if(buf->isBuffered){
        /* The input is buffered we have to do more
        * for returning a pointer U_TRUNCATED_CHAR_FOUND
        */
        for(;;){
            c = *temp++;
            if(buf->remaining==0){
                return nullptr; /* end of file is reached return nullptr */
            }
            if(temp>=buf->bufLimit && buf->currentPos == buf->buffer){
                *err= U_TRUNCATED_CHAR_FOUND;
                return nullptr;
            }else{
                ucbuf_fillucbuf(buf,err);
                if(U_FAILURE(*err)){
                    return nullptr; 
                }
            }
            /*
             * According to TR 13 readLine functions must interpret
             * CR, CR+LF, LF, NEL, PS, LS or FF as line seperators
             */
            /* Windows CR LF */
            if(c ==0x0d && temp <= buf->bufLimit && *temp == 0x0a ){
                *len = (int32_t)(temp++ - buf->currentPos);
                savePos = buf->currentPos;
                buf->currentPos = temp;
                return savePos;
            }
            /* else */

            if (temp>=buf->bufLimit|| ucbuf_isCharNewLine(c)){  /* Unipad inserts 2028 line separators! */
                *len = (int32_t)(temp - buf->currentPos);
                savePos = buf->currentPos;
                buf->currentPos = temp;
                return savePos;
            }
        }
    }else{
    /* we know that all input is read into the internal
    * buffer so we can safely return pointers
        */
        for(;;){
            c = *temp++;
            
            if(buf->currentPos==buf->bufLimit){
                return nullptr; /* end of file is reached return nullptr */
            }
            /* Windows CR LF */
            if(c ==0x0d && temp <= buf->bufLimit && *temp == 0x0a ){
                *len = (int32_t)(temp++ - buf->currentPos);
                savePos = buf->currentPos;
                buf->currentPos = temp;
                return savePos;
            }
            /* else */
            if (temp>=buf->bufLimit|| ucbuf_isCharNewLine(c)) {  /* Unipad inserts 2028 line separators! */
                *len = (int32_t)(temp - buf->currentPos);
                savePos = buf->currentPos;
                buf->currentPos = temp;
                return savePos;
            }
        }
    }
    /* not reached */
    /* A compiler warning will appear if all paths don't contain a return statement. */
/*    return nullptr;*/
}
#endif

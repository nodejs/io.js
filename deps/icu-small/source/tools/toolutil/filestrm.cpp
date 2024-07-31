// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
******************************************************************************
*
*   Copyright (C) 1997-2011, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
******************************************************************************
*
* File FILESTRM.C
*
* @author       Glenn Marcy
*
* Modification History:
*
*   Date        Name        Description
*   5/8/98      gm          Created
*  03/02/99     stephen     Reordered params in ungetc to match stdio
*                           Added wopen
*   3/29/99     helena      Merged Stephen and Bertrand's changes.
*
******************************************************************************
*/

#include "filestrm.h"

#include "cmemory.h"

#include <stdio.h>

U_CAPI FileStream* U_EXPORT2
T_FileStream_open(const char* filename, const char* mode)
{
    if(filename != nullptr && *filename != 0 && mode != nullptr && *mode != 0) {
        FILE *file = fopen(filename, mode);
        return (FileStream*)file;
    } else {
        return nullptr;
    }
}

/*
U_CAPI FileStream* U_EXPORT2
T_FileStream_wopen(const wchar_t* filename, const wchar_t* mode)
{
   // TBD: _wfopen is believed to be MS-specific? 
#if U_PLATFORM_USES_ONLY_WIN32_API
    FILE* result = _wfopen(filename, mode);
    return (FileStream*)result;
#else
    size_t fnMbsSize, mdMbsSize;
    char *fn, *md;
    FILE *result;

    // convert from wchar_t to char 
    fnMbsSize = wcstombs(nullptr, filename, ((size_t)-1) >> 1);
    fn = (char*)uprv_malloc(fnMbsSize+2);
    wcstombs(fn, filename, fnMbsSize);
    fn[fnMbsSize] = 0;

    mdMbsSize = wcstombs(nullptr, mode, ((size_t)-1) >> 1);
    md = (char*)uprv_malloc(mdMbsSize+2);
    wcstombs(md, mode, mdMbsSize);
    md[mdMbsSize] = 0;

    result = fopen(fn, md);
    uprv_free(fn);
    uprv_free(md);
    return (FileStream*)result;
#endif
}
*/
U_CAPI void U_EXPORT2
T_FileStream_close(FileStream* fileStream)
{
    if (fileStream != nullptr)
        fclose((FILE*)fileStream);
}

U_CAPI UBool U_EXPORT2
T_FileStream_file_exists(const char* filename)
{
    FILE* temp = fopen(filename, "r");
    if (temp) {
        fclose(temp);
        return true;
    } else
        return false;
}

/*static const int32_t kEOF;
const int32_t FileStream::kEOF = EOF;*/

/*
U_CAPI FileStream*
T_FileStream_tmpfile()
{
    FILE* file = tmpfile();
    return (FileStream*)file;
}
*/

U_CAPI int32_t U_EXPORT2
T_FileStream_read(FileStream* fileStream, void* addr, int32_t len)
{
    return static_cast<int32_t>(fread(addr, 1, len, (FILE*)fileStream));
}

U_CAPI int32_t U_EXPORT2
T_FileStream_write(FileStream* fileStream, const void* addr, int32_t len)
{

    return static_cast<int32_t>(fwrite(addr, 1, len, (FILE*)fileStream));
}

U_CAPI void U_EXPORT2
T_FileStream_rewind(FileStream* fileStream)
{
    rewind((FILE*)fileStream);
}

U_CAPI int32_t U_EXPORT2
T_FileStream_putc(FileStream* fileStream, int32_t ch)
{
    int32_t c = fputc(ch, (FILE*)fileStream);
    return c;
}

U_CAPI int U_EXPORT2
T_FileStream_getc(FileStream* fileStream)
{
    int c = fgetc((FILE*)fileStream);
    return c;
}

U_CAPI int32_t U_EXPORT2
T_FileStream_ungetc(int32_t ch, FileStream* fileStream)
{

    int32_t c = ungetc(ch, (FILE*)fileStream);
    return c;
}

U_CAPI int32_t U_EXPORT2
T_FileStream_peek(FileStream* fileStream)
{
    int32_t c = fgetc((FILE*)fileStream);
    return ungetc(c, (FILE*)fileStream);
}

U_CAPI char* U_EXPORT2
T_FileStream_readLine(FileStream* fileStream, char* buffer, int32_t length)
{
    return fgets(buffer, length, (FILE*)fileStream);
}

U_CAPI int32_t U_EXPORT2
T_FileStream_writeLine(FileStream* fileStream, const char* buffer)
{
    return fputs(buffer, (FILE*)fileStream);
}

U_CAPI int32_t U_EXPORT2
T_FileStream_size(FileStream* fileStream)
{
    int32_t savedPos = ftell((FILE*)fileStream);
    int32_t size = 0;

    /*Changes by Bertrand A. D. doesn't affect the current position
    goes to the end of the file before ftell*/
    fseek((FILE*)fileStream, 0, SEEK_END);
    size = (int32_t)ftell((FILE*)fileStream);
    fseek((FILE*)fileStream, savedPos, SEEK_SET);
    return size;
}

U_CAPI int U_EXPORT2
T_FileStream_eof(FileStream* fileStream)
{
    return feof((FILE*)fileStream);
}

/*
 Warning 
 This function may not work consistently on all platforms
 (e.g. HP-UX, FreeBSD and MacOSX don't return an error when
 putc is used on a file opened as readonly)
*/
U_CAPI int U_EXPORT2
T_FileStream_error(FileStream* fileStream)
{
    return (fileStream == nullptr || ferror((FILE*)fileStream));
}

/* This function doesn't work. */
/* force the stream to set its error flag*/
/*U_CAPI void U_EXPORT2
T_FileStream_setError(FileStream* fileStream)
{
    fseek((FILE*)fileStream, 99999, SEEK_SET);
}
*/

U_CAPI FileStream* U_EXPORT2
T_FileStream_stdin()
{
    return (FileStream*)stdin;
}

U_CAPI FileStream* U_EXPORT2
T_FileStream_stdout()
{
    return (FileStream*)stdout;
}


U_CAPI FileStream* U_EXPORT2
T_FileStream_stderr()
{
    return (FileStream*)stderr;
}

U_CAPI UBool U_EXPORT2
T_FileStream_remove(const char* fileName){
    return (remove(fileName) == 0);
}

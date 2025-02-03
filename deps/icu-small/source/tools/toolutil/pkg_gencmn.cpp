// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/******************************************************************************
 *   Copyright (C) 2008-2012, International Business Machines
 *   Corporation and others.  All Rights Reserved.
 *******************************************************************************
 */
#include "unicode/utypes.h"

#include <stdio.h>
#include <stdlib.h>
#include "unicode/utypes.h"
#include "unicode/putil.h"
#include "cmemory.h"
#include "cstring.h"
#include "filestrm.h"
#include "toolutil.h"
#include "unicode/uclean.h"
#include "unewdata.h"
#include "putilimp.h"
#include "pkg_gencmn.h"

#define STRING_STORE_SIZE 200000

#define COMMON_DATA_NAME U_ICUDATA_NAME
#define DATA_TYPE "dat"

/* ICU package data file format (.dat files) ------------------------------- ***

Description of the data format after the usual ICU data file header
(UDataInfo etc.).

Format version 1

A .dat package file contains a simple Table of Contents of item names,
followed by the items themselves:

1. ToC table

uint32_t count; - number of items
UDataOffsetTOCEntry entry[count]; - pair of uint32_t values per item:
    uint32_t nameOffset; - offset of the item name
    uint32_t dataOffset; - offset of the item data
both are byte offsets from the beginning of the data

2. item name strings

All item names are stored as char * strings in one block between the ToC table
and the data items.

3. data items

The data items are stored following the item names block.
Each data item is 16-aligned.
The data items are stored in the sorted order of their names.

Therefore, the top of the name strings block is the offset of the first item,
the length of the last item is the difference between its offset and
the .dat file length, and the length of all previous items is the difference
between its offset and the next one.

----------------------------------------------------------------------------- */

/* UDataInfo cf. udata.h */
static const UDataInfo dataInfo={
    sizeof(UDataInfo),
    0,

    U_IS_BIG_ENDIAN,
    U_CHARSET_FAMILY,
    sizeof(char16_t),
    0,

    {0x43, 0x6d, 0x6e, 0x44},     /* dataFormat="CmnD" */
    {1, 0, 0, 0},                 /* formatVersion */
    {3, 0, 0, 0}                  /* dataVersion */
};

static uint32_t maxSize;

static char stringStore[STRING_STORE_SIZE];
static uint32_t stringTop=0, basenameTotal=0;

typedef struct {
    char *pathname, *basename;
    uint32_t basenameLength, basenameOffset, fileSize, fileOffset;
} File;

#define CHUNK_FILE_COUNT 256
static File *files = nullptr;
static uint32_t fileCount=0;
static uint32_t fileMax = 0;


static char *symPrefix = nullptr;

#define LINE_BUFFER_SIZE 512
/* prototypes --------------------------------------------------------------- */

static void
addFile(const char *filename, const char *name, const char *source, UBool sourceTOC, UBool verbose);

static char *
allocString(uint32_t length);

U_CDECL_BEGIN
static int
compareFiles(const void *file1, const void *file2);
U_CDECL_END

static char *
pathToFullPath(const char *path, const char *source);

/* map non-tree separator (such as '\') to tree separator ('/') inplace. */
static void
fixDirToTreePath(char *s);
/* -------------------------------------------------------------------------- */

U_CAPI void U_EXPORT2
createCommonDataFile(const char *destDir, const char *name, const char *entrypointName, const char *type, const char *source, const char *copyRight,
                     const char *dataFile, uint32_t max_size, UBool sourceTOC, UBool verbose, char *gencmnFileName) {
    static char buffer[4096];
    char *line;
    char *linePtr;
    char *s = nullptr;
    UErrorCode errorCode=U_ZERO_ERROR;
    uint32_t i, fileOffset, basenameOffset, length, nread;
    FileStream *in, *file;

    line = (char *)uprv_malloc(sizeof(char) * LINE_BUFFER_SIZE);
    if (line == nullptr) {
        fprintf(stderr, "gencmn: unable to allocate memory for line buffer of size %d\n", LINE_BUFFER_SIZE);
        exit(U_MEMORY_ALLOCATION_ERROR);
    }

    linePtr = line;

    maxSize = max_size;

    if (destDir == nullptr) {
        destDir = u_getDataDirectory();
    }
    if (name == nullptr) {
        name = COMMON_DATA_NAME;
    }
    if (type == nullptr) {
        type = DATA_TYPE;
    }
    if (source == nullptr) {
        source = ".";
    }

    if (dataFile == nullptr) {
        in = T_FileStream_stdin();
    } else {
        in = T_FileStream_open(dataFile, "r");
        if(in == nullptr) {
            fprintf(stderr, "gencmn: unable to open input file %s\n", dataFile);
            exit(U_FILE_ACCESS_ERROR);
        }
    }

    if (verbose) {
        if(sourceTOC) {
            printf("generating %s_%s.c (table of contents source file)\n", name, type);
        } else {
            printf("generating %s.%s (common data file with table of contents)\n", name, type);
        }
    }

    /* read the list of files and get their lengths */
    while((s != nullptr && *s != 0) || (s=T_FileStream_readLine(in, (line=linePtr),
                                                             LINE_BUFFER_SIZE))!=nullptr) {
        /* remove trailing newline characters and parse space separated items */
        if (s != nullptr && *s != 0) {
            line=s;
        } else {
            s=line;
        }
        while(*s!=0) {
            if(*s==' ') {
                *s=0;
                ++s;
                break;
            } else if(*s=='\r' || *s=='\n') {
                *s=0;
                break;
            }
            ++s;
        }

        /* check for comment */

        if (*line == '#') {
            continue;
        }

        /* add the file */
#if (U_FILE_SEP_CHAR != U_FILE_ALT_SEP_CHAR)
        {
          char *t;
          while((t = uprv_strchr(line,U_FILE_ALT_SEP_CHAR))) {
            *t = U_FILE_SEP_CHAR;
          }
        }
#endif
        addFile(getLongPathname(line), name, source, sourceTOC, verbose);
    }

    uprv_free(linePtr);

    if(in!=T_FileStream_stdin()) {
        T_FileStream_close(in);
    }

    if(fileCount==0) {
        fprintf(stderr, "gencmn: no files listed in %s\n", dataFile == nullptr ? "<stdin>" : dataFile);
        return;
    }

    /* sort the files by basename */
    qsort(files, fileCount, sizeof(File), compareFiles);

    if(!sourceTOC) {
        UNewDataMemory *out;

        /* determine the offsets of all basenames and files in this common one */
        basenameOffset=4+8*fileCount;
        fileOffset=(basenameOffset+(basenameTotal+15))&~0xf;
        for(i=0; i<fileCount; ++i) {
            files[i].fileOffset=fileOffset;
            fileOffset+=(files[i].fileSize+15)&~0xf;
            files[i].basenameOffset=basenameOffset;
            basenameOffset+=files[i].basenameLength;
        }

        /* create the output file */
        out=udata_create(destDir, type, name,
                         &dataInfo,
                         copyRight == nullptr ? U_COPYRIGHT_STRING : copyRight,
                         &errorCode);
        if(U_FAILURE(errorCode)) {
            fprintf(stderr, "gencmn: udata_create(-d %s -n %s -t %s) failed - %s\n",
                destDir, name, type,
                u_errorName(errorCode));
            exit(errorCode);
        }

        /* write the table of contents */
        udata_write32(out, fileCount);
        for(i=0; i<fileCount; ++i) {
            udata_write32(out, files[i].basenameOffset);
            udata_write32(out, files[i].fileOffset);
        }

        /* write the basenames */
        for(i=0; i<fileCount; ++i) {
            udata_writeString(out, files[i].basename, files[i].basenameLength);
        }
        length=4+8*fileCount+basenameTotal;

        /* copy the files */
        for(i=0; i<fileCount; ++i) {
            /* pad to 16-align the next file */
            length&=0xf;
            if(length!=0) {
                udata_writePadding(out, 16-length);
            }

            if (verbose) {
                printf("adding %s (%ld byte%s)\n", files[i].pathname, (long)files[i].fileSize, files[i].fileSize == 1 ? "" : "s");
            }

            /* copy the next file */
            file=T_FileStream_open(files[i].pathname, "rb");
            if(file==nullptr) {
                fprintf(stderr, "gencmn: unable to open listed file %s\n", files[i].pathname);
                exit(U_FILE_ACCESS_ERROR);
            }
            for(nread = 0;;) {
                length=T_FileStream_read(file, buffer, sizeof(buffer));
                if(length <= 0) {
                    break;
                }
                nread += length;
                udata_writeBlock(out, buffer, length);
            }
            T_FileStream_close(file);
            length=files[i].fileSize;

            if (nread != files[i].fileSize) {
              fprintf(stderr, "gencmn: unable to read %s properly (got %ld/%ld byte%s)\n", files[i].pathname,  (long)nread, (long)files[i].fileSize, files[i].fileSize == 1 ? "" : "s");
                exit(U_FILE_ACCESS_ERROR);
            }
        }

        /* pad to 16-align the last file (cleaner, avoids growing .dat files in icuswap) */
        length&=0xf;
        if(length!=0) {
            udata_writePadding(out, 16-length);
        }

        /* finish */
        udata_finish(out, &errorCode);
        if(U_FAILURE(errorCode)) {
            fprintf(stderr, "gencmn: udata_finish() failed - %s\n", u_errorName(errorCode));
            exit(errorCode);
        }
    } else {
        /* write a .c source file with the table of contents */
        char *filename;
        FileStream *out;

        /* create the output filename */
        filename=s=buffer;
        uprv_strcpy(filename, destDir);
        s=filename+uprv_strlen(filename);
        if(s>filename && *(s-1)!=U_FILE_SEP_CHAR) {
            *s++=U_FILE_SEP_CHAR;
        }
        uprv_strcpy(s, name);
        if(*(type)!=0) {
            s+=uprv_strlen(s);
            *s++='_';
            uprv_strcpy(s, type);
        }
        s+=uprv_strlen(s);
        uprv_strcpy(s, ".c");

        /* open the output file */
        out=T_FileStream_open(filename, "w");
        if (gencmnFileName != nullptr) {
            uprv_strcpy(gencmnFileName, filename);
        }
        if(out==nullptr) {
            fprintf(stderr, "gencmn: unable to open .c output file %s\n", filename);
            exit(U_FILE_ACCESS_ERROR);
        }

        /* write the source file */
        snprintf(buffer, sizeof(buffer),
            "/*\n"
            " * ICU common data table of contents for %s.%s\n"
            " * Automatically generated by icu/source/tools/gencmn/gencmn .\n"
            " */\n\n"
            "#include \"unicode/utypes.h\"\n"
            "#include \"unicode/udata.h\"\n"
            "\n"
            "/* external symbol declarations for data (%d files) */\n",
                name, type, fileCount);
        T_FileStream_writeLine(out, buffer);

        snprintf(buffer, sizeof(buffer), "extern const char\n    %s%s[]", symPrefix?symPrefix:"", files[0].pathname);
        T_FileStream_writeLine(out, buffer);
        for(i=1; i<fileCount; ++i) {
            snprintf(buffer, sizeof(buffer), ",\n    %s%s[]", symPrefix?symPrefix:"", files[i].pathname);
            T_FileStream_writeLine(out, buffer);
        }
        T_FileStream_writeLine(out, ";\n\n");

        snprintf(
            buffer, sizeof(buffer),
            "U_EXPORT struct {\n"
            "    uint16_t headerSize;\n"
            "    uint8_t magic1, magic2;\n"
            "    UDataInfo info;\n"
            "    char padding[%lu];\n"
            "    uint32_t count, reserved;\n"
            "    struct {\n"
            "        const char *name;\n"
            "        const void *data;\n"
            "    } toc[%lu];\n"
            "} U_EXPORT2 %s_dat = {\n"
            "    32, 0xda, 0x27, {\n"
            "        %lu, 0,\n"
            "        %u, %u, %u, 0,\n"
            "        {0x54, 0x6f, 0x43, 0x50},\n"
            "        {1, 0, 0, 0},\n"
            "        {0, 0, 0, 0}\n"
            "    },\n"
            "    \"\", %lu, 0, {\n",
            static_cast<unsigned long>(32-4-sizeof(UDataInfo)),
            static_cast<unsigned long>(fileCount),
            entrypointName,
            static_cast<unsigned long>(sizeof(UDataInfo)),
            U_IS_BIG_ENDIAN,
            U_CHARSET_FAMILY,
            U_SIZEOF_UCHAR,
            static_cast<unsigned long>(fileCount)
        );
        T_FileStream_writeLine(out, buffer);

        snprintf(buffer, sizeof(buffer), "        { \"%s\", %s%s }", files[0].basename, symPrefix?symPrefix:"", files[0].pathname);
        T_FileStream_writeLine(out, buffer);
        for(i=1; i<fileCount; ++i) {
            snprintf(buffer, sizeof(buffer), ",\n        { \"%s\", %s%s }", files[i].basename, symPrefix?symPrefix:"", files[i].pathname);
            T_FileStream_writeLine(out, buffer);
        }

        T_FileStream_writeLine(out, "\n    }\n};\n");
        T_FileStream_close(out);

        uprv_free(symPrefix);
    }
}

static void
addFile(const char *filename, const char *name, const char *source, UBool sourceTOC, UBool verbose) {
    char *s;
    uint32_t length;
    char *fullPath = nullptr;

    if(fileCount==fileMax) {
      fileMax += CHUNK_FILE_COUNT;
      files = static_cast<File*>(uprv_realloc(files, fileMax * sizeof(files[0]))); /* note: never freed. */
      if(files==nullptr) {
        fprintf(stderr, "pkgdata/gencmn: Could not allocate %u bytes for %d files\n", static_cast<unsigned int>(fileMax * sizeof(files[0])), fileCount);
        exit(U_MEMORY_ALLOCATION_ERROR);
      }
    }

    if(!sourceTOC) {
        FileStream *file;

        if(uprv_pathIsAbsolute(filename)) {
            fprintf(stderr, "gencmn: Error: absolute path encountered. Old style paths are not supported. Use relative paths such as 'fur.res' or 'translit%cfur.res'.\n\tBad path: '%s'\n", U_FILE_SEP_CHAR, filename);
            exit(U_ILLEGAL_ARGUMENT_ERROR);
        }
        fullPath = pathToFullPath(filename, source);
        /* store the pathname */
        length = static_cast<uint32_t>(uprv_strlen(filename) + 1 + uprv_strlen(name) + 1);
        s=allocString(length);
        uprv_strcpy(s, name);
        uprv_strcat(s, U_TREE_ENTRY_SEP_STRING);
        uprv_strcat(s, filename);

        /* get the basename */
        fixDirToTreePath(s);
        files[fileCount].basename=s;
        files[fileCount].basenameLength=length;

        files[fileCount].pathname=fullPath;

        basenameTotal+=length;

        /* try to open the file */
        file=T_FileStream_open(fullPath, "rb");
        if(file==nullptr) {
            fprintf(stderr, "gencmn: unable to open listed file %s\n", fullPath);
            exit(U_FILE_ACCESS_ERROR);
        }

        /* get the file length */
        length=T_FileStream_size(file);
        if(T_FileStream_error(file) || length<=20) {
            fprintf(stderr, "gencmn: unable to get length of listed file %s\n", fullPath);
            exit(U_FILE_ACCESS_ERROR);
        }

        T_FileStream_close(file);

        /* do not add files that are longer than maxSize */
        if(maxSize && length>maxSize) {
            if (verbose) {
                printf("%s ignored (size %ld > %ld)\n", fullPath, static_cast<long>(length), static_cast<long>(maxSize));
            }
            return;
        }
        files[fileCount].fileSize=length;
    } else {
        char *t;
        /* get and store the basename */
        /* need to include the package name */
        length = static_cast<uint32_t>(uprv_strlen(filename) + 1 + uprv_strlen(name) + 1);
        s=allocString(length);
        uprv_strcpy(s, name);
        uprv_strcat(s, U_TREE_ENTRY_SEP_STRING);
        uprv_strcat(s, filename);
        fixDirToTreePath(s);
        files[fileCount].basename=s;
        /* turn the basename into an entry point name and store in the pathname field */
        t=files[fileCount].pathname=allocString(length);
        while(--length>0) {
            if(*s=='.' || *s=='-' || *s=='/') {
                *t='_';
            } else {
                *t=*s;
            }
            ++s;
            ++t;
        }
        *t=0;
    }
    ++fileCount;
}

static char *
allocString(uint32_t length) {
    uint32_t top=stringTop+length;
    char *p;

    if(top>STRING_STORE_SIZE) {
        fprintf(stderr, "gencmn: out of memory\n");
        exit(U_MEMORY_ALLOCATION_ERROR);
    }
    p=stringStore+stringTop;
    stringTop=top;
    return p;
}

static char *
pathToFullPath(const char *path, const char *source) {
    int32_t length;
    int32_t newLength;
    char *fullPath;
    int32_t n;

    length = static_cast<uint32_t>(uprv_strlen(path) + 1);
    newLength = (length + 1 + static_cast<int32_t>(uprv_strlen(source)));
    fullPath = static_cast<char*>(uprv_malloc(newLength));
    if(source != nullptr) {
        uprv_strcpy(fullPath, source);
        uprv_strcat(fullPath, U_FILE_SEP_STRING);
    } else {
        fullPath[0] = 0;
    }
    n = static_cast<int32_t>(uprv_strlen(fullPath));
    fullPath[n] = 0;       /* Suppress compiler warning for unused variable n    */
                           /*  when conditional code below is not compiled.      */
    uprv_strcat(fullPath, path);

#if (U_FILE_ALT_SEP_CHAR != U_TREE_ENTRY_SEP_CHAR)
#if (U_FILE_ALT_SEP_CHAR != U_FILE_SEP_CHAR)
    /* replace tree separator (such as '/') with file sep char (such as ':' or '\\') */
    for(;fullPath[n];n++) {
        if(fullPath[n] == U_FILE_ALT_SEP_CHAR) {
            fullPath[n] = U_FILE_SEP_CHAR;
        }
    }
#endif
#endif
#if (U_FILE_SEP_CHAR != U_TREE_ENTRY_SEP_CHAR)
    /* replace tree separator (such as '/') with file sep char (such as ':' or '\\') */
    for(;fullPath[n];n++) {
        if(fullPath[n] == U_TREE_ENTRY_SEP_CHAR) {
            fullPath[n] = U_FILE_SEP_CHAR;
        }
    }
#endif
    return fullPath;
}

U_CDECL_BEGIN
static int
compareFiles(const void *file1, const void *file2) {
    /* sort by basename */
    return uprv_strcmp(((File *)file1)->basename, ((File *)file2)->basename);
}
U_CDECL_END

static void
fixDirToTreePath(char *s)
{
    (void)s;
#if (U_FILE_SEP_CHAR != U_TREE_ENTRY_SEP_CHAR) || ((U_FILE_ALT_SEP_CHAR != U_FILE_SEP_CHAR) && (U_FILE_ALT_SEP_CHAR != U_TREE_ENTRY_SEP_CHAR))
    char *t;
#endif
#if (U_FILE_SEP_CHAR != U_TREE_ENTRY_SEP_CHAR)
    for(t=s;t=uprv_strchr(t,U_FILE_SEP_CHAR);) {
        *t = U_TREE_ENTRY_SEP_CHAR;
    }
#endif
#if (U_FILE_ALT_SEP_CHAR != U_FILE_SEP_CHAR) && (U_FILE_ALT_SEP_CHAR != U_TREE_ENTRY_SEP_CHAR)
    for(t=s;t=uprv_strchr(t,U_FILE_ALT_SEP_CHAR);) {
        *t = U_TREE_ENTRY_SEP_CHAR;
    }
#endif
}

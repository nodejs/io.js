// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 1999-2016, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  derb.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2000sep6
*   created by: Vladimir Weinstein as an ICU workshop example
*   maintained by: Yves Arrouye <yves@realnames.com>
*/

#include "unicode/stringpiece.h"
#include "unicode/ucnv.h"
#include "unicode/unistr.h"
#include "unicode/ustring.h"
#include "unicode/putil.h"
#include "unicode/ustdio.h"

#include "charstr.h"
#include "uresimp.h"
#include "cmemory.h"
#include "cstring.h"
#include "uoptions.h"
#include "toolutil.h"
#include "ustrfmt.h"

#if !UCONFIG_NO_FORMATTING

#define DERB_VERSION "1.1"

#define DERB_DEFAULT_TRUNC 80

static const int32_t indentsize = 4;
static int32_t truncsize = DERB_DEFAULT_TRUNC;
static UBool opt_truncate = false;

static const char *getEncodingName(const char *encoding);
static void reportError(const char *pname, UErrorCode *status, const char *when);
static char16_t *quotedString(const char16_t *string);
static void printOutBundle(UFILE *out, UResourceBundle *resource, int32_t indent, const char *pname, UErrorCode *status);
static void printString(UFILE *out, const char16_t *str, int32_t len);
static void printCString(UFILE *out, const char *str, int32_t len);
static void printIndent(UFILE *out, int32_t indent);
static void printHex(UFILE *out, uint8_t what);

static UOption options[]={
    UOPTION_HELP_H,
    UOPTION_HELP_QUESTION_MARK,
/* 2 */    UOPTION_ENCODING,
/* 3 */    { "to-stdout", nullptr, nullptr, nullptr, 'c', UOPT_NO_ARG, 0 } ,
/* 4 */    { "truncate", nullptr, nullptr, nullptr, 't', UOPT_OPTIONAL_ARG, 0 },
/* 5 */    UOPTION_VERBOSE,
/* 6 */    UOPTION_DESTDIR,
/* 7 */    UOPTION_SOURCEDIR,
/* 8 */    { "bom", nullptr, nullptr, nullptr, 0, UOPT_NO_ARG, 0 },
/* 9 */    UOPTION_ICUDATADIR,
/* 10 */   UOPTION_VERSION,
/* 11 */   { "suppressAliases", nullptr, nullptr, nullptr, 'A', UOPT_NO_ARG, 0 },
};

static UBool verbose = false;
static UBool suppressAliases = false;
static UFILE *ustderr = nullptr;

extern int
main(int argc, char* argv[]) {
    const char *encoding = nullptr;
    const char *outputDir = nullptr; /* nullptr = no output directory, use current */
    const char *inputDir  = ".";
    int tostdout = 0;
    int prbom = 0;

    const char *pname;

    UResourceBundle *bundle = nullptr;
    int32_t i = 0;

    const char* arg;

    /* Get the name of tool. */
    pname = uprv_strrchr(*argv, U_FILE_SEP_CHAR);
#if U_FILE_SEP_CHAR != U_FILE_ALT_SEP_CHAR
    if (!pname) {
        pname = uprv_strrchr(*argv, U_FILE_ALT_SEP_CHAR);
    }
#endif
    if (!pname) {
        pname = *argv;
    } else {
        ++pname;
    }

    /* error handling, printing usage message */
    argc=u_parseArgs(argc, argv, UPRV_LENGTHOF(options), options);

    /* error handling, printing usage message */
    if(argc<0) {
        fprintf(stderr,
            "%s: error in command line argument \"%s\"\n", pname,
            argv[-argc]);
    }
    if(argc<0 || options[0].doesOccur || options[1].doesOccur) {
        fprintf(argc < 0 ? stderr : stdout,
            "%csage: %s [ -h, -?, --help ] [ -V, --version ]\n"
            " [ -v, --verbose ] [ -e, --encoding encoding ] [ --bom ]\n"
            " [ -t, --truncate [ size ] ]\n"
            " [ -s, --sourcedir source ] [ -d, --destdir destination ]\n"
            " [ -i, --icudatadir directory ] [ -c, --to-stdout ]\n"
            " [ -A, --suppressAliases]\n"
            " bundle ...\n", argc < 0 ? 'u' : 'U',
            pname);
        return argc<0 ? U_ILLEGAL_ARGUMENT_ERROR : U_ZERO_ERROR;
    }

    if(options[10].doesOccur) {
        fprintf(stderr,
                "%s version %s (ICU version %s).\n"
                "%s\n",
                pname, DERB_VERSION, U_ICU_VERSION, U_COPYRIGHT_STRING);
        return U_ZERO_ERROR;
    }
    if(options[2].doesOccur) {
        encoding = options[2].value;
    }

    if (options[3].doesOccur) {
      if(options[2].doesOccur) {
        fprintf(stderr, "%s: Error: don't specify an encoding (-e) when writing to stdout (-c).\n", pname);
        return 3;
      }
      tostdout = 1;
    }

    if(options[4].doesOccur) {
        opt_truncate = true;
        if(options[4].value != nullptr) {
            truncsize = atoi(options[4].value); /* user defined printable size */
        } else {
            truncsize = DERB_DEFAULT_TRUNC; /* we'll use default omitting size */
        }
    } else {
        opt_truncate = false;
    }

    if(options[5].doesOccur) {
        verbose = true;
    }

    if (options[6].doesOccur) {
        outputDir = options[6].value;
    }

    if(options[7].doesOccur) {
        inputDir = options[7].value; /* we'll use users resources */
    }

    if (options[8].doesOccur) {
        prbom = 1;
    }

    if (options[9].doesOccur) {
        u_setDataDirectory(options[9].value);
    }

    if (options[11].doesOccur) {
      suppressAliases = true;
    }

    fflush(stderr); // use ustderr now.
    ustderr = u_finit(stderr, nullptr, nullptr);

    for (i = 1; i < argc; ++i) {
        static const char16_t sp[] = { 0x0020 }; /* " " */

        arg = getLongPathname(argv[i]);

        if (verbose) {
          u_fprintf(ustderr, "processing bundle \"%s\"\n", argv[i]);
        }

        icu::CharString locale;
        UErrorCode status = U_ZERO_ERROR;
        {
            const char *p = findBasename(arg);
            const char *q = uprv_strrchr(p, '.');
            if (q == nullptr) {
                locale.append(p, status);
            } else {
                locale.append(p, static_cast<int32_t>(q - p), status);
            }
        }
        if (U_FAILURE(status)) {
            return status;
        }

        icu::CharString infile;
        const char *thename = nullptr;
        UBool fromICUData = !uprv_strcmp(inputDir, "-");
        if (!fromICUData) {
            UBool absfilename = *arg == U_FILE_SEP_CHAR;
#if U_PLATFORM_HAS_WIN32_API
            if (!absfilename) {
                absfilename = (uprv_strlen(arg) > 2 && isalpha(arg[0])
                    && arg[1] == ':' && arg[2] == U_FILE_SEP_CHAR);
            }
#endif
            if (absfilename) {
                thename = arg;
            } else {
                const char *q = uprv_strrchr(arg, U_FILE_SEP_CHAR);
#if U_FILE_SEP_CHAR != U_FILE_ALT_SEP_CHAR
                if (q == nullptr) {
                    q = uprv_strrchr(arg, U_FILE_ALT_SEP_CHAR);
                }
#endif
                infile.append(inputDir, status);
                if(q != nullptr) {
                    infile.appendPathPart(icu::StringPiece(arg, static_cast<int32_t>(q - arg)), status);
                }
                if (U_FAILURE(status)) {
                    return status;
                }
                thename = infile.data();
            }
        }
        if (thename) {
            bundle = ures_openDirect(thename, locale.data(), &status);
        } else {
            bundle = ures_open(fromICUData ? nullptr : inputDir, locale.data(), &status);
        }
        if (U_SUCCESS(status)) {
            UFILE *out = nullptr;

            const char* filename = nullptr;
            const char* ext = nullptr;

            if (locale.isEmpty() || !tostdout) {
                filename = findBasename(arg);
                ext = uprv_strrchr(filename, '.');
                if (!ext) {
                    ext = uprv_strchr(filename, 0);
                }
            }

            if (tostdout) {
                out = u_get_stdout();
            } else {
                icu::CharString thefile;
                if (outputDir) {
                    thefile.append(outputDir, status);
                }
                thefile.appendPathPart(filename, status);
                if (*ext) {
                    thefile.truncate(thefile.length() - static_cast<int32_t>(uprv_strlen(ext)));
                }
                thefile.append(".txt", status);
                if (U_FAILURE(status)) {
                    return status;
                }

                out = u_fopen(thefile.data(), "w", nullptr, encoding);
                if (!out) {
                  u_fprintf(ustderr, "%s: couldn't create %s\n", pname, thefile.data());
                  u_fclose(ustderr);
                  return 4;
                }
            }

            // now, set the callback.
            ucnv_setFromUCallBack(u_fgetConverter(out), UCNV_FROM_U_CALLBACK_ESCAPE, UCNV_ESCAPE_C, nullptr, nullptr, &status);
            if (U_FAILURE(status)) {
              u_fprintf(ustderr, "%s: couldn't configure converter for encoding\n", pname);
              u_fclose(ustderr);
              if(!tostdout) {
                u_fclose(out);
              }
              return 3;
            }

            if (prbom) { /* XXX: Should be done only for UTFs */
              u_fputc(0xFEFF, out);
            }
            u_fprintf(out, "// -*- Coding: %s; -*-\n//\n", encoding ? encoding : getEncodingName(ucnv_getDefaultName()));
            u_fprintf(out, "// This file was dumped by derb(8) from ");
            if (thename) {
              u_fprintf(out, "%s", thename);
            } else if (fromICUData) {
              u_fprintf(out, "the ICU internal %s locale", locale.data());
            }

            u_fprintf(out, "\n// derb(8) by Vladimir Weinstein and Yves Arrouye\n\n");

            if (!locale.isEmpty()) {
              u_fprintf(out, "%s", locale.data());
            } else {
              u_fprintf(out, "%.*s%.*S", static_cast<int32_t>(ext - filename), filename, UPRV_LENGTHOF(sp), sp);
            }
            printOutBundle(out, bundle, 0, pname, &status);

            if (!tostdout) {
                u_fclose(out);
            }
        }
        else {
            reportError(pname, &status, "opening resource file");
        }

        ures_close(bundle);
    }

    return 0;
}

static char16_t *quotedString(const char16_t *string) {
    int len = u_strlen(string);
    int alen = len;
    const char16_t *sp;
    char16_t *newstr, *np;

    for (sp = string; *sp; ++sp) {
        switch (*sp) {
            case '\n':
            case 0x0022:
                ++alen;
                break;
        }
    }

    newstr = static_cast<char16_t*>(uprv_malloc((1 + alen) * U_SIZEOF_UCHAR));
    for (sp = string, np = newstr; *sp; ++sp) {
        switch (*sp) {
            case '\n':
                *np++ = 0x005C;
                *np++ = 0x006E;
                break;

            case 0x0022:
                *np++ = 0x005C;
                U_FALLTHROUGH;
            default:
                *np++ = *sp;
                break;
        }
    }
    *np = 0;

    return newstr;
}


static void printString(UFILE *out, const char16_t *str, int32_t len) {
  u_file_write(str, len, out);
}

static void printCString(UFILE *out, const char *str, int32_t len) {
  if(len==-1) {
    u_fprintf(out, "%s", str);
  } else {
    u_fprintf(out, "%.*s", len, str);
  }
}

static void printIndent(UFILE *out, int32_t indent) {
    icu::UnicodeString inchar(indent, 0x20, indent);
    printString(out, inchar.getBuffer(), indent);
}

static void printHex(UFILE *out, uint8_t what) {
    static const char map[] = "0123456789ABCDEF";
    char16_t hex[2];

    hex[0] = map[what >> 4];
    hex[1] = map[what & 0xf];

    printString(out, hex, 2);
}

static void printOutAlias(UFILE *out,  UResourceBundle *parent, Resource r, const char *key, int32_t indent, const char *pname, UErrorCode *status) {
    static const char16_t cr[] = { 0xA };  // LF
    int32_t len = 0;
    const char16_t* thestr = res_getAlias(&(parent->getResData()), r, &len);
    char16_t *string = quotedString(thestr);
    if(opt_truncate && len > truncsize) {
        char msg[128];
        printIndent(out, indent);
        snprintf(msg, sizeof(msg), "// WARNING: this resource, size %li is truncated to %li\n",
            static_cast<long>(len), static_cast<long>(truncsize) / 2);
        printCString(out, msg, -1);
        len = truncsize;
    }
    if(U_SUCCESS(*status)) {
        static const char16_t openStr[] = { 0x003A, 0x0061, 0x006C, 0x0069, 0x0061, 0x0073, 0x0020, 0x007B, 0x0020, 0x0022 }; /* ":alias { \"" */
        static const char16_t closeStr[] = { 0x0022, 0x0020, 0x007D, 0x0020 }; /* "\" } " */
        printIndent(out, indent);
        if(key != nullptr) {
            printCString(out, key, -1);
        }
        printString(out, openStr, UPRV_LENGTHOF(openStr));
        printString(out, string, len);
        printString(out, closeStr, UPRV_LENGTHOF(closeStr));
        if(verbose) {
            printCString(out, " // ALIAS", -1);
        }
        printString(out, cr, UPRV_LENGTHOF(cr));
    } else {
        reportError(pname, status, "getting binary value");
    }
    uprv_free(string);
}

static void printOutBundle(UFILE *out, UResourceBundle *resource, int32_t indent, const char *pname, UErrorCode *status)
{
    static const char16_t cr[] = { 0xA };  // LF

/*    int32_t noOfElements = ures_getSize(resource);*/
    int32_t i = 0;
    const char *key = ures_getKey(resource);

    switch(ures_getType(resource)) {
    case URES_STRING :
        {
            int32_t len=0;
            const char16_t* thestr = ures_getString(resource, &len, status);
            char16_t *string = quotedString(thestr);

            /* TODO: String truncation */
            if(opt_truncate && len > truncsize) {
                char msg[128];
                printIndent(out, indent);
                snprintf(msg, sizeof(msg), "// WARNING: this resource, size %li is truncated to %li\n",
                        static_cast<long>(len), static_cast<long>(truncsize / 2));
                printCString(out, msg, -1);
                len = truncsize/2;
            }
            printIndent(out, indent);
            if(key != nullptr) {
                static const char16_t openStr[] = { 0x0020, 0x007B, 0x0020, 0x0022 }; /* " { \"" */
                static const char16_t closeStr[] = { 0x0022, 0x0020, 0x007D }; /* "\" }" */
                printCString(out, key, static_cast<int32_t>(uprv_strlen(key)));
                printString(out, openStr, UPRV_LENGTHOF(openStr));
                printString(out, string, len);
                printString(out, closeStr, UPRV_LENGTHOF(closeStr));
            } else {
                static const char16_t openStr[] = { 0x0022 }; /* "\"" */
                static const char16_t closeStr[] = { 0x0022, 0x002C }; /* "\"," */

                printString(out, openStr, UPRV_LENGTHOF(openStr));
                printString(out, string, u_strlen(string));
                printString(out, closeStr, UPRV_LENGTHOF(closeStr));
            }

            if(verbose) {
                printCString(out, "// STRING", -1);
            }
            printString(out, cr, UPRV_LENGTHOF(cr));

            uprv_free(string);
        }
        break;

    case URES_INT :
        {
            static const char16_t openStr[] = { 0x003A, 0x0069, 0x006E, 0x0074, 0x0020, 0x007B, 0x0020 }; /* ":int { " */
            static const char16_t closeStr[] = { 0x0020, 0x007D }; /* " }" */
            char16_t num[20];

            printIndent(out, indent);
            if(key != nullptr) {
                printCString(out, key, -1);
            }
            printString(out, openStr, UPRV_LENGTHOF(openStr));
            uprv_itou(num, 20, ures_getInt(resource, status), 10, 0);
            printString(out, num, u_strlen(num));
            printString(out, closeStr, UPRV_LENGTHOF(closeStr));

            if(verbose) {
                printCString(out, "// INT", -1);
            }
            printString(out, cr, UPRV_LENGTHOF(cr));
            break;
        }
    case URES_BINARY :
        {
            int32_t len = 0;
            const int8_t* data = reinterpret_cast<const int8_t*>(ures_getBinary(resource, &len, status));
            if(opt_truncate && len > truncsize) {
                char msg[128];
                printIndent(out, indent);
                snprintf(msg, sizeof(msg), "// WARNING: this resource, size %li is truncated to %li\n",
                        static_cast<long>(len), static_cast<long>(truncsize / 2));
                printCString(out, msg, -1);
                len = truncsize;
            }
            if(U_SUCCESS(*status)) {
                static const char16_t openStr[] = { 0x003A, 0x0062, 0x0069, 0x006E, 0x0061, 0x0072, 0x0079, 0x0020, 0x007B, 0x0020 }; /* ":binary { " */
                static const char16_t closeStr[] = { 0x0020, 0x007D, 0x0020 }; /* " } " */
                printIndent(out, indent);
                if(key != nullptr) {
                    printCString(out, key, -1);
                }
                printString(out, openStr, UPRV_LENGTHOF(openStr));
                for(i = 0; i<len; i++) {
                    printHex(out, *data++);
                }
                printString(out, closeStr, UPRV_LENGTHOF(closeStr));
                if(verbose) {
                    printCString(out, " // BINARY", -1);
                }
                printString(out, cr, UPRV_LENGTHOF(cr));
            } else {
                reportError(pname, status, "getting binary value");
            }
        }
        break;
    case URES_INT_VECTOR :
        {
            int32_t len = 0;
            const int32_t *data = ures_getIntVector(resource, &len, status);
            if(U_SUCCESS(*status)) {
                static const char16_t openStr[] = { 0x003A, 0x0069, 0x006E, 0x0074, 0x0076, 0x0065, 0x0063, 0x0074, 0x006F, 0x0072, 0x0020, 0x007B, 0x0020 }; /* ":intvector { " */
                static const char16_t closeStr[] = { 0x0020, 0x007D, 0x0020 }; /* " } " */
                char16_t num[20];

                printIndent(out, indent);
                if(key != nullptr) {
                    printCString(out, key, -1);
                }
                printString(out, openStr, UPRV_LENGTHOF(openStr));
                for(i = 0; i < len - 1; i++) {
                    int32_t numLen =  uprv_itou(num, 20, data[i], 10, 0);
                    num[numLen++] = 0x002C; /* ',' */
                    num[numLen++] = 0x0020; /* ' ' */
                    num[numLen] = 0;
                    printString(out, num, u_strlen(num));
                }
                if(len > 0) {
                    uprv_itou(num, 20, data[len - 1], 10, 0);
                    printString(out, num, u_strlen(num));
                }
                printString(out, closeStr, UPRV_LENGTHOF(closeStr));
                if(verbose) {
                    printCString(out, "// INTVECTOR", -1);
                }
                printString(out, cr, UPRV_LENGTHOF(cr));
            } else {
                reportError(pname, status, "getting int vector");
            }
      }
      break;
    case URES_TABLE :
    case URES_ARRAY :
        {
            static const char16_t openStr[] = { 0x007B }; /* "{" */
            static const char16_t closeStr[] = { 0x007D, '\n' }; /* "}\n" */

            UResourceBundle *t = nullptr;
            ures_resetIterator(resource);
            printIndent(out, indent);
            if(key != nullptr) {
                printCString(out, key, -1);
            }
            printString(out, openStr, UPRV_LENGTHOF(openStr));
            if(verbose) {
                if(ures_getType(resource) == URES_TABLE) {
                    printCString(out, "// TABLE", -1);
                } else {
                    printCString(out, "// ARRAY", -1);
                }
            }
            printString(out, cr, UPRV_LENGTHOF(cr));

            if(suppressAliases == false) {
              while(U_SUCCESS(*status) && ures_hasNext(resource)) {
                  t = ures_getNextResource(resource, t, status);
                  if(U_SUCCESS(*status)) {
                    printOutBundle(out, t, indent+indentsize, pname, status);
                  } else {
                    reportError(pname, status, "While processing table");
                    *status = U_ZERO_ERROR;
                  }
              }
            } else { /* we have to use low level access to do this */
              Resource r;
              int32_t resSize = ures_getSize(resource);
              UBool isTable = static_cast<UBool>(ures_getType(resource) == URES_TABLE);
              for(i = 0; i < resSize; i++) {
                /* need to know if it's an alias */
                if(isTable) {
                  r = res_getTableItemByIndex(&resource->getResData(), resource->fRes, i, &key);
                } else {
                  r = res_getArrayItem(&resource->getResData(), resource->fRes, i);
                }
                if(U_SUCCESS(*status)) {
                  if(res_getPublicType(r) == URES_ALIAS) {
                    printOutAlias(out, resource, r, key, indent+indentsize, pname, status);
                  } else {
                    t = ures_getByIndex(resource, i, t, status);
                    printOutBundle(out, t, indent+indentsize, pname, status);
                  }
                } else {
                  reportError(pname, status, "While processing table");
                  *status = U_ZERO_ERROR;
                }
              }
            }

            printIndent(out, indent);
            printString(out, closeStr, UPRV_LENGTHOF(closeStr));
            ures_close(t);
        }
        break;
    default:
        break;
    }

}

static const char *getEncodingName(const char *encoding) {
    UErrorCode err;
    const char *enc;

    err = U_ZERO_ERROR;
    if (!(enc = ucnv_getStandardName(encoding, "MIME", &err))) {
        err = U_ZERO_ERROR;
        if (!(enc = ucnv_getStandardName(encoding, "IANA", &err))) {
            // do nothing
        }
    }

    return enc;
}

static void reportError(const char *pname, UErrorCode *status, const char *when) {
  u_fprintf(ustderr, "%s: error %d while %s: %s\n", pname, *status, when, u_errorName(*status));
}

#else
extern int
main(int argc, char* argv[]) {
    /* Changing stdio.h ustdio.h requires that formatting not be disabled. */
    return 3;
}
#endif /* !UCONFIG_NO_FORMATTING */

/*
 * Local Variables:
 * indent-tabs-mode: nil
 * End:
 */

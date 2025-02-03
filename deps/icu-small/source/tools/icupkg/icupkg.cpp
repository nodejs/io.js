// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 2005-2014, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  icupkg.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2005jul29
*   created by: Markus W. Scherer
*
*   This tool operates on ICU data (.dat package) files.
*   It takes one as input, or creates an empty one, and can remove, add, and
*   extract data pieces according to command-line options.
*   At the same time, it swaps each piece to a consistent set of platform
*   properties as desired.
*   Useful as an install-time tool for shipping only one flavor of ICU data
*   and preparing data files for the target platform.
*   Also for customizing ICU data (pruning, augmenting, replacing) and for
*   taking it apart.
*   Subsumes functionality and implementation code from
*   gencmn, decmn, and icuswap tools.
*   Will not work with data DLLs (shared libraries).
*/

#include "unicode/utypes.h"
#include "unicode/putil.h"
#include "cstring.h"
#include "toolutil.h"
#include "uoptions.h"
#include "uparse.h"
#include "filestrm.h"
#include "package.h"
#include "pkg_icu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

U_NAMESPACE_USE

// TODO: add --matchmode=regex for using the ICU regex engine for item name pattern matching?

// general definitions ----------------------------------------------------- ***

// main() ------------------------------------------------------------------ ***

static void
printUsage(const char *pname, UBool isHelp) {
    FILE *where=isHelp ? stdout : stderr;

    fprintf(where,
            "%csage: %s [-h|-?|--help ] [-tl|-tb|-te] [-c] [-C comment]\n"
            "\t[-a list] [-r list] [-x list] [-l [-o outputListFileName]]\n"
            "\t[-s path] [-d path] [-w] [-m mode]\n"
            "\t[--ignore-deps]\n"
            "\t[--auto_toc_prefix] [--auto_toc_prefix_with_type] [--toc_prefix]\n"
            "\tinfilename [outfilename]\n",
            isHelp ? 'U' : 'u', pname);
    if(isHelp) {
        fprintf(where,
            "\n"
            "Read the input ICU .dat package file, modify it according to the options,\n"
            "swap it to the desired platform properties (charset & endianness),\n"
            "and optionally write the resulting ICU .dat package to the output file.\n"
            "Items are removed, then added, then extracted and listed.\n"
            "An ICU .dat package is written if items are removed or added,\n"
            "or if the input and output filenames differ,\n"
            "or if the --writepkg (-w) option is set.\n");
        fprintf(where,
            "\n"
            "If the input filename is \"new\" then an empty package is created.\n"
            "If the output filename is missing, then it is automatically generated\n"
            "from the input filename: If the input filename ends with an l, b, or e\n"
            "matching its platform properties, then the output filename will\n"
            "contain the letter from the -t (--type) option.\n");
        fprintf(where,
            "\n"
            "This tool can also be used to just swap a single ICU data file, replacing the\n"
            "former icuswap tool. For this mode, provide the infilename (and optional\n"
            "outfilename) for a non-package ICU data file.\n"
            "Allowed options include -t, -w, -s and -d.\n"
            "The filenames can be absolute, or relative to the source/dest dir paths.\n"
            "Other options are not allowed in this mode.\n");
        fprintf(where,
            "\n"
            "Options:\n"
            "\t(Only the last occurrence of an option is used.)\n"
            "\n"
            "\t-h or -? or --help    print this message and exit\n");
        fprintf(where,
            "\n"
            "\t-tl or --type l   output for little-endian/ASCII charset family\n"
            "\t-tb or --type b   output for big-endian/ASCII charset family\n"
            "\t-te or --type e   output for big-endian/EBCDIC charset family\n"
            "\t                  The output type defaults to the input type.\n"
            "\n"
            "\t-c or --copyright include the ICU copyright notice\n"
            "\t-C comment or --comment comment   include a comment string\n");
        fprintf(where,
            "\n"
            "\t-a list or --add list      add items to the package\n"
            "\t-r list or --remove list   remove items from the package\n"
            "\t-x list or --extract list  extract items from the package\n"
            "\tThe list can be a single item's filename,\n"
            "\tor a .txt filename with a list of item filenames,\n"
            "\tor an ICU .dat package filename.\n");
        fprintf(where,
            "\n"
            "\t-w or --writepkg  write the output package even if no items are removed\n"
            "\t                  or added (e.g., for only swapping the data)\n");
        fprintf(where,
            "\n"
            "\t-m mode or --matchmode mode  set the matching mode for item names with\n"
            "\t                             wildcards\n"
            "\t        noslash: the '*' wildcard does not match the '/' tree separator\n");
        fprintf(where,
            "\n"
            "\t--ignore-deps     Do not fail if not all resource dependencies are met. Use this\n"
            "\t                  option if the missing resources come from another source.");
        fprintf(where,
            "\n"
            "\tIn the .dat package, the Table of Contents (ToC) contains an entry\n"
            "\tfor each item of the form prefix/tree/itemname .\n"
            "\tThe prefix normally matches the package basename, and icupkg checks that,\n"
            "\tbut this is not necessary when ICU need not find and load the package by filename.\n"
            "\tICU package names end with the platform type letter, and thus differ\n"
            "\tbetween platform types. This is not required for user data packages.\n");
        fprintf(where,
            "\n"
            "\t--auto_toc_prefix            automatic ToC entries prefix\n"
            "\t                             Uses the prefix of the first entry of the\n"
            "\t                             input package, rather than its basename.\n"
            "\t                             Requires a non-empty input package.\n"
            "\t--auto_toc_prefix_with_type  auto_toc_prefix + adjust platform type\n"
            "\t                             Same as auto_toc_prefix but also checks that\n"
            "\t                             the prefix ends with the input platform\n"
            "\t                             type letter, and modifies it to the output\n"
            "\t                             platform type letter.\n"
            "\t                At most one of the auto_toc_prefix options\n"
            "\t                can be used at a time.\n"
            "\t--toc_prefix prefix          ToC prefix to be used in the output package\n"
            "\t                             Overrides the package basename\n"
            "\t                             and --auto_toc_prefix.\n"
            "\t                             Cannot be combined with --auto_toc_prefix_with_type.\n");
        /*
         * Usage text columns, starting after the initial TAB.
         *      1         2         3         4         5         6         7         8
         *     901234567890123456789012345678901234567890123456789012345678901234567890
         */
        fprintf(where,
            "\n"
            "\tList file syntax: Items are listed on one or more lines and separated\n"
            "\tby whitespace (space+tab).\n"
            "\tComments begin with # and are ignored. Empty lines are ignored.\n"
            "\tLines where the first non-whitespace character is one of %s\n"
            "\tare also ignored, to reserve for future syntax.\n",
            U_PKG_RESERVED_CHARS);
        fprintf(where,
            "\tItems for removal or extraction may contain a single '*' wildcard\n"
            "\tcharacter. The '*' matches zero or more characters.\n"
            "\tIf --matchmode noslash (-m noslash) is set, then the '*'\n"
            "\tdoes not match '/'.\n");
        fprintf(where,
            "\n"
            "\tItems must be listed relative to the package, and the --sourcedir or\n"
            "\tthe --destdir path will be prepended.\n"
            "\tThe paths are only prepended to item filenames while adding or\n"
            "\textracting items, not to ICU .dat package or list filenames.\n"
            "\t\n"
            "\tPaths may contain '/' instead of the platform's\n"
            "\tfile separator character, and are converted as appropriate.\n");
        fprintf(where,
            "\n"
            "\t-s path or --sourcedir path  directory for the --add items\n"
            "\t-d path or --destdir path    directory for the --extract items\n"
            "\n"
            "\t-l or --list                 list the package items\n"
            "\t                             (after modifying the package)\n"
            "\t                             to stdout or to output list file\n"
            "\t-o path or --outlist path    path/filename for the --list output\n");
    }
}

static UOption options[]={
    UOPTION_HELP_H,
    UOPTION_HELP_QUESTION_MARK,
    UOPTION_DEF("type", 't', UOPT_REQUIRES_ARG),

    UOPTION_COPYRIGHT,
    UOPTION_DEF("comment", 'C', UOPT_REQUIRES_ARG),

    UOPTION_SOURCEDIR,
    UOPTION_DESTDIR,

    UOPTION_DEF("writepkg", 'w', UOPT_NO_ARG),

    UOPTION_DEF("matchmode", 'm', UOPT_REQUIRES_ARG),

    UOPTION_DEF("ignore-deps", '\1', UOPT_NO_ARG),

    UOPTION_DEF("add", 'a', UOPT_REQUIRES_ARG),
    UOPTION_DEF("remove", 'r', UOPT_REQUIRES_ARG),
    UOPTION_DEF("extract", 'x', UOPT_REQUIRES_ARG),

    UOPTION_DEF("list", 'l', UOPT_NO_ARG),
    UOPTION_DEF("outlist", 'o', UOPT_REQUIRES_ARG),

    UOPTION_DEF("auto_toc_prefix", '\1', UOPT_NO_ARG),
    UOPTION_DEF("auto_toc_prefix_with_type", '\1', UOPT_NO_ARG),
    UOPTION_DEF("toc_prefix", '\1', UOPT_REQUIRES_ARG)
};

enum {
    OPT_HELP_H,
    OPT_HELP_QUESTION_MARK,
    OPT_OUT_TYPE,

    OPT_COPYRIGHT,
    OPT_COMMENT,

    OPT_SOURCEDIR,
    OPT_DESTDIR,

    OPT_WRITEPKG,

    OPT_MATCHMODE,

    OPT_IGNORE_DEPS,

    OPT_ADD_LIST,
    OPT_REMOVE_LIST,
    OPT_EXTRACT_LIST,

    OPT_LIST_ITEMS,
    OPT_LIST_FILE,

    OPT_AUTO_TOC_PREFIX,
    OPT_AUTO_TOC_PREFIX_WITH_TYPE,
    OPT_TOC_PREFIX,

    OPT_COUNT
};

static UBool
isPackageName(const char *filename) {
    int32_t len;

    len = static_cast<int32_t>(strlen(filename)) - 4; /* -4: subtract the length of ".dat" */
    return len > 0 && 0 == strcmp(filename + len, ".dat");
}
/*
This line is required by MinGW because it incorrectly globs the arguments.
So when \* is used, it turns into a list of files instead of a literal "*"
*/
int _CRT_glob = 0;

extern int
main(int argc, char *argv[]) {
    const char *pname, *sourcePath, *destPath, *inFilename, *outFilename, *outComment;
    char outType;
    UBool isHelp, isModified, isPackage;
    int result = 0;

    Package *pkg, *listPkg, *addListPkg;

    U_MAIN_INIT_ARGS(argc, argv);

    /* get the program basename */
    pname=findBasename(argv[0]);

    argc=u_parseArgs(argc, argv, UPRV_LENGTHOF(options), options);
    isHelp=options[OPT_HELP_H].doesOccur || options[OPT_HELP_QUESTION_MARK].doesOccur;
    if(isHelp) {
        printUsage(pname, true);
        return U_ZERO_ERROR;
    }

    pkg=new Package;
    if(pkg==nullptr) {
        fprintf(stderr, "icupkg: not enough memory\n");
        return U_MEMORY_ALLOCATION_ERROR;
    }
    isModified=false;

    int autoPrefix=0;
    if(options[OPT_AUTO_TOC_PREFIX].doesOccur) {
        pkg->setAutoPrefix();
        ++autoPrefix;
    }
    if(options[OPT_AUTO_TOC_PREFIX_WITH_TYPE].doesOccur) {
        if(options[OPT_TOC_PREFIX].doesOccur) {
            fprintf(stderr, "icupkg: --auto_toc_prefix_with_type and also --toc_prefix\n");
            printUsage(pname, false);
            return U_ILLEGAL_ARGUMENT_ERROR;
        }
        pkg->setAutoPrefixWithType();
        ++autoPrefix;
    }
    if(argc<2 || 3<argc || autoPrefix>1) {
        printUsage(pname, false);
        return U_ILLEGAL_ARGUMENT_ERROR;
    }

    if(options[OPT_SOURCEDIR].doesOccur) {
        sourcePath=options[OPT_SOURCEDIR].value;
    } else {
        // work relative to the current working directory
        sourcePath=nullptr;
    }
    if(options[OPT_DESTDIR].doesOccur) {
        destPath=options[OPT_DESTDIR].value;
    } else {
        // work relative to the current working directory
        destPath=nullptr;
    }

    if(0==strcmp(argv[1], "new")) {
        if(autoPrefix) {
            fprintf(stderr, "icupkg: --auto_toc_prefix[_with_type] but no input package\n");
            printUsage(pname, false);
            return U_ILLEGAL_ARGUMENT_ERROR;
        }
        inFilename=nullptr;
        isPackage=true;
    } else {
        inFilename=argv[1];
        if(isPackageName(inFilename)) {
            pkg->readPackage(inFilename);
            isPackage=true;
        } else {
            /* swap a single file (icuswap replacement) rather than work on a package */
            pkg->addFile(sourcePath, inFilename);
            isPackage=false;
        }
    }

    if(argc>=3) {
        outFilename=argv[2];
        if(0!=strcmp(argv[1], argv[2])) {
            isModified=true;
        }
    } else if(isPackage) {
        outFilename=nullptr;
    } else /* !isPackage */ {
        outFilename=inFilename;
        isModified = static_cast<UBool>(sourcePath != destPath);
    }

    /* parse the output type option */
    if(options[OPT_OUT_TYPE].doesOccur) {
        const char *type=options[OPT_OUT_TYPE].value;
        if(type[0]==0 || type[1]!=0) {
            /* the type must be exactly one letter */
            printUsage(pname, false);
            return U_ILLEGAL_ARGUMENT_ERROR;
        }
        outType=type[0];
        switch(outType) {
        case 'l':
        case 'b':
        case 'e':
            break;
        default:
            printUsage(pname, false);
            return U_ILLEGAL_ARGUMENT_ERROR;
        }

        /*
         * Set the isModified flag if the output type differs from the
         * input package type.
         * If we swap a single file, just assume that we are modifying it.
         * The Package class does not give us access to the item and its type.
         */
        isModified |= static_cast<UBool>(!isPackage || outType != pkg->getInType());
    } else if(isPackage) {
        outType=pkg->getInType(); // default to input type
    } else /* !isPackage: swap single file */ {
        outType=0; /* tells extractItem() to not swap */
    }

    if(options[OPT_WRITEPKG].doesOccur) {
        isModified=true;
    }

    if(!isPackage) {
        /*
         * icuswap tool replacement: Only swap a single file.
         * Check that irrelevant options are not set.
         */
        if( options[OPT_COMMENT].doesOccur ||
            options[OPT_COPYRIGHT].doesOccur ||
            options[OPT_MATCHMODE].doesOccur ||
            options[OPT_REMOVE_LIST].doesOccur ||
            options[OPT_ADD_LIST].doesOccur ||
            options[OPT_EXTRACT_LIST].doesOccur ||
            options[OPT_LIST_ITEMS].doesOccur
        ) {
            printUsage(pname, false);
            return U_ILLEGAL_ARGUMENT_ERROR;
        }
        if(isModified) {
            pkg->extractItem(destPath, outFilename, 0, outType);
        }

        delete pkg;
        return result;
    }

    /* Work with a package. */

    if(options[OPT_COMMENT].doesOccur) {
        outComment=options[OPT_COMMENT].value;
    } else if(options[OPT_COPYRIGHT].doesOccur) {
        outComment=U_COPYRIGHT_STRING;
    } else {
        outComment=nullptr;
    }

    if(options[OPT_MATCHMODE].doesOccur) {
        if(0==strcmp(options[OPT_MATCHMODE].value, "noslash")) {
            pkg->setMatchMode(Package::MATCH_NOSLASH);
        } else {
            printUsage(pname, false);
            return U_ILLEGAL_ARGUMENT_ERROR;
        }
    }

    /* remove items */
    if(options[OPT_REMOVE_LIST].doesOccur) {
        listPkg=new Package();
        if(listPkg==nullptr) {
            fprintf(stderr, "icupkg: not enough memory\n");
            exit(U_MEMORY_ALLOCATION_ERROR);
        }
        if(readList(nullptr, options[OPT_REMOVE_LIST].value, false, listPkg)) {
            pkg->removeItems(*listPkg);
            delete listPkg;
            isModified=true;
        } else {
            printUsage(pname, false);
            return U_ILLEGAL_ARGUMENT_ERROR;
        }
    }

    /*
     * add items
     * use a separate Package so that its memory and items stay around
     * as long as the main Package
     */
    addListPkg=nullptr;
    if(options[OPT_ADD_LIST].doesOccur) {
        addListPkg=new Package();
        if(addListPkg==nullptr) {
            fprintf(stderr, "icupkg: not enough memory\n");
            exit(U_MEMORY_ALLOCATION_ERROR);
        }
        if(readList(sourcePath, options[OPT_ADD_LIST].value, true, addListPkg)) {
            pkg->addItems(*addListPkg);
            // delete addListPkg; deferred until after writePackage()
            isModified=true;
        } else {
            printUsage(pname, false);
            return U_ILLEGAL_ARGUMENT_ERROR;
        }
    }

    /* extract items */
    if(options[OPT_EXTRACT_LIST].doesOccur) {
        listPkg=new Package();
        if(listPkg==nullptr) {
            fprintf(stderr, "icupkg: not enough memory\n");
            exit(U_MEMORY_ALLOCATION_ERROR);
        }
        if(readList(nullptr, options[OPT_EXTRACT_LIST].value, false, listPkg)) {
            pkg->extractItems(destPath, *listPkg, outType);
            delete listPkg;
        } else {
            printUsage(pname, false);
            return U_ILLEGAL_ARGUMENT_ERROR;
        }
    }

    /* list items */
    if(options[OPT_LIST_ITEMS].doesOccur) {
        int32_t i;
        if (options[OPT_LIST_FILE].doesOccur) {
            FileStream *out;
            out = T_FileStream_open(options[OPT_LIST_FILE].value, "w");
            if (out != nullptr) {
                for(i=0; i<pkg->getItemCount(); ++i) {
                    T_FileStream_writeLine(out, pkg->getItem(i)->name);
                    T_FileStream_writeLine(out, "\n");
                }
                T_FileStream_close(out);
            } else {
                return U_ILLEGAL_ARGUMENT_ERROR;
            }
        } else {
            for(i=0; i<pkg->getItemCount(); ++i) {
                fprintf(stdout, "%s\n", pkg->getItem(i)->name);
            }
        }
    }

    /* check dependencies between items */
    if(!options[OPT_IGNORE_DEPS].doesOccur && !pkg->checkDependencies()) {
        /* some dependencies are not fulfilled */
        return U_MISSING_RESOURCE_ERROR;
    }

    /* write the output .dat package if there are any modifications */
    if(isModified) {
        char outFilenameBuffer[1024]; // for auto-generated output filename, if necessary

        if(outFilename==nullptr || outFilename[0]==0) {
            if(inFilename==nullptr || inFilename[0]==0) {
                fprintf(stderr, "icupkg: unable to auto-generate an output filename if there is no input filename\n");
                exit(U_ILLEGAL_ARGUMENT_ERROR);
            }

            /*
             * auto-generate a filename:
             * copy the inFilename,
             * and if the last basename character matches the input file's type,
             * then replace it with the output file's type
             */
            char suffix[6]="?.dat";
            char *s;

            suffix[0]=pkg->getInType();
            strcpy(outFilenameBuffer, inFilename);
            s=strchr(outFilenameBuffer, 0);
            if((s-outFilenameBuffer)>5 && 0==memcmp(s-5, suffix, 5)) {
                *(s-5)=outType;
            }
            outFilename=outFilenameBuffer;
        }
        if(options[OPT_TOC_PREFIX].doesOccur) {
            pkg->setPrefix(options[OPT_TOC_PREFIX].value);
        }
        result = writePackageDatFile(outFilename, outComment, nullptr, nullptr, pkg, outType);
    }

    delete addListPkg;
    delete pkg;
    return result;
}

/*
 * Hey, Emacs, please set the following:
 *
 * Local Variables:
 * indent-tabs-mode: nil
 * End:
 *
 */

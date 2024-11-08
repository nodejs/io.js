// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 *******************************************************************************
 *   Copyright (C) 1999-2016, International Business Machines
 *   Corporation and others.  All Rights Reserved.
 *******************************************************************************
 *   file name:  gennames.c
 *   encoding:   UTF-8
 *   tab size:   8 (not used)
 *   indentation:4
 *
 *   created on: 1999nov01
 *   created by: Markus W. Scherer
 *
 *   This program reads a binary file and creates a C source code file
 *   with a byte array that contains the data of the binary file.
 *
 *   12/09/1999  weiv    Added multiple file handling
 */

#include "unicode/utypes.h"

#if U_PLATFORM_HAS_WIN32_API
#   define VC_EXTRALEAN
#   define WIN32_LEAN_AND_MEAN
#   define NOUSER
#   define NOSERVICE
#   define NOIME
#   define NOMCX
#include <windows.h>
#include <time.h>
#endif

#if U_PLATFORM_IS_LINUX_BASED && U_HAVE_ELF_H
#   define U_ELF
#endif

#ifdef U_ELF
#   include <elf.h>
#   if defined(ELFCLASS64)
#       define U_ELF64
#   endif
    /* Old elf.h headers may not have EM_X86_64, or have EM_X8664 instead. */
#   ifndef EM_X86_64
#       define EM_X86_64 62
#   endif
#   define ICU_ENTRY_OFFSET 0
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "unicode/putil.h"
#include "cmemory.h"
#include "cstring.h"
#include "filestrm.h"
#include "toolutil.h"
#include "unicode/uclean.h"
#include "uoptions.h"
#include "pkg_genc.h"

enum {
  kOptHelpH = 0,
  kOptHelpQuestionMark,
  kOptDestDir,
  kOptQuiet,
  kOptName,
  kOptEntryPoint,
#ifdef CAN_GENERATE_OBJECTS
  kOptObject,
  kOptMatchArch,
  kOptCpuArch,
  kOptSkipDllExport,
#endif
  kOptFilename,
  kOptAssembly
};

static UOption options[]={
/*0*/UOPTION_HELP_H,
     UOPTION_HELP_QUESTION_MARK,
     UOPTION_DESTDIR,
     UOPTION_QUIET,
     UOPTION_DEF("name", 'n', UOPT_REQUIRES_ARG),
     UOPTION_DEF("entrypoint", 'e', UOPT_REQUIRES_ARG),
#ifdef CAN_GENERATE_OBJECTS
/*6*/UOPTION_DEF("object", 'o', UOPT_NO_ARG),
     UOPTION_DEF("match-arch", 'm', UOPT_REQUIRES_ARG),
     UOPTION_DEF("cpu-arch", 'c', UOPT_REQUIRES_ARG),
     UOPTION_DEF("skip-dll-export", '\0', UOPT_NO_ARG),
#endif
     UOPTION_DEF("filename", 'f', UOPT_REQUIRES_ARG),
     UOPTION_DEF("assembly", 'a', UOPT_REQUIRES_ARG)
};

#define CALL_WRITECCODE     'c'
#define CALL_WRITEASSEMBLY  'a'
#define CALL_WRITEOBJECT    'o'
extern int
main(int argc, char* argv[]) {
    UBool verbose = true;
    char writeCode;

    U_MAIN_INIT_ARGS(argc, argv);

    options[kOptDestDir].value = ".";

    /* read command line options */
    argc=u_parseArgs(argc, argv, UPRV_LENGTHOF(options), options);

    /* error handling, printing usage message */
    if(argc<0) {
        fprintf(stderr,
            "error in command line argument \"%s\"\n",
            argv[-argc]);
    }
    if(argc<0 || options[kOptHelpH].doesOccur || options[kOptHelpQuestionMark].doesOccur) {
        fprintf(stderr,
            "usage: %s [-options] filename1 filename2 ...\n"
            "\tread each binary input file and \n"
            "\tcreate a .c file with a byte array that contains the input file's data\n"
            "options:\n"
            "\t-h or -? or --help  this usage text\n"
            "\t-d or --destdir     destination directory, followed by the path\n"
            "\t-q or --quiet       do not display warnings and progress\n"
            "\t-n or --name        symbol prefix, followed by the prefix\n"
            "\t-e or --entrypoint  entry point name, followed by the name (_dat will be appended)\n"
            "\t-r or --revision    Specify a version\n"
            , argv[0]);
#ifdef CAN_GENERATE_OBJECTS
        fprintf(stderr,
            "\t-o or --object      write a .obj file instead of .c\n"
            "\t-m or --match-arch file.o  match the architecture (CPU, 32/64 bits) of the specified .o\n"
            "\t                    ELF format defaults to i386. Windows defaults to the native platform.\n"
            "\t-c or --cpu-arch    Specify a CPU architecture for which to write a .obj file for ClangCL on Windows\n"
            "\t                    Valid values for this opton are x64, x86 and arm64.\n"
            "\t--skip-dll-export   Don't export the ICU data entry point symbol (for use when statically linking)\n");
#endif
        fprintf(stderr,
            "\t-f or --filename    Specify an alternate base filename. (default: symbolname_typ)\n"
            "\t-a or --assembly    Create assembly file. (possible values are: ");

        printAssemblyHeadersToStdErr();
    } else {
        const char *message, *filename;
        /* TODO: remove void (*writeCode)(const char *, const char *); */

        if(options[kOptAssembly].doesOccur) {
            message="generating assembly code for %s\n";
            writeCode = CALL_WRITEASSEMBLY;
            /* TODO: remove writeCode=&writeAssemblyCode; */

            if (!checkAssemblyHeaderName(options[kOptAssembly].value)) {
                fprintf(stderr,
                    "Assembly type \"%s\" is unknown.\n", options[kOptAssembly].value);
                return -1;
            }
        }
#ifdef CAN_GENERATE_OBJECTS
        else if(options[kOptObject].doesOccur) {
            message="generating object code for %s\n";
            writeCode = CALL_WRITEOBJECT;
            /* TODO: remove writeCode=&writeObjectCode; */
        }
#endif
        else
        {
            message="generating C code for %s\n";
            writeCode = CALL_WRITECCODE;
            /* TODO: remove writeCode=&writeCCode; */
        }
        if (options[kOptQuiet].doesOccur) {
            verbose = false;
        }
        while(--argc) {
            filename=getLongPathname(argv[argc]);
            if (verbose) {
                fprintf(stdout, message, filename);
            }

            switch (writeCode) {
            case CALL_WRITECCODE:
                writeCCode(filename, options[kOptDestDir].value,
                           options[kOptEntryPoint].doesOccur ? options[kOptEntryPoint].value : NULL,
                           options[kOptName].doesOccur ? options[kOptName].value : NULL,
                           options[kOptFilename].doesOccur ? options[kOptFilename].value : NULL,
                           NULL,
                           0);
                break;
            case CALL_WRITEASSEMBLY:
                writeAssemblyCode(filename, options[kOptDestDir].value,
                                  options[kOptEntryPoint].doesOccur ? options[kOptEntryPoint].value : NULL,
                                  options[kOptFilename].doesOccur ? options[kOptFilename].value : NULL,
                                  NULL,
                                  0);
                break;
#ifdef CAN_GENERATE_OBJECTS
            case CALL_WRITEOBJECT:
                if(options[kOptCpuArch].doesOccur) {
                    if (!checkCpuArchitecture(options[kOptCpuArch].value)) {
                        fprintf(stderr,
                            "CPU architecture \"%s\" is unknown.\n", options[kOptCpuArch].value);
                        return -1;
                    }
                }
                writeObjectCode(filename, options[kOptDestDir].value,
                                options[kOptEntryPoint].doesOccur ? options[kOptEntryPoint].value : NULL,
                                options[kOptMatchArch].doesOccur ? options[kOptMatchArch].value : NULL,
                                options[kOptCpuArch].doesOccur ? options[kOptCpuArch].value : NULL,
                                options[kOptFilename].doesOccur ? options[kOptFilename].value : NULL,
                                NULL,
                                0,
                                !options[kOptSkipDllExport].doesOccur);
                break;
#endif
            default:
                /* Should never occur. */
                break;
            }
            /* TODO: remove writeCode(filename, options[kOptDestDir].value); */
        }
    }

    return 0;
}

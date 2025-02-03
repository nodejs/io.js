// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/******************************************************************************
 *   Copyright (C) 2000-2016, International Business Machines
 *   Corporation and others.  All Rights Reserved.
 *******************************************************************************
 *   file name:  pkgdata.cpp
 *   encoding:   ANSI X3.4 (1968)
 *   tab size:   8 (not used)
 *   indentation:4
 *
 *   created on: 2000may15
 *   created by: Steven \u24C7 Loomis
 *
 *   This program packages the ICU data into different forms
 *   (DLL, common data, etc.)
 */

// Defines _XOPEN_SOURCE for access to POSIX functions.
// Must be before any other #includes.
#include "uposixdefs.h"

#include "unicode/utypes.h"

#include "unicode/putil.h"
#include "putilimp.h"

#if U_HAVE_POPEN
#if (U_PF_MINGW <= U_PLATFORM && U_PLATFORM <= U_PF_CYGWIN) && defined(__STRICT_ANSI__)
/* popen/pclose aren't defined in strict ANSI on Cygwin and MinGW */
#undef __STRICT_ANSI__
#endif
#endif

#include "cmemory.h"
#include "cstring.h"
#include "filestrm.h"
#include "toolutil.h"
#include "unicode/uclean.h"
#include "unewdata.h"
#include "uoptions.h"
#include "package.h"
#include "pkg_icu.h"
#include "pkg_genc.h"
#include "pkg_gencmn.h"
#include "flagparser.h"
#include "filetools.h"
#include "charstr.h"
#include "uassert.h"

#if U_HAVE_POPEN
# include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>

U_CDECL_BEGIN
#include "pkgtypes.h"
U_CDECL_END

#if U_HAVE_POPEN
U_NAMESPACE_BEGIN
U_DEFINE_LOCAL_OPEN_POINTER(LocalPipeFilePointer, FILE, pclose);
U_NAMESPACE_END
#endif

using icu::LocalMemory;

static void loadLists(UPKGOptions *o, UErrorCode *status);

static int32_t pkg_executeOptions(UPKGOptions *o);

#ifdef WINDOWS_WITH_MSVC
static int32_t pkg_createWindowsDLL(const char mode, const char *gencFilePath, UPKGOptions *o);
#endif
static int32_t pkg_createSymLinks(const char *targetDir, UBool specialHandling=false);
static int32_t pkg_installLibrary(const char *installDir, const char *dir, UBool noVersion);
static int32_t pkg_installFileMode(const char *installDir, const char *srcDir, const char *fileListName);
static int32_t pkg_installCommonMode(const char *installDir, const char *fileName);

#ifdef BUILD_DATA_WITHOUT_ASSEMBLY
static int32_t pkg_createWithoutAssemblyCode(UPKGOptions *o, const char *targetDir, const char mode);
#endif

#ifdef CAN_WRITE_OBJ_CODE
static void pkg_createOptMatchArch(char *optMatchArch);
static void pkg_destroyOptMatchArch(char *optMatchArch);
#endif

static int32_t pkg_createWithAssemblyCode(const char *targetDir, const char mode, const char *gencFilePath);
static int32_t pkg_generateLibraryFile(const char *targetDir, const char mode, const char *objectFile, char *command = nullptr, UBool specialHandling=false);
static int32_t pkg_archiveLibrary(const char *targetDir, const char *version, UBool reverseExt);
static void createFileNames(UPKGOptions *o, const char mode, const char *version_major, const char *version, const char *libName, const UBool reverseExt, UBool noVersion);
static int32_t initializePkgDataFlags(UPKGOptions *o);

static int32_t pkg_getPkgDataPath(UBool verbose, UOption *option);
static int runCommand(const char* command, UBool specialHandling=false);

#define IN_COMMON_MODE(mode) (mode == 'a' || mode == 'c')
#define IN_DLL_MODE(mode)    (mode == 'd' || mode == 'l')
#define IN_STATIC_MODE(mode) (mode == 's')
#define IN_FILES_MODE(mode)  (mode == 'f')

enum {
    NAME,
    BLDOPT,
    MODE,
    HELP,
    HELP_QUESTION_MARK,
    VERBOSE,
    COPYRIGHT,
    COMMENT,
    DESTDIR,
    REBUILD,
    TEMPDIR,
    INSTALL,
    SOURCEDIR,
    ENTRYPOINT,
    REVISION,
    FORCE_PREFIX,
    LIBNAME,
    QUIET,
    WITHOUT_ASSEMBLY,
    PDS_BUILD,
    WIN_UWP_BUILD,
    WIN_DLL_ARCH,
    WIN_DYNAMICBASE
};

/* This sets the modes that are available */
static struct {
    const char *name, *alt_name;
    const char *desc;
} modes[] = {
        { "files", nullptr,     "Uses raw data files (no effect). Installation copies all files to the target location." },
#if U_PLATFORM_HAS_WIN32_API
        { "dll",    "library",  "Generates one common data file and one shared library, <package>.dll"},
        { "common", "archive",  "Generates just the common file, <package>.dat"},
        { "static", "static",   "Generates one statically linked library, " LIB_PREFIX "<package>" UDATA_LIB_SUFFIX }
#else
#ifdef UDATA_SO_SUFFIX
        { "dll",    "library",  "Generates one shared library, <package>" UDATA_SO_SUFFIX },
#endif
        { "common", "archive",  "Generates one common data file, <package>.dat" },
        { "static", "static",   "Generates one statically linked library, " LIB_PREFIX "<package>" UDATA_LIB_SUFFIX }
#endif
};

static UOption options[]={
    /*00*/    UOPTION_DEF( "name",    'p', UOPT_REQUIRES_ARG),
    /*01*/    UOPTION_DEF( "bldopt",  'O', UOPT_REQUIRES_ARG), /* on Win32 it is release or debug */
    /*02*/    UOPTION_DEF( "mode",    'm', UOPT_REQUIRES_ARG),
    /*03*/    UOPTION_HELP_H,                                   /* -h */
    /*04*/    UOPTION_HELP_QUESTION_MARK,                       /* -? */
    /*05*/    UOPTION_VERBOSE,                                  /* -v */
    /*06*/    UOPTION_COPYRIGHT,                                /* -c */
    /*07*/    UOPTION_DEF( "comment", 'C', UOPT_REQUIRES_ARG),
    /*08*/    UOPTION_DESTDIR,                                  /* -d */
    /*11*/    UOPTION_DEF( "rebuild", 'F', UOPT_NO_ARG),
    /*12*/    UOPTION_DEF( "tempdir", 'T', UOPT_REQUIRES_ARG),
    /*13*/    UOPTION_DEF( "install", 'I', UOPT_REQUIRES_ARG),
    /*14*/    UOPTION_SOURCEDIR ,
    /*15*/    UOPTION_DEF( "entrypoint", 'e', UOPT_REQUIRES_ARG),
    /*16*/    UOPTION_DEF( "revision", 'r', UOPT_REQUIRES_ARG),
    /*17*/    UOPTION_DEF( "force-prefix", 'f', UOPT_NO_ARG),
    /*18*/    UOPTION_DEF( "libname", 'L', UOPT_REQUIRES_ARG),
    /*19*/    UOPTION_DEF( "quiet", 'q', UOPT_NO_ARG),
    /*20*/    UOPTION_DEF( "without-assembly", 'w', UOPT_NO_ARG),
    /*21*/    UOPTION_DEF("zos-pds-build", 'z', UOPT_NO_ARG),
    /*22*/    UOPTION_DEF("windows-uwp-build", 'u', UOPT_NO_ARG),
    /*23*/    UOPTION_DEF("windows-DLL-arch", 'a', UOPT_REQUIRES_ARG),
    /*24*/    UOPTION_DEF("windows-dynamicbase", 'b', UOPT_NO_ARG),
};

/* This enum and the following char array should be kept in sync. */
enum {
    GENCCODE_ASSEMBLY_TYPE,
    SO_EXT,
    SOBJ_EXT,
    A_EXT,
    LIBPREFIX,
    LIB_EXT_ORDER,
    COMPILER,
    LIBFLAGS,
    GENLIB,
    LDICUDTFLAGS,
    LD_SONAME,
    RPATH_FLAGS,
    BIR_FLAGS,
    AR,
    ARFLAGS,
    RANLIB,
    INSTALL_CMD,
    PKGDATA_FLAGS_SIZE
};
static const char* FLAG_NAMES[PKGDATA_FLAGS_SIZE] = {
        "GENCCODE_ASSEMBLY_TYPE",
        "SO",
        "SOBJ",
        "A",
        "LIBPREFIX",
        "LIB_EXT_ORDER",
        "COMPILE",
        "LIBFLAGS",
        "GENLIB",
        "LDICUDTFLAGS",
        "LD_SONAME",
        "RPATH_FLAGS",
        "BIR_LDFLAGS",
        "AR",
        "ARFLAGS",
        "RANLIB",
        "INSTALL_CMD"
};
static char **pkgDataFlags = nullptr;

enum {
    LIB_FILE,
    LIB_FILE_VERSION_MAJOR,
    LIB_FILE_VERSION,
    LIB_FILE_VERSION_TMP,
#if U_PLATFORM == U_PF_CYGWIN
    LIB_FILE_CYGWIN,
    LIB_FILE_CYGWIN_VERSION,
#elif U_PLATFORM == U_PF_MINGW
    LIB_FILE_MINGW,
#elif U_PLATFORM == U_PF_OS390
    LIB_FILE_OS390BATCH_MAJOR,
    LIB_FILE_OS390BATCH_VERSION,
#endif
    LIB_FILENAMES_SIZE
};
static char libFileNames[LIB_FILENAMES_SIZE][256];

static UPKGOptions  *pkg_checkFlag(UPKGOptions *o);

const char options_help[][320]={
    "Set the data name",
#ifdef U_MAKE_IS_NMAKE
    "The directory where the ICU is located (e.g. <ICUROOT> which contains the bin directory)",
#else
    "Specify options for the builder.",
#endif
    "Specify the mode of building (see below; default: common)",
    "This usage text",
    "This usage text",
    "Make the output verbose",
    "Use the standard ICU copyright",
    "Use a custom comment (instead of the copyright)",
    "Specify the destination directory for files",
    "Force rebuilding of all data",
    "Specify temporary dir (default: output dir)",
    "Install the data (specify target)",
    "Specify a custom source directory",
    "Specify a custom entrypoint name (default: short name)",
    "Specify a version when packaging in dll or static mode",
    "Add package to all file names if not present",
    "Library name to build (if different than package name)",
    "Quiet mode. (e.g. Do not output a readme file for static libraries)",
    "Build the data without assembly code",
    "Build PDS dataset (zOS build only)",
    "Build for Universal Windows Platform (Windows build only)",
    "Specify the DLL machine architecture for LINK.exe (Windows build only)",
    "Ignored. Enable DYNAMICBASE on the DLL. This is now the default. (Windows build only)",
};

const char  *progname = "PKGDATA";

int
main(int argc, char* argv[]) {
    int result = 0;
    /* FileStream  *out; */
    UPKGOptions  o;
    CharList    *tail;
    UBool        needsHelp = false;
    UErrorCode   status = U_ZERO_ERROR;
    /* char         tmp[1024]; */
    uint32_t i;
    int32_t n;

    U_MAIN_INIT_ARGS(argc, argv);

    progname = argv[0];

    options[MODE].value = "common";

    /* read command line options */
    argc=u_parseArgs(argc, argv, UPRV_LENGTHOF(options), options);

    /* error handling, printing usage message */
    /* I've decided to simply print an error and quit. This tool has too
    many options to just display them all of the time. */

    if(options[HELP].doesOccur || options[HELP_QUESTION_MARK].doesOccur) {
        needsHelp = true;
    }
    else {
        if(!needsHelp && argc<0) {
            fprintf(stderr,
                "%s: error in command line argument \"%s\"\n",
                progname,
                argv[-argc]);
            fprintf(stderr, "Run '%s --help' for help.\n", progname);
            return 1;
        }


#if !defined(WINDOWS_WITH_MSVC) || defined(USING_CYGWIN)
        if(!options[BLDOPT].doesOccur && uprv_strcmp(options[MODE].value, "common") != 0) {
          if (pkg_getPkgDataPath(options[VERBOSE].doesOccur, &options[BLDOPT]) != 0) {
                fprintf(stderr, " required parameter is missing: -O is required for static and shared builds.\n");
                fprintf(stderr, "Run '%s --help' for help.\n", progname);
                return 1;
            }
        }
#else
        if(options[BLDOPT].doesOccur) {
            fprintf(stdout, "Warning: You are using the -O option which is not needed for MSVC build on Windows.\n");
        }
#endif

        if(!options[NAME].doesOccur) /* -O we already have - don't report it. */
        {
            fprintf(stderr, " required parameter -p is missing \n");
            fprintf(stderr, "Run '%s --help' for help.\n", progname);
            return 1;
        }

        if(argc == 1) {
            fprintf(stderr,
                "No input files specified.\n"
                "Run '%s --help' for help.\n", progname);
            return 1;
        }
    }   /* end !needsHelp */

    if(argc<0 || needsHelp  ) {
        fprintf(stderr,
            "usage: %s [-options] [-] [packageFile] \n"
            "\tProduce packaged ICU data from the given list(s) of files.\n"
            "\t'-' by itself means to read from stdin.\n"
            "\tpackageFile is a text file containing the list of files to package.\n",
            progname);

        fprintf(stderr, "\n options:\n");
        for(i=0;i<UPRV_LENGTHOF(options);i++) {
            fprintf(stderr, "%-5s -%c %s%-10s  %s\n",
                (i<1?"[REQ]":""),
                options[i].shortName,
                options[i].longName ? "or --" : "     ",
                options[i].longName ? options[i].longName : "",
                options_help[i]);
        }

        fprintf(stderr, "modes: (-m option)\n");
        for(i=0;i<UPRV_LENGTHOF(modes);i++) {
            fprintf(stderr, "   %-9s ", modes[i].name);
            if (modes[i].alt_name) {
                fprintf(stderr, "/ %-9s", modes[i].alt_name);
            } else {
                fprintf(stderr, "           ");
            }
            fprintf(stderr, "  %s\n", modes[i].desc);
        }
        return 1;
    }

    /* OK, fill in the options struct */
    uprv_memset(&o, 0, sizeof(o));

    o.mode      = options[MODE].value;
    o.version   = options[REVISION].doesOccur ? options[REVISION].value : nullptr;

    o.shortName = options[NAME].value;
    {
        int32_t len = (int32_t)uprv_strlen(o.shortName);
        char *csname, *cp;
        const char *sp;

        cp = csname = (char *) uprv_malloc((len + 1 + 1) * sizeof(*o.cShortName));
        if (*(sp = o.shortName)) {
            *cp++ = isalpha(*sp) ? * sp : '_';
            for (++sp; *sp; ++sp) {
                *cp++ = isalnum(*sp) ? *sp : '_';
            }
        }
        *cp = 0;

        o.cShortName = csname;
    }

    if(options[LIBNAME].doesOccur) { /* get libname from shortname, or explicit -L parameter */
      o.libName = options[LIBNAME].value;
    } else {
      o.libName = o.shortName;
    }

    if(options[QUIET].doesOccur) {
      o.quiet = true;
    } else {
      o.quiet = false;
    }

    if(options[PDS_BUILD].doesOccur) {
#if U_PLATFORM == U_PF_OS390
      o.pdsbuild = true;
#else
      o.pdsbuild = false;
      fprintf(stdout, "Warning: You are using the -z option which only works on z/OS.\n");

#endif
    } else {
      o.pdsbuild = false;
    }

    o.verbose   = options[VERBOSE].doesOccur;


#if !defined(WINDOWS_WITH_MSVC) || defined(USING_CYGWIN) /* on UNIX, we'll just include the file... */
    if (options[BLDOPT].doesOccur) {
        o.options   = options[BLDOPT].value;
    } else {
        o.options = nullptr;
    }
#endif
    if(options[COPYRIGHT].doesOccur) {
        o.comment = U_COPYRIGHT_STRING;
    } else if (options[COMMENT].doesOccur) {
        o.comment = options[COMMENT].value;
    }

    if( options[DESTDIR].doesOccur ) {
        o.targetDir = options[DESTDIR].value;
    } else {
        o.targetDir = ".";  /* cwd */
    }

    o.rebuild   = options[REBUILD].doesOccur;

    if( options[TEMPDIR].doesOccur ) {
        o.tmpDir    = options[TEMPDIR].value;
    } else {
        o.tmpDir    = o.targetDir;
    }

    if( options[INSTALL].doesOccur ) {
        o.install  = options[INSTALL].value;
    } else {
        o.install = nullptr;
    }

    if( options[SOURCEDIR].doesOccur ) {
        o.srcDir   = options[SOURCEDIR].value;
    } else {
        o.srcDir   = ".";
    }

    if( options[ENTRYPOINT].doesOccur ) {
        o.entryName = options[ENTRYPOINT].value;
    } else {
        o.entryName = o.cShortName;
    }

    o.withoutAssembly = false;
    if (options[WITHOUT_ASSEMBLY].doesOccur) {
#ifndef BUILD_DATA_WITHOUT_ASSEMBLY
        fprintf(stdout, "Warning: You are using the option to build without assembly code which is not supported on this platform.\n");
        fprintf(stdout, "Warning: This option will be ignored.\n");
#else
        o.withoutAssembly = true;
#endif
    }

    if (options[WIN_DYNAMICBASE].doesOccur) {
        fprintf(stdout, "Note: Ignoring option -b (windows-dynamicbase).\n");
    }

    /* OK options are set up. Now the file lists. */
    tail = nullptr;
    for( n=1; n<argc; n++) {
        o.fileListFiles = pkg_appendToList(o.fileListFiles, &tail, uprv_strdup(argv[n]));
    }

    /* load the files */
    loadLists(&o, &status);
    if( U_FAILURE(status) ) {
        fprintf(stderr, "error loading input file lists: %s\n", u_errorName(status));
        return 2;
    }

    result = pkg_executeOptions(&o);

    if (pkgDataFlags != nullptr) {
        for (n = 0; n < PKGDATA_FLAGS_SIZE; n++) {
            if (pkgDataFlags[n] != nullptr) {
                uprv_free(pkgDataFlags[n]);
            }
        }
        uprv_free(pkgDataFlags);
    }

    if (o.cShortName != nullptr) {
        uprv_free((char *)o.cShortName);
    }
    if (o.fileListFiles != nullptr) {
        pkg_deleteList(o.fileListFiles);
    }
    if (o.filePaths != nullptr) {
        pkg_deleteList(o.filePaths);
    }
    if (o.files != nullptr) {
        pkg_deleteList(o.files);
    }
    return result;
}

static int runCommand(const char* command, UBool specialHandling) {
    char *cmd = nullptr;
    char cmdBuffer[SMALL_BUFFER_MAX_SIZE];
    int32_t len = static_cast<int32_t>(strlen(command));

    if (len == 0) {
        return 0;
    }

    if (!specialHandling) {
#if defined(USING_CYGWIN) || U_PLATFORM == U_PF_MINGW || U_PLATFORM == U_PF_OS400
        int32_t buff_len;
        if ((len + BUFFER_PADDING_SIZE) >= SMALL_BUFFER_MAX_SIZE) {
            cmd = (char *)uprv_malloc(len + BUFFER_PADDING_SIZE);
            buff_len = len + BUFFER_PADDING_SIZE;
        } else {
            cmd = cmdBuffer;
            buff_len = SMALL_BUFFER_MAX_SIZE;
        }
#if defined(USING_CYGWIN) || U_PLATFORM == U_PF_MINGW
        snprintf(cmd, buff_len, "bash -c \"%s\"", command);

#elif U_PLATFORM == U_PF_OS400
        snprintf(cmd, buff_len "QSH CMD('%s')", command);
#endif
#else
        goto normal_command_mode;
#endif
    } else {
#if !(defined(USING_CYGWIN) || U_PLATFORM == U_PF_MINGW || U_PLATFORM == U_PF_OS400)
normal_command_mode:
#endif
        cmd = (char *)command;
    }

    printf("pkgdata: %s\n", cmd);
    int result = system(cmd);
    if (result != 0) {
        fprintf(stderr, "-- return status = %d\n", result);
        result = 1; // system() result code is platform specific.
    }

    if (cmd != cmdBuffer && cmd != command) {
        uprv_free(cmd);
    }

    return result;
}

#define LN_CMD "ln -s"
#define RM_CMD "rm -f"

static int32_t pkg_executeOptions(UPKGOptions *o) {
    int32_t result = 0;

    const char mode = o->mode[0];
    char targetDir[SMALL_BUFFER_MAX_SIZE] = "";
    char tmpDir[SMALL_BUFFER_MAX_SIZE] = "";
    char datFileName[SMALL_BUFFER_MAX_SIZE] = "";
    char datFileNamePath[LARGE_BUFFER_MAX_SIZE] = "";
    char checkLibFile[LARGE_BUFFER_MAX_SIZE] = "";

    initializePkgDataFlags(o);

    if (IN_FILES_MODE(mode)) {
        /* Copy the raw data to the installation directory. */
        if (o->install != nullptr) {
            uprv_strcpy(targetDir, o->install);
            if (o->shortName != nullptr) {
                uprv_strcat(targetDir, PKGDATA_FILE_SEP_STRING);
                uprv_strcat(targetDir, o->shortName);
            }
            
            if(o->verbose) {
              fprintf(stdout, "# Install: Files mode, copying files to %s..\n", targetDir);
            }
            result = pkg_installFileMode(targetDir, o->srcDir, o->fileListFiles->str);
        }
        return result;
    } else /* if (IN_COMMON_MODE(mode) || IN_DLL_MODE(mode) || IN_STATIC_MODE(mode)) */ {
        UBool noVersion = false;

        uprv_strcpy(targetDir, o->targetDir);
        uprv_strcat(targetDir, PKGDATA_FILE_SEP_STRING);

        uprv_strcpy(tmpDir, o->tmpDir);
        uprv_strcat(tmpDir, PKGDATA_FILE_SEP_STRING);

        uprv_strcpy(datFileNamePath, tmpDir);

        uprv_strcpy(datFileName, o->shortName);
        uprv_strcat(datFileName, UDATA_CMN_SUFFIX);

        uprv_strcat(datFileNamePath, datFileName);

        if(o->verbose) {
          fprintf(stdout, "# Writing package file %s ..\n", datFileNamePath);
        }
        result = writePackageDatFile(datFileNamePath, o->comment, o->srcDir, o->fileListFiles->str, nullptr, U_CHARSET_FAMILY ? 'e' :  U_IS_BIG_ENDIAN ? 'b' : 'l');
        if (result != 0) {
            fprintf(stderr,"Error writing package dat file.\n");
            return result;
        }

        if (IN_COMMON_MODE(mode)) {
            char targetFileNamePath[LARGE_BUFFER_MAX_SIZE] = "";

            uprv_strcpy(targetFileNamePath, targetDir);
            uprv_strcat(targetFileNamePath, datFileName);

            /* Move the dat file created to the target directory. */
            if (uprv_strcmp(datFileNamePath, targetFileNamePath) != 0) {
                if (T_FileStream_file_exists(targetFileNamePath)) {
                    if ((result = remove(targetFileNamePath)) != 0) {
                        fprintf(stderr, "Unable to remove old dat file: %s\n",
                                targetFileNamePath);
                        return result;
                    }
                }

                result = rename(datFileNamePath, targetFileNamePath);

                if (o->verbose) {
                    fprintf(stdout, "# Moving package file to %s ..\n",
                            targetFileNamePath);
                }
                if (result != 0) {
                    fprintf(
                            stderr,
                            "Unable to move dat file (%s) to target location (%s).\n",
                            datFileNamePath, targetFileNamePath);
                    return result;
                }
            }

            if (o->install != nullptr) {
                result = pkg_installCommonMode(o->install, targetFileNamePath);
            }

            return result;
        } else /* if (IN_STATIC_MODE(mode) || IN_DLL_MODE(mode)) */ {
            char gencFilePath[SMALL_BUFFER_MAX_SIZE] = "";
            char version_major[10] = "";
            UBool reverseExt = false;

#if !defined(WINDOWS_WITH_MSVC) || defined(USING_CYGWIN)
            /* Get the version major number. */
            if (o->version != nullptr) {
                for (uint32_t i = 0;i < sizeof(version_major);i++) {
                    if (o->version[i] == '.') {
                        version_major[i] = 0;
                        break;
                    }
                    version_major[i] = o->version[i];
                }
            } else {
                noVersion = true;
                if (IN_DLL_MODE(mode)) {
                    fprintf(stdout, "Warning: Providing a revision number with the -r option is recommended when packaging data in the current mode.\n");
                }
            }

#if U_PLATFORM != U_PF_OS400
            /* Certain platforms have different library extension ordering. (e.g. libicudata.##.so vs libicudata.so.##)
             * reverseExt is false if the suffix should be the version number.
             */
            if (pkgDataFlags[LIB_EXT_ORDER][uprv_strlen(pkgDataFlags[LIB_EXT_ORDER])-1] == pkgDataFlags[SO_EXT][uprv_strlen(pkgDataFlags[SO_EXT])-1]) {
                reverseExt = true;
            }
#endif
            /* Using the base libName and version number, generate the library file names. */
            createFileNames(o, mode, version_major, o->version == nullptr ? "" : o->version, o->libName, reverseExt, noVersion);

            if ((o->version!=nullptr || IN_STATIC_MODE(mode)) && o->rebuild == false && o->pdsbuild == false) {
                /* Check to see if a previous built data library file exists and check if it is the latest. */
                snprintf(checkLibFile, sizeof(checkLibFile), "%s%s", targetDir, libFileNames[LIB_FILE_VERSION]);
                if (T_FileStream_file_exists(checkLibFile)) {
                    if (isFileModTimeLater(checkLibFile, o->srcDir, true) && isFileModTimeLater(checkLibFile, o->options)) {
                        if (o->install != nullptr) {
                          if(o->verbose) {
                            fprintf(stdout, "# Installing already-built library into %s\n", o->install);
                          }
                          result = pkg_installLibrary(o->install, targetDir, noVersion);
                        } else {
                          if(o->verbose) {
                            printf("# Not rebuilding %s - up to date.\n", checkLibFile);
                          }
                        }
                        return result;
                    } else if (o->verbose && (o->install!=nullptr)) {
                      fprintf(stdout, "# Not installing up-to-date library %s into %s\n", checkLibFile, o->install);
                    }
                } else if(o->verbose && (o->install!=nullptr)) {
                  fprintf(stdout, "# Not installing missing %s into %s\n", checkLibFile, o->install);
                }
            }

            if (pkg_checkFlag(o) == nullptr) {
                /* Error occurred. */
                return result;
            }
#endif

            if (!o->withoutAssembly && pkgDataFlags[GENCCODE_ASSEMBLY_TYPE][0] != 0) {
                const char* genccodeAssembly = pkgDataFlags[GENCCODE_ASSEMBLY_TYPE];

                if(o->verbose) {
                  fprintf(stdout, "# Generating assembly code %s of type %s ..\n", gencFilePath, genccodeAssembly);
                }
                
                /* Offset genccodeAssembly by 3 because "-a " */
                if (genccodeAssembly &&
                    (uprv_strlen(genccodeAssembly)>3) &&
                    checkAssemblyHeaderName(genccodeAssembly+3)) {
                    writeAssemblyCode(
                        datFileNamePath,
                        o->tmpDir,
                        o->entryName,
                        nullptr,
                        gencFilePath,
                        sizeof(gencFilePath));

                    result = pkg_createWithAssemblyCode(targetDir, mode, gencFilePath);
                    if (result != 0) {
                        fprintf(stderr, "Error generating assembly code for data.\n");
                        return result;
                    } else if (IN_STATIC_MODE(mode)) {
                      if(o->install != nullptr) {
                        if(o->verbose) {
                          fprintf(stdout, "# Installing static library into %s\n", o->install);
                        }
                        result = pkg_installLibrary(o->install, targetDir, noVersion);
                      }
                      return result;
                    }
                } else {
                    fprintf(stderr,"Assembly type \"%s\" is unknown.\n", genccodeAssembly);
                    return -1;
                }
            } else {
                if(o->verbose) {
                  fprintf(stdout, "# Writing object code to %s ..\n", gencFilePath);
                }
                if (o->withoutAssembly) {
#ifdef BUILD_DATA_WITHOUT_ASSEMBLY
                    result = pkg_createWithoutAssemblyCode(o, targetDir, mode);
#else
                    /* This error should not occur. */
                    fprintf(stderr, "Error- BUILD_DATA_WITHOUT_ASSEMBLY is not defined. Internal error.\n");
#endif
                } else {
#ifdef CAN_WRITE_OBJ_CODE
                    /* Try to detect the arch type, use nullptr if unsuccessful */
                    char optMatchArch[10] = { 0 };
                    pkg_createOptMatchArch(optMatchArch);
                    writeObjectCode(
                        datFileNamePath,
                        o->tmpDir,
                        o->entryName,
                        (optMatchArch[0] == 0 ? nullptr : optMatchArch),
                        nullptr,
                        nullptr,
                        gencFilePath,
                        sizeof(gencFilePath),
                        true);
                    pkg_destroyOptMatchArch(optMatchArch);
#if U_PLATFORM_IS_LINUX_BASED
                    result = pkg_generateLibraryFile(targetDir, mode, gencFilePath);
#elif defined(WINDOWS_WITH_MSVC)
                    result = pkg_createWindowsDLL(mode, gencFilePath, o);
#endif
#elif defined(BUILD_DATA_WITHOUT_ASSEMBLY)
                    result = pkg_createWithoutAssemblyCode(o, targetDir, mode);
#else
                    fprintf(stderr, "Error- neither CAN_WRITE_OBJ_CODE nor BUILD_DATA_WITHOUT_ASSEMBLY are defined. Internal error.\n");
                    return 1;
#endif
                }

                if (result != 0) {
                    fprintf(stderr, "Error generating package data.\n");
                    return result;
                }
            }
#if !U_PLATFORM_USES_ONLY_WIN32_API
            if(!IN_STATIC_MODE(mode)) {
                /* Certain platforms uses archive library. (e.g. AIX) */
                if(o->verbose) {
                  fprintf(stdout, "# Creating data archive library file ..\n");
                }
                result = pkg_archiveLibrary(targetDir, o->version, reverseExt);
                if (result != 0) {
                    fprintf(stderr, "Error creating data archive library file.\n");
                   return result;
                }
#if U_PLATFORM != U_PF_OS400
                if (!noVersion) {
                    /* Create symbolic links for the final library file. */
#if U_PLATFORM == U_PF_OS390
                    result = pkg_createSymLinks(targetDir, o->pdsbuild);
#else
                    result = pkg_createSymLinks(targetDir, noVersion);
#endif
                    if (result != 0) {
                        fprintf(stderr, "Error creating symbolic links of the data library file.\n");
                        return result;
                    }
                }
#endif
            } /* !IN_STATIC_MODE */
#endif

#if !U_PLATFORM_USES_ONLY_WIN32_API
            /* Install the libraries if option was set. */
            if (o->install != nullptr) {
                if(o->verbose) {
                  fprintf(stdout, "# Installing library file to %s ..\n", o->install);
                }
                result = pkg_installLibrary(o->install, targetDir, noVersion);
                if (result != 0) {
                    fprintf(stderr, "Error installing the data library.\n");
                    return result;
                }
            }
#endif
        }
    }
    return result;
}

/* Initialize the pkgDataFlags with the option file given. */
static int32_t initializePkgDataFlags(UPKGOptions *o) {
    UErrorCode status = U_ZERO_ERROR;
    int32_t result = 0;
    int32_t currentBufferSize = SMALL_BUFFER_MAX_SIZE;
    int32_t tmpResult = 0;

    /* Initialize pkgdataFlags */
    pkgDataFlags = (char**)uprv_malloc(sizeof(char*) * PKGDATA_FLAGS_SIZE);

    /* If we run out of space, allocate more */
#if !defined(WINDOWS_WITH_MSVC) || defined(USING_CYGWIN)
    do {
#endif
        if (pkgDataFlags != nullptr) {
            for (int32_t i = 0; i < PKGDATA_FLAGS_SIZE; i++) {
                pkgDataFlags[i] = (char*)uprv_malloc(sizeof(char) * currentBufferSize);
                if (pkgDataFlags[i] != nullptr) {
                    pkgDataFlags[i][0] = 0;
                } else {
                    fprintf(stderr,"Error allocating memory for pkgDataFlags.\n");
                    /* If an error occurs, ensure that the rest of the array is nullptr */
                    for (int32_t n = i + 1; n < PKGDATA_FLAGS_SIZE; n++) {
                        pkgDataFlags[n] = nullptr;
                    }
                    return -1;
                }
            }
        } else {
            fprintf(stderr,"Error allocating memory for pkgDataFlags.\n");
            return -1;
        }

        if (o->options == nullptr) {
            return result;
        }

#if !defined(WINDOWS_WITH_MSVC) || defined(USING_CYGWIN)
        /* Read in options file. */
        if(o->verbose) {
          fprintf(stdout, "# Reading options file %s\n", o->options);
        }
        status = U_ZERO_ERROR;
        tmpResult = parseFlagsFile(o->options, pkgDataFlags, currentBufferSize, FLAG_NAMES, (int32_t)PKGDATA_FLAGS_SIZE, &status);
        if (status == U_BUFFER_OVERFLOW_ERROR) {
            for (int32_t i = 0; i < PKGDATA_FLAGS_SIZE; i++) {
                if (pkgDataFlags[i]) {
                    uprv_free(pkgDataFlags[i]);
                    pkgDataFlags[i] = nullptr;
                }
            }
            currentBufferSize = tmpResult;
        } else if (U_FAILURE(status)) {
            fprintf(stderr,"Unable to open or read \"%s\" option file. status = %s\n", o->options, u_errorName(status));
            return -1;
        }
#endif
        if(o->verbose) {
            fprintf(stdout, "# pkgDataFlags=\n");
            for(int32_t i=0;i<PKGDATA_FLAGS_SIZE;i++) {
                fprintf(stdout, "  [%d] %s:  %s\n", i, FLAG_NAMES[i], pkgDataFlags[i]);
            }
            fprintf(stdout, "\n");
        }
#if !defined(WINDOWS_WITH_MSVC) || defined(USING_CYGWIN)
    } while (status == U_BUFFER_OVERFLOW_ERROR);
#endif

    return result;
}


/*
 * Given the base libName and version numbers, generate the library file names and store it in libFileNames.
 * Depending on the configuration, the library name may either end with version number or shared object suffix.
 */
static void createFileNames(UPKGOptions *o, const char mode, const char *version_major, const char *version, const char *libName, UBool reverseExt, UBool noVersion) {
    const char* FILE_EXTENSION_SEP = uprv_strlen(pkgDataFlags[SO_EXT]) == 0 ? "" : ".";
    const char* FILE_SUFFIX = pkgDataFlags[LIB_EXT_ORDER][0] == '.' ? "." : "";

#if U_PLATFORM == U_PF_MINGW
        /* MinGW does not need the library prefix when building in dll mode. */
        if (IN_DLL_MODE(mode)) {
            snprintf(libFileNames[LIB_FILE], sizeof(libFileNames[LIB_FILE]), "%s", libName);
        } else {
            snprintf(libFileNames[LIB_FILE], sizeof(libFileNames[LIB_FILE]), "%s%s%s",
                    (strstr(libName, "icudt") ? "lib" : ""),
                    pkgDataFlags[LIBPREFIX],
                    libName);
        }
#else
        snprintf(libFileNames[LIB_FILE], sizeof(libFileNames[LIB_FILE]), "%s%s",
                pkgDataFlags[LIBPREFIX],
                libName);
#endif

        if(o->verbose) {
          fprintf(stdout, "# libFileName[LIB_FILE] = %s\n", libFileNames[LIB_FILE]);
        }

#if U_PLATFORM == U_PF_MINGW
        // Name the import library lib*.dll.a
        snprintf(libFileNames[LIB_FILE_MINGW], sizeof(libFileNames[LIB_FILE_MINGW]), "lib%s.dll.a", libName);
#elif U_PLATFORM == U_PF_CYGWIN
        snprintf(libFileNames[LIB_FILE_CYGWIN], sizeof(libFileNames[LIB_FILE_CYGWIN]), "cyg%s%s%s",
                libName,
                FILE_EXTENSION_SEP,
                pkgDataFlags[SO_EXT]);
        snprintf(libFileNames[LIB_FILE_CYGWIN_VERSION], sizeof(libFileNames[LIB_FILE_CYGWIN_VERSION]), "cyg%s%s%s%s",
                libName,
                version_major,
                FILE_EXTENSION_SEP,
                pkgDataFlags[SO_EXT]);

        uprv_strcat(pkgDataFlags[SO_EXT], ".");
        uprv_strcat(pkgDataFlags[SO_EXT], pkgDataFlags[A_EXT]);
#elif U_PLATFORM == U_PF_OS400 || defined(_AIX)
        snprintf(libFileNames[LIB_FILE_VERSION_TMP], sizeof(libFileNames[LIB_FILE_VERSION_TMP]), "%s%s%s",
                libFileNames[LIB_FILE],
                FILE_EXTENSION_SEP,
                pkgDataFlags[SOBJ_EXT]);
#elif U_PLATFORM == U_PF_OS390
        snprintf(libFileNames[LIB_FILE_VERSION_TMP], sizeof(libFileNames[LIB_FILE_VERSION_TMP]), "%s%s%s%s%s",
                    libFileNames[LIB_FILE],
                    pkgDataFlags[LIB_EXT_ORDER][0] == '.' ? "." : "",
                    reverseExt ? version : pkgDataFlags[SOBJ_EXT],
                    FILE_EXTENSION_SEP,
                    reverseExt ? pkgDataFlags[SOBJ_EXT] : version);

        snprintf(libFileNames[LIB_FILE_OS390BATCH_VERSION], sizeof(libFileNames[LIB_FILE_OS390BATCH_VERSION]), "%s%s.x",
                    libFileNames[LIB_FILE],
                    version);
        snprintf(libFileNames[LIB_FILE_OS390BATCH_MAJOR], sizeof(libFileNames[LIB_FILE_OS390BATCH_MAJOR]), "%s%s.x",
                    libFileNames[LIB_FILE],
                    version_major);
#else
        if (noVersion && !reverseExt) {
            snprintf(libFileNames[LIB_FILE_VERSION_TMP], sizeof(libFileNames[LIB_FILE_VERSION_TMP]), "%s%s%s",
                    libFileNames[LIB_FILE],
                    FILE_SUFFIX,
                    pkgDataFlags[SOBJ_EXT]);
        } else {
            snprintf(libFileNames[LIB_FILE_VERSION_TMP], sizeof(libFileNames[LIB_FILE_VERSION_TMP]), "%s%s%s%s%s",
                    libFileNames[LIB_FILE],
                    FILE_SUFFIX,
                    reverseExt ? version : pkgDataFlags[SOBJ_EXT],
                    FILE_EXTENSION_SEP,
                    reverseExt ? pkgDataFlags[SOBJ_EXT] : version);
        }
#endif
        if (noVersion && !reverseExt) {
            snprintf(libFileNames[LIB_FILE_VERSION_MAJOR], sizeof(libFileNames[LIB_FILE_VERSION_TMP]), "%s%s%s",
                    libFileNames[LIB_FILE],
                    FILE_SUFFIX,
                    pkgDataFlags[SO_EXT]);

            snprintf(libFileNames[LIB_FILE_VERSION], sizeof(libFileNames[LIB_FILE_VERSION]), "%s%s%s",
                    libFileNames[LIB_FILE],
                    FILE_SUFFIX,
                    pkgDataFlags[SO_EXT]);
        } else {
            snprintf(libFileNames[LIB_FILE_VERSION_MAJOR], sizeof(libFileNames[LIB_FILE_VERSION_MAJOR]), "%s%s%s%s%s",
                    libFileNames[LIB_FILE],
                    FILE_SUFFIX,
                    reverseExt ? version_major : pkgDataFlags[SO_EXT],
                    FILE_EXTENSION_SEP,
                    reverseExt ? pkgDataFlags[SO_EXT] : version_major);

            snprintf(libFileNames[LIB_FILE_VERSION], sizeof(libFileNames[LIB_FILE_VERSION]), "%s%s%s%s%s",
                    libFileNames[LIB_FILE],
                    FILE_SUFFIX,
                    reverseExt ? version : pkgDataFlags[SO_EXT],
                    FILE_EXTENSION_SEP,
                    reverseExt ? pkgDataFlags[SO_EXT] : version);
        }

        if(o->verbose) {
          fprintf(stdout, "# libFileName[LIB_FILE_VERSION] = %s\n", libFileNames[LIB_FILE_VERSION]);
        }

#if U_PF_MINGW <= U_PLATFORM && U_PLATFORM <= U_PF_CYGWIN
        /* Cygwin and MinGW only deals with the version major number. */
        uprv_strcpy(libFileNames[LIB_FILE_VERSION_TMP], libFileNames[LIB_FILE_VERSION_MAJOR]);
#endif

        if(IN_STATIC_MODE(mode)) {
            snprintf(libFileNames[LIB_FILE_VERSION], sizeof(libFileNames[LIB_FILE_VERSION]), "%s.%s", libFileNames[LIB_FILE], pkgDataFlags[A_EXT]);
            libFileNames[LIB_FILE_VERSION_MAJOR][0]=0;
            if(o->verbose) {
              fprintf(stdout, "# libFileName[LIB_FILE_VERSION] = %s  (static)\n", libFileNames[LIB_FILE_VERSION]);
            }
        }
}

/* Create the symbolic links for the final library file. */
static int32_t pkg_createSymLinks(const char *targetDir, UBool specialHandling) {
    int32_t result = 0;
    char cmd[LARGE_BUFFER_MAX_SIZE];
    char name1[SMALL_BUFFER_MAX_SIZE]; /* symlink file name */
    char name2[SMALL_BUFFER_MAX_SIZE]; /* file name to symlink */
    const char* FILE_EXTENSION_SEP = uprv_strlen(pkgDataFlags[SO_EXT]) == 0 ? "" : ".";

#if U_PLATFORM != U_PF_CYGWIN
    /* No symbolic link to make. */
    if (uprv_strlen(libFileNames[LIB_FILE_VERSION]) == 0 || uprv_strlen(libFileNames[LIB_FILE_VERSION_MAJOR]) == 0 ||
        uprv_strcmp(libFileNames[LIB_FILE_VERSION], libFileNames[LIB_FILE_VERSION_MAJOR]) == 0) {
        return result;
    }
    
    snprintf(cmd, sizeof(cmd), "cd %s && %s %s && %s %s %s",
            targetDir,
            RM_CMD,
            libFileNames[LIB_FILE_VERSION_MAJOR],
            LN_CMD,
            libFileNames[LIB_FILE_VERSION],
            libFileNames[LIB_FILE_VERSION_MAJOR]);
    result = runCommand(cmd);
    if (result != 0) {
        fprintf(stderr, "Error creating symbolic links. Failed command: %s\n", cmd);
        return result;
    }
#endif

    if (specialHandling) {
#if U_PLATFORM == U_PF_CYGWIN
        snprintf(name1, sizeof(name1), "%s", libFileNames[LIB_FILE_CYGWIN]);
        snprintf(name2, sizeof(name2), "%s", libFileNames[LIB_FILE_CYGWIN_VERSION]);
#elif U_PLATFORM == U_PF_OS390
        /* Create the symbolic links for the import data */
        /* Use the cmd buffer to store path to import data file to check its existence */
        snprintf(cmd, sizeof(cmd), "%s/%s", targetDir, libFileNames[LIB_FILE_OS390BATCH_VERSION]);
        if (T_FileStream_file_exists(cmd)) {
            snprintf(cmd, sizeof(cmd), "cd %s && %s %s && %s %s %s",
                    targetDir,
                    RM_CMD,
                    libFileNames[LIB_FILE_OS390BATCH_MAJOR],
                    LN_CMD,
                    libFileNames[LIB_FILE_OS390BATCH_VERSION],
                    libFileNames[LIB_FILE_OS390BATCH_MAJOR]);
            result = runCommand(cmd);
            if (result != 0) {
                fprintf(stderr, "Error creating symbolic links. Failed command: %s\n", cmd);
                return result;
            }

            snprintf(cmd, sizeof(cmd), "cd %s && %s %s.x && %s %s %s.x",
                    targetDir,
                    RM_CMD,
                    libFileNames[LIB_FILE],
                    LN_CMD,
                    libFileNames[LIB_FILE_OS390BATCH_VERSION],
                    libFileNames[LIB_FILE]);
            result = runCommand(cmd);
            if (result != 0) {
                fprintf(stderr, "Error creating symbolic links. Failed command: %s\n", cmd);
                return result;
            }
        }

        /* Needs to be set here because special handling skips it */
        snprintf(name1, sizeof(name1), "%s%s%s", libFileNames[LIB_FILE], FILE_EXTENSION_SEP, pkgDataFlags[SO_EXT]);
        snprintf(name2, sizeof(name2), "%s", libFileNames[LIB_FILE_VERSION]);
#else
        goto normal_symlink_mode;
#endif
    } else {
#if U_PLATFORM != U_PF_CYGWIN
normal_symlink_mode:
#endif
        snprintf(name1, sizeof(name1), "%s%s%s", libFileNames[LIB_FILE], FILE_EXTENSION_SEP, pkgDataFlags[SO_EXT]);
        snprintf(name2, sizeof(name2), "%s", libFileNames[LIB_FILE_VERSION]);
    }

    snprintf(cmd, sizeof(cmd), "cd %s && %s %s && %s %s %s",
            targetDir,
            RM_CMD,
            name1,
            LN_CMD,
            name2,
            name1);

     result = runCommand(cmd);

    return result;
}

static int32_t pkg_installLibrary(const char *installDir, const char *targetDir, UBool noVersion) {
    int32_t result = 0;
    char cmd[SMALL_BUFFER_MAX_SIZE];

    auto ret = snprintf(cmd,
            sizeof(cmd),
            "cd %s && %s %s %s%s%s",
            targetDir,
            pkgDataFlags[INSTALL_CMD],
            libFileNames[LIB_FILE_VERSION],
            installDir, PKGDATA_FILE_SEP_STRING, libFileNames[LIB_FILE_VERSION]);
    (void)ret;
    U_ASSERT(0 <= ret && ret < SMALL_BUFFER_MAX_SIZE);

    result = runCommand(cmd);

    if (result != 0) {
        fprintf(stderr, "Error installing library. Failed command: %s\n", cmd);
        return result;
    }

#ifdef CYGWINMSVC
    snprintf(cmd, sizeof(cmd), "cd %s && %s %s.lib %s",
            targetDir,
            pkgDataFlags[INSTALL_CMD],
            libFileNames[LIB_FILE],
            installDir
            );
    result = runCommand(cmd);

    if (result != 0) {
        fprintf(stderr, "Error installing library. Failed command: %s\n", cmd);
        return result;
    }
#elif U_PLATFORM == U_PF_CYGWIN
    snprintf(cmd, sizeof(cmd), "cd %s && %s %s %s",
            targetDir,
            pkgDataFlags[INSTALL_CMD],
            libFileNames[LIB_FILE_CYGWIN_VERSION],
            installDir
            );
    result = runCommand(cmd);

    if (result != 0) {
        fprintf(stderr, "Error installing library. Failed command: %s\n", cmd);
        return result;
    }

#elif U_PLATFORM == U_PF_OS390
    if (T_FileStream_file_exists(libFileNames[LIB_FILE_OS390BATCH_VERSION])) {
        snprintf(cmd, sizeof(cmd), "%s %s %s",
                pkgDataFlags[INSTALL_CMD],
                libFileNames[LIB_FILE_OS390BATCH_VERSION],
                installDir
                );
        result = runCommand(cmd);

        if (result != 0) {
            fprintf(stderr, "Error installing library. Failed command: %s\n", cmd);
            return result;
        }
    }
#endif

    if (noVersion) {
        return result;
    } else {
        return pkg_createSymLinks(installDir, true);
    }
}

static int32_t pkg_installCommonMode(const char *installDir, const char *fileName) {
    int32_t result = 0;
    char cmd[SMALL_BUFFER_MAX_SIZE] = "";

    if (!T_FileStream_file_exists(installDir)) {
        UErrorCode status = U_ZERO_ERROR;

        uprv_mkdir(installDir, &status);
        if (U_FAILURE(status)) {
            fprintf(stderr, "Error creating installation directory: %s\n", installDir);
            return -1;
        }
    }
#ifndef U_WINDOWS_WITH_MSVC
    snprintf(cmd, sizeof(cmd), "%s %s %s", pkgDataFlags[INSTALL_CMD], fileName, installDir);
#else
    snprintf(cmd, sizeof(cmd), "%s %s %s %s", WIN_INSTALL_CMD, fileName, installDir, WIN_INSTALL_CMD_FLAGS);
#endif

    result = runCommand(cmd);
    if (result != 0) {
        fprintf(stderr, "Failed to install data file with command: %s\n", cmd);
    }

    return result;
}

#ifdef U_WINDOWS_MSVC
/* Copy commands for installing the raw data files on Windows. */
#define WIN_INSTALL_CMD "xcopy"
#define WIN_INSTALL_CMD_FLAGS "/E /Y /K"
#endif
static int32_t pkg_installFileMode(const char *installDir, const char *srcDir, const char *fileListName) {
    int32_t result = 0;
    char cmd[SMALL_BUFFER_MAX_SIZE] = "";

    if (!T_FileStream_file_exists(installDir)) {
        UErrorCode status = U_ZERO_ERROR;

        uprv_mkdir(installDir, &status);
        if (U_FAILURE(status)) {
            fprintf(stderr, "Error creating installation directory: %s\n", installDir);
            return -1;
        }
    }
#ifndef U_WINDOWS_WITH_MSVC
    char buffer[SMALL_BUFFER_MAX_SIZE] = "";
    int32_t bufferLength = 0;

    FileStream *f = T_FileStream_open(fileListName, "r");
    if (f != nullptr) {
        for(;;) {
            if (T_FileStream_readLine(f, buffer, SMALL_BUFFER_MAX_SIZE) != nullptr) {
                bufferLength = static_cast<int32_t>(uprv_strlen(buffer));
                /* Remove new line character. */
                if (bufferLength > 0) {
                    buffer[bufferLength-1] = 0;
                }

                auto ret = snprintf(cmd,
                        sizeof(cmd),
                        "%s %s%s%s %s%s%s",
                        pkgDataFlags[INSTALL_CMD],
                        srcDir, PKGDATA_FILE_SEP_STRING, buffer,
                        installDir, PKGDATA_FILE_SEP_STRING, buffer);
                (void)ret;
                U_ASSERT(0 <= ret && ret < SMALL_BUFFER_MAX_SIZE);

                result = runCommand(cmd);
                if (result != 0) {
                    fprintf(stderr, "Failed to install data file with command: %s\n", cmd);
                    break;
                }
            } else {
                if (!T_FileStream_eof(f)) {
                    fprintf(stderr, "Failed to read line from file: %s\n", fileListName);
                    result = -1;
                }
                break;
            }
        }
        T_FileStream_close(f);
    } else {
        result = -1;
        fprintf(stderr, "Unable to open list file: %s\n", fileListName);
    }
#else
    snprintf(cmd, sizeof(cmd), "%s %s %s %s", WIN_INSTALL_CMD, srcDir, installDir, WIN_INSTALL_CMD_FLAGS);
    result = runCommand(cmd);
    if (result != 0) {
        fprintf(stderr, "Failed to install data file with command: %s\n", cmd);
    }
#endif

    return result;
}

/* Archiving of the library file may be needed depending on the platform and options given.
 * If archiving is not needed, copy over the library file name.
 */
static int32_t pkg_archiveLibrary(const char *targetDir, const char *version, UBool reverseExt) {
    int32_t result = 0;
    char cmd[LARGE_BUFFER_MAX_SIZE];

    /* If the shared object suffix and the final object suffix is different and the final object suffix and the
     * archive file suffix is the same, then the final library needs to be archived.
     */
    if (uprv_strcmp(pkgDataFlags[SOBJ_EXT], pkgDataFlags[SO_EXT]) != 0 && uprv_strcmp(pkgDataFlags[A_EXT], pkgDataFlags[SO_EXT]) == 0) {
        snprintf(libFileNames[LIB_FILE_VERSION], sizeof(libFileNames[LIB_FILE_VERSION]), "%s%s%s.%s",
                libFileNames[LIB_FILE],
                pkgDataFlags[LIB_EXT_ORDER][0] == '.' ? "." : "",
                reverseExt ? version : pkgDataFlags[SO_EXT],
                reverseExt ? pkgDataFlags[SO_EXT] : version);

        snprintf(cmd, sizeof(cmd), "%s %s %s%s %s%s",
                pkgDataFlags[AR],
                pkgDataFlags[ARFLAGS],
                targetDir,
                libFileNames[LIB_FILE_VERSION],
                targetDir,
                libFileNames[LIB_FILE_VERSION_TMP]);

        result = runCommand(cmd); 
        if (result != 0) { 
            fprintf(stderr, "Error creating archive library. Failed command: %s\n", cmd);
            return result; 
        } 
        
        snprintf(cmd, sizeof(cmd), "%s %s%s", 
            pkgDataFlags[RANLIB], 
            targetDir, 
            libFileNames[LIB_FILE_VERSION]);
        
        result = runCommand(cmd); 
        if (result != 0) {
            fprintf(stderr, "Error creating archive library. Failed command: %s\n", cmd);
            return result;
        }

        /* Remove unneeded library file. */
        snprintf(cmd, sizeof(cmd), "%s %s%s",
                RM_CMD,
                targetDir,
                libFileNames[LIB_FILE_VERSION_TMP]);

        result = runCommand(cmd);
        if (result != 0) {
            fprintf(stderr, "Error creating archive library. Failed command: %s\n", cmd);
            return result;
        }

    } else {
        uprv_strcpy(libFileNames[LIB_FILE_VERSION], libFileNames[LIB_FILE_VERSION_TMP]);
    }

    return result;
}

/*
 * Using the compiler information from the configuration file set by -O option, generate the library file.
 * command may be given to allow for a larger buffer for cmd.
 */
static int32_t pkg_generateLibraryFile(const char *targetDir, const char mode, const char *objectFile, char *command, UBool specialHandling) {
    int32_t result = 0;
    char *cmd = nullptr;
    UBool freeCmd = false;
    int32_t length = 0;

    (void)specialHandling;  // Suppress unused variable compiler warnings on platforms where all usage
                            // of this parameter is #ifdefed out.

    /* This is necessary because if packaging is done without assembly code, objectFile might be extremely large
     * containing many object files and so the calling function should supply a command buffer that is large
     * enough to handle this. Otherwise, use the default size.
     */
    if (command != nullptr) {
        cmd = command;
    }

    if (IN_STATIC_MODE(mode)) {
        if (cmd == nullptr) {
            length = static_cast<int32_t>(uprv_strlen(pkgDataFlags[AR]) + uprv_strlen(pkgDataFlags[ARFLAGS]) + uprv_strlen(targetDir) +
                     uprv_strlen(libFileNames[LIB_FILE_VERSION]) + uprv_strlen(objectFile) + uprv_strlen(pkgDataFlags[RANLIB]) + BUFFER_PADDING_SIZE);
            if ((cmd = (char *)uprv_malloc(sizeof(char) * length)) == nullptr) {
                fprintf(stderr, "Unable to allocate memory for command.\n");
                return -1;
            }
            freeCmd = true;
        }
        sprintf(cmd, "%s %s %s%s %s",
                pkgDataFlags[AR],
                pkgDataFlags[ARFLAGS],
                targetDir,
                libFileNames[LIB_FILE_VERSION],
                objectFile);

        result = runCommand(cmd);
        if (result == 0) {
            sprintf(cmd, "%s %s%s", 
                    pkgDataFlags[RANLIB], 
                    targetDir, 
                    libFileNames[LIB_FILE_VERSION]); 
        
            result = runCommand(cmd);
        }
    } else /* if (IN_DLL_MODE(mode)) */ {
        if (cmd == nullptr) {
            length = static_cast<int32_t>(uprv_strlen(pkgDataFlags[GENLIB]) + uprv_strlen(pkgDataFlags[LDICUDTFLAGS]) +
                     ((uprv_strlen(targetDir) + uprv_strlen(libFileNames[LIB_FILE_VERSION_TMP])) * 2) +
                     uprv_strlen(objectFile) + uprv_strlen(pkgDataFlags[LD_SONAME]) +
                     uprv_strlen(pkgDataFlags[LD_SONAME][0] == 0 ? "" : libFileNames[LIB_FILE_VERSION_MAJOR]) +
                     uprv_strlen(pkgDataFlags[RPATH_FLAGS]) + uprv_strlen(pkgDataFlags[BIR_FLAGS]) + BUFFER_PADDING_SIZE);
#if U_PLATFORM == U_PF_CYGWIN
            length += static_cast<int32_t>(uprv_strlen(targetDir) + uprv_strlen(libFileNames[LIB_FILE_CYGWIN_VERSION]));
#elif U_PLATFORM == U_PF_MINGW
            length += static_cast<int32_t>(uprv_strlen(targetDir) + uprv_strlen(libFileNames[LIB_FILE_MINGW]));
#endif
            if ((cmd = (char *)uprv_malloc(sizeof(char) * length)) == nullptr) {
                fprintf(stderr, "Unable to allocate memory for command.\n");
                return -1;
            }
            freeCmd = true;
        }
#if U_PLATFORM == U_PF_MINGW
        sprintf(cmd, "%s%s%s %s -o %s%s %s %s%s %s %s",
                pkgDataFlags[GENLIB],
                targetDir,
                libFileNames[LIB_FILE_MINGW],
                pkgDataFlags[LDICUDTFLAGS],
                targetDir,
                libFileNames[LIB_FILE_VERSION_TMP],
#elif U_PLATFORM == U_PF_CYGWIN
        sprintf(cmd, "%s%s%s %s -o %s%s %s %s%s %s %s",
                pkgDataFlags[GENLIB],
                targetDir,
                libFileNames[LIB_FILE_VERSION_TMP],
                pkgDataFlags[LDICUDTFLAGS],
                targetDir,
                libFileNames[LIB_FILE_CYGWIN_VERSION],
#elif U_PLATFORM == U_PF_AIX
        sprintf(cmd, "%s %s%s;%s %s -o %s%s %s %s%s %s %s",
                RM_CMD,
                targetDir,
                libFileNames[LIB_FILE_VERSION_TMP],
                pkgDataFlags[GENLIB],
                pkgDataFlags[LDICUDTFLAGS],
                targetDir,
                libFileNames[LIB_FILE_VERSION_TMP],
#else
        sprintf(cmd, "%s %s -o %s%s %s %s%s %s %s",
                pkgDataFlags[GENLIB],
                pkgDataFlags[LDICUDTFLAGS],
                targetDir,
                libFileNames[LIB_FILE_VERSION_TMP],
#endif
                objectFile,
                pkgDataFlags[LD_SONAME],
                pkgDataFlags[LD_SONAME][0] == 0 ? "" : libFileNames[LIB_FILE_VERSION_MAJOR],
                pkgDataFlags[RPATH_FLAGS],
                pkgDataFlags[BIR_FLAGS]);

        /* Generate the library file. */
        result = runCommand(cmd);

#if U_PLATFORM == U_PF_OS390
        char *env_tmp;
        char PDS_LibName[512];
        char PDS_Name[512];

        PDS_Name[0] = 0;
        PDS_LibName[0] = 0;
        if (specialHandling && uprv_strcmp(libFileNames[LIB_FILE],"libicudata") == 0) {
            if (env_tmp = getenv("ICU_PDS_NAME")) {
                sprintf(PDS_Name, "%s%s",
                        env_tmp,
                        "DA");
                strcat(PDS_Name, getenv("ICU_PDS_NAME_SUFFIX"));
            } else if (env_tmp = getenv("PDS_NAME_PREFIX")) {
                sprintf(PDS_Name, "%s%s",
                        env_tmp,
                        U_ICU_VERSION_SHORT "DA");
            } else {
                sprintf(PDS_Name, "%s%s",
                        "IXMI",
                        U_ICU_VERSION_SHORT "DA");
            }
        } else if (!specialHandling && uprv_strcmp(libFileNames[LIB_FILE],"libicudata_stub") == 0) {
            if (env_tmp = getenv("ICU_PDS_NAME")) {
                sprintf(PDS_Name, "%s%s",
                        env_tmp,
                        "D1");
                strcat(PDS_Name, getenv("ICU_PDS_NAME_SUFFIX"));
            } else if (env_tmp = getenv("PDS_NAME_PREFIX")) {
                sprintf(PDS_Name, "%s%s",
                        env_tmp,
                        U_ICU_VERSION_SHORT "D1");
            } else {
                sprintf(PDS_Name, "%s%s",
                        "IXMI",
                        U_ICU_VERSION_SHORT "D1");
            }
        }

        if (PDS_Name[0]) {
            sprintf(PDS_LibName,"%s%s%s%s%s",
                    "\"//'",
                    getenv("LOADMOD"),
                    "(",
                    PDS_Name,
                    ")'\"");
            sprintf(cmd, "%s %s -o %s %s %s%s %s %s",
                   pkgDataFlags[GENLIB],
                   pkgDataFlags[LDICUDTFLAGS],
                   PDS_LibName,
                   objectFile,
                   pkgDataFlags[LD_SONAME],
                   pkgDataFlags[LD_SONAME][0] == 0 ? "" : libFileNames[LIB_FILE_VERSION_MAJOR],
                   pkgDataFlags[RPATH_FLAGS],
                   pkgDataFlags[BIR_FLAGS]);

            result = runCommand(cmd);
        }
#endif
    }

    if (result != 0) {
        fprintf(stderr, "Error generating library file. Failed command: %s\n", cmd);
    }

    if (freeCmd) {
        uprv_free(cmd);
    }

    return result;
}

static int32_t pkg_createWithAssemblyCode(const char *targetDir, const char mode, const char *gencFilePath) {
    char tempObjectFile[SMALL_BUFFER_MAX_SIZE] = "";
    int32_t result = 0;
    int32_t length = 0;

    /* Remove the ending .s and replace it with .o for the new object file. */
    uprv_strcpy(tempObjectFile, gencFilePath);
    tempObjectFile[uprv_strlen(tempObjectFile)-1] = 'o';

    length = static_cast<int32_t>(uprv_strlen(pkgDataFlags[COMPILER]) + uprv_strlen(pkgDataFlags[LIBFLAGS])
             + uprv_strlen(tempObjectFile) + uprv_strlen(gencFilePath) + BUFFER_PADDING_SIZE);

    LocalMemory<char> cmd((char *)uprv_malloc(sizeof(char) * length));
    if (cmd.isNull()) {
        return -1;
    }

    /* Generate the object file. */
    snprintf(cmd.getAlias(), length, "%s %s -o %s %s",
            pkgDataFlags[COMPILER],
            pkgDataFlags[LIBFLAGS],
            tempObjectFile,
            gencFilePath);

    result = runCommand(cmd.getAlias());

    if (result != 0) {
        fprintf(stderr, "Error creating with assembly code. Failed command: %s\n", cmd.getAlias());
        return result;
    }

    return pkg_generateLibraryFile(targetDir, mode, tempObjectFile);
}

#ifdef BUILD_DATA_WITHOUT_ASSEMBLY
/*
 * Generation of the data library without assembly code needs to compile each data file
 * individually and then link it all together.
 * Note: Any update to the directory structure of the data needs to be reflected here.
 */
enum {
    DATA_PREFIX_BRKITR,
    DATA_PREFIX_COLL,
    DATA_PREFIX_CURR,
    DATA_PREFIX_LANG,
    DATA_PREFIX_RBNF,
    DATA_PREFIX_REGION,
    DATA_PREFIX_TRANSLIT,
    DATA_PREFIX_ZONE,
    DATA_PREFIX_UNIT,
    DATA_PREFIX_LENGTH
};

const static char DATA_PREFIX[DATA_PREFIX_LENGTH][10] = {
        "brkitr",
        "coll",
        "curr",
        "lang",
        "rbnf",
        "region",
        "translit",
        "zone",
        "unit"
};

static int32_t pkg_createWithoutAssemblyCode(UPKGOptions *o, const char *targetDir, const char mode) {
    int32_t result = 0;
    CharList *list = o->filePaths;
    CharList *listNames = o->files;
    int32_t listSize = pkg_countCharList(list);
    char *buffer;
    char *cmd;
    char gencmnFile[SMALL_BUFFER_MAX_SIZE] = "";
    char tempObjectFile[SMALL_BUFFER_MAX_SIZE] = "";
#ifdef USE_SINGLE_CCODE_FILE
    char icudtAll[SMALL_BUFFER_MAX_SIZE] = "";
    FileStream *icudtAllFile = nullptr;
    
    snprintf(icudtAll, sizeof(icudtAll), "%s%s%sall.c",
            o->tmpDir,
            PKGDATA_FILE_SEP_STRING, 
            libFileNames[LIB_FILE]);
    /* Remove previous icudtall.c file. */
    if (T_FileStream_file_exists(icudtAll) && (result = remove(icudtAll)) != 0) {
        fprintf(stderr, "Unable to remove old icudtall file: %s\n", icudtAll);
        return result;
    }

    if((icudtAllFile = T_FileStream_open(icudtAll, "w"))==nullptr) {
        fprintf(stderr, "Unable to write to icudtall file: %s\n", icudtAll);
        return result;
    }
#endif

    if (list == nullptr || listNames == nullptr) {
        /* list and listNames should never be nullptr since we are looping through the CharList with
         * the given size.
         */
        return -1;
    }

    if ((cmd = (char *)uprv_malloc((listSize + 2) * SMALL_BUFFER_MAX_SIZE)) == nullptr) {
        fprintf(stderr, "Unable to allocate memory for cmd.\n");
        return -1;
    } else if ((buffer = (char *)uprv_malloc((listSize + 1) * SMALL_BUFFER_MAX_SIZE)) == nullptr) {
        fprintf(stderr, "Unable to allocate memory for buffer.\n");
        uprv_free(cmd);
        return -1;
    }

    for (int32_t i = 0; i < (listSize + 1); i++) {
        const char *file ;
        const char *name;

        if (i == 0) {
            /* The first iteration calls the gencmn function and initializes the buffer. */
            createCommonDataFile(o->tmpDir, o->shortName, o->entryName, nullptr, o->srcDir, o->comment, o->fileListFiles->str, 0, true, o->verbose, gencmnFile);
            buffer[0] = 0;
#ifdef USE_SINGLE_CCODE_FILE
            uprv_strcpy(tempObjectFile, gencmnFile);
            tempObjectFile[uprv_strlen(tempObjectFile) - 1] = 'o';
            
            sprintf(cmd, "%s %s -o %s %s",
                        pkgDataFlags[COMPILER],
                        pkgDataFlags[LIBFLAGS],
                        tempObjectFile,
                        gencmnFile);
            
            result = runCommand(cmd);
            if (result != 0) {
                break;
            }
            
            sprintf(buffer, "%s",tempObjectFile);
#endif
        } else {
            char newName[SMALL_BUFFER_MAX_SIZE];
            char dataName[SMALL_BUFFER_MAX_SIZE];
            char dataDirName[SMALL_BUFFER_MAX_SIZE];
            const char *pSubstring;
            file = list->str;
            name = listNames->str;

            newName[0] = dataName[0] = 0;
            for (int32_t n = 0; n < DATA_PREFIX_LENGTH; n++) {
                dataDirName[0] = 0;
                sprintf(dataDirName, "%s%s", DATA_PREFIX[n], PKGDATA_FILE_SEP_STRING);
                /* If the name contains a prefix (indicating directory), alter the new name accordingly. */
                pSubstring = uprv_strstr(name, dataDirName);
                if (pSubstring != nullptr) {
                    char newNameTmp[SMALL_BUFFER_MAX_SIZE] = "";
                    const char *p = name + uprv_strlen(dataDirName);
                    for (int32_t i = 0;;i++) {
                        if (p[i] == '.') {
                            newNameTmp[i] = '_';
                            continue;
                        }
                        newNameTmp[i] = p[i];
                        if (p[i] == 0) {
                            break;
                        }
                    }
                    auto ret = snprintf(newName,
                            sizeof(newName),
                            "%s_%s",
                            DATA_PREFIX[n],
                            newNameTmp);
                    (void)ret;
                    U_ASSERT(0 <= ret && ret < SMALL_BUFFER_MAX_SIZE);
                    ret = snprintf(dataName,
                            sizeof(dataName),
                            "%s_%s",
                            o->shortName,
                            DATA_PREFIX[n]);
                    (void)ret;
                    U_ASSERT(0 <= ret && ret < SMALL_BUFFER_MAX_SIZE);
                }
                if (newName[0] != 0) {
                    break;
                }
            }

            if(o->verbose) {
              printf("# Generating %s \n", gencmnFile);
            }

            writeCCode(
                file,
                o->tmpDir,
                nullptr,
                dataName[0] != 0 ? dataName : o->shortName,
                newName[0] != 0 ? newName : nullptr,
                gencmnFile,
                sizeof(gencmnFile));

#ifdef USE_SINGLE_CCODE_FILE
            sprintf(cmd, "#include \"%s\"\n", gencmnFile);
            T_FileStream_writeLine(icudtAllFile, cmd);
            /* don't delete the file */
#endif
        }

#ifndef USE_SINGLE_CCODE_FILE
        uprv_strcpy(tempObjectFile, gencmnFile);
        tempObjectFile[uprv_strlen(tempObjectFile) - 1] = 'o';
        
        sprintf(cmd, "%s %s -o %s %s",
                    pkgDataFlags[COMPILER],
                    pkgDataFlags[LIBFLAGS],
                    tempObjectFile,
                    gencmnFile);
        result = runCommand(cmd);
        if (result != 0) {
            fprintf(stderr, "Error creating library without assembly code. Failed command: %s\n", cmd);
            break;
        }

        uprv_strcat(buffer, " ");
        uprv_strcat(buffer, tempObjectFile);

#endif
        
        if (i > 0) {
            list = list->next;
            listNames = listNames->next;
        }
    }

#ifdef USE_SINGLE_CCODE_FILE
    T_FileStream_close(icudtAllFile);
    uprv_strcpy(tempObjectFile, icudtAll);
    tempObjectFile[uprv_strlen(tempObjectFile) - 1] = 'o';

    sprintf(cmd, "%s %s -I. -o %s %s",
        pkgDataFlags[COMPILER],
        pkgDataFlags[LIBFLAGS],
        tempObjectFile,
        icudtAll);
    
    result = runCommand(cmd);
    if (result == 0) {
        uprv_strcat(buffer, " ");
        uprv_strcat(buffer, tempObjectFile);
    } else {
        fprintf(stderr, "Error creating library without assembly code. Failed command: %s\n", cmd);
    }
#endif

    if (result == 0) {
        /* Generate the library file. */
#if U_PLATFORM == U_PF_OS390
        result = pkg_generateLibraryFile(targetDir, mode, buffer, cmd, (o->pdsbuild && IN_DLL_MODE(mode)));
#else
        result = pkg_generateLibraryFile(targetDir,mode, buffer, cmd);
#endif
    }

    uprv_free(buffer);
    uprv_free(cmd);

    return result;
}
#endif

#ifdef WINDOWS_WITH_MSVC
#define LINK_CMD "link.exe /nologo /release /out:"
#define LINK_FLAGS "/NXCOMPAT /DYNAMICBASE /DLL /NOENTRY /MANIFEST:NO /implib:"

#define LINK_EXTRA_UWP_FLAGS "/APPCONTAINER "
#define LINK_EXTRA_UWP_FLAGS_X86_ONLY "/SAFESEH "

#define LINK_EXTRA_FLAGS_MACHINE "/MACHINE:"
#define LIB_CMD "LIB.exe /nologo /out:"
#define LIB_FILE "icudt.lib"
#define LIB_EXT UDATA_LIB_SUFFIX
#define DLL_EXT UDATA_SO_SUFFIX

static int32_t pkg_createWindowsDLL(const char mode, const char *gencFilePath, UPKGOptions *o) {
    int32_t result = 0;
    char cmd[LARGE_BUFFER_MAX_SIZE];
    if (IN_STATIC_MODE(mode)) {
        char staticLibFilePath[SMALL_BUFFER_MAX_SIZE] = "";

#ifdef CYGWINMSVC
        snprintf(staticLibFilePath, sizeof(staticLibFilePath), "%s%s%s%s%s",
                o->targetDir,
                PKGDATA_FILE_SEP_STRING,
                pkgDataFlags[LIBPREFIX],
                o->libName,
                LIB_EXT);
#else
        snprintf(staticLibFilePath, sizeof(staticLibFilePath), "%s%s%s%s%s",
                o->targetDir,
                PKGDATA_FILE_SEP_STRING,
                (strstr(o->libName, "icudt") ? "s" : ""),
                o->libName,
                LIB_EXT);
#endif

        snprintf(cmd, sizeof(cmd), "%s\"%s\" \"%s\"",
                LIB_CMD,
                staticLibFilePath,
                gencFilePath);
    } else if (IN_DLL_MODE(mode)) {
        char dllFilePath[SMALL_BUFFER_MAX_SIZE] = "";
        char libFilePath[SMALL_BUFFER_MAX_SIZE] = "";
        char resFilePath[SMALL_BUFFER_MAX_SIZE] = "";
        char tmpResFilePath[SMALL_BUFFER_MAX_SIZE] = "";

#ifdef CYGWINMSVC
        uprv_strcpy(dllFilePath, o->targetDir);
#else
        uprv_strcpy(dllFilePath, o->srcDir);
#endif
        uprv_strcat(dllFilePath, PKGDATA_FILE_SEP_STRING);
        uprv_strcpy(libFilePath, dllFilePath);

#ifdef CYGWINMSVC
        uprv_strcat(libFilePath, o->libName);
        uprv_strcat(libFilePath, ".lib");
        
        uprv_strcat(dllFilePath, o->libName);
        uprv_strcat(dllFilePath, o->version);
#else
        if (strstr(o->libName, "icudt")) {
            uprv_strcat(libFilePath, LIB_FILE);
        } else {
            uprv_strcat(libFilePath, o->libName);
            uprv_strcat(libFilePath, ".lib");
        }
        uprv_strcat(dllFilePath, o->entryName);
#endif
        uprv_strcat(dllFilePath, DLL_EXT);
        
        uprv_strcpy(tmpResFilePath, o->tmpDir);
        uprv_strcat(tmpResFilePath, PKGDATA_FILE_SEP_STRING);
        uprv_strcat(tmpResFilePath, ICUDATA_RES_FILE);

        if (T_FileStream_file_exists(tmpResFilePath)) {
            snprintf(resFilePath, sizeof(resFilePath), "\"%s\"", tmpResFilePath);
        }

        /* Check if dll file and lib file exists and that it is not newer than genc file. */
        if (!o->rebuild && (T_FileStream_file_exists(dllFilePath) && isFileModTimeLater(dllFilePath, gencFilePath)) &&
            (T_FileStream_file_exists(libFilePath) && isFileModTimeLater(libFilePath, gencFilePath))) {
          if(o->verbose) {
            printf("# Not rebuilding %s - up to date.\n", gencFilePath);
          }
          return 0;
        }

        char extraFlags[SMALL_BUFFER_MAX_SIZE] = "";
#ifdef WINDOWS_WITH_MSVC
        if (options[WIN_UWP_BUILD].doesOccur) {
            uprv_strcat(extraFlags, LINK_EXTRA_UWP_FLAGS);

            if (options[WIN_DLL_ARCH].doesOccur) {
                if (uprv_strcmp(options[WIN_DLL_ARCH].value, "X86") == 0) {
                    uprv_strcat(extraFlags, LINK_EXTRA_UWP_FLAGS_X86_ONLY);
                }
            }
        }

        if (options[WIN_DLL_ARCH].doesOccur) {
            uprv_strcat(extraFlags, LINK_EXTRA_FLAGS_MACHINE);
            uprv_strcat(extraFlags, options[WIN_DLL_ARCH].value);
        }

#endif
        snprintf(cmd, sizeof(cmd), "%s\"%s\" %s %s\"%s\" \"%s\" %s",
            LINK_CMD,
            dllFilePath,
            extraFlags,
            LINK_FLAGS,
            libFilePath,
            gencFilePath,
            resFilePath
        );
    }

    result = runCommand(cmd, true);
    if (result != 0) {
        fprintf(stderr, "Error creating Windows DLL library. Failed command: %s\n", cmd);
    }

    return result;
}
#endif

static UPKGOptions *pkg_checkFlag(UPKGOptions *o) {
#if U_PLATFORM == U_PF_AIX
    /* AIX needs a map file. */
    char *flag = nullptr;
    int32_t length = 0;
    char tmpbuffer[SMALL_BUFFER_MAX_SIZE];
    const char MAP_FILE_EXT[] = ".map";
    FileStream *f = nullptr;
    char mapFile[SMALL_BUFFER_MAX_SIZE] = "";
    int32_t start = -1;
    uint32_t count = 0;
    const char rm_cmd[] = "rm -f all ;";

    flag = pkgDataFlags[GENLIB];

    /* This portion of the code removes 'rm -f all' in the GENLIB.
     * Only occurs in AIX.
     */
    if (uprv_strstr(flag, rm_cmd) != nullptr) {
        char *tmpGenlibFlagBuffer = nullptr;
        int32_t i, offset;

        length = static_cast<int32_t>(uprv_strlen(flag) + 1);
        tmpGenlibFlagBuffer = (char *)uprv_malloc(length);
        if (tmpGenlibFlagBuffer == nullptr) {
            /* Memory allocation error */
            fprintf(stderr,"Unable to allocate buffer of size: %d.\n", length);
            return nullptr;
        }

        uprv_strcpy(tmpGenlibFlagBuffer, flag);

        offset = static_cast<int32_t>(uprv_strlen(rm_cmd));

        for (i = 0; i < (length - offset); i++) {
            flag[i] = tmpGenlibFlagBuffer[offset + i];
        }

        /* Zero terminate the string */
        flag[i] = 0;

        uprv_free(tmpGenlibFlagBuffer);
    }

    flag = pkgDataFlags[BIR_FLAGS];
    length = static_cast<int32_t>(uprv_strlen(pkgDataFlags[BIR_FLAGS]));

    for (int32_t i = 0; i < length; i++) {
        if (flag[i] == MAP_FILE_EXT[count]) {
            if (count == 0) {
                start = i;
            }
            count++;
        } else {
            count = 0;
        }

        if (count == uprv_strlen(MAP_FILE_EXT)) {
            break;
        }
    }

    if (start >= 0) {
        int32_t index = 0;
        for (int32_t i = 0;;i++) {
            if (i == start) {
                for (int32_t n = 0;;n++) {
                    if (o->shortName[n] == 0) {
                        break;
                    }
                    tmpbuffer[index++] = o->shortName[n];
                }
            }

            tmpbuffer[index++] = flag[i];

            if (flag[i] == 0) {
                break;
            }
        }

        uprv_memset(flag, 0, length);
        uprv_strcpy(flag, tmpbuffer);

        uprv_strcpy(mapFile, o->shortName);
        uprv_strcat(mapFile, MAP_FILE_EXT);

        f = T_FileStream_open(mapFile, "w");
        if (f == nullptr) {
            fprintf(stderr,"Unable to create map file: %s.\n", mapFile);
            return nullptr;
        } else {
            snprintf(tmpbuffer, sizeof(tmpbuffer), "%s%s ", o->entryName, UDATA_CMN_INTERMEDIATE_SUFFIX);
    
            T_FileStream_writeLine(f, tmpbuffer);
    
            T_FileStream_close(f);
        }
    }
#elif U_PLATFORM == U_PF_CYGWIN || U_PLATFORM == U_PF_MINGW
    /* Cygwin needs to change flag options. */
    char *flag = nullptr;
    int32_t length = 0;

    flag = pkgDataFlags[GENLIB];
    length = static_cast<int32_t>(uprv_strlen(pkgDataFlags[GENLIB]));

    int32_t position = length - 1;

    for(;position >= 0;position--) {
        if (flag[position] == '=') {
            position++;
            break;
        }
    }

    uprv_memset(flag + position, 0, length - position);
#elif U_PLATFORM == U_PF_OS400
    /* OS/400 needs to fix the ld options (swap single quote with double quote) */
    char *flag = nullptr;
    int32_t length = 0;

    flag = pkgDataFlags[GENLIB];
    length = static_cast<int32_t>(uprv_strlen(pkgDataFlags[GENLIB]));

    int32_t position = length - 1;

    for(int32_t i = 0; i < length; i++) {
        if (flag[i] == '\'') {
            flag[i] = '\"';
        }
    }
#endif
    // Don't really need a return value, just need to stop compiler warnings about
    // the unused parameter 'o' on platforms where it is not otherwise used.
    return o;   
}

static void loadLists(UPKGOptions *o, UErrorCode *status)
{
    CharList   *l, *tail = nullptr, *tail2 = nullptr;
    FileStream *in;
    char        line[16384];
    char       *linePtr, *lineNext;
    const uint32_t   lineMax = 16300;
    char       *tmp;
    int32_t     tmpLength = 0;
    char       *s;
    int32_t     ln=0; /* line number */

    for(l = o->fileListFiles; l; l = l->next) {
        if(o->verbose) {
            fprintf(stdout, "# pkgdata: Reading %s..\n", l->str);
        }
        /* TODO: stdin */
        in = T_FileStream_open(l->str, "r"); /* open files list */

        if(!in) {
            fprintf(stderr, "Error opening <%s>.\n", l->str);
            *status = U_FILE_ACCESS_ERROR;
            return;
        }

        while(T_FileStream_readLine(in, line, sizeof(line))!=nullptr) { /* for each line */
            ln++;
            if(uprv_strlen(line)>lineMax) {
                fprintf(stderr, "%s:%d - line too long (over %d chars)\n", l->str, (int)ln, (int)lineMax);
                exit(1);
            }
            /* remove spaces at the beginning */
            linePtr = line;
            /* On z/OS, disable call to isspace (#9996).  Investigate using uprv_isspace instead (#9999) */
#if U_PLATFORM != U_PF_OS390
            while(isspace(*linePtr)) {
                linePtr++;
            }
#endif
            s=linePtr;
            /* remove trailing newline characters */
            while(*s!=0) {
                if(*s=='\r' || *s=='\n') {
                    *s=0;
                    break;
                }
                ++s;
            }
            if((*linePtr == 0) || (*linePtr == '#')) {
                continue; /* comment or empty line */
            }

            /* Now, process the line */
            lineNext = nullptr;

            while(linePtr && *linePtr) { /* process space-separated items */
                while(*linePtr == ' ') {
                    linePtr++;
                }
                /* Find the next quote */
                if(linePtr[0] == '"')
                {
                    lineNext = uprv_strchr(linePtr+1, '"');
                    if(lineNext == nullptr) {
                        fprintf(stderr, "%s:%d - missing trailing double quote (\")\n",
                            l->str, (int)ln);
                        exit(1);
                    } else {
                        lineNext++;
                        if(*lineNext) {
                            if(*lineNext != ' ') {
                                fprintf(stderr, "%s:%d - malformed quoted line at position %d, expected ' ' got '%c'\n",
                                    l->str, (int)ln, (int)(lineNext-line), (*lineNext)?*lineNext:'0');
                                exit(1);
                            }
                            *lineNext = 0;
                            lineNext++;
                        }
                    }
                } else {
                    lineNext = uprv_strchr(linePtr, ' ');
                    if(lineNext) {
                        *lineNext = 0; /* terminate at space */
                        lineNext++;
                    }
                }

                /* add the file */
                s = (char*)getLongPathname(linePtr);

                /* normal mode.. o->files is just the bare list without package names */
                o->files = pkg_appendToList(o->files, &tail, uprv_strdup(linePtr));
                if(uprv_pathIsAbsolute(s) || s[0] == '.') {
                    fprintf(stderr, "pkgdata: Error: absolute path encountered. Old style paths are not supported. Use relative paths such as 'fur.res' or 'translit%cfur.res'.\n\tBad path: '%s'\n", U_FILE_SEP_CHAR, s);
                    exit(U_ILLEGAL_ARGUMENT_ERROR);
                }
                /* The +5 is to add a little extra space for, among other things, PKGDATA_FILE_SEP_STRING */
                tmpLength = static_cast<int32_t>(uprv_strlen(o->srcDir) + uprv_strlen(s) + 5);
                if((tmp = (char *)uprv_malloc(tmpLength)) == nullptr) {
                    fprintf(stderr, "pkgdata: Error: Unable to allocate tmp buffer size: %d\n", tmpLength);
                    exit(U_MEMORY_ALLOCATION_ERROR);
                }
                uprv_strcpy(tmp, o->srcDir);
                uprv_strcat(tmp, o->srcDir[uprv_strlen(o->srcDir)-1] == U_FILE_SEP_CHAR ? "" : PKGDATA_FILE_SEP_STRING);
                uprv_strcat(tmp, s);
                o->filePaths = pkg_appendToList(o->filePaths, &tail2, tmp);
                linePtr = lineNext;
            } /* for each entry on line */
        } /* for each line */
        T_FileStream_close(in);
    } /* for each file list file */
}

/* Helper for pkg_getPkgDataPath() */
#if U_HAVE_POPEN
static UBool getPkgDataPath(const char *cmd, UBool verbose, char *buf, size_t items) {
    icu::CharString cmdBuf;
    UErrorCode status = U_ZERO_ERROR;
    icu::LocalPipeFilePointer p;
    size_t n;

    cmdBuf.append(cmd, status);
    if (verbose) {
        fprintf(stdout, "# Calling: %s\n", cmdBuf.data());
    }
    p.adoptInstead( popen(cmdBuf.data(), "r") );

    if (p.isNull() || (n = fread(buf, 1, items-1, p.getAlias())) <= 0) {
        fprintf(stderr, "%s: Error calling '%s'\n", progname, cmd);
        *buf = 0;
        return false;
    }

    return true;
}
#endif

/* Get path to pkgdata.inc. Try pkg-config first, falling back to icu-config. */
static int32_t pkg_getPkgDataPath(UBool verbose, UOption *option) {
#if U_HAVE_POPEN
    static char buf[512] = "";
    UBool pkgconfigIsValid = true;
    const char *pkgconfigCmd = "pkg-config --variable=pkglibdir icu-uc";
    const char *icuconfigCmd = "icu-config --incpkgdatafile";
    const char *pkgdata = "pkgdata.inc";

    if (!getPkgDataPath(pkgconfigCmd, verbose, buf, UPRV_LENGTHOF(buf))) {
        if (!getPkgDataPath(icuconfigCmd, verbose, buf, UPRV_LENGTHOF(buf))) {
            fprintf(stderr, "%s: icu-config not found. Fix PATH or specify -O option\n", progname);
            return -1;
        }

        pkgconfigIsValid = false;
    }

    for (int32_t length = strlen(buf) - 1; length >= 0; length--) {
        if (buf[length] == '\n' || buf[length] == ' ') {
            buf[length] = 0;
        } else {
            break;
        }
    }

    if (!*buf) {
        fprintf(stderr, "%s: Unable to locate pkgdata.inc. Unable to parse the results of '%s'. Check paths or use the -O option to specify the path to pkgdata.inc.\n", progname, pkgconfigIsValid ? pkgconfigCmd : icuconfigCmd);
        return -1;
    }

    if (pkgconfigIsValid) {
        uprv_strcat(buf, U_FILE_SEP_STRING);
        uprv_strcat(buf, pkgdata);
    }

    buf[strlen(buf)] = 0;

    option->value = buf;
    option->doesOccur = true;

    return 0;
#else
    return -1;
#endif
}

#ifdef CAN_WRITE_OBJ_CODE
 /* Create optMatchArch for genccode architecture detection */
static void pkg_createOptMatchArch(char *optMatchArch) {
#if !defined(WINDOWS_WITH_MSVC) || defined(USING_CYGWIN)
    const char* code = "void oma(){}";
    const char* source = "oma.c";
    const char* obj = "oma.obj";
    FileStream* stream = nullptr;

    stream = T_FileStream_open(source,"w");
    if (stream != nullptr) {
        T_FileStream_writeLine(stream, code);
        T_FileStream_close(stream);

        char cmd[LARGE_BUFFER_MAX_SIZE];
        snprintf(cmd, sizeof(cmd), "%s %s -o %s",
            pkgDataFlags[COMPILER],
            source,
            obj);

        if (runCommand(cmd) == 0){
            sprintf(optMatchArch, "%s", obj);
        }
        else {
            fprintf(stderr, "Failed to compile %s\n", source);
        }
        if(!T_FileStream_remove(source)){
            fprintf(stderr, "T_FileStream_remove failed to delete %s\n", source);
        }
    }
    else {
        fprintf(stderr, "T_FileStream_open failed to open %s for writing\n", source);
    }
#endif
}
static void pkg_destroyOptMatchArch(char *optMatchArch) {
    if(T_FileStream_file_exists(optMatchArch) && !T_FileStream_remove(optMatchArch)){
        fprintf(stderr, "T_FileStream_remove failed to delete %s\n", optMatchArch);
    }
}
#endif

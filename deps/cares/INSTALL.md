```
                          ___       __ _ _ __ ___  ___
                         / __| ___ / _` | '__/ _ \/ __|
                        | (_  |___| (_| | | |  __/\__ \
                         \___|     \__,_|_|  \___||___/

                                How To Compile
```

Installing Binary Packages
==========================

Lots of people download binary distributions of c-ares. This document
does not describe how to install c-ares using such a binary package.
This document describes how to compile, build and install c-ares from
source code.

Building from Git
=================

If you get your code off a Git repository rather than an official
release tarball, see the [GIT-INFO](GIT-INFO) file in the root directory
for specific instructions on how to proceed.

In particular, if not using CMake you will need to run `./buildconf` (Unix) or
`buildconf.bat` (Windows) to generate build files, and for the former
you will need a local installation of Autotools.  If using CMake the steps are
the same for both Git and official release tarballs.

AutoTools Build
===============

### General Information, works on most Unix Platforms (Linux, FreeBSD, etc.)

A normal Unix installation is made in three or four steps (after you've
unpacked the source archive):

    ./configure
    make
    make install

You probably need to be root when doing the last command.

If you have checked out the sources from the git repository, read the
[GIT-INFO](GIT-INFO) on how to proceed.

Get a full listing of all available configure options by invoking it like:

    ./configure --help

If you want to install c-ares in a different file hierarchy than /usr/local,
you need to specify that already when running configure:

    ./configure --prefix=/path/to/c-ares/tree

If you happen to have write permission in that directory, you can do `make
install` without being root. An example of this would be to make a local
installation in your own home directory:

    ./configure --prefix=$HOME
    make
    make install

### More Options

To force configure to use the standard cc compiler if both cc and gcc are
present, run configure like

    CC=cc ./configure
    # or
    env CC=cc ./configure

To force a static library compile, disable the shared library creation
by running configure like:

    ./configure --disable-shared

If you're a c-ares developer and use gcc, you might want to enable more
debug options with the `--enable-debug` option.

### Special Cases

Some versions of uClibc require configuring with `CPPFLAGS=-D_GNU_SOURCE=1`
to get correct large file support.

The Open Watcom C compiler on Linux requires configuring with the variables:

    ./configure CC=owcc AR="$WATCOM/binl/wlib" AR_FLAGS=-q \
        RANLIB=/bin/true STRIP="$WATCOM/binl/wstrip" CFLAGS=-Wextra


### CROSS COMPILE

(This section was graciously brought to us by Jim Duey, with additions by
Dan Fandrich)

Download and unpack the c-ares package.

`cd` to the new directory. (e.g. `cd c-ares-1.7.6`)

Set environment variables to point to the cross-compile toolchain and call
configure with any options you need.  Be sure and specify the `--host` and
`--build` parameters at configuration time.  The following script is an
example of cross-compiling for the IBM 405GP PowerPC processor using the
toolchain from MonteVista for Hardhat Linux.

```sh
#! /bin/sh

export PATH=$PATH:/opt/hardhat/devkit/ppc/405/bin
export CPPFLAGS="-I/opt/hardhat/devkit/ppc/405/target/usr/include"
export AR=ppc_405-ar
export AS=ppc_405-as
export LD=ppc_405-ld
export RANLIB=ppc_405-ranlib
export CC=ppc_405-gcc
export NM=ppc_405-nm

./configure --target=powerpc-hardhat-linux \
     --host=powerpc-hardhat-linux \
     --build=i586-pc-linux-gnu \
     --prefix=/opt/hardhat/devkit/ppc/405/target/usr/local \
     --exec-prefix=/usr/local
```

You may also need to provide a parameter like `--with-random=/dev/urandom`
to configure as it cannot detect the presence of a random number
generating device for a target system.  The `--prefix` parameter
specifies where c-ares will be installed.  If `configure` completes
successfully, do `make` and `make install` as usual.

In some cases, you may be able to simplify the above commands to as
little as:

    ./configure --host=ARCH-OS


### Cygwin (Windows)

Almost identical to the unix installation. Run the configure script in the
c-ares root with `sh configure`. Make sure you have the sh executable in
`/bin/` or you'll see the configure fail toward the end.

Run `make`


### QNX

(This section was graciously brought to us by David Bentham)

As QNX is targeted for resource constrained environments, the QNX headers
set conservative limits. This includes the `FD_SETSIZE` macro, set by default
to 32. Socket descriptors returned within the c-ares library may exceed this,
resulting in memory faults/SIGSEGV crashes when passed into `select(..)`
calls using `fd_set` macros.

A good all-round solution to this is to override the default when building
c-ares, by overriding `CFLAGS` during configure, example:

    # configure CFLAGS='-DFD_SETSIZE=64 -g -O2'


### RISC OS

The library can be cross-compiled using gccsdk as follows:

    CC=riscos-gcc AR=riscos-ar RANLIB='riscos-ar -s' ./configure \
         --host=arm-riscos-aof --without-random --disable-shared
    make

where `riscos-gcc` and `riscos-ar` are links to the gccsdk tools.
You can then link your program with `c-ares/lib/.libs/libcares.a`.


### Android

Method using a configure cross-compile (tested with Android NDK r7b):

  - prepare the toolchain of the Android NDK for standalone use; this can
    be done by invoking the script:

        ./tools/make-standalone-toolchain.sh

    which creates a usual cross-compile toolchain. Let's assume that you put
    this toolchain below `/opt` then invoke configure with something
    like:

    ```
        export PATH=/opt/arm-linux-androideabi-4.4.3/bin:$PATH
       ./configure --host=arm-linux-androideabi [more configure options]
        make
    ```
  - if you want to compile directly from our GIT repo you might run into
    this issue with older automake stuff:

    ```
        checking host system type...
        Invalid configuration `arm-linux-androideabi':
        system `androideabi' not recognized
        configure: error: /bin/sh ./config.sub arm-linux-androideabi failed
    ```
    this issue can be fixed with using more recent versions of `config.sub`
    and `config.guess` which can be obtained here:
    http://git.savannah.gnu.org/gitweb/?p=config.git;a=tree
    you need to replace your system-own versions which usually can be
    found in your automake folder:
    `find /usr -name config.sub`


CMake builds
============

Current releases of c-ares introduce a CMake v3+ build system that has been
tested on most platforms including Windows, Linux, FreeBSD, macOS, AIX and
Solaris.

In the most basic form, building with CMake might look like:

```sh
cd /path/to/cmake/source
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local/cares ..
make
sudo make install
```

Options
-------

Options to CMake are passed on the command line using "-D${OPTION}=${VALUE}".
The values defined are all boolean and take values like On, Off, True, False.

| Option Name                 | Description                                                           | Default Value  |
|-----------------------------|-----------------------------------------------------------------------|----------------|
| CARES_STATIC                | Build the static library                                              | Off            |
| CARES_SHARED                | Build the shared library                                              | On             |
| CARES_INSTALL               | Hook in installation, useful to disable if chain building             | On             |
| CARES_STATIC_PIC            | Build the static library as position-independent                      | Off            |
| CARES_BUILD_TESTS           | Build and run tests                                                   | Off            |
| CARES_BUILD_CONTAINER_TESTS | Build and run container tests (implies CARES_BUILD_TESTS, Linux only) | Off            |
| CARES_BUILD_TOOLS           | Build tools                                                           | On             |
| CARES_SYMBOL_HIDING         | Hide private symbols in shared libraries                              | Off            |
| CARES_THREADS               | Build with thread-safety support                                      | On             |

Ninja
-----

Ninja is the next-generation build system meant for generators like CMake that
heavily parallelize builds.  Its use is very similar to the normal build:

```sh
cd /path/to/cmake/source
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local/cares -G "Ninja" ..
ninja
sudo ninja install
```

Windows MSVC Command Line
-------------------------

```
cd \path\to\cmake\source
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=C:\cares -G "NMake Makefiles" ..
nmake
nmake install
```

Windows MinGW-w64 Command Line via MSYS
---------------------------------------
```
cd \path\to\cmake\source
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=C:\cares -G "MSYS Makefiles" ..
make
make install
```


Platform-specific build systems
===============================

Win32
-----

### Building Windows DLLs and C run-time (CRT) linkage issues

As a general rule, building a DLL with static CRT linkage is highly
discouraged, and intermixing CRTs in the same app is something to
avoid at any cost.

Reading and comprehension of the following Microsoft Learn article
is a must for any Windows developer. Especially
important is full understanding if you are not going to follow the
advice given above.

 - [Use the C Run-Time](https://learn.microsoft.com/en-us/troubleshoot/developer/visualstudio/cpp/libraries/use-c-run-time)

If your app is misbehaving in some strange way, or it is suffering
from memory corruption, before asking for further help, please try
first to rebuild every single library your app uses as well as your
app using the debug multithreaded dynamic C runtime.


### MSYS

Building is supported for native windows via both AutoTools and CMake.  When
building with autotools, you can only build either a shared version or a static
version (use `--disable-shared` or `--disable-static`).  CMake can build both
simultaneously.

All of the MSYS environments are supported: `MINGW32`, `MINGW64`, `UCRT64`,
`CLANG32`, `CLANG64`, `CLANGARM64`.

### MingW32

Make sure that MinGW32's bin dir is in the search path, for example:

    set PATH=c:\mingw32\bin;%PATH%

then run 'make -f Makefile.m32' in the root dir.


### MSVC 6 caveats

If you use MSVC 6 it is required that you use the February 2003 edition PSDK:
http://www.microsoft.com/msdownload/platformsdk/sdkupdate/psdk-full.htm


### MSVC from command line

Run the `vcvars32.bat` file to get a proper environment. The
`vcvars32.bat` file is part of the Microsoft development environment and
you may find it in `C:\Program Files\Microsoft Visual Studio\vc98\bin`
provided that you installed Visual C/C++ 6 in the default directory.

Further details in [README.msvc](README.msvc)


### Important static c-ares usage note

When building an application that uses the static c-ares library, you must
add `-DCARES_STATICLIB` to your `CFLAGS`.  Otherwise the linker will look for
dynamic import symbols.


DOS
---

c-ares supports building as a 32bit protected mode application via
[DJGPP](https://www.delorie.com/djgpp/).  It is recommended to use a DJGPP
cross compiler from [Andrew Wu](https://github.com/andrewwutw/build-djgpp)
as building directly in a DOS environment can be difficult.

It is required to also have [Watt-32](https://www.watt-32.net/) available
built using the same compiler.  It is recommended to build the latest `master`
branch from [GitHub](https://github.com/sezero/watt32/tree/master).

Finally, the `DJ_PREFIX` and `WATT_ROOT` environment variables must be set
appropriately before calling `make Makefile.dj` to build c-ares.

Please refer to our CI
[GitHub Actions Workflow](https://github.com/c-ares/c-ares/blob/main/.github/workflows/djgpp.yml)
for a full build example, including building the latest Watt-32 release.


IBM OS/2
--------

Building under OS/2 is not much different from building under unix.
You need:

  - emx 0.9d
  - GNU make
  - GNU patch
  - ksh
  - GNU bison
  - GNU file utilities
  - GNU sed
  - autoconf 2.13

If during the linking you get an error about `_errno` being an undefined
symbol referenced from the text segment, you need to add `-D__ST_MT_ERRNO__`
in your definitions.

If you're getting huge binaries, probably your makefiles have the `-g` in
`CFLAGS`.


NetWare
-------

To compile `libcares.a` / `libcares.lib` you need:

 - either any gcc / nlmconv, or CodeWarrior 7 PDK 4 or later.
 - gnu make and awk running on the platform you compile on;
   native Win32 versions can be downloaded from:
   http://www.gknw.net/development/prgtools/
 - recent Novell LibC SDK available from:
   http://developer.novell.com/ndk/libc.htm
 - or recent Novell CLib SDK available from:
   http://developer.novell.com/ndk/clib.htm

Set a search path to your compiler, linker and tools; on Linux make
sure that the var `OSTYPE` contains the string 'linux'; set the var
`NDKBASE` to point to the base of your Novell NDK; and then type
`make -f Makefile.netware` from the top source directory;

VCPKG
=====

You can build and install c-ares using [vcpkg](https://github.com/Microsoft/vcpkg/) dependency manager:

```sh or powershell
    git clone https://github.com/Microsoft/vcpkg.git
    cd vcpkg
    ./bootstrap-vcpkg.sh
    ./vcpkg integrate install
    ./vcpkg install c-ares
```

The c-ares port in vcpkg is kept up to date by Microsoft team members and community contributors. If the version is out of date, please [create an issue or pull request](https://github.com/Microsoft/vcpkg) on the vcpkg repository.

WATCOM
=====

To build c-ares with OpenWatcom, you need to have at least version 1.9 of OpenWatcom. You can get the latest version from [http://openwatcom.org/ftp/install/](http://openwatcom.org/ftp/install/). Install the version that corresponds to your current host platform.

After installing OpenWatcom, open a new command prompt and execute the following commands:

```
  cd \path\to\cmake\source
  buildconf.bat
  wmake -u -f Makefile.Watcom
```

After running wmake, you should get adig.exe, ahost.exe, and the static and dynamic versions of libcares.

PORTS
=====

This is a probably incomplete list of known hardware and operating systems
that c-ares has been compiled for. If you know a system c-ares compiles and
runs on, that isn't listed, please let us know!

     - Linux (i686, x86_64, AARCH64, and more)
     - MacOS 10.4+
     - iOS
     - Windows 8+ (i686, x86_64)
     - Android (ARM, AARCH64, x86_64)
     - FreeBSD
     - NetBSD
     - OpenBSD
     - Solaris (SPARC, x86_64)
     - AIX (POWER)
     - Tru64 (Alpha)
     - IRIX (MIPS)
     - Novell NetWare (i386)


Useful URLs
===========

 - c-ares: https://c-ares.org/
 - MinGW-w64: http://mingw-w64.sourceforge.net/
 - MSYS2: https://msys2.org
 - OpenWatcom: http://www.openwatcom.org/

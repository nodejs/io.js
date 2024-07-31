Developer Notes
===============

* The distributed `ares_build.h` in the official release tarballs is only
  intended to be used on systems which can also not run the also distributed
  `configure` or `CMake` scripts.  It is generated as a copy of
  `ares_build.h.dist` as can be seen in the code repository.

* If you check out from git on a non-`configure` or `CMake` platform, you must run
  the appropriate `buildconf*` script to set up `ares_build.h` and other local
  files before being able to compile the library.  There are pre-made makefiles
  for a subset of such systems such as Watcom, NMake, and MinGW Makefiles. 

* On systems capable of running the `configure` or `CMake` scripts, the process
  will overwrite the distributed `ares_build.h` file with one that is suitable
  and specific to the library being configured and built, this new file is
  generated from the `ares_build.h.in` and `ares_build.h.cmake` template files.

* If you intend to distribute an already compiled c-ares library you **MUST**
  also distribute along with it the generated `ares_build.h` which has been
  used to compile it. Otherwise, the library will be of no use for the users of
  the library that you have built. It is **your** responsibility to provide this
  file. No one at the c-ares project can know how you have built the library.
  The generated file includes platform and configuration dependent info,
  and must not be modified by anyone.

* We support both the AutoTools `configure` based build system as well as the
  `CMake` build system.  Any new code changes must work with both.

* The files that get compiled and are present in the distribution are referenced
  in the `Makefile.inc` in the current directory.  This file gets included in
  every build system supported by c-ares so that the list of files doesn't need
  to be maintained per build system.  Don't forget to reference new header files
  otherwise they won't be included in the official release tarballs.

* We cannot assume anything else but very basic C89 compiler features being
  present. The lone exception is the requirement for 64bit integers which is
  not a requirement for C89 compilers to support. Please do not use any extended
  features released by later standards.

* Newlines must remain unix-style for older compilers' sake.

* Comments must be written in the old-style `/* unnested C-fashion */`

* Try to keep line lengths below 80 columns and formatted as the existing code.
  There is a `.clang-format` in the repository that can be used to run the
  automated code formatter as such: `clang-format -i */*.c */*.h */*/*.c */*/*.h`

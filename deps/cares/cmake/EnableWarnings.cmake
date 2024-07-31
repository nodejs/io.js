# Copyright (c) Monetra Technologies LLC
# SPDX-License-Identifier: MIT

# EnableWarnings.cmake
#
# Checks for and turns on a large number of warning C flags.
#
# Adds the following helper functions:
#
#	remove_warnings(... list of warnings ...)
#		Turn off given list of individual warnings for all targets and subdirectories added after this.
#
#   remove_all_warnings()
#		Remove all warning flags, add -w to suppress built-in warnings.
#
#   remove_all_warnings_from_targets(... list of targets ...)
#       Suppress warnings for the given targets only.
#
#   push_warnings()
#		Save current warning flags by pushing them onto an internal stack. Note that modifications to the internal
#		stack are only visible in the current CMakeLists.txt file and its children.
#
#       Note: changing warning flags multiple times in the same directory only affects add_subdirectory() calls.
#             Targets in the directory will always use the warning flags in effect at the end of the CMakeLists.txt
#             file - this is due to really weird and annoying legacy behavior of CMAKE_C_FLAGS.
#
#   pop_warnings()
#       Restore the last set of flags that were saved with push_warnings(). Note that modifications to the internal
#		stack are only visible in the current CMakeLists.txt file and its children.
#

if (_internal_enable_warnings_already_run)
	return()
endif ()
set(_internal_enable_warnings_already_run TRUE)

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)

get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# Helper functions


# This function can be called in subdirectories, to prune out warnings that they don't want.
#  vararg: warning flags to remove from list of enabled warnings. All "no" flags after EXPLICIT_DISABLE
#          will be added to C flags.
#
# Ex.: remove_warnings(-Wall -Wdouble-promotion -Wcomment) prunes those warnings flags from the compile command.
function(remove_warnings)
	get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)
	set(langs C)
	if ("CXX" IN_LIST languages)
		list(APPEND langs CXX)
	endif ()

	foreach(lang ${langs})
		set(toadd)
		set(in_explicit_disable FALSE)
		foreach (flag ${ARGN})
			if (flag STREQUAL "EXPLICIT_DISABLE")
				set(in_explicit_disable TRUE)
			elseif (in_explicit_disable)
				list(APPEND toadd "${flag}")
			else ()
				string(REGEX REPLACE "${flag}([ \t]+|$)" "" CMAKE_${lang}_FLAGS "${CMAKE_${lang}_FLAGS}")
			endif ()
		endforeach ()
		_int_enable_warnings_set_flags(lang ${toadd})
		string(STRIP "${CMAKE_${lang}_FLAGS}" CMAKE_${lang}_FLAGS)
		set(CMAKE_${lang}_FLAGS "${CMAKE_${lang}_FLAGS}" PARENT_SCOPE)
	endforeach()
endfunction()


# Explicitly suppress all warnings. As long as this flag is the last warning flag, warnings will be
# suppressed even if earlier flags enabled warnings.
function(remove_all_warnings)
	get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)
	set(langs C)
	if ("CXX" IN_LIST languages)
		list(APPEND langs CXX)
	endif ()

	foreach(lang ${langs})
		string(REGEX REPLACE "[-/][Ww][^ \t]*([ \t]+|$)" "" CMAKE_${lang}_FLAGS "${CMAKE_${lang}_FLAGS}")
		if (MSVC)
			string(APPEND CMAKE_${lang}_FLAGS " /w")
		else ()
			string(APPEND CMAKE_${lang}_FLAGS " -w")
		endif ()
		string(STRIP "${CMAKE_${lang}_FLAGS}" CMAKE_${lang}_FLAGS)
		set(CMAKE_${lang}_FLAGS "${CMAKE_${lang}_FLAGS}" PARENT_SCOPE)
	endforeach()
endfunction()


function(remove_all_warnings_from_targets)
	foreach (target ${ARGN})
		if (MSVC)
			target_compile_options(${target} PRIVATE "/w")
		else ()
			target_compile_options(${target} PRIVATE "-w")
		endif ()
	endforeach()
endfunction()


# Save the current warning settings to an internal variable.
function(push_warnings)
	get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)
	set(langs C)
	if ("CXX" IN_LIST languages)
		list(APPEND langs CXX)
	endif ()

	foreach(lang ${langs})
		if (CMAKE_${lang}_FLAGS MATCHES ";")
			message(AUTHOR_WARNING "Cannot push warnings for ${lang}, CMAKE_${lang}_FLAGS contains semicolons")
			continue()
		endif ()
		# Add current flags to end of internal list.
		list(APPEND _enable_warnings_internal_${lang}_flags_stack "${CMAKE_${lang}_FLAGS}")
		# Propagate results up to caller's scope.
		set(_enable_warnings_internal_${lang}_flags_stack "${_enable_warnings_internal_${lang}_flags_stack}" PARENT_SCOPE)
	endforeach()
endfunction()


# Restore the current warning settings from an internal variable.
function(pop_warnings)
	get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)
	set(langs C)
	if ("CXX" IN_LIST languages)
		list(APPEND langs CXX)
	endif ()

	foreach(lang ${langs})
		if (NOT _enable_warnings_internal_${lang}_flags_stack)
			continue()
		endif ()
		# Pop flags off of end of list, overwrite current flags with whatever we popped off.
		list(GET _enable_warnings_internal_${lang}_flags_stack -1 CMAKE_${lang}_FLAGS)
		list(REMOVE_AT _enable_warnings_internal_${lang}_flags_stack -1)
		# Propagate results up to caller's scope.
		set(_enable_warnings_internal_${lang}_flags_stack "${_enable_warnings_internal_${lang}_flags_stack}" PARENT_SCOPE)
		string(STRIP "${CMAKE_${lang}_FLAGS}" CMAKE_${lang}_FLAGS)
		set(CMAKE_${lang}_FLAGS "${CMAKE_${lang}_FLAGS}" PARENT_SCOPE)
	endforeach()
endfunction()


# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# Runs when included automatically

# internal helper: _int_enable_warnings_set_flags_ex(langs_var configs_var [warnings flags])
function(_int_enable_warnings_set_flags_ex langs_var configs_var)
	if (NOT ARGN)
		return()
	endif ()

	if (NOT ${configs_var})
		set(${configs_var} "NONE")
	endif ()
	string(TOUPPER "${${configs_var}}" ${configs_var})

	foreach(_flag ${ARGN})
		string(MAKE_C_IDENTIFIER "HAVE_${_flag}" varname)

		if ("C" IN_LIST ${langs_var})
			check_c_compiler_flag(${_flag} ${varname})
			if (${varname})
				foreach (config IN LISTS ${configs_var})
					if (config STREQUAL "NONE")
						set(config)
					else ()
						set(config "_${config}")
					endif ()
					string(APPEND CMAKE_C_FLAGS${config} " ${_flag}")
				endforeach ()
			endif ()
		endif ()

		if ("CXX" IN_LIST ${langs_var})
			string(APPEND varname "_CXX")
			check_cxx_compiler_flag(${_flag} ${varname})
			if (${varname})
				foreach (config IN LISTS ${configs_var})
					if (config STREQUAL "NONE")
						set(config)
					else ()
						set(config "_${config}")
					endif ()
					string(APPEND CMAKE_CXX_FLAGS${config} " ${_flag}")
				endforeach ()
			endif ()
		endif ()
	endforeach()

	foreach(lang C CXX)
		foreach (config IN LISTS ${configs_var})
			string(TOUPPER "${config}" config)
			if (config STREQUAL "NONE")
				set(config)
			else ()
				set(config "_${config}")
			endif ()
			string(STRIP "${CMAKE_${lang}_FLAGS${config}}" CMAKE_${lang}_FLAGS${config})
			set(CMAKE_${lang}_FLAGS${config} "${CMAKE_${lang}_FLAGS${config}}" PARENT_SCOPE)
		endforeach ()
	endforeach()
endfunction()

# internal helper: _int_enable_warnings_set_flags(langs_var [warnings flags])
macro(_int_enable_warnings_set_flags langs_var)
	set(configs "NONE")
	_int_enable_warnings_set_flags_ex(${langs_var} configs ${ARGN})
endmacro()

set(_flags_C)
set(_flags_CXX)
set(_debug_flags_C)
set(_debug_flags_CXX)

if (MSVC)
	# Don't automatically set /W3
	CMAKE_POLICY (SET CMP0092 NEW)

	# Visual Studio uses a completely different nomenclature for warnings than gcc/mingw/clang, so none of the
	# "-W[name]" warnings will work.

	set(_flags
		# Enable warnings
		/W4 # Baseline reasonable warnings
		/w14242 # 'identifier': conversion from 'type1' to 'type2', possible loss of data
		/w14254 # 'operator': conversion from 'type1:field_bits' to 'type2:field_bits', possible loss of data
		/w14263 # 'function': member function does not override any base class virtual member function
		/w14265 # 'classname': class has virtual functions, but destructor is not virtual instances of this class may
		        # not be destructed correctly
		/w14287 # 'operator': unsigned/negative constant mismatch
		/we4289 # nonstandard extension used: 'variable': loop control variable declared in the for-loop is used outside
		        # the for-loop scope
		/w14296 # 'operator': expression is always 'boolean_value'
		/w14311 # 'variable': pointer truncation from 'type1' to 'type2'
		/w14545 # expression before comma evaluates to a function which is missing an argument list
		/w14546 # function call before comma missing argument list
		/w14547 # 'operator': operator before comma has no effect; expected operator with side-effect
		/w14549 # 'operator': operator before comma has no effect; did you intend 'operator'?
		/w14555 # expression has no effect; expected expression with side- effect
		/w14619 # pragma warning: there is no warning number 'number'
		/w14640 # Enable warning on thread un-safe static member initialization
		/w14826 # Conversion from 'type1' to 'type2' is sign-extended. This may cause unexpected runtime behavior.
		/w14905 # wide string literal cast to 'LPSTR'
		/w14906 # string literal cast to 'LPWSTR'
		/w14928 # illegal copy-initialization; more than one user-defined conversion has been implicitly applied

		# Disable some warnings
		/wd4201 # nonstandard extension used: nameless struct/union. Used in some windows headers, e.g. IO_STATUS_BLOCK,
		        # disable.

		# Turn some warnings into errors
		/we4013 # Treat "function undefined, assuming extern returning int" warning as an error. https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-3-c4013
	)

	list(APPEND _flags_C   ${_flags})
	list(APPEND _flags_CXX ${_flags})

elseif (CMAKE_C_COMPILER_ID MATCHES "Intel")
	# Intel's compiler warning flags are more like Visual Studio than GCC, though the numbers aren't the same.
	set(_flags
		# Use warning level 3, quite wordy.
		-w3
		# Disable warnings we don't care about (add more as they are encountered).
		-wd383    # Spammy warning about initializing from a temporary object in C++ (which is done all the time ...).
		-wd11074  # Diagnostic related to inlining.
		-wd11076  # Diagnostic related to inlining.
	)

	list(APPEND _flags_C   ${_flags})
	list(APPEND _flags_CXX ${_flags})

elseif (CMAKE_C_COMPILER_ID MATCHES "XL")
	set (_flags
		-qwarn64
		-qformat=all
		-qflag=i:i
	)
	list(APPEND _flags_C   ${_flags})
	list(APPEND _flags_CXX ${_flags})

else ()
	# If we're compiling with GCC / Clang / MinGW (or anything else besides Visual Studio or Intel):
	# C Flags:
	list(APPEND _flags_C
		-Wall
		-Wextra

		# Enable additional warnings not covered by Wall and Wextra.
		-Waggregate-return
		-Wcast-align
		-Wcast-qual
		-Wconversion
		-Wdeclaration-after-statement
		-Wdouble-promotion
		-Wfloat-equal
		-Wformat-security
		-Winit-self
		-Wjump-misses-init
		-Wlogical-op
		-Wmissing-braces
		-Wmissing-declarations
		-Wmissing-format-attribute
		-Wmissing-include-dirs
		-Wmissing-prototypes
		-Wnested-externs
		-Wno-coverage-mismatch
		-Wold-style-definition
		-Wpacked
		-Wpedantic
		-Wpointer-arith
		-Wredundant-decls
		-Wshadow
		-Wsign-conversion
		-Wstrict-overflow
		-Wstrict-prototypes
		-Wtrampolines
		-Wundef
		-Wunreachable-code
		-Wunused
		-Wvariadic-macros
		-Wvla
		-Wwrite-strings

		# On Windows MinGW I think implicit fallthrough enabled by -Wextra must not default to 3
		-Wimplicit-fallthrough=3

		# Treat implicit variable typing and implicit function declarations as errors.
		-Werror=implicit-int
		-Werror=implicit-function-declaration

		# Make MacOSX honor -mmacosx-version-min
		-Werror=partial-availability

		# Some clang versions might warn if an argument like "-I/path/to/headers" is unused,
		# silence these.
		-Qunused-arguments

		-Wno-long-long
	)

	# C++ flags:
	list(APPEND _flags_CXX
		-Wall
		-Wextra

		# Enable additional warnings not covered by Wall and Wextra.
		-Wcast-align
		-Wformat-security
		-Wmissing-declarations
		-Wmissing-format-attribute
		-Wpacked-bitfield-compat
		-Wredundant-decls
		-Wvla

		# Turn off unused parameter warnings with C++ (they happen often in C++ and Qt).
		-Wno-unused-parameter

		# Some clang versions might warn if an argument like "-I/path/to/headers" is unused,
		# silence these.
		-Qunused-arguments
	)

	# Note: when cross-compiling to Windows from Cygwin, the Qt Mingw packages have a bunch of
	#       noisy type-conversion warnings in headers. So, only enable those warnings if we're
	#       not building that configuration.
	if (NOT (WIN32 AND (CMAKE_HOST_SYSTEM_NAME MATCHES "CYGWIN")))
		list(APPEND _flags_CXX
			-Wconversion
			-Wfloat-equal
			-Wsign-conversion
		)
	endif ()

	# Add flags to force colored output even when output is redirected via pipe.
	if (CMAKE_GENERATOR MATCHES "Ninja")
		set(color_default TRUE)
	else ()
		set(color_default FALSE)
	endif ()
	option(FORCE_COLOR "Force compiler to always colorize, even when output is redirected." ${color_default})
	mark_as_advanced(FORCE FORCE_COLOR)
	if (FORCE_COLOR)
		set(_flags
			-fdiagnostics-color=always # GCC
			-fcolor-diagnostics        # Clang
		)
		list(APPEND _flags_C   ${_flags})
		list(APPEND _flags_CXX ${_flags})
	endif ()

	# Add -fno-omit-frame-pointer (and optionally -fno-inline) to make debugging and stack dumps nicer.
	set(_flags
		-fno-omit-frame-pointer
	)
	option(M_NO_INLINE "Disable function inlining for RelWithDebInfo and Debug configurations?" FALSE)
	if (M_NO_INLINE)
		list(APPEND _flags
			-fno-inline
		)
	endif ()
	list(APPEND _debug_flags_C   ${_flags})
	list(APPEND _debug_flags_CXX ${_flags})
endif ()

# Check and set compiler flags.
set(_debug_configs
	RelWithDebInfo
	Debug
)
foreach(_lang ${languages})
	_int_enable_warnings_set_flags(_lang ${_flags_${_lang}})
	_int_enable_warnings_set_flags_ex(_lang _debug_configs ${_debug_flags_${_lang}})

	# Ensure pure Debug builds are NOT optimized (not possible on Visual Studio).
	# Any optimization of a Debug build will prevent debuggers like lldb from
	# fully displaying backtraces and stepping.
	if (NOT MSVC)
		set(_config Debug)
		_int_enable_warnings_set_flags_ex(_lang _config -O0)
	endif ()
endforeach()

# CMP0092 doesn't appear to really work, really remove the /W3 here.
if (MSVC)
  remove_warnings(/W3)
endif ()

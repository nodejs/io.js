@if not defined DEBUG_HELPER @ECHO OFF

:: Other scripts rely on the environment variables set in this script, so we
:: explicitly allow them to persist in the calling shell.
endlocal

set "arg=%1"
if /i "%arg:~-1%"=="?" goto help
if /i "%arg:~-4%"=="help" goto help

cd %~dp0

set JS_SUITES=default
set NATIVE_SUITES=addons js-native-api node-api
@rem CI_* variables should be kept synchronized with the ones in Makefile
set "CI_NATIVE_SUITES=%NATIVE_SUITES% benchmark"
set "CI_JS_SUITES=%JS_SUITES% pummel"
set CI_DOC=doctool
@rem Same as the test-ci target in Makefile
set "common_test_suites=%JS_SUITES% %NATIVE_SUITES%&set build_addons=1&set build_js_native_api_tests=1&set build_node_api_tests=1"

@rem Process arguments.
set config=Release
set target=Build
set target_arch=x64
set ltcg=
set target_env=
set noprojgen=
set projgen=
set clang_cl=
set nobuild=
set sign=
set nosnapshot=
set nonpm=
set nocorepack=
set cctest_args=
set test_args=
set stage_package=
set package=
set msi=
set upload=
set licensertf=
set lint_js=
set lint_js_build=
set lint_js_fix=
set lint_cpp=
set lint_md=
set lint_md_build=
set format_md=
set i18n_arg=
set download_arg=
set build_release=
set configure_flags=
set enable_vtune_arg=
set build_addons=
set dll=
set enable_static=
set build_js_native_api_tests=
set build_node_api_tests=
set test_node_inspect=
set test_check_deopts=
set v8_test_options=
set v8_build_options=
set http2_debug=
set nghttp2_debug=
set link_module=
set no_cctest=
set cctest=
set openssl_no_asm=
set no_shared_roheap=
set doc=
set extra_msbuild_args=
set compile_commands=
set exit_code=0

:next-arg
if "%1"=="" goto args-done
if /i "%1"=="debug"         set config=Debug&goto arg-ok
if /i "%1"=="release"       set config=Release&set ltcg=1&set cctest=1&goto arg-ok
if /i "%1"=="clean"         set target=Clean&goto arg-ok
if /i "%1"=="testclean"     set target=TestClean&goto arg-ok
if /i "%1"=="ia32"          set target_arch=x86&goto arg-ok
if /i "%1"=="x86"           set target_arch=x86&goto arg-ok
if /i "%1"=="x64"           set target_arch=x64&goto arg-ok
if /i "%1"=="arm64"         set target_arch=arm64&goto arg-ok
if /i "%1"=="vs2022"        set target_env=vs2022&goto arg-ok
if /i "%1"=="noprojgen"     set noprojgen=1&goto arg-ok
if /i "%1"=="projgen"       set projgen=1&goto arg-ok
if /i "%1"=="clang-cl"      set clang_cl=1&goto arg-ok
if /i "%1"=="nobuild"       set nobuild=1&goto arg-ok
if /i "%1"=="nosign"        set "sign="&echo Note: vcbuild no longer signs by default. "nosign" is redundant.&goto arg-ok
if /i "%1"=="sign"          set sign=1&goto arg-ok
if /i "%1"=="nosnapshot"    set nosnapshot=1&goto arg-ok
if /i "%1"=="nonpm"         set nonpm=1&goto arg-ok
if /i "%1"=="nocorepack"    set nocorepack=1&goto arg-ok
if /i "%1"=="ltcg"          set ltcg=1&goto arg-ok
if /i "%1"=="licensertf"    set licensertf=1&goto arg-ok
if /i "%1"=="test"          set test_args=%test_args% %common_test_suites%&set lint_cpp=1&set lint_js=1&set lint_md=1&goto arg-ok
if /i "%1"=="test-ci-native" set test_args=%test_args% %test_ci_args% -p tap --logfile test.tap %CI_NATIVE_SUITES% %CI_DOC%&set build_addons=1&set build_js_native_api_tests=1&set build_node_api_tests=1&set cctest_args=%cctest_args% --gtest_output=xml:cctest.junit.xml&goto arg-ok
if /i "%1"=="test-ci-js"    set test_args=%test_args% %test_ci_args% -p tap --logfile test.tap %CI_JS_SUITES%&set no_cctest=1&goto arg-ok
if /i "%1"=="build-addons"   set build_addons=1&goto arg-ok
if /i "%1"=="build-js-native-api-tests"   set build_js_native_api_tests=1&goto arg-ok
if /i "%1"=="build-node-api-tests"   set build_node_api_tests=1&goto arg-ok
if /i "%1"=="test-addons"   set test_args=%test_args% addons&set build_addons=1&goto arg-ok
if /i "%1"=="test-doc"      set test_args=%test_args% %CI_DOC%&set doc=1&&set lint_js=1&set lint_md=1&goto arg-ok
if /i "%1"=="test-js-native-api"   set test_args=%test_args% js-native-api&set build_js_native_api_tests=1&goto arg-ok
if /i "%1"=="test-node-api"   set test_args=%test_args% node-api&set build_node_api_tests=1&goto arg-ok
if /i "%1"=="test-tick-processor" set test_args=%test_args% tick-processor&goto arg-ok
if /i "%1"=="test-internet" set test_args=%test_args% internet&goto arg-ok
if /i "%1"=="test-known-issues" set test_args=%test_args% known_issues&goto arg-ok
if /i "%1"=="test-all"      set test_args=%test_args% gc internet pummel %common_test_suites%&set lint_cpp=1&set lint_js=1&goto arg-ok
if /i "%1"=="test-node-inspect" set test_node_inspect=1&goto arg-ok
if /i "%1"=="test-check-deopts" set test_check_deopts=1&goto arg-ok
if /i "%1"=="test-npm"      set test_npm=1&goto arg-ok
if /i "%1"=="test-v8"       set test_v8=1&set custom_v8_test=1&goto arg-ok
if /i "%1"=="test-v8-intl"  set test_v8_intl=1&set custom_v8_test=1&goto arg-ok
if /i "%1"=="test-v8-benchmarks" set test_v8_benchmarks=1&set custom_v8_test=1&goto arg-ok
if /i "%1"=="test-v8-all"       set test_v8=1&set test_v8_intl=1&set test_v8_benchmarks=1&set custom_v8_test=1&goto arg-ok
if /i "%1"=="lint-cpp"      set lint_cpp=1&goto arg-ok
if /i "%1"=="lint-js"       set lint_js=1&goto arg-ok
if /i "%1"=="lint-js-build" set lint_js_build=1&goto arg-ok
if /i "%1"=="lint-js-fix"   set lint_js_fix=1&goto arg-ok
if /i "%1"=="jslint"        set lint_js=1&echo Please use lint-js instead of jslint&goto arg-ok
if /i "%1"=="lint-md"       set lint_md=1&goto arg-ok
if /i "%1"=="lint"          set lint_cpp=1&set lint_js=1&set lint_md=1&goto arg-ok
if /i "%1"=="lint-ci"       set lint_cpp=1&set lint_js_ci=1&goto arg-ok
if /i "%1"=="format-md"     set format_md=1&goto arg-ok
if /i "%1"=="package"       set package=1&goto arg-ok
if /i "%1"=="msi"           set msi=1&set licensertf=1&set download_arg="--download=all"&set i18n_arg=full-icu&goto arg-ok
if /i "%1"=="build-release" set build_release=1&set sign=1&goto arg-ok
if /i "%1"=="upload"        set upload=1&goto arg-ok
if /i "%1"=="small-icu"     set i18n_arg=%1&goto arg-ok
if /i "%1"=="full-icu"      set i18n_arg=%1&goto arg-ok
if /i "%1"=="intl-none"     set i18n_arg=none&goto arg-ok
if /i "%1"=="without-intl"  set i18n_arg=none&goto arg-ok
if /i "%1"=="download-all"  set download_arg="--download=all"&goto arg-ok
if /i "%1"=="ignore-flaky"  set test_args=%test_args% --flaky-tests=dontcare&goto arg-ok
if /i "%1"=="dll"           set dll=1&goto arg-ok
if /i "%1"=="enable-vtune" set enable_vtune_arg=1&goto arg-ok
if /i "%1"=="static"           set enable_static=1&goto arg-ok
if /i "%1"=="no-NODE-OPTIONS"	set no_NODE_OPTIONS=1&goto arg-ok
if /i "%1"=="debug-nghttp2" set debug_nghttp2=1&goto arg-ok
if /i "%1"=="link-module"   set "link_module= --link-module=%2%link_module%"&goto arg-ok-2
if /i "%1"=="no-cctest"     set no_cctest=1&goto arg-ok
if /i "%1"=="cctest"        set cctest=1&goto arg-ok
if /i "%1"=="openssl-no-asm"   set openssl_no_asm=1&goto arg-ok
if /i "%1"=="no-shared-roheap" set no_shared_roheap=1&goto arg-ok
if /i "%1"=="doc"           set doc=1&goto arg-ok
if /i "%1"=="binlog"        set extra_msbuild_args=/binaryLogger:out\%config%\node.binlog&goto arg-ok
if /i "%1"=="compile-commands" set compile_commands=1&goto arg-ok

echo Error: invalid command line option `%1`.
exit /b 1

:arg-ok-2
shift
:arg-ok
shift
goto next-arg

:args-done

if defined build_release (
  set config=Release
  set package=1
  set msi=1
  set licensertf=1
  set download_arg="--download=all"
  set i18n_arg=full-icu
  set projgen=1
  set cctest=1
  set ltcg=1
)

if defined msi     set stage_package=1
if defined package set stage_package=1

:: assign path to node_exe
set "node_exe=%config%\node.exe"
set "node_gyp_exe="%node_exe%" deps\npm\node_modules\node-gyp\bin\node-gyp"
set "npm_exe="%~dp0%node_exe%" %~dp0deps\npm\bin\npm-cli.js"
if "%target_env%"=="vs2022" set "node_gyp_exe=%node_gyp_exe% --msvs_version=2022"

:: skip building if the only argument received was lint
if "%*"=="lint" if exist "%node_exe%" goto lint-cpp

:: skip building if the only argument received was format-md
if "%*"=="format-md" if exist "%node_exe%" goto format-md

if "%config%"=="Debug"      set configure_flags=%configure_flags% --debug
if defined nosnapshot       set configure_flags=%configure_flags% --without-snapshot
if defined nonpm            set configure_flags=%configure_flags% --without-npm
if defined nocorepack       set configure_flags=%configure_flags% --without-corepack
if defined ltcg             set configure_flags=%configure_flags% --with-ltcg
if defined release_urlbase  set configure_flags=%configure_flags% --release-urlbase=%release_urlbase%
if defined download_arg     set configure_flags=%configure_flags% %download_arg%
if defined enable_vtune_arg set configure_flags=%configure_flags% --enable-vtune-profiling
if defined dll              set configure_flags=%configure_flags% --shared
if defined enable_static    set configure_flags=%configure_flags% --enable-static
if defined no_NODE_OPTIONS  set configure_flags=%configure_flags% --without-node-options
if defined link_module      set configure_flags=%configure_flags% %link_module%
if defined i18n_arg         set configure_flags=%configure_flags% --with-intl=%i18n_arg%
if defined config_flags     set configure_flags=%configure_flags% %config_flags%
if defined target_arch      set configure_flags=%configure_flags% --dest-cpu=%target_arch%
if defined debug_nghttp2    set configure_flags=%configure_flags% --debug-nghttp2
if defined openssl_no_asm   set configure_flags=%configure_flags% --openssl-no-asm
if defined no_shared_roheap set configure_flags=%configure_flags% --disable-shared-readonly-heap
if defined DEBUG_HELPER     set configure_flags=%configure_flags% --verbose
if defined compile_commands set configure_flags=%configure_flags% -C

if "%target_arch%"=="x86" (
  echo "32-bit Windows builds are not supported anymore."
  exit /b 1
)

if not exist "%~dp0deps\icu" goto no-depsicu
if "%target%"=="Clean" echo deleting %~dp0deps\icu
if "%target%"=="Clean" rmdir /S /Q %~dp0deps\icu
:no-depsicu

if "%target%"=="TestClean" (
  echo deleting test/.tmp*
  if exist "test\.tmp*" for /f %%i in ('dir /a:d /s /b test\.tmp*') do rmdir /S /Q "%%i"
  goto exit
)


call tools\msvs\find_python.cmd
if errorlevel 1 goto :exit

REM NASM is only needed on x86_64.
if not defined openssl_no_asm if "%target_arch%" NEQ "arm64" call tools\msvs\find_nasm.cmd
if errorlevel 1 echo Could not find NASM, install it or build with openssl-no-asm. See BUILDING.md.

call :getnodeversion || exit /b 1

if defined TAG set configure_flags=%configure_flags% --tag=%TAG%

if not "%target%"=="Clean" goto skip-clean
rmdir /Q /S "%~dp0%config%\%TARGET_NAME%" > nul 2> nul
:skip-clean

if defined noprojgen if defined nobuild goto :after-build

@rem Set environment for msbuild

set msvs_host_arch=amd64
if _%PROCESSOR_ARCHITECTURE%_==_ARM64_ set msvs_host_arch=arm64
@rem usually vcvarsall takes an argument: host + '_' + target
set vcvarsall_arg=%msvs_host_arch%_%target_arch%
@rem unless both the host and the target are the same
if %target_arch%==x64 if %msvs_host_arch%==amd64 set vcvarsall_arg=amd64
if %target_arch%==%msvs_host_arch% set vcvarsall_arg=%target_arch%

@rem Look for Visual Studio 2022
:vs-set-2022
if defined target_env if "%target_env%" NEQ "vs2022" goto msbuild-not-found
echo Looking for Visual Studio 2022
@rem VCINSTALLDIR may be set if run from a VS Command Prompt and needs to be
@rem cleared first as vswhere_usability_wrapper.cmd doesn't when it fails to
@rem detect the version searched for
if not defined target_env set "VCINSTALLDIR="
call tools\msvs\vswhere_usability_wrapper.cmd "[17.6,18.0)" %target_arch% "prerelease" %clang_cl%
if "_%VCINSTALLDIR%_" == "__" goto msbuild-not-found
@rem check if VS2022 is already setup, and for the requested arch
if "_%VisualStudioVersion%_" == "_17.0_" if "_%VSCMD_ARG_TGT_ARCH%_"=="_%target_arch%_" goto found_vs2022
@rem need to clear VSINSTALLDIR for vcvarsall to work as expected
set "VSINSTALLDIR="
@rem prevent VsDevCmd.bat from changing the current working directory
set "VSCMD_START_DIR=%CD%"
set vcvars_call="%VCINSTALLDIR%\Auxiliary\Build\vcvarsall.bat" %vcvarsall_arg%
echo calling: %vcvars_call%
call %vcvars_call%
if errorlevel 1 goto msbuild-not-found
if defined DEBUG_HELPER @ECHO ON
:found_vs2022
echo Found MSVS version %VisualStudioVersion%
set GYP_MSVS_VERSION=2022
set PLATFORM_TOOLSET=v143
goto msbuild-found

:msbuild-not-found
set "clang_echo="
if defined clang_cl set "clang_echo= or Clang compiler/LLVM toolset"
echo Failed to find a suitable Visual Studio installation%clang_echo%.
echo Try to run in a "Developer Command Prompt" or consult
echo https://github.com/nodejs/node/blob/HEAD/BUILDING.md#windows
goto exit

:msbuild-found

@rem Visual Studio v17.10 has a bug that causes the build to fail.
@rem Check if the version is v17.10 and exit if it is.
echo %VSCMD_VER% | findstr /b /c:"17.10" >nul
if %errorlevel% neq 1  (
  echo Node.js doesn't compile with Visual Studio 17.10 Please use a different version.
  goto exit
)

@rem check if the clang-cl build is requested
if not defined clang_cl goto clang-skip
@rem x64 is hard coded as it is used for both cross and native compilation.
set "clang_path=%VCINSTALLDIR%\Tools\Llvm\x64\bin\clang.exe"
for /F "tokens=3" %%i in ('"%clang_path%" --version') do (
    set clang_version=%%i
    goto clang-found
)

:clang-not-found
echo Failed to find Clang compiler in %clang_path%.
goto exit

:clang-found
echo Found Clang version %clang_version%
set configure_flags=%configure_flags% --clang-cl=%clang_version%

:clang-skip

set project_generated=
:project-gen
@rem Skip project generation if requested.
if defined noprojgen goto msbuild
if defined projgen goto run-configure
if not exist node.sln goto run-configure
if not exist .gyp_configure_stamp goto run-configure
echo %configure_flags% > .tmp_gyp_configure_stamp
where /R . /T *.gyp* >> .tmp_gyp_configure_stamp
fc .gyp_configure_stamp .tmp_gyp_configure_stamp >NUL 2>&1
if errorlevel 1 goto run-configure

:skip-configure
del .tmp_gyp_configure_stamp 2> NUL
echo Reusing solution generated with %configure_flags%
goto msbuild

:run-configure
del .tmp_gyp_configure_stamp 2> NUL
del .gyp_configure_stamp 2> NUL
@rem Generate the VS project.
echo configure %configure_flags%
echo %configure_flags%> .used_configure_flags
python configure %configure_flags%
if errorlevel 1 goto create-msvs-files-failed
if not exist node.sln goto create-msvs-files-failed
set project_generated=1
echo Project files generated.
echo %configure_flags% > .gyp_configure_stamp
where /R . /T *.gyp* >> .gyp_configure_stamp

:msbuild
@rem Skip build if requested.
if defined nobuild goto :after-build

@rem Build the sln with msbuild.
set "msbcpu=/m:2"
if "%NUMBER_OF_PROCESSORS%"=="1" set "msbcpu=/m:1"
set "msbplatform=x64"
if "%target_arch%"=="arm64" set "msbplatform=ARM64"
if "%target%"=="Build" (
  if defined no_cctest set target=node
  if "%test_args%"=="" set target=node
  if defined cctest set target="Build"
)
if "%target%"=="node" if exist "%config%\cctest.exe" del "%config%\cctest.exe"
if "%target%"=="node" if exist "%config%\embedtest.exe" del "%config%\embedtest.exe"
if defined msbuild_args set "extra_msbuild_args=%extra_msbuild_args% %msbuild_args%"
@rem Setup env variables to use multiprocessor build
set UseMultiToolTask=True
set EnforceProcessCountAcrossBuilds=True
set MultiProcMaxCount=%NUMBER_OF_PROCESSORS%
msbuild node.sln %msbcpu% /t:%target% /p:Configuration=%config% /p:Platform=%msbplatform% /clp:NoItemAndPropertyList;Verbosity=minimal /nologo %extra_msbuild_args%
if errorlevel 1 (
  if not defined project_generated echo Building Node with reused solution failed. To regenerate project files use "vcbuild projgen"
  exit /B 1
)
if "%target%" == "Clean" goto exit

:after-build
:: Check existence of %config% before removing it.
if exist %config% rd %config%
if errorlevel 1 echo "Old build output exists at 'out\%config%'. Please remove." & exit /B
:: Use /J because /D (symlink) requires special permissions.
if EXIST out\%config% mklink /J %config% out\%config%
if errorlevel 1 echo "Could not create junction to 'out\%config%'." & exit /B

:sign
@rem Skip signing unless the `sign` option was specified.
if not defined sign goto licensertf

call tools\sign.bat Release\node.exe
if errorlevel 1 echo Failed to sign exe, got error code %errorlevel%&goto exit

:licensertf
@rem Skip license.rtf generation if not requested.
if not defined licensertf goto stage_package

set "use_x64_node_exe=false"
if "%target_arch%"=="arm64" if "%PROCESSOR_ARCHITECTURE%"=="AMD64" set "use_x64_node_exe=true"
set "x64_node_exe=temp-vcbuild\node-x64-cross-compiling.exe"
if "%use_x64_node_exe%"=="true" (
  echo Cross-compilation to ARM64 detected. We'll use the x64 Node executable for license2rtf.
  if not exist "%x64_node_exe%" (
    echo Downloading x64 node.exe...
    if not exist "temp-vcbuild" mkdir temp-vcbuild
    powershell -c "Invoke-WebRequest -Uri 'https://nodejs.org/dist/latest/win-x64/node.exe' -OutFile '%x64_node_exe%'"
  )
  if not exist "%x64_node_exe%" (
    echo Could not find the Node executable at the given x64_node_exe path. Aborting.
    set exit_code=1
    goto exit
  )
  %x64_node_exe% tools\license2rtf.mjs < LICENSE > %config%\license.rtf
) else (
  %node_exe% tools\license2rtf.mjs < LICENSE > %config%\license.rtf
)

if errorlevel 1 echo Failed to generate license.rtf, got error code %errorlevel%&goto exit

:stage_package
if not defined stage_package goto install-doctools

echo Creating package...
cd Release
rmdir /S /Q %TARGET_NAME% > nul 2> nul
mkdir %TARGET_NAME% > nul 2> nul
mkdir %TARGET_NAME%\node_modules > nul 2>nul

copy /Y node.exe %TARGET_NAME%\ > nul
if errorlevel 1 echo Cannot copy node.exe && goto package_error
copy /Y ..\LICENSE %TARGET_NAME%\ > nul
if errorlevel 1 echo Cannot copy LICENSE && goto package_error
copy /Y ..\README.md %TARGET_NAME%\ > nul
if errorlevel 1 echo Cannot copy README.md && goto package_error
copy /Y ..\CHANGELOG.md %TARGET_NAME%\ > nul
if errorlevel 1 echo Cannot copy CHANGELOG.md && goto package_error

if not defined nonpm (
  robocopy ..\deps\npm %TARGET_NAME%\node_modules\npm /e /xd test > nul
  if errorlevel 8 echo Cannot copy npm package && goto package_error
  copy /Y ..\deps\npm\bin\npm %TARGET_NAME%\ > nul
  if errorlevel 1 echo Cannot copy npm && goto package_error
  copy /Y ..\deps\npm\bin\npm.cmd %TARGET_NAME%\ > nul
  if errorlevel 1 echo Cannot copy npm.cmd && goto package_error
  copy /Y ..\deps\npm\bin\npx %TARGET_NAME%\ > nul
  if errorlevel 1 echo Cannot copy npx && goto package_error
  copy /Y ..\deps\npm\bin\npx.cmd %TARGET_NAME%\ > nul
  if errorlevel 1 echo Cannot copy npx.cmd && goto package_error
  copy /Y ..\deps\npm\bin\npm.ps1 %TARGET_NAME%\ > nul
  if errorlevel 1 echo Cannot copy npm.ps1 && goto package_error
  copy /Y ..\deps\npm\bin\npx.ps1 %TARGET_NAME%\ > nul
  if errorlevel 1 echo Cannot copy npx.ps1 && goto package_error
)

if not defined nocorepack (
  robocopy ..\deps\corepack %TARGET_NAME%\node_modules\corepack /e /xd test > nul
  if errorlevel 8 echo Cannot copy corepack package && goto package_error
  copy /Y ..\deps\corepack\shims\nodewin\corepack %TARGET_NAME%\ > nul
  if errorlevel 1 echo Cannot copy corepack && goto package_error
  copy /Y ..\deps\corepack\shims\nodewin\corepack.cmd %TARGET_NAME%\ > nul
  if errorlevel 1 echo Cannot copy corepack.cmd && goto package_error
)

copy /Y ..\tools\msvs\nodevars.bat %TARGET_NAME%\ > nul
if errorlevel 1 echo Cannot copy nodevars.bat && goto package_error
copy /Y ..\tools\msvs\install_tools\*.* %TARGET_NAME%\ > nul
if errorlevel 1 echo Cannot copy install_tools scripts && goto package_error
if defined dll (
  copy /Y libnode.dll %TARGET_NAME%\ > nul
  if errorlevel 1 echo Cannot copy libnode.dll && goto package_error

  copy /Y libnode.lib %TARGET_NAME%\ > nul
  if errorlevel 1 echo Cannot copy libnode.lib && goto package_error

  mkdir %TARGET_NAME%\Release > nul
  copy /Y node.def %TARGET_NAME%\Release\ > nul
  if errorlevel 1 echo Cannot copy node.def && goto package_error

  python ..\tools\install.py install --root-dir .. --config-gypi-path %CD%\..\config.gypi --dest-dir %CD%\%TARGET_NAME% --prefix \ --headers-only
  if errorlevel 1 echo Cannot install headers && goto package_error

  if exist ..\Debug (
    mkdir %TARGET_NAME%\Debug > nul

    copy /Y ..\Debug\libnode.dll %TARGET_NAME%\Debug\ > nul
    if errorlevel 1 echo Cannot copy libnode.dll && goto package_error

    copy /Y ..\Debug\libnode.lib %TARGET_NAME%\Debug\ > nul
    if errorlevel 1 echo Cannot copy libnode.lib && goto package_error

    copy /Y ..\Debug\node.def %TARGET_NAME%\Debug\ > nul
    if errorlevel 1 echo Cannot copy node.def && goto package_error
  )
)
cd ..

:package
if not defined package goto msi
cd Release
echo Creating %TARGET_NAME%.7z
del %TARGET_NAME%.7z > nul 2> nul
7z a -r -mx9 -t7z %TARGET_NAME%.7z %TARGET_NAME% > nul
if errorlevel 1 echo Cannot create %TARGET_NAME%.7z && goto package_error

echo Creating %TARGET_NAME%.zip
del %TARGET_NAME%.zip > nul 2> nul
7z a -r -mx9 -tzip %TARGET_NAME%.zip %TARGET_NAME% > nul
if errorlevel 1 echo Cannot create %TARGET_NAME%.zip && goto package_error

echo Creating node_pdb.7z
del node_pdb.7z > nul 2> nul
7z a -mx9 -t7z node_pdb.7z node.pdb > nul

echo Creating node_pdb.zip
del node_pdb.zip  > nul 2> nul
7z a -mx9 -tzip node_pdb.zip node.pdb > nul

cd ..
echo Package created!
goto package_done
:package_error
cd ..
exit /b 1
:package_done

:msi
@rem Skip msi generation if not requested
if not defined msi goto install-doctools

:msibuild
echo Building node-v%FULLVERSION%-%target_arch%.msi
set "msbsdk="
if defined WindowsSDKVersion set "msbsdk=/p:WindowsTargetPlatformVersion=%WindowsSDKVersion:~0,-1%"
msbuild "%~dp0tools\msvs\msi\nodemsi.sln" /m /t:Restore,Clean,Build %msbsdk% /p:PlatformToolset=%PLATFORM_TOOLSET% /p:Configuration=%config% /p:Platform=%target_arch% /p:NodeVersion=%NODE_VERSION% /p:FullVersion=%FULLVERSION% /p:DistTypeDir=%DISTTYPEDIR% /clp:NoSummary;NoItemAndPropertyList;Verbosity=minimal /nologo
if errorlevel 1 goto exit

if not defined sign goto upload
call tools\sign.bat node-v%FULLVERSION%-%target_arch%.msi
if errorlevel 1 echo Failed to sign msi, got error code %errorlevel%&goto exit

:upload
@rem Skip upload if not requested
if not defined upload goto install-doctools

if not defined SSHCONFIG (
  echo SSHCONFIG is not set for upload
  exit /b 1
)

if not defined STAGINGSERVER set STAGINGSERVER=node-www
if not defined CLOUDFLARE_BUCKET set CLOUDFLARE_BUCKET=r2:dist-staging
ssh -F %SSHCONFIG% %STAGINGSERVER% "mkdir -p nodejs/%DISTTYPEDIR%/v%FULLVERSION%/win-%target_arch%"
if errorlevel 1 goto exit
scp -F %SSHCONFIG% Release\node.exe %STAGINGSERVER%:nodejs/%DISTTYPEDIR%/v%FULLVERSION%/win-%target_arch%/node.exe
if errorlevel 1 goto exit
ssh -F %SSHCONFIG% %STAGINGSERVER% "rclone copyto nodejs/%DISTTYPEDIR%/v%FULLVERSION%/win-%target_arch%/node.exe %CLOUDFLARE_BUCKET%/nodejs/%DISTTYPEDIR%/v%FULLVERSION%/win-%target_arch%/node.exe"
if errorlevel 1 goto exit
scp -F %SSHCONFIG% Release\node.lib %STAGINGSERVER%:nodejs/%DISTTYPEDIR%/v%FULLVERSION%/win-%target_arch%/node.lib
if errorlevel 1 goto exit
ssh -F %SSHCONFIG% %STAGINGSERVER% "rclone copyto nodejs/%DISTTYPEDIR%/v%FULLVERSION%/win-%target_arch%/node.lib %CLOUDFLARE_BUCKET%/nodejs/%DISTTYPEDIR%/v%FULLVERSION%/win-%target_arch%/node.lib"
if errorlevel 1 goto exit
scp -F %SSHCONFIG% Release\node_pdb.zip %STAGINGSERVER%:nodejs/%DISTTYPEDIR%/v%FULLVERSION%/win-%target_arch%/node_pdb.zip
if errorlevel 1 goto exit
ssh -F %SSHCONFIG% %STAGINGSERVER% "rclone copyto nodejs/%DISTTYPEDIR%/v%FULLVERSION%/win-%target_arch%/node_pdb.zip %CLOUDFLARE_BUCKET%/nodejs/%DISTTYPEDIR%/v%FULLVERSION%/win-%target_arch%/node_pdb.zip"
if errorlevel 1 goto exit
scp -F %SSHCONFIG% Release\node_pdb.7z %STAGINGSERVER%:nodejs/%DISTTYPEDIR%/v%FULLVERSION%/win-%target_arch%/node_pdb.7z
if errorlevel 1 goto exit
ssh -F %SSHCONFIG% %STAGINGSERVER% "rclone copyto nodejs/%DISTTYPEDIR%/v%FULLVERSION%/win-%target_arch%/node_pdb.7z %CLOUDFLARE_BUCKET%/nodejs/%DISTTYPEDIR%/v%FULLVERSION%/win-%target_arch%/node_pdb.7z"
if errorlevel 1 goto exit
scp -F %SSHCONFIG% Release\%TARGET_NAME%.7z %STAGINGSERVER%:nodejs/%DISTTYPEDIR%/v%FULLVERSION%/%TARGET_NAME%.7z
if errorlevel 1 goto exit
ssh -F %SSHCONFIG% %STAGINGSERVER% "rclone copyto nodejs/%DISTTYPEDIR%/v%FULLVERSION%/%TARGET_NAME%.7z %CLOUDFLARE_BUCKET%/nodejs/%DISTTYPEDIR%/v%FULLVERSION%/%TARGET_NAME%.7z"
if errorlevel 1 goto exit
scp -F %SSHCONFIG% Release\%TARGET_NAME%.zip %STAGINGSERVER%:nodejs/%DISTTYPEDIR%/v%FULLVERSION%/%TARGET_NAME%.zip
if errorlevel 1 goto exit
ssh -F %SSHCONFIG% %STAGINGSERVER% "rclone copyto nodejs/%DISTTYPEDIR%/v%FULLVERSION%/%TARGET_NAME%.zip %CLOUDFLARE_BUCKET%/nodejs/%DISTTYPEDIR%/v%FULLVERSION%/%TARGET_NAME%.zip"
if errorlevel 1 goto exit
scp -F %SSHCONFIG% node-v%FULLVERSION%-%target_arch%.msi %STAGINGSERVER%:nodejs/%DISTTYPEDIR%/v%FULLVERSION%/
if errorlevel 1 goto exit
ssh -F %SSHCONFIG% %STAGINGSERVER% "rclone copyto nodejs/%DISTTYPEDIR%/v%FULLVERSION%/node-v%FULLVERSION%-%target_arch%.msi %CLOUDFLARE_BUCKET%/nodejs/%DISTTYPEDIR%/v%FULLVERSION%/node-v%FULLVERSION%-%target_arch%.msi"
if errorlevel 1 goto exit
ssh -F %SSHCONFIG% %STAGINGSERVER% "touch nodejs/%DISTTYPEDIR%/v%FULLVERSION%/node-v%FULLVERSION%-%target_arch%.msi.done nodejs/%DISTTYPEDIR%/v%FULLVERSION%/%TARGET_NAME%.zip.done nodejs/%DISTTYPEDIR%/v%FULLVERSION%/%TARGET_NAME%.7z.done nodejs/%DISTTYPEDIR%/v%FULLVERSION%/win-%target_arch%.done && chmod -R ug=rw-x+X,o=r+X nodejs/%DISTTYPEDIR%/v%FULLVERSION%/node-v%FULLVERSION%-%target_arch%.* nodejs/%DISTTYPEDIR%/v%FULLVERSION%/win-%target_arch%*"
if errorlevel 1 goto exit


:install-doctools
REM only install if building doc OR testing doctool OR building addons
if not defined doc if not defined build_addons (
  echo.%test_args% | findstr doctool 1>nul
  if errorlevel 1 goto :skip-install-doctools
)
if exist "tools\doc\node_modules\unified\package.json" goto skip-install-doctools
SETLOCAL
cd tools\doc
%npm_exe% ci
cd ..\..
if errorlevel 1 goto exit
ENDLOCAL
:skip-install-doctools
@rem Clear errorlevel from echo.%test_args% | findstr doctool 1>nul
cd .

:build-doc
@rem Build documentation if requested
if not defined doc goto run
if not exist %node_exe% (
  echo Failed to find node.exe
  goto run
)
mkdir %config%\doc
robocopy /e doc\api %config%\doc\api
robocopy /e doc\api_assets %config%\doc\api\assets

for %%F in (%config%\doc\api\*.md) do (
  %node_exe% tools\doc\generate.mjs --node-version=v%FULLVERSION% %%F --output-directory=%%~dF%%~pF
)

:run
@rem Run tests if requested.

if not defined build_addons goto build-js-native-api-tests
if not exist "%node_exe%" (
  echo Failed to find node.exe
  goto build-js-native-api-tests
)
echo Building addons
:: clear
for /d %%F in (test\addons\??_*) do (
  rd /s /q %%F
)
:: generate
"%node_exe%" tools\doc\addon-verify.mjs
if %errorlevel% neq 0 exit /b %errorlevel%
:: building addons
setlocal
python "%~dp0tools\build_addons.py" "%~dp0test\addons" --config %config%
if errorlevel 1 exit /b 1
endlocal

:build-js-native-api-tests
if not defined build_js_native_api_tests goto build-node-api-tests
if not exist "%node_exe%" (
  echo Failed to find node.exe
  goto build-node-api-tests
)
echo Building js-native-api
:: clear
for /d %%F in (test\js-native-api\??_*) do (
  rd /s /q %%F
)
:: building js-native-api
setlocal
python "%~dp0tools\build_addons.py" "%~dp0test\js-native-api" --config %config%
if errorlevel 1 exit /b 1
endlocal
goto build-node-api-tests

:build-node-api-tests
if not defined build_node_api_tests goto run-tests
if not exist "%node_exe%" (
  echo Failed to find node.exe
  goto run-tests
)
echo Building node-api
:: clear
for /d %%F in (test\node-api\??_*) do (
  rd /s /q %%F
)
:: building node-api
setlocal
python "%~dp0tools\build_addons.py" "%~dp0test\node-api" --config %config%
if errorlevel 1 exit /b 1
endlocal
goto run-tests

:run-tests
if defined test_check_deopts goto node-check-deopts
if defined test_node_inspect goto node-test-inspect
goto node-tests

:node-check-deopts
python tools\test.py --mode=release --check-deopts parallel sequential
if defined test_node_inspect goto node-test-inspect
goto node-tests

:node-test-inspect
set USE_EMBEDDED_NODE_INSPECT=1
%node_exe% tools\test-npm-package.js --install deps\node-inspect test
goto node-tests

:node-tests
if not defined test_npm goto no-test-npm
set npm_test_cmd="%node_exe%" tools\test-npm-package.js --install --logfile=test-npm.tap deps\npm test-node
echo %npm_test_cmd%
%npm_test_cmd%
if errorlevel 1 goto exit
:no-test-npm

if "%test_args%"=="" goto test-v8
if "%config%"=="Debug" set test_args=--mode=debug %test_args%
if "%config%"=="Release" set test_args=--mode=release %test_args%
if defined no_cctest echo Skipping cctest because no-cctest was specified && goto run-test-py
if not exist "%config%\cctest.exe" echo cctest.exe not found. Run "vcbuild test" or "vcbuild cctest" to build it. && goto run-test-py
echo running 'cctest %cctest_args%'
"%config%\cctest" %cctest_args%
if %errorlevel% neq 0 set exit_code=%errorlevel%
echo running '%node_exe% test\embedding\test-embedding.js'
"%node_exe%" test\embedding\test-embedding.js
if %errorlevel% neq 0 set exit_code=%errorlevel%
:run-test-py
echo running 'python tools\test.py %test_args%'
python tools\test.py %test_args%
if %errorlevel% neq 0 set exit_code=%errorlevel%
goto test-v8

:test-v8
if not defined custom_v8_test goto lint-cpp
call tools/test-v8.bat
if errorlevel 1 goto exit
goto lint-cpp

:lint-cpp
if not defined lint_cpp goto lint-js
if defined NODEJS_MAKE goto run-make-lint
where make > NUL 2>&1 && make -v | findstr /C:"GNU Make" 1> NUL
if "%ERRORLEVEL%"=="0" set "NODEJS_MAKE=make PYTHON=python" & goto run-make-lint
where wsl > NUL 2>&1
if "%ERRORLEVEL%"=="0" set "NODEJS_MAKE=wsl make" & goto run-make-lint
echo Could not find GNU Make, needed for linting C/C++
echo Alternatively, you can use WSL
goto lint-js

:run-make-lint
%NODEJS_MAKE% lint-cpp
goto lint-js-build

:lint-js-build
if not defined lint_js_build if not defined lint_js if not defined lint_js_fix goto lint-md-build
cd tools\eslint
%npm_exe% ci
cd ..\..

:lint-js
if not defined lint_js goto lint-js-fix
if not exist tools\eslint\node_modules\eslint goto no-lint
echo running lint-js
%node_exe% tools\eslint\node_modules\eslint\bin\eslint.js --cache --max-warnings=0 --report-unused-disable-directives --rule "@stylistic/js/linebreak-style: 0" eslint.config.mjs benchmark doc lib test tools
goto lint-js-fix

:lint-js-fix
if not defined lint_js_fix goto lint-md
if not exist tools\eslint\node_modules\eslint goto no-lint
echo running lint-js-fix
%node_exe% tools\eslint\node_modules\eslint\bin\eslint.js --cache --max-warnings=0 --report-unused-disable-directives --rule "@stylistic/js/linebreak-style: 0" eslint.config.mjs benchmark doc lib test tools --fix
goto lint-md-build

:lint-md-build
if not defined lint_md if not defined format_md goto lint-md
cd tools\lint-md
%npm_exe% ci
cd ..\..

:lint-md
if not defined lint_md goto format-md
if not exist tools\lint-md\node_modules goto no-lint
echo Running Markdown linter on docs...
SETLOCAL ENABLEDELAYEDEXPANSION
set lint_md_files=
for /D %%D IN (doc\*) do (
  for %%F IN (%%D\*.md) do (
    set "lint_md_files="%%F" !lint_md_files!"
  )
)
%node_exe% tools\lint-md\lint-md.mjs %lint_md_files%
ENDLOCAL
goto format-md

:format-md
if not defined format_md goto exit
if not exist tools\lint-md\node_modules goto no-lint
echo Running Markdown formatter on docs...
SETLOCAL ENABLEDELAYEDEXPANSION
set lint_md_files=
for /D %%D IN (doc\*) do (
  for %%F IN (%%D\*.md) do (
    set "lint_md_files="%%F" !lint_md_files!"
  )
)
%node_exe% tools\lint-md\lint-md.mjs --format %lint_md_files%
ENDLOCAL

:no-lint
echo Linting is not available through the source tarball.
echo Use the git repo instead: $ git clone https://github.com/nodejs/node.git
goto exit

:create-msvs-files-failed
echo Failed to create vc project files.
del .used_configure_flags
set exit_code=1
goto exit

:help
echo vcbuild.bat [debug/release] [msi] [doc] [test/test-all/test-addons/test-doc/test-js-native-api/test-node-api/test-internet/test-tick-processor/test-known-issues/test-node-inspect/test-check-deopts/test-npm/test-v8/test-v8-intl/test-v8-benchmarks/test-v8-all] [ignore-flaky] [static/dll] [noprojgen] [projgen] [clang-cl] [small-icu/full-icu/without-intl] [nobuild] [nosnapshot] [nonpm] [nocorepack] [ltcg] [licensetf] [sign] [x64/arm64] [vs2022] [download-all] [enable-vtune] [lint/lint-ci/lint-js/lint-md] [lint-md-build] [format-md] [package] [build-release] [upload] [no-NODE-OPTIONS] [link-module path-to-module] [debug-http2] [debug-nghttp2] [clean] [cctest] [no-cctest] [openssl-no-asm]
echo Examples:
echo   vcbuild.bat                          : builds release build
echo   vcbuild.bat debug                    : builds debug build
echo   vcbuild.bat release msi              : builds release build and MSI installer package
echo   vcbuild.bat test                     : builds debug build and runs tests
echo   vcbuild.bat build-release            : builds the release distribution as used by nodejs.org
echo   vcbuild.bat enable-vtune             : builds Node.js with Intel VTune profiling support to profile JavaScript
echo   vcbuild.bat link-module my_module.js : bundles my_module as built-in module
echo   vcbuild.bat lint                     : runs the C++, documentation and JavaScript linter
echo   vcbuild.bat no-cctest                : skip building cctest.exe
goto exit

:exit
if %errorlevel% neq 0 exit /b %errorlevel%
exit /b %exit_code%


rem ***************
rem   Subroutines
rem ***************

:getnodeversion
set NODE_VERSION=
set TAG=
set FULLVERSION=

for /F "usebackq tokens=*" %%i in (`python "%~dp0tools\getnodeversion.py"`) do set NODE_VERSION=%%i
if not defined NODE_VERSION (
  echo Cannot determine current version of Node.js
  exit /b 1
)

if not defined DISTTYPE set DISTTYPE=release
if "%DISTTYPE%"=="release" (
  set FULLVERSION=%NODE_VERSION%
  goto distexit
)
if "%DISTTYPE%"=="custom" (
  if not defined CUSTOMTAG (
    echo "CUSTOMTAG is not set for DISTTYPE=custom"
    exit /b 1
  )
  set TAG=%CUSTOMTAG%
)
if not "%DISTTYPE%"=="custom" (
  if not defined DATESTRING (
    echo "DATESTRING is not set for nightly"
    exit /b 1
  )
  if not defined COMMIT (
    echo "COMMIT is not set for nightly"
    exit /b 1
  )
  if not "%DISTTYPE%"=="nightly" (
    if not "%DISTTYPE%"=="next-nightly" (
      echo "DISTTYPE is not release, custom, nightly or next-nightly"
      exit /b 1
    )
  )
  set TAG=%DISTTYPE%%DATESTRING%%COMMIT%
)
set FULLVERSION=%NODE_VERSION%-%TAG%

:distexit
if not defined DISTTYPEDIR set DISTTYPEDIR=%DISTTYPE%
set TARGET_NAME=node-v%FULLVERSION%-win-%target_arch%
goto :EOF

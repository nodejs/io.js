:: Copyright 2017 - Refael Ackermann
:: Distributed under MIT style license
:: See accompanying file LICENSE at https://github.com/node4good/windows-autoconf
:: version: 2.0.0

@if not defined DEBUG_HELPER @ECHO OFF
setlocal
if "%~3"=="prerelease" set VSWHERE_WITH_PRERELEASE=1
set "InstallerPath=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%InstallerPath%" set "InstallerPath=%ProgramFiles%\Microsoft Visual Studio\Installer"
if not exist "%InstallerPath%" goto :no-vswhere
:: Manipulate %Path% for easier " handling
set "Path=%Path%;%InstallerPath%"
where vswhere 2> nul > nul
if errorlevel 1 goto :no-vswhere
:: VC.Tools are needed even when using clang because of the vcvarsall.bat usage
if "%2"=="arm64" (
    set VSWHERE_REQ=-requires Microsoft.VisualStudio.Component.VC.Tools.ARM64    
) else (
    set VSWHERE_REQ=-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
)
if "%4"=="1" (
    set VSWHERE_REQ=%VSWHERE_REQ% -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang
    set VSWHERE_REQ=%VSWHERE_REQ% -requires Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset
)
set VSWHERE_PRP=-property installationPath
set VSWHERE_LMT=-version %1
vswhere -prerelease > nul
if not errorlevel 1 if "%VSWHERE_WITH_PRERELEASE%"=="1" set "VSWHERE_LMT=%VSWHERE_LMT% -prerelease"
SET VSWHERE_ARGS=-latest -products * %VSWHERE_REQ% %VSWHERE_PRP% %VSWHERE_LMT%
for /f "usebackq tokens=*" %%i in (`vswhere %VSWHERE_ARGS%`) do (
    endlocal
    set "VCINSTALLDIR=%%i\VC\"
    set "VS150COMNTOOLS=%%i\Common7\Tools\"
    exit /B 0
)

:no-vswhere
endlocal
exit /B 1

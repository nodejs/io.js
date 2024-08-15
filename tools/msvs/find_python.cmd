@IF NOT DEFINED DEBUG_HELPER @ECHO OFF

echo Looking for Python
setlocal enabledelayedexpansion

:: Use python.exe if in %PATH%
set need_path=0
for /f "delims=" %%a in ('where python.exe 2^> nul') do (
  set p=%%~dpa
  goto :found-python
)

:: Query the 3 locations mentioned in PEP 514 for a Python InstallPath
set need_path=1
for %%k in ( "HKCU\Software", "HKLM\SOFTWARE", "HKLM\Software\Wow6432Node") do (
  call :find-versions %%k
  if not errorlevel 1 goto :found-python
)

:: Check for pyenv-win installation using pyenv which python
for /f "tokens=*" %%i in ('pyenv which python 2^>nul') do set p=%%i
if defined p (
    for /f "tokens=*" %%j in ('"%p%" --version') do set python_version=%%j
    goto :found-python
)

goto :no-python


:: Find Python installations in a registry location
:find-versions
for /f "delims=" %%a in ('reg query "%~1\Python\PythonCore" /f * /k 2^> nul ^| findstr /r ^^HK') do (
  call :read-installpath %%a
  if not errorlevel 1 exit /b 0
)
exit /b 1

:: Read the InstallPath of a given Environment Key to %p%
:: https://www.python.org/dev/peps/pep-0514/#installpath
:read-installpath
:: %%a will receive everything before ), might have spaces depending on language
:: %%b will receive *, corresponding to everything after )
:: %%c will receive REG_SZ
:: %%d will receive the path, including spaces
for /f "skip=2 tokens=1* delims=)" %%a in ('reg query "%1\InstallPath" /ve /t REG_SZ 2^> nul') do (
  for /f "tokens=1*" %%c in ("%%b") do (
    if not "%%c"=="REG_SZ" exit /b 1
    set "p=%%d"
    exit /b 0
  )
)
exit /b 1

:found-python
echo Python found in %p%\python.exe
call :check-python "%p%\python.exe"
if errorlevel 1 goto :no-python
endlocal ^
  & set "pt=%p%" ^
  & set "need_path_ext=%need_path%"
if %need_path_ext%==1 set "PATH=%pt%;%PATH%"
set "pt="
set "need_path_ext="
exit /b 0

:check-python
%1 -V
:: 9009 means error file not found
if %errorlevel% equ 9009 (
  echo Not an executable Python program
  exit /b 1
)
exit /b 0

:no-python
echo Could not find Python.
exit /b 1

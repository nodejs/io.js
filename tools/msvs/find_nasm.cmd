@IF NOT DEFINED DEBUG_HELPER @ECHO OFF

ECHO Looking for NASM

FOR /B "delims=" %%a IN ('where nasm 2^> NUL') DO (
  ECHO NASM found in %%a
  EXIT /B 0
)

FOR %%a IN ("%ProgramFiles%\NASM\nasm.exe" "%ProgramFiles(x86)%\NASM\nasm.exe" "%LOCALAPPDATA%\bin\NASM\nasm.exe") DO (
  IF EXIST %%a (
    CALL :find-nasm %%a
    EXIT /B 0
  )
)
EXIT /B 1

:find-nasm
SET p=%~1
:: Remove the last nine characters, which are "\nasm.exe"
SET p=%p:~0,-9%
SET "Path=%Path%;%p%"
SET p=
ECHO NASM found in %~1
EXIT /B 0

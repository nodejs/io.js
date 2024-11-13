@echo off

setlocal
title Install Additional Tools for Node.js

cls

echo ====================================================
echo Tools for Node.js Native Modules Installation Script
echo ====================================================
echo.
echo This script will install Python and the Visual Studio Build Tools, necessary
echo to compile Node.js native modules. Note that Chocolatey and required Windows
echo updates will also be installed.
echo.
echo This will require about 3 GiB of free disk space, plus any space necessary to
echo install Windows updates. This will take a while to run.
echo.
echo Please close all open programs for the duration of the installation. If the
echo installation fails, please ensure Windows is fully updated, reboot your
echo computer and try to run this again. This script can be found in the
echo Start menu under Node.js.
echo.
echo You can close this window to stop now. Detailed instructions to install these
echo tools manually are available at https://github.com/nodejs/node-gyp#on-windows
echo.
pause

cls

REM Adapted from https://github.com/Microsoft/windows-dev-box-setup-scripts/blob/79bbe5bdc4867088b3e074f9610932f8e4e192c2/README.md#legal
echo Using this script downloads third party software
echo ------------------------------------------------
echo This script will direct to Chocolatey to install packages. By using
echo Chocolatey to install a package, you are accepting the license for the
echo application, executable(s), or other artifacts delivered to your machine as a
echo result of a Chocolatey install. This acceptance occurs whether you know the
echo license terms or not. Read and understand the license terms of the packages
echo being installed and their dependencies prior to installation:
echo - https://chocolatey.org/packages/chocolatey
echo - https://chocolatey.org/packages/python
echo - https://chocolatey.org/packages/visualstudio2019-workload-vctools
echo.
echo This script is provided AS-IS without any warranties of any kind
echo ----------------------------------------------------------------
echo Chocolatey has implemented security safeguards in their process to help
echo protect the community from malicious or pirated software, but any use of this
echo script is at your own risk. Please read the Chocolatey's legal terms of use
echo as well as how the community repository for Chocolatey.org is maintained.
echo.
pause

cls

"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" ^
-NoProfile ^
-InputFormat None ^
-ExecutionPolicy Bypass ^
-Command Start-Process ^
    '%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe' ^
    -ArgumentList '-NoProfile -InputFormat None -ExecutionPolicy Bypass -Command ^
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; ^
    iex ((New-Object System.Net.WebClient).DownloadString(''https://chocolatey.org/install.ps1'')); ^
    choco upgrade -y python visualstudio2019-workload-vctools; ^
    Read-Host ''Type ENTER to exit'' ' ^
    -Verb RunAs

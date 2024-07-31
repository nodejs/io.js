@echo off

@REM From December 2023, new certificates use DigiCert cloud HSM service for EV signing.
@REM They provide a client side app smctl.exe for managing certificates and signing process.
@REM Release CI machines are configured to have it in the PATH so this can be used safely.
smctl sign -k key_nodejs -i %1
if not ERRORLEVEL 1 (
    echo Successfully signed %1 using smctl
    exit /b 0
)
echo Could not sign %1 using smctl
exit /b 1
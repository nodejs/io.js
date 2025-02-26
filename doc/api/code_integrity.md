# Code Integrity

<!-- type=misc -->

> Stability: 1.1 - Active development

Code integrity refers to the assurance that software code has not been
altered or tampered with in any unauthorized way. It ensures that
the code running on a system is exactly what was intended by the developers.

Code integrity in Node.js integrates with platform features for code integrity
policy enforcement. See platform speficic sections below for more information.

The Node.js threat model considers the code that the runtime executes to be
trusted. As such, this feature is an additional safety belt, not a strict
security boundary.

If you find a potential security vulnerability, please refer to our
[Security Policy][].

## Code Integrity on Windows

Code integrity is an opt-in feature that leverages Window Defender Application Control
to verify the code executing conforms to system policy and has not been modified since
signing time.

There are three audiences that are involved when using Node.js in an
environment enforcing code integrity: the application developers,
those administrating the system enforcing code integrity, and
the end user. The following sections describe how each audience
can interact with code integrity enforcement.

### Windows Code Integrity and Application Developers

Windows Defender Application Control uses digital signatures to verify
a file's integrity. Application developers are responsible for generating and
distributing the signature information for their Node.js application.
Application developers are also expected to design their application
in robust ways to avoid unintended code execution. This includes
use of `eval` and loading modules outside of standard methods.

Signature information for files which Node.js is intended to execute
can be stored in a catalog file. Application developers can generate
a Windows catalog file to store the hash of all files Node.js
is expected to execute.

A catalog can be generated using the `New-FileCatalog` Powershell
cmdlet. For example

```powershell
New-FileCatalog -Version 2 -CatalogFilePath MyApplicationCatalog.cat -Path \my\application\path\
```

The `Path` argument should point to the root folder containing your application's code. If
your application's code is fully contained in one file, `Path` can point to that single file.

Be sure that the catalog is generated using the final version of the files that you intend to ship
(i.e. after minifying).

The application developer should then sign the generated catalog with their Code Signing certificate
to ensure the catalog is not tampered with between distribution and execution.

This can be done with the [Set-AuthenticodeSignature commandlet](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.security/set-authenticodesignature).

### Windows Code Integrity and System Administrators

This section is intended for system administrators who want to enable Node.js
code integrity features in their environments.

This section assumes familiarity with managing WDAC polcies.
Official documentation for WDAC can be found [here](https://learn.microsoft.com/en-us/windows/security/application-security/application-control/windows-defender-application-control/).

Code integrity enforcement on Windows has two toggleable settings:
`EnforceCodeIntegrity` and `DisableInteractiveMode`. These settings are configured
by WDAC policy.

`EnforceCodeIntegrity` causes Node.js to call WldpCanExecuteFile whenever a module is loaded using `require`.
WldpCanExecuteFile verifies that the file's integrity has not been tampered with from signing time.
The system administrator should sign and install the application's file catalog where the application
is running, per WDAC guidance.

`DisableInteractiveMode` prevents Node.js from being run in interactive mode, and also disables the `-e` and `--eval`
command line options.

#### Enabling Code Integrity Enforcement

On newer Windows versions (22H2+), the preferred method of configuring application settings is done using
`AppSettings` in your WDAC Policy.

```text
<AppSettings>
  <App Manifest="wdac-manifest.xml">
    <Setting Name="EnforceCodeIntegrity" >
      <Value>True</Value>
    </Setting>
    <Setting Name="DisableInteractiveMode" >
      <Value>True</Value>
    </Setting>
  </App>
</AppSettings>
```

On older Windows versions, use the `Settings` section of your WDAC Policy.

```text
<Settings>
  <Setting Provider="Node.js" Key="Settings" ValueName="EnforceCodeIntegrity">
    <Value>
      <Boolean>true</Boolean>
    </Value>
  </Setting>
  <Setting Provider="Node.js" Key="Settings" ValueName="DisableInteractiveMode">
    <Value>
      <Boolean>true</Boolean>
    </Value>
  </Setting>
</Settings>
```

## Code Integrity on Linux

Code integrity on Linux is not yet implemented. Plans for implementation will
be made once the necessary APIs on Linux have been upstreamed. More information
can be found here: <https://github.com/nodejs/security-wg/issues/1388>

## Code Integrity on MacOS

Code integrity on MacOS is not yet implemented. Currently, there is no
timeline for implementation.

[Security Policy]: https://github.com/nodejs/node/blob/main/SECURITY.md

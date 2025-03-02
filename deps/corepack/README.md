# <img src="./icon.svg" height="25" /> corepack

[![Join us on OpenJS slack (channel #nodejs-corepack)](https://img.shields.io/badge/OpenJS%20Slack-%23nodejs--corepack-blue)](https://slack-invite.openjsf.org/)

Corepack is a zero-runtime-dependency Node.js script that acts as a bridge
between Node.js projects and the package managers they are intended to be used
with during development. In practical terms, **Corepack lets you use Yarn, npm,
and pnpm without having to install them**.

## How to Install

### Default Installs

Corepack is [distributed by default with all recent Node.js versions](https://nodejs.org/api/corepack.html).
Run `corepack enable` to install the required Yarn and pnpm binaries on your path.

### Manual Installs

<details>
<summary>Install Corepack using npm</summary>

First uninstall your global Yarn and pnpm binaries (just leave npm). In general,
you'd do this by running the following command:

```shell
npm uninstall -g yarn pnpm

# That should be enough, but if you installed Yarn without going through npm it might
# be more tedious - for example, you might need to run `brew uninstall yarn` as well.
```

Then install Corepack:

```shell
npm install -g corepack
```

We do acknowledge the irony and overhead of using npm to install Corepack, which
is at least part of why the preferred option is to use the Corepack version that
is distributed along with Node.js itself.

</details>

<details><summary>Update Corepack using npm</summary>

To install the latest version of Corepack, use:

```shell
npm install -g corepack@latest
```

If Corepack was installed on your system using a Node.js Windows Installer
`.msi` package then you might need to remove it before attempting to install a
different version of Corepack using npm. You can select the Modify option of the
Node.js app settings to access the Windows Installer feature selection, and on
the "corepack manager" feature of the Node.js `.msi` package by selecting
"Entire feature will be unavailable". See
[Repair apps and programs in Windows](https://support.microsoft.com/en-us/windows/repair-apps-and-programs-in-windows-e90eefe4-d0a2-7c1b-dd59-949a9030f317)
for instructions on accessing the Windows apps page to modify settings.

</details>

<details><summary>Install Corepack from source</summary>

See [`CONTRIBUTING.md`](./CONTRIBUTING.md).

</details>

## Usage

### When Building Packages

Just use your package managers as you usually would. Run `yarn install` in Yarn
projects, `pnpm install` in pnpm projects, and `npm` in npm projects. Corepack
will catch these calls, and depending on the situation:

- **If the local project is configured for the package manager you're using**,
  Corepack will download and cache the latest compatible version.

- **If the local project is configured for a different package manager**,
  Corepack will request you to run the command again using the right package
  manager - thus avoiding corruptions of your install artifacts.

- **If the local project isn't configured for any package manager**, Corepack
  will assume that you know what you're doing, and will use whatever package
  manager version has been pinned as "known good release". Check the relevant
  section for more details.

### When Authoring Packages

Set your package's manager with the `packageManager` field in `package.json`:

```json
{
  "packageManager": "yarn@3.2.3+sha224.953c8233f7a92884eee2de69a1b92d1f2ec1655e66d08071ba9a02fa"
}
```

Here, `yarn` is the name of the package manager, specified at version `3.2.3`,
along with the SHA-224 hash of this version for validation.
`packageManager@x.y.z` is required. The hash is optional but strongly
recommended as a security practice. Permitted values for the package manager are
`yarn`, `npm`, and `pnpm`.

You can also provide a URL to a `.js` file (which will be interpreted as a
CommonJS module) or a `.tgz` file (which will be interpreted as a package, and
the `"bin"` field of the `package.json` will be used to determine which file to
use in the archive).

```json
{
  "packageManager": "yarn@https://registry.npmjs.org/@yarnpkg/cli-dist/-/cli-dist-3.2.3.tgz#sha224.16a0797d1710d1fb7ec40ab5c3801b68370a612a9b66ba117ad9924b"
}
```

#### `devEngines.packageManager`

When a `devEngines.packageManager` field is defined, and is an object containing
a `"name"` field (can also optionally contain `version` and `onFail` fields),
Corepack will use it to validate you're using a compatible package manager.

Depending on the value of `devEngines.packageManager.onFail`:

- if set to `ignore`, Corepack won't print any warning or error.
- if unset or set to `error`, Corepack will throw an error in case of a mismatch.
- if set to `warn` or some other value, Corepack will print a warning in case
  of mismatch.

If the top-level `packageManager` field is missing, Corepack will use the
package manager defined in `devEngines.packageManager` – in which case you must
provide a specific version in `devEngines.packageManager.version`, ideally with
a hash, as explained in the previous section:

```json
{
  "devEngines":{
    "packageManager": {
      "name": "yarn",
      "version": "3.2.3+sha224.953c8233f7a92884eee2de69a1b92d1f2ec1655e66d08071ba9a02fa"
    }
  }
}
```

## Known Good Releases

When running Corepack within projects that don't list a supported package
manager, it will default to a set of Known Good Releases.

If there is no Known Good Release for the requested package manager, Corepack
looks up the npm registry for the latest available version and cache it for
future use.

The Known Good Releases can be updated system-wide using `corepack install -g`.
When Corepack downloads a new version of a given package manager on the same
major line as the Known Good Release, it auto-updates it by default.

## Offline Workflow

The utility commands detailed in the next section.

- Either you can use the network while building your container image, in which
  case you'll simply run `corepack pack` to make sure that your image
  includes the Last Known Good release for the specified package manager.

- Or you're publishing your project to a system where the network is
  unavailable, in which case you'll preemptively generate a package manager
  archive from your local computer (using `corepack pack -o`) before storing
  it somewhere your container will be able to access (for example within your
  repository). After that it'll just be a matter of running
  `corepack install -g --cache-only <path/to/corepack.tgz>` to setup the cache.

## Utility Commands

### `corepack <binary name>[@<version>] [... args]`

This meta-command runs the specified package manager in the local folder. You
can use it to force an install to run with a given version, which can be useful
when looking for regressions.

Note that those commands still check whether the local project is configured for
the given package manager (ie you won't be able to run `corepack yarn install`
on a project where the `packageManager` field references `pnpm`).

### `corepack cache clean`

Clears the local `COREPACK_HOME` cache directory.

### `corepack cache clear`

Clears the local `COREPACK_HOME` cache directory.

### `corepack enable [... name]`

| Option                | Description                             |
| --------------------- | --------------------------------------- |
| `--install-directory` | Add the shims to the specified location |

This command will detect where Corepack is installed and will create shims next
to it for each of the specified package managers (or all of them if the command
is called without parameters). Note that the npm shims will not be installed
unless explicitly requested, as npm is currently distributed with Node.js
through other means.

If the file system where the `corepack` binary is located is read-only, this
command will fail. A workaround is to add the binaries as alias in your
shell configuration file (e.g. in `~/.bash_aliases`):

```sh
alias yarn="corepack yarn"
alias yarnpkg="corepack yarnpkg"
alias pnpm="corepack pnpm"
alias pnpx="corepack pnpx"
alias npm="corepack npm"
alias npx="corepack npx"
```

On Windows PowerShell, you can add functions using the `$PROFILE` automatic
variable:

```powershell
echo "function yarn { corepack yarn `$args }" >> $PROFILE
echo "function yarnpkg { corepack yarnpkg `$args }" >> $PROFILE
echo "function pnpm { corepack pnpm `$args }" >> $PROFILE
echo "function pnpx { corepack pnpx `$args }" >> $PROFILE
echo "function npm { corepack npm `$args }" >> $PROFILE
echo "function npx { corepack npx `$args }" >> $PROFILE
```

### `corepack disable [... name]`

| Option                | Description                                |
| --------------------- | ------------------------------------------ |
| `--install-directory` | Remove the shims to the specified location |

This command will detect where Node.js is installed and will remove the shims
from there.

### `corepack install`

Download and install the package manager configured in the local project.
This command doesn't change the global version used when running the package
manager from outside the project (use the \`-g,--global\` flag if you wish
to do this).

### `corepack install <-g,--global> [... name[@<version>]]`

Install the selected package managers and install them on the system.

Package managers thus installed will be configured as the new default when
calling their respective binaries outside of projects defining the
`packageManager` field.

### `corepack pack [... name[@<version>]]`

| Option                | Description                                |
| --------------------- | ------------------------------------------ |
| `--json `             | Print the output folder rather than logs   |
| `-o,--output `        | Path where to generate the archive         |

Download the selected package managers and store them inside a tarball
suitable for use with `corepack install -g`.

### `corepack use <name[@<version>]>`

When run, this command will retrieve the latest release matching the provided
descriptor, assign it to the project's package.json file, and automatically
perform an install.

### `corepack up`

Retrieve the latest available version for the current major release line of
the package manager used in the local project, and update the project to use
it.

Unlike `corepack use` this command doesn't take a package manager name nor a
version range, as it will always select the latest available version from the
range specified in `devEngines.packageManager.version`, or fallback to the
same major line. Should you need to upgrade to a new major, use an explicit
`corepack use {name}@latest` call (or simply `corepack use {name}`).

## Environment Variables

- `COREPACK_DEFAULT_TO_LATEST` can be set to `0` in order to instruct Corepack
  not to lookup on the remote registry for the latest version of the selected
  package manager, and to not update the Last Known Good version when it
  downloads a new version of the same major line.

- `COREPACK_ENABLE_AUTO_PIN` can be set to `0` to prevent Corepack from
  updating the `packageManager` field when it detects that the local package
  doesn't list it. In general we recommend to always list a `packageManager`
  field (which you can easily set through `corepack use [name]@[version]`), as
  it ensures that your project installs are always deterministic.

- `COREPACK_ENABLE_DOWNLOAD_PROMPT` can be set to `0` to
  prevent Corepack showing the URL when it needs to download software, or can be
  set to `1` to have the URL shown. By default, when Corepack is called
  explicitly (e.g. `corepack pnpm …`), it is set to `0`; when Corepack is called
  implicitly (e.g. `pnpm …`), it is set to `1`.
  The default value cannot be overridden in a `.corepack.env` file.
  When standard input is a TTY and no CI environment is detected, Corepack will
  ask for user input before starting the download.

- `COREPACK_ENABLE_UNSAFE_CUSTOM_URLS` can be set to `1` to allow use of
  custom URLs to load a package manager known by Corepack (`yarn`, `npm`, and
  `pnpm`).

- `COREPACK_ENABLE_NETWORK` can be set to `0` to prevent Corepack from accessing
  the network (in which case you'll be responsible for hydrating the package
  manager versions that will be required for the projects you'll run, using
  `corepack install -g --cache-only`).

- `COREPACK_ENABLE_STRICT` can be set to `0` to prevent Corepack from throwing
  error if the package manager does not correspond to the one defined for the
  current project. This means that if a user is using the package manager
  specified in the current project, it will use the version specified by the
  project's `packageManager` field. But if the user is using other package
  manager different from the one specified for the current project, it will use
  the system-wide package manager version.

- `COREPACK_ENABLE_PROJECT_SPEC` can be set to `0` to prevent Corepack from
  checking if the package manager corresponds to the one defined for the current
  project. This means that it will always use the system-wide package manager
  regardless of what is being specified in the project's `packageManager` field.

- `COREPACK_ENV_FILE` can be set to `0` to request Corepack to not attempt to
  load `.corepack.env`; it can be set to a path to specify a different env file.
  Only keys that start with `COREPACK_` and are not in the exception list
  (`COREPACK_ENABLE_DOWNLOAD_PROMPT` and `COREPACK_ENV_FILE` are ignored)
  will be taken into account.
  For Node.js 18.x users, this setting has no effect as that version doesn't
  support parsing of `.env` files.

- `COREPACK_HOME` can be set in order to define where Corepack should install
  the package managers. By default it is set to `%LOCALAPPDATA%\node\corepack`
  on Windows, and to `$HOME/.cache/node/corepack` everywhere else.

- `COREPACK_ROOT` has no functional impact on Corepack itself; it's
  automatically being set in your environment by Corepack when it shells out to
  the underlying package managers, so that they can feature-detect its presence
  (useful for commands like `yarn init`).

- `COREPACK_NPM_REGISTRY` sets the registry base url used when retrieving
  package managers from npm. Default value is `https://registry.npmjs.org`

- `COREPACK_NPM_TOKEN` sets a Bearer token authorization header when connecting
  to a npm type registry.

- `COREPACK_NPM_USERNAME` and `COREPACK_NPM_PASSWORD` to set a Basic
  authorization header when connecting to a npm type registry. Note that both
  environment variables are required and as plain text. If you want to send an
  empty password, explicitly set `COREPACK_NPM_PASSWORD` to an empty string.

- `HTTP_PROXY`, `HTTPS_PROXY`, and `NO_PROXY` are supported through
  [`proxy-from-env`](https://github.com/Rob--W/proxy-from-env).

- `COREPACK_INTEGRITY_KEYS` can be set to an empty string or `0` to
  instruct Corepack to skip integrity checks, or to a JSON string containing
  custom keys.

## Troubleshooting

The environment variable `DEBUG` can be set to `corepack` to enable additional debug logging.

### Networking

There are a wide variety of networking issues that can occur while running
`corepack` commands. Things to check:

- Make sure your network connection is active.
- Make sure the host for your request can be resolved by your DNS; try using
  `curl [URL]` (ipv4) and `curl -6 [URL]` (ipv6) from your shell.
- Check your proxy settings (see [Environment Variables](#environment-variables)).

## Contributing

See [`CONTRIBUTING.md`](./CONTRIBUTING.md).

## License (MIT)

See [`LICENSE.md`](./LICENSE.md).

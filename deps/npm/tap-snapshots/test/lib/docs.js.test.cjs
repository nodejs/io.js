/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/docs.js TAP basic usage > must match snapshot 1`] = `
npm <command>

Usage:

npm install        install all the dependencies in your project
npm install <foo>  add the <foo> dependency to your project
npm test           run this project's tests
npm run <foo>      run the script named <foo>
npm <command> -h   quick help on <command>
npm -l             display usage info for all commands
npm help <term>    search for help on <term>
npm help npm       more involved overview

All commands:



Specify configs in the ini-formatted file:
    {USERCONFIG}
or on the command line via: npm <command> --key=value

More configuration info: npm help config
Configuration fields: npm help 7 config

npm@{VERSION} {BASEDIR}
`

exports[`test/lib/docs.js TAP command list > aliases 1`] = `
Object {
  "add": "install",
  "add-user": "adduser",
  "author": "owner",
  "c": "config",
  "cit": "install-ci-test",
  "clean-install": "ci",
  "clean-install-test": "install-ci-test",
  "create": "init",
  "ddp": "dedupe",
  "dist-tags": "dist-tag",
  "find": "search",
  "hlep": "help",
  "home": "docs",
  "i": "install",
  "ic": "ci",
  "in": "install",
  "info": "view",
  "innit": "init",
  "ins": "install",
  "inst": "install",
  "insta": "install",
  "instal": "install",
  "install-clean": "ci",
  "isnt": "install",
  "isnta": "install",
  "isntal": "install",
  "isntall": "install",
  "isntall-clean": "ci",
  "issues": "bugs",
  "it": "install-test",
  "la": "ll",
  "list": "ls",
  "ln": "link",
  "ogr": "org",
  "r": "uninstall",
  "rb": "rebuild",
  "remove": "uninstall",
  "rm": "uninstall",
  "rum": "run-script",
  "run": "run-script",
  "s": "search",
  "se": "search",
  "show": "view",
  "sit": "install-ci-test",
  "t": "test",
  "tst": "test",
  "udpate": "update",
  "un": "uninstall",
  "unlink": "uninstall",
  "up": "update",
  "upgrade": "update",
  "urn": "run-script",
  "v": "view",
  "verison": "version",
  "why": "explain",
  "x": "exec",
}
`

exports[`test/lib/docs.js TAP command list > commands 1`] = `
Array [
  "access",
  "adduser",
  "audit",
  "bugs",
  "cache",
  "ci",
  "completion",
  "config",
  "dedupe",
  "deprecate",
  "diff",
  "dist-tag",
  "docs",
  "doctor",
  "edit",
  "exec",
  "explain",
  "explore",
  "find-dupes",
  "fund",
  "get",
  "help",
  "help-search",
  "hook",
  "init",
  "install",
  "install-ci-test",
  "install-test",
  "link",
  "ll",
  "login",
  "logout",
  "ls",
  "org",
  "outdated",
  "owner",
  "pack",
  "ping",
  "pkg",
  "prefix",
  "profile",
  "prune",
  "publish",
  "query",
  "rebuild",
  "repo",
  "restart",
  "root",
  "run-script",
  "sbom",
  "search",
  "set",
  "shrinkwrap",
  "star",
  "stars",
  "start",
  "stop",
  "team",
  "test",
  "token",
  "uninstall",
  "unpublish",
  "unstar",
  "update",
  "version",
  "view",
  "whoami",
]
`

exports[`test/lib/docs.js TAP command list > deref 1`] = `
Function deref(c)
`

exports[`test/lib/docs.js TAP config > all definitions 1`] = `
#### \`_auth\`

* Default: null
* Type: null or String

A basic-auth string to use when authenticating against the npm registry.
This will ONLY be used to authenticate against the npm registry. For other
registries you will need to scope it like "//other-registry.tld/:_auth"

Warning: This should generally not be set via a command-line option. It is
safer to use a registry-provided authentication bearer token stored in the
~/.npmrc file by running \`npm login\`.



#### \`access\`

* Default: 'public' for new packages, existing packages it will not change the
  current level
* Type: null, "restricted", or "public"

If you do not want your scoped package to be publicly viewable (and
installable) set \`--access=restricted\`.

Unscoped packages can not be set to \`restricted\`.

Note: This defaults to not changing the current access level for existing
packages. Specifying a value of \`restricted\` or \`public\` during publish will
change the access for an existing package the same way that \`npm access set
status\` would.



#### \`all\`

* Default: false
* Type: Boolean

When running \`npm outdated\` and \`npm ls\`, setting \`--all\` will show all
outdated or installed packages, rather than only those directly depended
upon by the current project.



#### \`allow-same-version\`

* Default: false
* Type: Boolean

Prevents throwing an error when \`npm version\` is used to set the new version
to the same value as the current version.



#### \`audit\`

* Default: true
* Type: Boolean

When "true" submit audit reports alongside the current npm command to the
default registry and all registries configured for scopes. See the
documentation for [\`npm audit\`](/commands/npm-audit) for details on what is
submitted.



#### \`audit-level\`

* Default: null
* Type: null, "info", "low", "moderate", "high", "critical", or "none"

The minimum level of vulnerability for \`npm audit\` to exit with a non-zero
exit code.



#### \`auth-type\`

* Default: "web"
* Type: "legacy" or "web"

What authentication strategy to use with \`login\`. Note that if an \`otp\`
config is given, this value will always be set to \`legacy\`.



#### \`before\`

* Default: null
* Type: null or Date

If passed to \`npm install\`, will rebuild the npm tree such that only
versions that were available **on or before** the \`--before\` time get
installed. If there's no versions available for the current set of direct
dependencies, the command will error.

If the requested version is a \`dist-tag\` and the given tag does not pass the
\`--before\` filter, the most recent version less than or equal to that tag
will be used. For example, \`foo@latest\` might install \`foo@1.2\` even though
\`latest\` is \`2.0\`.



#### \`bin-links\`

* Default: true
* Type: Boolean

Tells npm to create symlinks (or \`.cmd\` shims on Windows) for package
executables.

Set to false to have it not do this. This can be used to work around the
fact that some file systems don't support symlinks, even on ostensibly Unix
systems.



#### \`browser\`

* Default: OS X: \`"open"\`, Windows: \`"start"\`, Others: \`"xdg-open"\`
* Type: null, Boolean, or String

The browser that is called by npm commands to open websites.

Set to \`false\` to suppress browser behavior and instead print urls to
terminal.

Set to \`true\` to use default system URL opener.



#### \`ca\`

* Default: null
* Type: null or String (can be set multiple times)

The Certificate Authority signing certificate that is trusted for SSL
connections to the registry. Values should be in PEM format (Windows calls
it "Base-64 encoded X.509 (.CER)") with newlines replaced by the string
"\\n". For example:

\`\`\`ini
ca="-----BEGIN CERTIFICATE-----\\nXXXX\\nXXXX\\n-----END CERTIFICATE-----"
\`\`\`

Set to \`null\` to only allow "known" registrars, or to a specific CA cert to
trust only that specific signing authority.

Multiple CAs can be trusted by specifying an array of certificates:

\`\`\`ini
ca[]="..."
ca[]="..."
\`\`\`

See also the \`strict-ssl\` config.



#### \`cache\`

* Default: Windows: \`%LocalAppData%\\npm-cache\`, Posix: \`~/.npm\`
* Type: Path

The location of npm's cache directory.



#### \`cafile\`

* Default: null
* Type: Path

A path to a file containing one or multiple Certificate Authority signing
certificates. Similar to the \`ca\` setting, but allows for multiple CA's, as
well as for the CA information to be stored in a file on disk.



#### \`call\`

* Default: ""
* Type: String

Optional companion option for \`npm exec\`, \`npx\` that allows for specifying a
custom command to be run along with the installed packages.

\`\`\`bash
npm exec --package yo --package generator-node --call "yo node"
\`\`\`



#### \`cidr\`

* Default: null
* Type: null or String (can be set multiple times)

This is a list of CIDR address to be used when configuring limited access
tokens with the \`npm token create\` command.



#### \`color\`

* Default: true unless the NO_COLOR environ is set to something other than '0'
* Type: "always" or Boolean

If false, never shows colors. If \`"always"\` then always shows colors. If
true, then only prints color codes for tty file descriptors.



#### \`commit-hooks\`

* Default: true
* Type: Boolean

Run git commit hooks when using the \`npm version\` command.



#### \`cpu\`

* Default: null
* Type: null or String

Override CPU architecture of native modules to install. Acceptable values
are same as \`cpu\` field of package.json, which comes from \`process.arch\`.



#### \`depth\`

* Default: \`Infinity\` if \`--all\` is set, otherwise \`1\`
* Type: null or Number

The depth to go when recursing packages for \`npm ls\`.

If not set, \`npm ls\` will show only the immediate dependencies of the root
project. If \`--all\` is set, then npm will show all dependencies by default.



#### \`description\`

* Default: true
* Type: Boolean

Show the description in \`npm search\`



#### \`diff\`

* Default:
* Type: String (can be set multiple times)

Define arguments to compare in \`npm diff\`.



#### \`diff-dst-prefix\`

* Default: "b/"
* Type: String

Destination prefix to be used in \`npm diff\` output.



#### \`diff-ignore-all-space\`

* Default: false
* Type: Boolean

Ignore whitespace when comparing lines in \`npm diff\`.



#### \`diff-name-only\`

* Default: false
* Type: Boolean

Prints only filenames when using \`npm diff\`.



#### \`diff-no-prefix\`

* Default: false
* Type: Boolean

Do not show any source or destination prefix in \`npm diff\` output.

Note: this causes \`npm diff\` to ignore the \`--diff-src-prefix\` and
\`--diff-dst-prefix\` configs.



#### \`diff-src-prefix\`

* Default: "a/"
* Type: String

Source prefix to be used in \`npm diff\` output.



#### \`diff-text\`

* Default: false
* Type: Boolean

Treat all files as text in \`npm diff\`.



#### \`diff-unified\`

* Default: 3
* Type: Number

The number of lines of context to print in \`npm diff\`.



#### \`dry-run\`

* Default: false
* Type: Boolean

Indicates that you don't want npm to make any changes and that it should
only report what it would have done. This can be passed into any of the
commands that modify your local installation, eg, \`install\`, \`update\`,
\`dedupe\`, \`uninstall\`, as well as \`pack\` and \`publish\`.

Note: This is NOT honored by other network related commands, eg \`dist-tags\`,
\`owner\`, etc.



#### \`editor\`

* Default: The EDITOR or VISUAL environment variables, or
  '%SYSTEMROOT%\\notepad.exe' on Windows, or 'vi' on Unix systems
* Type: String

The command to run for \`npm edit\` and \`npm config edit\`.



#### \`engine-strict\`

* Default: false
* Type: Boolean

If set to true, then npm will stubbornly refuse to install (or even consider
installing) any package that claims to not be compatible with the current
Node.js version.

This can be overridden by setting the \`--force\` flag.



#### \`expect-result-count\`

* Default: null
* Type: null or Number

Tells to expect a specific number of results from the command.

This config can not be used with: \`expect-results\`

#### \`expect-results\`

* Default: null
* Type: null or Boolean

Tells npm whether or not to expect results from the command. Can be either
true (expect some results) or false (expect no results).

This config can not be used with: \`expect-result-count\`

#### \`fetch-retries\`

* Default: 2
* Type: Number

The "retries" config for the \`retry\` module to use when fetching packages
from the registry.

npm will retry idempotent read requests to the registry in the case of
network failures or 5xx HTTP errors.



#### \`fetch-retry-factor\`

* Default: 10
* Type: Number

The "factor" config for the \`retry\` module to use when fetching packages.



#### \`fetch-retry-maxtimeout\`

* Default: 60000 (1 minute)
* Type: Number

The "maxTimeout" config for the \`retry\` module to use when fetching
packages.



#### \`fetch-retry-mintimeout\`

* Default: 10000 (10 seconds)
* Type: Number

The "minTimeout" config for the \`retry\` module to use when fetching
packages.



#### \`fetch-timeout\`

* Default: 300000 (5 minutes)
* Type: Number

The maximum amount of time to wait for HTTP requests to complete.



#### \`force\`

* Default: false
* Type: Boolean

Removes various protections against unfortunate side effects, common
mistakes, unnecessary performance degradation, and malicious input.

* Allow clobbering non-npm files in global installs.
* Allow the \`npm version\` command to work on an unclean git repository.
* Allow deleting the cache folder with \`npm cache clean\`.
* Allow installing packages that have an \`engines\` declaration requiring a
  different version of npm.
* Allow installing packages that have an \`engines\` declaration requiring a
  different version of \`node\`, even if \`--engine-strict\` is enabled.
* Allow \`npm audit fix\` to install modules outside your stated dependency
  range (including SemVer-major changes).
* Allow unpublishing all versions of a published package.
* Allow conflicting peerDependencies to be installed in the root project.
* Implicitly set \`--yes\` during \`npm init\`.
* Allow clobbering existing values in \`npm pkg\`
* Allow unpublishing of entire packages (not just a single version).

If you don't have a clear idea of what you want to do, it is strongly
recommended that you do not use this option!



#### \`foreground-scripts\`

* Default: \`false\` unless when using \`npm pack\` or \`npm publish\` where it
  defaults to \`true\`
* Type: Boolean

Run all build scripts (ie, \`preinstall\`, \`install\`, and \`postinstall\`)
scripts for installed packages in the foreground process, sharing standard
input, output, and error with the main npm process.

Note that this will generally make installs run slower, and be much noisier,
but can be useful for debugging.



#### \`format-package-lock\`

* Default: true
* Type: Boolean

Format \`package-lock.json\` or \`npm-shrinkwrap.json\` as a human readable
file.



#### \`fund\`

* Default: true
* Type: Boolean

When "true" displays the message at the end of each \`npm install\`
acknowledging the number of dependencies looking for funding. See [\`npm
fund\`](/commands/npm-fund) for details.



#### \`git\`

* Default: "git"
* Type: String

The command to use for git commands. If git is installed on the computer,
but is not in the \`PATH\`, then set this to the full path to the git binary.



#### \`git-tag-version\`

* Default: true
* Type: Boolean

Tag the commit when using the \`npm version\` command. Setting this to false
results in no commit being made at all.



#### \`global\`

* Default: false
* Type: Boolean

Operates in "global" mode, so that packages are installed into the \`prefix\`
folder instead of the current working directory. See
[folders](/configuring-npm/folders) for more on the differences in behavior.

* packages are installed into the \`{prefix}/lib/node_modules\` folder, instead
  of the current working directory.
* bin files are linked to \`{prefix}/bin\`
* man pages are linked to \`{prefix}/share/man\`



#### \`globalconfig\`

* Default: The global --prefix setting plus 'etc/npmrc'. For example,
  '/usr/local/etc/npmrc'
* Type: Path

The config file to read for global config options.



#### \`heading\`

* Default: "npm"
* Type: String

The string that starts all the debugging log output.



#### \`https-proxy\`

* Default: null
* Type: null or URL

A proxy to use for outgoing https requests. If the \`HTTPS_PROXY\` or
\`https_proxy\` or \`HTTP_PROXY\` or \`http_proxy\` environment variables are set,
proxy settings will be honored by the underlying \`make-fetch-happen\`
library.



#### \`if-present\`

* Default: false
* Type: Boolean

If true, npm will not exit with an error code when \`run-script\` is invoked
for a script that isn't defined in the \`scripts\` section of \`package.json\`.
This option can be used when it's desirable to optionally run a script when
it's present and fail if the script fails. This is useful, for example, when
running scripts that may only apply for some builds in an otherwise generic
CI setup.

This value is not exported to the environment for child processes.

#### \`ignore-scripts\`

* Default: false
* Type: Boolean

If true, npm does not run scripts specified in package.json files.

Note that commands explicitly intended to run a particular script, such as
\`npm start\`, \`npm stop\`, \`npm restart\`, \`npm test\`, and \`npm run-script\`
will still run their intended script if \`ignore-scripts\` is set, but they
will *not* run any pre- or post-scripts.



#### \`include\`

* Default:
* Type: "prod", "dev", "optional", or "peer" (can be set multiple times)

Option that allows for defining which types of dependencies to install.

This is the inverse of \`--omit=<type>\`.

Dependency types specified in \`--include\` will not be omitted, regardless of
the order in which omit/include are specified on the command-line.



#### \`include-staged\`

* Default: false
* Type: Boolean

Allow installing "staged" published packages, as defined by [npm RFC PR
#92](https://github.com/npm/rfcs/pull/92).

This is experimental, and not implemented by the npm public registry.



#### \`include-workspace-root\`

* Default: false
* Type: Boolean

Include the workspace root when workspaces are enabled for a command.

When false, specifying individual workspaces via the \`workspace\` config, or
all workspaces via the \`workspaces\` flag, will cause npm to operate only on
the specified workspaces, and not on the root project.

This value is not exported to the environment for child processes.

#### \`init-author-email\`

* Default: ""
* Type: String

The value \`npm init\` should use by default for the package author's email.



#### \`init-author-name\`

* Default: ""
* Type: String

The value \`npm init\` should use by default for the package author's name.



#### \`init-author-url\`

* Default: ""
* Type: "" or URL

The value \`npm init\` should use by default for the package author's
homepage.



#### \`init-license\`

* Default: "ISC"
* Type: String

The value \`npm init\` should use by default for the package license.



#### \`init-module\`

* Default: "~/.npm-init.js"
* Type: Path

A module that will be loaded by the \`npm init\` command. See the
documentation for the
[init-package-json](https://github.com/npm/init-package-json) module for
more information, or [npm init](/commands/npm-init).



#### \`init-version\`

* Default: "1.0.0"
* Type: SemVer string

The value that \`npm init\` should use by default for the package version
number, if not already set in package.json.



#### \`install-links\`

* Default: false
* Type: Boolean

When set file: protocol dependencies will be packed and installed as regular
dependencies instead of creating a symlink. This option has no effect on
workspaces.



#### \`install-strategy\`

* Default: "hoisted"
* Type: "hoisted", "nested", "shallow", or "linked"

Sets the strategy for installing packages in node_modules. hoisted
(default): Install non-duplicated in top-level, and duplicated as necessary
within directory structure. nested: (formerly --legacy-bundling) install in
place, no hoisting. shallow (formerly --global-style) only install direct
deps at top-level. linked: (experimental) install in node_modules/.store,
link in place, unhoisted.



#### \`json\`

* Default: false
* Type: Boolean

Whether or not to output JSON data, rather than the normal output.

* In \`npm pkg set\` it enables parsing set values with JSON.parse() before
  saving them to your \`package.json\`.

Not supported by all npm commands.



#### \`legacy-peer-deps\`

* Default: false
* Type: Boolean

Causes npm to completely ignore \`peerDependencies\` when building a package
tree, as in npm versions 3 through 6.

If a package cannot be installed because of overly strict \`peerDependencies\`
that collide, it provides a way to move forward resolving the situation.

This differs from \`--omit=peer\`, in that \`--omit=peer\` will avoid unpacking
\`peerDependencies\` on disk, but will still design a tree such that
\`peerDependencies\` _could_ be unpacked in a correct place.

Use of \`legacy-peer-deps\` is not recommended, as it will not enforce the
\`peerDependencies\` contract that meta-dependencies may rely on.



#### \`libc\`

* Default: null
* Type: null or String

Override libc of native modules to install. Acceptable values are same as
\`libc\` field of package.json



#### \`link\`

* Default: false
* Type: Boolean

Used with \`npm ls\`, limiting output to only those packages that are linked.



#### \`local-address\`

* Default: null
* Type: IP Address

The IP address of the local interface to use when making connections to the
npm registry. Must be IPv4 in versions of Node prior to 0.12.



#### \`location\`

* Default: "user" unless \`--global\` is passed, which will also set this value
  to "global"
* Type: "global", "user", or "project"

When passed to \`npm config\` this refers to which config file to use.

When set to "global" mode, packages are installed into the \`prefix\` folder
instead of the current working directory. See
[folders](/configuring-npm/folders) for more on the differences in behavior.

* packages are installed into the \`{prefix}/lib/node_modules\` folder, instead
  of the current working directory.
* bin files are linked to \`{prefix}/bin\`
* man pages are linked to \`{prefix}/share/man\`



#### \`lockfile-version\`

* Default: Version 3 if no lockfile, auto-converting v1 lockfiles to v3,
  otherwise maintain current lockfile version.
* Type: null, 1, 2, 3, "1", "2", or "3"

Set the lockfile format version to be used in package-lock.json and
npm-shrinkwrap-json files. Possible options are:

1: The lockfile version used by npm versions 5 and 6. Lacks some data that
is used during the install, resulting in slower and possibly less
deterministic installs. Prevents lockfile churn when interoperating with
older npm versions.

2: The default lockfile version used by npm version 7 and 8. Includes both
the version 1 lockfile data and version 3 lockfile data, for maximum
determinism and interoperability, at the expense of more bytes on disk.

3: Only the new lockfile information introduced in npm version 7. Smaller on
disk than lockfile version 2, but not interoperable with older npm versions.
Ideal if all users are on npm version 7 and higher.



#### \`loglevel\`

* Default: "notice"
* Type: "silent", "error", "warn", "notice", "http", "info", "verbose", or
  "silly"

What level of logs to report. All logs are written to a debug log, with the
path to that file printed if the execution of a command fails.

Any logs of a higher level than the setting are shown. The default is
"notice".

See also the \`foreground-scripts\` config.



#### \`logs-dir\`

* Default: A directory named \`_logs\` inside the cache
* Type: null or Path

The location of npm's log directory. See [\`npm logging\`](/using-npm/logging)
for more information.



#### \`logs-max\`

* Default: 10
* Type: Number

The maximum number of log files to store.

If set to 0, no log files will be written for the current run.



#### \`long\`

* Default: false
* Type: Boolean

Show extended information in \`ls\`, \`search\`, and \`help-search\`.



#### \`maxsockets\`

* Default: 15
* Type: Number

The maximum number of connections to use per origin (protocol/host/port
combination).



#### \`message\`

* Default: "%s"
* Type: String

Commit message which is used by \`npm version\` when creating version commit.

Any "%s" in the message will be replaced with the version number.



#### \`node-options\`

* Default: null
* Type: null or String

Options to pass through to Node.js via the \`NODE_OPTIONS\` environment
variable. This does not impact how npm itself is executed but it does impact
how lifecycle scripts are called.



#### \`noproxy\`

* Default: The value of the NO_PROXY environment variable
* Type: String (can be set multiple times)

Domain extensions that should bypass any proxies.

Also accepts a comma-delimited string.



#### \`offline\`

* Default: false
* Type: Boolean

Force offline mode: no network requests will be done during install. To
allow the CLI to fill in missing cache data, see \`--prefer-offline\`.



#### \`omit\`

* Default: 'dev' if the \`NODE_ENV\` environment variable is set to
  'production', otherwise empty.
* Type: "dev", "optional", or "peer" (can be set multiple times)

Dependency types to omit from the installation tree on disk.

Note that these dependencies _are_ still resolved and added to the
\`package-lock.json\` or \`npm-shrinkwrap.json\` file. They are just not
physically installed on disk.

If a package type appears in both the \`--include\` and \`--omit\` lists, then
it will be included.

If the resulting omit list includes \`'dev'\`, then the \`NODE_ENV\` environment
variable will be set to \`'production'\` for all lifecycle scripts.



#### \`omit-lockfile-registry-resolved\`

* Default: false
* Type: Boolean

This option causes npm to create lock files without a \`resolved\` key for
registry dependencies. Subsequent installs will need to resolve tarball
endpoints with the configured registry, likely resulting in a longer install
time.



#### \`os\`

* Default: null
* Type: null or String

Override OS of native modules to install. Acceptable values are same as \`os\`
field of package.json, which comes from \`process.platform\`.



#### \`otp\`

* Default: null
* Type: null or String

This is a one-time password from a two-factor authenticator. It's needed
when publishing or changing package permissions with \`npm access\`.

If not set, and a registry response fails with a challenge for a one-time
password, npm will prompt on the command line for one.



#### \`pack-destination\`

* Default: "."
* Type: String

Directory in which \`npm pack\` will save tarballs.



#### \`package\`

* Default:
* Type: String (can be set multiple times)

The package or packages to install for [\`npm exec\`](/commands/npm-exec)



#### \`package-lock\`

* Default: true
* Type: Boolean

If set to false, then ignore \`package-lock.json\` files when installing. This
will also prevent _writing_ \`package-lock.json\` if \`save\` is true.



#### \`package-lock-only\`

* Default: false
* Type: Boolean

If set to true, the current operation will only use the \`package-lock.json\`,
ignoring \`node_modules\`.

For \`update\` this means only the \`package-lock.json\` will be updated,
instead of checking \`node_modules\` and downloading dependencies.

For \`list\` this means the output will be based on the tree described by the
\`package-lock.json\`, rather than the contents of \`node_modules\`.



#### \`parseable\`

* Default: false
* Type: Boolean

Output parseable results from commands that write to standard output. For
\`npm search\`, this will be tab-separated table format.



#### \`prefer-dedupe\`

* Default: false
* Type: Boolean

Prefer to deduplicate packages if possible, rather than choosing a newer
version of a dependency.



#### \`prefer-offline\`

* Default: false
* Type: Boolean

If true, staleness checks for cached data will be bypassed, but missing data
will be requested from the server. To force full offline mode, use
\`--offline\`.



#### \`prefer-online\`

* Default: false
* Type: Boolean

If true, staleness checks for cached data will be forced, making the CLI
look for updates immediately even for fresh package data.



#### \`prefix\`

* Default: In global mode, the folder where the node executable is installed.
  Otherwise, the nearest parent folder containing either a package.json file
  or a node_modules folder.
* Type: Path

The location to install global items. If set on the command line, then it
forces non-global commands to run in the specified folder.



#### \`preid\`

* Default: ""
* Type: String

The "prerelease identifier" to use as a prefix for the "prerelease" part of
a semver. Like the \`rc\` in \`1.2.0-rc.8\`.



#### \`progress\`

* Default: \`true\` unless running in a known CI system
* Type: Boolean

When set to \`true\`, npm will display a progress bar during time intensive
operations, if \`process.stderr\` and \`process.stdout\` are a TTY.

Set to \`false\` to suppress the progress bar.



#### \`provenance\`

* Default: false
* Type: Boolean

When publishing from a supported cloud CI/CD system, the package will be
publicly linked to where it was built and published from.

This config can not be used with: \`provenance-file\`

#### \`provenance-file\`

* Default: null
* Type: Path

When publishing, the provenance bundle at the given path will be used.

This config can not be used with: \`provenance\`

#### \`proxy\`

* Default: null
* Type: null, false, or URL

A proxy to use for outgoing http requests. If the \`HTTP_PROXY\` or
\`http_proxy\` environment variables are set, proxy settings will be honored
by the underlying \`request\` library.



#### \`read-only\`

* Default: false
* Type: Boolean

This is used to mark a token as unable to publish when configuring limited
access tokens with the \`npm token create\` command.



#### \`rebuild-bundle\`

* Default: true
* Type: Boolean

Rebuild bundled dependencies after installation.



#### \`registry\`

* Default: "https://registry.npmjs.org/"
* Type: URL

The base URL of the npm registry.



#### \`replace-registry-host\`

* Default: "npmjs"
* Type: "npmjs", "never", "always", or String

Defines behavior for replacing the registry host in a lockfile with the
configured registry.

The default behavior is to replace package dist URLs from the default
registry (https://registry.npmjs.org) to the configured registry. If set to
"never", then use the registry value. If set to "always", then replace the
registry host with the configured host every time.

You may also specify a bare hostname (e.g., "registry.npmjs.org").



#### \`save\`

* Default: \`true\` unless when using \`npm update\` where it defaults to \`false\`
* Type: Boolean

Save installed packages to a \`package.json\` file as dependencies.

When used with the \`npm rm\` command, removes the dependency from
\`package.json\`.

Will also prevent writing to \`package-lock.json\` if set to \`false\`.



#### \`save-bundle\`

* Default: false
* Type: Boolean

If a package would be saved at install time by the use of \`--save\`,
\`--save-dev\`, or \`--save-optional\`, then also put it in the
\`bundleDependencies\` list.

Ignored if \`--save-peer\` is set, since peerDependencies cannot be bundled.



#### \`save-dev\`

* Default: false
* Type: Boolean

Save installed packages to a package.json file as \`devDependencies\`.



#### \`save-exact\`

* Default: false
* Type: Boolean

Dependencies saved to package.json will be configured with an exact version
rather than using npm's default semver range operator.



#### \`save-optional\`

* Default: false
* Type: Boolean

Save installed packages to a package.json file as \`optionalDependencies\`.



#### \`save-peer\`

* Default: false
* Type: Boolean

Save installed packages to a package.json file as \`peerDependencies\`



#### \`save-prefix\`

* Default: "^"
* Type: String

Configure how versions of packages installed to a package.json file via
\`--save\` or \`--save-dev\` get prefixed.

For example if a package has version \`1.2.3\`, by default its version is set
to \`^1.2.3\` which allows minor upgrades for that package, but after \`npm
config set save-prefix='~'\` it would be set to \`~1.2.3\` which only allows
patch upgrades.



#### \`save-prod\`

* Default: false
* Type: Boolean

Save installed packages into \`dependencies\` specifically. This is useful if
a package already exists in \`devDependencies\` or \`optionalDependencies\`, but
you want to move it to be a non-optional production dependency.

This is the default behavior if \`--save\` is true, and neither \`--save-dev\`
or \`--save-optional\` are true.



#### \`sbom-format\`

* Default: null
* Type: "cyclonedx" or "spdx"

SBOM format to use when generating SBOMs.



#### \`sbom-type\`

* Default: "library"
* Type: "library", "application", or "framework"

The type of package described by the generated SBOM. For SPDX, this is the
value for the \`primaryPackagePurpose\` field. For CycloneDX, this is the
value for the \`type\` field.



#### \`scope\`

* Default: the scope of the current project, if any, or ""
* Type: String

Associate an operation with a scope for a scoped registry.

Useful when logging in to or out of a private registry:

\`\`\`
# log in, linking the scope to the custom registry
npm login --scope=@mycorp --registry=https://registry.mycorp.com

# log out, removing the link and the auth token
npm logout --scope=@mycorp
\`\`\`

This will cause \`@mycorp\` to be mapped to the registry for future
installation of packages specified according to the pattern
\`@mycorp/package\`.

This will also cause \`npm init\` to create a scoped package.

\`\`\`
# accept all defaults, and create a package named "@foo/whatever",
# instead of just named "whatever"
npm init --scope=@foo --yes
\`\`\`



#### \`script-shell\`

* Default: '/bin/sh' on POSIX systems, 'cmd.exe' on Windows
* Type: null or String

The shell to use for scripts run with the \`npm exec\`, \`npm run\` and \`npm
init <package-spec>\` commands.



#### \`searchexclude\`

* Default: ""
* Type: String

Space-separated options that limit the results from search.



#### \`searchlimit\`

* Default: 20
* Type: Number

Number of items to limit search results to. Will not apply at all to legacy
searches.



#### \`searchopts\`

* Default: ""
* Type: String

Space-separated options that are always passed to search.



#### \`searchstaleness\`

* Default: 900
* Type: Number

The age of the cache, in seconds, before another registry request is made if
using legacy search endpoint.



#### \`shell\`

* Default: SHELL environment variable, or "bash" on Posix, or "cmd.exe" on
  Windows
* Type: String

The shell to run for the \`npm explore\` command.



#### \`sign-git-commit\`

* Default: false
* Type: Boolean

If set to true, then the \`npm version\` command will commit the new package
version using \`-S\` to add a signature.

Note that git requires you to have set up GPG keys in your git configs for
this to work properly.



#### \`sign-git-tag\`

* Default: false
* Type: Boolean

If set to true, then the \`npm version\` command will tag the version using
\`-s\` to add a signature.

Note that git requires you to have set up GPG keys in your git configs for
this to work properly.



#### \`strict-peer-deps\`

* Default: false
* Type: Boolean

If set to \`true\`, and \`--legacy-peer-deps\` is not set, then _any_
conflicting \`peerDependencies\` will be treated as an install failure, even
if npm could reasonably guess the appropriate resolution based on non-peer
dependency relationships.

By default, conflicting \`peerDependencies\` deep in the dependency graph will
be resolved using the nearest non-peer dependency specification, even if
doing so will result in some packages receiving a peer dependency outside
the range set in their package's \`peerDependencies\` object.

When such an override is performed, a warning is printed, explaining the
conflict and the packages involved. If \`--strict-peer-deps\` is set, then
this warning is treated as a failure.



#### \`strict-ssl\`

* Default: true
* Type: Boolean

Whether or not to do SSL key validation when making requests to the registry
via https.

See also the \`ca\` config.



#### \`tag\`

* Default: "latest"
* Type: String

If you ask npm to install a package and don't tell it a specific version,
then it will install the specified tag.

It is the tag added to the package@version specified in the \`npm dist-tag
add\` command, if no explicit tag is given.

When used by the \`npm diff\` command, this is the tag used to fetch the
tarball that will be compared with the local files by default.

If used in the \`npm publish\` command, this is the tag that will be added to
the package submitted to the registry.



#### \`tag-version-prefix\`

* Default: "v"
* Type: String

If set, alters the prefix used when tagging a new version when performing a
version increment using \`npm version\`. To remove the prefix altogether, set
it to the empty string: \`""\`.

Because other tools may rely on the convention that npm version tags look
like \`v1.0.0\`, _only use this property if it is absolutely necessary_. In
particular, use care when overriding this setting for public packages.



#### \`timing\`

* Default: false
* Type: Boolean

If true, writes timing information to a process specific json file in the
cache or \`logs-dir\`. The file name ends with \`-timing.json\`.

You can quickly view it with this [json](https://npm.im/json) command line:
\`cat ~/.npm/_logs/*-timing.json | npm exec -- json -g\`.

Timing information will also be reported in the terminal. To suppress this
while still writing the timing file, use \`--silent\`.



#### \`umask\`

* Default: 0
* Type: Octal numeric string in range 0000..0777 (0..511)

The "umask" value to use when setting the file creation mode on files and
folders.

Folders and executables are given a mode which is \`0o777\` masked against
this value. Other files are given a mode which is \`0o666\` masked against
this value.

Note that the underlying system will _also_ apply its own umask value to
files and folders that are created, and npm does not circumvent this, but
rather adds the \`--umask\` config to it.

Thus, the effective default umask value on most POSIX systems is 0o22,
meaning that folders and executables are created with a mode of 0o755 and
other files are created with a mode of 0o644.



#### \`unicode\`

* Default: false on windows, true on mac/unix systems with a unicode locale,
  as defined by the \`LC_ALL\`, \`LC_CTYPE\`, or \`LANG\` environment variables.
* Type: Boolean

When set to true, npm uses unicode characters in the tree output. When
false, it uses ascii characters instead of unicode glyphs.



#### \`update-notifier\`

* Default: true
* Type: Boolean

Set to false to suppress the update notification when using an older version
of npm than the latest.



#### \`usage\`

* Default: false
* Type: Boolean

Show short usage output about the command specified.



#### \`user-agent\`

* Default: "npm/{npm-version} node/{node-version} {platform} {arch}
  workspaces/{workspaces} {ci}"
* Type: String

Sets the User-Agent request header. The following fields are replaced with
their actual counterparts:

* \`{npm-version}\` - The npm version in use
* \`{node-version}\` - The Node.js version in use
* \`{platform}\` - The value of \`process.platform\`
* \`{arch}\` - The value of \`process.arch\`
* \`{workspaces}\` - Set to \`true\` if the \`workspaces\` or \`workspace\` options
  are set.
* \`{ci}\` - The value of the \`ci-name\` config, if set, prefixed with \`ci/\`, or
  an empty string if \`ci-name\` is empty.



#### \`userconfig\`

* Default: "~/.npmrc"
* Type: Path

The location of user-level configuration settings.

This may be overridden by the \`npm_config_userconfig\` environment variable
or the \`--userconfig\` command line option, but may _not_ be overridden by
settings in the \`globalconfig\` file.



#### \`version\`

* Default: false
* Type: Boolean

If true, output the npm version and exit successfully.

Only relevant when specified explicitly on the command line.



#### \`versions\`

* Default: false
* Type: Boolean

If true, output the npm version as well as node's \`process.versions\` map and
the version in the current working directory's \`package.json\` file if one
exists, and exit successfully.

Only relevant when specified explicitly on the command line.



#### \`viewer\`

* Default: "man" on Posix, "browser" on Windows
* Type: String

The program to use to view help content.

Set to \`"browser"\` to view html help content in the default web browser.



#### \`which\`

* Default: null
* Type: null or Number

If there are multiple funding sources, which 1-indexed source URL to open.



#### \`workspace\`

* Default:
* Type: String (can be set multiple times)

Enable running a command in the context of the configured workspaces of the
current project while filtering by running only the workspaces defined by
this configuration option.

Valid values for the \`workspace\` config are either:

* Workspace names
* Path to a workspace directory
* Path to a parent workspace directory (will result in selecting all
  workspaces within that folder)

When set for the \`npm init\` command, this may be set to the folder of a
workspace which does not yet exist, to create the folder and set it up as a
brand new workspace within the project.

This value is not exported to the environment for child processes.

#### \`workspaces\`

* Default: null
* Type: null or Boolean

Set to true to run the command in the context of **all** configured
workspaces.

Explicitly setting this to false will cause commands like \`install\` to
ignore workspaces altogether. When not set explicitly:

- Commands that operate on the \`node_modules\` tree (install, update, etc.)
will link workspaces into the \`node_modules\` folder. - Commands that do
other things (test, exec, publish, etc.) will operate on the root project,
_unless_ one or more workspaces are specified in the \`workspace\` config.

This value is not exported to the environment for child processes.

#### \`workspaces-update\`

* Default: true
* Type: Boolean

If set to true, the npm cli will run an update after operations that may
possibly change the workspaces installed to the \`node_modules\` folder.



#### \`yes\`

* Default: null
* Type: null or Boolean

Automatically answer "yes" to any prompts that npm might print on the
command line.



#### \`also\`

* Default: null
* Type: null, "dev", or "development"
* DEPRECATED: Please use --include=dev instead.

When set to \`dev\` or \`development\`, this is an alias for \`--include=dev\`.



#### \`cache-max\`

* Default: Infinity
* Type: Number
* DEPRECATED: This option has been deprecated in favor of \`--prefer-online\`

\`--cache-max=0\` is an alias for \`--prefer-online\`



#### \`cache-min\`

* Default: 0
* Type: Number
* DEPRECATED: This option has been deprecated in favor of \`--prefer-offline\`.

\`--cache-min=9999 (or bigger)\` is an alias for \`--prefer-offline\`.



#### \`cert\`

* Default: null
* Type: null or String
* DEPRECATED: \`key\` and \`cert\` are no longer used for most registry
  operations. Use registry scoped \`keyfile\` and \`certfile\` instead. Example:
  //other-registry.tld/:keyfile=/path/to/key.pem
  //other-registry.tld/:certfile=/path/to/cert.crt

A client certificate to pass when accessing the registry. Values should be
in PEM format (Windows calls it "Base-64 encoded X.509 (.CER)") with
newlines replaced by the string "\\n". For example:

\`\`\`ini
cert="-----BEGIN CERTIFICATE-----\\nXXXX\\nXXXX\\n-----END CERTIFICATE-----"
\`\`\`

It is _not_ the path to a certificate file, though you can set a
registry-scoped "certfile" path like
"//other-registry.tld/:certfile=/path/to/cert.pem".



#### \`dev\`

* Default: false
* Type: Boolean
* DEPRECATED: Please use --include=dev instead.

Alias for \`--include=dev\`.



#### \`global-style\`

* Default: false
* Type: Boolean
* DEPRECATED: This option has been deprecated in favor of
  \`--install-strategy=shallow\`

Only install direct dependencies in the top level \`node_modules\`, but hoist
on deeper dependencies. Sets \`--install-strategy=shallow\`.



#### \`init.author.email\`

* Default: ""
* Type: String
* DEPRECATED: Use \`--init-author-email\` instead.

Alias for \`--init-author-email\`



#### \`init.author.name\`

* Default: ""
* Type: String
* DEPRECATED: Use \`--init-author-name\` instead.

Alias for \`--init-author-name\`



#### \`init.author.url\`

* Default: ""
* Type: "" or URL
* DEPRECATED: Use \`--init-author-url\` instead.

Alias for \`--init-author-url\`



#### \`init.license\`

* Default: "ISC"
* Type: String
* DEPRECATED: Use \`--init-license\` instead.

Alias for \`--init-license\`



#### \`init.module\`

* Default: "~/.npm-init.js"
* Type: Path
* DEPRECATED: Use \`--init-module\` instead.

Alias for \`--init-module\`



#### \`init.version\`

* Default: "1.0.0"
* Type: SemVer string
* DEPRECATED: Use \`--init-version\` instead.

Alias for \`--init-version\`



#### \`key\`

* Default: null
* Type: null or String
* DEPRECATED: \`key\` and \`cert\` are no longer used for most registry
  operations. Use registry scoped \`keyfile\` and \`certfile\` instead. Example:
  //other-registry.tld/:keyfile=/path/to/key.pem
  //other-registry.tld/:certfile=/path/to/cert.crt

A client key to pass when accessing the registry. Values should be in PEM
format with newlines replaced by the string "\\n". For example:

\`\`\`ini
key="-----BEGIN PRIVATE KEY-----\\nXXXX\\nXXXX\\n-----END PRIVATE KEY-----"
\`\`\`

It is _not_ the path to a key file, though you can set a registry-scoped
"keyfile" path like "//other-registry.tld/:keyfile=/path/to/key.pem".



#### \`legacy-bundling\`

* Default: false
* Type: Boolean
* DEPRECATED: This option has been deprecated in favor of
  \`--install-strategy=nested\`

Instead of hoisting package installs in \`node_modules\`, install packages in
the same manner that they are depended on. This may cause very deep
directory structures and duplicate package installs as there is no
de-duplicating. Sets \`--install-strategy=nested\`.



#### \`only\`

* Default: null
* Type: null, "prod", or "production"
* DEPRECATED: Use \`--omit=dev\` to omit dev dependencies from the install.

When set to \`prod\` or \`production\`, this is an alias for \`--omit=dev\`.



#### \`optional\`

* Default: null
* Type: null or Boolean
* DEPRECATED: Use \`--omit=optional\` to exclude optional dependencies, or
  \`--include=optional\` to include them.

Default value does install optional deps unless otherwise omitted.

Alias for --include=optional or --omit=optional



#### \`production\`

* Default: null
* Type: null or Boolean
* DEPRECATED: Use \`--omit=dev\` instead.

Alias for \`--omit=dev\`



#### \`shrinkwrap\`

* Default: true
* Type: Boolean
* DEPRECATED: Use the --package-lock setting instead.

Alias for --package-lock


`

exports[`test/lib/docs.js TAP config > all keys 1`] = `
Array [
  "_auth",
  "access",
  "all",
  "allow-same-version",
  "also",
  "audit",
  "audit-level",
  "auth-type",
  "before",
  "bin-links",
  "browser",
  "ca",
  "cache",
  "cache-max",
  "cache-min",
  "cafile",
  "call",
  "cert",
  "cidr",
  "color",
  "commit-hooks",
  "cpu",
  "depth",
  "description",
  "dev",
  "diff",
  "diff-ignore-all-space",
  "diff-name-only",
  "diff-no-prefix",
  "diff-dst-prefix",
  "diff-src-prefix",
  "diff-text",
  "diff-unified",
  "dry-run",
  "editor",
  "engine-strict",
  "expect-result-count",
  "expect-results",
  "fetch-retries",
  "fetch-retry-factor",
  "fetch-retry-maxtimeout",
  "fetch-retry-mintimeout",
  "fetch-timeout",
  "force",
  "foreground-scripts",
  "format-package-lock",
  "fund",
  "git",
  "git-tag-version",
  "global",
  "globalconfig",
  "global-style",
  "heading",
  "https-proxy",
  "if-present",
  "ignore-scripts",
  "include",
  "include-staged",
  "include-workspace-root",
  "init-author-email",
  "init-author-name",
  "init-author-url",
  "init-license",
  "init-module",
  "init-version",
  "init.author.email",
  "init.author.name",
  "init.author.url",
  "init.license",
  "init.module",
  "init.version",
  "install-links",
  "install-strategy",
  "json",
  "key",
  "legacy-bundling",
  "legacy-peer-deps",
  "libc",
  "link",
  "local-address",
  "location",
  "lockfile-version",
  "loglevel",
  "logs-dir",
  "logs-max",
  "long",
  "maxsockets",
  "message",
  "node-options",
  "noproxy",
  "offline",
  "omit",
  "omit-lockfile-registry-resolved",
  "only",
  "optional",
  "os",
  "otp",
  "package",
  "package-lock",
  "package-lock-only",
  "pack-destination",
  "parseable",
  "prefer-dedupe",
  "prefer-offline",
  "prefer-online",
  "prefix",
  "preid",
  "production",
  "progress",
  "provenance",
  "provenance-file",
  "proxy",
  "read-only",
  "rebuild-bundle",
  "registry",
  "replace-registry-host",
  "save",
  "save-bundle",
  "save-dev",
  "save-exact",
  "save-optional",
  "save-peer",
  "save-prefix",
  "save-prod",
  "sbom-format",
  "sbom-type",
  "scope",
  "script-shell",
  "searchexclude",
  "searchlimit",
  "searchopts",
  "searchstaleness",
  "shell",
  "shrinkwrap",
  "sign-git-commit",
  "sign-git-tag",
  "strict-peer-deps",
  "strict-ssl",
  "tag",
  "tag-version-prefix",
  "timing",
  "umask",
  "unicode",
  "update-notifier",
  "usage",
  "user-agent",
  "userconfig",
  "version",
  "versions",
  "viewer",
  "which",
  "workspace",
  "workspaces",
  "workspaces-update",
  "yes",
]
`

exports[`test/lib/docs.js TAP config > keys that are flattened 1`] = `
Array [
  "_auth",
  "access",
  "all",
  "allow-same-version",
  "also",
  "audit",
  "audit-level",
  "auth-type",
  "before",
  "bin-links",
  "browser",
  "ca",
  "cache",
  "cache-max",
  "cache-min",
  "cafile",
  "call",
  "cert",
  "cidr",
  "color",
  "commit-hooks",
  "cpu",
  "depth",
  "description",
  "dev",
  "diff",
  "diff-ignore-all-space",
  "diff-name-only",
  "diff-no-prefix",
  "diff-dst-prefix",
  "diff-src-prefix",
  "diff-text",
  "diff-unified",
  "dry-run",
  "editor",
  "engine-strict",
  "fetch-retries",
  "fetch-retry-factor",
  "fetch-retry-maxtimeout",
  "fetch-retry-mintimeout",
  "fetch-timeout",
  "force",
  "foreground-scripts",
  "format-package-lock",
  "fund",
  "git",
  "git-tag-version",
  "global",
  "globalconfig",
  "global-style",
  "heading",
  "https-proxy",
  "if-present",
  "ignore-scripts",
  "include",
  "include-staged",
  "include-workspace-root",
  "install-links",
  "install-strategy",
  "json",
  "key",
  "legacy-bundling",
  "legacy-peer-deps",
  "libc",
  "local-address",
  "location",
  "lockfile-version",
  "loglevel",
  "maxsockets",
  "message",
  "noproxy",
  "offline",
  "omit",
  "omit-lockfile-registry-resolved",
  "only",
  "optional",
  "os",
  "otp",
  "package",
  "package-lock",
  "package-lock-only",
  "pack-destination",
  "parseable",
  "prefer-dedupe",
  "prefer-offline",
  "prefer-online",
  "preid",
  "production",
  "progress",
  "provenance",
  "provenance-file",
  "proxy",
  "read-only",
  "rebuild-bundle",
  "registry",
  "replace-registry-host",
  "save",
  "save-bundle",
  "save-dev",
  "save-exact",
  "save-optional",
  "save-peer",
  "save-prefix",
  "save-prod",
  "sbom-format",
  "sbom-type",
  "scope",
  "script-shell",
  "searchexclude",
  "searchlimit",
  "searchopts",
  "searchstaleness",
  "shell",
  "shrinkwrap",
  "sign-git-commit",
  "sign-git-tag",
  "strict-peer-deps",
  "strict-ssl",
  "tag",
  "tag-version-prefix",
  "umask",
  "unicode",
  "user-agent",
  "workspace",
  "workspaces",
  "workspaces-update",
]
`

exports[`test/lib/docs.js TAP config > keys that are not flattened 1`] = `
Array [
  "expect-result-count",
  "expect-results",
  "init-author-email",
  "init-author-name",
  "init-author-url",
  "init-license",
  "init-module",
  "init-version",
  "init.author.email",
  "init.author.name",
  "init.author.url",
  "init.license",
  "init.module",
  "init.version",
  "link",
  "logs-dir",
  "logs-max",
  "long",
  "node-options",
  "prefix",
  "timing",
  "update-notifier",
  "usage",
  "userconfig",
  "version",
  "versions",
  "viewer",
  "which",
  "yes",
]
`

exports[`test/lib/docs.js TAP flat options > full flat options object 1`] = `
Object {
  "_auth": null,
  "access": null,
  "all": false,
  "allowSameVersion": false,
  "audit": true,
  "auditLevel": null,
  "authType": "web",
  "before": null,
  "binLinks": true,
  "browser": null,
  "ca": null,
  "cache": "{CWD}/cache/_cacache",
  "call": "",
  "cert": null,
  "cidr": null,
  "color": false,
  "commitHooks": true,
  "cpu": null,
  "defaultTag": "latest",
  "depth": null,
  "diff": Array [],
  "diffDstPrefix": "b/",
  "diffIgnoreAllSpace": false,
  "diffNameOnly": false,
  "diffNoPrefix": false,
  "diffSrcPrefix": "a/",
  "diffText": false,
  "diffUnified": 3,
  "dryRun": false,
  "editor": "{EDITOR}",
  "engineStrict": false,
  "force": false,
  "foregroundScripts": false,
  "formatPackageLock": true,
  "fund": true,
  "git": "git",
  "gitTagVersion": true,
  "global": false,
  "globalconfig": "{CWD}/global/etc/npmrc",
  "heading": "npm",
  "httpsProxy": null,
  "ifPresent": false,
  "ignoreScripts": false,
  "includeStaged": false,
  "includeWorkspaceRoot": false,
  "installLinks": false,
  "installStrategy": "hoisted",
  "json": false,
  "key": null,
  "legacyPeerDeps": false,
  "libc": null,
  "localAddress": null,
  "location": "user",
  "lockfileVersion": null,
  "logColor": false,
  "maxSockets": 15,
  "message": "%s",
  "nodeBin": "{NODE}",
  "nodeVersion": "2.2.2",
  "noProxy": "",
  "npmBin": "{CWD}/other/bin/npm-cli.js",
  "npmCommand": "version",
  "npmVersion": "3.3.3",
  "npxCache": "{CWD}/cache/_npx",
  "offline": false,
  "omit": Array [],
  "omitLockfileRegistryResolved": false,
  "os": null,
  "otp": null,
  "package": Array [],
  "packageLock": true,
  "packageLockOnly": false,
  "packDestination": ".",
  "parseable": false,
  "preferDedupe": false,
  "preferOffline": false,
  "preferOnline": false,
  "preid": "",
  "progress": false,
  "projectScope": "",
  "provenance": false,
  "provenanceFile": null,
  "proxy": null,
  "readOnly": false,
  "rebuildBundle": true,
  "registry": "https://registry.npmjs.org/",
  "replaceRegistryHost": "npmjs",
  "retry": Object {
    "factor": 10,
    "maxTimeout": 60000,
    "minTimeout": 10000,
    "retries": 0,
  },
  "save": true,
  "saveBundle": false,
  "savePrefix": "^",
  "sbomFormat": null,
  "sbomType": "library",
  "scope": "",
  "scriptShell": undefined,
  "search": Object {
    "description": true,
    "exclude": "",
    "limit": 20,
    "opts": Null Object {},
    "staleness": 900,
  },
  "shell": "{SHELL}",
  "signGitCommit": false,
  "signGitTag": false,
  "silent": false,
  "strictPeerDeps": false,
  "strictSSL": true,
  "tagVersionPrefix": "v",
  "timeout": 300000,
  "tufCache": "{CWD}/cache/_tuf",
  "umask": 0,
  "unicode": false,
  "userAgent": "npm/1.1.1 node/2.2.2 {PLATFORM} {ARCH} workspaces/false ci/{ci}",
  "workspacesEnabled": true,
  "workspacesUpdate": true,
}
`

exports[`test/lib/docs.js TAP shorthands > docs 1`] = `
* \`-a\`: \`--all\`
* \`--enjoy-by\`: \`--before\`
* \`-c\`: \`--call\`
* \`--desc\`: \`--description\`
* \`-f\`: \`--force\`
* \`-g\`: \`--global\`
* \`--iwr\`: \`--include-workspace-root\`
* \`-L\`: \`--location\`
* \`-d\`: \`--loglevel info\`
* \`-s\`: \`--loglevel silent\`
* \`--silent\`: \`--loglevel silent\`
* \`--ddd\`: \`--loglevel silly\`
* \`--dd\`: \`--loglevel verbose\`
* \`--verbose\`: \`--loglevel verbose\`
* \`-q\`: \`--loglevel warn\`
* \`--quiet\`: \`--loglevel warn\`
* \`-l\`: \`--long\`
* \`-m\`: \`--message\`
* \`--local\`: \`--no-global\`
* \`-n\`: \`--no-yes\`
* \`--no\`: \`--no-yes\`
* \`-p\`: \`--parseable\`
* \`--porcelain\`: \`--parseable\`
* \`-C\`: \`--prefix\`
* \`--readonly\`: \`--read-only\`
* \`--reg\`: \`--registry\`
* \`-S\`: \`--save\`
* \`-B\`: \`--save-bundle\`
* \`-D\`: \`--save-dev\`
* \`-E\`: \`--save-exact\`
* \`-O\`: \`--save-optional\`
* \`-P\`: \`--save-prod\`
* \`-?\`: \`--usage\`
* \`-h\`: \`--usage\`
* \`-H\`: \`--usage\`
* \`--help\`: \`--usage\`
* \`-v\`: \`--version\`
* \`-w\`: \`--workspace\`
* \`--ws\`: \`--workspaces\`
* \`-y\`: \`--yes\`
`

exports[`test/lib/docs.js TAP usage access > must match snapshot 1`] = `
Set access level on published packages

Usage:
npm access list packages [<user>|<scope>|<scope:team>] [<package>]
npm access list collaborators [<package> [<user>]]
npm access get status [<package>]
npm access set status=public|private [<package>]
npm access set mfa=none|publish|automation [<package>]
npm access grant <read-only|read-write> <scope:team> [<package>]
npm access revoke <scope:team> [<package>]

Options:
[--json] [--otp <otp>] [--registry <registry>]

Run "npm help access" for more info

\`\`\`bash
npm access list packages [<user>|<scope>|<scope:team>] [<package>]
npm access list collaborators [<package> [<user>]]
npm access get status [<package>]
npm access set status=public|private [<package>]
npm access set mfa=none|publish|automation [<package>]
npm access grant <read-only|read-write> <scope:team> [<package>]
npm access revoke <scope:team> [<package>]
\`\`\`

Note: This command is unaware of workspaces.

#### \`json\`
#### \`otp\`
#### \`registry\`
`

exports[`test/lib/docs.js TAP usage adduser > must match snapshot 1`] = `
Add a registry user account

Usage:
npm adduser

Options:
[--registry <registry>] [--scope <@scope>] [--auth-type <legacy|web>]

alias: add-user

Run "npm help adduser" for more info

\`\`\`bash
npm adduser

alias: add-user
\`\`\`

Note: This command is unaware of workspaces.

#### \`registry\`
#### \`scope\`
#### \`auth-type\`
`

exports[`test/lib/docs.js TAP usage audit > must match snapshot 1`] = `
Run a security audit

Usage:
npm audit [fix|signatures]

Options:
[--audit-level <info|low|moderate|high|critical|none>] [--dry-run] [-f|--force]
[--json] [--package-lock-only] [--no-package-lock]
[--omit <dev|optional|peer> [--omit <dev|optional|peer> ...]]
[--include <prod|dev|optional|peer> [--include <prod|dev|optional|peer> ...]]
[--foreground-scripts] [--ignore-scripts]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root] [--install-links]

Run "npm help audit" for more info

\`\`\`bash
npm audit [fix|signatures]
\`\`\`

#### \`audit-level\`
#### \`dry-run\`
#### \`force\`
#### \`json\`
#### \`package-lock-only\`
#### \`package-lock\`
#### \`omit\`
#### \`include\`
#### \`foreground-scripts\`
#### \`ignore-scripts\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
#### \`install-links\`
`

exports[`test/lib/docs.js TAP usage bugs > must match snapshot 1`] = `
Report bugs for a package in a web browser

Usage:
npm bugs [<pkgname> [<pkgname> ...]]

Options:
[--no-browser|--browser <browser>] [--registry <registry>]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root]

alias: issues

Run "npm help bugs" for more info

\`\`\`bash
npm bugs [<pkgname> [<pkgname> ...]]

alias: issues
\`\`\`

#### \`browser\`
#### \`registry\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
`

exports[`test/lib/docs.js TAP usage cache > must match snapshot 1`] = `
Manipulates packages cache

Usage:
npm cache add <package-spec>
npm cache clean [<key>]
npm cache ls [<name>@<version>]
npm cache verify

Options:
[--cache <cache>]

Run "npm help cache" for more info

\`\`\`bash
npm cache add <package-spec>
npm cache clean [<key>]
npm cache ls [<name>@<version>]
npm cache verify
\`\`\`

Note: This command is unaware of workspaces.

#### \`cache\`
`

exports[`test/lib/docs.js TAP usage ci > must match snapshot 1`] = `
Clean install a project

Usage:
npm ci

Options:
[--install-strategy <hoisted|nested|shallow|linked>] [--legacy-bundling]
[--global-style] [--omit <dev|optional|peer> [--omit <dev|optional|peer> ...]]
[--include <prod|dev|optional|peer> [--include <prod|dev|optional|peer> ...]]
[--strict-peer-deps] [--foreground-scripts] [--ignore-scripts] [--no-audit]
[--no-bin-links] [--no-fund] [--dry-run]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root] [--install-links]

aliases: clean-install, ic, install-clean, isntall-clean

Run "npm help ci" for more info

\`\`\`bash
npm ci

aliases: clean-install, ic, install-clean, isntall-clean
\`\`\`

#### \`install-strategy\`
#### \`legacy-bundling\`
#### \`global-style\`
#### \`omit\`
#### \`include\`
#### \`strict-peer-deps\`
#### \`foreground-scripts\`
#### \`ignore-scripts\`
#### \`audit\`
#### \`bin-links\`
#### \`fund\`
#### \`dry-run\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
#### \`install-links\`
`

exports[`test/lib/docs.js TAP usage completion > must match snapshot 1`] = `
Tab Completion for npm

Usage:
npm completion

Run "npm help completion" for more info

\`\`\`bash
npm completion
\`\`\`

Note: This command is unaware of workspaces.

NO PARAMS
`

exports[`test/lib/docs.js TAP usage config > must match snapshot 1`] = `
Manage the npm configuration files

Usage:
npm config set <key>=<value> [<key>=<value> ...]
npm config get [<key> [<key> ...]]
npm config delete <key> [<key> ...]
npm config list [--json]
npm config edit
npm config fix

Options:
[--json] [-g|--global] [--editor <editor>] [-L|--location <global|user|project>]
[-l|--long]

alias: c

Run "npm help config" for more info

\`\`\`bash
npm config set <key>=<value> [<key>=<value> ...]
npm config get [<key> [<key> ...]]
npm config delete <key> [<key> ...]
npm config list [--json]
npm config edit
npm config fix

alias: c
\`\`\`

Note: This command is unaware of workspaces.

#### \`json\`
#### \`global\`
#### \`editor\`
#### \`location\`
#### \`long\`
`

exports[`test/lib/docs.js TAP usage dedupe > must match snapshot 1`] = `
Reduce duplication in the package tree

Usage:
npm dedupe

Options:
[--install-strategy <hoisted|nested|shallow|linked>] [--legacy-bundling]
[--global-style] [--strict-peer-deps] [--no-package-lock]
[--omit <dev|optional|peer> [--omit <dev|optional|peer> ...]]
[--include <prod|dev|optional|peer> [--include <prod|dev|optional|peer> ...]]
[--ignore-scripts] [--no-audit] [--no-bin-links] [--no-fund] [--dry-run]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root] [--install-links]

alias: ddp

Run "npm help dedupe" for more info

\`\`\`bash
npm dedupe

alias: ddp
\`\`\`

#### \`install-strategy\`
#### \`legacy-bundling\`
#### \`global-style\`
#### \`strict-peer-deps\`
#### \`package-lock\`
#### \`omit\`
#### \`include\`
#### \`ignore-scripts\`
#### \`audit\`
#### \`bin-links\`
#### \`fund\`
#### \`dry-run\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
#### \`install-links\`
`

exports[`test/lib/docs.js TAP usage deprecate > must match snapshot 1`] = `
Deprecate a version of a package

Usage:
npm deprecate <package-spec> <message>

Options:
[--registry <registry>] [--otp <otp>]

Run "npm help deprecate" for more info

\`\`\`bash
npm deprecate <package-spec> <message>
\`\`\`

Note: This command is unaware of workspaces.

#### \`registry\`
#### \`otp\`
`

exports[`test/lib/docs.js TAP usage diff > must match snapshot 1`] = `
The registry diff command

Usage:
npm diff [...<paths>]

Options:
[--diff <package-spec> [--diff <package-spec> ...]] [--diff-name-only]
[--diff-unified <number>] [--diff-ignore-all-space] [--diff-no-prefix]
[--diff-src-prefix <path>] [--diff-dst-prefix <path>] [--diff-text] [-g|--global]
[--tag <tag>]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root]

Run "npm help diff" for more info

\`\`\`bash
npm diff [...<paths>]
\`\`\`

#### \`diff\`
#### \`diff-name-only\`
#### \`diff-unified\`
#### \`diff-ignore-all-space\`
#### \`diff-no-prefix\`
#### \`diff-src-prefix\`
#### \`diff-dst-prefix\`
#### \`diff-text\`
#### \`global\`
#### \`tag\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
`

exports[`test/lib/docs.js TAP usage dist-tag > must match snapshot 1`] = `
Modify package distribution tags

Usage:
npm dist-tag add <package-spec (with version)> [<tag>]
npm dist-tag rm <package-spec> <tag>
npm dist-tag ls [<package-spec>]

Options:
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root]

alias: dist-tags

Run "npm help dist-tag" for more info

\`\`\`bash
npm dist-tag add <package-spec (with version)> [<tag>]
npm dist-tag rm <package-spec> <tag>
npm dist-tag ls [<package-spec>]

alias: dist-tags
\`\`\`

#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
`

exports[`test/lib/docs.js TAP usage docs > must match snapshot 1`] = `
Open documentation for a package in a web browser

Usage:
npm docs [<pkgname> [<pkgname> ...]]

Options:
[--no-browser|--browser <browser>] [--registry <registry>]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root]

alias: home

Run "npm help docs" for more info

\`\`\`bash
npm docs [<pkgname> [<pkgname> ...]]

alias: home
\`\`\`

#### \`browser\`
#### \`registry\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
`

exports[`test/lib/docs.js TAP usage doctor > must match snapshot 1`] = `
Check the health of your npm environment

Usage:
npm doctor [connection] [registry] [versions] [environment] [permissions] [cache]

Options:
[--registry <registry>]

Run "npm help doctor" for more info

\`\`\`bash
npm doctor [connection] [registry] [versions] [environment] [permissions] [cache]
\`\`\`

Note: This command is unaware of workspaces.

#### \`registry\`
`

exports[`test/lib/docs.js TAP usage edit > must match snapshot 1`] = `
Edit an installed package

Usage:
npm edit <pkg>[/<subpkg>...]

Options:
[--editor <editor>]

Run "npm help edit" for more info

\`\`\`bash
npm edit <pkg>[/<subpkg>...]
\`\`\`

Note: This command is unaware of workspaces.

#### \`editor\`
`

exports[`test/lib/docs.js TAP usage exec > must match snapshot 1`] = `
Run a command from a local or remote npm package

Usage:
npm exec -- <pkg>[@<version>] [args...]
npm exec --package=<pkg>[@<version>] -- <cmd> [args...]
npm exec -c '<cmd> [args...]'
npm exec --package=foo -c '<cmd> [args...]'

Options:
[--package <package-spec> [--package <package-spec> ...]] [-c|--call <call>]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root]

alias: x

Run "npm help exec" for more info

\`\`\`bash
npm exec -- <pkg>[@<version>] [args...]
npm exec --package=<pkg>[@<version>] -- <cmd> [args...]
npm exec -c '<cmd> [args...]'
npm exec --package=foo -c '<cmd> [args...]'

alias: x
\`\`\`

#### \`package\`
#### \`call\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
`

exports[`test/lib/docs.js TAP usage explain > must match snapshot 1`] = `
Explain installed packages

Usage:
npm explain <package-spec>

Options:
[--json] [-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]

alias: why

Run "npm help explain" for more info

\`\`\`bash
npm explain <package-spec>

alias: why
\`\`\`

#### \`json\`
#### \`workspace\`
`

exports[`test/lib/docs.js TAP usage explore > must match snapshot 1`] = `
Browse an installed package

Usage:
npm explore <pkg> [ -- <command>]

Options:
[--shell <shell>]

Run "npm help explore" for more info

\`\`\`bash
npm explore <pkg> [ -- <command>]
\`\`\`

Note: This command is unaware of workspaces.

#### \`shell\`
`

exports[`test/lib/docs.js TAP usage find-dupes > must match snapshot 1`] = `
Find duplication in the package tree

Usage:
npm find-dupes

Options:
[--install-strategy <hoisted|nested|shallow|linked>] [--legacy-bundling]
[--global-style] [--strict-peer-deps] [--no-package-lock]
[--omit <dev|optional|peer> [--omit <dev|optional|peer> ...]]
[--include <prod|dev|optional|peer> [--include <prod|dev|optional|peer> ...]]
[--ignore-scripts] [--no-audit] [--no-bin-links] [--no-fund]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root] [--install-links]

Run "npm help find-dupes" for more info

\`\`\`bash
npm find-dupes
\`\`\`

#### \`install-strategy\`
#### \`legacy-bundling\`
#### \`global-style\`
#### \`strict-peer-deps\`
#### \`package-lock\`
#### \`omit\`
#### \`include\`
#### \`ignore-scripts\`
#### \`audit\`
#### \`bin-links\`
#### \`fund\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
#### \`install-links\`
`

exports[`test/lib/docs.js TAP usage fund > must match snapshot 1`] = `
Retrieve funding information

Usage:
npm fund [<package-spec>]

Options:
[--json] [--no-browser|--browser <browser>] [--unicode]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[--which <fundingSourceNumber>]

Run "npm help fund" for more info

\`\`\`bash
npm fund [<package-spec>]
\`\`\`

#### \`json\`
#### \`browser\`
#### \`unicode\`
#### \`workspace\`
#### \`which\`
`

exports[`test/lib/docs.js TAP usage get > must match snapshot 1`] = `
Get a value from the npm configuration

Usage:
npm get [<key> ...] (See \`npm config\`)

Options:
[-l|--long]

Run "npm help get" for more info

\`\`\`bash
npm get [<key> ...] (See \`npm config\`)
\`\`\`

Note: This command is unaware of workspaces.

#### \`long\`
`

exports[`test/lib/docs.js TAP usage help > must match snapshot 1`] = `
Get help on npm

Usage:
npm help <term> [<terms..>]

Options:
[--viewer <viewer>]

alias: hlep

Run "npm help help" for more info

\`\`\`bash
npm help <term> [<terms..>]

alias: hlep
\`\`\`

Note: This command is unaware of workspaces.

#### \`viewer\`
`

exports[`test/lib/docs.js TAP usage help-search > must match snapshot 1`] = `
Search npm help documentation

Usage:
npm help-search <text>

Options:
[-l|--long]

Run "npm help help-search" for more info

\`\`\`bash
npm help-search <text>
\`\`\`

Note: This command is unaware of workspaces.

#### \`long\`
`

exports[`test/lib/docs.js TAP usage hook > must match snapshot 1`] = `
Manage registry hooks

Usage:
npm hook add <pkg> <url> <secret> [--type=<type>]
npm hook ls [pkg]
npm hook rm <id>
npm hook update <id> <url> <secret>

Options:
[--registry <registry>] [--otp <otp>]

Run "npm help hook" for more info

\`\`\`bash
npm hook add <pkg> <url> <secret> [--type=<type>]
npm hook ls [pkg]
npm hook rm <id>
npm hook update <id> <url> <secret>
\`\`\`

Note: This command is unaware of workspaces.

#### \`registry\`
#### \`otp\`
`

exports[`test/lib/docs.js TAP usage init > must match snapshot 1`] = `
Create a package.json file

Usage:
npm init <package-spec> (same as \`npx create-<package-spec>\`)
npm init <@scope> (same as \`npx <@scope>/create\`)

Options:
[--init-author-name <name>] [--init-author-url <url>] [--init-license <license>]
[--init-module <module>] [--init-version <version>] [-y|--yes] [-f|--force]
[--scope <@scope>]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--no-workspaces-update] [--include-workspace-root]

aliases: create, innit

Run "npm help init" for more info

\`\`\`bash
npm init <package-spec> (same as \`npx create-<package-spec>\`)
npm init <@scope> (same as \`npx <@scope>/create\`)

aliases: create, innit
\`\`\`

#### \`init-author-name\`
#### \`init-author-url\`
#### \`init-license\`
#### \`init-module\`
#### \`init-version\`
#### \`yes\`
#### \`force\`
#### \`scope\`
#### \`workspace\`
#### \`workspaces\`
#### \`workspaces-update\`
#### \`include-workspace-root\`
`

exports[`test/lib/docs.js TAP usage install > must match snapshot 1`] = `
Install a package

Usage:
npm install [<package-spec> ...]

Options:
[-S|--save|--no-save|--save-prod|--save-dev|--save-optional|--save-peer|--save-bundle]
[-E|--save-exact] [-g|--global]
[--install-strategy <hoisted|nested|shallow|linked>] [--legacy-bundling]
[--global-style] [--omit <dev|optional|peer> [--omit <dev|optional|peer> ...]]
[--include <prod|dev|optional|peer> [--include <prod|dev|optional|peer> ...]]
[--strict-peer-deps] [--prefer-dedupe] [--no-package-lock] [--package-lock-only]
[--foreground-scripts] [--ignore-scripts] [--no-audit] [--no-bin-links]
[--no-fund] [--dry-run] [--cpu <cpu>] [--os <os>] [--libc <libc>]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root] [--install-links]

aliases: add, i, in, ins, inst, insta, instal, isnt, isnta, isntal, isntall

Run "npm help install" for more info

\`\`\`bash
npm install [<package-spec> ...]

aliases: add, i, in, ins, inst, insta, instal, isnt, isnta, isntal, isntall
\`\`\`

#### \`save\`
#### \`save-exact\`
#### \`global\`
#### \`install-strategy\`
#### \`legacy-bundling\`
#### \`global-style\`
#### \`omit\`
#### \`include\`
#### \`strict-peer-deps\`
#### \`prefer-dedupe\`
#### \`package-lock\`
#### \`package-lock-only\`
#### \`foreground-scripts\`
#### \`ignore-scripts\`
#### \`audit\`
#### \`bin-links\`
#### \`fund\`
#### \`dry-run\`
#### \`cpu\`
#### \`os\`
#### \`libc\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
#### \`install-links\`
`

exports[`test/lib/docs.js TAP usage install-ci-test > must match snapshot 1`] = `
Install a project with a clean slate and run tests

Usage:
npm install-ci-test

Options:
[--install-strategy <hoisted|nested|shallow|linked>] [--legacy-bundling]
[--global-style] [--omit <dev|optional|peer> [--omit <dev|optional|peer> ...]]
[--include <prod|dev|optional|peer> [--include <prod|dev|optional|peer> ...]]
[--strict-peer-deps] [--foreground-scripts] [--ignore-scripts] [--no-audit]
[--no-bin-links] [--no-fund] [--dry-run]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root] [--install-links]

aliases: cit, clean-install-test, sit

Run "npm help install-ci-test" for more info

\`\`\`bash
npm install-ci-test

aliases: cit, clean-install-test, sit
\`\`\`

#### \`install-strategy\`
#### \`legacy-bundling\`
#### \`global-style\`
#### \`omit\`
#### \`include\`
#### \`strict-peer-deps\`
#### \`foreground-scripts\`
#### \`ignore-scripts\`
#### \`audit\`
#### \`bin-links\`
#### \`fund\`
#### \`dry-run\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
#### \`install-links\`
`

exports[`test/lib/docs.js TAP usage install-test > must match snapshot 1`] = `
Install package(s) and run tests

Usage:
npm install-test [<package-spec> ...]

Options:
[-S|--save|--no-save|--save-prod|--save-dev|--save-optional|--save-peer|--save-bundle]
[-E|--save-exact] [-g|--global]
[--install-strategy <hoisted|nested|shallow|linked>] [--legacy-bundling]
[--global-style] [--omit <dev|optional|peer> [--omit <dev|optional|peer> ...]]
[--include <prod|dev|optional|peer> [--include <prod|dev|optional|peer> ...]]
[--strict-peer-deps] [--prefer-dedupe] [--no-package-lock] [--package-lock-only]
[--foreground-scripts] [--ignore-scripts] [--no-audit] [--no-bin-links]
[--no-fund] [--dry-run] [--cpu <cpu>] [--os <os>] [--libc <libc>]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root] [--install-links]

alias: it

Run "npm help install-test" for more info

\`\`\`bash
npm install-test [<package-spec> ...]

alias: it
\`\`\`

#### \`save\`
#### \`save-exact\`
#### \`global\`
#### \`install-strategy\`
#### \`legacy-bundling\`
#### \`global-style\`
#### \`omit\`
#### \`include\`
#### \`strict-peer-deps\`
#### \`prefer-dedupe\`
#### \`package-lock\`
#### \`package-lock-only\`
#### \`foreground-scripts\`
#### \`ignore-scripts\`
#### \`audit\`
#### \`bin-links\`
#### \`fund\`
#### \`dry-run\`
#### \`cpu\`
#### \`os\`
#### \`libc\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
#### \`install-links\`
`

exports[`test/lib/docs.js TAP usage link > must match snapshot 1`] = `
Symlink a package folder

Usage:
npm link [<package-spec>]

Options:
[-S|--save|--no-save|--save-prod|--save-dev|--save-optional|--save-peer|--save-bundle]
[-E|--save-exact] [-g|--global]
[--install-strategy <hoisted|nested|shallow|linked>] [--legacy-bundling]
[--global-style] [--strict-peer-deps] [--no-package-lock]
[--omit <dev|optional|peer> [--omit <dev|optional|peer> ...]]
[--include <prod|dev|optional|peer> [--include <prod|dev|optional|peer> ...]]
[--ignore-scripts] [--no-audit] [--no-bin-links] [--no-fund] [--dry-run]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root] [--install-links]

alias: ln

Run "npm help link" for more info

\`\`\`bash
npm link [<package-spec>]

alias: ln
\`\`\`

#### \`save\`
#### \`save-exact\`
#### \`global\`
#### \`install-strategy\`
#### \`legacy-bundling\`
#### \`global-style\`
#### \`strict-peer-deps\`
#### \`package-lock\`
#### \`omit\`
#### \`include\`
#### \`ignore-scripts\`
#### \`audit\`
#### \`bin-links\`
#### \`fund\`
#### \`dry-run\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
#### \`install-links\`
`

exports[`test/lib/docs.js TAP usage ll > must match snapshot 1`] = `
List installed packages

Usage:
npm ll [[<@scope>/]<pkg> ...]

Options:
[-a|--all] [--json] [-l|--long] [-p|--parseable] [-g|--global] [--depth <depth>]
[--omit <dev|optional|peer> [--omit <dev|optional|peer> ...]]
[--include <prod|dev|optional|peer> [--include <prod|dev|optional|peer> ...]]
[--link] [--package-lock-only] [--unicode]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root] [--install-links]

alias: la

Run "npm help ll" for more info

\`\`\`bash
npm ll [[<@scope>/]<pkg> ...]

alias: la
\`\`\`

#### \`all\`
#### \`json\`
#### \`long\`
#### \`parseable\`
#### \`global\`
#### \`depth\`
#### \`omit\`
#### \`include\`
#### \`link\`
#### \`package-lock-only\`
#### \`unicode\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
#### \`install-links\`
`

exports[`test/lib/docs.js TAP usage login > must match snapshot 1`] = `
Login to a registry user account

Usage:
npm login

Options:
[--registry <registry>] [--scope <@scope>] [--auth-type <legacy|web>]

Run "npm help login" for more info

\`\`\`bash
npm login
\`\`\`

Note: This command is unaware of workspaces.

#### \`registry\`
#### \`scope\`
#### \`auth-type\`
`

exports[`test/lib/docs.js TAP usage logout > must match snapshot 1`] = `
Log out of the registry

Usage:
npm logout

Options:
[--registry <registry>] [--scope <@scope>]

Run "npm help logout" for more info

\`\`\`bash
npm logout
\`\`\`

Note: This command is unaware of workspaces.

#### \`registry\`
#### \`scope\`
`

exports[`test/lib/docs.js TAP usage ls > must match snapshot 1`] = `
List installed packages

Usage:
npm ls <package-spec>

Options:
[-a|--all] [--json] [-l|--long] [-p|--parseable] [-g|--global] [--depth <depth>]
[--omit <dev|optional|peer> [--omit <dev|optional|peer> ...]]
[--include <prod|dev|optional|peer> [--include <prod|dev|optional|peer> ...]]
[--link] [--package-lock-only] [--unicode]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root] [--install-links]

alias: list

Run "npm help ls" for more info

\`\`\`bash
npm ls <package-spec>

alias: list
\`\`\`

#### \`all\`
#### \`json\`
#### \`long\`
#### \`parseable\`
#### \`global\`
#### \`depth\`
#### \`omit\`
#### \`include\`
#### \`link\`
#### \`package-lock-only\`
#### \`unicode\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
#### \`install-links\`
`

exports[`test/lib/docs.js TAP usage npm > must match snapshot 1`] = `
\`\`\`bash
npm
\`\`\`

Note: This command is unaware of workspaces.

NO PARAMS
`

exports[`test/lib/docs.js TAP usage npx > must match snapshot 1`] = `
\`\`\`bash
npx -- <pkg>[@<version>] [args...]
npx --package=<pkg>[@<version>] -- <cmd> [args...]
npx -c '<cmd> [args...]'
npx --package=foo -c '<cmd> [args...]'
\`\`\`

NO PARAMS
`

exports[`test/lib/docs.js TAP usage org > must match snapshot 1`] = `
Manage orgs

Usage:
npm org set orgname username [developer | admin | owner]
npm org rm orgname username
npm org ls orgname [<username>]

Options:
[--registry <registry>] [--otp <otp>] [--json] [-p|--parseable]

alias: ogr

Run "npm help org" for more info

\`\`\`bash
npm org set orgname username [developer | admin | owner]
npm org rm orgname username
npm org ls orgname [<username>]

alias: ogr
\`\`\`

Note: This command is unaware of workspaces.

#### \`registry\`
#### \`otp\`
#### \`json\`
#### \`parseable\`
`

exports[`test/lib/docs.js TAP usage outdated > must match snapshot 1`] = `
Check for outdated packages

Usage:
npm outdated [<package-spec> ...]

Options:
[-a|--all] [--json] [-l|--long] [-p|--parseable] [-g|--global]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]

Run "npm help outdated" for more info

\`\`\`bash
npm outdated [<package-spec> ...]
\`\`\`

#### \`all\`
#### \`json\`
#### \`long\`
#### \`parseable\`
#### \`global\`
#### \`workspace\`
`

exports[`test/lib/docs.js TAP usage owner > must match snapshot 1`] = `
Manage package owners

Usage:
npm owner add <user> <package-spec>
npm owner rm <user> <package-spec>
npm owner ls <package-spec>

Options:
[--registry <registry>] [--otp <otp>]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces]

alias: author

Run "npm help owner" for more info

\`\`\`bash
npm owner add <user> <package-spec>
npm owner rm <user> <package-spec>
npm owner ls <package-spec>

alias: author
\`\`\`

#### \`registry\`
#### \`otp\`
#### \`workspace\`
#### \`workspaces\`
`

exports[`test/lib/docs.js TAP usage pack > must match snapshot 1`] = `
Create a tarball from a package

Usage:
npm pack <package-spec>

Options:
[--dry-run] [--json] [--pack-destination <pack-destination>]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root]

Run "npm help pack" for more info

\`\`\`bash
npm pack <package-spec>
\`\`\`

#### \`dry-run\`
#### \`json\`
#### \`pack-destination\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
`

exports[`test/lib/docs.js TAP usage ping > must match snapshot 1`] = `
Ping npm registry

Usage:
npm ping

Options:
[--registry <registry>]

Run "npm help ping" for more info

\`\`\`bash
npm ping
\`\`\`

Note: This command is unaware of workspaces.

#### \`registry\`
`

exports[`test/lib/docs.js TAP usage pkg > must match snapshot 1`] = `
Manages your package.json

Usage:
npm pkg set <key>=<value> [<key>=<value> ...]
npm pkg get [<key> [<key> ...]]
npm pkg delete <key> [<key> ...]
npm pkg set [<array>[<index>].<key>=<value> ...]
npm pkg set [<array>[].<key>=<value> ...]
npm pkg fix

Options:
[-f|--force] [--json]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces]

Run "npm help pkg" for more info

\`\`\`bash
npm pkg set <key>=<value> [<key>=<value> ...]
npm pkg get [<key> [<key> ...]]
npm pkg delete <key> [<key> ...]
npm pkg set [<array>[<index>].<key>=<value> ...]
npm pkg set [<array>[].<key>=<value> ...]
npm pkg fix
\`\`\`

#### \`force\`
#### \`json\`
#### \`workspace\`
#### \`workspaces\`
`

exports[`test/lib/docs.js TAP usage prefix > must match snapshot 1`] = `
Display prefix

Usage:
npm prefix [-g]

Options:
[-g|--global]

Run "npm help prefix" for more info

\`\`\`bash
npm prefix [-g]
\`\`\`

Note: This command is unaware of workspaces.

#### \`global\`
`

exports[`test/lib/docs.js TAP usage profile > must match snapshot 1`] = `
Change settings on your registry profile

Usage:
npm profile enable-2fa [auth-only|auth-and-writes]
npm profile disable-2fa
npm profile get [<key>]
npm profile set <key> <value>

Options:
[--registry <registry>] [--json] [-p|--parseable] [--otp <otp>]

Run "npm help profile" for more info

\`\`\`bash
npm profile enable-2fa [auth-only|auth-and-writes]
npm profile disable-2fa
npm profile get [<key>]
npm profile set <key> <value>
\`\`\`

Note: This command is unaware of workspaces.

#### \`registry\`
#### \`json\`
#### \`parseable\`
#### \`otp\`
`

exports[`test/lib/docs.js TAP usage prune > must match snapshot 1`] = `
Remove extraneous packages

Usage:
npm prune [[<@scope>/]<pkg>...]

Options:
[--omit <dev|optional|peer> [--omit <dev|optional|peer> ...]]
[--include <prod|dev|optional|peer> [--include <prod|dev|optional|peer> ...]]
[--dry-run] [--json] [--foreground-scripts] [--ignore-scripts]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root] [--install-links]

Run "npm help prune" for more info

\`\`\`bash
npm prune [[<@scope>/]<pkg>...]
\`\`\`

#### \`omit\`
#### \`include\`
#### \`dry-run\`
#### \`json\`
#### \`foreground-scripts\`
#### \`ignore-scripts\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
#### \`install-links\`
`

exports[`test/lib/docs.js TAP usage publish > must match snapshot 1`] = `
Publish a package

Usage:
npm publish <package-spec>

Options:
[--tag <tag>] [--access <restricted|public>] [--dry-run] [--otp <otp>]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root]
[--provenance|--provenance-file <file>]

Run "npm help publish" for more info

\`\`\`bash
npm publish <package-spec>
\`\`\`

#### \`tag\`
#### \`access\`
#### \`dry-run\`
#### \`otp\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
#### \`provenance\`
#### \`provenance-file\`
`

exports[`test/lib/docs.js TAP usage query > must match snapshot 1`] = `
Retrieve a filtered list of packages

Usage:
npm query <selector>

Options:
[-g|--global]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root] [--package-lock-only]
[--expect-results|--expect-result-count <count>]

Run "npm help query" for more info

\`\`\`bash
npm query <selector>
\`\`\`

#### \`global\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
#### \`package-lock-only\`
#### \`expect-results\`
#### \`expect-result-count\`
`

exports[`test/lib/docs.js TAP usage rebuild > must match snapshot 1`] = `
Rebuild a package

Usage:
npm rebuild [<package-spec>] ...]

Options:
[-g|--global] [--no-bin-links] [--foreground-scripts] [--ignore-scripts]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root] [--install-links]

alias: rb

Run "npm help rebuild" for more info

\`\`\`bash
npm rebuild [<package-spec>] ...]

alias: rb
\`\`\`

#### \`global\`
#### \`bin-links\`
#### \`foreground-scripts\`
#### \`ignore-scripts\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
#### \`install-links\`
`

exports[`test/lib/docs.js TAP usage repo > must match snapshot 1`] = `
Open package repository page in the browser

Usage:
npm repo [<pkgname> [<pkgname> ...]]

Options:
[--no-browser|--browser <browser>] [--registry <registry>]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root]

Run "npm help repo" for more info

\`\`\`bash
npm repo [<pkgname> [<pkgname> ...]]
\`\`\`

#### \`browser\`
#### \`registry\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
`

exports[`test/lib/docs.js TAP usage restart > must match snapshot 1`] = `
Restart a package

Usage:
npm restart [-- <args>]

Options:
[--ignore-scripts] [--script-shell <script-shell>]

Run "npm help restart" for more info

\`\`\`bash
npm restart [-- <args>]
\`\`\`

#### \`ignore-scripts\`
#### \`script-shell\`
`

exports[`test/lib/docs.js TAP usage root > must match snapshot 1`] = `
Display npm root

Usage:
npm root

Options:
[-g|--global]

Run "npm help root" for more info

\`\`\`bash
npm root
\`\`\`

Note: This command is unaware of workspaces.

#### \`global\`
`

exports[`test/lib/docs.js TAP usage run-script > must match snapshot 1`] = `
Run arbitrary package scripts

Usage:
npm run-script <command> [-- <args>]

Options:
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root] [--if-present] [--ignore-scripts]
[--foreground-scripts] [--script-shell <script-shell>]

aliases: run, rum, urn

Run "npm help run-script" for more info

\`\`\`bash
npm run-script <command> [-- <args>]

aliases: run, rum, urn
\`\`\`

#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
#### \`if-present\`
#### \`ignore-scripts\`
#### \`foreground-scripts\`
#### \`script-shell\`
`

exports[`test/lib/docs.js TAP usage sbom > must match snapshot 1`] = `
Generate a Software Bill of Materials (SBOM)

Usage:
npm sbom

Options:
[--omit <dev|optional|peer> [--omit <dev|optional|peer> ...]]
[--package-lock-only] [--sbom-format <cyclonedx|spdx>]
[--sbom-type <library|application|framework>]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces]

Run "npm help sbom" for more info

\`\`\`bash
npm sbom
\`\`\`

#### \`omit\`
#### \`package-lock-only\`
#### \`sbom-format\`
#### \`sbom-type\`
#### \`workspace\`
#### \`workspaces\`
`

exports[`test/lib/docs.js TAP usage search > must match snapshot 1`] = `
Search for packages

Usage:
npm search <search term> [<search term> ...]

Options:
[--json] [--color|--no-color|--color always] [-p|--parseable] [--no-description]
[--searchlimit <number>] [--searchopts <searchopts>]
[--searchexclude <searchexclude>] [--registry <registry>] [--prefer-online]
[--prefer-offline] [--offline]

aliases: find, s, se

Run "npm help search" for more info

\`\`\`bash
npm search <search term> [<search term> ...]

aliases: find, s, se
\`\`\`

Note: This command is unaware of workspaces.

#### \`json\`
#### \`color\`
#### \`parseable\`
#### \`description\`
#### \`searchlimit\`
#### \`searchopts\`
#### \`searchexclude\`
#### \`registry\`
#### \`prefer-online\`
#### \`prefer-offline\`
#### \`offline\`
`

exports[`test/lib/docs.js TAP usage set > must match snapshot 1`] = `
Set a value in the npm configuration

Usage:
npm set <key>=<value> [<key>=<value> ...] (See \`npm config\`)

Options:
[-g|--global] [-L|--location <global|user|project>]

Run "npm help set" for more info

\`\`\`bash
npm set <key>=<value> [<key>=<value> ...] (See \`npm config\`)
\`\`\`

Note: This command is unaware of workspaces.

#### \`global\`
#### \`location\`
`

exports[`test/lib/docs.js TAP usage shrinkwrap > must match snapshot 1`] = `
Lock down dependency versions for publication

Usage:
npm shrinkwrap

Run "npm help shrinkwrap" for more info

\`\`\`bash
npm shrinkwrap
\`\`\`

Note: This command is unaware of workspaces.

NO PARAMS
`

exports[`test/lib/docs.js TAP usage star > must match snapshot 1`] = `
Mark your favorite packages

Usage:
npm star [<package-spec>...]

Options:
[--registry <registry>] [--unicode] [--otp <otp>]

Run "npm help star" for more info

\`\`\`bash
npm star [<package-spec>...]
\`\`\`

Note: This command is unaware of workspaces.

#### \`registry\`
#### \`unicode\`
#### \`otp\`
`

exports[`test/lib/docs.js TAP usage stars > must match snapshot 1`] = `
View packages marked as favorites

Usage:
npm stars [<user>]

Options:
[--registry <registry>]

Run "npm help stars" for more info

\`\`\`bash
npm stars [<user>]
\`\`\`

Note: This command is unaware of workspaces.

#### \`registry\`
`

exports[`test/lib/docs.js TAP usage start > must match snapshot 1`] = `
Start a package

Usage:
npm start [-- <args>]

Options:
[--ignore-scripts] [--script-shell <script-shell>]

Run "npm help start" for more info

\`\`\`bash
npm start [-- <args>]
\`\`\`

#### \`ignore-scripts\`
#### \`script-shell\`
`

exports[`test/lib/docs.js TAP usage stop > must match snapshot 1`] = `
Stop a package

Usage:
npm stop [-- <args>]

Options:
[--ignore-scripts] [--script-shell <script-shell>]

Run "npm help stop" for more info

\`\`\`bash
npm stop [-- <args>]
\`\`\`

#### \`ignore-scripts\`
#### \`script-shell\`
`

exports[`test/lib/docs.js TAP usage team > must match snapshot 1`] = `
Manage organization teams and team memberships

Usage:
npm team create <scope:team> [--otp <otpcode>]
npm team destroy <scope:team> [--otp <otpcode>]
npm team add <scope:team> <user> [--otp <otpcode>]
npm team rm <scope:team> <user> [--otp <otpcode>]
npm team ls <scope>|<scope:team>

Options:
[--registry <registry>] [--otp <otp>] [-p|--parseable] [--json]

Run "npm help team" for more info

\`\`\`bash
npm team create <scope:team> [--otp <otpcode>]
npm team destroy <scope:team> [--otp <otpcode>]
npm team add <scope:team> <user> [--otp <otpcode>]
npm team rm <scope:team> <user> [--otp <otpcode>]
npm team ls <scope>|<scope:team>
\`\`\`

Note: This command is unaware of workspaces.

#### \`registry\`
#### \`otp\`
#### \`parseable\`
#### \`json\`
`

exports[`test/lib/docs.js TAP usage test > must match snapshot 1`] = `
Test a package

Usage:
npm test [-- <args>]

Options:
[--ignore-scripts] [--script-shell <script-shell>]

aliases: tst, t

Run "npm help test" for more info

\`\`\`bash
npm test [-- <args>]

aliases: tst, t
\`\`\`

#### \`ignore-scripts\`
#### \`script-shell\`
`

exports[`test/lib/docs.js TAP usage token > must match snapshot 1`] = `
Manage your authentication tokens

Usage:
npm token list
npm token revoke <id|token>
npm token create [--read-only] [--cidr=list]

Options:
[--read-only] [--cidr <cidr> [--cidr <cidr> ...]] [--registry <registry>]
[--otp <otp>]

Run "npm help token" for more info

\`\`\`bash
npm token list
npm token revoke <id|token>
npm token create [--read-only] [--cidr=list]
\`\`\`

Note: This command is unaware of workspaces.

#### \`read-only\`
#### \`cidr\`
#### \`registry\`
#### \`otp\`
`

exports[`test/lib/docs.js TAP usage uninstall > must match snapshot 1`] = `
Remove a package

Usage:
npm uninstall [<@scope>/]<pkg>...

Options:
[-S|--save|--no-save|--save-prod|--save-dev|--save-optional|--save-peer|--save-bundle]
[-g|--global]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root] [--install-links]

aliases: unlink, remove, rm, r, un

Run "npm help uninstall" for more info

\`\`\`bash
npm uninstall [<@scope>/]<pkg>...

aliases: unlink, remove, rm, r, un
\`\`\`

#### \`save\`
#### \`global\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
#### \`install-links\`
`

exports[`test/lib/docs.js TAP usage unpublish > must match snapshot 1`] = `
Remove a package from the registry

Usage:
npm unpublish [<package-spec>]

Options:
[--dry-run] [-f|--force]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces]

Run "npm help unpublish" for more info

\`\`\`bash
npm unpublish [<package-spec>]
\`\`\`

#### \`dry-run\`
#### \`force\`
#### \`workspace\`
#### \`workspaces\`
`

exports[`test/lib/docs.js TAP usage unstar > must match snapshot 1`] = `
Remove an item from your favorite packages

Usage:
npm unstar [<package-spec>...]

Options:
[--registry <registry>] [--unicode] [--otp <otp>]

Run "npm help unstar" for more info

\`\`\`bash
npm unstar [<package-spec>...]
\`\`\`

Note: This command is unaware of workspaces.

#### \`registry\`
#### \`unicode\`
#### \`otp\`
`

exports[`test/lib/docs.js TAP usage update > must match snapshot 1`] = `
Update packages

Usage:
npm update [<pkg>...]

Options:
[-S|--save|--no-save|--save-prod|--save-dev|--save-optional|--save-peer|--save-bundle]
[-g|--global] [--install-strategy <hoisted|nested|shallow|linked>]
[--legacy-bundling] [--global-style]
[--omit <dev|optional|peer> [--omit <dev|optional|peer> ...]]
[--include <prod|dev|optional|peer> [--include <prod|dev|optional|peer> ...]]
[--strict-peer-deps] [--no-package-lock] [--foreground-scripts]
[--ignore-scripts] [--no-audit] [--no-bin-links] [--no-fund] [--dry-run]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root] [--install-links]

aliases: up, upgrade, udpate

Run "npm help update" for more info

\`\`\`bash
npm update [<pkg>...]

aliases: up, upgrade, udpate
\`\`\`

#### \`save\`
#### \`global\`
#### \`install-strategy\`
#### \`legacy-bundling\`
#### \`global-style\`
#### \`omit\`
#### \`include\`
#### \`strict-peer-deps\`
#### \`package-lock\`
#### \`foreground-scripts\`
#### \`ignore-scripts\`
#### \`audit\`
#### \`bin-links\`
#### \`fund\`
#### \`dry-run\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
#### \`install-links\`
`

exports[`test/lib/docs.js TAP usage version > must match snapshot 1`] = `
Bump a package version

Usage:
npm version [<newversion> | major | minor | patch | premajor | preminor | prepatch | prerelease | from-git]

Options:
[--allow-same-version] [--no-commit-hooks] [--no-git-tag-version] [--json]
[--preid prerelease-id] [--sign-git-tag]
[-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--no-workspaces-update] [--include-workspace-root]

alias: verison

Run "npm help version" for more info

\`\`\`bash
npm version [<newversion> | major | minor | patch | premajor | preminor | prepatch | prerelease | from-git]

alias: verison
\`\`\`

#### \`allow-same-version\`
#### \`commit-hooks\`
#### \`git-tag-version\`
#### \`json\`
#### \`preid\`
#### \`sign-git-tag\`
#### \`workspace\`
#### \`workspaces\`
#### \`workspaces-update\`
#### \`include-workspace-root\`
`

exports[`test/lib/docs.js TAP usage view > must match snapshot 1`] = `
View registry info

Usage:
npm view [<package-spec>] [<field>[.subfield]...]

Options:
[--json] [-w|--workspace <workspace-name> [-w|--workspace <workspace-name> ...]]
[-ws|--workspaces] [--include-workspace-root]

aliases: info, show, v

Run "npm help view" for more info

\`\`\`bash
npm view [<package-spec>] [<field>[.subfield]...]

aliases: info, show, v
\`\`\`

#### \`json\`
#### \`workspace\`
#### \`workspaces\`
#### \`include-workspace-root\`
`

exports[`test/lib/docs.js TAP usage whoami > must match snapshot 1`] = `
Display npm username

Usage:
npm whoami

Options:
[--registry <registry>]

Run "npm help whoami" for more info

\`\`\`bash
npm whoami
\`\`\`

Note: This command is unaware of workspaces.

#### \`registry\`
`

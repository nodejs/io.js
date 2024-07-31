const Definition = require('./definition.js')

const ciInfo = require('ci-info')
const querystring = require('node:querystring')
const { join } = require('node:path')

const isWindows = process.platform === 'win32'

// used by cafile flattening to flatOptions.ca
const { readFileSync } = require('node:fs')
const maybeReadFile = file => {
  try {
    return readFileSync(file, 'utf8')
  } catch (er) {
    if (er.code !== 'ENOENT') {
      throw er
    }
    return null
  }
}

const buildOmitList = obj => {
  const include = obj.include || []
  const omit = obj.omit || []

  const only = obj.only
  if (/^prod(uction)?$/.test(only) || obj.production) {
    omit.push('dev')
  } else if (obj.production === false) {
    include.push('dev')
  }

  if (/^dev/.test(obj.also)) {
    include.push('dev')
  }

  if (obj.dev) {
    include.push('dev')
  }

  if (obj.optional === false) {
    omit.push('optional')
  } else if (obj.optional === true) {
    include.push('optional')
  }

  obj.omit = [...new Set(omit)].filter(type => !include.includes(type))
  obj.include = [...new Set(include)]

  if (obj.omit.includes('dev')) {
    process.env.NODE_ENV = 'production'
  }

  return obj.omit
}

const editor = process.env.EDITOR ||
  process.env.VISUAL ||
  (isWindows ? `${process.env.SYSTEMROOT}\\notepad.exe` : 'vi')

const shell = isWindows ? process.env.ComSpec || 'cmd'
  : process.env.SHELL || 'sh'

const { networkInterfaces } = require('node:os')
const getLocalAddresses = () => {
  try {
    return Object.values(networkInterfaces()).map(
      int => int.map(({ address }) => address)
    ).reduce((set, addrs) => set.concat(addrs), [null])
  } catch (e) {
    return [null]
  }
}

const unicode = /UTF-?8$/i.test(
  process.env.LC_ALL ||
  process.env.LC_CTYPE ||
  process.env.LANG
)

// use LOCALAPPDATA on Windows, if set
// https://github.com/npm/cli/pull/899
const cacheRoot = (isWindows && process.env.LOCALAPPDATA) || '~'
const cacheExtra = isWindows ? 'npm-cache' : '.npm'
const cache = `${cacheRoot}/${cacheExtra}`

// TODO: refactor these type definitions so that they are less
// weird to pull out of the config module.
// TODO: use better type definition/validation API, nopt's is so weird.
const {
  semver: { type: Semver },
  Umask: { type: Umask },
  url: { type: url },
  path: { type: path },
} = require('../type-defs.js')

// basic flattening function, just copy it over camelCase
const flatten = (key, obj, flatOptions) => {
  const camel = key.replace(/-([a-z])/g, (_0, _1) => _1.toUpperCase())
  flatOptions[camel] = obj[key]
}

// TODO:
// Instead of having each definition provide a flatten method,
// provide the (?list of?) flat option field(s?) that it impacts.
// When that config is set, we mark the relevant flatOption fields
// dirty.  Then, a getter for that field defines how we actually
// set it.
//
// So, `save-dev`, `save-optional`, `save-prod`, et al would indicate
// that they affect the `saveType` flat option.  Then the config.flat
// object has a `get saveType () { ... }` that looks at the "real"
// config settings from files etc and returns the appropriate value.
//
// Getters will also (maybe?) give us a hook to audit flat option
// usage, so we can document and group these more appropriately.
//
// This will be a problem with cases where we currently do:
// const opts = { ...npm.flatOptions, foo: 'bar' }, but we can maybe
// instead do `npm.config.set('foo', 'bar')` prior to passing the
// config object down where it needs to go.
//
// This way, when we go hunting for "where does saveType come from anyway!?"
// while fixing some Arborist bug, we won't have to hunt through too
// many places.

// XXX: We should really deprecate all these `--save-blah` switches
// in favor of a single `--save-type` option.  The unfortunate shortcut
// we took for `--save-peer --save-optional` being `--save-type=peerOptional`
// makes this tricky, and likely a breaking change.

// Define all config keys we know about.  They are indexed by their own key for
// ease of lookup later.  This duplication is an optimization so that we don't
// have to do an extra function call just to "reuse" the key in both places.

const definitions = {
  _auth: new Definition('_auth', {
    default: null,
    type: [null, String],
    description: `
    A basic-auth string to use when authenticating against the npm registry.
    This will ONLY be used to authenticate against the npm registry.  For other
    registries you will need to scope it like "//other-registry.tld/:_auth"

    Warning: This should generally not be set via a command-line option.  It
    is safer to use a registry-provided authentication bearer token stored in
    the ~/.npmrc file by running \`npm login\`.
  `,
    flatten,
  }),
  access: new Definition('access', {
    default: null,
    defaultDescription: `
    'public' for new packages, existing packages it will not change the current level
  `,
    type: [null, 'restricted', 'public'],
    description: `
    If you do not want your scoped package to be publicly viewable (and
    installable) set \`--access=restricted\`.

    Unscoped packages can not be set to \`restricted\`.

    Note: This defaults to not changing the current access level for existing
    packages.  Specifying a value of \`restricted\` or \`public\` during
    publish will change the access for an existing package the same way that
    \`npm access set status\` would.
  `,
    flatten,
  }),
  all: new Definition('all', {
    default: false,
    type: Boolean,
    short: 'a',
    description: `
    When running \`npm outdated\` and \`npm ls\`, setting \`--all\` will show
    all outdated or installed packages, rather than only those directly
    depended upon by the current project.
  `,
    flatten,
  }),
  'allow-same-version': new Definition('allow-same-version', {
    default: false,
    type: Boolean,
    description: `
    Prevents throwing an error when \`npm version\` is used to set the new
    version to the same value as the current version.
  `,
    flatten,
  }),
  also: new Definition('also', {
    default: null,
    type: [null, 'dev', 'development'],
    description: `
      When set to \`dev\` or \`development\`, this is an alias for
      \`--include=dev\`.
    `,
    deprecated: 'Please use --include=dev instead.',
    flatten (key, obj, flatOptions) {
      definitions.omit.flatten('omit', obj, flatOptions)
    },
  }),
  audit: new Definition('audit', {
    default: true,
    type: Boolean,
    description: `
      When "true" submit audit reports alongside the current npm command to the
      default registry and all registries configured for scopes.  See the
      documentation for [\`npm audit\`](/commands/npm-audit) for details on what
      is submitted.
    `,
    flatten,
  }),
  'audit-level': new Definition('audit-level', {
    default: null,
    type: [null, 'info', 'low', 'moderate', 'high', 'critical', 'none'],
    description: `
    The minimum level of vulnerability for \`npm audit\` to exit with
    a non-zero exit code.
    `,
    flatten,
  }),
  'auth-type': new Definition('auth-type', {
    default: 'web',
    type: ['legacy', 'web'],
    description: `
      What authentication strategy to use with \`login\`.
      Note that if an \`otp\` config is given, this value will always be set to \`legacy\`.
    `,
    flatten,
  }),
  before: new Definition('before', {
    default: null,
    type: [null, Date],
    description: `
      If passed to \`npm install\`, will rebuild the npm tree such that only
      versions that were available **on or before** the \`--before\` time get
      installed.  If there's no versions available for the current set of
      direct dependencies, the command will error.

      If the requested version is a \`dist-tag\` and the given tag does not
      pass the \`--before\` filter, the most recent version less than or equal
      to that tag will be used. For example, \`foo@latest\` might install
      \`foo@1.2\` even though \`latest\` is \`2.0\`.
    `,
    flatten,
  }),
  'bin-links': new Definition('bin-links', {
    default: true,
    type: Boolean,
    description: `
      Tells npm to create symlinks (or \`.cmd\` shims on Windows) for package
      executables.

      Set to false to have it not do this.  This can be used to work around the
      fact that some file systems don't support symlinks, even on ostensibly
      Unix systems.
    `,
    flatten,
  }),
  browser: new Definition('browser', {
    default: null,
    defaultDescription: `
    OS X: \`"open"\`, Windows: \`"start"\`, Others: \`"xdg-open"\`
    `,
    type: [null, Boolean, String],
    description: `
    The browser that is called by npm commands to open websites.

    Set to \`false\` to suppress browser behavior and instead print urls to
    terminal.

    Set to \`true\` to use default system URL opener.
    `,
    flatten,
  }),
  ca: new Definition('ca', {
    default: null,
    type: [null, String, Array],
    description: `
    The Certificate Authority signing certificate that is trusted for SSL
    connections to the registry. Values should be in PEM format (Windows
      calls it "Base-64 encoded X.509 (.CER)") with newlines replaced by the
    string "\\n". For example:

    \`\`\`ini
    ca="-----BEGIN CERTIFICATE-----\\nXXXX\\nXXXX\\n-----END CERTIFICATE-----"
    \`\`\`

    Set to \`null\` to only allow "known" registrars, or to a specific CA
    cert to trust only that specific signing authority.

    Multiple CAs can be trusted by specifying an array of certificates:

    \`\`\`ini
    ca[]="..."
    ca[]="..."
    \`\`\`

    See also the \`strict-ssl\` config.
  `,
    flatten,
  }),
  cache: new Definition('cache', {
    default: cache,
    defaultDescription: `
      Windows: \`%LocalAppData%\\npm-cache\`, Posix: \`~/.npm\`
    `,
    type: path,
    description: `
      The location of npm's cache directory.
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.cache = join(obj.cache, '_cacache')
      flatOptions.npxCache = join(obj.cache, '_npx')
      flatOptions.tufCache = join(obj.cache, '_tuf')
    },
  }),
  'cache-max': new Definition('cache-max', {
    default: Infinity,
    type: Number,
    description: `
      \`--cache-max=0\` is an alias for \`--prefer-online\`
    `,
    deprecated: `
      This option has been deprecated in favor of \`--prefer-online\`
    `,
    flatten (key, obj, flatOptions) {
      if (obj[key] <= 0) {
        flatOptions.preferOnline = true
      }
    },
  }),
  'cache-min': new Definition('cache-min', {
    default: 0,
    type: Number,
    description: `
      \`--cache-min=9999 (or bigger)\` is an alias for \`--prefer-offline\`.
    `,
    deprecated: `
      This option has been deprecated in favor of \`--prefer-offline\`.
    `,
    flatten (key, obj, flatOptions) {
      if (obj[key] >= 9999) {
        flatOptions.preferOffline = true
      }
    },
  }),
  cafile: new Definition('cafile', {
    default: null,
    type: path,
    description: `
      A path to a file containing one or multiple Certificate Authority signing
      certificates. Similar to the \`ca\` setting, but allows for multiple
      CA's, as well as for the CA information to be stored in a file on disk.
    `,
    flatten (key, obj, flatOptions) {
      // always set to null in defaults
      if (!obj.cafile) {
        return
      }

      const raw = maybeReadFile(obj.cafile)
      if (!raw) {
        return
      }

      const delim = '-----END CERTIFICATE-----'
      flatOptions.ca = raw.replace(/\r\n/g, '\n').split(delim)
        .filter(section => section.trim())
        .map(section => section.trimLeft() + delim)
    },
  }),
  call: new Definition('call', {
    default: '',
    type: String,
    short: 'c',
    description: `
      Optional companion option for \`npm exec\`, \`npx\` that allows for
      specifying a custom command to be run along with the installed packages.

      \`\`\`bash
      npm exec --package yo --package generator-node --call "yo node"
      \`\`\`
    `,
    flatten,
  }),
  cert: new Definition('cert', {
    default: null,
    type: [null, String],
    description: `
      A client certificate to pass when accessing the registry.  Values should
      be in PEM format (Windows calls it "Base-64 encoded X.509 (.CER)") with
      newlines replaced by the string "\\n". For example:

      \`\`\`ini
      cert="-----BEGIN CERTIFICATE-----\\nXXXX\\nXXXX\\n-----END CERTIFICATE-----"
      \`\`\`

      It is _not_ the path to a certificate file, though you can set a registry-scoped
      "certfile" path like "//other-registry.tld/:certfile=/path/to/cert.pem".
    `,
    deprecated: `
      \`key\` and \`cert\` are no longer used for most registry operations.
      Use registry scoped \`keyfile\` and \`certfile\` instead.
      Example:
      //other-registry.tld/:keyfile=/path/to/key.pem
      //other-registry.tld/:certfile=/path/to/cert.crt
    `,
    flatten,
  }),
  cidr: new Definition('cidr', {
    default: null,
    type: [null, String, Array],
    description: `
      This is a list of CIDR address to be used when configuring limited access
      tokens with the \`npm token create\` command.
    `,
    flatten,
  }),
  // This should never be directly used, the flattened value is the derived value
  // and is sent to other modules, and is also exposed as `npm.color` for use
  // inside npm itself.
  color: new Definition('color', {
    default: !process.env.NO_COLOR || process.env.NO_COLOR === '0',
    usage: '--color|--no-color|--color always',
    defaultDescription: `
      true unless the NO_COLOR environ is set to something other than '0'
    `,
    type: ['always', Boolean],
    description: `
      If false, never shows colors.  If \`"always"\` then always shows colors.
      If true, then only prints color codes for tty file descriptors.
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.color = !obj.color ? false
        : obj.color === 'always' ? true
        : !!process.stdout.isTTY
      flatOptions.logColor = !obj.color ? false
        : obj.color === 'always' ? true
        : !!process.stderr.isTTY
    },
  }),
  'commit-hooks': new Definition('commit-hooks', {
    default: true,
    type: Boolean,
    description: `
      Run git commit hooks when using the \`npm version\` command.
    `,
    flatten,
  }),
  cpu: new Definition('cpu', {
    default: null,
    type: [null, String],
    description: `
      Override CPU architecture of native modules to install.
      Acceptable values are same as \`cpu\` field of package.json,
      which comes from \`process.arch\`.
    `,
    flatten,
  }),
  depth: new Definition('depth', {
    default: null,
    defaultDescription: `
      \`Infinity\` if \`--all\` is set, otherwise \`1\`
    `,
    type: [null, Number],
    description: `
      The depth to go when recursing packages for \`npm ls\`.

      If not set, \`npm ls\` will show only the immediate dependencies of the
      root project.  If \`--all\` is set, then npm will show all dependencies
      by default.
    `,
    flatten,
  }),
  description: new Definition('description', {
    default: true,
    type: Boolean,
    usage: '--no-description',
    description: `
      Show the description in \`npm search\`
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.search = flatOptions.search || { limit: 20 }
      flatOptions.search[key] = obj[key]
    },
  }),
  dev: new Definition('dev', {
    default: false,
    type: Boolean,
    description: `
      Alias for \`--include=dev\`.
    `,
    deprecated: 'Please use --include=dev instead.',
    flatten (key, obj, flatOptions) {
      definitions.omit.flatten('omit', obj, flatOptions)
    },
  }),
  diff: new Definition('diff', {
    default: [],
    hint: '<package-spec>',
    type: [String, Array],
    description: `
      Define arguments to compare in \`npm diff\`.
    `,
    flatten,
  }),
  'diff-ignore-all-space': new Definition('diff-ignore-all-space', {
    default: false,
    type: Boolean,
    description: `
      Ignore whitespace when comparing lines in \`npm diff\`.
    `,
    flatten,
  }),
  'diff-name-only': new Definition('diff-name-only', {
    default: false,
    type: Boolean,
    description: `
      Prints only filenames when using \`npm diff\`.
    `,
    flatten,
  }),
  'diff-no-prefix': new Definition('diff-no-prefix', {
    default: false,
    type: Boolean,
    description: `
      Do not show any source or destination prefix in \`npm diff\` output.

      Note: this causes \`npm diff\` to ignore the \`--diff-src-prefix\` and
      \`--diff-dst-prefix\` configs.
    `,
    flatten,
  }),
  'diff-dst-prefix': new Definition('diff-dst-prefix', {
    default: 'b/',
    hint: '<path>',
    type: String,
    description: `
      Destination prefix to be used in \`npm diff\` output.
    `,
    flatten,
  }),
  'diff-src-prefix': new Definition('diff-src-prefix', {
    default: 'a/',
    hint: '<path>',
    type: String,
    description: `
      Source prefix to be used in \`npm diff\` output.
    `,
    flatten,
  }),
  'diff-text': new Definition('diff-text', {
    default: false,
    type: Boolean,
    description: `
      Treat all files as text in \`npm diff\`.
    `,
    flatten,
  }),
  'diff-unified': new Definition('diff-unified', {
    default: 3,
    type: Number,
    description: `
      The number of lines of context to print in \`npm diff\`.
    `,
    flatten,
  }),
  'dry-run': new Definition('dry-run', {
    default: false,
    type: Boolean,
    description: `
      Indicates that you don't want npm to make any changes and that it should
      only report what it would have done.  This can be passed into any of the
      commands that modify your local installation, eg, \`install\`,
      \`update\`, \`dedupe\`, \`uninstall\`, as well as \`pack\` and
      \`publish\`.

      Note: This is NOT honored by other network related commands, eg
      \`dist-tags\`, \`owner\`, etc.
    `,
    flatten,
  }),
  editor: new Definition('editor', {
    default: editor,
    defaultDescription: `
      The EDITOR or VISUAL environment variables, or '%SYSTEMROOT%\\notepad.exe' on Windows,
      or 'vi' on Unix systems
    `,
    type: String,
    description: `
      The command to run for \`npm edit\` and \`npm config edit\`.
    `,
    flatten,
  }),
  'engine-strict': new Definition('engine-strict', {
    default: false,
    type: Boolean,
    description: `
      If set to true, then npm will stubbornly refuse to install (or even
      consider installing) any package that claims to not be compatible with
      the current Node.js version.

      This can be overridden by setting the \`--force\` flag.
    `,
    flatten,
  }),
  'expect-result-count': new Definition('expect-result-count', {
    default: null,
    type: [null, Number],
    hint: '<count>',
    exclusive: ['expect-results'],
    description: `
      Tells to expect a specific number of results from the command.
    `,
  }),
  'expect-results': new Definition('expect-results', {
    default: null,
    type: [null, Boolean],
    exclusive: ['expect-result-count'],
    description: `
      Tells npm whether or not to expect results from the command.
      Can be either true (expect some results) or false (expect no results).
    `,
  }),
  'fetch-retries': new Definition('fetch-retries', {
    default: 2,
    type: Number,
    description: `
      The "retries" config for the \`retry\` module to use when fetching
      packages from the registry.

      npm will retry idempotent read requests to the registry in the case
      of network failures or 5xx HTTP errors.
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.retry = flatOptions.retry || {}
      flatOptions.retry.retries = obj[key]
    },
  }),
  'fetch-retry-factor': new Definition('fetch-retry-factor', {
    default: 10,
    type: Number,
    description: `
      The "factor" config for the \`retry\` module to use when fetching
      packages.
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.retry = flatOptions.retry || {}
      flatOptions.retry.factor = obj[key]
    },
  }),
  'fetch-retry-maxtimeout': new Definition('fetch-retry-maxtimeout', {
    default: 60000,
    defaultDescription: '60000 (1 minute)',
    type: Number,
    description: `
      The "maxTimeout" config for the \`retry\` module to use when fetching
      packages.
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.retry = flatOptions.retry || {}
      flatOptions.retry.maxTimeout = obj[key]
    },
  }),
  'fetch-retry-mintimeout': new Definition('fetch-retry-mintimeout', {
    default: 10000,
    defaultDescription: '10000 (10 seconds)',
    type: Number,
    description: `
      The "minTimeout" config for the \`retry\` module to use when fetching
      packages.
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.retry = flatOptions.retry || {}
      flatOptions.retry.minTimeout = obj[key]
    },
  }),
  'fetch-timeout': new Definition('fetch-timeout', {
    default: 5 * 60 * 1000,
    defaultDescription: `${5 * 60 * 1000} (5 minutes)`,
    type: Number,
    description: `
      The maximum amount of time to wait for HTTP requests to complete.
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.timeout = obj[key]
    },
  }),
  force: new Definition('force', {
    default: false,
    type: Boolean,
    short: 'f',
    description: `
      Removes various protections against unfortunate side effects, common
      mistakes, unnecessary performance degradation, and malicious input.

      * Allow clobbering non-npm files in global installs.
      * Allow the \`npm version\` command to work on an unclean git repository.
      * Allow deleting the cache folder with \`npm cache clean\`.
      * Allow installing packages that have an \`engines\` declaration
        requiring a different version of npm.
      * Allow installing packages that have an \`engines\` declaration
        requiring a different version of \`node\`, even if \`--engine-strict\`
        is enabled.
      * Allow \`npm audit fix\` to install modules outside your stated
        dependency range (including SemVer-major changes).
      * Allow unpublishing all versions of a published package.
      * Allow conflicting peerDependencies to be installed in the root project.
      * Implicitly set \`--yes\` during \`npm init\`.
      * Allow clobbering existing values in \`npm pkg\`
      * Allow unpublishing of entire packages (not just a single version).

      If you don't have a clear idea of what you want to do, it is strongly
      recommended that you do not use this option!
    `,
    flatten,
  }),
  'foreground-scripts': new Definition('foreground-scripts', {
    default: false,
    defaultDescription: `\`false\` unless when using \`npm pack\` or \`npm publish\` where it
    defaults to \`true\``,
    type: Boolean,
    description: `
      Run all build scripts (ie, \`preinstall\`, \`install\`, and
      \`postinstall\`) scripts for installed packages in the foreground
      process, sharing standard input, output, and error with the main npm
      process.

      Note that this will generally make installs run slower, and be much
      noisier, but can be useful for debugging.
    `,
    flatten,
  }),
  'format-package-lock': new Definition('format-package-lock', {
    default: true,
    type: Boolean,
    description: `
      Format \`package-lock.json\` or \`npm-shrinkwrap.json\` as a human
      readable file.
    `,
    flatten,
  }),
  fund: new Definition('fund', {
    default: true,
    type: Boolean,
    description: `
      When "true" displays the message at the end of each \`npm install\`
      acknowledging the number of dependencies looking for funding.
      See [\`npm fund\`](/commands/npm-fund) for details.
    `,
    flatten,
  }),
  git: new Definition('git', {
    default: 'git',
    type: String,
    description: `
      The command to use for git commands.  If git is installed on the
      computer, but is not in the \`PATH\`, then set this to the full path to
      the git binary.
    `,
    flatten,
  }),
  'git-tag-version': new Definition('git-tag-version', {
    default: true,
    type: Boolean,
    description: `
      Tag the commit when using the \`npm version\` command.  Setting this to
      false results in no commit being made at all.
    `,
    flatten,
  }),
  global: new Definition('global', {
    default: false,
    type: Boolean,
    short: 'g',
    description: `
      Operates in "global" mode, so that packages are installed into the
      \`prefix\` folder instead of the current working directory.  See
      [folders](/configuring-npm/folders) for more on the differences in
      behavior.

      * packages are installed into the \`{prefix}/lib/node_modules\` folder,
        instead of the current working directory.
      * bin files are linked to \`{prefix}/bin\`
      * man pages are linked to \`{prefix}/share/man\`
    `,
    flatten: (key, obj, flatOptions) => {
      flatten(key, obj, flatOptions)
      if (flatOptions.global) {
        flatOptions.location = 'global'
      }
    },
  }),
  // the globalconfig has its default defined outside of this module
  globalconfig: new Definition('globalconfig', {
    type: path,
    default: '',
    defaultDescription: `
      The global --prefix setting plus 'etc/npmrc'. For example,
      '/usr/local/etc/npmrc'
    `,
    description: `
      The config file to read for global config options.
    `,
    flatten,
  }),
  'global-style': new Definition('global-style', {
    default: false,
    type: Boolean,
    description: `
      Only install direct dependencies in the top level \`node_modules\`,
      but hoist on deeper dependencies.
      Sets \`--install-strategy=shallow\`.
    `,
    deprecated: `
      This option has been deprecated in favor of \`--install-strategy=shallow\`
    `,
    flatten (key, obj, flatOptions) {
      if (obj[key]) {
        obj['install-strategy'] = 'shallow'
        flatOptions.installStrategy = 'shallow'
      }
    },
  }),
  heading: new Definition('heading', {
    default: 'npm',
    type: String,
    description: `
      The string that starts all the debugging log output.
    `,
    flatten,
  }),
  'https-proxy': new Definition('https-proxy', {
    default: null,
    type: [null, url],
    description: `
      A proxy to use for outgoing https requests. If the \`HTTPS_PROXY\` or
      \`https_proxy\` or \`HTTP_PROXY\` or \`http_proxy\` environment variables
      are set, proxy settings will be honored by the underlying
      \`make-fetch-happen\` library.
    `,
    flatten,
  }),
  'if-present': new Definition('if-present', {
    default: false,
    type: Boolean,
    envExport: false,
    description: `
      If true, npm will not exit with an error code when \`run-script\` is
      invoked for a script that isn't defined in the \`scripts\` section of
      \`package.json\`. This option can be used when it's desirable to
      optionally run a script when it's present and fail if the script fails.
      This is useful, for example, when running scripts that may only apply for
      some builds in an otherwise generic CI setup.
    `,
    flatten,
  }),
  'ignore-scripts': new Definition('ignore-scripts', {
    default: false,
    type: Boolean,
    description: `
      If true, npm does not run scripts specified in package.json files.

      Note that commands explicitly intended to run a particular script, such
      as \`npm start\`, \`npm stop\`, \`npm restart\`, \`npm test\`, and \`npm
      run-script\` will still run their intended script if \`ignore-scripts\` is
      set, but they will *not* run any pre- or post-scripts.
    `,
    flatten,
  }),
  include: new Definition('include', {
    default: [],
    type: [Array, 'prod', 'dev', 'optional', 'peer'],
    description: `
      Option that allows for defining which types of dependencies to install.

      This is the inverse of \`--omit=<type>\`.

      Dependency types specified in \`--include\` will not be omitted,
      regardless of the order in which omit/include are specified on the
      command-line.
    `,
    flatten (key, obj, flatOptions) {
      // just call the omit flattener, it reads from obj.include
      definitions.omit.flatten('omit', obj, flatOptions)
    },
  }),
  'include-staged': new Definition('include-staged', {
    default: false,
    type: Boolean,
    description: `
      Allow installing "staged" published packages, as defined by [npm RFC PR
      #92](https://github.com/npm/rfcs/pull/92).

      This is experimental, and not implemented by the npm public registry.
    `,
    flatten,
  }),
  'include-workspace-root': new Definition('include-workspace-root', {
    default: false,
    type: Boolean,
    envExport: false,
    description: `
      Include the workspace root when workspaces are enabled for a command.

      When false, specifying individual workspaces via the \`workspace\` config,
      or all workspaces via the \`workspaces\` flag, will cause npm to operate only
      on the specified workspaces, and not on the root project.
    `,
    flatten,
  }),
  'init-author-email': new Definition('init-author-email', {
    default: '',
    hint: '<email>',
    type: String,
    description: `
      The value \`npm init\` should use by default for the package author's
      email.
    `,
  }),
  'init-author-name': new Definition('init-author-name', {
    default: '',
    hint: '<name>',
    type: String,
    description: `
      The value \`npm init\` should use by default for the package author's name.
    `,
  }),
  'init-author-url': new Definition('init-author-url', {
    default: '',
    type: ['', url],
    hint: '<url>',
    description: `
      The value \`npm init\` should use by default for the package author's homepage.
    `,
  }),
  'init-license': new Definition('init-license', {
    default: 'ISC',
    hint: '<license>',
    type: String,
    description: `
      The value \`npm init\` should use by default for the package license.
    `,
  }),
  'init-module': new Definition('init-module', {
    default: '~/.npm-init.js',
    type: path,
    hint: '<module>',
    description: `
      A module that will be loaded by the \`npm init\` command.  See the
      documentation for the
      [init-package-json](https://github.com/npm/init-package-json) module for
      more information, or [npm init](/commands/npm-init).
    `,
  }),
  'init-version': new Definition('init-version', {
    default: '1.0.0',
    type: Semver,
    hint: '<version>',
    description: `
      The value that \`npm init\` should use by default for the package
      version number, if not already set in package.json.
    `,
  }),
  // these "aliases" are historically supported in .npmrc files, unfortunately
  // They should be removed in a future npm version.
  'init.author.email': new Definition('init.author.email', {
    default: '',
    type: String,
    deprecated: `
      Use \`--init-author-email\` instead.`,
    description: `
      Alias for \`--init-author-email\`
    `,
  }),
  'init.author.name': new Definition('init.author.name', {
    default: '',
    type: String,
    deprecated: `
      Use \`--init-author-name\` instead.
    `,
    description: `
      Alias for \`--init-author-name\`
    `,
  }),
  'init.author.url': new Definition('init.author.url', {
    default: '',
    type: ['', url],
    deprecated: `
      Use \`--init-author-url\` instead.
    `,
    description: `
      Alias for \`--init-author-url\`
    `,
  }),
  'init.license': new Definition('init.license', {
    default: 'ISC',
    type: String,
    deprecated: `
      Use \`--init-license\` instead.
    `,
    description: `
      Alias for \`--init-license\`
    `,
  }),
  'init.module': new Definition('init.module', {
    default: '~/.npm-init.js',
    type: path,
    deprecated: `
      Use \`--init-module\` instead.
    `,
    description: `
      Alias for \`--init-module\`
    `,
  }),
  'init.version': new Definition('init.version', {
    default: '1.0.0',
    type: Semver,
    deprecated: `
      Use \`--init-version\` instead.
    `,
    description: `
      Alias for \`--init-version\`
    `,
  }),
  'install-links': new Definition('install-links', {
    default: false,
    type: Boolean,
    description: `
      When set file: protocol dependencies will be packed and installed as
      regular dependencies instead of creating a symlink. This option has
      no effect on workspaces.
    `,
    flatten,
  }),
  'install-strategy': new Definition('install-strategy', {
    default: 'hoisted',
    type: ['hoisted', 'nested', 'shallow', 'linked'],
    description: `
      Sets the strategy for installing packages in node_modules.
      hoisted (default): Install non-duplicated in top-level, and duplicated as
        necessary within directory structure.
      nested: (formerly --legacy-bundling) install in place, no hoisting.
      shallow (formerly --global-style) only install direct deps at top-level.
      linked: (experimental) install in node_modules/.store, link in place,
        unhoisted.
    `,
    flatten,
  }),
  json: new Definition('json', {
    default: false,
    type: Boolean,
    description: `
      Whether or not to output JSON data, rather than the normal output.

      * In \`npm pkg set\` it enables parsing set values with JSON.parse()
      before saving them to your \`package.json\`.

      Not supported by all npm commands.
    `,
    flatten,
  }),
  key: new Definition('key', {
    default: null,
    type: [null, String],
    description: `
      A client key to pass when accessing the registry.  Values should be in
      PEM format with newlines replaced by the string "\\n". For example:

      \`\`\`ini
      key="-----BEGIN PRIVATE KEY-----\\nXXXX\\nXXXX\\n-----END PRIVATE KEY-----"
      \`\`\`

      It is _not_ the path to a key file, though you can set a registry-scoped
      "keyfile" path like "//other-registry.tld/:keyfile=/path/to/key.pem".
    `,
    deprecated: `
      \`key\` and \`cert\` are no longer used for most registry operations.
      Use registry scoped \`keyfile\` and \`certfile\` instead.
      Example:
      //other-registry.tld/:keyfile=/path/to/key.pem
      //other-registry.tld/:certfile=/path/to/cert.crt
    `,
    flatten,
  }),
  'legacy-bundling': new Definition('legacy-bundling', {
    default: false,
    type: Boolean,
    description: `
      Instead of hoisting package installs in \`node_modules\`, install packages
      in the same manner that they are depended on. This may cause very deep
      directory structures and duplicate package installs as there is no
      de-duplicating.
      Sets \`--install-strategy=nested\`.
    `,
    deprecated: `
      This option has been deprecated in favor of \`--install-strategy=nested\`
    `,
    flatten (key, obj, flatOptions) {
      if (obj[key]) {
        obj['install-strategy'] = 'nested'
        flatOptions.installStrategy = 'nested'
      }
    },
  }),
  'legacy-peer-deps': new Definition('legacy-peer-deps', {
    default: false,
    type: Boolean,
    description: `
      Causes npm to completely ignore \`peerDependencies\` when building a
      package tree, as in npm versions 3 through 6.

      If a package cannot be installed because of overly strict
      \`peerDependencies\` that collide, it provides a way to move forward
      resolving the situation.

      This differs from \`--omit=peer\`, in that \`--omit=peer\` will avoid
      unpacking \`peerDependencies\` on disk, but will still design a tree such
      that \`peerDependencies\` _could_ be unpacked in a correct place.

      Use of \`legacy-peer-deps\` is not recommended, as it will not enforce
      the \`peerDependencies\` contract that meta-dependencies may rely on.
    `,
    flatten,
  }),
  libc: new Definition('libc', {
    default: null,
    type: [null, String],
    description: `
      Override libc of native modules to install.
      Acceptable values are same as \`libc\` field of package.json
    `,
    flatten,
  }),
  link: new Definition('link', {
    default: false,
    type: Boolean,
    description: `
      Used with \`npm ls\`, limiting output to only those packages that are
      linked.
    `,
  }),
  'local-address': new Definition('local-address', {
    default: null,
    type: getLocalAddresses(),
    typeDescription: 'IP Address',
    description: `
      The IP address of the local interface to use when making connections to
      the npm registry.  Must be IPv4 in versions of Node prior to 0.12.
    `,
    flatten,
  }),
  location: new Definition('location', {
    default: 'user',
    short: 'L',
    type: [
      'global',
      'user',
      'project',
    ],
    defaultDescription: `
      "user" unless \`--global\` is passed, which will also set this value to "global"
    `,
    description: `
      When passed to \`npm config\` this refers to which config file to use.

      When set to "global" mode, packages are installed into the \`prefix\` folder
      instead of the current working directory. See
      [folders](/configuring-npm/folders) for more on the differences in behavior.

      * packages are installed into the \`{prefix}/lib/node_modules\` folder,
        instead of the current working directory.
      * bin files are linked to \`{prefix}/bin\`
      * man pages are linked to \`{prefix}/share/man\`
    `,
    flatten: (key, obj, flatOptions) => {
      flatten(key, obj, flatOptions)
      if (flatOptions.global) {
        flatOptions.location = 'global'
      }
      if (obj.location === 'global') {
        flatOptions.global = true
      }
    },
  }),
  'lockfile-version': new Definition('lockfile-version', {
    default: null,
    type: [null, 1, 2, 3, '1', '2', '3'],
    defaultDescription: `
      Version 3 if no lockfile, auto-converting v1 lockfiles to v3, otherwise
      maintain current lockfile version.`,
    description: `
      Set the lockfile format version to be used in package-lock.json and
      npm-shrinkwrap-json files.  Possible options are:

      1: The lockfile version used by npm versions 5 and 6.  Lacks some data that
      is used during the install, resulting in slower and possibly less
      deterministic installs.  Prevents lockfile churn when interoperating with
      older npm versions.

      2: The default lockfile version used by npm version 7 and 8.  Includes both
      the version 1 lockfile data and version 3 lockfile data, for maximum
      determinism and interoperability, at the expense of more bytes on disk.

      3: Only the new lockfile information introduced in npm version 7.  Smaller
      on disk than lockfile version 2, but not interoperable with older npm
      versions.  Ideal if all users are on npm version 7 and higher.
    `,
    flatten: (key, obj, flatOptions) => {
      flatOptions.lockfileVersion = obj[key] && parseInt(obj[key], 10)
    },
  }),
  loglevel: new Definition('loglevel', {
    default: 'notice',
    type: [
      'silent',
      'error',
      'warn',
      'notice',
      'http',
      'info',
      'verbose',
      'silly',
    ],
    description: `
      What level of logs to report.  All logs are written to a debug log,
      with the path to that file printed if the execution of a command fails.

      Any logs of a higher level than the setting are shown. The default is
      "notice".

      See also the \`foreground-scripts\` config.
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.silent = obj[key] === 'silent'
    },
  }),
  'logs-dir': new Definition('logs-dir', {
    default: null,
    type: [null, path],
    defaultDescription: `
      A directory named \`_logs\` inside the cache
  `,
    description: `
      The location of npm's log directory.  See [\`npm
      logging\`](/using-npm/logging) for more information.
    `,
  }),
  'logs-max': new Definition('logs-max', {
    default: 10,
    type: Number,
    description: `
      The maximum number of log files to store.

      If set to 0, no log files will be written for the current run.
    `,
  }),
  long: new Definition('long', {
    default: false,
    type: Boolean,
    short: 'l',
    description: `
      Show extended information in \`ls\`, \`search\`, and \`help-search\`.
    `,
  }),
  maxsockets: new Definition('maxsockets', {
    default: 15,
    type: Number,
    description: `
      The maximum number of connections to use per origin (protocol/host/port
      combination).
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.maxSockets = obj[key]
    },
  }),
  message: new Definition('message', {
    default: '%s',
    type: String,
    short: 'm',
    description: `
      Commit message which is used by \`npm version\` when creating version commit.

      Any "%s" in the message will be replaced with the version number.
    `,
    flatten,
  }),
  'node-options': new Definition('node-options', {
    default: null,
    type: [null, String],
    description: `
      Options to pass through to Node.js via the \`NODE_OPTIONS\` environment
      variable.  This does not impact how npm itself is executed but it does
      impact how lifecycle scripts are called.
    `,
  }),
  noproxy: new Definition('noproxy', {
    default: '',
    defaultDescription: `
      The value of the NO_PROXY environment variable
    `,
    type: [String, Array],
    description: `
      Domain extensions that should bypass any proxies.

      Also accepts a comma-delimited string.
    `,
    flatten (key, obj, flatOptions) {
      if (Array.isArray(obj[key])) {
        flatOptions.noProxy = obj[key].join(',')
      } else {
        flatOptions.noProxy = obj[key]
      }
    },
  }),
  offline: new Definition('offline', {
    default: false,
    type: Boolean,
    description: `
      Force offline mode: no network requests will be done during install. To allow
      the CLI to fill in missing cache data, see \`--prefer-offline\`.
    `,
    flatten,
  }),
  omit: new Definition('omit', {
    default: process.env.NODE_ENV === 'production' ? ['dev'] : [],
    defaultDescription: `
      'dev' if the \`NODE_ENV\` environment variable is set to 'production',
      otherwise empty.
    `,
    type: [Array, 'dev', 'optional', 'peer'],
    description: `
      Dependency types to omit from the installation tree on disk.

      Note that these dependencies _are_ still resolved and added to the
      \`package-lock.json\` or \`npm-shrinkwrap.json\` file.  They are just
      not physically installed on disk.

      If a package type appears in both the \`--include\` and \`--omit\`
      lists, then it will be included.

      If the resulting omit list includes \`'dev'\`, then the \`NODE_ENV\`
      environment variable will be set to \`'production'\` for all lifecycle
      scripts.
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.omit = buildOmitList(obj)
    },
  }),
  'omit-lockfile-registry-resolved': new Definition('omit-lockfile-registry-resolved', {
    default: false,
    type: Boolean,
    description: `
      This option causes npm to create lock files without a \`resolved\` key for
      registry dependencies. Subsequent installs will need to resolve tarball
      endpoints with the configured registry, likely resulting in a longer install
      time.
    `,
    flatten,
  }),
  only: new Definition('only', {
    default: null,
    type: [null, 'prod', 'production'],
    deprecated: `
      Use \`--omit=dev\` to omit dev dependencies from the install.
    `,
    description: `
      When set to \`prod\` or \`production\`, this is an alias for
      \`--omit=dev\`.
    `,
    flatten (key, obj, flatOptions) {
      definitions.omit.flatten('omit', obj, flatOptions)
    },
  }),
  optional: new Definition('optional', {
    default: null,
    type: [null, Boolean],
    deprecated: `
      Use \`--omit=optional\` to exclude optional dependencies, or
      \`--include=optional\` to include them.

      Default value does install optional deps unless otherwise omitted.
    `,
    description: `
      Alias for --include=optional or --omit=optional
    `,
    flatten (key, obj, flatOptions) {
      definitions.omit.flatten('omit', obj, flatOptions)
    },
  }),
  os: new Definition('os', {
    default: null,
    type: [null, String],
    description: `
      Override OS of native modules to install.
      Acceptable values are same as \`os\` field of package.json,
      which comes from \`process.platform\`.
    `,
    flatten,
  }),
  otp: new Definition('otp', {
    default: null,
    type: [null, String],
    description: `
      This is a one-time password from a two-factor authenticator.  It's needed
      when publishing or changing package permissions with \`npm access\`.

      If not set, and a registry response fails with a challenge for a one-time
      password, npm will prompt on the command line for one.
    `,
    flatten (key, obj, flatOptions) {
      flatten(key, obj, flatOptions)
      if (obj.otp) {
        obj['auth-type'] = 'legacy'
        flatten('auth-type', obj, flatOptions)
      }
    },
  }),
  package: new Definition('package', {
    default: [],
    hint: '<package-spec>',
    type: [String, Array],
    description: `
      The package or packages to install for [\`npm exec\`](/commands/npm-exec)
    `,
    flatten,
  }),
  'package-lock': new Definition('package-lock', {
    default: true,
    type: Boolean,
    description: `
      If set to false, then ignore \`package-lock.json\` files when installing.
      This will also prevent _writing_ \`package-lock.json\` if \`save\` is
      true.
    `,
    flatten: (key, obj, flatOptions) => {
      flatten(key, obj, flatOptions)
      if (flatOptions.packageLockOnly) {
        flatOptions.packageLock = true
      }
    },
  }),
  'package-lock-only': new Definition('package-lock-only', {
    default: false,
    type: Boolean,
    description: `
      If set to true, the current operation will only use the \`package-lock.json\`,
      ignoring \`node_modules\`.

      For \`update\` this means only the \`package-lock.json\` will be updated,
      instead of checking \`node_modules\` and downloading dependencies.

      For \`list\` this means the output will be based on the tree described by the
      \`package-lock.json\`, rather than the contents of \`node_modules\`.
    `,
    flatten: (key, obj, flatOptions) => {
      flatten(key, obj, flatOptions)
      if (flatOptions.packageLockOnly) {
        flatOptions.packageLock = true
      }
    },
  }),
  'pack-destination': new Definition('pack-destination', {
    default: '.',
    type: String,
    description: `
      Directory in which \`npm pack\` will save tarballs.
    `,
    flatten,
  }),
  parseable: new Definition('parseable', {
    default: false,
    type: Boolean,
    short: 'p',
    description: `
      Output parseable results from commands that write to standard output. For
      \`npm search\`, this will be tab-separated table format.
    `,
    flatten,
  }),
  'prefer-dedupe': new Definition('prefer-dedupe', {
    default: false,
    type: Boolean,
    description: `
      Prefer to deduplicate packages if possible, rather than
      choosing a newer version of a dependency.
    `,
    flatten,
  }),
  'prefer-offline': new Definition('prefer-offline', {
    default: false,
    type: Boolean,
    description: `
      If true, staleness checks for cached data will be bypassed, but missing
      data will be requested from the server. To force full offline mode, use
      \`--offline\`.
    `,
    flatten,
  }),
  'prefer-online': new Definition('prefer-online', {
    default: false,
    type: Boolean,
    description: `
      If true, staleness checks for cached data will be forced, making the CLI
      look for updates immediately even for fresh package data.
    `,
    flatten,
  }),
  // `prefix` has its default defined outside of this module
  prefix: new Definition('prefix', {
    type: path,
    short: 'C',
    default: '',
    defaultDescription: `
      In global mode, the folder where the node executable is installed.
      Otherwise, the nearest parent folder containing either a package.json
      file or a node_modules folder.
    `,
    description: `
      The location to install global items.  If set on the command line, then
      it forces non-global commands to run in the specified folder.
    `,
  }),
  preid: new Definition('preid', {
    default: '',
    hint: 'prerelease-id',
    type: String,
    description: `
      The "prerelease identifier" to use as a prefix for the "prerelease" part
      of a semver. Like the \`rc\` in \`1.2.0-rc.8\`.
    `,
    flatten,
  }),
  production: new Definition('production', {
    default: null,
    type: [null, Boolean],
    deprecated: 'Use `--omit=dev` instead.',
    description: 'Alias for `--omit=dev`',
    flatten (key, obj, flatOptions) {
      definitions.omit.flatten('omit', obj, flatOptions)
    },
  }),
  progress: new Definition('progress', {
    default: !ciInfo.isCI,
    defaultDescription: `
      \`true\` unless running in a known CI system
    `,
    type: Boolean,
    description: `
      When set to \`true\`, npm will display a progress bar during time
      intensive operations, if \`process.stderr\` and \`process.stdout\` are a TTY.

      Set to \`false\` to suppress the progress bar.
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.progress = !obj.progress ? false
        // progress is only written to stderr but we disable it unless stdout is a tty
        // also. This prevents the progress from appearing when piping output to another
        // command which doesn't break anything, but does look very odd to users.
        : !!process.stderr.isTTY && !!process.stdout.isTTY && process.env.TERM !== 'dumb'
    },
  }),
  provenance: new Definition('provenance', {
    default: false,
    type: Boolean,
    exclusive: ['provenance-file'],
    description: `
      When publishing from a supported cloud CI/CD system, the package will be
      publicly linked to where it was built and published from.
    `,
    flatten,
  }),
  'provenance-file': new Definition('provenance-file', {
    default: null,
    type: path,
    hint: '<file>',
    exclusive: ['provenance'],
    description: `
      When publishing, the provenance bundle at the given path will be used.
    `,
    flatten,
  }),
  proxy: new Definition('proxy', {
    default: null,
    type: [null, false, url], // allow proxy to be disabled explicitly
    description: `
      A proxy to use for outgoing http requests. If the \`HTTP_PROXY\` or
      \`http_proxy\` environment variables are set, proxy settings will be
      honored by the underlying \`request\` library.
    `,
    flatten,
  }),
  'read-only': new Definition('read-only', {
    default: false,
    type: Boolean,
    description: `
      This is used to mark a token as unable to publish when configuring
      limited access tokens with the \`npm token create\` command.
    `,
    flatten,
  }),
  'rebuild-bundle': new Definition('rebuild-bundle', {
    default: true,
    type: Boolean,
    description: `
      Rebuild bundled dependencies after installation.
    `,
    flatten,
  }),
  registry: new Definition('registry', {
    default: 'https://registry.npmjs.org/',
    type: url,
    description: `
      The base URL of the npm registry.
    `,
    flatten,
  }),
  'replace-registry-host': new Definition('replace-registry-host', {
    default: 'npmjs',
    hint: '<npmjs|never|always> | hostname',
    type: ['npmjs', 'never', 'always', String],
    description: `
      Defines behavior for replacing the registry host in a lockfile with the
      configured registry.

      The default behavior is to replace package dist URLs from the default
      registry (https://registry.npmjs.org) to the configured registry. If set to
      "never", then use the registry value. If set to "always", then replace the
      registry host with the configured host every time.

      You may also specify a bare hostname (e.g., "registry.npmjs.org").
    `,
    flatten,
  }),
  save: new Definition('save', {
    default: true,
    defaultDescription: `\`true\` unless when using \`npm update\` where it
    defaults to \`false\``,
    usage: '-S|--save|--no-save|--save-prod|--save-dev|--save-optional|--save-peer|--save-bundle',
    type: Boolean,
    short: 'S',
    description: `
      Save installed packages to a \`package.json\` file as dependencies.

      When used with the \`npm rm\` command, removes the dependency from
      \`package.json\`.

      Will also prevent writing to \`package-lock.json\` if set to \`false\`.
    `,
    flatten,
  }),
  'save-bundle': new Definition('save-bundle', {
    default: false,
    type: Boolean,
    short: 'B',
    description: `
      If a package would be saved at install time by the use of \`--save\`,
      \`--save-dev\`, or \`--save-optional\`, then also put it in the
      \`bundleDependencies\` list.

      Ignored if \`--save-peer\` is set, since peerDependencies cannot be bundled.
    `,
    flatten (key, obj, flatOptions) {
      // XXX update arborist to just ignore it if resulting saveType is peer
      // otherwise this won't have the expected effect:
      //
      // npm config set save-peer true
      // npm i foo --save-bundle --save-prod <-- should bundle
      flatOptions.saveBundle = obj['save-bundle'] && !obj['save-peer']
    },
  }),
  'save-dev': new Definition('save-dev', {
    default: false,
    type: Boolean,
    short: 'D',
    description: `
      Save installed packages to a package.json file as \`devDependencies\`.
    `,
    flatten (key, obj, flatOptions) {
      if (!obj[key]) {
        if (flatOptions.saveType === 'dev') {
          delete flatOptions.saveType
        }
        return
      }

      flatOptions.saveType = 'dev'
    },
  }),
  'save-exact': new Definition('save-exact', {
    default: false,
    type: Boolean,
    short: 'E',
    description: `
      Dependencies saved to package.json will be configured with an exact
      version rather than using npm's default semver range operator.
    `,
    flatten (key, obj, flatOptions) {
      // just call the save-prefix flattener, it reads from obj['save-exact']
      definitions['save-prefix'].flatten('save-prefix', obj, flatOptions)
    },
  }),
  'save-optional': new Definition('save-optional', {
    default: false,
    type: Boolean,
    short: 'O',
    description: `
      Save installed packages to a package.json file as
      \`optionalDependencies\`.
    `,
    flatten (key, obj, flatOptions) {
      if (!obj[key]) {
        if (flatOptions.saveType === 'optional') {
          delete flatOptions.saveType
        } else if (flatOptions.saveType === 'peerOptional') {
          flatOptions.saveType = 'peer'
        }
        return
      }

      if (flatOptions.saveType === 'peerOptional') {
        return
      }

      if (flatOptions.saveType === 'peer') {
        flatOptions.saveType = 'peerOptional'
      } else {
        flatOptions.saveType = 'optional'
      }
    },
  }),
  'save-peer': new Definition('save-peer', {
    default: false,
    type: Boolean,
    description: `
      Save installed packages to a package.json file as \`peerDependencies\`
    `,
    flatten (key, obj, flatOptions) {
      if (!obj[key]) {
        if (flatOptions.saveType === 'peer') {
          delete flatOptions.saveType
        } else if (flatOptions.saveType === 'peerOptional') {
          flatOptions.saveType = 'optional'
        }
        return
      }

      if (flatOptions.saveType === 'peerOptional') {
        return
      }

      if (flatOptions.saveType === 'optional') {
        flatOptions.saveType = 'peerOptional'
      } else {
        flatOptions.saveType = 'peer'
      }
    },
  }),
  'save-prefix': new Definition('save-prefix', {
    default: '^',
    type: String,
    description: `
      Configure how versions of packages installed to a package.json file via
      \`--save\` or \`--save-dev\` get prefixed.

      For example if a package has version \`1.2.3\`, by default its version is
      set to \`^1.2.3\` which allows minor upgrades for that package, but after
      \`npm config set save-prefix='~'\` it would be set to \`~1.2.3\` which
      only allows patch upgrades.
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.savePrefix = obj['save-exact'] ? '' : obj['save-prefix']
      obj['save-prefix'] = flatOptions.savePrefix
    },
  }),
  'save-prod': new Definition('save-prod', {
    default: false,
    type: Boolean,
    short: 'P',
    description: `
      Save installed packages into \`dependencies\` specifically. This is
      useful if a package already exists in \`devDependencies\` or
      \`optionalDependencies\`, but you want to move it to be a non-optional
      production dependency.

      This is the default behavior if \`--save\` is true, and neither
      \`--save-dev\` or \`--save-optional\` are true.
    `,
    flatten (key, obj, flatOptions) {
      if (!obj[key]) {
        if (flatOptions.saveType === 'prod') {
          delete flatOptions.saveType
        }
        return
      }

      flatOptions.saveType = 'prod'
    },
  }),
  'sbom-format': new Definition('sbom-format', {
    default: null,
    type: [
      'cyclonedx',
      'spdx',
    ],
    description: `
      SBOM format to use when generating SBOMs.
    `,
    flatten,
  }),
  'sbom-type': new Definition('sbom-type', {
    default: 'library',
    type: [
      'library',
      'application',
      'framework',
    ],
    description: `
      The type of package described by the generated SBOM. For SPDX, this is the
      value for the \`primaryPackagePurpose\` field. For CycloneDX, this is the
      value for the \`type\` field.
    `,
    flatten,
  }),
  scope: new Definition('scope', {
    default: '',
    defaultDescription: `
      the scope of the current project, if any, or ""
    `,
    type: String,
    hint: '<@scope>',
    description: `
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
    `,
    flatten (key, obj, flatOptions) {
      const value = obj[key]
      const scope = value && !/^@/.test(value) ? `@${value}` : value
      flatOptions.scope = scope
      // projectScope is kept for compatibility with npm-registry-fetch
      flatOptions.projectScope = scope
    },
  }),
  'script-shell': new Definition('script-shell', {
    default: null,
    defaultDescription: `
      '/bin/sh' on POSIX systems, 'cmd.exe' on Windows
    `,
    type: [null, String],
    description: `
      The shell to use for scripts run with the \`npm exec\`,
      \`npm run\` and \`npm init <package-spec>\` commands.
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.scriptShell = obj[key] || undefined
    },
  }),
  searchexclude: new Definition('searchexclude', {
    default: '',
    type: String,
    description: `
      Space-separated options that limit the results from search.
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.search = flatOptions.search || { limit: 20 }
      flatOptions.search.exclude = obj[key].toLowerCase()
    },
  }),
  searchlimit: new Definition('searchlimit', {
    default: 20,
    type: Number,
    description: `
      Number of items to limit search results to. Will not apply at all to
      legacy searches.
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.search = flatOptions.search || {}
      flatOptions.search.limit = obj[key]
    },
  }),
  searchopts: new Definition('searchopts', {
    default: '',
    type: String,
    description: `
      Space-separated options that are always passed to search.
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.search = flatOptions.search || { limit: 20 }
      flatOptions.search.opts = querystring.parse(obj[key])
    },
  }),
  searchstaleness: new Definition('searchstaleness', {
    default: 15 * 60,
    type: Number,
    description: `
      The age of the cache, in seconds, before another registry request is made
      if using legacy search endpoint.
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.search = flatOptions.search || { limit: 20 }
      flatOptions.search.staleness = obj[key]
    },
  }),
  shell: new Definition('shell', {
    default: shell,
    defaultDescription: `
      SHELL environment variable, or "bash" on Posix, or "cmd.exe" on Windows
    `,
    type: String,
    description: `
      The shell to run for the \`npm explore\` command.
    `,
    flatten,
  }),
  shrinkwrap: new Definition('shrinkwrap', {
    default: true,
    type: Boolean,
    deprecated: `
      Use the --package-lock setting instead.
    `,
    description: `
      Alias for --package-lock
    `,
    flatten (key, obj, flatOptions) {
      obj['package-lock'] = obj.shrinkwrap
      definitions['package-lock'].flatten('package-lock', obj, flatOptions)
    },
  }),
  'sign-git-commit': new Definition('sign-git-commit', {
    default: false,
    type: Boolean,
    description: `
      If set to true, then the \`npm version\` command will commit the new
      package version using \`-S\` to add a signature.

      Note that git requires you to have set up GPG keys in your git configs
      for this to work properly.
    `,
    flatten,
  }),
  'sign-git-tag': new Definition('sign-git-tag', {
    default: false,
    type: Boolean,
    description: `
      If set to true, then the \`npm version\` command will tag the version
      using \`-s\` to add a signature.

      Note that git requires you to have set up GPG keys in your git configs
      for this to work properly.
    `,
    flatten,
  }),
  'strict-peer-deps': new Definition('strict-peer-deps', {
    default: false,
    type: Boolean,
    description: `
      If set to \`true\`, and \`--legacy-peer-deps\` is not set, then _any_
      conflicting \`peerDependencies\` will be treated as an install failure,
      even if npm could reasonably guess the appropriate resolution based on
      non-peer dependency relationships.

      By default, conflicting \`peerDependencies\` deep in the dependency graph
      will be resolved using the nearest non-peer dependency specification,
      even if doing so will result in some packages receiving a peer dependency
      outside the range set in their package's \`peerDependencies\` object.

      When such an override is performed, a warning is printed, explaining the
      conflict and the packages involved.  If \`--strict-peer-deps\` is set,
      then this warning is treated as a failure.
    `,
    flatten,
  }),
  'strict-ssl': new Definition('strict-ssl', {
    default: true,
    type: Boolean,
    description: `
      Whether or not to do SSL key validation when making requests to the
      registry via https.

      See also the \`ca\` config.
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.strictSSL = obj[key]
    },
  }),
  tag: new Definition('tag', {
    default: 'latest',
    type: String,
    description: `
      If you ask npm to install a package and don't tell it a specific version,
      then it will install the specified tag.

      It is the tag added to the package@version specified in the
      \`npm dist-tag add\` command, if no explicit tag is given.

      When used by the \`npm diff\` command, this is the tag used to fetch the
      tarball that will be compared with the local files by default.

      If used in the \`npm publish\` command, this is the tag that will be
      added to the package submitted to the registry.
    `,
    flatten (key, obj, flatOptions) {
      flatOptions.defaultTag = obj[key]
    },
  }),
  'tag-version-prefix': new Definition('tag-version-prefix', {
    default: 'v',
    type: String,
    description: `
      If set, alters the prefix used when tagging a new version when performing
      a version increment using  \`npm version\`. To remove the prefix
      altogether, set it to the empty string: \`""\`.

      Because other tools may rely on the convention that npm version tags look
      like \`v1.0.0\`, _only use this property if it is absolutely necessary_.
      In particular, use care when overriding this setting for public packages.
    `,
    flatten,
  }),
  timing: new Definition('timing', {
    default: false,
    type: Boolean,
    description: `
      If true, writes timing information to a process specific json file in
      the cache or \`logs-dir\`. The file name ends with \`-timing.json\`.

      You can quickly view it with this [json](https://npm.im/json) command
      line: \`cat ~/.npm/_logs/*-timing.json | npm exec -- json -g\`.

      Timing information will also be reported in the terminal. To suppress this
      while still writing the timing file, use \`--silent\`.
    `,
  }),
  umask: new Definition('umask', {
    default: 0,
    type: Umask,
    description: `
      The "umask" value to use when setting the file creation mode on files and
      folders.

      Folders and executables are given a mode which is \`0o777\` masked
      against this value.  Other files are given a mode which is \`0o666\`
      masked against this value.

      Note that the underlying system will _also_ apply its own umask value to
      files and folders that are created, and npm does not circumvent this, but
      rather adds the \`--umask\` config to it.

      Thus, the effective default umask value on most POSIX systems is 0o22,
      meaning that folders and executables are created with a mode of 0o755 and
      other files are created with a mode of 0o644.
    `,
    flatten,
  }),
  unicode: new Definition('unicode', {
    default: unicode,
    defaultDescription: `
      false on windows, true on mac/unix systems with a unicode locale, as
      defined by the \`LC_ALL\`, \`LC_CTYPE\`, or \`LANG\` environment variables.
    `,
    type: Boolean,
    description: `
      When set to true, npm uses unicode characters in the tree output.  When
      false, it uses ascii characters instead of unicode glyphs.
    `,
    flatten,
  }),
  'update-notifier': new Definition('update-notifier', {
    default: true,
    type: Boolean,
    description: `
      Set to false to suppress the update notification when using an older
      version of npm than the latest.
    `,
  }),
  usage: new Definition('usage', {
    default: false,
    type: Boolean,
    short: ['?', 'H', 'h'],
    description: `
      Show short usage output about the command specified.
    `,
  }),
  'user-agent': new Definition('user-agent', {
    default: 'npm/{npm-version} ' +
            'node/{node-version} ' +
            '{platform} ' +
            '{arch} ' +
            'workspaces/{workspaces} ' +
            '{ci}',
    type: String,
    description: `
      Sets the User-Agent request header.  The following fields are replaced
      with their actual counterparts:

      * \`{npm-version}\` - The npm version in use
      * \`{node-version}\` - The Node.js version in use
      * \`{platform}\` - The value of \`process.platform\`
      * \`{arch}\` - The value of \`process.arch\`
      * \`{workspaces}\` - Set to \`true\` if the \`workspaces\` or \`workspace\`
        options are set.
      * \`{ci}\` - The value of the \`ci-name\` config, if set, prefixed with
        \`ci/\`, or an empty string if \`ci-name\` is empty.
    `,
    flatten (key, obj, flatOptions) {
      const value = obj[key]
      const ciName = ciInfo.name?.toLowerCase().split(' ').join('-') || null
      let inWorkspaces = false
      if (obj.workspaces || obj.workspace && obj.workspace.length) {
        inWorkspaces = true
      }
      flatOptions.userAgent =
        value.replace(/\{node-version\}/gi, process.version)
          .replace(/\{npm-version\}/gi, obj['npm-version'])
          .replace(/\{platform\}/gi, process.platform)
          .replace(/\{arch\}/gi, process.arch)
          .replace(/\{workspaces\}/gi, inWorkspaces)
          .replace(/\{ci\}/gi, ciName ? `ci/${ciName}` : '')
          .trim()

      // We can't clobber the original or else subsequent flattening will fail
      // (i.e. when we change the underlying config values)
      // obj[key] = flatOptions.userAgent

      // user-agent is a unique kind of config item that gets set from a template
      // and ends up translated.  Because of this, the normal "should we set this
      // to process.env also doesn't work
      process.env.npm_config_user_agent = flatOptions.userAgent
    },
  }),
  userconfig: new Definition('userconfig', {
    default: '~/.npmrc',
    type: path,
    description: `
      The location of user-level configuration settings.

      This may be overridden by the \`npm_config_userconfig\` environment
      variable or the \`--userconfig\` command line option, but may _not_
      be overridden by settings in the \`globalconfig\` file.
    `,
  }),
  version: new Definition('version', {
    default: false,
    type: Boolean,
    short: 'v',
    description: `
      If true, output the npm version and exit successfully.

      Only relevant when specified explicitly on the command line.
    `,
  }),
  versions: new Definition('versions', {
    default: false,
    type: Boolean,
    description: `
      If true, output the npm version as well as node's \`process.versions\`
      map and the version in the current working directory's \`package.json\`
      file if one exists, and exit successfully.

      Only relevant when specified explicitly on the command line.
    `,
  }),
  viewer: new Definition('viewer', {
    default: isWindows ? 'browser' : 'man',
    defaultDescription: `
      "man" on Posix, "browser" on Windows
    `,
    type: String,
    description: `
      The program to use to view help content.

      Set to \`"browser"\` to view html help content in the default web browser.
    `,
  }),
  which: new Definition('which', {
    default: null,
    hint: '<fundingSourceNumber>',
    type: [null, Number],
    description: `
      If there are multiple funding sources, which 1-indexed source URL to open.
    `,
  }),
  workspace: new Definition('workspace', {
    default: [],
    type: [String, Array],
    hint: '<workspace-name>',
    short: 'w',
    envExport: false,
    description: `
      Enable running a command in the context of the configured workspaces of the
      current project while filtering by running only the workspaces defined by
      this configuration option.

      Valid values for the \`workspace\` config are either:

      * Workspace names
      * Path to a workspace directory
      * Path to a parent workspace directory (will result in selecting all
        workspaces within that folder)

      When set for the \`npm init\` command, this may be set to the folder of
      a workspace which does not yet exist, to create the folder and set it
      up as a brand new workspace within the project.
    `,
    flatten: (key, obj, flatOptions) => {
      definitions['user-agent'].flatten('user-agent', obj, flatOptions)
    },
  }),
  workspaces: new Definition('workspaces', {
    default: null,
    type: [null, Boolean],
    short: 'ws',
    envExport: false,
    description: `
      Set to true to run the command in the context of **all** configured
      workspaces.

      Explicitly setting this to false will cause commands like \`install\` to
      ignore workspaces altogether.
      When not set explicitly:

      - Commands that operate on the \`node_modules\` tree (install, update,
        etc.) will link workspaces into the \`node_modules\` folder.
      - Commands that do other things (test, exec, publish, etc.) will operate
        on the root project, _unless_ one or more workspaces are specified in
        the \`workspace\` config.
    `,
    flatten: (key, obj, flatOptions) => {
      definitions['user-agent'].flatten('user-agent', obj, flatOptions)

      // TODO: this is a derived value, and should be reworked when we have a
      // pattern for derived value

      // workspacesEnabled is true whether workspaces is null or true
      // commands contextually work with workspaces or not regardless of
      // configuration, so we need an option specifically to disable workspaces
      flatOptions.workspacesEnabled = obj[key] !== false
    },
  }),
  'workspaces-update': new Definition('workspaces-update', {
    default: true,
    type: Boolean,
    description: `
      If set to true, the npm cli will run an update after operations that may
      possibly change the workspaces installed to the \`node_modules\` folder.
    `,
    flatten,
  }),
  yes: new Definition('yes', {
    default: null,
    type: [null, Boolean],
    short: 'y',
    description: `
      Automatically answer "yes" to any prompts that npm might print on
      the command line.
    `,
  }),
}

module.exports = definitions

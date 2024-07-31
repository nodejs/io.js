# `@npmcli/config`

Configuration management for the npm cli.

This module is the spiritual descendant of
[`npmconf`](http://npm.im/npmconf), and the code that once lived in npm's
`lib/config/` folder.

It does the management of configuration files that npm uses, but
importantly, does _not_ define all the configuration defaults or types, as
those parts make more sense to live within the npm CLI itself.

The only exceptions:

- The `prefix` config value has some special semantics, setting the local
  prefix if specified on the CLI options and not in global mode, or the
  global prefix otherwise.
- The `project` config file is loaded based on the local prefix (which can
  only be set by the CLI config options, and otherwise defaults to a walk
  up the folder tree to the first parent containing a `node_modules`
  folder, `package.json` file, or `package-lock.json` file.)
- The `userconfig` value, as set by the environment and CLI (defaulting to
  `~/.npmrc`, is used to load user configs.
- The `globalconfig` value, as set by the environment, CLI, and
  `userconfig` file (defaulting to `$PREFIX/etc/npmrc`) is used to load
  global configs.
- A `builtin` config, read from a `npmrc` file in the root of the npm
  project itself, overrides all defaults.

The resulting hierarchy of configs:

- CLI switches.  eg `--some-key=some-value` on the command line.  These are
  parsed by [`nopt`](http://npm.im/nopt), which is not a great choice, but
  it's the one that npm has used forever, and changing it will be
  difficult.
- Environment variables.  eg `npm_config_some_key=some_value` in the
  environment.  There is no way at this time to modify this prefix.
- INI-formatted project configs.  eg `some-key = some-value` in the
  `localPrefix` folder (ie, the `cwd`, or its nearest parent that contains
  either a `node_modules` folder or `package.json` file.)
- INI-formatted userconfig file.  eg `some-key = some-value` in `~/.npmrc`.
  The `userconfig` config value can be overridden by the `cli`, `env`, or
  `project` configs to change this value.
- INI-formatted globalconfig file.  eg `some-key = some-value` in
  the `globalPrefix` folder, which is inferred by looking at the location
  of the node executable, or the `prefix` setting in the `cli`, `env`,
  `project`, or `userconfig`.  The `globalconfig` value at any of those
  levels can override this.
- INI-formatted builtin config file.  eg `some-key = some-value` in
  `/usr/local/lib/node_modules/npm/npmrc`.  This is not configurable, and
  is determined by looking in the `npmPath` folder.
- Default values (passed in by npm when it loads this module).

## USAGE

```js
const Config = require('@npmcli/config')
const { shorthands, definitions, flatten } = require('@npmcli/config/lib/definitions')

const conf = new Config({
  // path to the npm module being run
  npmPath: resolve(__dirname, '..'),
  definitions,
  shorthands,
  flatten,
  // optional, defaults to process.argv
  // argv: [] <- if you are using this package in your own cli
  //             and dont want to have colliding argv
  argv: process.argv,
  // optional, defaults to process.env
  env: process.env,
  // optional, defaults to process.execPath
  execPath: process.execPath,
  // optional, defaults to process.platform
  platform: process.platform,
  // optional, defaults to process.cwd()
  cwd: process.cwd(),
})

// emits log events on the process object
// see `proc-log` for more info
process.on('log', (level, ...args) => {
  console.log(level, ...args)
})

// returns a promise that fails if config loading fails, and
// resolves when the config object is ready for action
conf.load().then(() => {
  conf.validate()
  console.log('loaded ok! some-key = ' + conf.get('some-key'))
}).catch(er => {
  console.error('error loading configs!', er)
})
```

## API

The `Config` class is the sole export.

```js
const Config = require('@npmcli/config')
```

### static `Config.typeDefs`

The type definitions passed to `nopt` for CLI option parsing and known
configuration validation.

### constructor `new Config(options)`

Options:

- `types` Types of all known config values.  Note that some are effectively
  given semantic value in the config loading process itself.
- `shorthands` An object mapping a shorthand value to an array of CLI
  arguments that replace it.
- `defaults` Default values for each of the known configuration keys.
  These should be defined for all configs given a type, and must be valid.
- `npmPath` The path to the `npm` module, for loading the `builtin` config
  file.
- `cwd` Optional, defaults to `process.cwd()`, used for inferring the
  `localPrefix` and loading the `project` config.
- `platform` Optional, defaults to `process.platform`.  Used when inferring
  the `globalPrefix` from the `execPath`, since this is done diferently on
  Windows.
- `execPath` Optional, defaults to `process.execPath`.  Used to infer the
  `globalPrefix`.
- `env` Optional, defaults to `process.env`.  Source of the environment
  variables for configuration.
- `argv` Optional, defaults to `process.argv`.  Source of the CLI options
  used for configuration.

Returns a `config` object, which is not yet loaded.

Fields:

- `config.globalPrefix` The prefix for `global` operations.  Set by the
  `prefix` config value, or defaults based on the location of the
  `execPath` option.
- `config.localPrefix` The prefix for `local` operations.  Set by the
  `prefix` config value on the CLI only, or defaults to either the `cwd` or
  its nearest ancestor containing a `node_modules` folder or `package.json`
  file.
- `config.sources` A read-only `Map` of the file (or a comment, if no file
  found, or relevant) to the config level loaded from that source.
- `config.data` A `Map` of config level to `ConfigData` objects.  These
  objects should not be modified directly under any circumstances.
  - `source` The source where this data was loaded from.
  - `raw` The raw data used to generate this config data, as it was parsed
    initially from the environment, config file, or CLI options.
  - `data` The data object reflecting the inheritance of configs up to this
    point in the chain.
  - `loadError` Any errors encountered that prevented the loading of this
    config data.
- `config.list` A list sorted in priority of all the config data objects in
  the prototype chain.  `config.list[0]` is the `cli` level,
  `config.list[1]` is the `env` level, and so on.
- `cwd` The `cwd` param
- `env` The `env` param
- `argv` The `argv` param
- `execPath` The `execPath` param
- `platform` The `platform` param
- `defaults` The `defaults` param
- `shorthands` The `shorthands` param
- `types` The `types` param
- `npmPath` The `npmPath` param
- `globalPrefix` The effective `globalPrefix`
- `localPrefix` The effective `localPrefix`
- `prefix` If `config.get('global')` is true, then `globalPrefix`,
  otherwise `localPrefix`
- `home` The user's home directory, found by looking at `env.HOME` or
  calling `os.homedir()`.
- `loaded` A boolean indicating whether or not configs are loaded
- `valid` A getter that returns `true` if all the config objects are valid.
  Any data objects that have been modified with `config.set(...)` will be
  re-evaluated when `config.valid` is read.

### `config.load()`

Load configuration from the various sources of information.

Returns a `Promise` that resolves when configuration is loaded, and fails
if a fatal error is encountered.

### `config.find(key)`

Find the effective place in the configuration levels a given key is set.
Returns one of: `cli`, `env`, `project`, `user`, `global`, `builtin`, or
`default`.

Returns `null` if the key is not set.

### `config.get(key, where = 'cli')`

Load the given key from the config stack.

### `config.set(key, value, where = 'cli')`

Set the key to the specified value, at the specified level in the config
stack.

### `config.delete(key, where = 'cli')`

Delete the configuration key from the specified level in the config stack.

### `config.validate(where)`

Verify that all known configuration options are set to valid values, and
log a warning if they are invalid.

Invalid auth options will cause this method to throw an error with a `code`
property of `ERR_INVALID_AUTH`, and a `problems` property listing the specific
concerns with the current configuration.

If `where` is not set, then all config objects are validated.

Returns `true` if all configs are valid.

Note that it's usually enough (and more efficient) to just check
`config.valid`, since each data object is marked for re-evaluation on every
`config.set()` operation.

### `config.repair(problems)`

Accept an optional array of problems (as thrown by `config.validate()`) and
perform the necessary steps to resolve them. If no problems are provided,
this method will call `config.validate()` internally to retrieve them.

Note that you must `await config.save('user')` in order to persist the changes.

### `config.isDefault(key)`

Returns `true` if the value is coming directly from the
default definitions, if the current value for the key config is
coming from any other source, returns `false`.

This method can be used for avoiding or tweaking default values, e.g:

>  Given a global default definition of foo='foo' it's possible to read that
>  value such as:
>
>  ```js
>     const save = config.get('foo')
>  ```
>
>  Now in a different place of your app it's possible to avoid using the `foo`
>  default value, by checking to see if the current config value is currently
>  one that was defined by the default definitions:
>
>  ```js
>     const save = config.isDefault('foo') ? 'bar' : config.get('foo')
>  ```

### `config.save(where)`

Save the config file specified by the `where` param.  Must be one of
`project`, `user`, `global`, `builtin`.

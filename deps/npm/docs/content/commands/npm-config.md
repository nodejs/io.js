---
title: npm-config
section: 1
description: Manage the npm configuration files
---

### Synopsis

```bash
npm config set <key>=<value> [<key>=<value> ...]
npm config get [<key> [<key> ...]]
npm config delete <key> [<key> ...]
npm config list [--json]
npm config edit
npm config fix

alias: c
```

Note: This command is unaware of workspaces.

### Description

npm gets its config settings from the command line, environment
variables, `npmrc` files, and in some cases, the `package.json` file.

See [npmrc](/configuring-npm/npmrc) for more information about the npmrc
files.

See [config](/using-npm/config) for a more thorough explanation of the
mechanisms involved, and a full list of config options available.

The `npm config` command can be used to update and edit the contents
of the user and global npmrc files.

### Sub-commands

Config supports the following sub-commands:

#### set

```bash
npm config set key=value [key=value...]
npm set key=value [key=value...]
```

Sets each of the config keys to the value provided. Modifies the user configuration
file unless [`location`](/commands/npm-config#location) is passed.

If value is omitted, the key will be removed from your config file entirely.

Note: for backwards compatibility, `npm config set key value` is supported
as an alias for `npm config set key=value`.

#### get

```bash
npm config get [key ...]
npm get [key ...]
```

Echo the config value(s) to stdout.

If multiple keys are provided, then the values will be prefixed with the
key names.

If no keys are provided, then this command behaves the same as `npm config
list`.

#### list

```bash
npm config list
```

Show all the config settings. Use `-l` to also show defaults. Use `--json`
to show the settings in json format.

#### delete

```bash
npm config delete key [key ...]
```

Deletes the specified keys from all configuration files.

#### edit

```bash
npm config edit
```

Opens the config file in an editor.  Use the `--global` flag to edit the
global config.

#### fix

```bash
npm config fix
```

Attempts to repair invalid configuration items.  Usually this means
attaching authentication config (i.e. `_auth`, `_authToken`) to the
configured `registry`.

### Configuration

#### `json`

* Default: false
* Type: Boolean

Whether or not to output JSON data, rather than the normal output.

* In `npm pkg set` it enables parsing set values with JSON.parse() before
  saving them to your `package.json`.

Not supported by all npm commands.



#### `global`

* Default: false
* Type: Boolean

Operates in "global" mode, so that packages are installed into the `prefix`
folder instead of the current working directory. See
[folders](/configuring-npm/folders) for more on the differences in behavior.

* packages are installed into the `{prefix}/lib/node_modules` folder, instead
  of the current working directory.
* bin files are linked to `{prefix}/bin`
* man pages are linked to `{prefix}/share/man`



#### `editor`

* Default: The EDITOR or VISUAL environment variables, or
  '%SYSTEMROOT%\notepad.exe' on Windows, or 'vi' on Unix systems
* Type: String

The command to run for `npm edit` and `npm config edit`.



#### `location`

* Default: "user" unless `--global` is passed, which will also set this value
  to "global"
* Type: "global", "user", or "project"

When passed to `npm config` this refers to which config file to use.

When set to "global" mode, packages are installed into the `prefix` folder
instead of the current working directory. See
[folders](/configuring-npm/folders) for more on the differences in behavior.

* packages are installed into the `{prefix}/lib/node_modules` folder, instead
  of the current working directory.
* bin files are linked to `{prefix}/bin`
* man pages are linked to `{prefix}/share/man`



#### `long`

* Default: false
* Type: Boolean

Show extended information in `ls`, `search`, and `help-search`.



### See Also

* [npm folders](/configuring-npm/folders)
* [npm config](/commands/npm-config)
* [package.json](/configuring-npm/package-json)
* [npmrc](/configuring-npm/npmrc)
* [npm](/commands/npm)

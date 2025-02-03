---
title: npm-undeprecate
section: 1
description: Undeprecate a version of a package
---

### Synopsis

```bash
npm undeprecate <package-spec>
```

Note: This command is unaware of workspaces.

### Description

This command will update the npm registry entry for a package, removing any
deprecation warnings that currently exist.

It works in the same way as [npm deprecate](/commands/npm-deprecate), except
that this command removes deprecation warnings instead of adding them.

### Configuration

#### `registry`

* Default: "https://registry.npmjs.org/"
* Type: URL

The base URL of the npm registry.



#### `otp`

* Default: null
* Type: null or String

This is a one-time password from a two-factor authenticator. It's needed
when publishing or changing package permissions with `npm access`.

If not set, and a registry response fails with a challenge for a one-time
password, npm will prompt on the command line for one.



#### `dry-run`

* Default: false
* Type: Boolean

Indicates that you don't want npm to make any changes and that it should
only report what it would have done. This can be passed into any of the
commands that modify your local installation, eg, `install`, `update`,
`dedupe`, `uninstall`, as well as `pack` and `publish`.

Note: This is NOT honored by other network related commands, eg `dist-tags`,
`owner`, etc.


### See Also

* [npm deprecate](/commands/npm-deprecate)

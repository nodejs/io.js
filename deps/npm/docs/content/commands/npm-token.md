---
title: npm-token
section: 1
description: Manage your authentication tokens
---

### Synopsis

```bash
npm token list
npm token revoke <id|token>
npm token create [--read-only] [--cidr=list]
```

Note: This command is unaware of workspaces.

### Description

This lets you list, create and revoke authentication tokens.

* `npm token list`:
  Shows a table of all active authentication tokens. You can request
  this as JSON with `--json` or tab-separated values with `--parseable`.

```
Read only token npm_1f… with id 7f3134 created 2017-10-21

Publish token npm_af…  with id c03241 created 2017-10-02
with IP Whitelist: 192.168.0.1/24

Publish token npm_… with id e0cf92 created 2017-10-02

```

* `npm token create [--read-only] [--cidr=<cidr-ranges>]`:
  Create a new authentication token. It can be `--read-only`, or accept
  a list of
  [CIDR](https://en.wikipedia.org/wiki/Classless_Inter-Domain_Routing)
  ranges with which to limit use of this token. This will prompt you for
  your password, and, if you have two-factor authentication enabled, an
  otp.

  Currently, the cli can not generate automation tokens. Please refer to
  the [docs
  website](https://docs.npmjs.com/creating-and-viewing-access-tokens)
  for more information on generating automation tokens.

```
Created publish token a73c9572-f1b9-8983-983d-ba3ac3cc913d
```

* `npm token revoke <token|id>`:
  Immediately removes an authentication token from the registry.  You
  will no longer be able to use it.  This can accept both complete
  tokens (such as those you get back from `npm token create`, and those
  found in your `.npmrc`), and ids as seen in the parseable or json
  output of `npm token list`.  This will NOT accept the truncated token
  found in the normal `npm token list` output.

### Configuration

#### `read-only`

* Default: false
* Type: Boolean

This is used to mark a token as unable to publish when configuring limited
access tokens with the `npm token create` command.



#### `cidr`

* Default: null
* Type: null or String (can be set multiple times)

This is a list of CIDR address to be used when configuring limited access
tokens with the `npm token create` command.



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



### See Also

* [npm adduser](/commands/npm-adduser)
* [npm registry](/using-npm/registry)
* [npm config](/commands/npm-config)
* [npmrc](/configuring-npm/npmrc)
* [npm owner](/commands/npm-owner)
* [npm whoami](/commands/npm-whoami)
* [npm profile](/commands/npm-profile)

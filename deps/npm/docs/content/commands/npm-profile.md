---
title: npm-profile
section: 1
description: Change settings on your registry profile
---

### Synopsis

```bash
npm profile enable-2fa [auth-only|auth-and-writes]
npm profile disable-2fa
npm profile get [<key>]
npm profile set <key> <value>
```

Note: This command is unaware of workspaces.

### Description

Change your profile information on the registry.  Note that this command
depends on the registry implementation, so third-party registries may not
support this interface.

* `npm profile get [<property>]`: Display all of the properties of your
  profile, or one or more specific properties.  It looks like:

```
name: example
email: e@example.com (verified)
two-factor auth: auth-and-writes
fullname: Example User
homepage:
freenode:
twitter:
github:
created: 2015-02-26T01:38:35.892Z
updated: 2017-10-02T21:29:45.922Z
```

* `npm profile set <property> <value>`: Set the value of a profile
  property. You can set the following properties this way: email, fullname,
  homepage, freenode, twitter, github

* `npm profile set password`: Change your password.  This is interactive,
  you'll be prompted for your current password and a new password.  You'll
  also be prompted for an OTP if you have two-factor authentication
  enabled.

* `npm profile enable-2fa [auth-and-writes|auth-only]`: Enables two-factor
  authentication. Defaults to `auth-and-writes` mode. Modes are:
  * `auth-only`: Require an OTP when logging in or making changes to your
    account's authentication.  The OTP will be required on both the website
    and the command line.
  * `auth-and-writes`: Requires an OTP at all the times `auth-only` does,
    and also requires one when publishing a module, setting the `latest`
    dist-tag, or changing access via `npm access` and `npm owner`.

* `npm profile disable-2fa`: Disables two-factor authentication.

### Details

Some of these commands may not be available on non npmjs.com registries.

### Configuration

#### `registry`

* Default: "https://registry.npmjs.org/"
* Type: URL

The base URL of the npm registry.



#### `json`

* Default: false
* Type: Boolean

Whether or not to output JSON data, rather than the normal output.

* In `npm pkg set` it enables parsing set values with JSON.parse() before
  saving them to your `package.json`.

Not supported by all npm commands.



#### `parseable`

* Default: false
* Type: Boolean

Output parseable results from commands that write to standard output. For
`npm search`, this will be tab-separated table format.



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
* [npm token](/commands/npm-token)

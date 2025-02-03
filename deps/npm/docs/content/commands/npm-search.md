---
title: npm-search
section: 1
description: Search for packages
---

### Synopsis

```bash
npm search <search term> [<search term> ...]

aliases: find, s, se
```

Note: This command is unaware of workspaces.

### Description

Search the registry for packages matching the search terms. `npm search`
performs a linear, incremental, lexically-ordered search through package
metadata for all files in the registry. If your terminal has color
support, it will further highlight the matches in the results.  This can
be disabled with the config item `color`

Additionally, using the `--searchopts` and `--searchexclude` options
paired with more search terms will include and exclude further patterns.
The main difference between `--searchopts` and the standard search terms
is that the former does not highlight results in the output and you can
use them more fine-grained filtering. Additionally, you can add both of
these to your config to change default search filtering behavior.

Search also allows targeting of maintainers in search results, by prefixing
their npm username with `=`.

If a term starts with `/`, then it's interpreted as a regular expression
and supports standard JavaScript RegExp syntax. In this case search will
ignore a trailing `/` .  (Note you must escape or quote many regular
expression characters in most shells.)

### Configuration

#### `json`

* Default: false
* Type: Boolean

Whether or not to output JSON data, rather than the normal output.

* In `npm pkg set` it enables parsing set values with JSON.parse() before
  saving them to your `package.json`.

Not supported by all npm commands.



#### `color`

* Default: true unless the NO_COLOR environ is set to something other than '0'
* Type: "always" or Boolean

If false, never shows colors. If `"always"` then always shows colors. If
true, then only prints color codes for tty file descriptors.



#### `parseable`

* Default: false
* Type: Boolean

Output parseable results from commands that write to standard output. For
`npm search`, this will be tab-separated table format.



#### `description`

* Default: true
* Type: Boolean

Show the description in `npm search`



#### `searchlimit`

* Default: 20
* Type: Number

Number of items to limit search results to. Will not apply at all to legacy
searches.



#### `searchopts`

* Default: ""
* Type: String

Space-separated options that are always passed to search.



#### `searchexclude`

* Default: ""
* Type: String

Space-separated options that limit the results from search.



#### `registry`

* Default: "https://registry.npmjs.org/"
* Type: URL

The base URL of the npm registry.



#### `prefer-online`

* Default: false
* Type: Boolean

If true, staleness checks for cached data will be forced, making the CLI
look for updates immediately even for fresh package data.



#### `prefer-offline`

* Default: false
* Type: Boolean

If true, staleness checks for cached data will be bypassed, but missing data
will be requested from the server. To force full offline mode, use
`--offline`.



#### `offline`

* Default: false
* Type: Boolean

Force offline mode: no network requests will be done during install. To
allow the CLI to fill in missing cache data, see `--prefer-offline`.



### See Also

* [npm registry](/using-npm/registry)
* [npm config](/commands/npm-config)
* [npmrc](/configuring-npm/npmrc)
* [npm view](/commands/npm-view)
* [npm cache](/commands/npm-cache)
* https://npm.im/npm-registry-fetch

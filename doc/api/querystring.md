# Query string

<!--introduced_in=v0.1.25-->

> Stability: 2 - Stable

<!--name=querystring-->

<!-- source_link=lib/querystring.js -->

The `node:querystring` module provides utilities for parsing and formatting URL
query strings. It can be accessed using:

```js
const querystring = require('node:querystring');
```

`querystring` is more performant than {URLSearchParams} but is not a
standardized API. Use {URLSearchParams} when performance is not critical or
when compatibility with browser code is desirable.

## `querystring.decode()`

<!-- YAML
added: v0.1.99
-->

The `querystring.decode()` function is an alias for `querystring.parse()`.

## `querystring.encode()`

<!-- YAML
added: v0.1.99
-->

The `querystring.encode()` function is an alias for `querystring.stringify()`.

## `querystring.escape(str)`

<!-- YAML
added: v0.1.25
-->

* `str` {string}

The `querystring.escape()` method performs URL percent-encoding on the given
`str` in a manner that is optimized for the specific requirements of URL
query strings.

The `querystring.escape()` method is used by `querystring.stringify()` and is
generally not expected to be used directly. It is exported primarily to allow
application code to provide a replacement percent-encoding implementation if
necessary by assigning `querystring.escape` to an alternative function.

## `querystring.parse(str[, sep[, eq[, options]]])`

<!-- YAML
added: v0.1.25
changes:
  - version: v8.0.0
    pr-url: https://github.com/nodejs/node/pull/10967
    description: Multiple empty entries are now parsed correctly (e.g. `&=&=`).
  - version: v6.0.0
    pr-url: https://github.com/nodejs/node/pull/6055
    description: The returned object no longer inherits from `Object.prototype`.
  - version:
    - v6.0.0
    - v4.2.4
    pr-url: https://github.com/nodejs/node/pull/3807
    description: The `eq` parameter may now have a length of more than `1`.
-->

* `str` {string} The URL query string to parse
* `sep` {string} The substring used to delimit key and value pairs in the
  query string. **Default:** `'&'`.
* `eq` {string}. The substring used to delimit keys and values in the
  query string. **Default:** `'='`.
* `options` {Object}
  * `decodeURIComponent` {Function} The function to use when decoding
    percent-encoded characters in the query string. **Default:**
    `querystring.unescape()`.
  * `maxKeys` {number} Specifies the maximum number of keys to parse.
    Specify `0` to remove key counting limitations. **Default:** `1000`.

The `querystring.parse()` method parses a URL query string (`str`) into a
collection of key and value pairs.

For example, the query string `'foo=bar&abc=xyz&abc=123'` is parsed into:

```json
{
  "foo": "bar",
  "abc": ["xyz", "123"]
}
```

The object returned by the `querystring.parse()` method _does not_
prototypically inherit from the JavaScript `Object`. This means that typical
`Object` methods such as `obj.toString()`, `obj.hasOwnProperty()`, and others
are not defined and _will not work_.

By default, percent-encoded characters within the query string will be assumed
to use UTF-8 encoding. If an alternative character encoding is used, then an
alternative `decodeURIComponent` option will need to be specified:

```js
// Assuming gbkDecodeURIComponent function already exists...

querystring.parse('w=%D6%D0%CE%C4&foo=bar', null, null,
                  { decodeURIComponent: gbkDecodeURIComponent });
```

## `querystring.stringify(obj[, sep[, eq[, options]]])`

<!-- YAML
added: v0.1.25
-->

* `obj` {Object} The object to serialize into a URL query string
* `sep` {string} The substring used to delimit key and value pairs in the
  query string. **Default:** `'&'`.
* `eq` {string}. The substring used to delimit keys and values in the
  query string. **Default:** `'='`.
* `options`
  * `encodeURIComponent` {Function} The function to use when converting
    URL-unsafe characters to percent-encoding in the query string. **Default:**
    `querystring.escape()`.

The `querystring.stringify()` method produces a URL query string from a
given `obj` by iterating through the object's "own properties".

It serializes the following types of values passed in `obj`:
{string|number|bigint|boolean|string\[]|number\[]|bigint\[]|boolean\[]}
The numeric values must be finite. Any other input values will be coerced to
empty strings.

```js
querystring.stringify({ foo: 'bar', baz: ['qux', 'quux'], corge: '' });
// Returns 'foo=bar&baz=qux&baz=quux&corge='

querystring.stringify({ foo: 'bar', baz: 'qux' }, ';', ':');
// Returns 'foo:bar;baz:qux'
```

By default, characters requiring percent-encoding within the query string will
be encoded as UTF-8. If an alternative encoding is required, then an alternative
`encodeURIComponent` option will need to be specified:

```js
// Assuming gbkEncodeURIComponent function already exists,

querystring.stringify({ w: '中文', foo: 'bar' }, null, null,
                      { encodeURIComponent: gbkEncodeURIComponent });
```

## `querystring.unescape(str)`

<!-- YAML
added: v0.1.25
-->

* `str` {string}

The `querystring.unescape()` method performs decoding of URL percent-encoded
characters on the given `str`.

The `querystring.unescape()` method is used by `querystring.parse()` and is
generally not expected to be used directly. It is exported primarily to allow
application code to provide a replacement decoding implementation if
necessary by assigning `querystring.unescape` to an alternative function.

By default, the `querystring.unescape()` method will attempt to use the
JavaScript built-in `decodeURIComponent()` method to decode. If that fails,
a safer equivalent that does not throw on malformed URLs will be used.

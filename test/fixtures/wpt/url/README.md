## urltestdata.json / urltestdata-javascript-only.json

[`resources/urltestdata.json`](resources/urltestdata.json) contains URL parsing tests suitable for any URL parser implementation.
[`resources/urltestdata-javascript-only.json`](resources/urltestdata-javascript-only.json) contains URL parsing tests specifically meant
for JavaScript's `URL()` class as well as other languages accepting non-scalar-value strings.

These files are used as a source of tests by `a-element.html`, `failure.html`, `url-constructor.any.js`,
and other test files in this directory.

Both files share the same format. They consist of a JSON array of comments as strings and test cases as
objects. The keys for each test case are:

* `input`: a string to be parsed as URL.
* `base`: null or a serialized URL (i.e., does not fail parsing).
* Then either

  * `failure` whose value is `true`, indicating that parsing `input` relative to `base` returns
    failure
  * `relativeTo` whose value is "`non-opaque-path-base`" (input does parse against a non-null base
    URL without an opaque path) or "`any-base`" (input parses against any non-null base URL), or is
    omitted in its entirety (input never parses successfully)

  or `href`, `origin`, `protocol`, `username`, `password`, `host`, `hostname`, `port`,
  `pathname`, `search`, and `hash` with string values; indicating that parsing `input` should return
  an URL record and that the getters of each corresponding attribute in that URL’s [API] should
  return the corresponding value.

  The `origin` key may be missing. In that case, the API’s `origin` attribute is not tested.

## setters_tests.json

`resources/setters_tests.json` is self-documented.

## toascii.json

`resources/toascii.json` is a JSON resource containing an array where each item is an object
consisting of an optional `comment` field and mandatory `input` and `output` fields. `input` is the
domain to be parsed according to the rules of UTS #46 (as stipulated by the URL Standard). `output`
gives the expected output of the parser after serialization. An `output` of `null` means parsing is
expected to fail.

## URL parser's encoding argument

Tests in `/encoding` and `/html/infrastructure/urls/resolving-urls/query-encoding/` cover the
encoding argument to the URL parser.

There's also limited coverage in `resources/percent-encoding.json` for percent-encode after encoding
with _percentEncodeSet_ set to special-query percent-encode set and _spaceAsPlus_ set to false.
(Improvements to expand coverage here are welcome.)

## Specification

The tests in this directory assert conformance with [the URL Standard][URL].

[parsing]: https://url.spec.whatwg.org/#concept-basic-url-parser
[API]: https://url.spec.whatwg.org/#api
[URL]: https://url.spec.whatwg.org/

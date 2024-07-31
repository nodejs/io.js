# Node.js 4 ChangeLog

<!--lint disable maximum-line-length no-literal-urls prohibited-strings-->

<table>
<tr>
<th>LTS 'Argon'</th>
<th>Stable</th>
</tr>
<tr>
<td valign="top">
<a href="#4.9.1">4.9.1</a><br/>
<a href="#4.9.0">4.9.0</a><br/>
<a href="#4.8.7">4.8.7</a><br/>
<a href="#4.8.6">4.8.6</a><br/>
<a href="#4.8.5">4.8.5</a><br/>
<a href="#4.8.4">4.8.4</a><br/>
<a href="#4.8.3">4.8.3</a><br/>
<a href="#4.8.2">4.8.2</a><br/>
<a href="#4.8.1">4.8.1</a><br/>
<a href="#4.8.0">4.8.0</a><br/>
<a href="#4.7.3">4.7.3</a><br/>
<a href="#4.7.2">4.7.2</a><br/>
<a href="#4.7.1">4.7.1</a><br/>
<a href="#4.7.0">4.7.0</a><br/>
<a href="#4.6.2">4.6.2</a><br/>
<a href="#4.6.1">4.6.1</a><br/>
<a href="#4.6.0">4.6.0</a><br/>
<a href="#4.5.0">4.5.0</a><br/>
<a href="#4.4.7">4.4.7</a><br/>
<a href="#4.4.6">4.4.6</a><br/>
<a href="#4.4.5">4.4.5</a><br/>
<a href="#4.4.4">4.4.4</a><br/>
<a href="#4.4.3">4.4.3</a><br/>
<a href="#4.4.2">4.4.2</a><br/>
<a href="#4.4.1">4.4.1</a><br/>
<a href="#4.4.0">4.4.0</a><br/>
<a href="#4.3.2">4.3.2</a><br/>
<a href="#4.3.1">4.3.1</a><br/>
<a href="#4.3.0">4.3.0</a><br/>
<a href="#4.2.6">4.2.6</a><br/>
<a href="#4.2.5">4.2.5</a><br/>
<a href="#4.2.4">4.2.4</a><br/>
<a href="#4.2.3">4.2.3</a><br/>
<a href="#4.2.2">4.2.2</a><br/>
<a href="#4.2.1">4.2.1</a><br/>
<a href="#4.2.0">4.2.0</a><br/>
</td>
<td valign="top">
<a href="#4.1.2">4.1.2</a><br/>
<a href="#4.1.1">4.1.1</a><br/>
<a href="#4.1.0">4.1.0</a><br/>
<a href="#4.0.0">4.0.0</a><br/>
</td>
</tr>
</table>

* Other Versions
  * [22.x](CHANGELOG_V22.md)
  * [21.x](CHANGELOG_V21.md)
  * [20.x](CHANGELOG_V20.md)
  * [19.x](CHANGELOG_V19.md)
  * [18.x](CHANGELOG_V18.md)
  * [17.x](CHANGELOG_V17.md)
  * [16.x](CHANGELOG_V16.md)
  * [15.x](CHANGELOG_V15.md)
  * [14.x](CHANGELOG_V14.md)
  * [13.x](CHANGELOG_V13.md)
  * [12.x](CHANGELOG_V12.md)
  * [11.x](CHANGELOG_V11.md)
  * [10.x](CHANGELOG_V10.md)
  * [9.x](CHANGELOG_V9.md)
  * [8.x](CHANGELOG_V8.md)
  * [7.x](CHANGELOG_V7.md)
  * [6.x](CHANGELOG_V6.md)
  * [5.x](CHANGELOG_V5.md)
  * [0.12.x](CHANGELOG_V012.md)
  * [0.10.x](CHANGELOG_V010.md)
  * [io.js](CHANGELOG_IOJS.md)
  * [Archive](CHANGELOG_ARCHIVE.md)

_Note_: Node.js v4 is covered by the
[Node.js Long Term Support Plan](https://github.com/nodejs/LTS) and
will be supported actively until April 2017 and maintained until April 2018.

<a id="4.9.1"></a>

## 2018-03-29, Version 4.9.1 'Argon' (Maintenance), @MylesBorins

### Notable Changes

No additional commits.

Due to incorrect staging of the upgrade to the GCC 4.9.X compiler, the latest releases for PPC little
endian were built using GCC 4.9.X instead of GCC 4.8.X. This caused an ABI breakage on PPCLE based
environments. This has been fixed in our infrastructure and we are doing this release to ensure that
the hosted binaries are adhering to our platform support contract.

<a id="4.9.0"></a>

## 2018-03-28, Version 4.9.0 'Argon' (Maintenance), @MylesBorins

This is a security release. All Node.js users should consult the security release summary at <https://nodejs.org/en/blog/vulnerability/march-2018-security-releases/> for details on patched vulnerabilities.

Fixes for the following CVEs are included in this release:

* CVE-2018-7158
* CVE-2018-7159

### Notable Changes

* **Upgrade to OpenSSL 1.0.2o**: Does not contain any security fixes that are known to impact Node.js.
* **Fix for `'path'` module regular expression denial of service (CVE-2018-7158)**: A regular expression used for parsing POSIX an Windows paths could be used to cause a denial of service if an attacker were able to have a specially crafted path string passed through one of the impacted `'path'` module functions.
* **Reject spaces in HTTP `Content-Length` header values (CVE-2018-7159)**: The Node.js HTTP parser allowed for spaces inside `Content-Length` header values. Such values now lead to rejected connections in the same way as non-numeric values.
* **Update root certificates**: 5 additional root certificates have been added to the Node.js binary and 30 have been removed.

### Commits

* \[[`497ff3cd4f`](https://github.com/nodejs/node/commit/497ff3cd4f)] - **crypto**: update root certificates (Ben Noordhuis) [#19322](https://github.com/nodejs/node/pull/19322)
* \[[`514709e41f`](https://github.com/nodejs/node/commit/514709e41f)] - **deps**: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) [nodejs/io.js#1836](https://github.com/nodejs/io.js/pull/1836)
* \[[`5108108606`](https://github.com/nodejs/node/commit/5108108606)] - **deps**: fix asm build error of openssl in x86\_win32 (Shigeki Ohtsu) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`d67d0a63d9`](https://github.com/nodejs/node/commit/d67d0a63d9)] - **deps**: fix openssl assembly error on ia32 win32 (Fedor Indutny) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`6af057ecc8`](https://github.com/nodejs/node/commit/6af057ecc8)] - **deps**: copy all openssl header files to include dir (Shigeki Ohtsu) [#19638](https://github.com/nodejs/node/pull/19638)
* \[[`b50cd3359d`](https://github.com/nodejs/node/commit/b50cd3359d)] - **deps**: upgrade openssl sources to 1.0.2o (Shigeki Ohtsu) [#19638](https://github.com/nodejs/node/pull/19638)
* \[[`da6e24c8d6`](https://github.com/nodejs/node/commit/da6e24c8d6)] - **deps**: reject interior blanks in Content-Length (Ben Noordhuis) [nodejs-private/http-parser-private#1](https://github.com/nodejs-private/http-parser-private/pull/1)
* \[[`7ebc9981e0`](https://github.com/nodejs/node/commit/7ebc9981e0)] - **deps**: upgrade http-parser to v2.8.0 (Ben Noordhuis) [nodejs-private/http-parser-private#1](https://github.com/nodejs-private/http-parser-private/pull/1)
* \[[`6fd2cc93a6`](https://github.com/nodejs/node/commit/6fd2cc93a6)] - **openssl**: fix keypress requirement in apps on win32 (Shigeki Ohtsu) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`bf00665af6`](https://github.com/nodejs/node/commit/bf00665af6)] - **path**: unwind regular expressions in Windows (Myles Borins)
* \[[`4196fcf23e`](https://github.com/nodejs/node/commit/4196fcf23e)] - **path**: unwind regular expressions in POSIX (Myles Borins)
* \[[`625986b699`](https://github.com/nodejs/node/commit/625986b699)] - **src**: drop CNNIC+StartCom certificate whitelisting (Ben Noordhuis) [#19322](https://github.com/nodejs/node/pull/19322)
* \[[`ebc46448a4`](https://github.com/nodejs/node/commit/ebc46448a4)] - **tools**: update certdata.txt (Ben Noordhuis) [#19322](https://github.com/nodejs/node/pull/19322)

<a id="4.8.7"></a>

## 2017-12-08, Version 4.8.7 'Argon' (Maintenance), @MylesBorins

This is a security release. All Node.js users should consult the security release summary at <https://nodejs.org/en/blog/vulnerability/december-2017-security-releases/> for details on patched vulnerabilities.

Fixes for the following CVEs are included in this release:

* CVE-2017-15896
* CVE-2017-3738 (from the openssl project)

### Notable Changes

* **deps**:
  * openssl updated to 1.0.2n (Shigeki Ohtsu) [#17526](https://github.com/nodejs/node/pull/17526)

### Commits

* \[[`4f8fae3493`](https://github.com/nodejs/node/commit/4f8fae3493)] - **deps**: update openssl asm and asm\_obsolete files (Shigeki Ohtsu) [#17526](https://github.com/nodejs/node/pull/17526)
* \[[`eacd090e7b`](https://github.com/nodejs/node/commit/eacd090e7b)] - **deps**: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) [nodejs/io.js#1836](https://github.com/nodejs/io.js/pull/1836)
* \[[`3e6b0b0d13`](https://github.com/nodejs/node/commit/3e6b0b0d13)] - **deps**: fix asm build error of openssl in x86\_win32 (Shigeki Ohtsu) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`b0ed4c52af`](https://github.com/nodejs/node/commit/b0ed4c52af)] - **deps**: fix openssl assembly error on ia32 win32 (Fedor Indutny) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`dd6a2dff1e`](https://github.com/nodejs/node/commit/dd6a2dff1e)] - **deps**: copy all openssl header files to include dir (Shigeki Ohtsu) [#17526](https://github.com/nodejs/node/pull/17526)
* \[[`b3afedfbe9`](https://github.com/nodejs/node/commit/b3afedfbe9)] - **deps**: upgrade openssl sources to 1.0.2n (Shigeki Ohtsu) [#17526](https://github.com/nodejs/node/pull/17526)
* \[[`f7eb162d0d`](https://github.com/nodejs/node/commit/f7eb162d0d)] - **openssl**: fix keypress requirement in apps on win32 (Shigeki Ohtsu) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)

<a id="4.8.6"></a>

## 2017-11-07, Version 4.8.6 'Argon' (Maintenance), @MylesBorins

This Maintenance release comes with 47 commits. This includes 26 commits which are updates to dependencies,
8 which are build / tool related, 4 which are doc related, and 2 which are test related.

This release includes a security update to openssl that has been deemed low severity for the Node.js project.

### Notable Changes

* **crypto**:
  * update root certificates (Ben Noordhuis) [#13279](https://github.com/nodejs/node/pull/13279)
  * update root certificates (Ben Noordhuis) [#12402](https://github.com/nodejs/node/pull/12402)
* **deps**:
  * add support for more modern versions of INTL (Bruno Pagani) [#13040](https://github.com/nodejs/node/pull/13040)
  * upgrade openssl sources to 1.0.2m (Shigeki Ohtsu) [#16691](https://github.com/nodejs/node/pull/16691)
  * upgrade openssl sources to 1.0.2l (Daniel Bevenius) [#13233](https://github.com/nodejs/node/pull/13233)

### Commits

* \[[`e064ae62e4`](https://github.com/nodejs/node/commit/e064ae62e4)] - **build**: fix make test-v8 (Ben Noordhuis) [#15562](https://github.com/nodejs/node/pull/15562)
* \[[`a7f7a87a1b`](https://github.com/nodejs/node/commit/a7f7a87a1b)] - **build**: run test-hash-seed at the end of test-v8 (Michaël Zasso) [#14219](https://github.com/nodejs/node/pull/14219)
* \[[`05e8b1b7d9`](https://github.com/nodejs/node/commit/05e8b1b7d9)] - **build**: codesign tarball binary on macOS (Evan Lucas) [#14179](https://github.com/nodejs/node/pull/14179)
* \[[`e2b6fdf93e`](https://github.com/nodejs/node/commit/e2b6fdf93e)] - **build**: avoid /docs/api and /docs/doc/api upload (Rod Vagg) [#12957](https://github.com/nodejs/node/pull/12957)
* \[[`59d35c0775`](https://github.com/nodejs/node/commit/59d35c0775)] - **build,tools**: do not force codesign prefix (Evan Lucas) [#14179](https://github.com/nodejs/node/pull/14179)
* \[[`210fa72e9e`](https://github.com/nodejs/node/commit/210fa72e9e)] - **crypto**: update root certificates (Ben Noordhuis) [#13279](https://github.com/nodejs/node/pull/13279)
* \[[`752b46a259`](https://github.com/nodejs/node/commit/752b46a259)] - **crypto**: update root certificates (Ben Noordhuis) [#12402](https://github.com/nodejs/node/pull/12402)
* \[[`3640ba4acb`](https://github.com/nodejs/node/commit/3640ba4acb)] - **crypto**: clear err stack after ECDH::BufferToPoint (Ryan Kelly) [#13275](https://github.com/nodejs/node/pull/13275)
* \[[`545235fc4b`](https://github.com/nodejs/node/commit/545235fc4b)] - **deps**: add missing #include "unicode/normlzr.h" (Bruno Pagani) [#13040](https://github.com/nodejs/node/pull/13040)
* \[[`ea09a1c3e6`](https://github.com/nodejs/node/commit/ea09a1c3e6)] - **deps**: update openssl asm and asm\_obsolete files (Shigeki Ohtsu) [#16691](https://github.com/nodejs/node/pull/16691)
* \[[`68661a95b5`](https://github.com/nodejs/node/commit/68661a95b5)] - **deps**: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) [nodejs/io.js#1836](https://github.com/nodejs/io.js/pull/1836)
* \[[`bdcb2525fb`](https://github.com/nodejs/node/commit/bdcb2525fb)] - **deps**: fix asm build error of openssl in x86\_win32 (Shigeki Ohtsu) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`3f93ffee89`](https://github.com/nodejs/node/commit/3f93ffee89)] - **deps**: fix openssl assembly error on ia32 win32 (Fedor Indutny) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`16fbd9da0d`](https://github.com/nodejs/node/commit/16fbd9da0d)] - **deps**: copy all openssl header files to include dir (Shigeki Ohtsu) [#16691](https://github.com/nodejs/node/pull/16691)
* \[[`55e15ec820`](https://github.com/nodejs/node/commit/55e15ec820)] - **deps**: upgrade openssl sources to 1.0.2m (Shigeki Ohtsu) [#16691](https://github.com/nodejs/node/pull/16691)
* \[[`9c3e246ffe`](https://github.com/nodejs/node/commit/9c3e246ffe)] - **deps**: backport 4e18190 from V8 upstream (jshin) [#15562](https://github.com/nodejs/node/pull/15562)
* \[[`43d1ac3a62`](https://github.com/nodejs/node/commit/43d1ac3a62)] - **deps**: backport bff3074 from V8 upstream (Myles Borins) [#15562](https://github.com/nodejs/node/pull/15562)
* \[[`b259fd3bd5`](https://github.com/nodejs/node/commit/b259fd3bd5)] - **deps**: cherry pick d7f813b4 from V8 upstream (akos.palfi) [#15562](https://github.com/nodejs/node/pull/15562)
* \[[`85800c4ba4`](https://github.com/nodejs/node/commit/85800c4ba4)] - **deps**: backport e28183b5 from upstream V8 (karl) [#15562](https://github.com/nodejs/node/pull/15562)
* \[[`06eb181916`](https://github.com/nodejs/node/commit/06eb181916)] - **deps**: update openssl asm and asm\_obsolete files (Daniel Bevenius) [#13233](https://github.com/nodejs/node/pull/13233)
* \[[`c0fe1fccc3`](https://github.com/nodejs/node/commit/c0fe1fccc3)] - **deps**: update openssl config files (Daniel Bevenius) [#13233](https://github.com/nodejs/node/pull/13233)
* \[[`523eb60424`](https://github.com/nodejs/node/commit/523eb60424)] - **deps**: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) [nodejs/io.js#1836](https://github.com/nodejs/io.js/pull/1836)
* \[[`0aacd5a8cd`](https://github.com/nodejs/node/commit/0aacd5a8cd)] - **deps**: fix asm build error of openssl in x86\_win32 (Shigeki Ohtsu) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`80c48c0720`](https://github.com/nodejs/node/commit/80c48c0720)] - **deps**: fix openssl assembly error on ia32 win32 (Fedor Indutny) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`bbd92b4676`](https://github.com/nodejs/node/commit/bbd92b4676)] - **deps**: copy all openssl header files to include dir (Daniel Bevenius) [#13233](https://github.com/nodejs/node/pull/13233)
* \[[`8507f0fb5d`](https://github.com/nodejs/node/commit/8507f0fb5d)] - **deps**: upgrade openssl sources to 1.0.2l (Daniel Bevenius) [#13233](https://github.com/nodejs/node/pull/13233)
* \[[`9bfada8f0c`](https://github.com/nodejs/node/commit/9bfada8f0c)] - **deps**: add example of comparing OpenSSL changes (Daniel Bevenius) [#13234](https://github.com/nodejs/node/pull/13234)
* \[[`71f9cdf241`](https://github.com/nodejs/node/commit/71f9cdf241)] - **deps**: cherry-pick 09db540,686558d from V8 upstream (Jesse Rosenberger) [#14829](https://github.com/nodejs/node/pull/14829)
* \[[`751f1ac08e`](https://github.com/nodejs/node/commit/751f1ac08e)] - _**Revert**_ "**deps**: backport e093a04, 09db540 from upstream V8" (Jesse Rosenberger) [#14829](https://github.com/nodejs/node/pull/14829)
* \[[`ed6298c7de`](https://github.com/nodejs/node/commit/ed6298c7de)] - **deps**: cherry-pick 18ea996 from c-ares upstream (Anna Henningsen) [#13883](https://github.com/nodejs/node/pull/13883)
* \[[`639180adfa`](https://github.com/nodejs/node/commit/639180adfa)] - **deps**: update openssl asm and asm\_obsolete files (Shigeki Ohtsu) [#12913](https://github.com/nodejs/node/pull/12913)
* \[[`9ba73e1797`](https://github.com/nodejs/node/commit/9ba73e1797)] - **deps**: cherry-pick 4ae5993 from upstream OpenSSL (Shigeki Ohtsu) [#12913](https://github.com/nodejs/node/pull/12913)
* \[[`f8e282e51c`](https://github.com/nodejs/node/commit/f8e282e51c)] - **doc**: fix typo in zlib.md (Luigi Pinca) [#16480](https://github.com/nodejs/node/pull/16480)
* \[[`532a2941cb`](https://github.com/nodejs/node/commit/532a2941cb)] - **doc**: add missing make command to UPGRADING.md (Daniel Bevenius) [#13233](https://github.com/nodejs/node/pull/13233)
* \[[`1db33296cb`](https://github.com/nodejs/node/commit/1db33296cb)] - **doc**: add entry for subprocess.killed property (Rich Trott) [#14578](https://github.com/nodejs/node/pull/14578)
* \[[`0fa09dfd77`](https://github.com/nodejs/node/commit/0fa09dfd77)] - **doc**: change `child` to `subprocess` (Rich Trott) [#14578](https://github.com/nodejs/node/pull/14578)
* \[[`43bbfafaef`](https://github.com/nodejs/node/commit/43bbfafaef)] - **docs**: Fix broken links in crypto.md (Zuzana Svetlikova) [#15182](https://github.com/nodejs/node/pull/15182)
* \[[`1bde7f5cef`](https://github.com/nodejs/node/commit/1bde7f5cef)] - **openssl**: fix keypress requirement in apps on win32 (Shigeki Ohtsu) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`e69f47b686`](https://github.com/nodejs/node/commit/e69f47b686)] - **openssl**: fix keypress requirement in apps on win32 (Shigeki Ohtsu) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`cb92f93cd5`](https://github.com/nodejs/node/commit/cb92f93cd5)] - **test**: remove internal headers from addons (Gibson Fahnestock) [#7947](https://github.com/nodejs/node/pull/7947)
* \[[`5d9164c315`](https://github.com/nodejs/node/commit/5d9164c315)] - **test**: move test-cluster-debug-port to sequential (Oleksandr Kushchak) [#16292](https://github.com/nodejs/node/pull/16292)
* \[[`07c912e849`](https://github.com/nodejs/node/commit/07c912e849)] - **tools**: update certdata.txt (Ben Noordhuis) [#13279](https://github.com/nodejs/node/pull/13279)
* \[[`c40bffcb88`](https://github.com/nodejs/node/commit/c40bffcb88)] - **tools**: update certdata.txt (Ben Noordhuis) [#12402](https://github.com/nodejs/node/pull/12402)
* \[[`161162713f`](https://github.com/nodejs/node/commit/161162713f)] - **tools**: be explicit about including key-id (Myles Borins) [#13309](https://github.com/nodejs/node/pull/13309)
* \[[`0c820c092b`](https://github.com/nodejs/node/commit/0c820c092b)] - **v8**: fix stack overflow in recursive method (Ben Noordhuis) [#12460](https://github.com/nodejs/node/pull/12460)
* \[[`a1f992975f`](https://github.com/nodejs/node/commit/a1f992975f)] - **zlib**: fix crash when initializing failed (Anna Henningsen) [#14666](https://github.com/nodejs/node/pull/14666)
* \[[`31bf595b94`](https://github.com/nodejs/node/commit/31bf595b94)] - **zlib**: fix node crashing on invalid options (Alexey Orlenko) [#13098](https://github.com/nodejs/node/pull/13098)

<a id="4.8.5"></a>

## 2017-10-24, Version 4.8.5 'Argon' (Maintenance), @MylesBorins

This is a security release. All Node.js users should consult the security release summary at <https://nodejs.org/en/blog/vulnerability/oct-2017-dos/> for details on patched vulnerabilities.

### Notable Changes

* **zlib**:
  * CVE-2017-14919 - In zlib v1.2.9, a change was made that causes an error to be raised when a raw deflate stream is initialized with windowBits set to 8. On some versions this crashes Node and you cannot recover from it, while on some versions it throws an exception. Node.js will now gracefully set windowBits to 9 replicating the legacy behavior to avoid a DOS vector. [nodejs-private/node-private#95](https://github.com/nodejs-private/node-private/pull/95)

### Commits

* \[[`f5defa2a7c`](https://github.com/nodejs/node/commit/733578bb2e)] - **zlib**: gracefully set windowBits from 8 to 9 (Myles Borins) [nodejs-private/node-private#95](https://github.com/nodejs-private/node-private/pull/95)

<a id="4.8.4"></a>

## 2017-07-11, Version 4.8.4 'Argon' (Maintenance), @MylesBorins

This is a security release. All Node.js users should consult the security release summary at <https://nodejs.org/en/blog/vulnerability/july-2017-security-releases/> for details on patched vulnerabilities.

### Notable Changes

* **build**:
  * Disable V8 snapshots - The hashseed embedded in the snapshot is currently the same for all runs of the binary. This opens node up to collision attacks which could result in a Denial of Service. We have temporarily disabled snapshots until a more robust solution is found (Ali Ijaz Sheikh)
* **deps**:
  * CVE-2017-1000381 - The c-ares function ares\_parse\_naptr\_reply(), which is used for parsing NAPTR responses, could be triggered to read memory outside of the given input buffer if the passed in DNS response packet was crafted in a particular way. This patch checks that there is enough data for the required elements of an NAPTR record (2 int16, 3 bytes for string lengths) before processing a record. (David Drysdale)

### Commits

* \[[`9d51bdc9d4`](https://github.com/nodejs/node/commit/9d51bdc9d4)] - **build**: disable V8 snapshots (Ali Ijaz Sheikh) [nodejs/node-private#84](https://github.com/nodejs/node-private/pull/84)
* \[[`80fe2662e4`](https://github.com/nodejs/node/commit/80fe2662e4)] - **deps**: cherry-pick 9478908a49 from cares upstream (David Drysdale) [nodejs/node-private#88](https://github.com/nodejs/node-private/pull/88)
* \[[`d6969a717f`](https://github.com/nodejs/node/commit/d6969a717f)] - **http**: use Buffer.from to avoid Buffer(num) call (Сковорода Никита Андреевич) [nodejs/node-private#83](https://github.com/nodejs/node-private/pull/83)
* \[[`58a8f150e5`](https://github.com/nodejs/node/commit/58a8f150e5)] - **test**: verify hash seed uniqueness (Ali Ijaz Sheikh) [nodejs/node-private#84](https://github.com/nodejs/node-private/pull/84)

<a id="4.8.3"></a>

## 2017-05-02, Version 4.8.3 'Argon' (Maintenance), @MylesBorins

### Notable Changes

* **module**:
  * The [module loading global fallback](https://nodejs.org/dist/latest-v4.x/docs/api/modules.html#modules_loading_from_the_global_folders) to the Node executable's directory now works correctly on Windows.  (Richard Lau) [#9283](https://github.com/nodejs/node/pull/9283)
* **src**:
  * fix base64 decoding in rare edgecase (Nikolai Vavilov) [#11995](https://github.com/nodejs/node/pull/11995)
* **tls**:
  * fix rare segmentation faults when using TLS
    * (Trevor Norris) [#11947](https://github.com/nodejs/node/pull/11947)
    * (Ben Noordhuis) [#11898](https://github.com/nodejs/node/pull/11898)
    * (jBarz) [#11776](https://github.com/nodejs/node/pull/11776)

### Commits

* \[[`44260806a6`](https://github.com/nodejs/node/commit/44260806a6)] - Partial revert "tls: keep track of stream that is closed" (Trevor Norris) [#11947](https://github.com/nodejs/node/pull/11947)
* \[[`ab3fdf531f`](https://github.com/nodejs/node/commit/ab3fdf531f)] - **deps**: cherry-pick ca0f9573 from V8 upstream (Ali Ijaz Sheikh) [#11940](https://github.com/nodejs/node/pull/11940)
* \[[`07b92a3c0b`](https://github.com/nodejs/node/commit/07b92a3c0b)] - **doc**: add supported platforms list for v4.x (Michael Dawson) [#12091](https://github.com/nodejs/node/pull/12091)
* \[[`ba91c41478`](https://github.com/nodejs/node/commit/ba91c41478)] - **module**: fix loading from global folders on Windows (Richard Lau) [#9283](https://github.com/nodejs/node/pull/9283)
* \[[`b5b78b12b8`](https://github.com/nodejs/node/commit/b5b78b12b8)] - **src**: add fcntl.h include to node.cc (Bartosz Sosnowski) [#12540](https://github.com/nodejs/node/pull/12540)
* \[[`eb393f9ae1`](https://github.com/nodejs/node/commit/eb393f9ae1)] - **src**: fix base64 decoding (Nikolai Vavilov) [#11995](https://github.com/nodejs/node/pull/11995)
* \[[`8ed18a1429`](https://github.com/nodejs/node/commit/8ed18a1429)] - **src**: ensure that fd 0-2 are valid on windows (Bartosz Sosnowski) [#11863](https://github.com/nodejs/node/pull/11863)
* \[[`ff1d61c11b`](https://github.com/nodejs/node/commit/ff1d61c11b)] - **stream\_base,tls\_wrap**: notify on destruct (Trevor Norris) [#11947](https://github.com/nodejs/node/pull/11947)
* \[[`6040efd7dc`](https://github.com/nodejs/node/commit/6040efd7dc)] - **test**: fix flaky test-tls-wrap-timeout (Rich Trott) [#7857](https://github.com/nodejs/node/pull/7857)
* \[[`7a1920dc84`](https://github.com/nodejs/node/commit/7a1920dc84)] - **test**: add hasCrypto check to tls-socket-close (Daniel Bevenius) [#11911](https://github.com/nodejs/node/pull/11911)
* \[[`1dc6b38dcf`](https://github.com/nodejs/node/commit/1dc6b38dcf)] - **test**: add test for loading from global folders (Richard Lau) [#9283](https://github.com/nodejs/node/pull/9283)
* \[[`54f5258582`](https://github.com/nodejs/node/commit/54f5258582)] - **tls**: fix segfault on destroy after partial read (Ben Noordhuis) [#11898](https://github.com/nodejs/node/pull/11898)
* \[[`99749dccfe`](https://github.com/nodejs/node/commit/99749dccfe)] - **tls**: keep track of stream that is closed (jBarz) [#11776](https://github.com/nodejs/node/pull/11776)
* \[[`6d3aaa72a8`](https://github.com/nodejs/node/commit/6d3aaa72a8)] - **tls**: TLSSocket emits 'error' on handshake failure (Mariusz 'koder' Chwalba) [#8805](https://github.com/nodejs/node/pull/8805)

<a id="4.8.2"></a>

## 2017-04-04, Version 4.8.2 'Argon' (Maintenance), @MylesBorins

This is a maintenance release to fix a memory leak that was introduced in 4.8.1.

It also includes an upgrade to zlib 1.2.11 to fix a [number of low severity CVEs](http://seclists.org/oss-sec/2016/q4/602)
that were present in zlib 1.2.8.

### Notable Changes

* **crypto**:
  * fix memory leak if certificate is revoked (Tom Atkinson) [#12089](https://github.com/nodejs/node/pull/12089)
* **deps**:
  * upgrade zlib to 1.2.11 (Sam Roberts) [#10980](https://github.com/nodejs/node/pull/10980)

### Commits

* \[[`9d7fba4de2`](https://github.com/nodejs/node/commit/9d7fba4de2)] - **crypto**: fix memory leak if certificate is revoked (Tom Atkinson) [#12089](https://github.com/nodejs/node/pull/12089)
* \[[`253980ff38`](https://github.com/nodejs/node/commit/253980ff38)] - **deps**: fix CLEAR\_HASH macro to be usable as a single statement (Sam Roberts) [#11616](https://github.com/nodejs/node/pull/11616)
* \[[`2e52a2699b`](https://github.com/nodejs/node/commit/2e52a2699b)] - **deps**: upgrade zlib to 1.2.11 (Sam Roberts) [#10980](https://github.com/nodejs/node/pull/10980)

<a id="4.8.1"></a>

## 2017-03-21, Version 4.8.1 'Argon' (LTS), @MylesBorins

This LTS release comes with 147 commits. This includes 55 which are test related,
41 which are doc related,  11 which are build / tool related,
and 1 commits which are updates to dependencies.

### Notable Changes

* **buffer**: The performance of `.toJSON()` is now up to 2859% faster on average. (Brian White) [#10895](https://github.com/nodejs/node/pull/10895)
* **IPC**: Batched writes have been enabled for process IPC on platforms that support Unix Domain Sockets. (Alexey Orlenko) [#10677](https://github.com/nodejs/node/pull/10677)
  * Performance gains may be up to 40% for some workloads.
* **http**:
  * Control characters are now always rejected when using `http.request()`. (Ben Noordhuis) [#8923](https://github.com/nodejs/node/pull/8923)
* **node**: Heap statistics now support values larger than 4GB. (Ben Noordhuis) [#10186](https://github.com/nodejs/node/pull/10186)

### Commits

* \[[`77f23ec5af`](https://github.com/nodejs/node/commit/77f23ec5af)] - **assert**: unlock the assert API (Rich Trott) [#11304](https://github.com/nodejs/node/pull/11304)
* \[[`090037a41a`](https://github.com/nodejs/node/commit/090037a41a)] - **assert**: remove unneeded condition (Rich Trott) [#11314](https://github.com/nodejs/node/pull/11314)
* \[[`75af859af7`](https://github.com/nodejs/node/commit/75af859af7)] - **assert**: apply minor refactoring (Rich Trott) [#11511](https://github.com/nodejs/node/pull/11511)
* \[[`994f562858`](https://github.com/nodejs/node/commit/994f562858)] - **assert**: update comments (Kai Cataldo) [#10579](https://github.com/nodejs/node/pull/10579)
* \[[`14e57c1102`](https://github.com/nodejs/node/commit/14e57c1102)] - **benchmark**: add more thorough timers benchmarks (Jeremiah Senkpiel) [#10925](https://github.com/nodejs/node/pull/10925)
* \[[`850f85d96e`](https://github.com/nodejs/node/commit/850f85d96e)] - **benchmark**: add benchmark for object properties (Michaël Zasso) [#10949](https://github.com/nodejs/node/pull/10949)
* \[[`626875f2e4`](https://github.com/nodejs/node/commit/626875f2e4)] - **benchmark**: don't lint autogenerated modules (Brian White) [#10756](https://github.com/nodejs/node/pull/10756)
* \[[`9da6ebd73f`](https://github.com/nodejs/node/commit/9da6ebd73f)] - **benchmark**: add dgram bind(+/- params) benchmark (Vse Mozhet Byt) [#11313](https://github.com/nodejs/node/pull/11313)
* \[[`a597c11ba4`](https://github.com/nodejs/node/commit/a597c11ba4)] - **benchmark**: improve readability of net benchmarks (Brian White) [#10446](https://github.com/nodejs/node/pull/10446)
* \[[`22c25dee92`](https://github.com/nodejs/node/commit/22c25dee92)] - **buffer**: improve toJSON() performance (Brian White) [#10895](https://github.com/nodejs/node/pull/10895)
* \[[`af3c21197d`](https://github.com/nodejs/node/commit/af3c21197d)] - **build**: move source files from headers section (Daniel Bevenius) [#10850](https://github.com/nodejs/node/pull/10850)
* \[[`4bb61553f0`](https://github.com/nodejs/node/commit/4bb61553f0)] - **build**: disable C4267 conversion compiler warning (Ben Noordhuis) [#11205](https://github.com/nodejs/node/pull/11205)
* \[[`6a45ac0ea9`](https://github.com/nodejs/node/commit/6a45ac0ea9)] - **build**: fix newlines in addon build output (Brian White) [#11466](https://github.com/nodejs/node/pull/11466)
* \[[`bfc553d55d`](https://github.com/nodejs/node/commit/bfc553d55d)] - **build**: fail on CI if leftover processes (Rich Trott) [#11269](https://github.com/nodejs/node/pull/11269)
* \[[`094bfe66aa`](https://github.com/nodejs/node/commit/094bfe66aa)] - **build**: fix node\_g target (Daniel Bevenius) [#10153](https://github.com/nodejs/node/pull/10153)
* \[[`87db4f7225`](https://github.com/nodejs/node/commit/87db4f7225)] - **build**: Don't regenerate node symlink (sxa555) [#9827](https://github.com/nodejs/node/pull/9827)
* \[[`e0dc0ceb37`](https://github.com/nodejs/node/commit/e0dc0ceb37)] - **build**: don't squash signal handlers with --shared (Stewart X Addison) [#10539](https://github.com/nodejs/node/pull/10539)
* \[[`4676eec382`](https://github.com/nodejs/node/commit/4676eec382)] - **child\_process**: remove empty if condition (cjihrig) [#11427](https://github.com/nodejs/node/pull/11427)
* \[[`2b867d2ae5`](https://github.com/nodejs/node/commit/2b867d2ae5)] - **child\_process**: refactor internal/child\_process.js (Arseniy Maximov) [#11366](https://github.com/nodejs/node/pull/11366)
* \[[`c9a92ff494`](https://github.com/nodejs/node/commit/c9a92ff494)] - **crypto**: return the retval of HMAC\_Update (Travis Meisenheimer) [#10891](https://github.com/nodejs/node/pull/10891)
* \[[`9c53e402d7`](https://github.com/nodejs/node/commit/9c53e402d7)] - **crypto**: freelist\_max\_len is gone in OpenSSL 1.1.0 (Adam Langley) [#10859](https://github.com/nodejs/node/pull/10859)
* \[[`c6f6b029a1`](https://github.com/nodejs/node/commit/c6f6b029a1)] - **crypto**: add cert check issued by StartCom/WoSign (Shigeki Ohtsu) [#9469](https://github.com/nodejs/node/pull/9469)
* \[[`c56719f47a`](https://github.com/nodejs/node/commit/c56719f47a)] - **crypto**: Remove expired certs from CNNIC whitelist (Shigeki Ohtsu) [#9469](https://github.com/nodejs/node/pull/9469)
* \[[`b48f6ffc63`](https://github.com/nodejs/node/commit/b48f6ffc63)] - **crypto**: use CHECK\_NE instead of ABORT or abort (Sam Roberts) [#10413](https://github.com/nodejs/node/pull/10413)
* \[[`35a660ee70`](https://github.com/nodejs/node/commit/35a660ee70)] - **crypto**: fix handling of root\_cert\_store. (Adam Langley) [#9409](https://github.com/nodejs/node/pull/9409)
* \[[`3516f35b77`](https://github.com/nodejs/node/commit/3516f35b77)] - **deps**: backport 7c3748a from upstream V8 (Cristian Cavalli) [#10873](https://github.com/nodejs/node/pull/10873)
* \[[`f9e121ead8`](https://github.com/nodejs/node/commit/f9e121ead8)] - **dgram**: fix possibly deoptimizing use of arguments (Vse Mozhet Byt)
* \[[`fc2bb2c8ef`](https://github.com/nodejs/node/commit/fc2bb2c8ef)] - **doc**: remove Chris Dickinson from active releasers (Ben Noordhuis) [#11011](https://github.com/nodejs/node/pull/11011)
* \[[`725a89606b`](https://github.com/nodejs/node/commit/725a89606b)] - **doc**: remove duplicate properties bullet in readme (Javis Sullivan) [#10741](https://github.com/nodejs/node/pull/10741)
* \[[`db03294c41`](https://github.com/nodejs/node/commit/db03294c41)] - **doc**: fix typo in http.md (Peter Mescalchin) [#10975](https://github.com/nodejs/node/pull/10975)
* \[[`15188900b8`](https://github.com/nodejs/node/commit/15188900b8)] - **doc**: add who to CC list for dgram (cjihrig) [#11035](https://github.com/nodejs/node/pull/11035)
* \[[`a0742902bd`](https://github.com/nodejs/node/commit/a0742902bd)] - **doc**: correct and complete dgram's Socket.bind docs (Alex Jordan) [#11025](https://github.com/nodejs/node/pull/11025)
* \[[`f464dd837f`](https://github.com/nodejs/node/commit/f464dd837f)] - **doc**: edit CONTRIBUTING.md for clarity (Rich Trott) [#11045](https://github.com/nodejs/node/pull/11045)
* \[[`07dfed8f45`](https://github.com/nodejs/node/commit/07dfed8f45)] - **doc**: fix confusing example in dns.md (Vse Mozhet Byt) [#11022](https://github.com/nodejs/node/pull/11022)
* \[[`d55d760086`](https://github.com/nodejs/node/commit/d55d760086)] - **doc**: add personal pronouns option (Rich Trott) [#11089](https://github.com/nodejs/node/pull/11089)
* \[[`b86843a463`](https://github.com/nodejs/node/commit/b86843a463)] - **doc**: clarify msg when doc/api/cli.md not updated (Stewart X Addison) [#10872](https://github.com/nodejs/node/pull/10872)
* \[[`c2d70908e6`](https://github.com/nodejs/node/commit/c2d70908e6)] - **doc**: edit stability text for clarity and style (Rich Trott) [#11112](https://github.com/nodejs/node/pull/11112)
* \[[`115448ec94`](https://github.com/nodejs/node/commit/115448ec94)] - **doc**: remove assertions about assert (Rich Trott) [#11113](https://github.com/nodejs/node/pull/11113)
* \[[`e90317d739`](https://github.com/nodejs/node/commit/e90317d739)] - **doc**: fix "initial delay" link in http.md (Timo Tijhof) [#11108](https://github.com/nodejs/node/pull/11108)
* \[[`788d736ab6`](https://github.com/nodejs/node/commit/788d736ab6)] - **doc**: typographical fixes in COLLABORATOR\_GUIDE.md (Anna Henningsen) [#11163](https://github.com/nodejs/node/pull/11163)
* \[[`2016aa4e07`](https://github.com/nodejs/node/commit/2016aa4e07)] - **doc**: add not-an-aardvark as ESLint contact (Rich Trott) [#11169](https://github.com/nodejs/node/pull/11169)
* \[[`2b6ee39264`](https://github.com/nodejs/node/commit/2b6ee39264)] - **doc**: improve testing guide (Joyee Cheung) [#11150](https://github.com/nodejs/node/pull/11150)
* \[[`aae768c599`](https://github.com/nodejs/node/commit/aae768c599)] - **doc**: remove extraneous paragraph from assert doc (Rich Trott) [#11174](https://github.com/nodejs/node/pull/11174)
* \[[`ca4b2f6154`](https://github.com/nodejs/node/commit/ca4b2f6154)] - **doc**: fix typo in dgram doc (Rich Trott) [#11186](https://github.com/nodejs/node/pull/11186)
* \[[`bb1e97c31a`](https://github.com/nodejs/node/commit/bb1e97c31a)] - **doc**: add and fix System Error properties (Daiki Arai) [#10986](https://github.com/nodejs/node/pull/10986)
* \[[`e1e02efac5`](https://github.com/nodejs/node/commit/e1e02efac5)] - **doc**: clarify the behavior of Buffer.byteLength (Nikolai Vavilov) [#11238](https://github.com/nodejs/node/pull/11238)
* \[[`30d9202f54`](https://github.com/nodejs/node/commit/30d9202f54)] - **doc**: improve consistency in documentation titles (Vse Mozhet Byt) [#11230](https://github.com/nodejs/node/pull/11230)
* \[[`10afa8befc`](https://github.com/nodejs/node/commit/10afa8befc)] - **doc**: drop "and io.js" from release section (Ben Noordhuis) [#11054](https://github.com/nodejs/node/pull/11054)
* \[[`6f1db35e27`](https://github.com/nodejs/node/commit/6f1db35e27)] - **doc**: update email and add personal pronoun (JungMinu) [#11318](https://github.com/nodejs/node/pull/11318)
* \[[`61ac3346ba`](https://github.com/nodejs/node/commit/61ac3346ba)] - **doc**: update code examples in domain.md (Vse Mozhet Byt) [#11110](https://github.com/nodejs/node/pull/11110)
* \[[`0c9ea4fe8b`](https://github.com/nodejs/node/commit/0c9ea4fe8b)] - **doc**: dns examples implied string args were arrays (Sam Roberts) [#11350](https://github.com/nodejs/node/pull/11350)
* \[[`485ec6c180`](https://github.com/nodejs/node/commit/485ec6c180)] - **doc**: change STYLE-GUIDE to STYLE\_GUIDE (Dean Coakley) [#11460](https://github.com/nodejs/node/pull/11460)
* \[[`41bf266b0a`](https://github.com/nodejs/node/commit/41bf266b0a)] - **doc**: add STYLE\_GUIDE (moved from nodejs/docs) (Gibson Fahnestock) [#11321](https://github.com/nodejs/node/pull/11321)
* \[[`6abfcd560b`](https://github.com/nodejs/node/commit/6abfcd560b)] - **doc**: add comment for net.Server's error event (QianJin2013) [#11136](https://github.com/nodejs/node/pull/11136)
* \[[`f4bc12dd11`](https://github.com/nodejs/node/commit/f4bc12dd11)] - **doc**: note message event listeners ref IPC channels (Diego Rodríguez Baquero) [#11494](https://github.com/nodejs/node/pull/11494)
* \[[`09c9105a79`](https://github.com/nodejs/node/commit/09c9105a79)] - **doc**: argument types for assert methods (Amelia Clarke) [#11548](https://github.com/nodejs/node/pull/11548)
* \[[`d622b67302`](https://github.com/nodejs/node/commit/d622b67302)] - **doc**: document clientRequest.aborted (Zach Bjornson) [#11544](https://github.com/nodejs/node/pull/11544)
* \[[`d0dbf12884`](https://github.com/nodejs/node/commit/d0dbf12884)] - **doc**: update TheAlphaNerd to MylesBorins (Myles Borins) [#10586](https://github.com/nodejs/node/pull/10586)
* \[[`05273c5a4e`](https://github.com/nodejs/node/commit/05273c5a4e)] - **doc**: update AUTHORS list to fix name (Noah Rose Ledesma) [#10945](https://github.com/nodejs/node/pull/10945)
* \[[`79f700c891`](https://github.com/nodejs/node/commit/79f700c891)] - **doc**: add TimothyGu to collaborators (Timothy Gu) [#10954](https://github.com/nodejs/node/pull/10954)
* \[[`e656a4244a`](https://github.com/nodejs/node/commit/e656a4244a)] - **doc**: add edsadr to collaborators (Adrian Estrada) [#10883](https://github.com/nodejs/node/pull/10883)
* \[[`6d0e1621e5`](https://github.com/nodejs/node/commit/6d0e1621e5)] - **doc**: clarifying variables in fs.write() (Jessica Quynh Tran) [#9792](https://github.com/nodejs/node/pull/9792)
* \[[`7287dddd69`](https://github.com/nodejs/node/commit/7287dddd69)] - **doc**: add links for zlib convenience methods (Anna Henningsen) [#10829](https://github.com/nodejs/node/pull/10829)
* \[[`b10842ac77`](https://github.com/nodejs/node/commit/b10842ac77)] - **doc**: sort require statements in tests (Sam Roberts) [#10616](https://github.com/nodejs/node/pull/10616)
* \[[`8f0e31b2d9`](https://github.com/nodejs/node/commit/8f0e31b2d9)] - **doc**: add test naming information to guide (Rich Trott) [#10584](https://github.com/nodejs/node/pull/10584)
* \[[`56b779db93`](https://github.com/nodejs/node/commit/56b779db93)] - **doc**: "s/git apply/git am -3" in V8 guide (Myles Borins) [#10665](https://github.com/nodejs/node/pull/10665)
* \[[`3be7a7adb5`](https://github.com/nodejs/node/commit/3be7a7adb5)] - **doc**: update LTS info for current releases (Evan Lucas) [#10720](https://github.com/nodejs/node/pull/10720)
* \[[`530adfdb2a`](https://github.com/nodejs/node/commit/530adfdb2a)] - **doc**: improve rinfo object documentation (Matt Crummey) [#10050](https://github.com/nodejs/node/pull/10050)
* \[[`48b5097ea8`](https://github.com/nodejs/node/commit/48b5097ea8)] - **http**: make request.abort() destroy the socket (Luigi Pinca) [#10818](https://github.com/nodejs/node/pull/10818)
* \[[`15231aa6e5`](https://github.com/nodejs/node/commit/15231aa6e5)] - **http**: reject control characters in http.request() (Ben Noordhuis) [#8923](https://github.com/nodejs/node/pull/8923)
* \[[`fc2cd63998`](https://github.com/nodejs/node/commit/fc2cd63998)] - **lib,src**: support values > 4GB in heap statistics (Ben Noordhuis) [#10186](https://github.com/nodejs/node/pull/10186)
* \[[`533d2bf0a9`](https://github.com/nodejs/node/commit/533d2bf0a9)] - **meta**: add explicit deprecation and semver-major policy (James M Snell) [#7964](https://github.com/nodejs/node/pull/7964)
* \[[`923309adef`](https://github.com/nodejs/node/commit/923309adef)] - **meta**: remove Chris Dickinson from CTC (Chris Dickinson) [#11267](https://github.com/nodejs/node/pull/11267)
* \[[`342c3e2bb4`](https://github.com/nodejs/node/commit/342c3e2bb4)] - **meta**: adding Italo A. Casas PGP Fingerprint (Italo A. Casas) [#11202](https://github.com/nodejs/node/pull/11202)
* \[[`434b00be8a`](https://github.com/nodejs/node/commit/434b00be8a)] - **meta**: decharter the http working group (James M Snell) [#10604](https://github.com/nodejs/node/pull/10604)
* \[[`a7df345921`](https://github.com/nodejs/node/commit/a7df345921)] - **net**: prefer === to == (Arseniy Maximov) [#11513](https://github.com/nodejs/node/pull/11513)
* \[[`396688f075`](https://github.com/nodejs/node/commit/396688f075)] - **readline**: refactor construct Interface (Jackson Tian) [#4740](https://github.com/nodejs/node/pull/4740)
* \[[`a40f8429e6`](https://github.com/nodejs/node/commit/a40f8429e6)] - **readline**: update 6 comparions to strict (Umair Ishaq) [#11078](https://github.com/nodejs/node/pull/11078)
* \[[`90d8e118fb`](https://github.com/nodejs/node/commit/90d8e118fb)] - **src**: add a missing space in node\_os.cc (Alexey Orlenko) [#10931](https://github.com/nodejs/node/pull/10931)
* \[[`279cb09cc3`](https://github.com/nodejs/node/commit/279cb09cc3)] - **src**: enable writev for pipe handles on Unix (Alexey Orlenko) [#10677](https://github.com/nodejs/node/pull/10677)
* \[[`a557d6ce1d`](https://github.com/nodejs/node/commit/a557d6ce1d)] - **src**: unconsume stream fix in internal http impl (Roee Kasher) [#11015](https://github.com/nodejs/node/pull/11015)
* \[[`c4e1af712e`](https://github.com/nodejs/node/commit/c4e1af712e)] - **src**: remove unused typedef (Ben Noordhuis) [#11322](https://github.com/nodejs/node/pull/11322)
* \[[`da2adb7133`](https://github.com/nodejs/node/commit/da2adb7133)] - **src**: update http-parser link (Daniel Bevenius) [#11477](https://github.com/nodejs/node/pull/11477)
* \[[`2f48001574`](https://github.com/nodejs/node/commit/2f48001574)] - **src**: use ABORT() macro instead of abort() (Evan Lucas) [#9613](https://github.com/nodejs/node/pull/9613)
* \[[`a9eb093ce3`](https://github.com/nodejs/node/commit/a9eb093ce3)] - **src**: fix memory leak introduced in 34febfbf4 (Ben Noordhuis) [#9604](https://github.com/nodejs/node/pull/9604)
* \[[`f854d8c789`](https://github.com/nodejs/node/commit/f854d8c789)] - **test**: increase setMulticastLoopback() coverage (cjihrig) [#11277](https://github.com/nodejs/node/pull/11277)
* \[[`1df09f9d37`](https://github.com/nodejs/node/commit/1df09f9d37)] - **test**: add known\_issues test for #10223 (AnnaMag) [#11024](https://github.com/nodejs/node/pull/11024)
* \[[`be34b629de`](https://github.com/nodejs/node/commit/be34b629de)] - **test**: increase coverage for stream's duplex (abouthiroppy) [#10963](https://github.com/nodejs/node/pull/10963)
* \[[`dc24127e5c`](https://github.com/nodejs/node/commit/dc24127e5c)] - **test**: allow for slow hosts in spawnSync() test (Rich Trott) [#10998](https://github.com/nodejs/node/pull/10998)
* \[[`2f4b6bda97`](https://github.com/nodejs/node/commit/2f4b6bda97)] - **test**: expand test coverage of fs.js (Vinícius do Carmo) [#10947](https://github.com/nodejs/node/pull/10947)
* \[[`3f6a2dbc2f`](https://github.com/nodejs/node/commit/3f6a2dbc2f)] - **test**: enhance test-timers (Rich Trott) [#10960](https://github.com/nodejs/node/pull/10960)
* \[[`6ca9901d8b`](https://github.com/nodejs/node/commit/6ca9901d8b)] - **test**: add process.assert's test (abouthiroppy) [#10911](https://github.com/nodejs/node/pull/10911)
* \[[`d8af5a7431`](https://github.com/nodejs/node/commit/d8af5a7431)] - **test**: improve code in test-crypto-verify (Adrian Estrada) [#10845](https://github.com/nodejs/node/pull/10845)
* \[[`4d1f7b1df8`](https://github.com/nodejs/node/commit/4d1f7b1df8)] - **test**: add dgram.Socket.prototype.bind's test (abouthiroppy) [#10894](https://github.com/nodejs/node/pull/10894)
* \[[`6c1d82c68a`](https://github.com/nodejs/node/commit/6c1d82c68a)] - **test**: improving coverage for dgram (abouthiroppy) [#10783](https://github.com/nodejs/node/pull/10783)
* \[[`017afd48fd`](https://github.com/nodejs/node/commit/017afd48fd)] - **test**: improve code in test-console-instance (Adrian Estrada) [#10813](https://github.com/nodejs/node/pull/10813)
* \[[`1b1ba741c3`](https://github.com/nodejs/node/commit/1b1ba741c3)] - **test**: improve code in test-domain-multi (Adrian Estrada) [#10798](https://github.com/nodejs/node/pull/10798)
* \[[`ee27917a65`](https://github.com/nodejs/node/commit/ee27917a65)] - **test**: improve test-stream2-large-read-stall (stefan judis) [#10725](https://github.com/nodejs/node/pull/10725)
* \[[`9ac2316595`](https://github.com/nodejs/node/commit/9ac2316595)] - **test**: improve code in test-http-host-headers (Adrian Estrada) [#10830](https://github.com/nodejs/node/pull/10830)
* \[[`a9278a063f`](https://github.com/nodejs/node/commit/a9278a063f)] - **test**: refactor cluster-preload.js (abouthiroppy) [#10701](https://github.com/nodejs/node/pull/10701)
* \[[`db60d92e15`](https://github.com/nodejs/node/commit/db60d92e15)] - **test**: test hmac binding robustness (Sam Roberts) [#10923](https://github.com/nodejs/node/pull/10923)
* \[[`a1a850f066`](https://github.com/nodejs/node/commit/a1a850f066)] - **test**: don't connect to :: (use localhost instead) (Gibson Fahnestock)
* \[[`b3a8e95af3`](https://github.com/nodejs/node/commit/b3a8e95af3)] - **test**: improve test-assert (richnologies) [#10916](https://github.com/nodejs/node/pull/10916)
* \[[`56970efe51`](https://github.com/nodejs/node/commit/56970efe51)] - **test**: increase coverage for punycode's decode (abouthiroppy) [#10940](https://github.com/nodejs/node/pull/10940)
* \[[`df69c2148a`](https://github.com/nodejs/node/commit/df69c2148a)] - **test**: check fd 0,1,2 are used, not access mode (John Barboza) [#10339](https://github.com/nodejs/node/pull/10339)
* \[[`7bceb4fb48`](https://github.com/nodejs/node/commit/7bceb4fb48)] - **test**: add message verification on assert.throws (Travis Meisenheimer) [#10890](https://github.com/nodejs/node/pull/10890)
* \[[`1c223ecc70`](https://github.com/nodejs/node/commit/1c223ecc70)] - **test**: add http-common's test (abouthiroppy) [#10832](https://github.com/nodejs/node/pull/10832)
* \[[`89e9da6b6d`](https://github.com/nodejs/node/commit/89e9da6b6d)] - **test**: tests for \_readableStream.awaitDrain (Mark) [#8914](https://github.com/nodejs/node/pull/8914)
* \[[`53b0f413cd`](https://github.com/nodejs/node/commit/53b0f413cd)] - **test**: improve the code in test-process-cpuUsage (Adrian Estrada) [#10714](https://github.com/nodejs/node/pull/10714)
* \[[`b3d1700d1f`](https://github.com/nodejs/node/commit/b3d1700d1f)] - **test**: improve tests in pummel/test-exec (Chase Starr) [#10757](https://github.com/nodejs/node/pull/10757)
* \[[`6e7dfb1f45`](https://github.com/nodejs/node/commit/6e7dfb1f45)] - **test**: fix temp-dir option in tools/test.py (Gibson Fahnestock) [#10723](https://github.com/nodejs/node/pull/10723)
* \[[`9abde3ac6e`](https://github.com/nodejs/node/commit/9abde3ac6e)] - **test**: use realpath for NODE\_TEST\_DIR in common.js (Gibson Fahnestock) [#10723](https://github.com/nodejs/node/pull/10723)
* \[[`f86c64a13a`](https://github.com/nodejs/node/commit/f86c64a13a)] - **test**: refactor the code of test-keep-alive.js (sivaprasanna) [#10684](https://github.com/nodejs/node/pull/10684)
* \[[`4d51db87dc`](https://github.com/nodejs/node/commit/4d51db87dc)] - **test**: refactor test-doctool-html.js (abouthiroppy) [#10696](https://github.com/nodejs/node/pull/10696)
* \[[`ab65429e44`](https://github.com/nodejs/node/commit/ab65429e44)] - **test**: refactor test-watch-file.js (sivaprasanna) [#10679](https://github.com/nodejs/node/pull/10679)
* \[[`4453c0c1dc`](https://github.com/nodejs/node/commit/4453c0c1dc)] - **test**: refactor the code in test-child-process-spawn-loop.js (sivaprasanna) [#10605](https://github.com/nodejs/node/pull/10605)
* \[[`42b86ea968`](https://github.com/nodejs/node/commit/42b86ea968)] - **test**: improve test-http-chunked-304 (Adrian Estrada) [#10462](https://github.com/nodejs/node/pull/10462)
* \[[`1ae95e64ee`](https://github.com/nodejs/node/commit/1ae95e64ee)] - **test**: improve test-fs-readfile-zero-byte-liar (Adrian Estrada) [#10570](https://github.com/nodejs/node/pull/10570)
* \[[`3f3c78d785`](https://github.com/nodejs/node/commit/3f3c78d785)] - **test**: refactor test-fs-utimes (Junshu Okamoto) [#9290](https://github.com/nodejs/node/pull/9290)
* \[[`50a868b3f7`](https://github.com/nodejs/node/commit/50a868b3f7)] - **test**: require handler to be run in sigwinch test (Rich Trott) [#11068](https://github.com/nodejs/node/pull/11068)
* \[[`c1f45ec2d0`](https://github.com/nodejs/node/commit/c1f45ec2d0)] - **test**: add 2nd argument to throws in test-assert (Marlena Compton) [#11061](https://github.com/nodejs/node/pull/11061)
* \[[`f24aa7e071`](https://github.com/nodejs/node/commit/f24aa7e071)] - **test**: improve error messages in test-npm-install (Gonen Dukas) [#11027](https://github.com/nodejs/node/pull/11027)
* \[[`1db89d4009`](https://github.com/nodejs/node/commit/1db89d4009)] - **test**: improve coverage on removeListeners functions (matsuda-koushi) [#11140](https://github.com/nodejs/node/pull/11140)
* \[[`c532c16e53`](https://github.com/nodejs/node/commit/c532c16e53)] - **test**: increase specificity in dgram test (Rich Trott) [#11187](https://github.com/nodejs/node/pull/11187)
* \[[`cb81ae8eea`](https://github.com/nodejs/node/commit/cb81ae8eea)] - **test**: add vm module edge cases (Franziska Hinkelmann) [#11265](https://github.com/nodejs/node/pull/11265)
* \[[`8629c956c3`](https://github.com/nodejs/node/commit/8629c956c3)] - **test**: improve punycode test coverage (Sebastian Van Sande) [#11144](https://github.com/nodejs/node/pull/11144)
* \[[`caf1ba15f9`](https://github.com/nodejs/node/commit/caf1ba15f9)] - **test**: add coverage for dgram \_createSocketHandle() (cjihrig) [#11291](https://github.com/nodejs/node/pull/11291)
* \[[`d729e52ef3`](https://github.com/nodejs/node/commit/d729e52ef3)] - **test**: improve crypto coverage (Akito Ito) [#11280](https://github.com/nodejs/node/pull/11280)
* \[[`d1a8588cab`](https://github.com/nodejs/node/commit/d1a8588cab)] - **test**: improve message in net-connect-local-error (Rich Trott) [#11393](https://github.com/nodejs/node/pull/11393)
* \[[`f2fb4143b4`](https://github.com/nodejs/node/commit/f2fb4143b4)] - **test**: refactor test-dgram-membership (Rich Trott) [#11388](https://github.com/nodejs/node/pull/11388)
* \[[`bf4703d66f`](https://github.com/nodejs/node/commit/bf4703d66f)] - **test**: remove unused args and comparison fix (Alexander) [#11396](https://github.com/nodejs/node/pull/11396)
* \[[`28471c23ff`](https://github.com/nodejs/node/commit/28471c23ff)] - **test**: refactor test-http-response-splitting (Arseniy Maximov) [#11429](https://github.com/nodejs/node/pull/11429)
* \[[`cd3e17e248`](https://github.com/nodejs/node/commit/cd3e17e248)] - **test**: improve coverage in test-crypto.dh (Eric Christie) [#11253](https://github.com/nodejs/node/pull/11253)
* \[[`fa681ea55a`](https://github.com/nodejs/node/commit/fa681ea55a)] - **test**: add regex check to test-module-loading (Tarang Hirani) [#11413](https://github.com/nodejs/node/pull/11413)
* \[[`f0eee61a93`](https://github.com/nodejs/node/commit/f0eee61a93)] - **test**: throw check in test-zlib-write-after-close (Jason Wilson) [#11482](https://github.com/nodejs/node/pull/11482)
* \[[`f0c7c7fad4`](https://github.com/nodejs/node/commit/f0c7c7fad4)] - **test**: fix flaky test-vm-timeout-rethrow (Kunal Pathak) [#11530](https://github.com/nodejs/node/pull/11530)
* \[[`53f2848dc8`](https://github.com/nodejs/node/commit/53f2848dc8)] - **test**: favor assertions over console logging (Rich Trott) [#11547](https://github.com/nodejs/node/pull/11547)
* \[[`0109321fd8`](https://github.com/nodejs/node/commit/0109321fd8)] - **test**: refactor test-https-truncate (Rich Trott) [#10225](https://github.com/nodejs/node/pull/10225)
* \[[`536733697c`](https://github.com/nodejs/node/commit/536733697c)] - **test**: simplify test-http-client-unescaped-path (Rod Vagg) [#9649](https://github.com/nodejs/node/pull/9649)
* \[[`4ce9bfb4e7`](https://github.com/nodejs/node/commit/4ce9bfb4e7)] - **test**: exclude pseudo-tty test pertinent to #11541 (Gireesh Punathil) [#11602](https://github.com/nodejs/node/pull/11602)
* \[[`53dd1a8539`](https://github.com/nodejs/node/commit/53dd1a8539)] - **tls**: do not crash on STARTTLS when OCSP requested (Fedor Indutny) [#10706](https://github.com/nodejs/node/pull/10706)
* \[[`e607ff52fa`](https://github.com/nodejs/node/commit/e607ff52fa)] - **tools**: rename eslintrc to an undeprecated format (Sakthipriyan Vairamani) [#7699](https://github.com/nodejs/node/pull/7699)
* \[[`6648b729b7`](https://github.com/nodejs/node/commit/6648b729b7)] - **tools**: add compile\_commands.json gyp generator (Ben Noordhuis) [#7986](https://github.com/nodejs/node/pull/7986)
* \[[`8f49962f47`](https://github.com/nodejs/node/commit/8f49962f47)] - **tools**: suggest python2 command in configure (Roman Reiss) [#11375](https://github.com/nodejs/node/pull/11375)
* \[[`4b83a83c06`](https://github.com/nodejs/node/commit/4b83a83c06)] - **tools,doc**: add Google Analytics tracking. (Phillip Johnsen) [#6601](https://github.com/nodejs/node/pull/6601)
* \[[`ef63af6006`](https://github.com/nodejs/node/commit/ef63af6006)] - **tty**: avoid oob warning in TTYWrap::GetWindowSize() (Dmitry Tsvettsikh) [#11454](https://github.com/nodejs/node/pull/11454)
* \[[`2c84601062`](https://github.com/nodejs/node/commit/2c84601062)] - **util**: don't init Debug if it's not needed yet (Bryan English) [#8452](https://github.com/nodejs/node/pull/8452)

<a id="4.8.0"></a>

## 2017-02-21, Version 4.8.0 'Argon' (LTS), @MylesBorins

This LTS release comes with 118 commits. This includes 73 which are doc
related, 19 which are test related, 6 which are build / tool related, and 5
commits which are updates to dependencies.

### Notable Changes

* **child\_process**: add shell option to spawn() (cjihrig) [#4598](https://github.com/nodejs/node/pull/4598)
* **deps**:
  * **v8**: expose statistics about heap spaces (Ben Ripkens) [#4463](https://github.com/nodejs/node/pull/4463)
* **crypto**:
  * add ALPN Support (Shigeki Ohtsu) [#2564](https://github.com/nodejs/node/pull/2564)
  * allow adding extra certs to well-known CAs (Sam Roberts) [#9139](https://github.com/nodejs/node/pull/9139)
* **fs**: add the fs.mkdtemp() function. (Florian MARGAINE) [#5333](https://github.com/nodejs/node/pull/5333)
* **process**:
  * add `externalMemory` to `process` (Fedor Indutny) [#9587](https://github.com/nodejs/node/pull/9587)
  * add process.cpuUsage() (Patrick Mueller) [#10796](https://github.com/nodejs/node/pull/10796)

### Commits

* \[[`78010aa0cd`](https://github.com/nodejs/node/commit/78010aa0cd)] - **build**: add /opt/freeware/... to AIX library path (Stewart X Addison) [#10128](https://github.com/nodejs/node/pull/10128)
* \[[`0bb77f24fa`](https://github.com/nodejs/node/commit/0bb77f24fa)] - **build**: add (not) cross-compiled configure flags (Jesús Leganés-Combarro 'piranna) [#10287](https://github.com/nodejs/node/pull/10287)
* \[[`58245225ef`](https://github.com/nodejs/node/commit/58245225ef)] - **(SEMVER-MINOR)** **child\_process**: add shell option to spawn() (cjihrig) [#4598](https://github.com/nodejs/node/pull/4598)
* \[[`1595328b44`](https://github.com/nodejs/node/commit/1595328b44)] - **(SEMVER-MINOR)** **crypto**: allow adding extra certs to well-known CAs (Sam Roberts) [#9139](https://github.com/nodejs/node/pull/9139)
* \[[`bf882fba35`](https://github.com/nodejs/node/commit/bf882fba35)] - **crypto**: Use reference count to manage cert\_store (Adam Majer) [#9409](https://github.com/nodejs/node/pull/9409)
* \[[`4cf7dcff99`](https://github.com/nodejs/node/commit/4cf7dcff99)] - **crypto**: remove unnecessary variables of alpn/npn (Shigeki Ohtsu) [#10831](https://github.com/nodejs/node/pull/10831)
* \[[`d8b902f787`](https://github.com/nodejs/node/commit/d8b902f787)] - **debugger**: call `this.resume()` after `this.run()` (Lance Ball) [#10099](https://github.com/nodejs/node/pull/10099)
* \[[`4e07bd45d6`](https://github.com/nodejs/node/commit/4e07bd45d6)] - **deps**: update patch level in V8 (Myles Borins) [#10668](https://github.com/nodejs/node/pull/10668)
* \[[`a234d445c4`](https://github.com/nodejs/node/commit/a234d445c4)] - **deps**: backport a715957 from V8 upstream (Myles Borins) [#10668](https://github.com/nodejs/node/pull/10668)
* \[[`ce66c8e424`](https://github.com/nodejs/node/commit/ce66c8e424)] - **deps**: backport 7a88ff3 from V8 upstream (Myles Borins) [#10668](https://github.com/nodejs/node/pull/10668)
* \[[`8bd3d83e01`](https://github.com/nodejs/node/commit/8bd3d83e01)] - **deps**: backport d800a65 from V8 upstream (Myles Borins) [#10668](https://github.com/nodejs/node/pull/10668)
* \[[`81e9a3bfcb`](https://github.com/nodejs/node/commit/81e9a3bfcb)] - **deps**: V8: fix debug backtrace for symbols (Ali Ijaz Sheikh) [#10732](https://github.com/nodejs/node/pull/10732)
* \[[`d8961bdb3b`](https://github.com/nodejs/node/commit/d8961bdb3b)] - **doc**: correct vcbuild options for windows testing (Jonathan Boarman) [#10686](https://github.com/nodejs/node/pull/10686)
* \[[`d3c5bc1c63`](https://github.com/nodejs/node/commit/d3c5bc1c63)] - **doc**: update BUILDING.md (rainabba) [#8704](https://github.com/nodejs/node/pull/8704)
* \[[`d61c181085`](https://github.com/nodejs/node/commit/d61c181085)] - **doc**: unify dirname and filename description (Sam Roberts) [#10527](https://github.com/nodejs/node/pull/10527)
* \[[`8eeccd82d2`](https://github.com/nodejs/node/commit/8eeccd82d2)] - **doc**: killSignal option accepts integer values (Sakthipriyan Vairamani (thefourtheye)) [#10424](https://github.com/nodejs/node/pull/10424)
* \[[`7db7e47d7b`](https://github.com/nodejs/node/commit/7db7e47d7b)] - **doc**: change logical to bitwise OR in dns lookup (Sakthipriyan Vairamani (thefourtheye)) [#11037](https://github.com/nodejs/node/pull/11037)
* \[[`28b707ba42`](https://github.com/nodejs/node/commit/28b707ba42)] - **doc**: replace newlines in deprecation with space (Sakthipriyan Vairamani (thefourtheye)) [#11074](https://github.com/nodejs/node/pull/11074)
* \[[`79d49866f2`](https://github.com/nodejs/node/commit/79d49866f2)] - **doc**: update CONTRIBUTING.MD with link to V8 guide (sarahmeyer) [#10070](https://github.com/nodejs/node/pull/10070)
* \[[`acbe4d3516`](https://github.com/nodejs/node/commit/acbe4d3516)] - **doc**: add joyeecheung to collaborators (Joyee Cheung) [#10603](https://github.com/nodejs/node/pull/10603)
* \[[`c7378c4d5f`](https://github.com/nodejs/node/commit/c7378c4d5f)] - **doc**: warn about unvalidated input in child\_process (Matthew Garrett) [#10466](https://github.com/nodejs/node/pull/10466)
* \[[`08e924e45c`](https://github.com/nodejs/node/commit/08e924e45c)] - **doc**: require two-factor authentication (Rich Trott) [#10529](https://github.com/nodejs/node/pull/10529)
* \[[`d260fb2e7e`](https://github.com/nodejs/node/commit/d260fb2e7e)] - **doc**: use "Node.js" in V8 guide (Rich Trott) [#10438](https://github.com/nodejs/node/pull/10438)
* \[[`4f168a4a31`](https://github.com/nodejs/node/commit/4f168a4a31)] - **doc**: require() tries first core not native modules (Vicente Jimenez Aguilar) [#10324](https://github.com/nodejs/node/pull/10324)
* \[[`5777c79c52`](https://github.com/nodejs/node/commit/5777c79c52)] - **doc**: clarify the review and landing process (Joyee Cheung) [#10202](https://github.com/nodejs/node/pull/10202)
* \[[`d3a7fb8a9e`](https://github.com/nodejs/node/commit/d3a7fb8a9e)] - **doc**: redirect 'Start a Working Group' to TSC repo (William Kapke) [#9655](https://github.com/nodejs/node/pull/9655)
* \[[`0e51cbb827`](https://github.com/nodejs/node/commit/0e51cbb827)] - **doc**: add Working Group dissolution text (William Kapke) [#9656](https://github.com/nodejs/node/pull/9656)
* \[[`919e0cb8f2`](https://github.com/nodejs/node/commit/919e0cb8f2)] - **doc**: more efficient example in the console.md (Vse Mozhet Byt) [#10451](https://github.com/nodejs/node/pull/10451)
* \[[`70ea38f2ee`](https://github.com/nodejs/node/commit/70ea38f2ee)] - **doc**: var -> const / let in the console.md (Vse Mozhet Byt) [#10451](https://github.com/nodejs/node/pull/10451)
* \[[`dda777bf9e`](https://github.com/nodejs/node/commit/dda777bf9e)] - **doc**: consistent 'Returns:' part two (Myles Borins) [#10391](https://github.com/nodejs/node/pull/10391)
* \[[`3b252a69a0`](https://github.com/nodejs/node/commit/3b252a69a0)] - **doc**: clarify macosx-firewall suggestion BUILDING (Chase Starr) [#10311](https://github.com/nodejs/node/pull/10311)
* \[[`c4df02c815`](https://github.com/nodejs/node/commit/c4df02c815)] - **doc**: add Michaël Zasso to the CTC (Michaël Zasso)
* \[[`2269d7db0f`](https://github.com/nodejs/node/commit/2269d7db0f)] - **(SEMVER-MINOR)** **fs**: add the fs.mkdtemp() function. (Florian MARGAINE) [#5333](https://github.com/nodejs/node/pull/5333)
* \[[`2eda3c7c75`](https://github.com/nodejs/node/commit/2eda3c7c75)] - **lib,test**: use consistent operator linebreak style (Michaël Zasso) [#10178](https://github.com/nodejs/node/pull/10178)
* \[[`7505b86d2f`](https://github.com/nodejs/node/commit/7505b86d2f)] - **os**: fix os.release() for aix and add test (jBarz) [#10245](https://github.com/nodejs/node/pull/10245)
* \[[`7a9c8d8f10`](https://github.com/nodejs/node/commit/7a9c8d8f10)] - **(SEMVER-MINOR)** **process**: add process.cpuUsage() - implementation, doc, tests (Patrick Mueller) [#10796](https://github.com/nodejs/node/pull/10796)
* \[[`23a573f7cb`](https://github.com/nodejs/node/commit/23a573f7cb)] - **(SEMVER-MINOR)** **process**: add `process.memoryUsage.external` (Fedor Indutny) [#9587](https://github.com/nodejs/node/pull/9587)
* \[[`be6203715a`](https://github.com/nodejs/node/commit/be6203715a)] - **src**: describe what NODE\_MODULE\_VERSION is for (Sam Roberts) [#10414](https://github.com/nodejs/node/pull/10414)
* \[[`3f29cbb5bc`](https://github.com/nodejs/node/commit/3f29cbb5bc)] - **src**: fix string format mistake for 32 bit node (Alex Newman) [#10082](https://github.com/nodejs/node/pull/10082)
* \[[`271f5783fe`](https://github.com/nodejs/node/commit/271f5783fe)] - **stream, test**: test \_readableState.emittedReadable (Joyee Cheung) [#10249](https://github.com/nodejs/node/pull/10249)
* \[[`c279cbe6a9`](https://github.com/nodejs/node/commit/c279cbe6a9)] - **test**: fix test.py command line options processing (Julien Gilli) [#11153](https://github.com/nodejs/node/pull/11153)
* \[[`0f5d82e583`](https://github.com/nodejs/node/commit/0f5d82e583)] - **test**: add --abort-on-timeout option to test.py (Julien Gilli) [#11086](https://github.com/nodejs/node/pull/11086)
* \[[`735119c6fb`](https://github.com/nodejs/node/commit/735119c6fb)] - **test**: cleanup stream tests (Italo A. Casas) [#8668](https://github.com/nodejs/node/pull/8668)
* \[[`f9f8e4ee3e`](https://github.com/nodejs/node/commit/f9f8e4ee3e)] - **test**: refactor test-preload (Rich Trott) [#9803](https://github.com/nodejs/node/pull/9803)
* \[[`e7c4dfb83b`](https://github.com/nodejs/node/commit/e7c4dfb83b)] - **test**: invalid package.json causes error when require()ing in directory (Sam Shull) [#10044](https://github.com/nodejs/node/pull/10044)
* \[[`22226fa900`](https://github.com/nodejs/node/commit/22226fa900)] - **test**: refactoring test-pipe-head (Travis Bretton) [#10036](https://github.com/nodejs/node/pull/10036)
* \[[`11115c0d85`](https://github.com/nodejs/node/commit/11115c0d85)] - **test**: add second argument to assert.throws() (Ken Russo) [#9987](https://github.com/nodejs/node/pull/9987)
* \[[`96ca40bdd8`](https://github.com/nodejs/node/commit/96ca40bdd8)] - **test**: refactor test-tls-0-dns-altname (Richard Karmazin) [#9948](https://github.com/nodejs/node/pull/9948)
* \[[`98496b6d3e`](https://github.com/nodejs/node/commit/98496b6d3e)] - **test**: test: refactor test-sync-fileread (Jason Wohlgemuth) [#9941](https://github.com/nodejs/node/pull/9941)
* \[[`324c82b1c9`](https://github.com/nodejs/node/commit/324c82b1c9)] - **test**: use common.fixturesDir almost everywhere (Bryan English) [#6997](https://github.com/nodejs/node/pull/6997)
* \[[`ce91bb21ba`](https://github.com/nodejs/node/commit/ce91bb21ba)] - **test**: refactor test-repl-mode.js (Cesar Hernandez) [#10061](https://github.com/nodejs/node/pull/10061)
* \[[`61cbc202a1`](https://github.com/nodejs/node/commit/61cbc202a1)] - **test**: refactor test-net-dns-custom-lookup (Kent.Fan) [#10071](https://github.com/nodejs/node/pull/10071)
* \[[`812c6361ff`](https://github.com/nodejs/node/commit/812c6361ff)] - **test**: refactor test-tls-server-verify (Hutson Betts) [#10076](https://github.com/nodejs/node/pull/10076)
* \[[`19907c27a6`](https://github.com/nodejs/node/commit/19907c27a6)] - **test**: use mustCall() for simple flow tracking (cjihrig) [#7753](https://github.com/nodejs/node/pull/7753)
* \[[`42da81e6cc`](https://github.com/nodejs/node/commit/42da81e6cc)] - **test**: set stdin too for pseudo-tty tests (Anna Henningsen) [#10149](https://github.com/nodejs/node/pull/10149)
* \[[`53404dbc1f`](https://github.com/nodejs/node/commit/53404dbc1f)] - **test**: add stdin-setrawmode.out file (Jonathan Darling) [#10149](https://github.com/nodejs/node/pull/10149)
* \[[`1fac431307`](https://github.com/nodejs/node/commit/1fac431307)] - **test**: add tests for clearBuffer state machine (Safia Abdalla) [#9922](https://github.com/nodejs/node/pull/9922)
* \[[`37a362275e`](https://github.com/nodejs/node/commit/37a362275e)] - **test**: update test-cluster-shared-handle-bind-error (cjihrig) [#10547](https://github.com/nodejs/node/pull/10547)
* \[[`f5e54f5d5f`](https://github.com/nodejs/node/commit/f5e54f5d5f)] - **test**: avoid assigning this to variables (cjihrig) [#10548](https://github.com/nodejs/node/pull/10548)
* \[[`28a5ce10af`](https://github.com/nodejs/node/commit/28a5ce10af)] - **test**: improve test-http-allow-req-after-204-res (Adrian Estrada) [#10503](https://github.com/nodejs/node/pull/10503)
* \[[`52edebc8f3`](https://github.com/nodejs/node/commit/52edebc8f3)] - **test**: improve test-fs-empty-readStream.js (Adrian Estrada) [#10479](https://github.com/nodejs/node/pull/10479)
* \[[`b74bc517a6`](https://github.com/nodejs/node/commit/b74bc517a6)] - **test**: use strictEqual in test-http-server (Fabrice Tatieze) [#10478](https://github.com/nodejs/node/pull/10478)
* \[[`a9cd1d1267`](https://github.com/nodejs/node/commit/a9cd1d1267)] - **test**: refactor test-stream2-unpipe-drain (Chris Story) [#10033](https://github.com/nodejs/node/pull/10033)
* \[[`7020e9fd8b`](https://github.com/nodejs/node/commit/7020e9fd8b)] - **test**: add test for SIGWINCH handling by stdio.js (Sarah Meyer) [#10063](https://github.com/nodejs/node/pull/10063)
* \[[`56b193a9c2`](https://github.com/nodejs/node/commit/56b193a9c2)] - **test**: improve code in test-vm-preserves-property (Adrian Estrada) [#10428](https://github.com/nodejs/node/pull/10428)
* \[[`8a26ba142f`](https://github.com/nodejs/node/commit/8a26ba142f)] - **test**: fix flaky test-https-timeout (Rich Trott) [#10404](https://github.com/nodejs/node/pull/10404)
* \[[`eeb2d7885a`](https://github.com/nodejs/node/commit/eeb2d7885a)] - **test**: improve test-cluster-worker-constructor.js (Adrian Estrada) [#10396](https://github.com/nodejs/node/pull/10396)
* \[[`fd195b47d6`](https://github.com/nodejs/node/commit/fd195b47d6)] - **test**: stream readable resumeScheduled state (Italo A. Casas) [#10299](https://github.com/nodejs/node/pull/10299)
* \[[`135a7c9e19`](https://github.com/nodejs/node/commit/135a7c9e19)] - **test**: stream readable needReadable state (Joyee Cheung) [#10241](https://github.com/nodejs/node/pull/10241)
* \[[`f412b1fcfd`](https://github.com/nodejs/node/commit/f412b1fcfd)] - **test**: clean up domain-no-error-handler test (weyj4) [#10291](https://github.com/nodejs/node/pull/10291)
* \[[`14c28ebcf1`](https://github.com/nodejs/node/commit/14c28ebcf1)] - **test**: update test-domain-uncaught-exception.js (Andy Chen) [#10193](https://github.com/nodejs/node/pull/10193)
* \[[`928291c652`](https://github.com/nodejs/node/commit/928291c652)] - **test**: refactor test-domain.js (Siddhartha Sahai) [#10207](https://github.com/nodejs/node/pull/10207)
* \[[`13c6cec433`](https://github.com/nodejs/node/commit/13c6cec433)] - **test**: fail for missing output files (Anna Henningsen) [#10150](https://github.com/nodejs/node/pull/10150)
* \[[`544920f77b`](https://github.com/nodejs/node/commit/544920f77b)] - **test**: stream readableState readingMore state (Gregory) [#9868](https://github.com/nodejs/node/pull/9868)
* \[[`2f8bc9a7bc`](https://github.com/nodejs/node/commit/2f8bc9a7bc)] - **test**: s/ASSERT/assert/ (cjihrig) [#10544](https://github.com/nodejs/node/pull/10544)
* \[[`380a5d5e12`](https://github.com/nodejs/node/commit/380a5d5e12)] - **test**: fix flaky test-http-client-timeout-with-data (Rich Trott) [#10431](https://github.com/nodejs/node/pull/10431)
* \[[`14e07c96e1`](https://github.com/nodejs/node/commit/14e07c96e1)] - **test**: refactor test-stdin-from-file (Rob Adelmann) [#10331](https://github.com/nodejs/node/pull/10331)
* \[[`424c86139d`](https://github.com/nodejs/node/commit/424c86139d)] - **test**: refactor the code in test-fs-chmod (Adrian Estrada) [#10440](https://github.com/nodejs/node/pull/10440)
* \[[`31aa877003`](https://github.com/nodejs/node/commit/31aa877003)] - **test**: improve the code in test-pipe.js (Adrian Estrada) [#10452](https://github.com/nodejs/node/pull/10452)
* \[[`4bbd50ee07`](https://github.com/nodejs/node/commit/4bbd50ee07)] - **test**: improve code in test-fs-readfile-error (Adrian Estrada) [#10367](https://github.com/nodejs/node/pull/10367)
* \[[`9840f505f0`](https://github.com/nodejs/node/commit/9840f505f0)] - **test**: improve code in test-vm-symbols (Adrian Estrada) [#10429](https://github.com/nodejs/node/pull/10429)
* \[[`4efdbafeb3`](https://github.com/nodejs/node/commit/4efdbafeb3)] - **test**: refactor test-child-process-ipc (malen) [#9990](https://github.com/nodejs/node/pull/9990)
* \[[`dbfec29663`](https://github.com/nodejs/node/commit/dbfec29663)] - **test**: fix and improve debug-break-on-uncaught (Sakthipriyan Vairamani (thefourtheye)) [#10370](https://github.com/nodejs/node/pull/10370)
* \[[`80f4a37023`](https://github.com/nodejs/node/commit/80f4a37023)] - **test**: refactor test-pipe-file-to-http (Josh Mays) [#10054](https://github.com/nodejs/node/pull/10054)
* \[[`a983400ac2`](https://github.com/nodejs/node/commit/a983400ac2)] - **test**: refactor test-tls-interleave (Brian Chirgwin) [#10017](https://github.com/nodejs/node/pull/10017)
* \[[`6db76da2c8`](https://github.com/nodejs/node/commit/6db76da2c8)] - **test**: refactor test-cluster-send-handle-twice.js (Amar Zavery) [#10049](https://github.com/nodejs/node/pull/10049)
* \[[`19b314e40a`](https://github.com/nodejs/node/commit/19b314e40a)] - **test**: update test-tls-check-server-identity.js (Kevin Cox) [#9986](https://github.com/nodejs/node/pull/9986)
* \[[`ab3e4c6a9b`](https://github.com/nodejs/node/commit/ab3e4c6a9b)] - **test**: improve test-cluster-net-listen.js (Rico Cai) [#9953](https://github.com/nodejs/node/pull/9953)
* \[[`fb9a0ad6c0`](https://github.com/nodejs/node/commit/fb9a0ad6c0)] - **test**: refactor test-child-process-stdin (Segu Riluvan) [#10420](https://github.com/nodejs/node/pull/10420)
* \[[`122917df5a`](https://github.com/nodejs/node/commit/122917df5a)] - **test**: change var declarations, add mustCall check (Daniel Sims) [#9962](https://github.com/nodejs/node/pull/9962)
* \[[`d5e911c51e`](https://github.com/nodejs/node/commit/d5e911c51e)] - **test**: refactoring test-cluster-worker-constructor (Christopher Rokita) [#9956](https://github.com/nodejs/node/pull/9956)
* \[[`7d61bbf647`](https://github.com/nodejs/node/commit/7d61bbf647)] - **test**: refactor test-stdin-script-child (Emanuel Buholzer) [#10321](https://github.com/nodejs/node/pull/10321)
* \[[`76bb3cbff9`](https://github.com/nodejs/node/commit/76bb3cbff9)] - **test**: refactor test-stream2-writable (Rich Trott) [#10353](https://github.com/nodejs/node/pull/10353)
* \[[`b87ee26b96`](https://github.com/nodejs/node/commit/b87ee26b96)] - **test**: change assert.strict to assert.strictEqual() (Ashita Nagesh) [#9988](https://github.com/nodejs/node/pull/9988)
* \[[`4514fd78f4`](https://github.com/nodejs/node/commit/4514fd78f4)] - **test**: refactor the code in test-http-keep-alive (Adrian Estrada) [#10350](https://github.com/nodejs/node/pull/10350)
* \[[`f301df405a`](https://github.com/nodejs/node/commit/f301df405a)] - **test**: use strictEqual in test-cwd-enoent-repl.js (Neeraj Sharma) [#9952](https://github.com/nodejs/node/pull/9952)
* \[[`3b67001c99`](https://github.com/nodejs/node/commit/3b67001c99)] - **test**: refactor test-net-reconnect-error (Duy Le) [#9903](https://github.com/nodejs/node/pull/9903)
* \[[`34861efff6`](https://github.com/nodejs/node/commit/34861efff6)] - **test**: add test-require-invalid-package (Duy Le) [#9903](https://github.com/nodejs/node/pull/9903)
* \[[`90a79b3967`](https://github.com/nodejs/node/commit/90a79b3967)] - **test**: refactor test-timers-this (Rich Trott) [#10315](https://github.com/nodejs/node/pull/10315)
* \[[`5335b0a0d1`](https://github.com/nodejs/node/commit/5335b0a0d1)] - **test**: refactor test-tls-ecdh-disable (Aaron Williams) [#9989](https://github.com/nodejs/node/pull/9989)
* \[[`0f8a323546`](https://github.com/nodejs/node/commit/0f8a323546)] - **test**: cleanup test-stdout-close-catch.js (Travis Bretton) [#10006](https://github.com/nodejs/node/pull/10006)
* \[[`fc67a955e2`](https://github.com/nodejs/node/commit/fc67a955e2)] - **test**: use const/let and common.mustCall (Outsider) [#9959](https://github.com/nodejs/node/pull/9959)
* \[[`2f44d7f367`](https://github.com/nodejs/node/commit/2f44d7f367)] - **test**: refactor test-crypto-random (Rich Trott) [#10232](https://github.com/nodejs/node/pull/10232)
* \[[`730c3b29e8`](https://github.com/nodejs/node/commit/730c3b29e8)] - **test**: refactor test-fs-fsync (Rob Adelmann) [#10176](https://github.com/nodejs/node/pull/10176)
* \[[`9c9d422433`](https://github.com/nodejs/node/commit/9c9d422433)] - **test**: refactor test-http-after-connect.js (larissayvette) [#10229](https://github.com/nodejs/node/pull/10229)
* \[[`827bbe7985`](https://github.com/nodejs/node/commit/827bbe7985)] - **test**: refactor assert.equal, update syntax to ES6 (Prieto, Marcos)
* \[[`121b68a283`](https://github.com/nodejs/node/commit/121b68a283)] - **test**: refactor http pipelined socket test (Rich Trott) [#10189](https://github.com/nodejs/node/pull/10189)
* \[[`7ca31e38fb`](https://github.com/nodejs/node/commit/7ca31e38fb)] - **test**: fix alpn tests for openssl1.0.2h (Shigeki Ohtsu) [#6550](https://github.com/nodejs/node/pull/6550)
* \[[`278d718a93`](https://github.com/nodejs/node/commit/278d718a93)] - **test**: refactor test-handle-wrap-close-abort (Rich Trott) [#10188](https://github.com/nodejs/node/pull/10188)
* \[[`f12bab65b8`](https://github.com/nodejs/node/commit/f12bab65b8)] - **test**: stream readableListening internal state (Italo A. Casas) [#9864](https://github.com/nodejs/node/pull/9864)
* \[[`210290dfba`](https://github.com/nodejs/node/commit/210290dfba)] - **test**: check for error on invalid signal (Matt Phillips) [#10026](https://github.com/nodejs/node/pull/10026)
* \[[`4f5f0e4975`](https://github.com/nodejs/node/commit/4f5f0e4975)] - **test**: refactor test-net-keepalive.js (Kyle Corsi) [#9995](https://github.com/nodejs/node/pull/9995)
* \[[`cfa2b87b5d`](https://github.com/nodejs/node/commit/cfa2b87b5d)] - **test,lib,benchmark**: match function names (Rich Trott) [#9113](https://github.com/nodejs/node/pull/9113)
* \[[`a67ada7d32`](https://github.com/nodejs/node/commit/a67ada7d32)] - **tls**: copy the Buffer object before using (Sakthipriyan Vairamani) [#8055](https://github.com/nodejs/node/pull/8055)
* \[[`e750f142ce`](https://github.com/nodejs/node/commit/e750f142ce)] - **(SEMVER-MINOR)** **tls, crypto**: add ALPN Support (Shigeki Ohtsu) [#2564](https://github.com/nodejs/node/pull/2564)
* \[[`ef547f3325`](https://github.com/nodejs/node/commit/ef547f3325)] - **(SEMVER-MINOR)** **tls,crypto**: move NPN protcol data to hidden value (Shigeki Ohtsu) [#2564](https://github.com/nodejs/node/pull/2564)
* \[[`31434a1202`](https://github.com/nodejs/node/commit/31434a1202)] - **tools**: enforce consistent operator linebreak style (Michaël Zasso) [#10178](https://github.com/nodejs/node/pull/10178)
* \[[`9f13b5f7d5`](https://github.com/nodejs/node/commit/9f13b5f7d5)] - **tools**: forbid template literals in assert.throws (Michaël Zasso) [#10301](https://github.com/nodejs/node/pull/10301)
* \[[`c801de9814`](https://github.com/nodejs/node/commit/c801de9814)] - **tools**: add ESLint rule for assert.throws arguments (Michaël Zasso) [#10089](https://github.com/nodejs/node/pull/10089)
* \[[`b5e18f207f`](https://github.com/nodejs/node/commit/b5e18f207f)] - **tools**: add macosx-firwall script to avoid popups (Daniel Bevenius) [#10114](https://github.com/nodejs/node/pull/10114)
* \[[`30d60cf81c`](https://github.com/nodejs/node/commit/30d60cf81c)] - **(SEMVER-MINOR)** **v8,src**: expose statistics about heap spaces (Ben Ripkens) [#4463](https://github.com/nodejs/node/pull/4463)
* \[[`9556ef3241`](https://github.com/nodejs/node/commit/9556ef3241)] - **vm**: add error message if we abort (Franziska Hinkelmann) [#8634](https://github.com/nodejs/node/pull/8634)
* \[[`fa11f4b1fc`](https://github.com/nodejs/node/commit/fa11f4b1fc)] - **win,msi**: add required UIRef for localized strings (Bill Ticehurst) [#8884](https://github.com/nodejs/node/pull/8884)

<a id="4.7.3"></a>

## 2017-01-31, Version 4.7.3 'Argon' (LTS), @MylesBorins

This is a security release of the 'Argon' release line to upgrade OpenSSL to version 1.0.2k

Although the OpenSSL team have determined a maximum severity rating of "moderate", the Node.js
crypto team (Ben Noordhuis, Shigeki Ohtsu and Fedor Indutny) have determined the impact to Node
users is "low". Details on this determination can be found
[on the Nodejs.org website](https://nodejs.org/en/blog/vulnerability/openssl-january-2017/).

### Notable Changes

* **deps**: upgrade openssl sources to 1.0.2k (Shigeki Ohtsu) [#11021](https://github.com/nodejs/node/pull/11021)

### Commits

* \[[`8029f64135`](https://github.com/nodejs/node/commit/8029f64135)] - **deps**: update openssl asm and asm\_obsolete files (Shigeki Ohtsu) [#11021](https://github.com/nodejs/node/pull/11021)
* \[[`0081659a41`](https://github.com/nodejs/node/commit/0081659a41)] - **deps**: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) [nodejs/io.js#1836](https://github.com/nodejs/io.js/pull/1836)
* \[[`e55c3f4e21`](https://github.com/nodejs/node/commit/e55c3f4e21)] - **deps**: fix asm build error of openssl in x86\_win32 (Shigeki Ohtsu) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`24640f9278`](https://github.com/nodejs/node/commit/24640f9278)] - **deps**: fix openssl assembly error on ia32 win32 (Fedor Indutny) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`6c7bdf58e0`](https://github.com/nodejs/node/commit/6c7bdf58e0)] - **deps**: copy all openssl header files to include dir (Shigeki Ohtsu) [#11021](https://github.com/nodejs/node/pull/11021)
* \[[`c80844769c`](https://github.com/nodejs/node/commit/c80844769c)] - **deps**: upgrade openssl sources to 1.0.2k (Shigeki Ohtsu) [#11021](https://github.com/nodejs/node/pull/11021)
* \[[`e3915a415b`](https://github.com/nodejs/node/commit/e3915a415b)] - **openssl**: fix keypress requirement in apps on win32 (Shigeki Ohtsu) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)

<a id="4.7.2"></a>

## 2017-01-05, Version 4.7.2 'Argon' (LTS), @MylesBorins

This is a special release that contains 0 commits. While promoting additional
platforms for v4.7.1 after the release, the tarballs on the release server were
overwritten and now have different shasums. In order to remove any ambiguity
around the release we have opted to do a semver patch release with no changes.

### Notable Changes

N/A

### Commits

N/A

<a id="4.7.1"></a>

## 2017-01-03, Version 4.7.1 'Argon' (LTS), @MylesBorins

This LTS release comes with 180 commits. This includes 117 which are test related,
34 which are doc related, 15 which are build / tool related, and 1 commit which is
an update to dependencies.

### Notable Changes

* **build**: shared library support is now working for AIX builds (Stewart Addison) [#9675](https://github.com/nodejs/node/pull/9675)
* **repl**: Passing options to the repl will no longer overwrite defaults (cjihrig) [#7826](https://github.com/nodejs/node/pull/7826)
* **timers**: Re canceling a cancelled timers will no longer throw (Jeremiah Senkpiel) [#9685](https://github.com/nodejs/node/pull/9685)

### Commits

* \[[`c5f82b8421`](https://github.com/nodejs/node/commit/c5f82b8421)] - **assert**: fix deepEqual/deepStrictEqual on equivalent typed arrays (Feross Aboukhadijeh) [#8002](https://github.com/nodejs/node/pull/8002)
* \[[`60883de30f`](https://github.com/nodejs/node/commit/60883de30f)] - **async\_wrap**: call destroy() callback in uv\_idle\_t (Trevor Norris)
* \[[`28dbc460c6`](https://github.com/nodejs/node/commit/28dbc460c6)] - **async\_wrap**: make Initialize a static class member (Trevor Norris)
* \[[`bb05cd13db`](https://github.com/nodejs/node/commit/bb05cd13db)] - **async\_wrap**: mode constructor/destructor to .cc (Trevor Norris)
* \[[`b1075f6193`](https://github.com/nodejs/node/commit/b1075f6193)] - **benchmark**: split timers benchmark and refactor (Rich Trott) [#9497](https://github.com/nodejs/node/pull/9497)
* \[[`7b4268b889`](https://github.com/nodejs/node/commit/7b4268b889)] - **benchmark,lib,test,tools**: remove unneeded . escape (Rich Trott) [#9449](https://github.com/nodejs/node/pull/9449)
* \[[`54f2ce8ea0`](https://github.com/nodejs/node/commit/54f2ce8ea0)] - **build**: prioritise --shared-X-Y over pkg-config (Rod Vagg) [#9368](https://github.com/nodejs/node/pull/9368)
* \[[`61d377ddcd`](https://github.com/nodejs/node/commit/61d377ddcd)] - **build**: Make configure file parseable on python3 (kalrover) [#9657](https://github.com/nodejs/node/pull/9657)
* \[[`38e0f95d24`](https://github.com/nodejs/node/commit/38e0f95d24)] - **build**: add MAKEFLAGS="-j1" to node-gyp (Daniel Bevenius) [#9450](https://github.com/nodejs/node/pull/9450)
* \[[`d1b6407395`](https://github.com/nodejs/node/commit/d1b6407395)] - **build**: make node-gyp output silent (Sakthipriyan Vairamani (thefourtheye)) [#8990](https://github.com/nodejs/node/pull/8990)
* \[[`ae2eff2997`](https://github.com/nodejs/node/commit/ae2eff2997)] - **build**: start comments at beginning of line (Sakthipriyan Vairamani (thefourtheye)) [#9375](https://github.com/nodejs/node/pull/9375)
* \[[`6f1f955b33`](https://github.com/nodejs/node/commit/6f1f955b33)] - **build**: default to ppc64 on AIX (Gibson Fahnestock) [#9645](https://github.com/nodejs/node/pull/9645)
* \[[`f8d4577762`](https://github.com/nodejs/node/commit/f8d4577762)] - **build**: Add option to compile for coverage reports (Wayne Andrews) [#9463](https://github.com/nodejs/node/pull/9463)
* \[[`f2b00985f0`](https://github.com/nodejs/node/commit/f2b00985f0)] - **build**: add shared library support to AIX build (Stewart Addison) [#9675](https://github.com/nodejs/node/pull/9675)
* \[[`e2c5f41ddf`](https://github.com/nodejs/node/commit/e2c5f41ddf)] - **crypto**: use SSL\_get\_servername. (Adam Langley) [#9347](https://github.com/nodejs/node/pull/9347)
* \[[`724910a991`](https://github.com/nodejs/node/commit/724910a991)] - **debugger**: refactor \_debugger.js (Rich Trott) [#9860](https://github.com/nodejs/node/pull/9860)
* \[[`52f14931a2`](https://github.com/nodejs/node/commit/52f14931a2)] - **deps**: backport GYP fix to fix AIX shared suffix (Stewart Addison) [#9675](https://github.com/nodejs/node/pull/9675)
* \[[`c77ba8ce14`](https://github.com/nodejs/node/commit/c77ba8ce14)] - **doc**: consistent 'Returns:' (Roman Reiss) [#9554](https://github.com/nodejs/node/pull/9554)
* \[[`aecb2cac37`](https://github.com/nodejs/node/commit/aecb2cac37)] - **doc**: adding missing - in README (Italo A. Casas) [#10170](https://github.com/nodejs/node/pull/10170)
* \[[`52c022992e`](https://github.com/nodejs/node/commit/52c022992e)] - **doc**: removing extra space in README (Italo A. Casas) [#10168](https://github.com/nodejs/node/pull/10168)
* \[[`e8c57bbe77`](https://github.com/nodejs/node/commit/e8c57bbe77)] - **doc**: add people to cc for async\_wrap (Anna Henningsen) [#9471](https://github.com/nodejs/node/pull/9471)
* \[[`b5eae4463c`](https://github.com/nodejs/node/commit/b5eae4463c)] - **doc**: add link to `net.Server` in tls.md (Devon Rifkin) [#10109](https://github.com/nodejs/node/pull/10109)
* \[[`ad841a29d1`](https://github.com/nodejs/node/commit/ad841a29d1)] - **doc**: clarify fs.createReadStream options (Wes Tyler) [#10078](https://github.com/nodejs/node/pull/10078)
* \[[`338014ef24`](https://github.com/nodejs/node/commit/338014ef24)] - **doc**: rename writing\_tests.md to writing-tests.md (Safia Abdalla) [#9867](https://github.com/nodejs/node/pull/9867)
* \[[`b06b2343bc`](https://github.com/nodejs/node/commit/b06b2343bc)] - **doc**: it’s -> its in api/child\_process.md (Devon Rifkin) [#10090](https://github.com/nodejs/node/pull/10090)
* \[[`4885573080`](https://github.com/nodejs/node/commit/4885573080)] - **doc**: update Collaborators list in README (Rich Trott) [#9846](https://github.com/nodejs/node/pull/9846)
* \[[`3105becb2c`](https://github.com/nodejs/node/commit/3105becb2c)] - **doc**: remove minor contradiction in debugger doc (Rich Trott) [#9832](https://github.com/nodejs/node/pull/9832)
* \[[`a858e98921`](https://github.com/nodejs/node/commit/a858e98921)] - **doc**: clarify introductory module material (Rich Trott) [#9816](https://github.com/nodejs/node/pull/9816)
* \[[`18c38819fe`](https://github.com/nodejs/node/commit/18c38819fe)] - **doc**: improve description of module `exports` (Sam Roberts) [#9622](https://github.com/nodejs/node/pull/9622)
* \[[`9e68b8d329`](https://github.com/nodejs/node/commit/9e68b8d329)] - **doc**: fix crypto Verify cut-n-paste from Sign (子丶言) [#9796](https://github.com/nodejs/node/pull/9796)
* \[[`fd1a48c9c9`](https://github.com/nodejs/node/commit/fd1a48c9c9)] - **doc**: minor fixes event-loop-timers-and-nexttick.md (Dan Koster) [#9126](https://github.com/nodejs/node/pull/9126)
* \[[`107735a6e1`](https://github.com/nodejs/node/commit/107735a6e1)] - **doc**: changed order of invocations in https.request() example. (atrioom) [#9614](https://github.com/nodejs/node/pull/9614)
* \[[`eb5972fe9b`](https://github.com/nodejs/node/commit/eb5972fe9b)] - **doc**: fix crypto "decipher.setAAD()" typo (子丶言) [#9782](https://github.com/nodejs/node/pull/9782)
* \[[`dc4c348ea3`](https://github.com/nodejs/node/commit/dc4c348ea3)] - **doc**: fix typo in assert code example (Vse Mozhet Byt) [#9704](https://github.com/nodejs/node/pull/9704)
* \[[`16e97ab6c6`](https://github.com/nodejs/node/commit/16e97ab6c6)] - **doc**: fix typo in BUILDING.md (monkick) [#9569](https://github.com/nodejs/node/pull/9569)
* \[[`4f2e25441e`](https://github.com/nodejs/node/commit/4f2e25441e)] - **doc**: remove backtick escaping for manpage refs (Anna Henningsen) [#9632](https://github.com/nodejs/node/pull/9632)
* \[[`c0d44dfcc7`](https://github.com/nodejs/node/commit/c0d44dfcc7)] - **doc**: remove invalid padding from privateEncrypt (JungMinu) [#9611](https://github.com/nodejs/node/pull/9611)
* \[[`0f523583c3`](https://github.com/nodejs/node/commit/0f523583c3)] - **doc**: remove Sam Roberts from release team (Sam Roberts) [#9862](https://github.com/nodejs/node/pull/9862)
* \[[`4eeac8eb8c`](https://github.com/nodejs/node/commit/4eeac8eb8c)] - **doc**: add guide for maintaining V8 (Ali Ijaz Sheikh) [#9777](https://github.com/nodejs/node/pull/9777)
* \[[`34405ddb83`](https://github.com/nodejs/node/commit/34405ddb83)] - **doc**: move TSC and CTC meeting minutes out of core repo (James M Snell) [#9503](https://github.com/nodejs/node/pull/9503)
* \[[`198463a0ff`](https://github.com/nodejs/node/commit/198463a0ff)] - **doc**: fix a typo in the assert.md (Vse Mozhet Byt) [#9598](https://github.com/nodejs/node/pull/9598)
* \[[`aca0ede0d3`](https://github.com/nodejs/node/commit/aca0ede0d3)] - **doc**: fix typo e.g., => e.g. (Daijiro Yamada) [#9563](https://github.com/nodejs/node/pull/9563)
* \[[`c7997939f2`](https://github.com/nodejs/node/commit/c7997939f2)] - **doc**: fix typo about cluster doc, (eg. -> e.g.) (YutamaKotaro) [#9568](https://github.com/nodejs/node/pull/9568)
* \[[`229fa6921f`](https://github.com/nodejs/node/commit/229fa6921f)] - **doc**: fix e.g., to e.g. in doc/http.md (ikasumi\_wt) [#9564](https://github.com/nodejs/node/pull/9564)
* \[[`3ad7430f12`](https://github.com/nodejs/node/commit/3ad7430f12)] - **doc**: fix the index order in pseudocode of modules (kohta ito) [#9562](https://github.com/nodejs/node/pull/9562)
* \[[`06732babd3`](https://github.com/nodejs/node/commit/06732babd3)] - **doc**: remove Roadmap Working Group (William Kapke) [#9545](https://github.com/nodejs/node/pull/9545)
* \[[`6775163a94`](https://github.com/nodejs/node/commit/6775163a94)] - **doc**: fix minor style issue in code examples (Daniel Bevenius) [#9482](https://github.com/nodejs/node/pull/9482)
* \[[`aa25c74fe6`](https://github.com/nodejs/node/commit/aa25c74fe6)] - **doc**: grammar and structure revisions of wg doc (Ryan Lewis) [#9495](https://github.com/nodejs/node/pull/9495)
* \[[`1e06ed7e9d`](https://github.com/nodejs/node/commit/1e06ed7e9d)] - **doc**: clarify the exit code part of writing\_tests (Jeremiah Senkpiel) [#9502](https://github.com/nodejs/node/pull/9502)
* \[[`3f39a39657`](https://github.com/nodejs/node/commit/3f39a39657)] - **doc**: Fix inaccuracy in https.request docs (Andreas Lind) [#9453](https://github.com/nodejs/node/pull/9453)
* \[[`8380154e22`](https://github.com/nodejs/node/commit/8380154e22)] - **doc**: add npm link to README (Oscar Morrison) [#7894](https://github.com/nodejs/node/pull/7894)
* \[[`65e134ff12`](https://github.com/nodejs/node/commit/65e134ff12)] - **meta**: whitelist dotfiles in .gitignore (Claudio Rodriguez) [#8016](https://github.com/nodejs/node/pull/8016)
* \[[`698bf2e829`](https://github.com/nodejs/node/commit/698bf2e829)] - **repl**: don't override all internal repl defaults (cjihrig) [#7826](https://github.com/nodejs/node/pull/7826)
* \[[`3d45b35f73`](https://github.com/nodejs/node/commit/3d45b35f73)] - **repl**: refactor lib/repl.js (Rich Trott) [#9374](https://github.com/nodejs/node/pull/9374)
* \[[`f5b952b221`](https://github.com/nodejs/node/commit/f5b952b221)] - **test**: refactor and fix test-dns (Michaël Zasso) [#9811](https://github.com/nodejs/node/pull/9811)
* \[[`8b733dca05`](https://github.com/nodejs/node/commit/8b733dca05)] - **test**: refactor test-crypto-binary-default (Michaël Zasso) [#9810](https://github.com/nodejs/node/pull/9810)
* \[[`45af7857d7`](https://github.com/nodejs/node/commit/45af7857d7)] - **test**: refactor and fix test-crypto (Michaël Zasso) [#9807](https://github.com/nodejs/node/pull/9807)
* \[[`e0c8aafad8`](https://github.com/nodejs/node/commit/e0c8aafad8)] - **test**: fix test-buffer-slow (Michaël Zasso) [#9809](https://github.com/nodejs/node/pull/9809)
* \[[`e72dfce2c8`](https://github.com/nodejs/node/commit/e72dfce2c8)] - **test**: added validation regex argument to test (Avery, Frank) [#9918](https://github.com/nodejs/node/pull/9918)
* \[[`a779e7ffec`](https://github.com/nodejs/node/commit/a779e7ffec)] - **test**: clean up repl-reset-event file (Kailean Courtney) [#9931](https://github.com/nodejs/node/pull/9931)
* \[[`4022579b6e`](https://github.com/nodejs/node/commit/4022579b6e)] - **test**: improve domain-top-level-error-handler-throw (CodeVana) [#9950](https://github.com/nodejs/node/pull/9950)
* \[[`d3edaa3dc3`](https://github.com/nodejs/node/commit/d3edaa3dc3)] - **test**: replace var with const in test-require-dot (Amar Zavery) [#9916](https://github.com/nodejs/node/pull/9916)
* \[[`8694811ef0`](https://github.com/nodejs/node/commit/8694811ef0)] - **test**: refactor test-net-pingpong (Michaël Zasso) [#9812](https://github.com/nodejs/node/pull/9812)
* \[[`e849dd0ff3`](https://github.com/nodejs/node/commit/e849dd0ff3)] - **test**: Use strictEqual in test-tls-writewrap-leak (Aaron Petcoff) [#9666](https://github.com/nodejs/node/pull/9666)
* \[[`0662429268`](https://github.com/nodejs/node/commit/0662429268)] - **test**: fix test-tls-connect-address-family (mkamakura) [#9573](https://github.com/nodejs/node/pull/9573)
* \[[`420e7f17d9`](https://github.com/nodejs/node/commit/420e7f17d9)] - **test**: fix test-http-status-reason-invalid-chars (Yosuke Saito) [#9572](https://github.com/nodejs/node/pull/9572)
* \[[`13cace140f`](https://github.com/nodejs/node/commit/13cace140f)] - **test**: fix helper-debugger-repl.js (Rich Trott) [#9486](https://github.com/nodejs/node/pull/9486)
* \[[`aebbc965f9`](https://github.com/nodejs/node/commit/aebbc965f9)] - **test**: refactor large event emitter tests (cjihrig) [#6446](https://github.com/nodejs/node/pull/6446)
* \[[`b5012f3de2`](https://github.com/nodejs/node/commit/b5012f3de2)] - **test**: add expectWarning to common (Michaël Zasso) [#8662](https://github.com/nodejs/node/pull/8662)
* \[[`b98813d97c`](https://github.com/nodejs/node/commit/b98813d97c)] - **test**: refactor test-fs-non-number-arguments-throw (Michaël Zasso) [#9844](https://github.com/nodejs/node/pull/9844)
* \[[`80a752708a`](https://github.com/nodejs/node/commit/80a752708a)] - **test**: refactor test-dgram-exclusive-implicit-bind (Cesar Hernandez) [#10066](https://github.com/nodejs/node/pull/10066)
* \[[`9b974b4d54`](https://github.com/nodejs/node/commit/9b974b4d54)] - **test**: use `assert.strictEqual` (anoff) [#9975](https://github.com/nodejs/node/pull/9975)
* \[[`bc125bd729`](https://github.com/nodejs/node/commit/bc125bd729)] - **test**: change assert.equal to assert.strictEqual (Aileen) [#9946](https://github.com/nodejs/node/pull/9946)
* \[[`5049a10278`](https://github.com/nodejs/node/commit/5049a10278)] - **test**: changed assert.equal to assert.strictEqual (vazina robertson) [#10015](https://github.com/nodejs/node/pull/10015)
* \[[`b5c60edeed`](https://github.com/nodejs/node/commit/b5c60edeed)] - **test**: renamed assert.Equal to assert.strictEqual (Jared Young)
* \[[`f44e828a36`](https://github.com/nodejs/node/commit/f44e828a36)] - **test**: improves test-tls-client-verify (Paul Graham) [#10051](https://github.com/nodejs/node/pull/10051)
* \[[`a1e3967f69`](https://github.com/nodejs/node/commit/a1e3967f69)] - **test**: refactor test-https-agent-session-reuse (Diego Paez) [#10105](https://github.com/nodejs/node/pull/10105)
* \[[`9e46af6412`](https://github.com/nodejs/node/commit/9e46af6412)] - **test**: refactor test-beforeexit-event (Rob Adelmann) [#10121](https://github.com/nodejs/node/pull/10121)
* \[[`adcd6ea66f`](https://github.com/nodejs/node/commit/adcd6ea66f)] - **test**: refactor test-domain-from-timer (Daniel Sims) [#9889](https://github.com/nodejs/node/pull/9889)
* \[[`1377ea87eb`](https://github.com/nodejs/node/commit/1377ea87eb)] - **test**: refactor test-domain-exit-dispose-again (Ethan Arrowood) [#10003](https://github.com/nodejs/node/pull/10003)
* \[[`8a9af6843d`](https://github.com/nodejs/node/commit/8a9af6843d)] - **test**: use const and strictEqual in test-os-homedir-no-envvar (CodeVana) [#9899](https://github.com/nodejs/node/pull/9899)
* \[[`ee038c0e71`](https://github.com/nodejs/node/commit/ee038c0e71)] - **test**: refactor test-dgram-bind-default-address (Michael-Bryant Choa) [#9947](https://github.com/nodejs/node/pull/9947)
* \[[`a090899e93`](https://github.com/nodejs/node/commit/a090899e93)] - **test**: assert.throws() should include a RegExp (Chris Bystrek) [#9976](https://github.com/nodejs/node/pull/997://github.com/nodejs/node/pull/9976)
* \[[`542b40f410`](https://github.com/nodejs/node/commit/542b40f410)] - **test**: refactor test-event-emitter-method-names (Rodrigo Palma) [#10027](https://github.com/nodejs/node/pull/10027)
* \[[`a2023a9d97`](https://github.com/nodejs/node/commit/a2023a9d97)] - **test**: refactor tls-ticket-cluster (Yojan Shrestha) [#10023](https://github.com/nodejs/node/pull/10023)
* \[[`a64f40680f`](https://github.com/nodejs/node/commit/a64f40680f)] - **test**: refactor test-domain-exit-dispose (Chris Henney) [#9938](https://github.com/nodejs/node/pull/9938)
* \[[`a896d4ed36`](https://github.com/nodejs/node/commit/a896d4ed36)] - **test**: refactor test-stdin-from-file.js (amrios) [#10012](https://github.com/nodejs/node/pull/10012)
* \[[`ce14c1e51f`](https://github.com/nodejs/node/commit/ce14c1e51f)] - **test**: refactor test-require-extensions-main (Daryl Thayil) [#9912](https://github.com/nodejs/node/pull/9912)
* \[[`b9c45026f7`](https://github.com/nodejs/node/commit/b9c45026f7)] - **test**: clean up tls junk test (Danny Guo) [#9940](https://github.com/nodejs/node/pull/9940)
* \[[`e3712334a3`](https://github.com/nodejs/node/commit/e3712334a3)] - **test**: update test-stdout-to-file (scalkpdev) [#9939](https://github.com/nodejs/node/pull/9939)
* \[[`63f571e69c`](https://github.com/nodejs/node/commit/63f571e69c)] - **test**: changed assert.Equal to asset.strictEqual (Paul Chin) [#9973](https://github.com/nodejs/node/pull/9973)
* \[[`c3a3480606`](https://github.com/nodejs/node/commit/c3a3480606)] - **test**: refactor test-domain-multi (Wes Tyler) [#9963](https://github.com/nodejs/node/pull/9963)
* \[[`ad27555ff8`](https://github.com/nodejs/node/commit/ad27555ff8)] - **test**: use assert.strictEqual in test-cli-eval (Nigel Kibodeaux) [#9919](https://github.com/nodejs/node/pull/9919)
* \[[`cffd51e815`](https://github.com/nodejs/node/commit/cffd51e815)] - **test**: refactor test-tls-connect-simple (Russell Sherman) [#9934](https://github.com/nodejs/node/pull/9934)
* \[[`1424c25f3e`](https://github.com/nodejs/node/commit/1424c25f3e)] - **test**: refactor test-signal-unregister (mark hughes) [#9920](https://github.com/nodejs/node/pull/9920)
* \[[`920737180f`](https://github.com/nodejs/node/commit/920737180f)] - **test**: refactor test-require-resolve (blugavere) [#10120](https://github.com/nodejs/node/pull/10120)
* \[[`71ab88cc80`](https://github.com/nodejs/node/commit/71ab88cc80)] - **test**: refactor test-fs-read-stream-resume (Matt Webb) [#9927](https://github.com/nodejs/node/pull/9927)
* \[[`6a485da87c`](https://github.com/nodejs/node/commit/6a485da87c)] - **test**: replace equal with strictEqual (Tracy Hinds) [#10011](https://github.com/nodejs/node/pull/10011)
* \[[`b5d87569e1`](https://github.com/nodejs/node/commit/b5d87569e1)] - **test**: use strictEqual instead of equal (Uttam Pawar) [#9921](https://github.com/nodejs/node/pull/9921)
* \[[`c94c2fde8a`](https://github.com/nodejs/node/commit/c94c2fde8a)] - **test**: using const and strictEqual (Fabrice Tatieze) [#9926](https://github.com/nodejs/node/pull/9926)
* \[[`16164b5b44`](https://github.com/nodejs/node/commit/16164b5b44)] - **test**: test-file-write-stream3.js refactor (Richard Karmazin) [#10035](https://github.com/nodejs/node/pull/10035)
* \[[`7391983729`](https://github.com/nodejs/node/commit/7391983729)] - **test**: implemented es6 conventions (Erez Weiss) [#9669](https://github.com/nodejs/node/pull/9669)
* \[[`50ce3f91d7`](https://github.com/nodejs/node/commit/50ce3f91d7)] - **test**: update assert.equal() to assert.strictEqual() (Peter Diaz) [#10024](https://github.com/nodejs/node/pull/10024)
* \[[`3f9d75c481`](https://github.com/nodejs/node/commit/3f9d75c481)] - **test**: use const or let and assert.strictEqual (Christopher Rokita) [#10001](https://github.com/nodejs/node/pull/10001)
* \[[`98afba5676`](https://github.com/nodejs/node/commit/98afba5676)] - **test**: use strictEqual() domain-http (cdnadmin) [#9996](https://github.com/nodejs/node/pull/9996)
* \[[`07680b65fe`](https://github.com/nodejs/node/commit/07680b65fe)] - **test**: refactor test-cluster-worker-events (fmizzell) [#9994](https://github.com/nodejs/node/pull/9994)
* \[[`a3db54416f`](https://github.com/nodejs/node/commit/a3db54416f)] - **test**: update repl tests (makenova) [#9991](https://github.com/nodejs/node/pull/9991)
* \[[`db3cdd2449`](https://github.com/nodejs/node/commit/db3cdd2449)] - **test**: adding strictEqual to test-buffer-indexof.js (Eric Gonzalez) [#9955](https://github.com/nodejs/node/pull/9955)
* \[[`f670b05603`](https://github.com/nodejs/node/commit/f670b05603)] - **test**: strictEqual in test-beforeexit-event.js (CodeTheInternet) [#10004](https://github.com/nodejs/node/pull/10004)
* \[[`70b4d7d3a2`](https://github.com/nodejs/node/commit/70b4d7d3a2)] - **test**: refactor test-child-process-double-pipe (Dan Villa) [#9930](https://github.com/nodejs/node/pull/9930)
* \[[`1e53cf4764`](https://github.com/nodejs/node/commit/1e53cf4764)] - **test**: updated test-stream-pipe-unpipe-stream (Raja Panidepu) [#10100](https://github.com/nodejs/node/pull/10100)
* \[[`57d48ac3f4`](https://github.com/nodejs/node/commit/57d48ac3f4)] - **test**: refactor test-crypto-ecb (michael6) [#10029](https://github.com/nodejs/node/pull/10029)
* \[[`89feb8dc4d`](https://github.com/nodejs/node/commit/89feb8dc4d)] - **test**: refactor test-require-exceptions (Oscar Martinez) [#9882](https://github.com/nodejs/node/pull/9882)
* \[[`59f259c487`](https://github.com/nodejs/node/commit/59f259c487)] - **test**: refactor test-crypto-certificate (Josh Mays) [#9911](https://github.com/nodejs/node/pull/9911)
* \[[`815715d850`](https://github.com/nodejs/node/commit/815715d850)] - **test**: refactor test-domain (Johnny Reading) [#9890](https://github.com/nodejs/node/pull/9890)
* \[[`08cc269338`](https://github.com/nodejs/node/commit/08cc269338)] - **test**: refactor test-cli-syntax (Exlipse7) [#10057](https://github.com/nodejs/node/pull/10057)
* \[[`91d27ce4db`](https://github.com/nodejs/node/commit/91d27ce4db)] - **test**: refactor test-child-process-constructor (k3kathy) [#10060](https://github.com/nodejs/node/pull/10060)
* \[[`ae9e2a21c1`](https://github.com/nodejs/node/commit/ae9e2a21c1)] - **test**: var to const, assert.equal to assert.strictEqual in net (Sean Villars) [#9907](https://github.com/nodejs/node/pull/9907)
* \[[`30c9474286`](https://github.com/nodejs/node/commit/30c9474286)] - **test**: changed vars to const in test-net-better-error-messages-listen-path.js (anoff) [#9905](https://github.com/nodejs/node/pull/9905)
* \[[`bcbf50d9ba`](https://github.com/nodejs/node/commit/bcbf50d9ba)] - **test**: refactor test-http-dns-error (Outsider) [#10062](https://github.com/nodejs/node/pull/10062)
* \[[`00f08640ce`](https://github.com/nodejs/node/commit/00f08640ce)] - **test**: assert.equal -> assert.strictEqual (davidmarkclements) [#10065](https://github.com/nodejs/node/pull/10065)
* \[[`d9cca393e9`](https://github.com/nodejs/node/commit/d9cca393e9)] - **test**: assert.equal -> assert.strictEqual (davidmarkclements) [#10067](https://github.com/nodejs/node/pull/10067)
* \[[`6c64f6c445`](https://github.com/nodejs/node/commit/6c64f6c445)] - **test**: improve test for crypto padding (Julian Duque) [#9906](https://github.com/nodejs/node/pull/9906)
* \[[`37d734ae36`](https://github.com/nodejs/node/commit/37d734ae36)] - **test**: polish test-net-better-error-messages-listen (Hitesh Kanwathirtha) [#10087](https://github.com/nodejs/node/pull/10087)
* \[[`f126b44a3a`](https://github.com/nodejs/node/commit/f126b44a3a)] - **test**: change var to const in test-tls-key-mismatch.js (bjdelro) [#9897](https://github.com/nodejs/node/pull/9897)
* \[[`7538dd5c93`](https://github.com/nodejs/node/commit/7538dd5c93)] - **test**: use strictEqual in cwd-enoent (JDHarmon) [#10077](https://github.com/nodejs/node/pull/10077)
* \[[`39816a43af`](https://github.com/nodejs/node/commit/39816a43af)] - **test**: refactor test-fs-read-stream-inherit.js (Jonathan Darling) [#9894](https://github.com/nodejs/node/pull/9894)
* \[[`7615a0f2cd`](https://github.com/nodejs/node/commit/7615a0f2cd)] - **test**: refactor test-child-process-stdio-inherit (Wes Tyler) [#9893](https://github.com/nodejs/node/pull/9893)
* \[[`2a9ab8ea2a`](https://github.com/nodejs/node/commit/2a9ab8ea2a)] - **test**: change var to const for require and strict equality checks (Harish Tejwani) [#9892](https://github.com/nodejs/node/pull/9892)
* \[[`5cd7e7aaf1`](https://github.com/nodejs/node/commit/5cd7e7aaf1)] - **test**: Update to const and use regex for assertions (Daniel Flores) [#9891](https://github.com/nodejs/node/pull/9891)
* \[[`1a73cc5357`](https://github.com/nodejs/node/commit/1a73cc5357)] - **test**: swap var->const/let and equal->strictEqual (Peter Masucci) [#9888](https://github.com/nodejs/node/pull/9888)
* \[[`552169e950`](https://github.com/nodejs/node/commit/552169e950)] - **test**: replace equal with strictEqual in crypto (Julian Duque) [#9886](https://github.com/nodejs/node/pull/9886)
* \[[`49900e78b0`](https://github.com/nodejs/node/commit/49900e78b0)] - **test**: replace equal with strictEqual (Julian Duque) [#9879](https://github.com/nodejs/node/pull/9879)
* \[[`998db3a003`](https://github.com/nodejs/node/commit/998db3a003)] - **test**: refactor test-tls-timeout-server-2 (Devon Rifkin) [#9876](https://github.com/nodejs/node/pull/9876)
* \[[`aaab51047f`](https://github.com/nodejs/node/commit/aaab51047f)] - **test**: Changed assert.equal to assert.strictEqual (Daniel Pittman) [#9902](https://github.com/nodejs/node/pull/9902)
* \[[`a4488c3cbd`](https://github.com/nodejs/node/commit/a4488c3cbd)] - **test**: refactor test-vm-syntax-error-stderr.js (Jay Brownlee) [#9900](https://github.com/nodejs/node/pull/9900)
* \[[`cff80a5c0e`](https://github.com/nodejs/node/commit/cff80a5c0e)] - **test**: refactor test-tls-destroy-whilst-write (Chris Bystrek) [#10064](https://github.com/nodejs/node/pull/10064)
* \[[`8257671bdc`](https://github.com/nodejs/node/commit/8257671bdc)] - **test**: refactor test-https-truncate (davidmarkclements) [#10074](https://github.com/nodejs/node/pull/10074)
* \[[`457af874b5`](https://github.com/nodejs/node/commit/457af874b5)] - **test**: use strictEqual in test-cli-eval-event.js (Richard Karmazin) [#9964](https://github.com/nodejs/node/pull/9964)
* \[[`2890f0d904`](https://github.com/nodejs/node/commit/2890f0d904)] - **test**: refactor test-tls-friendly-error-message.js (Adrian Estrada) [#9967](https://github.com/nodejs/node/pull/9967)
* \[[`c37ae4a1b6`](https://github.com/nodejs/node/commit/c37ae4a1b6)] - **test**: refactor test-vm-static-this.js (David Bradford) [#9887](https://github.com/nodejs/node/pull/9887)
* \[[`9473fc6c2f`](https://github.com/nodejs/node/commit/9473fc6c2f)] - **test**: refactor test-crypto-cipheriv-decipheriv (Aileen) [#10018](https://github.com/nodejs/node/pull/10018)
* \[[`6ecc4ffb1c`](https://github.com/nodejs/node/commit/6ecc4ffb1c)] - **test**: refactor test for crypto cipher/decipher iv (Julian Duque) [#9943](https://github.com/nodejs/node/pull/9943)
* \[[`a486f6bad4`](https://github.com/nodejs/node/commit/a486f6bad4)] - **test**: refactor test-cluster-setup-master-argv (Oscar Martinez) [#9960](https://github.com/nodejs/node/pull/9960)
* \[[`384c954698`](https://github.com/nodejs/node/commit/384c954698)] - **test**: refactor test-cluster-setup-master-argv (Christine Hong) [#9993](https://github.com/nodejs/node/pull/9993)
* \[[`76645e8781`](https://github.com/nodejs/node/commit/76645e8781)] - **test**: use assert.strictEqual in test-crypto-ecb (Daniel Pittman) [#9980](https://github.com/nodejs/node/pull/9980)
* \[[`9103c3d3fe`](https://github.com/nodejs/node/commit/9103c3d3fe)] - **test**: update to const iin cluster test (Greg Valdez) [#10007](https://github.com/nodejs/node/pull/10007)
* \[[`27c9171586`](https://github.com/nodejs/node/commit/27c9171586)] - **test**: use assert.strictEqual() cluster test (Bidur Adhikari) [#10042](https://github.com/nodejs/node/pull/10042)
* \[[`2453d64aa7`](https://github.com/nodejs/node/commit/2453d64aa7)] - **test**: var -> let/const, .equal -> .strictEqual (shiya) [#9913](https://github.com/nodejs/node/pull/9913)
* \[[`1467c964a4`](https://github.com/nodejs/node/commit/1467c964a4)] - **test**: increase coverage for timers (lrlna) [#10068](https://github.com/nodejs/node/pull/10068)
* \[[`e47195cf78`](https://github.com/nodejs/node/commit/e47195cf78)] - **test**: change equal to strictEqual (Kevin Zurawel) [#9872](https://github.com/nodejs/node/pull/9872)
* \[[`33da22aba1`](https://github.com/nodejs/node/commit/33da22aba1)] - **test**: add toASCII and toUnicode punycode tests (Claudio Rodriguez) [#9741](https://github.com/nodejs/node/pull/9741)
* \[[`4c5d24b632`](https://github.com/nodejs/node/commit/4c5d24b632)] - **test**: refine test-http-status-reason-invalid-chars (Rich Trott) [#9802](https://github.com/nodejs/node/pull/9802)
* \[[`81d49aaeb2`](https://github.com/nodejs/node/commit/81d49aaeb2)] - **test**: exclude no\_interleaved\_stdio test for AIX (Michael Dawson) [#9772](https://github.com/nodejs/node/pull/9772)
* \[[`b59cf582e4`](https://github.com/nodejs/node/commit/b59cf582e4)] - **test**: refactor test-async-wrap-\* (Rich Trott) [#9663](https://github.com/nodejs/node/pull/9663)
* \[[`57cc5cb277`](https://github.com/nodejs/node/commit/57cc5cb277)] - **test**: use setImmediate() in test of stream2 (masashi.g) [#9583](https://github.com/nodejs/node/pull/9583)
* \[[`8345ffb0a0`](https://github.com/nodejs/node/commit/8345ffb0a0)] - **test**: add test case of PassThrough (Yoshiya Hinosawa) [#9581](https://github.com/nodejs/node/pull/9581)
* \[[`beb147a08b`](https://github.com/nodejs/node/commit/beb147a08b)] - **test**: check that `process.execPath` is a realpath (Anna Henningsen) [#9229](https://github.com/nodejs/node/pull/9229)
* \[[`cef5b1fa14`](https://github.com/nodejs/node/commit/cef5b1fa14)] - **test**: add test for broken child process stdio (cjihrig) [#9528](https://github.com/nodejs/node/pull/9528)
* \[[`29ab76b791`](https://github.com/nodejs/node/commit/29ab76b791)] - **test**: ensure nextTick is not scheduled in exit (Jeremiah Senkpiel) [#9555](https://github.com/nodejs/node/pull/9555)
* \[[`b87fe250d2`](https://github.com/nodejs/node/commit/b87fe250d2)] - **test**: change from setTimeout to setImmediate (MURAKAMI Masahiko) [#9578](https://github.com/nodejs/node/pull/9578)
* \[[`eca12d4316`](https://github.com/nodejs/node/commit/eca12d4316)] - **test**: improve test-stream2-objects.js (Yoshiya Hinosawa) [#9565](https://github.com/nodejs/node/pull/9565)
* \[[`4e36a14c15`](https://github.com/nodejs/node/commit/4e36a14c15)] - **test**: refactor test-next-tick-error-spin (Rich Trott) [#9537](https://github.com/nodejs/node/pull/9537)
* \[[`b2b2bc2293`](https://github.com/nodejs/node/commit/b2b2bc2293)] - **test**: move timer-dependent test to sequential (Rich Trott) [#9487](https://github.com/nodejs/node/pull/9487)
* \[[`1436fd70f5`](https://github.com/nodejs/node/commit/1436fd70f5)] - **test**: convert assert.equal to assert.strictEqual (Jonathan Darling) [#9925](https://github.com/nodejs/node/pull/9925)
* \[[`c9ed49da6e`](https://github.com/nodejs/node/commit/c9ed49da6e)] - **test**: run cpplint on files in test/cctest (Ben Noordhuis) [#9787](https://github.com/nodejs/node/pull/9787)
* \[[`10d4f470f8`](https://github.com/nodejs/node/commit/10d4f470f8)] - **test**: enable addons test to pass with debug build (Daniel Bevenius) [#8836](https://github.com/nodejs/node/pull/8836)
* \[[`550393dc78`](https://github.com/nodejs/node/commit/550393dc78)] - **test**: add new\.target add-on regression test (Ben Noordhuis) [#9689](https://github.com/nodejs/node/pull/9689)
* \[[`76245b2156`](https://github.com/nodejs/node/commit/76245b2156)] - **test**: refactor large event emitter tests (cjihrig) [#6446](https://github.com/nodejs/node/pull/6446)
* \[[`02e8187751`](https://github.com/nodejs/node/commit/02e8187751)] - **test**: allow globals to be whitelisted (cjihrig) [#7826](https://github.com/nodejs/node/pull/7826)
* \[[`c0c5608bfc`](https://github.com/nodejs/node/commit/c0c5608bfc)] - **test,assert**: add deepEqual/deepStrictEqual tests for typed arrays (Feross Aboukhadijeh) [#8002](https://github.com/nodejs/node/pull/8002)
* \[[`759e8fdd18`](https://github.com/nodejs/node/commit/759e8fdd18)] - **timers**: bail from intervals if \_repeat is bad (Jeremiah Senkpiel) [#10365](https://github.com/nodejs/node/pull/10365)
* \[[`553d95da15`](https://github.com/nodejs/node/commit/553d95da15)] - **timers**: use consistent checks for canceled timers (Jeremiah Senkpiel) [#9685](https://github.com/nodejs/node/pull/9685)
* \[[`5c6d908dd7`](https://github.com/nodejs/node/commit/5c6d908dd7)] - **tools**: enable final newline in .editorconfig (Roman Reiss) [#9410](https://github.com/nodejs/node/pull/9410)
* \[[`06e8120928`](https://github.com/nodejs/node/commit/06e8120928)] - **tools**: remove unneeded escaping in generate.js (Rich Trott) [#9781](https://github.com/nodejs/node/pull/9781)
* \[[`fd6b305421`](https://github.com/nodejs/node/commit/fd6b305421)] - **tools**: use better regexp for manpage references (Anna Henningsen) [#9632](https://github.com/nodejs/node/pull/9632)
* \[[`9b36469a3c`](https://github.com/nodejs/node/commit/9b36469a3c)] - **tools**: improve docopen target in Makefile (Sakthipriyan Vairamani (thefourtheye)) [#9436](https://github.com/nodejs/node/pull/9436)
* \[[`e3dc05d01b`](https://github.com/nodejs/node/commit/e3dc05d01b)] - **tools**: make run-valgrind.py useful (Ben Noordhuis) [#9520](https://github.com/nodejs/node/pull/9520)
* \[[`7b1b11a11c`](https://github.com/nodejs/node/commit/7b1b11a11c)] - **tools**: fix run-valgrind.py script (Ben Noordhuis) [#9520](https://github.com/nodejs/node/pull/9520)
* \[[`011ee0ba8b`](https://github.com/nodejs/node/commit/011ee0ba8b)] - **tools**: copy run-valgrind.py to tools/ (Ben Noordhuis) [#9520](https://github.com/nodejs/node/pull/9520)

<a id="4.7.0"></a>

## 2016-12-06, Version 4.7.0 'Argon' (LTS), @thealphanerd

This LTS release comes with 108 commits. This includes 30 which are doc
related, 28 which are test related, 16 which are build / tool related, and 4
commits which are updates to dependencies.

### Notable Changes

The SEMVER-MINOR changes include:

* **build**: export openssl symbols on Windows making it possible to build addons linking against the bundled version of openssl (Alex Hultman) [#7576](https://github.com/nodejs/node/pull/7576)
* **debugger**: make listen address configurable in the debugger server (Ben Noordhuis) [#3316](https://github.com/nodejs/node/pull/3316)
* **dgram**: generalized send queue to handle close fixing a potential throw when dgram socket is closed in the listening event handler. (Matteo Collina) [#7066](https://github.com/nodejs/node/pull/7066)
* **http**: Introduce the 451 status code "Unavailable For Legal Reasons" (Max Barinov) [#4377](https://github.com/nodejs/node/pull/4377)
* **tls**: introduce `secureContext` for `tls.connect` which is useful for caching client certificates, key, and CA certificates. (Fedor Indutny) [#4246](https://github.com/nodejs/node/pull/4246)

Notable SEMVER-PATCH changes include:

* **build**:
  * introduce the configure --shared option for embedders (sxa555) [#6994](https://github.com/nodejs/node/pull/6994)
* **gtest**: the test reporter now outputs tap comments as yamlish (Johan Bergström) [#9262](https://github.com/nodejs/node/pull/9262)
* **src**: node no longer aborts when c-ares initialization fails (Ben Noordhuis) [#8710](https://github.com/nodejs/node/pull/8710)
* **tls**: fix memory leak when writing data to TLSWrap instance during handshake (Fedor Indutny) [#9586](https://github.com/nodejs/node/pull/9586)

### Commits

* \[[`ed31f9cc30`](https://github.com/nodejs/node/commit/ed31f9cc30)] - **benchmark**: add microbenchmarks for ES Map (Rod Vagg) [#7581](https://github.com/nodejs/node/pull/7581)
* \[[`c5181eda4b`](https://github.com/nodejs/node/commit/c5181eda4b)] - **build**: reduce noise from doc target (Daniel Bevenius) [#9457](https://github.com/nodejs/node/pull/9457)
* \[[`59d821debe`](https://github.com/nodejs/node/commit/59d821debe)] - **build**: use wxneeded on openbsd (Aaron Bieber) [#9232](https://github.com/nodejs/node/pull/9232)
* \[[`7c73105606`](https://github.com/nodejs/node/commit/7c73105606)] - **build**: run cctests as part of test-ci target (Ben Noordhuis) [#8034](https://github.com/nodejs/node/pull/8034)
* \[[`3919edb47e`](https://github.com/nodejs/node/commit/3919edb47e)] - **build**: don't build icu with -fno-rtti (Ben Noordhuis) [#8886](https://github.com/nodejs/node/pull/8886)
* \[[`e97723b18c`](https://github.com/nodejs/node/commit/e97723b18c)] - **build**: abstract out shared library suffix (Stewart Addison) [#9385](https://github.com/nodejs/node/pull/9385)
* \[[`0138b4db7c`](https://github.com/nodejs/node/commit/0138b4db7c)] - **build**: windows sharedlib support (Stewart Addison) [#9385](https://github.com/nodejs/node/pull/9385)
* \[[`f21c2b9d3b`](https://github.com/nodejs/node/commit/f21c2b9d3b)] - **build**: configure --shared (sxa555) [#6994](https://github.com/nodejs/node/pull/6994)
* \[[`bb2fdf58f7`](https://github.com/nodejs/node/commit/bb2fdf58f7)] - **build**: cherry pick V8 change for windows DLL support (Stefan Budeanu) [#8084](https://github.com/nodejs/node/pull/8084)
* \[[`84849f186f`](https://github.com/nodejs/node/commit/84849f186f)] - **(SEMVER-MINOR)** **build**: export more openssl symbols on Windows (Alex Hultman) [#7576](https://github.com/nodejs/node/pull/7576)
* \[[`3cefd65e90`](https://github.com/nodejs/node/commit/3cefd65e90)] - **build**: export openssl symbols on windows (Ben Noordhuis) [#6274](https://github.com/nodejs/node/pull/6274)
* \[[`4de7a6e291`](https://github.com/nodejs/node/commit/4de7a6e291)] - **build**: fix config.gypi target (Daniel Bevenius) [#9053](https://github.com/nodejs/node/pull/9053)
* \[[`9389572cbc`](https://github.com/nodejs/node/commit/9389572cbc)] - **crypto**: fix faulty logic in iv size check (Ben Noordhuis) [#9032](https://github.com/nodejs/node/pull/9032)
* \[[`748e424163`](https://github.com/nodejs/node/commit/748e424163)] - **(SEMVER-MINOR)** **debugger**: make listen address configurable (Ben Noordhuis) [#3316](https://github.com/nodejs/node/pull/3316)
* \[[`c1effb1255`](https://github.com/nodejs/node/commit/c1effb1255)] - **deps**: fix build with libc++ 3.8.0 (Johan Bergström) [#9763](https://github.com/nodejs/node/pull/9763)
* \[[`eb34f687d5`](https://github.com/nodejs/node/commit/eb34f687d5)] - **deps**: revert default gtest reporter change (Brian White) [#8948](https://github.com/nodejs/node/pull/8948)
* \[[`4c47446133`](https://github.com/nodejs/node/commit/4c47446133)] - **deps**: make gtest output tap (Ben Noordhuis) [#8034](https://github.com/nodejs/node/pull/8034)
* \[[`91fce10aee`](https://github.com/nodejs/node/commit/91fce10aee)] - **deps**: back port OpenBSD fix in c-ares/c-ares (Aaron Bieber) [#9232](https://github.com/nodejs/node/pull/9232)
* \[[`4571c84c67`](https://github.com/nodejs/node/commit/4571c84c67)] - **(SEMVER-MINOR)** **dgram**: generalized send queue to handle close (Matteo Collina) [#7066](https://github.com/nodejs/node/pull/7066)
* \[[`d3c25c19ef`](https://github.com/nodejs/node/commit/d3c25c19ef)] - **doc**: update minute-taking procedure for CTC (Rich Trott) [#9425](https://github.com/nodejs/node/pull/9425)
* \[[`861b689c01`](https://github.com/nodejs/node/commit/861b689c01)] - **doc**: update GOVERNANCE.md to use "meeting chair" (Rich Trott) [#9432](https://github.com/nodejs/node/pull/9432)
* \[[`5e820ae746`](https://github.com/nodejs/node/commit/5e820ae746)] - **doc**: update Diagnostics WG info (Josh Gavant) [#9329](https://github.com/nodejs/node/pull/9329)
* \[[`e08173a2f1`](https://github.com/nodejs/node/commit/e08173a2f1)] - **doc**: fix outdate ninja link (Yangyang Liu) [#9278](https://github.com/nodejs/node/pull/9278)
* \[[`462c640a51`](https://github.com/nodejs/node/commit/462c640a51)] - **doc**: fix typo in email address in README (Rich Trott) [#8941](https://github.com/nodejs/node/pull/8941)
* \[[`fc77cbb5b1`](https://github.com/nodejs/node/commit/fc77cbb5b1)] - **doc**: make node(1) more consistent with tradition (Alex Jordan) [#8902](https://github.com/nodejs/node/pull/8902)
* \[[`66e26cd253`](https://github.com/nodejs/node/commit/66e26cd253)] - **doc**: child\_process.execSync .stdio default is pipe (Kenneth Skovhus) [#9701](https://github.com/nodejs/node/pull/9701)
* \[[`524ebfb5dd`](https://github.com/nodejs/node/commit/524ebfb5dd)] - **doc**: child\_process .stdio accepts a String type (Kenneth Skovhus) [#9701](https://github.com/nodejs/node/pull/9701)
* \[[`475fe96852`](https://github.com/nodejs/node/commit/475fe96852)] - **doc**: simplify process.memoryUsage() example code (Thomas Watson Steen) [#9560](https://github.com/nodejs/node/pull/9560)
* \[[`c48c318806`](https://github.com/nodejs/node/commit/c48c318806)] - **doc**: change ./node to node in debugger.md (AnnaMag) [#8943](https://github.com/nodejs/node/pull/8943)
* \[[`00a178257c`](https://github.com/nodejs/node/commit/00a178257c)] - **doc**: update CONTRIBUTING.md to address editing PRs (Gibson Fahnestock) [#9259](https://github.com/nodejs/node/pull/9259)
* \[[`2b2dde855a`](https://github.com/nodejs/node/commit/2b2dde855a)] - **doc**: add italoacasas to collaborators (Italo A. Casas) [#9677](https://github.com/nodejs/node/pull/9677)
* \[[`0f41058e41`](https://github.com/nodejs/node/commit/0f41058e41)] - **doc**: clarify relation between a file and a module (marzelin) [#9026](https://github.com/nodejs/node/pull/9026)
* \[[`d1d207bd75`](https://github.com/nodejs/node/commit/d1d207bd75)] - **doc**: add Sakthipriyan to the CTC (Rod Vagg) [#9427](https://github.com/nodejs/node/pull/9427)
* \[[`9dad98bdf1`](https://github.com/nodejs/node/commit/9dad98bdf1)] - **doc**: add 2016-10-26 CTC meeting minutes (Rich Trott) [#9348](https://github.com/nodejs/node/pull/9348)
* \[[`824009296a`](https://github.com/nodejs/node/commit/824009296a)] - **doc**: add 2016-10-05 CTC meeting minutes (Josh Gavant) [#9326](https://github.com/nodejs/node/pull/9326)
* \[[`1a701f1723`](https://github.com/nodejs/node/commit/1a701f1723)] - **doc**: add 2016-09-28 CTC meeting minutes (Josh Gavant) [#9325](https://github.com/nodejs/node/pull/9325)
* \[[`e9c6aff113`](https://github.com/nodejs/node/commit/e9c6aff113)] - **doc**: add 2016-10-19 CTC meeting minutes (Josh Gavant) [#9193](https://github.com/nodejs/node/pull/9193)
* \[[`c1e5e663a9`](https://github.com/nodejs/node/commit/c1e5e663a9)] - **doc**: improve header styling for API docs (Jeremiah Senkpiel) [#8811](https://github.com/nodejs/node/pull/8811)
* \[[`279e30c3ee`](https://github.com/nodejs/node/commit/279e30c3ee)] - **doc**: add CTC meeting minutes for 2016-10-12 (Michael Dawson) [#9070](https://github.com/nodejs/node/pull/9070)
* \[[`3b839d1855`](https://github.com/nodejs/node/commit/3b839d1855)] - **doc**: remove confusing reference in governance doc (Rich Trott) [#9073](https://github.com/nodejs/node/pull/9073)
* \[[`e564cb6af4`](https://github.com/nodejs/node/commit/e564cb6af4)] - **doc**: add ctc-review label information (Rich Trott) [#9072](https://github.com/nodejs/node/pull/9072)
* \[[`68ccc7a512`](https://github.com/nodejs/node/commit/68ccc7a512)] - **doc**: update reference to list hash algorithms in crypto.md (scott stern) [#9043](https://github.com/nodejs/node/pull/9043)
* \[[`132425a058`](https://github.com/nodejs/node/commit/132425a058)] - **doc**: specify that errno is a number, not a string (John Vilk) [#9007](https://github.com/nodejs/node/pull/9007)
* \[[`695ee1e77b`](https://github.com/nodejs/node/commit/695ee1e77b)] - **doc**: highlight deprecated API in ToC (Ilya Frolov) [#7189](https://github.com/nodejs/node/pull/7189)
* \[[`4f8bf1bcf8`](https://github.com/nodejs/node/commit/4f8bf1bcf8)] - **doc**: explains why Reviewed-By is added in PRs (jessicaquynh) [#9044](https://github.com/nodejs/node/pull/9044)
* \[[`af645a0553`](https://github.com/nodejs/node/commit/af645a0553)] - **doc**: explain why GitHub merge button is not used (jessicaquynh) [#9044](https://github.com/nodejs/node/pull/9044)
* \[[`f472c09e90`](https://github.com/nodejs/node/commit/f472c09e90)] - **doc**: reference signal(7) for the list of signals (Emanuele DelBono) [#9323](https://github.com/nodejs/node/pull/9323)
* \[[`88079817c2`](https://github.com/nodejs/node/commit/88079817c2)] - **doc**: fix typo in http.md (anu0012) [#9144](https://github.com/nodejs/node/pull/9144)
* \[[`9f0ef5a4f2`](https://github.com/nodejs/node/commit/9f0ef5a4f2)] - **doc**: fix heading type for v4.6.2 changelog (Myles Borins) [#9515](https://github.com/nodejs/node/pull/9515)
* \[[`f6f0b387ea`](https://github.com/nodejs/node/commit/f6f0b387ea)] - **events**: pass the original listener added by once (DavidCai) [#6394](https://github.com/nodejs/node/pull/6394)
* \[[`02e6c84de2`](https://github.com/nodejs/node/commit/02e6c84de2)] - **gitignore**: ignore all tap files (Johan Bergström) [#9262](https://github.com/nodejs/node/pull/9262)
* \[[`a7ae8876f9`](https://github.com/nodejs/node/commit/a7ae8876f9)] - **governance**: expand use of CTC issue tracker (Rich Trott) [#8945](https://github.com/nodejs/node/pull/8945)
* \[[`36abbbe736`](https://github.com/nodejs/node/commit/36abbbe736)] - **gtest**: output tap comments as yamlish (Johan Bergström) [#9262](https://github.com/nodejs/node/pull/9262)
* \[[`50a4471aff`](https://github.com/nodejs/node/commit/50a4471aff)] - **http**: fix connection upgrade checks (Brian White) [#8238](https://github.com/nodejs/node/pull/8238)
* \[[`c94482b167`](https://github.com/nodejs/node/commit/c94482b167)] - **(SEMVER-MINOR)** **http**: 451 status code "Unavailable For Legal Reasons" (Max Barinov) [#4377](https://github.com/nodejs/node/pull/4377)
* \[[`12da2581a8`](https://github.com/nodejs/node/commit/12da2581a8)] - **https**: fix memory leak with https.request() (Ilkka Myller) [#8647](https://github.com/nodejs/node/pull/8647)
* \[[`3b448a7f12`](https://github.com/nodejs/node/commit/3b448a7f12)] - **lib**: changed var to const in linkedlist (Adri Van Houdt) [#8609](https://github.com/nodejs/node/pull/8609)
* \[[`a3a184d40a`](https://github.com/nodejs/node/commit/a3a184d40a)] - **lib**: fix TypeError in v8-polyfill (Wyatt Preul) [#8863](https://github.com/nodejs/node/pull/8863)
* \[[`423846053b`](https://github.com/nodejs/node/commit/423846053b)] - **lib**: remove let from for loops (Myles Borins) [#8873](https://github.com/nodejs/node/pull/8873)
* \[[`9a192a9683`](https://github.com/nodejs/node/commit/9a192a9683)] - **net**: fix ambiguity in EOF handling (Fedor Indutny) [#9066](https://github.com/nodejs/node/pull/9066)
* \[[`62e83b363e`](https://github.com/nodejs/node/commit/62e83b363e)] - **src**: Malloc/Calloc size 0 returns non-null pointer (Rich Trott) [#8572](https://github.com/nodejs/node/pull/8572)
* \[[`51e09d00c4`](https://github.com/nodejs/node/commit/51e09d00c4)] - **src**: normalize malloc, realloc (Michael Dawson) [#7564](https://github.com/nodejs/node/pull/7564)
* \[[`3b5cedebd1`](https://github.com/nodejs/node/commit/3b5cedebd1)] - **src**: renaming ares\_task struct to node\_ares\_task (Daniel Bevenius) [#7345](https://github.com/nodejs/node/pull/7345)
* \[[`e5d2a95d68`](https://github.com/nodejs/node/commit/e5d2a95d68)] - **src**: remove out-of-date TODO comment (Daniel Bevenius) [#9000](https://github.com/nodejs/node/pull/9000)
* \[[`b4353e9017`](https://github.com/nodejs/node/commit/b4353e9017)] - **src**: fix typo in #endif comment (Juan Andres Andrango) [#8989](https://github.com/nodejs/node/pull/8989)
* \[[`f0192ec195`](https://github.com/nodejs/node/commit/f0192ec195)] - **src**: don't abort when c-ares initialization fails (Ben Noordhuis) [#8710](https://github.com/nodejs/node/pull/8710)
* \[[`f669a08b76`](https://github.com/nodejs/node/commit/f669a08b76)] - **src**: fix typo rval to value (Miguel Angel Asencio Hurtado) [#9023](https://github.com/nodejs/node/pull/9023)
* \[[`9b9762ccec`](https://github.com/nodejs/node/commit/9b9762ccec)] - **streams**: fix regression in `unpipe()` (Anna Henningsen) [#9171](https://github.com/nodejs/node/pull/9171)
* \[[`cc36a63205`](https://github.com/nodejs/node/commit/cc36a63205)] - **test**: remove watchdog in test-debug-signal-cluster (Rich Trott) [#9476](https://github.com/nodejs/node/pull/9476)
* \[[`9144d373ba`](https://github.com/nodejs/node/commit/9144d373ba)] - **test**: cleanup test-dgram-error-message-address (Michael Macherey) [#8938](https://github.com/nodejs/node/pull/8938)
* \[[`96bdfae041`](https://github.com/nodejs/node/commit/96bdfae041)] - **test**: improve test-debugger-util-regression (Santiago Gimeno) [#9490](https://github.com/nodejs/node/pull/9490)
* \[[`2c758861c0`](https://github.com/nodejs/node/commit/2c758861c0)] - **test**: move timer-dependent test to sequential (Rich Trott) [#9431](https://github.com/nodejs/node/pull/9431)
* \[[`d9955fbb17`](https://github.com/nodejs/node/commit/d9955fbb17)] - **test**: add test for HTTP client "aborted" event (Kyle E. Mitchell) [#7376](https://github.com/nodejs/node/pull/7376)
* \[[`b0476c5590`](https://github.com/nodejs/node/commit/b0476c5590)] - **test**: fix flaky test-fs-watch-recursive on OS X (Rich Trott) [#9303](https://github.com/nodejs/node/pull/9303)
* \[[`bcd156f4ab`](https://github.com/nodejs/node/commit/bcd156f4ab)] - **test**: refactor test-async-wrap-check-providers (Gerges Beshay) [#9297](https://github.com/nodejs/node/pull/9297)
* \[[`9d5e7f5c85`](https://github.com/nodejs/node/commit/9d5e7f5c85)] - **test**: use strict assertions in module loader test (Ben Noordhuis) [#9263](https://github.com/nodejs/node/pull/9263)
* \[[`6d742b3fdd`](https://github.com/nodejs/node/commit/6d742b3fdd)] - **test**: remove err timer from test-http-set-timeout (BethGriggs) [#9264](https://github.com/nodejs/node/pull/9264)
* \[[`51b251d8eb`](https://github.com/nodejs/node/commit/51b251d8eb)] - **test**: add coverage for spawnSync() killSignal (cjihrig) [#8960](https://github.com/nodejs/node/pull/8960)
* \[[`fafffd4f99`](https://github.com/nodejs/node/commit/fafffd4f99)] - **test**: fix test-child-process-fork-regr-gh-2847 (Santiago Gimeno) [#8954](https://github.com/nodejs/node/pull/8954)
* \[[`a2621a25e5`](https://github.com/nodejs/node/commit/a2621a25e5)] - **test**: remove FIXME pummel/test-tls-securepair-client (Alfred Cepeda) [#8757](https://github.com/nodejs/node/pull/8757)
* \[[`747013bc39`](https://github.com/nodejs/node/commit/747013bc39)] - **test**: output tap13 instead of almost-tap (Johan Bergström) [#9262](https://github.com/nodejs/node/pull/9262)
* \[[`790406661d`](https://github.com/nodejs/node/commit/790406661d)] - **test**: refactor test-net-server-max-connections (Rich Trott) [#8931](https://github.com/nodejs/node/pull/8931)
* \[[`347547a97e`](https://github.com/nodejs/node/commit/347547a97e)] - **test**: expand test coverage for url.js (Junshu Okamoto) [#8859](https://github.com/nodejs/node/pull/8859)
* \[[`cec5e36df7`](https://github.com/nodejs/node/commit/cec5e36df7)] - **test**: fix test-cluster-worker-init.js flakyness (Ilkka Myller) [#8703](https://github.com/nodejs/node/pull/8703)
* \[[`b3fccc2536`](https://github.com/nodejs/node/commit/b3fccc2536)] - **test**: enable cyrillic punycode test case (Ben Noordhuis) [#8695](https://github.com/nodejs/node/pull/8695)
* \[[`03f703177f`](https://github.com/nodejs/node/commit/03f703177f)] - **test**: remove call to `net.Socket.resume()` (Alfred Cepeda) [#8679](https://github.com/nodejs/node/pull/8679)
* \[[`527db40932`](https://github.com/nodejs/node/commit/527db40932)] - **test**: add coverage for execFileSync() errors (cjihrig) [#9211](https://github.com/nodejs/node/pull/9211)
* \[[`40ef23969d`](https://github.com/nodejs/node/commit/40ef23969d)] - **test**: writable stream needDrain state (Italo A. Casas) [#8799](https://github.com/nodejs/node/pull/8799)
* \[[`ba4a3ede56`](https://github.com/nodejs/node/commit/ba4a3ede56)] - **test**: writable stream ending state (Italo A. Casas) [#8707](https://github.com/nodejs/node/pull/8707)
* \[[`80a26c7540`](https://github.com/nodejs/node/commit/80a26c7540)] - **test**: writable stream finished state (Italo A. Casas) [#8791](https://github.com/nodejs/node/pull/8791)
* \[[`a64af39c83`](https://github.com/nodejs/node/commit/a64af39c83)] - **test**: remove duplicate required module (Rich Trott) [#9169](https://github.com/nodejs/node/pull/9169)
* \[[`a038fcc307`](https://github.com/nodejs/node/commit/a038fcc307)] - **test**: add regression test for instanceof (Franziska Hinkelmann) [#9178](https://github.com/nodejs/node/pull/9178)
* \[[`bd99b2d4e4`](https://github.com/nodejs/node/commit/bd99b2d4e4)] - **test**: checking if error constructor is assert.AssertionError (larissayvette) [#9119](https://github.com/nodejs/node/pull/9119)
* \[[`4a6bd8683f`](https://github.com/nodejs/node/commit/4a6bd8683f)] - **test**: fix flaky test-child-process-fork-dgram (Rich Trott) [#9098](https://github.com/nodejs/node/pull/9098)
* \[[`d9c33646e6`](https://github.com/nodejs/node/commit/d9c33646e6)] - **test**: add regression test for `unpipe()` (Niels Nielsen) [#9171](https://github.com/nodejs/node/pull/9171)
* \[[`f9b24f42ba`](https://github.com/nodejs/node/commit/f9b24f42ba)] - **test**: use npm sandbox in test-npm-install (João Reis) [#9079](https://github.com/nodejs/node/pull/9079)
* \[[`54c38eb22e`](https://github.com/nodejs/node/commit/54c38eb22e)] - **tickprocessor**: apply c++filt manually on mac (Fedor Indutny) [#8480](https://github.com/nodejs/node/pull/8480)
* \[[`bf25994308`](https://github.com/nodejs/node/commit/bf25994308)] - **tls**: fix leak of WriteWrap+TLSWrap combination (Fedor Indutny) [#9586](https://github.com/nodejs/node/pull/9586)
* \[[`9049c1f6b6`](https://github.com/nodejs/node/commit/9049c1f6b6)] - **(SEMVER-MINOR)** **tls**: introduce `secureContext` for `tls.connect` (Fedor Indutny) [#4246](https://github.com/nodejs/node/pull/4246)
* \[[`b1bd1c42c0`](https://github.com/nodejs/node/commit/b1bd1c42c0)] - **tools**: allow test.py to use full paths of tests (Francis Gulotta) [#9694](https://github.com/nodejs/node/pull/9694)
* \[[`533ce48b6a`](https://github.com/nodejs/node/commit/533ce48b6a)] - **tools**: make --repeat work with -j in test.py (Rich Trott) [#9249](https://github.com/nodejs/node/pull/9249)
* \[[`f9baa1119f`](https://github.com/nodejs/node/commit/f9baa1119f)] - **tools**: remove dangling eslint symlink (Sam Roberts) [#9299](https://github.com/nodejs/node/pull/9299)
* \[[`c8dccf29dd`](https://github.com/nodejs/node/commit/c8dccf29dd)] - **tools**: avoid let in for loops (jessicaquynh) [#9049](https://github.com/nodejs/node/pull/9049)
* \[[`620cdc5ce8`](https://github.com/nodejs/node/commit/620cdc5ce8)] - **tools**: fix release script on macOS 10.12 (Evan Lucas) [#8824](https://github.com/nodejs/node/pull/8824)
* \[[`f18f3b61e3`](https://github.com/nodejs/node/commit/f18f3b61e3)] - **util**: use template strings (Alejandro Oviedo Garcia) [#9120](https://github.com/nodejs/node/pull/9120)
* \[[`1dfb5b5a09`](https://github.com/nodejs/node/commit/1dfb5b5a09)] - **v8**: update make-v8.sh to use git (Jaideep Bajwa) [#9393](https://github.com/nodejs/node/pull/9393)
* \[[`bdb6cf92c7`](https://github.com/nodejs/node/commit/bdb6cf92c7)] - **win,msi**: mark INSTALLDIR property as secure (João Reis) [#8795](https://github.com/nodejs/node/pull/8795)
* \[[`9a02414a29`](https://github.com/nodejs/node/commit/9a02414a29)] - **zlib**: fix raw inflate with custom dictionary (Tarjei Husøy)

<a id="4.6.2"></a>

## 2016-11-08, Version 4.6.2 'Argon' (LTS), @thealphanerd

This LTS release comes with 219 commits. This includes 80 commits that are docs related, 58 commits that are test related, 20 commits that are build / tool related, and 9 commits that are updates to dependencies.

### Notable Changes

* **build**: It is now possible to build the documentation from the release tarball (Anna Henningsen) [#8413](https://github.com/nodejs/node/pull/8413)
* **buffer**: Buffer.alloc() will no longer incorrectly return a zero filled buffer when an encoding is passed (Teddy Katz) [#9238](https://github.com/nodejs/node/pull/9238)
* **deps**: upgrade npm in LTS to 2.15.11 (Kat Marchán) [#8928](https://github.com/nodejs/node/pull/8928)
* **repl**: Enable tab completion for global properties (Lance Ball) [#7369](https://github.com/nodejs/node/pull/7369)
* **url**: `url.format()` will now encode all `#` in `search` (Ilkka Myller) [#8072](https://github.com/nodejs/node/pull/8072)

### Commits

* \[[`06a1c9bf80`](https://github.com/nodejs/node/commit/06a1c9bf80)] - **assert**: remove code that is never reached (Rich Trott) [#8132](https://github.com/nodejs/node/pull/8132)
* \[[`861e584d46`](https://github.com/nodejs/node/commit/861e584d46)] - **async\_wrap**: add a missing case to test-async-wrap-throw-no-init (yorkie) [#8198](https://github.com/nodejs/node/pull/8198)
* \[[`a3d08025fa`](https://github.com/nodejs/node/commit/a3d08025fa)] - **benchmark**: add benches for fs.stat & fs.statSync (Anna Henningsen) [#8338](https://github.com/nodejs/node/pull/8338)
* \[[`408a585261`](https://github.com/nodejs/node/commit/408a585261)] - **buffer**: fix `fill` with encoding in Buffer.alloc() (Teddy Katz) [#9238](https://github.com/nodejs/node/pull/9238)
* \[[`17c4187949`](https://github.com/nodejs/node/commit/17c4187949)] - **buffer**: optimize hex\_decode (Christopher Jeffrey) [#7602](https://github.com/nodejs/node/pull/7602)
* \[[`50cfea0081`](https://github.com/nodejs/node/commit/50cfea0081)] - **build**: run `npm install` for doc builds in tarball (Anna Henningsen) [#8413](https://github.com/nodejs/node/pull/8413)
* \[[`c4be179064`](https://github.com/nodejs/node/commit/c4be179064)] - **build**: add missing files to zip and 7z packages (Richard Lau) [#8069](https://github.com/nodejs/node/pull/8069)
* \[[`41e27f6a6a`](https://github.com/nodejs/node/commit/41e27f6a6a)] - **build**: don't link against liblog on host system (Ben Noordhuis) [#7762](https://github.com/nodejs/node/pull/7762)
* \[[`7766997f7e`](https://github.com/nodejs/node/commit/7766997f7e)] - **build**: add conflict marker check during CI lint (Brian White) [#7625](https://github.com/nodejs/node/pull/7625)
* \[[`2a66ddbcbb`](https://github.com/nodejs/node/commit/2a66ddbcbb)] - **build**: re-add --ninja option to configure (Ehsan Akhgari) [#6780](https://github.com/nodejs/node/pull/6780)
* \[[`950cc1df83`](https://github.com/nodejs/node/commit/950cc1df83)] - **build**: adding config.gypi dep to addons/.buildstamp (Daniel Bevenius) [#7893](https://github.com/nodejs/node/pull/7893)
* \[[`e64063c344`](https://github.com/nodejs/node/commit/e64063c344)] - **build**: don't require processing docs for nightlies (Johan Bergström) [#8325](https://github.com/nodejs/node/pull/8325)
* \[[`00ea7388cb`](https://github.com/nodejs/node/commit/00ea7388cb)] - **build**: fix dependencies on AIX (Michael Dawson) [#8285](https://github.com/nodejs/node/pull/8285)
* \[[`8dfab3ad68`](https://github.com/nodejs/node/commit/8dfab3ad68)] - **build**: fix dependencies on AIX (Michael Dawson) [#8272](https://github.com/nodejs/node/pull/8272)
* \[[`1b5f35f1be`](https://github.com/nodejs/node/commit/1b5f35f1be)] - **build**: turn on thin static archives (Ben Noordhuis) [#7957](https://github.com/nodejs/node/pull/7957)
* \[[`c41efe4d68`](https://github.com/nodejs/node/commit/c41efe4d68)] - **build**: add node\_module\_version to config.gypi (Marcin Cieślak) [#8171](https://github.com/nodejs/node/pull/8171)
* \[[`f556b43e3e`](https://github.com/nodejs/node/commit/f556b43e3e)] - **build**: add --enable-d8 configure option (Ben Noordhuis) [#7538](https://github.com/nodejs/node/pull/7538)
* \[[`612dfeb647`](https://github.com/nodejs/node/commit/612dfeb647)] - **child\_process**: Check stderr before accessing it (Robert Chiras) [#6877](https://github.com/nodejs/node/pull/6877)
* \[[`5ed5142158`](https://github.com/nodejs/node/commit/5ed5142158)] - **child\_process**: workaround fd passing issue on OS X (Santiago Gimeno) [#7572](https://github.com/nodejs/node/pull/7572)
* \[[`227db0ab21`](https://github.com/nodejs/node/commit/227db0ab21)] - **cluster**: remove bind() and self (cjihrig) [#7710](https://github.com/nodejs/node/pull/7710)
* \[[`3003131e9a`](https://github.com/nodejs/node/commit/3003131e9a)] - **configure**: reword help for --without-npm (BlackYoup) [#7471](https://github.com/nodejs/node/pull/7471)
* \[[`2b933339d0`](https://github.com/nodejs/node/commit/2b933339d0)] - **debugger**: use arrow function for lexical `this` (Guy Fraser) [#7415](https://github.com/nodejs/node/pull/7415)
* \[[`52cba4147d`](https://github.com/nodejs/node/commit/52cba4147d)] - **deps**: backport 2bcbe2f from V8 upstream (ofrobots) [#7814](https://github.com/nodejs/node/pull/7814)
* \[[`2b01bc8e55`](https://github.com/nodejs/node/commit/2b01bc8e55)] - **deps**: backport a76d133 from v8 upstream (Matt Loring) [#7689](https://github.com/nodejs/node/pull/7689)
* \[[`e1f12fb358`](https://github.com/nodejs/node/commit/e1f12fb358)] - **deps**: cherry-pick b93c80a from v8 upstream (Matt Loring) [#7689](https://github.com/nodejs/node/pull/7689)
* \[[`2d07fd71ee`](https://github.com/nodejs/node/commit/2d07fd71ee)] - **deps**: backport e093a04, 09db540 from upstream V8 (Ali Ijaz Sheikh) [#7689](https://github.com/nodejs/node/pull/7689)
* \[[`4369055878`](https://github.com/nodejs/node/commit/4369055878)] - **deps**: cherry-pick 1f53e42 from v8 upstream (Ben Noordhuis) [#7612](https://github.com/nodejs/node/pull/7612)
* \[[`05d40d9573`](https://github.com/nodejs/node/commit/05d40d9573)] - **deps**: upgrade npm in LTS to 2.15.11 (Kat Marchán) [#8928](https://github.com/nodejs/node/pull/8928)
* \[[`36b3ff0cfc`](https://github.com/nodejs/node/commit/36b3ff0cfc)] - **deps**: float gyp patch for long filenames (Anna Henningsen) [#7963](https://github.com/nodejs/node/pull/7963)
* \[[`9ddc615d0e`](https://github.com/nodejs/node/commit/9ddc615d0e)] - **deps**: no /safeseh for ml64.exe (Fedor Indutny) [#7759](https://github.com/nodejs/node/pull/7759)
* \[[`ea36c61eda`](https://github.com/nodejs/node/commit/ea36c61eda)] - **deps**: `MASM.UseSafeExceptionHandlers` for OpenSSL (Fedor Indutny) [#7427](https://github.com/nodejs/node/pull/7427)
* \[[`0b87b1a095`](https://github.com/nodejs/node/commit/0b87b1a095)] - **dns**: tweak regex for IPv6 addresses (Luigi Pinca) [#8665](https://github.com/nodejs/node/pull/8665)
* \[[`0e2aba96bc`](https://github.com/nodejs/node/commit/0e2aba96bc)] - **doc**: make sure links are correctly passed to marked (Timothy Gu) [#8494](https://github.com/nodejs/node/pull/8494)
* \[[`3a43b0d981`](https://github.com/nodejs/node/commit/3a43b0d981)] - **doc**: correct metadata of `Buffer.from` (Anna Henningsen) [#9167](https://github.com/nodejs/node/pull/9167)
* \[[`880ca99847`](https://github.com/nodejs/node/commit/880ca99847)] - **doc**: fix broken link in dgram doc (Brian White) [#8365](https://github.com/nodejs/node/pull/8365)
* \[[`65ca2af471`](https://github.com/nodejs/node/commit/65ca2af471)] - **doc**: add missing semicolon (Ravindra barthwal) [#7915](https://github.com/nodejs/node/pull/7915)
* \[[`da3b938be3`](https://github.com/nodejs/node/commit/da3b938be3)] - **doc**: add `added:` information for globals (Luigi Pinca) [#8901](https://github.com/nodejs/node/pull/8901)
* \[[`b4ba4af525`](https://github.com/nodejs/node/commit/b4ba4af525)] - **doc**: add CTC meeting minutes 2016-09-07 (Josh Gavant) [#8499](https://github.com/nodejs/node/pull/8499)
* \[[`4b49b0e30c`](https://github.com/nodejs/node/commit/4b49b0e30c)] - **doc**: add CTC meeting minutes 2016-09-14 (Josh Gavant) [#8726](https://github.com/nodejs/node/pull/8726)
* \[[`88b0067229`](https://github.com/nodejs/node/commit/88b0067229)] - **doc**: add CTC meeting minutes 2016-09-21 (Josh Gavant) [#8727](https://github.com/nodejs/node/pull/8727)
* \[[`f7c4e9489f`](https://github.com/nodejs/node/commit/f7c4e9489f)] - **doc**: update npm LICENSE using license-builder.sh (Kat Marchán) [#8928](https://github.com/nodejs/node/pull/8928)
* \[[`6effc4aadc`](https://github.com/nodejs/node/commit/6effc4aadc)] - **doc**: add `added:` information for crypto (Luigi Pinca) [#8281](https://github.com/nodejs/node/pull/8281)
* \[[`d750fc6336`](https://github.com/nodejs/node/commit/d750fc6336)] - **doc**: add `added:` information for dgram (Luigi Pinca) [#8196](https://github.com/nodejs/node/pull/8196)
* \[[`b92e3fc72e`](https://github.com/nodejs/node/commit/b92e3fc72e)] - **doc**: add `added:` information for util (Luigi Pinca) [#8206](https://github.com/nodejs/node/pull/8206)
* \[[`578bf511f9`](https://github.com/nodejs/node/commit/578bf511f9)] - **doc**: add `added:` information for events (Luigi Pinca) [#7822](https://github.com/nodejs/node/pull/7822)
* \[[`6ef58e7211`](https://github.com/nodejs/node/commit/6ef58e7211)] - **doc**: add gibfahn to collaborators (Gibson Fahnestock) [#8533](https://github.com/nodejs/node/pull/8533)
* \[[`5ff1fc7d86`](https://github.com/nodejs/node/commit/5ff1fc7d86)] - **doc**: add imyller to collaborators (Ilkka Myller) [#8530](https://github.com/nodejs/node/pull/8530)
* \[[`88bb65dd74`](https://github.com/nodejs/node/commit/88bb65dd74)] - **doc**: add not-an-aardvark to collaborators (not-an-aardvark) [#8525](https://github.com/nodejs/node/pull/8525)
* \[[`5bec1eb0d4`](https://github.com/nodejs/node/commit/5bec1eb0d4)] - **doc**: update onboarding PR landing info (Rich Trott) [#8479](https://github.com/nodejs/node/pull/8479)
* \[[`ecd2b52982`](https://github.com/nodejs/node/commit/ecd2b52982)] - **doc**: encourage 2FA before onboarding (Rich Trott) [#8776](https://github.com/nodejs/node/pull/8776)
* \[[`2adbd53837`](https://github.com/nodejs/node/commit/2adbd53837)] - **doc**: add commit formats for release blog posts (fen) [#8631](https://github.com/nodejs/node/pull/8631)
* \[[`764502bb37`](https://github.com/nodejs/node/commit/764502bb37)] - **doc**: add CTC meeting minutes 2016-08-24 (Josh Gavant) [#8423](https://github.com/nodejs/node/pull/8423)
* \[[`3037a9da08`](https://github.com/nodejs/node/commit/3037a9da08)] - **doc**: add eugeneo to collaborators (Eugene Ostroukhov) [#8696](https://github.com/nodejs/node/pull/8696)
* \[[`0fd1d8dfd7`](https://github.com/nodejs/node/commit/0fd1d8dfd7)] - **doc**: add ak239 to collaborators (Aleksey Kozyatinskiy) [#8676](https://github.com/nodejs/node/pull/8676)
* \[[`64c4bb30fe`](https://github.com/nodejs/node/commit/64c4bb30fe)] - **doc**: add link to help repo in README (Rich Trott) [#8570](https://github.com/nodejs/node/pull/8570)
* \[[`d123fc1307`](https://github.com/nodejs/node/commit/d123fc1307)] - **doc**: update exercise portion of onboarding doc (Rich Trott) [#8559](https://github.com/nodejs/node/pull/8559)
* \[[`c6b622f6b3`](https://github.com/nodejs/node/commit/c6b622f6b3)] - **doc**: add CTC meeting minutes 2016-08-31 (Josh Gavant) [#8424](https://github.com/nodejs/node/pull/8424)
* \[[`055d39c724`](https://github.com/nodejs/node/commit/055d39c724)] - **doc**: add CI help/support info to onboarding doc (Rich Trott) [#8407](https://github.com/nodejs/node/pull/8407)
* \[[`a7e6fc08d8`](https://github.com/nodejs/node/commit/a7e6fc08d8)] - **doc**: add 2016-08-17 CTC meeting minutes (Josh Gavant) [#8245](https://github.com/nodejs/node/pull/8245)
* \[[`ca63c127c7`](https://github.com/nodejs/node/commit/ca63c127c7)] - **doc**: add 2016-08-10 CTC meeting minutes (Josh Gavant) [#8229](https://github.com/nodejs/node/pull/8229)
* \[[`3f2e3dfb32`](https://github.com/nodejs/node/commit/3f2e3dfb32)] - **doc**: update CI content in onboarding doc (Rich Trott) [#8374](https://github.com/nodejs/node/pull/8374)
* \[[`9e1325c42e`](https://github.com/nodejs/node/commit/9e1325c42e)] - **doc**: update authors list (James M Snell) [#8346](https://github.com/nodejs/node/pull/8346)
* \[[`c529bf5521`](https://github.com/nodejs/node/commit/c529bf5521)] - **doc**: add return type of clientRequest.setTimeout (Mike Ralphson) [#8356](https://github.com/nodejs/node/pull/8356)
* \[[`c094b2a51c`](https://github.com/nodejs/node/commit/c094b2a51c)] - **doc**: update targos email in readme per request (James M Snell) [#8389](https://github.com/nodejs/node/pull/8389)
* \[[`5c417ee25b`](https://github.com/nodejs/node/commit/5c417ee25b)] - **doc**: update landing pr info in onboarding doc (Rich Trott) [#8344](https://github.com/nodejs/node/pull/8344)
* \[[`763fa85ccf`](https://github.com/nodejs/node/commit/763fa85ccf)] - **doc**: bad/better examples for fs.access() and fs.exists() (Dan Fabulich) [#7832](https://github.com/nodejs/node/pull/7832)
* \[[`0c933e5bab`](https://github.com/nodejs/node/commit/0c933e5bab)] - **doc**: adding danbev to collaborators (Daniel Bevenius) [#8359](https://github.com/nodejs/node/pull/8359)
* \[[`e069dc45b0`](https://github.com/nodejs/node/commit/e069dc45b0)] - **doc**: add lpinca to collaborators (Luigi Pinca) [#8331](https://github.com/nodejs/node/pull/8331)
* \[[`e5f4367da5`](https://github.com/nodejs/node/commit/e5f4367da5)] - **doc**: readline write() is processed as input (James M Snell) [#8295](https://github.com/nodejs/node/pull/8295)
* \[[`b3617fcc7d`](https://github.com/nodejs/node/commit/b3617fcc7d)] - **doc**: add `added:` information for modules (Luigi Pinca) [#8250](https://github.com/nodejs/node/pull/8250)
* \[[`0b605636c5`](https://github.com/nodejs/node/commit/0b605636c5)] - **doc**: add Myles Borins to the CTC (Rod Vagg) [#8260](https://github.com/nodejs/node/pull/8260)
* \[[`a8a8f0a6f1`](https://github.com/nodejs/node/commit/a8a8f0a6f1)] - **doc**: add `added:` information for cluster (Anna Henningsen) [#7640](https://github.com/nodejs/node/pull/7640)
* \[[`2a2971b26e`](https://github.com/nodejs/node/commit/2a2971b26e)] - **doc**: use blockquotes for Stability: markers (Anna Henningsen) [#7757](https://github.com/nodejs/node/pull/7757)
* \[[`3a3fde69c7`](https://github.com/nodejs/node/commit/3a3fde69c7)] - **doc**: fix variable scoping bug in server example code (lazlojuly) [#8124](https://github.com/nodejs/node/pull/8124)
* \[[`f1e14e4227`](https://github.com/nodejs/node/commit/f1e14e4227)] - **doc**: fix cluster message event docs (Zach Bjornson) [#8017](https://github.com/nodejs/node/pull/8017)
* \[[`9b29cfc3a6`](https://github.com/nodejs/node/commit/9b29cfc3a6)] - **doc**: Clean up roff source in manpage (Alhadis) [#7819](https://github.com/nodejs/node/pull/7819)
* \[[`364af49e0f`](https://github.com/nodejs/node/commit/364af49e0f)] - **doc**: add CTC meeting minutes 2016-06-22 (Josh Gavant) [#7390](https://github.com/nodejs/node/pull/7390)
* \[[`9892a5ddc3`](https://github.com/nodejs/node/commit/9892a5ddc3)] - **doc**: remove extra spaces and concats in examples (Joe Esposito) [#7885](https://github.com/nodejs/node/pull/7885)
* \[[`3ad74089f5`](https://github.com/nodejs/node/commit/3ad74089f5)] - **doc**: correct sample output of buf.compare (Hargobind S. Khalsa) [#7777](https://github.com/nodejs/node/pull/7777)
* \[[`26e695c46c`](https://github.com/nodejs/node/commit/26e695c46c)] - **doc**: remove "feature branch" jargon (Rich Trott) [#8194](https://github.com/nodejs/node/pull/8194)
* \[[`d676467208`](https://github.com/nodejs/node/commit/d676467208)] - **doc**: remove outdated LTS info from ROADMAP.md (Rich Trott) [#8161](https://github.com/nodejs/node/pull/8161)
* \[[`b3545e148d`](https://github.com/nodejs/node/commit/b3545e148d)] - **doc**: update release announce instruction to tweet (Tracy Hinds) [#8126](https://github.com/nodejs/node/pull/8126)
* \[[`2032bba65f`](https://github.com/nodejs/node/commit/2032bba65f)] - **doc**: add @joshgav to collaborators (Josh Gavant) [#8146](https://github.com/nodejs/node/pull/8146)
* \[[`727c24f3a2`](https://github.com/nodejs/node/commit/727c24f3a2)] - **doc**: update Reviewing section of onboarding doc (Rich Trott)
* \[[`04515b891a`](https://github.com/nodejs/node/commit/04515b891a)] - **doc**: move orangemocha to collaborators list (Rich Trott) [#8062](https://github.com/nodejs/node/pull/8062)
* \[[`d3344aa216`](https://github.com/nodejs/node/commit/d3344aa216)] - **doc**: Add fhinkel to collaborators (Franziska Hinkelmann) [#8052](https://github.com/nodejs/node/pull/8052)
* \[[`532bbde4bf`](https://github.com/nodejs/node/commit/532bbde4bf)] - **doc**: add CTC meeting minutes 2016-08-03 (Josh Gavant) [#7980](https://github.com/nodejs/node/pull/7980)
* \[[`98fe74fbc8`](https://github.com/nodejs/node/commit/98fe74fbc8)] - **doc**: fix a markdown error in CTC meeting minutes (Сковорода Никита Андреевич) [#7729](https://github.com/nodejs/node/pull/7729)
* \[[`e74daadeb6`](https://github.com/nodejs/node/commit/e74daadeb6)] - **doc**: clarify collaborators & ctc members relationships (yorkie) [#7996](https://github.com/nodejs/node/pull/7996)
* \[[`6bfdc92860`](https://github.com/nodejs/node/commit/6bfdc92860)] - **doc**: clarify "Reviewed-By" iff "LGTM" (Bryan English) [#7183](https://github.com/nodejs/node/pull/7183)
* \[[`94a82cd0a7`](https://github.com/nodejs/node/commit/94a82cd0a7)] - **doc**: add CTC meeting minutes 2016-07-13 (Josh Gavant) [#7968](https://github.com/nodejs/node/pull/7968)
* \[[`012ccf010e`](https://github.com/nodejs/node/commit/012ccf010e)] - **doc**: add CTC meeting minutes 2016-07-20 (Josh Gavant) [#7970](https://github.com/nodejs/node/pull/7970)
* \[[`08111e84b1`](https://github.com/nodejs/node/commit/08111e84b1)] - **doc**: use consistent markdown in README (Rich Trott) [#7971](https://github.com/nodejs/node/pull/7971)
* \[[`009df788de`](https://github.com/nodejs/node/commit/009df788de)] - **doc**: use `git-secure-tag` for release tags (Fedor Indutny) [#7603](https://github.com/nodejs/node/pull/7603)
* \[[`abefdca5ae`](https://github.com/nodejs/node/commit/abefdca5ae)] - **doc**: piscisaureus has stepped-down from the CTC (James M Snell) [#7969](https://github.com/nodejs/node/pull/7969)
* \[[`9700660d2b`](https://github.com/nodejs/node/commit/9700660d2b)] - **doc**: add @addaleax to the CTC (Anna Henningsen) [#7966](https://github.com/nodejs/node/pull/7966)
* \[[`f255180853`](https://github.com/nodejs/node/commit/f255180853)] - **doc**: add CTC meeting minutes 2016-07-06 (Josh Gavant) [#7570](https://github.com/nodejs/node/pull/7570)
* \[[`b60473fac7`](https://github.com/nodejs/node/commit/b60473fac7)] - **doc**: add CTC meeting minutes 2016-06-29 (Josh Gavant) [#7571](https://github.com/nodejs/node/pull/7571)
* \[[`ac40b2a9b6`](https://github.com/nodejs/node/commit/ac40b2a9b6)] - **doc**: add CTC meeting minutes 2016-07-27 (William Kapke) [#7900](https://github.com/nodejs/node/pull/7900)
* \[[`bbbbb19658`](https://github.com/nodejs/node/commit/bbbbb19658)] - **doc**: add information about CTC quorum rules (Rich Trott) [#7813](https://github.com/nodejs/node/pull/7813)
* \[[`d759d4e0a6`](https://github.com/nodejs/node/commit/d759d4e0a6)] - **doc**: remove platform assumption from CONTRIBUTING (Bethany N Griggs) [#7783](https://github.com/nodejs/node/pull/7783)
* \[[`b01854dd9d`](https://github.com/nodejs/node/commit/b01854dd9d)] - **doc**: add princejwesley to collaborators (Prince J Wesley) [#7877](https://github.com/nodejs/node/pull/7877)
* \[[`26f5168c02`](https://github.com/nodejs/node/commit/26f5168c02)] - **doc**: clarify that the node.js irc channel is not under tsc oversight (James M Snell) [#7810](https://github.com/nodejs/node/pull/7810)
* \[[`506e367062`](https://github.com/nodejs/node/commit/506e367062)] - **doc**: update readme with andrasq as a collaborator (Andras) [#7801](https://github.com/nodejs/node/pull/7801)
* \[[`590c52a309`](https://github.com/nodejs/node/commit/590c52a309)] - **doc**: update CTC governance information (Rich Trott) [#7719](https://github.com/nodejs/node/pull/7719)
* \[[`fdff642e0b`](https://github.com/nodejs/node/commit/fdff642e0b)] - **doc**: fix util.deprecate() example (Evan Lucas) [#7674](https://github.com/nodejs/node/pull/7674)
* \[[`8fec02ffb8`](https://github.com/nodejs/node/commit/8fec02ffb8)] - **doc**: delete non-existing zlib constants (Franziska Hinkelmann) [#7520](https://github.com/nodejs/node/pull/7520)
* \[[`d6c2e383a2`](https://github.com/nodejs/node/commit/d6c2e383a2)] - **doc**: minor updates to onboarding doc (Rich Trott) [#8060](https://github.com/nodejs/node/pull/8060)
* \[[`e46d1e026e`](https://github.com/nodejs/node/commit/e46d1e026e)] - **doc**: add POST\_STATUS\_TO\_PR info to onboarding doc (Rich Trott) [#8059](https://github.com/nodejs/node/pull/8059)
* \[[`4f3107190d`](https://github.com/nodejs/node/commit/4f3107190d)] - **doc**: add `added:` info for dgram.\*Membership() (Rich Trott) [#6753](https://github.com/nodejs/node/pull/6753)
* \[[`0e52861629`](https://github.com/nodejs/node/commit/0e52861629)] - **doc**: grammar fixes to event loop guide (Ryan Lewis) [#7479](https://github.com/nodejs/node/pull/7479)
* \[[`29139bff65`](https://github.com/nodejs/node/commit/29139bff65)] - **doc**: improve server.listen() random port (Phillip Johnsen) [#8025](https://github.com/nodejs/node/pull/8025)
* \[[`b680eb99ad`](https://github.com/nodejs/node/commit/b680eb99ad)] - **doctool**: improve the title of pages in doc (yorkie)
* \[[`3d6f107a2f`](https://github.com/nodejs/node/commit/3d6f107a2f)] - **fs**: fix handling of `uv_stat_t` fields (Anna Henningsen) [#8515](https://github.com/nodejs/node/pull/8515)
* \[[`2e29b76666`](https://github.com/nodejs/node/commit/2e29b76666)] - **intl**: Don't crash if v8BreakIterator not available (Steven R. Loomis) [#4253](https://github.com/nodejs/node/pull/4253)
* \[[`f6e332da2d`](https://github.com/nodejs/node/commit/f6e332da2d)] - **lib**: implement consistent brace style (Rich Trott) [#8348](https://github.com/nodejs/node/pull/8348)
* \[[`9d9bcd7c55`](https://github.com/nodejs/node/commit/9d9bcd7c55)] - **meta**: clarify process for breaking changes (Rich Trott) [#7955](https://github.com/nodejs/node/pull/7955)
* \[[`6d49f22e35`](https://github.com/nodejs/node/commit/6d49f22e35)] - **meta**: include a minimal CTC removal policy (Rich Trott) [#7720](https://github.com/nodejs/node/pull/7720)
* \[[`7faf6dc0da`](https://github.com/nodejs/node/commit/7faf6dc0da)] - **meta**: provide example activities (Rich Trott) [#7744](https://github.com/nodejs/node/pull/7744)
* \[[`fe48415c60`](https://github.com/nodejs/node/commit/fe48415c60)] - **net**: add length check when normalizing args (Brian White) [#8112](https://github.com/nodejs/node/pull/8112)
* \[[`3906206ecc`](https://github.com/nodejs/node/commit/3906206ecc)] - **net**: remove unnecessary variables (Brian White) [#8112](https://github.com/nodejs/node/pull/8112)
* \[[`9f1b790f79`](https://github.com/nodejs/node/commit/9f1b790f79)] - **net**: make holding the buffer in memory more robust (Anna Henningsen) [#8252](https://github.com/nodejs/node/pull/8252)
* \[[`b630be2309`](https://github.com/nodejs/node/commit/b630be2309)] - **net**: export isIPv4, isIPv6 directly from cares (Sakthipriyan Vairamani) [#7481](https://github.com/nodejs/node/pull/7481)
* \[[`c235708bef`](https://github.com/nodejs/node/commit/c235708bef)] - **readline**: keypress trigger for escape character (Prince J Wesley) [#7382](https://github.com/nodejs/node/pull/7382)
* \[[`8198dbc5a4`](https://github.com/nodejs/node/commit/8198dbc5a4)] - **repl**: Enable tab completion for global properties (Lance Ball) [#7369](https://github.com/nodejs/node/pull/7369)
* \[[`12300626d7`](https://github.com/nodejs/node/commit/12300626d7)] - **src**: no abort from getter if object isn't wrapped (Trevor Norris) [#6184](https://github.com/nodejs/node/pull/6184)
* \[[`166a9b85d9`](https://github.com/nodejs/node/commit/166a9b85d9)] - **src**: always clear wrap before persistent Reset() (Trevor Norris) [#6184](https://github.com/nodejs/node/pull/6184)
* \[[`b3149cee8c`](https://github.com/nodejs/node/commit/b3149cee8c)] - **src**: inherit first from AsyncWrap (Trevor Norris) [#6184](https://github.com/nodejs/node/pull/6184)
* \[[`8b93fddd1b`](https://github.com/nodejs/node/commit/8b93fddd1b)] - **src**: disable stdio buffering (Ben Noordhuis) [#7610](https://github.com/nodejs/node/pull/7610)
* \[[`72be320962`](https://github.com/nodejs/node/commit/72be320962)] - **src**: suppress coverity message (cjihrig) [#7587](https://github.com/nodejs/node/pull/7587)
* \[[`6ba3ad5d34`](https://github.com/nodejs/node/commit/6ba3ad5d34)] - **src**: guard against overflow in ParseArrayIndex() (Ben Noordhuis) [#7497](https://github.com/nodejs/node/pull/7497)
* \[[`e1f961d050`](https://github.com/nodejs/node/commit/e1f961d050)] - **src**: move ParseArrayIndex() to src/node\_buffer.cc (Ben Noordhuis) [#7497](https://github.com/nodejs/node/pull/7497)
* \[[`57921ebec5`](https://github.com/nodejs/node/commit/57921ebec5)] - **src**: remove unnecessary HandleScopes (Ben Noordhuis) [#7711](https://github.com/nodejs/node/pull/7711)
* \[[`6838ad5f8e`](https://github.com/nodejs/node/commit/6838ad5f8e)] - **src**: fix handle leak in UDPWrap::Instantiate() (Ben Noordhuis) [#7711](https://github.com/nodejs/node/pull/7711)
* \[[`dadcf6b263`](https://github.com/nodejs/node/commit/dadcf6b263)] - **src**: fix handle leak in BuildStatsObject() (Ben Noordhuis) [#7711](https://github.com/nodejs/node/pull/7711)
* \[[`7aa268922a`](https://github.com/nodejs/node/commit/7aa268922a)] - **src**: fix handle leak in Buffer::New() (Ben Noordhuis) [#7711](https://github.com/nodejs/node/pull/7711)
* \[[`606deecd16`](https://github.com/nodejs/node/commit/606deecd16)] - **src**: don't include a null character in the WriteConsoleW call (Nikolai Vavilov) [#7764](https://github.com/nodejs/node/pull/7764)
* \[[`a5b6c2cdd7`](https://github.com/nodejs/node/commit/a5b6c2cdd7)] - **src**: use RAII for mutexes and condition variables (Ben Noordhuis) [#7334](https://github.com/nodejs/node/pull/7334)
* \[[`19d6f06058`](https://github.com/nodejs/node/commit/19d6f06058)] - **stream\_base**: always use Base template class (Trevor Norris) [#6184](https://github.com/nodejs/node/pull/6184)
* \[[`d5f03db819`](https://github.com/nodejs/node/commit/d5f03db819)] - **test**: fix test-cluster-dgram-1 flakiness (Santiago Gimeno)
* \[[`a83bbaa5a3`](https://github.com/nodejs/node/commit/a83bbaa5a3)] - **test**: refactor test-tick-processor (Rich Trott) [#8180](https://github.com/nodejs/node/pull/8180)
* \[[`1c81c078c2`](https://github.com/nodejs/node/commit/1c81c078c2)] - **test**: add assert.notDeepStrictEqual() tests (Rich Trott) [#8177](https://github.com/nodejs/node/pull/8177)
* \[[`57c98f18a9`](https://github.com/nodejs/node/commit/57c98f18a9)] - **test**: favor `===` over `==` in crypto tests (Rich Trott) [#8176](https://github.com/nodejs/node/pull/8176)
* \[[`11f761ab1a`](https://github.com/nodejs/node/commit/11f761ab1a)] - **test**: refactor pummel/test-dtrace-jsstack (Rich Trott) [#8175](https://github.com/nodejs/node/pull/8175)
* \[[`2997b79fcc`](https://github.com/nodejs/node/commit/2997b79fcc)] - **test**: favor strict equality in test-exec (Rich Trott) [#8173](https://github.com/nodejs/node/pull/8173)
* \[[`558f7d999c`](https://github.com/nodejs/node/commit/558f7d999c)] - **test**: add assert.notDeepEqual() tests (Rich Trott) [#8156](https://github.com/nodejs/node/pull/8156)
* \[[`49c488625d`](https://github.com/nodejs/node/commit/49c488625d)] - **test**: add missing assert.deepEqual() test case (Rich Trott) [#8152](https://github.com/nodejs/node/pull/8152)
* \[[`eec078cd66`](https://github.com/nodejs/node/commit/eec078cd66)] - **test**: favor strict equality in http tests (Rich Trott) [#8151](https://github.com/nodejs/node/pull/8151)
* \[[`e3669f8c21`](https://github.com/nodejs/node/commit/e3669f8c21)] - **test**: favor strict equality in pummel net tests (Rich Trott) [#8135](https://github.com/nodejs/node/pull/8135)
* \[[`ac83d199fb`](https://github.com/nodejs/node/commit/ac83d199fb)] - **test**: confirm that assert truncates long values (Rich Trott) [#8134](https://github.com/nodejs/node/pull/8134)
* \[[`9c826beef7`](https://github.com/nodejs/node/commit/9c826beef7)] - **test**: favor `===` over `==` in test-timers.js (Rich Trott) [#8131](https://github.com/nodejs/node/pull/8131)
* \[[`af02d2a642`](https://github.com/nodejs/node/commit/af02d2a642)] - **test**: favor strict equality check (Rich Trott) [#8130](https://github.com/nodejs/node/pull/8130)
* \[[`30034048b0`](https://github.com/nodejs/node/commit/30034048b0)] - **test**: fix assertion in test-watch-file.js (Rich Trott) [#8129](https://github.com/nodejs/node/pull/8129)
* \[[`b063dc90b1`](https://github.com/nodejs/node/commit/b063dc90b1)] - **test**: use strict equality in regression test (Rich Trott) [#8098](https://github.com/nodejs/node/pull/8098)
* \[[`dc7bc2e679`](https://github.com/nodejs/node/commit/dc7bc2e679)] - **test**: add test for debug usage message (Rich Trott) [#8061](https://github.com/nodejs/node/pull/8061)
* \[[`ce2cfbdc3a`](https://github.com/nodejs/node/commit/ce2cfbdc3a)] - **test**: console constructor missing new keyword (Rich Trott) [#8003](https://github.com/nodejs/node/pull/8003)
* \[[`69f4edd368`](https://github.com/nodejs/node/commit/69f4edd368)] - **test**: speed up test-net-reconnect-error (Rich Trott) [#7886](https://github.com/nodejs/node/pull/7886)
* \[[`50acf72d80`](https://github.com/nodejs/node/commit/50acf72d80)] - **test**: increase RAM requirement for intensive tests (Rich Trott) [#7772](https://github.com/nodejs/node/pull/7772)
* \[[`924ea0a2bd`](https://github.com/nodejs/node/commit/924ea0a2bd)] - **test**: fix flaky test-http-server-consumed-timeout (Rich Trott) [#7717](https://github.com/nodejs/node/pull/7717)
* \[[`97a3d89c80`](https://github.com/nodejs/node/commit/97a3d89c80)] - **test**: improve coverage of the util module (Michaël Zasso) [#8633](https://github.com/nodejs/node/pull/8633)
* \[[`52bb37734b`](https://github.com/nodejs/node/commit/52bb37734b)] - **test**: mark test-child-process-fork-dgram as flaky (Michael Dawson) [#8274](https://github.com/nodejs/node/pull/8274)
* \[[`97c68ddaad`](https://github.com/nodejs/node/commit/97c68ddaad)] - **test**: improve error message in test-tick-processor (Rich Trott) [#7693](https://github.com/nodejs/node/pull/7693)
* \[[`cd9e8e0361`](https://github.com/nodejs/node/commit/cd9e8e0361)] - **test**: fix old tty tests (Jeremiah Senkpiel) [#7613](https://github.com/nodejs/node/pull/7613)
* \[[`22990d8851`](https://github.com/nodejs/node/commit/22990d8851)] - **test**: move parallel/test-tty-\* to pseudo-tty/ (Jeremiah Senkpiel) [#7613](https://github.com/nodejs/node/pull/7613)
* \[[`afee32fed5`](https://github.com/nodejs/node/commit/afee32fed5)] - **test**: fix `fs-watch-recursive` flakiness on OS X (Santiago Gimeno) [#4629](https://github.com/nodejs/node/pull/4629)
* \[[`c543f4a879`](https://github.com/nodejs/node/commit/c543f4a879)] - **test**: stream writable ended state (Italo A. Casas) [#8778](https://github.com/nodejs/node/pull/8778)
* \[[`f46a04cc6d`](https://github.com/nodejs/node/commit/f46a04cc6d)] - **test**: add tests for add/remove header after sent (Niklas Ingholt) [#8682](https://github.com/nodejs/node/pull/8682)
* \[[`e79351c3ac`](https://github.com/nodejs/node/commit/e79351c3ac)] - **test**: improve test-https-agent.js (Dan.Williams) [#8517](https://github.com/nodejs/node/pull/8517)
* \[[`9ffb2f3c0d`](https://github.com/nodejs/node/commit/9ffb2f3c0d)] - **test**: add coverage for client.\_addHandle() (Rich Trott) [#8518](https://github.com/nodejs/node/pull/8518)
* \[[`8da2dcb70a`](https://github.com/nodejs/node/commit/8da2dcb70a)] - **test**: refector parallel/test-http.js (Junshu Okamoto) [#8471](https://github.com/nodejs/node/pull/8471)
* \[[`69404ec473`](https://github.com/nodejs/node/commit/69404ec473)] - **test**: fix flaky test-force-repl (Rich Trott) [#8484](https://github.com/nodejs/node/pull/8484)
* \[[`5a07bb62ea`](https://github.com/nodejs/node/commit/5a07bb62ea)] - **test**: swapped == and equal to === and strictEqual (Christopher Dunavan) [#8472](https://github.com/nodejs/node/pull/8472)
* \[[`ad1230e731`](https://github.com/nodejs/node/commit/ad1230e731)] - **test**: skip pseudo-tty/no\_dropped\_stdio test (Michael Dawson) [#8470](https://github.com/nodejs/node/pull/8470)
* \[[`6d03170751`](https://github.com/nodejs/node/commit/6d03170751)] - **test**: clean up net server try ports test (Thomas Hunter II) [#8458](https://github.com/nodejs/node/pull/8458)
* \[[`775c84ec38`](https://github.com/nodejs/node/commit/775c84ec38)] - **test**: add test-debug-protocol-execute (Rich Trott) [#8454](https://github.com/nodejs/node/pull/8454)
* \[[`0d1082426a`](https://github.com/nodejs/node/commit/0d1082426a)] - **test**: mark pseudo-tty/no\_dropped\_stdio as flaky (Michael Dawson) [#8385](https://github.com/nodejs/node/pull/8385)
* \[[`c034c861bb`](https://github.com/nodejs/node/commit/c034c861bb)] - **test**: test non-buffer/string with zlib (Rich Trott) [#8350](https://github.com/nodejs/node/pull/8350)
* \[[`bb8690433c`](https://github.com/nodejs/node/commit/bb8690433c)] - **test**: fix ::1 error in test-dns-ipv6 (Gibson Fahnestock) [#8254](https://github.com/nodejs/node/pull/8254)
* \[[`2f458ea663`](https://github.com/nodejs/node/commit/2f458ea663)] - **test**: add test for zlib.create\*Raw() (Rich Trott) [#8306](https://github.com/nodejs/node/pull/8306)
* \[[`a368ea673c`](https://github.com/nodejs/node/commit/a368ea673c)] - **test**: refactor test-debug-signal-cluster (Rich Trott) [#8289](https://github.com/nodejs/node/pull/8289)
* \[[`a48469f098`](https://github.com/nodejs/node/commit/a48469f098)] - **test**: add check in test-signal-handler (Rich Trott) [#8248](https://github.com/nodejs/node/pull/8248)
* \[[`cadb2612c6`](https://github.com/nodejs/node/commit/cadb2612c6)] - **test**: add test for attempted multiple IPC channels (cjihrig) [#8159](https://github.com/nodejs/node/pull/8159)
* \[[`21c1b8467e`](https://github.com/nodejs/node/commit/21c1b8467e)] - **test**: decrease inconsistency in the common.js (Vse Mozhet Byt) [#7758](https://github.com/nodejs/node/pull/7758)
* \[[`d40873ddcd`](https://github.com/nodejs/node/commit/d40873ddcd)] - **test**: ensure stream preprocessing order (Vse Mozhet Byt) [#7741](https://github.com/nodejs/node/pull/7741)
* \[[`0e1f098b09`](https://github.com/nodejs/node/commit/0e1f098b09)] - **test**: avoid usage of mixed IPV6 addresses (Gireesh Punathil) [#7702](https://github.com/nodejs/node/pull/7702)
* \[[`741373cb49`](https://github.com/nodejs/node/commit/741373cb49)] - **test**: clean up test-buffer-badhex (Jeremiah Senkpiel) [#7773](https://github.com/nodejs/node/pull/7773)
* \[[`58f3fa17eb`](https://github.com/nodejs/node/commit/58f3fa17eb)] - **test**: s/assert.fail/common.fail as appropriate (cjihrig) [#7735](https://github.com/nodejs/node/pull/7735)
* \[[`b0e2f9a37a`](https://github.com/nodejs/node/commit/b0e2f9a37a)] - **test**: add common.rootDir (cjihrig) [#7685](https://github.com/nodejs/node/pull/7685)
* \[[`c94f3a5784`](https://github.com/nodejs/node/commit/c94f3a5784)] - **test**: handle IPv6 localhost issues within tests (Rich Trott) [#7766](https://github.com/nodejs/node/pull/7766)
* \[[`b64828d8df`](https://github.com/nodejs/node/commit/b64828d8df)] - **test**: accept expected AIX result test-stdio-closed (Rich Trott) [#8755](https://github.com/nodejs/node/pull/8755)
* \[[`3dbcc3d2d9`](https://github.com/nodejs/node/commit/3dbcc3d2d9)] - **test**: fix flaky test-\*-connect-address-family (Rich Trott) [#7605](https://github.com/nodejs/node/pull/7605)
* \[[`733233d3ea`](https://github.com/nodejs/node/commit/733233d3ea)] - **test**: add uncaught exception test for debugger (Rich Trott) [#8087](https://github.com/nodejs/node/pull/8087)
* \[[`c9af24d2a7`](https://github.com/nodejs/node/commit/c9af24d2a7)] - **test**: add test for assert.notStrictEqual() (Rich Trott) [#8091](https://github.com/nodejs/node/pull/8091)
* \[[`337d2dd381`](https://github.com/nodejs/node/commit/337d2dd381)] - **test**: implement consistent braces (Rich Trott) [#8348](https://github.com/nodejs/node/pull/8348)
* \[[`77df523264`](https://github.com/nodejs/node/commit/77df523264)] - **test**: exclude tests for AIX (Michael Dawson) [#8076](https://github.com/nodejs/node/pull/8076)
* \[[`50ae37e350`](https://github.com/nodejs/node/commit/50ae37e350)] - **test**: add --repeat option to tools/test.py (Michael Dawson) [#6700](https://github.com/nodejs/node/pull/6700)
* \[[`ea72e9f143`](https://github.com/nodejs/node/commit/ea72e9f143)] - **test,doc**: clarify `buf.indexOf(num)` input range (Anna Henningsen) [#7611](https://github.com/nodejs/node/pull/7611)
* \[[`c841b5a6b9`](https://github.com/nodejs/node/commit/c841b5a6b9)] - **tls**: copy the Buffer object before using (Sakthipriyan Vairamani) [#8055](https://github.com/nodejs/node/pull/8055)
* \[[`6076293d6c`](https://github.com/nodejs/node/commit/6076293d6c)] - **tls\_wrap**: do not abort on new TLSWrap() (Trevor Norris) [#6184](https://github.com/nodejs/node/pull/6184)
* \[[`6e5906c7f1`](https://github.com/nodejs/node/commit/6e5906c7f1)] - **tools**: use long format for gpg fingerprint (Myles Borins) [#9258](https://github.com/nodejs/node/pull/9258)
* \[[`7409c332b8`](https://github.com/nodejs/node/commit/7409c332b8)] - **tools**: check tag is on github before release (Rod Vagg) [#9142](https://github.com/nodejs/node/pull/9142)
* \[[`b632badda2`](https://github.com/nodejs/node/commit/b632badda2)] - **tools**: make detached SHASUM .sig file for releases (Rod Vagg) [#9071](https://github.com/nodejs/node/pull/9071)
* \[[`5867ffe27e`](https://github.com/nodejs/node/commit/5867ffe27e)] - **tools**: explicitly set digest algo for SHASUM to 256 (Rod Vagg) [#9071](https://github.com/nodejs/node/pull/9071)
* \[[`bdfa3b388b`](https://github.com/nodejs/node/commit/bdfa3b388b)] - **tools**: favor === over == in license2rtf.js (Rich Trott)
* \[[`d7e3edc744`](https://github.com/nodejs/node/commit/d7e3edc744)] - **tools**: add remark-lint configuration in .remarkrc (Сковорода Никита Андреевич) [#7729](https://github.com/nodejs/node/pull/7729)
* \[[`afbfbc04c9`](https://github.com/nodejs/node/commit/afbfbc04c9)] - **tools**: add .vscode folder to .gitignore (Josh Gavant) [#7967](https://github.com/nodejs/node/pull/7967)
* \[[`3f4a5fe61e`](https://github.com/nodejs/node/commit/3f4a5fe61e)] - **tools**: increase lint coverage (Rich Trott) [#7647](https://github.com/nodejs/node/pull/7647)
* \[[`d1a50b3ed2`](https://github.com/nodejs/node/commit/d1a50b3ed2)] - **tools**: enforce JS brace style with linting (Rich Trott) [#8348](https://github.com/nodejs/node/pull/8348)
* \[[`76b8d81f38`](https://github.com/nodejs/node/commit/76b8d81f38)] - **tools,test**: show signal code when test crashes (Santiago Gimeno) [#7859](https://github.com/nodejs/node/pull/7859)
* \[[`389a6d2cc2`](https://github.com/nodejs/node/commit/389a6d2cc2)] - **url**: fix off-by-one error in loop handling dots (Luigi Pinca) [#8420](https://github.com/nodejs/node/pull/8420)
* \[[`be9d9bd7c3`](https://github.com/nodejs/node/commit/be9d9bd7c3)] - **url**: fix inconsistent port in url.resolveObject (Ilkka Myller) [#8214](https://github.com/nodejs/node/pull/8214)
* \[[`96cfa926bd`](https://github.com/nodejs/node/commit/96cfa926bd)] - **url**: `url.format()` encodes all `#` in `search` (Ilkka Myller) [#8072](https://github.com/nodejs/node/pull/8072)
* \[[`f7796f23e3`](https://github.com/nodejs/node/commit/f7796f23e3)] - **util**: inspect boxed symbols like other primitives (Anna Henningsen) [#7641](https://github.com/nodejs/node/pull/7641)
* \[[`410e083d7c`](https://github.com/nodejs/node/commit/410e083d7c)] - **win,build**: forward release\_urlbase to configure (João Reis) [#8430](https://github.com/nodejs/node/pull/8430)
* \[[`26e73740e9`](https://github.com/nodejs/node/commit/26e73740e9)] - **win,build**: exit when addons fail to build (João Reis) [#8412](https://github.com/nodejs/node/pull/8412)
* \[[`30e751f38b`](https://github.com/nodejs/node/commit/30e751f38b)] - **win,build**: skip finding VS when not needed (João Reis) [#8412](https://github.com/nodejs/node/pull/8412)
* \[[`b3090f8e64`](https://github.com/nodejs/node/commit/b3090f8e64)] - **win,build**: fail on invalid option in vcbuild (João Reis) [#8412](https://github.com/nodejs/node/pull/8412)
* \[[`1b5213bfc3`](https://github.com/nodejs/node/commit/1b5213bfc3)] - **win,msi**: fix inclusion of translations (João Reis) [#7798](https://github.com/nodejs/node/pull/7798)
* \[[`e8be413d0d`](https://github.com/nodejs/node/commit/e8be413d0d)] - **win,msi**: add zh-CN translations for the installer (Minqi Pan) [#2569](https://github.com/nodejs/node/pull/2569)
* \[[`99f85b8340`](https://github.com/nodejs/node/commit/99f85b8340)] - **win,msi**: Added Italian translation (Matteo Collina) [#4647](https://github.com/nodejs/node/pull/4647)

<a id="4.6.1"></a>

## 2016-10-18, Version 4.6.1 'Argon' (LTS), @rvagg

This is a security release. All Node.js users should consult the security release summary at <https://nodejs.org/en/blog/vulnerability/october-2016-security-releases/> for details on patched vulnerabilities.

### Notable Changes

* **c-ares**: fix for single-byte buffer overwrite, CVE-2016-5180, more information at <https://c-ares.haxx.se/adv_20160929.html> (Daniel Stenberg)

### Commits

* \[[`f3c63e7ccf`](https://github.com/nodejs/node/commit/f3c63e7ccf)] - **deps**: avoid single-byte buffer overwrite (Daniel Stenberg) [#8849](https://github.com/nodejs/node/pull/8849)
* \[[`5a0daa6c2f`](https://github.com/nodejs/node/commit/5a0daa6c2f)] - **win,build**: try multiple timeservers when signing (Rod Vagg) [#9155](https://github.com/nodejs/node/pull/9155)

<a id="4.6.0"></a>

## 2016-09-27, Version 4.6.0 'Argon' (LTS), @rvagg

This is a security release. All Node.js users should consult the security release summary at <https://nodejs.org/en/blog/vulnerability/september-2016-security-releases/> for details on patched vulnerabilities.

### Notable Changes

Semver Minor:

* **openssl**:
  * Upgrade to 1.0.2i, fixes a number of defects impacting Node.js: CVE-2016-6304 ("OCSP Status Request extension unbounded memory growth", high severity), CVE-2016-2183, CVE-2016-6303, CVE-2016-2178 and CVE-2016-6306. (Shigeki Ohtsu) [#8714](https://github.com/nodejs/node/pull/8714)
  * Upgrade to 1.0.2j, fixes a defect included in 1.0.2i resulting in a crash when using CRLs, CVE-2016-7052. (Shigeki Ohtsu) [#8786](https://github.com/nodejs/node/pull/8786)
  * Remove support for loading dynamic third-party engine modules. An attacker may be able to hide malicious code to be inserted into Node.js at runtime by masquerading as one of the dynamic engine modules. Originally reported by Ahmed Zaki (Skype). (Ben Noordhuis) [nodejs/node-private#70](https://github.com/nodejs/node-private/pull/70)
* **http**: CVE-2016-5325 - Properly validate for allowable characters in the `reason` argument in `ServerResponse#writeHead()`. Fixes a possible response splitting attack vector. This introduces a new case where `throw` may occur when configuring HTTP responses, users should already be adopting try/catch here. Originally reported independently by Evan Lucas and Romain Gaucher. (Evan Lucas) [nodejs/node-private#46](https://github.com/nodejs/node-private/pull/46)

Semver Patch:

* **buffer**: Zero-fill excess bytes in new `Buffer` objects created with `Buffer.concat()` while providing a `totalLength` parameter that exceeds the total length of the original `Buffer` objects being concatenated. (Сковорода Никита Андреевич) [nodejs/node-private#65](https://github.com/nodejs/node-private/pull/65)
* **tls**: CVE-2016-7099 - Fix invalid wildcard certificate validation check whereby a TLS server may be able to serve an invalid wildcard certificate for its hostname due to improper validation of `*.` in the wildcard string. Originally reported by Alexander Minozhenko and James Bunton (Atlassian). (Ben Noordhuis) [nodejs/node-private#63](https://github.com/nodejs/node-private/pull/63)

### Commits

* \[[`93b10fbec2`](https://github.com/nodejs/node/commit/93b10fbec2)] - **buffer**: zero-fill uninitialized bytes in .concat() (Сковорода Никита Андреевич) [nodejs/node-private#65](https://github.com/nodejs/node-private/pull/65)
* \[[`c214e8847d`](https://github.com/nodejs/node/commit/c214e8847d)] - **crypto**: don't build hardware engines (Ben Noordhuis) [nodejs/node-private#70](https://github.com/nodejs/node-private/pull/70)
* \[[`af9dda152c`](https://github.com/nodejs/node/commit/af9dda152c)] - **deps**: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) [nodejs/node#1836](https://github.com/nodejs/node/pull/1836)
* \[[`6bb9749c33`](https://github.com/nodejs/node/commit/6bb9749c33)] - **deps**: fix asm build error of openssl in x86\_win32 (Shigeki Ohtsu) [nodejs/node#1389](https://github.com/nodejs/node/pull/1389)
* \[[`5176a8ad57`](https://github.com/nodejs/node/commit/5176a8ad57)] - **deps**: fix openssl assembly error on ia32 win32 (Fedor Indutny) [nodejs/node#1389](https://github.com/nodejs/node/pull/1389)
* \[[`aa9ed60a51`](https://github.com/nodejs/node/commit/aa9ed60a51)] - **deps**: copy all openssl header files to include dir (Shigeki Ohtsu) [#8786](https://github.com/nodejs/node/pull/8786)
* \[[`0c74e2ad35`](https://github.com/nodejs/node/commit/0c74e2ad35)] - **deps**: upgrade openssl sources to 1.0.2j (Shigeki Ohtsu) [#8786](https://github.com/nodejs/node/pull/8786)
* \[[`8f3d6760cf`](https://github.com/nodejs/node/commit/8f3d6760cf)] - **deps**: update openssl asm and asm\_obsolete files (Shigeki Ohtsu) [#8714](https://github.com/nodejs/node/pull/8714)
* \[[`e8f29e2ba8`](https://github.com/nodejs/node/commit/e8f29e2ba8)] - **deps**: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) [nodejs/node#1836](https://github.com/nodejs/node/pull/1836)
* \[[`01cf5b0ae7`](https://github.com/nodejs/node/commit/01cf5b0ae7)] - **deps**: fix asm build error of openssl in x86\_win32 (Shigeki Ohtsu) [nodejs/node#1389](https://github.com/nodejs/node/pull/1389)
* \[[`19ae4e8ae1`](https://github.com/nodejs/node/commit/19ae4e8ae1)] - **deps**: fix openssl assembly error on ia32 win32 (Fedor Indutny) [nodejs/node#1389](https://github.com/nodejs/node/pull/1389)
* \[[`cbed5e64be`](https://github.com/nodejs/node/commit/cbed5e64be)] - **deps**: copy all openssl header files to include dir (Shigeki Ohtsu) [#8714](https://github.com/nodejs/node/pull/8714)
* \[[`e7fdace18f`](https://github.com/nodejs/node/commit/e7fdace18f)] - **deps**: upgrade openssl sources to 1.0.2i (Shigeki Ohtsu) [#8714](https://github.com/nodejs/node/pull/8714)
* \[[`b5c57ff772`](https://github.com/nodejs/node/commit/b5c57ff772)] - **http**: check reason chars in writeHead (Evan Lucas) [nodejs/node-private#46](https://github.com/nodejs/node-private/pull/46)
* \[[`3ff82deb2c`](https://github.com/nodejs/node/commit/3ff82deb2c)] - **lib**: make tls.checkServerIdentity() more strict (Ben Noordhuis) [nodejs/node-private#63](https://github.com/nodejs/node-private/pull/63)
* \[[`7c696e201a`](https://github.com/nodejs/node/commit/7c696e201a)] - **openssl**: fix keypress requirement in apps on win32 (Shigeki Ohtsu) [nodejs/node#1389](https://github.com/nodejs/node/pull/1389)
* \[[`44e5776c0f`](https://github.com/nodejs/node/commit/44e5776c0f)] - **openssl**: fix keypress requirement in apps on win32 (Shigeki Ohtsu) [nodejs/node#1389](https://github.com/nodejs/node/pull/1389)
* \[[`c7a601c090`](https://github.com/nodejs/node/commit/c7a601c090)] - **test**: remove openssl options of -no\_\<prot> (Shigeki Ohtsu) [#8714](https://github.com/nodejs/node/pull/8714)

<a id="4.5.0"></a>

## 2016-08-15, Version 4.5.0 'Argon' (LTS), @thealphanerd

### Notable Changes

Semver Minor:

* **buffer**:
  * backport new buffer constructor APIs to v4.x (Сковорода Никита Андреевич) [#7562](https://github.com/nodejs/node/pull/7562)
  * backport --zero-fill-buffers cli option (James M Snell) [#5745](https://github.com/nodejs/node/pull/5745)
* **build**:
  * add Intel Vtune profiling support (Chunyang Dai) [#5527](https://github.com/nodejs/node/pull/5527)
* **repl**:
  * copying tabs shouldn't trigger completion (Eugene Obrezkov) [#5958](https://github.com/nodejs/node/pull/5958)
* **src**:
  * add node::FreeEnvironment public API (Cheng Zhao) [#3098](https://github.com/nodejs/node/pull/3098)
* **test**:
  * run v8 tests from node tree (Bryon Leung) [#4704](https://github.com/nodejs/node/pull/4704)
* **V8**:
  * Add post mortem data to improve object inspection and function's context variables inspection (Fedor Indutny) [#3779](https://github.com/nodejs/node/pull/3779)

Semver Patch:

* **buffer**:
  * ignore negative allocation lengths (Anna Henningsen) [#7562](https://github.com/nodejs/node/pull/7562)
* **crypto**:
  * update root certificates (Ben Noordhuis) [#7363](https://github.com/nodejs/node/pull/7363)
* **libuv**:
  * upgrade libuv to 1.9.1 (Saúl Ibarra Corretgé) [#6796](https://github.com/nodejs/node/pull/6796)
  * upgrade libuv to 1.9.0 (Saúl Ibarra Corretgé) [#5994](https://github.com/nodejs/node/pull/5994)
* **npm**:
  * upgrade to 2.15.9 (Kat Marchán) [#7692](https://github.com/nodejs/node/pull/7692)

### Commits

* \[[`a4888926a2`](https://github.com/nodejs/node/commit/a4888926a2)] - **assert**: remove unneeded arguments special handling (Rich Trott) [#7413](https://github.com/nodejs/node/pull/7413)
* \[[`39e24742f8`](https://github.com/nodejs/node/commit/39e24742f8)] - **assert**: allow circular references (Rich Trott) [#6432](https://github.com/nodejs/node/pull/6432)
* \[[`271927f29e`](https://github.com/nodejs/node/commit/271927f29e)] - **async\_wrap**: pass uid to JS as double (Trevor Norris) [#7096](https://github.com/nodejs/node/pull/7096)
* \[[`747f107188`](https://github.com/nodejs/node/commit/747f107188)] - **async\_wrap**: don't abort on callback exception (Trevor Norris) [#5756](https://github.com/nodejs/node/pull/5756)
* \[[`c06e2b07b6`](https://github.com/nodejs/node/commit/c06e2b07b6)] - **async\_wrap**: notify post if intercepted exception (Trevor Norris) [#5756](https://github.com/nodejs/node/pull/5756)
* \[[`0642a146b3`](https://github.com/nodejs/node/commit/0642a146b3)] - **async\_wrap**: setupHooks now accepts object (Trevor Norris) [#5756](https://github.com/nodejs/node/pull/5756)
* \[[`75ecf8eb07`](https://github.com/nodejs/node/commit/75ecf8eb07)] - **async\_wrap**: add parent uid to init hook (Andreas Madsen) [#4600](https://github.com/nodejs/node/pull/4600)
* \[[`e10eebffa5`](https://github.com/nodejs/node/commit/e10eebffa5)] - **async\_wrap**: make uid the first argument in init (Andreas Madsen) [#4600](https://github.com/nodejs/node/pull/4600)
* \[[`13d465bcf6`](https://github.com/nodejs/node/commit/13d465bcf6)] - **async\_wrap**: add uid to all asyncWrap hooks (Andreas Madsen) [#4600](https://github.com/nodejs/node/pull/4600)
* \[[`046d651118`](https://github.com/nodejs/node/commit/046d651118)] - **benchmark**: fix child-process-exec-stdout on win (Bartosz Sosnowski) [#7178](https://github.com/nodejs/node/pull/7178)
* \[[`4b464ce4bf`](https://github.com/nodejs/node/commit/4b464ce4bf)] - **benchmark**: remove unused variables (Rich Trott) [#7600](https://github.com/nodejs/node/pull/7600)
* \[[`b95e5d7948`](https://github.com/nodejs/node/commit/b95e5d7948)] - **benchmark**: add benchmark for url.format() (Rich Trott) [#7250](https://github.com/nodejs/node/pull/7250)
* \[[`1bd62c7c34`](https://github.com/nodejs/node/commit/1bd62c7c34)] - **benchmark**: add benchmark for Buffer.concat (Anna Henningsen) [#7054](https://github.com/nodejs/node/pull/7054)
* \[[`08cd81b050`](https://github.com/nodejs/node/commit/08cd81b050)] - **benchmark**: add util.format benchmark (Evan Lucas) [#5360](https://github.com/nodejs/node/pull/5360)
* \[[`7dbb0d0084`](https://github.com/nodejs/node/commit/7dbb0d0084)] - **buffer**: fix dataview-set benchmark (Ingvar Stepanyan) [#6922](https://github.com/nodejs/node/pull/6922)
* \[[`200429e9e1`](https://github.com/nodejs/node/commit/200429e9e1)] - **buffer**: ignore negative allocation lengths (Anna Henningsen) [#7562](https://github.com/nodejs/node/pull/7562)
* \[[`709048134c`](https://github.com/nodejs/node/commit/709048134c)] - **(SEMVER-MINOR)** **buffer**: backport new buffer constructor APIs to v4.x (Сковорода Никита Андреевич) [#7562](https://github.com/nodejs/node/pull/7562)
* \[[`fb03e57de2`](https://github.com/nodejs/node/commit/fb03e57de2)] - **(SEMVER-MINOR)** **buffer**: backport --zero-fill-buffers cli option (James M Snell) [#5745](https://github.com/nodejs/node/pull/5745)
* \[[`236491e698`](https://github.com/nodejs/node/commit/236491e698)] - **build**: update build-addons when node-gyp changes (Lance Ball) [#6787](https://github.com/nodejs/node/pull/6787)
* \[[`8a7c5fdbd2`](https://github.com/nodejs/node/commit/8a7c5fdbd2)] - **build**: add REPLACEME tag for version info in docs (Ben Noordhuis) [#6864](https://github.com/nodejs/node/pull/6864)
* \[[`da1e13fde7`](https://github.com/nodejs/node/commit/da1e13fde7)] - **build**: add Make `doc-only` target (Jesse McCarthy) [#3888](https://github.com/nodejs/node/pull/3888)
* \[[`0db3aa9afa`](https://github.com/nodejs/node/commit/0db3aa9afa)] - **build**: remove unused files from CPPLINT\_FILES (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`5290c9d38c`](https://github.com/nodejs/node/commit/5290c9d38c)] - **build**: use BUILDTYPE when building V8 in Makefile (Michaël Zasso) [#7482](https://github.com/nodejs/node/pull/7482)
* \[[`79bd39c202`](https://github.com/nodejs/node/commit/79bd39c202)] - **build**: add v8 requirement to test-v8\* in Makefile (Michaël Zasso) [#7482](https://github.com/nodejs/node/pull/7482)
* \[[`65b75b51a6`](https://github.com/nodejs/node/commit/65b75b51a6)] - **build**: unbreak configure with python 2.6 (Ben Noordhuis) [#6874](https://github.com/nodejs/node/pull/6874)
* \[[`8513232c82`](https://github.com/nodejs/node/commit/8513232c82)] - **build**: split CI rules in Makefile (João Reis) [#7317](https://github.com/nodejs/node/pull/7317)
* \[[`13d0e463b0`](https://github.com/nodejs/node/commit/13d0e463b0)] - **build**: enable compilation for linuxOne (Michael Dawson) [#5941](https://github.com/nodejs/node/pull/5941)
* \[[`834ea2c5c0`](https://github.com/nodejs/node/commit/834ea2c5c0)] - **(SEMVER-MINOR)** **build,src**: add Intel Vtune profiling support (Chunyang Dai) [#5527](https://github.com/nodejs/node/pull/5527)
* \[[`ea20796e9d`](https://github.com/nodejs/node/commit/ea20796e9d)] - **build,test**: fix build-addons dependency chain (Ben Noordhuis) [#6652](https://github.com/nodejs/node/pull/6652)
* \[[`6a08535dd1`](https://github.com/nodejs/node/commit/6a08535dd1)] - **child\_process**: preserve argument type (Rich Trott) [#7391](https://github.com/nodejs/node/pull/7391)
* \[[`fd05b0b289`](https://github.com/nodejs/node/commit/fd05b0b289)] - _**Revert**_ "**child\_process**: measure buffer length in bytes" (Rich Trott) [#7391](https://github.com/nodejs/node/pull/7391)
* \[[`8eb18e4289`](https://github.com/nodejs/node/commit/8eb18e4289)] - **child\_process**: measure buffer length in bytes (Rich Trott) [#6764](https://github.com/nodejs/node/pull/6764)
* \[[`4ee863d956`](https://github.com/nodejs/node/commit/4ee863d956)] - **child\_process**: allow buffer encoding in spawnSync (cjihrig) [#6939](https://github.com/nodejs/node/pull/6939)
* \[[`0b8124f205`](https://github.com/nodejs/node/commit/0b8124f205)] - **child\_process**: emit IPC messages on next tick (cjihrig) [#6909](https://github.com/nodejs/node/pull/6909)
* \[[`20d3378969`](https://github.com/nodejs/node/commit/20d3378969)] - **cluster**: reset handle index on close (Santiago Gimeno) [#6981](https://github.com/nodejs/node/pull/6981)
* \[[`09349a8b92`](https://github.com/nodejs/node/commit/09349a8b92)] - **cluster**: don't send messages if no IPC channel (Santiago Gimeno) [#7132](https://github.com/nodejs/node/pull/7132)
* \[[`6ece2a0322`](https://github.com/nodejs/node/commit/6ece2a0322)] - **cluster**: rewrite debug ports consistently (cjihrig) [#7050](https://github.com/nodejs/node/pull/7050)
* \[[`8cba3b2f72`](https://github.com/nodejs/node/commit/8cba3b2f72)] - **cluster**: guard against undefined message handlers (cjihrig) [#6902](https://github.com/nodejs/node/pull/6902)
* \[[`f152adf5b7`](https://github.com/nodejs/node/commit/f152adf5b7)] - **cluster**: close ownerless handles on disconnect() (cjihrig) [#6909](https://github.com/nodejs/node/pull/6909)
* \[[`65624440bf`](https://github.com/nodejs/node/commit/65624440bf)] - **crypto**: allow GCM ciphers to have longer IV length (Michael Wain) [#6376](https://github.com/nodejs/node/pull/6376)
* \[[`1e0cede3a6`](https://github.com/nodejs/node/commit/1e0cede3a6)] - **crypto**: update root certificates (Ben Noordhuis) [#7363](https://github.com/nodejs/node/pull/7363)
* \[[`3be5cdcd43`](https://github.com/nodejs/node/commit/3be5cdcd43)] - **debugger**: remove obsolete setTimeout (Rich Trott) [#7154](https://github.com/nodejs/node/pull/7154)
* \[[`74a5e911c0`](https://github.com/nodejs/node/commit/74a5e911c0)] - **debugger**: propagate --debug-port= to debuggee (Ben Noordhuis) [#3470](https://github.com/nodejs/node/pull/3470)
* \[[`af4940d63b`](https://github.com/nodejs/node/commit/af4940d63b)] - **deps**: upgrade npm in LTS to 2.15.9 (Kat Marchán) [#7692](https://github.com/nodejs/node/pull/7692)
* \[[`da7b74b9bc`](https://github.com/nodejs/node/commit/da7b74b9bc)] - **deps**: upgrade libuv to 1.9.1 (Saúl Ibarra Corretgé) [#6796](https://github.com/nodejs/node/pull/6796)
* \[[`94eb980ca5`](https://github.com/nodejs/node/commit/94eb980ca5)] - **deps**: upgrade libuv to 1.9.0 (Saúl Ibarra Corretgé) [#5994](https://github.com/nodejs/node/pull/5994)
* \[[`4107b5d200`](https://github.com/nodejs/node/commit/4107b5d200)] - **deps**: backport 22c5e46 from V8 (Julien Gilli) [#7584](https://github.com/nodejs/node/pull/7584)
* \[[`e06ab64705`](https://github.com/nodejs/node/commit/e06ab64705)] - **deps**: update to http-parser 2.7.0 (Fedor Indutny) [#6279](https://github.com/nodejs/node/pull/6279)
* \[[`1164f542db`](https://github.com/nodejs/node/commit/1164f542db)] - **deps**: fix segfault during gc (Ali Ijaz Sheikh) [#7303](https://github.com/nodejs/node/pull/7303)
* \[[`d9e9d9fb11`](https://github.com/nodejs/node/commit/d9e9d9fb11)] - **deps**: backport e7cc609 from upstream V8 (Ali Ijaz Sheikh) [#7303](https://github.com/nodejs/node/pull/7303)
* \[[`9809992436`](https://github.com/nodejs/node/commit/9809992436)] - **(SEMVER-MINOR)** **deps**: backport 9c927d0f01 from V8 upstream (Myles Borins) [#7451](https://github.com/nodejs/node/pull/7451)
* \[[`da9595fc47`](https://github.com/nodejs/node/commit/da9595fc47)] - **(SEMVER-MINOR)** **deps**: cherry-pick 68e89fb from v8's upstream (Fedor Indutny) [#3779](https://github.com/nodejs/node/pull/3779)
* \[[`e9ff0f8fb2`](https://github.com/nodejs/node/commit/e9ff0f8fb2)] - **doc**: make doc-only -> fallback to user binary (Robert Jefe Lindstaedt) [#6906](https://github.com/nodejs/node/pull/6906)
* \[[`b869cdb876`](https://github.com/nodejs/node/commit/b869cdb876)] - **doc**: fix deprecation warnings in addon examples (Ben Noordhuis) [#6652](https://github.com/nodejs/node/pull/6652)
* \[[`ec25f38120`](https://github.com/nodejs/node/commit/ec25f38120)] - **doc**: add `added:` information for buffer (Anna Henningsen) [#6495](https://github.com/nodejs/node/pull/6495)
* \[[`1e86d16812`](https://github.com/nodejs/node/commit/1e86d16812)] - **doc**: buffers are not sent over IPC with a socket (Tim Kuijsten) [#6951](https://github.com/nodejs/node/pull/6951)
* \[[`5c1d8e1f0f`](https://github.com/nodejs/node/commit/5c1d8e1f0f)] - **doc**: add `added:` information for http (Anna Henningsen) [#7392](https://github.com/nodejs/node/pull/7392)
* \[[`60c054bc11`](https://github.com/nodejs/node/commit/60c054bc11)] - **doc**: add information for IncomingMessage.destroy() (Rich Trott) [#7237](https://github.com/nodejs/node/pull/7237)
* \[[`1a5c025f32`](https://github.com/nodejs/node/commit/1a5c025f32)] - **doc**: remove superfluos backticks in process.md (Anna Henningsen) [#7681](https://github.com/nodejs/node/pull/7681)
* \[[`fcb4e410e4`](https://github.com/nodejs/node/commit/fcb4e410e4)] - **doc**: add `added:` information for process (Bryan English) [#6589](https://github.com/nodejs/node/pull/6589)
* \[[`9b8565c42a`](https://github.com/nodejs/node/commit/9b8565c42a)] - **doc**: add `added:` information for tls (Italo A. Casas) [#7018](https://github.com/nodejs/node/pull/7018)
* \[[`fd4aa6c16a`](https://github.com/nodejs/node/commit/fd4aa6c16a)] - **doc**: correct `added:` information for fs.access (Richard Lau) [#7299](https://github.com/nodejs/node/pull/7299)
* \[[`1e9d27cbcc`](https://github.com/nodejs/node/commit/1e9d27cbcc)] - **doc**: add `added:` information for fs (Anna Henningsen) [#6717](https://github.com/nodejs/node/pull/6717)
* \[[`2244a3c250`](https://github.com/nodejs/node/commit/2244a3c250)] - **doc**: adds 'close' events to fs.ReadStream and fs.WriteStream (Jenna Vuong) [#6499](https://github.com/nodejs/node/pull/6499)
* \[[`88f46b886a`](https://github.com/nodejs/node/commit/88f46b886a)] - **doc**: add `added:` information for timers (Anna Henningsen) [#7493](https://github.com/nodejs/node/pull/7493)
* \[[`a53253a232`](https://github.com/nodejs/node/commit/a53253a232)] - **doc**: add `added:` information for zlib (Anna Henningsen) [#6840](https://github.com/nodejs/node/pull/6840)
* \[[`7abfb6e8dc`](https://github.com/nodejs/node/commit/7abfb6e8dc)] - **doc**: add `added:` information for vm (Anna Henningsen) [#7011](https://github.com/nodejs/node/pull/7011)
* \[[`3e3471fb5f`](https://github.com/nodejs/node/commit/3e3471fb5f)] - **doc**: add `added:` information for v8 (Rich Trott) [#6684](https://github.com/nodejs/node/pull/6684)
* \[[`1758f02ec1`](https://github.com/nodejs/node/commit/1758f02ec1)] - **doc**: add `added:` information for url (Bryan English) [#6593](https://github.com/nodejs/node/pull/6593)
* \[[`3c8f19fcdf`](https://github.com/nodejs/node/commit/3c8f19fcdf)] - **doc**: add `added:` in for `tty` (Rich Trott) [#6783](https://github.com/nodejs/node/pull/6783)
* \[[`5b50b1c255`](https://github.com/nodejs/node/commit/5b50b1c255)] - **doc**: add `added:` info for `string_decoder` (Rich Trott) [#6741](https://github.com/nodejs/node/pull/6741)
* \[[`4474e83b78`](https://github.com/nodejs/node/commit/4474e83b78)] - **doc**: add `added:` information for repl (Anna Henningsen) [#7256](https://github.com/nodejs/node/pull/7256)
* \[[`e6d7bfcbe7`](https://github.com/nodejs/node/commit/e6d7bfcbe7)] - **doc**: add `added:` information for readline (Julian Duque) [#6996](https://github.com/nodejs/node/pull/6996)
* \[[`eec0c635ee`](https://github.com/nodejs/node/commit/eec0c635ee)] - **doc**: add `added:` information for querystring (Bryan English) [#6593](https://github.com/nodejs/node/pull/6593)
* \[[`a870cdcd1f`](https://github.com/nodejs/node/commit/a870cdcd1f)] - **doc**: add `added:` information for punycode (Daniel Wang) [#6805](https://github.com/nodejs/node/pull/6805)
* \[[`f1a37ad749`](https://github.com/nodejs/node/commit/f1a37ad749)] - **doc**: add `added:` information for path (Julian Duque) [#6985](https://github.com/nodejs/node/pull/6985)
* \[[`8b53f4b27c`](https://github.com/nodejs/node/commit/8b53f4b27c)] - **doc**: add `added:` information for os (Bryan English) [#6609](https://github.com/nodejs/node/pull/6609)
* \[[`78d361b22b`](https://github.com/nodejs/node/commit/78d361b22b)] - **doc**: add `added` information for net (Italo A. Casas) [#7038](https://github.com/nodejs/node/pull/7038)
* \[[`b08ff33c01`](https://github.com/nodejs/node/commit/b08ff33c01)] - **doc**: add `added:` information for https (Anna Henningsen) [#7392](https://github.com/nodejs/node/pull/7392)
* \[[`1d99059bb1`](https://github.com/nodejs/node/commit/1d99059bb1)] - **doc**: add `added:` information for dns (Julian Duque) [#7021](https://github.com/nodejs/node/pull/7021)
* \[[`a0ca24b798`](https://github.com/nodejs/node/commit/a0ca24b798)] - **doc**: add `added:` information for console (Adrian Estrada) [#6995](https://github.com/nodejs/node/pull/6995)
* \[[`eb08c17a20`](https://github.com/nodejs/node/commit/eb08c17a20)] - **doc**: add `added: ` data for cli.md (Rich Trott) [#6960](https://github.com/nodejs/node/pull/6960)
* \[[`ec9038478f`](https://github.com/nodejs/node/commit/ec9038478f)] - **doc**: add `added:` information for child\_process (Anna Henningsen) [#6927](https://github.com/nodejs/node/pull/6927)
* \[[`e52b2b07d7`](https://github.com/nodejs/node/commit/e52b2b07d7)] - **doc**: add `added:` information for assert (Rich Trott) [#6688](https://github.com/nodejs/node/pull/6688)
* \[[`75e4f74c54`](https://github.com/nodejs/node/commit/75e4f74c54)] - **doc**: fix cluster worker 'message' event (cjihrig) [#7309](https://github.com/nodejs/node/pull/7309)
* \[[`de5e2357fc`](https://github.com/nodejs/node/commit/de5e2357fc)] - **doc**: dns.resolve fix callback argument description (Quentin Headen) [#7532](https://github.com/nodejs/node/pull/7532)
* \[[`0f903bb722`](https://github.com/nodejs/node/commit/0f903bb722)] - **doc**: add benchmark who-to-CC info (Rich Trott) [#7604](https://github.com/nodejs/node/pull/7604)
* \[[`700c6d9be8`](https://github.com/nodejs/node/commit/700c6d9be8)] - **doc**: added information on how to run the linter. (Diosney Sarmiento) [#7534](https://github.com/nodejs/node/pull/7534)
* \[[`537f33351e`](https://github.com/nodejs/node/commit/537f33351e)] - **doc**: fix minor style issues in http.md (Rich Trott) [#7528](https://github.com/nodejs/node/pull/7528)
* \[[`33a08b0414`](https://github.com/nodejs/node/commit/33a08b0414)] - **doc**: add bartosz sosnowski to colaborators (Bartosz Sosnowski) [#7567](https://github.com/nodejs/node/pull/7567)
* \[[`186af29298`](https://github.com/nodejs/node/commit/186af29298)] - **doc**: fix detached child stdio example (cjihrig) [#7540](https://github.com/nodejs/node/pull/7540)
* \[[`066cefb6de`](https://github.com/nodejs/node/commit/066cefb6de)] - **doc**: improve usage of `zero`/`0` (Rich Trott) [#7466](https://github.com/nodejs/node/pull/7466)
* \[[`6c94c67b73`](https://github.com/nodejs/node/commit/6c94c67b73)] - **doc**: fix "sign.verify" typo in crypto doc. (Ruslan Iusupov) [#7411](https://github.com/nodejs/node/pull/7411)
* \[[`35ee35cba2`](https://github.com/nodejs/node/commit/35ee35cba2)] - **doc**: clarify child\_process stdout/stderr types (sartrey) [#7361](https://github.com/nodejs/node/pull/7361)
* \[[`71ef71cff8`](https://github.com/nodejs/node/commit/71ef71cff8)] - **doc**: add CTC meeting minutes 2016-06-15 (Josh Gavant) [#7320](https://github.com/nodejs/node/pull/7320)
* \[[`13d60cab7c`](https://github.com/nodejs/node/commit/13d60cab7c)] - **doc**: add lance to collaborators (Lance Ball) [#7407](https://github.com/nodejs/node/pull/7407)
* \[[`9122b3b665`](https://github.com/nodejs/node/commit/9122b3b665)] - **doc**: update "who to cc in issues" chart (Jeremiah Senkpiel) [#6694](https://github.com/nodejs/node/pull/6694)
* \[[`ccb278d330`](https://github.com/nodejs/node/commit/ccb278d330)] - **doc**: mention http request "aborted" events (Kyle E. Mitchell) [#7270](https://github.com/nodejs/node/pull/7270)
* \[[`868af29f2b`](https://github.com/nodejs/node/commit/868af29f2b)] - **doc**: add RReverser to collaborators (Ingvar Stepanyan) [#7370](https://github.com/nodejs/node/pull/7370)
* \[[`f8fe474825`](https://github.com/nodejs/node/commit/f8fe474825)] - **doc**: fixing minor typo in AtExit hooks section (Daniel Bevenius) [#7485](https://github.com/nodejs/node/pull/7485)
* \[[`4a7e333287`](https://github.com/nodejs/node/commit/4a7e333287)] - **doc**: use `Buffer.byteLength` for Content-Length (kimown) [#7274](https://github.com/nodejs/node/pull/7274)
* \[[`85f70b36e4`](https://github.com/nodejs/node/commit/85f70b36e4)] - **doc**: clarify use of `0` port value (Rich Trott) [#7206](https://github.com/nodejs/node/pull/7206)
* \[[`57ba51ec46`](https://github.com/nodejs/node/commit/57ba51ec46)] - **doc**: fix IRC link (Ilkka Myller) [#7210](https://github.com/nodejs/node/pull/7210)
* \[[`ef37a2e80f`](https://github.com/nodejs/node/commit/ef37a2e80f)] - **doc**: add internal link in GOVERNANCE.md (Rich Trott) [#7279](https://github.com/nodejs/node/pull/7279)
* \[[`c9ef04a1b2`](https://github.com/nodejs/node/commit/c9ef04a1b2)] - **doc**: fix events typo (Greyson Parrelli) [#7329](https://github.com/nodejs/node/pull/7329)
* \[[`0013af61de`](https://github.com/nodejs/node/commit/0013af61de)] - **doc**: fix header depth of util.isSymbol (James M Snell) [#7138](https://github.com/nodejs/node/pull/7138)
* \[[`96de3f8820`](https://github.com/nodejs/node/commit/96de3f8820)] - **doc**: Add CII Best Practices badge to README.md (David A. Wheeler) [#6819](https://github.com/nodejs/node/pull/6819)
* \[[`146cba1f60`](https://github.com/nodejs/node/commit/146cba1f60)] - **doc**: improve debugger doc prose (Rich Trott) [#7007](https://github.com/nodejs/node/pull/7007)
* \[[`694e34458b`](https://github.com/nodejs/node/commit/694e34458b)] - **doc**: fix typos in WORKING\_GROUPS.md (Joao Andrade) [#7032](https://github.com/nodejs/node/pull/7032)
* \[[`fbdc16a8a4`](https://github.com/nodejs/node/commit/fbdc16a8a4)] - **doc**: update labels and CI info in onboarding doc (Rich Trott) [#7006](https://github.com/nodejs/node/pull/7006)
* \[[`1c65f1e3f6`](https://github.com/nodejs/node/commit/1c65f1e3f6)] - **doc**: add info on what's used for fswatch on AIX (Michael Dawson) [#6837](https://github.com/nodejs/node/pull/6837)
* \[[`72e8ee570a`](https://github.com/nodejs/node/commit/72e8ee570a)] - **doc**: improve server.listen() documentation prose (Rich Trott) [#7000](https://github.com/nodejs/node/pull/7000)
* \[[`649d201d63`](https://github.com/nodejs/node/commit/649d201d63)] - **doc**: improve `server.address()` doc text (Rich Trott) [#7001](https://github.com/nodejs/node/pull/7001)
* \[[`e2e85ced1d`](https://github.com/nodejs/node/commit/e2e85ced1d)] - **doc**: clarified use of sexual language in the CoC (Bryan Hughes) [#6973](https://github.com/nodejs/node/pull/6973)
* \[[`f395f6f5b2`](https://github.com/nodejs/node/commit/f395f6f5b2)] - **doc**: add yorkie to collaborators (Yazhong Liu) [#7004](https://github.com/nodejs/node/pull/7004)
* \[[`c5051ef643`](https://github.com/nodejs/node/commit/c5051ef643)] - **doc**: add firedfox to collaborators (Daniel Wang) [#6961](https://github.com/nodejs/node/pull/6961)
* \[[`2ef08323c6`](https://github.com/nodejs/node/commit/2ef08323c6)] - **doc**: add bmeck to collaborators (Bradley Meck) [#6962](https://github.com/nodejs/node/pull/6962)
* \[[`d1a0a146b3`](https://github.com/nodejs/node/commit/d1a0a146b3)] - **doc**: Add CTC meeting minutes for 2016-05-04 (Michael Dawson) [#6579](https://github.com/nodejs/node/pull/6579)
* \[[`0a85987899`](https://github.com/nodejs/node/commit/0a85987899)] - **doc**: update build instructions for Windows (João Reis) [#7285](https://github.com/nodejs/node/pull/7285)
* \[[`629a76f9fb`](https://github.com/nodejs/node/commit/629a76f9fb)] - **doc**: remove cluster.setupMaster() myth (cjihrig) [#7179](https://github.com/nodejs/node/pull/7179)
* \[[`5b807ac791`](https://github.com/nodejs/node/commit/5b807ac791)] - **doc**: specify how to link issues in commit log (Luigi Pinca) [#7161](https://github.com/nodejs/node/pull/7161)
* \[[`350f4cf292`](https://github.com/nodejs/node/commit/350f4cf292)] - **doc**: server.listen truncates socket path on unix (Jean Regisser) [#6659](https://github.com/nodejs/node/pull/6659)
* \[[`7813af7f16`](https://github.com/nodejs/node/commit/7813af7f16)] - **doc**: Add resolveNaptr and naptr rrtype docs (Doug Wade) [#6586](https://github.com/nodejs/node/pull/6586)
* \[[`5380743208`](https://github.com/nodejs/node/commit/5380743208)] - **doc**: document socket.destroyed (Tushar Mathur) [#6128](https://github.com/nodejs/node/pull/6128)
* \[[`f0edf87df1`](https://github.com/nodejs/node/commit/f0edf87df1)] - **doc**: add vm example, be able to require modules (Robert Jefe Lindstaedt) [#5323](https://github.com/nodejs/node/pull/5323)
* \[[`9121e94e62`](https://github.com/nodejs/node/commit/9121e94e62)] - **doc**: note that process.config can and will be changed (James M Snell) [#6266](https://github.com/nodejs/node/pull/6266)
* \[[`c237ac3d68`](https://github.com/nodejs/node/commit/c237ac3d68)] - **doc**: git mv to .md (Robert Jefe Lindstaedt) [#4747](https://github.com/nodejs/node/pull/4747)
* \[[`6324723cc1`](https://github.com/nodejs/node/commit/6324723cc1)] - **doc,dgram**: fix addMembership documentation (Santiago Gimeno) [#7244](https://github.com/nodejs/node/pull/7244)
* \[[`15bb0beab2`](https://github.com/nodejs/node/commit/15bb0beab2)] - **doc,test**: add `How to write a Node.js test` guide (Santiago Gimeno) [#6984](https://github.com/nodejs/node/pull/6984)
* \[[`9d13337183`](https://github.com/nodejs/node/commit/9d13337183)] - **http**: wait for both prefinish/end to keepalive (Fedor Indutny) [#7149](https://github.com/nodejs/node/pull/7149)
* \[[`ece428ea63`](https://github.com/nodejs/node/commit/ece428ea63)] - **http**: fix no dumping after `maybeReadMore` (Fedor Indutny) [#7211](https://github.com/nodejs/node/pull/7211)
* \[[`07fd52e5aa`](https://github.com/nodejs/node/commit/07fd52e5aa)] - **http**: skip body and next message of CONNECT res (Fedor Indutny) [#6279](https://github.com/nodejs/node/pull/6279)
* \[[`6f312b3a91`](https://github.com/nodejs/node/commit/6f312b3a91)] - **http\_parser**: use `MakeCallback` (Trevor Norris) [#5419](https://github.com/nodejs/node/pull/5419)
* \[[`373ffc5bad`](https://github.com/nodejs/node/commit/373ffc5bad)] - **installer**: don't install node\_internals.h (Ben Noordhuis) [#6913](https://github.com/nodejs/node/pull/6913)
* \[[`5782ec2427`](https://github.com/nodejs/node/commit/5782ec2427)] - **module**: don't cache uninitialized builtins (Anna Henningsen) [#6907](https://github.com/nodejs/node/pull/6907)
* \[[`c8e9adb135`](https://github.com/nodejs/node/commit/c8e9adb135)] - **repl**: fix tab completion for defined commands (Prince J Wesley) [#7364](https://github.com/nodejs/node/pull/7364)
* \[[`a3fa5db5ca`](https://github.com/nodejs/node/commit/a3fa5db5ca)] - **(SEMVER-MINOR)** **repl**: copying tabs shouldn't trigger completion (Eugene Obrezkov) [#5958](https://github.com/nodejs/node/pull/5958)
* \[[`d86332799c`](https://github.com/nodejs/node/commit/d86332799c)] - **src**: clean up string\_search (Brian White) [#7174](https://github.com/nodejs/node/pull/7174)
* \[[`3eea55167d`](https://github.com/nodejs/node/commit/3eea55167d)] - **src**: fix memory leak in WriteBuffers() error path (Ben Noordhuis) [#7374](https://github.com/nodejs/node/pull/7374)
* \[[`23797eb037`](https://github.com/nodejs/node/commit/23797eb037)] - **src**: remove obsolete NOLINT comments (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`5aff60e832`](https://github.com/nodejs/node/commit/5aff60e832)] - **src**: lint v8abbr.h (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`42e7c9d266`](https://github.com/nodejs/node/commit/42e7c9d266)] - **src**: lint node\_lttng\_tp.h (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`27c2d25be6`](https://github.com/nodejs/node/commit/27c2d25be6)] - **src**: lint node\_win32\_perfctr\_provider.cc (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`4f4d3e77ef`](https://github.com/nodejs/node/commit/4f4d3e77ef)] - **src**: fix whitespace/indent cpplint warnings (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`066064d65f`](https://github.com/nodejs/node/commit/066064d65f)] - **src**: fix whitespace/blank\_line cpplint warnings (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`44cbe0356d`](https://github.com/nodejs/node/commit/44cbe0356d)] - **src**: fix runtime/references cpplint warnings (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`f530a36c65`](https://github.com/nodejs/node/commit/f530a36c65)] - **src**: fix runtime/int cpplint warnings (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`d6595adcdb`](https://github.com/nodejs/node/commit/d6595adcdb)] - **src**: fix runtime/indentation\_namespace warnings (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`68db091aba`](https://github.com/nodejs/node/commit/68db091aba)] - **src**: fix readability/nolint cpplint warnings (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`4748bed736`](https://github.com/nodejs/node/commit/4748bed736)] - **src**: fix readability/namespace cpplint warnings (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`785211702a`](https://github.com/nodejs/node/commit/785211702a)] - **src**: fix readability/inheritance cpplint warnings (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`c90ae7fb72`](https://github.com/nodejs/node/commit/c90ae7fb72)] - **src**: fix readability/constructors cpplint warnings (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`16f2497994`](https://github.com/nodejs/node/commit/16f2497994)] - **src**: fix readability/braces cpplint warnings (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`c8f78a2682`](https://github.com/nodejs/node/commit/c8f78a2682)] - **src**: fix build/header\_guard cpplint warnings (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`ccc701e1d5`](https://github.com/nodejs/node/commit/ccc701e1d5)] - **src**: fix build/c++tr1 cpplint warnings (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`dda81b44b0`](https://github.com/nodejs/node/commit/dda81b44b0)] - **src**: unify implementations of Utf8Value etc. (Anna Henningsen) [#6357](https://github.com/nodejs/node/pull/6357)
* \[[`db2b23f06f`](https://github.com/nodejs/node/commit/db2b23f06f)] - **src**: fix sporadic deadlock in SIGUSR1 handler (Ben Noordhuis) [#5904](https://github.com/nodejs/node/pull/5904)
* \[[`53a67ed6d7`](https://github.com/nodejs/node/commit/53a67ed6d7)] - **src**: fix bad logic in uid/gid checks (Ben Noordhuis) [#7374](https://github.com/nodejs/node/pull/7374)
* \[[`e6a27a70d8`](https://github.com/nodejs/node/commit/e6a27a70d8)] - **src**: fix use-after-return in zlib bindings (Ben Noordhuis) [#7374](https://github.com/nodejs/node/pull/7374)
* \[[`61de6e9b47`](https://github.com/nodejs/node/commit/61de6e9b47)] - **src**: remove deprecated HMAC\_Init, use HMAC\_Init\_ex (Ben Noordhuis) [#7374](https://github.com/nodejs/node/pull/7374)
* \[[`7305e7b9d2`](https://github.com/nodejs/node/commit/7305e7b9d2)] - **src**: remove duplicate HMAC\_Init calls (Ben Noordhuis) [#7374](https://github.com/nodejs/node/pull/7374)
* \[[`38baf6a0b7`](https://github.com/nodejs/node/commit/38baf6a0b7)] - **src**: remove unused md\_ data members (Ben Noordhuis) [#7374](https://github.com/nodejs/node/pull/7374)
* \[[`e103044b68`](https://github.com/nodejs/node/commit/e103044b68)] - **src**: remove unused data member write\_queue\_size\_ (Ben Noordhuis) [#7374](https://github.com/nodejs/node/pull/7374)
* \[[`67937bca0a`](https://github.com/nodejs/node/commit/67937bca0a)] - **src**: guard against starting fs watcher twice (Ben Noordhuis) [#7374](https://github.com/nodejs/node/pull/7374)
* \[[`c03bd57ac6`](https://github.com/nodejs/node/commit/c03bd57ac6)] - **src**: check uv\_async\_init() return value (Ben Noordhuis) [#7374](https://github.com/nodejs/node/pull/7374)
* \[[`2b0dce5a5b`](https://github.com/nodejs/node/commit/2b0dce5a5b)] - **src**: don't use locale-sensitive strcasecmp() (Ben Noordhuis) [#6582](https://github.com/nodejs/node/pull/6582)
* \[[`9c31c738fc`](https://github.com/nodejs/node/commit/9c31c738fc)] - **src**: remove unused #include statement (Ben Noordhuis) [#6582](https://github.com/nodejs/node/pull/6582)
* \[[`426aa0a5e8`](https://github.com/nodejs/node/commit/426aa0a5e8)] - **src**: fix Windows segfault with `--eval` (Bryce Simonds) [#6938](https://github.com/nodejs/node/pull/6938)
* \[[`b21d145c2a`](https://github.com/nodejs/node/commit/b21d145c2a)] - **(SEMVER-MINOR)** **src**: add node::FreeEnvironment public API (Cheng Zhao) [#3098](https://github.com/nodejs/node/pull/3098)
* \[[`b9136c0c03`](https://github.com/nodejs/node/commit/b9136c0c03)] - **src**: add process.binding('config') (James M Snell) [#6266](https://github.com/nodejs/node/pull/6266)
* \[[`c3d87eee49`](https://github.com/nodejs/node/commit/c3d87eee49)] - **src**: reword command and add ternary (Trevor Norris) [#5756](https://github.com/nodejs/node/pull/5756)
* \[[`68f391bf3b`](https://github.com/nodejs/node/commit/68f391bf3b)] - **src**: remove unnecessary check (Brian White) [#5233](https://github.com/nodejs/node/pull/5233)
* \[[`981bbcd925`](https://github.com/nodejs/node/commit/981bbcd925)] - **src**: remove TryCatch in MakeCallback (Trevor Norris) [#4507](https://github.com/nodejs/node/pull/4507)
* \[[`48b7b71352`](https://github.com/nodejs/node/commit/48b7b71352)] - **src**: remove unused TickInfo::in\_tick() (Trevor Norris) [#4507](https://github.com/nodejs/node/pull/4507)
* \[[`d77b28c6b3`](https://github.com/nodejs/node/commit/d77b28c6b3)] - **src**: remove unused of TickInfo::last\_threw() (Trevor Norris) [#4507](https://github.com/nodejs/node/pull/4507)
* \[[`cb291d5c7f`](https://github.com/nodejs/node/commit/cb291d5c7f)] - **src**: add AsyncCallbackScope (Trevor Norris) [#4507](https://github.com/nodejs/node/pull/4507)
* \[[`2eb097f212`](https://github.com/nodejs/node/commit/2eb097f212)] - **src**: fix MakeCallback error handling (Trevor Norris) [#4507](https://github.com/nodejs/node/pull/4507)
* \[[`63356df39c`](https://github.com/nodejs/node/commit/63356df39c)] - **src,http**: fix uncaughtException miss in http (Trevor Norris) [#5591](https://github.com/nodejs/node/pull/5591)
* \[[`ee7040568d`](https://github.com/nodejs/node/commit/ee7040568d)] - **src,http\_parser**: remove KickNextTick call (Trevor Norris) [#5756](https://github.com/nodejs/node/pull/5756)
* \[[`9a8acad6ff`](https://github.com/nodejs/node/commit/9a8acad6ff)] - **test**: use random ports where possible (Brian White) [#7045](https://github.com/nodejs/node/pull/7045)
* \[[`223c0e2010`](https://github.com/nodejs/node/commit/223c0e2010)] - **test**: skip doctool tests when js-yaml is missing (Anna Henningsen) [#7218](https://github.com/nodejs/node/pull/7218)
* \[[`3681b9b868`](https://github.com/nodejs/node/commit/3681b9b868)] - **test**: refactor doctool tests (Rich Trott) [#6719](https://github.com/nodejs/node/pull/6719)
* \[[`686d7b329c`](https://github.com/nodejs/node/commit/686d7b329c)] - **test**: build addons with V8\_DEPRECATION\_WARNINGS=1 (Ben Noordhuis) [#6652](https://github.com/nodejs/node/pull/6652)
* \[[`8404e34665`](https://github.com/nodejs/node/commit/8404e34665)] - _**Revert**_ "**test**: mark test-vm-timeout flaky on windows" (Anna Henningsen) [#7373](https://github.com/nodejs/node/pull/7373)
* \[[`eab9ced2ee`](https://github.com/nodejs/node/commit/eab9ced2ee)] - **test**: fix flaky test-vm-timeout (Anna Henningsen) [#7373](https://github.com/nodejs/node/pull/7373)
* \[[`a31d3161f5`](https://github.com/nodejs/node/commit/a31d3161f5)] - **test**: add test for exec() known issue (Rich Trott) [#7375](https://github.com/nodejs/node/pull/7375)
* \[[`1baa145a16`](https://github.com/nodejs/node/commit/1baa145a16)] - **test**: remove internet/test-tls-connnect-cnnic (Ben Noordhuis) [#7363](https://github.com/nodejs/node/pull/7363)
* \[[`e3097b7cdf`](https://github.com/nodejs/node/commit/e3097b7cdf)] - **test**: test isFullWidthCodePoint with invalid input (Rich Trott) [#7422](https://github.com/nodejs/node/pull/7422)
* \[[`f0b0fc49f9`](https://github.com/nodejs/node/commit/f0b0fc49f9)] - **test**: update weak module for gc tests (Rich Trott) [#7014](https://github.com/nodejs/node/pull/7014)
* \[[`1d100f6853`](https://github.com/nodejs/node/commit/1d100f6853)] - **test**: remove unused vars from http/https tests (Rich Trott) [#7598](https://github.com/nodejs/node/pull/7598)
* \[[`3241536d95`](https://github.com/nodejs/node/commit/3241536d95)] - **test**: remove unused var in net-server-try-ports (Rich Trott) [#7597](https://github.com/nodejs/node/pull/7597)
* \[[`7bd7c235fa`](https://github.com/nodejs/node/commit/7bd7c235fa)] - **test**: remove unused var from stream2 test (Rich Trott) [#7596](https://github.com/nodejs/node/pull/7596)
* \[[`4d36a67738`](https://github.com/nodejs/node/commit/4d36a67738)] - **test**: remove unused var from child-process-fork (Rich Trott) [#7599](https://github.com/nodejs/node/pull/7599)
* \[[`b5e516a42c`](https://github.com/nodejs/node/commit/b5e516a42c)] - **test**: remove unused var in test-tls-server-verify (Rich Trott) [#7595](https://github.com/nodejs/node/pull/7595)
* \[[`db35efa6c1`](https://github.com/nodejs/node/commit/db35efa6c1)] - **test**: fix flaky test-net-write-slow (Rich Trott) [#7555](https://github.com/nodejs/node/pull/7555)
* \[[`8273824ca3`](https://github.com/nodejs/node/commit/8273824ca3)] - **test**: remove common.PORT from http tests (Rich Trott) [#7467](https://github.com/nodejs/node/pull/7467)
* \[[`5129f3f2cd`](https://github.com/nodejs/node/commit/5129f3f2cd)] - **test**: mark test-vm-timeout flaky on windows (Rich Trott) [#7359](https://github.com/nodejs/node/pull/7359)
* \[[`79b45886c1`](https://github.com/nodejs/node/commit/79b45886c1)] - **test**: add tests for some stream.Readable uses (Anna Henningsen) [#7260](https://github.com/nodejs/node/pull/7260)
* \[[`65b5cccee9`](https://github.com/nodejs/node/commit/65b5cccee9)] - **test**: fix spawn on windows (Brian White) [#7049](https://github.com/nodejs/node/pull/7049)
* \[[`96ed883d2f`](https://github.com/nodejs/node/commit/96ed883d2f)] - **test**: enable test-debug-brk-no-arg (Rich Trott) [#7143](https://github.com/nodejs/node/pull/7143)
* \[[`8724c442f3`](https://github.com/nodejs/node/commit/8724c442f3)] - **test**: add test for uid/gid setting in spawn (Rich Trott) [#7084](https://github.com/nodejs/node/pull/7084)
* \[[`042e858dfb`](https://github.com/nodejs/node/commit/042e858dfb)] - **test**: make test-child-process-fork-net more robust (Rich Trott) [#7033](https://github.com/nodejs/node/pull/7033)
* \[[`2a59e4e73d`](https://github.com/nodejs/node/commit/2a59e4e73d)] - **test**: improve debug-break-on-uncaught reliability (Rich Trott) [#6793](https://github.com/nodejs/node/pull/6793)
* \[[`77325d585e`](https://github.com/nodejs/node/commit/77325d585e)] - **test**: remove disabled eio race test (Rich Trott) [#7083](https://github.com/nodejs/node/pull/7083)
* \[[`5b1f54678b`](https://github.com/nodejs/node/commit/5b1f54678b)] - **test**: remove non-incremental common.PORT changes (Rich Trott) [#7055](https://github.com/nodejs/node/pull/7055)
* \[[`44228dfdef`](https://github.com/nodejs/node/commit/44228dfdef)] - **test**: remove `common.PORT` from gc tests (Rich Trott) [#7013](https://github.com/nodejs/node/pull/7013)
* \[[`644bfe14a6`](https://github.com/nodejs/node/commit/644bfe14a6)] - **test**: fix test-debug-port-numbers on OS X (Santiago Gimeno) [#7046](https://github.com/nodejs/node/pull/7046)
* \[[`cde3014f78`](https://github.com/nodejs/node/commit/cde3014f78)] - **test**: remove modifcation to common.PORT (Rich Trott) [#6990](https://github.com/nodejs/node/pull/6990)
* \[[`8c412af7ac`](https://github.com/nodejs/node/commit/8c412af7ac)] - **test**: verify cluster worker exit (cjihrig) [#6993](https://github.com/nodejs/node/pull/6993)
* \[[`7d6acefbcc`](https://github.com/nodejs/node/commit/7d6acefbcc)] - **test**: listen on and connect to 127.0.0.1 (Ben Noordhuis) [#7524](https://github.com/nodejs/node/pull/7524)
* \[[`ecf5c1cb25`](https://github.com/nodejs/node/commit/ecf5c1cb25)] - **test**: refactor spawnSync() cwd test (cjihrig) [#6939](https://github.com/nodejs/node/pull/6939)
* \[[`9cccaa3c80`](https://github.com/nodejs/node/commit/9cccaa3c80)] - **test**: fix component printing on windows (Ben Noordhuis) [#6915](https://github.com/nodejs/node/pull/6915)
* \[[`af4b56d6be`](https://github.com/nodejs/node/commit/af4b56d6be)] - **test**: pass python path to node-gyp (hefangshi) [#6646](https://github.com/nodejs/node/pull/6646)
* \[[`7c55f59214`](https://github.com/nodejs/node/commit/7c55f59214)] - **test**: make stdout buffer test more robust (Rich Trott) [#6633](https://github.com/nodejs/node/pull/6633)
* \[[`3aef9b813f`](https://github.com/nodejs/node/commit/3aef9b813f)] - **test**: unmark test-http-regr-gh-2928 as flaky (Rich Trott) [#6540](https://github.com/nodejs/node/pull/6540)
* \[[`2259e5db69`](https://github.com/nodejs/node/commit/2259e5db69)] - **test**: avoid test-cluster-master-\* flakiness (Stefan Budeanu) [#6531](https://github.com/nodejs/node/pull/6531)
* \[[`5f444ed6a3`](https://github.com/nodejs/node/commit/5f444ed6a3)] - **test**: add tests for stream3 buffering using cork (Alex J Burke) [#6493](https://github.com/nodejs/node/pull/6493)
* \[[`01b314d165`](https://github.com/nodejs/node/commit/01b314d165)] - **test**: test TTY problems by fakeing a TTY using openpty (Jeremiah Senkpiel) [#6895](https://github.com/nodejs/node/pull/6895)
* \[[`55f8689711`](https://github.com/nodejs/node/commit/55f8689711)] - **test**: add test for responses to HTTP CONNECT req (Josh Leder) [#6279](https://github.com/nodejs/node/pull/6279)
* \[[`9aec1ddb4f`](https://github.com/nodejs/node/commit/9aec1ddb4f)] - **test**: test cluster worker disconnection on error (Santiago Gimeno) [#6909](https://github.com/nodejs/node/pull/6909)
* \[[`c0a42bc040`](https://github.com/nodejs/node/commit/c0a42bc040)] - **test**: verify IPC messages are emitted on next tick (Santiago Gimeno) [#6909](https://github.com/nodejs/node/pull/6909)
* \[[`9606f768ea`](https://github.com/nodejs/node/commit/9606f768ea)] - **(SEMVER-MINOR)** **test**: run v8 tests from node tree (Bryon Leung) [#4704](https://github.com/nodejs/node/pull/4704)
* \[[`efdeb69c9a`](https://github.com/nodejs/node/commit/efdeb69c9a)] - **test**: work around debugger not killing inferior (Ben Noordhuis) [#7037](https://github.com/nodejs/node/pull/7037)
* \[[`e3f9bc893f`](https://github.com/nodejs/node/commit/e3f9bc893f)] - **test**: use strictEqual consistently in agent test (Ben Noordhuis) [#6654](https://github.com/nodejs/node/pull/6654)
* \[[`1186b7a401`](https://github.com/nodejs/node/commit/1186b7a401)] - **test**: add addons test for MakeCallback (Trevor Norris) [#4507](https://github.com/nodejs/node/pull/4507)
* \[[`8f76d7db03`](https://github.com/nodejs/node/commit/8f76d7db03)] - **test,tools**: test yaml parsing of doctool (Anna Henningsen) [#6495](https://github.com/nodejs/node/pull/6495)
* \[[`e544b1c40c`](https://github.com/nodejs/node/commit/e544b1c40c)] - **test,win**: skip addons/load-long-path on WOW64 (Alexis Campailla) [#6675](https://github.com/nodejs/node/pull/6675)
* \[[`b956635e41`](https://github.com/nodejs/node/commit/b956635e41)] - **tls**: catch `certCbDone` exceptions (Fedor Indutny) [#6887](https://github.com/nodejs/node/pull/6887)
* \[[`06327e5eed`](https://github.com/nodejs/node/commit/06327e5eed)] - **tls**: use process.binding('config') to detect fips mode (James M Snell) [#7551](https://github.com/nodejs/node/pull/7551)
* \[[`c807287e80`](https://github.com/nodejs/node/commit/c807287e80)] - **tls,https**: respect address family when connecting (Ben Noordhuis) [#6654](https://github.com/nodejs/node/pull/6654)
* \[[`9ef6e23088`](https://github.com/nodejs/node/commit/9ef6e23088)] - **tools**: make sure doctool anchors respect includes (Anna Henningsen) [#6943](https://github.com/nodejs/node/pull/6943)
* \[[`f9f85a006f`](https://github.com/nodejs/node/commit/f9f85a006f)] - **tools**: restore change of signatures to opts hashes (Jesse McCarthy) [#6690](https://github.com/nodejs/node/pull/6690)
* \[[`607173bbac`](https://github.com/nodejs/node/commit/607173bbac)] - **tools**: fix regression in doctool (Myles Borins) [#6680](https://github.com/nodejs/node/pull/6680)
* \[[`ed193ad8ae`](https://github.com/nodejs/node/commit/ed193ad8ae)] - **tools**: fix tools/doc/addon-verify.js regression (Anna Henningsen) [#6652](https://github.com/nodejs/node/pull/6652)
* \[[`8b88c384f0`](https://github.com/nodejs/node/commit/8b88c384f0)] - **tools**: lint for object literal spacing (Rich Trott) [#6592](https://github.com/nodejs/node/pull/6592)
* \[[`96b5aa8710`](https://github.com/nodejs/node/commit/96b5aa8710)] - **tools**: update marked dependency (Daniel Wang) [#6396](https://github.com/nodejs/node/pull/6396)
* \[[`ea137637b7`](https://github.com/nodejs/node/commit/ea137637b7)] - **tools**: allow multiple added: version entries (Anna Henningsen) [#6495](https://github.com/nodejs/node/pull/6495)
* \[[`2832a60426`](https://github.com/nodejs/node/commit/2832a60426)] - **tools**: parse documentation metadata (Tristian Flanagan) [#6495](https://github.com/nodejs/node/pull/6495)
* \[[`0149cb0577`](https://github.com/nodejs/node/commit/0149cb0577)] - **tools**: add mock-y js-yaml dependency to doctool (Anna Henningsen) [#6495](https://github.com/nodejs/node/pull/6495)
* \[[`68e9fd47c6`](https://github.com/nodejs/node/commit/68e9fd47c6)] - **tools**: fix -Wunused-variable warning (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`4a2bd2d515`](https://github.com/nodejs/node/commit/4a2bd2d515)] - **tools**: allow cpplint to run outside git repo (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`09e98a4457`](https://github.com/nodejs/node/commit/09e98a4457)] - **tools**: add back --mode=tap to cpplint (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`e74f199fe2`](https://github.com/nodejs/node/commit/e74f199fe2)] - **tools**: disable unwanted cpplint rules again (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`391fc80487`](https://github.com/nodejs/node/commit/391fc80487)] - **tools**: update cpplint to r456 (Ben Noordhuis) [#7462](https://github.com/nodejs/node/pull/7462)
* \[[`efadf7639f`](https://github.com/nodejs/node/commit/efadf7639f)] - **tools**: update certdata.txt (Ben Noordhuis) [#7363](https://github.com/nodejs/node/pull/7363)
* \[[`d7ce99214d`](https://github.com/nodejs/node/commit/d7ce99214d)] - **tools**: update ESLint, fix unused vars bug (Rich Trott) [#7601](https://github.com/nodejs/node/pull/7601)
* \[[`242d6c7323`](https://github.com/nodejs/node/commit/242d6c7323)] - **tools**: remove unused variable (Rich Trott) [#7594](https://github.com/nodejs/node/pull/7594)
* \[[`7182f5f876`](https://github.com/nodejs/node/commit/7182f5f876)] - **tools**: fix license builder to work with icu-small (Myles Borins) [#7119](https://github.com/nodejs/node/pull/7119)
* \[[`140b84dd7d`](https://github.com/nodejs/node/commit/140b84dd7d)] - **tools**: print stderr on bad test.py `vmArch` check (Jeremiah Senkpiel) [#6786](https://github.com/nodejs/node/pull/6786)
* \[[`4c423e649c`](https://github.com/nodejs/node/commit/4c423e649c)] - **tools**: explicit path for V8 test tap output (Myles Borins) [#7460](https://github.com/nodejs/node/pull/7460)
* \[[`d50f16969d`](https://github.com/nodejs/node/commit/d50f16969d)] - **tools,doc**: add example usage for REPLACEME tag (Anna Henningsen) [#6864](https://github.com/nodejs/node/pull/6864)
* \[[`b07c3a6ea6`](https://github.com/nodejs/node/commit/b07c3a6ea6)] - **tty**: use blocking mode on OS X (Jeremiah Senkpiel) [#6895](https://github.com/nodejs/node/pull/6895)
* \[[`a1719a94e9`](https://github.com/nodejs/node/commit/a1719a94e9)] - **udp**: use libuv API to get file descriptor (Saúl Ibarra Corretgé) [#6908](https://github.com/nodejs/node/pull/6908)
* \[[`7779639a11`](https://github.com/nodejs/node/commit/7779639a11)] - **unix,stream**: fix getting the correct fd for a handle (Saúl Ibarra Corretgé) [#6753](https://github.com/nodejs/node/pull/6753)
* \[[`d0bf09d3ad`](https://github.com/nodejs/node/commit/d0bf09d3ad)] - **util**: improve format() performance further (Brian White) [#5360](https://github.com/nodejs/node/pull/5360)
* \[[`72fb281961`](https://github.com/nodejs/node/commit/72fb281961)] - **util**: improve util.format performance (Evan Lucas) [#5360](https://github.com/nodejs/node/pull/5360)
* \[[`855759757a`](https://github.com/nodejs/node/commit/855759757a)] - **vm**: don't print out arrow message for custom error (Anna Henningsen) [#7398](https://github.com/nodejs/node/pull/7398)
* \[[`b9dfdfe1d3`](https://github.com/nodejs/node/commit/b9dfdfe1d3)] - **vm**: don't abort process when stack space runs out (Anna Henningsen) [#6907](https://github.com/nodejs/node/pull/6907)
* \[[`0bfedd13a9`](https://github.com/nodejs/node/commit/0bfedd13a9)] - **win,build**: add creation of zip and 7z package (Bartosz Sosnowski) [#5995](https://github.com/nodejs/node/pull/5995)
* \[[`7d66752f1f`](https://github.com/nodejs/node/commit/7d66752f1f)] - **zlib**: release callback and buffer after processing (Matt Lavin) [#6955](https://github.com/nodejs/node/pull/6955)

<a id="4.4.7"></a>

## 2016-06-28, Version 4.4.7 'Argon' (LTS), @thealphanerd

This LTS release comes with 89 commits. This includes 46 commits that are docs related, 11 commits that are test related, 8 commits that are build related, and 4 commits that are benchmark related.

### Notable Changes

* **debugger**:
  * All properties of an array (aside from length) can now be printed in the repl (cjihrig) [#6448](https://github.com/nodejs/node/pull/6448)
* **npm**:
  * Upgrade npm to 2.15.8 (Rebecca Turner) [#7412](https://github.com/nodejs/node/pull/7412)
* **stream**:
  * Fix for a bug that became more prevalent with the stream changes that landed in v4.4.5. (Anna Henningsen) [#7160](https://github.com/nodejs/node/pull/7160)
* **V8**:
  * Fix for a bug in crankshaft that was causing crashes on arm64 (Myles Borins) [#7442](https://github.com/nodejs/node/pull/7442)
  * Add missing classes to postmortem info such as JSMap and JSSet (evan.lucas) [#3792](https://github.com/nodejs/node/pull/3792)

### Commits

* \[[`87cdb83a96`](https://github.com/nodejs/node/commit/87cdb83a96)] - **benchmark**: merge url.js with url-resolve.js (Andreas Madsen) [#5177](https://github.com/nodejs/node/pull/5177)
* \[[`921e8568d5`](https://github.com/nodejs/node/commit/921e8568d5)] - **benchmark**: move misc to categorized directories (Andreas Madsen) [#5177](https://github.com/nodejs/node/pull/5177)
* \[[`c189eec14e`](https://github.com/nodejs/node/commit/c189eec14e)] - **benchmark**: fix configuation parameters (Andreas Madsen) [#5177](https://github.com/nodejs/node/pull/5177)
* \[[`58ad451f0b`](https://github.com/nodejs/node/commit/58ad451f0b)] - **benchmark**: move string-decoder to its own category (Andreas Madsen) [#5177](https://github.com/nodejs/node/pull/5177)
* \[[`a01caa3166`](https://github.com/nodejs/node/commit/a01caa3166)] - **build**: don't compile with -B, redux (Ben Noordhuis) [#6650](https://github.com/nodejs/node/pull/6650)
* \[[`37606caeaf`](https://github.com/nodejs/node/commit/37606caeaf)] - **build**: don't compile with -B (Ben Noordhuis) [#6393](https://github.com/nodejs/node/pull/6393)
* \[[`64fb7a1929`](https://github.com/nodejs/node/commit/64fb7a1929)] - **build**: update android-configure script for npm (Robert Chiras) [#6349](https://github.com/nodejs/node/pull/6349)
* \[[`43ce6fc8d2`](https://github.com/nodejs/node/commit/43ce6fc8d2)] - **build**: fix DESTCPU detection for binary target (Richard Lau) [#6310](https://github.com/nodejs/node/pull/6310)
* \[[`6dfe7aeed5`](https://github.com/nodejs/node/commit/6dfe7aeed5)] - **cares**: Support malloc(0) scenarios for AIX (Gireesh Punathil) [#6305](https://github.com/nodejs/node/pull/6305)
* \[[`2389006720`](https://github.com/nodejs/node/commit/2389006720)] - **debugger**: display array contents in repl (cjihrig) [#6448](https://github.com/nodejs/node/pull/6448)
* \[[`1c6809ce75`](https://github.com/nodejs/node/commit/1c6809ce75)] - **debugger**: introduce exec method for debugger (Jackson Tian)
* \[[`200b3ca9ed`](https://github.com/nodejs/node/commit/200b3ca9ed)] - **deps**: upgrade npm in LTS to 2.15.8 (Rebecca Turner) [#7412](https://github.com/nodejs/node/pull/7412)
* \[[`49921e8819`](https://github.com/nodejs/node/commit/49921e8819)] - **deps**: backport 102e3e87e7 from V8 upstream (Myles Borins) [#7442](https://github.com/nodejs/node/pull/7442)
* \[[`de00f91041`](https://github.com/nodejs/node/commit/de00f91041)] - **deps**: backport bc2e393 from v8 upstream (evan.lucas) [#3792](https://github.com/nodejs/node/pull/3792)
* \[[`1549899531`](https://github.com/nodejs/node/commit/1549899531)] - **dgram,test**: add addMembership/dropMembership tests (Rich Trott) [#6753](https://github.com/nodejs/node/pull/6753)
* \[[`0ba3c2ca66`](https://github.com/nodejs/node/commit/0ba3c2ca66)] - **doc**: fix layout problem in v4 changelog (Myles Borins) [#7394](https://github.com/nodejs/node/pull/7394)
* \[[`98469ad84d`](https://github.com/nodejs/node/commit/98469ad84d)] - **doc**: correct args for cluster message event (Colin Ihrig) [#7297](https://github.com/nodejs/node/pull/7297)
* \[[`67863f110b`](https://github.com/nodejs/node/commit/67863f110b)] - **doc**: update licenses (Myles Borins) [#7127](https://github.com/nodejs/node/pull/7127)
* \[[`c31eaad42d`](https://github.com/nodejs/node/commit/c31eaad42d)] - **doc**: clarify buffer class (Steve Mao) [#6914](https://github.com/nodejs/node/pull/6914)
* \[[`e0dd476fe5`](https://github.com/nodejs/node/commit/e0dd476fe5)] - **doc**: fix typos in timers topic to aid readability (Kevin Donahue) [#6916](https://github.com/nodejs/node/pull/6916)
* \[[`a8391bc9fc`](https://github.com/nodejs/node/commit/a8391bc9fc)] - **doc**: add jhamhader to collaborators (Yuval Brik) [#6946](https://github.com/nodejs/node/pull/6946)
* \[[`22ca7b877b`](https://github.com/nodejs/node/commit/22ca7b877b)] - **doc**: add @othiym23 to list of collaborators (Forrest L Norvell) [#6945](https://github.com/nodejs/node/pull/6945)
* \[[`2c3c4e5819`](https://github.com/nodejs/node/commit/2c3c4e5819)] - **doc**: reference list of language-specific globals (Anna Henningsen) [#6900](https://github.com/nodejs/node/pull/6900)
* \[[`5a1a0b5ed1`](https://github.com/nodejs/node/commit/5a1a0b5ed1)] - **doc**: make the api doc print-friendly (Marian) [#6748](https://github.com/nodejs/node/pull/6748)
* \[[`03db88e012`](https://github.com/nodejs/node/commit/03db88e012)] - **doc**: add bengl to collaborators (Bryan English) [#6921](https://github.com/nodejs/node/pull/6921)
* \[[`fbf95dde94`](https://github.com/nodejs/node/commit/fbf95dde94)] - **doc**: Update DCO to v1.1 (William Kapke) [#6353](https://github.com/nodejs/node/pull/6353)
* \[[`f23a9c39c0`](https://github.com/nodejs/node/commit/f23a9c39c0)] - **doc**: fix typo in Error.captureStackTrace (Mohsen) [#6811](https://github.com/nodejs/node/pull/6811)
* \[[`30ab6a890c`](https://github.com/nodejs/node/commit/30ab6a890c)] - **doc**: fix name to match git log (Robert Jefe Lindstaedt) [#6880](https://github.com/nodejs/node/pull/6880)
* \[[`2b0f40ca16`](https://github.com/nodejs/node/commit/2b0f40ca16)] - **doc**: add note for fs.watch virtualized env (Robert Jefe Lindstaedt) [#6809](https://github.com/nodejs/node/pull/6809)
* \[[`3b461870be`](https://github.com/nodejs/node/commit/3b461870be)] - **doc**: Backport ee.once doc clarifications to 4.x. (Lance Ball) [#7103](https://github.com/nodejs/node/pull/7103)
* \[[`eadb7e5b20`](https://github.com/nodejs/node/commit/eadb7e5b20)] - **doc**: subdivide TOC, add auxiliary links (Jeremiah Senkpiel) [#6167](https://github.com/nodejs/node/pull/6167)
* \[[`107839c5dd`](https://github.com/nodejs/node/commit/107839c5dd)] - **doc**: no Node.js(1) (Jeremiah Senkpiel) [#6167](https://github.com/nodejs/node/pull/6167)
* \[[`401325f9e2`](https://github.com/nodejs/node/commit/401325f9e2)] - **doc**: better example & synopsis (Jeremiah Senkpiel) [#6167](https://github.com/nodejs/node/pull/6167)
* \[[`c654184f28`](https://github.com/nodejs/node/commit/c654184f28)] - **doc**: remove link to Sign in crypto.md (Kirill Fomichev) [#6812](https://github.com/nodejs/node/pull/6812)
* \[[`3e9288e466`](https://github.com/nodejs/node/commit/3e9288e466)] - **doc**: fix exec example in child\_process (Evan Lucas) [#6660](https://github.com/nodejs/node/pull/6660)
* \[[`3d820e45b4`](https://github.com/nodejs/node/commit/3d820e45b4)] - **doc**: "a" -> "an" in api/documentation.md (Anchika Agarwal) [#6689](https://github.com/nodejs/node/pull/6689)
* \[[`352496daa2`](https://github.com/nodejs/node/commit/352496daa2)] - **doc**: move the readme newcomers section (Jeremiah Senkpiel) [#6681](https://github.com/nodejs/node/pull/6681)
* \[[`ac6b921ce5`](https://github.com/nodejs/node/commit/ac6b921ce5)] - **doc**: mention existence/purpose of module wrapper (Matt Harrison) [#6433](https://github.com/nodejs/node/pull/6433)
* \[[`97d1fc0fc6`](https://github.com/nodejs/node/commit/97d1fc0fc6)] - **doc**: improve onboarding-extras.md formatting (Jeremiah Senkpiel) [#6548](https://github.com/nodejs/node/pull/6548)
* \[[`c9b144ddd4`](https://github.com/nodejs/node/commit/c9b144ddd4)] - **doc**: linkify remaining references to fs.Stats object (Kevin Donahue) [#6485](https://github.com/nodejs/node/pull/6485)
* \[[`d909c25a33`](https://github.com/nodejs/node/commit/d909c25a33)] - **doc**: fix the lint of an example in cluster.md (yorkie) [#6516](https://github.com/nodejs/node/pull/6516)
* \[[`21d02f460f`](https://github.com/nodejs/node/commit/21d02f460f)] - **doc**: add missing underscore for markdown italics (Kevin Donahue) [#6529](https://github.com/nodejs/node/pull/6529)
* \[[`18ecc779bb`](https://github.com/nodejs/node/commit/18ecc779bb)] - **doc**: ensure consistent grammar in node.1 file (justshiv) [#6426](https://github.com/nodejs/node/pull/6426)
* \[[`52d9e7b61d`](https://github.com/nodejs/node/commit/52d9e7b61d)] - **doc**: fix a typo in \_\_dirname section (William Luo) [#6473](https://github.com/nodejs/node/pull/6473)
* \[[`de20235235`](https://github.com/nodejs/node/commit/de20235235)] - **doc**: remove all scrollbar styling (Claudio Rodriguez) [#6479](https://github.com/nodejs/node/pull/6479)
* \[[`a6f45b4eda`](https://github.com/nodejs/node/commit/a6f45b4eda)] - **doc**: Remove extra space in REPL example (Juan) [#6447](https://github.com/nodejs/node/pull/6447)
* \[[`feda15b2b8`](https://github.com/nodejs/node/commit/feda15b2b8)] - **doc**: update build instructions for OS X (Rich Trott) [#6309](https://github.com/nodejs/node/pull/6309)
* \[[`3d1a3e4a30`](https://github.com/nodejs/node/commit/3d1a3e4a30)] - **doc**: change references to Stable to Current (Myles Borins) [#6318](https://github.com/nodejs/node/pull/6318)
* \[[`e28598b1ef`](https://github.com/nodejs/node/commit/e28598b1ef)] - **doc**: update authors (James M Snell) [#6373](https://github.com/nodejs/node/pull/6373)
* \[[`0f3a94acbd`](https://github.com/nodejs/node/commit/0f3a94acbd)] - **doc**: add JacksonTian to collaborators (Jackson Tian) [#6388](https://github.com/nodejs/node/pull/6388)
* \[[`d7d54c8fd2`](https://github.com/nodejs/node/commit/d7d54c8fd2)] - **doc**: add Minqi Pan to collaborators (Minqi Pan) [#6387](https://github.com/nodejs/node/pull/6387)
* \[[`83721c6fd2`](https://github.com/nodejs/node/commit/83721c6fd2)] - **doc**: add eljefedelrodeodeljefe to collaborators (Robert Jefe Lindstaedt) [#6389](https://github.com/nodejs/node/pull/6389)
* \[[`b112fd1b4e`](https://github.com/nodejs/node/commit/b112fd1b4e)] - **doc**: add ronkorving to collaborators (ronkorving) [#6385](https://github.com/nodejs/node/pull/6385)
* \[[`ac60d9cc86`](https://github.com/nodejs/node/commit/ac60d9cc86)] - **doc**: add estliberitas to collaborators (Alexander Makarenko) [#6386](https://github.com/nodejs/node/pull/6386)
* \[[`435cd56de5`](https://github.com/nodejs/node/commit/435cd56de5)] - **doc**: DCO anchor that doesn't change (William Kapke) [#6257](https://github.com/nodejs/node/pull/6257)
* \[[`7d8141dd1b`](https://github.com/nodejs/node/commit/7d8141dd1b)] - **doc**: add stefanmb to collaborators (Stefan Budeanu) [#6227](https://github.com/nodejs/node/pull/6227)
* \[[`6dfc96326d`](https://github.com/nodejs/node/commit/6dfc96326d)] - **doc**: add iWuzHere to collaborators (Imran Iqbal) [#6226](https://github.com/nodejs/node/pull/6226)
* \[[`3dbcc73159`](https://github.com/nodejs/node/commit/3dbcc73159)] - **doc**: add santigimeno to collaborators (Santiago Gimeno) [#6225](https://github.com/nodejs/node/pull/6225)
* \[[`ae3eb24a3d`](https://github.com/nodejs/node/commit/ae3eb24a3d)] - **doc**: add addaleax to collaborators (Anna Henningsen) [#6224](https://github.com/nodejs/node/pull/6224)
* \[[`46ee7bb4ba`](https://github.com/nodejs/node/commit/46ee7bb4ba)] - **doc**: fix incorrect references in buffer docs (Amery) [#6194](https://github.com/nodejs/node/pull/6194)
* \[[`e3f78eb7c1`](https://github.com/nodejs/node/commit/e3f78eb7c1)] - **doc**: improve rendering of v4.4.5 changelog entry (Myles Borins) [#6958](https://github.com/nodejs/node/pull/6958)
* \[[`bac87d01d9`](https://github.com/nodejs/node/commit/bac87d01d9)] - **gitignore**: adding .vs/ directory to .gitignore (Mike Kaufman) [#6070](https://github.com/nodejs/node/pull/6070)
* \[[`93f2314dc2`](https://github.com/nodejs/node/commit/93f2314dc2)] - **gitignore**: ignore VS 2015 \*.VC.opendb files (Mike Kaufman) [#6070](https://github.com/nodejs/node/pull/6070)
* \[[`c98aaf59bf`](https://github.com/nodejs/node/commit/c98aaf59bf)] - **http**: speed up checkIsHttpToken (Jackson Tian) [#4790](https://github.com/nodejs/node/pull/4790)
* \[[`552e25cb6b`](https://github.com/nodejs/node/commit/552e25cb6b)] - **lib,test**: update in preparation for linter update (Rich Trott) [#6498](https://github.com/nodejs/node/pull/6498)
* \[[`aaeeec4765`](https://github.com/nodejs/node/commit/aaeeec4765)] - **lib,test,tools**: alignment on variable assignments (Rich Trott) [#6869](https://github.com/nodejs/node/pull/6869)
* \[[`b3acbc5648`](https://github.com/nodejs/node/commit/b3acbc5648)] - **net**: replace `__defineGetter__` with defineProperty (Fedor Indutny) [#6284](https://github.com/nodejs/node/pull/6284)
* \[[`4c1eb5bf03`](https://github.com/nodejs/node/commit/4c1eb5bf03)] - **repl**: create history file with mode 0600 (Carl Lei) [#3394](https://github.com/nodejs/node/pull/3394)
* \[[`90306bb81d`](https://github.com/nodejs/node/commit/90306bb81d)] - **src**: use size\_t for http parser array size fields (Ben Noordhuis) [#5969](https://github.com/nodejs/node/pull/5969)
* \[[`af41a63d0f`](https://github.com/nodejs/node/commit/af41a63d0f)] - **src**: replace ARRAY\_SIZE with typesafe arraysize (Ben Noordhuis) [#5969](https://github.com/nodejs/node/pull/5969)
* \[[`037291e31f`](https://github.com/nodejs/node/commit/037291e31f)] - **src**: make sure Utf8Value always zero-terminates (Anna Henningsen) [#7101](https://github.com/nodejs/node/pull/7101)
* \[[`a08a0179e9`](https://github.com/nodejs/node/commit/a08a0179e9)] - **stream**: ensure awaitDrain is increased once (David Halls) [#7292](https://github.com/nodejs/node/pull/7292)
* \[[`b73ec46dcb`](https://github.com/nodejs/node/commit/b73ec46dcb)] - **stream**: reset awaitDrain after manual .resume() (Anna Henningsen) [#7160](https://github.com/nodejs/node/pull/7160)
* \[[`55319fe798`](https://github.com/nodejs/node/commit/55319fe798)] - **stream\_base**: expose `bytesRead` getter (Fedor Indutny) [#6284](https://github.com/nodejs/node/pull/6284)
* \[[`0414d882ce`](https://github.com/nodejs/node/commit/0414d882ce)] - **test**: fix test-net-\* error code check for getaddrinfo(3) (Natanael Copa) [#5099](https://github.com/nodejs/node/pull/5099)
* \[[`be0bb5f5fc`](https://github.com/nodejs/node/commit/be0bb5f5fc)] - **test**: fix unreliable known\_issues test (Rich Trott) [#6555](https://github.com/nodejs/node/pull/6555)
* \[[`ab50e82f42`](https://github.com/nodejs/node/commit/ab50e82f42)] - **test**: fix test-process-exec-argv flakiness (Santiago Gimeno) [#7128](https://github.com/nodejs/node/pull/7128)
* \[[`4e38655d5f`](https://github.com/nodejs/node/commit/4e38655d5f)] - **test**: refactor test-tls-reuse-host-from-socket (Rich Trott) [#6756](https://github.com/nodejs/node/pull/6756)
* \[[`1c4549a31e`](https://github.com/nodejs/node/commit/1c4549a31e)] - **test**: fix flaky test-stdout-close-catch (Santiago Gimeno) [#6808](https://github.com/nodejs/node/pull/6808)
* \[[`3b94e31245`](https://github.com/nodejs/node/commit/3b94e31245)] - **test**: robust handling of env for npm-test-install (Myles Borins) [#6797](https://github.com/nodejs/node/pull/6797)
* \[[`4067cde7ee`](https://github.com/nodejs/node/commit/4067cde7ee)] - **test**: abstract skip functionality to common (Jeremiah Senkpiel) [#7114](https://github.com/nodejs/node/pull/7114)
* \[[`8b396e3d71`](https://github.com/nodejs/node/commit/8b396e3d71)] - **test**: fix test-debugger-repl-break-in-module (Rich Trott) [#6686](https://github.com/nodejs/node/pull/6686)
* \[[`847b29c050`](https://github.com/nodejs/node/commit/847b29c050)] - **test**: fix test-debugger-repl-term (Rich Trott) [#6682](https://github.com/nodejs/node/pull/6682)
* \[[`1d68bdbe3f`](https://github.com/nodejs/node/commit/1d68bdbe3f)] - **test**: fix error message checks in test-module-loading (James M Snell) [#5986](https://github.com/nodejs/node/pull/5986)
* \[[`7e739ae159`](https://github.com/nodejs/node/commit/7e739ae159)] - **test,tools**: adjust function argument alignment (Rich Trott) [#7100](https://github.com/nodejs/node/pull/7100)
* \[[`216486c2b6`](https://github.com/nodejs/node/commit/216486c2b6)] - **tools**: lint for function argument alignment (Rich Trott) [#7100](https://github.com/nodejs/node/pull/7100)
* \[[`6a76485ad7`](https://github.com/nodejs/node/commit/6a76485ad7)] - **tools**: update ESLint to 2.9.0 (Rich Trott) [#6498](https://github.com/nodejs/node/pull/6498)
* \[[`a31153c02c`](https://github.com/nodejs/node/commit/a31153c02c)] - **tools**: remove the minifying logic (Sakthipriyan Vairamani) [#6636](https://github.com/nodejs/node/pull/6636)
* \[[`10bd1a73fd`](https://github.com/nodejs/node/commit/10bd1a73fd)] - **tools**: fix license-builder.sh again for ICU (Steven R. Loomis) [#6068](https://github.com/nodejs/node/pull/6068)
* \[[`0f6146c6c0`](https://github.com/nodejs/node/commit/0f6146c6c0)] - **tools**: add tests for the doctool (Ian Kronquist) [#6031](https://github.com/nodejs/node/pull/6031)
* \[[`cc3645cff3`](https://github.com/nodejs/node/commit/cc3645cff3)] - **tools**: lint for alignment of variable assignments (Rich Trott) [#6869](https://github.com/nodejs/node/pull/6869)

<a id="4.4.6"></a>

## 2016-06-23, Version 4.4.6 'Argon' (LTS), @thealphanerd

### Notable Changes

This is an important security release. All Node.js users should consult the security release summary at nodejs.org for details on patched vulnerabilities.

This release is specifically related to a Buffer overflow vulnerability discovered in v8, more details can be found [in the CVE](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2016-1669)

### Commits

* \[[`134c3b3977`](https://github.com/nodejs/node/commit/134c3b3977)] - **deps**: backport 3a9bfec from v8 upstream (Ben Noordhuis) [nodejs/node-private#38](https://github.com/nodejs/node-private/pull/38)

<a id="4.4.5"></a>

## 2016-05-24, Version 4.4.5 'Argon' (LTS), @thealphanerd

### Notable Changes

* **buffer**:
  * Buffer.indexOf now returns correct values for all UTF-16 input (Anna Henningsen) [#6511](https://github.com/nodejs/node/pull/6511)
* **contextify**:
  * Context objects are now properly garbage collected, this solves a problem some individuals were experiencing with extreme memory growth (Ali Ijaz Sheikh) [#6871](https://github.com/nodejs/node/pull/6871)
* **deps**:
  * update npm to 2.15.5 (Rebecca Turner) [#6663](https://github.com/nodejs/node/pull/6663)
* **http**:
  * Invalid status codes can no longer be sent. Limited to 3 digit numbers between 100 - 999 (Brian White) [#6291](https://github.com/nodejs/node/pull/6291)

### Commits

* \[[`59a977dd22`](https://github.com/nodejs/node/commit/59a977dd22)] - **assert**: respect assert.doesNotThrow message. (Ilya Shaisultanov) [#2407](https://github.com/nodejs/node/pull/2407)
* \[[`8b077faa82`](https://github.com/nodejs/node/commit/8b077faa82)] - **buffer**: fix UCS2 indexOf for odd buffer length (Anna Henningsen) [#6511](https://github.com/nodejs/node/pull/6511)
* \[[`12a9699fcf`](https://github.com/nodejs/node/commit/12a9699fcf)] - **buffer**: fix needle length misestimation for UCS2 (Anna Henningsen) [#6511](https://github.com/nodejs/node/pull/6511)
* \[[`292b1b733e`](https://github.com/nodejs/node/commit/292b1b733e)] - **build**: fix make tar-headers for Linux (Gibson Fahnestock) [#5978](https://github.com/nodejs/node/pull/5978)
* \[[`918d33ad4b`](https://github.com/nodejs/node/commit/918d33ad4b)] - **build**: add script to create Android .mk files (Robert Chiras) [#5544](https://github.com/nodejs/node/pull/5544)
* \[[`4ad71847bc`](https://github.com/nodejs/node/commit/4ad71847bc)] - **build**: add suport for x86 architecture (Robert Chiras) [#5544](https://github.com/nodejs/node/pull/5544)
* \[[`6ad85914b1`](https://github.com/nodejs/node/commit/6ad85914b1)] - **child\_process**: add nullptr checks after allocs (Anna Henningsen) [#6256](https://github.com/nodejs/node/pull/6256)
* \[[`823f726f66`](https://github.com/nodejs/node/commit/823f726f66)] - **contextify**: tie lifetimes of context & sandbox (Ali Ijaz Sheikh) [#5800](https://github.com/nodejs/node/pull/5800)
* \[[`9ddb44ba61`](https://github.com/nodejs/node/commit/9ddb44ba61)] - **contextify**: cache sandbox and context in locals (Ali Ijaz Sheikh) [#5392](https://github.com/nodejs/node/pull/5392)
* \[[`8ebdcd65b0`](https://github.com/nodejs/node/commit/8ebdcd65b0)] - **contextify**: replace deprecated SetWeak usage (Ali Ijaz Sheikh) [#5392](https://github.com/nodejs/node/pull/5392)
* \[[`9e6d8170f7`](https://github.com/nodejs/node/commit/9e6d8170f7)] - **contextify**: cleanup weak ref for sandbox (Ali Ijaz Sheikh) [#5392](https://github.com/nodejs/node/pull/5392)
* \[[`b6fc15347d`](https://github.com/nodejs/node/commit/b6fc15347d)] - **contextify**: cleanup weak ref for global proxy (Ali Ijaz Sheikh) [#5392](https://github.com/nodejs/node/pull/5392)
* \[[`0dc875e2c7`](https://github.com/nodejs/node/commit/0dc875e2c7)] - **deps**: upgrade npm in LTS to 2.15.5 (Rebecca Turner)
* \[[`3c50350f41`](https://github.com/nodejs/node/commit/3c50350f41)] - **deps**: fix null pointer checks in v8 (Michaël Zasso) [#6669](https://github.com/nodejs/node/pull/6669)
* \[[`a40730b4b4`](https://github.com/nodejs/node/commit/a40730b4b4)] - **deps**: backport IsValid changes from 4e8736d in V8 (Michaël Zasso) [#6669](https://github.com/nodejs/node/pull/6669)
* \[[`855604c53a`](https://github.com/nodejs/node/commit/855604c53a)] - **deps**: upgrade npm in LTS to 2.15.4 (Rebecca Turner) [#6663](https://github.com/nodejs/node/pull/6663)
* \[[`433fb9a968`](https://github.com/nodejs/node/commit/433fb9a968)] - **deps**: cherry-pick 1383d00 from v8 upstream (Fedor Indutny) [#6179](https://github.com/nodejs/node/pull/6179)
* \[[`d1fca27ef8`](https://github.com/nodejs/node/commit/d1fca27ef8)] - **deps**: backport 125ac66 from v8 upstream (Myles Borins) [#6086](https://github.com/nodejs/node/pull/6086)
* \[[`df299019a0`](https://github.com/nodejs/node/commit/df299019a0)] - **deps**: upgrade npm in LTS to 2.15.2 (Kat Marchán)
* \[[`50f02bd8d6`](https://github.com/nodejs/node/commit/50f02bd8d6)] - **doc**: update vm.runInDebugContext() example (Ben Noordhuis) [#6757](https://github.com/nodejs/node/pull/6757)
* \[[`b872feade3`](https://github.com/nodejs/node/commit/b872feade3)] - **doc**: replace functions with arrow functions (abouthiroppy) [#6203](https://github.com/nodejs/node/pull/6203)
* \[[`7160229be4`](https://github.com/nodejs/node/commit/7160229be4)] - **doc**: note that zlib.flush acts after pending writes (Anna Henningsen) [#6172](https://github.com/nodejs/node/pull/6172)
* \[[`d069f2de8c`](https://github.com/nodejs/node/commit/d069f2de8c)] - **doc**: add full example for zlib.flush() (Anna Henningsen) [#6172](https://github.com/nodejs/node/pull/6172)
* \[[`59814acfef`](https://github.com/nodejs/node/commit/59814acfef)] - **doc**: describe child.kill() pitfalls on linux (Robert Jefe Lindstaedt) [#2098](https://github.com/nodejs/node/issues/2098)
* \[[`840c09492d`](https://github.com/nodejs/node/commit/840c09492d)] - **doc**: update openssl.org hash links (silverwind) [#6817](https://github.com/nodejs/node/pull/6817)
* \[[`126fdc3171`](https://github.com/nodejs/node/commit/126fdc3171)] - **doc**: fix issues related to page scrolling (Roman Reiss)
* \[[`29e25d8489`](https://github.com/nodejs/node/commit/29e25d8489)] - **doc**: add steps for running addons + npm tests (Myles Borins) [#6231](https://github.com/nodejs/node/pull/6231)
* \[[`fcc6a347f7`](https://github.com/nodejs/node/commit/fcc6a347f7)] - **doc**: get rid of sneaky hard tabs in CHANGELOG (Myles Borins) [#6608](https://github.com/nodejs/node/pull/6608)
* \[[`369569018e`](https://github.com/nodejs/node/commit/369569018e)] - **doc**: revert backported commits (Myles Borins) [#6530](https://github.com/nodejs/node/pull/6530)
* \[[`4ec9ae8a1c`](https://github.com/nodejs/node/commit/4ec9ae8a1c)] - **doc**: explain differences in console.assert between node and browsers (James M Snell) [#6169](https://github.com/nodejs/node/pull/6169)
* \[[`df5ce6fad4`](https://github.com/nodejs/node/commit/df5ce6fad4)] - **doc**: native module reloading is not supported (Bryan English) [#6168](https://github.com/nodejs/node/pull/6168)
* \[[`30f354f72b`](https://github.com/nodejs/node/commit/30f354f72b)] - **doc**: clarify fs.watch() and inodes on linux, os x (Joran Dirk Greef) [#6099](https://github.com/nodejs/node/pull/6099)
* \[[`29f821b73d`](https://github.com/nodejs/node/commit/29f821b73d)] - **doc**: clarifies http.serverResponse implementation (Allen Hernandez) [#6072](https://github.com/nodejs/node/pull/6072)
* \[[`6d560094f4`](https://github.com/nodejs/node/commit/6d560094f4)] - **doc**: minor argument formatting in stream.markdown (James M Snell) [#6016](https://github.com/nodejs/node/pull/6016)
* \[[`6a197ec617`](https://github.com/nodejs/node/commit/6a197ec617)] - **doc**: fix http response event, Agent#getName (Matthew Douglass) [#5993](https://github.com/nodejs/node/pull/5993)
* \[[`620a261240`](https://github.com/nodejs/node/commit/620a261240)] - **http**: disallow sending obviously invalid status codes (Brian White) [#6291](https://github.com/nodejs/node/pull/6291)
* \[[`9a8b53124d`](https://github.com/nodejs/node/commit/9a8b53124d)] - **http**: unref socket timer on parser execute (Fedor Indutny) [#6286](https://github.com/nodejs/node/pull/6286)
* \[[`b28e44deb2`](https://github.com/nodejs/node/commit/b28e44deb2)] - **http**: Corrects IPv6 address in Host header (Mihai Potra) [#5314](https://github.com/nodejs/node/pull/5314)
* \[[`2fac15ba94`](https://github.com/nodejs/node/commit/2fac15ba94)] - **src**: fix FindFirstCharacter argument alignment (Anna Henningsen) [#6511](https://github.com/nodejs/node/pull/6511)
* \[[`2942cff069`](https://github.com/nodejs/node/commit/2942cff069)] - **src**: add missing 'inline' keywords (Ben Noordhuis) [#6056](https://github.com/nodejs/node/pull/6056)
* \[[`e0eebf412e`](https://github.com/nodejs/node/commit/e0eebf412e)] - **src,tools**: remove null sentinel from source array (Ben Noordhuis) [#5418](https://github.com/nodejs/node/pull/5418)
* \[[`8f18414cd5`](https://github.com/nodejs/node/commit/8f18414cd5)] - **src,tools**: drop nul byte from built-in source code (Ben Noordhuis) [#5418](https://github.com/nodejs/node/pull/5418)
* \[[`d7a3ea457b`](https://github.com/nodejs/node/commit/d7a3ea457b)] - **src,tools**: allow utf-8 in built-in js source code (Ben Noordhuis) [#5418](https://github.com/nodejs/node/pull/5418)
* \[[`51c0808b55`](https://github.com/nodejs/node/commit/51c0808b55)] - **stream**: Fix readableState.awaitDrain mechanism (Anna Henningsen) [#6023](https://github.com/nodejs/node/pull/6023)
* \[[`49a5941d30`](https://github.com/nodejs/node/commit/49a5941d30)] - **test**: fix test-debug-port-cluster flakiness (Rich Trott) [#6769](https://github.com/nodejs/node/pull/6769)
* \[[`f8144e4c4a`](https://github.com/nodejs/node/commit/f8144e4c4a)] - **test**: add logging for test-debug-port-cluster (Rich Trott) [#6769](https://github.com/nodejs/node/pull/6769)
* \[[`773ea20d0e`](https://github.com/nodejs/node/commit/773ea20d0e)] - **test**: include component in tap output (Ben Noordhuis) [#6653](https://github.com/nodejs/node/pull/6653)
* \[[`333369e1ff`](https://github.com/nodejs/node/commit/333369e1ff)] - **test**: increase the platform timeout for AIX (Michael Dawson) [#6342](https://github.com/nodejs/node/pull/6342)
* \[[`06e5fafe84`](https://github.com/nodejs/node/commit/06e5fafe84)] - **test**: add tests for console.assert (Evan Lucas) [#6302](https://github.com/nodejs/node/pull/6302)
* \[[`f60ba54811`](https://github.com/nodejs/node/commit/f60ba54811)] - **test**: add zlib close-after-error regression test (Anna Henningsen) [#6270](https://github.com/nodejs/node/pull/6270)
* \[[`24ac16f4be`](https://github.com/nodejs/node/commit/24ac16f4be)] - **test**: fix flaky test-http-set-timeout-server (Santiago Gimeno) [#6248](https://github.com/nodejs/node/pull/6248)
* \[[`5002a71357`](https://github.com/nodejs/node/commit/5002a71357)] - **test**: assert - fixed error messages to match the tests (surya panikkal) [#6241](https://github.com/nodejs/node/pull/6241)
* \[[`0f9405dd33`](https://github.com/nodejs/node/commit/0f9405dd33)] - **test**: move more tests from sequential to parallel (Santiago Gimeno) [#6187](https://github.com/nodejs/node/pull/6187)
* \[[`37cc249218`](https://github.com/nodejs/node/commit/37cc249218)] - **test**: fix test-net-settimeout flakiness (Santiago Gimeno) [#6166](https://github.com/nodejs/node/pull/6166)
* \[[`69dcbb642f`](https://github.com/nodejs/node/commit/69dcbb642f)] - **test**: fix flaky test-child-process-fork-net (Rich Trott) [#6138](https://github.com/nodejs/node/pull/6138)
* \[[`a97a6a9d69`](https://github.com/nodejs/node/commit/a97a6a9d69)] - **test**: fix issues for ESLint 2.7.0 (silverwind) [#6132](https://github.com/nodejs/node/pull/6132)
* \[[`a865975909`](https://github.com/nodejs/node/commit/a865975909)] - **test**: fix flaky test-http-client-abort (Rich Trott) [#6124](https://github.com/nodejs/node/pull/6124)
* \[[`25d4b5b1e9`](https://github.com/nodejs/node/commit/25d4b5b1e9)] - **test**: move some test from sequential to parallel (Santiago Gimeno) [#6087](https://github.com/nodejs/node/pull/6087)
* \[[`28040ccf49`](https://github.com/nodejs/node/commit/28040ccf49)] - **test**: refactor test-file-write-stream3 (Rich Trott) [#6050](https://github.com/nodejs/node/pull/6050)
* \[[`3a67a05ed4`](https://github.com/nodejs/node/commit/3a67a05ed4)] - **test**: enforce strict mode for test-domain-crypto (Rich Trott) [#6047](https://github.com/nodejs/node/pull/6047)
* \[[`0b376cb3f9`](https://github.com/nodejs/node/commit/0b376cb3f9)] - **test**: fix pummel test failures (Rich Trott) [#6012](https://github.com/nodejs/node/pull/6012)
* \[[`7b60b8f8e9`](https://github.com/nodejs/node/commit/7b60b8f8e9)] - **test**: fix flakiness of stringbytes-external (Ali Ijaz Sheikh) [#6705](https://github.com/nodejs/node/pull/6705)
* \[[`cc4c5187ed`](https://github.com/nodejs/node/commit/cc4c5187ed)] - **test**: ensure test-npm-install uses correct node (Myles Borins) [#6658](https://github.com/nodejs/node/pull/6658)
* \[[`3d4d5777bc`](https://github.com/nodejs/node/commit/3d4d5777bc)] - **test**: refactor http-end-throw-socket-handling (Santiago Gimeno) [#5676](https://github.com/nodejs/node/pull/5676)
* \[[`c76f214b90`](https://github.com/nodejs/node/commit/c76f214b90)] - **test,tools**: enable linting for undefined vars (Rich Trott) [#6255](https://github.com/nodejs/node/pull/6255)
* \[[`9222689215`](https://github.com/nodejs/node/commit/9222689215)] - **test,vm**: enable strict mode for vm tests (Rich Trott) [#6209](https://github.com/nodejs/node/pull/6209)
* \[[`b8c9d6b64e`](https://github.com/nodejs/node/commit/b8c9d6b64e)] - **tools**: enable linting for v8\_prof\_processor.js (Rich Trott) [#6262](https://github.com/nodejs/node/pull/6262)
* \[[`8fa202947d`](https://github.com/nodejs/node/commit/8fa202947d)] - **tools**: lint rule for assert.fail() (Rich Trott) [#6261](https://github.com/nodejs/node/pull/6261)
* \[[`1aa6c5b7a9`](https://github.com/nodejs/node/commit/1aa6c5b7a9)] - **tools**: update ESLint to 2.7.0 (silverwind) [#6132](https://github.com/nodejs/node/pull/6132)
* \[[`68c7de4372`](https://github.com/nodejs/node/commit/68c7de4372)] - **tools**: remove simplejson dependency (Sakthipriyan Vairamani) [#6101](https://github.com/nodejs/node/pull/6101)
* \[[`4fb4ba98a8`](https://github.com/nodejs/node/commit/4fb4ba98a8)] - **tools**: remove disabling of already-disabled rule (Rich Trott) [#6013](https://github.com/nodejs/node/pull/6013)
* \[[`4e6ea7f01a`](https://github.com/nodejs/node/commit/4e6ea7f01a)] - **tools**: remove obsolete npm test-legacy command (Kat Marchán)
* \[[`4c73ab4302`](https://github.com/nodejs/node/commit/4c73ab4302)] - **tools,doc**: fix json for grouped optional params (firedfox) [#5977](https://github.com/nodejs/node/pull/5977)
* \[[`c893cd33d1`](https://github.com/nodejs/node/commit/c893cd33d1)] - **tools,doc**: parse types in braces everywhere (Alexander Makarenko) [#5329](https://github.com/nodejs/node/pull/5329)
* \[[`48684af55f`](https://github.com/nodejs/node/commit/48684af55f)] - **zlib**: fix use after null when calling .close (James Lal) [#5982](https://github.com/nodejs/node/pull/5982)

<a id="4.4.4"></a>

## 2016-05-05, Version 4.4.4 'Argon' (LTS), @thealphanerd

### Notable changes

* **deps**:
  * update openssl to 1.0.2h. (Shigeki Ohtsu) [#6551](https://github.com/nodejs/node/pull/6551)
    * Please see our [blog post](https://nodejs.org/en/blog/vulnerability/openssl-may-2016/) for more info on the security contents of this release.

### Commits

* \[[`f46952e727`](https://github.com/nodejs/node/commit/f46952e727)] - **buffer**: safeguard against accidental kNoZeroFill (Сковорода Никита Андреевич) [nodejs/node-private#30](https://github.com/nodejs/node-private/pull/30)
* \[[`4f1c82f995`](https://github.com/nodejs/node/commit/4f1c82f995)] - **streams**: support unlimited synchronous cork/uncork cycles (Matteo Collina) [#6164](https://github.com/nodejs/node/pull/6164)
* \[[`1efd96c767`](https://github.com/nodejs/node/commit/1efd96c767)] - **deps**: update openssl asm and asm\_obsolete files (Shigeki Ohtsu) [#6551](https://github.com/nodejs/node/pull/6551)
* \[[`c450f4a293`](https://github.com/nodejs/node/commit/c450f4a293)] - **deps**: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) [nodejs/io.js#1836](https://github.com/nodejs/io.js/pull/1836)
* \[[`baedfbae6a`](https://github.com/nodejs/node/commit/baedfbae6a)] - **openssl**: fix keypress requirement in apps on win32 (Shigeki Ohtsu) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`ff3045e40b`](https://github.com/nodejs/node/commit/ff3045e40b)] - **deps**: fix asm build error of openssl in x86\_win32 (Shigeki Ohtsu) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`dc8dc97db3`](https://github.com/nodejs/node/commit/dc8dc97db3)] - **deps**: fix openssl assembly error on ia32 win32 (Fedor Indutny) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`2dfeb01213`](https://github.com/nodejs/node/commit/2dfeb01213)] - **deps**: copy all openssl header files to include dir (Shigeki Ohtsu) [#6551](https://github.com/nodejs/node/pull/6551)
* \[[`72f9952516`](https://github.com/nodejs/node/commit/72f9952516)] - **deps**: upgrade openssl sources to 1.0.2h (Shigeki Ohtsu) [#6551](https://github.com/nodejs/node/pull/6551)

<a id="4.4.3"></a>

## 2016-04-12, Version 4.4.3 'Argon' (LTS), @thealphanerd

### Notable Changes

* **deps**:
  * Fix `--gdbjit` for embedders. Backported from v8 upstream. (Ben Noordhuis) [#5577](https://github.com/nodejs/node/pull/5577)
* **etw**:
  * Correctly display descriptors for ETW events 9 and 23 on the windows platform. (João Reis) [#5742](https://github.com/nodejs/node/pull/5742)
* **querystring**:
  * Restore throw when attempting to stringify bad surrogate pair. (Brian White) [#5858](https://github.com/nodejs/node/pull/5858)

### Commits

* \[[`f949c273cd`](https://github.com/nodejs/node/commit/f949c273cd)] - **assert**: Check typed array view type in deepEqual (Anna Henningsen) [#5910](https://github.com/nodejs/node/pull/5910)
* \[[`132acea0d4`](https://github.com/nodejs/node/commit/132acea0d4)] - **build**: introduce ci targets for lint/benchmark (Johan Bergström) [#5921](https://github.com/nodejs/node/pull/5921)
* \[[`9a8f922dee`](https://github.com/nodejs/node/commit/9a8f922dee)] - **build**: add missing `openssl_fips%` to common.gypi (Fedor Indutny) [#5919](https://github.com/nodejs/node/pull/5919)
* \[[`d275cdf202`](https://github.com/nodejs/node/commit/d275cdf202)] - **child\_process**: refactor self=this in socket\_list (Benjamin Gruenbaum) [#5860](https://github.com/nodejs/node/pull/5860)
* \[[`aadf356aa2`](https://github.com/nodejs/node/commit/aadf356aa2)] - **deps**: backport 8d00c2c from v8 upstream (Ben Noordhuis) [#5577](https://github.com/nodejs/node/pull/5577)
* \[[`200f763c43`](https://github.com/nodejs/node/commit/200f763c43)] - **deps**: completely upgrade npm in LTS to 2.15.1 (Forrest L Norvell) [#5989](https://github.com/nodejs/node/pull/5989)
* \[[`86e3903626`](https://github.com/nodejs/node/commit/86e3903626)] - **dns**: Use object without protoype for map (Benjamin Gruenbaum) [#5843](https://github.com/nodejs/node/pull/5843)
* \[[`9a33f43f73`](https://github.com/nodejs/node/commit/9a33f43f73)] - **doc**: update openssl LICENSE using license-builder.sh (Steven R. Loomis) [#6065](https://github.com/nodejs/node/pull/6065)
* \[[`9679e2dc70`](https://github.com/nodejs/node/commit/9679e2dc70)] - **doc**: clarify that \_\_dirname is module local (James M Snell) [#6018](https://github.com/nodejs/node/pull/6018)
* \[[`86d2af58d6`](https://github.com/nodejs/node/commit/86d2af58d6)] - **doc**: simple doc typo fix (Brendon Pierson) [#6041](https://github.com/nodejs/node/pull/6041)
* \[[`f16802f3ca`](https://github.com/nodejs/node/commit/f16802f3ca)] - **doc**: note about Android support (Rich Trott) [#6040](https://github.com/nodejs/node/pull/6040)
* \[[`8c2befe176`](https://github.com/nodejs/node/commit/8c2befe176)] - **doc**: note assert.throws() pitfall (Rich Trott) [#6029](https://github.com/nodejs/node/pull/6029)
* \[[`0870ac65f2`](https://github.com/nodejs/node/commit/0870ac65f2)] - **doc**: use HTTPS for links where possible (Rich Trott) [#6019](https://github.com/nodejs/node/pull/6019)
* \[[`56755de96e`](https://github.com/nodejs/node/commit/56755de96e)] - **doc**: clarify stdout/stderr arguments to callback (James M Snell) [#6015](https://github.com/nodejs/node/pull/6015)
* \[[`bb603b89a2`](https://github.com/nodejs/node/commit/bb603b89a2)] - **doc**: add 'Command Line Options' to 'View on single page' (firedfox) [#6011](https://github.com/nodejs/node/pull/6011)
* \[[`c91f3d897a`](https://github.com/nodejs/node/commit/c91f3d897a)] - **doc**: add copy about how to curl SHA256.txt (Myles Borins) [#6120](https://github.com/nodejs/node/pull/6120)
* \[[`f9cf232284`](https://github.com/nodejs/node/commit/f9cf232284)] - **doc**: add example using algorithms not directly exposed (Brad Hill) [#6108](https://github.com/nodejs/node/pull/6108)
* \[[`f60ce1078d`](https://github.com/nodejs/node/commit/f60ce1078d)] - **doc**: document unspecified behavior for buf.write\* methods (James M Snell) [#5925](https://github.com/nodejs/node/pull/5925)
* \[[`02401a6cbd`](https://github.com/nodejs/node/commit/02401a6cbd)] - **doc**: fix scrolling on iOS devices (Luigi Pinca) [#5878](https://github.com/nodejs/node/pull/5878)
* \[[`aed22d0855`](https://github.com/nodejs/node/commit/aed22d0855)] - **doc**: path.format provide more examples (John Eversole) [#5838](https://github.com/nodejs/node/pull/5838)
* \[[`6e2bfbe1fd`](https://github.com/nodejs/node/commit/6e2bfbe1fd)] - **doc**: fix doc for Buffer.readInt32LE() (ghaiklor) [#5890](https://github.com/nodejs/node/pull/5890)
* \[[`940d204401`](https://github.com/nodejs/node/commit/940d204401)] - **doc**: consolidate timers docs in timers.markdown (Bryan English) [#5837](https://github.com/nodejs/node/pull/5837)
* \[[`505faf6360`](https://github.com/nodejs/node/commit/505faf6360)] - **doc**: refine child\_process detach behaviour (Robert Jefe Lindstaedt) [#5330](https://github.com/nodejs/node/pull/5330)
* \[[`feedca7879`](https://github.com/nodejs/node/commit/feedca7879)] - **doc**: add topic - event loop, timers, `nextTick()` (Jeff Harris) [#4936](https://github.com/nodejs/node/pull/4936)
* \[[`6d3822c12b`](https://github.com/nodejs/node/commit/6d3822c12b)] - **etw**: fix descriptors of events 9 and 23 (João Reis) [#5742](https://github.com/nodejs/node/pull/5742)
* \[[`56dda6f336`](https://github.com/nodejs/node/commit/56dda6f336)] - **fs**: Remove unused branches (Benjamin Gruenbaum) [#5289](https://github.com/nodejs/node/pull/5289)
* \[[`dfe9e157c1`](https://github.com/nodejs/node/commit/dfe9e157c1)] - **governance**: remove target size for CTC (Rich Trott) [#5879](https://github.com/nodejs/node/pull/5879)
* \[[`c4103b154f`](https://github.com/nodejs/node/commit/c4103b154f)] - **lib**: refactor code with startsWith/endsWith (Jackson Tian) [#5753](https://github.com/nodejs/node/pull/5753)
* \[[`16216a81de`](https://github.com/nodejs/node/commit/16216a81de)] - **meta**: add "joining a wg" section to WORKING\_GROUPS.md (Matteo Collina) [#5488](https://github.com/nodejs/node/pull/5488)
* \[[`65fc4e36ce`](https://github.com/nodejs/node/commit/65fc4e36ce)] - **querystring**: don't stringify bad surrogate pair (Brian White) [#5858](https://github.com/nodejs/node/pull/5858)
* \[[`4f683ab912`](https://github.com/nodejs/node/commit/4f683ab912)] - **src,tools**: use template literals (Rich Trott) [#5778](https://github.com/nodejs/node/pull/5778)
* \[[`ac40a4510d`](https://github.com/nodejs/node/commit/ac40a4510d)] - **test**: explicitly set global in test-repl (Rich Trott) [#6026](https://github.com/nodejs/node/pull/6026)
* \[[`a7b3a7533a`](https://github.com/nodejs/node/commit/a7b3a7533a)] - **test**: be explicit about polluting of `global` (Rich Trott) [#6017](https://github.com/nodejs/node/pull/6017)
* \[[`73e3b7b9a8`](https://github.com/nodejs/node/commit/73e3b7b9a8)] - **test**: make use of globals explicit (Rich Trott) [#6014](https://github.com/nodejs/node/pull/6014)
* \[[`e7877e61b6`](https://github.com/nodejs/node/commit/e7877e61b6)] - **test**: fix flaky test-net-socket-timeout-unref (Rich Trott) [#6003](https://github.com/nodejs/node/pull/6003)
* \[[`a39051f5b3`](https://github.com/nodejs/node/commit/a39051f5b3)] - **test**: make arch available in status files (Santiago Gimeno) [#5997](https://github.com/nodejs/node/pull/5997)
* \[[`ccf90b651a`](https://github.com/nodejs/node/commit/ccf90b651a)] - **test**: fix test-dns.js flakiness (Rich Trott) [#5996](https://github.com/nodejs/node/pull/5996)
* \[[`1994ac0912`](https://github.com/nodejs/node/commit/1994ac0912)] - **test**: add test for piping large input from stdin (Anna Henningsen) [#5949](https://github.com/nodejs/node/pull/5949)
* \[[`cc1aab9f6a`](https://github.com/nodejs/node/commit/cc1aab9f6a)] - **test**: mitigate flaky test-https-agent (Rich Trott) [#5939](https://github.com/nodejs/node/pull/5939)
* \[[`10fe79b809`](https://github.com/nodejs/node/commit/10fe79b809)] - **test**: fix offending max-len linter error (Sakthipriyan Vairamani) [#5980](https://github.com/nodejs/node/pull/5980)
* \[[`63d82960fd`](https://github.com/nodejs/node/commit/63d82960fd)] - **test**: stdin is not always a net.Socket (Jeremiah Senkpiel) [#5935](https://github.com/nodejs/node/pull/5935)
* \[[`fe0233b923`](https://github.com/nodejs/node/commit/fe0233b923)] - **test**: add known\_issues test for GH-2148 (Rich Trott) [#5920](https://github.com/nodejs/node/pull/5920)
* \[[`d59be4d248`](https://github.com/nodejs/node/commit/d59be4d248)] - **test**: ensure \_handle property existence (Rich Trott) [#5916](https://github.com/nodejs/node/pull/5916)
* \[[`9702153107`](https://github.com/nodejs/node/commit/9702153107)] - **test**: fix flaky test-repl (Brian White) [#5914](https://github.com/nodejs/node/pull/5914)
* \[[`a0a2e69097`](https://github.com/nodejs/node/commit/a0a2e69097)] - **test**: move dns test to test/internet (Ben Noordhuis) [#5905](https://github.com/nodejs/node/pull/5905)
* \[[`8462d8f465`](https://github.com/nodejs/node/commit/8462d8f465)] - **test**: fix flaky test-net-socket-timeout (Brian White) [#5902](https://github.com/nodejs/node/pull/5902)
* \[[`e0b283af73`](https://github.com/nodejs/node/commit/e0b283af73)] - **test**: fix flaky test-http-set-timeout (Rich Trott) [#5856](https://github.com/nodejs/node/pull/5856)
* \[[`5853fec36f`](https://github.com/nodejs/node/commit/5853fec36f)] - **test**: fix test-debugger-client.js (Rich Trott) [#5851](https://github.com/nodejs/node/pull/5851)
* \[[`ea83c382f9`](https://github.com/nodejs/node/commit/ea83c382f9)] - **test**: ensure win32.isAbsolute() is consistent (Brian White) [#6043](https://github.com/nodejs/node/pull/6043)
* \[[`c33a23fd1e`](https://github.com/nodejs/node/commit/c33a23fd1e)] - **tools**: fix json doc generation (firedfox) [#5943](https://github.com/nodejs/node/pull/5943)
* \[[`6f0bd64122`](https://github.com/nodejs/node/commit/6f0bd64122)] - **tools,doc**: fix incomplete json produced by doctool (firedfox) [#5966](https://github.com/nodejs/node/pull/5966)
* \[[`f7eb48302c`](https://github.com/nodejs/node/commit/f7eb48302c)] - **win,build**: build and test add-ons on test-ci (Bogdan Lobor) [#5886](https://github.com/nodejs/node/pull/5886)

<a id="4.4.2"></a>

## 2016-03-31, Version 4.4.2 'Argon' (LTS), @thealphanerd

### Notable Changes

* **https**:
  * Under certain conditions ssl sockets may have been causing a memory leak when keepalive is enabled. This is no longer the case. (Alexander Penev) [#5713](https://github.com/nodejs/node/pull/5713)
* **lib**:
  * The way that we were internally passing arguments was causing a potential leak. By copying the arguments into an array we can avoid this. (Nathan Woltman) [#4361](https://github.com/nodejs/node/pull/4361)
* **npm**:
  * Upgrade to v2.15.1. Fixes a security flaw in the use of authentication tokens in HTTP requests that would allow an attacker to set up a server that could collect tokens from users of the command-line interface. Authentication tokens have previously been sent with every request made by the CLI for logged-in users, regardless of the destination of the request. This update fixes this by only including those tokens for requests made against the registry or registries used for the current install. (Forrest L Norvell)
* **repl**:
  * Previously if you were using the repl in strict mode the column number would be wrong in a stack trace. This is no longer an issue. (Prince J Wesley) [#5416](https://github.com/nodejs/node/pull/5416)

### Commits

* \[[`96e163a79f`](https://github.com/nodejs/node/commit/96e163a79f)] - **buffer**: changing let in for loops back to var (Gareth Ellis) [#5819](https://github.com/nodejs/node/pull/5819)
* \[[`0c6f6742f2`](https://github.com/nodejs/node/commit/0c6f6742f2)] - **console**: check that stderr is writable (Rich Trott) [#5635](https://github.com/nodejs/node/pull/5635)
* \[[`55c3f804c4`](https://github.com/nodejs/node/commit/55c3f804c4)] - **deps**: upgrade npm in LTS to 2.15.1 (Forrest L Norvell)
* \[[`1d0e4a987d`](https://github.com/nodejs/node/commit/1d0e4a987d)] - **deps**: remove unused openssl files (Ben Noordhuis) [#5619](https://github.com/nodejs/node/pull/5619)
* \[[`d55599f4d8`](https://github.com/nodejs/node/commit/d55599f4d8)] - **dns**: use template literals (Benjamin Gruenbaum) [#5809](https://github.com/nodejs/node/pull/5809)
* \[[`42bbdc9dd1`](https://github.com/nodejs/node/commit/42bbdc9dd1)] - **doc** Add @mhdawson back to the CTC (James M Snell) [#5633](https://github.com/nodejs/node/pull/5633)
* \[[`8d86d232e7`](https://github.com/nodejs/node/commit/8d86d232e7)] - **doc**: typo: interal->internal. (Corey Kosak) [#5849](https://github.com/nodejs/node/pull/5849)
* \[[`60ddab841e`](https://github.com/nodejs/node/commit/60ddab841e)] - **doc**: add instructions to only sign a release (Jeremiah Senkpiel) [#5876](https://github.com/nodejs/node/pull/5876)
* \[[`040263e0f3`](https://github.com/nodejs/node/commit/040263e0f3)] - **doc**: grammar, clarity and links in timers doc (Bryan English) [#5792](https://github.com/nodejs/node/pull/5792)
* \[[`8c24bd25a6`](https://github.com/nodejs/node/commit/8c24bd25a6)] - **doc**: fix order of end tags of list after heading (firedfox) [#5874](https://github.com/nodejs/node/pull/5874)
* \[[`7c837028da`](https://github.com/nodejs/node/commit/7c837028da)] - **doc**: use consistent event name parameter (Benjamin Gruenbaum) [#5850](https://github.com/nodejs/node/pull/5850)
* \[[`20faf9097d`](https://github.com/nodejs/node/commit/20faf9097d)] - **doc**: explain error message on missing main file (Wolfgang Steiner) [#5812](https://github.com/nodejs/node/pull/5812)
* \[[`79d26ae196`](https://github.com/nodejs/node/commit/79d26ae196)] - **doc**: explain path.format expected properties (John Eversole) [#5801](https://github.com/nodejs/node/pull/5801)
* \[[`e43e8e3a31`](https://github.com/nodejs/node/commit/e43e8e3a31)] - **doc**: add a cli options doc page (Jeremiah Senkpiel) [#5787](https://github.com/nodejs/node/pull/5787)
* \[[`c0a24e4a1d`](https://github.com/nodejs/node/commit/c0a24e4a1d)] - **doc**: fix multiline return comments in querystring (Claudio Rodriguez) [#5705](https://github.com/nodejs/node/pull/5705)
* \[[`bf1fe4693c`](https://github.com/nodejs/node/commit/bf1fe4693c)] - **doc**: Add windows example for Path.format (Mithun Patel) [#5700](https://github.com/nodejs/node/pull/5700)
* \[[`3b8fc4fddc`](https://github.com/nodejs/node/commit/3b8fc4fddc)] - **doc**: update crypto docs to use good defaults (Bill Automata) [#5505](https://github.com/nodejs/node/pull/5505)
* \[[`a6ec8a6cb7`](https://github.com/nodejs/node/commit/a6ec8a6cb7)] - **doc**: fix crypto update() signatures (Brian White) [#5500](https://github.com/nodejs/node/pull/5500)
* \[[`eb0ed46665`](https://github.com/nodejs/node/commit/eb0ed46665)] - **doc**: reformat & improve node.1 manual page (Jeremiah Senkpiel) [#5497](https://github.com/nodejs/node/pull/5497)
* \[[`b70ca4a4b4`](https://github.com/nodejs/node/commit/b70ca4a4b4)] - **doc**: updated fs #5862 removed irrelevant data in fs.markdown (topal) [#5877](https://github.com/nodejs/node/pull/5877)
* \[[`81876612f7`](https://github.com/nodejs/node/commit/81876612f7)] - **https**: fix ssl socket leak when keepalive is used (Alexander Penev) [#5713](https://github.com/nodejs/node/pull/5713)
* \[[`6daebdbd9b`](https://github.com/nodejs/node/commit/6daebdbd9b)] - **lib**: simplify code with String.prototype.repeat() (Jackson Tian) [#5359](https://github.com/nodejs/node/pull/5359)
* \[[`108fc90dd7`](https://github.com/nodejs/node/commit/108fc90dd7)] - **lib**: reduce usage of `self = this` (Jackson Tian) [#5231](https://github.com/nodejs/node/pull/5231)
* \[[`3c8e59c396`](https://github.com/nodejs/node/commit/3c8e59c396)] - **lib**: copy arguments object instead of leaking it (Nathan Woltman) [#4361](https://github.com/nodejs/node/pull/4361)
* \[[`8648420586`](https://github.com/nodejs/node/commit/8648420586)] - **net**: make `isIPv4` and `isIPv6` more efficient (Vladimir Kurchatkin) [#5478](https://github.com/nodejs/node/pull/5478)
* \[[`07b7172d76`](https://github.com/nodejs/node/commit/07b7172d76)] - **net**: remove unused `var self = this` from old code (Benjamin Gruenbaum) [#5224](https://github.com/nodejs/node/pull/5224)
* \[[`acbce4b72b`](https://github.com/nodejs/node/commit/acbce4b72b)] - **repl**: fix stack trace column number in strict mode (Prince J Wesley) [#5416](https://github.com/nodejs/node/pull/5416)
* \[[`0a1eb168e0`](https://github.com/nodejs/node/commit/0a1eb168e0)] - **test**: fix `test-cluster-worker-kill` (Santiago Gimeno) [#5814](https://github.com/nodejs/node/pull/5814)
* \[[`86b876fe7b`](https://github.com/nodejs/node/commit/86b876fe7b)] - **test**: smaller chunk size for smaller person.jpg (Jérémy Lal) [#5813](https://github.com/nodejs/node/pull/5813)
* \[[`1135ee97e7`](https://github.com/nodejs/node/commit/1135ee97e7)] - **test**: strip non-free icc profile from person.jpg (Jérémy Lal) [#5813](https://github.com/nodejs/node/pull/5813)
* \[[`0836d7e2fb`](https://github.com/nodejs/node/commit/0836d7e2fb)] - **test**: fix flaky test-cluster-shared-leak (Claudio Rodriguez) [#5802](https://github.com/nodejs/node/pull/5802)
* \[[`e57355c2f4`](https://github.com/nodejs/node/commit/e57355c2f4)] - **test**: make test-net-connect-options-ipv6.js better (Michael Dawson) [#5791](https://github.com/nodejs/node/pull/5791)
* \[[`1b266fc15c`](https://github.com/nodejs/node/commit/1b266fc15c)] - **test**: remove the use of curl in the test suite (Santiago Gimeno) [#5750](https://github.com/nodejs/node/pull/5750)
* \[[`7e45d4f076`](https://github.com/nodejs/node/commit/7e45d4f076)] - **test**: minimize test-http-get-pipeline-problem (Rich Trott) [#5728](https://github.com/nodejs/node/pull/5728)
* \[[`78effc3484`](https://github.com/nodejs/node/commit/78effc3484)] - **test**: add batch of known issue tests (cjihrig) [#5653](https://github.com/nodejs/node/pull/5653)
* \[[`d506eea4b7`](https://github.com/nodejs/node/commit/d506eea4b7)] - **test**: improve test-npm-install (Santiago Gimeno) [#5613](https://github.com/nodejs/node/pull/5613)
* \[[`7520100e8b`](https://github.com/nodejs/node/commit/7520100e8b)] - **test**: add test-npm-install to parallel tests suite (Myles Borins) [#5166](https://github.com/nodejs/node/pull/5166)
* \[[`b258dddb8c`](https://github.com/nodejs/node/commit/b258dddb8c)] - **test**: repl tab completion test (Santiago Gimeno) [#5534](https://github.com/nodejs/node/pull/5534)
* \[[`f209effe8b`](https://github.com/nodejs/node/commit/f209effe8b)] - **test**: remove timer from test-http-1.0 (Santiago Gimeno) [#5129](https://github.com/nodejs/node/pull/5129)
* \[[`3a901b0e3e`](https://github.com/nodejs/node/commit/3a901b0e3e)] - **tools**: remove unused imports (Sakthipriyan Vairamani) [#5765](https://github.com/nodejs/node/pull/5765)

<a id="4.4.1"></a>

## 2016-03-22, Version 4.4.1 'Argon' (LTS), @thealphanerd

This LTS release comes with 113 commits, 56 of which are doc related,
18 of which are build / tooling related, 16 of which are test related
and 7 which are benchmark related.

### Notable Changes

* **build**:
  * Updated Logos for the OSX + Windows installers
    * (Rod Vagg) [#5401](https://github.com/nodejs/node/pull/5401)
    * (Robert Jefe Lindstaedt) [#5531](https://github.com/nodejs/node/pull/5531)
  * New option to select your VS Version in the Windows installer
    * (julien.waechter) [#4645](https://github.com/nodejs/node/pull/4645)
  * Support Visual C++ Build Tools 2015
    * (João Reis) [#5627](https://github.com/nodejs/node/pull/5627)
* **tools**:
  * Gyp now works on OSX without XCode
    * (Shigeki Ohtsu) [nodejs/node#1325](https://github.com/nodejs/node/pull/1325)

### Commits

* \[[`df283f8a03`](https://github.com/nodejs/node/commit/df283f8a03)] - **benchmark**: fix linting issues (Rich Trott) [#5773](https://github.com/nodejs/node/pull/5773)
* \[[`c901741c60`](https://github.com/nodejs/node/commit/c901741c60)] - **benchmark**: use strict mode (Rich Trott) [#5773](https://github.com/nodejs/node/pull/5773)
* \[[`4be2065dbc`](https://github.com/nodejs/node/commit/4be2065dbc)] - **benchmark**: refactor to eliminate redeclared vars (Rich Trott) [#5773](https://github.com/nodejs/node/pull/5773)
* \[[`ddac368533`](https://github.com/nodejs/node/commit/ddac368533)] - **benchmark**: fix lint errors (Rich Trott) [#5773](https://github.com/nodejs/node/pull/5773)
* \[[`03b20a73b9`](https://github.com/nodejs/node/commit/03b20a73b9)] - **benchmark**: add benchmark for buf.compare() (Rich Trott) [#5441](https://github.com/nodejs/node/pull/5441)
* \[[`b816044845`](https://github.com/nodejs/node/commit/b816044845)] - **buffer**: remove duplicated code in fromObject (HUANG Wei) [#4948](https://github.com/nodejs/node/pull/4948)
* \[[`067ce9b905`](https://github.com/nodejs/node/commit/067ce9b905)] - **build**: don't install github templates (Johan Bergström) [#5612](https://github.com/nodejs/node/pull/5612)
* \[[`a1772dc515`](https://github.com/nodejs/node/commit/a1772dc515)] - **build**: update Node.js logo on OSX installer (Rod Vagg) [#5401](https://github.com/nodejs/node/pull/5401)
* \[[`9058fc0383`](https://github.com/nodejs/node/commit/9058fc0383)] - **build**: correctly detect clang version (Stefan Budeanu) [#5553](https://github.com/nodejs/node/pull/5553)
* \[[`1165ecc6f7`](https://github.com/nodejs/node/commit/1165ecc6f7)] - **build**: update Node.js logo on Win installer (Robert Jefe Lindstaedt) [#5531](https://github.com/nodejs/node/pull/5531)
* \[[`4990ddad72`](https://github.com/nodejs/node/commit/4990ddad72)] - **build**: remove --quiet from eslint invocation (firedfox) [#5519](https://github.com/nodejs/node/pull/5519)
* \[[`46a5d519dd`](https://github.com/nodejs/node/commit/46a5d519dd)] - **build**: skip msi build if WiX is not found (Tsarevich Dmitry) [#5220](https://github.com/nodejs/node/pull/5220)
* \[[`dac4e64491`](https://github.com/nodejs/node/commit/dac4e64491)] - **build**: add option to select VS version (julien.waechter) [#4645](https://github.com/nodejs/node/pull/4645)
* \[[`7a10fd3a56`](https://github.com/nodejs/node/commit/7a10fd3a56)] - **collaborator\_guide**: clarify commit message rules (Wyatt Preul) [#5661](https://github.com/nodejs/node/pull/5661)
* \[[`97e95d04c2`](https://github.com/nodejs/node/commit/97e95d04c2)] - **crypto**: PBKDF2 works with `int` not `ssize_t` (Fedor Indutny) [#5397](https://github.com/nodejs/node/pull/5397)
* \[[`57b02e6a3e`](https://github.com/nodejs/node/commit/57b02e6a3e)] - **debugger**: remove unneeded callback check (Rich Trott) [#5319](https://github.com/nodejs/node/pull/5319)
* \[[`19ae308867`](https://github.com/nodejs/node/commit/19ae308867)] - **deps**: update openssl config (Shigeki Ohtsu) [#5630](https://github.com/nodejs/node/pull/5630)
* \[[`d7b81b5bc7`](https://github.com/nodejs/node/commit/d7b81b5bc7)] - **deps**: cherry-pick 2e4da65 from v8's 4.8 upstream (Michael Dawson) [#5293](https://github.com/nodejs/node/pull/5293)
* \[[`1e05f371d6`](https://github.com/nodejs/node/commit/1e05f371d6)] - **doc**: fix typo in synchronous randomBytes example (Andrea Giammarchi) [#5781](https://github.com/nodejs/node/pull/5781)
* \[[`5f54bd2088`](https://github.com/nodejs/node/commit/5f54bd2088)] - **doc**: topic blocking vs non-blocking (Jarrett Widman) [#5326](https://github.com/nodejs/node/pull/5326)
* \[[`0943001563`](https://github.com/nodejs/node/commit/0943001563)] - **doc**: fix invalid path doc comments (Rich Trott) [#5797](https://github.com/nodejs/node/pull/5797)
* \[[`bb423bb1e6`](https://github.com/nodejs/node/commit/bb423bb1e6)] - **doc**: update release tweet template (Jeremiah Senkpiel) [#5628](https://github.com/nodejs/node/pull/5628)
* \[[`1e877f10aa`](https://github.com/nodejs/node/commit/1e877f10aa)] - **doc**: fix typo in child\_process docs (Benjamin Gruenbaum) [#5681](https://github.com/nodejs/node/pull/5681)
* \[[`d53dcc599b`](https://github.com/nodejs/node/commit/d53dcc599b)] - **doc**: update fansworld-claudio username on README (Claudio Rodriguez) [#5680](https://github.com/nodejs/node/pull/5680)
* \[[`4332f8011e`](https://github.com/nodejs/node/commit/4332f8011e)] - **doc**: fix return value of write methods (Felix Böhm) [#5736](https://github.com/nodejs/node/pull/5736)
* \[[`e572542de5`](https://github.com/nodejs/node/commit/e572542de5)] - **doc**: Add note about use of JSON.stringify() (Mithun Patel) [#5723](https://github.com/nodejs/node/pull/5723)
* \[[`daf3ef66ef`](https://github.com/nodejs/node/commit/daf3ef66ef)] - **doc**: explain path.format() algorithm (Rich Trott) [#5688](https://github.com/nodejs/node/pull/5688)
* \[[`f6d4982aa0`](https://github.com/nodejs/node/commit/f6d4982aa0)] - **doc**: clarify type of first argument in zlib (Kirill Fomichev) [#5685](https://github.com/nodejs/node/pull/5685)
* \[[`07e71b2d44`](https://github.com/nodejs/node/commit/07e71b2d44)] - **doc**: fix typo in api/addons (Daijiro Wachi) [#5678](https://github.com/nodejs/node/pull/5678)
* \[[`c6dc56175b`](https://github.com/nodejs/node/commit/c6dc56175b)] - **doc**: remove non-standard use of hyphens (Stefano Vozza)
* \[[`4c92316972`](https://github.com/nodejs/node/commit/4c92316972)] - **doc**: add fansworld-claudio to collaborators (Claudio Rodriguez) [#5668](https://github.com/nodejs/node/pull/5668)
* \[[`0a6e883f85`](https://github.com/nodejs/node/commit/0a6e883f85)] - **doc**: add thekemkid to collaborators (Glen Keane) [#5667](https://github.com/nodejs/node/pull/5667)
* \[[`39c7d8a972`](https://github.com/nodejs/node/commit/39c7d8a972)] - **doc**: add AndreasMadsen to collaborators (Andreas Madsen) [#5666](https://github.com/nodejs/node/pull/5666)
* \[[`eec3008970`](https://github.com/nodejs/node/commit/eec3008970)] - **doc**: add whitlockjc to collaborators (Jeremy Whitlock) [#5665](https://github.com/nodejs/node/pull/5665)
* \[[`e5f254d83c`](https://github.com/nodejs/node/commit/e5f254d83c)] - **doc**: add benjamingr to collaborator list (Benjamin Gruenbaum) [#5664](https://github.com/nodejs/node/pull/5664)
* \[[`3f718643c9`](https://github.com/nodejs/node/commit/3f718643c9)] - **doc**: add phillipj to collaborators (Phillip Johnsen) [#5663](https://github.com/nodejs/node/pull/5663)
* \[[`2d5527fe69`](https://github.com/nodejs/node/commit/2d5527fe69)] - **doc**: add mattloring to collaborators (Matt Loring) [#5662](https://github.com/nodejs/node/pull/5662)
* \[[`51763462bc`](https://github.com/nodejs/node/commit/51763462bc)] - **doc**: include typo in 'unhandledRejection' example (Robert C Jensen) [#5654](https://github.com/nodejs/node/pull/5654)
* \[[`cae5da2f0a`](https://github.com/nodejs/node/commit/cae5da2f0a)] - **doc**: fix markdown links (Steve Mao) [#5641](https://github.com/nodejs/node/pull/5641)
* \[[`b1b17efcb7`](https://github.com/nodejs/node/commit/b1b17efcb7)] - **doc**: move build instructions to a new document (Johan Bergström) [#5634](https://github.com/nodejs/node/pull/5634)
* \[[`13a8bde1fa`](https://github.com/nodejs/node/commit/13a8bde1fa)] - **doc**: fix dns.resolveCname description typo (axvm) [#5622](https://github.com/nodejs/node/pull/5622)
* \[[`1faea43c40`](https://github.com/nodejs/node/commit/1faea43c40)] - **doc**: fix typo in fs.symlink (Michaël Zasso) [#5560](https://github.com/nodejs/node/pull/5560)
* \[[`98a1bb6989`](https://github.com/nodejs/node/commit/98a1bb6989)] - **doc**: document directories in test directory (Michael Barrett) [#5557](https://github.com/nodejs/node/pull/5557)
* \[[`04d3f8a741`](https://github.com/nodejs/node/commit/04d3f8a741)] - **doc**: update link green to match homepage (silverwind) [#5548](https://github.com/nodejs/node/pull/5548)
* \[[`1afab6ac9c`](https://github.com/nodejs/node/commit/1afab6ac9c)] - **doc**: add clarification on birthtime in fs stat (Kári Tristan Helgason) [#5479](https://github.com/nodejs/node/pull/5479)
* \[[`d871ae2349`](https://github.com/nodejs/node/commit/d871ae2349)] - **doc**: fix typo in child\_process documentation (Evan Lucas) [#5474](https://github.com/nodejs/node/pull/5474)
* \[[`97a18bdbad`](https://github.com/nodejs/node/commit/97a18bdbad)] - **doc**: update NAN urls in ROADMAP.md and doc/releases.md (ronkorving) [#5472](https://github.com/nodejs/node/pull/5472)
* \[[`d4a1fc7acd`](https://github.com/nodejs/node/commit/d4a1fc7acd)] - **doc**: add Testing WG (Rich Trott) [#5461](https://github.com/nodejs/node/pull/5461)
* \[[`1642078580`](https://github.com/nodejs/node/commit/1642078580)] - **doc**: fix crypto function indentation level (Brian White) [#5460](https://github.com/nodejs/node/pull/5460)
* \[[`2b0c7ad985`](https://github.com/nodejs/node/commit/2b0c7ad985)] - **doc**: fix links in tls, cluster docs (Alexander Makarenko) [#5364](https://github.com/nodejs/node/pull/5364)
* \[[`901dbabea6`](https://github.com/nodejs/node/commit/901dbabea6)] - **doc**: fix relative links in net docs (Evan Lucas) [#5358](https://github.com/nodejs/node/pull/5358)
* \[[`38d429172d`](https://github.com/nodejs/node/commit/38d429172d)] - **doc**: fix typo in pbkdf2Sync code sample (Marc Cuva) [#5306](https://github.com/nodejs/node/pull/5306)
* \[[`d4cfc6f97c`](https://github.com/nodejs/node/commit/d4cfc6f97c)] - **doc**: add missing property in cluster example (Rafael Cepeda) [#5305](https://github.com/nodejs/node/pull/5305)
* \[[`b66d6b1458`](https://github.com/nodejs/node/commit/b66d6b1458)] - **doc**: improve httpVersionMajor / httpVersionMajor (Jackson Tian) [#5296](https://github.com/nodejs/node/pull/5296)
* \[[`70c872c9c4`](https://github.com/nodejs/node/commit/70c872c9c4)] - **doc**: improve unhandledException doc copy (James M Snell) [#5287](https://github.com/nodejs/node/pull/5287)
* \[[`ba5e0b6110`](https://github.com/nodejs/node/commit/ba5e0b6110)] - **doc**: fix buf.readInt16LE output (Chinedu Francis Nwafili) [#5282](https://github.com/nodejs/node/pull/5282)
* \[[`1624d5b049`](https://github.com/nodejs/node/commit/1624d5b049)] - **doc**: document base64url encoding support (Tristan Slominski) [#5243](https://github.com/nodejs/node/pull/5243)
* \[[`b1d580c9d2`](https://github.com/nodejs/node/commit/b1d580c9d2)] - **doc**: update removeListener behaviour (Vaibhav) [#5201](https://github.com/nodejs/node/pull/5201)
* \[[`ca17f91ba8`](https://github.com/nodejs/node/commit/ca17f91ba8)] - **doc**: add note for binary safe string reading (Anton Andesen) [#5155](https://github.com/nodejs/node/pull/5155)
* \[[`0830bb4950`](https://github.com/nodejs/node/commit/0830bb4950)] - **doc**: clarify when writable.write callback is called (Kevin Locke) [#4810](https://github.com/nodejs/node/pull/4810)
* \[[`17a74305c8`](https://github.com/nodejs/node/commit/17a74305c8)] - **doc**: add info to docs on how to submit docs patch (Sequoia McDowell) [#4591](https://github.com/nodejs/node/pull/4591)
* \[[`470a9ca909`](https://github.com/nodejs/node/commit/470a9ca909)] - **doc**: add onboarding resources (Jeremiah Senkpiel) [#3726](https://github.com/nodejs/node/pull/3726)
* \[[`3168e6b486`](https://github.com/nodejs/node/commit/3168e6b486)] - **doc**: update V8 URL (Craig Akimoto) [#5530](https://github.com/nodejs/node/pull/5530)
* \[[`04d16eb7e8`](https://github.com/nodejs/node/commit/04d16eb7e8)] - **doc**: document fs.datasync(Sync) (Ron Korving) [#5402](https://github.com/nodejs/node/pull/5402)
* \[[`29646200f8`](https://github.com/nodejs/node/commit/29646200f8)] - **doc**: add Evan Lucas to the CTC (Rod Vagg)
* \[[`a2a32b7810`](https://github.com/nodejs/node/commit/a2a32b7810)] - **doc**: add Rich Trott to the CTC (Rod Vagg) [#5276](https://github.com/nodejs/node/pull/5276)
* \[[`4e469d5e47`](https://github.com/nodejs/node/commit/4e469d5e47)] - **doc**: add Ali Ijaz Sheikh to the CTC (Rod Vagg) [#5277](https://github.com/nodejs/node/pull/5277)
* \[[`d09b44f59b`](https://github.com/nodejs/node/commit/d09b44f59b)] - **doc**: add Сковорода Никита Андреевич to the CTC (Rod Vagg) [#5278](https://github.com/nodejs/node/pull/5278)
* \[[`ebbc64bc97`](https://github.com/nodejs/node/commit/ebbc64bc97)] - **doc**: add "building node with ninja" guide (Jeremiah Senkpiel) [#4767](https://github.com/nodejs/node/pull/4767)
* \[[`67245fa0e3`](https://github.com/nodejs/node/commit/67245fa0e3)] - **doc**: clarify code of conduct reporting (Julie Pagano) [#5107](https://github.com/nodejs/node/pull/5107)
* \[[`cd78ff9706`](https://github.com/nodejs/node/commit/cd78ff9706)] - **doc**: fix links in Addons docs (Alexander Makarenko) [#5072](https://github.com/nodejs/node/pull/5072)
* \[[`20539954ff`](https://github.com/nodejs/node/commit/20539954ff)] - **docs**: fix man pages link if tok type is code (Mithun Patel) [#5721](https://github.com/nodejs/node/pull/5721)
* \[[`38d7b0b6ea`](https://github.com/nodejs/node/commit/38d7b0b6ea)] - **docs**: update link to iojs+release ci job (Myles Borins) [#5632](https://github.com/nodejs/node/pull/5632)
* \[[`f982632f90`](https://github.com/nodejs/node/commit/f982632f90)] - **http**: remove old, confusing comment (Brian White) [#5233](https://github.com/nodejs/node/pull/5233)
* \[[`ca5d7a8bb6`](https://github.com/nodejs/node/commit/ca5d7a8bb6)] - **http**: remove unnecessary check (Brian White) [#5233](https://github.com/nodejs/node/pull/5233)
* \[[`2ce83bd8f9`](https://github.com/nodejs/node/commit/2ce83bd8f9)] - **http,util**: fix typos in comments (Alexander Makarenko) [#5279](https://github.com/nodejs/node/pull/5279)
* \[[`b690916e5a`](https://github.com/nodejs/node/commit/b690916e5a)] - **lib**: freelist: use .pop() for allocation (Anton Khlynovskiy) [#2174](https://github.com/nodejs/node/pull/2174)
* \[[`e7f45f0a17`](https://github.com/nodejs/node/commit/e7f45f0a17)] - **repl**: handle quotes within regexp literal (Prince J Wesley) [#5117](https://github.com/nodejs/node/pull/5117)
* \[[`7c3b844f78`](https://github.com/nodejs/node/commit/7c3b844f78)] - **src**: return UV\_EAI\_NODATA on empty lookup (cjihrig) [#4715](https://github.com/nodejs/node/pull/4715)
* \[[`242a65e930`](https://github.com/nodejs/node/commit/242a65e930)] - **stream**: prevent object map change in TransformState (Evan Lucas) [#5032](https://github.com/nodejs/node/pull/5032)
* \[[`fb5ba6b928`](https://github.com/nodejs/node/commit/fb5ba6b928)] - **stream**: prevent object map change in ReadableState (Evan Lucas) [#4761](https://github.com/nodejs/node/pull/4761)
* \[[`04db9efd78`](https://github.com/nodejs/node/commit/04db9efd78)] - **stream**: fix no data on partial decode (Brian White) [#5226](https://github.com/nodejs/node/pull/5226)
* \[[`cc0e36ff98`](https://github.com/nodejs/node/commit/cc0e36ff98)] - **string\_decoder**: fix performance regression (Brian White) [#5134](https://github.com/nodejs/node/pull/5134)
* \[[`666d3690d8`](https://github.com/nodejs/node/commit/666d3690d8)] - **test**: eval a strict function (Kári Tristan Helgason) [#5250](https://github.com/nodejs/node/pull/5250)
* \[[`9952bcf203`](https://github.com/nodejs/node/commit/9952bcf203)] - **test**: bug repro for vm function redefinition (cjihrig) [#5528](https://github.com/nodejs/node/pull/5528)
* \[[`063f22f1f0`](https://github.com/nodejs/node/commit/063f22f1f0)] - **test**: check memoryUsage properties The properties on memoryUsage were not checked before, this commit checks them. (Wyatt Preul) [#5546](https://github.com/nodejs/node/pull/5546)
* \[[`7a0fcfc127`](https://github.com/nodejs/node/commit/7a0fcfc127)] - **test**: remove broken debugger scenarios (Rich Trott) [#5532](https://github.com/nodejs/node/pull/5532)
* \[[`ba9ad2662c`](https://github.com/nodejs/node/commit/ba9ad2662c)] - **test**: apply Linux workaround to Linux only (Rich Trott) [#5471](https://github.com/nodejs/node/pull/5471)
* \[[`4aa2c03d31`](https://github.com/nodejs/node/commit/4aa2c03d31)] - **test**: increase timeout for test-tls-fast-writing (Rich Trott) [#5466](https://github.com/nodejs/node/pull/5466)
* \[[`b4ef644ce4`](https://github.com/nodejs/node/commit/b4ef644ce4)] - **test**: retry on known SmartOS bug (Rich Trott) [#5454](https://github.com/nodejs/node/pull/5454)
* \[[`d681bf24b5`](https://github.com/nodejs/node/commit/d681bf24b5)] - **test**: fix flaky child-process-fork-regr-gh-2847 (Santiago Gimeno) [#5422](https://github.com/nodejs/node/pull/5422)
* \[[`b4fbe04514`](https://github.com/nodejs/node/commit/b4fbe04514)] - **test**: fix test-timers.reliability on OS X (Rich Trott) [#5379](https://github.com/nodejs/node/pull/5379)
* \[[`99269ffdbf`](https://github.com/nodejs/node/commit/99269ffdbf)] - **test**: increase timeouts on some unref timers tests (Jeremiah Senkpiel) [#5352](https://github.com/nodejs/node/pull/5352)
* \[[`85f927a774`](https://github.com/nodejs/node/commit/85f927a774)] - **test**: prevent flakey test on pi2 (Trevor Norris) [#5537](https://github.com/nodejs/node/pull/5537)
* \[[`c86902d800`](https://github.com/nodejs/node/commit/c86902d800)] - **test**: mitigate flaky test-http-agent (Rich Trott) [#5346](https://github.com/nodejs/node/pull/5346)
* \[[`f242e62817`](https://github.com/nodejs/node/commit/f242e62817)] - **test**: remove flaky designation from fixed tests (Rich Trott) [#5459](https://github.com/nodejs/node/pull/5459)
* \[[`a39aacf035`](https://github.com/nodejs/node/commit/a39aacf035)] - **test**: refactor test-dgram-udp4 (Santiago Gimeno) [#5339](https://github.com/nodejs/node/pull/5339)
* \[[`6386f62221`](https://github.com/nodejs/node/commit/6386f62221)] - **test**: remove unneeded bind() and related comments (Aayush Naik) [#5023](https://github.com/nodejs/node/pull/5023)
* \[[`068b0cbd12`](https://github.com/nodejs/node/commit/068b0cbd12)] - **test**: move cluster tests to parallel (Rich Trott) [#4774](https://github.com/nodejs/node/pull/4774)
* \[[`a673c9ae2d`](https://github.com/nodejs/node/commit/a673c9ae2d)] - **tls**: fix assert in context.\_external accessor (Ben Noordhuis) [#5521](https://github.com/nodejs/node/pull/5521)
* \[[`8ffef48fee`](https://github.com/nodejs/node/commit/8ffef48fee)] - **tools**: fix gyp to work on MacOSX without XCode (Shigeki Ohtsu) [nodejs/node#1325](https://github.com/nodejs/node/pull/1325)
* \[[`4b6a8f4321`](https://github.com/nodejs/node/commit/4b6a8f4321)] - **tools**: update gyp to b3cef02 (Imran Iqbal) [#3487](https://github.com/nodejs/node/pull/3487)
* \[[`7501ddc878`](https://github.com/nodejs/node/commit/7501ddc878)] - **tools**: support testing known issues (cjihrig) [#5528](https://github.com/nodejs/node/pull/5528)
* \[[`10ec1d2a6b`](https://github.com/nodejs/node/commit/10ec1d2a6b)] - **tools**: enable linting for benchmarks (Rich Trott) [#5773](https://github.com/nodejs/node/pull/5773)
* \[[`deec8bc5f5`](https://github.com/nodejs/node/commit/deec8bc5f5)] - **tools**: reduce verbosity of cpplint (Sakthipriyan Vairamani) [#5578](https://github.com/nodejs/node/pull/5578)
* \[[`64d5752711`](https://github.com/nodejs/node/commit/64d5752711)] - **tools**: enable no-self-assign ESLint rule (Rich Trott) [#5552](https://github.com/nodejs/node/pull/5552)
* \[[`131ed494e2`](https://github.com/nodejs/node/commit/131ed494e2)] - **tools**: enable no-extra-parens in ESLint (Rich Trott) [#5512](https://github.com/nodejs/node/pull/5512)
* \[[`d4b9f02fdc`](https://github.com/nodejs/node/commit/d4b9f02fdc)] - **tools**: apply custom buffer lint rule to /lib only (Rich Trott) [#5371](https://github.com/nodejs/node/pull/5371)
* \[[`6867bed4c4`](https://github.com/nodejs/node/commit/6867bed4c4)] - **tools**: enable additional lint rules (Rich Trott) [#5357](https://github.com/nodejs/node/pull/5357)
* \[[`5e6b7605ee`](https://github.com/nodejs/node/commit/5e6b7605ee)] - **tools**: add Node.js-specific ESLint rules (Rich Trott) [#5320](https://github.com/nodejs/node/pull/5320)
* \[[`6dc49ae203`](https://github.com/nodejs/node/commit/6dc49ae203)] - **tools,benchmark**: increase lint compliance (Rich Trott) [#5773](https://github.com/nodejs/node/pull/5773)
* \[[`dff7091fce`](https://github.com/nodejs/node/commit/dff7091fce)] - **url**: group slashed protocols by protocol name (nettofarah) [#5380](https://github.com/nodejs/node/pull/5380)
* \[[`0e97a3ea51`](https://github.com/nodejs/node/commit/0e97a3ea51)] - **win,build**: support Visual C++ Build Tools 2015 (João Reis) [#5627](https://github.com/nodejs/node/pull/5627)

<a id="4.4.0"></a>

## 2016-03-08, Version 4.4.0 'Argon' (LTS), @thealphanerd

In December we announced that we would be doing a minor release in order to
get a number of voted on SEMVER-MINOR changes into LTS. Our ability to release this
was delayed due to the unforeseen security release v4.3. We are quickly bumping to
v4.4 in order to bring you the features that we had committed to releasing.

This release also includes over 70 fixes to our docs and over 50 fixes to tests.

### Notable changes

The SEMVER-MINOR changes include:

* **deps**:
  * An update to v8 that introduces a new flag --perf\_basic\_prof\_only\_functions (Ali Ijaz Sheikh) [#3609](https://github.com/nodejs/node/pull/3609)
* **http**:
  * A new feature in http(s) agent that catches errors on _keep alived_ connections (José F. Romaniello) [#4482](https://github.com/nodejs/node/pull/4482)
* **src**:
  * Better support for Big-Endian systems (Bryon Leung) [#3410](https://github.com/nodejs/node/pull/3410)
* **tls**:
  * A new feature that allows you to pass common SSL options to `tls.createSecurePair` (Коренберг Марк) [#2441](https://github.com/nodejs/node/pull/2441)
* **tools**:
  * a new flag `--prof-process` which will execute the tick processor on the provided isolate files (Matt Loring) [#4021](https://github.com/nodejs/node/pull/4021)

Notable semver patch changes include:

* **buld**:
  * Support python path that includes spaces. This should be of particular interest to our Windows users who may have python living in `c:/Program Files` (Felix Becker) [#4841](https://github.com/nodejs/node/pull/4841)
* **https**:
  * A potential fix for [#3692](https://github.com/nodejs/node/issues/3692) HTTP/HTTPS client requests throwing EPROTO (Fedor Indutny) [#4982](https://github.com/nodejs/node/pull/4982)
* **installer**:
  * More readable profiling information from isolate tick logs (Matt Loring) [#3032](https://github.com/nodejs/node/pull/3032)
* **npm**:
  * upgrade to npm 2.14.20 (Kat Marchán) [#5510](https://github.com/nodejs/node/pull/5510)
* **process**:
  * Add support for symbols in event emitters. Symbols didn't exist when it was written ¯\_(ツ)\_/¯ (cjihrig) [#4798](https://github.com/nodejs/node/pull/4798)
* **querystring**:
  * querystring.parse() is now 13-22% faster! (Brian White) [#4675](https://github.com/nodejs/node/pull/4675)
* **streams**:
  * performance improvements for moving small buffers that shows a 5% throughput gain. IoT projects have been seen to be as much as 10% faster with this change! (Matteo Collina) [#4354](https://github.com/nodejs/node/pull/4354)
* **tools**:
  * eslint has been updated to version 2.1.0 (Rich Trott) [#5214](https://github.com/nodejs/node/pull/5214)

### Commits

* \[[`360e04fd5a`](https://github.com/nodejs/node/commit/360e04fd5a)] - internal/child\_process: call postSend on error (Fedor Indutny) [#4752](https://github.com/nodejs/node/pull/4752)
* \[[`a29f501aa2`](https://github.com/nodejs/node/commit/a29f501aa2)] - **benchmark**: add a constant declaration for `net` (Minwoo Jung) [#3950](https://github.com/nodejs/node/pull/3950)
* \[[`85e06a2e34`](https://github.com/nodejs/node/commit/85e06a2e34)] - **(SEMVER-MINOR)** **buffer**: allow encoding param to collapse (Trevor Norris) [#4803](https://github.com/nodejs/node/pull/4803)
* \[[`fe893a8ebc`](https://github.com/nodejs/node/commit/fe893a8ebc)] - **(SEMVER-MINOR)** **buffer**: properly retrieve binary length of needle (Trevor Norris) [#4803](https://github.com/nodejs/node/pull/4803)
* \[[`fae7c9db3f`](https://github.com/nodejs/node/commit/fae7c9db3f)] - **buffer**: refactor redeclared variables (Rich Trott) [#4886](https://github.com/nodejs/node/pull/4886)
* \[[`4a6e2b26f7`](https://github.com/nodejs/node/commit/4a6e2b26f7)] - **build**: treat aarch64 as arm64 (Johan Bergström) [#5191](https://github.com/nodejs/node/pull/5191)
* \[[`bc2536dfc6`](https://github.com/nodejs/node/commit/bc2536dfc6)] - **build**: add a help message and removed a TODO. (Ojas Shirekar) [#5080](https://github.com/nodejs/node/pull/5080)
* \[[`f6416be5d2`](https://github.com/nodejs/node/commit/f6416be5d2)] - **build**: remove redundant TODO in configure (Ojas Shirekar) [#5080](https://github.com/nodejs/node/pull/5080)
* \[[`6deb7a6eb8`](https://github.com/nodejs/node/commit/6deb7a6eb8)] - **build**: remove Makefile.build (Ojas Shirekar) [#5080](https://github.com/nodejs/node/pull/5080)
* \[[`66d1115555`](https://github.com/nodejs/node/commit/66d1115555)] - **build**: fix build when python path contains spaces (Felix Becker) [#4841](https://github.com/nodejs/node/pull/4841)
* \[[`29951cf36a`](https://github.com/nodejs/node/commit/29951cf36a)] - **child\_process**: fix data loss with readable event (Brian White) [#5036](https://github.com/nodejs/node/pull/5036)
* \[[`81d4127279`](https://github.com/nodejs/node/commit/81d4127279)] - **cluster**: dont rely on `this` in `fork` (Igor Klopov) [#5216](https://github.com/nodejs/node/pull/5216)
* \[[`de4c07b29e`](https://github.com/nodejs/node/commit/de4c07b29e)] - **console**: apply null as `this` for util.format (Jackson Tian) [#5222](https://github.com/nodejs/node/pull/5222)
* \[[`4e0755cab3`](https://github.com/nodejs/node/commit/4e0755cab3)] - **crypto**: have fixed NodeBIOs return EOF (Adam Langley) [#5105](https://github.com/nodejs/node/pull/5105)
* \[[`a7955d5071`](https://github.com/nodejs/node/commit/a7955d5071)] - **crypto**: fix memory leak in LoadPKCS12 (Fedor Indutny) [#5109](https://github.com/nodejs/node/pull/5109)
* \[[`5d9c1cf001`](https://github.com/nodejs/node/commit/5d9c1cf001)] - **crypto**: add `pfx` certs as CA certs too (Fedor Indutny) [#5109](https://github.com/nodejs/node/pull/5109)
* \[[`ab5cb0539b`](https://github.com/nodejs/node/commit/ab5cb0539b)] - **crypto**: use SSL\_CTX\_clear\_extra\_chain\_certs. (Adam Langley) [#4919](https://github.com/nodejs/node/pull/4919)
* \[[`198928eb9f`](https://github.com/nodejs/node/commit/198928eb9f)] - **crypto**: fix build when OCSP-stapling not provided (Adam Langley) [#4914](https://github.com/nodejs/node/pull/4914)
* \[[`b8e1089df0`](https://github.com/nodejs/node/commit/b8e1089df0)] - **crypto**: use a const SSL\_CIPHER (Adam Langley) [#4913](https://github.com/nodejs/node/pull/4913)
* \[[`139d6d9284`](https://github.com/nodejs/node/commit/139d6d9284)] - **debugger**: assert test before accessing this.binding (Prince J Wesley) [#5145](https://github.com/nodejs/node/pull/5145)
* \[[`9c8f2ab546`](https://github.com/nodejs/node/commit/9c8f2ab546)] - **deps**: upgrade to npm 2.14.20 (Kat Marchán) [#5510](https://github.com/nodejs/node/pull/5510)
* \[[`e591a0927f`](https://github.com/nodejs/node/commit/e591a0927f)] - **deps**: upgrade to npm 2.14.19 (Kat Marchán) [#5335](https://github.com/nodejs/node/pull/5335)
* \[[`a5ce67a0aa`](https://github.com/nodejs/node/commit/a5ce67a0aa)] - **deps**: upgrade to npm 2.14.18 (Kat Marchán) [#5245](https://github.com/nodejs/node/pull/5245)
* \[[`469db021f7`](https://github.com/nodejs/node/commit/469db021f7)] - **(SEMVER-MINOR)** **deps**: backport 9da3ab6 from V8 upstream (Ali Ijaz Sheikh) [#3609](https://github.com/nodejs/node/pull/3609)
* \[[`3ca04a5de9`](https://github.com/nodejs/node/commit/3ca04a5de9)] - **deps**: backport 8d00c2c from v8 upstream (Gibson Fahnestock) [#5024](https://github.com/nodejs/node/pull/5024)
* \[[`60e0bd4be9`](https://github.com/nodejs/node/commit/60e0bd4be9)] - **deps**: upgrade to npm 2.14.17 (Kat Marchán) [#5110](https://github.com/nodejs/node/pull/5110)
* \[[`976b9a9ab3`](https://github.com/nodejs/node/commit/976b9a9ab3)] - **deps**: upgrade to npm 2.14.16 (Kat Marchán) [#4960](https://github.com/nodejs/node/pull/4960)
* \[[`38b370abea`](https://github.com/nodejs/node/commit/38b370abea)] - **deps**: upgrade to npm 2.14.15 (Kat Marchán) [#4872](https://github.com/nodejs/node/pull/4872)
* \[[`82f549ef81`](https://github.com/nodejs/node/commit/82f549ef81)] - **dgram**: scope redeclared variables (Rich Trott) [#4940](https://github.com/nodejs/node/pull/4940)
* \[[`063e14b568`](https://github.com/nodejs/node/commit/063e14b568)] - **dns**: throw a TypeError in lookupService with invalid port (Evan Lucas) [#4839](https://github.com/nodejs/node/pull/4839)
* \[[`a2613aefae`](https://github.com/nodejs/node/commit/a2613aefae)] - **doc**: remove out-of-date matter from internal docs (Rich Trott) [#5421](https://github.com/nodejs/node/pull/5421)
* \[[`394743f4b3`](https://github.com/nodejs/node/commit/394743f4b3)] - **doc**: explicit about VS 2015 support in readme (Phillip Johnsen) [#5406](https://github.com/nodejs/node/pull/5406)
* \[[`da6b26fbfb`](https://github.com/nodejs/node/commit/da6b26fbfb)] - **doc**: copyedit util doc (Rich Trott) [#5399](https://github.com/nodejs/node/pull/5399)
* \[[`7070ad0cc0`](https://github.com/nodejs/node/commit/7070ad0cc0)] - **doc**: mention prototype check in deepStrictEqual() (cjihrig) [#5367](https://github.com/nodejs/node/pull/5367)
* \[[`d4789fc5fd`](https://github.com/nodejs/node/commit/d4789fc5fd)] - **doc**: s/http/https in Myles Borins' GitHub link (Rod Vagg) [#5356](https://github.com/nodejs/node/pull/5356)
* \[[`b86540d1eb`](https://github.com/nodejs/node/commit/b86540d1eb)] - **doc**: clarify error handling in net.createServer (Dirceu Pereira Tiegs) [#5353](https://github.com/nodejs/node/pull/5353)
* \[[`3106297037`](https://github.com/nodejs/node/commit/3106297037)] - **doc**: `require` behavior on case-insensitive systems (Hugo Wood)
* \[[`e0b45e4315`](https://github.com/nodejs/node/commit/e0b45e4315)] - **doc**: update repo docs to use 'CTC' (Alexis Campailla) [#5304](https://github.com/nodejs/node/pull/5304)
* \[[`e355f13989`](https://github.com/nodejs/node/commit/e355f13989)] - **doc**: improvements to crypto.markdown copy (Alexander Makarenko) [#5230](https://github.com/nodejs/node/pull/5230)
* \[[`a9035b5e1d`](https://github.com/nodejs/node/commit/a9035b5e1d)] - **doc**: link to man pages (<dcposch@dcpos.ch>) [#5073](https://github.com/nodejs/node/pull/5073)
* \[[`2043e6a63c`](https://github.com/nodejs/node/commit/2043e6a63c)] - **doc**: clarify child\_process.execFile{,Sync} file arg (Kevin Locke) [#5310](https://github.com/nodejs/node/pull/5310)
* \[[`8c732ad1e1`](https://github.com/nodejs/node/commit/8c732ad1e1)] - **doc**: fix buf.length slice example (Chinedu Francis Nwafili) [#5259](https://github.com/nodejs/node/pull/5259)
* \[[`6c27c78b8b`](https://github.com/nodejs/node/commit/6c27c78b8b)] - **doc**: fix buffer\[index] example (Chinedu Francis Nwafili) [#5253](https://github.com/nodejs/node/pull/5253)
* \[[`7765f99683`](https://github.com/nodejs/node/commit/7765f99683)] - **doc**: fix template string (Rafael Cepeda) [#5240](https://github.com/nodejs/node/pull/5240)
* \[[`d15ef20162`](https://github.com/nodejs/node/commit/d15ef20162)] - **doc**: improvements to console.markdown copy (Alexander Makarenko) [#5225](https://github.com/nodejs/node/pull/5225)
* \[[`593206a752`](https://github.com/nodejs/node/commit/593206a752)] - **doc**: fix net.createConnection() example (Brian White) [#5219](https://github.com/nodejs/node/pull/5219)
* \[[`464636b5c5`](https://github.com/nodejs/node/commit/464636b5c5)] - **doc**: improve scrolling, various CSS tweaks (Roman Reiss) [#5198](https://github.com/nodejs/node/pull/5198)
* \[[`f615cd5b0b`](https://github.com/nodejs/node/commit/f615cd5b0b)] - **doc**: console is asynchronous unless it's a file (Ben Noordhuis) [#5133](https://github.com/nodejs/node/pull/5133)
* \[[`fbed0d11f1`](https://github.com/nodejs/node/commit/fbed0d11f1)] - **doc**: merging behavior of writeHead vs setHeader (Alejandro Oviedo) [#5081](https://github.com/nodejs/node/pull/5081)
* \[[`b0bb42bd7d`](https://github.com/nodejs/node/commit/b0bb42bd7d)] - **doc**: fix reference to API `hash.final` (Minwoo Jung) [#5050](https://github.com/nodejs/node/pull/5050)
* \[[`dee5045221`](https://github.com/nodejs/node/commit/dee5045221)] - **doc**: uppercase 'RSA-SHA256' in crypto.markdown (Rainer Oviir) [#5044](https://github.com/nodejs/node/pull/5044)
* \[[`498052a017`](https://github.com/nodejs/node/commit/498052a017)] - **doc**: consistent styling for functions in TLS docs (Alexander Makarenko) [#5000](https://github.com/nodejs/node/pull/5000)
* \[[`031277e6f8`](https://github.com/nodejs/node/commit/031277e6f8)] - **doc**: apply consistent styling for functions (Rich Trott) [#4974](https://github.com/nodejs/node/pull/4974)
* \[[`808fe0ea48`](https://github.com/nodejs/node/commit/808fe0ea48)] - **doc**: fix `notDeepEqual` API (Minwoo Jung) [#4971](https://github.com/nodejs/node/pull/4971)
* \[[`5b9025689f`](https://github.com/nodejs/node/commit/5b9025689f)] - **doc**: show links consistently in deprecations (Sakthipriyan Vairamani) [#4907](https://github.com/nodejs/node/pull/4907)
* \[[`3a1865db5e`](https://github.com/nodejs/node/commit/3a1865db5e)] - **doc**: don't use "interface" as a variable name (ChALkeR) [#4900](https://github.com/nodejs/node/pull/4900)
* \[[`90715c3d68`](https://github.com/nodejs/node/commit/90715c3d68)] - **doc**: keep the names in sorted order (Sakthipriyan Vairamani) [#4876](https://github.com/nodejs/node/pull/4876)
* \[[`d8b3b25c9c`](https://github.com/nodejs/node/commit/d8b3b25c9c)] - **doc**: fix JSON generation for aliased methods (Timothy Gu) [#4871](https://github.com/nodejs/node/pull/4871)
* \[[`7b763c8d25`](https://github.com/nodejs/node/commit/7b763c8d25)] - **doc**: fix code type of markdowns (Jackson Tian) [#4858](https://github.com/nodejs/node/pull/4858)
* \[[`37d4e7afc2`](https://github.com/nodejs/node/commit/37d4e7afc2)] - **doc**: check for errors in 'listen' event (Benjamin Gruenbaum) [#4834](https://github.com/nodejs/node/pull/4834)
* \[[`3f876b104c`](https://github.com/nodejs/node/commit/3f876b104c)] - **doc**: Examples work when data exceeds buffer size (Glen Arrowsmith) [#4811](https://github.com/nodejs/node/pull/4811)
* \[[`e3e20422a7`](https://github.com/nodejs/node/commit/e3e20422a7)] - **doc**: harmonize $ node command line notation (Robert Jefe Lindstaedt) [#4806](https://github.com/nodejs/node/pull/4806)
* \[[`73e0195cef`](https://github.com/nodejs/node/commit/73e0195cef)] - **doc**: fix type references for link gen, link css (Claudio Rodriguez) [#4741](https://github.com/nodejs/node/pull/4741)
* \[[`0bdac429e1`](https://github.com/nodejs/node/commit/0bdac429e1)] - **doc**: multiple improvements in Stream docs (Alexander Makarenko) [#5009](https://github.com/nodejs/node/pull/5009)
* \[[`693c16fb6b`](https://github.com/nodejs/node/commit/693c16fb6b)] - **doc**: fix anchor links from stream to http and events (piepmatz) [#5007](https://github.com/nodejs/node/pull/5007)
* \[[`5fb533522c`](https://github.com/nodejs/node/commit/5fb533522c)] - **doc**: replace function expressions with arrows (Benjamin Gruenbaum) [#4832](https://github.com/nodejs/node/pull/4832)
* \[[`e3572fb809`](https://github.com/nodejs/node/commit/e3572fb809)] - **doc**: fix links order in Buffer doc (Alexander Makarenko) [#5076](https://github.com/nodejs/node/pull/5076)
* \[[`5c936ab765`](https://github.com/nodejs/node/commit/5c936ab765)] - **doc**: clarify optional arguments of Buffer methods (Michaël Zasso) [#5008](https://github.com/nodejs/node/pull/5008)
* \[[`6df350c2b3`](https://github.com/nodejs/node/commit/6df350c2b3)] - **doc**: improve styling consistency in Buffer docs (Alexander Makarenko) [#5001](https://github.com/nodejs/node/pull/5001)
* \[[`047f4a157f`](https://github.com/nodejs/node/commit/047f4a157f)] - **doc**: make buffer methods styles consistent (Timothy Gu) [#4873](https://github.com/nodejs/node/pull/4873)
* \[[`4cfc017b90`](https://github.com/nodejs/node/commit/4cfc017b90)] - **doc**: fix nonsensical grammar in Buffer::write (Jimb Esser) [#4863](https://github.com/nodejs/node/pull/4863)
* \[[`9087f6daca`](https://github.com/nodejs/node/commit/9087f6daca)] - **doc**: fix named anchors in addons.markdown and http.markdown (Michael Theriot) [#4708](https://github.com/nodejs/node/pull/4708)
* \[[`4c8713ce58`](https://github.com/nodejs/node/commit/4c8713ce58)] - **doc**: add buf.indexOf encoding param with example (Karl Skomski) [#3373](https://github.com/nodejs/node/pull/3373)
* \[[`1819d74491`](https://github.com/nodejs/node/commit/1819d74491)] - **doc**: fenced all code blocks, typo fixes (Robert Jefe Lindstaedt) [#4733](https://github.com/nodejs/node/pull/4733)
* \[[`961735e645`](https://github.com/nodejs/node/commit/961735e645)] - **doc**: make references clickable (Roman Klauke) [#4654](https://github.com/nodejs/node/pull/4654)
* \[[`7e80442483`](https://github.com/nodejs/node/commit/7e80442483)] - **doc**: improve child\_process.execFile() code example (Ryan Sobol) [#4504](https://github.com/nodejs/node/pull/4504)
* \[[`de9ad5b39d`](https://github.com/nodejs/node/commit/de9ad5b39d)] - **doc**: remove "above" and "below" references (Richard Sun) [#4499](https://github.com/nodejs/node/pull/4499)
* \[[`c549ca3b69`](https://github.com/nodejs/node/commit/c549ca3b69)] - **doc**: fix heading level error in Buffer doc (Shigeki Ohtsu) [#4537](https://github.com/nodejs/node/pull/4537)
* \[[`a613bae14c`](https://github.com/nodejs/node/commit/a613bae14c)] - **doc**: improvements to crypto.markdown copy (James M Snell) [#4435](https://github.com/nodejs/node/pull/4435)
* \[[`18f580d0c1`](https://github.com/nodejs/node/commit/18f580d0c1)] - **doc**: improve child\_process.markdown copy (James M Snell) [#4383](https://github.com/nodejs/node/pull/4383)
* \[[`a929837311`](https://github.com/nodejs/node/commit/a929837311)] - **doc**: improvements to buffer.markdown copy (James M Snell) [#4370](https://github.com/nodejs/node/pull/4370)
* \[[`a22f688407`](https://github.com/nodejs/node/commit/a22f688407)] - **doc**: improve addons.markdown copy (James M Snell) [#4320](https://github.com/nodejs/node/pull/4320)
* \[[`94c2de47b1`](https://github.com/nodejs/node/commit/94c2de47b1)] - **doc**: update process.send() signature (cjihrig) [#5284](https://github.com/nodejs/node/pull/5284)
* \[[`4e1926cb08`](https://github.com/nodejs/node/commit/4e1926cb08)] - **doc**: replace node-forward link in CONTRIBUTING.md (Ben Noordhuis) [#5227](https://github.com/nodejs/node/pull/5227)
* \[[`e1713e81e5`](https://github.com/nodejs/node/commit/e1713e81e5)] - **doc**: fix minor inconsistencies in repl doc (Rich Trott) [#5193](https://github.com/nodejs/node/pull/5193)
* \[[`b2e72c0d92`](https://github.com/nodejs/node/commit/b2e72c0d92)] - **doc**: clarify exceptions during uncaughtException (Noah Rose) [#5180](https://github.com/nodejs/node/pull/5180)
* \[[`c3c549836a`](https://github.com/nodejs/node/commit/c3c549836a)] - **doc**: update DCO to v1.1 (Mikeal Rogers) [#5170](https://github.com/nodejs/node/pull/5170)
* \[[`9dd35ad594`](https://github.com/nodejs/node/commit/9dd35ad594)] - **doc**: fix dgram doc indentation (Rich Trott) [#5118](https://github.com/nodejs/node/pull/5118)
* \[[`eed830702c`](https://github.com/nodejs/node/commit/eed830702c)] - **doc**: fix typo in dgram doc (Rich Trott) [#5114](https://github.com/nodejs/node/pull/5114)
* \[[`abfb2f5864`](https://github.com/nodejs/node/commit/abfb2f5864)] - **doc**: fix link in cluster documentation (Timothy Gu) [#5068](https://github.com/nodejs/node/pull/5068)
* \[[`8b040b5bb2`](https://github.com/nodejs/node/commit/8b040b5bb2)] - **doc**: fix minor typo in process doc (Prayag Verma) [#5018](https://github.com/nodejs/node/pull/5018)
* \[[`47eebe1d80`](https://github.com/nodejs/node/commit/47eebe1d80)] - **doc**: fix typo in Readme.md (Prayag Verma) [#5017](https://github.com/nodejs/node/pull/5017)
* \[[`2b97ff89a6`](https://github.com/nodejs/node/commit/2b97ff89a6)] - **doc**: minor improvement in OS docs (Alexander Makarenko) [#5006](https://github.com/nodejs/node/pull/5006)
* \[[`9a5d58b89e`](https://github.com/nodejs/node/commit/9a5d58b89e)] - **doc**: improve styling consistency in VM docs (Alexander Makarenko) [#5005](https://github.com/nodejs/node/pull/5005)
* \[[`960e1bab98`](https://github.com/nodejs/node/commit/960e1bab98)] - **doc**: minor improvement to HTTPS doc (Alexander Makarenko) [#5002](https://github.com/nodejs/node/pull/5002)
* \[[`6048b011e8`](https://github.com/nodejs/node/commit/6048b011e8)] - **doc**: spell writable consistently (Peter Lyons) [#4954](https://github.com/nodejs/node/pull/4954)
* \[[`7b8f904167`](https://github.com/nodejs/node/commit/7b8f904167)] - **doc**: update eol handling in readline (Kári Tristan Helgason) [#4927](https://github.com/nodejs/node/pull/4927)
* \[[`83efd0d4d1`](https://github.com/nodejs/node/commit/83efd0d4d1)] - **doc**: add more details to process.env (Evan Lucas) [#4924](https://github.com/nodejs/node/pull/4924)
* \[[`b2d2c0b588`](https://github.com/nodejs/node/commit/b2d2c0b588)] - **doc**: undo move http.IncomingMessage.statusMessage (Jeff Harris) [#4822](https://github.com/nodejs/node/pull/4822)
* \[[`b091c41b53`](https://github.com/nodejs/node/commit/b091c41b53)] - **doc**: proper markdown escaping -> \_\_, \*, \_ (Robert Jefe Lindstaedt) [#4805](https://github.com/nodejs/node/pull/4805)
* \[[`0887208290`](https://github.com/nodejs/node/commit/0887208290)] - **doc**: remove unnecessary bind(this) (Dmitriy Lazarev) [#4797](https://github.com/nodejs/node/pull/4797)
* \[[`f3e3c70bca`](https://github.com/nodejs/node/commit/f3e3c70bca)] - **doc**: Update small error in LICENSE for npm (Kat Marchán) [#4872](https://github.com/nodejs/node/pull/4872)
* \[[`e703b180b3`](https://github.com/nodejs/node/commit/e703b180b3)] - **doc,tools,test**: lint doc-based addon tests (Rich Trott) [#5427](https://github.com/nodejs/node/pull/5427)
* \[[`0f3b8ca192`](https://github.com/nodejs/node/commit/0f3b8ca192)] - **fs**: refactor redeclared variables (Rich Trott) [#4959](https://github.com/nodejs/node/pull/4959)
* \[[`152c6b6b8d`](https://github.com/nodejs/node/commit/152c6b6b8d)] - **http**: remove reference to onParserExecute (Tom Atkinson) [#4773](https://github.com/nodejs/node/pull/4773)
* \[[`6a0571cd72`](https://github.com/nodejs/node/commit/6a0571cd72)] - **http**: do not emit `upgrade` on advertisement (Fedor Indutny) [#4337](https://github.com/nodejs/node/pull/4337)
* \[[`567ced9ef0`](https://github.com/nodejs/node/commit/567ced9ef0)] - **(SEMVER-MINOR)** **http**: handle errors on idle sockets (José F. Romaniello) [#4482](https://github.com/nodejs/node/pull/4482)
* \[[`de5177ccb8`](https://github.com/nodejs/node/commit/de5177ccb8)] - **https**: evict cached sessions on error (Fedor Indutny) [#4982](https://github.com/nodejs/node/pull/4982)
* \[[`77a6036264`](https://github.com/nodejs/node/commit/77a6036264)] - **installer**: install the tick processor (Matt Loring) [#3032](https://github.com/nodejs/node/pull/3032)
* \[[`ea16d8d7c5`](https://github.com/nodejs/node/commit/ea16d8d7c5)] - **lib**: remove string\_decoder.js var redeclarations (Rich Trott) [#4978](https://github.com/nodejs/node/pull/4978)
* \[[`1389660ab3`](https://github.com/nodejs/node/commit/1389660ab3)] - **lib**: scope loop variables (Rich Trott) [#4965](https://github.com/nodejs/node/pull/4965)
* \[[`59255d7218`](https://github.com/nodejs/node/commit/59255d7218)] - **lib**: use arrow functions instead of bind (Minwoo Jung) [#3622](https://github.com/nodejs/node/pull/3622)
* \[[`fd26960aab`](https://github.com/nodejs/node/commit/fd26960aab)] - **lib,test**: remove extra semicolons (Michaël Zasso) [#2205](https://github.com/nodejs/node/pull/2205)
* \[[`9646d26ffd`](https://github.com/nodejs/node/commit/9646d26ffd)] - **module**: refactor redeclared variable (Rich Trott) [#4962](https://github.com/nodejs/node/pull/4962)
* \[[`09311128e8`](https://github.com/nodejs/node/commit/09311128e8)] - **net**: use `_server` for internal book-keeping (Fedor Indutny) [#5262](https://github.com/nodejs/node/pull/5262)
* \[[`824c402174`](https://github.com/nodejs/node/commit/824c402174)] - **net**: refactor redeclared variables (Rich Trott) [#4963](https://github.com/nodejs/node/pull/4963)
* \[[`96f306f3cf`](https://github.com/nodejs/node/commit/96f306f3cf)] - **net**: move isLegalPort to internal/net (Evan Lucas) [#4882](https://github.com/nodejs/node/pull/4882)
* \[[`78d64889bd`](https://github.com/nodejs/node/commit/78d64889bd)] - **node**: set process.\_eventsCount to 0 on startup (Evan Lucas) [#5208](https://github.com/nodejs/node/pull/5208)
* \[[`7a2e8f4356`](https://github.com/nodejs/node/commit/7a2e8f4356)] - **process**: support symbol events (cjihrig) [#4798](https://github.com/nodejs/node/pull/4798)
* \[[`c9e2dce247`](https://github.com/nodejs/node/commit/c9e2dce247)] - **querystring**: improve parse() performance (Brian White) [#4675](https://github.com/nodejs/node/pull/4675)
* \[[`18542c41fe`](https://github.com/nodejs/node/commit/18542c41fe)] - **repl**: remove variable redeclaration (Rich Trott) [#4977](https://github.com/nodejs/node/pull/4977)
* \[[`10be8dc360`](https://github.com/nodejs/node/commit/10be8dc360)] - **src**: force line buffering for stderr (Rich Trott) [#3701](https://github.com/nodejs/node/pull/3701)
* \[[`7958664e85`](https://github.com/nodejs/node/commit/7958664e85)] - **src**: clean up usage of `__proto__` (Jackson Tian) [#5069](https://github.com/nodejs/node/pull/5069)
* \[[`4e0a0d51b3`](https://github.com/nodejs/node/commit/4e0a0d51b3)] - **src**: remove no longer relevant comments (Chris911) [#4843](https://github.com/nodejs/node/pull/4843)
* \[[`51c8bc8abc`](https://github.com/nodejs/node/commit/51c8bc8abc)] - **src**: remove \_\_builtin\_bswap16 call (Ben Noordhuis) [#4290](https://github.com/nodejs/node/pull/4290)
* \[[`5e1976e37c`](https://github.com/nodejs/node/commit/5e1976e37c)] - **src**: remove unused BITS\_PER\_LONG macro (Ben Noordhuis) [#4290](https://github.com/nodejs/node/pull/4290)
* \[[`c18ef54d88`](https://github.com/nodejs/node/commit/c18ef54d88)] - **(SEMVER-MINOR)** **src**: add BE support to StringBytes::Encode() (Bryon Leung) [#3410](https://github.com/nodejs/node/pull/3410)
* \[[`be9e7610b5`](https://github.com/nodejs/node/commit/be9e7610b5)] - **src,test,tools**: modify for more stringent linting (Rich Trott) [#5214](https://github.com/nodejs/node/pull/5214)
* \[[`538c4756a7`](https://github.com/nodejs/node/commit/538c4756a7)] - **stream**: refactor redeclared variables (Rich Trott) [#4816](https://github.com/nodejs/node/pull/4816)
* \[[`4fa22e4126`](https://github.com/nodejs/node/commit/4fa22e4126)] - **streams**: 5% throughput gain when sending small chunks (Matteo Collina) [#4354](https://github.com/nodejs/node/pull/4354)
* \[[`b6bd87495f`](https://github.com/nodejs/node/commit/b6bd87495f)] - **test**: remove flaky mark for test-debug-no-context (Rich Trott) [#5317](https://github.com/nodejs/node/pull/5317)
* \[[`7705360e35`](https://github.com/nodejs/node/commit/7705360e35)] - **test**: add test for https server close event (Braydon Fuller) [#5106](https://github.com/nodejs/node/pull/5106)
* \[[`9d6623e1d1`](https://github.com/nodejs/node/commit/9d6623e1d1)] - **test**: use String.prototype.repeat() for clarity (Rich Trott) [#5311](https://github.com/nodejs/node/pull/5311)
* \[[`18e3987e2e`](https://github.com/nodejs/node/commit/18e3987e2e)] - **test**: mitigate flaky test-debug-no-context (Rich Trott) [#5269](https://github.com/nodejs/node/pull/5269)
* \[[`058db07ce8`](https://github.com/nodejs/node/commit/058db07ce8)] - **test**: refactor test-dgram-send-callback-recursive (Santiago Gimeno) [#5079](https://github.com/nodejs/node/pull/5079)
* \[[`1647113d7a`](https://github.com/nodejs/node/commit/1647113d7a)] - **test**: refactor test-http-destroyed-socket-write2 (Santiago Gimeno) [#4970](https://github.com/nodejs/node/pull/4970)
* \[[`07dc2b50e2`](https://github.com/nodejs/node/commit/07dc2b50e2)] - **test**: shorten path for bogus socket (Rich Trott) [#4478](https://github.com/nodejs/node/pull/4478)
* \[[`47e7c8c359`](https://github.com/nodejs/node/commit/47e7c8c359)] - **test**: mark test-http-regr-gh-2928 flaky (Rich Trott) [#5280](https://github.com/nodejs/node/pull/5280)
* \[[`9dbd66f7ef`](https://github.com/nodejs/node/commit/9dbd66f7ef)] - **test**: mark test-http-agent flaky (Rich Trott) [#5209](https://github.com/nodejs/node/pull/5209)
* \[[`98049876b5`](https://github.com/nodejs/node/commit/98049876b5)] - **test**: minimal repl eval option test (Rich Trott) [#5192](https://github.com/nodejs/node/pull/5192)
* \[[`ae3185b8ac`](https://github.com/nodejs/node/commit/ae3185b8ac)] - **test**: disable fs watch tests for AIX (Michael Dawson) [#5187](https://github.com/nodejs/node/pull/5187)
* \[[`b639c3345b`](https://github.com/nodejs/node/commit/b639c3345b)] - **test**: fix child-process-fork-regr-gh-2847 again (Santiago Gimeno) [#5179](https://github.com/nodejs/node/pull/5179)
* \[[`8be3afc474`](https://github.com/nodejs/node/commit/8be3afc474)] - **test**: fix flaky test-http-regr-gh-2928 (Rich Trott) [#5154](https://github.com/nodejs/node/pull/5154)
* \[[`46dc12bdcc`](https://github.com/nodejs/node/commit/46dc12bdcc)] - **test**: enable to work pkcs12 test in FIPS mode (Shigeki Ohtsu) [#5150](https://github.com/nodejs/node/pull/5150)
* \[[`e19b8ea692`](https://github.com/nodejs/node/commit/e19b8ea692)] - **test**: remove unneeded common.indirectInstanceOf() (Rich Trott) [#5149](https://github.com/nodejs/node/pull/5149)
* \[[`6072d2e15e`](https://github.com/nodejs/node/commit/6072d2e15e)] - **test**: disable gh-5100 test when in FIPS mode (Fedor Indutny) [#5144](https://github.com/nodejs/node/pull/5144)
* \[[`a8417a2787`](https://github.com/nodejs/node/commit/a8417a2787)] - **test**: fix flaky test-dgram-pingpong (Rich Trott) [#5125](https://github.com/nodejs/node/pull/5125)
* \[[`9db67a6a44`](https://github.com/nodejs/node/commit/9db67a6a44)] - **test**: fix child-process-fork-regr-gh-2847 (Santiago Gimeno) [#5121](https://github.com/nodejs/node/pull/5121)
* \[[`69150caedc`](https://github.com/nodejs/node/commit/69150caedc)] - **test**: don't run test-tick-processor.js on Aix (Michael Dawson) [#5093](https://github.com/nodejs/node/pull/5093)
* \[[`4a492b96b1`](https://github.com/nodejs/node/commit/4a492b96b1)] - **test**: mark flaky tests on Raspberry Pi (Rich Trott) [#5082](https://github.com/nodejs/node/pull/5082)
* \[[`4301f2cdc2`](https://github.com/nodejs/node/commit/4301f2cdc2)] - **test**: fix inconsistent styling in test-url (Brian White) [#5014](https://github.com/nodejs/node/pull/5014)
* \[[`865baaed60`](https://github.com/nodejs/node/commit/865baaed60)] - **test**: fix redeclared vars in sequential tests (Rich Trott) [#4999](https://github.com/nodejs/node/pull/4999)
* \[[`663e852c1b`](https://github.com/nodejs/node/commit/663e852c1b)] - **test**: pummel test fixes (Rich Trott) [#4998](https://github.com/nodejs/node/pull/4998)
* \[[`72d38a4a38`](https://github.com/nodejs/node/commit/72d38a4a38)] - **test**: fix redeclared vars in test-vm-\* (Rich Trott) [#4997](https://github.com/nodejs/node/pull/4997)
* \[[`97ddfa2b6e`](https://github.com/nodejs/node/commit/97ddfa2b6e)] - **test**: fix redeclared vars in test-url (Rich Trott) [#4993](https://github.com/nodejs/node/pull/4993)
* \[[`43d4db4314`](https://github.com/nodejs/node/commit/43d4db4314)] - **test**: fix redeclared test-util-\* vars (Rich Trott) [#4994](https://github.com/nodejs/node/pull/4994)
* \[[`88fae38d0c`](https://github.com/nodejs/node/commit/88fae38d0c)] - **test**: fix variable redeclarations (Rich Trott) [#4992](https://github.com/nodejs/node/pull/4992)
* \[[`58595f146a`](https://github.com/nodejs/node/commit/58595f146a)] - **test**: fix redeclared test-path vars (Rich Trott) [#4991](https://github.com/nodejs/node/pull/4991)
* \[[`2b711d51fa`](https://github.com/nodejs/node/commit/2b711d51fa)] - **test**: fix var redeclarations in test-os (Rich Trott) [#4990](https://github.com/nodejs/node/pull/4990)
* \[[`bd9e2c31d6`](https://github.com/nodejs/node/commit/bd9e2c31d6)] - **test**: fix test-net-\* variable redeclarations (Rich Trott) [#4989](https://github.com/nodejs/node/pull/4989)
* \[[`d67ab81882`](https://github.com/nodejs/node/commit/d67ab81882)] - **test**: fix redeclared test-intl var (Rich Trott) [#4988](https://github.com/nodejs/node/pull/4988)
* \[[`d6dbb2fae7`](https://github.com/nodejs/node/commit/d6dbb2fae7)] - **test**: fix redeclared test-http-\* vars (Rich Trott) [#4987](https://github.com/nodejs/node/pull/4987)
* \[[`ecaa89a8cb`](https://github.com/nodejs/node/commit/ecaa89a8cb)] - **test**: fix redeclared test-event-emitter-\* vars (Rich Trott) [#4985](https://github.com/nodejs/node/pull/4985)
* \[[`299c729371`](https://github.com/nodejs/node/commit/299c729371)] - **test**: remove redeclared var in test-domain (Rich Trott) [#4984](https://github.com/nodejs/node/pull/4984)
* \[[`35a4a203bf`](https://github.com/nodejs/node/commit/35a4a203bf)] - **test**: remove var redeclarations in test-crypto-\* (Rich Trott) [#4981](https://github.com/nodejs/node/pull/4981)
* \[[`1d56b74af0`](https://github.com/nodejs/node/commit/1d56b74af0)] - **test**: remove test-cluster-\* var redeclarations (Rich Trott) [#4980](https://github.com/nodejs/node/pull/4980)
* \[[`0ce12cc1ec`](https://github.com/nodejs/node/commit/0ce12cc1ec)] - **test**: fix test-http-extra-response flakiness (Santiago Gimeno) [#4979](https://github.com/nodejs/node/pull/4979)
* \[[`c6b4bf138c`](https://github.com/nodejs/node/commit/c6b4bf138c)] - **test**: scope redeclared vars in test-child-process\* (Rich Trott) [#4944](https://github.com/nodejs/node/pull/4944)
* \[[`7654c171c7`](https://github.com/nodejs/node/commit/7654c171c7)] - **test**: refactor switch (Rich Trott) [#4870](https://github.com/nodejs/node/pull/4870)
* \[[`226dfef690`](https://github.com/nodejs/node/commit/226dfef690)] - **test**: add common.platformTimeout() to dgram test (Rich Trott) [#4938](https://github.com/nodejs/node/pull/4938)
* \[[`fb14bac662`](https://github.com/nodejs/node/commit/fb14bac662)] - **test**: fix flaky cluster test on Windows 10 (Rich Trott) [#4934](https://github.com/nodejs/node/pull/4934)
* \[[`f5d29d7ac4`](https://github.com/nodejs/node/commit/f5d29d7ac4)] - **test**: Add assertion for TLS peer certificate fingerprint (Alan Cohen) [#4923](https://github.com/nodejs/node/pull/4923)
* \[[`618427cea6`](https://github.com/nodejs/node/commit/618427cea6)] - **test**: fix test-tls-zero-clear-in flakiness (Santiago Gimeno) [#4888](https://github.com/nodejs/node/pull/4888)
* \[[`8700c39c70`](https://github.com/nodejs/node/commit/8700c39c70)] - **test**: fix irregular whitespace issue (Roman Reiss) [#4864](https://github.com/nodejs/node/pull/4864)
* \[[`2b026c9d5a`](https://github.com/nodejs/node/commit/2b026c9d5a)] - **test**: fs.link() test runs on same device (Drew Folta) [#4861](https://github.com/nodejs/node/pull/4861)
* \[[`80a637ac4d`](https://github.com/nodejs/node/commit/80a637ac4d)] - **test**: scope redeclared variable (Rich Trott) [#4854](https://github.com/nodejs/node/pull/4854)
* \[[`8c4903d4ef`](https://github.com/nodejs/node/commit/8c4903d4ef)] - **test**: update arrow function style (cjihrig) [#4813](https://github.com/nodejs/node/pull/4813)
* \[[`0a44e6a447`](https://github.com/nodejs/node/commit/0a44e6a447)] - **test**: mark test-tick-processor flaky (Rich Trott) [#4809](https://github.com/nodejs/node/pull/4809)
* \[[`363460616c`](https://github.com/nodejs/node/commit/363460616c)] - **test**: refactor test-net-settimeout (Rich Trott) [#4799](https://github.com/nodejs/node/pull/4799)
* \[[`6841d82c22`](https://github.com/nodejs/node/commit/6841d82c22)] - **test**: remove race condition in http flood test (Rich Trott) [#4793](https://github.com/nodejs/node/pull/4793)
* \[[`b5bae32847`](https://github.com/nodejs/node/commit/b5bae32847)] - **test**: remove test-http-exit-delay (Rich Trott) [#4786](https://github.com/nodejs/node/pull/4786)
* \[[`60514f9521`](https://github.com/nodejs/node/commit/60514f9521)] - **test**: refactor test-fs-watch (Rich Trott) [#4776](https://github.com/nodejs/node/pull/4776)
* \[[`2a3a431119`](https://github.com/nodejs/node/commit/2a3a431119)] - **test**: fix `net-socket-timeout-unref` flakiness (Santiago Gimeno) [#4772](https://github.com/nodejs/node/pull/4772)
* \[[`9e6f3632a1`](https://github.com/nodejs/node/commit/9e6f3632a1)] - **test**: remove Object.observe from tests (Vladimir Kurchatkin) [#4769](https://github.com/nodejs/node/pull/4769)
* \[[`f78daa67b8`](https://github.com/nodejs/node/commit/f78daa67b8)] - **test**: make npm tests work on prerelease node versions (Kat Marchán) [#4960](https://github.com/nodejs/node/pull/4960)
* \[[`1c03191b6a`](https://github.com/nodejs/node/commit/1c03191b6a)] - **test**: make npm tests work on prerelease node versions (Kat Marchán) [#4872](https://github.com/nodejs/node/pull/4872)
* \[[`d9c22cc896`](https://github.com/nodejs/node/commit/d9c22cc896)] - **test,buffer**: refactor redeclarations (Rich Trott) [#4893](https://github.com/nodejs/node/pull/4893)
* \[[`5c4960468a`](https://github.com/nodejs/node/commit/5c4960468a)] - **tls**: nullify `.ssl` on handle close (Fedor Indutny) [#5168](https://github.com/nodejs/node/pull/5168)
* \[[`c0f5f01c9c`](https://github.com/nodejs/node/commit/c0f5f01c9c)] - **tls**: scope loop vars with let (Rich Trott) [#4853](https://github.com/nodejs/node/pull/4853)
* \[[`c86627e0d1`](https://github.com/nodejs/node/commit/c86627e0d1)] - **(SEMVER-MINOR)** **tls**: add `options` argument to createSecurePair (Коренберг Марк) [#2441](https://github.com/nodejs/node/pull/2441)
* \[[`c908ff36f4`](https://github.com/nodejs/node/commit/c908ff36f4)] - **tls\_wrap**: reach error reporting for UV\_EPROTO (Fedor Indutny) [#4885](https://github.com/nodejs/node/pull/4885)
* \[[`cebe3b95e3`](https://github.com/nodejs/node/commit/cebe3b95e3)] - **tools**: run tick processor without forking (Matt Loring) [#4224](https://github.com/nodejs/node/pull/4224)
* \[[`70d8827714`](https://github.com/nodejs/node/commit/70d8827714)] - **(SEMVER-MINOR)** **tools**: add --prof-process flag to node binary (Matt Loring) [#4021](https://github.com/nodejs/node/pull/4021)
* \[[`a43b9291c7`](https://github.com/nodejs/node/commit/a43b9291c7)] - **tools**: replace obsolete ESLint rules (Rich Trott) [#5214](https://github.com/nodejs/node/pull/5214)
* \[[`a89c6f58f1`](https://github.com/nodejs/node/commit/a89c6f58f1)] - **tools**: update ESLint to version 2.1.0 (Rich Trott) [#5214](https://github.com/nodejs/node/pull/5214)
* \[[`789f62196a`](https://github.com/nodejs/node/commit/789f62196a)] - **tools**: remove obsolete lint rules (Rich Trott) [#5214](https://github.com/nodejs/node/pull/5214)
* \[[`154772cfa8`](https://github.com/nodejs/node/commit/154772cfa8)] - **tools**: parse types into links in doc html gen (Claudio Rodriguez) [#4741](https://github.com/nodejs/node/pull/4741)
* \[[`9237b6e38a`](https://github.com/nodejs/node/commit/9237b6e38a)] - **tools**: fix warning in doc parsing (Shigeki Ohtsu) [#4537](https://github.com/nodejs/node/pull/4537)
* \[[`c653cc0c03`](https://github.com/nodejs/node/commit/c653cc0c03)] - **tools**: add recommended ES6 lint rules (Rich Trott) [#5210](https://github.com/nodejs/node/pull/5210)
* \[[`993d9b7df0`](https://github.com/nodejs/node/commit/993d9b7df0)] - **tools**: add recommended linting rules (Rich Trott) [#5188](https://github.com/nodejs/node/pull/5188)
* \[[`8423125223`](https://github.com/nodejs/node/commit/8423125223)] - **tools**: remove excessive comments from .eslintrc (Rich Trott) [#5151](https://github.com/nodejs/node/pull/5151)
* \[[`4c687c98e4`](https://github.com/nodejs/node/commit/4c687c98e4)] - **tools**: enable no-proto rule for linter (Jackson Tian) [#5140](https://github.com/nodejs/node/pull/5140)
* \[[`28e4e6f312`](https://github.com/nodejs/node/commit/28e4e6f312)] - **tools**: disallow mixed spaces and tabs for indents (Rich Trott) [#5135](https://github.com/nodejs/node/pull/5135)
* \[[`50c6fe8604`](https://github.com/nodejs/node/commit/50c6fe8604)] - **tools**: alphabetize eslint stylistic issues section (Rich Trott)
* \[[`ee594f1ed7`](https://github.com/nodejs/node/commit/ee594f1ed7)] - **tools**: lint for empty character classes in regex (Rich Trott) [#5115](https://github.com/nodejs/node/pull/5115)
* \[[`bf0e239e99`](https://github.com/nodejs/node/commit/bf0e239e99)] - **tools**: lint for spacing around unary operators (Rich Trott) [#5063](https://github.com/nodejs/node/pull/5063)
* \[[`6345acb792`](https://github.com/nodejs/node/commit/6345acb792)] - **tools**: enable no-redeclare rule for linter (Rich Trott) [#5047](https://github.com/nodejs/node/pull/5047)
* \[[`1dae175b62`](https://github.com/nodejs/node/commit/1dae175b62)] - **tools**: fix redeclared vars in doc/json.js (Rich Trott) [#5047](https://github.com/nodejs/node/pull/5047)
* \[[`d1d220a1cf`](https://github.com/nodejs/node/commit/d1d220a1cf)] - **tools**: apply linting to doc tools (Rich Trott) [#4973](https://github.com/nodejs/node/pull/4973)
* \[[`eddde1f60c`](https://github.com/nodejs/node/commit/eddde1f60c)] - **tools**: fix detecting constructor for JSON doc (Timothy Gu) [#4966](https://github.com/nodejs/node/pull/4966)
* \[[`bcb327c8dd`](https://github.com/nodejs/node/commit/bcb327c8dd)] - **tools**: add property types in JSON documentation (Timothy Gu) [#4884](https://github.com/nodejs/node/pull/4884)
* \[[`9a06a4c116`](https://github.com/nodejs/node/commit/9a06a4c116)] - **tools**: enable assorted ESLint error rules (Roman Reiss) [#4864](https://github.com/nodejs/node/pull/4864)
* \[[`38474cfd49`](https://github.com/nodejs/node/commit/38474cfd49)] - **tools**: add arrow function rules to eslint (cjihrig) [#4813](https://github.com/nodejs/node/pull/4813)
* \[[`f898abaa4f`](https://github.com/nodejs/node/commit/f898abaa4f)] - **tools**: fix setting path containing an ampersand (Brian White) [#4804](https://github.com/nodejs/node/pull/4804)
* \[[`d10bee8e79`](https://github.com/nodejs/node/commit/d10bee8e79)] - **tools**: enable no-extra-semi rule in eslint (Michaël Zasso) [#2205](https://github.com/nodejs/node/pull/2205)
* \[[`01006392cf`](https://github.com/nodejs/node/commit/01006392cf)] - **tools,doc**: fix linting errors (Rich Trott) [#5161](https://github.com/nodejs/node/pull/5161)
* \[[`57a5f8731a`](https://github.com/nodejs/node/commit/57a5f8731a)] - **url**: change scoping of variables with let (Kári Tristan Helgason) [#4867](https://github.com/nodejs/node/pull/4867)

<a id="4.3.2"></a>

## 2016-03-02, Version 4.3.2 'Argon' (LTS), @thealphanerd

This is a security release with only a single commit, an update to openssl due to a recent security advisory. You can read more about the security advisory on [the Node.js website](https://nodejs.org/en/blog/vulnerability/openssl-march-2016/)

### Notable changes

* **openssl**: Upgrade from 1.0.2f to 1.0.2g (Ben Noordhuis) [#5507](https://github.com/nodejs/node/pull/5507)
  * Fix a double-free defect in parsing malformed DSA keys that may potentially be used for DoS or memory corruption attacks. It is likely to be very difficult to use this defect for a practical attack and is therefore considered low severity for Node.js users. More info is available at [CVE-2016-0705](https://www.openssl.org/news/vulnerabilities.html#2016-0705).
  * Fix a defect that can cause memory corruption in certain very rare cases relating to the internal `BN_hex2bn()` and `BN_dec2bn()` functions. It is believed that Node.js is not invoking the code paths that use these functions so practical attacks via Node.js using this defect are _unlikely_ to be possible. More info is available at [CVE-2016-0797](https://www.openssl.org/news/vulnerabilities.html#2016-0797).
  * Fix a defect that makes the _[CacheBleed Attack](https://ssrg.nicta.com.au/projects/TS/cachebleed/)_ possible. This defect enables attackers to execute side-channel attacks leading to the potential recovery of entire RSA private keys. It only affects the Intel Sandy Bridge (and possibly older) microarchitecture when using hyper-threading. Newer microarchitectures, including Haswell, are unaffected. More info is available at [CVE-2016-0702](https://www.openssl.org/news/vulnerabilities.html#2016-0702).

## Commits

* \[[`c133797d09`](https://github.com/nodejs/node/commit/c133797d09)] - **deps**: upgrade openssl to 1.0.2g (Ben Noordhuis) [#5507](https://github.com/nodejs/node/pull/5507)

<a id="4.3.1"></a>

## 2016-02-16, Version 4.3.1 'Argon' (LTS), @thealphanerd

### Notable changes

* **buffer**
  * make byteLength work with Buffer correctly (Jackson Tian)
    * [#4738](https://github.com/nodejs/node/pull/4738)
* **debugger**
  * guard against call from non-node context (Ben Noordhuis)
    * [#4328](https://github.com/nodejs/node/pull/4328)
    * fixes segfaults in debugger
  * do not incept debug context (Myles Borins)
    * [#4819](https://github.com/nodejs/node/pull/4819)
    * fixes crash in debugger when using util methods
* **deps**
  * update to http-parser 2.5.2 (James Snell)
    * [#5238](https://github.com/nodejs/node/pull/5238)

### Commits

* \[[`748d2b4de1`](https://github.com/nodejs/node/commit/748d2b4de1)] - **buffer**: make byteLength work with Buffer correctly (Jackson Tian) [#4738](https://github.com/nodejs/node/pull/4738)
* \[[`fb615bdaf4`](https://github.com/nodejs/node/commit/fb615bdaf4)] - **buffer**: remove unnecessary TODO comments (Peter Geiss) [#4719](https://github.com/nodejs/node/pull/4719)
* \[[`b8213ba7e1`](https://github.com/nodejs/node/commit/b8213ba7e1)] - **cluster**: ignore queryServer msgs on disconnection (Santiago Gimeno) [#4465](https://github.com/nodejs/node/pull/4465)
* \[[`f8a676ed59`](https://github.com/nodejs/node/commit/f8a676ed59)] - **cluster**: fix race condition setting suicide prop (Santiago Gimeno) [#4349](https://github.com/nodejs/node/pull/4349)
* \[[`9d4a226dad`](https://github.com/nodejs/node/commit/9d4a226dad)] - **crypto**: clear error stack in ECDH::Initialize (Fedor Indutny) [#4689](https://github.com/nodejs/node/pull/4689)
* \[[`583f3347d8`](https://github.com/nodejs/node/commit/583f3347d8)] - **debugger**: remove variable redeclarations (Rich Trott) [#4633](https://github.com/nodejs/node/pull/4633)
* \[[`667f7a7ab3`](https://github.com/nodejs/node/commit/667f7a7ab3)] - **debugger**: guard against call from non-node context (Ben Noordhuis) [#4328](https://github.com/nodejs/node/pull/4328)
* \[[`188cff3c31`](https://github.com/nodejs/node/commit/188cff3c31)] - **deps**: update to http-parser 2.5.2 (James Snell) [#5238](https://github.com/nodejs/node/pull/5238)
* \[[`6e829b44e3`](https://github.com/nodejs/node/commit/6e829b44e3)] - **dgram**: prevent disabled optimization of bind() (Brian White) [#4613](https://github.com/nodejs/node/pull/4613)
* \[[`c3956d05b1`](https://github.com/nodejs/node/commit/c3956d05b1)] - **doc**: update list of personal traits in CoC (Kat Marchán) [#4801](https://github.com/nodejs/node/pull/4801)
* \[[`39cb69ca21`](https://github.com/nodejs/node/commit/39cb69ca21)] - **doc**: style fixes for the TOC (Roman Reiss) [#4748](https://github.com/nodejs/node/pull/4748)
* \[[`cb5986da81`](https://github.com/nodejs/node/commit/cb5986da81)] - **doc**: add `servername` parameter docs (Alexander Makarenko) [#4729](https://github.com/nodejs/node/pull/4729)
* \[[`91066b5f34`](https://github.com/nodejs/node/commit/91066b5f34)] - **doc**: update branch-diff arguments in release doc (Rod Vagg) [#4691](https://github.com/nodejs/node/pull/4691)
* \[[`9ca24de41d`](https://github.com/nodejs/node/commit/9ca24de41d)] - **doc**: add docs for more stream options (zoubin) [#4639](https://github.com/nodejs/node/pull/4639)
* \[[`437d0e336d`](https://github.com/nodejs/node/commit/437d0e336d)] - **doc**: mention that http.Server inherits from net.Server (Ryan Sobol) [#4455](https://github.com/nodejs/node/pull/4455)
* \[[`393e569160`](https://github.com/nodejs/node/commit/393e569160)] - **doc**: copyedit setTimeout() documentation (Rich Trott) [#4434](https://github.com/nodejs/node/pull/4434)
* \[[`e2a682ecc3`](https://github.com/nodejs/node/commit/e2a682ecc3)] - **doc**: fix formatting in process.markdown (Rich Trott) [#4433](https://github.com/nodejs/node/pull/4433)
* \[[`75b0ea85bd`](https://github.com/nodejs/node/commit/75b0ea85bd)] - **doc**: add path property to Write/ReadStream in fs.markdown (Claudio Rodriguez) [#4368](https://github.com/nodejs/node/pull/4368)
* \[[`48c2783421`](https://github.com/nodejs/node/commit/48c2783421)] - **doc**: add docs working group (Bryan English) [#4244](https://github.com/nodejs/node/pull/4244)
* \[[`c0432e9f56`](https://github.com/nodejs/node/commit/c0432e9f56)] - **doc**: restore ICU third-party software licenses (Richard Lau) [#4762](https://github.com/nodejs/node/pull/4762)
* \[[`36a4159dab`](https://github.com/nodejs/node/commit/36a4159dab)] - **doc**: rebuild LICENSE using tools/license-builder.sh (Rod Vagg) [#4194](https://github.com/nodejs/node/pull/4194)
* \[[`a2998a1bce`](https://github.com/nodejs/node/commit/a2998a1bce)] - **gitignore**: never ignore debug module (Michaël Zasso) [#2286](https://github.com/nodejs/node/pull/2286)
* \[[`661b2557d9`](https://github.com/nodejs/node/commit/661b2557d9)] - **http**: remove variable redeclaration (Rich Trott) [#4612](https://github.com/nodejs/node/pull/4612)
* \[[`1bb2967d48`](https://github.com/nodejs/node/commit/1bb2967d48)] - **http**: fix non-string header value concatenation (Brian White) [#4460](https://github.com/nodejs/node/pull/4460)
* \[[`15ed64e34c`](https://github.com/nodejs/node/commit/15ed64e34c)] - **lib**: fix style issues after eslint update (Michaël Zasso) [nodejs/io.js#2286](https://github.com/nodejs/io.js/pull/2286)
* \[[`2e92a1a6b4`](https://github.com/nodejs/node/commit/2e92a1a6b4)] - **module**: move unnecessary work for early return (Andres Suarez) [#3579](https://github.com/nodejs/node/pull/3579)
* \[[`40c8e6d75d`](https://github.com/nodejs/node/commit/40c8e6d75d)] - **net**: remove hot path comment from connect (Evan Lucas) [#4648](https://github.com/nodejs/node/pull/4648)
* \[[`8ed0c1c22c`](https://github.com/nodejs/node/commit/8ed0c1c22c)] - **net**: fix dns lookup for android (Josh Dague) [#4580](https://github.com/nodejs/node/pull/4580)
* \[[`15fa555204`](https://github.com/nodejs/node/commit/15fa555204)] - **net, doc**: fix line wrapping lint in net.js (James M Snell) [#4588](https://github.com/nodejs/node/pull/4588)
* \[[`1b070e48e0`](https://github.com/nodejs/node/commit/1b070e48e0)] - **node\_contextify**: do not incept debug context (Myles Borins) [#4815](https://github.com/nodejs/node/issues/4815)
* \[[`4fbcb47fe9`](https://github.com/nodejs/node/commit/4fbcb47fe9)] - **readline**: Remove XXX and output debuglog (Kohei TAKATA) [#4690](https://github.com/nodejs/node/pull/4690)
* \[[`26f02405d0`](https://github.com/nodejs/node/commit/26f02405d0)] - **repl**: make sure historyPath is trimmed (Evan Lucas) [#4539](https://github.com/nodejs/node/pull/4539)
* \[[`5990ba2a0a`](https://github.com/nodejs/node/commit/5990ba2a0a)] - **src**: remove redeclarations of variables (Rich Trott) [#4605](https://github.com/nodejs/node/pull/4605)
* \[[`c41ed59dbc`](https://github.com/nodejs/node/commit/c41ed59dbc)] - **src**: don't check failure with ERR\_peek\_error() (Ben Noordhuis) [#4731](https://github.com/nodejs/node/pull/4731)
* \[[`d71f9992f9`](https://github.com/nodejs/node/commit/d71f9992f9)] - **stream**: remove useless if test in transform (zoubin) [#4617](https://github.com/nodejs/node/pull/4617)
* \[[`f205e9920e`](https://github.com/nodejs/node/commit/f205e9920e)] - **test**: fix tls-no-rsa-key flakiness (Santiago Gimeno) [#4043](https://github.com/nodejs/node/pull/4043)
* \[[`447347cd62`](https://github.com/nodejs/node/commit/447347cd62)] - **test**: fix issues for space-in-parens ESLint rule (Roman Reiss) [#4753](https://github.com/nodejs/node/pull/4753)
* \[[`be8274508c`](https://github.com/nodejs/node/commit/be8274508c)] - **test**: improve test-cluster-disconnect-suicide-race (Rich Trott) [#4739](https://github.com/nodejs/node/pull/4739)
* \[[`0178001163`](https://github.com/nodejs/node/commit/0178001163)] - **test**: make test-cluster-disconnect-leak reliable (Rich Trott) [#4736](https://github.com/nodejs/node/pull/4736)
* \[[`d615757da2`](https://github.com/nodejs/node/commit/d615757da2)] - **test**: fix flaky test-net-socket-local-address (cjihrig) [#4650](https://github.com/nodejs/node/pull/4650)
* \[[`baa0a3dff5`](https://github.com/nodejs/node/commit/baa0a3dff5)] - **test**: fix race in test-net-server-pause-on-connect (Rich Trott) [#4637](https://github.com/nodejs/node/pull/4637)
* \[[`909b5167cb`](https://github.com/nodejs/node/commit/909b5167cb)] - **test**: remove 1 second delay from test (Rich Trott) [#4616](https://github.com/nodejs/node/pull/4616)
* \[[`8ea76608ed`](https://github.com/nodejs/node/commit/8ea76608ed)] - **test**: move resource intensive tests to sequential (Rich Trott) [#4615](https://github.com/nodejs/node/pull/4615)
* \[[`7afcdd358e`](https://github.com/nodejs/node/commit/7afcdd358e)] - **test**: require common module only once (Rich Trott) [#4611](https://github.com/nodejs/node/pull/4611)
* \[[`0e02eb0bbe`](https://github.com/nodejs/node/commit/0e02eb0bbe)] - **test**: only include http module once (Rich Trott) [#4606](https://github.com/nodejs/node/pull/4606)
* \[[`34d9e48bb6`](https://github.com/nodejs/node/commit/34d9e48bb6)] - **test**: fix `http-upgrade-client` flakiness (Santiago Gimeno) [#4602](https://github.com/nodejs/node/pull/4602)
* \[[`556703d531`](https://github.com/nodejs/node/commit/556703d531)] - **test**: fix flaky unrefed timers test (Rich Trott) [#4599](https://github.com/nodejs/node/pull/4599)
* \[[`3d5bc69796`](https://github.com/nodejs/node/commit/3d5bc69796)] - **test**: fix `http-upgrade-agent` flakiness (Santiago Gimeno) [#4520](https://github.com/nodejs/node/pull/4520)
* \[[`ec24d3767b`](https://github.com/nodejs/node/commit/ec24d3767b)] - **test**: fix flaky test-cluster-shared-leak (Rich Trott) [#4510](https://github.com/nodejs/node/pull/4510)
* \[[`a256790327`](https://github.com/nodejs/node/commit/a256790327)] - **test**: fix flaky cluster-net-send (Brian White) [#4444](https://github.com/nodejs/node/pull/4444)
* \[[`6809c2be1a`](https://github.com/nodejs/node/commit/6809c2be1a)] - **test**: fix flaky child-process-fork-regr-gh-2847 (Brian White) [#4442](https://github.com/nodejs/node/pull/4442)
* \[[`e6448aa36b`](https://github.com/nodejs/node/commit/e6448aa36b)] - **test**: use addon.md block headings as test dir names (Rod Vagg) [#4412](https://github.com/nodejs/node/pull/4412)
* \[[`305d340fca`](https://github.com/nodejs/node/commit/305d340fca)] - **test**: test each block in addon.md contains js & cc (Rod Vagg) [#4411](https://github.com/nodejs/node/pull/4411)
* \[[`f213406575`](https://github.com/nodejs/node/commit/f213406575)] - **test**: fix tls-multi-key race condition (Santiago Gimeno) [#3966](https://github.com/nodejs/node/pull/3966)
* \[[`607f545568`](https://github.com/nodejs/node/commit/607f545568)] - **test**: fix style issues after eslint update (Michaël Zasso) [nodejs/io.js#2286](https://github.com/nodejs/io.js/pull/2286)
* \[[`aefb20a94f`](https://github.com/nodejs/node/commit/aefb20a94f)] - **tls**: copy client CAs and cert store on CertCb (Fedor Indutny) [#3537](https://github.com/nodejs/node/pull/3537)
* \[[`7821b3e305`](https://github.com/nodejs/node/commit/7821b3e305)] - **tls\_legacy**: do not read on OpenSSL's stack (Fedor Indutny) [#4624](https://github.com/nodejs/node/pull/4624)
* \[[`b66db49f94`](https://github.com/nodejs/node/commit/b66db49f94)] - **tools**: add support for subkeys in release tools (Myles Borins) [#4807](https://github.com/nodejs/node/pull/4807)
* \[[`837ebd1985`](https://github.com/nodejs/node/commit/837ebd1985)] - **tools**: enable space-in-parens ESLint rule (Roman Reiss) [#4753](https://github.com/nodejs/node/pull/4753)
* \[[`066d5e7da2`](https://github.com/nodejs/node/commit/066d5e7da2)] - **tools**: fix style issue after eslint update (Michaël Zasso) [nodejs/io.js#2286](https://github.com/nodejs/io.js/pull/2286)
* \[[`b20ea69f46`](https://github.com/nodejs/node/commit/b20ea69f46)] - **tools**: update eslint config (Michaël Zasso) [nodejs/io.js#2286](https://github.com/nodejs/io.js/pull/2286)
* \[[`2e0352d50c`](https://github.com/nodejs/node/commit/2e0352d50c)] - **tools**: update eslint to v1.10.3 (Michaël Zasso) [nodejs/io.js#2286](https://github.com/nodejs/io.js/pull/2286)
* \[[`c96800a432`](https://github.com/nodejs/node/commit/c96800a432)] - **tools**: fix license-builder.sh for ICU (Richard Lau) [#4762](https://github.com/nodejs/node/pull/4762)
* \[[`720b03dca7`](https://github.com/nodejs/node/commit/720b03dca7)] - **tools**: add license-builder.sh to construct LICENSE (Rod Vagg) [#4194](https://github.com/nodejs/node/pull/4194)

<a id="4.3.0"></a>

## 2016-02-09, Version 4.3.0 'Argon' (LTS), @jasnell

This is an important security release. All Node.js users should consult the security release summary at nodejs.org for details on patched vulnerabilities.

Note that this release includes a non-backward compatible change to address a security issue. This change increases the version of the LTS v4.x line to v4.3.0. There will be _no further updates_ to v4.2.x.

### Notable changes

* **http**: fix defects in HTTP header parsing for requests and responses that can allow request smuggling (CVE-2016-2086) or response splitting (CVE-2016-2216). HTTP header parsing now aligns more closely with the HTTP spec including restricting the acceptable characters.
* **http-parser**: upgrade from 2.5.0 to 2.5.1
* **openssl**: upgrade from 1.0.2e to 1.0.2f. To mitigate against the Logjam attack, TLS clients now reject Diffie-Hellman handshakes with parameters shorter than 1024-bits, up from the previous limit of 768-bits.
* **src**:
  * introduce new `--security-revert={cvenum}` command line flag for selective reversion of specific CVE fixes
  * allow the fix for CVE-2016-2216 to be selectively reverted using `--security-revert=CVE-2016-2216`

### Commits

* \[[`d94f864abd`](https://github.com/nodejs/node/commit/d94f864abd)] - **deps**: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) [#1836](https://github.com/nodejs/node/pull/1836)
* \[[`136295e202`](https://github.com/nodejs/node/commit/136295e202)] - **deps**: upgrade openssl sources to 1.0.2f (Myles Borins) [#4961](https://github.com/nodejs/node/pull/4961)
* \[[`0eae95eae3`](https://github.com/nodejs/node/commit/0eae95eae3)] - **(SEMVER-MINOR)** **deps**: update http-parser to version 2.5.1 (James M Snell)
* \[[`cf2b714b02`](https://github.com/nodejs/node/commit/cf2b714b02)] - **(SEMVER-MINOR)** **http**: strictly forbid invalid characters from headers (James M Snell)
* \[[`49ae2e0334`](https://github.com/nodejs/node/commit/49ae2e0334)] - **src**: avoid compiler warning in node\_revert.cc (James M Snell)
* \[[`da3750f981`](https://github.com/nodejs/node/commit/da3750f981)] - **(SEMVER-MAJOR)** **src**: add --security-revert command line flag (James M Snell)

<a id="4.2.6"></a>

## 2016-01-21, Version 4.2.6 'Argon' (LTS), @TheAlphaNerd

### Notable changes

* Fix regression in debugger and profiler functionality

### Known issues

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`1408f7abb1`](https://github.com/nodejs/node/commit/1408f7abb1)] - **module,src**: do not wrap modules with -1 lineOffset (cjihrig) [#4298](https://github.com/nodejs/node/pull/4298)
* \[[`1f8e1472cc`](https://github.com/nodejs/node/commit/1f8e1472cc)] - **test**: add test for debugging one line files (cjihrig) [#4298](https://github.com/nodejs/node/pull/4298)

<a id="4.2.5"></a>

## 2016-01-20, Version 4.2.5 'Argon' (LTS), @TheAlphaNerd

Maintenance update.

### Notable changes

* **assert**
  * accommodate ES6 classes that extend Error (Rich Trott) [#4166](https://github.com/nodejs/node/pull/4166)
* **build**
  * add "--partly-static" build options (Super Zheng) [#4152](https://github.com/nodejs/node/pull/4152)
* **deps**
  * backport 066747e from upstream V8 (Ali Ijaz Sheikh) [#4655](https://github.com/nodejs/node/pull/4655)
  * backport 200315c from V8 upstream (Vladimir Kurchatkin) [#4128](https://github.com/nodejs/node/pull/4128)
  * upgrade libuv to 1.8.0 (Saúl Ibarra Corretgé)
* **docs**
  * various updates landed in 70 different commits!
* **repl**
  * attach location info to syntax errors (cjihrig) [#4013](https://github.com/nodejs/node/pull/4013)
  * display error message when loading directory (Prince J Wesley) [#4170](https://github.com/nodejs/node/pull/4170)
* **tests**
  * various updates landed in over 50 commits
* **tools**
  * add tap output to cpplint (Johan Bergström) [#3448](https://github.com/nodejs/node/pull/3448)
* **util**
  * allow lookup of hidden values (cjihrig) [#3988](https://github.com/nodejs/node/pull/3988)

### Known issues

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`87181cd74c`](https://github.com/nodejs/node/commit/87181cd74c)] - **assert**: accommodate ES6 classes that extend Error (Rich Trott) [#4166](https://github.com/nodejs/node/pull/4166)
* \[[`901172a783`](https://github.com/nodejs/node/commit/901172a783)] - **assert**: typed array deepequal performance fix (Claudio Rodriguez) [#4330](https://github.com/nodejs/node/pull/4330)
* \[[`55336810ee`](https://github.com/nodejs/node/commit/55336810ee)] - **async\_wrap**: call callback in destructor (Trevor Norris) [#3461](https://github.com/nodejs/node/pull/3461)
* \[[`a8b45e9e96`](https://github.com/nodejs/node/commit/a8b45e9e96)] - **async\_wrap**: new instances get uid (Trevor Norris) [#3461](https://github.com/nodejs/node/pull/3461)
* \[[`49f16d77c4`](https://github.com/nodejs/node/commit/49f16d77c4)] - **async\_wrap**: allow some hooks to be optional (Trevor Norris) [#3461](https://github.com/nodejs/node/pull/3461)
* \[[`44ee33f945`](https://github.com/nodejs/node/commit/44ee33f945)] - **buffer**: refactor create buffer (Jackson Tian) [#4340](https://github.com/nodejs/node/pull/4340)
* \[[`138d004ac0`](https://github.com/nodejs/node/commit/138d004ac0)] - **buffer**: faster case for create Buffer from new Buffer(0) (Jackson Tian) [#4326](https://github.com/nodejs/node/pull/4326)
* \[[`c6dc2a1609`](https://github.com/nodejs/node/commit/c6dc2a1609)] - **buffer**: Prevent Buffer constructor deopt (Bryce Baril) [#4158](https://github.com/nodejs/node/pull/4158)
* \[[`a320045e68`](https://github.com/nodejs/node/commit/a320045e68)] - **buffer**: default to UTF8 in byteLength() (Tom Gallacher) [#4010](https://github.com/nodejs/node/pull/4010)
* \[[`c5f71ac771`](https://github.com/nodejs/node/commit/c5f71ac771)] - **build**: add "--partly-static" build options (Super Zheng) [#4152](https://github.com/nodejs/node/pull/4152)
* \[[`e6c25335ea`](https://github.com/nodejs/node/commit/e6c25335ea)] - **build**: omit -gline-tables-only for --enable-asan (Ben Noordhuis) [#3680](https://github.com/nodejs/node/pull/3680)
* \[[`80b4ba286c`](https://github.com/nodejs/node/commit/80b4ba286c)] - **build**: Updates for AIX npm support - part 1 (Michael Dawson) [#3114](https://github.com/nodejs/node/pull/3114)
* \[[`35e32985ca`](https://github.com/nodejs/node/commit/35e32985ca)] - **child\_process**: guard against race condition (Rich Trott) [#4418](https://github.com/nodejs/node/pull/4418)
* \[[`48564204f0`](https://github.com/nodejs/node/commit/48564204f0)] - **child\_process**: flush consuming streams (Dave) [#4071](https://github.com/nodejs/node/pull/4071)
* \[[`481d59a74c`](https://github.com/nodejs/node/commit/481d59a74c)] - **configure**: fix arm vfpv2 (Jörg Krause) [#4203](https://github.com/nodejs/node/pull/4203)
* \[[`d19da6638d`](https://github.com/nodejs/node/commit/d19da6638d)] - **crypto**: load PFX chain the same way as regular one (Fedor Indutny) [#4165](https://github.com/nodejs/node/pull/4165)
* \[[`b8e75de1f3`](https://github.com/nodejs/node/commit/b8e75de1f3)] - **crypto**: fix native module compilation with FIPS (Stefan Budeanu) [#4023](https://github.com/nodejs/node/pull/4023)
* \[[`b7c3fb7f75`](https://github.com/nodejs/node/commit/b7c3fb7f75)] - **crypto**: disable crypto.createCipher in FIPS mode (Stefan Budeanu) [#3754](https://github.com/nodejs/node/pull/3754)
* \[[`31b4091a1e`](https://github.com/nodejs/node/commit/31b4091a1e)] - **debugger**: also exit when the repl emits 'exit' (Felix Böhm) [#2369](https://github.com/nodejs/node/pull/2369)
* \[[`9baa5618f5`](https://github.com/nodejs/node/commit/9baa5618f5)] - **deps**: backport 066747e from upstream V8 (Ali Ijaz Sheikh) [#4655](https://github.com/nodejs/node/pull/4655)
* \[[`c3a9d8a62e`](https://github.com/nodejs/node/commit/c3a9d8a62e)] - **deps**: backport 200315c from V8 upstream (Vladimir Kurchatkin) [#4128](https://github.com/nodejs/node/pull/4128)
* \[[`1ebb0c0fdf`](https://github.com/nodejs/node/commit/1ebb0c0fdf)] - **deps**: upgrade libuv to 1.8.0 (Saúl Ibarra Corretgé) [#4276](https://github.com/nodejs/node/pull/4276)
* \[[`253fe3e7c8`](https://github.com/nodejs/node/commit/253fe3e7c8)] - **dns**: remove nonexistant exports.ADNAME (Roman Reiss) [#3051](https://github.com/nodejs/node/pull/3051)
* \[[`8c2b65ad82`](https://github.com/nodejs/node/commit/8c2b65ad82)] - **doc**: clarify protocol default in http.request() (cjihrig) [#4714](https://github.com/nodejs/node/pull/4714)
* \[[`33e72e135f`](https://github.com/nodejs/node/commit/33e72e135f)] - **doc**: update links to use https where possible (jpersson) [#4054](https://github.com/nodejs/node/pull/4054)
* \[[`5f4aa79410`](https://github.com/nodejs/node/commit/5f4aa79410)] - **doc**: clarify explanation of first stream section (Vitor Cortez) [#4234](https://github.com/nodejs/node/pull/4234)
* \[[`295ca5bfb2`](https://github.com/nodejs/node/commit/295ca5bfb2)] - **doc**: add branch-diff example to releases.md (Myles Borins) [#4636](https://github.com/nodejs/node/pull/4636)
* \[[`18f5cd8710`](https://github.com/nodejs/node/commit/18f5cd8710)] - **doc**: update stylesheet to match frontpage (Roman Reiss) [#4621](https://github.com/nodejs/node/pull/4621)
* \[[`2f40715f08`](https://github.com/nodejs/node/commit/2f40715f08)] - **doc**: adds usage of readline line-by-line parsing (Robert Jefe Lindstaedt) [#4609](https://github.com/nodejs/node/pull/4609)
* \[[`5b45a464ee`](https://github.com/nodejs/node/commit/5b45a464ee)] - **doc**: document http's server.listen return value (Sequoia McDowell) [#4590](https://github.com/nodejs/node/pull/4590)
* \[[`bd31740339`](https://github.com/nodejs/node/commit/bd31740339)] - **doc**: label http.IncomingMessage as a Class (Sequoia McDowell) [#4589](https://github.com/nodejs/node/pull/4589)
* \[[`bcd2cbbb93`](https://github.com/nodejs/node/commit/bcd2cbbb93)] - **doc**: fix description about the latest-codename (Minwoo Jung) [#4583](https://github.com/nodejs/node/pull/4583)
* \[[`0b12bcb35d`](https://github.com/nodejs/node/commit/0b12bcb35d)] - **doc**: add Evan Lucas to Release Team (Evan Lucas) [#4579](https://github.com/nodejs/node/pull/4579)
* \[[`e20b1f6f10`](https://github.com/nodejs/node/commit/e20b1f6f10)] - **doc**: add Myles Borins to Release Team (Myles Borins) [#4578](https://github.com/nodejs/node/pull/4578)
* \[[`54977e63eb`](https://github.com/nodejs/node/commit/54977e63eb)] - **doc**: add missing backtick for readline (Brian White) [#4549](https://github.com/nodejs/node/pull/4549)
* \[[`5d6bed895c`](https://github.com/nodejs/node/commit/5d6bed895c)] - **doc**: bring releases.md up to date (cjihrig) [#4540](https://github.com/nodejs/node/pull/4540)
* \[[`0cd2252e85`](https://github.com/nodejs/node/commit/0cd2252e85)] - **doc**: fix numbering in stream.markdown (Richard Sun) [#4538](https://github.com/nodejs/node/pull/4538)
* \[[`8574d91f27`](https://github.com/nodejs/node/commit/8574d91f27)] - **doc**: stronger suggestion for userland assert (Wyatt Preul) [#4535](https://github.com/nodejs/node/pull/4535)
* \[[`a7bcf8b84d`](https://github.com/nodejs/node/commit/a7bcf8b84d)] - **doc**: close backtick in process.title description (Dave) [#4534](https://github.com/nodejs/node/pull/4534)
* \[[`0ceb3148b0`](https://github.com/nodejs/node/commit/0ceb3148b0)] - **doc**: improvements to events.markdown copy (James M Snell) [#4468](https://github.com/nodejs/node/pull/4468)
* \[[`bf56d509b9`](https://github.com/nodejs/node/commit/bf56d509b9)] - **doc**: explain ClientRequest#setTimeout time unit (Ben Ripkens) [#4458](https://github.com/nodejs/node/pull/4458)
* \[[`d927c51be3`](https://github.com/nodejs/node/commit/d927c51be3)] - **doc**: improvements to errors.markdown copy (James M Snell) [#4454](https://github.com/nodejs/node/pull/4454)
* \[[`ceea6df581`](https://github.com/nodejs/node/commit/ceea6df581)] - **doc**: improvements to dns.markdown copy (James M Snell) [#4449](https://github.com/nodejs/node/pull/4449)
* \[[`506f2f8ed1`](https://github.com/nodejs/node/commit/506f2f8ed1)] - **doc**: add anchors for \_transform \_flush \_writev in stream.markdown (iamchenxin) [#4448](https://github.com/nodejs/node/pull/4448)
* \[[`74bcad0b78`](https://github.com/nodejs/node/commit/74bcad0b78)] - **doc**: improvements to dgram.markdown copy (James M Snell) [#4437](https://github.com/nodejs/node/pull/4437)
* \[[`e244d560c9`](https://github.com/nodejs/node/commit/e244d560c9)] - **doc**: improvements to debugger.markdown copy (James M Snell) [#4436](https://github.com/nodejs/node/pull/4436)
* \[[`df7e1281a5`](https://github.com/nodejs/node/commit/df7e1281a5)] - **doc**: improvements to console.markdown copy (James M Snell) [#4428](https://github.com/nodejs/node/pull/4428)
* \[[`abb17cc6c1`](https://github.com/nodejs/node/commit/abb17cc6c1)] - **doc**: fix spelling error in lib/url.js comment (Nik Nyby) [#4390](https://github.com/nodejs/node/pull/4390)
* \[[`823269db2d`](https://github.com/nodejs/node/commit/823269db2d)] - **doc**: improve assert.markdown copy (James M Snell) [#4360](https://github.com/nodejs/node/pull/4360)
* \[[`2b1804f6cb`](https://github.com/nodejs/node/commit/2b1804f6cb)] - **doc**: copyedit releases.md (Rich Trott) [#4384](https://github.com/nodejs/node/pull/4384)
* \[[`2b142fd876`](https://github.com/nodejs/node/commit/2b142fd876)] - **doc**: catch the WORKING\_GROUPS.md bootstrap docs up to date (James M Snell) [#4367](https://github.com/nodejs/node/pull/4367)
* \[[`ed87873de3`](https://github.com/nodejs/node/commit/ed87873de3)] - **doc**: fix link in addons.markdown (Nicholas Young) [#4331](https://github.com/nodejs/node/pull/4331)
* \[[`fe693b7a4f`](https://github.com/nodejs/node/commit/fe693b7a4f)] - **doc**: Typo in buffer.markdown referencing buf.write() (chrisjohn404) [#4324](https://github.com/nodejs/node/pull/4324)
* \[[`764df2166e`](https://github.com/nodejs/node/commit/764df2166e)] - **doc**: document the cache parameter for fs.realpathSync (Jackson Tian) [#4285](https://github.com/nodejs/node/pull/4285)
* \[[`61f91b2f29`](https://github.com/nodejs/node/commit/61f91b2f29)] - **doc**: fix, modernize examples in docs (James M Snell) [#4282](https://github.com/nodejs/node/pull/4282)
* \[[`d87ad302ce`](https://github.com/nodejs/node/commit/d87ad302ce)] - **doc**: clarify error events in HTTP module documentation (Lenny Markus) [#4275](https://github.com/nodejs/node/pull/4275)
* \[[`7983577e41`](https://github.com/nodejs/node/commit/7983577e41)] - **doc**: fix improper http.get sample code (Hideki Yamamura) [#4263](https://github.com/nodejs/node/pull/4263)
* \[[`6c30d087e5`](https://github.com/nodejs/node/commit/6c30d087e5)] - **doc**: Fixing broken links to the v8 wiki (Tom Gallacher) [#4241](https://github.com/nodejs/node/pull/4241)
* \[[`cf214e56e4`](https://github.com/nodejs/node/commit/cf214e56e4)] - **doc**: move description of 'equals' method to right place (janriemer) [#4227](https://github.com/nodejs/node/pull/4227)
* \[[`fb8e8dbb92`](https://github.com/nodejs/node/commit/fb8e8dbb92)] - **doc**: copyedit console doc (Rich Trott) [#4225](https://github.com/nodejs/node/pull/4225)
* \[[`4ccf04c229`](https://github.com/nodejs/node/commit/4ccf04c229)] - **doc**: add mcollina to collaborators (Matteo Collina) [#4220](https://github.com/nodejs/node/pull/4220)
* \[[`59654c21d4`](https://github.com/nodejs/node/commit/59654c21d4)] - **doc**: add rmg to collaborators (Ryan Graham) [#4219](https://github.com/nodejs/node/pull/4219)
* \[[`bfe1a6bd2b`](https://github.com/nodejs/node/commit/bfe1a6bd2b)] - **doc**: add calvinmetcalf to collaborators (Calvin Metcalf) [#4218](https://github.com/nodejs/node/pull/4218)
* \[[`5140c404ae`](https://github.com/nodejs/node/commit/5140c404ae)] - **doc**: harmonize description of `ca` argument (Ben Noordhuis) [#4213](https://github.com/nodejs/node/pull/4213)
* \[[`2e642051cf`](https://github.com/nodejs/node/commit/2e642051cf)] - **doc**: copyedit child\_process doc (Rich Trott) [#4188](https://github.com/nodejs/node/pull/4188)
* \[[`7920f8dbde`](https://github.com/nodejs/node/commit/7920f8dbde)] - **doc**: copyedit buffer doc (Rich Trott) [#4187](https://github.com/nodejs/node/pull/4187)
* \[[`c35a409cbe`](https://github.com/nodejs/node/commit/c35a409cbe)] - **doc**: clarify assert.fail doc (Rich Trott) [#4186](https://github.com/nodejs/node/pull/4186)
* \[[`6235fdf72e`](https://github.com/nodejs/node/commit/6235fdf72e)] - **doc**: copyedit addons doc (Rich Trott) [#4185](https://github.com/nodejs/node/pull/4185)
* \[[`990e7ff93e`](https://github.com/nodejs/node/commit/990e7ff93e)] - **doc**: update AUTHORS list (Rod Vagg) [#4183](https://github.com/nodejs/node/pull/4183)
* \[[`8d676ef55e`](https://github.com/nodejs/node/commit/8d676ef55e)] - **doc**: change references from node to Node.js (Roman Klauke) [#4177](https://github.com/nodejs/node/pull/4177)
* \[[`1c34b139a2`](https://github.com/nodejs/node/commit/1c34b139a2)] - **doc**: add brief Node.js overview to README (wurde) [#4174](https://github.com/nodejs/node/pull/4174)
* \[[`27b9b72ab0`](https://github.com/nodejs/node/commit/27b9b72ab0)] - **doc**: add iarna to collaborators (Rebecca Turner) [#4144](https://github.com/nodejs/node/pull/4144)
* \[[`683d8dd564`](https://github.com/nodejs/node/commit/683d8dd564)] - **doc**: add JungMinu to collaborators (Minwoo Jung) [#4143](https://github.com/nodejs/node/pull/4143)
* \[[`17b06dfa94`](https://github.com/nodejs/node/commit/17b06dfa94)] - **doc**: add zkat to collaborators (Kat Marchán) [#4142](https://github.com/nodejs/node/pull/4142)
* \[[`39364c4c72`](https://github.com/nodejs/node/commit/39364c4c72)] - **doc**: improve child\_process.markdown wording (yorkie) [#4138](https://github.com/nodejs/node/pull/4138)
* \[[`abe452835f`](https://github.com/nodejs/node/commit/abe452835f)] - **doc**: url.format - true slash postfix behaviour (fansworld-claudio) [#4119](https://github.com/nodejs/node/pull/4119)
* \[[`6dd375cfe2`](https://github.com/nodejs/node/commit/6dd375cfe2)] - **doc**: document backlog for server.listen() variants (Jan Schär) [#4025](https://github.com/nodejs/node/pull/4025)
* \[[`b71a3b363a`](https://github.com/nodejs/node/commit/b71a3b363a)] - **doc**: fixup socket.remoteAddress (Arthur Gautier) [#4198](https://github.com/nodejs/node/pull/4198)
* \[[`e2fe214857`](https://github.com/nodejs/node/commit/e2fe214857)] - **doc**: add links and backticks around names (jpersson) [#4054](https://github.com/nodejs/node/pull/4054)
* \[[`bb158f8aed`](https://github.com/nodejs/node/commit/bb158f8aed)] - **doc**: s/node.js/Node.js in readme (Rod Vagg) [#3998](https://github.com/nodejs/node/pull/3998)
* \[[`f55491ad47`](https://github.com/nodejs/node/commit/f55491ad47)] - **doc**: move fs.existsSync() deprecation message (Martin Forsberg) [#3942](https://github.com/nodejs/node/pull/3942)
* \[[`8c5b847f5b`](https://github.com/nodejs/node/commit/8c5b847f5b)] - **doc**: Describe FIPSDIR environment variable (Stefan Budeanu) [#3752](https://github.com/nodejs/node/pull/3752)
* \[[`70c95ea0e5`](https://github.com/nodejs/node/commit/70c95ea0e5)] - **doc**: add warning about Windows process groups (Roman Klauke) [#3681](https://github.com/nodejs/node/pull/3681)
* \[[`46c59b7256`](https://github.com/nodejs/node/commit/46c59b7256)] - **doc**: add CTC meeting minutes 2015-10-28 (Rod Vagg) [#3661](https://github.com/nodejs/node/pull/3661)
* \[[`7ffd299a1d`](https://github.com/nodejs/node/commit/7ffd299a1d)] - **doc**: add final full stop in CONTRIBUTING.md (Emily Aviva Kapor-Mater) [#3576](https://github.com/nodejs/node/pull/3576)
* \[[`1f78bff7ce`](https://github.com/nodejs/node/commit/1f78bff7ce)] - **doc**: add TSC meeting minutes 2015-10-21 (Rod Vagg) [#3480](https://github.com/nodejs/node/pull/3480)
* \[[`2e623ff024`](https://github.com/nodejs/node/commit/2e623ff024)] - **doc**: add TSC meeting minutes 2015-10-14 (Rod Vagg) [#3463](https://github.com/nodejs/node/pull/3463)
* \[[`b9c69964bb`](https://github.com/nodejs/node/commit/b9c69964bb)] - **doc**: add TSC meeting minutes 2015-10-07 (Rod Vagg) [#3364](https://github.com/nodejs/node/pull/3364)
* \[[`f31d23c724`](https://github.com/nodejs/node/commit/f31d23c724)] - **doc**: add TSC meeting minutes 2015-09-30 (Rod Vagg) [#3235](https://github.com/nodejs/node/pull/3235)
* \[[`ae8e3af178`](https://github.com/nodejs/node/commit/ae8e3af178)] - **doc**: update irc channels: #node.js and #node-dev (Nelson Pecora) [#2743](https://github.com/nodejs/node/pull/2743)
* \[[`830caeb1bd`](https://github.com/nodejs/node/commit/830caeb1bd)] - **doc, test**: symbols as event names (Bryan English) [#4151](https://github.com/nodejs/node/pull/4151)
* \[[`82cbfcdcbe`](https://github.com/nodejs/node/commit/82cbfcdcbe)] - **docs**: update gpg key for Myles Borins (Myles Borins) [#4657](https://github.com/nodejs/node/pull/4657)
* \[[`50b72aa5a3`](https://github.com/nodejs/node/commit/50b72aa5a3)] - **docs**: fix npm command in releases.md (Myles Borins) [#4656](https://github.com/nodejs/node/pull/4656)
* \[[`5bf56882e1`](https://github.com/nodejs/node/commit/5bf56882e1)] - **fs,doc**: use `target` instead of `destination` (yorkie) [#3912](https://github.com/nodejs/node/pull/3912)
* \[[`41fcda840c`](https://github.com/nodejs/node/commit/41fcda840c)] - **http**: use `self.keepAlive` instead of `self.options.keepAlive` (Damian Schenkelman) [#4407](https://github.com/nodejs/node/pull/4407)
* \[[`3ff237333d`](https://github.com/nodejs/node/commit/3ff237333d)] - **http**: Remove an unnecessary assignment (Bo Borgerson) [#4323](https://github.com/nodejs/node/pull/4323)
* \[[`39dc054572`](https://github.com/nodejs/node/commit/39dc054572)] - **http**: remove excess calls to removeSocket (Dave) [#4172](https://github.com/nodejs/node/pull/4172)
* \[[`751fbd84dd`](https://github.com/nodejs/node/commit/751fbd84dd)] - **https**: use `servername` in agent key (Fedor Indutny) [#4389](https://github.com/nodejs/node/pull/4389)
* \[[`7a1a0a0055`](https://github.com/nodejs/node/commit/7a1a0a0055)] - **lib**: remove unused modules (Rich Trott) [#4683](https://github.com/nodejs/node/pull/4683)
* \[[`3d81ea99bb`](https://github.com/nodejs/node/commit/3d81ea99bb)] - **lib,test**: update let to const where applicable (Sakthipriyan Vairamani) [#3152](https://github.com/nodejs/node/pull/3152)
* \[[`8a9869eeab`](https://github.com/nodejs/node/commit/8a9869eeab)] - **module**: fix column offsets in errors (Tristian Flanagan) [#2867](https://github.com/nodejs/node/pull/2867)
* \[[`0ae90ecd3d`](https://github.com/nodejs/node/commit/0ae90ecd3d)] - **module,repl**: remove repl require() hack (Ben Noordhuis) [#4026](https://github.com/nodejs/node/pull/4026)
* \[[`a7367fdc1e`](https://github.com/nodejs/node/commit/a7367fdc1e)] - **net**: small code cleanup (Jan Schär) [#3943](https://github.com/nodejs/node/pull/3943)
* \[[`03e9495cc2`](https://github.com/nodejs/node/commit/03e9495cc2)] - **node**: remove unused variables in AppendExceptionLine (Yazhong Liu) [#4264](https://github.com/nodejs/node/pull/4264)
* \[[`06113b8711`](https://github.com/nodejs/node/commit/06113b8711)] - **node**: s/doNTCallbackX/nextTickCallbackWithXArgs/ (Rod Vagg) [#4167](https://github.com/nodejs/node/pull/4167)
* \[[`8ce6843fe4`](https://github.com/nodejs/node/commit/8ce6843fe4)] - **os**: fix crash in GetInterfaceAddresses (Martin Bark) [#4272](https://github.com/nodejs/node/pull/4272)
* \[[`53dcbb6aa4`](https://github.com/nodejs/node/commit/53dcbb6aa4)] - **repl**: remove unused function (Rich Trott)
* \[[`db0e906fc1`](https://github.com/nodejs/node/commit/db0e906fc1)] - **repl**: Fixed node repl history edge case. (Mudit Ameta) [#4108](https://github.com/nodejs/node/pull/4108)
* \[[`9855fab05f`](https://github.com/nodejs/node/commit/9855fab05f)] - **repl**: use String#repeat instead of Array#join (Evan Lucas) [#3900](https://github.com/nodejs/node/pull/3900)
* \[[`41882e4077`](https://github.com/nodejs/node/commit/41882e4077)] - **repl**: fix require('3rdparty') regression (Ben Noordhuis) [#4215](https://github.com/nodejs/node/pull/4215)
* \[[`93afc39d4a`](https://github.com/nodejs/node/commit/93afc39d4a)] - **repl**: attach location info to syntax errors (cjihrig) [#4013](https://github.com/nodejs/node/pull/4013)
* \[[`d4806675a6`](https://github.com/nodejs/node/commit/d4806675a6)] - **repl**: display error message when loading directory (Prince J Wesley) [#4170](https://github.com/nodejs/node/pull/4170)
* \[[`3080bdc7d7`](https://github.com/nodejs/node/commit/3080bdc7d7)] - **src**: define Is\* util functions with macros (cjihrig) [#4118](https://github.com/nodejs/node/pull/4118)
* \[[`2b8a32a13b`](https://github.com/nodejs/node/commit/2b8a32a13b)] - **src**: refactor vcbuild configure args creation (Rod Vagg) [#3399](https://github.com/nodejs/node/pull/3399)
* \[[`d47f6ba768`](https://github.com/nodejs/node/commit/d47f6ba768)] - **src**: fix deprecation message for ErrnoException (Martin von Gagern) [#4269](https://github.com/nodejs/node/pull/4269)
* \[[`5ba08fbf76`](https://github.com/nodejs/node/commit/5ba08fbf76)] - **src**: fix line numbers on core errors (cjihrig) [#4254](https://github.com/nodejs/node/pull/4254)
* \[[`70974e9362`](https://github.com/nodejs/node/commit/70974e9362)] - **src**: use GetCurrentProcessId() for process.pid (Ben Noordhuis) [#4163](https://github.com/nodejs/node/pull/4163)
* \[[`c96eca164f`](https://github.com/nodejs/node/commit/c96eca164f)] - **src**: don't print garbage errors (cjihrig) [#4112](https://github.com/nodejs/node/pull/4112)
* \[[`f61412c753`](https://github.com/nodejs/node/commit/f61412c753)] - **test**: mark test-debug-no-context is flaky (Rich Trott) [#4421](https://github.com/nodejs/node/pull/4421)
* \[[`46d8c93ed2`](https://github.com/nodejs/node/commit/46d8c93ed2)] - **test**: don't use cwd for relative path (Johan Bergström) [#4477](https://github.com/nodejs/node/pull/4477)
* \[[`b6124ea39c`](https://github.com/nodejs/node/commit/b6124ea39c)] - **test**: write to tmp dir rather than fixture dir (Rich Trott) [#4489](https://github.com/nodejs/node/pull/4489)
* \[[`350fa664bb`](https://github.com/nodejs/node/commit/350fa664bb)] - **test**: don't assume a certain folder structure (Johan Bergström) [#3325](https://github.com/nodejs/node/pull/3325)
* \[[`6b2ef0efac`](https://github.com/nodejs/node/commit/6b2ef0efac)] - **test**: make temp path customizable (Johan Bergström) [#3325](https://github.com/nodejs/node/pull/3325)
* \[[`f1837703a9`](https://github.com/nodejs/node/commit/f1837703a9)] - **test**: remove unused vars from parallel tests (Rich Trott) [#4511](https://github.com/nodejs/node/pull/4511)
* \[[`b4964b099a`](https://github.com/nodejs/node/commit/b4964b099a)] - **test**: remove unused variables form http tests (Rich Trott) [#4422](https://github.com/nodejs/node/pull/4422)
* \[[`0d5a508dfb`](https://github.com/nodejs/node/commit/0d5a508dfb)] - **test**: extend timeout in Debug mode (Rich Trott) [#4431](https://github.com/nodejs/node/pull/4431)
* \[[`6e4598d5da`](https://github.com/nodejs/node/commit/6e4598d5da)] - **test**: remove unused variables from TLS tests (Rich Trott) [#4424](https://github.com/nodejs/node/pull/4424)
* \[[`7b1aa045a0`](https://github.com/nodejs/node/commit/7b1aa045a0)] - **test**: remove unused variables from HTTPS tests (Rich Trott) [#4426](https://github.com/nodejs/node/pull/4426)
* \[[`da9e5c1b01`](https://github.com/nodejs/node/commit/da9e5c1b01)] - **test**: remove unused variables from net tests (Rich Trott) [#4430](https://github.com/nodejs/node/pull/4430)
* \[[`13241bd24b`](https://github.com/nodejs/node/commit/13241bd24b)] - **test**: remove unused vars in ChildProcess tests (Rich Trott) [#4425](https://github.com/nodejs/node/pull/4425)
* \[[`2f4538ddda`](https://github.com/nodejs/node/commit/2f4538ddda)] - **test**: remove unused vars (Rich Trott) [#4536](https://github.com/nodejs/node/pull/4536)
* \[[`dffe83ccd6`](https://github.com/nodejs/node/commit/dffe83ccd6)] - **test**: remove unused modules (Rich Trott) [#4684](https://github.com/nodejs/node/pull/4684)
* \[[`c4eeb88ba1`](https://github.com/nodejs/node/commit/c4eeb88ba1)] - **test**: fix flaky cluster-disconnect-race (Brian White) [#4457](https://github.com/nodejs/node/pull/4457)
* \[[`7caf87bf6c`](https://github.com/nodejs/node/commit/7caf87bf6c)] - **test**: fix flaky test-http-agent-keepalive (Rich Trott) [#4524](https://github.com/nodejs/node/pull/4524)
* \[[`25c41d084d`](https://github.com/nodejs/node/commit/25c41d084d)] - **test**: remove flaky designations for tests (Rich Trott) [#4519](https://github.com/nodejs/node/pull/4519)
* \[[`b8f097ece2`](https://github.com/nodejs/node/commit/b8f097ece2)] - **test**: fix flaky streams test (Rich Trott) [#4516](https://github.com/nodejs/node/pull/4516)
* \[[`c24fa1437c`](https://github.com/nodejs/node/commit/c24fa1437c)] - **test**: inherit JOBS from environment (Johan Bergström) [#4495](https://github.com/nodejs/node/pull/4495)
* \[[`7dc90e9e7f`](https://github.com/nodejs/node/commit/7dc90e9e7f)] - **test**: remove time check (Rich Trott) [#4494](https://github.com/nodejs/node/pull/4494)
* \[[`7ca3c6c388`](https://github.com/nodejs/node/commit/7ca3c6c388)] - **test**: refactor test-fs-empty-readStream (Rich Trott) [#4490](https://github.com/nodejs/node/pull/4490)
* \[[`610727dea7`](https://github.com/nodejs/node/commit/610727dea7)] - **test**: clarify role of domains in test (Rich Trott) [#4474](https://github.com/nodejs/node/pull/4474)
* \[[`1ae0e355b9`](https://github.com/nodejs/node/commit/1ae0e355b9)] - **test**: improve assert message (Rich Trott) [#4461](https://github.com/nodejs/node/pull/4461)
* \[[`e70c88df56`](https://github.com/nodejs/node/commit/e70c88df56)] - **test**: remove unused assert module imports (Rich Trott) [#4438](https://github.com/nodejs/node/pull/4438)
* \[[`c77fc71f9b`](https://github.com/nodejs/node/commit/c77fc71f9b)] - **test**: remove unused var from test-assert.js (Rich Trott) [#4405](https://github.com/nodejs/node/pull/4405)
* \[[`f613b3033f`](https://github.com/nodejs/node/commit/f613b3033f)] - **test**: add test-domain-exit-dispose-again back (Julien Gilli) [#4256](https://github.com/nodejs/node/pull/4256)
* \[[`f5bfacd858`](https://github.com/nodejs/node/commit/f5bfacd858)] - **test**: remove unused `util` imports (Rich Trott) [#4562](https://github.com/nodejs/node/pull/4562)
* \[[`d795301025`](https://github.com/nodejs/node/commit/d795301025)] - **test**: remove unnecessary assignments (Rich Trott) [#4563](https://github.com/nodejs/node/pull/4563)
* \[[`acc3d66934`](https://github.com/nodejs/node/commit/acc3d66934)] - **test**: move ArrayStream to common (cjihrig) [#4027](https://github.com/nodejs/node/pull/4027)
* \[[`6c0021361c`](https://github.com/nodejs/node/commit/6c0021361c)] - **test**: refactor test-net-connect-options-ipv6 (Rich Trott) [#4395](https://github.com/nodejs/node/pull/4395)
* \[[`29804e00ad`](https://github.com/nodejs/node/commit/29804e00ad)] - **test**: use platformTimeout() in more places (Brian White) [#4387](https://github.com/nodejs/node/pull/4387)
* \[[`761af37d0e`](https://github.com/nodejs/node/commit/761af37d0e)] - **test**: fix race condition in test-http-client-onerror (Devin Nakamura) [#4346](https://github.com/nodejs/node/pull/4346)
* \[[`980852165f`](https://github.com/nodejs/node/commit/980852165f)] - **test**: fix flaky test-net-error-twice (Brian White) [#4342](https://github.com/nodejs/node/pull/4342)
* \[[`1bc44e79d3`](https://github.com/nodejs/node/commit/1bc44e79d3)] - **test**: try other ipv6 localhost alternatives (Brian White) [#4325](https://github.com/nodejs/node/pull/4325)
* \[[`44dbe15640`](https://github.com/nodejs/node/commit/44dbe15640)] - **test**: fix debug-port-cluster flakiness (Ben Noordhuis) [#4310](https://github.com/nodejs/node/pull/4310)
* \[[`73e781172b`](https://github.com/nodejs/node/commit/73e781172b)] - **test**: add test for tls.parseCertString (Evan Lucas) [#4283](https://github.com/nodejs/node/pull/4283)
* \[[`15c295a21b`](https://github.com/nodejs/node/commit/15c295a21b)] - **test**: use regular timeout times for ARMv8 (Jeremiah Senkpiel) [#4248](https://github.com/nodejs/node/pull/4248)
* \[[`fd250b8fab`](https://github.com/nodejs/node/commit/fd250b8fab)] - **test**: parallelize test-repl-persistent-history (Jeremiah Senkpiel) [#4247](https://github.com/nodejs/node/pull/4247)
* \[[`9a0f156e5a`](https://github.com/nodejs/node/commit/9a0f156e5a)] - **test**: fix domain-top-level-error-handler-throw (Santiago Gimeno) [#4364](https://github.com/nodejs/node/pull/4364)
* \[[`6bc1b1c259`](https://github.com/nodejs/node/commit/6bc1b1c259)] - **test**: don't assume openssl s\_client supports -ssl3 (Ben Noordhuis) [#4204](https://github.com/nodejs/node/pull/4204)
* \[[`d00b9fc66f`](https://github.com/nodejs/node/commit/d00b9fc66f)] - **test**: fix tls-inception flakiness (Santiago Gimeno) [#4195](https://github.com/nodejs/node/pull/4195)
* \[[`c41b280a2b`](https://github.com/nodejs/node/commit/c41b280a2b)] - **test**: fix tls-inception (Santiago Gimeno) [#4195](https://github.com/nodejs/node/pull/4195)
* \[[`6f4ab1d1ab`](https://github.com/nodejs/node/commit/6f4ab1d1ab)] - **test**: mark test-cluster-shared-leak flaky (Rich Trott) [#4162](https://github.com/nodejs/node/pull/4162)
* \[[`90498e2a68`](https://github.com/nodejs/node/commit/90498e2a68)] - **test**: skip long path tests on non-Windows (Rafał Pocztarski) [#4116](https://github.com/nodejs/node/pull/4116)
* \[[`c9100d78f3`](https://github.com/nodejs/node/commit/c9100d78f3)] - **test**: fix flaky test-net-socket-local-address (Rich Trott) [#4109](https://github.com/nodejs/node/pull/4109)
* \[[`ac939d51d9`](https://github.com/nodejs/node/commit/ac939d51d9)] - **test**: improve cluster-disconnect-handles test (Brian White) [#4084](https://github.com/nodejs/node/pull/4084)
* \[[`22ba1b4115`](https://github.com/nodejs/node/commit/22ba1b4115)] - **test**: eliminate multicast test FreeBSD flakiness (Rich Trott) [#4042](https://github.com/nodejs/node/pull/4042)
* \[[`2ee7853bb7`](https://github.com/nodejs/node/commit/2ee7853bb7)] - **test**: fix http-many-ended-pipelines flakiness (Santiago Gimeno) [#4041](https://github.com/nodejs/node/pull/4041)
* \[[`a77dcfec06`](https://github.com/nodejs/node/commit/a77dcfec06)] - **test**: use platform-based timeout for reliability (Rich Trott) [#4015](https://github.com/nodejs/node/pull/4015)
* \[[`3f0ff879cf`](https://github.com/nodejs/node/commit/3f0ff879cf)] - **test**: fix time resolution constraint (Gireesh Punathil) [#3981](https://github.com/nodejs/node/pull/3981)
* \[[`22b88e1c48`](https://github.com/nodejs/node/commit/22b88e1c48)] - **test**: add TAP diagnostic message for retried tests (Rich Trott) [#3960](https://github.com/nodejs/node/pull/3960)
* \[[`22d2887b1c`](https://github.com/nodejs/node/commit/22d2887b1c)] - **test**: add OS X to module loading error test (Evan Lucas) [#3901](https://github.com/nodejs/node/pull/3901)
* \[[`e2141cb75e`](https://github.com/nodejs/node/commit/e2141cb75e)] - **test**: skip instead of fail when mem constrained (Michael Cornacchia) [#3697](https://github.com/nodejs/node/pull/3697)
* \[[`166523d0ed`](https://github.com/nodejs/node/commit/166523d0ed)] - **test**: fix race condition in unrefd interval test (Michael Cornacchia) [#3550](https://github.com/nodejs/node/pull/3550)
* \[[`86b47e8dc0`](https://github.com/nodejs/node/commit/86b47e8dc0)] - **timers**: optimize callback call: bind -> arrow (Andrei Sedoi) [#4038](https://github.com/nodejs/node/pull/4038)
* \[[`4d37472ea7`](https://github.com/nodejs/node/commit/4d37472ea7)] - **tls\_wrap**: clear errors on return (Fedor Indutny) [#4709](https://github.com/nodejs/node/pull/4709)
* \[[`5b695d0343`](https://github.com/nodejs/node/commit/5b695d0343)] - **tls\_wrap**: inherit from the `AsyncWrap` first (Fedor Indutny) [#4268](https://github.com/nodejs/node/pull/4268)
* \[[`0efc35e6d8`](https://github.com/nodejs/node/commit/0efc35e6d8)] - **tls\_wrap**: slice buffer properly in `ClearOut` (Fedor Indutny) [#4184](https://github.com/nodejs/node/pull/4184)
* \[[`628cb8657c`](https://github.com/nodejs/node/commit/628cb8657c)] - **tools**: add .editorconfig (ronkorving) [#2993](https://github.com/nodejs/node/pull/2993)
* \[[`69fef19624`](https://github.com/nodejs/node/commit/69fef19624)] - **tools**: implement no-unused-vars for eslint (Rich Trott) [#4536](https://github.com/nodejs/node/pull/4536)
* \[[`3ee16706f2`](https://github.com/nodejs/node/commit/3ee16706f2)] - **tools**: enforce `throw new Error()` with lint rule (Rich Trott) [#3714](https://github.com/nodejs/node/pull/3714)
* \[[`32801de4ef`](https://github.com/nodejs/node/commit/32801de4ef)] - **tools**: Use `throw new Error()` consistently (Rich Trott) [#3714](https://github.com/nodejs/node/pull/3714)
* \[[`f413fae0cd`](https://github.com/nodejs/node/commit/f413fae0cd)] - **tools**: add tap output to cpplint (Johan Bergström) [#3448](https://github.com/nodejs/node/pull/3448)
* \[[`efa30dd2f0`](https://github.com/nodejs/node/commit/efa30dd2f0)] - **tools**: enable prefer-const eslint rule (Sakthipriyan Vairamani) [#3152](https://github.com/nodejs/node/pull/3152)
* \[[`dd0c925896`](https://github.com/nodejs/node/commit/dd0c925896)] - **udp**: remove a needless instanceof Buffer check (ronkorving) [#4301](https://github.com/nodejs/node/pull/4301)
* \[[`f4414102ed`](https://github.com/nodejs/node/commit/f4414102ed)] - **util**: faster arrayToHash (Jackson Tian)
* \[[`b421119984`](https://github.com/nodejs/node/commit/b421119984)] - **util**: determine object types in C++ (cjihrig) [#4100](https://github.com/nodejs/node/pull/4100)
* \[[`6a7c9d9293`](https://github.com/nodejs/node/commit/6a7c9d9293)] - **util**: move .decorateErrorStack to internal/util (Ben Noordhuis) [#4026](https://github.com/nodejs/node/pull/4026)
* \[[`422a865d46`](https://github.com/nodejs/node/commit/422a865d46)] - **util**: add decorateErrorStack() (cjihrig) [#4013](https://github.com/nodejs/node/pull/4013)
* \[[`2d5380ea25`](https://github.com/nodejs/node/commit/2d5380ea25)] - **util**: fix constructor/instanceof checks (Brian White) [#3385](https://github.com/nodejs/node/pull/3385)
* \[[`1bf84b9d41`](https://github.com/nodejs/node/commit/1bf84b9d41)] - **util,src**: allow lookup of hidden values (cjihrig) [#3988](https://github.com/nodejs/node/pull/3988)

<a id="4.2.4"></a>

## 2015-12-23, Version 4.2.4 'Argon' (LTS), @jasnell

Maintenance update.

### Notable changes

* Roughly 78% of the commits are documentation and test improvements
* **domains**:
  * Fix handling of uncaught exceptions (Julien Gilli) [#3884](https://github.com/nodejs/node/pull/3884)
* **deps**:
  * Upgrade to npm 2.14.12 (Kat Marchán) [#4110](https://github.com/nodejs/node/pull/4110)
  * Backport 819b40a from V8 upstream (Michaël Zasso) [#3938](https://github.com/nodejs/node/pull/3938)
  * Updated node LICENSE file with new npm license (Kat Marchán) [#4110](https://github.com/nodejs/node/pull/4110)

### Known issues

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`907a13a07f`](https://github.com/nodejs/node/commit/907a13a07f)] - Add missing va\_end before return (Ömer Fadıl Usta) [#3565](https://github.com/nodejs/node/pull/3565)
* \[[`7ffc01756f`](https://github.com/nodejs/node/commit/7ffc01756f)] - **buffer**: fix writeInt{B,L}E for some neg values (Peter A. Bigot) [#3994](https://github.com/nodejs/node/pull/3994)
* \[[`db0186e435`](https://github.com/nodejs/node/commit/db0186e435)] - **buffer**: let WriteFloatGeneric silently drop values (Minqi Pan)
* \[[`5c6740865a`](https://github.com/nodejs/node/commit/5c6740865a)] - **build**: update signtool description, add url (Rod Vagg) [#4011](https://github.com/nodejs/node/pull/4011)
* \[[`60dda70f89`](https://github.com/nodejs/node/commit/60dda70f89)] - **build**: fix --with-intl=system-icu for x-compile (Steven R. Loomis) [#3808](https://github.com/nodejs/node/pull/3808)
* \[[`22208b067c`](https://github.com/nodejs/node/commit/22208b067c)] - **build**: fix configuring with prebuilt libraries (Markus Tzoe) [#3135](https://github.com/nodejs/node/pull/3135)
* \[[`914caf9c69`](https://github.com/nodejs/node/commit/914caf9c69)] - **child\_process**: add safety checks on stdio access (cjihrig) [#3799](https://github.com/nodejs/node/pull/3799)
* \[[`236ad90a84`](https://github.com/nodejs/node/commit/236ad90a84)] - **child\_process**: don't fork bomb ourselves from -e (Ben Noordhuis) [#3575](https://github.com/nodejs/node/pull/3575)
* \[[`f28f69dac4`](https://github.com/nodejs/node/commit/f28f69dac4)] - **cluster**: remove handles when disconnecting worker (Ben Noordhuis) [#3677](https://github.com/nodejs/node/pull/3677)
* \[[`f5c5e8bf91`](https://github.com/nodejs/node/commit/f5c5e8bf91)] - **cluster**: send suicide message on disconnect (cjihrig) [#3720](https://github.com/nodejs/node/pull/3720)
* \[[`629d5d18d7`](https://github.com/nodejs/node/commit/629d5d18d7)] - **configure**: `v8_use_snapshot` should be `true` (Fedor Indutny) [#3962](https://github.com/nodejs/node/pull/3962)
* \[[`3094464871`](https://github.com/nodejs/node/commit/3094464871)] - **configure**: use \_\_ARM\_ARCH to determine arm version (João Reis) [#4123](https://github.com/nodejs/node/pull/4123)
* \[[`1e1173fc5c`](https://github.com/nodejs/node/commit/1e1173fc5c)] - **configure**: respect CC\_host in host arch detection (João Reis) [#4117](https://github.com/nodejs/node/pull/4117)
* \[[`2e9b886fbf`](https://github.com/nodejs/node/commit/2e9b886fbf)] - **crypto**: DSA parameter validation in FIPS mode (Stefan Budeanu) [#3756](https://github.com/nodejs/node/pull/3756)
* \[[`00b77d9e84`](https://github.com/nodejs/node/commit/00b77d9e84)] - **crypto**: Improve error checking and reporting (Stefan Budeanu) [#3753](https://github.com/nodejs/node/pull/3753)
* \[[`810f76e440`](https://github.com/nodejs/node/commit/810f76e440)] - **deps**: upgrade to npm 2.14.12 (Kat Marchán) [#4110](https://github.com/nodejs/node/pull/4110)
* \[[`51ae8d10b3`](https://github.com/nodejs/node/commit/51ae8d10b3)] - **deps**: Updated node LICENSE file with new npm license (Kat Marchán) [#4110](https://github.com/nodejs/node/pull/4110)
* \[[`9e1edead22`](https://github.com/nodejs/node/commit/9e1edead22)] - **deps**: backport 819b40a from V8 upstream (Michaël Zasso) [#3938](https://github.com/nodejs/node/pull/3938)
* \[[`a2ce3843cc`](https://github.com/nodejs/node/commit/a2ce3843cc)] - **deps**: upgrade npm to 2.14.9 (Forrest L Norvell) [#3686](https://github.com/nodejs/node/pull/3686)
* \[[`b140cb29f4`](https://github.com/nodejs/node/commit/b140cb29f4)] - **dns**: prevent undefined values in results (Junliang Yan) [#3696](https://github.com/nodejs/node/pull/3696)
* \[[`8aafa2ecc0`](https://github.com/nodejs/node/commit/8aafa2ecc0)] - **doc**: standardize references to node.js in docs (Scott Buchanan) [#4136](https://github.com/nodejs/node/pull/4136)
* \[[`72f43a263a`](https://github.com/nodejs/node/commit/72f43a263a)] - **doc**: fix internal link to child.send() (Luigi Pinca) [#4089](https://github.com/nodejs/node/pull/4089)
* \[[`dcfdbac457`](https://github.com/nodejs/node/commit/dcfdbac457)] - **doc**: reword https.Agent example text (Jan Krems) [#4075](https://github.com/nodejs/node/pull/4075)
* \[[`f93d268dec`](https://github.com/nodejs/node/commit/f93d268dec)] - **doc**: add HTTP working group (James M Snell) [#3919](https://github.com/nodejs/node/pull/3919)
* \[[`beee0553ca`](https://github.com/nodejs/node/commit/beee0553ca)] - **doc**: update WORKING\_GROUPS.md - add missing groups (Michael Dawson) [#3450](https://github.com/nodejs/node/pull/3450)
* \[[`3327415fc4`](https://github.com/nodejs/node/commit/3327415fc4)] - **doc**: fix the exception description (yorkie) [#3658](https://github.com/nodejs/node/pull/3658)
* \[[`da8d012c88`](https://github.com/nodejs/node/commit/da8d012c88)] - **doc**: clarify v4.2.3 notable items (Rod Vagg) [#4155](https://github.com/nodejs/node/pull/4155)
* \[[`44a2d8ca24`](https://github.com/nodejs/node/commit/44a2d8ca24)] - **doc**: fix color of linked code blocks (jpersson) [#4068](https://github.com/nodejs/node/pull/4068)
* \[[`bebde48ebc`](https://github.com/nodejs/node/commit/bebde48ebc)] - **doc**: fix typo in README (Rich Trott) [#4000](https://github.com/nodejs/node/pull/4000)
* \[[`b48d5ec301`](https://github.com/nodejs/node/commit/b48d5ec301)] - **doc**: message.header duplication correction (Bryan English) [#3997](https://github.com/nodejs/node/pull/3997)
* \[[`6ef3625456`](https://github.com/nodejs/node/commit/6ef3625456)] - **doc**: replace sane with reasonable (Lewis Cowper) [#3980](https://github.com/nodejs/node/pull/3980)
* \[[`c5be3c63f0`](https://github.com/nodejs/node/commit/c5be3c63f0)] - **doc**: fix rare case of misaligned columns (Roman Reiss) [#3948](https://github.com/nodejs/node/pull/3948)
* \[[`bd82fb06ff`](https://github.com/nodejs/node/commit/bd82fb06ff)] - **doc**: fix broken references (Alexander Gromnitsky) [#3944](https://github.com/nodejs/node/pull/3944)
* \[[`8eb28c3d50`](https://github.com/nodejs/node/commit/8eb28c3d50)] - **doc**: add reference for buffer.inspect() (cjihrig) [#3921](https://github.com/nodejs/node/pull/3921)
* \[[`4bc71e0078`](https://github.com/nodejs/node/commit/4bc71e0078)] - **doc**: clarify module loading behavior (cjihrig) [#3920](https://github.com/nodejs/node/pull/3920)
* \[[`4c382e7aaa`](https://github.com/nodejs/node/commit/4c382e7aaa)] - **doc**: numeric flags to fs.open (Carl Lei) [#3641](https://github.com/nodejs/node/pull/3641)
* \[[`5207099dc9`](https://github.com/nodejs/node/commit/5207099dc9)] - **doc**: clarify that fs streams expect blocking fd (Carl Lei) [#3641](https://github.com/nodejs/node/pull/3641)
* \[[`753c5071ea`](https://github.com/nodejs/node/commit/753c5071ea)] - **doc**: Adding best practises for crypto.pbkdf2 (Tom Gallacher) [#3290](https://github.com/nodejs/node/pull/3290)
* \[[`8f0291beba`](https://github.com/nodejs/node/commit/8f0291beba)] - **doc**: update WORKING\_GROUPS.md to include Intl (Steven R. Loomis) [#3251](https://github.com/nodejs/node/pull/3251)
* \[[`c31d472487`](https://github.com/nodejs/node/commit/c31d472487)] - **doc**: sort repl alphabetically (Tristian Flanagan) [#3859](https://github.com/nodejs/node/pull/3859)
* \[[`6b172d9fe8`](https://github.com/nodejs/node/commit/6b172d9fe8)] - **doc**: consistent reference-style links (Bryan English) [#3845](https://github.com/nodejs/node/pull/3845)
* \[[`ffd3335e29`](https://github.com/nodejs/node/commit/ffd3335e29)] - **doc**: address use of profanity in code of conduct (James M Snell) [#3827](https://github.com/nodejs/node/pull/3827)
* \[[`a36a5b63cf`](https://github.com/nodejs/node/commit/a36a5b63cf)] - **doc**: reword message.headers to indicate they are not read-only (Tristian Flanagan) [#3814](https://github.com/nodejs/node/pull/3814)
* \[[`6de77cd320`](https://github.com/nodejs/node/commit/6de77cd320)] - **doc**: clarify duplicate header handling (Bryan English) [#3810](https://github.com/nodejs/node/pull/3810)
* \[[`b22973af81`](https://github.com/nodejs/node/commit/b22973af81)] - **doc**: replace head of readme with updated text (Rod Vagg) [#3482](https://github.com/nodejs/node/pull/3482)
* \[[`eab0d56ea9`](https://github.com/nodejs/node/commit/eab0d56ea9)] - **doc**: repl: add defineComand and displayPrompt (Bryan English) [#3765](https://github.com/nodejs/node/pull/3765)
* \[[`15fb02985f`](https://github.com/nodejs/node/commit/15fb02985f)] - **doc**: document release types in readme (Rod Vagg) [#3482](https://github.com/nodejs/node/pull/3482)
* \[[`29f26b882f`](https://github.com/nodejs/node/commit/29f26b882f)] - **doc**: add link to \[customizing util.inspect colors]. (Jesse McCarthy) [#3749](https://github.com/nodejs/node/pull/3749)
* \[[`90fdb4f7b3`](https://github.com/nodejs/node/commit/90fdb4f7b3)] - **doc**: sort tls alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`39fa9fa85c`](https://github.com/nodejs/node/commit/39fa9fa85c)] - **doc**: sort stream alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`e98e8afb2b`](https://github.com/nodejs/node/commit/e98e8afb2b)] - **doc**: sort net alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`6de887483d`](https://github.com/nodejs/node/commit/6de887483d)] - **doc**: sort process alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`37033dcb71`](https://github.com/nodejs/node/commit/37033dcb71)] - **doc**: sort zlib alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`9878034567`](https://github.com/nodejs/node/commit/9878034567)] - **doc**: sort util alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`48fc765eb6`](https://github.com/nodejs/node/commit/48fc765eb6)] - **doc**: sort https alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`3546eb4f40`](https://github.com/nodejs/node/commit/3546eb4f40)] - **doc**: sort http alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`dedfb1156a`](https://github.com/nodejs/node/commit/dedfb1156a)] - **doc**: sort modules alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`71722fe1a1`](https://github.com/nodejs/node/commit/71722fe1a1)] - **doc**: sort readline alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`660062bf9e`](https://github.com/nodejs/node/commit/660062bf9e)] - **doc**: sort repl alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`34b8d28725`](https://github.com/nodejs/node/commit/34b8d28725)] - **doc**: sort string\_decoder alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`3f3b9ed7d7`](https://github.com/nodejs/node/commit/3f3b9ed7d7)] - **doc**: sort timers alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`af876ddc64`](https://github.com/nodejs/node/commit/af876ddc64)] - **doc**: sort tty alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`3c2068704a`](https://github.com/nodejs/node/commit/3c2068704a)] - **doc**: sort url alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`363692fd0c`](https://github.com/nodejs/node/commit/363692fd0c)] - **doc**: sort vm alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`ca41b55166`](https://github.com/nodejs/node/commit/ca41b55166)] - **doc**: sort querystring alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`f37ff22b9f`](https://github.com/nodejs/node/commit/f37ff22b9f)] - **doc**: sort punycode alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`4d569607af`](https://github.com/nodejs/node/commit/4d569607af)] - **doc**: sort path alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`daa62447d1`](https://github.com/nodejs/node/commit/daa62447d1)] - **doc**: sort os alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`0906f9a8bb`](https://github.com/nodejs/node/commit/0906f9a8bb)] - **doc**: sort globals alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`6cd06c1319`](https://github.com/nodejs/node/commit/6cd06c1319)] - **doc**: sort fs alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`5b310f8d9e`](https://github.com/nodejs/node/commit/5b310f8d9e)] - **doc**: sort events alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`782cb7d15b`](https://github.com/nodejs/node/commit/782cb7d15b)] - **doc**: sort errors alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`c39eabbec4`](https://github.com/nodejs/node/commit/c39eabbec4)] - **doc**: sort dgram alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`261e0f3a21`](https://github.com/nodejs/node/commit/261e0f3a21)] - **doc**: sort crypto alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`0e6121d04d`](https://github.com/nodejs/node/commit/0e6121d04d)] - **doc**: sort dns alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`435ffb79f7`](https://github.com/nodejs/node/commit/435ffb79f7)] - **doc**: sort console alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`28935a10d6`](https://github.com/nodejs/node/commit/28935a10d6)] - **doc**: sort cluster alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`5e79dc4406`](https://github.com/nodejs/node/commit/5e79dc4406)] - **doc**: sort child\_process alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`af0bf1a72c`](https://github.com/nodejs/node/commit/af0bf1a72c)] - **doc**: sort buffer alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`f43a0330aa`](https://github.com/nodejs/node/commit/f43a0330aa)] - **doc**: sort assert alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`1bbc3b3ff8`](https://github.com/nodejs/node/commit/1bbc3b3ff8)] - **doc**: add note on tls connection meta data methods (Tyler Henkel) [#3746](https://github.com/nodejs/node/pull/3746)
* \[[`3c415bbb12`](https://github.com/nodejs/node/commit/3c415bbb12)] - **doc**: add note to util.isBuffer (Evan Lucas) [#3790](https://github.com/nodejs/node/pull/3790)
* \[[`7b5e4574fd`](https://github.com/nodejs/node/commit/7b5e4574fd)] - **doc**: add romankl to collaborators (Roman Klauke) [#3725](https://github.com/nodejs/node/pull/3725)
* \[[`4f7c638a7a`](https://github.com/nodejs/node/commit/4f7c638a7a)] - **doc**: add saghul as a collaborator (Saúl Ibarra Corretgé)
* \[[`523251270a`](https://github.com/nodejs/node/commit/523251270a)] - **doc**: add thealphanerd to collaborators (Myles Borins) [#3723](https://github.com/nodejs/node/pull/3723)
* \[[`488e74f27d`](https://github.com/nodejs/node/commit/488e74f27d)] - **doc**: update lts description in the collaborator guide (James M Snell) [#3668](https://github.com/nodejs/node/pull/3668)
* \[[`fe3ae3cea4`](https://github.com/nodejs/node/commit/fe3ae3cea4)] - **doc**: add LTS info to COLLABORATOR\_GUIDE.md (Myles Borins) [#3442](https://github.com/nodejs/node/pull/3442)
* \[[`daa10a345e`](https://github.com/nodejs/node/commit/daa10a345e)] - **doc**: typo fix in readme.md (Sam P Gallagher-Bishop) [#3649](https://github.com/nodejs/node/pull/3649)
* \[[`eca5720761`](https://github.com/nodejs/node/commit/eca5720761)] - **doc**: fix wrong date and known issue in changelog.md (James M Snell) [#3650](https://github.com/nodejs/node/pull/3650)
* \[[`83494f8f3e`](https://github.com/nodejs/node/commit/83494f8f3e)] - **doc**: rename iojs-\* groups to nodejs-\* (Steven R. Loomis) [#3634](https://github.com/nodejs/node/pull/3634)
* \[[`347fb65aee`](https://github.com/nodejs/node/commit/347fb65aee)] - **doc**: fix crypto spkac function descriptions (Jason Gerfen) [#3614](https://github.com/nodejs/node/pull/3614)
* \[[`11d2050d63`](https://github.com/nodejs/node/commit/11d2050d63)] - **doc**: Updated streams simplified constructor API (Tom Gallacher) [#3602](https://github.com/nodejs/node/pull/3602)
* \[[`6db4392bfb`](https://github.com/nodejs/node/commit/6db4392bfb)] - **doc**: made code spans more visible in the API docs (phijohns) [#3573](https://github.com/nodejs/node/pull/3573)
* \[[`8a7dd73af1`](https://github.com/nodejs/node/commit/8a7dd73af1)] - **doc**: added what buf.copy returns (Manuel B) [#3555](https://github.com/nodejs/node/pull/3555)
* \[[`cf4b65c2d6`](https://github.com/nodejs/node/commit/cf4b65c2d6)] - **doc**: fix function param order in assert doc (David Woods) [#3533](https://github.com/nodejs/node/pull/3533)
* \[[`a2efe4c72b`](https://github.com/nodejs/node/commit/a2efe4c72b)] - **doc**: add note about timeout delay > TIMEOUT\_MAX (Guilherme Souza) [#3512](https://github.com/nodejs/node/pull/3512)
* \[[`d1b5833476`](https://github.com/nodejs/node/commit/d1b5833476)] - **doc**: add caveats of algs and key size in crypto (Shigeki Ohtsu) [#3479](https://github.com/nodejs/node/pull/3479)
* \[[`12cdf6fcf3`](https://github.com/nodejs/node/commit/12cdf6fcf3)] - **doc**: add method links in events.markdown (Alejandro Oviedo) [#3187](https://github.com/nodejs/node/pull/3187)
* \[[`f50f19e384`](https://github.com/nodejs/node/commit/f50f19e384)] - **doc**: stdout/stderr can block when directed to file (Ben Noordhuis) [#3170](https://github.com/nodejs/node/pull/3170)
* \[[`b2cc1302e0`](https://github.com/nodejs/node/commit/b2cc1302e0)] - **docs**: improve discoverability of Code of Conduct (Ashley Williams) [#3774](https://github.com/nodejs/node/pull/3774)
* \[[`fa1ab497f1`](https://github.com/nodejs/node/commit/fa1ab497f1)] - **docs**: fs - change links to buffer encoding to Buffer class anchor (fansworld-claudio) [#2796](https://github.com/nodejs/node/pull/2796)
* \[[`34e64e5390`](https://github.com/nodejs/node/commit/34e64e5390)] - **domains**: fix handling of uncaught exceptions (Julien Gilli) [#3884](https://github.com/nodejs/node/pull/3884)
* \[[`0311836e7a`](https://github.com/nodejs/node/commit/0311836e7a)] - **meta**: remove use of profanity in source (Myles Borins) [#4122](https://github.com/nodejs/node/pull/4122)
* \[[`971762ada9`](https://github.com/nodejs/node/commit/971762ada9)] - **module**: cache regular expressions (Evan Lucas) [#3869](https://github.com/nodejs/node/pull/3869)
* \[[`d80fa2c77c`](https://github.com/nodejs/node/commit/d80fa2c77c)] - **module**: remove unnecessary JSON.stringify (Andres Suarez) [#3578](https://github.com/nodejs/node/pull/3578)
* \[[`aa85d62f09`](https://github.com/nodejs/node/commit/aa85d62f09)] - **net**: add local address/port for better errors (Jan Schär) [#3946](https://github.com/nodejs/node/pull/3946)
* \[[`803a56de52`](https://github.com/nodejs/node/commit/803a56de52)] - **querystring**: Parse multiple separator characters (Yosuke Furukawa) [#3807](https://github.com/nodejs/node/pull/3807)
* \[[`ff02b295fc`](https://github.com/nodejs/node/commit/ff02b295fc)] - **repl**: don't crash if cannot open history file (Evan Lucas) [#3630](https://github.com/nodejs/node/pull/3630)
* \[[`329e88e545`](https://github.com/nodejs/node/commit/329e88e545)] - **repl**: To exit, press ^C again or type .exit. (Hemanth.HM) [#3368](https://github.com/nodejs/node/pull/3368)
* \[[`9b05905361`](https://github.com/nodejs/node/commit/9b05905361)] - **src**: Revert "nix stdin \_readableState.reading" (Roman Reiss) [#3490](https://github.com/nodejs/node/pull/3490)
* \[[`957c1f2543`](https://github.com/nodejs/node/commit/957c1f2543)] - **stream\_wrap**: error if stream has StringDecoder (Fedor Indutny) [#4031](https://github.com/nodejs/node/pull/4031)
* \[[`43e3b69dae`](https://github.com/nodejs/node/commit/43e3b69dae)] - **test**: refactor test-http-exit-delay (Rich Trott) [#4055](https://github.com/nodejs/node/pull/4055)
* \[[`541d0d21be`](https://github.com/nodejs/node/commit/541d0d21be)] - **test**: fix cluster-disconnect-handles flakiness (Santiago Gimeno) [#4009](https://github.com/nodejs/node/pull/4009)
* \[[`5f66d66e84`](https://github.com/nodejs/node/commit/5f66d66e84)] - **test**: don't check the # of chunks in test-http-1.0 (Santiago Gimeno) [#3961](https://github.com/nodejs/node/pull/3961)
* \[[`355edf585b`](https://github.com/nodejs/node/commit/355edf585b)] - **test**: fix cluster-worker-isdead (Santiago Gimeno) [#3954](https://github.com/nodejs/node/pull/3954)
* \[[`4e46e04002`](https://github.com/nodejs/node/commit/4e46e04002)] - **test**: add test for repl.defineCommand() (Bryan English) [#3908](https://github.com/nodejs/node/pull/3908)
* \[[`4ea1a69c53`](https://github.com/nodejs/node/commit/4ea1a69c53)] - **test**: mark test flaky on FreeBSD (Rich Trott) [#4016](https://github.com/nodejs/node/pull/4016)
* \[[`05b64c11f5`](https://github.com/nodejs/node/commit/05b64c11f5)] - **test**: mark cluster-net-send test flaky on windows (Rich Trott) [#4006](https://github.com/nodejs/node/pull/4006)
* \[[`695015579b`](https://github.com/nodejs/node/commit/695015579b)] - **test**: remove flaky designation from ls-no-sslv3 (Rich Trott) [#3620](https://github.com/nodejs/node/pull/3620)
* \[[`abbd87b273`](https://github.com/nodejs/node/commit/abbd87b273)] - **test**: mark fork regression test flaky on windows (Rich Trott) [#4005](https://github.com/nodejs/node/pull/4005)
* \[[`38ba152a7a`](https://github.com/nodejs/node/commit/38ba152a7a)] - **test**: skip test if in FreeBSD jail (Rich Trott) [#3995](https://github.com/nodejs/node/pull/3995)
* \[[`cc24f0ea58`](https://github.com/nodejs/node/commit/cc24f0ea58)] - **test**: fix test-domain-exit-dispose-again (Julien Gilli) [#3990](https://github.com/nodejs/node/pull/3990)
* \[[`b2f1014d26`](https://github.com/nodejs/node/commit/b2f1014d26)] - **test**: remove flaky status for cluster test (Rich Trott) [#3975](https://github.com/nodejs/node/pull/3975)
* \[[`e66794fd30`](https://github.com/nodejs/node/commit/e66794fd30)] - **test**: address flaky test-http-client-timeout-event (Rich Trott) [#3968](https://github.com/nodejs/node/pull/3968)
* \[[`5a2727421a`](https://github.com/nodejs/node/commit/5a2727421a)] - **test**: retry on smartos if ECONNREFUSED (Rich Trott) [#3941](https://github.com/nodejs/node/pull/3941)
* \[[`dbc85a275c`](https://github.com/nodejs/node/commit/dbc85a275c)] - **test**: avoid test timeouts on rpi (Stefan Budeanu) [#3902](https://github.com/nodejs/node/pull/3902)
* \[[`b9d7378d20`](https://github.com/nodejs/node/commit/b9d7378d20)] - **test**: fix flaky test-child-process-spawnsync-input (Rich Trott) [#3889](https://github.com/nodejs/node/pull/3889)
* \[[`cca216a034`](https://github.com/nodejs/node/commit/cca216a034)] - **test**: move test-specific function out of common (Rich Trott) [#3871](https://github.com/nodejs/node/pull/3871)
* \[[`fb8df8d6c2`](https://github.com/nodejs/node/commit/fb8df8d6c2)] - **test**: module loading error fix solaris #3798 (fansworld-claudio) [#3855](https://github.com/nodejs/node/pull/3855)
* \[[`9ea6bc1e0f`](https://github.com/nodejs/node/commit/9ea6bc1e0f)] - **test**: skip test if FreeBSD jail will break it (Rich Trott) [#3839](https://github.com/nodejs/node/pull/3839)
* \[[`150f126618`](https://github.com/nodejs/node/commit/150f126618)] - **test**: fix flaky SmartOS test (Rich Trott) [#3830](https://github.com/nodejs/node/pull/3830)
* \[[`603a6f5405`](https://github.com/nodejs/node/commit/603a6f5405)] - **test**: run pipeline flood test in parallel (Rich Trott) [#3811](https://github.com/nodejs/node/pull/3811)
* \[[`4a26f74ee3`](https://github.com/nodejs/node/commit/4a26f74ee3)] - **test**: skip/replace weak crypto tests in FIPS mode (Stefan Budeanu) [#3757](https://github.com/nodejs/node/pull/3757)
* \[[`3f9562b6bd`](https://github.com/nodejs/node/commit/3f9562b6bd)] - **test**: stronger crypto in test fixtures (Stefan Budeanu) [#3759](https://github.com/nodejs/node/pull/3759)
* \[[`1f83eebec5`](https://github.com/nodejs/node/commit/1f83eebec5)] - **test**: increase crypto strength for FIPS standard (Stefan Budeanu) [#3758](https://github.com/nodejs/node/pull/3758)
* \[[`7c5fbf7850`](https://github.com/nodejs/node/commit/7c5fbf7850)] - **test**: add hasFipsCrypto to test/common.js (Stefan Budeanu) [#3756](https://github.com/nodejs/node/pull/3756)
* \[[`f30214f135`](https://github.com/nodejs/node/commit/f30214f135)] - **test**: add test for invalid DSA key size (Stefan Budeanu) [#3756](https://github.com/nodejs/node/pull/3756)
* \[[`9a6c9faafb`](https://github.com/nodejs/node/commit/9a6c9faafb)] - **test**: numeric flags to fs.open (Carl Lei) [#3641](https://github.com/nodejs/node/pull/3641)
* \[[`93d1d3cfcd`](https://github.com/nodejs/node/commit/93d1d3cfcd)] - **test**: refactor test-http-pipeline-flood (Rich Trott) [#3636](https://github.com/nodejs/node/pull/3636)
* \[[`6c23f67504`](https://github.com/nodejs/node/commit/6c23f67504)] - **test**: fix flaky test test-http-pipeline-flood (Devin Nakamura) [#3636](https://github.com/nodejs/node/pull/3636)
* \[[`4e5cae4360`](https://github.com/nodejs/node/commit/4e5cae4360)] - **test**: use really invalid hostname (Sakthipriyan Vairamani) [#3711](https://github.com/nodejs/node/pull/3711)
* \[[`da189f793b`](https://github.com/nodejs/node/commit/da189f793b)] - **test**: Fix test-cluster-worker-exit.js for AIX (Imran Iqbal) [#3666](https://github.com/nodejs/node/pull/3666)
* \[[`7b4194a863`](https://github.com/nodejs/node/commit/7b4194a863)] - **test**: fix test-module-loading-error for musl (Hugues Malphettes) [#3657](https://github.com/nodejs/node/pull/3657)
* \[[`3dc52e99df`](https://github.com/nodejs/node/commit/3dc52e99df)] - **test**: fix test-net-persistent-keepalive for AIX (Imran Iqbal) [#3646](https://github.com/nodejs/node/pull/3646)
* \[[`0e8eb66a78`](https://github.com/nodejs/node/commit/0e8eb66a78)] - **test**: fix path to module for repl test on Windows (Michael Cornacchia) [#3608](https://github.com/nodejs/node/pull/3608)
* \[[`3aecbc86d2`](https://github.com/nodejs/node/commit/3aecbc86d2)] - **test**: add test-zlib-flush-drain (Myles Borins) [#3534](https://github.com/nodejs/node/pull/3534)
* \[[`542d05cbe1`](https://github.com/nodejs/node/commit/542d05cbe1)] - **test**: enhance fs-watch-recursive test (Sakthipriyan Vairamani) [#2599](https://github.com/nodejs/node/pull/2599)
* \[[`0eb0119d64`](https://github.com/nodejs/node/commit/0eb0119d64)] - **tls**: Use SHA1 for sessionIdContext in FIPS mode (Stefan Budeanu) [#3755](https://github.com/nodejs/node/pull/3755)
* \[[`c10c08604c`](https://github.com/nodejs/node/commit/c10c08604c)] - **tls**: remove util and calls to util.format (Myles Borins) [#3456](https://github.com/nodejs/node/pull/3456)
* \[[`a558a570c0`](https://github.com/nodejs/node/commit/a558a570c0)] - **util**: use regexp instead of str.replace().join() (qinjia) [#3689](https://github.com/nodejs/node/pull/3689)
* \[[`47bb94a0c3`](https://github.com/nodejs/node/commit/47bb94a0c3)] - **zlib**: only apply drain listener if given callback (Craig Cavalier) [#3534](https://github.com/nodejs/node/pull/3534)
* \[[`4733a60158`](https://github.com/nodejs/node/commit/4733a60158)] - **zlib**: pass kind to recursive calls to flush (Myles Borins) [#3534](https://github.com/nodejs/node/pull/3534)

<a id="4.2.3"></a>

## 2015-12-04, Version 4.2.3 'Argon' (LTS), @rvagg

Security Update

### Notable changes

* **http**: Fix CVE-2015-8027, a bug whereby an HTTP socket may no longer have a parser associated with it but a pipelined request attempts to trigger a pause or resume on the non-existent parser, a potential denial-of-service vulnerability. (Fedor Indutny)
* **openssl**: Upgrade to 1.0.2e, containing fixes for:
  * CVE-2015-3193 "BN\_mod\_exp may produce incorrect results on x86\_64", an attack may be possible against a Node.js TLS server using DHE key exchange. Details are available at <http://openssl.org/news/secadv/20151203.txt>.
  * CVE-2015-3194 "Certificate verify crash with missing PSS parameter", a potential denial-of-service vector for Node.js TLS servers using client certificate authentication; TLS clients are also impacted. Details are available at <http://openssl.org/news/secadv/20151203.txt>.
    (Shigeki Ohtsu) [#4134](https://github.com/nodejs/node/pull/4134)
* **v8**: Backport fix for CVE-2015-6764, a bug in `JSON.stringify()` that can result in out-of-bounds reads for arrays. (Ben Noordhuis)

### Known issues

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`49bbd563be`](https://github.com/nodejs/node/commit/49bbd563be)] - **deps**: upgrade openssl sources to 1.0.2e (Shigeki Ohtsu) [#4134](https://github.com/nodejs/node/pull/4134)
* \[[`9a063fd492`](https://github.com/nodejs/node/commit/9a063fd492)] - **deps**: backport a7e50a5 from upstream v8 (Ben Noordhuis)
* \[[`07233206e9`](https://github.com/nodejs/node/commit/07233206e9)] - **deps**: backport 6df9a1d from upstream v8 (Ben Noordhuis)
* \[[`1c8e6de78e`](https://github.com/nodejs/node/commit/1c8e6de78e)] - **http**: fix pipeline regression (Fedor Indutny)

<a id="4.2.2"></a>

## 2015-11-03, Version 4.2.2 'Argon' (LTS), @jasnell

### Notable changes

This is an LTS maintenance release that addresses a number of issues:

* \[[`1d0f2cbf87`](https://github.com/nodejs/node/commit/1d0f2cbf87)] - **buffer**: fix value check for writeUInt{B,L}E (Trevor Norris) [#3500](https://github.com/nodejs/node/pull/3500)
* \[[`2a45b72b4a`](https://github.com/nodejs/node/commit/2a45b72b4a)] - **buffer**: don't CHECK on zero-sized realloc (Ben Noordhuis) [#3499](https://github.com/nodejs/node/pull/3499)
* \[[`a6469e901a`](https://github.com/nodejs/node/commit/a6469e901a)] - **deps**: backport 010897c from V8 upstream (Ali Ijaz Sheikh) [#3520](https://github.com/nodejs/node/pull/3520)
* \[[`cadee67c25`](https://github.com/nodejs/node/commit/cadee67c25)] - **deps**: backport 8d6a228 from the v8's upstream (Fedor Indutny) [#3549](https://github.com/nodejs/node/pull/3549)
* \[[`46c8c94055`](https://github.com/nodejs/node/commit/46c8c94055)] - **fs**: reduced duplicate code in fs.write() (ronkorving) [#2947](https://github.com/nodejs/node/pull/2947)
* \[[`0427cdf094`](https://github.com/nodejs/node/commit/0427cdf094)] - **http**: fix stalled pipeline bug (Fedor Indutny) [#3342](https://github.com/nodejs/node/pull/3342)
* \[[`2109708186`](https://github.com/nodejs/node/commit/2109708186)] - **lib**: fix cluster handle leak (Rich Trott) [#3510](https://github.com/nodejs/node/pull/3510)
* \[[`f49c7c6955`](https://github.com/nodejs/node/commit/f49c7c6955)] - **lib**: avoid REPL exit on completion error (Rich Trott) [#3358](https://github.com/nodejs/node/pull/3358)
* \[[`8a2c4aeeaa`](https://github.com/nodejs/node/commit/8a2c4aeeaa)] - **repl**: handle comments properly (Sakthipriyan Vairamani) [#3515](https://github.com/nodejs/node/pull/3515)
* \[[`a04408acce`](https://github.com/nodejs/node/commit/a04408acce)] - **repl**: limit persistent history correctly on load (Jeremiah Senkpiel) [#2356](https://github.com/nodejs/node/pull/2356)
* \[[`3bafe1a59b`](https://github.com/nodejs/node/commit/3bafe1a59b)] - **src**: fix race condition in debug signal on exit (Ben Noordhuis) [#3528](https://github.com/nodejs/node/pull/3528)
* \[[`fe01d0df7a`](https://github.com/nodejs/node/commit/fe01d0df7a)] - **src**: fix exception message encoding on Windows (Brian White) [#3288](https://github.com/nodejs/node/pull/3288)
* \[[`4bac5d9ddf`](https://github.com/nodejs/node/commit/4bac5d9ddf)] - **stream**: avoid unnecessary concat of a single buffer. (Calvin Metcalf) [#3300](https://github.com/nodejs/node/pull/3300)
* \[[`8d78d687d5`](https://github.com/nodejs/node/commit/8d78d687d5)] - **timers**: reuse timer in `setTimeout().unref()` (Fedor Indutny) [#3407](https://github.com/nodejs/node/pull/3407)
* \[[`e69c869399`](https://github.com/nodejs/node/commit/e69c869399)] - **tls**: TLSSocket options default isServer false (Yuval Brik) [#2614](https://github.com/nodejs/node/pull/2614)

### Known issues

* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`1d0f2cbf87`](https://github.com/nodejs/node/commit/1d0f2cbf87)] - **buffer**: fix value check for writeUInt{B,L}E (Trevor Norris) [#3500](https://github.com/nodejs/node/pull/3500)
* \[[`2a45b72b4a`](https://github.com/nodejs/node/commit/2a45b72b4a)] - **buffer**: don't CHECK on zero-sized realloc (Ben Noordhuis) [#3499](https://github.com/nodejs/node/pull/3499)
* \[[`dc655e1dd2`](https://github.com/nodejs/node/commit/dc655e1dd2)] - **build**: rectify --link-module help text (Minqi Pan) [#3379](https://github.com/nodejs/node/pull/3379)
* \[[`a6469e901a`](https://github.com/nodejs/node/commit/a6469e901a)] - **deps**: backport 010897c from V8 upstream (Ali Ijaz Sheikh) [#3520](https://github.com/nodejs/node/pull/3520)
* \[[`cadee67c25`](https://github.com/nodejs/node/commit/cadee67c25)] - **deps**: backport 8d6a228 from the v8's upstream (Fedor Indutny) [#3549](https://github.com/nodejs/node/pull/3549)
* \[[`1ebd35550b`](https://github.com/nodejs/node/commit/1ebd35550b)] - **doc**: fix typos in changelog (reggi) [#3291](https://github.com/nodejs/node/pull/3291)
* \[[`fbd93d4c1c`](https://github.com/nodejs/node/commit/fbd93d4c1c)] - **doc**: more use-cases for promise events (Domenic Denicola) [#3438](https://github.com/nodejs/node/pull/3438)
* \[[`6ceb9af407`](https://github.com/nodejs/node/commit/6ceb9af407)] - **doc**: remove old note, 'cluster' is marked stable (Balázs Galambosi) [#3314](https://github.com/nodejs/node/pull/3314)
* \[[`a5f0d64ddc`](https://github.com/nodejs/node/commit/a5f0d64ddc)] - **doc**: createServer's key option can be an array (Sakthipriyan Vairamani) [#3123](https://github.com/nodejs/node/pull/3123)
* \[[`317e0ec6b3`](https://github.com/nodejs/node/commit/317e0ec6b3)] - **doc**: binary encoding is not deprecated (Trevor Norris) [#3441](https://github.com/nodejs/node/pull/3441)
* \[[`b422f6ee1a`](https://github.com/nodejs/node/commit/b422f6ee1a)] - **doc**: mention the behaviour if URL is invalid (Sakthipriyan Vairamani) [#2966](https://github.com/nodejs/node/pull/2966)
* \[[`bc29aad22b`](https://github.com/nodejs/node/commit/bc29aad22b)] - **doc**: fix indent in tls resumption example (Roman Reiss) [#3372](https://github.com/nodejs/node/pull/3372)
* \[[`313877bd8f`](https://github.com/nodejs/node/commit/313877bd8f)] - **doc**: fix typo in changelog (Timothy Gu) [#3353](https://github.com/nodejs/node/pull/3353)
* \[[`4be432862a`](https://github.com/nodejs/node/commit/4be432862a)] - **doc**: show keylen in pbkdf2 as a byte length (calebboyd) [#3334](https://github.com/nodejs/node/pull/3334)
* \[[`23a1140ddb`](https://github.com/nodejs/node/commit/23a1140ddb)] - **doc**: add information about Assert behavior and maintenance (Rich Trott) [#3330](https://github.com/nodejs/node/pull/3330)
* \[[`e04cb1e1fc`](https://github.com/nodejs/node/commit/e04cb1e1fc)] - **doc**: clarify API buffer.concat (Martii) [#3255](https://github.com/nodejs/node/pull/3255)
* \[[`eae714c370`](https://github.com/nodejs/node/commit/eae714c370)] - **doc**: clarify the use of `option.detached` (Kyle Smith) [#3250](https://github.com/nodejs/node/pull/3250)
* \[[`b884899e67`](https://github.com/nodejs/node/commit/b884899e67)] - **doc**: label v4.2.1 as LTS in changelog heading (Phillip Johnsen) [#3360](https://github.com/nodejs/node/pull/3360)
* \[[`9120a04981`](https://github.com/nodejs/node/commit/9120a04981)] - **docs**: add missing shell option to execSync (fansworld-claudio) [#3440](https://github.com/nodejs/node/pull/3440)
* \[[`46c8c94055`](https://github.com/nodejs/node/commit/46c8c94055)] - **fs**: reduced duplicate code in fs.write() (ronkorving) [#2947](https://github.com/nodejs/node/pull/2947)
* \[[`0427cdf094`](https://github.com/nodejs/node/commit/0427cdf094)] - **http**: fix stalled pipeline bug (Fedor Indutny) [#3342](https://github.com/nodejs/node/pull/3342)
* \[[`2109708186`](https://github.com/nodejs/node/commit/2109708186)] - **lib**: fix cluster handle leak (Rich Trott) [#3510](https://github.com/nodejs/node/pull/3510)
* \[[`f49c7c6955`](https://github.com/nodejs/node/commit/f49c7c6955)] - **lib**: avoid REPL exit on completion error (Rich Trott) [#3358](https://github.com/nodejs/node/pull/3358)
* \[[`8a2c4aeeaa`](https://github.com/nodejs/node/commit/8a2c4aeeaa)] - **repl**: handle comments properly (Sakthipriyan Vairamani) [#3515](https://github.com/nodejs/node/pull/3515)
* \[[`a04408acce`](https://github.com/nodejs/node/commit/a04408acce)] - **repl**: limit persistent history correctly on load (Jeremiah Senkpiel) [#2356](https://github.com/nodejs/node/pull/2356)
* \[[`5d1f1c5fa8`](https://github.com/nodejs/node/commit/5d1f1c5fa8)] - **src**: wrap source before doing syntax check (Evan Lucas) [#3587](https://github.com/nodejs/node/pull/3587)
* \[[`3bafe1a59b`](https://github.com/nodejs/node/commit/3bafe1a59b)] - **src**: fix race condition in debug signal on exit (Ben Noordhuis) [#3528](https://github.com/nodejs/node/pull/3528)
* \[[`fe01d0df7a`](https://github.com/nodejs/node/commit/fe01d0df7a)] - **src**: fix exception message encoding on Windows (Brian White) [#3288](https://github.com/nodejs/node/pull/3288)
* \[[`4bac5d9ddf`](https://github.com/nodejs/node/commit/4bac5d9ddf)] - **stream**: avoid unnecessary concat of a single buffer. (Calvin Metcalf) [#3300](https://github.com/nodejs/node/pull/3300)
* \[[`117fb47a16`](https://github.com/nodejs/node/commit/117fb47a16)] - **stream**: fix signature of \_write() in a comment (Fábio Santos) [#3248](https://github.com/nodejs/node/pull/3248)
* \[[`c563a34427`](https://github.com/nodejs/node/commit/c563a34427)] - **test**: split independent tests into separate files (Rich Trott) [#3548](https://github.com/nodejs/node/pull/3548)
* \[[`3f62952d42`](https://github.com/nodejs/node/commit/3f62952d42)] - **test**: add node::MakeCallback() test coverage (Ben Noordhuis) [#3478](https://github.com/nodejs/node/pull/3478)
* \[[`6b75f10d8a`](https://github.com/nodejs/node/commit/6b75f10d8a)] - **test**: use port number from env in tls socket test (Stefan Budeanu) [#3557](https://github.com/nodejs/node/pull/3557)
* \[[`39ff44e94f`](https://github.com/nodejs/node/commit/39ff44e94f)] - **test**: fix heap-profiler link error LNK1194 on win (Junliang Yan) [#3572](https://github.com/nodejs/node/pull/3572)
* \[[`a2786dd408`](https://github.com/nodejs/node/commit/a2786dd408)] - **test**: fix missing unistd.h on windows (Junliang Yan) [#3532](https://github.com/nodejs/node/pull/3532)
* \[[`5e6f7c9a23`](https://github.com/nodejs/node/commit/5e6f7c9a23)] - **test**: add regression test for --debug-brk -e 0 (Ben Noordhuis) [#3585](https://github.com/nodejs/node/pull/3585)
* \[[`7cad182cb6`](https://github.com/nodejs/node/commit/7cad182cb6)] - **test**: port domains regression test from v0.10 (Jonas Dohse) [#3356](https://github.com/nodejs/node/pull/3356)
* \[[`78d854c6ce`](https://github.com/nodejs/node/commit/78d854c6ce)] - **test**: remove util from common (Rich Trott) [#3324](https://github.com/nodejs/node/pull/3324)
* \[[`c566c8b8c0`](https://github.com/nodejs/node/commit/c566c8b8c0)] - **test**: remove util properties from common (Rich Trott) [#3304](https://github.com/nodejs/node/pull/3304)
* \[[`eb7c3fb2f4`](https://github.com/nodejs/node/commit/eb7c3fb2f4)] - **test**: split up buffer tests for reliability (Rich Trott) [#3323](https://github.com/nodejs/node/pull/3323)
* \[[`b398a85e19`](https://github.com/nodejs/node/commit/b398a85e19)] - **test**: parallelize long-running test (Rich Trott) [#3287](https://github.com/nodejs/node/pull/3287)
* \[[`b5f3b4956b`](https://github.com/nodejs/node/commit/b5f3b4956b)] - **test**: change call to deprecated util.isError() (Rich Trott) [#3084](https://github.com/nodejs/node/pull/3084)
* \[[`32149cacb5`](https://github.com/nodejs/node/commit/32149cacb5)] - **test**: improve tests for util.inherits (Michaël Zasso) [#3507](https://github.com/nodejs/node/pull/3507)
* \[[`5be686fab8`](https://github.com/nodejs/node/commit/5be686fab8)] - **test**: print helpful err msg on test-dns-ipv6.js (Junliang Yan) [#3501](https://github.com/nodejs/node/pull/3501)
* \[[`0429131e32`](https://github.com/nodejs/node/commit/0429131e32)] - **test**: fix domain with abort-on-uncaught on PPC (Julien Gilli) [#3354](https://github.com/nodejs/node/pull/3354)
* \[[`788106eee9`](https://github.com/nodejs/node/commit/788106eee9)] - **test**: cleanup, improve repl-persistent-history (Jeremiah Senkpiel) [#2356](https://github.com/nodejs/node/pull/2356)
* \[[`ea58fa0bac`](https://github.com/nodejs/node/commit/ea58fa0bac)] - **test**: add Symbol test for assert.deepEqual() (Rich Trott) [#3327](https://github.com/nodejs/node/pull/3327)
* \[[`d409ac473b`](https://github.com/nodejs/node/commit/d409ac473b)] - **test**: disable test-tick-processor - aix and be ppc (Michael Dawson) [#3491](https://github.com/nodejs/node/pull/3491)
* \[[`c1623039dd`](https://github.com/nodejs/node/commit/c1623039dd)] - **test**: harden test-child-process-fork-regr-gh-2847 (Michael Dawson) [#3459](https://github.com/nodejs/node/pull/3459)
* \[[`3bb4437abb`](https://github.com/nodejs/node/commit/3bb4437abb)] - **test**: fix test-net-keepalive for AIX (Imran Iqbal) [#3458](https://github.com/nodejs/node/pull/3458)
* \[[`af55641a69`](https://github.com/nodejs/node/commit/af55641a69)] - **test**: wrap assert.fail when passed to callback (Myles Borins) [#3453](https://github.com/nodejs/node/pull/3453)
* \[[`7c7ef01e65`](https://github.com/nodejs/node/commit/7c7ef01e65)] - **test**: skip test-dns-ipv6.js if ipv6 is unavailable (Junliang Yan) [#3444](https://github.com/nodejs/node/pull/3444)
* \[[`a4d1510ba4`](https://github.com/nodejs/node/commit/a4d1510ba4)] - **test**: repl-persistent-history is no longer flaky (Jeremiah Senkpiel) [#3437](https://github.com/nodejs/node/pull/3437)
* \[[`a5d968b8a2`](https://github.com/nodejs/node/commit/a5d968b8a2)] - **test**: fix flaky test-child-process-emfile (Rich Trott) [#3430](https://github.com/nodejs/node/pull/3430)
* \[[`eac2acca76`](https://github.com/nodejs/node/commit/eac2acca76)] - **test**: remove flaky status from eval\_messages test (Rich Trott) [#3420](https://github.com/nodejs/node/pull/3420)
* \[[`155c778584`](https://github.com/nodejs/node/commit/155c778584)] - **test**: fix flaky test for symlinks (Rich Trott) [#3418](https://github.com/nodejs/node/pull/3418)
* \[[`74eb632483`](https://github.com/nodejs/node/commit/74eb632483)] - **test**: apply correct assert.fail() arguments (Rich Trott) [#3378](https://github.com/nodejs/node/pull/3378)
* \[[`0a4323dd82`](https://github.com/nodejs/node/commit/0a4323dd82)] - **test**: replace util with backtick strings (Myles Borins) [#3359](https://github.com/nodejs/node/pull/3359)
* \[[`93847694ec`](https://github.com/nodejs/node/commit/93847694ec)] - **test**: add test-child-process-emfile fail message (Rich Trott) [#3335](https://github.com/nodejs/node/pull/3335)
* \[[`8d78d687d5`](https://github.com/nodejs/node/commit/8d78d687d5)] - **timers**: reuse timer in `setTimeout().unref()` (Fedor Indutny) [#3407](https://github.com/nodejs/node/pull/3407)
* \[[`e69c869399`](https://github.com/nodejs/node/commit/e69c869399)] - **tls**: TLSSocket options default isServer false (Yuval Brik) [#2614](https://github.com/nodejs/node/pull/2614)
* \[[`0b32bbbf69`](https://github.com/nodejs/node/commit/0b32bbbf69)] - **v8**: pull fix for builtin code size on PPC (Michael Dawson) [#3474](https://github.com/nodejs/node/pull/3474)

<a id="4.2.1"></a>

## 2015-10-13, Version 4.2.1 'Argon' (LTS), @jasnell

### Notable changes

* Includes fixes for two regressions
  * Assertion error in WeakCallback - see [#3329](https://github.com/nodejs/node/pull/3329)
  * Undefined timeout regression - see [#3331](https://github.com/nodejs/node/pull/3331)

### Known issues

* When a server queues a large amount of data to send to a client over a pipelined HTTP connection, the underlying socket may be destroyed. See [#3332](https://github.com/nodejs/node/issues/3332) and [#3342](https://github.com/nodejs/node/pull/3342).
* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`b3cbd13340`](https://github.com/nodejs/node/commit/b3cbd13340)] - **buffer**: fix assertion error in WeakCallback (Fedor Indutny) [#3329](https://github.com/nodejs/node/pull/3329)
* \[[`102cb7288c`](https://github.com/nodejs/node/commit/102cb7288c)] - **doc**: label v4.2.0 as LTS in changelog heading (Rod Vagg) [#3343](https://github.com/nodejs/node/pull/3343)
* \[[`c245a199a7`](https://github.com/nodejs/node/commit/c245a199a7)] - **lib**: fix undefined timeout regression (Ryan Graham) [#3331](https://github.com/nodejs/node/pull/3331)

<a id="4.2.0"></a>

## 2015-10-07, Version 4.2.0 'Argon' (LTS), @jasnell

### Notable changes

The first Node.js LTS release! See <https://github.com/nodejs/LTS/> for details of the LTS process.

* **icu**: Updated to version 56 with significant performance improvements (Steven R. Loomis) [#3281](https://github.com/nodejs/node/pull/3281)
* **node**:
  * Added new `-c` (or `--check`) command line argument for checking script syntax without executing the code (Dave Eddy) [#2411](https://github.com/nodejs/node/pull/2411)
  * Added `process.versions.icu` to hold the current ICU library version (Evan Lucas) [#3102](https://github.com/nodejs/node/pull/3102)
  * Added `process.release.lts` to hold the current LTS codename when the binary is from an active LTS release line (Rod Vagg) [#3212](https://github.com/nodejs/node/pull/3212)
* **npm**: Upgraded to npm 2.14.7 from 2.14.4, see [release notes](https://github.com/npm/npm/releases/tag/v2.14.7) for full details (Kat Marchán) [#3299](https://github.com/nodejs/node/pull/3299)

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`8383c4fe00`](https://github.com/nodejs/node/commit/8383c4fe00)] - **assert**: support arrow functions in .throws() (Ben Noordhuis) [#3276](https://github.com/nodejs/node/pull/3276)
* \[[`3eaa593a32`](https://github.com/nodejs/node/commit/3eaa593a32)] - **async\_wrap**: correctly pass parent to init callback (Trevor Norris) [#3216](https://github.com/nodejs/node/pull/3216)
* \[[`54795620f6`](https://github.com/nodejs/node/commit/54795620f6)] - **buffer**: don't abort on prototype getters (Trevor Norris) [#3302](https://github.com/nodejs/node/pull/3302)
* \[[`660f7591c8`](https://github.com/nodejs/node/commit/660f7591c8)] - **buffer**: FreeCallback should be tied to ArrayBuffer (Fedor Indutny) [#3198](https://github.com/nodejs/node/pull/3198)
* \[[`651a5b51eb`](https://github.com/nodejs/node/commit/651a5b51eb)] - **buffer**: only check if instance is Uint8Array (Trevor Norris) [#3080](https://github.com/nodejs/node/pull/3080)
* \[[`d5a1b1ad7c`](https://github.com/nodejs/node/commit/d5a1b1ad7c)] - **buffer**: clean up usage of `__proto__` (Trevor Norris) [#3080](https://github.com/nodejs/node/pull/3080)
* \[[`af24376e18`](https://github.com/nodejs/node/commit/af24376e18)] - **build**: Intl: deps: bump ICU to 56.1 (GA) (Steven R. Loomis) [#3281](https://github.com/nodejs/node/pull/3281)
* \[[`9136359d57`](https://github.com/nodejs/node/commit/9136359d57)] - **build**: make icu download path customizable (Johan Bergström) [#3200](https://github.com/nodejs/node/pull/3200)
* \[[`b3c5ad10a8`](https://github.com/nodejs/node/commit/b3c5ad10a8)] - **build**: add --with-arm-fpu option (Jérémy Lal) [#3228](https://github.com/nodejs/node/pull/3228)
* \[[`f00f3268e4`](https://github.com/nodejs/node/commit/f00f3268e4)] - **build**: intl: avoid 'duplicate main()' on ICU 56 (Steven R. Loomis) [#3066](https://github.com/nodejs/node/pull/3066)
* \[[`071c72a6a3`](https://github.com/nodejs/node/commit/071c72a6a3)] - **deps**: upgrade to npm 2.14.7 (Kat Marchán) [#3299](https://github.com/nodejs/node/pull/3299)
* \[[`8b50e95f06`](https://github.com/nodejs/node/commit/8b50e95f06)] - **(SEMVER-MINOR)** **deps**: backport 1ee712a from V8 upstream (Julien Gilli) [#3036](https://github.com/nodejs/node/pull/3036)
* \[[`747271372f`](https://github.com/nodejs/node/commit/747271372f)] - **doc**: update the assert module summary (David Boivin) [#2799](https://github.com/nodejs/node/pull/2799)
* \[[`0d506556b0`](https://github.com/nodejs/node/commit/0d506556b0)] - **doc**: replace node-gyp link with nodejs/node-gyp (Roman Klauke) [#3320](https://github.com/nodejs/node/pull/3320)
* \[[`40a159e4f4`](https://github.com/nodejs/node/commit/40a159e4f4)] - **doc**: Amend capitalization of word JavaScript (Dave Hodder) [#3285](https://github.com/nodejs/node/pull/3285)
* \[[`6dd34761fd`](https://github.com/nodejs/node/commit/6dd34761fd)] - **doc**: add method links in dns.markdown (Alejandro Oviedo) [#3196](https://github.com/nodejs/node/pull/3196)
* \[[`333e8336be`](https://github.com/nodejs/node/commit/333e8336be)] - **doc**: add method links in child\_process.markdown (Alejandro Oviedo) [#3186](https://github.com/nodejs/node/pull/3186)
* \[[`0cfc6d39ca`](https://github.com/nodejs/node/commit/0cfc6d39ca)] - **doc**: recommend Infinity on emitter.setMaxListeners (Jason Karns) [#2559](https://github.com/nodejs/node/pull/2559)
* \[[`d4fc6d93ef`](https://github.com/nodejs/node/commit/d4fc6d93ef)] - **doc**: add help repo link to CONTRIBUTING.md (Doug Shamoo) [#3233](https://github.com/nodejs/node/pull/3233)
* \[[`28aac7f19d`](https://github.com/nodejs/node/commit/28aac7f19d)] - **doc**: add TLS session resumption example (Roman Reiss) [#3147](https://github.com/nodejs/node/pull/3147)
* \[[`365cf22cce`](https://github.com/nodejs/node/commit/365cf22cce)] - **doc**: update AUTHORS list (Rod Vagg) [#3211](https://github.com/nodejs/node/pull/3211)
* \[[`d4399613b7`](https://github.com/nodejs/node/commit/d4399613b7)] - **doc**: standardize references to userland (Martial) [#3192](https://github.com/nodejs/node/pull/3192)
* \[[`75de258376`](https://github.com/nodejs/node/commit/75de258376)] - **doc**: fix spelling in Buffer documentation (Rod Machen) [#3226](https://github.com/nodejs/node/pull/3226)
* \[[`725c7276dd`](https://github.com/nodejs/node/commit/725c7276dd)] - **doc**: fix README.md link to joyent/node intl wiki (Steven R. Loomis) [#3067](https://github.com/nodejs/node/pull/3067)
* \[[`4a35ba4966`](https://github.com/nodejs/node/commit/4a35ba4966)] - **(SEMVER-MINOR)** **fs**: include filename in watch errors (charlierudolph) [#2748](https://github.com/nodejs/node/pull/2748)
* \[[`2ddbbfd164`](https://github.com/nodejs/node/commit/2ddbbfd164)] - **http**: cork/uncork before flushing pipelined res (Fedor Indutny) [#3172](https://github.com/nodejs/node/pull/3172)
* \[[`f638402e2f`](https://github.com/nodejs/node/commit/f638402e2f)] - **http**: add comment about `outputSize` in res/server (Fedor Indutny) [#3128](https://github.com/nodejs/node/pull/3128)
* \[[`1850879b0e`](https://github.com/nodejs/node/commit/1850879b0e)] - **js\_stream**: prevent abort if isalive doesn't exist (Trevor Norris) [#3282](https://github.com/nodejs/node/pull/3282)
* \[[`63644dd1cd`](https://github.com/nodejs/node/commit/63644dd1cd)] - **lib**: remove redundant code, add tests in timers.js (Rich Trott) [#3143](https://github.com/nodejs/node/pull/3143)
* \[[`74f443583c`](https://github.com/nodejs/node/commit/74f443583c)] - **module**: use UNC paths when loading native addons (Justin Chase) [#2965](https://github.com/nodejs/node/pull/2965)
* \[[`01cb3fc36b`](https://github.com/nodejs/node/commit/01cb3fc36b)] - **net**: don't throw on bytesWritten access (Trevor Norris) [#3305](https://github.com/nodejs/node/pull/3305)
* \[[`9d65528b01`](https://github.com/nodejs/node/commit/9d65528b01)] - **(SEMVER-MINOR)** **node**: add -c|--check CLI arg to syntax check script (Dave Eddy) [#2411](https://github.com/nodejs/node/pull/2411)
* \[[`42b936e78d`](https://github.com/nodejs/node/commit/42b936e78d)] - **(SEMVER-MINOR)** **src**: add process.release.lts property (Rod Vagg) [#3212](https://github.com/nodejs/node/pull/3212)
* \[[`589287b2e3`](https://github.com/nodejs/node/commit/589287b2e3)] - **src**: convert BE-utf16-string to LE before search (Karl Skomski) [#3295](https://github.com/nodejs/node/pull/3295)
* \[[`2314378f06`](https://github.com/nodejs/node/commit/2314378f06)] - **src**: fix u-a-free if uv returns err in ASYNC\_CALL (Karl Skomski) [#3049](https://github.com/nodejs/node/pull/3049)
* \[[`d99336a391`](https://github.com/nodejs/node/commit/d99336a391)] - **(SEMVER-MINOR)** **src**: replace naive search in Buffer::IndexOf (Karl Skomski) [#2539](https://github.com/nodejs/node/pull/2539)
* \[[`546e8333ba`](https://github.com/nodejs/node/commit/546e8333ba)] - **(SEMVER-MINOR)** **src**: fix --abort-on-uncaught-exception (Jeremy Whitlock) [#3036](https://github.com/nodejs/node/pull/3036)
* \[[`7271cb047c`](https://github.com/nodejs/node/commit/7271cb047c)] - **(SEMVER-MINOR)** **src**: add process.versions.icu (Evan Lucas) [#3102](https://github.com/nodejs/node/pull/3102)
* \[[`7b9f78acb2`](https://github.com/nodejs/node/commit/7b9f78acb2)] - **stream**: avoid pause with unpipe in buffered write (Brian White) [#2325](https://github.com/nodejs/node/pull/2325)
* \[[`f0f8afd879`](https://github.com/nodejs/node/commit/f0f8afd879)] - **test**: remove common.inspect() (Rich Trott) [#3257](https://github.com/nodejs/node/pull/3257)
* \[[`5ca4f6f8bd`](https://github.com/nodejs/node/commit/5ca4f6f8bd)] - **test**: test `util` rather than `common` (Rich Trott) [#3256](https://github.com/nodejs/node/pull/3256)
* \[[`7a5ae34345`](https://github.com/nodejs/node/commit/7a5ae34345)] - **test**: refresh temp directory when using pipe (Rich Trott) [#3231](https://github.com/nodejs/node/pull/3231)
* \[[`7c85557ef0`](https://github.com/nodejs/node/commit/7c85557ef0)] - **test**: Fix test-fs-read-stream-fd-leak race cond (Junliang Yan) [#3218](https://github.com/nodejs/node/pull/3218)
* \[[`26a7ec6960`](https://github.com/nodejs/node/commit/26a7ec6960)] - **test**: fix losing original env vars issue (Junliang Yan) [#3190](https://github.com/nodejs/node/pull/3190)
* \[[`e922716192`](https://github.com/nodejs/node/commit/e922716192)] - **test**: remove deprecated error logging (Rich Trott) [#3079](https://github.com/nodejs/node/pull/3079)
* \[[`8f29d95a8c`](https://github.com/nodejs/node/commit/8f29d95a8c)] - **test**: report timeout in TapReporter (Karl Skomski) [#2647](https://github.com/nodejs/node/pull/2647)
* \[[`2d0fe4c657`](https://github.com/nodejs/node/commit/2d0fe4c657)] - **test**: linting for buffer-free-callback test (Rich Trott) [#3230](https://github.com/nodejs/node/pull/3230)
* \[[`70c9e4337e`](https://github.com/nodejs/node/commit/70c9e4337e)] - **test**: make common.js mandatory via linting rule (Rich Trott) [#3157](https://github.com/nodejs/node/pull/3157)
* \[[`b7179562aa`](https://github.com/nodejs/node/commit/b7179562aa)] - **test**: load common.js in all tests (Rich Trott) [#3157](https://github.com/nodejs/node/pull/3157)
* \[[`bab555a1c1`](https://github.com/nodejs/node/commit/bab555a1c1)] - **test**: speed up stringbytes-external test (Evan Lucas) [#3005](https://github.com/nodejs/node/pull/3005)
* \[[`ddf258376d`](https://github.com/nodejs/node/commit/ddf258376d)] - **test**: use normalize() for unicode paths (Roman Reiss) [#3007](https://github.com/nodejs/node/pull/3007)
* \[[`46876d519c`](https://github.com/nodejs/node/commit/46876d519c)] - **test**: remove arguments.callee usage (Roman Reiss) [#3167](https://github.com/nodejs/node/pull/3167)
* \[[`af10df6108`](https://github.com/nodejs/node/commit/af10df6108)] - **tls**: use parent handle's close callback (Fedor Indutny) [#2991](https://github.com/nodejs/node/pull/2991)
* \[[`9c2748bad1`](https://github.com/nodejs/node/commit/9c2748bad1)] - **tools**: remove leftover license boilerplate (Nathan Rajlich) [#3225](https://github.com/nodejs/node/pull/3225)
* \[[`5d9f83ff2a`](https://github.com/nodejs/node/commit/5d9f83ff2a)] - **tools**: apply linting to custom rules code (Rich Trott) [#3195](https://github.com/nodejs/node/pull/3195)
* \[[`18a8b2ec73`](https://github.com/nodejs/node/commit/18a8b2ec73)] - **tools**: remove unused gflags module (Ben Noordhuis) [#3220](https://github.com/nodejs/node/pull/3220)
* \[[`e0fffca836`](https://github.com/nodejs/node/commit/e0fffca836)] - **util**: fix for inspecting promises (Evan Lucas) [#3221](https://github.com/nodejs/node/pull/3221)
* \[[`8dfdee3733`](https://github.com/nodejs/node/commit/8dfdee3733)] - **util**: correctly inspect Map/Set Iterators (Evan Lucas) [#3119](https://github.com/nodejs/node/pull/3119)
* \[[`b5c51fdba0`](https://github.com/nodejs/node/commit/b5c51fdba0)] - **util**: fix check for Array constructor (Evan Lucas) [#3119](https://github.com/nodejs/node/pull/3119)

<a id="4.1.2"></a>

## 2015-10-05, Version 4.1.2 (Stable), @rvagg

### Notable changes

* **http**:
  * Fix out-of-order 'finish' event bug in pipelining that can abort execution, fixes DoS vulnerability [CVE-2015-7384](https://github.com/nodejs/node/issues/3138) (Fedor Indutny) [#3128](https://github.com/nodejs/node/pull/3128)
  * Account for pending response data instead of just the data on the current request to decide whether pause the socket or not (Fedor Indutny) [#3128](https://github.com/nodejs/node/pull/3128)
* **libuv**: Upgraded from v1.7.4 to v1.7.5, see [release notes](https://github.com/libuv/libuv/releases/tag/v1.7.5) for details (Saúl Ibarra Corretgé) [#3010](https://github.com/nodejs/node/pull/3010)
  * A better rwlock implementation for all Windows versions
  * Improved AIX support
* **v8**:
  * Upgraded from v4.5.103.33 to v4.5.103.35 (Ali Ijaz Sheikh) [#3117](https://github.com/nodejs/node/pull/3117)
  * Backported [f782159](https://codereview.chromium.org/1367123003) from v8's upstream to help speed up Promise introspection (Ben Noordhuis) [#3130](https://github.com/nodejs/node/pull/3130)
  * Backported [c281c15](https://codereview.chromium.org/1363683002) from v8's upstream to add JSTypedArray length in post-mortem metadata (Julien Gilli) [#3031](https://github.com/nodejs/node/pull/3031)

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`39b8730e8b`](https://github.com/nodejs/node/commit/39b8730e8b)] - **async\_wrap**: ensure all objects have internal field (Trevor Norris) [#3139](https://github.com/nodejs/node/pull/3139)
* \[[`99e66074d7`](https://github.com/nodejs/node/commit/99e66074d7)] - **async\_wrap**: update providers and add test (Trevor Norris) [#3139](https://github.com/nodejs/node/pull/3139)
* \[[`7a58157d4e`](https://github.com/nodejs/node/commit/7a58157d4e)] - **benchmark**: update comment in common.js (Minwoo Jung) [#2399](https://github.com/nodejs/node/pull/2399)
* \[[`9e9bfa4dc0`](https://github.com/nodejs/node/commit/9e9bfa4dc0)] - **build**: iojs -> nodejs of release-urlbase (Minqi Pan) [#3015](https://github.com/nodejs/node/pull/3015)
* \[[`8335ec7191`](https://github.com/nodejs/node/commit/8335ec7191)] - **build**: fix some typos inside the configure script (Minqi Pan) [#3016](https://github.com/nodejs/node/pull/3016)
* \[[`d6ac547d5d`](https://github.com/nodejs/node/commit/d6ac547d5d)] - **build,win**: fix node.exe resource version (João Reis) [#3053](https://github.com/nodejs/node/pull/3053)
* \[[`798dad24f4`](https://github.com/nodejs/node/commit/798dad24f4)] - **child\_process**: `null` channel handle on close (Fedor Indutny) [#3041](https://github.com/nodejs/node/pull/3041)
* \[[`e5615854ea`](https://github.com/nodejs/node/commit/e5615854ea)] - **contextify**: use CHECK instead of `if` (Oguz Bastemur) [#3125](https://github.com/nodejs/node/pull/3125)
* \[[`f055a66a38`](https://github.com/nodejs/node/commit/f055a66a38)] - **crypto**: enable FIPS only when configured with it (Fedor Indutny) [#3153](https://github.com/nodejs/node/pull/3153)
* \[[`4c8d96bc30`](https://github.com/nodejs/node/commit/4c8d96bc30)] - **crypto**: add more keylen sanity checks in pbkdf2 (Johann) [#3029](https://github.com/nodejs/node/pull/3029)
* \[[`4c5940776c`](https://github.com/nodejs/node/commit/4c5940776c)] - **deps**: upgrade libuv to 1.7.5 (Saúl Ibarra Corretgé) [#3010](https://github.com/nodejs/node/pull/3010)
* \[[`5a9e795577`](https://github.com/nodejs/node/commit/5a9e795577)] - **deps**: upgrade V8 to 4.5.103.35 (Ali Ijaz Sheikh) [#3117](https://github.com/nodejs/node/pull/3117)
* \[[`925b29f959`](https://github.com/nodejs/node/commit/925b29f959)] - **deps**: backport f782159 from v8's upstream (Ben Noordhuis) [#3130](https://github.com/nodejs/node/pull/3130)
* \[[`039f73fa83`](https://github.com/nodejs/node/commit/039f73fa83)] - **deps**: remove and gitignore .bin directory (Ben Noordhuis) [#3004](https://github.com/nodejs/node/pull/3004)
* \[[`5fbb24812d`](https://github.com/nodejs/node/commit/5fbb24812d)] - **deps**: backport c281c15 from V8's upstream (Julien Gilli) [#3031](https://github.com/nodejs/node/pull/3031)
* \[[`6ee5d0f69f`](https://github.com/nodejs/node/commit/6ee5d0f69f)] - **dns**: add missing exports.BADNAME (Roman Reiss) [#3051](https://github.com/nodejs/node/pull/3051)
* \[[`f92aee7170`](https://github.com/nodejs/node/commit/f92aee7170)] - **doc**: fix outdated 'try/catch' statement in sync (Minwoo Jung) [#3087](https://github.com/nodejs/node/pull/3087)
* \[[`c7161f39e8`](https://github.com/nodejs/node/commit/c7161f39e8)] - **doc**: add TSC meeting minutes 2015-09-16 (Rod Vagg) [#3023](https://github.com/nodejs/node/pull/3023)
* \[[`928166c4a8`](https://github.com/nodejs/node/commit/928166c4a8)] - **doc**: copyedit fs.watch() information (Rich Trott) [#3097](https://github.com/nodejs/node/pull/3097)
* \[[`75d5dcea76`](https://github.com/nodejs/node/commit/75d5dcea76)] - **doc**: jenkins-iojs.nodesource.com -> ci.nodejs.org (Michał Gołębiowski) [#2886](https://github.com/nodejs/node/pull/2886)
* \[[`5c3f50b21d`](https://github.com/nodejs/node/commit/5c3f50b21d)] - **doc**: rearrange execSync and execFileSync (Laurent Fortin) [#2940](https://github.com/nodejs/node/pull/2940)
* \[[`4fc33ac11a`](https://github.com/nodejs/node/commit/4fc33ac11a)] - **doc**: make execFileSync in line with execFile (Laurent Fortin) [#2940](https://github.com/nodejs/node/pull/2940)
* \[[`a366e84b17`](https://github.com/nodejs/node/commit/a366e84b17)] - **doc**: fix typos in cluster & errors (reggi) [#3011](https://github.com/nodejs/node/pull/3011)
* \[[`52031e1bf1`](https://github.com/nodejs/node/commit/52031e1bf1)] - **doc**: switch LICENSE from closure-linter to eslint (Minqi Pan) [#3018](https://github.com/nodejs/node/pull/3018)
* \[[`b28f6a53bc`](https://github.com/nodejs/node/commit/b28f6a53bc)] - **docs**: Clarify assert.doesNotThrow behavior (Fabio Oliveira) [#2807](https://github.com/nodejs/node/pull/2807)
* \[[`99943e189d`](https://github.com/nodejs/node/commit/99943e189d)] - **http**: fix out-of-order 'finish' bug in pipelining (Fedor Indutny) [#3128](https://github.com/nodejs/node/pull/3128)
* \[[`fb7a491d1c`](https://github.com/nodejs/node/commit/fb7a491d1c)] - **http\_server**: pause socket properly (Fedor Indutny) [#3128](https://github.com/nodejs/node/pull/3128)
* \[[`a0b35bfcf3`](https://github.com/nodejs/node/commit/a0b35bfcf3)] - **i18n**: add caller to removal list for bidi in ICU55 (Michael Dawson) [#3115](https://github.com/nodejs/node/pull/3115)
* \[[`ac2bce0b0c`](https://github.com/nodejs/node/commit/ac2bce0b0c)] - **path**: improve posixSplitPath performance (Evan Lucas) [#3034](https://github.com/nodejs/node/pull/3034)
* \[[`37cdeafa2f`](https://github.com/nodejs/node/commit/37cdeafa2f)] - **smalloc**: remove module (Brendan Ashworth) [#3099](https://github.com/nodejs/node/pull/3099)
* \[[`5ec5d0aa8b`](https://github.com/nodejs/node/commit/5ec5d0aa8b)] - **src**: internalize binding function property names (Ben Noordhuis) [#3060](https://github.com/nodejs/node/pull/3060)
* \[[`c8175fc2af`](https://github.com/nodejs/node/commit/c8175fc2af)] - **src**: internalize per-isolate string properties (Ben Noordhuis) [#3060](https://github.com/nodejs/node/pull/3060)
* \[[`9a593abc47`](https://github.com/nodejs/node/commit/9a593abc47)] - **src**: include signal.h in util.h (Cheng Zhao) [#3058](https://github.com/nodejs/node/pull/3058)
* \[[`fde0c6f321`](https://github.com/nodejs/node/commit/fde0c6f321)] - **src**: fix function and variable names in comments (Sakthipriyan Vairamani) [#3039](https://github.com/nodejs/node/pull/3039)
* \[[`1cc7b41ba4`](https://github.com/nodejs/node/commit/1cc7b41ba4)] - **stream\_wrap**: support empty `TryWrite`s (Fedor Indutny) [#3128](https://github.com/nodejs/node/pull/3128)
* \[[`9faf4c6fcf`](https://github.com/nodejs/node/commit/9faf4c6fcf)] - **test**: load common.js to test for global leaks (Rich Trott) [#3095](https://github.com/nodejs/node/pull/3095)
* \[[`0858c86374`](https://github.com/nodejs/node/commit/0858c86374)] - **test**: fix invalid variable name (Sakthipriyan Vairamani) [#3150](https://github.com/nodejs/node/pull/3150)
* \[[`1167171004`](https://github.com/nodejs/node/commit/1167171004)] - **test**: change calls to deprecated util.print() (Rich Trott) [#3083](https://github.com/nodejs/node/pull/3083)
* \[[`5ada45bf28`](https://github.com/nodejs/node/commit/5ada45bf28)] - **test**: replace deprecated util.debug() calls (Rich Trott) [#3082](https://github.com/nodejs/node/pull/3082)
* \[[`d8ab4e185d`](https://github.com/nodejs/node/commit/d8ab4e185d)] - **util**: optimize promise introspection (Ben Noordhuis) [#3130](https://github.com/nodejs/node/pull/3130)

<a id="4.1.1"></a>

## 2015-09-22, Version 4.1.1 (Stable), @rvagg

### Notable changes

* **buffer**: Fixed a bug introduced in v4.1.0 where allocating a new zero-length buffer can result in the _next_ allocation of a TypedArray in JavaScript not being zero-filled. In certain circumstances this could result in data leakage via reuse of memory space in TypedArrays, breaking the normally safe assumption that TypedArrays should be always zero-filled. (Trevor Norris) [#2931](https://github.com/nodejs/node/pull/2931).
* **http**: Guard against response-splitting of HTTP trailing headers added via [`response.addTrailers()`](https://nodejs.org/api/http.html#http_response_addtrailers_headers) by removing new-line (`[\r\n]`) characters from values. Note that standard header values are already stripped of new-line characters. The expected security impact is low because trailing headers are rarely used. (Ben Noordhuis) [#2945](https://github.com/nodejs/node/pull/2945).
* **npm**: Upgrade to npm 2.14.4 from 2.14.3, see [release notes](https://github.com/npm/npm/releases/tag/v2.14.4) for full details (Kat Marchán) [#2958](https://github.com/nodejs/node/pull/2958)
  * Upgrades `graceful-fs` on multiple dependencies to no longer rely on monkey-patching `fs`
  * Fix `npm link` for pre-release / RC builds of Node
* **v8**: Update post-mortem metadata to allow post-mortem debugging tools to find and inspect:
  * JavaScript objects that use dictionary properties (Julien Gilli) [#2959](https://github.com/nodejs/node/pull/2959)
  * ScopeInfo and thus closures (Julien Gilli) [#2974](https://github.com/nodejs/node/pull/2974)

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`d63e02e08d`](https://github.com/nodejs/node/commit/d63e02e08d)] - **buffer**: don't set zero fill for zero-length buffer (Trevor Norris) [#2931](https://github.com/nodejs/node/pull/2931)
* \[[`5905b14bff`](https://github.com/nodejs/node/commit/5905b14bff)] - **build**: fix icutrim when building small-icu on BE (Stewart Addison) [#2602](https://github.com/nodejs/node/pull/2602)
* \[[`f010cb5d96`](https://github.com/nodejs/node/commit/f010cb5d96)] - **configure**: detect mipsel host (Jérémy Lal) [#2971](https://github.com/nodejs/node/pull/2971)
* \[[`b93ad5abbd`](https://github.com/nodejs/node/commit/b93ad5abbd)] - **deps**: backport 357e6b9 from V8's upstream (Julien Gilli) [#2974](https://github.com/nodejs/node/pull/2974)
* \[[`8da3da4d41`](https://github.com/nodejs/node/commit/8da3da4d41)] - **deps**: backport ff7d70b from V8's upstream (Julien Gilli) [#2959](https://github.com/nodejs/node/pull/2959)
* \[[`2600fb8ae6`](https://github.com/nodejs/node/commit/2600fb8ae6)] - **deps**: upgraded to node-gyp\@3.0.3 in npm (Kat Marchán) [#2958](https://github.com/nodejs/node/pull/2958)
* \[[`793aad2d7a`](https://github.com/nodejs/node/commit/793aad2d7a)] - **deps**: upgrade to npm 2.14.4 (Kat Marchán) [#2958](https://github.com/nodejs/node/pull/2958)
* \[[`43e2b7f836`](https://github.com/nodejs/node/commit/43e2b7f836)] - **doc**: remove usage of events.EventEmitter (Sakthipriyan Vairamani) [#2921](https://github.com/nodejs/node/pull/2921)
* \[[`9c59d2f16a`](https://github.com/nodejs/node/commit/9c59d2f16a)] - **doc**: remove extra using v8::HandleScope statement (Christopher J. Brody) [#2983](https://github.com/nodejs/node/pull/2983)
* \[[`f7edbab367`](https://github.com/nodejs/node/commit/f7edbab367)] - **doc**: clarify description of assert.ifError() (Rich Trott) [#2941](https://github.com/nodejs/node/pull/2941)
* \[[`b2ddf0f9a2`](https://github.com/nodejs/node/commit/b2ddf0f9a2)] - **doc**: refine process.kill() and exit explanations (Rich Trott) [#2918](https://github.com/nodejs/node/pull/2918)
* \[[`f68fed2e6f`](https://github.com/nodejs/node/commit/f68fed2e6f)] - **http**: remove redundant code in \_deferToConnect (Malcolm Ahoy) [#2769](https://github.com/nodejs/node/pull/2769)
* \[[`f542e74c93`](https://github.com/nodejs/node/commit/f542e74c93)] - **http**: guard against response splitting in trailers (Ben Noordhuis) [#2945](https://github.com/nodejs/node/pull/2945)
* \[[`bc9f629387`](https://github.com/nodejs/node/commit/bc9f629387)] - **http\_parser**: do not dealloc during kOnExecute (Fedor Indutny) [#2956](https://github.com/nodejs/node/pull/2956)
* \[[`1860e0cebd`](https://github.com/nodejs/node/commit/1860e0cebd)] - **lib,src**: remove usage of events.EventEmitter (Sakthipriyan Vairamani) [#2921](https://github.com/nodejs/node/pull/2921)
* \[[`d4cd5ac407`](https://github.com/nodejs/node/commit/d4cd5ac407)] - **readline**: fix tab completion bug (Matt Harrison) [#2816](https://github.com/nodejs/node/pull/2816)
* \[[`9760e04839`](https://github.com/nodejs/node/commit/9760e04839)] - **repl**: don't use tty control codes when $TERM is set to "dumb" (Salman Aljammaz) [#2712](https://github.com/nodejs/node/pull/2712)
* \[[`cb971cc97d`](https://github.com/nodejs/node/commit/cb971cc97d)] - **repl**: backslash bug fix (Sakthipriyan Vairamani) [#2968](https://github.com/nodejs/node/pull/2968)
* \[[`2034f68668`](https://github.com/nodejs/node/commit/2034f68668)] - **src**: honor --abort\_on\_uncaught\_exception flag (Evan Lucas) [#2776](https://github.com/nodejs/node/pull/2776)
* \[[`0b1ca4a9ef`](https://github.com/nodejs/node/commit/0b1ca4a9ef)] - **src**: Add ABORT macro (Evan Lucas) [#2776](https://github.com/nodejs/node/pull/2776)
* \[[`4519dd00f9`](https://github.com/nodejs/node/commit/4519dd00f9)] - **test**: test sync version of mkdir & rmdir (Sakthipriyan Vairamani) [#2588](https://github.com/nodejs/node/pull/2588)
* \[[`816f609c8b`](https://github.com/nodejs/node/commit/816f609c8b)] - **test**: use tmpDir instead of fixtures in readdir (Sakthipriyan Vairamani) [#2587](https://github.com/nodejs/node/pull/2587)
* \[[`2084f52585`](https://github.com/nodejs/node/commit/2084f52585)] - **test**: test more http response splitting scenarios (Ben Noordhuis) [#2945](https://github.com/nodejs/node/pull/2945)
* \[[`fa08d1d8a1`](https://github.com/nodejs/node/commit/fa08d1d8a1)] - **test**: add test-spawn-cmd-named-pipe (Alexis Campailla) [#2770](https://github.com/nodejs/node/pull/2770)
* \[[`71b5d80682`](https://github.com/nodejs/node/commit/71b5d80682)] - **test**: make cluster tests more time tolerant (Michael Dawson) [#2891](https://github.com/nodejs/node/pull/2891)
* \[[`3e09dcfc32`](https://github.com/nodejs/node/commit/3e09dcfc32)] - **test**: update cwd-enoent tests for AIX (Imran Iqbal) [#2909](https://github.com/nodejs/node/pull/2909)
* \[[`6ea8ec1c59`](https://github.com/nodejs/node/commit/6ea8ec1c59)] - **tools**: single, cross-platform tick processor (Matt Loring) [#2868](https://github.com/nodejs/node/pull/2868)

<a id="4.1.0"></a>

## 2015-09-17, Version 4.1.0 (Stable), @Fishrock123

### Notable changes

* **buffer**:
  * Buffers are now created in JavaScript, rather than C++. This increases the speed of buffer creation (Trevor Norris) [#2866](https://github.com/nodejs/node/pull/2866).
  * `Buffer#slice()` now uses `Uint8Array#subarray()` internally, increasing `slice()` performance (Karl Skomski) [#2777](https://github.com/nodejs/node/pull/2777).
* **fs**:
  * `fs.utimes()` now properly converts numeric strings, `NaN`, and `Infinity` (Yazhong Liu) [#2387](https://github.com/nodejs/node/pull/2387).
  * `fs.WriteStream` now implements `_writev`, allowing for super-fast bulk writes (Ron Korving) [#2167](https://github.com/nodejs/node/pull/2167).
* **http**: Fixed an issue with certain `write()` sizes causing errors when using `http.request()` (Fedor Indutny) [#2824](https://github.com/nodejs/node/pull/2824).
* **npm**: Upgrade to version 2.14.3, see <https://github.com/npm/npm/releases/tag/v2.14.3> for more details (Kat Marchán) [#2822](https://github.com/nodejs/node/pull/2822).
* **src**: V8 cpu profiling no longer erroneously shows idle time (Oleksandr Chekhovskyi) [#2324](https://github.com/nodejs/node/pull/2324).
* **timers**: `#ref()` and `#unref()` now return the timer they belong to (Sam Roberts) [#2905](https://github.com/nodejs/node/pull/2905).
* **v8**: Lateral upgrade to 4.5.103.33 from 4.5.103.30, contains minor fixes (Ali Ijaz Sheikh) [#2870](https://github.com/nodejs/node/pull/2870).
  * This fixes a previously known bug where some computed object shorthand properties did not work correctly ([#2507](https://github.com/nodejs/node/issues/2507)).

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`b1abe812cd`](https://github.com/nodejs/node/commit/b1abe812cd)] - Working on 4.0.1 (Rod Vagg)
* \[[`f9f8378853`](https://github.com/nodejs/node/commit/f9f8378853)] - 2015-09-08, Version 4.0.0 (Stable) Release (Rod Vagg)
* \[[`9683e5df51`](https://github.com/nodejs/node/commit/9683e5df51)] - **bindings**: close after reading module struct (Fedor Indutny) [#2792](https://github.com/nodejs/node/pull/2792)
* \[[`4b4cfa2d44`](https://github.com/nodejs/node/commit/4b4cfa2d44)] - **buffer**: always allocate typed arrays outside heap (Trevor Norris) [#2893](https://github.com/nodejs/node/pull/2893)
* \[[`7df018a29b`](https://github.com/nodejs/node/commit/7df018a29b)] - **buffer**: construct Uint8Array in JS (Trevor Norris) [#2866](https://github.com/nodejs/node/pull/2866)
* \[[`43397b204e`](https://github.com/nodejs/node/commit/43397b204e)] - **(SEMVER-MINOR)** **build**: Updates to enable AIX support (Michael Dawson) [#2364](https://github.com/nodejs/node/pull/2364)
* \[[`e35b1fd610`](https://github.com/nodejs/node/commit/e35b1fd610)] - **build**: clean up the generated tap file (Sakthipriyan Vairamani) [#2837](https://github.com/nodejs/node/pull/2837)
* \[[`96670ebe37`](https://github.com/nodejs/node/commit/96670ebe37)] - **deps**: backport 6d32be2 from v8's upstream (Michaël Zasso) [#2916](https://github.com/nodejs/node/pull/2916)
* \[[`94972d5b13`](https://github.com/nodejs/node/commit/94972d5b13)] - **deps**: backport 0d01728 from v8's upstream (Fedor Indutny) [#2912](https://github.com/nodejs/node/pull/2912)
* \[[`7ebd881c29`](https://github.com/nodejs/node/commit/7ebd881c29)] - **deps**: upgrade V8 to 4.5.103.33 (Ali Ijaz Sheikh) [#2870](https://github.com/nodejs/node/pull/2870)
* \[[`ed47ab6e44`](https://github.com/nodejs/node/commit/ed47ab6e44)] - **deps**: upgraded to node-gyp\@3.0.3 in npm (Kat Marchán) [#2822](https://github.com/nodejs/node/pull/2822)
* \[[`f4641ae875`](https://github.com/nodejs/node/commit/f4641ae875)] - **deps**: upgrade to npm 2.14.3 (Kat Marchán) [#2822](https://github.com/nodejs/node/pull/2822)
* \[[`8119693a3d`](https://github.com/nodejs/node/commit/8119693a3d)] - **deps**: update libuv to version 1.7.4 (Saúl Ibarra Corretgé) [#2817](https://github.com/nodejs/node/pull/2817)
* \[[`6098504685`](https://github.com/nodejs/node/commit/6098504685)] - **deps**: cherry-pick 6da51b4 from v8's upstream (Fedor Indutny) [#2801](https://github.com/nodejs/node/pull/2801)
* \[[`bf42cc8dba`](https://github.com/nodejs/node/commit/bf42cc8dba)] - **doc**: process exit event is not guaranteed to fire (Rich Trott) [#2861](https://github.com/nodejs/node/pull/2861)
* \[[`bb0f869f67`](https://github.com/nodejs/node/commit/bb0f869f67)] - **doc**: remove incorrect reference to TCP in net docs (Sam Roberts) [#2903](https://github.com/nodejs/node/pull/2903)
* \[[`302d59dce8`](https://github.com/nodejs/node/commit/302d59dce8)] - **doc**: correct buffer.slice arg syntax (Sam Roberts) [#2903](https://github.com/nodejs/node/pull/2903)
* \[[`74db9637b7`](https://github.com/nodejs/node/commit/74db9637b7)] - **doc**: describe spawn option.detached (Sam Roberts) [#2903](https://github.com/nodejs/node/pull/2903)
* \[[`a7bd897273`](https://github.com/nodejs/node/commit/a7bd897273)] - **doc**: rename from iojs(1) to node(1) in benchmarks (Dmitry Vasilyev) [#2884](https://github.com/nodejs/node/pull/2884)
* \[[`cd643d7c37`](https://github.com/nodejs/node/commit/cd643d7c37)] - **doc**: add missing backtick in buffer.markdown (Sven Slootweg) [#2881](https://github.com/nodejs/node/pull/2881)
* \[[`e8a206e802`](https://github.com/nodejs/node/commit/e8a206e802)] - **doc**: fix broken link in repl.markdown (Danny Nemer) [#2827](https://github.com/nodejs/node/pull/2827)
* \[[`7ee36d61f7`](https://github.com/nodejs/node/commit/7ee36d61f7)] - **doc**: fix typos in README (Ionică Bizău) [#2852](https://github.com/nodejs/node/pull/2852)
* \[[`4d1ae26196`](https://github.com/nodejs/node/commit/4d1ae26196)] - **doc**: add tunniclm as a collaborator (Mike Tunnicliffe) [#2826](https://github.com/nodejs/node/pull/2826)
* \[[`2d77d03643`](https://github.com/nodejs/node/commit/2d77d03643)] - **doc**: fix two doc errors in stream and process (Jeremiah Senkpiel) [#2549](https://github.com/nodejs/node/pull/2549)
* \[[`55ac24f721`](https://github.com/nodejs/node/commit/55ac24f721)] - **doc**: fixed io.js references in process.markdown (Tristian Flanagan) [#2846](https://github.com/nodejs/node/pull/2846)
* \[[`cd1297fb57`](https://github.com/nodejs/node/commit/cd1297fb57)] - **doc**: use "Calls" over "Executes" for consistency (Minwoo Jung) [#2800](https://github.com/nodejs/node/pull/2800)
* \[[`d664b95581`](https://github.com/nodejs/node/commit/d664b95581)] - **doc**: use US English for consistency (Anne-Gaelle Colom) [#2784](https://github.com/nodejs/node/pull/2784)
* \[[`82ba1839fb`](https://github.com/nodejs/node/commit/82ba1839fb)] - **doc**: use 3rd person singular for consistency (Anne-Gaelle Colom) [#2765](https://github.com/nodejs/node/pull/2765)
* \[[`432cce6e95`](https://github.com/nodejs/node/commit/432cce6e95)] - **doc**: describe process API for IPC (Sam Roberts) [#1978](https://github.com/nodejs/node/pull/1978)
* \[[`1d75012b9d`](https://github.com/nodejs/node/commit/1d75012b9d)] - **doc**: fix comma splice in Assertion Testing doc (Rich Trott) [#2728](https://github.com/nodejs/node/pull/2728)
* \[[`6108ea9bb4`](https://github.com/nodejs/node/commit/6108ea9bb4)] - **fs**: consider NaN/Infinity in toUnixTimestamp (Yazhong Liu) [#2387](https://github.com/nodejs/node/pull/2387)
* \[[`2b6aa9415f`](https://github.com/nodejs/node/commit/2b6aa9415f)] - **(SEMVER-MINOR)** **fs**: implemented WriteStream#writev (Ron Korving) [#2167](https://github.com/nodejs/node/pull/2167)
* \[[`431bf74c55`](https://github.com/nodejs/node/commit/431bf74c55)] - **http**: default Agent.getName to 'localhost' (Malcolm Ahoy) [#2825](https://github.com/nodejs/node/pull/2825)
* \[[`ea15d71c16`](https://github.com/nodejs/node/commit/ea15d71c16)] - **http\_server**: fix resume after socket close (Fedor Indutny) [#2824](https://github.com/nodejs/node/pull/2824)
* \[[`8e5843405b`](https://github.com/nodejs/node/commit/8e5843405b)] - **src**: null env\_ field from constructor (Trevor Norris) [#2913](https://github.com/nodejs/node/pull/2913)
* \[[`0a5f80a11f`](https://github.com/nodejs/node/commit/0a5f80a11f)] - **src**: use subarray() in Buffer#slice() for speedup (Karl Skomski) [#2777](https://github.com/nodejs/node/pull/2777)
* \[[`57707e2490`](https://github.com/nodejs/node/commit/57707e2490)] - **src**: use ZCtxt as a source for v8::Isolates (Roman Klauke) [#2547](https://github.com/nodejs/node/pull/2547)
* \[[`b0df2273ab`](https://github.com/nodejs/node/commit/b0df2273ab)] - **src**: fix v8::CpuProfiler idle sampling (Oleksandr Chekhovskyi) [#2324](https://github.com/nodejs/node/pull/2324)
* \[[`eaa8e60b91`](https://github.com/nodejs/node/commit/eaa8e60b91)] - **streams**: refactor LazyTransform to internal/ (Brendan Ashworth) [#2566](https://github.com/nodejs/node/pull/2566)
* \[[`648c003e14`](https://github.com/nodejs/node/commit/648c003e14)] - **test**: use tmp directory in chdir test (Sakthipriyan Vairamani) [#2589](https://github.com/nodejs/node/pull/2589)
* \[[`079a2173d4`](https://github.com/nodejs/node/commit/079a2173d4)] - **test**: fix Buffer OOM error message (Trevor Norris) [#2915](https://github.com/nodejs/node/pull/2915)
* \[[`52019a1b21`](https://github.com/nodejs/node/commit/52019a1b21)] - **test**: fix default value for additional param (Sakthipriyan Vairamani) [#2553](https://github.com/nodejs/node/pull/2553)
* \[[`5df5d0423a`](https://github.com/nodejs/node/commit/5df5d0423a)] - **test**: remove disabled test (Rich Trott) [#2841](https://github.com/nodejs/node/pull/2841)
* \[[`9e5f0995bd`](https://github.com/nodejs/node/commit/9e5f0995bd)] - **test**: split up internet dns tests (Rich Trott) [#2802](https://github.com/nodejs/node/pull/2802)
* \[[`41f2dde51a`](https://github.com/nodejs/node/commit/41f2dde51a)] - **test**: increase dgram timeout for armv6 (Rich Trott) [#2808](https://github.com/nodejs/node/pull/2808)
* \[[`6e2fe1c21a`](https://github.com/nodejs/node/commit/6e2fe1c21a)] - **test**: remove valid hostname check in test-dns.js (Rich Trott) [#2785](https://github.com/nodejs/node/pull/2785)
* \[[`779e14f1a7`](https://github.com/nodejs/node/commit/779e14f1a7)] - **test**: expect error for test\_lookup\_ipv6\_hint on FreeBSD (Rich Trott) [#2724](https://github.com/nodejs/node/pull/2724)
* \[[`f931b9dd95`](https://github.com/nodejs/node/commit/f931b9dd95)] - **(SEMVER-MINOR)** **timer**: ref/unref return self (Sam Roberts) [#2905](https://github.com/nodejs/node/pull/2905)
* \[[`59d03738cc`](https://github.com/nodejs/node/commit/59d03738cc)] - **tools**: enable arrow functions in .eslintrc (Sakthipriyan Vairamani) [#2840](https://github.com/nodejs/node/pull/2840)
* \[[`69e7b875a2`](https://github.com/nodejs/node/commit/69e7b875a2)] - **tools**: open `test.tap` file in write-binary mode (Sakthipriyan Vairamani) [#2837](https://github.com/nodejs/node/pull/2837)
* \[[`ff6d30d784`](https://github.com/nodejs/node/commit/ff6d30d784)] - **tools**: add missing tick processor polyfill (Matt Loring) [#2694](https://github.com/nodejs/node/pull/2694)
* \[[`519caba021`](https://github.com/nodejs/node/commit/519caba021)] - **tools**: fix flakiness in test-tick-processor (Matt Loring) [#2694](https://github.com/nodejs/node/pull/2694)
* \[[`ac004b8555`](https://github.com/nodejs/node/commit/ac004b8555)] - **tools**: remove hyphen in TAP result (Sakthipriyan Vairamani) [#2718](https://github.com/nodejs/node/pull/2718)
* \[[`ba47511976`](https://github.com/nodejs/node/commit/ba47511976)] - **tsc**: adjust TSC membership for IBM+StrongLoop (James M Snell) [#2858](https://github.com/nodejs/node/pull/2858)
* \[[`e035266805`](https://github.com/nodejs/node/commit/e035266805)] - **win,msi**: fix documentation shortcut url (Brian White) [#2781](https://github.com/nodejs/node/pull/2781)

<a id="4.0.0"></a>

## 2015-09-08, Version 4.0.0 (Stable), @rvagg

### Notable changes

This list of changes is relative to the last io.js v3.x branch release, v3.3.0. Please see the list of notable changes in the v3.x, v2.x and v1.x releases for a more complete list of changes from 0.12.x. Note, that some changes in the v3.x series as well as major breaking changes in this release constitute changes required for full convergence of the Node.js and io.js projects.

* **child\_process**: `ChildProcess.prototype.send()` and `process.send()` operate asynchronously across all platforms so an optional callback parameter has been introduced that will be invoked once the message has been sent, i.e. `.send(message[, sendHandle][, callback])` (Ben Noordhuis) [#2620](https://github.com/nodejs/node/pull/2620).
* **node**: Rename "io.js" code to "Node.js" (cjihrig) [#2367](https://github.com/nodejs/node/pull/2367).
* **node-gyp**: This release bundles an updated version of node-gyp that works with all versions of Node.js and io.js including nightly and release candidate builds. From io.js v3 and Node.js v4 onward, it will only download a headers tarball when building addons rather than the entire source. (Rod Vagg) [#2700](https://github.com/nodejs/node/pull/2700)
* **npm**: Upgrade to version 2.14.2 from 2.13.3, includes a security update, see <https://github.com/npm/npm/releases/tag/v2.14.2> for more details, (Kat Marchán) [#2696](https://github.com/nodejs/node/pull/2696).
* **timers**: Improved timer performance from porting the 0.12 implementation, plus minor fixes (Jeremiah Senkpiel) [#2540](https://github.com/nodejs/node/pull/2540), (Julien Gilli) [nodejs/node-v0.x-archive#8751](https://github.com/nodejs/node-v0.x-archive/pull/8751) [nodejs/node-v0.x-archive#8905](https://github.com/nodejs/node-v0.x-archive/pull/8905)
* **util**: The `util.is*()` functions have been deprecated, beginning with deprecation warnings in the documentation for this release, users are encouraged to seek more robust alternatives in the npm registry, (Sakthipriyan Vairamani) [#2447](https://github.com/nodejs/node/pull/2447).
* **v8**: Upgrade to version 4.5.103.30 from 4.4.63.30 (Ali Ijaz Sheikh) [#2632](https://github.com/nodejs/node/pull/2632).
  * Implement new `TypedArray` prototype methods: `copyWithin()`, `every()`, `fill()`, `filter()`, `find()`, `findIndex()`, `forEach()`, `indexOf()`, `join()`, `lastIndexOf()`, `map()`, `reduce()`, `reduceRight()`, `reverse()`, `slice()`, `some()`, `sort()`. See <https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/TypedArray> for further information.
  * Implement new `TypedArray.from()` and `TypedArray.of()` functions. See <https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/TypedArray> for further information.
  * Implement arrow functions, see <https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Functions/Arrow_functions> for further information.
  * Full ChangeLog available at <https://github.com/v8/v8-git-mirror/blob/4.5.103/ChangeLog>

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Some uses of computed object shorthand properties are not handled correctly by the current version of V8. e.g. `[{ [prop]: val }]` evaluates to `[{}]`. [#2507](https://github.com/nodejs/node/issues/2507)
* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`4f50d3fb90`](https://github.com/nodejs/node/commit/4f50d3fb90)] - **(SEMVER-MAJOR)** This commit sets the value of process.release.name to "node". (cjihrig) [#2367](https://github.com/nodejs/node/pull/2367)
* \[[`d3178d8b1b`](https://github.com/nodejs/node/commit/d3178d8b1b)] - **buffer**: SlowBuffer only accept valid numeric values (Michaël Zasso) [#2635](https://github.com/nodejs/node/pull/2635)
* \[[`0cb0f4a6e4`](https://github.com/nodejs/node/commit/0cb0f4a6e4)] - **build**: fix v8\_enable\_handle\_zapping override (Karl Skomski) [#2731](https://github.com/nodejs/node/pull/2731)
* \[[`a7596d7efc`](https://github.com/nodejs/node/commit/a7596d7efc)] - **build**: remote commands on staging in single session (Rod Vagg) [#2717](https://github.com/nodejs/node/pull/2717)
* \[[`be427e9efa`](https://github.com/nodejs/node/commit/be427e9efa)] - **build**: make .msi install to "nodejs", not "node" (Rod Vagg) [#2701](https://github.com/nodejs/node/pull/2701)
* \[[`5652ce0dbc`](https://github.com/nodejs/node/commit/5652ce0dbc)] - **build**: fix .pkg creation tooling (Rod Vagg) [#2687](https://github.com/nodejs/node/pull/2687)
* \[[`101db80111`](https://github.com/nodejs/node/commit/101db80111)] - **build**: add --enable-asan with builtin leakcheck (Karl Skomski) [#2376](https://github.com/nodejs/node/pull/2376)
* \[[`2c3939c9c0`](https://github.com/nodejs/node/commit/2c3939c9c0)] - **child\_process**: use stdio.fd even if it is 0 (Evan Lucas) [#2727](https://github.com/nodejs/node/pull/2727)
* \[[`609db5a1dd`](https://github.com/nodejs/node/commit/609db5a1dd)] - **child\_process**: check execFile and fork args (James M Snell) [#2667](https://github.com/nodejs/node/pull/2667)
* \[[`d010568c23`](https://github.com/nodejs/node/commit/d010568c23)] - **(SEMVER-MAJOR)** **child\_process**: add callback parameter to .send() (Ben Noordhuis) [#2620](https://github.com/nodejs/node/pull/2620)
* \[[`c60857a81a`](https://github.com/nodejs/node/commit/c60857a81a)] - **cluster**: allow shared reused dgram sockets (Fedor Indutny) [#2548](https://github.com/nodejs/node/pull/2548)
* \[[`b2ecbb6191`](https://github.com/nodejs/node/commit/b2ecbb6191)] - **contextify**: ignore getters during initialization (Fedor Indutny) [#2091](https://github.com/nodejs/node/pull/2091)
* \[[`3711934095`](https://github.com/nodejs/node/commit/3711934095)] - **cpplint**: make it possible to run outside git repo (Ben Noordhuis) [#2710](https://github.com/nodejs/node/pull/2710)
* \[[`03f900ab25`](https://github.com/nodejs/node/commit/03f900ab25)] - **crypto**: replace rwlocks with simple mutexes (Ben Noordhuis) [#2723](https://github.com/nodejs/node/pull/2723)
* \[[`847459c29b`](https://github.com/nodejs/node/commit/847459c29b)] - **(SEMVER-MAJOR)** **crypto**: show exponent in decimal and hex (Chad Johnston) [#2320](https://github.com/nodejs/node/pull/2320)
* \[[`e1c976184d`](https://github.com/nodejs/node/commit/e1c976184d)] - **deps**: improve ArrayBuffer performance in v8 (Fedor Indutny) [#2732](https://github.com/nodejs/node/pull/2732)
* \[[`cc0ab17a23`](https://github.com/nodejs/node/commit/cc0ab17a23)] - **deps**: float node-gyp v3.0.0 (Rod Vagg) [#2700](https://github.com/nodejs/node/pull/2700)
* \[[`b2c3c6d727`](https://github.com/nodejs/node/commit/b2c3c6d727)] - **deps**: create .npmrc during npm tests (Kat Marchán) [#2696](https://github.com/nodejs/node/pull/2696)
* \[[`babdbfdbd5`](https://github.com/nodejs/node/commit/babdbfdbd5)] - **deps**: upgrade to npm 2.14.2 (Kat Marchán) [#2696](https://github.com/nodejs/node/pull/2696)
* \[[`155783d876`](https://github.com/nodejs/node/commit/155783d876)] - **deps**: backport 75e43a6 from v8 upstream (again) (saper) [#2692](https://github.com/nodejs/node/pull/2692)
* \[[`5424d6fcf0`](https://github.com/nodejs/node/commit/5424d6fcf0)] - **deps**: upgrade V8 to 4.5.103.30 (Ali Ijaz Sheikh) [#2632](https://github.com/nodejs/node/pull/2632)
* \[[`c43172578e`](https://github.com/nodejs/node/commit/c43172578e)] - **(SEMVER-MAJOR)** **deps**: upgrade V8 to 4.5.103.24 (Ali Ijaz Sheikh) [#2509](https://github.com/nodejs/node/pull/2509)
* \[[`714e96e8b9`](https://github.com/nodejs/node/commit/714e96e8b9)] - **deps**: backport 75e43a6 from v8 upstream (saper) [#2636](https://github.com/nodejs/node/pull/2636)
* \[[`8637755cbf`](https://github.com/nodejs/node/commit/8637755cbf)] - **doc**: add TSC meeting minutes 2015-09-02 (Rod Vagg) [#2674](https://github.com/nodejs/node/pull/2674)
* \[[`d3d5b93214`](https://github.com/nodejs/node/commit/d3d5b93214)] - **doc**: update environment vars in manpage and --help (Roman Reiss) [#2690](https://github.com/nodejs/node/pull/2690)
* \[[`29f586ac0a`](https://github.com/nodejs/node/commit/29f586ac0a)] - **doc**: update url doc to account for escaping (Jeremiah Senkpiel) [#2605](https://github.com/nodejs/node/pull/2605)
* \[[`ba50cfebef`](https://github.com/nodejs/node/commit/ba50cfebef)] - **doc**: reorder collaborators by their usernames (Johan Bergström) [#2322](https://github.com/nodejs/node/pull/2322)
* \[[`8a9a3bf798`](https://github.com/nodejs/node/commit/8a9a3bf798)] - **doc**: update changelog for io.js v3.3.0 (Rod Vagg) [#2653](https://github.com/nodejs/node/pull/2653)
* \[[`6cd0e2664b`](https://github.com/nodejs/node/commit/6cd0e2664b)] - **doc**: update io.js reference (Ben Noordhuis) [#2580](https://github.com/nodejs/node/pull/2580)
* \[[`f9539c19e8`](https://github.com/nodejs/node/commit/f9539c19e8)] - **doc**: update changelog for io.js v3.2.0 (Rod Vagg) [#2512](https://github.com/nodejs/node/pull/2512)
* \[[`cded6e7993`](https://github.com/nodejs/node/commit/cded6e7993)] - **doc**: fix CHANGELOG.md on master (Roman Reiss) [#2513](https://github.com/nodejs/node/pull/2513)
* \[[`93e2830686`](https://github.com/nodejs/node/commit/93e2830686)] - **(SEMVER-MINOR)** **doc**: document deprecation of util.is\* functions (Sakthipriyan Vairamani) [#2447](https://github.com/nodejs/node/pull/2447)
* \[[`7038388558`](https://github.com/nodejs/node/commit/7038388558)] - **doc,test**: enable recursive file watching in Windows (Sakthipriyan Vairamani) [#2649](https://github.com/nodejs/node/pull/2649)
* \[[`f3696f64a1`](https://github.com/nodejs/node/commit/f3696f64a1)] - **events,lib**: don't require EE#listenerCount() (Jeremiah Senkpiel) [#2661](https://github.com/nodejs/node/pull/2661)
* \[[`45a2046f5d`](https://github.com/nodejs/node/commit/45a2046f5d)] - **(SEMVER-MAJOR)** **installer**: fix installers for node.js rename (Frederic Hemberger) [#2367](https://github.com/nodejs/node/pull/2367)
* \[[`7a999a1376`](https://github.com/nodejs/node/commit/7a999a1376)] - **(SEMVER-MAJOR)** **lib**: add net.Socket#localFamily property (Ben Noordhuis) [#956](https://github.com/nodejs/node/pull/956)
* \[[`de88255b0f`](https://github.com/nodejs/node/commit/de88255b0f)] - _**Revert**_ "**lib,src**: add unix socket getsockname/getpeername" (Ben Noordhuis) [#2584](https://github.com/nodejs/node/pull/2584)
* \[[`f337595441`](https://github.com/nodejs/node/commit/f337595441)] - **(SEMVER-MAJOR)** **lib,src**: add unix socket getsockname/getpeername (Ben Noordhuis) [#956](https://github.com/nodejs/node/pull/956)
* \[[`3b602527d1`](https://github.com/nodejs/node/commit/3b602527d1)] - **(SEMVER-MAJOR)** **node**: additional cleanup for node rename (cjihrig) [#2367](https://github.com/nodejs/node/pull/2367)
* \[[`a69ab27ab4`](https://github.com/nodejs/node/commit/a69ab27ab4)] - **(SEMVER-MAJOR)** **node**: rename from io.js to node (cjihrig) [#2367](https://github.com/nodejs/node/pull/2367)
* \[[`9358eee9dd`](https://github.com/nodejs/node/commit/9358eee9dd)] - **node-gyp**: float 3.0.1, minor fix for download url (Rod Vagg) [#2737](https://github.com/nodejs/node/pull/2737)
* \[[`d2d981252b`](https://github.com/nodejs/node/commit/d2d981252b)] - **src**: s/ia32/x86 for process.release.libUrl for win (Rod Vagg) [#2699](https://github.com/nodejs/node/pull/2699)
* \[[`eba3d3dccd`](https://github.com/nodejs/node/commit/eba3d3dccd)] - **src**: use standard conform snprintf on windows (Karl Skomski) [#2404](https://github.com/nodejs/node/pull/2404)
* \[[`cddbec231f`](https://github.com/nodejs/node/commit/cddbec231f)] - **src**: fix buffer overflow for long exception lines (Karl Skomski) [#2404](https://github.com/nodejs/node/pull/2404)
* \[[`dd3f3417c7`](https://github.com/nodejs/node/commit/dd3f3417c7)] - **src**: re-enable fast math on arm (Michaël Zasso) [#2592](https://github.com/nodejs/node/pull/2592)
* \[[`e137c1177c`](https://github.com/nodejs/node/commit/e137c1177c)] - **(SEMVER-MAJOR)** **src**: enable vector ics on arm again (Ali Ijaz Sheikh) [#2509](https://github.com/nodejs/node/pull/2509)
* \[[`7ce749d722`](https://github.com/nodejs/node/commit/7ce749d722)] - **src**: replace usage of v8::Handle with v8::Local (Michaël Zasso) [#2202](https://github.com/nodejs/node/pull/2202)
* \[[`b1a2d9509f`](https://github.com/nodejs/node/commit/b1a2d9509f)] - **src**: enable v8 deprecation warnings and fix them (Ben Noordhuis) [#2091](https://github.com/nodejs/node/pull/2091)
* \[[`808de0da03`](https://github.com/nodejs/node/commit/808de0da03)] - **(SEMVER-MAJOR)** **src**: apply debug force load fixups from 41e63fb (Ali Ijaz Sheikh) [#2509](https://github.com/nodejs/node/pull/2509)
* \[[`5201cb0ff1`](https://github.com/nodejs/node/commit/5201cb0ff1)] - **src**: fix memory leak in ExternString (Karl Skomski) [#2402](https://github.com/nodejs/node/pull/2402)
* \[[`2308a27c0a`](https://github.com/nodejs/node/commit/2308a27c0a)] - **src**: only set v8 flags if argc > 1 (Evan Lucas) [#2646](https://github.com/nodejs/node/pull/2646)
* \[[`384effed20`](https://github.com/nodejs/node/commit/384effed20)] - **test**: fix use of `common` before required (Rod Vagg) [#2685](https://github.com/nodejs/node/pull/2685)
* \[[`f146f686b7`](https://github.com/nodejs/node/commit/f146f686b7)] - **(SEMVER-MAJOR)** **test**: fix test-repl-tab-complete.js for V8 4.5 (Ali Ijaz Sheikh) [#2509](https://github.com/nodejs/node/pull/2509)
* \[[`fe4b309fd3`](https://github.com/nodejs/node/commit/fe4b309fd3)] - **test**: refactor to eliminate flaky test (Rich Trott) [#2609](https://github.com/nodejs/node/pull/2609)
* \[[`619721e6b8`](https://github.com/nodejs/node/commit/619721e6b8)] - **test**: mark eval\_messages as flaky (Alexis Campailla) [#2648](https://github.com/nodejs/node/pull/2648)
* \[[`93ba585b66`](https://github.com/nodejs/node/commit/93ba585b66)] - **test**: mark test-vm-syntax-error-stderr as flaky (João Reis) [#2662](https://github.com/nodejs/node/pull/2662)
* \[[`367140bca0`](https://github.com/nodejs/node/commit/367140bca0)] - **test**: mark test-repl-persistent-history as flaky (João Reis) [#2659](https://github.com/nodejs/node/pull/2659)
* \[[`f6b093343d`](https://github.com/nodejs/node/commit/f6b093343d)] - **timers**: minor `_unrefActive` fixes and improvements (Jeremiah Senkpiel) [#2540](https://github.com/nodejs/node/pull/2540)
* \[[`403d7ee7d1`](https://github.com/nodejs/node/commit/403d7ee7d1)] - **timers**: don't mutate unref list while iterating it (Julien Gilli) [#2540](https://github.com/nodejs/node/pull/2540)
* \[[`7a8c3e08c3`](https://github.com/nodejs/node/commit/7a8c3e08c3)] - **timers**: Avoid linear scan in `_unrefActive`. (Julien Gilli) [#2540](https://github.com/nodejs/node/pull/2540)
* \[[`b630ebaf43`](https://github.com/nodejs/node/commit/b630ebaf43)] - **win,msi**: Upgrade from old upgrade code (João Reis) [#2439](https://github.com/nodejs/node/pull/2439)

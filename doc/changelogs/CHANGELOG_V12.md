# Node.js 12 ChangeLog

<!--lint disable maximum-line-length no-literal-urls prohibited-strings-->

<table>
<tr>
<th>LTS 'Erbium'</th>
<th>Current</th>
</tr>
<tr>
<td valign="top">
<a href="#12.22.12">12.22.12</a><br/>
<a href="#12.22.11">12.22.11</a><br/>
<a href="#12.22.10">12.22.10</a><br/>
<a href="#12.22.9">12.22.9</a><br/>
<a href="#12.22.8">12.22.8</a><br/>
<a href="#12.22.7">12.22.7</a><br/>
<a href="#12.22.6">12.22.6</a><br/>
<a href="#12.22.5">12.22.5</a><br/>
<a href="#12.22.4">12.22.4</a><br/>
<a href="#12.22.3">12.22.3</a><br/>
<a href="#12.22.2">12.22.2</a><br/>
<a href="#12.22.1">12.22.1</a><br/>
<a href="#12.22.0">12.22.0</a><br/>
<a href="#12.21.0">12.21.0</a><br/>
<a href="#12.20.2">12.20.2</a><br/>
<a href="#12.20.1">12.20.1</a><br/>
<a href="#12.20.0">12.20.0</a><br/>
<a href="#12.19.1">12.19.1</a><br/>
<a href="#12.19.0">12.19.0</a><br/>
<a href="#12.18.4">12.18.4</a><br/>
<a href="#12.18.3">12.18.3</a><br/>
<a href="#12.18.2">12.18.2</a><br/>
<a href="#12.18.1">12.18.1</a><br/>
<a href="#12.18.0">12.18.0</a><br/>
<a href="#12.17.0">12.17.0</a><br/>
<a href="#12.16.3">12.16.3</a><br/>
<a href="#12.16.2">12.16.2</a><br/>
<a href="#12.16.1">12.16.1</a><br/>
<a href="#12.16.0">12.16.0</a><br/>
<a href="#12.15.0">12.15.0</a><br/>
<a href="#12.14.1">12.14.1</a><br/>
<a href="#12.14.0">12.14.0</a><br/>
<a href="#12.13.1">12.13.1</a><br/>
<a href="#12.13.0">12.13.0</a><br/>
</td>
<td valign="top">
<a href="#12.12.0">12.12.0</a><br/>
<a href="#12.11.1">12.11.1</a><br/>
<a href="#12.11.0">12.11.0</a><br/>
<a href="#12.10.0">12.10.0</a><br/>
<a href="#12.9.1">12.9.1</a><br/>
<a href="#12.9.0">12.9.0</a><br/>
<a href="#12.8.1">12.8.1</a><br/>
<a href="#12.8.0">12.8.0</a><br/>
<a href="#12.7.0">12.7.0</a><br/>
<a href="#12.6.0">12.6.0</a><br/>
<a href="#12.5.0">12.5.0</a><br/>
<a href="#12.4.0">12.4.0</a><br/>
<a href="#12.3.1">12.3.1</a><br/>
<a href="#12.3.0">12.3.0</a><br/>
<a href="#12.2.0">12.2.0</a><br/>
<a href="#12.1.0">12.1.0</a><br/>
<a href="#12.0.0">12.0.0</a><br/>
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
  * [11.x](CHANGELOG_V11.md)
  * [10.x](CHANGELOG_V10.md)
  * [9.x](CHANGELOG_V9.md)
  * [8.x](CHANGELOG_V8.md)
  * [7.x](CHANGELOG_V7.md)
  * [6.x](CHANGELOG_V6.md)
  * [5.x](CHANGELOG_V5.md)
  * [4.x](CHANGELOG_V4.md)
  * [0.12.x](CHANGELOG_V012.md)
  * [0.10.x](CHANGELOG_V010.md)
  * [io.js](CHANGELOG_IOJS.md)
  * [Archive](CHANGELOG_ARCHIVE.md)

<a id="12.22.12"></a>

## 2022-04-05, Version 12.22.12 'Erbium' (LTS), @richardlau

### Notable Changes

This is planned to be the final Node.js 12 release. Node.js 12 will
reach End-of-Life status on 30 April 2022, after which it will not
receive updates. You are strongly advised to migrate your applications
to Node.js 16 or 14 (both of which are Long Term Support (LTS) releases)
to continue to receive future security updates beyond 30 April 2022.

This release fixes a shutdown crash in Node-API (formerly N-API) and a
potential stack overflow when using `vm.runInNewContext()`.

The list of GPG keys used to sign releases and instructions on how to
fetch the keys for verifying binaries has been synchronized with the
main branch.

### Commits

* \[[`1193290f3f`](https://github.com/nodejs/node/commit/1193290f3f)] - **deps**: V8: cherry-pick cc9a8a37445e (devsnek) [#42065](https://github.com/nodejs/node/pull/42065)
* \[[`333eda8d03`](https://github.com/nodejs/node/commit/333eda8d03)] - **doc**: add a note about possible missing lines to readline.asyncIterator (Igor Mikhalev) [#34675](https://github.com/nodejs/node/pull/34675)
* \[[`518a49c0c6`](https://github.com/nodejs/node/commit/518a49c0c6)] - **doc**: use openpgp.org for keyserver examples (Nick Schonning) [#39227](https://github.com/nodejs/node/pull/39227)
* \[[`11aef2ad03`](https://github.com/nodejs/node/commit/11aef2ad03)] - **doc**: update release key for Danielle Adams (Danielle Adams) [#36793](https://github.com/nodejs/node/pull/36793)
* \[[`a9c38f1003`](https://github.com/nodejs/node/commit/a9c38f1003)] - **doc**: add release key for Danielle Adams (Danielle Adams) [#35545](https://github.com/nodejs/node/pull/35545)
* \[[`a35f553889`](https://github.com/nodejs/node/commit/a35f553889)] - **doc**: add release key for Bryan English (Bryan English) [#42102](https://github.com/nodejs/node/pull/42102)
* \[[`5f104e3218`](https://github.com/nodejs/node/commit/5f104e3218)] - **node-api**: cctest on v8impl::Reference (legendecas) [#38970](https://github.com/nodejs/node/pull/38970)
* \[[`e23c04f0dc`](https://github.com/nodejs/node/commit/e23c04f0dc)] - **node-api**: avoid SecondPassCallback crash (Michael Dawson) [#38899](https://github.com/nodejs/node/pull/38899)
* \[[`a7224c9559`](https://github.com/nodejs/node/commit/a7224c9559)] - **node-api**: fix shutdown crashes (Michael Dawson) [#38492](https://github.com/nodejs/node/pull/38492)
* \[[`81b4dc88f1`](https://github.com/nodejs/node/commit/81b4dc88f1)] - **node-api**: make reference weak parameter an indirect link to references (Chengzhong Wu) [#38000](https://github.com/nodejs/node/pull/38000)
* \[[`2aa9ca1ea9`](https://github.com/nodejs/node/commit/2aa9ca1ea9)] - **node-api**: fix crash in finalization (Michael Dawson) [#37876](https://github.com/nodejs/node/pull/37876)
* \[[`a2f4206415`](https://github.com/nodejs/node/commit/a2f4206415)] - **node-api**: stop ref gc during environment teardown (Gabriel Schulhof) [#37616](https://github.com/nodejs/node/pull/37616)
* \[[`171bb66ccc`](https://github.com/nodejs/node/commit/171bb66ccc)] - **node-api**: force env shutdown deferring behavior (Gabriel Schulhof) [#37303](https://github.com/nodejs/node/pull/37303)
* \[[`e707514c80`](https://github.com/nodejs/node/commit/e707514c80)] - **src**: fix finalization crash (James M Snell) [#38250](https://github.com/nodejs/node/pull/38250)

<a id="12.22.11"></a>

## 2022-03-17, Version 12.22.11 'Erbium' (LTS), @richardlau

This is a security release.

### Notable changes

Update to OpenSSL 1.1.1n, which addresses the following vulnerability:

* Infinite loop in `BN_mod_sqrt()` reachable when parsing certificates (High)(CVE-2022-0778)
  More details are available at <https://www.openssl.org/news/secadv/20220315.txt>

Fix for building Node.js 12.x with Visual Studio 2019 to allow us to continue to
run CI tests.

### Commits

* \[[`e3e5bf11ba`](https://github.com/nodejs/node/commit/e3e5bf11ba)] - **build**: pin Windows GitHub runner to windows-2019 (Richard Lau) [#42349](https://github.com/nodejs/node/pull/42349)
* \[[`f41e7771bf`](https://github.com/nodejs/node/commit/f41e7771bf)] - **build**: fix detection of Visual Studio 2019 (Richard Lau) [#42349](https://github.com/nodejs/node/pull/42349)
* \[[`c372ec207d`](https://github.com/nodejs/node/commit/c372ec207d)] - **deps**: update archs files for OpenSSL-1.1.n (Richard Lau) [#42348](https://github.com/nodejs/node/pull/42348)
* \[[`d574a1dccb`](https://github.com/nodejs/node/commit/d574a1dccb)] - **deps**: upgrade openssl sources to 1.1.1n (Richard Lau) [#42348](https://github.com/nodejs/node/pull/42348)

<a id="12.22.10"></a>

## 2022-02-01, Version 12.22.10 'Erbium' (LTS), @ruyadorno

### Notable changes

* Upgrade npm to 6.14.16
* Updated ICU time zone data

### Commits

* \[[`33899b435d`](https://github.com/nodejs/node/commit/33899b435d)] - **deps**: upgrade npm to 6.14.16 (Ruy Adorno) [#41601](https://github.com/nodejs/node/pull/41601)
* \[[`d9237c46ca`](https://github.com/nodejs/node/commit/d9237c46ca)] - **tools**: update tzdata to 2021a4 (Albert Wang) [#41443](https://github.com/nodejs/node/pull/41443)

<a id="12.22.9"></a>

## 2022-01-10, Version 12.22.9 'Erbium' (LTS), @richardlau

This is a security release.

### Notable changes

#### Improper handling of URI Subject Alternative Names (Medium)(CVE-2021-44531)

Accepting arbitrary Subject Alternative Name (SAN) types, unless a PKI is specifically defined to use a particular SAN type, can result in bypassing name-constrained intermediates. Node.js was accepting URI SAN types, which PKIs are often not defined to use. Additionally, when a protocol allows URI SANs, Node.js did not match the URI correctly.

Versions of Node.js with the fix for this disable the URI SAN type when checking a certificate against a hostname. This behavior can be reverted through the `--security-revert` command-line option.

More details will be available at [CVE-2021-44531](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2021-44531) after publication.

#### Certificate Verification Bypass via String Injection (Medium)(CVE-2021-44532)

Node.js converts SANs (Subject Alternative Names) to a string format. It uses this string to check peer certificates against hostnames when validating connections. The string format was subject to an injection vulnerability when name constraints were used within a certificate chain, allowing the bypass of these name constraints.

Versions of Node.js with the fix for this escape SANs containing the problematic characters in order to prevent the injection. This behavior can be reverted through the `--security-revert` command-line option.

More details will be available at [CVE-2021-44532](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2021-44532) after publication.

#### Incorrect handling of certificate subject and issuer fields (Medium)(CVE-2021-44533)

Node.js did not handle multi-value Relative Distinguished Names correctly. Attackers could craft certificate subjects containing a single-value Relative Distinguished Name that would be interpreted as a multi-value Relative Distinguished Name, for example, in order to inject a Common Name that would allow bypassing the certificate subject verification.

Affected versions of Node.js do not accept multi-value Relative Distinguished Names and are thus not vulnerable to such attacks themselves. However, third-party code that uses node's ambiguous presentation of certificate subjects may be vulnerable.

More details will be available at [CVE-2021-44533](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2021-44533) after publication.

#### Prototype pollution via `console.table` properties (Low)(CVE-2022-21824)

Due to the formatting logic of the `console.table()` function it was not safe to allow user controlled input to be passed to the `properties` parameter while simultaneously passing a plain object with at least one property as the first parameter, which could be `__proto__`. The prototype pollution has very limited control, in that it only allows an empty string to be assigned numerical keys of the object prototype.

Versions of Node.js with the fix for this use a null protoype for the object these properties are being assigned to.

More details will be available at [CVE-2022-21824](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-21824) after publication.

Thanks to Patrik Oldsberg (rugvip) for reporting this vulnerability.

### Commits

* \[[`be69403528`](https://github.com/nodejs/node/commit/be69403528)] - **console**: fix prototype pollution via console.table (Tobias Nießen) [nodejs-private/node-private#307](https://github.com/nodejs-private/node-private/pull/307)
* \[[`19873abfb2`](https://github.com/nodejs/node/commit/19873abfb2)] - **crypto,tls**: implement safe x509 GeneralName format (Tobias Nießen and Akshay Kumar) [nodejs-private/node-private#300](https://github.com/nodejs-private/node-private/pull/300)
* \[[`ff9ac7d757`](https://github.com/nodejs/node/commit/ff9ac7d757)] - **doc**: fix date for v12.22.8 (Richard Lau) [#41213](https://github.com/nodejs/node/pull/41213)
* \[[`a5c7843cab`](https://github.com/nodejs/node/commit/a5c7843cab)] - **src**: add cve reverts and associated tests (Michael Dawson and Akshay Kumar) [nodejs-private/node-private#300](https://github.com/nodejs-private/node-private/pull/300)
* \[[`d4e5d1b9ca`](https://github.com/nodejs/node/commit/d4e5d1b9ca)] - **src**: remove unused x509 functions (Tobias Nießen and Akshay Kumar) [nodejs-private/node-private#300](https://github.com/nodejs-private/node-private/pull/300)
* \[[`8c2db2c86b`](https://github.com/nodejs/node/commit/8c2db2c86b)] - **tls**: fix handling of x509 subject and issuer (Tobias Nießen and Akshay Kumar) [nodejs-private/node-private#300](https://github.com/nodejs-private/node-private/pull/300)
* \[[`e0fe6a635e`](https://github.com/nodejs/node/commit/e0fe6a635e)] - **tls**: drop support for URI alternative names (Tobias Nießen and Akshay Kumar) [nodejs-private/node-private#300](https://github.com/nodejs-private/node-private/pull/300)

<a id="12.22.8"></a>

## 2021-12-16, Version 12.22.8 'Erbium' (LTS), @richardlau

### Notable Changes

This release contains a c-ares update to fix a regression introduced in
Node.js 12.22.5 resolving CNAME records containing underscores
[#39780](https://github.com/nodejs/node/issues/39780).

Root certificates have been updated to those from Mozilla's Network
Security Services 3.71 [#40281](https://github.com/nodejs/node/pull/40280).

### Commits

* \[[`2d42295d2a`](https://github.com/nodejs/node/commit/2d42295d2a)] - **build**: pin macOS GitHub runner to macos-10.15 (Richard Lau) [#41124](https://github.com/nodejs/node/pull/41124)
* \[[`41e09ec71b`](https://github.com/nodejs/node/commit/41e09ec71b)] - **child\_process**: retain reference to data with advanced serialization (Anna Henningsen) [#38728](https://github.com/nodejs/node/pull/38728)
* \[[`f0be07796e`](https://github.com/nodejs/node/commit/f0be07796e)] - **crypto**: update root certificates (Richard Lau) [#40280](https://github.com/nodejs/node/pull/40280)
* \[[`4c9f920d34`](https://github.com/nodejs/node/commit/4c9f920d34)] - **deps**: update archs files for OpenSSL-1.1.1m (Richard Lau) [#41172](https://github.com/nodejs/node/pull/41172)
* \[[`60d7d4171e`](https://github.com/nodejs/node/commit/60d7d4171e)] - **deps**: upgrade openssl sources to 1.1.1m (Richard Lau) [#41172](https://github.com/nodejs/node/pull/41172)
* \[[`7feff67419`](https://github.com/nodejs/node/commit/7feff67419)] - **deps**: add -fno-strict-aliasing flag to libuv (Daniel Bevenius) [#40631](https://github.com/nodejs/node/pull/40631)
* \[[`534ac7c7c6`](https://github.com/nodejs/node/commit/534ac7c7c6)] - **deps**: update c-ares to 1.18.1 (Richard Lau) [#40660](https://github.com/nodejs/node/pull/40660)
* \[[`c019fa9b70`](https://github.com/nodejs/node/commit/c019fa9b70)] - **deps**: update to cjs-module-lexer\@1.2.2 (Guy Bedford) [#39402](https://github.com/nodejs/node/pull/39402)
* \[[`b13340eff4`](https://github.com/nodejs/node/commit/b13340eff4)] - **doc**: add alternative version links to the packages page (Filip Skokan) [#36915](https://github.com/nodejs/node/pull/36915)
* \[[`243b2fbfdb`](https://github.com/nodejs/node/commit/243b2fbfdb)] - **lib**: fix regular expression to detect \`/\` and \`\\\` (Francesco Trotta) [#40325](https://github.com/nodejs/node/pull/40325)
* \[[`70e094a26b`](https://github.com/nodejs/node/commit/70e094a26b)] - **repl**: fix error message printing (Anna Henningsen) [#38209](https://github.com/nodejs/node/pull/38209)
* \[[`02b432a704`](https://github.com/nodejs/node/commit/02b432a704)] - **src**: fix crash in AfterGetAddrInfo (Anna Henningsen) [#39735](https://github.com/nodejs/node/pull/39735)
* \[[`7479447d6a`](https://github.com/nodejs/node/commit/7479447d6a)] - **test**: deflake child-process-pipe-dataflow (Luigi Pinca) [#40838](https://github.com/nodejs/node/pull/40838)
* \[[`833e199393`](https://github.com/nodejs/node/commit/833e199393)] - **tools**: update certdata.txt (Richard Lau) [#40280](https://github.com/nodejs/node/pull/40280)
* \[[`e4339fe286`](https://github.com/nodejs/node/commit/e4339fe286)] - **tools**: add script to update c-ares (Richard Lau) [#40660](https://github.com/nodejs/node/pull/40660)
* \[[`f50b9c1e8a`](https://github.com/nodejs/node/commit/f50b9c1e8a)] - **worker**: avoid potential deadlock on NearHeapLimit (Santiago Gimeno) [#38403](https://github.com/nodejs/node/pull/38403)

<a id="12.22.7"></a>

## 2021-10-12, Version 12.22.7 'Erbium' (LTS), @danielleadams

This is a security release.

### Notable changes

* **CVE-2021-22959**: HTTP Request Smuggling due to spaced in headers (Medium)
  * The http parser accepts requests with a space (SP) right after the header name before the colon. This can lead to HTTP Request Smuggling (HRS). More details will be available at [CVE-2021-22959](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2021-22959) after publication.
* **CVE-2021-22960**: HTTP Request Smuggling when parsing the body (Medium)
  * The http parser ignores chunk extensions when parsing the body of chunked requests. This leads to HTTP Request Smuggling (HRS) under certain conditions. More details will be available at [CVE-2021-22960](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2021-22960) after publication.

### Commits

* \[[`21a2e554e3`](https://github.com/nodejs/node/commit/21a2e554e3)] - **deps**: update llhttp to 2.1.4 (Fedor Indutny) [nodejs-private/node-private#286](https://github.com/nodejs-private/node-private/pull/286)
* \[[`d5d3a03246`](https://github.com/nodejs/node/commit/d5d3a03246)] - **http**: add regression test for smuggling content length (Matteo Collina) [nodejs-private/node-private#286](https://github.com/nodejs-private/node-private/pull/286)
* \[[`0858587f21`](https://github.com/nodejs/node/commit/0858587f21)] - **http**: add regression test for chunked smuggling (Matteo Collina) [nodejs-private/node-private#286](https://github.com/nodejs-private/node-private/pull/286)

<a id="12.22.6"></a>

## 2021-08-31, Version 12.22.6 'Erbium' (LTS), @MylesBorins

This is a security release.

### Notable Changes

These are vulnerabilities in the node-tar, arborist, and npm cli modules which
are related to the initial reports and subsequent remediation of node-tar
vulnerabilities [CVE-2021-32803](https://github.com/advisories/GHSA-r628-mhmh-qjhw)
and [CVE-2021-32804](https://github.com/advisories/GHSA-3jfq-g458-7qm9).
Subsequent internal security review of node-tar and additional external bounty
reports have resulted in another 5 CVE being remediated in core npm CLI
dependencies including node-tar, and npm arborist.

You can read more about it in:

* [CVE-2021-37701](https://github.com/npm/node-tar/security/advisories/GHSA-9r2w-394v-53qc)
* [CVE-2021-37712](https://github.com/npm/node-tar/security/advisories/GHSA-qq89-hq3f-393p)
* [CVE-2021-37713](https://github.com/npm/node-tar/security/advisories/GHSA-5955-9wpr-37jh)
* [CVE-2021-39134](https://github.com/npm/arborist/security/advisories/GHSA-2h3h-q99f-3fhc)
* [CVE-2021-39135](https://github.com/npm/arborist/security/advisories/GHSA-gmw6-94gg-2rc2)

### Commits

* \[[`a0154b586b`](https://github.com/nodejs/node/commit/a0154b586b)] - **deps**: update archs files for OpenSSL-1.1.1l (Richard Lau) [#39869](https://github.com/nodejs/node/pull/39869)
* \[[`7a95637eb7`](https://github.com/nodejs/node/commit/7a95637eb7)] - **deps**: upgrade openssl sources to 1.1.1l (Richard Lau) [#39869](https://github.com/nodejs/node/pull/39869)
* \[[`840b0ffff6`](https://github.com/nodejs/node/commit/840b0ffff6)] - **deps**: upgrade npm to 6.14.15 (Darcy Clarke) [#39856](https://github.com/nodejs/node/pull/39856)

<a id="12.22.5"></a>

## 2021-08-11, Version 12.22.5 'Erbium' (LTS), @BethGriggs

This is a security release.

### Notable Changes

* **CVE-2021-3672/CVE-2021-22931**: Improper handling of untypical characters in domain names (High)
  * Node.js was vulnerable to Remote Code Execution, XSS, application crashes due to missing input validation of hostnames returned by Domain Name Servers in the Node.js DNS library which can lead to the output of wrong hostnames (leading to Domain Hijacking) and injection vulnerabilities in applications using the library. You can read more about it at <https://nvd.nist.gov/vuln/detail/CVE-2021-22931>.
* **CVE-2021-22940**: Use after free on close http2 on stream canceling (High)
  * Node.js was vulnerable to a use after free attack where an attacker might be able to exploit memory corruption to change process behavior. This release includes a follow-up fix for CVE-2021-22930 as the issue was not completely resolved by the previous fix. You can read more about it at <https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2021-22940>.
* **CVE-2021-22939**: Incomplete validation of rejectUnauthorized parameter (Low)
  * If the Node.js HTTPS API was used incorrectly and "undefined" was in passed for the "rejectUnauthorized" parameter, no error was returned and connections to servers with an expired certificate would have been accepted. You can read more about it at <https://nvd.nist.gov/vuln/detail/CVE-2021-22939>.

### Commits

* \[[`5f947db68c`](https://github.com/nodejs/node/commit/5f947db68c)] - **deps**: update c-ares to 1.17.2 (Beth Griggs) [#39724](https://github.com/nodejs/node/pull/39724)
* \[[`42695ea34b`](https://github.com/nodejs/node/commit/42695ea34b)] - **deps**: reflect c-ares source tree (Beth Griggs) [#39653](https://github.com/nodejs/node/pull/39653)
* \[[`e4c9156b32`](https://github.com/nodejs/node/commit/e4c9156b32)] - **deps**: apply missed updates from c-ares 1.17.1 (Beth Griggs) [#39653](https://github.com/nodejs/node/pull/39653)
* \[[`9cd1f53103`](https://github.com/nodejs/node/commit/9cd1f53103)] - **http2**: add tests for cancel event while client is paused reading (Akshay K) [#39622](https://github.com/nodejs/node/pull/39622)
* \[[`2008c9722f`](https://github.com/nodejs/node/commit/2008c9722f)] - **http2**: update handling of rst\_stream with error code NGHTTP2\_CANCEL (Akshay K) [#39622](https://github.com/nodejs/node/pull/39622)
* \[[`1780bbc329`](https://github.com/nodejs/node/commit/1780bbc329)] - **tls**: validate "rejectUnauthorized: undefined" (Matteo Collina) [nodejs-private/node-private#276](https://github.com/nodejs-private/node-private/pull/276)

<a id="12.22.4"></a>

## 2021-07-29, Version 12.22.4 'Erbium' (LTS), @richardlau

This is a security release.

### Notable Changes

* **CVE-2021-22930**: Use after free on close http2 on stream canceling (High)
  * Node.js is vulnerable to a use after free attack where an attacker might be able to exploit the memory corruption, to change process behavior. You can read more about it in <https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2021-22930>

### Commits

* \[[`499e56babe`](https://github.com/nodejs/node/commit/499e56babe)] - **build**: fix label-pr workflow (Michaël Zasso) [#38399](https://github.com/nodejs/node/pull/38399)
* \[[`98ac3c4108`](https://github.com/nodejs/node/commit/98ac3c4108)] - **build**: label PRs with GitHub Action instead of nodejs-github-bot (Phillip Johnsen) [#38301](https://github.com/nodejs/node/pull/38301)
* \[[`ddc8dde150`](https://github.com/nodejs/node/commit/ddc8dde150)] - **deps**: upgrade npm to 6.14.14 (Darcy Clarke) [#39553](https://github.com/nodejs/node/pull/39553)
* \[[`e11a862eed`](https://github.com/nodejs/node/commit/e11a862eed)] - **deps**: update to c-ares 1.17.1 (Danny Sonnenschein) [#36207](https://github.com/nodejs/node/pull/36207)
* \[[`39e9cd540f`](https://github.com/nodejs/node/commit/39e9cd540f)] - **deps**: restore minimum ICU version to 65 (Richard Lau) [#39068](https://github.com/nodejs/node/pull/39068)
* \[[`e459c79b02`](https://github.com/nodejs/node/commit/e459c79b02)] - **deps**: V8: cherry-pick 035c305ce776 (Michaël Zasso) [#38497](https://github.com/nodejs/node/pull/38497)
* \[[`b3c698a5d8`](https://github.com/nodejs/node/commit/b3c698a5d8)] - **deps**: update to cjs-module-lexer\@1.2.1 (Guy Bedford) [#38450](https://github.com/nodejs/node/pull/38450)
* \[[`7d5a2f9588`](https://github.com/nodejs/node/commit/7d5a2f9588)] - **deps**: update to cjs-module-lexer\@1.1.1 (Guy Bedford) [#37992](https://github.com/nodejs/node/pull/37992)
* \[[`906b43e586`](https://github.com/nodejs/node/commit/906b43e586)] - **deps**: V8: update build dependencies (Michaël Zasso) [#39245](https://github.com/nodejs/node/pull/39245)
* \[[`15b91fa3fa`](https://github.com/nodejs/node/commit/15b91fa3fa)] - **deps**: V8: backport 895949419186 (Michaël Zasso) [#39245](https://github.com/nodejs/node/pull/39245)
* \[[`8046daf09f`](https://github.com/nodejs/node/commit/8046daf09f)] - **deps**: V8: cherry-pick 0b3a4ecf7083 (Michaël Zasso) [#39245](https://github.com/nodejs/node/pull/39245)
* \[[`f4377b13a6`](https://github.com/nodejs/node/commit/f4377b13a6)] - **deps**: V8: cherry-pick 7c182bd65f42 (Michaël Zasso) [#39245](https://github.com/nodejs/node/pull/39245)
* \[[`add7b5b4c2`](https://github.com/nodejs/node/commit/add7b5b4c2)] - **deps**: V8: cherry-pick cc641f6be756 (Michaël Zasso) [#39245](https://github.com/nodejs/node/pull/39245)
* \[[`a73275f056`](https://github.com/nodejs/node/commit/a73275f056)] - **deps**: V8: cherry-pick 7b3332844212 (Michaël Zasso) [#39245](https://github.com/nodejs/node/pull/39245)
* \[[`492b0d6b37`](https://github.com/nodejs/node/commit/492b0d6b37)] - **deps**: V8: cherry-pick e6f62a41f5ee (Michaël Zasso) [#39245](https://github.com/nodejs/node/pull/39245)
* \[[`2b54156260`](https://github.com/nodejs/node/commit/2b54156260)] - **deps**: V8: cherry-pick 92e6d3317082 (Michaël Zasso) [#39245](https://github.com/nodejs/node/pull/39245)
* \[[`bbceab4d91`](https://github.com/nodejs/node/commit/bbceab4d91)] - **deps**: V8: backport 1b1eda0876aa (Michaël Zasso) [#39245](https://github.com/nodejs/node/pull/39245)
* \[[`93a1a3c5ae`](https://github.com/nodejs/node/commit/93a1a3c5ae)] - **deps**: V8: cherry-pick 530080c44af2 (Milad Fa) [#38509](https://github.com/nodejs/node/pull/38509)
* \[[`b263f2585a`](https://github.com/nodejs/node/commit/b263f2585a)] - **http2**: on receiving rst\_stream with cancel code add it to pending list (Akshay K) [#39423](https://github.com/nodejs/node/pull/39423)
* \[[`3e4bc1b0d3`](https://github.com/nodejs/node/commit/3e4bc1b0d3)] - **module**: fix legacy `node` specifier resolution to resolve `"main"` field (Antoine du Hamel) [#38979](https://github.com/nodejs/node/pull/38979)
* \[[`f552c45676`](https://github.com/nodejs/node/commit/f552c45676)] - **src**: move CHECK in AddIsolateFinishedCallback (Fedor Indutny) [#38010](https://github.com/nodejs/node/pull/38010)
* \[[`30ce0e66ae`](https://github.com/nodejs/node/commit/30ce0e66ae)] - **src**: update cares\_wrap OpenBSD defines (Anna Henningsen) [#38670](https://github.com/nodejs/node/pull/38670)

<a id="12.22.3"></a>

## 2021-07-05, Version 12.22.3 'Erbium' (LTS), @richardlau

### Notable Changes

Node.js 12.22.2 introduced a regression in the Windows installer on
non-English locales that is being fixed in this release. There is no
need to download this release if you are not using the Windows
installer.

### Commits

* \[[`182f86a4d4`](https://github.com/nodejs/node/commit/182f86a4d4)] - **win,msi**: use localized "Authenticated Users" name (Richard Lau) [#39241](https://github.com/nodejs/node/pull/39241)

<a id="12.22.2"></a>

## 2021-07-01, Version 12.22.2 'Erbium' (LTS), @richardlau

This is a security release.

### Notable Changes

Vulnerabilities fixed:

* **CVE-2021-22918**: libuv upgrade - Out of bounds read (Medium)
  * Node.js is vulnerable to out-of-bounds read in libuv's uv\_\_idna\_toascii() function which is used to convert strings to ASCII. This is called by Node's dns module's lookup() function and can lead to information disclosures or crashes. You can read more about it in <https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2021-22918>
* **CVE-2021-22921**: Windows installer - Node Installer Local Privilege Escalation (Medium)
  * Node.js is vulnerable to local privilege escalation attacks under certain conditions on Windows platforms. More specifically, improper configuration of permissions in the installation directory allows an attacker to perform two different escalation attacks: PATH and DLL hijacking. You can read more about it in <https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2021-22921>
* **CVE-2021-27290**: npm upgrade - ssri Regular Expression Denial of Service (ReDoS) (High)
  * This is a vulnerability in the ssri npm mudule which may be vulnerable to denial of service attacks. You can read more about it in <https://github.com/advisories/GHSA-vx3p-948g-6vhq>
* **CVE-2021-23362**: npm upgrade - hosted-git-info Regular Expression Denial of Service (ReDoS) (Medium)
  * This is a vulnerability in the hosted-git-info npm mudule which may be vulnerable to denial of service attacks. You can read more about it in <https://nvd.nist.gov/vuln/detail/CVE-2021-23362>

### Commits

* \[[`623fd1fcb5`](https://github.com/nodejs/node/commit/623fd1fcb5)] - **deps**: uv: cherry-pick 99c29c9c2c9b (Ben Noordhuis) [nodejs-private/node-private#267](https://github.com/nodejs-private/node-private/pull/267)
* \[[`923b3760f8`](https://github.com/nodejs/node/commit/923b3760f8)] - **deps**: upgrade npm to 6.14.13 (Ruy Adorno) [#38214](https://github.com/nodejs/node/pull/38214)
* \[[`a52790cba0`](https://github.com/nodejs/node/commit/a52790cba0)] - **win,msi**: set install directory permission (AkshayK) [nodejs-private/node-private#269](https://github.com/nodejs-private/node-private/pull/269)

<a id="12.22.1"></a>

## 2021-04-06, Version 12.22.1 'Erbium' (LTS), @mylesborins

This is a security release.

### Notable Changes

Vulnerabilities fixed:

* **CVE-2021-3450**: OpenSSL - CA certificate check bypass with X509\_V\_FLAG\_X509\_STRICT (High)
  * This is a vulnerability in OpenSSL which may be exploited through Node.js. You can read more about it in <https://www.openssl.org/news/secadv/20210325.txt>
  * Impacts:
    * All versions of the 15.x, 14.x, 12.x and 10.x releases lines
* **CVE-2021-3449**: OpenSSL - NULL pointer deref in signature\_algorithms processing (High)
  * This is a vulnerability in OpenSSL which may be exploited through Node.js. You can read more about it in <https://www.openssl.org/news/secadv/20210325.txt>
  * Impacts:
    * All versions of the 15.x, 14.x, 12.x and 10.x releases lines
* **CVE-2020-7774**: npm upgrade - Update y18n to fix Prototype-Pollution (High)
  * This is a vulnerability in the y18n npm module which may be exploited by prototype pollution. You can read more about it in <https://github.com/advisories/GHSA-c4w7-xm78-47vh>
  * Impacts:
    * All versions of the 14.x, 12.x and 10.x releases lines

### Commits

* \[[`c947f1a0e1`](https://github.com/nodejs/node/commit/c947f1a0e1)] - **deps**: upgrade npm to 6.14.12 (Ruy Adorno) [#37918](https://github.com/nodejs/node/pull/37918)
* \[[`51a753c06f`](https://github.com/nodejs/node/commit/51a753c06f)] - **deps**: update archs files for OpenSSL-1.1.1k (Tobias Nießen) [#37939](https://github.com/nodejs/node/pull/37939)
* \[[`c85a519b48`](https://github.com/nodejs/node/commit/c85a519b48)] - **deps**: upgrade openssl sources to 1.1.1k (Tobias Nießen) [#37939](https://github.com/nodejs/node/pull/37939)

<a id="12.22.0"></a>

## 2021-03-30, Version 12.22.0 'Erbium' (LTS), @richardlau

### Notable changes

#### The legacy HTTP parser is runtime deprecated

The legacy HTTP parser, selected by the `--http-parser=legacy` command line
option, is deprecated with the pending End-of-Life of Node.js 10.x (where it
is the only HTTP parser implementation provided) at the end of April 2021. It
will now warn on use but otherwise continue to function and may be removed in
a future Node.js 12.x release.

The default HTTP parser based on llhttp is not affected. By default it is
stricter than the now deprecated legacy HTTP parser. If interoperability with
HTTP implementations that send invalid HTTP headers is required, the HTTP
parser can be started in a less secure mode with the
[`--insecure-http-parser`](https://nodejs.org/docs/latest-v12.x/api/cli.html#cli_insecure_http_parser)
command line option.

Contributed by Beth Griggs [#37603](https://github.com/nodejs/node/pull/37603).

#### ES Modules

ES Modules are now considered stable.

Contributed by Guy Bedford [#35781](https://github.com/nodejs/node/pull/35781)

#### node-api

Updated to node-api version 8 and added an experimental API to allow retrieval of the add-on file name.

Contributed by Gabriel Schulhof [#37652](https://github.com/nodejs/node/pull/37652) and [#37195](https://github.com/nodejs/node/pull/37195).

#### New API's to control code coverage data collection

`v8.stopCoverage()` and `v8.takeCoverage()` have been added.

Contributed by Joyee Cheung [#33807](https://github.com/nodejs/node/pull/33807).

#### New API to monitor event loop utilization by Worker threads

`worker.performance.eventLoopUtilization()` has been added.

Contributed by Trevor Norris [#35664](https://github.com/nodejs/node/pull/35664).

### Commits

* \[[`1872625990`](https://github.com/nodejs/node/commit/1872625990)] - **(SEMVER-MINOR)** **deps**: update to cjs-module-lexer\@1.1.0 (Guy Bedford) [#37712](https://github.com/nodejs/node/pull/37712)
* \[[`dfa04d9035`](https://github.com/nodejs/node/commit/dfa04d9035)] - **deps**: V8: cherry-pick beebee4f80ff (Peter Marshall) [#37293](https://github.com/nodejs/node/pull/37293)
* \[[`bf8733fe22`](https://github.com/nodejs/node/commit/bf8733fe22)] - **doc**: mark modules implementation as stable (Guy Bedford) [#35781](https://github.com/nodejs/node/pull/35781)
* \[[`0a35d49f56`](https://github.com/nodejs/node/commit/0a35d49f56)] - _**Revert**_ "**embedding**: make Stop() stop Workers" (Anna Henningsen) [#32623](https://github.com/nodejs/node/pull/32623)
* \[[`a0b610450a`](https://github.com/nodejs/node/commit/a0b610450a)] - **(SEMVER-MINOR)** **http**: runtime deprecate legacy HTTP parser (Beth Griggs) [#37603](https://github.com/nodejs/node/pull/37603)
* \[[`2da24ac302`](https://github.com/nodejs/node/commit/2da24ac302)] - **lib**: add URI handling functions to primordials (Antoine du Hamel) [#37394](https://github.com/nodejs/node/pull/37394)
* \[[`7b0ed4ba92`](https://github.com/nodejs/node/commit/7b0ed4ba92)] - **module**: improve support of data: URLs (Antoine du Hamel) [#37392](https://github.com/nodejs/node/pull/37392)
* \[[`93dd799a86`](https://github.com/nodejs/node/commit/93dd799a86)] - **(SEMVER-MINOR)** **node-api**: define version 8 (Gabriel Schulhof) [#37652](https://github.com/nodejs/node/pull/37652)
* \[[`f5692093d3`](https://github.com/nodejs/node/commit/f5692093d3)] - **(SEMVER-MINOR)** **node-api**: allow retrieval of add-on file name (Gabriel Schulhof) [#37195](https://github.com/nodejs/node/pull/37195)
* \[[`6cef0e3678`](https://github.com/nodejs/node/commit/6cef0e3678)] - **src,test**: add regression test for nested Worker termination (Anna Henningsen) [#32623](https://github.com/nodejs/node/pull/32623)
* \[[`364bf03a68`](https://github.com/nodejs/node/commit/364bf03a68)] - **test**: fix races in test-performance-eventlooputil (Gerhard Stoebich) [#36028](https://github.com/nodejs/node/pull/36028)
* \[[`d7a4ccdf09`](https://github.com/nodejs/node/commit/d7a4ccdf09)] - **test**: correct test-worker-eventlooputil (Gerhard Stoebich) [#35891](https://github.com/nodejs/node/pull/35891)
* \[[`0f6d44500c`](https://github.com/nodejs/node/commit/0f6d44500c)] - **test**: add cpu-profiler-crash test (Santiago Gimeno) [#37293](https://github.com/nodejs/node/pull/37293)
* \[[`86f34ee18c`](https://github.com/nodejs/node/commit/86f34ee18c)] - **(SEMVER-MINOR)** **v8**: implement v8.stopCoverage() (Joyee Cheung) [#33807](https://github.com/nodejs/node/pull/33807)
* \[[`8ddea3f16d`](https://github.com/nodejs/node/commit/8ddea3f16d)] - **(SEMVER-MINOR)** **v8**: implement v8.takeCoverage() (Joyee Cheung) [#33807](https://github.com/nodejs/node/pull/33807)
* \[[`eec7542781`](https://github.com/nodejs/node/commit/eec7542781)] - **(SEMVER-MINOR)** **worker**: add eventLoopUtilization() (Trevor Norris) [#35664](https://github.com/nodejs/node/pull/35664)

<a id="12.21.0"></a>

## 2021-02-23, Version 12.21.0 'Erbium' (LTS), @richardlau

This is a security release.

### Notable changes

Vulnerabilities fixed:

* **CVE-2021-22883**: HTTP2 'unknownProtocol' cause Denial of Service by resource exhaustion
  * Affected Node.js versions are vulnerable to denial of service attacks when too many connection attempts with an 'unknownProtocol' are established. This leads to a leak of file descriptors. If a file descriptor limit is configured on the system, then the server is unable to accept new connections and prevent the process also from opening, e.g. a file. If no file descriptor limit is configured, then this lead to an excessive memory usage and cause the system to run out of memory.
* **CVE-2021-22884**: DNS rebinding in --inspect
  * Affected Node.js versions are vulnerable to denial of service attacks when the whitelist includes “localhost6”. When “localhost6” is not present in /etc/hosts, it is just an ordinary domain that is resolved via DNS, i.e., over network. If the attacker controls the victim's DNS server or can spoof its responses, the DNS rebinding protection can be bypassed by using the “localhost6” domain. As long as the attacker uses the “localhost6” domain, they can still apply the attack described in CVE-2018-7160.
* **CVE-2021-23840**: OpenSSL - Integer overflow in CipherUpdate
  * This is a vulnerability in OpenSSL which may be exploited through Node.js. You can read more about it in <https://www.openssl.org/news/secadv/20210216.txt>

### Commits

* \[[`e69177a088`](https://github.com/nodejs/node/commit/e69177a088)] - **deps**: update archs files for OpenSSL-1.1.1j (Daniel Bevenius) [#37413](https://github.com/nodejs/node/pull/37413)
* \[[`0633ae77e6`](https://github.com/nodejs/node/commit/0633ae77e6)] - **deps**: upgrade openssl sources to 1.1.1j (Daniel Bevenius) [#37413](https://github.com/nodejs/node/pull/37413)
* \[[`922ada7713`](https://github.com/nodejs/node/commit/922ada7713)] - **(SEMVER-MINOR)** **http2**: add unknownProtocol timeout (Daniel Bevenius) [nodejs-private/node-private#246](https://github.com/nodejs-private/node-private/pull/246)
* \[[`1564752d55`](https://github.com/nodejs/node/commit/1564752d55)] - **src**: drop localhost6 as allowed host for inspector (Matteo Collina) [nodejs-private/node-private#244](https://github.com/nodejs-private/node-private/pull/244)

<a id="12.20.2"></a>

## 2021-02-10, Version 12.20.2 'Erbium' (LTS), @ruyadorno

### Notable changes

* **deps**:
  * upgrade npm to 6.14.11 (Ruy Adorno) [#37173](https://github.com/nodejs/node/pull/37173)

### Commits

* \[[`e8a4e560ea`](https://github.com/nodejs/node/commit/e8a4e560ea)] - **async\_hooks**: fix leak in AsyncLocalStorage exit (Stephen Belanger) [#35779](https://github.com/nodejs/node/pull/35779)
* \[[`427968d266`](https://github.com/nodejs/node/commit/427968d266)] - **deps**: upgrade npm to 6.14.11 (Ruy Adorno) [#37173](https://github.com/nodejs/node/pull/37173)
* \[[`cd9a8106be`](https://github.com/nodejs/node/commit/cd9a8106be)] - **http**: do not loop over prototype in Agent (Michaël Zasso) [#36410](https://github.com/nodejs/node/pull/36410)
* \[[`4ac8f37800`](https://github.com/nodejs/node/commit/4ac8f37800)] - **http2**: check write not scheduled in scope destructor (David Halls) [#36241](https://github.com/nodejs/node/pull/36241)

<a id="12.20.1"></a>

## 2021-01-04, Version 12.20.1 'Erbium' (LTS), @richardlau

### Notable changes

This is a security release.

Vulnerabilities fixed:

* **CVE-2020-8265**: use-after-free in TLSWrap (High)
  Affected Node.js versions are vulnerable to a use-after-free bug in its
  TLS implementation. When writing to a TLS enabled socket,
  node::StreamBase::Write calls node::TLSWrap::DoWrite with a freshly
  allocated WriteWrap object as first argument. If the DoWrite method does
  not return an error, this object is passed back to the caller as part of
  a StreamWriteResult structure. This may be exploited to corrupt memory
  leading to a Denial of Service or potentially other exploits
* **CVE-2020-8287**: HTTP Request Smuggling in nodejs
  Affected versions of Node.js allow two copies of a header field in a
  http request. For example, two Transfer-Encoding header fields. In this
  case Node.js identifies the first header field and ignores the second.
  This can lead to HTTP Request Smuggling
  (<https://cwe.mitre.org/data/definitions/444.html>).
* **CVE-2020-1971**: OpenSSL - EDIPARTYNAME NULL pointer de-reference (High)
  This is a vulnerability in OpenSSL which may be exploited through Node.js.
  You can read more about it in
  <https://www.openssl.org/news/secadv/20201208.txt>

### Commits

* \[[`5de5354918`](https://github.com/nodejs/node/commit/5de5354918)] - **deps**: update http-parser to http-parser\@ec8b5ee63f (Richard Lau) [nodejs-private/node-private#236](https://github.com/nodejs-private/node-private/pull/236)
* \[[`2eacfbec68`](https://github.com/nodejs/node/commit/2eacfbec68)] - **deps**: upgrade npm to 6.14.10 (Ruy Adorno) [#36571](https://github.com/nodejs/node/pull/36571)
* \[[`96ec482d90`](https://github.com/nodejs/node/commit/96ec482d90)] - **deps**: update archs files for OpenSSL-1.1.1i (Myles Borins) [#36521](https://github.com/nodejs/node/pull/36521)
* \[[`7ec0eb408b`](https://github.com/nodejs/node/commit/7ec0eb408b)] - **deps**: upgrade openssl sources to 1.1.1i (Myles Borins) [#36521](https://github.com/nodejs/node/pull/36521)
* \[[`76ea9c5a7a`](https://github.com/nodejs/node/commit/76ea9c5a7a)] - **deps**: upgrade npm to 6.14.9 (Myles Borins) [#36450](https://github.com/nodejs/node/pull/36450)
* \[[`420244e4d9`](https://github.com/nodejs/node/commit/420244e4d9)] - **http**: unset `F_CHUNKED` on new `Transfer-Encoding` (Matteo Collina) [nodejs-private/node-private#236](https://github.com/nodejs-private/node-private/pull/236)
* \[[`4a30ac8c75`](https://github.com/nodejs/node/commit/4a30ac8c75)] - **http**: add test for http transfer encoding smuggling (Richard Lau) [nodejs-private/node-private#236](https://github.com/nodejs-private/node-private/pull/236)
* \[[`92d430917a`](https://github.com/nodejs/node/commit/92d430917a)] - **http**: unset `F_CHUNKED` on new `Transfer-Encoding` (Fedor Indutny) [nodejs-private/node-private#236](https://github.com/nodejs-private/node-private/pull/236)
* \[[`5b00de7d67`](https://github.com/nodejs/node/commit/5b00de7d67)] - **src**: retain pointers to WriteWrap/ShutdownWrap (James M Snell) [nodejs-private/node-private#230](https://github.com/nodejs-private/node-private/pull/230)

<a id="12.20.0"></a>

## 2020-11-24, Version 12.20.0 'Erbium' (LTS), @mylesborins

### Notable Changes

* **crypto**:
  * update certdata to NSS 3.56 (Shelley Vohr)
    <https://github.com/nodejs/node/pull/35546>
* **deps**:
  * update llhttp to 2.1.3 (Fedor Indutny)
    <https://github.com/nodejs/node/pull/35435>
  * (SEMVER-MINOR) upgrade to libuv 1.40.0 (Colin Ihrig)
    <https://github.com/nodejs/node/pull/35333>
* **doc**:
  * add aduh95 to collaborators (Antoine du Hamel)
    <https://github.com/nodejs/node/pull/35542>
* **fs**:
  * (SEMVER-MINOR) add .ref() and .unref() methods to watcher classes (rickyes)
    <https://github.com/nodejs/node/pull/33134>
* **http**:
  * (SEMVER-MINOR) added scheduling option to http agent (delvedor)
    <https://github.com/nodejs/node/pull/33278>
* **module**:
  * (SEMVER-MINOR) exports pattern support (Guy Bedford)
    <https://github.com/nodejs/node/pull/34718>
  * (SEMVER-MINOR) named exports for CJS via static analysis (Guy Bedford)
    <https://github.com/nodejs/node/pull/35249>
* **n-api**:
  * (SEMVER-MINOR) add more property defaults (Gerhard Stoebich)
    <https://github.com/nodejs/node/pull/35214>
* **src**:
  * (SEMVER-MINOR) move node\_contextify to modern THROW\_ERR\_\* (James M Snell)
    <https://github.com/nodejs/node/pull/35470>
  * (SEMVER-MINOR) move node\_process to modern THROW\_ERR\* (James M Snell)
    <https://github.com/nodejs/node/pull/35472>
  * (SEMVER-MINOR) expose v8::Isolate setup callbacks (Shelley Vohr)
    <https://github.com/nodejs/node/pull/35512>

### Commits

* \[[`c6eb0b62d9`](https://github.com/nodejs/node/commit/c6eb0b62d9)] - **benchmark**: ignore build artifacts for napi addons (Richard Lau) [#35970](https://github.com/nodejs/node/pull/35970)
* \[[`f3a045720c`](https://github.com/nodejs/node/commit/f3a045720c)] - **build**: fuzzer that targets node::LoadEnvironment() (davkor) [#34844](https://github.com/nodejs/node/pull/34844)
* \[[`48bc3fcd4c`](https://github.com/nodejs/node/commit/48bc3fcd4c)] - **build**: improved release lint error message (Shelley Vohr) [#35523](https://github.com/nodejs/node/pull/35523)
* \[[`2e766a6adf`](https://github.com/nodejs/node/commit/2e766a6adf)] - **console**: add Symbol.toStringTag property (Leko) [#35399](https://github.com/nodejs/node/pull/35399)
* \[[`90244362cc`](https://github.com/nodejs/node/commit/90244362cc)] - **crypto**: fix KeyObject garbage collection (Anna Henningsen) [#35481](https://github.com/nodejs/node/pull/35481)
* \[[`42f64eba89`](https://github.com/nodejs/node/commit/42f64eba89)] - **crypto**: update certdata to NSS 3.56 (Shelley Vohr) [#35546](https://github.com/nodejs/node/pull/35546)
* \[[`a6f58c0888`](https://github.com/nodejs/node/commit/a6f58c0888)] - **crypto**: set env values in KeyObject Deserialize method (ThakurKarthik) [#35416](https://github.com/nodejs/node/pull/35416)
* \[[`6539cf2725`](https://github.com/nodejs/node/commit/6539cf2725)] - **deps**: upgrade to cjs-module-lexer\@1.0.0 (Guy Bedford) [#35928](https://github.com/nodejs/node/pull/35928)
* \[[`bdcc77bdf4`](https://github.com/nodejs/node/commit/bdcc77bdf4)] - **deps**: update to cjs-module-lexer\@0.5.2 (Guy Bedford) [#35901](https://github.com/nodejs/node/pull/35901)
* \[[`5b8d3c74e8`](https://github.com/nodejs/node/commit/5b8d3c74e8)] - **deps**: upgrade to cjs-module-lexer\@0.5.0 (Guy Bedford) [#35871](https://github.com/nodejs/node/pull/35871)
* \[[`d7f0e3e5f0`](https://github.com/nodejs/node/commit/d7f0e3e5f0)] - **deps**: update to cjs-module-lexer\@0.4.3 (Guy Bedford) [#35745](https://github.com/nodejs/node/pull/35745)
* \[[`0a1474d9df`](https://github.com/nodejs/node/commit/0a1474d9df)] - **deps**: update llhttp to 2.1.3 (Fedor Indutny) [#35435](https://github.com/nodejs/node/pull/35435)
* \[[`cf07a8695a`](https://github.com/nodejs/node/commit/cf07a8695a)] - **deps**: upgrade to libuv 1.40.0 (Colin Ihrig) [#35333](https://github.com/nodejs/node/pull/35333)
* \[[`cc11464b4e`](https://github.com/nodejs/node/commit/cc11464b4e)] - **deps**: upgrade to c-ares v1.16.1 (Shelley Vohr) [#35324](https://github.com/nodejs/node/pull/35324)
* \[[`5405e62eaf`](https://github.com/nodejs/node/commit/5405e62eaf)] - **deps**: update to uvwasi 0.0.11 (Colin Ihrig) [#35104](https://github.com/nodejs/node/pull/35104)
* \[[`44c739cc49`](https://github.com/nodejs/node/commit/44c739cc49)] - **deps**: V8: cherry-pick 6be2f6e26e8d (Benjamin Coe) [#35055](https://github.com/nodejs/node/pull/35055)
* \[[`b78a1a186f`](https://github.com/nodejs/node/commit/b78a1a186f)] - **doc**: update releaser in v12.18.4 changelog (Beth Griggs) [#35217](https://github.com/nodejs/node/pull/35217)
* \[[`1cd1d0159d`](https://github.com/nodejs/node/commit/1cd1d0159d)] - **doc**: move package.import content higher (Myles Borins) [#35535](https://github.com/nodejs/node/pull/35535)
* \[[`79f3c323f6`](https://github.com/nodejs/node/commit/79f3c323f6)] - **doc**: fix broken links in modules.md (Rich Trott) [#35182](https://github.com/nodejs/node/pull/35182)
* \[[`b4941cfaec`](https://github.com/nodejs/node/commit/b4941cfaec)] - **doc**: make minor improvements to module.md (Rich Trott) [#35083](https://github.com/nodejs/node/pull/35083)
* \[[`7dc3b74c34`](https://github.com/nodejs/node/commit/7dc3b74c34)] - **doc**: add ESM examples in `module` API doc page (Antoine du HAMEL) [#34875](https://github.com/nodejs/node/pull/34875)
* \[[`f0b06b64ff`](https://github.com/nodejs/node/commit/f0b06b64ff)] - **doc**: move module core module doc to separate page (Antoine du HAMEL) [#34747](https://github.com/nodejs/node/pull/34747)
* \[[`77555d8500`](https://github.com/nodejs/node/commit/77555d8500)] - **doc**: put landing specifics in details tag (Rich Trott) [#35296](https://github.com/nodejs/node/pull/35296)
* \[[`b50b34b30e`](https://github.com/nodejs/node/commit/b50b34b30e)] - **doc**: put release script specifics in details (Myles Borins) [#35260](https://github.com/nodejs/node/pull/35260)
* \[[`1a8f3a844e`](https://github.com/nodejs/node/commit/1a8f3a844e)] - **doc**: copyedit esm.md (Rich Trott) [#35414](https://github.com/nodejs/node/pull/35414)
* \[[`d99120040c`](https://github.com/nodejs/node/commit/d99120040c)] - **doc**: error code fix in resolver spec (Guy Bedford) [#34998](https://github.com/nodejs/node/pull/34998)
* \[[`df52814113`](https://github.com/nodejs/node/commit/df52814113)] - **doc**: document Buffer.concat may use internal pool (Andrey Pechkurov) [#35541](https://github.com/nodejs/node/pull/35541)
* \[[`42a587f9ba`](https://github.com/nodejs/node/commit/42a587f9ba)] - **doc**: use test username instead of real (Pooja D.P) [#35611](https://github.com/nodejs/node/pull/35611)
* \[[`bfff4fc3c9`](https://github.com/nodejs/node/commit/bfff4fc3c9)] - **doc**: revise description of process.ppid (Pooja D.P) [#35589](https://github.com/nodejs/node/pull/35589)
* \[[`a9ac75480f`](https://github.com/nodejs/node/commit/a9ac75480f)] - **doc**: add symlink information for process.execpath (Pooja D.P) [#35590](https://github.com/nodejs/node/pull/35590)
* \[[`5fea51b66c`](https://github.com/nodejs/node/commit/5fea51b66c)] - **doc**: add PoojaDurgad as a triager (Pooja D.P) [#35153](https://github.com/nodejs/node/pull/35153)
* \[[`a0b541c3e0`](https://github.com/nodejs/node/commit/a0b541c3e0)] - **doc**: use kbd element in process doc (Rich Trott) [#35584](https://github.com/nodejs/node/pull/35584)
* \[[`992355cdf9`](https://github.com/nodejs/node/commit/992355cdf9)] - **doc**: simplify wording in tracing APIs doc (Pooja D.P) [#35556](https://github.com/nodejs/node/pull/35556)
* \[[`05db4b8343`](https://github.com/nodejs/node/commit/05db4b8343)] - **doc**: improve SIGINT error text (Rich Trott) [#35558](https://github.com/nodejs/node/pull/35558)
* \[[`42c479572c`](https://github.com/nodejs/node/commit/42c479572c)] - **doc**: use sentence case for class property (Rich Trott) [#35540](https://github.com/nodejs/node/pull/35540)
* \[[`fb9bb05ee2`](https://github.com/nodejs/node/commit/fb9bb05ee2)] - **doc**: fix util.inspect change history (Antoine du Hamel) [#35528](https://github.com/nodejs/node/pull/35528)
* \[[`6952c45202`](https://github.com/nodejs/node/commit/6952c45202)] - **doc**: add aduh95 to collaborators (Antoine du Hamel) [#35542](https://github.com/nodejs/node/pull/35542)
* \[[`b5f752528b`](https://github.com/nodejs/node/commit/b5f752528b)] - **doc**: update AUTHORS list (Anna Henningsen) [#35280](https://github.com/nodejs/node/pull/35280)
* \[[`370f8e3afd`](https://github.com/nodejs/node/commit/370f8e3afd)] - **doc**: update sxa's email address to Red Hat from IBM (Stewart X Addison) [#35442](https://github.com/nodejs/node/pull/35442)
* \[[`edf3fbbd14`](https://github.com/nodejs/node/commit/edf3fbbd14)] - **doc**: update contact information for @BethGriggs (Beth Griggs) [#35451](https://github.com/nodejs/node/pull/35451)
* \[[`8be289e58c`](https://github.com/nodejs/node/commit/8be289e58c)] - **doc**: update contact information for richardlau (Richard Lau) [#35450](https://github.com/nodejs/node/pull/35450)
* \[[`42c0dfcc23`](https://github.com/nodejs/node/commit/42c0dfcc23)] - **doc**: importable node protocol URLs (Bradley Meck) [#35434](https://github.com/nodejs/node/pull/35434)
* \[[`c192af66e7`](https://github.com/nodejs/node/commit/c192af66e7)] - **doc**: unhide resolver spec (Guy Bedford) [#35358](https://github.com/nodejs/node/pull/35358)
* \[[`b0e43c718c`](https://github.com/nodejs/node/commit/b0e43c718c)] - **doc**: add gpg key export directions to releases doc (Danielle Adams) [#35298](https://github.com/nodejs/node/pull/35298)
* \[[`884755f1e5`](https://github.com/nodejs/node/commit/884755f1e5)] - **doc**: simplify circular dependencies text in modules.md (Rich Trott) [#35126](https://github.com/nodejs/node/pull/35126)
* \[[`85c47d753c`](https://github.com/nodejs/node/commit/85c47d753c)] - **doc**: avoid double-while sentence in perf\_hooks.md (Rich Trott) [#35078](https://github.com/nodejs/node/pull/35078)
* \[[`68c5ee45a2`](https://github.com/nodejs/node/commit/68c5ee45a2)] - **doc**: update fs promise-based examples (Richard Lau) [#35760](https://github.com/nodejs/node/pull/35760)
* \[[`66f8730441`](https://github.com/nodejs/node/commit/66f8730441)] - **doc**: add history entry for exports patterns (Antoine du Hamel) [#35410](https://github.com/nodejs/node/pull/35410)
* \[[`a7e66b635d`](https://github.com/nodejs/node/commit/a7e66b635d)] - **doc**: fix conditional exports flag removal version (Antoine du Hamel) [#35428](https://github.com/nodejs/node/pull/35428)
* \[[`9197a6651d`](https://github.com/nodejs/node/commit/9197a6651d)] - **doc**: copyedit packages.md (Rich Trott) [#35427](https://github.com/nodejs/node/pull/35427)
* \[[`f507ca9e21`](https://github.com/nodejs/node/commit/f507ca9e21)] - **doc**: packages docs feedback (Guy Bedford) [#35370](https://github.com/nodejs/node/pull/35370)
* \[[`5330930128`](https://github.com/nodejs/node/commit/5330930128)] - **doc**: refine require/import conditions constraints (Guy Bedford) [#35311](https://github.com/nodejs/node/pull/35311)
* \[[`5f0b1571a7`](https://github.com/nodejs/node/commit/5f0b1571a7)] - **doc**: edit subpath export patterns introduction (Rich Trott) [#35254](https://github.com/nodejs/node/pull/35254)
* \[[`d6a13a947e`](https://github.com/nodejs/node/commit/d6a13a947e)] - **doc**: document support for package.json fields (Antoine du HAMEL) [#34970](https://github.com/nodejs/node/pull/34970)
* \[[`7c1700e143`](https://github.com/nodejs/node/commit/7c1700e143)] - **doc**: move package config docs to separate page (Antoine du HAMEL) [#34748](https://github.com/nodejs/node/pull/34748)
* \[[`7510667d87`](https://github.com/nodejs/node/commit/7510667d87)] - **doc**: rename module pages (Antoine du HAMEL) [#34663](https://github.com/nodejs/node/pull/34663)
* \[[`b644ab6ae6`](https://github.com/nodejs/node/commit/b644ab6ae6)] - **doc**: fix line length in worker\_threads.md (Jucke) [#34419](https://github.com/nodejs/node/pull/34419)
* \[[`fb9b66bdd7`](https://github.com/nodejs/node/commit/fb9b66bdd7)] - **doc**: fix typos in n-api, tls and worker\_threads (Jucke) [#34419](https://github.com/nodejs/node/pull/34419)
* \[[`1f34230373`](https://github.com/nodejs/node/commit/1f34230373)] - **doc,esm**: document experimental warning removal (Antoine du Hamel) [#35750](https://github.com/nodejs/node/pull/35750)
* \[[`985b96a7d5`](https://github.com/nodejs/node/commit/985b96a7d5)] - **doc,esm**: add history support info (Antoine du Hamel) [#35395](https://github.com/nodejs/node/pull/35395)
* \[[`548137f4ec`](https://github.com/nodejs/node/commit/548137f4ec)] - **errors**: simplify ERR\_REQUIRE\_ESM message generation (Rich Trott) [#35123](https://github.com/nodejs/node/pull/35123)
* \[[`f22672de18`](https://github.com/nodejs/node/commit/f22672de18)] - **errors**: improve ERR\_INVALID\_OPT\_VALUE error (Denys Otrishko) [#34671](https://github.com/nodejs/node/pull/34671)
* \[[`7a98961a26`](https://github.com/nodejs/node/commit/7a98961a26)] - **esm**: fix hook mistypes and links to types (Derek Lewis) [#34240](https://github.com/nodejs/node/pull/34240)
* \[[`0f757bc2df`](https://github.com/nodejs/node/commit/0f757bc2df)] - **esm**: use "node:" namespace for builtins (Guy Bedford) [#35387](https://github.com/nodejs/node/pull/35387)
* \[[`b48473228c`](https://github.com/nodejs/node/commit/b48473228c)] - **events**: assume an EventEmitter if emitter.on is a function (Luigi Pinca) [#35818](https://github.com/nodejs/node/pull/35818)
* \[[`19d711391e`](https://github.com/nodejs/node/commit/19d711391e)] - **fs**: simplify realpathSync (himself65) [#35413](https://github.com/nodejs/node/pull/35413)
* \[[`decfc2ae5c`](https://github.com/nodejs/node/commit/decfc2ae5c)] - **fs**: add .ref() and .unref() methods to watcher classes (rickyes) [#33134](https://github.com/nodejs/node/pull/33134)
* \[[`cce464513e`](https://github.com/nodejs/node/commit/cce464513e)] - **http**: added scheduling option to http agent (delvedor) [#33278](https://github.com/nodejs/node/pull/33278)
* \[[`d477e2e147`](https://github.com/nodejs/node/commit/d477e2e147)] - **http**: only set keep-alive when not exists (<atian25@qq.com>) [#35138](https://github.com/nodejs/node/pull/35138)
* \[[`f10d721737`](https://github.com/nodejs/node/commit/f10d721737)] - **http**: reset headers timeout on headers complete (Robert Nagy) [#34578](https://github.com/nodejs/node/pull/34578)
* \[[`c8a778985b`](https://github.com/nodejs/node/commit/c8a778985b)] - **http2**: avoid unnecessary buffer resize (Denys Otrishko) [#34480](https://github.com/nodejs/node/pull/34480)
* \[[`b732c92e3d`](https://github.com/nodejs/node/commit/b732c92e3d)] - **http2**: use and support non-empty DATA frame with END\_STREAM flag (Carlos Lopez) [#33875](https://github.com/nodejs/node/pull/33875)
* \[[`bfce0eb13a`](https://github.com/nodejs/node/commit/bfce0eb13a)] - _**Revert**_ "**http2**: streamline OnStreamRead streamline memory accounting" (Rich Trott) [#34315](https://github.com/nodejs/node/pull/34315)
* \[[`e85ca7af43`](https://github.com/nodejs/node/commit/e85ca7af43)] - **http2**: wait for session socket writable end on close/destroy (Denys Otrishko) [#30854](https://github.com/nodejs/node/pull/30854)
* \[[`2471197099`](https://github.com/nodejs/node/commit/2471197099)] - **http2**: wait for session to finish writing before destroy (Denys Otrishko) [#30854](https://github.com/nodejs/node/pull/30854)
* \[[`82af8acc8e`](https://github.com/nodejs/node/commit/82af8acc8e)] - **http2,doc**: minor fixes (Alba Mendez) [#28044](https://github.com/nodejs/node/pull/28044)
* \[[`a3e8829d4a`](https://github.com/nodejs/node/commit/a3e8829d4a)] - **inspector**: do not hardcode Debugger.CallFrameId in tests (Dmitry Gozman) [#35570](https://github.com/nodejs/node/pull/35570)
* \[[`6efa140f8f`](https://github.com/nodejs/node/commit/6efa140f8f)] - **lib**: change http client path assignment (Yohanan Baruchel) [#35508](https://github.com/nodejs/node/pull/35508)
* \[[`ad7281b081`](https://github.com/nodejs/node/commit/ad7281b081)] - **lib**: use remaining typed arrays from primordials (Michaël Zasso) [#35499](https://github.com/nodejs/node/pull/35499)
* \[[`a9a606f06b`](https://github.com/nodejs/node/commit/a9a606f06b)] - **lib**: use full URL to GitHub issues in comments (Rich Trott) [#34686](https://github.com/nodejs/node/pull/34686)
* \[[`ea239392c2`](https://github.com/nodejs/node/commit/ea239392c2)] - **module**: cjs-module-lexer\@0.4.1 big endian fix (Guy Bedford) [#35634](https://github.com/nodejs/node/pull/35634)
* \[[`354f358c1b`](https://github.com/nodejs/node/commit/354f358c1b)] - **module**: use Wasm CJS lexer when available (Guy Bedford) [#35583](https://github.com/nodejs/node/pull/35583)
* \[[`76f76017bf`](https://github.com/nodejs/node/commit/76f76017bf)] - **module**: fix builtin reexport tracing (Guy Bedford) [#35500](https://github.com/nodejs/node/pull/35500)
* \[[`992af4e112`](https://github.com/nodejs/node/commit/992af4e112)] - **module**: fix specifier resolution option value (himself65) [#35098](https://github.com/nodejs/node/pull/35098)
* \[[`1ff956f49e`](https://github.com/nodejs/node/commit/1ff956f49e)] - **module**: remove experimental modules warning (Guy Bedford) [#31974](https://github.com/nodejs/node/pull/31974)
* \[[`41af927efb`](https://github.com/nodejs/node/commit/41af927efb)] - **module**: exports pattern support (Guy Bedford) [#34718](https://github.com/nodejs/node/pull/34718)
* \[[`a18d0df33a`](https://github.com/nodejs/node/commit/a18d0df33a)] - **module**: update to cjs-module-lexer\@0.4.0 (Guy Bedford) [#35501](https://github.com/nodejs/node/pull/35501)
* \[[`6ca8fb552d`](https://github.com/nodejs/node/commit/6ca8fb552d)] - **module**: refine module type mismatch error cases (Guy Bedford) [#35426](https://github.com/nodejs/node/pull/35426)
* \[[`9eb1fa1924`](https://github.com/nodejs/node/commit/9eb1fa1924)] - **module**: named exports for CJS via static analysis (Guy Bedford) [#35249](https://github.com/nodejs/node/pull/35249)
* \[[`a93ca2d494`](https://github.com/nodejs/node/commit/a93ca2d494)] - **n-api**: revert change to finalization (Michael Dawson) [#35777](https://github.com/nodejs/node/pull/35777)
* \[[`5faaa603d8`](https://github.com/nodejs/node/commit/5faaa603d8)] - **n-api**: support for object freeze/seal (Shelley Vohr) [#35359](https://github.com/nodejs/node/pull/35359)
* \[[`d938e8508b`](https://github.com/nodejs/node/commit/d938e8508b)] - **n-api**: add more property defaults (Gerhard Stoebich) [#35214](https://github.com/nodejs/node/pull/35214)
* \[[`18f01ddcb5`](https://github.com/nodejs/node/commit/18f01ddcb5)] - **repl**: improve static import error message in repl (Myles Borins) [#33588](https://github.com/nodejs/node/pull/33588)
* \[[`70768ce109`](https://github.com/nodejs/node/commit/70768ce109)] - **repl**: give repl entries unique names (Bradley Meck) [#34372](https://github.com/nodejs/node/pull/34372)
* \[[`e9bee3950c`](https://github.com/nodejs/node/commit/e9bee3950c)] - **src**: move node\_contextify to modern THROW\_ERR\_\* (James M Snell) [#35470](https://github.com/nodejs/node/pull/35470)
* \[[`b741f2ff84`](https://github.com/nodejs/node/commit/b741f2ff84)] - **src**: move node\_process to modern THROW\_ERR\* (James M Snell) [#35472](https://github.com/nodejs/node/pull/35472)
* \[[`2d5393bb28`](https://github.com/nodejs/node/commit/2d5393bb28)] - **src**: fix freeing unintialized pointer bug in ParseSoaReply (Aastha Gupta) [#35502](https://github.com/nodejs/node/pull/35502)
* \[[`dec004f742`](https://github.com/nodejs/node/commit/dec004f742)] - **src**: expose v8::Isolate setup callbacks (Shelley Vohr) [#35512](https://github.com/nodejs/node/pull/35512)
* \[[`7f8834f76c`](https://github.com/nodejs/node/commit/7f8834f76c)] - **src**: more idiomatic error pattern in node\_wasi (James M Snell) [#35493](https://github.com/nodejs/node/pull/35493)
* \[[`ade27b732b`](https://github.com/nodejs/node/commit/ade27b732b)] - **src**: use env->ThrowUVException in pipe\_wrap (James M Snell) [#35493](https://github.com/nodejs/node/pull/35493)
* \[[`e70b05208f`](https://github.com/nodejs/node/commit/e70b05208f)] - **src**: remove invalid ToLocalChecked in EmitBeforeExit (Anna Henningsen) [#35484](https://github.com/nodejs/node/pull/35484)
* \[[`cd80195524`](https://github.com/nodejs/node/commit/cd80195524)] - **src**: make MakeCallback() check can\_call\_into\_js before getting method (Anna Henningsen) [#35424](https://github.com/nodejs/node/pull/35424)
* \[[`8a1091648c`](https://github.com/nodejs/node/commit/8a1091648c)] - **stream**: destroy wrapped streams on error (Robert Nagy) [#34102](https://github.com/nodejs/node/pull/34102)
* \[[`fdc67ebf5f`](https://github.com/nodejs/node/commit/fdc67ebf5f)] - **test**: replace annonymous functions with arrow (Pooja D.P) [#34921](https://github.com/nodejs/node/pull/34921)
* \[[`c3e1bf78c4`](https://github.com/nodejs/node/commit/c3e1bf78c4)] - **test**: add wasi readdir() test (Colin Ihrig) [#35202](https://github.com/nodejs/node/pull/35202)
* \[[`607f3c5d84`](https://github.com/nodejs/node/commit/607f3c5d84)] - **test**: fix comment about DNS lookup test (Tobias Nießen) [#35080](https://github.com/nodejs/node/pull/35080)
* \[[`02787ce5d1`](https://github.com/nodejs/node/commit/02787ce5d1)] - **test**: add ALPNProtocols option to clientOptions (Luigi Pinca) [#35482](https://github.com/nodejs/node/pull/35482)
* \[[`12d76b8e8e`](https://github.com/nodejs/node/commit/12d76b8e8e)] - **tls**: reset secureConnecting on client socket (David Halls) [#33209](https://github.com/nodejs/node/pull/33209)
* \[[`adf4f90bce`](https://github.com/nodejs/node/commit/adf4f90bce)] - **tools**: refloat 7 Node.js patches to cpplint.py (Rich Trott) [#35569](https://github.com/nodejs/node/pull/35569)
* \[[`1173efca27`](https://github.com/nodejs/node/commit/1173efca27)] - **tools**: bump cpplint.py to 1.4.6 (Rich Trott) [#35569](https://github.com/nodejs/node/pull/35569)
* \[[`09552670fe`](https://github.com/nodejs/node/commit/09552670fe)] - **tools**: add missing uv\_setup\_argv() calls (Anna Henningsen) [#35491](https://github.com/nodejs/node/pull/35491)
* \[[`ae149232a1`](https://github.com/nodejs/node/commit/ae149232a1)] - **tools**: exclude gyp from markdown link checker (Michaël Zasso) [#35423](https://github.com/nodejs/node/pull/35423)
* \[[`a9ce9b2614`](https://github.com/nodejs/node/commit/a9ce9b2614)] - **tools**: update ESLint to 7.10.0 (Colin Ihrig) [#35366](https://github.com/nodejs/node/pull/35366)
* \[[`bc7da0c22c`](https://github.com/nodejs/node/commit/bc7da0c22c)] - **tools**: ignore build folder when checking links (Ash Cripps) [#35315](https://github.com/nodejs/node/pull/35315)
* \[[`f29717437f`](https://github.com/nodejs/node/commit/f29717437f)] - **tools,doc**: enforce alphabetical order for md refs (Antoine du Hamel) [#35244](https://github.com/nodejs/node/pull/35244)
* \[[`11b10d7d1f`](https://github.com/nodejs/node/commit/11b10d7d1f)] - **tools,doc**: upgrade dependencies (Antoine du Hamel) [#35244](https://github.com/nodejs/node/pull/35244)

<a id="12.19.1"></a>

## 2020-11-16, Version 12.19.1 'Erbium' (LTS), @BethGriggs

### Notable changes

This is a security release.

Vulnerabilities fixed:

* **CVE-2020-8277**: Denial of Service through DNS request (High). A Node.js application that allows an attacker to trigger a DNS request for a host of their choice could trigger a Denial of Service by getting the application to resolve a DNS record with a larger number of responses.

### Commits

* \[[`022899e1d5`](https://github.com/nodejs/node/commit/022899e1d5)] - **deps**: cherry-pick 0d252eb from upstream c-ares (Michael Dawson) [nodejs-private/node-private#231](https://github.com/nodejs-private/node-private/pull/231)

<a id="12.19.0"></a>

## 2020-10-06, Version 12.19.0 'Erbium' (LTS), @codebytere

### Notable Changes

* \[[`d065334d42`](https://github.com/nodejs/node/commit/d065334d42)] - **(SEMVER-MINOR)** **module**: package "imports" field (Guy Bedford) [#34117](https://github.com/nodejs/node/pull/34117)
* \[[`b9d0f73c7c`](https://github.com/nodejs/node/commit/b9d0f73c7c)] - **(SEMVER-MINOR)** **n-api**: create N-API version 7 (Gabriel Schulhof) [#35199](https://github.com/nodejs/node/pull/35199)
* \[[`53c9975673`](https://github.com/nodejs/node/commit/53c9975673)] - **(SEMVER-MINOR)** **crypto**: add randomInt function (Oli Lalonde) [#34600](https://github.com/nodejs/node/pull/34600)
* \[[`9b53b4ddf2`](https://github.com/nodejs/node/commit/9b53b4ddf2)] - **deps**: upgrade to libuv 1.39.0 (Colin Ihrig) [#34915](https://github.com/nodejs/node/pull/34915)
* \[[`e9a8f0c127`](https://github.com/nodejs/node/commit/e9a8f0c127)] - **doc**: add Ricky Zhou to collaborators (rickyes) [#34676](https://github.com/nodejs/node/pull/34676)
* \[[`260914c432`](https://github.com/nodejs/node/commit/260914c432)] - **doc**: add release key for Ruy Adorno (Ruy Adorno) [#34628](https://github.com/nodejs/node/pull/34628)
* \[[`39f90346f8`](https://github.com/nodejs/node/commit/39f90346f8)] - **doc**: add DerekNonGeneric to collaborators (Derek Lewis) [#34602](https://github.com/nodejs/node/pull/34602)
* \[[`7ef1f6a71d`](https://github.com/nodejs/node/commit/7ef1f6a71d)] - **deps**: upgrade npm to 6.14.7 (claudiahdz) [#34468](https://github.com/nodejs/node/pull/34468)
* \[[`437b092eed`](https://github.com/nodejs/node/commit/437b092eed)] - **doc**: add AshCripps to collaborators (Ash Cripps) [#34494](https://github.com/nodejs/node/pull/34494)
* \[[`319d570a47`](https://github.com/nodejs/node/commit/319d570a47)] - **doc**: add HarshithaKP to collaborators (Harshitha K P) [#34417](https://github.com/nodejs/node/pull/34417)
* \[[`d60b13f2e3`](https://github.com/nodejs/node/commit/d60b13f2e3)] - **zlib**: switch to lazy init for zlib streams (Andrey Pechkurov) [#34048](https://github.com/nodejs/node/pull/34048)
* \[[`ae60f50a69`](https://github.com/nodejs/node/commit/ae60f50a69)] - **doc**: add rexagod to collaborators (Pranshu Srivastava) [#34457](https://github.com/nodejs/node/pull/34457)
* \[[`39dea8f70d`](https://github.com/nodejs/node/commit/39dea8f70d)] - **doc**: add release key for Richard Lau (Richard Lau) [#34397](https://github.com/nodejs/node/pull/34397)
* \[[`a2107101be`](https://github.com/nodejs/node/commit/a2107101be)] - **doc**: add danielleadams to collaborators (Danielle Adams) [#34360](https://github.com/nodejs/node/pull/34360)
* \[[`c4f0cb65a1`](https://github.com/nodejs/node/commit/c4f0cb65a1)] - **doc**: add sxa as collaborator (Stewart X Addison) [#34338](https://github.com/nodejs/node/pull/34338)
* \[[`e9a514d13e`](https://github.com/nodejs/node/commit/e9a514d13e)] - **deps**: upgrade to libuv 1.38.1 (Colin Ihrig) [#34187](https://github.com/nodejs/node/pull/34187)
* \[[`a04d76d2ad`](https://github.com/nodejs/node/commit/a04d76d2ad)] - **doc**: add ruyadorno to collaborators (Ruy Adorno) [#34297](https://github.com/nodejs/node/pull/34297)
* \[[`c9bd1a7d8a`](https://github.com/nodejs/node/commit/c9bd1a7d8a)] - **(SEMVER-MINOR)** **module**: deprecate module.parent (Antoine du HAMEL) [#32217](https://github.com/nodejs/node/pull/32217)
* \[[`0a927216cf`](https://github.com/nodejs/node/commit/0a927216cf)] - **(SEMVER-MAJOR)** **doc**: deprecate process.umask() with no arguments (Colin Ihrig) [#32499](https://github.com/nodejs/node/pull/32499)

### Commits

* \[[`27ceec0bc6`](https://github.com/nodejs/node/commit/27ceec0bc6)] - Forces Powershell to use tls1.2 (Bartosz Sosnowski) [#33609](https://github.com/nodejs/node/pull/33609)
* \[[`d73b8346b8`](https://github.com/nodejs/node/commit/d73b8346b8)] - **(SEMVER-MINOR)** **assert**: port common.mustCall() to assert (ConorDavenport) [#31982](https://github.com/nodejs/node/pull/31982)
* \[[`148383fdc3`](https://github.com/nodejs/node/commit/148383fdc3)] - **async\_hooks**: avoid GC tracking of AsyncResource in ALS (Gerhard Stoebich) [#34653](https://github.com/nodejs/node/pull/34653)
* \[[`0a4401713a`](https://github.com/nodejs/node/commit/0a4401713a)] - **async\_hooks**: avoid unneeded AsyncResource creation (Gerhard Stoebich) [#34616](https://github.com/nodejs/node/pull/34616)
* \[[`07968ac456`](https://github.com/nodejs/node/commit/07968ac456)] - **async\_hooks**: improve property descriptors in als.bind (Gerhard Stoebich) [#34620](https://github.com/nodejs/node/pull/34620)
* \[[`45d2f4dd3c`](https://github.com/nodejs/node/commit/45d2f4dd3c)] - **(SEMVER-MINOR)** **async\_hooks**: add AsyncResource.bind utility (James M Snell) [#34574](https://github.com/nodejs/node/pull/34574)
* \[[`61683e1763`](https://github.com/nodejs/node/commit/61683e1763)] - **async\_hooks**: don't read resource if ALS is disabled (Gerhard Stoebich) [#34617](https://github.com/nodejs/node/pull/34617)
* \[[`95e0f8ef52`](https://github.com/nodejs/node/commit/95e0f8ef52)] - **async\_hooks**: execute destroy hooks earlier (Gerhard Stoebich) [#34342](https://github.com/nodejs/node/pull/34342)
* \[[`cfc769b048`](https://github.com/nodejs/node/commit/cfc769b048)] - **async\_hooks**: fix resource stack for deep stacks (Anna Henningsen) [#34573](https://github.com/nodejs/node/pull/34573)
* \[[`b2241e9fc1`](https://github.com/nodejs/node/commit/b2241e9fc1)] - **async\_hooks**: improve resource stack performance (Anna Henningsen) [#34319](https://github.com/nodejs/node/pull/34319)
* \[[`24fddba59b`](https://github.com/nodejs/node/commit/24fddba59b)] - **benchmark**: add benchmark script for resourceUsage (Yash Ladha) [#34691](https://github.com/nodejs/node/pull/34691)
* \[[`145691b83e`](https://github.com/nodejs/node/commit/145691b83e)] - **benchmark**: always throw the same Error instance (Anna Henningsen) [#34523](https://github.com/nodejs/node/pull/34523)
* \[[`7bc26c2e8c`](https://github.com/nodejs/node/commit/7bc26c2e8c)] - **bootstrap**: correct --frozen-intrinsics override fix (Guy Bedford) [#35041](https://github.com/nodejs/node/pull/35041)
* \[[`6ee800f0c3`](https://github.com/nodejs/node/commit/6ee800f0c3)] - **(SEMVER-MINOR)** **buffer**: also alias BigUInt methods (Anna Henningsen) [#34960](https://github.com/nodejs/node/pull/34960)
* \[[`9d07217d2c`](https://github.com/nodejs/node/commit/9d07217d2c)] - **(SEMVER-MINOR)** **buffer**: alias UInt ➡️ Uint in buffer methods (Anna Henningsen) [#34729](https://github.com/nodejs/node/pull/34729)
* \[[`8f2d2aa9e3`](https://github.com/nodejs/node/commit/8f2d2aa9e3)] - **build**: increase API requests for stale action (Phillip Johnsen) [#35235](https://github.com/nodejs/node/pull/35235)
* \[[`ff0b1000d1`](https://github.com/nodejs/node/commit/ff0b1000d1)] - **build**: filter issues & PRs to auto close by matching on stalled label (Phillip Johnsen) [#35159](https://github.com/nodejs/node/pull/35159)
* \[[`06c5120eef`](https://github.com/nodejs/node/commit/06c5120eef)] - **(SEMVER-MINOR)** **build**: add build flag for OSS-Fuzz integration (davkor) [#34761](https://github.com/nodejs/node/pull/34761)
* \[[`9107595acd`](https://github.com/nodejs/node/commit/9107595acd)] - **build**: comment about auto close when stalled via with github action (Phillip Johnsen) [#34555](https://github.com/nodejs/node/pull/34555)
* \[[`60774c08e3`](https://github.com/nodejs/node/commit/60774c08e3)] - **build**: close stalled issues and PRs with github action (Phillip Johnsen) [#34555](https://github.com/nodejs/node/pull/34555)
* \[[`9bb681458c`](https://github.com/nodejs/node/commit/9bb681458c)] - **build**: use autorebase option for git node land (Denys Otrishko) [#34969](https://github.com/nodejs/node/pull/34969)
* \[[`8d27998bd6`](https://github.com/nodejs/node/commit/8d27998bd6)] - **build**: use latest node-core-utils from npm (Denys Otrishko) [#34969](https://github.com/nodejs/node/pull/34969)
* \[[`d2f44a74f8`](https://github.com/nodejs/node/commit/d2f44a74f8)] - **build**: add support for build on arm64 (Evan Lucas) [#34238](https://github.com/nodejs/node/pull/34238)
* \[[`ea56aea452`](https://github.com/nodejs/node/commit/ea56aea452)] - **build**: run link checker in linter workflow (Richard Lau) [#34810](https://github.com/nodejs/node/pull/34810)
* \[[`9e1f8fcb65`](https://github.com/nodejs/node/commit/9e1f8fcb65)] - **build**: implement a Commit Queue in Actions (Mary Marchini) [#34112](https://github.com/nodejs/node/pull/34112)
* \[[`380600dbe5`](https://github.com/nodejs/node/commit/380600dbe5)] - **build**: set --v8-enable-object-print by default (Mary Marchini) [#34705](https://github.com/nodejs/node/pull/34705)
* \[[`191d0ae311`](https://github.com/nodejs/node/commit/191d0ae311)] - **build**: add flag to build V8 with OBJECT\_PRINT (Mary Marchini) [#32834](https://github.com/nodejs/node/pull/32834)
* \[[`f6ad59b60f`](https://github.com/nodejs/node/commit/f6ad59b60f)] - **build**: do not run auto-start-ci on forks (Evan Lucas) [#34650](https://github.com/nodejs/node/pull/34650)
* \[[`90a44e198b`](https://github.com/nodejs/node/commit/90a44e198b)] - **build**: increase startCI verbosity and fix job name (Mary Marchini) [#34635](https://github.com/nodejs/node/pull/34635)
* \[[`7886e763f5`](https://github.com/nodejs/node/commit/7886e763f5)] - **build**: don't run auto-start-ci on push (Mary Marchini) [#34588](https://github.com/nodejs/node/pull/34588)
* \[[`544a722de4`](https://github.com/nodejs/node/commit/544a722de4)] - **build**: fix auto-start-ci script path (Mary Marchini) [#34588](https://github.com/nodejs/node/pull/34588)
* \[[`e51b2680a8`](https://github.com/nodejs/node/commit/e51b2680a8)] - **build**: auto start Jenkins CI via PR labels (Mary Marchini) [#34089](https://github.com/nodejs/node/pull/34089)
* \[[`343894f990`](https://github.com/nodejs/node/commit/343894f990)] - **build**: toolchain.gypi and node\_gyp.py cleanup (iandrc) [#34268](https://github.com/nodejs/node/pull/34268)
* \[[`e7252df0b9`](https://github.com/nodejs/node/commit/e7252df0b9)] - **build**: fix test-ci-js task in Makefile (Rich Trott) [#34433](https://github.com/nodejs/node/pull/34433)
* \[[`833474f844`](https://github.com/nodejs/node/commit/833474f844)] - **build**: do not run benchmark tests on 'make test' (Rich Trott) [#34434](https://github.com/nodejs/node/pull/34434)
* \[[`f14775e492`](https://github.com/nodejs/node/commit/f14775e492)] - **build**: add benchmark tests to CI runs (Rich Trott) [#34288](https://github.com/nodejs/node/pull/34288)
* \[[`acf63b009d`](https://github.com/nodejs/node/commit/acf63b009d)] - **build,deps**: add gen-openssl target (Evan Lucas) [#34642](https://github.com/nodejs/node/pull/34642)
* \[[`b977672edc`](https://github.com/nodejs/node/commit/b977672edc)] - **build,tools**: fix cmd\_regen\_makefile (Daniel Bevenius) [#34255](https://github.com/nodejs/node/pull/34255)
* \[[`17a098b9e6`](https://github.com/nodejs/node/commit/17a098b9e6)] - **(SEMVER-MINOR)** **cli**: add alias for report-directory to make it consistent (Ash Cripps) [#33587](https://github.com/nodejs/node/pull/33587)
* \[[`b329a95c01`](https://github.com/nodejs/node/commit/b329a95c01)] - **console**: document the behavior of console.assert() (iandrc) [#34501](https://github.com/nodejs/node/pull/34501)
* \[[`ed72d83802`](https://github.com/nodejs/node/commit/ed72d83802)] - **crypto**: simplify KeyObject constructor (Rich Trott) [#35064](https://github.com/nodejs/node/pull/35064)
* \[[`b828560908`](https://github.com/nodejs/node/commit/b828560908)] - **(SEMVER-MINOR)** **crypto**: allow KeyObjects in postMessage (Tobias Nießen) [#33360](https://github.com/nodejs/node/pull/33360)
* \[[`2b7273b2ad`](https://github.com/nodejs/node/commit/2b7273b2ad)] - **crypto**: improve invalid arg type message for randomInt() (Rich Trott) [#35089](https://github.com/nodejs/node/pull/35089)
* \[[`bf5a85b43a`](https://github.com/nodejs/node/commit/bf5a85b43a)] - **crypto**: improve randomInt out-of-range error message (Rich Trott) [#35088](https://github.com/nodejs/node/pull/35088)
* \[[`5ef9ee4254`](https://github.com/nodejs/node/commit/5ef9ee4254)] - **crypto**: fix randomInt range check (Tobias Nießen) [#35052](https://github.com/nodejs/node/pull/35052)
* \[[`921129c1d8`](https://github.com/nodejs/node/commit/921129c1d8)] - **crypto**: align parameter names with documentation (Rich Trott) [#35054](https://github.com/nodejs/node/pull/35054)
* \[[`53c9975673`](https://github.com/nodejs/node/commit/53c9975673)] - **(SEMVER-MINOR)** **crypto**: add randomInt function (Oli Lalonde) [#34600](https://github.com/nodejs/node/pull/34600)
* \[[`39dc4086fe`](https://github.com/nodejs/node/commit/39dc4086fe)] - **crypto**: avoid unitializing ECDH objects on error (Tobias Nießen) [#34302](https://github.com/nodejs/node/pull/34302)
* \[[`865f8e85c4`](https://github.com/nodejs/node/commit/865f8e85c4)] - **crypto**: add OP flag constants added in OpenSSL v1.1.1 (Mateusz Krawczuk) [#33929](https://github.com/nodejs/node/pull/33929)
* \[[`bf4e778e50`](https://github.com/nodejs/node/commit/bf4e778e50)] - **crypto**: move typechecking for timingSafeEqual into C++ (Anna Henningsen) [#34141](https://github.com/nodejs/node/pull/34141)
* \[[`4ff6c77e17`](https://github.com/nodejs/node/commit/4ff6c77e17)] - **deps**: V8: cherry-pick e06ace6b5cdb (Anna Henningsen) [#34673](https://github.com/nodejs/node/pull/34673)
* \[[`5db8b357ce`](https://github.com/nodejs/node/commit/5db8b357ce)] - **deps**: V8: cherry-pick eec10a2fd8fa (Stephen Belanger) [#33778](https://github.com/nodejs/node/pull/33778)
* \[[`e9e3390b18`](https://github.com/nodejs/node/commit/e9e3390b18)] - **deps**: V8: backport 3f071e3e7e15 (Milad Fa) [#35305](https://github.com/nodejs/node/pull/35305)
* \[[`57564eb86d`](https://github.com/nodejs/node/commit/57564eb86d)] - **deps**: V8: cherry-pick 71736859756b2bd0444bdb0a87a (Daniel Bevenius) [#35205](https://github.com/nodejs/node/pull/35205)
* \[[`481cced20e`](https://github.com/nodejs/node/commit/481cced20e)] - **deps**: update brotli to v1.0.9 (Anna Henningsen) [#34937](https://github.com/nodejs/node/pull/34937)
* \[[`f6c0b270e0`](https://github.com/nodejs/node/commit/f6c0b270e0)] - **deps**: add openssl support for arm64 (Evan Lucas) [#34238](https://github.com/nodejs/node/pull/34238)
* \[[`9b53b4ddf2`](https://github.com/nodejs/node/commit/9b53b4ddf2)] - **deps**: upgrade to libuv 1.39.0 (Colin Ihrig) [#34915](https://github.com/nodejs/node/pull/34915)
* \[[`f87b6c0f7c`](https://github.com/nodejs/node/commit/f87b6c0f7c)] - **deps**: upgrade npm to 6.14.8 (Ruy Adorno) [#34834](https://github.com/nodejs/node/pull/34834)
* \[[`f710dbf1b7`](https://github.com/nodejs/node/commit/f710dbf1b7)] - **deps**: update to uvwasi 0.0.10 (Colin Ihrig) [#34623](https://github.com/nodejs/node/pull/34623)
* \[[`7ef1f6a71d`](https://github.com/nodejs/node/commit/7ef1f6a71d)] - **deps**: upgrade npm to 6.14.7 (claudiahdz) [#34468](https://github.com/nodejs/node/pull/34468)
* \[[`e9a514d13e`](https://github.com/nodejs/node/commit/e9a514d13e)] - **deps**: upgrade to libuv 1.38.1 (Colin Ihrig) [#34187](https://github.com/nodejs/node/pull/34187)
* \[[`60b697de30`](https://github.com/nodejs/node/commit/60b697de30)] - **deps**: V8: cherry-pick 7889803e82d3 (Zhao Jiazhong) [#34214](https://github.com/nodejs/node/pull/34214)
* \[[`de174cd1bc`](https://github.com/nodejs/node/commit/de174cd1bc)] - **(SEMVER-MINOR)** **dgram**: add IPv6 scope id suffix to received udp6 dgrams (Pekka Nikander) [#14500](https://github.com/nodejs/node/pull/14500)
* \[[`be6aee9f53`](https://github.com/nodejs/node/commit/be6aee9f53)] - **(SEMVER-MINOR)** **dgram**: allow typed arrays in .send() (Sarat Addepalli) [#22413](https://github.com/nodejs/node/pull/22413)
* \[[`1a8669d6ec`](https://github.com/nodejs/node/commit/1a8669d6ec)] - **(SEMVER-MINOR)** **doc**: Add maxTotalSockets option to agent constructor (rickyes) [#33617](https://github.com/nodejs/node/pull/33617)
* \[[`05da376c05`](https://github.com/nodejs/node/commit/05da376c05)] - **doc**: remove errors that were never released (Rich Trott) [#34197](https://github.com/nodejs/node/pull/34197)
* \[[`831328bdb2`](https://github.com/nodejs/node/commit/831328bdb2)] - **doc**: add note about multiple sync events and once (James M Snell) [#34220](https://github.com/nodejs/node/pull/34220)
* \[[`a9f0fc9896`](https://github.com/nodejs/node/commit/a9f0fc9896)] - **doc**: document behavior for once(ee, 'error') (James M Snell) [#34225](https://github.com/nodejs/node/pull/34225)
* \[[`ed055c010d`](https://github.com/nodejs/node/commit/ed055c010d)] - **doc**: replace http to https of link urls (sapics) [#34158](https://github.com/nodejs/node/pull/34158)
* \[[`cef9921c74`](https://github.com/nodejs/node/commit/cef9921c74)] - **doc**: specify how fs.WriteStream/ReadStreams are created (James M Snell) [#34188](https://github.com/nodejs/node/pull/34188)
* \[[`4277d952c0`](https://github.com/nodejs/node/commit/4277d952c0)] - **doc**: mark assert.CallTracker experimental (Ruben Bridgewater) [#33124](https://github.com/nodejs/node/pull/33124)
* \[[`1a7082052f`](https://github.com/nodejs/node/commit/1a7082052f)] - **(SEMVER-MINOR)** **doc**: add basic embedding example documentation (Anna Henningsen) [#30467](https://github.com/nodejs/node/pull/30467)
* \[[`55dc7aaaa3`](https://github.com/nodejs/node/commit/55dc7aaaa3)] - **doc**: standardize on \_backward\_ (Rich Trott) [#35243](https://github.com/nodejs/node/pull/35243)
* \[[`746517aad5`](https://github.com/nodejs/node/commit/746517aad5)] - **doc**: revise stability section of values doc (Rich Trott) [#35242](https://github.com/nodejs/node/pull/35242)
* \[[`1018e520d6`](https://github.com/nodejs/node/commit/1018e520d6)] - **doc**: remove excessive formatting in dgram.md (Rich Trott) [#35234](https://github.com/nodejs/node/pull/35234)
* \[[`e026ce9b82`](https://github.com/nodejs/node/commit/e026ce9b82)] - **doc**: sort repl references in ASCII order (Rich Trott) [#35230](https://github.com/nodejs/node/pull/35230)
* \[[`6669effc4d`](https://github.com/nodejs/node/commit/6669effc4d)] - **doc**: clarify use of NAPI\_EXPERIMENTAL (Michael Dawson) [#35195](https://github.com/nodejs/node/pull/35195)
* \[[`89636e3257`](https://github.com/nodejs/node/commit/89636e3257)] - **doc**: update attributes used by n-api samples (#35220) (Gerhard Stoebich)
* \[[`e21d1cd58f`](https://github.com/nodejs/node/commit/e21d1cd58f)] - **doc**: add issue labels sections to release guide (Michaël Zasso) [#35224](https://github.com/nodejs/node/pull/35224)
* \[[`f050ecc3b1`](https://github.com/nodejs/node/commit/f050ecc3b1)] - **doc**: fix small grammatical issues in timers.md (Turner Jabbour) [#35203](https://github.com/nodejs/node/pull/35203)
* \[[`d81db1dcb9`](https://github.com/nodejs/node/commit/d81db1dcb9)] - **doc**: add technical values document (Michael Dawson) [#35145](https://github.com/nodejs/node/pull/35145)
* \[[`ee1bcdbe0d`](https://github.com/nodejs/node/commit/ee1bcdbe0d)] - **doc**: remove "end user" (Rich Trott) [#35200](https://github.com/nodejs/node/pull/35200)
* \[[`3ffaf66886`](https://github.com/nodejs/node/commit/3ffaf66886)] - **doc**: replace "you should do X" with "do X" (Rich Trott) [#35194](https://github.com/nodejs/node/pull/35194)
* \[[`c606ed761c`](https://github.com/nodejs/node/commit/c606ed761c)] - **doc**: fix missing word in dgram.md (Tom Atkinson) [#35231](https://github.com/nodejs/node/pull/35231)
* \[[`3094ace6b0`](https://github.com/nodejs/node/commit/3094ace6b0)] - **doc**: fix deprecation documentation inconsistencies (Antoine du HAMEL) [#35082](https://github.com/nodejs/node/pull/35082)
* \[[`2b86032728`](https://github.com/nodejs/node/commit/2b86032728)] - **doc**: fix broken link in crypto.md (Rich Trott) [#35181](https://github.com/nodejs/node/pull/35181)
* \[[`4af4a809c2`](https://github.com/nodejs/node/commit/4af4a809c2)] - **doc**: remove problematic auto-linking of curl man pages (Rich Trott) [#35174](https://github.com/nodejs/node/pull/35174)
* \[[`d94dac467b`](https://github.com/nodejs/node/commit/d94dac467b)] - **doc**: update process.release (schamberg97) [#35167](https://github.com/nodejs/node/pull/35167)
* \[[`52eba5b542`](https://github.com/nodejs/node/commit/52eba5b542)] - **doc**: add missing copyFile change history (Shelley Vohr) [#35056](https://github.com/nodejs/node/pull/35056)
* \[[`799fad73e9`](https://github.com/nodejs/node/commit/799fad73e9)] - **doc**: perform cleanup on security-release-process.md (Rich Trott) [#35154](https://github.com/nodejs/node/pull/35154)
* \[[`62436e6bab`](https://github.com/nodejs/node/commit/62436e6bab)] - **doc**: fix minor punctuation issue in path.md (Amila Welihinda) [#35127](https://github.com/nodejs/node/pull/35127)
* \[[`23dcfe52ac`](https://github.com/nodejs/node/commit/23dcfe52ac)] - **doc**: fix left nav color contrast (Rich Trott) [#35141](https://github.com/nodejs/node/pull/35141)
* \[[`745987e9f5`](https://github.com/nodejs/node/commit/745987e9f5)] - **doc**: update contact info for Ash Cripps (Ash Cripps) [#35139](https://github.com/nodejs/node/pull/35139)
* \[[`f3f72fd951`](https://github.com/nodejs/node/commit/f3f72fd951)] - **doc**: update my email address (Michael Dawson) [#35121](https://github.com/nodejs/node/pull/35121)
* \[[`0f9908beef`](https://github.com/nodejs/node/commit/0f9908beef)] - **doc**: add missing changes entry for breakEvalOnSigint REPL option (Anna Henningsen) [#35143](https://github.com/nodejs/node/pull/35143)
* \[[`f0b9866a93`](https://github.com/nodejs/node/commit/f0b9866a93)] - **doc**: update security process (Michael Dawson) [#35107](https://github.com/nodejs/node/pull/35107)
* \[[`255d47a6b1`](https://github.com/nodejs/node/commit/255d47a6b1)] - **doc**: fix broken link in perf\_hooks.md (Rich Trott) [#35113](https://github.com/nodejs/node/pull/35113)
* \[[`1e3982047d`](https://github.com/nodejs/node/commit/1e3982047d)] - **doc**: fix broken link in http2.md (Rich Trott) [#35112](https://github.com/nodejs/node/pull/35112)
* \[[`ec5a0ada51`](https://github.com/nodejs/node/commit/ec5a0ada51)] - **doc**: fix broken link in fs.md (Rich Trott) [#35111](https://github.com/nodejs/node/pull/35111)
* \[[`55b8caa958`](https://github.com/nodejs/node/commit/55b8caa958)] - **doc**: fix broken links in deprecations.md (Rich Trott) [#35109](https://github.com/nodejs/node/pull/35109)
* \[[`3954b8f12d`](https://github.com/nodejs/node/commit/3954b8f12d)] - **doc**: add note about path.basename on Windows (Tobias Nießen) [#35065](https://github.com/nodejs/node/pull/35065)
* \[[`bf39354cbc`](https://github.com/nodejs/node/commit/bf39354cbc)] - **doc**: add link to safe integer definition (Tobias Nießen) [#35049](https://github.com/nodejs/node/pull/35049)
* \[[`8ed4ab5ac4`](https://github.com/nodejs/node/commit/8ed4ab5ac4)] - **doc**: format exponents better (Tobias Nießen) [#35050](https://github.com/nodejs/node/pull/35050)
* \[[`b117467a77`](https://github.com/nodejs/node/commit/b117467a77)] - **doc**: improve link-local text in dgram.md (Rich Trott) [#34868](https://github.com/nodejs/node/pull/34868)
* \[[`14d4bfa7c8`](https://github.com/nodejs/node/commit/14d4bfa7c8)] - **doc**: use \_Static method\_ instead of \_Class Method\_ (Rich Trott) [#34659](https://github.com/nodejs/node/pull/34659)
* \[[`d05f615896`](https://github.com/nodejs/node/commit/d05f615896)] - **doc**: tidy some addons.md text (Rich Trott) [#34654](https://github.com/nodejs/node/pull/34654)
* \[[`5846befacb`](https://github.com/nodejs/node/commit/5846befacb)] - **doc**: use \_Class Method\_ in async\_hooks.md (Rich Trott) [#34626](https://github.com/nodejs/node/pull/34626)
* \[[`2302dff635`](https://github.com/nodejs/node/commit/2302dff635)] - **doc**: fix typo in cli.md for report-dir (Ash Cripps) [#33725](https://github.com/nodejs/node/pull/33725)
* \[[`65b7bf40b8`](https://github.com/nodejs/node/commit/65b7bf40b8)] - **doc**: restore color for visited links (Rich Trott) [#35108](https://github.com/nodejs/node/pull/35108)
* \[[`ef8d3731eb`](https://github.com/nodejs/node/commit/ef8d3731eb)] - **doc**: change stablility-2 color for accessibility (Rich Trott) [#35061](https://github.com/nodejs/node/pull/35061)
* \[[`7c947b26e8`](https://github.com/nodejs/node/commit/7c947b26e8)] - **doc**: add deprecated badge to legacy URL methods (Antoine du HAMEL) [#34931](https://github.com/nodejs/node/pull/34931)
* \[[`fb1a1339de`](https://github.com/nodejs/node/commit/fb1a1339de)] - **doc**: spruce up user journey to local docs browsing (Derek Lewis) [#34986](https://github.com/nodejs/node/pull/34986)
* \[[`08b56130db`](https://github.com/nodejs/node/commit/08b56130db)] - **doc**: update syntax highlighting color for accessibility (Rich Trott) [#35063](https://github.com/nodejs/node/pull/35063)
* \[[`1ce26fe50c`](https://github.com/nodejs/node/commit/1ce26fe50c)] - **doc**: remove style for empty links (Antoine du HAMEL) [#35034](https://github.com/nodejs/node/pull/35034)
* \[[`3c984115a0`](https://github.com/nodejs/node/commit/3c984115a0)] - **doc**: fix certificate display in tls doc (Rich Trott) [#35032](https://github.com/nodejs/node/pull/35032)
* \[[`d7989bd1d7`](https://github.com/nodejs/node/commit/d7989bd1d7)] - **doc**: use consistent header typography (Rich Trott) [#35030](https://github.com/nodejs/node/pull/35030)
* \[[`80fa1f5722`](https://github.com/nodejs/node/commit/80fa1f5722)] - **doc**: fix malformed hashes in assert.md (Rich Trott) [#35028](https://github.com/nodejs/node/pull/35028)
* \[[`2529ba261b`](https://github.com/nodejs/node/commit/2529ba261b)] - **doc**: change color contrast for accessibility (Rich Trott) [#35047](https://github.com/nodejs/node/pull/35047)
* \[[`8cc7a730a5`](https://github.com/nodejs/node/commit/8cc7a730a5)] - **doc**: revise commit-queue.md (Rich Trott) [#35006](https://github.com/nodejs/node/pull/35006)
* \[[`e7c74ebee2`](https://github.com/nodejs/node/commit/e7c74ebee2)] - **doc**: change effected to affected (Turner Jabbour) [#34989](https://github.com/nodejs/node/pull/34989)
* \[[`c68c6cd485`](https://github.com/nodejs/node/commit/c68c6cd485)] - **doc**: drop the --production flag for installing windows-build-tools (DeeDeeG) [#34979](https://github.com/nodejs/node/pull/34979)
* \[[`4d28435104`](https://github.com/nodejs/node/commit/4d28435104)] - **doc**: fix broken link to response.writableFinished in deprecations doc (Rich Trott) [#34983](https://github.com/nodejs/node/pull/34983)
* \[[`23389a082f`](https://github.com/nodejs/node/commit/23389a082f)] - **doc**: fix broken link to response.finished in deprecations doc (Rich Trott) [#34982](https://github.com/nodejs/node/pull/34982)
* \[[`4e2415fc6a`](https://github.com/nodejs/node/commit/4e2415fc6a)] - **doc**: fix broken link to writableEnded in deprecations doc (Rich Trott) [#34984](https://github.com/nodejs/node/pull/34984)
* \[[`b575e6341c`](https://github.com/nodejs/node/commit/b575e6341c)] - **doc**: fix typos in buffer doc (Robert Williams) [#34981](https://github.com/nodejs/node/pull/34981)
* \[[`0695e243de`](https://github.com/nodejs/node/commit/0695e243de)] - **doc**: make minor improvements to query string sentence in http2.md (Rich Trott) [#34929](https://github.com/nodejs/node/pull/34929)
* \[[`a5b4526f5d`](https://github.com/nodejs/node/commit/a5b4526f5d)] - **doc**: simplify "make use of" to "use" (Rich Trott) [#34861](https://github.com/nodejs/node/pull/34861)
* \[[`1e33bfcc6a`](https://github.com/nodejs/node/commit/1e33bfcc6a)] - **doc**: make minor fixes to maintaining-openssl.md (Rich Trott) [#34926](https://github.com/nodejs/node/pull/34926)
* \[[`533d00d05d`](https://github.com/nodejs/node/commit/533d00d05d)] - **doc**: fix CHANGELOG.md parsing issue (Juan José Arboleda) [#34923](https://github.com/nodejs/node/pull/34923)
* \[[`1b27f098bd`](https://github.com/nodejs/node/commit/1b27f098bd)] - **doc**: provide more guidance about process.version (Rich Trott) [#34909](https://github.com/nodejs/node/pull/34909)
* \[[`f50fec605d`](https://github.com/nodejs/node/commit/f50fec605d)] - **doc**: use consistent typography for node-addon-api (Rich Trott) [#34910](https://github.com/nodejs/node/pull/34910)
* \[[`222fcb1e66`](https://github.com/nodejs/node/commit/222fcb1e66)] - **doc**: use "previous"/"preceding" instead of "above" as modifier (Rich Trott) [#34877](https://github.com/nodejs/node/pull/34877)
* \[[`961844d25b`](https://github.com/nodejs/node/commit/961844d25b)] - **doc**: improve fs doc intro (James M Snell) [#34843](https://github.com/nodejs/node/pull/34843)
* \[[`26b060f4cd`](https://github.com/nodejs/node/commit/26b060f4cd)] - **doc**: indicate the format of process.version (Danny Guo) [#34872](https://github.com/nodejs/node/pull/34872)
* \[[`da150f4e1e`](https://github.com/nodejs/node/commit/da150f4e1e)] - **doc**: fix ESM/CJS wrapper example (Maksim Sinik) [#34853](https://github.com/nodejs/node/pull/34853)
* \[[`3ea7e03ae4`](https://github.com/nodejs/node/commit/3ea7e03ae4)] - **doc**: adopt Microsoft Style Guide officially (Rich Trott) [#34821](https://github.com/nodejs/node/pull/34821)
* \[[`5f09f45d1f`](https://github.com/nodejs/node/commit/5f09f45d1f)] - **doc**: use 'console' info string for console output (Rich Trott) [#34837](https://github.com/nodejs/node/pull/34837)
* \[[`9d52480396`](https://github.com/nodejs/node/commit/9d52480396)] - **doc**: move addaleax to TSC emeritus (Anna Henningsen) [#34809](https://github.com/nodejs/node/pull/34809)
* \[[`6d9e6f6186`](https://github.com/nodejs/node/commit/6d9e6f6186)] - **doc**: remove space above version picker (Justice Almanzar) [#34768](https://github.com/nodejs/node/pull/34768)
* \[[`c53c34c045`](https://github.com/nodejs/node/commit/c53c34c045)] - **doc**: reorder deprecated tls docs (Jerome T.K. Covington) [#34687](https://github.com/nodejs/node/pull/34687)
* \[[`edda492a94`](https://github.com/nodejs/node/commit/edda492a94)] - **doc**: fix file name to main.mjs and not main.js in esm.md (Frank Lemanschik) [#34786](https://github.com/nodejs/node/pull/34786)
* \[[`3abcc74882`](https://github.com/nodejs/node/commit/3abcc74882)] - **doc**: improve async\_hooks snippets (Andrey Pechkurov) [#34829](https://github.com/nodejs/node/pull/34829)
* \[[`fd4f561ce4`](https://github.com/nodejs/node/commit/fd4f561ce4)] - **doc**: fix some typos and grammar mistakes (Hilla Shahrabani) [#34800](https://github.com/nodejs/node/pull/34800)
* \[[`7a983f5f1d`](https://github.com/nodejs/node/commit/7a983f5f1d)] - **doc**: remove "is recommended from crypto legacy API text (Rich Trott) [#34697](https://github.com/nodejs/node/pull/34697)
* \[[`c7fc16e10a`](https://github.com/nodejs/node/commit/c7fc16e10a)] - **doc**: fix broken links in commit-queue.md (Luigi Pinca) [#34789](https://github.com/nodejs/node/pull/34789)
* \[[`09687b85f7`](https://github.com/nodejs/node/commit/09687b85f7)] - **doc**: avoid \_may\_ in collaborator guide (Rich Trott) [#34749](https://github.com/nodejs/node/pull/34749)
* \[[`f295869ba3`](https://github.com/nodejs/node/commit/f295869ba3)] - **doc**: use sentence-casing for headers in collaborator guide (Rich Trott) [#34713](https://github.com/nodejs/node/pull/34713)
* \[[`94039b75d3`](https://github.com/nodejs/node/commit/94039b75d3)] - **doc**: edit (general) collaborator guide (Rich Trott) [#34712](https://github.com/nodejs/node/pull/34712)
* \[[`653d88ac13`](https://github.com/nodejs/node/commit/653d88ac13)] - **doc**: reduce repetitiveness on Consensus Seeking (Mary Marchini) [#34702](https://github.com/nodejs/node/pull/34702)
* \[[`b28a6a57c4`](https://github.com/nodejs/node/commit/b28a6a57c4)] - **doc**: remove typo in crypto.md (Rich Trott) [#34698](https://github.com/nodejs/node/pull/34698)
* \[[`c189832647`](https://github.com/nodejs/node/commit/c189832647)] - **doc**: n-api environment life cycle APIs are stable (Jim Schlight) [#34641](https://github.com/nodejs/node/pull/34641)
* \[[`898947b5b1`](https://github.com/nodejs/node/commit/898947b5b1)] - **doc**: add padding in the sidebar column (Antoine du HAMEL) [#34665](https://github.com/nodejs/node/pull/34665)
* \[[`75ea463c25`](https://github.com/nodejs/node/commit/75ea463c25)] - **doc**: use semantically appropriate tag for lines (Antoine du HAMEL) [#34660](https://github.com/nodejs/node/pull/34660)
* \[[`0da5ac805c`](https://github.com/nodejs/node/commit/0da5ac805c)] - **doc**: add HPE\_UNEXPECTED\_CONTENT\_LENGTH error description (Nikolay Krashnikov) [#34596](https://github.com/nodejs/node/pull/34596)
* \[[`75ed2f6e2e`](https://github.com/nodejs/node/commit/75ed2f6e2e)] - **doc**: update http server response 'close' event (Renato Mariscal) [#34472](https://github.com/nodejs/node/pull/34472)
* \[[`0ba9052b57`](https://github.com/nodejs/node/commit/0ba9052b57)] - **doc**: add writable and readable options to Duplex docs (Priyank Singh) [#34383](https://github.com/nodejs/node/pull/34383)
* \[[`d0bf0f9c00`](https://github.com/nodejs/node/commit/d0bf0f9c00)] - **doc**: harden policy around objections (Mary Marchini) [#34639](https://github.com/nodejs/node/pull/34639)
* \[[`e9a8f0c127`](https://github.com/nodejs/node/commit/e9a8f0c127)] - **doc**: add Ricky Zhou to collaborators (rickyes) [#34676](https://github.com/nodejs/node/pull/34676)
* \[[`fc612d5635`](https://github.com/nodejs/node/commit/fc612d5635)] - **doc**: edit process.title note for brevity and clarity (Rich Trott) [#34627](https://github.com/nodejs/node/pull/34627)
* \[[`3dda55aedf`](https://github.com/nodejs/node/commit/3dda55aedf)] - **doc**: update fs.watch() availability for IBM i (iandrc) [#34611](https://github.com/nodejs/node/pull/34611)
* \[[`dc6e7f8584`](https://github.com/nodejs/node/commit/dc6e7f8584)] - **doc**: fix typo in path.md (aetheryx) [#34550](https://github.com/nodejs/node/pull/34550)
* \[[`260914c432`](https://github.com/nodejs/node/commit/260914c432)] - **doc**: add release key for Ruy Adorno (Ruy Adorno) [#34628](https://github.com/nodejs/node/pull/34628)
* \[[`e67bd9e050`](https://github.com/nodejs/node/commit/e67bd9e050)] - **doc**: clarify process.title inconsistencies (Corey Butler) [#34557](https://github.com/nodejs/node/pull/34557)
* \[[`c56a29178b`](https://github.com/nodejs/node/commit/c56a29178b)] - **doc**: document the connection event for HTTP2 & TLS servers (Tim Perry) [#34531](https://github.com/nodejs/node/pull/34531)
* \[[`059db0591c`](https://github.com/nodejs/node/commit/059db0591c)] - **doc**: mention null special-case for `napi\_typeof` (Renée Kooi) [#34577](https://github.com/nodejs/node/pull/34577)
* \[[`39f90346f8`](https://github.com/nodejs/node/commit/39f90346f8)] - **doc**: add DerekNonGeneric to collaborators (Derek Lewis) [#34602](https://github.com/nodejs/node/pull/34602)
* \[[`65a0ddbfc3`](https://github.com/nodejs/node/commit/65a0ddbfc3)] - **doc**: use consistent spelling for "falsy" (Rich Trott) [#34545](https://github.com/nodejs/node/pull/34545)
* \[[`261fd11d4b`](https://github.com/nodejs/node/commit/261fd11d4b)] - **doc**: simplify and clarify console.assert() documentation (Rich Trott) [#34544](https://github.com/nodejs/node/pull/34544)
* \[[`b4b2057fb6`](https://github.com/nodejs/node/commit/b4b2057fb6)] - **doc**: use consistent capitalization for addons (Rich Trott) [#34536](https://github.com/nodejs/node/pull/34536)
* \[[`2410a0f7cb`](https://github.com/nodejs/node/commit/2410a0f7cb)] - **doc**: add mmarchini pronouns (Mary Marchini) [#34586](https://github.com/nodejs/node/pull/34586)
* \[[`de03d635d4`](https://github.com/nodejs/node/commit/de03d635d4)] - **doc**: update mmarchini contact info (Mary Marchini) [#34586](https://github.com/nodejs/node/pull/34586)
* \[[`873e84366c`](https://github.com/nodejs/node/commit/873e84366c)] - **doc**: update .mailmap for mmarchini (Mary Marchini) [#34586](https://github.com/nodejs/node/pull/34586)
* \[[`f350b512e7`](https://github.com/nodejs/node/commit/f350b512e7)] - **doc**: use sentence-case for headers in SECURITY.md (Rich Trott) [#34525](https://github.com/nodejs/node/pull/34525)
* \[[`057613c464`](https://github.com/nodejs/node/commit/057613c464)] - _**Revert**_ "**doc**: move ronkorving to emeritus" (Rich Trott) [#34507](https://github.com/nodejs/node/pull/34507)
* \[[`9c725919fc`](https://github.com/nodejs/node/commit/9c725919fc)] - **doc**: use sentence-case for GOVERNANCE.md headers (Rich Trott) [#34503](https://github.com/nodejs/node/pull/34503)
* \[[`c95964afd6`](https://github.com/nodejs/node/commit/c95964afd6)] - **doc**: revise onboarding-extras (Rich Trott) [#34496](https://github.com/nodejs/node/pull/34496)
* \[[`3db13a8043`](https://github.com/nodejs/node/commit/3db13a8043)] - **doc**: remove breaking-change-helper from onboarding-extras (Rich Trott) [#34497](https://github.com/nodejs/node/pull/34497)
* \[[`cef1284a22`](https://github.com/nodejs/node/commit/cef1284a22)] - **doc**: add Triagers section to table of contents in GOVERNANCE.md (Rich Trott) [#34504](https://github.com/nodejs/node/pull/34504)
* \[[`8c0a781ee0`](https://github.com/nodejs/node/commit/8c0a781ee0)] - **doc**: onboarding process extras (Gireesh Punathil) [#34455](https://github.com/nodejs/node/pull/34455)
* \[[`b37b3f017f`](https://github.com/nodejs/node/commit/b37b3f017f)] - **doc**: mention triage in GOVERNANCE.md (Gireesh Punathil) [#34426](https://github.com/nodejs/node/pull/34426)
* \[[`dfdedfd67a`](https://github.com/nodejs/node/commit/dfdedfd67a)] - **doc**: move thefourtheye to emeritus (Rich Trott) [#34471](https://github.com/nodejs/node/pull/34471)
* \[[`56d5ba852f`](https://github.com/nodejs/node/commit/56d5ba852f)] - **doc**: move ronkorving to emeritus (Rich Trott) [#34471](https://github.com/nodejs/node/pull/34471)
* \[[`f70cbc63b8`](https://github.com/nodejs/node/commit/f70cbc63b8)] - **doc**: match link text in index to doc headline (Rich Trott) [#34449](https://github.com/nodejs/node/pull/34449)
* \[[`437b092eed`](https://github.com/nodejs/node/commit/437b092eed)] - **doc**: add AshCripps to collaborators (Ash Cripps) [#34494](https://github.com/nodejs/node/pull/34494)
* \[[`c91e31ded2`](https://github.com/nodejs/node/commit/c91e31ded2)] - **doc**: add author-ready label ref to onboarding doc (Ruy Adorno) [#34381](https://github.com/nodejs/node/pull/34381)
* \[[`319d570a47`](https://github.com/nodejs/node/commit/319d570a47)] - **doc**: add HarshithaKP to collaborators (Harshitha K P) [#34417](https://github.com/nodejs/node/pull/34417)
* \[[`ae60f50a69`](https://github.com/nodejs/node/commit/ae60f50a69)] - **doc**: add rexagod to collaborators (Pranshu Srivastava) [#34457](https://github.com/nodejs/node/pull/34457)
* \[[`8ee83a9d58`](https://github.com/nodejs/node/commit/8ee83a9d58)] - **doc**: add statement of purpose to documentation style guide (Rich Trott) [#34424](https://github.com/nodejs/node/pull/34424)
* \[[`39dea8f70d`](https://github.com/nodejs/node/commit/39dea8f70d)] - **doc**: add release key for Richard Lau (Richard Lau) [#34397](https://github.com/nodejs/node/pull/34397)
* \[[`e15dc5f6ea`](https://github.com/nodejs/node/commit/e15dc5f6ea)] - **doc**: use correct identifier for callback argument (Rich Trott) [#34405](https://github.com/nodejs/node/pull/34405)
* \[[`88bd124d5c`](https://github.com/nodejs/node/commit/88bd124d5c)] - **doc**: add changes metadata to TLS newSession event (Tobias Nießen) [#34294](https://github.com/nodejs/node/pull/34294)
* \[[`0f050d4597`](https://github.com/nodejs/node/commit/0f050d4597)] - **doc**: introduce a triager role (Gireesh Punathil) [#34295](https://github.com/nodejs/node/pull/34295)
* \[[`857ba90138`](https://github.com/nodejs/node/commit/857ba90138)] - **doc**: strengthen suggestion in errors.md (Rich Trott) [#34390](https://github.com/nodejs/node/pull/34390)
* \[[`7c7d3e3697`](https://github.com/nodejs/node/commit/7c7d3e3697)] - **doc**: strengthen wording about fs.access() misuse (Rich Trott) [#34352](https://github.com/nodejs/node/pull/34352)
* \[[`1d64c2c345`](https://github.com/nodejs/node/commit/1d64c2c345)] - **doc**: fix typo in assert.md (Ye-hyoung Kang) [#34316](https://github.com/nodejs/node/pull/34316)
* \[[`7be8dded52`](https://github.com/nodejs/node/commit/7be8dded52)] - **doc**: clarify conditional exports guidance (Guy Bedford) [#34306](https://github.com/nodejs/node/pull/34306)
* \[[`c1b5c89e60`](https://github.com/nodejs/node/commit/c1b5c89e60)] - **doc**: reword warnings about sockets passed to subprocesses (Rich Trott) [#34273](https://github.com/nodejs/node/pull/34273)
* \[[`a2107101be`](https://github.com/nodejs/node/commit/a2107101be)] - **doc**: add danielleadams to collaborators (Danielle Adams) [#34360](https://github.com/nodejs/node/pull/34360)
* \[[`eff1febe9e`](https://github.com/nodejs/node/commit/eff1febe9e)] - **doc**: buffer documentation improvements (James M Snell) [#34230](https://github.com/nodejs/node/pull/34230)
* \[[`ba7ba4fe14`](https://github.com/nodejs/node/commit/ba7ba4fe14)] - **doc**: improve text in fs docs about omitting callbacks (Rich Trott) [#34307](https://github.com/nodejs/node/pull/34307)
* \[[`c4f0cb65a1`](https://github.com/nodejs/node/commit/c4f0cb65a1)] - **doc**: add sxa as collaborator (Stewart X Addison) [#34338](https://github.com/nodejs/node/pull/34338)
* \[[`513ad146c8`](https://github.com/nodejs/node/commit/513ad146c8)] - **doc**: move sebdeckers to emeritus (Rich Trott) [#34298](https://github.com/nodejs/node/pull/34298)
* \[[`a04d76d2ad`](https://github.com/nodejs/node/commit/a04d76d2ad)] - **doc**: add ruyadorno to collaborators (Ruy Adorno) [#34297](https://github.com/nodejs/node/pull/34297)
* \[[`3064755d31`](https://github.com/nodejs/node/commit/3064755d31)] - **doc**: move kfarnung to collaborator emeriti list (Rich Trott) [#34258](https://github.com/nodejs/node/pull/34258)
* \[[`ea33e738fb`](https://github.com/nodejs/node/commit/ea33e738fb)] - **doc**: specify encoding in text/html examples (James M Snell) [#34222](https://github.com/nodejs/node/pull/34222)
* \[[`2615e55d93`](https://github.com/nodejs/node/commit/2615e55d93)] - **doc**: document the ready event for Http2Stream (James M Snell) [#34221](https://github.com/nodejs/node/pull/34221)
* \[[`fbb36ed5c4`](https://github.com/nodejs/node/commit/fbb36ed5c4)] - **doc**: add comment to example about 2xx status codes (James M Snell) [#34223](https://github.com/nodejs/node/pull/34223)
* \[[`f2f1537ea0`](https://github.com/nodejs/node/commit/f2f1537ea0)] - **doc**: document that whitespace is ignored in base64 decoding (James M Snell) [#34227](https://github.com/nodejs/node/pull/34227)
* \[[`0ebb30bb88`](https://github.com/nodejs/node/commit/0ebb30bb88)] - **doc**: document security issues with url.parse() (James M Snell) [#34226](https://github.com/nodejs/node/pull/34226)
* \[[`b60b6d7404`](https://github.com/nodejs/node/commit/b60b6d7404)] - **doc**: move digitalinfinity to emeritus (Rich Trott) [#34191](https://github.com/nodejs/node/pull/34191)
* \[[`e65d6fddaf`](https://github.com/nodejs/node/commit/e65d6fddaf)] - **doc**: move gibfahn to emeritus (Rich Trott) [#34190](https://github.com/nodejs/node/pull/34190)
* \[[`c62941e84c`](https://github.com/nodejs/node/commit/c62941e84c)] - **doc**: remove parenthetical \r\n comment in http and http2 docs (Rich Trott) [#34178](https://github.com/nodejs/node/pull/34178)
* \[[`9bb70a498d`](https://github.com/nodejs/node/commit/9bb70a498d)] - **doc**: remove stability from unreleased errors (Rich Trott) [#33764](https://github.com/nodejs/node/pull/33764)
* \[[`a7a564b418`](https://github.com/nodejs/node/commit/a7a564b418)] - **doc**: util.debuglog callback (Bradley Meck) [#33856](https://github.com/nodejs/node/pull/33856)
* \[[`089a4479a4`](https://github.com/nodejs/node/commit/089a4479a4)] - **doc**: update wording in "Two reading modes" (Julien Poissonnier) [#34119](https://github.com/nodejs/node/pull/34119)
* \[[`32ef1b3347`](https://github.com/nodejs/node/commit/32ef1b3347)] - **doc**: clarify that the ctx argument is optional (Luigi Pinca) [#34097](https://github.com/nodejs/node/pull/34097)
* \[[`8960a63312`](https://github.com/nodejs/node/commit/8960a63312)] - **doc**: add a reference to the list of OpenSSL flags. (Mateusz Krawczuk) [#34050](https://github.com/nodejs/node/pull/34050)
* \[[`4ac0df9160`](https://github.com/nodejs/node/commit/4ac0df9160)] - **doc**: no longer maintain a CNA structure (Sam Roberts) [#33639](https://github.com/nodejs/node/pull/33639)
* \[[`75637e6867`](https://github.com/nodejs/node/commit/75637e6867)] - **doc**: use consistent naming in stream doc (Saleem) [#30506](https://github.com/nodejs/node/pull/30506)
* \[[`71664158fc`](https://github.com/nodejs/node/commit/71664158fc)] - **doc**: clarify how to read process.stdin (Anentropic) [#27350](https://github.com/nodejs/node/pull/27350)
* \[[`25939ccded`](https://github.com/nodejs/node/commit/25939ccded)] - **doc**: fix entry for `napi\_create\_external\_buffer` (Gabriel Schulhof) [#34125](https://github.com/nodejs/node/pull/34125)
* \[[`5f131f71e9`](https://github.com/nodejs/node/commit/5f131f71e9)] - **doc**: fix source link margin to sub-header mark (Rodion Abdurakhimov) [#33664](https://github.com/nodejs/node/pull/33664)
* \[[`f12c6f406a`](https://github.com/nodejs/node/commit/f12c6f406a)] - **doc**: improve async\_hooks asynchronous context example (Denys Otrishko) [#33730](https://github.com/nodejs/node/pull/33730)
* \[[`8fb265d03c`](https://github.com/nodejs/node/commit/8fb265d03c)] - **doc**: clarify esm conditional exports prose (Derek Lewis) [#33886](https://github.com/nodejs/node/pull/33886)
* \[[`49383c8a25`](https://github.com/nodejs/node/commit/49383c8a25)] - **doc**: improve triaging text in issues.md (Rich Trott) [#34164](https://github.com/nodejs/node/pull/34164)
* \[[`a9302b50c9`](https://github.com/nodejs/node/commit/a9302b50c9)] - **doc**: simply dns.ADDRCONFIG language (Rich Trott) [#34155](https://github.com/nodejs/node/pull/34155)
* \[[`1d25e70392`](https://github.com/nodejs/node/commit/1d25e70392)] - **doc**: remove "considered" in errors.md (Rich Trott) [#34152](https://github.com/nodejs/node/pull/34152)
* \[[`f6dff0a57e`](https://github.com/nodejs/node/commit/f6dff0a57e)] - **doc**: simplify and clarify ReferenceError material in errors.md (Rich Trott) [#34151](https://github.com/nodejs/node/pull/34151)
* \[[`e2fff1b1b0`](https://github.com/nodejs/node/commit/e2fff1b1b0)] - **doc**: add http highlight grammar (Derek Lewis) [#33785](https://github.com/nodejs/node/pull/33785)
* \[[`19bfc012d1`](https://github.com/nodejs/node/commit/19bfc012d1)] - **doc**: move sam-github to TSC Emeriti (Sam Roberts) [#34095](https://github.com/nodejs/node/pull/34095)
* \[[`c78ef2d35c`](https://github.com/nodejs/node/commit/c78ef2d35c)] - **doc**: change "considered experimental" to "experimental" in n-api.md (Rich Trott) [#34129](https://github.com/nodejs/node/pull/34129)
* \[[`3d5f7674e7`](https://github.com/nodejs/node/commit/3d5f7674e7)] - **doc**: changed "considered experimental" to "experimental" in cli.md (Rich Trott) [#34128](https://github.com/nodejs/node/pull/34128)
* \[[`6c739aac55`](https://github.com/nodejs/node/commit/6c739aac55)] - **doc**: improve text in issues.md (falguniraina) [#33973](https://github.com/nodejs/node/pull/33973)
* \[[`0672384be9`](https://github.com/nodejs/node/commit/0672384be9)] - **doc**: change "currently not considered public" to "not supported" (Rich Trott) [#34114](https://github.com/nodejs/node/pull/34114)
* \[[`64e182553e`](https://github.com/nodejs/node/commit/64e182553e)] - **doc**: clarify that APIs are no longer experimental (Rich Trott) [#34113](https://github.com/nodejs/node/pull/34113)
* \[[`e4ac393383`](https://github.com/nodejs/node/commit/e4ac393383)] - **doc**: clarify O\_EXCL text in fs.md (Rich Trott) [#34096](https://github.com/nodejs/node/pull/34096)
* \[[`d67cb7ed0f`](https://github.com/nodejs/node/commit/d67cb7ed0f)] - **doc**: clarify ambiguous rdev description (Rich Trott) [#34094](https://github.com/nodejs/node/pull/34094)
* \[[`c6ea3d6616`](https://github.com/nodejs/node/commit/c6ea3d6616)] - **doc**: make minor improvements to paragraph in child\_process.md (Rich Trott) [#34063](https://github.com/nodejs/node/pull/34063)
* \[[`21b0132eec`](https://github.com/nodejs/node/commit/21b0132eec)] - **doc**: improve paragraph in esm.md (Rich Trott) [#34064](https://github.com/nodejs/node/pull/34064)
* \[[`66cd7bf69d`](https://github.com/nodejs/node/commit/66cd7bf69d)] - **doc**: clarify require/import mutual exclusivity (Guy Bedford) [#33832](https://github.com/nodejs/node/pull/33832)
* \[[`5ba0ba4b69`](https://github.com/nodejs/node/commit/5ba0ba4b69)] - **doc**: add dynamic source code links (Alec Davidson) [#33996](https://github.com/nodejs/node/pull/33996)
* \[[`51cdd10ea5`](https://github.com/nodejs/node/commit/51cdd10ea5)] - **doc**: mention errors thrown by methods called on an unbound dgram.Socket (Mateusz Krawczuk) [#33983](https://github.com/nodejs/node/pull/33983)
* \[[`6d22ae3630`](https://github.com/nodejs/node/commit/6d22ae3630)] - **doc**: document n-api callback scope usage (Gabriel Schulhof) [#33915](https://github.com/nodejs/node/pull/33915)
* \[[`e4854de18c`](https://github.com/nodejs/node/commit/e4854de18c)] - **doc**: standardize constructor doc header layout (Rich Trott) [#33781](https://github.com/nodejs/node/pull/33781)
* \[[`79c4c73f4c`](https://github.com/nodejs/node/commit/79c4c73f4c)] - **doc**: split process.umask() entry into two (Rich Trott) [#32711](https://github.com/nodejs/node/pull/32711)
* \[[`0a927216cf`](https://github.com/nodejs/node/commit/0a927216cf)] - **(SEMVER-MAJOR)** **doc**: deprecate process.umask() with no arguments (Colin Ihrig) [#32499](https://github.com/nodejs/node/pull/32499)
* \[[`05dae0231b`](https://github.com/nodejs/node/commit/05dae0231b)] - **doc,lib**: remove unused error code (Rich Trott) [#34792](https://github.com/nodejs/node/pull/34792)
* \[[`e8ddaa3f0e`](https://github.com/nodejs/node/commit/e8ddaa3f0e)] - **doc,n-api**: add link to n-api tutorial website (Jim Schlight) [#34870](https://github.com/nodejs/node/pull/34870)
* \[[`b47172d2ed`](https://github.com/nodejs/node/commit/b47172d2ed)] - **doc,test**: specify and test CLI option precedence rules (Anna Henningsen) [#35106](https://github.com/nodejs/node/pull/35106)
* \[[`3975dd3525`](https://github.com/nodejs/node/commit/3975dd3525)] - **doc,tools**: remove malfunctioning Linux manpage linker (Rich Trott) [#34985](https://github.com/nodejs/node/pull/34985)
* \[[`f57104bb1a`](https://github.com/nodejs/node/commit/f57104bb1a)] - **doc,tools**: annotate broken links in actions workflow (Richard Lau) [#34810](https://github.com/nodejs/node/pull/34810)
* \[[`7b29c91944`](https://github.com/nodejs/node/commit/7b29c91944)] - **doc,tools**: syntax highlight api docs at compile-time (Francisco Ryan Tolmasky I) [#34148](https://github.com/nodejs/node/pull/34148)
* \[[`7a8f59f1d6`](https://github.com/nodejs/node/commit/7a8f59f1d6)] - **(SEMVER-MINOR)** **embedding**: make Stop() stop Workers (Anna Henningsen) [#32531](https://github.com/nodejs/node/pull/32531)
* \[[`ff0a0366f7`](https://github.com/nodejs/node/commit/ff0a0366f7)] - **(SEMVER-MINOR)** **embedding**: provide hook for custom process.exit() behaviour (Anna Henningsen) [#32531](https://github.com/nodejs/node/pull/32531)
* \[[`5c968a0f92`](https://github.com/nodejs/node/commit/5c968a0f92)] - **errors**: use `ErrorPrototypeToString` from `primordials` object (ExE Boss) [#34891](https://github.com/nodejs/node/pull/34891)
* \[[`bf7b796491`](https://github.com/nodejs/node/commit/bf7b796491)] - **esm**: better package.json parser errors (Guy Bedford) [#35117](https://github.com/nodejs/node/pull/35117)
* \[[`9159649395`](https://github.com/nodejs/node/commit/9159649395)] - **esm**: shorten ERR\_UNSUPPORTED\_ESM\_URL\_SCHEME message (Rich Trott) [#34836](https://github.com/nodejs/node/pull/34836)
* \[[`551be2aeb9`](https://github.com/nodejs/node/commit/551be2aeb9)] - **esm**: improve error message of ERR\_UNSUPPORTED\_ESM\_URL\_SCHEME (Denys Otrishko) [#34795](https://github.com/nodejs/node/pull/34795)
* \[[`5c3c8b3029`](https://github.com/nodejs/node/commit/5c3c8b3029)] - **events**: variable originalListener is useless (fuxingZhang) [#33596](https://github.com/nodejs/node/pull/33596)
* \[[`ff7fbc38f1`](https://github.com/nodejs/node/commit/ff7fbc38f1)] - **events**: improve listeners() performance (Brian White) [#33863](https://github.com/nodejs/node/pull/33863)
* \[[`830574f199`](https://github.com/nodejs/node/commit/830574f199)] - **events**: improve arrayClone performance (Brian White) [#33774](https://github.com/nodejs/node/pull/33774)
* \[[`a19933f7fc`](https://github.com/nodejs/node/commit/a19933f7fc)] - **(SEMVER-MINOR)** **fs**: implement lutimes (Maël Nison) [#33399](https://github.com/nodejs/node/pull/33399)
* \[[`3d1bdc254c`](https://github.com/nodejs/node/commit/3d1bdc254c)] - **(SEMVER-MINOR)** **http**: add maxTotalSockets to agent class (rickyes) [#33617](https://github.com/nodejs/node/pull/33617)
* \[[`fb68487b8c`](https://github.com/nodejs/node/commit/fb68487b8c)] - **(SEMVER-MINOR)** **http**: return this from IncomingMessage#destroy() (Colin Ihrig) [#32789](https://github.com/nodejs/node/pull/32789)
* \[[`388d125a64`](https://github.com/nodejs/node/commit/388d125a64)] - **(SEMVER-MINOR)** **http**: expose host and protocol on ClientRequest (wenningplus) [#33803](https://github.com/nodejs/node/pull/33803)
* \[[`756ac65218`](https://github.com/nodejs/node/commit/756ac65218)] - **http**: fix crash for sync write errors during header parsing (Anna Henningsen) [#34251](https://github.com/nodejs/node/pull/34251)
* \[[`10815c4eff`](https://github.com/nodejs/node/commit/10815c4eff)] - **http**: provide keep-alive timeout response header (Robert Nagy) [#34561](https://github.com/nodejs/node/pull/34561)
* \[[`e52cc24e31`](https://github.com/nodejs/node/commit/e52cc24e31)] - **http**: don't write error to socket (Robert Nagy) [#34465](https://github.com/nodejs/node/pull/34465)
* \[[`4e07faa7cf`](https://github.com/nodejs/node/commit/4e07faa7cf)] - **http**: add note about timer unref (Robert Nagy) [#34143](https://github.com/nodejs/node/pull/34143)
* \[[`1a09b4d2ca`](https://github.com/nodejs/node/commit/1a09b4d2ca)] - **http**: fixes memory retention issue with FreeList and HTTPParser (John Leidegren) [#33190](https://github.com/nodejs/node/pull/33190)
* \[[`ec1df7b4c9`](https://github.com/nodejs/node/commit/ec1df7b4c9)] - **http**: fix incorrect headersTimeout measurement (Alex R) [#32329](https://github.com/nodejs/node/pull/32329)
* \[[`ca836344fa`](https://github.com/nodejs/node/commit/ca836344fa)] - **http**: don't throw on `Uint8Array`s for `http.ServerResponse#write` (Pranshu Srivastava) [#33155](https://github.com/nodejs/node/pull/33155)
* \[[`4079cdd5f2`](https://github.com/nodejs/node/commit/4079cdd5f2)] - **http2**: fix Http2Response.sendDate (João Lucas Lucchetta) [#34850](https://github.com/nodejs/node/pull/34850)
* \[[`7551a8be47`](https://github.com/nodejs/node/commit/7551a8be47)] - **(SEMVER-MINOR)** **http2**: return this for Http2ServerRequest#setTimeout (Pranshu Srivastava) [#33994](https://github.com/nodejs/node/pull/33994)
* \[[`4d0129aefb`](https://github.com/nodejs/node/commit/4d0129aefb)] - **(SEMVER-MINOR)** **http2**: do not modify explicity set date headers (Pranshu Srivastava) [#33160](https://github.com/nodejs/node/pull/33160)
* \[[`45d712c6f6`](https://github.com/nodejs/node/commit/45d712c6f6)] - **http2**: add maxHeaderSize option to http2 (Priyank Singh) [#33636](https://github.com/nodejs/node/pull/33636)
* \[[`4a2accb3d0`](https://github.com/nodejs/node/commit/4a2accb3d0)] - **internal**: rename error-serdes for consistency (Evan Lucas) [#33793](https://github.com/nodejs/node/pull/33793)
* \[[`9f16b7f332`](https://github.com/nodejs/node/commit/9f16b7f332)] - **lib**: improve debuglog() performance (Brian White) [#32260](https://github.com/nodejs/node/pull/32260)
* \[[`efd46e3b61`](https://github.com/nodejs/node/commit/efd46e3b61)] - **lib**: always initialize esm loader callbackMap (Shelley Vohr) [#34127](https://github.com/nodejs/node/pull/34127)
* \[[`f29ab4092f`](https://github.com/nodejs/node/commit/f29ab4092f)] - **lib**: add UNC support to url.pathToFileURL() (Matthew McEachen) [#34743](https://github.com/nodejs/node/pull/34743)
* \[[`176f8c35c5`](https://github.com/nodejs/node/commit/176f8c35c5)] - **lib**: use non-symbols in isURLInstance check (Shelley Vohr) [#34622](https://github.com/nodejs/node/pull/34622)
* \[[`633b4d5e62`](https://github.com/nodejs/node/commit/633b4d5e62)] - **lib**: absorb `path` error cases (Gireesh Punathil) [#34519](https://github.com/nodejs/node/pull/34519)
* \[[`6054e213f9`](https://github.com/nodejs/node/commit/6054e213f9)] - **lib**: simplify assignment (sapics) [#33718](https://github.com/nodejs/node/pull/33718)
* \[[`32c51c6c7d`](https://github.com/nodejs/node/commit/32c51c6c7d)] - **lib**: replace http to https of comment link urls (sapics) [#34158](https://github.com/nodejs/node/pull/34158)
* \[[`d1be44c705`](https://github.com/nodejs/node/commit/d1be44c705)] - **meta**: update module pages in CODEOWNERS (Antoine du Hamel) [#34932](https://github.com/nodejs/node/pull/34932)
* \[[`09100ce4ce`](https://github.com/nodejs/node/commit/09100ce4ce)] - **meta**: add links to OpenJSF Slack (Mary Marchini) [#35128](https://github.com/nodejs/node/pull/35128)
* \[[`c7eb462bde`](https://github.com/nodejs/node/commit/c7eb462bde)] - **meta**: update my collab entry (devsnek) [#35160](https://github.com/nodejs/node/pull/35160)
* \[[`2b3d4bd550`](https://github.com/nodejs/node/commit/2b3d4bd550)] - **meta**: remove non-existent quic from CODEOWNERS (Richard Lau) [#34947](https://github.com/nodejs/node/pull/34947)
* \[[`36c705d83b`](https://github.com/nodejs/node/commit/36c705d83b)] - **meta**: enable wasi for CODEOWNERS (gengjiawen) [#34889](https://github.com/nodejs/node/pull/34889)
* \[[`fb98e762ce`](https://github.com/nodejs/node/commit/fb98e762ce)] - **meta**: fix codeowners docs path (Mary Marchini) [#34811](https://github.com/nodejs/node/pull/34811)
* \[[`5119586c0b`](https://github.com/nodejs/node/commit/5119586c0b)] - **meta**: add TSC as owner of governance-related docs (Mary Marchini) [#34737](https://github.com/nodejs/node/pull/34737)
* \[[`6d6bd2dc3b`](https://github.com/nodejs/node/commit/6d6bd2dc3b)] - **meta**: uncomment all codeowners (Mary Marchini) [#34670](https://github.com/nodejs/node/pull/34670)
* \[[`ac0b9496e5`](https://github.com/nodejs/node/commit/ac0b9496e5)] - **meta**: enable http2 team for CODEOWNERS (Rich Trott) [#34534](https://github.com/nodejs/node/pull/34534)
* \[[`2ac653dc1a`](https://github.com/nodejs/node/commit/2ac653dc1a)] - **meta**: make issue template mobile friendly and address nits (Derek Lewis) [#34243](https://github.com/nodejs/node/pull/34243)
* \[[`6319c8f8bb`](https://github.com/nodejs/node/commit/6319c8f8bb)] - **meta**: add N-API to codeowners coverage (Michael Dawson) [#34039](https://github.com/nodejs/node/pull/34039)
* \[[`78ee480469`](https://github.com/nodejs/node/commit/78ee480469)] - **meta**: fixup CODEOWNERS so it hopefully works (James M Snell) [#34147](https://github.com/nodejs/node/pull/34147)
* \[[`ed3278d55d`](https://github.com/nodejs/node/commit/ed3278d55d)] - **module**: fix crash on multiline named cjs imports (Christoph Tavan) [#35275](https://github.com/nodejs/node/pull/35275)
* \[[`89a58f61d7`](https://github.com/nodejs/node/commit/89a58f61d7)] - **module**: use isURLInstance instead of instanceof (Antoine du HAMEL) [#34951](https://github.com/nodejs/node/pull/34951)
* \[[`fc93cc95d8`](https://github.com/nodejs/node/commit/fc93cc95d8)] - **module**: drop `-u` alias for `--conditions` (Richard Lau) [#34935](https://github.com/nodejs/node/pull/34935)
* \[[`740c95819f`](https://github.com/nodejs/node/commit/740c95819f)] - **module**: fix check for package.json at volume root (Derek Lewis) [#34595](https://github.com/nodejs/node/pull/34595)
* \[[`cecc193abc`](https://github.com/nodejs/node/commit/cecc193abc)] - **module**: share CJS/ESM resolver fns, refactoring (Guy Bedford) [#34744](https://github.com/nodejs/node/pull/34744)
* \[[`d9857fdbc2`](https://github.com/nodejs/node/commit/d9857fdbc2)] - **module**: custom --conditions flag option (Guy Bedford) [#34637](https://github.com/nodejs/node/pull/34637)
* \[[`3ad146d474`](https://github.com/nodejs/node/commit/3ad146d474)] - **module**: use cjsCache over esm injection (Guy Bedford) [#34605](https://github.com/nodejs/node/pull/34605)
* \[[`00aa935f5c`](https://github.com/nodejs/node/commit/00aa935f5c)] - **module**: self referential modules in repl or `-r` (Daniele Belardi) [#32261](https://github.com/nodejs/node/pull/32261)
* \[[`d065334d42`](https://github.com/nodejs/node/commit/d065334d42)] - **(SEMVER-MINOR)** **module**: package "imports" field (Guy Bedford) [#34117](https://github.com/nodejs/node/pull/34117)
* \[[`c9bd1a7d8a`](https://github.com/nodejs/node/commit/c9bd1a7d8a)] - **(SEMVER-MINOR)** **module**: deprecate module.parent (Antoine du HAMEL) [#32217](https://github.com/nodejs/node/pull/32217)
* \[[`b9d0f73c7c`](https://github.com/nodejs/node/commit/b9d0f73c7c)] - **(SEMVER-MINOR)** **n-api**: create N-API version 7 (Gabriel Schulhof) [#35199](https://github.com/nodejs/node/pull/35199)
* \[[`a5aa3ddacf`](https://github.com/nodejs/node/commit/a5aa3ddacf)] - **n-api**: re-implement async env cleanup hooks (Gabriel Schulhof) [#34819](https://github.com/nodejs/node/pull/34819)
* \[[`c440748779`](https://github.com/nodejs/node/commit/c440748779)] - **n-api**: fix use-after-free with napi\_remove\_async\_cleanup\_hook (Anna Henningsen) [#34662](https://github.com/nodejs/node/pull/34662)
* \[[`e7486d4df6`](https://github.com/nodejs/node/commit/e7486d4df6)] - **(SEMVER-MINOR)** **n-api**: support type-tagging objects (Gabriel Schulhof) [#28237](https://github.com/nodejs/node/pull/28237)
* \[[`a6b655614f`](https://github.com/nodejs/node/commit/a6b655614f)] - **n-api**: handle weak no-finalizer refs correctly (Gabriel Schulhof) [#34839](https://github.com/nodejs/node/pull/34839)
* \[[`02fe75026e`](https://github.com/nodejs/node/commit/02fe75026e)] - **n-api**: simplify bigint-from-word creation (Gabriel Schulhof) [#34554](https://github.com/nodejs/node/pull/34554)
* \[[`ba2e341f1d`](https://github.com/nodejs/node/commit/ba2e341f1d)] - **n-api**: run all finalizers via SetImmediate() (Gabriel Schulhof) [#34386](https://github.com/nodejs/node/pull/34386)
* \[[`2cf231678b`](https://github.com/nodejs/node/commit/2cf231678b)] - **(SEMVER-MINOR)** **n-api,src**: provide asynchronous cleanup hooks (Anna Henningsen) [#34572](https://github.com/nodejs/node/pull/34572)
* \[[`3c4abe0e91`](https://github.com/nodejs/node/commit/3c4abe0e91)] - **net**: replace usage of internal stream state with public api (Denys Otrishko) [#34885](https://github.com/nodejs/node/pull/34885)
* \[[`6b5d679c80`](https://github.com/nodejs/node/commit/6b5d679c80)] - **net**: validate custom lookup() output (Colin Ihrig) [#34813](https://github.com/nodejs/node/pull/34813)
* \[[`09056fdf38`](https://github.com/nodejs/node/commit/09056fdf38)] - **net**: don't return the stream object from onStreamRead (Robey Pointer) [#34375](https://github.com/nodejs/node/pull/34375)
* \[[`76ba129151`](https://github.com/nodejs/node/commit/76ba129151)] - **net**: allow wider regex in interface name (Stewart X Addison) [#34364](https://github.com/nodejs/node/pull/34364)
* \[[`ce5d0db34b`](https://github.com/nodejs/node/commit/ce5d0db34b)] - **net**: fix bufferSize (Robert Nagy) [#34088](https://github.com/nodejs/node/pull/34088)
* \[[`2c409a2853`](https://github.com/nodejs/node/commit/2c409a2853)] - **(SEMVER-MINOR)** **perf\_hooks**: add idleTime and event loop util (Trevor Norris) [#34938](https://github.com/nodejs/node/pull/34938)
* \[[`35ff592613`](https://github.com/nodejs/node/commit/35ff592613)] - **policy**: increase tests via permutation matrix (Bradley Meck) [#34404](https://github.com/nodejs/node/pull/34404)
* \[[`0ede223fa8`](https://github.com/nodejs/node/commit/0ede223fa8)] - **policy**: add startup benchmark and make SRI lazier (Bradley Farias) [#29527](https://github.com/nodejs/node/pull/29527)
* \[[`53eae0dafd`](https://github.com/nodejs/node/commit/53eae0dafd)] - **process**: correctly parse Unicode in NODE\_OPTIONS (Bartosz Sosnowski) [#34476](https://github.com/nodejs/node/pull/34476)
* \[[`6ccacdfddb`](https://github.com/nodejs/node/commit/6ccacdfddb)] - **querystring**: manage percent character at unescape (Daijiro Wachi) [#35013](https://github.com/nodejs/node/pull/35013)
* \[[`b7be751447`](https://github.com/nodejs/node/commit/b7be751447)] - **repl**: support --loader option in builtin REPL (Michaël Zasso) [#33437](https://github.com/nodejs/node/pull/33437)
* \[[`63cd05b1d6`](https://github.com/nodejs/node/commit/63cd05b1d6)] - **src**: fix ParseEncoding (sapics) [#33957](https://github.com/nodejs/node/pull/33957)
* \[[`090f86955f`](https://github.com/nodejs/node/commit/090f86955f)] - **src**: fix minor comment typo in KeyObjectData (Daniel Bevenius) [#34167](https://github.com/nodejs/node/pull/34167)
* \[[`50b1cde872`](https://github.com/nodejs/node/commit/50b1cde872)] - **(SEMVER-MINOR)** **src**: store key data in separate class (Tobias Nießen) [#33360](https://github.com/nodejs/node/pull/33360)
* \[[`bf3aaa31d0`](https://github.com/nodejs/node/commit/bf3aaa31d0)] - **(SEMVER-MINOR)** **src**: add NativeKeyObject base class (Tobias Nießen) [#33360](https://github.com/nodejs/node/pull/33360)
* \[[`91978820fa`](https://github.com/nodejs/node/commit/91978820fa)] - **(SEMVER-MINOR)** **src**: rename internal key handles to KeyObjectHandle (Tobias Nießen) [#33360](https://github.com/nodejs/node/pull/33360)
* \[[`667d520148`](https://github.com/nodejs/node/commit/667d520148)] - **(SEMVER-MINOR)** **src**: introduce BaseObject base FunctionTemplate (Anna Henningsen) [#33772](https://github.com/nodejs/node/pull/33772)
* \[[`3e21dd91c1`](https://github.com/nodejs/node/commit/3e21dd91c1)] - **(SEMVER-MINOR)** **src**: add option to track unmanaged file descriptors (Anna Henningsen) [#34303](https://github.com/nodejs/node/pull/34303)
* \[[`0affe8622e`](https://github.com/nodejs/node/commit/0affe8622e)] - **(SEMVER-MINOR)** **src**: add public APIs to manage v8::TracingController (Anna Henningsen) [#33850](https://github.com/nodejs/node/pull/33850)
* \[[`b7e4d5fc0e`](https://github.com/nodejs/node/commit/b7e4d5fc0e)] - **src**: shutdown libuv before exit() (Anna Henningsen) [#35021](https://github.com/nodejs/node/pull/35021)
* \[[`5e28660121`](https://github.com/nodejs/node/commit/5e28660121)] - **(SEMVER-MINOR)** **src**: allow embedders to disable esm loader (Shelley Vohr) [#34060](https://github.com/nodejs/node/pull/34060)
* \[[`7e2cd728bb`](https://github.com/nodejs/node/commit/7e2cd728bb)] - **src**: add callback scope for native immediates (Anna Henningsen) [#34366](https://github.com/nodejs/node/pull/34366)
* \[[`147440510f`](https://github.com/nodejs/node/commit/147440510f)] - **src**: flush V8 interrupts from Environment dtor (Anna Henningsen) [#32523](https://github.com/nodejs/node/pull/32523)
* \[[`29620c41fb`](https://github.com/nodejs/node/commit/29620c41fb)] - **src**: use env->RequestInterrupt() for inspector MainThreadInterface (Anna Henningsen) [#32523](https://github.com/nodejs/node/pull/32523)
* \[[`2e4536e701`](https://github.com/nodejs/node/commit/2e4536e701)] - **src**: use env->RequestInterrupt() for inspector io thread start (Anna Henningsen) [#32523](https://github.com/nodejs/node/pull/32523)
* \[[`4704e586dc`](https://github.com/nodejs/node/commit/4704e586dc)] - **src**: fix cleanup hook removal for InspectorTimer (Anna Henningsen) [#32523](https://github.com/nodejs/node/pull/32523)
* \[[`4513b6a3df`](https://github.com/nodejs/node/commit/4513b6a3df)] - **src**: make `Environment::interrupt\_data\_` atomic (Anna Henningsen) [#32523](https://github.com/nodejs/node/pull/32523)
* \[[`1066341cd9`](https://github.com/nodejs/node/commit/1066341cd9)] - **src**: initialize inspector before RunBootstrapping() (Anna Henningsen) [#32672](https://github.com/nodejs/node/pull/32672)
* \[[`b8c9048a87`](https://github.com/nodejs/node/commit/b8c9048a87)] - **(SEMVER-MINOR)** **src**: shutdown platform from FreePlatform() (Anna Henningsen) [#30467](https://github.com/nodejs/node/pull/30467)
* \[[`a28c990061`](https://github.com/nodejs/node/commit/a28c990061)] - **(SEMVER-MINOR)** **src**: fix what a dispose without checking (Jichan) [#30467](https://github.com/nodejs/node/pull/30467)
* \[[`2f8f76736b`](https://github.com/nodejs/node/commit/2f8f76736b)] - **(SEMVER-MINOR)** **src**: allow non-Node.js TracingControllers (Anna Henningsen) [#30467](https://github.com/nodejs/node/pull/30467)
* \[[`9b84ee6480`](https://github.com/nodejs/node/commit/9b84ee6480)] - **(SEMVER-MINOR)** **src**: add ability to look up platform based on `Environment\*` (Anna Henningsen) [#30467](https://github.com/nodejs/node/pull/30467)
* \[[`a770a35f61`](https://github.com/nodejs/node/commit/a770a35f61)] - **(SEMVER-MINOR)** **src**: make InitializeNodeWithArgs() official public API (Anna Henningsen) [#30467](https://github.com/nodejs/node/pull/30467)
* \[[`8005e637b1`](https://github.com/nodejs/node/commit/8005e637b1)] - **(SEMVER-MINOR)** **src**: add unique\_ptr equivalent of CreatePlatform (Anna Henningsen) [#30467](https://github.com/nodejs/node/pull/30467)
* \[[`4a6748d2c3`](https://github.com/nodejs/node/commit/4a6748d2c3)] - **(SEMVER-MINOR)** **src**: add LoadEnvironment() variant taking a string (Anna Henningsen) [#30467](https://github.com/nodejs/node/pull/30467)
* \[[`c5aa3f4adb`](https://github.com/nodejs/node/commit/c5aa3f4adb)] - **(SEMVER-MINOR)** **src**: provide a variant of LoadEnvironment taking a callback (Anna Henningsen) [#30467](https://github.com/nodejs/node/pull/30467)
* \[[`808dedc4b3`](https://github.com/nodejs/node/commit/808dedc4b3)] - **(SEMVER-MINOR)** **src**: align worker and main thread code with embedder API (Anna Henningsen) [#30467](https://github.com/nodejs/node/pull/30467)
* \[[`e809a5cd6b`](https://github.com/nodejs/node/commit/e809a5cd6b)] - **(SEMVER-MINOR)** **src**: associate is\_main\_thread() with worker\_context() (Anna Henningsen) [#30467](https://github.com/nodejs/node/pull/30467)
* \[[`b7350e8c6e`](https://github.com/nodejs/node/commit/b7350e8c6e)] - **(SEMVER-MINOR)** **src**: move worker\_context from Environment to IsolateData (Anna Henningsen) [#30467](https://github.com/nodejs/node/pull/30467)
* \[[`9a5cec3466`](https://github.com/nodejs/node/commit/9a5cec3466)] - **(SEMVER-MINOR)** **src**: fix memory leak in CreateEnvironment when bootstrap fails (Anna Henningsen) [#30467](https://github.com/nodejs/node/pull/30467)
* \[[`7d92ac7a35`](https://github.com/nodejs/node/commit/7d92ac7a35)] - **(SEMVER-MINOR)** **src**: make `FreeEnvironment()` perform all necessary cleanup (Anna Henningsen) [#30467](https://github.com/nodejs/node/pull/30467)
* \[[`1d3638b189`](https://github.com/nodejs/node/commit/1d3638b189)] - **src**: use enum for refed flag on native immediates (Anna Henningsen) [#33444](https://github.com/nodejs/node/pull/33444)
* \[[`18e8687923`](https://github.com/nodejs/node/commit/18e8687923)] - **(SEMVER-MINOR)** **src**: allow preventing SetPromiseRejectCallback (Shelley Vohr) [#34387](https://github.com/nodejs/node/pull/34387)
* \[[`403deb71d5`](https://github.com/nodejs/node/commit/403deb71d5)] - **(SEMVER-MINOR)** **src**: allow setting a dir for all diagnostic output (Ash Cripps) [#33584](https://github.com/nodejs/node/pull/33584)
* \[[`19b55be03b`](https://github.com/nodejs/node/commit/19b55be03b)] - **(SEMVER-MINOR)** **src**: add equality operators for BaseObjectPtr (Anna Henningsen) [#33772](https://github.com/nodejs/node/pull/33772)
* \[[`5eb1c9cee0`](https://github.com/nodejs/node/commit/5eb1c9cee0)] - **src**: add get/set pair for env context awareness (Shelley Vohr) [#35024](https://github.com/nodejs/node/pull/35024)
* \[[`00e020b841`](https://github.com/nodejs/node/commit/00e020b841)] - **src**: disallow JS execution during exit() (Anna Henningsen) [#35020](https://github.com/nodejs/node/pull/35020)
* \[[`26a596bf29`](https://github.com/nodejs/node/commit/26a596bf29)] - **src**: fix abort on uv\_loop\_init() failure (Ben Noordhuis) [#34874](https://github.com/nodejs/node/pull/34874)
* \[[`d953fa3038`](https://github.com/nodejs/node/commit/d953fa3038)] - **src**: usage of modernize-use-equals-default (Yash Ladha) [#34807](https://github.com/nodejs/node/pull/34807)
* \[[`541fb1b001`](https://github.com/nodejs/node/commit/541fb1b001)] - **src**: prefer C++ empty() in boolean expressions (Tobias Nießen) [#34432](https://github.com/nodejs/node/pull/34432)
* \[[`1549048307`](https://github.com/nodejs/node/commit/1549048307)] - **src**: spin shutdown loop while immediates are pending (Anna Henningsen) [#34662](https://github.com/nodejs/node/pull/34662)
* \[[`dabd04d79b`](https://github.com/nodejs/node/commit/dabd04d79b)] - **src**: fix `size` underflow in CallbackQueue (Anna Henningsen) [#34662](https://github.com/nodejs/node/pull/34662)
* \[[`c0a961efc7`](https://github.com/nodejs/node/commit/c0a961efc7)] - **src**: fix unused namespace member in node\_util (Andrey Pechkurov) [#34565](https://github.com/nodejs/node/pull/34565)
* \[[`9f465009b1`](https://github.com/nodejs/node/commit/9f465009b1)] - **src**: skip weak references for memory tracking (Anna Henningsen) [#34469](https://github.com/nodejs/node/pull/34469)
* \[[`c302cae814`](https://github.com/nodejs/node/commit/c302cae814)] - **src**: remove unused variable in node\_file.cc (sapics) [#34317](https://github.com/nodejs/node/pull/34317)
* \[[`5a16a671ef`](https://github.com/nodejs/node/commit/5a16a671ef)] - **src**: avoid strcmp in SecureContext::Init (Tobias Nießen) [#34329](https://github.com/nodejs/node/pull/34329)
* \[[`007b4c1ac9`](https://github.com/nodejs/node/commit/007b4c1ac9)] - **src**: refactor CertCbDone to avoid goto statement (Tobias Nießen) [#34325](https://github.com/nodejs/node/pull/34325)
* \[[`a2141d32ed`](https://github.com/nodejs/node/commit/a2141d32ed)] - **src**: remove redundant snprintf (Anna Henningsen) [#34282](https://github.com/nodejs/node/pull/34282)
* \[[`6ddeee4b8d`](https://github.com/nodejs/node/commit/6ddeee4b8d)] - **src**: use FromMaybe instead of ToLocal in GetCert (Daniel Bevenius) [#34276](https://github.com/nodejs/node/pull/34276)
* \[[`3901c7fd30`](https://github.com/nodejs/node/commit/3901c7fd30)] - **src**: add GetCipherValue function (Daniel Bevenius) [#34287](https://github.com/nodejs/node/pull/34287)
* \[[`c1901896b7`](https://github.com/nodejs/node/commit/c1901896b7)] - **src**: add encoding\_type variable in WritePrivateKey (Daniel Bevenius) [#34181](https://github.com/nodejs/node/pull/34181)
* \[[`00835434ef`](https://github.com/nodejs/node/commit/00835434ef)] - **src**: fix unused namespace member (Nikola Glavina) [#34212](https://github.com/nodejs/node/pull/34212)
* \[[`88d12c00da`](https://github.com/nodejs/node/commit/88d12c00da)] - **src**: remove unnecessary ToLocalChecked call (Daniel Bevenius) [#33902](https://github.com/nodejs/node/pull/33902)
* \[[`a1da012f6b`](https://github.com/nodejs/node/commit/a1da012f6b)] - **src**: do not crash if ToggleAsyncHook fails during termination (Anna Henningsen) [#34362](https://github.com/nodejs/node/pull/34362)
* \[[`2a7c65acaf`](https://github.com/nodejs/node/commit/2a7c65acaf)] - **src,doc**: fix wording to refer to context, not environment (Turner Jabbour) [#34880](https://github.com/nodejs/node/pull/34880)
* \[[`302d38974d`](https://github.com/nodejs/node/commit/302d38974d)] - **src,doc**: rephrase for clarity (Turner Jabbour) [#34879](https://github.com/nodejs/node/pull/34879)
* \[[`4af336d741`](https://github.com/nodejs/node/commit/4af336d741)] - **(SEMVER-MINOR)** **src,test**: add full-featured embedder API test (Anna Henningsen) [#30467](https://github.com/nodejs/node/pull/30467)
* \[[`d44b05b18c`](https://github.com/nodejs/node/commit/d44b05b18c)] - **stream**: allow using `.push()`/`.unshift()` during `once('data')` (Anna Henningsen) [#34957](https://github.com/nodejs/node/pull/34957)
* \[[`2e77a10d9c`](https://github.com/nodejs/node/commit/2e77a10d9c)] - **stream**: pipeline should use req.abort() to destroy response (Robert Nagy) [#31054](https://github.com/nodejs/node/pull/31054)
* \[[`2f67e99a0b`](https://github.com/nodejs/node/commit/2f67e99a0b)] - **test**: add arrayOfStreams to pipeline (rickyes) [#34156](https://github.com/nodejs/node/pull/34156)
* \[[`3598056ac1`](https://github.com/nodejs/node/commit/3598056ac1)] - **test**: add vm crash regression test (Anna Henningsen) [#34673](https://github.com/nodejs/node/pull/34673)
* \[[`8545fb2aa9`](https://github.com/nodejs/node/commit/8545fb2aa9)] - **test**: add common/udppair utility (James M Snell) [#33380](https://github.com/nodejs/node/pull/33380)
* \[[`232f6e1154`](https://github.com/nodejs/node/commit/232f6e1154)] - **test**: AsyncLocalStorage works with thenables (Gerhard Stoebich) [#34008](https://github.com/nodejs/node/pull/34008)
* \[[`4cd7f5f147`](https://github.com/nodejs/node/commit/4cd7f5f147)] - **test**: add non-ASCII character embedding test (Anna Henningsen) [#33972](https://github.com/nodejs/node/pull/33972)
* \[[`b0c1acafda`](https://github.com/nodejs/node/commit/b0c1acafda)] - **test**: verify threadId in reports (Dylan Coakley) [#31556](https://github.com/nodejs/node/pull/31556)
* \[[`bd71cdf153`](https://github.com/nodejs/node/commit/bd71cdf153)] - **test**: use common.buildType in embedding test (Anna Henningsen) [#32422](https://github.com/nodejs/node/pull/32422)
* \[[`bdf6d41c72`](https://github.com/nodejs/node/commit/bdf6d41c72)] - **test**: use InitializeNodeWithArgs in cctest (Anna Henningsen) [#32406](https://github.com/nodejs/node/pull/32406)
* \[[`61eec0c6c7`](https://github.com/nodejs/node/commit/61eec0c6c7)] - **test**: wait for message from parent in embedding cctest (Anna Henningsen) [#32563](https://github.com/nodejs/node/pull/32563)
* \[[`cb635c2dc0`](https://github.com/nodejs/node/commit/cb635c2dc0)] - **(SEMVER-MINOR)** **test**: add extended embedder cctest (Anna Henningsen) [#30467](https://github.com/nodejs/node/pull/30467)
* \[[`f325c9544f`](https://github.com/nodejs/node/commit/f325c9544f)] - **(SEMVER-MINOR)** **test**: re-enable cctest that was commented out (Anna Henningsen) [#30467](https://github.com/nodejs/node/pull/30467)
* \[[`5a6bdd040d`](https://github.com/nodejs/node/commit/5a6bdd040d)] - **test**: improve assertions in pummel/test-timers (Rich Trott) [#35216](https://github.com/nodejs/node/pull/35216)
* \[[`942551e46f`](https://github.com/nodejs/node/commit/942551e46f)] - **test**: improve pummel/test-timers.js (Rich Trott) [#35175](https://github.com/nodejs/node/pull/35175)
* \[[`43c0174867`](https://github.com/nodejs/node/commit/43c0174867)] - **test**: revise test-policy-integrity (Rich Trott) [#35101](https://github.com/nodejs/node/pull/35101)
* \[[`d60c487c53`](https://github.com/nodejs/node/commit/d60c487c53)] - **test**: remove setMaxListeners in test-crypto-random (Tobias Nießen) [#35079](https://github.com/nodejs/node/pull/35079)
* \[[`867c4516af`](https://github.com/nodejs/node/commit/867c4516af)] - **test**: add regression tests for HTTP parser crash (Anna Henningsen) [#34251](https://github.com/nodejs/node/pull/34251)
* \[[`627e484e62`](https://github.com/nodejs/node/commit/627e484e62)] - **test**: use mustCall() in test-http-timeout (Pooja D.P) [#34996](https://github.com/nodejs/node/pull/34996)
* \[[`cd4b2aa891`](https://github.com/nodejs/node/commit/cd4b2aa891)] - **test**: change var to let (Pooja D.P) [#34902](https://github.com/nodejs/node/pull/34902)
* \[[`0bd176896e`](https://github.com/nodejs/node/commit/0bd176896e)] - **test**: remove incorrect debug() in test-policy-integrity (Rich Trott) [#34961](https://github.com/nodejs/node/pull/34961)
* \[[`327d00997d`](https://github.com/nodejs/node/commit/327d00997d)] - **test**: fix typo in test/parallel/test-icu-punycode.js (Daijiro Wachi) [#34934](https://github.com/nodejs/node/pull/34934)
* \[[`3fd7889e30`](https://github.com/nodejs/node/commit/3fd7889e30)] - **test**: add readline test for escape sequence (Rich Trott) [#34952](https://github.com/nodejs/node/pull/34952)
* \[[`46f94f9111`](https://github.com/nodejs/node/commit/46f94f9111)] - **test**: make test-tls-reuse-host-from-socket pass without internet (Rich Trott) [#34953](https://github.com/nodejs/node/pull/34953)
* \[[`76d991cf6b`](https://github.com/nodejs/node/commit/76d991cf6b)] - **test**: simplify test-vm-memleak (Rich Trott) [#34881](https://github.com/nodejs/node/pull/34881)
* \[[`d016cdcaa9`](https://github.com/nodejs/node/commit/d016cdcaa9)] - **test**: fix test-cluster-net-listen-relative-path.js to run in / (Rich Trott) [#34820](https://github.com/nodejs/node/pull/34820)
* \[[`cc98103802`](https://github.com/nodejs/node/commit/cc98103802)] - **test**: run REPL preview test regardless of terminal type (Rich Trott) [#34798](https://github.com/nodejs/node/pull/34798)
* \[[`4661b887cf`](https://github.com/nodejs/node/commit/4661b887cf)] - **test**: modernize test-cluster-master-error (Anna Henningsen) [#34685](https://github.com/nodejs/node/pull/34685)
* \[[`a4d50de661`](https://github.com/nodejs/node/commit/a4d50de661)] - **test**: move test-inspector-already-activated-cli to parallel (Rich Trott) [#34755](https://github.com/nodejs/node/pull/34755)
* \[[`4b22d335d1`](https://github.com/nodejs/node/commit/4b22d335d1)] - **test**: move execution of WPT to worker threads (Michaël Zasso) [#34796](https://github.com/nodejs/node/pull/34796)
* \[[`ac776f43f4`](https://github.com/nodejs/node/commit/ac776f43f4)] - **test**: convert assertion that always fails to assert.fail() (Rich Trott) [#34793](https://github.com/nodejs/node/pull/34793)
* \[[`a0ba41685b`](https://github.com/nodejs/node/commit/a0ba41685b)] - **test**: remove common.rootDir (Rich Trott) [#34772](https://github.com/nodejs/node/pull/34772)
* \[[`5352cde7ee`](https://github.com/nodejs/node/commit/5352cde7ee)] - **test**: allow ENOENT in test-worker-init-failure (Rich Trott) [#34769](https://github.com/nodejs/node/pull/34769)
* \[[`238d01f62f`](https://github.com/nodejs/node/commit/238d01f62f)] - **test**: allow ENFILE in test-worker-init-failure (Rich Trott) [#34769](https://github.com/nodejs/node/pull/34769)
* \[[`9cde4eb73a`](https://github.com/nodejs/node/commit/9cde4eb73a)] - **test**: use process.env.PYTHON to spawn python (Anna Henningsen) [#34700](https://github.com/nodejs/node/pull/34700)
* \[[`b4d9e0da6b`](https://github.com/nodejs/node/commit/b4d9e0da6b)] - **test**: remove error message checking in test-worker-init-failure (Rich Trott) [#34727](https://github.com/nodejs/node/pull/34727)
* \[[`335b61ac74`](https://github.com/nodejs/node/commit/335b61ac74)] - **test**: skip node-api/test\_worker\_terminate\_finalization (Anna Henningsen) [#34732](https://github.com/nodejs/node/pull/34732)
* \[[`e23f7ee1b9`](https://github.com/nodejs/node/commit/e23f7ee1b9)] - **test**: fix test\_worker\_terminate\_finalization (Anna Henningsen) [#34726](https://github.com/nodejs/node/pull/34726)
* \[[`b77309fe37`](https://github.com/nodejs/node/commit/b77309fe37)] - **test**: split test-crypto-dh-hash (Rich Trott) [#34631](https://github.com/nodejs/node/pull/34631)
* \[[`aa24b4a69d`](https://github.com/nodejs/node/commit/aa24b4a69d)] - **test**: use block-scoping in test/pummel/test-timers.js (Rich Trott) [#34630](https://github.com/nodejs/node/pull/34630)
* \[[`e30ddacddb`](https://github.com/nodejs/node/commit/e30ddacddb)] - **test**: remove test-child-process-fork-args flaky designation (Rich Trott) [#34684](https://github.com/nodejs/node/pull/34684)
* \[[`7eb80403b5`](https://github.com/nodejs/node/commit/7eb80403b5)] - **test**: add debugging for callbacks in test-https-foafssl.js (Rich Trott) [#34603](https://github.com/nodejs/node/pull/34603)
* \[[`4dbc787a2f`](https://github.com/nodejs/node/commit/4dbc787a2f)] - **test**: add debugging for test-https-foafssl.js (Rich Trott) [#34603](https://github.com/nodejs/node/pull/34603)
* \[[`71ee48863a`](https://github.com/nodejs/node/commit/71ee48863a)] - **test**: change Fixes: to Refs: (Rich Trott) [#34568](https://github.com/nodejs/node/pull/34568)
* \[[`09a6cefa94`](https://github.com/nodejs/node/commit/09a6cefa94)] - **test**: remove unneeded flag check in test-vm-memleak (Rich Trott) [#34528](https://github.com/nodejs/node/pull/34528)
* \[[`17973b7d7c`](https://github.com/nodejs/node/commit/17973b7d7c)] - **test**: add ref comment to test-regress-GH-814\_2 (Rich Trott) [#34516](https://github.com/nodejs/node/pull/34516)
* \[[`f6c674029d`](https://github.com/nodejs/node/commit/f6c674029d)] - **test**: add ref comment to test-regress-GH-814 (Rich Trott) [#34516](https://github.com/nodejs/node/pull/34516)
* \[[`d8c5bdaa08`](https://github.com/nodejs/node/commit/d8c5bdaa08)] - **test**: remove superfluous check in pummel/test-timers (Rich Trott) [#34488](https://github.com/nodejs/node/pull/34488)
* \[[`afd6e46772`](https://github.com/nodejs/node/commit/afd6e46772)] - **test**: fix test-heapdump-zlib (Andrey Pechkurov) [#34499](https://github.com/nodejs/node/pull/34499)
* \[[`72e0df3734`](https://github.com/nodejs/node/commit/72e0df3734)] - **test**: remove duplicate checks in pummel/test-timers (Rich Trott) [#34473](https://github.com/nodejs/node/pull/34473)
* \[[`4d4aa9a859`](https://github.com/nodejs/node/commit/4d4aa9a859)] - **test**: delete invalid test (Anna Henningsen) [#34445](https://github.com/nodejs/node/pull/34445)
* \[[`967334b9dc`](https://github.com/nodejs/node/commit/967334b9dc)] - **test**: fixup worker + source map test (Anna Henningsen) [#34446](https://github.com/nodejs/node/pull/34446)
* \[[`26c5f9febd`](https://github.com/nodejs/node/commit/26c5f9febd)] - **test**: force resigning of app (Colin Ihrig) [#34331](https://github.com/nodejs/node/pull/34331)
* \[[`8cb306e5a4`](https://github.com/nodejs/node/commit/8cb306e5a4)] - **test**: fix flaky test-watch-file (Rich Trott) [#34420](https://github.com/nodejs/node/pull/34420)
* \[[`cc2643188f`](https://github.com/nodejs/node/commit/cc2643188f)] - **test**: fix flaky test-heapdump-http2 (Rich Trott) [#34415](https://github.com/nodejs/node/pull/34415)
* \[[`2137024a55`](https://github.com/nodejs/node/commit/2137024a55)] - **test**: do not write to fixtures dir in test-watch-file (Rich Trott) [#34376](https://github.com/nodejs/node/pull/34376)
* \[[`95b2a39cf6`](https://github.com/nodejs/node/commit/95b2a39cf6)] - **test**: remove common.localhostIPv6 (Rich Trott) [#34373](https://github.com/nodejs/node/pull/34373)
* \[[`2ab3fccdbc`](https://github.com/nodejs/node/commit/2ab3fccdbc)] - **test**: fix test-net-pingpong pummel test for non-IPv6 hosts (Rich Trott) [#34359](https://github.com/nodejs/node/pull/34359)
* \[[`c3ac5e945c`](https://github.com/nodejs/node/commit/c3ac5e945c)] - **test**: fix flaky test-net-connect-econnrefused (Rich Trott) [#34330](https://github.com/nodejs/node/pull/34330)
* \[[`bd3cef7e0f`](https://github.com/nodejs/node/commit/bd3cef7e0f)] - **test**: use mustCall() in pummel test (Rich Trott) [#34327](https://github.com/nodejs/node/pull/34327)
* \[[`9741510336`](https://github.com/nodejs/node/commit/9741510336)] - **test**: fix flaky test-http2-reset-flood (Rich Trott) [#34318](https://github.com/nodejs/node/pull/34318)
* \[[`ed651374a4`](https://github.com/nodejs/node/commit/ed651374a4)] - **test**: add n-api null checks for conversions (Gabriel Schulhof) [#34142](https://github.com/nodejs/node/pull/34142)
* \[[`55ba743600`](https://github.com/nodejs/node/commit/55ba743600)] - **test**: add WASI test for file resizing (Colin Ihrig) [#31617](https://github.com/nodejs/node/pull/31617)
* \[[`4ae34e8ea8`](https://github.com/nodejs/node/commit/4ae34e8ea8)] - **test**: skip an ipv6 test on IBM i (Xu Meng) [#34209](https://github.com/nodejs/node/pull/34209)
* \[[`b7ae73bfe2`](https://github.com/nodejs/node/commit/b7ae73bfe2)] - **test**: add regression test for C++-created Buffer transfer (Anna Henningsen) [#34140](https://github.com/nodejs/node/pull/34140)
* \[[`235417039f`](https://github.com/nodejs/node/commit/235417039f)] - **test**: replace deprecated function call from test-repl-history-navigation (Rich Trott) [#34199](https://github.com/nodejs/node/pull/34199)
* \[[`44246e6701`](https://github.com/nodejs/node/commit/44246e6701)] - **test**: skip some IBM i unsupported test cases (Xu Meng) [#34118](https://github.com/nodejs/node/pull/34118)
* \[[`bb542176b0`](https://github.com/nodejs/node/commit/bb542176b0)] - **test**: report actual error code on failure (Richard Lau) [#34134](https://github.com/nodejs/node/pull/34134)
* \[[`09a12892e1`](https://github.com/nodejs/node/commit/09a12892e1)] - **test**: update test-child-process-spawn-loop for Python 3 (Richard Lau) [#34071](https://github.com/nodejs/node/pull/34071)
* \[[`26ede7f295`](https://github.com/nodejs/node/commit/26ede7f295)] - **test,doc**: add missing uv\_setup\_args() calls (Colin Ihrig) [#34751](https://github.com/nodejs/node/pull/34751)
* \[[`987e0cb785`](https://github.com/nodejs/node/commit/987e0cb785)] - **(SEMVER-MINOR)** **timers**: allow timers to be used as primitives (Denys Otrishko) [#34017](https://github.com/nodejs/node/pull/34017)
* \[[`9b27933549`](https://github.com/nodejs/node/commit/9b27933549)] - **(SEMVER-MINOR)** **tls**: make 'createSecureContext' honor more options (Mateusz Krawczuk) [#33974](https://github.com/nodejs/node/pull/33974)
* \[[`c059d3d287`](https://github.com/nodejs/node/commit/c059d3d287)] - **tls**: enable renegotiation when using BoringSSL (Jeremy Rose) [#34832](https://github.com/nodejs/node/pull/34832)
* \[[`bcc0913564`](https://github.com/nodejs/node/commit/bcc0913564)] - **tls**: remove setMaxSendFragment guards (Tobias Nießen) [#34323](https://github.com/nodejs/node/pull/34323)
* \[[`68654da30d`](https://github.com/nodejs/node/commit/68654da30d)] - **tls**: remove unnecessary close listener (Robert Nagy) [#34105](https://github.com/nodejs/node/pull/34105)
* \[[`55ed2d2280`](https://github.com/nodejs/node/commit/55ed2d2280)] - **tools**: update ESLint to 7.9.0 (Colin Ihrig) [#35170](https://github.com/nodejs/node/pull/35170)
* \[[`a3c59d8707`](https://github.com/nodejs/node/commit/a3c59d8707)] - **tools**: fix docopen target (Antoine du HAMEL) [#35062](https://github.com/nodejs/node/pull/35062)
* \[[`6d6c6fa929`](https://github.com/nodejs/node/commit/6d6c6fa929)] - **tools**: fix doc build targets (Antoine du HAMEL) [#35060](https://github.com/nodejs/node/pull/35060)
* \[[`1dce35d04a`](https://github.com/nodejs/node/commit/1dce35d04a)] - **tools**: add banner to lint-md.js by rollup.config.js (KuthorX) [#34233](https://github.com/nodejs/node/pull/34233)
* \[[`0f6102065e`](https://github.com/nodejs/node/commit/0f6102065e)] - **tools**: update ESLint to 7.8.1 (Colin Ihrig) [#35004](https://github.com/nodejs/node/pull/35004)
* \[[`eeb8a4aaa0`](https://github.com/nodejs/node/commit/eeb8a4aaa0)] - **tools**: update ESLint to 7.8.0 (Colin Ihrig) [#35004](https://github.com/nodejs/node/pull/35004)
* \[[`b4b0dcd43e`](https://github.com/nodejs/node/commit/b4b0dcd43e)] - **tools**: add debug entitlements for macOS 10.15+ (Gabriele Greco) [#34378](https://github.com/nodejs/node/pull/34378)
* \[[`a92aec137e`](https://github.com/nodejs/node/commit/a92aec137e)] - **tools**: update ESLint to 7.6.0 (Colin Ihrig) [#34589](https://github.com/nodejs/node/pull/34589)
* \[[`155f706ad0`](https://github.com/nodejs/node/commit/155f706ad0)] - **tools**: add meta.fixable to fixable lint rules (Colin Ihrig) [#34589](https://github.com/nodejs/node/pull/34589)
* \[[`aa15abb2be`](https://github.com/nodejs/node/commit/aa15abb2be)] - **tools**: update ESLint to 7.5.0 (Colin Ihrig) [#34423](https://github.com/nodejs/node/pull/34423)
* \[[`0507535277`](https://github.com/nodejs/node/commit/0507535277)] - **tools**: remove lint-js.js (Rich Trott) [#30955](https://github.com/nodejs/node/pull/30955)
* \[[`fed08a8e49`](https://github.com/nodejs/node/commit/fed08a8e49)] - **tools,doc**: allow page titles to contain inline code (Antoine du HAMEL) [#35003](https://github.com/nodejs/node/pull/35003)
* \[[`0ec3e6138e`](https://github.com/nodejs/node/commit/0ec3e6138e)] - **tools,doc**: fix global table of content active element (Antoine du Hamel) [#34976](https://github.com/nodejs/node/pull/34976)
* \[[`4a0c01e3d5`](https://github.com/nodejs/node/commit/4a0c01e3d5)] - **tools,doc**: remove "toc" anchor name (Rich Trott) [#34893](https://github.com/nodejs/node/pull/34893)
* \[[`8d0c21fd24`](https://github.com/nodejs/node/commit/8d0c21fd24)] - **util**: restrict custom inspect function + vm.Context interaction (Anna Henningsen) [#33690](https://github.com/nodejs/node/pull/33690)
* \[[`9027a87f62`](https://github.com/nodejs/node/commit/9027a87f62)] - **util**: print External address from inspect (unknown) [#34398](https://github.com/nodejs/node/pull/34398)
* \[[`58cd76cb04`](https://github.com/nodejs/node/commit/58cd76cb04)] - **util**: improve getStringWidth performance (Ruben Bridgewater) [#33674](https://github.com/nodejs/node/pull/33674)
* \[[`7f51e79511`](https://github.com/nodejs/node/commit/7f51e79511)] - **vm**: add tests for function declarations using \[\[DefineOwnProperty]] (ExE Boss) [#34032](https://github.com/nodejs/node/pull/34032)
* \[[`4913051ba6`](https://github.com/nodejs/node/commit/4913051ba6)] - **wasi**: add \_\_wasi\_fd\_filestat\_set\_times() test (Colin Ihrig) [#34623](https://github.com/nodejs/node/pull/34623)
* \[[`2e95550476`](https://github.com/nodejs/node/commit/2e95550476)] - **wasi**: add reactor support (Gus Caplan) [#34046](https://github.com/nodejs/node/pull/34046)
* \[[`139442c34e`](https://github.com/nodejs/node/commit/139442c34e)] - **(SEMVER-MINOR)** **worker**: add public method for marking objects as untransferable (Anna Henningsen) [#33979](https://github.com/nodejs/node/pull/33979)
* \[[`44864d7385`](https://github.com/nodejs/node/commit/44864d7385)] - **worker**: do not crash when JSTransferable lists untransferable value (Anna Henningsen) [#34766](https://github.com/nodejs/node/pull/34766)
* \[[`dafa380732`](https://github.com/nodejs/node/commit/dafa380732)] - **(SEMVER-MINOR)** **worker**: emit `'messagerror'` events for failed deserialization (Anna Henningsen) [#33772](https://github.com/nodejs/node/pull/33772)
* \[[`0d35eaa034`](https://github.com/nodejs/node/commit/0d35eaa034)] - **(SEMVER-MINOR)** **worker**: allow passing JS wrapper objects via postMessage (Anna Henningsen) [#33772](https://github.com/nodejs/node/pull/33772)
* \[[`8e1698a784`](https://github.com/nodejs/node/commit/8e1698a784)] - **(SEMVER-MINOR)** **worker**: allow transferring/cloning generic BaseObjects (Anna Henningsen) [#33772](https://github.com/nodejs/node/pull/33772)
* \[[`b4819dba5c`](https://github.com/nodejs/node/commit/b4819dba5c)] - **(SEMVER-MINOR)** **worker**: add option to track unmanaged file descriptors (Anna Henningsen) [#34303](https://github.com/nodejs/node/pull/34303)
* \[[`5e9f0cfa62`](https://github.com/nodejs/node/commit/5e9f0cfa62)] - **worker**: fix --abort-on-uncaught-exception handling (Anna Henningsen) [#34724](https://github.com/nodejs/node/pull/34724)
* \[[`9173b09445`](https://github.com/nodejs/node/commit/9173b09445)] - **(SEMVER-MINOR)** **worker**: add stack size resource limit option (Anna Henningsen) [#33085](https://github.com/nodejs/node/pull/33085)
* \[[`18ecaebdbb`](https://github.com/nodejs/node/commit/18ecaebdbb)] - **worker**: unify custom error creation (Anna Henningsen) [#33084](https://github.com/nodejs/node/pull/33084)
* \[[`c31b6bff34`](https://github.com/nodejs/node/commit/c31b6bff34)] - **worker**: fix nested uncaught exception handling (Anna Henningsen) [#34310](https://github.com/nodejs/node/pull/34310)
* \[[`dd51ba3f93`](https://github.com/nodejs/node/commit/dd51ba3f93)] - **(SEMVER-MINOR)** **worker,fs**: make FileHandle transferable (Anna Henningsen) [#33772](https://github.com/nodejs/node/pull/33772)
* \[[`1b24d3a552`](https://github.com/nodejs/node/commit/1b24d3a552)] - **zlib**: remove redundant variable in zlibBufferOnEnd (Andrey Pechkurov) [#34072](https://github.com/nodejs/node/pull/34072)
* \[[`33b22d7c4f`](https://github.com/nodejs/node/commit/33b22d7c4f)] - **(SEMVER-MINOR)** **zlib**: add `maxOutputLength` option (unknown) [#33516](https://github.com/nodejs/node/pull/33516)
* \[[`cda459ecb0`](https://github.com/nodejs/node/commit/cda459ecb0)] - **zlib**: replace usage of internal stream state with public api (Denys Otrishko) [#34884](https://github.com/nodejs/node/pull/34884)
* \[[`d60b13f2e3`](https://github.com/nodejs/node/commit/d60b13f2e3)] - **zlib**: switch to lazy init for zlib streams (Andrey Pechkurov) [#34048](https://github.com/nodejs/node/pull/34048)

<a id="12.18.4"></a>

## 2020-09-15, Version 12.18.4 'Erbium' (LTS), @BethGriggs prepared by @targos

### Notable Changes

This is a security release.

Vulnerabilities fixed:

* **CVE-2020-8201**: HTTP Request Smuggling due to CR-to-Hyphen conversion (High).
* **CVE-2020-8252**: fs.realpath.native on may cause buffer overflow (Medium).

### Commits

* \[[`2ea6d255f8`](https://github.com/nodejs/node/commit/2ea6d255f8)] - **deps**: libuv: cherry-pick 0e6e8620 (cjihrig) [nodejs-private/node-private#221](https://github.com/nodejs-private/node-private/pull/221)
* \[[`65415049af`](https://github.com/nodejs/node/commit/65415049af)] - **deps**: update llhttp to 2.1.2 (Fedor Indutny) [nodejs-private/node-private#219](https://github.com/nodejs-private/node-private/pull/219)
* \[[`edad52e243`](https://github.com/nodejs/node/commit/edad52e243)] - **test**: modify tests to support the latest llhttp (Fedor Indutny) [nodejs-private/node-private#219](https://github.com/nodejs-private/node-private/pull/219)

<a id="12.18.3"></a>

## 2020-07-22, Version 12.18.3 'Erbium' (LTS), @codebytere

### Notable Changes

* **deps:**
  * upgrade npm to 6.14.6 (claudiahdz) [#34246](https://github.com/nodejs/node/pull/34246)
  * update node-inspect to v2.0.0 (Jan Krems) [#33447](https://github.com/nodejs/node/pull/33447)
  * uvwasi: cherry-pick 9e75217 (Colin Ihrig) [#33521](https://github.com/nodejs/node/pull/33521)

### Commits

* \[[`0d79c533ef`](https://github.com/nodejs/node/commit/0d79c533ef)] - **async\_hooks**: callback trampoline for MakeCallback (Stephen Belanger) [#33801](https://github.com/nodejs/node/pull/33801)
* \[[`bfffb977ad`](https://github.com/nodejs/node/commit/bfffb977ad)] - **benchmark**: fix async-resource benchmark (Anna Henningsen) [#33642](https://github.com/nodejs/node/pull/33642)
* \[[`09277fa5e4`](https://github.com/nodejs/node/commit/09277fa5e4)] - **benchmark**: fixing http\_server\_for\_chunky\_client.js (Adrian Estrada) [#33271](https://github.com/nodejs/node/pull/33271)
* \[[`5a6d80f25f`](https://github.com/nodejs/node/commit/5a6d80f25f)] - **buffer**: remove hoisted variable (Nikolai Vavilov) [#33470](https://github.com/nodejs/node/pull/33470)
* \[[`e057189ee8`](https://github.com/nodejs/node/commit/e057189ee8)] - **build**: configure byte order for mips targets (Ben Noordhuis) [#33898](https://github.com/nodejs/node/pull/33898)
* \[[`d77eaeefb8`](https://github.com/nodejs/node/commit/d77eaeefb8)] - **build**: add target specific build\_type variable (Daniel Bevenius) [#33925](https://github.com/nodejs/node/pull/33925)
* \[[`d56585ec8d`](https://github.com/nodejs/node/commit/d56585ec8d)] - **build**: add LINT\_CPP\_FILES to checkimports check (Daniel Bevenius) [#33697](https://github.com/nodejs/node/pull/33697)
* \[[`a5ce90c46b`](https://github.com/nodejs/node/commit/a5ce90c46b)] - **build**: add --v8-lite-mode flag (Maciej Kacper Jagiełło) [#33541](https://github.com/nodejs/node/pull/33541)
* \[[`11dad02e50`](https://github.com/nodejs/node/commit/11dad02e50)] - **build**: fix python-version selection with actions (Richard Lau) [#33589](https://github.com/nodejs/node/pull/33589)
* \[[`bba41bf6e1`](https://github.com/nodejs/node/commit/bba41bf6e1)] - **build**: fix makefile script on windows (Thomas) [#33136](https://github.com/nodejs/node/pull/33136)
* \[[`817f6593ee`](https://github.com/nodejs/node/commit/817f6593ee)] - **configure**: account for CLANG\_VENDOR when checking for llvm version (Nathan Blair) [#33860](https://github.com/nodejs/node/pull/33860)
* \[[`a9c5b3348c`](https://github.com/nodejs/node/commit/a9c5b3348c)] - **console**: name console functions appropriately (Ruben Bridgewater) [#33524](https://github.com/nodejs/node/pull/33524)
* \[[`d8365bc71e`](https://github.com/nodejs/node/commit/d8365bc71e)] - **console**: mark special console properties as non-enumerable (Ruben Bridgewater) [#33524](https://github.com/nodejs/node/pull/33524)
* \[[`80782cb261`](https://github.com/nodejs/node/commit/80782cb261)] - **console**: remove dead code (Ruben Bridgewater) [#33524](https://github.com/nodejs/node/pull/33524)
* \[[`18dc03d6a5`](https://github.com/nodejs/node/commit/18dc03d6a5)] - **crypto**: fix wrong error message (Ben Bucksch) [#33482](https://github.com/nodejs/node/pull/33482)
* \[[`b64963e5c3`](https://github.com/nodejs/node/commit/b64963e5c3)] - **deps**: upgrade npm to 6.14.6 (claudiahdz) [#34246](https://github.com/nodejs/node/pull/34246)
* \[[`9ee9688fe0`](https://github.com/nodejs/node/commit/9ee9688fe0)] - **deps**: uvwasi: cherry-pick 9e75217 (Colin Ihrig) [#33521](https://github.com/nodejs/node/pull/33521)
* \[[`8803d7e8cf`](https://github.com/nodejs/node/commit/8803d7e8cf)] - **deps**: update node-inspect to v2.0.0 (Jan Krems) [#33447](https://github.com/nodejs/node/pull/33447)
* \[[`5d3f818e9e`](https://github.com/nodejs/node/commit/5d3f818e9e)] - **dns**: make dns.Resolver timeout configurable (Ben Noordhuis) [#33472](https://github.com/nodejs/node/pull/33472)
* \[[`10b88cb117`](https://github.com/nodejs/node/commit/10b88cb117)] - **dns**: use ternary operator simplify statement (Wenning Zhang) [#33234](https://github.com/nodejs/node/pull/33234)
* \[[`fbd6fe5839`](https://github.com/nodejs/node/commit/fbd6fe5839)] - **doc**: update code language flag for internal doc (Rich Trott) [#33852](https://github.com/nodejs/node/pull/33852)
* \[[`24fd15778a`](https://github.com/nodejs/node/commit/24fd15778a)] - **doc**: specify maxHeaderCount alias for maxHeaderListPairs (Pranshu Srivastava) [#33519](https://github.com/nodejs/node/pull/33519)
* \[[`04ceeaf5eb`](https://github.com/nodejs/node/commit/04ceeaf5eb)] - **doc**: add allowed info strings to style guide (Derek Lewis) [#34024](https://github.com/nodejs/node/pull/34024)
* \[[`ee36c87fd7`](https://github.com/nodejs/node/commit/ee36c87fd7)] - **doc**: clarify thread-safe function references (legendecas) [#33871](https://github.com/nodejs/node/pull/33871)
* \[[`30b5e76ffd`](https://github.com/nodejs/node/commit/30b5e76ffd)] - **doc**: use npm team for npm upgrades in collaborator guide (Rich Trott) [#33999](https://github.com/nodejs/node/pull/33999)
* \[[`06937249d0`](https://github.com/nodejs/node/commit/06937249d0)] - **doc**: correct default values in http2 docs (Rich Trott) [#33997](https://github.com/nodejs/node/pull/33997)
* \[[`498dfba33a`](https://github.com/nodejs/node/commit/498dfba33a)] - **doc**: use a single space between sentences (Rich Trott) [#33995](https://github.com/nodejs/node/pull/33995)
* \[[`47ea3067d0`](https://github.com/nodejs/node/commit/47ea3067d0)] - **doc**: revise text in dns module documentation introduction (Rich Trott) [#33986](https://github.com/nodejs/node/pull/33986)
* \[[`f29f77f111`](https://github.com/nodejs/node/commit/f29f77f111)] - **doc**: update fs.md (Shakil-Shahadat) [#33820](https://github.com/nodejs/node/pull/33820)
* \[[`ddc5afdddc`](https://github.com/nodejs/node/commit/ddc5afdddc)] - **doc**: warn that tls.connect() doesn't set SNI (Alba Mendez) [#33855](https://github.com/nodejs/node/pull/33855)
* \[[`732b80b474`](https://github.com/nodejs/node/commit/732b80b474)] - **doc**: fix lexical sorting of bottom-references in dns doc (Rich Trott) [#33987](https://github.com/nodejs/node/pull/33987)
* \[[`6af2ed3fdc`](https://github.com/nodejs/node/commit/6af2ed3fdc)] - **doc**: change "GitHub Repo" to "Code repository" (Rich Trott) [#33985](https://github.com/nodejs/node/pull/33985)
* \[[`322a51e582`](https://github.com/nodejs/node/commit/322a51e582)] - **doc**: use Class: consistently (Rich Trott) [#33978](https://github.com/nodejs/node/pull/33978)
* \[[`410b23398d`](https://github.com/nodejs/node/commit/410b23398d)] - **doc**: update WASM code sample (Pragyan Das) [#33626](https://github.com/nodejs/node/pull/33626)
* \[[`335f405f1b`](https://github.com/nodejs/node/commit/335f405f1b)] - **doc**: link readable.\_read in stream.md (Pranshu Srivastava) [#33767](https://github.com/nodejs/node/pull/33767)
* \[[`3789c28c89`](https://github.com/nodejs/node/commit/3789c28c89)] - **doc**: specify default encoding in writable.write (Pranshu Srivastava) [#33765](https://github.com/nodejs/node/pull/33765)
* \[[`5609b17e2d`](https://github.com/nodejs/node/commit/5609b17e2d)] - **doc**: move --force-context-aware option in cli.md (Daniel Bevenius) [#33823](https://github.com/nodejs/node/pull/33823)
* \[[`f39ee7d245`](https://github.com/nodejs/node/commit/f39ee7d245)] - **doc**: add snippet for AsyncResource and EE integration (Andrey Pechkurov) [#33751](https://github.com/nodejs/node/pull/33751)
* \[[`f8baeccaaa`](https://github.com/nodejs/node/commit/f8baeccaaa)] - **doc**: use single quotes in --tls-cipher-list (Daniel Bevenius) [#33709](https://github.com/nodejs/node/pull/33709)
* \[[`4654e2321b`](https://github.com/nodejs/node/commit/4654e2321b)] - **doc**: fix misc. mislabeled code block info strings (Derek Lewis) [#33548](https://github.com/nodejs/node/pull/33548)
* \[[`046dee6eb3`](https://github.com/nodejs/node/commit/046dee6eb3)] - **doc**: update V8 inspector example (Colin Ihrig) [#33758](https://github.com/nodejs/node/pull/33758)
* \[[`d547d1c1bc`](https://github.com/nodejs/node/commit/d547d1c1bc)] - **doc**: fix linting in doc-style-guide.md (Pranshu Srivastava) [#33787](https://github.com/nodejs/node/pull/33787)
* \[[`3b437416d5`](https://github.com/nodejs/node/commit/3b437416d5)] - **doc**: add formatting for version numbers to doc-style-guide.md (Rich Trott) [#33755](https://github.com/nodejs/node/pull/33755)
* \[[`b00996ce35`](https://github.com/nodejs/node/commit/b00996ce35)] - **doc**: remove "currently" from repl.md (Rich Trott) [#33756](https://github.com/nodejs/node/pull/33756)
* \[[`7595d15286`](https://github.com/nodejs/node/commit/7595d15286)] - **doc**: remove "currently" from vm.md (Rich Trott) [#33756](https://github.com/nodejs/node/pull/33756)
* \[[`36a8af7a5e`](https://github.com/nodejs/node/commit/36a8af7a5e)] - **doc**: remove "currently" from addons.md (Rich Trott) [#33756](https://github.com/nodejs/node/pull/33756)
* \[[`27e797687f`](https://github.com/nodejs/node/commit/27e797687f)] - **doc**: remove "currently" from util.md (Rich Trott) [#33756](https://github.com/nodejs/node/pull/33756)
* \[[`94ac13678d`](https://github.com/nodejs/node/commit/94ac13678d)] - **doc**: change "pre Node.js v0.10" to "prior to Node.js 0.10" (Rich Trott) [#33754](https://github.com/nodejs/node/pull/33754)
* \[[`f1a810880e`](https://github.com/nodejs/node/commit/f1a810880e)] - **doc**: normalize C++ code block info strings (Derek Lewis) [#33483](https://github.com/nodejs/node/pull/33483)
* \[[`289d0bf105`](https://github.com/nodejs/node/commit/289d0bf105)] - **doc**: remove default parameter value from header (Rich Trott) [#33752](https://github.com/nodejs/node/pull/33752)
* \[[`35cee03849`](https://github.com/nodejs/node/commit/35cee03849)] - **doc**: remove shell dollar signs without output (Nick Schonning) [#33692](https://github.com/nodejs/node/pull/33692)
* \[[`d10fac73a3`](https://github.com/nodejs/node/commit/d10fac73a3)] - **doc**: add lint disabling comment for collaborator list (Rich Trott) [#33719](https://github.com/nodejs/node/pull/33719)
* \[[`8dbf3349d0`](https://github.com/nodejs/node/commit/8dbf3349d0)] - **doc**: fix urls to avoid redirection (sapics) [#33614](https://github.com/nodejs/node/pull/33614)
* \[[`5416635677`](https://github.com/nodejs/node/commit/5416635677)] - **doc**: improve buffer.md a tiny bit (Tom Nagle) [#33547](https://github.com/nodejs/node/pull/33547)
* \[[`a3b6095db1`](https://github.com/nodejs/node/commit/a3b6095db1)] - **doc**: normalize Markdown code block info strings (Derek Lewis) [#33542](https://github.com/nodejs/node/pull/33542)
* \[[`4fcbfdc45c`](https://github.com/nodejs/node/commit/4fcbfdc45c)] - **doc**: normalize JavaScript code block info strings (Derek Lewis) [#33531](https://github.com/nodejs/node/pull/33531)
* \[[`543605782d`](https://github.com/nodejs/node/commit/543605782d)] - **doc**: outline when origin is set to unhandledRejection (Ruben Bridgewater) [#33530](https://github.com/nodejs/node/pull/33530)
* \[[`7dc28ab4d3`](https://github.com/nodejs/node/commit/7dc28ab4d3)] - **doc**: update txt fandamental and raw code blocks (Zeke Sikelianos) [#33028](https://github.com/nodejs/node/pull/33028)
* \[[`cf82adf87f`](https://github.com/nodejs/node/commit/cf82adf87f)] - **doc**: normalize Bash code block info strings (Derek Lewis) [#33510](https://github.com/nodejs/node/pull/33510)
* \[[`7ea6b07b90`](https://github.com/nodejs/node/commit/7ea6b07b90)] - **doc**: normalize shell code block info strings (Derek Lewis) [#33486](https://github.com/nodejs/node/pull/33486)
* \[[`74a1493441`](https://github.com/nodejs/node/commit/74a1493441)] - **doc**: normalize C code block info strings (Derek Lewis) [#33507](https://github.com/nodejs/node/pull/33507)
* \[[`281d7f74d8`](https://github.com/nodejs/node/commit/281d7f74d8)] - **doc**: correct tls.rootCertificates to match implementation (Eric Bickle) [#33313](https://github.com/nodejs/node/pull/33313)
* \[[`6133639d53`](https://github.com/nodejs/node/commit/6133639d53)] - **doc**: fix Buffer.from(object) documentation (Nikolai Vavilov) [#33327](https://github.com/nodejs/node/pull/33327)
* \[[`b599037f78`](https://github.com/nodejs/node/commit/b599037f78)] - **doc**: fix typo in pathToFileURL example (Antoine du HAMEL) [#33418](https://github.com/nodejs/node/pull/33418)
* \[[`78734c2698`](https://github.com/nodejs/node/commit/78734c2698)] - **doc**: eliminate dead space in API section's sidebar (John Gardner) [#33469](https://github.com/nodejs/node/pull/33469)
* \[[`c76ec4d007`](https://github.com/nodejs/node/commit/c76ec4d007)] - **doc**: fixed a grammatical error in path.md (Deep310) [#33489](https://github.com/nodejs/node/pull/33489)
* \[[`1b76377bce`](https://github.com/nodejs/node/commit/1b76377bce)] - **doc**: correct CommonJS self-resolve spec (Guy Bedford) [#33391](https://github.com/nodejs/node/pull/33391)
* \[[`70d025f510`](https://github.com/nodejs/node/commit/70d025f510)] - **doc**: standardize on sentence case for headers (Rich Trott) [#33889](https://github.com/nodejs/node/pull/33889)
* \[[`3e68d21c6f`](https://github.com/nodejs/node/commit/3e68d21c6f)] - **doc**: use sentence-case for headings in docs (Rich Trott) [#33889](https://github.com/nodejs/node/pull/33889)
* \[[`dfa8028254`](https://github.com/nodejs/node/commit/dfa8028254)] - **doc**: fix readline key binding documentation (Ruben Bridgewater) [#33361](https://github.com/nodejs/node/pull/33361)
* \[[`6f8b7a85d2`](https://github.com/nodejs/node/commit/6f8b7a85d2)] - **doc,tools**: properly syntax highlight API ref docs (Derek Lewis) [#33442](https://github.com/nodejs/node/pull/33442)
* \[[`43d1d89d27`](https://github.com/nodejs/node/commit/43d1d89d27)] - **domain**: fix unintentional deprecation warning (Anna Henningsen) [#34245](https://github.com/nodejs/node/pull/34245)
* \[[`ba476326dd`](https://github.com/nodejs/node/commit/ba476326dd)] - **domain**: remove native domain code (Stephen Belanger) [#33801](https://github.com/nodejs/node/pull/33801)
* \[[`76b06e53c6`](https://github.com/nodejs/node/commit/76b06e53c6)] - **errors**: fully inspect errors on exit (Ruben Bridgewater) [#33523](https://github.com/nodejs/node/pull/33523)
* \[[`9111fab663`](https://github.com/nodejs/node/commit/9111fab663)] - **esm**: fix loader hooks doc annotations (Derek Lewis) [#33563](https://github.com/nodejs/node/pull/33563)
* \[[`3559471153`](https://github.com/nodejs/node/commit/3559471153)] - **esm**: share package.json cache between ESM and CJS loaders (Kirill Shatskiy) [#33229](https://github.com/nodejs/node/pull/33229)
* \[[`d09f6d55c7`](https://github.com/nodejs/node/commit/d09f6d55c7)] - **esm**: doc & validate source values for formats (Bradley Farias) [#32202](https://github.com/nodejs/node/pull/32202)
* \[[`a76fa60c63`](https://github.com/nodejs/node/commit/a76fa60c63)] - **fs**: fix readdir failure when libuv returns UV\_DIRENT\_UNKNOWN (Kirill Shatskiy) [#33395](https://github.com/nodejs/node/pull/33395)
* \[[`b92c0cb15c`](https://github.com/nodejs/node/commit/b92c0cb15c)] - **fs**: fix realpath inode link caching (Denys Otrishko) [#33945](https://github.com/nodejs/node/pull/33945)
* \[[`04fa6d675f`](https://github.com/nodejs/node/commit/04fa6d675f)] - **fs**: close file descriptor of promisified truncate (João Reis) [#34239](https://github.com/nodejs/node/pull/34239)
* \[[`c9cf41d841`](https://github.com/nodejs/node/commit/c9cf41d841)] - **fs**: support util.promisify for fs.readv (Lucas Holmquist) [#33590](https://github.com/nodejs/node/pull/33590)
* \[[`adb93f153b`](https://github.com/nodejs/node/commit/adb93f153b)] - **fs**: unify style in preprocessSymlinkDestination (Bartosz Sosnowski) [#33496](https://github.com/nodejs/node/pull/33496)
* \[[`5fb1cc8cc1`](https://github.com/nodejs/node/commit/5fb1cc8cc1)] - **fs**: replace checkPosition with validateInteger (rickyes) [#33277](https://github.com/nodejs/node/pull/33277)
* \[[`75107e23a8`](https://github.com/nodejs/node/commit/75107e23a8)] - **http2**: always call callback on Http2ServerResponse#end (Pranshu Srivastava) [#33911](https://github.com/nodejs/node/pull/33911)
* \[[`0f0720a665`](https://github.com/nodejs/node/commit/0f0720a665)] - **http2**: add writable\* properties to compat api (Pranshu Srivastava) [#33506](https://github.com/nodejs/node/pull/33506)
* \[[`8def93429e`](https://github.com/nodejs/node/commit/8def93429e)] - **http2**: add type checks for Http2ServerResponse.end (Pranshu Srivastava) [#33146](https://github.com/nodejs/node/pull/33146)
* \[[`a3b7e5992d`](https://github.com/nodejs/node/commit/a3b7e5992d)] - **http2**: use `Object.create(null)` for `getHeaders` (Pranshu Srivastava) [#33188](https://github.com/nodejs/node/pull/33188)
* \[[`bcdf4c808d`](https://github.com/nodejs/node/commit/bcdf4c808d)] - **http2**: reuse .\_onTimeout() in Http2Session and Http2Stream classes (rickyes) [#33354](https://github.com/nodejs/node/pull/33354)
* \[[`103a9af673`](https://github.com/nodejs/node/commit/103a9af673)] - **inspector**: drop 'chrome-' from inspector url (Colin Ihrig) [#33758](https://github.com/nodejs/node/pull/33758)
* \[[`0941635bb5`](https://github.com/nodejs/node/commit/0941635bb5)] - **inspector**: throw error when activating an already active inspector (Joyee Cheung) [#33015](https://github.com/nodejs/node/pull/33015)
* \[[`0197ea4e56`](https://github.com/nodejs/node/commit/0197ea4e56)] - **lib**: replace charCodeAt with fixed Unicode (rickyes) [#32758](https://github.com/nodejs/node/pull/32758)
* \[[`69291e4b7d`](https://github.com/nodejs/node/commit/69291e4b7d)] - **lib**: add Int16Array primordials (Sebastien Ahkrin) [#31205](https://github.com/nodejs/node/pull/31205)
* \[[`83c9364bf1`](https://github.com/nodejs/node/commit/83c9364bf1)] - **lib**: update TODO comments (Ruben Bridgewater) [#33361](https://github.com/nodejs/node/pull/33361)
* \[[`a94e7dabcc`](https://github.com/nodejs/node/commit/a94e7dabcc)] - **lib**: update executionAsyncId/triggerAsyncId comment (Daniel Bevenius) [#33396](https://github.com/nodejs/node/pull/33396)
* \[[`857ff68485`](https://github.com/nodejs/node/commit/857ff68485)] - **meta**: introduce codeowners again (James M Snell) [#33895](https://github.com/nodejs/node/pull/33895)
* \[[`f534ac06bd`](https://github.com/nodejs/node/commit/f534ac06bd)] - **meta**: fix a typo in the flaky test template (Colin Ihrig) [#33677](https://github.com/nodejs/node/pull/33677)
* \[[`1376c3bab2`](https://github.com/nodejs/node/commit/1376c3bab2)] - **meta**: wrap flaky test template at 80 characters (Colin Ihrig) [#33677](https://github.com/nodejs/node/pull/33677)
* \[[`b7ea7be2a8`](https://github.com/nodejs/node/commit/b7ea7be2a8)] - **meta**: add flaky test issue template (Ash Cripps) [#33500](https://github.com/nodejs/node/pull/33500)
* \[[`0867ab7da5`](https://github.com/nodejs/node/commit/0867ab7da5)] - **module**: fix error message about importing names from cjs (Fábio Santos) [#33882](https://github.com/nodejs/node/pull/33882)
* \[[`47f5eeb0d5`](https://github.com/nodejs/node/commit/47f5eeb0d5)] - **n-api**: add version to wasm registration (Gus Caplan) [#34045](https://github.com/nodejs/node/pull/34045)
* \[[`2e97d82509`](https://github.com/nodejs/node/commit/2e97d82509)] - **n-api**: document nextTick timing in callbacks (Mathias Buus) [#33804](https://github.com/nodejs/node/pull/33804)
* \[[`90ddf0aa2e`](https://github.com/nodejs/node/commit/90ddf0aa2e)] - **n-api**: ensure scope present for finalization (Michael Dawson) [#33508](https://github.com/nodejs/node/pull/33508)
* \[[`ed741ecb1e`](https://github.com/nodejs/node/commit/ed741ecb1e)] - **n-api**: remove `napi_env::CallIntoModuleThrow` (Gabriel Schulhof) [#33570](https://github.com/nodejs/node/pull/33570)
* \[[`0a949c3f93`](https://github.com/nodejs/node/commit/0a949c3f93)] - **napi**: add \_\_wasm32\_\_ guards (Gus Caplan) [#33597](https://github.com/nodejs/node/pull/33597)
* \[[`7c7f5c8869`](https://github.com/nodejs/node/commit/7c7f5c8869)] - **net**: refactor check for Windows (rickyes) [#33497](https://github.com/nodejs/node/pull/33497)
* \[[`578e731321`](https://github.com/nodejs/node/commit/578e731321)] - **querystring**: fix stringify for empty array (sapics) [#33918](https://github.com/nodejs/node/pull/33918)
* \[[`13b693fd54`](https://github.com/nodejs/node/commit/13b693fd54)] - **querystring**: improve stringify() performance (Brian White) [#33669](https://github.com/nodejs/node/pull/33669)
* \[[`d3737a1c32`](https://github.com/nodejs/node/commit/d3737a1c32)] - **src**: add errorProperties on process.report (himself65) [#28426](https://github.com/nodejs/node/pull/28426)
* \[[`b57778ff26`](https://github.com/nodejs/node/commit/b57778ff26)] - **src**: tolerate EPERM returned from tcsetattr (patr0nus) [#33944](https://github.com/nodejs/node/pull/33944)
* \[[`9e1185afee`](https://github.com/nodejs/node/commit/9e1185afee)] - **src**: clang\_format base\_object (Yash Ladha) [#33680](https://github.com/nodejs/node/pull/33680)
* \[[`69f962953c`](https://github.com/nodejs/node/commit/69f962953c)] - **src**: remove unnecessary calculation in base64.h (sapics) [#33839](https://github.com/nodejs/node/pull/33839)
* \[[`b1c9f75a20`](https://github.com/nodejs/node/commit/b1c9f75a20)] - **src**: use ToLocal in node\_os.cc (wenningplus) [#33939](https://github.com/nodejs/node/pull/33939)
* \[[`153f292a97`](https://github.com/nodejs/node/commit/153f292a97)] - **src**: handle empty Maybe(Local) in node\_util.cc (Anna Henningsen) [#33867](https://github.com/nodejs/node/pull/33867)
* \[[`6d5383de35`](https://github.com/nodejs/node/commit/6d5383de35)] - **src**: improve indention for upd\_wrap.cc (gengjiawen) [#33976](https://github.com/nodejs/node/pull/33976)
* \[[`437f387de9`](https://github.com/nodejs/node/commit/437f387de9)] - **src**: reduce scope of code cache mutex (Anna Henningsen) [#33980](https://github.com/nodejs/node/pull/33980)
* \[[`9199808355`](https://github.com/nodejs/node/commit/9199808355)] - **src**: do not track BaseObjects via cleanup hooks (Anna Henningsen) [#33809](https://github.com/nodejs/node/pull/33809)
* \[[`5b987c46b7`](https://github.com/nodejs/node/commit/5b987c46b7)] - **src**: remove ref to tools/generate\_code\_cache.js (Daniel Bevenius) [#33825](https://github.com/nodejs/node/pull/33825)
* \[[`185657dfd7`](https://github.com/nodejs/node/commit/185657dfd7)] - **src**: remove unused vector include in string\_bytes (Daniel Bevenius) [#33824](https://github.com/nodejs/node/pull/33824)
* \[[`ec2452c4af`](https://github.com/nodejs/node/commit/ec2452c4af)] - **src**: avoid unnecessary ToLocalChecked calls (Daniel Bevenius) [#33824](https://github.com/nodejs/node/pull/33824)
* \[[`74843db28c`](https://github.com/nodejs/node/commit/74843db28c)] - **src**: simplify format in node\_file.cc (himself65) [#33660](https://github.com/nodejs/node/pull/33660)
* \[[`86283aaa6a`](https://github.com/nodejs/node/commit/86283aaa6a)] - **src**: handle missing TracingController everywhere (Anna Henningsen) [#33815](https://github.com/nodejs/node/pull/33815)
* \[[`e07c1c2508`](https://github.com/nodejs/node/commit/e07c1c2508)] - **src**: simplify Reindent function in json\_utils.cc (sapics) [#33722](https://github.com/nodejs/node/pull/33722)
* \[[`449d9ec1c5`](https://github.com/nodejs/node/commit/449d9ec1c5)] - **src**: add "missing" bash completion options (Daniel Bevenius) [#33744](https://github.com/nodejs/node/pull/33744)
* \[[`4b4fb1381b`](https://github.com/nodejs/node/commit/4b4fb1381b)] - **src**: use Check() instead of FromJust in environment (Daniel Bevenius) [#33706](https://github.com/nodejs/node/pull/33706)
* \[[`6f1d38cd8f`](https://github.com/nodejs/node/commit/6f1d38cd8f)] - **src**: use ToLocal in SafeGetenv (Daniel Bevenius) [#33695](https://github.com/nodejs/node/pull/33695)
* \[[`5b8cac8cf5`](https://github.com/nodejs/node/commit/5b8cac8cf5)] - **src**: remove unnecessary ToLocalChecked call (Daniel Bevenius) [#33683](https://github.com/nodejs/node/pull/33683)
* \[[`eb8d6f5fd8`](https://github.com/nodejs/node/commit/eb8d6f5fd8)] - **src**: simplify MaybeStackBuffer::capacity() (Ben Noordhuis) [#33602](https://github.com/nodejs/node/pull/33602)
* \[[`e3beb781e0`](https://github.com/nodejs/node/commit/e3beb781e0)] - **src**: avoid OOB read in URL parser (Anna Henningsen) [#33640](https://github.com/nodejs/node/pull/33640)
* \[[`99371ade2a`](https://github.com/nodejs/node/commit/99371ade2a)] - **src**: use MaybeLocal.ToLocal instead of IsEmpty worker (Daniel Bevenius) [#33599](https://github.com/nodejs/node/pull/33599)
* \[[`9c69296990`](https://github.com/nodejs/node/commit/9c69296990)] - **src**: don't use semicolon outside function (Shelley Vohr) [#33592](https://github.com/nodejs/node/pull/33592)
* \[[`41d879616f`](https://github.com/nodejs/node/commit/41d879616f)] - **src**: remove unused using declarations (Daniel Bevenius) [#33268](https://github.com/nodejs/node/pull/33268)
* \[[`103479a0c5`](https://github.com/nodejs/node/commit/103479a0c5)] - **src**: use MaybeLocal.ToLocal instead of IsEmpty (Daniel Bevenius) [#33554](https://github.com/nodejs/node/pull/33554)
* \[[`05cbd8f6f2`](https://github.com/nodejs/node/commit/05cbd8f6f2)] - **src**: use const in constant args.Length() (himself65) [#33555](https://github.com/nodejs/node/pull/33555)
* \[[`48035a2a35`](https://github.com/nodejs/node/commit/48035a2a35)] - **src**: use MaybeLocal::FromMaybe to return exception (Daniel Bevenius) [#33514](https://github.com/nodejs/node/pull/33514)
* \[[`e1050344f8`](https://github.com/nodejs/node/commit/e1050344f8)] - _**Revert**_ "**src**: fix missing extra ca in tls.rootCertificates" (Eric Bickle) [#33313](https://github.com/nodejs/node/pull/33313)
* \[[`77b6298b67`](https://github.com/nodejs/node/commit/77b6298b67)] - **src**: remove BeforeExit callback list (Ben Noordhuis) [#33386](https://github.com/nodejs/node/pull/33386)
* \[[`a522c0e2c7`](https://github.com/nodejs/node/commit/a522c0e2c7)] - **src**: use MaybeLocal.ToLocal instead of IsEmpty (Daniel Bevenius) [#33457](https://github.com/nodejs/node/pull/33457)
* \[[`0837c2cc99`](https://github.com/nodejs/node/commit/0837c2cc99)] - **src**: remove unused headers in src/util.h (Juan José Arboleda) [#33070](https://github.com/nodejs/node/pull/33070)
* \[[`6f6fb1fcf5`](https://github.com/nodejs/node/commit/6f6fb1fcf5)] - **src**: prefer make\_unique (Michael Dawson) [#33378](https://github.com/nodejs/node/pull/33378)
* \[[`c697b96dea`](https://github.com/nodejs/node/commit/c697b96dea)] - **src**: remove unnecessary else in base\_object-inl.h (Daniel Bevenius) [#33413](https://github.com/nodejs/node/pull/33413)
* \[[`abf04b245a`](https://github.com/nodejs/node/commit/abf04b245a)] - **src,build**: add --openssl-default-cipher-list (Daniel Bevenius) [#33708](https://github.com/nodejs/node/pull/33708)
* \[[`62edaaefc2`](https://github.com/nodejs/node/commit/62edaaefc2)] - **stream**: fix the spellings (antsmartian) [#33635](https://github.com/nodejs/node/pull/33635)
* \[[`998b22cbbc`](https://github.com/nodejs/node/commit/998b22cbbc)] - **test**: add test for Http2ServerResponse#\[writableCorked,cork,uncork] (Pranshu Srivastava) [#33956](https://github.com/nodejs/node/pull/33956)
* \[[`9b8695fb35`](https://github.com/nodejs/node/commit/9b8695fb35)] - **test**: account for non-node basename (Shelley Vohr) [#33952](https://github.com/nodejs/node/pull/33952)
* \[[`b9f8034f95`](https://github.com/nodejs/node/commit/b9f8034f95)] - **test**: fix typo in common/index.js (gengjiawen) [#33976](https://github.com/nodejs/node/pull/33976)
* \[[`7744f66e0d`](https://github.com/nodejs/node/commit/7744f66e0d)] - **test**: print arguments passed to mustNotCall function (Denys Otrishko) [#33951](https://github.com/nodejs/node/pull/33951)
* \[[`b5113d0b53`](https://github.com/nodejs/node/commit/b5113d0b53)] - **test**: temporarily exclude test on arm (Michael Dawson) [#33814](https://github.com/nodejs/node/pull/33814)
* \[[`c50bd2f954`](https://github.com/nodejs/node/commit/c50bd2f954)] - **test**: fix invalid regular expressions in case test-trace-exit (legendecas) [#33769](https://github.com/nodejs/node/pull/33769)
* \[[`d374e76428`](https://github.com/nodejs/node/commit/d374e76428)] - **test**: changed function to arrow function (Sagar Jadhav) [#33711](https://github.com/nodejs/node/pull/33711)
* \[[`0982bf4234`](https://github.com/nodejs/node/commit/0982bf4234)] - **test**: uv\_tty\_init now returns EINVAL on IBM i (Xu Meng) [#33629](https://github.com/nodejs/node/pull/33629)
* \[[`3032f0f38d`](https://github.com/nodejs/node/commit/3032f0f38d)] - **test**: make flaky test stricter (Robert Nagy) [#33539](https://github.com/nodejs/node/pull/33539)
* \[[`ef27e6ce57`](https://github.com/nodejs/node/commit/ef27e6ce57)] - **test**: mark test-dgram-multicast-ssmv6-multi-process flaky (AshCripps) [#33498](https://github.com/nodejs/node/pull/33498)
* \[[`a131c72586`](https://github.com/nodejs/node/commit/a131c72586)] - **tools**: enable no-else-return lint rule (Luigi Pinca) [#32667](https://github.com/nodejs/node/pull/32667)
* \[[`6651bde34e`](https://github.com/nodejs/node/commit/6651bde34e)] - **tools**: update remark-preset-lint-node\@1.15.1 to 1.16.0 (Rich Trott) [#33852](https://github.com/nodejs/node/pull/33852)
* \[[`2e38f0dafd`](https://github.com/nodejs/node/commit/2e38f0dafd)] - **tools**: remove superfluous regex in tools/doc/json.js (Rich Trott) [#33998](https://github.com/nodejs/node/pull/33998)
* \[[`ba813dd0dd`](https://github.com/nodejs/node/commit/ba813dd0dd)] - **tools**: prevent js2c from running if nothing changed (Daniel Bevenius) [#33844](https://github.com/nodejs/node/pull/33844)
* \[[`fd5ab63d96`](https://github.com/nodejs/node/commit/fd5ab63d96)] - **tools**: remove unused vector include in mkdcodecache (Daniel Bevenius) [#33828](https://github.com/nodejs/node/pull/33828)
* \[[`54a4a816a4`](https://github.com/nodejs/node/commit/54a4a816a4)] - **tools**: update ESLint to 7.2.0 (Colin Ihrig) [#33776](https://github.com/nodejs/node/pull/33776)
* \[[`5328089c91`](https://github.com/nodejs/node/commit/5328089c91)] - **tools**: remove unused using declarations code\_cache (Daniel Bevenius) [#33697](https://github.com/nodejs/node/pull/33697)
* \[[`2f02fbac3a`](https://github.com/nodejs/node/commit/2f02fbac3a)] - **tools**: update remark-preset-lint-node from 1.15.0 to 1.15.1 (Rich Trott) [#33727](https://github.com/nodejs/node/pull/33727)
* \[[`3d05e3d861`](https://github.com/nodejs/node/commit/3d05e3d861)] - **tools**: fix check-imports.py to match on word boundaries (Richard Lau) [#33268](https://github.com/nodejs/node/pull/33268)
* \[[`ff4f9a9247`](https://github.com/nodejs/node/commit/ff4f9a9247)] - **tools**: update ESLint to 7.1.0 (Colin Ihrig) [#33526](https://github.com/nodejs/node/pull/33526)
* \[[`f495ab3dcb`](https://github.com/nodejs/node/commit/f495ab3dcb)] - **tools**: add docserve target (Antoine du HAMEL) [#33221](https://github.com/nodejs/node/pull/33221)
* \[[`a9dbb224af`](https://github.com/nodejs/node/commit/a9dbb224af)] - **util**: fix width detection for DEL without ICU (Ruben Bridgewater) [#33650](https://github.com/nodejs/node/pull/33650)
* \[[`02ae3f5625`](https://github.com/nodejs/node/commit/02ae3f5625)] - **util**: support Combining Diacritical Marks for Symbols (Ruben Bridgewater) [#33650](https://github.com/nodejs/node/pull/33650)
* \[[`524b230143`](https://github.com/nodejs/node/commit/524b230143)] - **util**: gracefully handle unknown colors (Ruben Bridgewater) [#33797](https://github.com/nodejs/node/pull/33797)
* \[[`e3533ab337`](https://github.com/nodejs/node/commit/e3533ab337)] - **util**: mark classes while inspecting them (Ruben Bridgewater) [#32332](https://github.com/nodejs/node/pull/32332)
* \[[`c4129f91e8`](https://github.com/nodejs/node/commit/c4129f91e8)] - **vm**: allow proxy callbacks to throw (Gus Caplan) [#33808](https://github.com/nodejs/node/pull/33808)
* \[[`8adfb542eb`](https://github.com/nodejs/node/commit/8adfb542eb)] - **wasi**: allow WASI stdio to be configured (Colin Ihrig) [#33544](https://github.com/nodejs/node/pull/33544)
* \[[`33984d6e4d`](https://github.com/nodejs/node/commit/33984d6e4d)] - **wasi**: simplify WASI memory management (Colin Ihrig) [#33525](https://github.com/nodejs/node/pull/33525)
* \[[`5e5be9929b`](https://github.com/nodejs/node/commit/5e5be9929b)] - **wasi**: refactor and enable poll\_oneoff() test (Colin Ihrig) [#33521](https://github.com/nodejs/node/pull/33521)
* \[[`383c5b3962`](https://github.com/nodejs/node/commit/383c5b3962)] - **wasi**: relax WebAssembly.Instance type check (Ben Noordhuis) [#33431](https://github.com/nodejs/node/pull/33431)
* \[[`7df79f498c`](https://github.com/nodejs/node/commit/7df79f498c)] - **wasi,worker**: handle termination exception (Ben Noordhuis) [#33386](https://github.com/nodejs/node/pull/33386)
* \[[`3b46e7f148`](https://github.com/nodejs/node/commit/3b46e7f148)] - **win,fs**: use namespaced path in absolute symlinks (Bartosz Sosnowski) [#33351](https://github.com/nodejs/node/pull/33351)
* \[[`4388dad537`](https://github.com/nodejs/node/commit/4388dad537)] - **win,msi**: add arm64 config for windows msi (Dennis Ameling) [#33689](https://github.com/nodejs/node/pull/33689)
* \[[`032c64f1e4`](https://github.com/nodejs/node/commit/032c64f1e4)] - **worker**: fix variable referencing in template string (Harshitha KP) [#33467](https://github.com/nodejs/node/pull/33467)
* \[[`1c64bc5e34`](https://github.com/nodejs/node/commit/1c64bc5e34)] - **worker**: perform initial port.unref() before preload modules (Anna Henningsen) [#33455](https://github.com/nodejs/node/pull/33455)
* \[[`c502384ab7`](https://github.com/nodejs/node/commit/c502384ab7)] - **worker**: use \_writev in internal communication (Anna Henningsen) [#33454](https://github.com/nodejs/node/pull/33454)

<a id="12.18.2"></a>

## 2020-06-30, Version 12.18.2 'Erbium' (LTS), @BethGriggs

### Notable changes

* **deps**: V8: backport fb26d0bb1835 (Matheus Marchini) [#33573](https://github.com/nodejs/node/pull/33573)
  * Fixes memory leak in `PrototypeUsers::Add`
* **src**: use symbol to store `AsyncWrap` resource (Anna Henningsen) [#31745](https://github.com/nodejs/node/pull/31745)
  * Fixes reported memory leak in [#33468](https://github.com/nodejs/node/issues/33468)

### Commits

* \[[`97a3f7b702`](https://github.com/nodejs/node/commit/97a3f7b702)] - **deps**: V8: backport fb26d0bb1835 (Matheus Marchini) [#33573](https://github.com/nodejs/node/pull/33573)
* \[[`30b0339061`](https://github.com/nodejs/node/commit/30b0339061)] - **src**: use symbol to store `AsyncWrap` resource (Anna Henningsen) [#31745](https://github.com/nodejs/node/pull/31745)

<a id="12.18.1"></a>

## 2020-06-17, Version 12.18.1 'Erbium' (LTS), @codebytere

### Notable Changes

* **deps**:
  * V8: cherry-pick 548f6c81d424 (Dominykas Blyžė) [#33484](https://github.com/nodejs/node/pull/33484)
  * update to uvwasi 0.0.9 (Colin Ihrig) [#33445](https://github.com/nodejs/node/pull/33445)
  * upgrade to libuv 1.38.0 (Colin Ihrig) [#33446](https://github.com/nodejs/node/pull/33446)
  * upgrade npm to 6.14.5 (Ruy Adorno) [#33239](https://github.com/nodejs/node/pull/33239)

### Commits

* \[[`ba93c8d87d`](https://github.com/nodejs/node/commit/ba93c8d87d)] - **async\_hooks**: clear async\_id\_stack for terminations in more places (Anna Henningsen) [#33347](https://github.com/nodejs/node/pull/33347)
* \[[`964adfafa5`](https://github.com/nodejs/node/commit/964adfafa5)] - **buffer**: improve copy() performance (Nikolai Vavilov) [#33214](https://github.com/nodejs/node/pull/33214)
* \[[`af95bd70bd`](https://github.com/nodejs/node/commit/af95bd70bd)] - **deps**: V8: cherry-pick 548f6c81d424 (Dominykas Blyžė) [#33484](https://github.com/nodejs/node/pull/33484)
* \[[`5c7176bf90`](https://github.com/nodejs/node/commit/5c7176bf90)] - **deps**: update to uvwasi 0.0.9 (Colin Ihrig) [#33445](https://github.com/nodejs/node/pull/33445)
* \[[`402aa1b840`](https://github.com/nodejs/node/commit/402aa1b840)] - **deps**: upgrade to libuv 1.38.0 (Colin Ihrig) [#33446](https://github.com/nodejs/node/pull/33446)
* \[[`4d6f56a76a`](https://github.com/nodejs/node/commit/4d6f56a76a)] - **deps**: upgrade npm to 6.14.5 (Ruy Adorno) [#33239](https://github.com/nodejs/node/pull/33239)
* \[[`98a7026311`](https://github.com/nodejs/node/commit/98a7026311)] - **doc**: document module.path (Antoine du Hamel) [#33323](https://github.com/nodejs/node/pull/33323)
* \[[`9572701705`](https://github.com/nodejs/node/commit/9572701705)] - **doc**: add fs.open() multiple constants example (Ethan Arrowood) [#33281](https://github.com/nodejs/node/pull/33281)
* \[[`7d8a226958`](https://github.com/nodejs/node/commit/7d8a226958)] - **doc**: fix typos in handle scope descriptions (Tobias Nießen) [#33267](https://github.com/nodejs/node/pull/33267)
* \[[`0c9b826ef8`](https://github.com/nodejs/node/commit/0c9b826ef8)] - **doc**: update function description for `decipher.setAAD` (Jonathan Buhacoff) [#33095](https://github.com/nodejs/node/pull/33095)
* \[[`4749156f4b`](https://github.com/nodejs/node/commit/4749156f4b)] - **doc**: add comment about highWaterMark limit (Benjamin Gruenbaum) [#33432](https://github.com/nodejs/node/pull/33432)
* \[[`a48aeb3f74`](https://github.com/nodejs/node/commit/a48aeb3f74)] - **doc**: clarify about the Node.js-only extensions in perf\_hooks (Joyee Cheung) [#33199](https://github.com/nodejs/node/pull/33199)
* \[[`a9ed287f00`](https://github.com/nodejs/node/commit/a9ed287f00)] - **doc**: fix extension in esm example (Gus Caplan) [#33408](https://github.com/nodejs/node/pull/33408)
* \[[`d2897a2836`](https://github.com/nodejs/node/commit/d2897a2836)] - **doc**: enhance guides by fixing and making grammar more consistent (Chris Holland) [#33152](https://github.com/nodejs/node/pull/33152)
* \[[`3d8ba292e2`](https://github.com/nodejs/node/commit/3d8ba292e2)] - **doc**: add examples for implementing ESM (unknown) [#33168](https://github.com/nodejs/node/pull/33168)
* \[[`318fcf8188`](https://github.com/nodejs/node/commit/318fcf8188)] - **doc**: add note about clientError writable handling (Paolo Insogna) [#33308](https://github.com/nodejs/node/pull/33308)
* \[[`30c9cb556f`](https://github.com/nodejs/node/commit/30c9cb556f)] - **doc**: fix typo in n-api.md (Daniel Bevenius) [#33319](https://github.com/nodejs/node/pull/33319)
* \[[`9dde1db332`](https://github.com/nodejs/node/commit/9dde1db332)] - **doc**: add warning for socket.connect reuse (Robert Nagy) [#33204](https://github.com/nodejs/node/pull/33204)
* \[[`0c7cf24431`](https://github.com/nodejs/node/commit/0c7cf24431)] - **doc**: correct description of `decipher.setAuthTag` in crypto.md (Jonathan Buhacoff)
* \[[`59619b0c9a`](https://github.com/nodejs/node/commit/59619b0c9a)] - **doc**: mention python3-distutils dependency in BUILDING.md (osher) [#33174](https://github.com/nodejs/node/pull/33174)
* \[[`0cee4c3eae`](https://github.com/nodejs/node/commit/0cee4c3eae)] - **doc**: removed unnecessary util imports from vm examples (Karol Walasek) [#33179](https://github.com/nodejs/node/pull/33179)
* \[[`903862089b`](https://github.com/nodejs/node/commit/903862089b)] - **doc**: update Buffer(size) documentation (Nikolai Vavilov) [#33198](https://github.com/nodejs/node/pull/33198)
* \[[`8b44be9b26`](https://github.com/nodejs/node/commit/8b44be9b26)] - **doc**: add Uint8Array to `end` and `write` (Pranshu Srivastava) [#33217](https://github.com/nodejs/node/pull/33217)
* \[[`4a584200f8`](https://github.com/nodejs/node/commit/4a584200f8)] - **doc**: specify unit of time passed to `fs.utimes` (Simen Bekkhus) [#33230](https://github.com/nodejs/node/pull/33230)
* \[[`ad7a890597`](https://github.com/nodejs/node/commit/ad7a890597)] - **doc**: add troubleshooting guide for AsyncLocalStorage (Andrey Pechkurov) [#33248](https://github.com/nodejs/node/pull/33248)
* \[[`2262962ab7`](https://github.com/nodejs/node/commit/2262962ab7)] - **doc**: remove AsyncWrap mentions from async\_hooks.md (Andrey Pechkurov) [#33249](https://github.com/nodejs/node/pull/33249)
* \[[`ac5cdd682a`](https://github.com/nodejs/node/commit/ac5cdd682a)] - **doc**: add warnings about transferring Buffers and ArrayBuffer (James M Snell) [#33252](https://github.com/nodejs/node/pull/33252)
* \[[`033bc96ec1`](https://github.com/nodejs/node/commit/033bc96ec1)] - **doc**: update napi\_async\_init documentation (Michael Dawson) [#33181](https://github.com/nodejs/node/pull/33181)
* \[[`ea3a68f74f`](https://github.com/nodejs/node/commit/ea3a68f74f)] - **doc**: doc and test URLSearchParams discrepancy (James M Snell) [#33236](https://github.com/nodejs/node/pull/33236)
* \[[`c6cf0483f2`](https://github.com/nodejs/node/commit/c6cf0483f2)] - **doc**: explicitly doc package.exports is breaking (Myles Borins) [#33074](https://github.com/nodejs/node/pull/33074)
* \[[`e572cf93e5`](https://github.com/nodejs/node/commit/e572cf93e5)] - **doc**: fix style and grammer in buffer.md (Nikolai Vavilov) [#33194](https://github.com/nodejs/node/pull/33194)
* \[[`5d80576889`](https://github.com/nodejs/node/commit/5d80576889)] - **errors**: skip fatal error highlighting on windows (Thomas) [#33132](https://github.com/nodejs/node/pull/33132)
* \[[`a029dca90e`](https://github.com/nodejs/node/commit/a029dca90e)] - **esm**: improve commonjs hint on module not found (Antoine du Hamel) [#33220](https://github.com/nodejs/node/pull/33220)
* \[[`c129e8809e`](https://github.com/nodejs/node/commit/c129e8809e)] - **fs**: forbid concurrent operations on Dir handle (Anna Henningsen) [#33274](https://github.com/nodejs/node/pull/33274)
* \[[`aa4611cccb`](https://github.com/nodejs/node/commit/aa4611cccb)] - **fs**: clean up Dir.read() uv\_fs\_t data before calling into JS (Anna Henningsen) [#33274](https://github.com/nodejs/node/pull/33274)
* \[[`fa4a37c57b`](https://github.com/nodejs/node/commit/fa4a37c57b)] - **http2**: comment on usage of `Object.create(null)` (Pranshu Srivastava) [#33183](https://github.com/nodejs/node/pull/33183)
* \[[`66dbaff848`](https://github.com/nodejs/node/commit/66dbaff848)] - **http2**: add `bytesWritten` test for `Http2Stream` (Pranshu Srivastava) [#33162](https://github.com/nodejs/node/pull/33162)
* \[[`59769c4d14`](https://github.com/nodejs/node/commit/59769c4d14)] - **lib**: fix typo in timers insert function comment (Daniel Bevenius) [#33301](https://github.com/nodejs/node/pull/33301)
* \[[`6881410951`](https://github.com/nodejs/node/commit/6881410951)] - **lib**: refactored scheduling policy assignment (Yash Ladha) [#32663](https://github.com/nodejs/node/pull/32663)
* \[[`9017bce54b`](https://github.com/nodejs/node/commit/9017bce54b)] - **lib**: fix grammar in internal/bootstrap/loaders.js (szTheory) [#33211](https://github.com/nodejs/node/pull/33211)
* \[[`d64dbfa1e7`](https://github.com/nodejs/node/commit/d64dbfa1e7)] - **meta**: add issue template for API reference docs (Derek Lewis) [#32944](https://github.com/nodejs/node/pull/32944)
* \[[`4f6e4ae49d`](https://github.com/nodejs/node/commit/4f6e4ae49d)] - **module**: add specific error for dir import (Antoine du HAMEL) [#33220](https://github.com/nodejs/node/pull/33220)
* \[[`77caf92314`](https://github.com/nodejs/node/commit/77caf92314)] - **module**: better error for named exports from cjs (Myles Borins) [#33256](https://github.com/nodejs/node/pull/33256)
* \[[`82da74b1cd`](https://github.com/nodejs/node/commit/82da74b1cd)] - **n-api**: add uint32 test for -1 (Gabriel Schulhof)
* \[[`68551d22d2`](https://github.com/nodejs/node/commit/68551d22d2)] - **perf\_hooks**: fix error message for invalid entryTypes (Michaël Zasso) [#33285](https://github.com/nodejs/node/pull/33285)
* \[[`e67df04df2`](https://github.com/nodejs/node/commit/e67df04df2)] - **src**: use BaseObjectPtr in StreamReq::Dispose (James M Snell) [#33102](https://github.com/nodejs/node/pull/33102)
* \[[`c797c7c7ab`](https://github.com/nodejs/node/commit/c797c7c7ab)] - **src**: reduce duplication in RegisterHandleCleanups (Daniel Bevenius) [#33421](https://github.com/nodejs/node/pull/33421)
* \[[`548db2e5b9`](https://github.com/nodejs/node/commit/548db2e5b9)] - **src**: remove unused IsolateSettings variable (Daniel Bevenius) [#33417](https://github.com/nodejs/node/pull/33417)
* \[[`e668376b5b`](https://github.com/nodejs/node/commit/e668376b5b)] - **src**: remove unused misc variable (Daniel Bevenius) [#33417](https://github.com/nodejs/node/pull/33417)
* \[[`9883ba6ddd`](https://github.com/nodejs/node/commit/9883ba6ddd)] - **src**: add promise\_resolve to SetupHooks comment (Daniel Bevenius) [#33365](https://github.com/nodejs/node/pull/33365)
* \[[`b924910fe7`](https://github.com/nodejs/node/commit/b924910fe7)] - **src**: distinguish refed/unrefed threadsafe Immediates (Anna Henningsen) [#33320](https://github.com/nodejs/node/pull/33320)
* \[[`29d24db914`](https://github.com/nodejs/node/commit/29d24db914)] - **src**: add #include \<string> in json\_utils.h (Cheng Zhao) [#33332](https://github.com/nodejs/node/pull/33332)
* \[[`a0bc2e3b64`](https://github.com/nodejs/node/commit/a0bc2e3b64)] - **src**: replace to CHECK\_NOT\_NULL in node\_crypto (himself65) [#33383](https://github.com/nodejs/node/pull/33383)
* \[[`1f159e45f2`](https://github.com/nodejs/node/commit/1f159e45f2)] - **src**: add primordials to arguments comment (Daniel Bevenius) [#33318](https://github.com/nodejs/node/pull/33318)
* \[[`fe780a5fe0`](https://github.com/nodejs/node/commit/fe780a5fe0)] - **src**: remove unused using declarations in node.cc (Daniel Bevenius) [#33261](https://github.com/nodejs/node/pull/33261)
* \[[`82c43d1594`](https://github.com/nodejs/node/commit/82c43d1594)] - **src**: delete unused variables to resolve compile time print warning (rickyes) [#33358](https://github.com/nodejs/node/pull/33358)
* \[[`548672d39c`](https://github.com/nodejs/node/commit/548672d39c)] - **src**: use MaybeLocal.ToLocal instead of IsEmpty (Daniel Bevenius) [#33312](https://github.com/nodejs/node/pull/33312)
* \[[`f27ae6ef46`](https://github.com/nodejs/node/commit/f27ae6ef46)] - **src**: fix typo in comment in async\_wrap.cc (Daniel Bevenius) [#33350](https://github.com/nodejs/node/pull/33350)
* \[[`b6300793fb`](https://github.com/nodejs/node/commit/b6300793fb)] - **src**: remove unnecessary Isolate::GetCurrent() calls (Anna Henningsen) [#33298](https://github.com/nodejs/node/pull/33298)
* \[[`642f81317e`](https://github.com/nodejs/node/commit/642f81317e)] - **src**: fix invalid windowBits=8 gzip segfault (Ben Noordhuis) [#33045](https://github.com/nodejs/node/pull/33045)
* \[[`a5e8c5ce0d`](https://github.com/nodejs/node/commit/a5e8c5ce0d)] - **src**: split out callback queue implementation from Environment (Anna Henningsen) [#33272](https://github.com/nodejs/node/pull/33272)
* \[[`ed62d43e79`](https://github.com/nodejs/node/commit/ed62d43e79)] - **src**: clean up large pages code (Gabriel Schulhof) [#33255](https://github.com/nodejs/node/pull/33255)
* \[[`c05483483f`](https://github.com/nodejs/node/commit/c05483483f)] - _**Revert**_ "**src**: add test/abort build tasks" (Richard Lau) [#33196](https://github.com/nodejs/node/pull/33196)
* \[[`b43fc64aa7`](https://github.com/nodejs/node/commit/b43fc64aa7)] - _**Revert**_ "**src**: add aliased-buffer-overflow abort test" (Richard Lau) [#33196](https://github.com/nodejs/node/pull/33196)
* \[[`edf75e4299`](https://github.com/nodejs/node/commit/edf75e4299)] - **src**: use basename(argv0) for --trace-uncaught suggestion (Anna Henningsen) [#32798](https://github.com/nodejs/node/pull/32798)
* \[[`4294d92b26`](https://github.com/nodejs/node/commit/4294d92b26)] - **stream**: make from read one at a time (Robert Nagy) [#33201](https://github.com/nodejs/node/pull/33201)
* \[[`194789f25b`](https://github.com/nodejs/node/commit/194789f25b)] - **stream**: make all streams error in a pipeline (Matteo Collina) [#30869](https://github.com/nodejs/node/pull/30869)
* \[[`5da7d52a9f`](https://github.com/nodejs/node/commit/5da7d52a9f)] - **test**: regression tests for async\_hooks + Promise + Worker interaction (Anna Henningsen) [#33347](https://github.com/nodejs/node/pull/33347)
* \[[`9f594be75a`](https://github.com/nodejs/node/commit/9f594be75a)] - **test**: fix test-dns-idna2008 (Rich Trott) [#33367](https://github.com/nodejs/node/pull/33367)
* \[[`33a787873f`](https://github.com/nodejs/node/commit/33a787873f)] - **test**: refactor WPTRunner (Joyee Cheung) [#33297](https://github.com/nodejs/node/pull/33297)
* \[[`fa1631355f`](https://github.com/nodejs/node/commit/fa1631355f)] - **test**: update WPT interfaces and hr-time (Joyee Cheung) [#33297](https://github.com/nodejs/node/pull/33297)
* \[[`c459832e4b`](https://github.com/nodejs/node/commit/c459832e4b)] - **test**: fix test-net-throttle (Rich Trott) [#33329](https://github.com/nodejs/node/pull/33329)
* \[[`cd92052935`](https://github.com/nodejs/node/commit/cd92052935)] - **test**: add hr-time Web platform tests (Michaël Zasso) [#33287](https://github.com/nodejs/node/pull/33287)
* \[[`0177cbf9e0`](https://github.com/nodejs/node/commit/0177cbf9e0)] - **test**: rename test-lookupService-promises (rickyes) [#33100](https://github.com/nodejs/node/pull/33100)
* \[[`139eb6bd68`](https://github.com/nodejs/node/commit/139eb6bd68)] - **test**: skip some console tests on dumb terminal (Adam Majer) [#33165](https://github.com/nodejs/node/pull/33165)
* \[[`1766514c5b`](https://github.com/nodejs/node/commit/1766514c5b)] - **test**: add tests for options.fs in fs streams (Julian Duque) [#33185](https://github.com/nodejs/node/pull/33185)
* \[[`7315c2288a`](https://github.com/nodejs/node/commit/7315c2288a)] - **tls**: fix --tls-keylog option (Alba Mendez) [#33366](https://github.com/nodejs/node/pull/33366)
* \[[`e240d56983`](https://github.com/nodejs/node/commit/e240d56983)] - **tools**: update dependencies for markdown linting (Rich Trott) [#33412](https://github.com/nodejs/node/pull/33412)
* \[[`2645b1c85b`](https://github.com/nodejs/node/commit/2645b1c85b)] - **tools**: update ESLint to 7.0.0 (Colin Ihrig) [#33316](https://github.com/nodejs/node/pull/33316)
* \[[`cdd7d3a66d`](https://github.com/nodejs/node/commit/cdd7d3a66d)] - **tools**: remove obsolete no-restricted-syntax eslint rules (Ruben Bridgewater) [#32161](https://github.com/nodejs/node/pull/32161)
* \[[`5d5e66c10c`](https://github.com/nodejs/node/commit/5d5e66c10c)] - **tools**: add eslint rule to only pass through 'test' to debuglog (Ruben Bridgewater) [#32161](https://github.com/nodejs/node/pull/32161)
* \[[`22f2c2c871`](https://github.com/nodejs/node/commit/22f2c2c871)] - **wasi**: fix poll\_oneoff memory interface (Colin Ihrig) [#33250](https://github.com/nodejs/node/pull/33250)
* \[[`33aacbefb1`](https://github.com/nodejs/node/commit/33aacbefb1)] - **wasi**: prevent syscalls before start (Tobias Nießen) [#33235](https://github.com/nodejs/node/pull/33235)
* \[[`5eed20b3b7`](https://github.com/nodejs/node/commit/5eed20b3b7)] - **worker**: fix race condition in node\_messaging.cc (Anna Henningsen) [#33429](https://github.com/nodejs/node/pull/33429)
* \[[`b4d903402b`](https://github.com/nodejs/node/commit/b4d903402b)] - **worker**: fix crash when .unref() is called during exit (Anna Henningsen) [#33394](https://github.com/nodejs/node/pull/33394)
* \[[`8a926982e5`](https://github.com/nodejs/node/commit/8a926982e5)] - **worker**: call CancelTerminateExecution() before exiting Locker (Anna Henningsen) [#33347](https://github.com/nodejs/node/pull/33347)
* \[[`631e433cf5`](https://github.com/nodejs/node/commit/631e433cf5)] - **zlib**: reject windowBits=8 when mode=GZIP (Ben Noordhuis) [#33045](https://github.com/nodejs/node/pull/33045)

<a id="12.18.0"></a>

## 2020-06-02, Version 12.18.0 'Erbium' (LTS), @targos

### Notable changes

This is a security release.

Vulnerabilities fixed:

* **CVE-2020-8172**: TLS session reuse can lead to host certificate verification bypass (High).
* **CVE-2020-11080**: HTTP/2 Large Settings Frame DoS (Low).
* **CVE-2020-8174**: `napi_get_value_string_*()` allows various kinds of memory corruption (High).

### Commits

* \[[`c6d0bdacc4`](https://github.com/nodejs/node/commit/c6d0bdacc4)] - **crypto**: update root certificates (AshCripps) [#33682](https://github.com/nodejs/node/pull/33682)
* \[[`916b2824d1`](https://github.com/nodejs/node/commit/916b2824d1)] - **(SEMVER-MINOR)** **deps**: update nghttp2 to 1.41.0 (James M Snell) [nodejs-private/node-private#206](https://github.com/nodejs-private/node-private/pull/206)
* \[[`d381426377`](https://github.com/nodejs/node/commit/d381426377)] - **(SEMVER-MINOR)** **http2**: implement support for max settings entries (James M Snell) [nodejs-private/node-private#206](https://github.com/nodejs-private/node-private/pull/206)
* \[[`7dd8982570`](https://github.com/nodejs/node/commit/7dd8982570)] - **napi**: fix memory corruption vulnerability (Tobias Nießen) [nodejs-private/node-private#195](https://github.com/nodejs-private/node-private/pull/195)
* \[[`0932309af2`](https://github.com/nodejs/node/commit/0932309af2)] - **tls**: emit `session` after verifying certificate (Fedor Indutny) [nodejs-private/node-private#200](https://github.com/nodejs-private/node-private/pull/200)
* \[[`c392d3923f`](https://github.com/nodejs/node/commit/c392d3923f)] - **tools**: update certdata.txt (AshCripps) [#33682](https://github.com/nodejs/node/pull/33682)

<a id="12.17.0"></a>

## 2020-05-26, Version 12.17.0 'Erbium' (LTS), @targos

### Notable Changes

#### ECMAScript Modules - `--experimental-modules` flag removal

As of Node.js 12.17.0, the `--experimental-modules` flag is no longer necessary
to use ECMAScript modules (ESM). However, the ESM implementation in Node.js
remains experimental. As per our stability index: “The feature is not subject
to Semantic Versioning rules. Non-backward compatible changes or removal may
occur in any future release.” Users should be cautious when using the feature
in production environments.

Unlike Node.js 14, using ESM will still emit a runtime experimental warning,
either when a module is used at the application's entrypoint or the first time
dynamic `import()` is called.

Please keep in mind that the implementation of ESM in Node.js differs from the
developer experience you might be familiar with. Most transpilation workflows
support features such as named exports from CommonJS module imports, optional
file extensions or JSON modules that the Node.js ESM implementation does not
support. It is highly likely that modules from transpiled environments will
require a certain degree of refactoring to work in Node.js. It is worth
mentioning that many of our design decisions were made with two primary goals.
Spec compliance and Web Compatibility. It is our belief that the current
implementation offers a future proof model to authoring ESM modules that paves
the path to Universal JavaScript. Please read more in our documentation.

The ESM implementation in Node.js is still experimental but we do believe that
we are getting very close to being able to call ESM in Node.js “stable”.
Removing the flag is a huge step in that direction.

We expect to remove the warning Node.js 12 later this year, possibly in late
October, when Node.js 14 will become LTS.

#### AsyncLocalStorage API (experimental)

The `AsyncLocalStorage` class has been introduced in the Async Hooks module.

This API allows keeping a context across asynchronous operations. For instance,
if a sequence id is stored within an instance of `AsyncLocalStorage` for each
HTTP request entering in a server, it will be possible to retrieve this id
without having access the current HTTP request:

```js
const http = require('http');
const { AsyncLocalStorage } = require('async_hooks');

const asyncLocalStorage = new AsyncLocalStorage();

function logWithId(msg) {
  const id = asyncLocalStorage.getStore();
  console.log(`${id !== undefined ? id : '-'}: `, msg);
}

let idSeq = 0;
http.createServer((req, res) => {
  asyncLocalStorage.run(idSeq++, () => {
    logWithId('start');
    // Imagine any chain of async operations here.
    setImmediate(() => {
      logWithId('finish');
      res.end();
    });
  });
}).listen(8080);
```

In this example, the `logWithId` function will always know what the current
request id is, even when there are multiple requests in parallel.

##### What can this API be used for

Use cases of this API include:

* Logging
* User identification
* Performance tracking
* Error tracking and handling
* Much more!

_Note: This API is still experimental and some methods might change in future releases of Node.js_

Contributed by Vladimir de Turckheim - [#26540](https://github.com/nodejs/node/pull/26540).

#### REPL previews

If further input is predicable, a suggestion is inserted as preview.

The REPL now supports previews similar to the Chrome DevTools console. An input
suggestion is inserted as preview in case further input is predicable. The
suggestion may be accepted by either pressing `<TAB>` or `<RIGHT>` at the end of
the input.
On top of that, output is previewed when entering variable names or function
calls that have no side effect.

![image](https://user-images.githubusercontent.com/8822573/80928108-afb03300-8da2-11ea-8898-499d8c2dbc7a.png)
![image](https://user-images.githubusercontent.com/8822573/80928118-c191d600-8da2-11ea-9739-32e8becc68fe.png)

[Check the preview in action](https://asciinema.org/a/ePQx0GfCYQGdnQTzwlnSIyxbN)
and try it out on your own. Just access the REPL on your terminal by starting
the Node.js executable without any further command.

Contributed by Ruben Bridgewater - [#30907](https://github.com/nodejs/node/pull/30907), [#30811](https://github.com/nodejs/node/pull/30811).

#### REPL reverse-i-search

The REPL supports bi-directional reverse-i-search similar to
[ZSH](https://en.wikipedia.org/wiki/Z_shell). It is triggered with `<ctrl> + R`
to search backwards and `<ctrl> + S` to search forwards.

Entries are accepted as soon as any button is pressed that doesn't correspond
with the reverse search. Cancelling is possible by pressing `escape` or
`<ctrl> + C`.

Changing the direction immediately searches for the next entry in the expected
direction from the current position on.

![image](https://user-images.githubusercontent.com/8822573/80928291-f3f00300-8da3-11ea-97d8-12e85e2e3d2c.png)

[Reverse-i-search in action](https://asciinema.org/a/shV3YOFu74BcBakJcvY4USNqv).

Contributed by Ruben Bridgewater - [#31006](https://github.com/nodejs/node/pull/31006).

#### REPL substring-based search

It is now possible to access former history entries very fast by writing the
first characters of the formerly entered code you are looking for. Then push
`<UP>` or `<DOWN>` to go through the history entries that start with those
characters.

It works similar to the [Fish Shell](https://fishshell.com/) substring-based
history search.

Contributed by Ruben Bridgewater - [#31112](https://github.com/nodejs/node/pull/31112).

#### Error monitoring

##### Monitoring `error` events

It is now possible to monitor `'error'` events on an `EventEmitter` without
consuming the emitted error by installing a listener using the symbol
`EventEmitter.errorMonitor`:

```js
const myEmitter = new MyEmitter();

myEmitter.on(EventEmitter.errorMonitor, (err) => {
  MyMonitoringTool.log(err);
});

myEmitter.emit('error', new Error('whoops!'));
// Still throws and crashes Node.js
```

Contributed by Gerhard Stoebich - [#30932](https://github.com/nodejs/node/pull/30932).

#### Monitoring uncaught exceptions

It is now possible to monitor `'uncaughtException'` events without overriding
the default behavior that exits the process by installing an
`'uncaughtExceptionMonitor'` listener:

```js
process.on('uncaughtExceptionMonitor', (err, origin) => {
  MyMonitoringTool.logSync(err, origin);
});

// Intentionally cause an exception, but do not catch it.
nonexistentFunc();
// Still crashes Node.js
```

Contributed by Gerhard Stoebich - [#31257](https://github.com/nodejs/node/pull/31257).

#### File system APIs

##### New function: `fs.readv`

This new function (along with its sync and promisified versions) takes an array
of `ArrayBufferView` elements and will write the data it reads sequentially to
the buffers.

Contributed by Sk Sajidul Kadir - [#32356](https://github.com/nodejs/node/pull/32356).

##### Optional parameters in `fs.read`

A new overload is available for `fs.read` (along with its sync and promisified
versions), which allows to optionally pass any of the `offset`, `length` and
`position` parameters.

Contributed by Lucas Holmquist - [#31402](https://github.com/nodejs/node/pull/31402).

#### Console `groupIndentation` option

The Console constructor (`require('console').Console`) now supports different group indentations.

This is useful in case you want different grouping width than 2 spaces.

```js
const { Console } = require('console');
const customConsole = new Console({
  stdout: process.stdout,
  stderr: process.stderr,
  groupIndentation: 10,
});

customConsole.log('foo');
// 'foo'
customConsole.group();
customConsole.log('foo');
//           'foo'
```

Contributed by rickyes - [#32964](https://github.com/nodejs/node/pull/32964).

#### `maxStringLength` option for `util.inspect()`

It is now possible to limit the length of strings while inspecting objects.
This is possible by passing through the `maxStringLength` option similar to:

```js
const { inspect } = require('util');

const string = inspect(['a'.repeat(1e8)], { maxStringLength: 10 });

console.log(string);
// "[ 'aaaaaaaaaa'... 99999990 more characters ]"
```

Contributed by rosaxny - [#32392](https://github.com/nodejs/node/pull/32392).

#### Stable N-API release 6

The following N-API features are now stable as part of the N-API 6 release:

* [`napi_set_instance_data`](https://nodejs.org/dist/latest-v12.x/docs/api/n-api.html#n_api_napi_set_instance_data)
* [`napi_get_instance_data`](https://nodejs.org/dist/latest-v12.x/docs/api/n-api.html#n_api_napi_get_instance_data)
* [`napi_key_collection_mode`](https://nodejs.org/dist/latest-v12.x/docs/api/n-api.html#n_api_napi_key_collection_mode)
* [`napi_key_filter`](https://nodejs.org/dist/latest-v12.x/docs/api/n-api.html#n_api_napi_key_filter)
* [`napi_key_conversion`](https://nodejs.org/dist/latest-v12.x/docs/api/n-api.html#n_api_napi_key_conversion)
* [`napi_create_bigint_int64`](https://nodejs.org/dist/latest-v12.x/docs/api/n-api.html#n_api_napi_create_bigint_int64)
* [`napi_create_bigint_uint64`](https://nodejs.org/dist/latest-v12.x/docs/api/n-api.html#n_api_napi_create_bigint_uint64)
* [`napi_create_bigint_words`](https://nodejs.org/dist/latest-v12.x/docs/api/n-api.html#n_api_napi_create_bigint_words)
* [`napi_get_value_bigint_int64`](https://nodejs.org/dist/latest-v12.x/docs/api/n-api.html#n_api_napi_get_value_bigint_int64)
* [`napi_get_value_bigint_uint64`](https://nodejs.org/dist/latest-v12.x/docs/api/n-api.html#n_api_napi_get_value_bigint_uint64)
* [`napi_get_value_bigint_words`](https://nodejs.org/dist/latest-v12.x/docs/api/n-api.html#n_api_napi_get_value_bigint_words)
* [`napi_get_all_property_names`](https://nodejs.org/dist/latest-v12.x/docs/api/n-api.html#n_api_napi_get_all_property_names)

#### Stable diagnostic reports

The [Diagnostic Report](https://nodejs.org/dist/latest-v12.x/docs/api/report.html)
feature is now stable and supports a new `--report-compact` flag to write the
reports in a compact, single-line JSON format, more easily consumable by log
processing systems than the default multi-line format designed for human
consumption.

#### Increase of the default server headers timeout

The default value of `server.headersTimeout` for `http` and `https` servers was
increased from `40000` to `60000` (60 seconds). This to accomodate for systems
like AWS ELB that have a timeout of 60 seconds.

Contributed by Tim Costa - [#30071](https://github.com/nodejs/node/pull/30071).

#### Other changes

* **cli**:
  * Added a `--trace-sigint` CLI flag that will print the current execution
    stack on SIGINT (legendecas) [#29207](https://github.com/nodejs/node/pull/29207).
* **crypto**:
  * Various crypto APIs now support Diffie-Hellman secrets (Tobias Nießen) [#31178](https://github.com/nodejs/node/pull/31178).
* **dns**:
  * Added the `dns.ALL` flag, that can be passed to `dns.lookup()` with `dns.V4MAPPED`
    to return resolved IPv6 addresses as well as IPv4 mapped IPv6 addresses (murgatroid99) [#32183](https://github.com/nodejs/node/pull/32183).
* **module**
  * Added a new experimental API to interact with Source Map V3 data (Benjamin Coe) [#31132](https://github.com/nodejs/node/pull/31132).
* **worker**:
  * Added support for passing a `transferList` along with `workerData` to the
    `Worker` constructor (Juan José Arboleda) [#32278](https://github.com/nodejs/node/pull/32278).

### Commits

#### Semver-minor commits

* \[[`a35e88caf5`](https://github.com/nodejs/node/commit/a35e88caf5)] - **(SEMVER-MINOR)** **async\_hooks**: merge run and exit methods (Andrey Pechkurov) [#31950](https://github.com/nodejs/node/pull/31950)
* \[[`3eb34068a2`](https://github.com/nodejs/node/commit/3eb34068a2)] - **(SEMVER-MINOR)** **async\_hooks**: prevent sync methods of async storage exiting outer context (Stephen Belanger) [#31950](https://github.com/nodejs/node/pull/31950)
* \[[`22db34caa7`](https://github.com/nodejs/node/commit/22db34caa7)] - **(SEMVER-MINOR)** **async\_hooks**: add sync enterWith to ALS (Stephen Belanger) [#31945](https://github.com/nodejs/node/pull/31945)
* \[[`16e8b11708`](https://github.com/nodejs/node/commit/16e8b11708)] - **(SEMVER-MINOR)** **async\_hooks**: introduce async-context API (Vladimir de Turckheim) [#26540](https://github.com/nodejs/node/pull/26540)
* \[[`f7adfcc1df`](https://github.com/nodejs/node/commit/f7adfcc1df)] - **(SEMVER-MINOR)** **async\_hooks**: add executionAsyncResource (Matteo Collina) [#30959](https://github.com/nodejs/node/pull/30959)
* \[[`984ae304f2`](https://github.com/nodejs/node/commit/984ae304f2)] - **(SEMVER-MINOR)** **build**: make --without-report a no-op (Colin Ihrig) [#32242](https://github.com/nodejs/node/pull/32242)
* \[[`e67b97ee53`](https://github.com/nodejs/node/commit/e67b97ee53)] - **(SEMVER-MINOR)** **cli**: allow --huge-max-old-generation-size in NODE\_OPTIONS (Anna Henningsen) [#32251](https://github.com/nodejs/node/pull/32251)
* \[[`154b18ffca`](https://github.com/nodejs/node/commit/154b18ffca)] - **(SEMVER-MINOR)** **console**: support console constructor groupIndentation option (rickyes) [#32964](https://github.com/nodejs/node/pull/32964)
* \[[`40253cc1c8`](https://github.com/nodejs/node/commit/40253cc1c8)] - **(SEMVER-MINOR)** **crypto**: add crypto.diffieHellman (Tobias Nießen) [#31178](https://github.com/nodejs/node/pull/31178)
* \[[`1977136a19`](https://github.com/nodejs/node/commit/1977136a19)] - **(SEMVER-MINOR)** **crypto**: add DH support to generateKeyPair (Tobias Nießen) [#31178](https://github.com/nodejs/node/pull/31178)
* \[[`9f85585b13`](https://github.com/nodejs/node/commit/9f85585b13)] - **(SEMVER-MINOR)** **crypto**: add key type 'dh' (Tobias Nießen) [#31178](https://github.com/nodejs/node/pull/31178)
* \[[`6ffe4ed3b5`](https://github.com/nodejs/node/commit/6ffe4ed3b5)] - **(SEMVER-MINOR)** **deps**: upgrade to libuv 1.37.0 (Colin Ihrig) [#32866](https://github.com/nodejs/node/pull/32866)
* \[[`2d7a7592ec`](https://github.com/nodejs/node/commit/2d7a7592ec)] - **(SEMVER-MINOR)** **deps**: upgrade to libuv 1.36.0 (Colin Ihrig) [#32866](https://github.com/nodejs/node/pull/32866)
* \[[`ae83f0f993`](https://github.com/nodejs/node/commit/ae83f0f993)] - **(SEMVER-MINOR)** **deps**: upgrade to libuv 1.35.0 (Colin Ihrig) [#32204](https://github.com/nodejs/node/pull/32204)
* \[[`b7d264edaf`](https://github.com/nodejs/node/commit/b7d264edaf)] - **(SEMVER-MINOR)** **dns**: add dns.ALL hints flag constant (murgatroid99) [#32183](https://github.com/nodejs/node/pull/32183)
* \[[`fd2486ea44`](https://github.com/nodejs/node/commit/fd2486ea44)] - **(SEMVER-MINOR)** **doc**: update stability of report features (Colin Ihrig) [#32242](https://github.com/nodejs/node/pull/32242)
* \[[`90d35adccd`](https://github.com/nodejs/node/commit/90d35adccd)] - **(SEMVER-MINOR)** **doc,lib,src,test**: make --experimental-report a nop (Colin Ihrig) [#32242](https://github.com/nodejs/node/pull/32242)
* \[[`93226a5097`](https://github.com/nodejs/node/commit/93226a5097)] - **(SEMVER-MINOR)** **esm**: unflag --experimental-modules (Guy Bedford) [#29866](https://github.com/nodejs/node/pull/29866)
* \[[`8c497f8969`](https://github.com/nodejs/node/commit/8c497f8969)] - **(SEMVER-MINOR)** **events**: allow monitoring error events (Gerhard Stoebich) [#30932](https://github.com/nodejs/node/pull/30932)
* \[[`a100709fa8`](https://github.com/nodejs/node/commit/a100709fa8)] - **(SEMVER-MINOR)** **fs**: make parameters optional for readSync (Lucas Holmquist) [#32460](https://github.com/nodejs/node/pull/32460)
* \[[`6601fac06a`](https://github.com/nodejs/node/commit/6601fac06a)] - **(SEMVER-MINOR)** **fs**: add fs.readv() (Sk Sajidul Kadir) [#32356](https://github.com/nodejs/node/pull/32356)
* \[[`16a913f702`](https://github.com/nodejs/node/commit/16a913f702)] - **(SEMVER-MINOR)** **fs**: make fs.read params optional (Lucas Holmquist) [#31402](https://github.com/nodejs/node/pull/31402)
* \[[`7260ede9e6`](https://github.com/nodejs/node/commit/7260ede9e6)] - **(SEMVER-MINOR)** **fs**: return first folder made by mkdir recursive (Benjamin Coe) [#31530](https://github.com/nodejs/node/pull/31530)
* \[[`a15e712ef6`](https://github.com/nodejs/node/commit/a15e712ef6)] - **(SEMVER-MINOR)** **fs**: allow overriding fs for streams (Robert Nagy) [#29083](https://github.com/nodejs/node/pull/29083)
* \[[`b5983213c1`](https://github.com/nodejs/node/commit/b5983213c1)] - **(SEMVER-MINOR)** **lib**: add option to disable \_\_proto\_\_ (Gus Caplan) [#32279](https://github.com/nodejs/node/pull/32279)
* \[[`784fb8f08c`](https://github.com/nodejs/node/commit/784fb8f08c)] - **(SEMVER-MINOR)** **module**: add API for interacting with source maps (Benjamin Coe) [#31132](https://github.com/nodejs/node/pull/31132)
* \[[`e22d853c5d`](https://github.com/nodejs/node/commit/e22d853c5d)] - **(SEMVER-MINOR)** **n-api**: define release 6 (Gabriel Schulhof) [#32058](https://github.com/nodejs/node/pull/32058)
* \[[`f56c4dd933`](https://github.com/nodejs/node/commit/f56c4dd933)] - **(SEMVER-MINOR)** **n-api**: add napi\_get\_all\_property\_names (himself65) [#30006](https://github.com/nodejs/node/pull/30006)
* \[[`9eeee0d9f2`](https://github.com/nodejs/node/commit/9eeee0d9f2)] - **(SEMVER-MINOR)** **perf\_hooks**: add property flags to GCPerformanceEntry (Kirill Fomichev) [#29547](https://github.com/nodejs/node/pull/29547)
* \[[`5ec9295034`](https://github.com/nodejs/node/commit/5ec9295034)] - **(SEMVER-MINOR)** **process**: report ArrayBuffer memory in `memoryUsage()` (Anna Henningsen) [#31550](https://github.com/nodejs/node/pull/31550)
* \[[`de3603f0a6`](https://github.com/nodejs/node/commit/de3603f0a6)] - **(SEMVER-MINOR)** **process**: allow monitoring uncaughtException (Gerhard Stoebich) [#31257](https://github.com/nodejs/node/pull/31257)
* \[[`cf28afeeb6`](https://github.com/nodejs/node/commit/cf28afeeb6)] - **(SEMVER-MINOR)** **readline,repl**: improve history up/previous (Ruben Bridgewater) [#31112](https://github.com/nodejs/node/pull/31112)
* \[[`a0eb3e4ed2`](https://github.com/nodejs/node/commit/a0eb3e4ed2)] - **(SEMVER-MINOR)** **readline,repl**: skip history entries identical to the current line (Ruben Bridgewater) [#31112](https://github.com/nodejs/node/pull/31112)
* \[[`d7e153bddb`](https://github.com/nodejs/node/commit/d7e153bddb)] - **(SEMVER-MINOR)** **readline,repl**: add substring based history search (Ruben Bridgewater) [#31112](https://github.com/nodejs/node/pull/31112)
* \[[`936c85c309`](https://github.com/nodejs/node/commit/936c85c309)] - **(SEMVER-MINOR)** **repl**: implement reverse search (Ruben Bridgewater) [#31006](https://github.com/nodejs/node/pull/31006)
* \[[`bf9ff16412`](https://github.com/nodejs/node/commit/bf9ff16412)] - **(SEMVER-MINOR)** **repl**: add completion preview (Ruben Bridgewater) [#30907](https://github.com/nodejs/node/pull/30907)
* \[[`b14440fb5c`](https://github.com/nodejs/node/commit/b14440fb5c)] - **(SEMVER-MINOR)** **repl**: support previews by eager evaluating input (Ruben Bridgewater) [#30811](https://github.com/nodejs/node/pull/30811)
* \[[`0b310df532`](https://github.com/nodejs/node/commit/0b310df532)] - **(SEMVER-MINOR)** **src**: unconditionally include report feature (Colin Ihrig) [#32242](https://github.com/nodejs/node/pull/32242)
* \[[`394487e3e8`](https://github.com/nodejs/node/commit/394487e3e8)] - **(SEMVER-MINOR)** **src**: create a getter for kernel version (Juan José Arboleda) [#31732](https://github.com/nodejs/node/pull/31732)
* \[[`4ec25b4865`](https://github.com/nodejs/node/commit/4ec25b4865)] - **(SEMVER-MINOR)** **src,cli**: support compact (one-line) JSON reports (Sam Roberts) [#32254](https://github.com/nodejs/node/pull/32254)
* \[[`b038ad91f5`](https://github.com/nodejs/node/commit/b038ad91f5)] - **(SEMVER-MINOR)** **src,lib**: make ^C print a JS stack trace (legendecas) [#29207](https://github.com/nodejs/node/pull/29207)
* \[[`6348fae690`](https://github.com/nodejs/node/commit/6348fae690)] - **(SEMVER-MINOR)** **tls**: expose SSL\_export\_keying\_material (simon) [#31814](https://github.com/nodejs/node/pull/31814)
* \[[`6aa3869688`](https://github.com/nodejs/node/commit/6aa3869688)] - **(SEMVER-MINOR)** **util**: add `maxStrLength` option to `inspect` function (unknown) [#32392](https://github.com/nodejs/node/pull/32392)
* \[[`eda6665799`](https://github.com/nodejs/node/commit/eda6665799)] - **(SEMVER-MINOR)** **vm**: add code cache support for SourceTextModule (Gus Caplan) [#31278](https://github.com/nodejs/node/pull/31278)
* \[[`5c81b8d814`](https://github.com/nodejs/node/commit/5c81b8d814)] - **(SEMVER-MINOR)** **wasi**: add returnOnExit option (Colin Ihrig) [#32101](https://github.com/nodejs/node/pull/32101)
* \[[`ca4e65273f`](https://github.com/nodejs/node/commit/ca4e65273f)] - **(SEMVER-MINOR)** **worker**: support MessagePort to workers data (Juan José Arboleda) [#32278](https://github.com/nodejs/node/pull/32278)
* \[[`217e3dfea6`](https://github.com/nodejs/node/commit/217e3dfea6)] - **(SEMVER-MINOR)** **worker**: allow URL in Worker constructor (Antoine du HAMEL) [#31664](https://github.com/nodejs/node/pull/31664)
* \[[`ab8f38b551`](https://github.com/nodejs/node/commit/ab8f38b551)] - **(SEMVER-MINOR)** **worker**: add ability to take heap snapshot from parent thread (Anna Henningsen) [#31569](https://github.com/nodejs/node/pull/31569)

#### Semver-patch commits

* \[[`06d607d50f`](https://github.com/nodejs/node/commit/06d607d50f)] - **async\_hooks**: fix ctx loss after nested ALS calls (Andrey Pechkurov) [#32085](https://github.com/nodejs/node/pull/32085)
* \[[`96d1f14005`](https://github.com/nodejs/node/commit/96d1f14005)] - **async\_hooks**: add store arg in AsyncLocalStorage (Andrey Pechkurov) [#31930](https://github.com/nodejs/node/pull/31930)
* \[[`b4ca132254`](https://github.com/nodejs/node/commit/b4ca132254)] - **async\_hooks**: executionAsyncResource matches in hooks (Gerhard Stoebich) [#31821](https://github.com/nodejs/node/pull/31821)
* \[[`02f99d289d`](https://github.com/nodejs/node/commit/02f99d289d)] - **buffer**: add type check in bidirectionalIndexOf (Gerhard Stoebich) [#32770](https://github.com/nodejs/node/pull/32770)
* \[[`b53193a33b`](https://github.com/nodejs/node/commit/b53193a33b)] - **buffer**: mark pool ArrayBuffer as untransferable (Anna Henningsen) [#32759](https://github.com/nodejs/node/pull/32759)
* \[[`b555a772cc`](https://github.com/nodejs/node/commit/b555a772cc)] - **build**: fix vcbuild error for missing Visual Studio (Thomas) [#32658](https://github.com/nodejs/node/pull/32658)
* \[[`6f1931de25`](https://github.com/nodejs/node/commit/6f1931de25)] - **build**: remove .git folders when testing V8 (Richard Lau) [#32877](https://github.com/nodejs/node/pull/32877)
* \[[`c0805f0cab`](https://github.com/nodejs/node/commit/c0805f0cab)] - **build**: add configure flag to build V8 with DCHECKs (Anna Henningsen) [#32787](https://github.com/nodejs/node/pull/32787)
* \[[`60660c35ee`](https://github.com/nodejs/node/commit/60660c35ee)] - **build**: use same flags as V8 for ASAN (Matheus Marchini) [#32776](https://github.com/nodejs/node/pull/32776)
* \[[`26fee8b323`](https://github.com/nodejs/node/commit/26fee8b323)] - **build**: remove `.txt` files from .gitignore (Rich Trott) [#32710](https://github.com/nodejs/node/pull/32710)
* \[[`70eaba12a1`](https://github.com/nodejs/node/commit/70eaba12a1)] - **build**: remove node\_report option in node.gyp (Colin Ihrig) [#32242](https://github.com/nodejs/node/pull/32242)
* \[[`e765d597fd`](https://github.com/nodejs/node/commit/e765d597fd)] - **build**: add missing comma in node.gyp (Colin Ihrig) [#31959](https://github.com/nodejs/node/pull/31959)
* \[[`49ddd36f13`](https://github.com/nodejs/node/commit/49ddd36f13)] - **build**: fix building with ninja (Richard Lau) [#32071](https://github.com/nodejs/node/pull/32071)
* \[[`e097980cfe`](https://github.com/nodejs/node/commit/e097980cfe)] - **build**: warn upon --use-largepages config option (Gabriel Schulhof) [#31103](https://github.com/nodejs/node/pull/31103)
* \[[`c3efd2cb9a`](https://github.com/nodejs/node/commit/c3efd2cb9a)] - **build**: switch realpath to pwd (Benjamin Coe) [#31095](https://github.com/nodejs/node/pull/31095)
* \[[`0190a62f58`](https://github.com/nodejs/node/commit/0190a62f58)] - **build**: re-introduce --use-largepages as no-op (Gabriel Schulhof)
* \[[`e2a090b693`](https://github.com/nodejs/node/commit/e2a090b693)] - **build**: enable loading internal modules from disk (Gus Caplan) [#31321](https://github.com/nodejs/node/pull/31321)
* \[[`c4da682437`](https://github.com/nodejs/node/commit/c4da682437)] - **cli, report**: move --report-on-fatalerror to stable (Colin Ihrig) [#32496](https://github.com/nodejs/node/pull/32496)
* \[[`e05c29db3f`](https://github.com/nodejs/node/commit/e05c29db3f)] - **cluster**: fix error on worker disconnect/destroy (Santiago Gimeno) [#32793](https://github.com/nodejs/node/pull/32793)
* \[[`d217b792bc`](https://github.com/nodejs/node/commit/d217b792bc)] - **cluster**: removed unused addressType argument from constructor (Yash Ladha) [#32963](https://github.com/nodejs/node/pull/32963)
* \[[`71bccdde76`](https://github.com/nodejs/node/commit/71bccdde76)] - **crypto**: check DiffieHellman p and g params (Ben Noordhuis) [#32739](https://github.com/nodejs/node/pull/32739)
* \[[`c1b767471a`](https://github.com/nodejs/node/commit/c1b767471a)] - **crypto**: generator must be int32 in DiffieHellman() (Ben Noordhuis) [#32739](https://github.com/nodejs/node/pull/32739)
* \[[`4236175878`](https://github.com/nodejs/node/commit/4236175878)] - **crypto**: key size must be int32 in DiffieHellman() (Ben Noordhuis) [#32739](https://github.com/nodejs/node/pull/32739)
* \[[`0847bc3788`](https://github.com/nodejs/node/commit/0847bc3788)] - **crypto**: simplify exportKeyingMaterial (Tobias Nießen) [#31922](https://github.com/nodejs/node/pull/31922)
* \[[`907252d4cf`](https://github.com/nodejs/node/commit/907252d4cf)] - **crypto**: improve errors in DiffieHellmanGroup (Tobias Nießen) [#31445](https://github.com/nodejs/node/pull/31445)
* \[[`30633acf20`](https://github.com/nodejs/node/commit/30633acf20)] - **crypto**: assign and use ERR\_CRYPTO\_UNKNOWN\_CIPHER (Tobias Nießen) [#31437](https://github.com/nodejs/node/pull/31437)
* \[[`5dab489d50`](https://github.com/nodejs/node/commit/5dab489d50)] - **crypto**: simplify DH groups (Tobias Nießen) [#31178](https://github.com/nodejs/node/pull/31178)
* \[[`5c0232a632`](https://github.com/nodejs/node/commit/5c0232a632)] - **deps**: backport ICU-21081 for ICU 67.x (constexpr) (Steven R. Loomis) [#33337](https://github.com/nodejs/node/pull/33337)
* \[[`2d76ae7497`](https://github.com/nodejs/node/commit/2d76ae7497)] - **deps**: update to ICU 67.1 (Michaël Zasso) [#33337](https://github.com/nodejs/node/pull/33337)
* \[[`e073da095e`](https://github.com/nodejs/node/commit/e073da095e)] - **deps**: update to uvwasi 0.0.8 (Colin Ihrig) [#33078](https://github.com/nodejs/node/pull/33078)
* \[[`eb33d523da`](https://github.com/nodejs/node/commit/eb33d523da)] - **deps**: V8: backport 3f8dc4b2e5ba (Ujjwal Sharma) [#32993](https://github.com/nodejs/node/pull/32993)
* \[[`56313daff6`](https://github.com/nodejs/node/commit/56313daff6)] - **deps**: V8: cherry-pick e1eac1b16c96 (Milad Farazmand) [#32974](https://github.com/nodejs/node/pull/32974)
* \[[`65db9b210d`](https://github.com/nodejs/node/commit/65db9b210d)] - **deps**: fix zlib compilation for CPUs without SIMD features (Anna Henningsen) [#32627](https://github.com/nodejs/node/pull/32627)
* \[[`1b53e179b8`](https://github.com/nodejs/node/commit/1b53e179b8)] - **deps**: update zlib to upstream d7f3ca9 (Sam Roberts) [#31800](https://github.com/nodejs/node/pull/31800)
* \[[`9a89718410`](https://github.com/nodejs/node/commit/9a89718410)] - **deps**: move zlib maintenance info to guides (Sam Roberts) [#31800](https://github.com/nodejs/node/pull/31800)
* \[[`9e33f97c4e`](https://github.com/nodejs/node/commit/9e33f97c4e)] - **deps**: switch to chromium's zlib implementation (Brian White) [#31201](https://github.com/nodejs/node/pull/31201)
* \[[`322a9986fe`](https://github.com/nodejs/node/commit/322a9986fe)] - **dgram**: make UDPWrap more reusable (Anna Henningsen) [#31871](https://github.com/nodejs/node/pull/31871)
* \[[`ea4302bd46`](https://github.com/nodejs/node/commit/ea4302bd46)] - **errors**: drop pronouns from ERR\_WORKER\_PATH message (Colin Ihrig) [#32285](https://github.com/nodejs/node/pull/32285)
* \[[`daf1d842cc`](https://github.com/nodejs/node/commit/daf1d842cc)] - **esm**: improve commonjs hint on module not found (Daniele Belardi) [#31906](https://github.com/nodejs/node/pull/31906)
* \[[`7410e8d63a`](https://github.com/nodejs/node/commit/7410e8d63a)] - **esm**: port loader code to JS (Anna Henningsen) [#32201](https://github.com/nodejs/node/pull/32201)
* \[[`3241aee0f7`](https://github.com/nodejs/node/commit/3241aee0f7)] - **events**: convert errorMonitor to a normal property (Gerhard Stoebich) [#31848](https://github.com/nodejs/node/pull/31848)
* \[[`2093f13333`](https://github.com/nodejs/node/commit/2093f13333)] - **fs**: update validateOffsetLengthRead in utils.js (daemon1024) [#32896](https://github.com/nodejs/node/pull/32896)
* \[[`9c18838e8e`](https://github.com/nodejs/node/commit/9c18838e8e)] - **fs**: remove unnecessary else statement (Jesus Hernandez) [#32662](https://github.com/nodejs/node/pull/32662)
* \[[`6d6bb2a3dc`](https://github.com/nodejs/node/commit/6d6bb2a3dc)] - **fs**: use finished over destroy w/ cb (Robert Nagy) [#32809](https://github.com/nodejs/node/pull/32809)
* \[[`bde08377a1`](https://github.com/nodejs/node/commit/bde08377a1)] - **fs**: fix fs.read when passing null value (himself65) [#32479](https://github.com/nodejs/node/pull/32479)
* \[[`ebd9090240`](https://github.com/nodejs/node/commit/ebd9090240)] - **http**: disable headersTimeout check when set to zero (Paolo Insogna) [#33307](https://github.com/nodejs/node/pull/33307)
* \[[`a3decf5e59`](https://github.com/nodejs/node/commit/a3decf5e59)] - **http**: simplify sending header (Robert Nagy) [#33200](https://github.com/nodejs/node/pull/33200)
* \[[`12b8345db8`](https://github.com/nodejs/node/commit/12b8345db8)] - **http, async\_hooks**: remove unneeded reference to wrapping resource (Gerhard Stoebich) [#32054](https://github.com/nodejs/node/pull/32054)
* \[[`d60988161d`](https://github.com/nodejs/node/commit/d60988161d)] - **http,https**: increase server headers timeout (Tim Costa) [#30071](https://github.com/nodejs/node/pull/30071)
* \[[`d883024884`](https://github.com/nodejs/node/commit/d883024884)] - **http2**: wait for secureConnect before initializing (Benjamin Coe) [#32958](https://github.com/nodejs/node/pull/32958)
* \[[`79e95e49f7`](https://github.com/nodejs/node/commit/79e95e49f7)] - **inspector**: only write coverage in fully bootstrapped Environments (Joyee Cheung) [#32960](https://github.com/nodejs/node/pull/32960)
* \[[`9570644194`](https://github.com/nodejs/node/commit/9570644194)] - **lib**: cosmetic change to builtinLibs list for maintainability (James M Snell) [#33106](https://github.com/nodejs/node/pull/33106)
* \[[`6356ad42ab`](https://github.com/nodejs/node/commit/6356ad42ab)] - **lib**: fix validateport error message when allowZero is false (rickyes) [#32861](https://github.com/nodejs/node/pull/32861)
* \[[`698e21b346`](https://github.com/nodejs/node/commit/698e21b346)] - **lib**: add warning on dynamic import es modules (Juan José Arboleda) [#30720](https://github.com/nodejs/node/pull/30720)
* \[[`4dba3fcafd`](https://github.com/nodejs/node/commit/4dba3fcafd)] - **lib**: unnecessary const assignment for class (Yash Ladha) [#32962](https://github.com/nodejs/node/pull/32962)
* \[[`84571cec7e`](https://github.com/nodejs/node/commit/84571cec7e)] - **lib**: remove unnecesary else block (David Daza) [#32644](https://github.com/nodejs/node/pull/32644)
* \[[`5885b37bcc`](https://github.com/nodejs/node/commit/5885b37bcc)] - **lib**: created isValidCallback helper (Yash Ladha) [#32665](https://github.com/nodejs/node/pull/32665)
* \[[`5b1c34651e`](https://github.com/nodejs/node/commit/5b1c34651e)] - **lib**: removed unused error code (Yash Ladha) [#32481](https://github.com/nodejs/node/pull/32481)
* \[[`965452dbad`](https://github.com/nodejs/node/commit/965452dbad)] - **lib**: replace Array to ArrayIsArray by primordials (himself65) [#32258](https://github.com/nodejs/node/pull/32258)
* \[[`434ca8766a`](https://github.com/nodejs/node/commit/434ca8766a)] - **lib**: move isLegalPort to validators, refactor (James M Snell) [#31851](https://github.com/nodejs/node/pull/31851)
* \[[`65ebfb2f12`](https://github.com/nodejs/node/commit/65ebfb2f12)] - **lib**: delete dead code in SourceMap (Justin Ridgewell) [#31512](https://github.com/nodejs/node/pull/31512)
* \[[`b1f08b8359`](https://github.com/nodejs/node/commit/b1f08b8359)] - **module**: no type module resolver side effects (Guy Bedford) [#33086](https://github.com/nodejs/node/pull/33086)
* \[[`a1fa180079`](https://github.com/nodejs/node/commit/a1fa180079)] - **module**: partial doc removal of --experimental-modules (Myles Borins) [#32915](https://github.com/nodejs/node/pull/32915)
* \[[`195043f910`](https://github.com/nodejs/node/commit/195043f910)] - **module**: refactor condition (Myles Borins) [#32989](https://github.com/nodejs/node/pull/32989)
* \[[`1811a10415`](https://github.com/nodejs/node/commit/1811a10415)] - **module**: exports not exported for null resolutions (Guy Bedford) [#32838](https://github.com/nodejs/node/pull/32838)
* \[[`3dc3772bb0`](https://github.com/nodejs/node/commit/3dc3772bb0)] - **module**: improve error for invalid package targets (Myles Borins) [#32052](https://github.com/nodejs/node/pull/32052)
* \[[`6489a5b1d8`](https://github.com/nodejs/node/commit/6489a5b1d8)] - **module**: fix memory leak when require error occurs (Qinhui Chen) [#32837](https://github.com/nodejs/node/pull/32837)
* \[[`b62910c851`](https://github.com/nodejs/node/commit/b62910c851)] - **module**: expose exports conditions to loaders (Jan Krems) [#31303](https://github.com/nodejs/node/pull/31303)
* \[[`b62db597af`](https://github.com/nodejs/node/commit/b62db597af)] - **module**: port source map sort logic from chromium (Benjamin Coe) [#31927](https://github.com/nodejs/node/pull/31927)
* \[[`4d7f9869f3`](https://github.com/nodejs/node/commit/4d7f9869f3)] - **n-api**: simplify uv\_idle wrangling (Ben Noordhuis) [#32997](https://github.com/nodejs/node/pull/32997)
* \[[`d08be9c8ca`](https://github.com/nodejs/node/commit/d08be9c8ca)] - **n-api**: fix false assumption on napi\_async\_context structures (legendecas) [#32928](https://github.com/nodejs/node/pull/32928)
* \[[`fbd39436a0`](https://github.com/nodejs/node/commit/fbd39436a0)] - **n-api**: fix comment on expected N-API version (Michael Dawson) [#32236](https://github.com/nodejs/node/pull/32236)
* \[[`d50fe6c1ea`](https://github.com/nodejs/node/commit/d50fe6c1ea)] - **path**: fix comment grammar (thecodrr) [#32942](https://github.com/nodejs/node/pull/32942)
* \[[`8dcb22f735`](https://github.com/nodejs/node/commit/8dcb22f735)] - **perf\_hooks**: remove unnecessary assignment when name is undefined (rickyes) [#32910](https://github.com/nodejs/node/pull/32910)
* \[[`f537377957`](https://github.com/nodejs/node/commit/f537377957)] - **process**: fix two overflow cases in SourceMap VLQ decoding (Justin Ridgewell) [#31490](https://github.com/nodejs/node/pull/31490)
* \[[`7582bce58d`](https://github.com/nodejs/node/commit/7582bce58d)] - **readline**: improve unicode support and tab completion (Ruben Bridgewater) [#31288](https://github.com/nodejs/node/pull/31288)
* \[[`5231c84396`](https://github.com/nodejs/node/commit/5231c84396)] - **readline**: move charLengthLeft() and charLengthAt() (Ruben Bridgewater) [#31112](https://github.com/nodejs/node/pull/31112)
* \[[`03efa716f0`](https://github.com/nodejs/node/commit/03efa716f0)] - **readline**: improve getStringWidth() (Ruben Bridgewater) [#31112](https://github.com/nodejs/node/pull/31112)
* \[[`e894eeb22d`](https://github.com/nodejs/node/commit/e894eeb22d)] - **readline**: set null as callback return in case there's no error (Ruben Bridgewater) [#31006](https://github.com/nodejs/node/pull/31006)
* \[[`3946cadf89`](https://github.com/nodejs/node/commit/3946cadf89)] - **readline**: small refactoring (Ruben Bridgewater) [#31006](https://github.com/nodejs/node/pull/31006)
* \[[`0bafe087e4`](https://github.com/nodejs/node/commit/0bafe087e4)] - **readline**: update ansi-regex (Ruben Bridgewater) [#30907](https://github.com/nodejs/node/pull/30907)
* \[[`4e9e4402c5`](https://github.com/nodejs/node/commit/4e9e4402c5)] - **readline,repl**: support tabs properly (Ruben Bridgewater) [#31112](https://github.com/nodejs/node/pull/31112)
* \[[`3903aec0b4`](https://github.com/nodejs/node/commit/3903aec0b4)] - **repl**: align preview with the actual executed code (Ruben Bridgewater) [#32154](https://github.com/nodejs/node/pull/32154)
* \[[`709d3e5eb3`](https://github.com/nodejs/node/commit/709d3e5eb3)] - **repl**: eager-evaluate input in parens (Shelley Vohr) [#31943](https://github.com/nodejs/node/pull/31943)
* \[[`ce5c9d771c`](https://github.com/nodejs/node/commit/ce5c9d771c)] - **repl**: do not preview while pasting code (Ruben Bridgewater) [#31315](https://github.com/nodejs/node/pull/31315)
* \[[`3867f2095e`](https://github.com/nodejs/node/commit/3867f2095e)] - **repl**: fix preview cursor position (Ruben Bridgewater) [#31293](https://github.com/nodejs/node/pull/31293)
* \[[`ee40b67413`](https://github.com/nodejs/node/commit/ee40b67413)] - **repl**: change preview default in case of custom eval functions (Ruben Bridgewater) [#31259](https://github.com/nodejs/node/pull/31259)
* \[[`a4ca3787ea`](https://github.com/nodejs/node/commit/a4ca3787ea)] - **repl**: activate previews for lines exceeding the terminal columns (Ruben Bridgewater) [#31112](https://github.com/nodejs/node/pull/31112)
* \[[`a892b4d00c`](https://github.com/nodejs/node/commit/a892b4d00c)] - **repl**: improve preview length calculation (Ruben Bridgewater) [#31112](https://github.com/nodejs/node/pull/31112)
* \[[`9abe0e32d8`](https://github.com/nodejs/node/commit/9abe0e32d8)] - **repl**: use public getCursorPos() (Colin Ihrig) [#31091](https://github.com/nodejs/node/pull/31091)
* \[[`85f8654415`](https://github.com/nodejs/node/commit/85f8654415)] - **repl**: fix preview of lines that exceed the terminal columns (Ruben Bridgewater) [#31006](https://github.com/nodejs/node/pull/31006)
* \[[`47dfa22adb`](https://github.com/nodejs/node/commit/47dfa22adb)] - **repl**: fix preview bug in case of long lines (Ruben Bridgewater) [#30907](https://github.com/nodejs/node/pull/30907)
* \[[`7131de5f77`](https://github.com/nodejs/node/commit/7131de5f77)] - **repl**: improve completion (Ruben Bridgewater) [#30907](https://github.com/nodejs/node/pull/30907)
* \[[`61886507ce`](https://github.com/nodejs/node/commit/61886507ce)] - **repl**: simplify code (Ruben Bridgewater) [#30907](https://github.com/nodejs/node/pull/30907)
* \[[`9b893e1bee`](https://github.com/nodejs/node/commit/9b893e1bee)] - **repl**: simplify repl autocompletion (Ruben Bridgewater) [#30907](https://github.com/nodejs/node/pull/30907)
* \[[`78dcdee35f`](https://github.com/nodejs/node/commit/78dcdee35f)] - **repl**: remove dead code (Ruben Bridgewater) [#30907](https://github.com/nodejs/node/pull/30907)
* \[[`f588301f2d`](https://github.com/nodejs/node/commit/f588301f2d)] - **repl,readline**: clean up code (Ruben Bridgewater) [#31288](https://github.com/nodejs/node/pull/31288)
* \[[`8be00314a6`](https://github.com/nodejs/node/commit/8be00314a6)] - **repl,readline**: refactor for simplicity (Ruben Bridgewater) [#30907](https://github.com/nodejs/node/pull/30907)
* \[[`6eda28c69f`](https://github.com/nodejs/node/commit/6eda28c69f)] - **repl,readline**: refactor common code (Ruben Bridgewater) [#30907](https://github.com/nodejs/node/pull/30907)
* \[[`f945a5e3e1`](https://github.com/nodejs/node/commit/f945a5e3e1)] - **report**: fix stderr matching for fatal error (gengjiawen) [#32699](https://github.com/nodejs/node/pull/32699)
* \[[`4b96fc522c`](https://github.com/nodejs/node/commit/4b96fc522c)] - **report**: add missing locks for report\_on\_fatalerror accessors (Anna Henningsen) [#32535](https://github.com/nodejs/node/pull/32535)
* \[[`c126d28c2e`](https://github.com/nodejs/node/commit/c126d28c2e)] - **report**: handle on-fatalerror better (Harshitha KP) [#32207](https://github.com/nodejs/node/pull/32207)
* \[[`85ef383bc5`](https://github.com/nodejs/node/commit/85ef383bc5)] - **src**: remove unused v8 Message namespace (Adrian Estrada) [#33180](https://github.com/nodejs/node/pull/33180)
* \[[`ffca498ca2`](https://github.com/nodejs/node/commit/ffca498ca2)] - **src**: use unique\_ptr for CachedData in ContextifyScript::New (Anna Henningsen) [#33113](https://github.com/nodejs/node/pull/33113)
* \[[`b3f0417830`](https://github.com/nodejs/node/commit/b3f0417830)] - **src**: return undefined when validation err == 0 (James M Snell) [#33107](https://github.com/nodejs/node/pull/33107)
* \[[`1436977984`](https://github.com/nodejs/node/commit/1436977984)] - **src**: crypto::UseSNIContext to use BaseObjectPtr (James M Snell) [#33107](https://github.com/nodejs/node/pull/33107)
* \[[`6b1e2359c2`](https://github.com/nodejs/node/commit/6b1e2359c2)] - **src**: separate out NgLibMemoryManagerBase (James M Snell) [#33104](https://github.com/nodejs/node/pull/33104)
* \[[`8864353c6e`](https://github.com/nodejs/node/commit/8864353c6e)] - **src**: remove unnecessary fully qualified names (rickyes) [#33077](https://github.com/nodejs/node/pull/33077)
* \[[`62f29534de`](https://github.com/nodejs/node/commit/62f29534de)] - **src**: add AsyncWrapObject constructor template factory (Stephen Belanger) [#33051](https://github.com/nodejs/node/pull/33051)
* \[[`08b66f223d`](https://github.com/nodejs/node/commit/08b66f223d)] - **src**: do not compare against wide characters (Christopher Beeson) [#32921](https://github.com/nodejs/node/pull/32921)
* \[[`60db9afde5`](https://github.com/nodejs/node/commit/60db9afde5)] - **src**: fix empty-named env var assertion failure (Christopher Beeson) [#32921](https://github.com/nodejs/node/pull/32921)
* \[[`b893c5b7ba`](https://github.com/nodejs/node/commit/b893c5b7ba)] - **src**: assignment to valid type (Yash Ladha) [#32879](https://github.com/nodejs/node/pull/32879)
* \[[`846d7bdbbf`](https://github.com/nodejs/node/commit/846d7bdbbf)] - **src**: delete MicroTaskPolicy namespace (Juan José Arboleda) [#32853](https://github.com/nodejs/node/pull/32853)
* \[[`05059a2469`](https://github.com/nodejs/node/commit/05059a2469)] - **src**: use using NewStringType (rickyes) [#32843](https://github.com/nodejs/node/pull/32843)
* \[[`cf16cb7ed5`](https://github.com/nodejs/node/commit/cf16cb7ed5)] - **src**: fix null deref in AllocatedBuffer::clear (Matt Kulukundis) [#32892](https://github.com/nodejs/node/pull/32892)
* \[[`0745f8884c`](https://github.com/nodejs/node/commit/0745f8884c)] - **src**: remove validation of unreachable code (Juan José Arboleda) [#32818](https://github.com/nodejs/node/pull/32818)
* \[[`9c216640d7`](https://github.com/nodejs/node/commit/9c216640d7)] - **src**: elevate v8 namespaces (Nimit) [#32872](https://github.com/nodejs/node/pull/32872)
* \[[`71bdcaeac7`](https://github.com/nodejs/node/commit/71bdcaeac7)] - **src**: remove redundant v8::HeapSnapshot namespace (Juan José Arboleda) [#32854](https://github.com/nodejs/node/pull/32854)
* \[[`bb1481fd23`](https://github.com/nodejs/node/commit/bb1481fd23)] - **src**: remove unused using in node\_worker.cc (Daniel Bevenius) [#32840](https://github.com/nodejs/node/pull/32840)
* \[[`8a38726826`](https://github.com/nodejs/node/commit/8a38726826)] - **src**: ignore GCC -Wcast-function-type for v8.h (Daniel Bevenius) [#32679](https://github.com/nodejs/node/pull/32679)
* \[[`c26637b7da`](https://github.com/nodejs/node/commit/c26637b7da)] - **src**: remove unused v8 Array namespace (Juan José Arboleda) [#32749](https://github.com/nodejs/node/pull/32749)
* \[[`c0d3fc28ec`](https://github.com/nodejs/node/commit/c0d3fc28ec)] - **src**: sync access for report and openssl options (Sam Roberts) [#32618](https://github.com/nodejs/node/pull/32618)
* \[[`9a010a3ea5`](https://github.com/nodejs/node/commit/9a010a3ea5)] - **src**: munmap(2) upon class instance destructor (Gabriel Schulhof) [#32570](https://github.com/nodejs/node/pull/32570)
* \[[`06953df051`](https://github.com/nodejs/node/commit/06953df051)] - **src**: fix extra includes of "env.h" and "env-inl.h" (Nick Kreeger) [#32293](https://github.com/nodejs/node/pull/32293)
* \[[`7432d0a170`](https://github.com/nodejs/node/commit/7432d0a170)] - **src**: avoid using elevated v8 namespaces in node\_perf.h (James M Snell) [#32468](https://github.com/nodejs/node/pull/32468)
* \[[`6175a22b87`](https://github.com/nodejs/node/commit/6175a22b87)] - **src**: avoid using elevated v8 namespaces in node\_errors.h (James M Snell) [#32468](https://github.com/nodejs/node/pull/32468)
* \[[`464ff85ddd`](https://github.com/nodejs/node/commit/464ff85ddd)] - **src**: remove loop\_init\_failed\_ from Worker class (Anna Henningsen) [#32562](https://github.com/nodejs/node/pull/32562)
* \[[`9f6ed724e0`](https://github.com/nodejs/node/commit/9f6ed724e0)] - **src**: clean up worker thread creation code (Anna Henningsen) [#32562](https://github.com/nodejs/node/pull/32562)
* \[[`73c55d39f3`](https://github.com/nodejs/node/commit/73c55d39f3)] - **src**: include AsyncWrap provider strings in snapshot (Anna Henningsen) [#32572](https://github.com/nodejs/node/pull/32572)
* \[[`29eca36ea8`](https://github.com/nodejs/node/commit/29eca36ea8)] - **src**: move JSONWriter into its own file (Anna Henningsen) [#32552](https://github.com/nodejs/node/pull/32552)
* \[[`8e3dd47db7`](https://github.com/nodejs/node/commit/8e3dd47db7)] - **src**: handle report options on fatalerror (Sam Roberts) [#32497](https://github.com/nodejs/node/pull/32497)
* \[[`e0351945bc`](https://github.com/nodejs/node/commit/e0351945bc)] - **src**: refactoring and cleanup of node\_i18n (James M Snell) [#32438](https://github.com/nodejs/node/pull/32438)
* \[[`23f8f35022`](https://github.com/nodejs/node/commit/23f8f35022)] - **src**: unify Linux and FreeBSD large pages implem (Gabriel Schulhof) [#32534](https://github.com/nodejs/node/pull/32534)
* \[[`16d85d9328`](https://github.com/nodejs/node/commit/16d85d9328)] - **src**: fix compiler warnings in node\_report\_module (Daniel Bevenius) [#32498](https://github.com/nodejs/node/pull/32498)
* \[[`58aadcdacf`](https://github.com/nodejs/node/commit/58aadcdacf)] - **src**: simplify large pages mapping code (Gabriel Schulhof) [#32396](https://github.com/nodejs/node/pull/32396)
* \[[`2da974e15e`](https://github.com/nodejs/node/commit/2da974e15e)] - **src**: use single ObjectTemplate for TextDecoder (Anna Henningsen) [#32426](https://github.com/nodejs/node/pull/32426)
* \[[`8f7f4e5aba`](https://github.com/nodejs/node/commit/8f7f4e5aba)] - **src**: avoid Isolate::GetCurrent() for platform implementation (Anna Henningsen) [#32269](https://github.com/nodejs/node/pull/32269)
* \[[`df046dec97`](https://github.com/nodejs/node/commit/df046dec97)] - **src**: add debug option to report large page stats (Gabriel Schulhof) [#32331](https://github.com/nodejs/node/pull/32331)
* \[[`43e9ae8317`](https://github.com/nodejs/node/commit/43e9ae8317)] - **src**: prefer OnScopeLeave over shared\_ptr\<void> (Anna Henningsen) [#32247](https://github.com/nodejs/node/pull/32247)
* \[[`2f976d783f`](https://github.com/nodejs/node/commit/2f976d783f)] - **src**: find .text section using dl\_iterate\_phdr (Gabriel Schulhof) [#32244](https://github.com/nodejs/node/pull/32244)
* \[[`40c5d58095`](https://github.com/nodejs/node/commit/40c5d58095)] - _**Revert**_ "**src**: keep main-thread Isolate attached to platform during Dispose" (Anna Henningsen) [#31853](https://github.com/nodejs/node/pull/31853)
* \[[`51a345674e`](https://github.com/nodejs/node/commit/51a345674e)] - **src**: handle NULL env scenario (Harshitha KP) [#31899](https://github.com/nodejs/node/pull/31899)
* \[[`154da1f0d3`](https://github.com/nodejs/node/commit/154da1f0d3)] - **src**: add missing namespace using statements in node\_watchdog.h (legendecas) [#32117](https://github.com/nodejs/node/pull/32117)
* \[[`83c47b6079`](https://github.com/nodejs/node/commit/83c47b6079)] - **src**: introduce node\_sockaddr (James M Snell) [#32070](https://github.com/nodejs/node/pull/32070)
* \[[`c979aeaf26`](https://github.com/nodejs/node/commit/c979aeaf26)] - **src**: improve handling of internal field counting (James M Snell) [#31960](https://github.com/nodejs/node/pull/31960)
* \[[`38de40ac50`](https://github.com/nodejs/node/commit/38de40ac50)] - **src**: do not unnecessarily re-assign uv handle data (Anna Henningsen) [#31696](https://github.com/nodejs/node/pull/31696)
* \[[`e204dba3f3`](https://github.com/nodejs/node/commit/e204dba3f3)] - **src**: pass resource object along with InternalMakeCallback (Anna Henningsen) [#32063](https://github.com/nodejs/node/pull/32063)
* \[[`ffefb059e2`](https://github.com/nodejs/node/commit/ffefb059e2)] - **src**: move InternalCallbackScope to StartExecution (Shelley Vohr) [#31944](https://github.com/nodejs/node/pull/31944)
* \[[`178c682ad1`](https://github.com/nodejs/node/commit/178c682ad1)] - **src**: start the .text section with an asm symbol (Gabriel Schulhof) [#31981](https://github.com/nodejs/node/pull/31981)
* \[[`809d8b5036`](https://github.com/nodejs/node/commit/809d8b5036)] - **src**: include large pages source unconditionally (Gabriel Schulhof) [#31904](https://github.com/nodejs/node/pull/31904)
* \[[`5ea3d60db1`](https://github.com/nodejs/node/commit/5ea3d60db1)] - **src**: use \_\_executable\_start for linux hugepages (Ben Noordhuis) [#31547](https://github.com/nodejs/node/pull/31547)
* \[[`1e95bb85a9`](https://github.com/nodejs/node/commit/1e95bb85a9)] - **src**: make large\_pages node.cc include conditional (Denys Otrishko) [#31078](https://github.com/nodejs/node/pull/31078)
* \[[`6dcb868a0a`](https://github.com/nodejs/node/commit/6dcb868a0a)] - **src**: make --use-largepages a runtime option (Gabriel Schulhof) [#30954](https://github.com/nodejs/node/pull/30954)
* \[[`f3fb6a11fe`](https://github.com/nodejs/node/commit/f3fb6a11fe)] - **src**: change GetStringWidth's expand\_emoji\_sequence option default (Ruben Bridgewater) [#31112](https://github.com/nodejs/node/pull/31112)
* \[[`4f6300f804`](https://github.com/nodejs/node/commit/4f6300f804)] - **src**: improve GetColumnWidth performance (Ruben Bridgewater) [#31112](https://github.com/nodejs/node/pull/31112)
* \[[`98297b92f5`](https://github.com/nodejs/node/commit/98297b92f5)] - **src**: inline SetSNICallback (Anna Henningsen) [#30548](https://github.com/nodejs/node/pull/30548)
* \[[`ce8d8c06ac`](https://github.com/nodejs/node/commit/ce8d8c06ac)] - **src**: use BaseObjectPtr to store SNI context (Anna Henningsen) [#30548](https://github.com/nodejs/node/pull/30548)
* \[[`c86883e4fe`](https://github.com/nodejs/node/commit/c86883e4fe)] - **stream**: add null check in Readable.from (Pranshu Srivastava) [#32873](https://github.com/nodejs/node/pull/32873)
* \[[`5df8ab16f2`](https://github.com/nodejs/node/commit/5df8ab16f2)] - **stream**: close iterator in Readable.from (Vadzim Zieńka) [#32844](https://github.com/nodejs/node/pull/32844)
* \[[`c8b4ab0978`](https://github.com/nodejs/node/commit/c8b4ab0978)] - **stream**: fix readable state `awaitDrain` increase in recursion (ran) [#27572](https://github.com/nodejs/node/pull/27572)
* \[[`becbe9e246`](https://github.com/nodejs/node/commit/becbe9e246)] - **tls**: move getAllowUnauthorized to internal/options (James M Snell) [#32917](https://github.com/nodejs/node/pull/32917)
* \[[`dec8a21cc8`](https://github.com/nodejs/node/commit/dec8a21cc8)] - **tls**: provide default cipher list from command line (Anna Henningsen) [#32760](https://github.com/nodejs/node/pull/32760)
* \[[`8961d33aff`](https://github.com/nodejs/node/commit/8961d33aff)] - **tls**: add memory tracking support to SSLWrap (Anna Henningsen) [#30548](https://github.com/nodejs/node/pull/30548)
* \[[`1b41829828`](https://github.com/nodejs/node/commit/1b41829828)] - **util**: improve unicode support (Ruben Bridgewater) [#31319](https://github.com/nodejs/node/pull/31319)
* \[[`a0b1a06fff`](https://github.com/nodejs/node/commit/a0b1a06fff)] - **util**: add todo comments for inspect to add unicode support (Ruben Bridgewater) [#31112](https://github.com/nodejs/node/pull/31112)
* \[[`e0e8a9af6f`](https://github.com/nodejs/node/commit/e0e8a9af6f)] - **util,readline**: NFC-normalize strings before getStringWidth (Anna Henningsen) [#33052](https://github.com/nodejs/node/pull/33052)
* \[[`6a9f867e56`](https://github.com/nodejs/node/commit/6a9f867e56)] - **vm**: throw error when duplicated exportNames in SyntheticModule (himself65) [#32810](https://github.com/nodejs/node/pull/32810)
* \[[`02de66a110`](https://github.com/nodejs/node/commit/02de66a110)] - **vm**: lazily initialize primordials for vm contexts (Joyee Cheung) [#31738](https://github.com/nodejs/node/pull/31738)
* \[[`843a54fd33`](https://github.com/nodejs/node/commit/843a54fd33)] - **wasi**: use free() to release preopen array (Anna Henningsen) [#33110](https://github.com/nodejs/node/pull/33110)
* \[[`7f845e614b`](https://github.com/nodejs/node/commit/7f845e614b)] - **wasi**: update start() behavior to match spec (Colin Ihrig) [#33073](https://github.com/nodejs/node/pull/33073)
* \[[`e1fe0b66b5`](https://github.com/nodejs/node/commit/e1fe0b66b5)] - **wasi**: rename \_\_wasi\_unstable\_reactor\_start() (Colin Ihrig) [#33073](https://github.com/nodejs/node/pull/33073)
* \[[`7c723af3ae`](https://github.com/nodejs/node/commit/7c723af3ae)] - **wasi**: clean up options validation (Denys Otrishko) [#31797](https://github.com/nodejs/node/pull/31797)
* \[[`9ce6e363f4`](https://github.com/nodejs/node/commit/9ce6e363f4)] - **worker**: fix process.env var empty key access (Christopher Beeson) [#32921](https://github.com/nodejs/node/pull/32921)
* \[[`57cd7d2faa`](https://github.com/nodejs/node/commit/57cd7d2faa)] - **worker**: fix type check in receiveMessageOnPort (Anna Henningsen) [#32745](https://github.com/nodejs/node/pull/32745)
* \[[`ade4ec6f9a`](https://github.com/nodejs/node/commit/ade4ec6f9a)] - **worker**: runtime error on pthread creation (Harshitha KP) [#32344](https://github.com/nodejs/node/pull/32344)

#### Documentation commits

* \[[`51c3c8d1e8`](https://github.com/nodejs/node/commit/51c3c8d1e8)] - **doc**: fix a typo in crypto.generateKeyPairSync() (himself65) [#33187](https://github.com/nodejs/node/pull/33187)
* \[[`62143b5cb9`](https://github.com/nodejs/node/commit/62143b5cb9)] - **doc**: add util.types.isArrayBufferView() (Kevin Locke) [#33092](https://github.com/nodejs/node/pull/33092)
* \[[`f127ae3102`](https://github.com/nodejs/node/commit/f127ae3102)] - **doc**: clarify when not to run CI on docs (Juan José Arboleda) [#33101](https://github.com/nodejs/node/pull/33101)
* \[[`7c8b0d27eb`](https://github.com/nodejs/node/commit/7c8b0d27eb)] - **doc**: fix the spelling error in stream.md (白一梓) [#31561](https://github.com/nodejs/node/pull/31561)
* \[[`31b143ccf1`](https://github.com/nodejs/node/commit/31b143ccf1)] - **doc**: correct Nodejs to Node.js spelling (Nick Schonning) [#33088](https://github.com/nodejs/node/pull/33088)
* \[[`2ac0f20019`](https://github.com/nodejs/node/commit/2ac0f20019)] - **doc**: improve worker pool example (Ranjan Purbey) [#33082](https://github.com/nodejs/node/pull/33082)
* \[[`90cf88614e`](https://github.com/nodejs/node/commit/90cf88614e)] - **doc**: some grammar fixes (Chris Holland) [#33081](https://github.com/nodejs/node/pull/33081)
* \[[`8a9be1d071`](https://github.com/nodejs/node/commit/8a9be1d071)] - **doc**: don't check links in tmp dirs (Ben Noordhuis) [#32996](https://github.com/nodejs/node/pull/32996)
* \[[`5ea5c26442`](https://github.com/nodejs/node/commit/5ea5c26442)] - **doc**: improve WHATWG url constructor code example (Liran Tal) [#32782](https://github.com/nodejs/node/pull/32782)
* \[[`c90a070735`](https://github.com/nodejs/node/commit/c90a070735)] - **doc**: make openssl maintenance position independent (Sam Roberts) [#32977](https://github.com/nodejs/node/pull/32977)
* \[[`d75f644dc2`](https://github.com/nodejs/node/commit/d75f644dc2)] - **doc**: improve release documentation (Michaël Zasso) [#33042](https://github.com/nodejs/node/pull/33042)
* \[[`98c3c427fc`](https://github.com/nodejs/node/commit/98c3c427fc)] - **doc**: add documentation for transferList arg at worker threads (Juan José Arboleda) [#32881](https://github.com/nodejs/node/pull/32881)
* \[[`9038e64619`](https://github.com/nodejs/node/commit/9038e64619)] - **doc**: avoid tautology in README (Ishaan Jain) [#33005](https://github.com/nodejs/node/pull/33005)
* \[[`cb7dae3385`](https://github.com/nodejs/node/commit/cb7dae3385)] - **doc**: updated directory entry information (Eileen) [#32791](https://github.com/nodejs/node/pull/32791)
* \[[`7397f80dc2`](https://github.com/nodejs/node/commit/7397f80dc2)] - **doc**: ignore no-literal-urls in README (Nick Schonning) [#32676](https://github.com/nodejs/node/pull/32676)
* \[[`d8bb226c0d`](https://github.com/nodejs/node/commit/d8bb226c0d)] - **doc**: convert bare email addresses to mailto links (Nick Schonning) [#32676](https://github.com/nodejs/node/pull/32676)
* \[[`b057175893`](https://github.com/nodejs/node/commit/b057175893)] - **doc**: ignore no-literal-urls in changelogs (Nick Schonning) [#32676](https://github.com/nodejs/node/pull/32676)
* \[[`92d91d1e78`](https://github.com/nodejs/node/commit/92d91d1e78)] - **doc**: add angle brackets around implicit links (Nick Schonning) [#32676](https://github.com/nodejs/node/pull/32676)
* \[[`e5a24507f2`](https://github.com/nodejs/node/commit/e5a24507f2)] - **doc**: remove repeated word in modules.md (Prosper Opara) [#32931](https://github.com/nodejs/node/pull/32931)
* \[[`a7ec1ea38d`](https://github.com/nodejs/node/commit/a7ec1ea38d)] - **doc**: elevate diagnostic report to tier1 (Gireesh Punathil) [#32732](https://github.com/nodejs/node/pull/32732)
* \[[`931c0c74b0`](https://github.com/nodejs/node/commit/931c0c74b0)] - **doc**: fix typo in security-release-process.md (Edward Elric) [#32926](https://github.com/nodejs/node/pull/32926)
* \[[`26cf6a3752`](https://github.com/nodejs/node/commit/26cf6a3752)] - **doc**: fix usage of folder and directory terms in fs.md (karan singh virdi) [#32919](https://github.com/nodejs/node/pull/32919)
* \[[`1472a39819`](https://github.com/nodejs/node/commit/1472a39819)] - **doc**: fix typo in zlib.md (雨夜带刀) [#32901](https://github.com/nodejs/node/pull/32901)
* \[[`26fdc64d96`](https://github.com/nodejs/node/commit/26fdc64d96)] - **doc**: synch SECURITY.md with website (Rich Trott) [#32903](https://github.com/nodejs/node/pull/32903)
* \[[`6ddf37cbfd`](https://github.com/nodejs/node/commit/6ddf37cbfd)] - **doc**: add `tsc-agenda` to onboarding labels list (Rich Trott) [#32832](https://github.com/nodejs/node/pull/32832)
* \[[`f7b78f221d`](https://github.com/nodejs/node/commit/f7b78f221d)] - **doc**: add N-API version 6 to table (Michael Dawson) [#32829](https://github.com/nodejs/node/pull/32829)
* \[[`aed5112889`](https://github.com/nodejs/node/commit/aed5112889)] - **doc**: add juanarbol as collaborator (Juan José Arboleda) [#32906](https://github.com/nodejs/node/pull/32906)
* \[[`68a1cec37f`](https://github.com/nodejs/node/commit/68a1cec37f)] - **doc**: missing brackets (William Bonawentura) [#32657](https://github.com/nodejs/node/pull/32657)
* \[[`8d53024889`](https://github.com/nodejs/node/commit/8d53024889)] - **doc**: improve consistency in usage of NULL (Michael Dawson) [#32726](https://github.com/nodejs/node/pull/32726)
* \[[`e9927e5587`](https://github.com/nodejs/node/commit/e9927e5587)] - **doc**: improve net docs (Robert Nagy) [#32811](https://github.com/nodejs/node/pull/32811)
* \[[`8e7c41ee55`](https://github.com/nodejs/node/commit/8e7c41ee55)] - **doc**: note that signatures of binary may be from subkeys (Reşat SABIQ) [#32591](https://github.com/nodejs/node/pull/32591)
* \[[`873d4aaaa2`](https://github.com/nodejs/node/commit/873d4aaaa2)] - **doc**: add transform stream destroy() return value (Colin Ihrig) [#32788](https://github.com/nodejs/node/pull/32788)
* \[[`39da5bfca7`](https://github.com/nodejs/node/commit/39da5bfca7)] - **doc**: updated guidance for n-api changes (Michael Dawson) [#32721](https://github.com/nodejs/node/pull/32721)
* \[[`38e51d335d`](https://github.com/nodejs/node/commit/38e51d335d)] - **doc**: remove warning from `response.writeHead` (Cecchi MacNaughton) [#32700](https://github.com/nodejs/node/pull/32700)
* \[[`fa51d859b5`](https://github.com/nodejs/node/commit/fa51d859b5)] - **doc**: document `buffer.from` returns internal pool buffer (Harshitha KP) [#32703](https://github.com/nodejs/node/pull/32703)
* \[[`47d2a9604b`](https://github.com/nodejs/node/commit/47d2a9604b)] - **doc**: add puzpuzpuz to collaborators (Andrey Pechkurov) [#32817](https://github.com/nodejs/node/pull/32817)
* \[[`ad2fdd5142`](https://github.com/nodejs/node/commit/ad2fdd5142)] - **doc**: make openssl commit messages be valid (Sam Roberts) [#32602](https://github.com/nodejs/node/pull/32602)
* \[[`a55d215b08`](https://github.com/nodejs/node/commit/a55d215b08)] - **doc**: add missing changes: entry for dns.ALL (Anna Henningsen) [#32617](https://github.com/nodejs/node/pull/32617)
* \[[`13342f884b`](https://github.com/nodejs/node/commit/13342f884b)] - **doc**: fix typo in maintaining-openssl guide (Nitin Kumar) [#32292](https://github.com/nodejs/node/pull/32292)
* \[[`d7e4bb20a7`](https://github.com/nodejs/node/commit/d7e4bb20a7)] - **doc**: add missing changes: entry for mkdir (Anna Henningsen) [#32490](https://github.com/nodejs/node/pull/32490)
* \[[`742a032775`](https://github.com/nodejs/node/commit/742a032775)] - **doc**: complete n-api version matrix (Gabriel Schulhof) [#32304](https://github.com/nodejs/node/pull/32304)
* \[[`f5b60ec8dd`](https://github.com/nodejs/node/commit/f5b60ec8dd)] - **doc**: change worker.takeHeapSnapshot to getHeapSnapshot (Gerhard Stoebich) [#32061](https://github.com/nodejs/node/pull/32061)
* \[[`c0cf234da9`](https://github.com/nodejs/node/commit/c0cf234da9)] - **doc**: remove personal pronoun usage in policy.md (Rich Trott) [#32142](https://github.com/nodejs/node/pull/32142)
* \[[`97689e05fd`](https://github.com/nodejs/node/commit/97689e05fd)] - **doc**: remove personal pronoun usage in fs.md (Rich Trott) [#32142](https://github.com/nodejs/node/pull/32142)
* \[[`9a7f89245d`](https://github.com/nodejs/node/commit/9a7f89245d)] - **doc**: remove personal pronoun usage in errors.md (Rich Trott) [#32142](https://github.com/nodejs/node/pull/32142)
* \[[`c98ba9537b`](https://github.com/nodejs/node/commit/c98ba9537b)] - **doc**: remove personal pronoun usage in addons.md (Rich Trott) [#32142](https://github.com/nodejs/node/pull/32142)
* \[[`e8985c2e87`](https://github.com/nodejs/node/commit/e8985c2e87)] - **doc**: improve AsyncLocalStorage sample (Andrey Pechkurov) [#32757](https://github.com/nodejs/node/pull/32757)
* \[[`9d9e185e3a`](https://github.com/nodejs/node/commit/9d9e185e3a)] - **doc**: list largepage values in --help (Colin Ihrig) [#31537](https://github.com/nodejs/node/pull/31537)
* \[[`f13ecd801e`](https://github.com/nodejs/node/commit/f13ecd801e)] - **doc**: fix typo in maintaining-zlib guide (Nitin Kumar) [#32292](https://github.com/nodejs/node/pull/32292)
* \[[`3e939ffb5e`](https://github.com/nodejs/node/commit/3e939ffb5e)] - **doc**: describe how to update zlib (Sam Roberts) [#31800](https://github.com/nodejs/node/pull/31800)
* \[[`1d2565b8b6`](https://github.com/nodejs/node/commit/1d2565b8b6)] - **doc**: document readline key bindings (Harshitha KP) [#31256](https://github.com/nodejs/node/pull/31256)
* \[[`8e829d4a56`](https://github.com/nodejs/node/commit/8e829d4a56)] - _**Revert**_ "**doc**: fix default server timeout description for https" (Michaël Zasso) [#33069](https://github.com/nodejs/node/pull/33069)

#### Other commits

* \[[`cdf4f4875d`](https://github.com/nodejs/node/commit/cdf4f4875d)] - **benchmark**: use let instead of var in worker (Daniele Belardi) [#31794](https://github.com/nodejs/node/pull/31794)
* \[[`c572218552`](https://github.com/nodejs/node/commit/c572218552)] - **benchmark**: use let instead of var in util (Daniele Belardi) [#31794](https://github.com/nodejs/node/pull/31794)
* \[[`862aeae238`](https://github.com/nodejs/node/commit/862aeae238)] - **benchmark**: use let instead of var in url (Daniele Belardi) [#31794](https://github.com/nodejs/node/pull/31794)
* \[[`e68c21f079`](https://github.com/nodejs/node/commit/e68c21f079)] - **benchmark**: use let instead of var in tls (Daniele Belardi) [#31794](https://github.com/nodejs/node/pull/31794)
* \[[`f3ef8946d0`](https://github.com/nodejs/node/commit/f3ef8946d0)] - **benchmark**: use let instead of var in timers (Daniele Belardi) [#31794](https://github.com/nodejs/node/pull/31794)
* \[[`33858fa917`](https://github.com/nodejs/node/commit/33858fa917)] - **benchmark**: use let instead of var in run.js (Daniele Belardi) [#31794](https://github.com/nodejs/node/pull/31794)
* \[[`a05f22647a`](https://github.com/nodejs/node/commit/a05f22647a)] - **benchmark**: use let instead of var in dns (Daniele Belardi) [#31794](https://github.com/nodejs/node/pull/31794)
* \[[`2d7c52d729`](https://github.com/nodejs/node/commit/2d7c52d729)] - **benchmark**: use let instead of var in common.js (Daniele Belardi) [#31794](https://github.com/nodejs/node/pull/31794)
* \[[`d205bc91d4`](https://github.com/nodejs/node/commit/d205bc91d4)] - **benchmark**: use const instead of var in async\_hooks (Daniele Belardi) [#31794](https://github.com/nodejs/node/pull/31794)
* \[[`d7f1add038`](https://github.com/nodejs/node/commit/d7f1add038)] - **benchmark**: add `no-var` rule in .eslintrc.yaml (Daniele Belardi) [#31794](https://github.com/nodejs/node/pull/31794)
* \[[`b4a6351634`](https://github.com/nodejs/node/commit/b4a6351634)] - **benchmark**: remove special test entries (Ruben Bridgewater) [#31755](https://github.com/nodejs/node/pull/31755)
* \[[`71397885b2`](https://github.com/nodejs/node/commit/71397885b2)] - **benchmark**: add `test` and `all` options and improve errors" (Ruben Bridgewater) [#31755](https://github.com/nodejs/node/pull/31755)
* \[[`011e3dec00`](https://github.com/nodejs/node/commit/011e3dec00)] - **benchmark**: refactor helper into a class (Ruben Bridgewater) [#31755](https://github.com/nodejs/node/pull/31755)
* \[[`cf2ca11828`](https://github.com/nodejs/node/commit/cf2ca11828)] - _**Revert**_ "**benchmark**: refactor helper into a class" (Anna Henningsen) [#31722](https://github.com/nodejs/node/pull/31722)
* \[[`ef80c02794`](https://github.com/nodejs/node/commit/ef80c02794)] - _**Revert**_ "**benchmark**: remove special test entries" (Anna Henningsen) [#31722](https://github.com/nodejs/node/pull/31722)
* \[[`3861c69b02`](https://github.com/nodejs/node/commit/3861c69b02)] - **benchmark**: fix error on server close in AsyncLocalStorage benchmark (Andrey Pechkurov) [#32503](https://github.com/nodejs/node/pull/32503)
* \[[`daf6e1702f`](https://github.com/nodejs/node/commit/daf6e1702f)] - **benchmark**: use let instead of var in zlib (Daniele Belardi) [#31794](https://github.com/nodejs/node/pull/31794)
* \[[`6b02359dbf`](https://github.com/nodejs/node/commit/6b02359dbf)] - **test**: update c8 ignore comment (Benjamin Coe) [#33151](https://github.com/nodejs/node/pull/33151)
* \[[`d7b13abbf8`](https://github.com/nodejs/node/commit/d7b13abbf8)] - **test**: skip memory usage tests when ASAN is enabled (Anna Henningsen) [#33129](https://github.com/nodejs/node/pull/33129)
* \[[`238353839c`](https://github.com/nodejs/node/commit/238353839c)] - **test**: move test-process-title to sequential (Anna Henningsen) [#33150](https://github.com/nodejs/node/pull/33150)
* \[[`13cae34484`](https://github.com/nodejs/node/commit/13cae34484)] - **test**: fix out-of-bound reads from invalid sizeof usage (Anna Henningsen) [#33115](https://github.com/nodejs/node/pull/33115)
* \[[`08e01a12d0`](https://github.com/nodejs/node/commit/08e01a12d0)] - **test**: add missing calls to napi\_async\_destroy (Anna Henningsen) [#33114](https://github.com/nodejs/node/pull/33114)
* \[[`3015887019`](https://github.com/nodejs/node/commit/3015887019)] - **test**: check args on SourceTextModule cachedData (Juan José Arboleda) [#32956](https://github.com/nodejs/node/pull/32956)
* \[[`dad82173cd`](https://github.com/nodejs/node/commit/dad82173cd)] - **test**: mark test flaky on freebsd (Sam Roberts) [#32849](https://github.com/nodejs/node/pull/32849)
* \[[`4ab6643abb`](https://github.com/nodejs/node/commit/4ab6643abb)] - **test**: flaky test-stdout-close-catch on freebsd (Sam Roberts) [#32849](https://github.com/nodejs/node/pull/32849)
* \[[`60550f35ac`](https://github.com/nodejs/node/commit/60550f35ac)] - **test**: refactor test-async-hooks-constructor (himself65) [#33063](https://github.com/nodejs/node/pull/33063)
* \[[`83520451cc`](https://github.com/nodejs/node/commit/83520451cc)] - **test**: remove timers-blocking-callback (Jeremiah Senkpiel) [#32870](https://github.com/nodejs/node/pull/32870)
* \[[`579f68c5fd`](https://github.com/nodejs/node/commit/579f68c5fd)] - **test**: better error validations for event-capture (Adrian Estrada) [#32771](https://github.com/nodejs/node/pull/32771)
* \[[`dacd27927a`](https://github.com/nodejs/node/commit/dacd27927a)] - **test**: refactor events tests for invalid listeners (Adrian Estrada) [#32769](https://github.com/nodejs/node/pull/32769)
* \[[`4c67568148`](https://github.com/nodejs/node/commit/4c67568148)] - **test**: test-async-wrap-constructor prefer forEach (Daniel Estiven Rico Posada) [#32631](https://github.com/nodejs/node/pull/32631)
* \[[`0bae243438`](https://github.com/nodejs/node/commit/0bae243438)] - **test**: mark test-child-process-fork-args as flaky on Windows (Andrey Pechkurov) [#32950](https://github.com/nodejs/node/pull/32950)
* \[[`f181b5996a`](https://github.com/nodejs/node/commit/f181b5996a)] - **test**: changed function to arrow function (Nimit) [#32875](https://github.com/nodejs/node/pull/32875)
* \[[`68e3954d1a`](https://github.com/nodejs/node/commit/68e3954d1a)] - **test**: replace console.log/error() with debuglog (daemon1024) [#32692](https://github.com/nodejs/node/pull/32692)
* \[[`c566906789`](https://github.com/nodejs/node/commit/c566906789)] - **test**: only detect uname on supported os (Xu Meng) [#32833](https://github.com/nodejs/node/pull/32833)
* \[[`50130f0e23`](https://github.com/nodejs/node/commit/50130f0e23)] - **test**: mark cpu-prof-dir-worker flaky on all (Sam Roberts) [#32828](https://github.com/nodejs/node/pull/32828)
* \[[`96c93113a8`](https://github.com/nodejs/node/commit/96c93113a8)] - **test**: replace equal with strictEqual (Jesus Hernandez) [#32727](https://github.com/nodejs/node/pull/32727)
* \[[`e839a71ca8`](https://github.com/nodejs/node/commit/e839a71ca8)] - **test**: mark test-worker-prof flaky on arm (Sam Roberts) [#32826](https://github.com/nodejs/node/pull/32826)
* \[[`44ca47904d`](https://github.com/nodejs/node/commit/44ca47904d)] - **test**: mark test-http2-reset-flood flaky on all (Sam Roberts) [#32825](https://github.com/nodejs/node/pull/32825)
* \[[`271b309c91`](https://github.com/nodejs/node/commit/271b309c91)] - **test**: cover node entry type in perf\_hooks (Julian Duque) [#32751](https://github.com/nodejs/node/pull/32751)
* \[[`769ac24eba`](https://github.com/nodejs/node/commit/769ac24eba)] - **test**: use symlinks to copy shells (John Kleinschmidt) [#32129](https://github.com/nodejs/node/pull/32129)
* \[[`b3ac840b97`](https://github.com/nodejs/node/commit/b3ac840b97)] - **test**: save test file in temporary directory (Luigi Pinca) [#32670](https://github.com/nodejs/node/pull/32670)
* \[[`c5e0615942`](https://github.com/nodejs/node/commit/c5e0615942)] - **test**: refactor test-worker (himself65) [#32509](https://github.com/nodejs/node/pull/32509)
* \[[`8eb6807dfe`](https://github.com/nodejs/node/commit/8eb6807dfe)] - **test**: replace flag expose\_internals to expose-internals (Juan José Arboleda) [#32542](https://github.com/nodejs/node/pull/32542)
* \[[`5598dd14df`](https://github.com/nodejs/node/commit/5598dd14df)] - **test**: fix a typo on test-fs-read-optional-params (himself65) [#32461](https://github.com/nodejs/node/pull/32461)
* \[[`30207985cc`](https://github.com/nodejs/node/commit/30207985cc)] - **test**: als variant of test-timers-clearImmediate (Harshitha KP) [#32303](https://github.com/nodejs/node/pull/32303)
* \[[`e3baee6c3d`](https://github.com/nodejs/node/commit/e3baee6c3d)] - **test**: refactoring / cleanup on child-process tests (James M Snell) [#32078](https://github.com/nodejs/node/pull/32078)
* \[[`6a0bc83370`](https://github.com/nodejs/node/commit/6a0bc83370)] - **test**: remove common.skipIfReportDisabled() (Colin Ihrig) [#32242](https://github.com/nodejs/node/pull/32242)
* \[[`4a08b85fc8`](https://github.com/nodejs/node/commit/4a08b85fc8)] - **test**: make test-memory-usage predictable (Matheus Marchini) [#32239](https://github.com/nodejs/node/pull/32239)
* \[[`efc844d00d`](https://github.com/nodejs/node/commit/efc844d00d)] - **test**: verify that WASI errors are rethrown (Colin Ihrig) [#32157](https://github.com/nodejs/node/pull/32157)
* \[[`10ee89a8d5`](https://github.com/nodejs/node/commit/10ee89a8d5)] - **test**: refactor and simplify test-repl-preview (Ruben Bridgewater) [#32154](https://github.com/nodejs/node/pull/32154)
* \[[`5a8e54b6de`](https://github.com/nodejs/node/commit/5a8e54b6de)] - **test**: refactor all benchmark tests to use the new test option (Ruben Bridgewater) [#31755](https://github.com/nodejs/node/pull/31755)
* \[[`d1d22fa86e`](https://github.com/nodejs/node/commit/d1d22fa86e)] - **test**: add secp224k1 check in crypto-dh-stateless (Daniel Bevenius) [#31715](https://github.com/nodejs/node/pull/31715)
* \[[`8a044cb9ae`](https://github.com/nodejs/node/commit/8a044cb9ae)] - **test**: fix flaky parallel/test-repl-history-navigation test (Ruben Bridgewater) [#31708](https://github.com/nodejs/node/pull/31708)
* \[[`2fc72cac97`](https://github.com/nodejs/node/commit/2fc72cac97)] - **test**: fix flaky test-trace-sigint-on-idle (Anna Henningsen) [#31645](https://github.com/nodejs/node/pull/31645)
* \[[`a4ee930d71`](https://github.com/nodejs/node/commit/a4ee930d71)] - **test**: improve logged errors (Ruben Bridgewater) [#31425](https://github.com/nodejs/node/pull/31425)
* \[[`4aaf4075e9`](https://github.com/nodejs/node/commit/4aaf4075e9)] - **test**: show child stderr output in largepages test (Ben Noordhuis) [#31612](https://github.com/nodejs/node/pull/31612)
* \[[`2508e1321f`](https://github.com/nodejs/node/commit/2508e1321f)] - **test**: add new scenario for async-local storage (Harshitha KP) [#32082](https://github.com/nodejs/node/pull/32082)
* \[[`52a11544cf`](https://github.com/nodejs/node/commit/52a11544cf)] - **test**: add GC test for disabled AsyncLocalStorage (Andrey Pechkurov) [#31995](https://github.com/nodejs/node/pull/31995)
* \[[`98ece74dc7`](https://github.com/nodejs/node/commit/98ece74dc7)] - **test**: improve disable AsyncLocalStorage test (Andrey Pechkurov) [#31998](https://github.com/nodejs/node/pull/31998)
* \[[`e5a64e5def`](https://github.com/nodejs/node/commit/e5a64e5def)] - **test**: fix flaky test-memory-usage (Anna Henningsen) [#31602](https://github.com/nodejs/node/pull/31602)
* \[[`02ec03ce27`](https://github.com/nodejs/node/commit/02ec03ce27)] - **test**: cover property n-api null cases (Gabriel Schulhof) [#31488](https://github.com/nodejs/node/pull/31488)
* \[[`733002b081`](https://github.com/nodejs/node/commit/733002b081)] - **test**: skip keygen tests on arm systems (Tobias Nießen) [#31178](https://github.com/nodejs/node/pull/31178)
* \[[`5e5d053585`](https://github.com/nodejs/node/commit/5e5d053585)] - **test**: add repl tests to verify unicode support in previews (Ruben Bridgewater) [#31112](https://github.com/nodejs/node/pull/31112)
* \[[`f1624bbafa`](https://github.com/nodejs/node/commit/f1624bbafa)] - **test**: add multiple repl preview tests (Ruben Bridgewater) [#30907](https://github.com/nodejs/node/pull/30907)
* \[[`9dcf137623`](https://github.com/nodejs/node/commit/9dcf137623)] - **test,benchmark**: fix test-benchmark-zlib (Rich Trott) [#31538](https://github.com/nodejs/node/pull/31538)
* \[[`94e4847142`](https://github.com/nodejs/node/commit/94e4847142)] - **tools**: bump remark-preset-lint-node to 1.15.0 (Rich Trott) [#33157](https://github.com/nodejs/node/pull/33157)
* \[[`58bd92aa26`](https://github.com/nodejs/node/commit/58bd92aa26)] - **tools**: update remark-preset-lint-node\@1.14.0 (Rich Trott) [#33072](https://github.com/nodejs/node/pull/33072)
* \[[`b9d9c24cfc`](https://github.com/nodejs/node/commit/b9d9c24cfc)] - **tools**: update broken types in type parser (Colin Ihrig) [#33068](https://github.com/nodejs/node/pull/33068)
* \[[`3dafc1460d`](https://github.com/nodejs/node/commit/3dafc1460d)] - **tools**: fix mkcodecache when run with ASAN (Anna Henningsen) [#32850](https://github.com/nodejs/node/pull/32850)
* \[[`1c010b41a1`](https://github.com/nodejs/node/commit/1c010b41a1)] - **tools**: update ESLint to 7.0.0-rc.0 (himself65) [#33062](https://github.com/nodejs/node/pull/33062)
* \[[`5f79ab2239`](https://github.com/nodejs/node/commit/5f79ab2239)] - **tools**: remove unused code in doc generation tool (Rich Trott) [#32913](https://github.com/nodejs/node/pull/32913)
* \[[`576a62688f`](https://github.com/nodejs/node/commit/576a62688f)] - **tools**: decrease timeout in test.py (Anna Henningsen) [#32868](https://github.com/nodejs/node/pull/32868)
* \[[`9cf9cb436b`](https://github.com/nodejs/node/commit/9cf9cb436b)] - **tools**: remove prefer-common-expectserror lint rule (Colin Ihrig) [#31147](https://github.com/nodejs/node/pull/31147)

<a id="12.16.3"></a>

## 2020-04-28, Version 12.16.3 'Erbium' (LTS), @targos

### Notable Changes

* **Dependencies**:
  * Updated OpenSSL to 1.1.1g (Hassaan Pasha) [#32971](https://github.com/nodejs/node/pull/32971).
  * Updated c-ares to 1.16.0 (Anna Henningsen) [#32246](https://github.com/nodejs/node/pull/32246).
  * Updated experimental uvwasi to 0.0.6 (Colin Ihrig) [#32309](https://github.com/nodejs/node/pull/32309).
* **ESM (experimental)**:
  * Additional warnings are no longer printed for modules that use conditional
    exports or package name self resolution (Guy Bedford) [#31845](https://github.com/nodejs/node/pull/31845).

### Commits

* \[[`2c5b0147fa`](https://github.com/nodejs/node/commit/2c5b0147fa)] - **async\_hooks**: use hasHooks function internally (rickyes) [#32656](https://github.com/nodejs/node/pull/32656)
* \[[`28abbfd594`](https://github.com/nodejs/node/commit/28abbfd594)] - **async\_hooks**: move to lazy destroy hook registration in AsyncResource (Andrey Pechkurov) [#32429](https://github.com/nodejs/node/pull/32429)
* \[[`146ad4eaae`](https://github.com/nodejs/node/commit/146ad4eaae)] - **async\_hooks**: avoid resource reuse by FileHandle (Gerhard Stoebich) [#31972](https://github.com/nodejs/node/pull/31972)
* \[[`39a3cc13dc`](https://github.com/nodejs/node/commit/39a3cc13dc)] - **buffer,n-api**: fix double ArrayBuffer::Detach() during cleanup (Anna Henningsen) [#33039](https://github.com/nodejs/node/pull/33039)
* \[[`20f3e9d836`](https://github.com/nodejs/node/commit/20f3e9d836)] - **build**: output dots instead of tap in GitHub actions (Michaël Zasso) [#32714](https://github.com/nodejs/node/pull/32714)
* \[[`c98aa9312e`](https://github.com/nodejs/node/commit/c98aa9312e)] - **build**: move doc versions JSON file out of out/doc (Richard Lau) [#32728](https://github.com/nodejs/node/pull/32728)
* \[[`546a9ea998`](https://github.com/nodejs/node/commit/546a9ea998)] - **build**: fix LINT\_MD\_NEWER assignment (Rich Trott) [#32712](https://github.com/nodejs/node/pull/32712)
* \[[`ae772b7c6a`](https://github.com/nodejs/node/commit/ae772b7c6a)] - **build**: log detected compilers in --verbose mode (Richard Lau) [#32715](https://github.com/nodejs/node/pull/32715)
* \[[`43055519d3`](https://github.com/nodejs/node/commit/43055519d3)] - **build**: use tabs for indentation in Makefile (Luigi Pinca) [#32614](https://github.com/nodejs/node/pull/32614)
* \[[`2e31ac96f3`](https://github.com/nodejs/node/commit/2e31ac96f3)] - **build**: remove make lint on lint-py (himself65) [#32599](https://github.com/nodejs/node/pull/32599)
* \[[`d8a948f0fc`](https://github.com/nodejs/node/commit/d8a948f0fc)] - **build**: disable -Wattributes warnings on aix (Ben Noordhuis) [#32419](https://github.com/nodejs/node/pull/32419)
* \[[`a3848e51aa`](https://github.com/nodejs/node/commit/a3848e51aa)] - **build**: expand ASAN acronym in configure help (Sam Roberts) [#32325](https://github.com/nodejs/node/pull/32325)
* \[[`c8541a7d7a`](https://github.com/nodejs/node/commit/c8541a7d7a)] - **build**: disable libstdc++ debug containers globally (Ben Noordhuis) [#30147](https://github.com/nodejs/node/pull/30147)
* \[[`d3c9a82a6e`](https://github.com/nodejs/node/commit/d3c9a82a6e)] - **build**: remove empty line on node.gyp file (Juan José Arboleda) [#31952](https://github.com/nodejs/node/pull/31952)
* \[[`e65586985f`](https://github.com/nodejs/node/commit/e65586985f)] - **build**: support android build on ndk version equal or above 23 (forfun414) [#31521](https://github.com/nodejs/node/pull/31521)
* \[[`790841597d`](https://github.com/nodejs/node/commit/790841597d)] - **console**: fixup error message (James M Snell) [#32475](https://github.com/nodejs/node/pull/32475)
* \[[`d19251630e`](https://github.com/nodejs/node/commit/d19251630e)] - **crypto**: clear openssl error stack after en/decrypt (Ben Noordhuis) [#32248](https://github.com/nodejs/node/pull/32248)
* \[[`51f05d2f3d`](https://github.com/nodejs/node/commit/51f05d2f3d)] - **deps**: update archs files for OpenSSL-1.1.1g (Hassaan Pasha) [#32971](https://github.com/nodejs/node/pull/32971)
* \[[`a89744f4e0`](https://github.com/nodejs/node/commit/a89744f4e0)] - **deps**: upgrade openssl sources to 1.1.1g (Hassaan Pasha) [#32971](https://github.com/nodejs/node/pull/32971)
* \[[`80c89d4ec7`](https://github.com/nodejs/node/commit/80c89d4ec7)] - **deps**: update archs files for OpenSSL-1.1.1f (Hassaan Pasha) [#32583](https://github.com/nodejs/node/pull/32583)
* \[[`c9cc38114a`](https://github.com/nodejs/node/commit/c9cc38114a)] - **deps**: upgrade openssl sources to 1.1.1f (Hassaan Pasha) [#32583](https://github.com/nodejs/node/pull/32583)
* \[[`fedcb16144`](https://github.com/nodejs/node/commit/fedcb16144)] - **deps**: update acorn to v7.1.1 (Ruben Bridgewater) [#32310](https://github.com/nodejs/node/pull/32310)
* \[[`37476a339a`](https://github.com/nodejs/node/commit/37476a339a)] - **deps**: upgrade to c-ares v1.16.0 (Anna Henningsen) [#32246](https://github.com/nodejs/node/pull/32246)
* \[[`fe0e1dbd13`](https://github.com/nodejs/node/commit/fe0e1dbd13)] - **deps**: update to uvwasi 0.0.6 (Colin Ihrig) [#32309](https://github.com/nodejs/node/pull/32309)
* \[[`2e92cb476d`](https://github.com/nodejs/node/commit/2e92cb476d)] - **deps**: V8: cherry-pick f9257802c1c0 (Matheus Marchini) [#32180](https://github.com/nodejs/node/pull/32180)
* \[[`0e922440d6`](https://github.com/nodejs/node/commit/0e922440d6)] - **deps,doc**: move openssl maintenance guide to doc (Sam Roberts) [#32209](https://github.com/nodejs/node/pull/32209)
* \[[`06d16cf9ef`](https://github.com/nodejs/node/commit/06d16cf9ef)] - **dns**: remove duplicate code (rickyes) [#32664](https://github.com/nodejs/node/pull/32664)
* \[[`af392a114b`](https://github.com/nodejs/node/commit/af392a114b)] - **doc**: add link to code ide configs (Robert Nagy) [#32767](https://github.com/nodejs/node/pull/32767)
* \[[`b1790fbf4b`](https://github.com/nodejs/node/commit/b1790fbf4b)] - **doc**: replace node-test-pull-request-lite-pipeline from onboarding (Juan José Arboleda) [#32736](https://github.com/nodejs/node/pull/32736)
* \[[`00ce6a3240`](https://github.com/nodejs/node/commit/00ce6a3240)] - **doc**: add useful v8 option section (Nimit) [#32262](https://github.com/nodejs/node/pull/32262)
* \[[`c78019d792`](https://github.com/nodejs/node/commit/c78019d792)] - **doc**: add himself65 to collaborators (himself65) [#32734](https://github.com/nodejs/node/pull/32734)
* \[[`16126328ac`](https://github.com/nodejs/node/commit/16126328ac)] - **doc**: clarify behavior of napi\_get\_typedarray\_info (Michael Dawson) [#32603](https://github.com/nodejs/node/pull/32603)
* \[[`a5fd29b024`](https://github.com/nodejs/node/commit/a5fd29b024)] - **doc**: remove optional parameter from markdown anchor link (Rich Trott) [#32671](https://github.com/nodejs/node/pull/32671)
* \[[`d2c86a9dfc`](https://github.com/nodejs/node/commit/d2c86a9dfc)] - **doc**: clarify `listening` event (Harshitha KP) [#32581](https://github.com/nodejs/node/pull/32581)
* \[[`9039c03967`](https://github.com/nodejs/node/commit/9039c03967)] - **doc**: update Ninja information in build guide (Adrian Estrada) [#32629](https://github.com/nodejs/node/pull/32629)
* \[[`1d563a646e`](https://github.com/nodejs/node/commit/1d563a646e)] - **doc**: correct version metadata for Readable.from (Dave Vandyke) [#32639](https://github.com/nodejs/node/pull/32639)
* \[[`5e2791ee84`](https://github.com/nodejs/node/commit/5e2791ee84)] - **doc**: adjust paths in openssl maintenance guide (Hassaan Pasha) [#32593](https://github.com/nodejs/node/pull/32593)
* \[[`21c3685623`](https://github.com/nodejs/node/commit/21c3685623)] - **doc**: clarify docs fs.watch exception may be emitted (Juan José Arboleda) [#32513](https://github.com/nodejs/node/pull/32513)
* \[[`c3d91eb94d`](https://github.com/nodejs/node/commit/c3d91eb94d)] - **doc**: add unreachable code on events example (himself65) [#32364](https://github.com/nodejs/node/pull/32364)
* \[[`b4ba9b8bef`](https://github.com/nodejs/node/commit/b4ba9b8bef)] - **doc**: clarify `length` param in `buffer.write` (Harshitha KP) [#32119](https://github.com/nodejs/node/pull/32119)
* \[[`5996df3c39`](https://github.com/nodejs/node/commit/5996df3c39)] - **doc**: document that server.address() can return null (Thomas Watson Steen) [#32519](https://github.com/nodejs/node/pull/32519)
* \[[`a299e9cf28`](https://github.com/nodejs/node/commit/a299e9cf28)] - **doc**: return type of `crypto.getFips()` may change (Richard Lau) [#32580](https://github.com/nodejs/node/pull/32580)
* \[[`4604127697`](https://github.com/nodejs/node/commit/4604127697)] - **doc**: fix return type of `crypto.getFips()` (Richard Lau) [#32580](https://github.com/nodejs/node/pull/32580)
* \[[`f2235f68aa`](https://github.com/nodejs/node/commit/f2235f68aa)] - **doc**: clarify `requireManualDestroy` option (Harshitha KP) [#32514](https://github.com/nodejs/node/pull/32514)
* \[[`7e952f2d38`](https://github.com/nodejs/node/commit/7e952f2d38)] - **doc**: fix wordy sentence (Moni) [#32567](https://github.com/nodejs/node/pull/32567)
* \[[`f93b770bda`](https://github.com/nodejs/node/commit/f93b770bda)] - **doc**: fix more links (Alba Mendez) [#32586](https://github.com/nodejs/node/pull/32586)
* \[[`d764414706`](https://github.com/nodejs/node/commit/d764414706)] - **doc**: improve markdown link checker (Alba Mendez) [#32586](https://github.com/nodejs/node/pull/32586)
* \[[`3d36458cc6`](https://github.com/nodejs/node/commit/3d36458cc6)] - **doc**: add flarna to collaborators (Gerhard Stoebich) [#32620](https://github.com/nodejs/node/pull/32620)
* \[[`4b417f87bd`](https://github.com/nodejs/node/commit/4b417f87bd)] - **doc**: improve fs.read documentation (Hachimi Aa (Sfeir)) [#29270](https://github.com/nodejs/node/pull/29270)
* \[[`959055f225`](https://github.com/nodejs/node/commit/959055f225)] - **doc**: add ASAN build instructions (gengjiawen) [#32436](https://github.com/nodejs/node/pull/32436)
* \[[`f1552f830f`](https://github.com/nodejs/node/commit/f1552f830f)] - **doc**: update context-aware section of addon doc (Gabriel Schulhof) [#28659](https://github.com/nodejs/node/pull/28659)
* \[[`d0d414d98c`](https://github.com/nodejs/node/commit/d0d414d98c)] - **doc**: update AUTHORS list (Luigi Pinca) [#32222](https://github.com/nodejs/node/pull/32222)
* \[[`e51c42dc52`](https://github.com/nodejs/node/commit/e51c42dc52)] - **doc**: tests local links in markdown documents (Antoine du HAMEL) [#32359](https://github.com/nodejs/node/pull/32359)
* \[[`8b355eab57`](https://github.com/nodejs/node/commit/8b355eab57)] - **doc**: fix profile type of --heap-prof-name (Syohei YOSHIDA) [#32404](https://github.com/nodejs/node/pull/32404)
* \[[`59a8dbebc2`](https://github.com/nodejs/node/commit/59a8dbebc2)] - **doc**: use uppercase on windows path (himself65) [#32294](https://github.com/nodejs/node/pull/32294)
* \[[`fa9b10cebe`](https://github.com/nodejs/node/commit/fa9b10cebe)] - **doc**: rename cve\_management\_process.md to fit doc style guide (Ling Samuel) [#32456](https://github.com/nodejs/node/pull/32456)
* \[[`3ed9fcd784`](https://github.com/nodejs/node/commit/3ed9fcd784)] - **doc**: add mildsunrise to collaborators (Alba Mendez) [#32525](https://github.com/nodejs/node/pull/32525)
* \[[`5d15dd3fe3`](https://github.com/nodejs/node/commit/5d15dd3fe3)] - **doc**: add link to DNS definition (unknown) [#32228](https://github.com/nodejs/node/pull/32228)
* \[[`8d27eb94d1`](https://github.com/nodejs/node/commit/8d27eb94d1)] - **doc**: remove extraneous sentence in events.md (Rich Trott) [#32457](https://github.com/nodejs/node/pull/32457)
* \[[`1c84d85437`](https://github.com/nodejs/node/commit/1c84d85437)] - **doc**: trim wording in n-api.md text about exceptions (Rich Trott) [#32457](https://github.com/nodejs/node/pull/32457)
* \[[`bba8dd3344`](https://github.com/nodejs/node/commit/bba8dd3344)] - **doc**: simplify and correct example descriptions in net.md (Rich Trott) [#32451](https://github.com/nodejs/node/pull/32451)
* \[[`2976ac6c2e`](https://github.com/nodejs/node/commit/2976ac6c2e)] - **doc**: add new TSC members (Michael Dawson) [#32473](https://github.com/nodejs/node/pull/32473)
* \[[`3d752cd3b9`](https://github.com/nodejs/node/commit/3d752cd3b9)] - **doc**: improve wording in vm.md (Rich Trott) [#32427](https://github.com/nodejs/node/pull/32427)
* \[[`80a8e20826`](https://github.com/nodejs/node/commit/80a8e20826)] - **doc**: update security release process (Sam Roberts) [#31679](https://github.com/nodejs/node/pull/31679)
* \[[`80493f54c8`](https://github.com/nodejs/node/commit/80493f54c8)] - **doc**: fix some 404 links (Thomas Watson Steen) [#32200](https://github.com/nodejs/node/pull/32200)
* \[[`76e2455b06`](https://github.com/nodejs/node/commit/76e2455b06)] - **doc**: expand fs.watch caveats (Bartosz Sosnowski) [#32176](https://github.com/nodejs/node/pull/32176)
* \[[`c1c3aa1b5f`](https://github.com/nodejs/node/commit/c1c3aa1b5f)] - **doc**: add Ruben to TSC (Michael Dawson) [#32213](https://github.com/nodejs/node/pull/32213)
* \[[`385faf7721`](https://github.com/nodejs/node/commit/385faf7721)] - **doc**: include the error type in the request.resolve doc (Joe Pea) [#32152](https://github.com/nodejs/node/pull/32152)
* \[[`11899f647a`](https://github.com/nodejs/node/commit/11899f647a)] - **doc**: clear up child\_process command resolution (Denys Otrishko) [#32091](https://github.com/nodejs/node/pull/32091)
* \[[`e33e989f20`](https://github.com/nodejs/node/commit/e33e989f20)] - **doc**: clarify windows specific behaviour (Sam Roberts) [#32079](https://github.com/nodejs/node/pull/32079)
* \[[`860239255b`](https://github.com/nodejs/node/commit/860239255b)] - **doc**: improve Buffer documentation (Anna Henningsen) [#32086](https://github.com/nodejs/node/pull/32086)
* \[[`ab1136a7ed`](https://github.com/nodejs/node/commit/ab1136a7ed)] - **doc**: add support encoding link on string\_decoder.md (himself65) [#31911](https://github.com/nodejs/node/pull/31911)
* \[[`c439d83dbf`](https://github.com/nodejs/node/commit/c439d83dbf)] - **doc**: add entry for `AsyncHook` class (Harshitha KP) [#31865](https://github.com/nodejs/node/pull/31865)
* \[[`e6e38ecf64`](https://github.com/nodejs/node/commit/e6e38ecf64)] - **doc**: prevent tables from shrinking page (David Gilbertson) [#31859](https://github.com/nodejs/node/pull/31859)
* \[[`6e68d9816d`](https://github.com/nodejs/node/commit/6e68d9816d)] - **doc**: fix anchor for ERR\_TLS\_INVALID\_CONTEXT (Tobias Nießen) [#31915](https://github.com/nodejs/node/pull/31915)
* \[[`d3b9a8810c`](https://github.com/nodejs/node/commit/d3b9a8810c)] - **doc,crypto**: clarify oaepHash option's impact (Filip Skokan) [#32340](https://github.com/nodejs/node/pull/32340)
* \[[`b85bc0cc02`](https://github.com/nodejs/node/commit/b85bc0cc02)] - **fs**: fixup error message for invalid options.recursive (James M Snell) [#32472](https://github.com/nodejs/node/pull/32472)
* \[[`010814856a`](https://github.com/nodejs/node/commit/010814856a)] - **fs**: fix writeFile\[Sync] for non-seekable files (Alba Mendez) [#32006](https://github.com/nodejs/node/pull/32006)
* \[[`225ddd5f42`](https://github.com/nodejs/node/commit/225ddd5f42)] - **http**: move free socket error handling to agent (Robert Nagy) [#32003](https://github.com/nodejs/node/pull/32003)
* \[[`3b0204245d`](https://github.com/nodejs/node/commit/3b0204245d)] - **http**: don't emit 'readable' after 'close' (Robert Nagy) [#32277](https://github.com/nodejs/node/pull/32277)
* \[[`52a52d2664`](https://github.com/nodejs/node/commit/52a52d2664)] - **http**: fixup options.method error message (James M Snell) [#32471](https://github.com/nodejs/node/pull/32471)
* \[[`cf47bb9818`](https://github.com/nodejs/node/commit/cf47bb9818)] - **http**: don't emit 'finish' after 'error' (Robert Nagy) [#32276](https://github.com/nodejs/node/pull/32276)
* \[[`f9123eb91b`](https://github.com/nodejs/node/commit/f9123eb91b)] - **http**: fix socket re-use races (Robert Nagy) [#32000](https://github.com/nodejs/node/pull/32000)
* \[[`e54eb46cdb`](https://github.com/nodejs/node/commit/e54eb46cdb)] - **http2**: rename counter in `mapToHeaders` inner loop (Mateusz Krawczuk) [#32012](https://github.com/nodejs/node/pull/32012)
* \[[`0db58753db`](https://github.com/nodejs/node/commit/0db58753db)] - **lib**: fix return type of setTimeout in net.Socket (龙腾道) [#32722](https://github.com/nodejs/node/pull/32722)
* \[[`a152792590`](https://github.com/nodejs/node/commit/a152792590)] - **lib**: removes unnecessary params (Jesus Hernandez) [#32694](https://github.com/nodejs/node/pull/32694)
* \[[`7dd001c1db`](https://github.com/nodejs/node/commit/7dd001c1db)] - **lib**: changed functional logic in cluster schedulers (Yash Ladha) [#32505](https://github.com/nodejs/node/pull/32505)
* \[[`5a671772a2`](https://github.com/nodejs/node/commit/5a671772a2)] - **lib**: use spread operator on cluster (himself65) [#32125](https://github.com/nodejs/node/pull/32125)
* \[[`4d0be3dce5`](https://github.com/nodejs/node/commit/4d0be3dce5)] - **meta**: move inactive collaborators to emeriti (Rich Trott) [#32151](https://github.com/nodejs/node/pull/32151)
* \[[`ecddf6519f`](https://github.com/nodejs/node/commit/ecddf6519f)] - **module**: disable conditional exports, self resolve warnings (Guy Bedford) [#31845](https://github.com/nodejs/node/pull/31845)
* \[[`717f9c5905`](https://github.com/nodejs/node/commit/717f9c5905)] - **module**: path-only CJS exports extension searching (Guy Bedford) [#32351](https://github.com/nodejs/node/pull/32351)
* \[[`ff5ab6f925`](https://github.com/nodejs/node/commit/ff5ab6f925)] - **net**: fix crash if POLLHUP is received (Santiago Gimeno) [#32590](https://github.com/nodejs/node/pull/32590)
* \[[`ed21d32a7c`](https://github.com/nodejs/node/commit/ed21d32a7c)] - **net**: wait for shutdown to complete before closing (Robert Nagy) [#32491](https://github.com/nodejs/node/pull/32491)
* \[[`7d66ceadbb`](https://github.com/nodejs/node/commit/7d66ceadbb)] - **perf,src**: add HistogramBase and internal/histogram.js (James M Snell) [#31988](https://github.com/nodejs/node/pull/31988)
* \[[`f302ac9ae4`](https://github.com/nodejs/node/commit/f302ac9ae4)] - **perf\_hooks**: allow omitted parameters in 'performance.measure' (himself65) [#32651](https://github.com/nodejs/node/pull/32651)
* \[[`7c0c4e9a7e`](https://github.com/nodejs/node/commit/7c0c4e9a7e)] - **repl**: fixup error message (James M Snell) [#32474](https://github.com/nodejs/node/pull/32474)
* \[[`522101dbca`](https://github.com/nodejs/node/commit/522101dbca)] - **src**: removes unused v8::Integer and v8::Array namespace (Jesus Hernandez) [#32779](https://github.com/nodejs/node/pull/32779)
* \[[`f9d94143fb`](https://github.com/nodejs/node/commit/f9d94143fb)] - **src**: remove unused v8::TryCatch namespace (Juan José Arboleda) [#32729](https://github.com/nodejs/node/pull/32729)
* \[[`d0d7ebc2a6`](https://github.com/nodejs/node/commit/d0d7ebc2a6)] - **src**: remove duplicated code (himself65) [#32719](https://github.com/nodejs/node/pull/32719)
* \[[`a50220955e`](https://github.com/nodejs/node/commit/a50220955e)] - **src**: refactor to avoid goto in node\_file.cc (Tobias Nießen) [#32637](https://github.com/nodejs/node/pull/32637)
* \[[`fabb53ed79`](https://github.com/nodejs/node/commit/fabb53ed79)] - **src**: fix warnings on SPrintF (himself65) [#32558](https://github.com/nodejs/node/pull/32558)
* \[[`3605a9d67a`](https://github.com/nodejs/node/commit/3605a9d67a)] - **src**: replace goto with lambda in options parser (Tobias Nießen) [#32635](https://github.com/nodejs/node/pull/32635)
* \[[`872f893e0f`](https://github.com/nodejs/node/commit/872f893e0f)] - **src**: align PerformanceState class name with conventions (Anna Henningsen) [#32539](https://github.com/nodejs/node/pull/32539)
* \[[`191cde0e4d`](https://github.com/nodejs/node/commit/191cde0e4d)] - **src**: remove unnecessary 'Local.As' operation (himself65) [#32286](https://github.com/nodejs/node/pull/32286)
* \[[`6d71eb5b5b`](https://github.com/nodejs/node/commit/6d71eb5b5b)] - **src**: add test/abort build tasks (Christian Niederer) [#31740](https://github.com/nodejs/node/pull/31740)
* \[[`0dfb9514de`](https://github.com/nodejs/node/commit/0dfb9514de)] - **src**: add aliased-buffer-overflow abort test (Christian Niederer) [#31740](https://github.com/nodejs/node/pull/31740)
* \[[`28cfaa837e`](https://github.com/nodejs/node/commit/28cfaa837e)] - **src**: check for overflow when extending AliasedBufferBase (Christian Niederer) [#31740](https://github.com/nodejs/node/pull/31740)
* \[[`4155358031`](https://github.com/nodejs/node/commit/4155358031)] - **src**: replace handle dereference with ContainerOf (Harshitha KP) [#32298](https://github.com/nodejs/node/pull/32298)
* \[[`c9b22c8d6d`](https://github.com/nodejs/node/commit/c9b22c8d6d)] - **src**: enhance template function 'MakeUtf8String' (himself65) [#32322](https://github.com/nodejs/node/pull/32322)
* \[[`ad347f4cbb`](https://github.com/nodejs/node/commit/ad347f4cbb)] - **src**: remove excess v8 namespace (himself65) [#32191](https://github.com/nodejs/node/pull/32191)
* \[[`12d83b3242`](https://github.com/nodejs/node/commit/12d83b3242)] - **src**: clean v8 namespaces in env.cc file (Juan José Arboleda) [#32374](https://github.com/nodejs/node/pull/32374)
* \[[`13a7e0546f`](https://github.com/nodejs/node/commit/13a7e0546f)] - **src**: check for empty maybe local (Xavier Stouder) [#32339](https://github.com/nodejs/node/pull/32339)
* \[[`aaf94fd6bb`](https://github.com/nodejs/node/commit/aaf94fd6bb)] - **src**: cleanup DestroyParam when Environment exits (Anna Henningsen) [#32421](https://github.com/nodejs/node/pull/32421)
* \[[`4b5fd24855`](https://github.com/nodejs/node/commit/4b5fd24855)] - **src**: enhance C++ sprintf utility (himself65) [#32385](https://github.com/nodejs/node/pull/32385)
* \[[`46e68bb445`](https://github.com/nodejs/node/commit/46e68bb445)] - **src**: simplify IsolateData shortcut accesses (Anna Henningsen) [#32407](https://github.com/nodejs/node/pull/32407)
* \[[`7aa2ee2bd8`](https://github.com/nodejs/node/commit/7aa2ee2bd8)] - **src**: delete CallbackInfo when cleared from cleanup hook (Anna Henningsen) [#32405](https://github.com/nodejs/node/pull/32405)
* \[[`7a346f63d6`](https://github.com/nodejs/node/commit/7a346f63d6)] - **src**: update comment for SetImmediate() (Anna Henningsen) [#32300](https://github.com/nodejs/node/pull/32300)
* \[[`46c751e7f1`](https://github.com/nodejs/node/commit/46c751e7f1)] - **src**: handle NULL env scenario (himself65) [#32230](https://github.com/nodejs/node/pull/32230)
* \[[`9b6f678751`](https://github.com/nodejs/node/commit/9b6f678751)] - **src**: fix warn\_unused\_result compiler warning (Colin Ihrig) [#32241](https://github.com/nodejs/node/pull/32241)
* \[[`4e268314b5`](https://github.com/nodejs/node/commit/4e268314b5)] - **src**: refactor to more safe method (gengjiawen) [#32087](https://github.com/nodejs/node/pull/32087)
* \[[`f223d2c7e4`](https://github.com/nodejs/node/commit/f223d2c7e4)] - **src**: fix spawnSync CHECK when SIGKILL fails (Ben Noordhuis) [#31768](https://github.com/nodejs/node/pull/31768)
* \[[`5b2f698b32`](https://github.com/nodejs/node/commit/5b2f698b32)] - **src**: fix missing extra ca in tls.rootCertificates (Eric Bickle) [#32075](https://github.com/nodejs/node/pull/32075)
* \[[`a53980d947`](https://github.com/nodejs/node/commit/a53980d947)] - **src**: fix -Wmaybe-uninitialized compiler warning (Ben Noordhuis) [#31809](https://github.com/nodejs/node/pull/31809)
* \[[`a2d961da23`](https://github.com/nodejs/node/commit/a2d961da23)] - **src**: remove unused include from node\_file.cc (Ben Noordhuis) [#31809](https://github.com/nodejs/node/pull/31809)
* \[[`8fe70e88fe`](https://github.com/nodejs/node/commit/8fe70e88fe)] - **src**: elevate v8 namespace (RamanandPatil) [#32041](https://github.com/nodejs/node/pull/32041)
* \[[`7e5e34d01e`](https://github.com/nodejs/node/commit/7e5e34d01e)] - **src**: simplify node\_worker.cc using new KVStore API (Denys Otrishko) [#31773](https://github.com/nodejs/node/pull/31773)
* \[[`7152fe3180`](https://github.com/nodejs/node/commit/7152fe3180)] - **src**: improve KVStore API (Denys Otrishko) [#31773](https://github.com/nodejs/node/pull/31773)
* \[[`3bf21b096e`](https://github.com/nodejs/node/commit/3bf21b096e)] - **src**: fix minor typo in base\_object.h (Daniel Bevenius) [#31535](https://github.com/nodejs/node/pull/31535)
* \[[`8d1eeb1ae5`](https://github.com/nodejs/node/commit/8d1eeb1ae5)] - **stream**: combine properties using defineProperties (antsmartian) [#31187](https://github.com/nodejs/node/pull/31187)
* \[[`d07dd313ae`](https://github.com/nodejs/node/commit/d07dd313ae)] - **stream**: add regression test for async iteration completion (Matteo Collina) [#31508](https://github.com/nodejs/node/pull/31508)
* \[[`2f72054ec7`](https://github.com/nodejs/node/commit/2f72054ec7)] - **test**: replace console.log/error with debuglog (Agustin Daguerre) [#32695](https://github.com/nodejs/node/pull/32695)
* \[[`bc9453a870`](https://github.com/nodejs/node/commit/bc9453a870)] - **test**: make sure that inspector tests finish (Anna Henningsen) [#32673](https://github.com/nodejs/node/pull/32673)
* \[[`2cf7381a87`](https://github.com/nodejs/node/commit/2cf7381a87)] - **test**: fix check error name on error instance (himself65) [#32508](https://github.com/nodejs/node/pull/32508)
* \[[`e4174165f3`](https://github.com/nodejs/node/commit/e4174165f3)] - _**Revert**_ "**test**: mark empty udp tests flaky on OS X" (Luigi Pinca) [#32489](https://github.com/nodejs/node/pull/32489)
* \[[`6feed98f33`](https://github.com/nodejs/node/commit/6feed98f33)] - **test**: remove unused variables on async hook test (Julian Duque) [#32630](https://github.com/nodejs/node/pull/32630)
* \[[`b0386b4aaf`](https://github.com/nodejs/node/commit/b0386b4aaf)] - **test**: check that --expose-internals is disallowed in NODE\_OPTIONS (Juan José Arboleda) [#32554](https://github.com/nodejs/node/pull/32554)
* \[[`0adc867d59`](https://github.com/nodejs/node/commit/0adc867d59)] - **test**: add Worker initialization failure test case (Harshitha KP) [#31929](https://github.com/nodejs/node/pull/31929)
* \[[`73221278d7`](https://github.com/nodejs/node/commit/73221278d7)] - **test**: fix tool path in test-doctool-versions.js (Richard Lau) [#32645](https://github.com/nodejs/node/pull/32645)
* \[[`90a5b9d964`](https://github.com/nodejs/node/commit/90a5b9d964)] - **test**: copy addons .gitignore to test/abort/ (Anna Henningsen) [#32624](https://github.com/nodejs/node/pull/32624)
* \[[`39be571a3f`](https://github.com/nodejs/node/commit/39be571a3f)] - **test**: refactor test-http2-buffersize (Rich Trott) [#32540](https://github.com/nodejs/node/pull/32540)
* \[[`f71007ff39`](https://github.com/nodejs/node/commit/f71007ff39)] - **test**: skip crypto test on arm buildbots (Ben Noordhuis) [#32636](https://github.com/nodejs/node/pull/32636)
* \[[`4e405ee899`](https://github.com/nodejs/node/commit/4e405ee899)] - **test**: replace console.error() with debuglog calls (Rich Trott) [#32588](https://github.com/nodejs/node/pull/32588)
* \[[`8083d452e6`](https://github.com/nodejs/node/commit/8083d452e6)] - **test**: add a missing common.mustCall (Harshitha KP) [#32305](https://github.com/nodejs/node/pull/32305)
* \[[`416531227e`](https://github.com/nodejs/node/commit/416531227e)] - **test**: remove unnecessary console.log() calls (Juan José Arboleda) [#32541](https://github.com/nodejs/node/pull/32541)
* \[[`30d21fb6e6`](https://github.com/nodejs/node/commit/30d21fb6e6)] - **test**: replace console.log() with debuglog() (Juan José Arboleda) [#32550](https://github.com/nodejs/node/pull/32550)
* \[[`fcf1123052`](https://github.com/nodejs/node/commit/fcf1123052)] - **test**: validate util.format when the value is 'Infinity' (Andrés M. Gómez) [#32573](https://github.com/nodejs/node/pull/32573)
* \[[`e2174e4e3c`](https://github.com/nodejs/node/commit/e2174e4e3c)] - **test**: fix fs test-fs-utimes strictEqual arg order (Ben Noordhuis) [#32420](https://github.com/nodejs/node/pull/32420)
* \[[`32ab30cc35`](https://github.com/nodejs/node/commit/32ab30cc35)] - **test**: use common.mustCall in test-worker-esm-exit (himself65) [#32544](https://github.com/nodejs/node/pull/32544)
* \[[`a0552441fa`](https://github.com/nodejs/node/commit/a0552441fa)] - **test**: use template strings in parallel tests (Daniel Estiven Rico Posada) [#32549](https://github.com/nodejs/node/pull/32549)
* \[[`d53d152da3`](https://github.com/nodejs/node/commit/d53d152da3)] - **test**: add known issues test for #31733 (Ben Noordhuis) [#31734](https://github.com/nodejs/node/pull/31734)
* \[[`d6f6623243`](https://github.com/nodejs/node/commit/d6f6623243)] - **test**: refactor test-http-information-processing (Rich Trott) [#32547](https://github.com/nodejs/node/pull/32547)
* \[[`b6e739a6b3`](https://github.com/nodejs/node/commit/b6e739a6b3)] - **test**: skip a wasi test on IBMi PASE (Xu Meng) [#32459](https://github.com/nodejs/node/pull/32459)
* \[[`a40e7daf3c`](https://github.com/nodejs/node/commit/a40e7daf3c)] - **test**: harden the tick sampling logic (Harshitha KP) [#32190](https://github.com/nodejs/node/pull/32190)
* \[[`9c84d7773a`](https://github.com/nodejs/node/commit/9c84d7773a)] - **test**: skip some binding tests on IBMi PASE (Xu Meng) [#31967](https://github.com/nodejs/node/pull/31967)
* \[[`afc0c708a2`](https://github.com/nodejs/node/commit/afc0c708a2)] - **test**: revise test-http-response-multi-content-length (Rich Trott) [#32526](https://github.com/nodejs/node/pull/32526)
* \[[`df890ad3d2`](https://github.com/nodejs/node/commit/df890ad3d2)] - **test**: remove a duplicated test (himself65) [#32453](https://github.com/nodejs/node/pull/32453)
* \[[`fa4de53a3e`](https://github.com/nodejs/node/commit/fa4de53a3e)] - **test**: check bundled binaries are signed on macOS (Richard Lau) [#32522](https://github.com/nodejs/node/pull/32522)
* \[[`d9abea5e3f`](https://github.com/nodejs/node/commit/d9abea5e3f)] - **test**: unflake async-hooks/test-statwatcher (Bartosz Sosnowski) [#32484](https://github.com/nodejs/node/pull/32484)
* \[[`5cae1b7a53`](https://github.com/nodejs/node/commit/5cae1b7a53)] - **test**: use Promise.all() in test-cluster-net-listen-ipv6only-false (Rich Trott) [#32398](https://github.com/nodejs/node/pull/32398)
* \[[`60db56ddba`](https://github.com/nodejs/node/commit/60db56ddba)] - **test**: replace Map with Array in test-cluster-net-listen-ipv6only-false (Rich Trott) [#32398](https://github.com/nodejs/node/pull/32398)
* \[[`565f0f73e2`](https://github.com/nodejs/node/commit/565f0f73e2)] - **test**: revise test-http-client-default-headers-exist (Rich Trott) [#32493](https://github.com/nodejs/node/pull/32493)
* \[[`7f5b89c307`](https://github.com/nodejs/node/commit/7f5b89c307)] - **test**: use mustCall in place of countdown in timers test (Rich Trott) [#32416](https://github.com/nodejs/node/pull/32416)
* \[[`97e352d1a6`](https://github.com/nodejs/node/commit/97e352d1a6)] - **test**: replace countdown with Promise.all() in cluster-net-listen tests (Rich Trott) [#32381](https://github.com/nodejs/node/pull/32381)
* \[[`1b79174203`](https://github.com/nodejs/node/commit/1b79174203)] - **test**: replace Map with Array in cluster-net-listen tests (Rich Trott) [#32381](https://github.com/nodejs/node/pull/32381)
* \[[`85ae5661df`](https://github.com/nodejs/node/commit/85ae5661df)] - **test**: uv\_tty\_init returns EBADF on IBM i (Xu Meng) [#32338](https://github.com/nodejs/node/pull/32338)
* \[[`8dbd7cf0e4`](https://github.com/nodejs/node/commit/8dbd7cf0e4)] - **test**: use Promise.all() in test-hash-seed (Rich Trott) [#32273](https://github.com/nodejs/node/pull/32273)
* \[[`92a207cd2d`](https://github.com/nodejs/node/commit/92a207cd2d)] - **test**: workaround for V8 8.1 inspector pause issue (Matheus Marchini) [#32234](https://github.com/nodejs/node/pull/32234)
* \[[`776905ef99`](https://github.com/nodejs/node/commit/776905ef99)] - **test**: use portable EOL (Harshitha KP) [#32104](https://github.com/nodejs/node/pull/32104)
* \[[`914edddd79`](https://github.com/nodejs/node/commit/914edddd79)] - **test**: `buffer.write` with longer string scenario (Harshitha KP) [#32123](https://github.com/nodejs/node/pull/32123)
* \[[`7060ed1176`](https://github.com/nodejs/node/commit/7060ed1176)] - **test**: fix test-tls-env-extra-ca-file-load (Eric Bickle) [#32073](https://github.com/nodejs/node/pull/32073)
* \[[`bee009d271`](https://github.com/nodejs/node/commit/bee009d271)] - **test**: improve test-fs-existssync-false.js (himself65) [#31883](https://github.com/nodejs/node/pull/31883)
* \[[`0403f00321`](https://github.com/nodejs/node/commit/0403f00321)] - **test**: mark test-timers-blocking-callback flaky on osx (Myles Borins) [#32189](https://github.com/nodejs/node/pull/32189)
* \[[`fa7e975d2f`](https://github.com/nodejs/node/commit/fa7e975d2f)] - **test**: warn when inspector process crashes (Matheus Marchini) [#32133](https://github.com/nodejs/node/pull/32133)
* \[[`4a94179a3c`](https://github.com/nodejs/node/commit/4a94179a3c)] - **tools**: update Boxstarter script and document (himself65) [#32299](https://github.com/nodejs/node/pull/32299)
* \[[`8bc53d1298`](https://github.com/nodejs/node/commit/8bc53d1298)] - **tools**: update ESLint to 7.0.0-alpha.3 (Colin Ihrig) [#32533](https://github.com/nodejs/node/pull/32533)
* \[[`baf56f8135`](https://github.com/nodejs/node/commit/baf56f8135)] - **tools**: fixup icutrim.py use of string and bytes objects (Jonathan MERCIER) [#31659](https://github.com/nodejs/node/pull/31659)
* \[[`540a024057`](https://github.com/nodejs/node/commit/540a024057)] - **tools**: update to acorn\@7.1.1 (Rich Trott) [#32259](https://github.com/nodejs/node/pull/32259)
* \[[`ecf842ec27`](https://github.com/nodejs/node/commit/ecf842ec27)] - **tools**: enable no-useless-backreference lint rule (Colin Ihrig) [#31400](https://github.com/nodejs/node/pull/31400)
* \[[`bcf152e2d0`](https://github.com/nodejs/node/commit/bcf152e2d0)] - **tools**: enable default-case-last lint rule (Colin Ihrig) [#31400](https://github.com/nodejs/node/pull/31400)
* \[[`5dacfa76f2`](https://github.com/nodejs/node/commit/5dacfa76f2)] - **tools**: update ESLint to 7.0.0-alpha.2 (Colin Ihrig) [#31400](https://github.com/nodejs/node/pull/31400)
* \[[`e641b3c6b6`](https://github.com/nodejs/node/commit/e641b3c6b6)] - **tools**: update ESLint to 7.0.0-alpha.1 (Colin Ihrig) [#31400](https://github.com/nodejs/node/pull/31400)
* \[[`394fa1f356`](https://github.com/nodejs/node/commit/394fa1f356)] - **tools**: update ESLint to 7.0.0-alpha.0 (Colin Ihrig) [#31400](https://github.com/nodejs/node/pull/31400)
* \[[`848df6f6cc`](https://github.com/nodejs/node/commit/848df6f6cc)] - **tracing**: do not attempt to call into JS when disallowed (Anna Henningsen) [#32548](https://github.com/nodejs/node/pull/32548)
* \[[`12fe985154`](https://github.com/nodejs/node/commit/12fe985154)] - **util**: only inspect error properties that are not visible otherwise (Ruben Bridgewater) [#32327](https://github.com/nodejs/node/pull/32327)
* \[[`eccd2a7740`](https://github.com/nodejs/node/commit/eccd2a7740)] - **util**: fix inspecting document.all (Gus Caplan) [#31938](https://github.com/nodejs/node/pull/31938)
* \[[`58c6422f83`](https://github.com/nodejs/node/commit/58c6422f83)] - **util**: text decoding allows SharedArrayBuffer (Bradley Farias) [#32203](https://github.com/nodejs/node/pull/32203)
* \[[`10c525f38d`](https://github.com/nodejs/node/commit/10c525f38d)] - **win,build**: set exit\_code on configure failure (Bartlomiej Brzozowski) [#32205](https://github.com/nodejs/node/pull/32205)
* \[[`aeea7d9c1f`](https://github.com/nodejs/node/commit/aeea7d9c1f)] - **worker**: do not emit 'exit' events during process.exit() (Anna Henningsen) [#32546](https://github.com/nodejs/node/pull/32546)
* \[[`28cb7e78ff`](https://github.com/nodejs/node/commit/28cb7e78ff)] - **worker**: improve MessagePort performance (Anna Henningsen) [#31605](https://github.com/nodejs/node/pull/31605)

<a id="12.16.2"></a>

## 2020-04-08, Version 12.16.2 'Erbium' (LTS), @codebytere

### Notable Changes

* **doc**:
  * add ronag to collaborators (Robert Nagy) [#31498](https://github.com/nodejs/node/pull/31498)
  * add GeoffreyBooth to collaborators (Geoffrey Booth) [#31306](https://github.com/nodejs/node/pull/31306)
* **deps**:
  * upgrade npm to 6.13.6 (Ruy Adorno) [#31304](https://github.com/nodejs/node/pull/31304)
  * update openssl to 1.1.1e (Hassaan Pasha) [#32328](https://github.com/nodejs/node/pull/32328)

### Commits

* \[[`f756c809e0`](https://github.com/nodejs/node/commit/f756c809e0)] - **assert**: align character indicators properly (Ruben Bridgewater) [#31429](https://github.com/nodejs/node/pull/31429)
* \[[`597431b1f2`](https://github.com/nodejs/node/commit/597431b1f2)] - **async\_hooks**: remove internal only error checking (Anatoli Papirovski) [#30967](https://github.com/nodejs/node/pull/30967)
* \[[`35f107da53`](https://github.com/nodejs/node/commit/35f107da53)] - **benchmark**: remove problematic tls params (Brian White) [#31816](https://github.com/nodejs/node/pull/31816)
* \[[`0b7579022c`](https://github.com/nodejs/node/commit/0b7579022c)] - **benchmark**: use let instead of var (Daniele Belardi) [#31592](https://github.com/nodejs/node/pull/31592)
* \[[`7173b285e7`](https://github.com/nodejs/node/commit/7173b285e7)] - **benchmark**: swap var for let in benchmarks (Alex Ramirez) [#28958](https://github.com/nodejs/node/pull/28958)
* \[[`c18cec7593`](https://github.com/nodejs/node/commit/c18cec7593)] - **benchmark**: remove special test entries (Ruben Bridgewater) [#31396](https://github.com/nodejs/node/pull/31396)
* \[[`19fbe55451`](https://github.com/nodejs/node/commit/19fbe55451)] - **benchmark**: refactor helper into a class (Ruben Bridgewater) [#31396](https://github.com/nodejs/node/pull/31396)
* \[[`a305ae2308`](https://github.com/nodejs/node/commit/a305ae2308)] - **benchmark**: check for and fix multiple end() (Brian White) [#31624](https://github.com/nodejs/node/pull/31624)
* \[[`7f828a4dd0`](https://github.com/nodejs/node/commit/7f828a4dd0)] - **benchmark**: clean up config resolution in multiple benchmarks (Denys Otrishko) [#31581](https://github.com/nodejs/node/pull/31581)
* \[[`4e40c77dc7`](https://github.com/nodejs/node/commit/4e40c77dc7)] - **benchmark**: add MessagePort benchmark (Anna Henningsen) [#31568](https://github.com/nodejs/node/pull/31568)
* \[[`2973c1fabf`](https://github.com/nodejs/node/commit/2973c1fabf)] - **benchmark**: use let and const instead of var (Daniele Belardi) [#31518](https://github.com/nodejs/node/pull/31518)
* \[[`393b48e744`](https://github.com/nodejs/node/commit/393b48e744)] - **benchmark**: fix getStringWidth() benchmark (Rich Trott) [#31476](https://github.com/nodejs/node/pull/31476)
* \[[`267a01f4eb`](https://github.com/nodejs/node/commit/267a01f4eb)] - **benchmark**: add default type in getstringwidth.js (Rich Trott) [#31377](https://github.com/nodejs/node/pull/31377)
* \[[`4f9d57d01e`](https://github.com/nodejs/node/commit/4f9d57d01e)] - **benchmark**: benchmarking impacts of async hooks on promises (legendecas) [#31188](https://github.com/nodejs/node/pull/31188)
* \[[`06bc2ded77`](https://github.com/nodejs/node/commit/06bc2ded77)] - **buffer**: improve from() performance (Brian White) [#31615](https://github.com/nodejs/node/pull/31615)
* \[[`22a37d6847`](https://github.com/nodejs/node/commit/22a37d6847)] - **buffer**: improve concat() performance (Brian White) [#31522](https://github.com/nodejs/node/pull/31522)
* \[[`1849c2cc99`](https://github.com/nodejs/node/commit/1849c2cc99)] - **buffer**: improve fill(number) performance (Brian White) [#31489](https://github.com/nodejs/node/pull/31489)
* \[[`45d05e1cf6`](https://github.com/nodejs/node/commit/45d05e1cf6)] - **build**: add mjs extension to lint-js (Nick Schonning) [#32145](https://github.com/nodejs/node/pull/32145)
* \[[`fae680f740`](https://github.com/nodejs/node/commit/fae680f740)] - **build**: drop Travis in favor of Actions (Christian Clauss) [#32450](https://github.com/nodejs/node/pull/32450)
* \[[`a50648975d`](https://github.com/nodejs/node/commit/a50648975d)] - **build**: annotate markdown lint failures in pull requests (Richard Lau) [#32391](https://github.com/nodejs/node/pull/32391)
* \[[`c42cb79bb7`](https://github.com/nodejs/node/commit/c42cb79bb7)] - **build**: build docs in GitHub Actions CI workflow (Richard Lau) [#31504](https://github.com/nodejs/node/pull/31504)
* \[[`46f83df2fd`](https://github.com/nodejs/node/commit/46f83df2fd)] - **build**: do not use setup-node in build workflows (Richard Lau) [#31349](https://github.com/nodejs/node/pull/31349)
* \[[`ad12c82e37`](https://github.com/nodejs/node/commit/ad12c82e37)] - **build**: fix macos runner type in GitHub Action (扩散性百万甜面包) [#31327](https://github.com/nodejs/node/pull/31327)
* \[[`df701f3b12`](https://github.com/nodejs/node/commit/df701f3b12)] - **build**: fix step name in GitHub Actions workflow (Richard Lau) [#31323](https://github.com/nodejs/node/pull/31323)
* \[[`d190ee06b4`](https://github.com/nodejs/node/commit/d190ee06b4)] - **build**: add GitHub actions to run linters (Richard Lau) [#31323](https://github.com/nodejs/node/pull/31323)
* \[[`7d3910d078`](https://github.com/nodejs/node/commit/7d3910d078)] - **build**: macOS package notarization (Rod Vagg) [#31459](https://github.com/nodejs/node/pull/31459)
* \[[`36ae81a52a`](https://github.com/nodejs/node/commit/36ae81a52a)] - **build**: allow use of system-installed brotli (André Draszik) [#32046](https://github.com/nodejs/node/pull/32046)
* \[[`6605bba0b8`](https://github.com/nodejs/node/commit/6605bba0b8)] - **build**: allow passing multiple libs to pkg\_config (André Draszik) [#32046](https://github.com/nodejs/node/pull/32046)
* \[[`8a0b0a76c0`](https://github.com/nodejs/node/commit/8a0b0a76c0)] - **build**: enable backtrace when V8 is built for PPC and S390x (Michaël Zasso) [#32113](https://github.com/nodejs/node/pull/32113)
* \[[`4dddb56178`](https://github.com/nodejs/node/commit/4dddb56178)] - **build**: only lint markdown files that have changed (POSIX-only) (Rich Trott) [#31923](https://github.com/nodejs/node/pull/31923)
* \[[`4f36bf78ea`](https://github.com/nodejs/node/commit/4f36bf78ea)] - **build**: add configure option to debug only Node.js part of the binary (Anna Henningsen) [#31644](https://github.com/nodejs/node/pull/31644)
* \[[`a53500e26b`](https://github.com/nodejs/node/commit/a53500e26b)] - **build**: ignore all the "Debug","Release" folders (ConorDavenport) [#31565](https://github.com/nodejs/node/pull/31565)
* \[[`038fd25ef8`](https://github.com/nodejs/node/commit/038fd25ef8)] - **build**: fix zlib tarball generation (Shelley Vohr) [#32094](https://github.com/nodejs/node/pull/32094)
* \[[`e79f368783`](https://github.com/nodejs/node/commit/e79f368783)] - **build**: remove enable\_vtune from vcbuild.bat (Richard Lau) [#31338](https://github.com/nodejs/node/pull/31338)
* \[[`8296224e41`](https://github.com/nodejs/node/commit/8296224e41)] - **build**: add vs2019 to vcbuild.bat help (Richard Lau) [#31338](https://github.com/nodejs/node/pull/31338)
* \[[`93a7f91b52`](https://github.com/nodejs/node/commit/93a7f91b52)] - **build**: silence OpenSSL Windows compiler warnings (Richard Lau) [#31311](https://github.com/nodejs/node/pull/31311)
* \[[`a89893f3df`](https://github.com/nodejs/node/commit/a89893f3df)] - **build**: silence c-ares Windows compiler warnings (Richard Lau) [#31311](https://github.com/nodejs/node/pull/31311)
* \[[`f6a6638d0c`](https://github.com/nodejs/node/commit/f6a6638d0c)] - **build**: test Python 3 using GitHub Actions-based CI (Christian Clauss) [#29474](https://github.com/nodejs/node/pull/29474)
* \[[`aec22268af`](https://github.com/nodejs/node/commit/aec22268af)] - **cli**: allow --jitless V8 flag in NODE\_OPTIONS (Andrew Neitsch) [#32100](https://github.com/nodejs/node/pull/32100)
* \[[`70dc1cefea`](https://github.com/nodejs/node/commit/70dc1cefea)] - **cli**: --perf-prof only works on Linux (Shelley Vohr) [#31892](https://github.com/nodejs/node/pull/31892)
* \[[`f9113fd7c2`](https://github.com/nodejs/node/commit/f9113fd7c2)] - **crypto**: turn impossible DH errors into assertions (Tobias Nießen) [#31934](https://github.com/nodejs/node/pull/31934)
* \[[`c6bbae44a9`](https://github.com/nodejs/node/commit/c6bbae44a9)] - **crypto**: fix ieee-p1363 for createVerify (Tobias Nießen) [#31876](https://github.com/nodejs/node/pull/31876)
* \[[`b84fd947d2`](https://github.com/nodejs/node/commit/b84fd947d2)] - **crypto**: fix performance regression (Robert Nagy) [#31742](https://github.com/nodejs/node/pull/31742)
* \[[`9a3760d2fa`](https://github.com/nodejs/node/commit/9a3760d2fa)] - **crypto**: improve randomBytes() performance (Brian White) [#31519](https://github.com/nodejs/node/pull/31519)
* \[[`a1be32752c`](https://github.com/nodejs/node/commit/a1be32752c)] - **deps**: V8: backport 07ee86a5a28b (Milad Farazmand) [#32619](https://github.com/nodejs/node/pull/32619)
* \[[`a83fc49717`](https://github.com/nodejs/node/commit/a83fc49717)] - **deps**: V8: cherry-pick cb1c2b0fbfe7 (Gerhard Stoebich) [#31939](https://github.com/nodejs/node/pull/31939)
* \[[`784df12494`](https://github.com/nodejs/node/commit/784df12494)] - **deps**: revert whitespace changes on V8 (Matheus Marchini) [#32605](https://github.com/nodejs/node/pull/32605)
* \[[`6db190bb1c`](https://github.com/nodejs/node/commit/6db190bb1c)] - **deps**: upgrade npm to 6.14.4 (Ruy Adorno) [#32495](https://github.com/nodejs/node/pull/32495)
* \[[`652ffa5470`](https://github.com/nodejs/node/commit/652ffa5470)] - **deps**: update term-size with signed version (Rod Vagg) [#31459](https://github.com/nodejs/node/pull/31459)
* \[[`f55d071bfd`](https://github.com/nodejs/node/commit/f55d071bfd)] - **deps**: remove \*.pyc files from deps/npm (Ben Noordhuis) [#32387](https://github.com/nodejs/node/pull/32387)
* \[[`c419cd21e3`](https://github.com/nodejs/node/commit/c419cd21e3)] - **deps**: update npm to 6.14.3 (Myles Borins) [#32368](https://github.com/nodejs/node/pull/32368)
* \[[`17217a5275`](https://github.com/nodejs/node/commit/17217a5275)] - **deps**: upgrade npm to 6.14.1 (Isaac Z. Schlueter) [#31977](https://github.com/nodejs/node/pull/31977)
* \[[`260bd810e9`](https://github.com/nodejs/node/commit/260bd810e9)] - **deps**: update archs files for OpenSSL-1.1.1e (Hassaan Pasha) [#32328](https://github.com/nodejs/node/pull/32328)
* \[[`e96e8afead`](https://github.com/nodejs/node/commit/e96e8afead)] - **deps**: adjust openssl configuration for 1.1.1e (Hassaan Pasha) [#32328](https://github.com/nodejs/node/pull/32328)
* \[[`4de1afd32d`](https://github.com/nodejs/node/commit/4de1afd32d)] - **deps**: upgrade openssl sources to 1.1.1e (Hassaan Pasha) [#32328](https://github.com/nodejs/node/pull/32328)
* \[[`e2c40ccb78`](https://github.com/nodejs/node/commit/e2c40ccb78)] - **deps**: V8: backport f7771e5b0cc4 (Matheus Marchini) [#31957](https://github.com/nodejs/node/pull/31957)
* \[[`78d9a50c83`](https://github.com/nodejs/node/commit/78d9a50c83)] - **deps**: openssl: cherry-pick 4dcb150ea30f (Adam Majer) [#32002](https://github.com/nodejs/node/pull/32002)
* \[[`ff1e5e01f4`](https://github.com/nodejs/node/commit/ff1e5e01f4)] - **deps**: upgrade npm to 6.13.7 (Michael Perrotte) [#31558](https://github.com/nodejs/node/pull/31558)
* \[[`48e4d45cca`](https://github.com/nodejs/node/commit/48e4d45cca)] - **deps**: uvwasi: cherry-pick 7b5b6f9 (Colin Ihrig) [#31495](https://github.com/nodejs/node/pull/31495)
* \[[`604ce0aa5a`](https://github.com/nodejs/node/commit/604ce0aa5a)] - **deps**: upgrade to libuv 1.34.2 (Colin Ihrig) [#31477](https://github.com/nodejs/node/pull/31477)
* \[[`3fb3079337`](https://github.com/nodejs/node/commit/3fb3079337)] - **deps**: uvwasi: cherry-pick eea4508 (Colin Ihrig) [#31432](https://github.com/nodejs/node/pull/31432)
* \[[`3bd1c02941`](https://github.com/nodejs/node/commit/3bd1c02941)] - **deps**: uvwasi: cherry-pick c3bef8e (Colin Ihrig) [#31432](https://github.com/nodejs/node/pull/31432)
* \[[`12949019de`](https://github.com/nodejs/node/commit/12949019de)] - **deps**: uvwasi: cherry-pick ea73af5 (Colin Ihrig) [#31432](https://github.com/nodejs/node/pull/31432)
* \[[`ada070eed4`](https://github.com/nodejs/node/commit/ada070eed4)] - **deps**: update to uvwasi 0.0.5 (Colin Ihrig) [#31432](https://github.com/nodejs/node/pull/31432)
* \[[`8cf2666248`](https://github.com/nodejs/node/commit/8cf2666248)] - **deps**: uvwasi: cherry-pick 941bedf (Colin Ihrig) [#31363](https://github.com/nodejs/node/pull/31363)
* \[[`ef5517aa0c`](https://github.com/nodejs/node/commit/ef5517aa0c)] - **deps**: port uvwasi\@676ba9a to gyp (Colin Ihrig) [#31363](https://github.com/nodejs/node/pull/31363)
* \[[`bbb8ae7bd0`](https://github.com/nodejs/node/commit/bbb8ae7bd0)] - **deps**: upgrade to libuv 1.34.1 (Colin Ihrig) [#31332](https://github.com/nodejs/node/pull/31332)
* \[[`7a8963bc75`](https://github.com/nodejs/node/commit/7a8963bc75)] - **deps**: upgrade npm to 6.13.6 (Ruy Adorno) [#31304](https://github.com/nodejs/node/pull/31304)
* \[[`676e3c3c38`](https://github.com/nodejs/node/commit/676e3c3c38)] - **deps,test**: update to uvwasi 0.0.4 (Colin Ihrig) [#31363](https://github.com/nodejs/node/pull/31363)
* \[[`e88960d1f4`](https://github.com/nodejs/node/commit/e88960d1f4)] - **doc**: esm spec bug (Myles Borins) [#32610](https://github.com/nodejs/node/pull/32610)
* \[[`155a6c397d`](https://github.com/nodejs/node/commit/155a6c397d)] - **doc**: update conditional exports recommendations (Guy Bedford) [#32098](https://github.com/nodejs/node/pull/32098)
* \[[`7e56e3dee3`](https://github.com/nodejs/node/commit/7e56e3dee3)] - **doc**: remove unnecessary "obvious(ly)" modifiers in esm.md (Rich Trott) [#32457](https://github.com/nodejs/node/pull/32457)
* \[[`61f44c94ae`](https://github.com/nodejs/node/commit/61f44c94ae)] - **doc**: fix lint warning in doc/api/esm.md (Richard Lau) [#32462](https://github.com/nodejs/node/pull/32462)
* \[[`d8e17bf12a`](https://github.com/nodejs/node/commit/d8e17bf12a)] - **doc**: improve wording in esm.md (Rich Trott) [#32427](https://github.com/nodejs/node/pull/32427)
* \[[`8b961347fe`](https://github.com/nodejs/node/commit/8b961347fe)] - **doc**: import clarifications with links to MDN (Eric Dobbertin) [#31479](https://github.com/nodejs/node/pull/31479)
* \[[`f58594b8ec`](https://github.com/nodejs/node/commit/f58594b8ec)] - **doc**: update releaser list in README.md (Myles Borins) [#32577](https://github.com/nodejs/node/pull/32577)
* \[[`885c88ee5b`](https://github.com/nodejs/node/commit/885c88ee5b)] - **doc**: official macOS builds now on 10.15 + Xcode 11 (Rod Vagg) [#31459](https://github.com/nodejs/node/pull/31459)
* \[[`efd20f08e8`](https://github.com/nodejs/node/commit/efd20f08e8)] - **doc**: link setRawMode() from signal docs (Anna Henningsen) [#32088](https://github.com/nodejs/node/pull/32088)
* \[[`3f1b9162f6`](https://github.com/nodejs/node/commit/3f1b9162f6)] - **doc**: document self-referencing a package name (Gil Tayar) [#31680](https://github.com/nodejs/node/pull/31680)
* \[[`f820ce6e50`](https://github.com/nodejs/node/commit/f820ce6e50)] - **doc**: add AsyncResource + Worker pool example (Anna Henningsen) [#31601](https://github.com/nodejs/node/pull/31601)
* \[[`df8d51b411`](https://github.com/nodejs/node/commit/df8d51b411)] - **doc**: document fs.watchFile() bigint option (Colin Ihrig) [#32128](https://github.com/nodejs/node/pull/32128)
* \[[`eaf615709f`](https://github.com/nodejs/node/commit/eaf615709f)] - **doc**: fix broken links in benchmark README (Rich Trott) [#32121](https://github.com/nodejs/node/pull/32121)
* \[[`047bd9d38e`](https://github.com/nodejs/node/commit/047bd9d38e)] - **doc**: update email address in authors (Yael Hermon) [#32026](https://github.com/nodejs/node/pull/32026)
* \[[`c20f2cd41d`](https://github.com/nodejs/node/commit/c20f2cd41d)] - **doc**: update maintaining-V8.md (kenzo-spaulding) [#31503](https://github.com/nodejs/node/pull/31503)
* \[[`05fbc80f45`](https://github.com/nodejs/node/commit/05fbc80f45)] - **doc**: visibility of Worker threads cli options (Harshitha KP) [#31380](https://github.com/nodejs/node/pull/31380)
* \[[`224a17e202`](https://github.com/nodejs/node/commit/224a17e202)] - **doc**: improve doc/markdown file organization coherence (ConorDavenport) [#31792](https://github.com/nodejs/node/pull/31792)
* \[[`df54a1932a`](https://github.com/nodejs/node/commit/df54a1932a)] - **doc**: revise --zero-fill-buffers text in buffer.md (Rich Trott) [#32019](https://github.com/nodejs/node/pull/32019)
* \[[`9161b7e5c3`](https://github.com/nodejs/node/commit/9161b7e5c3)] - **doc**: add link to sem-ver info (unknown) [#31985](https://github.com/nodejs/node/pull/31985)
* \[[`1630320781`](https://github.com/nodejs/node/commit/1630320781)] - **doc**: update zlib doc (James M Snell) [#31665](https://github.com/nodejs/node/pull/31665)
* \[[`a1c0d467ef`](https://github.com/nodejs/node/commit/a1c0d467ef)] - **doc**: clarify http2.connect authority details (James M Snell) [#31828](https://github.com/nodejs/node/pull/31828)
* \[[`ed86854025`](https://github.com/nodejs/node/commit/ed86854025)] - **doc**: updated YAML version representation in readline.md (Rich Trott) [#31924](https://github.com/nodejs/node/pull/31924)
* \[[`370653f1a0`](https://github.com/nodejs/node/commit/370653f1a0)] - **doc**: update releases guide re pushing tags (Myles Borins) [#31855](https://github.com/nodejs/node/pull/31855)
* \[[`ab735d0144`](https://github.com/nodejs/node/commit/ab735d0144)] - **doc**: update assert.rejects() docs with a validation function example (Eric Eastwood) [#31271](https://github.com/nodejs/node/pull/31271)
* \[[`911dfc5099`](https://github.com/nodejs/node/commit/911dfc5099)] - **doc**: add note about ssh key to releases (Shelley Vohr) [#31856](https://github.com/nodejs/node/pull/31856)
* \[[`e83af20b70`](https://github.com/nodejs/node/commit/e83af20b70)] - **doc**: add note in BUILDING.md about running `make distclean` (Swagat Konchada) [#31542](https://github.com/nodejs/node/pull/31542)
* \[[`17882ac83d`](https://github.com/nodejs/node/commit/17882ac83d)] - **doc**: reword possessive form of Node.js in adding-new-napi-api.md (Rich Trott) [#31748](https://github.com/nodejs/node/pull/31748)
* \[[`648f83135e`](https://github.com/nodejs/node/commit/648f83135e)] - **doc**: reword possessive form of Node.js in http.md (Rich Trott) [#31748](https://github.com/nodejs/node/pull/31748)
* \[[`d034eb41f2`](https://github.com/nodejs/node/commit/d034eb41f2)] - **doc**: reword possessive form of Node.js in process.md (Rich Trott) [#31748](https://github.com/nodejs/node/pull/31748)
* \[[`b8d2997950`](https://github.com/nodejs/node/commit/b8d2997950)] - **doc**: reword possessive form of Node.js in debugger.md (Rich Trott) [#31748](https://github.com/nodejs/node/pull/31748)
* \[[`aebaeadf05`](https://github.com/nodejs/node/commit/aebaeadf05)] - **doc**: move gireeshpunathil to TSC emeritus (Gireesh Punathil) [#31770](https://github.com/nodejs/node/pull/31770)
* \[[`88a6d9b077`](https://github.com/nodejs/node/commit/88a6d9b077)] - **doc**: pronouns for @Fishrock123 (Jeremiah Senkpiel) [#31725](https://github.com/nodejs/node/pull/31725)
* \[[`b3d0795a4a`](https://github.com/nodejs/node/commit/b3d0795a4a)] - **doc**: move @Fishrock123 to TSC Emeriti (Jeremiah Senkpiel) [#31725](https://github.com/nodejs/node/pull/31725)
* \[[`e65c1c25fc`](https://github.com/nodejs/node/commit/e65c1c25fc)] - **doc**: move @Fishrock123 to a previous releaser (Jeremiah Senkpiel) [#31725](https://github.com/nodejs/node/pull/31725)
* \[[`38d3f56ea3`](https://github.com/nodejs/node/commit/38d3f56ea3)] - **doc**: fix typos in doc/api/https.md (Jeff) [#31793](https://github.com/nodejs/node/pull/31793)
* \[[`de275d0e0b`](https://github.com/nodejs/node/commit/de275d0e0b)] - **doc**: guide - using valgrind to debug memory leaks (Michael Dawson) [#31501](https://github.com/nodejs/node/pull/31501)
* \[[`82defc0d15`](https://github.com/nodejs/node/commit/82defc0d15)] - **doc**: add glossary.md (gengjiawen) [#27517](https://github.com/nodejs/node/pull/27517)
* \[[`01ec7221e6`](https://github.com/nodejs/node/commit/01ec7221e6)] - **doc**: add prerequisites information for Arch (Ujjwal Sharma) [#31669](https://github.com/nodejs/node/pull/31669)
* \[[`a7a6261fa4`](https://github.com/nodejs/node/commit/a7a6261fa4)] - **doc**: fix typo on fs docs (Juan José Arboleda) [#31620](https://github.com/nodejs/node/pull/31620)
* \[[`d4c1a8cc7b`](https://github.com/nodejs/node/commit/d4c1a8cc7b)] - **doc**: update contact email for @ryzokuken (Ujjwal Sharma) [#31670](https://github.com/nodejs/node/pull/31670)
* \[[`86686ccbab`](https://github.com/nodejs/node/commit/86686ccbab)] - **doc**: fix default server timeout description for https (Andrey Pechkurov) [#31692](https://github.com/nodejs/node/pull/31692)
* \[[`448fe39c35`](https://github.com/nodejs/node/commit/448fe39c35)] - **doc**: add directions to mark a release line as lts (Danielle Adams) [#31724](https://github.com/nodejs/node/pull/31724)
* \[[`dbe2da65c9`](https://github.com/nodejs/node/commit/dbe2da65c9)] - **doc**: expand C++ README with information about exception handling (Anna Henningsen) [#31720](https://github.com/nodejs/node/pull/31720)
* \[[`236943ac5b`](https://github.com/nodejs/node/commit/236943ac5b)] - **doc**: update foundation name in onboarding (Tobias Nießen) [#31719](https://github.com/nodejs/node/pull/31719)
* \[[`165047a787`](https://github.com/nodejs/node/commit/165047a787)] - **doc**: reword possessive form of Node.js in zlib.md (Rich Trott) [#31713](https://github.com/nodejs/node/pull/31713)
* \[[`d3201d7933`](https://github.com/nodejs/node/commit/d3201d7933)] - **doc**: reword possessive form of Node.js in modules.md (Rich Trott) [#31713](https://github.com/nodejs/node/pull/31713)
* \[[`4c65d0a3d3`](https://github.com/nodejs/node/commit/4c65d0a3d3)] - **doc**: reword possessive form of Node.js in repl.md (Rich Trott) [#31713](https://github.com/nodejs/node/pull/31713)
* \[[`4c5c340d28`](https://github.com/nodejs/node/commit/4c5c340d28)] - **doc**: reword section title in addons.md (Rich Trott) [#31713](https://github.com/nodejs/node/pull/31713)
* \[[`1db85aa71c`](https://github.com/nodejs/node/commit/1db85aa71c)] - **doc**: revise deepEqual() legacy assertion mode text (Rich Trott) [#31704](https://github.com/nodejs/node/pull/31704)
* \[[`aadd8cac4b`](https://github.com/nodejs/node/commit/aadd8cac4b)] - **doc**: improve strict assertion mode color text (Rich Trott) [#31703](https://github.com/nodejs/node/pull/31703)
* \[[`708aff953a`](https://github.com/nodejs/node/commit/708aff953a)] - **doc**: consolidate introductory text (Rich Trott) [#31667](https://github.com/nodejs/node/pull/31667)
* \[[`959fa8ff9d`](https://github.com/nodejs/node/commit/959fa8ff9d)] - **doc**: simplify async\_hooks overview (Rich Trott) [#31660](https://github.com/nodejs/node/pull/31660)
* \[[`28657cc614`](https://github.com/nodejs/node/commit/28657cc614)] - **doc**: clarify Worker exit/message event ordering (Anna Henningsen) [#31642](https://github.com/nodejs/node/pull/31642)
* \[[`cb75ca1a51`](https://github.com/nodejs/node/commit/cb75ca1a51)] - **doc**: update TSC name in "Release Process" (Tobias Nießen) [#31652](https://github.com/nodejs/node/pull/31652)
* \[[`76b500218b`](https://github.com/nodejs/node/commit/76b500218b)] - **doc**: remove .github/ISSUE\_TEMPLATE.md in favor of the template folder (Joyee Cheung) [#31656](https://github.com/nodejs/node/pull/31656)
* \[[`55c7b9f94a`](https://github.com/nodejs/node/commit/55c7b9f94a)] - **doc**: correct getting an ArrayBuffer's length (tsabolov) [#31632](https://github.com/nodejs/node/pull/31632)
* \[[`afeaec7d6f`](https://github.com/nodejs/node/commit/afeaec7d6f)] - **doc**: ask more questions in the bug report template (Joyee Cheung) [#31611](https://github.com/nodejs/node/pull/31611)
* \[[`06d5a9c0a0`](https://github.com/nodejs/node/commit/06d5a9c0a0)] - **doc**: add example to fs.promises.readdir (Conor ONeill) [#31552](https://github.com/nodejs/node/pull/31552)
* \[[`351c86310b`](https://github.com/nodejs/node/commit/351c86310b)] - **doc**: fix numbering (Steffen) [#31575](https://github.com/nodejs/node/pull/31575)
* \[[`356e505ab0`](https://github.com/nodejs/node/commit/356e505ab0)] - **doc**: clarify socket.setNoDelay() explanation (Rusty Conover) [#31541](https://github.com/nodejs/node/pull/31541)
* \[[`b2e571ea65`](https://github.com/nodejs/node/commit/b2e571ea65)] - **doc**: clarify require() OS independence (Denys Otrishko) [#31571](https://github.com/nodejs/node/pull/31571)
* \[[`1759f0ab52`](https://github.com/nodejs/node/commit/1759f0ab52)] - **doc**: add protocol option in http2.connect() (Michael Lumish) [#31560](https://github.com/nodejs/node/pull/31560)
* \[[`f5663d92b8`](https://github.com/nodejs/node/commit/f5663d92b8)] - **doc**: clarify that `v8.serialize()` is not deterministic (Anna Henningsen) [#31548](https://github.com/nodejs/node/pull/31548)
* \[[`af61c5d1b2`](https://github.com/nodejs/node/commit/af61c5d1b2)] - **doc**: update job reference in COLLABORATOR\_GUIDE.md (Richard Lau) [#31557](https://github.com/nodejs/node/pull/31557)
* \[[`f4bdcf86ce`](https://github.com/nodejs/node/commit/f4bdcf86ce)] - **doc**: simultaneous blog and email of sec announce (Sam Roberts) [#31483](https://github.com/nodejs/node/pull/31483)
* \[[`5286ccc1dc`](https://github.com/nodejs/node/commit/5286ccc1dc)] - **doc**: update collaborator guide citgm instructions (Robert Nagy) [#31549](https://github.com/nodejs/node/pull/31549)
* \[[`1cf450c51f`](https://github.com/nodejs/node/commit/1cf450c51f)] - **doc**: change error message testing policy (Tobias Nießen) [#31421](https://github.com/nodejs/node/pull/31421)
* \[[`d978bb56dd`](https://github.com/nodejs/node/commit/d978bb56dd)] - **doc**: remove redundant properties from headers (XhmikosR) [#31492](https://github.com/nodejs/node/pull/31492)
* \[[`e48f874afd`](https://github.com/nodejs/node/commit/e48f874afd)] - **doc**: enable visual code indication in headers (Rich Trott) [#31493](https://github.com/nodejs/node/pull/31493)
* \[[`8c78b87d97`](https://github.com/nodejs/node/commit/8c78b87d97)] - **doc**: clean up and streamline vm.md examples (Denys Otrishko) [#31474](https://github.com/nodejs/node/pull/31474)
* \[[`821b9ac007`](https://github.com/nodejs/node/commit/821b9ac007)] - **doc**: further fix async iterator example (Robert Nagy) [#31367](https://github.com/nodejs/node/pull/31367)
* \[[`f0b5f9fb94`](https://github.com/nodejs/node/commit/f0b5f9fb94)] - **doc**: add ronag to collaborators (Robert Nagy) [#31498](https://github.com/nodejs/node/pull/31498)
* \[[`37754bab2d`](https://github.com/nodejs/node/commit/37754bab2d)] - **doc**: fix code display in header glitch (Rich Trott) [#31460](https://github.com/nodejs/node/pull/31460)
* \[[`40480e0c0d`](https://github.com/nodejs/node/commit/40480e0c0d)] - **doc**: fix syntax in N-API documentation (Tobias Nießen) [#31466](https://github.com/nodejs/node/pull/31466)
* \[[`11dbdcb839`](https://github.com/nodejs/node/commit/11dbdcb839)] - **doc**: add explanatory to path.resolve description (Yakov Litvin) [#31430](https://github.com/nodejs/node/pull/31430)
* \[[`5e8f8b8320`](https://github.com/nodejs/node/commit/5e8f8b8320)] - **doc**: document process.std\*.fd (Harshitha KP) [#31395](https://github.com/nodejs/node/pull/31395)
* \[[`c7f03ad8ca`](https://github.com/nodejs/node/commit/c7f03ad8ca)] - **doc**: fix several child\_process doc typos (Colin Ihrig) [#31393](https://github.com/nodejs/node/pull/31393)
* \[[`2d9f111011`](https://github.com/nodejs/node/commit/2d9f111011)] - **doc**: correct added version for --abort-on-uncaught-exception (Anna Henningsen) [#31360](https://github.com/nodejs/node/pull/31360)
* \[[`d944fa71dd`](https://github.com/nodejs/node/commit/d944fa71dd)] - **doc**: explain `hex` encoding in Buffer API (Harshitha KP) [#31352](https://github.com/nodejs/node/pull/31352)
* \[[`ff8f0bc3cc`](https://github.com/nodejs/node/commit/ff8f0bc3cc)] - **doc**: explain \_writev() API (Harshitha KP) [#31356](https://github.com/nodejs/node/pull/31356)
* \[[`b4d15a9adc`](https://github.com/nodejs/node/commit/b4d15a9adc)] - **doc**: document missing properties in child\_process (Harshitha KP) [#31342](https://github.com/nodejs/node/pull/31342)
* \[[`9aa4fcc052`](https://github.com/nodejs/node/commit/9aa4fcc052)] - **doc**: standardize on "host name" in deprecations.md (Rich Trott) [#31326](https://github.com/nodejs/node/pull/31326)
* \[[`175a5ec795`](https://github.com/nodejs/node/commit/175a5ec795)] - **doc**: standardize on "host name" in url.md (Rich Trott) [#31326](https://github.com/nodejs/node/pull/31326)
* \[[`5f45eaf390`](https://github.com/nodejs/node/commit/5f45eaf390)] - **doc**: standardize on "host name" in tls.md (Rich Trott) [#31326](https://github.com/nodejs/node/pull/31326)
* \[[`54b5346392`](https://github.com/nodejs/node/commit/54b5346392)] - **doc**: standardize on "host name" in os.md (Rich Trott) [#31326](https://github.com/nodejs/node/pull/31326)
* \[[`ac3d0c90f5`](https://github.com/nodejs/node/commit/ac3d0c90f5)] - **doc**: standardize on "host name" in net.md (Rich Trott) [#31326](https://github.com/nodejs/node/pull/31326)
* \[[`9217b7a639`](https://github.com/nodejs/node/commit/9217b7a639)] - **doc**: standardize on "host name" in https.md (Rich Trott) [#31326](https://github.com/nodejs/node/pull/31326)
* \[[`9bca4514bf`](https://github.com/nodejs/node/commit/9bca4514bf)] - **doc**: standardize on "host name" in http2.md (Rich Trott) [#31326](https://github.com/nodejs/node/pull/31326)
* \[[`a419698b18`](https://github.com/nodejs/node/commit/a419698b18)] - **doc**: standardize on "host name" in fs.md (Rich Trott) [#31326](https://github.com/nodejs/node/pull/31326)
* \[[`d4a0300424`](https://github.com/nodejs/node/commit/d4a0300424)] - **doc**: standardize on "host name" in errors.md (Rich Trott) [#31326](https://github.com/nodejs/node/pull/31326)
* \[[`ad701329d6`](https://github.com/nodejs/node/commit/ad701329d6)] - **doc**: standardize on "host name" in dgram.md (Rich Trott) [#31326](https://github.com/nodejs/node/pull/31326)
* \[[`0eba07b267`](https://github.com/nodejs/node/commit/0eba07b267)] - **doc**: standardize on "host name" in async\_hooks.md (Rich Trott) [#31326](https://github.com/nodejs/node/pull/31326)
* \[[`52a4a44b76`](https://github.com/nodejs/node/commit/52a4a44b76)] - **doc**: fix a code example in crypto.md (himself65) [#31313](https://github.com/nodejs/node/pull/31313)
* \[[`6598a08308`](https://github.com/nodejs/node/commit/6598a08308)] - **doc**: add an example for util.types.isExternal (Harshitha KP) [#31173](https://github.com/nodejs/node/pull/31173)
* \[[`760bedee44`](https://github.com/nodejs/node/commit/760bedee44)] - **doc**: fix example of parsing request.url (Egor Pavlov) [#31302](https://github.com/nodejs/node/pull/31302)
* \[[`fa0762d663`](https://github.com/nodejs/node/commit/fa0762d663)] - **doc**: improve doc v8.getHeapSpaceStatistics() 'GetHeapSpaceStatistics' (dev-313) [#31274](https://github.com/nodejs/node/pull/31274)
* \[[`cb40a1a90f`](https://github.com/nodejs/node/commit/cb40a1a90f)] - **doc**: update README to make Node.js description clearer (carterbancroft) [#31266](https://github.com/nodejs/node/pull/31266)
* \[[`dd9a6c6c22`](https://github.com/nodejs/node/commit/dd9a6c6c22)] - **doc**: fix a code example in zlib.md (Alexander Wang) [#31264](https://github.com/nodejs/node/pull/31264)
* \[[`97c12f120e`](https://github.com/nodejs/node/commit/97c12f120e)] - **doc**: add GeoffreyBooth to collaborators (Geoffrey Booth) [#31306](https://github.com/nodejs/node/pull/31306)
* \[[`63af1ab60f`](https://github.com/nodejs/node/commit/63af1ab60f)] - **doc**: update description of `External` (Anna Henningsen) [#31255](https://github.com/nodejs/node/pull/31255)
* \[[`e398137020`](https://github.com/nodejs/node/commit/e398137020)] - **doc**: rename iterator to iterable in examples (Robert Nagy) [#31252](https://github.com/nodejs/node/pull/31252)
* \[[`4922184310`](https://github.com/nodejs/node/commit/4922184310)] - **doc**: fix stream async iterator sample (Robert Nagy) [#31252](https://github.com/nodejs/node/pull/31252)
* \[[`623e1118a0`](https://github.com/nodejs/node/commit/623e1118a0)] - **doc**: correct filehandle.\[read|write|append]File() (Bryan English) [#31235](https://github.com/nodejs/node/pull/31235)
* \[[`60e35d454c`](https://github.com/nodejs/node/commit/60e35d454c)] - **doc**: prefer server vs srv and client vs clt (Andrew Hughes) [#31224](https://github.com/nodejs/node/pull/31224)
* \[[`6cfc4dcfb4`](https://github.com/nodejs/node/commit/6cfc4dcfb4)] - **doc**: explain native external types (Harshitha KP) [#31214](https://github.com/nodejs/node/pull/31214)
* \[[`ebc47f8b52`](https://github.com/nodejs/node/commit/ebc47f8b52)] - **doc**: remove em dashes (Rich Trott) [#32146](https://github.com/nodejs/node/pull/32146)
* \[[`db125c5618`](https://github.com/nodejs/node/commit/db125c5618)] - **doc**: fix missing changelog corrections (Myles Borins) [#31854](https://github.com/nodejs/node/pull/31854)
* \[[`8f75c7497e`](https://github.com/nodejs/node/commit/8f75c7497e)] - **doc,assert**: rename "mode" to "assertion mode" (Rich Trott) [#31635](https://github.com/nodejs/node/pull/31635)
* \[[`fd5aa41178`](https://github.com/nodejs/node/commit/fd5aa41178)] - **doc,crypto**: re-document oaepLabel option (Ben Noordhuis) [#31825](https://github.com/nodejs/node/pull/31825)
* \[[`8f9f92fb33`](https://github.com/nodejs/node/commit/8f9f92fb33)] - **doc,net**: reword Unix domain path paragraph in net.md (Rich Trott) [#31684](https://github.com/nodejs/node/pull/31684)
* \[[`073b4f2750`](https://github.com/nodejs/node/commit/073b4f2750)] - **doc,src**: clarify that one napi\_env is per-module (legendecas) [#31102](https://github.com/nodejs/node/pull/31102)
* \[[`844f893e4e`](https://github.com/nodejs/node/commit/844f893e4e)] - **doc,util**: revise util.md introductory paragraph (Rich Trott) [#31685](https://github.com/nodejs/node/pull/31685)
* \[[`b1517a4f6c`](https://github.com/nodejs/node/commit/b1517a4f6c)] - **errors**: make use of "cannot" consistent (Tobias Nießen) [#31420](https://github.com/nodejs/node/pull/31420)
* \[[`7231090a5d`](https://github.com/nodejs/node/commit/7231090a5d)] - **errors**: remove dead code in ERR\_INVALID\_ARG\_TYPE (Gerhard Stoebich) [#31322](https://github.com/nodejs/node/pull/31322)
* \[[`0e513b2ae7`](https://github.com/nodejs/node/commit/0e513b2ae7)] - **esm**: remove unused parameter on module.instantiate (himself65) [#32147](https://github.com/nodejs/node/pull/32147)
* \[[`05091d48e3`](https://github.com/nodejs/node/commit/05091d48e3)] - **esm**: import.meta.resolve with nodejs: builtins (Guy Bedford) [#31032](https://github.com/nodejs/node/pull/31032)
* \[[`400083b9f5`](https://github.com/nodejs/node/commit/400083b9f5)] - **events**: fix removeListener for Symbols (zfx) [#31847](https://github.com/nodejs/node/pull/31847)
* \[[`de5d162c60`](https://github.com/nodejs/node/commit/de5d162c60)] - **fs**: fix valid id range on chown, lchown, fchown (himself65) [#31694](https://github.com/nodejs/node/pull/31694)
* \[[`d36699662f`](https://github.com/nodejs/node/commit/d36699662f)] - **fs**: set path when mkdir recursive called on file (Benjamin Coe) [#31607](https://github.com/nodejs/node/pull/31607)
* \[[`3d8e850d31`](https://github.com/nodejs/node/commit/3d8e850d31)] - **fs**: bail on permission error in recursive directory creation (Benjamin Coe) [#31505](https://github.com/nodejs/node/pull/31505)
* \[[`fc9c6c3227`](https://github.com/nodejs/node/commit/fc9c6c3227)] - **fs**: do not emit 'close' twice if emitClose enabled (Robert Nagy) [#31383](https://github.com/nodejs/node/pull/31383)
* \[[`ca951e182e`](https://github.com/nodejs/node/commit/ca951e182e)] - **fs**: unset FileHandle fd after close (Anna Henningsen) [#31389](https://github.com/nodejs/node/pull/31389)
* \[[`1fe0065a51`](https://github.com/nodejs/node/commit/1fe0065a51)] - **fs**: add missing HandleScope to FileHandle.close (Anna Henningsen) [#31276](https://github.com/nodejs/node/pull/31276)
* \[[`73c4729652`](https://github.com/nodejs/node/commit/73c4729652)] - **fs**: use async writeFile in FileHandle#appendFile (Bryan English) [#31235](https://github.com/nodejs/node/pull/31235)
* \[[`4745ac4fd7`](https://github.com/nodejs/node/commit/4745ac4fd7)] - **http2**: use custom BaseObject smart pointers (Anna Henningsen) [#30374](https://github.com/nodejs/node/pull/30374)
* \[[`76a0ba689a`](https://github.com/nodejs/node/commit/76a0ba689a)] - **http2**: make compat finished match http/1 (Robert Nagy) [#24347](https://github.com/nodejs/node/pull/24347)
* \[[`f910f645b9`](https://github.com/nodejs/node/commit/f910f645b9)] - **http2**: skip creating native ShutdownWrap (Anna Henningsen) [#31283](https://github.com/nodejs/node/pull/31283)
* \[[`d00a1b9ad2`](https://github.com/nodejs/node/commit/d00a1b9ad2)] - **lib**: replace BigInt64Array global by the primordials (Sebastien Ahkrin) [#31193](https://github.com/nodejs/node/pull/31193)
* \[[`2147c29de0`](https://github.com/nodejs/node/commit/2147c29de0)] - **lib**: add Uint16Array primordials (Sebastien Ahkrin) [#31210](https://github.com/nodejs/node/pull/31210)
* \[[`bc4cbe3f50`](https://github.com/nodejs/node/commit/bc4cbe3f50)] - **lib**: add RegExp primordials (Sebastien Ahkrin) [#31208](https://github.com/nodejs/node/pull/31208)
* \[[`41f0fa742e`](https://github.com/nodejs/node/commit/41f0fa742e)] - **lib**: replace Float32Array global by the primordials (Sebastien Ahkrin) [#31195](https://github.com/nodejs/node/pull/31195)
* \[[`68d48fead3`](https://github.com/nodejs/node/commit/68d48fead3)] - **lib**: replace BigUInt64Array global by the primordials (Sebastien Ahkrin) [#31194](https://github.com/nodejs/node/pull/31194)
* \[[`a0ad12bd7d`](https://github.com/nodejs/node/commit/a0ad12bd7d)] - **lib,tools,test**: remove custom number-isnan rule (Colin Ihrig) [#31211](https://github.com/nodejs/node/pull/31211)
* \[[`a6f56bb11e`](https://github.com/nodejs/node/commit/a6f56bb11e)] - **meta**: move thefourtheye to TSC Emeritus (Rich Trott) [#32059](https://github.com/nodejs/node/pull/32059)
* \[[`ae9f58cbdd`](https://github.com/nodejs/node/commit/ae9f58cbdd)] - **meta**: move not-an-aardvark to emeritus (Rich Trott) [#31928](https://github.com/nodejs/node/pull/31928)
* \[[`553d62c26d`](https://github.com/nodejs/node/commit/553d62c26d)] - **meta**: move aqrln to emeritus (Rich Trott) [#31997](https://github.com/nodejs/node/pull/31997)
* \[[`a44fb3fabf`](https://github.com/nodejs/node/commit/a44fb3fabf)] - **meta**: move jbergstroem to emeritus (Rich Trott) [#31996](https://github.com/nodejs/node/pull/31996)
* \[[`a75aa93b2d`](https://github.com/nodejs/node/commit/a75aa93b2d)] - **meta**: move maclover7 to Emeritus (Rich Trott) [#31994](https://github.com/nodejs/node/pull/31994)
* \[[`fd5c3a749a`](https://github.com/nodejs/node/commit/fd5c3a749a)] - **meta**: move Glen Keane to Collaborator Emeritus (Rich Trott) [#31993](https://github.com/nodejs/node/pull/31993)
* \[[`9251307570`](https://github.com/nodejs/node/commit/9251307570)] - **meta**: move julianduque to emeritus (Rich Trott) [#31863](https://github.com/nodejs/node/pull/31863)
* \[[`2a4d31ae23`](https://github.com/nodejs/node/commit/2a4d31ae23)] - **meta**: move eljefedelrodeodeljefe to emeritus (Rich Trott) [#31735](https://github.com/nodejs/node/pull/31735)
* \[[`c222d561c6`](https://github.com/nodejs/node/commit/c222d561c6)] - **meta**: move princejwesley to emeritus (Rich Trott) [#31730](https://github.com/nodejs/node/pull/31730)
* \[[`3e7e9fdca9`](https://github.com/nodejs/node/commit/3e7e9fdca9)] - **meta**: move vkurchatkin to emeritus (Rich Trott) [#31729](https://github.com/nodejs/node/pull/31729)
* \[[`ca52b5b1e3`](https://github.com/nodejs/node/commit/ca52b5b1e3)] - **meta**: move calvinmetcalf to emeritus (Rich Trott) [#31736](https://github.com/nodejs/node/pull/31736)
* \[[`c892d410bb`](https://github.com/nodejs/node/commit/c892d410bb)] - **meta**: fix collaborator list errors in README.md (James M Snell) [#31655](https://github.com/nodejs/node/pull/31655)
* \[[`62b5bd4ca0`](https://github.com/nodejs/node/commit/62b5bd4ca0)] - **module**: add hook for global preload code (Jan Krems) [#32068](https://github.com/nodejs/node/pull/32068)
* \[[`c537afb18c`](https://github.com/nodejs/node/commit/c537afb18c)] - **module**: package "exports" error refinements (Guy Bedford) [#31625](https://github.com/nodejs/node/pull/31625)
* \[[`4ee41c572c`](https://github.com/nodejs/node/commit/4ee41c572c)] - **module**: drop support for extensionless main entry points in esm (Geoffrey Booth) [#31415](https://github.com/nodejs/node/pull/31415)
* \[[`08e09eca34`](https://github.com/nodejs/node/commit/08e09eca34)] - **n-api**: free instance data as reference (Gabriel Schulhof) [#31638](https://github.com/nodejs/node/pull/31638)
* \[[`16c690373a`](https://github.com/nodejs/node/commit/16c690373a)] - **n-api**: rename 'promise' parameter to 'value' (Tobias Nießen) [#31544](https://github.com/nodejs/node/pull/31544)
* \[[`3a84634cc1`](https://github.com/nodejs/node/commit/3a84634cc1)] - **n-api**: return napi\_invalid\_arg on napi\_create\_bigint\_words (legendecas) [#31312](https://github.com/nodejs/node/pull/31312)
* \[[`0d30546329`](https://github.com/nodejs/node/commit/0d30546329)] - **net**: track state of setNoDelay() and prevent unnecessary system calls (Rusty Conover) [#31543](https://github.com/nodejs/node/pull/31543)
* \[[`87cfbb2da1`](https://github.com/nodejs/node/commit/87cfbb2da1)] - **report**: add support for Workers (Anna Henningsen) [#31386](https://github.com/nodejs/node/pull/31386)
* \[[`782f5dbddd`](https://github.com/nodejs/node/commit/782f5dbddd)] - **src**: add build Github Action (gengjiawen) [#31153](https://github.com/nodejs/node/pull/31153)
* \[[`fbd5be6734`](https://github.com/nodejs/node/commit/fbd5be6734)] - **src**: delete BaseObjectWeakPtr data when pointee is gone (Anna Henningsen) [#32393](https://github.com/nodejs/node/pull/32393)
* \[[`56a45095b7`](https://github.com/nodejs/node/commit/56a45095b7)] - **src**: harden running native `SetImmediate()`s slightly (Anna Henningsen) [#31468](https://github.com/nodejs/node/pull/31468)
* \[[`cb16aabd15`](https://github.com/nodejs/node/commit/cb16aabd15)] - **src**: simplify native immediate queue running (Anna Henningsen) [#31502](https://github.com/nodejs/node/pull/31502)
* \[[`c2176e15ea`](https://github.com/nodejs/node/commit/c2176e15ea)] - **src**: move MemoryInfo() for worker code to .cc files (Anna Henningsen) [#31386](https://github.com/nodejs/node/pull/31386)
* \[[`22bf867149`](https://github.com/nodejs/node/commit/22bf867149)] - **src**: add interrupts to Environments/Workers (Anna Henningsen) [#31386](https://github.com/nodejs/node/pull/31386)
* \[[`7c2c068aeb`](https://github.com/nodejs/node/commit/7c2c068aeb)] - **src**: remove AsyncRequest (Anna Henningsen) [#31386](https://github.com/nodejs/node/pull/31386)
* \[[`748a530046`](https://github.com/nodejs/node/commit/748a530046)] - **src**: add a threadsafe variant of SetImmediate() (Anna Henningsen) [#31386](https://github.com/nodejs/node/pull/31386)
* \[[`aafb224147`](https://github.com/nodejs/node/commit/aafb224147)] - **src**: exclude C++ SetImmediate() from count (Anna Henningsen) [#31386](https://github.com/nodejs/node/pull/31386)
* \[[`5df969826d`](https://github.com/nodejs/node/commit/5df969826d)] - **src**: better encapsulate native immediate list (Anna Henningsen) [#31386](https://github.com/nodejs/node/pull/31386)
* \[[`2625244111`](https://github.com/nodejs/node/commit/2625244111)] - **src**: run native immediates during Environment cleanup (Anna Henningsen) [#30666](https://github.com/nodejs/node/pull/30666)
* \[[`5b65348fed`](https://github.com/nodejs/node/commit/5b65348fed)] - **src**: no SetImmediate from destructor in stream\_pipe code (Anna Henningsen) [#30666](https://github.com/nodejs/node/pull/30666)
* \[[`51230f71ff`](https://github.com/nodejs/node/commit/51230f71ff)] - **src**: add more `can\_call\_into\_js()` guards (Anna Henningsen) [#30666](https://github.com/nodejs/node/pull/30666)
* \[[`7647bfe3fc`](https://github.com/nodejs/node/commit/7647bfe3fc)] - **src**: keep object alive in stream\_pipe code (Anna Henningsen) [#30666](https://github.com/nodejs/node/pull/30666)
* \[[`5f95e69f4d`](https://github.com/nodejs/node/commit/5f95e69f4d)] - **src**: remove HandleWrap instances from list once closed (Anna Henningsen) [#30374](https://github.com/nodejs/node/pull/30374)
* \[[`e17d314a21`](https://github.com/nodejs/node/commit/e17d314a21)] - **src**: remove keep alive option from SetImmediate() (Anna Henningsen) [#30374](https://github.com/nodejs/node/pull/30374)
* \[[`6db84d3e50`](https://github.com/nodejs/node/commit/6db84d3e50)] - **src**: use BaseObjectPtr for keeping channel alive in dns bindings (Anna Henningsen) [#30374](https://github.com/nodejs/node/pull/30374)
* \[[`c60780ff52`](https://github.com/nodejs/node/commit/c60780ff52)] - **src**: introduce custom smart pointers for `BaseObject`s (Anna Henningsen) [#30374](https://github.com/nodejs/node/pull/30374)
* \[[`17e10dd3cb`](https://github.com/nodejs/node/commit/17e10dd3cb)] - **src**: use C++ style for struct with initializers (Sam Roberts) [#32134](https://github.com/nodejs/node/pull/32134)
* \[[`b5c6230258`](https://github.com/nodejs/node/commit/b5c6230258)] - **src**: implement per-process native Debug() printer (Joyee Cheung) [#31884](https://github.com/nodejs/node/pull/31884)
* \[[`b95e8eafa5`](https://github.com/nodejs/node/commit/b95e8eafa5)] - **src**: refactor debug category parsing (Joyee Cheung) [#31884](https://github.com/nodejs/node/pull/31884)
* \[[`19f3c0adc2`](https://github.com/nodejs/node/commit/19f3c0adc2)] - **src**: make aliased\_buffer.h self-contained (Joyee Cheung) [#31884](https://github.com/nodejs/node/pull/31884)
* \[[`908f634110`](https://github.com/nodejs/node/commit/908f634110)] - **src**: discard tasks posted to platform TaskRunner during shutdown (Anna Henningsen) [#31853](https://github.com/nodejs/node/pull/31853)
* \[[`808379c379`](https://github.com/nodejs/node/commit/808379c379)] - **src**: Handle bad callback in asyc\_wrap (Harshitha KP) [#31946](https://github.com/nodejs/node/pull/31946)
* \[[`a6a41f4c77`](https://github.com/nodejs/node/commit/a6a41f4c77)] - **src**: add node\_crypto\_common and refactor (James M Snell) [#32016](https://github.com/nodejs/node/pull/32016)
* \[[`0b327bd81d`](https://github.com/nodejs/node/commit/0b327bd81d)] - **src**: enable `StreamPipe` for generic `StreamBase`s (Anna Henningsen) [#31869](https://github.com/nodejs/node/pull/31869)
* \[[`bd92243ddf`](https://github.com/nodejs/node/commit/bd92243ddf)] - **src**: elevate v8 namespaces (Harshitha KP) [#31901](https://github.com/nodejs/node/pull/31901)
* \[[`3b2bbbdeca`](https://github.com/nodejs/node/commit/3b2bbbdeca)] - **src**: allow unique\_ptrs with custom deleter in memory tracker (Anna Henningsen) [#31870](https://github.com/nodejs/node/pull/31870)
* \[[`9ab4a7e5ce`](https://github.com/nodejs/node/commit/9ab4a7e5ce)] - **src**: move BaseObject subclass dtors/ctors out of node\_crypto.h (Anna Henningsen) [#31872](https://github.com/nodejs/node/pull/31872)
* \[[`041408d513`](https://github.com/nodejs/node/commit/041408d513)] - **src**: don't run bootstrapper in CreateEnvironment (Shelley Vohr) [#31910](https://github.com/nodejs/node/pull/31910)
* \[[`e6debf5c25`](https://github.com/nodejs/node/commit/e6debf5c25)] - **src**: prefer 3-argument Array::New() (Anna Henningsen) [#31775](https://github.com/nodejs/node/pull/31775)
* \[[`98640f7a6d`](https://github.com/nodejs/node/commit/98640f7a6d)] - **src**: use hex not decimal in IsArrayIndex (Shelley Vohr) [#31758](https://github.com/nodejs/node/pull/31758)
* \[[`75971009d0`](https://github.com/nodejs/node/commit/75971009d0)] - **src**: wrap HostPort in ExclusiveAccess (Ben Noordhuis) [#31717](https://github.com/nodejs/node/pull/31717)
* \[[`01da65644e`](https://github.com/nodejs/node/commit/01da65644e)] - **src**: add ExclusiveAccess class (Ben Noordhuis) [#31717](https://github.com/nodejs/node/pull/31717)
* \[[`28289eaeb8`](https://github.com/nodejs/node/commit/28289eaeb8)] - **src**: allow to reuse env options handling (Denys Otrishko) [#31711](https://github.com/nodejs/node/pull/31711)
* \[[`249a0fe61d`](https://github.com/nodejs/node/commit/249a0fe61d)] - **src**: fix compile warnings in node\_url.cc (Anna Henningsen) [#31689](https://github.com/nodejs/node/pull/31689)
* \[[`bf729d02b7`](https://github.com/nodejs/node/commit/bf729d02b7)] - **src**: modernized unique\_ptr construction (Yuhanun Citgez) [#31654](https://github.com/nodejs/node/pull/31654)
* \[[`6e3e158f51`](https://github.com/nodejs/node/commit/6e3e158f51)] - **src**: remove dead code in InternalMakeCallback (Gerhard Stoebich) [#31622](https://github.com/nodejs/node/pull/31622)
* \[[`c34672a3b0`](https://github.com/nodejs/node/commit/c34672a3b0)] - **src**: remove fixed-size GetHumanReadableProcessName (Ben Noordhuis) [#31633](https://github.com/nodejs/node/pull/31633)
* \[[`57d1d73b47`](https://github.com/nodejs/node/commit/57d1d73b47)] - **src**: fix OOB reads in process.title getter (Ben Noordhuis) [#31633](https://github.com/nodejs/node/pull/31633)
* \[[`5e68a13d53`](https://github.com/nodejs/node/commit/5e68a13d53)] - **src**: various minor improvements to node\_url (James M Snell) [#31651](https://github.com/nodejs/node/pull/31651)
* \[[`2cdd57ab67`](https://github.com/nodejs/node/commit/2cdd57ab67)] - **src**: fix inspecting `MessagePort` from `init` async hook (Anna Henningsen) [#31600](https://github.com/nodejs/node/pull/31600)
* \[[`753db6aee2`](https://github.com/nodejs/node/commit/753db6aee2)] - **src**: remove unused `Worker::child\_port\_` member (Anna Henningsen) [#31599](https://github.com/nodejs/node/pull/31599)
* \[[`7e52e39385`](https://github.com/nodejs/node/commit/7e52e39385)] - **src**: change Fill() to use ParseArrayIndex() (ConorDavenport) [#31591](https://github.com/nodejs/node/pull/31591)
* \[[`79a6872809`](https://github.com/nodejs/node/commit/79a6872809)] - **src**: remove duplicate field env in CryptoJob class (ConorDavenport) [#31554](https://github.com/nodejs/node/pull/31554)
* \[[`5e19c4a9d4`](https://github.com/nodejs/node/commit/5e19c4a9d4)] - **src**: fix console debug output on Windows (Denys Otrishko) [#31580](https://github.com/nodejs/node/pull/31580)
* \[[`9c9dc4b184`](https://github.com/nodejs/node/commit/9c9dc4b184)] - **src**: remove preview for heap dump utilities (Anna Henningsen) [#31570](https://github.com/nodejs/node/pull/31570)
* \[[`91dd1018ac`](https://github.com/nodejs/node/commit/91dd1018ac)] - **src**: fix debug crash handling null strings (Rusty Conover) [#31523](https://github.com/nodejs/node/pull/31523)
* \[[`fb32043e6a`](https://github.com/nodejs/node/commit/fb32043e6a)] - **src**: define noreturn attribute for windows (Alexander Smarus) [#31467](https://github.com/nodejs/node/pull/31467)
* \[[`ce6b9d15d2`](https://github.com/nodejs/node/commit/ce6b9d15d2)] - **src**: reduce code duplication in BootstrapNode (Denys Otrishko) [#31465](https://github.com/nodejs/node/pull/31465)
* \[[`a309af0f52`](https://github.com/nodejs/node/commit/a309af0f52)] - **src**: use custom fprintf alike to write errors to stderr (Anna Henningsen) [#31446](https://github.com/nodejs/node/pull/31446)
* \[[`7bdd29fa21`](https://github.com/nodejs/node/commit/7bdd29fa21)] - **src**: add C++-style sprintf utility (Anna Henningsen) [#31446](https://github.com/nodejs/node/pull/31446)
* \[[`8f88d62a31`](https://github.com/nodejs/node/commit/8f88d62a31)] - **src**: reduce large pages code duplication (Gabriel Schulhof) [#31385](https://github.com/nodejs/node/pull/31385)
* \[[`de6d5523a1`](https://github.com/nodejs/node/commit/de6d5523a1)] - **src**: fix ignore GCC -Wcast-function-type for older compilers (Denys Otrishko) [#31524](https://github.com/nodejs/node/pull/31524)
* \[[`a8d9c0f8b6`](https://github.com/nodejs/node/commit/a8d9c0f8b6)] - **src**: ignore GCC -Wcast-function-type for v8.h (Daniel Bevenius) [#31475](https://github.com/nodejs/node/pull/31475)
* \[[`a2f1825cb5`](https://github.com/nodejs/node/commit/a2f1825cb5)] - **src**: fix performance regression in node\_file.cc (Ben Noordhuis) [#31343](https://github.com/nodejs/node/pull/31343)
* \[[`1d075cfd7f`](https://github.com/nodejs/node/commit/1d075cfd7f)] - **src**: use uv\_guess\_handle() to detect TTYs (Colin Ihrig) [#31333](https://github.com/nodejs/node/pull/31333)
* \[[`21bcc96f92`](https://github.com/nodejs/node/commit/21bcc96f92)] - **src**: include uv.h in node\_binding header (Shelley Vohr) [#31265](https://github.com/nodejs/node/pull/31265)
* \[[`d77a1b088f`](https://github.com/nodejs/node/commit/d77a1b088f)] - **src**: remove node::InitializeV8Platform() (Ben Noordhuis) [#31245](https://github.com/nodejs/node/pull/31245)
* \[[`fe1ac496f7`](https://github.com/nodejs/node/commit/fe1ac496f7)] - **src**: remove uses of node::InitializeV8Platform() (Ben Noordhuis) [#31245](https://github.com/nodejs/node/pull/31245)
* \[[`8aa7bf2d23`](https://github.com/nodejs/node/commit/8aa7bf2d23)] - **src**: clean up large\_pages code (Denys Otrishko) [#31196](https://github.com/nodejs/node/pull/31196)
* \[[`12253f8c74`](https://github.com/nodejs/node/commit/12253f8c74)] - **stream**: sync stream unpipe resume (Robert Nagy) [#31191](https://github.com/nodejs/node/pull/31191)
* \[[`6e76752a7b`](https://github.com/nodejs/node/commit/6e76752a7b)] - **stream**: simplify push (Robert Nagy) [#31150](https://github.com/nodejs/node/pull/31150)
* \[[`8973209ad0`](https://github.com/nodejs/node/commit/8973209ad0)] - **stream**: clean up definition using defineProperties (antsmartian) [#31236](https://github.com/nodejs/node/pull/31236)
* \[[`a987972bde`](https://github.com/nodejs/node/commit/a987972bde)] - **stream**: replace Function.prototype with primordial (Sebastien Ahkrin) [#31204](https://github.com/nodejs/node/pull/31204)
* \[[`e685f12ee6`](https://github.com/nodejs/node/commit/e685f12ee6)] - **test**: restore --jitless test on AIX (Richard Lau) [#32619](https://github.com/nodejs/node/pull/32619)
* \[[`eee587b847`](https://github.com/nodejs/node/commit/eee587b847)] - **test**: fix test-http2-reset-flood flakiness (Anna Henningsen) [#32607](https://github.com/nodejs/node/pull/32607)
* \[[`d568efcd22`](https://github.com/nodejs/node/commit/d568efcd22)] - **test**: refactor common.expectsError (Ruben Bridgewater) [#31092](https://github.com/nodejs/node/pull/31092)
* \[[`e4f9360287`](https://github.com/nodejs/node/commit/e4f9360287)] - **test**: mark test-http2-reset-flood flaky on bsd (Myles Borins) [#32595](https://github.com/nodejs/node/pull/32595)
* \[[`6f50b60018`](https://github.com/nodejs/node/commit/6f50b60018)] - **test**: add test-worker-prof to the SLOW list for debug (Myles Borins) [#32589](https://github.com/nodejs/node/pull/32589)
* \[[`7123c0f042`](https://github.com/nodejs/node/commit/7123c0f042)] - **test**: always skip vm-timeout-escape-queuemicrotask (Denys Otrishko) [#31980](https://github.com/nodejs/node/pull/31980)
* \[[`bb947ce3c2`](https://github.com/nodejs/node/commit/bb947ce3c2)] - **test**: improve test-debug-usage (Rich Trott) [#32141](https://github.com/nodejs/node/pull/32141)
* \[[`7c8a7b4c7d`](https://github.com/nodejs/node/commit/7c8a7b4c7d)] - **test**: end tls connection with some data (Sam Roberts) [#32328](https://github.com/nodejs/node/pull/32328)
* \[[`f4bd01c816`](https://github.com/nodejs/node/commit/f4bd01c816)] - **test**: discard data received by client (Hassaan Pasha) [#32328](https://github.com/nodejs/node/pull/32328)
* \[[`7a14ddf104`](https://github.com/nodejs/node/commit/7a14ddf104)] - **test**: increase test timeout to prevent flakiness (Ruben Bridgewater) [#31716](https://github.com/nodejs/node/pull/31716)
* \[[`147045716b`](https://github.com/nodejs/node/commit/147045716b)] - **test**: use index.js if package.json "main" is empty (Ben Noordhuis) [#32040](https://github.com/nodejs/node/pull/32040)
* \[[`03aa2e1b7b`](https://github.com/nodejs/node/commit/03aa2e1b7b)] - **test**: changed function to arrow function (ProdipRoy89) [#32045](https://github.com/nodejs/node/pull/32045)
* \[[`b4c407fecc`](https://github.com/nodejs/node/commit/b4c407fecc)] - **test**: allow EAI\_FAIL in test-net-dns-error.js (Vita Batrla) [#31780](https://github.com/nodejs/node/pull/31780)
* \[[`2582083f63`](https://github.com/nodejs/node/commit/2582083f63)] - **test**: remove superfluous checks in test-net-reconnect-error (Rich Trott) [#32120](https://github.com/nodejs/node/pull/32120)
* \[[`f365e5c262`](https://github.com/nodejs/node/commit/f365e5c262)] - **test**: apply camelCase in test-net-reconnect-error (Rich Trott) [#32120](https://github.com/nodejs/node/pull/32120)
* \[[`256bc4412c`](https://github.com/nodejs/node/commit/256bc4412c)] - **test**: update tests for larger Buffers (Jakob Kummerow) [#32114](https://github.com/nodejs/node/pull/32114)
* \[[`96c7226897`](https://github.com/nodejs/node/commit/96c7226897)] - **test**: remove common.port from test-tls-securepair-client (Rich Trott) [#32024](https://github.com/nodejs/node/pull/32024)
* \[[`1318662ff7`](https://github.com/nodejs/node/commit/1318662ff7)] - **test**: add WASI test for path\_link() (Colin Ihrig) [#32132](https://github.com/nodejs/node/pull/32132)
* \[[`55214628af`](https://github.com/nodejs/node/commit/55214628af)] - **test**: move test-inspector-module to parallel (Rich Trott) [#32025](https://github.com/nodejs/node/pull/32025)
* \[[`3574116887`](https://github.com/nodejs/node/commit/3574116887)] - **test**: fix flaky test-dns-any.js (Rich Trott) [#32017](https://github.com/nodejs/node/pull/32017)
* \[[`d62538404e`](https://github.com/nodejs/node/commit/d62538404e)] - **test**: fix flaky test-gc-net-timeout (Robert Nagy) [#31918](https://github.com/nodejs/node/pull/31918)
* \[[`2bf9a2d84c`](https://github.com/nodejs/node/commit/2bf9a2d84c)] - **test**: change test to not be sensitive to buffer send size (Rusty Conover) [#31499](https://github.com/nodejs/node/pull/31499)
* \[[`b1cf56f5db`](https://github.com/nodejs/node/commit/b1cf56f5db)] - **test**: remove sequential/test-https-keep-alive-large-write.js (Rusty Conover) [#31499](https://github.com/nodejs/node/pull/31499)
* \[[`67c3a95f7d`](https://github.com/nodejs/node/commit/67c3a95f7d)] - **test**: validate common property usage (Denys Otrishko) [#31933](https://github.com/nodejs/node/pull/31933)
* \[[`26d9f4c160`](https://github.com/nodejs/node/commit/26d9f4c160)] - **test**: fix usage of invalid common properties (Denys Otrishko) [#31933](https://github.com/nodejs/node/pull/31933)
* \[[`086e14d251`](https://github.com/nodejs/node/commit/086e14d251)] - **test**: increase timeout in vm-timeout-escape-queuemicrotask (Denys Otrishko) [#31966](https://github.com/nodejs/node/pull/31966)
* \[[`c2ffef8678`](https://github.com/nodejs/node/commit/c2ffef8678)] - **test**: add documentation for common.enoughTestCpu (Rich Trott) [#31931](https://github.com/nodejs/node/pull/31931)
* \[[`0c6fdfc4ac`](https://github.com/nodejs/node/commit/0c6fdfc4ac)] - **test**: fix typo in common/index.js (Rich Trott) [#31931](https://github.com/nodejs/node/pull/31931)
* \[[`3deee057b3`](https://github.com/nodejs/node/commit/3deee057b3)] - **test**: remove common.PORT from assorted pummel tests (Rich Trott) [#31897](https://github.com/nodejs/node/pull/31897)
* \[[`bde5a9bda8`](https://github.com/nodejs/node/commit/bde5a9bda8)] - **test**: remove flaky designation for test-net-connect-options-port (Rich Trott) [#31841](https://github.com/nodejs/node/pull/31841)
* \[[`c386f7568c`](https://github.com/nodejs/node/commit/c386f7568c)] - **test**: remove common.PORT from test-net-write-callbacks.js (Rich Trott) [#31839](https://github.com/nodejs/node/pull/31839)
* \[[`709256346c`](https://github.com/nodejs/node/commit/709256346c)] - **test**: remove common.PORT from test-net-pause (Rich Trott) [#31749](https://github.com/nodejs/node/pull/31749)
* \[[`61de609ac8`](https://github.com/nodejs/node/commit/61de609ac8)] - **test**: remove common.PORT from test-tls-server-large-request (Rich Trott) [#31749](https://github.com/nodejs/node/pull/31749)
* \[[`33d3cccb98`](https://github.com/nodejs/node/commit/33d3cccb98)] - **test**: remove common.PORT from test-net-throttle (Rich Trott) [#31749](https://github.com/nodejs/node/pull/31749)
* \[[`d172cc1474`](https://github.com/nodejs/node/commit/d172cc1474)] - **test**: remove common.PORT from test-net-timeout (Rich Trott) [#31749](https://github.com/nodejs/node/pull/31749)
* \[[`1109124313`](https://github.com/nodejs/node/commit/1109124313)] - **test**: add known issue test for sync writable callback (James M Snell) [#31756](https://github.com/nodejs/node/pull/31756)
* \[[`aa5afd013b`](https://github.com/nodejs/node/commit/aa5afd013b)] - **test**: mark test-fs-stat-bigint flaky on FreeBSD (Rich Trott) [#31728](https://github.com/nodejs/node/pull/31728)
* \[[`3f43c5f508`](https://github.com/nodejs/node/commit/3f43c5f508)] - **test**: improve test-fs-stat-bigint (Rich Trott) [#31726](https://github.com/nodejs/node/pull/31726)
* \[[`3f6805f0e7`](https://github.com/nodejs/node/commit/3f6805f0e7)] - **test**: remove flaky designation for test-fs-stat-bigint (Rich Trott) [#30437](https://github.com/nodejs/node/pull/30437)
* \[[`7d71465194`](https://github.com/nodejs/node/commit/7d71465194)] - **test**: fix flaky test-fs-stat-bigint (Duncan Healy) [#30437](https://github.com/nodejs/node/pull/30437)
* \[[`ca6fce0cbb`](https://github.com/nodejs/node/commit/ca6fce0cbb)] - **test**: add debugging output to test-net-listen-after-destroy-stdin (Rich Trott) [#31698](https://github.com/nodejs/node/pull/31698)
* \[[`59eba1177b`](https://github.com/nodejs/node/commit/59eba1177b)] - **test**: improve assertion message in test-dns-any (Rich Trott) [#31697](https://github.com/nodejs/node/pull/31697)
* \[[`61e534baa0`](https://github.com/nodejs/node/commit/61e534baa0)] - **test**: stricter assert color test (Ruben Bridgewater) [#31429](https://github.com/nodejs/node/pull/31429)
* \[[`bdd1133451`](https://github.com/nodejs/node/commit/bdd1133451)] - **test**: fix test-benchmark-http (Rich Trott) [#31686](https://github.com/nodejs/node/pull/31686)
* \[[`795a21d53a`](https://github.com/nodejs/node/commit/795a21d53a)] - **test**: fix flaky test-inspector-connect-main-thread (Anna Henningsen) [#31637](https://github.com/nodejs/node/pull/31637)
* \[[`297fb67304`](https://github.com/nodejs/node/commit/297fb67304)] - **test**: add test-dns-promises-lookupService (Rich Trott) [#31640](https://github.com/nodejs/node/pull/31640)
* \[[`02c2396976`](https://github.com/nodejs/node/commit/02c2396976)] - **test**: fix flaky test-http2-stream-destroy-event-order (Anna Henningsen) [#31610](https://github.com/nodejs/node/pull/31610)
* \[[`d2fbe80a4a`](https://github.com/nodejs/node/commit/d2fbe80a4a)] - **test**: unset NODE\_OPTIONS for cctest (Anna Henningsen) [#31594](https://github.com/nodejs/node/pull/31594)
* \[[`944f1a345a`](https://github.com/nodejs/node/commit/944f1a345a)] - **test**: simplify test-https-simple.js (Sam Roberts) [#31584](https://github.com/nodejs/node/pull/31584)
* \[[`0eb2dbb24e`](https://github.com/nodejs/node/commit/0eb2dbb24e)] - **test**: mark additional tests as flaky on Windows (Anna Henningsen) [#31606](https://github.com/nodejs/node/pull/31606)
* \[[`0bc3bd7c11`](https://github.com/nodejs/node/commit/0bc3bd7c11)] - **test**: remove --experimental-worker flag comment (Harshitha KP) [#31563](https://github.com/nodejs/node/pull/31563)
* \[[`baa14c9e39`](https://github.com/nodejs/node/commit/baa14c9e39)] - **test**: make test-http2-buffersize more correct (Anna Henningsen) [#31502](https://github.com/nodejs/node/pull/31502)
* \[[`e3e056d5cd`](https://github.com/nodejs/node/commit/e3e056d5cd)] - **test**: fix test-heapdump-worker (Anna Henningsen) [#31494](https://github.com/nodejs/node/pull/31494)
* \[[`48f4212286`](https://github.com/nodejs/node/commit/48f4212286)] - **test**: add tests for main() argument handling (Colin Ihrig) [#31426](https://github.com/nodejs/node/pull/31426)
* \[[`dbe2d85f66`](https://github.com/nodejs/node/commit/dbe2d85f66)] - **test**: add wasi test for freopen() (Colin Ihrig) [#31432](https://github.com/nodejs/node/pull/31432)
* \[[`a8e2f405f2`](https://github.com/nodejs/node/commit/a8e2f405f2)] - **test**: remove bluebird remnants from test fixture (Rich Trott) [#31435](https://github.com/nodejs/node/pull/31435)
* \[[`8438d1498d`](https://github.com/nodejs/node/commit/8438d1498d)] - **test**: improve wasi stat test (Colin Ihrig) [#31413](https://github.com/nodejs/node/pull/31413)
* \[[`596920dbf4`](https://github.com/nodejs/node/commit/596920dbf4)] - **test**: add wasi test for symlink() and readlink() (Colin Ihrig) [#31403](https://github.com/nodejs/node/pull/31403)
* \[[`2750e65f5c`](https://github.com/nodejs/node/commit/2750e65f5c)] - **test**: update postmortem test with v12 constants (Matheus Marchini) [#31391](https://github.com/nodejs/node/pull/31391)
* \[[`642f8c0eb9`](https://github.com/nodejs/node/commit/642f8c0eb9)] - **test**: export public symbols in addons tests (Ben Noordhuis) [#28717](https://github.com/nodejs/node/pull/28717)
* \[[`20167fec5f`](https://github.com/nodejs/node/commit/20167fec5f)] - **test**: stricten readline keypress failure test condition (Ruben Bridgewater) [#31300](https://github.com/nodejs/node/pull/31300)
* \[[`c719f7ab36`](https://github.com/nodejs/node/commit/c719f7ab36)] - **test**: allow disabling crypto tests (Shelley Vohr) [#31268](https://github.com/nodejs/node/pull/31268)
* \[[`31a13dc3a4`](https://github.com/nodejs/node/commit/31a13dc3a4)] - **test**: fix recursive rm test to actually use tmpdir (Denys Otrishko) [#31250](https://github.com/nodejs/node/pull/31250)
* \[[`320ac13452`](https://github.com/nodejs/node/commit/320ac13452)] - **test**: remove unused symlink loop (Colin Ihrig) [#31267](https://github.com/nodejs/node/pull/31267)
* \[[`f3af68ea80`](https://github.com/nodejs/node/commit/f3af68ea80)] - **test**: prefer server over srv (Andrew Hughes) [#31224](https://github.com/nodejs/node/pull/31224)
* \[[`04e2f41783`](https://github.com/nodejs/node/commit/04e2f41783)] - **test**: fix unit test logging with python3 (Adam Majer) [#31156](https://github.com/nodejs/node/pull/31156)
* \[[`5a537babe1`](https://github.com/nodejs/node/commit/5a537babe1)] - **test**: mark empty udp tests flaky on OS X (Sam Roberts) [#32146](https://github.com/nodejs/node/pull/32146)
* \[[`99cfab2594`](https://github.com/nodejs/node/commit/99cfab2594)] - **test,dns**: add coverage for dns exception (Rich Trott) [#31678](https://github.com/nodejs/node/pull/31678)
* \[[`54395c60eb`](https://github.com/nodejs/node/commit/54395c60eb)] - **tls**: reduce memory copying and number of BIO buffer allocations (Rusty Conover) [#31499](https://github.com/nodejs/node/pull/31499)
* \[[`4f177c4f63`](https://github.com/nodejs/node/commit/4f177c4f63)] - **tls**: simplify errors using ThrowCryptoError (Tobias Nießen) [#31436](https://github.com/nodejs/node/pull/31436)
* \[[`c0e6e60cb1`](https://github.com/nodejs/node/commit/c0e6e60cb1)] - **tools**: update minimist\@1.2.5 (Rich Trott) [#32274](https://github.com/nodejs/node/pull/32274)
* \[[`dca3d298dd`](https://github.com/nodejs/node/commit/dca3d298dd)] - **tools**: update icu to 65.1 (Albert Wang) [#30232](https://github.com/nodejs/node/pull/30232)
* \[[`d57719098c`](https://github.com/nodejs/node/commit/d57719098c)] - **tools**: only fetch previous versions when necessary (Richard Lau) [#32518](https://github.com/nodejs/node/pull/32518)
* \[[`61d54e7716`](https://github.com/nodejs/node/commit/61d54e7716)] - **tools**: use per-process native Debug() printer in mkcodecache (Joyee Cheung) [#31884](https://github.com/nodejs/node/pull/31884)
* \[[`1060a2bba9`](https://github.com/nodejs/node/commit/1060a2bba9)] - **tools**: add NODE\_TEST\_NO\_INTERNET to the doc builder (Joyee Cheung) [#31849](https://github.com/nodejs/node/pull/31849)
* \[[`aa8a435e17`](https://github.com/nodejs/node/commit/aa8a435e17)] - **tools**: sync gyp code base with node-gyp repo (Michaël Zasso) [#30563](https://github.com/nodejs/node/pull/30563)
* \[[`6b1a5518e0`](https://github.com/nodejs/node/commit/6b1a5518e0)] - **tools**: update lint-md task to lint for possessives of Node.js (Rich Trott) [#31862](https://github.com/nodejs/node/pull/31862)
* \[[`b657df4759`](https://github.com/nodejs/node/commit/b657df4759)] - **tools**: update Markdown linter to be cross-platform (Derek Lewis) [#31239](https://github.com/nodejs/node/pull/31239)
* \[[`289f3dc538`](https://github.com/nodejs/node/commit/289f3dc538)] - **tools**: replace deprecated iteritems() for items() (Giovanny Andres Gongora Granada (Gioyik)) [#31528](https://github.com/nodejs/node/pull/31528)
* \[[`77e6700b03`](https://github.com/nodejs/node/commit/77e6700b03)] - **tools**: remove obsolete dependencies (Rich Trott) [#31359](https://github.com/nodejs/node/pull/31359)
* \[[`c7b1f1df3b`](https://github.com/nodejs/node/commit/c7b1f1df3b)] - **tools**: update remark-preset-lint-node to 1.12.0 (Rich Trott) [#31359](https://github.com/nodejs/node/pull/31359)
* \[[`20f857fa01`](https://github.com/nodejs/node/commit/20f857fa01)] - **tools**: update JSON header parsing for backticks (Rich Trott) [#31294](https://github.com/nodejs/node/pull/31294)
* \[[`0f4a9e26ef`](https://github.com/nodejs/node/commit/0f4a9e26ef)] - **tools**: ensure consistent perms of signed release files (Rod Vagg) [#29350](https://github.com/nodejs/node/pull/29350)
* \[[`6f71efa0ed`](https://github.com/nodejs/node/commit/6f71efa0ed)] - **tools**: add clang-tidy rule in src (gengjiawen) [#26840](https://github.com/nodejs/node/pull/26840)
* \[[`3a1566a267`](https://github.com/nodejs/node/commit/3a1566a267)] - **tools**: unify make-v8.sh for ppc64le and s390x (Richard Lau) [#31628](https://github.com/nodejs/node/pull/31628)
* \[[`fbc0bd95ec`](https://github.com/nodejs/node/commit/fbc0bd95ec)] - **tty**: do not end in an infinite warning recursion (Ruben Bridgewater) [#31429](https://github.com/nodejs/node/pull/31429)
* \[[`32c0449141`](https://github.com/nodejs/node/commit/32c0449141)] - **(SEMVER-MINOR)** **util**: use a global symbol for `util.promisify.custom` (ExE Boss) [#31672](https://github.com/nodejs/node/pull/31672)
* \[[`f4e5404b5d`](https://github.com/nodejs/node/commit/f4e5404b5d)] - **util**: throw if unreachable TypedArray checking code is reached (Rich Trott) [#31737](https://github.com/nodejs/node/pull/31737)
* \[[`785417aeda`](https://github.com/nodejs/node/commit/785417aeda)] - **util**: add coverage for util.inspect.colors alias setter (Rich Trott) [#31743](https://github.com/nodejs/node/pull/31743)
* \[[`c9fa2d1fbf`](https://github.com/nodejs/node/commit/c9fa2d1fbf)] - **util**: throw if unreachable code is reached (Rich Trott) [#31712](https://github.com/nodejs/node/pull/31712)
* \[[`51d8fbf31f`](https://github.com/nodejs/node/commit/51d8fbf31f)] - **util**: fix inspection of typed arrays with unusual length (Ruben Bridgewater) [#31458](https://github.com/nodejs/node/pull/31458)
* \[[`f068788f59`](https://github.com/nodejs/node/commit/f068788f59)] - **util**: add colors to debuglog() (Ruben Bridgewater) [#30930](https://github.com/nodejs/node/pull/30930)
* \[[`a91a824108`](https://github.com/nodejs/node/commit/a91a824108)] - **wasi**: improve use of primordials (Colin Ihrig) [#31212](https://github.com/nodejs/node/pull/31212)
* \[[`2029c10196`](https://github.com/nodejs/node/commit/2029c10196)] - **win**: change to use Python in install tool (gengjiawen) [#31221](https://github.com/nodejs/node/pull/31221)
* \[[`c5de212039`](https://github.com/nodejs/node/commit/c5de212039)] - **worker**: move JoinThread() back into exit callback (Anna Henningsen) [#31468](https://github.com/nodejs/node/pull/31468)
* \[[`65729f966e`](https://github.com/nodejs/node/commit/65729f966e)] - **worker**: emit runtime error on loop creation failure (Harshitha KP) [#31621](https://github.com/nodejs/node/pull/31621)
* \[[`ea989e160e`](https://github.com/nodejs/node/commit/ea989e160e)] - **worker**: unroll file extension regexp (Anna Henningsen) [#31779](https://github.com/nodejs/node/pull/31779)
* \[[`9f8d315a09`](https://github.com/nodejs/node/commit/9f8d315a09)] - **worker**: add support for .cjs extension (Antoine du HAMEL) [#31662](https://github.com/nodejs/node/pull/31662)
* \[[`30dbc84642`](https://github.com/nodejs/node/commit/30dbc84642)] - **worker**: properly handle env and NODE\_OPTIONS in workers (Denys Otrishko) [#31711](https://github.com/nodejs/node/pull/31711)
* \[[`0697f65f70`](https://github.com/nodejs/node/commit/0697f65f70)] - **worker**: reset `Isolate` stack limit after entering `Locker` (Anna Henningsen) [#31593](https://github.com/nodejs/node/pull/31593)
* \[[`5500521804`](https://github.com/nodejs/node/commit/5500521804)] - **worker**: remove redundant closing of child port (aaccttrr) [#31555](https://github.com/nodejs/node/pull/31555)

<a id="12.16.1"></a>

## 2020-02-18, Version 12.16.1 'Erbium' (LTS), @MylesBorins

### Notable changes

Node.js 12.16.0 included 6 regressions that are being fixed in this release

**Accidental Unflagging of Self Resolving Modules**:

12.16.0 included a large update to the ESM implementation. One of the new features,
Self Referential Modules, was accidentally released without requiring the `--experimental-modules`
flag. This release is being made to appropriately flag the feature.

**Process Cleanup Changed Introduced WASM-Related Assertion**:

A change during Node.js process cleanup led to a crash in combination with
specific usage of WASM. This has been fixed by partially reverted said change.
A regression test and a full fix are being worked on and will likely be included
in future 12.x and 13.x releases.

**Use Largepages Runtime Option Introduced Linking Failure**:

A Semver-Minor change to introduce `--use-largepages` as a runtime option
introduced a linking failure. This had been fixed in master but regressed as the fix has not yet gone out
in a Current release. The feature has been reverted, but will be able to reland with a fix in a future
Semver-Minor release.

**Async Hooks was Causing an Exception When Handling Errors**:

Changes in async hooks internals introduced a case where an internal api call could be called with undefined
causing a process to crash. The change to async hooks was reverted. A regression test and fix has been proposed
and the change could re-land in a future Semver-Patch release if the regression is reliably fixed.

**New Enumerable Read-Only Property on EventEmitter breaks @types/extend**

A new property for enumerating events was added to the EventEmitter class. This
broke existing code that was using the `@types/extend` module for extending classses
as `@types/extend` was attemping to write over the existing field which the new
change made read-only. As this is the first property on EventEmitter that is
read-only this feature could be considered Semver-Major. The new feature has been
reverted but could re-land in a future Semver-Minor release if a non breaking way
of applying it is found.

**Exceptions in the HTTP parser were not emitting an uncaughtException**

A refactoring to Node.js interanls resulted in a bug where errors in the HTTP
parser were not being emitted by `process.on('uncaughtException')` when the `async_hooks` `after`
hook exists. The fix to this bug has been included in this release.

### Commits

* \[[`51fdd759b9`](https://github.com/nodejs/node/commit/51fdd759b9)] - **async\_hooks**: ensure event after been emitted on runInAsyncScope (legendecas) [#31784](https://github.com/nodejs/node/pull/31784)
* \[[`7a1b0ac06f`](https://github.com/nodejs/node/commit/7a1b0ac06f)] - _**Revert**_ "**build**: re-introduce --use-largepages as no-op" (Myles Borins) [#31782](https://github.com/nodejs/node/pull/31782)
* \[[`a53eeca2a9`](https://github.com/nodejs/node/commit/a53eeca2a9)] - _**Revert**_ "**build**: switch realpath to pwd" (Myles Borins) [#31782](https://github.com/nodejs/node/pull/31782)
* \[[`6d432994e6`](https://github.com/nodejs/node/commit/6d432994e6)] - _**Revert**_ "**build**: warn upon --use-largepages config option" (Myles Borins) [#31782](https://github.com/nodejs/node/pull/31782)
* \[[`a5bc00af12`](https://github.com/nodejs/node/commit/a5bc00af12)] - _**Revert**_ "**events**: allow monitoring error events" (Myles Borins)
* \[[`f0b2d875d9`](https://github.com/nodejs/node/commit/f0b2d875d9)] - **module**: 12.x self resolve flag as experimental modules (Guy Bedford) [#31757](https://github.com/nodejs/node/pull/31757)
* \[[`42b68a4e24`](https://github.com/nodejs/node/commit/42b68a4e24)] - **src**: inform callback scopes about exceptions in HTTP parser (Anna Henningsen) [#31801](https://github.com/nodejs/node/pull/31801)
* \[[`065a32f064`](https://github.com/nodejs/node/commit/065a32f064)] - _**Revert**_ "**src**: make --use-largepages a runtime option" (Myles Borins) [#31782](https://github.com/nodejs/node/pull/31782)
* \[[`3d5beebc62`](https://github.com/nodejs/node/commit/3d5beebc62)] - _**Revert**_ "**src**: make large\_pages node.cc include conditional" (Myles Borins) [#31782](https://github.com/nodejs/node/pull/31782)
* \[[`43d02e20e0`](https://github.com/nodejs/node/commit/43d02e20e0)] - **src**: keep main-thread Isolate attached to platform during Dispose (Anna Henningsen) [#31795](https://github.com/nodejs/node/pull/31795)
* \[[`7a5954ef26`](https://github.com/nodejs/node/commit/7a5954ef26)] - **src**: fix -Winconsistent-missing-override warning (Colin Ihrig) [#30549](https://github.com/nodejs/node/pull/30549)

<a id="12.16.0"></a>

## 2020-02-11, Version 12.16.0 'Erbium' (LTS), @targos

### Notable changes

#### New assert APIs

The `assert` module now provides experimental `assert.match()` and
`assert.doesNotMatch()` methods. They will validate that the first argument is a
string and matches (or does not match) the provided regular expression:

```js
const assert = require('assert').strict;

assert.match('I will fail', /pass/);
// AssertionError [ERR_ASSERTION]: The input did not match the regular ...

assert.doesNotMatch('I will fail', /fail/);
// AssertionError [ERR_ASSERTION]: The input was expected to not match the ...
```

This is an experimental feature.

Ruben Bridgewater [#30929](https://github.com/nodejs/node/pull/30929).

#### Advanced serialization for IPC

The `child_process` and `cluster` modules now support a `serialization` option
to change the serialization mechanism used for IPC. The option can have one of
two values:

* `'json'` (default): `JSON.stringify()` and `JSON.parse()` are used. This is
  how message serialization was done before.
* `'advanced'`: The serialization API of the `v8` module is used. It is based on
  the [HTML structured clone algorithm](https://developer.mozilla.org/en-US/docs/Web/API/Web_Workers_API/Structured_clone_algorithm)
  and is able to serialize more built-in JavaScript object types, such as
  `BigInt`, `Map`, `Set` etc. as well as circular data structures.

Anna Henningsen [#30162](https://github.com/nodejs/node/pull/30162).

#### CLI flags

The new `--trace-exit` CLI flag makes Node.js print a stack trace whenever the
Node.js environment is exited proactively (i.e. by invoking the `process.exit()`
function or pressing Ctrl+C).

legendecas [#30516](https://github.com/nodejs/node/pull/30516).

***

The new `--trace-uncaught` CLI flag makes Node.js print a stack trace at the
time of throwing uncaught exceptions, rather than at the creation of the `Error`
object, if there is any.
This option is not enabled by default because it may affect garbage collection
behavior negatively.

Anna Henningsen [#30025](https://github.com/nodejs/node/pull/30025).

***

The `--disallow-code-generation-from-strings` V8 CLI flag is now whitelisted in
the `NODE_OPTIONS` environment variable.

Shelley Vohr [#30094](https://github.com/nodejs/node/pull/30094).

#### New crypto APIs

For DSA and ECDSA, a new signature encoding is now supported in addition to the
existing one (DER). The `verify` and `sign` methods accept a `dsaEncoding`
option, which can have one of two values:

* `'der'` (default): DER-encoded ASN.1 signature structure encoding `(r, s)`.
* `'ieee-p1363'`: Signature format `r || s` as proposed in IEEE-P1363.

Tobias Nießen [#29292](https://github.com/nodejs/node/pull/29292).

***

A new method was added to `Hash`: `Hash.prototype.copy`. It makes it possible to
clone the internal state of a `Hash` object into a new `Hash` object, allowing
to compute the digest between updates:

```js
// Calculate a rolling hash.
const crypto = require('crypto');
const hash = crypto.createHash('sha256');

hash.update('one');
console.log(hash.copy().digest('hex'));

hash.update('two');
console.log(hash.copy().digest('hex'));

hash.update('three');
console.log(hash.copy().digest('hex'));

// Etc.
```

Ben Noordhuis [#29910](https://github.com/nodejs/node/pull/29910).

#### Dependency updates

libuv was updated to 1.34.0. This includes fixes to `uv_fs_copyfile()` and
`uv_interface_addresses()` and adds two new functions: `uv_sleep()` and
`uv_fs_mkstemp()`.

Colin Ihrig [#30783](https://github.com/nodejs/node/pull/30783).

***

V8 was updated to 7.8.279.23. This includes performance improvements to object
destructuring, RegExp match failures and WebAssembly startup time.
The official release notes are available at <https://v8.dev/blog/v8-release-78>.

Michaël Zasso [#30109](https://github.com/nodejs/node/pull/30109).

#### New EventEmitter APIs

The new `EventEmitter.on` static method allows to async iterate over events:

```js
const { on, EventEmitter } = require('events');

(async () => {

  const ee = new EventEmitter();

  // Emit later on
  process.nextTick(() => {
    ee.emit('foo', 'bar');
    ee.emit('foo', 42);
  });

  for await (const event of on(ee, 'foo')) {
    // The execution of this inner block is synchronous and it
    // processes one event at a time (even with await). Do not use
    // if concurrent execution is required.
    console.log(event); // prints ['bar'] [42]
  }

})();
```

Matteo Collina [#27994](https://github.com/nodejs/node/pull/27994).

***

It is now possible to monitor `'error'` events on an `EventEmitter` without
consuming the emitted error by installing a listener using the symbol
`EventEmitter.errorMonitor`:

```js
const myEmitter = new MyEmitter();

myEmitter.on(EventEmitter.errorMonitor, (err) => {
  MyMonitoringTool.log(err);
});

myEmitter.emit('error', new Error('whoops!'));
// Still throws and crashes Node.js
```

Gerhard Stoebich [#30932](https://github.com/nodejs/node/pull/30932).

***

Using `async` functions with event handlers is problematic, because it
can lead to an unhandled rejection in case of a thrown exception:

```js
const ee = new EventEmitter();

ee.on('something', async (value) => {
  throw new Error('kaboom');
});
```

The experimental `captureRejections` option in the `EventEmitter` constructor or
the global setting change this behavior, installing a
`.then(undefined, handler)` handler on the `Promise`. This handler routes the
exception asynchronously to the `Symbol.for('nodejs.rejection')` method if there
is one, or to the `'error'` event handler if there is none.

```js
const ee1 = new EventEmitter({ captureRejections: true });
ee1.on('something', async (value) => {
  throw new Error('kaboom');
});

ee1.on('error', console.log);

const ee2 = new EventEmitter({ captureRejections: true });
ee2.on('something', async (value) => {
  throw new Error('kaboom');
});

ee2[Symbol.for('nodejs.rejection')] = console.log;
```

Setting `EventEmitter.captureRejections = true` will change the default for all
new instances of `EventEmitter`.

```js
EventEmitter.captureRejections = true;
const ee1 = new EventEmitter();
ee1.on('something', async (value) => {
  throw new Error('kaboom');
});

ee1.on('error', console.log);
```

This is an experimental feature.

Matteo Collina [#27867](https://github.com/nodejs/node/pull/27867).

#### Performance Hooks are no longer experimental

The `perf_hooks` module is now considered a stable API.

legendecas [#31101](https://github.com/nodejs/node/pull/31101).

#### Introduction of experimental WebAssembly System Interface (WASI) support

A new core module, `wasi`, is introduced to provide an implementation of the
[WebAssembly System Interface](https://wasi.dev/) specification.
WASI gives sandboxed WebAssembly applications access to the
underlying operating system via a collection of POSIX-like functions.

This is an experimental feature.

Colin Ihrig [#30258](https://github.com/nodejs/node/pull/30258).

### Commits

* \[[`fc7b27ea78`](https://github.com/nodejs/node/commit/fc7b27ea78)] - **(SEMVER-MINOR)** **assert**: implement `assert.match()` and `assert.doesNotMatch()` (Ruben Bridgewater) [#30929](https://github.com/nodejs/node/pull/30929)
* \[[`7d6c963b9d`](https://github.com/nodejs/node/commit/7d6c963b9d)] - **assert**: DRY .throws code (Ruben Bridgewater) [#28263](https://github.com/nodejs/node/pull/28263)
* \[[`749bc16cca`](https://github.com/nodejs/node/commit/749bc16cca)] - **assert**: fix generatedMessage property (Ruben Bridgewater) [#28263](https://github.com/nodejs/node/pull/28263)
* \[[`6909e3e656`](https://github.com/nodejs/node/commit/6909e3e656)] - **assert**: use for...of (Soar) [#30983](https://github.com/nodejs/node/pull/30983)
* \[[`b4e8f0de12`](https://github.com/nodejs/node/commit/b4e8f0de12)] - **assert**: fix line number calculation after V8 upgrade (Michaël Zasso) [#29694](https://github.com/nodejs/node/pull/29694)
* \[[`a0f338ecc1`](https://github.com/nodejs/node/commit/a0f338ecc1)] - **assert,util**: stricter type comparison using deep equal comparisons (Ruben Bridgewater) [#30764](https://github.com/nodejs/node/pull/30764)
* \[[`a9fad8524c`](https://github.com/nodejs/node/commit/a9fad8524c)] - **async\_hooks**: ensure proper handling in runInAsyncScope (Anatoli Papirovski) [#30965](https://github.com/nodejs/node/pull/30965)
* \[[`73e3c15a70`](https://github.com/nodejs/node/commit/73e3c15a70)] - **benchmark**: add more util inspect and format benchmarks (Ruben Bridgewater) [#30767](https://github.com/nodejs/node/pull/30767)
* \[[`634389b3ee`](https://github.com/nodejs/node/commit/634389b3ee)] - **benchmark**: use let instead of var in dgram (dnlup) [#31175](https://github.com/nodejs/node/pull/31175)
* \[[`b55420889c`](https://github.com/nodejs/node/commit/b55420889c)] - **benchmark**: add benchmark on async\_hooks enabled http server (legendecas) [#31100](https://github.com/nodejs/node/pull/31100)
* \[[`1c97163f76`](https://github.com/nodejs/node/commit/1c97163f76)] - **benchmark**: use let instead of var in crypto (dnlup) [#31135](https://github.com/nodejs/node/pull/31135)
* \[[`3de7713aa5`](https://github.com/nodejs/node/commit/3de7713aa5)] - **benchmark**: replace var with let/const in cluster benchmark (dnlup) [#31042](https://github.com/nodejs/node/pull/31042)
* \[[`471c59b4ba`](https://github.com/nodejs/node/commit/471c59b4ba)] - **benchmark**: include writev in benchmark (Robert Nagy) [#31066](https://github.com/nodejs/node/pull/31066)
* \[[`c73256460d`](https://github.com/nodejs/node/commit/c73256460d)] - **benchmark**: use let instead of var in child\_process (dnlup) [#31043](https://github.com/nodejs/node/pull/31043)
* \[[`aa973c5cd9`](https://github.com/nodejs/node/commit/aa973c5cd9)] - **benchmark**: add clear connections to secure-pair (Diego Lafuente) [#27971](https://github.com/nodejs/node/pull/27971)
* \[[`d5bebc3be8`](https://github.com/nodejs/node/commit/d5bebc3be8)] - **benchmark**: update manywrites back pressure (Robert Nagy) [#30977](https://github.com/nodejs/node/pull/30977)
* \[[`baabf3e764`](https://github.com/nodejs/node/commit/baabf3e764)] - **benchmark**: use let/const instead of var in buffers (dnlup) [#30945](https://github.com/nodejs/node/pull/30945)
* \[[`667471ee8b`](https://github.com/nodejs/node/commit/667471ee8b)] - **benchmark**: improve `--filter` pattern matching (Matheus Marchini) [#29987](https://github.com/nodejs/node/pull/29987)
* \[[`b4509170f4`](https://github.com/nodejs/node/commit/b4509170f4)] - **bootstrap**: use different scripts to setup different configurations (Joyee Cheung) [#30862](https://github.com/nodejs/node/pull/30862)
* \[[`655d0685c4`](https://github.com/nodejs/node/commit/655d0685c4)] - **buffer**: release buffers with free callbacks on env exit (Anna Henningsen) [#30551](https://github.com/nodejs/node/pull/30551)
* \[[`ae3459af9f`](https://github.com/nodejs/node/commit/ae3459af9f)] - **buffer**: improve .from() error details (Ruben Bridgewater) [#29675](https://github.com/nodejs/node/pull/29675)
* \[[`ada7624e6b`](https://github.com/nodejs/node/commit/ada7624e6b)] - **build**: auto-load ICU data from --with-icu-default-data-dir (Stephen Gallagher) [#30825](https://github.com/nodejs/node/pull/30825)
* \[[`d66996ce0d`](https://github.com/nodejs/node/commit/d66996ce0d)] - **build**: remove (almost) unused macros/constants (Benjamin Coe) [#30755](https://github.com/nodejs/node/pull/30755)
* \[[`ca432d756e`](https://github.com/nodejs/node/commit/ca432d756e)] - **build**: do not build mksnapshot and mkcodecache for --shared (Joyee Cheung) [#30647](https://github.com/nodejs/node/pull/30647)
* \[[`30096ef5a4`](https://github.com/nodejs/node/commit/30096ef5a4)] - **build**: add --without-node-code-cache configure option (Joyee Cheung) [#30647](https://github.com/nodejs/node/pull/30647)
* \[[`cb89fbcafc`](https://github.com/nodejs/node/commit/cb89fbcafc)] - **build**: don't use -latomic on macOS (Ryan Schmidt) [#30099](https://github.com/nodejs/node/pull/30099)
* \[[`b1b7f6746c`](https://github.com/nodejs/node/commit/b1b7f6746c)] - **build**: fixes build for some os versions (David Carlier)
* \[[`dc7a2320ff`](https://github.com/nodejs/node/commit/dc7a2320ff)] - **build**: fix missing x64 arch suffix in binary tar name (legendecas) [#30877](https://github.com/nodejs/node/pull/30877)
* \[[`ebe6a55ba8`](https://github.com/nodejs/node/commit/ebe6a55ba8)] - **build**: on Android, use android log library to print stack traces (Giovanni Campagna) [#29388](https://github.com/nodejs/node/pull/29388)
* \[[`fbf5beee56`](https://github.com/nodejs/node/commit/fbf5beee56)] - **build**: fix library version and compile flags on Android (Giovanni Campagna) [#29388](https://github.com/nodejs/node/pull/29388)
* \[[`c8c22b8d4c`](https://github.com/nodejs/node/commit/c8c22b8d4c)] - **build**: ease DragonFlyBSD build (David Carlier) [#30201](https://github.com/nodejs/node/pull/30201)
* \[[`766c2abff3`](https://github.com/nodejs/node/commit/766c2abff3)] - **build**: warn upon --use-largepages config option (Gabriel Schulhof) [#31103](https://github.com/nodejs/node/pull/31103)
* \[[`e67b3608af`](https://github.com/nodejs/node/commit/e67b3608af)] - **build**: switch realpath to pwd (Benjamin Coe) [#31095](https://github.com/nodejs/node/pull/31095)
* \[[`332b343f50`](https://github.com/nodejs/node/commit/332b343f50)] - **build**: re-introduce --use-largepages as no-op (Gabriel Schulhof)
* \[[`a91ed2eada`](https://github.com/nodejs/node/commit/a91ed2eada)] - **build**: reset embedder string to "-node.0" (Michaël Zasso) [#30109](https://github.com/nodejs/node/pull/30109)
* \[[`0b3951a8e7`](https://github.com/nodejs/node/commit/0b3951a8e7)] - **build,win**: fix goto exit in vcbuild (João Reis) [#30931](https://github.com/nodejs/node/pull/30931)
* \[[`df1e183e3f`](https://github.com/nodejs/node/commit/df1e183e3f)] - **child\_process,cluster**: allow using V8 serialization API (Anna Henningsen) [#30162](https://github.com/nodejs/node/pull/30162)
* \[[`8dc4e4ecb7`](https://github.com/nodejs/node/commit/8dc4e4ecb7)] - **cli**: add --trace-exit cli option (legendecas) [#30516](https://github.com/nodejs/node/pull/30516)
* \[[`ba289ffb4e`](https://github.com/nodejs/node/commit/ba289ffb4e)] - **cli**: whitelist new V8 flag in NODE\_OPTIONS (Shelley Vohr) [#30094](https://github.com/nodejs/node/pull/30094)
* \[[`dc58731e28`](https://github.com/nodejs/node/commit/dc58731e28)] - **cli**: add --trace-uncaught flag (Anna Henningsen) [#30025](https://github.com/nodejs/node/pull/30025)
* \[[`2d23502121`](https://github.com/nodejs/node/commit/2d23502121)] - **cluster**: remove unnecessary bind (Anatoli Papirovski) [#28131](https://github.com/nodejs/node/pull/28131)
* \[[`f54dc362a9`](https://github.com/nodejs/node/commit/f54dc362a9)] - **console**: unregister temporary error listener (Robert Nagy) [#30852](https://github.com/nodejs/node/pull/30852)
* \[[`9bc5c9fbc3`](https://github.com/nodejs/node/commit/9bc5c9fbc3)] - **crypto**: cast oaepLabel to unsigned char\* (Shelley Vohr) [#30917](https://github.com/nodejs/node/pull/30917)
* \[[`dd118b7272`](https://github.com/nodejs/node/commit/dd118b7272)] - **crypto**: automatically manage memory for ECDSA\_SIG (Tobias Nießen) [#30641](https://github.com/nodejs/node/pull/30641)
* \[[`df54ec3eb2`](https://github.com/nodejs/node/commit/df54ec3eb2)] - **crypto**: add support for IEEE-P1363 DSA signatures (Tobias Nießen) [#29292](https://github.com/nodejs/node/pull/29292)
* \[[`5dd72a67c4`](https://github.com/nodejs/node/commit/5dd72a67c4)] - **crypto**: add Hash.prototype.copy() method (Ben Noordhuis) [#29910](https://github.com/nodejs/node/pull/29910)
* \[[`e2cd110c0a`](https://github.com/nodejs/node/commit/e2cd110c0a)] - **deps**: V8: cherry-pick 0dfd9ea51241 (Benjamin Coe) [#30713](https://github.com/nodejs/node/pull/30713)
* \[[`b724eaf66d`](https://github.com/nodejs/node/commit/b724eaf66d)] - **deps**: V8: cherry-pick d89f4ef1cd62 (Milad Farazmand) [#31354](https://github.com/nodejs/node/pull/31354)
* \[[`6de77d3f09`](https://github.com/nodejs/node/commit/6de77d3f09)] - **deps**: uvwasi: cherry-pick 75b389c (Colin Ihrig) [#31076](https://github.com/nodejs/node/pull/31076)
* \[[`8f4339b8af`](https://github.com/nodejs/node/commit/8f4339b8af)] - **deps**: uvwasi: cherry-pick 64e59d5 (Colin Ihrig) [#31076](https://github.com/nodejs/node/pull/31076)
* \[[`63f85d52de`](https://github.com/nodejs/node/commit/63f85d52de)] - **deps**: update uvwasi (Anna Henningsen) [#30745](https://github.com/nodejs/node/pull/30745)
* \[[`317c3dffbb`](https://github.com/nodejs/node/commit/317c3dffbb)] - **deps**: V8: cherry-pick b38dfaf3a633 (Matheus Marchini) [#30870](https://github.com/nodejs/node/pull/30870)
* \[[`554c7c2c98`](https://github.com/nodejs/node/commit/554c7c2c98)] - **deps**: V8: cherry-pick cc5016e1b702 (Matheus Marchini) [#30870](https://github.com/nodejs/node/pull/30870)
* \[[`250198220d`](https://github.com/nodejs/node/commit/250198220d)] - **deps**: V8: backport a4545db (David Carlier) [#31127](https://github.com/nodejs/node/pull/31127)
* \[[`76eaf24f8f`](https://github.com/nodejs/node/commit/76eaf24f8f)] - **deps**: V8: cherry-pick d406bfd64653 (Sam Roberts) [#30819](https://github.com/nodejs/node/pull/30819)
* \[[`c004cf51c6`](https://github.com/nodejs/node/commit/c004cf51c6)] - **deps**: V8: cherry-pick d3a1a5b6c491 (Michaël Zasso) [#31005](https://github.com/nodejs/node/pull/31005)
* \[[`850cb15ae8`](https://github.com/nodejs/node/commit/850cb15ae8)] - **deps**: upgrade to libuv 1.34.0 (Colin Ihrig) [#30783](https://github.com/nodejs/node/pull/30783)
* \[[`ff82ccb151`](https://github.com/nodejs/node/commit/ff82ccb151)] - **deps**: fix OPENSSLDIR on Windows (Shigeki Ohtsu) [#29456](https://github.com/nodejs/node/pull/29456)
* \[[`6bee6878ba`](https://github.com/nodejs/node/commit/6bee6878ba)] - **deps**: V8: cherry-pick ca5b0ec (Anna Henningsen) [#30708](https://github.com/nodejs/node/pull/30708)
* \[[`c4074e37e2`](https://github.com/nodejs/node/commit/c4074e37e2)] - **deps**: V8: backport 777fa98 (Michaël Zasso) [#30062](https://github.com/nodejs/node/pull/30062)
* \[[`45240a1325`](https://github.com/nodejs/node/commit/45240a1325)] - **deps**: V8: cherry-pick 53e62af (Michaël Zasso) [#29898](https://github.com/nodejs/node/pull/29898)
* \[[`b335529803`](https://github.com/nodejs/node/commit/b335529803)] - **deps**: patch V8 to be API/ABI compatible with 7.4 (from 7.7) (Michaël Zasso) [#29241](https://github.com/nodejs/node/pull/29241)
* \[[`499ccdcf03`](https://github.com/nodejs/node/commit/499ccdcf03)] - **deps**: patch V8 to be API/ABI compatible with 7.4 (from 7.6) (Michaël Zasso) [#28955](https://github.com/nodejs/node/pull/28955)
* \[[`bb616bb06b`](https://github.com/nodejs/node/commit/bb616bb06b)] - **deps**: patch V8 to be API/ABI compatible with 7.4 (from 7.5) (Michaël Zasso) [#28005](https://github.com/nodejs/node/pull/28005)
* \[[`18c713da2c`](https://github.com/nodejs/node/commit/18c713da2c)] - **deps**: update V8's postmortem script (Colin Ihrig) [#29694](https://github.com/nodejs/node/pull/29694)
* \[[`593d989e8e`](https://github.com/nodejs/node/commit/593d989e8e)] - **deps**: V8: cherry-pick a7dffcd767be (Christian Clauss) [#30218](https://github.com/nodejs/node/pull/30218)
* \[[`5e1da86d9b`](https://github.com/nodejs/node/commit/5e1da86d9b)] - **deps**: V8: cherry-pick 0a055086c377 (Michaël Zasso) [#30109](https://github.com/nodejs/node/pull/30109)
* \[[`25dd890847`](https://github.com/nodejs/node/commit/25dd890847)] - **deps**: V8: cherry-pick e5dbc95 (Gabriel Schulhof) [#30130](https://github.com/nodejs/node/pull/30130)
* \[[`98dfe272b0`](https://github.com/nodejs/node/commit/98dfe272b0)] - **deps**: V8: cherry-pick ed40ab1 (Michaël Zasso) [#30064](https://github.com/nodejs/node/pull/30064)
* \[[`4cdccbda80`](https://github.com/nodejs/node/commit/4cdccbda80)] - **deps**: V8: cherry-pick 716875d (Myles Borins) [#29694](https://github.com/nodejs/node/pull/29694)
* \[[`667b9a409b`](https://github.com/nodejs/node/commit/667b9a409b)] - **deps**: V8: cherry-pick 35c6d4d (Sam Roberts) [#29585](https://github.com/nodejs/node/pull/29585)
* \[[`c43f5be7cf`](https://github.com/nodejs/node/commit/c43f5be7cf)] - **deps**: V8: cherry-pick deac757 (Benjamin Coe) [#29626](https://github.com/nodejs/node/pull/29626)
* \[[`d89f874871`](https://github.com/nodejs/node/commit/d89f874871)] - **deps**: V8: fix linking issue for MSVS (Refael Ackermann) [#28016](https://github.com/nodejs/node/pull/28016)
* \[[`0d20a85b8e`](https://github.com/nodejs/node/commit/0d20a85b8e)] - **deps**: V8: fix BUILDING\_V8\_SHARED issues (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`3d11924917`](https://github.com/nodejs/node/commit/3d11924917)] - **deps**: V8: add workaround for MSVC optimizer bug (Refael Ackermann) [#28016](https://github.com/nodejs/node/pull/28016)
* \[[`9135bc219b`](https://github.com/nodejs/node/commit/9135bc219b)] - **deps**: V8: use ATOMIC\_VAR\_INIT instead of std::atomic\_init (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`d98789b348`](https://github.com/nodejs/node/commit/d98789b348)] - **deps**: V8: forward declaration of `Rtl\*FunctionTable` (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`5a31dc8177`](https://github.com/nodejs/node/commit/5a31dc8177)] - **deps**: V8: patch register-arm64.h (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`fe18796b03`](https://github.com/nodejs/node/commit/fe18796b03)] - **deps**: V8: silence irrelevant warning (Michaël Zasso) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`4bf6e025a7`](https://github.com/nodejs/node/commit/4bf6e025a7)] - **deps**: V8: un-cherry-pick bd019bd (Refael Ackermann) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`fdad5b6f38`](https://github.com/nodejs/node/commit/fdad5b6f38)] - **deps**: V8: fix filename manipulation for Windows (Refael Ackermann) [#28016](https://github.com/nodejs/node/pull/28016)
* \[[`35f289260e`](https://github.com/nodejs/node/commit/35f289260e)] - **(SEMVER-MINOR)** **deps**: update V8 to 7.8.279.23 (Michaël Zasso) [#30109](https://github.com/nodejs/node/pull/30109)
* \[[`614ce0c51a`](https://github.com/nodejs/node/commit/614ce0c51a)] - **deps,http**: http\_parser set max header size to 8KB (Matteo Collina) [nodejs-private/node-private#143](https://github.com/nodejs-private/node-private/pull/143)
* \[[`8d336ff796`](https://github.com/nodejs/node/commit/8d336ff796)] - **deps,src**: patch V8 to be API/ABI compatible with 7.4 (from 7.8) (Anna Henningsen) [#30109](https://github.com/nodejs/node/pull/30109)
* \[[`bf4f516eea`](https://github.com/nodejs/node/commit/bf4f516eea)] - **deps,src,test**: update to uvwasi 0.0.3 (Colin Ihrig) [#30980](https://github.com/nodejs/node/pull/30980)
* \[[`25d96ecd4b`](https://github.com/nodejs/node/commit/25d96ecd4b)] - **dgram**: test to add and to drop specific membership (A. Volgin) [#31047](https://github.com/nodejs/node/pull/31047)
* \[[`b7ff93f45d`](https://github.com/nodejs/node/commit/b7ff93f45d)] - **dgram**: use for...of (Trivikram Kamat) [#30999](https://github.com/nodejs/node/pull/30999)
* \[[`b560f7b9d6`](https://github.com/nodejs/node/commit/b560f7b9d6)] - **(SEMVER-MINOR)** **dgram**: add source-specific multicast support (Lucas Pardue) [#15735](https://github.com/nodejs/node/pull/15735)
* \[[`9a6aff8517`](https://github.com/nodejs/node/commit/9a6aff8517)] - **doc**: make `AssertionError` a link (Ruben Bridgewater) [#28263](https://github.com/nodejs/node/pull/28263)
* \[[`08b5a2fcb4`](https://github.com/nodejs/node/commit/08b5a2fcb4)] - **doc**: update assert.throws() examples (Ruben Bridgewater) [#28263](https://github.com/nodejs/node/pull/28263)
* \[[`fd78d04188`](https://github.com/nodejs/node/commit/fd78d04188)] - **doc**: remove extra backtick (Colin Ihrig) [#31186](https://github.com/nodejs/node/pull/31186)
* \[[`808f025bea`](https://github.com/nodejs/node/commit/808f025bea)] - **doc**: use code markup/markdown in headers (Ruben Bridgewater) [#31149](https://github.com/nodejs/node/pull/31149)
* \[[`95eb1c2884`](https://github.com/nodejs/node/commit/95eb1c2884)] - **doc**: add note about fs.close() about undefined behavior (Robert Nagy) [#30966](https://github.com/nodejs/node/pull/30966)
* \[[`cfe30aebe1`](https://github.com/nodejs/node/commit/cfe30aebe1)] - **doc**: add code example to inspector.url() method (Juan José Arboleda) [#29496](https://github.com/nodejs/node/pull/29496)
* \[[`79521d304c`](https://github.com/nodejs/node/commit/79521d304c)] - **doc**: deprecate http finished (Robert Nagy) [#28679](https://github.com/nodejs/node/pull/28679)
* \[[`2c85dd91d6`](https://github.com/nodejs/node/commit/2c85dd91d6)] - **doc**: update REPL documentation to instantiate the REPL (Ruben Bridgewater) [#30928](https://github.com/nodejs/node/pull/30928)
* \[[`deb1a591f5`](https://github.com/nodejs/node/commit/deb1a591f5)] - **doc**: improve explanation of package.json "type" field (Ronald J Kimball) [#27516](https://github.com/nodejs/node/pull/27516)
* \[[`37560cdf81`](https://github.com/nodejs/node/commit/37560cdf81)] - **doc**: clarify role of writable.cork() (Colin Grant) [#30442](https://github.com/nodejs/node/pull/30442)
* \[[`5648f5ec6e`](https://github.com/nodejs/node/commit/5648f5ec6e)] - **doc**: de-duplicate security release processes (Sam Roberts) [#30996](https://github.com/nodejs/node/pull/30996)
* \[[`2d9d59f427`](https://github.com/nodejs/node/commit/2d9d59f427)] - **doc**: fix createDiffieHellman generator type (Tobias Nießen) [#31121](https://github.com/nodejs/node/pull/31121)
* \[[`6df270451a`](https://github.com/nodejs/node/commit/6df270451a)] - **doc**: update mode type for mkdir() functions (Colin Ihrig) [#31115](https://github.com/nodejs/node/pull/31115)
* \[[`1d7ff3d673`](https://github.com/nodejs/node/commit/1d7ff3d673)] - **doc**: update mode type for process.umask() (Colin Ihrig) [#31115](https://github.com/nodejs/node/pull/31115)
* \[[`f851d9fbd8`](https://github.com/nodejs/node/commit/f851d9fbd8)] - **doc**: update mode type for fs open() functions (Colin Ihrig) [#31115](https://github.com/nodejs/node/pull/31115)
* \[[`e104e72f58`](https://github.com/nodejs/node/commit/e104e72f58)] - **doc**: update mode type for fchmod() functions (Colin Ihrig) [#31115](https://github.com/nodejs/node/pull/31115)
* \[[`13fe137791`](https://github.com/nodejs/node/commit/13fe137791)] - **doc**: update parameter type for fsPromises.chmod() (Colin Ihrig) [#31115](https://github.com/nodejs/node/pull/31115)
* \[[`ddad6eb90f`](https://github.com/nodejs/node/commit/ddad6eb90f)] - **doc**: improve dns introduction (Rich Trott) [#31090](https://github.com/nodejs/node/pull/31090)
* \[[`a192afc2aa`](https://github.com/nodejs/node/commit/a192afc2aa)] - **doc**: update parameter type for fs.chmod() (Santosh Yadav) [#31085](https://github.com/nodejs/node/pull/31085)
* \[[`fd0565c91c`](https://github.com/nodejs/node/commit/fd0565c91c)] - **doc**: add --inspect-publish-uid man page entry (Colin Ihrig) [#31077](https://github.com/nodejs/node/pull/31077)
* \[[`39e2af67e2`](https://github.com/nodejs/node/commit/39e2af67e2)] - **doc**: add --force-context-aware man page entry (Colin Ihrig) [#31077](https://github.com/nodejs/node/pull/31077)
* \[[`1d28db1007`](https://github.com/nodejs/node/commit/1d28db1007)] - **doc**: add --enable-source-maps man page entry (Colin Ihrig) [#31077](https://github.com/nodejs/node/pull/31077)
* \[[`5796ec757f`](https://github.com/nodejs/node/commit/5796ec757f)] - **doc**: fix anchors and subtitle in BUILDING.md (sutangu) [#30296](https://github.com/nodejs/node/pull/30296)
* \[[`4f95213b83`](https://github.com/nodejs/node/commit/4f95213b83)] - **doc**: standardize usage of hostname vs. host name (Rich Trott) [#31073](https://github.com/nodejs/node/pull/31073)
* \[[`7b567bdd49`](https://github.com/nodejs/node/commit/7b567bdd49)] - **doc**: add unrepresented flags docs for configure (Pranshu Srivastava) [#28069](https://github.com/nodejs/node/pull/28069)
* \[[`f0994940f0`](https://github.com/nodejs/node/commit/f0994940f0)] - **doc**: improve doc net:server.listen (dev-313) [#31064](https://github.com/nodejs/node/pull/31064)
* \[[`f8530128bd`](https://github.com/nodejs/node/commit/f8530128bd)] - **doc**: implement minor improvements to BUILDING.md text (Rich Trott) [#31070](https://github.com/nodejs/node/pull/31070)
* \[[`53403619ad`](https://github.com/nodejs/node/commit/53403619ad)] - **doc**: avoid using v8::Persistent in addon docs (Anna Henningsen) [#31018](https://github.com/nodejs/node/pull/31018)
* \[[`d3c969547a`](https://github.com/nodejs/node/commit/d3c969547a)] - **doc**: reference worker threads on signal events (legendecas) [#30990](https://github.com/nodejs/node/pull/30990)
* \[[`55360487b7`](https://github.com/nodejs/node/commit/55360487b7)] - **doc**: update message.url example in http.IncomingMessage (Tadao Iseki) [#30830](https://github.com/nodejs/node/pull/30830)
* \[[`178acac7d5`](https://github.com/nodejs/node/commit/178acac7d5)] - **doc**: explain napi\_run\_script (Tobias Nießen) [#30918](https://github.com/nodejs/node/pull/30918)
* \[[`fb3af1b23a`](https://github.com/nodejs/node/commit/fb3af1b23a)] - **doc**: add "Be direct." to the style guide (Rich Trott) [#30935](https://github.com/nodejs/node/pull/30935)
* \[[`0688c99823`](https://github.com/nodejs/node/commit/0688c99823)] - **doc**: clarify expectations for PR commit messages (Derek Lewis) [#30922](https://github.com/nodejs/node/pull/30922)
* \[[`28a8247918`](https://github.com/nodejs/node/commit/28a8247918)] - **doc**: fix description of N-API exception handlers (Tobias Nießen) [#30893](https://github.com/nodejs/node/pull/30893)
* \[[`be4fffe396`](https://github.com/nodejs/node/commit/be4fffe396)] - **doc**: improve doc writable streams: 'finish' event (dev-313) [#30889](https://github.com/nodejs/node/pull/30889)
* \[[`21ea47a08e`](https://github.com/nodejs/node/commit/21ea47a08e)] - **doc**: clarify build support text (Rich Trott) [#30899](https://github.com/nodejs/node/pull/30899)
* \[[`fc0c7286c8`](https://github.com/nodejs/node/commit/fc0c7286c8)] - **doc**: edit colorMode information (Rich Trott) [#30887](https://github.com/nodejs/node/pull/30887)
* \[[`22f83598d9`](https://github.com/nodejs/node/commit/22f83598d9)] - **doc**: fix argument type of setAAD (Tobias Nießen) [#30863](https://github.com/nodejs/node/pull/30863)
* \[[`7b3e26987d`](https://github.com/nodejs/node/commit/7b3e26987d)] - **doc**: clarify Tier 2 implications in BUILDING.md (Rich Trott) [#30866](https://github.com/nodejs/node/pull/30866)
* \[[`e0811cd8cc`](https://github.com/nodejs/node/commit/e0811cd8cc)] - **doc**: improve doc Http2Stream: FrameError, Timeout and Trailers (dev-313) [#30373](https://github.com/nodejs/node/pull/30373)
* \[[`6db2562796`](https://github.com/nodejs/node/commit/6db2562796)] - **doc**: include line/cursor in readline documentation (Jeremy Albright) [#30667](https://github.com/nodejs/node/pull/30667)
* \[[`5d56e85f84`](https://github.com/nodejs/node/commit/5d56e85f84)] - **doc**: improve napi formatting (Ruben Bridgewater) [#30772](https://github.com/nodejs/node/pull/30772)
* \[[`998d04d792`](https://github.com/nodejs/node/commit/998d04d792)] - **doc**: add documentation about node\_mksnapshot and mkcodecache (Joyee Cheung) [#30773](https://github.com/nodejs/node/pull/30773)
* \[[`73427af3c8`](https://github.com/nodejs/node/commit/73427af3c8)] - **doc**: remove imprecise and redundant testing text (Rich Trott) [#30763](https://github.com/nodejs/node/pull/30763)
* \[[`6418b939e3`](https://github.com/nodejs/node/commit/6418b939e3)] - **doc**: remove usage of "Node" in favor of "Node.js" (Rich Trott) [#30758](https://github.com/nodejs/node/pull/30758)
* \[[`a500eee3e7`](https://github.com/nodejs/node/commit/a500eee3e7)] - **doc**: revise addons introduction for brevity and clarity (Rich Trott) [#30756](https://github.com/nodejs/node/pull/30756)
* \[[`005b601aa1`](https://github.com/nodejs/node/commit/005b601aa1)] - **doc**: fix up N-API doc (NickNaso) [#30656](https://github.com/nodejs/node/pull/30656)
* \[[`420d793f9a`](https://github.com/nodejs/node/commit/420d793f9a)] - **doc**: adds assert doc for strict mode with pointer to strict equality (Shobhit Chittora) [#30486](https://github.com/nodejs/node/pull/30486)
* \[[`ab7304767e`](https://github.com/nodejs/node/commit/ab7304767e)] - **doc**: Buffer.toString(): add note about invalid data (Jan-Philip Gehrcke) [#30706](https://github.com/nodejs/node/pull/30706)
* \[[`a152458e6e`](https://github.com/nodejs/node/commit/a152458e6e)] - **doc**: clarify text about using 'session' event for compatibility (Rich Trott) [#30746](https://github.com/nodejs/node/pull/30746)
* \[[`c79f485af9`](https://github.com/nodejs/node/commit/c79f485af9)] - **doc**: fix worker.resourceLimits indentation (Daniel Nalborczyk) [#30663](https://github.com/nodejs/node/pull/30663)
* \[[`1a6443dfde`](https://github.com/nodejs/node/commit/1a6443dfde)] - **doc**: fix worker.resourceLimits type (Daniel Nalborczyk) [#30664](https://github.com/nodejs/node/pull/30664)
* \[[`b7bd84f7d2`](https://github.com/nodejs/node/commit/b7bd84f7d2)] - **doc**: simplify "is recommended" language in assert documentation (Rich Trott) [#30558](https://github.com/nodejs/node/pull/30558)
* \[[`9b7bde14c3`](https://github.com/nodejs/node/commit/9b7bde14c3)] - **doc**: update http.md mention of socket (Jesse O'Connor) [#30155](https://github.com/nodejs/node/pull/30155)
* \[[`2cbb358c23`](https://github.com/nodejs/node/commit/2cbb358c23)] - **doc**: clarify required flag for extensionless esm (Lucas Azzola) [#30657](https://github.com/nodejs/node/pull/30657)
* \[[`de3fdfaa6f`](https://github.com/nodejs/node/commit/de3fdfaa6f)] - **doc**: avoid proposal syntax in code example (Alex Zherdev) [#30685](https://github.com/nodejs/node/pull/30685)
* \[[`138a905b15`](https://github.com/nodejs/node/commit/138a905b15)] - **doc**: esm: improve dual package hazard docs (Geoffrey Booth) [#30345](https://github.com/nodejs/node/pull/30345)
* \[[`5687a3178d`](https://github.com/nodejs/node/commit/5687a3178d)] - **doc**: fix some recent doc nits (vsemozhetbyt) [#30341](https://github.com/nodejs/node/pull/30341)
* \[[`007dab8f25`](https://github.com/nodejs/node/commit/007dab8f25)] - **doc**: update outdated commonjs compat info (Geoffrey Booth) [#30512](https://github.com/nodejs/node/pull/30512)
* \[[`d0f4a2f14a`](https://github.com/nodejs/node/commit/d0f4a2f14a)] - **doc**: update divergent specifier hazard guidance (Geoffrey Booth) [#30051](https://github.com/nodejs/node/pull/30051)
* \[[`1f46eea24d`](https://github.com/nodejs/node/commit/1f46eea24d)] - **doc**: include --experimental-resolve-self in manpage (Guy Bedford) [#29978](https://github.com/nodejs/node/pull/29978)
* \[[`30edcc03aa`](https://github.com/nodejs/node/commit/30edcc03aa)] - **doc**: update vm.md for link linting (Rich Trott) [#29982](https://github.com/nodejs/node/pull/29982)
* \[[`426ed0dffa`](https://github.com/nodejs/node/commit/426ed0dffa)] - **doc**: make YAML matter consistent in crypto.md (Rich Trott) [#30016](https://github.com/nodejs/node/pull/30016)
* \[[`2d5aec013c`](https://github.com/nodejs/node/commit/2d5aec013c)] - **doc**: fix numbering in require algorithm (Jan Krems) [#30117](https://github.com/nodejs/node/pull/30117)
* \[[`9023c59a8d`](https://github.com/nodejs/node/commit/9023c59a8d)] - **doc**: use code markup/markdown in headers in globals documentation (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`448a1178fa`](https://github.com/nodejs/node/commit/448a1178fa)] - **doc**: use code markup/markdown in headers in deprecations documentation (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`b5a19bcf65`](https://github.com/nodejs/node/commit/b5a19bcf65)] - **doc**: use code markup/markdown in headers in addons documentation (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`2f2f79d8eb`](https://github.com/nodejs/node/commit/2f2f79d8eb)] - **doc**: allow \<code> in header elements (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`2885bdbc56`](https://github.com/nodejs/node/commit/2885bdbc56)] - **doc,assert**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`da25662fc8`](https://github.com/nodejs/node/commit/da25662fc8)] - **doc,async\_hooks**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`54c60d2e57`](https://github.com/nodejs/node/commit/54c60d2e57)] - **doc,benchmark**: move benchmark guide to benchmark directory (Rich Trott) [#30781](https://github.com/nodejs/node/pull/30781)
* \[[`a96590a69f`](https://github.com/nodejs/node/commit/a96590a69f)] - **doc,buffer**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`8a5fe08fd4`](https://github.com/nodejs/node/commit/8a5fe08fd4)] - **doc,child\_process**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`8eecc56cd3`](https://github.com/nodejs/node/commit/8eecc56cd3)] - **doc,cluster**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`54e41cebbd`](https://github.com/nodejs/node/commit/54e41cebbd)] - **doc,console**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`67637c652b`](https://github.com/nodejs/node/commit/67637c652b)] - **doc,crypto**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`c2ad43af89`](https://github.com/nodejs/node/commit/c2ad43af89)] - **doc,dgram**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`135097f845`](https://github.com/nodejs/node/commit/135097f845)] - **doc,dns**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`0a29db286d`](https://github.com/nodejs/node/commit/0a29db286d)] - **doc,domain**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`69da6110ab`](https://github.com/nodejs/node/commit/69da6110ab)] - **doc,errors**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`c4503ea987`](https://github.com/nodejs/node/commit/c4503ea987)] - **doc,esm**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`c4c10d1c09`](https://github.com/nodejs/node/commit/c4c10d1c09)] - **doc,events**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`8848062bc4`](https://github.com/nodejs/node/commit/8848062bc4)] - **doc,fs**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`25b30e4b61`](https://github.com/nodejs/node/commit/25b30e4b61)] - **doc,http**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`be7d4dea4b`](https://github.com/nodejs/node/commit/be7d4dea4b)] - **doc,http2**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`2449d5fee6`](https://github.com/nodejs/node/commit/2449d5fee6)] - **doc,https**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`f7255c12a8`](https://github.com/nodejs/node/commit/f7255c12a8)] - **doc,inspector**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`3454f65ebe`](https://github.com/nodejs/node/commit/3454f65ebe)] - **doc,lib,src,test**: rename WASI CLI flag (Colin Ihrig) [#30980](https://github.com/nodejs/node/pull/30980)
* \[[`bd5ae0a140`](https://github.com/nodejs/node/commit/bd5ae0a140)] - **doc,module**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`2697c0d008`](https://github.com/nodejs/node/commit/2697c0d008)] - **doc,n-api**: mark napi\_detach\_arraybuffer as experimental (legendecas) [#30703](https://github.com/nodejs/node/pull/30703)
* \[[`bff03ca2cb`](https://github.com/nodejs/node/commit/bff03ca2cb)] - **doc,net**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`4fa99591b0`](https://github.com/nodejs/node/commit/4fa99591b0)] - **doc,os**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`b18c128aff`](https://github.com/nodejs/node/commit/b18c128aff)] - **doc,path**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`77813e0426`](https://github.com/nodejs/node/commit/77813e0426)] - **doc,perf\_hooks**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`84e3a86bd5`](https://github.com/nodejs/node/commit/84e3a86bd5)] - **doc,process**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`7f2625f5df`](https://github.com/nodejs/node/commit/7f2625f5df)] - **doc,punycode**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`6de05ecf23`](https://github.com/nodejs/node/commit/6de05ecf23)] - **doc,querystring**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`4dc930cdd9`](https://github.com/nodejs/node/commit/4dc930cdd9)] - **doc,readline**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`55a269ce7c`](https://github.com/nodejs/node/commit/55a269ce7c)] - **doc,repl**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`8a98243fc6`](https://github.com/nodejs/node/commit/8a98243fc6)] - **doc,stream**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`b0e4a02dca`](https://github.com/nodejs/node/commit/b0e4a02dca)] - **doc,string\_decoder**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`ad48c27fe9`](https://github.com/nodejs/node/commit/ad48c27fe9)] - **doc,timers**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`fd0a3cbfd1`](https://github.com/nodejs/node/commit/fd0a3cbfd1)] - **doc,tls**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`38bcd45b4c`](https://github.com/nodejs/node/commit/38bcd45b4c)] - **doc,tty**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`4f564e77f7`](https://github.com/nodejs/node/commit/4f564e77f7)] - **doc,url**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`1b2c0a9c43`](https://github.com/nodejs/node/commit/1b2c0a9c43)] - **doc,util**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`9dfe436588`](https://github.com/nodejs/node/commit/9dfe436588)] - **doc,v8**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`930cf99345`](https://github.com/nodejs/node/commit/930cf99345)] - **doc,vm**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`ffe92267fc`](https://github.com/nodejs/node/commit/ffe92267fc)] - **doc,vm,test**: remove \_sandbox\_ from vm documentation (Rich Trott) [#31057](https://github.com/nodejs/node/pull/31057)
* \[[`255e3cdd40`](https://github.com/nodejs/node/commit/255e3cdd40)] - **doc,wasi**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`a361a7356d`](https://github.com/nodejs/node/commit/a361a7356d)] - **doc,worker**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`367143ee33`](https://github.com/nodejs/node/commit/367143ee33)] - **doc,zlib**: use code markup/markdown in headers (Rich Trott) [#31086](https://github.com/nodejs/node/pull/31086)
* \[[`df94cfb67c`](https://github.com/nodejs/node/commit/df94cfb67c)] - **errors**: improve ERR\_INVALID\_ARG\_TYPE (Ruben Bridgewater) [#29675](https://github.com/nodejs/node/pull/29675)
* \[[`2986982459`](https://github.com/nodejs/node/commit/2986982459)] - **errors**: support prepareSourceMap with source-maps (Benjamin Coe) [#31143](https://github.com/nodejs/node/pull/31143)
* \[[`a2ac9d3098`](https://github.com/nodejs/node/commit/a2ac9d3098)] - **esm**: better error message for unsupported URL (Thomas) [#31129](https://github.com/nodejs/node/pull/31129)
* \[[`298fdbe442`](https://github.com/nodejs/node/commit/298fdbe442)] - **esm**: empty ext from pkg type/main doesnt affect format (Bradley Farias) [#31021](https://github.com/nodejs/node/pull/31021)
* \[[`fa96f54028`](https://github.com/nodejs/node/commit/fa96f54028)] - **esm**: make specifier flag clearly experimental (Myles Borins) [#30678](https://github.com/nodejs/node/pull/30678)
* \[[`05172951ac`](https://github.com/nodejs/node/commit/05172951ac)] - **esm**: data URLs should ignore unknown parameters (Bradley Farias) [#30593](https://github.com/nodejs/node/pull/30593)
* \[[`2275da52a0`](https://github.com/nodejs/node/commit/2275da52a0)] - **esm**: disable non-js exts outside package scopes (Guy Bedford) [#30501](https://github.com/nodejs/node/pull/30501)
* \[[`7b46b20947`](https://github.com/nodejs/node/commit/7b46b20947)] - **esm**: exit the process with an error if loader has an issue (Michaël Zasso) [#30219](https://github.com/nodejs/node/pull/30219)
* \[[`d6e69fbd25`](https://github.com/nodejs/node/commit/d6e69fbd25)] - **(SEMVER-MINOR)** **esm**: unflag --experimental-exports (Guy Bedford) [#29867](https://github.com/nodejs/node/pull/29867)
* \[[`eb82683538`](https://github.com/nodejs/node/commit/eb82683538)] - **(SEMVER-MINOR)** **events**: add EventEmitter.on to async iterate over events (Matteo Collina) [#27994](https://github.com/nodejs/node/pull/27994)
* \[[`5cb0de948d`](https://github.com/nodejs/node/commit/5cb0de948d)] - **(SEMVER-MINOR)** **events**: allow monitoring error events (Gerhard Stoebich) [#30932](https://github.com/nodejs/node/pull/30932)
* \[[`9f81da5883`](https://github.com/nodejs/node/commit/9f81da5883)] - **(SEMVER-MINOR)** **events**: add captureRejection option (Matteo Collina) [#27867](https://github.com/nodejs/node/pull/27867)
* \[[`578d12fa10`](https://github.com/nodejs/node/commit/578d12fa10)] - **fs**: synchronize close with other I/O for streams (Anna Henningsen) [#30837](https://github.com/nodejs/node/pull/30837)
* \[[`55c5baf413`](https://github.com/nodejs/node/commit/55c5baf413)] - **fs**: retry unlink operations in rimraf (Colin Ihrig) [#30569](https://github.com/nodejs/node/pull/30569)
* \[[`edc9efa5c8`](https://github.com/nodejs/node/commit/edc9efa5c8)] - **fs**: only operate on buffers in rimraf (Colin Ihrig) [#30569](https://github.com/nodejs/node/pull/30569)
* \[[`465a1cf8b9`](https://github.com/nodejs/node/commit/465a1cf8b9)] - **fs**: use consistent defaults in sync stat functions (Colin Ihrig) [#31097](https://github.com/nodejs/node/pull/31097)
* \[[`cc9712d7b3`](https://github.com/nodejs/node/commit/cc9712d7b3)] - **fs**: remove unnecessary bind (Anatoli Papirovski) [#28131](https://github.com/nodejs/node/pull/28131)
* \[[`1d4e3d50ab`](https://github.com/nodejs/node/commit/1d4e3d50ab)] - **fs**: reduce unnecessary sync rimraf retries (Colin Ihrig) [#30785](https://github.com/nodejs/node/pull/30785)
* \[[`5d39527b22`](https://github.com/nodejs/node/commit/5d39527b22)] - **fs**: add synchronous retries to rimraf (Colin Ihrig) [#30785](https://github.com/nodejs/node/pull/30785)
* \[[`366a45be2a`](https://github.com/nodejs/node/commit/366a45be2a)] - **fs**: fix existsSync for invalid symlink at win32 (Rongjian Zhang) [#30556](https://github.com/nodejs/node/pull/30556)
* \[[`4fffb42939`](https://github.com/nodejs/node/commit/4fffb42939)] - **fs**: add ENFILE to rimraf retry logic (Colin Ihrig) [#30644](https://github.com/nodejs/node/pull/30644)
* \[[`f9d8494410`](https://github.com/nodejs/node/commit/f9d8494410)] - **fs**: add retryDelay option to rimraf (Colin Ihrig) [#30644](https://github.com/nodejs/node/pull/30644)
* \[[`7a321989ac`](https://github.com/nodejs/node/commit/7a321989ac)] - **fs**: remove rimraf's emfileWait option (Colin Ihrig) [#30644](https://github.com/nodejs/node/pull/30644)
* \[[`ccc228b438`](https://github.com/nodejs/node/commit/ccc228b438)] - **fs**: make rimraf default to 0 retries (Colin Ihrig) [#30644](https://github.com/nodejs/node/pull/30644)
* \[[`3a70185c16`](https://github.com/nodejs/node/commit/3a70185c16)] - **fs**: rename rimraf's maxBusyTries to maxRetries (Colin Ihrig) [#30644](https://github.com/nodejs/node/pull/30644)
* \[[`785aa86b94`](https://github.com/nodejs/node/commit/785aa86b94)] - **(SEMVER-MINOR)** **fs**: add `bufferSize` option to `fs.opendir()` (Anna Henningsen) [#30114](https://github.com/nodejs/node/pull/30114)
* \[[`73717f2d7e`](https://github.com/nodejs/node/commit/73717f2d7e)] - **http**: http\_outgoing rename var to let and const (telenord) [#30284](https://github.com/nodejs/node/pull/30284)
* \[[`350cfa7333`](https://github.com/nodejs/node/commit/350cfa7333)] - **http**: free listeners on free sockets (Robert Nagy) [#29259](https://github.com/nodejs/node/pull/29259)
* \[[`4cc10d5fd4`](https://github.com/nodejs/node/commit/4cc10d5fd4)] - **http**: use for...of in http library code (Trivikram Kamat) [#30958](https://github.com/nodejs/node/pull/30958)
* \[[`35a33f6e01`](https://github.com/nodejs/node/commit/35a33f6e01)] - **http**: add captureRejection support to OutgoingMessage (Matteo Collina) [#27867](https://github.com/nodejs/node/pull/27867)
* \[[`f7d128ad48`](https://github.com/nodejs/node/commit/f7d128ad48)] - **http**: implement capture rejections for 'request' event (Matteo Collina) [#27867](https://github.com/nodejs/node/pull/27867)
* \[[`8111d69635`](https://github.com/nodejs/node/commit/8111d69635)] - **http**: remove unnecessary bind (Anatoli Papirovski) [#28131](https://github.com/nodejs/node/pull/28131)
* \[[`4f85f52933`](https://github.com/nodejs/node/commit/4f85f52933)] - **http**: improve performance caused by primordials (Lucas Recknagel) [#30416](https://github.com/nodejs/node/pull/30416)
* \[[`db8144be31`](https://github.com/nodejs/node/commit/db8144be31)] - **(SEMVER-MINOR)** **http**: outgoing cork (Robert Nagy) [#29053](https://github.com/nodejs/node/pull/29053)
* \[[`86369e4ac5`](https://github.com/nodejs/node/commit/86369e4ac5)] - **(SEMVER-MINOR)** **http**: support readable hwm in IncomingMessage (Colin Ihrig) [#30135](https://github.com/nodejs/node/pull/30135)
* \[[`b9ffca1a00`](https://github.com/nodejs/node/commit/b9ffca1a00)] - **(SEMVER-MINOR)** **http**: add reusedSocket property on client request (themez) [#29715](https://github.com/nodejs/node/pull/29715)
* \[[`2445bc0d48`](https://github.com/nodejs/node/commit/2445bc0d48)] - **http**: fix monkey-patching of http\_parser (Jimb Esser) [#30222](https://github.com/nodejs/node/pull/30222)
* \[[`92a9dacce9`](https://github.com/nodejs/node/commit/92a9dacce9)] - **http2**: make HTTP2ServerResponse more streams compliant (Robert Nagy)
* \[[`5dd7c92b41`](https://github.com/nodejs/node/commit/5dd7c92b41)] - **http2**: set default enableConnectProtocol to 0 (Yongsheng Zhang) [#31174](https://github.com/nodejs/node/pull/31174)
* \[[`d7d7cf7513`](https://github.com/nodejs/node/commit/d7d7cf7513)] - **http2**: implement capture rection for 'request' and 'stream' events (Matteo Collina) [#27867](https://github.com/nodejs/node/pull/27867)
* \[[`84603ec3ee`](https://github.com/nodejs/node/commit/84603ec3ee)] - **http2**: remove unnecessary bind from setImmediate (Anatoli Papirovski) [#28131](https://github.com/nodejs/node/pull/28131)
* \[[`081e488871`](https://github.com/nodejs/node/commit/081e488871)] - **http2**: forward debug message in debugStreamObj (Denys Otrishko) [#30840](https://github.com/nodejs/node/pull/30840)
* \[[`b2a2c6c032`](https://github.com/nodejs/node/commit/b2a2c6c032)] - **http2**: track nghttp2-allocated memory in heap snapshot (Anna Henningsen) [#30745](https://github.com/nodejs/node/pull/30745)
* \[[`9be789e632`](https://github.com/nodejs/node/commit/9be789e632)] - **http2**: use shared memory tracking implementation (Anna Henningsen) [#30745](https://github.com/nodejs/node/pull/30745)
* \[[`53c691c390`](https://github.com/nodejs/node/commit/53c691c390)] - **http2**: streamline OnStreamRead streamline memory accounting (Denys Otrishko) [#30351](https://github.com/nodejs/node/pull/30351)
* \[[`da9fffa6a0`](https://github.com/nodejs/node/commit/da9fffa6a0)] - **http2**: small clean up in OnStreamRead (Denys Otrishko) [#30351](https://github.com/nodejs/node/pull/30351)
* \[[`a4ae272c5b`](https://github.com/nodejs/node/commit/a4ae272c5b)] - **(SEMVER-MINOR)** **http2**: make maximum tolerated rejected streams configurable (Denys Otrishko) [#30534](https://github.com/nodejs/node/pull/30534)
* \[[`7b2660630c`](https://github.com/nodejs/node/commit/7b2660630c)] - **(SEMVER-MINOR)** **http2**: allow to configure maximum tolerated invalid frames (Denys Otrishko) [#30534](https://github.com/nodejs/node/pull/30534)
* \[[`7998fbb7e9`](https://github.com/nodejs/node/commit/7998fbb7e9)] - **http2**: replace direct array usage with struct for js\_fields\_ (Denys Otrishko) [#30534](https://github.com/nodejs/node/pull/30534)
* \[[`06bcd1ab9b`](https://github.com/nodejs/node/commit/06bcd1ab9b)] - **https**: prevent options object from being mutated (Vighnesh Raut) [#31151](https://github.com/nodejs/node/pull/31151)
* \[[`dbee78caa4`](https://github.com/nodejs/node/commit/dbee78caa4)] - **(SEMVER-MINOR)** **https**: add client support for TLS keylog events (Sam Roberts) [#30053](https://github.com/nodejs/node/pull/30053)
* \[[`9908bd0dc2`](https://github.com/nodejs/node/commit/9908bd0dc2)] - **inspector**: do not access queueMicrotask from global (Michaël Zasso) [#30732](https://github.com/nodejs/node/pull/30732)
* \[[`367484dbe1`](https://github.com/nodejs/node/commit/367484dbe1)] - **lib**: move initialization of APIs for changing process state (Anna Henningsen) [#31172](https://github.com/nodejs/node/pull/31172)
* \[[`7f70c2431c`](https://github.com/nodejs/node/commit/7f70c2431c)] - **lib**: do not catch user errors (Ruben Bridgewater) [#31159](https://github.com/nodejs/node/pull/31159)
* \[[`59101b5a2a`](https://github.com/nodejs/node/commit/59101b5a2a)] - **lib**: replace var with let/const (kresimirfranin) [#30394](https://github.com/nodejs/node/pull/30394)
* \[[`e7829758dd`](https://github.com/nodejs/node/commit/e7829758dd)] - **lib**: further simplify assertions in vm/module (Anna Henningsen) [#30815](https://github.com/nodejs/node/pull/30815)
* \[[`02fc95d3d7`](https://github.com/nodejs/node/commit/02fc95d3d7)] - **lib**: improve spelling and grammar in comment (David Newman) [#31026](https://github.com/nodejs/node/pull/31026)
* \[[`d19316d2d9`](https://github.com/nodejs/node/commit/d19316d2d9)] - **lib**: change var to let/const (rene.herrmann) [#30910](https://github.com/nodejs/node/pull/30910)
* \[[`84c9e4f1b5`](https://github.com/nodejs/node/commit/84c9e4f1b5)] - **lib**: refactor NativeModule (Joyee Cheung) [#30856](https://github.com/nodejs/node/pull/30856)
* \[[`168dd92537`](https://github.com/nodejs/node/commit/168dd92537)] - **lib**: replace var with let/const (jens-cappelle) [#30384](https://github.com/nodejs/node/pull/30384)
* \[[`abfb8c11b5`](https://github.com/nodejs/node/commit/abfb8c11b5)] - **lib**: delay access to CLI option to pre-execution (Joyee Cheung) [#30778](https://github.com/nodejs/node/pull/30778)
* \[[`947d066da7`](https://github.com/nodejs/node/commit/947d066da7)] - **lib**: replace Map global by the primordials (Sebastien Ahkrin) [#31155](https://github.com/nodejs/node/pull/31155)
* \[[`dc9a51d72b`](https://github.com/nodejs/node/commit/dc9a51d72b)] - **lib**: replace use of Error with primordials (Sebastien Ahkrin) [#31163](https://github.com/nodejs/node/pull/31163)
* \[[`131d961845`](https://github.com/nodejs/node/commit/131d961845)] - **lib**: replace Set global by the primordials (Sebastien Ahkrin) [#31154](https://github.com/nodejs/node/pull/31154)
* \[[`e955606a1e`](https://github.com/nodejs/node/commit/e955606a1e)] - **lib**: replace WeakSet global by the primordials (Sebastien Ahkrin) [#31157](https://github.com/nodejs/node/pull/31157)
* \[[`d5d0744d1a`](https://github.com/nodejs/node/commit/d5d0744d1a)] - **lib**: replace WeakMap global by the primordials (Sebastien Ahkrin) [#31158](https://github.com/nodejs/node/pull/31158)
* \[[`61e794b243`](https://github.com/nodejs/node/commit/61e794b243)] - **lib**: replace Set.prototype with SetPrototype primordial (Sebastien Ahkrin) [#31161](https://github.com/nodejs/node/pull/31161)
* \[[`0229a24b47`](https://github.com/nodejs/node/commit/0229a24b47)] - **lib**: replace Symbol.species by SymbolSpecies (Sebastien Ahkrin) [#30950](https://github.com/nodejs/node/pull/30950)
* \[[`e01433db8b`](https://github.com/nodejs/node/commit/e01433db8b)] - **lib**: replace Symbol.hasInstance by SymbolHasInstance (Sebastien Ahkrin) [#30948](https://github.com/nodejs/node/pull/30948)
* \[[`497a1c8405`](https://github.com/nodejs/node/commit/497a1c8405)] - **lib**: replace Symbol.asyncIterator by SymbolAsyncIterator (Sebastien Ahkrin) [#30947](https://github.com/nodejs/node/pull/30947)
* \[[`a8a04efac8`](https://github.com/nodejs/node/commit/a8a04efac8)] - **lib**: enforce use of Promise from primordials (Michaël Zasso) [#30936](https://github.com/nodejs/node/pull/30936)
* \[[`1092e0b6fb`](https://github.com/nodejs/node/commit/1092e0b6fb)] - **lib**: add TypedArray constructors to primordials (Sebastien Ahkrin) [#30740](https://github.com/nodejs/node/pull/30740)
* \[[`3348fe40e4`](https://github.com/nodejs/node/commit/3348fe40e4)] - **lib**: replace Symbol.toPrimitive to SymbolToPrimitive primordials (Sebastien Ahkrin) [#30905](https://github.com/nodejs/node/pull/30905)
* \[[`06776496b4`](https://github.com/nodejs/node/commit/06776496b4)] - **lib**: update Symbol.toStringTag by SymbolToStringTag primordial (Sebastien Ahkrin) [#30908](https://github.com/nodejs/node/pull/30908)
* \[[`b92511d8e6`](https://github.com/nodejs/node/commit/b92511d8e6)] - **lib**: enforce use of BigInt from primordials (Michaël Zasso) [#30882](https://github.com/nodejs/node/pull/30882)
* \[[`fb18ad4e60`](https://github.com/nodejs/node/commit/fb18ad4e60)] - **lib**: replace Symbol.iterator by SymbolIterator (Sebastien Ahkrin) [#30859](https://github.com/nodejs/node/pull/30859)
* \[[`993c419ce7`](https://github.com/nodejs/node/commit/993c419ce7)] - **lib**: replace every Symbol.for by SymbolFor primordials (Sebastien Ahkrin) [#30857](https://github.com/nodejs/node/pull/30857)
* \[[`b95c50b392`](https://github.com/nodejs/node/commit/b95c50b392)] - **lib**: replace Symbol global by the primordials Symbol (Sebastien Ahkrin) [#30737](https://github.com/nodejs/node/pull/30737)
* \[[`8f911fe747`](https://github.com/nodejs/node/commit/8f911fe747)] - **lib**: enforce use of primordial Number (Sebastien Ahkrin) [#30700](https://github.com/nodejs/node/pull/30700)
* \[[`099edea144`](https://github.com/nodejs/node/commit/099edea144)] - **lib**: use static Number properties from primordials (Michaël Zasso) [#30686](https://github.com/nodejs/node/pull/30686)
* \[[`41510491f2`](https://github.com/nodejs/node/commit/41510491f2)] - **lib**: enforce use of Boolean from primordials (Michaël Zasso) [#30698](https://github.com/nodejs/node/pull/30698)
* \[[`d3e1c5864b`](https://github.com/nodejs/node/commit/d3e1c5864b)] - **lib**: replace Date.now function by primordial DateNow (Tchoupinax) [#30689](https://github.com/nodejs/node/pull/30689)
* \[[`e53f5afdbe`](https://github.com/nodejs/node/commit/e53f5afdbe)] - **lib**: replace ArrayBuffer.isView by primordial ArrayBuffer (Vincent Dhennin) [#30692](https://github.com/nodejs/node/pull/30692)
* \[[`9260844e91`](https://github.com/nodejs/node/commit/9260844e91)] - **lib**: enforce use of Array from primordials (Michaël Zasso) [#30635](https://github.com/nodejs/node/pull/30635)
* \[[`e2ae4c1aa1`](https://github.com/nodejs/node/commit/e2ae4c1aa1)] - **lib**: flatten access to primordials (Michaël Zasso) [#30610](https://github.com/nodejs/node/pull/30610)
* \[[`1c2d699ed8`](https://github.com/nodejs/node/commit/1c2d699ed8)] - **lib**: use strict equality comparison (Donggeon Lim) [#30898](https://github.com/nodejs/node/pull/30898)
* \[[`1698a53ed1`](https://github.com/nodejs/node/commit/1698a53ed1)] - **lib**: add parent to ERR\_UNKNOWN\_FILE\_EXTENSION (qualitymanifest) [#30728](https://github.com/nodejs/node/pull/30728)
* \[[`bcd27f7300`](https://github.com/nodejs/node/commit/bcd27f7300)] - **lib**: no need to strip BOM or shebang for scripts (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`1c50714729`](https://github.com/nodejs/node/commit/1c50714729)] - **lib**: rework logic of stripping BOM+Shebang from commonjs (Gus Caplan) [#27768](https://github.com/nodejs/node/pull/27768)
* \[[`e98509b67c`](https://github.com/nodejs/node/commit/e98509b67c)] - **lib,test**: improves ERR\_REQUIRE\_ESM message (Juan José Arboleda) [#30694](https://github.com/nodejs/node/pull/30694)
* \[[`1607d38b22`](https://github.com/nodejs/node/commit/1607d38b22)] - **meta**: clarify scope of new nodejs.org issue choice (Derek Lewis) [#31123](https://github.com/nodejs/node/pull/31123)
* \[[`624ed0eed4`](https://github.com/nodejs/node/commit/624ed0eed4)] - **module**: fix check exports issue in cjs module loading (Guy Bedford) [#31427](https://github.com/nodejs/node/pull/31427)
* \[[`60490f441a`](https://github.com/nodejs/node/commit/60490f441a)] - **(SEMVER-MINOR)** **module**: unflag conditional exports (Guy Bedford) [#31001](https://github.com/nodejs/node/pull/31001)
* \[[`fcbc7756fe`](https://github.com/nodejs/node/commit/fcbc7756fe)] - **module**: logical conditional exports ordering (Guy Bedford) [#31008](https://github.com/nodejs/node/pull/31008)
* \[[`c6300e15bc`](https://github.com/nodejs/node/commit/c6300e15bc)] - **module**: loader getSource, getFormat, transform hooks (Geoffrey Booth) [#30986](https://github.com/nodejs/node/pull/30986)
* \[[`e5437ef355`](https://github.com/nodejs/node/commit/e5437ef355)] - **module**: fix require in node repl (Yongsheng Zhang) [#30835](https://github.com/nodejs/node/pull/30835)
* \[[`3d1ca78144`](https://github.com/nodejs/node/commit/3d1ca78144)] - **module**: reduce circular dependency of internal/modules/cjs/loader (Joyee Cheung) [#30349](https://github.com/nodejs/node/pull/30349)
* \[[`cad5c2bc6e`](https://github.com/nodejs/node/commit/cad5c2bc6e)] - **module**: fix dynamic import from eval (Corey Farrell) [#30624](https://github.com/nodejs/node/pull/30624)
* \[[`77c69f51a3`](https://github.com/nodejs/node/commit/77c69f51a3)] - **module**: fixup lint and test regressions (Guy Bedford) [#30802](https://github.com/nodejs/node/pull/30802)
* \[[`d65566a052`](https://github.com/nodejs/node/commit/d65566a052)] - **module**: fix specifier resolution algorithm (Rongjian Zhang) [#30574](https://github.com/nodejs/node/pull/30574)
* \[[`657a8af8a9`](https://github.com/nodejs/node/commit/657a8af8a9)] - **module**: unflag resolve self (Guy Bedford) [#31002](https://github.com/nodejs/node/pull/31002)
* \[[`7f4ee67435`](https://github.com/nodejs/node/commit/7f4ee67435)] - **module**: self resolve bug fix and esm ordering (Guy Bedford) [#31009](https://github.com/nodejs/node/pull/31009)
* \[[`bb06225341`](https://github.com/nodejs/node/commit/bb06225341)] - **module**: conditional exports import condition (Guy Bedford) [#30799](https://github.com/nodejs/node/pull/30799)
* \[[`b830f44ade`](https://github.com/nodejs/node/commit/b830f44ade)] - **module**: ignore resolution failures for inspect-brk (Maël Nison) [#30336](https://github.com/nodejs/node/pull/30336)
* \[[`dc084f9e33`](https://github.com/nodejs/node/commit/dc084f9e33)] - **module**: add warnings for experimental flags (Rongjian Zhang) [#30617](https://github.com/nodejs/node/pull/30617)
* \[[`680ae770ab`](https://github.com/nodejs/node/commit/680ae770ab)] - **module**: conditional exports with flagged conditions (Guy Bedford) [#29978](https://github.com/nodejs/node/pull/29978)
* \[[`02c4d27007`](https://github.com/nodejs/node/commit/02c4d27007)] - **module**: refactor modules bootstrap (Bradley Farias) [#29937](https://github.com/nodejs/node/pull/29937)
* \[[`121c845714`](https://github.com/nodejs/node/commit/121c845714)] - **(SEMVER-MINOR)** **module**: resolve self-references (Jan Krems) [#29327](https://github.com/nodejs/node/pull/29327)
* \[[`b5d42aeac4`](https://github.com/nodejs/node/commit/b5d42aeac4)] - **(SEMVER-MINOR)** **n-api**: implement napi\_is\_detached\_arraybuffer (Denys Otrishko) [#30613](https://github.com/nodejs/node/pull/30613)
* \[[`af5c489f39`](https://github.com/nodejs/node/commit/af5c489f39)] - **n-api**: keep napi\_env alive while it has finalizers (Anna Henningsen) [#31140](https://github.com/nodejs/node/pull/31140)
* \[[`cab905f5ef`](https://github.com/nodejs/node/commit/cab905f5ef)] - **(SEMVER-MINOR)** **n-api**: add `napi\_detach\_arraybuffer` (legendecas) [#29768](https://github.com/nodejs/node/pull/29768)
* \[[`7952154e5e`](https://github.com/nodejs/node/commit/7952154e5e)] - **net**: remove duplicate \_undestroy (Robert Nagy) [#30833](https://github.com/nodejs/node/pull/30833)
* \[[`75972da470`](https://github.com/nodejs/node/commit/75972da470)] - **net**: implement capture rejections for 'connection' event (Matteo Collina) [#27867](https://github.com/nodejs/node/pull/27867)
* \[[`35b7ba6e7a`](https://github.com/nodejs/node/commit/35b7ba6e7a)] - **perf\_hooks**: use for...of (Trivikram Kamat) [#31049](https://github.com/nodejs/node/pull/31049)
* \[[`61a8af78fe`](https://github.com/nodejs/node/commit/61a8af78fe)] - **(SEMVER-MINOR)** **perf\_hooks**: move perf\_hooks out of experimental (legendecas) [#31101](https://github.com/nodejs/node/pull/31101)
* \[[`25ba7f4d7c`](https://github.com/nodejs/node/commit/25ba7f4d7c)] - **perf\_hooks**: remove unnecessary bind (Anatoli Papirovski) [#28131](https://github.com/nodejs/node/pull/28131)
* \[[`1bcbc70ea8`](https://github.com/nodejs/node/commit/1bcbc70ea8)] - **process**: refs --unhandled-rejections documentation in warning message (Antoine du HAMEL) [#30564](https://github.com/nodejs/node/pull/30564)
* \[[`5eafe3b5cb`](https://github.com/nodejs/node/commit/5eafe3b5cb)] - **process**: fix promise catching (Rongjian Zhang) [#30957](https://github.com/nodejs/node/pull/30957)
* \[[`7a8232a041`](https://github.com/nodejs/node/commit/7a8232a041)] - **(SEMVER-MINOR)** **readline**: promote \_getCursorPos to public api (Jeremy Albright) [#30687](https://github.com/nodejs/node/pull/30687)
* \[[`835151dadd`](https://github.com/nodejs/node/commit/835151dadd)] - **readline**: eagerly load string\_decoder (Ruben Bridgewater) [#30807](https://github.com/nodejs/node/pull/30807)
* \[[`c978396ee1`](https://github.com/nodejs/node/commit/c978396ee1)] - **repl**: use better uncaught exceptions indicator (Ruben Bridgewater) [#29676](https://github.com/nodejs/node/pull/29676)
* \[[`5ee105c9af`](https://github.com/nodejs/node/commit/5ee105c9af)] - **repl**: fix autocomplete when useGlobal is false (Michaël Zasso) [#30883](https://github.com/nodejs/node/pull/30883)
* \[[`106e5ce581`](https://github.com/nodejs/node/commit/106e5ce581)] - **repl**: fix referrer for dynamic import (Corey Farrell) [#30609](https://github.com/nodejs/node/pull/30609)
* \[[`7fc6984c83`](https://github.com/nodejs/node/commit/7fc6984c83)] - **(SEMVER-MINOR)** **repl**: check for NODE\_REPL\_EXTERNAL\_MODULE (Gus Caplan) [#29778](https://github.com/nodejs/node/pull/29778)
* \[[`7a855f57b8`](https://github.com/nodejs/node/commit/7a855f57b8)] - **src**: accept single argument in getProxyDetails (Ruben Bridgewater) [#30858](https://github.com/nodejs/node/pull/30858)
* \[[`9d60499bfb`](https://github.com/nodejs/node/commit/9d60499bfb)] - **src**: mark ArrayBuffers with free callbacks as untransferable (Anna Henningsen) [#30475](https://github.com/nodejs/node/pull/30475)
* \[[`a4c7eba474`](https://github.com/nodejs/node/commit/a4c7eba474)] - **src**: prevent hard coding stack trace limit (legendecas) [#30752](https://github.com/nodejs/node/pull/30752)
* \[[`65e5a8a90c`](https://github.com/nodejs/node/commit/65e5a8a90c)] - **src**: unregister Isolate with platform before disposing (Anna Henningsen) [#30909](https://github.com/nodejs/node/pull/30909)
* \[[`bf789145d9`](https://github.com/nodejs/node/commit/bf789145d9)] - **src**: free preopen memory in WASI::New() (Colin Ihrig) [#30809](https://github.com/nodejs/node/pull/30809)
* \[[`e9bda6618d`](https://github.com/nodejs/node/commit/e9bda6618d)] - **src**: use checked allocations in WASI::New() (Colin Ihrig) [#30809](https://github.com/nodejs/node/pull/30809)
* \[[`29608beb82`](https://github.com/nodejs/node/commit/29608beb82)] - _**Revert**_ "**src**: update v8abbr.h for V8 7.7" (Matheus Marchini) [#30870](https://github.com/nodejs/node/pull/30870)
* \[[`5edd1a229b`](https://github.com/nodejs/node/commit/5edd1a229b)] - **src**: suppress warning in src/node\_env\_var.cc (Harshitha KP) [#31136](https://github.com/nodejs/node/pull/31136)
* \[[`1b04e678ed`](https://github.com/nodejs/node/commit/1b04e678ed)] - **src**: enable stack trace printing for V8 check failures (Anna Henningsen) [#31079](https://github.com/nodejs/node/pull/31079)
* \[[`715c158f2c`](https://github.com/nodejs/node/commit/715c158f2c)] - **src**: port --bash-completion to C++ (Joyee Cheung) [#25901](https://github.com/nodejs/node/pull/25901)
* \[[`f71b09fb27`](https://github.com/nodejs/node/commit/f71b09fb27)] - **src**: list used functions on headers (Juan José Arboleda) [#30827](https://github.com/nodejs/node/pull/30827)
* \[[`91b72b3794`](https://github.com/nodejs/node/commit/91b72b3794)] - **src**: fix compiler warning in env.cc (Anna Henningsen) [#31020](https://github.com/nodejs/node/pull/31020)
* \[[`24a5929fae`](https://github.com/nodejs/node/commit/24a5929fae)] - **src**: make debug\_options getters public (Shelley Vohr) [#30494](https://github.com/nodejs/node/pull/30494)
* \[[`e00c4e41b8`](https://github.com/nodejs/node/commit/e00c4e41b8)] - **src**: fix the false isatty() issue on IBMi (Xu Meng) [#30829](https://github.com/nodejs/node/pull/30829)
* \[[`d3c792997a`](https://github.com/nodejs/node/commit/d3c792997a)] - **src**: improve checked uv loop close output (Anna Henningsen) [#30814](https://github.com/nodejs/node/pull/30814)
* \[[`0c066dc610`](https://github.com/nodejs/node/commit/0c066dc610)] - **src**: port memory-tracking allocator from QUIC repo (Anna Henningsen) [#30745](https://github.com/nodejs/node/pull/30745)
* \[[`721ebf0487`](https://github.com/nodejs/node/commit/721ebf0487)] - **src**: don't use deprecated OpenSSL APIs (Rosen Penev) [#30812](https://github.com/nodejs/node/pull/30812)
* \[[`b233c36c10`](https://github.com/nodejs/node/commit/b233c36c10)] - **src**: delete redundant method in node\_dir.h (gengjiawen) [#30747](https://github.com/nodejs/node/pull/30747)
* \[[`214042cd2f`](https://github.com/nodejs/node/commit/214042cd2f)] - **src**: remove redundant cast in node\_dir.cc (gengjiawen) [#30747](https://github.com/nodejs/node/pull/30747)
* \[[`bd380d55d0`](https://github.com/nodejs/node/commit/bd380d55d0)] - **src**: improve node\_crypto.cc memory allocation (Priyanka Kore) [#30751](https://github.com/nodejs/node/pull/30751)
* \[[`19eb8e0268`](https://github.com/nodejs/node/commit/19eb8e0268)] - **src**: fix node\_dir.cc memory allocation (Priyanka Kore) [#30750](https://github.com/nodejs/node/pull/30750)
* \[[`78098d3859`](https://github.com/nodejs/node/commit/78098d3859)] - **src**: change header file in node\_stat\_watcher.cc (Reza Fatahi) [#29976](https://github.com/nodejs/node/pull/29976)
* \[[`33064a14e4`](https://github.com/nodejs/node/commit/33064a14e4)] - **src**: clean up node\_file.h (Anna Henningsen) [#30530](https://github.com/nodejs/node/pull/30530)
* \[[`3513243998`](https://github.com/nodejs/node/commit/3513243998)] - **src**: fix -Wsign-compare warnings (Colin Ihrig) [#30565](https://github.com/nodejs/node/pull/30565)
* \[[`50b7f840a1`](https://github.com/nodejs/node/commit/50b7f840a1)] - **src**: use uv\_async\_t for WeakRefs (Anna Henningsen) [#30616](https://github.com/nodejs/node/pull/30616)
* \[[`8d6a90384d`](https://github.com/nodejs/node/commit/8d6a90384d)] - **(SEMVER-MINOR)** **src**: expose ArrayBuffer version of Buffer::New() (Anna Henningsen) [#30476](https://github.com/nodejs/node/pull/30476)
* \[[`a81eae67ec`](https://github.com/nodejs/node/commit/a81eae67ec)] - **(SEMVER-MINOR)** **src**: expose ability to set options (Shelley Vohr) [#30466](https://github.com/nodejs/node/pull/30466)
* \[[`af787b87ae`](https://github.com/nodejs/node/commit/af787b87ae)] - **(SEMVER-MINOR)** **src**: allow adding linked bindings to Environment (Anna Henningsen) [#30274](https://github.com/nodejs/node/pull/30274)
* \[[`60ac18ccd7`](https://github.com/nodejs/node/commit/60ac18ccd7)] - **(SEMVER-MINOR)** **src**: deprecate two- and one-argument AtExit() (Anna Henningsen) [#30227](https://github.com/nodejs/node/pull/30227)
* \[[`455a643c33`](https://github.com/nodejs/node/commit/455a643c33)] - **(SEMVER-MINOR)** **src**: expose granular SetIsolateUpForNode (Shelley Vohr) [#30150](https://github.com/nodejs/node/pull/30150)
* \[[`a5d2b66bdc`](https://github.com/nodejs/node/commit/a5d2b66bdc)] - **src**: do not use `std::function` for `OnScopeLeave` (Anna Henningsen) [#30134](https://github.com/nodejs/node/pull/30134)
* \[[`08e55e302b`](https://github.com/nodejs/node/commit/08e55e302b)] - **src**: remove AsyncScope and AsyncCallbackScope (Anna Henningsen) [#30236](https://github.com/nodejs/node/pull/30236)
* \[[`ce13d43819`](https://github.com/nodejs/node/commit/ce13d43819)] - **src**: use callback scope for main script (Anna Henningsen) [#30236](https://github.com/nodejs/node/pull/30236)
* \[[`e11a376677`](https://github.com/nodejs/node/commit/e11a376677)] - **src**: fix crash with SyntheticModule#setExport (Michaël Zasso) [#30062](https://github.com/nodejs/node/pull/30062)
* \[[`e7c9042c72`](https://github.com/nodejs/node/commit/e7c9042c72)] - **src**: implement v8 host weakref hooks (Gus Caplan) [#29874](https://github.com/nodejs/node/pull/29874)
* \[[`330eb10404`](https://github.com/nodejs/node/commit/330eb10404)] - **src**: make large\_pages node.cc include conditional (Denys Otrishko) [#31078](https://github.com/nodejs/node/pull/31078)
* \[[`84863994a3`](https://github.com/nodejs/node/commit/84863994a3)] - **src**: make --use-largepages a runtime option (Gabriel Schulhof) [#30954](https://github.com/nodejs/node/pull/30954)
* \[[`ec3c39f54b`](https://github.com/nodejs/node/commit/ec3c39f54b)] - **src,test**: use v8::Global instead of v8::Persistent (Anna Henningsen) [#31018](https://github.com/nodejs/node/pull/31018)
* \[[`aad2578325`](https://github.com/nodejs/node/commit/aad2578325)] - **stream**: group all properties using defineProperties (antsmartian) [#31144](https://github.com/nodejs/node/pull/31144)
* \[[`823ee2be05`](https://github.com/nodejs/node/commit/823ee2be05)] - **stream**: reset flowing state if no 'readable' or 'data' listeners (Robert Nagy) [#31036](https://github.com/nodejs/node/pull/31036)
* \[[`b12b9305f0`](https://github.com/nodejs/node/commit/b12b9305f0)] - **stream**: simplify isBuf (Robert Nagy) [#31067](https://github.com/nodejs/node/pull/31067)
* \[[`3872a02020`](https://github.com/nodejs/node/commit/3872a02020)] - **stream**: use for...of (Trivikram Kamat) [#30960](https://github.com/nodejs/node/pull/30960)
* \[[`322912aa65`](https://github.com/nodejs/node/commit/322912aa65)] - **stream**: do not chunk strings and Buffer in Readable.from (Matteo Collina) [#30912](https://github.com/nodejs/node/pull/30912)
* \[[`d627724e9d`](https://github.com/nodejs/node/commit/d627724e9d)] - **stream**: add support for captureRejection option (Matteo Collina) [#27867](https://github.com/nodejs/node/pull/27867)
* \[[`369b7c235f`](https://github.com/nodejs/node/commit/369b7c235f)] - **stream**: use more accurate end-of-stream writable and readable detection (Stewart X Addison) [#29409](https://github.com/nodejs/node/pull/29409)
* \[[`55ca3a86b7`](https://github.com/nodejs/node/commit/55ca3a86b7)] - **(SEMVER-MINOR)** **stream**: add writableCorked to Duplex (Anna Henningsen) [#29053](https://github.com/nodejs/node/pull/29053)
* \[[`af960d7f22`](https://github.com/nodejs/node/commit/af960d7f22)] - **(SEMVER-MINOR)** **stream**: add writableCorked property (Robert Nagy) [#29012](https://github.com/nodejs/node/pull/29012)
* \[[`6e17ea4788`](https://github.com/nodejs/node/commit/6e17ea4788)] - **test**: change buffer offset to accommodate V8 BackingStore (Thang Tran) [#31171](https://github.com/nodejs/node/pull/31171)
* \[[`b3e0bc2e91`](https://github.com/nodejs/node/commit/b3e0bc2e91)] - **test**: get lib/wasi.js coverage to 100% (Colin Ihrig) [#31039](https://github.com/nodejs/node/pull/31039)
* \[[`07d195ab5f`](https://github.com/nodejs/node/commit/07d195ab5f)] - **test**: cover vm with negative tests (Andrew Kuzmenko) [#31028](https://github.com/nodejs/node/pull/31028)
* \[[`b2caac25a7`](https://github.com/nodejs/node/commit/b2caac25a7)] - **test**: remove obsolete WASI test (Colin Ihrig) [#30980](https://github.com/nodejs/node/pull/30980)
* \[[`ceca54940b`](https://github.com/nodejs/node/commit/ceca54940b)] - **test**: simplify test-wasi-start-validation.js (Colin Ihrig) [#30972](https://github.com/nodejs/node/pull/30972)
* \[[`be3fd2e714`](https://github.com/nodejs/node/commit/be3fd2e714)] - **test**: improve WASI start() coverage (Colin Ihrig) [#30972](https://github.com/nodejs/node/pull/30972)
* \[[`a4b6668877`](https://github.com/nodejs/node/commit/a4b6668877)] - **test**: add missing test flags (Colin Ihrig) [#30971](https://github.com/nodejs/node/pull/30971)
* \[[`8f5ffffb61`](https://github.com/nodejs/node/commit/8f5ffffb61)] - **test**: add test for validation for wasi.start() argument (Rich Trott) [#30919](https://github.com/nodejs/node/pull/30919)
* \[[`5fff46a531`](https://github.com/nodejs/node/commit/5fff46a531)] - **test**: work around ENOTEMPTY when cleaning tmp dir (Ben Noordhuis) [#30849](https://github.com/nodejs/node/pull/30849)
* \[[`5bffe11cab`](https://github.com/nodejs/node/commit/5bffe11cab)] - **test**: wait for stream close before writing to file (Anna Henningsen) [#30836](https://github.com/nodejs/node/pull/30836)
* \[[`6725fa11c7`](https://github.com/nodejs/node/commit/6725fa11c7)] - **test**: use fs rimraf to refresh tmpdir (Colin Ihrig) [#30569](https://github.com/nodejs/node/pull/30569)
* \[[`b5d59a726b`](https://github.com/nodejs/node/commit/b5d59a726b)] - **test**: improve WASI options validation (Rich Trott) [#30800](https://github.com/nodejs/node/pull/30800)
* \[[`6086a1dd61`](https://github.com/nodejs/node/commit/6086a1dd61)] - **test**: run more assert tests (Ruben Bridgewater) [#30764](https://github.com/nodejs/node/pull/30764)
* \[[`45b74fbb5b`](https://github.com/nodejs/node/commit/45b74fbb5b)] - **test**: improve wasi test coverage (Rich Trott) [#30770](https://github.com/nodejs/node/pull/30770)
* \[[`fd33df7f33`](https://github.com/nodejs/node/commit/fd33df7f33)] - **test**: simplify tmpdir import in wasi tests (Rich Trott) [#30770](https://github.com/nodejs/node/pull/30770)
* \[[`25b88acd0f`](https://github.com/nodejs/node/commit/25b88acd0f)] - **test**: use arrow functions in addons tests (garygsc) [#30131](https://github.com/nodejs/node/pull/30131)
* \[[`023a802f20`](https://github.com/nodejs/node/commit/023a802f20)] - _**Revert**_ "**test**: update postmortem metadata test for V8 7.7" (Matheus Marchini) [#30870](https://github.com/nodejs/node/pull/30870)
* \[[`05ef4bdd35`](https://github.com/nodejs/node/commit/05ef4bdd35)] - **test**: use spread object (Fran Herrero) [#30423](https://github.com/nodejs/node/pull/30423)
* \[[`1d3405b05a`](https://github.com/nodejs/node/commit/1d3405b05a)] - **test**: log errors in test-http2-propagate-session-destroy-code (Denys Otrishko) [#31072](https://github.com/nodejs/node/pull/31072)
* \[[`c7dafd8958`](https://github.com/nodejs/node/commit/c7dafd8958)] - **test**: skip the unsupported test cases for IBM i (Xu Meng) [#30819](https://github.com/nodejs/node/pull/30819)
* \[[`2de25f18cd`](https://github.com/nodejs/node/commit/2de25f18cd)] - **test**: unflake async hooks statwatcher test (Denys Otrishko) [#30362](https://github.com/nodejs/node/pull/30362)
* \[[`e68d86c195`](https://github.com/nodejs/node/commit/e68d86c195)] - **test**: fix common.enoughTestMem (Rich Trott) [#31035](https://github.com/nodejs/node/pull/31035)
* \[[`027a2dc3ec`](https://github.com/nodejs/node/commit/027a2dc3ec)] - **test**: fix long lines (Colin Ihrig) [#31014](https://github.com/nodejs/node/pull/31014)
* \[[`fa677ca5c5`](https://github.com/nodejs/node/commit/fa677ca5c5)] - **test**: fix flaky test-http2-client-upload (Gerhard Stoebich) [#29889](https://github.com/nodejs/node/pull/29889)
* \[[`d633ba07e3`](https://github.com/nodejs/node/commit/d633ba07e3)] - **test**: improve test coverage in child\_process (Juan José Arboleda) [#26282](https://github.com/nodejs/node/pull/26282)
* \[[`87543f024b`](https://github.com/nodejs/node/commit/87543f024b)] - **test**: improve dns lookup coverage (Kirill Ponomarev) [#30777](https://github.com/nodejs/node/pull/30777)
* \[[`54a1078e76`](https://github.com/nodejs/node/commit/54a1078e76)] - **test**: avoid leftover report file (Gerhard Stoebich) [#30925](https://github.com/nodejs/node/pull/30925)
* \[[`d75a3f61d7`](https://github.com/nodejs/node/commit/d75a3f61d7)] - **test**: improve assertion error message in test-debug-usage (Rich Trott) [#30913](https://github.com/nodejs/node/pull/30913)
* \[[`768a53f219`](https://github.com/nodejs/node/commit/768a53f219)] - **test**: disable colorMode in test-console-group (Rich Trott) [#30886](https://github.com/nodejs/node/pull/30886)
* \[[`a22d5e78e1`](https://github.com/nodejs/node/commit/a22d5e78e1)] - **test**: assert: fix deepStrictEqual comparing a real array and fake array (Jordan Harband) [#30743](https://github.com/nodejs/node/pull/30743)
* \[[`0dae8feefd`](https://github.com/nodejs/node/commit/0dae8feefd)] - **test**: refactor test-accessor-properties (himself65) [#29943](https://github.com/nodejs/node/pull/29943)
* \[[`55a1a90fed`](https://github.com/nodejs/node/commit/55a1a90fed)] - **test**: scale keepalive timeouts for slow machines (Ben Noordhuis) [#30834](https://github.com/nodejs/node/pull/30834)
* \[[`2d39ed97b9`](https://github.com/nodejs/node/commit/2d39ed97b9)] - **test**: mark tests as flaky (João Reis) [#30848](https://github.com/nodejs/node/pull/30848)
* \[[`fe3818e016`](https://github.com/nodejs/node/commit/fe3818e016)] - **test**: mark addons/openssl-bindings/test flaky on arm (Richard Lau) [#30838](https://github.com/nodejs/node/pull/30838)
* \[[`d15a2b6c19`](https://github.com/nodejs/node/commit/d15a2b6c19)] - **test**: remove common.busyLoop() (Colin Ihrig) [#30787](https://github.com/nodejs/node/pull/30787)
* \[[`4bb8d6aa03`](https://github.com/nodejs/node/commit/4bb8d6aa03)] - **test**: use callback arguments in getconnections test (Rich Trott) [#30775](https://github.com/nodejs/node/pull/30775)
* \[[`35919999ce`](https://github.com/nodejs/node/commit/35919999ce)] - **test**: remove duplicate entries from root.status (Richard Lau) [#30769](https://github.com/nodejs/node/pull/30769)
* \[[`d3004aacbf`](https://github.com/nodejs/node/commit/d3004aacbf)] - **test**: increase debugging information in subprocess test (Rich Trott) [#30761](https://github.com/nodejs/node/pull/30761)
* \[[`7dc8237de1`](https://github.com/nodejs/node/commit/7dc8237de1)] - **test**: use block-scoping in test-net-server-address (Rich Trott) [#30754](https://github.com/nodejs/node/pull/30754)
* \[[`13629368a4`](https://github.com/nodejs/node/commit/13629368a4)] - **test**: move test-child-process-fork-getconnections to parallel (Rich Trott) [#30749](https://github.com/nodejs/node/pull/30749)
* \[[`096e3378ec`](https://github.com/nodejs/node/commit/096e3378ec)] - **test**: change common.PORT to arbitrary port (Rich Trott) [#30749](https://github.com/nodejs/node/pull/30749)
* \[[`860139408d`](https://github.com/nodejs/node/commit/860139408d)] - **test**: update and harden http2-reset-flood (Denys Otrishko) [#30534](https://github.com/nodejs/node/pull/30534)
* \[[`24f7335772`](https://github.com/nodejs/node/commit/24f7335772)] - **test**: cover 'close' method in Dir class (Artem Maksimov) [#30310](https://github.com/nodejs/node/pull/30310)
* \[[`b87ae6dd43`](https://github.com/nodejs/node/commit/b87ae6dd43)] - **test**: use tmpdir.refresh() in test-esm-windows.js (Richard Lau) [#30997](https://github.com/nodejs/node/pull/30997)
* \[[`0ca80446c4`](https://github.com/nodejs/node/commit/0ca80446c4)] - **test**: make test-os-checked-function work without test harness (Rich Trott) [#30914](https://github.com/nodejs/node/pull/30914)
* \[[`20c8a0a5ac`](https://github.com/nodejs/node/commit/20c8a0a5ac)] - **test**: delay loading 'os' in test/common module (Rich Trott) [#30914](https://github.com/nodejs/node/pull/30914)
* \[[`71a3f485ba`](https://github.com/nodejs/node/commit/71a3f485ba)] - **test**: remove AtExit() addon test (Anna Henningsen) [#30275](https://github.com/nodejs/node/pull/30275)
* \[[`e0eb670f0e`](https://github.com/nodejs/node/commit/e0eb670f0e)] - **test**: revert 6d022c13 (Anna Henningsen) [#30708](https://github.com/nodejs/node/pull/30708)
* \[[`ac9a8933c9`](https://github.com/nodejs/node/commit/ac9a8933c9)] - **test,module**: add test for exports cjs loader check (Rich Trott) [#31427](https://github.com/nodejs/node/pull/31427)
* \[[`f86862a2f5`](https://github.com/nodejs/node/commit/f86862a2f5)] - **timers**: fix refresh for expired timers (Anatoli Papirovski) [#27345](https://github.com/nodejs/node/pull/27345)
* \[[`a4cbd57356`](https://github.com/nodejs/node/commit/a4cbd57356)] - **timers**: do less work in insert (Anatoli Papirovski) [#27345](https://github.com/nodejs/node/pull/27345)
* \[[`cd700ffcba`](https://github.com/nodejs/node/commit/cd700ffcba)] - **tls**: for...of in \_tls\_common.js (Trivikram Kamat) [#30961](https://github.com/nodejs/node/pull/30961)
* \[[`ba18406402`](https://github.com/nodejs/node/commit/ba18406402)] - **tls**: implement capture rejections for 'secureConnection' event (Matteo Collina) [#27867](https://github.com/nodejs/node/pull/27867)
* \[[`b94172c9f7`](https://github.com/nodejs/node/commit/b94172c9f7)] - **(SEMVER-MINOR)** **tls**: add PSK support (Denys Otrishko) [#23188](https://github.com/nodejs/node/pull/23188)
* \[[`693099cfae`](https://github.com/nodejs/node/commit/693099cfae)] - **(SEMVER-MINOR)** **tls**: expose IETF name for current cipher suite (Sam Roberts) [#30637](https://github.com/nodejs/node/pull/30637)
* \[[`8a9243afce`](https://github.com/nodejs/node/commit/8a9243afce)] - **tls**: introduce ERR\_TLS\_INVALID\_CONTEXT (Rich Trott) [#30718](https://github.com/nodejs/node/pull/30718)
* \[[`e80103a4cb`](https://github.com/nodejs/node/commit/e80103a4cb)] - **(SEMVER-MINOR)** **tls**: cli option to enable TLS key logging to file (Sam Roberts) [#30055](https://github.com/nodejs/node/pull/30055)
* \[[`1974fd94ed`](https://github.com/nodejs/node/commit/1974fd94ed)] - **tools**: allow the travis commit message job to fail (Ruben Bridgewater) [#31116](https://github.com/nodejs/node/pull/31116)
* \[[`340ce925a8`](https://github.com/nodejs/node/commit/340ce925a8)] - **tools**: fix Raspbian armv7 build (Andrey Hohutkin) [#31041](https://github.com/nodejs/node/pull/31041)
* \[[`56fb29a146`](https://github.com/nodejs/node/commit/56fb29a146)] - **tools**: update ESLint to 6.8.0 (Colin Ihrig) [#31044](https://github.com/nodejs/node/pull/31044)
* \[[`f2cc093d4a`](https://github.com/nodejs/node/commit/f2cc093d4a)] - **tools**: enable Markdown linter's usage information (Derek Lewis) [#30216](https://github.com/nodejs/node/pull/30216)
* \[[`7fabf77f7d`](https://github.com/nodejs/node/commit/7fabf77f7d)] - **tools**: update link to google styleguide for cpplint (Daniel Bevenius) [#30876](https://github.com/nodejs/node/pull/30876)
* \[[`21ccbddd5a`](https://github.com/nodejs/node/commit/21ccbddd5a)] - **tools**: use CC instead of CXX when pointing to gcc (Milad Farazmand) [#30817](https://github.com/nodejs/node/pull/30817)
* \[[`1f138f3450`](https://github.com/nodejs/node/commit/1f138f3450)] - **tools**: update remark-preset-lint-node to 1.11.0 (Rich Trott) [#30789](https://github.com/nodejs/node/pull/30789)
* \[[`fcecd09d6d`](https://github.com/nodejs/node/commit/fcecd09d6d)] - **tools**: update ESLint to 6.7.2 (Rich Trott) [#30762](https://github.com/nodejs/node/pull/30762)
* \[[`b09eda55f7`](https://github.com/nodejs/node/commit/b09eda55f7)] - **tools**: update remark-preset-lint-node to 1.10.1 (Rich Trott) [#29982](https://github.com/nodejs/node/pull/29982)
* \[[`9acff553f9`](https://github.com/nodejs/node/commit/9acff553f9)] - **tools**: update remark-preset-lint-node to 1.10.0 (Rich Trott) [#29594](https://github.com/nodejs/node/pull/29594)
* \[[`e8176b0841`](https://github.com/nodejs/node/commit/e8176b0841)] - **tools**: apply more stringent blank-line linting for markdown files (Rich Trott) [#29447](https://github.com/nodejs/node/pull/29447)
* \[[`a19e9bd933`](https://github.com/nodejs/node/commit/a19e9bd933)] - **tools**: patch V8 to run on older XCode versions (Ujjwal Sharma) [#29694](https://github.com/nodejs/node/pull/29694)
* \[[`0df49106d5`](https://github.com/nodejs/node/commit/0df49106d5)] - **tools**: update V8 gypfiles (Michaël Zasso) [#29694](https://github.com/nodejs/node/pull/29694)
* \[[`9b1aff4448`](https://github.com/nodejs/node/commit/9b1aff4448)] - **tools,src**: forbid usage of v8::Persistent (Anna Henningsen) [#31018](https://github.com/nodejs/node/pull/31018)
* \[[`6d674d476e`](https://github.com/nodejs/node/commit/6d674d476e)] - **url**: declare iterator inside loop (Trivikram Kamat) [#30509](https://github.com/nodejs/node/pull/30509)
* \[[`a1d36db9d8`](https://github.com/nodejs/node/commit/a1d36db9d8)] - **util**: improve prototype inspection using `inspect()` and `showHidden` (Ruben Bridgewater) [#31113](https://github.com/nodejs/node/pull/31113)
* \[[`d3c0f46054`](https://github.com/nodejs/node/commit/d3c0f46054)] - **util**: add (typed) array length to the default output (Ruben Bridgewater) [#31027](https://github.com/nodejs/node/pull/31027)
* \[[`19a3f8b8b5`](https://github.com/nodejs/node/commit/19a3f8b8b5)] - **util**: refactor inspect code for constistency (Ruben Bridgewater) [#30225](https://github.com/nodejs/node/pull/30225)
* \[[`7686865174`](https://github.com/nodejs/node/commit/7686865174)] - **(SEMVER-MINOR)** **util**: inspect (user defined) prototype properties (Ruben Bridgewater) [#30768](https://github.com/nodejs/node/pull/30768)
* \[[`0376e1cf4d`](https://github.com/nodejs/node/commit/0376e1cf4d)] - **util**: fix built-in detection (Ruben Bridgewater) [#30768](https://github.com/nodejs/node/pull/30768)
* \[[`c6193fe009`](https://github.com/nodejs/node/commit/c6193fe009)] - **util**: never trigger any proxy traps using `format()` (Ruben Bridgewater) [#30767](https://github.com/nodejs/node/pull/30767)
* \[[`963c14c05c`](https://github.com/nodejs/node/commit/963c14c05c)] - **util**: improve performance inspecting proxies (Ruben Bridgewater) [#30767](https://github.com/nodejs/node/pull/30767)
* \[[`0074790b9c`](https://github.com/nodejs/node/commit/0074790b9c)] - **util**: fix .format() not always calling toString when it should be (Ruben Bridgewater) [#30343](https://github.com/nodejs/node/pull/30343)
* \[[`6a5580299e`](https://github.com/nodejs/node/commit/6a5580299e)] - **(SEMVER-MINOR)** **util**: add more predefined color codes to inspect.colors (Ruben Bridgewater) [#30659](https://github.com/nodejs/node/pull/30659)
* \[[`68f6841ca1`](https://github.com/nodejs/node/commit/68f6841ca1)] - **(SEMVER-MINOR)** **util**: improve inspect's customInspect performance (Ruben Bridgewater) [#30659](https://github.com/nodejs/node/pull/30659)
* \[[`fe0c830864`](https://github.com/nodejs/node/commit/fe0c830864)] - **util**: add internal sleep() function (Colin Ihrig) [#30787](https://github.com/nodejs/node/pull/30787)
* \[[`fbd9ea8471`](https://github.com/nodejs/node/commit/fbd9ea8471)] - **v8**: use of TypedArray constructors from primordials (Sebastien Ahkrin) [#30740](https://github.com/nodejs/node/pull/30740)
* \[[`c802c998bf`](https://github.com/nodejs/node/commit/c802c998bf)] - **(SEMVER-MINOR)** **vm**: add Synthetic modules (Gus Caplan) [#29864](https://github.com/nodejs/node/pull/29864)
* \[[`8c34a14d89`](https://github.com/nodejs/node/commit/8c34a14d89)] - **wasi**: refactor destructuring object on constructor (himself65) [#31185](https://github.com/nodejs/node/pull/31185)
* \[[`6bec620efc`](https://github.com/nodejs/node/commit/6bec620efc)] - **wasi**: fix serdes bugs from snapshot1 migration (Colin Ihrig) [#31122](https://github.com/nodejs/node/pull/31122)
* \[[`0d212fc9c1`](https://github.com/nodejs/node/commit/0d212fc9c1)] - **wasi**: throw on failed uvwasi\_init() (Colin Ihrig) [#31076](https://github.com/nodejs/node/pull/31076)
* \[[`d31e6d9ee6`](https://github.com/nodejs/node/commit/d31e6d9ee6)] - **wasi**: require CLI flag to require() wasi module (Colin Ihrig) [#30963](https://github.com/nodejs/node/pull/30963)
* \[[`bf789b5e3c`](https://github.com/nodejs/node/commit/bf789b5e3c)] - **wasi**: use memory-tracking allocator (Anna Henningsen) [#30745](https://github.com/nodejs/node/pull/30745)
* \[[`e2cb5d0754`](https://github.com/nodejs/node/commit/e2cb5d0754)] - **(SEMVER-MINOR)** **wasi**: introduce initial WASI support (Colin Ihrig) [#30258](https://github.com/nodejs/node/pull/30258)
* \[[`4ea1b816a4`](https://github.com/nodejs/node/commit/4ea1b816a4)] - **(SEMVER-MINOR)** **worker**: add argv constructor option (legendecas) [#30559](https://github.com/nodejs/node/pull/30559)
* \[[`7a8b659379`](https://github.com/nodejs/node/commit/7a8b659379)] - **(SEMVER-MINOR)** **worker**: allow specifying resource limits (Anna Henningsen) [#26628](https://github.com/nodejs/node/pull/26628)
* \[[`a2fa0318be`](https://github.com/nodejs/node/commit/a2fa0318be)] - **zlib**: use for...of (Trivikram Kamat) [#31051](https://github.com/nodejs/node/pull/31051)
* \[[`7cad756e0c`](https://github.com/nodejs/node/commit/7cad756e0c)] - **zlib**: allow writes after readable 'end' to finish (Anna Henningsen) [#31082](https://github.com/nodejs/node/pull/31082)

<a id="12.15.0"></a>

## 2020-02-06, Version 12.15.0 'Erbium' (LTS), @BethGriggs

### Notable changes

This is a security release.

Vulnerabilities fixed:

* **CVE-2019-15606**: HTTP header values do not have trailing OWS trimmed.
* **CVE-2019-15605**: HTTP request smuggling using malformed Transfer-Encoding header.
* **CVE-2019-15604**: Remotely trigger an assertion on a TLS server with a malformed certificate string.

Also, HTTP parsing is more strict to be more secure. Since this may
cause problems in interoperability with some non-conformant HTTP
implementations, it is possible to disable the strict checks with the
`--insecure-http-parser` command line flag, or the `insecureHTTPParser`
http option. Using the insecure HTTP parser should be avoided.

### Commits

* \[[`209767c7a2`](https://github.com/nodejs/node/commit/209767c7a2)] - **benchmark**: support optional headers with wrk (Sam Roberts) [nodejs-private/node-private#189](https://github.com/nodejs-private/node-private/pull/189)
* \[[`02c8905051`](https://github.com/nodejs/node/commit/02c8905051)] - **crypto**: fix assertion caused by unsupported ext (Fedor Indutny) [nodejs-private/node-private#175](https://github.com/nodejs-private/node-private/pull/175)
* \[[`25d6011912`](https://github.com/nodejs/node/commit/25d6011912)] - **deps**: update llhttp to 2.0.4 (Beth Griggs) [nodejs-private/llhttp-private#1](https://github.com/nodejs-private/llhttp-private/pull/1)
* \[[`8162f0e194`](https://github.com/nodejs/node/commit/8162f0e194)] - **deps**: upgrade http-parser to v2.9.3 (Sam Roberts) [nodejs-private/http-parser-private#4](https://github.com/nodejs-private/http-parser-private/pull/4)
* \[[`d41314ef99`](https://github.com/nodejs/node/commit/d41314ef99)] - **(SEMVER-MINOR)** **deps**: upgrade http-parser to v2.9.1 (Sam Roberts) [#30473](https://github.com/nodejs/node/pull/30473)
* \[[`7fc565666c`](https://github.com/nodejs/node/commit/7fc565666c)] - **(SEMVER-MINOR)** **http**: make --insecure-http-parser configurable per-stream or per-server (Anna Henningsen) [#31448](https://github.com/nodejs/node/pull/31448)
* \[[`496736ff78`](https://github.com/nodejs/node/commit/496736ff78)] - **(SEMVER-MINOR)** **http**: opt-in insecure HTTP header parsing (Sam Roberts) [#30567](https://github.com/nodejs/node/pull/30567)
* \[[`76fd8910e9`](https://github.com/nodejs/node/commit/76fd8910e9)] - **http**: strip trailing OWS from header values (Sam Roberts) [nodejs-private/node-private#189](https://github.com/nodejs-private/node-private/pull/189)
* \[[`9cd155eb4a`](https://github.com/nodejs/node/commit/9cd155eb4a)] - **test**: using TE to smuggle reqs is not possible (Sam Roberts) [nodejs-private/node-private#192](https://github.com/nodejs-private/node-private/pull/192)
* \[[`ab1fcb89cb`](https://github.com/nodejs/node/commit/ab1fcb89cb)] - **test**: check that --insecure-http-parser works (Sam Roberts) [#31253](https://github.com/nodejs/node/pull/31253)

<a id="12.14.1"></a>

## 2020-01-07, Version 12.14.1 'Erbium' (LTS), @BethGriggs

### Notable changes

* **crypto**: fix key requirements in asymmetric cipher (Tobias Nießen) [#30249](https://github.com/nodejs/node/pull/30249)
* **deps**:
  * update llhttp to 2.0.1 (Fedor Indutny) [#30553](https://github.com/nodejs/node/pull/30553)
  * update nghttp2 to 1.40.0 (gengjiawen) [#30493](https://github.com/nodejs/node/pull/30493)
* **v8**: mark serdes API as stable (Anna Henningsen) [#30234](https://github.com/nodejs/node/pull/30234)

### Commits

* \[[`ddbe9826d0`](https://github.com/nodejs/node/commit/ddbe9826d0)] - **assert**: replace var with let in lib/assert.js (PerfectPan) [#30261](https://github.com/nodejs/node/pull/30261)
* \[[`33e0bd6b7c`](https://github.com/nodejs/node/commit/33e0bd6b7c)] - **benchmark**: use let instead of var in async\_hooks (dnlup) [#30470](https://github.com/nodejs/node/pull/30470)
* \[[`351ee645c8`](https://github.com/nodejs/node/commit/351ee645c8)] - **benchmark**: use let instead of var in assert (dnlup) [#30450](https://github.com/nodejs/node/pull/30450)
* \[[`da0c6736d0`](https://github.com/nodejs/node/commit/da0c6736d0)] - **benchmark,doc,lib,test**: prepare for padding lint rule (Rich Trott) [#30696](https://github.com/nodejs/node/pull/30696)
* \[[`719e33ff40`](https://github.com/nodejs/node/commit/719e33ff40)] - **buffer**: fix 6-byte writeUIntBE() range check (Brian White) [#30459](https://github.com/nodejs/node/pull/30459)
* \[[`58fca405c1`](https://github.com/nodejs/node/commit/58fca405c1)] - **buffer**: change var to let (Vladislav Botvin) [#30292](https://github.com/nodejs/node/pull/30292)
* \[[`cc93a3742b`](https://github.com/nodejs/node/commit/cc93a3742b)] - **build**: store cache on timed out builds on Travis (Richard Lau) [#30469](https://github.com/nodejs/node/pull/30469)
* \[[`3b096c2bc4`](https://github.com/nodejs/node/commit/3b096c2bc4)] - **build,win**: propagate error codes in vcbuild (João Reis) [#30724](https://github.com/nodejs/node/pull/30724)
* \[[`f9f3ab293f`](https://github.com/nodejs/node/commit/f9f3ab293f)] - **child\_process**: document kill() return value (cjihrig) [#30669](https://github.com/nodejs/node/pull/30669)
* \[[`8d3e023061`](https://github.com/nodejs/node/commit/8d3e023061)] - **child\_process**: replace var with let/const (dnlup) [#30389](https://github.com/nodejs/node/pull/30389)
* \[[`6974f26e26`](https://github.com/nodejs/node/commit/6974f26e26)] - **child\_process**: replace var with const/let in internal/child\_process.js (Luis Camargo) [#30414](https://github.com/nodejs/node/pull/30414)
* \[[`d70ec60839`](https://github.com/nodejs/node/commit/d70ec60839)] - **cluster**: replace vars in child.js (EmaSuriano) [#30383](https://github.com/nodejs/node/pull/30383)
* \[[`98321448a6`](https://github.com/nodejs/node/commit/98321448a6)] - **cluster**: replace var with let (Herrmann, Rene R. (656)) [#30425](https://github.com/nodejs/node/pull/30425)
* \[[`daa593263d`](https://github.com/nodejs/node/commit/daa593263d)] - **cluster**: replace var by let in shared\_handle.js (poutch) [#30402](https://github.com/nodejs/node/pull/30402)
* \[[`2938fe31ae`](https://github.com/nodejs/node/commit/2938fe31ae)] - **cluster**: destruct primordials in lib/internal/cluster/worker.js (peze) [#30246](https://github.com/nodejs/node/pull/30246)
* \[[`c1cb639e84`](https://github.com/nodejs/node/commit/c1cb639e84)] - **crypto**: remove redundant validateUint32 argument (Tobias Nießen) [#30579](https://github.com/nodejs/node/pull/30579)
* \[[`bb7e78a2a0`](https://github.com/nodejs/node/commit/bb7e78a2a0)] - **crypto**: fix key requirements in asymmetric cipher (Tobias Nießen) [#30249](https://github.com/nodejs/node/pull/30249)
* \[[`74dd216886`](https://github.com/nodejs/node/commit/74dd216886)] - **crypto**: update root certificates (AshCripps) [#30195](https://github.com/nodejs/node/pull/30195)
* \[[`7589486584`](https://github.com/nodejs/node/commit/7589486584)] - **deps**: update llhttp to 2.0.1 (Fedor Indutny) [#30553](https://github.com/nodejs/node/pull/30553)
* \[[`d2e32ab6b2`](https://github.com/nodejs/node/commit/d2e32ab6b2)] - **deps**: update nghttp2 to 1.40.0 (gengjiawen) [#30493](https://github.com/nodejs/node/pull/30493)
* \[[`3d39be73d4`](https://github.com/nodejs/node/commit/3d39be73d4)] - **dgram**: remove listeners on bind error (Anna Henningsen) [#30210](https://github.com/nodejs/node/pull/30210)
* \[[`fd5fed1c89`](https://github.com/nodejs/node/commit/fd5fed1c89)] - **dgram**: reset bind state before emitting error (Anna Henningsen) [#30210](https://github.com/nodejs/node/pull/30210)
* \[[`ded0748dea`](https://github.com/nodejs/node/commit/ded0748dea)] - **dns**: use length for building TXT string (Anna Henningsen) [#30690](https://github.com/nodejs/node/pull/30690)
* \[[`7cf19ab069`](https://github.com/nodejs/node/commit/7cf19ab069)] - **dns**: switch var to const/let (Dmitriy Kikinskiy) [#30302](https://github.com/nodejs/node/pull/30302)
* \[[`6a35c387a1`](https://github.com/nodejs/node/commit/6a35c387a1)] - **doc**: fix typographical error (Rich Trott) [#30735](https://github.com/nodejs/node/pull/30735)
* \[[`043163a602`](https://github.com/nodejs/node/commit/043163a602)] - **doc**: revise REPL uncaught exception text (Rich Trott) [#30729](https://github.com/nodejs/node/pull/30729)
* \[[`50a54ce713`](https://github.com/nodejs/node/commit/50a54ce713)] - **doc**: update signature algorithm in release doc (Myles Borins) [#30673](https://github.com/nodejs/node/pull/30673)
* \[[`827c53bdbe`](https://github.com/nodejs/node/commit/827c53bdbe)] - **doc**: update README.md to fix active/maint times (Michael Dawson) [#30707](https://github.com/nodejs/node/pull/30707)
* \[[`4e93f2333e`](https://github.com/nodejs/node/commit/4e93f2333e)] - **doc**: update socket.bufferSize text (Rich Trott) [#30723](https://github.com/nodejs/node/pull/30723)
* \[[`5334f59519`](https://github.com/nodejs/node/commit/5334f59519)] - **doc**: note that buf.buffer's contents might differ (AJ Jordan) [#29651](https://github.com/nodejs/node/pull/29651)
* \[[`7bc8c85d53`](https://github.com/nodejs/node/commit/7bc8c85d53)] - **doc**: clarify IncomingMessage.destroy() description (Sam Foxman) [#30255](https://github.com/nodejs/node/pull/30255)
* \[[`ca960614af`](https://github.com/nodejs/node/commit/ca960614af)] - **doc**: fixed a typo in process.md (Harendra Singh) [#30277](https://github.com/nodejs/node/pull/30277)
* \[[`efbe0a28a1`](https://github.com/nodejs/node/commit/efbe0a28a1)] - **doc**: documenting a bit more FreeBSD case (David Carlier) [#30325](https://github.com/nodejs/node/pull/30325)
* \[[`42cb92f116`](https://github.com/nodejs/node/commit/42cb92f116)] - **doc**: add missing 'added' versions to module.builtinModules (Thomas Watson) [#30562](https://github.com/nodejs/node/pull/30562)
* \[[`670e4b5e23`](https://github.com/nodejs/node/commit/670e4b5e23)] - **doc**: address nits for src/README.md (Anna Henningsen) [#30693](https://github.com/nodejs/node/pull/30693)
* \[[`6fc562c97c`](https://github.com/nodejs/node/commit/6fc562c97c)] - **doc**: revise socket.connect() note (Rich Trott) [#30691](https://github.com/nodejs/node/pull/30691)
* \[[`bea2069f22`](https://github.com/nodejs/node/commit/bea2069f22)] - **doc**: remove "this API is unstable" note for v8 serdes API (bruce-one) [#30631](https://github.com/nodejs/node/pull/30631)
* \[[`2532bf347d`](https://github.com/nodejs/node/commit/2532bf347d)] - **doc**: minor updates to releases.md (Beth Griggs) [#30636](https://github.com/nodejs/node/pull/30636)
* \[[`8b60065778`](https://github.com/nodejs/node/commit/8b60065778)] - **doc**: add 13 and 12 to previous versions (Andrew Hughes) [#30590](https://github.com/nodejs/node/pull/30590)
* \[[`16a8daaae9`](https://github.com/nodejs/node/commit/16a8daaae9)] - **doc**: update AUTHORS list (Gus Caplan) [#30672](https://github.com/nodejs/node/pull/30672)
* \[[`c705a8e816`](https://github.com/nodejs/node/commit/c705a8e816)] - **doc**: add explanation why keep var with for loop (Lucas Recknagel) [#30380](https://github.com/nodejs/node/pull/30380)
* \[[`a493feb863`](https://github.com/nodejs/node/commit/a493feb863)] - **doc**: document "Resume Build" limitation (Richard Lau) [#30604](https://github.com/nodejs/node/pull/30604)
* \[[`0d6fbe8b30`](https://github.com/nodejs/node/commit/0d6fbe8b30)] - **doc**: add note of caution about non-conforming streams (Robert Nagy) [#29895](https://github.com/nodejs/node/pull/29895)
* \[[`7ebafda6c3`](https://github.com/nodejs/node/commit/7ebafda6c3)] - **doc**: add note about debugging worker\_threads (Denys Otrishko) [#30594](https://github.com/nodejs/node/pull/30594)
* \[[`e10f9224ac`](https://github.com/nodejs/node/commit/e10f9224ac)] - **doc**: add mention for using promisify on class methods (Denys Otrishko) [#30355](https://github.com/nodejs/node/pull/30355)
* \[[`768bac5678`](https://github.com/nodejs/node/commit/768bac5678)] - **doc**: explain GIT\_REMOTE\_REF in COLLABORATOR\_GUIDE (Denys Otrishko) [#30371](https://github.com/nodejs/node/pull/30371)
* \[[`a836ac10ff`](https://github.com/nodejs/node/commit/a836ac10ff)] - **doc**: fix overriding of prefix option (Luigi Pinca) [#30518](https://github.com/nodejs/node/pull/30518)
* \[[`ec5fe999e8`](https://github.com/nodejs/node/commit/ec5fe999e8)] - **doc**: adds NO\_COLOR to assert doc page (Shobhit Chittora) [#30483](https://github.com/nodejs/node/pull/30483)
* \[[`4b716a6798`](https://github.com/nodejs/node/commit/4b716a6798)] - **doc**: document timed out Travis CI builds (Richard Lau) [#30469](https://github.com/nodejs/node/pull/30469)
* \[[`2e70ad391b`](https://github.com/nodejs/node/commit/2e70ad391b)] - **doc**: replace const / var with let (Duncan Healy) [#30446](https://github.com/nodejs/node/pull/30446)
* \[[`793d360d26`](https://github.com/nodejs/node/commit/793d360d26)] - **doc**: update 8.x to 10.x in backporting guide (garygsc) [#30481](https://github.com/nodejs/node/pull/30481)
* \[[`25c8a13fde`](https://github.com/nodejs/node/commit/25c8a13fde)] - **doc**: createRequire can take import.meta.url directly (Geoffrey Booth) [#30495](https://github.com/nodejs/node/pull/30495)
* \[[`d979a9d391`](https://github.com/nodejs/node/commit/d979a9d391)] - **doc**: add entry to url.parse() changes metadata (Luigi Pinca) [#30348](https://github.com/nodejs/node/pull/30348)
* \[[`2e0ef36a19`](https://github.com/nodejs/node/commit/2e0ef36a19)] - **doc**: simplify text in pull-requests.md (Rich Trott) [#30458](https://github.com/nodejs/node/pull/30458)
* \[[`11d01700fd`](https://github.com/nodejs/node/commit/11d01700fd)] - **doc**: remove "multiple variants" from BUILDING.md (Rich Trott) [#30366](https://github.com/nodejs/node/pull/30366)
* \[[`0c68515e8c`](https://github.com/nodejs/node/commit/0c68515e8c)] - **doc**: remove "maintenance is supported by" text in BUILDING.md (Rich Trott) [#30365](https://github.com/nodejs/node/pull/30365)
* \[[`d2c85f32c6`](https://github.com/nodejs/node/commit/d2c85f32c6)] - **doc**: add lookup to http.request() options (Luigi Pinca) [#30353](https://github.com/nodejs/node/pull/30353)
* \[[`b8b6f258fe`](https://github.com/nodejs/node/commit/b8b6f258fe)] - **doc**: fix up N-API doc (Michael Dawson) [#30254](https://github.com/nodejs/node/pull/30254)
* \[[`3710068d3e`](https://github.com/nodejs/node/commit/3710068d3e)] - **doc**: add link to node-code-ide-configs in testing (Kamat, Trivikram) [#24012](https://github.com/nodejs/node/pull/24012)
* \[[`d713e5a9bf`](https://github.com/nodejs/node/commit/d713e5a9bf)] - **doc**: update GOVERNANCE.md (Rich Trott) [#30259](https://github.com/nodejs/node/pull/30259)
* \[[`f66f28ea7a`](https://github.com/nodejs/node/commit/f66f28ea7a)] - **doc**: move inactive Collaborators to emeriti (Rich Trott) [#30243](https://github.com/nodejs/node/pull/30243)
* \[[`fd16e9f6ff`](https://github.com/nodejs/node/commit/fd16e9f6ff)] - **doc**: update examples in writing-tests.md (garygsc) [#30126](https://github.com/nodejs/node/pull/30126)
* \[[`17963bbce6`](https://github.com/nodejs/node/commit/17963bbce6)] - **doc, console**: remove non-existant methods from docs (Simon Schick) [#30346](https://github.com/nodejs/node/pull/30346)
* \[[`fe1296e507`](https://github.com/nodejs/node/commit/fe1296e507)] - **doc,meta**: allow Travis results for doc/comment changes (Rich Trott) [#30330](https://github.com/nodejs/node/pull/30330)
* \[[`d32cd8595d`](https://github.com/nodejs/node/commit/d32cd8595d)] - **doc,meta**: remove wait period for npm pull requests (Rich Trott) [#30329](https://github.com/nodejs/node/pull/30329)
* \[[`574b0f2104`](https://github.com/nodejs/node/commit/574b0f2104)] - **domain**: rename var to let and const (Maria Stogova) [#30312](https://github.com/nodejs/node/pull/30312)
* \[[`c80a4d851d`](https://github.com/nodejs/node/commit/c80a4d851d)] - **encoding**: make TextDecoder handle BOM correctly (Anna Henningsen) [#30132](https://github.com/nodejs/node/pull/30132)
* \[[`217cc13f29`](https://github.com/nodejs/node/commit/217cc13f29)] - **events**: improve performance caused by primordials (guzhizhou) [#30577](https://github.com/nodejs/node/pull/30577)
* \[[`1a83c654f9`](https://github.com/nodejs/node/commit/1a83c654f9)] - **fs**: change var to let (Àlvar Pérez) [#30407](https://github.com/nodejs/node/pull/30407)
* \[[`7ba8037244`](https://github.com/nodejs/node/commit/7ba8037244)] - **fs**: cover fs.opendir ERR\_INVALID\_CALLBACK (Vladislav Botvin) [#30307](https://github.com/nodejs/node/pull/30307)
* \[[`d7c2911f8d`](https://github.com/nodejs/node/commit/d7c2911f8d)] - **fs**: change var to let (Nadya) [#30318](https://github.com/nodejs/node/pull/30318)
* \[[`6b380cc791`](https://github.com/nodejs/node/commit/6b380cc791)] - **http**: set socket.server unconditionally (Anna Henningsen) [#30571](https://github.com/nodejs/node/pull/30571)
* \[[`175b8fe5a7`](https://github.com/nodejs/node/commit/175b8fe5a7)] - **http**: replace var with let (Guilherme Goncalves) [#30421](https://github.com/nodejs/node/pull/30421)
* \[[`fdfcf68360`](https://github.com/nodejs/node/commit/fdfcf68360)] - **http**: destructure primordials in lib/\_http\_server.js (Artem Maksimov) [#30315](https://github.com/nodejs/node/pull/30315)
* \[[`71e3c485ad`](https://github.com/nodejs/node/commit/71e3c485ad)] - **http**: revise \_http\_server.js (telenord) [#30279](https://github.com/nodejs/node/pull/30279)
* \[[`5b8c481906`](https://github.com/nodejs/node/commit/5b8c481906)] - **http**: http\_common rename var to let and const (telenord) [#30288](https://github.com/nodejs/node/pull/30288)
* \[[`5727bc3880`](https://github.com/nodejs/node/commit/5727bc3880)] - **http**: http\_incoming rename var to let and const (telenord) [#30285](https://github.com/nodejs/node/pull/30285)
* \[[`b0816c2926`](https://github.com/nodejs/node/commit/b0816c2926)] - **http**: replace vars with lets and consts in lib/\_http\_agent.js (palmires) [#30301](https://github.com/nodejs/node/pull/30301)
* \[[`652514233f`](https://github.com/nodejs/node/commit/652514233f)] - **http,async\_hooks**: keep resource object alive from socket (Anna Henningsen) [#30196](https://github.com/nodejs/node/pull/30196)
* \[[`9fd6b5e98b`](https://github.com/nodejs/node/commit/9fd6b5e98b)] - **http2**: fix session memory accounting after pausing (Michael Lehenbauer) [#30684](https://github.com/nodejs/node/pull/30684)
* \[[`cb3e008c97`](https://github.com/nodejs/node/commit/cb3e008c97)] - **http2**: change var to let compact.js (Maria Emmanouil) [#30392](https://github.com/nodejs/node/pull/30392)
* \[[`68f3dde04b`](https://github.com/nodejs/node/commit/68f3dde04b)] - **http2**: core.js replace var with let (Daniel Schuech) [#30403](https://github.com/nodejs/node/pull/30403)
* \[[`bccef49b5b`](https://github.com/nodejs/node/commit/bccef49b5b)] - **http2**: replace var with let/const (Paolo Ceschi Berrini) [#30417](https://github.com/nodejs/node/pull/30417)
* \[[`13f65d1f16`](https://github.com/nodejs/node/commit/13f65d1f16)] - **http2**: remove duplicated assertIsObject (ZYSzys) [#30541](https://github.com/nodejs/node/pull/30541)
* \[[`eceeed7a11`](https://github.com/nodejs/node/commit/eceeed7a11)] - **https**: change var to let in lib/https.js (galina.prokofeva) [#30320](https://github.com/nodejs/node/pull/30320)
* \[[`55e94cbba1`](https://github.com/nodejs/node/commit/55e94cbba1)] - **inspector**: properly shut down uv\_async\_t (Anna Henningsen) [#30612](https://github.com/nodejs/node/pull/30612)
* \[[`d138e2db53`](https://github.com/nodejs/node/commit/d138e2db53)] - **lib**: use let instead of var (Shubham Chaturvedi) [#30375](https://github.com/nodejs/node/pull/30375)
* \[[`d951209458`](https://github.com/nodejs/node/commit/d951209458)] - **lib**: replace var with let/const (jens-cappelle) [#30391](https://github.com/nodejs/node/pull/30391)
* \[[`f963409f77`](https://github.com/nodejs/node/commit/f963409f77)] - **lib**: replace var w/ let (Chris Oyler) [#30386](https://github.com/nodejs/node/pull/30386)
* \[[`a6625dd7b6`](https://github.com/nodejs/node/commit/a6625dd7b6)] - **lib**: replace var with let/const (Tijl Claessens) [#30390](https://github.com/nodejs/node/pull/30390)
* \[[`7d0631aefc`](https://github.com/nodejs/node/commit/7d0631aefc)] - **lib**: adding perf notes js\_stream\_socket.js (ryan jarvinen) [#30415](https://github.com/nodejs/node/pull/30415)
* \[[`1cfaccdc66`](https://github.com/nodejs/node/commit/1cfaccdc66)] - **lib**: replace var with let (Dennis Saenger) [#30396](https://github.com/nodejs/node/pull/30396)
* \[[`c4fcd5b4ca`](https://github.com/nodejs/node/commit/c4fcd5b4ca)] - **lib**: main\_thread\_only change var to let (matijagaspar) [#30398](https://github.com/nodejs/node/pull/30398)
* \[[`dc786c3315`](https://github.com/nodejs/node/commit/dc786c3315)] - **lib**: change var to let in stream\_base\_commons (Kyriakos Markakis) [#30426](https://github.com/nodejs/node/pull/30426)
* \[[`e72be52c8e`](https://github.com/nodejs/node/commit/e72be52c8e)] - **lib**: use let instead of var (Semir Ajruli) [#30424](https://github.com/nodejs/node/pull/30424)
* \[[`96c061552d`](https://github.com/nodejs/node/commit/96c061552d)] - **lib**: changed var to let (Oliver Belaifa) [#30427](https://github.com/nodejs/node/pull/30427)
* \[[`03e6d0d408`](https://github.com/nodejs/node/commit/03e6d0d408)] - **lib**: replace var with let/const (Dries Stelten) [#30409](https://github.com/nodejs/node/pull/30409)
* \[[`7eafe8acdd`](https://github.com/nodejs/node/commit/7eafe8acdd)] - **lib**: change var to let (Dimitris Ktistakis) [#30408](https://github.com/nodejs/node/pull/30408)
* \[[`0c4bb4a70e`](https://github.com/nodejs/node/commit/0c4bb4a70e)] - **lib**: replace var with let/const (Tembrechts) [#30404](https://github.com/nodejs/node/pull/30404)
* \[[`ff4f23623c`](https://github.com/nodejs/node/commit/ff4f23623c)] - **lib**: replace var to let in cli\_table.js (Jing Lin) [#30400](https://github.com/nodejs/node/pull/30400)
* \[[`80bfc08935`](https://github.com/nodejs/node/commit/80bfc08935)] - **lib**: replace var with let (David OLIVIER) [#30381](https://github.com/nodejs/node/pull/30381)
* \[[`614949d25d`](https://github.com/nodejs/node/commit/614949d25d)] - **lib**: replace var with let and const in readline.js (VinceOPS) [#30377](https://github.com/nodejs/node/pull/30377)
* \[[`4834a31880`](https://github.com/nodejs/node/commit/4834a31880)] - **lib**: change var to let/const in internal/querystring.js (Artem Maksimov) [#30286](https://github.com/nodejs/node/pull/30286)
* \[[`df2dce08aa`](https://github.com/nodejs/node/commit/df2dce08aa)] - **lib**: change var to let in internal/streams (Kyriakos Markakis) [#30430](https://github.com/nodejs/node/pull/30430)
* \[[`bd853cc709`](https://github.com/nodejs/node/commit/bd853cc709)] - **lib**: replace var with let/const (Kenza Houmani) [#30440](https://github.com/nodejs/node/pull/30440)
* \[[`45e9c31885`](https://github.com/nodejs/node/commit/45e9c31885)] - **lib**: change var to let in string\_decoder (mkdorff) [#30393](https://github.com/nodejs/node/pull/30393)
* \[[`8725a5c935`](https://github.com/nodejs/node/commit/8725a5c935)] - **lib**: replaced var to let in lib/v8.js (Vadim Gorbachev) [#30305](https://github.com/nodejs/node/pull/30305)
* \[[`bcc00e1e88`](https://github.com/nodejs/node/commit/bcc00e1e88)] - **lib**: change var to let in lib/\_stream\_duplex.js (Ilia Safronov) [#30297](https://github.com/nodejs/node/pull/30297)
* \[[`b1415564ea`](https://github.com/nodejs/node/commit/b1415564ea)] - **module**: fix for empty object in InternalModuleReadJSON (Guy Bedford) [#30256](https://github.com/nodejs/node/pull/30256)
* \[[`dc9c7709ff`](https://github.com/nodejs/node/commit/dc9c7709ff)] - **n-api**: detach external ArrayBuffers on env exit (Anna Henningsen) [#30551](https://github.com/nodejs/node/pull/30551)
* \[[`4d396fd874`](https://github.com/nodejs/node/commit/4d396fd874)] - **n-api**: add missed nullptr check in napi\_has\_own\_property (Denys Otrishko) [#30626](https://github.com/nodejs/node/pull/30626)
* \[[`21be4b1b95`](https://github.com/nodejs/node/commit/21be4b1b95)] - **net**: replaced vars to lets and consts (nathias) [#30401](https://github.com/nodejs/node/pull/30401)
* \[[`0ae1d17517`](https://github.com/nodejs/node/commit/0ae1d17517)] - **net**: destructure primordials (Guilherme Goncalves) [#30447](https://github.com/nodejs/node/pull/30447)
* \[[`1597626a02`](https://github.com/nodejs/node/commit/1597626a02)] - **net**: replaced vars to lets and consts (alexahdp) [#30287](https://github.com/nodejs/node/pull/30287)
* \[[`f874aa1552`](https://github.com/nodejs/node/commit/f874aa1552)] - **path**: replace var with let in lib/path.js (peze) [#30260](https://github.com/nodejs/node/pull/30260)
* \[[`956207fa8d`](https://github.com/nodejs/node/commit/956207fa8d)] - **process**: replace var with let/const (Jesper Ek) [#30382](https://github.com/nodejs/node/pull/30382)
* \[[`db029650d9`](https://github.com/nodejs/node/commit/db029650d9)] - **process**: replace vars in per\_thread.js (EmaSuriano) [#30385](https://github.com/nodejs/node/pull/30385)
* \[[`02f606d528`](https://github.com/nodejs/node/commit/02f606d528)] - **process**: add coverage tests for sourceMapFromDataUrl method (Nolik) [#30319](https://github.com/nodejs/node/pull/30319)
* \[[`56b3edcce8`](https://github.com/nodejs/node/commit/56b3edcce8)] - **process**: make source map getter resistant against prototype tampering (Anna Henningsen) [#30228](https://github.com/nodejs/node/pull/30228)
* \[[`cbb2f81bf1`](https://github.com/nodejs/node/commit/cbb2f81bf1)] - **querystring**: replace var with let/const (Raoul Jaeckel) [#30429](https://github.com/nodejs/node/pull/30429)
* \[[`b21f46d95d`](https://github.com/nodejs/node/commit/b21f46d95d)] - **readline**: change var to let (dnlup) [#30435](https://github.com/nodejs/node/pull/30435)
* \[[`cc84dbfe7b`](https://github.com/nodejs/node/commit/cc84dbfe7b)] - **repl**: change var to let (Oliver Belaifa) [#30428](https://github.com/nodejs/node/pull/30428)
* \[[`4b1f730357`](https://github.com/nodejs/node/commit/4b1f730357)] - **src**: remove unused variable in node\_dir.cc (gengjiawen) [#30267](https://github.com/nodejs/node/pull/30267)
* \[[`3bb085dbad`](https://github.com/nodejs/node/commit/3bb085dbad)] - **src**: cleanup unused headers (Alexandre Ferrando) [#30328](https://github.com/nodejs/node/pull/30328)
* \[[`df4dddb0d2`](https://github.com/nodejs/node/commit/df4dddb0d2)] - **src**: replaced var with let (Aldo Ambrosioni) [#30397](https://github.com/nodejs/node/pull/30397)
* \[[`8af33114e8`](https://github.com/nodejs/node/commit/8af33114e8)] - **src**: fix signal handler crash on close (Shelley Vohr) [#30582](https://github.com/nodejs/node/pull/30582)
* \[[`12d7d645dd`](https://github.com/nodejs/node/commit/12d7d645dd)] - **src**: add file name to 'Module did not self-register' error (Jeremy Apthorp) [#30125](https://github.com/nodejs/node/pull/30125)
* \[[`2f4069a932`](https://github.com/nodejs/node/commit/2f4069a932)] - **src**: enhance feature access `CHECK`s during bootstrap (Anna Henningsen) [#30452](https://github.com/nodejs/node/pull/30452)
* \[[`3d7882e0d1`](https://github.com/nodejs/node/commit/3d7882e0d1)] - **src**: lib/internal/timers.js var -> let/const (Nikolay Krashnikov) [#30314](https://github.com/nodejs/node/pull/30314)
* \[[`d8046fc0f8`](https://github.com/nodejs/node/commit/d8046fc0f8)] - **src**: persist strings that are used multiple times in the environment (Vadim Gorbachev) [#30321](https://github.com/nodejs/node/pull/30321)
* \[[`e1a12446c5`](https://github.com/nodejs/node/commit/e1a12446c5)] - **src**: run RunBeforeExitCallbacks as part of EmitBeforeExit (Anna Henningsen) [#30229](https://github.com/nodejs/node/pull/30229)
* \[[`da8ceb965d`](https://github.com/nodejs/node/commit/da8ceb965d)] - **src**: use unique\_ptr for InitializeInspector() (Anna Henningsen) [#30229](https://github.com/nodejs/node/pull/30229)
* \[[`fc9e7082fd`](https://github.com/nodejs/node/commit/fc9e7082fd)] - **src**: make WaitForInspectorDisconnect an exit hook (Anna Henningsen) [#30229](https://github.com/nodejs/node/pull/30229)
* \[[`d14d9e8b1b`](https://github.com/nodejs/node/commit/d14d9e8b1b)] - **src**: make EndStartedProfilers an exit hook (Anna Henningsen) [#30229](https://github.com/nodejs/node/pull/30229)
* \[[`eb8beb5f5f`](https://github.com/nodejs/node/commit/eb8beb5f5f)] - **src**: track no of active JS signal handlers (Anna Henningsen) [#30229](https://github.com/nodejs/node/pull/30229)
* \[[`2e729f2dc9`](https://github.com/nodejs/node/commit/2e729f2dc9)] - **src**: make AtExit() callbacks run in reverse order (Anna Henningsen) [#30230](https://github.com/nodejs/node/pull/30230)
* \[[`569f797917`](https://github.com/nodejs/node/commit/569f797917)] - **src**: remove unimplemented method from node.h (Anna Henningsen) [#30098](https://github.com/nodejs/node/pull/30098)
* \[[`f6360c124c`](https://github.com/nodejs/node/commit/f6360c124c)] - **src,doc**: fix broken links (cjihrig) [#30662](https://github.com/nodejs/node/pull/30662)
* \[[`a621ab8695`](https://github.com/nodejs/node/commit/a621ab8695)] - **src,doc**: add C++ internals documentation (Anna Henningsen) [#30552](https://github.com/nodejs/node/pull/30552)
* \[[`ce6a865ab2`](https://github.com/nodejs/node/commit/ce6a865ab2)] - **stream**: improve performance for sync write finishes (Anna Henningsen) [#30710](https://github.com/nodejs/node/pull/30710)
* \[[`8792147fd2`](https://github.com/nodejs/node/commit/8792147fd2)] - **stream**: replace var with let (daern91) [#30379](https://github.com/nodejs/node/pull/30379)
* \[[`88adad26f5`](https://github.com/nodejs/node/commit/88adad26f5)] - **stream**: increase MAX\_HWM (Robert Nagy) [#29938](https://github.com/nodejs/node/pull/29938)
* \[[`a83ccf8a6b`](https://github.com/nodejs/node/commit/a83ccf8a6b)] - **test**: skip test-domain-error-types in debug mode temporariliy (Rich Trott) [#30629](https://github.com/nodejs/node/pull/30629)
* \[[`f3c3b1d328`](https://github.com/nodejs/node/commit/f3c3b1d328)] - **test**: add coverage for ERR\_TLS\_INVALID\_PROTOCOL\_VERSION (Rich Trott) [#30741](https://github.com/nodejs/node/pull/30741)
* \[[`197b61656d`](https://github.com/nodejs/node/commit/197b61656d)] - **test**: add an indicator `isIBMi` (Xu Meng) [#30714](https://github.com/nodejs/node/pull/30714)
* \[[`6c6ffdd56c`](https://github.com/nodejs/node/commit/6c6ffdd56c)] - **test**: use arrow functions in async-hooks tests (garygsc) [#30137](https://github.com/nodejs/node/pull/30137)
* \[[`2aa86547ff`](https://github.com/nodejs/node/commit/2aa86547ff)] - **test**: fix test-benchmark-streams (Rich Trott) [#30757](https://github.com/nodejs/node/pull/30757)
* \[[`b37609d193`](https://github.com/nodejs/node/commit/b37609d193)] - **test**: remove unused callback argument (Rich Trott) [#30712](https://github.com/nodejs/node/pull/30712)
* \[[`5bb7bf3767`](https://github.com/nodejs/node/commit/5bb7bf3767)] - **test**: simplify forEach() usage (Rich Trott) [#30712](https://github.com/nodejs/node/pull/30712)
* \[[`276741ae75`](https://github.com/nodejs/node/commit/276741ae75)] - **test**: remove unused callback argument (Rich Trott) [#30712](https://github.com/nodejs/node/pull/30712)
* \[[`e624bb529d`](https://github.com/nodejs/node/commit/e624bb529d)] - **test**: increase coverage for trace\_events.js (Rich Trott) [#30705](https://github.com/nodejs/node/pull/30705)
* \[[`9f49b978e4`](https://github.com/nodejs/node/commit/9f49b978e4)] - **test**: refactor createHook test (Jeny) [#30568](https://github.com/nodejs/node/pull/30568)
* \[[`646b81c209`](https://github.com/nodejs/node/commit/646b81c209)] - **test**: port worker + buffer test to N-API (Anna Henningsen) [#30551](https://github.com/nodejs/node/pull/30551)
* \[[`8554ff2a4c`](https://github.com/nodejs/node/commit/8554ff2a4c)] - **test**: move test-https-server-consumed-timeout to parallel (Rich Trott) [#30677](https://github.com/nodejs/node/pull/30677)
* \[[`d3bac601c3`](https://github.com/nodejs/node/commit/d3bac601c3)] - **test**: remove unnecessary common.platformTimeout() call (Rich Trott) [#30677](https://github.com/nodejs/node/pull/30677)
* \[[`564e477b37`](https://github.com/nodejs/node/commit/564e477b37)] - **test**: do not skip test-http-server-consumed-timeout (Rich Trott) [#30677](https://github.com/nodejs/node/pull/30677)
* \[[`d3785941a9`](https://github.com/nodejs/node/commit/d3785941a9)] - **test**: remove unused function argument from http test (Rich Trott) [#30677](https://github.com/nodejs/node/pull/30677)
* \[[`648318dc5c`](https://github.com/nodejs/node/commit/648318dc5c)] - **test**: add logging in case of infinite loop (Rich Trott) [#30649](https://github.com/nodejs/node/pull/30649)
* \[[`196e08dafc`](https://github.com/nodejs/node/commit/196e08dafc)] - **test**: remove destructuring from test-inspector-contexts (Rich Trott) [#30649](https://github.com/nodejs/node/pull/30649)
* \[[`ece08a5821`](https://github.com/nodejs/node/commit/ece08a5821)] - **test**: check for session.post() errors in test-insepctor-context (Rich Trott) [#30649](https://github.com/nodejs/node/pull/30649)
* \[[`c3ad977867`](https://github.com/nodejs/node/commit/c3ad977867)] - **test**: add mustCall() to test-inspector-contexts (Rich Trott) [#30649](https://github.com/nodejs/node/pull/30649)
* \[[`f5ac4ec49a`](https://github.com/nodejs/node/commit/f5ac4ec49a)] - **test**: add regression test for signal handler removal in exit (Anna Henningsen) [#30589](https://github.com/nodejs/node/pull/30589)
* \[[`825b3057d1`](https://github.com/nodejs/node/commit/825b3057d1)] - **test**: move test-worker-prof to sequential (Rich Trott) [#30628](https://github.com/nodejs/node/pull/30628)
* \[[`12ef7d99eb`](https://github.com/nodejs/node/commit/12ef7d99eb)] - **test**: dir class initialisation w/o handler (Dmitriy Kikinskiy) [#30313](https://github.com/nodejs/node/pull/30313)
* \[[`2f8dcefa6d`](https://github.com/nodejs/node/commit/2f8dcefa6d)] - **test**: change object assign by spread operator (poutch) [#30438](https://github.com/nodejs/node/pull/30438)
* \[[`e52237d66e`](https://github.com/nodejs/node/commit/e52237d66e)] - **test**: use useful message argument in test function (Rich Trott) [#30618](https://github.com/nodejs/node/pull/30618)
* \[[`1c9ba2cdc3`](https://github.com/nodejs/node/commit/1c9ba2cdc3)] - **test**: test for minimum ICU version consistency (Richard Lau) [#30608](https://github.com/nodejs/node/pull/30608)
* \[[`2e37828350`](https://github.com/nodejs/node/commit/2e37828350)] - **test**: code\&learn var to let update (Nazar Malyy) [#30436](https://github.com/nodejs/node/pull/30436)
* \[[`01da702fec`](https://github.com/nodejs/node/commit/01da702fec)] - **test**: change object assign to spread object (poutch) [#30422](https://github.com/nodejs/node/pull/30422)
* \[[`d708887c0b`](https://github.com/nodejs/node/commit/d708887c0b)] - **test**: use spread instead of Object.assign (dnlup) [#30419](https://github.com/nodejs/node/pull/30419)
* \[[`46f698fed5`](https://github.com/nodejs/node/commit/46f698fed5)] - **test**: changed var to let in module-errors (Jamar Torres) [#30413](https://github.com/nodejs/node/pull/30413)
* \[[`78c7118ab7`](https://github.com/nodejs/node/commit/78c7118ab7)] - **test**: use spread instead of object.assign (Shubham Chaturvedi) [#30412](https://github.com/nodejs/node/pull/30412)
* \[[`e7f1d57cdf`](https://github.com/nodejs/node/commit/e7f1d57cdf)] - **test**: replace var with let in pre\_execution.js (Vladimir Adamic) [#30411](https://github.com/nodejs/node/pull/30411)
* \[[`d077550c44`](https://github.com/nodejs/node/commit/d077550c44)] - **test**: change var to let in test-trace-events (Jon Church) [#30406](https://github.com/nodejs/node/pull/30406)
* \[[`7f0e7fd4b2`](https://github.com/nodejs/node/commit/7f0e7fd4b2)] - **test**: dns utils replace var (Osmond van Hemert) [#30405](https://github.com/nodejs/node/pull/30405)
* \[[`0a068db450`](https://github.com/nodejs/node/commit/0a068db450)] - **test**: test cover cases when trace is empty (telenord) [#30311](https://github.com/nodejs/node/pull/30311)
* \[[`984c40629c`](https://github.com/nodejs/node/commit/984c40629c)] - **test**: switch to object spread in common/benchmark.js (palmires) [#30309](https://github.com/nodejs/node/pull/30309)
* \[[`88bca0fcc0`](https://github.com/nodejs/node/commit/88bca0fcc0)] - **test**: add common.mustCall() to stream test (Rich Trott) [#30561](https://github.com/nodejs/node/pull/30561)
* \[[`516bdaf54d`](https://github.com/nodejs/node/commit/516bdaf54d)] - **test**: move explanatory comment to expected location in file (Rich Trott) [#30561](https://github.com/nodejs/node/pull/30561)
* \[[`8e36901748`](https://github.com/nodejs/node/commit/8e36901748)] - **test**: move stream test to parallel (Rich Trott) [#30561](https://github.com/nodejs/node/pull/30561)
* \[[`0903f67a87`](https://github.com/nodejs/node/commit/0903f67a87)] - **test**: remove string literal as message in strictEqual() in stream test (Rich Trott) [#30561](https://github.com/nodejs/node/pull/30561)
* \[[`59c9592679`](https://github.com/nodejs/node/commit/59c9592679)] - **test**: use arrow function for callback in stream test (Rich Trott) [#30561](https://github.com/nodejs/node/pull/30561)
* \[[`4a5f00c35d`](https://github.com/nodejs/node/commit/4a5f00c35d)] - **test**: replace setTimeout with setImmediate in stream test (Rich Trott) [#30561](https://github.com/nodejs/node/pull/30561)
* \[[`cd5076eb62`](https://github.com/nodejs/node/commit/cd5076eb62)] - **test**: refactor test-dgram-multicast-set-interface-lo.js (Taylor Gagne) [#30536](https://github.com/nodejs/node/pull/30536)
* \[[`0d26263b7e`](https://github.com/nodejs/node/commit/0d26263b7e)] - **test**: move test not requiring internet from internet to parallel (Rich Trott) [#30545](https://github.com/nodejs/node/pull/30545)
* \[[`9869aaaee4`](https://github.com/nodejs/node/commit/9869aaaee4)] - **test**: use reserved .invalid TLD for invalid address in test (Rich Trott) [#30545](https://github.com/nodejs/node/pull/30545)
* \[[`d802393336`](https://github.com/nodejs/node/commit/d802393336)] - **test**: improve assertion message in internet dgram test (Rich Trott) [#30545](https://github.com/nodejs/node/pull/30545)
* \[[`6da56e91a3`](https://github.com/nodejs/node/commit/6da56e91a3)] - **test**: add test for options validation of createServer (ZYSzys) [#30541](https://github.com/nodejs/node/pull/30541)
* \[[`3fb0f7ebd0`](https://github.com/nodejs/node/commit/3fb0f7ebd0)] - **test**: clean up http-set-trailers (Denys Otrishko) [#30522](https://github.com/nodejs/node/pull/30522)
* \[[`e4a1cffcbc`](https://github.com/nodejs/node/commit/e4a1cffcbc)] - **test**: handle undefined default\_configuration (Shelley Vohr) [#30465](https://github.com/nodejs/node/pull/30465)
* \[[`c0d9a545a3`](https://github.com/nodejs/node/commit/c0d9a545a3)] - **test**: Change from var to const (Jure Stepisnik) [#30431](https://github.com/nodejs/node/pull/30431)
* \[[`91c6fe4b61`](https://github.com/nodejs/node/commit/91c6fe4b61)] - **test**: changed var to let in test-repl-editor (JL Phillips) [#30443](https://github.com/nodejs/node/pull/30443)
* \[[`4dfcc12666`](https://github.com/nodejs/node/commit/4dfcc12666)] - **test**: improve test-fs-open (Artem Maksimov) [#30280](https://github.com/nodejs/node/pull/30280)
* \[[`f0b6a236ac`](https://github.com/nodejs/node/commit/f0b6a236ac)] - **test**: change var to let (nathias) [#30444](https://github.com/nodejs/node/pull/30444)
* \[[`df1d73539e`](https://github.com/nodejs/node/commit/df1d73539e)] - **test**: changed var to const in test (Kerry Mahne) [#30434](https://github.com/nodejs/node/pull/30434)
* \[[`cf9da71e97`](https://github.com/nodejs/node/commit/cf9da71e97)] - **test**: var to const in test-repl-multiline.js (SoulMonk) [#30433](https://github.com/nodejs/node/pull/30433)
* \[[`b49e63d51c`](https://github.com/nodejs/node/commit/b49e63d51c)] - **test**: deflake test-http-dump-req-when-res-ends.js (Luigi Pinca) [#30360](https://github.com/nodejs/node/pull/30360)
* \[[`2e6b3be8dd`](https://github.com/nodejs/node/commit/2e6b3be8dd)] - **test**: change var to const in parallel/test-stream-transform-final\* (Kenza Houmani) [#30448](https://github.com/nodejs/node/pull/30448)
* \[[`aaedc06ea4`](https://github.com/nodejs/node/commit/aaedc06ea4)] - **test**: replace Object.assign with object spread (Grigoriy Levanov) [#30306](https://github.com/nodejs/node/pull/30306)
* \[[`d1483305ae`](https://github.com/nodejs/node/commit/d1483305ae)] - **test**: fix Python unittests in ./test and ./tools (cclauss) [#30340](https://github.com/nodejs/node/pull/30340)
* \[[`5e2848d44f`](https://github.com/nodejs/node/commit/5e2848d44f)] - **test**: mark test-http-dump-req-when-res-ends as flaky on windows (AshCripps) [#30316](https://github.com/nodejs/node/pull/30316)
* \[[`5b428571e2`](https://github.com/nodejs/node/commit/5b428571e2)] - **test**: fix test-benchmark-cluster (Rich Trott) [#30342](https://github.com/nodejs/node/pull/30342)
* \[[`342031eac0`](https://github.com/nodejs/node/commit/342031eac0)] - **test**: deflake test-tls-close-notify.js (Luigi Pinca) [#30202](https://github.com/nodejs/node/pull/30202)
* \[[`43cec65d6f`](https://github.com/nodejs/node/commit/43cec65d6f)] - **tls**: allow empty subject even with altNames defined (Jason Macgowan) [#22906](https://github.com/nodejs/node/pull/22906)
* \[[`0f7281a305`](https://github.com/nodejs/node/commit/0f7281a305)] - **tls**: change loop var to let (Xavier Redondo) [#30445](https://github.com/nodejs/node/pull/30445)
* \[[`6fe2c7a106`](https://github.com/nodejs/node/commit/6fe2c7a106)] - **tls**: replace var with let (Daniil Pletnev) [#30308](https://github.com/nodejs/node/pull/30308)
* \[[`d59df36f58`](https://github.com/nodejs/node/commit/d59df36f58)] - **tls**: replace var with let and const (Nolik) [#30299](https://github.com/nodejs/node/pull/30299)
* \[[`634aac5b94`](https://github.com/nodejs/node/commit/634aac5b94)] - **tls**: refactor tls\_wrap.cc (Artem Maksimov) [#30303](https://github.com/nodejs/node/pull/30303)
* \[[`a715c2506b`](https://github.com/nodejs/node/commit/a715c2506b)] - **tools**: enforce blank line between functions (Rich Trott) [#30696](https://github.com/nodejs/node/pull/30696)
* \[[`da1e5ae1fd`](https://github.com/nodejs/node/commit/da1e5ae1fd)] - **tools**: add unified plugin changing links for html docs (Marek Łabuz) [#29946](https://github.com/nodejs/node/pull/29946)
* \[[`df91d5fd66`](https://github.com/nodejs/node/commit/df91d5fd66)] - **tools**: enable more eslint rules (cjihrig) [#30598](https://github.com/nodejs/node/pull/30598)
* \[[`bce08806c7`](https://github.com/nodejs/node/commit/bce08806c7)] - **tools**: update ESLint to 6.7.1 (cjihrig) [#30598](https://github.com/nodejs/node/pull/30598)
* \[[`0797cc706a`](https://github.com/nodejs/node/commit/0797cc706a)] - **tools**: fix build at non-English windows (Rongjian Zhang) [#30492](https://github.com/nodejs/node/pull/30492)
* \[[`5e8b2a8190`](https://github.com/nodejs/node/commit/5e8b2a8190)] - **tools**: make doctool work if no internet available (Richard Lau) [#30214](https://github.com/nodejs/node/pull/30214)
* \[[`05290fd5ea`](https://github.com/nodejs/node/commit/05290fd5ea)] - **tools**: update certdata.txt (AshCripps) [#30195](https://github.com/nodejs/node/pull/30195)
* \[[`d9e5b72c0b`](https://github.com/nodejs/node/commit/d9e5b72c0b)] - **tools**: check-imports using utf-8 (cclauss) [#30220](https://github.com/nodejs/node/pull/30220)
* \[[`0e68f550e5`](https://github.com/nodejs/node/commit/0e68f550e5)] - **tty**: truecolor check moved before 256 check (Duncan Healy) [#30474](https://github.com/nodejs/node/pull/30474)
* \[[`f2f45297a0`](https://github.com/nodejs/node/commit/f2f45297a0)] - **url**: replace var with let in lib/url.js (xefimx) [#30281](https://github.com/nodejs/node/pull/30281)
* \[[`d96c76507f`](https://github.com/nodejs/node/commit/d96c76507f)] - **util**: fix inspection of errors with tampered name or stack property (Ruben Bridgewater) [#30576](https://github.com/nodejs/node/pull/30576)
* \[[`7421cc8fbd`](https://github.com/nodejs/node/commit/7421cc8fbd)] - **util**: use let instead of var for util/inspect.js (Luciano) [#30399](https://github.com/nodejs/node/pull/30399)
* \[[`ec49ea74fe`](https://github.com/nodejs/node/commit/ec49ea74fe)] - **util**: replace var with let (Susana Ferreira) [#30439](https://github.com/nodejs/node/pull/30439)
* \[[`3f24a87f41`](https://github.com/nodejs/node/commit/3f24a87f41)] - **v8**: mark serdes API as stable (Anna Henningsen) [#30234](https://github.com/nodejs/node/pull/30234)
* \[[`2994976ec7`](https://github.com/nodejs/node/commit/2994976ec7)] - **v8**: inspect unserializable objects (Anna Henningsen) [#30167](https://github.com/nodejs/node/pull/30167)

<a id="12.14.0"></a>

## 2019-12-17, Version 12.14.0 'Erbium' (LTS), @MylesBorins

This is a security release.

For more details about the vulnerability please consult the npm blog:

<https://blog.npmjs.org/post/189618601100/binary-planting-with-the-npm-cli>

### Notable changes

* **deps**: update npm to 6.13.4 [#30904](https://github.com/nodejs/node/pull/30904)

### Commits

* \[[`f01959a616`](https://github.com/nodejs/node/commit/f01959a616)] - **build,win**: add test-ci-native and test-ci-js (João Reis) [#30724](https://github.com/nodejs/node/pull/30724)
* \[[`d586682b0b`](https://github.com/nodejs/node/commit/d586682b0b)] - **deps**: update npm to 6.13.4 (Ruy Adorno) [#30904](https://github.com/nodejs/node/pull/30904)

<a id="12.13.1"></a>

## 2019-11-19, Version 12.13.1 'Erbium' (LTS), @targos

### Notable changes

* Experimental support for building Node.js with Python 3 is improved.
* ICU time zone data is updated to version 2019c. This fixes the date offset
  in Brazil.

### Commits

* \[[`56be32d22d`](https://github.com/nodejs/node/commit/56be32d22d)] - **async\_hooks**: only emit `after` for AsyncResource if stack not empty (Anna Henningsen) [#30087](https://github.com/nodejs/node/pull/30087)
* \[[`e16e3d5b90`](https://github.com/nodejs/node/commit/e16e3d5b90)] - **benchmark**: remove double word "then" in comments (Nick Schonning) [#29823](https://github.com/nodejs/node/pull/29823)
* \[[`dcdb96c7bb`](https://github.com/nodejs/node/commit/dcdb96c7bb)] - **benchmark**: add benchmark for vm.createContext (Joyee Cheung) [#29845](https://github.com/nodejs/node/pull/29845)
* \[[`680e9cc7e1`](https://github.com/nodejs/node/commit/680e9cc7e1)] - **buffer**: improve performance caused by primordials (Jizu Sun) [#30235](https://github.com/nodejs/node/pull/30235)
* \[[`bcd2238b3e`](https://github.com/nodejs/node/commit/bcd2238b3e)] - **build**: add workaround for WSL (gengjiawen) [#30221](https://github.com/nodejs/node/pull/30221)
* \[[`c5d312f821`](https://github.com/nodejs/node/commit/c5d312f821)] - **build**: find Python syntax errors in dependencies (Christian Clauss) [#30143](https://github.com/nodejs/node/pull/30143)
* \[[`468f203809`](https://github.com/nodejs/node/commit/468f203809)] - **build**: fix pkg-config search for libnghttp2 (Ben Noordhuis) [#30145](https://github.com/nodejs/node/pull/30145)
* \[[`0415dd7cb3`](https://github.com/nodejs/node/commit/0415dd7cb3)] - **build**: python3 support for configure (Rod Vagg) [#30047](https://github.com/nodejs/node/pull/30047)
* \[[`032c23d360`](https://github.com/nodejs/node/commit/032c23d360)] - **build**: make linter failures fail `test-doc` target (Richard Lau) [#30012](https://github.com/nodejs/node/pull/30012)
* \[[`a86648c8d2`](https://github.com/nodejs/node/commit/a86648c8d2)] - **build**: log the found compiler version if too old (Richard Lau) [#30028](https://github.com/nodejs/node/pull/30028)
* \[[`02f6e5cc40`](https://github.com/nodejs/node/commit/02f6e5cc40)] - **build**: fix version checks in configure.py (Michaël Zasso) [#29965](https://github.com/nodejs/node/pull/29965)
* \[[`a1adce1b4f`](https://github.com/nodejs/node/commit/a1adce1b4f)] - **build**: build benchmark addons like test addons (Richard Lau) [#29995](https://github.com/nodejs/node/pull/29995)
* \[[`735ec1bf96`](https://github.com/nodejs/node/commit/735ec1bf96)] - **build**: fix version checks in gyp files (Ben Noordhuis) [#29931](https://github.com/nodejs/node/pull/29931)
* \[[`8da83e8c24`](https://github.com/nodejs/node/commit/8da83e8c24)] - **build**: always use strings for compiler version in gyp files (Michaël Zasso) [#29897](https://github.com/nodejs/node/pull/29897)
* \[[`b7bdfd346c`](https://github.com/nodejs/node/commit/b7bdfd346c)] - **crypto**: guard with OPENSSL\_NO\_GOST (Shelley Vohr) [#30050](https://github.com/nodejs/node/pull/30050)
* \[[`e175d0beb6`](https://github.com/nodejs/node/commit/e175d0beb6)] - **crypto**: reject public keys properly (Tobias Nießen) [#29913](https://github.com/nodejs/node/pull/29913)
* \[[`b1529c6bc2`](https://github.com/nodejs/node/commit/b1529c6bc2)] - **deps**: V8: cherry-pick a7dffcd767be (Christian Clauss) [#30218](https://github.com/nodejs/node/pull/30218)
* \[[`6bc7a6db0e`](https://github.com/nodejs/node/commit/6bc7a6db0e)] - **deps**: V8: cherry-pick e5dbc95 (Gabriel Schulhof) [#30130](https://github.com/nodejs/node/pull/30130)
* \[[`b88314f735`](https://github.com/nodejs/node/commit/b88314f735)] - **deps**: update npm to 6.12.1 (Michael Perrotte) [#30164](https://github.com/nodejs/node/pull/30164)
* \[[`ce49a412ef`](https://github.com/nodejs/node/commit/ce49a412ef)] - **deps**: V8: cherry-pick c721203 (Michaël Zasso) [#30065](https://github.com/nodejs/node/pull/30065)
* \[[`d2756fd14d`](https://github.com/nodejs/node/commit/d2756fd14d)] - **deps**: V8: cherry-pick ed40ab1 (Michaël Zasso) [#30064](https://github.com/nodejs/node/pull/30064)
* \[[`58c585e3ed`](https://github.com/nodejs/node/commit/58c585e3ed)] - **deps**: npm: patch support for 13.x (Jordan Harband) [#30079](https://github.com/nodejs/node/pull/30079)
* \[[`2764567f90`](https://github.com/nodejs/node/commit/2764567f90)] - **deps**: upgrade to libuv 1.33.1 (Colin Ihrig) [#29996](https://github.com/nodejs/node/pull/29996)
* \[[`33bd1281fc`](https://github.com/nodejs/node/commit/33bd1281fc)] - **doc**: add missing hash for header link (Nick Schonning) [#30188](https://github.com/nodejs/node/pull/30188)
* \[[`b159b91798`](https://github.com/nodejs/node/commit/b159b91798)] - **doc**: linkify `.setupMaster()` in cluster doc (Trivikram Kamat) [#30204](https://github.com/nodejs/node/pull/30204)
* \[[`9c4a9e7337`](https://github.com/nodejs/node/commit/9c4a9e7337)] - **doc**: explain http2 aborted event callback (dev-313) [#30179](https://github.com/nodejs/node/pull/30179)
* \[[`d7bfc6c987`](https://github.com/nodejs/node/commit/d7bfc6c987)] - **doc**: linkify `.fork()` in cluster documentation (Anna Henningsen) [#30163](https://github.com/nodejs/node/pull/30163)
* \[[`a71f210206`](https://github.com/nodejs/node/commit/a71f210206)] - **doc**: update AUTHORS list (Michaël Zasso) [#30142](https://github.com/nodejs/node/pull/30142)
* \[[`7b5047454b`](https://github.com/nodejs/node/commit/7b5047454b)] - **doc**: improve doc Http2Session:Timeout (dev-313) [#30161](https://github.com/nodejs/node/pull/30161)
* \[[`0efe9a0c97`](https://github.com/nodejs/node/commit/0efe9a0c97)] - **doc**: move inactive Collaborators to emeriti (Rich Trott) [#30177](https://github.com/nodejs/node/pull/30177)
* \[[`98d31da342`](https://github.com/nodejs/node/commit/98d31da342)] - **doc**: add options description for send APIs (dev-313) [#29868](https://github.com/nodejs/node/pull/29868)
* \[[`d0f5bc1aa7`](https://github.com/nodejs/node/commit/d0f5bc1aa7)] - **doc**: fix an error in resolution algorithm steps (Alex Zherdev) [#29940](https://github.com/nodejs/node/pull/29940)
* \[[`28db99932a`](https://github.com/nodejs/node/commit/28db99932a)] - **doc**: remove incorrect and outdated example (Tobias Nießen) [#30138](https://github.com/nodejs/node/pull/30138)
* \[[`c2108d4919`](https://github.com/nodejs/node/commit/c2108d4919)] - **doc**: adjust code sample for stream.finished (Cotton Hou) [#29983](https://github.com/nodejs/node/pull/29983)
* \[[`2ac76e3055`](https://github.com/nodejs/node/commit/2ac76e3055)] - **doc**: remove "it is important to" phrasing (Rich Trott) [#30108](https://github.com/nodejs/node/pull/30108)
* \[[`ec992878e8`](https://github.com/nodejs/node/commit/ec992878e8)] - **doc**: revise os.md (Rich Trott) [#30102](https://github.com/nodejs/node/pull/30102)
* \[[`a56e78c8c8`](https://github.com/nodejs/node/commit/a56e78c8c8)] - **doc**: delete "a number of" things in the docs (Rich Trott) [#30103](https://github.com/nodejs/node/pull/30103)
* \[[`ee954d5570`](https://github.com/nodejs/node/commit/ee954d5570)] - **doc**: remove dashes (Rich Trott) [#30101](https://github.com/nodejs/node/pull/30101)
* \[[`c4c8e01af1`](https://github.com/nodejs/node/commit/c4c8e01af1)] - **doc**: add legendecas to collaborators (legendecas) [#30115](https://github.com/nodejs/node/pull/30115)
* \[[`22e10fd15a`](https://github.com/nodejs/node/commit/22e10fd15a)] - **doc**: --enable-source-maps and prepareStackTrace are incompatible (Benjamin Coe) [#30046](https://github.com/nodejs/node/pull/30046)
* \[[`870c320f31`](https://github.com/nodejs/node/commit/870c320f31)] - **doc**: join parts of disrupt section in cli.md (vsemozhetbyt) [#30038](https://github.com/nodejs/node/pull/30038)
* \[[`8df5bdbd66`](https://github.com/nodejs/node/commit/8df5bdbd66)] - **doc**: update collaborator email address (Minwoo Jung) [#30007](https://github.com/nodejs/node/pull/30007)
* \[[`d9b5508fc8`](https://github.com/nodejs/node/commit/d9b5508fc8)] - **doc**: fix tls version typo (akitsu-sanae) [#29984](https://github.com/nodejs/node/pull/29984)
* \[[`5616f22839`](https://github.com/nodejs/node/commit/5616f22839)] - **doc**: clarify readable.unshift null/EOF (Robert Nagy) [#29950](https://github.com/nodejs/node/pull/29950)
* \[[`b57fe3b370`](https://github.com/nodejs/node/commit/b57fe3b370)] - **doc**: remove unused Markdown reference links (Nick Schonning) [#29961](https://github.com/nodejs/node/pull/29961)
* \[[`12f24542b8`](https://github.com/nodejs/node/commit/12f24542b8)] - **doc**: re-enable passing remark-lint rule (Nick Schonning) [#29961](https://github.com/nodejs/node/pull/29961)
* \[[`c0cbfae0e3`](https://github.com/nodejs/node/commit/c0cbfae0e3)] - **doc**: add server header into the discarded list of http message.headers (Huachao Mao) [#29962](https://github.com/nodejs/node/pull/29962)
* \[[`a23b5cbf61`](https://github.com/nodejs/node/commit/a23b5cbf61)] - **doc**: prepare miscellaneous docs for new markdown lint rules (Rich Trott) [#29963](https://github.com/nodejs/node/pull/29963)
* \[[`c66bc20bbf`](https://github.com/nodejs/node/commit/c66bc20bbf)] - **doc**: fix some recent nits in fs.md (vsemozhetbyt) [#29906](https://github.com/nodejs/node/pull/29906)
* \[[`1fefd7fddc`](https://github.com/nodejs/node/commit/1fefd7fddc)] - **doc**: fs dir modifications may not be reflected by dir.read (Anna Henningsen) [#29893](https://github.com/nodejs/node/pull/29893)
* \[[`66c6818473`](https://github.com/nodejs/node/commit/66c6818473)] - **doc,meta**: prefer aliases and stubs over Runtime Deprecations (Rich Trott) [#30153](https://github.com/nodejs/node/pull/30153)
* \[[`5ade490505`](https://github.com/nodejs/node/commit/5ade490505)] - **doc,meta**: reduce npm PR wait period to one week (Rich Trott) [#29922](https://github.com/nodejs/node/pull/29922)
* \[[`0ec63ee27a`](https://github.com/nodejs/node/commit/0ec63ee27a)] - **doc,n-api**: sort bottom-of-the-page references (Gabriel Schulhof) [#30124](https://github.com/nodejs/node/pull/30124)
* \[[`8a333a4519`](https://github.com/nodejs/node/commit/8a333a4519)] - **domain**: do not import util for a simple type check (Ruben Bridgewater) [#29825](https://github.com/nodejs/node/pull/29825)
* \[[`94ac44f3fc`](https://github.com/nodejs/node/commit/94ac44f3fc)] - **esm**: modify resolution order for specifier flag (Myles Borins) [#29974](https://github.com/nodejs/node/pull/29974)
* \[[`216e200fa9`](https://github.com/nodejs/node/commit/216e200fa9)] - **fs**: buffer dir entries in opendir() (Anna Henningsen) [#29893](https://github.com/nodejs/node/pull/29893)
* \[[`5959023b76`](https://github.com/nodejs/node/commit/5959023b76)] - **http2**: fix file close error condition at respondWithFd (Anna Henningsen) [#29884](https://github.com/nodejs/node/pull/29884)
* \[[`4277066afd`](https://github.com/nodejs/node/commit/4277066afd)] - **inspector**: turn platform tasks that outlive Agent into no-ops (Anna Henningsen) [#30031](https://github.com/nodejs/node/pull/30031)
* \[[`b0837fead3`](https://github.com/nodejs/node/commit/b0837fead3)] - **meta**: use contact\_links instead of issue templates (Michaël Zasso) [#30172](https://github.com/nodejs/node/pull/30172)
* \[[`2695f822bc`](https://github.com/nodejs/node/commit/2695f822bc)] - **module**: warn on require of .js inside type: module (Guy Bedford) [#29909](https://github.com/nodejs/node/pull/29909)
* \[[`ee3c3ad0f5`](https://github.com/nodejs/node/commit/ee3c3ad0f5)] - **n-api,doc**: add info about building n-api addons (Jim Schlight) [#30032](https://github.com/nodejs/node/pull/30032)
* \[[`da58301054`](https://github.com/nodejs/node/commit/da58301054)] - **net**: treat ENOTCONN at shutdown as success (Anna Henningsen) [#29912](https://github.com/nodejs/node/pull/29912)
* \[[`62bc80c906`](https://github.com/nodejs/node/commit/62bc80c906)] - **process**: add lineLength to source-map-cache (Benjamin Coe) [#29863](https://github.com/nodejs/node/pull/29863)
* \[[`ab03c29587`](https://github.com/nodejs/node/commit/ab03c29587)] - **src**: isolate->Dispose() order consistency (Shelley Vohr) [#30181](https://github.com/nodejs/node/pull/30181)
* \[[`c52b292adf`](https://github.com/nodejs/node/commit/c52b292adf)] - **src**: change env.h includes for forward declarations (Alexandre Ferrando) [#30133](https://github.com/nodejs/node/pull/30133)
* \[[`b215b1665a`](https://github.com/nodejs/node/commit/b215b1665a)] - **src**: split up InitializeContext (Shelley Vohr) [#30067](https://github.com/nodejs/node/pull/30067)
* \[[`d586070388`](https://github.com/nodejs/node/commit/d586070388)] - **src**: allow inspector without v8 platform (Shelley Vohr) [#30049](https://github.com/nodejs/node/pull/30049)
* \[[`f6655b41fa`](https://github.com/nodejs/node/commit/f6655b41fa)] - **src**: remove unnecessary std::endl usage (Daniel Bevenius) [#30003](https://github.com/nodejs/node/pull/30003)
* \[[`abfac9640e`](https://github.com/nodejs/node/commit/abfac9640e)] - **src**: make implementing CancelPendingDelayedTasks for platform optional (Anna Henningsen) [#30034](https://github.com/nodejs/node/pull/30034)
* \[[`693bf73b06`](https://github.com/nodejs/node/commit/693bf73b06)] - **src**: expose ListNode\<T>::prev\_ on postmortem metadata (legendecas) [#30027](https://github.com/nodejs/node/pull/30027)
* \[[`4b57088c25`](https://github.com/nodejs/node/commit/4b57088c25)] - **src**: fewer uses of NODE\_USE\_V8\_PLATFORM (Shelley Vohr) [#30029](https://github.com/nodejs/node/pull/30029)
* \[[`6269a3c92a`](https://github.com/nodejs/node/commit/6269a3c92a)] - **src**: remove unused iomanip include (Daniel Bevenius) [#30004](https://github.com/nodejs/node/pull/30004)
* \[[`aa0aacbba9`](https://github.com/nodejs/node/commit/aa0aacbba9)] - **src**: initialize openssl only once (Sam Roberts) [#29999](https://github.com/nodejs/node/pull/29999)
* \[[`45c5ad7922`](https://github.com/nodejs/node/commit/45c5ad7922)] - **src**: refine maps parsing for large pages (Gabriel Schulhof) [#29973](https://github.com/nodejs/node/pull/29973)
* \[[`aac2476346`](https://github.com/nodejs/node/commit/aac2476346)] - **src**: render N-API weak callbacks as cleanup hooks (Gabriel Schulhof) [#28428](https://github.com/nodejs/node/pull/28428)
* \[[`f3115c4d62`](https://github.com/nodejs/node/commit/f3115c4d62)] - **src**: fix largepages regression (Gabriel Schulhof) [#29914](https://github.com/nodejs/node/pull/29914)
* \[[`ddbf150edb`](https://github.com/nodejs/node/commit/ddbf150edb)] - **src**: remove unused using declarations in worker.cc (Daniel Bevenius) [#29883](https://github.com/nodejs/node/pull/29883)
* \[[`8a31136a95`](https://github.com/nodejs/node/commit/8a31136a95)] - **stream**: extract Readable.from in its own file (Matteo Collina) [#30140](https://github.com/nodejs/node/pull/30140)
* \[[`21a43bd2fd`](https://github.com/nodejs/node/commit/21a43bd2fd)] - **stream**: simplify uint8ArrayToBuffer helper (Luigi Pinca) [#30041](https://github.com/nodejs/node/pull/30041)
* \[[`ae390393b6`](https://github.com/nodejs/node/commit/ae390393b6)] - **stream**: remove dead code (Luigi Pinca) [#30041](https://github.com/nodejs/node/pull/30041)
* \[[`56e986aa23`](https://github.com/nodejs/node/commit/56e986aa23)] - **test**: do not run release-npm test without crypto (Michaël Zasso) [#30265](https://github.com/nodejs/node/pull/30265)
* \[[`d96e8b662e`](https://github.com/nodejs/node/commit/d96e8b662e)] - **test**: use arrow functions for callbacks (Minuk Park) [#30069](https://github.com/nodejs/node/pull/30069)
* \[[`00dab3495d`](https://github.com/nodejs/node/commit/00dab3495d)] - **test**: verify npm compatibility with releases (Michaël Zasso) [#30082](https://github.com/nodejs/node/pull/30082)
* \[[`ecf6ae89f4`](https://github.com/nodejs/node/commit/ecf6ae89f4)] - **test**: expand Worker test for non-shared ArrayBuffer (Anna Henningsen) [#30044](https://github.com/nodejs/node/pull/30044)
* \[[`2ebd1a0d3f`](https://github.com/nodejs/node/commit/2ebd1a0d3f)] - **test**: fix test runner for Python 3 on Windows (Michaël Zasso) [#30023](https://github.com/nodejs/node/pull/30023)
* \[[`9fed62f7cb`](https://github.com/nodejs/node/commit/9fed62f7cb)] - **test**: remove common.skipIfInspectorEnabled() (Rich Trott) [#29993](https://github.com/nodejs/node/pull/29993)
* \[[`3e39909022`](https://github.com/nodejs/node/commit/3e39909022)] - **test**: add cb error test for fs.close() (Matteo Rossi) [#29970](https://github.com/nodejs/node/pull/29970)
* \[[`b93c8a77a3`](https://github.com/nodejs/node/commit/b93c8a77a3)] - **test**: fix flaky doctool and test (Rich Trott) [#29979](https://github.com/nodejs/node/pull/29979)
* \[[`aec8e77ae1`](https://github.com/nodejs/node/commit/aec8e77ae1)] - **test**: fix fs benchmark test (Rich Trott) [#29967](https://github.com/nodejs/node/pull/29967)
* \[[`b9fd18f9fb`](https://github.com/nodejs/node/commit/b9fd18f9fb)] - **tools**: pull xcode\_emulation.py from node-gyp (Christian Clauss) [#30272](https://github.com/nodejs/node/pull/30272)
* \[[`2810f1aec3`](https://github.com/nodejs/node/commit/2810f1aec3)] - **tools**: update tzdata to 2019c (Myles Borins) [#30478](https://github.com/nodejs/node/pull/30478)
* \[[`41d1f166bc`](https://github.com/nodejs/node/commit/41d1f166bc)] - **tools**: fix Python 3 deprecation warning in test.py (Loris Zinsou) [#30208](https://github.com/nodejs/node/pull/30208)
* \[[`b6546736a0`](https://github.com/nodejs/node/commit/b6546736a0)] - **tools**: fix Python 3 syntax error in mac\_tool.py (Christian Clauss) [#30146](https://github.com/nodejs/node/pull/30146)
* \[[`87cb6b2418`](https://github.com/nodejs/node/commit/87cb6b2418)] - **tools**: use print() function in buildbot\_run.py (Christian Clauss) [#30148](https://github.com/nodejs/node/pull/30148)
* \[[`309c395aba`](https://github.com/nodejs/node/commit/309c395aba)] - **tools**: undefined name opts -> args in gyptest.py (Christian Clauss) [#30144](https://github.com/nodejs/node/pull/30144)
* \[[`df0fbf2e46`](https://github.com/nodejs/node/commit/df0fbf2e46)] - **tools**: git rm -r tools/v8\_gypfiles/broken (Christian Clauss) [#30149](https://github.com/nodejs/node/pull/30149)
* \[[`375f349760`](https://github.com/nodejs/node/commit/375f349760)] - **tools**: update ESLint to 6.6.0 (Colin Ihrig) [#30123](https://github.com/nodejs/node/pull/30123)
* \[[`0b6fb3d1db`](https://github.com/nodejs/node/commit/0b6fb3d1db)] - **tools**: doc: improve async workflow of generate.js (Theotime Poisseau) [#30106](https://github.com/nodejs/node/pull/30106)
* \[[`8d030131a4`](https://github.com/nodejs/node/commit/8d030131a4)] - **tools**: fix test runner in presence of NODE\_REPL\_EXTERNAL\_MODULE (Gus Caplan) [#29956](https://github.com/nodejs/node/pull/29956)
* \[[`59033f618a`](https://github.com/nodejs/node/commit/59033f618a)] - **tools**: fix GYP MSVS solution generator for Python 3 (Michaël Zasso) [#29897](https://github.com/nodejs/node/pull/29897)
* \[[`41430bea3c`](https://github.com/nodejs/node/commit/41430bea3c)] - **tools**: port Python 3 compat patches from node-gyp to gyp (Michaël Zasso) [#29897](https://github.com/nodejs/node/pull/29897)

<a id="12.13.0"></a>

## 2019-10-21, Version 12.13.0 'Erbium' (LTS), @targos

This release marks the transition of Node.js 12.x into Long Term Support (LTS)
with the codename 'Erbium'. The 12.x release line now moves into "Active LTS"
and will remain so until October 2020. After that time, it will move into
"Maintenance" until end of life in April 2022.

### Notable changes

npm was updated to 6.12.0. It now includes a version of `node-gyp` that
supports Python 3 for building native modules.

### Commits

* \[[`b59209b118`](https://github.com/nodejs/node/commit/b59209b118)] - **deps**: update npm to 6.12.0 (isaacs) [#29885](https://github.com/nodejs/node/pull/29885)
* \[[`1dde617491`](https://github.com/nodejs/node/commit/1dde617491)] - **doc**: fix --enable-source-maps flag in v12.12.0 changelog (Unlocked) [#29960](https://github.com/nodejs/node/pull/29960)
* \[[`e5e2dfabdc`](https://github.com/nodejs/node/commit/e5e2dfabdc)] - **doc**: nest code fence under unordered list (Nick Schonning) [#29915](https://github.com/nodejs/node/pull/29915)
* \[[`5b0c993d4c`](https://github.com/nodejs/node/commit/5b0c993d4c)] - **doc**: remove double word "where" (Nick Schonning) [#29915](https://github.com/nodejs/node/pull/29915)
* \[[`ad318c6cec`](https://github.com/nodejs/node/commit/ad318c6cec)] - **doc**: add brackets to implicit markdown links (Nick Schonning) [#29911](https://github.com/nodejs/node/pull/29911)
* \[[`3155ab4134`](https://github.com/nodejs/node/commit/3155ab4134)] - **doc**: use the WHATWG URL API in http code examples (Thomas Watson) [#29917](https://github.com/nodejs/node/pull/29917)
* \[[`b916ea3010`](https://github.com/nodejs/node/commit/b916ea3010)] - **doc**: escape brackets not used as markdown reference links (Nick Schonning) [#29809](https://github.com/nodejs/node/pull/29809)
* \[[`f3bf8be11c`](https://github.com/nodejs/node/commit/f3bf8be11c)] - **doc**: correct typos in security release process (Nick Schonning) [#29822](https://github.com/nodejs/node/pull/29822)
* \[[`25fa2066a2`](https://github.com/nodejs/node/commit/25fa2066a2)] - **doc**: indent code fence under list item (Nick Schonning) [#29822](https://github.com/nodejs/node/pull/29822)
* \[[`f3842892dd`](https://github.com/nodejs/node/commit/f3842892dd)] - **doc**: return type is number (exoego) [#29828](https://github.com/nodejs/node/pull/29828)
* \[[`cbd12518d4`](https://github.com/nodejs/node/commit/cbd12518d4)] - **doc**: add note about forwarding stream options (Robert Nagy) [#29857](https://github.com/nodejs/node/pull/29857)
* \[[`7683aa0bfb`](https://github.com/nodejs/node/commit/7683aa0bfb)] - **doc**: set module version 72 to node 12 (Gerhard Stoebich) [#29877](https://github.com/nodejs/node/pull/29877)
* \[[`f58fe5099a`](https://github.com/nodejs/node/commit/f58fe5099a)] - **doc**: fix tls version values (Tobias Nießen) [#29839](https://github.com/nodejs/node/pull/29839)
* \[[`8ebc94562c`](https://github.com/nodejs/node/commit/8ebc94562c)] - **fs**: do not emit 'finish' before 'open' on write empty file (Robert Nagy) [#29930](https://github.com/nodejs/node/pull/29930)
* \[[`50f066087e`](https://github.com/nodejs/node/commit/50f066087e)] - **test**: do not force the process to exit (Luigi Pinca) [#29923](https://github.com/nodejs/node/pull/29923)
* \[[`44c581ef0b`](https://github.com/nodejs/node/commit/44c581ef0b)] - **test**: add more recursive fs.rmdir() tests (Maria Paktiti) [#29815](https://github.com/nodejs/node/pull/29815)
* \[[`fc5334513c`](https://github.com/nodejs/node/commit/fc5334513c)] - **test**: remove unnecessary --expose-internals flags (Anna Henningsen) [#29886](https://github.com/nodejs/node/pull/29886)

<a id="12.12.0"></a>

## 2019-10-11, Version 12.12.0 (Current), @BridgeAR

### Notable changes

* **build**:
  * Add `--force-context-aware` flag to prevent usage of native node addons that aren't context aware [#29631](https://github.com/nodejs/node/pull/29631)
* **deprecations**:
  * Add documentation-only deprecation for `process._tickCallback()` [#29781](https://github.com/nodejs/node/pull/29781)
* **esm**:
  * Using JSON modules is experimental again [#29754](https://github.com/nodejs/node/pull/29754)
* **fs**:
  * Introduce `opendir()` and `fs.Dir` to iterate through directories [#29349](https://github.com/nodejs/node/pull/29349)
* **process**:
  * Add source-map support to stack traces by using `--enable-source-maps`[#29564](https://github.com/nodejs/node/pull/29564)
* **tls**:
  * Honor `pauseOnConnect` option [#29635](https://github.com/nodejs/node/pull/29635)
  * Add option for private keys for OpenSSL engines [#28973](https://github.com/nodejs/node/pull/28973)

### Commits

* \[[`d09f2b4170`](https://github.com/nodejs/node/commit/d09f2b4170)] - **build**: build docs on Travis (Richard Lau) [#29783](https://github.com/nodejs/node/pull/29783)
* \[[`03ec4cea30`](https://github.com/nodejs/node/commit/03ec4cea30)] - **build**: do not link against librt on linux (Sam Roberts) [#29727](https://github.com/nodejs/node/pull/29727)
* \[[`f91778d2c7`](https://github.com/nodejs/node/commit/f91778d2c7)] - **build**: remove unused libatomic on ppc64, s390x (Sam Roberts) [#29727](https://github.com/nodejs/node/pull/29727)
* \[[`ab4c53e0ef`](https://github.com/nodejs/node/commit/ab4c53e0ef)] - **crypto**: remove arbitrary UTF16 restriction (Anna Henningsen) [#29795](https://github.com/nodejs/node/pull/29795)
* \[[`75f3b28d67`](https://github.com/nodejs/node/commit/75f3b28d67)] - **crypto**: refactor array buffer view validation (Ruben Bridgewater) [#29683](https://github.com/nodejs/node/pull/29683)
* \[[`5eb013b854`](https://github.com/nodejs/node/commit/5eb013b854)] - **deps**: update archs files for OpenSSL-1.1.1 (Sam Roberts) [#29550](https://github.com/nodejs/node/pull/29550)
* \[[`1766cfcb9e`](https://github.com/nodejs/node/commit/1766cfcb9e)] - **deps**: upgrade openssl sources to 1.1.1d (Sam Roberts) [#29550](https://github.com/nodejs/node/pull/29550)
* \[[`3d88b76680`](https://github.com/nodejs/node/commit/3d88b76680)] - **deps**: patch V8 to 7.7.299.13 (Michaël Zasso) [#29869](https://github.com/nodejs/node/pull/29869)
* \[[`600478ac13`](https://github.com/nodejs/node/commit/600478ac13)] - **dgram**: use `uv_udp_try_send()` (Anna Henningsen) [#29832](https://github.com/nodejs/node/pull/29832)
* \[[`ea6b6abb91`](https://github.com/nodejs/node/commit/ea6b6abb91)] - **doc**: remove spaces inside code span elements (Nick Schonning) [#29329](https://github.com/nodejs/node/pull/29329)
* \[[`20b9ef92d1`](https://github.com/nodejs/node/commit/20b9ef92d1)] - **doc**: add more info to fs.Dir and fix typos (Jeremiah Senkpiel) [#29890](https://github.com/nodejs/node/pull/29890)
* \[[`f566cd5801`](https://github.com/nodejs/node/commit/f566cd5801)] - **doc**: remove misleading paragraph about the Legacy URL API (Jakob Krigovsky) [#29844](https://github.com/nodejs/node/pull/29844)
* \[[`a5c2154534`](https://github.com/nodejs/node/commit/a5c2154534)] - **doc**: add explicit bracket for markdown reference links (Nick Schonning) [#29808](https://github.com/nodejs/node/pull/29808)
* \[[`ea9bf4a666`](https://github.com/nodejs/node/commit/ea9bf4a666)] - **doc**: implement minor CSS improvements (XhmikosR) [#29669](https://github.com/nodejs/node/pull/29669)
* \[[`a0498606a0`](https://github.com/nodejs/node/commit/a0498606a0)] - **doc**: fix return type for crypto.createDiffieHellmanGroup() (exoego) [#29696](https://github.com/nodejs/node/pull/29696)
* \[[`a00cd17b9e`](https://github.com/nodejs/node/commit/a00cd17b9e)] - **doc**: reuse link indexes for n-api.md (legendecas) [#29787](https://github.com/nodejs/node/pull/29787)
* \[[`aea0253697`](https://github.com/nodejs/node/commit/aea0253697)] - **doc**: unify place of stability notes (Vse Mozhet Byt) [#29799](https://github.com/nodejs/node/pull/29799)
* \[[`8b4f210bf5`](https://github.com/nodejs/node/commit/8b4f210bf5)] - **doc**: add missing deprecation code (cjihrig) [#29820](https://github.com/nodejs/node/pull/29820)
* \[[`bede98128f`](https://github.com/nodejs/node/commit/bede98128f)] - **doc**: remove reference to stale CITGM job (Michael Dawson) [#29774](https://github.com/nodejs/node/pull/29774)
* \[[`014eb67117`](https://github.com/nodejs/node/commit/014eb67117)] - **(SEMVER-MINOR)** **doc**: add documentation deprecation for process.\_tickCallback (Lucas Holmquist) [#29781](https://github.com/nodejs/node/pull/29781)
* \[[`62370efe7e`](https://github.com/nodejs/node/commit/62370efe7e)] - **doc**: add dash between SHA and PR in changelog (Nick Schonning) [#29558](https://github.com/nodejs/node/pull/29558)
* \[[`d1a4aa3ca2`](https://github.com/nodejs/node/commit/d1a4aa3ca2)] - **doc**: add missing reference link values (Nick Schonning) [#29558](https://github.com/nodejs/node/pull/29558)
* \[[`de4652f55e`](https://github.com/nodejs/node/commit/de4652f55e)] - **doc**: convert old changlogs SHA links to match newer format (Nick Schonning) [#29558](https://github.com/nodejs/node/pull/29558)
* \[[`60b1f6f303`](https://github.com/nodejs/node/commit/60b1f6f303)] - **doc**: complete cut off links in old changelog (Nick Schonning) [#29558](https://github.com/nodejs/node/pull/29558)
* \[[`906245e1a4`](https://github.com/nodejs/node/commit/906245e1a4)] - **doc**: clarify --pending-deprecation effects on Buffer() usage (Rich Trott) [#29769](https://github.com/nodejs/node/pull/29769)
* \[[`401f3e7235`](https://github.com/nodejs/node/commit/401f3e7235)] - **doc**: fix nits in dgram.md (Vse Mozhet Byt) [#29761](https://github.com/nodejs/node/pull/29761)
* \[[`bc48646206`](https://github.com/nodejs/node/commit/bc48646206)] - **doc**: improve process.ppid 'added in' info (Thomas Watson) [#29772](https://github.com/nodejs/node/pull/29772)
* \[[`0b46bcaaa5`](https://github.com/nodejs/node/commit/0b46bcaaa5)] - **doc**: security maintenance processes (Sam Roberts) [#29685](https://github.com/nodejs/node/pull/29685)
* \[[`f39259c079`](https://github.com/nodejs/node/commit/f39259c079)] - **doc**: remove redundant escape (XhmikosR) [#29716](https://github.com/nodejs/node/pull/29716)
* \[[`87fb1c297a`](https://github.com/nodejs/node/commit/87fb1c297a)] - **errors**: make sure all Node.js errors show their properties (Ruben Bridgewater) [#29677](https://github.com/nodejs/node/pull/29677)
* \[[`df218ce066`](https://github.com/nodejs/node/commit/df218ce066)] - _**Revert**_ "**esm**: remove experimental status from JSON modules" (Guy Bedford) [#29754](https://github.com/nodejs/node/pull/29754)
* \[[`e7f604f495`](https://github.com/nodejs/node/commit/e7f604f495)] - **esm**: remove proxy for builtin exports (Bradley Farias) [#29737](https://github.com/nodejs/node/pull/29737)
* \[[`c56f765cf6`](https://github.com/nodejs/node/commit/c56f765cf6)] - **fs**: remove options.encoding from Dir.read\*() (Jeremiah Senkpiel) [#29908](https://github.com/nodejs/node/pull/29908)
* \[[`b76a2e502c`](https://github.com/nodejs/node/commit/b76a2e502c)] - **(SEMVER-MINOR)** **fs**: introduce `opendir()` and `fs.Dir` (Jeremiah Senkpiel) [#29349](https://github.com/nodejs/node/pull/29349)
* \[[`2bcde8309c`](https://github.com/nodejs/node/commit/2bcde8309c)] - **(SEMVER-MINOR)** **http2**: allow passing FileHandle to respondWithFD (Anna Henningsen) [#29876](https://github.com/nodejs/node/pull/29876)
* \[[`a240d45d1a`](https://github.com/nodejs/node/commit/a240d45d1a)] - **http2**: support passing options of http2.connect to net.connect (ZYSzys) [#29816](https://github.com/nodejs/node/pull/29816)
* \[[`3f153789b5`](https://github.com/nodejs/node/commit/3f153789b5)] - **http2**: set default maxConcurrentStreams (ZYSzys) [#29833](https://github.com/nodejs/node/pull/29833)
* \[[`6a989da6a0`](https://github.com/nodejs/node/commit/6a989da6a0)] - **http2**: use the latest settings (ZYSzys) [#29780](https://github.com/nodejs/node/pull/29780)
* \[[`b2cce13235`](https://github.com/nodejs/node/commit/b2cce13235)] - **inspector**: update faviconUrl (dokugo) [#29562](https://github.com/nodejs/node/pull/29562)
* \[[`60296a3612`](https://github.com/nodejs/node/commit/60296a3612)] - **lib**: make tick processor detect xcodebuild errors (Ben Noordhuis) [#29830](https://github.com/nodejs/node/pull/29830)
* \[[`9e5d691ee4`](https://github.com/nodejs/node/commit/9e5d691ee4)] - **lib**: introduce no-mixed-operators eslint rule to lib (ZYSzys) [#29834](https://github.com/nodejs/node/pull/29834)
* \[[`74a69abd12`](https://github.com/nodejs/node/commit/74a69abd12)] - **lib**: stop using prepareStackTrace (Gus Caplan) [#29777](https://github.com/nodejs/node/pull/29777)
* \[[`90562ae356`](https://github.com/nodejs/node/commit/90562ae356)] - **module**: use v8 synthetic modules (Guy Bedford) [#29846](https://github.com/nodejs/node/pull/29846)
* \[[`20896f74d6`](https://github.com/nodejs/node/commit/20896f74d6)] - **n-api,doc**: clarify napi\_finalize related APIs (legendecas) [#29797](https://github.com/nodejs/node/pull/29797)
* \[[`65c475269e`](https://github.com/nodejs/node/commit/65c475269e)] - **net**: emit close on unconnected socket (Robert Nagy) [#29803](https://github.com/nodejs/node/pull/29803)
* \[[`ae8b2b4ab7`](https://github.com/nodejs/node/commit/ae8b2b4ab7)] - **(SEMVER-MINOR)** **process**: add source-map support to stack traces (bcoe) [#29564](https://github.com/nodejs/node/pull/29564)
* \[[`3f6ce39acf`](https://github.com/nodejs/node/commit/3f6ce39acf)] - **src**: fix ESM path resolution on Windows (Thomas) [#29574](https://github.com/nodejs/node/pull/29574)
* \[[`6bfe8f47fa`](https://github.com/nodejs/node/commit/6bfe8f47fa)] - **(SEMVER-MINOR)** **src**: add buildflag to force context-aware addons (Shelley Vohr) [#29631](https://github.com/nodejs/node/pull/29631)
* \[[`6c75cc1b11`](https://github.com/nodejs/node/commit/6c75cc1b11)] - **stream**: do not deadlock duplexpair (Robert Nagy) [#29836](https://github.com/nodejs/node/pull/29836)
* \[[`320f649539`](https://github.com/nodejs/node/commit/320f649539)] - **stream**: add comment about undocumented API (Robert Nagy) [#29805](https://github.com/nodejs/node/pull/29805)
* \[[`5fdf4a474f`](https://github.com/nodejs/node/commit/5fdf4a474f)] - **test**: remove extra process.exit() (cjihrig) [#29873](https://github.com/nodejs/node/pull/29873)
* \[[`6a5d401f30`](https://github.com/nodejs/node/commit/6a5d401f30)] - **test**: remove spaces inside code span elements (Nick Schonning) [#29329](https://github.com/nodejs/node/pull/29329)
* \[[`adee99883a`](https://github.com/nodejs/node/commit/adee99883a)] - **test**: debug output for dlopen-ping-pong test (Sam Roberts) [#29818](https://github.com/nodejs/node/pull/29818)
* \[[`b309e20661`](https://github.com/nodejs/node/commit/b309e20661)] - **test**: add test for HTTP server response with Connection: close (Austin Wright) [#29836](https://github.com/nodejs/node/pull/29836)
* \[[`bf1727a3f3`](https://github.com/nodejs/node/commit/bf1727a3f3)] - **test**: add test for writable.write() argument types (Robert Nagy) [#29746](https://github.com/nodejs/node/pull/29746)
* \[[`3153dd6766`](https://github.com/nodejs/node/commit/3153dd6766)] - **test**: well-defined DH groups now verify clean (Sam Roberts) [#29550](https://github.com/nodejs/node/pull/29550)
* \[[`690a863aaa`](https://github.com/nodejs/node/commit/690a863aaa)] - **test**: simplify force-context-aware test (cjihrig) [#29705](https://github.com/nodejs/node/pull/29705)
* \[[`54ef0fd010`](https://github.com/nodejs/node/commit/54ef0fd010)] - **(SEMVER-MINOR)** **test**: --force-context-aware cli flag (Shelley Vohr) [#29631](https://github.com/nodejs/node/pull/29631)
* \[[`a7b56a5b01`](https://github.com/nodejs/node/commit/a7b56a5b01)] - **(SEMVER-MINOR)** **tls**: honor pauseOnConnect option (Robert Jensen) [#29635](https://github.com/nodejs/node/pull/29635)
* \[[`cf7b4056ca`](https://github.com/nodejs/node/commit/cf7b4056ca)] - **(SEMVER-MINOR)** **tls**: add option for private keys for OpenSSL engines (Anton Gerasimov) [#28973](https://github.com/nodejs/node/pull/28973)
* \[[`ba4946a520`](https://github.com/nodejs/node/commit/ba4946a520)] - **tools**: prohibit Error.prepareStackTrace() usage (Ruben Bridgewater) [#29827](https://github.com/nodejs/node/pull/29827)
* \[[`79f6cd3606`](https://github.com/nodejs/node/commit/79f6cd3606)] - **tools**: update ESLint to v6.5.1 (Rich Trott) [#29785](https://github.com/nodejs/node/pull/29785)
* \[[`6d88f0fef7`](https://github.com/nodejs/node/commit/6d88f0fef7)] - **vm**: refactor SourceTextModule (Gus Caplan) [#29776](https://github.com/nodejs/node/pull/29776)
* \[[`a7113048e3`](https://github.com/nodejs/node/commit/a7113048e3)] - **worker**: do not use two-arg NewIsolate (Shelley Vohr) [#29850](https://github.com/nodejs/node/pull/29850)

<a id="12.11.1"></a>

## 2019-10-01, Version 12.11.1 (Current), @targos

### Notable changes

* **build**:
  * This release fixes a regression that prevented from building Node.js using
    the official source tarball (Richard Lau) [#29712](https://github.com/nodejs/node/pull/29712).
* **deps**:
  * Updated small-icu data to support "unit" style in the `Intl.NumberFormat` API (Michaël Zasso) [#29735](https://github.com/nodejs/node/pull/29735).

### Commits

* \[[`35e1d8c5ba`](https://github.com/nodejs/node/commit/35e1d8c5ba)] - **build**: include deps/v8/test/torque in source tarball (Richard Lau) [#29712](https://github.com/nodejs/node/pull/29712)
* \[[`ae461964a7`](https://github.com/nodejs/node/commit/ae461964a7)] - **build,win**: goto lint only after defining node\_exe (João Reis) [#29616](https://github.com/nodejs/node/pull/29616)
* \[[`588b388181`](https://github.com/nodejs/node/commit/588b388181)] - **crypto**: use byteLength in timingSafeEqual (Tobias Nießen) [#29657](https://github.com/nodejs/node/pull/29657)
* \[[`298d92785c`](https://github.com/nodejs/node/commit/298d92785c)] - **deps**: enable unit data in small-icu (Michaël Zasso) [#29735](https://github.com/nodejs/node/pull/29735)
* \[[`0041f1c0d3`](https://github.com/nodejs/node/commit/0041f1c0d3)] - **doc**: sync security policy with nodejs.org (Sam Roberts) [#29682](https://github.com/nodejs/node/pull/29682)
* \[[`038cbb08de`](https://github.com/nodejs/node/commit/038cbb08de)] - **doc**: fix output in inspector HeapProfile example (Kirill Fomichev) [#29711](https://github.com/nodejs/node/pull/29711)
* \[[`d86f10cf0b`](https://github.com/nodejs/node/commit/d86f10cf0b)] - **doc**: add KeyObject to type for crypto.createDecipheriv() argument (exoego) [#29689](https://github.com/nodejs/node/pull/29689)
* \[[`1303e3551f`](https://github.com/nodejs/node/commit/1303e3551f)] - **doc**: clarify description of `readable.push()` method (imhype) [#29687](https://github.com/nodejs/node/pull/29687)
* \[[`d258e0242c`](https://github.com/nodejs/node/commit/d258e0242c)] - **doc**: clarify stream errors while reading and writing (Robert Nagy) [#29653](https://github.com/nodejs/node/pull/29653)
* \[[`0fc85ff96a`](https://github.com/nodejs/node/commit/0fc85ff96a)] - **doc**: specify `display=fallback` for Google Fonts (XhmikosR) [#29688](https://github.com/nodejs/node/pull/29688)
* \[[`c2791dcd9c`](https://github.com/nodejs/node/commit/c2791dcd9c)] - **doc**: fix some recent nits (Vse Mozhet Byt) [#29670](https://github.com/nodejs/node/pull/29670)
* \[[`7a6b05a26f`](https://github.com/nodejs/node/commit/7a6b05a26f)] - **doc**: fix 404 links (XhmikosR) [#29661](https://github.com/nodejs/node/pull/29661)
* \[[`2b76cb6dda`](https://github.com/nodejs/node/commit/2b76cb6dda)] - **doc**: remove align from tables (XhmikosR) [#29668](https://github.com/nodejs/node/pull/29668)
* \[[`3de1fc6958`](https://github.com/nodejs/node/commit/3de1fc6958)] - **doc**: document that iv may be null when using createCipheriv() (Ruben Bridgewater) [#29684](https://github.com/nodejs/node/pull/29684)
* \[[`91e4cc7500`](https://github.com/nodejs/node/commit/91e4cc7500)] - **doc**: update AUTHORS list (Anna Henningsen) [#29608](https://github.com/nodejs/node/pull/29608)
* \[[`2ea4cc0732`](https://github.com/nodejs/node/commit/2ea4cc0732)] - **doc**: clarify pipeline stream cleanup (Robert Nagy) [#29738](https://github.com/nodejs/node/pull/29738)
* \[[`ab060bfdab`](https://github.com/nodejs/node/commit/ab060bfdab)] - **doc**: clarify fs.symlink() usage (Simon A. Eugster) [#29700](https://github.com/nodejs/node/pull/29700)
* \[[`b5c24dfbe8`](https://github.com/nodejs/node/commit/b5c24dfbe8)] - **doc**: fix type of atime/mtime (TATSUNO Yasuhiro) [#29666](https://github.com/nodejs/node/pull/29666)
* \[[`6579b1a547`](https://github.com/nodejs/node/commit/6579b1a547)] - **doc,http**: indicate callback is optional for message.setTimeout() (Trivikram Kamat) [#29654](https://github.com/nodejs/node/pull/29654)
* \[[`a04fc86723`](https://github.com/nodejs/node/commit/a04fc86723)] - **http2**: optimize the altsvc Max bytes limit, define and use constants (rickyes) [#29673](https://github.com/nodejs/node/pull/29673)
* \[[`d1f4befd09`](https://github.com/nodejs/node/commit/d1f4befd09)] - **module**: pass full URL to loader for top-level load (Guy Bedford) [#29736](https://github.com/nodejs/node/pull/29736)
* \[[`3f028551a8`](https://github.com/nodejs/node/commit/3f028551a8)] - **module**: move cjs type check behind flag (Guy Bedford) [#29732](https://github.com/nodejs/node/pull/29732)
* \[[`c3a1303bc2`](https://github.com/nodejs/node/commit/c3a1303bc2)] - **src**: rename --loader to --experimental-loader (Alex Aubuchon) [#29752](https://github.com/nodejs/node/pull/29752)
* \[[`17c3478d78`](https://github.com/nodejs/node/commit/17c3478d78)] - **src**: fix asan build for gcc/clang (David Carlier) [#29383](https://github.com/nodejs/node/pull/29383)
* \[[`64740d44b5`](https://github.com/nodejs/node/commit/64740d44b5)] - **src**: fix compiler warning in inspector\_profiler.cc (Daniel Bevenius) [#29660](https://github.com/nodejs/node/pull/29660)
* \[[`a86b71f745`](https://github.com/nodejs/node/commit/a86b71f745)] - **src**: disconnect inspector before exiting out of fatal exception (Joyee Cheung) [#29611](https://github.com/nodejs/node/pull/29611)
* \[[`8d88010277`](https://github.com/nodejs/node/commit/8d88010277)] - **src**: try showing stack traces when process.\_fatalException is not set (Joyee Cheung) [#29624](https://github.com/nodejs/node/pull/29624)
* \[[`2a6b7b0476`](https://github.com/nodejs/node/commit/2a6b7b0476)] - **test**: fix flaky test-cluster-net-listen-ipv6only-none (Rich Trott) [#29681](https://github.com/nodejs/node/pull/29681)
* \[[`69f26340e9`](https://github.com/nodejs/node/commit/69f26340e9)] - **tls**: simplify setSecureContext() option parsing (cjihrig) [#29704](https://github.com/nodejs/node/pull/29704)
* \[[`c361180c07`](https://github.com/nodejs/node/commit/c361180c07)] - **tools**: make mailmap processing for author list case-insensitive (Anna Henningsen) [#29608](https://github.com/nodejs/node/pull/29608)
* \[[`ef033d046a`](https://github.com/nodejs/node/commit/ef033d046a)] - **worker**: fix process.\_fatalException return type (Ruben Bridgewater) [#29706](https://github.com/nodejs/node/pull/29706)
* \[[`04df7dbadb`](https://github.com/nodejs/node/commit/04df7dbadb)] - **worker**: keep allocators for transferred SAB instances alive longer (Anna Henningsen) [#29637](https://github.com/nodejs/node/pull/29637)

<a id="12.11.0"></a>

## 2019-09-25, Version 12.11.0 (Current), @BridgeAR

### Notable changes

* **crypto**:
  * Add `oaepLabel` option [#29489](https://github.com/nodejs/node/pull/29489)
* **deps**:
  * Update V8 to 7.7.299.11 [#28918](https://github.com/nodejs/node/pull/28918)
    * More efficient memory handling
    * Stack trace serialization got faster
    * The `Intl.NumberFormat` API gained new functionality
    * For more information: <https://v8.dev/blog/v8-release-77>
* **events**:
  * Add support for `EventTarget` in `once` [#29498](https://github.com/nodejs/node/pull/29498)
* **fs**:
  * Expose memory file mapping flag `UV_FS_O_FILEMAP` [#29260](https://github.com/nodejs/node/pull/29260)
* **inspector**:
  * New API - `Session.connectToMainThread` [#28870](https://github.com/nodejs/node/pull/28870)
* **process**:
  * Initial SourceMap support via `env.NODE_V8_COVERAGE` [#28960](https://github.com/nodejs/node/pull/28960)
* **stream**:
  * Make `_write()` optional when `_writev()` is implemented [#29639](https://github.com/nodejs/node/pull/29639)
* **tls**:
  * Add option to override signature algorithms [#29598](https://github.com/nodejs/node/pull/29598)
* **util**:
  * Add `encodeInto` to `TextEncoder` [#29524](https://github.com/nodejs/node/pull/29524)
* **worker**:
  * The `worker_thread` module is now stable [#29512](https://github.com/nodejs/node/pull/29512)

### Commits

* \[[`b9c7c9002f`](https://github.com/nodejs/node/commit/b9c7c9002f)] - **benchmark**: improve process.env benchmarks (Anna Henningsen) [#29188](https://github.com/nodejs/node/pull/29188)
* \[[`6b8951231c`](https://github.com/nodejs/node/commit/6b8951231c)] - **bootstrap**: add exception handling for profiler bootstrap (Shobhit Chittora) [#29552](https://github.com/nodejs/node/pull/29552)
* \[[`c052967636`](https://github.com/nodejs/node/commit/c052967636)] - **bootstrap**: provide usable error on missing internal module (Jeremiah Senkpiel) [#29593](https://github.com/nodejs/node/pull/29593)
* \[[`5c24bc6c68`](https://github.com/nodejs/node/commit/5c24bc6c68)] - **build**: do not indent assignments in Makefile (Joyee Cheung) [#29623](https://github.com/nodejs/node/pull/29623)
* \[[`f90740d734`](https://github.com/nodejs/node/commit/f90740d734)] - **build**: allow clang 10+ in configure.py (Kamil Rytarowski) [#29541](https://github.com/nodejs/node/pull/29541)
* \[[`c304594536`](https://github.com/nodejs/node/commit/c304594536)] - **build**: re-run configure on node\_version.h change (Anna Henningsen) [#29510](https://github.com/nodejs/node/pull/29510)
* \[[`f622771079`](https://github.com/nodejs/node/commit/f622771079)] - **build**: improve `make coverage` (Anna Henningsen) [#29487](https://github.com/nodejs/node/pull/29487)
* \[[`c1695c6635`](https://github.com/nodejs/node/commit/c1695c6635)] - **build**: add comment to .travis.yml on how to test Py3 (cclauss) [#29473](https://github.com/nodejs/node/pull/29473)
* \[[`6f50c3f391`](https://github.com/nodejs/node/commit/6f50c3f391)] - **build**: update minimum AIX OS level (Michael Dawson) [#29476](https://github.com/nodejs/node/pull/29476)
* \[[`ee18238f55`](https://github.com/nodejs/node/commit/ee18238f55)] - **build**: remove experimental Python 3 tests (Christian Clauss) [#29413](https://github.com/nodejs/node/pull/29413)
* \[[`fe46054b14`](https://github.com/nodejs/node/commit/fe46054b14)] - **(SEMVER-MINOR)** **build**: reset embedder string to "-node.0" (Michaël Zasso) [#28918](https://github.com/nodejs/node/pull/28918)
* \[[`42fd139279`](https://github.com/nodejs/node/commit/42fd139279)] - **build,win**: fix Python detection on localized OS (João Reis) [#29423](https://github.com/nodejs/node/pull/29423)
* \[[`f61c5097e2`](https://github.com/nodejs/node/commit/f61c5097e2)] - **console**: skip/strip %c formatting (Gus Caplan) [#29606](https://github.com/nodejs/node/pull/29606)
* \[[`68630c5b65`](https://github.com/nodejs/node/commit/68630c5b65)] - **console,util**: fix missing recursion end while inspecting prototypes (Ruben Bridgewater) [#29647](https://github.com/nodejs/node/pull/29647)
* \[[`99c2cd8f08`](https://github.com/nodejs/node/commit/99c2cd8f08)] - **crypto**: use BoringSSL-compatible flag getter (Shelley Vohr) [#29604](https://github.com/nodejs/node/pull/29604)
* \[[`dd5d944005`](https://github.com/nodejs/node/commit/dd5d944005)] - **(SEMVER-MINOR)** **crypto**: fix OpenSSL return code handling (Tobias Nießen) [#29489](https://github.com/nodejs/node/pull/29489)
* \[[`54f327b4dc`](https://github.com/nodejs/node/commit/54f327b4dc)] - **(SEMVER-MINOR)** **crypto**: add oaepLabel option (Tobias Nießen) [#29489](https://github.com/nodejs/node/pull/29489)
* \[[`5d60adf38b`](https://github.com/nodejs/node/commit/5d60adf38b)] - **deps**: patch V8 to 7.7.299.11 (Michaël Zasso) [#29628](https://github.com/nodejs/node/pull/29628)
* \[[`c718c606c8`](https://github.com/nodejs/node/commit/c718c606c8)] - **deps**: V8: cherry-pick deac757 (Benjamin Coe) [#29626](https://github.com/nodejs/node/pull/29626)
* \[[`e4a51ad980`](https://github.com/nodejs/node/commit/e4a51ad980)] - **deps**: patch V8 to 7.7.299.10 (Thomas) [#29472](https://github.com/nodejs/node/pull/29472)
* \[[`bc3c0b2d65`](https://github.com/nodejs/node/commit/bc3c0b2d65)] - **deps**: V8: cherry-pick 35c6d4d (Sam Roberts) [#29585](https://github.com/nodejs/node/pull/29585)
* \[[`fa7de9b27f`](https://github.com/nodejs/node/commit/fa7de9b27f)] - **deps**: update npm to 6.11.3 (claudiahdz) [#29430](https://github.com/nodejs/node/pull/29430)
* \[[`f5f238de6c`](https://github.com/nodejs/node/commit/f5f238de6c)] - **deps**: upgrade to libuv 1.32.0 (cjihrig) [#29508](https://github.com/nodejs/node/pull/29508)
* \[[`7957b392e4`](https://github.com/nodejs/node/commit/7957b392e4)] - **deps**: patch V8 to 7.7.299.8 (Michaël Zasso) [#29336](https://github.com/nodejs/node/pull/29336)
* \[[`90713c6697`](https://github.com/nodejs/node/commit/90713c6697)] - **(SEMVER-MINOR)** **deps**: patch V8 to be API/ABI compatible with 7.4 (from 7.7) (Michaël Zasso) [#29241](https://github.com/nodejs/node/pull/29241)
* \[[`e95f866956`](https://github.com/nodejs/node/commit/e95f866956)] - **deps**: patch V8 to be API/ABI compatible with 7.4 (from 7.6) (Michaël Zasso) [#28955](https://github.com/nodejs/node/pull/28955)
* \[[`4eeb2a99e5`](https://github.com/nodejs/node/commit/4eeb2a99e5)] - **(SEMVER-MINOR)** **deps**: patch V8 to be API/ABI compatible with 7.4 (from 7.5) (Michaël Zasso) [#28005](https://github.com/nodejs/node/pull/28005)
* \[[`60efc5fd52`](https://github.com/nodejs/node/commit/60efc5fd52)] - **deps**: V8: cherry-pick e3d7f8a (cclauss) [#29105](https://github.com/nodejs/node/pull/29105)
* \[[`d1bedbe717`](https://github.com/nodejs/node/commit/d1bedbe717)] - **(SEMVER-MINOR)** **deps**: V8: fix linking issue for MSVS (Refael Ackermann) [#28016](https://github.com/nodejs/node/pull/28016)
* \[[`19e38e0def`](https://github.com/nodejs/node/commit/19e38e0def)] - **(SEMVER-MINOR)** **deps**: V8: fix BUILDING\_V8\_SHARED issues (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`8aaa0abd26`](https://github.com/nodejs/node/commit/8aaa0abd26)] - **(SEMVER-MINOR)** **deps**: V8: add workaround for MSVC optimizer bug (Refael Ackermann) [#28016](https://github.com/nodejs/node/pull/28016)
* \[[`07ed874446`](https://github.com/nodejs/node/commit/07ed874446)] - **(SEMVER-MINOR)** **deps**: V8: use ATOMIC\_VAR\_INIT instead of std::atomic\_init (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`1ed3909ca3`](https://github.com/nodejs/node/commit/1ed3909ca3)] - **(SEMVER-MINOR)** **deps**: V8: forward declaration of `Rtl*FunctionTable` (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`242f6174e5`](https://github.com/nodejs/node/commit/242f6174e5)] - **(SEMVER-MINOR)** **deps**: V8: patch register-arm64.h (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`63093e9d49`](https://github.com/nodejs/node/commit/63093e9d49)] - **(SEMVER-MINOR)** **deps**: V8: update postmortem metadata generation script (cjihrig) [#28918](https://github.com/nodejs/node/pull/28918)
* \[[`b54ee2185e`](https://github.com/nodejs/node/commit/b54ee2185e)] - **(SEMVER-MINOR)** **deps**: V8: silence irrelevant warning (Michaël Zasso) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`c6f97bb7ce`](https://github.com/nodejs/node/commit/c6f97bb7ce)] - **(SEMVER-MINOR)** **deps**: V8: un-cherry-pick bd019bd (Refael Ackermann) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`5df55c2626`](https://github.com/nodejs/node/commit/5df55c2626)] - **(SEMVER-MINOR)** **deps**: V8: fix filename manipulation for Windows (Refael Ackermann) [#28016](https://github.com/nodejs/node/pull/28016)
* \[[`80ccae000e`](https://github.com/nodejs/node/commit/80ccae000e)] - **(SEMVER-MINOR)** **deps**: update V8 to 7.7.299.4 (Michaël Zasso) [#28918](https://github.com/nodejs/node/pull/28918)
* \[[`325de437b3`](https://github.com/nodejs/node/commit/325de437b3)] - **doc**: update N-API version matrix (Gabriel Schulhof) [#29461](https://github.com/nodejs/node/pull/29461)
* \[[`2707beb8b8`](https://github.com/nodejs/node/commit/2707beb8b8)] - **doc**: add code example to process.throwDeprecation property (Juan José Arboleda) [#29495](https://github.com/nodejs/node/pull/29495)
* \[[`edaa2eebd6`](https://github.com/nodejs/node/commit/edaa2eebd6)] - **doc**: fix some signatures of .end() methods (Vse Mozhet Byt) [#29615](https://github.com/nodejs/node/pull/29615)
* \[[`13d173f131`](https://github.com/nodejs/node/commit/13d173f131)] - **doc**: remove the suffix number of the anchor link (Maledong) [#29468](https://github.com/nodejs/node/pull/29468)
* \[[`3ba64646ed`](https://github.com/nodejs/node/commit/3ba64646ed)] - **doc**: explain stream.finished cleanup (Robert Nagy) [#28935](https://github.com/nodejs/node/pull/28935)
* \[[`84b353cf5d`](https://github.com/nodejs/node/commit/84b353cf5d)] - **doc**: fix require call for spawn() in code example (Marian Rusnak) [#29621](https://github.com/nodejs/node/pull/29621)
* \[[`39b17706ea`](https://github.com/nodejs/node/commit/39b17706ea)] - **doc**: make minor improvements to stream.md (Robert Nagy) [#28970](https://github.com/nodejs/node/pull/28970)
* \[[`50b5ad1638`](https://github.com/nodejs/node/commit/50b5ad1638)] - **doc**: fix nits in net.md (Vse Mozhet Byt) [#29577](https://github.com/nodejs/node/pull/29577)
* \[[`4954792991`](https://github.com/nodejs/node/commit/4954792991)] - **doc**: correct trivial misspelling in AUTHORS (gcr) [#29597](https://github.com/nodejs/node/pull/29597)
* \[[`0074c8adb7`](https://github.com/nodejs/node/commit/0074c8adb7)] - **doc**: update list style in misc README docs (Rich Trott) [#29594](https://github.com/nodejs/node/pull/29594)
* \[[`38028ef818`](https://github.com/nodejs/node/commit/38028ef818)] - **doc**: add missing complete property to http2 docs (Javier Ledezma) [#29571](https://github.com/nodejs/node/pull/29571)
* \[[`55631f4f0a`](https://github.com/nodejs/node/commit/55631f4f0a)] - **doc**: add leap second behavior notes for napi methods (Levhita) [#29569](https://github.com/nodejs/node/pull/29569)
* \[[`7fd32619c1`](https://github.com/nodejs/node/commit/7fd32619c1)] - **doc**: explain esm options for package authors (Geoffrey Booth) [#29497](https://github.com/nodejs/node/pull/29497)
* \[[`f2217cdafe`](https://github.com/nodejs/node/commit/f2217cdafe)] - **doc**: update experimental loader hooks example code (Denis Zavershinskiy) [#29373](https://github.com/nodejs/node/pull/29373)
* \[[`bf08c08384`](https://github.com/nodejs/node/commit/bf08c08384)] - **doc**: use consistent unordered list style (Nick Schonning) [#29516](https://github.com/nodejs/node/pull/29516)
* \[[`ca8e87a6d3`](https://github.com/nodejs/node/commit/ca8e87a6d3)] - **doc**: add Bethany to TSC (Michael Dawson) [#29546](https://github.com/nodejs/node/pull/29546)
* \[[`aa541bbc88`](https://github.com/nodejs/node/commit/aa541bbc88)] - **doc**: add Tobias to the TSC (Michael Dawson) [#29545](https://github.com/nodejs/node/pull/29545)
* \[[`9abee075ad`](https://github.com/nodejs/node/commit/9abee075ad)] - **doc**: mention unit for process.hrtime.bigint() (Anna Henningsen) [#29482](https://github.com/nodejs/node/pull/29482)
* \[[`3aea277b72`](https://github.com/nodejs/node/commit/3aea277b72)] - **doc**: add documentation for stream readableFlowing (Chetan Karande) [#29506](https://github.com/nodejs/node/pull/29506)
* \[[`a262e2f8d4`](https://github.com/nodejs/node/commit/a262e2f8d4)] - **doc**: indent child list items for remark-lint (Nick Schonning) [#29488](https://github.com/nodejs/node/pull/29488)
* \[[`2a5340144c`](https://github.com/nodejs/node/commit/2a5340144c)] - **doc**: space around lists (Nick Schonning) [#29467](https://github.com/nodejs/node/pull/29467)
* \[[`9e63f914da`](https://github.com/nodejs/node/commit/9e63f914da)] - **doc**: exitedAfterDisconnect value can be false (Nimit Aggarwal) [#29404](https://github.com/nodejs/node/pull/29404)
* \[[`b1509e8f8e`](https://github.com/nodejs/node/commit/b1509e8f8e)] - **doc**: remove wrong escapes (XhmikosR) [#29452](https://github.com/nodejs/node/pull/29452)
* \[[`7dd897f49a`](https://github.com/nodejs/node/commit/7dd897f49a)] - **doc**: prepare markdown files for more stringent blank-line linting (Rich Trott) [#29447](https://github.com/nodejs/node/pull/29447)
* \[[`a9d16b5e30`](https://github.com/nodejs/node/commit/a9d16b5e30)] - **doc**: simplify wording in n-api doc (Michael Dawson) [#29441](https://github.com/nodejs/node/pull/29441)
* \[[`c95e9ca6dc`](https://github.com/nodejs/node/commit/c95e9ca6dc)] - **doc**: update release guide with notes for major releases (James M Snell) [#25497](https://github.com/nodejs/node/pull/25497)
* \[[`a7331da863`](https://github.com/nodejs/node/commit/a7331da863)] - **doc**: indent ordered list child content (Nick Schonning) [#29332](https://github.com/nodejs/node/pull/29332)
* \[[`32bb58ba9c`](https://github.com/nodejs/node/commit/32bb58ba9c)] - **doc**: fix unsafe writable stream code example (Chetan Karande) [#29425](https://github.com/nodejs/node/pull/29425)
* \[[`735ef8b235`](https://github.com/nodejs/node/commit/735ef8b235)] - **doc**: async\_hooks.createHook promiseResolve option (Ben Noordhuis) [#29405](https://github.com/nodejs/node/pull/29405)
* \[[`844b45bf4f`](https://github.com/nodejs/node/commit/844b45bf4f)] - **doc**: change urls directly from 'http' to 'https' (Maledong) [#29422](https://github.com/nodejs/node/pull/29422)
* \[[`4374d28c52`](https://github.com/nodejs/node/commit/4374d28c52)] - **doc**: use consistent indenting for unordered list items (Nick Schonning) [#29390](https://github.com/nodejs/node/pull/29390)
* \[[`835d1cabf6`](https://github.com/nodejs/node/commit/835d1cabf6)] - **doc**: start unorded lists at start of line (Nick Schonning) [#29390](https://github.com/nodejs/node/pull/29390)
* \[[`8023e43e1d`](https://github.com/nodejs/node/commit/8023e43e1d)] - **doc**: change the 'txt' to 'console' for a command (Maledong) [#29389](https://github.com/nodejs/node/pull/29389)
* \[[`b9c082d764`](https://github.com/nodejs/node/commit/b9c082d764)] - **esm**: make dynamic import work in the REPL (Bradley Farias) [#29437](https://github.com/nodejs/node/pull/29437)
* \[[`0a47d06150`](https://github.com/nodejs/node/commit/0a47d06150)] - **events**: improve performance of EventEmitter.emit (Matteo Collina) [#29633](https://github.com/nodejs/node/pull/29633)
* \[[`9150c4dc72`](https://github.com/nodejs/node/commit/9150c4dc72)] - **(SEMVER-MINOR)** **events**: add support for EventTarget in once (Jenia) [#29498](https://github.com/nodejs/node/pull/29498)
* \[[`67f5de9b34`](https://github.com/nodejs/node/commit/67f5de9b34)] - **fs**: remove unnecessary argument check (Robert Nagy) [#29043](https://github.com/nodejs/node/pull/29043)
* \[[`a20a8f48f7`](https://github.com/nodejs/node/commit/a20a8f48f7)] - **gyp**: make StringIO work in ninja.py (Christian Clauss) [#29414](https://github.com/nodejs/node/pull/29414)
* \[[`31b0b52a71`](https://github.com/nodejs/node/commit/31b0b52a71)] - **http**: refactor responseKeepAlive() (Robert Nagy) [#28700](https://github.com/nodejs/node/pull/28700)
* \[[`6a7d24b69c`](https://github.com/nodejs/node/commit/6a7d24b69c)] - **http2**: do not crash on stream listener removal w/ destroyed session (Anna Henningsen) [#29459](https://github.com/nodejs/node/pull/29459)
* \[[`fa949ca365`](https://github.com/nodejs/node/commit/fa949ca365)] - **http2**: send out pending data earlier (Anna Henningsen) [#29398](https://github.com/nodejs/node/pull/29398)
* \[[`d6ba106f8c`](https://github.com/nodejs/node/commit/d6ba106f8c)] - **http2**: do not start reading after write if new write is on wire (Anna Henningsen) [#29399](https://github.com/nodejs/node/pull/29399)
* \[[`a268658496`](https://github.com/nodejs/node/commit/a268658496)] - **(SEMVER-MINOR)** **inspector**: new API - Session.connectToMainThread (Eugene Ostroukhov) [#28870](https://github.com/nodejs/node/pull/28870)
* \[[`144aeeac68`](https://github.com/nodejs/node/commit/144aeeac68)] - **lib**: remove the use of util.isFunction (himself65) [#29566](https://github.com/nodejs/node/pull/29566)
* \[[`91d99ce41c`](https://github.com/nodejs/node/commit/91d99ce41c)] - **(SEMVER-MINOR)** **lib,test**: fix error message check after V8 update (Michaël Zasso) [#28918](https://github.com/nodejs/node/pull/28918)
* \[[`13fa966b7b`](https://github.com/nodejs/node/commit/13fa966b7b)] - **module**: error for CJS .js load within type: module (Guy Bedford) [#29492](https://github.com/nodejs/node/pull/29492)
* \[[`ce45aae2ab`](https://github.com/nodejs/node/commit/ce45aae2ab)] - **module**: reintroduce package exports dot main (Guy Bedford) [#29494](https://github.com/nodejs/node/pull/29494)
* \[[`8474b82e35`](https://github.com/nodejs/node/commit/8474b82e35)] - **n-api**: delete callback bundle via reference (Gabriel Schulhof) [#29479](https://github.com/nodejs/node/pull/29479)
* \[[`50d7c39d91`](https://github.com/nodejs/node/commit/50d7c39d91)] - **n-api**: mark version 5 N-APIs as stable (Gabriel Schulhof) [#29401](https://github.com/nodejs/node/pull/29401)
* \[[`6b30802471`](https://github.com/nodejs/node/commit/6b30802471)] - **perf\_hooks**: remove non-existent entries from inspect (Kirill Fomichev) [#29528](https://github.com/nodejs/node/pull/29528)
* \[[`c146fff307`](https://github.com/nodejs/node/commit/c146fff307)] - **perf\_hooks**: ignore duplicated entries in observer (Kirill Fomichev) [#29442](https://github.com/nodejs/node/pull/29442)
* \[[`9b4a49c844`](https://github.com/nodejs/node/commit/9b4a49c844)] - **perf\_hooks**: remove GC callbacks on zero observers count (Kirill Fomichev) [#29444](https://github.com/nodejs/node/pull/29444)
* \[[`b30c40bd5d`](https://github.com/nodejs/node/commit/b30c40bd5d)] - **perf\_hooks**: import http2 only once (Kirill Fomichev) [#29419](https://github.com/nodejs/node/pull/29419)
* \[[`95431eace9`](https://github.com/nodejs/node/commit/95431eace9)] - **policy**: minor perf opts and cleanup (Bradley Farias) [#29322](https://github.com/nodejs/node/pull/29322)
* \[[`6ba39d4fe4`](https://github.com/nodejs/node/commit/6ba39d4fe4)] - **(SEMVER-MINOR)** **process**: initial SourceMap support via NODE\_V8\_COVERAGE (Benjamin Coe) [#28960](https://github.com/nodejs/node/pull/28960)
* \[[`03a3468666`](https://github.com/nodejs/node/commit/03a3468666)] - **process**: use public readableFlowing property (Chetan Karande) [#29502](https://github.com/nodejs/node/pull/29502)
* \[[`a5bd7e3b2a`](https://github.com/nodejs/node/commit/a5bd7e3b2a)] - **repl**: convert var to let and const (Lucas Holmquist) [#29575](https://github.com/nodejs/node/pull/29575)
* \[[`7eae707fd9`](https://github.com/nodejs/node/commit/7eae707fd9)] - **repl**: fix bug in fs module autocompletion (zhangyongsheng) [#29555](https://github.com/nodejs/node/pull/29555)
* \[[`596dd9fe34`](https://github.com/nodejs/node/commit/596dd9fe34)] - **repl**: add autocomplete support for fs.promises (antsmartian) [#29400](https://github.com/nodejs/node/pull/29400)
* \[[`70a0c170d4`](https://github.com/nodejs/node/commit/70a0c170d4)] - **repl**: add missing variable declaration (Lucas Holmquist) [#29535](https://github.com/nodejs/node/pull/29535)
* \[[`3878e1ed31`](https://github.com/nodejs/node/commit/3878e1ed31)] - **src**: perform check before running in runMicrotasks() (Jeremy Apthorp) [#29581](https://github.com/nodejs/node/pull/29581)
* \[[`6f8ef2cbab`](https://github.com/nodejs/node/commit/6f8ef2cbab)] - **src**: discard remaining foreground tasks on platform shutdown (Anna Henningsen) [#29587](https://github.com/nodejs/node/pull/29587)
* \[[`f84f1dbd98`](https://github.com/nodejs/node/commit/f84f1dbd98)] - **src**: fix closing weak `HandleWrap`s on GC (Anna Henningsen) [#29640](https://github.com/nodejs/node/pull/29640)
* \[[`6284b498b4`](https://github.com/nodejs/node/commit/6284b498b4)] - **src**: use libuv to get env vars (Anna Henningsen) [#29188](https://github.com/nodejs/node/pull/29188)
* \[[`3a6bc90c29`](https://github.com/nodejs/node/commit/3a6bc90c29)] - **src**: re-delete Atomics.wake (Gus Caplan) [#29586](https://github.com/nodejs/node/pull/29586)
* \[[`51a1dfab94`](https://github.com/nodejs/node/commit/51a1dfab94)] - **src**: print exceptions from PromiseRejectCallback (Anna Henningsen) [#29513](https://github.com/nodejs/node/pull/29513)
* \[[`4a5ba60e00`](https://github.com/nodejs/node/commit/4a5ba60e00)] - **src**: modified RealEnvStore methods to use libuv functions (Devendra Satram) [#27310](https://github.com/nodejs/node/pull/27310)
* \[[`67aa5ef12b`](https://github.com/nodejs/node/commit/67aa5ef12b)] - **src**: make ELDHistogram a HandleWrap (Anna Henningsen) [#29317](https://github.com/nodejs/node/pull/29317)
* \[[`5c3d484c21`](https://github.com/nodejs/node/commit/5c3d484c21)] - **src**: check microtasks before running them (Shelley Vohr) [#29434](https://github.com/nodejs/node/pull/29434)
* \[[`010d29d74f`](https://github.com/nodejs/node/commit/010d29d74f)] - **src**: fix ValidateDSAParameters when fips is enabled (Daniel Bevenius) [#29407](https://github.com/nodejs/node/pull/29407)
* \[[`59b464026f`](https://github.com/nodejs/node/commit/59b464026f)] - **(SEMVER-MINOR)** **src**: update v8abbr.h for V8 7.7 (cjihrig) [#28918](https://github.com/nodejs/node/pull/28918)
* \[[`78af92dda5`](https://github.com/nodejs/node/commit/78af92dda5)] - **(SEMVER-MINOR)** **src,lib**: expose memory file mapping flag (João Reis) [#29260](https://github.com/nodejs/node/pull/29260)
* \[[`f016823929`](https://github.com/nodejs/node/commit/f016823929)] - **stream**: add test for multiple .push(null) (Chetan Karande) [#29645](https://github.com/nodejs/node/pull/29645)
* \[[`b1008973e9`](https://github.com/nodejs/node/commit/b1008973e9)] - **stream**: cleanup use of internal ended state (Chetan Karande) [#29645](https://github.com/nodejs/node/pull/29645)
* \[[`e71bdadf52`](https://github.com/nodejs/node/commit/e71bdadf52)] - **(SEMVER-MINOR)** **stream**: make \_write() optional when \_writev() is implemented (Robert Nagy) [#29639](https://github.com/nodejs/node/pull/29639)
* \[[`123437bcc3`](https://github.com/nodejs/node/commit/123437bcc3)] - **stream**: apply special logic in removeListener for readable.off() (Robert Nagy) [#29486](https://github.com/nodejs/node/pull/29486)
* \[[`322bc6f0a6`](https://github.com/nodejs/node/commit/322bc6f0a6)] - **stream**: do not call \_read() after destroy() (Robert Nagy) [#29491](https://github.com/nodejs/node/pull/29491)
* \[[`78cbdf3286`](https://github.com/nodejs/node/commit/78cbdf3286)] - **stream**: optimize creation (Robert Nagy) [#29135](https://github.com/nodejs/node/pull/29135)
* \[[`2dc52ad09c`](https://github.com/nodejs/node/commit/2dc52ad09c)] - **stream**: simplify isUint8Array helper (Anna Henningsen) [#29514](https://github.com/nodejs/node/pull/29514)
* \[[`560511924f`](https://github.com/nodejs/node/commit/560511924f)] - **test**: fix race condition in test-worker-process-cwd.js (Ruben Bridgewater) [#28609](https://github.com/nodejs/node/pull/28609)
* \[[`78ee065a11`](https://github.com/nodejs/node/commit/78ee065a11)] - **test**: fix flaky test-inspector-connect-main-thread (Anna Henningsen) [#29588](https://github.com/nodejs/node/pull/29588)
* \[[`87fd55c387`](https://github.com/nodejs/node/commit/87fd55c387)] - **test**: unmark test-worker-prof as flaky (Anna Henningsen) [#29511](https://github.com/nodejs/node/pull/29511)
* \[[`79a277ed10`](https://github.com/nodejs/node/commit/79a277ed10)] - **test**: improve test-worker-message-port-message-before-close (Anna Henningsen) [#29483](https://github.com/nodejs/node/pull/29483)
* \[[`909c669c04`](https://github.com/nodejs/node/commit/909c669c04)] - **test**: disable core dumps before running crash test (Ben Noordhuis) [#29478](https://github.com/nodejs/node/pull/29478)
* \[[`561d504d71`](https://github.com/nodejs/node/commit/561d504d71)] - **test**: permit test-signalwrap to work without test runner (Rich Trott) [#28306](https://github.com/nodejs/node/pull/28306)
* \[[`75c559dc3a`](https://github.com/nodejs/node/commit/75c559dc3a)] - **test**: remove flaky status for test-statwatcher (Rich Trott) [#29392](https://github.com/nodejs/node/pull/29392)
* \[[`f056d55346`](https://github.com/nodejs/node/commit/f056d55346)] - **(SEMVER-MINOR)** **test**: update postmortem metadata test for V8 7.7 (cjihrig) [#28918](https://github.com/nodejs/node/pull/28918)
* \[[`b43d2dd852`](https://github.com/nodejs/node/commit/b43d2dd852)] - **timers**: set \_destroyed even if there are no destroy-hooks (Jeremiah Senkpiel) [#29595](https://github.com/nodejs/node/pull/29595)
* \[[`6272f82c07`](https://github.com/nodejs/node/commit/6272f82c07)] - **(SEMVER-MINOR)** **tls**: add option to override signature algorithms (Anton Gerasimov) [#29598](https://github.com/nodejs/node/pull/29598)
* \[[`b7488c2a5c`](https://github.com/nodejs/node/commit/b7488c2a5c)] - **tools**: cleanup getnodeversion.py for readability (Christian Clauss) [#29648](https://github.com/nodejs/node/pull/29648)
* \[[`7bc2f06e0b`](https://github.com/nodejs/node/commit/7bc2f06e0b)] - **tools**: update ESLint to 6.4.0 (zhangyongsheng) [#29553](https://github.com/nodejs/node/pull/29553)
* \[[`0db7ebe073`](https://github.com/nodejs/node/commit/0db7ebe073)] - **tools**: fix iculslocs to support ICU 65.1 (Steven R. Loomis) [#29523](https://github.com/nodejs/node/pull/29523)
* \[[`bc7cc348cc`](https://github.com/nodejs/node/commit/bc7cc348cc)] - **tools**: python3 compat for inspector code generator (Ben Noordhuis) [#29340](https://github.com/nodejs/node/pull/29340)
* \[[`9de417ad59`](https://github.com/nodejs/node/commit/9de417ad59)] - **tools**: delete v8\_external\_snapshot.gypi (Ujjwal Sharma) [#29369](https://github.com/nodejs/node/pull/29369)
* \[[`2f81d59e75`](https://github.com/nodejs/node/commit/2f81d59e75)] - **tools**: fix GYP ninja generator for Python 3 (Michaël Zasso) [#29416](https://github.com/nodejs/node/pull/29416)
* \[[`027dcff207`](https://github.com/nodejs/node/commit/027dcff207)] - **(SEMVER-MINOR)** **tools**: sync gypfiles with V8 7.7 (Michaël Zasso) [#28918](https://github.com/nodejs/node/pull/28918)
* \[[`bbf209b5df`](https://github.com/nodejs/node/commit/bbf209b5df)] - **tty**: add color support for mosh (Aditya) [#27843](https://github.com/nodejs/node/pull/27843)
* \[[`ced89ad75d`](https://github.com/nodejs/node/commit/ced89ad75d)] - **util**: include reference anchor for circular structures (Ruben Bridgewater) [#27685](https://github.com/nodejs/node/pull/27685)
* \[[`772a5e0658`](https://github.com/nodejs/node/commit/772a5e0658)] - **(SEMVER-MINOR)** **util**: add encodeInto to TextEncoder (Anna Henningsen) [#29524](https://github.com/nodejs/node/pull/29524)
* \[[`97d8b33ffc`](https://github.com/nodejs/node/commit/97d8b33ffc)] - **(SEMVER-MINOR)** **worker**: mark as stable (Anna Henningsen) [#29512](https://github.com/nodejs/node/pull/29512)
* \[[`fa77dc5f3b`](https://github.com/nodejs/node/commit/fa77dc5f3b)] - **worker**: make terminate() resolve for unref’ed Workers (Anna Henningsen) [#29484](https://github.com/nodejs/node/pull/29484)
* \[[`53f23715df`](https://github.com/nodejs/node/commit/53f23715df)] - **worker**: prevent event loop starvation through MessagePorts (Anna Henningsen) [#29315](https://github.com/nodejs/node/pull/29315)
* \[[`d2b0568890`](https://github.com/nodejs/node/commit/d2b0568890)] - **worker**: make transfer list behave like web MessagePort (Anna Henningsen) [#29319](https://github.com/nodejs/node/pull/29319)

<a id="12.10.0"></a>

## 2019-09-03, Version 12.10.0 (Current), @BridgeAr

### Notable changes

* **deps**:
  * Update npm to 6.10.3 (isaacs) [#29023](https://github.com/nodejs/node/pull/29023)
* **fs**:
  * Add recursive option to rmdir() (cjihrig) [#29168](https://github.com/nodejs/node/pull/29168)
  * Allow passing true to emitClose option (Giorgos Ntemiris) [#29212](https://github.com/nodejs/node/pull/29212)
  * Add \*timeNs properties to BigInt Stats objects (Joyee Cheung) [#21387](https://github.com/nodejs/node/pull/21387)
* **net**:
  * Allow reading data into a static buffer (Brian White) [#25436](https://github.com/nodejs/node/pull/25436)

### Commits

* \[[`293c9f0d75`](https://github.com/nodejs/node/commit/293c9f0d75)] - **bootstrap**: run preload prior to frozen-intrinsics (Bradley Farias) [#28940](https://github.com/nodejs/node/pull/28940)
* \[[`71aaf590c1`](https://github.com/nodejs/node/commit/71aaf590c1)] - **buffer**: correct indexOf() error message (Brian White) [#29217](https://github.com/nodejs/node/pull/29217)
* \[[`c900762fe4`](https://github.com/nodejs/node/commit/c900762fe4)] - **buffer**: consolidate encoding parsing (Brian White) [#29217](https://github.com/nodejs/node/pull/29217)
* \[[`054407511e`](https://github.com/nodejs/node/commit/054407511e)] - **buffer**: correct concat() error message (Brian White) [#29198](https://github.com/nodejs/node/pull/29198)
* \[[`35bca312ed`](https://github.com/nodejs/node/commit/35bca312ed)] - **buffer**: improve equals() performance (Brian White) [#29199](https://github.com/nodejs/node/pull/29199)
* \[[`449f1fd578`](https://github.com/nodejs/node/commit/449f1fd578)] - _**Revert**_ "**build**: add full Python 3 tests to Travis CI" (Ben Noordhuis) [#29406](https://github.com/nodejs/node/pull/29406)
* \[[`256da1fdb3`](https://github.com/nodejs/node/commit/256da1fdb3)] - **build**: add full Python 3 tests to Travis CI (cclauss) [#29360](https://github.com/nodejs/node/pull/29360)
* \[[`0c4df35db0`](https://github.com/nodejs/node/commit/0c4df35db0)] - **build**: hard code doctool in test-doc target (Daniel Bevenius) [#29375](https://github.com/nodejs/node/pull/29375)
* \[[`d6b6a0578b`](https://github.com/nodejs/node/commit/d6b6a0578b)] - **build**: integrate DragonFlyBSD into gyp build (David Carlier) [#29313](https://github.com/nodejs/node/pull/29313)
* \[[`6a914ed36e`](https://github.com/nodejs/node/commit/6a914ed36e)] - **build**: make --without-snapshot imply --without-node-snapshot (Joyee Cheung) [#29294](https://github.com/nodejs/node/pull/29294)
* \[[`def5c3e5d8`](https://github.com/nodejs/node/commit/def5c3e5d8)] - **build**: test Python 3.6 and 3.7 on Travis CI (cclauss) [#29291](https://github.com/nodejs/node/pull/29291)
* \[[`feafc019b1`](https://github.com/nodejs/node/commit/feafc019b1)] - **build**: move tooltest to before jstest target (Daniel Bevenius) [#29220](https://github.com/nodejs/node/pull/29220)
* \[[`aeafb91e2c`](https://github.com/nodejs/node/commit/aeafb91e2c)] - **build**: add Python 3 tests to Travis CI (cclauss) [#29196](https://github.com/nodejs/node/pull/29196)
* \[[`bb6e3b5404`](https://github.com/nodejs/node/commit/bb6e3b5404)] - **build,win**: accept Python 3 if 2 is not available (João Reis) [#29236](https://github.com/nodejs/node/pull/29236)
* \[[`dce5649d9c`](https://github.com/nodejs/node/commit/dce5649d9c)] - **build,win**: find Python in paths with spaces (João Reis) [#29236](https://github.com/nodejs/node/pull/29236)
* \[[`2489682eb5`](https://github.com/nodejs/node/commit/2489682eb5)] - **console**: use getStringWidth() for character width calculation (Anna Henningsen) [#29300](https://github.com/nodejs/node/pull/29300)
* \[[`5c3e49d84e`](https://github.com/nodejs/node/commit/5c3e49d84e)] - **crypto**: don't expose openssl internals (Shelley Vohr) [#29325](https://github.com/nodejs/node/pull/29325)
* \[[`e0537e6978`](https://github.com/nodejs/node/commit/e0537e6978)] - **crypto**: simplify DSA validation in FIPS mode (Tobias Nießen) [#29195](https://github.com/nodejs/node/pull/29195)
* \[[`28ffc9f599`](https://github.com/nodejs/node/commit/28ffc9f599)] - **deps**: V8: cherry-pick 597f885 (Benjamin Coe) [#29367](https://github.com/nodejs/node/pull/29367)
* \[[`219c19530e`](https://github.com/nodejs/node/commit/219c19530e)] - **(SEMVER-MINOR)** **deps**: update npm to 6.10.3 (isaacs) [#29023](https://github.com/nodejs/node/pull/29023)
* \[[`4a7c4b7366`](https://github.com/nodejs/node/commit/4a7c4b7366)] - **doc**: escape elements swallowed as HTML in markdown (Nick Schonning) [#29374](https://github.com/nodejs/node/pull/29374)
* \[[`5a16449edf`](https://github.com/nodejs/node/commit/5a16449edf)] - **doc**: add extends for derived classes (Kamat, Trivikram) [#29290](https://github.com/nodejs/node/pull/29290)
* \[[`3fc29b8f9a`](https://github.com/nodejs/node/commit/3fc29b8f9a)] - **doc**: add blanks around code fences (Nick Schonning) [#29366](https://github.com/nodejs/node/pull/29366)
* \[[`187d08be65`](https://github.com/nodejs/node/commit/187d08be65)] - **doc**: format http2 anchor link and reference (Nick Schonning) [#29362](https://github.com/nodejs/node/pull/29362)
* \[[`6734782f25`](https://github.com/nodejs/node/commit/6734782f25)] - **doc**: remove multiple consecutive blank lines (Nick Schonning) [#29352](https://github.com/nodejs/node/pull/29352)
* \[[`a94afedc9b`](https://github.com/nodejs/node/commit/a94afedc9b)] - **doc**: add devnexen to collaborators (David Carlier) [#29370](https://github.com/nodejs/node/pull/29370)
* \[[`43797d9427`](https://github.com/nodejs/node/commit/43797d9427)] - **doc**: inconsistent indentation for list items (Nick Schonning) [#29330](https://github.com/nodejs/node/pull/29330)
* \[[`bb72217faf`](https://github.com/nodejs/node/commit/bb72217faf)] - **doc**: heading levels should only increment by one (Nick Schonning) [#29331](https://github.com/nodejs/node/pull/29331)
* \[[`ef76c7d997`](https://github.com/nodejs/node/commit/ef76c7d997)] - **doc**: add dco to github pr template (Myles Borins) [#24023](https://github.com/nodejs/node/pull/24023)
* \[[`8599052283`](https://github.com/nodejs/node/commit/8599052283)] - **doc**: add https.Server extends tls.Server (Trivikram Kamat) [#29256](https://github.com/nodejs/node/pull/29256)
* \[[`2fafd635d7`](https://github.com/nodejs/node/commit/2fafd635d7)] - **doc**: fix nits in esm.md (Vse Mozhet Byt) [#29242](https://github.com/nodejs/node/pull/29242)
* \[[`6a4f156ba4`](https://github.com/nodejs/node/commit/6a4f156ba4)] - **doc**: add missing extends Http2Session (Trivikram Kamat) [#29252](https://github.com/nodejs/node/pull/29252)
* \[[`1d649e3444`](https://github.com/nodejs/node/commit/1d649e3444)] - **doc**: indicate that Http2ServerRequest extends Readable (Trivikram Kamat) [#29253](https://github.com/nodejs/node/pull/29253)
* \[[`b2f169e628`](https://github.com/nodejs/node/commit/b2f169e628)] - **doc**: indicate that Http2ServerResponse extends Stream (Trivikram Kamat) [#29254](https://github.com/nodejs/node/pull/29254)
* \[[`65de900052`](https://github.com/nodejs/node/commit/65de900052)] - **(SEMVER-MINOR)** **doc**: add emitClose option for fs streams (Rich Trott) [#29212](https://github.com/nodejs/node/pull/29212)
* \[[`ae810cc8d5`](https://github.com/nodejs/node/commit/ae810cc8d5)] - **doc,crypto**: add extends for derived classes (Kamat, Trivikram) [#29302](https://github.com/nodejs/node/pull/29302)
* \[[`a2c704773a`](https://github.com/nodejs/node/commit/a2c704773a)] - **doc,errors**: add extends to derived classes (Kamat, Trivikram) [#29303](https://github.com/nodejs/node/pull/29303)
* \[[`395245f1eb`](https://github.com/nodejs/node/commit/395245f1eb)] - **doc,fs**: add extends for derived classes (Kamat, Trivikram) [#29304](https://github.com/nodejs/node/pull/29304)
* \[[`8a93b63a6b`](https://github.com/nodejs/node/commit/8a93b63a6b)] - **doc,http**: add extends for derived classes (Trivikram Kamat) [#29255](https://github.com/nodejs/node/pull/29255)
* \[[`ba29be60ae`](https://github.com/nodejs/node/commit/ba29be60ae)] - **doc,tls**: add extends for derived classes (Trivikram Kamat) [#29257](https://github.com/nodejs/node/pull/29257)
* \[[`30b80e5d7c`](https://github.com/nodejs/node/commit/30b80e5d7c)] - **errors**: provide defaults for unmapped uv errors (cjihrig) [#29288](https://github.com/nodejs/node/pull/29288)
* \[[`a7c8322a54`](https://github.com/nodejs/node/commit/a7c8322a54)] - **esm**: support loading data URLs (Bradley Farias) [#28614](https://github.com/nodejs/node/pull/28614)
* \[[`3bc16f917d`](https://github.com/nodejs/node/commit/3bc16f917d)] - **events**: improve once() performance (Brian White) [#29307](https://github.com/nodejs/node/pull/29307)
* \[[`ed2293e3d7`](https://github.com/nodejs/node/commit/ed2293e3d7)] - **(SEMVER-MINOR)** **fs**: add recursive option to rmdir() (cjihrig) [#29168](https://github.com/nodejs/node/pull/29168)
* \[[`8f47ff16d4`](https://github.com/nodejs/node/commit/8f47ff16d4)] - **(SEMVER-MINOR)** **fs**: allow passing true to emitClose option (Giorgos Ntemiris) [#29212](https://github.com/nodejs/node/pull/29212)
* \[[`6ff803d97c`](https://github.com/nodejs/node/commit/6ff803d97c)] - **fs**: fix (temporary) for esm package (Robert Nagy) [#28957](https://github.com/nodejs/node/pull/28957)
* \[[`e6353bda1a`](https://github.com/nodejs/node/commit/e6353bda1a)] - **fs**: document the Date conversion in Stats objects (Joyee Cheung) [#28224](https://github.com/nodejs/node/pull/28224)
* \[[`365e062e14`](https://github.com/nodejs/node/commit/365e062e14)] - **(SEMVER-MINOR)** **fs**: add \*timeNs properties to BigInt Stats objects (Joyee Cheung) [#21387](https://github.com/nodejs/node/pull/21387)
* \[[`12cbb3f12f`](https://github.com/nodejs/node/commit/12cbb3f12f)] - **gyp**: remove semicolons (Python != JavaScript) (MattIPv4) [#29228](https://github.com/nodejs/node/pull/29228)
* \[[`10bae2ec91`](https://github.com/nodejs/node/commit/10bae2ec91)] - **gyp**: futurize imput.py to prepare for Python 3 (cclauss) [#29140](https://github.com/nodejs/node/pull/29140)
* \[[`e5a9a8522d`](https://github.com/nodejs/node/commit/e5a9a8522d)] - **http**: simplify timeout handling (Robert Nagy) [#29200](https://github.com/nodejs/node/pull/29200)
* \[[`87b8f02daa`](https://github.com/nodejs/node/commit/87b8f02daa)] - **lib**: add ASCII fast path to getStringWidth() (Anna Henningsen) [#29301](https://github.com/nodejs/node/pull/29301)
* \[[`6e585fb063`](https://github.com/nodejs/node/commit/6e585fb063)] - **lib**: consolidate lazyErrmapGet() (cjihrig) [#29285](https://github.com/nodejs/node/pull/29285)
* \[[`eb2d96fecf`](https://github.com/nodejs/node/commit/eb2d96fecf)] - **module**: avoid passing unnecessary loop reference (Saúl Ibarra Corretgé) [#29275](https://github.com/nodejs/node/pull/29275)
* \[[`dfc0ef5d88`](https://github.com/nodejs/node/commit/dfc0ef5d88)] - **(SEMVER-MINOR)** **net**: allow reading data into a static buffer (Brian White) [#25436](https://github.com/nodejs/node/pull/25436)
* \[[`f4f88270e7`](https://github.com/nodejs/node/commit/f4f88270e7)] - **process**: improve nextTick performance (Brian White) [#25461](https://github.com/nodejs/node/pull/25461)
* \[[`0e1ccca81d`](https://github.com/nodejs/node/commit/0e1ccca81d)] - **querystring**: improve performance (Brian White) [#29306](https://github.com/nodejs/node/pull/29306)
* \[[`f8f3af099a`](https://github.com/nodejs/node/commit/f8f3af099a)] - **src**: do not crash when accessing empty WeakRefs (Anna Henningsen) [#29289](https://github.com/nodejs/node/pull/29289)
* \[[`b964bdd162`](https://github.com/nodejs/node/commit/b964bdd162)] - **src**: turn `GET_OFFSET()` into an inline function (Anna Henningsen) [#29357](https://github.com/nodejs/node/pull/29357)
* \[[`2666e006e1`](https://github.com/nodejs/node/commit/2666e006e1)] - **src**: inline `SLICE_START_END()` in node\_buffer.cc (Anna Henningsen) [#29357](https://github.com/nodejs/node/pull/29357)
* \[[`8c6896e5d3`](https://github.com/nodejs/node/commit/8c6896e5d3)] - **src**: allow --interpreted-frames-native-stack in NODE\_OPTIONS (Matheus Marchini) [#27744](https://github.com/nodejs/node/pull/27744)
* \[[`db6e4ce239`](https://github.com/nodejs/node/commit/db6e4ce239)] - **src**: expose MaybeInitializeContext to allow existing contexts (Samuel Attard) [#28544](https://github.com/nodejs/node/pull/28544)
* \[[`4d4583e0a2`](https://github.com/nodejs/node/commit/4d4583e0a2)] - **src**: add large page support for macOS (David Carlier) [#28977](https://github.com/nodejs/node/pull/28977)
* \[[`7809adfb1f`](https://github.com/nodejs/node/commit/7809adfb1f)] - **stream**: don't deadlock on aborted stream (Robert Nagy) [#29376](https://github.com/nodejs/node/pull/29376)
* \[[`2efd72f28d`](https://github.com/nodejs/node/commit/2efd72f28d)] - **stream**: improve read() performance (Brian White) [#29337](https://github.com/nodejs/node/pull/29337)
* \[[`e939a8747f`](https://github.com/nodejs/node/commit/e939a8747f)] - **stream**: async iterator destroy compat (Robert Nagy) [#29176](https://github.com/nodejs/node/pull/29176)
* \[[`b36a6e9ed5`](https://github.com/nodejs/node/commit/b36a6e9ed5)] - **stream**: do not emit drain if stream ended (Robert Nagy) [#29086](https://github.com/nodejs/node/pull/29086)
* \[[`0ccf90b415`](https://github.com/nodejs/node/commit/0ccf90b415)] - **test**: remove Windows skipping of http keepalive request GC test (Rich Trott) [#29354](https://github.com/nodejs/node/pull/29354)
* \[[`83fb133267`](https://github.com/nodejs/node/commit/83fb133267)] - **test**: fix test-benchmark-net (Rich Trott) [#29359](https://github.com/nodejs/node/pull/29359)
* \[[`bd1e8eacf3`](https://github.com/nodejs/node/commit/bd1e8eacf3)] - **test**: fix flaky test-http-server-keepalive-req-gc (Rich Trott) [#29347](https://github.com/nodejs/node/pull/29347)
* \[[`9a150027da`](https://github.com/nodejs/node/commit/9a150027da)] - **test**: use print() function in both Python 2 and 3 (Christian Clauss) [#29298](https://github.com/nodejs/node/pull/29298)
* \[[`1f88ca3424`](https://github.com/nodejs/node/commit/1f88ca3424)] - **(SEMVER-MINOR)** **test**: add `emitClose: true` tests for fs streams (Rich Trott) [#29212](https://github.com/nodejs/node/pull/29212)
* \[[`cd70fd2bc0`](https://github.com/nodejs/node/commit/cd70fd2bc0)] - **tools**: update ESLint to 6.3.0 (cjihrig) [#29382](https://github.com/nodejs/node/pull/29382)
* \[[`350975e312`](https://github.com/nodejs/node/commit/350975e312)] - **tools**: use 'from io import StringIO' in ninja.py (cclauss) [#29371](https://github.com/nodejs/node/pull/29371)
* \[[`3f68be1098`](https://github.com/nodejs/node/commit/3f68be1098)] - **tools**: fix mksnapshot blob wrong freeing operator (David Carlier) [#29384](https://github.com/nodejs/node/pull/29384)
* \[[`3802da790b`](https://github.com/nodejs/node/commit/3802da790b)] - **tools**: update ESLint to 6.2.2 (cjihrig) [#29320](https://github.com/nodejs/node/pull/29320)
* \[[`2df84752c6`](https://github.com/nodejs/node/commit/2df84752c6)] - **tools**: update babel-eslint to 10.0.3 (cjihrig) [#29320](https://github.com/nodejs/node/pull/29320)
* \[[`783c8eeb0b`](https://github.com/nodejs/node/commit/783c8eeb0b)] - **tools**: fix Python 3 issues in inspector\_protocol (cclauss) [#29296](https://github.com/nodejs/node/pull/29296)
* \[[`925141f946`](https://github.com/nodejs/node/commit/925141f946)] - **tools**: fix mixup with bytes.decode() and str.encode() (Christian Clauss) [#29208](https://github.com/nodejs/node/pull/29208)
* \[[`a123a20134`](https://github.com/nodejs/node/commit/a123a20134)] - **tools**: fix Python 3 issues in tools/icu/icutrim.py (cclauss) [#29213](https://github.com/nodejs/node/pull/29213)
* \[[`eceebd3ef1`](https://github.com/nodejs/node/commit/eceebd3ef1)] - **tools**: fix Python 3 issues in gyp/generator/make.py (cclauss) [#29214](https://github.com/nodejs/node/pull/29214)
* \[[`5abbd51c60`](https://github.com/nodejs/node/commit/5abbd51c60)] - **util**: do not throw when inspecting detached ArrayBuffer (Anna Henningsen) [#29318](https://github.com/nodejs/node/pull/29318)

<a id="12.9.1"></a>

## 2019-08-26, Version 12.9.1 (Current), @targos

### Notable changes

This release fixes two regressions in the **http** module:

* Fixes an event listener leak in the HTTP client. This resulted in lots of
  warnings during npm/yarn installs (Robert Nagy) [#29245](https://github.com/nodejs/node/pull/29245).
* Fixes a regression preventing the `'end'` event from being emitted for
  keepalive requests in case the full body was not parsed (Matteo Collina) [#29263](https://github.com/nodejs/node/pull/29263).

### Commits

* \[[`3cc8fca299`](https://github.com/nodejs/node/commit/3cc8fca299)] - **crypto**: handle i2d\_SSL\_SESSION() error return (Ben Noordhuis) [#29225](https://github.com/nodejs/node/pull/29225)
* \[[`ae0a0e97ba`](https://github.com/nodejs/node/commit/ae0a0e97ba)] - **http**: reset parser.incoming when server request is finished (Anna Henningsen) [#29297](https://github.com/nodejs/node/pull/29297)
* \[[`dedbd119c5`](https://github.com/nodejs/node/commit/dedbd119c5)] - **http**: fix event listener leak (Robert Nagy) [#29245](https://github.com/nodejs/node/pull/29245)
* \[[`f8f8754d43`](https://github.com/nodejs/node/commit/f8f8754d43)] - _**Revert**_ "**http**: reset parser.incoming when server response is finished" (Matteo Collina) [#29263](https://github.com/nodejs/node/pull/29263)
* \[[`a6abfcb423`](https://github.com/nodejs/node/commit/a6abfcb423)] - **src**: remove unused using declarations (Daniel Bevenius) [#29222](https://github.com/nodejs/node/pull/29222)
* \[[`ff6330a6ac`](https://github.com/nodejs/node/commit/ff6330a6ac)] - **test**: fix 'timeout' typos (cjihrig) [#29234](https://github.com/nodejs/node/pull/29234)
* \[[`3c7a1a9090`](https://github.com/nodejs/node/commit/3c7a1a9090)] - **test, http**: add regression test for keepalive 'end' event (Matteo Collina) [#29263](https://github.com/nodejs/node/pull/29263)

<a id="12.9.0"></a>

## 2019-08-20, Version 12.9.0 (Current), @targos

### Notable changes

* **crypto**:
  * Added an oaepHash option to asymmetric encryption which allows users to specify a hash function when using OAEP padding (Tobias Nießen) [#28335](https://github.com/nodejs/node/pull/28335).
* **deps**:
  * Updated V8 to 7.6.303.29 (Michaël Zasso) [#28955](https://github.com/nodejs/node/pull/28955).
    * Improves the performance of various APIs such as `JSON.parse` and methods
      called on frozen arrays.
    * Adds the [`Promise.allSettled`](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise/allSettled) method.
    * Improves support of `BigInt` in `Intl` methods.
    * For more information: <https://v8.dev/blog/v8-release-76>
  * Updated libuv to 1.31.0 (cjihrig) [#29070](https://github.com/nodejs/node/pull/29070).
    * `UV_FS_O_FILEMAP` has been added for faster access to memory mapped files on Windows.
    * `uv_fs_mkdir()` now returns `UV_EINVAL` for invalid filenames on Windows. It previously returned `UV_ENOENT`.
    * The `uv_fs_statfs()` API has been added.
    * The `uv_os_environ()` and `uv_os_free_environ()` APIs have been added.
* **fs**:
  * Added `fs.writev`, `fs.writevSync` and `filehandle.writev` (promise version) methods. They allow to write an array of `ArrayBufferView`s to a file descriptor (Anas Aboureada) [#25925](https://github.com/nodejs/node/pull/25925), (cjihrig) [#29186](https://github.com/nodejs/node/pull/29186).
* **http**:
  * Added three properties to `OutgoingMessage.prototype`: `writableObjectMode`, `writableLength` and `writableHighWaterMark` [#29018](https://github.com/nodejs/node/pull/29018).
* **stream**:
  * Added an new property `readableEnded` to readable streams. Its value is set to `true` when the `'end'` event is emitted. (Robert Nagy) [#28814](https://github.com/nodejs/node/pull/28814).
  * Added an new property `writableEnded` to writable streams. Its value is set to `true` after `writable.end()` has been called. (Robert Nagy) [#28934](https://github.com/nodejs/node/pull/28934).

### Commits

* \[[`5008b46159`](https://github.com/nodejs/node/commit/5008b46159)] - **benchmark**: allow easy passing of process flags (Brian White) [#28986](https://github.com/nodejs/node/pull/28986)
* \[[`9057814206`](https://github.com/nodejs/node/commit/9057814206)] - **buffer**: improve copy() performance (Brian White) [#29066](https://github.com/nodejs/node/pull/29066)
* \[[`c7a4525bbe`](https://github.com/nodejs/node/commit/c7a4525bbe)] - **buffer**: improve ERR\_BUFFER\_OUT\_OF\_BOUNDS default (cjihrig) [#29098](https://github.com/nodejs/node/pull/29098)
* \[[`f42eb01d1d`](https://github.com/nodejs/node/commit/f42eb01d1d)] - **build**: enable linux large pages LLVM lld linkage support (David Carlier) [#28938](https://github.com/nodejs/node/pull/28938)
* \[[`5c5ef4e858`](https://github.com/nodejs/node/commit/5c5ef4e858)] - **build**: remove unused option (Rich Trott) [#29173](https://github.com/nodejs/node/pull/29173)
* \[[`fbe25c7fb7`](https://github.com/nodejs/node/commit/fbe25c7fb7)] - **build**: remove unnecessary Python semicolon (MattIPv4) [#29170](https://github.com/nodejs/node/pull/29170)
* \[[`e51f924964`](https://github.com/nodejs/node/commit/e51f924964)] - **build**: add a testclean target (Daniel Bevenius) [#29094](https://github.com/nodejs/node/pull/29094)
* \[[`0a63e2d9ff`](https://github.com/nodejs/node/commit/0a63e2d9ff)] - **build**: support py3 for configure.py (cclauss) [#29106](https://github.com/nodejs/node/pull/29106)
* \[[`b04f9e1f57`](https://github.com/nodejs/node/commit/b04f9e1f57)] - **build**: reset embedder string to "-node.0" (Michaël Zasso) [#28955](https://github.com/nodejs/node/pull/28955)
* \[[`b085b94fce`](https://github.com/nodejs/node/commit/b085b94fce)] - **console**: minor timeLogImpl() refactor (cjihrig) [#29100](https://github.com/nodejs/node/pull/29100)
* \[[`54197eac4d`](https://github.com/nodejs/node/commit/54197eac4d)] - **(SEMVER-MINOR)** **crypto**: extend RSA-OAEP support with oaepHash (Tobias Nießen) [#28335](https://github.com/nodejs/node/pull/28335)
* \[[`a17d398989`](https://github.com/nodejs/node/commit/a17d398989)] - **deps**: V8: cherry-pick e3d7f8a (cclauss) [#29105](https://github.com/nodejs/node/pull/29105)
* \[[`2c91c65961`](https://github.com/nodejs/node/commit/2c91c65961)] - **deps**: upgrade to libuv 1.31.0 (cjihrig) [#29070](https://github.com/nodejs/node/pull/29070)
* \[[`53c7fac310`](https://github.com/nodejs/node/commit/53c7fac310)] - **deps**: patch V8 to be API/ABI compatible with 7.4 (from 7.6) (Michaël Zasso) [#28955](https://github.com/nodejs/node/pull/28955)
* \[[`7b8eb83895`](https://github.com/nodejs/node/commit/7b8eb83895)] - **(SEMVER-MINOR)** **deps**: patch V8 to be API/ABI compatible with 7.4 (from 7.5) (Michaël Zasso) [#28005](https://github.com/nodejs/node/pull/28005)
* \[[`7d411f47b2`](https://github.com/nodejs/node/commit/7d411f47b2)] - **deps**: V8: backport b33af60 (Gus Caplan) [#28671](https://github.com/nodejs/node/pull/28671)
* \[[`492b7cb8c3`](https://github.com/nodejs/node/commit/492b7cb8c3)] - **(SEMVER-MINOR)** **deps**: V8: cherry-pick d2ccc59 (Michaël Zasso) [#28016](https://github.com/nodejs/node/pull/28016)
* \[[`945955ff86`](https://github.com/nodejs/node/commit/945955ff86)] - **deps**: cherry-pick 13a04aba from V8 upstream (Jon Kunkee) [#28602](https://github.com/nodejs/node/pull/28602)
* \[[`9b3c115efb`](https://github.com/nodejs/node/commit/9b3c115efb)] - **(SEMVER-MINOR)** **deps**: V8: cherry-pick 3b8c624 (Michaël Zasso) [#28016](https://github.com/nodejs/node/pull/28016)
* \[[`533b2d4a08`](https://github.com/nodejs/node/commit/533b2d4a08)] - **(SEMVER-MINOR)** **deps**: V8: fix linking issue for MSVS (Refael Ackermann) [#28016](https://github.com/nodejs/node/pull/28016)
* \[[`c9f8d28f71`](https://github.com/nodejs/node/commit/c9f8d28f71)] - **(SEMVER-MINOR)** **deps**: V8: fix BUILDING\_V8\_SHARED issues (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`f24caeff2f`](https://github.com/nodejs/node/commit/f24caeff2f)] - **(SEMVER-MINOR)** **deps**: V8: add workaround for MSVC optimizer bug (Refael Ackermann) [#28016](https://github.com/nodejs/node/pull/28016)
* \[[`94c8d068f8`](https://github.com/nodejs/node/commit/94c8d068f8)] - **(SEMVER-MINOR)** **deps**: V8: use ATOMIC\_VAR\_INIT instead of std::atomic\_init (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`d940403c20`](https://github.com/nodejs/node/commit/d940403c20)] - **(SEMVER-MINOR)** **deps**: V8: forward declaration of `Rtl*FunctionTable` (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`ac0c075cad`](https://github.com/nodejs/node/commit/ac0c075cad)] - **(SEMVER-MINOR)** **deps**: V8: patch register-arm64.h (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`eefbc0773f`](https://github.com/nodejs/node/commit/eefbc0773f)] - **(SEMVER-MINOR)** **deps**: V8: update postmortem metadata generation script (cjihrig) [#28016](https://github.com/nodejs/node/pull/28016)
* \[[`33f3e383da`](https://github.com/nodejs/node/commit/33f3e383da)] - **(SEMVER-MINOR)** **deps**: V8: silence irrelevant warning (Michaël Zasso) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`434c127651`](https://github.com/nodejs/node/commit/434c127651)] - **(SEMVER-MINOR)** **deps**: V8: un-cherry-pick bd019bd (Refael Ackermann) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`040e7dabe4`](https://github.com/nodejs/node/commit/040e7dabe4)] - **(SEMVER-MINOR)** **deps**: V8: fix filename manipulation for Windows (Refael Ackermann) [#28016](https://github.com/nodejs/node/pull/28016)
* \[[`cfe2484e04`](https://github.com/nodejs/node/commit/cfe2484e04)] - **deps**: update V8 to 7.6.303.29 (Michaël Zasso) [#28955](https://github.com/nodejs/node/pull/28955)
* \[[`6f7b561295`](https://github.com/nodejs/node/commit/6f7b561295)] - **dns**: update lookupService() first arg name (cjihrig) [#29040](https://github.com/nodejs/node/pull/29040)
* \[[`ad28f555a1`](https://github.com/nodejs/node/commit/ad28f555a1)] - **doc**: improve example single-test command (David Guttman) [#29171](https://github.com/nodejs/node/pull/29171)
* \[[`edbe38d52d`](https://github.com/nodejs/node/commit/edbe38d52d)] - **doc**: mention N-API as recommended approach (Michael Dawson) [#28922](https://github.com/nodejs/node/pull/28922)
* \[[`ebfe6367f0`](https://github.com/nodejs/node/commit/ebfe6367f0)] - **doc**: fix introduced\_in note in querystring.md (Ben Noordhuis) [#29014](https://github.com/nodejs/node/pull/29014)
* \[[`9ead4eceeb`](https://github.com/nodejs/node/commit/9ead4eceeb)] - **doc**: note that stream error can close stream (Robert Nagy) [#29082](https://github.com/nodejs/node/pull/29082)
* \[[`5ea9237d2b`](https://github.com/nodejs/node/commit/5ea9237d2b)] - **doc**: clarify async iterator leak (Robert Nagy) [#28997](https://github.com/nodejs/node/pull/28997)
* \[[`1b6d7c0d00`](https://github.com/nodejs/node/commit/1b6d7c0d00)] - **doc**: fix nits in child\_process.md (Vse Mozhet Byt) [#29024](https://github.com/nodejs/node/pull/29024)
* \[[`375d1ee58b`](https://github.com/nodejs/node/commit/375d1ee58b)] - **doc**: improve UV\_THREADPOOL\_SIZE description (Tobias Nießen) [#29033](https://github.com/nodejs/node/pull/29033)
* \[[`7b7b8f21cb`](https://github.com/nodejs/node/commit/7b7b8f21cb)] - **doc**: documented default statusCode (Natalie Fearnley) [#28982](https://github.com/nodejs/node/pull/28982)
* \[[`17d9495afa`](https://github.com/nodejs/node/commit/17d9495afa)] - **doc**: make unshift doc compliant with push doc (EduardoRFS) [#28953](https://github.com/nodejs/node/pull/28953)
* \[[`3bfca0b7e4`](https://github.com/nodejs/node/commit/3bfca0b7e4)] - **doc, lib, src, test, tools**: fix assorted typos (XhmikosR) [#29075](https://github.com/nodejs/node/pull/29075)
* \[[`b7696b41e5`](https://github.com/nodejs/node/commit/b7696b41e5)] - **events**: give subclass name in unhandled 'error' message (Anna Henningsen) [#28952](https://github.com/nodejs/node/pull/28952)
* \[[`d79d142e0c`](https://github.com/nodejs/node/commit/d79d142e0c)] - **fs**: use fs.writev() internally (Robert Nagy) [#29189](https://github.com/nodejs/node/pull/29189)
* \[[`82eeadb216`](https://github.com/nodejs/node/commit/82eeadb216)] - **fs**: use consistent buffer array validation (cjihrig) [#29186](https://github.com/nodejs/node/pull/29186)
* \[[`81f3eb5bb1`](https://github.com/nodejs/node/commit/81f3eb5bb1)] - **fs**: add writev() promises version (cjihrig) [#29186](https://github.com/nodejs/node/pull/29186)
* \[[`435683610b`](https://github.com/nodejs/node/commit/435683610b)] - **fs**: validate writev fds consistently (cjihrig) [#29185](https://github.com/nodejs/node/pull/29185)
* \[[`bb19d8212a`](https://github.com/nodejs/node/commit/bb19d8212a)] - **(SEMVER-MINOR)** **fs**: add fs.writev() which exposes syscall writev() (Anas Aboureada) [#25925](https://github.com/nodejs/node/pull/25925)
* \[[`178caa56ae`](https://github.com/nodejs/node/commit/178caa56ae)] - **fs**: add default options for \*stat() (Tony Brix) [#29114](https://github.com/nodejs/node/pull/29114)
* \[[`f194626ffb`](https://github.com/nodejs/node/commit/f194626ffb)] - **fs**: validate fds as int32s (cjihrig) [#28984](https://github.com/nodejs/node/pull/28984)
* \[[`c5edeb9776`](https://github.com/nodejs/node/commit/c5edeb9776)] - **http**: simplify drain() (Robert Nagy) [#29081](https://github.com/nodejs/node/pull/29081)
* \[[`6b9e372198`](https://github.com/nodejs/node/commit/6b9e372198)] - **http**: follow symbol naming convention (Robert Nagy) [#29091](https://github.com/nodejs/node/pull/29091)
* \[[`2e5008848e`](https://github.com/nodejs/node/commit/2e5008848e)] - **http**: remove redundant condition (Luigi Pinca) [#29078](https://github.com/nodejs/node/pull/29078)
* \[[`13a497912d`](https://github.com/nodejs/node/commit/13a497912d)] - **http**: remove duplicate check (Robert Nagy) [#29022](https://github.com/nodejs/node/pull/29022)
* \[[`16e001112c`](https://github.com/nodejs/node/commit/16e001112c)] - **(SEMVER-MINOR)** **http**: add missing stream-like properties to OutgoingMessage (Robert Nagy) [#29018](https://github.com/nodejs/node/pull/29018)
* \[[`c396b2a5b2`](https://github.com/nodejs/node/commit/c396b2a5b2)] - **http**: buffer writes even while no socket assigned (Robert Nagy) [#29019](https://github.com/nodejs/node/pull/29019)
* \[[`f9b61d2bc7`](https://github.com/nodejs/node/commit/f9b61d2bc7)] - **(SEMVER-MINOR)** **http,stream**: add writableEnded (Robert Nagy) [#28934](https://github.com/nodejs/node/pull/28934)
* \[[`964dff8a9e`](https://github.com/nodejs/node/commit/964dff8a9e)] - **http2**: remove unused FlushData() function (Anna Henningsen) [#29145](https://github.com/nodejs/node/pull/29145)
* \[[`66249bbcad`](https://github.com/nodejs/node/commit/66249bbcad)] - **inspector**: use const for contextGroupId (gengjiawen) [#29076](https://github.com/nodejs/node/pull/29076)
* \[[`cb162298eb`](https://github.com/nodejs/node/commit/cb162298eb)] - **module**: pkg exports validations and fallbacks (Guy Bedford) [#28949](https://github.com/nodejs/node/pull/28949)
* \[[`491b46d605`](https://github.com/nodejs/node/commit/491b46d605)] - **module**: refine package name validation (Guy Bedford) [#28965](https://github.com/nodejs/node/pull/28965)
* \[[`925e40cfa7`](https://github.com/nodejs/node/commit/925e40cfa7)] - **module**: add warning when import,export is detected in CJS context (Giorgos Ntemiris) [#28950](https://github.com/nodejs/node/pull/28950)
* \[[`17319e7f44`](https://github.com/nodejs/node/commit/17319e7f44)] - **net**: use callback to properly propagate error (Robert Nagy) [#29178](https://github.com/nodejs/node/pull/29178)
* \[[`7f11c72cc5`](https://github.com/nodejs/node/commit/7f11c72cc5)] - **readline**: close dumb terminals on Control+D (cjihrig) [#29149](https://github.com/nodejs/node/pull/29149)
* \[[`3c346b8bad`](https://github.com/nodejs/node/commit/3c346b8bad)] - **readline**: close dumb terminals on Control+C (cjihrig) [#29149](https://github.com/nodejs/node/pull/29149)
* \[[`e474c6776c`](https://github.com/nodejs/node/commit/e474c6776c)] - **(SEMVER-MINOR)** **readline**: establish y in cursorTo as optional (Gerhard Stoebich) [#29128](https://github.com/nodejs/node/pull/29128)
* \[[`2528dee674`](https://github.com/nodejs/node/commit/2528dee674)] - **report**: list envvars using uv\_os\_environ() (cjihrig) [#28963](https://github.com/nodejs/node/pull/28963)
* \[[`a6b9299039`](https://github.com/nodejs/node/commit/a6b9299039)] - **src**: rename --security-reverts to ...-revert (Sam Roberts) [#29153](https://github.com/nodejs/node/pull/29153)
* \[[`62a0e91d8c`](https://github.com/nodejs/node/commit/62a0e91d8c)] - **src**: simplify UnionBytes (Ben Noordhuis) [#29116](https://github.com/nodejs/node/pull/29116)
* \[[`b2936cff5d`](https://github.com/nodejs/node/commit/b2936cff5d)] - **src**: organize imports in inspector\_profiler.cc (pi1024e) [#29073](https://github.com/nodejs/node/pull/29073)
* \[[`a9f8b62b47`](https://github.com/nodejs/node/commit/a9f8b62b47)] - **(SEMVER-MINOR)** **stream**: add readableEnded (Robert Nagy) [#28814](https://github.com/nodejs/node/pull/28814)
* \[[`1e3e6da9b9`](https://github.com/nodejs/node/commit/1e3e6da9b9)] - **stream**: simplify howMuchToRead() (Robert Nagy) [#29155](https://github.com/nodejs/node/pull/29155)
* \[[`e2a2a3f4dd`](https://github.com/nodejs/node/commit/e2a2a3f4dd)] - **stream**: use lazy registration for drain for fast destinations (Robert Nagy) [#29095](https://github.com/nodejs/node/pull/29095)
* \[[`2a844599db`](https://github.com/nodejs/node/commit/2a844599db)] - **stream**: improve read() performance further (Brian White) [#29077](https://github.com/nodejs/node/pull/29077)
* \[[`e543d35f35`](https://github.com/nodejs/node/commit/e543d35f35)] - **stream**: inline and simplify onwritedrain (Robert Nagy) [#29037](https://github.com/nodejs/node/pull/29037)
* \[[`6bc4620d8b`](https://github.com/nodejs/node/commit/6bc4620d8b)] - **stream**: improve read() performance more (Brian White) [#28989](https://github.com/nodejs/node/pull/28989)
* \[[`647f3a8d01`](https://github.com/nodejs/node/commit/647f3a8d01)] - **stream**: encapsulate buffer-list (Robert Nagy) [#28974](https://github.com/nodejs/node/pull/28974)
* \[[`000999c06c`](https://github.com/nodejs/node/commit/000999c06c)] - **stream**: improve read() performance (Brian White) [#28961](https://github.com/nodejs/node/pull/28961)
* \[[`6bafd35369`](https://github.com/nodejs/node/commit/6bafd35369)] - **test**: deflake test-tls-passphrase (Luigi Pinca) [#29134](https://github.com/nodejs/node/pull/29134)
* \[[`aff1ef9d27`](https://github.com/nodejs/node/commit/aff1ef9d27)] - **test**: add required settings to test-benchmark-buffer (Rich Trott) [#29163](https://github.com/nodejs/node/pull/29163)
* \[[`18b711366a`](https://github.com/nodejs/node/commit/18b711366a)] - **test**: make exported method static (Rainer Poisel) [#29102](https://github.com/nodejs/node/pull/29102)
* \[[`0aa339e5e1`](https://github.com/nodejs/node/commit/0aa339e5e1)] - **test**: skip test-fs-access if root (Daniel Bevenius) [#29092](https://github.com/nodejs/node/pull/29092)
* \[[`e3b1243ea2`](https://github.com/nodejs/node/commit/e3b1243ea2)] - **test**: unskip tests that now pass on AIX (Sam Roberts) [#29054](https://github.com/nodejs/node/pull/29054)
* \[[`f9ed5f351b`](https://github.com/nodejs/node/commit/f9ed5f351b)] - **test**: assert: add failing deepEqual test for faked boxed primitives (Jordan Harband) [#29029](https://github.com/nodejs/node/pull/29029)
* \[[`43e444b07a`](https://github.com/nodejs/node/commit/43e444b07a)] - **test**: refactor test-sync-io-option (Anna Henningsen) [#29020](https://github.com/nodejs/node/pull/29020)
* \[[`82edebfebf`](https://github.com/nodejs/node/commit/82edebfebf)] - **(SEMVER-MINOR)** **test**: add test for OAEP hash mismatch (Tobias Nießen) [#28335](https://github.com/nodejs/node/pull/28335)
* \[[`f08f154fd6`](https://github.com/nodejs/node/commit/f08f154fd6)] - **(SEMVER-MINOR)** **test**: update postmortem metadata test for V8 7.6 (cjihrig) [#28016](https://github.com/nodejs/node/pull/28016)
* \[[`5b892c44d6`](https://github.com/nodejs/node/commit/5b892c44d6)] - **tls**: allow client-side sockets to be half-opened (Luigi Pinca) [#27836](https://github.com/nodejs/node/pull/27836)
* \[[`c4f6077994`](https://github.com/nodejs/node/commit/c4f6077994)] - **tools**: make code cache and snapshot deterministic (Ben Noordhuis) [#29142](https://github.com/nodejs/node/pull/29142)
* \[[`acdc21b832`](https://github.com/nodejs/node/commit/acdc21b832)] - **tools**: make pty\_helper.py python3-compatible (Ben Noordhuis) [#29167](https://github.com/nodejs/node/pull/29167)
* \[[`31c50e5c17`](https://github.com/nodejs/node/commit/31c50e5c17)] - **tools**: make nodedownload.py Python 3 compatible (cclauss) [#29104](https://github.com/nodejs/node/pull/29104)
* \[[`1011a1792c`](https://github.com/nodejs/node/commit/1011a1792c)] - **tools**: allow single JS file for --link-module (Daniel Bevenius) [#28443](https://github.com/nodejs/node/pull/28443)
* \[[`4d7a7957fc`](https://github.com/nodejs/node/commit/4d7a7957fc)] - **(SEMVER-MINOR)** **tools**: sync gypfiles with V8 7.6 (Michaël Zasso) [#28016](https://github.com/nodejs/node/pull/28016)
* \[[`a4e2549b87`](https://github.com/nodejs/node/commit/a4e2549b87)] - **util**: improve debuglog performance (Brian White) [#28956](https://github.com/nodejs/node/pull/28956)
* \[[`112ec73c95`](https://github.com/nodejs/node/commit/112ec73c95)] - **util**: isEqualBoxedPrimitive: ensure both values are actual boxed Symbols (Jordan Harband) [#29029](https://github.com/nodejs/node/pull/29029)
* \[[`8426077898`](https://github.com/nodejs/node/commit/8426077898)] - **util**: assert: fix deepEqual comparing fake-boxed to real boxed primitive (Jordan Harband) [#29029](https://github.com/nodejs/node/pull/29029)
* \[[`d4e397a900`](https://github.com/nodejs/node/commit/d4e397a900)] - **worker**: fix crash when SharedArrayBuffer outlives creating thread (Anna Henningsen) [#29190](https://github.com/nodejs/node/pull/29190)

<a id="12.8.1"></a>

## 2019-08-15, Version 12.8.1 (Current), @targos

### Notable changes

This is a security release.

Node.js, as well as many other implementations of HTTP/2, have been found
vulnerable to Denial of Service attacks.
See <https://github.com/Netflix/security-bulletins/blob/master/advisories/third-party/2019-002.md>
for more information.

Vulnerabilities fixed:

* **CVE-2019-9511 “Data Dribble”**: The attacker requests a large amount of data from a specified resource over multiple streams. They manipulate window size and stream priority to force the server to queue the data in 1-byte chunks. Depending on how efficiently this data is queued, this can consume excess CPU, memory, or both, potentially leading to a denial of service.
* **CVE-2019-9512 “Ping Flood”**: The attacker sends continual pings to an HTTP/2 peer, causing the peer to build an internal queue of responses. Depending on how efficiently this data is queued, this can consume excess CPU, memory, or both, potentially leading to a denial of service.
* **CVE-2019-9513 “Resource Loop”**: The attacker creates multiple request streams and continually shuffles the priority of the streams in a way that causes substantial churn to the priority tree. This can consume excess CPU, potentially leading to a denial of service.
* **CVE-2019-9514 “Reset Flood”**: The attacker opens a number of streams and sends an invalid request over each stream that should solicit a stream of RST\_STREAM frames from the peer. Depending on how the peer queues the RST\_STREAM frames, this can consume excess memory, CPU, or both, potentially leading to a denial of service.
* **CVE-2019-9515 “Settings Flood”**: The attacker sends a stream of SETTINGS frames to the peer. Since the RFC requires that the peer reply with one acknowledgement per SETTINGS frame, an empty SETTINGS frame is almost equivalent in behavior to a ping. Depending on how efficiently this data is queued, this can consume excess CPU, memory, or both, potentially leading to a denial of service.
* **CVE-2019-9516 “0-Length Headers Leak”**: The attacker sends a stream of headers with a 0-length header name and 0-length header value, optionally Huffman encoded into 1-byte or greater headers. Some implementations allocate memory for these headers and keep the allocation alive until the session dies. This can consume excess memory, potentially leading to a denial of service.
* **CVE-2019-9517 “Internal Data Buffering”**: The attacker opens the HTTP/2 window so the peer can send without constraint; however, they leave the TCP window closed so the peer cannot actually write (many of) the bytes on the wire. The attacker then sends a stream of requests for a large response object. Depending on how the servers queue the responses, this can consume excess memory, CPU, or both, potentially leading to a denial of service.
* **CVE-2019-9518 “Empty Frames Flood”**: The attacker sends a stream of frames with an empty payload and without the end-of-stream flag. These frames can be DATA, HEADERS, CONTINUATION and/or PUSH\_PROMISE. The peer spends time processing each frame disproportionate to attack bandwidth. This can consume excess CPU, potentially leading to a denial of service. (Discovered by Piotr Sikora of Google)

### Commits

* \[[`bfeb5fc07f`](https://github.com/nodejs/node/commit/bfeb5fc07f)] - **deps**: update nghttp2 to 1.39.2 (Anna Henningsen) [#29122](https://github.com/nodejs/node/pull/29122)
* \[[`08021fac59`](https://github.com/nodejs/node/commit/08021fac59)] - **http2**: allow security revert for Ping/Settings Flood (Anna Henningsen) [#29122](https://github.com/nodejs/node/pull/29122)
* \[[`bbb4769cc1`](https://github.com/nodejs/node/commit/bbb4769cc1)] - **http2**: pause input processing if sending output (Anna Henningsen) [#29122](https://github.com/nodejs/node/pull/29122)
* \[[`f64515b05e`](https://github.com/nodejs/node/commit/f64515b05e)] - **http2**: stop reading from socket if writes are in progress (Anna Henningsen) [#29122](https://github.com/nodejs/node/pull/29122)
* \[[`ba332df5d2`](https://github.com/nodejs/node/commit/ba332df5d2)] - **http2**: consider 0-length non-end DATA frames an error (Anna Henningsen) [#29122](https://github.com/nodejs/node/pull/29122)
* \[[`23b0db58ca`](https://github.com/nodejs/node/commit/23b0db58ca)] - **http2**: shrink default `vector::reserve()` allocations (Anna Henningsen) [#29122](https://github.com/nodejs/node/pull/29122)
* \[[`4f10ac3623`](https://github.com/nodejs/node/commit/4f10ac3623)] - **http2**: handle 0-length headers better (Anna Henningsen) [#29122](https://github.com/nodejs/node/pull/29122)
* \[[`a21a1c007b`](https://github.com/nodejs/node/commit/a21a1c007b)] - **http2**: limit number of invalid incoming frames (Anna Henningsen) [#29122](https://github.com/nodejs/node/pull/29122)
* \[[`4570ed10d7`](https://github.com/nodejs/node/commit/4570ed10d7)] - **http2**: limit number of rejected stream openings (Anna Henningsen) [#29122](https://github.com/nodejs/node/pull/29122)
* \[[`88726f2384`](https://github.com/nodejs/node/commit/88726f2384)] - **http2**: do not create ArrayBuffers when no DATA received (Anna Henningsen) [#29122](https://github.com/nodejs/node/pull/29122)
* \[[`530004ef32`](https://github.com/nodejs/node/commit/530004ef32)] - **http2**: only call into JS when necessary for session events (Anna Henningsen) [#29122](https://github.com/nodejs/node/pull/29122)
* \[[`58d8c9ef48`](https://github.com/nodejs/node/commit/58d8c9ef48)] - **http2**: improve JS-side debug logging (Anna Henningsen) [#29122](https://github.com/nodejs/node/pull/29122)

<a id="12.8.0"></a>

## 2019-08-06, Version 12.8.0 (Current), @BridgeAR

### Notable changes

* **assert**:
  * Legacy mode deprecation (`DEP0089`) is revoked (Colin Ihrig) [#28892](https://github.com/nodejs/node/pull/28892)
* **crypto**:
  * The `outputLength` option is added to `crypto.createHash` (Tobias Nießen) [#28805](https://github.com/nodejs/node/pull/28805)
  * The `maxmem` range is increased from 32 to 53 bits (Tobias Nießen) [#28799](https://github.com/nodejs/node/pull/28799)
* **n-api**:
  * Added APIs for per-instance state management (Gabriel Schulhof) [#28682](https://github.com/nodejs/node/pull/28682)
* **report**:
  * Network interfaces get included in the report (Colin Ihrig) [#28911](https://github.com/nodejs/node/pull/28911)
* **src**:
  * `v8.getHeapCodeStatistics()` is now exported (Yuriy Vasiyarov) [#27978](https://github.com/nodejs/node/pull/27978)

### Commits

* \[[`d3426ee9f1`](https://github.com/nodejs/node/commit/d3426ee9f1)] - **assert**: avoid potentially misleading reference to object identity (Anna Henningsen) [#28824](https://github.com/nodejs/node/pull/28824)
* \[[`bbcf9f0625`](https://github.com/nodejs/node/commit/bbcf9f0625)] - **benchmark**: swap var for let in buffer benchmarks (Alex Ramirez) [#28867](https://github.com/nodejs/node/pull/28867)
* \[[`f2c1f3613b`](https://github.com/nodejs/node/commit/f2c1f3613b)] - **benchmark**: swap var for let in util benchmarks (Alex Ramirez) [#28867](https://github.com/nodejs/node/pull/28867)
* \[[`048db38ada`](https://github.com/nodejs/node/commit/048db38ada)] - **benchmark**: swap var for let in url benchmarks (Alex Ramirez) [#28867](https://github.com/nodejs/node/pull/28867)
* \[[`391fe46378`](https://github.com/nodejs/node/commit/391fe46378)] - **benchmark, http**: refactor for code consistency (Alex Ramirez) [#28791](https://github.com/nodejs/node/pull/28791)
* \[[`dcef7b8cc1`](https://github.com/nodejs/node/commit/dcef7b8cc1)] - **build**: include stubs in shared library (Jeroen Ooms) [#28897](https://github.com/nodejs/node/pull/28897)
* \[[`470db47cb4`](https://github.com/nodejs/node/commit/470db47cb4)] - **build**: remove support for s390 (but not s390x) (Ben Noordhuis) [#28883](https://github.com/nodejs/node/pull/28883)
* \[[`25aa2228e4`](https://github.com/nodejs/node/commit/25aa2228e4)] - **build**: generate openssl config for BSD-x86 (Ben Noordhuis) [#28806](https://github.com/nodejs/node/pull/28806)
* \[[`fb57bc4be4`](https://github.com/nodejs/node/commit/fb57bc4be4)] - **build**: do not mix spaces and tabs in Makefile (Luigi Pinca) [#28881](https://github.com/nodejs/node/pull/28881)
* \[[`9e7c66280e`](https://github.com/nodejs/node/commit/9e7c66280e)] - **build**: ignore backup files (Adam Majer) [#28865](https://github.com/nodejs/node/pull/28865)
* \[[`24b9d29650`](https://github.com/nodejs/node/commit/24b9d29650)] - **build**: `uname -m` is amd64 on freebsd, not x86\_64 (Ben Noordhuis) [#28804](https://github.com/nodejs/node/pull/28804)
* \[[`82f263d022`](https://github.com/nodejs/node/commit/82f263d022)] - **build,tools**: support building with Visual Studio 2019 (Michaël Zasso) [#28781](https://github.com/nodejs/node/pull/28781)
* \[[`a7ef102a66`](https://github.com/nodejs/node/commit/a7ef102a66)] - **crypto**: add null check to outputLength logic (Colin Ihrig) [#28864](https://github.com/nodejs/node/pull/28864)
* \[[`3a62202a54`](https://github.com/nodejs/node/commit/3a62202a54)] - **crypto**: fix handling of malicious getters (scrypt) (Tobias Nießen) [#28838](https://github.com/nodejs/node/pull/28838)
* \[[`b7c6ad595b`](https://github.com/nodejs/node/commit/b7c6ad595b)] - **(SEMVER-MINOR)** **crypto**: add outputLength option to crypto.createHash (Tobias Nießen) [#28805](https://github.com/nodejs/node/pull/28805)
* \[[`86f4c68d6a`](https://github.com/nodejs/node/commit/86f4c68d6a)] - **crypto**: update root certificates (Sam Roberts) [#28808](https://github.com/nodejs/node/pull/28808)
* \[[`e0e776331a`](https://github.com/nodejs/node/commit/e0e776331a)] - **(SEMVER-MINOR)** **crypto**: increase maxmem range from 32 to 53 bits (Tobias Nießen) [#28799](https://github.com/nodejs/node/pull/28799)
* \[[`11470d5c26`](https://github.com/nodejs/node/commit/11470d5c26)] - **deps**: upgrade npm to 6.10.2 (isaacs) [#28853](https://github.com/nodejs/node/pull/28853)
* \[[`9b02f3623b`](https://github.com/nodejs/node/commit/9b02f3623b)] - **deps**: dlloads node static linked executable (Luca Lindhorst) [#28045](https://github.com/nodejs/node/pull/28045)
* \[[`24b8f2000c`](https://github.com/nodejs/node/commit/24b8f2000c)] - **deps**: remove backup files (Adam Majer) [#28865](https://github.com/nodejs/node/pull/28865)
* \[[`ae56a232e1`](https://github.com/nodejs/node/commit/ae56a232e1)] - **deps**: backport b107214 from upstream V8 (Anna Henningsen) [#28850](https://github.com/nodejs/node/pull/28850)
* \[[`19dad196e0`](https://github.com/nodejs/node/commit/19dad196e0)] - **deps**: float 15d7e79 from openssl (Tobias Nießen) [#28796](https://github.com/nodejs/node/pull/28796)
* \[[`9dfa636083`](https://github.com/nodejs/node/commit/9dfa636083)] - **dgram**: changed 'var' to 'let' and 'const' (Manuel Ochoa Loaiza) [#28357](https://github.com/nodejs/node/pull/28357)
* \[[`02a50c3b42`](https://github.com/nodejs/node/commit/02a50c3b42)] - **doc**: remove use of you (Michael Dawson) [#28919](https://github.com/nodejs/node/pull/28919)
* \[[`bdd442fe35`](https://github.com/nodejs/node/commit/bdd442fe35)] - **doc**: describe NODE\_OPTIONS interop w/cmd line opts (Alex Aubuchon) [#28928](https://github.com/nodejs/node/pull/28928)
* \[[`57f5d50a3b`](https://github.com/nodejs/node/commit/57f5d50a3b)] - **doc**: fix sorting nit in sections of http.md (Vse Mozhet Byt) [#28943](https://github.com/nodejs/node/pull/28943)
* \[[`f4abf17d36`](https://github.com/nodejs/node/commit/f4abf17d36)] - **doc**: remove legacy mode deprecation in assert (Rich Trott) [#28909](https://github.com/nodejs/node/pull/28909)
* \[[`0ac6d28f80`](https://github.com/nodejs/node/commit/0ac6d28f80)] - **doc**: writableFinished is true before 'finish' (Robert Nagy) [#28811](https://github.com/nodejs/node/pull/28811)
* \[[`7c80963d98`](https://github.com/nodejs/node/commit/7c80963d98)] - **doc**: include "exports" resolver specification (guybedford) [#28899](https://github.com/nodejs/node/pull/28899)
* \[[`5f07f49933`](https://github.com/nodejs/node/commit/5f07f49933)] - **doc**: revoke DEP0089 (Colin Ihrig) [#28892](https://github.com/nodejs/node/pull/28892)
* \[[`3e6342958b`](https://github.com/nodejs/node/commit/3e6342958b)] - **doc**: add example about emitter.emit in events documentation (Felipe Duitama) [#28374](https://github.com/nodejs/node/pull/28374)
* \[[`a28db5f470`](https://github.com/nodejs/node/commit/a28db5f470)] - **doc**: add example of event close for child\_process (Laura Ciro) [#28376](https://github.com/nodejs/node/pull/28376)
* \[[`085eb4828b`](https://github.com/nodejs/node/commit/085eb4828b)] - **doc**: fixup esm resolver spec formatting (Guy Bedford) [#28885](https://github.com/nodejs/node/pull/28885)
* \[[`5533d48290`](https://github.com/nodejs/node/commit/5533d48290)] - **doc**: correct import statement (himself65) [#28876](https://github.com/nodejs/node/pull/28876)
* \[[`ffc7a00c10`](https://github.com/nodejs/node/commit/ffc7a00c10)] - **doc**: add documentation for stream.destroyed (Robert Nagy) [#28815](https://github.com/nodejs/node/pull/28815)
* \[[`454e879a4a`](https://github.com/nodejs/node/commit/454e879a4a)] - **doc**: fix incorrect name in report docs (Colin Ihrig) [#28830](https://github.com/nodejs/node/pull/28830)
* \[[`881e345e0c`](https://github.com/nodejs/node/commit/881e345e0c)] - **doc**: describe why new Buffer() is problematic (Sam Roberts) [#28825](https://github.com/nodejs/node/pull/28825)
* \[[`95b87ce24a`](https://github.com/nodejs/node/commit/95b87ce24a)] - **doc**: claim NODE\_MODULE\_VERSION=76 for Electron 8 (Samuel Attard) [#28809](https://github.com/nodejs/node/pull/28809)
* \[[`0667d0c6c2`](https://github.com/nodejs/node/commit/0667d0c6c2)] - **doc**: add documentation for response.flushHeaders() (Luigi Pinca) [#28807](https://github.com/nodejs/node/pull/28807)
* \[[`c0a044849d`](https://github.com/nodejs/node/commit/c0a044849d)] - **doc**: fix type in NSS update instructions (Sam Roberts) [#28808](https://github.com/nodejs/node/pull/28808)
* \[[`d0b1fb3311`](https://github.com/nodejs/node/commit/d0b1fb3311)] - **doc**: api/stream.md typo from writeable to writable (Cotton Hou) [#28822](https://github.com/nodejs/node/pull/28822)
* \[[`727ffe4720`](https://github.com/nodejs/node/commit/727ffe4720)] - **domain**: use strong reference to domain while active (Anna Henningsen) [#28313](https://github.com/nodejs/node/pull/28313)
* \[[`c9c7256f50`](https://github.com/nodejs/node/commit/c9c7256f50)] - **http**: reset parser.incoming when server response is finished (Anna Henningsen) [#28646](https://github.com/nodejs/node/pull/28646)
* \[[`7d9eb17d30`](https://github.com/nodejs/node/commit/7d9eb17d30)] - **http2**: destroy when settingsFn throws an error (himself65) [#28908](https://github.com/nodejs/node/pull/28908)
* \[[`fa82cbc6cb`](https://github.com/nodejs/node/commit/fa82cbc6cb)] - **http2**: destructure constants from require call (Daniel Nalborczyk) [#28176](https://github.com/nodejs/node/pull/28176)
* \[[`d0d31498d1`](https://github.com/nodejs/node/commit/d0d31498d1)] - **http2**: add constant to already destructured constants (Daniel Nalborczyk) [#28176](https://github.com/nodejs/node/pull/28176)
* \[[`d72b6820bd`](https://github.com/nodejs/node/commit/d72b6820bd)] - **inspector**: report all workers (Eugene Ostroukhov) [#28872](https://github.com/nodejs/node/pull/28872)
* \[[`464136fbc2`](https://github.com/nodejs/node/commit/464136fbc2)] - **lib**: replace var with let in loaders.js (mbj36) [#28081](https://github.com/nodejs/node/pull/28081)
* \[[`386d5d70fb`](https://github.com/nodejs/node/commit/386d5d70fb)] - **lib**: support min/max values in validateInteger() (Colin Ihrig) [#28810](https://github.com/nodejs/node/pull/28810)
* \[[`2236affbf8`](https://github.com/nodejs/node/commit/2236affbf8)] - **module**: exports error as MODULE\_NOT\_FOUND (Guy Bedford) [#28905](https://github.com/nodejs/node/pull/28905)
* \[[`d9084d29fe`](https://github.com/nodejs/node/commit/d9084d29fe)] - **module**: unify package exports test for CJS and ESM (Jan Krems) [#28831](https://github.com/nodejs/node/pull/28831)
* \[[`2262526562`](https://github.com/nodejs/node/commit/2262526562)] - **module**: implement "exports" proposal for CommonJS (Jan Krems) [#28759](https://github.com/nodejs/node/pull/28759)
* \[[`c93df0cfc3`](https://github.com/nodejs/node/commit/c93df0cfc3)] - **n-api**: refactoring napi\_create\_function testing (Octavian Soldea) [#28894](https://github.com/nodejs/node/pull/28894)
* \[[`e6b3bfe111`](https://github.com/nodejs/node/commit/e6b3bfe111)] - **n-api**: refactor a previous commit (Octavian Soldea) [#28848](https://github.com/nodejs/node/pull/28848)
* \[[`860c0d89b6`](https://github.com/nodejs/node/commit/860c0d89b6)] - **(SEMVER-MINOR)** **n-api**: add APIs for per-instance state management (Gabriel Schulhof) [#28682](https://github.com/nodejs/node/pull/28682)
* \[[`3c52dbe15b`](https://github.com/nodejs/node/commit/3c52dbe15b)] - **net**: shallow copy option when create Server (himself65) [#28924](https://github.com/nodejs/node/pull/28924)
* \[[`1f82929ed0`](https://github.com/nodejs/node/commit/1f82929ed0)] - **path**: improve normalization performance (Brian White) [#28948](https://github.com/nodejs/node/pull/28948)
* \[[`5d5c89a8f7`](https://github.com/nodejs/node/commit/5d5c89a8f7)] - **policy**: add dependencies map for resources (Bradley Farias) [#28767](https://github.com/nodejs/node/pull/28767)
* \[[`4b91e4dafd`](https://github.com/nodejs/node/commit/4b91e4dafd)] - **(SEMVER-MINOR)** **report**: include network interfaces in report (Colin Ihrig) [#28911](https://github.com/nodejs/node/pull/28911)
* \[[`e0951c80f6`](https://github.com/nodejs/node/commit/e0951c80f6)] - **report**: loop over uv\_cpu\_info() results (Colin Ihrig) [#28829](https://github.com/nodejs/node/pull/28829)
* \[[`4a747f6037`](https://github.com/nodejs/node/commit/4a747f6037)] - _**Revert**_ "**src**: remove trace\_sync\_io\_ from env" (Сковорода Никита Андреевич) [#28926](https://github.com/nodejs/node/pull/28926)
* \[[`d601a0a9c0`](https://github.com/nodejs/node/commit/d601a0a9c0)] - **src**: allow generic C++ callables in SetImmediate() (Anna Henningsen) [#28704](https://github.com/nodejs/node/pull/28704)
* \[[`3d51d3039c`](https://github.com/nodejs/node/commit/3d51d3039c)] - **src**: large pages fix FreeBSD fix region size (David Carlier) [#28735](https://github.com/nodejs/node/pull/28735)
* \[[`cce208794e`](https://github.com/nodejs/node/commit/cce208794e)] - **(SEMVER-MINOR)** **src**: export v8.GetHeapCodeAndMetadataStatistics() (Yuriy Vasiyarov) [#27978](https://github.com/nodejs/node/pull/27978)
* \[[`32cf344f8e`](https://github.com/nodejs/node/commit/32cf344f8e)] - **src**: readlink("/proc/self/exe") -> uv\_exename() (Ben Noordhuis) [#28333](https://github.com/nodejs/node/pull/28333)
* \[[`1b0d67b1e7`](https://github.com/nodejs/node/commit/1b0d67b1e7)] - **src**: fix OpenBSD build (David Carlier) [#28384](https://github.com/nodejs/node/pull/28384)
* \[[`406c50c1d4`](https://github.com/nodejs/node/commit/406c50c1d4)] - **src**: read break\_node\_first\_line from the inspect options (Samuel Attard) [#28034](https://github.com/nodejs/node/pull/28034)
* \[[`8db43b1ff5`](https://github.com/nodejs/node/commit/8db43b1ff5)] - **src**: move relative uptime init (Micha Hanselmann) [#28849](https://github.com/nodejs/node/pull/28849)
* \[[`e334c1f13b`](https://github.com/nodejs/node/commit/e334c1f13b)] - **src**: fix type name in comment (Ben Noordhuis) [#28320](https://github.com/nodejs/node/pull/28320)
* \[[`cf071a01f2`](https://github.com/nodejs/node/commit/cf071a01f2)] - **stream**: resolve perf regression introduced by V8 7.3 (Matteo Collina) [#28842](https://github.com/nodejs/node/pull/28842)
* \[[`0f8f552105`](https://github.com/nodejs/node/commit/0f8f552105)] - **test**: refactor test-fs-stat.js (Rich Trott) [#28929](https://github.com/nodejs/node/pull/28929)
* \[[`c38952610e`](https://github.com/nodejs/node/commit/c38952610e)] - **test**: add tests for spaces in folder names (PaulBags) [#28819](https://github.com/nodejs/node/pull/28819)
* \[[`efe9b97d40`](https://github.com/nodejs/node/commit/efe9b97d40)] - **test**: refactor test-beforeexit-event-exit using mustNotCall (himself65) [#28901](https://github.com/nodejs/node/pull/28901)
* \[[`c42eb5dd55`](https://github.com/nodejs/node/commit/c42eb5dd55)] - **test**: refactoring test\_error testing (himself65) [#28902](https://github.com/nodejs/node/pull/28902)
* \[[`b6e174b4f5`](https://github.com/nodejs/node/commit/b6e174b4f5)] - **test**: use assert.throws() in test-require-json.js (Alejandro Nanez) [#28358](https://github.com/nodejs/node/pull/28358)
* \[[`19070e442d`](https://github.com/nodejs/node/commit/19070e442d)] - **test**: fix nits in test/fixtures/tls-connect.js (Luigi Pinca) [#28880](https://github.com/nodejs/node/pull/28880)
* \[[`31aa33bdcb`](https://github.com/nodejs/node/commit/31aa33bdcb)] - **test**: fix race in test-http2-origin (Alba Mendez) [#28903](https://github.com/nodejs/node/pull/28903)
* \[[`9b47f77571`](https://github.com/nodejs/node/commit/9b47f77571)] - **test**: udpate test comment description (Andres Bedoya) [#28351](https://github.com/nodejs/node/pull/28351)
* \[[`a0f89a2845`](https://github.com/nodejs/node/commit/a0f89a2845)] - **test**: refactor test using assert instead of try/catch (Juan Bedoya) [#28346](https://github.com/nodejs/node/pull/28346)
* \[[`2142b6d3d1`](https://github.com/nodejs/node/commit/2142b6d3d1)] - **test**: improve test-async-hooks-http-parser-destroy (Gerhard Stoebich) [#28253](https://github.com/nodejs/node/pull/28253)
* \[[`f6051f9506`](https://github.com/nodejs/node/commit/f6051f9506)] - **test**: specialize OOM check for AIX (Sam Roberts) [#28857](https://github.com/nodejs/node/pull/28857)
* \[[`84efadf263`](https://github.com/nodejs/node/commit/84efadf263)] - **test, util**: refactor isObject in test-util (Alex Ramirez) [#28878](https://github.com/nodejs/node/pull/28878)
* \[[`0b6a84a861`](https://github.com/nodejs/node/commit/0b6a84a861)] - **test,report**: relax CPU match requirements (Anna Henningsen) [#28884](https://github.com/nodejs/node/pull/28884)
* \[[`a38fecdb20`](https://github.com/nodejs/node/commit/a38fecdb20)] - **tools**: update certdata.txt (Sam Roberts) [#28808](https://github.com/nodejs/node/pull/28808)
* \[[`b282c8512b`](https://github.com/nodejs/node/commit/b282c8512b)] - **vm**: increase code coverage of source\_text\_module.js (kball) [#28350](https://github.com/nodejs/node/pull/28350)
* \[[`43acce1925`](https://github.com/nodejs/node/commit/43acce1925)] - **worker**: handle calling terminate when kHandler is null (elyalvarado) [#28370](https://github.com/nodejs/node/pull/28370)

<a id="12.7.0"></a>

## 2019-07-23, Version 12.7.0 (Current), @targos

### Notable changes

* **deps**:
  * Updated nghttp2 to 1.39.1 (gengjiawen) [#28448](https://github.com/nodejs/node/pull/28448).
  * Updated npm to 6.10.0 (isaacs) [#28525](https://github.com/nodejs/node/pull/28525).
* **esm**:
  * Implemented experimental "pkg-exports" proposal. A new `"exports"` field can
    be added to a module's `package.json` file to provide custom subpath
    aliasing. See [proposal-pkg-exports](https://github.com/jkrems/proposal-pkg-exports/)
    for more information (Guy Bedford) [#28568](https://github.com/nodejs/node/pull/28568).
* **http**:
  * Added `response.writableFinished` (Robert Nagy) [#28681](https://github.com/nodejs/node/pull/28681).
  * Exposed `headers`, `rawHeaders` and other fields on an `http.ClientRequest`
    `"information"` event (Austin Wright) [#28459](https://github.com/nodejs/node/pull/28459).
* **inspector**:
  * Added `inspector.waitForDebugger()` (Aleksei Koziatinskii) [#28453](https://github.com/nodejs/node/pull/28453).
* **policy**:
  * Added `--policy-integrity=sri` CLI option to mitigate policy tampering. If a
    policy integrity is specified and the policy does not have that integrity,
    Node.js will error prior to running any code (Bradley Farias) [#28734](https://github.com/nodejs/node/pull/28734).
* **readline,tty**:
  * Exposed stream API from various methods which write characters (cjihrig) [#28674](https://github.com/nodejs/node/pull/28674),
    [#28721](https://github.com/nodejs/node/pull/28721).
* **src**:
  * Use cgroups to get memory limits. This improves the way we set the memory ceiling for a Node.js process. Previously
    we would use the physical memory size to estimate the necessary V8 heap sizes. The physical memory size
    is not necessarily the correct limit, e.g. if the process is running inside a docker container or is otherwise
    constrained. This change adds the ability to get a memory limit set by linux cgroups, which is used by
    [docker containers to set resource constraints](https://docs.docker.com/config/containers/resource_constraints/)
    (Kelvin Jin) [#27508](https://github.com/nodejs/node/pull/27508).

### Commits

* \[[`632d7d5839`](https://github.com/nodejs/node/commit/632d7d5839)] - **build**: skip test-ci doc targets if no crypto (Rod Vagg) [#28747](https://github.com/nodejs/node/pull/28747)
* \[[`5d09c15c5b`](https://github.com/nodejs/node/commit/5d09c15c5b)] - **build**: update of the large page option error (David Carlier) [#28729](https://github.com/nodejs/node/pull/28729)
* \[[`be32becb67`](https://github.com/nodejs/node/commit/be32becb67)] - **build**: fix building with d8 (Michaël Zasso) [#28733](https://github.com/nodejs/node/pull/28733)
* \[[`72f92293c8`](https://github.com/nodejs/node/commit/72f92293c8)] - **build**: specify Python version once for all tests (cclauss) [#28694](https://github.com/nodejs/node/pull/28694)
* \[[`b4aa7d3570`](https://github.com/nodejs/node/commit/b4aa7d3570)] - **build**: remove broken intel vtune support (Ben Noordhuis) [#28522](https://github.com/nodejs/node/pull/28522)
* \[[`171c8f44b6`](https://github.com/nodejs/node/commit/171c8f44b6)] - **build**: do not always build the default V8 snapshot (Michaël Zasso) [#28467](https://github.com/nodejs/node/pull/28467)
* \[[`608d6ed090`](https://github.com/nodejs/node/commit/608d6ed090)] - **build**: update Windows icon to Feb 2016 rebrand (Mike MacCana) [#28524](https://github.com/nodejs/node/pull/28524)
* \[[`7d3ddfe6b8`](https://github.com/nodejs/node/commit/7d3ddfe6b8)] - **build**: remove --code-cache-path help option (Daniel Bevenius) [#28446](https://github.com/nodejs/node/pull/28446)
* \[[`e4fae24b62`](https://github.com/nodejs/node/commit/e4fae24b62)] - **build**: change ASM compiler url to https (gengjiawen) [#28189](https://github.com/nodejs/node/pull/28189)
* \[[`209b353ff4`](https://github.com/nodejs/node/commit/209b353ff4)] - **build,v8**: support IBM i (Xu Meng) [#28607](https://github.com/nodejs/node/pull/28607)
* \[[`674d33cb8c`](https://github.com/nodejs/node/commit/674d33cb8c)] - **deps**: V8: backport b33af60 (Gus Caplan) [#28671](https://github.com/nodejs/node/pull/28671)
* \[[`9f47242e19`](https://github.com/nodejs/node/commit/9f47242e19)] - **deps**: update nghttp2 to 1.39.1 (gengjiawen) [#28448](https://github.com/nodejs/node/pull/28448)
* \[[`1ce2b5e828`](https://github.com/nodejs/node/commit/1ce2b5e828)] - **deps**: upgrade npm to 6.10.0 (isaacs) [#28525](https://github.com/nodejs/node/pull/28525)
* \[[`312f94916c`](https://github.com/nodejs/node/commit/312f94916c)] - **deps**: V8: backport d2ccc59 (Joyee Cheung) [#28648](https://github.com/nodejs/node/pull/28648)
* \[[`df0f42ab7f`](https://github.com/nodejs/node/commit/df0f42ab7f)] - **deps**: cherry-pick 91744bf from node-gyp upstream (Jon Kunkee) [#28604](https://github.com/nodejs/node/pull/28604)
* \[[`7fa982ee89`](https://github.com/nodejs/node/commit/7fa982ee89)] - **deps**: cherry-pick 721dc7d from node-gyp upstream (Jon Kunkee) [#28604](https://github.com/nodejs/node/pull/28604)
* \[[`9e9bfb65c7`](https://github.com/nodejs/node/commit/9e9bfb65c7)] - **deps**: cherry-pick 13a04aba from V8 upstream (Jon Kunkee) [#28602](https://github.com/nodejs/node/pull/28602)
* \[[`c7cb70ce5e`](https://github.com/nodejs/node/commit/c7cb70ce5e)] - **deps**: update acorn to 6.2.0 (Michaël Zasso) [#28649](https://github.com/nodejs/node/pull/28649)
* \[[`0ee1298056`](https://github.com/nodejs/node/commit/0ee1298056)] - **dns**: fix unsigned record values (Brian White) [#28792](https://github.com/nodejs/node/pull/28792)
* \[[`8586294670`](https://github.com/nodejs/node/commit/8586294670)] - **doc**: claim NODE\_MODULE\_VERSION=75 for Electron 7 (Samuel Attard) [#28774](https://github.com/nodejs/node/pull/28774)
* \[[`2a82d54d9d`](https://github.com/nodejs/node/commit/2a82d54d9d)] - **doc**: update env default on child\_process functions (h3knix) [#28776](https://github.com/nodejs/node/pull/28776)
* \[[`cf811ecd47`](https://github.com/nodejs/node/commit/cf811ecd47)] - **doc**: add code example to subprocess.stdout (Juan José Arboleda) [#28402](https://github.com/nodejs/node/pull/28402)
* \[[`06991cd902`](https://github.com/nodejs/node/commit/06991cd902)] - **doc**: add information for heap snapshot flag (Tanner Stirrat) [#28754](https://github.com/nodejs/node/pull/28754)
* \[[`8fe9ca416d`](https://github.com/nodejs/node/commit/8fe9ca416d)] - **doc**: amplify warning for execute callback (Michael Dawson) [#28738](https://github.com/nodejs/node/pull/28738)
* \[[`ca83b2736e`](https://github.com/nodejs/node/commit/ca83b2736e)] - **doc**: add example for beforeExit event (Vickodev) [#28430](https://github.com/nodejs/node/pull/28430)
* \[[`44acec5386`](https://github.com/nodejs/node/commit/44acec5386)] - **doc**: add example for zlib.createGzip() (Alex Ramirez) [#28136](https://github.com/nodejs/node/pull/28136)
* \[[`4a78fe5ab0`](https://github.com/nodejs/node/commit/4a78fe5ab0)] - **doc**: improve os.homedir() docs (Juan José Arboleda) [#28401](https://github.com/nodejs/node/pull/28401)
* \[[`3f78a51b5e`](https://github.com/nodejs/node/commit/3f78a51b5e)] - **doc**: add examples at assert.strictEqual (himself65) [#28092](https://github.com/nodejs/node/pull/28092)
* \[[`3a4a236b51`](https://github.com/nodejs/node/commit/3a4a236b51)] - **doc**: fix minor typo (Shajan Jacob) [#28148](https://github.com/nodejs/node/pull/28148)
* \[[`4321cb2cf3`](https://github.com/nodejs/node/commit/4321cb2cf3)] - **doc**: update js-native-api example (Gabriel Schulhof) [#28657](https://github.com/nodejs/node/pull/28657)
* \[[`8ddf86b3d4`](https://github.com/nodejs/node/commit/8ddf86b3d4)] - **doc**: add missing version metadata for Readable.from (Anna Henningsen) [#28695](https://github.com/nodejs/node/pull/28695)
* \[[`638c8a394c`](https://github.com/nodejs/node/commit/638c8a394c)] - **doc**: small grammar correction (cjihrig) [#28669](https://github.com/nodejs/node/pull/28669)
* \[[`5614e08f34`](https://github.com/nodejs/node/commit/5614e08f34)] - **doc**: add documentation for createDiffieHellmanGroup (Ojasvi Monga) [#28585](https://github.com/nodejs/node/pull/28585)
* \[[`aee86940f9`](https://github.com/nodejs/node/commit/aee86940f9)] - **doc**: mark N-API thread-safe function stable (Gabriel Schulhof) [#28643](https://github.com/nodejs/node/pull/28643)
* \[[`7a4062ab88`](https://github.com/nodejs/node/commit/7a4062ab88)] - **doc**: mark process.report as experimental (cjihrig) [#28653](https://github.com/nodejs/node/pull/28653)
* \[[`3f65b91eb9`](https://github.com/nodejs/node/commit/3f65b91eb9)] - **doc**: remove superfluous MDN link in assert.md (Rich Trott) [#28246](https://github.com/nodejs/node/pull/28246)
* \[[`f688122dff`](https://github.com/nodejs/node/commit/f688122dff)] - **doc**: drop 'for more details' in deprecations (cjihrig) [#28617](https://github.com/nodejs/node/pull/28617)
* \[[`d7c7023503`](https://github.com/nodejs/node/commit/d7c7023503)] - **doc**: add example on how to create \_\_filename, \_\_dirname for esm (Walle Cyril) [#28282](https://github.com/nodejs/node/pull/28282)
* \[[`ebc3876754`](https://github.com/nodejs/node/commit/ebc3876754)] - **doc**: add missing types (Luigi Pinca) [#28623](https://github.com/nodejs/node/pull/28623)
* \[[`f7a13e5034`](https://github.com/nodejs/node/commit/f7a13e5034)] - **doc**: relax requirements for setAAD in CCM mode (Tobias Nießen) [#28624](https://github.com/nodejs/node/pull/28624)
* \[[`bf2d5a75f8`](https://github.com/nodejs/node/commit/bf2d5a75f8)] - **doc**: add a link to the throw-deprecations flag (Lucas Holmquist) [#28625](https://github.com/nodejs/node/pull/28625)
* \[[`871a60cd12`](https://github.com/nodejs/node/commit/871a60cd12)] - **doc**: fix nits in stream.md (Vse Mozhet Byt) [#28591](https://github.com/nodejs/node/pull/28591)
* \[[`0380a558af`](https://github.com/nodejs/node/commit/0380a558af)] - **doc**: edit stream module introduction (Rich Trott) [#28595](https://github.com/nodejs/node/pull/28595)
* \[[`729b232d11`](https://github.com/nodejs/node/commit/729b232d11)] - **doc**: change 'unix' to 'Unix' in ninja guide (Rich Trott) [#28619](https://github.com/nodejs/node/pull/28619)
* \[[`74af944de1`](https://github.com/nodejs/node/commit/74af944de1)] - **doc**: add line for inspect host:port invocation (Tim Baverstock) [#28405](https://github.com/nodejs/node/pull/28405)
* \[[`0aca527263`](https://github.com/nodejs/node/commit/0aca527263)] - **doc**: mention unit for event loop delay measurements (Jan Krems) [#28629](https://github.com/nodejs/node/pull/28629)
* \[[`ac9908fe37`](https://github.com/nodejs/node/commit/ac9908fe37)] - **doc**: update stream.md "Organization of this Document" (Rich Trott) [#28601](https://github.com/nodejs/node/pull/28601)
* \[[`9be1111179`](https://github.com/nodejs/node/commit/9be1111179)] - **doc**: move Usage and Example to same header level (Rich Trott) [#28570](https://github.com/nodejs/node/pull/28570)
* \[[`70c3116783`](https://github.com/nodejs/node/commit/70c3116783)] - **doc**: mention markdown linting in BUILDING.md (Tariq Ramlall) [#28578](https://github.com/nodejs/node/pull/28578)
* \[[`f0e4bf990e`](https://github.com/nodejs/node/commit/f0e4bf990e)] - **doc**: remove URLs from zlib docs (cjihrig) [#28580](https://github.com/nodejs/node/pull/28580)
* \[[`a6d50a7562`](https://github.com/nodejs/node/commit/a6d50a7562)] - **doc**: make tls links more readable (cjihrig) [#28580](https://github.com/nodejs/node/pull/28580)
* \[[`6f3ebb8787`](https://github.com/nodejs/node/commit/6f3ebb8787)] - **doc**: clarify http2 server.close() behavior (cjihrig) [#28581](https://github.com/nodejs/node/pull/28581)
* \[[`2205818cca`](https://github.com/nodejs/node/commit/2205818cca)] - **doc**: format Unix consistently (cjihrig) [#28576](https://github.com/nodejs/node/pull/28576)
* \[[`643d09961b`](https://github.com/nodejs/node/commit/643d09961b)] - **doc**: document family:0 behavior in socket.connect (cjihrig) [#28574](https://github.com/nodejs/node/pull/28574)
* \[[`d2ba4547aa`](https://github.com/nodejs/node/commit/d2ba4547aa)] - **doc**: fix link in build instructions (Gautham B A) [#28572](https://github.com/nodejs/node/pull/28572)
* \[[`24a77ae19a`](https://github.com/nodejs/node/commit/24a77ae19a)] - **doc**: add description for the listener argument (Luigi Pinca) [#28500](https://github.com/nodejs/node/pull/28500)
* \[[`0777e090b4`](https://github.com/nodejs/node/commit/0777e090b4)] - **doc**: fix family default value in socket.connect (Kirill Fomichev) [#28521](https://github.com/nodejs/node/pull/28521)
* \[[`29d2076ac7`](https://github.com/nodejs/node/commit/29d2076ac7)] - **doc**: simplify `process.resourceUsage()` section (Vse Mozhet Byt) [#28499](https://github.com/nodejs/node/pull/28499)
* \[[`e83b256306`](https://github.com/nodejs/node/commit/e83b256306)] - **doc**: add example for chmod in fs.md (Juan Roa) [#28365](https://github.com/nodejs/node/pull/28365)
* \[[`c177a68c7f`](https://github.com/nodejs/node/commit/c177a68c7f)] - **doc**: provide an example to fs.stat() (Felipe) [#28381](https://github.com/nodejs/node/pull/28381)
* \[[`68ed32f71d`](https://github.com/nodejs/node/commit/68ed32f71d)] - **doc**: fix link from bootstrap README to BUILDING (Rod Vagg) [#28504](https://github.com/nodejs/node/pull/28504)
* \[[`59aaee4295`](https://github.com/nodejs/node/commit/59aaee4295)] - **doc**: format try...catch consistently (cjihrig) [#28481](https://github.com/nodejs/node/pull/28481)
* \[[`ec9ba4b803`](https://github.com/nodejs/node/commit/ec9ba4b803)] - **doc**: remove unnecessary stability specifiers (cjihrig) [#28485](https://github.com/nodejs/node/pull/28485)
* \[[`0a0832fb52`](https://github.com/nodejs/node/commit/0a0832fb52)] - **doc**: address missing paren (cjihrig) [#28483](https://github.com/nodejs/node/pull/28483)
* \[[`b379c0e8b6`](https://github.com/nodejs/node/commit/b379c0e8b6)] - **(SEMVER-MINOR)** **esm**: implement "pkg-exports" proposal (Guy Bedford) [#28568](https://github.com/nodejs/node/pull/28568)
* \[[`d630cc0ec5`](https://github.com/nodejs/node/commit/d630cc0ec5)] - **gyp**: cherrypick more Python3 changes from node-gyp (cclauss) [#28563](https://github.com/nodejs/node/pull/28563)
* \[[`b1db810d50`](https://github.com/nodejs/node/commit/b1db810d50)] - **gyp**: pull Python 3 changes from node/node-gyp (cclauss) [#28573](https://github.com/nodejs/node/pull/28573)
* \[[`ed8504388e`](https://github.com/nodejs/node/commit/ed8504388e)] - **http**: avoid extra listener (Robert Nagy) [#28705](https://github.com/nodejs/node/pull/28705)
* \[[`06d0abea0d`](https://github.com/nodejs/node/commit/06d0abea0d)] - **(SEMVER-MINOR)** **http**: add response.writableFinished (Robert Nagy) [#28681](https://github.com/nodejs/node/pull/28681)
* \[[`2308c7412a`](https://github.com/nodejs/node/commit/2308c7412a)] - **(SEMVER-MINOR)** **http**: expose headers on an http.ClientRequest "information" event (Austin Wright) [#28459](https://github.com/nodejs/node/pull/28459)
* \[[`38f8cd5ba1`](https://github.com/nodejs/node/commit/38f8cd5ba1)] - **http**: improve parser error messages (Anna Henningsen) [#28487](https://github.com/nodejs/node/pull/28487)
* \[[`49e4d72b5a`](https://github.com/nodejs/node/commit/49e4d72b5a)] - **http2**: compat req.complete (Robert Nagy) [#28627](https://github.com/nodejs/node/pull/28627)
* \[[`62f36828be`](https://github.com/nodejs/node/commit/62f36828be)] - **http2**: report memory allocated by nghttp2 to V8 (Anna Henningsen) [#28645](https://github.com/nodejs/node/pull/28645)
* \[[`5b9c22710a`](https://github.com/nodejs/node/commit/5b9c22710a)] - **http2**: override authority with options (Luigi Pinca) [#28584](https://github.com/nodejs/node/pull/28584)
* \[[`77bdbc5f0d`](https://github.com/nodejs/node/commit/77bdbc5f0d)] - **(SEMVER-MINOR)** **inspector**: add inspector.waitForDebugger() (Aleksei Koziatinskii) [#28453](https://github.com/nodejs/node/pull/28453)
* \[[`7b0b06d735`](https://github.com/nodejs/node/commit/7b0b06d735)] - **inspector**: do not spin-wait while waiting for the initial connection (Eugene Ostroukhov) [#28756](https://github.com/nodejs/node/pull/28756)
* \[[`aba0cf33ec`](https://github.com/nodejs/node/commit/aba0cf33ec)] - **inspector**: do not change async call stack depth if the worker is done (Eugene Ostroukhov) [#28613](https://github.com/nodejs/node/pull/28613)
* \[[`66382abe29`](https://github.com/nodejs/node/commit/66382abe29)] - **inspector**: reduce InspectorIo API surface (Eugene Ostroukhov) [#28526](https://github.com/nodejs/node/pull/28526)
* \[[`5c100075f0`](https://github.com/nodejs/node/commit/5c100075f0)] - **lib**: rename lib/internal/readline.js (cjihrig) [#28753](https://github.com/nodejs/node/pull/28753)
* \[[`75c628130f`](https://github.com/nodejs/node/commit/75c628130f)] - **lib**: use `class ... extends` in perf\_hooks.js (Anna Henningsen) [#28495](https://github.com/nodejs/node/pull/28495)
* \[[`1770bc870e`](https://github.com/nodejs/node/commit/1770bc870e)] - **module**: increase code coverage of cjs loader (Andrey Melikhov) [#27898](https://github.com/nodejs/node/pull/27898)
* \[[`9c6791ee00`](https://github.com/nodejs/node/commit/9c6791ee00)] - **n-api**: correct bug in napi\_get\_last\_error (Octavian Soldea) [#28702](https://github.com/nodejs/node/pull/28702)
* \[[`44de4317cf`](https://github.com/nodejs/node/commit/44de4317cf)] - **n-api**: make thread-safe-function calls properly (Gabriel Schulhof) [#28606](https://github.com/nodejs/node/pull/28606)
* \[[`5b5c8196c3`](https://github.com/nodejs/node/commit/5b5c8196c3)] - **path**: move branch to the correct location (Ruben Bridgewater) [#28556](https://github.com/nodejs/node/pull/28556)
* \[[`18c56df928`](https://github.com/nodejs/node/commit/18c56df928)] - **path**: using .relative() should not return a trailing slash (Ruben Bridgewater) [#28556](https://github.com/nodejs/node/pull/28556)
* \[[`997531193b`](https://github.com/nodejs/node/commit/997531193b)] - **perf\_hooks**: add HttpRequest statistics monitoring #28445 (vmarchaud) [#28486](https://github.com/nodejs/node/pull/28486)
* \[[`2eeb44f3fa`](https://github.com/nodejs/node/commit/2eeb44f3fa)] - **(SEMVER-MINOR)** **policy**: add policy-integrity to mitigate policy tampering (Bradley Farias) [#28734](https://github.com/nodejs/node/pull/28734)
* \[[`4cb0fc3ab1`](https://github.com/nodejs/node/commit/4cb0fc3ab1)] - **process**: refactor unhandledRejection logic (cjihrig) [#28540](https://github.com/nodejs/node/pull/28540)
* \[[`caee9106ac`](https://github.com/nodejs/node/commit/caee9106ac)] - **(SEMVER-MINOR)** **readline**: expose stream API in cursorTo() (cjihrig) [#28674](https://github.com/nodejs/node/pull/28674)
* \[[`4a7e20ff81`](https://github.com/nodejs/node/commit/4a7e20ff81)] - **(SEMVER-MINOR)** **readline**: expose stream API in moveCursor() (cjihrig) [#28674](https://github.com/nodejs/node/pull/28674)
* \[[`0f5af44304`](https://github.com/nodejs/node/commit/0f5af44304)] - **(SEMVER-MINOR)** **readline**: expose stream API in clearLine() (cjihrig) [#28674](https://github.com/nodejs/node/pull/28674)
* \[[`17df75f5c9`](https://github.com/nodejs/node/commit/17df75f5c9)] - **(SEMVER-MINOR)** **readline**: expose stream API in clearScreenDown() (cjihrig) [#28641](https://github.com/nodejs/node/pull/28641)
* \[[`0383947ed7`](https://github.com/nodejs/node/commit/0383947ed7)] - **readline**: simplify isFullWidthCodePoint() (cjihrig) [#28640](https://github.com/nodejs/node/pull/28640)
* \[[`dc734030fc`](https://github.com/nodejs/node/commit/dc734030fc)] - **readline**: remove IIFE in SIGCONT handler (cjihrig) [#28639](https://github.com/nodejs/node/pull/28639)
* \[[`e0c5e7a939`](https://github.com/nodejs/node/commit/e0c5e7a939)] - **readline**: use named constant for surrogate checks (cjihrig) [#28638](https://github.com/nodejs/node/pull/28638)
* \[[`e6e98afbf2`](https://github.com/nodejs/node/commit/e6e98afbf2)] - **readline**: fix position computation (Benoît Zugmeyer) [#28272](https://github.com/nodejs/node/pull/28272)
* \[[`d611f5ad3e`](https://github.com/nodejs/node/commit/d611f5ad3e)] - **repl**: fix some repl context issues (Ruben Bridgewater) [#28561](https://github.com/nodejs/node/pull/28561)
* \[[`cbd586aa99`](https://github.com/nodejs/node/commit/cbd586aa99)] - **repl**: fix autocomplete while using .load (Ruben Bridgewater) [#28608](https://github.com/nodejs/node/pull/28608)
* \[[`35e3f1f449`](https://github.com/nodejs/node/commit/35e3f1f449)] - **report**: modify getReport() to return an Object (Christopher Hiller) [#28630](https://github.com/nodejs/node/pull/28630)
* \[[`302865e8b9`](https://github.com/nodejs/node/commit/302865e8b9)] - **src**: do not include partial AsyncWrap instances in heap dump (Anna Henningsen) [#28789](https://github.com/nodejs/node/pull/28789)
* \[[`c0f24be185`](https://github.com/nodejs/node/commit/c0f24be185)] - **src**: make `CompiledFnEntry` a `BaseObject` (Anna Henningsen) [#28782](https://github.com/nodejs/node/pull/28782)
* \[[`7df54988e1`](https://github.com/nodejs/node/commit/7df54988e1)] - **src**: silence compiler warning (cjihrig) [#28764](https://github.com/nodejs/node/pull/28764)
* \[[`2839298a1e`](https://github.com/nodejs/node/commit/2839298a1e)] - **src**: expose TraceEventHelper with NODE\_EXTERN (Samuel Attard) [#28724](https://github.com/nodejs/node/pull/28724)
* \[[`74243da707`](https://github.com/nodejs/node/commit/74243da707)] - **src**: add public virtual destructor for KVStore (GauthamBanasandra) [#28737](https://github.com/nodejs/node/pull/28737)
* \[[`0b7fecaf97`](https://github.com/nodejs/node/commit/0b7fecaf97)] - **src**: large pages option: FreeBSD support proposal (David Carlier) [#28331](https://github.com/nodejs/node/pull/28331)
* \[[`1f0fd1bb78`](https://github.com/nodejs/node/commit/1f0fd1bb78)] - **src**: add missing option parser template for the DebugOptionsParser (Samuel Attard) [#28543](https://github.com/nodejs/node/pull/28543)
* \[[`4b9d4193e1`](https://github.com/nodejs/node/commit/4b9d4193e1)] - **src**: lint #defines in src/node.h (Tariq Ramlall) [#28547](https://github.com/nodejs/node/pull/28547)
* \[[`5c1d5958e0`](https://github.com/nodejs/node/commit/5c1d5958e0)] - **src**: add cleanup hook for ContextifyContext (Anna Henningsen) [#28631](https://github.com/nodejs/node/pull/28631)
* \[[`29fda66ca6`](https://github.com/nodejs/node/commit/29fda66ca6)] - **src**: simplify --debug flags (cjihrig) [#28615](https://github.com/nodejs/node/pull/28615)
* \[[`c50e235947`](https://github.com/nodejs/node/commit/c50e235947)] - **src**: replace already elevated Object, Local v8 namespace (Juan José Arboleda) [#28611](https://github.com/nodejs/node/pull/28611)
* \[[`3c418d9629`](https://github.com/nodejs/node/commit/3c418d9629)] - **src**: manage MakeContext() pointer with unique\_ptr (cjihrig) [#28616](https://github.com/nodejs/node/pull/28616)
* \[[`22daf952de`](https://github.com/nodejs/node/commit/22daf952de)] - **src**: clang build warning fix (David Carlier) [#28480](https://github.com/nodejs/node/pull/28480)
* \[[`a8b094cf3b`](https://github.com/nodejs/node/commit/a8b094cf3b)] - **src**: implement special member functions for classes in env.h (GauthamBanasandra) [#28579](https://github.com/nodejs/node/pull/28579)
* \[[`c432ab1391`](https://github.com/nodejs/node/commit/c432ab1391)] - **src**: simplify DEP0062 logic (cjihrig) [#28589](https://github.com/nodejs/node/pull/28589)
* \[[`4f035e4d84`](https://github.com/nodejs/node/commit/4f035e4d84)] - **src**: implement runtime option --no-node-snapshot for debugging (Joyee Cheung) [#28567](https://github.com/nodejs/node/pull/28567)
* \[[`a24ab56dc5`](https://github.com/nodejs/node/commit/a24ab56dc5)] - **src**: allow fatal exceptions to be enhanced (cjihrig) [#28562](https://github.com/nodejs/node/pull/28562)
* \[[`d4113f96f5`](https://github.com/nodejs/node/commit/d4113f96f5)] - **src**: block SIGTTOU before calling tcsetattr() (Ben Noordhuis) [#28535](https://github.com/nodejs/node/pull/28535)
* \[[`48c369b715`](https://github.com/nodejs/node/commit/48c369b715)] - **src**: correct json writer placement in process.report (himself65) [#28433](https://github.com/nodejs/node/pull/28433)
* \[[`8d41b07c4c`](https://github.com/nodejs/node/commit/8d41b07c4c)] - **src**: remove unused using declarations in src/api (Daniel Bevenius) [#28506](https://github.com/nodejs/node/pull/28506)
* \[[`6fbad8baa4`](https://github.com/nodejs/node/commit/6fbad8baa4)] - **src**: configure v8 isolate with uv\_get\_constrained\_memory (Kelvin Jin) [#27508](https://github.com/nodejs/node/pull/27508)
* \[[`f3f51e4187`](https://github.com/nodejs/node/commit/f3f51e4187)] - **src**: use thread\_local to declare modpending (Gabriel Schulhof) [#28456](https://github.com/nodejs/node/pull/28456)
* \[[`e610c45076`](https://github.com/nodejs/node/commit/e610c45076)] - **src**: remove redundant return (gengjiawen) [#28189](https://github.com/nodejs/node/pull/28189)
* \[[`d34c2567c9`](https://github.com/nodejs/node/commit/d34c2567c9)] - **src, tools**: replace raw ptr with smart ptr (GauthamBanasandra) [#28577](https://github.com/nodejs/node/pull/28577)
* \[[`0793398b4f`](https://github.com/nodejs/node/commit/0793398b4f)] - **stream**: add null push transform in async\_iterator (David Mark Clements) [#28566](https://github.com/nodejs/node/pull/28566)
* \[[`00b2200e03`](https://github.com/nodejs/node/commit/00b2200e03)] - **(SEMVER-MINOR)** **stream**: use readableEncoding public api for child\_process (ZYSzys) [#28548](https://github.com/nodejs/node/pull/28548)
* \[[`af6fe5f4c5`](https://github.com/nodejs/node/commit/af6fe5f4c5)] - **test**: fix assertion argument order in test-esm-namespace (Alex Ramirez) [#28474](https://github.com/nodejs/node/pull/28474)
* \[[`7989d5c600`](https://github.com/nodejs/node/commit/7989d5c600)] - **test**: changed function to arrow function (Harshitha KP) [#28726](https://github.com/nodejs/node/pull/28726)
* \[[`88809a49f6`](https://github.com/nodejs/node/commit/88809a49f6)] - **test**: propagate napi\_status to JS (Octavian Soldea) [#28505](https://github.com/nodejs/node/pull/28505)
* \[[`61db987b01`](https://github.com/nodejs/node/commit/61db987b01)] - **test**: use consistent test naming (Rich Trott) [#28744](https://github.com/nodejs/node/pull/28744)
* \[[`506b50a54a`](https://github.com/nodejs/node/commit/506b50a54a)] - **test**: make repl tests more resilient (Ruben Bridgewater) [#28608](https://github.com/nodejs/node/pull/28608)
* \[[`af6608ca11`](https://github.com/nodejs/node/commit/af6608ca11)] - **test**: improve variable names in pty\_helper.py (Anna Henningsen) [#28688](https://github.com/nodejs/node/pull/28688)
* \[[`9b2eee12eb`](https://github.com/nodejs/node/commit/9b2eee12eb)] - **test**: update hasFipsCrypto in test/common/README (Daniel Bevenius) [#28507](https://github.com/nodejs/node/pull/28507)
* \[[`d3f51457af`](https://github.com/nodejs/node/commit/d3f51457af)] - **test**: use openssl\_is\_fips instead of hasFipsCrypto (Daniel Bevenius) [#28507](https://github.com/nodejs/node/pull/28507)
* \[[`499969db9e`](https://github.com/nodejs/node/commit/499969db9e)] - **test**: increase limit for network space overhead test (Ben L. Titzer) [#28492](https://github.com/nodejs/node/pull/28492)
* \[[`9f6600ac1c`](https://github.com/nodejs/node/commit/9f6600ac1c)] - **test**: fix pty test hangs on aix (Ben Noordhuis) [#28600](https://github.com/nodejs/node/pull/28600)
* \[[`b4643dd9dc`](https://github.com/nodejs/node/commit/b4643dd9dc)] - **test**: add test-fs-writeFileSync-invalid-windows (Rich Trott) [#28569](https://github.com/nodejs/node/pull/28569)
* \[[`e2adfb79b0`](https://github.com/nodejs/node/commit/e2adfb79b0)] - **test**: refactor test-fs-write-sync (Gabriela Niño) [#28371](https://github.com/nodejs/node/pull/28371)
* \[[`4c333f4028`](https://github.com/nodejs/node/commit/4c333f4028)] - **test**: change the repeat Buffer.from('blerg'); statments (Miken) [#28372](https://github.com/nodejs/node/pull/28372)
* \[[`598037346e`](https://github.com/nodejs/node/commit/598037346e)] - **test**: check getReport when error with one line stack (himself65) [#28433](https://github.com/nodejs/node/pull/28433)
* \[[`793163e353`](https://github.com/nodejs/node/commit/793163e353)] - **test**: check writeReport when error with one line stack (himself65) [#28433](https://github.com/nodejs/node/pull/28433)
* \[[`c3311c25ff`](https://github.com/nodejs/node/commit/c3311c25ff)] - **test**: generate des rsa\_cert.pfx (Caleb ツ Everett) [#28471](https://github.com/nodejs/node/pull/28471)
* \[[`4941d47212`](https://github.com/nodejs/node/commit/4941d47212)] - **test**: don't use deprecated crypto.fips property (Ben Noordhuis) [#28509](https://github.com/nodejs/node/pull/28509)
* \[[`e854bfa3b1`](https://github.com/nodejs/node/commit/e854bfa3b1)] - **test**: create home for test-npm-install (Daniel Bevenius) [#28510](https://github.com/nodejs/node/pull/28510)
* \[[`13f139368f`](https://github.com/nodejs/node/commit/13f139368f)] - **test**: unmark test-gc-http-client-onerror flaky (Rich Trott) [#28429](https://github.com/nodejs/node/pull/28429)
* \[[`b7731eb0e4`](https://github.com/nodejs/node/commit/b7731eb0e4)] - **test**: skip pseudo-tty tests on AIX (Sam Roberts) [#28541](https://github.com/nodejs/node/pull/28541)
* \[[`33ab37fcdb`](https://github.com/nodejs/node/commit/33ab37fcdb)] - **test**: skip stringbytes-external-exceed-max on AIX (Sam Roberts) [#28516](https://github.com/nodejs/node/pull/28516)
* \[[`f0c436ff50`](https://github.com/nodejs/node/commit/f0c436ff50)] - **test**: switch the argument order for the assertion (Ivan Villa) [#28356](https://github.com/nodejs/node/pull/28356)
* \[[`49c533964f`](https://github.com/nodejs/node/commit/49c533964f)] - **test**: fix assertion argument order in test-https-agent.js (Julian Correa) [#28383](https://github.com/nodejs/node/pull/28383)
* \[[`e4f1e909e1`](https://github.com/nodejs/node/commit/e4f1e909e1)] - **test**: increase test-resource-usage.js validation (cjihrig) [#28498](https://github.com/nodejs/node/pull/28498)
* \[[`ff432c8ef6`](https://github.com/nodejs/node/commit/ff432c8ef6)] - **test,win**: cleanup exec-timeout processes (João Reis) [#28723](https://github.com/nodejs/node/pull/28723)
* \[[`ed43880d6b`](https://github.com/nodejs/node/commit/ed43880d6b)] - **tools**: update ESLint to 6.1.0 (cjihrig) [#28793](https://github.com/nodejs/node/pull/28793)
* \[[`5eb37cccc6`](https://github.com/nodejs/node/commit/5eb37cccc6)] - **tools**: remove unused pkgsrc directory (Michaël Zasso) [#28783](https://github.com/nodejs/node/pull/28783)
* \[[`9ffa5fb6b8`](https://github.com/nodejs/node/commit/9ffa5fb6b8)] - **tools**: add coverage to ignored files (Lucas Holmquist) [#28626](https://github.com/nodejs/node/pull/28626)
* \[[`ccb54f7a84`](https://github.com/nodejs/node/commit/ccb54f7a84)] - **tools**: add markdown lint rule for 'Unix' (Rich Trott) [#28619](https://github.com/nodejs/node/pull/28619)
* \[[`487a417dd1`](https://github.com/nodejs/node/commit/487a417dd1)] - **(SEMVER-MINOR)** **tty**: expose stream API from readline methods (cjihrig) [#28721](https://github.com/nodejs/node/pull/28721)
* \[[`7b4638cee0`](https://github.com/nodejs/node/commit/7b4638cee0)] - **vm**: fix gc bug with modules and compiled functions (Gus Caplan) [#28671](https://github.com/nodejs/node/pull/28671)
* \[[`a0e8a25721`](https://github.com/nodejs/node/commit/a0e8a25721)] - **vm**: remove usage of public util module (Karen He) [#28460](https://github.com/nodejs/node/pull/28460)
* \[[`0e2cbe6203`](https://github.com/nodejs/node/commit/0e2cbe6203)] - **worker**: fix passing multiple SharedArrayBuffers at once (Anna Henningsen) [#28582](https://github.com/nodejs/node/pull/28582)
* \[[`cbf540136f`](https://github.com/nodejs/node/commit/cbf540136f)] - **worker**: assign missing deprecation code (James M Snell) [#28395](https://github.com/nodejs/node/pull/28395)
* \[[`b8079f5c23`](https://github.com/nodejs/node/commit/b8079f5c23)] - **zlib**: remove usage of public util module (Karen He) [#28454](https://github.com/nodejs/node/pull/28454)
* \[[`03de306281`](https://github.com/nodejs/node/commit/03de306281)] - **zlib**: do not coalesce multiple `.flush()` calls (Anna Henningsen) [#28520](https://github.com/nodejs/node/pull/28520)

<a id="12.6.0"></a>

## 2019-07-03, Version 12.6.0 (Current), @targos

### Notable changes

* **build**:
  * Experimental support for building Node.js on MIPS architecture is back [#27992](https://github.com/nodejs/node/pull/27992).
* **child\_process**:
  * The promisified versions of `child_process.exec` and `child_process.execFile`
    now both return a `Promise` which has the child instance attached to their
    `child` property [#28325](https://github.com/nodejs/node/pull/28325).
* **deps**:
  * Updated libuv to 1.30.1 [#28449](https://github.com/nodejs/node/pull/28449), [#28511](https://github.com/nodejs/node/pull/28511).
    * Support for the Haiku platform has been added.
    * The maximum `UV_THREADPOOL_SIZE` has been increased from 128 to 1024.
    * `uv_fs_copyfile()` now works properly when the source and destination files are the same.
* **process**:
  * A new method, `process.resourceUsage()` was added. It returns resource usage
    for the current process, such as CPU time [#28018](https://github.com/nodejs/node/pull/28018).
* **src**:
  * Fixed an issue related to stdio that could lead to a crash of the process in
    some circumstances [#28490](https://github.com/nodejs/node/pull/28490).
* **stream**:
  * Added a `writableFinished` property to writable streams. It indicates that
    all the data has been flushed to the underlying system [#28007](https://github.com/nodejs/node/pull/28007).
* **worker**:
  * Fixed an issue that prevented worker threads to listen for data on stdin [#28153](https://github.com/nodejs/node/pull/28153).
* **meta**:
  * Added [Jiawen Geng](https://github.com/gengjiawen) to collaborators [#28322](https://github.com/nodejs/node/pull/28322).

### Commits

* \[[`db65594c33`](https://github.com/nodejs/node/commit/db65594c33)] - **benchmark**: refactor buffer benchmarks (Ruben Bridgewater) [#26418](https://github.com/nodejs/node/pull/26418)
* \[[`e607055693`](https://github.com/nodejs/node/commit/e607055693)] - **bootstrap**: --frozen-intrinsics override problem workaround (Guy Bedford) [#28254](https://github.com/nodejs/node/pull/28254)
* \[[`cd71aad62b`](https://github.com/nodejs/node/commit/cd71aad62b)] - **build**: expose napi\_build\_version variable (NickNaso) [#27835](https://github.com/nodejs/node/pull/27835)
* \[[`4d12cef2a5`](https://github.com/nodejs/node/commit/4d12cef2a5)] - **build**: link libatomic on mac and linux (Gus Caplan) [#28232](https://github.com/nodejs/node/pull/28232)
* \[[`cfb5ca3887`](https://github.com/nodejs/node/commit/cfb5ca3887)] - **build**: enable openssl support for mips64el (mutao) [#27992](https://github.com/nodejs/node/pull/27992)
* \[[`2cf37f54f0`](https://github.com/nodejs/node/commit/2cf37f54f0)] - _**Revert**_ "**build**: remove mips support" (mutao) [#27992](https://github.com/nodejs/node/pull/27992)
* \[[`dd5e07f9b4`](https://github.com/nodejs/node/commit/dd5e07f9b4)] - **child\_process**: attach child in promisification (cjihrig) [#28325](https://github.com/nodejs/node/pull/28325)
* \[[`f21ddb2131`](https://github.com/nodejs/node/commit/f21ddb2131)] - **crypto**: move \_impl call out of handleError funct (Daniel Bevenius) [#28318](https://github.com/nodejs/node/pull/28318)
* \[[`558e9cfb6c`](https://github.com/nodejs/node/commit/558e9cfb6c)] - **crypto**: move \_pbkdf2 call out of handleError funct (Daniel Bevenius) [#28318](https://github.com/nodejs/node/pull/28318)
* \[[`47b230a92b`](https://github.com/nodejs/node/commit/47b230a92b)] - **crypto**: move \_randomBytes call out of handleError funct (Daniel Bevenius) [#28318](https://github.com/nodejs/node/pull/28318)
* \[[`def96ae278`](https://github.com/nodejs/node/commit/def96ae278)] - **crypto**: move \_scrypt call out of handleError funct (Daniel Bevenius) [#28318](https://github.com/nodejs/node/pull/28318)
* \[[`990feafcb6`](https://github.com/nodejs/node/commit/990feafcb6)] - **crypto**: fix crash when calling digest after piping (Tobias Nießen) [#28251](https://github.com/nodejs/node/pull/28251)
* \[[`43677325e1`](https://github.com/nodejs/node/commit/43677325e1)] - **deps**: upgrade to libuv 1.30.0 (cjihrig) [#28449](https://github.com/nodejs/node/pull/28449)
* \[[`3a493b804e`](https://github.com/nodejs/node/commit/3a493b804e)] - **deps**: upgrade to libuv 1.30.1 (cjihrig) [#28511](https://github.com/nodejs/node/pull/28511)
* \[[`eee66c5e56`](https://github.com/nodejs/node/commit/eee66c5e56)] - **doc**: merge bootstrap/README.md into BUILDING.md (Rod Vagg) [#28465](https://github.com/nodejs/node/pull/28465)
* \[[`0111c61ec0`](https://github.com/nodejs/node/commit/0111c61ec0)] - **doc**: fix swapedOut typo (cjihrig) [#28497](https://github.com/nodejs/node/pull/28497)
* \[[`14f6cee694`](https://github.com/nodejs/node/commit/14f6cee694)] - **doc**: reformat for-await-of (cjihrig) [#28425](https://github.com/nodejs/node/pull/28425)
* \[[`3fea2e43c0`](https://github.com/nodejs/node/commit/3fea2e43c0)] - **doc**: update readline asyncIterator docs (cjihrig) [#28425](https://github.com/nodejs/node/pull/28425)
* \[[`0d2d116446`](https://github.com/nodejs/node/commit/0d2d116446)] - **doc**: add links to 12.5.0 changelog notable changes (Gus Caplan) [#28450](https://github.com/nodejs/node/pull/28450)
* \[[`96e8b988d4`](https://github.com/nodejs/node/commit/96e8b988d4)] - **doc**: clean up isDead() example (cjihrig) [#28421](https://github.com/nodejs/node/pull/28421)
* \[[`3c047b3919`](https://github.com/nodejs/node/commit/3c047b3919)] - **doc**: clarify response.finished (Robert Nagy) [#28411](https://github.com/nodejs/node/pull/28411)
* \[[`5367d02ce1`](https://github.com/nodejs/node/commit/5367d02ce1)] - **doc**: replace version with REPLACEME (cjihrig) [#28431](https://github.com/nodejs/node/pull/28431)
* \[[`e55d0efe36`](https://github.com/nodejs/node/commit/e55d0efe36)] - **doc**: remove N-API version for Experimental APIs (Michael Dawson) [#28330](https://github.com/nodejs/node/pull/28330)
* \[[`e3dd4d5225`](https://github.com/nodejs/node/commit/e3dd4d5225)] - **doc**: fix nits regarding stream utilities (Vse Mozhet Byt) [#28385](https://github.com/nodejs/node/pull/28385)
* \[[`3d693c5ead`](https://github.com/nodejs/node/commit/3d693c5ead)] - **doc**: cleanup pendingSettingsAck docs (cjihrig) [#28388](https://github.com/nodejs/node/pull/28388)
* \[[`b6d0cbcf20`](https://github.com/nodejs/node/commit/b6d0cbcf20)] - **doc**: add example code for worker.isDead() to cluster.md (Jesse Cogollo) [#28362](https://github.com/nodejs/node/pull/28362)
* \[[`0e6196cc17`](https://github.com/nodejs/node/commit/0e6196cc17)] - **doc**: add missing word in frameError event docs (cjihrig) [#28387](https://github.com/nodejs/node/pull/28387)
* \[[`d25d40e1e5`](https://github.com/nodejs/node/commit/d25d40e1e5)] - **doc**: fix sentence about Http2Stream destruction (cjihrig) [#28336](https://github.com/nodejs/node/pull/28336)
* \[[`4762399aca`](https://github.com/nodejs/node/commit/4762399aca)] - **doc**: add example for Buffer.isEncoding() (Angie M. Delgado) [#28360](https://github.com/nodejs/node/pull/28360)
* \[[`818f08416c`](https://github.com/nodejs/node/commit/818f08416c)] - **doc**: add example code for fs.existsSync() (nicolasrestrepo) [#28354](https://github.com/nodejs/node/pull/28354)
* \[[`d759e0fa49`](https://github.com/nodejs/node/commit/d759e0fa49)] - **doc**: remove "note that" from assert.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`d384911746`](https://github.com/nodejs/node/commit/d384911746)] - **doc**: remove "note that" from async\_hooks.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`9ca7c8603e`](https://github.com/nodejs/node/commit/9ca7c8603e)] - **doc**: remove "note that" from buffer.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`658c7587ff`](https://github.com/nodejs/node/commit/658c7587ff)] - **doc**: remove "note that" from cli.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`cb89b3b290`](https://github.com/nodejs/node/commit/cb89b3b290)] - **doc**: remove "note that" from cluster.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`af05ad123e`](https://github.com/nodejs/node/commit/af05ad123e)] - **doc**: remove "note that" from console.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`898b69ccdf`](https://github.com/nodejs/node/commit/898b69ccdf)] - **doc**: remove "note that" from crypto.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`c41dbf5bc7`](https://github.com/nodejs/node/commit/c41dbf5bc7)] - **doc**: remove "note that" from dgram.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`33d9cf5a7c`](https://github.com/nodejs/node/commit/33d9cf5a7c)] - **doc**: remove "note that" from dns.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`f3b4449c07`](https://github.com/nodejs/node/commit/f3b4449c07)] - **doc**: remove "note that" from domain.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`75954865e6`](https://github.com/nodejs/node/commit/75954865e6)] - **doc**: remove "note that" from errors.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`520ef836c1`](https://github.com/nodejs/node/commit/520ef836c1)] - **doc**: remove "note that" from events.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`d65c90b545`](https://github.com/nodejs/node/commit/d65c90b545)] - **doc**: remove "note that" from fs.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`3174bc14a2`](https://github.com/nodejs/node/commit/3174bc14a2)] - **doc**: remove "note that" from http.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`f0a857f4b8`](https://github.com/nodejs/node/commit/f0a857f4b8)] - **doc**: remove "note that" from http2.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`f4c6f7a5db`](https://github.com/nodejs/node/commit/f4c6f7a5db)] - **doc**: remove "note that" from modules.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`f299c44860`](https://github.com/nodejs/node/commit/f299c44860)] - **doc**: remove "note that" from net.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`b0a6da7e3c`](https://github.com/nodejs/node/commit/b0a6da7e3c)] - **doc**: remove "note that" from process.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`eba2e3c0df`](https://github.com/nodejs/node/commit/eba2e3c0df)] - **doc**: remove "note that" from stream.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`7bd2cae197`](https://github.com/nodejs/node/commit/7bd2cae197)] - **doc**: remove "note that" from tls.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`204c9d8aa8`](https://github.com/nodejs/node/commit/204c9d8aa8)] - **doc**: remove "note that" from tty.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`5e979bff2f`](https://github.com/nodejs/node/commit/5e979bff2f)] - **doc**: remove "note that" from url.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`c3c86b6da6`](https://github.com/nodejs/node/commit/c3c86b6da6)] - **doc**: remove "note that" from util.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`6d94620bfc`](https://github.com/nodejs/node/commit/6d94620bfc)] - **doc**: remove "note that" from zlib.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`651ab3f58e`](https://github.com/nodejs/node/commit/651ab3f58e)] - **doc**: remove "note that" from pull-requests.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`9ac3a553ea`](https://github.com/nodejs/node/commit/9ac3a553ea)] - **doc**: remove "note that" from maintaining-V8.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`a67afc8b60`](https://github.com/nodejs/node/commit/a67afc8b60)] - **doc**: remove "note that" from maintaining-the-build-files.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`9461ef8afb`](https://github.com/nodejs/node/commit/9461ef8afb)] - **doc**: remove "note that" from using-symbols.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`ffba80b107`](https://github.com/nodejs/node/commit/ffba80b107)] - **doc**: remove "note that" from writing-and-running-benchmarks.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`1591309735`](https://github.com/nodejs/node/commit/1591309735)] - **doc**: remove "note that" from writing-tests.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`3daced70cf`](https://github.com/nodejs/node/commit/3daced70cf)] - **doc**: remove "make that" from onboarding.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`79f23b5aa6`](https://github.com/nodejs/node/commit/79f23b5aa6)] - **doc**: remove "note that" from releases.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`71cf5586a9`](https://github.com/nodejs/node/commit/71cf5586a9)] - **doc**: remove "note that" from CPP\_STYLE\_GUIDE.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`3d6ae65181`](https://github.com/nodejs/node/commit/3d6ae65181)] - **doc**: remote "note that" from BUILDING.md (Rich Trott) [#28329](https://github.com/nodejs/node/pull/28329)
* \[[`64f8530adc`](https://github.com/nodejs/node/commit/64f8530adc)] - **doc**: fix typo in process.disconnect() docs (cjihrig) [#28328](https://github.com/nodejs/node/pull/28328)
* \[[`c9226f5eb3`](https://github.com/nodejs/node/commit/c9226f5eb3)] - **doc**: drop 'Note that' in addons docs (cjihrig) [#28327](https://github.com/nodejs/node/pull/28327)
* \[[`a213eb7635`](https://github.com/nodejs/node/commit/a213eb7635)] - **doc**: remove obsolete external link (cjihrig) [#28326](https://github.com/nodejs/node/pull/28326)
* \[[`632fc1faf5`](https://github.com/nodejs/node/commit/632fc1faf5)] - **doc**: make multipleResolves docs less opinionated (cjihrig) [#28314](https://github.com/nodejs/node/pull/28314)
* \[[`6063cebdd6`](https://github.com/nodejs/node/commit/6063cebdd6)] - **doc**: format ECMA-262 with a hyphen (cjihrig) [#28309](https://github.com/nodejs/node/pull/28309)
* \[[`51742b834d`](https://github.com/nodejs/node/commit/51742b834d)] - **doc**: revise assert legacy mode text (Rich Trott) [#28315](https://github.com/nodejs/node/pull/28315)
* \[[`57ac661bcb`](https://github.com/nodejs/node/commit/57ac661bcb)] - **doc**: document PerformanceNodeTiming.environment field (Yuriy Vasiyarov) [#28280](https://github.com/nodejs/node/pull/28280)
* \[[`1f2b8c8cab`](https://github.com/nodejs/node/commit/1f2b8c8cab)] - **doc**: revise strict mode text in assert (Rich Trott) [#28285](https://github.com/nodejs/node/pull/28285)
* \[[`0856a4d043`](https://github.com/nodejs/node/commit/0856a4d043)] - **doc**: add gengjiawen to collaborators (gengjiawen) [#28322](https://github.com/nodejs/node/pull/28322)
* \[[`359e20f048`](https://github.com/nodejs/node/commit/359e20f048)] - **doc**: clarify when http emits aborted event (Robert Nagy) [#28262](https://github.com/nodejs/node/pull/28262)
* \[[`168c12758b`](https://github.com/nodejs/node/commit/168c12758b)] - **doc**: tidy AssertionError text (Rich Trott) [#28255](https://github.com/nodejs/node/pull/28255)
* \[[`17efd9372b`](https://github.com/nodejs/node/commit/17efd9372b)] - **doc**: remove instructions to post CI links (Rich Trott) [#28248](https://github.com/nodejs/node/pull/28248)
* \[[`91d5a4df04`](https://github.com/nodejs/node/commit/91d5a4df04)] - **doc,n-api**: fix metadata for napi\_create\_threadsafe\_function (Richard Lau) [#28410](https://github.com/nodejs/node/pull/28410)
* \[[`c9a96aeeee`](https://github.com/nodejs/node/commit/c9a96aeeee)] - **esm**: ensure cwd-relative imports for module --eval (Guy Bedford) [#28389](https://github.com/nodejs/node/pull/28389)
* \[[`fd4d1e20f3`](https://github.com/nodejs/node/commit/fd4d1e20f3)] - **http2**: remove square brackets from parsed hostname (Luigi Pinca) [#28406](https://github.com/nodejs/node/pull/28406)
* \[[`d8d4f9b569`](https://github.com/nodejs/node/commit/d8d4f9b569)] - **http2**: propagate session destroy code to streams (cjihrig) [#28435](https://github.com/nodejs/node/pull/28435)
* \[[`d8942f877d`](https://github.com/nodejs/node/commit/d8942f877d)] - **(SEMVER-MINOR)** **http2**: use writableFinished instead of \_writableState (zero1five) [#28007](https://github.com/nodejs/node/pull/28007)
* \[[`d0de204c12`](https://github.com/nodejs/node/commit/d0de204c12)] - **http2**: refactor ping + settings object lifetime management (Anna Henningsen) [#28150](https://github.com/nodejs/node/pull/28150)
* \[[`5f9ee9f69f`](https://github.com/nodejs/node/commit/5f9ee9f69f)] - **lib**: fix stack overflow check to not break on primitives (kball) [#28338](https://github.com/nodejs/node/pull/28338)
* \[[`b6a70520d2`](https://github.com/nodejs/node/commit/b6a70520d2)] - **lib**: refactor unhandled rejection deprecation warning emission (Joyee Cheung) [#28258](https://github.com/nodejs/node/pull/28258)
* \[[`d95d610e0e`](https://github.com/nodejs/node/commit/d95d610e0e)] - **meta**: update LICENSE (Rich Trott) [#28260](https://github.com/nodejs/node/pull/28260)
* \[[`ed8cee6b1a`](https://github.com/nodejs/node/commit/ed8cee6b1a)] - **n-api**: add error message for date expected (Gabriel Schulhof) [#28303](https://github.com/nodejs/node/pull/28303)
* \[[`53297e66cb`](https://github.com/nodejs/node/commit/53297e66cb)] - **(SEMVER-MINOR)** **n-api**: make func argument of napi\_create\_threadsafe\_function optional (legendecas) [#27791](https://github.com/nodejs/node/pull/27791)
* \[[`8ad880f3fc`](https://github.com/nodejs/node/commit/8ad880f3fc)] - **net**: replace \_writableState.finished with writableFinished (Rich Trott) [#27974](https://github.com/nodejs/node/pull/27974)
* \[[`19f9281743`](https://github.com/nodejs/node/commit/19f9281743)] - **(SEMVER-MINOR)** **process**: expose uv\_rusage on process.resourcesUsage() (vmarchaud) [#28018](https://github.com/nodejs/node/pull/28018)
* \[[`0fd6524680`](https://github.com/nodejs/node/commit/0fd6524680)] - **process**: split routines used to enhance fatal exception stack traces (Joyee Cheung) [#28308](https://github.com/nodejs/node/pull/28308)
* \[[`e517b03701`](https://github.com/nodejs/node/commit/e517b03701)] - **process**: hide NodeEnvironmentFlagsSet's `add` function (Ruben Bridgewater) [#28206](https://github.com/nodejs/node/pull/28206)
* \[[`c4a357dada`](https://github.com/nodejs/node/commit/c4a357dada)] - **report**: add report versioning (cjihrig) [#28121](https://github.com/nodejs/node/pull/28121)
* \[[`035b613f80`](https://github.com/nodejs/node/commit/035b613f80)] - **src**: don't abort on EIO when restoring tty (Ben Noordhuis) [#28490](https://github.com/nodejs/node/pull/28490)
* \[[`624fd17064`](https://github.com/nodejs/node/commit/624fd17064)] - **src**: fix small memory leak (David Carlier) [#28452](https://github.com/nodejs/node/pull/28452)
* \[[`0044fd2642`](https://github.com/nodejs/node/commit/0044fd2642)] - **src**: add error codes to errors thrown in node\_i18n.cc (Yaniv Friedensohn) [#28221](https://github.com/nodejs/node/pull/28221)
* \[[`5b92eb4686`](https://github.com/nodejs/node/commit/5b92eb4686)] - **src**: refactor uncaught exception handling (Joyee Cheung) [#28257](https://github.com/nodejs/node/pull/28257)
* \[[`c491e4dfe6`](https://github.com/nodejs/node/commit/c491e4dfe6)] - **src**: fall back to env->exec\_path() for default profile directory (Joyee Cheung) [#28252](https://github.com/nodejs/node/pull/28252)
* \[[`040b9db07b`](https://github.com/nodejs/node/commit/040b9db07b)] - **src**: save exec path when initializing Environment (Joyee Cheung) [#28252](https://github.com/nodejs/node/pull/28252)
* \[[`1650bcf491`](https://github.com/nodejs/node/commit/1650bcf491)] - **(SEMVER-MINOR)** **stream**: add writableFinished (zero1five) [#28007](https://github.com/nodejs/node/pull/28007)
* \[[`8a64b70efe`](https://github.com/nodejs/node/commit/8a64b70efe)] - **test**: fix flaky test-vm-timeout-escape-nexttick (Rich Trott) [#28461](https://github.com/nodejs/node/pull/28461)
* \[[`3f6f968dee`](https://github.com/nodejs/node/commit/3f6f968dee)] - **test**: skip tests related to CI failures on AIX (Sam Roberts) [#28469](https://github.com/nodejs/node/pull/28469)
* \[[`937afcc365`](https://github.com/nodejs/node/commit/937afcc365)] - **test**: add test to doesNotThrow; validate if actual with regex (estrada9166) [#28355](https://github.com/nodejs/node/pull/28355)
* \[[`004d26d5a5`](https://github.com/nodejs/node/commit/004d26d5a5)] - **test**: add tests to assert.ok and improve coverage (estrada9166) [#28355](https://github.com/nodejs/node/pull/28355)
* \[[`82b80e0a61`](https://github.com/nodejs/node/commit/82b80e0a61)] - **test**: reset validity dates of expired certs (Sam Roberts) [#28473](https://github.com/nodejs/node/pull/28473)
* \[[`dce4947335`](https://github.com/nodejs/node/commit/dce4947335)] - **test**: do not use fixed port in async-hooks/test-httparser-reuse (Anna Henningsen) [#28312](https://github.com/nodejs/node/pull/28312)
* \[[`79b1bf5a09`](https://github.com/nodejs/node/commit/79b1bf5a09)] - **test**: use assert() in N-API async test (Anna Henningsen) [#28423](https://github.com/nodejs/node/pull/28423)
* \[[`cd78c5ef7e`](https://github.com/nodejs/node/commit/cd78c5ef7e)] - **test**: fixing broken test (melinamejia95) [#28345](https://github.com/nodejs/node/pull/28345)
* \[[`d88c697f7f`](https://github.com/nodejs/node/commit/d88c697f7f)] - **test**: refactoring test, reordering arguments (David Sánchez) [#28343](https://github.com/nodejs/node/pull/28343)
* \[[`e63990e383`](https://github.com/nodejs/node/commit/e63990e383)] - **test**: eliminate duplicate statements (khriztianmoreno) [#28342](https://github.com/nodejs/node/pull/28342)
* \[[`b822545f84`](https://github.com/nodejs/node/commit/b822545f84)] - **test**: switch the param order in the assertion (raveneyex) [#28341](https://github.com/nodejs/node/pull/28341)
* \[[`3bc62b9374`](https://github.com/nodejs/node/commit/3bc62b9374)] - **test**: switch assertion order (Yomar) [#28339](https://github.com/nodejs/node/pull/28339)
* \[[`ecf4494dd2`](https://github.com/nodejs/node/commit/ecf4494dd2)] - **test**: tls switch arguments order for the assertion (Laura Ciro) [#28340](https://github.com/nodejs/node/pull/28340)
* \[[`4bca4a5091`](https://github.com/nodejs/node/commit/4bca4a5091)] - **test**: change order of arguments (MistyBlunch) [#28359](https://github.com/nodejs/node/pull/28359)
* \[[`4973f217b8`](https://github.com/nodejs/node/commit/4973f217b8)] - **test**: fix order of assertion arguments in test-event-emitter-num-args (Luis Gallon) [#28368](https://github.com/nodejs/node/pull/28368)
* \[[`69f17f1ab0`](https://github.com/nodejs/node/commit/69f17f1ab0)] - **test**: make test-dh-regr more efficient where possible (Rich Trott) [#28390](https://github.com/nodejs/node/pull/28390)
* \[[`9f508e3a0a`](https://github.com/nodejs/node/commit/9f508e3a0a)] - **test**: split pummel crypto dh test into two separate tests (Rich Trott) [#28390](https://github.com/nodejs/node/pull/28390)
* \[[`e161744610`](https://github.com/nodejs/node/commit/e161744610)] - **test**: move non-pummel crypto DH tests to parallel (Rich Trott) [#28390](https://github.com/nodejs/node/pull/28390)
* \[[`16926a8183`](https://github.com/nodejs/node/commit/16926a8183)] - **test**: duplicated buffer in test-stream2-writable.js (Duvan Monsalve) [#28380](https://github.com/nodejs/node/pull/28380)
* \[[`758a003f9d`](https://github.com/nodejs/node/commit/758a003f9d)] - **test**: fix assertion argument order in test-buffer-failed-alloc-type (Alex Ramirez) [#28349](https://github.com/nodejs/node/pull/28349)
* \[[`5047006980`](https://github.com/nodejs/node/commit/5047006980)] - **test**: use regex for OpenSSL function name (Daniel Bevenius) [#28289](https://github.com/nodejs/node/pull/28289)
* \[[`b448db3e01`](https://github.com/nodejs/node/commit/b448db3e01)] - **test**: remove test-ttywrap.writestream.js (Rich Trott) [#28316](https://github.com/nodejs/node/pull/28316)
* \[[`8346596552`](https://github.com/nodejs/node/commit/8346596552)] - **test**: permit test-graph.signal to work without test runner (Rich Trott) [#28305](https://github.com/nodejs/node/pull/28305)
* \[[`337aef0c2f`](https://github.com/nodejs/node/commit/337aef0c2f)] - **test**: normalize location test-worker-process-cwd.js runs tests (Samantha Sample) [#28271](https://github.com/nodejs/node/pull/28271)
* \[[`c14e4d5bd5`](https://github.com/nodejs/node/commit/c14e4d5bd5)] - **test**: use .code for error in setgid (=) [#28219](https://github.com/nodejs/node/pull/28219)
* \[[`c44db7fea5`](https://github.com/nodejs/node/commit/c44db7fea5)] - **test**: fix flaky test-worker-debug (Anna Henningsen) [#28307](https://github.com/nodejs/node/pull/28307)
* \[[`424d91aacb`](https://github.com/nodejs/node/commit/424d91aacb)] - **test**: add logging to statwatcher test (Rich Trott) [#28270](https://github.com/nodejs/node/pull/28270)
* \[[`72f52a330b`](https://github.com/nodejs/node/commit/72f52a330b)] - **test**: add Worker + uncaughtException + process.exit() test (Anna Henningsen) [#28259](https://github.com/nodejs/node/pull/28259)
* \[[`3a2e67b916`](https://github.com/nodejs/node/commit/3a2e67b916)] - **test**: do not spawn rmdir in test-statwatcher (João Reis) [#28276](https://github.com/nodejs/node/pull/28276)
* \[[`d949eadc38`](https://github.com/nodejs/node/commit/d949eadc38)] - **test**: check custom inspection truncation in assert (Rich Trott) [#28234](https://github.com/nodejs/node/pull/28234)
* \[[`993c0dbf14`](https://github.com/nodejs/node/commit/993c0dbf14)] - **test**: make sure test function resolves in test-worker-debug (Anna Henningsen) [#28155](https://github.com/nodejs/node/pull/28155)
* \[[`1b4a7fb9cb`](https://github.com/nodejs/node/comit/1b4a7fb9cb)] - **tools**: update unified-args to 7.0.0 for md-lint CLI (Rich Trott) [#28434](https://github.com/nodejs/node/pull/28434)
* \[[`40ae2a6025`](https://github.com/nodejs/node/commit/40ae2a6025)] - **tools**: move python code out of jenkins shell (Sam Roberts) [#28458](https://github.com/nodejs/node/pull/28458)
* \[[`d38b98529c`](https://github.com/nodejs/node/commit/d38b98529c)] - **tools**: fix v8 testing with devtoolset on ppcle (Sam Roberts) [#28458](https://github.com/nodejs/node/pull/28458)
* \[[`b8084840d8`](https://github.com/nodejs/node/commit/b8084840d8)] - **tools**: change editorconfig's 'ignore' to 'unset' (silverwind) [#28440](https://github.com/nodejs/node/pull/28440)
* \[[`21d2bdd3ce`](https://github.com/nodejs/node/commit/21d2bdd3ce)] - **tools**: remove unused using declarations (Daniel Bevenius) [#28422](https://github.com/nodejs/node/pull/28422)
* \[[`3d014e1bf9`](https://github.com/nodejs/node/commit/3d014e1bf9)] - **tools**: remove out-of-date code-cache-path comment (Daniel Bevenius) [#28419](https://github.com/nodejs/node/pull/28419)
* \[[`60cf9111cb`](https://github.com/nodejs/node/commit/60cf9111cb)] - **tools**: fix typo in js2c.py (Daniel Bevenius) [#28417](https://github.com/nodejs/node/pull/28417)
* \[[`b744bd9dcb`](https://github.com/nodejs/node/commit/b744bd9dcb)] - **tools**: update eslint (Ruben Bridgewater) [#28173](https://github.com/nodejs/node/pull/28173)
* \[[`03e3ccdbe5`](https://github.com/nodejs/node/commit/03e3ccdbe5)] - **tools**: update remark-preset-lint-node to 1.7.0 (Rich Trott) [#28393](https://github.com/nodejs/node/pull/28393)
* \[[`619eb93942`](https://github.com/nodejs/node/commit/619eb93942)] - **tools**: fix typo in cache\_builder.cc (Daniel Bevenius) [#28418](https://github.com/nodejs/node/pull/28418)
* \[[`dd53e6aa7f`](https://github.com/nodejs/node/commit/dd53e6aa7f)] - **tools**: update babel-eslint to 10.0.2 (ZYSzys) [#28266](https://github.com/nodejs/node/pull/28266)
* \[[`e6c7ebe90c`](https://github.com/nodejs/node/commit/e6c7ebe90c)] - **vm**: increase code coverage of source\_text\_module.js (kball) [#28363](https://github.com/nodejs/node/pull/28363)
* \[[`2053dd0c9c`](https://github.com/nodejs/node/commit/2053dd0c9c)] - **worker**: only unref port for stdin if we ref’ed it before (Anna Henningsen) [#28153](https://github.com/nodejs/node/pull/28153)

<a id="12.5.0"></a>

## 2019-06-27, Version 12.5.0 (Current), @BridgeAR

### Notable changes

* **build**:
  * The startup time is reduced by enabling V8 snapshots by default
    [#28181](https://github.com/nodejs/node/pull/28181)
* **deps**:
  * Updated `V8` to 7.5.288.22 [#27375](https://github.com/nodejs/node/pull/27375)
    * The [numeric separator](https://v8.dev/features/numeric-separators) feature is now
      enabled by default
  * Updated `OpenSSL` to 1.1.1c [#28211](https://github.com/nodejs/node/pull/28211)
* **inspector**:
  * The `--inspect-publish-uid` flag was added to specify ways of the inspector
    web socket url exposure [#27741](https://github.com/nodejs/node/pull/27741)
* **n-api**:
  * Accessors on napi\_define\_\* are now ECMAScript-compliant
    [#27851](https://github.com/nodejs/node/pull/27851)
* **report**:
  * The cpu info got added to the report output
    [#28188](https://github.com/nodejs/node/pull/28188)
* **src**:
  * Restore the original state of the stdio file descriptors on exit to prevent
    leaving stdio in raw or non-blocking mode
    [#24260](https://github.com/nodejs/node/pull/24260)
* **tools,gyp**:
  * Introduce MSVS 2019 [#27375](https://github.com/nodejs/node/pull/27375)
* **util**:
  * **inspect**:
    * Array grouping became more compact and uses more columns than before
      [#28059](https://github.com/nodejs/node/pull/28059)
      [#28070](https://github.com/nodejs/node/pull/28070)
    * Long strings will not be split at 80 characters anymore. Instead they will
      be split on new lines [#28055](https://github.com/nodejs/node/pull/28055)
* **worker**:
  * `worker.terminate()` now returns a promise and using the callback is
    deprecated [#28021](https://github.com/nodejs/node/pull/28021)

### Commits

* \[[`f03241fc0a`](https://github.com/nodejs/node/commit/f03241fc0a)] - **(SEMVER-MINOR)** **assert**: add partial support for evaluated code in simple assert (Ruben Bridgewater) [#27781](https://github.com/nodejs/node/pull/27781)
* \[[`ef8f147b7e`](https://github.com/nodejs/node/commit/ef8f147b7e)] - **(SEMVER-MINOR)** **assert**: improve regular expression validation (Ruben Bridgewater) [#27781](https://github.com/nodejs/node/pull/27781)
* \[[`8157a50161`](https://github.com/nodejs/node/commit/8157a50161)] - **assert**: print more lines in the error diff (Ruben Bridgewater) [#28058](https://github.com/nodejs/node/pull/28058)
* \[[`82174412a5`](https://github.com/nodejs/node/commit/82174412a5)] - **assert**: fix error diff (Ruben Bridgewater) [#28058](https://github.com/nodejs/node/pull/28058)
* \[[`1ee7ce6092`](https://github.com/nodejs/node/commit/1ee7ce6092)] - **assert**: limit string inspection when logging assertion errors (Ruben Bridgewater) [#28058](https://github.com/nodejs/node/pull/28058)
* \[[`ddef3d0560`](https://github.com/nodejs/node/commit/ddef3d0560)] - **build**: fix cctest target for --without-report (Richard Lau) [#28238](https://github.com/nodejs/node/pull/28238)
* \[[`7cf79fa1c9`](https://github.com/nodejs/node/commit/7cf79fa1c9)] - **build**: guard test-doc recipe with node\_use\_openssl (Daniel Bevenius) [#28199](https://github.com/nodejs/node/pull/28199)
* \[[`32b0803ef3`](https://github.com/nodejs/node/commit/32b0803ef3)] - **build**: turn on custom V8 snapshot by default (Joyee Cheung) [#28181](https://github.com/nodejs/node/pull/28181)
* \[[`6a2d8e2579`](https://github.com/nodejs/node/commit/6a2d8e2579)] - **build**: unbreak --with-intl=system-icu build (Ben Noordhuis) [#28118](https://github.com/nodejs/node/pull/28118)
* \[[`eb89c06b95`](https://github.com/nodejs/node/commit/eb89c06b95)] - **build**: fix icu-i18n pkg-config version check (Ben Noordhuis) [#28118](https://github.com/nodejs/node/pull/28118)
* \[[`02fdf5c14c`](https://github.com/nodejs/node/commit/02fdf5c14c)] - **build**: don't swallow pkg-config warnings (Ben Noordhuis) [#28118](https://github.com/nodejs/node/pull/28118)
* \[[`48d7d7c53e`](https://github.com/nodejs/node/commit/48d7d7c53e)] - **build**: lint all docs under doc (Richard Lau) [#28128](https://github.com/nodejs/node/pull/28128)
* \[[`d3207912fb`](https://github.com/nodejs/node/commit/d3207912fb)] - **build**: fix configure script to work with Apple Clang 11 (Saagar Jha) [#28071](https://github.com/nodejs/node/pull/28071)
* \[[`21bcfb67f6`](https://github.com/nodejs/node/commit/21bcfb67f6)] - **(SEMVER-MINOR)** **build**: reset embedder string to "-node.0" (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`e5c26753e9`](https://github.com/nodejs/node/commit/e5c26753e9)] - **build,meta**: rearrange and narrow git ignore rules (Refael Ackermann) [#27954](https://github.com/nodejs/node/pull/27954)
* \[[`5101e4c2a2`](https://github.com/nodejs/node/commit/5101e4c2a2)] - **(SEMVER-MINOR)** **build,v8**: sync V8 gypfiles with 7.5 (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`5a7154ef32`](https://github.com/nodejs/node/commit/5a7154ef32)] - **build,win**: delegate lint-cpp to make (Refael Ackermann) [#28102](https://github.com/nodejs/node/pull/28102)
* \[[`3f1787b47d`](https://github.com/nodejs/node/commit/3f1787b47d)] - **crypto**: add debug info client emit secureConnect (Daniel Bevenius) [#28067](https://github.com/nodejs/node/pull/28067)
* \[[`9ea74b7bff`](https://github.com/nodejs/node/commit/9ea74b7bff)] - **deps**: update archs files for OpenSSL-1.1.1c (Sam Roberts) [#28211](https://github.com/nodejs/node/pull/28211)
* \[[`9c7ea2c9d9`](https://github.com/nodejs/node/commit/9c7ea2c9d9)] - **deps**: upgrade openssl sources to 1.1.1c (Sam Roberts) [#28211](https://github.com/nodejs/node/pull/28211)
* \[[`9419daf503`](https://github.com/nodejs/node/commit/9419daf503)] - **deps**: updated openssl upgrade instructions (Sam Roberts) [#28211](https://github.com/nodejs/node/pull/28211)
* \[[`084ffd8c2f`](https://github.com/nodejs/node/commit/084ffd8c2f)] - **deps**: update llhttp to 1.1.4 (Fedor Indutny) [#28154](https://github.com/nodejs/node/pull/28154)
* \[[`9382b3be9c`](https://github.com/nodejs/node/commit/9382b3be9c)] - **deps**: V8: cherry-pick e0a109c (Joyee Cheung) [#27533](https://github.com/nodejs/node/pull/27533)
* \[[`b690e19a9a`](https://github.com/nodejs/node/commit/b690e19a9a)] - **deps**: ignore deps/.cipd fetched by deps/v8/tools/node/fetch\_deps.py (Joyee Cheung) [#28095](https://github.com/nodejs/node/pull/28095)
* \[[`d42ad64253`](https://github.com/nodejs/node/commit/d42ad64253)] - **deps**: update node-inspect to v1.11.6 (Jan Krems) [#28039](https://github.com/nodejs/node/pull/28039)
* \[[`40a1a11542`](https://github.com/nodejs/node/commit/40a1a11542)] - **(SEMVER-MINOR)** **deps**: patch V8 to be API/ABI compatible with 7.4 (Michaël Zasso) [#28005](https://github.com/nodejs/node/pull/28005)
* \[[`ad3a164ec3`](https://github.com/nodejs/node/commit/ad3a164ec3)] - **(SEMVER-MINOR)** **deps**: bump minimum icu version to 64 (Michaël Zasso) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`e4aa869726`](https://github.com/nodejs/node/commit/e4aa869726)] - **(SEMVER-MINOR)** **deps**: V8: backport 3a75c1f (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`bb729a415a`](https://github.com/nodejs/node/commit/bb729a415a)] - **(SEMVER-MINOR)** **deps**: V8: fix BUILDING\_V8\_SHARED issues (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`f8a33abe0c`](https://github.com/nodejs/node/commit/f8a33abe0c)] - **(SEMVER-MINOR)** **deps**: V8: workaround for MSVC 14.20 optimizer bug (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`0a5ff4cb33`](https://github.com/nodejs/node/commit/0a5ff4cb33)] - **(SEMVER-MINOR)** **deps**: V8: template explicit instantiation for GCC-8 (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`b411114a52`](https://github.com/nodejs/node/commit/b411114a52)] - **(SEMVER-MINOR)** **deps**: V8: use ATOMIC\_VAR\_INIT instead of std::atomic\_init (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`c08d94baef`](https://github.com/nodejs/node/commit/c08d94baef)] - **(SEMVER-MINOR)** **deps**: V8: forward declaration of `Rtl*FunctionTable` (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`445bb81ab6`](https://github.com/nodejs/node/commit/445bb81ab6)] - **(SEMVER-MINOR)** **deps**: V8: patch register-arm64.h (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`fa6dfec186`](https://github.com/nodejs/node/commit/fa6dfec186)] - **(SEMVER-MINOR)** **deps**: V8: backport f89e555 (Michaël Zasso) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`8b8fe87e54`](https://github.com/nodejs/node/commit/8b8fe87e54)] - **deps**: V8: cherry-pick cca9ae3c9a (Benedikt Meurer) [#27729](https://github.com/nodejs/node/pull/27729)
* \[[`55e99448c8`](https://github.com/nodejs/node/commit/55e99448c8)] - **(SEMVER-MINOR)** **deps**: V8: update postmortem metadata generation script (cjihrig) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`2f92b15435`](https://github.com/nodejs/node/commit/2f92b15435)] - **(SEMVER-MINOR)** **deps**: V8: silence irrelevant warning (Michaël Zasso) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`ca8e5aa77b`](https://github.com/nodejs/node/commit/ca8e5aa77b)] - **(SEMVER-MINOR)** **deps**: V8: un-cherry-pick bd019bd (Refael Ackermann) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`4a61bdbc1f`](https://github.com/nodejs/node/commit/4a61bdbc1f)] - **(SEMVER-MINOR)** **deps**: V8: fix filename manipulation for Windows (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`86a8bb7612`](https://github.com/nodejs/node/commit/86a8bb7612)] - **(SEMVER-MINOR)** **deps**: update V8 to 7.5.288.22 (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`47366d7cc6`](https://github.com/nodejs/node/commit/47366d7cc6)] - **deps**: V8: extend workaround for MSVC optimizer bug (Michaël Zasso) [#28286](https://github.com/nodejs/node/pull/28286)
* \[[`071694472f`](https://github.com/nodejs/node/commit/071694472f)] - **dgram**: fix abort on bad args (cjihrig) [#28135](https://github.com/nodejs/node/pull/28135)
* \[[`8aeb9cc10f`](https://github.com/nodejs/node/commit/8aeb9cc10f)] - **doc**: revise intro sentence for assert (Rich Trott) [#28226](https://github.com/nodejs/node/pull/28226)
* \[[`60156274b1`](https://github.com/nodejs/node/commit/60156274b1)] - **doc**: improve assert strict-mode text (Rich Trott) [#28239](https://github.com/nodejs/node/pull/28239)
* \[[`39b10abf63`](https://github.com/nodejs/node/commit/39b10abf63)] - **doc**: clarify commit message format in pull-requests.md (rexagod) [#28125](https://github.com/nodejs/node/pull/28125)
* \[[`dba5983b00`](https://github.com/nodejs/node/commit/dba5983b00)] - **doc**: add missing options allowed in NODE\_OPTIONS (Richard Lau) [#28179](https://github.com/nodejs/node/pull/28179)
* \[[`4cadddc6a7`](https://github.com/nodejs/node/commit/4cadddc6a7)] - **doc**: document behavior of family:0 in dns.lookup() (cjihrig) [#28163](https://github.com/nodejs/node/pull/28163)
* \[[`694faf13fb`](https://github.com/nodejs/node/commit/694faf13fb)] - **doc**: pass path in URL constructor (Daniel Nalborczyk) [#28161](https://github.com/nodejs/node/pull/28161)
* \[[`81a1a13efd`](https://github.com/nodejs/node/commit/81a1a13efd)] - **doc**: update kernel and glibc reqs for PPCle (Michael Dawson) [#28162](https://github.com/nodejs/node/pull/28162)
* \[[`abe5d05523`](https://github.com/nodejs/node/commit/abe5d05523)] - **(SEMVER-MINOR)** **doc**: update assert's validation functions (Ruben Bridgewater) [#27781](https://github.com/nodejs/node/pull/27781)
* \[[`ecb963dd44`](https://github.com/nodejs/node/commit/ecb963dd44)] - **doc**: document trace-events category for dns requests (vmarchaud) [#28100](https://github.com/nodejs/node/pull/28100)
* \[[`8c277555b7`](https://github.com/nodejs/node/commit/8c277555b7)] - **doc**: add Buffer#subarray() and add note about Uint8Array#slice() (FUJI Goro (gfx)) [#28101](https://github.com/nodejs/node/pull/28101)
* \[[`4d6262fb56`](https://github.com/nodejs/node/commit/4d6262fb56)] - **doc**: update broken/foundation links in README.md (Tierney Cyren) [#28119](https://github.com/nodejs/node/pull/28119)
* \[[`00e6c9d2dd`](https://github.com/nodejs/node/commit/00e6c9d2dd)] - **doc**: add tls-min/max options to NODE\_OPTIONS (Daniel Bevenius) [#28146](https://github.com/nodejs/node/pull/28146)
* \[[`705f259142`](https://github.com/nodejs/node/commit/705f259142)] - **doc**: split example into two (Ruben Bridgewater) [#27670](https://github.com/nodejs/node/pull/27670)
* \[[`69af43ead9`](https://github.com/nodejs/node/commit/69af43ead9)] - **doc**: clarify N-API version Matrix (Michael Dawson) [#27942](https://github.com/nodejs/node/pull/27942)
* \[[`56b150b3d7`](https://github.com/nodejs/node/commit/56b150b3d7)] - **doc**: add current recommendation for ESM/CommonJS dual packages (Geoffrey Booth) [#27957](https://github.com/nodejs/node/pull/27957)
* \[[`360c708f64`](https://github.com/nodejs/node/commit/360c708f64)] - **doc**: document Http2Stream#id property (murgatroid99) [#28074](https://github.com/nodejs/node/pull/28074)
* \[[`5fc4e48fdd`](https://github.com/nodejs/node/commit/5fc4e48fdd)] - **doc**: add note about AsyncResource for Worker pooling (Anna Henningsen) [#28023](https://github.com/nodejs/node/pull/28023)
* \[[`d1c53fc54e`](https://github.com/nodejs/node/commit/d1c53fc54e)] - **doc**: fix `prohibited-strings` warning in `pull-requests.md` (rexagod) [#28127](https://github.com/nodejs/node/pull/28127)
* \[[`e6ecc13cfa`](https://github.com/nodejs/node/commit/e6ecc13cfa)] - **doc**: improve synopsis.md (Rich Trott) [#28115](https://github.com/nodejs/node/pull/28115)
* \[[`eb05db907a`](https://github.com/nodejs/node/commit/eb05db907a)] - **doc**: edit reason-for-deprecation text (Rich Trott) [#28098](https://github.com/nodejs/node/pull/28098)
* \[[`5ad0d047c6`](https://github.com/nodejs/node/commit/5ad0d047c6)] - **doc**: improve DEP0090 text (Rich Trott) [#28097](https://github.com/nodejs/node/pull/28097)
* \[[`9074f9b4e5`](https://github.com/nodejs/node/commit/9074f9b4e5)] - **doc**: clarify special schemes (Rich Trott) [#28091](https://github.com/nodejs/node/pull/28091)
* \[[`f95a52cb1e`](https://github.com/nodejs/node/commit/f95a52cb1e)] - **doc**: clarify weak keys text (Rich Trott) [#28090](https://github.com/nodejs/node/pull/28090)
* \[[`eb73ed8158`](https://github.com/nodejs/node/commit/eb73ed8158)] - **doc**: remove superfluous filenaming convention (Rich Trott) [#28089](https://github.com/nodejs/node/pull/28089)
* \[[`f7d8384af2`](https://github.com/nodejs/node/commit/f7d8384af2)] - **doc**: mark Node.js 11 as EOL in changelog (Richard Lau) [#28076](https://github.com/nodejs/node/pull/28076)
* \[[`87c55ea0ef`](https://github.com/nodejs/node/commit/87c55ea0ef)] - **doc**: adjust TOC margins (Roman Reiss) [#28075](https://github.com/nodejs/node/pull/28075)
* \[[`9dd4813008`](https://github.com/nodejs/node/commit/9dd4813008)] - **doc**: order deprecation reasons (Trivikram Kamat) [#27960](https://github.com/nodejs/node/pull/27960)
* \[[`e3f905ac7e`](https://github.com/nodejs/node/commit/e3f905ac7e)] - **doc**: remove "encouraged" as hedging in fs.md (Rich Trott) [#28027](https://github.com/nodejs/node/pull/28027)
* \[[`df22b96cb0`](https://github.com/nodejs/node/commit/df22b96cb0)] - **doc**: remove "strongly recommended" as hedging in fs.md (Rich Trott) [#28028](https://github.com/nodejs/node/pull/28028)
* \[[`049429bd97`](https://github.com/nodejs/node/commit/049429bd97)] - **doc**: remove "strongly recommended" hedging from tls.md (Rich Trott) [#28029](https://github.com/nodejs/node/pull/28029)
* \[[`79d4f285be`](https://github.com/nodejs/node/commit/79d4f285be)] - **doc**: remove "strongly recommended" hedging in deprecations.md (Rich Trott) [#28031](https://github.com/nodejs/node/pull/28031)
* \[[`613064699e`](https://github.com/nodejs/node/commit/613064699e)] - **doc,n-api**: fix typo (Richard Lau) [#28178](https://github.com/nodejs/node/pull/28178)
* \[[`5ee6ecd979`](https://github.com/nodejs/node/commit/5ee6ecd979)] - **doc,test**: test documentation consistency for NODE\_OPTIONS (Richard Lau) [#28179](https://github.com/nodejs/node/pull/28179)
* \[[`e1fc9b987a`](https://github.com/nodejs/node/commit/e1fc9b987a)] - **http2**: do not register unnecessary listeners (Antonio Kukas) [#27987](https://github.com/nodejs/node/pull/27987)
* \[[`faeed804c7`](https://github.com/nodejs/node/commit/faeed804c7)] - **https**: do not automatically use invalid servername (Sam Roberts) [#28209](https://github.com/nodejs/node/pull/28209)
* \[[`f8c9a58bf5`](https://github.com/nodejs/node/commit/f8c9a58bf5)] - **inspector**: added --inspect-publish-uid (Aleksei Koziatinskii) [#27741](https://github.com/nodejs/node/pull/27741)
* \[[`9b248e33de`](https://github.com/nodejs/node/commit/9b248e33de)] - **module**: prevent race condition while combining import and require (Ruben Bridgewater) [#27674](https://github.com/nodejs/node/pull/27674)
* \[[`6014429580`](https://github.com/nodejs/node/commit/6014429580)] - **module**: handle empty require.resolve() options (cjihrig) [#28078](https://github.com/nodejs/node/pull/28078)
* \[[`9c19c4b6a3`](https://github.com/nodejs/node/commit/9c19c4b6a3)] - **n-api**: define ECMAScript-compliant accessors on napi\_define\_class (legendecas) [#27851](https://github.com/nodejs/node/pull/27851)
* \[[`b60287d188`](https://github.com/nodejs/node/commit/b60287d188)] - **n-api**: define ECMAScript-compliant accessors on napi\_define\_properties (legendecas) [#27851](https://github.com/nodejs/node/pull/27851)
* \[[`a40cfb32d2`](https://github.com/nodejs/node/commit/a40cfb32d2)] - **n-api**: defer Buffer finalizer with SetImmediate (Anna Henningsen) [#28082](https://github.com/nodejs/node/pull/28082)
* \[[`dfbbfbb765`](https://github.com/nodejs/node/commit/dfbbfbb765)] - **net**: make writeAfterFIN() return false (Luigi Pinca) [#27996](https://github.com/nodejs/node/pull/27996)
* \[[`2515df029a`](https://github.com/nodejs/node/commit/2515df029a)] - **perf\_hooks,trace\_events**: use stricter equality (cjihrig) [#28166](https://github.com/nodejs/node/pull/28166)
* \[[`43fa824a3b`](https://github.com/nodejs/node/commit/43fa824a3b)] - **process**: refactor unhandled rejection handling (Joyee Cheung) [#28228](https://github.com/nodejs/node/pull/28228)
* \[[`b491eabff1`](https://github.com/nodejs/node/commit/b491eabff1)] - **process**: improve queueMicrotask performance (Anatoli Papirovski) [#28093](https://github.com/nodejs/node/pull/28093)
* \[[`460cc6285a`](https://github.com/nodejs/node/commit/460cc6285a)] - **process**: code cleanup for nextTick (Anatoli Papirovski) [#28047](https://github.com/nodejs/node/pull/28047)
* \[[`4eaac83c5f`](https://github.com/nodejs/node/commit/4eaac83c5f)] - **report**: add cpu info to report output (Christopher Hiller) [#28188](https://github.com/nodejs/node/pull/28188)
* \[[`029b50dab4`](https://github.com/nodejs/node/commit/029b50dab4)] - **src**: fix compiler warning in node\_worker.cc (Daniel Bevenius) [#28198](https://github.com/nodejs/node/pull/28198)
* \[[`a5998152d5`](https://github.com/nodejs/node/commit/a5998152d5)] - **src**: fix off-by-one error in native SetImmediate (Anna Henningsen) [#28082](https://github.com/nodejs/node/pull/28082)
* \[[`c67642ae03`](https://github.com/nodejs/node/commit/c67642ae03)] - **src**: do not use pointer for loop in node\_watchdog (Anna Henningsen) [#28020](https://github.com/nodejs/node/pull/28020)
* \[[`b5dda32b8a`](https://github.com/nodejs/node/commit/b5dda32b8a)] - **src**: restore stdio on program exit (Ben Noordhuis) [#24260](https://github.com/nodejs/node/pull/24260)
* \[[`8984b73033`](https://github.com/nodejs/node/commit/8984b73033)] - **src**: remove TLS code for unsupported OpenSSLs (Sam Roberts) [#28085](https://github.com/nodejs/node/pull/28085)
* \[[`8849eb24c1`](https://github.com/nodejs/node/commit/8849eb24c1)] - **src**: handle exceptions from ToDetailString() (Anna Henningsen) [#28019](https://github.com/nodejs/node/pull/28019)
* \[[`8a032fc50c`](https://github.com/nodejs/node/commit/8a032fc50c)] - **src**: expose DOMException to internalBinding('message') for testing (Joyee Cheung) [#28072](https://github.com/nodejs/node/pull/28072)
* \[[`a5fdedb3d5`](https://github.com/nodejs/node/commit/a5fdedb3d5)] - **src**: only run preloadModules if the preload array is not empty (Samuel Attard) [#28012](https://github.com/nodejs/node/pull/28012)
* \[[`c821eefa5f`](https://github.com/nodejs/node/commit/c821eefa5f)] - **src**: add napi\_define\_class() null checks (Octavian Soldea) [#27945](https://github.com/nodejs/node/pull/27945)
* \[[`95ee3b55d3`](https://github.com/nodejs/node/commit/95ee3b55d3)] - **src**: use RAII in setgroups implementation (Anna Henningsen) [#28022](https://github.com/nodejs/node/pull/28022)
* \[[`d81c67bd8f`](https://github.com/nodejs/node/commit/d81c67bd8f)] - **src**: fix unused private field warning (cjihrig) [#28036](https://github.com/nodejs/node/pull/28036)
* \[[`e8bedd2009`](https://github.com/nodejs/node/commit/e8bedd2009)] - **src**: split `RunBootstrapping()` (Joyee Cheung) [#27539](https://github.com/nodejs/node/pull/27539)
* \[[`c20c6e55b5`](https://github.com/nodejs/node/commit/c20c6e55b5)] - **src**: reorganize inspector and diagnostics initialization (Joyee Cheung) [#27539](https://github.com/nodejs/node/pull/27539)
* \[[`c086736a49`](https://github.com/nodejs/node/commit/c086736a49)] - **src**: create Environment properties in Environment::CreateProperties() (Joyee Cheung) [#27539](https://github.com/nodejs/node/pull/27539)
* \[[`70f8e71a0d`](https://github.com/nodejs/node/commit/70f8e71a0d)] - **src**: inline ProcessCliArgs in the Environment constructor (Joyee Cheung) [#27539](https://github.com/nodejs/node/pull/27539)
* \[[`174b3c4b1b`](https://github.com/nodejs/node/commit/174b3c4b1b)] - **test**: add eval ESM module tests (Evgenii Shchepotev) [#27956](https://github.com/nodejs/node/pull/27956)
* \[[`aa3c41fe40`](https://github.com/nodejs/node/commit/aa3c41fe40)] - **test**: fix NODE\_OPTIONS feature check (Richard Lau) [#28225](https://github.com/nodejs/node/pull/28225)
* \[[`9edf69545d`](https://github.com/nodejs/node/commit/9edf69545d)] - **test**: move --cpu-prof tests to sequential (Joyee Cheung) [#28210](https://github.com/nodejs/node/pull/28210)
* \[[`df9b253e2c`](https://github.com/nodejs/node/commit/df9b253e2c)] - **test**: skip `test-worker-prof` as flaky for all (Milad Farazmand) [#28175](https://github.com/nodejs/node/pull/28175)
* \[[`bd16f9b2da`](https://github.com/nodejs/node/commit/bd16f9b2da)] - **test**: remove FIB environment variable from cpu-prof.js (Rich Trott) [#28183](https://github.com/nodejs/node/pull/28183)
* \[[`a3f8385d7f`](https://github.com/nodejs/node/commit/a3f8385d7f)] - **test**: remove unused `output` argument for `getFrames()` (Rich Trott) [#28183](https://github.com/nodejs/node/pull/28183)
* \[[`58eccb1213`](https://github.com/nodejs/node/commit/58eccb1213)] - **test**: document cpu-prof module (Rich Trott) [#28183](https://github.com/nodejs/node/pull/28183)
* \[[`318328f6b7`](https://github.com/nodejs/node/commit/318328f6b7)] - **test**: improve unexpected warnings error (Ruben Bridgewater) [#28138](https://github.com/nodejs/node/pull/28138)
* \[[`31ccd36668`](https://github.com/nodejs/node/commit/31ccd36668)] - **test**: mark test-fs-stat-bigint as flaky (Rich Trott) [#28156](https://github.com/nodejs/node/pull/28156)
* \[[`de6627f9a4`](https://github.com/nodejs/node/commit/de6627f9a4)] - **test**: remove duplicate test-child-process-execfilesync-maxBuffer.js (Joyee Cheung) [#28139](https://github.com/nodejs/node/pull/28139)
* \[[`2353c63dc2`](https://github.com/nodejs/node/commit/2353c63dc2)] - **test**: split test-cpu-prof.js (Joyee Cheung) [#28170](https://github.com/nodejs/node/pull/28170)
* \[[`186e94c322`](https://github.com/nodejs/node/commit/186e94c322)] - **test**: add github refs to flaky tests (Sam Roberts) [#28123](https://github.com/nodejs/node/pull/28123)
* \[[`d8061dc1a0`](https://github.com/nodejs/node/commit/d8061dc1a0)] - **test**: remove test-gc-http-client from status file (Rich Trott) [#28130](https://github.com/nodejs/node/pull/28130)
* \[[`d6791d1cb8`](https://github.com/nodejs/node/commit/d6791d1cb8)] - **test**: remove test-tty-wrap from status file (Rich Trott) [#28129](https://github.com/nodejs/node/pull/28129)
* \[[`b0bc23c572`](https://github.com/nodejs/node/commit/b0bc23c572)] - **test**: add comments to the foaf+ssl fixtures (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`189d6af2b3`](https://github.com/nodejs/node/commit/189d6af2b3)] - **test**: change formatting of fixtures/keys/Makefile (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`94a6d7a518`](https://github.com/nodejs/node/commit/94a6d7a518)] - **test**: change fixtures.readSync to fixtures.readKey (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`c82023a173`](https://github.com/nodejs/node/commit/c82023a173)] - **test**: remove uneeded agent keypair in fixtures/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`74e6109f39`](https://github.com/nodejs/node/commit/74e6109f39)] - **test**: move foafssl certs to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`78f39c91ac`](https://github.com/nodejs/node/commit/78f39c91ac)] - **test**: remove uneeded alice certs in fixtures/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`5d0737735b`](https://github.com/nodejs/node/commit/5d0737735b)] - **test**: remove uneeded certs in fixtures/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`d757e0b6d4`](https://github.com/nodejs/node/commit/d757e0b6d4)] - **test**: move dherror.pem to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`f9ddcc6305`](https://github.com/nodejs/node/commi/f9ddcc6305)] - **test**: remove pass-\* certs (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`e673b57055`](https://github.com/nodejs/node/commit/e673b57055)] - **test**: move test\_\[key|ca|cert] to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`8670f6dd22`](https://github.com/nodejs/node/commit/8670f6dd22)] - **test**: move spkac certs to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`7d1f15fba1`](https://github.com/nodejs/node/commit/7d1f15fba1)] - **test**: move x448 keypairs to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`22bbdc5068`](https://github.com/nodejs/node/commit/22bbdc5068)] - **test**: move ed448 keypairs to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`9de9d55bfc`](https://github.com/nodejs/node/commit/9de9d55bfc)] - **test**: move dsa keypairs to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`9684842023`](https://github.com/nodejs/node/commit/9684842023)] - **test**: move rsa keypairs to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`7ae23abc01`](https://github.com/nodejs/node/commit/7ae23abc01)] - **test**: move x25519 keypair to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`adb0197d6d`](https://github.com/nodejs/node/commit/adb0197d6d)] - **test**: move ed25519 keypair to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`14bd26c8da`](https://github.com/nodejs/node/commit/14bd26c8da)] - **test**: remove workaround for unsupported OpenSSLs (Sam Roberts) [#28085](https://github.com/nodejs/node/pull/28085)
* \[[`d4bb88eed8`](https://github.com/nodejs/node/commit/d4bb88eed8)] - **test**: simplify tests code (himself65) [#28065](https://github.com/nodejs/node/pull/28065)
* \[[`87e977ae42`](https://github.com/nodejs/node/commit/87e977ae42)] - **test**: make sure vtable is generated in addon test with LTO (Anna Henningsen) [#28057](https://github.com/nodejs/node/pull/28057)
* \[[`3feaf3ddbe`](https://github.com/nodejs/node/commit/3feaf3ddbe)] - **test**: mark test-worker-debug as flaky (Refael Ackermann) [#28035](https://github.com/nodejs/node/pull/28035)
* \[[`098cf74292`](https://github.com/nodejs/node/commit/098cf74292)] - **test**: regression test `tmpdir` (Refael Ackermann) [#28035](https://github.com/nodejs/node/pull/28035)
* \[[`e08a98fa43`](https://github.com/nodejs/node/commit/e08a98fa43)] - **test**: always suffix `tmpdir` (Refael Ackermann) [#28035](https://github.com/nodejs/node/pull/28035)
* \[[`f33623662f`](https://github.com/nodejs/node/commit/f33623662f)] - **test**: shell out to `rmdir` first on Windows (Refael Ackermann) [#28035](https://github.com/nodejs/node/pull/28035)
* \[[`1ef2811236`](https://github.com/nodejs/node/commit/1ef2811236)] - **test**: only assert on first lines of TLS trace (Sam Roberts) [#28043](https://github.com/nodejs/node/pull/28043)
* \[[`62de36e8d3`](https://github.com/nodejs/node/commit/62de36e8d3)] - _**Revert**_ "**test**: move all test keys/certs under `test/fixtures/keys/`" (Sam Roberts) [#28083](https://github.com/nodejs/node/pull/28083)
* \[[`2331e9c380`](https://github.com/nodejs/node/commit/2331e9c380)] - **test**: add comments to the foaf+ssl fixtures (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`8e28259bf8`](https://github.com/nodejs/node/commit/8e28259bf8)] - **test**: change formatting of fixtures/keys/Makefile (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`d258504a31`](https://github.com/nodejs/node/commit/d258504a31)] - **test**: change fixtures.readSync to fixtures.readKey (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`328b2d0c88`](https://github.com/nodejs/node/commit/328b2d0c88)] - **test**: remove uneeded agent keypair in fixtures/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`a0d2862b1e`](https://github.com/nodejs/node/commit/a0d2862b1e)] - **test**: move foafssl certs to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`af9eb9648e`](https://github.com/nodejs/node/commit/af9eb9648e)] - **test**: remove uneeded alice certs in fixtures/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`ee62fa172c`](https://github.com/nodejs/node/commit/ee62fa172c)] - **test**: remove uneeded certs in fixtures/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`f41dfd71a0`](https://github.com/nodejs/node/commit/f41dfd71a0)] - **test**: move dherror.pem to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`98f7ae9e7b`](https://github.com/nodejs/node/commit/98f7ae9e7b)] - **test**: remove pass-\* certs (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`27d6b28943`](https://github.com/nodejs/node/commit/27d6b28943)] - **test**: move test\_\[key|ca|cert] to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`5ac6dddb83`](https://github.com/nodejs/node/commit/5ac6dddb83)] - **test**: move spkac certs to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`03b92e93b1`](https://github.com/nodejs/node/commit/03b92e93b1)] - **test**: move x448 keypairs to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`4155bbaeab`](https://github.com/nodejs/node/commit/4155bbaeab)] - **test**: move ed448 keypairs to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`9209698139`](https://github.com/nodejs/node/commit/9209698139)] - **test**: move dsa keypairs to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`ad42258d5a`](https://github.com/nodejs/node/commit/ad42258d5a)] - **test**: move rsa keypairs to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`686cb13f78`](https://github.com/nodejs/node/commit/686cb13f78)] - **test**: move x25519 keypair to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`1f2de2fbe1`](https://github.com/nodejs/node/commit/1f2de2fbe1)] - **test**: move ed25519 keypair to fixtures/keys/ (Alex Aubuchon) [#27962](https://github.com/nodejs/node/pull/27962)
* \[[`687e57fe19`](https://github.com/nodejs/node/commit/687e57fe19)] - **test**: rename worker MessagePort test (Anna Henningsen) [#28024](https://github.com/nodejs/node/pull/28024)
* \[[`7165254f8b`](https://github.com/nodejs/node/commit/7165254f8b)] - **test**: more tls hostname verification coverage (Ben Noordhuis) [#27999](https://github.com/nodejs/node/pull/27999)
* \[[`92d1ca9645`](https://github.com/nodejs/node/commit/92d1ca9645)] - **(SEMVER-MINOR)** **test**: fail `test-worker-prof` on internal timeout (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`ab1a4eb12d`](https://github.com/nodejs/node/commit/ab1a4eb12d)] - **(SEMVER-MINOR)** **test**: drain platform before unregistering isolate (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`b6bdf752d6`](https://github.com/nodejs/node/commit/b6bdf752d6)] - **test,v8**: skip less and stabilize test-linux-perf.js (Refael Ackermann) [#27364](https://github.com/nodejs/node/pull/27364)
* \[[`7044a7a302`](https://github.com/nodejs/node/commit/7044a7a302)] - **tls**: remove unnecessary set of DEFAULT\_MAX\_VERSION (Daniel Bevenius) [#28147](https://github.com/nodejs/node/pull/28147)
* \[[`6b9d477520`](https://github.com/nodejs/node/commit/6b9d477520)] - **tls**: rename validateKeyCert in \_tls\_common.js (Daniel Bevenius) [#28116](https://github.com/nodejs/node/pull/28116)
* \[[`aeda0c3d35`](https://github.com/nodejs/node/commit/aeda0c3d35)] - **tools**: assert that the snapshot can be rehashed in node\_mksnapshot (Joyee Cheung) [#28181](https://github.com/nodejs/node/pull/28181)
* \[[`a0c5b58b44`](https://github.com/nodejs/node/commit/a0c5b58b44)] - **tools**: fix update-babel-eslint.sh script (RubenBridgewater) [#27670](https://github.com/nodejs/node/pull/27670)
* \[[`6460d071d2`](https://github.com/nodejs/node/commit/6460d071d2)] - **tools**: increase the maximum number of files to lint per worker (Ruben Bridgewater) [#27670](https://github.com/nodejs/node/pull/27670)
* \[[`bf76823a47`](https://github.com/nodejs/node/commit/bf76823a47)] - **tools**: ignore node\_modules when linting (Ruben Bridgewater) [#27670](https://github.com/nodejs/node/pull/27670)
* \[[`52564dbb28`](https://github.com/nodejs/node/commit/52564dbb28)] - **tools**: update babel-eslint (Ruben Bridgewater) [#27670](https://github.com/nodejs/node/pull/27670)
* \[[`9be67b2cbc`](https://github.com/nodejs/node/commit/9be67b2cbc)] - **tools**: activate more eslint rules (Ruben Bridgewater) [#27670](https://github.com/nodejs/node/pull/27670)
* \[[`739c2a3336`](https://github.com/nodejs/node/commit/739c2a3336)] - **tools**: update eslint config (Ruben Bridgewater) [#27670](https://github.com/nodejs/node/pull/27670)
* \[[`adfbd362fc`](https://github.com/nodejs/node/commit/adfbd362fc)] - **tools**: update eslint (Ruben Bridgewater) [#27670](https://github.com/nodejs/node/pull/27670)
* \[[`018159d734`](https://github.com/nodejs/node/commit/018159d734)] - **(SEMVER-MINOR)** **tools,gyp**: introduce MSVS 2019 (Refael Ackermann) [#27375](https://github.com/nodejs/node/pull/27375)
* \[[`9dd840dff5`](https://github.com/nodejs/node/commit/9dd840dff5)] - **trace\_events**: respect inspect() depth (cjihrig) [#28037](https://github.com/nodejs/node/pull/28037)
* \[[`b6f113bc15`](https://github.com/nodejs/node/commit/b6f113bc15)] - **util**: use average bias while grouping arrays (Ruben Bridgewater) [#28070](https://github.com/nodejs/node/pull/28070)
* \[[`7e617606b0`](https://github.com/nodejs/node/commit/7e617606b0)] - **util**: improve .inspect() array grouping (Ruben Bridgewater) [#28070](https://github.com/nodejs/node/pull/28070)
* \[[`9ced334a6c`](https://github.com/nodejs/node/commit/9ced334a6c)] - **util**: refactor inspecting long lines (Ruben Bridgewater) [#28055](https://github.com/nodejs/node/pull/28055)
* \[[`dfdf742fd1`](https://github.com/nodejs/node/commit/dfdf742fd1)] - **util**: use Set to store deprecation codes (Daniel Nalborczyk) [#28113](https://github.com/nodejs/node/pull/28113)
* \[[`c3243de47a`](https://github.com/nodejs/node/commit/c3243de47a)] - **util**: special handle `maxArrayLength` while grouping arrays (Ruben Bridgewater) [#28059](https://github.com/nodejs/node/pull/28059)
* \[[`f897860427`](https://github.com/nodejs/node/commit/f897860427)] - **util**: support AsyncGeneratorFunction in .inspect (Ruben Bridgewater) [#28056](https://github.com/nodejs/node/pull/28056)
* \[[`d659ed6dbe`](https://github.com/nodejs/node/commit/d659ed6dbe)] - **worker**: refactor `worker.terminate()` (Anna Henningsen) [#28021](https://github.com/nodejs/node/pull/28021)
* \[[`303a9a3d06`](https://github.com/nodejs/node/commit/303a9a3d06)] - **worker**: make MessagePort constructor non-callable (Anna Henningsen) [#28032](https://github.com/nodejs/node/pull/28032)
* \[[`79a8cd0dec`](https://github.com/nodejs/node/commit/79a8cd0dec)] - **worker**: add typechecking for postMessage transfer list (Anna Henningsen) [#28033](https://github.com/nodejs/node/pull/28033)
* \[[`d7641d833c`](https://github.com/nodejs/node/commit/d7641d833c)] - **worker**: use DataCloneError for unknown native objects (Anna Henningsen) [#28025](https://github.com/nodejs/node/pull/28025)

<a id="12.4.0"></a>

## 2019-06-04, Version 12.4.0 (Current), @targos

### Notable changes

* **doc**:
  * The JSON variant of the API documentation is no longer experimental (Rich Trott) [#27842](https://github.com/nodejs/node/pull/27842).
* **esm**:
  * JSON module support is always enabled under `--experimental-modules`. The
    `--experimental-json-modules` flag has been removed (Myles Borins) [#27752](https://github.com/nodejs/node/pull/27752).
* **http,http2**:
  * A new flag has been added for overriding the default HTTP server socket
    timeout (which is two minutes). Pass `--http-server-default-timeout=milliseconds`
    or `--http-server-default-timeout=0` to respectively change or disable the timeout.
    Starting with Node.js 13.0.0, the timeout will be disabled by default (Ali Ijaz Sheikh) [#27704](https://github.com/nodejs/node/pull/27704).
* **inspector**:
  * Added an experimental `--heap-prof` flag to start the V8 heap profiler
    on startup and write the heap profile to disk before exit (Joyee Cheung) [#27596](https://github.com/nodejs/node/pull/27596).
* **stream**:
  * The `readable.unshift()` method now correctly converts strings to buffers.
    Additionally, a new optional argument is accepted to specify the string's
    encoding, such as `'utf8'` or `'ascii'` (Marcos Casagrande) [#27194](https://github.com/nodejs/node/pull/27194).
* **v8**:
  * The object returned by `v8.getHeapStatistics()` has two new properties:
    `number_of_native_contexts` and `number_of_detached_contexts` (Yuriy Vasiyarov) [#27933](https://github.com/nodejs/node/pull/27933).

### Commits

* \[[`5bbc6d79c3`](https://github.com/nodejs/node/commit/5bbc6d79c3)] - **assert**: remove unreachable code (Rich Trott) [#27840](https://github.com/nodejs/node/pull/27840)
* \[[`530e63a4eb`](https://github.com/nodejs/node/commit/530e63a4eb)] - **assert**: remove unreachable code (Rich Trott) [#27786](https://github.com/nodejs/node/pull/27786)
* \[[`9b08c458be`](https://github.com/nodejs/node/commit/9b08c458be)] - **build,aix**: link with `noerrmsg` to eliminate warnings (Refael Ackermann) [#27773](https://github.com/nodejs/node/pull/27773)
* \[[`08b0ca9645`](https://github.com/nodejs/node/commit/08b0ca9645)] - **build,win**: create junction instead of symlink to `out\%config%` (Refael Ackermann) [#27736](https://github.com/nodejs/node/pull/27736)
* \[[`ea2d550507`](https://github.com/nodejs/node/commit/ea2d550507)] - **child\_process**: move exports to bottom for consistent code style (himself65) [#27845](https://github.com/nodejs/node/pull/27845)
* \[[`a9f95572c3`](https://github.com/nodejs/node/commit/a9f95572c3)] - **child\_process**: remove extra shallow copy (zero1five) [#27801](https://github.com/nodejs/node/pull/27801)
* \[[`449ee8dd42`](https://github.com/nodejs/node/commit/449ee8dd42)] - **console**: fix table() output (Brian White) [#27917](https://github.com/nodejs/node/pull/27917)
* \[[`9220a68a76`](https://github.com/nodejs/node/commit/9220a68a76)] - **crypto**: fix KeyObject handle type error message (Alexander Avakov) [#27904](https://github.com/nodejs/node/pull/27904)
* \[[`3b6424fa29`](https://github.com/nodejs/node/commit/3b6424fa29)] - **deps**: histogram: unexport symbols (Ben Noordhuis) [#27779](https://github.com/nodejs/node/pull/27779)
* \[[`ef25ac5223`](https://github.com/nodejs/node/commit/ef25ac5223)] - **doc**: clarify wording in modules.md (Alex Temny) [#27853](https://github.com/nodejs/node/pull/27853)
* \[[`c683cd99d7`](https://github.com/nodejs/node/commit/c683cd99d7)] - **doc**: improve explanation for directory with fs.rename() (Rich Trott) [#27963](https://github.com/nodejs/node/pull/27963)
* \[[`70b485478c`](https://github.com/nodejs/node/commit/70b485478c)] - **doc**: fix the wrong name of AssertionError (Kyle Zhang) [#27982](https://github.com/nodejs/node/pull/27982)
* \[[`11c3ddb4cb`](https://github.com/nodejs/node/commit/11c3ddb4cb)] - **doc**: simplify system call material in doc overview (Rich Trott) [#27966](https://github.com/nodejs/node/pull/27966)
* \[[`c56640138a`](https://github.com/nodejs/node/commit/c56640138a)] - **doc**: warn about relying on fs gc close behavior (Benjamin Gruenbaum) [#27972](https://github.com/nodejs/node/pull/27972)
* \[[`bab9f5a891`](https://github.com/nodejs/node/commit/bab9f5a891)] - **doc**: add information to revoked deprecations (cjihrig) [#27952](https://github.com/nodejs/node/pull/27952)
* \[[`f4fc75d245`](https://github.com/nodejs/node/commit/f4fc75d245)] - **doc**: add missing status to DEP0121 (cjihrig) [#27950](https://github.com/nodejs/node/pull/27950)
* \[[`77ff597faa`](https://github.com/nodejs/node/commit/77ff597faa)] - **doc**: add missing --experimental-wasm-modules docs (cjihrig) [#27948](https://github.com/nodejs/node/pull/27948)
* \[[`6ca4f03ccf`](https://github.com/nodejs/node/commit/6ca4f03ccf)] - **doc**: revise additional Experimental status text (Rich Trott) [#27931](https://github.com/nodejs/node/pull/27931)
* \[[`a1788de0a4`](https://github.com/nodejs/node/commit/a1788de0a4)] - **doc**: adds link to nightly code coverage report (Tariq Ramlall) [#27922](https://github.com/nodejs/node/pull/27922)
* \[[`b7cd0de145`](https://github.com/nodejs/node/commit/b7cd0de145)] - **doc**: fix typo in pipe from async iterator example (Luigi Pinca) [#27870](https://github.com/nodejs/node/pull/27870)
* \[[`f621b8f178`](https://github.com/nodejs/node/commit/f621b8f178)] - **doc**: reword Experimental stability index (Rich Trott) [#27879](https://github.com/nodejs/node/pull/27879)
* \[[`7a7fc4e7e6`](https://github.com/nodejs/node/commit/7a7fc4e7e6)] - **doc**: update n-api support matrix (teams2ua) [#27567](https://github.com/nodejs/node/pull/27567)
* \[[`9d9b32eff5`](https://github.com/nodejs/node/commit/9d9b32eff5)] - **doc**: fix for OutgoingMessage.prototype.\_headers/\_headerNames (Daniel Nalborczyk) [#27574](https://github.com/nodejs/node/pull/27574)
* \[[`263e53317b`](https://github.com/nodejs/node/commit/263e53317b)] - **doc**: reposition "How to Contribute" README section (Anish Asrani) [#27811](https://github.com/nodejs/node/pull/27811)
* \[[`85f505c292`](https://github.com/nodejs/node/commit/85f505c292)] - **doc**: add version info for types (Michael Dawson) [#27754](https://github.com/nodejs/node/pull/27754)
* \[[`e3bb2aef60`](https://github.com/nodejs/node/commit/e3bb2aef60)] - **doc**: remove experimental status for JSON documentation (Rich Trott) [#27842](https://github.com/nodejs/node/pull/27842)
* \[[`6981565c20`](https://github.com/nodejs/node/commit/6981565c20)] - **doc**: edit stability index overview (Rich Trott) [#27831](https://github.com/nodejs/node/pull/27831)
* \[[`1a8e67cc1f`](https://github.com/nodejs/node/commit/1a8e67cc1f)] - **doc**: simplify contributing documentation (Rich Trott) [#27785](https://github.com/nodejs/node/pull/27785)
* \[[`041b2220be`](https://github.com/nodejs/node/commit/041b2220be)] - **doc,n-api**: fix typo in N-API introduction (Richard Lau) [#27833](https://github.com/nodejs/node/pull/27833)
* \[[`6cd64c8279`](https://github.com/nodejs/node/commit/6cd64c8279)] - **doc,test**: clarify that Http2Stream is destroyed after data is read (Alba Mendez) [#27891](https://github.com/nodejs/node/pull/27891)
* \[[`cc69d5af8e`](https://github.com/nodejs/node/commit/cc69d5af8e)] - **doc,tools**: get altDocs versions from CHANGELOG.md (Richard Lau) [#27661](https://github.com/nodejs/node/pull/27661)
* \[[`e72d4aa522`](https://github.com/nodejs/node/commit/e72d4aa522)] - **errors**: create internal connResetException (Rich Trott) [#27953](https://github.com/nodejs/node/pull/27953)
* \[[`be1166fd01`](https://github.com/nodejs/node/commit/be1166fd01)] - **esm**: refactor createDynamicModule() (cjihrig) [#27809](https://github.com/nodejs/node/pull/27809)
* \[[`e66648e887`](https://github.com/nodejs/node/commit/e66648e887)] - **(SEMVER-MINOR)** **esm**: remove experimental status from JSON modules (Myles Borins) [#27752](https://github.com/nodejs/node/pull/27752)
* \[[`d948656635`](https://github.com/nodejs/node/commit/d948656635)] - **http**: fix deferToConnect comments (Robert Nagy) [#27876](https://github.com/nodejs/node/pull/27876)
* \[[`24eaeed393`](https://github.com/nodejs/node/commit/24eaeed393)] - **http**: fix socketOnWrap edge cases (Anatoli Papirovski) [#27968](https://github.com/nodejs/node/pull/27968)
* \[[`8b38dfbf39`](https://github.com/nodejs/node/commit/8b38dfbf39)] - **http**: call write callback even if there is no message body (Luigi Pinca) [#27777](https://github.com/nodejs/node/pull/27777)
* \[[`588fd0c20d`](https://github.com/nodejs/node/commit/588fd0c20d)] - **(SEMVER-MINOR)** **http, http2**: flag for overriding server timeout (Ali Ijaz Sheikh) [#27704](https://github.com/nodejs/node/pull/27704)
* \[[`799aeca134`](https://github.com/nodejs/node/commit/799aeca134)] - **http2**: respect inspect() depth (cjihrig) [#27983](https://github.com/nodejs/node/pull/27983)
* \[[`83aaef87d0`](https://github.com/nodejs/node/commit/83aaef87d0)] - **http2**: fix tracking received data for maxSessionMemory (Anna Henningsen) [#27914](https://github.com/nodejs/node/pull/27914)
* \[[`8c35198499`](https://github.com/nodejs/node/commit/8c35198499)] - **http2**: support net.Server options (Luigi Pinca) [#27782](https://github.com/nodejs/node/pull/27782)
* \[[`23119cacf8`](https://github.com/nodejs/node/commit/23119cacf8)] - **inspector**: supported NodeRuntime domain in worker (Aleksei Koziatinskii) [#27706](https://github.com/nodejs/node/pull/27706)
* \[[`89483be254`](https://github.com/nodejs/node/commit/89483be254)] - **inspector**: more conservative minimum stack size (Ben Noordhuis) [#27855](https://github.com/nodejs/node/pull/27855)
* \[[`512ab1fddf`](https://github.com/nodejs/node/commit/512ab1fddf)] - **inspector**: removing checking of non existent field in lib/inspector.js (Keroosha) [#27919](https://github.com/nodejs/node/pull/27919)
* \[[`d99e70381e`](https://github.com/nodejs/node/commit/d99e70381e)] - **SEMVER-MINOR** **inspector**: implement --heap-prof (Joyee Cheung) [#27596](https://github.com/nodejs/node/pull/27596)
* \[[`25eb05a97a`](https://github.com/nodejs/node/commit/25eb05a97a)] - **lib**: removed unnecessary fs.realpath `options` arg check + tests (Alex Pry) [#27909](https://github.com/nodejs/node/pull/27909)
* \[[`9b90385825`](https://github.com/nodejs/node/commit/9b90385825)] - _**Revert**_ "**lib**: print to stdout/stderr directly instead of using console" (Richard Lau) [#27823](https://github.com/nodejs/node/pull/27823)
* \[[`18650579e8`](https://github.com/nodejs/node/commit/18650579e8)] - **meta**: correct personal info (Refael Ackermann (רפאל פלחי)) [#27940](https://github.com/nodejs/node/pull/27940)
* \[[`d982f0b7e2`](https://github.com/nodejs/node/commit/d982f0b7e2)] - **meta**: create github support file (Gus Caplan) [#27926](https://github.com/nodejs/node/pull/27926)
* \[[`2b7ad122b2`](https://github.com/nodejs/node/commit/2b7ad122b2)] - **n-api**: DRY napi\_coerce\_to\_x() API methods (Ben Noordhuis) [#27796](https://github.com/nodejs/node/pull/27796)
* \[[`1da5acbf91`](https://github.com/nodejs/node/commit/1da5acbf91)] - **os**: assume UTF-8 for hostname (Anna Henningsen) [#27849](https://github.com/nodejs/node/pull/27849)
* \[[`d406785814`](https://github.com/nodejs/node/commit/d406785814)] - **src**: unimplement deprecated v8-platform methods (Michaël Zasso) [#27872](https://github.com/nodejs/node/pull/27872)
* \[[`33236b7c54`](https://github.com/nodejs/node/commit/33236b7c54)] - **(SEMVER-MINOR)** **src**: export number\_of\_native\_contexts and number\_of\_detached\_contexts (Yuriy Vasiyarov) [#27933](https://github.com/nodejs/node/pull/27933)
* \[[`1a179e1736`](https://github.com/nodejs/node/commit/1a179e1736)] - **src**: use ArrayBufferViewContents more frequently (Anna Henningsen) [#27920](https://github.com/nodejs/node/pull/27920)
* \[[`b9cc4072e6`](https://github.com/nodejs/node/commit/b9cc4072e6)] - **src**: make UNREACHABLE variadic (Refael Ackermann) [#27877](https://github.com/nodejs/node/pull/27877)
* \[[`44846aebd2`](https://github.com/nodejs/node/commit/44846aebd2)] - **src**: move DiagnosticFilename inlines into a -inl.h (Sam Roberts) [#27839](https://github.com/nodejs/node/pull/27839)
* \[[`d774ea5cce`](https://github.com/nodejs/node/commit/d774ea5cce)] - **src**: remove env-inl.h from header files (Sam Roberts) [#27755](https://github.com/nodejs/node/pull/27755)
* \[[`02f794a53f`](https://github.com/nodejs/node/commit/02f794a53f)] - **src**: remove memory\_tracker-inl.h from header files (Sam Roberts) [#27755](https://github.com/nodejs/node/pull/27755)
* \[[`940577bd76`](https://github.com/nodejs/node/commit/940577bd76)] - **src**: move ThreadPoolWork inlines into a -inl.h (Sam Roberts) [#27755](https://github.com/nodejs/node/pull/27755)
* \[[`c0cf17388c`](https://github.com/nodejs/node/commit/c0cf17388c)] - **src**: ignore SIGXFSZ, don't terminate (ulimit -f) (Ben Noordhuis) [#27798](https://github.com/nodejs/node/pull/27798)
* \[[`a47ee80114`](https://github.com/nodejs/node/commit/a47ee80114)] - **(SEMVER-MINOR)** **stream**: convert string to Buffer when calling `unshift(<string>)` (Marcos Casagrande) [#27194](https://github.com/nodejs/node/pull/27194)
* \[[`5eccd642ef`](https://github.com/nodejs/node/commit/5eccd642ef)] - **stream**: convert existing buffer when calling .setEncoding (Anna Henningsen) [#27936](https://github.com/nodejs/node/pull/27936)
* \[[`6a5ce36fb8`](https://github.com/nodejs/node/commit/6a5ce36fb8)] - **test**: handle unknown message type in worker threads (Rich Trott) [#27995](https://github.com/nodejs/node/pull/27995)
* \[[`182725651b`](https://github.com/nodejs/node/commit/182725651b)] - **test**: add coverage for unserializable worker thread error (Rich Trott) [#27995](https://github.com/nodejs/node/pull/27995)
* \[[`887dd604f1`](https://github.com/nodejs/node/commit/887dd604f1)] - **test**: simplify fs promises test (Daniel Nalborczyk) [#27242](https://github.com/nodejs/node/pull/27242)
* \[[`9229825496`](https://github.com/nodejs/node/commit/9229825496)] - **test**: covering destroying when worker already disconnected (Keroosha) [#27896](https://github.com/nodejs/node/pull/27896)
* \[[`10bdd13972`](https://github.com/nodejs/node/commit/10bdd13972)] - **test**: rename test-performance to test-perf-hooks (Ujjwal Sharma) [#27969](https://github.com/nodejs/node/pull/27969)
* \[[`6129376cd9`](https://github.com/nodejs/node/commit/6129376cd9)] - **test**: add coverage for sparse array maxArrayLength (went.out) [#27901](https://github.com/nodejs/node/pull/27901)
* \[[`38e3827ca8`](https://github.com/nodejs/node/commit/38e3827ca8)] - **test**: add util inspect null getter test (Mikhail Kuklin) [#27884](https://github.com/nodejs/node/pull/27884)
* \[[`0e1ce2055e`](https://github.com/nodejs/node/commit/0e1ce2055e)] - **test**: rsa-pss generateKeyPairSync invalid option hash (Evgenii Shchepotev) [#27883](https://github.com/nodejs/node/pull/27883)
* \[[`0d74198123`](https://github.com/nodejs/node/commit/0d74198123)] - **test**: cover import of a \*.node file with a policy manifest (Evgenii Shchepotev) [#27903](https://github.com/nodejs/node/pull/27903)
* \[[`6f9aa3f722`](https://github.com/nodejs/node/commit/6f9aa3f722)] - **test**: add test cases for paramEncoding 'explicit' (oksana) [#27900](https://github.com/nodejs/node/pull/27900)
* \[[`682319f449`](https://github.com/nodejs/node/commit/682319f449)] - **test**: switch assertEqual arguments (Evgenii Shchepotev) [#27910](https://github.com/nodejs/node/pull/27910)
* \[[`b5b234deff`](https://github.com/nodejs/node/commit/b5b234deff)] - **test**: add testcase for SourceTextModule custom inspect (Grigory Gorshkov) [#27889](https://github.com/nodejs/node/pull/27889)
* \[[`630cc3ac30`](https://github.com/nodejs/node/commit/630cc3ac30)] - **test**: cover util.inspect on boxed primitive with colors (Alexander Avakov) [#27897](https://github.com/nodejs/node/pull/27897)
* \[[`67b692bdb9`](https://github.com/nodejs/node/commit/67b692bdb9)] - **test**: add test case for checking typeof mgf1Hash (Levin Eugene) [#27892](https://github.com/nodejs/node/pull/27892)
* \[[`2a509d40f4`](https://github.com/nodejs/node/commit/2a509d40f4)] - **test**: switch assertEqual arguments (Evgenii Shchepotev) [#27912](https://github.com/nodejs/node/pull/27912)
* \[[`3ba354aaaa`](https://github.com/nodejs/node/commit/3ba354aaaa)] - **test**: add test for util.inspect (Levin Eugene) [#27906](https://github.com/nodejs/node/pull/27906)
* \[[`313077ea62`](https://github.com/nodejs/node/commit/313077ea62)] - **test**: expect wpt/encoding/encodeInto.any.js to fail (Joyee Cheung) [#27860](https://github.com/nodejs/node/pull/27860)
* \[[`8fc6914d09`](https://github.com/nodejs/node/commit/8fc6914d09)] - **test**: update wpt/encoding to 7287608f90 (Joyee Cheung) [#27860](https://github.com/nodejs/node/pull/27860)
* \[[`0f86c2b185`](https://github.com/nodejs/node/commit/0f86c2b185)] - **test**: run WPT in subdirectories (Joyee Cheung) [#27860](https://github.com/nodejs/node/pull/27860)
* \[[`51ccdae445`](https://github.com/nodejs/node/commit/51ccdae445)] - **test**: expect wpt/encoding/streams to fail (Joyee Cheung) [#27860](https://github.com/nodejs/node/pull/27860)
* \[[`652cadba1c`](https://github.com/nodejs/node/commit/652cadba1c)] - **test**: fix arguments order of comparsion functions (martyns0n) [#27907](https://github.com/nodejs/node/pull/27907)
* \[[`b117f6d5d8`](https://github.com/nodejs/node/commit/b117f6d5d8)] - **test**: switch assertEqual arguments (Evgenii Shchepotev) [#27913](https://github.com/nodejs/node/pull/27913)
* \[[`e7966bcb80`](https://github.com/nodejs/node/commit/e7966bcb80)] - **test**: unhardcode server port (MurkyMeow) [#27908](https://github.com/nodejs/node/pull/27908)
* \[[`b83571d236`](https://github.com/nodejs/node/commit/b83571d236)] - **test**: add a test case for the path.posix.resolve (Grigorii K. Shartsev) [#27905](https://github.com/nodejs/node/pull/27905)
* \[[`f5bb1b380f`](https://github.com/nodejs/node/commit/f5bb1b380f)] - **test**: switch actual value argument and expected in deepStrictEqual call (Kopachyov Vitaliy) [#27888](https://github.com/nodejs/node/pull/27888)
* \[[`531669b917`](https://github.com/nodejs/node/commit/531669b917)] - **test**: fix test-http2-multiheaders-raw (Grigorii K. Shartsev) [#27885](https://github.com/nodejs/node/pull/27885)
* \[[`724d9c89bc`](https://github.com/nodejs/node/commit/724d9c89bc)] - **test**: change expected and actual values in assert call (oksana) [#27881](https://github.com/nodejs/node/pull/27881)
* \[[`34ef9e4a2b`](https://github.com/nodejs/node/commit/34ef9e4a2b)] - **test**: detect missing postmortem metadata (cjihrig) [#27828](https://github.com/nodejs/node/pull/27828)
* \[[`bfcbab4c0c`](https://github.com/nodejs/node/commit/bfcbab4c0c)] - **test**: fix test-https-agent-additional-options (Rich Trott) [#27830](https://github.com/nodejs/node/pull/27830)
* \[[`a4c1fd5ffc`](https://github.com/nodejs/node/commit/a4c1fd5ffc)] - **test**: refactor test-https-agent-additional-options (Rich Trott) [#27830](https://github.com/nodejs/node/pull/27830)
* \[[`17abc8c942`](https://github.com/nodejs/node/commit/17abc8c942)] - **test**: favor arrow functions for anonymous callbacks (Rich Trott) [#27830](https://github.com/nodejs/node/pull/27830)
* \[[`155b947251`](https://github.com/nodejs/node/commit/155b947251)] - **test**: replace flag with option (Rich Trott) [#27830](https://github.com/nodejs/node/pull/27830)
* \[[`144db48b6d`](https://github.com/nodejs/node/commit/144db48b6d)] - **test**: update wpt/url to 418f7fabeb (Joyee Cheung) [#27822](https://github.com/nodejs/node/pull/27822)
* \[[`65d4f734e0`](https://github.com/nodejs/node/commit/65d4f734e0)] - **test**: use ShellTestEnvironment in WPT (Joyee Cheung) [#27822](https://github.com/nodejs/node/pull/27822)
* \[[`a9a400e604`](https://github.com/nodejs/node/commit/a9a400e604)] - **test**: update wpt/resources to e1fddfbf80 (Joyee Cheung) [#27822](https://github.com/nodejs/node/pull/27822)
* \[[`8040d8b321`](https://github.com/nodejs/node/commit/8040d8b321)] - **test**: increase debugging information on failure (Rich Trott) [#27790](https://github.com/nodejs/node/pull/27790)
* \[[`6548b91835`](https://github.com/nodejs/node/commit/6548b91835)] - **tls**: trace errors can show up as SSL errors (Sam Roberts) [#27841](https://github.com/nodejs/node/pull/27841)
* \[[`0fe16edfab`](https://github.com/nodejs/node/commit/0fe16edfab)] - **tls**: group chunks into TLS segments (Alba Mendez) [#27861](https://github.com/nodejs/node/pull/27861)
* \[[`e8fa0671a4`](https://github.com/nodejs/node/commit/e8fa0671a4)] - **tls**: destroy trace BIO instead of leaking it (Sam Roberts) [#27834](https://github.com/nodejs/node/pull/27834)
* \[[`10e0d7f2ac`](https://github.com/nodejs/node/commit/10e0d7f2ac)] - **tls**: support the hints option (Luigi Pinca) [#27816](https://github.com/nodejs/node/pull/27816)
* \[[`4716caa12e`](https://github.com/nodejs/node/commit/4716caa12e)] - **tls**: set tlsSocket.servername as early as possible (oyyd) [#27759](https://github.com/nodejs/node/pull/27759)
* \[[`2ce24a9452`](https://github.com/nodejs/node/commit/2ce24a9452)] - **tools**: fix js2c regression (Refael Ackermann) [#27980](https://github.com/nodejs/node/pull/27980)
* \[[`a75a59d3e3`](https://github.com/nodejs/node/commit/a75a59d3e3)] - **tools**: update inspector\_protocol to 0aafd2 (Michaël Zasso) [#27770](https://github.com/nodejs/node/pull/27770)
* \[[`728bc2f59a`](https://github.com/nodejs/node/commit/728bc2f59a)] - **tools**: update dependencies in tools/doc (Rich Trott) [#27927](https://github.com/nodejs/node/pull/27927)
* \[[`b54f3e0405`](https://github.com/nodejs/node/commit/b54f3e0405)] - **tools**: edit .eslintrc.js for minor maintainability improvements (Rich Trott) [#27789](https://github.com/nodejs/node/pull/27789)

<a id="12.3.1"></a>

## 2019-05-22, Version 12.3.1 (Current), @BridgeAR

### Notable changes

* **deps**:
  * Fix handling of +0/-0 when constant field tracking is enabled (Michaël Zasso) [#27792](https://github.com/nodejs/node/pull/27792)
  * Fix `os.freemem()` and `os.totalmem` correctness (cjihrig) [#27718](https://github.com/nodejs/node/pull/27718)
* **src**:
  * Fix v12.3.0 regression that prevents native addons from compiling [#27804](https://github.com/nodejs/node/pull/27804)

### Commits

* \[[`c478884725`](https://github.com/nodejs/node/commit/c478884725)] - **deps**: V8: cherry-pick 94c87fe (Michaël Zasso) [#27792](https://github.com/nodejs/node/pull/27792)
* \[[`aed74ccb4c`](https://github.com/nodejs/node/commit/aed74ccb4c)] - **deps**: upgrade to libuv 1.29.1 (cjihrig) [#27718](https://github.com/nodejs/node/pull/27718)
* \[[`7438a557af`](https://github.com/nodejs/node/commit/7438a557af)] - **src**: remove util-inl.h include in node.h (Anna Henningsen) [#27804](https://github.com/nodejs/node/pull/27804)
* \[[`6f7005465a`](https://github.com/nodejs/node/commit/6f7005465a)] - **src, lib**: take control of prepareStackTrace (Gus Caplan) [#23926](https://github.com/nodejs/node/pull/23926)

<a id="12.3.0"></a>

## 2019-05-21, Version 12.3.0 (Current), @BridgeAR

### Notable changes

* **esm**:
  * Added the `--experimental-wasm-modules` flag to support WebAssembly modules (Myles Borins & Guy Bedford) [#27659](https://github.com/nodejs/node/pull/27659)
* **process**:
  * Log errors using `util.inspect` in case of fatal exceptions (Ruben Bridgewater) [#27243](https://github.com/nodejs/node/pull/27243)
* **repl**:
  * Add `process.on('uncaughtException')` support (Ruben Bridgewater) [#27151](https://github.com/nodejs/node/pull/27151)
* **stream**:
  * Implemented `Readable.from` async iterator utility (Guy Bedford) [#27660](https://github.com/nodejs/node/pull/27660)
* **tls**:
  * Expose built-in root certificates (Ben Noordhuis) [#26415](https://github.com/nodejs/node/pull/26415)
  * Support `net.Server` options (Luigi Pinca) [#27665](https://github.com/nodejs/node/pull/27665)
  * Expose `keylog` event on TLSSocket (Alba Mendez) [#27654](https://github.com/nodejs/node/pull/27654)
* **worker**:
  * Added the ability to unshift messages from the `MessagePort` (Anna Henningsen) [#27294](https://github.com/nodejs/node/pull/27294)

### Commits

* \[[`7cc21d8afa`](https://github.com/nodejs/node/commit/7cc21d8afa)] - **assert**: remove unused code (Ruben Bridgewater) [#27676](https://github.com/nodejs/node/pull/27676)
* \[[`6983a0c336`](https://github.com/nodejs/node/commit/6983a0c336)] - **assert**: add compatibility for older Node.js versions (Ruben Bridgewater) [#27672](https://github.com/nodejs/node/pull/27672)
* \[[`493ead144d`](https://github.com/nodejs/node/commit/493ead144d)] - **assert**: loose deep equal should not compare symbol properties (Ruben Bridgewater) [#27653](https://github.com/nodejs/node/pull/27653)
* \[[`ec642f18cc`](https://github.com/nodejs/node/commit/ec642f18cc)] - **assert**: use less read operations (Ruben Bridgewater) [#27525](https://github.com/nodejs/node/pull/27525)
* \[[`3367bad080`](https://github.com/nodejs/node/commit/3367bad080)] - **assert**: refine assertion message (Ruben Bridgewater) [#27525](https://github.com/nodejs/node/pull/27525)
* \[[`e573c99bfd`](https://github.com/nodejs/node/commit/e573c99bfd)] - **assert**: fix `assert.fail()` stack (Ruben Bridgewater) [#27525](https://github.com/nodejs/node/pull/27525)
* \[[`6070e8872d`](https://github.com/nodejs/node/commit/6070e8872d)] - **async\_hooks**: don't reuse resource in HttpAgent (Gerhard Stoebich) [#27581](https://github.com/nodejs/node/pull/27581)
* \[[`e74e661044`](https://github.com/nodejs/node/commit/e74e661044)] - **async\_hooks**: only disable promise hook if wanted (Anna Henningsen) [#27590](https://github.com/nodejs/node/pull/27590)
* \[[`026bebfcbc`](https://github.com/nodejs/node/commit/026bebfcbc)] - **bootstrap**: --frozen-intrinsics unfreeze console (Guy Bedford) [#27663](https://github.com/nodejs/node/pull/27663)
* \[[`e0589006a8`](https://github.com/nodejs/node/commit/e0589006a8)] - **build**: add arm64 to vcbuild.bat help message (Jon Kunkee) [#27683](https://github.com/nodejs/node/pull/27683)
* \[[`766a731137`](https://github.com/nodejs/node/commit/766a731137)] - **build**: export OpenSSL UI symbols (Sam Roberts) [#27586](https://github.com/nodejs/node/pull/27586)
* \[[`2bc177aa4f`](https://github.com/nodejs/node/commit/2bc177aa4f)] - **child\_process**: setup stdio on error when possible (cjihrig) [#27696](https://github.com/nodejs/node/pull/27696)
* \[[`b380c0f311`](https://github.com/nodejs/node/commit/b380c0f311)] - **child\_process**: refactor stdioStringToArray function (zero1five) [#27657](https://github.com/nodejs/node/pull/27657)
* \[[`da102cda54`](https://github.com/nodejs/node/commit/da102cda54)] - **console**: don't attach unnecessary error handlers (cjihrig) [#27691](https://github.com/nodejs/node/pull/27691)
* \[[`83f243038f`](https://github.com/nodejs/node/commit/83f243038f)] - **deps**: V8: cherry-pick cca9ae3c9a (Benedikt Meurer) [#27729](https://github.com/nodejs/node/pull/27729)
* \[[`750556dcfd`](https://github.com/nodejs/node/commit/750556dcfd)] - **deps**: update OpenSSL configs' timestamps (Jon Kunkee) [#27544](https://github.com/nodejs/node/pull/27544)
* \[[`314fdda0c3`](https://github.com/nodejs/node/commit/314fdda0c3)] - **deps**: regenerate OpenSSL configs with fixed tooling (Jon Kunkee) [#27544](https://github.com/nodejs/node/pull/27544)
* \[[`c7e5fca32c`](https://github.com/nodejs/node/commit/c7e5fca32c)] - **deps**: make VC-WIN config generation deterministic (Jon Kunkee) [#27543](https://github.com/nodejs/node/pull/27543)
* \[[`76c9e86609`](https://github.com/nodejs/node/commit/76c9e86609)] - **deps**: patch V8 to 7.4.288.27 (Matheus Marchini) [#27615](https://github.com/nodejs/node/pull/27615)
* \[[`9f5b6900e7`](https://github.com/nodejs/node/commit/9f5b6900e7)] - **doc**: corrected tlsSocket.getPeerCertificate response type (Dan Beglin) [#27757](https://github.com/nodejs/node/pull/27757)
* \[[`d1da11765d`](https://github.com/nodejs/node/commit/d1da11765d)] - **doc**: correct parameter type on 'subprocess.kill(\[signal])' (himself65) [#27760](https://github.com/nodejs/node/pull/27760)
* \[[`7e750868c6`](https://github.com/nodejs/node/commit/7e750868c6)] - **doc**: replace createRequireFromPath() references (cjihrig) [#27762](https://github.com/nodejs/node/pull/27762)
* \[[`55fe340dc2`](https://github.com/nodejs/node/commit/55fe340dc2)] - **doc**: improve createRequire() example (cjihrig) [#27762](https://github.com/nodejs/node/pull/27762)
* \[[`378f44c2ed`](https://github.com/nodejs/node/commit/378f44c2ed)] - **doc**: update `util.format` formatters documentation (Ruben Bridgewater) [#27621](https://github.com/nodejs/node/pull/27621)
* \[[`f663e74d0b`](https://github.com/nodejs/node/commit/f663e74d0b)] - **doc**: remove stability highlight for stable functions (Michael Dawson) [#27753](https://github.com/nodejs/node/pull/27753)
* \[[`cf516f7b6a`](https://github.com/nodejs/node/commit/cf516f7b6a)] - **doc**: rewrite "About this Documentation" section (Rich Trott) [#27725](https://github.com/nodejs/node/pull/27725)
* \[[`df01645c7c`](https://github.com/nodejs/node/commit/df01645c7c)] - **doc**: correct entry for electron v4.0.4 (Jacob) [#27394](https://github.com/nodejs/node/pull/27394)
* \[[`1f7a527f04`](https://github.com/nodejs/node/commit/1f7a527f04)] - **doc**: clarify behavior of fs.mkdir (Gaelan) [#27505](https://github.com/nodejs/node/pull/27505)
* \[[`d570995427`](https://github.com/nodejs/node/commit/d570995427)] - **doc**: remove non-existent entry-type flag (dnalborczyk) [#27678](https://github.com/nodejs/node/pull/27678)
* \[[`da4a3797cb`](https://github.com/nodejs/node/commit/da4a3797cb)] - **doc**: format correction for experimental loader hooks (Daniel Nalborczyk) [#27537](https://github.com/nodejs/node/pull/27537)
* \[[`cc45080109`](https://github.com/nodejs/node/commit/cc45080109)] - **doc**: dns.lookup() documentation error code (jvelezpo) [#27625](https://github.com/nodejs/node/pull/27625)
* \[[`7923b4a407`](https://github.com/nodejs/node/commit/7923b4a407)] - **doc**: add call-once note to napi\_queue\_async\_work (Gabriel Schulhof) [#27582](https://github.com/nodejs/node/pull/27582)
* \[[`8d448be9fd`](https://github.com/nodejs/node/commit/8d448be9fd)] - **doc**: simplify test/README.md (Rich Trott) [#27630](https://github.com/nodejs/node/pull/27630)
* \[[`172fa639a6`](https://github.com/nodejs/node/commit/172fa639a6)] - **doc**: simplify About This Documentation text (Rich Trott) [#27619](https://github.com/nodejs/node/pull/27619)
* \[[`66cf89f57d`](https://github.com/nodejs/node/commit/66cf89f57d)] - **doc**: move Rod Vagg to TSC emeritus (Rod Vagg) [#27633](https://github.com/nodejs/node/pull/27633)
* \[[`8a1f2d0bfc`](https://github.com/nodejs/node/commit/8a1f2d0bfc)] - **doc**: doc deprecate the legacy http parser (cjihrig) [#27498](https://github.com/nodejs/node/pull/27498)
* \[[`a23e86f029`](https://github.com/nodejs/node/commit/a23e86f029)] - **doc**: add Sam Roberts to TSC (Rod Vagg) [#27606](https://github.com/nodejs/node/pull/27606)
* \[[`c53a674be7`](https://github.com/nodejs/node/commit/c53a674be7)] - **doc**: add example to test doc for clarity (Aditya Pratap Singh) [#27561](https://github.com/nodejs/node/pull/27561)
* \[[`c0cdf30e6e`](https://github.com/nodejs/node/commit/c0cdf30e6e)] - **doc**: improve CCM example (Tobias Nießen) [#27396](https://github.com/nodejs/node/pull/27396)
* \[[`b5498ed19b`](https://github.com/nodejs/node/commit/b5498ed19b)] - **doc,meta**: codify security release commit message (Richard Lau) [#27643](https://github.com/nodejs/node/pull/27643)
* \[[`6e2c8d0e18`](https://github.com/nodejs/node/commit/6e2c8d0e18)] - **doc,n-api**: update N-API version matrix for v12.x (Richard Lau) [#27745](https://github.com/nodejs/node/pull/27745)
* \[[`767889b0a3`](https://github.com/nodejs/node/commit/767889b0a3)] - **doc,n-api**: fix `introduced_in` metadata (Richard Lau) [#27745](https://github.com/nodejs/node/pull/27745)
* \[[`4ed8a9ba7e`](https://github.com/nodejs/node/commit/4ed8a9ba7e)] - **doc,tools**: updates for 6.x End-of-Life (Richard Lau) [#27658](https://github.com/nodejs/node/pull/27658)
* \[[`80f30741bd`](https://github.com/nodejs/node/commit/80f30741bd)] - **esm**: use correct error arguments (cjihrig) [#27763](https://github.com/nodejs/node/pull/27763)
* \[[`47f913bedc`](https://github.com/nodejs/node/commit/47f913bedc)] - **(SEMVER-MINOR)** **esm**: --experimental-wasm-modules integration support (Myles Borins) [#27659](https://github.com/nodejs/node/pull/27659)
* \[[`89fda94b6a`](https://github.com/nodejs/node/commit/89fda94b6a)] - **esm**: fix esm load bug (ZYSzys) [#25491](https://github.com/nodejs/node/pull/25491)
* \[[`1f935f899f`](https://github.com/nodejs/node/commit/1f935f899f)] - **events**: improve max listeners warning (Ruben Bridgewater) [#27694](https://github.com/nodejs/node/pull/27694)
* \[[`6f23816bcf`](https://github.com/nodejs/node/commit/6f23816bcf)] - **fs**: extract path conversion and validation to getValidatedPath (ZYSzys) [#27656](https://github.com/nodejs/node/pull/27656)
* \[[`206ae31a7e`](https://github.com/nodejs/node/commit/206ae31a7e)] - **http**: always call response.write() callback (Luigi Pinca) [#27709](https://github.com/nodejs/node/pull/27709)
* \[[`bfb9356942`](https://github.com/nodejs/node/commit/bfb9356942)] - **http**: do not default to chunked encoding for TRACE (Luigi Pinca) [#27673](https://github.com/nodejs/node/pull/27673)
* \[[`4a9af1778d`](https://github.com/nodejs/node/commit/4a9af1778d)] - **http**: add an alias at addListener on Server connection socket (himself65) [#27325](https://github.com/nodejs/node/pull/27325)
* \[[`a66b391d20`](https://github.com/nodejs/node/commit/a66b391d20)] - **http2**: do no throw in writeHead if state.closed (Matteo Collina) [#27682](https://github.com/nodejs/node/pull/27682)
* \[[`74046cee72`](https://github.com/nodejs/node/commit/74046cee72)] - **http2**: do not override the allowHalfOpen option (Luigi Pinca) [#27623](https://github.com/nodejs/node/pull/27623)
* \[[`c7461567ce`](https://github.com/nodejs/node/commit/c7461567ce)] - **inspector**: mark profile type const (gengjiawen) [#27712](https://github.com/nodejs/node/pull/27712)
* \[[`24b26c0687`](https://github.com/nodejs/node/commit/24b26c0687)] - **inspector**: fix typo (gengjiawen) [#27712](https://github.com/nodejs/node/pull/27712)
* \[[`700459e008`](https://github.com/nodejs/node/commit/700459e008)] - **inspector**: added NodeRuntime domain (Aleksei Koziatinskii) [#27600](https://github.com/nodejs/node/pull/27600)
* \[[`d2d3bf8b3b`](https://github.com/nodejs/node/commit/d2d3bf8b3b)] - **inspector**: code cleanup (Eugene Ostroukhov) [#27591](https://github.com/nodejs/node/pull/27591)
* \[[`4dbebfd464`](https://github.com/nodejs/node/commit/4dbebfd464)] - **lib**: fix typo in pre\_execution.js (gengjiawen) [#27649](https://github.com/nodejs/node/pull/27649)
* \[[`88b4d00fc6`](https://github.com/nodejs/node/commit/88b4d00fc6)] - **lib**: restore `global.module` after --eval code is run (Anna Henningsen) [#27587](https://github.com/nodejs/node/pull/27587)
* \[[`3ac4a7122b`](https://github.com/nodejs/node/commit/3ac4a7122b)] - **meta**: move jhamhader to Collaborator Emeriti list (Rich Trott) [#27707](https://github.com/nodejs/node/pull/27707)
* \[[`9f9871c4b2`](https://github.com/nodejs/node/commit/9f9871c4b2)] - **meta**: move chrisdickinson to Collaborator Emeriti list (Rich Trott) [#27703](https://github.com/nodejs/node/pull/27703)
* \[[`2e85642f4a`](https://github.com/nodejs/node/commit/2e85642f4a)] - **meta**: move whitlockjc to Collaborator Emeriti list (Rich Trott) [#27702](https://github.com/nodejs/node/pull/27702)
* \[[`fc8ad7731f`](https://github.com/nodejs/node/commit/fc8ad7731f)] - **meta**: move estliberitas to Collaborator Emeriti list (Rich Trott) [#27697](https://github.com/nodejs/node/pull/27697)
* \[[`ea62149212`](https://github.com/nodejs/node/commit/ea62149212)] - **meta**: move firedfox to Collaborator Emeriti list (Rich Trott) [#27618](https://github.com/nodejs/node/pull/27618)
* \[[`6bef4c0083`](https://github.com/nodejs/node/commit/6bef4c0083)] - **meta**: move AnnaMag to Collaborator Emeriti list (Rich Trott) [#27603](https://github.com/nodejs/node/pull/27603)
* \[[`14d58c2f95`](https://github.com/nodejs/node/commit/14d58c2f95)] - **meta**: move pmq20 to Collaborator Emeriti list (Rich Trott) [#27602](https://github.com/nodejs/node/pull/27602)
* \[[`876441eefb`](https://github.com/nodejs/node/commit/876441eefb)] - **meta**: move orangemocha to Collaborator Emeriti list (Rich Trott) [#27626](https://github.com/nodejs/node/pull/27626)
* \[[`140b44f3ea`](https://github.com/nodejs/node/commit/140b44f3ea)] - **module**: fix createRequireFromPath() slash logic (cjihrig) [#27634](https://github.com/nodejs/node/pull/27634)
* \[[`8a96182827`](https://github.com/nodejs/node/commit/8a96182827)] - **module**: add missing space in error message (cjihrig) [#27627](https://github.com/nodejs/node/pull/27627)
* \[[`c33e83497e`](https://github.com/nodejs/node/commit/c33e83497e)] - **module**: simplify createRequire() validation (cjihrig) [#27629](https://github.com/nodejs/node/pull/27629)
* \[[`119a590f84`](https://github.com/nodejs/node/commit/119a590f84)] - **module**: improve resolve paths validation (cjihrig) [#27613](https://github.com/nodejs/node/pull/27613)
* \[[`2f512e32a7`](https://github.com/nodejs/node/commit/2f512e32a7)] - **module**: handle relative paths in resolve paths (cjihrig) [#27598](https://github.com/nodejs/node/pull/27598)
* \[[`74feb0b81e`](https://github.com/nodejs/node/commit/74feb0b81e)] - **process**: mark process.env as side-effect-free (Anna Henningsen) [#27684](https://github.com/nodejs/node/pull/27684)
* \[[`0393045198`](https://github.com/nodejs/node/commit/0393045198)] - **(SEMVER-MINOR)** **process**: inspect error in case of a fatal exception (Ruben Bridgewater) [#27243](https://github.com/nodejs/node/pull/27243)
* \[[`688a0bd2b8`](https://github.com/nodejs/node/commit/688a0bd2b8)] - **repl**: do not run --eval code if there is none (Anna Henningsen) [#27587](https://github.com/nodejs/node/pull/27587)
* \[[`c78de13238`](https://github.com/nodejs/node/commit/c78de13238)] - **(SEMVER-MINOR)** **repl**: handle uncaughtException properly (Ruben Bridgewater) [#27151](https://github.com/nodejs/node/pull/27151)
* \[[`d21e066f5a`](https://github.com/nodejs/node/commit/d21e066f5a)] - **src**: update UNREACHABLE macro to take a string (Nitish Sakhawalkar) [#26502](https://github.com/nodejs/node/pull/26502)
* \[[`ae8b64df78`](https://github.com/nodejs/node/commit/ae8b64df78)] - **src**: remove util-inl.h from header files (Sam Roberts) [#27631](https://github.com/nodejs/node/pull/27631)
* \[[`e736e20e87`](https://github.com/nodejs/node/commit/e736e20e87)] - **src**: declare unused priv argument (Sam Roberts) [#27631](https://github.com/nodejs/node/pull/27631)
* \[[`d2e1efe8a3`](https://github.com/nodejs/node/commit/d2e1efe8a3)] - **src**: fix warnings about redefined BSWAP macros (Sam Roberts) [#27631](https://github.com/nodejs/node/pull/27631)
* \[[`3c707976da`](https://github.com/nodejs/node/commit/3c707976da)] - **src**: remove extra semicolons after macros (gengjiawen) [#27579](https://github.com/nodejs/node/pull/27579)
* \[[`a18692c4df`](https://github.com/nodejs/node/commit/a18692c4df)] - **src**: extract common macro to util.h (gengjiawen) [#27512](https://github.com/nodejs/node/pull/27512)
* \[[`f6642e90b2`](https://github.com/nodejs/node/commit/f6642e90b2)] - **src**: elevate namespaces in node\_worker.cc (Preveen Padmanabhan) [#27568](https://github.com/nodejs/node/pull/27568)
* \[[`62fe3422fb`](https://github.com/nodejs/node/commit/62fe3422fb)] - **src**: refactor deprecated UVException usage in pipe-wrap.cc (gengjiawen) [#27562](https://github.com/nodejs/node/pull/27562)
* \[[`b338d53916`](https://github.com/nodejs/node/commit/b338d53916)] - **src**: fix typos (gengjiawen) [#27580](https://github.com/nodejs/node/pull/27580)
* \[[`32fd0ac901`](https://github.com/nodejs/node/commit/32fd0ac901)] - **stream**: use readableObjectMode public api for js stream (Anto Aravinth) [#27655](https://github.com/nodejs/node/pull/27655)
* \[[`05c3d53ecc`](https://github.com/nodejs/node/commit/05c3d53ecc)] - **(SEMVER-MINOR)** **stream**: implement Readable.from async iterator utility (Guy Bedford) [#27660](https://github.com/nodejs/node/pull/27660)
* \[[`f872210ffd`](https://github.com/nodejs/node/commit/f872210ffd)] - **test**: relax check in verify-graph (Gerhard Stoebich) [#27742](https://github.com/nodejs/node/pull/27742)
* \[[`8b4101a97f`](https://github.com/nodejs/node/commit/8b4101a97f)] - **test**: un-mark worker syntax error tests as flaky (Anna Henningsen) [#27705](https://github.com/nodejs/node/pull/27705)
* \[[`1757250997`](https://github.com/nodejs/node/commit/1757250997)] - **test**: clearing require cache crashes esm loader (Antoine du HAMEL) [#25491](https://github.com/nodejs/node/pull/25491)
* \[[`7252a64a23`](https://github.com/nodejs/node/commit/7252a64a23)] - **test**: pass null params to napi\_xxx\_property() (Octavian Soldea) [#27628](https://github.com/nodejs/node/pull/27628)
* \[[`9ed5882dec`](https://github.com/nodejs/node/commit/9ed5882dec)] - **test**: use common.PORT instead of an extraneous variable (Benjamin Ki) [#27565](https://github.com/nodejs/node/pull/27565)
* \[[`f01183c29a`](https://github.com/nodejs/node/commit/f01183c29a)] - **test**: move dgram invalid host test to internet tests (Benjamin Ki) [#27565](https://github.com/nodejs/node/pull/27565)
* \[[`8cba1affe3`](https://github.com/nodejs/node/commit/8cba1affe3)] - **test**: better assertion for async hook tests (Ali Ijaz Sheikh) [#27601](https://github.com/nodejs/node/pull/27601)
* \[[`0c7f18ebd3`](https://github.com/nodejs/node/commit/0c7f18ebd3)] - **test**: test error when breakOnSigint is not a boolean for evaluate (Ruwan Geeganage) [#27503](https://github.com/nodejs/node/pull/27503)
* \[[`3801859032`](https://github.com/nodejs/node/commit/3801859032)] - **test**: add tests for hasItems method in FreeList (Ruwan Geeganage) [#27588](https://github.com/nodejs/node/pull/27588)
* \[[`691866f124`](https://github.com/nodejs/node/commit/691866f124)] - **test**: fix test-linux-perf flakiness (Matheus Marchini) [#27615](https://github.com/nodejs/node/pull/27615)
* \[[`d7fcd75f62`](https://github.com/nodejs/node/commit/d7fcd75f62)] - **test**: remove unneeded `--expose-internals` (Rich Trott) [#27608](https://github.com/nodejs/node/pull/27608)
* \[[`815a95734e`](https://github.com/nodejs/node/commit/815a95734e)] - **test**: refactor test-tls-enable-trace-cli.js (cjihrig) [#27553](https://github.com/nodejs/node/pull/27553)
* \[[`b6e540a9a2`](https://github.com/nodejs/node/commit/b6e540a9a2)] - **test**: fix flaky test-tls-multiple-cas-as-string (Luigi Pinca) [#27569](https://github.com/nodejs/node/pull/27569)
* \[[`a5dab9e85a`](https://github.com/nodejs/node/commit/a5dab9e85a)] - **test**: deflake test-tls-js-stream (Luigi Pinca) [#27478](https://github.com/nodejs/node/pull/27478)
* \[[`bdd75d0622`](https://github.com/nodejs/node/commit/bdd75d0622)] - **(SEMVER-MINOR)** **tls**: expose built-in root certificates (Ben Noordhuis) [#26415](https://github.com/nodejs/node/pull/26415)
* \[[`e61823c43a`](https://github.com/nodejs/node/commit/e61823c43a)] - **(SEMVER-MINOR)** **tls**: support `net.Server` options (Luigi Pinca) [#27665](https://github.com/nodejs/node/pull/27665)
* \[[`eb1f4e50c7`](https://github.com/nodejs/node/commit/eb1f4e50c7)] - **(SEMVER-MINOR)** **tls**: expose keylog event on TLSSocket (Alba Mendez) [#27654](https://github.com/nodejs/node/pull/27654)
* \[[`6624f802d9`](https://github.com/nodejs/node/commit/6624f802d9)] - **tls**: fix createSecureContext() cipher list filter (Sam Roberts) [#27614](https://github.com/nodejs/node/pull/27614)
* \[[`b8b02c35ee`](https://github.com/nodejs/node/commit/b8b02c35ee)] - **tls**: add missing 'new' (cjihrig) [#27614](https://github.com/nodejs/node/pull/27614)
* \[[`a8a11862e0`](https://github.com/nodejs/node/commit/a8a11862e0)] - **tools**: update markdown linter for Windows line endings (Rich Trott) [#27756](https://github.com/nodejs/node/pull/27756)
* \[[`c3d16756f2`](https://github.com/nodejs/node/commit/c3d16756f2)] - **tools**: remove unneeded dependency files (Rich Trott) [#27730](https://github.com/nodejs/node/pull/27730)
* \[[`0db846f734`](https://github.com/nodejs/node/commit/0db846f734)] - **tools**: refactor js2c.py for maximal Python3 compatibility (Refael Ackermann) [#25518](https://github.com/nodejs/node/pull/25518)
* \[[`0e16b352b4`](https://github.com/nodejs/node/commit/0e16b352b4)] - **tools**: decrease code duplication for isString() in lint rules (cjihrig) [#27719](https://github.com/nodejs/node/pull/27719)
* \[[`47184d1a0a`](https://github.com/nodejs/node/commit/47184d1a0a)] - **tools**: update capitalized-comments eslint rule (Ruben Bridgewater) [#27675](https://github.com/nodejs/node/pull/27675)
* \[[`ea62f4a820`](https://github.com/nodejs/node/commit/ea62f4a820)] - **tools**: update dmn to 2.2.2 (Rich Trott) [#27686](https://github.com/nodejs/node/pull/27686)
* \[[`d2dad0b4b8`](https://github.com/nodejs/node/commit/d2dad0b4b8)] - **tools**: DRY isRequireCall() in lint rules (cjihrig) [#27680](https://github.com/nodejs/node/pull/27680)
* \[[`1b8bc77990`](https://github.com/nodejs/node/commit/1b8bc77990)] - **tools**: add `12.x` to alternative docs versions (Richard Lau) [#27658](https://github.com/nodejs/node/pull/27658)
* \[[`1365683c23`](https://github.com/nodejs/node/commit/1365683c23)] - **tools**: allow RegExp in required-modules eslint rule (Richard Lau) [#27647](https://github.com/nodejs/node/pull/27647)
* \[[`169ddc5097`](https://github.com/nodejs/node/commit/169ddc5097)] - **tools**: force common be required before any other modules (ZYSzys) [#27650](https://github.com/nodejs/node/pull/27650)
* \[[`c6ab6b279c`](https://github.com/nodejs/node/commit/c6ab6b279c)] - **tools**: enable block-scoped-var eslint rule (Roman Reiss) [#27616](https://github.com/nodejs/node/pull/27616)
* \[[`fd823ea7a8`](https://github.com/nodejs/node/commit/fd823ea7a8)] - **tools**: enable camelcase linting in tools (Rich Trott) [#27607](https://github.com/nodejs/node/pull/27607)
* \[[`217e6b5a06`](https://github.com/nodejs/node/commit/217e6b5a06)] - **tools**: switch to camelcasing in apilinks.js (Rich Trott) [#27607](https://github.com/nodejs/node/pull/27607)
* \[[`10b4a8103d`](https://github.com/nodejs/node/commit/10b4a8103d)] - **util**: if present, fallback to `toString` using the %s formatter (Ruben Bridgewater) [#27621](https://github.com/nodejs/node/pull/27621)
* \[[`5205902762`](https://github.com/nodejs/node/commit/5205902762)] - **util**: remove outdated comment (Ruben Bridgewater) [#27733](https://github.com/nodejs/node/pull/27733)
* \[[`099c9ce1a1`](https://github.com/nodejs/node/commit/099c9ce1a1)] - **util**: unify constructor inspection in `util.inspect` (Ruben Bridgewater) [#27733](https://github.com/nodejs/node/pull/27733)
* \[[`d8b48675a7`](https://github.com/nodejs/node/commit/d8b48675a7)] - **util**: simplify inspection limit handling (Ruben Bridgewater) [#27733](https://github.com/nodejs/node/pull/27733)
* \[[`6984ca1c2f`](https://github.com/nodejs/node/commit/6984ca1c2f)] - **util**: reconstruct constructor in more cases (Ruben Bridgewater) [#27668](https://github.com/nodejs/node/pull/27668)
* \[[`8f48edd28f`](https://github.com/nodejs/node/commit/8f48edd28f)] - **vm**: mark global proxy as side-effect-free (Anna Henningsen) [#27523](https://github.com/nodejs/node/pull/27523)
* \[[`c7cf8d9b74`](https://github.com/nodejs/node/commit/c7cf8d9b74)] - **(SEMVER-MINOR)** **worker**: add ability to unshift message from MessagePort (Anna Henningsen) [#27294](https://github.com/nodejs/node/pull/27294)
* \[[`e004d427ce`](https://github.com/nodejs/node/commit/e004d427ce)] - **worker**: use special message as MessagePort close command (Anna Henningsen) [#27705](https://github.com/nodejs/node/pull/27705)
* \[[`b7ed4d7187`](https://github.com/nodejs/node/commit/b7ed4d7187)] - **worker**: move `receiving_messages_` field to `MessagePort` (Anna Henningsen) [#27705](https://github.com/nodejs/node/pull/27705)

<a id="12.2.0"></a>

## 2019-05-07, Version 12.2.0 (Current), @targos

### Notable changes

* **deps**:
  * Updated llhttp to 1.1.3. This fixes a bug that made Node.js' HTTP parser
    refuse any request URL that contained the "|" (vertical bar) character (Fedor Indutny) [#27595](https://github.com/nodejs/node/pull/27595).
* **tls**:
  * Added an `enableTrace()` method to `TLSSocket` and an `enableTrace` option
    to `tls.createServer()`. When enabled, TSL packet trace information is
    written to `stderr`. This can be used to debug TLS connection problems (cjihrig) [#27497](https://github.com/nodejs/node/pull/27497), (Sam Roberts) [#27376](https://github.com/nodejs/node/pull/27376).
* **cli**:
  * Added a `--trace-tls` command-line flag that enables tracing of TLS
    connections without the need to modify existing application code (cjihrig) [#27497](https://github.com/nodejs/node/pull/27497).
  * Added a `--cpu-prof-interval` command-line flag. It can be used to specify
    the sampling interval for the CPU profiles generated by `--cpu-prof` (Joyee Cheung) [#27535](https://github.com/nodejs/node/pull/27535).
* **module**:
  * Added the `createRequire()` method. It allows to create a require function
    from a file URL object, a file URL string or an absolute path string. The
    existing `createRequireFromPath()` method is now deprecated (Myles Borins) [#27405](https://github.com/nodejs/node/pull/27405).
  * Throw on `require('./path.mjs')`. This is technically a breaking change that
    should have landed with Node.js 12.0.0. It is necessary to have this to keep
    the possibility for a future minor version to load ES Modules with the
    require function (Myles Borins) [#27417](https://github.com/nodejs/node/pull/27417).
* **repl**:
  * The REPL now supports multi-line statements using `BigInt` literals as well
    as public and private class fields and methods (Ruben Bridgewater) [#27400](https://github.com/nodejs/node/pull/27400).
  * The REPL now supports tab autocompletion of file paths with `fs` methods (Anto Aravinth) [#26648](https://github.com/nodejs/node/pull/26648).
* **meta**:
  * Added [Christian Clauss](https://github.com/cclauss) to collaborators [#27554](https://github.com/nodejs/node/pull/27554).

### Commits

* \[[`c0ab2a141b`](https://github.com/nodejs/node/commit/c0ab2a141b)] - **assert**: use new language features (Ruben Bridgewater) [#27400](https://github.com/nodejs/node/pull/27400)
* \[[`4b3d0d1953`](https://github.com/nodejs/node/commit/4b3d0d1953)] - **async\_hooks**: fixup do not reuse HTTPParser (Gerhard Stoebich) [#27477](https://github.com/nodejs/node/pull/27477)
* \[[`cfc7bdd303`](https://github.com/nodejs/node/commit/cfc7bdd303)] - **benchmark**: add benchmark for node -p (Joyee Cheung) [#27320](https://github.com/nodejs/node/pull/27320)
* \[[`53eefeb73e`](https://github.com/nodejs/node/commit/53eefeb73e)] - **buffer**: remove unreachable code (Rich Trott) [#27445](https://github.com/nodejs/node/pull/27445)
* \[[`cac584d260`](https://github.com/nodejs/node/commit/cac584d260)] - **buffer,errors**: improve bigint, big numbers and more (Ruben Bridgewater) [#27228](https://github.com/nodejs/node/pull/27228)
* \[[`22a5a05785`](https://github.com/nodejs/node/commit/22a5a05785)] - **build**: delegate building from Makefile to ninja (Refael Ackermann) [#27504](https://github.com/nodejs/node/pull/27504)
* \[[`67205f5941`](https://github.com/nodejs/node/commit/67205f5941)] - **build**: remove unsupported Python 2.6 from configure (cclauss) [#27381](https://github.com/nodejs/node/pull/27381)
* \[[`615d386390`](https://github.com/nodejs/node/commit/615d386390)] - **child\_process**: only stop readable side of stream passed to proc (Anna Henningsen) [#27373](https://github.com/nodejs/node/pull/27373)
* \[[`8e876e60aa`](https://github.com/nodejs/node/commit/8e876e60aa)] - **console**: use consolePropAttributes for k-bind properties (reland) (Ruben Bridgewater) [#27352](https://github.com/nodejs/node/pull/27352)
* \[[`55804e1726`](https://github.com/nodejs/node/commit/55804e1726)] - **deps**: update llhttp to 1.1.2 (Fedor Indutny) [#27513](https://github.com/nodejs/node/pull/27513)
* \[[`f142363cfa`](https://github.com/nodejs/node/commit/f142363cfa)] - **deps**: update llhttp to 1.1.3 (Fedor Indutny) [#27595](https://github.com/nodejs/node/pull/27595)
* \[[`5f72246499`](https://github.com/nodejs/node/commit/5f72246499)] - **deps**: add acorn stage-3 plugins (Ruben Bridgewater) [#27400](https://github.com/nodejs/node/pull/27400)
* \[[`230a773e32`](https://github.com/nodejs/node/commit/230a773e32)] - **(SEMVER-MINOR)** **deps**: update archs files for OpenSSL-1.1.1b (Sam Roberts) [#27376](https://github.com/nodejs/node/pull/27376)
* \[[`b68132e01a`](https://github.com/nodejs/node/commit/b68132e01a)] - **(SEMVER-MINOR)** **deps**: configure OpenSSL's SSL\_trace to be built (Sam Roberts) [#27376](https://github.com/nodejs/node/pull/27376)
* \[[`7c25dce7ba`](https://github.com/nodejs/node/commit/7c25dce7ba)] - **deps**: V8: cherry-pick 5d0cf6b (Joyee Cheung) [#27423](https://github.com/nodejs/node/pull/27423)
* \[[`2c3c0d7d3e`](https://github.com/nodejs/node/commit/2c3c0d7d3e)] - **doc**: add cclauss to collaborators (cclauss) [#27554](https://github.com/nodejs/node/pull/27554)
* \[[`b51dcf62b8`](https://github.com/nodejs/node/commit/b51dcf62b8)] - **doc**: add Electron 6 to abi\_version\_registry (Jeremy Apthorp) [#27288](https://github.com/nodejs/node/pull/27288)
* \[[`cb97de7a9b`](https://github.com/nodejs/node/commit/cb97de7a9b)] - **doc**: move James back onto TSC (Michael Dawson) [#27411](https://github.com/nodejs/node/pull/27411)
* \[[`a9748bc124`](https://github.com/nodejs/node/commit/a9748bc124)] - **doc**: describe API ERR\_INVALID\_PROTOCOL context (Sam Roberts) [#27393](https://github.com/nodejs/node/pull/27393)
* \[[`a0353fdbe2`](https://github.com/nodejs/node/commit/a0353fdbe2)] - **fs**: align fs.ReadStream buffer pool writes to 8-byte boundary (ptaylor) [#24838](https://github.com/nodejs/node/pull/24838)
* \[[`7be1e0af44`](https://github.com/nodejs/node/commit/7be1e0af44)] - **fs**: added tests for util file preprocessSymlinkDestination (Ruwan Geeganage) [#27468](https://github.com/nodejs/node/pull/27468)
* \[[`f882c9b09b`](https://github.com/nodejs/node/commit/f882c9b09b)] - **(SEMVER-MINOR)** **http**: `servername === false` should disable SNI (Fedor Indutny) [#27316](https://github.com/nodejs/node/pull/27316)
* \[[`de337bb37c`](https://github.com/nodejs/node/commit/de337bb37c)] - **(SEMVER-MINOR)** **inspector**: implement --cpu-prof-interval (Joyee Cheung) [#27535](https://github.com/nodejs/node/pull/27535)
* \[[`9c842f4119`](https://github.com/nodejs/node/commit/9c842f4119)] - **lib**: remove Reflect.apply where appropriate (Anatoli Papirovski) [#27349](https://github.com/nodejs/node/pull/27349)
* \[[`47d311b3f0`](https://github.com/nodejs/node/commit/47d311b3f0)] - **lib**: remove outdated optimizations (Weijia Wang) [#27380](https://github.com/nodejs/node/pull/27380)
* \[[`c2a03d58c3`](https://github.com/nodejs/node/commit/c2a03d58c3)] - **lib**: print to stdout/stderr directly instead of using console (Joyee Cheung) [#27320](https://github.com/nodejs/node/pull/27320)
* \[[`b68ecf3e17`](https://github.com/nodejs/node/commit/b68ecf3e17)] - **meta**: move andrasq to Collaborator Emeriti list (Rich Trott) [#27546](https://github.com/nodejs/node/pull/27546)
* \[[`fd17f37a83`](https://github.com/nodejs/node/commit/fd17f37a83)] - **meta**: move stefanmb to Collaborator Emeriti list (Rich Trott) [#27502](https://github.com/nodejs/node/pull/27502)
* \[[`8495e8bceb`](https://github.com/nodejs/node/commit/8495e8bceb)] - **meta**: move Forrest Norvell to Collaborator Emeriti list (Rich Trott) [#27437](https://github.com/nodejs/node/pull/27437)
* \[[`7d1c90b614`](https://github.com/nodejs/node/commit/7d1c90b614)] - **meta**: move @vsemozhetbyt to collaborator emeriti (Vse Mozhet Byt) [#27412](https://github.com/nodejs/node/pull/27412)
* \[[`014a9fd46f`](https://github.com/nodejs/node/commit/014a9fd46f)] - **module**: throw on require('./path.mjs'); (Myles Borins) [#27417](https://github.com/nodejs/node/pull/27417)
* \[[`5bcd7700ca`](https://github.com/nodejs/node/commit/5bcd7700ca)] - **(SEMVER-MINOR)** **module**: add createRequire method (Myles Borins) [#27405](https://github.com/nodejs/node/pull/27405)
* \[[`be9a1ec1d1`](https://github.com/nodejs/node/commit/be9a1ec1d1)] - **module**: allow passing a directory to createRequireFromPath (Gilles De Mey) [#23818](https://github.com/nodejs/node/pull/23818)
* \[[`e5fdc30bd1`](https://github.com/nodejs/node/commit/e5fdc30bd1)] - **n-api**: make napi\_get\_property\_names return strings (Anna Henningsen) [#27524](https://github.com/nodejs/node/pull/27524)
* \[[`826fb66729`](https://github.com/nodejs/node/commit/826fb66729)] - **process**: compatibility patch to backport 1d022e8 (Ruben Bridgewater) [#27483](https://github.com/nodejs/node/pull/27483)
* \[[`91b7f5e103`](https://github.com/nodejs/node/commit/91b7f5e103)] - **process**: improve cwd performance (Ruben Bridgewater) [#27224](https://github.com/nodejs/node/pull/27224)
* \[[`05cea679a3`](https://github.com/nodejs/node/commit/05cea679a3)] - **repl**: handle stage-3 language features properly (Ruben Bridgewater) [#27400](https://github.com/nodejs/node/pull/27400)
* \[[`01d632d7e8`](https://github.com/nodejs/node/commit/01d632d7e8)] - **repl**: add new language features to top level await statements (Ruben Bridgewater) [#27400](https://github.com/nodejs/node/pull/27400)
* \[[`149412ca02`](https://github.com/nodejs/node/commit/149412ca02)] - **repl**: add autocomplete for filesystem modules (Anto Aravinth) [#26648](https://github.com/nodejs/node/pull/26648)
* \[[`a55457c713`](https://github.com/nodejs/node/commit/a55457c713)] - **report**: use const reference in node\_report.cc (gengjiawen) [#27479](https://github.com/nodejs/node/pull/27479)
* \[[`8724229155`](https://github.com/nodejs/node/commit/8724229155)] - **src**: make deleted function public in node\_native\_module.h (gengjiawen) [#27509](https://github.com/nodejs/node/pull/27509)
* \[[`1489d12735`](https://github.com/nodejs/node/commit/1489d12735)] - **src**: make deleted function public in node\_main\_instance.h (gengjiawen) [#27509](https://github.com/nodejs/node/pull/27509)
* \[[`294d2ea71d`](https://github.com/nodejs/node/commit/294d2ea71d)] - **(SEMVER-MINOR)** **src**: refactor V8ProfilerConnection::DispatchMessage() (Joyee Cheung) [#27535](https://github.com/nodejs/node/pull/27535)
* \[[`a758f9bdf5`](https://github.com/nodejs/node/commit/a758f9bdf5)] - **src**: remove node\_options-inl.h from header files (Sam Roberts) [#27538](https://github.com/nodejs/node/pull/27538)
* \[[`bb373d0def`](https://github.com/nodejs/node/commit/bb373d0def)] - **src**: remove unnecessary semicolons after macros (Yang Guo) [#27529](https://github.com/nodejs/node/pull/27529)
* \[[`0c9bc02b96`](https://github.com/nodejs/node/commit/0c9bc02b96)] - **src**: refactor V8ProfilerConnection to be more reusable (Joyee Cheung) [#27475](https://github.com/nodejs/node/pull/27475)
* \[[`c787bb85cd`](https://github.com/nodejs/node/commit/c787bb85cd)] - **src**: refactor profile initialization (Joyee Cheung) [#27475](https://github.com/nodejs/node/pull/27475)
* \[[`600048b1b7`](https://github.com/nodejs/node/commit/600048b1b7)] - **src**: move Environment::context out of strong properties (Joyee Cheung) [#27430](https://github.com/nodejs/node/pull/27430)
* \[[`33702913b1`](https://github.com/nodejs/node/commit/33702913b1)] - **src**: prefer v8::Global over node::Persistent (Anna Henningsen) [#27287](https://github.com/nodejs/node/pull/27287)
* \[[`9d6d45e7d2`](https://github.com/nodejs/node/commit/9d6d45e7d2)] - **stream**: remove TODO and add a description instead (Ruben Bridgewater) [#27086](https://github.com/nodejs/node/pull/27086)
* \[[`bb1eaeec75`](https://github.com/nodejs/node/commit/bb1eaeec75)] - **test**: mark test-tls-enable-trace-cli flaky (cjihrig) [#27559](https://github.com/nodejs/node/pull/27559)
* \[[`d648ecc488`](https://github.com/nodejs/node/commit/d648ecc488)] - **test**: improve test-async-hooks-http-parser-destroy (Rich Trott) [#27319](https://github.com/nodejs/node/pull/27319)
* \[[`ca720b3a55`](https://github.com/nodejs/node/commit/ca720b3a55)] - **test**: converting NghttpError to string in HTTP2 module (Ruwan Geeganage) [#27506](https://github.com/nodejs/node/pull/27506)
* \[[`99e4a576eb`](https://github.com/nodejs/node/commit/99e4a576eb)] - **test**: add mustCall to openssl-client-cert-engine (Boxuan Li) [#27474](https://github.com/nodejs/node/pull/27474)
* \[[`e1d88aa880`](https://github.com/nodejs/node/commit/e1d88aa880)] - **test**: document NODE\_COMMON\_PORT env var (cjihrig) [#27507](https://github.com/nodejs/node/pull/27507)
* \[[`66cf706521`](https://github.com/nodejs/node/commit/66cf706521)] - **test**: allow EAI\_FAIL in test-http-dns-error.js (cjihrig) [#27500](https://github.com/nodejs/node/pull/27500)
* \[[`df4246e3b6`](https://github.com/nodejs/node/commit/df4246e3b6)] - **test**: refactor and deflake test-tls-sni-server-client (Luigi Pinca) [#27426](https://github.com/nodejs/node/pull/27426)
* \[[`a278814818`](https://github.com/nodejs/node/commit/a278814818)] - **test**: make sure weak references are not GCed too early (Ruben Bridgewater) [#27482](https://github.com/nodejs/node/pull/27482)
* \[[`aa281d284a`](https://github.com/nodejs/node/commit/aa281d284a)] - **test**: better output for test-report-uv-handles.js (gengjiawen) [#27479](https://github.com/nodejs/node/pull/27479)
* \[[`86c27c6005`](https://github.com/nodejs/node/commit/86c27c6005)] - **test**: add mustcall in test-net-bytes-read.js (imhype) [#27471](https://github.com/nodejs/node/pull/27471)
* \[[`33fead3f5e`](https://github.com/nodejs/node/commit/33fead3f5e)] - _**Revert**_ "**test**: skip test-cpu-prof in debug builds with code cache" (Anna Henningsen) [#27469](https://github.com/nodejs/node/pull/27469)
* \[[`a9a85d6271`](https://github.com/nodejs/node/commit/a9a85d6271)] - **test**: check `napi_get_reference_value()` during finalization (Anna Henningsen) [#27470](https://github.com/nodejs/node/pull/27470)
* \[[`16af9435a0`](https://github.com/nodejs/node/commit/16af9435a0)] - **test**: remove flaky designation for test-tls-sni-option (Luigi Pinca) [#27425](https://github.com/nodejs/node/pull/27425)
* \[[`1b94d025bc`](https://github.com/nodejs/node/commit/1b94d025bc)] - **test**: add missing line breaks to keep-alive header of slow headers test (Shuhei Kagawa) [#27442](https://github.com/nodejs/node/pull/27442)
* \[[`fefbbd90af`](https://github.com/nodejs/node/commit/fefbbd90af)] - **test**: add tests for new language features (Ruben Bridgewater) [#27400](https://github.com/nodejs/node/pull/27400)
* \[[`3711684ccf`](https://github.com/nodejs/node/commit/3711684ccf)] - **test**: add mustCall for parallel/test-net-connect-paused-connection (sujunfei) [#27463](https://github.com/nodejs/node/pull/27463)
* \[[`0e4f8788eb`](https://github.com/nodejs/node/commit/0e4f8788eb)] - **test**: add mustCallAtLeast to test-fs-read-stream-resume.js (heben) [#27456](https://github.com/nodejs/node/pull/27456)
* \[[`e89b6fee3a`](https://github.com/nodejs/node/commit/e89b6fee3a)] - **test**: adding mustCall in test-fs-readfile-empty.js (陈健) [#27455](https://github.com/nodejs/node/pull/27455)
* \[[`457549b67d`](https://github.com/nodejs/node/commit/457549b67d)] - **test**: add common.mustCall in test-http-abort-client.js (OneNail) [#27449](https://github.com/nodejs/node/pull/27449)
* \[[`f4124d5ba5`](https://github.com/nodejs/node/commit/f4124d5ba5)] - **test**: add mustCall to http-abort-queued test (Yaphet Ye) [#27447](https://github.com/nodejs/node/pull/27447)
* \[[`e21f035666`](https://github.com/nodejs/node/commit/e21f035666)] - **test**: add mustCall in test-fs-readfilesync-pipe-large.js (sinoon) [#27458](https://github.com/nodejs/node/pull/27458)
* \[[`1dd0205f10`](https://github.com/nodejs/node/commit/1dd0205f10)] - **test**: add mustCall to test-dgram-connect-send-multi-buffer-copy.js (XGHeaven) [#27465](https://github.com/nodejs/node/pull/27465)
* \[[`0dfe5bebb2`](https://github.com/nodejs/node/commit/0dfe5bebb2)] - **test**: add test of policy about parse error (Daiki Ihara) [#26873](https://github.com/nodejs/node/pull/26873)
* \[[`eeab007b25`](https://github.com/nodejs/node/commit/eeab007b25)] - **test**: add mustCall to test-net-after-close test (xuqinggang) [#27459](https://github.com/nodejs/node/pull/27459)
* \[[`c1b04652f5`](https://github.com/nodejs/node/commit/c1b04652f5)] - **test**: add "mustCall" to test-fs-readfile-unlink (wuchenkai) [#27453](https://github.com/nodejs/node/pull/27453)
* \[[`b6c65c1351`](https://github.com/nodejs/node/commit/b6c65c1351)] - **test**: add missing ToC entries (cjihrig) [#27434](https://github.com/nodejs/node/pull/27434)
* \[[`66bff5071f`](https://github.com/nodejs/node/commit/66bff5071f)] - **test**: document report helper module (cjihrig) [#27434](https://github.com/nodejs/node/pull/27434)
* \[[`2c335928cd`](https://github.com/nodejs/node/commit/2c335928cd)] - **test**: document NODE\_SKIP\_FLAG\_CHECK (cjihrig) [#27434](https://github.com/nodejs/node/pull/27434)
* \[[`115d06cdbb`](https://github.com/nodejs/node/commit/115d06cdbb)] - **test**: document NODE\_TEST\_KNOWN\_GLOBALS (cjihrig) [#27434](https://github.com/nodejs/node/pull/27434)
* \[[`51fc672da9`](https://github.com/nodejs/node/commit/51fc672da9)] - **test**: add mustCallAtLeast to test-fs-read-stream-inherit (nilianzhu) [#27457](https://github.com/nodejs/node/pull/27457)
* \[[`4b9d109518`](https://github.com/nodejs/node/commit/4b9d109518)] - **test**: add mustCall to test-dgram-implicit-bind.js (Chenxi Yuan) [#27452](https://github.com/nodejs/node/pull/27452)
* \[[`c4d67f2af5`](https://github.com/nodejs/node/commit/c4d67f2af5)] - **test**: add common.mustCall test-dgram-listen-after-bind (zhoujiamin) [#27454](https://github.com/nodejs/node/pull/27454)
* \[[`23fb430e03`](https://github.com/nodejs/node/commit/23fb430e03)] - **test**: add mustCall to test-dgram-connect-send-callback-buffer (shenchen) [#27466](https://github.com/nodejs/node/pull/27466)
* \[[`a37ca245ff`](https://github.com/nodejs/node/commit/a37ca245ff)] - **test**: add mustCallAtLeast to test-fs-read-stream-fd test (hardfist) [#27461](https://github.com/nodejs/node/pull/27461)
* \[[`cf84f20453`](https://github.com/nodejs/node/commit/cf84f20453)] - **test**: skip fs-copyfile-respect-permission if root (Daniel Bevenius) [#27378](https://github.com/nodejs/node/pull/27378)
* \[[`7d80999454`](https://github.com/nodejs/node/commit/7d80999454)] - **test**: add mustCall to net-can-reset-timeout (xinyulee) [#27462](https://github.com/nodejs/node/pull/27462)
* \[[`9fa5ba8b3c`](https://github.com/nodejs/node/commit/9fa5ba8b3c)] - **test**: add mustCall to test-fs-readfile-pipe-large (luoyu) [#27460](https://github.com/nodejs/node/pull/27460)
* \[[`e8d5b6226a`](https://github.com/nodejs/node/commit/e8d5b6226a)] - **test**: add "mustCall" for test-net-buffersize (lixin.atom) [#27451](https://github.com/nodejs/node/pull/27451)
* \[[`d784ecb1ad`](https://github.com/nodejs/node/commit/d784ecb1ad)] - **test**: add mustCall to test-net-eaddrinuse test (tongshouyu) [#27448](https://github.com/nodejs/node/pull/27448)
* \[[`6fd1384a43`](https://github.com/nodejs/node/commit/6fd1384a43)] - **test**: add mustcall in test-dgram-connect-send-callback-buffer-length (jyjunyz) [#27464](https://github.com/nodejs/node/pull/27464)
* \[[`7a35077197`](https://github.com/nodejs/node/commit/7a35077197)] - **test**: add mustCall to test-fs-readfile-pipe (tonyhty) [#27450](https://github.com/nodejs/node/pull/27450)
* \[[`af29ae0344`](https://github.com/nodejs/node/commit/af29ae0344)] - **test**: add mustCall to net-connect-buffer test (Rongjian Zhang) [#27446](https://github.com/nodejs/node/pull/27446)
* \[[`bdabf699eb`](https://github.com/nodejs/node/commit/bdabf699eb)] - **(SEMVER-MINOR)** **tls**: add --tls-min-v1.2 CLI switch (Sam Roberts) [#27520](https://github.com/nodejs/node/pull/27520)
* \[[`7bbf951095`](https://github.com/nodejs/node/commit/7bbf951095)] - **tls**: disallow conflicting TLS protocol options (Sam Roberts) [#27521](https://github.com/nodejs/node/pull/27521)
* \[[`84a2768c25`](https://github.com/nodejs/node/commit/84a2768c25)] - **(SEMVER-MINOR)** **tls**: support enableTrace in TLSSocket() (cjihrig) [#27497](https://github.com/nodejs/node/pull/27497)
* \[[`576fe339a1`](https://github.com/nodejs/node/commit/576fe339a1)] - **(SEMVER-MINOR)** **tls**: simplify enableTrace logic (cjihrig) [#27497](https://github.com/nodejs/node/pull/27497)
* \[[`30a72e8c7b`](https://github.com/nodejs/node/commit/30a72e8c7b)] - **(SEMVER-MINOR)** **tls**: allow enabling the TLS debug trace (Sam Roberts) [#27376](https://github.com/nodejs/node/pull/27376)
* \[[`f1efe6dae0`](https://github.com/nodejs/node/commit/f1efe6dae0)] - **(SEMVER-MINOR)** **tls,cli**: add --trace-tls command-line flag (cjihrig) [#27497](https://github.com/nodejs/node/pull/27497)
* \[[`3d37414002`](https://github.com/nodejs/node/commit/3d37414002)] - **tools**: fix node-core/required-modules eslint rule (Ben Noordhuis) [#27545](https://github.com/nodejs/node/pull/27545)
* \[[`29e2793a87`](https://github.com/nodejs/node/commit/29e2793a87)] - **tools**: add Release and Debug symlinks to .gitignore (Gerhard Stoebich) [#27484](https://github.com/nodejs/node/pull/27484)
* \[[`76af4f0d05`](https://github.com/nodejs/node/commit/76af4f0d05)] - **tools**: prohibit `assert.doesNotReject()` in Node.js core (Ruben Bridgewater) [#27402](https://github.com/nodejs/node/pull/27402)
* \[[`95498df1cf`](https://github.com/nodejs/node/commit/95498df1cf)] - **util**: inspect constructor closer (Ruben Bridgewater) [#27522](https://github.com/nodejs/node/pull/27522)
* \[[`7b5bd93ced`](https://github.com/nodejs/node/commit/7b5bd93ced)] - **util**: compatibility patch to backport d0667e8 (Ruben Bridgewater) [#27570](https://github.com/nodejs/node/pull/27570)
* \[[`52d4f1febf`](https://github.com/nodejs/node/commit/52d4f1febf)] - **util**: improve function inspection (Ruben Bridgewater) [#27227](https://github.com/nodejs/node/pull/27227)
* \[[`caab7d4664`](https://github.com/nodejs/node/commit/caab7d4664)] - **util**: better number formatters (Ruben Bridgewater) [#27499](https://github.com/nodejs/node/pull/27499)

<a id="12.1.0"></a>

## 2019-04-29, Version 12.1.0 (Current), @targos

### Notable changes

* **intl**:
  * Update ICU to 64.2. This adds support for Japanese Era (Reiwa) (Ujjwal Sharma) [#27361](https://github.com/nodejs/node/pull/27361).
  * Fixes a bug in ICU that affected Node.js 12.0.0 in the case where `new Date().toLocaleString()` was called with a non-default locale (Steven R. Loomis) [#27415](https://github.com/nodejs/node/pull/27415).
* **C++ API**:
  * Added an overload `EmitAsyncDestroy` that can be used during garbage collection (Anna Henningsen) [#27255](https://github.com/nodejs/node/pull/27255).

### Commits

* \[[`8ca110cc6f`](https://github.com/nodejs/node/commit/8ca110cc6f)] - **benchmark**: fix http bench-parser.js (Rich Trott) [#27359](https://github.com/nodejs/node/pull/27359)
* \[[`2f9bafba08`](https://github.com/nodejs/node/commit/2f9bafba08)] - **bootstrap**: delay the instantiation of maps in per-context scripts (Joyee Cheung) [#27371](https://github.com/nodejs/node/pull/27371)
* \[[`e7026f1428`](https://github.com/nodejs/node/commit/e7026f1428)] - **build**: allow icu download to use other hashes besides md5 (Steven R. Loomis) [#27370](https://github.com/nodejs/node/pull/27370)
* \[[`50234460f9`](https://github.com/nodejs/node/commit/50234460f9)] - **build**: disable custom v8 snapshot by default (Joyee Cheung) [#27365](https://github.com/nodejs/node/pull/27365)
* \[[`b21b28f653`](https://github.com/nodejs/node/commit/b21b28f653)] - **crypto**: update root certificates (Sam Roberts) [#27374](https://github.com/nodejs/node/pull/27374)
* \[[`3282ccb845`](https://github.com/nodejs/node/commit/3282ccb845)] - **deps**: backport ICU-20575 to fix err/crasher (Steven R. Loomis) [#27435](https://github.com/nodejs/node/pull/27435)
* \[[`e922a22725`](https://github.com/nodejs/node/commit/e922a22725)] - **deps**: backport ICU-20558 to fix Intl crasher (Steven R. Loomis) [#27415](https://github.com/nodejs/node/pull/27415)
* \[[`d852d9e904`](https://github.com/nodejs/node/commit/d852d9e904)] - **deps**: update ICU to 64.2 (Ujjwal Sharma) [#27361](https://github.com/nodejs/node/pull/27361)
* \[[`ee80a210ff`](https://github.com/nodejs/node/commit/ee80a210ff)] - **dgram**: change 'this' to 'self' for 'isConnected' (MaleDong) [#27338](https://github.com/nodejs/node/pull/27338)
* \[[`8302148c83`](https://github.com/nodejs/node/commit/8302148c83)] - **doc**: add Node 12 to the first list of versions (Rivaldo Junior) [#27414](https://github.com/nodejs/node/pull/27414)
* \[[`f6ceefa4bd`](https://github.com/nodejs/node/commit/f6ceefa4bd)] - **doc**: update comment in bootstrap for primordials (Myles Borins) [#27398](https://github.com/nodejs/node/pull/27398)
* \[[`9c30806fb5`](https://github.com/nodejs/node/commit/9c30806fb5)] - **doc**: simplify GOVERNANCE.md text (Rich Trott) [#27354](https://github.com/nodejs/node/pull/27354)
* \[[`453510c7ef`](https://github.com/nodejs/node/commit/453510c7ef)] - **doc**: fix pull request number (Ruben Bridgewater) [#27336](https://github.com/nodejs/node/pull/27336)
* \[[`36762883a0`](https://github.com/nodejs/node/commit/36762883a0)] - **doc**: clarify behaviour of writeFile(fd) (Sam Roberts) [#27282](https://github.com/nodejs/node/pull/27282)
* \[[`f70588fb67`](https://github.com/nodejs/node/commit/f70588fb67)] - **doc**: fix v12.0.0 changelog id (Beth Griggs) [#27368](https://github.com/nodejs/node/pull/27368)
* \[[`a6d1fa5954`](https://github.com/nodejs/node/commit/a6d1fa5954)] - **doc**: simplify Collaborator pre-nomination text (Rich Trott) [#27348](https://github.com/nodejs/node/pull/27348)
* \[[`dd709fc84a`](https://github.com/nodejs/node/commit/dd709fc84a)] - **lib**: throw a special error in internal/assert (Joyee Cheung) [#26635](https://github.com/nodejs/node/pull/26635)
* \[[`4dfe54a89a`](https://github.com/nodejs/node/commit/4dfe54a89a)] - **module**: initialize module\_wrap.callbackMap during pre-execution (Joyee Cheung) [#27323](https://github.com/nodejs/node/pull/27323)
* \[[`8b5d73867f`](https://github.com/nodejs/node/commit/8b5d73867f)] - **(SEMVER-MINOR)** **n-api**: do not require JS Context for `napi_async_destroy()` (Anna Henningsen) [#27255](https://github.com/nodejs/node/pull/27255)
* \[[`d00014e599`](https://github.com/nodejs/node/commit/d00014e599)] - **process**: reduce the number of internal frames in async stack trace (Joyee Cheung) [#27392](https://github.com/nodejs/node/pull/27392)
* \[[`dc510fb435`](https://github.com/nodejs/node/commit/dc510fb435)] - **report**: print common items first for readability (gengjiawen) [#27367](https://github.com/nodejs/node/pull/27367)
* \[[`3614a00276`](https://github.com/nodejs/node/commit/3614a00276)] - **src**: refactor deprecated UVException in node\_file.cc (gengjiawen) [#27280](https://github.com/nodejs/node/pull/27280)
* \[[`071300b39d`](https://github.com/nodejs/node/commit/071300b39d)] - **src**: move OnMessage to node\_errors.cc (Joyee Cheung) [#27304](https://github.com/nodejs/node/pull/27304)
* \[[`81e7b49c8f`](https://github.com/nodejs/node/commit/81e7b49c8f)] - **src**: use predefined AliasedBuffer types in the code base (Joyee Cheung) [#27334](https://github.com/nodejs/node/pull/27334)
* \[[`8089d29567`](https://github.com/nodejs/node/commit/8089d29567)] - **src**: apply clang-tidy modernize-deprecated-headers found by Jenkins CI (gengjiawen) [#27279](https://github.com/nodejs/node/pull/27279)
* \[[`619c5b6eb3`](https://github.com/nodejs/node/commit/619c5b6eb3)] - **(SEMVER-MINOR)** **src**: do not require JS Context for `~AsyncResoure()` (Anna Henningsen) [#27255](https://github.com/nodejs/node/pull/27255)
* \[[`809cf595eb`](https://github.com/nodejs/node/commit/809cf595eb)] - **(SEMVER-MINOR)** **src**: add `Environment` overload of `EmitAsyncDestroy` (Anna Henningsen) [#27255](https://github.com/nodejs/node/pull/27255)
* \[[`7bc47cba2e`](https://github.com/nodejs/node/commit/7bc47cba2e)] - **src**: apply clang-tidy rule modernize-use-equals-default (gengjiawen) [#27264](https://github.com/nodejs/node/pull/27264)
* \[[`ad42cd69cf`](https://github.com/nodejs/node/commit/ad42cd69cf)] - **src**: use std::vector\<size\_t> instead of IndexArray (Joyee Cheung) [#27321](https://github.com/nodejs/node/pull/27321)
* \[[`228127fc67`](https://github.com/nodejs/node/commit/228127fc67)] - **src**: enable context snapshot after running per-context scripts (Joyee Cheung) [#27321](https://github.com/nodejs/node/pull/27321)
* \[[`45d6106129`](https://github.com/nodejs/node/commit/45d6106129)] - **src**: enable snapshot with per-isolate data (Joyee Cheung) [#27321](https://github.com/nodejs/node/pull/27321)
* \[[`631bea8fd2`](https://github.com/nodejs/node/commit/631bea8fd2)] - **src**: implement IsolateData serialization and deserialization (Joyee Cheung) [#27321](https://github.com/nodejs/node/pull/27321)
* \[[`a636338945`](https://github.com/nodejs/node/commit/a636338945)] - **src**: allow creating NodeMainInstance that does not own the isolate (Joyee Cheung) [#27321](https://github.com/nodejs/node/pull/27321)
* \[[`50732c1b3f`](https://github.com/nodejs/node/commit/50732c1b3f)] - **test**: refactor net-connect-handle-econnrefused (Luigi Pinca) [#27014](https://github.com/nodejs/node/pull/27014)
* \[[`e9021cc498`](https://github.com/nodejs/node/commit/e9021cc498)] - **test**: move test-net-connect-handle-econnrefused (Luigi Pinca) [#27014](https://github.com/nodejs/node/pull/27014)
* \[[`ebbed6047d`](https://github.com/nodejs/node/commit/ebbed6047d)] - **test**: rework to remove flakiness, and be parallel (Sam Roberts) [#27300](https://github.com/nodejs/node/pull/27300)
* \[[`f0b2992f5c`](https://github.com/nodejs/node/commit/f0b2992f5c)] - **test**: fix ineffective error tests (Masashi Hirano) [#27333](https://github.com/nodejs/node/pull/27333)
* \[[`d84a6d05a1`](https://github.com/nodejs/node/commit/d84a6d05a1)] - **test**: make test-worker-esm-missing-main more robust (Rich Trott) [#27340](https://github.com/nodejs/node/pull/27340)
* \[[`8486917b9a`](https://github.com/nodejs/node/commit/8486917b9a)] - **test**: increase coverage in lib/internal/dns/promises.js (Rich Trott) [#27330](https://github.com/nodejs/node/pull/27330)
* \[[`6ca0270320`](https://github.com/nodejs/node/commit/6ca0270320)] - **tls**: include invalid method name in thrown error (Sam Roberts) [#27390](https://github.com/nodejs/node/pull/27390)
* \[[`dcbe5b9dff`](https://github.com/nodejs/node/commit/dcbe5b9dff)] - **tools**: update certdata.txt (Sam Roberts) [#27374](https://github.com/nodejs/node/pull/27374)
* \[[`53f0ef36c0`](https://github.com/nodejs/node/commit/53f0ef36c0)] - **tools**: update LICENSE and tools/icu/current\_ver.dep (Ujjwal Sharma) [#27361](https://github.com/nodejs/node/pull/27361)
* \[[`481789c235`](https://github.com/nodejs/node/commit/481789c235)] - **tools**: fix use-after-free mkcodecache warning (Ben Noordhuis) [#27332](https://github.com/nodejs/node/pull/27332)
* \[[`d62a3243b1`](https://github.com/nodejs/node/commit/d62a3243b1)] - **tools**: update tools/license-builder.sh (Ujjwal Sharma) [#27362](https://github.com/nodejs/node/pull/27362)
* \[[`b44323f3de`](https://github.com/nodejs/node/commit/b44323f3de)] - **tools**: implement node\_mksnapshot (Joyee Cheung) [#27321](https://github.com/nodejs/node/pull/27321)
* \[[`ae2333db65`](https://github.com/nodejs/node/commit/ae2333db65)] - **util**: add prototype support for boxed primitives (Ruben Bridgewater) [#27351](https://github.com/nodejs/node/pull/27351)
* \[[`8f3442809a`](https://github.com/nodejs/node/commit/8f3442809a)] - **util**: rename setIteratorBraces to getIteratorBraces (Ruben Bridgewater) [#27342](https://github.com/nodejs/node/pull/27342)
* \[[`973d705aa9`](https://github.com/nodejs/node/commit/973d705aa9)] - **util**: improve `Symbol.toStringTag` handling (Ruben Bridgewater) [#27342](https://github.com/nodejs/node/pull/27342)

<a id="12.0.0"></a>

## 2019-04-23, Version 12.0.0 (Current), @BethGriggs

### Notable Changes

* **assert**:
  * validate required arguments (Ruben Bridgewater) [#26641](https://github.com/nodejs/node/pull/26641)
  * adjust loose assertions (Ruben Bridgewater) [#25008](https://github.com/nodejs/node/pull/25008)
* **async\_hooks**:
  * remove deprecated `emitBefore` and `emitAfter` (Matteo Collina) [#26530](https://github.com/nodejs/node/pull/26530)
  * remove promise object from resource (Andreas Madsen) [#23443](https://github.com/nodejs/node/pull/23443)
* **bootstrap**: make Buffer and process non-enumerable (Ruben Bridgewater) [#24874](https://github.com/nodejs/node/pull/24874)
* **buffer**:
  * use stricter range checks (Ruben Bridgewater) [#27045](https://github.com/nodejs/node/pull/27045)
  * harden `SlowBuffer` creation (ZYSzys) [#26272](https://github.com/nodejs/node/pull/26272)
  * harden validation of buffer allocation size (ZYSzys) [#26162](https://github.com/nodejs/node/pull/26162)
  * do proper error propagation in addon methods (Anna Henningsen) [#23939](https://github.com/nodejs/node/pull/23939)
* **child\_process**:
  * remove `options.customFds` (cjihrig) [#25279](https://github.com/nodejs/node/pull/25279)
  * harden fork arguments validation (ZYSzys) [#27039](https://github.com/nodejs/node/pull/27039)
  * use non-infinite `maxBuffer` defaults (kohta ito) [#23027](https://github.com/nodejs/node/pull/23027)
* **console**: don't use ANSI escape codes when `TERM=dumb` (Vladislav Kaminsky) [#26261](https://github.com/nodejs/node/pull/26261)
* **crypto**:
  * remove legacy native handles (Tobias Nießen) [#27011](https://github.com/nodejs/node/pull/27011)
  * decode missing passphrase errors (Tobias Nießen) [#25208](https://github.com/nodejs/node/pull/25208)
  * remove `Cipher.setAuthTag()` and `Decipher.getAuthTag()` (Tobias Nießen) [#26249](https://github.com/nodejs/node/pull/26249)
  * remove deprecated `crypto._toBuf()` (Tobias Nießen) [#25338](https://github.com/nodejs/node/pull/25338)
  * set `DEFAULT_ENCODING` property to non-enumerable (Antoine du Hamel) [#23222](https://github.com/nodejs/node/pull/23222)
* **deps**:
  * update V8 to 7.4.288.13 (Michaël Zasso, cjihrig, Refael Ackermann, Anna Henningsen, Ujjwal Sharma) [#26685](https://github.com/nodejs/node/pull/26685)
  * bump minimum icu version to 63 (Ujjwal Sharma) [#25852](https://github.com/nodejs/node/pull/25852)
  * update OpenSSL to 1.1.1b (Sam Roberts, Shigeki Ohtsu) [#26327](https://github.com/nodejs/node/pull/26327)
* **errors**: update error name (Ruben Bridgewater) [#26738](https://github.com/nodejs/node/pull/26738)
* **fs**:
  * use proper .destroy() implementation for SyncWriteStream (Matteo Collina) [#26690](https://github.com/nodejs/node/pull/26690)
  * improve mode validation (Ruben Bridgewater) [#26575](https://github.com/nodejs/node/pull/26575)
  * harden validation of start option in `createWriteStream()` (ZYSzys) [#25579](https://github.com/nodejs/node/pull/25579)
  * make writeFile consistent with readFile wrt fd (Sakthipriyan Vairamani (thefourtheye)) [#23709](https://github.com/nodejs/node/pull/23709)
* **http**:
  * validate timeout in `ClientRequest()` (cjihrig) [#26214](https://github.com/nodejs/node/pull/26214)
  * return HTTP 431 on `HPE_HEADER_OVERFLOW` error (Albert Still) [#25605](https://github.com/nodejs/node/pull/25605)
  * switch default parser to llhttp (Anna Henningsen) [#24870](https://github.com/nodejs/node/pull/24870)
  * Runtime-deprecate `outgoingMessage._headers` and `outgoingMessage._headerNames` (Morgan Roderick) [#24167](https://github.com/nodejs/node/pull/24167)
* **lib**:
  * remove `Atomics.wake()` (Gus Caplan) [#27033](https://github.com/nodejs/node/pull/27033)
  * move DTRACE\_\* probes out of global scope (James M Snell) [#26541](https://github.com/nodejs/node/pull/26541)
  * deprecate `_stream_wrap` (Sam Roberts) [#26245](https://github.com/nodejs/node/pull/26245)
  * use ES6 class inheritance style (Ruben Bridgewater) [#24755](https://github.com/nodejs/node/pull/24755)
* **module**:
  * remove unintended access to deps/ (Anna Henningsen) [#25138](https://github.com/nodejs/node/pull/25138)
  * improve error message for MODULE\_NOT\_FOUND (Ali Ijaz Sheikh) [#25690](https://github.com/nodejs/node/pull/25690)
  * requireStack property for MODULE\_NOT\_FOUND (Ali Ijaz Sheikh) [#25690](https://github.com/nodejs/node/pull/25690)
  * remove dead code (Ruben Bridgewater) [#26983](https://github.com/nodejs/node/pull/26983)
  * make `require('.')` never resolve outside the current directory (Ruben Bridgewater) [#26973](https://github.com/nodejs/node/pull/26973)
  * throw an error for invalid package.json main entries (Ruben Bridgewater) [#26823](https://github.com/nodejs/node/pull/26823)
  * don't search in `require.resolve.paths` (cjihrig) [#23683](https://github.com/nodejs/node/pull/23683)
* **net**:
  * remove `Server.listenFD()` (cjihrig) [#27127](https://github.com/nodejs/node/pull/27127)
  * do not add `.host` and `.port` properties to DNS error (Ruben Bridgewater) [#26751](https://github.com/nodejs/node/pull/26751)
  * emit "write after end" errors in the next tick (Ouyang Yadong) [#24457](https://github.com/nodejs/node/pull/24457)
  * deprecate `_setSimultaneousAccepts()` undocumented function (James M Snell) [#23760](https://github.com/nodejs/node/pull/23760)
* **os**:
  * implement `os.type()` using `uv_os_uname()` (cjihrig) [#25659](https://github.com/nodejs/node/pull/25659)
  * remove `os.getNetworkInterfaces()` (cjihrig) [#25280](https://github.com/nodejs/node/pull/25280)
* **process**:
  * make global.process, global.Buffer getters (Guy Bedford) [#26882](https://github.com/nodejs/node/pull/26882)
  * move DEP0062 (node --debug) to end-of-life (Joyee Cheung) [#25828](https://github.com/nodejs/node/pull/25828)
  * exit on --debug and --debug-brk after option parsing (Joyee Cheung) [#25828](https://github.com/nodejs/node/pull/25828)
  * improve `--redirect-warnings` handling (Ruben Bridgewater) [#24965](https://github.com/nodejs/node/pull/24965)
* **readline**: support TERM=dumb (Vladislav Kaminsky) [#26261](https://github.com/nodejs/node/pull/26261)
* **repl**:
  * add welcome message (gengjiawen) [#25947](https://github.com/nodejs/node/pull/25947)
  * fix terminal default setting (Ruben Bridgewater) [#26518](https://github.com/nodejs/node/pull/26518)
  * check colors with `.getColorDepth()` (Vladislav Kaminsky) [#26261](https://github.com/nodejs/node/pull/26261)
  * deprecate REPLServer.rli (Ruben Bridgewater) [#26260](https://github.com/nodejs/node/pull/26260)
* **src**:
  * remove unused `INT_MAX` constant (Sam Roberts) [#27078](https://github.com/nodejs/node/pull/27078)
  * update `NODE_MODULE_VERSION` to 72 (Ujjwal Sharma) [#26685](https://github.com/nodejs/node/pull/26685)
  * remove `AddPromiseHook()` (Anna Henningsen) [#26574](https://github.com/nodejs/node/pull/26574)
  * clean up `MultiIsolatePlatform` interface (Anna Henningsen) [#26384](https://github.com/nodejs/node/pull/26384)
  * properly configure default heap limits (Ali Ijaz Sheikh) [#25576](https://github.com/nodejs/node/pull/25576)
  * remove `icuDataDir` from node config (GauthamBanasandra) [#24780](https://github.com/nodejs/node/pull/24780)
* **tls**:
  * support TLSv1.3 (Sam Roberts) [#26209](https://github.com/nodejs/node/pull/26209)
  * return correct version from `getCipher()` (Sam Roberts) [#26625](https://github.com/nodejs/node/pull/26625)
  * check arg types of renegotiate() (Sam Roberts) [#25876](https://github.com/nodejs/node/pull/25876)
  * add code for `ERR_TLS_INVALID_PROTOCOL_METHOD` (Sam Roberts) [#24729](https://github.com/nodejs/node/pull/24729)
  * emit a warning when servername is an IP address (Rodger Combs) [#23329](https://github.com/nodejs/node/pull/23329)
  * disable TLS v1.0 and v1.1 by default (Ben Noordhuis) [#23814](https://github.com/nodejs/node/pull/23814)
  * remove unused arg to createSecureContext() (Sam Roberts) [#24241](https://github.com/nodejs/node/pull/24241)
  * deprecate `Server.prototype.setOptions()` (cjihrig) [#23820](https://github.com/nodejs/node/pull/23820)
  * load `NODE_EXTRA_CA_CERTS` at startup (Ouyang Yadong) [#23354](https://github.com/nodejs/node/pull/23354)
* **util**:
  * remove `util.print()`, `util.puts()`, `util.debug()` and `util.error()` (cjihrig) [#25377](https://github.com/nodejs/node/pull/25377)
  * change inspect compact and breakLength default (Ruben Bridgewater) [#27109](https://github.com/nodejs/node/pull/27109)
  * improve inspect edge cases (Ruben Bridgewater) [#27109](https://github.com/nodejs/node/pull/27109)
  * only the first line of the error message (Simon Zünd) [#26685](https://github.com/nodejs/node/pull/26685)
  * don't set the prototype of callbackified functions (Ruben Bridgewater) [#26893](https://github.com/nodejs/node/pull/26893)
  * rename callbackified function (Ruben Bridgewater) [#26893](https://github.com/nodejs/node/pull/26893)
  * increase function length when using `callbackify()` (Ruben Bridgewater) [#26893](https://github.com/nodejs/node/pull/26893)
  * prevent tampering with internals in `inspect()` (Ruben Bridgewater) [#26577](https://github.com/nodejs/node/pull/26577)
  * prevent Proxy traps being triggered by `.inspect()` (Ruben Bridgewater) [#26241](https://github.com/nodejs/node/pull/26241)
  * prevent leaking internal properties (Ruben Bridgewater) [#24971](https://github.com/nodejs/node/pull/24971)
  * protect against monkeypatched Object prototype for inspect() (Rich Trott) [#25953](https://github.com/nodejs/node/pull/25953)
  * treat format arguments equally (Roman Reiss) [#23162](https://github.com/nodejs/node/pull/23162)
* **win, fs**: detect if symlink target is a directory (Bartosz Sosnowski) [#23724](https://github.com/nodejs/node/pull/23724)
* **zlib**:
  * throw TypeError if callback is missing (Anna Henningsen) [#24929](https://github.com/nodejs/node/pull/24929)
  * make “bare” constants un-enumerable (Anna Henningsen) [#24824](https://github.com/nodejs/node/pull/24824)

### Semver-Major Commits

* \[[`afce912193`](https://github.com/nodejs/node/commit/afce912193)] - **(SEMVER-MAJOR)** **assert**: improve performance to instantiate errors (Ruben Bridgewater) [#26738](https://github.com/nodejs/node/pull/26738)
* \[[`5a3623af74`](https://github.com/nodejs/node/commit/5a3623af74)] - **(SEMVER-MAJOR)** **assert**: validate required arguments (Ruben Bridgewater) [#26641](https://github.com/nodejs/node/pull/26641)
* \[[`7493db21b6`](https://github.com/nodejs/node/commit/7493db21b6)] - **(SEMVER-MAJOR)** **assert**: adjust loose assertions (Ruben Bridgewater) [#25008](https://github.com/nodejs/node/pull/25008)
* \[[`9d064439e5`](https://github.com/nodejs/node/commit/9d064439e5)] - **(SEMVER-MAJOR)** **async\_hooks**: remove deprecated emitBefore and emitAfter (Matteo Collina) [#26530](https://github.com/nodejs/node/pull/26530)
* \[[`1a2cf6696f`](https://github.com/nodejs/node/commit/1a2cf6696f)] - **(SEMVER-MAJOR)** **async\_hooks**: remove promise object from resource (Andreas Madsen) [#23443](https://github.com/nodejs/node/pull/23443)
* \[[`c992639fbd`](https://github.com/nodejs/node/commit/c992639fbd)] - **(SEMVER-MAJOR)** **bootstrap**: make Buffer and process non-enumerable (Ruben Bridgewater) [#24874](https://github.com/nodejs/node/pull/24874)
* \[[`693401d0dd`](https://github.com/nodejs/node/commit/693401d0dd)] - **(SEMVER-MAJOR)** **buffer**: use stricter range checks (Ruben Bridgewater) [#27045](https://github.com/nodejs/node/pull/27045)
* \[[`6113ba96cb`](https://github.com/nodejs/node/commit/6113ba96cb)] - **(SEMVER-MAJOR)** **buffer**: harden SlowBuffer creation (ZYSzys) [#26272](https://github.com/nodejs/node/pull/26272)
* \[[`6fb7baf935`](https://github.com/nodejs/node/commit/6fb7baf935)] - **(SEMVER-MAJOR)** **buffer**: harden validation of buffer allocation size (ZYSzys) [#26162](https://github.com/nodejs/node/pull/26162)
* \[[`c6d29ccf5a`](https://github.com/nodejs/node/commit/c6d29ccf5a)] - **(SEMVER-MAJOR)** **buffer**: do proper error propagation in addon methods (Anna Henningsen) [#23939](https://github.com/nodejs/node/pull/23939)
* \[[`a7d7d4dfb7`](https://github.com/nodejs/node/commit/a7d7d4dfb7)] - **(SEMVER-MAJOR)** **build**: increase MACOS\_DEPLOYMENT\_TARGET to 10.10 (Rod Vagg) [#27275](https://github.com/nodejs/node/pull/27275)
* \[[`561327702d`](https://github.com/nodejs/node/commit/561327702d)] - **(SEMVER-MAJOR)** **build**: reset embedder string to "-node.0" (Ujjwal Sharma) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`dfcc918e65`](https://github.com/nodejs/node/commit/dfcc918e65)] - **(SEMVER-MAJOR)** **build**: reset embedder string to "-node.0" (Michaël Zasso) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`9334e45aa0`](https://github.com/nodejs/node/commit/9334e45aa0)] - **(SEMVER-MAJOR)** **build**: remove mips support (Ben Noordhuis) [#26192](https://github.com/nodejs/node/pull/26192)
* \[[`bb564a3688`](https://github.com/nodejs/node/commit/bb564a3688)] - **(SEMVER-MAJOR)** **build**: update prerequisites on progress towards Python 3 (cclauss) [#25766](https://github.com/nodejs/node/pull/25766)
* \[[`3c332abe28`](https://github.com/nodejs/node/commit/3c332abe28)] - **(SEMVER-MAJOR)** **build**: reset embedder string to "-node.0" (Michaël Zasso) [#23423](https://github.com/nodejs/node/pull/23423)
* \[[`765766be64`](https://github.com/nodejs/node/commit/765766be64)] - **(SEMVER-MAJOR)** **build**: add common `defines` (Refael Ackermann) [#23426](https://github.com/nodejs/node/pull/23426)
* \[[`3b5773fee3`](https://github.com/nodejs/node/commit/3b5773fee3)] - **(SEMVER-MAJOR)** **build,deps**: move gypfiles out 2/2 - moving (Refael Ackermann) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`3531fe9320`](https://github.com/nodejs/node/commit/3531fe9320)] - **(SEMVER-MAJOR)** **build,deps**: add `NOMINMAX` to V8 Windows builds (Refael Ackermann) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`ff5d632a83`](https://github.com/nodejs/node/commit/ff5d632a83)] - **(SEMVER-MAJOR)** **build,deps**: fix V8 snapshot gyp dependencies (Refael Ackermann) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`ecf98b0839`](https://github.com/nodejs/node/commit/ecf98b0839)] - **(SEMVER-MAJOR)** **build,meta**: quiet/pretty make output by default (Refael Ackermann) [#26740](https://github.com/nodejs/node/pull/26740)
* \[[`2f477bd34d`](https://github.com/nodejs/node/commit/2f477bd34d)] - **(SEMVER-MAJOR)** **build,win**: mark x86 image as not SAFESEH (Refael Ackermann) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`652877e3a9`](https://github.com/nodejs/node/commit/652877e3a9)] - **(SEMVER-MAJOR)** **child\_process**: change the defaults maxBuffer size (kohta ito) [#27179](https://github.com/nodejs/node/pull/27179)
* \[[`9ad5106934`](https://github.com/nodejs/node/commit/9ad5106934)] - **(SEMVER-MAJOR)** **child\_process**: harden fork arguments validation (ZYSzys) [#27039](https://github.com/nodejs/node/pull/27039)
* \[[`eb8a51a35c`](https://github.com/nodejs/node/commit/eb8a51a35c)] - **(SEMVER-MAJOR)** **child\_process**: use non-infinite maxBuffer defaults (kohta ito) [#23027](https://github.com/nodejs/node/pull/23027)
* \[[`99523758dc`](https://github.com/nodejs/node/commit/99523758dc)] - **(SEMVER-MAJOR)** **console**: don't use ANSI escape codes when TERM=dumb (Vladislav Kaminsky) [#26261](https://github.com/nodejs/node/pull/26261)
* \[[`2f1ed5c063`](https://github.com/nodejs/node/commit/2f1ed5c063)] - **(SEMVER-MAJOR)** **crypto**: remove legacy native handles (Tobias Nießen) [#27011](https://github.com/nodejs/node/pull/27011)
* \[[`2e2c015422`](https://github.com/nodejs/node/commit/2e2c015422)] - **(SEMVER-MAJOR)** **crypto**: decode missing passphrase errors (Tobias Nießen) [#25208](https://github.com/nodejs/node/pull/25208)
* \[[`b8018f407b`](https://github.com/nodejs/node/commit/b8018f407b)] - **(SEMVER-MAJOR)** **crypto**: move DEP0113 to End-of-Life (Tobias Nießen) [#26249](https://github.com/nodejs/node/pull/26249)
* \[[`bf3cb3f9b1`](https://github.com/nodejs/node/commit/bf3cb3f9b1)] - **(SEMVER-MAJOR)** **crypto**: remove deprecated crypto.\_toBuf (Tobias Nießen) [#25338](https://github.com/nodejs/node/pull/25338)
* \[[`0f63d84f80`](https://github.com/nodejs/node/commit/0f63d84f80)] - **(SEMVER-MAJOR)** **crypto**: set `DEFAULT_ENCODING` property to non-enumerable (Antoine du Hamel) [#23222](https://github.com/nodejs/node/pull/23222)
* \[[`95e779a6e9`](https://github.com/nodejs/node/commit/95e779a6e9)] - **(SEMVER-MAJOR)** **deps**: silence irrelevant V8 warning (Michaël Zasso) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`08efd3060d`](https://github.com/nodejs/node/commit/08efd3060d)] - **(SEMVER-MAJOR)** **deps**: update postmortem metadata generation script (cjihrig) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`0da7e99f98`](https://github.com/nodejs/node/commit/0da7e99f98)] - **(SEMVER-MAJOR)** **deps**: V8: un-cherry-pick bd019bd (Refael Ackermann) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`b1015e0de8`](https://github.com/nodejs/node/commit/b1015e0de8)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 6 commits (Michaël Zasso) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`8181811d73`](https://github.com/nodejs/node/commit/8181811d73)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick d82c9af (Anna Henningsen) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`1f03fb4d49`](https://github.com/nodejs/node/commit/1f03fb4d49)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick e5f01ba (Anna Henningsen) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`e6af2207a9`](https://github.com/nodejs/node/commit/e6af2207a9)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick d5f08e4 (Anna Henningsen) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`963061bc02`](https://github.com/nodejs/node/commit/963061bc02)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 6b09d21 (Anna Henningsen) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`b7338b700f`](https://github.com/nodejs/node/commit/b7338b700f)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick f0bb5d2 (Anna Henningsen) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`02171949a0`](https://github.com/nodejs/node/commit/02171949a0)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 5b0510d (Anna Henningsen) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`bf572c7831`](https://github.com/nodejs/node/commit/bf572c7831)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 91f0cd0 (Anna Henningsen) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`09f134fccf`](https://github.com/nodejs/node/commit/09f134fccf)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 392316d (Anna Henningsen) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`53ea813d5c`](https://github.com/nodejs/node/commit/53ea813d5c)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 2f79d68 (Anna Henningsen) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`cc75ba3f14`](https://github.com/nodejs/node/commit/cc75ba3f14)] - **(SEMVER-MAJOR)** **deps**: sync V8 gypfiles with 7.4 (Ujjwal Sharma) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`f579e11940`](https://github.com/nodejs/node/commit/f579e11940)] - **(SEMVER-MAJOR)** **deps**: update V8 to 7.4.288.13 (Ujjwal Sharma) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`e0b3de1e90`](https://github.com/nodejs/node/commit/e0b3de1e90)] - **(SEMVER-MAJOR)** **deps**: bump minimum icu version to 63 (Ujjwal Sharma) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`1c494b0a95`](https://github.com/nodejs/node/commit/1c494b0a95)] - **(SEMVER-MAJOR)** **deps**: silence irrelevant V8 warnings (Michaël Zasso) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`cec35a5eb9`](https://github.com/nodejs/node/commit/cec35a5eb9)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 7803fa6 (Jon Kunkee) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`0d4d6b39a7`](https://github.com/nodejs/node/commit/0d4d6b39a7)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 58cefed (Jon Kunkee) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`bea1a386a3`](https://github.com/nodejs/node/commit/bea1a386a3)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick d3308d0 (Michaël Zasso) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`cf649c9b02`](https://github.com/nodejs/node/commit/cf649c9b02)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 74571c8 (Michaël Zasso) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`44d5401b8d`](https://github.com/nodejs/node/commit/44d5401b8d)] - **(SEMVER-MAJOR)** **deps**: cherry-pick fc0ddf5 from upstream V8 (Anna Henningsen) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`cefb8029cd`](https://github.com/nodejs/node/commit/cefb8029cd)] - **(SEMVER-MAJOR)** **deps**: sync V8 gypfiles with 7.3 (Ujjwal Sharma) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`d266e3e2cf`](https://github.com/nodejs/node/commit/d266e3e2cf)] - **(SEMVER-MAJOR)** **deps**: sync V8 gypfiles with 7.2 (Michaël Zasso) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`7b48713334`](https://github.com/nodejs/node/commit/7b48713334)] - **(SEMVER-MAJOR)** **deps**: update V8 to 7.3.492.25 (Michaël Zasso) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`6df7bd6c3b`](https://github.com/nodejs/node/commit/6df7bd6c3b)] - **(SEMVER-MAJOR)** **deps**: add s390 asm rules for OpenSSL-1.1.1 (Shigeki Ohtsu) [#19794](https://github.com/nodejs/node/pull/19794)
* \[[`5620727f30`](https://github.com/nodejs/node/commit/5620727f30)] - **(SEMVER-MAJOR)** **deps**: sync V8 gypfiles with 7.1 (Refael Ackermann) [#23423](https://github.com/nodejs/node/pull/23423)
* \[[`9b4bf7de6c`](https://github.com/nodejs/node/commit/9b4bf7de6c)] - **(SEMVER-MAJOR)** **deps**: update V8 to 7.1.302.28 (Michaël Zasso) [#23423](https://github.com/nodejs/node/pull/23423)
* \[[`3d8b844112`](https://github.com/nodejs/node/commit/3d8b844112)] - **(SEMVER-MAJOR)** **deps,build**: move gypfiles out 1/2 - required changes (Refael Ackermann) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`fff922afee`](https://github.com/nodejs/node/commit/fff922afee)] - **(SEMVER-MAJOR)** **deps,build**: compute torque\_outputs in v8.gyp (Refael Ackermann) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`4507246adc`](https://github.com/nodejs/node/commit/4507246adc)] - **(SEMVER-MAJOR)** **deps,build**: refactor v8 gypfiles (Refael Ackermann) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`b581d59655`](https://github.com/nodejs/node/commit/b581d59655)] - **(SEMVER-MAJOR)** **doc**: update supported platforms for Node.js 12 (Rod Vagg) [#26714](https://github.com/nodejs/node/pull/26714)
* \[[`309e7723ea`](https://github.com/nodejs/node/commit/309e7723ea)] - **(SEMVER-MAJOR)** **doc**: update behaviour of fs.writeFile (Sakthipriyan Vairamani (thefourtheye)) [#25080](https://github.com/nodejs/node/pull/25080)
* \[[`89740a4f0e`](https://github.com/nodejs/node/commit/89740a4f0e)] - **(SEMVER-MAJOR)** **doc**: add internal functionality details of util.inherits (Ruben Bridgewater) [#24755](https://github.com/nodejs/node/pull/24755)
* \[[`1ed3c54ecb`](https://github.com/nodejs/node/commit/1ed3c54ecb)] - **(SEMVER-MAJOR)** **errors**: update error name (Ruben Bridgewater) [#26738](https://github.com/nodejs/node/pull/26738)
* \[[`abafd38c8d`](https://github.com/nodejs/node/commit/abafd38c8d)] - **(SEMVER-MAJOR)** **fs**: use proper .destroy() implementation for SyncWriteStream (Matteo Collina) [#26690](https://github.com/nodejs/node/pull/26690)
* \[[`1cdeb9f956`](https://github.com/nodejs/node/commit/1cdeb9f956)] - **(SEMVER-MAJOR)** **fs**: improve mode validation (Ruben Bridgewater) [#26575](https://github.com/nodejs/node/pull/26575)
* \[[`70f4f08a9f`](https://github.com/nodejs/node/commit/70f4f08a9f)] - **(SEMVER-MAJOR)** **fs**: harden validation of start option in createWriteStream (ZYSzys) [#25579](https://github.com/nodejs/node/pull/25579)
* \[[`8f4b924f4a`](https://github.com/nodejs/node/commit/8f4b924f4a)] - **(SEMVER-MAJOR)** **fs**: make writeFile consistent with readFile wrt fd (Sakthipriyan Vairamani (thefourtheye)) [#23709](https://github.com/nodejs/node/pull/23709)
* \[[`907941d48e`](https://github.com/nodejs/node/commit/907941d48e)] - **(SEMVER-MAJOR)** **http**: validate timeout in ClientRequest() (cjihrig) [#26214](https://github.com/nodejs/node/pull/26214)
* \[[`bcf2886a84`](https://github.com/nodejs/node/commit/bcf2886a84)] - **(SEMVER-MAJOR)** **http**: return HTTP 431 on HPE\_HEADER\_OVERFLOW error (Albert Still) [#25605](https://github.com/nodejs/node/pull/25605)
* \[[`2cb8f24751`](https://github.com/nodejs/node/commit/2cb8f24751)] - **(SEMVER-MAJOR)** **http**: switch default parser to llhttp (Anna Henningsen) [#24870](https://github.com/nodejs/node/pull/24870)
* \[[`91748dd89c`](https://github.com/nodejs/node/commit/91748dd89c)] - **(SEMVER-MAJOR)** **http**: change DEP0066 to a runtime deprecation (Morgan Roderick) [#24167](https://github.com/nodejs/node/pull/24167)
* \[[`f3b49cfa7b`](https://github.com/nodejs/node/commit/f3b49cfa7b)] - **(SEMVER-MAJOR)** **http**: else case is not reachable (szabolcsit) [#24176](https://github.com/nodejs/node/pull/24176)
* \[[`bd9109c241`](https://github.com/nodejs/node/commit/bd9109c241)] - **(SEMVER-MAJOR)** **lib**: move DEP0021 to end of life (cjihrig) [#27127](https://github.com/nodejs/node/pull/27127)
* \[[`15c0947fee`](https://github.com/nodejs/node/commit/15c0947fee)] - **(SEMVER-MAJOR)** **lib**: remove Atomics.wake (Gus Caplan) [#27033](https://github.com/nodejs/node/pull/27033)
* \[[`3fe1e80896`](https://github.com/nodejs/node/commit/3fe1e80896)] - **(SEMVER-MAJOR)** **lib**: validate Error.captureStackTrace() calls (Ruben Bridgewater) [#26738](https://github.com/nodejs/node/pull/26738)
* \[[`bfbce289c3`](https://github.com/nodejs/node/commit/bfbce289c3)] - **(SEMVER-MAJOR)** **lib**: refactor Error.captureStackTrace() usage (Ruben Bridgewater) [#26738](https://github.com/nodejs/node/pull/26738)
* \[[`f9ddbb6b2f`](https://github.com/nodejs/node/commit/f9ddbb6b2f)] - **(SEMVER-MAJOR)** **lib**: move DTRACE\_\* probes out of global scope (James M Snell) [#26541](https://github.com/nodejs/node/pull/26541)
* \[[`c7e628f8b3`](https://github.com/nodejs/node/commit/c7e628f8b3)] - **(SEMVER-MAJOR)** **lib**: deprecate \_stream\_wrap (Sam Roberts) [#26245](https://github.com/nodejs/node/pull/26245)
* \[[`be78266fb3`](https://github.com/nodejs/node/commit/be78266fb3)] - **(SEMVER-MAJOR)** **lib**: don't use `util.inspect()` internals (Ruben Bridgewater) [#24971](https://github.com/nodejs/node/pull/24971)
* \[[`a02e3e2d5f`](https://github.com/nodejs/node/commit/a02e3e2d5f)] - **(SEMVER-MAJOR)** **lib**: improve error message for MODULE\_NOT\_FOUND (Ali Ijaz Sheikh) [#25690](https://github.com/nodejs/node/pull/25690)
* \[[`05cd1a0929`](https://github.com/nodejs/node/commit/05cd1a0929)] - **(SEMVER-MAJOR)** **lib**: requireStack property for MODULE\_NOT\_FOUND (Ali Ijaz Sheikh) [#25690](https://github.com/nodejs/node/pull/25690)
* \[[`29d3d1ea13`](https://github.com/nodejs/node/commit/29d3d1ea13)] - **(SEMVER-MAJOR)** **lib**: move DEP0029 to end of life (cjihrig) [#25377](https://github.com/nodejs/node/pull/25377)
* \[[`a665d13ad9`](https://github.com/nodejs/node/commit/a665d13ad9)] - **(SEMVER-MAJOR)** **lib**: move DEP0028 to end of life (cjihrig) [#25377](https://github.com/nodejs/node/pull/25377)
* \[[`10df21b071`](https://github.com/nodejs/node/commit/10df21b071)] - **(SEMVER-MAJOR)** **lib**: move DEP0027 to end of life (cjihrig) [#25377](https://github.com/nodejs/node/pull/25377)
* \[[`2d578ad996`](https://github.com/nodejs/node/commit/2d578ad996)] - **(SEMVER-MAJOR)** **lib**: move DEP0026 to end of life (cjihrig) [#25377](https://github.com/nodejs/node/pull/25377)
* \[[`853bee0acf`](https://github.com/nodejs/node/commit/853bee0acf)] - **(SEMVER-MAJOR)** **lib**: move DEP0023 to end of life (cjihrig) [#25280](https://github.com/nodejs/node/pull/25280)
* \[[`d4934ae6f2`](https://github.com/nodejs/node/commit/d4934ae6f2)] - **(SEMVER-MAJOR)** **lib**: move DEP0006 to end of life (cjihrig) [#25279](https://github.com/nodejs/node/pull/25279)
* \[[`4100001624`](https://github.com/nodejs/node/commit/4100001624)] - **(SEMVER-MAJOR)** **lib**: remove unintended access to deps/ (Anna Henningsen) [#25138](https://github.com/nodejs/node/pull/25138)
* \[[`b416dafb87`](https://github.com/nodejs/node/commit/b416dafb87)] - **(SEMVER-MAJOR)** **lib**: move DEP0120 to end of life (cjihrig) [#24862](https://github.com/nodejs/node/pull/24862)
* \[[`59257543c3`](https://github.com/nodejs/node/commit/59257543c3)] - **(SEMVER-MAJOR)** **lib**: use ES6 class inheritance style (Ruben Bridgewater) [#24755](https://github.com/nodejs/node/pull/24755)
* \[[`dcc82b37b6`](https://github.com/nodejs/node/commit/dcc82b37b6)] - **(SEMVER-MAJOR)** **lib**: remove `inherits()` usage (Ruben Bridgewater) [#24755](https://github.com/nodejs/node/pull/24755)
* \[[`d11c4beb4b`](https://github.com/nodejs/node/commit/d11c4beb4b)] - **(SEMVER-MAJOR)** **module**: remove dead code (Ruben Bridgewater) [#26983](https://github.com/nodejs/node/pull/26983)
* \[[`75007d64c0`](https://github.com/nodejs/node/commit/75007d64c0)] - **(SEMVER-MAJOR)** **module**: mark DEP0019 as End-of-Life (Ruben Bridgewater) [#26973](https://github.com/nodejs/node/pull/26973)
* \[[`115f0f5a57`](https://github.com/nodejs/node/commit/115f0f5a57)] - **(SEMVER-MAJOR)** **module**: throw an error for invalid package.json main entries (Ruben Bridgewater) [#26823](https://github.com/nodejs/node/pull/26823)
* \[[`60ce2fd827`](https://github.com/nodejs/node/commit/60ce2fd827)] - **(SEMVER-MAJOR)** **module**: don't search in require.resolve.paths (cjihrig) [#23683](https://github.com/nodejs/node/pull/23683)
* \[[`f0f26cedcc`](https://github.com/nodejs/node/commit/f0f26cedcc)] - **(SEMVER-MAJOR)** **n-api**: remove code from error name (Ruben Bridgewater) [#26738](https://github.com/nodejs/node/pull/26738)
* \[[`96204c3c71`](https://github.com/nodejs/node/commit/96204c3c71)] - **(SEMVER-MAJOR)** **net**: do not manipulate potential user code (Ruben Bridgewater) [#26751](https://github.com/nodejs/node/pull/26751)
* \[[`9389b464ea`](https://github.com/nodejs/node/commit/9389b464ea)] - **(SEMVER-MAJOR)** **net**: emit "write after end" errors in the next tick (Ouyang Yadong) [#24457](https://github.com/nodejs/node/pull/24457)
* \[[`1523111250`](https://github.com/nodejs/node/commit/1523111250)] - **(SEMVER-MAJOR)** **net**: deprecate \_setSimultaneousAccepts() undocumented function (James M Snell) [#23760](https://github.com/nodejs/node/pull/23760)
* \[[`802ea05a37`](https://github.com/nodejs/node/commit/802ea05a37)] - **(SEMVER-MAJOR)** **net,http2**: merge setTimeout code (ZYSzys) [#25084](https://github.com/nodejs/node/pull/25084)
* \[[`16e4cd19f2`](https://github.com/nodejs/node/commit/16e4cd19f2)] - **(SEMVER-MAJOR)** **os**: implement os.type() using uv\_os\_uname() (cjihrig) [#25659](https://github.com/nodejs/node/pull/25659)
* \[[`53ebd3311d`](https://github.com/nodejs/node/commit/53ebd3311d)] - **(SEMVER-MAJOR)** **process**: global.process, global.Buffer getters (Guy Bedford) [#26882](https://github.com/nodejs/node/pull/26882)
* \[[`fa5e097530`](https://github.com/nodejs/node/commit/fa5e097530)] - **(SEMVER-MAJOR)** **process**: move DEP0062 (node --debug) to end-of-life (Joyee Cheung) [#25828](https://github.com/nodejs/node/pull/25828)
* \[[`154efc9bde`](https://github.com/nodejs/node/commit/154efc9bde)] - **(SEMVER-MAJOR)** **process**: exit on --debug and --debug-brk after option parsing (Joyee Cheung) [#25828](https://github.com/nodejs/node/pull/25828)
* \[[`3439c955ab`](https://github.com/nodejs/node/commit/3439c955ab)] - **(SEMVER-MAJOR)** **process**: improve `--redirect-warnings` handling (Ruben Bridgewater) [#24965](https://github.com/nodejs/node/pull/24965)
* \[[`d3a62fe7fc`](https://github.com/nodejs/node/commit/d3a62fe7fc)] - **(SEMVER-MAJOR)** **readline**: support TERM=dumb (Vladislav Kaminsky) [#26261](https://github.com/nodejs/node/pull/26261)
* \[[`fe963149f6`](https://github.com/nodejs/node/commit/fe963149f6)] - **(SEMVER-MAJOR)** **repl**: add welcome message (gengjiawen) [#25947](https://github.com/nodejs/node/pull/25947)
* \[[`97737fd5fb`](https://github.com/nodejs/node/commit/97737fd5fb)] - **(SEMVER-MAJOR)** **repl**: fix terminal default setting (Ruben Bridgewater) [#26518](https://github.com/nodejs/node/pull/26518)
* \[[`82b3ee776b`](https://github.com/nodejs/node/commit/82b3ee776b)] - **(SEMVER-MAJOR)** **repl**: check colors with .getColorDepth() (Vladislav Kaminsky) [#26261](https://github.com/nodejs/node/pull/26261)
* \[[`584305841d`](https://github.com/nodejs/node/commit/584305841d)] - **(SEMVER-MAJOR)** **repl**: deprecate REPLServer.rli (Ruben Bridgewater) [#26260](https://github.com/nodejs/node/pull/26260)
* \[[`bf766c1b44`](https://github.com/nodejs/node/commit/bf766c1b44)] - **(SEMVER-MAJOR)** **src**: remove unused INT\_MAX constant (Sam Roberts) [#27078](https://github.com/nodejs/node/pull/27078)
* \[[`7df9e77236`](https://github.com/nodejs/node/commit/7df9e77236)] - **(SEMVER-MAJOR)** **src**: update NODE\_MODULE\_VERSION to 72 (Ujjwal Sharma) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`96c3224de0`](https://github.com/nodejs/node/commit/96c3224de0)] - **(SEMVER-MAJOR)** **src**: remove `AddPromiseHook()` (Anna Henningsen) [#26574](https://github.com/nodejs/node/pull/26574)
* \[[`9577f7724d`](https://github.com/nodejs/node/commit/9577f7724d)] - **(SEMVER-MAJOR)** **src**: update NODE\_MODULE\_VERSION to 71 (Michaël Zasso) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`6d9aa73b1f`](https://github.com/nodejs/node/commit/6d9aa73b1f)] - **(SEMVER-MAJOR)** **src**: clean up MultiIsolatePlatform interface (Anna Henningsen) [#26384](https://github.com/nodejs/node/pull/26384)
* \[[`1d996f58af`](https://github.com/nodejs/node/commit/1d996f58af)] - **(SEMVER-MAJOR)** **src**: properly configure default heap limits (Ali Ijaz Sheikh) [#25576](https://github.com/nodejs/node/pull/25576)
* \[[`9021b0d3fc`](https://github.com/nodejs/node/commit/9021b0d3fc)] - **(SEMVER-MAJOR)** **src**: remove icuDataDir from node config (GauthamBanasandra) [#24780](https://github.com/nodejs/node/pull/24780)
* \[[`a6f69ebc05`](https://github.com/nodejs/node/commit/a6f69ebc05)] - **(SEMVER-MAJOR)** **src**: explicitly allow JS in ReadHostObject (Yang Guo) [#23423](https://github.com/nodejs/node/pull/23423)
* \[[`3d25544148`](https://github.com/nodejs/node/commit/3d25544148)] - **(SEMVER-MAJOR)** **src**: update postmortem constant (cjihrig) [#23423](https://github.com/nodejs/node/pull/23423)
* \[[`23603447ad`](https://github.com/nodejs/node/commit/23603447ad)] - **(SEMVER-MAJOR)** **src**: update NODE\_MODULE\_VERSION to 68 (Michaël Zasso) [#23423](https://github.com/nodejs/node/pull/23423)
* \[[`afad3b443e`](https://github.com/nodejs/node/commit/afad3b443e)] - **(SEMVER-MAJOR)** **test**: update postmortem metadata test for V8 7.4 (cjihrig) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`e96e3f9eb0`](https://github.com/nodejs/node/commit/e96e3f9eb0)] - **(SEMVER-MAJOR)** **test**: remove redundant common.mustCall (Ruben Bridgewater) [#26738](https://github.com/nodejs/node/pull/26738)
* \[[`01b112a031`](https://github.com/nodejs/node/commit/01b112a031)] - **(SEMVER-MAJOR)** **test**: update postmortem metadata test for V8 7.3 (cjihrig) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`38ad285a2e`](https://github.com/nodejs/node/commit/38ad285a2e)] - **(SEMVER-MAJOR)** **test**: fix tests after V8 update (Michaël Zasso) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`260d5f8c3b`](https://github.com/nodejs/node/commit/260d5f8c3b)] - **(SEMVER-MAJOR)** **test**: update test-v8-stats (Michaël Zasso) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`78c8491a7e`](https://github.com/nodejs/node/commit/78c8491a7e)] - **(SEMVER-MAJOR)** **test**: remove apply calls over 65534 arg limit (Peter Marshall) [#25852](https://github.com/nodejs/node/pull/25852)
* \[[`22a9fe3552`](https://github.com/nodejs/node/commit/22a9fe3552)] - **(SEMVER-MAJOR)** **test**: add test for net-socket-setTimeout callback (ZYSzys) [#25084](https://github.com/nodejs/node/pull/25084)
* \[[`379bf1aa8e`](https://github.com/nodejs/node/commit/379bf1aa8e)] - **(SEMVER-MAJOR)** **test**: update postmortem metadata test for V8 7.1 (cjihrig) [#23423](https://github.com/nodejs/node/pull/23423)
* \[[`624a242b05`](https://github.com/nodejs/node/commit/624a242b05)] - **(SEMVER-MAJOR)** **test**: simplify regression test for SEGV (Sam Roberts) [#24241](https://github.com/nodejs/node/pull/24241)
* \[[`42dbaed460`](https://github.com/nodejs/node/commit/42dbaed460)] - **(SEMVER-MAJOR)** **tls**: support TLSv1.3 (Sam Roberts) [#26209](https://github.com/nodejs/node/pull/26209)
* \[[`0f745bf9bd`](https://github.com/nodejs/node/commit/0f745bf9bd)] - **(SEMVER-MAJOR)** **tls**: return correct version from getCipher() (Sam Roberts) [#26625](https://github.com/nodejs/node/pull/26625)
* \[[`6b7c402518`](https://github.com/nodejs/node/commit/6b7c402518)] - **(SEMVER-MAJOR)** **tls**: check arg types of renegotiate() (Sam Roberts) [#25876](https://github.com/nodejs/node/pull/25876)
* \[[`b05b330025`](https://github.com/nodejs/node/commit/b05b330025)] - **(SEMVER-MAJOR)** **tls**: add code for ERR\_TLS\_INVALID\_PROTOCOL\_METHOD (Sam Roberts) [#24729](https://github.com/nodejs/node/pull/24729)
* \[[`9b2ffff62c`](https://github.com/nodejs/node/commit/9b2ffff62c)] - **(SEMVER-MAJOR)** **tls**: emit a warning when servername is an IP address (Rodger Combs) [#23329](https://github.com/nodejs/node/pull/23329)
* \[[`60eca6a5d4`](https://github.com/nodejs/node/commit/60eca6a5d4)] - **(SEMVER-MAJOR)** **tls**: disable TLS v1.0 and v1.1 by default (Ben Noordhuis) [#23814](https://github.com/nodejs/node/pull/23814)
* \[[`3b4159c8ed`](https://github.com/nodejs/node/commit/3b4159c8ed)] - **(SEMVER-MAJOR)** **tls**: remove unused arg to createSecureContext() (Sam Roberts) [#24241](https://github.com/nodejs/node/pull/24241)
* \[[`246a6fc107`](https://github.com/nodejs/node/commit/246a6fc107)] - **(SEMVER-MAJOR)** **tls**: deprecate Server.prototype.setOptions() (cjihrig) [#23820](https://github.com/nodejs/node/pull/23820)
* \[[`87719d792b`](https://github.com/nodejs/node/commit/87719d792b)] - **(SEMVER-MAJOR)** **tls**: load NODE\_EXTRA\_CA\_CERTS at startup (Ouyang Yadong) [#23354](https://github.com/nodejs/node/pull/23354)
* \[[`c9fece38c8`](https://github.com/nodejs/node/commit/c9fece38c8)] - **(SEMVER-MAJOR)** **util**: change inspect compact and breakLength default (Ruben Bridgewater) [#27109](https://github.com/nodejs/node/pull/27109)
* \[[`892c51f330`](https://github.com/nodejs/node/commit/892c51f330)] - **(SEMVER-MAJOR)** **util**: improve inspect edge cases (Ruben Bridgewater) [#27109](https://github.com/nodejs/node/pull/27109)
* \[[`63e13fd220`](https://github.com/nodejs/node/commit/63e13fd220)] - **(SEMVER-MAJOR)** **util**: only the first line of the error message (Simon Zünd) [#26685](https://github.com/nodejs/node/pull/26685)
* \[[`b5ea925c8e`](https://github.com/nodejs/node/commit/b5ea925c8e)] - **(SEMVER-MAJOR)** **util**: don't set the prototype of callbackified functions (Ruben Bridgewater) [#26893](https://github.com/nodejs/node/pull/26893)
* \[[`46bf0d0f4f`](https://github.com/nodejs/node/commit/46bf0d0f4f)] - **(SEMVER-MAJOR)** **util**: rename callbackified function (Ruben Bridgewater) [#26893](https://github.com/nodejs/node/pull/26893)
* \[[`61d1334e5b`](https://github.com/nodejs/node/commit/61d1334e5b)] - **(SEMVER-MAJOR)** **util**: increase function length when using `callbackify()` (Ruben Bridgewater) [#26893](https://github.com/nodejs/node/pull/26893)
* \[[`5672ab7668`](https://github.com/nodejs/node/commit/5672ab7668)] - **(SEMVER-MAJOR)** **util**: prevent tampering with internals in `inspect()` (Ruben Bridgewater) [#26577](https://github.com/nodejs/node/pull/26577)
* \[[`a32cbe1597`](https://github.com/nodejs/node/commit/a32cbe1597)] - **(SEMVER-MAJOR)** **util**: fix proxy inspection (Ruben Bridgewater) [#26241](https://github.com/nodejs/node/pull/26241)
* \[[`7b674697d8`](https://github.com/nodejs/node/commit/7b674697d8)] - **(SEMVER-MAJOR)** **util**: prevent leaking internal properties (Ruben Bridgewater) [#24971](https://github.com/nodejs/node/pull/24971)
* \[[`1847696f4b`](https://github.com/nodejs/node/commit/1847696f4b)] - **(SEMVER-MAJOR)** **util**: protect against monkeypatched Object prototype for inspect() (Rich Trott) [#25953](https://github.com/nodejs/node/pull/25953)
* \[[`c1b9be53c8`](https://github.com/nodejs/node/commit/c1b9be53c8)] - **(SEMVER-MAJOR)** **util**: treat format arguments equally (Roman Reiss) [#23162](https://github.com/nodejs/node/pull/23162)
* \[[`cda6b20816`](https://github.com/nodejs/node/commit/cda6b20816)] - **(SEMVER-MAJOR)** **win, fs**: detect if symlink target is a directory (Bartosz Sosnowski) [#23724](https://github.com/nodejs/node/pull/23724)
* \[[`9a2654601e`](https://github.com/nodejs/node/commit/9a2654601e)] - **(SEMVER-MAJOR)** **zlib**: throw TypeError if callback is missing (Anna Henningsen) [#24929](https://github.com/nodejs/node/pull/24929)
* \[[`4eee55d354`](https://github.com/nodejs/node/commit/4eee55d354)] - **(SEMVER-MAJOR)** **zlib**: make “bare” constants un-enumerable (Anna Henningsen) [#24824](https://github.com/nodejs/node/pull/24824)

### Semver-Minor Commits

* \[[`3d8532f851`](https://github.com/nodejs/node/commit/3d8532f851)] - **(SEMVER-MINOR)** **buffer**: add {read|write}Big\[U]Int64{BE|LE} methods (Nikolai Vavilov) [#19691](https://github.com/nodejs/node/pull/19691)
* \[[`969bd1eb7b`](https://github.com/nodejs/node/commit/969bd1eb7b)] - **(SEMVER-MINOR)** **crypto**: add support for RSA-PSS keys (Tobias Nießen) [#26960](https://github.com/nodejs/node/pull/26960)
* \[[`7d0e50dcfe`](https://github.com/nodejs/node/commit/7d0e50dcfe)] - **(SEMVER-MINOR)** **crypto**: add crypto.sign() and crypto.verify() (Brian White) [#26611](https://github.com/nodejs/node/pull/26611)
* \[[`bcbd35a48d`](https://github.com/nodejs/node/commit/bcbd35a48d)] - **(SEMVER-MINOR)** **crypto**: add openssl specific error properties (Sam Roberts) [#26868](https://github.com/nodejs/node/pull/26868)
* \[[`85fda7e848`](https://github.com/nodejs/node/commit/85fda7e848)] - **(SEMVER-MINOR)** **crypto**: add support for x25119 and x448 KeyObjects (Filip Skokan) [#26774](https://github.com/nodejs/node/pull/26774)
* \[[`3a9592496c`](https://github.com/nodejs/node/commit/3a9592496c)] - **(SEMVER-MINOR)** **crypto**: add support for EdDSA key pair generation (Tobias Nießen) [#26554](https://github.com/nodejs/node/pull/26554)
* \[[`4895927a0a`](https://github.com/nodejs/node/commit/4895927a0a)] - **(SEMVER-MINOR)** **crypto**: add KeyObject.asymmetricKeySize (Patrick Gansterer) [#26387](https://github.com/nodejs/node/pull/26387)
* \[[`2161690024`](https://github.com/nodejs/node/commit/2161690024)] - **(SEMVER-MINOR)** **deps**: update nghttp2 to 1.38.0 (gengjiawen) [#27295](https://github.com/nodejs/node/pull/27295)
* \[[`ffd2df063c`](https://github.com/nodejs/node/commit/ffd2df063c)] - **(SEMVER-MINOR)** **doc**: update util colors (Ruben Bridgewater) [#27052](https://github.com/nodejs/node/pull/27052)
* \[[`b1094dbe19`](https://github.com/nodejs/node/commit/b1094dbe19)] - **(SEMVER-MINOR)** **esm**: phase two of new esm implementation (guybedford) [#26745](https://github.com/nodejs/node/pull/26745)
* \[[`e0e3084482`](https://github.com/nodejs/node/commit/e0e3084482)] - **(SEMVER-MINOR)** **inspector**: implement --cpu-prof\[-path] (Joyee Cheung) [#27147](https://github.com/nodejs/node/pull/27147)
* \[[`9f1282d536`](https://github.com/nodejs/node/commit/9f1282d536)] - **(SEMVER-MINOR)** **lib**: move queueMicrotask to stable (Gus Caplan) [#25594](https://github.com/nodejs/node/pull/25594)
* \[[`9b6b567bc4`](https://github.com/nodejs/node/commit/9b6b567bc4)] - **(SEMVER-MINOR)** **lib,src,doc**: add --heapsnapshot-signal CLI flag (cjihrig) [#27133](https://github.com/nodejs/node/pull/27133)
* \[[`9dcc9b6a6b`](https://github.com/nodejs/node/commit/9dcc9b6a6b)] - **(SEMVER-MINOR)** **process**: add --unhandled-rejections flag (Ruben Bridgewater) [#26599](https://github.com/nodejs/node/pull/26599)
* \[[`ece507394a`](https://github.com/nodejs/node/commit/ece507394a)] - **(SEMVER-MINOR)** **src**: do not reuse async resource in http parsers (Daniel Beckert) [#25094](https://github.com/nodejs/node/pull/25094)
* \[[`2755471bf3`](https://github.com/nodejs/node/commit/2755471bf3)] - **(SEMVER-MINOR)** **src**: print error before aborting (Ruben Bridgewater) [#26599](https://github.com/nodejs/node/pull/26599)
* \[[`ca9c0c90c2`](https://github.com/nodejs/node/commit/ca9c0c90c2)] - **(SEMVER-MINOR)** **src**: add .code and SSL specific error properties (Sam Roberts) [#25093](https://github.com/nodejs/node/pull/25093)
* \[[`8c69e06972`](https://github.com/nodejs/node/commit/8c69e06972)] - **(SEMVER-MINOR)** **tls**: return an OpenSSL error from renegotiate (Sam Roberts) [#26868](https://github.com/nodejs/node/pull/26868)
* \[[`90e958aa4d`](https://github.com/nodejs/node/commit/90e958aa4d)] - **(SEMVER-MINOR)** **util**: only sort weak entries once (Ruben Bridgewater) [#27052](https://github.com/nodejs/node/pull/27052)
* \[[`1940114ac3`](https://github.com/nodejs/node/commit/1940114ac3)] - **(SEMVER-MINOR)** **util**: highlight stack frames (Ruben Bridgewater) [#27052](https://github.com/nodejs/node/pull/27052)

### Semver-Patch Commits

* \[[`75463a9004`](https://github.com/nodejs/node/commit/75463a9004)] - **assert**: fix rejects stack trace and operator (Ruben Bridgewater) [#27047](https://github.com/nodejs/node/pull/27047)
* \[[`d3d4e10107`](https://github.com/nodejs/node/commit/d3d4e10107)] - **async\_hooks**: improve AsyncResource performance (Anatoli Papirovski) [#27032](https://github.com/nodejs/node/pull/27032)
* \[[`3973354951`](https://github.com/nodejs/node/commit/3973354951)] - **benchmark**: fix buffer-base64-decode.js (Rich Trott) [#27260](https://github.com/nodejs/node/pull/27260)
* \[[`f98679f3b2`](https://github.com/nodejs/node/commit/f98679f3b2)] - **benchmark**: add benchmark for dns.promises.lookup() (Rich Trott) [#27249](https://github.com/nodejs/node/pull/27249)
* \[[`29d0b43426`](https://github.com/nodejs/node/commit/29d0b43426)] - **benchmark**: fix http headers benchmark (Anatoli Papirovski) [#27021](https://github.com/nodejs/node/pull/27021)
* \[[`77dee25efd`](https://github.com/nodejs/node/commit/77dee25efd)] - **benchmark**: remove deprecated argument (Rich Trott) [#27091](https://github.com/nodejs/node/pull/27091)
* \[[`b08a867d60`](https://github.com/nodejs/node/commit/b08a867d60)] - **benchmark,doc,lib**: capitalize more comments (Ruben Bridgewater) [#26849](https://github.com/nodejs/node/pull/26849)
* \[[`d834275a48`](https://github.com/nodejs/node/commit/d834275a48)] - **buffer**: fix custom inspection with extra properties (Ruben Bridgewater) [#27074](https://github.com/nodejs/node/pull/27074)
* \[[`75eaf25e78`](https://github.com/nodejs/node/commit/75eaf25e78)] - **buffer**: use stricter `from()` input validation (Ruben Bridgewater) [#26825](https://github.com/nodejs/node/pull/26825)
* \[[`5aaf666b3b`](https://github.com/nodejs/node/commit/5aaf666b3b)] - **build**: improve embedded code-cache detection (Refael Ackermann) [#27311](https://github.com/nodejs/node/pull/27311)
* \[[`d17dfc7bb1`](https://github.com/nodejs/node/commit/d17dfc7bb1)] - **build**: remove redundant pyenv call in Travis build (Richard Lau) [#27247](https://github.com/nodejs/node/pull/27247)
* \[[`14df42fd00`](https://github.com/nodejs/node/commit/14df42fd00)] - **build**: run `mkcodecache` as an action (Refael Ackermann) [#27161](https://github.com/nodejs/node/pull/27161)
* \[[`b468a1dfc3`](https://github.com/nodejs/node/commit/b468a1dfc3)] - **build**: pin Python version in Travis (Richard Lau) [#27166](https://github.com/nodejs/node/pull/27166)
* \[[`7b0d867389`](https://github.com/nodejs/node/commit/7b0d867389)] - **build**: fix test failures not failing Travis builds (Richard Lau) [#27176](https://github.com/nodejs/node/pull/27176)
* \[[`56354d480d`](https://github.com/nodejs/node/commit/56354d480d)] - **build**: run flaky tests in Travis (Anna Henningsen) [#27158](https://github.com/nodejs/node/pull/27158)
* \[[`72f4a830d7`](https://github.com/nodejs/node/commit/72f4a830d7)] - **build**: tidy up additional libraries on Windows (Richard Lau) [#27138](https://github.com/nodejs/node/pull/27138)
* \[[`af03de48d8`](https://github.com/nodejs/node/commit/af03de48d8)] - **build**: don't use lint-ci on Travis (Richard Lau) [#27062](https://github.com/nodejs/node/pull/27062)
* \[[`41ba699973`](https://github.com/nodejs/node/commit/41ba699973)] - **build**: update configure for Node.js 12 (Richard Lau) [#26719](https://github.com/nodejs/node/pull/26719)
* \[[`20a917c571`](https://github.com/nodejs/node/commit/20a917c571)] - **build**: move optimizing link directives to node.exe target (Refael Ackermann) [#25931](https://github.com/nodejs/node/pull/25931)
* \[[`4698757610`](https://github.com/nodejs/node/commit/4698757610)] - **build,deps**: remove cygwin configuration which is not supported (Refael Ackermann) [#25931](https://github.com/nodejs/node/pull/25931)
* \[[`cd5c7bf240`](https://github.com/nodejs/node/commit/cd5c7bf240)] - **build,deps**: use PCH also for v8\_initializers (Refael Ackermann) [#25931](https://github.com/nodejs/node/pull/25931)
* \[[`6608cf286d`](https://github.com/nodejs/node/commit/6608cf286d)] - **build,deps,v8**: tie up loose ends (Refael Ackermann) [#26666](https://github.com/nodejs/node/pull/26666)
* \[[`6ac80f0e2b`](https://github.com/nodejs/node/commit/6ac80f0e2b)] - **build,src**: add PCH to node.gypi (Refael Ackermann) [#25931](https://github.com/nodejs/node/pull/25931)
* \[[`f216d5bbb1`](https://github.com/nodejs/node/commit/f216d5bbb1)] - **build,test**: fail `coverage` target if tests fail (Refael Ackermann) [#25432](https://github.com/nodejs/node/pull/25432)
* \[[`82b798907d`](https://github.com/nodejs/node/commit/82b798907d)] - **build,tools**: add more headers to V8 PCH file (Refael Ackermann) [#25931](https://github.com/nodejs/node/pull/25931)
* \[[`d66c7e3470`](https://github.com/nodejs/node/commit/d66c7e3470)] - **build,win**: deprecate `vcbuild test-ci` (Refael Ackermann) [#27231](https://github.com/nodejs/node/pull/27231)
* \[[`0fc27f6bc0`](https://github.com/nodejs/node/commit/0fc27f6bc0)] - **build,win**: bail vcbuild if mklink fails (Refael Ackermann) [#27216](https://github.com/nodejs/node/pull/27216)
* \[[`88beaf01f1`](https://github.com/nodejs/node/commit/88beaf01f1)] - **build,win**: rename node.lib to libnode.lib (Refael Ackermann) [#27150](https://github.com/nodejs/node/pull/27150)
* \[[`25df3c10f4`](https://github.com/nodejs/node/commit/25df3c10f4)] - **build,win**: put all compilation artifacts into `out` (Refael Ackermann) [#27149](https://github.com/nodejs/node/pull/27149)
* \[[`06c10cdc4c`](https://github.com/nodejs/node/commit/06c10cdc4c)] - **build,win**: teach GYP MSVS generator about MARMASM (Jon Kunkee) [#26020](https://github.com/nodejs/node/pull/26020)
* \[[`2ffd20bb91`](https://github.com/nodejs/node/commit/2ffd20bb91)] - **build,win**: emit MSBuild summary (Refael Ackermann) [#25931](https://github.com/nodejs/node/pull/25931)
* \[[`ff4adab78c`](https://github.com/nodejs/node/commit/ff4adab78c)] - **build,win**: always build with PCH (Refael Ackermann) [#25931](https://github.com/nodejs/node/pull/25931)
* \[[`28e2c3771d`](https://github.com/nodejs/node/commit/28e2c3771d)] - **child\_process**: rename \_validateStdtio to getValidStdio (Ruben Bridgewater) [#26809](https://github.com/nodejs/node/pull/26809)
* \[[`091902ae00`](https://github.com/nodejs/node/commit/091902ae00)] - **console**: remove trace frame (Ruben Bridgewater) [#27159](https://github.com/nodejs/node/pull/27159)
* \[[`a8eac78f8d`](https://github.com/nodejs/node/commit/a8eac78f8d)] - _**Revert**_ "**console**: use consolePropAttributes for k-bind properties in constructor" (Daniel Bevenius) [#26943](https://github.com/nodejs/node/pull/26943)
* \[[`ed5e69d7e6`](https://github.com/nodejs/node/commit/ed5e69d7e6)] - **console**: use consolePropAttributes for k-bind properties in constructor (Beni von Cheni) [#26850](https://github.com/nodejs/node/pull/26850)
* \[[`69140bc7f8`](https://github.com/nodejs/node/commit/69140bc7f8)] - **crypto**: do not abort when setting throws (Sam Roberts) [#27157](https://github.com/nodejs/node/pull/27157)
* \[[`0911e88056`](https://github.com/nodejs/node/commit/0911e88056)] - **crypto**: fix rsa key gen with non-default exponent (Sam Roberts) [#27092](https://github.com/nodejs/node/pull/27092)
* \[[`fadcb2d850`](https://github.com/nodejs/node/commit/fadcb2d850)] - **crypto**: simplify missing passphrase detection (Tobias Nießen) [#27089](https://github.com/nodejs/node/pull/27089)
* \[[`73bca57988`](https://github.com/nodejs/node/commit/73bca57988)] - **crypto**: fail early if passphrase is too long (Tobias Nießen) [#27010](https://github.com/nodejs/node/pull/27010)
* \[[`05bd6071a6`](https://github.com/nodejs/node/commit/05bd6071a6)] - **crypto**: use EVP\_PKEY\_X448 in GetEphemeralKeyInfo (cjihrig) [#26988](https://github.com/nodejs/node/pull/26988)
* \[[`6ac692a3db`](https://github.com/nodejs/node/commit/6ac692a3db)] - **crypto**: use EVP\_PKEY\_X25519 in GetEphemeralKeyInfo (cjihrig) [#26988](https://github.com/nodejs/node/pull/26988)
* \[[`7c1fc93e30`](https://github.com/nodejs/node/commit/7c1fc93e30)] - **crypto**: don't crash on unknown asymmetricKeyType (Filip Skokan) [#26786](https://github.com/nodejs/node/pull/26786)
* \[[`df1c9eb975`](https://github.com/nodejs/node/commit/df1c9eb975)] - **crypto**: rename generateKeyPairEdDSA (Tobias Nießen) [#26900](https://github.com/nodejs/node/pull/26900)
* \[[`751c92d972`](https://github.com/nodejs/node/commit/751c92d972)] - **crypto**: remove obsolete encoding check (Ruben Bridgewater) [#26809](https://github.com/nodejs/node/pull/26809)
* \[[`6f77af541e`](https://github.com/nodejs/node/commit/6f77af541e)] - _**Revert**_ "**crypto**: add KeyObject.asymmetricKeySize" (Tobias Nießen) [#26636](https://github.com/nodejs/node/pull/26636)
* \[[`247c14c040`](https://github.com/nodejs/node/commit/247c14c040)] - **crypto**: fix EdDSA support for KeyObject (Brian White) [#26319](https://github.com/nodejs/node/pull/26319)
* \[[`90cf2d5f00`](https://github.com/nodejs/node/commit/90cf2d5f00)] - **deps**: use nghttp2's config.h on all platforms (Sam Roberts) [#27283](https://github.com/nodejs/node/pull/27283)
* \[[`aec2ce4ee1`](https://github.com/nodejs/node/commit/aec2ce4ee1)] - **deps**: upgrade to libuv 1.28.0 (cjihrig) [#27241](https://github.com/nodejs/node/pull/27241)
* \[[`7f29117de3`](https://github.com/nodejs/node/commit/7f29117de3)] - **deps**: patch V8 to 7.4.288.21 (Matheus Marchini) [#27265](https://github.com/nodejs/node/pull/27265)
* \[[`033f6b566e`](https://github.com/nodejs/node/commit/033f6b566e)] - **deps**: upgrade npm to 6.9.0 (Kat Marchán) [#26244](https://github.com/nodejs/node/pull/26244)
* \[[`135b79a31d`](https://github.com/nodejs/node/commit/135b79a31d)] - **deps**: patch V8 to 7.4.288.18 (Michaël Zasso) [#27066](https://github.com/nodejs/node/pull/27066)
* \[[`c1d61f2b4b`](https://github.com/nodejs/node/commit/c1d61f2b4b)] - **deps**: patch V8 to 7.4.288.17 (Michaël Zasso) [#27066](https://github.com/nodejs/node/pull/27066)
* \[[`5b8434eebc`](https://github.com/nodejs/node/commit/5b8434eebc)] - **deps**: V8: cherry-pick 0188634 (Michaël Zasso) [#27013](https://github.com/nodejs/node/pull/27013)
* \[[`8cc181c8ee`](https://github.com/nodejs/node/commit/8cc181c8ee)] - **deps**: V8: cherry-pick c8785d1 (Michaël Zasso) [#27013](https://github.com/nodejs/node/pull/27013)
* \[[`2ea9de2e85`](https://github.com/nodejs/node/commit/2ea9de2e85)] - **deps**: V8: cherry-pick f4b860d (Michaël Zasso) [#27013](https://github.com/nodejs/node/pull/27013)
* \[[`ddbb7d7777`](https://github.com/nodejs/node/commit/ddbb7d7777)] - **deps**: cherry-pick 56f6a76 from upstream V8 (Ruben Bridgewater) [#25269](https://github.com/nodejs/node/pull/25269)
* \[[`59fa7f1257`](https://github.com/nodejs/node/commit/59fa7f1257)] - **deps**: cherry-pick 26b145a from upstream V8 (Sam Roberts) [#25148](https://github.com/nodejs/node/pull/25148)
* \[[`a9812142ca`](https://github.com/nodejs/node/commit/a9812142ca)] - **deps**: patch V8 to 7.1.302.33 (Ruben Bridgewater) [#25101](https://github.com/nodejs/node/pull/25101)
* \[[`f0e460968e`](https://github.com/nodejs/node/commit/f0e460968e)] - **deps**: remove test-related GYP files (Michaël Zasso) [#25097](https://github.com/nodejs/node/pull/25097)
* \[[`323a365766`](https://github.com/nodejs/node/commit/323a365766)] - **deps**: float 26d7fce1 from openssl (Rod Vagg) [#24353](https://github.com/nodejs/node/pull/24353)
* \[[`d8fb81fab3`](https://github.com/nodejs/node/commit/d8fb81fab3)] - **deps**: float 99540ec from openssl (CVE-2018-0735) (Rod Vagg) [#23950](https://github.com/nodejs/node/pull/23950)
* \[[`213c7d2d64`](https://github.com/nodejs/node/commit/213c7d2d64)] - **deps**: float a9cfb8c2 from openssl (CVE-2018-0734) (Rod Vagg) [#23965](https://github.com/nodejs/node/pull/23965)
* \[[`e2260e901d`](https://github.com/nodejs/node/commit/e2260e901d)] - **deps**: float 415c3356 from openssl (DSA vulnerability) (Rod Vagg) [#23965](https://github.com/nodejs/node/pull/23965)
* \[[`e356807a79`](https://github.com/nodejs/node/commit/e356807a79)] - **deps,test**: bump googletest to 39f72ea6f5 (Refael Ackermann) [#27231](https://github.com/nodejs/node/pull/27231)
* \[[`8e308e8b28`](https://github.com/nodejs/node/commit/8e308e8b28)] - **deps,v8**: cherry-pick 385aa80 (Refael Ackermann) [#26702](https://github.com/nodejs/node/pull/26702)
* \[[`d1b7193428`](https://github.com/nodejs/node/commit/d1b7193428)] - **deps,v8**: silence V8 self-deprecation warnings (Refael Ackermann) [#25394](https://github.com/nodejs/node/pull/25394)
* \[[`9e960175d1`](https://github.com/nodejs/node/commit/9e960175d1)] - **dgram**: add support for UDP connected sockets (Santiago Gimeno) [#26871](https://github.com/nodejs/node/pull/26871)
* \[[`09cdc37824`](https://github.com/nodejs/node/commit/09cdc37824)] - **dns**: do not indicate invalid IPs are IPv6 (Rich Trott) [#27081](https://github.com/nodejs/node/pull/27081)
* \[[`bc2d258a3e`](https://github.com/nodejs/node/commit/bc2d258a3e)] - **dns**: refactor internal/dns/promises.js (Rich Trott) [#27081](https://github.com/nodejs/node/pull/27081)
* \[[`72308a5deb`](https://github.com/nodejs/node/commit/72308a5deb)] - **doc**: simplify nomination process text (Rich Trott) [#27317](https://github.com/nodejs/node/pull/27317)
* \[[`290faec0e7`](https://github.com/nodejs/node/commit/290faec0e7)] - **doc**: fix extname with the correct description (himself65) [#27303](https://github.com/nodejs/node/pull/27303)
* \[[`d4dae5e1ca`](https://github.com/nodejs/node/commit/d4dae5e1ca)] - **doc**: simplify bullet points in GOVERNANCE.md (Rich Trott) [#27284](https://github.com/nodejs/node/pull/27284)
* \[[`ba74e42000`](https://github.com/nodejs/node/commit/ba74e42000)] - **doc**: revise Collaborator Nominations introduction (Rich Trott) [#27237](https://github.com/nodejs/node/pull/27237)
* \[[`c61c722c8c`](https://github.com/nodejs/node/commit/c61c722c8c)] - **doc**: add ABI version registry (Rod Vagg) [#24114](https://github.com/nodejs/node/pull/24114)
* \[[`7938238b31`](https://github.com/nodejs/node/commit/7938238b31)] - **doc**: add internal documentation (Aymen Naghmouchi) [#26665](https://github.com/nodejs/node/pull/26665)
* \[[`82e6c3378f`](https://github.com/nodejs/node/commit/82e6c3378f)] - **doc**: revise TSC Meetings material in GOVERNANCE.md (Rich Trott) [#27204](https://github.com/nodejs/node/pull/27204)
* \[[`d5f9cf81e3`](https://github.com/nodejs/node/commit/d5f9cf81e3)] - **doc**: fix some links (Vse Mozhet Byt) [#27141](https://github.com/nodejs/node/pull/27141)
* \[[`7b854959e7`](https://github.com/nodejs/node/commit/7b854959e7)] - **doc**: revise TSC text in GOVERNANCE.md (Rich Trott) [#27169](https://github.com/nodejs/node/pull/27169)
* \[[`9b859f50d5`](https://github.com/nodejs/node/commit/9b859f50d5)] - **doc**: add missing n-api version indicator (Michael Dawson) [#27155](https://github.com/nodejs/node/pull/27155)
* \[[`41d5666aaa`](https://github.com/nodejs/node/commit/41d5666aaa)] - **doc**: consolidate Collaborator status in GOVERNANCE (Rich Trott) [#27128](https://github.com/nodejs/node/pull/27128)
* \[[`1656cd2edb`](https://github.com/nodejs/node/commit/1656cd2edb)] - **doc**: remove outdated link (cjihrig) [#27126](https://github.com/nodejs/node/pull/27126)
* \[[`643a2fa447`](https://github.com/nodejs/node/commit/643a2fa447)] - **doc**: specify return type for tty.isatty() (Mykola Bilochub) [#27154](https://github.com/nodejs/node/pull/27154)
* \[[`557bd861aa`](https://github.com/nodejs/node/commit/557bd861aa)] - **doc**: revise Collaborator material in GOVERNANCE.md (Rich Trott) [#27103](https://github.com/nodejs/node/pull/27103)
* \[[`1afec97130`](https://github.com/nodejs/node/commit/1afec97130)] - **doc**: link bigint type to MDN instead of proposal (Vse Mozhet Byt) [#27101](https://github.com/nodejs/node/pull/27101)
* \[[`21b739fb69`](https://github.com/nodejs/node/commit/21b739fb69)] - **doc**: add missing step to npm release process (Myles Borins) [#27105](https://github.com/nodejs/node/pull/27105)
* \[[`181052d7c2`](https://github.com/nodejs/node/commit/181052d7c2)] - **doc**: revise Collaborator description in GOVERNANCE.md (Rich Trott) [#27071](https://github.com/nodejs/node/pull/27071)
* \[[`10eaf6a09f`](https://github.com/nodejs/node/commit/10eaf6a09f)] - **doc**: fix section sorting, add link reference (Vse Mozhet Byt) [#27075](https://github.com/nodejs/node/pull/27075)
* \[[`d989e20717`](https://github.com/nodejs/node/commit/d989e20717)] - **doc**: describe tls.DEFAULT\_MIN\_VERSION/\_MAX\_VERSION (Sam Roberts) [#26821](https://github.com/nodejs/node/pull/26821)
* \[[`0622ce6e7f`](https://github.com/nodejs/node/commit/0622ce6e7f)] - **doc**: fix changelog date typo (Jesse McCarthy) [#26831](https://github.com/nodejs/node/pull/26831)
* \[[`cd9898a52a`](https://github.com/nodejs/node/commit/cd9898a52a)] - **doc**: add missing pr-url (cjihrig) [#26753](https://github.com/nodejs/node/pull/26753)
* \[[`06879aafee`](https://github.com/nodejs/node/commit/06879aafee)] - **doc**: fix year in changelog (Colin Prince) [#26584](https://github.com/nodejs/node/pull/26584)
* \[[`7e0ddf66b9`](https://github.com/nodejs/node/commit/7e0ddf66b9)] - **doc**: fix deprecation "End-of-Life" capitalization (Tobias Nießen) [#26251](https://github.com/nodejs/node/pull/26251)
* \[[`3d4db3a7bf`](https://github.com/nodejs/node/commit/3d4db3a7bf)] - **doc**: fix metadata of DEP0114 (Tobias Nießen) [#26250](https://github.com/nodejs/node/pull/26250)
* \[[`ccf37b3a84`](https://github.com/nodejs/node/commit/ccf37b3a84)] - **doc**: fix deprecations metadata (Richard Lau) [#25434](https://github.com/nodejs/node/pull/25434)
* \[[`3614157b78`](https://github.com/nodejs/node/commit/3614157b78)] - **doc**: fix lint in CHANGELOG\_V6 (Myles Borins) [#25233](https://github.com/nodejs/node/pull/25233)
* \[[`928f776385`](https://github.com/nodejs/node/commit/928f776385)] - **doc**: add missing pr-url (cjihrig) [#25091](https://github.com/nodejs/node/pull/25091)
* \[[`43273262e5`](https://github.com/nodejs/node/commit/43273262e5)] - **doc**: describe secureProtocol and CLI interaction (Sam Roberts) [#24386](https://github.com/nodejs/node/pull/24386)
* \[[`34eccb2a1b`](https://github.com/nodejs/node/commit/34eccb2a1b)] - **doc**: fix missing PR id of 23329 (Ouyang Yadong) [#24458](https://github.com/nodejs/node/pull/24458)
* \[[`db2ac1dbd9`](https://github.com/nodejs/node/commit/db2ac1dbd9)] - **doc**: fix headings for CHANGELOG\_v10.md (Myles Borins) [#23973](https://github.com/nodejs/node/pull/23973)
* \[[`c99026bdd7`](https://github.com/nodejs/node/commit/c99026bdd7)] - **doc**: update missing deprecation (cjihrig) [#23883](https://github.com/nodejs/node/pull/23883)
* \[[`4afd503465`](https://github.com/nodejs/node/commit/4afd503465)] - **doc,test,repl**: fix deprecation code (cjihrig) [#26368](https://github.com/nodejs/node/pull/26368)
* \[[`3b044962c4`](https://github.com/nodejs/node/commit/3b044962c4)] - **errors**: add more information in case of invalid callbacks (Ruben Bridgewater) [#27048](https://github.com/nodejs/node/pull/27048)
* \[[`96e46d37c4`](https://github.com/nodejs/node/commit/96e46d37c4)] - **esm**: replace --entry-type with --input-type (Geoffrey Booth) [#27184](https://github.com/nodejs/node/pull/27184)
* \[[`5e98f875b9`](https://github.com/nodejs/node/commit/5e98f875b9)] - **esm**: fix typos (Geoffrey Booth) [#27067](https://github.com/nodejs/node/pull/27067)
* \[[`7a547098d5`](https://github.com/nodejs/node/commit/7a547098d5)] - **esm**: use primordials (Myles Borins) [#26954](https://github.com/nodejs/node/pull/26954)
* \[[`2400cbcf7c`](https://github.com/nodejs/node/commit/2400cbcf7c)] - **fs**: fix infinite loop with async recursive mkdir on Windows (Richard Lau) [#27207](https://github.com/nodejs/node/pull/27207)
* \[[`b925379f50`](https://github.com/nodejs/node/commit/b925379f50)] - **fs**: warn on non-portable mkdtemp() templates (cjihrig) [#26980](https://github.com/nodejs/node/pull/26980)
* \[[`eb2d4161f5`](https://github.com/nodejs/node/commit/eb2d4161f5)] - **fs**: improve readFile performance (Ruben Bridgewater) [#27063](https://github.com/nodejs/node/pull/27063)
* \[[`92db780d9e`](https://github.com/nodejs/node/commit/92db780d9e)] - **http2**: rename function for clarity (Ruben Bridgewater) [#26809](https://github.com/nodejs/node/pull/26809)
* \[[`ce265908eb`](https://github.com/nodejs/node/commit/ce265908eb)] - **http2**: remove side effects from validateSettings (Ruben Bridgewater) [#26809](https://github.com/nodejs/node/pull/26809)
* \[[`cd3a9eebca`](https://github.com/nodejs/node/commit/cd3a9eebca)] - **https**: remove usage of public require('util') (dnlup) [#26772](https://github.com/nodejs/node/pull/26772)
* \[[`49d3d11ba7`](https://github.com/nodejs/node/commit/49d3d11ba7)] - **inspector**: split --cpu-prof-path to --cpu-prof-dir and --cpu-prof-name (Joyee Cheung) [#27306](https://github.com/nodejs/node/pull/27306)
* \[[`94adfe9831`](https://github.com/nodejs/node/commit/94adfe9831)] - **lib**: replace --diagnostic-report-\* with --report-\* (Joyee Cheung) [#27312](https://github.com/nodejs/node/pull/27312)
* \[[`49ee010005`](https://github.com/nodejs/node/commit/49ee010005)] - **lib**: use getOptionValue instead of process underscore aliases (Joyee Cheung) [#27278](https://github.com/nodejs/node/pull/27278)
* \[[`a38e9c438a`](https://github.com/nodejs/node/commit/a38e9c438a)] - **lib**: require globals instead of using the global proxy (Joyee Cheung) [#27112](https://github.com/nodejs/node/pull/27112)
* \[[`914d6c9ab8`](https://github.com/nodejs/node/commit/914d6c9ab8)] - **lib**: use primordials in domexception.js (Joyee Cheung) [#27171](https://github.com/nodejs/node/pull/27171)
* \[[`3da36d0e94`](https://github.com/nodejs/node/commit/3da36d0e94)] - **lib**: create primordials in every context (Joyee Cheung) [#27171](https://github.com/nodejs/node/pull/27171)
* \[[`908292cf1f`](https://github.com/nodejs/node/commit/908292cf1f)] - **lib**: enforce the use of Object from primordials (Michaël Zasso) [#27146](https://github.com/nodejs/node/pull/27146)
* \[[`47f5cc1ad1`](https://github.com/nodejs/node/commit/47f5cc1ad1)] - **lib**: faster FreeList (Anatoli Papirovski) [#27021](https://github.com/nodejs/node/pull/27021)
* \[[`5b9e57012a`](https://github.com/nodejs/node/commit/5b9e57012a)] - **lib**: add signal name validator (cjihrig) [#27137](https://github.com/nodejs/node/pull/27137)
* \[[`112cc7c275`](https://github.com/nodejs/node/commit/112cc7c275)] - **lib**: use safe methods from primordials (Michaël Zasso) [#27096](https://github.com/nodejs/node/pull/27096)
* \[[`5a8c55f078`](https://github.com/nodejs/node/commit/5a8c55f078)] - **lib**: fix outdated comment (Vse Mozhet Byt) [#27122](https://github.com/nodejs/node/pull/27122)
* \[[`de23055536`](https://github.com/nodejs/node/commit/de23055536)] - **lib**: remove `env: node` in eslint config for lib files (Joyee Cheung) [#27082](https://github.com/nodejs/node/pull/27082)
* \[[`2c49e8b537`](https://github.com/nodejs/node/commit/2c49e8b537)] - **lib**: make queueMicrotask faster (Anatoli Papirovski) [#27032](https://github.com/nodejs/node/pull/27032)
* \[[`0817840f77`](https://github.com/nodejs/node/commit/0817840f77)] - **lib**: force using primordials for JSON, Math and Reflect (Michaël Zasso) [#27027](https://github.com/nodejs/node/pull/27027)
* \[[`7bddfcc61a`](https://github.com/nodejs/node/commit/7bddfcc61a)] - **lib**: consolidate arrayBufferView validation (Ruben Bridgewater) [#26809](https://github.com/nodejs/node/pull/26809)
* \[[`6c913fb028`](https://github.com/nodejs/node/commit/6c913fb028)] - **lib**: remove return values from validation functions (Ruben Bridgewater) [#26809](https://github.com/nodejs/node/pull/26809)
* \[[`50a3fe20ea`](https://github.com/nodejs/node/commit/50a3fe20ea)] - **lib**: rename validateMode to parseMode (Ruben Bridgewater) [#26809](https://github.com/nodejs/node/pull/26809)
* \[[`76e67e9884`](https://github.com/nodejs/node/commit/76e67e9884)] - **lib**: assign missed deprecation code (Anna Henningsen) [#26492](https://github.com/nodejs/node/pull/26492)
* \[[`f3b5cc0807`](https://github.com/nodejs/node/commit/f3b5cc0807)] - **meta**: travis: run compilation jobs first (Refael Ackermann) [#27205](https://github.com/nodejs/node/pull/27205)
* \[[`7c816b7588`](https://github.com/nodejs/node/commit/7c816b7588)] - **module**: explicitly initialize CJS loader (Joyee Cheung) [#27313](https://github.com/nodejs/node/pull/27313)
* \[[`d6317d0a45`](https://github.com/nodejs/node/commit/d6317d0a45)] - **module**: remove usage of require('util') (dnlup) [#26803](https://github.com/nodejs/node/pull/26803)
* \[[`ff89670902`](https://github.com/nodejs/node/commit/ff89670902)] - **n-api**: reduce gc finalization stress (Michael Dawson) [#27085](https://github.com/nodejs/node/pull/27085)
* \[[`655c90b287`](https://github.com/nodejs/node/commit/655c90b287)] - **net**: inline maybeDestroy() (Luigi Pinca) [#27136](https://github.com/nodejs/node/pull/27136)
* \[[`f0b3855a90`](https://github.com/nodejs/node/commit/f0b3855a90)] - **net**: remove usage of require('util') (dnlup) [#26920](https://github.com/nodejs/node/pull/26920)
* \[[`9946c59707`](https://github.com/nodejs/node/commit/9946c59707)] - **path**: simplify normalizeString (Ruben Bridgewater) [#27240](https://github.com/nodejs/node/pull/27240)
* \[[`9dba96dc1a`](https://github.com/nodejs/node/commit/9dba96dc1a)] - **process**: patch more process properties during pre-execution (Joyee Cheung) [#26945](https://github.com/nodejs/node/pull/26945)
* \[[`d4eda4d876`](https://github.com/nodejs/node/commit/d4eda4d876)] - **process**: remove protection for SyncWriteStream destroy in stdio (Matteo Collina) [#26902](https://github.com/nodejs/node/pull/26902)
* \[[`2701f5538f`](https://github.com/nodejs/node/commit/2701f5538f)] - **readline**: remove usage of require('util') (dnlup) [#26818](https://github.com/nodejs/node/pull/26818)
* \[[`415a825dc0`](https://github.com/nodejs/node/commit/415a825dc0)] - **repl**: remove usage of require('util') in `repl.js` (dnlup) [#26820](https://github.com/nodejs/node/pull/26820)
* \[[`af35d4044f`](https://github.com/nodejs/node/commit/af35d4044f)] - **report**: use uv\_gettimeofday for dumpEventTimeStamp (cjihrig) [#27029](https://github.com/nodejs/node/pull/27029)
* \[[`44a3acb627`](https://github.com/nodejs/node/commit/44a3acb627)] - **report**: improve signal name validation (cjihrig) [#27137](https://github.com/nodejs/node/pull/27137)
* \[[`e3032708e0`](https://github.com/nodejs/node/commit/e3032708e0)] - **report**: add support for UDP connected sockets (Richard Lau) [#27072](https://github.com/nodejs/node/pull/27072)
* \[[`8e1e9946a9`](https://github.com/nodejs/node/commit/8e1e9946a9)] - **src**: use uv\_gettimeofday() to get microseconds (cjihrig) [#27029](https://github.com/nodejs/node/pull/27029)
* \[[`8eaf31181a`](https://github.com/nodejs/node/commit/8eaf31181a)] - **src**: apply modernize-use-nullptr in node\_win32\_etw\_provider.cc (gengjiawen) [#27263](https://github.com/nodejs/node/pull/27263)
* \[[`19e3e02a2d`](https://github.com/nodejs/node/commit/19e3e02a2d)] - **src**: move SIGINT watchdog utils to the contextify binding (Joyee Cheung) [#27290](https://github.com/nodejs/node/pull/27290)
* \[[`5356b4a675`](https://github.com/nodejs/node/commit/5356b4a675)] - **src**: split per-process initialization and teardown routines (Joyee Cheung) [#27276](https://github.com/nodejs/node/pull/27276)
* \[[`8d901bb44e`](https://github.com/nodejs/node/commit/8d901bb44e)] - **src**: move guessHandleType in the util binding (Joyee Cheung) [#27289](https://github.com/nodejs/node/pull/27289)
* \[[`758191033f`](https://github.com/nodejs/node/commit/758191033f)] - **src**: fix performance-faster-string-find in node\_report.cc (gengjiawen) [#27262](https://github.com/nodejs/node/pull/27262)
* \[[`dc8b57fdc1`](https://github.com/nodejs/node/commit/dc8b57fdc1)] - **src**: use ArrayBufferAllocator::Create in node\_worker.cc (Anna Henningsen) [#27251](https://github.com/nodejs/node/pull/27251)
* \[[`f9da3f0cce`](https://github.com/nodejs/node/commit/f9da3f0cce)] - **src**: enable non-nestable V8 platform tasks (Anna Henningsen) [#27252](https://github.com/nodejs/node/pull/27252)
* \[[`3ef1512f9e`](https://github.com/nodejs/node/commit/3ef1512f9e)] - **src**: allows escaping NODE\_OPTIONS with backslashes (Maël Nison) [#24065](https://github.com/nodejs/node/pull/24065)
* \[[`cdba9f23ec`](https://github.com/nodejs/node/commit/cdba9f23ec)] - **src**: handle fatal error when Environment is not assigned to context (Joyee Cheung) [#27236](https://github.com/nodejs/node/pull/27236)
* \[[`83d1ca7de9`](https://github.com/nodejs/node/commit/83d1ca7de9)] - **src**: disallow calling env-dependent methods during bootstrap (Joyee Cheung) [#27234](https://github.com/nodejs/node/pull/27234)
* \[[`cab1dc5bb3`](https://github.com/nodejs/node/commit/cab1dc5bb3)] - **src**: use RAII to manage the main isolate data (Joyee Cheung) [#27220](https://github.com/nodejs/node/pull/27220)
* \[[`1e7823dd4e`](https://github.com/nodejs/node/commit/1e7823dd4e)] - **src**: remove redundant call in node\_options-inl.h (gengjiawen) [#26959](https://github.com/nodejs/node/pull/26959)
* \[[`73471236d8`](https://github.com/nodejs/node/commit/73471236d8)] - **src**: remove unimplemented method in TracingAgent (gengjiawen) [#26959](https://github.com/nodejs/node/pull/26959)
* \[[`427fce711f`](https://github.com/nodejs/node/commit/427fce711f)] - **src**: fix check for accepting Buffers into Node’s allocator (Anna Henningsen) [#27174](https://github.com/nodejs/node/pull/27174)
* \[[`dfd7e99425`](https://github.com/nodejs/node/commit/dfd7e99425)] - **src**: make a Environment-independent proxy class for NativeModuleLoader (Joyee Cheung) [#27160](https://github.com/nodejs/node/pull/27160)
* \[[`060d901f87`](https://github.com/nodejs/node/commit/060d901f87)] - **src**: replace FromJust() with Check() when possible (Sam Roberts) [#27162](https://github.com/nodejs/node/pull/27162)
* \[[`ee7daf76c0`](https://github.com/nodejs/node/commit/ee7daf76c0)] - **src**: remove redundant string initialization (gengjiawen) [#27152](https://github.com/nodejs/node/pull/27152)
* \[[`845a6214f8`](https://github.com/nodejs/node/commit/845a6214f8)] - **src**: use macro instead of magic number for fd (gengjiawen) [#27152](https://github.com/nodejs/node/pull/27152)
* \[[`547576f530`](https://github.com/nodejs/node/commit/547576f530)] - **src**: always use diagnostic file sequence number (cjihrig) [#27142](https://github.com/nodejs/node/pull/27142)
* \[[`c1e03eda07`](https://github.com/nodejs/node/commit/c1e03eda07)] - **src**: use SealHandleScope for inspector tasks (Anna Henningsen) [#27116](https://github.com/nodejs/node/pull/27116)
* \[[`a3f30a48c2`](https://github.com/nodejs/node/commit/a3f30a48c2)] - **src**: unify crypto constant setup (Sam Roberts) [#27077](https://github.com/nodejs/node/pull/27077)
* \[[`97c0a34935`](https://github.com/nodejs/node/commit/97c0a34935)] - **src**: don't point to out of scope variable (cjihrig) [#27070](https://github.com/nodejs/node/pull/27070)
* \[[`864860e9f3`](https://github.com/nodejs/node/commit/864860e9f3)] - **src**: port coverage serialization to C++ (Joyee Cheung) [#26874](https://github.com/nodejs/node/pull/26874)
* \[[`d0e2650d03`](https://github.com/nodejs/node/commit/d0e2650d03)] - **src**: add NOLINT to js\_native\_.\* (gengjiawen) [#26884](https://github.com/nodejs/node/pull/26884)
* \[[`eb2dccb17a`](https://github.com/nodejs/node/commit/eb2dccb17a)] - **src**: move AsyncResource impl out of public header (Ben Noordhuis) [#26348](https://github.com/nodejs/node/pull/26348)
* \[[`e1d55a0cbc`](https://github.com/nodejs/node/commit/e1d55a0cbc)] - **src**: port bootstrap/cache.js to C++ (Joyee Cheung) [#27046](https://github.com/nodejs/node/pull/27046)
* \[[`f59ec2abee`](https://github.com/nodejs/node/commit/f59ec2abee)] - **src**: implement MemoryRetainer in Environment (Joyee Cheung) [#27018](https://github.com/nodejs/node/pull/27018)
* \[[`1087805eeb`](https://github.com/nodejs/node/commit/1087805eeb)] - **src**: check return value, silence coverity warning (Ben Noordhuis) [#26997](https://github.com/nodejs/node/pull/26997)
* \[[`bb98f27181`](https://github.com/nodejs/node/commit/bb98f27181)] - **src**: check uv\_fs\_close() return value (cjihrig) [#26967](https://github.com/nodejs/node/pull/26967)
* \[[`8bc7d2a5be`](https://github.com/nodejs/node/commit/8bc7d2a5be)] - **src**: fix data type when using uv\_get\_total\_memory() (gengjiawen) [#26886](https://github.com/nodejs/node/pull/26886)
* \[[`c0f031c5bd`](https://github.com/nodejs/node/commit/c0f031c5bd)] - **src**: remove unused variable (cjihrig) [#26879](https://github.com/nodejs/node/pull/26879)
* \[[`1935625df4`](https://github.com/nodejs/node/commit/1935625df4)] - **src**: disallow constructor behaviour for native methods (Anna Henningsen) [#26700](https://github.com/nodejs/node/pull/26700)
* \[[`f091d4e840`](https://github.com/nodejs/node/commit/f091d4e840)] - **src**: apply clang-tidy rule modernize-use-emplace (gengjiawen) [#26564](https://github.com/nodejs/node/pull/26564)
* \[[`f47adfbda5`](https://github.com/nodejs/node/commit/f47adfbda5)] - **src**: fix DTrace GC callbacks DCHECKs and add cleanup (Joyee Cheung) [#26742](https://github.com/nodejs/node/pull/26742)
* \[[`0752a18b88`](https://github.com/nodejs/node/commit/0752a18b88)] - **src**: fix warning in node\_messaging (ZYSzys) [#26682](https://github.com/nodejs/node/pull/26682)
* \[[`b200a46bef`](https://github.com/nodejs/node/commit/b200a46bef)] - **src**: remove `process.binding('config').debugOptions` (Joyee Cheung) [#25999](https://github.com/nodejs/node/pull/25999)
* \[[`c2d374fccc`](https://github.com/nodejs/node/commit/c2d374fccc)] - **src**: remove unused method in env.h (gengjiawen) [#25934](https://github.com/nodejs/node/pull/25934)
* \[[`55569759b3`](https://github.com/nodejs/node/commit/55569759b3)] - **src**: pass along errors from PromiseWrap instantiation (Anna Henningsen) [#25734](https://github.com/nodejs/node/pull/25734)
* \[[`24e6b709ea`](https://github.com/nodejs/node/commit/24e6b709ea)] - **src**: use isolate version of BooleanValue() (cjihrig) [#24883](https://github.com/nodejs/node/pull/24883)
* \[[`b0089a580f`](https://github.com/nodejs/node/commit/b0089a580f)] - **src**: make model counter in `GetCPUInfo()` unsigned (Anna Henningsen) [#23880](https://github.com/nodejs/node/pull/23880)
* \[[`53e0f632db`](https://github.com/nodejs/node/commit/53e0f632db)] - **stream**: inline onwriteStateUpdate() (Luigi Pinca) [#27203](https://github.com/nodejs/node/pull/27203)
* \[[`1a67c9948c`](https://github.com/nodejs/node/commit/1a67c9948c)] - **stream**: remove dead code (Marcos Casagrande) [#27125](https://github.com/nodejs/node/pull/27125)
* \[[`a3d1922958`](https://github.com/nodejs/node/commit/a3d1922958)] - **test**: unskip copyfile permission test (cjihrig) [#27241](https://github.com/nodejs/node/pull/27241)
* \[[`b368571fba`](https://github.com/nodejs/node/commit/b368571fba)] - **test**: move known issue test to parallel (cjihrig) [#27241](https://github.com/nodejs/node/pull/27241)
* \[[`528d100394`](https://github.com/nodejs/node/commit/528d100394)] - **test**: mark some known flakes (Refael Ackermann) [#27225](https://github.com/nodejs/node/pull/27225)
* \[[`e37eee2b1e`](https://github.com/nodejs/node/commit/e37eee2b1e)] - **test**: remove flaky designation for test-cli-node-options (Rich Trott) [#27305](https://github.com/nodejs/node/pull/27305)
* \[[`7167eb2f12`](https://github.com/nodejs/node/commit/7167eb2f12)] - **test**: increase coverage for dns.promises.lookup() (Rich Trott) [#27299](https://github.com/nodejs/node/pull/27299)
* \[[`b66f01d903`](https://github.com/nodejs/node/commit/b66f01d903)] - **test**: skip test-cpu-prof in debug builds with code cache (Joyee Cheung) [#27308](https://github.com/nodejs/node/pull/27308)
* \[[`57ab3b56fc`](https://github.com/nodejs/node/commit/57ab3b56fc)] - **test**: allow leaked global check to be skipped (cjihrig) [#27239](https://github.com/nodejs/node/pull/27239)
* \[[`02885dad5a`](https://github.com/nodejs/node/commit/02885dad5a)] - **test**: add ability to skip common flag checks (Anna Henningsen) [#27254](https://github.com/nodejs/node/pull/27254)
* \[[`ed893111b9`](https://github.com/nodejs/node/commit/ed893111b9)] - **test**: do not strip left whitespace in pseudo-tty tests (Ruben Bridgewater) [#27244](https://github.com/nodejs/node/pull/27244)
* \[[`8712edf53a`](https://github.com/nodejs/node/commit/8712edf53a)] - **test**: fix postmortem metadata test (Matheus Marchini) [#27265](https://github.com/nodejs/node/pull/27265)
* \[[`d5bb500d0f`](https://github.com/nodejs/node/commit/d5bb500d0f)] - **test**: fix test-benchmark-buffer (Rich Trott) [#27260](https://github.com/nodejs/node/pull/27260)
* \[[`4f8b497991`](https://github.com/nodejs/node/commit/4f8b497991)] - **test**: try to stabalize test-child-process-fork-exec-path.js (Refael Ackermann) [#27277](https://github.com/nodejs/node/pull/27277)
* \[[`c6c37e9e85`](https://github.com/nodejs/node/commit/c6c37e9e85)] - **test**: use assert.rejects() and assert.throws() (Richard Lau) [#27207](https://github.com/nodejs/node/pull/27207)
* \[[`f85ef977e6`](https://github.com/nodejs/node/commit/f85ef977e6)] - **test**: log errors in test-fs-readfile-tostring-fail (Richard Lau) [#27058](https://github.com/nodejs/node/pull/27058)
* \[[`de463f1490`](https://github.com/nodejs/node/commit/de463f1490)] - **test**: ec2 generateKeyPairSync invalid parameter encoding (Ruwan Geeganage) [#27212](https://github.com/nodejs/node/pull/27212)
* \[[`2fed83dee8`](https://github.com/nodejs/node/commit/2fed83dee8)] - **test**: test privateEncrypt/publicDecrypt + padding (Ben Noordhuis) [#27188](https://github.com/nodejs/node/pull/27188)
* \[[`f6bd3b27ee`](https://github.com/nodejs/node/commit/f6bd3b27ee)] - **test**: fix test-dns-idna2008.js (Rich Trott) [#27208](https://github.com/nodejs/node/pull/27208)
* \[[`8cf3af1486`](https://github.com/nodejs/node/commit/8cf3af1486)] - **test**: optimize total Travis run time (Refael Ackermann) [#27182](https://github.com/nodejs/node/pull/27182)
* \[[`abe4183d41`](https://github.com/nodejs/node/commit/abe4183d41)] - **test**: mark some known flakes (Refael Ackermann) [#27193](https://github.com/nodejs/node/pull/27193)
* \[[`06c803d9b9`](https://github.com/nodejs/node/commit/06c803d9b9)] - **test**: pass null params to napi\_create\_function() (Octavian Soldea) [#26998](https://github.com/nodejs/node/pull/26998)
* \[[`2a51ae424a`](https://github.com/nodejs/node/commit/2a51ae424a)] - **test**: cover thenables when we check for promises (szabolcsit) [#24219](https://github.com/nodejs/node/pull/24219)
* \[[`3a6eba3de6`](https://github.com/nodejs/node/commit/3a6eba3de6)] - **test**: use assert.rejects (Ruben Bridgewater) [#27123](https://github.com/nodejs/node/pull/27123)
* \[[`3d6533ea02`](https://github.com/nodejs/node/commit/3d6533ea02)] - **test**: simplify vm-module-errors test (Ruben Bridgewater) [#27123](https://github.com/nodejs/node/pull/27123)
* \[[`d1413305e0`](https://github.com/nodejs/node/commit/d1413305e0)] - **test**: print child stderr in test-http-chunk-problem (Anna Henningsen) [#27117](https://github.com/nodejs/node/pull/27117)
* \[[`f96a6608eb`](https://github.com/nodejs/node/commit/f96a6608eb)] - **test**: fix test-worker-memory.js for large cpu #s (Michael Dawson) [#27090](https://github.com/nodejs/node/pull/27090)
* \[[`93df085386`](https://github.com/nodejs/node/commit/93df085386)] - **test**: fix this scope bug in test-stream2-writable.js (gengjiawen) [#27111](https://github.com/nodejs/node/pull/27111)
* \[[`58aaf58406`](https://github.com/nodejs/node/commit/58aaf58406)] - **test**: fix test-repl-require-after-write (Michaël Zasso) [#27088](https://github.com/nodejs/node/pull/27088)
* \[[`baa54a5ae7`](https://github.com/nodejs/node/commit/baa54a5ae7)] - **test**: cover napi\_get/set/has\_named\_property() (Gabriel Schulhof) [#26947](https://github.com/nodejs/node/pull/26947)
* \[[`c86883cfac`](https://github.com/nodejs/node/commit/c86883cfac)] - **test**: fix test-benchmark-module (Rich Trott) [#27094](https://github.com/nodejs/node/pull/27094)
* \[[`f13733d12d`](https://github.com/nodejs/node/commit/f13733d12d)] - **test**: test vm.runInNewContext() filename option (Ben Noordhuis) [#27056](https://github.com/nodejs/node/pull/27056)
* \[[`666c67e078`](https://github.com/nodejs/node/commit/666c67e078)] - **test**: simplify date inspection tests (Ruben Bridgewater) [#26922](https://github.com/nodejs/node/pull/26922)
* \[[`1375af204a`](https://github.com/nodejs/node/commit/1375af204a)] - **test**: revert fail `coverage` target if tests fail" (Michael Dawson) [#25543](https://github.com/nodejs/node/pull/25543)
* \[[`3235d318dc`](https://github.com/nodejs/node/commit/3235d318dc)] - **test**: add test for \_setSimultaneousAccepts() (Andrey Melikhov) [#24180](https://github.com/nodejs/node/pull/24180)
* \[[`9e8c9be3ff`](https://github.com/nodejs/node/commit/9e8c9be3ff)] - **timers**: rename validateTimerDuration to getTimerDuration (Ruben Bridgewater) [#26809](https://github.com/nodejs/node/pull/26809)
* \[[`a1d05e49fe`](https://github.com/nodejs/node/commit/a1d05e49fe)] - **timers**: support name in validateTimerDuration() (cjihrig) [#26215](https://github.com/nodejs/node/pull/26215)
* \[[`2d5387e143`](https://github.com/nodejs/node/commit/2d5387e143)] - **tls**: add debugging to native TLS code (Anna Henningsen) [#26843](https://github.com/nodejs/node/pull/26843)
* \[[`f87b3a72cd`](https://github.com/nodejs/node/commit/f87b3a72cd)] - **tls**: add CHECK for impossible condition (Anna Henningsen) [#26843](https://github.com/nodejs/node/pull/26843)
* \[[`a1330af6a3`](https://github.com/nodejs/node/commit/a1330af6a3)] - **tls**: remove usage of public require('util') (dnlup) [#26747](https://github.com/nodejs/node/pull/26747)
* \[[`00d49ad673`](https://github.com/nodejs/node/commit/00d49ad673)] - **tls**: null not valid as a renegotiate callback (Sam Roberts) [#25929](https://github.com/nodejs/node/pull/25929)
* \[[`54b4beb506`](https://github.com/nodejs/node/commit/54b4beb506)] - **tls**: support TLS\_client\_method, TLS\_server\_method (Sam Roberts) [#24386](https://github.com/nodejs/node/pull/24386)
* \[[`5ac0308af9`](https://github.com/nodejs/node/commit/5ac0308af9)] - **tools**: refactor mkcodecache (Refael Ackermann) [#27161](https://github.com/nodejs/node/pull/27161)
* \[[`4fd7193579`](https://github.com/nodejs/node/commit/4fd7193579)] - **tools**: implement mkcodecache as an executable (Joyee Cheung) [#27161](https://github.com/nodejs/node/pull/27161)
* \[[`d4e743169e`](https://github.com/nodejs/node/commit/d4e743169e)] - **tools**: update js-yaml to 3.13.1 for lint-md.js (Rich Trott) [#27195](https://github.com/nodejs/node/pull/27195)
* \[[`1fc4255221`](https://github.com/nodejs/node/commit/1fc4255221)] - **tools**: python: ignore instead of select flake8 rules (Refael Ackermann) [#25614](https://github.com/nodejs/node/pull/25614)
* \[[`a16a0fe962`](https://github.com/nodejs/node/commit/a16a0fe962)] - **tools**: python: activate more flake8 rules (Refael Ackermann) [#25614](https://github.com/nodejs/node/pull/25614)
* \[[`0befda6970`](https://github.com/nodejs/node/commit/0befda6970)] - **tools**: python: update flake8 rules (Refael Ackermann) [#25614](https://github.com/nodejs/node/pull/25614)
* \[[`0a25ace9c3`](https://github.com/nodejs/node/commit/0a25ace9c3)] - **tools**: move cpplint configuration to .cpplint (Refael Ackermann) [#27098](https://github.com/nodejs/node/pull/27098)
* \[[`cd2987f83f`](https://github.com/nodejs/node/commit/cd2987f83f)] - **tools**: refloat 4 Node.js patches to cpplint.py (Refael Ackermann) [#27098](https://github.com/nodejs/node/pull/27098)
* \[[`1302e0174a`](https://github.com/nodejs/node/commit/1302e0174a)] - **tools**: bump cpplint.py to 1.4.4 (Refael Ackermann) [#27098](https://github.com/nodejs/node/pull/27098)
* \[[`dd89a1182f`](https://github.com/nodejs/node/commit/dd89a1182f)] - **tools**: print a better message for unexpected use of globals (Michaël Zasso) [#27083](https://github.com/nodejs/node/pull/27083)
* \[[`39141426d4`](https://github.com/nodejs/node/commit/39141426d4)] - **tools**: update capitalize-comments eslint rule (Ruben Bridgewater) [#26849](https://github.com/nodejs/node/pull/26849)
* \[[`964174e339`](https://github.com/nodejs/node/commit/964174e339)] - **tools,doc**: fix 404 broken links in docs (Gerson Niño) [#27168](https://github.com/nodejs/node/pull/27168)
* \[[`bbfa93af3d`](https://github.com/nodejs/node/commit/bbfa93af3d)] - **url**: refactor validateHostname (Ruben Bridgewater) [#26809](https://github.com/nodejs/node/pull/26809)
* \[[`2e4ceb5747`](https://github.com/nodejs/node/commit/2e4ceb5747)] - **util**: access process states lazily in debuglog (Joyee Cheung) [#27281](https://github.com/nodejs/node/pull/27281)
* \[[`2948e96afd`](https://github.com/nodejs/node/commit/2948e96afd)] - **util**: fix wrong usage of Error.prepareStackTrace (Simon Zünd) [#27250](https://github.com/nodejs/node/pull/27250)
* \[[`a9bf6652b5`](https://github.com/nodejs/node/commit/a9bf6652b5)] - **util**: use minimal object inspection with %s specifier (Ruben Bridgewater) [#26927](https://github.com/nodejs/node/pull/26927)
* \[[`f7c96856f9`](https://github.com/nodejs/node/commit/f7c96856f9)] - **util**: improve error property inspection (Ruben Bridgewater) [#26984](https://github.com/nodejs/node/pull/26984)
* \[[`14b2db0145`](https://github.com/nodejs/node/commit/14b2db0145)] - **util**: improve `inspect()` compact number mode (Ruben Bridgewater) [#26984](https://github.com/nodejs/node/pull/26984)
* \[[`0f58ae392b`](https://github.com/nodejs/node/commit/0f58ae392b)] - **util**: `format()` now formats bigint and booleans (Ruben Bridgewater) [#25046](https://github.com/nodejs/node/pull/25046)
* \[[`9752fce34d`](https://github.com/nodejs/node/commit/9752fce34d)] - **util**: improve format performance (Ruben Bridgewater) [#24981](https://github.com/nodejs/node/pull/24981)
* \[[`e9fb92dc42`](https://github.com/nodejs/node/commit/e9fb92dc42)] - **vm**: remove require('util') from lib/vm/source\_text\_module.js (freestraws) [#27285](https://github.com/nodejs/node/pull/27285)
* \[[`002dacb7f7`](https://github.com/nodejs/node/commit/002dacb7f7)] - **worker**: handle exception when creating execArgv errors (Anna Henningsen) [#27245](https://github.com/nodejs/node/pull/27245)
* \[[`d070f5d965`](https://github.com/nodejs/node/commit/d070f5d965)] - **worker**: improve coverage (Ruben Bridgewater) [#27230](https://github.com/nodejs/node/pull/27230)
* \[[`5450e48f69`](https://github.com/nodejs/node/commit/5450e48f69)] - **worker**: simplify filename checks (Ruben Bridgewater) [#27233](https://github.com/nodejs/node/pull/27233)

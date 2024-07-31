# Node.js 0.12 ChangeLog

<!--lint disable maximum-line-length no-literal-urls prohibited-strings-->

<table>
<tr>
<th>Stable</th>
</tr>
<tr>
<td>
<a href="#0.12.18">0.12.18</a><br/>
<a href="#0.12.17">0.12.17</a><br/>
<a href="#0.12.16">0.12.16</a><br/>
<a href="#0.12.15">0.12.15</a><br/>
<a href="#0.12.14">0.12.14</a><br/>
<a href="#0.12.13">0.12.13</a><br/>
<a href="#0.12.12">0.12.12</a><br/>
<a href="#0.12.11">0.12.11</a><br/>
<a href="#0.12.10">0.12.10</a><br/>
<a href="#0.12.9">0.12.9</a><br/>
<a href="#0.12.8">0.12.8</a><br/>
<a href="#0.12.7">0.12.7</a><br/>
<a href="#0.12.6">0.12.6</a><br/>
<a href="#0.12.5">0.12.5</a><br/>
<a href="#0.12.4">0.12.4</a><br/>
<a href="#0.12.3">0.12.3</a><br/>
<a href="#0.12.2">0.12.2</a><br/>
<a href="#0.12.1">0.12.1</a><br/>
<a href="#0.12.0">0.12.0</a><br/>
</td>
</tr>
</table>

* Other Versions
  * [22.x](CHANGELOG_V22.md)
  * [21.x](CHANGELOG_V21.md)
  * [20.x](CHANGELOG_V20.md)
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
  * [4.x](CHANGELOG_V4.md)
  * [0.10.x](CHANGELOG_V010.md)
  * [io.js](CHANGELOG_IOJS.md)
  * [Archive](CHANGELOG_ARCHIVE.md)

_Note_: Node.js v0.12 is covered by the
[Node.js Long Term Support Plan](https://github.com/nodejs/LTS) and
will be maintained until December 31st, 2016.

<a id="0.12.18"></a>

## 2016-12-21, Version 0.12.18 (Maintenance), @rvagg

### Notable changes:

* npm: upgrade from v2.15.1 to v2.15.11, including accurate updated license (Jeremiah Senkpiel)
* process: `process.versions.ares` now outputs the c-ares version (Johan Bergström)

### Commits:

* \[[`a47fd4549d`](https://github.com/nodejs/node/commit/a47fd4549d)] - build: add working lint-ci make target (Rod Vagg) <https://github.com/nodejs/node/pull/9151>
* \[[`830584ca59`](https://github.com/nodejs/node/commit/830584ca59)] - deps: define missing operator delete functions (John Barboza) <https://github.com/nodejs/node/pull/10356>
* \[[`c130b31cba`](https://github.com/nodejs/node/commit/c130b31cba)] - deps: upgrade npm to 2.15.11 (Jeremiah Senkpiel) <https://github.com/nodejs/node/pull/9619>
* \[[`bc6766d847`](https://github.com/nodejs/node/commit/bc6766d847)] - doc: update npm license in main LICENSE file (Rod Vagg) <https://github.com/nodejs/node/pull/10352>
* \[[`0cdf344c80`](https://github.com/nodejs/node/commit/0cdf344c80)] - (SEMVER-MINOR) process: reintroduce ares to versions (Johan Bergström) <https://github.com/nodejs/node/pull/9191>
* \[[`d8e27ec30a`](https://github.com/nodejs/node/commit/d8e27ec30a)] - test: mark dgram-multicast-multi-process as flaky (Rod Vagg) <https://github.com/nodejs/node/pull/9150>
* \[[`c722335ead`](https://github.com/nodejs/node/commit/c722335ead)] - tls: fix minor jslint failure (Rod Vagg) <https://github.com/nodejs/node/pull/9107>

<a id="0.12.17"></a>

## 2016-10-18, Version 0.12.17 (Maintenance), @rvagg

This is a security release. All Node.js users should consult the security release summary at <https://nodejs.org/en/blog/vulnerability/october-2016-security-releases/> for details on patched vulnerabilities.

### Notable changes:

* c-ares: fix for single-byte buffer overwrite, CVE-2016-5180, more information at <https://c-ares.haxx.se/adv_20160929.html> (Daniel Stenberg)

### Commits:

* \[[`c5b095ecf8`](https://github.com/nodejs/node/commit/c5b095ecf8)] - deps: avoid single-byte buffer overwrite (Daniel Stenberg) <https://github.com/nodejs/node/pull/8849>

<a id="0.12.16"></a>

## 2016-09-27, Version 0.12.16 (Maintenance), @rvagg

This is a security release. All Node.js users should consult the security release summary at <https://nodejs.org/en/blog/vulnerability/september-2016-security-releases/> for details on patched vulnerabilities.

### Notable changes:

* buffer: Zero-fill excess bytes in new `Buffer` objects created with `Buffer.concat()` while providing a `totalLength` parameter that exceeds the total length of the original `Buffer` objects being concatenated. (Сковорода Никита Андреевич)
* http:
  * CVE-2016-5325 - Properly validate for allowable characters in the `reason` argument in `ServerResponse#writeHead()`. Fixes a possible response splitting attack vector. This introduces a new case where `throw` may occur when configuring HTTP responses, users should already be adopting try/catch here. Originally reported independently by Evan Lucas and Romain Gaucher. (Evan Lucas)
  * Invalid status codes can no longer be sent. Limited to 3 digit numbers between 100 - 999. Lack of proper validation may also serve as a potential response splitting attack vector. Backported from v4.x. (Brian White)
* openssl:
  * Upgrade to 1.0.1u, fixes a number of defects impacting Node.js: CVE-2016-6304 ("OCSP Status Request extension unbounded memory growth", high severity), CVE-2016-2183, CVE-2016-6303, CVE-2016-2178 and CVE-2016-6306.
  * Remove support for loading dynamic third-party engine modules. An attacker may be able to hide malicious code to be inserted into Node.js at runtime by masquerading as one of the dynamic engine modules. Originally reported by Ahmed Zaki (Skype). (Ben Noordhuis, Rod Vagg)
* tls: CVE-2016-7099 - Fix invalid wildcard certificate validation check whereby a TLS server may be able to serve an invalid wildcard certificate for its hostname due to improper validation of `*.` in the wildcard string. Originally reported by Alexander Minozhenko and James Bunton (Atlassian). (Ben Noordhuis)

### Commits:

* \[[`38d7258d89`](https://github.com/nodejs/node/commit/38d7258d89)] - buffer: zero-fill uninitialized bytes in .concat() (Сковорода Никита Андреевич) <https://github.com/nodejs/node-private/pull/66>
* \[[`1ba6d16786`](https://github.com/nodejs/node/commit/1ba6d16786)] - build: turn on -fno-delete-null-pointer-checks (Ben Noordhuis) <https://github.com/nodejs/node/pull/6737>
* \[[`71e4285e27`](https://github.com/nodejs/node/commit/71e4285e27)] - crypto: don't build hardware engines (Rod Vagg) <https://github.com/nodejs/node-private/pull/69>
* \[[`b6e0105a66`](https://github.com/nodejs/node/commit/b6e0105a66)] - deps: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) <https://github.com/nodejs/node-v0.x-archive/pull/25368>
* \[[`1caec97eab`](https://github.com/nodejs/node/commit/1caec97eab)] - deps: fix openssl assembly error on ia32 win32 (Fedor Indutny) <https://github.com/nodejs/node-v0.x-archive/pull/25654>
* \[[`734bc6938b`](https://github.com/nodejs/node/commit/734bc6938b)] - deps: separate sha256/sha512-x86\_64.pl for openssl (Shigeki Ohtsu) <https://github.com/nodejs/node-v0.x-archive/pull/25654>
* \[[`7cc6d4eb5c`](https://github.com/nodejs/node/commit/7cc6d4eb5c)] - deps: copy all openssl header files to include dir (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/8718>
* \[[`4a9da21217`](https://github.com/nodejs/node/commit/4a9da21217)] - deps: upgrade openssl sources to 1.0.1u (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/8718>
* \[[`6d977902bd`](https://github.com/nodejs/node/commit/6d977902bd)] - http: check reason chars in writeHead (Evan Lucas) <https://github.com/nodejs/node-private/pull/47>
* \[[`ad470e496b`](https://github.com/nodejs/node/commit/ad470e496b)] - http: disallow sending obviously invalid status codes (Evan Lucas) <https://github.com/nodejs/node-private/pull/47>
* \[[`9dbde2fc88`](https://github.com/nodejs/node/commit/9dbde2fc88)] - lib: make tls.checkServerIdentity() more strict (Ben Noordhuis) <https://github.com/nodejs/node-private/pull/61>
* \[[`db80592071`](https://github.com/nodejs/node/commit/db80592071)] - openssl: fix keypress requirement in apps on win32 (Shigeki Ohtsu) <https://github.com/nodejs/node-v0.x-archive/pull/25654>

<a id="0.12.15"></a>

## 2016-06-23, Version 0.12.15 (Maintenance), @rvagg

### Notable changes:

This is a security release. All Node.js users should consult the security release summary at <https://nodejs.org/en/blog/vulnerability/june-2016-security-releases/> for details on patched vulnerabilities.

* libuv: (CVE-2014-9748) Fixes a bug in the read/write locks implementation for Windows XP and Windows 2003 that can lead to undefined and potentially unsafe behaviour. More information can be found at <https://github.com/libuv/libuv/issues/515> or at <https://nodejs.org/en/blog/vulnerability/june-2016-security-releases/>.
* V8: (CVE-2016-1669) Fixes a potential Buffer overflow vulnerability discovered in V8, more details can be found in the CVE at <https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2016-1669> or at <https://nodejs.org/en/blog/vulnerability/june-2016-security-releases/>.

### Commits:

* \[[`da8501edf6`](https://github.com/nodejs/node/commit/da8501edf6)] - deps: backport bd1777fd from libuv upstream (Rod Vagg)
* \[[`9207a00f8e`](https://github.com/nodejs/node/commit/9207a00f8e)] - deps: backport 85adf43e from libuv upstream (Rod Vagg)
* \[[`9627f34230`](https://github.com/nodejs/node/commit/9627f34230)] - deps: backport 98239224 from libuv upstream (Rod Vagg)
* \[[`5df21b2e36`](https://github.com/nodejs/node/commit/5df21b2e36)] - deps: backport 9a4fd268 from libuv upstream (Rod Vagg)
* \[[`e75de35057`](https://github.com/nodejs/node/commit/e75de35057)] - deps: backport 3eb6764a from libuv upstream (Rod Vagg)
* \[[`a113e02f16`](https://github.com/nodejs/node/commit/a113e02f16)] - deps: backport 3a9bfec from v8 upstream (Ben Noordhuis)
* \[[`8138055c88`](https://github.com/nodejs/node/commit/8138055c88)] - test: fix test failure due to expired certificates (Ben Noordhuis) <https://github.com/nodejs/node/pull/7195>

<a id="0.12.14"></a>

## 2016-05-06, Version 0.12.14 (Maintenance), @rvagg

### Notable changes:

* npm: Correct erroneous version number in v2.15.1 code (Forrest L Norvell) <https://github.com/nodejs/node/pull/5988>
* openssl: Upgrade to v1.0.1t, addressing security vulnerabilities (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/6553>
  * Fixes CVE-2016-2107 "Padding oracle in AES-NI CBC MAC check"
  * Fixes CVE-2016-2105 "EVP\_EncodeUpdate overflow"
  * See <https://nodejs.org/en/blog/vulnerability/openssl-may-2016/> for full details

### Commits:

* \[[`3e99ee1b47`](https://github.com/nodejs/node/commit/3e99ee1b47)] - deps: completely upgrade npm in LTS to 2.15.1 (Forrest L Norvell) <https://github.com/nodejs/node/pull/5988>
* \[[`2b63396e1f`](https://github.com/nodejs/node/commit/2b63396e1f)] - deps: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) <https://github.com/joyent/node/pull/25368>
* \[[`f21705df58`](https://github.com/nodejs/node/commit/f21705df58)] - deps: update openssl asm files (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/6553>
* \[[`02b6a6bc27`](https://github.com/nodejs/node/commit/02b6a6bc27)] - deps: fix openssl assembly error on ia32 win32 (Fedor Indutny) <https://github.com/joyent/node/pull/25654>
* \[[`1aecc668b0`](https://github.com/nodejs/node/commit/1aecc668b0)] - deps: separate sha256/sha512-x86\_64.pl for openssl (Shigeki Ohtsu) <https://github.com/joyent/node/pull/25654>
* \[[`39380836a0`](https://github.com/nodejs/node/commit/39380836a0)] - deps: copy all openssl header files to include dir (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/6553>
* \[[`08c8ae44a8`](https://github.com/nodejs/node/commit/08c8ae44a8)] - deps: upgrade openssl sources to 1.0.1t (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/6553>
* \[[`f5a961ab13`](https://github.com/nodejs/node/commit/f5a961ab13)] - openssl: fix keypress requirement in apps on win32 (Shigeki Ohtsu) <https://github.com/joyent/node/pull/25654>
* \[[`810fb211a7`](https://github.com/nodejs/node/commit/810fb211a7)] - tools: remove obsolete npm test-legacy command (Kat Marchán) <https://github.com/nodejs/node/pull/5988>

<a id="0.12.13"></a>

## 2016-03-31, Version 0.12.13 (LTS), @rvagg

### Notable changes

* npm: Upgrade to v2.15.1. (Forrest L Norvell)
* openssl: OpenSSL v1.0.1s disables the EXPORT and LOW ciphers as they are obsolete and not considered safe. This release of Node.js turns on `OPENSSL_NO_WEAK_SSL_CIPHERS` to fully disable the 27 ciphers included in these lists which can be used in SSLv3 and higher. Full details can be found in our LTS discussion on the matter (<https://github.com/nodejs/LTS/issues/85>). (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/5712>

### Commits

* \[[`4041ea6bc5`](https://github.com/nodejs/node/commit/4041ea6bc5)] - deps: upgrade npm in LTS to 2.15.1 (Forrest L Norvell)
* \[[`a115779026`](https://github.com/nodejs/node/commit/a115779026)] - deps: Disable EXPORT and LOW ciphers in openssl (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/5712>
* \[[`ab907eb5a8`](https://github.com/nodejs/node/commit/ab907eb5a8)] - test: skip cluster-disconnect-race on Windows (Gibson Fahnestock) <https://github.com/nodejs/node/pull/5621>
* \[[`9c06db7444`](https://github.com/nodejs/node/commit/9c06db7444)] - test: change tls tests not to use LOW cipher (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/5712>
* \[[`154098a3dc`](https://github.com/nodejs/node/commit/154098a3dc)] - test: bp fix for test-http-get-pipeline-problem.js (Michael Dawson) <https://github.com/nodejs/node/pull/3013>
* \[[`ff2bed6e86`](https://github.com/nodejs/node/commit/ff2bed6e86)] - win,build: support Visual C++ Build Tools 2015 (João Reis) <https://github.com/nodejs/node/pull/5627>

<a id="0.12.12"></a>

## 2016-03-08, Version 0.12.12 (LTS), @rvagg

### Notable changes:

* openssl: Fully remove SSLv2 support, the `--enable-ssl2` command line argument will now produce an error. The DROWN Attack (<https://drownattack.com/>) creates a vulnerability where SSLv2 is enabled by a server, even if a client connection is not using SSLv2. The SSLv2 protocol is widely considered unacceptably broken and should not be supported. More information is available at <https://www.openssl.org/news/vulnerabilities.html#2016-0800>

Note that the upgrade to OpenSSL 1.0.1s in Node.js v0.12.11 removed internal SSLv2 support. The change in this release was originally intended for v0.12.11. The `--enable-ssl2` command line argument now produces an error rather than being a no-op.

### Commits:

* \[[`dbfc9d9241`](https://github.com/nodejs/node/commit/dbfc9d9241)] - crypto,tls: remove SSLv2 support (Ben Noordhuis) <https://github.com/nodejs/node/pull/5536>

<a id="0.12.11"></a>

## 2016-03-03, Version 0.12.11 (LTS), @rvagg

### Notable changes:

* http\_parser: Update to http-parser 2.3.2 to fix an unintentionally strict limitation of allowable header characters. (James M Snell) <https://github.com/nodejs/node/pull/5241>
* domains:
  * Prevent an exit due to an exception being thrown rather than emitting an 'uncaughtException' event on the `process` object when no error handler is set on the domain within which an error is thrown and an 'uncaughtException' event listener is set on `process`. (Julien Gilli) <https://github.com/nodejs/node/pull/3885>
  * Fix an issue where the process would not abort in the proper function call if an error is thrown within a domain with no error handler and `--abort-on-uncaught-exception` is used. (Julien Gilli) <https://github.com/nodejs/node/pull/3885>
* openssl: Upgrade from 1.0.1r to 1.0.1s (Ben Noordhuis) <https://github.com/nodejs/node/pull/5509>
  * Fix a double-free defect in parsing malformed DSA keys that may potentially be used for DoS or memory corruption attacks. It is likely to be very difficult to use this defect for a practical attack and is therefore considered low severity for Node.js users. More info is available at <https://www.openssl.org/news/vulnerabilities.html#2016-0705>
  * Fix a defect that can cause memory corruption in certain very rare cases relating to the internal `BN_hex2bn()` and `BN_dec2bn()` functions. It is believed that Node.js is not invoking the code paths that use these functions so practical attacks via Node.js using this defect are _unlikely_ to be possible. More info is available at <https://www.openssl.org/news/vulnerabilities.html#2016-0797>
  * Fix a defect that makes the CacheBleed Attack (<https://ssrg.nicta.com.au/projects/TS/cachebleed/>) possible. This defect enables attackers to execute side-channel attacks leading to the potential recovery of entire RSA private keys. It only affects the Intel Sandy Bridge (and possibly older) microarchitecture when using hyper-threading. Newer microarchitectures, including Haswell, are unaffected. More info is available at <https://www.openssl.org/news/vulnerabilities.html#2016-0702>

### Commits:

* \[[`1ab6653db9`](https://github.com/nodejs/node/commit/1ab6653db9)] - build: update Node.js logo on OSX installer (Rod Vagg) <https://github.com/nodejs/node/pull/5401>
* \[[`fcc64792ae`](https://github.com/nodejs/node/commit/fcc64792ae)] - child\_process: guard against race condition (Rich Trott) <https://github.com/nodejs/node/pull/5153>
* \[[`6c468df9af`](https://github.com/nodejs/node/commit/6c468df9af)] - child\_process: fix data loss with readable event (Brian White) <https://github.com/nodejs/node/pull/5037>
* \[[`61a22019c2`](https://github.com/nodejs/node/commit/61a22019c2)] - deps: upgrade openssl to 1.0.1s (Ben Noordhuis) <https://github.com/nodejs/node/pull/5509>
* \[[`fa26b13df7`](https://github.com/nodejs/node/commit/fa26b13df7)] - deps: update to http-parser 2.3.2 (James M Snell) <https://github.com/nodejs/node/pull/5241>
* \[[`46c8e2165f`](https://github.com/nodejs/node/commit/46c8e2165f)] - deps: backport 1f8555 from v8's upstream (Trevor Norris) <https://github.com/nodejs/node/pull/3945>
* \[[`ce58c2c31a`](https://github.com/nodejs/node/commit/ce58c2c31a)] - doc: remove SSLv2 descriptions (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/5541>
* \[[`018e4e0b1a`](https://github.com/nodejs/node/commit/018e4e0b1a)] - domains: fix handling of uncaught exceptions (Julien Gilli) <https://github.com/nodejs/node/pull/3885>
* \[[`d421e85dc9`](https://github.com/nodejs/node/commit/d421e85dc9)] - lib: fix cluster handle leak (Rich Trott) <https://github.com/nodejs/node/pull/5152>
* \[[`3a48f0022f`](https://github.com/nodejs/node/commit/3a48f0022f)] - node: fix leaking Context handle (Trevor Norris) <https://github.com/nodejs/node/pull/3945>
* \[[`28dddabf6a`](https://github.com/nodejs/node/commit/28dddabf6a)] - src: fix build error without OpenSSL support (Jörg Krause) <https://github.com/nodejs/node/pull/4201>
* \[[`a79baf03cd`](https://github.com/nodejs/node/commit/a79baf03cd)] - src: use global SealHandleScope (Trevor Norris) <https://github.com/nodejs/node/pull/3945>
* \[[`be39f30447`](https://github.com/nodejs/node/commit/be39f30447)] - test: add test-domain-exit-dispose-again back (Julien Gilli) <https://github.com/nodejs/node/pull/4278>
* \[[`da66166b9a`](https://github.com/nodejs/node/commit/da66166b9a)] - test: fix test-domain-exit-dispose-again (Julien Gilli) <https://github.com/nodejs/node/pull/3991>

<a id="0.12.10"></a>

## 2016-02-09, Version 0.12.10 (LTS), @jasnell

This is an important security release. All Node.js users should consult the security release summary at nodejs.org for details on patched vulnerabilities.

### Notable changes

* http: fix defects in HTTP header parsing for requests and responses that can allow request smuggling (CVE-2016-2086) or response splitting (CVE-2016-2216). HTTP header parsing now aligns more closely with the HTTP spec including restricting the acceptable characters.
* http-parser: upgrade from 2.3.0 to 2.3.1
* openssl: upgrade from 1.0.1q to 1.0.1r. To mitigate against the Logjam attack, TLS clients now reject Diffie-Hellman handshakes with parameters shorter than 1024-bits, up from the previous limit of 768-bits.
* src:
  * introduce new `--security-revert={cvenum}` command line flag for selective reversion of specific CVE fixes
  * allow the fix for CVE-2016-2216 to be selectively reverted using `--security-revert=CVE-2016-2216`
* build:
  * xz compressed tar files will be made available from nodejs.org for v0.12 builds from v0.12.10 onward
  * A headers.tar.gz file will be made available from nodejs.org for v0.12 builds from v0.12.10 onward, a future change to node-gyp will be required to make use of these

### Commits

* \[[`4312848bff`](https://github.com/nodejs/node/commit/4312848bff)] - build: enable xz compressed tarballs where possible (Rod Vagg) <https://github.com/nodejs/node/pull/4894>
* \[[`247626245c`](https://github.com/nodejs/node/commit/247626245c)] - deps: upgrade openssl sources to 1.0.1r (Shigeki Ohtsu) <https://github.com/joyent/node/pull/25368>
* \[[`744c9749fc`](https://github.com/nodejs/node/commit/744c9749fc)] - deps: update http-parser to version 2.3.1 (James M Snell)
* \[[`d1c56ec7d1`](https://github.com/nodejs/node/commit/d1c56ec7d1)] - doc: clarify v0.12.9 notable items (Rod Vagg) <https://github.com/nodejs/node/pull/4154>
* \[[`e128d9a5b4`](https://github.com/nodejs/node/commit/e128d9a5b4)] - http: strictly forbid invalid characters from headers (James M Snell)
* \[[`bdb9f2cf89`](https://github.com/nodejs/node/commit/bdb9f2cf89)] - src: avoiding compiler warnings in node\_revert.cc (James M Snell)
* \[[`23bced1fb3`](https://github.com/nodejs/node/commit/23bced1fb3)] - src: add --security-revert command line flag (James M Snell)
* \[[`f41a3c73e7`](https://github.com/nodejs/node/commit/f41a3c73e7)] - tools: backport tools/install.py for headers (Richard Lau) <https://github.com/nodejs/node/pull/4149>

<a id="0.12.9"></a>

## 2015-12-04, Version 0.12.9 (LTS), @rvagg

Security Update

### Notable changes

* http: Fix CVE-2015-8027, a bug whereby an HTTP socket may no longer have a parser associated with it but a pipelined request attempts to trigger a pause or resume on the non-existent parser, a potential denial-of-service vulnerability. (Fedor Indutny)
* openssl: Upgrade to 1.0.1q, fixes CVE-2015-3194 "Certificate verify crash with missing PSS parameter", a potential denial-of-service vector for Node.js TLS servers using client certificate authentication; TLS clients are also impacted. Details are available at <http://openssl.org/news/secadv/20151203.txt>. (Ben Noordhuis) <https://github.com/nodejs/node/pull/4133>

### Commits

* \[[`8d24a14f2c`](https://github.com/nodejs/node/commit/8d24a14f2c)] - deps: upgrade to openssl 1.0.1q (Ben Noordhuis) <https://github.com/nodejs/node/pull/4133>
* \[[`dfc6f4a9af`](https://github.com/nodejs/node/commit/dfc6f4a9af)] - http: fix pipeline regression (Fedor Indutny)

<a id="0.12.8"></a>

## 2015.11.25, Version 0.12.8 (LTS), @rvagg

* \[[`d9399569bd`](https://github.com/nodejs/node/commit/d9399569bd)] - build: backport tools/release.sh (Rod Vagg) <https://github.com/nodejs/node/pull/3642>
* \[[`78c5b4c8bd`](https://github.com/nodejs/node/commit/78c5b4c8bd)] - build: backport config for new CI infrastructure (Rod Vagg) <https://github.com/nodejs/node/pull/3642>
* \[[`83441616a5`](https://github.com/nodejs/node/commit/83441616a5)] - build: fix --without-ssl compile time error (Ben Noordhuis) <https://github.com/nodejs/node/pull/3825>
* \[[`8887666b0b`](https://github.com/nodejs/node/commit/8887666b0b)] - build: update manifest to include Windows 10 (Lucien Greathouse) <https://github.com/nodejs/node/pull/2843>
* \[[`08afe4ec8e`](https://github.com/nodejs/node/commit/08afe4ec8e)] - build: add MSVS 2015 support (Rod Vagg) <https://github.com/nodejs/node/pull/2843>
* \[[`4f2456369c`](https://github.com/nodejs/node/commit/4f2456369c)] - build: work around VS2015 issue in ICU <56 (Steven R. Loomis) <https://github.com/nodejs/node-v0.x-archive/pull/25804>
* \[[`15030f26fd`](https://github.com/nodejs/node/commit/15030f26fd)] - build: Intl: bump ICU4C from 54 to 55 (backport) (Steven R. Loomis) <https://github.com/nodejs/node-v0.x-archive/pull/25856>
* \[[`1083fa70f0`](https://github.com/nodejs/node/commit/1083fa70f0)] - build: run-ci makefile rule (Alexis Campailla) <https://github.com/nodejs/node-v0.x-archive/pull/25653>
* \[[`2d2494cf14`](https://github.com/nodejs/node/commit/2d2494cf14)] - build: support flaky tests in test-ci (Alexis Campailla) <https://github.com/nodejs/node-v0.x-archive/pull/25653>
* \[[`b25d26f2ef`](https://github.com/nodejs/node/commit/b25d26f2ef)] - build: support Jenkins via test-ci (Alexis Campailla) <https://github.com/nodejs/node-v0.x-archive/pull/25653>
* \[[`7e4b47f38a`](https://github.com/nodejs/node/commit/7e4b47f38a)] - build,win: fix node.exe resource version (João Reis) <https://github.com/nodejs/node/pull/3053>
* \[[`e07c86e240`](https://github.com/nodejs/node/commit/e07c86e240)] - build,win: try next MSVS version on failure (João Reis) <https://github.com/nodejs/node/pull/2843>
* \[[`b5a0abcfdf`](https://github.com/nodejs/node/commit/b5a0abcfdf)] - child\_process: clone spawn options argument (cjihrig) <https://github.com/nodejs/node-v0.x-archive/pull/9159>
* \[[`8b81f98c41`](https://github.com/nodejs/node/commit/8b81f98c41)] - configure: add --without-mdb flag (cgalibern) <https://github.com/nodejs/node-v0.x-archive/pull/25707>
* \[[`071c860c2b`](https://github.com/nodejs/node/commit/071c860c2b)] - crypto: replace rwlocks with simple mutexes (Ben Noordhuis) <https://github.com/nodejs/node/pull/2723>
* \[[`ca97fb6be3`](https://github.com/nodejs/node/commit/ca97fb6be3)] - deps: upgrade npm to 2.14.9 (Forrest L Norvell) <https://github.com/nodejs/node/pull/3684>
* \[[`583734342e`](https://github.com/nodejs/node/commit/583734342e)] - deps: fix openssl for MSVS 2015 (Andy Polyakov) <https://github.com/nodejs/node/pull/2843>
* \[[`02c262a4c6`](https://github.com/nodejs/node/commit/02c262a4c6)] - deps: fix gyp to work on MacOSX without XCode (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/2843>
* \[[`f0fba0bce8`](https://github.com/nodejs/node/commit/f0fba0bce8)] - deps: update gyp to 25ed9ac (João Reis) <https://github.com/nodejs/node/pull/2843>
* \[[`f693565813`](https://github.com/nodejs/node/commit/f693565813)] - deps: upgrade to npm 2.13.4 (Kat Marchán) <https://github.com/nodejs/node-v0.x-archive/pull/25825>
* \[[`618b142679`](https://github.com/nodejs/node/commit/618b142679)] - deps,v8: fix compilation in VS2015 (João Reis) <https://github.com/nodejs/node/pull/2843>
* \[[`49b4f0d54e`](https://github.com/nodejs/node/commit/49b4f0d54e)] - doc: backport README.md (Rod Vagg) <https://github.com/nodejs/node/pull/3642>
* \[[`2860c53562`](https://github.com/nodejs/node/commit/2860c53562)] - doc: fixed child\_process.exec doc (Tyler Anton) <https://github.com/nodejs/node-v0.x-archive/pull/14088>
* \[[`4a91fa11a3`](https://github.com/nodejs/node/commit/4a91fa11a3)] - doc: Update docs for os.platform() (George Kotchlamazashvili) <https://github.com/nodejs/node-v0.x-archive/pull/25777>
* \[[`b03ab02fe8`](https://github.com/nodejs/node/commit/b03ab02fe8)] - doc: Change the link for v8 docs to v8dox.com (Chad Walker) <https://github.com/nodejs/node-v0.x-archive/pull/25811>
* \[[`1fd8f37efd`](https://github.com/nodejs/node/commit/1fd8f37efd)] - doc: buffer, adding missing backtick (Dyana Rose) <https://github.com/nodejs/node-v0.x-archive/pull/25811>
* \[[`162d0db3bb`](https://github.com/nodejs/node/commit/162d0db3bb)] - doc: tls.markdown, adjust version from v0.10.39 to v0.10.x (James M Snell) <https://github.com/nodejs/node-v0.x-archive/pull/25591>
* \[[`eda2560cdc`](https://github.com/nodejs/node/commit/eda2560cdc)] - doc: additional refinement to readable event (James M Snell) <https://github.com/nodejs/node-v0.x-archive/pull/25591>
* \[[`881d9bea01`](https://github.com/nodejs/node/commit/881d9bea01)] - doc: readable event clarification (James M Snell) <https://github.com/nodejs/node-v0.x-archive/pull/25591>
* \[[`b6378f0c75`](https://github.com/nodejs/node/commit/b6378f0c75)] - doc: stream.unshift does not reset reading state (James M Snell) <https://github.com/nodejs/node-v0.x-archive/pull/25591>

### Commits

* \[[`0cf9f27703`](https://github.com/nodejs/node/commit/0cf9f27703)] - **deps**: upgrade openssl sources to 1.0.1p [#25654](https://github.com/joyent/node/pull/25654)
* \[[`8917e430b8`](https://github.com/nodejs/node/commit/8917e430b8)] - **deps**: upgrade to npm 2.11.3 [#25545](https://github.com/joyent/node/pull/25545)
* \[[`88a27a9621`](https://github.com/nodejs/node/commit/88a27a9621)] - **V8**: cherry-pick JitCodeEvent patch from upstream (Ben Noordhuis) [#25589](https://github.com/joyent/node/pull/25589)
* \[[`18d413d299`](https://github.com/nodejs/node/commit/18d413d299)] - **win,msi**: create npm folder in AppData directory (Steven Rockarts) [#8838](https://github.com/joyent/node/pull/8838)

<a id="0.12.6"></a>

## 2015-07-03, Version 0.12.6 (Stable)

### Notable changes

* **deps**: Fixed an out-of-band write in utf8 decoder. **This is an important security update** as it can be used to cause a denial of service attack.

### Commits

* \[[`78b0e30954`](https://github.com/nodejs/node/commit/78b0e30954)] - **deps**: fix out-of-band write in utf8 decoder (Fedor Indutny)

<a id="0.12.5"></a>

## 2015-06-22, Version 0.12.5 (Stable)

### Commits

* \[[`456c22f63f`](https://github.com/nodejs/node/commit/456c22f63f)] - **openssl**: upgrade to 1.0.1o (Addressing multiple CVEs) [#25523](https://github.com/joyent/node/pull/25523)
* \[[`20d8db1a42`](https://github.com/nodejs/node/commit/20d8db1a42)] - **npm**: upgrade to 2.11.2 [#25517](https://github.com/joyent/node/pull/25517)
* \[[`50f961596d`](https://github.com/nodejs/node/commit/50f961596d)] - **uv**: upgrade to 1.6.1 [#25475](https://github.com/joyent/node/pull/25475)
* \[[`b81a643f9a`](https://github.com/nodejs/node/commit/b81a643f9a)] - **V8**: avoid deadlock when profiling is active (Dmitri Melikyan) [#25309](https://github.com/joyent/node/pull/25309)
* \[[`9d19dfbfdb`](https://github.com/nodejs/node/commit/9d19dfbfdb)] - **install**: fix source path for openssl headers (Oguz Bastemur) [#14089](https://github.com/joyent/node/pull/14089)
* \[[`4028669531`](https://github.com/nodejs/node/commit/4028669531)] - **install**: make sure opensslconf.h is overwritten (Oguz Bastemur) [#14089](https://github.com/joyent/node/pull/14089)
* \[[`d38e865fce`](https://github.com/nodejs/node/commit/d38e865fce)] - **timers**: fix timeout when added in timer's callback (Julien Gilli) [#17203](https://github.com/joyent/node/pull/17203)
* \[[`e7c84f82c7`](https://github.com/nodejs/node/commit/e7c84f82c7)] - **windows**: broadcast WM\_SETTINGCHANGE after install (Mathias Küsel) [#25100](https://github.com/joyent/node/pull/25100)

<a id="0.12.4"></a>

## 2015-05-22, Version 0.12.4 (Stable)

### Commits

* \[[`202c18bbc3`](https://github.com/nodejs/node/commit/202c18bbc3)] - **npm**: upgrade to 2.10.1 [#25364](https://github.com/joyent/node/pull/25364)
* \[[`6157697bd5`](https://github.com/nodejs/node/commit/6157697bd5)] - **V8**: revert v8 Array.prototype.values() removal (cjihrig) [#25328](https://github.com/joyent/node/pull/25328)
* \[[`3122052890`](https://github.com/nodejs/node/commit/3122052890)] - **win**: bring back xp/2k3 support (Bert Belder) [#25367](https://github.com/joyent/node/pull/25367)

<a id="0.12.3"></a>

## 2015-05-13, Version 0.12.3 (Stable)

### Commits

* \[[`32166a90cf`](https://github.com/nodejs/node/commit/32166a90cf)] - **V8**: update to 3.28.71.19 [#18206](https://github.com/joyent/node/pull/18206)
* \[[`84f1ab6114`](https://github.com/nodejs/node/commit/84f1ab6114)] - **uv**: upgrade to 1.5.0 [#25141](https://github.com/joyent/node/pull/25141)
* \[[`03cfbd65fb`](https://github.com/nodejs/node/commit/03cfbd65fb)] - **npm**: upgrade to 2.9.1 [#25289](https://github.com/joyent/node/pull/25289)
* \[[`80cdae855f`](https://github.com/nodejs/node/commit/80cdae855f)] - **V8**: don't busy loop in v8 cpu profiler thread (Mike Tunnicliffe) [#25268](https://github.com/joyent/node/pull/25268)
* \[[`2a5f4bd7ce`](https://github.com/nodejs/node/commit/2a5f4bd7ce)] - **V8**: fix issue with let bindings in for loops (adamk) [#23948](https://github.com/joyent/node/pull/23948)
* \[[`f0ef597e09`](https://github.com/nodejs/node/commit/f0ef597e09)] - **debugger**: don't spawn child process in remote mode (Jackson Tian) [#14172](https://github.com/joyent/node/pull/14172)
* \[[`0e392f3b68`](https://github.com/nodejs/node/commit/0e392f3b68)] - **net**: do not set V4MAPPED on FreeBSD (Julien Gilli) [#18204](https://github.com/joyent/node/pull/18204)
* \[[`101e103e3b`](https://github.com/nodejs/node/commit/101e103e3b)] - **repl**: make 'Unexpected token' errors recoverable (Julien Gilli) [#8875](https://github.com/joyent/node/pull/8875)
* \[[`d5b32246fb`](https://github.com/nodejs/node/commit/d5b32246fb)] - **src**: backport ignore ENOTCONN on shutdown race (Ben Noordhuis) [#14480](https://github.com/joyent/node/pull/14480)
* \[[`f99eaefe75`](https://github.com/nodejs/node/commit/f99eaefe75)] - **src**: fix backport of SIGINT crash fix on FreeBSD (Julien Gilli) [#14819](https://github.com/joyent/node/pull/14819)

<a id="0.12.2"></a>

## 2015-03-31, Version 0.12.2 (Stable)

### Commits

* \[[`7a37910f25`](https://github.com/nodejs/node/commit/7a37910f25)] - **uv**: Upgrade to 1.4.2 [#9179](https://github.com/joyent/node/pull/9179)
* \[[`2704c62933`](https://github.com/nodejs/node/commit/2704c62933)] - **npm**: Upgrade to 2.7.4 [#14180](https://github.com/joyent/node/pull/14180)
* \[[`a103712a62`](https://github.com/nodejs/node/commit/a103712a62)] - **V8**: do not add extra newline in log file (Julien Gilli)
* \[[`2fc5eeb3da`](https://github.com/nodejs/node/commit/2fc5eeb3da)] - **V8**: Fix --max\_old\_space\_size=4096 integer overflow (Andrei Sedoi) [#9200](https://github.com/joyent/node/pull/9200)
* \[[`605329d7f7`](https://github.com/nodejs/node/commit/605329d7f7)] - **asyncwrap**: fix constructor condition for early ret (Trevor Norris) [#9146](https://github.com/joyent/node/pull/9146)
* \[[`a33f23cbbc`](https://github.com/nodejs/node/commit/a33f23cbbc)] - **buffer**: align chunks on 8-byte boundary (Fedor Indutny) [#9375](https://github.com/joyent/node/pull/9375)
* \[[`a35ba2f67d`](https://github.com/nodejs/node/commit/a35ba2f67d)] - **buffer**: fix pool offset adjustment (Trevor Norris)
* \[[`c0766eb1a4`](https://github.com/nodejs/node/commit/c0766eb1a4)] - **build**: fix use of strict aliasing (Trevor Norris) [#9179](https://github.com/joyent/node/pull/9179)
* \[[`6c3647c38d`](https://github.com/nodejs/node/commit/6c3647c38d)] - **console**: allow Object.prototype fields as labels (Colin Ihrig) [#9116](https://github.com/joyent/node/pull/9116)
* \[[`4823afcbe2`](https://github.com/nodejs/node/commit/4823afcbe2)] - **fs**: make F\_OK/R\_OK/W\_OK/X\_OK not writable (Jackson Tian) [#9060](https://github.com/joyent/node/pull/9060)
* \[[`b3aa876f08`](https://github.com/nodejs/node/commit/b3aa876f08)] - **fs**: properly handle fd passed to truncate() (Bruno Jouhier) [#9161](https://github.com/joyent/node/pull/9161)
* \[[`d6484f3f7b`](https://github.com/nodejs/node/commit/d6484f3f7b)] - **http**: fix assert on data/end after socket error (Fedor Indutny) [#14087](https://github.com/joyent/node/pull/14087)
* \[[`04b63e022a`](https://github.com/nodejs/node/commit/04b63e022a)] - **lib**: fix max size check in Buffer constructor (Ben Noordhuis) [#657](https://github.com/iojs/io.js/pull/657)
* \[[`2411bea0df`](https://github.com/nodejs/node/commit/2411bea0df)] - **lib**: fix stdio/ipc sync i/o regression (Ben Noordhuis) [#9179](https://github.com/joyent/node/pull/9179)
* \[[`b8604fa480`](https://github.com/nodejs/node/commit/b8604fa480)] - **module**: replace NativeModule.require (Herbert Vojčík) [#9201](https://github.com/joyent/node/pull/9201)
* \[[`1a2a4dac23`](https://github.com/nodejs/node/commit/1a2a4dac23)] - **net**: allow port 0 in connect() (cjihrig) [#9268](https://github.com/joyent/node/pull/9268)
* \[[`bada87bd66`](https://github.com/nodejs/node/commit/bada87bd66)] - **net**: unref timer in parent sockets (Fedor Indutny) [#891](https://github.com/iojs/io.js/pull/891)
* \[[`c66f8c21f0`](https://github.com/nodejs/node/commit/c66f8c21f0)] - **path**: refactor for performance and consistency (Nathan Woltman) [#9289](https://github.com/joyent/node/pull/9289)
* \[[`9deade4322`](https://github.com/nodejs/node/commit/9deade4322)] - **smalloc**: extend user API (Trevor Norris) [#905](https://github.com/iojs/io.js/pull/905)
* \[[`61fe1fe21b`](https://github.com/nodejs/node/commit/61fe1fe21b)] - **src**: fix for SIGINT crash on FreeBSD (Fedor Indutny) [#14184](https://github.com/joyent/node/pull/14184)
* \[[`b233131901`](https://github.com/nodejs/node/commit/b233131901)] - **src**: fix builtin modules failing with --use-strict (Julien Gilli) [#9237](https://github.com/joyent/node/pull/9237)
* \[[`7e9d2f8de8`](https://github.com/nodejs/node/commit/7e9d2f8de8)] - **watchdog**: fix timeout for early polling return (Saúl Ibarra Corretgé) [#9410](https://github.com/joyent/node/pull/9410)

<a id="0.12.1"></a>

## 2015-03-23, Version 0.12.1 (Stable)

### Commits

* \[[`3b511a8ccd`](https://github.com/nodejs/node/commit/3b511a8ccd)] - **openssl**: upgrade to 1.0.1m (Addressing multiple CVES)

<a id="0.12.0"></a>

## 2015-02-06, Version 0.12.0 (Stable)

### Commits

* \[[`087a7519ce`](https://github.com/nodejs/node/commit/087a7519ce)] - **npm**: Upgrade to 2.5.1
* \[[`4312f8d760`](https://github.com/nodejs/node/commit/4312f8d760)] - **mdb\_v8**: update for v0.12 (Dave Pacheco)

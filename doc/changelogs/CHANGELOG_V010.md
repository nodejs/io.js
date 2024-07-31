# Node.js 0.10 ChangeLog

<!--lint disable maximum-line-length no-literal-urls prohibited-strings-->

<table>
<tr>
<th colspan="2">Stable</th>
</tr>
<tr>
<td valign="top">
<a href="#0.10.48">0.10.48</a><br/>
<a href="#0.10.47">0.10.47</a><br/>
<a href="#0.10.46">0.10.46</a><br/>
<a href="#0.10.45">0.10.45</a><br/>
<a href="#0.10.44">0.10.44</a><br/>
<a href="#0.10.43">0.10.43</a><br/>
<a href="#0.10.42">0.10.42</a><br/>
<a href="#0.10.41">0.10.41</a><br/>
<a href="#0.10.40">0.10.40</a><br/>
<a href="#0.10.39">0.10.39</a><br/>
<a href="#0.10.38">0.10.38</a><br/>
<a href="#0.10.37">0.10.37</a><br/>
<a href="#0.10.36">0.10.36</a><br/>
<a href="#0.10.35">0.10.35</a><br/>
<a href="#0.10.34">0.10.34</a><br/>
<a href="#0.10.33">0.10.33</a><br/>
<a href="#0.10.32">0.10.32</a><br/>
<a href="#0.10.31">0.10.31</a><br/>
<a href="#0.10.30">0.10.30</a><br/>
<a href="#0.10.29">0.10.29</a><br/>
<a href="#0.10.28">0.10.28</a><br/>
<a href="#0.10.27">0.10.27</a><br/>
<a href="#0.10.26">0.10.26</a><br/>
<a href="#0.10.25">0.10.25</a><br/>
<a href="#0.10.24">0.10.24</a><br/>
<a href="#0.10.23">0.10.23</a><br/>
</td>
<td valign="top">
<a href="#0.10.22">0.10.22</a><br/>
<a href="#0.10.21">0.10.21</a><br/>
<a href="#0.10.20">0.10.20</a><br/>
<a href="#0.10.19">0.10.19</a><br/>
<a href="#0.10.18">0.10.18</a><br/>
<a href="#0.10.17">0.10.17</a><br/>
<a href="#0.10.16">0.10.16</a><br/>
<a href="#0.10.15">0.10.15</a><br/>
<a href="#0.10.14">0.10.14</a><br/>
<a href="#0.10.13">0.10.13</a><br/>
<a href="#0.10.12">0.10.12</a><br/>
<a href="#0.10.11">0.10.11</a><br/>
<a href="#0.10.10">0.10.10</a><br/>
<a href="#0.10.9">0.10.9</a><br/>
<a href="#0.10.8">0.10.8</a><br/>
<a href="#0.10.7">0.10.7</a><br/>
<a href="#0.10.6">0.10.6</a><br/>
<a href="#0.10.5">0.10.5</a><br/>
<a href="#0.10.4">0.10.4</a><br/>
<a href="#0.10.3">0.10.3</a><br/>
<a href="#0.10.2">0.10.2</a><br/>
<a href="#0.10.1">0.10.1</a><br/>
<a href="#0.10.0">0.10.0</a><br/>
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
  * [0.12.x](CHANGELOG_V012.md)
  * [io.js](CHANGELOG_IOJS.md)
  * [Archive](CHANGELOG_ARCHIVE.md)

_Note_: Node.js v0.10 is covered by the
[Node.js Long Term Support Plan](https://github.com/nodejs/LTS) and
will be maintained until October 2016.

<a id="0.10.48"></a>

## 2016-10-18, Version 0.10.48 (Maintenance), @rvagg

This is a security release. All Node.js users should consult the security release summary at <https://nodejs.org/en/blog/vulnerability/october-2016-security-releases/> for details on patched vulnerabilities.

### Notable changes

* c-ares: fix for single-byte buffer overwrite, CVE-2016-5180, more information at <https://c-ares.haxx.se/adv_20160929.html> (Rod Vagg)

### Commits

* \[[`a14a6a3a11`](https://github.com/nodejs/node/commit/a14a6a3a11)] - deps: c-ares, avoid single-byte buffer overwrite (Rod Vagg) <https://github.com/nodejs/node/pull/9108>
* \[[`b798f598af`](https://github.com/nodejs/node/commit/b798f598af)] - tls: fix minor jslint failure (Rod Vagg) <https://github.com/nodejs/node/pull/9107>
* \[[`92b232ba01`](https://github.com/nodejs/node/commit/92b232ba01)] - win,build: try multiple timeservers when signing (Rod Vagg) <https://github.com/nodejs/node/pull/9155>

<a id="0.10.47"></a>

## 2016-09-27, Version 0.10.47 (Maintenance), @rvagg

This is a security release. All Node.js users should consult the security release summary at <https://nodejs.org/en/blog/vulnerability/september-2016-security-releases/> for details on patched vulnerabilities.

### Notable changes:

* buffer: Zero-fill excess bytes in new `Buffer` objects created with `Buffer.concat()` while providing a `totalLength` parameter that exceeds the total length of the original `Buffer` objects being concatenated. (Сковорода Никита Андреевич)
* http:
  * CVE-2016-5325 - Properly validate for allowable characters in the `reason` argument in `ServerResponse#writeHead()`. Fixes a possible response splitting attack vector. This introduces a new case where `throw` may occur when configuring HTTP responses, users should already be adopting try/catch here. Originally reported independently by Evan Lucas and Romain Gaucher. (Evan Lucas)
  * Invalid status codes can no longer be sent. Limited to 3 digit numbers between 100 - 999. Lack of proper validation may also serve as a potential response splitting attack vector. Backported from v4.x. (Brian White)
* openssl: Upgrade to 1.0.1u, fixes a number of defects impacting Node.js: CVE-2016-6304 ("OCSP Status Request extension unbounded memory growth", high severity), CVE-2016-2183, CVE-2016-2183, CVE-2016-2178 and CVE-2016-6306.
* tls: CVE-2016-7099 - Fix invalid wildcard certificate validation check whereby a TLS server may be able to serve an invalid wildcard certificate for its hostname due to improper validation of `*.` in the wildcard string. Originally reported by Alexander Minozhenko and James Bunton (Atlassian) (Ben Noordhuis)

### Commits:

* \[[`fc259c7dc4`](https://github.com/nodejs/node/commit/fc259c7dc4)] - buffer: zero-fill uninitialized bytes in .concat() (Сковорода Никита Андреевич) <https://github.com/nodejs/node-private/pull/67>
* \[[`35b49ed4bb`](https://github.com/nodejs/node/commit/35b49ed4bb)] - build: turn on -fno-delete-null-pointer-checks (Ben Noordhuis) <https://github.com/nodejs/node/pull/6738>
* \[[`03f4920d6a`](https://github.com/nodejs/node/commit/03f4920d6a)] - crypto: don't build hardware engines (Rod Vagg) <https://github.com/nodejs/node-private/pull/68>
* \[[`1cbdb1957d`](https://github.com/nodejs/node/commit/1cbdb1957d)] - deps: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) <https://github.com/nodejs/node-v0.x-archive/pull/25368>
* \[[`c66408cd0c`](https://github.com/nodejs/node/commit/c66408cd0c)] - deps: fix openssl assembly error on ia32 win32 (Fedor Indutny) <https://github.com/nodejs/node-v0.x-archive/pull/25654>
* \[[`68f88ea792`](https://github.com/nodejs/node/commit/68f88ea792)] - deps: separate sha256/sha512-x86\_64.pl for openssl (Shigeki Ohtsu) <https://github.com/nodejs/node-v0.x-archive/pull/25654>
* \[[`884d50b348`](https://github.com/nodejs/node/commit/884d50b348)] - deps: copy all openssl header files to include dir (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/8718>
* \[[`bfd6cb5699`](https://github.com/nodejs/node/commit/bfd6cb5699)] - deps: upgrade openssl sources to 1.0.1u (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/8718>
* \[[`3614a173d0`](https://github.com/nodejs/node/commit/3614a173d0)] - http: check reason chars in writeHead (Evan Lucas) <https://github.com/nodejs/node-private/pull/48>
* \[[`f2433430ca`](https://github.com/nodejs/node/commit/f2433430ca)] - http: disallow sending obviously invalid status codes (Evan Lucas) <https://github.com/nodejs/node-private/pull/48>
* \[[`0d7e21ee7b`](https://github.com/nodejs/node/commit/0d7e21ee7b)] - lib: make tls.checkServerIdentity() more strict (Ben Noordhuis) <https://github.com/nodejs/node-private/pull/62>
* \[[`1f4a6f5bd1`](https://github.com/nodejs/node/commit/1f4a6f5bd1)] - openssl: fix keypress requirement in apps on win32 (Shigeki Ohtsu) <https://github.com/nodejs/node-v0.x-archive/pull/25654>
* \[[`88dcc7f5bb`](https://github.com/nodejs/node/commit/88dcc7f5bb)] - v8: fix -Wsign-compare warning in Zone::New() (Ben Noordhuis) <https://github.com/nodejs/node-private/pull/62>
* \[[`fd8ac56c75`](https://github.com/nodejs/node/commit/fd8ac56c75)] - v8: fix build errors with g++ 6.1.1 (Ben Noordhuis) <https://github.com/nodejs/node-private/pull/62>

<a id="0.10.46"></a>

## 2016-06-23, Version 0.10.46 (Maintenance), @rvagg

### Notable changes:

This is a security release. All Node.js users should consult the security release summary at <https://nodejs.org/en/blog/vulnerability/june-2016-security-releases/> for details on patched vulnerabilities.

* libuv: (CVE-2014-9748) Fixes a bug in the read/write locks implementation for Windows XP and Windows 2003 that can lead to undefined and potentially unsafe behaviour. More information can be found at <https://github.com/libuv/libuv/issues/515> or at <https://nodejs.org/en/blog/vulnerability/june-2016-security-releases/>.
* V8: (CVE-2016-1669) Fixes a potential Buffer overflow vulnerability discovered in V8, more details can be found in the CVE at <https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2016-1669> or at <https://nodejs.org/en/blog/vulnerability/june-2016-security-releases/>.

### Commits:

* \[[`3374f57973`](https://github.com/nodejs/node/commit/3374f57973)] - deps: update libuv to 0.10.37 (Saúl Ibarra Corretgé) <https://github.com/nodejs/node/pull/7293>
* \[[`fcb9145e29`](https://github.com/nodejs/node/commit/fcb9145e29)] - deps: backport 3a9bfec from v8 upstream (Myles Borins) <https://github.com/nodejs/node-private/pull/43>

<a id="0.10.45"></a>

## 2016-05-06, Version 0.10.45 (Maintenance), @rvagg

### Notable changes:

* npm: Correct erroneous version number in v2.15.1 code (Forrest L Norvell) <https://github.com/nodejs/node/pull/5987>
* openssl: Upgrade to v1.0.1t, addressing security vulnerabilities (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/6553>
  * Fixes CVE-2016-2107 "Padding oracle in AES-NI CBC MAC check"
  * See <https://nodejs.org/en/blog/vulnerability/openssl-may-2016/> for full details

### Commits:

* \[[`3cff81c7d6`](https://github.com/nodejs/node/commit/3cff81c7d6)] - deps: completely upgrade npm in LTS to 2.15.1 (Forrest L Norvell) <https://github.com/nodejs/node/pull/5987>
* \[[`7c22f19009`](https://github.com/nodejs/node/commit/7c22f19009)] - deps: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) <https://github.com/joyent/node/pull/25368>
* \[[`5d78366937`](https://github.com/nodejs/node/commit/5d78366937)] - deps: update openssl asm files (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/6553>
* \[[`2bc2427cb7`](https://github.com/nodejs/node/commit/2bc2427cb7)] - deps: fix openssl assembly error on ia32 win32 (Fedor Indutny) <https://github.com/joyent/node/pull/25654>
* \[[`8df4b0914c`](https://github.com/nodejs/node/commit/8df4b0914c)] - deps: separate sha256/sha512-x86\_64.pl for openssl (Shigeki Ohtsu) <https://github.com/joyent/node/pull/25654>
* \[[`11eefefb17`](https://github.com/nodejs/node/commit/11eefefb17)] - deps: copy all openssl header files to include dir (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/6553>
* \[[`61ccc27b54`](https://github.com/nodejs/node/commit/61ccc27b54)] - deps: upgrade openssl sources to 1.0.1t (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/6553>
* \[[`aa02438274`](https://github.com/nodejs/node/commit/aa02438274)] - openssl: fix keypress requirement in apps on win32 (Shigeki Ohtsu) <https://github.com/joyent/node/pull/25654>

<a id="0.10.44"></a>

## 2016-03-31, Version 0.10.44 (Maintenance), @rvagg

### Notable changes

* npm: Upgrade to v2.15.1. Fixes a security flaw in the use of authentication tokens in HTTP requests that would allow an attacker to set up a server that could collect tokens from users of the command-line interface. Authentication tokens have previously been sent with every request made by the CLI for logged-in users, regardless of the destination of the request. This update fixes this by only including those tokens for requests made against the registry or registries used for the current install. IMPORTANT: This is a major upgrade to npm v2 LTS from the previously deprecated npm v1. (Forrest L Norvell) <https://github.com/nodejs/node/pull/5967>
* openssl: OpenSSL v1.0.1s disables the EXPORT and LOW ciphers as they are obsolete and not considered safe. This release of Node.js turns on `OPENSSL_NO_WEAK_SSL_CIPHERS` to fully disable the 27 ciphers included in these lists which can be used in SSLv3 and higher. Full details can be found in our LTS discussion on the matter (<https://github.com/nodejs/LTS/issues/85>). (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/5712>

### Commits

* \[[`feceb77d7e`](https://github.com/nodejs/node/commit/feceb77d7e)] - deps: upgrade npm in LTS to 2.15.1 (Forrest L Norvell) <https://github.com/nodejs/node/pull/5968>
* \[[`0847954331`](https://github.com/nodejs/node/commit/0847954331)] - deps: Disable EXPORT and LOW ciphers in openssl (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/5712>
* \[[`6bb86e727a`](https://github.com/nodejs/node/commit/6bb86e727a)] - test: change tls tests not to use LOW cipher (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/5712>
* \[[`905bec29ad`](https://github.com/nodejs/node/commit/905bec29ad)] - win,build: support Visual C++ Build Tools 2015 (João Reis) <https://github.com/nodejs/node/pull/5627>

<a id="0.10.43"></a>

## 2016-03-04, Version 0.10.43 (Maintenance), @rvagg

### Notable changes:

* http\_parser: Update to http-parser 1.2 to fix an unintentionally strict limitation of allowable header characters. (James M Snell) <https://github.com/nodejs/node/pull/5242>
* domains:
  * Prevent an exit due to an exception being thrown rather than emitting an `'uncaughtException'` event on the `process` object when no error handler is set on the domain within which an error is thrown and an `'uncaughtException'` event listener is set on `process`. (Julien Gilli) <https://github.com/nodejs/node/pull/3887>
  * Fix an issue where the process would not abort in the proper function call if an error is thrown within a domain with no error handler and `--abort-on-uncaught-exception` is used. (Julien Gilli) <https://github.com/nodejs/node/pull/3887>
* openssl: Upgrade from 1.0.1r to 1.0.1s (Ben Noordhuis) <https://github.com/nodejs/node/pull/5508>
  * Fix a double-free defect in parsing malformed DSA keys that may potentially be used for DoS or memory corruption attacks. It is likely to be very difficult to use this defect for a practical attack and is therefore considered low severity for Node.js users. More info is available at <https://www.openssl.org/news/vulnerabilities.html#2016-0705>
  * Fix a defect that can cause memory corruption in certain very rare cases relating to the internal `BN_hex2bn()` and `BN_dec2bn()` functions. It is believed that Node.js is not invoking the code paths that use these functions so practical attacks via Node.js using this defect are _unlikely_ to be possible. More info is available at <https://www.openssl.org/news/vulnerabilities.html#2016-0797>
  * Fix a defect that makes the CacheBleed Attack (<https://ssrg.nicta.com.au/projects/TS/cachebleed/>) possible. This defect enables attackers to execute side-channel attacks leading to the potential recovery of entire RSA private keys. It only affects the Intel Sandy Bridge (and possibly older) microarchitecture when using hyper-threading. Newer microarchitectures, including Haswell, are unaffected. More info is available at <https://www.openssl.org/news/vulnerabilities.html#2016-0702>
  * Remove SSLv2 support, the `--enable-ssl2` command line argument will now produce an error. The DROWN Attack (<https://drownattack.com/>) creates a vulnerability where SSLv2 is enabled by a server, even if a client connection is not using SSLv2. The SSLv2 protocol is widely considered unacceptably broken and should not be supported. More information is available at <https://www.openssl.org/news/vulnerabilities.html#2016-0800>

### Commits:

* \[[`164157abbb`](https://github.com/nodejs/node/commit/164157abbb)] - build: update Node.js logo on OSX installer (Rod Vagg) <https://github.com/nodejs/node/pull/5401>
* \[[`f8cb0dcf67`](https://github.com/nodejs/node/commit/f8cb0dcf67)] - crypto,tls: remove SSLv2 support (Ben Noordhuis) <https://github.com/nodejs/node/pull/5529>
* \[[`42ded2a590`](https://github.com/nodejs/node/commit/42ded2a590)] - deps: upgrade openssl to 1.0.1s (Ben Noordhuis) <https://github.com/nodejs/node/pull/5508>
* \[[`1e45a6111c`](https://github.com/nodejs/node/commit/1e45a6111c)] - deps: update http-parser to version 1.2 (James M Snell) <https://github.com/nodejs/node/pull/5242>
* \[[`6db377b2f4`](https://github.com/nodejs/node/commit/6db377b2f4)] - doc: remove SSLv2 descriptions (Shigeki Ohtsu) <https://github.com/nodejs/node/pull/5541>
* \[[`563c359f5c`](https://github.com/nodejs/node/commit/563c359f5c)] - domains: fix handling of uncaught exceptions (Julien Gilli) <https://github.com/nodejs/node/pull/3887>
* \[[`e483f3fd26`](https://github.com/nodejs/node/commit/e483f3fd26)] - test: fix hanging http obstext test (Ben Noordhuis) <https://github.com/nodejs/node/pull/5511>

<a id="0.10.42"></a>

## 2016-02-09, Version 0.10.42 (Maintenance), @jasnell

This is an important security release. All Node.js users should consult the security release summary at nodejs.org for details on patched vulnerabilities.

### Notable changes

* http: fix defects in HTTP header parsing for requests and responses that can allow request smuggling (CVE-2016-2086) or response splitting (CVE-2016-2216). HTTP header parsing now aligns more closely with the HTTP spec including restricting the acceptable characters.
* http-parser: upgrade from 1.0 to 1.1
* openssl: upgrade from 1.0.1q to 1.0.1r. To mitigate against the Logjam attack, TLS clients now reject Diffie-Hellman handshakes with parameters shorter than 1024-bits, up from the previous limit of 768-bits.
* src:
  * introduce new `--security-revert={cvenum}` command line flag for selective reversion of specific CVE fixes
  * allow the fix for CVE-2016-2216 to be selectively reverted using `--security-revert=CVE-2016-2216`
* build:
  * xz compressed tar files will be made available from nodejs.org for v0.10 builds from v0.10.42 onward
  * A headers.tar.gz file will be made available from nodejs.org for v0.10 builds from v0.10.42 onward, a future change to node-gyp will be required to make use of these

### Commits

* \[[`fdc332183e`](https://github.com/nodejs/node/commit/fdc332183e)] - build: enable xz compressed tarballs where possible (Rod Vagg) <https://github.com/nodejs/node/pull/4894>
* \[[`2d35b421b5`](https://github.com/nodejs/node/commit/2d35b421b5)] - deps: upgrade openssl sources to 1.0.1r (Shigeki Ohtsu) <https://github.com/joyent/node/pull/25368>
* \[[`b31c0f3ea4`](https://github.com/nodejs/node/commit/b31c0f3ea4)] - deps: update http-parser to version 1.1 (James M Snell)
* \[[`616ec1d6b0`](https://github.com/nodejs/node/commit/616ec1d6b0)] - doc: clarify v0.10.41 openssl tls security impact (Rod Vagg) <https://github.com/nodejs/node/pull/4153>
* \[[`ccb3c2377c`](https://github.com/nodejs/node/commit/ccb3c2377c)] - http: strictly forbid invalid characters from headers (James M Snell)
* \[[`f0af0d1f96`](https://github.com/nodejs/node/commit/f0af0d1f96)] - src: avoid compiler warning in node\_revert.cc (James M Snell)
* \[[`df80e856c6`](https://github.com/nodejs/node/commit/df80e856c6)] - src: add --security-revert command line flag (James M Snell)
* \[[`ff58dcdd74`](https://github.com/nodejs/node/commit/ff58dcdd74)] - tools: backport tools/install.py for headers (Richard Lau) <https://github.com/nodejs/node/pull/4149>

<a id="0.10.41"></a>

## 2015-12-04, Version 0.10.41 (Maintenance), @rvagg

Security Update

### Notable changes

* build: Add support for Microsoft Visual Studio 2015
* npm: Upgrade to v1.4.29 from v1.4.28. A special one-off release as part of the strategy to get a version of npm into Node.js v0.10.x that works with the current registry (<https://github.com/nodejs/LTS/issues/37>). This version of npm prints out a banner each time it is run. The banner warns that the next standard release of Node.js v0.10.x will ship with a version of npm v2.
* openssl: Upgrade to 1.0.1q, containing fixes CVE-2015-3194 "Certificate verify crash with missing PSS parameter", a potential denial-of-service vector for Node.js TLS servers using client certificate authentication; TLS clients are also impacted. Details are available at <http://openssl.org/news/secadv/20151203.txt>. (Ben Noordhuis) <https://github.com/nodejs/node/pull/4133>

### Commits

* \[[`16ca0779f5`](https://github.com/nodejs/node/commit/16ca0779f5)] - src/node.cc: fix build error without OpenSSL support (Jörg Krause) <https://github.com/nodejs/node-v0.x-archive/pull/25862>
* \[[`c559c7911d`](https://github.com/nodejs/node/commit/c559c7911d)] - build: backport tools/release.sh (Rod Vagg) <https://github.com/nodejs/node/pull/3965>
* \[[`268d2b4637`](https://github.com/nodejs/node/commit/268d2b4637)] - build: backport config for new CI infrastructure (Rod Vagg) <https://github.com/nodejs/node/pull/3965>
* \[[`c88a0b26da`](https://github.com/nodejs/node/commit/c88a0b26da)] - build: update manifest to include Windows 10 (Lucien Greathouse) <https://github.com/nodejs/node/pull/2838>
* \[[`8564a9f5f7`](https://github.com/nodejs/node/commit/8564a9f5f7)] - build: gcc version detection on openSUSE Tumbleweed (Henrique Aparecido Lavezzo) <https://github.com/nodejs/node-v0.x-archive/pull/25671>
* \[[`9c7bd6de56`](https://github.com/nodejs/node/commit/9c7bd6de56)] - build: run-ci makefile rule (Alexis Campailla) <https://github.com/nodejs/node-v0.x-archive/pull/25686>
* \[[`ffa1e1f31d`](https://github.com/nodejs/node/commit/ffa1e1f31d)] - build: support flaky tests in test-ci (Alexis Campailla) <https://github.com/nodejs/node-v0.x-archive/pull/25686>
* \[[`100dd19e61`](https://github.com/nodejs/node/commit/100dd19e61)] - build: support Jenkins via test-ci (Alexis Campailla) <https://github.com/nodejs/node-v0.x-archive/pull/25686>
* \[[`ec861f6f90`](https://github.com/nodejs/node/commit/ec861f6f90)] - build: make release process easier for multi users (Julien Gilli) <https://github.com/nodejs/node-v0.x-archive/pull/25638>
* \[[`d7ae79a452`](https://github.com/nodejs/node/commit/d7ae79a452)] - build,win: fix node.exe resource version (João Reis) <https://github.com/nodejs/node/pull/3053>
* \[[`6ac47aa9f5`](https://github.com/nodejs/node/commit/6ac47aa9f5)] - build,win: try next MSVS version on failure (João Reis) <https://github.com/nodejs/node/pull/2910>
* \[[`e669b27740`](https://github.com/nodejs/node/commit/e669b27740)] - crypto: replace rwlocks with simple mutexes (Ben Noordhuis) <https://github.com/nodejs/node/pull/2723>
* \[[`ce0a48826e`](https://github.com/nodejs/node/commit/ce0a48826e)] - deps: upgrade to openssl 1.0.1q (Ben Noordhuis) <https://github.com/nodejs/node/pull/4132>
* \[[`b68781e500`](https://github.com/nodejs/node/commit/b68781e500)] - deps: upgrade npm to 1.4.29 (Forrest L Norvell) <https://github.com/nodejs/node/pull/3639>
* \[[`7cf0d9c1d9`](https://github.com/nodejs/node/commit/7cf0d9c1d9)] - deps: fix openssl for MSVS 2015 (Andy Polyakov) <https://github.com/nodejs/node-v0.x-archive/pull/25857>
* \[[`9ee8a14f9e`](https://github.com/nodejs/node/commit/9ee8a14f9e)] - deps: fix gyp to work on MacOSX without XCode (Shigeki Ohtsu) <https://github.com/nodejs/node-v0.x-archive/pull/25857>
* \[[`a525c7244e`](https://github.com/nodejs/node/commit/a525c7244e)] - deps: update gyp to 25ed9ac (João Reis) <https://github.com/nodejs/node-v0.x-archive/pull/25857>
* \[[`6502160294`](https://github.com/nodejs/node/commit/6502160294)] - dns: allow v8 to optimize lookup() (Brian White) <https://github.com/nodejs/node-v0.x-archive/pull/8942>
* \[[`5d829a63ab`](https://github.com/nodejs/node/commit/5d829a63ab)] - doc: backport README.md (Rod Vagg) <https://github.com/nodejs/node/pull/3965>
* \[[`62c8948109`](https://github.com/nodejs/node/commit/62c8948109)] - doc: fix Folders as Modules omission of index.json (Elan Shanker) <https://github.com/nodejs/node-v0.x-archive/pull/8868>
* \[[`572663f303`](https://github.com/nodejs/node/commit/572663f303)] - https: don't overwrite servername option (skenqbx) <https://github.com/nodejs/node-v0.x-archive/pull/9368>
* \[[`75c84b2439`](https://github.com/nodejs/node/commit/75c84b2439)] - test: add test for https agent servername option (skenqbx) <https://github.com/nodejs/node-v0.x-archive/pull/9368>
* \[[`841a6dd264`](https://github.com/nodejs/node/commit/841a6dd264)] - test: mark more tests as flaky (Alexis Campailla) <https://github.com/nodejs/node-v0.x-archive/pull/25807>
* \[[`a7fee30da1`](https://github.com/nodejs/node/commit/a7fee30da1)] - test: mark test-tls-securepair-server as flaky (Alexis Campailla) <https://github.com/nodejs/node-v0.x-archive/pull/25807>
* \[[`7df57703dd`](https://github.com/nodejs/node/commit/7df57703dd)] - test: mark test-net-error-twice flaky on SmartOS (Julien Gilli) <https://github.com/nodejs/node-v0.x-archive/pull/25760>
* \[[`e10892cccc`](https://github.com/nodejs/node/commit/e10892cccc)] - test: make test-abort-fatal-error non flaky (Julien Gilli) <https://github.com/nodejs/node-v0.x-archive/pull/25755>
* \[[`a2f879f197`](https://github.com/nodejs/node/commit/a2f879f197)] - test: mark recently failing tests as flaky (Alexis Campailla) <https://github.com/nodejs/node-v0.x-archive/pull/25686>
* \[[`e7010bdf92`](https://github.com/nodejs/node/commit/e7010bdf92)] - test: runner should return 0 on flaky tests (Alexis Campailla) <https://github.com/nodejs/node-v0.x-archive/pull/25686>
* \[[`c283c9bbb3`](https://github.com/nodejs/node/commit/c283c9bbb3)] - test: support writing test output to file (Alexis Campailla) <https://github.com/nodejs/node-v0.x-archive/pull/25686>
* \[[`eeaed586bb`](https://github.com/nodejs/node/commit/eeaed586bb)] - test: runner support for flaky tests (Alexis Campailla) <https://github.com/nodejs/node-v0.x-archive/pull/25686>
* \[[`3bb8174b94`](https://github.com/nodejs/node/commit/3bb8174b94)] - test: refactor to use common testcfg (Timothy J Fontaine) <https://github.com/nodejs/node-v0.x-archive/pull/25686>
* \[[`df59d43586`](https://github.com/nodejs/node/commit/df59d43586)] - tools: pass constant to logger instead of string (Johan Bergström) <https://github.com/nodejs/node-v0.x-archive/pull/25686>
* \[[`d103d4ed9a`](https://github.com/nodejs/node/commit/d103d4ed9a)] - tools: fix test.py after v8 upgrade (Ben Noordhuis) <https://github.com/nodejs/node-v0.x-archive/pull/25686>
* \[[`8002192b4e`](https://github.com/nodejs/node/commit/8002192b4e)] - win: manifest node.exe for Windows 8.1 (Alexis Campailla) <https://github.com/nodejs/node/pull/2838>
* \[[`66ec1dae8f`](https://github.com/nodejs/node/commit/66ec1dae8f)] - win: add MSVS 2015 support (Rod Vagg) <https://github.com/nodejs/node-v0.x-archive/pull/25857>
* \[[`e192f61514`](https://github.com/nodejs/node/commit/e192f61514)] - win: fix custom actions for WiX older than 3.9 (João Reis) <https://github.com/nodejs/node-v0.x-archive/pull/25569>
* \[[`16bcd68dc5`](https://github.com/nodejs/node/commit/16bcd68dc5)] - win: fix custom actions on Visual Studio != 2013 (Julien Gilli) <https://github.com/nodejs/node-v0.x-archive/pull/25569>
* \[[`517986c2f4`](https://github.com/nodejs/node/commit/517986c2f4)] - win: backport bringing back xp/2k3 support (Bert Belder) <https://github.com/nodejs/node-v0.x-archive/pull/25569>
* \[[`10f251e8dd`](https://github.com/nodejs/node/commit/10f251e8dd)] - win: backport set env before generating projects (Alexis Campailla) <https://github.com/nodejs/node-v0.x-archive/pull/25569>

<a id="0.10.40"></a>

## 2015-07-09, Version 0.10.40 (Maintenance)

### Commits

* \[[`0cf9f27703`](https://github.com/nodejs/node/commit/0cf9f27703)] - **openssl**: upgrade to 1.0.1p [#25654](https://github.com/joyent/node/pull/25654)
* \[[`5a60e0d904`](https://github.com/nodejs/node/commit/5a60e0d904)] - **V8**: back-port JitCodeEvent patch from upstream (Ben Noordhuis) [#25588](https://github.com/joyent/node/pull/25588)
* \[[`18d413d299`](https://github.com/nodejs/node/commit/18d413d299)] - **win,msi**: create npm folder in AppData directory (Steven Rockarts) [#8838](https://github.com/joyent/node/pull/8838)

<a id="0.10.39"></a>

## 2015-06-18, Version 0.10.39 (Maintenance)

### Commits

* \[[`456c22f63f`](https://github.com/nodejs/node/commit/456c22f63f)] - **openssl**: upgrade to 1.0.1o (Addressing multiple CVEs) [#25523](https://github.com/joyent/node/pull/25523)
* \[[`9d19dfbfdb`](https://github.com/nodejs/node/commit/9d19dfbfdb)] - **install**: fix source path for openssl headers (Oguz Bastemur) [#14089](https://github.com/joyent/node/pull/14089)
* \[[`4028669531`](https://github.com/nodejs/node/commit/4028669531)] - **install**: make sure opensslconf.h is overwritten (Oguz Bastemur) [#14089](https://github.com/joyent/node/pull/14089)
* \[[`d38e865fce`](https://github.com/nodejs/node/commit/d38e865fce)] - **timers**: fix timeout when added in timer's callback (Julien Gilli) [#17203](https://github.com/joyent/node/pull/17203)
* \[[`e7c84f82c7`](https://github.com/nodejs/node/commit/e7c84f82c7)] - **windows**: broadcast WM\_SETTINGCHANGE after install (Mathias Küsel) [#25100](https://github.com/joyent/node/pull/25100)

<a id="0.10.38"></a>

## 2015-03-23, Version 0.10.38 (Maintenance)

### Commits

* \[[`3b511a8ccd`](https://github.com/nodejs/node/commit/3b511a8ccd)] - **openssl**: upgrade to 1.0.1m (Addressing multiple CVES)

<a id="0.10.37"></a>

## 2015-03-11, Version 0.10.37 (Maintenance)

### Commits

* \[[`dcff5d565c`](https://github.com/nodejs/node/commit/dcff5d565c)] - uv: update to 0.10.36 (CVE-2015-0278) [#9274](https://github.com/joyent/node/pull/9274)
* \[[`f2a45caf2e`](https://github.com/nodejs/node/commit/f2a45caf2e)] - domains: fix stack clearing after error handled (Jonas Dohse) [#9364](https://github.com/joyent/node/pull/9364)
* \[[`d01a900078`](https://github.com/nodejs/node/commit/d01a900078)] - buffer: reword Buffer.concat error message (Chris Dickinson) [#8723](https://github.com/joyent/node/pull/8723)
* \[[`c8239c08d7`](https://github.com/nodejs/node/commit/c8239c08d7)] - console: allow Object.prototype fields as labels (Julien Gilli) [#9215](https://github.com/joyent/node/pull/9215)
* \[[`431eb172f9`](https://github.com/nodejs/node/commit/431eb172f9)] - V8: log version in profiler log file (Ben Noordhuis) [#9043](https://github.com/joyent/node/pull/9043)
* \[[`8bcd0a4c4a`](https://github.com/nodejs/node/commit/8bcd0a4c4a)] - http: fix performance regression for GET requests (Florin-Cristian Gavrila) [#9026](https://github.com/joyent/node/pull/9026)

<a id="0.10.36"></a>

## 2015-01-26, Version 0.10.36 (Stable)

### Commits

* \[[`deef605085`](https://github.com/nodejs/node/commit/deef605085)] - **openssl**: update to 1.0.1l
* \[[`45f1330425`](https://github.com/nodejs/node/commit/45f1330425)] - **v8**: Fix debugger and strict mode regression (Julien Gilli)
* \[[`6ebd85e105`](https://github.com/nodejs/node/commit/6ebd85e105)] - **v8**: don't busy loop in cpu profiler thread (Ben Noordhuis) [#8789](https://github.com/joyent/node/pull/8789)

<a id="0.10.35"></a>

## 2014.12.22, Version 0.10.35 (Stable)

* tls: re-add 1024-bit SSL certs removed by f9456a2 (Chris Dickinson)
* timers: don't close interval timers when unrefd (Julien Gilli)
* timers: don't mutate unref list while iterating it (Julien Gilli)

<a id="0.10.34"></a>

## 2014.12.17, Version 0.10.34 (Stable)

<https://github.com/nodejs/node/commit/52795f8fcc2de77cf997e671ea58614e5e425dfe>

* uv: update to v0.10.30
* zlib: upgrade to v1.2.8
* child\_process: check execFile args is an array (Sam Roberts)
* child\_process: check fork args is an array (Sam Roberts)
* crypto: update root certificates (Ben Noordhuis)
* domains: fix issues with abort on uncaught (Julien Gilli)
* timers: Avoid linear scan in \_unrefActive. (Julien Gilli)
* timers: fix unref() memory leak (Trevor Norris)
* v8: add api for aborting on uncaught exception (Julien Gilli)
* debugger: fix when using "use strict" (Julien Gilli)

<a id="0.10.33"></a>

## 2014.10.20, Version 0.10.33 (Stable)

<https://github.com/nodejs/node/commit/8d045a30e95602b443eb259a5021d33feb4df079>

* openssl: Update to 1.0.1j (Addressing multiple CVEs)
* uv: Update to v0.10.29
* child\_process: properly support optional args (cjihrig)
* crypto: Disable autonegotiation for SSLv2/3 by default (Fedor Indutny,
  Timothy J Fontaine, Alexis Campailla)

  This is a behavior change, by default we will not allow the negotiation to
  SSLv2 or SSLv3. If you want this behavior, run Node.js with either
  `--enable-ssl2` or `--enable-ssl3` respectively.

  This does not change the behavior for users specifically requesting
  `SSLv2_method` or `SSLv3_method`. While this behavior is not advised, it is
  assumed you know what you're doing since you're specifically asking to use
  these methods.

<a id="0.10.32"></a>

## 2014.09.16, Version 0.10.32 (Stable)

<https://github.com/nodejs/node/commit/0fe0d121551593c23a565db8397f85f17bb0f00e>

* npm: Update to 1.4.28
* v8: fix a crash introduced by previous release (Fedor Indutny)
* configure: add --openssl-no-asm flag (Fedor Indutny)
* crypto: use domains for any callback-taking method (Chris Dickinson)
* http: do not send `0\r\n\r\n` in TE HEAD responses (Fedor Indutny)
* querystring: fix unescape override (Tristan Berger)
* url: Add support for RFC 3490 separators (Mathias Bynens)

<a id="0.10.31"></a>

## 2014.08.19, Version 0.10.31 (Stable)

<https://github.com/nodejs/node/commit/7fabdc23d843cb705d2d0739e7bbdaaf50aa3292>

* v8: backport CVE-2013-6668
* openssl: Update to v1.0.1i
* npm: Update to v1.4.23
* cluster: disconnect should not be synchronous (Sam Roberts)
* fs: fix fs.readFileSync fd leak when get RangeError (Jackson Tian)
* stream: fix Readable.wrap objectMode falsy values (James Halliday)
* timers: fix timers with non-integer delay hanging. (Julien Gilli)

<a id="0.10.30"></a>

## 2014.07.31, Version 0.10.30 (Stable)

<https://github.com/nodejs/node/commit/bc0ff830aff1e016163d855e86ded5c98b0899e8>

* uv: Upgrade to v0.10.28
* npm: Upgrade to v1.4.21
* v8: Interrupts must not mask stack overflow.
* Revert "stream: start old-mode read in a next tick" (Fedor Indutny)
* buffer: fix sign overflow in `readUIn32BE` (Fedor Indutny)
* buffer: improve {read,write}{U}Int\* methods (Nick Apperson)
* child\_process: handle writeUtf8String error (Fedor Indutny)
* deps: backport 4ed5fde4f from v8 upstream (Fedor Indutny)
* deps: cherry-pick eca441b2 from OpenSSL (Fedor Indutny)
* lib: remove and restructure calls to isNaN() (cjihrig)
* module: eliminate double `getenv()` (Maciej Małecki)
* stream2: flush extant data on read of ended stream (Chris Dickinson)
* streams: remove unused require('assert') (Rod Vagg)
* timers: backport f8193ab (Julien Gilli)
* util.h: interface compatibility (Oguz Bastemur)
* zlib: do not crash on write after close (Fedor Indutny)

<a id="0.10.29"></a>

## 2014.06.05, Version 0.10.29 (Stable)

<https://github.com/nodejs/node/commit/ce82d6b8474bde7ac7df6d425fb88fb1bcba35bc>

* openssl: to 1.0.1h (CVE-2014-0224)

* npm: upgrade to 1.4.14

* utf8: Prevent Node from sending invalid UTF-8 (Felix Geisendörfer)
  * _NOTE_ this introduces a breaking change, previously you could construct
    invalid UTF-8 and invoke an error in a client that was expecting valid
    UTF-8, now unmatched surrogate pairs are replaced with the unknown UTF-8
    character. To restore the old functionality simply have NODE\_INVALID\_UTF8
    environment variable set.

* child\_process: do not set args before throwing (Greg Sabia Tucker)

* child\_process: spawn() does not throw TypeError (Greg Sabia Tucker)

* constants: export O\_NONBLOCK (Fedor Indutny)

* crypto: improve memory usage (Alexis Campailla)

* fs: close file if fstat() fails in readFile() (cjihrig)

* lib: name EventEmitter prototype methods (Ben Noordhuis)

* tls: fix performance issue (Alexis Campailla)

<a id="0.10.28"></a>

## 2014.05.01, Version 0.10.28 (Stable)

<https://github.com/nodejs/node/commit/b148cbe09d4657766fdb61575ba985734c2ff0a8>

* npm: upgrade to v1.4.9

<a id="0.10.27"></a>

## 2014.05.01, Version 0.10.27 (Stable)

<https://github.com/nodejs/node/commit/cb7911f78ae96ef7a540df992cc1359ba9636e86>

* npm: upgrade to v1.4.8
* openssl: upgrade to 1.0.1g
* uv: update to v0.10.27
* dns: fix certain txt entries (Fedor Indutny)
* assert: Ensure reflexivity of deepEqual (Mike Pennisi)
* child\_process: fix deadlock when sending handles (Fedor Indutny)
* child\_process: fix sending handle twice (Fedor Indutny)
* crypto: do not lowercase cipher/hash names (Fedor Indutny)
* dtrace: workaround linker bug on FreeBSD (Fedor Indutny)
* http: do not emit EOF non-readable socket (Fedor Indutny)
* http: invoke createConnection when no agent (Nathan Rajlich)
* stream: remove useless check (Brian White)
* timer: don't reschedule timer bucket in a domain (Greg Brail)
* url: treat \ the same as / (isaacs)
* util: format as Error if instanceof Error (Rod Vagg)

<a id="0.10.26"></a>

## 2014.02.18, Version 0.10.26 (Stable)

<https://github.com/nodejs/node/commit/cc56c62ed879ad4f93b1fdab3235c43e60f48b7e>

* uv: Upgrade to v0.10.25 (Timothy J Fontaine)
* npm: upgrade to 1.4.3 (isaacs)
* v8: support compiling with VS2013 (Fedor Indutny)
* cares: backport TXT parsing fix (Fedor Indutny)
* crypto: throw on SignFinal failure (Fedor Indutny)
* crypto: update root certificates (Ben Noordhuis)
* debugger: Fix breakpoint not showing after restart (Farid Neshat)
* fs: make unwatchFile() insensitive to path (iamdoron)
* net: do not re-emit stream errors (Fedor Indutny)
* net: make Socket destroy() re-entrance safe (Jun Ma)
* net: reset `endEmitted` on reconnect (Fedor Indutny)
* node: do not close stdio implicitly (Fedor Indutny)
* zlib: avoid assertion in close (Fedor Indutny)

<a id="0.10.25"></a>

## 2014.01.23, Version 0.10.25 (Stable)

<https://github.com/nodejs/node/commit/b0e5f195dfce3e2b99f5091373d49f6616682596>

* uv: Upgrade to v0.10.23
* npm: Upgrade to v1.3.24
* v8: Fix enumeration for objects with lots of properties
* child\_process: fix spawn() optional arguments (Sam Roberts)
* cluster: report more errors to workers (Fedor Indutny)
* domains: exit() only affects active domains (Ryan Graham)
* src: OnFatalError handler must abort() (Timothy J Fontaine)
* stream: writes may return false but forget to emit drain (Yang Tianyang)

<a id="0.10.24"></a>

## 2013.12.18, Version 0.10.24 (Stable)

<https://github.com/nodejs/node/commit/b7fd6bc899ccb629d790c47aee06aba87e535c41>

* uv: Upgrade to v0.10.21
* npm: upgrade to 1.3.21
* v8: backport fix for CVE-2013-{6639|6640}
* build: unix install node and dep library headers (Timothy J Fontaine)
* cluster, v8: fix --logfile=%p.log (Ben Noordhuis)
* module: only cache package main (Wyatt Preul)

<a id="0.10.23"></a>

## 2013.12.12, Version 0.10.23 (Stable)

<https://github.com/nodejs/node/commit/0462bc23564e7e950a70ae4577a840b04db6c7c6>

* uv: Upgrade to v0.10.20 (Timothy J Fontaine)
* npm: Upgrade to 1.3.17 (isaacs)
* gyp: update to 78b26f7 (Timothy J Fontaine)
* build: include postmortem symbols on linux (Timothy J Fontaine)
* crypto: Make Decipher.\_flush() emit errors. (Kai Groner)
* dgram: fix abort when getting `fd` of closed dgram (Fedor Indutny)
* events: do not accept NaN in setMaxListeners (Fedor Indutny)
* events: avoid calling `once` functions twice (Tim Wood)
* events: fix TypeError in removeAllListeners (Jeremy Martin)
* fs: report correct path when EEXIST (Fedor Indutny)
* process: enforce allowed signals for kill (Sam Roberts)
* tls: emit 'end' on .receivedShutdown (Fedor Indutny)
* tls: fix potential data corruption (Fedor Indutny)
* tls: handle `ssl.start()` errors appropriately (Fedor Indutny)
* tls: reset NPN callbacks after SNI (Fedor Indutny)

<a id="0.10.22"></a>

## 2013.11.12, Version 0.10.22 (Stable)

<https://github.com/nodejs/node/commit/cbff8f091c22fb1df6b238c7a1b9145db950fa65>

* npm: Upgrade to 1.3.14
* uv: Upgrade to v0.10.19
* child\_process: don't assert on stale file descriptor events (Fedor Indutny)
* darwin: Fix "Not Responding" in Mavericks activity monitor (Fedor Indutny)
* debugger: Fix bug in sb() with unnamed script (Maxim Bogushevich)
* repl: do not insert duplicates into completions (Maciej Małecki)
* src: Fix memory leak on closed handles (Timothy J Fontaine)
* tls: prevent stalls by using read(0) (Fedor Indutny)
* v8: use correct timezone information on Solaris (Maciej Małecki)

<a id="0.10.21"></a>

## 2013.10.18, Version 0.10.21 (Stable)

<https://github.com/nodejs/node/commit/e2da042844a830fafb8031f6c477eb4f96195210>

* uv: Upgrade to v0.10.18
* crypto: clear errors from verify failure (Timothy J Fontaine)
* dtrace: interpret two byte strings (Dave Pacheco)
* fs: fix fs.truncate() file content zeroing bug (Ben Noordhuis)
* http: provide backpressure for pipeline flood (isaacs)
* tls: fix premature connection termination (Ben Noordhuis)

<a id="0.10.20"></a>

## 2013.09.30, Version 0.10.20 (Stable)

<https://github.com/nodejs/node/commit/d7234c8d50a1af73f60d2d3c0cc7eed17429a481>

* tls: fix sporadic hang and partial reads (Fedor Indutny)
  * fixes "npm ERR! cb() never called!"

<a id="0.10.19"></a>

## 2013.09.24, Version 0.10.19 (Stable)

<https://github.com/nodejs/node/commit/6b5e6a5a3ec8d994c9aab3b800b9edbf1b287904>

* uv: Upgrade to v0.10.17
* npm: upgrade to 1.3.11
* readline: handle input starting with control chars (Eric Schrock)
* configure: add mips-float-abi (soft, hard) option (Andrei Sedoi)
* stream: objectMode transforms allow falsey values (isaacs)
* tls: prevent duplicate values returned from read (Nathan Rajlich)
* tls: NPN protocols are now local to connections (Fedor Indutny)

<a id="0.10.18"></a>

## 2013.09.04, Version 0.10.18 (Stable)

<https://github.com/nodejs/node/commit/67a1f0c52e0708e2596f3f2134b8386d6112561e>

* uv: Upgrade to v0.10.15
* stream: Don't crash on unset \_events property (isaacs)
* stream: Pass 'buffer' encoding with decoded writable chunks (isaacs)

<a id="0.10.17"></a>

## 2013.08.21, Version 0.10.17 (Stable)

<https://github.com/nodejs/node/commit/469a4a5091a677df62be319675056b869c31b35c>

* uv: Upgrade v0.10.14
* http\_parser: Do not accept PUN/GEM methods as PUT/GET (Chris Dickinson)
* tls: fix assertion when ssl is destroyed at read (Fedor Indutny)
* stream: Throw on 'error' if listeners removed (isaacs)
* dgram: fix assertion on bad send() arguments (Ben Noordhuis)
* readline: pause stdin before turning off terminal raw mode (Daniel Chatfield)

<a id="0.10.16"></a>

## 2013.08.16, Version 0.10.16 (Stable)

<https://github.com/nodejs/node/commit/50b4c905a4425430ae54db4906f88982309e128d>

* v8: back-port fix for CVE-2013-2882
* npm: Upgrade to 1.3.8
* crypto: fix assert() on malformed hex input (Ben Noordhuis)
* crypto: fix memory leak in randomBytes() error path (Ben Noordhuis)
* events: fix memory leak, don't leak event names (Ben Noordhuis)
* http: Handle hex/base64 encodings properly (isaacs)
* http: improve chunked res.write(buf) performance (Ben Noordhuis)
* stream: Fix double pipe error emit (Eran Hammer)

<a id="0.10.15"></a>

## 2013.07.25, Version 0.10.15 (Stable)

<https://github.com/nodejs/node/commit/2426d65af860bda7be9f0832a99601cc43c6cf63>

* src: fix process.getuid() return value (Ben Noordhuis)

<a id="0.10.14"></a>

## 2013.07.25, Version 0.10.14 (Stable)

<https://github.com/nodejs/node/commit/fdf57f811f9683a4ec49a74dc7226517e32e6c9d>

* uv: Upgrade to v0.10.13
* npm: Upgrade to v1.3.5
* os: Don't report negative times in cpu info (Ben Noordhuis)
* fs: Handle large UID and GID (Ben Noordhuis)
* url: Fix edge-case when protocol is non-lowercase (Shuan Wang)
* doc: Streams API Doc Rewrite (isaacs)
* node: call MakeDomainCallback in all domain cases (Trevor Norris)
* crypto: fix memory leak in LoadPKCS12 (Fedor Indutny)

<a id="0.10.13"></a>

## 2013.07.09, Version 0.10.13 (Stable)

<https://github.com/nodejs/node/commit/e32660a984427d46af6a144983cf7b8045b7299c>

* uv: Upgrade to v0.10.12
* npm: Upgrade to 1.3.2
* windows: get proper errno (Ben Noordhuis)
* tls: only wait for finish if we haven't seen it (Timothy J Fontaine)
* http: Dump response when request is aborted (isaacs)
* http: use an unref'd timer to fix delay in exit (Peter Rust)
* zlib: level can be negative (Brian White)
* zlib: allow zero values for level and strategy (Brian White)
* buffer: add comment explaining buffer alignment (Ben Noordhuis)
* string\_bytes: properly detect 64bit (Timothy J Fontaine)
* src: fix memory leak in UsingDomains() (Ben Noordhuis)

<a id="0.10.12"></a>

## 2013.06.18, Version 0.10.12 (Stable)

<https://github.com/nodejs/node/commit/a088cf4f930d3928c97d239adf950ab43e7794aa>

* npm: Upgrade to 1.2.32
* readline: make `ctrl + L` clear the screen (Yuan Chuan)
* v8: add setVariableValue debugger command (Ben Noordhuis)
* net: Do not destroy socket mid-write (isaacs)
* v8: fix build for mips32r2 architecture (Andrei Sedoi)
* configure: fix cross-compilation host\_arch\_cc() (Andrei Sedoi)

<a id="0.10.11"></a>

## 2013.06.13, Version 0.10.11 (Stable)

<https://github.com/nodejs/node/commit/d9d5bc465450ae5d60da32e9ffcf71c2767f1fad>

* uv: upgrade to 0.10.11
* npm: Upgrade to 1.2.30
* openssl: add missing configuration pieces for MIPS (Andrei Sedoi)
* Revert "http: remove bodyHead from 'upgrade' events" (isaacs)
* v8: fix pointer arithmetic undefined behavior (Trevor Norris)
* crypto: fix utf8/utf-8 encoding check (Ben Noordhuis)
* net: Fix busy loop on POLLERR|POLLHUP on older linux kernels (Ben Noordhuis, isaacs)

<a id="0.10.10"></a>

## 2013.06.04, Version 0.10.10 (Stable)

<https://github.com/nodejs/node/commit/25e51c396aa23018603baae2b1d9390f5d9db496>

* uv: Upgrade to 0.10.10
* npm: Upgrade to 1.2.25
* url: Properly parse certain oddly formed urls (isaacs)
* stream: unshift('') is a noop (isaacs)

<a id="0.10.9"></a>

## 2013.05.30, Version 0.10.9 (Stable)

<https://github.com/nodejs/node/commit/878ffdbe6a8eac918ef3a7f13925681c3778060b>

* npm: Upgrade to 1.2.24
* uv: Upgrade to v0.10.9
* repl: fix JSON.parse error check (Brian White)
* tls: proper .destroySoon (Fedor Indutny)
* tls: invoke write cb only after opposite read end (Fedor Indutny)
* tls: ignore .shutdown() syscall error (Fedor Indutny)

<a id="0.10.8"></a>

## 2013.05.24, Version 0.10.8 (Stable)

<https://github.com/nodejs/node/commit/30d9e9fdd9d4c33d3d95a129d021cd8b5b91eddb>

* v8: update to 3.14.5.9
* uv: upgrade to 0.10.8
* npm: Upgrade to 1.2.23
* http: remove bodyHead from 'upgrade' events (Nathan Zadoks)
* http: Return true on empty writes, not false (isaacs)
* http: save roundtrips, convert buffers to strings (Ben Noordhuis)
* configure: respect the --dest-os flag consistently (Nathan Rajlich)
* buffer: throw when writing beyond buffer (Trevor Norris)
* crypto: Clear error after DiffieHellman key errors (isaacs)
* string\_bytes: strip padding from base64 strings (Trevor Norris)

<a id="0.10.7"></a>

## 2013.05.17, Version 0.10.7 (Stable)

<https://github.com/nodejs/node/commit/d2fdae197ac542f686ee06835d1153dd43b862e5>

* uv: upgrade to v0.10.7
* npm: Upgrade to 1.2.21
* crypto: Don't ignore verify encoding argument (isaacs)
* buffer, crypto: fix default encoding regression (Ben Noordhuis)
* timers: fix setInterval() assert (Ben Noordhuis)

<a id="0.10.6"></a>

## 2013.05.14, Version 0.10.6 (Stable)

<https://github.com/nodejs/node/commit/5deb1672f2b5794f8be19498a425ea4dc0b0711f>

* module: Deprecate require.extensions (isaacs)
* stream: make Readable.wrap support objectMode, empty streams (Daniel Moore)
* child\_process: fix handle delivery (Ben Noordhuis)
* crypto: Fix performance regression (isaacs)
* src: DRY string encoding/decoding (isaacs)

<a id="0.10.5"></a>

## 2013.04.23, Version 0.10.5 (Stable)

<https://github.com/nodejs/node/commit/deeaf8fab978e3cadb364e46fb32dafdebe5f095>

* uv: Upgrade to 0.10.5 (isaacs)
* build: added support for Visual Studio 2012 (Miroslav Bajtoš)
* http: Don't try to destroy nonexistent sockets (isaacs)
* crypto: LazyTransform on properties, not methods (isaacs)
* assert: put info in err.message, not err.name (Ryan Doenges)
* dgram: fix no address bind() (Ben Noordhuis)
* handle\_wrap: fix NULL pointer dereference (Ben Noordhuis)
* os: fix unlikely buffer overflow in os.type() (Ben Noordhuis)
* stream: Fix unshift() race conditions (isaacs)

<a id="0.10.4"></a>

## 2013.04.11, Version 0.10.4 (Stable)

<https://github.com/nodejs/node/commit/9712aa9f76073c30850b20a188b1ed12ffb74d17>

* uv: Upgrade to 0.10.4
* npm: Upgrade to 1.2.18
* v8: Avoid excessive memory growth in JSON.parse (Fedor Indutny)
* child\_process, cluster: fix O(n\*m) scan of cmd string (Ben Noordhuis)
* net: fix socket.bytesWritten Buffers support (Fedor Indutny)
* buffer: fix offset checks (Łukasz Walukiewicz)
* stream: call write cb before finish event (isaacs)
* http: Support write(data, 'hex') (isaacs)
* crypto: dh secret should be left-padded (Fedor Indutny)
* process: expose NODE\_MODULE\_VERSION in process.versions (Rod Vagg)
* crypto: fix constructor call in crypto streams (Andreas Madsen)
* net: account for encoding in .byteLength (Fedor Indutny)
* net: fix buffer iteration in bytesWritten (Fedor Indutny)
* crypto: zero is not an error if writing 0 bytes (Fedor Indutny)
* tls: Re-enable check of CN-ID in cert verification (Tobias Müllerleile)

<a id="0.10.3"></a>

## 2013.04.03, Version 0.10.3 (Stable)

<https://github.com/nodejs/node/commit/d4982f6f5e4a9a703127489a553b8d782997ea43>

* npm: Upgrade to 1.2.17
* child\_process: acknowledge sent handles (Fedor Indutny)
* etw: update prototypes to match dtrace provider (Timothy J Fontaine)
* dtrace: pass more arguments to probes (Dave Pacheco)
* build: allow building with dtrace on osx (Dave Pacheco)
* http: Remove legacy ECONNRESET workaround code (isaacs)
* http: Ensure socket cleanup on client response end (isaacs)
* tls: Destroy socket when encrypted side closes (isaacs)
* repl: isSyntaxError() catches "strict mode" errors (Nathan Rajlich)
* crypto: Pass options to ctor calls (isaacs)
* src: tie process.versions.uv to uv\_version\_string() (Ben Noordhuis)

<a id="0.10.2"></a>

## 2013.03.28, Version 0.10.2 (Stable)

<https://github.com/nodejs/node/commit/1e0de9c426e07a260bbec2d2196c2d2db8eb8886>

* npm: Upgrade to 1.2.15
* uv: Upgrade to 0.10.3
* tls: handle SSL\_ERROR\_ZERO\_RETURN (Fedor Indutny)
* tls: handle errors before calling C++ methods (Fedor Indutny)
* tls: remove harmful unnecessary bounds checking (Marcel Laverdet)
* crypto: make getCiphers() return non-SSL ciphers (Ben Noordhuis)
* crypto: check randomBytes() size argument (Ben Noordhuis)
* timers: do not calculate Timeout.\_when property (Alexey Kupershtokh)
* timers: fix off-by-one ms error (Alexey Kupershtokh)
* timers: handle signed int32 overflow in enroll() (Fedor Indutny)
* stream: Fix stall in Transform under very specific conditions (Gil Pedersen)
* stream: Handle late 'readable' event listeners (isaacs)
* stream: Fix early end in Writables on zero-length writes (isaacs)
* domain: fix domain callback from MakeCallback (Trevor Norris)
* child\_process: don't emit same handle twice (Ben Noordhuis)
* child\_process: fix sending utf-8 to child process (Ben Noordhuis)

<a id="0.10.1"></a>

## 2013.03.21, Version 0.10.1 (Stable)

<https://github.com/nodejs/node/commit/c274d1643589bf104122674a8c3fd147527a667d>

* npm: upgrade to 1.2.15
* crypto: Improve performance of non-stream APIs (Fedor Indutny)
* tls: always reset this.ssl.error after handling (Fedor Indutny)
* tls: Prevent mid-stream hangs (Fedor Indutny, isaacs)
* net: improve arbitrary tcp socket support (Ben Noordhuis)
* net: handle 'finish' event only after 'connect' (Fedor Indutny)
* http: Don't hot-path end() for large buffers (isaacs)
* fs: Missing cb errors are deprecated, not a throw (isaacs)
* fs: make write/appendFileSync correctly set file mode (Raymond Feng)
* stream: Return self from readable.wrap (isaacs)
* stream: Never call decoder.end() multiple times (Gil Pedersen)
* windows: enable watching signals with process.on('SIGXYZ') (Bert Belder)
* node: revert removal of MakeCallback (Trevor Norris)
* node: Unwrap without aborting in handle fd getter (isaacs)

<a id="0.10.0"></a>

## 2013.03.11, Version 0.10.0 (Stable)

<https://github.com/nodejs/node/commit/163ca274230fce536afe76c64676c332693ad7c1>

* npm: Upgrade to 1.2.14
* core: Append filename properly in dlopen on windows (isaacs)
* zlib: Manage flush flags appropriately (isaacs)
* domains: Handle errors thrown in nested error handlers (isaacs)
* buffer: Strip high bits when converting to ascii (Ben Noordhuis)
* win/msi: Enable modify and repair (Bert Belder)
* win/msi: Add feature selection for various node parts (Bert Belder)
* win/msi: use consistent registry key paths (Bert Belder)
* child\_process: support sending dgram socket (Andreas Madsen)
* fs: Raise EISDIR on Windows when calling fs.read/write on a dir (isaacs)
* unix: fix strict aliasing warnings, macro-ify functions (Ben Noordhuis)
* unix: honor UV\_THREADPOOL\_SIZE environment var (Ben Noordhuis)
* win/tty: fix typo in color attributes enumeration (Bert Belder)
* win/tty: don't touch insert mode or quick edit mode (Bert Belder)

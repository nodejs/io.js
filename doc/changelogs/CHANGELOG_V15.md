# Node.js 15 ChangeLog

<!--lint disable maximum-line-length no-literal-urls prohibited-strings-->

<table>
<tr>
<th>Current</th>
</tr>
<tr>
<td>
<a href="#15.14.0">15.14.0</a><br/>
<a href="#15.13.0">15.13.0</a><br/>
<a href="#15.12.0">15.12.0</a><br/>
<a href="#15.11.0">15.11.0</a><br/>
<a href="#15.10.0">15.10.0</a><br/>
<a href="#15.9.0">15.9.0</a><br/>
<a href="#15.8.0">15.8.0</a><br/>
<a href="#15.7.0">15.7.0</a><br/>
<a href="#15.6.0">15.6.0</a><br/>
<a href="#15.5.1">15.5.1</a><br/>
<a href="#15.5.0">15.5.0</a><br/>
<a href="#15.4.0">15.4.0</a><br/>
<a href="#15.3.0">15.3.0</a><br/>
<a href="#15.2.1">15.2.1</a><br/>
<a href="#15.2.0">15.2.0</a><br/>
<a href="#15.1.0">15.1.0</a><br/>
<a href="#15.0.1">15.0.1</a><br/>
<a href="#15.0.0">15.0.0</a><br/>
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
  * [0.10.x](CHANGELOG_V010.md)
  * [io.js](CHANGELOG_IOJS.md)
  * [Archive](CHANGELOG_ARCHIVE.md)

<a id="15.14.0"></a>

## 2021-04-06, Version 15.14.0 (Current), @mylesborins

This is a security release.

### Notable Changes

Vulnerabilties Fixed:

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

Other Notable Changes:

* \[[`b6f4901221`](https://github.com/nodejs/node/commit/b6f4901221)] - **(SEMVER-MINOR)** **fs**: add support for async iterators to `fsPromises.writeFile` (HiroyukiYagihashi) [#37490](https://github.com/nodejs/node/pull/37490)
* \[[`0709cbb7fe`](https://github.com/nodejs/node/commit/0709cbb7fe)] - **(SEMVER-MINOR)** **net**: allow net.BlockList to use net.SocketAddress objects (James M Snell) [#37917](https://github.com/nodejs/node/pull/37917)
* \[[`daa8a7bbcf`](https://github.com/nodejs/node/commit/daa8a7bbcf)] - **(SEMVER-MINOR)** **net**: add SocketAddress class (James M Snell) [#37917](https://github.com/nodejs/node/pull/37917)
* \[[`a4169ce519`](https://github.com/nodejs/node/commit/a4169ce519)] - **(SEMVER-MINOR)** **net**: make net.BlockList cloneable (James M Snell) [#37917](https://github.com/nodejs/node/pull/37917)
* \[[`669b81c68b`](https://github.com/nodejs/node/commit/669b81c68b)] - **(SEMVER-MINOR)** **net,tls**: add abort signal support to connect (Nitzan Uziely) [#37735](https://github.com/nodejs/node/pull/37735)
* \[[`a1123f0a29`](https://github.com/nodejs/node/commit/a1123f0a29)] - **(SEMVER-MINOR)** **readline**: add AbortSignal support to interface (Nitzan Uziely) [#37932](https://github.com/nodejs/node/pull/37932)

### Commits

* \[[`ac69b95e47`](https://github.com/nodejs/node/commit/ac69b95e47)] - **crypto**: use correct webcrypto RSASSA-PKCS1-v1\_5 algorithm name (Filip Skokan) [#38029](https://github.com/nodejs/node/pull/38029)
* \[[`960c6be229`](https://github.com/nodejs/node/commit/960c6be229)] - **crypto**: add buffering to randomInt (Tobias Nießen) [#35110](https://github.com/nodejs/node/pull/35110)
* \[[`4ef102d34e`](https://github.com/nodejs/node/commit/4ef102d34e)] - **deps**: update to cjs-module-lexer\@1.1.1 (Guy Bedford) [#37992](https://github.com/nodejs/node/pull/37992)
* \[[`f0e77149a4`](https://github.com/nodejs/node/commit/f0e77149a4)] - **deps**: update archs files for OpenSSL-1.1.1k (Hassaan Pasha) [#37916](https://github.com/nodejs/node/pull/37916)
* \[[`bbdcdad2c6`](https://github.com/nodejs/node/commit/bbdcdad2c6)] - **deps**: upgrade openssl sources to 1.1.1k+quic (Hassaan Pasha) [#37916](https://github.com/nodejs/node/pull/37916)
* \[[`913ec56798`](https://github.com/nodejs/node/commit/913ec56798)] - **deps**: cjs-module-lexer: cherry-pick 22093e765f (pezhmanparsaee) [#37895](https://github.com/nodejs/node/pull/37895)
* \[[`afc6ab2122`](https://github.com/nodejs/node/commit/afc6ab2122)] - **doc**: fix asyncLocalStorage.run() description (Darkripper214) [#38023](https://github.com/nodejs/node/pull/38023)
* \[[`b40d35d649`](https://github.com/nodejs/node/commit/b40d35d649)] - **doc**: document how to unref stdin when using readline.Interface (Anu Pasumarthy) [#38019](https://github.com/nodejs/node/pull/38019)
* \[[`ce14080473`](https://github.com/nodejs/node/commit/ce14080473)] - **doc**: move psmarshall to collaborators emeriti (Peter Marshall) [#37994](https://github.com/nodejs/node/pull/37994)
* \[[`ae70aa3c63`](https://github.com/nodejs/node/commit/ae70aa3c63)] - **doc**: add distinctive color for code elements inside links (Antoine du Hamel) [#37950](https://github.com/nodejs/node/pull/37950)
* \[[`8792c7c96b`](https://github.com/nodejs/node/commit/8792c7c96b)] - **doc**: add missing events.on metadata (Anna Henningsen) [#37965](https://github.com/nodejs/node/pull/37965)
* \[[`a57dc06adf`](https://github.com/nodejs/node/commit/a57dc06adf)] - **doc**: improve Buffer's encoding documentation (Michaël Zasso) [#37945](https://github.com/nodejs/node/pull/37945)
* \[[`f3fabb57cf`](https://github.com/nodejs/node/commit/f3fabb57cf)] - **doc**: add missing cleanup step in OpenSSL upgrade (Tobias Nießen) [#37927](https://github.com/nodejs/node/pull/37927)
* \[[`13c3924af8`](https://github.com/nodejs/node/commit/13c3924af8)] - **doc**: add Windows-specific info to subprocess.kill() (João Lucas Lucchetta) [#34867](https://github.com/nodejs/node/pull/34867)
* \[[`b6f4901221`](https://github.com/nodejs/node/commit/b6f4901221)] - **(SEMVER-MINOR)** **fs**: add support for async iterators to `fsPromises.writeFile` (HiroyukiYagihashi) [#37490](https://github.com/nodejs/node/pull/37490)
* \[[`ad7e34446c`](https://github.com/nodejs/node/commit/ad7e34446c)] - **fs**: fix chown abort (Darshan Sen) [#38004](https://github.com/nodejs/node/pull/38004)
* \[[`d86aca9a77`](https://github.com/nodejs/node/commit/d86aca9a77)] - **http**: optimize debug function correctly (Michaël Zasso) [#37966](https://github.com/nodejs/node/pull/37966)
* \[[`062541aae5`](https://github.com/nodejs/node/commit/062541aae5)] - **http2**: add specific error code for custom frames (Anna Henningsen) [#37936](https://github.com/nodejs/node/pull/37936)
* \[[`8525231902`](https://github.com/nodejs/node/commit/8525231902)] - **lib**: change wording in lib/domain.js comment (Akhil Marsonya) [#37933](https://github.com/nodejs/node/pull/37933)
* \[[`21e399be4c`](https://github.com/nodejs/node/commit/21e399be4c)] - **lib**: change wording in lib/internal/child\_process comment (Akhil Marsonya) [#37903](https://github.com/nodejs/node/pull/37903)
* \[[`3ab9619e56`](https://github.com/nodejs/node/commit/3ab9619e56)] - **module**: improve error message for invalid data URL (Antoine du Hamel) [#37701](https://github.com/nodejs/node/pull/37701)
* \[[`0709cbb7fe`](https://github.com/nodejs/node/commit/0709cbb7fe)] - **(SEMVER-MINOR)** **net**: allow net.BlockList to use net.SocketAddress objects (James M Snell) [#37917](https://github.com/nodejs/node/pull/37917)
* \[[`daa8a7bbcf`](https://github.com/nodejs/node/commit/daa8a7bbcf)] - **(SEMVER-MINOR)** **net**: add SocketAddress class (James M Snell) [#37917](https://github.com/nodejs/node/pull/37917)
* \[[`a4169ce519`](https://github.com/nodejs/node/commit/a4169ce519)] - **(SEMVER-MINOR)** **net**: make net.BlockList cloneable (James M Snell) [#37917](https://github.com/nodejs/node/pull/37917)
* \[[`669b81c68b`](https://github.com/nodejs/node/commit/669b81c68b)] - **(SEMVER-MINOR)** **net,tls**: add abort signal support to connect (Nitzan Uziely) [#37735](https://github.com/nodejs/node/pull/37735)
* \[[`a94cc27cbe`](https://github.com/nodejs/node/commit/a94cc27cbe)] - **path**: refactor to use more primordials (Akhil Marsonya) [#37893](https://github.com/nodejs/node/pull/37893)
* \[[`6cc1e15669`](https://github.com/nodejs/node/commit/6cc1e15669)] - **readline**: fix pre-aborted signal question handling (Nitzan Uziely) [#37929](https://github.com/nodejs/node/pull/37929)
* \[[`a1123f0a29`](https://github.com/nodejs/node/commit/a1123f0a29)] - **(SEMVER-MINOR)** **readline**: add AbortSignal support to interface (Nitzan Uziely) [#37932](https://github.com/nodejs/node/pull/37932)
* \[[`629e72e9f4`](https://github.com/nodejs/node/commit/629e72e9f4)] - **src**: fix typo in node\_mutex (Tobias Nießen) [#38011](https://github.com/nodejs/node/pull/38011)
* \[[`e61cc0bfb0`](https://github.com/nodejs/node/commit/e61cc0bfb0)] - **src**: fix typos in crypto comments (Tobias Nießen) [#38024](https://github.com/nodejs/node/pull/38024)
* \[[`6ad0b6f0f5`](https://github.com/nodejs/node/commit/6ad0b6f0f5)] - **src**: fix error handling for CryptoJob::ToResult (Tobias Nießen) [#37076](https://github.com/nodejs/node/pull/37076)
* \[[`3175559bed`](https://github.com/nodejs/node/commit/3175559bed)] - **test**: add extra space in test failure output (Qingyu Deng) [#37957](https://github.com/nodejs/node/pull/37957)
* \[[`0243376cfc`](https://github.com/nodejs/node/commit/0243376cfc)] - **test**: use faster variant for rss (Pooja D P) [#36839](https://github.com/nodejs/node/pull/36839)
* \[[`b02c352ad6`](https://github.com/nodejs/node/commit/b02c352ad6)] - **test**: fix test-tls-no-sslv3 for OpenSSL 3 (Richard Lau) [#38027](https://github.com/nodejs/node/pull/38027)
* \[[`0db1a1eacf`](https://github.com/nodejs/node/commit/0db1a1eacf)] - **test**: deflake test-fs-read-optional-params (Luigi Pinca) [#37991](https://github.com/nodejs/node/pull/37991)
* \[[`4d50975cd7`](https://github.com/nodejs/node/commit/4d50975cd7)] - **test**: improve clarity of ALS-enable-disable.js (Darkripper214) [#38008](https://github.com/nodejs/node/pull/38008)
* \[[`5e15ae05d0`](https://github.com/nodejs/node/commit/5e15ae05d0)] - **test**: add DataView test case for v8 serdes (Rich Trott) [#37955](https://github.com/nodejs/node/pull/37955)
* \[[`6d28a24f1c`](https://github.com/nodejs/node/commit/6d28a24f1c)] - **tools**: update ESLint to 7.23.0 (Luigi Pinca) [#37979](https://github.com/nodejs/node/pull/37979)
* \[[`51e7a33d54`](https://github.com/nodejs/node/commit/51e7a33d54)] - **tools,doc**: add "legacy" badge in the TOC (Antoine du Hamel) [#37949](https://github.com/nodejs/node/pull/37949)
* \[[`570fbcef93`](https://github.com/nodejs/node/commit/570fbcef93)] - **url**: forbid pipe in URL host (Darshan Sen) [#37877](https://github.com/nodejs/node/pull/37877)

<a id="15.13.0"></a>

## 2021-03-31, Version 15.13.0 (Current), @ruyadorno

### Notable Changes

* **buffer**:
  * implement btoa and atob (James M Snell) [#37529](https://github.com/nodejs/node/pull/37529)
* **deps**:
  * upgrade npm to 7.7.6 (Ruy Adorno) [#37968](https://github.com/nodejs/node/pull/37968)
    * This update adds workspaces support to [`npm run`](https://github.com/npm/cli/pull/2864) and [`npm exec`](https://github.com/npm/cli/pull/2886)
* **doc**:
  * add legacy status to stability index (James M Snell) [#37784](https://github.com/nodejs/node/pull/37784)
  * add @linkgoron to collaborators (Nitzan Uziely) [#37817](https://github.com/nodejs/node/pull/37817)
* **http**:
  * add http.ClientRequest.getRawHeaderNames() (simov) [#37660](https://github.com/nodejs/node/pull/37660)

### Commits

* \[[`dc9cd43d8f`](https://github.com/nodejs/node/commit/dc9cd43d8f)] - **(SEMVER-MINOR)** **buffer**: implement btoa and atob (James M Snell) [#37529](https://github.com/nodejs/node/pull/37529)
* \[[`377830fd28`](https://github.com/nodejs/node/commit/377830fd28)] - **child\_process**: remove unused argument (Rich Trott) [#37923](https://github.com/nodejs/node/pull/37923)
* \[[`cdfc1c8692`](https://github.com/nodejs/node/commit/cdfc1c8692)] - **child\_process**: cleanup AbortSignal duplication (Nitzan Uziely) [#37823](https://github.com/nodejs/node/pull/37823)
* \[[`95aa032413`](https://github.com/nodejs/node/commit/95aa032413)] - **(SEMVER-MINOR)** **child\_process**: add timeout to spawn and fork (Nitzan Uziely) [#37256](https://github.com/nodejs/node/pull/37256)
* \[[`50fc6b9df0`](https://github.com/nodejs/node/commit/50fc6b9df0)] - **crypto**: clear errors in SignTraits::DeriveBits (Filip Skokan) [#37820](https://github.com/nodejs/node/pull/37820)
* \[[`79259389a1`](https://github.com/nodejs/node/commit/79259389a1)] - **crypto**: fix DiffieHellman argument validation (Antoine du Hamel) [#37810](https://github.com/nodejs/node/pull/37810)
* \[[`11d45855cd`](https://github.com/nodejs/node/commit/11d45855cd)] - **crypto**: fix header name (Jiawen Geng) [#37792](https://github.com/nodejs/node/pull/37792)
* \[[`c37806d0ba`](https://github.com/nodejs/node/commit/c37806d0ba)] - **crypto**: use macro map for NodeCryptoError (Darshan Sen) [#37758](https://github.com/nodejs/node/pull/37758)
* \[[`bfe3f21ee0`](https://github.com/nodejs/node/commit/bfe3f21ee0)] - **crypto**: fix crypto.verify callback invocation with a private keyobject (Filip Skokan) [#37795](https://github.com/nodejs/node/pull/37795)
* \[[`f09c033faf`](https://github.com/nodejs/node/commit/f09c033faf)] - **deps**: backport v8 f19142e6 (Guy Bedford) [#37864](https://github.com/nodejs/node/pull/37864)
* \[[`2fd97ce687`](https://github.com/nodejs/node/commit/2fd97ce687)] - **deps**: v8 backport 9689b17687b (Guy Bedford) [#37865](https://github.com/nodejs/node/pull/37865)
* \[[`f2cef54b6f`](https://github.com/nodejs/node/commit/f2cef54b6f)] - **deps**: upgrade npm to 7.7.6 (Ruy Adorno) [#37968](https://github.com/nodejs/node/pull/37968)
* \[[`ec82feb728`](https://github.com/nodejs/node/commit/ec82feb728)] - **deps**: upgrade npm to 7.7.5 (Ruy Adorno) [#37919](https://github.com/nodejs/node/pull/37919)
* \[[`649e04c4a5`](https://github.com/nodejs/node/commit/649e04c4a5)] - **deps**: upgrade npm to 7.7.4 (Ruy Adorno) [#37897](https://github.com/nodejs/node/pull/37897)
* \[[`d5b472b70d`](https://github.com/nodejs/node/commit/d5b472b70d)] - **deps**: upgrade npm to 7.7.0 (Ruy Adorno) [#37879](https://github.com/nodejs/node/pull/37879)
* \[[`9e6aa190e3`](https://github.com/nodejs/node/commit/9e6aa190e3)] - **deps**: add ngtcp2 and nghttp3 (James M Snell) [#37682](https://github.com/nodejs/node/pull/37682)
* \[[`659fc5d684`](https://github.com/nodejs/node/commit/659fc5d684)] - **doc**: fix typos in lib/internal/bootstrap/pre\_execution.js (marsonya) [#37658](https://github.com/nodejs/node/pull/37658)
* \[[`ac60d018e2`](https://github.com/nodejs/node/commit/ac60d018e2)] - **doc**: add more commands for cherry-picking and changelog to release docs (Danielle Adams) [#37785](https://github.com/nodejs/node/pull/37785)
* \[[`0fe3c7edd3`](https://github.com/nodejs/node/commit/0fe3c7edd3)] - **doc**: spell out ICU acronym on first occurrence (Rich Trott) [#37942](https://github.com/nodejs/node/pull/37942)
* \[[`364c8ac40d`](https://github.com/nodejs/node/commit/364c8ac40d)] - **doc**: update GOVERNANCE.md for TSC Charter changes (Rich Trott) [#37888](https://github.com/nodejs/node/pull/37888)
* \[[`e84252b35d`](https://github.com/nodejs/node/commit/e84252b35d)] - **doc**: reduce header nesting in async\_hooks.md (Rich Trott) [#37839](https://github.com/nodejs/node/pull/37839)
* \[[`a6f21e2cfc`](https://github.com/nodejs/node/commit/a6f21e2cfc)] - **doc**: fix wording in outgoingMessage.write (Tobias Nießen) [#37894](https://github.com/nodejs/node/pull/37894)
* \[[`30bc2e43e4`](https://github.com/nodejs/node/commit/30bc2e43e4)] - **doc**: add examples for WHATWG URL objects (James M Snell) [#37822](https://github.com/nodejs/node/pull/37822)
* \[[`c0a424f3e9`](https://github.com/nodejs/node/commit/c0a424f3e9)] - **doc**: clarify when child process 'spawn' event is \*not\* emitted (Matthew Francis Brunetti) [#37833](https://github.com/nodejs/node/pull/37833)
* \[[`9defe10371`](https://github.com/nodejs/node/commit/9defe10371)] - **doc**: fix legacy stability indicator display (Rich Trott) [#37838](https://github.com/nodejs/node/pull/37838)
* \[[`f97a5dd22f`](https://github.com/nodejs/node/commit/f97a5dd22f)] - **doc**: use sentence-style capitlaztion in template header (Rich Trott) [#37837](https://github.com/nodejs/node/pull/37837)
* \[[`71fde07274`](https://github.com/nodejs/node/commit/71fde07274)] - **doc**: add Ayase-252 to triagers (Qingyu Deng) [#37781](https://github.com/nodejs/node/pull/37781)
* \[[`8f18133de0`](https://github.com/nodejs/node/commit/8f18133de0)] - **doc**: use sentence case in issues.md headers (marsonya) [#37537](https://github.com/nodejs/node/pull/37537)
* \[[`3376051a0e`](https://github.com/nodejs/node/commit/3376051a0e)] - **doc**: fix JS flavor selection (Antoine du Hamel) [#37791](https://github.com/nodejs/node/pull/37791)
* \[[`b09d032683`](https://github.com/nodejs/node/commit/b09d032683)] - **doc**: move Derek Lewis back to collaborators (Derek Lewis) [#37726](https://github.com/nodejs/node/pull/37726)
* \[[`6da0a0e85a`](https://github.com/nodejs/node/commit/6da0a0e85a)] - **doc**: apply style for legacy status (James M Snell) [#37784](https://github.com/nodejs/node/pull/37784)
* \[[`185d4cd4aa`](https://github.com/nodejs/node/commit/185d4cd4aa)] - **doc**: revoke deprecation of legacy url, change status to legacy (James M Snell) [#37784](https://github.com/nodejs/node/pull/37784)
* \[[`9d160daa89`](https://github.com/nodejs/node/commit/9d160daa89)] - **doc**: add legacy status to stability index (James M Snell) [#37784](https://github.com/nodejs/node/pull/37784)
* \[[`4700042a9b`](https://github.com/nodejs/node/commit/4700042a9b)] - **doc**: add @linkgoron to collaborators (Nitzan Uziely) [#37817](https://github.com/nodejs/node/pull/37817)
* \[[`c4183bbea4`](https://github.com/nodejs/node/commit/c4183bbea4)] - **doc**: fix AbortError example for timers (dbachko) [#37738](https://github.com/nodejs/node/pull/37738)
* \[[`50f3ad1946`](https://github.com/nodejs/node/commit/50f3ad1946)] - **doc**: fix typo in stream docs (Ian Kerins) [#37716](https://github.com/nodejs/node/pull/37716)
* \[[`2e82a97520`](https://github.com/nodejs/node/commit/2e82a97520)] - **doc**: add gyp maintain info (Jiawen Geng) [#37765](https://github.com/nodejs/node/pull/37765)
* \[[`3925458df7`](https://github.com/nodejs/node/commit/3925458df7)] - **doc,tools**: use only one level 1 header per page (Rich Trott) [#37839](https://github.com/nodejs/node/pull/37839)
* \[[`e9c161ce12`](https://github.com/nodejs/node/commit/e9c161ce12)] - **http**: fix double AbortSignal registration (Nitzan Uziely) [#37730](https://github.com/nodejs/node/pull/37730)
* \[[`a5205819d8`](https://github.com/nodejs/node/commit/a5205819d8)] - **(SEMVER-MINOR)** **http**: add http.ClientRequest.getRawHeaderNames() (simov) [#37660](https://github.com/nodejs/node/pull/37660)
* \[[`1c043272ea`](https://github.com/nodejs/node/commit/1c043272ea)] - **http2**: treat non-EOF empty frames like other invalid frames (Anna Henningsen) [#37875](https://github.com/nodejs/node/pull/37875)
* \[[`a5bf7de6eb`](https://github.com/nodejs/node/commit/a5bf7de6eb)] - **http2**: fix setting options before handle exists (Anna Henningsen) [#37875](https://github.com/nodejs/node/pull/37875)
* \[[`af7489cb6c`](https://github.com/nodejs/node/commit/af7489cb6c)] - **lib**: add brand checks to AbortController and AbortSignal (Mattias Buelens) [#37720](https://github.com/nodejs/node/pull/37720)
* \[[`6e2b60931c`](https://github.com/nodejs/node/commit/6e2b60931c)] - **lib**: fix typo in internal/modules/esm/module\_job.js (marsonya) [#37773](https://github.com/nodejs/node/pull/37773)
* \[[`3a440ecdf8`](https://github.com/nodejs/node/commit/3a440ecdf8)] - **lib**: fix typo in lib/internal/crypto/certificate.js (marsonya) [#37741](https://github.com/nodejs/node/pull/37741)
* \[[`3ab223dd32`](https://github.com/nodejs/node/commit/3ab223dd32)] - **node-api**: fix crash in finalization (Michael Dawson) [#37876](https://github.com/nodejs/node/pull/37876)
* \[[`d1a3e0efb6`](https://github.com/nodejs/node/commit/d1a3e0efb6)] - **node-api**: stop ref gc during environment teardown (Gabriel Schulhof) [#37616](https://github.com/nodejs/node/pull/37616)
* \[[`e60bd1a7dc`](https://github.com/nodejs/node/commit/e60bd1a7dc)] - **(SEMVER-MINOR)** **perf\_hooks**: make Performance extend EventTarget (Michaël Zasso) [#37621](https://github.com/nodejs/node/pull/37621)
* \[[`b6ad8e4cc1`](https://github.com/nodejs/node/commit/b6ad8e4cc1)] - **src**: indent long help text properly (David Glasser) [#37911](https://github.com/nodejs/node/pull/37911)
* \[[`13ecff63d6`](https://github.com/nodejs/node/commit/13ecff63d6)] - **src**: document newer values for --unhandled-rejections flag (David Glasser) [#37899](https://github.com/nodejs/node/pull/37899)
* \[[`bd87e195ed`](https://github.com/nodejs/node/commit/bd87e195ed)] - **src**: fix typo in src code guide (Tobias Nießen) [#37956](https://github.com/nodejs/node/pull/37956)
* \[[`2da532cef8`](https://github.com/nodejs/node/commit/2da532cef8)] - **src**: report idle time correctly (Stephen Belanger) [#37868](https://github.com/nodejs/node/pull/37868)
* \[[`836cb67945`](https://github.com/nodejs/node/commit/836cb67945)] - **src**: add .note.GNU-stack section (James Addison) [#37688](https://github.com/nodejs/node/pull/37688)
* \[[`9557dda2eb`](https://github.com/nodejs/node/commit/9557dda2eb)] - **(SEMVER-MINOR)** **stream**: pipeline accept Buffer as a valid first argument (Nitzan Uziely) [#37739](https://github.com/nodejs/node/pull/37739)
* \[[`43c3b43ea3`](https://github.com/nodejs/node/commit/43c3b43ea3)] - **stream**: make Readable.from performance better (wwwzbwcom) [#37609](https://github.com/nodejs/node/pull/37609)
* \[[`b0226b39f2`](https://github.com/nodejs/node/commit/b0226b39f2)] - **test**: split promisified timers test for coverage purposes (Rich Trott) [#37943](https://github.com/nodejs/node/pull/37943)
* \[[`e256c4d11d`](https://github.com/nodejs/node/commit/e256c4d11d)] - **test**: fix typeof comparison (Rich Trott) [#37924](https://github.com/nodejs/node/pull/37924)
* \[[`76ebc4bbd9`](https://github.com/nodejs/node/commit/76ebc4bbd9)] - **test**: increase wiggle room for memory in test-worker-resource-limits (Rich Trott) [#37901](https://github.com/nodejs/node/pull/37901)
* \[[`5cdeb76708`](https://github.com/nodejs/node/commit/5cdeb76708)] - **test**: add OpenSSL 3.0 checks to tls-passphrase (Daniel Bevenius) [#37860](https://github.com/nodejs/node/pull/37860)
* \[[`33c35a38dc`](https://github.com/nodejs/node/commit/33c35a38dc)] - **test**: add OpenSSL 3.0 checks to test-crypto-keygen (Daniel Bevenius) [#37860](https://github.com/nodejs/node/pull/37860)
* \[[`86bf341a35`](https://github.com/nodejs/node/commit/86bf341a35)] - **test**: fix deprecation warning in test-doctool-html (Antoine du Hamel) [#37858](https://github.com/nodejs/node/pull/37858)
* \[[`aa529b73b7`](https://github.com/nodejs/node/commit/aa529b73b7)] - **test**: fix ibmi skip message (Tobias Nießen) [#37821](https://github.com/nodejs/node/pull/37821)
* \[[`d9ab1d56ce`](https://github.com/nodejs/node/commit/d9ab1d56ce)] - **test**: fix flaky test-vm-timeout-escape-promise-module-2 (Rich Trott) [#37842](https://github.com/nodejs/node/pull/37842)
* \[[`5d4c610727`](https://github.com/nodejs/node/commit/5d4c610727)] - **test**: remove duplicated test for eventtarget (himself65) [#37853](https://github.com/nodejs/node/pull/37853)
* \[[`44490af948`](https://github.com/nodejs/node/commit/44490af948)] - **test**: relax Y2K38 check in test-fs-utimes-y2K38 (Richard Lau) [#37825](https://github.com/nodejs/node/pull/37825)
* \[[`9bc6fe7eb3`](https://github.com/nodejs/node/commit/9bc6fe7eb3)] - **test**: remove references to unsupported AIX versions (Richard Lau) [#37826](https://github.com/nodejs/node/pull/37826)
* \[[`f07428ae51`](https://github.com/nodejs/node/commit/f07428ae51)] - **test**: remove skip for fixed test-benchmark-fs (Rich Trott) [#37803](https://github.com/nodejs/node/pull/37803)
* \[[`9f61cbd1fd`](https://github.com/nodejs/node/commit/9f61cbd1fd)] - **test**: account for OOM risks in heapsnapshot-near-heap-limit tests (Joyee Cheung) [#37761](https://github.com/nodejs/node/pull/37761)
* \[[`e85f311cf2`](https://github.com/nodejs/node/commit/e85f311cf2)] - **test**: refactor code to use AbortSignal.abort() (Wassim Chegham) [#37798](https://github.com/nodejs/node/pull/37798)
* \[[`6ed9e0bd81`](https://github.com/nodejs/node/commit/6ed9e0bd81)] - **test**: improve test-arm-math-illegal-instruction (marsonya) [#37670](https://github.com/nodejs/node/pull/37670)
* \[[`505f9c95d1`](https://github.com/nodejs/node/commit/505f9c95d1)] - **(SEMVER-MINOR)** **test**: app atob web platform tests (James M Snell) [#37529](https://github.com/nodejs/node/pull/37529)
* \[[`a8edf1aafe`](https://github.com/nodejs/node/commit/a8edf1aafe)] - **test**: add known\_issues test for #13683 (Rich Trott) [#37744](https://github.com/nodejs/node/pull/37744)
* \[[`4487483d9d`](https://github.com/nodejs/node/commit/4487483d9d)] - **test**: fix test-fs-utimes on non-Y2K38 file systems (Rich Trott) [#37707](https://github.com/nodejs/node/pull/37707)
* \[[`d44b268910`](https://github.com/nodejs/node/commit/d44b268910)] - **timers**: fix arbitrary object clearImmediate errors (Nitzan Uziely) [#37824](https://github.com/nodejs/node/pull/37824)
* \[[`b7e7384109`](https://github.com/nodejs/node/commit/b7e7384109)] - **tools**: improve valid-typeof lint rule (Rich Trott) [#37924](https://github.com/nodejs/node/pull/37924)
* \[[`ca93e52783`](https://github.com/nodejs/node/commit/ca93e52783)] - **tools**: simplify eslint comma-dangle configuration (tools) (Rich Trott) [#37883](https://github.com/nodejs/node/pull/37883)
* \[[`b5879efef1`](https://github.com/nodejs/node/commit/b5879efef1)] - **tools**: improve macos-firewall.sh output (Rich Trott) [#37846](https://github.com/nodejs/node/pull/37846)
* \[[`dbc4804468`](https://github.com/nodejs/node/commit/dbc4804468)] - **tools**: simplify eslint comma-dangle configuration (Rich Trott) [#37850](https://github.com/nodejs/node/pull/37850)
* \[[`0f2e142946`](https://github.com/nodejs/node/commit/0f2e142946)] - **tools**: make genv8constants.py Python3-compatible (Michaël Zasso) [#37835](https://github.com/nodejs/node/pull/37835)
* \[[`b6be472456`](https://github.com/nodejs/node/commit/b6be472456)] - **tools**: update gitignore for CMake (Jiawen Geng) [#37793](https://github.com/nodejs/node/pull/37793)
* \[[`2227aa61ea`](https://github.com/nodejs/node/commit/2227aa61ea)] - **tools**: partially detect quic support in shared\_openssl (James M Snell) [#37682](https://github.com/nodejs/node/pull/37682)
* \[[`01dcf4d1d8`](https://github.com/nodejs/node/commit/01dcf4d1d8)] - **tools**: update ESLint to 7.22.0 (Colin Ihrig) [#37734](https://github.com/nodejs/node/pull/37734)
* \[[`3452618905`](https://github.com/nodejs/node/commit/3452618905)] - **tty**: validate file descriptor to avoid int32 overflow (Antoine du Hamel) [#37809](https://github.com/nodejs/node/pull/37809)
* \[[`d33f446abd`](https://github.com/nodejs/node/commit/d33f446abd)] - **util**: remove unreachable inspect code (Rich Trott) [#37941](https://github.com/nodejs/node/pull/37941)

<a id="15.12.0"></a>

## 2021-03-17, Version 15.12.0 (Current), @danielleadams

### Notable Changes

* **crypto**:
  * add optional callback to crypto.sign and crypto.verify (Filip Skokan) [#37500](https://github.com/nodejs/node/pull/37500)
  * support JWK objects in create\*Key (Filip Skokan) [#37254](https://github.com/nodejs/node/pull/37254)
* **deps**:
  * switch openssl to quictls/openssl (James M Snell) [#37601](https://github.com/nodejs/node/pull/37601)
  * update to cjs-module-lexer\@1.1.0 (Guy Bedford) [#37712](https://github.com/nodejs/node/pull/37712)
* **fs**:
  * improve fsPromises writeFile performance (Nitzan Uziely) [#37610](https://github.com/nodejs/node/pull/37610)
  * improve fsPromises readFile performance (Nitzan Uziely) [#37608](https://github.com/nodejs/node/pull/37608)
* **lib**:
  * implement AbortSignal.abort() (James M Snell) [#37693](https://github.com/nodejs/node/pull/37693)
* **node-api**:
  * define version 8 (Gabriel Schulhof) [#37652](https://github.com/nodejs/node/pull/37652)
* **worker**:
  * add setEnvironmentData/getEnvironmentData (James M Snell) [#37486](https://github.com/nodejs/node/pull/37486)

### Commits

* \[[`44514600b2`](https://github.com/nodejs/node/commit/44514600b2)] - **assert,util**: fix commutativity edge case (Ruben Bridgewater) [#37711](https://github.com/nodejs/node/pull/37711)
* \[[`8666d777cc`](https://github.com/nodejs/node/commit/8666d777cc)] - **benchmark**: add benchmark for fsPromises.writeFile (Nitzan Uziely) [#37610](https://github.com/nodejs/node/pull/37610)
* \[[`e9028eb646`](https://github.com/nodejs/node/commit/e9028eb646)] - **cluster**: restructure to same prototype for cluster child (Yash Ladha) [#36610](https://github.com/nodejs/node/pull/36610)
* \[[`8e1257e26d`](https://github.com/nodejs/node/commit/8e1257e26d)] - **cluster**: clarify construct Handle (Jackson Tian) [#37385](https://github.com/nodejs/node/pull/37385)
* \[[`341ee31e15`](https://github.com/nodejs/node/commit/341ee31e15)] - **crypto**: reconcile duplicated code (James M Snell) [#37704](https://github.com/nodejs/node/pull/37704)
* \[[`a2d08d5dfd`](https://github.com/nodejs/node/commit/a2d08d5dfd)] - **crypto**: add internal error codes (Darshan Sen) [#37650](https://github.com/nodejs/node/pull/37650)
* \[[`922f2f0eb2`](https://github.com/nodejs/node/commit/922f2f0eb2)] - **(SEMVER-MINOR)** **crypto**: add optional callback to crypto.sign and crypto.verify (Filip Skokan) [#37500](https://github.com/nodejs/node/pull/37500)
* \[[`55e522ca23`](https://github.com/nodejs/node/commit/55e522ca23)] - **(SEMVER-MINOR)** **crypto**: support JWK objects in create\*Key (Filip Skokan) [#37254](https://github.com/nodejs/node/pull/37254)
* \[[`33180fad81`](https://github.com/nodejs/node/commit/33180fad81)] - **crypto**: add separate error for INVALID\_KEY\_TYPE (Darshan Sen) [#37555](https://github.com/nodejs/node/pull/37555)
* \[[`d81b9af1fc`](https://github.com/nodejs/node/commit/d81b9af1fc)] - **crypto**: improve randomUUID performance (Dawid Rusnak) [#37243](https://github.com/nodejs/node/pull/37243)
* \[[`23d654105f`](https://github.com/nodejs/node/commit/23d654105f)] - **crypto,test**: improve hmac coverage with webcrypto tests (obi-el) [#37571](https://github.com/nodejs/node/pull/37571)
* \[[`dfca2fac24`](https://github.com/nodejs/node/commit/dfca2fac24)] - **(SEMVER-MINOR)** **deps**: update to cjs-module-lexer\@1.1.0 (Guy Bedford) [#37712](https://github.com/nodejs/node/pull/37712)
* \[[`ce357c0c11`](https://github.com/nodejs/node/commit/ce357c0c11)] - **(SEMVER-MINOR)** **deps**: update archs files for OpenSSL-1.1.1+quic (James M Snell) [#37601](https://github.com/nodejs/node/pull/37601)
* \[[`6d77b6174f`](https://github.com/nodejs/node/commit/6d77b6174f)] - **(SEMVER-MINOR)** **deps**: switch openssl to quictls/openssl (James M Snell) [#37601](https://github.com/nodejs/node/pull/37601)
* \[[`3e1a46a6a8`](https://github.com/nodejs/node/commit/3e1a46a6a8)] - **deps**: upgrade npm to 7.6.3 (Ruy Adorno) [#37721](https://github.com/nodejs/node/pull/37721)
* \[[`b2fd00398c`](https://github.com/nodejs/node/commit/b2fd00398c)] - **deps**: V8: cherry-pick 1648e050cade (Colin Ihrig) [#37664](https://github.com/nodejs/node/pull/37664)
* \[[`7422453072`](https://github.com/nodejs/node/commit/7422453072)] - **deps**: upgrade npm to 7.6.1 (Ruy Adorno) [#37606](https://github.com/nodejs/node/pull/37606)
* \[[`89f3aa92b4`](https://github.com/nodejs/node/commit/89f3aa92b4)] - **doc**: add marsonya as a triager (marsonya) [#37667](https://github.com/nodejs/node/pull/37667)
* \[[`3710857de3`](https://github.com/nodejs/node/commit/3710857de3)] - **doc**: add hints to http.request() options (Luigi Pinca) [#37745](https://github.com/nodejs/node/pull/37745)
* \[[`5d793737d7`](https://github.com/nodejs/node/commit/5d793737d7)] - **(SEMVER-MINOR)** **doc**: update maintaining-openssl guide (James M Snell) [#37601](https://github.com/nodejs/node/pull/37601)
* \[[`1022d3d947`](https://github.com/nodejs/node/commit/1022d3d947)] - **doc**: recommend checking abortSignal.aborted first (James M Snell) [#37714](https://github.com/nodejs/node/pull/37714)
* \[[`764aa2dcee`](https://github.com/nodejs/node/commit/764aa2dcee)] - **doc**: fix link to googletest fixtures (Tobias Nießen) [#37698](https://github.com/nodejs/node/pull/37698)
* \[[`0d3cc2dc82`](https://github.com/nodejs/node/commit/0d3cc2dc82)] - **doc**: fix typo in description of close event (Tobias Nießen) [#37662](https://github.com/nodejs/node/pull/37662)
* \[[`e55058fed1`](https://github.com/nodejs/node/commit/e55058fed1)] - **doc**: use sentence case in README.md headers (marsonya) [#37645](https://github.com/nodejs/node/pull/37645)
* \[[`e7fc7a4c23`](https://github.com/nodejs/node/commit/e7fc7a4c23)] - **doc**: crypto esm examples (James M Snell) [#37594](https://github.com/nodejs/node/pull/37594)
* \[[`a3abd52e1e`](https://github.com/nodejs/node/commit/a3abd52e1e)] - **doc**: add localPort to http.request() options (Luigi Pinca) [#37586](https://github.com/nodejs/node/pull/37586)
* \[[`705bdfbe3e`](https://github.com/nodejs/node/commit/705bdfbe3e)] - **doc**: fix grammar errors in http document (Qingyu Deng) [#37265](https://github.com/nodejs/node/pull/37265)
* \[[`e5f7179d1e`](https://github.com/nodejs/node/commit/e5f7179d1e)] - **doc**: add document for http.OutgoingMessage (Qingyu Deng) [#37265](https://github.com/nodejs/node/pull/37265)
* \[[`7c0ce17e65`](https://github.com/nodejs/node/commit/7c0ce17e65)] - **doc**: fix typo in doc/guides/collaborator-guide.md (marsonya) [#37643](https://github.com/nodejs/node/pull/37643)
* \[[`60d8afa9ab`](https://github.com/nodejs/node/commit/60d8afa9ab)] - **doc**: document that module.evaluate fulfills as undefined (James M Snell) [#37663](https://github.com/nodejs/node/pull/37663)
* \[[`6192315cf3`](https://github.com/nodejs/node/commit/6192315cf3)] - **doc**: remove generated from dsaEncoding description (Marko Kaznovac) [#37459](https://github.com/nodejs/node/pull/37459)
* \[[`e4c8c50b28`](https://github.com/nodejs/node/commit/e4c8c50b28)] - **doc**: fix typos in /doc/api/fs.md (Merlin Luntke) [#37557](https://github.com/nodejs/node/pull/37557)
* \[[`ebc6f41072`](https://github.com/nodejs/node/commit/ebc6f41072)] - **doc**: fix linter issue (Antoine du Hamel) [#37657](https://github.com/nodejs/node/pull/37657)
* \[[`d17aab1775`](https://github.com/nodejs/node/commit/d17aab1775)] - **doc**: add esm examples for assert (James M Snell) [#37607](https://github.com/nodejs/node/pull/37607)
* \[[`366772bf87`](https://github.com/nodejs/node/commit/366772bf87)] - **doc**: add return type of readline.createInterface (Darshan Sen) [#37600](https://github.com/nodejs/node/pull/37600)
* \[[`f50db89a52`](https://github.com/nodejs/node/commit/f50db89a52)] - **doc**: change lang info string in fs JS snippets (Antoine du Hamel) [#37605](https://github.com/nodejs/node/pull/37605)
* \[[`5a9196e0e4`](https://github.com/nodejs/node/commit/5a9196e0e4)] - **doc**: apply sentence case to headers in pull-requests.md (marsonya) [#37602](https://github.com/nodejs/node/pull/37602)
* \[[`05badcf755`](https://github.com/nodejs/node/commit/05badcf755)] - **doc**: fix small typo in 15.11.0 release (Tierney Cyren) [#37590](https://github.com/nodejs/node/pull/37590)
* \[[`e0e7aa1058`](https://github.com/nodejs/node/commit/e0e7aa1058)] - **doc**: add top-level await syntax in vm.md (Antoine du Hamel) [#37077](https://github.com/nodejs/node/pull/37077)
* \[[`732d8ca811`](https://github.com/nodejs/node/commit/732d8ca811)] - **doc**: clarify that columnOffset applies only to the first line (James M Snell) [#37563](https://github.com/nodejs/node/pull/37563)
* \[[`267bbe3412`](https://github.com/nodejs/node/commit/267bbe3412)] - **doc**: document that NODE\_EXTRA\_CA\_CERTS is read only once (James M Snell) [#37562](https://github.com/nodejs/node/pull/37562)
* \[[`f56a805a0d`](https://github.com/nodejs/node/commit/f56a805a0d)] - **doc**: refactor signal info in child\_process.md (Darshan Sen) [#37528](https://github.com/nodejs/node/pull/37528)
* \[[`236ba04a79`](https://github.com/nodejs/node/commit/236ba04a79)] - **domain**: add name to monkey-patched emit function (Colin Ihrig) [#37550](https://github.com/nodejs/node/pull/37550)
* \[[`1c09776106`](https://github.com/nodejs/node/commit/1c09776106)] - **domain**: show falsy names as anonymous for DEP0097 (Colin Ihrig) [#37550](https://github.com/nodejs/node/pull/37550)
* \[[`5a49e3139e`](https://github.com/nodejs/node/commit/5a49e3139e)] - **errors**: remove experimental from --enable-source-maps (Benjamin Coe) [#37743](https://github.com/nodejs/node/pull/37743)
* \[[`e384291c90`](https://github.com/nodejs/node/commit/e384291c90)] - **events**: remove return value on addEventListener (James M Snell) [#37696](https://github.com/nodejs/node/pull/37696)
* \[[`ba91ef2d08`](https://github.com/nodejs/node/commit/ba91ef2d08)] - **fs**: improve fsPromises writeFile performance (Nitzan Uziely) [#37610](https://github.com/nodejs/node/pull/37610)
* \[[`3572299fc2`](https://github.com/nodejs/node/commit/3572299fc2)] - **fs**: add promisified readFile benchmark (Nitzan Uziely) [#37608](https://github.com/nodejs/node/pull/37608)
* \[[`b277776845`](https://github.com/nodejs/node/commit/b277776845)] - **fs**: improve fsPromises readFile performance (Nitzan Uziely) [#37608](https://github.com/nodejs/node/pull/37608)
* \[[`6688569a50`](https://github.com/nodejs/node/commit/6688569a50)] - **http**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#37654](https://github.com/nodejs/node/pull/37654)
* \[[`c737df64fe`](https://github.com/nodejs/node/commit/c737df64fe)] - **http2**: make res.req a normal property (Colin Ihrig) [#37706](https://github.com/nodejs/node/pull/37706)
* \[[`ac2f50b3fd`](https://github.com/nodejs/node/commit/ac2f50b3fd)] - **(SEMVER-MINOR)** **lib**: implement AbortSignal.abort() (James M Snell) [#37693](https://github.com/nodejs/node/pull/37693)
* \[[`12fb2ffc33`](https://github.com/nodejs/node/commit/12fb2ffc33)] - **lib**: use AbortError consistently (James M Snell) [#37715](https://github.com/nodejs/node/pull/37715)
* \[[`e63a25e2ff`](https://github.com/nodejs/node/commit/e63a25e2ff)] - **lib**: fix typo in lib/internal/http2/core.js (marsonya) [#37695](https://github.com/nodejs/node/pull/37695)
* \[[`852f53ed7e`](https://github.com/nodejs/node/commit/852f53ed7e)] - **lib**: fix typo in lib/internal/bootstrap/loaders.js (marsonya) [#37644](https://github.com/nodejs/node/pull/37644)
* \[[`daa4ac54c5`](https://github.com/nodejs/node/commit/daa4ac54c5)] - **lib**: remove use of array destructuring (Antoine du Hamel) [#36818](https://github.com/nodejs/node/pull/36818)
* \[[`ae0e76c264`](https://github.com/nodejs/node/commit/ae0e76c264)] - **module**: refactor NativeModule to avoid unsafe array iteration (Antoine du Hamel) [#37656](https://github.com/nodejs/node/pull/37656)
* \[[`a86334fbb9`](https://github.com/nodejs/node/commit/a86334fbb9)] - **(SEMVER-MINOR)** **node-api**: define version 8 (Gabriel Schulhof) [#37652](https://github.com/nodejs/node/pull/37652)
* \[[`d28ce328ed`](https://github.com/nodejs/node/commit/d28ce328ed)] - **src**: fix variable name of OnCloseReceived callback (Tobias Nießen) [#37521](https://github.com/nodejs/node/pull/37521)
* \[[`d59c6de7e8`](https://github.com/nodejs/node/commit/d59c6de7e8)] - **src**: add error formatting support (Gus Caplan) [#37598](https://github.com/nodejs/node/pull/37598)
* \[[`33436e39fe`](https://github.com/nodejs/node/commit/33436e39fe)] - **src**: make BaseObject::is\_snapshotable virtual (Anna Henningsen) [#37539](https://github.com/nodejs/node/pull/37539)
* \[[`30c62dee1c`](https://github.com/nodejs/node/commit/30c62dee1c)] - **src,test**: support dynamically linking OpenSSL 3.0 (Daniel Bevenius) [#37669](https://github.com/nodejs/node/pull/37669)
* \[[`4bf1f333c7`](https://github.com/nodejs/node/commit/4bf1f333c7)] - **stream,util**: fix "the the" typo in comments (Luigi Pinca) [#37674](https://github.com/nodejs/node/pull/37674)
* \[[`1b53087541`](https://github.com/nodejs/node/commit/1b53087541)] - **(SEMVER-MINOR)** **test**: update dom/abort tests (James M Snell) [#37693](https://github.com/nodejs/node/pull/37693)
* \[[`c2cb153646`](https://github.com/nodejs/node/commit/c2cb153646)] - **(SEMVER-MINOR)** **test**: fixup test to account for quic openssl version (James M Snell) [#37601](https://github.com/nodejs/node/pull/37601)
* \[[`ede34aa128`](https://github.com/nodejs/node/commit/ede34aa128)] - **test**: address flaky wpt/test-timers (Rich Trott) [#37691](https://github.com/nodejs/node/pull/37691)
* \[[`ed32cd4e67`](https://github.com/nodejs/node/commit/ed32cd4e67)] - **test**: fixup flaky test-crypto-x509 (Filip Skokan) [#37709](https://github.com/nodejs/node/pull/37709)
* \[[`013b3ff2d4`](https://github.com/nodejs/node/commit/013b3ff2d4)] - **test**: remove unnecessary V8 flag (Antoine du Hamel) [#37671](https://github.com/nodejs/node/pull/37671)
* \[[`cc48816826`](https://github.com/nodejs/node/commit/cc48816826)] - **test**: fix WPT URL tests that fetch JSON data (Michaël Zasso) [#37624](https://github.com/nodejs/node/pull/37624)
* \[[`b0ed1e790e`](https://github.com/nodejs/node/commit/b0ed1e790e)] - **test**: improve error reporting in test-child-process-pipe-dataflow (Rich Trott) [#37632](https://github.com/nodejs/node/pull/37632)
* \[[`f7edb07ec2`](https://github.com/nodejs/node/commit/f7edb07ec2)] - **test**: terminate WPT workers after test completion (Michaël Zasso) [#37627](https://github.com/nodejs/node/pull/37627)
* \[[`b7ef829dac`](https://github.com/nodejs/node/commit/b7ef829dac)] - **test**: ignore WPT worker errors after tests finished (Michaël Zasso) [#37626](https://github.com/nodejs/node/pull/37626)
* \[[`257b1ab225`](https://github.com/nodejs/node/commit/257b1ab225)] - **test**: update Web Platform Tests (Michaël Zasso) [#37620](https://github.com/nodejs/node/pull/37620)
* \[[`1f6341852f`](https://github.com/nodejs/node/commit/1f6341852f)] - **test**: remove FLAKY status for test-async-hooks-http-parser-destroy (Rich Trott) [#37636](https://github.com/nodejs/node/pull/37636)
* \[[`044fd2fc86`](https://github.com/nodejs/node/commit/044fd2fc86)] - **test**: remove FLAKY status for fixed test (Rich Trott) [#37633](https://github.com/nodejs/node/pull/37633)
* \[[`d5ff50d2a7`](https://github.com/nodejs/node/commit/d5ff50d2a7)] - **test**: clear flaky designation for test-stream-pipeline-http2 (Rich Trott) [#37631](https://github.com/nodejs/node/pull/37631)
* \[[`381fb98061`](https://github.com/nodejs/node/commit/381fb98061)] - **test**: clear FLAKY designation for test-http2-pipe (Rich Trott) [#37631](https://github.com/nodejs/node/pull/37631)
* \[[`0582c51754`](https://github.com/nodejs/node/commit/0582c51754)] - **test**: fix wasi/test-return-on-exit on 32-bit systems (Colin Ihrig) [#37615](https://github.com/nodejs/node/pull/37615)
* \[[`0d04b6c043`](https://github.com/nodejs/node/commit/0d04b6c043)] - **test**: fix flaky test-child-process-exec-abortcontroller-promisified (Antoine du Hamel) [#37572](https://github.com/nodejs/node/pull/37572)
* \[[`a44daff34d`](https://github.com/nodejs/node/commit/a44daff34d)] - **test**: update all Web Platform Tests (Michaël Zasso) [#37467](https://github.com/nodejs/node/pull/37467)
* \[[`c09bd77daf`](https://github.com/nodejs/node/commit/c09bd77daf)] - **test**: redownload wpt fixtures with correct encoding (Michaël Zasso) [#37467](https://github.com/nodejs/node/pull/37467)
* \[[`57319770bb`](https://github.com/nodejs/node/commit/57319770bb)] - **test,crypto**: ensure promises resolve in webcrypto tests (Antoine du Hamel) [#37653](https://github.com/nodejs/node/pull/37653)
* \[[`2d9b624668`](https://github.com/nodejs/node/commit/2d9b624668)] - **tls**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#37655](https://github.com/nodejs/node/pull/37655)
* \[[`72af5d9895`](https://github.com/nodejs/node/commit/72af5d9895)] - **tools**: parse changelogs only in the default branch (Antoine du Hamel) [#37768](https://github.com/nodejs/node/pull/37768)
* \[[`bd62771a22`](https://github.com/nodejs/node/commit/bd62771a22)] - **tools**: use bundled npm in update scripts (Ruy Adorno) [#37613](https://github.com/nodejs/node/pull/37613)
* \[[`4de3b8483a`](https://github.com/nodejs/node/commit/4de3b8483a)] - **tools**: update glob-parent to 5.1.2 (Rich Trott) [#37646](https://github.com/nodejs/node/pull/37646)
* \[[`ec71a0f817`](https://github.com/nodejs/node/commit/ec71a0f817)] - **tools**: check version number in YAML comments from changelogs (Antoine du Hamel) [#37599](https://github.com/nodejs/node/pull/37599)
* \[[`07fc61b900`](https://github.com/nodejs/node/commit/07fc61b900)] - **tools**: add support for mjs and cjs JS snippet linting (Antoine du Hamel) [#37311](https://github.com/nodejs/node/pull/37311)
* \[[`440c944420`](https://github.com/nodejs/node/commit/440c944420)] - **tools**: fix object name in prefer-assert-methods.js (Tobias Nießen) [#37544](https://github.com/nodejs/node/pull/37544)
* \[[`7042ec89f1`](https://github.com/nodejs/node/commit/7042ec89f1)] - **tools**: update remark-preset-lint-node to 2.1.1 (Rich Trott) [#37604](https://github.com/nodejs/node/pull/37604)
* \[[`82e78f7c12`](https://github.com/nodejs/node/commit/82e78f7c12)] - **tools**: fix compiler warning in inspector\_protocol (Darshan Sen) [#37573](https://github.com/nodejs/node/pull/37573)
* \[[`fd7234c52f`](https://github.com/nodejs/node/commit/fd7234c52f)] - **tools**: make update-eslint.sh work with npm\@7 (Luigi Pinca) [#37566](https://github.com/nodejs/node/pull/37566)
* \[[`057c6a842a`](https://github.com/nodejs/node/commit/057c6a842a)] - **tools**: add ESLint rule no-array-destructuring (Antoine du Hamel) [#36818](https://github.com/nodejs/node/pull/36818)
* \[[`25a5f0b3b8`](https://github.com/nodejs/node/commit/25a5f0b3b8)] - **tools**: update eslint-plugin-markdown configuration (Colin Ihrig) [#37549](https://github.com/nodejs/node/pull/37549)
* \[[`7a1de1fce9`](https://github.com/nodejs/node/commit/7a1de1fce9)] - **tools**: update ESLint to 7.21.0 (Luigi Pinca) [#37546](https://github.com/nodejs/node/pull/37546)
* \[[`9c0ca4689d`](https://github.com/nodejs/node/commit/9c0ca4689d)] - **tools,doc**: add support for several flavors of JS code snippets (Antoine du Hamel) [#37162](https://github.com/nodejs/node/pull/37162)
* \[[`80af610d95`](https://github.com/nodejs/node/commit/80af610d95)] - **util**: inspect \_\_proto\_\_ key as written in object literal (Anna Henningsen) [#37713](https://github.com/nodejs/node/pull/37713)
* \[[`0d135e8316`](https://github.com/nodejs/node/commit/0d135e8316)] - **(SEMVER-MINOR)** **worker**: add setEnvironmentData/getEnvironmentData (James M Snell) [#37486](https://github.com/nodejs/node/pull/37486)
* \[[`8024ffbba4`](https://github.com/nodejs/node/commit/8024ffbba4)] - **worker**: add ports property to MessageEvents (Anna Henningsen) [#37538](https://github.com/nodejs/node/pull/37538)
* \[[`f4fd3fb6a7`](https://github.com/nodejs/node/commit/f4fd3fb6a7)] - **worker**: allow BroadcastChannel in receiveMessageOnPort (Anna Henningsen) [#37535](https://github.com/nodejs/node/pull/37535)

<a id="15.11.0"></a>

## 2021-03-03, Version 15.11.0 (Current), @targos

### Notable Changes

* \[[`a3e3156b52`](https://github.com/nodejs/node/commit/a3e3156b52)] - **(SEMVER-MINOR)** **crypto**: make FIPS related options always available (Vít Ondruch) [#36341](https://github.com/nodejs/node/pull/36341)
* \[[`9ba5c0f9ba`](https://github.com/nodejs/node/commit/9ba5c0f9ba)] - **(SEMVER-MINOR)** **errors**: remove experimental from --enable-source-maps (Benjamin Coe) [#37362](https://github.com/nodejs/node/pull/37362)

### Commits

* \[[`d039e6fa80`](https://github.com/nodejs/node/commit/d039e6fa80)] - **assert**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#37344](https://github.com/nodejs/node/pull/37344)
* \[[`d2e5529e08`](https://github.com/nodejs/node/commit/d2e5529e08)] - **bootstrap**: include v8 module into the builtin snapshot (Joyee Cheung) [#36943](https://github.com/nodejs/node/pull/36943)
* \[[`59861bac0e`](https://github.com/nodejs/node/commit/59861bac0e)] - **bootstrap**: include fs module into the builtin snapshot (Joyee Cheung) [#36943](https://github.com/nodejs/node/pull/36943)
* \[[`458a4108b7`](https://github.com/nodejs/node/commit/458a4108b7)] - **buffer**: make Blob's constructor more spec-compliant (Michaël Zasso) [#37361](https://github.com/nodejs/node/pull/37361)
* \[[`0d564ce214`](https://github.com/nodejs/node/commit/0d564ce214)] - **buffer**: make Blob's slice method more spec-compliant (Michaël Zasso) [#37361](https://github.com/nodejs/node/pull/37361)
* \[[`ddae112133`](https://github.com/nodejs/node/commit/ddae112133)] - **child\_process**: fix spawn and fork abort behavior (Nitzan Uziely) [#37325](https://github.com/nodejs/node/pull/37325)
* \[[`b1e188de8d`](https://github.com/nodejs/node/commit/b1e188de8d)] - **crypto**: refactor hasAnyNotIn to avoid unsafe array iteration (Antoine du Hamel) [#37433](https://github.com/nodejs/node/pull/37433)
* \[[`291d9e9936`](https://github.com/nodejs/node/commit/291d9e9936)] - **crypto**: check ed/x webcrypto key import algorithm names (Filip Skokan) [#37305](https://github.com/nodejs/node/pull/37305)
* \[[`a3e3156b52`](https://github.com/nodejs/node/commit/a3e3156b52)] - **(SEMVER-MINOR)** **crypto**: make FIPS related options always awailable (Vít Ondruch) [#36341](https://github.com/nodejs/node/pull/36341)
* \[[`b634469c38`](https://github.com/nodejs/node/commit/b634469c38)] - **crypto**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#37364](https://github.com/nodejs/node/pull/37364)
* \[[`01773ab614`](https://github.com/nodejs/node/commit/01773ab614)] - **crypto**: use BoringSSL compatible errors (Shelley Vohr) [#37297](https://github.com/nodejs/node/pull/37297)
* \[[`f3d67000a0`](https://github.com/nodejs/node/commit/f3d67000a0)] - **deps**: upgrade npm to 7.6.0 (Ruy Adorno) [#37559](https://github.com/nodejs/node/pull/37559)
* \[[`e1045f1004`](https://github.com/nodejs/node/commit/e1045f1004)] - **deps**: upgrade npm to 7.5.6 (Ruy Adorno) [#37496](https://github.com/nodejs/node/pull/37496)
* \[[`80d3c118f4`](https://github.com/nodejs/node/commit/80d3c118f4)] - **deps**: V8: cherry-pick 373f4ae739ee (Richard Lau) [#37505](https://github.com/nodejs/node/pull/37505)
* \[[`1408de7e24`](https://github.com/nodejs/node/commit/1408de7e24)] - **deps**: cherry-pick 8957d4677aa794c230577f234071af0 from V8 upstream (Antoine du Hamel) [#37471](https://github.com/nodejs/node/pull/37471)
* \[[`725d48ae77`](https://github.com/nodejs/node/commit/725d48ae77)] - **doc**: remove experimental from --enable-source-maps (Colin Ihrig) [#37540](https://github.com/nodejs/node/pull/37540)
* \[[`5d939b7a49`](https://github.com/nodejs/node/commit/5d939b7a49)] - **doc**: fix typo in doc/api/packages.md (marsonya) [#37536](https://github.com/nodejs/node/pull/37536)
* \[[`cbfc6b1692`](https://github.com/nodejs/node/commit/cbfc6b1692)] - **doc**: document how to register external bindings for snapshot (Joyee Cheung) [#37463](https://github.com/nodejs/node/pull/37463)
* \[[`dd7a04dc9f`](https://github.com/nodejs/node/commit/dd7a04dc9f)] - **doc**: fix typo "director" instead of "directory" (humanwebpl) [#37523](https://github.com/nodejs/node/pull/37523)
* \[[`ba81e7cb5e`](https://github.com/nodejs/node/commit/ba81e7cb5e)] - **doc**: revise LTS text in collaborator guide (Rich Trott) [#37527](https://github.com/nodejs/node/pull/37527)
* \[[`7529a97a5c`](https://github.com/nodejs/node/commit/7529a97a5c)] - **doc**: revise CI text in collaborator guide (Rich Trott) [#37526](https://github.com/nodejs/node/pull/37526)
* \[[`1285b907ce`](https://github.com/nodejs/node/commit/1285b907ce)] - **doc**: revise objections section of collaborator guide (Rich Trott) [#37525](https://github.com/nodejs/node/pull/37525)
* \[[`bc86208a0a`](https://github.com/nodejs/node/commit/bc86208a0a)] - **doc**: revise premature disclosure text in collaborator guide (Rich Trott) [#37524](https://github.com/nodejs/node/pull/37524)
* \[[`46af56752e`](https://github.com/nodejs/node/commit/46af56752e)] - **doc**: change links to use HEAD in top level docs (Michael Dawson) [#37494](https://github.com/nodejs/node/pull/37494)
* \[[`3b737e63ce`](https://github.com/nodejs/node/commit/3b737e63ce)] - **doc**: apply sentence case to headers in doc/guides (marsonya) [#37506](https://github.com/nodejs/node/pull/37506)
* \[[`fb5e5bed21`](https://github.com/nodejs/node/commit/fb5e5bed21)] - **doc**: fix typo in webcrypto.md (marsonya) [#37507](https://github.com/nodejs/node/pull/37507)
* \[[`3b7cb75554`](https://github.com/nodejs/node/commit/3b7cb75554)] - **doc**: document the NO\_COLOR and FORCE\_COLOR env vars (James M Snell) [#37477](https://github.com/nodejs/node/pull/37477)
* \[[`0fac27d546`](https://github.com/nodejs/node/commit/0fac27d546)] - **doc**: add url.resolve replacement example (Antoine du Hamel) [#37501](https://github.com/nodejs/node/pull/37501)
* \[[`2228f44b25`](https://github.com/nodejs/node/commit/2228f44b25)] - **doc**: apply sentence case to guides headers (marsonya) [#37497](https://github.com/nodejs/node/pull/37497)
* \[[`617819e4fb`](https://github.com/nodejs/node/commit/617819e4fb)] - **doc**: update CI requirements for landing pull requests (Antoine du Hamel) [#37308](https://github.com/nodejs/node/pull/37308)
* \[[`4a40759b33`](https://github.com/nodejs/node/commit/4a40759b33)] - **doc**: recommend queueMicrotask over process.nextTick (James M Snell) [#37484](https://github.com/nodejs/node/pull/37484)
* \[[`834f63793a`](https://github.com/nodejs/node/commit/834f63793a)] - **doc**: apply sentence case to headers in doc/guides (marsonya) [#37478](https://github.com/nodejs/node/pull/37478)
* \[[`7ac0820da0`](https://github.com/nodejs/node/commit/7ac0820da0)] - **doc**: fix typo in doc/api/http2/md (marsonya) [#37479](https://github.com/nodejs/node/pull/37479)
* \[[`4ad7a78448`](https://github.com/nodejs/node/commit/4ad7a78448)] - **doc**: alphabetize vm Module class properties (Rich Trott) [#37451](https://github.com/nodejs/node/pull/37451)
* \[[`a193d7ca87`](https://github.com/nodejs/node/commit/a193d7ca87)] - **doc**: alphabetize crypto Cipher class entries (Rich Trott) [#37450](https://github.com/nodejs/node/pull/37450)
* \[[`54b6f1bcf9`](https://github.com/nodejs/node/commit/54b6f1bcf9)] - **doc**: use HEAD for links in api docs (Michael Dawson) [#37437](https://github.com/nodejs/node/pull/37437)
* \[[`549d24b8ad`](https://github.com/nodejs/node/commit/549d24b8ad)] - **doc**: fix alignment of parameters (Michael Dawson) [#37422](https://github.com/nodejs/node/pull/37422)
* \[[`f3559a922b`](https://github.com/nodejs/node/commit/f3559a922b)] - **doc**: fix typo in doc/api/esm.md (marsonya) [#37400](https://github.com/nodejs/node/pull/37400)
* \[[`c3d236d405`](https://github.com/nodejs/node/commit/c3d236d405)] - **doc**: fix "referred to" in fs docs (Tobias Nießen) [#37388](https://github.com/nodejs/node/pull/37388)
* \[[`9ac8c74539`](https://github.com/nodejs/node/commit/9ac8c74539)] - **doc**: document x509 error codes (Dan Čermák) [#37096](https://github.com/nodejs/node/pull/37096)
* \[[`9a454afcd6`](https://github.com/nodejs/node/commit/9a454afcd6)] - **doc**: fix typo in esm.md (Jay Tailor) [#37417](https://github.com/nodejs/node/pull/37417)
* \[[`b3bf3d9824`](https://github.com/nodejs/node/commit/b3bf3d9824)] - **doc**: use HEAD in links where possible (Michael Dawson) [#37421](https://github.com/nodejs/node/pull/37421)
* \[[`6675342cd9`](https://github.com/nodejs/node/commit/6675342cd9)] - **doc**: clarify that async\_hook callbacks cannot be async (James M Snell) [#37384](https://github.com/nodejs/node/pull/37384)
* \[[`4b54c10500`](https://github.com/nodejs/node/commit/4b54c10500)] - **doc**: use \*\*Default:\*\* more consistently (Colin Ihrig) [#37387](https://github.com/nodejs/node/pull/37387)
* \[[`f20ce47dbb`](https://github.com/nodejs/node/commit/f20ce47dbb)] - **doc,child\_process**: `pid` can be `undefined` when `ENOENT` (dr-js) [#37014](https://github.com/nodejs/node/pull/37014)
* \[[`6205e29cb9`](https://github.com/nodejs/node/commit/6205e29cb9)] - **doc,lib**: prepare for stricter multi-line array linting (Rich Trott) [#37088](https://github.com/nodejs/node/pull/37088)
* \[[`9ba5c0f9ba`](https://github.com/nodejs/node/commit/9ba5c0f9ba)] - **(SEMVER-MINOR)** **errors**: remove experimental from --enable-source-maps (Benjamin Coe) [#37362](https://github.com/nodejs/node/pull/37362)
* \[[`c0cdb83433`](https://github.com/nodejs/node/commit/c0cdb83433)] - **fs**: fix writeFile signal does not close file (Nitzan Uziely) [#37402](https://github.com/nodejs/node/pull/37402)
* \[[`e8b1e2c0a3`](https://github.com/nodejs/node/commit/e8b1e2c0a3)] - **fs**: fix pre-aborted writeFile AbortSignal file leak (Nitzan Uziely) [#37393](https://github.com/nodejs/node/pull/37393)
* \[[`6b42e65983`](https://github.com/nodejs/node/commit/6b42e65983)] - **fs**: fixup negative length in fs.truncate (James M Snell) [#37483](https://github.com/nodejs/node/pull/37483)
* \[[`d141fce634`](https://github.com/nodejs/node/commit/d141fce634)] - **fs**: use createDeferredPromise() in promises.watch() (Colin Ihrig) [#37386](https://github.com/nodejs/node/pull/37386)
* \[[`bb81accb16`](https://github.com/nodejs/node/commit/bb81accb16)] - **lib**: use \<array>.push and \<array>.unshift instead of \<array>.concat (Antoine du Hamel) [#37239](https://github.com/nodejs/node/pull/37239)
* \[[`dc3c299862`](https://github.com/nodejs/node/commit/dc3c299862)] - **lib**: remove outdated todo comment (Antoine du Hamel) [#37396](https://github.com/nodejs/node/pull/37396)
* \[[`856d20b772`](https://github.com/nodejs/node/commit/856d20b772)] - **lib**: add URI handling functions to primordials (Antoine du Hamel) [#37394](https://github.com/nodejs/node/pull/37394)
* \[[`a1ed78cb3b`](https://github.com/nodejs/node/commit/a1ed78cb3b)] - **module**: improve support of data: URLs (Antoine du Hamel) [#37392](https://github.com/nodejs/node/pull/37392)
* \[[`27816eac61`](https://github.com/nodejs/node/commit/27816eac61)] - **node-api**: force env shutdown deferring behavior (Gabriel Schulhof) [#37303](https://github.com/nodejs/node/pull/37303)
* \[[`f1381f7a7a`](https://github.com/nodejs/node/commit/f1381f7a7a)] - **src**: fix alloc-dealloc-mismatch in node\_snapshotable.h (Darshan Sen) [#37443](https://github.com/nodejs/node/pull/37443)
* \[[`5ea2ed611f`](https://github.com/nodejs/node/commit/5ea2ed611f)] - **src**: fix ETW\_WRITE\_EMPTY\_EVENT macro (Michaël Zasso) [#37334](https://github.com/nodejs/node/pull/37334)
* \[[`96bcd52d3e`](https://github.com/nodejs/node/commit/96bcd52d3e)] - **src**: disable unfixable MSVC warnings (Michaël Zasso) [#37334](https://github.com/nodejs/node/pull/37334)
* \[[`c75f5f372d`](https://github.com/nodejs/node/commit/c75f5f372d)] - **src**: avoid implicit type conversions (take 2) (Michaël Zasso) [#37334](https://github.com/nodejs/node/pull/37334)
* \[[`e400f8c9c8`](https://github.com/nodejs/node/commit/e400f8c9c8)] - **src**: support serialization of binding data (Joyee Cheung) [#36943](https://github.com/nodejs/node/pull/36943)
* \[[`daad7bbd34`](https://github.com/nodejs/node/commit/daad7bbd34)] - **src**: adjust THP sysfs config token retrieval and file closure (James Addison) [#37187](https://github.com/nodejs/node/pull/37187)
* \[[`4cc76457d9`](https://github.com/nodejs/node/commit/4cc76457d9)] - **stream**: move duplicated code to an internal module (Rich Trott) [#37508](https://github.com/nodejs/node/pull/37508)
* \[[`3d3df0c005`](https://github.com/nodejs/node/commit/3d3df0c005)] - **stream**: add AbortSignal support to finished (Nitzan Uziely) [#37354](https://github.com/nodejs/node/pull/37354)
* \[[`429dffd32e`](https://github.com/nodejs/node/commit/429dffd32e)] - **stream**: add AbortSignal to promisified pipeline (Nitzan Uziely) [#37359](https://github.com/nodejs/node/pull/37359)
* \[[`9696cf7142`](https://github.com/nodejs/node/commit/9696cf7142)] - **test**: remove FLAKY status for test-http2-multistream-destroy-on-read-tls (Rich Trott) [#37533](https://github.com/nodejs/node/pull/37533)
* \[[`453113938d`](https://github.com/nodejs/node/commit/453113938d)] - **test**: make status file names consistent (Rich Trott) [#37532](https://github.com/nodejs/node/pull/37532)
* \[[`00b3446a8e`](https://github.com/nodejs/node/commit/00b3446a8e)] - **test**: account for pending deprecations in esm test (Rich Trott) [#37542](https://github.com/nodejs/node/pull/37542)
* \[[`f2aa305348`](https://github.com/nodejs/node/commit/f2aa305348)] - **test**: fix incorrect timers-promisified case (ttzztztz) [#37425](https://github.com/nodejs/node/pull/37425)
* \[[`ce7fbbf94c`](https://github.com/nodejs/node/commit/ce7fbbf94c)] - **test**: fix typo in test\_node\_crypto.cc (Ikko Ashimine) [#37469](https://github.com/nodejs/node/pull/37469)
* \[[`ba319f0c60`](https://github.com/nodejs/node/commit/ba319f0c60)] - **test**: remove FLAKY for test-http2-compat-client-upload-reject (Rich Trott) [#37462](https://github.com/nodejs/node/pull/37462)
* \[[`dfa0440341`](https://github.com/nodejs/node/commit/dfa0440341)] - **test**: validate no debug info for http2 (Michael Dawson) [#37447](https://github.com/nodejs/node/pull/37447)
* \[[`b38404ee17`](https://github.com/nodejs/node/commit/b38404ee17)] - **test**: remove FLAKY designation for test-http2-client-upload-reject (Rich Trott) [#37461](https://github.com/nodejs/node/pull/37461)
* \[[`b569105183`](https://github.com/nodejs/node/commit/b569105183)] - **test**: clarify usage of tmpdir.refresh() (Darshan Sen) [#37383](https://github.com/nodejs/node/pull/37383)
* \[[`4f41900687`](https://github.com/nodejs/node/commit/4f41900687)] - **test**: update upload.zip to be uncorrupted (Greg Ziskind) [#37294](https://github.com/nodejs/node/pull/37294)
* \[[`d5c311ed15`](https://github.com/nodejs/node/commit/d5c311ed15)] - **test**: fix flaky test-worker-prof (Rich Trott) [#37372](https://github.com/nodejs/node/pull/37372)
* \[[`df538ebc8e`](https://github.com/nodejs/node/commit/df538ebc8e)] - **test**: fix flaky test-webcrypto-encrypt-decrypt-aes (Darshan Sen) [#37380](https://github.com/nodejs/node/pull/37380)
* \[[`19d6eb929c`](https://github.com/nodejs/node/commit/19d6eb929c)] - **test**: fix flaky test-fs-promises-file-handle-read (Rich Trott) [#37371](https://github.com/nodejs/node/pull/37371)
* \[[`c554aa149c`](https://github.com/nodejs/node/commit/c554aa149c)] - **test,child\_process**: add check for `subProcess.pid` (dr-js) [#37014](https://github.com/nodejs/node/pull/37014)
* \[[`5c27fd73b0`](https://github.com/nodejs/node/commit/5c27fd73b0)] - **tools**: run doctool tests on GitHub Actions CI (Antoine du Hamel) [#37398](https://github.com/nodejs/node/pull/37398)
* \[[`49013fcee1`](https://github.com/nodejs/node/commit/49013fcee1)] - **tools**: make comma-dangle ESLint rule more stringent … (Rich Trott) [#37088](https://github.com/nodejs/node/pull/37088)
* \[[`31f4600b7a`](https://github.com/nodejs/node/commit/31f4600b7a)] - **worker**: fix interaction of terminate() with messaging port (Anna Henningsen) [#37319](https://github.com/nodejs/node/pull/37319)
* \[[`d93137b2a9`](https://github.com/nodejs/node/commit/d93137b2a9)] - **workers**: fix spawning from preload scripts (James M Snell) [#37481](https://github.com/nodejs/node/pull/37481)

<a id="15.10.0"></a>

## 2021-02-23, Version 15.10.0 (Current), @BethGriggs

This is a security release.

### Notable changes

Vulnerabilities fixed:

* **CVE-2021-22883**: HTTP2 'unknownProtocol' cause Denial of Service by resource exhaustion
  * Affected Node.js versions are vulnerable to denial of service attacks when too many connection attempts with an 'unknownProtocol' are established. This leads to a leak of file descriptors. If a file descriptor limit is configured on the system, then the server is unable to accept new connections and prevent the process also from opening, e.g. a file. If no file descriptor limit is configured, then this lead to an excessive memory usage and cause the system to run out of memory.
* **CVE-2021-22884**: DNS rebinding in --inspect
  * Affected Node.js versions are vulnerable to denial of service attacks when the whitelist includes “localhost6”. When “localhost6” is not present in /etc/hosts, it is just an ordinary domain that is resolved via DNS, i.e., over network. If the attacker controls the victim's DNS server or can spoof its responses, the DNS rebinding protection can be bypassed by using the “localhost6” domain. As long as the attacker uses the “localhost6” domain, they can still apply the attack described in CVE-2018-7160.
* **CVE-2021-23840**: OpenSSL - Integer overflow in CipherUpdate
  * This is a vulnerability in OpenSSL which may be exploited through Node.js. You can read more about it in
    <https://www.openssl.org/news/secadv/20210216.txt>

### Commits

* \[[`2a3ce5974b`](https://github.com/nodejs/node/commit/2a3ce5974b)] - **deps**: update archs files for OpenSSL-1.1.1j (Daniel Bevenius) [#37412](https://github.com/nodejs/node/pull/37412)
* \[[`afbce66874`](https://github.com/nodejs/node/commit/afbce66874)] - **deps**: upgrade openssl sources to 1.1.1j (Daniel Bevenius) [#37412](https://github.com/nodejs/node/pull/37412)
* \[[`4184806dee`](https://github.com/nodejs/node/commit/4184806dee)] - **(SEMVER-MINOR)** **http2**: add unknownProtocol timeout (Daniel Bevenius) [nodejs-private/node-private#246](https://github.com/nodejs-private/node-private/pull/246)
* \[[`43ae9c46c3`](https://github.com/nodejs/node/commit/43ae9c46c3)] - **src**: drop localhost6 as allowed host for inspector (Matteo Collina) [nodejs-private/node-private#244](https://github.com/nodejs-private/node-private/pull/244)

<a id="15.9.0"></a>

## 2021-02-17, Version 15.9.0 (Current), @danielleadams

### Notable Changes

* **crypto**:
  * add keyObject.export() 'jwk' format option (Filip Skokan) [#37081](https://github.com/nodejs/node/pull/37081)
* **deps**:
  * upgrade to libuv 1.41.0 (Colin Ihrig) [#37360](https://github.com/nodejs/node/pull/37360)
* **doc**:
  * add dmabupt to collaborators (Xu Meng) [#37377](https://github.com/nodejs/node/pull/37377)
  * refactor fs docs structure (James M Snell) [#37170](https://github.com/nodejs/node/pull/37170)
* **fs**:
  * add fsPromises.watch() (James M Snell) [#37179](https://github.com/nodejs/node/pull/37179)
  * use a default callback for fs.close() (James M Snell) [#37174](https://github.com/nodejs/node/pull/37174)
  * add AbortSignal support to watch (Benjamin Gruenbaum) [#37190](https://github.com/nodejs/node/pull/37190)
* **perf\_hooks**:
  * introduce createHistogram (James M Snell) [#37155](https://github.com/nodejs/node/pull/37155)
* **stream**:
  * improve Readable.from error handling (Benjamin Gruenbaum) [#37158](https://github.com/nodejs/node/pull/37158)
* **timers**:
  * introduce setInterval async iterator (linkgoron) [#37153](https://github.com/nodejs/node/pull/37153)
* **tls**:
  * add ability to get cert/peer cert as X509Certificate object (James M Snell) [#37070](https://github.com/nodejs/node/pull/37070)

### Commits

* \[[`d0f1ff53ff`](https://github.com/nodejs/node/commit/d0f1ff53ff)] - **async\_hooks**: set unhandledRejection async context (Sajal Khandelwal) [#37281](https://github.com/nodejs/node/pull/37281)
* \[[`c160d88c9e`](https://github.com/nodejs/node/commit/c160d88c9e)] - **buffer**: add @@toStringTag to Blob (Colin Ihrig) [#37336](https://github.com/nodejs/node/pull/37336)
* \[[`8487184457`](https://github.com/nodejs/node/commit/8487184457)] - **child\_process**: fix bad abort signal leak (Nitzan Uziely) [#37257](https://github.com/nodejs/node/pull/37257)
* \[[`e28ea89b1a`](https://github.com/nodejs/node/commit/e28ea89b1a)] - **crypto**: fix subtle.importKey JWK OKP public key import (Filip Skokan) [#37255](https://github.com/nodejs/node/pull/37255)
* \[[`55fd6b6611`](https://github.com/nodejs/node/commit/55fd6b6611)] - **crypto**: avoid infinite loops in prime generation (Tobias Nießen) [#37212](https://github.com/nodejs/node/pull/37212)
* \[[`9dac99a11a`](https://github.com/nodejs/node/commit/9dac99a11a)] - **crypto**: fix and simplify prime option validation (Tobias Nießen) [#37164](https://github.com/nodejs/node/pull/37164)
* \[[`3e2746ff63`](https://github.com/nodejs/node/commit/3e2746ff63)] - **crypto**: remove webcrypto "DSA" JWK Key Type operations (Filip Skokan) [#37203](https://github.com/nodejs/node/pull/37203)
* \[[`011910b424`](https://github.com/nodejs/node/commit/011910b424)] - **(SEMVER-MINOR)** **crypto**: add keyObject.export() 'jwk' format option (Filip Skokan) [#37081](https://github.com/nodejs/node/pull/37081)
* \[[`c0eadef495`](https://github.com/nodejs/node/commit/c0eadef495)] - **deps**: upgrade to libuv 1.41.0 (Colin Ihrig) [#37360](https://github.com/nodejs/node/pull/37360)
* \[[`50e81ba0b8`](https://github.com/nodejs/node/commit/50e81ba0b8)] - **deps**: V8: cherry-pick 0c8b6e415c30 (Matin Zadehdolatabad) [#37276](https://github.com/nodejs/node/pull/37276)
* \[[`d1c1724c69`](https://github.com/nodejs/node/commit/d1c1724c69)] - **deps**: upgrade npm to 7.5.3 (Ruy Adorno) [#37283](https://github.com/nodejs/node/pull/37283)
* \[[`20c65b00c2`](https://github.com/nodejs/node/commit/20c65b00c2)] - **deps**: V8: backport dfcf1e86fac0 (Michaël Zasso) [#37245](https://github.com/nodejs/node/pull/37245)
* \[[`e63b380f76`](https://github.com/nodejs/node/commit/e63b380f76)] - **deps**: upgrade npm to 7.5.2 (Ruy Adorno) [#37191](https://github.com/nodejs/node/pull/37191)
* \[[`d808db2732`](https://github.com/nodejs/node/commit/d808db2732)] - **doc**: add dmabupt to collaborators (Xu Meng) [#37377](https://github.com/nodejs/node/pull/37377)
* \[[`dd054ca37f`](https://github.com/nodejs/node/commit/dd054ca37f)] - **doc**: optimize HTML rendering (Antoine du Hamel) [#37301](https://github.com/nodejs/node/pull/37301)
* \[[`c188466a18`](https://github.com/nodejs/node/commit/c188466a18)] - **doc**: fix quotes in stream docs (Tobias Nießen) [#37269](https://github.com/nodejs/node/pull/37269)
* \[[`f5e4625468`](https://github.com/nodejs/node/commit/f5e4625468)] - **doc**: fix backticks in crypto API docs (Tobias Nießen) [#37269](https://github.com/nodejs/node/pull/37269)
* \[[`e2a2bab44e`](https://github.com/nodejs/node/commit/e2a2bab44e)] - **doc**: link PACKAGE\_EXPORTS\_RESOLVE to ESM section (Utku Gultopu) [#37135](https://github.com/nodejs/node/pull/37135)
* \[[`1e99175e01`](https://github.com/nodejs/node/commit/1e99175e01)] - **doc**: alphabetize crypto.\* methods (Rich Trott) [#37353](https://github.com/nodejs/node/pull/37353)
* \[[`392c86d38b`](https://github.com/nodejs/node/commit/392c86d38b)] - **doc**: use sentence case in benchmark doc (Rich Trott) [#37351](https://github.com/nodejs/node/pull/37351)
* \[[`62b2648a96`](https://github.com/nodejs/node/commit/62b2648a96)] - **doc**: apply sentence-consistently in C++ style guide (Rich Trott) [#37350](https://github.com/nodejs/node/pull/37350)
* \[[`189ce399da`](https://github.com/nodejs/node/commit/189ce399da)] - **doc**: apply sentence case to release doc headers (Rich Trott) [#37349](https://github.com/nodejs/node/pull/37349)
* \[[`610b29b8bd`](https://github.com/nodejs/node/commit/610b29b8bd)] - **doc**: fix performanceEntry.flags style format (Cheng Liu) [#37274](https://github.com/nodejs/node/pull/37274)
* \[[`85b1476f1d`](https://github.com/nodejs/node/commit/85b1476f1d)] - **doc**: fix typo in deprecations.md (marsonya) [#37282](https://github.com/nodejs/node/pull/37282)
* \[[`f253cb9303`](https://github.com/nodejs/node/commit/f253cb9303)] - **doc**: fix typo in buffer.md (marsonya) [#37268](https://github.com/nodejs/node/pull/37268)
* \[[`804e7ae713`](https://github.com/nodejs/node/commit/804e7ae713)] - **doc**: add version metadata for packages features (Antoine du Hamel) [#37289](https://github.com/nodejs/node/pull/37289)
* \[[`cdd2fe5651`](https://github.com/nodejs/node/commit/cdd2fe5651)] - **doc**: fix typo in /api/dns.md (marsonya) [#37312](https://github.com/nodejs/node/pull/37312)
* \[[`7d8fd3f576`](https://github.com/nodejs/node/commit/7d8fd3f576)] - **doc**: refactor fs docs structure (James M Snell) [#37170](https://github.com/nodejs/node/pull/37170)
* \[[`facf3a5c23`](https://github.com/nodejs/node/commit/facf3a5c23)] - **doc**: fix description of hasSubscribers (Tobias Nießen) [#37324](https://github.com/nodejs/node/pull/37324)
* \[[`3464c9f007`](https://github.com/nodejs/node/commit/3464c9f007)] - **doc**: discourage error event (Benjamin Gruenbaum) [#37264](https://github.com/nodejs/node/pull/37264)
* \[[`85bed2ec26`](https://github.com/nodejs/node/commit/85bed2ec26)] - **doc**: fix misnamed SHASUMS256.txt name in README.md (marsonya) [#37260](https://github.com/nodejs/node/pull/37260)
* \[[`cd50e93307`](https://github.com/nodejs/node/commit/cd50e93307)] - **doc**: warn about using strings as inputs in crypto (Tobias Nießen) [#37248](https://github.com/nodejs/node/pull/37248)
* \[[`5a4288ebb6`](https://github.com/nodejs/node/commit/5a4288ebb6)] - **doc**: fix typo in crypto.md (marsonya) [#37279](https://github.com/nodejs/node/pull/37279)
* \[[`0e887caf32`](https://github.com/nodejs/node/commit/0e887caf32)] - **doc**: fix typo in console.md (marsonya) [#37279](https://github.com/nodejs/node/pull/37279)
* \[[`47c4f1fc54`](https://github.com/nodejs/node/commit/47c4f1fc54)] - **doc**: use sentence case in README headers (Rich Trott) [#37251](https://github.com/nodejs/node/pull/37251)
* \[[`7da1c9b219`](https://github.com/nodejs/node/commit/7da1c9b219)] - **doc**: use sentence case for headers in BUILDING.md (Rich Trott) [#37250](https://github.com/nodejs/node/pull/37250)
* \[[`ebf3597db1`](https://github.com/nodejs/node/commit/ebf3597db1)] - **doc**: rename N-API to Node-API (Gabriel Schulhof) [#37259](https://github.com/nodejs/node/pull/37259)
* \[[`760f126adb`](https://github.com/nodejs/node/commit/760f126adb)] - **doc**: mark Certificate methods as static, add missing KeyObject.from (Filip Skokan) [#37198](https://github.com/nodejs/node/pull/37198)
* \[[`aebe532967`](https://github.com/nodejs/node/commit/aebe532967)] - **doc**: consistent webcrypto `node.keyObject` format (Filip Skokan) [#37200](https://github.com/nodejs/node/pull/37200)
* \[[`596bfb36a0`](https://github.com/nodejs/node/commit/596bfb36a0)] - **doc**: mention CryptoKey in port.postMessage() (Filip Skokan) [#37196](https://github.com/nodejs/node/pull/37196)
* \[[`0702d60def`](https://github.com/nodejs/node/commit/0702d60def)] - **doc**: fix webcrypto HMAC generateKey example (Filip Skokan) [#37197](https://github.com/nodejs/node/pull/37197)
* \[[`8a254058f5`](https://github.com/nodejs/node/commit/8a254058f5)] - **doc**: fix accommodate typos (Colin Ihrig) [#37229](https://github.com/nodejs/node/pull/37229)
* \[[`5906e85ce2`](https://github.com/nodejs/node/commit/5906e85ce2)] - **doc**: fix version number for DEP006 (Antoine du Hamel) [#37231](https://github.com/nodejs/node/pull/37231)
* \[[`52c40c7a48`](https://github.com/nodejs/node/commit/52c40c7a48)] - **doc**: fix CHANGELOG\_ARCHIVE table of contents (Antoine du Hamel) [#37232](https://github.com/nodejs/node/pull/37232)
* \[[`eb08afdf24`](https://github.com/nodejs/node/commit/eb08afdf24)] - **doc**: fix typo in globals.md (Darshan Sen) [#37228](https://github.com/nodejs/node/pull/37228)
* \[[`b87c0d6c16`](https://github.com/nodejs/node/commit/b87c0d6c16)] - **doc**: fix typo in cli.md (Kalvin Vasconcellos) [#37214](https://github.com/nodejs/node/pull/37214)
* \[[`3f815d93bf`](https://github.com/nodejs/node/commit/3f815d93bf)] - **doc**: fix pr-url for DEP0148 (Antoine du Hamel) [#37205](https://github.com/nodejs/node/pull/37205)
* \[[`ff02e5e12c`](https://github.com/nodejs/node/commit/ff02e5e12c)] - **doc**: fix 404 links in module.md (Antoine du Hamel) [#37202](https://github.com/nodejs/node/pull/37202)
* \[[`67c9a8e176`](https://github.com/nodejs/node/commit/67c9a8e176)] - **doc**: improve promise terminology (Benjamin Gruenbaum) [#37181](https://github.com/nodejs/node/pull/37181)
* \[[`15804e0b3f`](https://github.com/nodejs/node/commit/15804e0b3f)] - **errors**: align source-map stacks with spec (Benjamin Coe) [#37252](https://github.com/nodejs/node/pull/37252)
* \[[`88d3f74c85`](https://github.com/nodejs/node/commit/88d3f74c85)] - **(SEMVER-MINOR)** **fs**: add fsPromises.watch() (James M Snell) [#37179](https://github.com/nodejs/node/pull/37179)
* \[[`c30245072a`](https://github.com/nodejs/node/commit/c30245072a)] - **fs**: allow passing negative zero fd (Darshan Sen) [#37123](https://github.com/nodejs/node/pull/37123)
* \[[`655d19638a`](https://github.com/nodejs/node/commit/655d19638a)] - **(SEMVER-MINOR)** **fs**: use a default callback for fs.close() (James M Snell) [#37174](https://github.com/nodejs/node/pull/37174)
* \[[`acd087dffb`](https://github.com/nodejs/node/commit/acd087dffb)] - **(SEMVER-MINOR)** **fs**: add AbortSignal support to watch (Benjamin Gruenbaum) [#37190](https://github.com/nodejs/node/pull/37190)
* \[[`f5d1bf9d0e`](https://github.com/nodejs/node/commit/f5d1bf9d0e)] - **http**: explain the possibilty of refactor unused argument (Qingyu Deng) [#37275](https://github.com/nodejs/node/pull/37275)
* \[[`d63ac28a9a`](https://github.com/nodejs/node/commit/d63ac28a9a)] - **http**: explain the unused argument in IncomingMessage.\_read (Qingyu Deng) [#37275](https://github.com/nodejs/node/pull/37275)
* \[[`4cdc5ea823`](https://github.com/nodejs/node/commit/4cdc5ea823)] - **http**: fix ClientRequest unhandled errors (Robert Nagy) [#36970](https://github.com/nodejs/node/pull/36970)
* \[[`c6198fddc7`](https://github.com/nodejs/node/commit/c6198fddc7)] - **lib**: simplify check in child\_process (Darshan Sen) [#37367](https://github.com/nodejs/node/pull/37367)
* \[[`f6f9af6a59`](https://github.com/nodejs/node/commit/f6f9af6a59)] - **lib**: fix WebIDL `object` and dictionary type conversion (ExE Boss) [#37047](https://github.com/nodejs/node/pull/37047)
* \[[`acabe08b10`](https://github.com/nodejs/node/commit/acabe08b10)] - **lib**: add weak event handlers (Benjamin Gruenbaum) [#36607](https://github.com/nodejs/node/pull/36607)
* \[[`3db1b30732`](https://github.com/nodejs/node/commit/3db1b30732)] - **meta**: update README releases section (Zuzana Svetlikova) [#37318](https://github.com/nodejs/node/pull/37318)
* \[[`d96a97a2b9`](https://github.com/nodejs/node/commit/d96a97a2b9)] - **module**: make synthetic module evaluation steps return a Promise to support top level await (Daniel Clark) [#37300](https://github.com/nodejs/node/pull/37300)
* \[[`a693baa0cb`](https://github.com/nodejs/node/commit/a693baa0cb)] - **module**: use optional chaining in cjs/loader.js (Darshan Sen) [#37238](https://github.com/nodejs/node/pull/37238)
* \[[`061939d2f6`](https://github.com/nodejs/node/commit/061939d2f6)] - **(SEMVER-MINOR)** **node-api**: allow retrieval of add-on file name (Gabriel Schulhof) [#37195](https://github.com/nodejs/node/pull/37195)
* \[[`c4faa39768`](https://github.com/nodejs/node/commit/c4faa39768)] - **(SEMVER-MINOR)** **perf\_hooks**: introduce createHistogram (James M Snell) [#37155](https://github.com/nodejs/node/pull/37155)
* \[[`799b2d5275`](https://github.com/nodejs/node/commit/799b2d5275)] - **policy**: fix cascade getting scope (Bradley Meck) [#37298](https://github.com/nodejs/node/pull/37298)
* \[[`6d53e797d7`](https://github.com/nodejs/node/commit/6d53e797d7)] - **repl**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#37345](https://github.com/nodejs/node/pull/37345)
* \[[`3fee5b2219`](https://github.com/nodejs/node/commit/3fee5b2219)] - **repl**: add auto‑completion for dynamic import calls (ExE Boss) [#37178](https://github.com/nodejs/node/pull/37178)
* \[[`c3778343aa`](https://github.com/nodejs/node/commit/c3778343aa)] - **repl**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#37188](https://github.com/nodejs/node/pull/37188)
* \[[`e28fa6c3fc`](https://github.com/nodejs/node/commit/e28fa6c3fc)] - **src**: fix return type of method in string\_search.h (Darshan Sen) [#37167](https://github.com/nodejs/node/pull/37167)
* \[[`42cc33cc48`](https://github.com/nodejs/node/commit/42cc33cc48)] - **src**: add mutex to ManagedEVPPKey class (Daniel Bevenius) [#36825](https://github.com/nodejs/node/pull/36825)
* \[[`1a9bcdf1d9`](https://github.com/nodejs/node/commit/1a9bcdf1d9)] - **src**: refactor v8 binding (Joyee Cheung) [#37112](https://github.com/nodejs/node/pull/37112)
* \[[`54d36b00af`](https://github.com/nodejs/node/commit/54d36b00af)] - **src**: rename binding\_data\_name to type\_name in BindingData (Joyee Cheung) [#37112](https://github.com/nodejs/node/pull/37112)
* \[[`3079a78428`](https://github.com/nodejs/node/commit/3079a78428)] - **src**: avoid implicit type conversions (Michaël Zasso) [#37149](https://github.com/nodejs/node/pull/37149)
* \[[`a6053dc14a`](https://github.com/nodejs/node/commit/a6053dc14a)] - **src**: add context for TODO comment in env.cc (Yash Ladha) [#37140](https://github.com/nodejs/node/pull/37140)
* \[[`354df9e8a1`](https://github.com/nodejs/node/commit/354df9e8a1)] - **src**: use make\_shared for safe allocation (Yash Ladha) [#37139](https://github.com/nodejs/node/pull/37139)
* \[[`337b4e7540`](https://github.com/nodejs/node/commit/337b4e7540)] - **src**: put (de)serialization code into node\_snapshotable.h/cc (Joyee Cheung) [#37114](https://github.com/nodejs/node/pull/37114)
* \[[`2a5f67b381`](https://github.com/nodejs/node/commit/2a5f67b381)] - **src**: refactor bookkeeping of bootstrap status (Joyee Cheung) [#37113](https://github.com/nodejs/node/pull/37113)
* \[[`48ce1eb364`](https://github.com/nodejs/node/commit/48ce1eb364)] - **src**: fix warning in string\_search.h (Darshan Sen) [#37146](https://github.com/nodejs/node/pull/37146)
* \[[`bfe0b46d92`](https://github.com/nodejs/node/commit/bfe0b46d92)] - **src**: simplify calls to BN\_bin2bn in prime gen (Tobias Nießen) [#37169](https://github.com/nodejs/node/pull/37169)
* \[[`9946c1137e`](https://github.com/nodejs/node/commit/9946c1137e)] - **src**: read exactly two tokens from Linux THP sysfs config (James Addison) [#37065](https://github.com/nodejs/node/pull/37065)
* \[[`1fea05149a`](https://github.com/nodejs/node/commit/1fea05149a)] - **(SEMVER-MINOR)** **stream**: improve Readable.from error handling (Benjamin Gruenbaum) [#37158](https://github.com/nodejs/node/pull/37158)
* \[[`d2a487e640`](https://github.com/nodejs/node/commit/d2a487e640)] - _**Revert**_ "**stream**: fix .end() error propagation" (Matteo Collina) [#37060](https://github.com/nodejs/node/pull/37060)
* \[[`b5692b4b06`](https://github.com/nodejs/node/commit/b5692b4b06)] - **test**: fix test-doctool-html (Antoine du Hamel) [#37397](https://github.com/nodejs/node/pull/37397)
* \[[`b09d21b06b`](https://github.com/nodejs/node/commit/b09d21b06b)] - **test**: enable no-restricted-syntax rule for test-timers-promisified (Rich Trott) [#37357](https://github.com/nodejs/node/pull/37357)
* \[[`1fc8307138`](https://github.com/nodejs/node/commit/1fc8307138)] - **test**: re-implement promises.setInterval() test robustly (Rich Trott) [#37230](https://github.com/nodejs/node/pull/37230)
* \[[`8483de4da8`](https://github.com/nodejs/node/commit/8483de4da8)] - **test**: only run prime test with supported OpenSSL (Tobias Nießen) [#37212](https://github.com/nodejs/node/pull/37212)
* \[[`48a634e514`](https://github.com/nodejs/node/commit/48a634e514)] - **test**: rename n-api to node-api (Gabriel Schulhof) [#37217](https://github.com/nodejs/node/pull/37217)
* \[[`51575252f5`](https://github.com/nodejs/node/commit/51575252f5)] - **test**: remove flaky designation for test-http2-large-file (Rich Trott) [#37156](https://github.com/nodejs/node/pull/37156)
* \[[`13fe17c4ef`](https://github.com/nodejs/node/commit/13fe17c4ef)] - **test**: split heap snapshot limit tests (Rich Trott) [#37189](https://github.com/nodejs/node/pull/37189)
* \[[`dc38dd2c6f`](https://github.com/nodejs/node/commit/dc38dd2c6f)] - **timers**: fix unsafe array iteration (Darshan Sen) [#37223](https://github.com/nodejs/node/pull/37223)
* \[[`eb7ec1b257`](https://github.com/nodejs/node/commit/eb7ec1b257)] - **timers**: remove flaky setInterval test (Nitzan Uziely) [#37227](https://github.com/nodejs/node/pull/37227)
* \[[`4ebe38b212`](https://github.com/nodejs/node/commit/4ebe38b212)] - **(SEMVER-MINOR)** **timers**: introduce setInterval async iterator (linkgoron) [#37153](https://github.com/nodejs/node/pull/37153)
* \[[`dc84c181c3`](https://github.com/nodejs/node/commit/dc84c181c3)] - **(SEMVER-MINOR)** **tls**: add ability to get cert/peer cert as X509Certificate object (James M Snell) [#37070](https://github.com/nodejs/node/pull/37070)
* \[[`2e1f1c6f3c`](https://github.com/nodejs/node/commit/2e1f1c6f3c)] - **tools**: refactor prefer-primordials (Antoine du Hamel) [#36018](https://github.com/nodejs/node/pull/36018)
* \[[`b2b64113b1`](https://github.com/nodejs/node/commit/b2b64113b1)] - **tools**: update ESLint to 7.20.0 (Colin Ihrig) [#37339](https://github.com/nodejs/node/pull/37339)
* \[[`a483c284f3`](https://github.com/nodejs/node/commit/a483c284f3)] - **tools**: fix lint-pr-url message (Antoine du Hamel) [#37304](https://github.com/nodejs/node/pull/37304)
* \[[`1ff375beb3`](https://github.com/nodejs/node/commit/1ff375beb3)] - **tools**: avoid pending deprecation in doc generator (Michaël Zasso) [#37267](https://github.com/nodejs/node/pull/37267)
* \[[`6db5e7958a`](https://github.com/nodejs/node/commit/6db5e7958a)] - **tools**: add GitHub Action linter for pr-url (Antoine du Hamel) [#37221](https://github.com/nodejs/node/pull/37221)
* \[[`d8d851ac5c`](https://github.com/nodejs/node/commit/d8d851ac5c)] - **tools**: bump remark-present-lint-node from 2.0.0 to 2.0.1 (Rich Trott) [#37270](https://github.com/nodejs/node/pull/37270)
* \[[`eb0daaedf9`](https://github.com/nodejs/node/commit/eb0daaedf9)] - **tools**: fix d8 macOS build (Michaël Zasso) [#37211](https://github.com/nodejs/node/pull/37211)
* \[[`745aad73dc`](https://github.com/nodejs/node/commit/745aad73dc)] - **tools**: update ESLint to 7.19.0 (Colin Ihrig) [#37159](https://github.com/nodejs/node/pull/37159)
* \[[`676f696a99`](https://github.com/nodejs/node/commit/676f696a99)] - **url**: fix definitions of `URL`/`SearchParams` methods and accessors (ExE Boss) [#36799](https://github.com/nodejs/node/pull/36799)
* \[[`fbcab109de`](https://github.com/nodejs/node/commit/fbcab109de)] - **url**: move `URLSearchParams` method definitions (ExE Boss) [#36799](https://github.com/nodejs/node/pull/36799)
* \[[`7c51cecbca`](https://github.com/nodejs/node/commit/7c51cecbca)] - **util**: use assert for unreachable code (Rich Trott) [#37249](https://github.com/nodejs/node/pull/37249)
* \[[`66a14d3992`](https://github.com/nodejs/node/commit/66a14d3992)] - **vm**: add importModuleDynamically option to compileFunction (Gus Caplan) [#35431](https://github.com/nodejs/node/pull/35431)
* \[[`05a16e7259`](https://github.com/nodejs/node/commit/05a16e7259)] - **worker**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#37346](https://github.com/nodejs/node/pull/37346)

<a id="15.8.0"></a>

## 2021-02-02, Version 15.8.0 (Current), @targos

### Notable Changes

* \[[`110063d694`](https://github.com/nodejs/node/commit/110063d694)] - **(SEMVER-MINOR)** **crypto**: add generatePrime/checkPrime (James M Snell) [#36997](https://github.com/nodejs/node/pull/36997)
* \[[`53a0bdff47`](https://github.com/nodejs/node/commit/53a0bdff47)] - **(SEMVER-MINOR)** **crypto**: experimental (Ed/X)25519/(Ed/X)448 support (James M Snell) [#36879](https://github.com/nodejs/node/pull/36879)
* \[[`03460432af`](https://github.com/nodejs/node/commit/03460432af)] - **deps**: upgrade npm to 7.5.0 (Ruy Adorno) [#37117](https://github.com/nodejs/node/pull/37117)
  * This update adds a new [`npm diff` command](https://github.com/npm/cli/pull/1319).
* \[[`2c7ad38c75`](https://github.com/nodejs/node/commit/2c7ad38c75)] - **(SEMVER-MINOR)** **dgram**: support AbortSignal in createSocket (Nitzan Uziely) [#37026](https://github.com/nodejs/node/pull/37026)
* \[[`b7c3f99f7e`](https://github.com/nodejs/node/commit/b7c3f99f7e)] - **doc**: add Zijian Liu to collaborators (ZiJian Liu) [#37075](https://github.com/nodejs/node/pull/37075)
* \[[`02f1d2fda4`](https://github.com/nodejs/node/commit/02f1d2fda4)] - **esm**: deprecate legacy main lookup for modules (Guy Bedford) [#36918](https://github.com/nodejs/node/pull/36918)
* \[[`75124298d5`](https://github.com/nodejs/node/commit/75124298d5)] - **(SEMVER-MINOR)** **readline**: add history event and option to set initial history (Mattias Runge-Broberg) [#33662](https://github.com/nodejs/node/pull/33662)
* \[[`4e757eab96`](https://github.com/nodejs/node/commit/4e757eab96)] - **(SEMVER-MINOR)** **readline**: add support for the AbortController to the question method (Mattias Runge-Broberg) [#33676](https://github.com/nodejs/node/pull/33676)

### Commits

* \[[`602aaf25af`](https://github.com/nodejs/node/commit/602aaf25af)] - **async\_hooks**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#37125](https://github.com/nodejs/node/pull/37125)
* \[[`dcd34b0144`](https://github.com/nodejs/node/commit/dcd34b0144)] - **benchmark**: add benchmark for NODE\_V8\_COVERAGE (Benjamin Coe) [#36972](https://github.com/nodejs/node/pull/36972)
* \[[`ec22756ac9`](https://github.com/nodejs/node/commit/ec22756ac9)] - **benchmark**: make output RFC 4180 compliant (Tobias Nießen) [#37038](https://github.com/nodejs/node/pull/37038)
* \[[`96cec1e5f3`](https://github.com/nodejs/node/commit/96cec1e5f3)] - **benchmark**: improve explanations in R script (Tobias Nießen) [#36995](https://github.com/nodejs/node/pull/36995)
* \[[`e4b88b521a`](https://github.com/nodejs/node/commit/e4b88b521a)] - **buffer**: avoid creating the backing store in the thread (James M Snell) [#37052](https://github.com/nodejs/node/pull/37052)
* \[[`7b78c6773d`](https://github.com/nodejs/node/commit/7b78c6773d)] - **child\_process**: allow promisified exec to be cancel (Carlos Fuentes) [#34249](https://github.com/nodejs/node/pull/34249)
* \[[`c4193ba8ae`](https://github.com/nodejs/node/commit/c4193ba8ae)] - **crypto**: fix encrypted private -> public import (Tobias Nießen) [#37056](https://github.com/nodejs/node/pull/37056)
* \[[`cb3b0ec4fc`](https://github.com/nodejs/node/commit/cb3b0ec4fc)] - **crypto**: generateKeyPair('ec') should not support NODE-ED\* and NODE-X\* (Filip Skokan) [#37063](https://github.com/nodejs/node/pull/37063)
* \[[`110063d694`](https://github.com/nodejs/node/commit/110063d694)] - **(SEMVER-MINOR)** **crypto**: add generatePrime/checkPrime (James M Snell) [#36997](https://github.com/nodejs/node/pull/36997)
* \[[`ab64d74791`](https://github.com/nodejs/node/commit/ab64d74791)] - **crypto**: throw error on invalid object in diffieHellman() (ZiJian Liu) [#37016](https://github.com/nodejs/node/pull/37016)
* \[[`53a0bdff47`](https://github.com/nodejs/node/commit/53a0bdff47)] - **(SEMVER-MINOR)** **crypto**: experimental (Ed/X)25519/(Ed/X)448 support (James M Snell) [#36879](https://github.com/nodejs/node/pull/36879)
* \[[`4551d14b8e`](https://github.com/nodejs/node/commit/4551d14b8e)] - **deps**: upgrade npm to 7.5.1 (Ruy Adorno) [#37177](https://github.com/nodejs/node/pull/37177)
* \[[`9d6fd4586f`](https://github.com/nodejs/node/commit/9d6fd4586f)] - **deps**: update openssl config (James M Snell) [#37067](https://github.com/nodejs/node/pull/37067)
* \[[`f74b376596`](https://github.com/nodejs/node/commit/f74b376596)] - _**Revert**_ "**deps**: various quic patches from akamai/openssl" (James M Snell) [#37067](https://github.com/nodejs/node/pull/37067)
* \[[`6756130c4b`](https://github.com/nodejs/node/commit/6756130c4b)] - _**Revert**_ "**deps**: re-enable OPENSSL\_NO\_QUIC guards" (James M Snell) [#37067](https://github.com/nodejs/node/pull/37067)
* \[[`52ce1d5f1a`](https://github.com/nodejs/node/commit/52ce1d5f1a)] - _**Revert**_ "**deps**: update patch and docs for openssl update" (James M Snell) [#37067](https://github.com/nodejs/node/pull/37067)
* \[[`03460432af`](https://github.com/nodejs/node/commit/03460432af)] - **deps**: upgrade npm to 7.5.0 (Ruy Adorno) [#37117](https://github.com/nodejs/node/pull/37117)
* \[[`2c7ad38c75`](https://github.com/nodejs/node/commit/2c7ad38c75)] - **(SEMVER-MINOR)** **dgram**: support AbortSignal in createSocket (Nitzan Uziely) [#37026](https://github.com/nodejs/node/pull/37026)
* \[[`47bfde00fd`](https://github.com/nodejs/node/commit/47bfde00fd)] - **doc**: fix color contrast on \<kbd> elements (Antoine du Hamel) [#37185](https://github.com/nodejs/node/pull/37185)
* \[[`3c9077130d`](https://github.com/nodejs/node/commit/3c9077130d)] - **doc**: fix list format in Developer's Certificate of Origin (Akash Negi) [#37138](https://github.com/nodejs/node/pull/37138)
* \[[`8cecce3ff4`](https://github.com/nodejs/node/commit/8cecce3ff4)] - **doc**: fix markup and alphabetization in errors.md (Rich Trott) [#37144](https://github.com/nodejs/node/pull/37144)
* \[[`a7780815bf`](https://github.com/nodejs/node/commit/a7780815bf)] - **doc**: clarify ERR\_INVALID\_REPL\_INPUT usage (Rich Trott) [#37143](https://github.com/nodejs/node/pull/37143)
* \[[`e7126503e0`](https://github.com/nodejs/node/commit/e7126503e0)] - **doc**: clarify repl exception conditions (Rich Trott) [#37142](https://github.com/nodejs/node/pull/37142)
* \[[`e55d3d0953`](https://github.com/nodejs/node/commit/e55d3d0953)] - **doc**: add example for test structure (Turner Jabbour) [#35046](https://github.com/nodejs/node/pull/35046)
* \[[`9b9a1801ba`](https://github.com/nodejs/node/commit/9b9a1801ba)] - **doc**: remove TOC summary for pages with no TOC (Rich Trott) [#37043](https://github.com/nodejs/node/pull/37043)
* \[[`ae42658be9`](https://github.com/nodejs/node/commit/ae42658be9)] - **doc**: add missing deprecation code (Colin Ihrig) [#37147](https://github.com/nodejs/node/pull/37147)
* \[[`b79b82de8e`](https://github.com/nodejs/node/commit/b79b82de8e)] - **doc**: update Buffer encoding option count (Dave Cardwell) [#37102](https://github.com/nodejs/node/pull/37102)
* \[[`ddee21b587`](https://github.com/nodejs/node/commit/ddee21b587)] - **doc**: update BUILDING.md previous versions links (Richard Lau) [#37082](https://github.com/nodejs/node/pull/37082)
* \[[`1710016053`](https://github.com/nodejs/node/commit/1710016053)] - **doc**: mention adding Fixes to collaborator onboarding PR (Joyee Cheung) [#37097](https://github.com/nodejs/node/pull/37097)
* \[[`b7c3f99f7e`](https://github.com/nodejs/node/commit/b7c3f99f7e)] - **doc**: add Zijian Liu to collaborators (ZiJian Liu) [#37075](https://github.com/nodejs/node/pull/37075)
* \[[`7ddfa81612`](https://github.com/nodejs/node/commit/7ddfa81612)] - **doc**: add tooltip for light/dark mode toggle (Rich Trott) [#37044](https://github.com/nodejs/node/pull/37044)
* \[[`c79688ffe3`](https://github.com/nodejs/node/commit/c79688ffe3)] - **doc**: improve AsyncLocalStorage introduction (Romuald Brillout) [#36946](https://github.com/nodejs/node/pull/36946)
* \[[`a7b6464097`](https://github.com/nodejs/node/commit/a7b6464097)] - **doc**: `EventTarget` and `Event` are available to user code since v15.0.0 (ExE Boss) [#37059](https://github.com/nodejs/node/pull/37059)
* \[[`3722c15a75`](https://github.com/nodejs/node/commit/3722c15a75)] - **doc**: add missing comma in tty (Matthew Mario Di Pasquale) [#37039](https://github.com/nodejs/node/pull/37039)
* \[[`2cfe7954fc`](https://github.com/nodejs/node/commit/2cfe7954fc)] - **doc**: list Unsupported Directory Import resolve err (Guy Bedford) [#37032](https://github.com/nodejs/node/pull/37032)
* \[[`fef6ac77e5`](https://github.com/nodejs/node/commit/fef6ac77e5)] - **doc**: add missing ARIA label for button (Rich Trott) [#37031](https://github.com/nodejs/node/pull/37031)
* \[[`634bedcd6f`](https://github.com/nodejs/node/commit/634bedcd6f)] - **doc,test**: fix prime generation description (Tobias Nießen) [#37085](https://github.com/nodejs/node/pull/37085)
* \[[`181719d4c4`](https://github.com/nodejs/node/commit/181719d4c4)] - **esm**: update to correct deprecation code (Colin Ihrig) [#37147](https://github.com/nodejs/node/pull/37147)
* \[[`02f1d2fda4`](https://github.com/nodejs/node/commit/02f1d2fda4)] - **esm**: deprecate legacy main lookup for modules (Guy Bedford) [#36918](https://github.com/nodejs/node/pull/36918)
* \[[`69402522fd`](https://github.com/nodejs/node/commit/69402522fd)] - **fs**: read full size if known in promises.readFile (Anna Henningsen) [#37127](https://github.com/nodejs/node/pull/37127)
* \[[`ad12fefcb0`](https://github.com/nodejs/node/commit/ad12fefcb0)] - **fs**: only use Buffer.concat in promises.readFile when necessary (Anna Henningsen) [#37127](https://github.com/nodejs/node/pull/37127)
* \[[`6f54a14cda`](https://github.com/nodejs/node/commit/6f54a14cda)] - **fs**: add validatePosition and use in read and readSync (Darshan Sen) [#37051](https://github.com/nodejs/node/pull/37051)
* \[[`175f6f0be3`](https://github.com/nodejs/node/commit/175f6f0be3)] - **fs**: use throwIfNoEntry option on statSync calls (Antoine du Hamel) [#36975](https://github.com/nodejs/node/pull/36975)
* \[[`97fc7d8396`](https://github.com/nodejs/node/commit/97fc7d8396)] - **fs**: refactor to remove redundant validation (Darshan Sen) [#36984](https://github.com/nodejs/node/pull/36984)
* \[[`0129a79d0a`](https://github.com/nodejs/node/commit/0129a79d0a)] - **fs**: add explicit note about undefined path when recursive (Sebastian Silbermann) [#37010](https://github.com/nodejs/node/pull/37010)
* \[[`7196ac19c1`](https://github.com/nodejs/node/commit/7196ac19c1)] - **http**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#37124](https://github.com/nodejs/node/pull/37124)
* \[[`ed58065d1f`](https://github.com/nodejs/node/commit/ed58065d1f)] - **lib**: add `bound apply` variants of varargs `primordials` (ExE Boss) [#37005](https://github.com/nodejs/node/pull/37005)
* \[[`67b58f68c9`](https://github.com/nodejs/node/commit/67b58f68c9)] - **lib**: refactor to use validateObject (ZiJian Liu) [#37028](https://github.com/nodejs/node/pull/37028)
* \[[`5227c5e6f5`](https://github.com/nodejs/node/commit/5227c5e6f5)] - **lib**: refactor to use validateFunction (ZiJian Liu) [#37045](https://github.com/nodejs/node/pull/37045)
* \[[`34adf7f74b`](https://github.com/nodejs/node/commit/34adf7f74b)] - **lib**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#37029](https://github.com/nodejs/node/pull/37029)
* \[[`4a1fc42178`](https://github.com/nodejs/node/commit/4a1fc42178)] - **lib**: refactor to use optional chaining in internal/options.js (raisinten) [#36939](https://github.com/nodejs/node/pull/36939)
* \[[`d76400a264`](https://github.com/nodejs/node/commit/d76400a264)] - **lib**: refactor to use validateString (ZiJian Liu) [#37006](https://github.com/nodejs/node/pull/37006)
* \[[`a29da64b46`](https://github.com/nodejs/node/commit/a29da64b46)] - **lib**: refactor to use validateNumber (ZiJian Liu) [#36993](https://github.com/nodejs/node/pull/36993)
* \[[`56377d6cee`](https://github.com/nodejs/node/commit/56377d6cee)] - **lib**: support returning Safe collections from C++ (ExE Boss) [#36989](https://github.com/nodejs/node/pull/36989)
* \[[`c4cab1f408`](https://github.com/nodejs/node/commit/c4cab1f408)] - **lib**: refactor to use validateBoolean (ZiJian Liu) [#36983](https://github.com/nodejs/node/pull/36983)
* \[[`11dd2672cd`](https://github.com/nodejs/node/commit/11dd2672cd)] - **quic**: remove quic (James M Snell) [#37067](https://github.com/nodejs/node/pull/37067)
* \[[`b533485f32`](https://github.com/nodejs/node/commit/b533485f32)] - **quic**: remove duplicate checks (ZiJian Liu) [#37017](https://github.com/nodejs/node/pull/37017)
* \[[`1714998e2c`](https://github.com/nodejs/node/commit/1714998e2c)] - **readline**: replace \_questionCancel with a symbol (Colin Ihrig) [#37094](https://github.com/nodejs/node/pull/37094)
* \[[`3d64d2b5ef`](https://github.com/nodejs/node/commit/3d64d2b5ef)] - **readline**: check for null input in question() (Colin Ihrig) [#37089](https://github.com/nodejs/node/pull/37089)
* \[[`75124298d5`](https://github.com/nodejs/node/commit/75124298d5)] - **(SEMVER-MINOR)** **readline**: add history event and option to set initial history (Mattias Runge-Broberg) [#33662](https://github.com/nodejs/node/pull/33662)
* \[[`4e757eab96`](https://github.com/nodejs/node/commit/4e757eab96)] - **(SEMVER-MINOR)** **readline**: add support for the AbortController to the question method (Mattias Runge-Broberg) [#33676](https://github.com/nodejs/node/pull/33676)
* \[[`a26dfb323b`](https://github.com/nodejs/node/commit/a26dfb323b)] - **src**: expose BaseObject::kInternalFieldCount in post-mortem metadata (Joyee Cheung) [#37111](https://github.com/nodejs/node/pull/37111)
* \[[`9c831c0d8f`](https://github.com/nodejs/node/commit/9c831c0d8f)] - **src**: fix dead code in RandomPrimeTraits (Tobias Nießen) [#37083](https://github.com/nodejs/node/pull/37083)
* \[[`81e9acf242`](https://github.com/nodejs/node/commit/81e9acf242)] - **src**: rename crypto\_ecdh.(h|cc) to crypto\_ec.(h|cc) (Tobias Nießen) [#37048](https://github.com/nodejs/node/pull/37048)
* \[[`1f819ec47d`](https://github.com/nodejs/node/commit/1f819ec47d)] - **test**: add tests for `bound apply` variants of varargs `primordials` (ExE Boss) [#37005](https://github.com/nodejs/node/pull/37005)
* \[[`db38cf27c2`](https://github.com/nodejs/node/commit/db38cf27c2)] - **test**: increase inspect coverage (Emil Sivervik) [#36755](https://github.com/nodejs/node/pull/36755)
* \[[`10da5c1104`](https://github.com/nodejs/node/commit/10da5c1104)] - **test**: skip tests consistently in parallel.status (Rich Trott) [#37035](https://github.com/nodejs/node/pull/37035)
* \[[`da07eb654e`](https://github.com/nodejs/node/commit/da07eb654e)] - **test**: increase read file abort coverage (Moshe vilner) [#36716](https://github.com/nodejs/node/pull/36716)
* \[[`55407b826f`](https://github.com/nodejs/node/commit/55407b826f)] - **test**: update to improve terminology (Michael Dawson) [#37011](https://github.com/nodejs/node/pull/37011)
* \[[`ef2b25088d`](https://github.com/nodejs/node/commit/ef2b25088d)] - **test**: increase coverage for assert/calltracker (ZiJian Liu) [#36728](https://github.com/nodejs/node/pull/36728)
* \[[`074641c2e9`](https://github.com/nodejs/node/commit/074641c2e9)] - **test**: improve assertion message for test-vm-memleak (Rich Trott) [#37034](https://github.com/nodejs/node/pull/37034)
* \[[`4086b230b8`](https://github.com/nodejs/node/commit/4086b230b8)] - **test**: increase fs promise coverage (Emil Sivervik) [#36813](https://github.com/nodejs/node/pull/36813)
* \[[`94204f7e46`](https://github.com/nodejs/node/commit/94204f7e46)] - **test**: process.nextTick for before exit (ttzztztz) [#37012](https://github.com/nodejs/node/pull/37012)
* \[[`2135618052`](https://github.com/nodejs/node/commit/2135618052)] - **test**: increase timeout on ASAN Action (Antoine du Hamel) [#37007](https://github.com/nodejs/node/pull/37007)
* \[[`de6dca12e8`](https://github.com/nodejs/node/commit/de6dca12e8)] - **test**: improve coverage of `SourceTextModule` getters (Juan José Arboleda) [#37013](https://github.com/nodejs/node/pull/37013)
* \[[`36cc8df358`](https://github.com/nodejs/node/commit/36cc8df358)] - **test**: log error in test-fs-realpath-pipe (Joyee Cheung) [#36996](https://github.com/nodejs/node/pull/36996)
* \[[`36930e4fe7`](https://github.com/nodejs/node/commit/36930e4fe7)] - **test**: test mode passed as an options object in mkdir/mkdirSync (Darshan Sen) [#37008](https://github.com/nodejs/node/pull/37008)
* \[[`9c69ca5e54`](https://github.com/nodejs/node/commit/9c69ca5e54)] - **test,doc,lib**: adjust object literal newlines for lint rule (Rich Trott) [#37040](https://github.com/nodejs/node/pull/37040)
* \[[`fe9f4fdba5`](https://github.com/nodejs/node/commit/fe9f4fdba5)] - **tools**: remove commented code from stability.js (Colin Ihrig) [#37092](https://github.com/nodejs/node/pull/37092)
* \[[`d2d6121f3e`](https://github.com/nodejs/node/commit/d2d6121f3e)] - **tools**: enable object-curly-newline in ESLint rules (Rich Trott) [#37040](https://github.com/nodejs/node/pull/37040)
* \[[`3187845980`](https://github.com/nodejs/node/commit/3187845980)] - **util**: add internal createDeferredPromise() (Colin Ihrig) [#37095](https://github.com/nodejs/node/pull/37095)

<a id="15.7.0"></a>

## 2021-01-26, Version 15.7.0 (Current), @ruyadorno

### Notable changes

* **buffer**:
  * introduce Blob (James M Snell) [#36811](https://github.com/nodejs/node/pull/36811)
  * add base64url encoding option (Filip Skokan) [#36952](https://github.com/nodejs/node/pull/36952)
* **doc**:
  * add @iansu to collaborators (Ian Sutherland) [#36951](https://github.com/nodejs/node/pull/36951)
  * add @RaisinTen to collaborators (Darshan Sen) [#36998](https://github.com/nodejs/node/pull/36998)
  * add @miladfarca to collaborators (Milad Fa) [#36934](https://github.com/nodejs/node/pull/36934)
* **fs**:
  * allow `position` parameter to be a `BigInt` in read and readSync (raisinten) [#36190](https://github.com/nodejs/node/pull/36190)
* **http**:
  * attach request as res.req (Ian Storm Taylor) [#36505](https://github.com/nodejs/node/pull/36505)
  * expose urlToHttpOptions utility (Yongsheng Zhang) [#35960](https://github.com/nodejs/node/pull/35960)

### Commits

* \[[`775b34b822`](https://github.com/nodejs/node/commit/775b34b822)] - **(SEMVER-MINOR)** **buffer**: introduce Blob (James M Snell) [#36811](https://github.com/nodejs/node/pull/36811)
* \[[`832cd015d5`](https://github.com/nodejs/node/commit/832cd015d5)] - **(SEMVER-MINOR)** **buffer**: add base64url encoding option (Filip Skokan) [#36952](https://github.com/nodejs/node/pull/36952)
* \[[`7ce7404f79`](https://github.com/nodejs/node/commit/7ce7404f79)] - **build**: fix compiling against openssl with no-psk (Caleb ツ Everett) [#36881](https://github.com/nodejs/node/pull/36881)
* \[[`b7d8e61ef1`](https://github.com/nodejs/node/commit/b7d8e61ef1)] - **crypto**: fix randomInt bias (Tobias Nießen) [#36894](https://github.com/nodejs/node/pull/36894)
* \[[`1149af6265`](https://github.com/nodejs/node/commit/1149af6265)] - **(SEMVER-MINOR)** **crypto**: add keyObject.asymmetricKeyDetails for asymmetric keys (Filip Skokan) [#36188](https://github.com/nodejs/node/pull/36188)
* \[[`0398167b35`](https://github.com/nodejs/node/commit/0398167b35)] - **crypto**: fix WebCrypto import of RSA-PSS keys (Tobias Nießen) [#36877](https://github.com/nodejs/node/pull/36877)
* \[[`e52e860172`](https://github.com/nodejs/node/commit/e52e860172)] - **deps**: upgrade npm to 7.4.3 (Ruy Adorno) [#37018](https://github.com/nodejs/node/pull/37018)
* \[[`ef3a5f6958`](https://github.com/nodejs/node/commit/ef3a5f6958)] - **deps**: update ICU to 68.2 (Michaël Zasso) [#36980](https://github.com/nodejs/node/pull/36980)
* \[[`ca479b9e9d`](https://github.com/nodejs/node/commit/ca479b9e9d)] - **deps**: V8: cherry-pick fe191e8d05cc (Benjamin Coe) [#36956](https://github.com/nodejs/node/pull/36956)
* \[[`6f773fbe84`](https://github.com/nodejs/node/commit/6f773fbe84)] - **deps**: upgrade npm to 7.4.2 (Ruy Adorno) [#36953](https://github.com/nodejs/node/pull/36953)
* \[[`4b952d8d3e`](https://github.com/nodejs/node/commit/4b952d8d3e)] - **doc**: fix maintaining ICU guide (Michaël Zasso) [#36980](https://github.com/nodejs/node/pull/36980)
* \[[`a2559b9044`](https://github.com/nodejs/node/commit/a2559b9044)] - **doc**: add @RaisinTen to collaborators (Darshan Sen) [#36998](https://github.com/nodejs/node/pull/36998)
* \[[`4d5273b156`](https://github.com/nodejs/node/commit/4d5273b156)] - **doc**: fix typo in http.server.requestTimout docs (alexbs) [#36987](https://github.com/nodejs/node/pull/36987)
* \[[`93fc295b75`](https://github.com/nodejs/node/commit/93fc295b75)] - **doc**: add performance notes for fs.readFile (James M Snell) [#36880](https://github.com/nodejs/node/pull/36880)
* \[[`7ea374b159`](https://github.com/nodejs/node/commit/7ea374b159)] - **doc**: clarify maxSockets option of http.Agent (Pooja D P) [#36941](https://github.com/nodejs/node/pull/36941)
* \[[`f3637d5328`](https://github.com/nodejs/node/commit/f3637d5328)] - **doc**: remove pull-requests.md preamble (Rich Trott) [#36960](https://github.com/nodejs/node/pull/36960)
* \[[`d2d9ad7477`](https://github.com/nodejs/node/commit/d2d9ad7477)] - **doc**: fix module.isPreloading documentation (Antoine du Hamel) [#36944](https://github.com/nodejs/node/pull/36944)
* \[[`48b6781151`](https://github.com/nodejs/node/commit/48b6781151)] - **doc**: fix crypto.generateKeySync aes allowed length list (Filip Skokan) [#36928](https://github.com/nodejs/node/pull/36928)
* \[[`120db2c169`](https://github.com/nodejs/node/commit/120db2c169)] - **doc**: fix grammar and link to QUIC in changelog (Dan Dascalescu) [#36959](https://github.com/nodejs/node/pull/36959)
* \[[`af0f0a0f65`](https://github.com/nodejs/node/commit/af0f0a0f65)] - **doc**: fix percentile range in perf\_hooks.md (raisinten) [#36938](https://github.com/nodejs/node/pull/36938)
* \[[`8cf280d9ab`](https://github.com/nodejs/node/commit/8cf280d9ab)] - **doc**: improve perf\_hooks docs (Juan José Arboleda) [#36909](https://github.com/nodejs/node/pull/36909)
* \[[`3ea37c2d67`](https://github.com/nodejs/node/commit/3ea37c2d67)] - **doc**: fix invalid HTML in doc template (Rich Trott) [#36930](https://github.com/nodejs/node/pull/36930)
* \[[`eaf378aa46`](https://github.com/nodejs/node/commit/eaf378aa46)] - **doc**: remove issue template duplication from contributing docs (Rich Trott) [#36908](https://github.com/nodejs/node/pull/36908)
* \[[`7a794417f3`](https://github.com/nodejs/node/commit/7a794417f3)] - **doc**: remove resolving-a-bug-report from contributing docs (Rich Trott) [#36905](https://github.com/nodejs/node/pull/36905)
* \[[`707b97307d`](https://github.com/nodejs/node/commit/707b97307d)] - **doc**: use ESM syntax for WASI example (Antoine du Hamel) [#36848](https://github.com/nodejs/node/pull/36848)
* \[[`5a9a07e7cd`](https://github.com/nodejs/node/commit/5a9a07e7cd)] - **doc**: add iansu to collaborators (Ian Sutherland) [#36951](https://github.com/nodejs/node/pull/36951)
* \[[`aa3bc74cd6`](https://github.com/nodejs/node/commit/aa3bc74cd6)] - **doc**: fixup typo in metadata entry (James M Snell) [#36947](https://github.com/nodejs/node/pull/36947)
* \[[`22e29ccfa3`](https://github.com/nodejs/node/commit/22e29ccfa3)] - **doc**: add alternative version links to the packages page (Filip Skokan) [#36915](https://github.com/nodejs/node/pull/36915)
* \[[`80c84a1136`](https://github.com/nodejs/node/commit/80c84a1136)] - **doc**: add miladfarca to collaborators (Milad Fa) [#36934](https://github.com/nodejs/node/pull/36934)
* \[[`e73b1072f3`](https://github.com/nodejs/node/commit/e73b1072f3)] - **doc**: update tls test to use better terminology (Michael Dawson) [#36851](https://github.com/nodejs/node/pull/36851)
* \[[`5cbf638c06`](https://github.com/nodejs/node/commit/5cbf638c06)] - **doc**: remove unnecessary contributing.md section (Rich Trott) [#36891](https://github.com/nodejs/node/pull/36891)
* \[[`f99b38fedd`](https://github.com/nodejs/node/commit/f99b38fedd)] - **doc**: wrap TOC in a \<details> tag (Mattia Pontonio) [#36896](https://github.com/nodejs/node/pull/36896)
* \[[`82eccddf1e`](https://github.com/nodejs/node/commit/82eccddf1e)] - **doc**: update fs.l/statSync API history for throwIfNoEntry (Andrew Casey) [#36882](https://github.com/nodejs/node/pull/36882)
* \[[`70cd43c32e`](https://github.com/nodejs/node/commit/70cd43c32e)] - **doc**: change "it's" to "its" where necessary (Tobias Nießen) [#36913](https://github.com/nodejs/node/pull/36913)
* \[[`02a8f52040`](https://github.com/nodejs/node/commit/02a8f52040)] - **doc**: fix indentation on http2 doc entry (Rich Trott) [#36869](https://github.com/nodejs/node/pull/36869)
* \[[`dc596d0607`](https://github.com/nodejs/node/commit/dc596d0607)] - **events**: remove error listener on signal abort (ZiJian Liu) [#36969](https://github.com/nodejs/node/pull/36969)
* \[[`c4cdf1d830`](https://github.com/nodejs/node/commit/c4cdf1d830)] - **(SEMVER-MINOR)** **fs**: allow `position` parameter to be a `BigInt` in read and readSync (raisinten) [#36190](https://github.com/nodejs/node/pull/36190)
* \[[`70ee7dce62`](https://github.com/nodejs/node/commit/70ee7dce62)] - **(SEMVER-MINOR)** **http**: attach request as res.req (Ian Storm Taylor) [#36505](https://github.com/nodejs/node/pull/36505)
* \[[`f07e1c9d03`](https://github.com/nodejs/node/commit/f07e1c9d03)] - **http**: abortIncoming only on socket close (Robert Nagy) [#36821](https://github.com/nodejs/node/pull/36821)
* \[[`aa7243e3d4`](https://github.com/nodejs/node/commit/aa7243e3d4)] - **http**: refactor ClientRequest destroy (Robert Nagy) [#36863](https://github.com/nodejs/node/pull/36863)
* \[[`80051abfcb`](https://github.com/nodejs/node/commit/80051abfcb)] - **http**: cleanup ClientRequest oncreate (Robert Nagy) [#36862](https://github.com/nodejs/node/pull/36862)
* \[[`f5b8e7b068`](https://github.com/nodejs/node/commit/f5b8e7b068)] - **http2**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#36700](https://github.com/nodejs/node/pull/36700)
* \[[`8aeba3cb92`](https://github.com/nodejs/node/commit/8aeba3cb92)] - **lib**: refactor to use validateArray (ZiJian Liu) [#36982](https://github.com/nodejs/node/pull/36982)
* \[[`743dd8f89d`](https://github.com/nodejs/node/commit/743dd8f89d)] - **lib**: remove non used getter in `lib/perf\_hooks.js` (Juan José Arboleda) [#36907](https://github.com/nodejs/node/pull/36907)
* \[[`f2ac4bb8e2`](https://github.com/nodejs/node/commit/f2ac4bb8e2)] - **lib**: expose primordials object (Antoine du Hamel) [#36872](https://github.com/nodejs/node/pull/36872)
* \[[`850d3578b6`](https://github.com/nodejs/node/commit/850d3578b6)] - **lib**: refactor `primordials.makeSafe` to use more primordials (ExE Boss) [#36865](https://github.com/nodejs/node/pull/36865)
* \[[`b86c48cc91`](https://github.com/nodejs/node/commit/b86c48cc91)] - **lib**: refactor source\_map to use more primordials (Antoine du Hamel) [#36733](https://github.com/nodejs/node/pull/36733)
* \[[`1ef92f61fa`](https://github.com/nodejs/node/commit/1ef92f61fa)] - **lib**: refactor source\_map to avoid unsafe array iteration (Antoine du Hamel) [#36734](https://github.com/nodejs/node/pull/36734)
* \[[`5290d63e7f`](https://github.com/nodejs/node/commit/5290d63e7f)] - **module**: simplify tryStatSync with throwIfNoEntry option (Antoine du Hamel) [#36971](https://github.com/nodejs/node/pull/36971)
* \[[`89a7941425`](https://github.com/nodejs/node/commit/89a7941425)] - **os**: performance improvement in vector allocation (Yash Ladha) [#36748](https://github.com/nodejs/node/pull/36748)
* \[[`3f75a60b51`](https://github.com/nodejs/node/commit/3f75a60b51)] - **perf\_hooks**: throw ERR\_INVALID\_ARG\_VALUE if histogram.percentile param is NaN (ZiJian Liu) [#36937](https://github.com/nodejs/node/pull/36937)
* \[[`9951daefbd`](https://github.com/nodejs/node/commit/9951daefbd)] - **repl**: refactor to avoid unsafe array iteration (raisinten) [#36663](https://github.com/nodejs/node/pull/36663)
* \[[`868d3b2ff6`](https://github.com/nodejs/node/commit/868d3b2ff6)] - **src**: use BaseObject::kInteralFieldCount in Blob (Joyee Cheung) [#36991](https://github.com/nodejs/node/pull/36991)
* \[[`a5ffdaee1c`](https://github.com/nodejs/node/commit/a5ffdaee1c)] - **src**: replace push\_back with emplace\_back in debug\_utils (raisinten) [#36897](https://github.com/nodejs/node/pull/36897)
* \[[`d54998538e`](https://github.com/nodejs/node/commit/d54998538e)] - **src**: use BaseObject::kInternalFieldCount in X509Certificate constructor (Joyee Cheung) [#36892](https://github.com/nodejs/node/pull/36892)
* \[[`7acea78493`](https://github.com/nodejs/node/commit/7acea78493)] - **test**: mark flaky tests on IBM i (Richard Lau) [#36986](https://github.com/nodejs/node/pull/36986)
* \[[`e69c4a941d`](https://github.com/nodejs/node/commit/e69c4a941d)] - **(SEMVER-MINOR)** **test**: add wpt tests for Blob (Michaël Zasso) [#36811](https://github.com/nodejs/node/pull/36811)
* \[[`2f1f1dadaa`](https://github.com/nodejs/node/commit/2f1f1dadaa)] - **test**: increase buffer list coverage (Emil Sivervik) [#36688](https://github.com/nodejs/node/pull/36688)
* \[[`8d49ce9d75`](https://github.com/nodejs/node/commit/8d49ce9d75)] - **test**: fix warning in test\_environment.cc (raisinten) [#36846](https://github.com/nodejs/node/pull/36846)
* \[[`98369aaf7b`](https://github.com/nodejs/node/commit/98369aaf7b)] - **test**: remove unused ecdhPeerKey (Daniel Bevenius) [#36942](https://github.com/nodejs/node/pull/36942)
* \[[`ba87be0b0e`](https://github.com/nodejs/node/commit/ba87be0b0e)] - **test**: improve coverage for `Module` getters (Juan José Arboleda) [#36950](https://github.com/nodejs/node/pull/36950)
* \[[`c7dd9c8c69`](https://github.com/nodejs/node/commit/c7dd9c8c69)] - **test**: skip internet for test-npm-install (Ruy Adorno) [#36933](https://github.com/nodejs/node/pull/36933)
* \[[`3bbe9a5588`](https://github.com/nodejs/node/commit/3bbe9a5588)] - **test**: improve coverage on worker threads (Juan José Arboleda) [#36910](https://github.com/nodejs/node/pull/36910)
* \[[`f589bb2052`](https://github.com/nodejs/node/commit/f589bb2052)] - **test**: improve coverage at `lib/internal/vm/module.js` (Juan José Arboleda) [#36898](https://github.com/nodejs/node/pull/36898)
* \[[`8a8241529e`](https://github.com/nodejs/node/commit/8a8241529e)] - _**Revert**_ "**test**: mark test-cluster-bind-privileged-port flaky on arm" (Rod Vagg) [#36884](https://github.com/nodejs/node/pull/36884)
* \[[`99c15909ad`](https://github.com/nodejs/node/commit/99c15909ad)] - **test**: fixup flaky test-crypto-x509 on windows (James M Snell) [#36966](https://github.com/nodejs/node/pull/36966)
* \[[`c2ec15aff6`](https://github.com/nodejs/node/commit/c2ec15aff6)] - **test**: check mustCall errors in test-fs-read-type (Tobias Nießen) [#36914](https://github.com/nodejs/node/pull/36914)
* \[[`30b2aac98a`](https://github.com/nodejs/node/commit/30b2aac98a)] - **test**: fix variable name for non-RSA keys (Tobias Nießen) [#36912](https://github.com/nodejs/node/pull/36912)
* \[[`fada6b0087`](https://github.com/nodejs/node/commit/fada6b0087)] - **test,benchmark**: stop requiring URL and URLSearchParams (raisinten) [#36927](https://github.com/nodejs/node/pull/36927)
* \[[`864b97b24d`](https://github.com/nodejs/node/commit/864b97b24d)] - **tls**: use recently added matching SecureContext in default SNICallback (Mateusz Krawczuk) [#36072](https://github.com/nodejs/node/pull/36072)
* \[[`6ef54bb9ca`](https://github.com/nodejs/node/commit/6ef54bb9ca)] - **tools**: cleanup old ICU version-specific fixes (Michaël Zasso) [#36980](https://github.com/nodejs/node/pull/36980)
* \[[`8e02b53b09`](https://github.com/nodejs/node/commit/8e02b53b09)] - **tools**: update ESLint to 7.18.0 (Colin Ihrig) [#36955](https://github.com/nodejs/node/pull/36955)
* \[[`8dc8adc782`](https://github.com/nodejs/node/commit/8dc8adc782)] - **tools**: add support for top-level await syntax in linter (Antoine du Hamel) [#36911](https://github.com/nodejs/node/pull/36911)
* \[[`17bdcd9d18`](https://github.com/nodejs/node/commit/17bdcd9d18)] - **tools,doc**: list the stability status of each API (Zijian Liu) [#36223](https://github.com/nodejs/node/pull/36223)
* \[[`889654d36c`](https://github.com/nodejs/node/commit/889654d36c)] - **url**: align url format behavior with browsers (ZiJian Liu) [#36903](https://github.com/nodejs/node/pull/36903)
* \[[`64fed319ef`](https://github.com/nodejs/node/commit/64fed319ef)] - **(SEMVER-MINOR)** **url**: expose urlToHttpOptions utility (Yongsheng Zhang) [#35960](https://github.com/nodejs/node/pull/35960)
* \[[`f2704170a3`](https://github.com/nodejs/node/commit/f2704170a3)] - **util**: prefer `Reflect.ownKeys(…)` (ExE Boss) [#36740](https://github.com/nodejs/node/pull/36740)
* \[[`0d719476e0`](https://github.com/nodejs/node/commit/0d719476e0)] - **vm**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#36752](https://github.com/nodejs/node/pull/36752)
* \[[`bf695ebdb1`](https://github.com/nodejs/node/commit/bf695ebdb1)] - **worker**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#36735](https://github.com/nodejs/node/pull/36735)
* \[[`403b595ef5`](https://github.com/nodejs/node/commit/403b595ef5)] - **zlib**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#36722](https://github.com/nodejs/node/pull/36722)

<a id="15.6.0"></a>

## 2021-01-14, Version 15.6.0 (Current), @danielleadams

### Notable Changes

* **child\_process**:
  * add 'overlapped' stdio flag (Thiago Padilha) [#29412](https://github.com/nodejs/node/pull/29412)
  * support AbortSignal in fork (Benjamin Gruenbaum) [#36603](https://github.com/nodejs/node/pull/36603)
* **crypto**:
  * implement basic secure heap support (James M Snell) [#36779](https://github.com/nodejs/node/pull/36779)
  * fixup bug in keygen error handling (James M Snell) [#36779](https://github.com/nodejs/node/pull/36779)
  * introduce X509Certificate API (James M Snell) [#36804](https://github.com/nodejs/node/pull/36804)
  * implement randomuuid (James M Snell) [#36729](https://github.com/nodejs/node/pull/36729)
* **doc**:
  * update release key for Danielle Adams (Danielle Adams) [#36793](https://github.com/nodejs/node/pull/36793)
  * add dnlup to collaborators (Daniele Belardi) [#36849](https://github.com/nodejs/node/pull/36849)
  * add panva to collaborators (Filip Skokan) [#36802](https://github.com/nodejs/node/pull/36802)
  * add yashLadha to collaborator (Yash Ladha) [#36666](https://github.com/nodejs/node/pull/36666)
* **http**:
  * set lifo as the default scheduling strategy in Agent (Matteo Collina) [#36685](https://github.com/nodejs/node/pull/36685)
* **net**:
  * support abortSignal in server.listen (Nitzan Uziely) [#36623](https://github.com/nodejs/node/pull/36623)
* **process**:
  * add direct access to rss without iterating pages (Adrien Maret) [#34291](https://github.com/nodejs/node/pull/34291)
* **v8**:
  * fix native `serdes` constructors (ExE Boss) [#36549](https://github.com/nodejs/node/pull/36549)

### Commits

* \[[`3ca7a786c5`](https://github.com/nodejs/node/commit/3ca7a786c5)] - **benchmark**: fix http2 benchmarks (Rich Trott) [#36871](https://github.com/nodejs/node/pull/36871)
* \[[`4601886d7c`](https://github.com/nodejs/node/commit/4601886d7c)] - **benchmark**: fix http/headers.js with test-double (Rich Trott) [#36794](https://github.com/nodejs/node/pull/36794)
* \[[`7aedda9dcd`](https://github.com/nodejs/node/commit/7aedda9dcd)] - **benchmark**: add simple https benchmark (Andrey Pechkurov) [#36612](https://github.com/nodejs/node/pull/36612)
* \[[`822ac48272`](https://github.com/nodejs/node/commit/822ac48272)] - **buffer**: make FastBuffer safe to construct (Antoine du Hamel) [#36587](https://github.com/nodejs/node/pull/36587)
* \[[`21f329532f`](https://github.com/nodejs/node/commit/21f329532f)] - **build**: refactor Makefile (raisinten) [#36759](https://github.com/nodejs/node/pull/36759)
* \[[`857b98eed9`](https://github.com/nodejs/node/commit/857b98eed9)] - **build**: fix unknown warning option (raisinten) [#36629](https://github.com/nodejs/node/pull/36629)
* \[[`ffaa8c1735`](https://github.com/nodejs/node/commit/ffaa8c1735)] - **build**: do not "exit" a script meant to be "source"d (François-Denis Gonthier) [#35520](https://github.com/nodejs/node/pull/35520)
* \[[`9bc2cec848`](https://github.com/nodejs/node/commit/9bc2cec848)] - **(SEMVER-MINOR)** **child\_process**: add 'overlapped' stdio flag (Thiago Padilha) [#29412](https://github.com/nodejs/node/pull/29412)
* \[[`b98cc51be2`](https://github.com/nodejs/node/commit/b98cc51be2)] - **child\_process**: reduce abort handler code duplication (Rich Trott) [#36644](https://github.com/nodejs/node/pull/36644)
* \[[`78d4d91e54`](https://github.com/nodejs/node/commit/78d4d91e54)] - **child\_process**: treat already-aborted controller as aborting (Rich Trott) [#36644](https://github.com/nodejs/node/pull/36644)
* \[[`a8a427f646`](https://github.com/nodejs/node/commit/a8a427f646)] - **(SEMVER-MINOR)** **child\_process**: support AbortSignal in fork (Benjamin Gruenbaum) [#36603](https://github.com/nodejs/node/pull/36603)
* \[[`7134d49e56`](https://github.com/nodejs/node/commit/7134d49e56)] - **child\_process**: clean event listener correctly (Benjamin Gruenbaum) [#36424](https://github.com/nodejs/node/pull/36424)
* \[[`54bd4ab855`](https://github.com/nodejs/node/commit/54bd4ab855)] - **cluster**: fix edge cases that throw ERR\_INTERNAL\_ASSERTION (Ouyang Yadong) [#36764](https://github.com/nodejs/node/pull/36764)
* \[[`0c11a17d82`](https://github.com/nodejs/node/commit/0c11a17d82)] - **console**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#36753](https://github.com/nodejs/node/pull/36753)
* \[[`53cf996270`](https://github.com/nodejs/node/commit/53cf996270)] - **(SEMVER-MINOR)** **crypto**: implement basic secure heap support (James M Snell) [#36779](https://github.com/nodejs/node/pull/36779)
* \[[`42aca13953`](https://github.com/nodejs/node/commit/42aca13953)] - **(SEMVER-MINOR)** **crypto**: fixup bug in keygen error handling (James M Snell) [#36779](https://github.com/nodejs/node/pull/36779)
* \[[`c4ad50e0ff`](https://github.com/nodejs/node/commit/c4ad50e0ff)] - **(SEMVER-MINOR)** **crypto**: introduce X509Certificate API (James M Snell) [#36804](https://github.com/nodejs/node/pull/36804)
* \[[`4e4deca90d`](https://github.com/nodejs/node/commit/4e4deca90d)] - **(SEMVER-MINOR)** **crypto**: implement randomuuid (James M Snell) [#36729](https://github.com/nodejs/node/pull/36729)
* \[[`1c9ec2529e`](https://github.com/nodejs/node/commit/1c9ec2529e)] - **deps**: upgrade npm to 7.4.0 (Ruy Adorno) [#36829](https://github.com/nodejs/node/pull/36829)
* \[[`ff5bd04900`](https://github.com/nodejs/node/commit/ff5bd04900)] - **deps**: update nghttp2 to 1.42.0 (Michaël Zasso) [#36842](https://github.com/nodejs/node/pull/36842)
* \[[`578fa0fedf`](https://github.com/nodejs/node/commit/578fa0fedf)] - **deps**: V8: cherry-pick dfcdf7837e23 (Benjamin Coe) [#36573](https://github.com/nodejs/node/pull/36573)
* \[[`05f34c6963`](https://github.com/nodejs/node/commit/05f34c6963)] - **doc**: define "browser", "production", "development" (Guy Bedford) [#36856](https://github.com/nodejs/node/pull/36856)
* \[[`e8bb1f7350`](https://github.com/nodejs/node/commit/e8bb1f7350)] - **doc**: clarify event.isTrusted text (Rich Trott) [#36827](https://github.com/nodejs/node/pull/36827)
* \[[`153be6c80e`](https://github.com/nodejs/node/commit/153be6c80e)] - **doc**: fix module syncBuiltinESMExports example (Bruce A. MacNaughton) [#34284](https://github.com/nodejs/node/pull/34284)
* \[[`3b64b38142`](https://github.com/nodejs/node/commit/3b64b38142)] - **doc**: os.uptime() temporary bug notice (Nicholas Schamberg) [#36503](https://github.com/nodejs/node/pull/36503)
* \[[`da49624a46`](https://github.com/nodejs/node/commit/da49624a46)] - **doc**: update release key for Danielle Adams (Danielle Adams) [#36793](https://github.com/nodejs/node/pull/36793)
* \[[`2d8423da3c`](https://github.com/nodejs/node/commit/2d8423da3c)] - **doc**: clarify child\_process.exec inherits cwd (ugultopu) [#36809](https://github.com/nodejs/node/pull/36809)
* \[[`1a4d34ebd0`](https://github.com/nodejs/node/commit/1a4d34ebd0)] - **doc**: clarify descriptions of \_writev chunks argument (James M Snell) [#36822](https://github.com/nodejs/node/pull/36822)
* \[[`7c7180a6f7`](https://github.com/nodejs/node/commit/7c7180a6f7)] - **doc**: document buffer's "Uint" aliases clearly (Michaël Zasso) [#36796](https://github.com/nodejs/node/pull/36796)
* \[[`ff6edbc6b2`](https://github.com/nodejs/node/commit/ff6edbc6b2)] - **doc**: add dnlup to collaborators (Daniele Belardi) [#36849](https://github.com/nodejs/node/pull/36849)
* \[[`835bdf0e50`](https://github.com/nodejs/node/commit/835bdf0e50)] - **doc**: improve crypto.randomUUID() text (Rich Trott) [#36830](https://github.com/nodejs/node/pull/36830)
* \[[`d4bcb3689d`](https://github.com/nodejs/node/commit/d4bcb3689d)] - **doc**: clarify subprocess.stdout/in/err/io properties (James M Snell) [#36784](https://github.com/nodejs/node/pull/36784)
* \[[`a956fb3fdd`](https://github.com/nodejs/node/commit/a956fb3fdd)] - **doc**: add dark mode (Ajay Poshak) [#36313](https://github.com/nodejs/node/pull/36313)
* \[[`757b9664cd`](https://github.com/nodejs/node/commit/757b9664cd)] - **doc**: revise method text in async\_hooks.md (Rich Trott) [#36736](https://github.com/nodejs/node/pull/36736)
* \[[`b4091ea59b`](https://github.com/nodejs/node/commit/b4091ea59b)] - **doc**: clarify when messageerror is emitted (James M Snell) [#36780](https://github.com/nodejs/node/pull/36780)
* \[[`61b039365c`](https://github.com/nodejs/node/commit/61b039365c)] - **doc**: avoid memory leak warning in async\_hooks example (James M Snell) [#36783](https://github.com/nodejs/node/pull/36783)
* \[[`a7bb4da55e`](https://github.com/nodejs/node/commit/a7bb4da55e)] - **doc**: clarify that --require only supports cjs (James M Snell) [#36806](https://github.com/nodejs/node/pull/36806)
* \[[`c6eb2b4fec`](https://github.com/nodejs/node/commit/c6eb2b4fec)] - **doc**: clarify Buffer.from when using ArrayBuffer (James M Snell) [#36785](https://github.com/nodejs/node/pull/36785)
* \[[`ad1d8fba9f`](https://github.com/nodejs/node/commit/ad1d8fba9f)] - **doc**: fix broken link for ChildProcess (James M Snell) [#36788](https://github.com/nodejs/node/pull/36788)
* \[[`ef628891f7`](https://github.com/nodejs/node/commit/ef628891f7)] - **doc**: revise exit() and run() text in async\_hooks.md (Rich Trott) [#36738](https://github.com/nodejs/node/pull/36738)
* \[[`ff39464559`](https://github.com/nodejs/node/commit/ff39464559)] - **doc**: add OpenSSL CVE fix to notable changes in v15.5.0 (Beth Griggs) [#36798](https://github.com/nodejs/node/pull/36798)
* \[[`6db465a99f`](https://github.com/nodejs/node/commit/6db465a99f)] - **doc**: clarify that N-API addons are context-aware (Alba Mendez) [#36640](https://github.com/nodejs/node/pull/36640)
* \[[`fad07d5439`](https://github.com/nodejs/node/commit/fad07d5439)] - **doc**: fix typo in esm documentation (Mohamed Kamagate) [#36800](https://github.com/nodejs/node/pull/36800)
* \[[`67dd48ed05`](https://github.com/nodejs/node/commit/67dd48ed05)] - **doc**: add panva to collaborators (Filip Skokan) [#36802](https://github.com/nodejs/node/pull/36802)
* \[[`b2c1aeb694`](https://github.com/nodejs/node/commit/b2c1aeb694)] - **doc**: revise process.memoryUsage() text (Rich Trott) [#36757](https://github.com/nodejs/node/pull/36757)
* \[[`8f672ebbd6`](https://github.com/nodejs/node/commit/8f672ebbd6)] - **doc**: add YAML metadata for process.memoryUsage.rss (Gerhard Stoebich) [#36781](https://github.com/nodejs/node/pull/36781)
* \[[`fa54f012b8`](https://github.com/nodejs/node/commit/fa54f012b8)] - **doc**: reduce abbreviations in async\_hooks.md (Rich Trott) [#36737](https://github.com/nodejs/node/pull/36737)
* \[[`56c00d7b2f`](https://github.com/nodejs/node/commit/56c00d7b2f)] - **doc**: simplify pull request template (Rich Trott) [#36739](https://github.com/nodejs/node/pull/36739)
* \[[`214dbac8ff`](https://github.com/nodejs/node/commit/214dbac8ff)] - **doc**: clarify undocumented stream properties (James M Snell) [#36715](https://github.com/nodejs/node/pull/36715)
* \[[`242ce19346`](https://github.com/nodejs/node/commit/242ce19346)] - **doc**: document common warning types (James M Snell) [#36713](https://github.com/nodejs/node/pull/36713)
* \[[`d3dc124575`](https://github.com/nodejs/node/commit/d3dc124575)] - **doc**: update emitClose default for fs streams (Kevin Locke) [#36653](https://github.com/nodejs/node/pull/36653)
* \[[`181bd0510f`](https://github.com/nodejs/node/commit/181bd0510f)] - **doc**: improve ALS.enterWith and exit descriptions (Andrey Pechkurov) [#36705](https://github.com/nodejs/node/pull/36705)
* \[[`edf8c6de5a`](https://github.com/nodejs/node/commit/edf8c6de5a)] - **doc**: add note about uncloneable objects (James M Snell) [#36534](https://github.com/nodejs/node/pull/36534)
* \[[`651e7d27b7`](https://github.com/nodejs/node/commit/651e7d27b7)] - **doc**: document http.IncomingMessage behaviour change (Dr) [#36641](https://github.com/nodejs/node/pull/36641)
* \[[`72b0ab0739`](https://github.com/nodejs/node/commit/72b0ab0739)] - **doc**: add yashLadha to collaborator (Yash Ladha) [#36666](https://github.com/nodejs/node/pull/36666)
* \[[`8a0cdb3b4e`](https://github.com/nodejs/node/commit/8a0cdb3b4e)] - **doc**: alphabetize http response properties (Rich Trott) [#36631](https://github.com/nodejs/node/pull/36631)
* \[[`ff4674b033`](https://github.com/nodejs/node/commit/ff4674b033)] - **doc**: correct callback parameter type for createPushResponse() (Rich Trott) [#36631](https://github.com/nodejs/node/pull/36631)
* \[[`f623d5d377`](https://github.com/nodejs/node/commit/f623d5d377)] - **doc**: use \_code name\_ rather than \_codename\_ (Rich Trott) [#36611](https://github.com/nodejs/node/pull/36611)
* \[[`1ed517c176`](https://github.com/nodejs/node/commit/1ed517c176)] - **doc**: document return value of https.request (Michael Chen) [#36370](https://github.com/nodejs/node/pull/36370)
* \[[`5645b21e23`](https://github.com/nodejs/node/commit/5645b21e23)] - **doc**: document "http: lazy create IncomingMessage.headers" (ExE Boss) [#36601](https://github.com/nodejs/node/pull/36601)
* \[[`3ee4cfc7d7`](https://github.com/nodejs/node/commit/3ee4cfc7d7)] - **doc**: fix bugs in \_construct() example (Maksym Baranovskyi) [#36509](https://github.com/nodejs/node/pull/36509)
* \[[`93237c5999`](https://github.com/nodejs/node/commit/93237c5999)] - **doc**: remove replication of GitHub template (Rich Trott) [#36590](https://github.com/nodejs/node/pull/36590)
* \[[`538f226f6d`](https://github.com/nodejs/node/commit/538f226f6d)] - **doc**: remove "Related Issues" from pull request template (Rich Trott) [#36590](https://github.com/nodejs/node/pull/36590)
* \[[`dcc93d3dce`](https://github.com/nodejs/node/commit/dcc93d3dce)] - **doc**: expand openssl instructions (Michael Dawson) [#36554](https://github.com/nodejs/node/pull/36554)
* \[[`41e278bf61`](https://github.com/nodejs/node/commit/41e278bf61)] - **docs**: add references to punycode.md (Isaac Levy) [#36761](https://github.com/nodejs/node/pull/36761)
* \[[`9b9b6d5fc5`](https://github.com/nodejs/node/commit/9b9b6d5fc5)] - **domain**: make node resilient to Array prototype tempering (Antoine du Hamel) [#36676](https://github.com/nodejs/node/pull/36676)
* \[[`f0a9c53bec`](https://github.com/nodejs/node/commit/f0a9c53bec)] - **errors**: refactor to use more primordials (Antoine du Hamel) [#36651](https://github.com/nodejs/node/pull/36651)
* \[[`c844d22b72`](https://github.com/nodejs/node/commit/c844d22b72)] - **errors**: eliminate all overhead for hidden calls (Momtchil Momtchev) [#35644](https://github.com/nodejs/node/pull/35644)
* \[[`3fa470a3c9`](https://github.com/nodejs/node/commit/3fa470a3c9)] - **events**: refactor to use optional chaining (ZiJian Liu) [#36763](https://github.com/nodejs/node/pull/36763)
* \[[`82393aefff`](https://github.com/nodejs/node/commit/82393aefff)] - **events**: refactor to use more primordials (Antoine du Hamel) [#36304](https://github.com/nodejs/node/pull/36304)
* \[[`e3a091d9f3`](https://github.com/nodejs/node/commit/e3a091d9f3)] - **fs**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#36699](https://github.com/nodejs/node/pull/36699)
* \[[`d5e1b82125`](https://github.com/nodejs/node/commit/d5e1b82125)] - **fs**: accept non-32-bit length in writeBuffer (raisinten) [#36667](https://github.com/nodejs/node/pull/36667)
* \[[`d858c9576a`](https://github.com/nodejs/node/commit/d858c9576a)] - **http**: remove dead code from internal/http.js (ZiJian Liu) [#36630](https://github.com/nodejs/node/pull/36630)
* \[[`7e3ad1be32`](https://github.com/nodejs/node/commit/7e3ad1be32)] - _**Revert**_ "**http**: remove dead code from internal/http.js" (ZiJian Liu) [#36890](https://github.com/nodejs/node/pull/36890)
* \[[`a9a2dd32e3`](https://github.com/nodejs/node/commit/a9a2dd32e3)] - **http**: don't cork noop .end() (Robert Nagy) [#36633](https://github.com/nodejs/node/pull/36633)
* \[[`dfc962f67a`](https://github.com/nodejs/node/commit/dfc962f67a)] - **http**: add test case for req-res close ordering (Daniele Belardi) [#36645](https://github.com/nodejs/node/pull/36645)
* \[[`cc28d2f541`](https://github.com/nodejs/node/commit/cc28d2f541)] - **(SEMVER-MINOR)** **http**: set lifo as the default scheduling strategy in Agent (Matteo Collina) [#36685](https://github.com/nodejs/node/pull/36685)
* \[[`954a36947d`](https://github.com/nodejs/node/commit/954a36947d)] - **http**: make HEAD method to work with keep-alive (Joseph Hackman) [#34231](https://github.com/nodejs/node/pull/34231)
* \[[`9156f430b5`](https://github.com/nodejs/node/commit/9156f430b5)] - **http**: remove dead code from internal/http.js (ZiJian Liu) [#36630](https://github.com/nodejs/node/pull/36630)
* \[[`5e499c490e`](https://github.com/nodejs/node/commit/5e499c490e)] - **http**: refactor to use more primordials (Antoine du Hamel) [#36194](https://github.com/nodejs/node/pull/36194)
* \[[`c784f15588`](https://github.com/nodejs/node/commit/c784f15588)] - _**Revert**_ "**http**: use `autoDestroy: true` in incoming message" (Daniele Belardi) [#36647](https://github.com/nodejs/node/pull/36647)
* \[[`a38ad0709c`](https://github.com/nodejs/node/commit/a38ad0709c)] - **http2**: refactor to use primordials instead of \<string>.indexOf (Rohan Chougule) [#36679](https://github.com/nodejs/node/pull/36679)
* \[[`e85fbb778d`](https://github.com/nodejs/node/commit/e85fbb778d)] - **http2**: fix typos in core.js (Pranshu Jethmalani) [#36719](https://github.com/nodejs/node/pull/36719)
* \[[`a4d64f967a`](https://github.com/nodejs/node/commit/a4d64f967a)] - **https**: refactor to use more primordials (Antoine du Hamel) [#36195](https://github.com/nodejs/node/pull/36195)
* \[[`1db3772c95`](https://github.com/nodejs/node/commit/1db3772c95)] - **lib**: simplify `primordials.uncurryThis` (ExE Boss) [#36866](https://github.com/nodejs/node/pull/36866)
* \[[`95219eac08`](https://github.com/nodejs/node/commit/95219eac08)] - **lib**: refactor to use mapping in cluster master (Yash Ladha) [#36250](https://github.com/nodejs/node/pull/36250)
* \[[`b764269437`](https://github.com/nodejs/node/commit/b764269437)] - **lib**: remove v8\_prof\_polyfill from eslint ignore list (Antoine du Hamel) [#36537](https://github.com/nodejs/node/pull/36537)
* \[[`eb6b38639a`](https://github.com/nodejs/node/commit/eb6b38639a)] - **lib**: remove unused code (Brian White) [#36632](https://github.com/nodejs/node/pull/36632)
* \[[`7fe1b5ef5a`](https://github.com/nodejs/node/commit/7fe1b5ef5a)] - **lib**: refactor to use validateCallback (ZiJian Liu) [#36609](https://github.com/nodejs/node/pull/36609)
* \[[`bb4f8c8732`](https://github.com/nodejs/node/commit/bb4f8c8732)] - **lib**: use more primordials in shared validators (Pooja D P) [#36552](https://github.com/nodejs/node/pull/36552)
* \[[`181bad58d3`](https://github.com/nodejs/node/commit/181bad58d3)] - **lib**: add primordials.SafeArrayIterator (Antoine du Hamel) [#36532](https://github.com/nodejs/node/pull/36532)
* \[[`6e338dac3c`](https://github.com/nodejs/node/commit/6e338dac3c)] - **lib**: refactor to use more primordials in internal/encoding.js (raisinten) [#36480](https://github.com/nodejs/node/pull/36480)
* \[[`ec3e841f59`](https://github.com/nodejs/node/commit/ec3e841f59)] - **lib**: refactor to use primordials in internal/priority\_queue.js (ZiJian Liu) [#36560](https://github.com/nodejs/node/pull/36560)
* \[[`8ac2016229`](https://github.com/nodejs/node/commit/8ac2016229)] - **lib**: add primordials.SafeStringIterator (Antoine du Hamel) [#36526](https://github.com/nodejs/node/pull/36526)
* \[[`56af1250fe`](https://github.com/nodejs/node/commit/56af1250fe)] - **lib**: make safe primordials safe to construct (Antoine du Hamel) [#36428](https://github.com/nodejs/node/pull/36428)
* \[[`d20235b6cb`](https://github.com/nodejs/node/commit/d20235b6cb)] - **lib**: fix diagnostics\_channel hasSubscribers error (ZiJian Liu) [#36599](https://github.com/nodejs/node/pull/36599)
* \[[`63091f8440`](https://github.com/nodejs/node/commit/63091f8440)] - **lib**: refactor to use more primordials in internal/histogram.js (raisinten) [#36455](https://github.com/nodejs/node/pull/36455)
* \[[`eca2df0909`](https://github.com/nodejs/node/commit/eca2df0909)] - **meta**: notify slack when someone force pushes (Mary Marchini) [#35131](https://github.com/nodejs/node/pull/35131)
* \[[`01213c71b9`](https://github.com/nodejs/node/commit/01213c71b9)] - **module**: fix Windows folder exports deprecation warning (Guy Bedford) [#36859](https://github.com/nodejs/node/pull/36859)
* \[[`302be57be4`](https://github.com/nodejs/node/commit/302be57be4)] - **module**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#36680](https://github.com/nodejs/node/pull/36680)
* \[[`24246a29d7`](https://github.com/nodejs/node/commit/24246a29d7)] - **net**: throw ERR\_OUT\_OF\_RANGE if blockList.addSubnet prefix is NaN (ZiJian Liu) [#36732](https://github.com/nodejs/node/pull/36732)
* \[[`02dbcc4317`](https://github.com/nodejs/node/commit/02dbcc4317)] - **(SEMVER-MINOR)** **net**: support abortSignal in server.listen (Nitzan Uziely) [#36623](https://github.com/nodejs/node/pull/36623)
* \[[`a258bc9b70`](https://github.com/nodejs/node/commit/a258bc9b70)] - **perf\_hooks**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#36723](https://github.com/nodejs/node/pull/36723)
* \[[`94afc3e712`](https://github.com/nodejs/node/commit/94afc3e712)] - **process**: passing -1 to setuid/setgid should not abort (James M Snell) [#36786](https://github.com/nodejs/node/pull/36786)
* \[[`92af50327e`](https://github.com/nodejs/node/commit/92af50327e)] - **(SEMVER-MINOR)** **process**: add direct access to rss without iterating pages (Adrien Maret) [#34291](https://github.com/nodejs/node/pull/34291)
* \[[`8b7336b072`](https://github.com/nodejs/node/commit/8b7336b072)] - **quic,timers**: refactor to use validateAbortSignal (ZiJian Liu) [#36604](https://github.com/nodejs/node/pull/36604)
* \[[`b17130a55a`](https://github.com/nodejs/node/commit/b17130a55a)] - **readline**: fix behaviour of Interface plugged to a non-terminal output (Antoine du Hamel) [#36774](https://github.com/nodejs/node/pull/36774)
* \[[`d70824f567`](https://github.com/nodejs/node/commit/d70824f567)] - **src**: fix typo in crypto\_aes.cc (Ikko Ashimine) [#36717](https://github.com/nodejs/node/pull/36717)
* \[[`8b43388903`](https://github.com/nodejs/node/commit/8b43388903)] - **src**: reduce duplicated boilerplate with new env utility fn (James M Snell) [#36536](https://github.com/nodejs/node/pull/36536)
* \[[`a53997e6c0`](https://github.com/nodejs/node/commit/a53997e6c0)] - **src**: fix leading backslash bug in URL (raisinten) [#36613](https://github.com/nodejs/node/pull/36613)
* \[[`abae61e230`](https://github.com/nodejs/node/commit/abae61e230)] - **stream**: finished waits for 'close' on OutgoingMessage (Robert Nagy) [#36648](https://github.com/nodejs/node/pull/36648)
* \[[`4c819d65f9`](https://github.com/nodejs/node/commit/4c819d65f9)] - **stream**: fix .end() error propagation (Robert Nagy) [#36817](https://github.com/nodejs/node/pull/36817)
* \[[`cb0b53edb1`](https://github.com/nodejs/node/commit/cb0b53edb1)] - **stream**: lazy read ReadStream (Momtchil Momtchev) [#36823](https://github.com/nodejs/node/pull/36823)
* \[[`b996e3b4b5`](https://github.com/nodejs/node/commit/b996e3b4b5)] - **stream**: do not use \_stream\_\* anymore (Matteo Collina) [#36684](https://github.com/nodejs/node/pull/36684)
* \[[`190ddced46`](https://github.com/nodejs/node/commit/190ddced46)] - **stream**: only use legacy close listeners if not willEmitClose (Robert Nagy) [#36649](https://github.com/nodejs/node/pull/36649)
* \[[`1fc30a84ac`](https://github.com/nodejs/node/commit/1fc30a84ac)] - **stream,zlib**: do not use \_stream\_\* anymore (Matteo Collina) [#36618](https://github.com/nodejs/node/pull/36618)
* \[[`d2b9e7cb01`](https://github.com/nodejs/node/commit/d2b9e7cb01)] - **string\_decoder**: throw ERR\_STRING\_TOO\_LONG for UTF-8 (Michaël Zasso) [#36661](https://github.com/nodejs/node/pull/36661)
* \[[`abc2ff47c2`](https://github.com/nodejs/node/commit/abc2ff47c2)] - **test**: disable test-crypto-secure-heap with asan (James M Snell) [#36900](https://github.com/nodejs/node/pull/36900)
* \[[`17a52337c4`](https://github.com/nodejs/node/commit/17a52337c4)] - **test**: http complete response after socket double end (Dimitris Halatsis) [#36633](https://github.com/nodejs/node/pull/36633)
* \[[`cc37ff24dc`](https://github.com/nodejs/node/commit/cc37ff24dc)] - **test**: use faster variant for rss in test-crypto-dh-leak (Pooja D P) [#36766](https://github.com/nodejs/node/pull/36766)
* \[[`daad0ab1cc`](https://github.com/nodejs/node/commit/daad0ab1cc)] - **test**: use faster variant for rss in test-vm-memleak.js (Pooja D P) [#36769](https://github.com/nodejs/node/pull/36769)
* \[[`9d25d25cfd`](https://github.com/nodejs/node/commit/9d25d25cfd)] - **test**: mark test-cluster-bind-privileged-port flaky on arm (James M Snell) [#36850](https://github.com/nodejs/node/pull/36850)
* \[[`c64db20fdd`](https://github.com/nodejs/node/commit/c64db20fdd)] - **test**: use faster variant for rss test-memoryusage-emfile (Pooja D P) [#36768](https://github.com/nodejs/node/pull/36768)
* \[[`d48e00e5a3`](https://github.com/nodejs/node/commit/d48e00e5a3)] - **test**: fix test-memory-usage.js for IBMi (Rich Trott) [#36758](https://github.com/nodejs/node/pull/36758)
* \[[`9b7d2c2523`](https://github.com/nodejs/node/commit/9b7d2c2523)] - **test**: guard large string decoder allocation (Michaël Zasso) [#36795](https://github.com/nodejs/node/pull/36795)
* \[[`5bc130bd9e`](https://github.com/nodejs/node/commit/5bc130bd9e)] - **test**: increase coverage for events (ZiJian Liu) [#36668](https://github.com/nodejs/node/pull/36668)
* \[[`9f7fbcc64d`](https://github.com/nodejs/node/commit/9f7fbcc64d)] - **test**: add coverage for breakLength one-column array (Rich Trott) [#36657](https://github.com/nodejs/node/pull/36657)
* \[[`9eff709c23`](https://github.com/nodejs/node/commit/9eff709c23)] - **test**: update wpt interfaces (Daijiro Wachi) [#36659](https://github.com/nodejs/node/pull/36659)
* \[[`a7f743f5cc`](https://github.com/nodejs/node/commit/a7f743f5cc)] - **test**: update wpt resources (Daijiro Wachi) [#36659](https://github.com/nodejs/node/pull/36659)
* \[[`4acc2732f9`](https://github.com/nodejs/node/commit/4acc2732f9)] - **test**: update wpt encoding (Daijiro Wachi) [#36659](https://github.com/nodejs/node/pull/36659)
* \[[`986d5aca44`](https://github.com/nodejs/node/commit/986d5aca44)] - **test**: update wpt url (Daijiro Wachi) [#36659](https://github.com/nodejs/node/pull/36659)
* \[[`833e614682`](https://github.com/nodejs/node/commit/833e614682)] - **test**: increase coverage for diagnostics\_channel (ZiJian Liu) [#36602](https://github.com/nodejs/node/pull/36602)
* \[[`f0dfe57bd1`](https://github.com/nodejs/node/commit/f0dfe57bd1)] - **test**: add already-aborted-controller test for spawn() (Rich Trott) [#36644](https://github.com/nodejs/node/pull/36644)
* \[[`d5d56ec3d4`](https://github.com/nodejs/node/commit/d5d56ec3d4)] - **test**: add test for reused AbortController with execfile() (Rich Trott) [#36644](https://github.com/nodejs/node/pull/36644)
* \[[`f81556563a`](https://github.com/nodejs/node/commit/f81556563a)] - **test**: increase coverage for internal/error\_serdes.js (ZiJian Liu) [#36628](https://github.com/nodejs/node/pull/36628)
* \[[`34d1d791e5`](https://github.com/nodejs/node/commit/34d1d791e5)] - **test**: improve coverage for util.inspect() with classes (Rich Trott) [#36625](https://github.com/nodejs/node/pull/36625)
* \[[`1f3bc5ed73`](https://github.com/nodejs/node/commit/1f3bc5ed73)] - **test**: increase runInAsyncScope() coverage (Rich Trott) [#36624](https://github.com/nodejs/node/pull/36624)
* \[[`863bfc44d2`](https://github.com/nodejs/node/commit/863bfc44d2)] - **test**: redirect stderr EnvironmentWithNoESMLoader (Daniel Bevenius) [#36548](https://github.com/nodejs/node/pull/36548)
* \[[`8e8b16ff7e`](https://github.com/nodejs/node/commit/8e8b16ff7e)] - **timers**: refactor to use optional chaining (ZiJian Liu) [#36767](https://github.com/nodejs/node/pull/36767)
* \[[`c23cca2de9`](https://github.com/nodejs/node/commit/c23cca2de9)] - **tls**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#36772](https://github.com/nodejs/node/pull/36772)
* \[[`37becfda8c`](https://github.com/nodejs/node/commit/37becfda8c)] - **tools**: update all lint-md rollup dependencies (Michaël Zasso) [#36843](https://github.com/nodejs/node/pull/36843)
* \[[`cfdbb79ccf`](https://github.com/nodejs/node/commit/cfdbb79ccf)] - **tools**: update doc tool dependencies (Michaël Zasso) [#36844](https://github.com/nodejs/node/pull/36844)
* \[[`1f2a198c32`](https://github.com/nodejs/node/commit/1f2a198c32)] - **tools**: fix md5 hash for ICU 68.1 src (Richard Lau) [#36777](https://github.com/nodejs/node/pull/36777)
* \[[`4e0995bc60`](https://github.com/nodejs/node/commit/4e0995bc60)] - **tools**: update ESLint to 7.17.0 (Colin Ihrig) [#36726](https://github.com/nodejs/node/pull/36726)
* \[[`8ad3455ae3`](https://github.com/nodejs/node/commit/8ad3455ae3)] - **tools**: revise install.py for minor improvements (Rich Trott) [#36626](https://github.com/nodejs/node/pull/36626)
* \[[`b367d5a61d`](https://github.com/nodejs/node/commit/b367d5a61d)] - **tools**: update gyp-next to v0.7.0 (Michaël Zasso) [#36580](https://github.com/nodejs/node/pull/36580)
* \[[`10f1c893c8`](https://github.com/nodejs/node/commit/10f1c893c8)] - **tools**: correct usage message for genv8constants.py (Rich Trott) [#36606](https://github.com/nodejs/node/pull/36606)
* \[[`37b39a2d6b`](https://github.com/nodejs/node/commit/37b39a2d6b)] - **tools**: call close() explicitly in genv8constants.py (Rich Trott) [#36606](https://github.com/nodejs/node/pull/36606)
* \[[`7664f3678c`](https://github.com/nodejs/node/commit/7664f3678c)] - **tools**: use `is None` consistently in Python (Rich Trott) [#36606](https://github.com/nodejs/node/pull/36606)
* \[[`cb7f73c9d4`](https://github.com/nodejs/node/commit/cb7f73c9d4)] - **tools**: revise line in configure.py for clarity (Rich Trott) [#36551](https://github.com/nodejs/node/pull/36551)
* \[[`258aa50986`](https://github.com/nodejs/node/commit/258aa50986)] - **tty**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#36771](https://github.com/nodejs/node/pull/36771)
* \[[`5cb8b16452`](https://github.com/nodejs/node/commit/5cb8b16452)] - **url**: fix url.format with ipv6 hostname (ZiJian Liu) [#36665](https://github.com/nodejs/node/pull/36665)
* \[[`b1c6a44caf`](https://github.com/nodejs/node/commit/b1c6a44caf)] - **url**: refactor to use more primordials (Antoine du Hamel) [#36316](https://github.com/nodejs/node/pull/36316)
* \[[`baa8064bd0`](https://github.com/nodejs/node/commit/baa8064bd0)] - **util**: refactor inspect.js to use more primodials (Rohan Chougule) [#36730](https://github.com/nodejs/node/pull/36730)
* \[[`bff201a66d`](https://github.com/nodejs/node/commit/bff201a66d)] - **util**: remove unreachable defensive coding (Rich Trott) [#36744](https://github.com/nodejs/node/pull/36744)
* \[[`64bf2f229e`](https://github.com/nodejs/node/commit/64bf2f229e)] - **util**: refactor to use more primordials (Antoine du Hamel) [#36265](https://github.com/nodejs/node/pull/36265)
* \[[`2dd2ec3836`](https://github.com/nodejs/node/commit/2dd2ec3836)] - **v8**: refactor to use more primordials (Antoine du Hamel) [#36527](https://github.com/nodejs/node/pull/36527)
* \[[`3170636a8e`](https://github.com/nodejs/node/commit/3170636a8e)] - **(SEMVER-MINOR)** **v8**: fix native `serdes` constructors (ExE Boss) [#36549](https://github.com/nodejs/node/pull/36549)
* \[[`d5a9799e76`](https://github.com/nodejs/node/commit/d5a9799e76)] - **wasi**: refactor to avoid unsafe array iteration (Antoine du Hamel) [#36724](https://github.com/nodejs/node/pull/36724)
* \[[`b6f74b0b09`](https://github.com/nodejs/node/commit/b6f74b0b09)] - **zlib**: refactor to use primordial instead of \<string>.startsWith (Rohan Chougule) [#36718](https://github.com/nodejs/node/pull/36718)

<a id="15.5.1"></a>

## 2021-01-04, Version 15.5.1 (Current), @BethGriggs

This is a security release.

### Notable changes

Vulnerabilities fixed:

* **CVE-2020-8265**: use-after-free in TLSWrap (High)
  * Affected Node.js versions are vulnerable to a use-after-free bug in
    its TLS implementation. When writing to a TLS enabled socket,
    node::StreamBase::Write calls node::TLSWrap::DoWrite with a freshly
    allocated WriteWrap object as first argument. If the DoWrite method
    does not return an error, this object is passed back to the caller as
    part of a StreamWriteResult structure. This may be exploited to
    corrupt memory leading to a Denial of Service or potentially other
    exploits.

* **CVE-2020-8287**: HTTP Request Smuggling in nodejs (Low)
  * Affected versions of Node.js allow two copies of a header field in
    a http request. For example, two Transfer-Encoding header fields. In
    this case Node.js identifies the first header field and ignores the
    second. This can lead to HTTP Request Smuggling
    (<https://cwe.mitre.org/data/definitions/444.html>).

### Commits

* \[[`c5dbe831b7`](https://github.com/nodejs/node/commit/c5dbe831b7)] - **http**: add test for http transfer encoding smuggling (Matteo Collina) [nodejs-private/node-private#228](https://github.com/nodejs-private/node-private/pull/228)
* \[[`e0c9a2285c`](https://github.com/nodejs/node/commit/e0c9a2285c)] - **http**: unset `F_CHUNKED` on new `Transfer-Encoding` (Matteo Collina) [nodejs-private/node-private#228](https://github.com/nodejs-private/node-private/pull/228)
* \[[`9834ef85a0`](https://github.com/nodejs/node/commit/9834ef85a0)] - **src**: retain pointers to WriteWrap/ShutdownWrap (James M Snell) [nodejs-private/node-private#23](https://github.com/nodejs-private/node-private/pull/23)

<a id="15.5.0"></a>

## 2020-12-22, Version 15.5.0 (Current), @targos

### Notable Changes

#### OpenSSL-1.1.1i

OpenSSL-1.1.1i contains a fix for CVE-2020-1971: OpenSSL - EDIPARTYNAME NULL pointer de-reference (High). This is a vulnerability in OpenSSL which may be exploited through Node.js. You can read more about it in <https://www.openssl.org/news/secadv/20201208.txt>

Contributed by Myles Borins [#36520](https://github.com/nodejs/node/pull/36520).

#### Extended support for `AbortSignal` in child\_process and stream

The following APIs now support an `AbortSignal` in their options object:

* `child_process.spawn()`

Calling `.abort()` on the corresponding `AbortController` is similar to calling `.kill()` on the child process except the error passed to the callback will be an `AbortError`:

```js
const controller = new AbortController();
const { signal } = controller;
const grep = spawn('grep', ['ssh'], { signal });
grep.on('error', (err) => {
  // This will be called with err being an AbortError if the controller aborts
});
controller.abort(); // stops the process
```

* `new stream.Writable()` and `new stream.Readable()`

Calling `.abort()` on the corresponding `AbortController` will behave the same way as calling `.destroy(new AbortError())` on the stream:

```js
const { Readable } = require('stream');
const controller = new AbortController();
const read = new Readable({
  read(size) {
    // ...
  },
  signal: controller.signal,
});
// Later, abort the operation closing the stream
controller.abort();
```

Contributed by Benjamin Gruenbaum [#36431](https://github.com/nodejs/node/pull/36431), [#36432](https://github.com/nodejs/node/pull/36432).

#### BigInt support in `querystring.stringify()`

If `querystring.stringify()` is called with an object that contains `BigInt` values, they will now be serialized to their decimal representation instead of the empty string:

```js
const querystring = require('querystring');
console.log(querystring.stringify({ bigint: 2n ** 64n }));
// Prints: bigint=18446744073709551616
```

Contributed by Darshan Sen [#36499](https://github.com/nodejs/node/pull/36499).

#### Additions to the C++ embedder APIs

A new `IsolateSettingsFlag` is available for those calling `SetIsolateUpForNode()`: `SHOULD_NOT_SET_PREPARE_STACK_TRACE_CALLBACK` can be used to prevent Node.js from setting a custom callback to prepare stack traces.

Contributed by Shelley Vohr [#36447](https://github.com/nodejs/node/pull/36447).

***

Added `node::GetEnvironmentIsolateData()` and `node::GetArrayBufferAllocator()` to respectively get the current `IsolateData*` and, from it, the current Node.js `ArrayBufferAllocator` if there is one.

Contributed by Anna Henningsen [#36441](https://github.com/nodejs/node/pull/36441).

#### New core collaborator

With this release, we welcome a new Node.js core collaborator:

* Pooja D P [@PoojaDurgad](https://github.com/PoojaDurgad) [#36511](https://github.com/nodejs/node/pull/36511)

### Commits

#### Semver-minor commits

* \[[`e449571230`](https://github.com/nodejs/node/commit/e449571230)] - **(SEMVER-MINOR)** **child\_process**: add signal support to spawn (Benjamin Gruenbaum) [#36432](https://github.com/nodejs/node/pull/36432)
* \[[`25d7e90386`](https://github.com/nodejs/node/commit/25d7e90386)] - **(SEMVER-MINOR)** **http**: use `autoDestroy: true` in incoming message (Daniele Belardi) [#33035](https://github.com/nodejs/node/pull/33035)
* \[[`5481be8cbd`](https://github.com/nodejs/node/commit/5481be8cbd)] - **(SEMVER-MINOR)** **lib**: support BigInt in querystring.stringify (raisinten) [#36499](https://github.com/nodejs/node/pull/36499)
* \[[`036ed1fafc`](https://github.com/nodejs/node/commit/036ed1fafc)] - **(SEMVER-MINOR)** **src**: add way to get IsolateData and allocator from Environment (Anna Henningsen) [#36441](https://github.com/nodejs/node/pull/36441)
* \[[`e23309486b`](https://github.com/nodejs/node/commit/e23309486b)] - **(SEMVER-MINOR)** **src**: allow preventing SetPrepareStackTraceCallback (Shelley Vohr) [#36447](https://github.com/nodejs/node/pull/36447)
* \[[`6ecbc1dcb3`](https://github.com/nodejs/node/commit/6ecbc1dcb3)] - **(SEMVER-MINOR)** **stream**: support abortsignal in constructor (Benjamin Gruenbaum) [#36431](https://github.com/nodejs/node/pull/36431)

#### Semver-patch commits

* \[[`1330995b80`](https://github.com/nodejs/node/commit/1330995b80)] - **build,lib,test**: change whitelist to allowlist (Michaël Zasso) [#36406](https://github.com/nodejs/node/pull/36406)
* \[[`dc8d1a74a6`](https://github.com/nodejs/node/commit/dc8d1a74a6)] - **deps**: upgrade npm to 7.3.0 (Ruy Adorno) [#36572](https://github.com/nodejs/node/pull/36572)
* \[[`b6a31f0a70`](https://github.com/nodejs/node/commit/b6a31f0a70)] - **deps**: update archs files for OpenSSL-1.1.1i (Myles Borins) [#36520](https://github.com/nodejs/node/pull/36520)
* \[[`5b49807c3f`](https://github.com/nodejs/node/commit/5b49807c3f)] - **deps**: re-enable OPENSSL\_NO\_QUIC guards (James M Snell) [#36520](https://github.com/nodejs/node/pull/36520)
* \[[`309e2971a2`](https://github.com/nodejs/node/commit/309e2971a2)] - **deps**: various quic patches from akamai/openssl (Todd Short) [#36520](https://github.com/nodejs/node/pull/36520)
* \[[`27fb651cbc`](https://github.com/nodejs/node/commit/27fb651cbc)] - **deps**: upgrade openssl sources to 1.1.1i (Myles Borins) [#36520](https://github.com/nodejs/node/pull/36520)
* \[[`1f43aadf90`](https://github.com/nodejs/node/commit/1f43aadf90)] - **deps**: update patch and docs for openssl update (Myles Borins) [#36520](https://github.com/nodejs/node/pull/36520)
* \[[`752c94d202`](https://github.com/nodejs/node/commit/752c94d202)] - **deps**: fix npm doctor tests for pre-release node (nlf) [#36543](https://github.com/nodejs/node/pull/36543)
* \[[`b0393fa2ed`](https://github.com/nodejs/node/commit/b0393fa2ed)] - **deps**: upgrade npm to 7.2.0 (Myles Borins) [#36543](https://github.com/nodejs/node/pull/36543)
* \[[`cb4652e91d`](https://github.com/nodejs/node/commit/cb4652e91d)] - **deps**: update to c-ares 1.17.1 (Danny Sonnenschein) [#36207](https://github.com/nodejs/node/pull/36207)
* \[[`21fbcb6f81`](https://github.com/nodejs/node/commit/21fbcb6f81)] - **deps**: V8: backport 4bf051d536a1 (Anna Henningsen) [#36482](https://github.com/nodejs/node/pull/36482)
* \[[`30fe0ff681`](https://github.com/nodejs/node/commit/30fe0ff681)] - **deps**: upgrade npm to 7.1.2 (Darcy Clarke) [#36487](https://github.com/nodejs/node/pull/36487)
* \[[`0baa610c3e`](https://github.com/nodejs/node/commit/0baa610c3e)] - **deps**: upgrade npm to 7.1.1 (Ruy Adorno) [#36459](https://github.com/nodejs/node/pull/36459)
* \[[`5929b08851`](https://github.com/nodejs/node/commit/5929b08851)] - **deps**: upgrade npm to 7.1.0 (Ruy Adorno) [#36395](https://github.com/nodejs/node/pull/36395)
* \[[`deaafd5788`](https://github.com/nodejs/node/commit/deaafd5788)] - **dns**: refactor to use more primordials (Antoine du Hamel) [#36314](https://github.com/nodejs/node/pull/36314)
* \[[`e30af7be33`](https://github.com/nodejs/node/commit/e30af7be33)] - **fs**: refactor to use optional chaining (ZiJian Liu) [#36524](https://github.com/nodejs/node/pull/36524)
* \[[`213dcd7930`](https://github.com/nodejs/node/commit/213dcd7930)] - **http**: add test for incomingmessage destroy (Daniele Belardi) [#33035](https://github.com/nodejs/node/pull/33035)
* \[[`36b4ddd382`](https://github.com/nodejs/node/commit/36b4ddd382)] - **http**: use standard args order in IncomingMEssage onError (Daniele Belardi) [#33035](https://github.com/nodejs/node/pull/33035)
* \[[`60b5e696fc`](https://github.com/nodejs/node/commit/60b5e696fc)] - **http**: remove trailing space (Daniele Belardi) [#33035](https://github.com/nodejs/node/pull/33035)
* \[[`f11a648d8e`](https://github.com/nodejs/node/commit/f11a648d8e)] - **http**: add comments in \_http\_incoming (Daniele Belardi) [#33035](https://github.com/nodejs/node/pull/33035)
* \[[`4b81d79b58`](https://github.com/nodejs/node/commit/4b81d79b58)] - **http**: fix lint error in incoming message (Daniele Belardi) [#33035](https://github.com/nodejs/node/pull/33035)
* \[[`397e31e25f`](https://github.com/nodejs/node/commit/397e31e25f)] - **http**: reafactor incoming message destroy (Daniele Belardi) [#33035](https://github.com/nodejs/node/pull/33035)
* \[[`9852ebca8d`](https://github.com/nodejs/node/commit/9852ebca8d)] - **http**: do not loop over prototype in Agent (Michaël Zasso) [#36410](https://github.com/nodejs/node/pull/36410)
* \[[`e46a46a4cd`](https://github.com/nodejs/node/commit/e46a46a4cd)] - **inspector**: refactor to use more primordials (Antoine du Hamel) [#36356](https://github.com/nodejs/node/pull/36356)
* \[[`728f512c7d`](https://github.com/nodejs/node/commit/728f512c7d)] - **lib**: make safe primordials safe to iterate (Antoine du Hamel) [#36391](https://github.com/nodejs/node/pull/36391)
* \[[`f368d697cf`](https://github.com/nodejs/node/commit/f368d697cf)] - _**Revert**_ "**perf\_hooks**: make PerformanceObserver an AsyncResource" (Nicolai Stange) [#36343](https://github.com/nodejs/node/pull/36343)
* \[[`e2ced0d401`](https://github.com/nodejs/node/commit/e2ced0d401)] - **perf\_hooks**: invoke performance\_entry\_callback via MakeSyncCallback() (Nicolai Stange) [#36343](https://github.com/nodejs/node/pull/36343)
* \[[`7c903ec6c8`](https://github.com/nodejs/node/commit/7c903ec6c8)] - **repl**: disable blocking completions by default (Anna Henningsen) [#36564](https://github.com/nodejs/node/pull/36564)
* \[[`d38a0ec93e`](https://github.com/nodejs/node/commit/d38a0ec93e)] - **src**: remove unnecessary ToLocalChecked node\_errors (Daniel Bevenius) [#36547](https://github.com/nodejs/node/pull/36547)
* \[[`bbc0d14cd2`](https://github.com/nodejs/node/commit/bbc0d14cd2)] - **src**: use correct microtask queue for checkpoints (Anna Henningsen) [#36581](https://github.com/nodejs/node/pull/36581)
* \[[`7efb3111e8`](https://github.com/nodejs/node/commit/7efb3111e8)] - **src**: remove unnecessary ToLocalChecked call (Daniel Bevenius) [#36523](https://github.com/nodejs/node/pull/36523)
* \[[`68687d3419`](https://github.com/nodejs/node/commit/68687d3419)] - **src**: remove empty name check in node\_env\_var.cc (raisinten) [#36133](https://github.com/nodejs/node/pull/36133)
* \[[`1b4984de98`](https://github.com/nodejs/node/commit/1b4984de98)] - **src**: remove duplicate V macros in node\_v8.cc (Daniel Bevenius) [#36454](https://github.com/nodejs/node/pull/36454)
* \[[`5ff7f42e65`](https://github.com/nodejs/node/commit/5ff7f42e65)] - **src**: use correct outer Context’s microtask queue (Anna Henningsen) [#36482](https://github.com/nodejs/node/pull/36482)
* \[[`96c095f237`](https://github.com/nodejs/node/commit/96c095f237)] - **src**: guard against env != null in node\_errors.cc (Anna Henningsen) [#36414](https://github.com/nodejs/node/pull/36414)
* \[[`4f3d7bb417`](https://github.com/nodejs/node/commit/4f3d7bb417)] - **src**: introduce convenience node::MakeSyncCallback() (Nicolai Stange) [#36343](https://github.com/nodejs/node/pull/36343)
* \[[`e59788262c`](https://github.com/nodejs/node/commit/e59788262c)] - **src**: add typedef for CleanupHookCallback callback (Daniel Bevenius) [#36442](https://github.com/nodejs/node/pull/36442)
* \[[`2a60e3b9df`](https://github.com/nodejs/node/commit/2a60e3b9df)] - **src**: fix indentation in memory\_tracker-inl.h (Daniel Bevenius) [#36425](https://github.com/nodejs/node/pull/36425)
* \[[`210390f6fd`](https://github.com/nodejs/node/commit/210390f6fd)] - **src**: remove identical V macro (Daniel Bevenius) [#36427](https://github.com/nodejs/node/pull/36427)
* \[[`02afe586aa`](https://github.com/nodejs/node/commit/02afe586aa)] - **src**: use using declarations consistently (Daniel Bevenius) [#36365](https://github.com/nodejs/node/pull/36365)
* \[[`169406b7d7`](https://github.com/nodejs/node/commit/169406b7d7)] - **src**: add missing context scopes (Anna Henningsen) [#36413](https://github.com/nodejs/node/pull/36413)
* \[[`3f33d0bcda`](https://github.com/nodejs/node/commit/3f33d0bcda)] - **stream**: fix pipe deadlock when starting with needDrain (Robert Nagy) [#36563](https://github.com/nodejs/node/pull/36563)
* \[[`d8b5b9499c`](https://github.com/nodejs/node/commit/d8b5b9499c)] - **stream**: accept iterable as a valid first argument (ZiJian Liu) [#36479](https://github.com/nodejs/node/pull/36479)
* \[[`58319d5336`](https://github.com/nodejs/node/commit/58319d5336)] - **tls**: forward new SecureContext options (Alba Mendez) [#36416](https://github.com/nodejs/node/pull/36416)
* \[[`fa40366276`](https://github.com/nodejs/node/commit/fa40366276)] - **util**: simplify constructor retrieval in inspect() (Rich Trott) [#36466](https://github.com/nodejs/node/pull/36466)
* \[[`cc544dbfaa`](https://github.com/nodejs/node/commit/cc544dbfaa)] - **util**: fix instanceof checks with null prototypes during inspection (Ruben Bridgewater) [#36178](https://github.com/nodejs/node/pull/36178)
* \[[`13d6597b4b`](https://github.com/nodejs/node/commit/13d6597b4b)] - **util**: fix module prefixes during inspection (Ruben Bridgewater) [#36178](https://github.com/nodejs/node/pull/36178)
* \[[`20ecc82569`](https://github.com/nodejs/node/commit/20ecc82569)] - **worker**: fix broadcast channel SharedArrayBuffer passing (Anna Henningsen) [#36501](https://github.com/nodejs/node/pull/36501)
* \[[`56fe9bae26`](https://github.com/nodejs/node/commit/56fe9bae26)] - **worker**: refactor MessagePort entanglement management (Anna Henningsen) [#36345](https://github.com/nodejs/node/pull/36345)

#### Documentation commits

* \[[`19c233232f`](https://github.com/nodejs/node/commit/19c233232f)] - **doc**: fix AbortSignal example for stream.Readable (Michaël Zasso) [#36596](https://github.com/nodejs/node/pull/36596)
* \[[`9fbab3e2f5`](https://github.com/nodejs/node/commit/9fbab3e2f5)] - **doc**: update and run license-builder for Babel (Michaël Zasso) [#36504](https://github.com/nodejs/node/pull/36504)
* \[[`a1ba6686a0`](https://github.com/nodejs/node/commit/a1ba6686a0)] - **doc**: add remark about Collaborators discussion page (FrankQiu) [#36420](https://github.com/nodejs/node/pull/36420)
* \[[`c5602fb166`](https://github.com/nodejs/node/commit/c5602fb166)] - **doc**: simplify worker\_threads.md text (Rich Trott) [#36545](https://github.com/nodejs/node/pull/36545)
* \[[`149f2cfac1`](https://github.com/nodejs/node/commit/149f2cfac1)] - **doc**: add two tips for speeding the dev builds (Momtchil Momtchev) [#36452](https://github.com/nodejs/node/pull/36452)
* \[[`ad75c78c32`](https://github.com/nodejs/node/commit/ad75c78c32)] - **doc**: add note about timingSafeEqual for TypedArray (Tobias Nießen) [#36323](https://github.com/nodejs/node/pull/36323)
* \[[`9830fe5c9e`](https://github.com/nodejs/node/commit/9830fe5c9e)] - **doc**: move Derek Lewis to emeritus (Rich Trott) [#36514](https://github.com/nodejs/node/pull/36514)
* \[[`eb29a16bae`](https://github.com/nodejs/node/commit/eb29a16bae)] - **doc**: add issue reference to github pr template (Chinmoy Chakraborty) [#36440](https://github.com/nodejs/node/pull/36440)
* \[[`f09985d42a`](https://github.com/nodejs/node/commit/f09985d42a)] - **doc**: update url.md (Rock) [#36147](https://github.com/nodejs/node/pull/36147)
* \[[`c3ec90d23c`](https://github.com/nodejs/node/commit/c3ec90d23c)] - **doc**: make explicit reverting node\_version.h changes (Richard Lau) [#36461](https://github.com/nodejs/node/pull/36461)
* \[[`7a34452b1d`](https://github.com/nodejs/node/commit/7a34452b1d)] - **doc**: add license info to the README (FrankQiu) [#36278](https://github.com/nodejs/node/pull/36278)
* \[[`22f039339f`](https://github.com/nodejs/node/commit/22f039339f)] - **doc**: revise addon mulitple initializations text (Rich Trott) [#36457](https://github.com/nodejs/node/pull/36457)
* \[[`25a245443a`](https://github.com/nodejs/node/commit/25a245443a)] - **doc**: add v15.4.0 link to CHANGELOG.md (Danielle Adams) [#36456](https://github.com/nodejs/node/pull/36456)
* \[[`1ec8516fd6`](https://github.com/nodejs/node/commit/1ec8516fd6)] - **doc**: add PoojaDurgad to collaborators (Pooja D P) [#36511](https://github.com/nodejs/node/pull/36511)
* \[[`98918110a1`](https://github.com/nodejs/node/commit/98918110a1)] - **doc**: edit addon text about event loop blocking (Rich Trott) [#36448](https://github.com/nodejs/node/pull/36448)
* \[[`62bfe3d313`](https://github.com/nodejs/node/commit/62bfe3d313)] - **doc**: note v15.0.0 changed default --unhandled-rejections=throw (kai zhu) [#36361](https://github.com/nodejs/node/pull/36361)
* \[[`129053fe4c`](https://github.com/nodejs/node/commit/129053fe4c)] - **doc**: update terminology (Michael Dawson) [#36475](https://github.com/nodejs/node/pull/36475)
* \[[`e331de2571`](https://github.com/nodejs/node/commit/e331de2571)] - **doc**: reword POSIX threads text in addons.md (Rich Trott) [#36436](https://github.com/nodejs/node/pull/36436)
* \[[`04f166389b`](https://github.com/nodejs/node/commit/04f166389b)] - **doc**: add RaisinTen as a triager (raisinten) [#36404](https://github.com/nodejs/node/pull/36404)
* \[[`3341b2cb9d`](https://github.com/nodejs/node/commit/3341b2cb9d)] - **doc**: document ABORT\_ERR code (Benjamin Gruenbaum) [#36319](https://github.com/nodejs/node/pull/36319)
* \[[`6a6b3af736`](https://github.com/nodejs/node/commit/6a6b3af736)] - **doc**: provide more context on techinical values (Michael Dawson) [#36201](https://github.com/nodejs/node/pull/36201)

#### Other commits

* \[[`e1f00fd996`](https://github.com/nodejs/node/commit/e1f00fd996)] - **benchmark**: reduce code duplication (Rich Trott) [#36568](https://github.com/nodejs/node/pull/36568)
* \[[`82a26268d7`](https://github.com/nodejs/node/commit/82a26268d7)] - **build**: do not run GitHub actions for draft PRs (Michaël Zasso) [#35910](https://github.com/nodejs/node/pull/35910)
* \[[`95c80f5fb0`](https://github.com/nodejs/node/commit/95c80f5fb0)] - **build**: run some workflows only on nodejs/node (Michaël Zasso) [#36507](https://github.com/nodejs/node/pull/36507)
* \[[`584ea8b26c`](https://github.com/nodejs/node/commit/584ea8b26c)] - **build**: fix make test-npm (Ruy Adorno) [#36369](https://github.com/nodejs/node/pull/36369)
* \[[`01576fbc19`](https://github.com/nodejs/node/commit/01576fbc19)] - **test**: increase abort logic coverage (Moshe vilner) [#36586](https://github.com/nodejs/node/pull/36586)
* \[[`22ac2279ee`](https://github.com/nodejs/node/commit/22ac2279ee)] - **test**: increase coverage for stream (ZiJian Liu) [#36538](https://github.com/nodejs/node/pull/36538)
* \[[`9fc2479707`](https://github.com/nodejs/node/commit/9fc2479707)] - **test**: increase coverage for worker (ZiJian Liu) [#36491](https://github.com/nodejs/node/pull/36491)
* \[[`81e603b7cf`](https://github.com/nodejs/node/commit/81e603b7cf)] - **test**: specify global object for globals (Rich Trott) [#36498](https://github.com/nodejs/node/pull/36498)
* \[[`109ab787fd`](https://github.com/nodejs/node/commit/109ab787fd)] - **test**: increase coverage for fs/dir read (Zijian Liu) [#36388](https://github.com/nodejs/node/pull/36388)
* \[[`9f2d3c291b`](https://github.com/nodejs/node/commit/9f2d3c291b)] - **test**: remove test-http2-client-upload as flaky (Rich Trott) [#36496](https://github.com/nodejs/node/pull/36496)
* \[[`d299ceeac7`](https://github.com/nodejs/node/commit/d299ceeac7)] - **test**: increase coverage for net/blocklist (Zijian Liu) [#36405](https://github.com/nodejs/node/pull/36405)
* \[[`f7635fd86d`](https://github.com/nodejs/node/commit/f7635fd86d)] - **test**: make executable name more general (Shelley Vohr) [#36489](https://github.com/nodejs/node/pull/36489)
* \[[`acd78d9d25`](https://github.com/nodejs/node/commit/acd78d9d25)] - **test**: increased externalized string length (Shelley Vohr) [#36451](https://github.com/nodejs/node/pull/36451)
* \[[`0f749a35ec`](https://github.com/nodejs/node/commit/0f749a35ec)] - **test**: add test for async contexts in PerformanceObserver (ZauberNerd) [#36343](https://github.com/nodejs/node/pull/36343)
* \[[`dd705ad1f0`](https://github.com/nodejs/node/commit/dd705ad1f0)] - **test**: increase execFile abort coverage (Moshe vilner) [#36429](https://github.com/nodejs/node/pull/36429)
* \[[`31b062d591`](https://github.com/nodejs/node/commit/31b062d591)] - **test**: fix flaky test-repl (Rich Trott) [#36415](https://github.com/nodejs/node/pull/36415)
* \[[`023291b43c`](https://github.com/nodejs/node/commit/023291b43c)] - **test**: check null proto-of-proto in util.inspect() (Rich Trott) [#36399](https://github.com/nodejs/node/pull/36399)
* \[[`d3d1f338c7`](https://github.com/nodejs/node/commit/d3d1f338c7)] - **test**: add SIGTRAP to test-signal-handler (Ash Cripps) [#36368](https://github.com/nodejs/node/pull/36368)
* \[[`166aa8a7b5`](https://github.com/nodejs/node/commit/166aa8a7b5)] - **test**: fix child-process-pipe-dataflow (Santiago Gimeno) [#36366](https://github.com/nodejs/node/pull/36366)
* \[[`ecbb757ae0`](https://github.com/nodejs/node/commit/ecbb757ae0)] - **tools**: fix make-v8.sh (Richard Lau) [#36594](https://github.com/nodejs/node/pull/36594)
* \[[`e3c5adc6d0`](https://github.com/nodejs/node/commit/e3c5adc6d0)] - **tools**: fix release script sign function (Antoine du Hamel) [#36556](https://github.com/nodejs/node/pull/36556)
* \[[`0d4d34748d`](https://github.com/nodejs/node/commit/0d4d34748d)] - **tools**: update ESLint to 7.16.0 (Yongsheng Zhang) [#36579](https://github.com/nodejs/node/pull/36579)
* \[[`f3828c9dcb`](https://github.com/nodejs/node/commit/f3828c9dcb)] - **tools**: fix update-eslint.sh (Yongsheng Zhang) [#36579](https://github.com/nodejs/node/pull/36579)
* \[[`27260c70b4`](https://github.com/nodejs/node/commit/27260c70b4)] - **tools**: fix release script (Antoine du Hamel) [#36540](https://github.com/nodejs/node/pull/36540)
* \[[`c6700ad041`](https://github.com/nodejs/node/commit/c6700ad041)] - **tools**: remove unused variable in configure.py (Rich Trott) [#36525](https://github.com/nodejs/node/pull/36525)
* \[[`7b8d373d5e`](https://github.com/nodejs/node/commit/7b8d373d5e)] - **tools**: lint shell scripts (Antoine du Hamel) [#36099](https://github.com/nodejs/node/pull/36099)
* \[[`c6e65d09ef`](https://github.com/nodejs/node/commit/c6e65d09ef)] - **tools**: update ini in tools/node-lint-md-cli-rollup (Myles Borins) [#36474](https://github.com/nodejs/node/pull/36474)
* \[[`7542a3bd55`](https://github.com/nodejs/node/commit/7542a3bd55)] - **tools**: enable no-unsafe-optional-chaining lint rule (Colin Ihrig) [#36411](https://github.com/nodejs/node/pull/36411)
* \[[`26f8ccfbe6`](https://github.com/nodejs/node/commit/26f8ccfbe6)] - **tools**: update ESLint to 7.15.0 (Colin Ihrig) [#36411](https://github.com/nodejs/node/pull/36411)
* \[[`8ecf2f9976`](https://github.com/nodejs/node/commit/8ecf2f9976)] - **tools**: update doc tool dependencies (Michaël Zasso) [#36407](https://github.com/nodejs/node/pull/36407)
* \[[`040b39f076`](https://github.com/nodejs/node/commit/040b39f076)] - **tools**: enable no-unused-expressions lint rule (Michaël Zasso) [#36248](https://github.com/nodejs/node/pull/36248)

<a id="15.4.0"></a>

## 2020-12-09, Version 15.4.0 (Current), @danielleadams

### Notable Changes

* **child\_processes**:
  * add AbortSignal support (Benjamin Gruenbaum) [#36308](https://github.com/nodejs/node/pull/36308)
* **deps**:
  * update ICU to 68.1 (Michaël Zasso) [#36187](https://github.com/nodejs/node/pull/36187)
* **events**:
  * support signal in EventTarget (Benjamin Gruenbaum) [#36258](https://github.com/nodejs/node/pull/36258)
  * graduate Event, EventTarget, AbortController (James M Snell) [#35949](https://github.com/nodejs/node/pull/35949)
* **http**:
  * enable call chaining with setHeader() (pooja d.p) [#35924](https://github.com/nodejs/node/pull/35924)
* **module**:
  * add isPreloading indicator (James M Snell) [#36263](https://github.com/nodejs/node/pull/36263)
* **stream**:
  * support abort signal (Benjamin Gruenbaum) [#36061](https://github.com/nodejs/node/pull/36061)
  * add FileHandle support to Read/WriteStream (Momtchil Momtchev) [#35922](https://github.com/nodejs/node/pull/35922)
* **worker**:
  * add experimental BroadcastChannel (James M Snell) [#36271](https://github.com/nodejs/node/pull/36271)

### Commits

* \[[`e79bdc313a`](https://github.com/nodejs/node/commit/e79bdc313a)] - **assert**: refactor to use more primordials (Antoine du Hamel) [#36234](https://github.com/nodejs/node/pull/36234)
* \[[`2344e3e360`](https://github.com/nodejs/node/commit/2344e3e360)] - **benchmark**: changed `fstat` to `fstatSync` (Narasimha Prasanna HN) [#36206](https://github.com/nodejs/node/pull/36206)
* \[[`ca8db41151`](https://github.com/nodejs/node/commit/ca8db41151)] - **benchmark,child\_process**: remove failing benchmark parameter (Antoine du Hamel) [#36295](https://github.com/nodejs/node/pull/36295)
* \[[`9db9be774b`](https://github.com/nodejs/node/commit/9db9be774b)] - **buffer**: refactor to use primordials instead of Array#reduce (Antoine du Hamel) [#36392](https://github.com/nodejs/node/pull/36392)
* \[[`8d8d2261a5`](https://github.com/nodejs/node/commit/8d8d2261a5)] - **buffer**: refactor to use more primordials (Antoine du Hamel) [#36166](https://github.com/nodejs/node/pull/36166)
* \[[`74adc441c4`](https://github.com/nodejs/node/commit/74adc441c4)] - **build**: fix typo in Makefile (raisinten) [#36176](https://github.com/nodejs/node/pull/36176)
* \[[`224a6471cc`](https://github.com/nodejs/node/commit/224a6471cc)] - **(SEMVER-MINOR)** **child\_process**: add AbortSignal support (Benjamin Gruenbaum) [#36308](https://github.com/nodejs/node/pull/36308)
* \[[`4ca1bd8806`](https://github.com/nodejs/node/commit/4ca1bd8806)] - **child\_process**: refactor to use more primordials (Zijian Liu) [#36269](https://github.com/nodejs/node/pull/36269)
* \[[`841e8f444e`](https://github.com/nodejs/node/commit/841e8f444e)] - **crypto**: fix "Invalid JWK" error messages (Filip Skokan) [#36200](https://github.com/nodejs/node/pull/36200)
* \[[`278862aeb9`](https://github.com/nodejs/node/commit/278862aeb9)] - **deps**: upgrade npm to 7.0.15 (Ruy Adorno) [#36293](https://github.com/nodejs/node/pull/36293)
* \[[`66bc2067ce`](https://github.com/nodejs/node/commit/66bc2067ce)] - **deps**: V8: cherry-pick 86991d0587a1 (Benjamin Coe) [#36254](https://github.com/nodejs/node/pull/36254)
* \[[`095cef2c11`](https://github.com/nodejs/node/commit/095cef2c11)] - **deps**: update ICU to 68.1 (Michaël Zasso) [#36187](https://github.com/nodejs/node/pull/36187)
* \[[`8d69d8387e`](https://github.com/nodejs/node/commit/8d69d8387e)] - **dgram**: refactor to use more primordials (Antoine du Hamel) [#36286](https://github.com/nodejs/node/pull/36286)
* \[[`bef550a50c`](https://github.com/nodejs/node/commit/bef550a50c)] - **doc**: add Powershell oneliner to get Windows version (Michael Bashurov) [#30289](https://github.com/nodejs/node/pull/30289)
* \[[`2649c384c6`](https://github.com/nodejs/node/commit/2649c384c6)] - **doc**: add version metadata to timers/promises (Colin Ihrig) [#36378](https://github.com/nodejs/node/pull/36378)
* \[[`0401ffbfb6`](https://github.com/nodejs/node/commit/0401ffbfb6)] - **doc**: add process for handling premature disclosure (Michael Dawson) [#36155](https://github.com/nodejs/node/pull/36155)
* \[[`3e5fcda13e`](https://github.com/nodejs/node/commit/3e5fcda13e)] - **doc**: add table header in intl.md (Rich Trott) [#36261](https://github.com/nodejs/node/pull/36261)
* \[[`65d89fdd69`](https://github.com/nodejs/node/commit/65d89fdd69)] - **doc**: adding example to Buffer.isBuffer method (naortedgi) [#36233](https://github.com/nodejs/node/pull/36233)
* \[[`03cf8dbc0e`](https://github.com/nodejs/node/commit/03cf8dbc0e)] - **doc**: fix typo in events.md (Luigi Pinca) [#36231](https://github.com/nodejs/node/pull/36231)
* \[[`b176d61e8c`](https://github.com/nodejs/node/commit/b176d61e8c)] - **doc**: fix --experimental-wasm-modules text location (Colin Ihrig) [#36220](https://github.com/nodejs/node/pull/36220)
* \[[`44c4aaddad`](https://github.com/nodejs/node/commit/44c4aaddad)] - **doc**: stabilize subpath patterns (Guy Bedford) [#36177](https://github.com/nodejs/node/pull/36177)
* \[[`fdf5d851d0`](https://github.com/nodejs/node/commit/fdf5d851d0)] - **doc**: add missing version to update cmd (Ruy Adorno) [#36204](https://github.com/nodejs/node/pull/36204)
* \[[`186ad24fdf`](https://github.com/nodejs/node/commit/186ad24fdf)] - **doc**: cleanup events.md structure (James M Snell) [#36100](https://github.com/nodejs/node/pull/36100)
* \[[`c14512b9a5`](https://github.com/nodejs/node/commit/c14512b9a5)] - **errors**: display original symbol name (Benjamin Coe) [#36042](https://github.com/nodejs/node/pull/36042)
* \[[`855a85c124`](https://github.com/nodejs/node/commit/855a85c124)] - **(SEMVER-MINOR)** **events**: support signal in EventTarget (Benjamin Gruenbaum) [#36258](https://github.com/nodejs/node/pull/36258)
* \[[`dc1930923b`](https://github.com/nodejs/node/commit/dc1930923b)] - **(SEMVER-MINOR)** **events**: graduate Event, EventTarget, AbortController (James M Snell) [#35949](https://github.com/nodejs/node/pull/35949)
* \[[`537e5cbf51`](https://github.com/nodejs/node/commit/537e5cbf51)] - **fs**: move method definition from header (Yash Ladha) [#36256](https://github.com/nodejs/node/pull/36256)
* \[[`744b8aa807`](https://github.com/nodejs/node/commit/744b8aa807)] - **fs**: pass ERR\_DIR\_CLOSED asynchronously to dir.close (Zijian Liu) [#36243](https://github.com/nodejs/node/pull/36243)
* \[[`c04a2df185`](https://github.com/nodejs/node/commit/c04a2df185)] - **fs**: refactor to use more primordials (Antoine du Hamel) [#36196](https://github.com/nodejs/node/pull/36196)
* \[[`58abdcaceb`](https://github.com/nodejs/node/commit/58abdcaceb)] - **(SEMVER-MINOR)** **http**: enable call chaining with setHeader() (pooja d.p) [#35924](https://github.com/nodejs/node/pull/35924)
* \[[`cedf51f3ce`](https://github.com/nodejs/node/commit/cedf51f3ce)] - **http2**: refactor to use more primordials (Antoine du Hamel) [#36357](https://github.com/nodejs/node/pull/36357)
* \[[`5f41f1b19e`](https://github.com/nodejs/node/commit/5f41f1b19e)] - **http2**: check write not scheduled in scope destructor (David Halls) [#36241](https://github.com/nodejs/node/pull/36241)
* \[[`4127eb2405`](https://github.com/nodejs/node/commit/4127eb2405)] - **https**: add abortcontroller test (Benjamin Gruenbaum) [#36307](https://github.com/nodejs/node/pull/36307)
* \[[`c2938bde6c`](https://github.com/nodejs/node/commit/c2938bde6c)] - **lib**: add uncurried accessor properties to `primordials` (ExE Boss) [#36329](https://github.com/nodejs/node/pull/36329)
* \[[`f73a0a8069`](https://github.com/nodejs/node/commit/f73a0a8069)] - **lib**: fix typo in internal/errors.js (raisinten) [#36426](https://github.com/nodejs/node/pull/36426)
* \[[`617cb58cc8`](https://github.com/nodejs/node/commit/617cb58cc8)] - **lib**: refactor primordials.uncurryThis (Antoine du Hamel) [#36221](https://github.com/nodejs/node/pull/36221)
* \[[`cc18907ec4`](https://github.com/nodejs/node/commit/cc18907ec4)] - **module**: refactor to use more primordials (Antoine du Hamel) [#36348](https://github.com/nodejs/node/pull/36348)
* \[[`d4de7c7eb9`](https://github.com/nodejs/node/commit/d4de7c7eb9)] - **(SEMVER-MINOR)** **module**: add isPreloading indicator (James M Snell) [#36263](https://github.com/nodejs/node/pull/36263)
* \[[`8611b8f98a`](https://github.com/nodejs/node/commit/8611b8f98a)] - **net**: refactor to use more primordials (Antoine du Hamel) [#36303](https://github.com/nodejs/node/pull/36303)
* \[[`2a24096720`](https://github.com/nodejs/node/commit/2a24096720)] - **os**: refactor to use more primordials (Antoine du Hamel) [#36284](https://github.com/nodejs/node/pull/36284)
* \[[`0e7f0c6d27`](https://github.com/nodejs/node/commit/0e7f0c6d27)] - **path**: refactor to use more primordials (Antoine du Hamel) [#36302](https://github.com/nodejs/node/pull/36302)
* \[[`ea46ca8cbf`](https://github.com/nodejs/node/commit/ea46ca8cbf)] - **perf\_hooks**: refactor to use more primordials (Antoine du Hamel) [#36297](https://github.com/nodejs/node/pull/36297)
* \[[`a9ac86d1ee`](https://github.com/nodejs/node/commit/a9ac86d1ee)] - **policy**: refactor to use more primordials (Antoine du Hamel) [#36210](https://github.com/nodejs/node/pull/36210)
* \[[`39d0ceda48`](https://github.com/nodejs/node/commit/39d0ceda48)] - **process**: refactor to use more primordials (Antoine du Hamel) [#36212](https://github.com/nodejs/node/pull/36212)
* \[[`ab084c199e`](https://github.com/nodejs/node/commit/ab084c199e)] - **querystring**: refactor to use more primordials (Antoine du Hamel) [#36315](https://github.com/nodejs/node/pull/36315)
* \[[`d29199ef82`](https://github.com/nodejs/node/commit/d29199ef82)] - **quic**: refactor to use more primordials (Antoine du Hamel) [#36211](https://github.com/nodejs/node/pull/36211)
* \[[`b885409e48`](https://github.com/nodejs/node/commit/b885409e48)] - **readline**: refactor to use more primordials (Antoine du Hamel) [#36296](https://github.com/nodejs/node/pull/36296)
* \[[`9cb53f635a`](https://github.com/nodejs/node/commit/9cb53f635a)] - **repl**: refactor to use more primordials (Antoine du Hamel) [#36264](https://github.com/nodejs/node/pull/36264)
* \[[`8dadaa652e`](https://github.com/nodejs/node/commit/8dadaa652e)] - **src**: remove some duplication in DeserializeProps (Daniel Bevenius) [#36336](https://github.com/nodejs/node/pull/36336)
* \[[`a03aa0a6b2`](https://github.com/nodejs/node/commit/a03aa0a6b2)] - **src**: rename AliasedBufferInfo->AliasedBufferIndex (Daniel Bevenius) [#36339](https://github.com/nodejs/node/pull/36339)
* \[[`e7b2d91e04`](https://github.com/nodejs/node/commit/e7b2d91e04)] - **src**: use transferred consistently (Daniel Bevenius) [#36340](https://github.com/nodejs/node/pull/36340)
* \[[`6ebb98af11`](https://github.com/nodejs/node/commit/6ebb98af11)] - **src**: use ToLocal in DeserializeProperties (Daniel Bevenius) [#36279](https://github.com/nodejs/node/pull/36279)
* \[[`47397ffd56`](https://github.com/nodejs/node/commit/47397ffd56)] - **src**: update node.rc file description (devsnek) [#36197](https://github.com/nodejs/node/pull/36197)
* \[[`cfc8ec18db`](https://github.com/nodejs/node/commit/cfc8ec18db)] - **src**: fix label indentation (Rich Trott) [#36213](https://github.com/nodejs/node/pull/36213)
* \[[`197ba21279`](https://github.com/nodejs/node/commit/197ba21279)] - **(SEMVER-MINOR)** **stream**: support abort signal (Benjamin Gruenbaum) [#36061](https://github.com/nodejs/node/pull/36061)
* \[[`6033d30361`](https://github.com/nodejs/node/commit/6033d30361)] - **(SEMVER-MINOR)** **stream**: add FileHandle support to Read/WriteStream (Momtchil Momtchev) [#35922](https://github.com/nodejs/node/pull/35922)
* \[[`a15addc153`](https://github.com/nodejs/node/commit/a15addc153)] - **string\_decoder**: refactor to use more primordials (Antoine du Hamel) [#36358](https://github.com/nodejs/node/pull/36358)
* \[[`b39d150e60`](https://github.com/nodejs/node/commit/b39d150e60)] - **test**: fix comment misspellings of transferred (Rich Trott) [#36360](https://github.com/nodejs/node/pull/36360)
* \[[`a7e794d1bf`](https://github.com/nodejs/node/commit/a7e794d1bf)] - **test**: fix flaky test-http2-respond-file-error-pipe-offset (Rich Trott) [#36305](https://github.com/nodejs/node/pull/36305)
* \[[`1091a658e1`](https://github.com/nodejs/node/commit/1091a658e1)] - **test**: fix bootstrap test (Benjamin Gruenbaum) [#36418](https://github.com/nodejs/node/pull/36418)
* \[[`fbcb72a665`](https://github.com/nodejs/node/commit/fbcb72a665)] - **test**: increase coverage for readline (Zijian Liu) [#36389](https://github.com/nodejs/node/pull/36389)
* \[[`22028aae54`](https://github.com/nodejs/node/commit/22028aae54)] - **test**: skip flaky parts of broadcastchannel test on Windows (Rich Trott) [#36386](https://github.com/nodejs/node/pull/36386)
* \[[`faca2b829e`](https://github.com/nodejs/node/commit/faca2b829e)] - **test**: fix test-worker-broadcastchannel-wpt (Rich Trott) [#36353](https://github.com/nodejs/node/pull/36353)
* \[[`ea09da492c`](https://github.com/nodejs/node/commit/ea09da492c)] - **test**: fix typo in comment (inokawa) [#36312](https://github.com/nodejs/node/pull/36312)
* \[[`b61ca1bfe6`](https://github.com/nodejs/node/commit/b61ca1bfe6)] - **test**: replace anonymous functions by arrows (Aleksandr Krutko) [#36125](https://github.com/nodejs/node/pull/36125)
* \[[`2c7358ef43`](https://github.com/nodejs/node/commit/2c7358ef43)] - **test**: fix flaky sequential/test-fs-watch (Rich Trott) [#36249](https://github.com/nodejs/node/pull/36249)
* \[[`b613950016`](https://github.com/nodejs/node/commit/b613950016)] - **test**: increase coverage for util.inspect() (Rich Trott) [#36228](https://github.com/nodejs/node/pull/36228)
* \[[`69a8f05488`](https://github.com/nodejs/node/commit/69a8f05488)] - **test**: improve test coverage SourceMap API (Juan José Arboleda) [#36089](https://github.com/nodejs/node/pull/36089)
* \[[`44d6d0bf0d`](https://github.com/nodejs/node/commit/44d6d0bf0d)] - **test**: fix missed warning for non-experimental AbortController (James M Snell) [#36240](https://github.com/nodejs/node/pull/36240)
* \[[`29b5236256`](https://github.com/nodejs/node/commit/29b5236256)] - **timers**: reject with AbortError on cancellation (Benjamin Gruenbaum) [#36317](https://github.com/nodejs/node/pull/36317)
* \[[`b20409e985`](https://github.com/nodejs/node/commit/b20409e985)] - **tls**: refactor to use more primordials (Antoine du Hamel) [#36266](https://github.com/nodejs/node/pull/36266)
* \[[`f317bba034`](https://github.com/nodejs/node/commit/f317bba034)] - **tls**: permit null as a cipher value (Rich Trott) [#36318](https://github.com/nodejs/node/pull/36318)
* \[[`9ae59c847a`](https://github.com/nodejs/node/commit/9ae59c847a)] - **tools**: upgrade to @babel/eslint-parser 7.12.1 (Antoine du Hamel) [#36321](https://github.com/nodejs/node/pull/36321)
* \[[`e798770803`](https://github.com/nodejs/node/commit/e798770803)] - **tools**: refloat 7 Node.js patches to cpplint.py (Rich Trott) [#36324](https://github.com/nodejs/node/pull/36324)
* \[[`a8b95cfcb2`](https://github.com/nodejs/node/commit/a8b95cfcb2)] - **tools**: bump cpplint to 1.5.4 (Rich Trott) [#36324](https://github.com/nodejs/node/pull/36324)
* \[[`754b7a76b1`](https://github.com/nodejs/node/commit/754b7a76b1)] - **tools**: remove bashisms from macOS release scripts (Antoine du Hamel) [#36121](https://github.com/nodejs/node/pull/36121)
* \[[`2868ffb331`](https://github.com/nodejs/node/commit/2868ffb331)] - **tools**: remove bashisms from release script (Antoine du Hamel) [#36123](https://github.com/nodejs/node/pull/36123)
* \[[`8cf1addaa8`](https://github.com/nodejs/node/commit/8cf1addaa8)] - **tools**: update stability index linking logic (Rich Trott) [#36280](https://github.com/nodejs/node/pull/36280)
* \[[`d95ae65986`](https://github.com/nodejs/node/commit/d95ae65986)] - **tools**: update highlight.js to 10.1.2 (Myles Borins) [#36309](https://github.com/nodejs/node/pull/36309)
* \[[`5935ccc11c`](https://github.com/nodejs/node/commit/5935ccc11c)] - **tools**: fix undeclared identifier FALSE (Antoine du Hamel) [#36276](https://github.com/nodejs/node/pull/36276)
* \[[`a2da7ba914`](https://github.com/nodejs/node/commit/a2da7ba914)] - **tools**: use using-declaration consistently (Daniel Bevenius) [#36245](https://github.com/nodejs/node/pull/36245)
* \[[`82c1e39c4a`](https://github.com/nodejs/node/commit/82c1e39c4a)] - **tools**: refloat 7 Node.js patches to cpplint.py (Rich Trott) [#36235](https://github.com/nodejs/node/pull/36235)
* \[[`bcf7393412`](https://github.com/nodejs/node/commit/bcf7393412)] - **tools**: bump cpplint to 1.5.3 (Rich Trott) [#36235](https://github.com/nodejs/node/pull/36235)
* \[[`be11976407`](https://github.com/nodejs/node/commit/be11976407)] - **tools**: enable no-nonoctal-decimal-escape lint rule (Colin Ihrig) [#36217](https://github.com/nodejs/node/pull/36217)
* \[[`c86c2399a2`](https://github.com/nodejs/node/commit/c86c2399a2)] - **tools**: update ESLint to 7.14.0 (Colin Ihrig) [#36217](https://github.com/nodejs/node/pull/36217)
* \[[`cfadd82cf3`](https://github.com/nodejs/node/commit/cfadd82cf3)] - **tools**: refloat 7 Node.js patches to cpplint.py (Rich Trott) [#36213](https://github.com/nodejs/node/pull/36213)
* \[[`03e8aaf613`](https://github.com/nodejs/node/commit/03e8aaf613)] - **tools**: bump cpplint.py to 1.5.2 (Rich Trott) [#36213](https://github.com/nodejs/node/pull/36213)
* \[[`6bc007fc94`](https://github.com/nodejs/node/commit/6bc007fc94)] - **tty**: refactor to use more primordials (Zijian Liu) [#36272](https://github.com/nodejs/node/pull/36272)
* \[[`fbd5652943`](https://github.com/nodejs/node/commit/fbd5652943)] - **v8**: refactor to use more primordials (Antoine du Hamel) [#36285](https://github.com/nodejs/node/pull/36285)
* \[[`8731a80439`](https://github.com/nodejs/node/commit/8731a80439)] - **vm**: add `SafeForTerminationScope`s for SIGINT interruptions (Anna Henningsen) [#36344](https://github.com/nodejs/node/pull/36344)
* \[[`47345a1f84`](https://github.com/nodejs/node/commit/47345a1f84)] - **worker**: refactor to use more primordials (Antoine du Hamel) [#36393](https://github.com/nodejs/node/pull/36393)
* \[[`21c4704c7b`](https://github.com/nodejs/node/commit/21c4704c7b)] - **worker**: refactor to use more primordials (Antoine du Hamel) [#36267](https://github.com/nodejs/node/pull/36267)
* \[[`802d44b1a9`](https://github.com/nodejs/node/commit/802d44b1a9)] - **(SEMVER-MINOR)** **worker**: add experimental BroadcastChannel (James M Snell) [#36271](https://github.com/nodejs/node/pull/36271)
* \[[`4b4caada9f`](https://github.com/nodejs/node/commit/4b4caada9f)] - **zlib**: refactor to use more primordials (Antoine du Hamel) [#36347](https://github.com/nodejs/node/pull/36347)

<a id="15.3.0"></a>

## 2020-11-24, Version 15.3.0 (Current), @codebytere

### Notable Changes

* \[[`6349b1d673`](https://github.com/nodejs/node/commit/6349b1d673)] - **(SEMVER-MINOR)** **dns**: add a cancel() method to the promise Resolver (Szymon Marczak) [#33099](https://github.com/nodejs/node/pull/33099)
* \[[`9ce9b016e6`](https://github.com/nodejs/node/commit/9ce9b016e6)] - **(SEMVER-MINOR)** **events**: add max listener warning for EventTarget (James M Snell) [#36001](https://github.com/nodejs/node/pull/36001)
* \[[`8390f8a86b`](https://github.com/nodejs/node/commit/8390f8a86b)] - **(SEMVER-MINOR)** **http**: add support for abortsignal to http.request (Benjamin Gruenbaum) [#36048](https://github.com/nodejs/node/pull/36048)
* \[[`9c6be3cc90`](https://github.com/nodejs/node/commit/9c6be3cc90)] - **(SEMVER-MINOR)** **http2**: allow setting the local window size of a session (Yongsheng Zhang) [#35978](https://github.com/nodejs/node/pull/35978)
* \[[`15ff155c12`](https://github.com/nodejs/node/commit/15ff155c12)] - **(SEMVER-MINOR)** **lib**: add throws option to fs.f/l/statSync (Andrew Casey) [#33716](https://github.com/nodejs/node/pull/33716)
* \[[`85c85d368a`](https://github.com/nodejs/node/commit/85c85d368a)] - **(SEMVER-MINOR)** **path**: add `path/posix` and `path/win32` alias modules (ExE Boss) [#34962](https://github.com/nodejs/node/pull/34962)
* \[[`d1baae3640`](https://github.com/nodejs/node/commit/d1baae3640)] - **(SEMVER-MINOR)** **readline**: add getPrompt to get the current prompt (Mattias Runge-Broberg) [#33675](https://github.com/nodejs/node/pull/33675)
* \[[`5729478509`](https://github.com/nodejs/node/commit/5729478509)] - **(SEMVER-MINOR)** **src**: add loop idle time in diagnostic report (Gireesh Punathil) [#35940](https://github.com/nodejs/node/pull/35940)
* \[[`baa87c1a7d`](https://github.com/nodejs/node/commit/baa87c1a7d)] - **(SEMVER-MINOR)** **util**: add `util/types` alias module (ExE Boss) [#34055](https://github.com/nodejs/node/pull/34055)

### Commits

* \[[`34aa0c868e`](https://github.com/nodejs/node/commit/34aa0c868e)] - **assert**: refactor to use more primordials (Antoine du Hamel) [#35998](https://github.com/nodejs/node/pull/35998)
* \[[`28d710164a`](https://github.com/nodejs/node/commit/28d710164a)] - **async\_hooks**: refactor to use more primordials (Antoine du Hamel) [#36168](https://github.com/nodejs/node/pull/36168)
* \[[`1924255fdb`](https://github.com/nodejs/node/commit/1924255fdb)] - **async\_hooks**: fix leak in AsyncLocalStorage exit (Stephen Belanger) [#35779](https://github.com/nodejs/node/pull/35779)
* \[[`3ee556a867`](https://github.com/nodejs/node/commit/3ee556a867)] - **benchmark**: fix build warnings (Gabriel Schulhof) [#36157](https://github.com/nodejs/node/pull/36157)
* \[[`fcc38a1312`](https://github.com/nodejs/node/commit/fcc38a1312)] - **build**: replace which with command -v (raisinten) [#36118](https://github.com/nodejs/node/pull/36118)
* \[[`60874ba941`](https://github.com/nodejs/node/commit/60874ba941)] - **build**: try “python3” as a last resort for 3.x (Ole André Vadla Ravnås) [#35983](https://github.com/nodejs/node/pull/35983)
* \[[`fbe210b2a1`](https://github.com/nodejs/node/commit/fbe210b2a1)] - **build**: conditionally clear vcinstalldir (Brian Ingenito) [#36009](https://github.com/nodejs/node/pull/36009)
* \[[`56f83e6876`](https://github.com/nodejs/node/commit/56f83e6876)] - **build**: refactor configure.py to use argparse (raisinten) [#35755](https://github.com/nodejs/node/pull/35755)
* \[[`0b70822461`](https://github.com/nodejs/node/commit/0b70822461)] - **child\_process**: refactor to use more primordials (Antoine du Hamel) [#36003](https://github.com/nodejs/node/pull/36003)
* \[[`e54108f2e4`](https://github.com/nodejs/node/commit/e54108f2e4)] - **cluster**: refactor to use more primordials (Antoine du Hamel) [#36011](https://github.com/nodejs/node/pull/36011)
* \[[`272fc794b2`](https://github.com/nodejs/node/commit/272fc794b2)] - **crypto**: fix format warning in AdditionalConfig (raisinten) [#36060](https://github.com/nodejs/node/pull/36060)
* \[[`63a138e02f`](https://github.com/nodejs/node/commit/63a138e02f)] - **crypto**: fix passing TypedArray to webcrypto AES methods (Antoine du Hamel) [#36087](https://github.com/nodejs/node/pull/36087)
* \[[`4a88c73fa5`](https://github.com/nodejs/node/commit/4a88c73fa5)] - **deps**: upgrade npm to 7.0.14 (nlf) [#36238](https://github.com/nodejs/node/pull/36238)
* \[[`d16e8622a7`](https://github.com/nodejs/node/commit/d16e8622a7)] - **deps**: upgrade npm to 7.0.13 (Ruy Adorno) [#36202](https://github.com/nodejs/node/pull/36202)
* \[[`c23ee3744f`](https://github.com/nodejs/node/commit/c23ee3744f)] - **deps**: upgrade npm to 7.0.12 (Ruy Adorno) [#36153](https://github.com/nodejs/node/pull/36153)
* \[[`0fcbb1c0d5`](https://github.com/nodejs/node/commit/0fcbb1c0d5)] - **deps**: V8: cherry-pick 3176bfd447a9 (Anna Henningsen) [#35612](https://github.com/nodejs/node/pull/35612)
* \[[`27f1bc05fd`](https://github.com/nodejs/node/commit/27f1bc05fd)] - **deps**: upgrade npm to 7.0.11 (Darcy Clarke) [#36112](https://github.com/nodejs/node/pull/36112)
* \[[`8ae3ffe2be`](https://github.com/nodejs/node/commit/8ae3ffe2be)] - **deps**: V8: cherry-pick 1d0f426311d4 (Ole André Vadla Ravnås) [#35986](https://github.com/nodejs/node/pull/35986)
* \[[`4b7ba11d67`](https://github.com/nodejs/node/commit/4b7ba11d67)] - **deps**: V8: cherry-pick 4e077ff0444a (Ole André Vadla Ravnås) [#35986](https://github.com/nodejs/node/pull/35986)
* \[[`098a5b1298`](https://github.com/nodejs/node/commit/098a5b1298)] - **deps**: V8: cherry-pick 086eecbd96b6 (Ole André Vadla Ravnås) [#35986](https://github.com/nodejs/node/pull/35986)
* \[[`d2c757ab19`](https://github.com/nodejs/node/commit/d2c757ab19)] - **deps**: V8: cherry-pick 27e1ac1a79ff (Ole André Vadla Ravnås) [#35986](https://github.com/nodejs/node/pull/35986)
* \[[`6349b1d673`](https://github.com/nodejs/node/commit/6349b1d673)] - **(SEMVER-MINOR)** **dns**: add a cancel() method to the promise Resolver (Szymon Marczak) [#33099](https://github.com/nodejs/node/pull/33099)
* \[[`0fbade38ef`](https://github.com/nodejs/node/commit/0fbade38ef)] - **doc**: add arm64 macOS as experimental (Richard Lau) [#36189](https://github.com/nodejs/node/pull/36189)
* \[[`42dfda8f78`](https://github.com/nodejs/node/commit/42dfda8f78)] - **doc**: remove stray comma in url.md (Rich Trott) [#36175](https://github.com/nodejs/node/pull/36175)
* \[[`8bbdbccbb6`](https://github.com/nodejs/node/commit/8bbdbccbb6)] - **doc**: revise agent.destroy() text (Rich Trott) [#36163](https://github.com/nodejs/node/pull/36163)
* \[[`545ac1fec5`](https://github.com/nodejs/node/commit/545ac1fec5)] - **doc**: fix punctuation in v8.md (Rich Trott) [#36192](https://github.com/nodejs/node/pull/36192)
* \[[`a6a90af8c0`](https://github.com/nodejs/node/commit/a6a90af8c0)] - **doc**: add compatibility/interop technical value (Geoffrey Booth) [#35323](https://github.com/nodejs/node/pull/35323)
* \[[`4ab4a99900`](https://github.com/nodejs/node/commit/4ab4a99900)] - **doc**: de-emphasize wrapping in napi\_define\_class (Gabriel Schulhof) [#36159](https://github.com/nodejs/node/pull/36159)
* \[[`bb29508e8f`](https://github.com/nodejs/node/commit/bb29508e8f)] - **doc**: add link for v8.takeCoverage() (Rich Trott) [#36135](https://github.com/nodejs/node/pull/36135)
* \[[`24065b92f1`](https://github.com/nodejs/node/commit/24065b92f1)] - **doc**: mark modules implementation as stable (Guy Bedford) [#35781](https://github.com/nodejs/node/pull/35781)
* \[[`142cacdc63`](https://github.com/nodejs/node/commit/142cacdc63)] - **doc**: clarify text about process not responding (Rich Trott) [#36117](https://github.com/nodejs/node/pull/36117)
* \[[`0ff384b0be`](https://github.com/nodejs/node/commit/0ff384b0be)] - **doc**: esm docs consolidation and reordering (Guy Bedford) [#36046](https://github.com/nodejs/node/pull/36046)
* \[[`b17a83a00d`](https://github.com/nodejs/node/commit/b17a83a00d)] - **doc**: claim ABI version for Electron v13 (Shelley Vohr) [#36101](https://github.com/nodejs/node/pull/36101)
* \[[`e8a8513b2c`](https://github.com/nodejs/node/commit/e8a8513b2c)] - **doc**: fix invalid link in worker\_threads.md (Rich Trott) [#36109](https://github.com/nodejs/node/pull/36109)
* \[[`cd33594a0d`](https://github.com/nodejs/node/commit/cd33594a0d)] - **doc**: move shigeki to emeritus (Rich Trott) [#36093](https://github.com/nodejs/node/pull/36093)
* \[[`eefc6aa6c9`](https://github.com/nodejs/node/commit/eefc6aa6c9)] - **doc**: document the error when cwd not exists in child\_process.spawn (FeelyChau) [#34505](https://github.com/nodejs/node/pull/34505)
* \[[`841a2812d0`](https://github.com/nodejs/node/commit/841a2812d0)] - **doc**: fix typo in debugger.md (Rich Trott) [#36066](https://github.com/nodejs/node/pull/36066)
* \[[`500e709439`](https://github.com/nodejs/node/commit/500e709439)] - **doc**: update list styles for remark-parse\@9 rendering (Rich Trott) [#36049](https://github.com/nodejs/node/pull/36049)
* \[[`a8dab217eb`](https://github.com/nodejs/node/commit/a8dab217eb)] - **doc,url**: fix url.hostname example (Rishabh Mehan) [#33735](https://github.com/nodejs/node/pull/33735)
* \[[`e48ec703ba`](https://github.com/nodejs/node/commit/e48ec703ba)] - **domain**: improve deprecation warning text for DEP0097 (Anna Henningsen) [#36136](https://github.com/nodejs/node/pull/36136)
* \[[`bcbf176c22`](https://github.com/nodejs/node/commit/bcbf176c22)] - **errors**: refactor to use more primordials (Antoine du Hamel) [#36167](https://github.com/nodejs/node/pull/36167)
* \[[`66788970ac`](https://github.com/nodejs/node/commit/66788970ac)] - **esm**: refactor to use more primordials (Antoine du Hamel) [#36019](https://github.com/nodejs/node/pull/36019)
* \[[`9ce9b016e6`](https://github.com/nodejs/node/commit/9ce9b016e6)] - **(SEMVER-MINOR)** **events**: add max listener warning for EventTarget (James M Snell) [#36001](https://github.com/nodejs/node/pull/36001)
* \[[`1550073dbc`](https://github.com/nodejs/node/commit/1550073dbc)] - **events**: disabled manual construction AbortSignal (raisinten) [#36094](https://github.com/nodejs/node/pull/36094)
* \[[`8a6cabbb23`](https://github.com/nodejs/node/commit/8a6cabbb23)] - **events**: port some wpt tests (Ethan Arrowood) [#34169](https://github.com/nodejs/node/pull/34169)
* \[[`3691eccf0a`](https://github.com/nodejs/node/commit/3691eccf0a)] - **fs**: remove experimental from promises.rmdir recursive (Anders Kaseorg) [#36131](https://github.com/nodejs/node/pull/36131)
* \[[`76b1863240`](https://github.com/nodejs/node/commit/76b1863240)] - **fs**: filehandle read now accepts object as argument (Nikola Glavina) [#34180](https://github.com/nodejs/node/pull/34180)
* \[[`2fdf509268`](https://github.com/nodejs/node/commit/2fdf509268)] - **http**: fix typo in comment (Hollow Man) [#36193](https://github.com/nodejs/node/pull/36193)
* \[[`8390f8a86b`](https://github.com/nodejs/node/commit/8390f8a86b)] - **(SEMVER-MINOR)** **http**: add support for abortsignal to http.request (Benjamin Gruenbaum) [#36048](https://github.com/nodejs/node/pull/36048)
* \[[`387d92fd0e`](https://github.com/nodejs/node/commit/387d92fd0e)] - **http**: onFinish will not be triggered again when finished (rickyes) [#35845](https://github.com/nodejs/node/pull/35845)
* \[[`48bf59bb8b`](https://github.com/nodejs/node/commit/48bf59bb8b)] - **http2**: add support for AbortSignal to http2Session.request (Madara Uchiha) [#36070](https://github.com/nodejs/node/pull/36070)
* \[[`8a0c3b9c76`](https://github.com/nodejs/node/commit/8a0c3b9c76)] - **http2**: refactor to use more primordials (Antoine du Hamel) [#36142](https://github.com/nodejs/node/pull/36142)
* \[[`f0aed8c01c`](https://github.com/nodejs/node/commit/f0aed8c01c)] - **http2**: add support for TypedArray to getUnpackedSettings (Antoine du Hamel) [#36141](https://github.com/nodejs/node/pull/36141)
* \[[`9c6be3cc90`](https://github.com/nodejs/node/commit/9c6be3cc90)] - **(SEMVER-MINOR)** **http2**: allow setting the local window size of a session (Yongsheng Zhang) [#35978](https://github.com/nodejs/node/pull/35978)
* \[[`0b40568afe`](https://github.com/nodejs/node/commit/0b40568afe)] - **http2**: delay session.receive() by a tick (Szymon Marczak) [#35985](https://github.com/nodejs/node/pull/35985)
* \[[`1a4d43f840`](https://github.com/nodejs/node/commit/1a4d43f840)] - **lib**: refactor to use more primordials (Antoine du Hamel) [#36140](https://github.com/nodejs/node/pull/36140)
* \[[`d6ea12e003`](https://github.com/nodejs/node/commit/d6ea12e003)] - **lib**: set abort-controller toStringTag (Benjamin Gruenbaum) [#36115](https://github.com/nodejs/node/pull/36115)
* \[[`82f1cde57e`](https://github.com/nodejs/node/commit/82f1cde57e)] - **lib**: remove primordials.SafePromise (Antoine du Hamel) [#36149](https://github.com/nodejs/node/pull/36149)
* \[[`15ff155c12`](https://github.com/nodejs/node/commit/15ff155c12)] - **(SEMVER-MINOR)** **lib**: add throws option to fs.f/l/statSync (Andrew Casey) [#33716](https://github.com/nodejs/node/pull/33716)
* \[[`75707f45eb`](https://github.com/nodejs/node/commit/75707f45eb)] - **lib,tools**: enforce access to prototype from primordials (Antoine du Hamel) [#36025](https://github.com/nodejs/node/pull/36025)
* \[[`79b2ba6744`](https://github.com/nodejs/node/commit/79b2ba6744)] - **n-api**: clean up binding creation (Gabriel Schulhof) [#36170](https://github.com/nodejs/node/pull/36170)
* \[[`5698cc08f0`](https://github.com/nodejs/node/commit/5698cc08f0)] - **n-api**: fix test\_async\_context warnings (Gabriel Schulhof) [#36171](https://github.com/nodejs/node/pull/36171)
* \[[`3d623d850c`](https://github.com/nodejs/node/commit/3d623d850c)] - **n-api**: improve consistency of how we get context (Michael Dawson) [#36068](https://github.com/nodejs/node/pull/36068)
* \[[`89da0c3353`](https://github.com/nodejs/node/commit/89da0c3353)] - **n-api**: factor out calling pattern (Gabriel Schulhof) [#36113](https://github.com/nodejs/node/pull/36113)
* \[[`5c0ddbca01`](https://github.com/nodejs/node/commit/5c0ddbca01)] - **net**: fix invalid write after end error (Robert Nagy) [#36043](https://github.com/nodejs/node/pull/36043)
* \[[`85c85d368a`](https://github.com/nodejs/node/commit/85c85d368a)] - **(SEMVER-MINOR)** **path**: add `path/posix` and `path/win32` alias modules (ExE Boss) [#34962](https://github.com/nodejs/node/pull/34962)
* \[[`ed8af3a8b7`](https://github.com/nodejs/node/commit/ed8af3a8b7)] - **perf\_hooks**: make nodeTiming a first-class object (Momtchil Momtchev) [#35977](https://github.com/nodejs/node/pull/35977)
* \[[`eb9295b583`](https://github.com/nodejs/node/commit/eb9295b583)] - **promise**: emit error on domain unhandled rejections (Benjamin Gruenbaum) [#36082](https://github.com/nodejs/node/pull/36082)
* \[[`59af919d6b`](https://github.com/nodejs/node/commit/59af919d6b)] - **querystring**: reduce memory usage by Int8Array (sapics) [#34179](https://github.com/nodejs/node/pull/34179)
* \[[`d1baae3640`](https://github.com/nodejs/node/commit/d1baae3640)] - **(SEMVER-MINOR)** **readline**: add getPrompt to get the current prompt (Mattias Runge-Broberg) [#33675](https://github.com/nodejs/node/pull/33675)
* \[[`6d1b1c7ad0`](https://github.com/nodejs/node/commit/6d1b1c7ad0)] - **src**: integrate URL::href() and use in inspector (Daijiro Wachi) [#35912](https://github.com/nodejs/node/pull/35912)
* \[[`7086f2e653`](https://github.com/nodejs/node/commit/7086f2e653)] - **src**: refactor using-declarations node\_env\_var.cc (raisinten) [#36128](https://github.com/nodejs/node/pull/36128)
* \[[`122797e87f`](https://github.com/nodejs/node/commit/122797e87f)] - **src**: remove duplicate logic for getting buffer (Yash Ladha) [#34553](https://github.com/nodejs/node/pull/34553)
* \[[`5729478509`](https://github.com/nodejs/node/commit/5729478509)] - **(SEMVER-MINOR)** **src**: add loop idle time in diagnostic report (Gireesh Punathil) [#35940](https://github.com/nodejs/node/pull/35940)
* \[[`a81dc9ae18`](https://github.com/nodejs/node/commit/a81dc9ae18)] - **src,crypto**: refactoring of crypto\_context, SecureContext (James M Snell) [#35665](https://github.com/nodejs/node/pull/35665)
* \[[`5fa35f6934`](https://github.com/nodejs/node/commit/5fa35f6934)] - **test**: update comments in test-fs-read-offset-null (Rich Trott) [#36152](https://github.com/nodejs/node/pull/36152)
* \[[`73bb54af77`](https://github.com/nodejs/node/commit/73bb54af77)] - **test**: update wpt url and resource (Daijiro Wachi) [#36032](https://github.com/nodejs/node/pull/36032)
* \[[`77b47dfd08`](https://github.com/nodejs/node/commit/77b47dfd08)] - **test**: fix typo in inspector-helper.js (Luigi Pinca) [#36127](https://github.com/nodejs/node/pull/36127)
* \[[`474664963c`](https://github.com/nodejs/node/commit/474664963c)] - **test**: deflake test-http-destroyed-socket-write2 (Luigi Pinca) [#36120](https://github.com/nodejs/node/pull/36120)
* \[[`f9bbd35937`](https://github.com/nodejs/node/commit/f9bbd35937)] - **test**: make test-http2-client-jsstream-destroy.js reliable (Rich Trott) [#36129](https://github.com/nodejs/node/pull/36129)
* \[[`c19df17acb`](https://github.com/nodejs/node/commit/c19df17acb)] - **test**: add test for fs.read when offset key is null (mayank agarwal) [#35918](https://github.com/nodejs/node/pull/35918)
* \[[`9405cddbee`](https://github.com/nodejs/node/commit/9405cddbee)] - **test**: improve test-stream-duplex-readable-end (Luigi Pinca) [#36056](https://github.com/nodejs/node/pull/36056)
* \[[`3be5e86c57`](https://github.com/nodejs/node/commit/3be5e86c57)] - **test**: add util.inspect test for null maxStringLength (Rich Trott) [#36086](https://github.com/nodejs/node/pull/36086)
* \[[`6a4cc43028`](https://github.com/nodejs/node/commit/6a4cc43028)] - **test**: replace var with const (Aleksandr Krutko) [#36069](https://github.com/nodejs/node/pull/36069)
* \[[`a367c0dfc2`](https://github.com/nodejs/node/commit/a367c0dfc2)] - **timers**: refactor to use more primordials (Antoine du Hamel) [#36132](https://github.com/nodejs/node/pull/36132)
* \[[`a6ef92bc27`](https://github.com/nodejs/node/commit/a6ef92bc27)] - **tools**: bump unist-util-find\@1.0.1 to unist-util-find\@1.0.2 (Rich Trott) [#36106](https://github.com/nodejs/node/pull/36106)
* \[[`2d2491284e`](https://github.com/nodejs/node/commit/2d2491284e)] - **tools**: only use 2 cores for macos action (Myles Borins) [#36169](https://github.com/nodejs/node/pull/36169)
* \[[`d8fcf2c324`](https://github.com/nodejs/node/commit/d8fcf2c324)] - **tools**: remove bashisms from license builder script (Antoine du Hamel) [#36122](https://github.com/nodejs/node/pull/36122)
* \[[`7e7ddb11c0`](https://github.com/nodejs/node/commit/7e7ddb11c0)] - **tools**: hide commit queue action link (Antoine du Hamel) [#36124](https://github.com/nodejs/node/pull/36124)
* \[[`63494e434a`](https://github.com/nodejs/node/commit/63494e434a)] - **tools**: update doc tools to remark-parse\@9.0.0 (Rich Trott) [#36049](https://github.com/nodejs/node/pull/36049)
* \[[`bf0550ce4e`](https://github.com/nodejs/node/commit/bf0550ce4e)] - **tools**: enforce use of single quotes in editorconfig (Antoine du Hamel) [#36020](https://github.com/nodejs/node/pull/36020)
* \[[`49649a499e`](https://github.com/nodejs/node/commit/49649a499e)] - **tools**: fix config serialization w/ long strings (Ole André Vadla Ravnås) [#35982](https://github.com/nodejs/node/pull/35982)
* \[[`be220b213d`](https://github.com/nodejs/node/commit/be220b213d)] - **tools**: update ESLint to 7.13.0 (Luigi Pinca) [#36031](https://github.com/nodejs/node/pull/36031)
* \[[`4140f491fd`](https://github.com/nodejs/node/commit/4140f491fd)] - **util**: fix to inspect getters that access this (raisinten) [#36052](https://github.com/nodejs/node/pull/36052)
* \[[`baa87c1a7d`](https://github.com/nodejs/node/commit/baa87c1a7d)] - **(SEMVER-MINOR)** **util**: add `util/types` alias module (ExE Boss) [#34055](https://github.com/nodejs/node/pull/34055)
* \[[`f7b2fce1c1`](https://github.com/nodejs/node/commit/f7b2fce1c1)] - **vm**: refactor to use more primordials (Antoine du Hamel) [#36023](https://github.com/nodejs/node/pull/36023)
* \[[`4e3883ec2d`](https://github.com/nodejs/node/commit/4e3883ec2d)] - **win,build,tools**: support VS prerelease (Baruch Odem) [#36033](https://github.com/nodejs/node/pull/36033)

<a id="15.2.1"></a>

## 2020-11-16, Version 15.2.1 (Current), @targos

### Notable changes

This is a security release.

Vulnerabilities fixed:

* **CVE-2020-8277**: Denial of Service through DNS request (High). A Node.js application that allows an attacker to trigger a DNS request for a host of their choice could trigger a Denial of service by getting the application to resolve a DNS record with a larger number of responses.

### Commits

* \[[`2a44836eeb`](https://github.com/nodejs/node/commit/2a44836eeb)] - **deps**: cherry-pick 0d252eb from upstream c-ares (Michael Dawson) [nodejs-private/node-private#231](https://github.com/nodejs-private/node-private/pull/231)
* \[[`b1f5518a0a`](https://github.com/nodejs/node/commit/b1f5518a0a)] - **doc**: fix `events.getEventListeners` example (Dmitry Semigradsky) [#36085](https://github.com/nodejs/node/pull/36085)
* \[[`b477447a55`](https://github.com/nodejs/node/commit/b477447a55)] - **doc**: fix `added:` info for `stream.\_construct()` (Luigi Pinca) [#36067](https://github.com/nodejs/node/pull/36067)
* \[[`df211208c0`](https://github.com/nodejs/node/commit/df211208c0)] - **test**: add missing test coverage for setLocalAddress() (Rich Trott) [#36039](https://github.com/nodejs/node/pull/36039)
* \[[`f5191f5bd2`](https://github.com/nodejs/node/commit/f5191f5bd2)] - **test**: remove flaky designation for fixed test (Rich Trott) [#35961](https://github.com/nodejs/node/pull/35961)
* \[[`a2f652f7c5`](https://github.com/nodejs/node/commit/a2f652f7c5)] - **test**: move test-worker-eventlooputil to sequential (Rich Trott) [#35996](https://github.com/nodejs/node/pull/35996)
* \[[`b0b43b27d6`](https://github.com/nodejs/node/commit/b0b43b27d6)] - **test**: fix unreliable test-fs-write-file.js (Rich Trott) [#36102](https://github.com/nodejs/node/pull/36102)

<a id="15.2.0"></a>

## 2020-11-10, Version 15.2.0 (Current), @danielleadams

### Notable changes

* **events**:
  * getEventListeners static (Benjamin Gruenbaum) [#35991](https://github.com/nodejs/node/pull/35991)
* **fs**:
  * support abortsignal in writeFile (Benjamin Gruenbaum) [#35993](https://github.com/nodejs/node/pull/35993)
  * add support for AbortSignal in readFile (Benjamin Gruenbaum) [#35911](https://github.com/nodejs/node/pull/35911)
* **stream**:
  * fix thrown object reference (Gil Pedersen) [#36065](https://github.com/nodejs/node/pull/36065)

### Commits

* \[[`9d9a044c1b`](https://github.com/nodejs/node/commit/9d9a044c1b)] - **benchmark**: ignore build artifacts for napi addons (Richard Lau) [#35970](https://github.com/nodejs/node/pull/35970)
* \[[`4c6de854be`](https://github.com/nodejs/node/commit/4c6de854be)] - **benchmark**: remove modules that require intl (Richard Lau) [#35968](https://github.com/nodejs/node/pull/35968)
* \[[`292915a6a8`](https://github.com/nodejs/node/commit/292915a6a8)] - **bootstrap**: refactor to use more primordials (Antoine du Hamel) [#35999](https://github.com/nodejs/node/pull/35999)
* \[[`10c9ea771d`](https://github.com/nodejs/node/commit/10c9ea771d)] - **build**: fix zlib inlining for IA-32 (raisinten) [#35679](https://github.com/nodejs/node/pull/35679)
* \[[`6ac9c8f31b`](https://github.com/nodejs/node/commit/6ac9c8f31b)] - **build, tools**: look for local installation of NASM (Richard Lau) [#36014](https://github.com/nodejs/node/pull/36014)
* \[[`9757b47c44`](https://github.com/nodejs/node/commit/9757b47c44)] - **console**: use more primordials (Antoine du Hamel) [#35734](https://github.com/nodejs/node/pull/35734)
* \[[`0d7422651b`](https://github.com/nodejs/node/commit/0d7422651b)] - **crypto**: refactor to use more primordials (Antoine du Hamel) [#36012](https://github.com/nodejs/node/pull/36012)
* \[[`dc4936ba50`](https://github.com/nodejs/node/commit/dc4936ba50)] - **crypto**: fix comment in ByteSource (Tobias Nießen) [#35972](https://github.com/nodejs/node/pull/35972)
* \[[`7cb5c0911e`](https://github.com/nodejs/node/commit/7cb5c0911e)] - **deps**: cherry-pick 9a49b22 from V8 upstream (Daniel Bevenius) [#35939](https://github.com/nodejs/node/pull/35939)
* \[[`4b03670877`](https://github.com/nodejs/node/commit/4b03670877)] - **dns**: fix trace\_events name for resolveCaa() (Rich Trott) [#35979](https://github.com/nodejs/node/pull/35979)
* \[[`dcb27600da`](https://github.com/nodejs/node/commit/dcb27600da)] - **doc**: escape asterisk in cctest gtest-filter (raisinten) [#36034](https://github.com/nodejs/node/pull/36034)
* \[[`923276ca53`](https://github.com/nodejs/node/commit/923276ca53)] - **doc**: move v8.getHeapCodeStatistics() (Rich Trott) [#36027](https://github.com/nodejs/node/pull/36027)
* \[[`71fa9c6b24`](https://github.com/nodejs/node/commit/71fa9c6b24)] - **doc**: add note regarding file structure in src/README.md (Denys Otrishko) [#35000](https://github.com/nodejs/node/pull/35000)
* \[[`99cb36238d`](https://github.com/nodejs/node/commit/99cb36238d)] - **doc**: advise users to import the full set of trusted release keys (Reşat SABIQ) [#32655](https://github.com/nodejs/node/pull/32655)
* \[[`06cc400160`](https://github.com/nodejs/node/commit/06cc400160)] - **doc**: fix crypto doc linter errors (Antoine du Hamel) [#36035](https://github.com/nodejs/node/pull/36035)
* \[[`01129a7b39`](https://github.com/nodejs/node/commit/01129a7b39)] - **doc**: revise v8.getHeapSnapshot() (Rich Trott) [#35849](https://github.com/nodejs/node/pull/35849)
* \[[`77d33c9b2f`](https://github.com/nodejs/node/commit/77d33c9b2f)] - **doc**: update core-validate-commit link in guide (Daijiro Wachi) [#35938](https://github.com/nodejs/node/pull/35938)
* \[[`6d56ba03e2`](https://github.com/nodejs/node/commit/6d56ba03e2)] - **doc**: update benchmark CI test indicator in README (Rich Trott) [#35945](https://github.com/nodejs/node/pull/35945)
* \[[`8bd364a9b3`](https://github.com/nodejs/node/commit/8bd364a9b3)] - **doc**: add new wordings to the API description (Pooja D.P) [#35588](https://github.com/nodejs/node/pull/35588)
* \[[`acd3617e1a`](https://github.com/nodejs/node/commit/acd3617e1a)] - **doc**: option --prof documentation help added (krank2me) [#34991](https://github.com/nodejs/node/pull/34991)
* \[[`6968b0fd49`](https://github.com/nodejs/node/commit/6968b0fd49)] - **doc**: fix release-schedule link in backport guide (Daijiro Wachi) [#35920](https://github.com/nodejs/node/pull/35920)
* \[[`efbfeff62b`](https://github.com/nodejs/node/commit/efbfeff62b)] - **doc**: fix incorrect heading level (Bryan Field) [#35965](https://github.com/nodejs/node/pull/35965)
* \[[`9c4b360d08`](https://github.com/nodejs/node/commit/9c4b360d08)] - **doc,crypto**: added sign/verify method changes about dsaEncoding (Filip Skokan) [#35480](https://github.com/nodejs/node/pull/35480)
* \[[`85cf30541d`](https://github.com/nodejs/node/commit/85cf30541d)] - **doc,fs**: document value of stats.isDirectory on symbolic links (coderaiser) [#27413](https://github.com/nodejs/node/pull/27413)
* \[[`d6bd78ff82`](https://github.com/nodejs/node/commit/d6bd78ff82)] - **doc,net**: document socket.timeout (Brandon Kobel) [#34543](https://github.com/nodejs/node/pull/34543)
* \[[`36c20d939a`](https://github.com/nodejs/node/commit/36c20d939a)] - **doc,stream**: write(chunk, encoding, cb) encoding can be null (dev-script) [#35372](https://github.com/nodejs/node/pull/35372)
* \[[`9d26c4d496`](https://github.com/nodejs/node/commit/9d26c4d496)] - **domain**: refactor to use more primordials (Antoine du Hamel) [#35885](https://github.com/nodejs/node/pull/35885)
* \[[`d83e253065`](https://github.com/nodejs/node/commit/d83e253065)] - **errors**: refactor to use more primordials (Antoine du Hamel) [#35944](https://github.com/nodejs/node/pull/35944)
* \[[`567f8d8caf`](https://github.com/nodejs/node/commit/567f8d8caf)] - **(SEMVER-MINOR)** **events**: getEventListeners static (Benjamin Gruenbaum) [#35991](https://github.com/nodejs/node/pull/35991)
* \[[`9e673723e3`](https://github.com/nodejs/node/commit/9e673723e3)] - **events**: fire handlers in correct oder (Benjamin Gruenbaum) [#35931](https://github.com/nodejs/node/pull/35931)
* \[[`ff59fcdf7b`](https://github.com/nodejs/node/commit/ff59fcdf7b)] - **events**: define abort on prototype (Benjamin Gruenbaum) [#35931](https://github.com/nodejs/node/pull/35931)
* \[[`ab0eb4f2c9`](https://github.com/nodejs/node/commit/ab0eb4f2c9)] - **events**: support event handlers on prototypes (Benjamin Gruenbaum) [#35931](https://github.com/nodejs/node/pull/35931)
* \[[`33e2ee58a7`](https://github.com/nodejs/node/commit/33e2ee58a7)] - **events**: define event handler as enumerable (Benjamin Gruenbaum) [#35931](https://github.com/nodejs/node/pull/35931)
* \[[`a7d0c76f86`](https://github.com/nodejs/node/commit/a7d0c76f86)] - **events**: support emit on nodeeventtarget (Benjamin Gruenbaum) [#35851](https://github.com/nodejs/node/pull/35851)
* \[[`76332a0439`](https://github.com/nodejs/node/commit/76332a0439)] - **events**: port some wpt tests (Benjamin Gruenbaum) [#33621](https://github.com/nodejs/node/pull/33621)
* \[[`ccf9f0e62e`](https://github.com/nodejs/node/commit/ccf9f0e62e)] - **(SEMVER-MINOR)** **fs**: support abortsignal in writeFile (Benjamin Gruenbaum) [#35993](https://github.com/nodejs/node/pull/35993)
* \[[`7ef9c707e9`](https://github.com/nodejs/node/commit/7ef9c707e9)] - **fs**: replace finally with PromisePrototypeFinally (Baruch Odem (Rothkoff)) [#35995](https://github.com/nodejs/node/pull/35995)
* \[[`ccbe267515`](https://github.com/nodejs/node/commit/ccbe267515)] - **fs**: remove unnecessary Function#bind() in fs/promises (Ben Noordhuis) [#35208](https://github.com/nodejs/node/pull/35208)
* \[[`6011bfdec5`](https://github.com/nodejs/node/commit/6011bfdec5)] - **fs**: remove unused assignment (Rich Trott) [#35882](https://github.com/nodejs/node/pull/35882)
* \[[`92bdfd141b`](https://github.com/nodejs/node/commit/92bdfd141b)] - **(SEMVER-MINOR)** **fs**: add support for AbortSignal in readFile (Benjamin Gruenbaum) [#35911](https://github.com/nodejs/node/pull/35911)
* \[[`11f592450b`](https://github.com/nodejs/node/commit/11f592450b)] - **http2**: add has method to proxySocketHandler (masx200) [#35197](https://github.com/nodejs/node/pull/35197)
* \[[`28ed7d062e`](https://github.com/nodejs/node/commit/28ed7d062e)] - **http2**: centralise socket event binding in Http2Session (Momtchil Momtchev) [#35772](https://github.com/nodejs/node/pull/35772)
* \[[`429113ebfb`](https://github.com/nodejs/node/commit/429113ebfb)] - **http2**: move events to the JSStreamSocket (Momtchil Momtchev) [#35772](https://github.com/nodejs/node/pull/35772)
* \[[`1dd744a420`](https://github.com/nodejs/node/commit/1dd744a420)] - **http2**: fix error stream write followed by destroy (David Halls) [#35951](https://github.com/nodejs/node/pull/35951)
* \[[`af2a560c42`](https://github.com/nodejs/node/commit/af2a560c42)] - **lib**: add %TypedArray% abstract constructor to primordials (ExE Boss) [#36016](https://github.com/nodejs/node/pull/36016)
* \[[`b700900d02`](https://github.com/nodejs/node/commit/b700900d02)] - **lib**: refactor to use more primordials (Antoine du Hamel) [#35875](https://github.com/nodejs/node/pull/35875)
* \[[`7a375902ff`](https://github.com/nodejs/node/commit/7a375902ff)] - **module**: refactor to use more primordials (Antoine du Hamel) [#36024](https://github.com/nodejs/node/pull/36024)
* \[[`8d76db86b5`](https://github.com/nodejs/node/commit/8d76db86b5)] - **module**: refactor to use iterable-weak-map (Benjamin Coe) [#35915](https://github.com/nodejs/node/pull/35915)
* \[[`9b6512f7de`](https://github.com/nodejs/node/commit/9b6512f7de)] - **n-api**: unlink reference during its destructor (Gabriel Schulhof) [#35933](https://github.com/nodejs/node/pull/35933)
* \[[`1b277d97f3`](https://github.com/nodejs/node/commit/1b277d97f3)] - **src**: remove ERR prefix in crypto status enums (Daniel Bevenius) [#35867](https://github.com/nodejs/node/pull/35867)
* \[[`9774b4cc72`](https://github.com/nodejs/node/commit/9774b4cc72)] - **stream**: fix thrown object reference (Gil Pedersen) [#36065](https://github.com/nodejs/node/pull/36065)
* \[[`359a6590b0`](https://github.com/nodejs/node/commit/359a6590b0)] - **stream**: writableNeedDrain (Robert Nagy) [#35348](https://github.com/nodejs/node/pull/35348)
* \[[`b7aa5e2296`](https://github.com/nodejs/node/commit/b7aa5e2296)] - **stream**: remove isPromise utility function (Antoine du Hamel) [#35925](https://github.com/nodejs/node/pull/35925)
* \[[`fdae9ad188`](https://github.com/nodejs/node/commit/fdae9ad188)] - **test**: fix races in test-performance-eventlooputil (Gerhard Stoebich) [#36028](https://github.com/nodejs/node/pull/36028)
* \[[`0a4c96a7df`](https://github.com/nodejs/node/commit/0a4c96a7df)] - **test**: use global.EventTarget instead of internals (Antoine du Hamel) [#36002](https://github.com/nodejs/node/pull/36002)
* \[[`f73b8d84db`](https://github.com/nodejs/node/commit/f73b8d84db)] - **test**: improve error message for policy failures (Bradley Meck) [#35633](https://github.com/nodejs/node/pull/35633)
* \[[`cb6f0d3d89`](https://github.com/nodejs/node/commit/cb6f0d3d89)] - **test**: update old comment style test\_util.cc (raisinten) [#35884](https://github.com/nodejs/node/pull/35884)
* \[[`23f0d0c45c`](https://github.com/nodejs/node/commit/23f0d0c45c)] - **test**: fix error in test/internet/test-dns.js (Rich Trott) [#35969](https://github.com/nodejs/node/pull/35969)
* \[[`77e4f19701`](https://github.com/nodejs/node/commit/77e4f19701)] - **timers**: cleanup abort listener on awaitable timers (James M Snell) [#36006](https://github.com/nodejs/node/pull/36006)
* \[[`a7350b3a8f`](https://github.com/nodejs/node/commit/a7350b3a8f)] - **tools**: don't print gold linker warning w/o flag (Myles Borins) [#35955](https://github.com/nodejs/node/pull/35955)
* \[[`1f27214480`](https://github.com/nodejs/node/commit/1f27214480)] - **tools**: add new ESLint rule: prefer-primordials (Leko) [#35448](https://github.com/nodejs/node/pull/35448)
* \[[`da3c2ab828`](https://github.com/nodejs/node/commit/da3c2ab828)] - **tools,doc**: enable ecmaVersion 2021 in acorn parser (Antoine du Hamel) [#35994](https://github.com/nodejs/node/pull/35994)
* \[[`f8098c3e43`](https://github.com/nodejs/node/commit/f8098c3e43)] - **tools,lib**: recommend using safe primordials (Antoine du Hamel) [#36026](https://github.com/nodejs/node/pull/36026)
* \[[`eea7e3b0d0`](https://github.com/nodejs/node/commit/eea7e3b0d0)] - **tools,lib**: tighten prefer-primordials rules for Error statics (Antoine du Hamel) [#36017](https://github.com/nodejs/node/pull/36017)
* \[[`7a2edea7ed`](https://github.com/nodejs/node/commit/7a2edea7ed)] - **win, build**: fix build time on Windows (Bartosz Sosnowski) [#35932](https://github.com/nodejs/node/pull/35932)

<a id="15.1.0"></a>

## 2020-11-04, Version 15.1.0 (Current), @targos

### Notable Changes

#### Diagnostics channel (experimental module)

`diagnostics_channel` is a new experimental module that provides an API to create named channels to report arbitrary message data for diagnostics purposes.

With `diagnostics_channel`, Node.js core and module authors can publish contextual data about what they are doing at a given time. This could be the hostname and query string of a mysql query, for example. Just create a named channel with `dc.channel(name)` and call `channel.publish(data)` to send the data to any listeners to that channel.

```js
const dc = require('diagnostics_channel');
const channel = dc.channel('mysql.query');

MySQL.prototype.query = function query(queryString, values, callback) {
  // Broadcast query information whenever a query is made
  channel.publish({
    query: queryString,
    host: this.hostname,
  });

  this.doQuery(queryString, values, callback);
};
```

Channels are like one big global event emitter but are split into separate objects to ensure they get the best performance. If nothing is listening to the channel, the publishing overhead should be as close to zero as possible. Consuming channel data is as easy as using `channel.subscribe(listener)` to run a function whenever a message is published to that channel.

```js
const dc = require('diagnostics_channel');
const channel = dc.channel('mysql.query');

channel.subscribe(({ query, host }) => {
  console.log(`mysql query to ${host}: ${query}`);
});
```

The data captured can be used to provide context for what an app is doing at a given time. This can be used for things like augmenting tracing data, tracking network and filesystem activity, logging queries, and many other things. It's also a very useful data source for diagnostics tools to provide a clearer picture of exactly what the application is doing at a given point in the data they are presenting.

Contributed by Stephen Belanger [#34895](https://github.com/nodejs/node/pull/34895).

#### New child process `'spawn'` event

Instances of `ChildProcess` now emit a new `'spawn'` event once the child process has spawned successfully.

If emitted, the `'spawn'` event comes before all other events and before any data is received via `stdout` or `stderr`.

The `'spawn'` event will fire regardless of whether an error occurs **within** the spawned process.
For example, if `bash some-command` spawns successfully, the `'spawn'` event will fire, though `bash` may fail to spawn `some-command`.
This caveat also applies when using `{ shell: true }`.

Contributed by Matthew Francis Brunetti [#35369](https://github.com/nodejs/node/pull/35369).

#### Set the local address for DNS resolution

It is now possible to set the local IP address used by a `Resolver` instance to send its requests.
This allows programs to specify outbound interfaces when used on multi-homed
systems.

The resolver will use the v4 local address when making requests to IPv4 DNS servers, and the v6 local address when making requests to IPv6 DNS servers.

```js
const { Resolver } = require('dns');

const resolver = new Resolver();

resolver.setLocalAddress('10.1.2.3');
// Equivalent to: resolver.setLocalAddress('10.1.2.3', '::0');
```

Contributed by Josh Dague [#34824](https://github.com/nodejs/node/pull/34824).

#### Control V8 coverage at runtime

The `v8` module includes two new methods to control the V8 coverage started by the `NODE_V8_COVERAGE` environment variable.

With `v8.takeCoverage()`, it is possible to write a coverage report to disk on demand. This can be done multiple times during the lifetime of the process, and the execution counter will be reset on each call.
When the process is about to exit, one last coverage will still be written to disk, unless `v8.stopCoverage()` was invoked before.

The `v8.stopCoverage()` method allows to stop the coverage collection, so that V8 can release the execution counters and optimize code.

Contributed by Joyee Cheung [#33807](https://github.com/nodejs/node/pull/33807).

#### Analyze Worker's event loop utilization

`Worker` instances now have a `performance` property, with a single `eventLoopUtilization` method that can be used to gather information about the worker's event loop utilization between the `'online'` and `'exit'` events.

The method works the same way as `perf_hooks` `eventLoopUtilization()`.

Contributed by Trevor Norris [#35664](https://github.com/nodejs/node/pull/35664).

#### Take a V8 heap snapshot just before running out of memory (experimental)

With the new `--heapsnapshot-near-heap-limit=max_count` experimental command line flag, it is now possible to automatically generate a heap snapshot when the V8 heap usage is approaching the heap limit. `count` should be a non-negative integer (in which case Node.js will write no more than `max_count` snapshots to disk).

When generating snapshots, garbage collection may be triggered and bring the heap usage down, therefore multiple snapshots may be written to disk before the Node.js instance finally runs out of memory. These heap snapshots can be compared to determine what objects are being allocated during the time consecutive snapshots are taken.

Generating V8 snapshots takes time and memory (both memory managed by the V8 heap and native memory outside the V8 heap). The bigger the heap is, the more resources it needs. Node.js will adjust the V8 heap to accommodate the additional V8 heap memory overhead, and try its best to avoid using up all the memory avialable to the process.

```console
$ node --max-old-space-size=100 --heapsnapshot-near-heap-limit=3 index.js
Wrote snapshot to Heap.20200430.100036.49580.0.001.heapsnapshot
Wrote snapshot to Heap.20200430.100037.49580.0.002.heapsnapshot
Wrote snapshot to Heap.20200430.100038.49580.0.003.heapsnapshot

<--- Last few GCs --->

[49580:0x110000000]     4826 ms: Mark-sweep 130.6 (147.8) -> 130.5 (147.8) MB, 27.4 / 0.0 ms  (average mu = 0.126, current mu = 0.034) allocation failure scavenge might not succeed
[49580:0x110000000]     4845 ms: Mark-sweep 130.6 (147.8) -> 130.6 (147.8) MB, 18.8 / 0.0 ms  (average mu = 0.088, current mu = 0.031) allocation failure scavenge might not succeed


<--- JS stacktrace --->

FATAL ERROR: Ineffective mark-compacts near heap limit Allocation failed - JavaScript heap out of memory
....
```

Contributed by Joyee Cheung [#33010](https://github.com/nodejs/node/pull/33010).

### Commits

#### Semver-minor commits

* \[[`8169902b40`](https://github.com/nodejs/node/commit/8169902b40)] - **(SEMVER-MINOR)** **child\_process**: add ChildProcess 'spawn' event (Matthew Francis Brunetti) [#35369](https://github.com/nodejs/node/pull/35369)
* \[[`548f91af2c`](https://github.com/nodejs/node/commit/548f91af2c)] - **(SEMVER-MINOR)** **dns**: add setLocalAddress to Resolver (Josh Dague) [#34824](https://github.com/nodejs/node/pull/34824)
* \[[`f861733bac`](https://github.com/nodejs/node/commit/f861733bac)] - **(SEMVER-MINOR)** **http**: report request start and end with diagnostics\_channel (Stephen Belanger) [#34895](https://github.com/nodejs/node/pull/34895)
* \[[`883ed4b7f1`](https://github.com/nodejs/node/commit/883ed4b7f1)] - **(SEMVER-MINOR)** **http2**: add updateSettings to both http2 servers (Vincent Boivin) [#35383](https://github.com/nodejs/node/pull/35383)
* \[[`b38a43d5d9`](https://github.com/nodejs/node/commit/b38a43d5d9)] - **(SEMVER-MINOR)** **lib**: create diagnostics\_channel module (Stephen Belanger) [#34895](https://github.com/nodejs/node/pull/34895)
* \[[`a7f37bc725`](https://github.com/nodejs/node/commit/a7f37bc725)] - **(SEMVER-MINOR)** **src**: add --heapsnapshot-near-heap-limit option (Joyee Cheung) [#33010](https://github.com/nodejs/node/pull/33010)
* \[[`7bfa872013`](https://github.com/nodejs/node/commit/7bfa872013)] - **(SEMVER-MINOR)** **v8**: implement v8.stopCoverage() (Joyee Cheung) [#33807](https://github.com/nodejs/node/pull/33807)
* \[[`15ffed5319`](https://github.com/nodejs/node/commit/15ffed5319)] - **(SEMVER-MINOR)** **v8**: implement v8.takeCoverage() (Joyee Cheung) [#33807](https://github.com/nodejs/node/pull/33807)
* \[[`221e28311f`](https://github.com/nodejs/node/commit/221e28311f)] - **(SEMVER-MINOR)** **worker**: add eventLoopUtilization() (Trevor Norris) [#35664](https://github.com/nodejs/node/pull/35664)

#### Semver-patch commits

* \[[`d95013f399`](https://github.com/nodejs/node/commit/d95013f399)] - **assert,repl**: enable ecmaVersion 2021 in acorn parser (Michaël Zasso) [#35827](https://github.com/nodejs/node/pull/35827)
* \[[`b11c7378e3`](https://github.com/nodejs/node/commit/b11c7378e3)] - **build**: fix lint-js-fix target (Antoine du Hamel) [#35927](https://github.com/nodejs/node/pull/35927)
* \[[`a5fa849631`](https://github.com/nodejs/node/commit/a5fa849631)] - **build**: add vcbuilt test-doc target (Antoine du Hamel) [#35708](https://github.com/nodejs/node/pull/35708)
* \[[`34281cdaba`](https://github.com/nodejs/node/commit/34281cdaba)] - **build**: turn off Codecov comments (bcoe) [#35800](https://github.com/nodejs/node/pull/35800)
* \[[`a9c09246bb`](https://github.com/nodejs/node/commit/a9c09246bb)] - **build**: add license-builder GitHub Action (Tierney Cyren) [#35712](https://github.com/nodejs/node/pull/35712)
* \[[`4447ff1162`](https://github.com/nodejs/node/commit/4447ff1162)] - **build,tools**: gitHub Actions: use Node.js Fermium (Antoine du Hamel) [#35840](https://github.com/nodejs/node/pull/35840)
* \[[`273e147017`](https://github.com/nodejs/node/commit/273e147017)] - **build,tools**: add lint-js-doc target (Antoine du Hamel) [#35708](https://github.com/nodejs/node/pull/35708)
* \[[`0ebf44b466`](https://github.com/nodejs/node/commit/0ebf44b466)] - **crypto**: pass empty passphrases to OpenSSL properly (Tobias Nießen) [#35914](https://github.com/nodejs/node/pull/35914)
* \[[`644c416389`](https://github.com/nodejs/node/commit/644c416389)] - **crypto**: rename check to createJob (Daniel Bevenius) [#35858](https://github.com/nodejs/node/pull/35858)
* \[[`79a8fb62e6`](https://github.com/nodejs/node/commit/79a8fb62e6)] - **crypto**: fixup scrypt regressions (James M Snell) [#35821](https://github.com/nodejs/node/pull/35821)
* \[[`abd7c9447c`](https://github.com/nodejs/node/commit/abd7c9447c)] - **crypto**: fix webcrypto ECDH JWK import (Filip Skokan) [#35855](https://github.com/nodejs/node/pull/35855)
* \[[`d3f1cde908`](https://github.com/nodejs/node/commit/d3f1cde908)] - **deps**: upgrade npm to 7.0.8 (Myles Borins) [#35953](https://github.com/nodejs/node/pull/35953)
* \[[`55adee0947`](https://github.com/nodejs/node/commit/55adee0947)] - **deps**: upgrade npm to 7.0.7 (Luigi Pinca) [#35908](https://github.com/nodejs/node/pull/35908)
* \[[`5cb77f2e79`](https://github.com/nodejs/node/commit/5cb77f2e79)] - **deps**: upgrade to cjs-module-lexer\@1.0.0 (Guy Bedford) [#35928](https://github.com/nodejs/node/pull/35928)
* \[[`1303a1fca8`](https://github.com/nodejs/node/commit/1303a1fca8)] - **deps**: update to cjs-module-lexer\@0.5.2 (Guy Bedford) [#35901](https://github.com/nodejs/node/pull/35901)
* \[[`20accb08fa`](https://github.com/nodejs/node/commit/20accb08fa)] - **deps**: upgrade to cjs-module-lexer\@0.5.0 (Guy Bedford) [#35871](https://github.com/nodejs/node/pull/35871)
* \[[`52a77db759`](https://github.com/nodejs/node/commit/52a77db759)] - **deps**: update acorn to v8.0.4 (Michaël Zasso) [#35791](https://github.com/nodejs/node/pull/35791)
* \[[`e0a1541260`](https://github.com/nodejs/node/commit/e0a1541260)] - **deps**: update to cjs-module-lexer\@0.4.3 (Guy Bedford) [#35745](https://github.com/nodejs/node/pull/35745)
* \[[`894419c1f4`](https://github.com/nodejs/node/commit/894419c1f4)] - **deps**: V8: backport 4263f8a5e8e0 (Brian 'bdougie' Douglas) [#35650](https://github.com/nodejs/node/pull/35650)
* \[[`564aadedac`](https://github.com/nodejs/node/commit/564aadedac)] - **doc,src,test**: revise C++ code for linter update (Rich Trott) [#35719](https://github.com/nodejs/node/pull/35719)
* \[[`7c8b5e5e0e`](https://github.com/nodejs/node/commit/7c8b5e5e0e)] - **errors**: do not call resolve on URLs with schemes (bcoe) [#35903](https://github.com/nodejs/node/pull/35903)
* \[[`1cdfaa80f8`](https://github.com/nodejs/node/commit/1cdfaa80f8)] - **events**: add a few tests (Benjamin Gruenbaum) [#35806](https://github.com/nodejs/node/pull/35806)
* \[[`f08e2c0213`](https://github.com/nodejs/node/commit/f08e2c0213)] - **events**: make abort\_controller event trusted (Benjamin Gruenbaum) [#35811](https://github.com/nodejs/node/pull/35811)
* \[[`438d9debfd`](https://github.com/nodejs/node/commit/438d9debfd)] - **events**: make eventTarget.removeAllListeners() return this (Luigi Pinca) [#35805](https://github.com/nodejs/node/pull/35805)
* \[[`b6b7a3b86a`](https://github.com/nodejs/node/commit/b6b7a3b86a)] - **http**: lazy create IncomingMessage.headers (Robert Nagy) [#35281](https://github.com/nodejs/node/pull/35281)
* \[[`86ed87b6b7`](https://github.com/nodejs/node/commit/86ed87b6b7)] - **http2**: fix reinjection check (Momtchil Momtchev) [#35678](https://github.com/nodejs/node/pull/35678)
* \[[`5833007eb0`](https://github.com/nodejs/node/commit/5833007eb0)] - **http2**: reinject data received before http2 is attached (Momtchil Momtchev) [#35678](https://github.com/nodejs/node/pull/35678)
* \[[`cfe61b8714`](https://github.com/nodejs/node/commit/cfe61b8714)] - **http2**: remove unsupported %.\* specifier (Momtchil Momtchev) [#35694](https://github.com/nodejs/node/pull/35694)
* \[[`d2f574b5be`](https://github.com/nodejs/node/commit/d2f574b5be)] - **lib**: let abort\_controller target be EventTarget (Daijiro Wachi) [#35869](https://github.com/nodejs/node/pull/35869)
* \[[`b1e531a70b`](https://github.com/nodejs/node/commit/b1e531a70b)] - **lib**: use primordials when calling methods of Error (Antoine du Hamel) [#35837](https://github.com/nodejs/node/pull/35837)
* \[[`0f5a8c55c2`](https://github.com/nodejs/node/commit/0f5a8c55c2)] - **module**: runtime deprecate subpath folder mappings (Guy Bedford) [#35747](https://github.com/nodejs/node/pull/35747)
* \[[`d16e2fa69a`](https://github.com/nodejs/node/commit/d16e2fa69a)] - **n-api**: napi\_make\_callback emit async init with resource of async\_context (legendecas) [#32930](https://github.com/nodejs/node/pull/32930)
* \[[`0c17dbd201`](https://github.com/nodejs/node/commit/0c17dbd201)] - **n-api**: revert change to finalization (Michael Dawson) [#35777](https://github.com/nodejs/node/pull/35777)
* \[[`fb7196434e`](https://github.com/nodejs/node/commit/fb7196434e)] - **src**: remove redundant OpenSSLBuffer (James M Snell) [#35663](https://github.com/nodejs/node/pull/35663)
* \[[`c9225789d3`](https://github.com/nodejs/node/commit/c9225789d3)] - **src**: remove ERR prefix in WebCryptoKeyExportStatus (Daniel Bevenius) [#35639](https://github.com/nodejs/node/pull/35639)
* \[[`4128eefcb3`](https://github.com/nodejs/node/commit/4128eefcb3)] - **src**: remove ignore GCC -Wcast-function-type for v8 (Daniel Bevenius) [#35768](https://github.com/nodejs/node/pull/35768)
* \[[`4b8b5fee6a`](https://github.com/nodejs/node/commit/4b8b5fee6a)] - **src**: use MaybeLocal.ToLocal instead of IsEmpty (Daniel Bevenius) [#35716](https://github.com/nodejs/node/pull/35716)
* \[[`01d7c46776`](https://github.com/nodejs/node/commit/01d7c46776)] - _**Revert**_ "**src**: ignore GCC -Wcast-function-type for v8.h" (Daniel Bevenius) [#35758](https://github.com/nodejs/node/pull/35758)
* \[[`2868f52a5c`](https://github.com/nodejs/node/commit/2868f52a5c)] - **stream**: fix regression on duplex end (Momtchil Momtchev) [#35941](https://github.com/nodejs/node/pull/35941)
* \[[`70c41a830d`](https://github.com/nodejs/node/commit/70c41a830d)] - **stream**: remove redundant context from comments (Yash Ladha) [#35728](https://github.com/nodejs/node/pull/35728)
* \[[`88eb6191e4`](https://github.com/nodejs/node/commit/88eb6191e4)] - **stream**: fix duplicate logic in stream destroy (Yash Ladha) [#35727](https://github.com/nodejs/node/pull/35727)
* \[[`a41e3ebc3a`](https://github.com/nodejs/node/commit/a41e3ebc3a)] - **timers**: correct explanation in comment (Turner Jabbour) [#35437](https://github.com/nodejs/node/pull/35437)
* \[[`ee15142fef`](https://github.com/nodejs/node/commit/ee15142fef)] - **tls**: allow reading data into a static buffer (Andrey Pechkurov) [#35753](https://github.com/nodejs/node/pull/35753)
* \[[`102d7dfe02`](https://github.com/nodejs/node/commit/102d7dfe02)] - **zlib**: test BrotliCompress throws invalid arg value (raisinten) [#35830](https://github.com/nodejs/node/pull/35830)

#### Documentation commits

* \[[`7937fbe3bc`](https://github.com/nodejs/node/commit/7937fbe3bc)] - **doc**: update tables in README files for linting changes (Rich Trott) [#35905](https://github.com/nodejs/node/pull/35905)
* \[[`c5b94220c5`](https://github.com/nodejs/node/commit/c5b94220c5)] - **doc**: temporarily disable list-item-bullet-indent (Nick Schonning) [#35647](https://github.com/nodejs/node/pull/35647)
* \[[`59b36af8d5`](https://github.com/nodejs/node/commit/59b36af8d5)] - **doc**: disable no-undefined-references workarounds (Nick Schonning) [#35647](https://github.com/nodejs/node/pull/35647)
* \[[`eb55462a75`](https://github.com/nodejs/node/commit/eb55462a75)] - **doc**: adjust table alignment for remark v13 (Nick Schonning) [#35647](https://github.com/nodejs/node/pull/35647)
* \[[`0ac4a6ab16`](https://github.com/nodejs/node/commit/0ac4a6ab16)] - **doc**: update crypto.createSecretKey history (Ben Turner) [#35874](https://github.com/nodejs/node/pull/35874)
* \[[`4899998855`](https://github.com/nodejs/node/commit/4899998855)] - **doc**: move bnoordhuis to emeritus (Ben Noordhuis) [#35865](https://github.com/nodejs/node/pull/35865)
* \[[`337bfcf614`](https://github.com/nodejs/node/commit/337bfcf614)] - **doc**: add on statement in the APIs docs (Pooja D.P) [#35610](https://github.com/nodejs/node/pull/35610)
* \[[`9703219fdb`](https://github.com/nodejs/node/commit/9703219fdb)] - **doc**: fix a typo in CHANGELOG\_V15 (Takuya Noguchi) [#35804](https://github.com/nodejs/node/pull/35804)
* \[[`c14889bcc1`](https://github.com/nodejs/node/commit/c14889bcc1)] - **doc**: move ronkorving to emeritus (Rich Trott) [#35828](https://github.com/nodejs/node/pull/35828)
* \[[`8c2b17926c`](https://github.com/nodejs/node/commit/8c2b17926c)] - **doc**: recommend test-doc instead of lint-md (Antoine du Hamel) [#35708](https://github.com/nodejs/node/pull/35708)
* \[[`0580258449`](https://github.com/nodejs/node/commit/0580258449)] - **doc**: fix reference to googletest test fixture (Tobias Nießen) [#35813](https://github.com/nodejs/node/pull/35813)
* \[[`d291e3abd9`](https://github.com/nodejs/node/commit/d291e3abd9)] - **doc**: stabilize packages features (Myles Borins) [#35742](https://github.com/nodejs/node/pull/35742)
* \[[`5e8d821b4c`](https://github.com/nodejs/node/commit/5e8d821b4c)] - **doc**: add conditional example for setBreakpoint() (Chris Opperwall) [#35823](https://github.com/nodejs/node/pull/35823)
* \[[`8074f69f82`](https://github.com/nodejs/node/commit/8074f69f82)] - **doc**: make small improvements to REPL doc (Rich Trott) [#35808](https://github.com/nodejs/node/pull/35808)
* \[[`4e76a3c106`](https://github.com/nodejs/node/commit/4e76a3c106)] - **doc**: update MessagePort documentation for EventTarget inheritance (Anna Henningsen) [#35839](https://github.com/nodejs/node/pull/35839)
* \[[`3db4354cc8`](https://github.com/nodejs/node/commit/3db4354cc8)] - **doc**: use case-sensitive in the example (Pooja D.P) [#35624](https://github.com/nodejs/node/pull/35624)
* \[[`b07f4a3f7a`](https://github.com/nodejs/node/commit/b07f4a3f7a)] - **doc**: consolidate and clarify breakOnSigInt text (Rich Trott) [#35787](https://github.com/nodejs/node/pull/35787)
* \[[`c2e6a4b081`](https://github.com/nodejs/node/commit/c2e6a4b081)] - **doc**: fix \_construct example params order (Alejandro Oviedo) [#35790](https://github.com/nodejs/node/pull/35790)
* \[[`6513a589fe`](https://github.com/nodejs/node/commit/6513a589fe)] - **doc**: add a subsystems header in pull-requests.md (Pooja D.P) [#35718](https://github.com/nodejs/node/pull/35718)
* \[[`c365867c60`](https://github.com/nodejs/node/commit/c365867c60)] - **doc**: fix typo in BUILDING.md (raisinten) [#35807](https://github.com/nodejs/node/pull/35807)
* \[[`6211ffd2f7`](https://github.com/nodejs/node/commit/6211ffd2f7)] - **doc**: add require statement in the example (Pooja D.P) [#35554](https://github.com/nodejs/node/pull/35554)
* \[[`7b3743d8dd`](https://github.com/nodejs/node/commit/7b3743d8dd)] - **doc**: modified memory set statement set size (Pooja D.P) [#35517](https://github.com/nodejs/node/pull/35517)
* \[[`afbe23d800`](https://github.com/nodejs/node/commit/afbe23d800)] - **doc**: use kbd element in readline doc prose (Rich Trott) [#35737](https://github.com/nodejs/node/pull/35737)
* \[[`c0a4fac040`](https://github.com/nodejs/node/commit/c0a4fac040)] - **doc**: fix a typo in CHANGELOG\_V12 (Shubham Parihar) [#35786](https://github.com/nodejs/node/pull/35786)
* \[[`0e9acf83f7`](https://github.com/nodejs/node/commit/0e9acf83f7)] - **doc**: fix header level in fs.md (ax1) [#35771](https://github.com/nodejs/node/pull/35771)
* \[[`f49afb5e10`](https://github.com/nodejs/node/commit/f49afb5e10)] - **doc**: remove stability warning in v8 module doc (Rich Trott) [#35774](https://github.com/nodejs/node/pull/35774)
* \[[`368ae952b2`](https://github.com/nodejs/node/commit/368ae952b2)] - **doc**: mark optional parameters in timers.md (Vse Mozhe Buty) [#35764](https://github.com/nodejs/node/pull/35764)
* \[[`f6aa7c82c5`](https://github.com/nodejs/node/commit/f6aa7c82c5)] - **doc**: add a example code to API doc property (Pooja D.P) [#35738](https://github.com/nodejs/node/pull/35738)
* \[[`55b7a6cea3`](https://github.com/nodejs/node/commit/55b7a6cea3)] - **doc**: document changes for `\*/promises` alias modules (ExE Boss) [#34002](https://github.com/nodejs/node/pull/34002)
* \[[`4b7708a316`](https://github.com/nodejs/node/commit/4b7708a316)] - **doc**: update console.error example (Lee, Bonggi) [#34964](https://github.com/nodejs/node/pull/34964)
* \[[`292b529dfa`](https://github.com/nodejs/node/commit/292b529dfa)] - **doc**: add missing link in Node.js 14 Changelog (Antoine du Hamel) [#35782](https://github.com/nodejs/node/pull/35782)
* \[[`890b03ecd6`](https://github.com/nodejs/node/commit/890b03ecd6)] - **doc**: improve text for breakOnSigint (Rich Trott) [#35692](https://github.com/nodejs/node/pull/35692)
* \[[`1892532ee8`](https://github.com/nodejs/node/commit/1892532ee8)] - **doc**: this prints replaced with this is printed (Pooja D.P) [#35515](https://github.com/nodejs/node/pull/35515)
* \[[`6590f8cb4a`](https://github.com/nodejs/node/commit/6590f8cb4a)] - **doc**: update package.json field definitions (Myles Borins) [#35741](https://github.com/nodejs/node/pull/35741)
* \[[`f269c6cbe2`](https://github.com/nodejs/node/commit/f269c6cbe2)] - **doc**: add Installing Node.js header in BUILDING.md (Pooja D.P) [#35710](https://github.com/nodejs/node/pull/35710)
* \[[`05a888a8c3`](https://github.com/nodejs/node/commit/05a888a8c3)] - **doc,esm**: document experimental warning removal (Antoine du Hamel) [#35750](https://github.com/nodejs/node/pull/35750)
* \[[`092c6c4f8f`](https://github.com/nodejs/node/commit/092c6c4f8f)] - **doc,test**: update v8 method doc and comment (Rich Trott) [#35795](https://github.com/nodejs/node/pull/35795)

#### Other commits

* \[[`76ebae4c05`](https://github.com/nodejs/node/commit/76ebae4c05)] - **benchmark**: make the benchmark tool work with Node 10 (Joyee Cheung) [#35817](https://github.com/nodejs/node/pull/35817)
* \[[`9b549c1691`](https://github.com/nodejs/node/commit/9b549c1691)] - **benchmark**: add startup benchmark for loading public modules (Joyee Cheung) [#35816](https://github.com/nodejs/node/pull/35816)
* \[[`5d61e3db4b`](https://github.com/nodejs/node/commit/5d61e3db4b)] - **test**: add missing ref comments to parallel.status (Rich Trott) [#35896](https://github.com/nodejs/node/pull/35896)
* \[[`231af88001`](https://github.com/nodejs/node/commit/231af88001)] - **test**: correct test-worker-eventlooputil (Gerhard Stoebich) [#35891](https://github.com/nodejs/node/pull/35891)
* \[[`da612dfc20`](https://github.com/nodejs/node/commit/da612dfc20)] - **test**: integrate abort\_controller tests from wpt (Daijiro Wachi) [#35869](https://github.com/nodejs/node/pull/35869)
* \[[`66ad4be2c1`](https://github.com/nodejs/node/commit/66ad4be2c1)] - **test**: add test to fs/promises setImmediate (tyankatsu) [#35852](https://github.com/nodejs/node/pull/35852)
* \[[`830b789299`](https://github.com/nodejs/node/commit/830b789299)] - **test**: mark test-worker-eventlooputil flaky (Myles Borins) [#35886](https://github.com/nodejs/node/pull/35886)
* \[[`7691b673dc`](https://github.com/nodejs/node/commit/7691b673dc)] - **test**: mark test-http2-respond-file-error-pipe-offset flaky (Myles Borins) [#35883](https://github.com/nodejs/node/pull/35883)
* \[[`de3dcd7356`](https://github.com/nodejs/node/commit/de3dcd7356)] - **test**: fix reference to WPT testharness.js (Tobias Nießen) [#35814](https://github.com/nodejs/node/pull/35814)
* \[[`8958af4aa0`](https://github.com/nodejs/node/commit/8958af4aa0)] - **test**: add onerror test cases to policy (Daijiro Wachi) [#35797](https://github.com/nodejs/node/pull/35797)
* \[[`dd3cbb455a`](https://github.com/nodejs/node/commit/dd3cbb455a)] - **test**: add upstream test cases to encoding (Daijiro Wachi) [#35794](https://github.com/nodejs/node/pull/35794)
* \[[`76991c039f`](https://github.com/nodejs/node/commit/76991c039f)] - **test**: add upstream test cases to urlsearchparam (Daijiro Wachi) [#35792](https://github.com/nodejs/node/pull/35792)
* \[[`110ef8aa50`](https://github.com/nodejs/node/commit/110ef8aa50)] - **test**: refactor coverage logic (bcoe) [#35767](https://github.com/nodejs/node/pull/35767)
* \[[`0c5e8ed651`](https://github.com/nodejs/node/commit/0c5e8ed651)] - **test**: add additional deprecation warning tests for rmdir recursive (Ian Sutherland) [#35683](https://github.com/nodejs/node/pull/35683)
* \[[`11eca36e83`](https://github.com/nodejs/node/commit/11eca36e83)] - **test**: add windows and C++ coverage (Benjamin Coe) [#35670](https://github.com/nodejs/node/pull/35670)
* \[[`fd027cd61a`](https://github.com/nodejs/node/commit/fd027cd61a)] - **tools**: bump remark-lint-preset-node to 2.0.0 (Rich Trott) [#35905](https://github.com/nodejs/node/pull/35905)
* \[[`c09fdba786`](https://github.com/nodejs/node/commit/c09fdba786)] - **tools**: refloat 7 Node.js patches to cpplint.py (Rich Trott) [#35866](https://github.com/nodejs/node/pull/35866)
* \[[`3955ccd305`](https://github.com/nodejs/node/commit/3955ccd305)] - **tools**: bump cpplint to 1.5.1 (Rich Trott) [#35866](https://github.com/nodejs/node/pull/35866)
* \[[`a07d1af4ea`](https://github.com/nodejs/node/commit/a07d1af4ea)] - **tools**: update ESLint to 7.12.1 (cjihrig) [#35799](https://github.com/nodejs/node/pull/35799)
* \[[`d20b318c58`](https://github.com/nodejs/node/commit/d20b318c58)] - **tools**: update ESLint to 7.12.0 (cjihrig) [#35799](https://github.com/nodejs/node/pull/35799)
* \[[`31753ec8aa`](https://github.com/nodejs/node/commit/31753ec8aa)] - **tools**: add msvc /P output to .gitignore (Jiawen Geng) [#35735](https://github.com/nodejs/node/pull/35735)
* \[[`afb3e24cb0`](https://github.com/nodejs/node/commit/afb3e24cb0)] - **tools**: add update-npm script (Myles Borins) [#35822](https://github.com/nodejs/node/pull/35822)
* \[[`66da122d46`](https://github.com/nodejs/node/commit/66da122d46)] - **tools**: refloat 7 Node.js patches to cpplint.py (Rich Trott) [#35719](https://github.com/nodejs/node/pull/35719)
* \[[`042d4dd71c`](https://github.com/nodejs/node/commit/042d4dd71c)] - **tools**: bump cpplint to 1.5.0 (Rich Trott) [#35719](https://github.com/nodejs/node/pull/35719)

<a id="15.0.1"></a>

## 2020-10-21, Version 15.0.1 (Current), @BethGriggs

### Notable changes

* **crypto**: fix regression on randomFillSync (James M Snell) [#35723](https://github.com/nodejs/node/pull/35723)
  * This fixes issue <https://github.com/nodejs/node/issues/35722>.
* **deps**: upgrade npm to 7.0.3 (Ruy Adorno) [#35724](https://github.com/nodejs/node/pull/35724)
* **doc**: add release key for Danielle Adams (Danielle Adams) [#35545](https://github.com/nodejs/node/pull/35545)

### Commits

* \[[`c509485c19`](https://github.com/nodejs/node/commit/c509485c19)] - **build**: use make functions instead of echo (Antoine du Hamel) [#35707](https://github.com/nodejs/node/pull/35707)
* \[[`f5acc2d030`](https://github.com/nodejs/node/commit/f5acc2d030)] - **crypto**: fix regression on randomFillSync (James M Snell) [#35723](https://github.com/nodejs/node/pull/35723)
* \[[`595c8df48d`](https://github.com/nodejs/node/commit/595c8df48d)] - **deps**: upgrade npm to 7.0.3 (Ruy Adorno) [#35724](https://github.com/nodejs/node/pull/35724)
* \[[`69e7f20f2d`](https://github.com/nodejs/node/commit/69e7f20f2d)] - **deps**: V8: set correct V8 version patch number (Michaël Zasso) [#35732](https://github.com/nodejs/node/pull/35732)
* \[[`b78294dc00`](https://github.com/nodejs/node/commit/b78294dc00)] - **doc**: use kbd element in readline doc (Rich Trott) [#35698](https://github.com/nodejs/node/pull/35698)
* \[[`1efa87082b`](https://github.com/nodejs/node/commit/1efa87082b)] - **doc**: add release key for Danielle Adams (Danielle Adams) [#35545](https://github.com/nodejs/node/pull/35545)
* \[[`6e91d644e3`](https://github.com/nodejs/node/commit/6e91d644e3)] - **doc**: use kbd element in os doc (Rich Trott) [#35656](https://github.com/nodejs/node/pull/35656)
* \[[`5a48a7b6f8`](https://github.com/nodejs/node/commit/5a48a7b6f8)] - **doc**: add a statement in the documentation. (Pooja D.P) [#35585](https://github.com/nodejs/node/pull/35585)
* \[[`6a7a61be7c`](https://github.com/nodejs/node/commit/6a7a61be7c)] - **src**: mark/pop OpenSSL errors in NewRootCertStore (Daniel Bevenius) [#35514](https://github.com/nodejs/node/pull/35514)
* \[[`d54edece99`](https://github.com/nodejs/node/commit/d54edece99)] - **test**: refactor test-crypto-pbkdf2 (Tobias Nießen) [#35693](https://github.com/nodejs/node/pull/35693)

<a id="15.0.0"></a>

## 2020-10-20, Version 15.0.0 (Current), @BethGriggs

### Notable Changes

#### Deprecations and Removals

* \[[`a11788736a`](https://github.com/nodejs/node/commit/a11788736a)] - **(SEMVER-MAJOR)** **build**: remove --build-v8-with-gn configure option (Yang Guo) [#27576](https://github.com/nodejs/node/pull/27576)
* \[[`89428c7a2d`](https://github.com/nodejs/node/commit/89428c7a2d)] - **(SEMVER-MAJOR)** **build**: drop support for VS2017 (Michaël Zasso) [#33694](https://github.com/nodejs/node/pull/33694)
* \[[`c25cf34ac1`](https://github.com/nodejs/node/commit/c25cf34ac1)] - **(SEMVER-MAJOR)** **doc**: move DEP0018 to End-of-Life (Rich Trott) [#35316](https://github.com/nodejs/node/pull/35316)
* \[[`2002d90abd`](https://github.com/nodejs/node/commit/2002d90abd)] - **(SEMVER-MAJOR)** **fs**: deprecation warning on recursive rmdir (Ian Sutherland) [#35562](https://github.com/nodejs/node/pull/35562)
* \[[`eee522ac29`](https://github.com/nodejs/node/commit/eee522ac29)] - **(SEMVER-MAJOR)** **lib**: add EventTarget-related browser globals (Anna Henningsen) [#35496](https://github.com/nodejs/node/pull/35496)
* \[[`41796ebd30`](https://github.com/nodejs/node/commit/41796ebd30)] - **(SEMVER-MAJOR)** **net**: remove long deprecated server.connections property (James M Snell) [#33647](https://github.com/nodejs/node/pull/33647)
* \[[`a416692e93`](https://github.com/nodejs/node/commit/a416692e93)] - **(SEMVER-MAJOR)** **repl**: remove deprecated repl.memory function (Ruben Bridgewater) [#33286](https://github.com/nodejs/node/pull/33286)
* \[[`f217b2dfb0`](https://github.com/nodejs/node/commit/f217b2dfb0)] - **(SEMVER-MAJOR)** **repl**: remove deprecated repl.turnOffEditorMode() function (Ruben Bridgewater) [#33286](https://github.com/nodejs/node/pull/33286)
* \[[`a1bcad8dc0`](https://github.com/nodejs/node/commit/a1bcad8dc0)] - **(SEMVER-MAJOR)** **repl**: remove deprecated repl.parseREPLKeyword() function (Ruben Bridgewater) [#33286](https://github.com/nodejs/node/pull/33286)
* \[[`4ace010b53`](https://github.com/nodejs/node/commit/4ace010b53)] - **(SEMVER-MAJOR)** **repl**: remove deprecated bufferedCommand property (Ruben Bridgewater) [#33286](https://github.com/nodejs/node/pull/33286)
* \[[`37524307fe`](https://github.com/nodejs/node/commit/37524307fe)] - **(SEMVER-MAJOR)** **repl**: remove deprecated .rli (Ruben Bridgewater) [#33286](https://github.com/nodejs/node/pull/33286)
* \[[`a85ce885bd`](https://github.com/nodejs/node/commit/a85ce885bd)] - **(SEMVER-MAJOR)** **src**: remove deprecated node debug command (James M Snell) [#33648](https://github.com/nodejs/node/pull/33648)
* \[[`a8904e8eee`](https://github.com/nodejs/node/commit/a8904e8eee)] - **(SEMVER-MAJOR)** **timers**: introduce timers/promises (James M Snell) [#33950](https://github.com/nodejs/node/pull/33950)
* \[[`1211b9a72f`](https://github.com/nodejs/node/commit/1211b9a72f)] - **(SEMVER-MAJOR)** **util**: change default value of `maxStringLength` to 10000 (unknown) [#32744](https://github.com/nodejs/node/pull/32744)
* \[[`ca8f3ef2e5`](https://github.com/nodejs/node/commit/ca8f3ef2e5)] - **(SEMVER-MAJOR)** **wasi**: drop --experimental-wasm-bigint requirement (Colin Ihrig) [#35415](https://github.com/nodejs/node/pull/35415)

#### npm 7 - [#35631](https://github.com/nodejs/node/pull/35631)

Node.js 15 comes with a new major release of npm, npm 7. npm 7 comes with many new features - including npm workspaces and a new package-lock.json format. npm 7 also includes yarn.lock file support. One of the big changes in npm 7 is that peer dependencies are now installed by default.

#### Throw On Unhandled Rejections - [#33021](https://github.com/nodejs/node/pull/33021)

As of Node.js 15, the default mode for `unhandledRejection` is changed to `throw` (from `warn`). In `throw` mode, if an `unhandledRejection` hook is not set, the `unhandledRejection` is raised as an uncaught exception. Users that have an `unhandledRejection` hook should see no change in behavior, and it’s still possible to switch modes using the `--unhandled-rejections=mode` process flag.

#### QUIC - [#32379](https://github.com/nodejs/node/pull/32379)

Node.js 15 comes with experimental support for [QUIC](https://en.wikipedia.org/wiki/QUIC), which can be enabled by compiling Node.js with the `--experimental-quic` configuration flag. The Node.js QUIC implementation is exposed by the core `net` module.

#### V8 8.6 - [#35415](https://github.com/nodejs/node/pull/35415)

The V8 JavaScript engine has been updated to V8 8.6 (V8 8.4 is the latest available in Node.js 14). Along with performance tweaks and improvements the V8 update also brings the following language features:

* `Promise.any()` (from V8 8.5)
* `AggregateError` (from V8 8.5)
* `String.prototype.replaceAll()` (from V8 8.5)
* Logical assignment operators `&&=`, `||=`, and `??=` (from V8 8.5)

#### Other Notable Changes

* \[[`50228cf6ff`](https://github.com/nodejs/node/commit/50228cf6ff)] - **(SEMVER-MAJOR)** **assert**: add `assert/strict` alias module (ExE Boss) [#34001](https://github.com/nodejs/node/pull/34001)
* \[[`039cd00a9a`](https://github.com/nodejs/node/commit/039cd00a9a)] - **(SEMVER-MAJOR)** **dns**: add dns/promises alias (shisama) [#32953](https://github.com/nodejs/node/pull/32953)
* \[[`54b36e401d`](https://github.com/nodejs/node/commit/54b36e401d)] - **(SEMVER-MAJOR)** **fs**: reimplement read and write streams using stream.construct (Robert Nagy) [#29656](https://github.com/nodejs/node/pull/29656)
* \[[`f5c0e282cc`](https://github.com/nodejs/node/commit/f5c0e282cc)] - **(SEMVER-MAJOR)** **http2**: allow Host in HTTP/2 requests (Alba Mendez) [#34664](https://github.com/nodejs/node/pull/34664)
* \[[`eee522ac29`](https://github.com/nodejs/node/commit/eee522ac29)] - **(SEMVER-MAJOR)** **lib**: add EventTarget-related browser globals (Anna Henningsen) [#35496](https://github.com/nodejs/node/pull/35496)
* \[[`a8b26d72c5`](https://github.com/nodejs/node/commit/a8b26d72c5)] - **(SEMVER-MAJOR)** **lib**: unflag AbortController (James M Snell) [#33527](https://github.com/nodejs/node/pull/33527)
* \[[`74ca960aac`](https://github.com/nodejs/node/commit/74ca960aac)] - **(SEMVER-MAJOR)** **lib**: initial experimental AbortController implementation (James M Snell) [#33527](https://github.com/nodejs/node/pull/33527)
* \[[`efefdd668d`](https://github.com/nodejs/node/commit/efefdd668d)] - **(SEMVER-MAJOR)** **net**: autoDestroy Socket (Robert Nagy) [#31806](https://github.com/nodejs/node/pull/31806)
* \[[`0fb91acedf`](https://github.com/nodejs/node/commit/0fb91acedf)] - **(SEMVER-MAJOR)** **src**: disallow JS execution inside FreeEnvironment (Anna Henningsen) [#33874](https://github.com/nodejs/node/pull/33874)
* \[[`21782277c2`](https://github.com/nodejs/node/commit/21782277c2)] - **(SEMVER-MAJOR)** **src**: use node:moduleName as builtin module filename (Michaël Zasso) [#35498](https://github.com/nodejs/node/pull/35498)
* \[[`fb8cc72e73`](https://github.com/nodejs/node/commit/fb8cc72e73)] - **(SEMVER-MAJOR)** **stream**: construct (Robert Nagy) [#29656](https://github.com/nodejs/node/pull/29656)
* \[[`705d888387`](https://github.com/nodejs/node/commit/705d888387)] - **(SEMVER-MAJOR)** **worker**: make MessageEvent class more Web-compatible (Anna Henningsen) [#35496](https://github.com/nodejs/node/pull/35496)

### Semver-Major Commits

* \[[`50228cf6ff`](https://github.com/nodejs/node/commit/50228cf6ff)] - **(SEMVER-MAJOR)** **assert**: add `assert/strict` alias module (ExE Boss) [#34001](https://github.com/nodejs/node/pull/34001)
* \[[`d701247165`](https://github.com/nodejs/node/commit/d701247165)] - **(SEMVER-MAJOR)** **build**: reset embedder string to "-node.0" (Michaël Zasso) [#35415](https://github.com/nodejs/node/pull/35415)
* \[[`a11788736a`](https://github.com/nodejs/node/commit/a11788736a)] - **(SEMVER-MAJOR)** **build**: remove --build-v8-with-gn configure option (Yang Guo) [#27576](https://github.com/nodejs/node/pull/27576)
* \[[`89428c7a2d`](https://github.com/nodejs/node/commit/89428c7a2d)] - **(SEMVER-MAJOR)** **build**: drop support for VS2017 (Michaël Zasso) [#33694](https://github.com/nodejs/node/pull/33694)
* \[[`dae283d96f`](https://github.com/nodejs/node/commit/dae283d96f)] - **(SEMVER-MAJOR)** **crypto**: refactoring internals, add WebCrypto (James M Snell) [#35093](https://github.com/nodejs/node/pull/35093)
* \[[`ba77dc8597`](https://github.com/nodejs/node/commit/ba77dc8597)] - **(SEMVER-MAJOR)** **crypto**: move node\_crypto files to src/crypto (James M Snell) [#35093](https://github.com/nodejs/node/pull/35093)
* \[[`9378070da0`](https://github.com/nodejs/node/commit/9378070da0)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick d76abfed3512 (Michaël Zasso) [#35415](https://github.com/nodejs/node/pull/35415)
* \[[`efee8341ad`](https://github.com/nodejs/node/commit/efee8341ad)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 717543bbf0ef (Michaël Zasso) [#35415](https://github.com/nodejs/node/pull/35415)
* \[[`b006fa8730`](https://github.com/nodejs/node/commit/b006fa8730)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 6be2f6e26e8d (Michaël Zasso) [#35415](https://github.com/nodejs/node/pull/35415)
* \[[`3c23af4cb7`](https://github.com/nodejs/node/commit/3c23af4cb7)] - **(SEMVER-MAJOR)** **deps**: fix V8 build issue with inline methods (Jiawen Geng) [#35415](https://github.com/nodejs/node/pull/35415)
* \[[`b803b3f48b`](https://github.com/nodejs/node/commit/b803b3f48b)] - **(SEMVER-MAJOR)** **deps**: fix platform-embedded-file-writer-win for ARM64 (Michaël Zasso) [#35415](https://github.com/nodejs/node/pull/35415)
* \[[`47cb9f14e8`](https://github.com/nodejs/node/commit/47cb9f14e8)] - **(SEMVER-MAJOR)** **deps**: update V8 postmortem metadata script (Colin Ihrig) [#35415](https://github.com/nodejs/node/pull/35415)
* \[[`a1d639ba5d`](https://github.com/nodejs/node/commit/a1d639ba5d)] - **(SEMVER-MAJOR)** **deps**: update V8 to 8.6.395 (Michaël Zasso) [#35415](https://github.com/nodejs/node/pull/35415)
* \[[`3ddcad55fb`](https://github.com/nodejs/node/commit/3ddcad55fb)] - **(SEMVER-MAJOR)** **deps**: upgrade npm to 7.0.0 (Myles Borins) [#35631](https://github.com/nodejs/node/pull/35631)
* \[[`2e54524955`](https://github.com/nodejs/node/commit/2e54524955)] - **(SEMVER-MAJOR)** **deps**: update npm to 7.0.0-rc.3 (Myles Borins) [#35474](https://github.com/nodejs/node/pull/35474)
* \[[`e983b1cece`](https://github.com/nodejs/node/commit/e983b1cece)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 0d6debcc5f08 (Gus Caplan) [#33600](https://github.com/nodejs/node/pull/33600)
* \[[`039cd00a9a`](https://github.com/nodejs/node/commit/039cd00a9a)] - **(SEMVER-MAJOR)** **dns**: add dns/promises alias (shisama) [#32953](https://github.com/nodejs/node/pull/32953)
* \[[`c25cf34ac1`](https://github.com/nodejs/node/commit/c25cf34ac1)] - **(SEMVER-MAJOR)** **doc**: move DEP0018 to End-of-Life (Rich Trott) [#35316](https://github.com/nodejs/node/pull/35316)
* \[[`8bf37ee496`](https://github.com/nodejs/node/commit/8bf37ee496)] - **(SEMVER-MAJOR)** **doc**: update support macos version for 15.x (Ash Cripps) [#35022](https://github.com/nodejs/node/pull/35022)
* \[[`2002d90abd`](https://github.com/nodejs/node/commit/2002d90abd)] - **(SEMVER-MAJOR)** **fs**: deprecation warning on recursive rmdir (Ian Sutherland) [#35562](https://github.com/nodejs/node/pull/35562)
* \[[`54b36e401d`](https://github.com/nodejs/node/commit/54b36e401d)] - **(SEMVER-MAJOR)** **fs**: reimplement read and write streams using stream.construct (Robert Nagy) [#29656](https://github.com/nodejs/node/pull/29656)
* \[[`32b641e528`](https://github.com/nodejs/node/commit/32b641e528)] - **(SEMVER-MAJOR)** **http**: fixed socket.setEncoding fatal error (iskore) [#33405](https://github.com/nodejs/node/pull/33405)
* \[[`8a6fab02ad`](https://github.com/nodejs/node/commit/8a6fab02ad)] - **(SEMVER-MAJOR)** **http**: emit 'error' on aborted server request (Robert Nagy) [#33172](https://github.com/nodejs/node/pull/33172)
* \[[`d005f490a8`](https://github.com/nodejs/node/commit/d005f490a8)] - **(SEMVER-MAJOR)** **http**: cleanup end argument handling (Robert Nagy) [#31818](https://github.com/nodejs/node/pull/31818)
* \[[`f5c0e282cc`](https://github.com/nodejs/node/commit/f5c0e282cc)] - **(SEMVER-MAJOR)** **http2**: allow Host in HTTP/2 requests (Alba Mendez) [#34664](https://github.com/nodejs/node/pull/34664)
* \[[`1e4187fcf4`](https://github.com/nodejs/node/commit/1e4187fcf4)] - **(SEMVER-MAJOR)** **http2**: add `invalidheaders` test (Pranshu Srivastava) [#33161](https://github.com/nodejs/node/pull/33161)
* \[[`d79c330186`](https://github.com/nodejs/node/commit/d79c330186)] - **(SEMVER-MAJOR)** **http2**: refactor state code validation for the http2Stream class (rickyes) [#33535](https://github.com/nodejs/node/pull/33535)
* \[[`df31f71f1e`](https://github.com/nodejs/node/commit/df31f71f1e)] - **(SEMVER-MAJOR)** **http2**: header field valid checks (Pranshu Srivastava) [#33193](https://github.com/nodejs/node/pull/33193)
* \[[`1428db8a1f`](https://github.com/nodejs/node/commit/1428db8a1f)] - **(SEMVER-MAJOR)** **lib**: refactor Socket.\_getpeername and Socket.\_getsockname (himself65) [#32969](https://github.com/nodejs/node/pull/32969)
* \[[`eee522ac29`](https://github.com/nodejs/node/commit/eee522ac29)] - **(SEMVER-MAJOR)** **lib**: add EventTarget-related browser globals (Anna Henningsen) [#35496](https://github.com/nodejs/node/pull/35496)
* \[[`c66e6471e7`](https://github.com/nodejs/node/commit/c66e6471e7)] - **(SEMVER-MAJOR)** **lib**: remove ERR\_INVALID\_OPT\_VALUE and ERR\_INVALID\_OPT\_VALUE\_ENCODING (Denys Otrishko) [#34682](https://github.com/nodejs/node/pull/34682)
* \[[`b546a2b469`](https://github.com/nodejs/node/commit/b546a2b469)] - **(SEMVER-MAJOR)** **lib**: handle one of args case in ERR\_MISSING\_ARGS (Denys Otrishko) [#34022](https://github.com/nodejs/node/pull/34022)
* \[[`a86a295fd7`](https://github.com/nodejs/node/commit/a86a295fd7)] - **(SEMVER-MAJOR)** **lib**: remove NodeError from the prototype of errors with code (Michaël Zasso) [#33857](https://github.com/nodejs/node/pull/33857)
* \[[`a8b26d72c5`](https://github.com/nodejs/node/commit/a8b26d72c5)] - **(SEMVER-MAJOR)** **lib**: unflag AbortController (James M Snell) [#33527](https://github.com/nodejs/node/pull/33527)
* \[[`74ca960aac`](https://github.com/nodejs/node/commit/74ca960aac)] - **(SEMVER-MAJOR)** **lib**: initial experimental AbortController implementation (James M Snell) [#33527](https://github.com/nodejs/node/pull/33527)
* \[[`78ca61e2cf`](https://github.com/nodejs/node/commit/78ca61e2cf)] - **(SEMVER-MAJOR)** **net**: check args in net.connect() and socket.connect() calls (Denys Otrishko) [#34022](https://github.com/nodejs/node/pull/34022)
* \[[`41796ebd30`](https://github.com/nodejs/node/commit/41796ebd30)] - **(SEMVER-MAJOR)** **net**: remove long deprecated server.connections property (James M Snell) [#33647](https://github.com/nodejs/node/pull/33647)
* \[[`efefdd668d`](https://github.com/nodejs/node/commit/efefdd668d)] - **(SEMVER-MAJOR)** **net**: autoDestroy Socket (Robert Nagy) [#31806](https://github.com/nodejs/node/pull/31806)
* \[[`6cfba9f7f6`](https://github.com/nodejs/node/commit/6cfba9f7f6)] - **(SEMVER-MAJOR)** **process**: update v8 fast api calls usage (Maya Lekova) [#35415](https://github.com/nodejs/node/pull/35415)
* \[[`3b10f7f933`](https://github.com/nodejs/node/commit/3b10f7f933)] - **(SEMVER-MAJOR)** **process**: change default --unhandled-rejections=throw (Dan Fabulich) [#33021](https://github.com/nodejs/node/pull/33021)
* \[[`d8eef83757`](https://github.com/nodejs/node/commit/d8eef83757)] - **(SEMVER-MAJOR)** **process**: use v8 fast api calls for hrtime (Gus Caplan) [#33600](https://github.com/nodejs/node/pull/33600)
* \[[`49745cdef0`](https://github.com/nodejs/node/commit/49745cdef0)] - **(SEMVER-MAJOR)** **process**: delay throwing an error using `throwDeprecation` (Ruben Bridgewater) [#32312](https://github.com/nodejs/node/pull/32312)
* \[[`a416692e93`](https://github.com/nodejs/node/commit/a416692e93)] - **(SEMVER-MAJOR)** **repl**: remove deprecated repl.memory function (Ruben Bridgewater) [#33286](https://github.com/nodejs/node/pull/33286)
* \[[`f217b2dfb0`](https://github.com/nodejs/node/commit/f217b2dfb0)] - **(SEMVER-MAJOR)** **repl**: remove deprecated repl.turnOffEditorMode() function (Ruben Bridgewater) [#33286](https://github.com/nodejs/node/pull/33286)
* \[[`a1bcad8dc0`](https://github.com/nodejs/node/commit/a1bcad8dc0)] - **(SEMVER-MAJOR)** **repl**: remove deprecated repl.parseREPLKeyword() function (Ruben Bridgewater) [#33286](https://github.com/nodejs/node/pull/33286)
* \[[`4ace010b53`](https://github.com/nodejs/node/commit/4ace010b53)] - **(SEMVER-MAJOR)** **repl**: remove deprecated bufferedCommand property (Ruben Bridgewater) [#33286](https://github.com/nodejs/node/pull/33286)
* \[[`37524307fe`](https://github.com/nodejs/node/commit/37524307fe)] - **(SEMVER-MAJOR)** **repl**: remove deprecated .rli (Ruben Bridgewater) [#33286](https://github.com/nodejs/node/pull/33286)
* \[[`b65e5aeaa7`](https://github.com/nodejs/node/commit/b65e5aeaa7)] - **(SEMVER-MAJOR)** **src**: implement NodePlatform::PostJob (Clemens Backes) [#35415](https://github.com/nodejs/node/pull/35415)
* \[[`b1e8e0e604`](https://github.com/nodejs/node/commit/b1e8e0e604)] - **(SEMVER-MAJOR)** **src**: update NODE\_MODULE\_VERSION to 88 (Michaël Zasso) [#35415](https://github.com/nodejs/node/pull/35415)
* \[[`eeb6b473fd`](https://github.com/nodejs/node/commit/eeb6b473fd)] - **(SEMVER-MAJOR)** **src**: error reporting on CPUUsage (Yash Ladha) [#34762](https://github.com/nodejs/node/pull/34762)
* \[[`21782277c2`](https://github.com/nodejs/node/commit/21782277c2)] - **(SEMVER-MAJOR)** **src**: use node:moduleName as builtin module filename (Michaël Zasso) [#35498](https://github.com/nodejs/node/pull/35498)
* \[[`05771279af`](https://github.com/nodejs/node/commit/05771279af)] - **(SEMVER-MAJOR)** **src**: enable wasm trap handler on windows (Gus Caplan) [#35033](https://github.com/nodejs/node/pull/35033)
* \[[`b7cf823410`](https://github.com/nodejs/node/commit/b7cf823410)] - **(SEMVER-MAJOR)** **src**: update NODE\_MODULE\_VERSION to 86 (Michaël Zasso) [#33579](https://github.com/nodejs/node/pull/33579)
* \[[`0fb91acedf`](https://github.com/nodejs/node/commit/0fb91acedf)] - **(SEMVER-MAJOR)** **src**: disallow JS execution inside FreeEnvironment (Anna Henningsen) [#33874](https://github.com/nodejs/node/pull/33874)
* \[[`53fb2b6b41`](https://github.com/nodejs/node/commit/53fb2b6b41)] - **(SEMVER-MAJOR)** **src**: remove \_third\_party\_main support (Anna Henningsen) [#33971](https://github.com/nodejs/node/pull/33971)
* \[[`a85ce885bd`](https://github.com/nodejs/node/commit/a85ce885bd)] - **(SEMVER-MAJOR)** **src**: remove deprecated node debug command (James M Snell) [#33648](https://github.com/nodejs/node/pull/33648)
* \[[`ac3714637e`](https://github.com/nodejs/node/commit/ac3714637e)] - **(SEMVER-MAJOR)** **src**: remove unused CancelPendingDelayedTasks (Anna Henningsen) [#32859](https://github.com/nodejs/node/pull/32859)
* \[[`a65218f5e8`](https://github.com/nodejs/node/commit/a65218f5e8)] - **(SEMVER-MAJOR)** **stream**: try to wait for flush to complete before 'finish' (Robert Nagy) [#34314](https://github.com/nodejs/node/pull/34314)
* \[[`4e3f6f355b`](https://github.com/nodejs/node/commit/4e3f6f355b)] - **(SEMVER-MAJOR)** **stream**: cleanup and fix Readable.wrap (Robert Nagy) [#34204](https://github.com/nodejs/node/pull/34204)
* \[[`527e2147af`](https://github.com/nodejs/node/commit/527e2147af)] - **(SEMVER-MAJOR)** **stream**: add promises version to utility functions (rickyes) [#33991](https://github.com/nodejs/node/pull/33991)
* \[[`c7e55c6b72`](https://github.com/nodejs/node/commit/c7e55c6b72)] - **(SEMVER-MAJOR)** **stream**: fix writable.end callback behavior (Robert Nagy) [#34101](https://github.com/nodejs/node/pull/34101)
* \[[`fb8cc72e73`](https://github.com/nodejs/node/commit/fb8cc72e73)] - **(SEMVER-MAJOR)** **stream**: construct (Robert Nagy) [#29656](https://github.com/nodejs/node/pull/29656)
* \[[`4bc7025309`](https://github.com/nodejs/node/commit/4bc7025309)] - **(SEMVER-MAJOR)** **stream**: write should throw on unknown encoding (Robert Nagy) [#33075](https://github.com/nodejs/node/pull/33075)
* \[[`ea87809bb6`](https://github.com/nodejs/node/commit/ea87809bb6)] - **(SEMVER-MAJOR)** **stream**: fix \_final and 'prefinish' timing (Robert Nagy) [#32780](https://github.com/nodejs/node/pull/32780)
* \[[`0bd5595509`](https://github.com/nodejs/node/commit/0bd5595509)] - **(SEMVER-MAJOR)** **stream**: simplify Transform stream implementation (Robert Nagy) [#32763](https://github.com/nodejs/node/pull/32763)
* \[[`8f86986985`](https://github.com/nodejs/node/commit/8f86986985)] - **(SEMVER-MAJOR)** **stream**: use callback to properly propagate error (Robert Nagy) [#29179](https://github.com/nodejs/node/pull/29179)
* \[[`94dd7b9f94`](https://github.com/nodejs/node/commit/94dd7b9f94)] - **(SEMVER-MAJOR)** **test**: update tests after increasing typed array size to 4GB (Kim-Anh Tran) [#35415](https://github.com/nodejs/node/pull/35415)
* \[[`d9e98df01b`](https://github.com/nodejs/node/commit/d9e98df01b)] - **(SEMVER-MAJOR)** **test**: fix tests for npm 7.0.0 (Myles Borins) [#35631](https://github.com/nodejs/node/pull/35631)
* \[[`c87641aa97`](https://github.com/nodejs/node/commit/c87641aa97)] - **(SEMVER-MAJOR)** **test**: fix test suite to work with npm 7 (Myles Borins) [#35474](https://github.com/nodejs/node/pull/35474)
* \[[`eb9d7a437e`](https://github.com/nodejs/node/commit/eb9d7a437e)] - **(SEMVER-MAJOR)** **test**: update WPT harness and tests (Michaël Zasso) [#33770](https://github.com/nodejs/node/pull/33770)
* \[[`a8904e8eee`](https://github.com/nodejs/node/commit/a8904e8eee)] - **(SEMVER-MAJOR)** **timers**: introduce timers/promises (James M Snell) [#33950](https://github.com/nodejs/node/pull/33950)
* \[[`c55f661551`](https://github.com/nodejs/node/commit/c55f661551)] - **(SEMVER-MAJOR)** **tools**: disable x86 safe exception handlers in V8 (Michaël Zasso) [#35415](https://github.com/nodejs/node/pull/35415)
* \[[`80e8aec4a5`](https://github.com/nodejs/node/commit/80e8aec4a5)] - **(SEMVER-MAJOR)** **tools**: update V8 gypfiles for 8.6 (Ujjwal Sharma) [#35415](https://github.com/nodejs/node/pull/35415)
* \[[`faeb9607c6`](https://github.com/nodejs/node/commit/faeb9607c6)] - **(SEMVER-MAJOR)** **tools**: update V8 gypfiles for 8.5 (Ujjwal Sharma) [#35415](https://github.com/nodejs/node/pull/35415)
* \[[`bb62f4ad9e`](https://github.com/nodejs/node/commit/bb62f4ad9e)] - **(SEMVER-MAJOR)** **url**: file URL path normalization (Daijiro Wachi) [#35477](https://github.com/nodejs/node/pull/35477)
* \[[`69ef4c2375`](https://github.com/nodejs/node/commit/69ef4c2375)] - **(SEMVER-MAJOR)** **url**: verify domain is not empty after "ToASCII" (Michaël Zasso) [#33770](https://github.com/nodejs/node/pull/33770)
* \[[`4831278a16`](https://github.com/nodejs/node/commit/4831278a16)] - **(SEMVER-MAJOR)** **url**: remove U+0000 case in the fragment state (Michaël Zasso) [#33770](https://github.com/nodejs/node/pull/33770)
* \[[`0d08d5ae7c`](https://github.com/nodejs/node/commit/0d08d5ae7c)] - **(SEMVER-MAJOR)** **url**: remove gopher from special schemes (Michaël Zasso) [#33325](https://github.com/nodejs/node/pull/33325)
* \[[`9be51ee9a1`](https://github.com/nodejs/node/commit/9be51ee9a1)] - **(SEMVER-MAJOR)** **url**: forbid lt and gt in url host code point (Yash Ladha) [#33328](https://github.com/nodejs/node/pull/33328)
* \[[`1211b9a72f`](https://github.com/nodejs/node/commit/1211b9a72f)] - **(SEMVER-MAJOR)** **util**: change default value of `maxStringLength` to 10000 (unknown) [#32744](https://github.com/nodejs/node/pull/32744)
* \[[`ca8f3ef2e5`](https://github.com/nodejs/node/commit/ca8f3ef2e5)] - **(SEMVER-MAJOR)** **wasi**: drop --experimental-wasm-bigint requirement (Colin Ihrig) [#35415](https://github.com/nodejs/node/pull/35415)
* \[[`abd8cdfc4e`](https://github.com/nodejs/node/commit/abd8cdfc4e)] - **(SEMVER-MAJOR)** **win, child\_process**: sanitize env variables (Bartosz Sosnowski) [#35210](https://github.com/nodejs/node/pull/35210)
* \[[`705d888387`](https://github.com/nodejs/node/commit/705d888387)] - **(SEMVER-MAJOR)** **worker**: make MessageEvent class more Web-compatible (Anna Henningsen) [#35496](https://github.com/nodejs/node/pull/35496)
* \[[`7603c7e50c`](https://github.com/nodejs/node/commit/7603c7e50c)] - **(SEMVER-MAJOR)** **worker**: set trackUnmanagedFds to true by default (Anna Henningsen) [#34394](https://github.com/nodejs/node/pull/34394)
* \[[`5ef5116311`](https://github.com/nodejs/node/commit/5ef5116311)] - **(SEMVER-MAJOR)** **worker**: rename error code to be more accurate (Anna Henningsen) [#33872](https://github.com/nodejs/node/pull/33872)

### Semver-Minor Commits

* \[[`1d5fa88eb8`](https://github.com/nodejs/node/commit/1d5fa88eb8)] - **(SEMVER-MINOR)** **cli**: add --node-memory-debug option (Anna Henningsen) [#35537](https://github.com/nodejs/node/pull/35537)
* \[[`095be6a01f`](https://github.com/nodejs/node/commit/095be6a01f)] - **(SEMVER-MINOR)** **crypto**: add getCipherInfo method (James M Snell) [#35368](https://github.com/nodejs/node/pull/35368)
* \[[`df1023bb22`](https://github.com/nodejs/node/commit/df1023bb22)] - **(SEMVER-MINOR)** **events**: allow use of AbortController with on (James M Snell) [#34912](https://github.com/nodejs/node/pull/34912)
* \[[`883fc779b6`](https://github.com/nodejs/node/commit/883fc779b6)] - **(SEMVER-MINOR)** **events**: allow use of AbortController with once (James M Snell) [#34911](https://github.com/nodejs/node/pull/34911)
* \[[`e876c0c308`](https://github.com/nodejs/node/commit/e876c0c308)] - **(SEMVER-MINOR)** **http2**: add support for sensitive headers (Anna Henningsen) [#34145](https://github.com/nodejs/node/pull/34145)
* \[[`6f34498148`](https://github.com/nodejs/node/commit/6f34498148)] - **(SEMVER-MINOR)** **net**: add support for resolving DNS CAA records (Danny Sonnenschein) [#35466](https://github.com/nodejs/node/pull/35466)
* \[[`37a8179673`](https://github.com/nodejs/node/commit/37a8179673)] - **(SEMVER-MINOR)** **net**: make blocklist family case insensitive (James M Snell) [#34864](https://github.com/nodejs/node/pull/34864)
* \[[`1f9b20b637`](https://github.com/nodejs/node/commit/1f9b20b637)] - **(SEMVER-MINOR)** **net**: introduce net.BlockList (James M Snell) [#34625](https://github.com/nodejs/node/pull/34625)
* \[[`278d38f4cf`](https://github.com/nodejs/node/commit/278d38f4cf)] - **(SEMVER-MINOR)** **src**: add maybe versions of EmitExit and EmitBeforeExit (Anna Henningsen) [#35486](https://github.com/nodejs/node/pull/35486)
* \[[`2310f679a1`](https://github.com/nodejs/node/commit/2310f679a1)] - **(SEMVER-MINOR)** **src**: move node\_binding to modern THROW\_ERR\* (James M Snell) [#35469](https://github.com/nodejs/node/pull/35469)
* \[[`744a284ccc`](https://github.com/nodejs/node/commit/744a284ccc)] - **(SEMVER-MINOR)** **stream**: support async for stream impl functions (James M Snell) [#34416](https://github.com/nodejs/node/pull/34416)
* \[[`bfbdc84738`](https://github.com/nodejs/node/commit/bfbdc84738)] - **(SEMVER-MINOR)** **timers**: allow promisified timeouts/immediates to be canceled (James M Snell) [#33833](https://github.com/nodejs/node/pull/33833)
* \[[`a8971f87d3`](https://github.com/nodejs/node/commit/a8971f87d3)] - **(SEMVER-MINOR)** **url**: support non-special URLs (Daijiro Wachi) [#34925](https://github.com/nodejs/node/pull/34925)

### Semver-Patch Commits

* \[[`d10c59fc60`](https://github.com/nodejs/node/commit/d10c59fc60)] - **benchmark,test**: remove output from readable-async-iterator benchmark (Rich Trott) [#34411](https://github.com/nodejs/node/pull/34411)
* \[[`8a12e9994f`](https://github.com/nodejs/node/commit/8a12e9994f)] - **bootstrap**: use file URL instead of relative url (Daijiro Wachi) [#35622](https://github.com/nodejs/node/pull/35622)
* \[[`f8bde7ce06`](https://github.com/nodejs/node/commit/f8bde7ce06)] - **bootstrap**: build fast APIs in pre-execution (Joyee Cheung) [#32984](https://github.com/nodejs/node/pull/32984)
* \[[`b18651bcd2`](https://github.com/nodejs/node/commit/b18651bcd2)] - **build**: do not pass mode option to test-v8 command (Michaël Zasso) [#35705](https://github.com/nodejs/node/pull/35705)
* \[[`bb2945ed6b`](https://github.com/nodejs/node/commit/bb2945ed6b)] - **build**: add GitHub Action for code coverage (Benjamin Coe) [#35653](https://github.com/nodejs/node/pull/35653)
* \[[`cfbbeea4a1`](https://github.com/nodejs/node/commit/cfbbeea4a1)] - **build**: use GITHUB\_ENV file to set env variables (Michaël Zasso) [#35638](https://github.com/nodejs/node/pull/35638)
* \[[`8a93b371a3`](https://github.com/nodejs/node/commit/8a93b371a3)] - **build**: do not install jq in workflows (Michaël Zasso) [#35638](https://github.com/nodejs/node/pull/35638)
* \[[`ccbd1d5efa`](https://github.com/nodejs/node/commit/ccbd1d5efa)] - **build**: add quic to github action (gengjiawen) [#34336](https://github.com/nodejs/node/pull/34336)
* \[[`f4f191bbc2`](https://github.com/nodejs/node/commit/f4f191bbc2)] - **build**: define NODE\_EXPERIMENTAL\_QUIC in mkcodecache and node\_mksnapshot (Joyee Cheung) [#34454](https://github.com/nodejs/node/pull/34454)
* \[[`5b2c263ba8`](https://github.com/nodejs/node/commit/5b2c263ba8)] - **deps**: fix typo in zlib.gyp that break arm-fpu-neon build (lucasg) [#35659](https://github.com/nodejs/node/pull/35659)
* \[[`5b9593f727`](https://github.com/nodejs/node/commit/5b9593f727)] - **deps**: upgrade npm to 7.0.2 (Myles Borins) [#35667](https://github.com/nodejs/node/pull/35667)
* \[[`dabc6ddddc`](https://github.com/nodejs/node/commit/dabc6ddddc)] - **deps**: upgrade npm to 7.0.0-rc.4 (Myles Borins) [#35576](https://github.com/nodejs/node/pull/35576)
* \[[`757bac6711`](https://github.com/nodejs/node/commit/757bac6711)] - **deps**: update nghttp3 (James M Snell) [#34752](https://github.com/nodejs/node/pull/34752)
* \[[`c788be2e6e`](https://github.com/nodejs/node/commit/c788be2e6e)] - **deps**: update ngtcp2 (James M Snell) [#34752](https://github.com/nodejs/node/pull/34752)
* \[[`7816e5f7b9`](https://github.com/nodejs/node/commit/7816e5f7b9)] - **deps**: fix indenting of sources in ngtcp2.gyp (James M Snell) [#34033](https://github.com/nodejs/node/pull/34033)
* \[[`f5343d1b40`](https://github.com/nodejs/node/commit/f5343d1b40)] - **deps**: re-enable OPENSSL\_NO\_QUIC guards (James M Snell) [#34033](https://github.com/nodejs/node/pull/34033)
* \[[`9de95f494e`](https://github.com/nodejs/node/commit/9de95f494e)] - **deps**: temporary fixup for ngtcp2 to build on windows (James M Snell) [#34033](https://github.com/nodejs/node/pull/34033)
* \[[`ec7ad1d0ec`](https://github.com/nodejs/node/commit/ec7ad1d0ec)] - **deps**: cherry-pick akamai/openssl/commit/bf4b08ecfbb7a26ca4b0b9ecaee3b31d18d7bda9 (Tatsuhiro Tsujikawa) [#34033](https://github.com/nodejs/node/pull/34033)
* \[[`c3d85b7637`](https://github.com/nodejs/node/commit/c3d85b7637)] - **deps**: cherry-pick akamai/openssl/commit/a5a08cb8050bb69120e833456e355f482e392456 (Benjamin Kaduk) [#34033](https://github.com/nodejs/node/pull/34033)
* \[[`bad1a150ea`](https://github.com/nodejs/node/commit/bad1a150ea)] - **deps**: cherry-pick akamai/openssl/commit/d5a13ca6e29f3ff85c731770ab0ee2f2487bf8b3 (Benjamin Kaduk) [#34033](https://github.com/nodejs/node/pull/34033)
* \[[`74cbfd3f36`](https://github.com/nodejs/node/commit/74cbfd3f36)] - **deps**: cherry-pick akamai/openssl/commit/a6282c566d88db11300c82abc3c84a4e2e9ea568 (Benjamin Kaduk) [#34033](https://github.com/nodejs/node/pull/34033)
* \[[`8a9763a8ea`](https://github.com/nodejs/node/commit/8a9763a8ea)] - **deps**: update nghttp3 (James M Snell) [#34033](https://github.com/nodejs/node/pull/34033)
* \[[`6b27d07779`](https://github.com/nodejs/node/commit/6b27d07779)] - **deps**: update ngtcp2 (James M Snell) [#34033](https://github.com/nodejs/node/pull/34033)
* \[[`a041723774`](https://github.com/nodejs/node/commit/a041723774)] - **deps**: fix indentation for sources in nghttp3.gyp (Daniel Bevenius) [#33942](https://github.com/nodejs/node/pull/33942)
* \[[`a0cbd676e7`](https://github.com/nodejs/node/commit/a0cbd676e7)] - **deps**: add defines to nghttp3/ngtcp2 gyp configs (Daniel Bevenius) [#33942](https://github.com/nodejs/node/pull/33942)
* \[[`bccb514936`](https://github.com/nodejs/node/commit/bccb514936)] - **deps**: maintaining ngtcp2 and nghttp3 (James M Snell) [#32379](https://github.com/nodejs/node/pull/32379)
* \[[`834fa8f23f`](https://github.com/nodejs/node/commit/834fa8f23f)] - **deps**: add ngtcp2 and nghttp3 (James M Snell) [#32379](https://github.com/nodejs/node/pull/32379)
* \[[`f96b981528`](https://github.com/nodejs/node/commit/f96b981528)] - **deps**: details for updating openssl quic support (James M Snell) [#32379](https://github.com/nodejs/node/pull/32379)
* \[[`98c8498552`](https://github.com/nodejs/node/commit/98c8498552)] - **deps**: update archs files for OpenSSL-1.1.0 (James M Snell) [#32379](https://github.com/nodejs/node/pull/32379)
* \[[`2c549e505e`](https://github.com/nodejs/node/commit/2c549e505e)] - **deps**: add support for BoringSSL QUIC APIs (Todd Short) [#32379](https://github.com/nodejs/node/pull/32379)
* \[[`1103b15af6`](https://github.com/nodejs/node/commit/1103b15af6)] - **doc**: fix YAML lint error on master (Rich Trott) [#35709](https://github.com/nodejs/node/pull/35709)
* \[[`7798e59e98`](https://github.com/nodejs/node/commit/7798e59e98)] - **doc**: upgrade stability status of report API (Gireesh Punathil) [#35654](https://github.com/nodejs/node/pull/35654)
* \[[`ce03a182cf`](https://github.com/nodejs/node/commit/ce03a182cf)] - **doc**: clarify experimental API elements in vm.md (Rich Trott) [#35594](https://github.com/nodejs/node/pull/35594)
* \[[`89defff3b9`](https://github.com/nodejs/node/commit/89defff3b9)] - **doc**: correct order of metadata for deprecation (Rich Trott) [#35668](https://github.com/nodejs/node/pull/35668)
* \[[`ee85eb9f8a`](https://github.com/nodejs/node/commit/ee85eb9f8a)] - **doc**: importModuleDynamically gets Script, not Module (Simen Bekkhus) [#35593](https://github.com/nodejs/node/pull/35593)
* \[[`9e5a27a9d3`](https://github.com/nodejs/node/commit/9e5a27a9d3)] - **doc**: fix EventEmitter examples (Sourav Shaw) [#33513](https://github.com/nodejs/node/pull/33513)
* \[[`2c2c87e291`](https://github.com/nodejs/node/commit/2c2c87e291)] - **doc**: fix stability indicator in webcrypto doc (Rich Trott) [#35672](https://github.com/nodejs/node/pull/35672)
* \[[`f59d4e05a2`](https://github.com/nodejs/node/commit/f59d4e05a2)] - **doc**: add example code for process.getgroups() (Pooja D.P) [#35625](https://github.com/nodejs/node/pull/35625)
* \[[`8a3808dc37`](https://github.com/nodejs/node/commit/8a3808dc37)] - **doc**: use kbd element in tty doc (Rich Trott) [#35613](https://github.com/nodejs/node/pull/35613)
* \[[`4079bfd462`](https://github.com/nodejs/node/commit/4079bfd462)] - **doc**: Remove reference to io.js (Hussaina Begum Nandyala) [#35618](https://github.com/nodejs/node/pull/35618)
* \[[`e6d5af3c95`](https://github.com/nodejs/node/commit/e6d5af3c95)] - **doc**: fix typos in quic.md (Luigi Pinca) [#35444](https://github.com/nodejs/node/pull/35444)
* \[[`524123fbf0`](https://github.com/nodejs/node/commit/524123fbf0)] - **doc**: update releaser in v12.18.4 changelog (Beth Griggs) [#35217](https://github.com/nodejs/node/pull/35217)
* \[[`ccdd1bd82a`](https://github.com/nodejs/node/commit/ccdd1bd82a)] - **doc**: fix incorrectly marked Buffer in quic.md (Rich Trott) [#35075](https://github.com/nodejs/node/pull/35075)
* \[[`cc754f2985`](https://github.com/nodejs/node/commit/cc754f2985)] - **doc**: make AbortSignal text consistent in events.md (Rich Trott) [#35005](https://github.com/nodejs/node/pull/35005)
* \[[`f9c362ff6c`](https://github.com/nodejs/node/commit/f9c362ff6c)] - **doc**: revise AbortSignal text and example using events.once() (Rich Trott) [#35005](https://github.com/nodejs/node/pull/35005)
* \[[`7aeff6b8c8`](https://github.com/nodejs/node/commit/7aeff6b8c8)] - **doc**: claim ABI version for Electron v12 (Shelley Vohr) [#34816](https://github.com/nodejs/node/pull/34816)
* \[[`7a1220a1d7`](https://github.com/nodejs/node/commit/7a1220a1d7)] - **doc**: fix headings in quic.md (Anna Henningsen) [#34717](https://github.com/nodejs/node/pull/34717)
* \[[`d5c7aec3cb`](https://github.com/nodejs/node/commit/d5c7aec3cb)] - **doc**: use \_can\_ to describe actions in quic.md (Rich Trott) [#34613](https://github.com/nodejs/node/pull/34613)
* \[[`319c275b26`](https://github.com/nodejs/node/commit/319c275b26)] - **doc**: use \_can\_ to describe actions in quic.md (Rich Trott) [#34613](https://github.com/nodejs/node/pull/34613)
* \[[`2c30920886`](https://github.com/nodejs/node/commit/2c30920886)] - **doc**: use sentence-case in quic.md headers (Rich Trott) [#34453](https://github.com/nodejs/node/pull/34453)
* \[[`8ada27510d`](https://github.com/nodejs/node/commit/8ada27510d)] - **doc**: add missing backticks in timers.md (vsemozhetbyt) [#34030](https://github.com/nodejs/node/pull/34030)
* \[[`862d005e60`](https://github.com/nodejs/node/commit/862d005e60)] - **doc**: make globals Extends usage consistent (Colin Ihrig) [#33777](https://github.com/nodejs/node/pull/33777)
* \[[`85dbd17bde`](https://github.com/nodejs/node/commit/85dbd17bde)] - **doc**: make perf\_hooks Extends usage consistent (Colin Ihrig) [#33777](https://github.com/nodejs/node/pull/33777)
* \[[`2e49010bc8`](https://github.com/nodejs/node/commit/2e49010bc8)] - **doc**: make events Extends usage consistent (Colin Ihrig) [#33777](https://github.com/nodejs/node/pull/33777)
* \[[`680fb8fc62`](https://github.com/nodejs/node/commit/680fb8fc62)] - **doc**: fix deprecation "End-of-Life" capitalization (Colin Ihrig) [#33691](https://github.com/nodejs/node/pull/33691)
* \[[`458677f5ef`](https://github.com/nodejs/node/commit/458677f5ef)] - **errors**: print original exception context (Benjamin Coe) [#33491](https://github.com/nodejs/node/pull/33491)
* \[[`b1831fed3a`](https://github.com/nodejs/node/commit/b1831fed3a)] - **events**: simplify event target agnostic logic in on and once (Denys Otrishko) [#34997](https://github.com/nodejs/node/pull/34997)
* \[[`7f25fe8b67`](https://github.com/nodejs/node/commit/7f25fe8b67)] - **fs**: remove unused assignment (Rich Trott) [#35642](https://github.com/nodejs/node/pull/35642)
* \[[`2c4f30deea`](https://github.com/nodejs/node/commit/2c4f30deea)] - **fs**: fix when path is buffer on fs.symlinkSync (himself65) [#34540](https://github.com/nodejs/node/pull/34540)
* \[[`db0e991d52`](https://github.com/nodejs/node/commit/db0e991d52)] - **fs**: remove custom Buffer pool for streams (Robert Nagy) [#33981](https://github.com/nodejs/node/pull/33981)
* \[[`51a2df4439`](https://github.com/nodejs/node/commit/51a2df4439)] - **fs**: document why isPerformingIO is required (Robert Nagy) [#33982](https://github.com/nodejs/node/pull/33982)
* \[[`999e7d7b44`](https://github.com/nodejs/node/commit/999e7d7b44)] - **gyp,build**: consistent shared library location (Rod Vagg) [#35635](https://github.com/nodejs/node/pull/35635)
* \[[`30cc54275d`](https://github.com/nodejs/node/commit/30cc54275d)] - **http**: don't emit error after close (Robert Nagy) [#33654](https://github.com/nodejs/node/pull/33654)
* \[[`ddff2b2b22`](https://github.com/nodejs/node/commit/ddff2b2b22)] - **lib**: honor setUncaughtExceptionCaptureCallback (Gireesh Punathil) [#35595](https://github.com/nodejs/node/pull/35595)
* \[[`a8806535d9`](https://github.com/nodejs/node/commit/a8806535d9)] - **lib**: use Object static properties from primordials (Michaël Zasso) [#35380](https://github.com/nodejs/node/pull/35380)
* \[[`11f1ad939f`](https://github.com/nodejs/node/commit/11f1ad939f)] - **module**: only try to enrich CJS syntax errors (Michaël Zasso) [#35691](https://github.com/nodejs/node/pull/35691)
* \[[`aaf225a2a0`](https://github.com/nodejs/node/commit/aaf225a2a0)] - **module**: add setter for module.parent (Antoine du Hamel) [#35522](https://github.com/nodejs/node/pull/35522)
* \[[`109a296e2a`](https://github.com/nodejs/node/commit/109a296e2a)] - **quic**: fix typo in code comment (Ikko Ashimine) [#35308](https://github.com/nodejs/node/pull/35308)
* \[[`186230527b`](https://github.com/nodejs/node/commit/186230527b)] - **quic**: fix error message on invalid connection ID (Rich Trott) [#35026](https://github.com/nodejs/node/pull/35026)
* \[[`e5116b304f`](https://github.com/nodejs/node/commit/e5116b304f)] - **quic**: remove unused function arguments (Rich Trott) [#35010](https://github.com/nodejs/node/pull/35010)
* \[[`449f73e05f`](https://github.com/nodejs/node/commit/449f73e05f)] - **quic**: remove undefined variable (Rich Trott) [#35007](https://github.com/nodejs/node/pull/35007)
* \[[`44e6a6af67`](https://github.com/nodejs/node/commit/44e6a6af67)] - **quic**: use qlog fin flag (James M Snell) [#34752](https://github.com/nodejs/node/pull/34752)
* \[[`2a80737278`](https://github.com/nodejs/node/commit/2a80737278)] - **quic**: fixups after ngtcp2/nghttp3 update (James M Snell) [#34752](https://github.com/nodejs/node/pull/34752)
* \[[`c855c3e8ca`](https://github.com/nodejs/node/commit/c855c3e8ca)] - **quic**: use net.BlockList for limiting access to a QuicSocket (James M Snell) [#34741](https://github.com/nodejs/node/pull/34741)
* \[[`bfc35354c1`](https://github.com/nodejs/node/commit/bfc35354c1)] - **quic**: consolidate stats collecting in QuicSession (James M Snell) [#34741](https://github.com/nodejs/node/pull/34741)
* \[[`94aa291348`](https://github.com/nodejs/node/commit/94aa291348)] - **quic**: clarify TODO statements (James M Snell) [#34741](https://github.com/nodejs/node/pull/34741)
* \[[`19e712b9b2`](https://github.com/nodejs/node/commit/19e712b9b2)] - **quic**: resolve InitializeSecureContext TODO comment (James M Snell) [#34741](https://github.com/nodejs/node/pull/34741)
* \[[`240592228b`](https://github.com/nodejs/node/commit/240592228b)] - **quic**: fixup session ticket app data todo comments (James M Snell) [#34741](https://github.com/nodejs/node/pull/34741)
* \[[`c17eaa3f3f`](https://github.com/nodejs/node/commit/c17eaa3f3f)] - **quic**: add natRebinding argument to docs (James M Snell) [#34669](https://github.com/nodejs/node/pull/34669)
* \[[`442968c92a`](https://github.com/nodejs/node/commit/442968c92a)] - **quic**: check setSocket natRebinding argument, extend test (James M Snell) [#34669](https://github.com/nodejs/node/pull/34669)
* \[[`10d5047a4f`](https://github.com/nodejs/node/commit/10d5047a4f)] - **quic**: fixup set\_socket, fix skipped test (James M Snell) [#34669](https://github.com/nodejs/node/pull/34669)
* \[[`344c5e4e50`](https://github.com/nodejs/node/commit/344c5e4e50)] - **quic**: limit push check to http/3 (James M Snell) [#34655](https://github.com/nodejs/node/pull/34655)
* \[[`34165f03aa`](https://github.com/nodejs/node/commit/34165f03aa)] - **quic**: resolve some minor TODOs (James M Snell) [#34655](https://github.com/nodejs/node/pull/34655)
* \[[`1e6e5c3ef3`](https://github.com/nodejs/node/commit/1e6e5c3ef3)] - **quic**: resolve minor TODO in QuicSocket (James M Snell) [#34655](https://github.com/nodejs/node/pull/34655)
* \[[`ba5c64bf45`](https://github.com/nodejs/node/commit/ba5c64bf45)] - **quic**: use AbortController with correct name/message (Anna Henningsen) [#34763](https://github.com/nodejs/node/pull/34763)
* \[[`a7477704c4`](https://github.com/nodejs/node/commit/a7477704c4)] - **quic**: prefer modernize-make-unique (gengjiawen) [#34692](https://github.com/nodejs/node/pull/34692)
* \[[`5b6cd6fa1a`](https://github.com/nodejs/node/commit/5b6cd6fa1a)] - **quic**: use the SocketAddressLRU to track validation status (James M Snell) [#34618](https://github.com/nodejs/node/pull/34618)
* \[[`f75e69a94b`](https://github.com/nodejs/node/commit/f75e69a94b)] - **quic**: use SocketAddressLRU to track known SocketAddress info (James M Snell) [#34618](https://github.com/nodejs/node/pull/34618)
* \[[`6b0b33cd4c`](https://github.com/nodejs/node/commit/6b0b33cd4c)] - **quic**: cleanup some outstanding todo items (James M Snell) [#34618](https://github.com/nodejs/node/pull/34618)
* \[[`6e65f26b73`](https://github.com/nodejs/node/commit/6e65f26b73)] - **quic**: use QuicCallbackScope consistently for QuicSession (James M Snell) [#34541](https://github.com/nodejs/node/pull/34541)
* \[[`d96083bad5`](https://github.com/nodejs/node/commit/d96083bad5)] - **quic**: introduce QuicCallbackScope (James M Snell) [#34541](https://github.com/nodejs/node/pull/34541)
* \[[`4b0275ab87`](https://github.com/nodejs/node/commit/4b0275ab87)] - **quic**: refactor clientHello (James M Snell) [#34541](https://github.com/nodejs/node/pull/34541)
* \[[`a97b5f9c6a`](https://github.com/nodejs/node/commit/a97b5f9c6a)] - **quic**: use OpenSSL built-in cert and hostname validation (James M Snell) [#34533](https://github.com/nodejs/node/pull/34533)
* \[[`7a5fbafe96`](https://github.com/nodejs/node/commit/7a5fbafe96)] - **quic**: fix build for macOS (gengjiawen) [#34336](https://github.com/nodejs/node/pull/34336)
* \[[`1f94b89309`](https://github.com/nodejs/node/commit/1f94b89309)] - **quic**: refactor ocsp to use async function rather than event/callback (James M Snell) [#34498](https://github.com/nodejs/node/pull/34498)
* \[[`06664298fa`](https://github.com/nodejs/node/commit/06664298fa)] - **quic**: remove no-longer relevant TODO statements (James M Snell) [#34498](https://github.com/nodejs/node/pull/34498)
* \[[`2fb92f4cc6`](https://github.com/nodejs/node/commit/2fb92f4cc6)] - **quic**: remove extraneous unused debug property (James M Snell) [#34498](https://github.com/nodejs/node/pull/34498)
* \[[`b06fe33de1`](https://github.com/nodejs/node/commit/b06fe33de1)] - **quic**: use async \_construct for QuicStream (James M Snell) [#34351](https://github.com/nodejs/node/pull/34351)
* \[[`8bd61d4c38`](https://github.com/nodejs/node/commit/8bd61d4c38)] - **quic**: documentation updates (James M Snell) [#34351](https://github.com/nodejs/node/pull/34351)
* \[[`086c916997`](https://github.com/nodejs/node/commit/086c916997)] - **quic**: extensive refactoring of QuicStream lifecycle (James M Snell) [#34351](https://github.com/nodejs/node/pull/34351)
* \[[`cf28f8a7dd`](https://github.com/nodejs/node/commit/cf28f8a7dd)] - **quic**: gitignore qlog files (James M Snell) [#34351](https://github.com/nodejs/node/pull/34351)
* \[[`83bf0d7e8c`](https://github.com/nodejs/node/commit/83bf0d7e8c)] - **quic**: remove unneeded quicstream.aborted and fixup docs (James M Snell) [#34351](https://github.com/nodejs/node/pull/34351)
* \[[`a65296db2c`](https://github.com/nodejs/node/commit/a65296db2c)] - **quic**: remove stream pending code (James M Snell) [#34351](https://github.com/nodejs/node/pull/34351)
* \[[`da20287e1a`](https://github.com/nodejs/node/commit/da20287e1a)] - **quic**: simplify QuicStream construction logic (James M Snell) [#34351](https://github.com/nodejs/node/pull/34351)
* \[[`6e30fe7a7f`](https://github.com/nodejs/node/commit/6e30fe7a7f)] - **quic**: convert openStream to Promise (James M Snell) [#34351](https://github.com/nodejs/node/pull/34351)
* \[[`89453cfc08`](https://github.com/nodejs/node/commit/89453cfc08)] - **quic**: fixup quic.md (James M Snell) [#34283](https://github.com/nodejs/node/pull/34283)
* \[[`4523d4a813`](https://github.com/nodejs/node/commit/4523d4a813)] - **quic**: fixup closing/draining period timing (James M Snell) [#34283](https://github.com/nodejs/node/pull/34283)
* \[[`ed4882241c`](https://github.com/nodejs/node/commit/ed4882241c)] - **quic**: properly pass readable/writable constructor options (James M Snell) [#34283](https://github.com/nodejs/node/pull/34283)
* \[[`57c1129508`](https://github.com/nodejs/node/commit/57c1129508)] - **quic**: implement QuicSession close as promise (James M Snell) [#34283](https://github.com/nodejs/node/pull/34283)
* \[[`8e5c5b16ab`](https://github.com/nodejs/node/commit/8e5c5b16ab)] - **quic**: cleanup QuicClientSession constructor (James M Snell) [#34283](https://github.com/nodejs/node/pull/34283)
* \[[`fe4e7e4598`](https://github.com/nodejs/node/commit/fe4e7e4598)] - **quic**: use promisified dns lookup (James M Snell) [#34283](https://github.com/nodejs/node/pull/34283)
* \[[`346aeaf874`](https://github.com/nodejs/node/commit/346aeaf874)] - **quic**: eliminate "ready"/"not ready" states for QuicSession (James M Snell) [#34283](https://github.com/nodejs/node/pull/34283)
* \[[`6665dda9f6`](https://github.com/nodejs/node/commit/6665dda9f6)] - **quic**: implement QuicSocket Promise API, part 2 (James M Snell) [#34283](https://github.com/nodejs/node/pull/34283)
* \[[`79c0e892dd`](https://github.com/nodejs/node/commit/79c0e892dd)] - **quic**: implement QuicSocket Promise API, part 1 (James M Snell) [#34283](https://github.com/nodejs/node/pull/34283)
* \[[`53b12f0c7b`](https://github.com/nodejs/node/commit/53b12f0c7b)] - **quic**: implement QuicEndpoint Promise API (James M Snell) [#34283](https://github.com/nodejs/node/pull/34283)
* \[[`16b32eae3e`](https://github.com/nodejs/node/commit/16b32eae3e)] - **quic**: handle unhandled rejections on QuicSession (James M Snell) [#34283](https://github.com/nodejs/node/pull/34283)
* \[[`e5d963e24d`](https://github.com/nodejs/node/commit/e5d963e24d)] - **quic**: fixup kEndpointClose (James M Snell) [#34283](https://github.com/nodejs/node/pull/34283)
* \[[`9f552df5b4`](https://github.com/nodejs/node/commit/9f552df5b4)] - **quic**: fix endpointClose error handling, document (James M Snell) [#34283](https://github.com/nodejs/node/pull/34283)
* \[[`b80108c033`](https://github.com/nodejs/node/commit/b80108c033)] - **quic**: restrict addEndpoint to before QuicSocket bind (James M Snell) [#34283](https://github.com/nodejs/node/pull/34283)
* \[[`81c01bbdba`](https://github.com/nodejs/node/commit/81c01bbdba)] - **quic**: use a getter for stream options (James M Snell) [#34283](https://github.com/nodejs/node/pull/34283)
* \[[`b8945ba2ab`](https://github.com/nodejs/node/commit/b8945ba2ab)] - **quic**: clarifying code comments (James M Snell) [#34283](https://github.com/nodejs/node/pull/34283)
* \[[`429ab1dce6`](https://github.com/nodejs/node/commit/429ab1dce6)] - **quic**: minor reduction in code duplication (James M Snell) [#34283](https://github.com/nodejs/node/pull/34283)
* \[[`aafdc2fcad`](https://github.com/nodejs/node/commit/aafdc2fcad)] - **quic**: replace ipv6Only option with `'udp6-only'` type (James M Snell) [#34283](https://github.com/nodejs/node/pull/34283)
* \[[`fbc38ee134`](https://github.com/nodejs/node/commit/fbc38ee134)] - **quic**: clear clang warning (gengjiawen) [#34335](https://github.com/nodejs/node/pull/34335)
* \[[`c176d5fac2`](https://github.com/nodejs/node/commit/c176d5fac2)] - **quic**: set destroyed at timestamps for duration calculation (James M Snell) [#34262](https://github.com/nodejs/node/pull/34262)
* \[[`48a349efd9`](https://github.com/nodejs/node/commit/48a349efd9)] - **quic**: use Number instead of BigInt for more stats (James M Snell) [#34262](https://github.com/nodejs/node/pull/34262)
* \[[`5e769b2eaf`](https://github.com/nodejs/node/commit/5e769b2eaf)] - **quic**: use less specific error codes (James M Snell) [#34262](https://github.com/nodejs/node/pull/34262)
* \[[`26493c02a2`](https://github.com/nodejs/node/commit/26493c02a2)] - **quic**: remove no longer valid CHECK (James M Snell) [#34247](https://github.com/nodejs/node/pull/34247)
* \[[`458d243f20`](https://github.com/nodejs/node/commit/458d243f20)] - **quic**: proper custom inspect for QuicStream (James M Snell) [#34247](https://github.com/nodejs/node/pull/34247)
* \[[`0860b11655`](https://github.com/nodejs/node/commit/0860b11655)] - **quic**: proper custom inspect for QuicSession (James M Snell) [#34247](https://github.com/nodejs/node/pull/34247)
* \[[`b047930d76`](https://github.com/nodejs/node/commit/b047930d76)] - **quic**: proper custom inspect for QuicSocket (James M Snell) [#34247](https://github.com/nodejs/node/pull/34247)
* \[[`511f8c1138`](https://github.com/nodejs/node/commit/511f8c1138)] - **quic**: proper custom inspect for QuicEndpoint (James M Snell) [#34247](https://github.com/nodejs/node/pull/34247)
* \[[`fe11f6bf7c`](https://github.com/nodejs/node/commit/fe11f6bf7c)] - **quic**: cleanup QuicSocketFlags, used shared state struct (James M Snell) [#34247](https://github.com/nodejs/node/pull/34247)
* \[[`d08e99de24`](https://github.com/nodejs/node/commit/d08e99de24)] - **quic**: use getter/setter for stateless reset toggle (James M Snell) [#34247](https://github.com/nodejs/node/pull/34247)
* \[[`f2753c7695`](https://github.com/nodejs/node/commit/f2753c7695)] - **quic**: unref timers again (Anna Henningsen) [#34247](https://github.com/nodejs/node/pull/34247)
* \[[`71236097d0`](https://github.com/nodejs/node/commit/71236097d0)] - **quic**: use Number() instead of bigint for QuicSocket stats (James M Snell) [#34247](https://github.com/nodejs/node/pull/34247)
* \[[`94372b124a`](https://github.com/nodejs/node/commit/94372b124a)] - **quic**: refactor/improve/document QuicSocket listening event (James M Snell) [#34247](https://github.com/nodejs/node/pull/34247)
* \[[`afc9390ae5`](https://github.com/nodejs/node/commit/afc9390ae5)] - **quic**: refactor/improve QuicSocket ready event handling (James M Snell) [#34247](https://github.com/nodejs/node/pull/34247)
* \[[`e3813261b8`](https://github.com/nodejs/node/commit/e3813261b8)] - **quic**: add tests confirming error handling for QuicSocket close event (James M Snell) [#34247](https://github.com/nodejs/node/pull/34247)
* \[[`cc89aac5f7`](https://github.com/nodejs/node/commit/cc89aac5f7)] - **quic**: refactor/improve error handling for busy event (James M Snell) [#34247](https://github.com/nodejs/node/pull/34247)
* \[[`edc71ef008`](https://github.com/nodejs/node/commit/edc71ef008)] - **quic**: handle errors thrown / rejections in the session event (James M Snell) [#34247](https://github.com/nodejs/node/pull/34247)
* \[[`bcde849be9`](https://github.com/nodejs/node/commit/bcde849be9)] - **quic**: remove unnecessary bool conversion (James M Snell) [#34247](https://github.com/nodejs/node/pull/34247)
* \[[`c535131627`](https://github.com/nodejs/node/commit/c535131627)] - **quic**: additional minor cleanups in node\_quic\_session.h (James M Snell) [#34247](https://github.com/nodejs/node/pull/34247)
* \[[`0f97d6066a`](https://github.com/nodejs/node/commit/0f97d6066a)] - **quic**: use TimerWrap for idle and retransmit timers (James M Snell) [#34186](https://github.com/nodejs/node/pull/34186)
* \[[`1b1e985478`](https://github.com/nodejs/node/commit/1b1e985478)] - **quic**: add missing memory tracker fields (James M Snell) [#34160](https://github.com/nodejs/node/pull/34160)
* \[[`5a87e9b0a5`](https://github.com/nodejs/node/commit/5a87e9b0a5)] - **quic**: cleanup timers if they haven't been already (James M Snell) [#34160](https://github.com/nodejs/node/pull/34160)
* \[[`3837d9cf1f`](https://github.com/nodejs/node/commit/3837d9cf1f)] - **quic**: fixup lint issues (James M Snell) [#34160](https://github.com/nodejs/node/pull/34160)
* \[[`7b062ca015`](https://github.com/nodejs/node/commit/7b062ca015)] - **quic**: refactor qlog handling (James M Snell) [#34160](https://github.com/nodejs/node/pull/34160)
* \[[`e4d369e96e`](https://github.com/nodejs/node/commit/e4d369e96e)] - **quic**: remove onSessionDestroy callback (James M Snell) [#34160](https://github.com/nodejs/node/pull/34160)
* \[[`3acdd6aac7`](https://github.com/nodejs/node/commit/3acdd6aac7)] - **quic**: refactor QuicSession shared state to use AliasedStruct (James M Snell) [#34160](https://github.com/nodejs/node/pull/34160)
* \[[`f9c2245fb5`](https://github.com/nodejs/node/commit/f9c2245fb5)] - **quic**: refactor QuicSession close/destroy flow (James M Snell) [#34160](https://github.com/nodejs/node/pull/34160)
* \[[`f7510ca439`](https://github.com/nodejs/node/commit/f7510ca439)] - **quic**: additional cleanups on the c++ side (James M Snell) [#34160](https://github.com/nodejs/node/pull/34160)
* \[[`b5bf5bb20f`](https://github.com/nodejs/node/commit/b5bf5bb20f)] - **quic**: refactor native object flags for better readability (James M Snell) [#34160](https://github.com/nodejs/node/pull/34160)
* \[[`b1750a4d53`](https://github.com/nodejs/node/commit/b1750a4d53)] - **quic**: continued refactoring for quic\_stream/quic\_session (James M Snell) [#34160](https://github.com/nodejs/node/pull/34160)
* \[[`31d6d9d0f7`](https://github.com/nodejs/node/commit/31d6d9d0f7)] - **quic**: reduce duplication of code (James M Snell) [#34137](https://github.com/nodejs/node/pull/34137)
* \[[`b5fe31ef19`](https://github.com/nodejs/node/commit/b5fe31ef19)] - **quic**: avoid using private JS fields for now (James M Snell) [#34137](https://github.com/nodejs/node/pull/34137)
* \[[`2afc1abd05`](https://github.com/nodejs/node/commit/2afc1abd05)] - **quic**: fixup constant exports, export all protocol error codes (James M Snell) [#34137](https://github.com/nodejs/node/pull/34137)
* \[[`b1fab88ff0`](https://github.com/nodejs/node/commit/b1fab88ff0)] - **quic**: remove unused callback function (James M Snell) [#34137](https://github.com/nodejs/node/pull/34137)
* \[[`3bae2d5073`](https://github.com/nodejs/node/commit/3bae2d5073)] - **quic**: consolidate onSessionClose and onSessionSilentClose (James M Snell) [#34137](https://github.com/nodejs/node/pull/34137)
* \[[`def8e76999`](https://github.com/nodejs/node/commit/def8e76999)] - **quic**: fixup set\_final\_size (James M Snell) [#34137](https://github.com/nodejs/node/pull/34137)
* \[[`d6034186d6`](https://github.com/nodejs/node/commit/d6034186d6)] - **quic**: cleanups for QuicSocket (James M Snell) [#34137](https://github.com/nodejs/node/pull/34137)
* \[[`73a51bb9dc`](https://github.com/nodejs/node/commit/73a51bb9dc)] - **quic**: cleanups in JS API (James M Snell) [#34137](https://github.com/nodejs/node/pull/34137)
* \[[`204f20f2d1`](https://github.com/nodejs/node/commit/204f20f2d1)] - **quic**: minor cleanups in quic\_buffer (James M Snell) [#34087](https://github.com/nodejs/node/pull/34087)
* \[[`68634d2592`](https://github.com/nodejs/node/commit/68634d2592)] - **quic**: remove redundant cast (gengjiawen) [#34086](https://github.com/nodejs/node/pull/34086)
* \[[`213cac0b94`](https://github.com/nodejs/node/commit/213cac0b94)] - **quic**: temporarily skip quic-ipv6only test (James M Snell) [#34033](https://github.com/nodejs/node/pull/34033)
* \[[`99f7c4bb5e`](https://github.com/nodejs/node/commit/99f7c4bb5e)] - **quic**: possibly resolve flaky assertion failure in ipv6only test (James M Snell) [#34033](https://github.com/nodejs/node/pull/34033)
* \[[`2a5922e483`](https://github.com/nodejs/node/commit/2a5922e483)] - **quic**: temporarily disable packetloss tests (James M Snell) [#34033](https://github.com/nodejs/node/pull/34033)
* \[[`86e67aaa69`](https://github.com/nodejs/node/commit/86e67aaa69)] - **quic**: updates to implement for h3-29 (James M Snell) [#34033](https://github.com/nodejs/node/pull/34033)
* \[[`adf14e2617`](https://github.com/nodejs/node/commit/adf14e2617)] - **quic**: fix lint error in node\_quic\_crypto (Daniel Bevenius) [#34019](https://github.com/nodejs/node/pull/34019)
* \[[`9f2e00fb99`](https://github.com/nodejs/node/commit/9f2e00fb99)] - **quic**: temporarily disable preferred address tests (James M Snell) [#33934](https://github.com/nodejs/node/pull/33934)
* \[[`0e7c8bdc0c`](https://github.com/nodejs/node/commit/0e7c8bdc0c)] - **quic**: return 0 from SSL\_CTX\_sess\_set\_new\_cb callback (Anna Henningsen) [#33931](https://github.com/nodejs/node/pull/33931)
* \[[`c7d859e756`](https://github.com/nodejs/node/commit/c7d859e756)] - **quic**: refactor and improve ipv6Only (James M Snell) [#33935](https://github.com/nodejs/node/pull/33935)
* \[[`1b7434dfc0`](https://github.com/nodejs/node/commit/1b7434dfc0)] - **quic**: set up FunctionTemplates more cleanly (Anna Henningsen) [#33968](https://github.com/nodejs/node/pull/33968)
* \[[`8ef86a920c`](https://github.com/nodejs/node/commit/8ef86a920c)] - **quic**: fix clang warning (gengjiawen) [#33963](https://github.com/nodejs/node/pull/33963)
* \[[`013cd1ac6f`](https://github.com/nodejs/node/commit/013cd1ac6f)] - **quic**: use Check instead of FromJust in node\_quic.cc (Daniel Bevenius) [#33937](https://github.com/nodejs/node/pull/33937)
* \[[`09330fc155`](https://github.com/nodejs/node/commit/09330fc155)] - **quic**: fix clang-tidy performance-faster-string-find issue (gengjiawen) [#33975](https://github.com/nodejs/node/pull/33975)
* \[[`9743624c0b`](https://github.com/nodejs/node/commit/9743624c0b)] - **quic**: fix typo in comments (gengjiawen) [#33975](https://github.com/nodejs/node/pull/33975)
* \[[`88ef15812c`](https://github.com/nodejs/node/commit/88ef15812c)] - **quic**: remove unused string include http3\_application (Daniel Bevenius) [#33926](https://github.com/nodejs/node/pull/33926)
* \[[`1bd88a3ac6`](https://github.com/nodejs/node/commit/1bd88a3ac6)] - **quic**: fix up node\_quic\_stream includes (Daniel Bevenius) [#33921](https://github.com/nodejs/node/pull/33921)
* \[[`d7d79f2163`](https://github.com/nodejs/node/commit/d7d79f2163)] - **quic**: avoid memory fragmentation issue (James M Snell) [#33912](https://github.com/nodejs/node/pull/33912)
* \[[`16116f5f5f`](https://github.com/nodejs/node/commit/16116f5f5f)] - **quic**: remove noop code (Robert Nagy) [#33914](https://github.com/nodejs/node/pull/33914)
* \[[`272b46e04d`](https://github.com/nodejs/node/commit/272b46e04d)] - **quic**: skip test-quic-preferred-address-ipv6.js when no ipv6 (James M Snell) [#33919](https://github.com/nodejs/node/pull/33919)
* \[[`4b70f95d64`](https://github.com/nodejs/node/commit/4b70f95d64)] - **quic**: use Check instead of FromJust in QuicStream (Daniel Bevenius) [#33909](https://github.com/nodejs/node/pull/33909)
* \[[`133a97f60d`](https://github.com/nodejs/node/commit/133a97f60d)] - **quic**: always copy stateless reset token (Anna Henningsen) [#33917](https://github.com/nodejs/node/pull/33917)
* \[[`14d012ef96`](https://github.com/nodejs/node/commit/14d012ef96)] - **quic**: fix minor linting issue (James M Snell) [#33913](https://github.com/nodejs/node/pull/33913)
* \[[`55360443ce`](https://github.com/nodejs/node/commit/55360443ce)] - **quic**: initial QUIC implementation (James M Snell) [#32379](https://github.com/nodejs/node/pull/32379)
* \[[`a12a2d892f`](https://github.com/nodejs/node/commit/a12a2d892f)] - **repl**: update deprecation codes (Antoine du HAMEL) [#33430](https://github.com/nodejs/node/pull/33430)
* \[[`2b3acc44f0`](https://github.com/nodejs/node/commit/2b3acc44f0)] - **src**: large pages support in illumos/solaris systems (David Carlier) [#34320](https://github.com/nodejs/node/pull/34320)
* \[[`84a7880749`](https://github.com/nodejs/node/commit/84a7880749)] - **src**: minor cleanup and simplification of crypto::Hash (James M Snell) [#35651](https://github.com/nodejs/node/pull/35651)
* \[[`bfc906906f`](https://github.com/nodejs/node/commit/bfc906906f)] - **src**: combine TLSWrap/SSLWrap (James M Snell) [#35552](https://github.com/nodejs/node/pull/35552)
* \[[`9fd6122659`](https://github.com/nodejs/node/commit/9fd6122659)] - **src**: add embedding helpers to reduce boilerplate code (Anna Henningsen) [#35597](https://github.com/nodejs/node/pull/35597)
* \[[`f7ed5f4ae3`](https://github.com/nodejs/node/commit/f7ed5f4ae3)] - **src**: remove toLocalChecked in crypto\_context (James M Snell) [#35509](https://github.com/nodejs/node/pull/35509)
* \[[`17d5d94921`](https://github.com/nodejs/node/commit/17d5d94921)] - **src**: replace more toLocalCheckeds in crypto\_\* (James M Snell) [#35509](https://github.com/nodejs/node/pull/35509)
* \[[`83eaaf9731`](https://github.com/nodejs/node/commit/83eaaf9731)] - **src**: remove unused AsyncWrapObject (James M Snell) [#35511](https://github.com/nodejs/node/pull/35511)
* \[[`ee5f849fda`](https://github.com/nodejs/node/commit/ee5f849fda)] - **src**: fix compiler warning in env.cc (Anna Henningsen) [#35547](https://github.com/nodejs/node/pull/35547)
* \[[`40364b181d`](https://github.com/nodejs/node/commit/40364b181d)] - **src**: add check against non-weak BaseObjects at process exit (Anna Henningsen) [#35490](https://github.com/nodejs/node/pull/35490)
* \[[`bc0c094b74`](https://github.com/nodejs/node/commit/bc0c094b74)] - **src**: unset NODE\_VERSION\_IS\_RELEASE from master (Antoine du Hamel) [#35531](https://github.com/nodejs/node/pull/35531)
* \[[`fdf0a84e82`](https://github.com/nodejs/node/commit/fdf0a84e82)] - **src**: move all base64.h inline methods into -inl.h header file (Anna Henningsen) [#35432](https://github.com/nodejs/node/pull/35432)
* \[[`ff4cf817a3`](https://github.com/nodejs/node/commit/ff4cf817a3)] - **src**: create helper for reading Uint32BE (Juan José Arboleda) [#34944](https://github.com/nodejs/node/pull/34944)
* \[[`c6e1edcc28`](https://github.com/nodejs/node/commit/c6e1edcc28)] - **src**: add Update(const sockaddr\*) variant (James M Snell) [#34752](https://github.com/nodejs/node/pull/34752)
* \[[`1c14810edc`](https://github.com/nodejs/node/commit/1c14810edc)] - **src**: allow instances of net.BlockList to be created internally (James M Snell) [#34741](https://github.com/nodejs/node/pull/34741)
* \[[`6d1f0aed52`](https://github.com/nodejs/node/commit/6d1f0aed52)] - **src**: add SocketAddressLRU Utility (James M Snell) [#34618](https://github.com/nodejs/node/pull/34618)
* \[[`feb93c4e84`](https://github.com/nodejs/node/commit/feb93c4e84)] - **src**: guard against nullptr deref in TimerWrapHandle::Stop (Anna Henningsen) [#34460](https://github.com/nodejs/node/pull/34460)
* \[[`7a447bcd54`](https://github.com/nodejs/node/commit/7a447bcd54)] - **src**: snapshot node (Joyee Cheung) [#32984](https://github.com/nodejs/node/pull/32984)
* \[[`c943cb4809`](https://github.com/nodejs/node/commit/c943cb4809)] - **src**: reset zero fill toggle at pre-execution (Joyee Cheung) [#32984](https://github.com/nodejs/node/pull/32984)
* \[[`0b8ae5f2cd`](https://github.com/nodejs/node/commit/0b8ae5f2cd)] - **src**: snapshot loaders (Joyee Cheung) [#32984](https://github.com/nodejs/node/pull/32984)
* \[[`7ecb285842`](https://github.com/nodejs/node/commit/7ecb285842)] - **src**: make code cache test work with snapshots (Joyee Cheung) [#32984](https://github.com/nodejs/node/pull/32984)
* \[[`1faf6f459f`](https://github.com/nodejs/node/commit/1faf6f459f)] - **src**: snapshot Environment upon instantiation (Joyee Cheung) [#32984](https://github.com/nodejs/node/pull/32984)
* \[[`ef9964f4c1`](https://github.com/nodejs/node/commit/ef9964f4c1)] - **src**: add an ExternalReferenceRegistry class (Joyee Cheung) [#32984](https://github.com/nodejs/node/pull/32984)
* \[[`404302fff5`](https://github.com/nodejs/node/commit/404302fff5)] - **src**: split the main context initialization from Environment ctor (Joyee Cheung) [#32984](https://github.com/nodejs/node/pull/32984)
* \[[`874460a1d1`](https://github.com/nodejs/node/commit/874460a1d1)] - **src**: refactor TimerWrap lifetime management (Anna Henningsen) [#34252](https://github.com/nodejs/node/pull/34252)
* \[[`e2f9dc6e5a`](https://github.com/nodejs/node/commit/e2f9dc6e5a)] - **src**: remove user\_data from TimerWrap (Anna Henningsen) [#34252](https://github.com/nodejs/node/pull/34252)
* \[[`e19a251824`](https://github.com/nodejs/node/commit/e19a251824)] - **src**: replace InspectorTimer with TimerWrap utility (James M Snell) [#34186](https://github.com/nodejs/node/pull/34186)
* \[[`d4f69002b4`](https://github.com/nodejs/node/commit/d4f69002b4)] - **src**: add TimerWrap utility (James M Snell) [#34186](https://github.com/nodejs/node/pull/34186)
* \[[`52de4cb107`](https://github.com/nodejs/node/commit/52de4cb107)] - **src**: minor updates to FastHrtime (Anna Henningsen) [#33851](https://github.com/nodejs/node/pull/33851)
* \[[`4678e44bb2`](https://github.com/nodejs/node/commit/4678e44bb2)] - **src**: perform bounds checking on error source line (Anna Henningsen) [#33645](https://github.com/nodejs/node/pull/33645)
* \[[`7232c2a160`](https://github.com/nodejs/node/commit/7232c2a160)] - **src**: use getauxval in node\_main.cc (Daniel Bevenius) [#33693](https://github.com/nodejs/node/pull/33693)
* \[[`6be80e1893`](https://github.com/nodejs/node/commit/6be80e1893)] - **stream**: fix legacy pipe error handling (Robert Nagy) [#35257](https://github.com/nodejs/node/pull/35257)
* \[[`2b9003b165`](https://github.com/nodejs/node/commit/2b9003b165)] - **stream**: don't destroy on async iterator success (Robert Nagy) [#35122](https://github.com/nodejs/node/pull/35122)
* \[[`9c62e0e384`](https://github.com/nodejs/node/commit/9c62e0e384)] - **stream**: move to internal/streams (Matteo Collina) [#35239](https://github.com/nodejs/node/pull/35239)
* \[[`e0d3b758a0`](https://github.com/nodejs/node/commit/e0d3b758a0)] - **stream**: improve Writable.destroy performance (Robert Nagy) [#35067](https://github.com/nodejs/node/pull/35067)
* \[[`02c4869bee`](https://github.com/nodejs/node/commit/02c4869bee)] - **stream**: fix Duplex.\_construct race (Robert Nagy) [#34456](https://github.com/nodejs/node/pull/34456)
* \[[`5aeaff6499`](https://github.com/nodejs/node/commit/5aeaff6499)] - **stream**: refactor lazyLoadPromises (rickyes) [#34354](https://github.com/nodejs/node/pull/34354)
* \[[`a55b77d2d3`](https://github.com/nodejs/node/commit/a55b77d2d3)] - **stream**: finished on closed OutgoingMessage (Robert Nagy) [#34313](https://github.com/nodejs/node/pull/34313)
* \[[`e10e292c5e`](https://github.com/nodejs/node/commit/e10e292c5e)] - **stream**: remove unused \_transformState (Robert Nagy) [#33105](https://github.com/nodejs/node/pull/33105)
* \[[`f5c11a1a0a`](https://github.com/nodejs/node/commit/f5c11a1a0a)] - **stream**: don't emit finish after close (Robert Nagy) [#32933](https://github.com/nodejs/node/pull/32933)
* \[[`089d654dd8`](https://github.com/nodejs/node/commit/089d654dd8)] - **test**: fix addons/dlopen-ping-pong for npm 7.0.1 (Myles Borins) [#35667](https://github.com/nodejs/node/pull/35667)
* \[[`9ce5a03148`](https://github.com/nodejs/node/commit/9ce5a03148)] - **test**: add test for listen callback runtime binding (H Adinarayana) [#35657](https://github.com/nodejs/node/pull/35657)
* \[[`a3731309cc`](https://github.com/nodejs/node/commit/a3731309cc)] - **test**: refactor test-https-host-headers (himself65) [#32805](https://github.com/nodejs/node/pull/32805)
* \[[`30fb4a015d`](https://github.com/nodejs/node/commit/30fb4a015d)] - **test**: add common.mustSucceed (Tobias Nießen) [#35086](https://github.com/nodejs/node/pull/35086)
* \[[`c143266b55`](https://github.com/nodejs/node/commit/c143266b55)] - **test**: add a few uncovered url tests from wpt (Daijiro Wachi) [#35636](https://github.com/nodejs/node/pull/35636)
* \[[`6751b6dc3d`](https://github.com/nodejs/node/commit/6751b6dc3d)] - **test**: check for AbortController existence (James M Snell) [#35616](https://github.com/nodejs/node/pull/35616)
* \[[`9f2e19fa30`](https://github.com/nodejs/node/commit/9f2e19fa30)] - **test**: update url test for win (Daijiro Wachi) [#35622](https://github.com/nodejs/node/pull/35622)
* \[[`c88d845db3`](https://github.com/nodejs/node/commit/c88d845db3)] - **test**: update wpt status for url (Daijiro Wachi) [#35335](https://github.com/nodejs/node/pull/35335)
* \[[`589dbf1392`](https://github.com/nodejs/node/commit/589dbf1392)] - **test**: update wpt tests for url (Daijiro Wachi) [#35329](https://github.com/nodejs/node/pull/35329)
* \[[`46bef7b771`](https://github.com/nodejs/node/commit/46bef7b771)] - **test**: add Actions annotation output (Mary Marchini) [#34590](https://github.com/nodejs/node/pull/34590)
* \[[`a9c5b873ca`](https://github.com/nodejs/node/commit/a9c5b873ca)] - **test**: move buffer-as-path symlink test to its own test file (Rich Trott) [#34569](https://github.com/nodejs/node/pull/34569)
* \[[`31ba9a20bd`](https://github.com/nodejs/node/commit/31ba9a20bd)] - **test**: run test-benchmark-napi on arm (Rich Trott) [#34502](https://github.com/nodejs/node/pull/34502)
* \[[`2c4ebe0426`](https://github.com/nodejs/node/commit/2c4ebe0426)] - **test**: use `.then(common.mustCall())` for all async IIFEs (Anna Henningsen) [#34363](https://github.com/nodejs/node/pull/34363)
* \[[`772fdb0cd3`](https://github.com/nodejs/node/commit/772fdb0cd3)] - **test**: fix flaky test-fs-stream-construct (Rich Trott) [#34203](https://github.com/nodejs/node/pull/34203)
* \[[`9b8d317d99`](https://github.com/nodejs/node/commit/9b8d317d99)] - **test**: fix flaky test-http2-invalidheaderfield (Rich Trott) [#34173](https://github.com/nodejs/node/pull/34173)
* \[[`2ccf15b2bf`](https://github.com/nodejs/node/commit/2ccf15b2bf)] - **test**: ensure finish is emitted before destroy (Robert Nagy) [#33137](https://github.com/nodejs/node/pull/33137)
* \[[`27f3530da3`](https://github.com/nodejs/node/commit/27f3530da3)] - **test**: remove unnecessary eslint-disable comment (Rich Trott) [#34000](https://github.com/nodejs/node/pull/34000)
* \[[`326a79ebb9`](https://github.com/nodejs/node/commit/326a79ebb9)] - **test**: fix typo in test-quic-client-empty-preferred-address.js (gengjiawen) [#33976](https://github.com/nodejs/node/pull/33976)
* \[[`b0b268f5a2`](https://github.com/nodejs/node/commit/b0b268f5a2)] - **test**: fix flaky fs-construct test (Robert Nagy) [#33625](https://github.com/nodejs/node/pull/33625)
* \[[`cbe955c227`](https://github.com/nodejs/node/commit/cbe955c227)] - **test**: add net regression test (Robert Nagy) [#32794](https://github.com/nodejs/node/pull/32794)
* \[[`5d179cb2ec`](https://github.com/nodejs/node/commit/5d179cb2ec)] - **timers**: use AbortController with correct name/message (Anna Henningsen) [#34763](https://github.com/nodejs/node/pull/34763)
* \[[`64d22c320c`](https://github.com/nodejs/node/commit/64d22c320c)] - **timers**: fix multipleResolves in promisified timeouts/immediates (Denys Otrishko) [#33949](https://github.com/nodejs/node/pull/33949)
* \[[`fbe33aa52e`](https://github.com/nodejs/node/commit/fbe33aa52e)] - **tools**: bump remark-lint-preset-node to 1.17.1 (Rich Trott) [#35668](https://github.com/nodejs/node/pull/35668)
* \[[`35a6946193`](https://github.com/nodejs/node/commit/35a6946193)] - **tools**: update gyp-next to v0.6.2 (Michaël Zasso) [#35690](https://github.com/nodejs/node/pull/35690)
* \[[`be80faa0c8`](https://github.com/nodejs/node/commit/be80faa0c8)] - **tools**: update gyp-next to v0.6.0 (Ujjwal Sharma) [#35635](https://github.com/nodejs/node/pull/35635)
* \[[`2d83e743d9`](https://github.com/nodejs/node/commit/2d83e743d9)] - **tools**: update ESLint to 7.11.0 (Colin Ihrig) [#35578](https://github.com/nodejs/node/pull/35578)
* \[[`0eca660948`](https://github.com/nodejs/node/commit/0eca660948)] - **tools**: update ESLint to 7.7.0 (Colin Ihrig) [#34783](https://github.com/nodejs/node/pull/34783)
* \[[`77b68f9a29`](https://github.com/nodejs/node/commit/77b68f9a29)] - **tools**: add linting rule for async IIFEs (Anna Henningsen) [#34363](https://github.com/nodejs/node/pull/34363)
* \[[`f04538761f`](https://github.com/nodejs/node/commit/f04538761f)] - **tools**: enable Node.js command line flags in node\_mksnapshot (Joyee Cheung) [#32984](https://github.com/nodejs/node/pull/32984)
* \[[`b0d4eb37c7`](https://github.com/nodejs/node/commit/b0d4eb37c7)] - **tools**: update ESLint to 7.4.0 (Colin Ihrig) [#34205](https://github.com/nodejs/node/pull/34205)
* \[[`076e4ed2d1`](https://github.com/nodejs/node/commit/076e4ed2d1)] - **tools**: update ESLint from 7.2.0 to 7.3.1 (Rich Trott) [#34000](https://github.com/nodejs/node/pull/34000)
* \[[`7afe3af200`](https://github.com/nodejs/node/commit/7afe3af200)] - **url**: fix file url reparse (Daijiro Wachi) [#35671](https://github.com/nodejs/node/pull/35671)

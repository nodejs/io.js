# Node.js 18 ChangeLog

<!--lint disable maximum-line-length no-literal-urls prohibited-strings-->

<table>
<tr>
<th>LTS 'Hydrogen'</th>
<th>Current</th>
</tr>
<tr>
<td>
<a href="#18.20.5">18.20.5</a><br/>
<a href="#18.20.4">18.20.4</a><br/>
<a href="#18.20.3">18.20.3</a><br/>
<a href="#18.20.2">18.20.2</a><br/>
<a href="#18.20.1">18.20.1</a><br/>
<a href="#18.20.0">18.20.0</a><br/>
<a href="#18.19.1">18.19.1</a><br/>
<a href="#18.19.0">18.19.0</a><br/>
<a href="#18.18.2">18.18.2</a><br/>
<a href="#18.18.1">18.18.1</a><br/>
<a href="#18.18.0">18.18.0</a><br/>
<a href="#18.17.1">18.17.1</a><br/>
<a href="#18.17.0">18.17.0</a><br/>
<a href="#18.16.1">18.16.1</a><br/>
<a href="#18.16.0">18.16.0</a><br/>
<a href="#18.15.0">18.15.0</a><br/>
<a href="#18.14.2">18.14.2</a><br/>
<a href="#18.14.1">18.14.1</a><br/>
<a href="#18.14.0">18.14.0</a><br/>
<a href="#18.13.0">18.13.0</a><br/>
<a href="#18.12.1">18.12.1</a><br/>
<a href="#18.12.0">18.12.0</a><br/>
</td>
<td>
<a href="#18.11.0">18.11.0</a><br/>
<a href="#18.10.0">18.10.0</a><br/>
<a href="#18.9.1">18.9.1</a><br/>
<a href="#18.9.0">18.9.0</a><br/>
<a href="#18.8.0">18.8.0</a><br/>
<a href="#18.7.0">18.7.0</a><br/>
<a href="#18.6.0">18.6.0</a><br/>
<a href="#18.5.0">18.5.0</a><br/>
<a href="#18.4.0">18.4.0</a><br/>
<a href="#18.3.0">18.3.0</a><br/>
<a href="#18.2.0">18.2.0</a><br/>
<a href="#18.1.0">18.1.0</a><br/>
<a href="#18.0.0">18.0.0</a><br/>
</td>
</tr>
</table>

* Other Versions
  * [23.x](CHANGELOG_V23.md)
  * [22.x](CHANGELOG_V22.md)
  * [21.x](CHANGELOG_V21.md)
  * [20.x](CHANGELOG_V20.md)
  * [19.x](CHANGELOG_V19.md)
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
  * [0.10.x](CHANGELOG_V010.md)
  * [io.js](CHANGELOG_IOJS.md)
  * [Archive](CHANGELOG_ARCHIVE.md)

<a id="18.20.5"></a>

## 2024-11-12, Version 18.20.5 'Hydrogen' (LTS), @aduh95

### Notable Changes

* \[[`ac37e554a5`](https://github.com/nodejs/node/commit/ac37e554a5)] - **esm**: mark import attributes and JSON module as stable (Nicolò Ribaudo) [#55333](https://github.com/nodejs/node/pull/55333)

### Commits

* \[[`c2e6a8f215`](https://github.com/nodejs/node/commit/c2e6a8f215)] - **benchmark**: fix napi/ref addon (Michaël Zasso) [#53233](https://github.com/nodejs/node/pull/53233)
* \[[`4c2e07aaac`](https://github.com/nodejs/node/commit/4c2e07aaac)] - **build**: pin doc workflow to Node.js 20 (Richard Lau) [#55755](https://github.com/nodejs/node/pull/55755)
* \[[`6ba4ebd060`](https://github.com/nodejs/node/commit/6ba4ebd060)] - **build**: fix build with Python 3.12 (Luigi Pinca) [#50582](https://github.com/nodejs/node/pull/50582)
* \[[`c50f01399e`](https://github.com/nodejs/node/commit/c50f01399e)] - **crypto**: ensure invalid SubtleCrypto JWK data import results in DataError (Filip Skokan) [#55041](https://github.com/nodejs/node/pull/55041)
* \[[`5c46782137`](https://github.com/nodejs/node/commit/5c46782137)] - **crypto**: make deriveBits length parameter optional and nullable (Filip Skokan) [#53601](https://github.com/nodejs/node/pull/53601)
* \[[`6e7274fa53`](https://github.com/nodejs/node/commit/6e7274fa53)] - **crypto**: reject dh,x25519,x448 in {Sign,Verify}Final (Huáng Jùnliàng) [#53774](https://github.com/nodejs/node/pull/53774)
* \[[`d2442044db`](https://github.com/nodejs/node/commit/d2442044db)] - **crypto**: reject Ed25519/Ed448 in Sign/Verify prototypes (Filip Skokan) [#52340](https://github.com/nodejs/node/pull/52340)
* \[[`93670de499`](https://github.com/nodejs/node/commit/93670de499)] - **deps**: upgrade npm to 10.8.2 (npm team) [#53799](https://github.com/nodejs/node/pull/53799)
* \[[`8531c95587`](https://github.com/nodejs/node/commit/8531c95587)] - **deps**: upgrade npm to 10.8.1 (npm team) [#53207](https://github.com/nodejs/node/pull/53207)
* \[[`fd9933ea0f`](https://github.com/nodejs/node/commit/fd9933ea0f)] - **deps**: upgrade npm to 10.8.0 (npm team) [#53014](https://github.com/nodejs/node/pull/53014)
* \[[`03852495d7`](https://github.com/nodejs/node/commit/03852495d7)] - **deps**: update simdutf to 5.6.0 (Node.js GitHub Bot) [#55379](https://github.com/nodejs/node/pull/55379)
* \[[`3597be4146`](https://github.com/nodejs/node/commit/3597be4146)] - **deps**: update simdutf to 5.5.0 (Node.js GitHub Bot) [#54434](https://github.com/nodejs/node/pull/54434)
* \[[`52d2c03738`](https://github.com/nodejs/node/commit/52d2c03738)] - **deps**: update simdutf to 5.3.4 (Node.js GitHub Bot) [#54312](https://github.com/nodejs/node/pull/54312)
* \[[`dd882ac483`](https://github.com/nodejs/node/commit/dd882ac483)] - **deps**: update simdutf to 5.3.1 (Node.js GitHub Bot) [#54196](https://github.com/nodejs/node/pull/54196)
* \[[`5fb8e1b428`](https://github.com/nodejs/node/commit/5fb8e1b428)] - **deps**: update simdutf to 5.3.0 (Node.js GitHub Bot) [#53837](https://github.com/nodejs/node/pull/53837)
* \[[`c952fd886d`](https://github.com/nodejs/node/commit/c952fd886d)] - **deps**: update simdutf to 5.2.8 (Node.js GitHub Bot) [#52727](https://github.com/nodejs/node/pull/52727)
* \[[`a1ae050ed5`](https://github.com/nodejs/node/commit/a1ae050ed5)] - **deps**: update simdutf to 5.2.6 (Node.js GitHub Bot) [#52727](https://github.com/nodejs/node/pull/52727)
* \[[`96ec48da7f`](https://github.com/nodejs/node/commit/96ec48da7f)] - **deps**: update brotli to 1.1.0 (Node.js GitHub Bot) [#50804](https://github.com/nodejs/node/pull/50804)
* \[[`11242bcfb4`](https://github.com/nodejs/node/commit/11242bcfb4)] - **deps**: update zlib to 1.3.0.1-motley-71660e1 (Node.js GitHub Bot) [#53464](https://github.com/nodejs/node/pull/53464)
* \[[`64f98a9869`](https://github.com/nodejs/node/commit/64f98a9869)] - **deps**: update zlib to 1.3.0.1-motley-c2469fd (Node.js GitHub Bot) [#53464](https://github.com/nodejs/node/pull/53464)
* \[[`4b815550e0`](https://github.com/nodejs/node/commit/4b815550e0)] - **deps**: update zlib to 1.3.0.1-motley-68e57e6 (Node.js GitHub Bot) [#53464](https://github.com/nodejs/node/pull/53464)
* \[[`f6b2f68ce7`](https://github.com/nodejs/node/commit/f6b2f68ce7)] - **deps**: update zlib to 1.3.0.1-motley-8b7eff8 (Node.js GitHub Bot) [#53464](https://github.com/nodejs/node/pull/53464)
* \[[`e151ebef86`](https://github.com/nodejs/node/commit/e151ebef86)] - **deps**: update zlib to 1.3.0.1-motley-e432200 (Node.js GitHub Bot) [#53464](https://github.com/nodejs/node/pull/53464)
* \[[`637a306e02`](https://github.com/nodejs/node/commit/637a306e02)] - **deps**: update zlib to 1.3.0.1-motley-887bb57 (Node.js GitHub Bot) [#53464](https://github.com/nodejs/node/pull/53464)
* \[[`569a739569`](https://github.com/nodejs/node/commit/569a739569)] - **deps**: update zlib to 1.3.0.1-motley-209717d (Node.js GitHub Bot) [#53156](https://github.com/nodejs/node/pull/53156)
* \[[`033f1e2ba5`](https://github.com/nodejs/node/commit/033f1e2ba5)] - **deps**: update zlib to 1.3.0.1-motley-4f653ff (Node.js GitHub Bot) [#53052](https://github.com/nodejs/node/pull/53052)
* \[[`aaa857fc01`](https://github.com/nodejs/node/commit/aaa857fc01)] - **deps**: update ada to 2.8.0 (Node.js GitHub Bot) [#53254](https://github.com/nodejs/node/pull/53254)
* \[[`d577321877`](https://github.com/nodejs/node/commit/d577321877)] - **deps**: update acorn to 8.13.0 (Node.js GitHub Bot) [#55558](https://github.com/nodejs/node/pull/55558)
* \[[`55b3c8a41f`](https://github.com/nodejs/node/commit/55b3c8a41f)] - **deps**: update acorn-walk to 8.3.4 (Node.js GitHub Bot) [#54950](https://github.com/nodejs/node/pull/54950)
* \[[`50a9456f1e`](https://github.com/nodejs/node/commit/50a9456f1e)] - **deps**: update acorn-walk to 8.3.3 (Node.js GitHub Bot) [#53466](https://github.com/nodejs/node/pull/53466)
* \[[`f56cfe776b`](https://github.com/nodejs/node/commit/f56cfe776b)] - **deps**: update acorn to 8.12.1 (Node.js GitHub Bot) [#53465](https://github.com/nodejs/node/pull/53465)
* \[[`fce3ab686d`](https://github.com/nodejs/node/commit/fce3ab686d)] - **deps**: update archs files for openssl-3.0.15+quic1 (Node.js GitHub Bot) [#55184](https://github.com/nodejs/node/pull/55184)
* \[[`46c782486e`](https://github.com/nodejs/node/commit/46c782486e)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.15+quic1 (Node.js GitHub Bot) [#55184](https://github.com/nodejs/node/pull/55184)
* \[[`4a18581dc3`](https://github.com/nodejs/node/commit/4a18581dc3)] - **deps**: update corepack to 0.29.4 (Node.js GitHub Bot) [#54845](https://github.com/nodejs/node/pull/54845)
* \[[`67e98831ab`](https://github.com/nodejs/node/commit/67e98831ab)] - **deps**: update archs files for openssl-3.0.14+quic1 (Node.js GitHub Bot) [#54336](https://github.com/nodejs/node/pull/54336)
* \[[`c60c6630af`](https://github.com/nodejs/node/commit/c60c6630af)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.14+quic1 (Node.js GitHub Bot) [#54336](https://github.com/nodejs/node/pull/54336)
* \[[`935a506377`](https://github.com/nodejs/node/commit/935a506377)] - **deps**: update corepack to 0.29.3 (Node.js GitHub Bot) [#54072](https://github.com/nodejs/node/pull/54072)
* \[[`dbdfdd0226`](https://github.com/nodejs/node/commit/dbdfdd0226)] - **deps**: update corepack to 0.29.2 (Node.js GitHub Bot) [#53838](https://github.com/nodejs/node/pull/53838)
* \[[`395ee44608`](https://github.com/nodejs/node/commit/395ee44608)] - **deps**: update corepack to 0.28.2 (Node.js GitHub Bot) [#53253](https://github.com/nodejs/node/pull/53253)
* \[[`6ba8bc0618`](https://github.com/nodejs/node/commit/6ba8bc0618)] - **deps**: update c-ares to 1.29.0 (Node.js GitHub Bot) [#53155](https://github.com/nodejs/node/pull/53155)
* \[[`81c3260cd2`](https://github.com/nodejs/node/commit/81c3260cd2)] - **deps**: update corepack to 0.28.1 (Node.js GitHub Bot) [#52946](https://github.com/nodejs/node/pull/52946)
* \[[`e4739e9aa6`](https://github.com/nodejs/node/commit/e4739e9aa6)] - **doc**: only apply content-visibility on all.html (Filip Skokan) [#53510](https://github.com/nodejs/node/pull/53510)
* \[[`4d2ac5d98f`](https://github.com/nodejs/node/commit/4d2ac5d98f)] - **doc**: move release key for Myles Borins (Richard Lau) [#54059](https://github.com/nodejs/node/pull/54059)
* \[[`1c4decc998`](https://github.com/nodejs/node/commit/1c4decc998)] - **doc**: add release key for aduh95 (Antoine du Hamel) [#55349](https://github.com/nodejs/node/pull/55349)
* \[[`a4f6f0918f`](https://github.com/nodejs/node/commit/a4f6f0918f)] - **doc**: add names next to release key bash commands (Aviv Keller) [#52878](https://github.com/nodejs/node/pull/52878)
* \[[`c679348f83`](https://github.com/nodejs/node/commit/c679348f83)] - **errors**: use `determineSpecificType` in more error messages (Antoine du Hamel) [#49580](https://github.com/nodejs/node/pull/49580)
* \[[`3059262185`](https://github.com/nodejs/node/commit/3059262185)] - **esm**: fix broken assertion in `legacyMainResolve` (Antoine du Hamel) [#55708](https://github.com/nodejs/node/pull/55708)
* \[[`ac37e554a5`](https://github.com/nodejs/node/commit/ac37e554a5)] - **esm**: mark import attributes and JSON module as stable (Nicolò Ribaudo) [#55333](https://github.com/nodejs/node/pull/55333)
* \[[`84b0ead758`](https://github.com/nodejs/node/commit/84b0ead758)] - **esm**: fix hook name in error message (Bruce MacNaughton) [#50466](https://github.com/nodejs/node/pull/50466)
* \[[`0092358d00`](https://github.com/nodejs/node/commit/0092358d00)] - **http**: handle multi-value content-disposition header (Arsalan Ahmad) [#50977](https://github.com/nodejs/node/pull/50977)
* \[[`d814fe935c`](https://github.com/nodejs/node/commit/d814fe935c)] - **src**: account for OpenSSL unexpected version (Shelley Vohr) [#54038](https://github.com/nodejs/node/pull/54038)
* \[[`6615fe5db1`](https://github.com/nodejs/node/commit/6615fe5db1)] - **src**: fix dynamically linked OpenSSL version (Richard Lau) [#53456](https://github.com/nodejs/node/pull/53456)
* \[[`d6114cb2e2`](https://github.com/nodejs/node/commit/d6114cb2e2)] - **test**: fix test when compiled without engine support (Richard Lau) [#53232](https://github.com/nodejs/node/pull/53232)
* \[[`ac3a39051c`](https://github.com/nodejs/node/commit/ac3a39051c)] - **test**: fix test-tls-junk-closes-server (Michael Dawson) [#55089](https://github.com/nodejs/node/pull/55089)
* \[[`c8520ff7d2`](https://github.com/nodejs/node/commit/c8520ff7d2)] - **test**: fix OpenSSL version checks (Richard Lau) [#53503](https://github.com/nodejs/node/pull/53503)
* \[[`9824827937`](https://github.com/nodejs/node/commit/9824827937)] - **test**: update tls test to support OpenSSL32 (Michael Dawson) [#55030](https://github.com/nodejs/node/pull/55030)
* \[[`1a4d497936`](https://github.com/nodejs/node/commit/1a4d497936)] - **test**: adjust tls-set-ciphers for OpenSSL32 (Michael Dawson) [#55016](https://github.com/nodejs/node/pull/55016)
* \[[`341496a5a2`](https://github.com/nodejs/node/commit/341496a5a2)] - **test**: add asserts to validate test assumptions (Michael Dawson) [#54997](https://github.com/nodejs/node/pull/54997)
* \[[`37a2f7eaa4`](https://github.com/nodejs/node/commit/37a2f7eaa4)] - **test**: adjust key sizes to support OpenSSL32 (Michael Dawson) [#54972](https://github.com/nodejs/node/pull/54972)
* \[[`75ff0cdf66`](https://github.com/nodejs/node/commit/75ff0cdf66)] - **test**: update test to support OpenSSL32 (Michael Dawson) [#54968](https://github.com/nodejs/node/pull/54968)
* \[[`b097d85dfe`](https://github.com/nodejs/node/commit/b097d85dfe)] - **test**: adjust test-tls-junk-server for OpenSSL32 (Michael Dawson) [#54926](https://github.com/nodejs/node/pull/54926)
* \[[`e9997388a6`](https://github.com/nodejs/node/commit/e9997388a6)] - **test**: adjust tls test for OpenSSL32 (Michael Dawson) [#54909](https://github.com/nodejs/node/pull/54909)
* \[[`c7de027adb`](https://github.com/nodejs/node/commit/c7de027adb)] - **test**: fix test test-tls-dhe for OpenSSL32 (Michael Dawson) [#54903](https://github.com/nodejs/node/pull/54903)
* \[[`68156cbae1`](https://github.com/nodejs/node/commit/68156cbae1)] - **test**: fix test-tls-client-mindhsize for OpenSSL32 (Michael Dawson) [#54739](https://github.com/nodejs/node/pull/54739)
* \[[`d5b73e5683`](https://github.com/nodejs/node/commit/d5b73e5683)] - **test**: increase key size for ca2-cert.pem (Michael Dawson) [#54599](https://github.com/nodejs/node/pull/54599)
* \[[`5316314755`](https://github.com/nodejs/node/commit/5316314755)] - **test**: update TLS test for OpenSSL 3.2 (Richard Lau) [#54612](https://github.com/nodejs/node/pull/54612)
* \[[`a1f0c87859`](https://github.com/nodejs/node/commit/a1f0c87859)] - **test**: fix test-tls-client-auth test for OpenSSL32 (Michael Dawson) [#54610](https://github.com/nodejs/node/pull/54610)
* \[[`e9e3306426`](https://github.com/nodejs/node/commit/e9e3306426)] - **test**: use assert.{s,deepS}trictEqual() (Sonny) [#54208](https://github.com/nodejs/node/pull/54208)
* \[[`1320fb9475`](https://github.com/nodejs/node/commit/1320fb9475)] - **test**: update TLS trace tests for OpenSSL >= 3.2 (Richard Lau) [#53229](https://github.com/nodejs/node/pull/53229)
* \[[`cc3cdf7cc0`](https://github.com/nodejs/node/commit/cc3cdf7cc0)] - **test**: check against run-time OpenSSL version (Richard Lau) [#53456](https://github.com/nodejs/node/pull/53456)
* \[[`fc43c6803e`](https://github.com/nodejs/node/commit/fc43c6803e)] - **test**: update TLS tests for OpenSSL 3.2 (Richard Lau) [#53384](https://github.com/nodejs/node/pull/53384)
* \[[`627d3993f0`](https://github.com/nodejs/node/commit/627d3993f0)] - **test**: fix unreliable assumption in js-native-api/test\_cannot\_run\_js (Joyee Cheung) [#51898](https://github.com/nodejs/node/pull/51898)
* \[[`9f521f456e`](https://github.com/nodejs/node/commit/9f521f456e)] - **test**: update tests for OpenSSL 3.0.14 (Richard Lau) [#53373](https://github.com/nodejs/node/pull/53373)
* \[[`0fb652eba9`](https://github.com/nodejs/node/commit/0fb652eba9)] - **tools**: update gyp-next to v0.16.1 (Michaël Zasso) [#50380](https://github.com/nodejs/node/pull/50380)
* \[[`fa72b2c2de`](https://github.com/nodejs/node/commit/fa72b2c2de)] - **tools**: skip ruff on tools/gyp (Michaël Zasso) [#50380](https://github.com/nodejs/node/pull/50380)

<a id="18.20.4"></a>

## 2024-07-08, Version 18.20.4 'Hydrogen' (LTS), @RafaelGSS

This is a security release.

### Notable Changes

* CVE-2024-36138 - Bypass incomplete fix of CVE-2024-27980 (High)
* CVE-2024-22020 - Bypass network import restriction via data URL (Medium)

### Commits

* \[[`85abedf1ff`](https://github.com/nodejs/node/commit/85abedf1ff)] - **lib,esm**: handle bypass network-import via data: (RafaelGSS) [nodejs-private/node-private#522](https://github.com/nodejs-private/node-private/pull/522)
* \[[`eccd63b865`](https://github.com/nodejs/node/commit/eccd63b865)] - **src**: handle permissive extension on cmd check (RafaelGSS) [nodejs-private/node-private#596](https://github.com/nodejs-private/node-private/pull/596)

<a id="18.20.3"></a>

## 2024-05-21, Version 18.20.3 'Hydrogen' (LTS), @richardlau

### Notable Changes

This release fixes a regression introduced in Node.js 18.19.0 where `http.server.close()` was incorrectly closing idle connections.

A fix has also been included for compiling Node.js from source with newer versions of Clang.

The list of keys used to sign releases has been synchronized with the current list from the `main` branch.

#### Updated dependencies

* acorn updated to 8.11.3.
* acorn-walk updated to 8.3.2.
* ada updated to 2.7.8.
* c-ares updated to 1.28.1.
* corepack updated to 0.28.0.
* nghttp2 updated to 1.61.0.
* ngtcp2 updated to 1.3.0.
* npm updated to 10.7.0. Includes a fix from npm\@10.5.1 to limit the number of open connections [npm/cli#7324](https://github.com/npm/cli/pull/7324).
* simdutf updated to 5.2.4.
* zlib updated to 1.3.0.1-motley-7d77fb7.

### Commits

* \[[`0c260e10e7`](https://github.com/nodejs/node/commit/0c260e10e7)] - **deps**: update zlib to 1.3.0.1-motley-7d77fb7 (Node.js GitHub Bot) [#52516](https://github.com/nodejs/node/pull/52516)
* \[[`1152d7f919`](https://github.com/nodejs/node/commit/1152d7f919)] - **deps**: update zlib to 1.3.0.1-motley-24c07df (Node.js GitHub Bot) [#52199](https://github.com/nodejs/node/pull/52199)
* \[[`755399db9d`](https://github.com/nodejs/node/commit/755399db9d)] - **deps**: update zlib to 1.3.0.1-motley-24342f6 (Node.js GitHub Bot) [#52123](https://github.com/nodejs/node/pull/52123)
* \[[`af3e32073b`](https://github.com/nodejs/node/commit/af3e32073b)] - **deps**: update ada to 2.7.8 (Node.js GitHub Bot) [#52517](https://github.com/nodejs/node/pull/52517)
* \[[`e4ea2db58b`](https://github.com/nodejs/node/commit/e4ea2db58b)] - **deps**: update c-ares to 1.28.1 (Node.js GitHub Bot) [#52285](https://github.com/nodejs/node/pull/52285)
* \[[`14e857bea2`](https://github.com/nodejs/node/commit/14e857bea2)] - **deps**: update corepack to 0.28.0 (Node.js GitHub Bot) [#52616](https://github.com/nodejs/node/pull/52616)
* \[[`7f5dd44ca6`](https://github.com/nodejs/node/commit/7f5dd44ca6)] - **deps**: upgrade npm to 10.7.0 (npm team) [#52767](https://github.com/nodejs/node/pull/52767)
* \[[`78f84ebb09`](https://github.com/nodejs/node/commit/78f84ebb09)] - **deps**: update ngtcp2 to 1.3.0 (Node.js GitHub Bot) [#51796](https://github.com/nodejs/node/pull/51796)
* \[[`1f489a3753`](https://github.com/nodejs/node/commit/1f489a3753)] - **deps**: update ngtcp2 to 1.2.0 (Node.js GitHub Bot) [#51584](https://github.com/nodejs/node/pull/51584)
* \[[`3034968225`](https://github.com/nodejs/node/commit/3034968225)] - **deps**: update ngtcp2 to 1.1.0 (Node.js GitHub Bot) [#51319](https://github.com/nodejs/node/pull/51319)
* \[[`1aa9da467f`](https://github.com/nodejs/node/commit/1aa9da467f)] - **deps**: add nghttp3/\*\*/.deps to .gitignore (Luigi Pinca) [#51400](https://github.com/nodejs/node/pull/51400)
* \[[`28c0c78c9a`](https://github.com/nodejs/node/commit/28c0c78c9a)] - **deps**: update ngtcp2 and nghttp3 (James M Snell) [#51291](https://github.com/nodejs/node/pull/51291)
* \[[`8fd5a35364`](https://github.com/nodejs/node/commit/8fd5a35364)] - **deps**: upgrade npm to 10.5.2 (npm team) [#52458](https://github.com/nodejs/node/pull/52458)
* \[[`2c53ff31c9`](https://github.com/nodejs/node/commit/2c53ff31c9)] - **deps**: update acorn-walk to 8.3.2 (Node.js GitHub Bot) [#51457](https://github.com/nodejs/node/pull/51457)
* \[[`12f28f33c2`](https://github.com/nodejs/node/commit/12f28f33c2)] - **deps**: update acorn to 8.11.3 (Node.js GitHub Bot) [#51317](https://github.com/nodejs/node/pull/51317)
* \[[`dddb7eb3e0`](https://github.com/nodejs/node/commit/dddb7eb3e0)] - **deps**: update acorn-walk to 8.3.1 (Node.js GitHub Bot) [#50457](https://github.com/nodejs/node/pull/50457)
* \[[`c86550e607`](https://github.com/nodejs/node/commit/c86550e607)] - **deps**: update acorn-walk to 8.3.0 (Node.js GitHub Bot) [#50457](https://github.com/nodejs/node/pull/50457)
* \[[`9500817f66`](https://github.com/nodejs/node/commit/9500817f66)] - **deps**: update acorn to 8.11.2 (Node.js GitHub Bot) [#50460](https://github.com/nodejs/node/pull/50460)
* \[[`7a8c7b6275`](https://github.com/nodejs/node/commit/7a8c7b6275)] - **deps**: update ada to 2.7.7 (Node.js GitHub Bot) [#52028](https://github.com/nodejs/node/pull/52028)
* \[[`b199889943`](https://github.com/nodejs/node/commit/b199889943)] - **deps**: update corepack to 0.26.0 (Node.js GitHub Bot) [#52027](https://github.com/nodejs/node/pull/52027)
* \[[`052b0ba0c6`](https://github.com/nodejs/node/commit/052b0ba0c6)] - **deps**: upgrade npm to 10.5.1 (npm team) [#52351](https://github.com/nodejs/node/pull/52351)
* \[[`209823d3af`](https://github.com/nodejs/node/commit/209823d3af)] - **deps**: update simdutf to 5.2.4 (Node.js GitHub Bot) [#52473](https://github.com/nodejs/node/pull/52473)
* \[[`5114cbe18a`](https://github.com/nodejs/node/commit/5114cbe18a)] - **deps**: update simdutf to 5.2.3 (Yagiz Nizipli) [#52381](https://github.com/nodejs/node/pull/52381)
* \[[`be30309ea0`](https://github.com/nodejs/node/commit/be30309ea0)] - **deps**: update simdutf to 5.0.0 (Daniel Lemire) [#52138](https://github.com/nodejs/node/pull/52138)
* \[[`b56f66e250`](https://github.com/nodejs/node/commit/b56f66e250)] - **deps**: update simdutf to 4.0.9 (Node.js GitHub Bot) [#51655](https://github.com/nodejs/node/pull/51655)
* \[[`a9f3b9d9d1`](https://github.com/nodejs/node/commit/a9f3b9d9d1)] - **deps**: update nghttp2 to 1.61.0 (Node.js GitHub Bot) [#52395](https://github.com/nodejs/node/pull/52395)
* \[[`1b6fa70620`](https://github.com/nodejs/node/commit/1b6fa70620)] - **deps**: update nghttp2 to 1.60.0 (Node.js GitHub Bot) [#51948](https://github.com/nodejs/node/pull/51948)
* \[[`3c9dbbf4d4`](https://github.com/nodejs/node/commit/3c9dbbf4d4)] - **deps**: update nghttp2 to 1.59.0 (Node.js GitHub Bot) [#51581](https://github.com/nodejs/node/pull/51581)
* \[[`e28316da54`](https://github.com/nodejs/node/commit/e28316da54)] - **deps**: update nghttp2 to 1.58.0 (Node.js GitHub Bot) [#50441](https://github.com/nodejs/node/pull/50441)
* \[[`678641f470`](https://github.com/nodejs/node/commit/678641f470)] - **deps**: V8: cherry-pick d15d49b09dc7 (Bo Anderson) [#52337](https://github.com/nodejs/node/pull/52337)
* \[[`1147fee7d9`](https://github.com/nodejs/node/commit/1147fee7d9)] - **doc**: remove ableist language from crypto (Jamie King) [#52063](https://github.com/nodejs/node/pull/52063)
* \[[`5e93eae972`](https://github.com/nodejs/node/commit/5e93eae972)] - **doc**: add release key for marco-ippolito (marco-ippolito) [#52257](https://github.com/nodejs/node/pull/52257)
* \[[`6689a98488`](https://github.com/nodejs/node/commit/6689a98488)] - **http**: remove closeIdleConnections function while calling server close (Kumar Rishav) [#52336](https://github.com/nodejs/node/pull/52336)
* \[[`71616e8a8a`](https://github.com/nodejs/node/commit/71616e8a8a)] - **node-api**: make tsfn accept napi\_finalize once more (Gabriel Schulhof) [#51801](https://github.com/nodejs/node/pull/51801)
* \[[`d9d9e62474`](https://github.com/nodejs/node/commit/d9d9e62474)] - **src**: avoid draining platform tasks at FreeEnvironment (Chengzhong Wu) [#51290](https://github.com/nodejs/node/pull/51290)
* \[[`e5fc8ec9fc`](https://github.com/nodejs/node/commit/e5fc8ec9fc)] - **test**: skip v8-updates/test-linux-perf (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`351ef189ca`](https://github.com/nodejs/node/commit/351ef189ca)] - **test**: v8: Add test-linux-perf-logger test suite (Luke Albao) [#50352](https://github.com/nodejs/node/pull/50352)
* \[[`5cec2efc31`](https://github.com/nodejs/node/commit/5cec2efc31)] - **test**: reduce the number of requests and parsers (Luigi Pinca) [#50240](https://github.com/nodejs/node/pull/50240)
* \[[`5186e453d9`](https://github.com/nodejs/node/commit/5186e453d9)] - **test**: deflake test-http-regr-gh-2928 (Luigi Pinca) [#49574](https://github.com/nodejs/node/pull/49574)
* \[[`c60cd67e1c`](https://github.com/nodejs/node/commit/c60cd67e1c)] - **test**: skip test for dynamically linked OpenSSL (Richard Lau) [#52542](https://github.com/nodejs/node/pull/52542)

<a id="18.20.2"></a>

## 2024-04-10, Version 18.20.2 'Hydrogen' (LTS), @RafaelGSS

This is a security release.

### Notable Changes

* CVE-2024-27980 - Command injection via args parameter of `child_process.spawn` without shell option enabled on Windows

### Commits

* \[[`6627222409`](https://github.com/nodejs/node/commit/6627222409)] - **src**: disallow direct .bat and .cmd file spawning (Ben Noordhuis) [nodejs-private/node-private#564](https://github.com/nodejs-private/node-private/pull/564)

<a id="18.20.1"></a>

## 2024-04-03, Version 18.20.1 'Hydrogen' (LTS), @RafaelGSS

This is a security release.

### Notable Changes

* CVE-2024-27983 - Assertion failed in node::http2::Http2Session::\~Http2Session() leads to HTTP/2 server crash- (High)
* CVE-2024-27982 - HTTP Request Smuggling via Content Length Obfuscation - (Medium)
* llhttp version 9.2.1
* undici version 5.28.4

### Commits

* \[[`60d24938de`](https://github.com/nodejs/node/commit/60d24938de)] - **deps**: update undici to v5.28.4 (Matteo Collina) [nodejs-private/node-private#577](https://github.com/nodejs-private/node-private/pull/577)
* \[[`5d4d5848cf`](https://github.com/nodejs/node/commit/5d4d5848cf)] - **http**: do not allow OBS fold in headers by default (Paolo Insogna) [nodejs-private/node-private#558](https://github.com/nodejs-private/node-private/pull/558)
* \[[`0fb816dbcc`](https://github.com/nodejs/node/commit/0fb816dbcc)] - **src**: ensure to close stream when destroying session (Anna Henningsen) [nodejs-private/node-private#561](https://github.com/nodejs-private/node-private/pull/561)

<a id="18.20.0"></a>

## 2024-03-26, Version 18.20.0 'Hydrogen' (LTS), @richardlau

### Notable Changes

#### Added support for import attributes

Support has been added for import attributes, to replace the old import
assertions syntax. This will aid migration by making the new syntax available
across all currently supported Node.js release lines.

This adds the `with` keyword which should be used in place of the previous
`assert` keyword, which will be removed in a future semver-major Node.js
release.

For example,

```console
import "foo" assert { ... }
```

should be replaced with

```console
import "foo" with { ... }
```

For more details, see

* [#50134](https://github.com/nodejs/node/issues/50134)
* [#51622](https://github.com/nodejs/node/issues/51622)

Contributed by Nicolò Ribaudo in [#51136](https://github.com/nodejs/node/pull/51136)
and Antoine du Hamel in [#50140](https://github.com/nodejs/node/pull/50140).

#### Doc deprecation for `dirent.path`

Please use newly added `dirent.parentPath` instead.

Contributed by Antoine du Hamel in [#50976](https://github.com/nodejs/node/pull/50976)
and [#51020](https://github.com/nodejs/node/pull/51020).

#### Experimental node-api feature flags

Introduces an experimental feature to segregate finalizers that affect GC state.
A new type called `node_api_nogc_env` has been introduced as the const version
of `napi_env` and `node_api_nogc_finalize` as a variant of `napi_finalize` that
accepts a `node_api_nogc_env` as its first argument.

This feature can be turned off by defining
`NODE_API_EXPERIMENTAL_NOGC_ENV_OPT_OUT`.

Contributed by Gabriel Schulhof in [#50060](https://github.com/nodejs/node/pull/50060).

#### Root certificates updated to NSS 3.98

Certificates added:

* Telekom Security TLS ECC Root 2020
* Telekom Security TLS RSA Root 2023

Certificates removed:

* Security Communication Root CA

#### Updated dependencies

* ada updated to 2.7.6.
* base64 updated to 0.5.2.
* c-ares updated to 1.27.0.
* corepack updated to 0.25.2.
* ICU updated to 74.2. Includes CLDR 44.1 and Unicode 15.1.
* npm updated to 10.5.0. Fixes a regression in signals not being passed onto child processes.
* simdutf8 updated to 4.0.8.
* Timezone updated to 2024a.
* zlib updated to 1.3.0.1-motley-40e35a7.

#### vm: fix V8 compilation cache support for vm.Script

Previously repeated compilation of the same source code using `vm.Script`
stopped hitting the V8 compilation cache after v16.x when support for
`importModuleDynamically` was added to `vm.Script`, resulting in a performance
regression that blocked users (in particular Jest users) from upgrading from
v16.x.

The recent fixes allow the compilation cache to be hit again
for `vm.Script` when `--experimental-vm-modules` is not used even in the
presence of the `importModuleDynamically` option, so that users affected by the
performance regression can now upgrade. Ongoing work is also being done to
enable compilation cache support for `vm.CompileFunction`.

Contributed by Joyee Cheung in [#49950](https://github.com/nodejs/node/pull/49950)
and [#50137](https://github.com/nodejs/node/pull/50137).

### Commits

* \[[`c70383b8d4`](https://github.com/nodejs/node/commit/c70383b8d4)] - **build**: support Python 3.12 (Shi Pujin) [#50209](https://github.com/nodejs/node/pull/50209)
* \[[`4b960c3a4a`](https://github.com/nodejs/node/commit/4b960c3a4a)] - **build**: fix incorrect g++ warning message (Richard Lau) [#51695](https://github.com/nodejs/node/pull/51695)
* \[[`8fdea67694`](https://github.com/nodejs/node/commit/8fdea67694)] - **crypto**: update root certificates to NSS 3.98 (Node.js GitHub Bot) [#51794](https://github.com/nodejs/node/pull/51794)
* \[[`812b126dd9`](https://github.com/nodejs/node/commit/812b126dd9)] - **deps**: V8: cherry-pick d90d4533b053 (Michaël Zasso) [#50077](https://github.com/nodejs/node/pull/50077)
* \[[`9ab8c3db87`](https://github.com/nodejs/node/commit/9ab8c3db87)] - **deps**: update c-ares to 1.27.0 (Node.js GitHub Bot) [#51846](https://github.com/nodejs/node/pull/51846)
* \[[`c688680387`](https://github.com/nodejs/node/commit/c688680387)] - **deps**: update c-ares to 1.26.0 (Node.js GitHub Bot) [#51582](https://github.com/nodejs/node/pull/51582)
* \[[`9498ac8a47`](https://github.com/nodejs/node/commit/9498ac8a47)] - **deps**: compile c-ares with C11 support (Michaël Zasso) [#51410](https://github.com/nodejs/node/pull/51410)
* \[[`8fb743642f`](https://github.com/nodejs/node/commit/8fb743642f)] - **deps**: update c-ares to 1.25.0 (Node.js GitHub Bot) [#51385](https://github.com/nodejs/node/pull/51385)
* \[[`7bea2d7c12`](https://github.com/nodejs/node/commit/7bea2d7c12)] - **deps**: update zlib to 1.3.0.1-motley-40e35a7 (Node.js GitHub Bot) [#51274](https://github.com/nodejs/node/pull/51274)
* \[[`57a38c8f75`](https://github.com/nodejs/node/commit/57a38c8f75)] - **deps**: update zlib to 1.3.0.1-motley-dd5fc13 (Node.js GitHub Bot) [#51105](https://github.com/nodejs/node/pull/51105)
* \[[`b0ca084a6b`](https://github.com/nodejs/node/commit/b0ca084a6b)] - **deps**: update zlib to 1.3-22124f5 (Node.js GitHub Bot) [#50910](https://github.com/nodejs/node/pull/50910)
* \[[`4b43823f37`](https://github.com/nodejs/node/commit/4b43823f37)] - **deps**: update zlib to 1.2.13.1-motley-5daffc7 (Node.js GitHub Bot) [#50803](https://github.com/nodejs/node/pull/50803)
* \[[`f0da591812`](https://github.com/nodejs/node/commit/f0da591812)] - **deps**: update zlib to 1.2.13.1-motley-dfc48fc (Node.js GitHub Bot) [#50456](https://github.com/nodejs/node/pull/50456)
* \[[`16d28a883a`](https://github.com/nodejs/node/commit/16d28a883a)] - **deps**: update base64 to 0.5.2 (Node.js GitHub Bot) [#51455](https://github.com/nodejs/node/pull/51455)
* \[[`13a9e81cb6`](https://github.com/nodejs/node/commit/13a9e81cb6)] - **deps**: update base64 to 0.5.1 (Node.js GitHub Bot) [#50629](https://github.com/nodejs/node/pull/50629)
* \[[`b4502d3ac5`](https://github.com/nodejs/node/commit/b4502d3ac5)] - **deps**: update simdutf to 4.0.8 (Node.js GitHub Bot) [#51000](https://github.com/nodejs/node/pull/51000)
* \[[`183cf8a74a`](https://github.com/nodejs/node/commit/183cf8a74a)] - **deps**: update simdutf to 4.0.4 (Node.js GitHub Bot) [#50772](https://github.com/nodejs/node/pull/50772)
* \[[`11ba8593ea`](https://github.com/nodejs/node/commit/11ba8593ea)] - **deps**: update ada to 2.7.6 (Node.js GitHub Bot) [#51542](https://github.com/nodejs/node/pull/51542)
* \[[`73a946d55c`](https://github.com/nodejs/node/commit/73a946d55c)] - **deps**: update ada to 2.7.5 (Node.js GitHub Bot) [#51542](https://github.com/nodejs/node/pull/51542)
* \[[`cc434c1a39`](https://github.com/nodejs/node/commit/cc434c1a39)] - **deps**: update ada to 2.7.4 (Node.js GitHub Bot) [#50815](https://github.com/nodejs/node/pull/50815)
* \[[`3a3808a6ae`](https://github.com/nodejs/node/commit/3a3808a6ae)] - **deps**: upgrade npm to 10.5.0 (npm team) [#51913](https://github.com/nodejs/node/pull/51913)
* \[[`c8876d765c`](https://github.com/nodejs/node/commit/c8876d765c)] - **deps**: upgrade npm to 10.3.0 (npm team) [#51431](https://github.com/nodejs/node/pull/51431)
* \[[`5aec3af460`](https://github.com/nodejs/node/commit/5aec3af460)] - **deps**: update corepack to 0.25.2 (Node.js GitHub Bot) [#51810](https://github.com/nodejs/node/pull/51810)
* \[[`a593985326`](https://github.com/nodejs/node/commit/a593985326)] - **deps**: update corepack to 0.24.1 (Node.js GitHub Bot) [#51459](https://github.com/nodejs/node/pull/51459)
* \[[`d1a9237bf5`](https://github.com/nodejs/node/commit/d1a9237bf5)] - **deps**: update corepack to 0.24.0 (Node.js GitHub Bot) [#51318](https://github.com/nodejs/node/pull/51318)
* \[[`adac0c7a63`](https://github.com/nodejs/node/commit/adac0c7a63)] - **deps**: update corepack to 0.23.0 (Node.js GitHub Bot) [#50563](https://github.com/nodejs/node/pull/50563)
* \[[`4a6f83e32a`](https://github.com/nodejs/node/commit/4a6f83e32a)] - **deps**: escape Python strings correctly (Michaël Zasso) [#50695](https://github.com/nodejs/node/pull/50695)
* \[[`c13969e52a`](https://github.com/nodejs/node/commit/c13969e52a)] - **deps**: V8: cherry-pick ea996ad04a68 (Nicolò Ribaudo) [#51136](https://github.com/nodejs/node/pull/51136)
* \[[`6fbf0ba5c3`](https://github.com/nodejs/node/commit/6fbf0ba5c3)] - **deps**: V8: cherry-pick a0fd3209dda8 (Nicolò Ribaudo) [#51136](https://github.com/nodejs/node/pull/51136)
* \[[`68fd7516e1`](https://github.com/nodejs/node/commit/68fd7516e1)] - **deps**: update timezone to 2024a (Michaël Zasso) [#51723](https://github.com/nodejs/node/pull/51723)
* \[[`f9b229ebe1`](https://github.com/nodejs/node/commit/f9b229ebe1)] - **deps**: update icu to 74.2 (Michaël Zasso) [#51723](https://github.com/nodejs/node/pull/51723)
* \[[`90c73d2eb4`](https://github.com/nodejs/node/commit/90c73d2eb4)] - **deps**: update timezone to 2023d (Node.js GitHub Bot) [#51461](https://github.com/nodejs/node/pull/51461)
* \[[`2a2bf57028`](https://github.com/nodejs/node/commit/2a2bf57028)] - **deps**: update icu to 74.1 (Node.js GitHub Bot) [#50515](https://github.com/nodejs/node/pull/50515)
* \[[`425e011e52`](https://github.com/nodejs/node/commit/425e011e52)] - **deps**: add v8::Object::SetInternalFieldForNodeCore() (Joyee Cheung) [#49874](https://github.com/nodejs/node/pull/49874)
* \[[`58c70344a2`](https://github.com/nodejs/node/commit/58c70344a2)] - **deps**: V8: cherry-pick 705e374124ae (Joyee Cheung) [#51004](https://github.com/nodejs/node/pull/51004)
* \[[`b0e88899e1`](https://github.com/nodejs/node/commit/b0e88899e1)] - **deps**: V8: cherry-pick 1fada6b36f8d (Joyee Cheung) [#51004](https://github.com/nodejs/node/pull/51004)
* \[[`d87a810b81`](https://github.com/nodejs/node/commit/d87a810b81)] - **deps**: V8: cherry-pick 3dd9576ce336 (Joyee Cheung) [#51004](https://github.com/nodejs/node/pull/51004)
* \[[`6d50966876`](https://github.com/nodejs/node/commit/6d50966876)] - **deps**: V8: cherry-pick 94e8282325a1 (Joyee Cheung) [#51004](https://github.com/nodejs/node/pull/51004)
* \[[`fafbacdfec`](https://github.com/nodejs/node/commit/fafbacdfec)] - **deps**: V8: cherry-pick 9a98f96b6d68 (Joyee Cheung) [#51004](https://github.com/nodejs/node/pull/51004)
* \[[`d4a530ed8d`](https://github.com/nodejs/node/commit/d4a530ed8d)] - **deps**: V8: cherry-pick 7f5daed62d47 (Joyee Cheung) [#51004](https://github.com/nodejs/node/pull/51004)
* \[[`1ce901b164`](https://github.com/nodejs/node/commit/1ce901b164)] - **deps**: V8: cherry-pick c400af48b5ef (Joyee Cheung) [#51004](https://github.com/nodejs/node/pull/51004)
* \[[`f232064f35`](https://github.com/nodejs/node/commit/f232064f35)] - **doc**: fix historical experimental fetch flag (Kenrick) [#51506](https://github.com/nodejs/node/pull/51506)
* \[[`194ff6a40f`](https://github.com/nodejs/node/commit/194ff6a40f)] - **(SEMVER-MINOR)** **doc**: add deprecation notice to `dirent.path` (Antoine du Hamel) [#50976](https://github.com/nodejs/node/pull/50976)
* \[[`0f09267dc6`](https://github.com/nodejs/node/commit/0f09267dc6)] - **(SEMVER-MINOR)** **doc**: deprecate `dirent.path` (Antoine du Hamel) [#50976](https://github.com/nodejs/node/pull/50976)
* \[[`8bfb8f5b2f`](https://github.com/nodejs/node/commit/8bfb8f5b2f)] - **doc,crypto**: further clarify RSA\_PKCS1\_PADDING support (Tobias Nießen) [#51799](https://github.com/nodejs/node/pull/51799)
* \[[`c7baf7b274`](https://github.com/nodejs/node/commit/c7baf7b274)] - **doc,crypto**: add changelog and note about disabled RSA\_PKCS1\_PADDING (Filip Skokan) [#51782](https://github.com/nodejs/node/pull/51782)
* \[[`a193be3dc2`](https://github.com/nodejs/node/commit/a193be3dc2)] - **esm**: use import attributes instead of import assertions (Antoine du Hamel) [#50140](https://github.com/nodejs/node/pull/50140)
* \[[`26e8f7793e`](https://github.com/nodejs/node/commit/26e8f7793e)] - **(SEMVER-MINOR)** **fs**: introduce `dirent.parentPath` (Antoine du Hamel) [#50976](https://github.com/nodejs/node/pull/50976)
* \[[`5b5e5192f7`](https://github.com/nodejs/node/commit/5b5e5192f7)] - **lib**: fix compileFunction throws range error for negative numbers (Jithil P Ponnan) [#49855](https://github.com/nodejs/node/pull/49855)
* \[[`7552de6806`](https://github.com/nodejs/node/commit/7552de6806)] - **module**: fix the leak in SourceTextModule and ContextifySript (Joyee Cheung) [#48510](https://github.com/nodejs/node/pull/48510)
* \[[`2e05cf1c60`](https://github.com/nodejs/node/commit/2e05cf1c60)] - **module**: fix leak of vm.SyntheticModule (Joyee Cheung) [#48510](https://github.com/nodejs/node/pull/48510)
* \[[`a86a2e14a3`](https://github.com/nodejs/node/commit/a86a2e14a3)] - **module**: use symbol in WeakMap to manage host defined options (Joyee Cheung) [#48510](https://github.com/nodejs/node/pull/48510)
* \[[`32906ddcac`](https://github.com/nodejs/node/commit/32906ddcac)] - **node-api**: segregate nogc APIs from rest via type system (Gabriel Schulhof) [#50060](https://github.com/nodejs/node/pull/50060)
* \[[`1aa71c26ff`](https://github.com/nodejs/node/commit/1aa71c26ff)] - **node-api**: factor out common code into macros (Gabriel Schulhof) [#50664](https://github.com/nodejs/node/pull/50664)
* \[[`3d0b233f52`](https://github.com/nodejs/node/commit/3d0b233f52)] - **node-api**: introduce experimental feature flags (Gabriel Schulhof) [#50991](https://github.com/nodejs/node/pull/50991)
* \[[`96514a8b9f`](https://github.com/nodejs/node/commit/96514a8b9f)] - **src**: iterate on import attributes array correctly (Michaël Zasso) [#50703](https://github.com/nodejs/node/pull/50703)
* \[[`2c2892bf88`](https://github.com/nodejs/node/commit/2c2892bf88)] - **src**: set ModuleWrap internal fields only once (Joyee Cheung) [#49391](https://github.com/nodejs/node/pull/49391)
* \[[`ff334cb774`](https://github.com/nodejs/node/commit/ff334cb774)] - **src**: cast v8::Object::GetInternalField() return value to v8::Value (Joyee Cheung) [#48943](https://github.com/nodejs/node/pull/48943)
* \[[`270b519971`](https://github.com/nodejs/node/commit/270b519971)] - **stream**: do not defer construction by one microtick (Matteo Collina) [#52005](https://github.com/nodejs/node/pull/52005)
* \[[`95d7a75084`](https://github.com/nodejs/node/commit/95d7a75084)] - **test**: fix dns test case failures after c-ares update to 1.21.0+ (Brad House) [#50743](https://github.com/nodejs/node/pull/50743)
* \[[`cd613e5167`](https://github.com/nodejs/node/commit/cd613e5167)] - **test**: handle relative https redirect (Richard Lau) [#51121](https://github.com/nodejs/node/pull/51121)
* \[[`40f10eafcf`](https://github.com/nodejs/node/commit/40f10eafcf)] - **test**: fix `internet/test-inspector-help-page` (Richard Lau) [#51693](https://github.com/nodejs/node/pull/51693)
* \[[`5e426511b1`](https://github.com/nodejs/node/commit/5e426511b1)] - **test**: deflake test-vm-contextified-script-leak (Joyee Cheung) [#49710](https://github.com/nodejs/node/pull/49710)
* \[[`0b156c6d28`](https://github.com/nodejs/node/commit/0b156c6d28)] - **test**: use checkIfCollectable in vm leak tests (Joyee Cheung) [#49671](https://github.com/nodejs/node/pull/49671)
* \[[`1586c11b3c`](https://github.com/nodejs/node/commit/1586c11b3c)] - **test**: add checkIfCollectable to test/common/gc.js (Joyee Cheung) [#49671](https://github.com/nodejs/node/pull/49671)
* \[[`902d8b3d4b`](https://github.com/nodejs/node/commit/902d8b3d4b)] - **test**: fix flaky http-chunk-extensions-limit test (Ethan Arrowood) [#51943](https://github.com/nodejs/node/pull/51943)
* \[[`1743d2bdc1`](https://github.com/nodejs/node/commit/1743d2bdc1)] - **test**: test surrogate pair filenames on windows (Mert Can Altın) [#51800](https://github.com/nodejs/node/pull/51800)
* \[[`1c1a7ec22d`](https://github.com/nodejs/node/commit/1c1a7ec22d)] - **test**: increase platform timeout zlib-brotli-16gb (Rafael Gonzaga) [#51792](https://github.com/nodejs/node/pull/51792)
* \[[`931d02fe3e`](https://github.com/nodejs/node/commit/931d02fe3e)] - **test, v8**: fix wrong import attributes test (Nicolò Ribaudo) [#52184](https://github.com/nodejs/node/pull/52184)
* \[[`d9ea6c1f8d`](https://github.com/nodejs/node/commit/d9ea6c1f8d)] - **tls**: fix order of setting cipher before setting cert and key (Kumar Rishav) [#50186](https://github.com/nodejs/node/pull/50186)
* \[[`3184befa2e`](https://github.com/nodejs/node/commit/3184befa2e)] - **tools**: fix update-icu.sh (Michaël Zasso) [#51723](https://github.com/nodejs/node/pull/51723)
* \[[`06646e11be`](https://github.com/nodejs/node/commit/06646e11be)] - **(SEMVER-MINOR)** **vm**: use import attributes instead of import assertions (Antoine du Hamel) [#50141](https://github.com/nodejs/node/pull/50141)
* \[[`fe66e9d06e`](https://github.com/nodejs/node/commit/fe66e9d06e)] - **vm**: reject in importModuleDynamically without --experimental-vm-modules (Joyee Cheung) [#50137](https://github.com/nodejs/node/pull/50137)
* \[[`052e095c6b`](https://github.com/nodejs/node/commit/052e095c6b)] - **vm**: use internal versions of compileFunction and Script (Joyee Cheung) [#50137](https://github.com/nodejs/node/pull/50137)
* \[[`9f7899ed0a`](https://github.com/nodejs/node/commit/9f7899ed0a)] - **vm**: unify host-defined option generation in vm.compileFunction (Joyee Cheung) [#50137](https://github.com/nodejs/node/pull/50137)
* \[[`6291c107d0`](https://github.com/nodejs/node/commit/6291c107d0)] - **vm**: use default HDO when importModuleDynamically is not set (Joyee Cheung) [#49950](https://github.com/nodejs/node/pull/49950)

<a id="18.19.1"></a>

## 2024-02-14, Version 18.19.1 'Hydrogen' (LTS), @RafaelGSS prepared by @marco-ippolito

### Notable changes

This is a security release.

### Notable changes

* CVE-2024-21892 - Code injection and privilege escalation through Linux capabilities- (High)
* CVE-2024-22019 - http: Reading unprocessed HTTP request with unbounded chunk extension allows DoS attacks- (High)
* CVE-2023-46809 - Node.js is vulnerable to the Marvin Attack (timing variant of the Bleichenbacher attack against PKCS#1 v1.5 padding) - (Medium)
* CVE-2024-22025 - Denial of Service by resource exhaustion in fetch() brotli decoding - (Medium)
* undici version 5.28.3
* npm version 10.2.4

### Commits

* \[[`69e0a1dba8`](https://github.com/nodejs/node/commit/69e0a1dba8)] - **crypto**: update root certificates to NSS 3.95 (Node.js GitHub Bot) [#50805](https://github.com/nodejs/node/pull/50805)
* \[[`d3d357ab09`](https://github.com/nodejs/node/commit/d3d357ab09)] - **crypto**: disable PKCS#1 padding for privateDecrypt (Michael Dawson) [nodejs-private/node-private#525](https://github.com/nodejs-private/node-private/pull/525)
* \[[`3d27175c42`](https://github.com/nodejs/node/commit/3d27175c42)] - **deps**: fix GHSA-f74f-cvh7-c6q6/CVE-2024-24806 (Santiago Gimeno) [#51614](https://github.com/nodejs/node/pull/51614)
* \[[`331558b8ab`](https://github.com/nodejs/node/commit/331558b8ab)] - **deps**: update archs files for openssl-3.0.13+quic1 (Node.js GitHub Bot) [#51614](https://github.com/nodejs/node/pull/51614)
* \[[`99b77dfb9c`](https://github.com/nodejs/node/commit/99b77dfb9c)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.13+quic1 (Node.js GitHub Bot) [#51614](https://github.com/nodejs/node/pull/51614)
* \[[`6cdc71bff1`](https://github.com/nodejs/node/commit/6cdc71bff1)] - **deps**: upgrade npm to 10.2.4 (npm team) [#50751](https://github.com/nodejs/node/pull/50751)
* \[[`911cb33cda`](https://github.com/nodejs/node/commit/911cb33cda)] - **http**: add maximum chunk extension size (Paolo Insogna) [nodejs-private/node-private#520](https://github.com/nodejs-private/node-private/pull/520)
* \[[`f48b89689d`](https://github.com/nodejs/node/commit/f48b89689d)] - **lib**: update undici to v5.28.3 (Matteo Collina) [nodejs-private/node-private#536](https://github.com/nodejs-private/node-private/pull/536)
* \[[`e6b4c105e0`](https://github.com/nodejs/node/commit/e6b4c105e0)] - **src**: fix HasOnly(capability) in node::credentials (Tobias Nießen) [nodejs-private/node-private#505](https://github.com/nodejs-private/node-private/pull/505)
* \[[`97c49076cd`](https://github.com/nodejs/node/commit/97c49076cd)] - **test**: skip test-child-process-stdio-reuse-readable-stdio on Windows (Joyee Cheung) [#49621](https://github.com/nodejs/node/pull/49621)
* \[[`60affdde8e`](https://github.com/nodejs/node/commit/60affdde8e)] - **tools**: add macOS notarization verification step (Ulises Gascón) [#50833](https://github.com/nodejs/node/pull/50833)
* \[[`ccc676a327`](https://github.com/nodejs/node/commit/ccc676a327)] - **tools**: use macOS keychain to notarize the releases (Ulises Gascón) [#50715](https://github.com/nodejs/node/pull/50715)
* \[[`31f1ceb380`](https://github.com/nodejs/node/commit/31f1ceb380)] - **tools**: remove unused file (Ulises Gascon) [#50622](https://github.com/nodejs/node/pull/50622)
* \[[`bd5f6fb92a`](https://github.com/nodejs/node/commit/bd5f6fb92a)] - **tools**: add macOS notarization stapler (Ulises Gascón) [#50625](https://github.com/nodejs/node/pull/50625)
* \[[`4168c4f71b`](https://github.com/nodejs/node/commit/4168c4f71b)] - **tools**: improve macOS notarization process output readability (Ulises Gascón) [#50389](https://github.com/nodejs/node/pull/50389)
* \[[`4622f775aa`](https://github.com/nodejs/node/commit/4622f775aa)] - **tools**: remove unused `version` function (Ulises Gascón) [#50390](https://github.com/nodejs/node/pull/50390)
* \[[`b90804b1e7`](https://github.com/nodejs/node/commit/b90804b1e7)] - **win,tools**: upgrade Windows signing to smctl (Stefan Stojanovic) [#50956](https://github.com/nodejs/node/pull/50956)
* \[[`f31d47e135`](https://github.com/nodejs/node/commit/f31d47e135)] - **zlib**: pause stream if outgoing buffer is full (Matteo Collina) [nodejs-private/node-private#542](https://github.com/nodejs-private/node-private/pull/542)

<a id="18.19.0"></a>

## 2023-11-29, Version 18.19.0 'Hydrogen' (LTS), @targos

### Notable Changes

#### npm updated to v10

After two months of baking time in Node.js 20, npm 10 is backported, so that all
release lines include a supported version of npm. This release includes npm v10.2.3.

Refer to <https://github.com/nodejs/Release/issues/884> for the plan to land npm 10.

#### ESM and customization hook changes

##### Leverage loaders when resolving subsequent loaders

Loaders now apply to subsequent loaders, for example: `--experimental-loader ts-node --experimental-loader loader-written-in-typescript`.

Contributed by Maël Nison in [#43772](https://github.com/nodejs/node/pull/43772).

##### New `node:module` API `register` for module customization hooks; new `initialize` hook

There is a new API `register` available on `node:module` to specify a file that exports module customization hooks, and pass data to the hooks, and establish communication channels with them. The “define the file with the hooks” part was previously handled by a flag `--experimental-loader`, but when the hooks moved into a dedicated thread in 20.0.0 there was a need to provide a way to communicate between the main (application) thread and the hooks thread. This can now be done by calling `register` from the main thread and passing data, including `MessageChannel` instances.

We encourage users to migrate to an approach that uses [`--import`](https://nodejs.org/api/cli.html#--importmodule) with `register`, such as:

```bash
node --import ./file-that-calls-register.js ./app.js
```

Using `--import` ensures that the customization hooks are registered before any application code runs, even the entry point.

Contributed by João Lenon and Jacob Smith in [#46826](https://github.com/nodejs/node/pull/46826), Izaak Schroeder and Jacob Smith in [#48842](https://github.com/nodejs/node/pull/48842) and [#48559](https://github.com/nodejs/node/pull/48559).

##### `import.meta.resolve` unflagged

In ES modules, [`import.meta.resolve(specifier)`](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/import.meta/resolve)
can be used to get an absolute URL string to which `specifier` resolves, similar
to `require.resolve` in CommonJS. This aligns Node.js with browsers and other server-side runtimes.

Contributed by Guy Bedford in [#49028](https://github.com/nodejs/node/pull/49028).

##### `--experimental-default-type` flag to flip module defaults

The new flag `--experimental-default-type` can be used to flip the default
module system used by Node.js. Input that is already explicitly defined as ES
modules or CommonJS, such as by a `package.json` `"type"` field or `.mjs`/`.cjs`
file extension or the `--input-type` flag, is unaffected. What is currently
implicitly CommonJS would instead be interpreted as ES modules under
`--experimental-default-type=module`:

* String input provided via `--eval` or STDIN, if `--input-type` is unspecified.

* Files ending in `.js` or with no extension, if there is no `package.json` file
  present in the same folder or any parent folder.

* Files ending in `.js` or with no extension, if the nearest parent
  `package.json` field lacks a `type` field; unless the folder is inside a
  `node_modules` folder.

In addition, extensionless files are interpreted as Wasm if
`--experimental-wasm-modules` is passed and the file contains the "magic bytes"
Wasm header.

Contributed by Geoffrey Booth in [#49869](https://github.com/nodejs/node/pull/49869).

##### Other ESM-related changes

* \[[`ed2d46f4cc`](https://github.com/nodejs/node/commit/ed2d46f4cc)] - **doc**: move and rename loaders section (Geoffrey Booth) [#49261](https://github.com/nodejs/node/pull/49261)
* \[[`92734d4480`](https://github.com/nodejs/node/commit/92734d4480)] - **esm**: use import attributes instead of import assertions (Antoine du Hamel) [#50140](https://github.com/nodejs/node/pull/50140)
* \[[`e96f7ef881`](https://github.com/nodejs/node/commit/e96f7ef881)] - **(SEMVER-MINOR)** **vm**: use import attributes instead of import assertions (Antoine du Hamel) [#50141](https://github.com/nodejs/node/pull/50141)

#### Test runner changes

Many changes to the built-in test runner have been backported. This includes
the following additions:

* \[[`b283ae4238`](https://github.com/nodejs/node/commit/b283ae4238)] - **(SEMVER-MINOR)** **test\_runner**: accept `testOnly` in `run` (Moshe Atlow) [#49753](https://github.com/nodejs/node/pull/49753)
* \[[`059b1945d8`](https://github.com/nodejs/node/commit/059b1945d8)] - **(SEMVER-MINOR)** **test\_runner**: add junit reporter (Moshe Atlow) [#49614](https://github.com/nodejs/node/pull/49614)
* \[[`d61a505546`](https://github.com/nodejs/node/commit/d61a505546)] - **(SEMVER-MINOR)** **test\_runner**: expose location of tests (Colin Ihrig) [#48975](https://github.com/nodejs/node/pull/48975)
* \[[`b55eb2a8d1`](https://github.com/nodejs/node/commit/b55eb2a8d1)] - **(SEMVER-MINOR)** **test\_runner**: add shards support (Raz Luvaton) [#48639](https://github.com/nodejs/node/pull/48639)
* \[[`05e7f28b40`](https://github.com/nodejs/node/commit/05e7f28b40)] - **(SEMVER-MINOR)** **test\_runner**: add initial draft for fakeTimers (Erick Wendel) [#47775](https://github.com/nodejs/node/pull/47775)
* \[[`428301ad27`](https://github.com/nodejs/node/commit/428301ad27)] - **(SEMVER-MINOR)** **test\_runner, cli**: add --test-concurrency flag (Colin Ihrig) [#49996](https://github.com/nodejs/node/pull/49996)

#### Other notable changes

* \[[`0c4a84e8e9`](https://github.com/nodejs/node/commit/0c4a84e8e9)] - **(SEMVER-MINOR)** **deps**: update uvwasi to 0.0.19 (Node.js GitHub Bot) [#49908](https://github.com/nodejs/node/pull/49908)
* \[[`fae60c5841`](https://github.com/nodejs/node/commit/fae60c5841)] - **stream**: use bitmap in readable state (Benjamin Gruenbaum) [#49745](https://github.com/nodejs/node/pull/49745)
* \[[`17246be158`](https://github.com/nodejs/node/commit/17246be158)] - **(SEMVER-MINOR)** **lib**: add api to detect whether source-maps are enabled (翠 / green) [#46391](https://github.com/nodejs/node/pull/46391)
* \[[`2e9f7284a1`](https://github.com/nodejs/node/commit/2e9f7284a1)] - **(SEMVER-MINOR)** **lib**: add tracing channel to diagnostics\_channel (Stephen Belanger) [#44943](https://github.com/nodejs/node/pull/44943)
* \[[`cc7bf1f641`](https://github.com/nodejs/node/commit/cc7bf1f641)] - **(SEMVER-MINOR)** **src**: add cjs\_module\_lexer\_version base64\_version (Jithil P Ponnan) [#45629](https://github.com/nodejs/node/pull/45629)
* \[[`b5d16cd8f0`](https://github.com/nodejs/node/commit/b5d16cd8f0)] - **(SEMVER-MINOR)** **tls**: add ALPNCallback server option for dynamic ALPN negotiation (Tim Perry) [#45190](https://github.com/nodejs/node/pull/45190)

### Commits

* \[[`0d0eb47e2a`](https://github.com/nodejs/node/commit/0d0eb47e2a)] - **benchmark**: add benchmarks for the test\_runner (Raz Luvaton) [#48931](https://github.com/nodejs/node/pull/48931)
* \[[`8bb03d10f4`](https://github.com/nodejs/node/commit/8bb03d10f4)] - **benchmark**: differentiate whatwg and legacy url (Yagiz Nizipli) [#47377](https://github.com/nodejs/node/pull/47377)
* \[[`3d7734cbe3`](https://github.com/nodejs/node/commit/3d7734cbe3)] - **benchmark**: lower URL.canParse runs (Khafra) [#47351](https://github.com/nodejs/node/pull/47351)
* \[[`24d3fcf415`](https://github.com/nodejs/node/commit/24d3fcf415)] - **benchmark**: stablize encode benchmark (Joyee Cheung) [#46658](https://github.com/nodejs/node/pull/46658)
* \[[`e08fd98bcc`](https://github.com/nodejs/node/commit/e08fd98bcc)] - **bootstrap**: use correct descriptor for Symbol.{dispose,asyncDispose} (Jordan Harband) [#48703](https://github.com/nodejs/node/pull/48703)
* \[[`cf9ddcd6c8`](https://github.com/nodejs/node/commit/cf9ddcd6c8)] - **bootstrap**: simplify initialization of source map handlers (Joyee Cheung) [#48304](https://github.com/nodejs/node/pull/48304)
* \[[`12d731e431`](https://github.com/nodejs/node/commit/12d731e431)] - **bootstrap**: log isolate data info in mksnapshot debug logs (Joyee Cheung) [#47768](https://github.com/nodejs/node/pull/47768)
* \[[`d66873871a`](https://github.com/nodejs/node/commit/d66873871a)] - **bootstrap**: store internal loaders in C++ via a binding (Joyee Cheung) [#47215](https://github.com/nodejs/node/pull/47215)
* \[[`1a499c5082`](https://github.com/nodejs/node/commit/1a499c5082)] - **bootstrap**: optimize modules loaded in the built-in snapshot (Joyee Cheung) [#45849](https://github.com/nodejs/node/pull/45849)
* \[[`e0e09caafe`](https://github.com/nodejs/node/commit/e0e09caafe)] - **bootstrap**: make CJS loader snapshotable (Joyee Cheung) [#45849](https://github.com/nodejs/node/pull/45849)
* \[[`5f37decd56`](https://github.com/nodejs/node/commit/5f37decd56)] - **bootstrap**: include event\_target into the built-in snapshot (Joyee Cheung) [#45849](https://github.com/nodejs/node/pull/45849)
* \[[`c3f56a3dee`](https://github.com/nodejs/node/commit/c3f56a3dee)] - **bootstrap**: support module\_wrap binding in snapshot (Joyee Cheung) [#45849](https://github.com/nodejs/node/pull/45849)
* \[[`6466acbc89`](https://github.com/nodejs/node/commit/6466acbc89)] - **bootstrap**: lazy load non-essential modules (Joyee Cheung) [#45659](https://github.com/nodejs/node/pull/45659)
* \[[`a0d4b69df4`](https://github.com/nodejs/node/commit/a0d4b69df4)] - **bootstrap**: lazy-load Performance.prototype.timeOrigin (Joyee Cheung) [#46425](https://github.com/nodejs/node/pull/46425)
* \[[`c1bc8118e3`](https://github.com/nodejs/node/commit/c1bc8118e3)] - **bootstrap**: generate bootstrapper arguments in BuiltinLoader (Joyee Cheung) [#44488](https://github.com/nodejs/node/pull/44488)
* \[[`075c57e88b`](https://github.com/nodejs/node/commit/075c57e88b)] - **build**: add symlink to `compile_commands.json` file if needed (Juan José) [#49260](https://github.com/nodejs/node/pull/49260)
* \[[`9e1c531b8d`](https://github.com/nodejs/node/commit/9e1c531b8d)] - **build**: expand when we run internet tests (Michael Dawson) [#49218](https://github.com/nodejs/node/pull/49218)
* \[[`a781d24624`](https://github.com/nodejs/node/commit/a781d24624)] - **build**: fix typo `libray` -> `library` (configure.py) (michalbiesek) [#49106](https://github.com/nodejs/node/pull/49106)
* \[[`f2eccb7a04`](https://github.com/nodejs/node/commit/f2eccb7a04)] - **build**: fix `configure --link-module` (Richard Lau) [#48522](https://github.com/nodejs/node/pull/48522)
* \[[`a44d555494`](https://github.com/nodejs/node/commit/a44d555494)] - **build**: fix IBM i build with Python 3.9 (Richard Lau) [#48056](https://github.com/nodejs/node/pull/48056)
* \[[`7f68e14ea2`](https://github.com/nodejs/node/commit/7f68e14ea2)] - **child\_process**: improve spawn performance on Linux (Keyhan Vakil) [#48523](https://github.com/nodejs/node/pull/48523)
* \[[`76e4d4117c`](https://github.com/nodejs/node/commit/76e4d4117c)] - **crypto**: ensure valid point on elliptic curve in SubtleCrypto.importKey (Filip Skokan) [#50234](https://github.com/nodejs/node/pull/50234)
* \[[`7af54279b5`](https://github.com/nodejs/node/commit/7af54279b5)] - **deps**: V8: cherry-pick 70caf337c3f6 (kxxt) [#50506](https://github.com/nodejs/node/pull/50506)
* \[[`49c5495339`](https://github.com/nodejs/node/commit/49c5495339)] - **deps**: update zlib to 1.2.13.1-motley-fef5869 (Node.js GitHub Bot) [#50085](https://github.com/nodejs/node/pull/50085)
* \[[`e0fd52bf07`](https://github.com/nodejs/node/commit/e0fd52bf07)] - **deps**: update googletest to 2dd1c13 (Node.js GitHub Bot) [#50081](https://github.com/nodejs/node/pull/50081)
* \[[`1b103cc567`](https://github.com/nodejs/node/commit/1b103cc567)] - **deps**: update googletest to e47544a (Node.js GitHub Bot) [#49982](https://github.com/nodejs/node/pull/49982)
* \[[`736c869eeb`](https://github.com/nodejs/node/commit/736c869eeb)] - **deps**: update googletest to d1467f5 (Node.js GitHub Bot) [#49676](https://github.com/nodejs/node/pull/49676)
* \[[`cd99ee1f35`](https://github.com/nodejs/node/commit/cd99ee1f35)] - **deps**: update googletest to 8a6feab (Node.js GitHub Bot) [#49463](https://github.com/nodejs/node/pull/49463)
* \[[`5c338573ff`](https://github.com/nodejs/node/commit/5c338573ff)] - **deps**: update zlib to 1.2.13.1-motley-f5fd0ad (Node.js GitHub Bot) [#49252](https://github.com/nodejs/node/pull/49252)
* \[[`374ec3d623`](https://github.com/nodejs/node/commit/374ec3d623)] - **deps**: update googletest to 7e33b6a (Node.js GitHub Bot) [#49034](https://github.com/nodejs/node/pull/49034)
* \[[`c15dd6679b`](https://github.com/nodejs/node/commit/c15dd6679b)] - **deps**: update zlib to 1.2.13.1-motley-526382e (Node.js GitHub Bot) [#49033](https://github.com/nodejs/node/pull/49033)
* \[[`588bd5e524`](https://github.com/nodejs/node/commit/588bd5e524)] - **deps**: update googletest to c875c4e (Node.js GitHub Bot) [#48964](https://github.com/nodejs/node/pull/48964)
* \[[`6059b59018`](https://github.com/nodejs/node/commit/6059b59018)] - **deps**: update zlib to 1.2.13.1-motley-61dc0bd (Node.js GitHub Bot) [#48788](https://github.com/nodejs/node/pull/48788)
* \[[`e455dd4003`](https://github.com/nodejs/node/commit/e455dd4003)] - **deps**: update googletest to cc36671 (Node.js GitHub Bot) [#48789](https://github.com/nodejs/node/pull/48789)
* \[[`747fbb49ca`](https://github.com/nodejs/node/commit/747fbb49ca)] - **deps**: V8: cherry-pick 1a782f6543ae (Keyhan Vakil) [#48523](https://github.com/nodejs/node/pull/48523)
* \[[`272e55c66f`](https://github.com/nodejs/node/commit/272e55c66f)] - **deps**: upgrade npm to 10.2.3 (npm team) [#50531](https://github.com/nodejs/node/pull/50531)
* \[[`3f6dcc62e5`](https://github.com/nodejs/node/commit/3f6dcc62e5)] - **deps**: update archs files for openssl-3.0.12+quic1 (Node.js GitHub Bot) [#50411](https://github.com/nodejs/node/pull/50411)
* \[[`da26cdbe84`](https://github.com/nodejs/node/commit/da26cdbe84)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.12+quic1 (Node.js GitHub Bot) [#50411](https://github.com/nodejs/node/pull/50411)
* \[[`23069c34b2`](https://github.com/nodejs/node/commit/23069c34b2)] - **deps**: V8: cherry-pick d69c7937c99d (Michaël Zasso) [#46425](https://github.com/nodejs/node/pull/46425)
* \[[`5f852cc9fe`](https://github.com/nodejs/node/commit/5f852cc9fe)] - **deps**: V8: cherry-pick f7d000a7ae7b (Luke Albao) [#50344](https://github.com/nodejs/node/pull/50344)
* \[[`0c4a84e8e9`](https://github.com/nodejs/node/commit/0c4a84e8e9)] - **(SEMVER-MINOR)** **deps**: update uvwasi to 0.0.19 (Node.js GitHub Bot) [#49908](https://github.com/nodejs/node/pull/49908)
* \[[`8682b90d02`](https://github.com/nodejs/node/commit/8682b90d02)] - **deps**: update simdutf to 3.2.18 (Node.js GitHub Bot) [#50091](https://github.com/nodejs/node/pull/50091)
* \[[`11ecd06aeb`](https://github.com/nodejs/node/commit/11ecd06aeb)] - **deps**: update simdutf to 3.2.17 (Node.js GitHub Bot) [#49019](https://github.com/nodejs/node/pull/49019)
* \[[`43bfe5f020`](https://github.com/nodejs/node/commit/43bfe5f020)] - **deps**: upgrade npm to 10.2.0 (npm team) [#50027](https://github.com/nodejs/node/pull/50027)
* \[[`a140bc284b`](https://github.com/nodejs/node/commit/a140bc284b)] - **deps**: upgrade npm to 10.1.0 (npm team) [#49570](https://github.com/nodejs/node/pull/49570)
* \[[`65ca41c276`](https://github.com/nodejs/node/commit/65ca41c276)] - **(SEMVER-MINOR)** **deps**: upgrade npm to 10.0.0 (npm team) [#49423](https://github.com/nodejs/node/pull/49423)
* \[[`df1ff8e3da`](https://github.com/nodejs/node/commit/df1ff8e3da)] - **deps**: fix call to undeclared functions 'ntohl' and 'htons' (MatteoBax) [#49979](https://github.com/nodejs/node/pull/49979)
* \[[`f228dc7955`](https://github.com/nodejs/node/commit/f228dc7955)] - **deps**: update corepack to 0.22.0 (Node.js GitHub Bot) [#50325](https://github.com/nodejs/node/pull/50325)
* \[[`4324ebab67`](https://github.com/nodejs/node/commit/4324ebab67)] - **deps**: update corepack to 0.21.0 (Node.js GitHub Bot) [#50088](https://github.com/nodejs/node/pull/50088)
* \[[`1cabb77659`](https://github.com/nodejs/node/commit/1cabb77659)] - **deps**: update corepack to 0.20.0 (Node.js GitHub Bot) [#49464](https://github.com/nodejs/node/pull/49464)
* \[[`04227b287e`](https://github.com/nodejs/node/commit/04227b287e)] - **deps**: update c-ares to 1.20.1 (Node.js GitHub Bot) [#50082](https://github.com/nodejs/node/pull/50082)
* \[[`13e69ee11c`](https://github.com/nodejs/node/commit/13e69ee11c)] - **deps**: update c-ares to 1.20.0 (Node.js GitHub Bot) [#50082](https://github.com/nodejs/node/pull/50082)
* \[[`ac717df17e`](https://github.com/nodejs/node/commit/ac717df17e)] - **deps**: update ada to 2.7.2 (Node.js GitHub Bot) [#50338](https://github.com/nodejs/node/pull/50338)
* \[[`6885fc9386`](https://github.com/nodejs/node/commit/6885fc9386)] - **deps**: update ada to 2.6.10 (Node.js GitHub Bot) [#49984](https://github.com/nodejs/node/pull/49984)
* \[[`76c5f4039f`](https://github.com/nodejs/node/commit/76c5f4039f)] - **deps**: update ada to 2.6.9 (Node.js GitHub Bot) [#49340](https://github.com/nodejs/node/pull/49340)
* \[[`597ea77422`](https://github.com/nodejs/node/commit/597ea77422)] - **deps**: update ada to 2.6.8 (Node.js GitHub Bot) [#49340](https://github.com/nodejs/node/pull/49340)
* \[[`8e7dcba553`](https://github.com/nodejs/node/commit/8e7dcba553)] - **deps**: update ada to 2.6.7 (Node.js GitHub Bot) [#49340](https://github.com/nodejs/node/pull/49340)
* \[[`9f2037e8ee`](https://github.com/nodejs/node/commit/9f2037e8ee)] - **deps**: update ada to 2.6.5 (Node.js GitHub Bot) [#49340](https://github.com/nodejs/node/pull/49340)
* \[[`4723976703`](https://github.com/nodejs/node/commit/4723976703)] - **deps**: update ada to 2.6.3 (Node.js GitHub Bot) [#49340](https://github.com/nodejs/node/pull/49340)
* \[[`7ccb478664`](https://github.com/nodejs/node/commit/7ccb478664)] - **deps**: update undici to 5.26.4 (Node.js GitHub Bot) [#50274](https://github.com/nodejs/node/pull/50274)
* \[[`88f9ebb770`](https://github.com/nodejs/node/commit/88f9ebb770)] - **diagnostics\_channel**: fix ref counting bug when reaching zero subscribers (Stephen Belanger) [#47520](https://github.com/nodejs/node/pull/47520)
* \[[`284a869540`](https://github.com/nodejs/node/commit/284a869540)] - **dns**: call `ada::idna::to_ascii` directly from c++ (Yagiz Nizipli) [#47920](https://github.com/nodejs/node/pull/47920)
* \[[`10968370b5`](https://github.com/nodejs/node/commit/10968370b5)] - **doc**: drop github actions check in sec release process (Rafael Gonzaga) [#48978](https://github.com/nodejs/node/pull/48978)
* \[[`07c3b88c74`](https://github.com/nodejs/node/commit/07c3b88c74)] - **doc**: remove `@anonrig` from performance initiative (Yagiz Nizipli) [#49641](https://github.com/nodejs/node/pull/49641)
* \[[`e26b89e8be`](https://github.com/nodejs/node/commit/e26b89e8be)] - **doc**: fix node-api call example (Chengzhong Wu) [#49395](https://github.com/nodejs/node/pull/49395)
* \[[`4c93905f6c`](https://github.com/nodejs/node/commit/4c93905f6c)] - **doc**: add news issue for Diagnostics WG (Michael Dawson) [#49306](https://github.com/nodejs/node/pull/49306)
* \[[`3f1a237a8f`](https://github.com/nodejs/node/commit/3f1a237a8f)] - **doc**: add print results for examples in `StringDecoder` (Jungku Lee) [#49326](https://github.com/nodejs/node/pull/49326)
* \[[`45caad82bb`](https://github.com/nodejs/node/commit/45caad82bb)] - **doc**: update outdated reference to NIST SP 800-131A (Tobias Nießen) [#49316](https://github.com/nodejs/node/pull/49316)
* \[[`62f823d5a2`](https://github.com/nodejs/node/commit/62f823d5a2)] - **doc**: use `cjs` as block code's type in `MockTimers` (Deokjin Kim) [#49309](https://github.com/nodejs/node/pull/49309)
* \[[`0dda724d3f`](https://github.com/nodejs/node/commit/0dda724d3f)] - **doc**: update `options.filter` description for `fs.cp` (Shubham Pandey) [#49289](https://github.com/nodejs/node/pull/49289)
* \[[`4ba11e352b`](https://github.com/nodejs/node/commit/4ba11e352b)] - **doc**: avoid "not currently recommended" (Tobias Nießen) [#49300](https://github.com/nodejs/node/pull/49300)
* \[[`9ca85b58b3`](https://github.com/nodejs/node/commit/9ca85b58b3)] - **doc**: modify param description for end(),write() in `StringDecoder` (Jungku Lee) [#49285](https://github.com/nodejs/node/pull/49285)
* \[[`3f771cab67`](https://github.com/nodejs/node/commit/3f771cab67)] - **doc**: use NODE\_API\_SUPPORTED\_VERSION\_MAX in release doc (Cheng Zhao) [#49268](https://github.com/nodejs/node/pull/49268)
* \[[`f181c37e75`](https://github.com/nodejs/node/commit/f181c37e75)] - **doc**: fix typo in `stream.finished` documentation (Antoine du Hamel) [#49271](https://github.com/nodejs/node/pull/49271)
* \[[`c70945ddc2`](https://github.com/nodejs/node/commit/c70945ddc2)] - **doc**: update description for `percent_encode` sets in `WHATWG API` (Jungku Lee) [#49258](https://github.com/nodejs/node/pull/49258)
* \[[`f9c2a3fb3e`](https://github.com/nodejs/node/commit/f9c2a3fb3e)] - **doc**: clarify use of Uint8Array for n-api (Fedor Indutny) [#48742](https://github.com/nodejs/node/pull/48742)
* \[[`bf22a5f66e`](https://github.com/nodejs/node/commit/bf22a5f66e)] - **doc**: use same name in the doc as in the code (Hyunjin Kim) [#49216](https://github.com/nodejs/node/pull/49216)
* \[[`72bd527fb6`](https://github.com/nodejs/node/commit/72bd527fb6)] - **doc**: add notable-change label mention to PR template (Rafael Gonzaga) [#49188](https://github.com/nodejs/node/pull/49188)
* \[[`2247e52fe0`](https://github.com/nodejs/node/commit/2247e52fe0)] - **doc**: add h1 summary to security release process (Rafael Gonzaga) [#49112](https://github.com/nodejs/node/pull/49112)
* \[[`3b82e9aed1`](https://github.com/nodejs/node/commit/3b82e9aed1)] - **doc**: fix wording in napi\_async\_init (Tobias Nießen) [#49180](https://github.com/nodejs/node/pull/49180)
* \[[`55171d88a0`](https://github.com/nodejs/node/commit/55171d88a0)] - **doc**: fix `Type` notation in webstreams (Deokjin Kim) [#49121](https://github.com/nodejs/node/pull/49121)
* \[[`79c0497398`](https://github.com/nodejs/node/commit/79c0497398)] - **doc**: make the NODE\_VERSION\_IS\_RELEASE revert clear (Rafael Gonzaga) [#49114](https://github.com/nodejs/node/pull/49114)
* \[[`7ee26fb8df`](https://github.com/nodejs/node/commit/7ee26fb8df)] - **doc**: update with latest security release (Rafael Gonzaga) [#49085](https://github.com/nodejs/node/pull/49085)
* \[[`9ce73964be`](https://github.com/nodejs/node/commit/9ce73964be)] - **doc**: add description for `--port` flag of `node inspect` (Michael Bianco) [#48785](https://github.com/nodejs/node/pull/48785)
* \[[`633b8cd181`](https://github.com/nodejs/node/commit/633b8cd181)] - **doc**: add missing period (Rich Trott) [#49094](https://github.com/nodejs/node/pull/49094)
* \[[`6daa9ec2a4`](https://github.com/nodejs/node/commit/6daa9ec2a4)] - **doc**: add ESM examples in http.md (btea) [#47763](https://github.com/nodejs/node/pull/47763)
* \[[`12b83e81b9`](https://github.com/nodejs/node/commit/12b83e81b9)] - **doc**: detailed description of keystrokes Ctrl-Y and Meta-Y (Ray) [#43529](https://github.com/nodejs/node/pull/43529)
* \[[`ead654f976`](https://github.com/nodejs/node/commit/ead654f976)] - **doc**: clarify use of process.env in worker threads on Windows (Daeyeon Jeong) [#49008](https://github.com/nodejs/node/pull/49008)
* \[[`4047947838`](https://github.com/nodejs/node/commit/4047947838)] - **doc**: remove v14 mention (Rafael Gonzaga) [#49005](https://github.com/nodejs/node/pull/49005)
* \[[`833c643eb4`](https://github.com/nodejs/node/commit/833c643eb4)] - **doc**: run license-builder (github-actions\[bot]) [#48898](https://github.com/nodejs/node/pull/48898)
* \[[`cb43717c97`](https://github.com/nodejs/node/commit/cb43717c97)] - **doc**: add ver of 18.x where Node-api 9 is supported (Michael Dawson) [#48876](https://github.com/nodejs/node/pull/48876)
* \[[`a8d5c16a2a`](https://github.com/nodejs/node/commit/a8d5c16a2a)] - **doc**: include experimental features assessment (Rafael Gonzaga) [#48824](https://github.com/nodejs/node/pull/48824)
* \[[`e6d8735e2b`](https://github.com/nodejs/node/commit/e6d8735e2b)] - **doc**: add new TSC members (Michael Dawson) [#48841](https://github.com/nodejs/node/pull/48841)
* \[[`d4fe00d0c7`](https://github.com/nodejs/node/commit/d4fe00d0c7)] - **doc**: refactor node-api support matrix (Michael Dawson) [#48774](https://github.com/nodejs/node/pull/48774)
* \[[`629132d84c`](https://github.com/nodejs/node/commit/629132d84c)] - **doc**: declare `path` on example of `async_hooks.executionAsyncId()` (Deokjin Kim) [#48556](https://github.com/nodejs/node/pull/48556)
* \[[`dfd368ac9f`](https://github.com/nodejs/node/commit/dfd368ac9f)] - **doc**: remove the . in the end to reduce confusing (Jason) [#48719](https://github.com/nodejs/node/pull/48719)
* \[[`74d8f96413`](https://github.com/nodejs/node/commit/74d8f96413)] - **doc**: nodejs-social over nodejs/tweet (Rafael Gonzaga) [#48769](https://github.com/nodejs/node/pull/48769)
* \[[`73a7e00d06`](https://github.com/nodejs/node/commit/73a7e00d06)] - **doc**: add missing history info for `import.meta.resolve` (Antoine du Hamel) [#49700](https://github.com/nodejs/node/pull/49700)
* \[[`c20fdb4e52`](https://github.com/nodejs/node/commit/c20fdb4e52)] - **doc**: edit `import.meta.resolve` documentation (Antoine du Hamel) [#49247](https://github.com/nodejs/node/pull/49247)
* \[[`1ac389ecef`](https://github.com/nodejs/node/commit/1ac389ecef)] - **doc**: update module hooks docs (Geoffrey Booth) [#49265](https://github.com/nodejs/node/pull/49265)
* \[[`ed2d46f4cc`](https://github.com/nodejs/node/commit/ed2d46f4cc)] - **doc**: move and rename loaders section (Geoffrey Booth) [#49261](https://github.com/nodejs/node/pull/49261)
* \[[`258df0e72d`](https://github.com/nodejs/node/commit/258df0e72d)] - **doc**: add signature for `module.register` (Geoffrey Booth) [#49251](https://github.com/nodejs/node/pull/49251)
* \[[`58eaf3f6ae`](https://github.com/nodejs/node/commit/58eaf3f6ae)] - **doc**: caveat unavailability of `import.meta.resolve` in custom loaders (Jacob Smith) [#49242](https://github.com/nodejs/node/pull/49242)
* \[[`2fef28b2b9`](https://github.com/nodejs/node/commit/2fef28b2b9)] - **doc**: fix name of the flag in `initialize()` docs (Antoine du Hamel) [#49158](https://github.com/nodejs/node/pull/49158)
* \[[`15280fb42c`](https://github.com/nodejs/node/commit/15280fb42c)] - **doc**: add steps about signing the binary in single-executable docs (Darshan Sen) [#46764](https://github.com/nodejs/node/pull/46764)
* \[[`e374ba296c`](https://github.com/nodejs/node/commit/e374ba296c)] - **doc**: add "type" to test runner event details (Phil Nash) [#49014](https://github.com/nodejs/node/pull/49014)
* \[[`ec0a6c1f1b`](https://github.com/nodejs/node/commit/ec0a6c1f1b)] - **doc**: add new reporter events to custom reporter examples (Chemi Atlow) [#48903](https://github.com/nodejs/node/pull/48903)
* \[[`e8a32fb49b`](https://github.com/nodejs/node/commit/e8a32fb49b)] - **doc**: change duration to duration\_ms on test documentation (Ardi\_Nugraha) [#48892](https://github.com/nodejs/node/pull/48892)
* \[[`2b30c8b8a3`](https://github.com/nodejs/node/commit/2b30c8b8a3)] - **doc**: fix `globalPreload` example (bmacnaughton) [#50300](https://github.com/nodejs/node/pull/50300)
* \[[`8a57182769`](https://github.com/nodejs/node/commit/8a57182769)] - **doc,test**: extend the list of platforms supported by single-executables (Darshan Sen) [#47026](https://github.com/nodejs/node/pull/47026)
* \[[`92734d4480`](https://github.com/nodejs/node/commit/92734d4480)] - **esm**: use import attributes instead of import assertions (Antoine du Hamel) [#50140](https://github.com/nodejs/node/pull/50140)
* \[[`c3c945e654`](https://github.com/nodejs/node/commit/c3c945e654)] - **esm**: bypass CommonJS loader under --default-type (Geoffrey Booth) [#49986](https://github.com/nodejs/node/pull/49986)
* \[[`fe691984b2`](https://github.com/nodejs/node/commit/fe691984b2)] - **esm**: unflag extensionless javascript and wasm in module scope (Geoffrey Booth) [#49974](https://github.com/nodejs/node/pull/49974)
* \[[`56bd9a88ac`](https://github.com/nodejs/node/commit/56bd9a88ac)] - **esm**: --experimental-default-type flag to flip module defaults (Geoffrey Booth) [#49869](https://github.com/nodejs/node/pull/49869)
* \[[`72644d62e7`](https://github.com/nodejs/node/commit/72644d62e7)] - **esm**: improve JSDoc annotation of internal functions (Antoine du Hamel) [#49959](https://github.com/nodejs/node/pull/49959)
* \[[`957725f601`](https://github.com/nodejs/node/commit/957725f601)] - **esm**: require braces for modules code (Geoffrey Booth) [#49657](https://github.com/nodejs/node/pull/49657)
* \[[`c12685f82d`](https://github.com/nodejs/node/commit/c12685f82d)] - **esm**: fix cache collision on JSON files using file: URL (Antoine du Hamel) [#49887](https://github.com/nodejs/node/pull/49887)
* \[[`ed8dd33493`](https://github.com/nodejs/node/commit/ed8dd33493)] - **esm**: identify parent importing a url with invalid host (Jacob Smith) [#49736](https://github.com/nodejs/node/pull/49736)
* \[[`46d730ab75`](https://github.com/nodejs/node/commit/46d730ab75)] - **esm**: fix return type of `import.meta.resolve` (Antoine du Hamel) [#49698](https://github.com/nodejs/node/pull/49698)
* \[[`12cb700478`](https://github.com/nodejs/node/commit/12cb700478)] - **esm**: update loaders warning (Geoffrey Booth) [#49633](https://github.com/nodejs/node/pull/49633)
* \[[`47193a347e`](https://github.com/nodejs/node/commit/47193a347e)] - **esm**: fix support for `URL` instances in `register` (Antoine du Hamel) [#49655](https://github.com/nodejs/node/pull/49655)
* \[[`51ced0f1a1`](https://github.com/nodejs/node/commit/51ced0f1a1)] - **esm**: clarify ERR\_REQUIRE\_ESM errors (Daniel Compton) [#49521](https://github.com/nodejs/node/pull/49521)
* \[[`4be5612bae`](https://github.com/nodejs/node/commit/4be5612bae)] - **esm**: remove return value for `Module.register` (Antoine du Hamel) [#49529](https://github.com/nodejs/node/pull/49529)
* \[[`0875867e27`](https://github.com/nodejs/node/commit/0875867e27)] - **esm**: refactor test-esm-loader-resolve-type (Geoffrey Booth) [#49493](https://github.com/nodejs/node/pull/49493)
* \[[`9b7c9d93e9`](https://github.com/nodejs/node/commit/9b7c9d93e9)] - **esm**: refactor test-esm-named-exports (Geoffrey Booth) [#49493](https://github.com/nodejs/node/pull/49493)
* \[[`d1f5514917`](https://github.com/nodejs/node/commit/d1f5514917)] - **esm**: refactor mocking test (Geoffrey Booth) [#49465](https://github.com/nodejs/node/pull/49465)
* \[[`01ca6d277d`](https://github.com/nodejs/node/commit/01ca6d277d)] - **esm**: fix `globalPreload` warning (Antoine du Hamel) [#49069](https://github.com/nodejs/node/pull/49069)
* \[[`c1a84398b4`](https://github.com/nodejs/node/commit/c1a84398b4)] - **esm**: unflag import.meta.resolve (Guy Bedford) [#49028](https://github.com/nodejs/node/pull/49028)
* \[[`ef43f084e9`](https://github.com/nodejs/node/commit/ef43f084e9)] - **esm**: import.meta.resolve exact module not found errors should return (Guy Bedford) [#49038](https://github.com/nodejs/node/pull/49038)
* \[[`385f24c9cc`](https://github.com/nodejs/node/commit/385f24c9cc)] - **esm**: protect `ERR_UNSUPPORTED_DIR_IMPORT` against prototype pollution (Antoine du Hamel) [#49060](https://github.com/nodejs/node/pull/49060)
* \[[`10e7c3a376`](https://github.com/nodejs/node/commit/10e7c3a376)] - **esm**: add `initialize` hook, integrate with `register` (Izaak Schroeder) [#48842](https://github.com/nodejs/node/pull/48842)
* \[[`f96b610268`](https://github.com/nodejs/node/commit/f96b610268)] - **esm**: fix typo `parentUrl` -> `parentURL` (Antoine du Hamel) [#48999](https://github.com/nodejs/node/pull/48999)
* \[[`03c1b5e647`](https://github.com/nodejs/node/commit/03c1b5e647)] - **esm**: unflag `Module.register` and allow nested loader `import()` (Izaak Schroeder) [#48559](https://github.com/nodejs/node/pull/48559)
* \[[`63aa5d7270`](https://github.com/nodejs/node/commit/63aa5d7270)] - **esm**: add back `globalPreload` tests and fix failing ones (Antoine du Hamel) [#48779](https://github.com/nodejs/node/pull/48779)
* \[[`1c7be606f1`](https://github.com/nodejs/node/commit/1c7be606f1)] - **esm**: remove support for arrays in `import` internal method (Antoine du Hamel) [#48296](https://github.com/nodejs/node/pull/48296)
* \[[`655111fa00`](https://github.com/nodejs/node/commit/655111fa00)] - **esm**: handle `globalPreload` hook returning a nullish value (Antoine du Hamel) [#48249](https://github.com/nodejs/node/pull/48249)
* \[[`9938a8bf13`](https://github.com/nodejs/node/commit/9938a8bf13)] - **esm**: handle more error types thrown from the loader thread (Antoine du Hamel) [#48247](https://github.com/nodejs/node/pull/48247)
* \[[`8cab32a5d1`](https://github.com/nodejs/node/commit/8cab32a5d1)] - **esm**: do not use `'beforeExit'` on the main thread (Antoine du Hamel) [#47964](https://github.com/nodejs/node/pull/47964)
* \[[`b61efcce95`](https://github.com/nodejs/node/commit/b61efcce95)] - **esm**: rename `URLCanParse` to be consistent (Antoine du Hamel) [#47668](https://github.com/nodejs/node/pull/47668)
* \[[`ca20f5931d`](https://github.com/nodejs/node/commit/ca20f5931d)] - **esm**: remove support for deprecated hooks (Antoine du Hamel) [#47580](https://github.com/nodejs/node/pull/47580)
* \[[`5de37a1e37`](https://github.com/nodejs/node/commit/5de37a1e37)] - **esm**: initialize `import.meta` on eval (Antoine du Hamel) [#47551](https://github.com/nodejs/node/pull/47551)
* \[[`39fbce7313`](https://github.com/nodejs/node/commit/39fbce7313)] - **esm**: propagate `process.exit` from the loader thread to the main thread (Antoine du Hamel) [#47548](https://github.com/nodejs/node/pull/47548)
* \[[`2a528b76e6`](https://github.com/nodejs/node/commit/2a528b76e6)] - **esm**: avoid try/catch when validating urls (Yagiz Nizipli) [#47541](https://github.com/nodejs/node/pull/47541)
* \[[`bac9b1758f`](https://github.com/nodejs/node/commit/bac9b1758f)] - **esm**: move hook execution to separate thread (Jacob Smith) [#44710](https://github.com/nodejs/node/pull/44710)
* \[[`dfa444477a`](https://github.com/nodejs/node/commit/dfa444477a)] - **esm**: skip file: URL conversion to path when possible (Antoine du Hamel) [#46305](https://github.com/nodejs/node/pull/46305)
* \[[`45de8d1bd7`](https://github.com/nodejs/node/commit/45de8d1bd7)] - **esm**: allow resolve to return import assertions (Geoffrey Booth) [#46153](https://github.com/nodejs/node/pull/46153)
* \[[`5ffc90a06b`](https://github.com/nodejs/node/commit/5ffc90a06b)] - **esm**: move hooks handling into separate class (Geoffrey Booth) [#45869](https://github.com/nodejs/node/pull/45869)
* \[[`490b598dbf`](https://github.com/nodejs/node/commit/490b598dbf)] - **esm**: leverage loaders when resolving subsequent loaders (Maël Nison) [#43772](https://github.com/nodejs/node/pull/43772)
* \[[`acd987287c`](https://github.com/nodejs/node/commit/acd987287c)] - **events**: remove weak listener for event target (Raz Luvaton) [#48952](https://github.com/nodejs/node/pull/48952)
* \[[`69b7f91a92`](https://github.com/nodejs/node/commit/69b7f91a92)] - **fs**: remove redundant code in readableWebStream() (Deokjin Kim) [#49298](https://github.com/nodejs/node/pull/49298)
* \[[`ae8bb162b4`](https://github.com/nodejs/node/commit/ae8bb162b4)] - **fs**: remove redundant `nullCheck` (Livia Medeiros) [#48826](https://github.com/nodejs/node/pull/48826)
* \[[`48c25b154b`](https://github.com/nodejs/node/commit/48c25b154b)] - **fs**: make `mkdtemp` accept buffers and URL (LiviaMedeiros) [#48828](https://github.com/nodejs/node/pull/48828)
* \[[`edf46c1b59`](https://github.com/nodejs/node/commit/edf46c1b59)] - **fs**: move fs\_use\_promises\_symbol to per-isolate symbols (Joyee Cheung) [#47768](https://github.com/nodejs/node/pull/47768)
* \[[`fe41d22afc`](https://github.com/nodejs/node/commit/fe41d22afc)] - **fs**: use kResistStopPropagation (Chemi Atlow) [#48521](https://github.com/nodejs/node/pull/48521)
* \[[`7c758f60ab`](https://github.com/nodejs/node/commit/7c758f60ab)] - **fs**: fix readdir recursive sync & callback (Ethan Arrowood) [#48698](https://github.com/nodejs/node/pull/48698)
* \[[`8874b2e11d`](https://github.com/nodejs/node/commit/8874b2e11d)] - **http**: start connections checking interval on listen (Paolo Insogna) [#48611](https://github.com/nodejs/node/pull/48611)
* \[[`29697229b6`](https://github.com/nodejs/node/commit/29697229b6)] - **https**: fix connection checking interval not clearing on server close (Nitzan Uziely) [#48383](https://github.com/nodejs/node/pull/48383)
* \[[`981aa7866d`](https://github.com/nodejs/node/commit/981aa7866d)] - **lib**: fix MIME overmatch in data URLs (André Alves) [#49104](https://github.com/nodejs/node/pull/49104)
* \[[`fe26f8a860`](https://github.com/nodejs/node/commit/fe26f8a860)] - **lib**: merge cjs and esm package json reader caches (Yagiz Nizipli) [#48477](https://github.com/nodejs/node/pull/48477)
* \[[`17246be158`](https://github.com/nodejs/node/commit/17246be158)] - **(SEMVER-MINOR)** **lib**: add api to detect whether source-maps are enabled (翠 / green) [#46391](https://github.com/nodejs/node/pull/46391)
* \[[`2e9f7284a1`](https://github.com/nodejs/node/commit/2e9f7284a1)] - **(SEMVER-MINOR)** **lib**: add tracing channel to diagnostics\_channel (Stephen Belanger) [#44943](https://github.com/nodejs/node/pull/44943)
* \[[`04dad9c2f6`](https://github.com/nodejs/node/commit/04dad9c2f6)] - **lib**: fix BroadcastChannel initialization location (Shelley Vohr) [#46864](https://github.com/nodejs/node/pull/46864)
* \[[`671d2c0067`](https://github.com/nodejs/node/commit/671d2c0067)] - **lib**: fix DOMException property descriptors after being lazy loaded (Filip Skokan) [#46799](https://github.com/nodejs/node/pull/46799)
* \[[`9a4b57d6d4`](https://github.com/nodejs/node/commit/9a4b57d6d4)] - **lib**: improve esm resolve performance (Yagiz Nizipli) [#46652](https://github.com/nodejs/node/pull/46652)
* \[[`c6b2f56723`](https://github.com/nodejs/node/commit/c6b2f56723)] - **lib**: lazy-load deps in modules/run\_main.js (Joyee Cheung) [#45849](https://github.com/nodejs/node/pull/45849)
* \[[`baa280a4f6`](https://github.com/nodejs/node/commit/baa280a4f6)] - **lib**: lazy-load deps in source\_map\_cache.js (Joyee Cheung) [#45849](https://github.com/nodejs/node/pull/45849)
* \[[`00bdb72b6e`](https://github.com/nodejs/node/commit/00bdb72b6e)] - **lib**: add getLazy() method to internal/util (Joyee Cheung) [#45849](https://github.com/nodejs/node/pull/45849)
* \[[`b77a6b2376`](https://github.com/nodejs/node/commit/b77a6b2376)] - **loader**: use default loader as cascaded loader in the in loader worker (Joyee Cheung) [#47620](https://github.com/nodejs/node/pull/47620)
* \[[`7778e190b0`](https://github.com/nodejs/node/commit/7778e190b0)] - **meta**: move Trott to TSC regular member (Rich Trott) [#50297](https://github.com/nodejs/node/pull/50297)
* \[[`5b9575dd30`](https://github.com/nodejs/node/commit/5b9575dd30)] - **meta**: ping TSC for offboarding (Tobias Nießen) [#50147](https://github.com/nodejs/node/pull/50147)
* \[[`d24c3a0692`](https://github.com/nodejs/node/commit/d24c3a0692)] - **meta**: update website team with new name (Rich Trott) [#49883](https://github.com/nodejs/node/pull/49883)
* \[[`332d2aedb4`](https://github.com/nodejs/node/commit/332d2aedb4)] - **meta**: fix linter error (Antoine du Hamel) [#49755](https://github.com/nodejs/node/pull/49755)
* \[[`dc70c444f4`](https://github.com/nodejs/node/commit/dc70c444f4)] - **meta**: add primordials strategic initiative (Benjamin Gruenbaum) [#49706](https://github.com/nodejs/node/pull/49706)
* \[[`213a9f8a4b`](https://github.com/nodejs/node/commit/213a9f8a4b)] - **meta**: bump rtCamp/action-slack-notify from 2.2.0 to 2.2.1 (dependabot\[bot]) [#49437](https://github.com/nodejs/node/pull/49437)
* \[[`2827779faa`](https://github.com/nodejs/node/commit/2827779faa)] - **meta**: remove modules team from CODEOWNERS (Benjamin Gruenbaum) [#49412](https://github.com/nodejs/node/pull/49412)
* \[[`5d6daf0d01`](https://github.com/nodejs/node/commit/5d6daf0d01)] - **meta**: add test/reporters to codeowners (Chemi Atlow) [#49186](https://github.com/nodejs/node/pull/49186)
* \[[`dee7dc5d54`](https://github.com/nodejs/node/commit/dee7dc5d54)] - **meta**: bump actions/upload-artifact from 3.1.2 to 3.1.3 (dependabot\[bot]) [#50000](https://github.com/nodejs/node/pull/50000)
* \[[`98294fdeee`](https://github.com/nodejs/node/commit/98294fdeee)] - **meta**: bump actions/cache from 3.3.1 to 3.3.2 (dependabot\[bot]) [#50003](https://github.com/nodejs/node/pull/50003)
* \[[`79270327d0`](https://github.com/nodejs/node/commit/79270327d0)] - **meta**: bump github/codeql-action from 2.21.5 to 2.21.9 (dependabot\[bot]) [#50002](https://github.com/nodejs/node/pull/50002)
* \[[`6591a03c89`](https://github.com/nodejs/node/commit/6591a03c89)] - **meta**: bump github/codeql-action from 2.21.2 to 2.21.5 (dependabot\[bot]) [#49438](https://github.com/nodejs/node/pull/49438)
* \[[`3a107f80bc`](https://github.com/nodejs/node/commit/3a107f80bc)] - **meta**: bump actions/checkout from 3.6.0 to 4.1.0 (dependabot\[bot]) [#50001](https://github.com/nodejs/node/pull/50001)
* \[[`26c5c3c4a2`](https://github.com/nodejs/node/commit/26c5c3c4a2)] - **meta**: bump actions/checkout from 3.5.3 to 3.6.0 (dependabot\[bot]) [#49436](https://github.com/nodejs/node/pull/49436)
* \[[`0fc0b88d97`](https://github.com/nodejs/node/commit/0fc0b88d97)] - **meta**: bump step-security/harden-runner from 2.5.0 to 2.5.1 (dependabot\[bot]) [#49435](https://github.com/nodejs/node/pull/49435)
* \[[`dad5785d5d`](https://github.com/nodejs/node/commit/dad5785d5d)] - **meta**: bump actions/setup-node from 3.7.0 to 3.8.1 (dependabot\[bot]) [#49434](https://github.com/nodejs/node/pull/49434)
* \[[`155a275acb`](https://github.com/nodejs/node/commit/155a275acb)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#49264](https://github.com/nodejs/node/pull/49264)
* \[[`bd17f0b992`](https://github.com/nodejs/node/commit/bd17f0b992)] - **meta**: mention nodejs/tsc when changing GH templates (Rafael Gonzaga) [#49189](https://github.com/nodejs/node/pull/49189)
* \[[`f6e68a0238`](https://github.com/nodejs/node/commit/f6e68a0238)] - **meta**: bump github/codeql-action from 2.20.1 to 2.21.2 (dependabot\[bot]) [#48986](https://github.com/nodejs/node/pull/48986)
* \[[`5c352eeecb`](https://github.com/nodejs/node/commit/5c352eeecb)] - **meta**: bump step-security/harden-runner from 2.4.1 to 2.5.0 (dependabot\[bot]) [#48985](https://github.com/nodejs/node/pull/48985)
* \[[`42ac5a6e5f`](https://github.com/nodejs/node/commit/42ac5a6e5f)] - **meta**: bump actions/setup-node from 3.6.0 to 3.7.0 (dependabot\[bot]) [#48984](https://github.com/nodejs/node/pull/48984)
* \[[`b0d769fe7c`](https://github.com/nodejs/node/commit/b0d769fe7c)] - **meta**: bump actions/setup-python from 4.6.1 to 4.7.0 (dependabot\[bot]) [#48983](https://github.com/nodejs/node/pull/48983)
* \[[`f62b24276c`](https://github.com/nodejs/node/commit/f62b24276c)] - **meta**: add mailmap entry for atlowChemi (Chemi Atlow) [#48810](https://github.com/nodejs/node/pull/48810)
* \[[`8c55f317a3`](https://github.com/nodejs/node/commit/8c55f317a3)] - **module**: move helpers out of cjs loader (Geoffrey Booth) [#49912](https://github.com/nodejs/node/pull/49912)
* \[[`14e148ee6c`](https://github.com/nodejs/node/commit/14e148ee6c)] - **module**: ensure successful import returns the same result (Antoine du Hamel) [#46662](https://github.com/nodejs/node/pull/46662)
* \[[`65dfe85f03`](https://github.com/nodejs/node/commit/65dfe85f03)] - **module**: implement `register` utility (João Lenon) [#46826](https://github.com/nodejs/node/pull/46826)
* \[[`6f0458d0a6`](https://github.com/nodejs/node/commit/6f0458d0a6)] - **module**: refactor to use `normalizeRequirableId` in the CJS module loader (Darshan Sen) [#47896](https://github.com/nodejs/node/pull/47896)
* \[[`89ed24b94a`](https://github.com/nodejs/node/commit/89ed24b94a)] - **module**: do less CJS module loader initialization at run time (Joyee Cheung) [#47194](https://github.com/nodejs/node/pull/47194)
* \[[`939c8764b8`](https://github.com/nodejs/node/commit/939c8764b8)] - **module**: move callbacks and conditions into modules/esm/utils.js (Joyee Cheung) [#45849](https://github.com/nodejs/node/pull/45849)
* \[[`a3b0f4cf55`](https://github.com/nodejs/node/commit/a3b0f4cf55)] - **module**: move modules/cjs/helpers.js to modules/helpers.js (Joyee Cheung) [#45849](https://github.com/nodejs/node/pull/45849)
* \[[`97579895f2`](https://github.com/nodejs/node/commit/97579895f2)] - **module, esm**: jsdoc for modules files (Geoffrey Booth) [#49523](https://github.com/nodejs/node/pull/49523)
* \[[`daca87bbef`](https://github.com/nodejs/node/commit/daca87bbef)] - **net**: use asserts in JS Socket Stream to catch races in future (Tim Perry) [#49400](https://github.com/nodejs/node/pull/49400)
* \[[`03063bd022`](https://github.com/nodejs/node/commit/03063bd022)] - **net**: fix crash due to simultaneous close/shutdown on JS Stream Sockets (Tim Perry) [#49400](https://github.com/nodejs/node/pull/49400)
* \[[`67fe7d8822`](https://github.com/nodejs/node/commit/67fe7d8822)] - **net**: fix setting of value in 'setDefaultAutoSelectFamilyAttemptTimeout' (Deokjin Kim) [#47012](https://github.com/nodejs/node/pull/47012)
* \[[`f449b222fd`](https://github.com/nodejs/node/commit/f449b222fd)] - **node-api**: update headers for better wasm support (Toyo Li) [#49037](https://github.com/nodejs/node/pull/49037)
* \[[`5148f030b8`](https://github.com/nodejs/node/commit/5148f030b8)] - **node-api**: run finalizers directly from GC (Vladimir Morozov) [#42651](https://github.com/nodejs/node/pull/42651)
* \[[`edef4fa668`](https://github.com/nodejs/node/commit/edef4fa668)] - **node-api**: enable uncaught exceptions policy by default (Chengzhong Wu) [#49313](https://github.com/nodejs/node/pull/49313)
* \[[`48a1b9336b`](https://github.com/nodejs/node/commit/48a1b9336b)] - **node-api**: fix compiler warning in node\_api.h (Michael Graeb) [#49103](https://github.com/nodejs/node/pull/49103)
* \[[`57966318fe`](https://github.com/nodejs/node/commit/57966318fe)] - **node-api**: avoid macro redefinition (Tobias Nießen) [#48879](https://github.com/nodejs/node/pull/48879)
* \[[`d4f26f4651`](https://github.com/nodejs/node/commit/d4f26f4651)] - **policy**: fix path to URL conversion (Antoine du Hamel) [#49133](https://github.com/nodejs/node/pull/49133)
* \[[`a625f22acb`](https://github.com/nodejs/node/commit/a625f22acb)] - **readline**: add paste bracket mode (Jakub Jankiewicz) [#47150](https://github.com/nodejs/node/pull/47150)
* \[[`bbafd42d75`](https://github.com/nodejs/node/commit/bbafd42d75)] - **repl**: display dynamic import variant in static import error messages (Hemanth HM) [#48129](https://github.com/nodejs/node/pull/48129)
* \[[`b8634eeb16`](https://github.com/nodejs/node/commit/b8634eeb16)] - **sea**: allow requiring core modules with the "node:" prefix (Darshan Sen) [#47779](https://github.com/nodejs/node/pull/47779)
* \[[`066d9d4492`](https://github.com/nodejs/node/commit/066d9d4492)] - **src**: remove unused function `GetName()` in node\_perf (Jungku Lee) [#49244](https://github.com/nodejs/node/pull/49244)
* \[[`158c91a38a`](https://github.com/nodejs/node/commit/158c91a38a)] - **src**: use ARES\_SUCCESS instead of 0 (Jungku Lee) [#49048](https://github.com/nodejs/node/pull/49048)
* \[[`8c33731ac6`](https://github.com/nodejs/node/commit/8c33731ac6)] - **src**: add a condition if the argument of `DomainToUnicode` is empty (Jungku Lee) [#49097](https://github.com/nodejs/node/pull/49097)
* \[[`67dba57d77`](https://github.com/nodejs/node/commit/67dba57d77)] - **src**: use ARES\_SUCCESS instead of 0 (Hyunjin Kim) [#48834](https://github.com/nodejs/node/pull/48834)
* \[[`97d87495c7`](https://github.com/nodejs/node/commit/97d87495c7)] - **src**: remove unnecessary temporary creation (Jason) [#48734](https://github.com/nodejs/node/pull/48734)
* \[[`f5384c3262`](https://github.com/nodejs/node/commit/f5384c3262)] - **src**: fix nullptr access on realm (Jan Olaf Krems) [#48802](https://github.com/nodejs/node/pull/48802)
* \[[`358273d77f`](https://github.com/nodejs/node/commit/358273d77f)] - **src**: remove OnScopeLeaveImpl's move assignment overload (Jason) [#48732](https://github.com/nodejs/node/pull/48732)
* \[[`cc7bf1f641`](https://github.com/nodejs/node/commit/cc7bf1f641)] - **(SEMVER-MINOR)** **src**: add cjs\_module\_lexer\_version base64\_version (Jithil P Ponnan) [#45629](https://github.com/nodejs/node/pull/45629)
* \[[`0a950c3752`](https://github.com/nodejs/node/commit/0a950c3752)] - **src**: add missing to\_ascii method in dns queries (Daniel Lemire) [#48354](https://github.com/nodejs/node/pull/48354)
* \[[`3552afb904`](https://github.com/nodejs/node/commit/3552afb904)] - **src**: fix duplication of externalized builtin code (Keyhan Vakil) [#47079](https://github.com/nodejs/node/pull/47079)
* \[[`66e4ba5062`](https://github.com/nodejs/node/commit/66e4ba5062)] - **src**: fix AliasedBuffer memory attribution in heap snapshots (Joyee Cheung) [#46817](https://github.com/nodejs/node/pull/46817)
* \[[`946f19b5e3`](https://github.com/nodejs/node/commit/946f19b5e3)] - **src**: move AliasedBuffer implementation to -inl.h (Joyee Cheung) [#46817](https://github.com/nodejs/node/pull/46817)
* \[[`d35f8702c9`](https://github.com/nodejs/node/commit/d35f8702c9)] - **src**: bootstrap prepare stack trace callback in shadow realm (Chengzhong Wu) [#47107](https://github.com/nodejs/node/pull/47107)
* \[[`3551a19205`](https://github.com/nodejs/node/commit/3551a19205)] - **src**: make BuiltinLoader threadsafe and non-global (Anna Henningsen) [#45942](https://github.com/nodejs/node/pull/45942)
* \[[`92311a0801`](https://github.com/nodejs/node/commit/92311a0801)] - **src**: define per-isolate internal bindings registration callback (Chengzhong Wu) [#45547](https://github.com/nodejs/node/pull/45547)
* \[[`629fc774ca`](https://github.com/nodejs/node/commit/629fc774ca)] - **src**: use an array for faster binding data lookup (Joyee Cheung) [#46620](https://github.com/nodejs/node/pull/46620)
* \[[`62e2e590fc`](https://github.com/nodejs/node/commit/62e2e590fc)] - **src**: fix TLSWrap lifetime bug in ALPN callback (Ben Noordhuis) [#49635](https://github.com/nodejs/node/pull/49635)
* \[[`fae60c5841`](https://github.com/nodejs/node/commit/fae60c5841)] - **stream**: use bitmap in readable state (Benjamin Gruenbaum) [#49745](https://github.com/nodejs/node/pull/49745)
* \[[`ee4fc7d78c`](https://github.com/nodejs/node/commit/ee4fc7d78c)] - **stream**: use Buffer.from when constructor is a Buffer (Matthew Aitken) [#49250](https://github.com/nodejs/node/pull/49250)
* \[[`651e4504ce`](https://github.com/nodejs/node/commit/651e4504ce)] - **stream**: add `highWaterMark` for the map operator (Raz Luvaton) [#49249](https://github.com/nodejs/node/pull/49249)
* \[[`d585d13127`](https://github.com/nodejs/node/commit/d585d13127)] - **stream**: improve WebStreams performance (Raz Luvaton) [#49089](https://github.com/nodejs/node/pull/49089)
* \[[`7f39f8e805`](https://github.com/nodejs/node/commit/7f39f8e805)] - **test**: replace forEach with for..of in test-http-perf\_hooks.js (Niya Shiyas) [#49818](https://github.com/nodejs/node/pull/49818)
* \[[`2f0ffde842`](https://github.com/nodejs/node/commit/2f0ffde842)] - **test**: replace forEach with for..of in test-net-isipv4.js (Niya Shiyas) [#49822](https://github.com/nodejs/node/pull/49822)
* \[[`bbd302b5ab`](https://github.com/nodejs/node/commit/bbd302b5ab)] - **test**: replace forEach with for..of in test-http2-server (Niya Shiyas) [#49819](https://github.com/nodejs/node/pull/49819)
* \[[`128ca3e213`](https://github.com/nodejs/node/commit/128ca3e213)] - **test**: replace forEach with for..of in test-http2-client-destroy.js (Niya Shiyas) [#49820](https://github.com/nodejs/node/pull/49820)
* \[[`a2ca1a605f`](https://github.com/nodejs/node/commit/a2ca1a605f)] - **test**: print instruction for creating missing snapshot in assertSnapshot (Raz Luvaton) [#48914](https://github.com/nodejs/node/pull/48914)
* \[[`a0bb30cdca`](https://github.com/nodejs/node/commit/a0bb30cdca)] - **test**: set `test-watch-mode-inspect` as flaky (Yagiz Nizipli) [#50259](https://github.com/nodejs/node/pull/50259)
* \[[`1047d95698`](https://github.com/nodejs/node/commit/1047d95698)] - **test**: set `test-emit-after-on-destroyed` as flaky (Yagiz Nizipli) [#50246](https://github.com/nodejs/node/pull/50246)
* \[[`91a3b57962`](https://github.com/nodejs/node/commit/91a3b57962)] - **test**: set inspector async stack test as flaky (Yagiz Nizipli) [#50244](https://github.com/nodejs/node/pull/50244)
* \[[`b41aa7b82a`](https://github.com/nodejs/node/commit/b41aa7b82a)] - **test**: set test-worker-nearheaplimit-deadlock flaky (StefanStojanovic) [#50277](https://github.com/nodejs/node/pull/50277)
* \[[`e81b066fb1`](https://github.com/nodejs/node/commit/e81b066fb1)] - **test**: set `test-cli-node-options` as flaky (Yagiz Nizipli) [#50296](https://github.com/nodejs/node/pull/50296)
* \[[`0c05c25c4e`](https://github.com/nodejs/node/commit/0c05c25c4e)] - **test**: set crypto-timing test as flaky (Yagiz Nizipli) [#50232](https://github.com/nodejs/node/pull/50232)
* \[[`83e339dbba`](https://github.com/nodejs/node/commit/83e339dbba)] - **test**: set `test-structuredclone-*` as flaky (Yagiz Nizipli) [#50261](https://github.com/nodejs/node/pull/50261)
* \[[`866a399488`](https://github.com/nodejs/node/commit/866a399488)] - **test**: set inspector async hook test as flaky (Yagiz Nizipli) [#50252](https://github.com/nodejs/node/pull/50252)
* \[[`cb0bd2116b`](https://github.com/nodejs/node/commit/cb0bd2116b)] - **test**: set parallel http server test as flaky (Yagiz Nizipli) [#50227](https://github.com/nodejs/node/pull/50227)
* \[[`54f3d877ae`](https://github.com/nodejs/node/commit/54f3d877ae)] - **test**: set test-worker-nearheaplimit-deadlock flaky (Stefan Stojanovic) [#50238](https://github.com/nodejs/node/pull/50238)
* \[[`5953a255b6`](https://github.com/nodejs/node/commit/5953a255b6)] - **test**: set `test-runner-watch-mode` as flaky (Yagiz Nizipli) [#50221](https://github.com/nodejs/node/pull/50221)
* \[[`5820d7e14d`](https://github.com/nodejs/node/commit/5820d7e14d)] - **test**: deflake test-runner-output (Moshe Atlow) [#49878](https://github.com/nodejs/node/pull/49878)
* \[[`1d75da43f2`](https://github.com/nodejs/node/commit/1d75da43f2)] - _**Revert**_ "**test**: mark test-http-regr-gh-2928 as flaky" (Luigi Pinca) [#49708](https://github.com/nodejs/node/pull/49708)
* \[[`5df23520e7`](https://github.com/nodejs/node/commit/5df23520e7)] - **test**: mark test-runner-watch-mode as flaky (Joyee Cheung) [#49627](https://github.com/nodejs/node/pull/49627)
* \[[`7e714e6497`](https://github.com/nodejs/node/commit/7e714e6497)] - **test**: remove --no-warnings flag in test\_runner fixtures (Raz Luvaton) [#48989](https://github.com/nodejs/node/pull/48989)
* \[[`0fea550641`](https://github.com/nodejs/node/commit/0fea550641)] - **test**: reorder test files fixtures for better understanding (Raz Luvaton) [#48787](https://github.com/nodejs/node/pull/48787)
* \[[`c5857bde42`](https://github.com/nodejs/node/commit/c5857bde42)] - **test**: avoid copying test source files (Chengzhong Wu) [#49515](https://github.com/nodejs/node/pull/49515)
* \[[`67cb6f9a34`](https://github.com/nodejs/node/commit/67cb6f9a34)] - **test**: reduce length in crypto keygen tests (Joyee Cheung) [#49221](https://github.com/nodejs/node/pull/49221)
* \[[`958e114655`](https://github.com/nodejs/node/commit/958e114655)] - **test**: split JWK async elliptic curve keygen tests (Joyee Cheung) [#49221](https://github.com/nodejs/node/pull/49221)
* \[[`344592dc00`](https://github.com/nodejs/node/commit/344592dc00)] - **test**: split test-crypto-keygen.js (Joyee Cheung) [#49221](https://github.com/nodejs/node/pull/49221)
* \[[`5f1a1aeeed`](https://github.com/nodejs/node/commit/5f1a1aeeed)] - **test**: rename test-crypto-modp1-error (Tobias Nießen) [#49348](https://github.com/nodejs/node/pull/49348)
* \[[`107015b45d`](https://github.com/nodejs/node/commit/107015b45d)] - **test**: migrate message source map tests from Python to JS (Yiyun Lei) [#49238](https://github.com/nodejs/node/pull/49238)
* \[[`ddebd29515`](https://github.com/nodejs/node/commit/ddebd29515)] - **test**: fix compiler warning in NodeCryptoEnv (Tobias Nießen) [#49206](https://github.com/nodejs/node/pull/49206)
* \[[`0261c5de56`](https://github.com/nodejs/node/commit/0261c5de56)] - **test**: make test-perf-hooks more robust and work with workers (Joyee Cheung) [#49197](https://github.com/nodejs/node/pull/49197)
* \[[`e18de802c8`](https://github.com/nodejs/node/commit/e18de802c8)] - **test**: use gcUntil() in test-v8-serialize-leak (Joyee Cheung) [#49168](https://github.com/nodejs/node/pull/49168)
* \[[`52d5dd8dba`](https://github.com/nodejs/node/commit/52d5dd8dba)] - **test**: add expectSyncExitWithoutError() and expectSyncExit() utils (Joyee Cheung) [#49020](https://github.com/nodejs/node/pull/49020)
* \[[`67925bb914`](https://github.com/nodejs/node/commit/67925bb914)] - **test**: add Symbol.dispose support to mock timers (Benjamin Gruenbaum) [#48549](https://github.com/nodejs/node/pull/48549)
* \[[`00c08539b6`](https://github.com/nodejs/node/commit/00c08539b6)] - **test**: fix edge snapshot stack traces (Geoffrey Booth) [#49659](https://github.com/nodejs/node/pull/49659)
* \[[`48f6d9a975`](https://github.com/nodejs/node/commit/48f6d9a975)] - **test**: refactor `test-node-output-errors` (Antoine du Hamel) [#48992](https://github.com/nodejs/node/pull/48992)
* \[[`c1a8ae38c0`](https://github.com/nodejs/node/commit/c1a8ae38c0)] - **test**: deflake `test-loaders-workers-spawned` (Antoine du Hamel) [#50251](https://github.com/nodejs/node/pull/50251)
* \[[`d5cc5d7956`](https://github.com/nodejs/node/commit/d5cc5d7956)] - **test**: deflake `test-esm-loader-resolve-type` (Antoine du Hamel) [#50273](https://github.com/nodejs/node/pull/50273)
* \[[`866d646b0e`](https://github.com/nodejs/node/commit/866d646b0e)] - **test**: increase coverage of `Module.register` and `initialize` hook (Antoine du Hamel) [#49532](https://github.com/nodejs/node/pull/49532)
* \[[`65201ab36b`](https://github.com/nodejs/node/commit/65201ab36b)] - **test**: isolate `globalPreload` tests (Geoffrey Booth) [#49545](https://github.com/nodejs/node/pull/49545)
* \[[`65058d9a35`](https://github.com/nodejs/node/commit/65058d9a35)] - **test**: add `tmpdir.fileURL()` (Livia Medeiros) [#49040](https://github.com/nodejs/node/pull/49040)
* \[[`e1c6f46926`](https://github.com/nodejs/node/commit/e1c6f46926)] - **test**: reduce flakiness of `test-esm-loader-hooks` (Antoine du Hamel) [#49248](https://github.com/nodejs/node/pull/49248)
* \[[`e8d475ffc4`](https://github.com/nodejs/node/commit/e8d475ffc4)] - **test**: refactor `test-esm-loader-hooks` for easier debugging (Antoine du Hamel) [#49131](https://github.com/nodejs/node/pull/49131)
* \[[`26f01669fc`](https://github.com/nodejs/node/commit/26f01669fc)] - **test**: reduce flakiness of `test-esm-loader-hooks` (Antoine du Hamel) [#49105](https://github.com/nodejs/node/pull/49105)
* \[[`d5a1153970`](https://github.com/nodejs/node/commit/d5a1153970)] - **test**: use `fixtures.fileURL` when appropriate (Antoine du Hamel) [#48990](https://github.com/nodejs/node/pull/48990)
* \[[`3424793013`](https://github.com/nodejs/node/commit/3424793013)] - **test**: fix snapshot tests when cwd contains spaces or backslashes (Antoine du Hamel) [#48959](https://github.com/nodejs/node/pull/48959)
* \[[`a9347a4635`](https://github.com/nodejs/node/commit/a9347a4635)] - **test**: order `common.mjs` in ASCII order (Antoine du Hamel) [#48960](https://github.com/nodejs/node/pull/48960)
* \[[`adb9280d8e`](https://github.com/nodejs/node/commit/adb9280d8e)] - **test**: fix some assumptions in tests (Antoine du Hamel) [#48958](https://github.com/nodejs/node/pull/48958)
* \[[`f02637e805`](https://github.com/nodejs/node/commit/f02637e805)] - **test**: fix `es-module/test-esm-initialization` (Antoine du Hamel) [#48880](https://github.com/nodejs/node/pull/48880)
* \[[`8c918e5e59`](https://github.com/nodejs/node/commit/8c918e5e59)] - **test**: relax version check with shared OpenSSL (Luigi Pinca) [#50505](https://github.com/nodejs/node/pull/50505)
* \[[`c6caf13ad5`](https://github.com/nodejs/node/commit/c6caf13ad5)] - **test**: fix crypto-dh error message for OpenSSL 3.x (Kerem Kat) [#50395](https://github.com/nodejs/node/pull/50395)
* \[[`65b41ebd1f`](https://github.com/nodejs/node/commit/65b41ebd1f)] - **test**: split test-crypto-dh to avoid timeout on slow machines in the CI (Joyee Cheung) [#49492](https://github.com/nodejs/node/pull/49492)
* \[[`7606921e1d`](https://github.com/nodejs/node/commit/7606921e1d)] - **test**: fix testsuite against zlib version 1.3 (Dominique Leuenberger) [#50364](https://github.com/nodejs/node/pull/50364)
* \[[`4f78233254`](https://github.com/nodejs/node/commit/4f78233254)] - **test**: verify tracePromise does not do runStores (Stephen Belanger) [#47349](https://github.com/nodejs/node/pull/47349)
* \[[`cb6ef74cf2`](https://github.com/nodejs/node/commit/cb6ef74cf2)] - **test**: fix IPv6 checks on IBM i (Abdirahim Musse) [#46546](https://github.com/nodejs/node/pull/46546)
* \[[`bcf97ab1b2`](https://github.com/nodejs/node/commit/bcf97ab1b2)] - **test**: fix flaky test-runner-exit-code.js (Colin Ihrig) [#46138](https://github.com/nodejs/node/pull/46138)
* \[[`f0e8ff90eb`](https://github.com/nodejs/node/commit/f0e8ff90eb)] - **test**: use an array for WPT gloablThis initialization scripts (Joyee Cheung) [#46425](https://github.com/nodejs/node/pull/46425)
* \[[`0e0dd1fe90`](https://github.com/nodejs/node/commit/0e0dd1fe90)] - **test**: adapt tests for OpenSSL 3.1 (OttoHollmann) [#47859](https://github.com/nodejs/node/pull/47859)
* \[[`e3ea906988`](https://github.com/nodejs/node/commit/e3ea906988)] - **test**: disambiguate AIX and IBM i (Richard Lau) [#48056](https://github.com/nodejs/node/pull/48056)
* \[[`088460d80c`](https://github.com/nodejs/node/commit/088460d80c)] - **test**: add os setPriority, getPriority test coverage (Wael) [#38771](https://github.com/nodejs/node/pull/38771)
* \[[`b011a498c5`](https://github.com/nodejs/node/commit/b011a498c5)] - **test,benchmark**: use `tmpdir.fileURL()` (Livia Medeiros) [#49138](https://github.com/nodejs/node/pull/49138)
* \[[`54168b7364`](https://github.com/nodejs/node/commit/54168b7364)] - **test\_runner**: add test location for FileTests (Colin Ihrig) [#49999](https://github.com/nodejs/node/pull/49999)
* \[[`1d9c37161d`](https://github.com/nodejs/node/commit/1d9c37161d)] - **test\_runner**: replace spurious if with else (Colin Ihrig) [#49943](https://github.com/nodejs/node/pull/49943)
* \[[`b283ae4238`](https://github.com/nodejs/node/commit/b283ae4238)] - **(SEMVER-MINOR)** **test\_runner**: accept `testOnly` in `run` (Moshe Atlow) [#49753](https://github.com/nodejs/node/pull/49753)
* \[[`103c4a088a`](https://github.com/nodejs/node/commit/103c4a088a)] - **test\_runner**: catch reporter errors (Moshe Atlow) [#49646](https://github.com/nodejs/node/pull/49646)
* \[[`e459598cf2`](https://github.com/nodejs/node/commit/e459598cf2)] - **test\_runner**: fix test runner watch mode when no positional arguments (Moshe Atlow) [#49578](https://github.com/nodejs/node/pull/49578)
* \[[`059b1945d8`](https://github.com/nodejs/node/commit/059b1945d8)] - **(SEMVER-MINOR)** **test\_runner**: add junit reporter (Moshe Atlow) [#49614](https://github.com/nodejs/node/pull/49614)
* \[[`76a35632d3`](https://github.com/nodejs/node/commit/76a35632d3)] - **test\_runner**: add jsdocs to mock.js (Caio Borghi) [#49555](https://github.com/nodejs/node/pull/49555)
* \[[`ba57c24fde`](https://github.com/nodejs/node/commit/ba57c24fde)] - **test\_runner**: fix invalid timer call (Erick Wendel) [#49477](https://github.com/nodejs/node/pull/49477)
* \[[`5949ae016f`](https://github.com/nodejs/node/commit/5949ae016f)] - **test\_runner**: add jsdocs to MockTimers (Erick Wendel) [#49476](https://github.com/nodejs/node/pull/49476)
* \[[`429846f258`](https://github.com/nodejs/node/commit/429846f258)] - **test\_runner**: fix typescript coverage (Moshe Atlow) [#49406](https://github.com/nodejs/node/pull/49406)
* \[[`760c98280e`](https://github.com/nodejs/node/commit/760c98280e)] - **test\_runner**: preserve original property descriptor (Erick Wendel) [#49433](https://github.com/nodejs/node/pull/49433)
* \[[`95cc98e14b`](https://github.com/nodejs/node/commit/95cc98e14b)] - **test\_runner**: expose spec reporter as newable function (Chemi Atlow) [#49184](https://github.com/nodejs/node/pull/49184)
* \[[`d83964031b`](https://github.com/nodejs/node/commit/d83964031b)] - **test\_runner**: reland run global after() hook earlier (Colin Ihrig) [#49116](https://github.com/nodejs/node/pull/49116)
* \[[`d61a505546`](https://github.com/nodejs/node/commit/d61a505546)] - **(SEMVER-MINOR)** **test\_runner**: expose location of tests (Colin Ihrig) [#48975](https://github.com/nodejs/node/pull/48975)
* \[[`00c70e12f7`](https://github.com/nodejs/node/commit/00c70e12f7)] - **test\_runner**: dont set exit code on todo tests (Moshe Atlow) [#48929](https://github.com/nodejs/node/pull/48929)
* \[[`71f7ed728d`](https://github.com/nodejs/node/commit/71f7ed728d)] - **test\_runner**: fix global after not failing the tests (Raz Luvaton) [#48913](https://github.com/nodejs/node/pull/48913)
* \[[`11fbd924ed`](https://github.com/nodejs/node/commit/11fbd924ed)] - **test\_runner**: add support for setImmediate (Erick Wendel) [#49397](https://github.com/nodejs/node/pull/49397)
* \[[`fb64d415be`](https://github.com/nodejs/node/commit/fb64d415be)] - **test\_runner**: fix timeout in \*Each hook failing further tests (Raz Luvaton) [#48925](https://github.com/nodejs/node/pull/48925)
* \[[`67d7faddb2`](https://github.com/nodejs/node/commit/67d7faddb2)] - **test\_runner**: cleanup test timeout abort listener (Raz Luvaton) [#48915](https://github.com/nodejs/node/pull/48915)
* \[[`d217435cd3`](https://github.com/nodejs/node/commit/d217435cd3)] - **test\_runner**: fix todo and only in spec reporter (Moshe Atlow) [#48929](https://github.com/nodejs/node/pull/48929)
* \[[`9f122be15a`](https://github.com/nodejs/node/commit/9f122be15a)] - **test\_runner**: unwrap error message in TAP reporter (Colin Ihrig) [#48942](https://github.com/nodejs/node/pull/48942)
* \[[`3a74316624`](https://github.com/nodejs/node/commit/3a74316624)] - **test\_runner**: add `__proto__` null (Raz Luvaton) [#48663](https://github.com/nodejs/node/pull/48663)
* \[[`66ea9bdb4d`](https://github.com/nodejs/node/commit/66ea9bdb4d)] - **test\_runner**: fix global before not called when no global test exists (Raz Luvaton) [#48877](https://github.com/nodejs/node/pull/48877)
* \[[`0bd9704018`](https://github.com/nodejs/node/commit/0bd9704018)] - **test\_runner**: use os.availableParallelism() (Colin Ihrig) [#45969](https://github.com/nodejs/node/pull/45969)
* \[[`b55eb2a8d1`](https://github.com/nodejs/node/commit/b55eb2a8d1)] - **(SEMVER-MINOR)** **test\_runner**: add shards support (Raz Luvaton) [#48639](https://github.com/nodejs/node/pull/48639)
* \[[`c2575c8db0`](https://github.com/nodejs/node/commit/c2575c8db0)] - **test\_runner**: fix test\_runner `test:fail` event type (Ethan Arrowood) [#48854](https://github.com/nodejs/node/pull/48854)
* \[[`6b186d41cd`](https://github.com/nodejs/node/commit/6b186d41cd)] - **test\_runner**: fix async callback in describe not awaited (Raz Luvaton) [#48856](https://github.com/nodejs/node/pull/48856)
* \[[`2d4511baab`](https://github.com/nodejs/node/commit/2d4511baab)] - **test\_runner**: call abort on test finish (Raz Luvaton) [#48827](https://github.com/nodejs/node/pull/48827)
* \[[`05e7f28b40`](https://github.com/nodejs/node/commit/05e7f28b40)] - **(SEMVER-MINOR)** **test\_runner**: add initial draft for fakeTimers (Erick Wendel) [#47775](https://github.com/nodejs/node/pull/47775)
* \[[`428301ad27`](https://github.com/nodejs/node/commit/428301ad27)] - **(SEMVER-MINOR)** **test\_runner, cli**: add --test-concurrency flag (Colin Ihrig) [#49996](https://github.com/nodejs/node/pull/49996)
* \[[`4b2e258c76`](https://github.com/nodejs/node/commit/4b2e258c76)] - **test\_runner,test**: fix flaky test-runner-cli-concurrency.js (Colin Ihrig) [#50108](https://github.com/nodejs/node/pull/50108)
* \[[`b5d16cd8f0`](https://github.com/nodejs/node/commit/b5d16cd8f0)] - **(SEMVER-MINOR)** **tls**: add ALPNCallback server option for dynamic ALPN negotiation (Tim Perry) [#45190](https://github.com/nodejs/node/pull/45190)
* \[[`a1c94037ab`](https://github.com/nodejs/node/commit/a1c94037ab)] - **tools**: fix --v8-non-optimized-debug for v18.x (Joyee Cheung) [#50612](https://github.com/nodejs/node/pull/50612)
* \[[`3b65c61f5c`](https://github.com/nodejs/node/commit/3b65c61f5c)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#50083](https://github.com/nodejs/node/pull/50083)
* \[[`07941a3609`](https://github.com/nodejs/node/commit/07941a3609)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#49983](https://github.com/nodejs/node/pull/49983)
* \[[`ad6e7a6270`](https://github.com/nodejs/node/commit/ad6e7a6270)] - **tools**: update lint-md-dependencies to rollup\@3.29.2 (Node.js GitHub Bot) [#49679](https://github.com/nodejs/node/pull/49679)
* \[[`3511b13c94`](https://github.com/nodejs/node/commit/3511b13c94)] - **tools**: update lint-md-dependencies to rollup\@3.29.0 unified\@11.0.3 (Node.js GitHub Bot) [#49584](https://github.com/nodejs/node/pull/49584)
* \[[`b6522a2b6b`](https://github.com/nodejs/node/commit/b6522a2b6b)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#49342](https://github.com/nodejs/node/pull/49342)
* \[[`01b2588acf`](https://github.com/nodejs/node/commit/01b2588acf)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#49253](https://github.com/nodejs/node/pull/49253)
* \[[`0cb42ccb5c`](https://github.com/nodejs/node/commit/0cb42ccb5c)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#49122](https://github.com/nodejs/node/pull/49122)
* \[[`caa7ecca30`](https://github.com/nodejs/node/commit/caa7ecca30)] - **tools**: update lint-md-dependencies to rollup\@3.27.2 (Node.js GitHub Bot) [#49035](https://github.com/nodejs/node/pull/49035)
* \[[`42846a02b3`](https://github.com/nodejs/node/commit/42846a02b3)] - **tools**: limit the number of auto start CIs (Antoine du Hamel) [#49067](https://github.com/nodejs/node/pull/49067)
* \[[`9cc3b2061f`](https://github.com/nodejs/node/commit/9cc3b2061f)] - **tools**: update lint-md-dependencies to rollup\@3.27.0 (Node.js GitHub Bot) [#48965](https://github.com/nodejs/node/pull/48965)
* \[[`1cbb186039`](https://github.com/nodejs/node/commit/1cbb186039)] - **tools**: update lint-md-dependencies to rollup\@3.26.3 (Node.js GitHub Bot) [#48888](https://github.com/nodejs/node/pull/48888)
* \[[`f7d6e9ba43`](https://github.com/nodejs/node/commit/f7d6e9ba43)] - **tools**: update lint-md-dependencies to @rollup/plugin-commonjs\@25.0.3 (Node.js GitHub Bot) [#48791](https://github.com/nodejs/node/pull/48791)
* \[[`a98addb626`](https://github.com/nodejs/node/commit/a98addb626)] - **tools**: skip ruff on tools/node\_modules (Moshe Atlow) [#49838](https://github.com/nodejs/node/pull/49838)
* \[[`6cf8dd95d1`](https://github.com/nodejs/node/commit/6cf8dd95d1)] - **tools**: update eslint to 8.51.0 (Node.js GitHub Bot) [#50084](https://github.com/nodejs/node/pull/50084)
* \[[`b8d2a29ff5`](https://github.com/nodejs/node/commit/b8d2a29ff5)] - **tools**: update eslint to 8.50.0 (Node.js GitHub Bot) [#49989](https://github.com/nodejs/node/pull/49989)
* \[[`1c1cbf06f0`](https://github.com/nodejs/node/commit/1c1cbf06f0)] - **tools**: update eslint to 8.49.0 (Node.js GitHub Bot) [#49586](https://github.com/nodejs/node/pull/49586)
* \[[`92d21864c5`](https://github.com/nodejs/node/commit/92d21864c5)] - **tools**: update eslint to 8.48.0 (Node.js GitHub Bot) [#49343](https://github.com/nodejs/node/pull/49343)
* \[[`0940de36ee`](https://github.com/nodejs/node/commit/0940de36ee)] - **tools**: update eslint to 8.47.0 (Node.js GitHub Bot) [#49124](https://github.com/nodejs/node/pull/49124)
* \[[`4880a05fa2`](https://github.com/nodejs/node/commit/4880a05fa2)] - **tools**: update eslint to 8.46.0 (Node.js GitHub Bot) [#48966](https://github.com/nodejs/node/pull/48966)
* \[[`e9632454cd`](https://github.com/nodejs/node/commit/e9632454cd)] - **tools**: update eslint to 8.45.0 (Node.js GitHub Bot) [#48793](https://github.com/nodejs/node/pull/48793)
* \[[`ae49f319c4`](https://github.com/nodejs/node/commit/ae49f319c4)] - **tools**: drop support for osx notarization with gon (Ulises Gascón) [#50291](https://github.com/nodejs/node/pull/50291)
* \[[`014b65e9b7`](https://github.com/nodejs/node/commit/014b65e9b7)] - **tools**: use osx notarytool for future releases (Ulises Gascon) [#48701](https://github.com/nodejs/node/pull/48701)
* \[[`71c386a25c`](https://github.com/nodejs/node/commit/71c386a25c)] - **typings**: update JSDoc for `cwd` in `child_process` (LiviaMedeiros) [#49029](https://github.com/nodejs/node/pull/49029)
* \[[`4d70a2c344`](https://github.com/nodejs/node/commit/4d70a2c344)] - **typings**: sync JSDoc with the actual implementation (Hyunjin Kim) [#48853](https://github.com/nodejs/node/pull/48853)
* \[[`945609487c`](https://github.com/nodejs/node/commit/945609487c)] - **typings**: fix missing property in `ExportedHooks` (Antoine du Hamel) [#49567](https://github.com/nodejs/node/pull/49567)
* \[[`2ef80f12b9`](https://github.com/nodejs/node/commit/2ef80f12b9)] - **typings**: fix JSDoc in ESM loader modules (Antoine du Hamel) [#48424](https://github.com/nodejs/node/pull/48424)
* \[[`7fc15b6d42`](https://github.com/nodejs/node/commit/7fc15b6d42)] - **url**: fix `isURL` detection by checking `path` (Zhuo Zhang) [#48928](https://github.com/nodejs/node/pull/48928)
* \[[`916a63b124`](https://github.com/nodejs/node/commit/916a63b124)] - **url**: improve `isURL` detection (Yagiz Nizipli) [#47886](https://github.com/nodejs/node/pull/47886)
* \[[`ac27431372`](https://github.com/nodejs/node/commit/ac27431372)] - **url**: validate `pathToFileURL(path)` argument as string (LiviaMedeiros) [#49161](https://github.com/nodejs/node/pull/49161)
* \[[`f256b160bf`](https://github.com/nodejs/node/commit/f256b160bf)] - **url**: handle unicode hostname if empty (Yagiz Nizipli) [#49396](https://github.com/nodejs/node/pull/49396)
* \[[`2441415c68`](https://github.com/nodejs/node/commit/2441415c68)] - **url**: reduce `pathToFileURL` cpp calls (Yagiz Nizipli) [#48709](https://github.com/nodejs/node/pull/48709)
* \[[`227e749888`](https://github.com/nodejs/node/commit/227e749888)] - **url**: validate URL constructor arg length (Matthew Aitken) [#47513](https://github.com/nodejs/node/pull/47513)
* \[[`bf4ee17d18`](https://github.com/nodejs/node/commit/bf4ee17d18)] - **url**: validate argument length in canParse (Matthew Aitken) [#47513](https://github.com/nodejs/node/pull/47513)
* \[[`a67fa2a107`](https://github.com/nodejs/node/commit/a67fa2a107)] - **util**: add `getCwdSafe` internal util fn (João Lenon) [#48434](https://github.com/nodejs/node/pull/48434)
* \[[`e96f7ef881`](https://github.com/nodejs/node/commit/e96f7ef881)] - **(SEMVER-MINOR)** **vm**: use import attributes instead of import assertions (Antoine du Hamel) [#50141](https://github.com/nodejs/node/pull/50141)
* \[[`525de686a7`](https://github.com/nodejs/node/commit/525de686a7)] - **(SEMVER-MINOR)** **wasi**: updates required for latest uvwasi version (Michael Dawson) [#49908](https://github.com/nodejs/node/pull/49908)
* \[[`f27d505805`](https://github.com/nodejs/node/commit/f27d505805)] - **watch**: decrease debounce rate (Moshe Atlow) [#48926](https://github.com/nodejs/node/pull/48926)
* \[[`83a6d20d70`](https://github.com/nodejs/node/commit/83a6d20d70)] - **watch**: use debounce instead of throttle (Moshe Atlow) [#48926](https://github.com/nodejs/node/pull/48926)
* \[[`879b958184`](https://github.com/nodejs/node/commit/879b958184)] - **worker**: protect against user mutating well-known prototypes (Antoine du Hamel) [#49270](https://github.com/nodejs/node/pull/49270)

<a id="18.18.2"></a>

## 2023-10-13, Version 18.18.2 'Hydrogen' (LTS), @RafaelGSS

This is a security release.

### Notable Changes

The following CVEs are fixed in this release:

* [CVE-2023-44487](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-44487): `nghttp2` Security Release (High)
* [CVE-2023-45143](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-45143): `undici` Security Release (High)
* [CVE-2023-38552](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-38552):  Integrity checks according to policies can be circumvented (Medium)
* [CVE-2023-39333](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-39333): Code injection via WebAssembly export names (Low)

More detailed information on each of the vulnerabilities can be found in [October 2023 Security Releases](https://nodejs.org/en/blog/vulnerability/october-2023-security-releases/) blog post.

### Commits

* \[[`55028468db`](https://github.com/nodejs/node/commit/55028468db)] - **deps**: update undici to v5.26.3 (Matteo Collina) [#50153](https://github.com/nodejs/node/pull/50153)
* \[[`a792bbc515`](https://github.com/nodejs/node/commit/a792bbc515)] - **deps**: update nghttp2 to 1.57.0 (James M Snell) [#50121](https://github.com/nodejs/node/pull/50121)
* \[[`f6444defa4`](https://github.com/nodejs/node/commit/f6444defa4)] - **deps**: update nghttp2 to 1.56.0 (Node.js GitHub Bot) [#49582](https://github.com/nodejs/node/pull/49582)
* \[[`7e9b08dfd4`](https://github.com/nodejs/node/commit/7e9b08dfd4)] - **deps**: update nghttp2 to 1.55.1 (Node.js GitHub Bot) [#48790](https://github.com/nodejs/node/pull/48790)
* \[[`85672c153f`](https://github.com/nodejs/node/commit/85672c153f)] - **deps**: update nghttp2 to 1.55.0 (Node.js GitHub Bot) [#48746](https://github.com/nodejs/node/pull/48746)
* \[[`300a902422`](https://github.com/nodejs/node/commit/300a902422)] - **deps**: update nghttp2 to 1.53.0 (Node.js GitHub Bot) [#47997](https://github.com/nodejs/node/pull/47997)
* \[[`7d83ed0bf6`](https://github.com/nodejs/node/commit/7d83ed0bf6)] - _**Revert**_ "**deps**: update nghttp2 to 1.55.0" (Richard Lau) [#50151](https://github.com/nodejs/node/pull/50151)
* \[[`1193ca5fdb`](https://github.com/nodejs/node/commit/1193ca5fdb)] - **lib**: let deps require `node` prefixed modules (Matthew Aitken) [#50047](https://github.com/nodejs/node/pull/50047)
* \[[`eaf9083cf1`](https://github.com/nodejs/node/commit/eaf9083cf1)] - **module**: fix code injection through export names (Tobias Nießen) [nodejs-private/node-private#461](https://github.com/nodejs-private/node-private/pull/461)
* \[[`1c538938cc`](https://github.com/nodejs/node/commit/1c538938cc)] - **policy**: use tamper-proof integrity check function (Tobias Nießen) [nodejs-private/node-private#462](https://github.com/nodejs-private/node-private/pull/462)

<a id="18.18.1"></a>

## 2023-10-10, Version 18.18.1 'Hydrogen' (LTS), @richardlau

### Notable Changes

This release addresses some regressions that appeared in Node.js 18.18.0:

* (Windows) FS can not handle certain characters in file name [#48673](https://github.com/nodejs/node/issues/48673)
* 18 and 20 node images give error - Text file busy (after re-build images) [nodejs/docker-node#1968](https://github.com/nodejs/docker-node/issues/1968)
* libuv update in 18.18.0 breaks webpack's thread-loader [#49911](https://github.com/nodejs/node/issues/49911)

The libuv 1.45.0 and 1.46.0 updates that were released in Node.js 18.18.0 have been temporarily reverted.

### Commits

* \[[`3e3a75cc46`](https://github.com/nodejs/node/commit/3e3a75cc46)] - _**Revert**_ "**build**: sync libuv header change" (Richard Lau) [#50036](https://github.com/nodejs/node/pull/50036)
* \[[`14ece2c479`](https://github.com/nodejs/node/commit/14ece2c479)] - _**Revert**_ "**deps**: upgrade to libuv 1.45.0" (Richard Lau) [#50036](https://github.com/nodejs/node/pull/50036)
* \[[`022352acbe`](https://github.com/nodejs/node/commit/022352acbe)] - _**Revert**_ "**deps**: upgrade to libuv 1.46.0" (Richard Lau) [#50036](https://github.com/nodejs/node/pull/50036)
* \[[`d9f138189c`](https://github.com/nodejs/node/commit/d9f138189c)] - _**Revert**_ "**deps**: add missing thread-common.c in uv.gyp" (Richard Lau) [#50036](https://github.com/nodejs/node/pull/50036)
* \[[`7a3e1ffbb8`](https://github.com/nodejs/node/commit/7a3e1ffbb8)] - **fs**: make sure to write entire buffer (Robert Nagy) [#49211](https://github.com/nodejs/node/pull/49211)
* \[[`04cba95a67`](https://github.com/nodejs/node/commit/04cba95a67)] - **test**: add `tmpdir.resolve()` (Livia Medeiros) [#49079](https://github.com/nodejs/node/pull/49079)

<a id="18.18.0"></a>

## 2023-09-18, Version 18.18.0 'Hydrogen' (LTS), @ruyadorno

### Notable Changes

* \[[`7dc731d4bf`](https://github.com/nodejs/node/commit/7dc731d4bf)] - **build**: sync libuv header change (Jiawen Geng) [#48078](https://github.com/nodejs/node/pull/48078)
* \[[`490fc004b0`](https://github.com/nodejs/node/commit/490fc004b0)] - **crypto**: update root certificates to NSS 3.93 (Node.js GitHub Bot) [#49341](https://github.com/nodejs/node/pull/49341)
* \[[`dd8cd97d4d`](https://github.com/nodejs/node/commit/dd8cd97d4d)] - **crypto**: update root certificates to NSS 3.90 (Node.js GitHub Bot) [#48416](https://github.com/nodejs/node/pull/48416)
* \[[`ea23870bec`](https://github.com/nodejs/node/commit/ea23870bec)] - **deps**: add missing thread-common.c in uv.gyp (Santiago Gimeno) [#48078](https://github.com/nodejs/node/pull/48078)
* \[[`88855e0b1b`](https://github.com/nodejs/node/commit/88855e0b1b)] - **deps**: upgrade to libuv 1.46.0 (Santiago Gimeno) [#48078](https://github.com/nodejs/node/pull/48078)
* \[[`fb2b80fca0`](https://github.com/nodejs/node/commit/fb2b80fca0)] - **deps**: upgrade to libuv 1.45.0 (Santiago Gimeno) [#48078](https://github.com/nodejs/node/pull/48078)
* \[[`249879e46c`](https://github.com/nodejs/node/commit/249879e46c)] - **doc**: add atlowChemi to collaborators (atlowChemi) [#48757](https://github.com/nodejs/node/pull/48757)
* \[[`e8dc7bde6a`](https://github.com/nodejs/node/commit/e8dc7bde6a)] - **doc**: add vmoroz to collaborators (Vladimir Morozov) [#48527](https://github.com/nodejs/node/pull/48527)
* \[[`a30f2fbcc1`](https://github.com/nodejs/node/commit/a30f2fbcc1)] - **doc**: add kvakil to collaborators (Keyhan Vakil) [#48449](https://github.com/nodejs/node/pull/48449)
* \[[`c39b7c240e`](https://github.com/nodejs/node/commit/c39b7c240e)] - **(SEMVER-MINOR)** **esm**: add `--import` flag (Moshe Atlow) [#43942](https://github.com/nodejs/node/pull/43942)
* \[[`a68a67f54d`](https://github.com/nodejs/node/commit/a68a67f54d)] - **(SEMVER-MINOR)** **events**: allow safely adding listener to abortSignal (Chemi Atlow) [#48596](https://github.com/nodejs/node/pull/48596)
* \[[`3a8586bee2`](https://github.com/nodejs/node/commit/3a8586bee2)] - **fs, stream**: initial `Symbol.dispose` and `Symbol.asyncDispose` support (Moshe Atlow) [#48518](https://github.com/nodejs/node/pull/48518)
* \[[`863bdb785d`](https://github.com/nodejs/node/commit/863bdb785d)] - **net**: add autoSelectFamily global getter and setter (Paolo Insogna) [#45777](https://github.com/nodejs/node/pull/45777)
* \[[`c59ae86ba0`](https://github.com/nodejs/node/commit/c59ae86ba0)] - **(SEMVER-MINOR)** **url**: add value argument to has and delete methods (Sankalp Shubham) [#47885](https://github.com/nodejs/node/pull/47885)

### Commits

* \[[`d1f43317ea`](https://github.com/nodejs/node/commit/d1f43317ea)] - **benchmark**: add bar.R (Rafael Gonzaga) [#47729](https://github.com/nodejs/node/pull/47729)
* \[[`4f74be3c92`](https://github.com/nodejs/node/commit/4f74be3c92)] - **benchmark**: refactor crypto oneshot (Filip Skokan) [#48267](https://github.com/nodejs/node/pull/48267)
* \[[`fe9da9df0f`](https://github.com/nodejs/node/commit/fe9da9df0f)] - **benchmark**: add crypto.create\*Key (Filip Skokan) [#48284](https://github.com/nodejs/node/pull/48284)
* \[[`9cb18b3e9d`](https://github.com/nodejs/node/commit/9cb18b3e9d)] - **build**: do not pass target toolchain flags to host toolchain (Ivan Trubach) [#48597](https://github.com/nodejs/node/pull/48597)
* \[[`7dc731d4bf`](https://github.com/nodejs/node/commit/7dc731d4bf)] - **build**: sync libuv header change (Jiawen Geng) [#48078](https://github.com/nodejs/node/pull/48078)
* \[[`211a4f88a9`](https://github.com/nodejs/node/commit/211a4f88a9)] - **build**: update action to close stale PRs (Michael Dawson) [#48196](https://github.com/nodejs/node/pull/48196)
* \[[`cc33a1864b`](https://github.com/nodejs/node/commit/cc33a1864b)] - **child\_process**: harden against prototype pollution (Livia Medeiros) [#48726](https://github.com/nodejs/node/pull/48726)
* \[[`b5df084e1e`](https://github.com/nodejs/node/commit/b5df084e1e)] - **child\_process**: use addAbortListener (atlowChemi) [#48550](https://github.com/nodejs/node/pull/48550)
* \[[`611db8df1a`](https://github.com/nodejs/node/commit/611db8df1a)] - **child\_process**: support `Symbol.dispose` (Moshe Atlow) [#48551](https://github.com/nodejs/node/pull/48551)
* \[[`490fc004b0`](https://github.com/nodejs/node/commit/490fc004b0)] - **crypto**: update root certificates to NSS 3.93 (Node.js GitHub Bot) [#49341](https://github.com/nodejs/node/pull/49341)
* \[[`dd8cd97d4d`](https://github.com/nodejs/node/commit/dd8cd97d4d)] - **crypto**: update root certificates to NSS 3.90 (Node.js GitHub Bot) [#48416](https://github.com/nodejs/node/pull/48416)
* \[[`b2bc839d4c`](https://github.com/nodejs/node/commit/b2bc839d4c)] - **crypto**: remove OPENSSL\_FIPS guard for OpenSSL 3 (Richard Lau) [#48392](https://github.com/nodejs/node/pull/48392)
* \[[`c8da8c80b9`](https://github.com/nodejs/node/commit/c8da8c80b9)] - **deps**: update nghttp2 to 1.55.0 (Node.js GitHub Bot) [#48746](https://github.com/nodejs/node/pull/48746)
* \[[`7e04242dcb`](https://github.com/nodejs/node/commit/7e04242dcb)] - **deps**: update minimatch to 9.0.3 (Node.js GitHub Bot) [#48704](https://github.com/nodejs/node/pull/48704)
* \[[`ea23870bec`](https://github.com/nodejs/node/commit/ea23870bec)] - **deps**: add missing thread-common.c in uv.gyp (Santiago Gimeno) [#48078](https://github.com/nodejs/node/pull/48078)
* \[[`88855e0b1b`](https://github.com/nodejs/node/commit/88855e0b1b)] - **deps**: upgrade to libuv 1.46.0 (Santiago Gimeno) [#48078](https://github.com/nodejs/node/pull/48078)
* \[[`fb2b80fca0`](https://github.com/nodejs/node/commit/fb2b80fca0)] - **deps**: upgrade to libuv 1.45.0 (Santiago Gimeno) [#48078](https://github.com/nodejs/node/pull/48078)
* \[[`59fca4e09a`](https://github.com/nodejs/node/commit/59fca4e09a)] - **deps**: update acorn to 8.10.0 (Node.js GitHub Bot) [#48713](https://github.com/nodejs/node/pull/48713)
* \[[`bcb255d5a8`](https://github.com/nodejs/node/commit/bcb255d5a8)] - **deps**: V8: cherry-pick cb00db4dba6c (Keyhan Vakil) [#48671](https://github.com/nodejs/node/pull/48671)
* \[[`65a6c90fc6`](https://github.com/nodejs/node/commit/65a6c90fc6)] - **deps**: update acorn to 8.9.0 (Node.js GitHub Bot) [#48484](https://github.com/nodejs/node/pull/48484)
* \[[`6b6d5d91e9`](https://github.com/nodejs/node/commit/6b6d5d91e9)] - **deps**: update zlib to 1.2.13.1-motley-f81f385 (Node.js GitHub Bot) [#48541](https://github.com/nodejs/node/pull/48541)
* \[[`56249b0770`](https://github.com/nodejs/node/commit/56249b0770)] - **deps**: update googletest to ec4fed9 (Node.js GitHub Bot) [#48538](https://github.com/nodejs/node/pull/48538)
* \[[`8914a5204a`](https://github.com/nodejs/node/commit/8914a5204a)] - **deps**: update minimatch to 9.0.2 (Node.js GitHub Bot) [#48542](https://github.com/nodejs/node/pull/48542)
* \[[`1b960d9988`](https://github.com/nodejs/node/commit/1b960d9988)] - **deps**: update icu to 73.2 (Node.js GitHub Bot) [#48502](https://github.com/nodejs/node/pull/48502)
* \[[`f0e2e3c549`](https://github.com/nodejs/node/commit/f0e2e3c549)] - **deps**: update zlib to 1.2.13.1-motley-3ca9f16 (Node.js GitHub Bot) [#48413](https://github.com/nodejs/node/pull/48413)
* \[[`9cf8fe6b93`](https://github.com/nodejs/node/commit/9cf8fe6b93)] - **deps**: upgrade npm to 9.8.1 (npm team) [#48838](https://github.com/nodejs/node/pull/48838)
* \[[`d9ff473ff3`](https://github.com/nodejs/node/commit/d9ff473ff3)] - **deps**: upgrade npm to 9.8.0 (npm team) [#48665](https://github.com/nodejs/node/pull/48665)
* \[[`4a6177daad`](https://github.com/nodejs/node/commit/4a6177daad)] - **deps**: upgrade npm to 9.7.2 (npm team) [#48514](https://github.com/nodejs/node/pull/48514)
* \[[`104b58feb1`](https://github.com/nodejs/node/commit/104b58feb1)] - **deps**: update ada to 2.6.0 (Node.js GitHub Bot) [#48896](https://github.com/nodejs/node/pull/48896)
* \[[`7f7a125d78`](https://github.com/nodejs/node/commit/7f7a125d78)] - **deps**: update corepack to 0.19.0 (Node.js GitHub Bot) [#48540](https://github.com/nodejs/node/pull/48540)
* \[[`5e1eb451d1`](https://github.com/nodejs/node/commit/5e1eb451d1)] - **deps**: update corepack to 0.18.1 (Node.js GitHub Bot) [#48483](https://github.com/nodejs/node/pull/48483)
* \[[`3be53358bc`](https://github.com/nodejs/node/commit/3be53358bc)] - **deps**: add loong64 config into openssl gypi (Shi Pujin) [#48043](https://github.com/nodejs/node/pull/48043)
* \[[`555982c59e`](https://github.com/nodejs/node/commit/555982c59e)] - **deps**: upgrade npm to 9.7.1 (npm team) [#48378](https://github.com/nodejs/node/pull/48378)
* \[[`3c03ec0832`](https://github.com/nodejs/node/commit/3c03ec0832)] - **deps**: update simdutf to 3.2.14 (Node.js GitHub Bot) [#48344](https://github.com/nodejs/node/pull/48344)
* \[[`a2964a4583`](https://github.com/nodejs/node/commit/a2964a4583)] - **deps**: update ada to 2.5.1 (Node.js GitHub Bot) [#48319](https://github.com/nodejs/node/pull/48319)
* \[[`38f6e0d8cd`](https://github.com/nodejs/node/commit/38f6e0d8cd)] - **deps**: update zlib to 982b036 (Node.js GitHub Bot) [#48327](https://github.com/nodejs/node/pull/48327)
* \[[`f4617a4f81`](https://github.com/nodejs/node/commit/f4617a4f81)] - **deps**: add loongarch64 into openssl Makefile and gen openssl-loongarch64 (Shi Pujin) [#46401](https://github.com/nodejs/node/pull/46401)
* \[[`573eb4be12`](https://github.com/nodejs/node/commit/573eb4be12)] - **dgram**: socket add `asyncDispose` (atlowChemi) [#48717](https://github.com/nodejs/node/pull/48717)
* \[[`f3c4300e00`](https://github.com/nodejs/node/commit/f3c4300e00)] - **dgram**: use addAbortListener (atlowChemi) [#48550](https://github.com/nodejs/node/pull/48550)
* \[[`d3041df738`](https://github.com/nodejs/node/commit/d3041df738)] - **doc**: expand on squashing and rebasing to land a PR (Chengzhong Wu) [#48751](https://github.com/nodejs/node/pull/48751)
* \[[`249879e46c`](https://github.com/nodejs/node/commit/249879e46c)] - **doc**: add atlowChemi to collaborators (atlowChemi) [#48757](https://github.com/nodejs/node/pull/48757)
* \[[`42ecd46d1f`](https://github.com/nodejs/node/commit/42ecd46d1f)] - **doc**: fix ambiguity in http.md and https.md (an5er) [#48692](https://github.com/nodejs/node/pull/48692)
* \[[`e78824e053`](https://github.com/nodejs/node/commit/e78824e053)] - **doc**: add release key for Ulises Gascon (Ulises Gascón) [#49196](https://github.com/nodejs/node/pull/49196)
* \[[`1aa798d69f`](https://github.com/nodejs/node/commit/1aa798d69f)] - **doc**: clarify transform.\_transform() callback argument logic (Rafael Sofi-zada) [#48680](https://github.com/nodejs/node/pull/48680)
* \[[`d723e870a2`](https://github.com/nodejs/node/commit/d723e870a2)] - **doc**: mention git node release prepare (Rafael Gonzaga) [#48644](https://github.com/nodejs/node/pull/48644)
* \[[`a9a1394388`](https://github.com/nodejs/node/commit/a9a1394388)] - **doc**: fix options order (Luigi Pinca) [#48617](https://github.com/nodejs/node/pull/48617)
* \[[`989ea6858f`](https://github.com/nodejs/node/commit/989ea6858f)] - **doc**: update security release stewards (Rafael Gonzaga) [#48569](https://github.com/nodejs/node/pull/48569)
* \[[`f436ac1803`](https://github.com/nodejs/node/commit/f436ac1803)] - **doc**: update return type for describe (Shrujal Shah) [#48572](https://github.com/nodejs/node/pull/48572)
* \[[`fbe89e6320`](https://github.com/nodejs/node/commit/fbe89e6320)] - **doc**: run license-builder (github-actions\[bot]) [#48552](https://github.com/nodejs/node/pull/48552)
* \[[`f18b287bc3`](https://github.com/nodejs/node/commit/f18b287bc3)] - **doc**: add description of autoAllocateChunkSize in ReadableStream (Debadree Chatterjee) [#48004](https://github.com/nodejs/node/pull/48004)
* \[[`e2f3ed1444`](https://github.com/nodejs/node/commit/e2f3ed1444)] - **doc**: fix `filename` type in `watch` result (Dmitry Semigradsky) [#48032](https://github.com/nodejs/node/pull/48032)
* \[[`1fe75dc2b0`](https://github.com/nodejs/node/commit/1fe75dc2b0)] - **doc**: unnest `mime` and `MIMEParams` from MIMEType constructor (Dmitry Semigradsky) [#47950](https://github.com/nodejs/node/pull/47950)
* \[[`e1339d58e8`](https://github.com/nodejs/node/commit/e1339d58e8)] - **doc**: update security-release-process.md (Rafael Gonzaga) [#48504](https://github.com/nodejs/node/pull/48504)
* \[[`e8dc7bde6a`](https://github.com/nodejs/node/commit/e8dc7bde6a)] - **doc**: add vmoroz to collaborators (Vladimir Morozov) [#48527](https://github.com/nodejs/node/pull/48527)
* \[[`f8ba672c7b`](https://github.com/nodejs/node/commit/f8ba672c7b)] - **doc**: link to Runtime Keys in export conditions (Jacob Hummer) [#48408](https://github.com/nodejs/node/pull/48408)
* \[[`0056cb93e9`](https://github.com/nodejs/node/commit/0056cb93e9)] - **doc**: update fs flags documentation (sinkhaha) [#48463](https://github.com/nodejs/node/pull/48463)
* \[[`3cf3fb9479`](https://github.com/nodejs/node/commit/3cf3fb9479)] - **doc**: revise `error.md` introduction (Antoine du Hamel) [#48423](https://github.com/nodejs/node/pull/48423)
* \[[`7575d8b90e`](https://github.com/nodejs/node/commit/7575d8b90e)] - **doc**: add preveen-stack to triagers (Preveen P) [#48387](https://github.com/nodejs/node/pull/48387)
* \[[`820aa550a4`](https://github.com/nodejs/node/commit/820aa550a4)] - **doc**: refine when file is undefined in test events (Moshe Atlow) [#48451](https://github.com/nodejs/node/pull/48451)
* \[[`a30f2fbcc1`](https://github.com/nodejs/node/commit/a30f2fbcc1)] - **doc**: add kvakil to collaborators (Keyhan Vakil) [#48449](https://github.com/nodejs/node/pull/48449)
* \[[`239b4ea66f`](https://github.com/nodejs/node/commit/239b4ea66f)] - **doc**: mark `--import` as experimental (Moshe Atlow) [#44067](https://github.com/nodejs/node/pull/44067)
* \[[`2a561aefe2`](https://github.com/nodejs/node/commit/2a561aefe2)] - **doc**: add additional info on TSFN dispatch (Michael Dawson) [#48367](https://github.com/nodejs/node/pull/48367)
* \[[`5cc6eee30d`](https://github.com/nodejs/node/commit/5cc6eee30d)] - **doc**: add link for news from security wg (Michael Dawson) [#48396](https://github.com/nodejs/node/pull/48396)
* \[[`ffece88452`](https://github.com/nodejs/node/commit/ffece88452)] - **doc**: fix typo in events.md (Darshan Sen) [#48436](https://github.com/nodejs/node/pull/48436)
* \[[`06513585dc`](https://github.com/nodejs/node/commit/06513585dc)] - **doc**: run license-builder (github-actions\[bot]) [#48336](https://github.com/nodejs/node/pull/48336)
* \[[`d9a800ee5c`](https://github.com/nodejs/node/commit/d9a800ee5c)] - **esm**: fix emit deprecation on legacy main resolve (Antoine du Hamel) [#48664](https://github.com/nodejs/node/pull/48664)
* \[[`c39b7c240e`](https://github.com/nodejs/node/commit/c39b7c240e)] - **(SEMVER-MINOR)** **esm**: add `--import` flag (Moshe Atlow) [#43942](https://github.com/nodejs/node/pull/43942)
* \[[`a00464ee06`](https://github.com/nodejs/node/commit/a00464ee06)] - **esm**: fix specifier resolution and symlinks (Zack Newsham) [#47674](https://github.com/nodejs/node/pull/47674)
* \[[`3b8ec348b0`](https://github.com/nodejs/node/commit/3b8ec348b0)] - **events**: fix bug listenerCount don't compare wrapped listener (yuzheng14) [#48592](https://github.com/nodejs/node/pull/48592)
* \[[`a68a67f54d`](https://github.com/nodejs/node/commit/a68a67f54d)] - **(SEMVER-MINOR)** **events**: allow safely adding listener to abortSignal (Chemi Atlow) [#48596](https://github.com/nodejs/node/pull/48596)
* \[[`5354af3dab`](https://github.com/nodejs/node/commit/5354af3dab)] - **fs**: call the callback with an error if writeSync fails (killa) [#47949](https://github.com/nodejs/node/pull/47949)
* \[[`c3a27d1d3d`](https://github.com/nodejs/node/commit/c3a27d1d3d)] - **fs**: remove unneeded return statement (Luigi Pinca) [#48526](https://github.com/nodejs/node/pull/48526)
* \[[`3a8586bee2`](https://github.com/nodejs/node/commit/3a8586bee2)] - **fs, stream**: initial `Symbol.dispose` and `Symbol.asyncDispose` support (Moshe Atlow) [#48518](https://github.com/nodejs/node/pull/48518)
* \[[`01746c71df`](https://github.com/nodejs/node/commit/01746c71df)] - **http**: null the joinDuplicateHeaders property on cleanup (Luigi Pinca) [#48608](https://github.com/nodejs/node/pull/48608)
* \[[`d47eb73a85`](https://github.com/nodejs/node/commit/d47eb73a85)] - **http**: remove useless ternary in test (geekreal) [#48481](https://github.com/nodejs/node/pull/48481)
* \[[`977e9a38b4`](https://github.com/nodejs/node/commit/977e9a38b4)] - **http**: fix for handling on boot timers headers and request (Franciszek Koltuniuk) [#48291](https://github.com/nodejs/node/pull/48291)
* \[[`be88f7cd22`](https://github.com/nodejs/node/commit/be88f7cd22)] - **http2**: use addAbortListener (atlowChemi) [#48550](https://github.com/nodejs/node/pull/48550)
* \[[`7c7230a85c`](https://github.com/nodejs/node/commit/7c7230a85c)] - **http2**: send RST code 8 on AbortController signal (Devraj Mehta) [#48573](https://github.com/nodejs/node/pull/48573)
* \[[`f74c2fc72a`](https://github.com/nodejs/node/commit/f74c2fc72a)] - **lib**: use addAbortListener (atlowChemi) [#48550](https://github.com/nodejs/node/pull/48550)
* \[[`db355d1f37`](https://github.com/nodejs/node/commit/db355d1f37)] - **lib**: add option to force handling stopped events (Chemi Atlow) [#48301](https://github.com/nodejs/node/pull/48301)
* \[[`5d682c55a5`](https://github.com/nodejs/node/commit/5d682c55a5)] - **lib**: reduce url getters on `makeRequireFunction` (Yagiz Nizipli) [#48492](https://github.com/nodejs/node/pull/48492)
* \[[`5260f53e55`](https://github.com/nodejs/node/commit/5260f53e55)] - **lib**: add support for inherited custom inspection methods (Antoine du Hamel) [#48306](https://github.com/nodejs/node/pull/48306)
* \[[`69aaf8b1d1`](https://github.com/nodejs/node/commit/69aaf8b1d1)] - **lib**: remove invalid parameter to toASCII (Yagiz Nizipli) [#48878](https://github.com/nodejs/node/pull/48878)
* \[[`51863b80e4`](https://github.com/nodejs/node/commit/51863b80e4)] - **meta**: bump actions/checkout from 3.5.2 to 3.5.3 (dependabot\[bot]) [#48625](https://github.com/nodejs/node/pull/48625)
* \[[`7ec370991d`](https://github.com/nodejs/node/commit/7ec370991d)] - **meta**: bump step-security/harden-runner from 2.4.0 to 2.4.1 (dependabot\[bot]) [#48626](https://github.com/nodejs/node/pull/48626)
* \[[`34b8e980d4`](https://github.com/nodejs/node/commit/34b8e980d4)] - **meta**: bump ossf/scorecard-action from 2.1.3 to 2.2.0 (dependabot\[bot]) [#48628](https://github.com/nodejs/node/pull/48628)
* \[[`dfed9a7da9`](https://github.com/nodejs/node/commit/dfed9a7da9)] - **meta**: bump github/codeql-action from 2.3.6 to 2.20.1 (dependabot\[bot]) [#48627](https://github.com/nodejs/node/pull/48627)
* \[[`071eaadc5a`](https://github.com/nodejs/node/commit/071eaadc5a)] - **module**: add SourceMap.findOrigin (Isaac Z. Schlueter) [#47790](https://github.com/nodejs/node/pull/47790)
* \[[`bf1525c549`](https://github.com/nodejs/node/commit/bf1525c549)] - **module**: reduce url invocations in esm/load.js (Yagiz Nizipli) [#48337](https://github.com/nodejs/node/pull/48337)
* \[[`f8921630a2`](https://github.com/nodejs/node/commit/f8921630a2)] - **net**: server add `asyncDispose` (atlowChemi) [#48717](https://github.com/nodejs/node/pull/48717)
* \[[`b5f53d9a0b`](https://github.com/nodejs/node/commit/b5f53d9a0b)] - **net**: fix family autoselection SSL connection handling (Paolo Insogna) [#48189](https://github.com/nodejs/node/pull/48189)
* \[[`267439fc34`](https://github.com/nodejs/node/commit/267439fc34)] - **net**: rework autoSelectFamily implementation (Paolo Insogna) [#46587](https://github.com/nodejs/node/pull/46587)
* \[[`d3637cdbbf`](https://github.com/nodejs/node/commit/d3637cdbbf)] - **net**: fix address iteration with autoSelectFamily (Fedor Indutny) [#48258](https://github.com/nodejs/node/pull/48258)
* \[[`e8289a83f1`](https://github.com/nodejs/node/commit/e8289a83f1)] - **net**: fix family autoselection timeout handling (Paolo Insogna) [#47860](https://github.com/nodejs/node/pull/47860)
* \[[`863bdb785d`](https://github.com/nodejs/node/commit/863bdb785d)] - **net**: add autoSelectFamily global getter and setter (Paolo Insogna) [#45777](https://github.com/nodejs/node/pull/45777)
* \[[`04dc090bfa`](https://github.com/nodejs/node/commit/04dc090bfa)] - **node-api**: provide napi\_define\_properties fast path (Gabriel Schulhof) [#48440](https://github.com/nodejs/node/pull/48440)
* \[[`feb6a54dc3`](https://github.com/nodejs/node/commit/feb6a54dc3)] - **node-api**: implement external strings (Gabriel Schulhof) [#48339](https://github.com/nodejs/node/pull/48339)
* \[[`121f74c463`](https://github.com/nodejs/node/commit/121f74c463)] - **perf\_hooks**: convert maxSize to IDL value in setResourceTimingBufferSize (Chengzhong Wu) [#44902](https://github.com/nodejs/node/pull/44902)
* \[[`804d880589`](https://github.com/nodejs/node/commit/804d880589)] - **permission**: fix data types in PrintTree (Tobias Nießen) [#48770](https://github.com/nodejs/node/pull/48770)
* \[[`7aaecce9bf`](https://github.com/nodejs/node/commit/7aaecce9bf)] - **permission**: add debug log when inserting fs nodes (Rafael Gonzaga) [#48677](https://github.com/nodejs/node/pull/48677)
* \[[`cb51ef2905`](https://github.com/nodejs/node/commit/cb51ef2905)] - **readline**: use addAbortListener (atlowChemi) [#48550](https://github.com/nodejs/node/pull/48550)
* \[[`07065d0814`](https://github.com/nodejs/node/commit/07065d0814)] - **report**: disable js stack when no context is entered (Chengzhong Wu) [#48495](https://github.com/nodejs/node/pull/48495)
* \[[`572b82ffef`](https://github.com/nodejs/node/commit/572b82ffef)] - **src**: make BaseObject iteration order deterministic (Joyee Cheung) [#48702](https://github.com/nodejs/node/pull/48702)
* \[[`3f65598a41`](https://github.com/nodejs/node/commit/3f65598a41)] - **src**: remove kEagerCompile for CompileFunction (Keyhan Vakil) [#48671](https://github.com/nodejs/node/pull/48671)
* \[[`f43eacac9b`](https://github.com/nodejs/node/commit/f43eacac9b)] - **src**: deduplicate X509 getter implementations (Tobias Nießen) [#48563](https://github.com/nodejs/node/pull/48563)
* \[[`0c19621bdc`](https://github.com/nodejs/node/commit/0c19621bdc)] - **src**: fix uninitialized field access in AsyncHooks (Jan Olaf Krems) [#48566](https://github.com/nodejs/node/pull/48566)
* \[[`0c38184d62`](https://github.com/nodejs/node/commit/0c38184d62)] - **src**: fix Coverity issue regarding unnecessary copy (Yagiz Nizipli) [#48565](https://github.com/nodejs/node/pull/48565)
* \[[`0d73009ba3`](https://github.com/nodejs/node/commit/0d73009ba3)] - **src**: refactor `SplitString` in util (Yagiz Nizipli) [#48491](https://github.com/nodejs/node/pull/48491)
* \[[`6c72622df9`](https://github.com/nodejs/node/commit/6c72622df9)] - **src**: handle wasm out of bound in osx will raise SIGBUS correctly (Congcong Cai) [#46561](https://github.com/nodejs/node/pull/46561)
* \[[`e4261809b0`](https://github.com/nodejs/node/commit/e4261809b0)] - **src**: replace idna functions with ada::idna (Yagiz Nizipli) [#47735](https://github.com/nodejs/node/pull/47735)
* \[[`3dd82b1820`](https://github.com/nodejs/node/commit/3dd82b1820)] - **stream**: use addAbortListener (atlowChemi) [#48550](https://github.com/nodejs/node/pull/48550)
* \[[`786fbdb824`](https://github.com/nodejs/node/commit/786fbdb824)] - **stream**: fix premature pipeline end (Robert Nagy) [#48435](https://github.com/nodejs/node/pull/48435)
* \[[`c224e1b255`](https://github.com/nodejs/node/commit/c224e1b255)] - **stream**: fix deadlock when pipeing to full sink (Robert Nagy) [#48691](https://github.com/nodejs/node/pull/48691)
* \[[`2c75b9ece2`](https://github.com/nodejs/node/commit/2c75b9ece2)] - **test**: fix flaky test-string-decode.js on x86 (Stefan Stojanovic) [#48750](https://github.com/nodejs/node/pull/48750)
* \[[`279c4f64c1`](https://github.com/nodejs/node/commit/279c4f64c1)] - **test**: mark test-http-regr-gh-2928 as flaky (Joyee Cheung) [#49565](https://github.com/nodejs/node/pull/49565)
* \[[`01eacccd9a`](https://github.com/nodejs/node/commit/01eacccd9a)] - **test**: deflake test-net-throttle (Luigi Pinca) [#48599](https://github.com/nodejs/node/pull/48599)
* \[[`33886b271c`](https://github.com/nodejs/node/commit/33886b271c)] - **test**: move test-net-throttle to parallel (Luigi Pinca) [#48599](https://github.com/nodejs/node/pull/48599)
* \[[`a79112b5f4`](https://github.com/nodejs/node/commit/a79112b5f4)] - _**Revert**_ "**test**: remove test-crypto-keygen flaky designation" (Luigi Pinca) [#48652](https://github.com/nodejs/node/pull/48652)
* \[[`6ec57984db`](https://github.com/nodejs/node/commit/6ec57984db)] - **test**: add missing assertions to test-runner-cli (Moshe Atlow) [#48593](https://github.com/nodejs/node/pull/48593)
* \[[`dd1805e802`](https://github.com/nodejs/node/commit/dd1805e802)] - **test**: remove test-crypto-keygen flaky designation (Luigi Pinca) [#48575](https://github.com/nodejs/node/pull/48575)
* \[[`df9a9afc99`](https://github.com/nodejs/node/commit/df9a9afc99)] - **test**: remove test-timers-immediate-queue flaky designation (Luigi Pinca) [#48575](https://github.com/nodejs/node/pull/48575)
* \[[`3ae96ae380`](https://github.com/nodejs/node/commit/3ae96ae380)] - **test**: make IsolateData per-isolate in cctest (Joyee Cheung) [#48450](https://github.com/nodejs/node/pull/48450)
* \[[`f2ce8e0c06`](https://github.com/nodejs/node/commit/f2ce8e0c06)] - **test**: define NAPI\_VERSION before including node\_api.h (Chengzhong Wu) [#48376](https://github.com/nodejs/node/pull/48376)
* \[[`13ac0a5e26`](https://github.com/nodejs/node/commit/13ac0a5e26)] - **test**: remove unnecessary noop function args to `mustNotCall()` (Antoine du Hamel) [#48513](https://github.com/nodejs/node/pull/48513)
* \[[`8fdd4c55b3`](https://github.com/nodejs/node/commit/8fdd4c55b3)] - **test**: skip test-runner-watch-mode on IBMi (Moshe Atlow) [#48473](https://github.com/nodejs/node/pull/48473)
* \[[`9d90409241`](https://github.com/nodejs/node/commit/9d90409241)] - **test**: fix flaky test-watch-mode (Moshe Atlow) [#48147](https://github.com/nodejs/node/pull/48147)
* \[[`27a4bc7c32`](https://github.com/nodejs/node/commit/27a4bc7c32)] - **test**: add missing \<algorithm> include for std::find (Sam James) [#48380](https://github.com/nodejs/node/pull/48380)
* \[[`cb92c4b9fe`](https://github.com/nodejs/node/commit/cb92c4b9fe)] - **test**: update url web-platform tests (Yagiz Nizipli) [#48319](https://github.com/nodejs/node/pull/48319)
* \[[`f35c4d3190`](https://github.com/nodejs/node/commit/f35c4d3190)] - **test**: ignore the copied entry\_point.c (Luigi Pinca) [#48297](https://github.com/nodejs/node/pull/48297)
* \[[`41d1e6888f`](https://github.com/nodejs/node/commit/41d1e6888f)] - **test**: refactor test-gc-http-client-timeout (Luigi Pinca) [#48292](https://github.com/nodejs/node/pull/48292)
* \[[`125bca621a`](https://github.com/nodejs/node/commit/125bca621a)] - **test**: update encoding web-platform tests (Yagiz Nizipli) [#48320](https://github.com/nodejs/node/pull/48320)
* \[[`e9ac111d02`](https://github.com/nodejs/node/commit/e9ac111d02)] - **test**: update FileAPI web-platform tests (Yagiz Nizipli) [#48322](https://github.com/nodejs/node/pull/48322)
* \[[`3da57d17f5`](https://github.com/nodejs/node/commit/3da57d17f5)] - **test**: update user-timing web-platform tests (Yagiz Nizipli) [#48321](https://github.com/nodejs/node/pull/48321)
* \[[`c728b8a29b`](https://github.com/nodejs/node/commit/c728b8a29b)] - **test**: fix `test-net-autoselectfamily` for kernel without IPv6 support (Livia Medeiros) [#45856](https://github.com/nodejs/node/pull/45856)
* \[[`6de7aa1d19`](https://github.com/nodejs/node/commit/6de7aa1d19)] - **test**: move `test-tls-autoselectfamily-servername` to `test/internet` (Antoine du Hamel) [#47029](https://github.com/nodejs/node/pull/47029)
* \[[`2de9868292`](https://github.com/nodejs/node/commit/2de9868292)] - **test**: validate host with commas on url.parse (Yagiz Nizipli) [#48878](https://github.com/nodejs/node/pull/48878)
* \[[`e7d2e8ef2a`](https://github.com/nodejs/node/commit/e7d2e8ef2a)] - **test**: delete test-net-bytes-per-incoming-chunk-overhead (Michaël Zasso) [#48811](https://github.com/nodejs/node/pull/48811)
* \[[`f5494fa1b0`](https://github.com/nodejs/node/commit/f5494fa1b0)] - **test\_runner**: fixed `test` shorthands return type (Shocker) [#48555](https://github.com/nodejs/node/pull/48555)
* \[[`7051cafdfa`](https://github.com/nodejs/node/commit/7051cafdfa)] - **test\_runner**: make `--test-name-pattern` recursive (Moshe Atlow) [#48382](https://github.com/nodejs/node/pull/48382)
* \[[`f302286442`](https://github.com/nodejs/node/commit/f302286442)] - **test\_runner**: refactor coverage report output for readability (Damien Seguin) [#47791](https://github.com/nodejs/node/pull/47791)
* \[[`7822a541e5`](https://github.com/nodejs/node/commit/7822a541e5)] - **timers**: support Symbol.dispose (Moshe Atlow) [#48633](https://github.com/nodejs/node/pull/48633)
* \[[`3eeca52db1`](https://github.com/nodejs/node/commit/3eeca52db1)] - **tls**: fix bugs of double TLS (rogertyang) [#48969](https://github.com/nodejs/node/pull/48969)
* \[[`4826379516`](https://github.com/nodejs/node/commit/4826379516)] - **tools**: run fetch\_deps.py with Python 3 (Richard Lau) [#48729](https://github.com/nodejs/node/pull/48729)
* \[[`e2688c8d79`](https://github.com/nodejs/node/commit/e2688c8d79)] - **tools**: update doc to unist-util-select\@5.0.0 unist-util-visit\@5.0.0 (Node.js GitHub Bot) [#48714](https://github.com/nodejs/node/pull/48714)
* \[[`7399481096`](https://github.com/nodejs/node/commit/7399481096)] - **tools**: update lint-md-dependencies to rollup\@3.26.2 (Node.js GitHub Bot) [#48705](https://github.com/nodejs/node/pull/48705)
* \[[`31c07153ce`](https://github.com/nodejs/node/commit/31c07153ce)] - **tools**: update eslint to 8.44.0 (Node.js GitHub Bot) [#48632](https://github.com/nodejs/node/pull/48632)
* \[[`4e53f51e24`](https://github.com/nodejs/node/commit/4e53f51e24)] - **tools**: update lint-md-dependencies to rollup\@3.26.0 (Node.js GitHub Bot) [#48631](https://github.com/nodejs/node/pull/48631)
* \[[`7d52950a96`](https://github.com/nodejs/node/commit/7d52950a96)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#48544](https://github.com/nodejs/node/pull/48544)
* \[[`e168eab3ee`](https://github.com/nodejs/node/commit/e168eab3ee)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#48486](https://github.com/nodejs/node/pull/48486)
* \[[`9711bc24f6`](https://github.com/nodejs/node/commit/9711bc24f6)] - **tools**: replace sed with perl (Luigi Pinca) [#48499](https://github.com/nodejs/node/pull/48499)
* \[[`9c1937c0a7`](https://github.com/nodejs/node/commit/9c1937c0a7)] - **tools**: update eslint to 8.43.0 (Node.js GitHub Bot) [#48487](https://github.com/nodejs/node/pull/48487)
* \[[`9449f05ab1`](https://github.com/nodejs/node/commit/9449f05ab1)] - **tools**: update doc to to-vfile\@8.0.0 (Node.js GitHub Bot) [#48485](https://github.com/nodejs/node/pull/48485)
* \[[`79dcd968b1`](https://github.com/nodejs/node/commit/79dcd968b1)] - **tools**: prepare tools/doc for to-vfile 8.0.0 (Rich Trott) [#48485](https://github.com/nodejs/node/pull/48485)
* \[[`538f388ac0`](https://github.com/nodejs/node/commit/538f388ac0)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#48417](https://github.com/nodejs/node/pull/48417)
* \[[`01bc10dcd5`](https://github.com/nodejs/node/commit/01bc10dcd5)] - **tools**: update create-or-update-pull-request-action (Richard Lau) [#48398](https://github.com/nodejs/node/pull/48398)
* \[[`590a072657`](https://github.com/nodejs/node/commit/590a072657)] - **tools**: update eslint-plugin-jsdoc (Richard Lau) [#48393](https://github.com/nodejs/node/pull/48393)
* \[[`6a5805491e`](https://github.com/nodejs/node/commit/6a5805491e)] - **tools**: update eslint to 8.42.0 (Node.js GitHub Bot) [#48328](https://github.com/nodejs/node/pull/48328)
* \[[`2eb13e3986`](https://github.com/nodejs/node/commit/2eb13e3986)] - **tools**: disable jsdoc/no-defaults rule (Luigi Pinca) [#48328](https://github.com/nodejs/node/pull/48328)
* \[[`3363cfa6c7`](https://github.com/nodejs/node/commit/3363cfa6c7)] - **typings**: remove unused primordials (Yagiz Nizipli) [#48509](https://github.com/nodejs/node/pull/48509)
* \[[`c59ae86ba0`](https://github.com/nodejs/node/commit/c59ae86ba0)] - **(SEMVER-MINOR)** **url**: add value argument to has and delete methods (Sankalp Shubham) [#47885](https://github.com/nodejs/node/pull/47885)
* \[[`f59c9636f4`](https://github.com/nodejs/node/commit/f59c9636f4)] - **url**: conform to origin getter spec changes (Yagiz Nizipli) [#48319](https://github.com/nodejs/node/pull/48319)
* \[[`0beb5ab93d`](https://github.com/nodejs/node/commit/0beb5ab93d)] - **url**: ensure getter access do not mutate observable symbols (Antoine du Hamel) [#48897](https://github.com/nodejs/node/pull/48897)
* \[[`0a022c496d`](https://github.com/nodejs/node/commit/0a022c496d)] - **util**: use `primordials.ArrayPrototypeIndexOf` instead of mutable method (DaisyDogs07) [#48586](https://github.com/nodejs/node/pull/48586)

<a id="18.17.1"></a>

## 2023-08-09, Version 18.17.1 'Hydrogen' (LTS), @RafaelGSS

This is a security release.

### Notable Changes

The following CVEs are fixed in this release:

* [CVE-2023-32002](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-32002):  Policies can be bypassed via Module.\_load (High)
* [CVE-2023-32006](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-32006): Policies can be bypassed by module.constructor.createRequire (Medium)
* [CVE-2023-32559](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-32559): Policies can be bypassed via process.binding (Medium)
* OpenSSL Security Releases
  * [OpenSSL security advisory 14th July](https://mta.openssl.org/pipermail/openssl-announce/2023-July/000264.html).
  * [OpenSSL security advisory 19th July](https://mta.openssl.org/pipermail/openssl-announce/2023-July/000265.html).
  * [OpenSSL security advisory 31st July](https://mta.openssl.org/pipermail/openssl-announce/2023-July/000267.html)

More detailed information on each of the vulnerabilities can be found in [August 2023 Security Releases](https://nodejs.org/en/blog/vulnerability/august-2023-security-releases/) blog post.

### Commits

* \[[`fe3abdf82e`](https://github.com/nodejs/node/commit/fe3abdf82e)] - **deps**: update archs files for openssl-3.0.10+quic1 (Node.js GitHub Bot) [#49036](https://github.com/nodejs/node/pull/49036)
* \[[`2c5a522d9c`](https://github.com/nodejs/node/commit/2c5a522d9c)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.10+quic1 (Node.js GitHub Bot) [#49036](https://github.com/nodejs/node/pull/49036)
* \[[`15bced0bde`](https://github.com/nodejs/node/commit/15bced0bde)] - **policy**: handle Module.constructor and main.extensions bypass (RafaelGSS) [nodejs-private/node-private#417](https://github.com/nodejs-private/node-private/pull/417)
* \[[`d4570fae35`](https://github.com/nodejs/node/commit/d4570fae35)] - **policy**: disable process.binding() when enabled (Tobias Nießen) [nodejs-private/node-private#460](https://github.com/nodejs-private/node-private/pull/460)

<a id="18.17.0"></a>

## 2023-07-18, Version 18.17.0 'Hydrogen' (LTS), @danielleadams

### Notable Changes

#### Ada 2.0

Node.js v18.17.0 comes with the latest version of the URL parser, Ada. This update brings significant performance improvements
to URL parsing, including enhancements to the url.domainToASCII and url.domainToUnicode functions in node:url.

Ada 2.0 has been integrated into the Node.js codebase, ensuring that all parts of the application can benefit from the
improved performance. Additionally, Ada 2.0 features a significant performance boost over its predecessor, Ada 1.0.4,
while also eliminating the need for the ICU requirement for URL hostname parsing.

Contributed by Yagiz Nizipli and Daniel Lemire in [#47339](https://github.com/nodejs/node/pull/47339)

#### Web Crypto API

Web Crypto API functions' arguments are now coerced and validated as per their WebIDL definitions like in other Web Crypto API implementations.
This further improves interoperability with other implementations of Web Crypto API.

Contributed by Filip Skokan in [#46067](https://github.com/nodejs/node/pull/46067)

* **crypto**:
  * update root certificates to NSS 3.89 (Node.js GitHub Bot) [#47659](https://github.com/nodejs/node/pull/47659)
* **dns**:
  * **(SEMVER-MINOR)** expose getDefaultResultOrder (btea) [#46973](https://github.com/nodejs/node/pull/46973)
* **doc**:
  * add ovflowd to collaborators (Claudio Wunder) [#47844](https://github.com/nodejs/node/pull/47844)
  * add KhafraDev to collaborators (Matthew Aitken) [#47510](https://github.com/nodejs/node/pull/47510)
* **events**:
  * **(SEMVER-MINOR)** add getMaxListeners method (Matthew Aitken) [#47039](https://github.com/nodejs/node/pull/47039)
* **fs**:
  * **(SEMVER-MINOR)** add support for mode flag to specify the copy behavior (Tetsuharu Ohzeki) [#47084](https://github.com/nodejs/node/pull/47084)
  * **(SEMVER-MINOR)** add recursive option to readdir and opendir (Ethan Arrowood) [#41439](https://github.com/nodejs/node/pull/41439)
  * **(SEMVER-MINOR)** add support for mode flag to specify the copy behavior (Tetsuharu Ohzeki) [#47084](https://github.com/nodejs/node/pull/47084)
  * **(SEMVER-MINOR)** implement byob mode for readableWebStream() (Debadree Chatterjee) [#46933](https://github.com/nodejs/node/pull/46933)
* **http**:
  * **(SEMVER-MINOR)** prevent writing to the body when not allowed by HTTP spec (Gerrard Lindsay) [#47732](https://github.com/nodejs/node/pull/47732)
  * **(SEMVER-MINOR)** remove internal error in assignSocket (Matteo Collina) [#47723](https://github.com/nodejs/node/pull/47723)
  * **(SEMVER-MINOR)** add highWaterMark opt in http.createServer (HinataKah0) [#47405](https://github.com/nodejs/node/pull/47405)
* **lib**:
  * **(SEMVER-MINOR)** add webstreams to Duplex.from() (Debadree Chatterjee) [#46190](https://github.com/nodejs/node/pull/46190)
  * **(SEMVER-MINOR)** implement AbortSignal.any() (Chemi Atlow) [#47821](https://github.com/nodejs/node/pull/47821)
* **module**:
  * change default resolver to not throw on unknown scheme (Gil Tayar) [#47824](https://github.com/nodejs/node/pull/47824)
* **node-api**:
  * **(SEMVER-MINOR)** define version 9 (Chengzhong Wu) [#48151](https://github.com/nodejs/node/pull/48151)
  * **(SEMVER-MINOR)** deprecate napi\_module\_register (Vladimir Morozov) [#46319](https://github.com/nodejs/node/pull/46319)
* **stream**:
  * **(SEMVER-MINOR)** preserve object mode in compose (Raz Luvaton) [#47413](https://github.com/nodejs/node/pull/47413)
  * **(SEMVER-MINOR)** add setter & getter for default highWaterMark (#46929) (Robert Nagy) [#46929](https://github.com/nodejs/node/pull/46929)
* **test**:
  * unflake test-vm-timeout-escape-nexttick (Santiago Gimeno) [#48078](https://github.com/nodejs/node/pull/48078)
* **test\_runner**:
  * **(SEMVER-MINOR)** add shorthands to `test` (Chemi Atlow) [#47909](https://github.com/nodejs/node/pull/47909)
  * **(SEMVER-MINOR)** support combining coverage reports (Colin Ihrig) [#47686](https://github.com/nodejs/node/pull/47686)
  * **(SEMVER-MINOR)** execute before hook on test (Chemi Atlow) [#47586](https://github.com/nodejs/node/pull/47586)
  * **(SEMVER-MINOR)** expose reporter for use in run api (Chemi Atlow) [#47238](https://github.com/nodejs/node/pull/47238)
* **tools**:
  * update LICENSE and license-builder.sh (Santiago Gimeno) [#48078](https://github.com/nodejs/node/pull/48078)
* **url**:
  * **(SEMVER-MINOR)** implement URL.canParse (Matthew Aitken) [#47179](https://github.com/nodejs/node/pull/47179)
* **wasi**:
  * **(SEMVER-MINOR)** no longer require flag to enable wasi (Michael Dawson) [#47286](https://github.com/nodejs/node/pull/47286)

### Commits

* \[[`2ba08ac002`](https://github.com/nodejs/node/commit/2ba08ac002)] - **benchmark**: use `cluster.isPrimary` instead of `cluster.isMaster` (Deokjin Kim) [#48002](https://github.com/nodejs/node/pull/48002)
* \[[`60ca69d96c`](https://github.com/nodejs/node/commit/60ca69d96c)] - **benchmark**: add eventtarget creation bench (Rafael Gonzaga) [#47774](https://github.com/nodejs/node/pull/47774)
* \[[`d8233d96bb`](https://github.com/nodejs/node/commit/d8233d96bb)] - **benchmark**: add a benchmark for `defaultResolve` (Antoine du Hamel) [#47543](https://github.com/nodejs/node/pull/47543)
* \[[`a1aabb6912`](https://github.com/nodejs/node/commit/a1aabb6912)] - **benchmark**: fix invalid requirementsURL (Deokjin Kim) [#47378](https://github.com/nodejs/node/pull/47378)
* \[[`394c61caf9`](https://github.com/nodejs/node/commit/394c61caf9)] - **bootstrap**: support namespaced builtins in snapshot scripts (Joyee Cheung) [#47467](https://github.com/nodejs/node/pull/47467)
* \[[`0165a765a0`](https://github.com/nodejs/node/commit/0165a765a0)] - **bootstrap**: do not expand process.argv\[1] for snapshot entry points (Joyee Cheung) [#47466](https://github.com/nodejs/node/pull/47466)
* \[[`cca557cdd9`](https://github.com/nodejs/node/commit/cca557cdd9)] - **buffer**: combine checking range of sourceStart in `buf.copy` (Deokjin Kim) [#47758](https://github.com/nodejs/node/pull/47758)
* \[[`4c69be467c`](https://github.com/nodejs/node/commit/4c69be467c)] - **buffer**: use private properties for brand checks in File (Matthew Aitken) [#47154](https://github.com/nodejs/node/pull/47154)
* \[[`d002f9b6e2`](https://github.com/nodejs/node/commit/d002f9b6e2)] - **build**: revert unkonwn ruff selector (Moshe Atlow) [#48753](https://github.com/nodejs/node/pull/48753)
* \[[`93f77cb762`](https://github.com/nodejs/node/commit/93f77cb762)] - **build**: set v8\_enable\_webassembly=false when lite mode is enabled (Cheng Shao) [#48248](https://github.com/nodejs/node/pull/48248)
* \[[`1662e894f3`](https://github.com/nodejs/node/commit/1662e894f3)] - **build**: add action to close stale PRs (Michael Dawson) [#48051](https://github.com/nodejs/node/pull/48051)
* \[[`5ca437b288`](https://github.com/nodejs/node/commit/5ca437b288)] - **build**: use pathlib for paths (Mohammed Keyvanzadeh) [#47581](https://github.com/nodejs/node/pull/47581)
* \[[`72443bc54b`](https://github.com/nodejs/node/commit/72443bc54b)] - **build**: refactor configure.py (Mohammed Keyvanzadeh) [#47667](https://github.com/nodejs/node/pull/47667)
* \[[`d4eecb5be9`](https://github.com/nodejs/node/commit/d4eecb5be9)] - **build**: add devcontainer configuration (Tierney Cyren) [#40825](https://github.com/nodejs/node/pull/40825)
* \[[`803ed41144`](https://github.com/nodejs/node/commit/803ed41144)] - **build**: bump ossf/scorecard-action from 2.1.2 to 2.1.3 (dependabot\[bot]) [#47367](https://github.com/nodejs/node/pull/47367)
* \[[`48468c4413`](https://github.com/nodejs/node/commit/48468c4413)] - **build**: replace Python linter flake8 with ruff (Christian Clauss) [#47519](https://github.com/nodejs/node/pull/47519)
* \[[`3ceb2c4387`](https://github.com/nodejs/node/commit/3ceb2c4387)] - **build**: add node-core-utils to setup (Jiawen Geng) [#47442](https://github.com/nodejs/node/pull/47442)
* \[[`fdc59b8e14`](https://github.com/nodejs/node/commit/fdc59b8e14)] - **build**: bump github/codeql-action from 2.2.6 to 2.2.9 (dependabot\[bot]) [#47366](https://github.com/nodejs/node/pull/47366)
* \[[`3924893023`](https://github.com/nodejs/node/commit/3924893023)] - **build**: update stale action from v7 to v8 (Rich Trott) [#47357](https://github.com/nodejs/node/pull/47357)
* \[[`753185c5b0`](https://github.com/nodejs/node/commit/753185c5b0)] - **build**: remove Python pip `--no-user` option (Christian Clauss) [#47372](https://github.com/nodejs/node/pull/47372)
* \[[`67af0a6a2b`](https://github.com/nodejs/node/commit/67af0a6a2b)] - **build**: avoid usage of pipes library (Mohammed Keyvanzadeh) [#47271](https://github.com/nodejs/node/pull/47271)
* \[[`db910dd6b2`](https://github.com/nodejs/node/commit/db910dd6b2)] - **build, deps, tools**: avoid excessive LTO (Konstantin Demin) [#47313](https://github.com/nodejs/node/pull/47313)
* \[[`35d1def891`](https://github.com/nodejs/node/commit/35d1def891)] - **child\_process**: use signal.reason in child process abort (Debadree Chatterjee) [#47817](https://github.com/nodejs/node/pull/47817)
* \[[`7692d2e7b9`](https://github.com/nodejs/node/commit/7692d2e7b9)] - **cluster**: use ObjectPrototypeHasOwnProperty (Daeyeon Jeong) [#48141](https://github.com/nodejs/node/pull/48141)
* \[[`7617772762`](https://github.com/nodejs/node/commit/7617772762)] - **crypto**: use openssl's own memory BIOs in crypto\_context.cc (GauriSpears) [#47160](https://github.com/nodejs/node/pull/47160)
* \[[`8cabfe7c6e`](https://github.com/nodejs/node/commit/8cabfe7c6e)] - **crypto**: fix setEngine() when OPENSSL\_NO\_ENGINE set (Tobias Nießen) [#47977](https://github.com/nodejs/node/pull/47977)
* \[[`de1338da05`](https://github.com/nodejs/node/commit/de1338da05)] - **crypto**: fix webcrypto private/secret import with empty usages (Filip Skokan) [#47877](https://github.com/nodejs/node/pull/47877)
* \[[`27a696fda9`](https://github.com/nodejs/node/commit/27a696fda9)] - **crypto**: update root certificates to NSS 3.89 (Node.js GitHub Bot) [#47659](https://github.com/nodejs/node/pull/47659)
* \[[`e2292f936e`](https://github.com/nodejs/node/commit/e2292f936e)] - **crypto**: remove INT\_MAX restriction in randomBytes (Tobias Nießen) [#47559](https://github.com/nodejs/node/pull/47559)
* \[[`a5f214c00c`](https://github.com/nodejs/node/commit/a5f214c00c)] - **crypto**: replace THROW with CHECK for scrypt keylen (Tobias Nießen) [#47407](https://github.com/nodejs/node/pull/47407)
* \[[`dd42214fd4`](https://github.com/nodejs/node/commit/dd42214fd4)] - **crypto**: unify validation of checkPrime checks (Tobias Nießen) [#47165](https://github.com/nodejs/node/pull/47165)
* \[[`76e4d12fb3`](https://github.com/nodejs/node/commit/76e4d12fb3)] - **crypto**: re-add padding for AES-KW wrapped JWKs (Filip Skokan) [#46563](https://github.com/nodejs/node/pull/46563)
* \[[`9d894c17dd`](https://github.com/nodejs/node/commit/9d894c17dd)] - **crypto**: use WebIDL converters in WebCryptoAPI (Filip Skokan) [#46067](https://github.com/nodejs/node/pull/46067)
* \[[`6f3a8b45a5`](https://github.com/nodejs/node/commit/6f3a8b45a5)] - **deps**: update ada to 2.5.0 (Node.js GitHub Bot) [#48223](https://github.com/nodejs/node/pull/48223)
* \[[`075b6db919`](https://github.com/nodejs/node/commit/075b6db919)] - **deps**: update ada to 2.4.2 (Node.js GitHub Bot) [#48092](https://github.com/nodejs/node/pull/48092)
* \[[`a4ee1f652c`](https://github.com/nodejs/node/commit/a4ee1f652c)] - **deps**: update ada to 2.4.1 (Node.js GitHub Bot) [#48036](https://github.com/nodejs/node/pull/48036)
* \[[`81b514d3f0`](https://github.com/nodejs/node/commit/81b514d3f0)] - **deps**: update ada to 2.4.0 (Node.js GitHub Bot) [#47922](https://github.com/nodejs/node/pull/47922)
* \[[`575ddf694f`](https://github.com/nodejs/node/commit/575ddf694f)] - **deps**: update ada to 2.3.1 (Node.js GitHub Bot) [#47893](https://github.com/nodejs/node/pull/47893)
* \[[`2d03d5f458`](https://github.com/nodejs/node/commit/2d03d5f458)] - **deps**: update ada to 2.3.0 (Node.js GitHub Bot) [#47737](https://github.com/nodejs/node/pull/47737)
* \[[`42e690f2d5`](https://github.com/nodejs/node/commit/42e690f2d5)] - **deps**: update ada to 2.2.0 (Node.js GitHub Bot) [#47678](https://github.com/nodejs/node/pull/47678)
* \[[`08dd271521`](https://github.com/nodejs/node/commit/08dd271521)] - **deps**: update ada to 2.1.0 (Node.js GitHub Bot) [#47598](https://github.com/nodejs/node/pull/47598)
* \[[`96c50ba71f`](https://github.com/nodejs/node/commit/96c50ba71f)] - **deps**: update ada to 2.0.0 (Node.js GitHub Bot) [#47339](https://github.com/nodejs/node/pull/47339)
* \[[`4d1c38b758`](https://github.com/nodejs/node/commit/4d1c38b758)] - **deps**: update zlib to 337322d (Node.js GitHub Bot) [#48218](https://github.com/nodejs/node/pull/48218)
* \[[`74206b2549`](https://github.com/nodejs/node/commit/74206b2549)] - **deps**: update histogram 0.11.8 (Marco Ippolito) [#47742](https://github.com/nodejs/node/pull/47742)
* \[[`fbb4b3775d`](https://github.com/nodejs/node/commit/fbb4b3775d)] - **deps**: update histogram to 0.11.7 (Marco Ippolito) [#47742](https://github.com/nodejs/node/pull/47742)
* \[[`e88c079022`](https://github.com/nodejs/node/commit/e88c079022)] - **deps**: update simdutf to 3.2.12 (Node.js GitHub Bot) [#48118](https://github.com/nodejs/node/pull/48118)
* \[[`48bd1248b9`](https://github.com/nodejs/node/commit/48bd1248b9)] - **deps**: update minimatch to 9.0.1 (Node.js GitHub Bot) [#48094](https://github.com/nodejs/node/pull/48094)
* \[[`d4572d31fa`](https://github.com/nodejs/node/commit/d4572d31fa)] - **deps**: update corepack to 0.18.0 (Node.js GitHub Bot) [#48091](https://github.com/nodejs/node/pull/48091)
* \[[`8090d29dc4`](https://github.com/nodejs/node/commit/8090d29dc4)] - **deps**: update uvwasi to 0.0.18 (Node.js GitHub Bot) [#47866](https://github.com/nodejs/node/pull/47866)
* \[[`169c8eea2e`](https://github.com/nodejs/node/commit/169c8eea2e)] - **deps**: update uvwasi to 0.0.17 (Node.js GitHub Bot) [#47866](https://github.com/nodejs/node/pull/47866)
* \[[`6acbb23380`](https://github.com/nodejs/node/commit/6acbb23380)] - **deps**: upgrade npm to 9.6.7 (npm team) [#48062](https://github.com/nodejs/node/pull/48062)
* \[[`e8f2c0a58b`](https://github.com/nodejs/node/commit/e8f2c0a58b)] - **deps**: update undici to 5.22.1 (Node.js GitHub Bot) [#47994](https://github.com/nodejs/node/pull/47994)
* \[[`9309fd3120`](https://github.com/nodejs/node/commit/9309fd3120)] - **deps**: update simdutf to 3.2.9 (Node.js GitHub Bot) [#47983](https://github.com/nodejs/node/pull/47983)
* \[[`b796d3560a`](https://github.com/nodejs/node/commit/b796d3560a)] - **deps**: upgrade npm to 9.6.6 (npm team) [#47862](https://github.com/nodejs/node/pull/47862)
* \[[`cce372e14e`](https://github.com/nodejs/node/commit/cce372e14e)] - **deps**: V8: cherry-pick c5ab3e4f0c5a (Richard Lau) [#47736](https://github.com/nodejs/node/pull/47736)
* \[[`7283486adb`](https://github.com/nodejs/node/commit/7283486adb)] - **deps**: update undici to 5.22.0 (Node.js GitHub Bot) [#47679](https://github.com/nodejs/node/pull/47679)
* \[[`2ea6e03003`](https://github.com/nodejs/node/commit/2ea6e03003)] - **deps**: add minimatch as a dependency (Moshe Atlow) [#47499](https://github.com/nodejs/node/pull/47499)
* \[[`261e1d23d1`](https://github.com/nodejs/node/commit/261e1d23d1)] - **deps**: update ICU to 73.1 release (Steven R. Loomis) [#47456](https://github.com/nodejs/node/pull/47456)
* \[[`f532f9df5f`](https://github.com/nodejs/node/commit/f532f9df5f)] - **deps**: update undici to 5.21.2 (Node.js GitHub Bot) [#47508](https://github.com/nodejs/node/pull/47508)
* \[[`dcb8c038b9`](https://github.com/nodejs/node/commit/dcb8c038b9)] - **deps**: update simdutf to 3.2.8 (Node.js GitHub Bot) [#47507](https://github.com/nodejs/node/pull/47507)
* \[[`6c8456d61f`](https://github.com/nodejs/node/commit/6c8456d61f)] - **deps**: update undici to 5.21.1 (Node.js GitHub Bot) [#47488](https://github.com/nodejs/node/pull/47488)
* \[[`d3b2e8a438`](https://github.com/nodejs/node/commit/d3b2e8a438)] - **deps**: update simdutf to 3.2.7 (Node.js GitHub Bot) [#47473](https://github.com/nodejs/node/pull/47473)
* \[[`64a5fe0499`](https://github.com/nodejs/node/commit/64a5fe0499)] - **deps**: update corepack to 0.17.2 (Node.js GitHub Bot) [#47474](https://github.com/nodejs/node/pull/47474)
* \[[`6f0f61a7d3`](https://github.com/nodejs/node/commit/6f0f61a7d3)] - **deps**: upgrade npm to 9.6.4 (npm team) [#47432](https://github.com/nodejs/node/pull/47432)
* \[[`443a72e207`](https://github.com/nodejs/node/commit/443a72e207)] - **deps**: update zlib to upstream 5edb52d4 (Luigi Pinca) [#47151](https://github.com/nodejs/node/pull/47151)
* \[[`dc3bc46914`](https://github.com/nodejs/node/commit/dc3bc46914)] - **deps**: update simdutf to 3.2.3 (Node.js GitHub Bot) [#47331](https://github.com/nodejs/node/pull/47331)
* \[[`b2f2bebbc2`](https://github.com/nodejs/node/commit/b2f2bebbc2)] - **deps**: update timezone to 2023c (Node.js GitHub Bot) [#47302](https://github.com/nodejs/node/pull/47302)
* \[[`c10729ffa7`](https://github.com/nodejs/node/commit/c10729ffa7)] - **deps**: upgrade npm to 9.6.3 (npm team) [#47325](https://github.com/nodejs/node/pull/47325)
* \[[`420deac1de`](https://github.com/nodejs/node/commit/420deac1de)] - **deps**: update corepack to 0.17.1 (Node.js GitHub Bot) [#47156](https://github.com/nodejs/node/pull/47156)
* \[[`966ba28491`](https://github.com/nodejs/node/commit/966ba28491)] - **deps**: V8: cherry-pick 3e4952cb2a59 (Richard Lau) [#47236](https://github.com/nodejs/node/pull/47236)
* \[[`fc6ab26824`](https://github.com/nodejs/node/commit/fc6ab26824)] - **deps**: update timezone to 2023b (Node.js GitHub Bot) [#47256](https://github.com/nodejs/node/pull/47256)
* \[[`2700e70215`](https://github.com/nodejs/node/commit/2700e70215)] - **deps**: upgrade npm to 9.6.2 (npm team) [#47108](https://github.com/nodejs/node/pull/47108)
* \[[`29ba98a0a5`](https://github.com/nodejs/node/commit/29ba98a0a5)] - **deps**: V8: cherry-pick 975ff4dbfd1b (Debadree Chatterjee) [#47209](https://github.com/nodejs/node/pull/47209)
* \[[`be34777be8`](https://github.com/nodejs/node/commit/be34777be8)] - **deps**: cherry-pick win/arm64/clang fixes (Cheng Zhao) [#47011](https://github.com/nodejs/node/pull/47011)
* \[[`b52aacb614`](https://github.com/nodejs/node/commit/b52aacb614)] - **deps**: update uvwasi to v0.0.16 (Michael Dawson) [#46434](https://github.com/nodejs/node/pull/46434)
* \[[`27a76cf5e0`](https://github.com/nodejs/node/commit/27a76cf5e0)] - **deps,test**: update postject to 1.0.0-alpha.6 (Node.js GitHub Bot) [#48072](https://github.com/nodejs/node/pull/48072)
* \[[`b171c1a3a4`](https://github.com/nodejs/node/commit/b171c1a3a4)] - **dgram**: convert macro to template (Tobias Nießen) [#47891](https://github.com/nodejs/node/pull/47891)
* \[[`709bf1c758`](https://github.com/nodejs/node/commit/709bf1c758)] - **(SEMVER-MINOR)** **dns**: expose getDefaultResultOrder (btea) [#46973](https://github.com/nodejs/node/pull/46973)
* \[[`2f202c93e7`](https://github.com/nodejs/node/commit/2f202c93e7)] - **doc**: clarify array args to Buffer.from() (Bryan English) [#48274](https://github.com/nodejs/node/pull/48274)
* \[[`27f195f8d8`](https://github.com/nodejs/node/commit/27f195f8d8)] - **doc**: document watch option for node:test run() (Moshe Atlow) [#48256](https://github.com/nodejs/node/pull/48256)
* \[[`7558ef350a`](https://github.com/nodejs/node/commit/7558ef350a)] - **doc**: update documentation for FIPS support (Richard Lau) [#48194](https://github.com/nodejs/node/pull/48194)
* \[[`f2bb1919e5`](https://github.com/nodejs/node/commit/f2bb1919e5)] - **doc**: improve the documentation of the stdio option (Kumar Arnav) [#48110](https://github.com/nodejs/node/pull/48110)
* \[[`a2aa52ba92`](https://github.com/nodejs/node/commit/a2aa52ba92)] - **doc**: update Buffer.allocUnsafe description (sinkhaha) [#48183](https://github.com/nodejs/node/pull/48183)
* \[[`19ad471d52`](https://github.com/nodejs/node/commit/19ad471d52)] - **doc**: update codeowners with website team (Claudio Wunder) [#48197](https://github.com/nodejs/node/pull/48197)
* \[[`67b2c2a98f`](https://github.com/nodejs/node/commit/67b2c2a98f)] - **doc**: fix broken link to new folder doc/contributing/maintaining (Andrea Fassina) [#48205](https://github.com/nodejs/node/pull/48205)
* \[[`795ca70815`](https://github.com/nodejs/node/commit/795ca70815)] - **doc**: add atlowChemi to triagers (Chemi Atlow) [#48104](https://github.com/nodejs/node/pull/48104)
* \[[`e437a0aff1`](https://github.com/nodejs/node/commit/e437a0aff1)] - **doc**: fix typo in readline completer function section (Vadym) [#48188](https://github.com/nodejs/node/pull/48188)
* \[[`92e0ea496d`](https://github.com/nodejs/node/commit/92e0ea496d)] - **doc**: remove broken link for keygen (Rich Trott) [#48176](https://github.com/nodejs/node/pull/48176)
* \[[`fe15dae8e6`](https://github.com/nodejs/node/commit/fe15dae8e6)] - **doc**: add `auto` intrinsic height to prevent jitter/flicker (Daniel Holbert) [#48195](https://github.com/nodejs/node/pull/48195)
* \[[`230335e21f`](https://github.com/nodejs/node/commit/230335e21f)] - **doc**: add version info on the SEA docs (Antoine du Hamel) [#48173](https://github.com/nodejs/node/pull/48173)
* \[[`e6f37d1b80`](https://github.com/nodejs/node/commit/e6f37d1b80)] - **doc**: add Ruy to list of TSC members (Michael Dawson) [#48172](https://github.com/nodejs/node/pull/48172)
* \[[`69205a250c`](https://github.com/nodejs/node/commit/69205a250c)] - **doc**: update socket.remote\* properties documentation (Saba Kharanauli) [#48139](https://github.com/nodejs/node/pull/48139)
* \[[`e4a5d6298c`](https://github.com/nodejs/node/commit/e4a5d6298c)] - **doc**: update outdated section on TLSv1.3-PSK (Tobias Nießen) [#48123](https://github.com/nodejs/node/pull/48123)
* \[[`d14018ed99`](https://github.com/nodejs/node/commit/d14018ed99)] - **doc**: improve HMAC key recommendations (Tobias Nießen) [#48121](https://github.com/nodejs/node/pull/48121)
* \[[`e9d4baf770`](https://github.com/nodejs/node/commit/e9d4baf770)] - **doc**: clarify mkdir() recursive behavior (Stephen Odogwu) [#48109](https://github.com/nodejs/node/pull/48109)
* \[[`3e4a469139`](https://github.com/nodejs/node/commit/3e4a469139)] - **doc**: fix typo in crypto legacy streams API section (Tobias Nießen) [#48122](https://github.com/nodejs/node/pull/48122)
* \[[`bdf366ab88`](https://github.com/nodejs/node/commit/bdf366ab88)] - **doc**: update SEA source link (Rich Trott) [#48080](https://github.com/nodejs/node/pull/48080)
* \[[`2a4f79a75f`](https://github.com/nodejs/node/commit/2a4f79a75f)] - **doc**: clarify tty.isRaw (Roberto Vidal) [#48055](https://github.com/nodejs/node/pull/48055)
* \[[`98c6e4be03`](https://github.com/nodejs/node/commit/98c6e4be03)] - **doc**: use secure key length for HMAC generateKey (Tobias Nießen) [#48052](https://github.com/nodejs/node/pull/48052)
* \[[`8ae5c8cf9d`](https://github.com/nodejs/node/commit/8ae5c8cf9d)] - **doc**: update broken EVP\_BytesToKey link (Rich Trott) [#48064](https://github.com/nodejs/node/pull/48064)
* \[[`3c713e7caa`](https://github.com/nodejs/node/commit/3c713e7caa)] - **doc**: update broken spkac link (Rich Trott) [#48063](https://github.com/nodejs/node/pull/48063)
* \[[`c22f739e94`](https://github.com/nodejs/node/commit/c22f739e94)] - **doc**: document node-api version process (Chengzhong Wu) [#47972](https://github.com/nodejs/node/pull/47972)
* \[[`ce859f9f9f`](https://github.com/nodejs/node/commit/ce859f9f9f)] - **doc**: fix typo in binding functions (Deokjin Kim) [#48003](https://github.com/nodejs/node/pull/48003)
* \[[`070c3457b7`](https://github.com/nodejs/node/commit/070c3457b7)] - **doc**: mark Node.js 14 as End-of-Life (Richard Lau) [#48023](https://github.com/nodejs/node/pull/48023)
* \[[`3611027d8e`](https://github.com/nodejs/node/commit/3611027d8e)] - **doc**: clarify CRYPTO\_CUSTOM\_ENGINE\_NOT\_SUPPORTED (Tobias Nießen) [#47976](https://github.com/nodejs/node/pull/47976)
* \[[`dbffad958c`](https://github.com/nodejs/node/commit/dbffad958c)] - **doc**: add missing deprecated blocks to cluster (Tobias Nießen) [#47981](https://github.com/nodejs/node/pull/47981)
* \[[`035356f711`](https://github.com/nodejs/node/commit/035356f711)] - **doc**: update description of global (Tobias Nießen) [#47969](https://github.com/nodejs/node/pull/47969)
* \[[`081a6ffaea`](https://github.com/nodejs/node/commit/081a6ffaea)] - **doc**: update measure memory rejection information (Yash Ladha) [#41639](https://github.com/nodejs/node/pull/41639)
* \[[`3460cf9c23`](https://github.com/nodejs/node/commit/3460cf9c23)] - **doc**: fix broken link to TC39 import attributes proposal (Rich Trott) [#47954](https://github.com/nodejs/node/pull/47954)
* \[[`3b018c8aa9`](https://github.com/nodejs/node/commit/3b018c8aa9)] - **doc**: fix broken link (Rich Trott) [#47953](https://github.com/nodejs/node/pull/47953)
* \[[`244db960a9`](https://github.com/nodejs/node/commit/244db960a9)] - **doc**: remove broken link (Rich Trott) [#47942](https://github.com/nodejs/node/pull/47942)
* \[[`2cc8715bb9`](https://github.com/nodejs/node/commit/2cc8715bb9)] - **doc**: document make lint-md-clean (Matteo Collina) [#47926](https://github.com/nodejs/node/pull/47926)
* \[[`b80e006c17`](https://github.com/nodejs/node/commit/b80e006c17)] - **doc**: mark global object as legacy (Mert Can Altın) [#47819](https://github.com/nodejs/node/pull/47819)
* \[[`bf4eb058f3`](https://github.com/nodejs/node/commit/bf4eb058f3)] - **doc**: ntfs junction points must link to directories (Ben Noordhuis) [#47907](https://github.com/nodejs/node/pull/47907)
* \[[`49875f0d69`](https://github.com/nodejs/node/commit/49875f0d69)] - **doc**: fix params names (Dmitry Semigradsky) [#47853](https://github.com/nodejs/node/pull/47853)
* \[[`94b5eaaf17`](https://github.com/nodejs/node/commit/94b5eaaf17)] - **doc**: update supported version of FreeBSD to 12.4 (Michaël Zasso) [#47838](https://github.com/nodejs/node/pull/47838)
* \[[`0114201825`](https://github.com/nodejs/node/commit/0114201825)] - **doc**: swap Matteo with Rafael in the stewards (Rafael Gonzaga) [#47841](https://github.com/nodejs/node/pull/47841)
* \[[`8bcfcc0af9`](https://github.com/nodejs/node/commit/8bcfcc0af9)] - **doc**: add valgrind suppression details (Kevin Eady) [#47760](https://github.com/nodejs/node/pull/47760)
* \[[`75d397ecab`](https://github.com/nodejs/node/commit/75d397ecab)] - **doc**: replace EOL versions in README (Tobias Nießen) [#47833](https://github.com/nodejs/node/pull/47833)
* \[[`2b0c57cb80`](https://github.com/nodejs/node/commit/2b0c57cb80)] - **doc**: add ovflowd to collaborators (Claudio Wunder) [#47844](https://github.com/nodejs/node/pull/47844)
* \[[`be4966977c`](https://github.com/nodejs/node/commit/be4966977c)] - **doc**: update BUILDING.md previous versions links (Tobias Nießen) [#47835](https://github.com/nodejs/node/pull/47835)
* \[[`a9e8a20fb8`](https://github.com/nodejs/node/commit/a9e8a20fb8)] - **doc**: create maintaining folder for deps (Marco Ippolito) [#47589](https://github.com/nodejs/node/pull/47589)
* \[[`fd0f362d7c`](https://github.com/nodejs/node/commit/fd0f362d7c)] - **doc**: remove MoLow from Triagers (Moshe Atlow) [#47792](https://github.com/nodejs/node/pull/47792)
* \[[`0927c67ab6`](https://github.com/nodejs/node/commit/0927c67ab6)] - **doc**: fix typo in webstreams.md (Christian Takle) [#47766](https://github.com/nodejs/node/pull/47766)
* \[[`994be578da`](https://github.com/nodejs/node/commit/994be578da)] - **doc**: move BethGriggs to regular member (Rich Trott) [#47776](https://github.com/nodejs/node/pull/47776)
* \[[`64d19f4678`](https://github.com/nodejs/node/commit/64d19f4678)] - **doc**: move addaleax to TSC emeriti (Anna Henningsen) [#47752](https://github.com/nodejs/node/pull/47752)
* \[[`33ec10e6b8`](https://github.com/nodejs/node/commit/33ec10e6b8)] - **doc**: add link to news for Node.js core (Michael Dawson) [#47704](https://github.com/nodejs/node/pull/47704)
* \[[`2a682b5efe`](https://github.com/nodejs/node/commit/2a682b5efe)] - **doc**: async\_hooks asynchronous content example add mjs code (btea) [#47401](https://github.com/nodejs/node/pull/47401)
* \[[`4f541c3ca3`](https://github.com/nodejs/node/commit/4f541c3ca3)] - **doc**: clarify concurrency model of test runner (Tobias Nießen) [#47642](https://github.com/nodejs/node/pull/47642)
* \[[`ffcff68f0d`](https://github.com/nodejs/node/commit/ffcff68f0d)] - **doc**: fix typos (Mohammed Keyvanzadeh) [#47685](https://github.com/nodejs/node/pull/47685)
* \[[`290b2b7afc`](https://github.com/nodejs/node/commit/290b2b7afc)] - **doc**: fix capitalization of ASan (Mohammed Keyvanzadeh) [#47676](https://github.com/nodejs/node/pull/47676)
* \[[`b4ca788878`](https://github.com/nodejs/node/commit/b4ca788878)] - **doc**: fix typos in SECURITY.md (Mohammed Keyvanzadeh) [#47677](https://github.com/nodejs/node/pull/47677)
* \[[`971c545a47`](https://github.com/nodejs/node/commit/971c545a47)] - **doc**: update error code of buffer (Deokjin Kim) [#47617](https://github.com/nodejs/node/pull/47617)
* \[[`ec5c919928`](https://github.com/nodejs/node/commit/ec5c919928)] - **doc**: change offset of example in `Buffer.copyBytesFrom` (Deokjin Kim) [#47606](https://github.com/nodejs/node/pull/47606)
* \[[`980bf052c7`](https://github.com/nodejs/node/commit/980bf052c7)] - **doc**: remove markdown link from heading (Tobias Nießen) [#47585](https://github.com/nodejs/node/pull/47585)
* \[[`e96451ec5e`](https://github.com/nodejs/node/commit/e96451ec5e)] - **doc**: fix release-post script location (Rafael Gonzaga) [#47517](https://github.com/nodejs/node/pull/47517)
* \[[`61ea15339c`](https://github.com/nodejs/node/commit/61ea15339c)] - **doc**: add link for news from uvwasi team (Michael Dawson) [#47531](https://github.com/nodejs/node/pull/47531)
* \[[`d40bcdd73e`](https://github.com/nodejs/node/commit/d40bcdd73e)] - **doc**: add missing setEncoding call in ESM example (Anna Henningsen) [#47558](https://github.com/nodejs/node/pull/47558)
* \[[`924dc909b3`](https://github.com/nodejs/node/commit/924dc909b3)] - **doc**: fix typo in util.types.isNativeError() (Julian Dax) [#47532](https://github.com/nodejs/node/pull/47532)
* \[[`a24d72a6fb`](https://github.com/nodejs/node/commit/a24d72a6fb)] - **doc**: add KhafraDev to collaborators (Matthew Aitken) [#47510](https://github.com/nodejs/node/pull/47510)
* \[[`b0196378b6`](https://github.com/nodejs/node/commit/b0196378b6)] - **doc**: create maintaining-brotli.md (Marco Ippolito) [#47380](https://github.com/nodejs/node/pull/47380)
* \[[`3902be8fe8`](https://github.com/nodejs/node/commit/3902be8fe8)] - **doc**: info on handling unintended breaking changes (Michael Dawson) [#47426](https://github.com/nodejs/node/pull/47426)
* \[[`670f9a591d`](https://github.com/nodejs/node/commit/670f9a591d)] - **doc**: add performance initiative (Yagiz Nizipli) [#47424](https://github.com/nodejs/node/pull/47424)
* \[[`89a5d04a8e`](https://github.com/nodejs/node/commit/89a5d04a8e)] - **doc**: do not create a backup file (Luigi Pinca) [#47151](https://github.com/nodejs/node/pull/47151)
* \[[`7f2bccc5d6`](https://github.com/nodejs/node/commit/7f2bccc5d6)] - **doc**: add MoLow to the TSC (Colin Ihrig) [#47436](https://github.com/nodejs/node/pull/47436)
* \[[`7db2e889ac`](https://github.com/nodejs/node/commit/7db2e889ac)] - **doc**: add a note about os.cpus() returning an empty list (codedokode) [#47363](https://github.com/nodejs/node/pull/47363)
* \[[`289a8e30d6`](https://github.com/nodejs/node/commit/289a8e30d6)] - **doc**: clarify reports are only evaluated on active versions (Rafael Gonzaga) [#47341](https://github.com/nodejs/node/pull/47341)
* \[[`dc22edb4d2`](https://github.com/nodejs/node/commit/dc22edb4d2)] - **doc**: remove Vladimir de Turckheim from Security release stewards (Vladimir de Turckheim) [#47318](https://github.com/nodejs/node/pull/47318)
* \[[`3e74a74da3`](https://github.com/nodejs/node/commit/3e74a74da3)] - **doc**: add importing util to example of \`process.report.getReport' (Deokjin Kim) [#47298](https://github.com/nodejs/node/pull/47298)
* \[[`ece029f64e`](https://github.com/nodejs/node/commit/ece029f64e)] - **doc**: vm.SourceTextModule() without context option (Axel Kittenberger) [#47295](https://github.com/nodejs/node/pull/47295)
* \[[`c7227204cc`](https://github.com/nodejs/node/commit/c7227204cc)] - **doc**: document process for sharing project news (Michael Dawson) [#47189](https://github.com/nodejs/node/pull/47189)
* \[[`2865cbb4bd`](https://github.com/nodejs/node/commit/2865cbb4bd)] - **doc**: revise example of assert.CallTracker (Deokjin Kim) [#47252](https://github.com/nodejs/node/pull/47252)
* \[[`81ebaf2670`](https://github.com/nodejs/node/commit/81ebaf2670)] - **doc**: fix typo in SECURITY.md (Rich Trott) [#47282](https://github.com/nodejs/node/pull/47282)
* \[[`faabd48f11`](https://github.com/nodejs/node/commit/faabd48f11)] - **doc**: use serial comma in cli docs (Tobias Nießen) [#47262](https://github.com/nodejs/node/pull/47262)
* \[[`3a85794089`](https://github.com/nodejs/node/commit/3a85794089)] - **doc**: improve example for Error.captureStackTrace() (Julian Dax) [#46886](https://github.com/nodejs/node/pull/46886)
* \[[`2114fa472b`](https://github.com/nodejs/node/commit/2114fa472b)] - **doc**: clarify http error events after calling destroy() (Zach Bjornson) [#46903](https://github.com/nodejs/node/pull/46903)
* \[[`d15f522d1f`](https://github.com/nodejs/node/commit/d15f522d1f)] - **doc**: update output of example in AbortController (Deokjin Kim) [#47227](https://github.com/nodejs/node/pull/47227)
* \[[`ab6588e343`](https://github.com/nodejs/node/commit/ab6588e343)] - **doc**: drop one-week branch sync on major releases (Rafael Gonzaga) [#47149](https://github.com/nodejs/node/pull/47149)
* \[[`6ac52e3061`](https://github.com/nodejs/node/commit/6ac52e3061)] - **doc**: fix grammar in the collaborator guide (Mohammed Keyvanzadeh) [#47245](https://github.com/nodejs/node/pull/47245)
* \[[`13175774a6`](https://github.com/nodejs/node/commit/13175774a6)] - **doc**: update stream.reduce concurrency note (Raz Luvaton) [#47166](https://github.com/nodejs/node/pull/47166)
* \[[`1e97ccd6d4`](https://github.com/nodejs/node/commit/1e97ccd6d4)] - **doc**: remove use of DEFAULT\_ENCODING in PBKDF2 docs (Tobias Nießen) [#47181](https://github.com/nodejs/node/pull/47181)
* \[[`d562a7c461`](https://github.com/nodejs/node/commit/d562a7c461)] - **doc**: fix typos in async\_context.md (Shubham Sharma) [#47155](https://github.com/nodejs/node/pull/47155)
* \[[`9a11788cdf`](https://github.com/nodejs/node/commit/9a11788cdf)] - **doc**: update collaborator guide to reflect TSC changes (Rich Trott) [#47126](https://github.com/nodejs/node/pull/47126)
* \[[`5fc2bb763f`](https://github.com/nodejs/node/commit/5fc2bb763f)] - **doc**: clarify that `fs.create{Read,Write}Stream` support `AbortSignal` (Antoine du Hamel) [#47122](https://github.com/nodejs/node/pull/47122)
* \[[`2dd3b0213e`](https://github.com/nodejs/node/commit/2dd3b0213e)] - **doc**: improve documentation for util.types.isNativeError() (Julian Dax) [#46840](https://github.com/nodejs/node/pull/46840)
* \[[`ce4636e36b`](https://github.com/nodejs/node/commit/ce4636e36b)] - **doc**: rename the startup performance initiative to startup snapshot (#47111) (Joyee Cheung)
* \[[`309d017f15`](https://github.com/nodejs/node/commit/309d017f15)] - **doc**: fix "maintaining dependencies" heading typos (Keyhan Vakil) [#47082](https://github.com/nodejs/node/pull/47082)
* \[[`230a984eb3`](https://github.com/nodejs/node/commit/230a984eb3)] - **doc**: include webstreams in finished() and Duplex.from() parameters (Debadree Chatterjee) [#46312](https://github.com/nodejs/node/pull/46312)
* \[[`8651ea822e`](https://github.com/nodejs/node/commit/8651ea822e)] - **doc,fs**: update description of fs.stat() method (Mert Can Altın) [#47654](https://github.com/nodejs/node/pull/47654)
* \[[`e4539e1f19`](https://github.com/nodejs/node/commit/e4539e1f19)] - **doc,test**: update the v8.startupSnapshot doc and test the example (Joyee Cheung) [#47468](https://github.com/nodejs/node/pull/47468)
* \[[`3dddc0175f`](https://github.com/nodejs/node/commit/3dddc0175f)] - **doc,test**: fix concurrency option of test() (Tobias Nießen) [#47734](https://github.com/nodejs/node/pull/47734)
* \[[`563f9fe06a`](https://github.com/nodejs/node/commit/563f9fe06a)] - **doc,vm**: clarify usage of cachedData in vm.compileFunction() (Darshan Sen) [#48193](https://github.com/nodejs/node/pull/48193)
* \[[`316016ffac`](https://github.com/nodejs/node/commit/316016ffac)] - **esm**: avoid accessing lazy getters for urls (Yagiz Nizipli) [#47542](https://github.com/nodejs/node/pull/47542)
* \[[`e5e385d2b2`](https://github.com/nodejs/node/commit/e5e385d2b2)] - **esm**: increase test coverage of edge cases (Antoine du Hamel) [#47033](https://github.com/nodejs/node/pull/47033)
* \[[`061fb20660`](https://github.com/nodejs/node/commit/061fb20660)] - **(SEMVER-MINOR)** **events**: add getMaxListeners method (Matthew Aitken) [#47039](https://github.com/nodejs/node/pull/47039)
* \[[`ed0b62cc01`](https://github.com/nodejs/node/commit/ed0b62cc01)] - **(SEMVER-MINOR)** **fs**: add support for mode flag to specify the copy behavior (Tetsuharu Ohzeki) [#47084](https://github.com/nodejs/node/pull/47084)
* \[[`9b44c56c9a`](https://github.com/nodejs/node/commit/9b44c56c9a)] - **fs**: make readdir recursive algorithm iterative (Ethan Arrowood) [#47650](https://github.com/nodejs/node/pull/47650)
* \[[`7273ef53b3`](https://github.com/nodejs/node/commit/7273ef53b3)] - **(SEMVER-MINOR)** **fs**: add recursive option to readdir and opendir (Ethan Arrowood) [#41439](https://github.com/nodejs/node/pull/41439)
* \[[`3f0636d2c1`](https://github.com/nodejs/node/commit/3f0636d2c1)] - **(SEMVER-MINOR)** **fs**: add support for mode flag to specify the copy behavior (Tetsuharu Ohzeki) [#47084](https://github.com/nodejs/node/pull/47084)
* \[[`a0b9853251`](https://github.com/nodejs/node/commit/a0b9853251)] - **(SEMVER-MINOR)** **fs**: implement byob mode for readableWebStream() (Debadree Chatterjee) [#46933](https://github.com/nodejs/node/pull/46933)
* \[[`709e368708`](https://github.com/nodejs/node/commit/709e368708)] - **http**: send implicit headers on HEAD with no body (Matteo Collina) [#48108](https://github.com/nodejs/node/pull/48108)
* \[[`dc318f26c0`](https://github.com/nodejs/node/commit/dc318f26c0)] - **(SEMVER-MINOR)** **http**: prevent writing to the body when not allowed by HTTP spec (Gerrard Lindsay) [#47732](https://github.com/nodejs/node/pull/47732)
* \[[`4b2a015642`](https://github.com/nodejs/node/commit/4b2a015642)] - **(SEMVER-MINOR)** **http**: remove internal error in assignSocket (Matteo Collina) [#47723](https://github.com/nodejs/node/pull/47723)
* \[[`7cef6aa721`](https://github.com/nodejs/node/commit/7cef6aa721)] - **(SEMVER-MINOR)** **http**: add highWaterMark opt in http.createServer (HinataKah0) [#47405](https://github.com/nodejs/node/pull/47405)
* \[[`9186f3a0ef`](https://github.com/nodejs/node/commit/9186f3a0ef)] - **http2**: improve nghttp2 error callback (Tobias Nießen) [#47840](https://github.com/nodejs/node/pull/47840)
* \[[`cc7e5dd4cd`](https://github.com/nodejs/node/commit/cc7e5dd4cd)] - **inspector**: add tips for Session (theanarkh) [#47195](https://github.com/nodejs/node/pull/47195)
* \[[`70c0e882d3`](https://github.com/nodejs/node/commit/70c0e882d3)] - **inspector**: log response and requests in the inspector for debugging (Joyee Cheung) [#46941](https://github.com/nodejs/node/pull/46941)
* \[[`6099d2d08b`](https://github.com/nodejs/node/commit/6099d2d08b)] - **inspector**: fix session.disconnect crash (theanarkh) [#46942](https://github.com/nodejs/node/pull/46942)
* \[[`156292d44a`](https://github.com/nodejs/node/commit/156292d44a)] - **lib**: create weakRef only if any signals provided (Chemi Atlow) [#48448](https://github.com/nodejs/node/pull/48448)
* \[[`efaa073303`](https://github.com/nodejs/node/commit/efaa073303)] - **(SEMVER-MINOR)** **lib**: implement AbortSignal.any() (Chemi Atlow) [#47821](https://github.com/nodejs/node/pull/47821)
* \[[`c46b31f3bf`](https://github.com/nodejs/node/commit/c46b31f3bf)] - **lib**: support FORCE\_COLOR for non TTY streams (Moshe Atlow) [#48034](https://github.com/nodejs/node/pull/48034)
* \[[`286c358832`](https://github.com/nodejs/node/commit/286c358832)] - **lib**: do not disable linter for entire files (Antoine du Hamel) [#48299](https://github.com/nodejs/node/pull/48299)
* \[[`a2552ab7c0`](https://github.com/nodejs/node/commit/a2552ab7c0)] - **lib**: use existing `isWindows` variable (sinkhaha) [#48134](https://github.com/nodejs/node/pull/48134)
* \[[`2b65625281`](https://github.com/nodejs/node/commit/2b65625281)] - **lib**: update comment (sinkhaha) [#47884](https://github.com/nodejs/node/pull/47884)
* \[[`fa447b5120`](https://github.com/nodejs/node/commit/fa447b5120)] - **lib**: use webidl DOMString converter in EventTarget (Matthew Aitken) [#47514](https://github.com/nodejs/node/pull/47514)
* \[[`33f32dc318`](https://github.com/nodejs/node/commit/33f32dc318)] - **lib**: define Event.isTrusted in the prototype (Santiago Gimeno) [#46974](https://github.com/nodejs/node/pull/46974)
* \[[`50789a5e4a`](https://github.com/nodejs/node/commit/50789a5e4a)] - **lib**: refactor to use `validateBuffer` (Deokjin Kim) [#46489](https://github.com/nodejs/node/pull/46489)
* \[[`3658abea26`](https://github.com/nodejs/node/commit/3658abea26)] - **(SEMVER-MINOR)** **lib**: add webstreams to Duplex.from() (Debadree Chatterjee) [#46190](https://github.com/nodejs/node/pull/46190)
* \[[`fcf3781d22`](https://github.com/nodejs/node/commit/fcf3781d22)] - **lib,src,test**: lint codebase according new rules for v18.x (Juan José Arboleda) [#48697](https://github.com/nodejs/node/pull/48697)
* \[[`b55dc53422`](https://github.com/nodejs/node/commit/b55dc53422)] - **meta**: bump github/codeql-action from 2.3.3 to 2.3.6 (dependabot\[bot]) [#48287](https://github.com/nodejs/node/pull/48287)
* \[[`8ac4579d85`](https://github.com/nodejs/node/commit/8ac4579d85)] - **meta**: bump actions/setup-python from 4.6.0 to 4.6.1 (dependabot\[bot]) [#48286](https://github.com/nodejs/node/pull/48286)
* \[[`b1854fe9c1`](https://github.com/nodejs/node/commit/b1854fe9c1)] - **meta**: bump codecov/codecov-action from 3.1.3 to 3.1.4 (dependabot\[bot]) [#48285](https://github.com/nodejs/node/pull/48285)
* \[[`d9448b8d93`](https://github.com/nodejs/node/commit/d9448b8d93)] - **meta**: remove dont-land-on-v14 auto labeling (Shrujal Shah) [#48031](https://github.com/nodejs/node/pull/48031)
* \[[`bb859768b6`](https://github.com/nodejs/node/commit/bb859768b6)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#48010](https://github.com/nodejs/node/pull/48010)
* \[[`af90fb939b`](https://github.com/nodejs/node/commit/af90fb939b)] - **meta**: bump step-security/harden-runner from 2.3.1 to 2.4.0 (Rich Trott) [#47980](https://github.com/nodejs/node/pull/47980)
* \[[`4dcf5e2052`](https://github.com/nodejs/node/commit/4dcf5e2052)] - **meta**: bump github/codeql-action from 2.3.2 to 2.3.3 (Rich Trott) [#47979](https://github.com/nodejs/node/pull/47979)
* \[[`dab3186ea2`](https://github.com/nodejs/node/commit/dab3186ea2)] - **meta**: bump actions/setup-python from 4.5.0 to 4.6.0 (Rich Trott) [#47968](https://github.com/nodejs/node/pull/47968)
* \[[`546224c13c`](https://github.com/nodejs/node/commit/546224c13c)] - **meta**: add security-wg ping to permission.js (Rafael Gonzaga) [#47941](https://github.com/nodejs/node/pull/47941)
* \[[`353dfbd2d6`](https://github.com/nodejs/node/commit/353dfbd2d6)] - **meta**: bump step-security/harden-runner from 2.2.1 to 2.3.1 (dependabot\[bot]) [#47808](https://github.com/nodejs/node/pull/47808)
* \[[`20a5cc27ec`](https://github.com/nodejs/node/commit/20a5cc27ec)] - **meta**: bump actions/setup-python from 4.5.0 to 4.6.0 (dependabot\[bot]) [#47806](https://github.com/nodejs/node/pull/47806)
* \[[`eef6442d8d`](https://github.com/nodejs/node/commit/eef6442d8d)] - **meta**: bump actions/checkout from 3.3.0 to 3.5.2 (dependabot\[bot]) [#47805](https://github.com/nodejs/node/pull/47805)
* \[[`e30e6a718a`](https://github.com/nodejs/node/commit/e30e6a718a)] - **meta**: remove extra space in scorecard workflow (Mestery) [#47805](https://github.com/nodejs/node/pull/47805)
* \[[`2d13cdebc4`](https://github.com/nodejs/node/commit/2d13cdebc4)] - **meta**: bump github/codeql-action from 2.2.9 to 2.3.2 (dependabot\[bot]) [#47809](https://github.com/nodejs/node/pull/47809)
* \[[`f0d8352ed8`](https://github.com/nodejs/node/commit/f0d8352ed8)] - **meta**: bump codecov/codecov-action from 3.1.1 to 3.1.3 (dependabot\[bot]) [#47807](https://github.com/nodejs/node/pull/47807)
* \[[`7e95fba0ff`](https://github.com/nodejs/node/commit/7e95fba0ff)] - **meta**: fix dependabot commit message (Mestery) [#47810](https://github.com/nodejs/node/pull/47810)
* \[[`d31d9c7c28`](https://github.com/nodejs/node/commit/d31d9c7c28)] - **meta**: ping nodejs/startup for startup test changes (Joyee Cheung) [#47771](https://github.com/nodejs/node/pull/47771)
* \[[`077686055b`](https://github.com/nodejs/node/commit/077686055b)] - **meta**: add mailmap entry for KhafraDev (Rich Trott) [#47512](https://github.com/nodejs/node/pull/47512)
* \[[`e50eb6570a`](https://github.com/nodejs/node/commit/e50eb6570a)] - **meta**: ping security-wg team on permission model changes (Rafael Gonzaga) [#47483](https://github.com/nodejs/node/pull/47483)
* \[[`2df1a36214`](https://github.com/nodejs/node/commit/2df1a36214)] - **meta**: ping startup and realm team on src/node\_realm\* changes (Joyee Cheung) [#47448](https://github.com/nodejs/node/pull/47448)
* \[[`c7dc6e321b`](https://github.com/nodejs/node/commit/c7dc6e321b)] - **meta**: fix notable-change comment label url (Filip Skokan) [#47300](https://github.com/nodejs/node/pull/47300)
* \[[`e859ca44d5`](https://github.com/nodejs/node/commit/e859ca44d5)] - **meta**: clarify the threat model to explain the JSON.parse case (Matteo Collina) [#47276](https://github.com/nodejs/node/pull/47276)
* \[[`1f08f4848d`](https://github.com/nodejs/node/commit/1f08f4848d)] - **meta**: update link to collaborators discussion page (Michaël Zasso) [#47211](https://github.com/nodejs/node/pull/47211)
* \[[`3b524cbf86`](https://github.com/nodejs/node/commit/3b524cbf86)] - **meta**: automate description requests when notable change label is added (Danielle Adams) [#47078](https://github.com/nodejs/node/pull/47078)
* \[[`da16ca7c59`](https://github.com/nodejs/node/commit/da16ca7c59)] - **meta**: move TSC voting member(s) to regular member(s) (Node.js GitHub Bot) [#47180](https://github.com/nodejs/node/pull/47180)
* \[[`0c80e60d7e`](https://github.com/nodejs/node/commit/0c80e60d7e)] - **meta**: move TSC voting member to regular membership (Node.js GitHub Bot) [#46985](https://github.com/nodejs/node/pull/46985)
* \[[`0edcfed895`](https://github.com/nodejs/node/commit/0edcfed895)] - **meta**: update GOVERNANCE.md to reflect TSC charter changes (Rich Trott) [#47126](https://github.com/nodejs/node/pull/47126)
* \[[`baada5d035`](https://github.com/nodejs/node/commit/baada5d035)] - **meta**: ask expected behavior reason in bug template (Ben Noordhuis) [#47049](https://github.com/nodejs/node/pull/47049)
* \[[`5d75ec402e`](https://github.com/nodejs/node/commit/5d75ec402e)] - **module**: reduce the number of URL initializations (Yagiz Nizipli) [#48272](https://github.com/nodejs/node/pull/48272)
* \[[`c5af5a4f4f`](https://github.com/nodejs/node/commit/c5af5a4f4f)] - **module**: change default resolver to not throw on unknown scheme (Gil Tayar) [#47824](https://github.com/nodejs/node/pull/47824)
* \[[`cf8845d001`](https://github.com/nodejs/node/commit/cf8845d001)] - **module**: block requiring `test/reporters` without scheme (Moshe Atlow) [#47831](https://github.com/nodejs/node/pull/47831)
* \[[`ce7e6c6765`](https://github.com/nodejs/node/commit/ce7e6c6765)] - **(SEMVER-MINOR)** **node-api**: define version 9 (Chengzhong Wu) [#48151](https://github.com/nodejs/node/pull/48151)
* \[[`53c02b20b8`](https://github.com/nodejs/node/commit/53c02b20b8)] - **node-api**: add status napi\_cannot\_run\_js (Gabriel Schulhof) [#47986](https://github.com/nodejs/node/pull/47986)
* \[[`4b280d5361`](https://github.com/nodejs/node/commit/4b280d5361)] - **node-api**: napi\_ref on all types is experimental (Vladimir Morozov) [#47975](https://github.com/nodejs/node/pull/47975)
* \[[`e2553b12e7`](https://github.com/nodejs/node/commit/e2553b12e7)] - **(NODE-API-SEMVER-MAJOR)** **node-api**: get Node API version used by addon (Vladimir Morozov) [#45715](https://github.com/nodejs/node/pull/45715)
* \[[`beaad7f692`](https://github.com/nodejs/node/commit/beaad7f692)] - **node-api**: test passing NULL to napi\_define\_class (Gabriel Schulhof) [#47567](https://github.com/nodejs/node/pull/47567)
* \[[`6ab892780c`](https://github.com/nodejs/node/commit/6ab892780c)] - **node-api**: test passing NULL to number APIs (Gabriel Schulhof) [#47549](https://github.com/nodejs/node/pull/47549)
* \[[`a67e5ea89c`](https://github.com/nodejs/node/commit/a67e5ea89c)] - **node-api**: remove unused mark\_arraybuffer\_as\_untransferable (Chengzhong Wu) [#47557](https://github.com/nodejs/node/pull/47557)
* \[[`7019d48ba1`](https://github.com/nodejs/node/commit/7019d48ba1)] - **(SEMVER-MINOR)** **node-api**: deprecate napi\_module\_register (Vladimir Morozov) [#46319](https://github.com/nodejs/node/pull/46319)
* \[[`395c56bd7c`](https://github.com/nodejs/node/commit/395c56bd7c)] - **node-api**: extend type-tagging to externals (Gabriel Schulhof) [#47141](https://github.com/nodejs/node/pull/47141)
* \[[`6e66371eee`](https://github.com/nodejs/node/commit/6e66371eee)] - **node-api**: document node-api shutdown finalization (Chengzhong Wu) [#45903](https://github.com/nodejs/node/pull/45903)
* \[[`d8d2d33efb`](https://github.com/nodejs/node/commit/d8d2d33efb)] - **node-api**: verify cleanup hooks order (Chengzhong Wu) [#46692](https://github.com/nodejs/node/pull/46692)
* \[[`b34eaf393e`](https://github.com/nodejs/node/commit/b34eaf393e)] - **path**: indicate index of wrong resolve() parameter (sosoba) [#47660](https://github.com/nodejs/node/pull/47660)
* \[[`13bc5488a1`](https://github.com/nodejs/node/commit/13bc5488a1)] - **permission**: remove unused function declaration (Deokjin Kim) [#47957](https://github.com/nodejs/node/pull/47957)
* \[[`5f8aef477e`](https://github.com/nodejs/node/commit/5f8aef477e)] - **quic**: address recent coverity warning (Michael Dawson) [#47753](https://github.com/nodejs/node/pull/47753)
* \[[`75d7024ecd`](https://github.com/nodejs/node/commit/75d7024ecd)] - **quic**: add more QUIC implementation (James M Snell) [#47494](https://github.com/nodejs/node/pull/47494)
* \[[`24840832bd`](https://github.com/nodejs/node/commit/24840832bd)] - **quic**: add more QUIC impl (James M Snell) [#47348](https://github.com/nodejs/node/pull/47348)
* \[[`114b8479b4`](https://github.com/nodejs/node/commit/114b8479b4)] - **readline**: fix issue with newline-less last line (Ian Harris) [#47317](https://github.com/nodejs/node/pull/47317)
* \[[`e668efac6b`](https://github.com/nodejs/node/commit/e668efac6b)] - **repl**: preserve preview on ESCAPE key press (Xuguang Mei) [#46878](https://github.com/nodejs/node/pull/46878)
* \[[`7306b0f733`](https://github.com/nodejs/node/commit/7306b0f733)] - **sea**: fix memory leak detected by asan (Darshan Sen) [#47309](https://github.com/nodejs/node/pull/47309)
* \[[`1f2c91f98a`](https://github.com/nodejs/node/commit/1f2c91f98a)] - **src**: use std::array for passing argv in node::url (Anna Henningsen) [#47035](https://github.com/nodejs/node/pull/47035)
* \[[`36bf06904f`](https://github.com/nodejs/node/commit/36bf06904f)] - **src**: add Realm document in the src README.md (Chengzhong Wu) [#47932](https://github.com/nodejs/node/pull/47932)
* \[[`5445835671`](https://github.com/nodejs/node/commit/5445835671)] - **src**: check node\_extra\_ca\_certs after openssl cfg (Raghu Saxena) [#48159](https://github.com/nodejs/node/pull/48159)
* \[[`eb96856555`](https://github.com/nodejs/node/commit/eb96856555)] - **src**: include missing header in node\_sea.h (Joyee Cheung) [#48152](https://github.com/nodejs/node/pull/48152)
* \[[`2a35462045`](https://github.com/nodejs/node/commit/2a35462045)] - **src**: deduplicate X509Certificate::Fingerprint\* (Tobias Nießen) [#47978](https://github.com/nodejs/node/pull/47978)
* \[[`4c556816bd`](https://github.com/nodejs/node/commit/4c556816bd)] - **src**: move BlobSerializerDeserializer to a separate header file (Darshan Sen) [#47933](https://github.com/nodejs/node/pull/47933)
* \[[`4bad757012`](https://github.com/nodejs/node/commit/4bad757012)] - **src**: rename SKIP\_CHECK\_SIZE to SKIP\_CHECK\_STRLEN (Tobias Nießen) [#47845](https://github.com/nodejs/node/pull/47845)
* \[[`33daa89dac`](https://github.com/nodejs/node/commit/33daa89dac)] - **src**: register ext reference for Fingerprint512 (Tobias Nießen) [#47892](https://github.com/nodejs/node/pull/47892)
* \[[`30b7133008`](https://github.com/nodejs/node/commit/30b7133008)] - **src**: clarify the parameter name in `Permission::Apply` (Daeyeon Jeong) [#47874](https://github.com/nodejs/node/pull/47874)
* \[[`274c0f2e0a`](https://github.com/nodejs/node/commit/274c0f2e0a)] - **src**: avoid strcmp() with Utf8Value (Tobias Nießen) [#47827](https://github.com/nodejs/node/pull/47827)
* \[[`559c98f468`](https://github.com/nodejs/node/commit/559c98f468)] - **src**: prefer data accessor of string and vector (Mohammed Keyvanzadeh) [#47750](https://github.com/nodejs/node/pull/47750)
* \[[`933673de61`](https://github.com/nodejs/node/commit/933673de61)] - **src**: avoid copying string in fs\_permission (Yagiz Nizipli) [#47746](https://github.com/nodejs/node/pull/47746)
* \[[`77f2b97197`](https://github.com/nodejs/node/commit/77f2b97197)] - **src**: fix typo in comment in quic/sessionticket.cc (Tobias Nießen) [#47754](https://github.com/nodejs/node/pull/47754)
* \[[`8e6af9fcf4`](https://github.com/nodejs/node/commit/8e6af9fcf4)] - **src**: mark fatal error functions as noreturn (Chengzhong Wu) [#47695](https://github.com/nodejs/node/pull/47695)
* \[[`d0ad873b0e`](https://github.com/nodejs/node/commit/d0ad873b0e)] - **src**: prevent changing FunctionTemplateInfo after publish (Shelley Vohr) [#46979](https://github.com/nodejs/node/pull/46979)
* \[[`71fb476781`](https://github.com/nodejs/node/commit/71fb476781)] - **src**: use v8::Boolean(b) over b ? True() : False() (Tobias Nießen) [#47554](https://github.com/nodejs/node/pull/47554)
* \[[`175b78bc02`](https://github.com/nodejs/node/commit/175b78bc02)] - **src**: fix typo in process.env accessor error message (Moritz Raho) [#47014](https://github.com/nodejs/node/pull/47014)
* \[[`2c2b6d1661`](https://github.com/nodejs/node/commit/2c2b6d1661)] - **src**: replace static const string\_view by static constexpr (Daniel Lemire) [#47524](https://github.com/nodejs/node/pull/47524)
* \[[`3840bb586e`](https://github.com/nodejs/node/commit/3840bb586e)] - **src**: fix CSPRNG when length exceeds INT\_MAX (Tobias Nießen) [#47515](https://github.com/nodejs/node/pull/47515)
* \[[`f6aa38dc5f`](https://github.com/nodejs/node/commit/f6aa38dc5f)] - **src**: use correct variable in node\_builtins.cc (Michaël Zasso) [#47343](https://github.com/nodejs/node/pull/47343)
* \[[`e88e249838`](https://github.com/nodejs/node/commit/e88e249838)] - **src**: slim down stream\_base-inl.h (lilsweetcaligula) [#46972](https://github.com/nodejs/node/pull/46972)
* \[[`b34de64442`](https://github.com/nodejs/node/commit/b34de64442)] - **src**: allow simdutf::convert\_\* functions to return zero (Daniel Lemire) [#47471](https://github.com/nodejs/node/pull/47471)
* \[[`ded4a5eb8f`](https://github.com/nodejs/node/commit/ded4a5eb8f)] - **src**: remove usage of `std::shared_ptr<T>::unique()` (Darshan Sen) [#47315](https://github.com/nodejs/node/pull/47315)
* \[[`0fbfb28cf5`](https://github.com/nodejs/node/commit/0fbfb28cf5)] - **src**: use stricter compile-time guidance (Tobias Nießen) [#46509](https://github.com/nodejs/node/pull/46509)
* \[[`a8430ad211`](https://github.com/nodejs/node/commit/a8430ad211)] - **src**: remove unused variable in crypto\_x509.cc (Michaël Zasso) [#47344](https://github.com/nodejs/node/pull/47344)
* \[[`24dabf8965`](https://github.com/nodejs/node/commit/24dabf8965)] - **src**: don't reset embeder signal handlers (Dmitry Vyukov) [#47188](https://github.com/nodejs/node/pull/47188)
* \[[`4add36872c`](https://github.com/nodejs/node/commit/4add36872c)] - **src**: replace impossible THROW with CHECK (Tobias Nießen) [#47168](https://github.com/nodejs/node/pull/47168)
* \[[`e1007ff6a8`](https://github.com/nodejs/node/commit/e1007ff6a8)] - **src**: remove dead comments about return\_code\_cache (Keyhan Vakil) [#47083](https://github.com/nodejs/node/pull/47083)
* \[[`5501d12713`](https://github.com/nodejs/node/commit/5501d12713)] - **src**: remove SSL\_CTX\_get\_tlsext\_ticket\_keys guards (Tobias Nießen) [#47068](https://github.com/nodejs/node/pull/47068)
* \[[`716d289874`](https://github.com/nodejs/node/commit/716d289874)] - **src**: fix clang 14 linker error (Keyhan Vakil) [#47057](https://github.com/nodejs/node/pull/47057)
* \[[`8809adfdb6`](https://github.com/nodejs/node/commit/8809adfdb6)] - **src**: clarify OptionEnvvarSettings member names (Chengzhong Wu) [#45057](https://github.com/nodejs/node/pull/45057)
* \[[`05f5c79574`](https://github.com/nodejs/node/commit/05f5c79574)] - **src**: per-realm binding data (Chengzhong Wu) [#46556](https://github.com/nodejs/node/pull/46556)
* \[[`a7620d19c8`](https://github.com/nodejs/node/commit/a7620d19c8)] - **src,http2**: ensure cleanup if a frame is not sent (ywave620) [#47244](https://github.com/nodejs/node/pull/47244)
* \[[`585d62848e`](https://github.com/nodejs/node/commit/585d62848e)] - **stream**: deprecate asIndexedPairs (Chemi Atlow) [#48102](https://github.com/nodejs/node/pull/48102)
* \[[`d3449ca010`](https://github.com/nodejs/node/commit/d3449ca010)] - **stream**: prevent pipeline hang with generator functions (Debadree Chatterjee) [#47712](https://github.com/nodejs/node/pull/47712)
* \[[`5e4b2434a6`](https://github.com/nodejs/node/commit/5e4b2434a6)] - **(SEMVER-MINOR)** **stream**: preserve object mode in compose (Raz Luvaton) [#47413](https://github.com/nodejs/node/pull/47413)
* \[[`912eb308ab`](https://github.com/nodejs/node/commit/912eb308ab)] - **(SEMVER-MINOR)** **stream**: add setter & getter for default highWaterMark (#46929) (Robert Nagy) [#46929](https://github.com/nodejs/node/pull/46929)
* \[[`c59887744a`](https://github.com/nodejs/node/commit/c59887744a)] - **stream**: expose stream symbols (Robert Nagy) [#45671](https://github.com/nodejs/node/pull/45671)
* \[[`4edc1abf0b`](https://github.com/nodejs/node/commit/4edc1abf0b)] - **stream**: dont wait for next item in take when finished (Raz Luvaton) [#47132](https://github.com/nodejs/node/pull/47132)
* \[[`cfb18d816d`](https://github.com/nodejs/node/commit/cfb18d816d)] - **stream**: remove brandchecks from stream duplexify (Debadree Chatterjee) [#46315](https://github.com/nodejs/node/pull/46315)
* \[[`9d4025c411`](https://github.com/nodejs/node/commit/9d4025c411)] - **test**: mark test-child-process-pipe-dataflow as flaky (Moshe Atlow) [#48334](https://github.com/nodejs/node/pull/48334)
* \[[`c29b6874d4`](https://github.com/nodejs/node/commit/c29b6874d4)] - **test**: unflake test-vm-timeout-escape-nexttick (Santiago Gimeno) [#48078](https://github.com/nodejs/node/pull/48078)
* \[[`0e3312b01b`](https://github.com/nodejs/node/commit/0e3312b01b)] - **test**: fix zlib version regex (Luigi Pinca) [#48227](https://github.com/nodejs/node/pull/48227)
* \[[`a9d0b8d005`](https://github.com/nodejs/node/commit/a9d0b8d005)] - **test**: use lower security level in s\_client (Luigi Pinca) [#48192](https://github.com/nodejs/node/pull/48192)
* \[[`7250d8c2f1`](https://github.com/nodejs/node/commit/7250d8c2f1)] - _**Revert**_ "**test**: unskip negative-settimeout.any.js WPT" (Filip Skokan) [#48182](https://github.com/nodejs/node/pull/48182)
* \[[`18476697bd`](https://github.com/nodejs/node/commit/18476697bd)] - **test**: mark test\_cannot\_run\_js as flaky (Keyhan Vakil) [#48181](https://github.com/nodejs/node/pull/48181)
* \[[`446f6118ff`](https://github.com/nodejs/node/commit/446f6118ff)] - **test**: fix flaky test-runner-watch-mode (Moshe Atlow) [#48144](https://github.com/nodejs/node/pull/48144)
* \[[`7fa144e3fe`](https://github.com/nodejs/node/commit/7fa144e3fe)] - **test**: skip test-http-pipeline-flood on IBM i (Abdirahim Musse) [#48048](https://github.com/nodejs/node/pull/48048)
* \[[`5c5b1d2867`](https://github.com/nodejs/node/commit/5c5b1d2867)] - **test**: ignore helper files in WPTs (Filip Skokan) [#48079](https://github.com/nodejs/node/pull/48079)
* \[[`3b2cee071b`](https://github.com/nodejs/node/commit/3b2cee071b)] - **test**: move `test-cluster-primary-error` flaky test (Yagiz Nizipli) [#48039](https://github.com/nodejs/node/pull/48039)
* \[[`7b816b4922`](https://github.com/nodejs/node/commit/7b816b4922)] - **test**: fix suite signal (Benjamin Gruenbaum) [#47800](https://github.com/nodejs/node/pull/47800)
* \[[`ca4a0e3717`](https://github.com/nodejs/node/commit/ca4a0e3717)] - **test**: fix parsing test flags (Daeyeon Jeong) [#48012](https://github.com/nodejs/node/pull/48012)
* \[[`a3f0504556`](https://github.com/nodejs/node/commit/a3f0504556)] - **test**: mark test-esm-loader-http-imports as flaky (Tobias Nießen) [#47987](https://github.com/nodejs/node/pull/47987)
* \[[`ab36a30143`](https://github.com/nodejs/node/commit/ab36a30143)] - **test**: unskip negative-settimeout.any.js WPT (Filip Skokan) [#47946](https://github.com/nodejs/node/pull/47946)
* \[[`7c80439b21`](https://github.com/nodejs/node/commit/7c80439b21)] - **test**: use appropriate usages for a negative import test (Filip Skokan) [#47878](https://github.com/nodejs/node/pull/47878)
* \[[`81d8d95ba0`](https://github.com/nodejs/node/commit/81d8d95ba0)] - **test**: fix webcrypto wrap unwrap tests (Filip Skokan) [#47876](https://github.com/nodejs/node/pull/47876)
* \[[`1b84e85576`](https://github.com/nodejs/node/commit/1b84e85576)] - **test**: fix output tests when path includes node version (Moshe Atlow) [#47843](https://github.com/nodejs/node/pull/47843)
* \[[`95972aac8d`](https://github.com/nodejs/node/commit/95972aac8d)] - **test**: migrate a pseudo\_tty test to use assertSnapshot (Moshe Atlow) [#47803](https://github.com/nodejs/node/pull/47803)
* \[[`f1e131283d`](https://github.com/nodejs/node/commit/f1e131283d)] - **test**: fix WPT state when process exits but workers are still running (Filip Skokan) [#47826](https://github.com/nodejs/node/pull/47826)
* \[[`03dcf7bc94`](https://github.com/nodejs/node/commit/03dcf7bc94)] - **test**: migrate message tests to use assertSnapshot (Moshe Atlow) [#47498](https://github.com/nodejs/node/pull/47498)
* \[[`dedbeee336`](https://github.com/nodejs/node/commit/dedbeee336)] - **test**: refactor to use `getEventListeners` in timers (Deokjin Kim) [#47759](https://github.com/nodejs/node/pull/47759)
* \[[`11a2d1c4e4`](https://github.com/nodejs/node/commit/11a2d1c4e4)] - **test**: add and use tmpdir.hasEnoughSpace() (Tobias Nießen) [#47767](https://github.com/nodejs/node/pull/47767)
* \[[`d669714e57`](https://github.com/nodejs/node/commit/d669714e57)] - **test**: remove spaces from test runner test names (Tobias Nießen) [#47733](https://github.com/nodejs/node/pull/47733)
* \[[`3a9c43a6d7`](https://github.com/nodejs/node/commit/3a9c43a6d7)] - **test**: mark test-cluster-primary-error flaky on asan (Yagiz Nizipli) [#47422](https://github.com/nodejs/node/pull/47422)
* \[[`bd1eb14cb0`](https://github.com/nodejs/node/commit/bd1eb14cb0)] - **test**: remove unnecessary status check on test-release-npm (RafaelGSS) [#47516](https://github.com/nodejs/node/pull/47516)
* \[[`914f68d953`](https://github.com/nodejs/node/commit/914f68d953)] - **test**: mark test/parallel/test-file-write-stream4 as flaky (Yagiz Nizipli) [#47423](https://github.com/nodejs/node/pull/47423)
* \[[`7c4178cb11`](https://github.com/nodejs/node/commit/7c4178cb11)] - **test**: remove unused callback variables (angellovc) [#47167](https://github.com/nodejs/node/pull/47167)
* \[[`d0bda902dc`](https://github.com/nodejs/node/commit/d0bda902dc)] - **test**: migrate test runner message tests to snapshot (Moshe Atlow) [#47392](https://github.com/nodejs/node/pull/47392)
* \[[`095ca5ccf2`](https://github.com/nodejs/node/commit/095ca5ccf2)] - **test**: remove stale entry from known\_issues.status (Richard Lau) [#47454](https://github.com/nodejs/node/pull/47454)
* \[[`8820d5415b`](https://github.com/nodejs/node/commit/8820d5415b)] - **test**: move more inspector sequential tests to parallel (Joyee Cheung) [#47412](https://github.com/nodejs/node/pull/47412)
* \[[`f6ef5c4ad3`](https://github.com/nodejs/node/commit/f6ef5c4ad3)] - **test**: use random port in test-inspector-enabled (Joyee Cheung) [#47412](https://github.com/nodejs/node/pull/47412)
* \[[`e97ceeca69`](https://github.com/nodejs/node/commit/e97ceeca69)] - **test**: use random port in test-inspector-debug-brk-flag (Joyee Cheung) [#47412](https://github.com/nodejs/node/pull/47412)
* \[[`a2e4643981`](https://github.com/nodejs/node/commit/a2e4643981)] - **test**: use random port in NodeInstance.startViaSignal() (Joyee Cheung) [#47412](https://github.com/nodejs/node/pull/47412)
* \[[`a779261732`](https://github.com/nodejs/node/commit/a779261732)] - **test**: fix flaky test-watch-mode-inspect (Moshe Atlow) [#47403](https://github.com/nodejs/node/pull/47403)
* \[[`116df2ad3e`](https://github.com/nodejs/node/commit/116df2ad3e)] - **test**: move debugger tests with --port=0 to parallel (Joyee Cheung) [#47274](https://github.com/nodejs/node/pull/47274)
* \[[`016b8bd27d`](https://github.com/nodejs/node/commit/016b8bd27d)] - **test**: use --port=0 in debugger tests that do not have to work on 9229 (Joyee Cheung) [#47274](https://github.com/nodejs/node/pull/47274)
* \[[`3c157cb7a3`](https://github.com/nodejs/node/commit/3c157cb7a3)] - **test**: run doctool tests in parallel (Joyee Cheung) [#47273](https://github.com/nodejs/node/pull/47273)
* \[[`44f08ed941`](https://github.com/nodejs/node/commit/44f08ed941)] - **test**: move test-shadow-realm-gc.js to known\_issues (Joyee Cheung) [#47355](https://github.com/nodejs/node/pull/47355)
* \[[`e2b5d968c3`](https://github.com/nodejs/node/commit/e2b5d968c3)] - **test**: update wasm/jsapi WPT (Michaël Zasso) [#47210](https://github.com/nodejs/node/pull/47210)
* \[[`53d9eb5950`](https://github.com/nodejs/node/commit/53d9eb5950)] - **test**: skip test-wasm-web-api on ARM (Michaël Zasso) [#47299](https://github.com/nodejs/node/pull/47299)
* \[[`b620f5fcd6`](https://github.com/nodejs/node/commit/b620f5fcd6)] - **test**: skip instantiateStreaming-bad-imports WPT (Michaël Zasso) [#47292](https://github.com/nodejs/node/pull/47292)
* \[[`1e6fe56333`](https://github.com/nodejs/node/commit/1e6fe56333)] - **test**: fix test-child-process-exec-cwd (Stefan Stojanovic) [#47235](https://github.com/nodejs/node/pull/47235)
* \[[`0dc4971aab`](https://github.com/nodejs/node/commit/0dc4971aab)] - **test**: skip broken tests win arm64 (Stefan Stojanovic) [#47020](https://github.com/nodejs/node/pull/47020)
* \[[`6fee6050e3`](https://github.com/nodejs/node/commit/6fee6050e3)] - **test**: fix 'checks' validation test for checkPrime (Tobias Nießen) [#47139](https://github.com/nodejs/node/pull/47139)
* \[[`6b85472f01`](https://github.com/nodejs/node/commit/6b85472f01)] - **test**: update URL web-platform-tests (Yagiz Nizipli) [#47135](https://github.com/nodejs/node/pull/47135)
* \[[`732a98e1ba`](https://github.com/nodejs/node/commit/732a98e1ba)] - **test**: reduce flakiness of test-http-remove-header-stays-removed.js (Debadree Chatterjee) [#46855](https://github.com/nodejs/node/pull/46855)
* \[[`713b412ee9`](https://github.com/nodejs/node/commit/713b412ee9)] - **test**: mark test-http-max-sockets as flaky on win32 (Tobias Nießen) [#47134](https://github.com/nodejs/node/pull/47134)
* \[[`b3b7dbc395`](https://github.com/nodejs/node/commit/b3b7dbc395)] - **test**: update web-platform tests for url (Xuguang Mei) [#46860](https://github.com/nodejs/node/pull/46860)
* \[[`2edd0fdb5b`](https://github.com/nodejs/node/commit/2edd0fdb5b)] - **test**: mark test-child-process-stdio-reuse-readable-stdio flaky (Luigi Pinca) [#48537](https://github.com/nodejs/node/pull/48537)
* \[[`fae240e492`](https://github.com/nodejs/node/commit/fae240e492)] - **test**: mark test-child-process-pipe-dataflow as flaky (Moshe Atlow) [#48334](https://github.com/nodejs/node/pull/48334)
* \[[`9ca3cc0f2a`](https://github.com/nodejs/node/commit/9ca3cc0f2a)] - **test**: remove useless require('../common') from WPTs (Filip Skokan) [#46796](https://github.com/nodejs/node/pull/46796)
* \[[`4bece055a5`](https://github.com/nodejs/node/commit/4bece055a5)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#47921](https://github.com/nodejs/node/pull/47921)
* \[[`7ef169b706`](https://github.com/nodejs/node/commit/7ef169b706)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#47222](https://github.com/nodejs/node/pull/47222)
* \[[`873606b355`](https://github.com/nodejs/node/commit/873606b355)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#47131](https://github.com/nodejs/node/pull/47131)
* \[[`8f1fc0bc99`](https://github.com/nodejs/node/commit/8f1fc0bc99)] - **test,doc,sea**: run SEA tests on ppc64 (Darshan Sen) [#48111](https://github.com/nodejs/node/pull/48111)
* \[[`5d910ca389`](https://github.com/nodejs/node/commit/5d910ca389)] - **test\_runner**: add enqueue and dequeue events (Moshe Atlow) [#48428](https://github.com/nodejs/node/pull/48428)
* \[[`d795c0ab0c`](https://github.com/nodejs/node/commit/d795c0ab0c)] - **test\_runner**: dont split lines on `test:stdout` (Moshe Atlow) [#48057](https://github.com/nodejs/node/pull/48057)
* \[[`04eb1f85da`](https://github.com/nodejs/node/commit/04eb1f85da)] - **test\_runner**: pass FORCE\_COLOR to child process (Moshe Atlow) [#48057](https://github.com/nodejs/node/pull/48057)
* \[[`2262653148`](https://github.com/nodejs/node/commit/2262653148)] - **test\_runner**: apply `runOnly` on suites (Moshe Atlow) [#48279](https://github.com/nodejs/node/pull/48279)
* \[[`033d0bb3e1`](https://github.com/nodejs/node/commit/033d0bb3e1)] - **test\_runner**: emit `test:watch:drained` event (Moshe Atlow) [#48259](https://github.com/nodejs/node/pull/48259)
* \[[`618a9e1c09`](https://github.com/nodejs/node/commit/618a9e1c09)] - **test\_runner**: stop watch mode when abortSignal aborted (Moshe Atlow) [#48259](https://github.com/nodejs/node/pull/48259)
* \[[`6a82fbd006`](https://github.com/nodejs/node/commit/6a82fbd006)] - **test\_runner**: fix global after hook (Moshe Atlow) [#48231](https://github.com/nodejs/node/pull/48231)
* \[[`d1295d7b10`](https://github.com/nodejs/node/commit/d1295d7b10)] - **test\_runner**: remove redundant check from coverage (Colin Ihrig) [#48070](https://github.com/nodejs/node/pull/48070)
* \[[`47602fe73b`](https://github.com/nodejs/node/commit/47602fe73b)] - **test\_runner**: fix test deserialize edge cases (Moshe Atlow) [#48106](https://github.com/nodejs/node/pull/48106)
* \[[`b18a78cd0b`](https://github.com/nodejs/node/commit/b18a78cd0b)] - **test\_runner**: delegate stderr and stdout formatting to reporter (Shiba) [#48045](https://github.com/nodejs/node/pull/48045)
* \[[`e0d0b19c30`](https://github.com/nodejs/node/commit/e0d0b19c30)] - **test\_runner**: display dot report as wide as the terminal width (Raz Luvaton) [#48038](https://github.com/nodejs/node/pull/48038)
* \[[`bdca468a79`](https://github.com/nodejs/node/commit/bdca468a79)] - **test\_runner**: use v8.serialize instead of TAP (Moshe Atlow) [#47867](https://github.com/nodejs/node/pull/47867)
* \[[`866ed6a887`](https://github.com/nodejs/node/commit/866ed6a887)] - **(SEMVER-MINOR)** **test\_runner**: add shorthands to `test` (Chemi Atlow) [#47909](https://github.com/nodejs/node/pull/47909)
* \[[`4737314865`](https://github.com/nodejs/node/commit/4737314865)] - **test\_runner**: fix ordering of test hooks (Phil Nash) [#47931](https://github.com/nodejs/node/pull/47931)
* \[[`ea543d95f6`](https://github.com/nodejs/node/commit/ea543d95f6)] - **test\_runner**: omit inaccessible files from coverage (Colin Ihrig) [#47850](https://github.com/nodejs/node/pull/47850)
* \[[`8398bca842`](https://github.com/nodejs/node/commit/8398bca842)] - **test\_runner**: fix --require with --experimental-loader (Moshe Atlow) [#47751](https://github.com/nodejs/node/pull/47751)
* \[[`4c0036ba1b`](https://github.com/nodejs/node/commit/4c0036ba1b)] - **(SEMVER-MINOR)** **test\_runner**: support combining coverage reports (Colin Ihrig) [#47686](https://github.com/nodejs/node/pull/47686)
* \[[`cb3abda9aa`](https://github.com/nodejs/node/commit/cb3abda9aa)] - **test\_runner**: remove no-op validation (Colin Ihrig) [#47687](https://github.com/nodejs/node/pull/47687)
* \[[`323881f60b`](https://github.com/nodejs/node/commit/323881f60b)] - **test\_runner**: fix test runner concurrency (Moshe Atlow) [#47675](https://github.com/nodejs/node/pull/47675)
* \[[`3bbb1fc990`](https://github.com/nodejs/node/commit/3bbb1fc990)] - **test\_runner**: fix test counting (Moshe Atlow) [#47675](https://github.com/nodejs/node/pull/47675)
* \[[`0ea63717ba`](https://github.com/nodejs/node/commit/0ea63717ba)] - **test\_runner**: fix nested hooks (Moshe Atlow) [#47648](https://github.com/nodejs/node/pull/47648)
* \[[`fa18b17d88`](https://github.com/nodejs/node/commit/fa18b17d88)] - **test\_runner**: add testNamePatterns to run api (atlowChemi) [#47648](https://github.com/nodejs/node/pull/47648)
* \[[`2033691bfc`](https://github.com/nodejs/node/commit/2033691bfc)] - **test\_runner**: support coverage of unnamed functions (Colin Ihrig) [#47652](https://github.com/nodejs/node/pull/47652)
* \[[`882c6127ae`](https://github.com/nodejs/node/commit/882c6127ae)] - **test\_runner**: move coverage collection to root.postRun() (Colin Ihrig) [#47651](https://github.com/nodejs/node/pull/47651)
* \[[`e97eefa538`](https://github.com/nodejs/node/commit/e97eefa538)] - **(SEMVER-MINOR)** **test\_runner**: execute before hook on test (Chemi Atlow) [#47586](https://github.com/nodejs/node/pull/47586)
* \[[`4bce39108c`](https://github.com/nodejs/node/commit/4bce39108c)] - **test\_runner**: avoid reporting parents of failing tests in summary (Moshe Atlow) [#47579](https://github.com/nodejs/node/pull/47579)
* \[[`688078b93a`](https://github.com/nodejs/node/commit/688078b93a)] - **test\_runner**: fix spec skip detection (Moshe Atlow) [#47537](https://github.com/nodejs/node/pull/47537)
* \[[`0b32a8c8a3`](https://github.com/nodejs/node/commit/0b32a8c8a3)] - **test\_runner**: color errors only when colors are available (Moshe Atlow) [#47394](https://github.com/nodejs/node/pull/47394)
* \[[`d5fc8236bf`](https://github.com/nodejs/node/commit/d5fc8236bf)] - **test\_runner**: hide failing tests title when all tests pass (Moshe Atlow) [#47370](https://github.com/nodejs/node/pull/47370)
* \[[`1d453e4d31`](https://github.com/nodejs/node/commit/1d453e4d31)] - **test\_runner**: stringify AssertError expected and actual (Moshe Atlow) [#47088](https://github.com/nodejs/node/pull/47088)
* \[[`99312a55f2`](https://github.com/nodejs/node/commit/99312a55f2)] - **test\_runner**: add code coverage support to spec reporter (Pulkit Gupta) [#46674](https://github.com/nodejs/node/pull/46674)
* \[[`2091b4718f`](https://github.com/nodejs/node/commit/2091b4718f)] - **(SEMVER-MINOR)** **test\_runner**: expose reporter for use in run api (Chemi Atlow) [#47238](https://github.com/nodejs/node/pull/47238)
* \[[`9cbf89717e`](https://github.com/nodejs/node/commit/9cbf89717e)] - **test\_runner**: report failing tests after summary (HinataKah0) [#47164](https://github.com/nodejs/node/pull/47164)
* \[[`460bcc042e`](https://github.com/nodejs/node/commit/460bcc042e)] - **test\_runner**: count nested tests (Moshe Atlow) [#47094](https://github.com/nodejs/node/pull/47094)
* \[[`c62e6b2e54`](https://github.com/nodejs/node/commit/c62e6b2e54)] - **test\_runner**: accept \x1b as a escape symbol (Debadree Chatterjee) [#47050](https://github.com/nodejs/node/pull/47050)
* \[[`ddf819f810`](https://github.com/nodejs/node/commit/ddf819f810)] - **test\_runner**: support defining test reporter in NODE\_OPTIONS (Steve Herzog) [#46688](https://github.com/nodejs/node/pull/46688)
* \[[`e049ce296a`](https://github.com/nodejs/node/commit/e049ce296a)] - **tls**: reapply servername on happy eyeballs connect (Fedor Indutny) [#48255](https://github.com/nodejs/node/pull/48255)
* \[[`19b0f244b2`](https://github.com/nodejs/node/commit/19b0f244b2)] - **tls**: accept SecureContext object in server.addContext() (HinataKah0) [#47570](https://github.com/nodejs/node/pull/47570)
* \[[`7786d7cece`](https://github.com/nodejs/node/commit/7786d7cece)] - **tools**: pin ruff version number (Rich Trott) [#48505](https://github.com/nodejs/node/pull/48505)
* \[[`a2bfe02289`](https://github.com/nodejs/node/commit/a2bfe02289)] - **tools**: remove non-existing file from CODEOWNERS file (Juan José Arboleda) [#48697](https://github.com/nodejs/node/pull/48697)
* \[[`87562c9af0`](https://github.com/nodejs/node/commit/87562c9af0)] - **tools**: update rollup lint-md-dependencies (Node.js GitHub Bot) [#48329](https://github.com/nodejs/node/pull/48329)
* \[[`1cde4a4299`](https://github.com/nodejs/node/commit/1cde4a4299)] - _**Revert**_ "**tools**: open issue when update workflow fails" (Marco Ippolito) [#48312](https://github.com/nodejs/node/pull/48312)
* \[[`361cf8cffc`](https://github.com/nodejs/node/commit/361cf8cffc)] - **tools**: don't gitignore base64 config.h (Ben Noordhuis) [#48174](https://github.com/nodejs/node/pull/48174)
* \[[`0cfdb3affa`](https://github.com/nodejs/node/commit/0cfdb3affa)] - **tools**: update LICENSE and license-builder.sh (Santiago Gimeno) [#48078](https://github.com/nodejs/node/pull/48078)
* \[[`715cf81b4a`](https://github.com/nodejs/node/commit/715cf81b4a)] - **tools**: automate histogram update (Marco Ippolito) [#48171](https://github.com/nodejs/node/pull/48171)
* \[[`d9afffab03`](https://github.com/nodejs/node/commit/d9afffab03)] - **tools**: use shasum instead of sha256sum (Luigi Pinca) [#48229](https://github.com/nodejs/node/pull/48229)
* \[[`1a5cddfc1f`](https://github.com/nodejs/node/commit/1a5cddfc1f)] - **tools**: harmonize `dep_updaters` scripts (Antoine du Hamel) [#48201](https://github.com/nodejs/node/pull/48201)
* \[[`24abe07dda`](https://github.com/nodejs/node/commit/24abe07dda)] - **tools**: log and verify sha256sum (Andrea Fassina) [#48088](https://github.com/nodejs/node/pull/48088)
* \[[`9aed8683aa`](https://github.com/nodejs/node/commit/9aed8683aa)] - **tools**: open issue when update workflow fails (Marco Ippolito) [#48018](https://github.com/nodejs/node/pull/48018)
* \[[`4c5c63fd82`](https://github.com/nodejs/node/commit/4c5c63fd82)] - **tools**: alphabetize CODEOWNERS (Rich Trott) [#48124](https://github.com/nodejs/node/pull/48124)
* \[[`9b849a7270`](https://github.com/nodejs/node/commit/9b849a7270)] - **tools**: use latest upstream commit for zlib updates (Andrea Fassina) [#48054](https://github.com/nodejs/node/pull/48054)
* \[[`e18c1258ae`](https://github.com/nodejs/node/commit/e18c1258ae)] - **tools**: add security-wg as dep updaters owner (Marco Ippolito) [#48113](https://github.com/nodejs/node/pull/48113)
* \[[`999a289dd9`](https://github.com/nodejs/node/commit/999a289dd9)] - **tools**: fix race condition when npm installing (Tobias Nießen) [#48101](https://github.com/nodejs/node/pull/48101)
* \[[`25b0033b86`](https://github.com/nodejs/node/commit/25b0033b86)] - **tools**: refloat 7 Node.js patches to cpplint.py (Rich Trott) [#48098](https://github.com/nodejs/node/pull/48098)
* \[[`87b36b3a8a`](https://github.com/nodejs/node/commit/87b36b3a8a)] - **tools**: update cpplint to 1.6.1 (Yagiz Nizipli) [#48098](https://github.com/nodejs/node/pull/48098)
* \[[`64ff6fe443`](https://github.com/nodejs/node/commit/64ff6fe443)] - **tools**: update eslint to 8.41.0 (Node.js GitHub Bot) [#48097](https://github.com/nodejs/node/pull/48097)
* \[[`739c314851`](https://github.com/nodejs/node/commit/739c314851)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#48096](https://github.com/nodejs/node/pull/48096)
* \[[`6674ed1901`](https://github.com/nodejs/node/commit/6674ed1901)] - **tools**: update doc to remark-parse\@10.0.2 (Node.js GitHub Bot) [#48095](https://github.com/nodejs/node/pull/48095)
* \[[`0b0818caab`](https://github.com/nodejs/node/commit/0b0818caab)] - **tools**: add debug logs (Marco Ippolito) [#48060](https://github.com/nodejs/node/pull/48060)
* \[[`168d080f40`](https://github.com/nodejs/node/commit/168d080f40)] - **tools**: fix zconf.h path (Luigi Pinca) [#48089](https://github.com/nodejs/node/pull/48089)
* \[[`ccd2795f42`](https://github.com/nodejs/node/commit/ccd2795f42)] - **tools**: update remark-preset-lint-node to 4.0.0 (Node.js GitHub Bot) [#47995](https://github.com/nodejs/node/pull/47995)
* \[[`89e068a550`](https://github.com/nodejs/node/commit/89e068a550)] - **tools**: debug log for nghttp3 (Marco Ippolito) [#47992](https://github.com/nodejs/node/pull/47992)
* \[[`3bf2bd4ec6`](https://github.com/nodejs/node/commit/3bf2bd4ec6)] - **tools**: automate icu-small update (Marco Ippolito) [#47727](https://github.com/nodejs/node/pull/47727)
* \[[`fdf9681dc0`](https://github.com/nodejs/node/commit/fdf9681dc0)] - **tools**: update lint-md-dependencies to rollup\@3.21.5 (Node.js GitHub Bot) [#47903](https://github.com/nodejs/node/pull/47903)
* \[[`52532c2cb1`](https://github.com/nodejs/node/commit/52532c2cb1)] - **tools**: update eslint to 8.40.0 (Node.js GitHub Bot) [#47906](https://github.com/nodejs/node/pull/47906)
* \[[`951cc7b624`](https://github.com/nodejs/node/commit/951cc7b624)] - **tools**: update eslint to 8.39.0 (Node.js GitHub Bot) [#47789](https://github.com/nodejs/node/pull/47789)
* \[[`706c87db50`](https://github.com/nodejs/node/commit/706c87db50)] - **tools**: fix jsdoc lint (Moshe Atlow) [#47789](https://github.com/nodejs/node/pull/47789)
* \[[`8eef5f5f5f`](https://github.com/nodejs/node/commit/8eef5f5f5f)] - **tools**: update doc to highlight.js\@11.8.0 (Node.js GitHub Bot) [#47786](https://github.com/nodejs/node/pull/47786)
* \[[`899eea6237`](https://github.com/nodejs/node/commit/899eea6237)] - **tools**: update lint-md-dependencies to rollup\@3.21.1 (Node.js GitHub Bot) [#47787](https://github.com/nodejs/node/pull/47787)
* \[[`8ca50b6ac9`](https://github.com/nodejs/node/commit/8ca50b6ac9)] - **tools**: move update-npm to dep updaters (Marco Ippolito) [#47619](https://github.com/nodejs/node/pull/47619)
* \[[`5ee554397d`](https://github.com/nodejs/node/commit/5ee554397d)] - **tools**: fix update-v8-patch cache (Marco Ippolito) [#47725](https://github.com/nodejs/node/pull/47725)
* \[[`4e0d19e1bd`](https://github.com/nodejs/node/commit/4e0d19e1bd)] - **tools**: automate v8 patch update (Marco Ippolito) [#47594](https://github.com/nodejs/node/pull/47594)
* \[[`64c3154bdf`](https://github.com/nodejs/node/commit/64c3154bdf)] - **tools**: fix skip message in update-cjs-module-lexer (Tobias Nießen) [#47701](https://github.com/nodejs/node/pull/47701)
* \[[`6535ab90d9`](https://github.com/nodejs/node/commit/6535ab90d9)] - **tools**: update lint-md-dependencies to @rollup/plugin-commonjs\@24.1.0 (Node.js GitHub Bot) [#47577](https://github.com/nodejs/node/pull/47577)
* \[[`7cfc00d072`](https://github.com/nodejs/node/commit/7cfc00d072)] - **tools**: keep PR titles/description up-to-date (Tobias Nießen) [#47621](https://github.com/nodejs/node/pull/47621)
* \[[`d6b555e2c8`](https://github.com/nodejs/node/commit/d6b555e2c8)] - **tools**: fix updating root certificates (Richard Lau) [#47607](https://github.com/nodejs/node/pull/47607)
* \[[`0d3e3ddea6`](https://github.com/nodejs/node/commit/0d3e3ddea6)] - **tools**: update PR label config (Mohammed Keyvanzadeh) [#47593](https://github.com/nodejs/node/pull/47593)
* \[[`fa52c472b8`](https://github.com/nodejs/node/commit/fa52c472b8)] - **tools**: add execution permission to uvwasi script (Mert Can Altın) [#47600](https://github.com/nodejs/node/pull/47600)
* \[[`f6955a6b0c`](https://github.com/nodejs/node/commit/f6955a6b0c)] - **tools**: add update script for googletest (Tobias Nießen) [#47482](https://github.com/nodejs/node/pull/47482)
* \[[`4e1d87e752`](https://github.com/nodejs/node/commit/4e1d87e752)] - **tools**: add option to run workflow with specific tool id (Michaël Zasso) [#47591](https://github.com/nodejs/node/pull/47591)
* \[[`e605402590`](https://github.com/nodejs/node/commit/e605402590)] - **tools**: automate zlib update (Marco Ippolito) [#47417](https://github.com/nodejs/node/pull/47417)
* \[[`b6ca57e8d0`](https://github.com/nodejs/node/commit/b6ca57e8d0)] - **tools**: add url and whatwg-url labels automatically (Yagiz Nizipli) [#47545](https://github.com/nodejs/node/pull/47545)
* \[[`d5c9bc4f8e`](https://github.com/nodejs/node/commit/d5c9bc4f8e)] - **tools**: add performance label to benchmark changes (Yagiz Nizipli) [#47545](https://github.com/nodejs/node/pull/47545)
* \[[`c5227628a9`](https://github.com/nodejs/node/commit/c5227628a9)] - **tools**: automate uvwasi dependency update (Ranieri Innocenti Spada) [#47509](https://github.com/nodejs/node/pull/47509)
* \[[`5ffdb57302`](https://github.com/nodejs/node/commit/5ffdb57302)] - **tools**: add missing pinned dependencies (Mateo Nunez) [#47346](https://github.com/nodejs/node/pull/47346)
* \[[`c7b898d4e4`](https://github.com/nodejs/node/commit/c7b898d4e4)] - **tools**: automate ngtcp2 and nghttp3 update (Marco Ippolito) [#47402](https://github.com/nodejs/node/pull/47402)
* \[[`e696a48225`](https://github.com/nodejs/node/commit/e696a48225)] - **tools**: move update-undici.sh to dep\_updaters and create maintain md (Marco Ippolito) [#47380](https://github.com/nodejs/node/pull/47380)
* \[[`056067286a`](https://github.com/nodejs/node/commit/056067286a)] - **tools**: make `js2c.py` usable for other build systems (Cheng Zhao) [#46930](https://github.com/nodejs/node/pull/46930)
* \[[`d11c6ba2eb`](https://github.com/nodejs/node/commit/d11c6ba2eb)] - **tools**: move update-acorn.sh to dep\_updaters and create maintaining md (Marco Ippolito) [#47382](https://github.com/nodejs/node/pull/47382)
* \[[`0a65c7c300`](https://github.com/nodejs/node/commit/0a65c7c300)] - **tools**: update eslint to 8.38.0 (Node.js GitHub Bot) [#47475](https://github.com/nodejs/node/pull/47475)
* \[[`60dc249cd5`](https://github.com/nodejs/node/commit/60dc249cd5)] - **tools**: update eslint to 8.38.0 (Node.js GitHub Bot) [#47475](https://github.com/nodejs/node/pull/47475)
* \[[`1271b0eded`](https://github.com/nodejs/node/commit/1271b0eded)] - **tools**: automate cjs-module-lexer dependency update (Marco Ippolito) [#47446](https://github.com/nodejs/node/pull/47446)
* \[[`26905572d4`](https://github.com/nodejs/node/commit/26905572d4)] - **tools**: fix notify-on-push Slack messages (Antoine du Hamel) [#47453](https://github.com/nodejs/node/pull/47453)
* \[[`3c62663797`](https://github.com/nodejs/node/commit/3c62663797)] - **tools**: update lint-md-dependencies to @rollup/plugin-node-resolve\@15.0.2 (Node.js GitHub Bot) [#47431](https://github.com/nodejs/node/pull/47431)
* \[[`c57dabe360`](https://github.com/nodejs/node/commit/c57dabe360)] - **tools**: add root certificate update script (Richard Lau) [#47425](https://github.com/nodejs/node/pull/47425)
* \[[`f27680e37c`](https://github.com/nodejs/node/commit/f27680e37c)] - **tools**: fix update-openssl.yml compare version (Marco Ippolito) [#47384](https://github.com/nodejs/node/pull/47384)
* \[[`35e6cf2944`](https://github.com/nodejs/node/commit/35e6cf2944)] - **tools**: use ref\_name to get branch pushed on (Debadree Chatterjee) [#47358](https://github.com/nodejs/node/pull/47358)
* \[[`28935a86f8`](https://github.com/nodejs/node/commit/28935a86f8)] - **tools**: add a at here tag for slack messages (Debadree Chatterjee) [#47358](https://github.com/nodejs/node/pull/47358)
* \[[`e1846ee4f1`](https://github.com/nodejs/node/commit/e1846ee4f1)] - **tools**: disable Codecov commit statuses (Michaël Zasso) [#47306](https://github.com/nodejs/node/pull/47306)
* \[[`d1c8229da4`](https://github.com/nodejs/node/commit/d1c8229da4)] - **tools**: update eslint to 8.37.0 (Node.js GitHub Bot) [#47333](https://github.com/nodejs/node/pull/47333)
* \[[`6ab22151c4`](https://github.com/nodejs/node/commit/6ab22151c4)] - **tools**: fix duration\_ms to be milliseconds (Moshe Atlow) [#44490](https://github.com/nodejs/node/pull/44490)
* \[[`5d46c594a7`](https://github.com/nodejs/node/commit/5d46c594a7)] - **tools**: automate brotli update (Marco Ippolito) [#47205](https://github.com/nodejs/node/pull/47205)
* \[[`d324c15227`](https://github.com/nodejs/node/commit/d324c15227)] - **tools**: fix typo in nghttp2 path (Marco Ippolito) [#47330](https://github.com/nodejs/node/pull/47330)
* \[[`b1bcdceef5`](https://github.com/nodejs/node/commit/b1bcdceef5)] - **tools**: add scorecard workflow (Mateo Nunez) [#47254](https://github.com/nodejs/node/pull/47254)
* \[[`281362bcc2`](https://github.com/nodejs/node/commit/281362bcc2)] - **tools**: pin actions by hash for auto-start-ci.yml (Gabriela Gutierrez) [#46820](https://github.com/nodejs/node/pull/46820)
* \[[`6cae1bd377`](https://github.com/nodejs/node/commit/6cae1bd377)] - **tools**: standardize base64 update (Marco Ippolito) [#47201](https://github.com/nodejs/node/pull/47201)
* \[[`a682bd0714`](https://github.com/nodejs/node/commit/a682bd0714)] - **tools**: update codecov branch (Rich Trott) [#47285](https://github.com/nodejs/node/pull/47285)
* \[[`1061e17c66`](https://github.com/nodejs/node/commit/1061e17c66)] - **tools**: standardize update-llhttp.sh (Marco Ippolito) [#47198](https://github.com/nodejs/node/pull/47198)
* \[[`9781185943`](https://github.com/nodejs/node/commit/9781185943)] - **tools**: upgrade Windows digital signature to SHA256 (Tobias Nießen) [#47206](https://github.com/nodejs/node/pull/47206)
* \[[`37638e43c5`](https://github.com/nodejs/node/commit/37638e43c5)] - **tools**: add button to copy code example to clipboard (jakecastelli) [#46928](https://github.com/nodejs/node/pull/46928)
* \[[`05cb503f02`](https://github.com/nodejs/node/commit/05cb503f02)] - **tools**: standardize update-nghttp2.sh (Marco Ippolito) [#47197](https://github.com/nodejs/node/pull/47197)
* \[[`816e215701`](https://github.com/nodejs/node/commit/816e215701)] - **tools**: fix Slack notification action (Antoine du Hamel) [#47237](https://github.com/nodejs/node/pull/47237)
* \[[`9ac01ecc59`](https://github.com/nodejs/node/commit/9ac01ecc59)] - **tools**: notify on Slack when invalid commit lands (Antoine du Hamel) [#47178](https://github.com/nodejs/node/pull/47178)
* \[[`13eb029b4f`](https://github.com/nodejs/node/commit/13eb029b4f)] - **tools**: update daily wpt actions summary (Filip Skokan) [#47138](https://github.com/nodejs/node/pull/47138)
* \[[`e0a00ebfc5`](https://github.com/nodejs/node/commit/e0a00ebfc5)] - **tools**: allow test tap output to include unicode characters (Moshe Atlow) [#47175](https://github.com/nodejs/node/pull/47175)
* \[[`fca3391d0b`](https://github.com/nodejs/node/commit/fca3391d0b)] - **tools**: update lint-md-dependencies to rollup\@3.19.1 (Node.js GitHub Bot) [#47045](https://github.com/nodejs/node/pull/47045)
* \[[`c0fd6a3721`](https://github.com/nodejs/node/commit/c0fd6a3721)] - **tools**: update eslint to 8.36.0 (Node.js GitHub Bot) [#47046](https://github.com/nodejs/node/pull/47046)
* \[[`7d971daf29`](https://github.com/nodejs/node/commit/7d971daf29)] - **tools,meta**: update README and tools to reflect changes in TSC charter (Rich Trott) [#47126](https://github.com/nodejs/node/pull/47126)
* \[[`d078d66bdc`](https://github.com/nodejs/node/commit/d078d66bdc)] - **typings**: fix syntax error in tsconfig (Mohammed Keyvanzadeh) [#47584](https://github.com/nodejs/node/pull/47584)
* \[[`889730512c`](https://github.com/nodejs/node/commit/889730512c)] - **url**: handle URL.canParse without base parameter (Yagiz Nizipli) [#47547](https://github.com/nodejs/node/pull/47547)
* \[[`0dc485eb28`](https://github.com/nodejs/node/commit/0dc485eb28)] - **url**: drop ICU requirement for parsing hostnames (Yagiz Nizipli) [#47339](https://github.com/nodejs/node/pull/47339)
* \[[`b395b16c40`](https://github.com/nodejs/node/commit/b395b16c40)] - **url**: use ada::url\_aggregator for parsing urls (Yagiz Nizipli) [#47339](https://github.com/nodejs/node/pull/47339)
* \[[`11f48e02a8`](https://github.com/nodejs/node/commit/11f48e02a8)] - **(SEMVER-MINOR)** **url**: implement URL.canParse (Matthew Aitken) [#47179](https://github.com/nodejs/node/pull/47179)
* \[[`977a8bad35`](https://github.com/nodejs/node/commit/977a8bad35)] - **url**: fix array overrun in node:url::SetArgs() (Yagiz Nizipli) [#47001](https://github.com/nodejs/node/pull/47001)
* \[[`4784e64850`](https://github.com/nodejs/node/commit/4784e64850)] - **url**: allow extension of user provided URL objects (Antoine du Hamel) [#46989](https://github.com/nodejs/node/pull/46989)
* \[[`f495cb6bf4`](https://github.com/nodejs/node/commit/f495cb6bf4)] - **url**: backport non-major changes from #46904 (Yagiz Nizipli) [#46904](https://github.com/nodejs/node/pull/46904)
* \[[`aa4f485388`](https://github.com/nodejs/node/commit/aa4f485388)] - **url**: set `formatUrl` method as no side effect (Yagiz Nizipli) [#46884](https://github.com/nodejs/node/pull/46884)
* \[[`c79e1b72f2`](https://github.com/nodejs/node/commit/c79e1b72f2)] - **url**: offload `URLSearchParams` initialization (Yagiz Nizipli) [#46867](https://github.com/nodejs/node/pull/46867)
* \[[`3db235b822`](https://github.com/nodejs/node/commit/3db235b822)] - **url**: remove unused `kFormat` from url (Yagiz Nizipli) [#46867](https://github.com/nodejs/node/pull/46867)
* \[[`b9df1a9668`](https://github.com/nodejs/node/commit/b9df1a9668)] - **url**: clean vertical alignment of docs (Robin Ury) [#48037](https://github.com/nodejs/node/pull/48037)
* \[[`9a2354d4a9`](https://github.com/nodejs/node/commit/9a2354d4a9)] - **url**: do not use object as hashmap (Timothy Gu) [#47415](https://github.com/nodejs/node/pull/47415)
* \[[`4850ba4bd4`](https://github.com/nodejs/node/commit/4850ba4bd4)] - **url**: improve URLSearchParams creation performance (Yagiz Nizipli) [#47190](https://github.com/nodejs/node/pull/47190)
* \[[`7fb1980fd9`](https://github.com/nodejs/node/commit/7fb1980fd9)] - **url**: add pending-deprecation to `url.parse()` (Yagiz Nizipli) [#47203](https://github.com/nodejs/node/pull/47203)
* \[[`b04ea5aa9b`](https://github.com/nodejs/node/commit/b04ea5aa9b)] - **url**: allow extension of user provided URL objects (Antoine du Hamel) [#46989](https://github.com/nodejs/node/pull/46989)
* \[[`972c851918`](https://github.com/nodejs/node/commit/972c851918)] - **url**: remove unnecessary call to `FunctionPrototypeBind` (Antoine du Hamel) [#46870](https://github.com/nodejs/node/pull/46870)
* \[[`87ef1b2859`](https://github.com/nodejs/node/commit/87ef1b2859)] - **util**: fix inspecting error with a throwing getter for `cause` (Antoine du Hamel) [#47163](https://github.com/nodejs/node/pull/47163)
* \[[`4729d30c1e`](https://github.com/nodejs/node/commit/4729d30c1e)] - **v8**: fix ERR\_NOT\_BUILDING\_SNAPSHOT is not a constructor (Chengzhong Wu) [#47721](https://github.com/nodejs/node/pull/47721)
* \[[`d9a68b821e`](https://github.com/nodejs/node/commit/d9a68b821e)] - **vm**: properly handle defining symbol props (Nicolas DUBIEN) [#47572](https://github.com/nodejs/node/pull/47572)
* \[[`0d0fad8f0a`](https://github.com/nodejs/node/commit/0d0fad8f0a)] - **vm**: fix crash when setting \_\_proto\_\_ on context's globalThis (Feng Yu) [#47939](https://github.com/nodejs/node/pull/47939)
* \[[`fb90b6b3fb`](https://github.com/nodejs/node/commit/fb90b6b3fb)] - **vm**: properly handle defining props on any value (Nicolas DUBIEN) [#46615](https://github.com/nodejs/node/pull/46615)
* \[[`4b2aa3d27c`](https://github.com/nodejs/node/commit/4b2aa3d27c)] - **vm,lib**: refactor microtaskQueue assignment logic (Khaidi Chu) [#47765](https://github.com/nodejs/node/pull/47765)
* \[[`58afcc27f6`](https://github.com/nodejs/node/commit/58afcc27f6)] - **(SEMVER-MINOR)** **wasi**: no longer require flag to enable wasi (Michael Dawson) [#47286](https://github.com/nodejs/node/pull/47286)
* \[[`407af51cf5`](https://github.com/nodejs/node/commit/407af51cf5)] - **wasi**: add wasi sock\_accept stub (Michael Dawson) [#46434](https://github.com/nodejs/node/pull/46434)
* \[[`d3e0229948`](https://github.com/nodejs/node/commit/d3e0229948)] - **watch**: fix watch path with equals (Moshe Atlow) [#47369](https://github.com/nodejs/node/pull/47369)
* \[[`78972d4696`](https://github.com/nodejs/node/commit/78972d4696)] - **worker**: support more cases when (de)serializing errors (Moshe Atlow) [#47925](https://github.com/nodejs/node/pull/47925)

<a id="18.16.1"></a>

## 2023-06-20, Version 18.16.1 'Hydrogen' (LTS), @RafaelGSS

This is a security release.

### Notable Changes

The following CVEs are fixed in this release:

* [CVE-2023-30581](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-30581): `mainModule.__proto__` Bypass Experimental Policy Mechanism (High)
* [CVE-2023-30585](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-30585): Privilege escalation via Malicious Registry Key manipulation during Node.js installer repair process (Medium)
* [CVE-2023-30588](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-30588): Process interuption due to invalid Public Key information in x509 certificates (Medium)
* [CVE-2023-30589](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-30589): HTTP Request Smuggling via Empty headers separated by CR (Medium)
* [CVE-2023-30590](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-30590): DiffieHellman does not generate keys after setting a private key (Medium)
* OpenSSL Security Releases
  * [OpenSSL security advisory 28th March](https://www.openssl.org/news/secadv/20230328.txt).
  * [OpenSSL security advisory 20th April](https://www.openssl.org/news/secadv/20230420.txt).
  * [OpenSSL security advisory 30th May](https://www.openssl.org/news/secadv/20230530.txt)
* c-ares vulnerabilities:
  * [GHSA-9g78-jv2r-p7vc](https://github.com/c-ares/c-ares/security/advisories/GHSA-9g78-jv2r-p7vc)
  * [GHSA-8r8p-23f3-64c2](https://github.com/c-ares/c-ares/security/advisories/GHSA-8r8p-23f3-64c2)
  * [GHSA-54xr-f67r-4pc4](https://github.com/c-ares/c-ares/security/advisories/GHSA-54xr-f67r-4pc4)
  * [GHSA-x6mf-cxr9-8q6v](https://github.com/c-ares/c-ares/security/advisories/GHSA-x6mf-cxr9-8q6v)

More detailed information on each of the vulnerabilities can be found in [June 2023 Security Releases](https://nodejs.org/en/blog/vulnerability/june-2023-security-releases/) blog post.

### Commits

* \[[`bf3e2c8928`](https://github.com/nodejs/node/commit/bf3e2c8928)] - **crypto**: handle cert with invalid SPKI gracefully (Tobias Nießen) [nodejs-private/node-private#393](https://github.com/nodejs-private/node-private/pull/393)
* \[[`70f9449072`](https://github.com/nodejs/node/commit/70f9449072)] - **deps**: set `CARES_RANDOM_FILE` for c-ares (Richard Lau) [#48156](https://github.com/nodejs/node/pull/48156)
* \[[`35d4efb57b`](https://github.com/nodejs/node/commit/35d4efb57b)] - **deps**: update c-ares to 1.19.1 (RafaelGSS) [#48115](https://github.com/nodejs/node/pull/48115)
* \[[`392dfedc77`](https://github.com/nodejs/node/commit/392dfedc77)] - **deps**: update archs files for openssl-3.0.9-quic1 (Node.js GitHub Bot) [#48402](https://github.com/nodejs/node/pull/48402)
* \[[`46cd5fe38b`](https://github.com/nodejs/node/commit/46cd5fe38b)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.9-quic1 (Node.js GitHub Bot) [#48402](https://github.com/nodejs/node/pull/48402)
* \[[`7e3d2d85c2`](https://github.com/nodejs/node/commit/7e3d2d85c2)] - **doc,test**: clarify behavior of DH generateKeys (Tobias Nießen) [nodejs-private/node-private#426](https://github.com/nodejs-private/node-private/pull/426)
* \[[`4ff6ba050a`](https://github.com/nodejs/node/commit/4ff6ba050a)] - **http**: disable request smuggling via rempty headers (Paolo Insogna) [nodejs-private/node-private#428](https://github.com/nodejs-private/node-private/pull/428)
* \[[`ab269129a6`](https://github.com/nodejs/node/commit/ab269129a6)] - **msi**: do not create AppData\Roaming\npm (Tobias Nießen) [nodejs-private/node-private#408](https://github.com/nodejs-private/node-private/pull/408)
* \[[`925e8f5619`](https://github.com/nodejs/node/commit/925e8f5619)] - **policy**: handle mainModule.\_\_proto\_\_ bypass (RafaelGSS) [nodejs-private/node-private#416](https://github.com/nodejs-private/node-private/pull/416)
* \[[`d6fae8e47e`](https://github.com/nodejs/node/commit/d6fae8e47e)] - **test**: allow SIGBUS in signal-handler abort test (Michaël Zasso) [#47851](https://github.com/nodejs/node/pull/47851)

<a id="18.16.0"></a>

## 2023-04-12, Version 18.16.0 'Hydrogen' (LTS), @danielleadams

### Notable changes

#### Add initial support for single executable applications

Compile a JavaScript file into a single executable application:

```console
$ echo 'console.log(`Hello, ${process.argv[2]}!`);' > hello.js

$ cp $(command -v node) hello

# On systems other than macOS:
$ npx postject hello NODE_JS_CODE hello.js \
    --sentinel-fuse NODE_JS_FUSE_fce680ab2cc467b6e072b8b5df1996b2

# On macOS:
$ npx postject hello NODE_JS_CODE hello.js \
    --sentinel-fuse NODE_JS_FUSE_fce680ab2cc467b6e072b8b5df1996b2 \
    --macho-segment-name NODE_JS

$ ./hello world
Hello, world!
```

Contributed by Darshan Sen in <https://github.com/nodejs/node/pull/45038>

#### Replace url parser with Ada

Node.js gets a new URL parser called Ada that is compliant with the WHATWG URL Specification and provides more than 100% performance improvement to the existing implementation.

Contributed by Yagiz Nizipli in <https://github.com/nodejs/node/pull/46410>

#### Other notable changes

* **buffer**:
  * **(SEMVER-MINOR)** add Buffer.copyBytesFrom(...) (James M Snell) [#46500](https://github.com/nodejs/node/pull/46500)
* **doc**:
  * add marco-ippolito to collaborators (Marco Ippolito) [#46816](https://github.com/nodejs/node/pull/46816)
  * add debadree25 to collaborators (Debadree Chatterjee) [#46716](https://github.com/nodejs/node/pull/46716)
  * add deokjinkim to collaborators (Deokjin Kim) [#46444](https://github.com/nodejs/node/pull/46444)
* **events**:
  * **(SEMVER-MINOR)** add listener argument to listenerCount (Paolo Insogna) [#46523](https://github.com/nodejs/node/pull/46523)
* **lib**:
  * **(SEMVER-MINOR)** add AsyncLocalStorage.bind() and .snapshot() (flakey5) [#46387](https://github.com/nodejs/node/pull/46387)
  * **(SEMVER-MINOR)** add aborted() utility function (Debadree Chatterjee) [#46494](https://github.com/nodejs/node/pull/46494)
* **src**:
  * **(SEMVER-MINOR)** allow optional Isolate termination in node::Stop() (Shelley Vohr) [#46583](https://github.com/nodejs/node/pull/46583)
  * **(SEMVER-MINOR)** allow embedder control of code generation policy (Shelley Vohr) [#46368](https://github.com/nodejs/node/pull/46368)
* **stream**:
  * **(SEMVER-MINOR)** add abort signal for ReadableStream and WritableStream (Debadree Chatterjee) [#46273](https://github.com/nodejs/node/pull/46273)
* **tls**:
  * **(SEMVER-MINOR)** support automatic DHE (Tobias Nießen) [#46978](https://github.com/nodejs/node/pull/46978)
* **url**:
  * **(SEMVER-MINOR)** implement URLSearchParams size getter (James M Snell) [#46308](https://github.com/nodejs/node/pull/46308)
* **worker**:
  * **(SEMVER-MINOR)** add support for worker name in inspector and trace\_events (Debadree Chatterjee) [#46832](https://github.com/nodejs/node/pull/46832)

### Commits

* \[[`c742493b61`](https://github.com/nodejs/node/commit/c742493b61)] - **assert**: fix exception message for assert(0) on try catch block (hidecology) [#46760](https://github.com/nodejs/node/pull/46760)
* \[[`0ddf73ae7c`](https://github.com/nodejs/node/commit/0ddf73ae7c)] - **assert**: remove deprecated getFunction() usage (Ruben Bridgewater) [#46661](https://github.com/nodejs/node/pull/46661)
* \[[`97ad72f19f`](https://github.com/nodejs/node/commit/97ad72f19f)] - **async\_hooks**: add async local storage propagation benchmarks (Chengzhong Wu) [#46414](https://github.com/nodejs/node/pull/46414)
* \[[`b1bde69574`](https://github.com/nodejs/node/commit/b1bde69574)] - **async\_hooks**: remove experimental onPropagate option (James M Snell) [#46386](https://github.com/nodejs/node/pull/46386)
* \[[`b5db3b579a`](https://github.com/nodejs/node/commit/b5db3b579a)] - **benchmark**: add a benchmark for URLSearchParams creation and toString() (Debadree Chatterjee) [#46810](https://github.com/nodejs/node/pull/46810)
* \[[`ff94f9ffbe`](https://github.com/nodejs/node/commit/ff94f9ffbe)] - **benchmark**: replace table in docs with description of file tree structure (Theodor Steiner) [#46991](https://github.com/nodejs/node/pull/46991)
* \[[`d4af671f09`](https://github.com/nodejs/node/commit/d4af671f09)] - **benchmark**: split `Buffer.byteLength` benchmark (Joyee Cheung) [#46616](https://github.com/nodejs/node/pull/46616)
* \[[`5f647fb7b4`](https://github.com/nodejs/node/commit/5f647fb7b4)] - **benchmark**: add benchmark for EventTarget add and remove (Debadree Chatterjee) [#46779](https://github.com/nodejs/node/pull/46779)
* \[[`d7d634bd67`](https://github.com/nodejs/node/commit/d7d634bd67)] - **benchmark**: fix worker startup benchmark (Joyee Cheung) [#46680](https://github.com/nodejs/node/pull/46680)
* \[[`f7c4796c56`](https://github.com/nodejs/node/commit/f7c4796c56)] - **benchmark**: add trailing commas in `benchmark/path` (Antoine du Hamel) [#46628](https://github.com/nodejs/node/pull/46628)
* \[[`9b0d5030a5`](https://github.com/nodejs/node/commit/9b0d5030a5)] - **benchmark**: add trailing commas in `benchmark/http` (Antoine du Hamel) [#46609](https://github.com/nodejs/node/pull/46609)
* \[[`e0f436041e`](https://github.com/nodejs/node/commit/e0f436041e)] - **benchmark**: add trailing commas in `benchmark/crypto` (Antoine du Hamel) [#46553](https://github.com/nodejs/node/pull/46553)
* \[[`a383aee386`](https://github.com/nodejs/node/commit/a383aee386)] - **benchmark**: add trailing commas in `benchmark/url` (Antoine du Hamel) [#46551](https://github.com/nodejs/node/pull/46551)
* \[[`a10c3558c6`](https://github.com/nodejs/node/commit/a10c3558c6)] - **benchmark**: add trailing commas in `benchmark/http2` (Antoine du Hamel) [#46552](https://github.com/nodejs/node/pull/46552)
* \[[`8036583f1f`](https://github.com/nodejs/node/commit/8036583f1f)] - **benchmark**: add trailing commas in `benchmark/process` (Antoine du Hamel) [#46481](https://github.com/nodejs/node/pull/46481)
* \[[`1497244078`](https://github.com/nodejs/node/commit/1497244078)] - **benchmark**: add trailing commas in `benchmark/misc` (Antoine du Hamel) [#46474](https://github.com/nodejs/node/pull/46474)
* \[[`057e3f5309`](https://github.com/nodejs/node/commit/057e3f5309)] - **benchmark**: add trailing commas in `benchmark/buffers` (Antoine du Hamel) [#46473](https://github.com/nodejs/node/pull/46473)
* \[[`26e1a81243`](https://github.com/nodejs/node/commit/26e1a81243)] - **benchmark**: add trailing commas in `benchmark/module` (Antoine du Hamel) [#46461](https://github.com/nodejs/node/pull/46461)
* \[[`bd6c828cf3`](https://github.com/nodejs/node/commit/bd6c828cf3)] - **benchmark**: add trailing commas in `benchmark/net` (Antoine du Hamel) [#46439](https://github.com/nodejs/node/pull/46439)
* \[[`01cf87aca7`](https://github.com/nodejs/node/commit/01cf87aca7)] - **benchmark**: add trailing commas in `benchmark/util` (Antoine du Hamel) [#46438](https://github.com/nodejs/node/pull/46438)
* \[[`f006b2f9dc`](https://github.com/nodejs/node/commit/f006b2f9dc)] - **benchmark**: add trailing commas in `benchmark/async_hooks` (Antoine du Hamel) [#46424](https://github.com/nodejs/node/pull/46424)
* \[[`f969cc30ab`](https://github.com/nodejs/node/commit/f969cc30ab)] - **benchmark**: add trailing commas in `benchmark/fs` (Antoine du Hamel) [#46426](https://github.com/nodejs/node/pull/46426)
* \[[`5202b84382`](https://github.com/nodejs/node/commit/5202b84382)] - **bootstrap**: print stack trace during environment creation failure (Joyee Cheung) [#46533](https://github.com/nodejs/node/pull/46533)
* \[[`c6e722aca4`](https://github.com/nodejs/node/commit/c6e722aca4)] - **(SEMVER-MINOR)** **buffer**: add Buffer.copyBytesFrom(...) (James M Snell) [#46500](https://github.com/nodejs/node/pull/46500)
* \[[`886504fdf8`](https://github.com/nodejs/node/commit/886504fdf8)] - **build**: fix Visual Studio installation detection for Arm64 (Radek Bartoň) [#46420](https://github.com/nodejs/node/pull/46420)
* \[[`2b72a453cf`](https://github.com/nodejs/node/commit/2b72a453cf)] - **build**: add GitHub Action for coverage with --without-intl (Rich Trott) [#37954](https://github.com/nodejs/node/pull/37954)
* \[[`ff07aa7fe3`](https://github.com/nodejs/node/commit/ff07aa7fe3)] - **build**: do not disable inspector when intl is disabled (Rich Trott) [#37954](https://github.com/nodejs/node/pull/37954)
* \[[`4b25b98bd8`](https://github.com/nodejs/node/commit/4b25b98bd8)] - **build,test**: add proper support for IBM i (Xu Meng) [#46739](https://github.com/nodejs/node/pull/46739)
* \[[`535311097c`](https://github.com/nodejs/node/commit/535311097c)] - **child\_process**: add trailing commas in source files (Antoine du Hamel) [#46758](https://github.com/nodejs/node/pull/46758)
* \[[`d2692c65df`](https://github.com/nodejs/node/commit/d2692c65df)] - **cluster**: add trailing commas in source files (Antoine du Hamel) [#46695](https://github.com/nodejs/node/pull/46695)
* \[[`effdca8b10`](https://github.com/nodejs/node/commit/effdca8b10)] - **crypto**: don't assume FIPS is disabled by default (Michael Dawson) [#46532](https://github.com/nodejs/node/pull/46532)
* \[[`bce37c60ce`](https://github.com/nodejs/node/commit/bce37c60ce)] - **debugger**: improve validations and documents for watch and unwatch (Eungyu Lee) [#46947](https://github.com/nodejs/node/pull/46947)
* \[[`51253bae83`](https://github.com/nodejs/node/commit/51253bae83)] - **debugger**: add a command to set which lines to check for context (Eungyu Lee) [#46812](https://github.com/nodejs/node/pull/46812)
* \[[`44375c6a3c`](https://github.com/nodejs/node/commit/44375c6a3c)] - **debugger**: add trailing commas in source files (Antoine du Hamel) [#46714](https://github.com/nodejs/node/pull/46714)
* \[[`e5c4d69681`](https://github.com/nodejs/node/commit/e5c4d69681)] - **deps**: update ada to 1.0.4 (Node.js GitHub Bot) [#46853](https://github.com/nodejs/node/pull/46853)
* \[[`94f83536d7`](https://github.com/nodejs/node/commit/94f83536d7)] - **deps**: update ada to 1.0.3 (Node.js GitHub Bot) [#46784](https://github.com/nodejs/node/pull/46784)
* \[[`484c4f6674`](https://github.com/nodejs/node/commit/484c4f6674)] - **deps**: update ada to v1.0.1 (Yagiz Nizipli) [#46550](https://github.com/nodejs/node/pull/46550)
* \[[`0bc4c17e57`](https://github.com/nodejs/node/commit/0bc4c17e57)] - **deps**: add ada as a dependency (Yagiz Nizipli) [#46410](https://github.com/nodejs/node/pull/46410)
* \[[`956f786499`](https://github.com/nodejs/node/commit/956f786499)] - **deps**: update undici to 5.21.0 (Node.js GitHub Bot) [#47063](https://github.com/nodejs/node/pull/47063)
* \[[`73be4f8ef5`](https://github.com/nodejs/node/commit/73be4f8ef5)] - **deps**: update simdutf to 3.2.2 (Node.js GitHub Bot) [#46841](https://github.com/nodejs/node/pull/46841)
* \[[`0e78fd5883`](https://github.com/nodejs/node/commit/0e78fd5883)] - **deps**: update corepack to 0.17.0 (Node.js GitHub Bot) [#46842](https://github.com/nodejs/node/pull/46842)
* \[[`61c9433d8a`](https://github.com/nodejs/node/commit/61c9433d8a)] - **deps**: update simdutf to 3.2.1 (Node.js GitHub Bot) [#46800](https://github.com/nodejs/node/pull/46800)
* \[[`63a62ed532`](https://github.com/nodejs/node/commit/63a62ed532)] - **deps**: upgrade npm to 9.5.1 (npm team) [#46783](https://github.com/nodejs/node/pull/46783)
* \[[`c8974d678a`](https://github.com/nodejs/node/commit/c8974d678a)] - **deps**: update nghttp2 to 1.52.0 (Michaël Zasso) [#46636](https://github.com/nodejs/node/pull/46636)
* \[[`2b439a2cdf`](https://github.com/nodejs/node/commit/2b439a2cdf)] - **deps**: fix libuv for android (Julian Dropmann) [#46746](https://github.com/nodejs/node/pull/46746)
* \[[`d5eb1df869`](https://github.com/nodejs/node/commit/d5eb1df869)] - **deps**: update simdutf to 3.2.0 (Node.js GitHub Bot) [#46621](https://github.com/nodejs/node/pull/46621)
* \[[`dd97b05aeb`](https://github.com/nodejs/node/commit/dd97b05aeb)] - **deps**: update corepack to 0.16.0 (Node.js GitHub Bot) [#46710](https://github.com/nodejs/node/pull/46710)
* \[[`65b877de45`](https://github.com/nodejs/node/commit/65b877de45)] - **deps**: copy `postject-api.h` and `LICENSE` to the `deps` folder (Darshan Sen) [#46582](https://github.com/nodejs/node/pull/46582)
* \[[`a918ac886e`](https://github.com/nodejs/node/commit/a918ac886e)] - **deps**: update c-ares to 1.19.0 (Michaël Zasso) [#46415](https://github.com/nodejs/node/pull/46415)
* \[[`1ac639a240`](https://github.com/nodejs/node/commit/1ac639a240)] - **deps**: V8: cherry-pick 9ec4e9095a25 (Kleis Auke Wolthuizen) [#47092](https://github.com/nodejs/node/pull/47092)
* \[[`f8d4bf8540`](https://github.com/nodejs/node/commit/f8d4bf8540)] - **deps,test**: update postject to 1.0.0-alpha.5 (Node.js GitHub Bot) [#46934](https://github.com/nodejs/node/pull/46934)
* \[[`8646b06c1b`](https://github.com/nodejs/node/commit/8646b06c1b)] - **dgram**: fix unhandled exception aborting a closed udp socket (Ramana Venkata) [#46770](https://github.com/nodejs/node/pull/46770)
* \[[`e435199ccc`](https://github.com/nodejs/node/commit/e435199ccc)] - **doc**: remove remaining SSL\_OP\_NETSCAPE\_\*\_BUG (Tobias Nießen) [#47066](https://github.com/nodejs/node/pull/47066)
* \[[`01d82670c7`](https://github.com/nodejs/node/commit/01d82670c7)] - **doc**: fix typo in test.md (Victor Hiairrassary) [#47053](https://github.com/nodejs/node/pull/47053)
* \[[`0e3077dc48`](https://github.com/nodejs/node/commit/0e3077dc48)] - **doc**: amend support tier qualifier (Gireesh Punathil) [#42805](https://github.com/nodejs/node/pull/42805)
* \[[`a5bf6693b9`](https://github.com/nodejs/node/commit/a5bf6693b9)] - **doc**: fix typo on esm loaders example (Ruy Adorno) [#47015](https://github.com/nodejs/node/pull/47015)
* \[[`6a0c1d053e`](https://github.com/nodejs/node/commit/6a0c1d053e)] - **doc**: add missing test runner flags to man page (Colin Ihrig) [#46982](https://github.com/nodejs/node/pull/46982)
* \[[`43b94b0f13`](https://github.com/nodejs/node/commit/43b94b0f13)] - **doc**: fix history information for `node:diagnostics_channel` (Thomas Hunter II) [#46984](https://github.com/nodejs/node/pull/46984)
* \[[`b37d53a1ba`](https://github.com/nodejs/node/commit/b37d53a1ba)] - **doc**: fix myUrl is not defined in url (Youngmin Yoo) [#46968](https://github.com/nodejs/node/pull/46968)
* \[[`257c5ac1fa`](https://github.com/nodejs/node/commit/257c5ac1fa)] - **doc**: remove useless SSL\_OP\_\* options (Tobias Nießen) [#46954](https://github.com/nodejs/node/pull/46954)
* \[[`09c5e6a9f3`](https://github.com/nodejs/node/commit/09c5e6a9f3)] - **doc**: fix description of TLS dhparam option (Tobias Nießen) [#46949](https://github.com/nodejs/node/pull/46949)
* \[[`8907732fcf`](https://github.com/nodejs/node/commit/8907732fcf)] - **doc**: improve fs code example quality (jakecastelli) [#46948](https://github.com/nodejs/node/pull/46948)
* \[[`17a25f1153`](https://github.com/nodejs/node/commit/17a25f1153)] - **doc**: fix port of destination server is not defined in http2 (Deokjin Kim) [#46940](https://github.com/nodejs/node/pull/46940)
* \[[`ad06168a5c`](https://github.com/nodejs/node/commit/ad06168a5c)] - **doc**: use number which is bigger than 1024 as port in http2 (Deokjin Kim) [#46938](https://github.com/nodejs/node/pull/46938)
* \[[`4e6dda5be4`](https://github.com/nodejs/node/commit/4e6dda5be4)] - **doc**: add release key for Juan Arboleda (Juan José) [#46922](https://github.com/nodejs/node/pull/46922)
* \[[`f49c6e64ba`](https://github.com/nodejs/node/commit/f49c6e64ba)] - **doc**: fix links to SSL\_CTX\_set\_options (Tobias Nießen) [#46953](https://github.com/nodejs/node/pull/46953)
* \[[`ea7fb16e5c`](https://github.com/nodejs/node/commit/ea7fb16e5c)] - **doc**: fix fs missing import (jakecastelli) [#46907](https://github.com/nodejs/node/pull/46907)
* \[[`11885a7351`](https://github.com/nodejs/node/commit/11885a7351)] - **doc**: add request to hold off publicising sec releases (Michael Dawson) [#46702](https://github.com/nodejs/node/pull/46702)
* \[[`0254fd1da6`](https://github.com/nodejs/node/commit/0254fd1da6)] - **doc**: fix stream iterator helpers examples (Benjamin Gruenbaum) [#46897](https://github.com/nodejs/node/pull/46897)
* \[[`0a983f7125`](https://github.com/nodejs/node/commit/0a983f7125)] - **doc**: add history info for `node:test` (Antoine du Hamel) [#46851](https://github.com/nodejs/node/pull/46851)
* \[[`810d393ded`](https://github.com/nodejs/node/commit/810d393ded)] - **doc**: sort import order (jakecastelli) [#46847](https://github.com/nodejs/node/pull/46847)
* \[[`6e03499437`](https://github.com/nodejs/node/commit/6e03499437)] - **doc**: use destructing import (jakecastelli) [#46847](https://github.com/nodejs/node/pull/46847)
* \[[`8b636c3cd6`](https://github.com/nodejs/node/commit/8b636c3cd6)] - **doc**: add marco-ippolito to collaborators (Marco Ippolito) [#46816](https://github.com/nodejs/node/pull/46816)
* \[[`7e08ca125a`](https://github.com/nodejs/node/commit/7e08ca125a)] - **doc**: document how to use the tls.DEFAULT\_CIPHERS (Andreas Martens) [#46482](https://github.com/nodejs/node/pull/46482)
* \[[`3dae6f2f81`](https://github.com/nodejs/node/commit/3dae6f2f81)] - **doc**: add document for profiling and heap snapshot (cola119) [#46787](https://github.com/nodejs/node/pull/46787)
* \[[`eef30513b9`](https://github.com/nodejs/node/commit/eef30513b9)] - **doc**: add test:coverage event to custom reporter examples (Richie McColl) [#46752](https://github.com/nodejs/node/pull/46752)
* \[[`e6db6bedf7`](https://github.com/nodejs/node/commit/e6db6bedf7)] - **doc**: include context on .toWeb() parameters (Debadree Chatterjee) [#46617](https://github.com/nodejs/node/pull/46617)
* \[[`a24350e49f`](https://github.com/nodejs/node/commit/a24350e49f)] - **doc**: add in security steward for recent release (Michael Dawson) [#46701](https://github.com/nodejs/node/pull/46701)
* \[[`55360e9386`](https://github.com/nodejs/node/commit/55360e9386)] - **doc**: clarify semver-minor notable changes approach (Beth Griggs) [#46592](https://github.com/nodejs/node/pull/46592)
* \[[`a384dd42ff`](https://github.com/nodejs/node/commit/a384dd42ff)] - **doc**: maintaining nghttp2 (Marco Ippolito) [#46539](https://github.com/nodejs/node/pull/46539)
* \[[`45fccc9737`](https://github.com/nodejs/node/commit/45fccc9737)] - **doc**: add emit to NodeEventTarget (Deokjin Kim) [#46356](https://github.com/nodejs/node/pull/46356)
* \[[`760616890c`](https://github.com/nodejs/node/commit/760616890c)] - **doc**: add debadree25 to collaborators (Debadree Chatterjee) [#46716](https://github.com/nodejs/node/pull/46716)
* \[[`b9dd876e7c`](https://github.com/nodejs/node/commit/b9dd876e7c)] - **doc**: move bcoe to emeriti (Benjamin Coe) [#46703](https://github.com/nodejs/node/pull/46703)
* \[[`3afbb92bb4`](https://github.com/nodejs/node/commit/3afbb92bb4)] - **doc**: add response.strictContentLength to documentation (Marco Ippolito) [#46627](https://github.com/nodejs/node/pull/46627)
* \[[`2c0e1aa095`](https://github.com/nodejs/node/commit/2c0e1aa095)] - **doc**: remove unused functions from example of `streamConsumers.text` (Deokjin Kim) [#46581](https://github.com/nodejs/node/pull/46581)
* \[[`61268303fc`](https://github.com/nodejs/node/commit/61268303fc)] - **doc**: fix test runner examples (Richie McColl) [#46565](https://github.com/nodejs/node/pull/46565)
* \[[`2b702c98c2`](https://github.com/nodejs/node/commit/2b702c98c2)] - **doc**: update test concurrency description / default values (richiemccoll) [#46457](https://github.com/nodejs/node/pull/46457)
* \[[`f1de3f7a31`](https://github.com/nodejs/node/commit/f1de3f7a31)] - **doc**: enrich test command with executable (Tony Gorez) [#44347](https://github.com/nodejs/node/pull/44347)
* \[[`68b5cf8e38`](https://github.com/nodejs/node/commit/68b5cf8e38)] - **doc**: fix wrong location of `requestTimeout`'s default value (Deokjin Kim) [#46423](https://github.com/nodejs/node/pull/46423)
* \[[`4d5d6d2193`](https://github.com/nodejs/node/commit/4d5d6d2193)] - **doc**: add deokjinkim to collaborators (Deokjin Kim) [#46444](https://github.com/nodejs/node/pull/46444)
* \[[`de7f6182be`](https://github.com/nodejs/node/commit/de7f6182be)] - **doc**: fix -C flag usage (三咲智子 Kevin Deng) [#46388](https://github.com/nodejs/node/pull/46388)
* \[[`4165cf34ba`](https://github.com/nodejs/node/commit/4165cf34ba)] - **doc**: add note about major release rotation (Rafael Gonzaga) [#46436](https://github.com/nodejs/node/pull/46436)
* \[[`f088ce2dc7`](https://github.com/nodejs/node/commit/f088ce2dc7)] - **doc**: update threat model based on discussions (Michael Dawson) [#46373](https://github.com/nodejs/node/pull/46373)
* \[[`5b94e2bcdb`](https://github.com/nodejs/node/commit/5b94e2bcdb)] - **esm**: fix import assertion warning (Antoine du Hamel) [#46971](https://github.com/nodejs/node/pull/46971)
* \[[`96a39d1a99`](https://github.com/nodejs/node/commit/96a39d1a99)] - **esm**: add a runtime warning when using import assertions (Antoine du Hamel) [#46901](https://github.com/nodejs/node/pull/46901)
* \[[`320a8adb45`](https://github.com/nodejs/node/commit/320a8adb45)] - **esm**: misc test refactors (Geoffrey Booth) [#46631](https://github.com/nodejs/node/pull/46631)
* \[[`b08687f739`](https://github.com/nodejs/node/commit/b08687f739)] - **events**: add trailing commas in source files (Antoine du Hamel) [#46759](https://github.com/nodejs/node/pull/46759)
* \[[`cc6deeaf7a`](https://github.com/nodejs/node/commit/cc6deeaf7a)] - **(SEMVER-MINOR)** **events**: add listener argument to listenerCount (Paolo Insogna) [#46523](https://github.com/nodejs/node/pull/46523)
* \[[`efc24097a6`](https://github.com/nodejs/node/commit/efc24097a6)] - **fs**: add trailing commas in source files (Antoine du Hamel) [#46696](https://github.com/nodejs/node/pull/46696)
* \[[`80b4e6da53`](https://github.com/nodejs/node/commit/80b4e6da53)] - **http**: use listenerCount when adding noop event (Paolo Insogna) [#46769](https://github.com/nodejs/node/pull/46769)
* \[[`3538521bf6`](https://github.com/nodejs/node/commit/3538521bf6)] - **http**: correctly calculate strict content length (Robert Nagy) [#46601](https://github.com/nodejs/node/pull/46601)
* \[[`9582c8ef3a`](https://github.com/nodejs/node/commit/9582c8ef3a)] - **http**: fix validation of "Link" header (Steve Herzog) [#46466](https://github.com/nodejs/node/pull/46466)
* \[[`23c1e2fa52`](https://github.com/nodejs/node/commit/23c1e2fa52)] - **http**: unify header treatment (Marco Ippolito) [#46528](https://github.com/nodejs/node/pull/46528)
* \[[`abeee994c4`](https://github.com/nodejs/node/commit/abeee994c4)] - **http**: add note about clientError event (Paolo Insogna) [#46584](https://github.com/nodejs/node/pull/46584)
* \[[`3d0602c96c`](https://github.com/nodejs/node/commit/3d0602c96c)] - **http**: use v8::Array::New() with a prebuilt vector (Joyee Cheung) [#46447](https://github.com/nodejs/node/pull/46447)
* \[[`62cbddd86f`](https://github.com/nodejs/node/commit/62cbddd86f)] - **lib**: fix trailing commas and leftover function from rebasing (Danielle Adams) [#47503](https://github.com/nodejs/node/pull/47503)
* \[[`c463f133bd`](https://github.com/nodejs/node/commit/c463f133bd)] - **lib**: enforce use of trailing commas (Antoine du Hamel) [#46881](https://github.com/nodejs/node/pull/46881)
* \[[`0f33bb0961`](https://github.com/nodejs/node/commit/0f33bb0961)] - **lib**: add trailing commas to all public core modules (Antoine du Hamel) [#46848](https://github.com/nodejs/node/pull/46848)
* \[[`06e0dd3e15`](https://github.com/nodejs/node/commit/06e0dd3e15)] - **lib**: rename internal module declaration as internal bindings (okmttdhr, okp) [#46663](https://github.com/nodejs/node/pull/46663)
* \[[`31578ab1b4`](https://github.com/nodejs/node/commit/31578ab1b4)] - **lib**: add trailing commas to more internal files (Antoine du Hamel) [#46811](https://github.com/nodejs/node/pull/46811)
* \[[`ad510d9029`](https://github.com/nodejs/node/commit/ad510d9029)] - **lib**: update punycode to 2.3.0 (Yagiz Nizipli) [#46719](https://github.com/nodejs/node/pull/46719)
* \[[`4cf3de8b02`](https://github.com/nodejs/node/commit/4cf3de8b02)] - **lib**: add trailing commas in `internal/perf` (Antoine du Hamel) [#46697](https://github.com/nodejs/node/pull/46697)
* \[[`f1b79828bc`](https://github.com/nodejs/node/commit/f1b79828bc)] - **(SEMVER-MINOR)** **lib**: add AsyncLocalStorage.bind() and .snapshot() (flakey5) [#46387](https://github.com/nodejs/node/pull/46387)
* \[[`48cd712c0d`](https://github.com/nodejs/node/commit/48cd712c0d)] - **lib**: add trailing commas in `internal/process` (Antoine du Hamel) [#46687](https://github.com/nodejs/node/pull/46687)
* \[[`46a22ab601`](https://github.com/nodejs/node/commit/46a22ab601)] - **lib**: do not crash using workers with disabled shared array buffers (Ruben Bridgewater) [#41023](https://github.com/nodejs/node/pull/41023)
* \[[`1395e36e64`](https://github.com/nodejs/node/commit/1395e36e64)] - **lib**: delete module findPath unused params (sinkhaha) [#45371](https://github.com/nodejs/node/pull/45371)
* \[[`c410572620`](https://github.com/nodejs/node/commit/c410572620)] - **lib**: enforce use of trailing commas in more files (Antoine du Hamel) [#46655](https://github.com/nodejs/node/pull/46655)
* \[[`36e080cd13`](https://github.com/nodejs/node/commit/36e080cd13)] - **lib**: enforce use of trailing commas for functions (Antoine du Hamel) [#46629](https://github.com/nodejs/node/pull/46629)
* \[[`71249a6c00`](https://github.com/nodejs/node/commit/71249a6c00)] - **lib**: predeclare Event.isTrusted prop descriptor (Santiago Gimeno) [#46527](https://github.com/nodejs/node/pull/46527)
* \[[`2b80d19c06`](https://github.com/nodejs/node/commit/2b80d19c06)] - **lib**: tighten `AbortSignal.prototype.throwIfAborted` implementation (Antoine du Hamel) [#46521](https://github.com/nodejs/node/pull/46521)
* \[[`2f79f3fa38`](https://github.com/nodejs/node/commit/2f79f3fa38)] - **(SEMVER-MINOR)** **lib**: add aborted() utility function (Debadree Chatterjee) [#46494](https://github.com/nodejs/node/pull/46494)
* \[[`7f9e60aa1a`](https://github.com/nodejs/node/commit/7f9e60aa1a)] - **lib,src**: fix a few typos in comments (Tobias Nießen) [#46835](https://github.com/nodejs/node/pull/46835)
* \[[`c4f192b592`](https://github.com/nodejs/node/commit/c4f192b592)] - **meta**: add single-executable labels and code owners (Joyee Cheung) [#47004](https://github.com/nodejs/node/pull/47004)
* \[[`abe1488f86`](https://github.com/nodejs/node/commit/abe1488f86)] - **meta**: remove AUTHORS file (Rich Trott) [#46845](https://github.com/nodejs/node/pull/46845)
* \[[`e53774c7bf`](https://github.com/nodejs/node/commit/e53774c7bf)] - **meta**: remove unnecessary onboarding step (Rich Trott) [#46793](https://github.com/nodejs/node/pull/46793)
* \[[`55f1cfd7e4`](https://github.com/nodejs/node/commit/55f1cfd7e4)] - **meta**: update CODEOWNERS of url implementations (Yagiz Nizipli) [#46775](https://github.com/nodejs/node/pull/46775)
* \[[`ccaa7cabdd`](https://github.com/nodejs/node/commit/ccaa7cabdd)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#46726](https://github.com/nodejs/node/pull/46726)
* \[[`260ddd5b0f`](https://github.com/nodejs/node/commit/260ddd5b0f)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#46624](https://github.com/nodejs/node/pull/46624)
* \[[`5ef78e50a0`](https://github.com/nodejs/node/commit/5ef78e50a0)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#46513](https://github.com/nodejs/node/pull/46513)
* \[[`5efb829156`](https://github.com/nodejs/node/commit/5efb829156)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#46504](https://github.com/nodejs/node/pull/46504)
* \[[`f0705f7de1`](https://github.com/nodejs/node/commit/f0705f7de1)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#46411](https://github.com/nodejs/node/pull/46411)
* \[[`331073a7aa`](https://github.com/nodejs/node/commit/331073a7aa)] - **node-api**: add \_\_wasm32\_\_ guards on async works (Chengzhong Wu) [#46633](https://github.com/nodejs/node/pull/46633)
* \[[`594bb9d558`](https://github.com/nodejs/node/commit/594bb9d558)] - **os**: improve network interface performance (Ruben Bridgewater) [#46598](https://github.com/nodejs/node/pull/46598)
* \[[`c231aae6c5`](https://github.com/nodejs/node/commit/c231aae6c5)] - **process**: print versions by sort (Himself65) [#46428](https://github.com/nodejs/node/pull/46428)
* \[[`fe318dd095`](https://github.com/nodejs/node/commit/fe318dd095)] - **punycode**: add pending deprecation (Antoine du Hamel) [#46719](https://github.com/nodejs/node/pull/46719)
* \[[`34ba230d9b`](https://github.com/nodejs/node/commit/34ba230d9b)] - **repl**: remove lastInputPreview conditional check (Duy Mac Van) [#46857](https://github.com/nodejs/node/pull/46857)
* \[[`09739a20b1`](https://github.com/nodejs/node/commit/09739a20b1)] - **repl**: fix .load infinite loop caused by shared use of lineEnding RegExp (Theodor Steiner) [#46742](https://github.com/nodejs/node/pull/46742)
* \[[`91fbad617d`](https://github.com/nodejs/node/commit/91fbad617d)] - **repl**: add trailing commas in source files (Antoine du Hamel) [#46757](https://github.com/nodejs/node/pull/46757)
* \[[`e4cd81b526`](https://github.com/nodejs/node/commit/e4cd81b526)] - **src**: use string\_view for report and related code (Anna Henningsen) [#46723](https://github.com/nodejs/node/pull/46723)
* \[[`8ea835419e`](https://github.com/nodejs/node/commit/8ea835419e)] - **(SEMVER-MINOR)** **src**: add initial support for single executable applications (Darshan Sen) [#45038](https://github.com/nodejs/node/pull/45038)
* \[[`4652dfdc88`](https://github.com/nodejs/node/commit/4652dfdc88)] - **src**: remove TLSEXT\_TYPE\_alpn guard (Tobias Nießen) [#46956](https://github.com/nodejs/node/pull/46956)
* \[[`a4a433fc0b`](https://github.com/nodejs/node/commit/a4a433fc0b)] - **src**: remove use of SSL\_OP\_SINGLE\_DH\_USE (Tobias Nießen) [#46955](https://github.com/nodejs/node/pull/46955)
* \[[`339b52f3bc`](https://github.com/nodejs/node/commit/339b52f3bc)] - **src**: make util.h self-containted (Joyee Cheung) [#46817](https://github.com/nodejs/node/pull/46817)
* \[[`b25ee523ea`](https://github.com/nodejs/node/commit/b25ee523ea)] - **src**: remove shadowed variable in OptionsParser (Shelley Vohr) [#46672](https://github.com/nodejs/node/pull/46672)
* \[[`88baf98fa3`](https://github.com/nodejs/node/commit/88baf98fa3)] - **src**: do not track BaseObjects directly in Realm (Joyee Cheung) [#46470](https://github.com/nodejs/node/pull/46470)
* \[[`03e8d824db`](https://github.com/nodejs/node/commit/03e8d824db)] - **src**: fix cb scope bugs involved in termination (ywave620) [#45596](https://github.com/nodejs/node/pull/45596)
* \[[`7fb32b43dc`](https://github.com/nodejs/node/commit/7fb32b43dc)] - **src**: add SetFastMethodNoSideEffect() (Joyee Cheung) [#46619](https://github.com/nodejs/node/pull/46619)
* \[[`ea411d6422`](https://github.com/nodejs/node/commit/ea411d6422)] - **(SEMVER-MINOR)** **src**: allow optional Isolate termination in node::Stop() (Shelley Vohr) [#46583](https://github.com/nodejs/node/pull/46583)
* \[[`f8d029183a`](https://github.com/nodejs/node/commit/f8d029183a)] - **src**: remove icu usage from node\_string.cc (Yagiz Nizipli) [#46548](https://github.com/nodejs/node/pull/46548)
* \[[`95674360b2`](https://github.com/nodejs/node/commit/95674360b2)] - **src**: add fflush() to SnapshotData::ToFile() (Anna Henningsen) [#46531](https://github.com/nodejs/node/pull/46531)
* \[[`9445a0a5f6`](https://github.com/nodejs/node/commit/9445a0a5f6)] - **src**: make edge names in BaseObjects more descriptive in heap snapshots (Joyee Cheung) [#46492](https://github.com/nodejs/node/pull/46492)
* \[[`483cd1922f`](https://github.com/nodejs/node/commit/483cd1922f)] - **src**: avoid leaking snapshot fp on error (Tobias Nießen) [#46497](https://github.com/nodejs/node/pull/46497)
* \[[`bc9a577625`](https://github.com/nodejs/node/commit/bc9a577625)] - **src**: check return value of ftell() (Tobias Nießen) [#46495](https://github.com/nodejs/node/pull/46495)
* \[[`b3b3b33799`](https://github.com/nodejs/node/commit/b3b3b33799)] - **src**: remove unused includes from main thread (Yagiz Nizipli) [#46471](https://github.com/nodejs/node/pull/46471)
* \[[`b9ca302558`](https://github.com/nodejs/node/commit/b9ca302558)] - **src**: use string\_view instead of std::string& (Yagiz Nizipli) [#46471](https://github.com/nodejs/node/pull/46471)
* \[[`f1c770065a`](https://github.com/nodejs/node/commit/f1c770065a)] - **src**: use simdutf utf8 to utf16 instead of icu (Yagiz Nizipli) [#46471](https://github.com/nodejs/node/pull/46471)
* \[[`aaf04f498a`](https://github.com/nodejs/node/commit/aaf04f498a)] - **src**: replace icu with simdutf for char counts (Yagiz Nizipli) [#46472](https://github.com/nodejs/node/pull/46472)
* \[[`3aef68d079`](https://github.com/nodejs/node/commit/3aef68d079)] - **src**: add additional utilities to crypto::SecureContext (James M Snell) [#45912](https://github.com/nodejs/node/pull/45912)
* \[[`92fdb24fac`](https://github.com/nodejs/node/commit/92fdb24fac)] - **src**: add KeyObjectHandle::HasInstance (James M Snell) [#45912](https://github.com/nodejs/node/pull/45912)
* \[[`a639e2a557`](https://github.com/nodejs/node/commit/a639e2a557)] - **src**: add GetCurrentCipherName/Version to crypto\_common (James M Snell) [#45912](https://github.com/nodejs/node/pull/45912)
* \[[`8dfa748373`](https://github.com/nodejs/node/commit/8dfa748373)] - **src**: back snapshot I/O with a std::vector sink (Joyee Cheung) [#46463](https://github.com/nodejs/node/pull/46463)
* \[[`17eea1812e`](https://github.com/nodejs/node/commit/17eea1812e)] - **(SEMVER-MINOR)** **src**: allow embedder control of code generation policy (Shelley Vohr) [#46368](https://github.com/nodejs/node/pull/46368)
* \[[`dffe7d814e`](https://github.com/nodejs/node/commit/dffe7d814e)] - **stream**: enable usage of webstreams on compose() (Debadree Chatterjee) [#46675](https://github.com/nodejs/node/pull/46675)
* \[[`654b7474e3`](https://github.com/nodejs/node/commit/654b7474e3)] - **stream**: always delay construct callback by a nextTick (Matteo Collina) [#46818](https://github.com/nodejs/node/pull/46818)
* \[[`ed2faa7c7c`](https://github.com/nodejs/node/commit/ed2faa7c7c)] - **stream**: fix respondWithNewView() errors when view\.byteOffset != 0 (Debadree Chatterjee) [#46465](https://github.com/nodejs/node/pull/46465)
* \[[`247caacc7a`](https://github.com/nodejs/node/commit/247caacc7a)] - **stream**: fix pipeline callback not called on ended stream (Debadree Chatterjee) [#46600](https://github.com/nodejs/node/pull/46600)
* \[[`c40eec3f3b`](https://github.com/nodejs/node/commit/c40eec3f3b)] - **stream**: add trailing commas in webstream source files (Antoine du Hamel) [#46685](https://github.com/nodejs/node/pull/46685)
* \[[`d669f22567`](https://github.com/nodejs/node/commit/d669f22567)] - **stream**: add trailing commas in stream source files (Antoine du Hamel) [#46686](https://github.com/nodejs/node/pull/46686)
* \[[`c60816a68d`](https://github.com/nodejs/node/commit/c60816a68d)] - **(SEMVER-MINOR)** **stream**: add abort signal for ReadableStream and WritableStream (Debadree Chatterjee) [#46273](https://github.com/nodejs/node/pull/46273)
* \[[`17b4e9bc5f`](https://github.com/nodejs/node/commit/17b4e9bc5f)] - **stream**: refactor to use `validateAbortSignal` (Antoine du Hamel) [#46520](https://github.com/nodejs/node/pull/46520)
* \[[`4d81124397`](https://github.com/nodejs/node/commit/4d81124397)] - **stream**: allow transfer of readable byte streams (MrBBot) [#45955](https://github.com/nodejs/node/pull/45955)
* \[[`6c454af5ea`](https://github.com/nodejs/node/commit/6c454af5ea)] - **stream**: add pipeline() for webstreams (Debadree Chatterjee) [#46307](https://github.com/nodejs/node/pull/46307)
* \[[`91a550e5a1`](https://github.com/nodejs/node/commit/91a550e5a1)] - **stream**: add suport for abort signal in finished() for webstreams (Debadree Chatterjee) [#46403](https://github.com/nodejs/node/pull/46403)
* \[[`406ff320bf`](https://github.com/nodejs/node/commit/406ff320bf)] - **stream**: dont access Object.prototype.type during TransformStream init (Debadree Chatterjee) [#46389](https://github.com/nodejs/node/pull/46389)
* \[[`972bdee8c4`](https://github.com/nodejs/node/commit/972bdee8c4)] - **test**: update web-platform tests for url (Yagiz Nizipli) [#46547](https://github.com/nodejs/node/pull/46547)
* \[[`ba340a0ddb`](https://github.com/nodejs/node/commit/ba340a0ddb)] - **test**: move `test-tls-autoselectfamily-servername` to `test/internet` (Antoine du Hamel) [#47029](https://github.com/nodejs/node/pull/47029)
* \[[`0edaf016cc`](https://github.com/nodejs/node/commit/0edaf016cc)] - **test**: fallback to IPv4 if IPv6 is unavailable (Abdirahim Musse) [#47017](https://github.com/nodejs/node/pull/47017)
* \[[`ef6c86c59f`](https://github.com/nodejs/node/commit/ef6c86c59f)] - **test**: simplify test-tls-ecdh-multiple (Tobias Nießen) [#46963](https://github.com/nodejs/node/pull/46963)
* \[[`b8cdd1502f`](https://github.com/nodejs/node/commit/b8cdd1502f)] - **test**: improve test coverage of lib/dns (Anderson Paiva) [#46910](https://github.com/nodejs/node/pull/46910)
* \[[`33991954b6`](https://github.com/nodejs/node/commit/33991954b6)] - **test**: simplify test-tls-ecdh-auto (Tobias Nießen) [#46911](https://github.com/nodejs/node/pull/46911)
* \[[`512f28fa8b`](https://github.com/nodejs/node/commit/512f28fa8b)] - **test**: move testPath from CWD to temporary directory (Livia Medeiros) [#46890](https://github.com/nodejs/node/pull/46890)
* \[[`3fdcf7bef4`](https://github.com/nodejs/node/commit/3fdcf7bef4)] - **test**: assume priv ports start at 1024 if it can't be changed (KrayzeeKev) [#46536](https://github.com/nodejs/node/pull/46536)
* \[[`966d0d8b16`](https://github.com/nodejs/node/commit/966d0d8b16)] - **test**: fix os-release check for Ubuntu in SEA test (Anna Henningsen) [#46838](https://github.com/nodejs/node/pull/46838)
* \[[`b344e74bc1`](https://github.com/nodejs/node/commit/b344e74bc1)] - **test**: move socket from CWD to temporary directory (Livia Medeiros) [#46863](https://github.com/nodejs/node/pull/46863)
* \[[`dfb566aec2`](https://github.com/nodejs/node/commit/dfb566aec2)] - **test**: fix test-net-connect-reset-until-connected (Vita Batrla) [#46781](https://github.com/nodejs/node/pull/46781)
* \[[`364479644c`](https://github.com/nodejs/node/commit/364479644c)] - **test**: simplify test-tls-alert (Tobias Nießen) [#46805](https://github.com/nodejs/node/pull/46805)
* \[[`df149448b5`](https://github.com/nodejs/node/commit/df149448b5)] - **test**: update encoding WPTs (Filip Skokan) [#46802](https://github.com/nodejs/node/pull/46802)
* \[[`928998dd2d`](https://github.com/nodejs/node/commit/928998dd2d)] - **test**: remove useless WPT init scripts (Filip Skokan) [#46801](https://github.com/nodejs/node/pull/46801)
* \[[`bb182461a3`](https://github.com/nodejs/node/commit/bb182461a3)] - **test**: remove unreachable return (jakecastelli) [#46807](https://github.com/nodejs/node/pull/46807)
* \[[`ea71a2ae1b`](https://github.com/nodejs/node/commit/ea71a2ae1b)] - **test**: fix test-v8-collect-gc-profile-in-worker.js (theanarkh) [#46735](https://github.com/nodejs/node/pull/46735)
* \[[`25cab7eab5`](https://github.com/nodejs/node/commit/25cab7eab5)] - **test**: improve control flow in test-tls-dhe (Tobias Nießen) [#46751](https://github.com/nodejs/node/pull/46751)
* \[[`3b70e7a34e`](https://github.com/nodejs/node/commit/3b70e7a34e)] - **test**: include strace openat test (Rafael Gonzaga) [#46150](https://github.com/nodejs/node/pull/46150)
* \[[`533170d456`](https://github.com/nodejs/node/commit/533170d456)] - **test**: remove OpenSSL 1.0.2 error message compat (Tobias Nießen) [#46709](https://github.com/nodejs/node/pull/46709)
* \[[`b8948996b4`](https://github.com/nodejs/node/commit/b8948996b4)] - **test**: fix flaky test-watch-mode-files\_watcher (Moshe Atlow) [#46738](https://github.com/nodejs/node/pull/46738)
* \[[`931111609e`](https://github.com/nodejs/node/commit/931111609e)] - **test**: use newish OpenSSL feature in test-tls-dhe (Tobias Nießen) [#46708](https://github.com/nodejs/node/pull/46708)
* \[[`847d74005c`](https://github.com/nodejs/node/commit/847d74005c)] - **test**: fix assertions in test-snapshot-dns-lookup\* (Tobias Nießen) [#46618](https://github.com/nodejs/node/pull/46618)
* \[[`d7b995cd38`](https://github.com/nodejs/node/commit/d7b995cd38)] - **test**: cover publicExponent validation in OpenSSL (Tobias Nießen) [#46632](https://github.com/nodejs/node/pull/46632)
* \[[`a2b103a761`](https://github.com/nodejs/node/commit/a2b103a761)] - **test**: add trailing commas in `test/pummel` (Antoine du Hamel) [#46610](https://github.com/nodejs/node/pull/46610)
* \[[`4fb2fc7536`](https://github.com/nodejs/node/commit/4fb2fc7536)] - **test**: enable api-invalid-label.any.js in encoding WPTs (Filip Skokan) [#46506](https://github.com/nodejs/node/pull/46506)
* \[[`b0f2415312`](https://github.com/nodejs/node/commit/b0f2415312)] - **test**: add trailing commas in `test/js-native-api` (Antoine du Hamel) [#46385](https://github.com/nodejs/node/pull/46385)
* \[[`534562f51d`](https://github.com/nodejs/node/commit/534562f51d)] - **test**: make more crypto tests work with BoringSSL (Shelley Vohr) [#46429](https://github.com/nodejs/node/pull/46429)
* \[[`9bb08527f0`](https://github.com/nodejs/node/commit/9bb08527f0)] - **test**: add trailing commas in `test/known_issues` (Antoine du Hamel) [#46408](https://github.com/nodejs/node/pull/46408)
* \[[`231f0ee9b2`](https://github.com/nodejs/node/commit/231f0ee9b2)] - **test**: add trailing commas in `test/internet` (Antoine du Hamel) [#46407](https://github.com/nodejs/node/pull/46407)
* \[[`5e008e7d38`](https://github.com/nodejs/node/commit/5e008e7d38)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#47010](https://github.com/nodejs/node/pull/47010)
* \[[`99bfbed78a`](https://github.com/nodejs/node/commit/99bfbed78a)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#46575](https://github.com/nodejs/node/pull/46575)
* \[[`8398f9fcb6`](https://github.com/nodejs/node/commit/8398f9fcb6)] - **test\_runner**: default to spec reporter when on TTY environment (Moshe Atlow) [#46969](https://github.com/nodejs/node/pull/46969)
* \[[`16db3ad1f8`](https://github.com/nodejs/node/commit/16db3ad1f8)] - **test\_runner**: handle errors not bound to tests (Colin Ihrig) [#46962](https://github.com/nodejs/node/pull/46962)
* \[[`d60eef25a1`](https://github.com/nodejs/node/commit/d60eef25a1)] - **test\_runner**: throw if harness is not bootstrapped (Colin Ihrig) [#46962](https://github.com/nodejs/node/pull/46962)
* \[[`ab5b3185f1`](https://github.com/nodejs/node/commit/ab5b3185f1)] - **test\_runner**: track bootstrapping process (Colin Ihrig) [#46962](https://github.com/nodejs/node/pull/46962)
* \[[`413493cab9`](https://github.com/nodejs/node/commit/413493cab9)] - **test\_runner**: avoid running twice tests in describe (Moshe Atlow) [#46888](https://github.com/nodejs/node/pull/46888)
* \[[`524eaf5488`](https://github.com/nodejs/node/commit/524eaf5488)] - **test\_runner**: fix reconstruction of errors extracted from YAML (Moshe Atlow) [#46872](https://github.com/nodejs/node/pull/46872)
* \[[`17fbfb23bd`](https://github.com/nodejs/node/commit/17fbfb23bd)] - **test\_runner**: reset count on watch mode (Moshe Atlow) [#46577](https://github.com/nodejs/node/pull/46577)
* \[[`3b077a6885`](https://github.com/nodejs/node/commit/3b077a6885)] - **test\_runner**: remove root tracking set (Colin Ihrig) [#46961](https://github.com/nodejs/node/pull/46961)
* \[[`15503ffd12`](https://github.com/nodejs/node/commit/15503ffd12)] - **test\_runner**: give the root test a harness reference (Colin Ihrig) [#46962](https://github.com/nodejs/node/pull/46962)
* \[[`e52b169ef4`](https://github.com/nodejs/node/commit/e52b169ef4)] - **test\_runner**: align behavior of it and test (Moshe Atlow) [#46889](https://github.com/nodejs/node/pull/46889)
* \[[`9b58857630`](https://github.com/nodejs/node/commit/9b58857630)] - **timers**: cleanup no-longer relevant TODOs in timers/promises (James M Snell) [#46499](https://github.com/nodejs/node/pull/46499)
* \[[`32c527d80f`](https://github.com/nodejs/node/commit/32c527d80f)] - **(SEMVER-MINOR)** **tls**: support automatic DHE (Tobias Nießen) [#46978](https://github.com/nodejs/node/pull/46978)
* \[[`6ff90270ca`](https://github.com/nodejs/node/commit/6ff90270ca)] - **tls**: add trailing commas in source files (Antoine du Hamel) [#46715](https://github.com/nodejs/node/pull/46715)
* \[[`958da453dd`](https://github.com/nodejs/node/commit/958da453dd)] - **tools**: fix update-ada script (Yagiz Nizipli) [#46550](https://github.com/nodejs/node/pull/46550)
* \[[`a3f272e667`](https://github.com/nodejs/node/commit/a3f272e667)] - **tools**: automate cares update (Marco Ippolito) [#46993](https://github.com/nodejs/node/pull/46993)
* \[[`77f5b69840`](https://github.com/nodejs/node/commit/77f5b69840)] - **tools**: update lint-md-dependencies to rollup\@3.18.0 (Node.js GitHub Bot) [#46935](https://github.com/nodejs/node/pull/46935)
* \[[`36ea9e7f65`](https://github.com/nodejs/node/commit/36ea9e7f65)] - **tools**: refactor dep\_updaters (Tony Gorez) [#46488](https://github.com/nodejs/node/pull/46488)
* \[[`c0b6794f82`](https://github.com/nodejs/node/commit/c0b6794f82)] - **tools**: update eslint to 8.35.0 (Node.js GitHub Bot) [#46854](https://github.com/nodejs/node/pull/46854)
* \[[`307d096e21`](https://github.com/nodejs/node/commit/307d096e21)] - **tools**: create llhttp update action (Marco Ippolito) [#46766](https://github.com/nodejs/node/pull/46766)
* \[[`fff31862ac`](https://github.com/nodejs/node/commit/fff31862ac)] - **tools**: fix linter message when using global `DOMException` (Antoine du Hamel) [#46822](https://github.com/nodejs/node/pull/46822)
* \[[`89c2e8ac97`](https://github.com/nodejs/node/commit/89c2e8ac97)] - **tools**: update lint-md-dependencies to rollup\@3.17.3 (Node.js GitHub Bot) [#46843](https://github.com/nodejs/node/pull/46843)
* \[[`3bf54ff624`](https://github.com/nodejs/node/commit/3bf54ff624)] - **tools**: update lint-md-dependencies to rollup\@3.17.2 (Node.js GitHub Bot) [#46771](https://github.com/nodejs/node/pull/46771)
* \[[`4065e5cda6`](https://github.com/nodejs/node/commit/4065e5cda6)] - **tools**: run format-cpp on node-api test c files (Chengzhong Wu) [#46694](https://github.com/nodejs/node/pull/46694)
* \[[`7349708527`](https://github.com/nodejs/node/commit/7349708527)] - **tools**: use actions pinned by commit hash in coverage-linux (Gabriela Gutierrez) [#46294](https://github.com/nodejs/node/pull/46294)
* \[[`f80e93d71f`](https://github.com/nodejs/node/commit/f80e93d71f)] - **tools**: fix regex strings in Python tools (Jan Osusky) [#46671](https://github.com/nodejs/node/pull/46671)
* \[[`8fd176055f`](https://github.com/nodejs/node/commit/8fd176055f)] - **tools**: fixed path (Marco Ippolito) [#46700](https://github.com/nodejs/node/pull/46700)
* \[[`15b0e8b1aa`](https://github.com/nodejs/node/commit/15b0e8b1aa)] - **tools**: update nghttp2 action (Marco Ippolito) [#46700](https://github.com/nodejs/node/pull/46700)
* \[[`4da3eecbd7`](https://github.com/nodejs/node/commit/4da3eecbd7)] - **tools**: update-nghttp2 preserve config.h (Marco Ippolito) [#46698](https://github.com/nodejs/node/pull/46698)
* \[[`3714d8f236`](https://github.com/nodejs/node/commit/3714d8f236)] - **tools**: update lint-md-dependencies to rollup\@3.17.1 (Node.js GitHub Bot) [#46712](https://github.com/nodejs/node/pull/46712)
* \[[`474084e063`](https://github.com/nodejs/node/commit/474084e063)] - **tools**: update lint-md-dependencies to rollup\@3.17.0 (Node.js GitHub Bot) [#46712](https://github.com/nodejs/node/pull/46712)
* \[[`665b7424eb`](https://github.com/nodejs/node/commit/665b7424eb)] - **tools**: fix bug in `prefer-primordials` lint rule (Antoine du Hamel) [#46659](https://github.com/nodejs/node/pull/46659)
* \[[`7eb1456892`](https://github.com/nodejs/node/commit/7eb1456892)] - **tools**: update eslint to 8.34.0 (Node.js GitHub Bot) [#46625](https://github.com/nodejs/node/pull/46625)
* \[[`678b8f53c1`](https://github.com/nodejs/node/commit/678b8f53c1)] - **tools**: update lint-md-dependencies to rollup\@3.15.0 to-vfile\@7.2.4 (Node.js GitHub Bot) [#46623](https://github.com/nodejs/node/pull/46623)
* \[[`ee81da5883`](https://github.com/nodejs/node/commit/ee81da5883)] - **tools**: update doc to remark-html\@15.0.2 to-vfile\@7.2.4 (Node.js GitHub Bot) [#46622](https://github.com/nodejs/node/pull/46622)
* \[[`92e3e0e4e4`](https://github.com/nodejs/node/commit/92e3e0e4e4)] - **tools**: update lint-md-dependencies to rollup\@3.13.0 vfile-reporter\@7.0.5 (Node.js GitHub Bot) [#46503](https://github.com/nodejs/node/pull/46503)
* \[[`728f96861e`](https://github.com/nodejs/node/commit/728f96861e)] - **tools**: update ESLint custom rules to not use the deprecated format (Antoine du Hamel) [#46460](https://github.com/nodejs/node/pull/46460)
* \[[`78b79c0220`](https://github.com/nodejs/node/commit/78b79c0220)] - **typings**: improve `primordials` typings (Antoine du Hamel) [#46970](https://github.com/nodejs/node/pull/46970)
* \[[`a39dd37653`](https://github.com/nodejs/node/commit/a39dd37653)] - **url**: simplify and improve url formatting (Yagiz Nizipli) [#46736](https://github.com/nodejs/node/pull/46736)
* \[[`01b6525706`](https://github.com/nodejs/node/commit/01b6525706)] - **url**: improve performance by removing host (Yagiz Nizipli) [#46547](https://github.com/nodejs/node/pull/46547)
* \[[`0c67a7ad57`](https://github.com/nodejs/node/commit/0c67a7ad57)] - **url**: fix url spec compliance issues (Yagiz Nizipli) [#46547](https://github.com/nodejs/node/pull/46547)
* \[[`e9263541a7`](https://github.com/nodejs/node/commit/e9263541a7)] - **url**: replace url-parser with ada (Yagiz Nizipli) [#46410](https://github.com/nodejs/node/pull/46410)
* \[[`4d0faf4ddc`](https://github.com/nodejs/node/commit/4d0faf4ddc)] - **url**: improve `isURLThis` detection (Yagiz Nizipli) [#46866](https://github.com/nodejs/node/pull/46866)
* \[[`1ece4a72b7`](https://github.com/nodejs/node/commit/1ece4a72b7)] - **(SEMVER-MINOR)** **url**: implement URLSearchParams size getter (James M Snell) [#46308](https://github.com/nodejs/node/pull/46308)
* \[[`8f14002459`](https://github.com/nodejs/node/commit/8f14002459)] - **url**: remove unused `URL::ToFilePath()` (Yagiz Nizipli) [#46487](https://github.com/nodejs/node/pull/46487)
* \[[`f57e7bcc11`](https://github.com/nodejs/node/commit/f57e7bcc11)] - **url**: remove unused `URL::toObject` (Yagiz Nizipli) [#46486](https://github.com/nodejs/node/pull/46486)
* \[[`04930562f8`](https://github.com/nodejs/node/commit/04930562f8)] - **url**: remove unused `setURLConstructor` function (Yagiz Nizipli) [#46485](https://github.com/nodejs/node/pull/46485)
* \[[`b801873a8a`](https://github.com/nodejs/node/commit/b801873a8a)] - **vm**: properly support symbols on globals (Nicolas DUBIEN) [#46458](https://github.com/nodejs/node/pull/46458)
* \[[`f7423bdbaa`](https://github.com/nodejs/node/commit/f7423bdbaa)] - **(SEMVER-MINOR)** **worker**: add support for worker name in inspector and trace\_events (Debadree Chatterjee) [#46832](https://github.com/nodejs/node/pull/46832)

<a id="18.15.0"></a>

## 2023-03-07, Version 18.15.0 'Hydrogen' (LTS), @BethGriggs prepared by @juanarbol

### Notable Changes

* \[[`63563f8a7a`](https://github.com/nodejs/node/commit/63563f8a7a)] - **doc,lib,src,test**: rename --test-coverage (Colin Ihrig) [#46017](https://github.com/nodejs/node/pull/46017)
* \[[`28a775b32f`](https://github.com/nodejs/node/commit/28a775b32f)] - **test\_runner**: add initial code coverage support (Colin Ihrig) [#46017](https://github.com/nodejs/node/pull/46017)
* \[[`4d50db14b3`](https://github.com/nodejs/node/commit/4d50db14b3)] - **(SEMVER-MINOR)** **test\_runner**: add reporters (Moshe Atlow) [#45712](https://github.com/nodejs/node/pull/45712)
* \[[`643545ab79`](https://github.com/nodejs/node/commit/643545ab79)] - **(SEMVER-MINOR)** **fs**: add statfs() functions (Colin Ihrig) [#46358](https://github.com/nodejs/node/pull/46358)
* \[[`110ead9abb`](https://github.com/nodejs/node/commit/110ead9abb)] - **(SEMVER-MINOR)** **vm**: expose cachedDataRejected for vm.compileFunction (Anna Henningsen) [#46320](https://github.com/nodejs/node/pull/46320)
* \[[`02632b42cf`](https://github.com/nodejs/node/commit/02632b42cf)] - **(SEMVER-MINOR)** **v8**: support gc profile (theanarkh) [#46255](https://github.com/nodejs/node/pull/46255)
* \[[`f09b838408`](https://github.com/nodejs/node/commit/f09b838408)] - **(SEMVER-MINOR)** **src,lib**: add constrainedMemory API for process (theanarkh) [#46218](https://github.com/nodejs/node/pull/46218)
* \[[`cb5bb12422`](https://github.com/nodejs/node/commit/cb5bb12422)] - **(SEMVER-MINOR)** **buffer**: add isAscii method (Yagiz Nizipli) [#46046](https://github.com/nodejs/node/pull/46046)

### Commits

* \[[`6f91c8e2ae`](https://github.com/nodejs/node/commit/6f91c8e2ae)] - **benchmark**: add trailing commas (Antoine du Hamel) [#46370](https://github.com/nodejs/node/pull/46370)
* \[[`d0b9be21eb`](https://github.com/nodejs/node/commit/d0b9be21eb)] - **benchmark**: remove buffer benchmarks redundancy (Brian White) [#45735](https://github.com/nodejs/node/pull/45735)
* \[[`6468f30d0d`](https://github.com/nodejs/node/commit/6468f30d0d)] - **benchmark**: introduce benchmark combination filtering (Brian White) [#45735](https://github.com/nodejs/node/pull/45735)
* \[[`cb5bb12422`](https://github.com/nodejs/node/commit/cb5bb12422)] - **(SEMVER-MINOR)** **buffer**: add isAscii method (Yagiz Nizipli) [#46046](https://github.com/nodejs/node/pull/46046)
* \[[`ec61bb04c0`](https://github.com/nodejs/node/commit/ec61bb04c0)] - **build**: export more OpenSSL symbols on Windows (Mohamed Akram) [#45486](https://github.com/nodejs/node/pull/45486)
* \[[`7bae4333ce`](https://github.com/nodejs/node/commit/7bae4333ce)] - **build**: fix MSVC 2022 Release compilation (Vladimir Morozov (REDMOND)) [#46228](https://github.com/nodejs/node/pull/46228)
* \[[`0f5f2d4470`](https://github.com/nodejs/node/commit/0f5f2d4470)] - **crypto**: include `hmac.h` in `crypto_util.h` (Adam Langley) [#46279](https://github.com/nodejs/node/pull/46279)
* \[[`91ece4161b`](https://github.com/nodejs/node/commit/91ece4161b)] - **crypto**: avoid hang when no algorithm available (Richard Lau) [#46237](https://github.com/nodejs/node/pull/46237)
* \[[`492fc95bdf`](https://github.com/nodejs/node/commit/492fc95bdf)] - **deps**: V8: cherry-pick 90be99fab31c (Michaël Zasso) [#46646](https://github.com/nodejs/node/pull/46646)
* \[[`732c77e3d9`](https://github.com/nodejs/node/commit/732c77e3d9)] - **deps**: update acorn to 8.8.2 (Node.js GitHub Bot) [#46363](https://github.com/nodejs/node/pull/46363)
* \[[`8582f99ffb`](https://github.com/nodejs/node/commit/8582f99ffb)] - **deps**: update to uvwasi 0.0.15 (Colin Ihrig) [#46253](https://github.com/nodejs/node/pull/46253)
* \[[`5453cd9940`](https://github.com/nodejs/node/commit/5453cd9940)] - **deps**: V8: cherry-pick bf0bd4868dde (Michaël Zasso) [#45908](https://github.com/nodejs/node/pull/45908)
* \[[`3ea53c5dc8`](https://github.com/nodejs/node/commit/3ea53c5dc8)] - **deps**: V8: cherry-pick c875e86df1d7 (sepehrst) [#46501](https://github.com/nodejs/node/pull/46501)
* \[[`c04808de4b`](https://github.com/nodejs/node/commit/c04808de4b)] - **doc**: correct the `sed` command for macOS in release process docs (Juan José) [#46397](https://github.com/nodejs/node/pull/46397)
* \[[`8113220690`](https://github.com/nodejs/node/commit/8113220690)] - **doc**: pass string to `textEncoder.encode` as input (Deokjin Kim) [#46421](https://github.com/nodejs/node/pull/46421)
* \[[`129dccf5d2`](https://github.com/nodejs/node/commit/129dccf5d2)] - **doc**: add tip for session.post function (theanarkh) [#46354](https://github.com/nodejs/node/pull/46354)
* \[[`919e581732`](https://github.com/nodejs/node/commit/919e581732)] - **doc**: add documentation for socket.destroySoon() (Luigi Pinca) [#46337](https://github.com/nodejs/node/pull/46337)
* \[[`fc15ac95a5`](https://github.com/nodejs/node/commit/fc15ac95a5)] - **doc**: fix commit message using test instead of deps (Tony Gorez) [#46313](https://github.com/nodejs/node/pull/46313)
* \[[`d153a93200`](https://github.com/nodejs/node/commit/d153a93200)] - **doc**: add v8 fast api contribution guidelines (Yagiz Nizipli) [#46199](https://github.com/nodejs/node/pull/46199)
* \[[`dbf082d082`](https://github.com/nodejs/node/commit/dbf082d082)] - **doc**: fix small typo error (0xflotus) [#46186](https://github.com/nodejs/node/pull/46186)
* \[[`94421b4cfe`](https://github.com/nodejs/node/commit/94421b4cfe)] - **doc**: mark some parameters as optional in webstreams (Deokjin Kim) [#46269](https://github.com/nodejs/node/pull/46269)
* \[[`5adb743511`](https://github.com/nodejs/node/commit/5adb743511)] - **doc**: update output of example in `events.getEventListeners` (Deokjin Kim) [#46268](https://github.com/nodejs/node/pull/46268)
* \[[`63563f8a7a`](https://github.com/nodejs/node/commit/63563f8a7a)] - **doc,lib,src,test**: rename --test-coverage (Colin Ihrig) [#46017](https://github.com/nodejs/node/pull/46017)
* \[[`4e88c7c813`](https://github.com/nodejs/node/commit/4e88c7c813)] - **esm**: delete preload mock test (Geoffrey Booth) [#46402](https://github.com/nodejs/node/pull/46402)
* \[[`643545ab79`](https://github.com/nodejs/node/commit/643545ab79)] - **(SEMVER-MINOR)** **fs**: add statfs() functions (Colin Ihrig) [#46358](https://github.com/nodejs/node/pull/46358)
* \[[`5019b5473f`](https://github.com/nodejs/node/commit/5019b5473f)] - **http**: res.setHeaders first implementation (Marco Ippolito) [#46109](https://github.com/nodejs/node/pull/46109)
* \[[`76622c4c60`](https://github.com/nodejs/node/commit/76622c4c60)] - **inspector**: allow opening inspector when `NODE_V8_COVERAGE` is set (Moshe Atlow) [#46113](https://github.com/nodejs/node/pull/46113)
* \[[`92f0747e03`](https://github.com/nodejs/node/commit/92f0747e03)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#46399](https://github.com/nodejs/node/pull/46399)
* \[[`795251bc6f`](https://github.com/nodejs/node/commit/795251bc6f)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#46303](https://github.com/nodejs/node/pull/46303)
* \[[`8865424c31`](https://github.com/nodejs/node/commit/8865424c31)] - **meta**: add .mailmap entry (Rich Trott) [#46303](https://github.com/nodejs/node/pull/46303)
* \[[`5ed679407b`](https://github.com/nodejs/node/commit/5ed679407b)] - **meta**: move evanlucas to emeritus (Evan Lucas) [#46274](https://github.com/nodejs/node/pull/46274)
* \[[`403df210ac`](https://github.com/nodejs/node/commit/403df210ac)] - **module**: move test reporter loading (Geoffrey Booth) [#45923](https://github.com/nodejs/node/pull/45923)
* \[[`2f7319e387`](https://github.com/nodejs/node/commit/2f7319e387)] - **readline**: fix detection of carriage return (Antoine du Hamel) [#46306](https://github.com/nodejs/node/pull/46306)
* \[[`73a8f46c4d`](https://github.com/nodejs/node/commit/73a8f46c4d)] - _**Revert**_ "**src**: let http2 streams end after session close" (Santiago Gimeno) [#46721](https://github.com/nodejs/node/pull/46721)
* \[[`30d783f91a`](https://github.com/nodejs/node/commit/30d783f91a)] - **src**: stop tracing agent before shutting down libuv (Santiago Gimeno) [#46380](https://github.com/nodejs/node/pull/46380)
* \[[`1508d90fda`](https://github.com/nodejs/node/commit/1508d90fda)] - **src**: get rid of fp arithmetic in ParseIPv4Host (Tobias Nießen) [#46326](https://github.com/nodejs/node/pull/46326)
* \[[`bdb793a082`](https://github.com/nodejs/node/commit/bdb793a082)] - **src**: use UNREACHABLE instead of CHECK(falsy) (Tobias Nießen) [#46317](https://github.com/nodejs/node/pull/46317)
* \[[`116a33649b`](https://github.com/nodejs/node/commit/116a33649b)] - **src**: add support for ETW stack walking (José Dapena Paz) [#46203](https://github.com/nodejs/node/pull/46203)
* \[[`b06298c98e`](https://github.com/nodejs/node/commit/b06298c98e)] - **src**: refactor EndsInANumber in node\_url.cc and adds IsIPv4NumberValid (Miguel Teixeira) [#46227](https://github.com/nodejs/node/pull/46227)
* \[[`26f41b041c`](https://github.com/nodejs/node/commit/26f41b041c)] - **src**: fix c++ exception on bad command line arg (Ben Noordhuis) [#46290](https://github.com/nodejs/node/pull/46290)
* \[[`14da89f41a`](https://github.com/nodejs/node/commit/14da89f41a)] - **src**: remove unreachable UNREACHABLE (Tobias Nießen) [#46281](https://github.com/nodejs/node/pull/46281)
* \[[`18c4dd004b`](https://github.com/nodejs/node/commit/18c4dd004b)] - **src**: replace custom ASCII validation with simdutf one (Anna Henningsen) [#46271](https://github.com/nodejs/node/pull/46271)
* \[[`cde375510f`](https://github.com/nodejs/node/commit/cde375510f)] - **src**: replace unreachable code with static\_assert (Tobias Nießen) [#46250](https://github.com/nodejs/node/pull/46250)
* \[[`f389b2f3fc`](https://github.com/nodejs/node/commit/f389b2f3fc)] - **src**: use explicit C++17 fallthrough (Tobias Nießen) [#46251](https://github.com/nodejs/node/pull/46251)
* \[[`8adaa1333c`](https://github.com/nodejs/node/commit/8adaa1333c)] - **src**: use CreateEnvironment instead of inlining its code where possible (Anna Henningsen) [#45886](https://github.com/nodejs/node/pull/45886)
* \[[`f09b838408`](https://github.com/nodejs/node/commit/f09b838408)] - **(SEMVER-MINOR)** **src,lib**: add constrainedMemory API for process (theanarkh) [#46218](https://github.com/nodejs/node/pull/46218)
* \[[`63e92eae63`](https://github.com/nodejs/node/commit/63e92eae63)] - **stream**: remove brandchecks from stream duplexify (Debadree Chatterjee) [#46315](https://github.com/nodejs/node/pull/46315)
* \[[`3acfe9bf92`](https://github.com/nodejs/node/commit/3acfe9bf92)] - **stream**: fix readable stream as async iterator function (Erick Wendel) [#46147](https://github.com/nodejs/node/pull/46147)
* \[[`de64315ccb`](https://github.com/nodejs/node/commit/de64315ccb)] - **test**: fix WPT title when no META title is present (Filip Skokan) [#46804](https://github.com/nodejs/node/pull/46804)
* \[[`162e3400ff`](https://github.com/nodejs/node/commit/162e3400ff)] - **test**: fix default WPT titles (Filip Skokan) [#46778](https://github.com/nodejs/node/pull/46778)
* \[[`5f422c4d70`](https://github.com/nodejs/node/commit/5f422c4d70)] - **test**: add WPTRunner support for variants and generating WPT reports (Filip Skokan) [#46498](https://github.com/nodejs/node/pull/46498)
* \[[`4f5aff2557`](https://github.com/nodejs/node/commit/4f5aff2557)] - **test**: fix tap parser fails if a test logs a number (Pulkit Gupta) [#46056](https://github.com/nodejs/node/pull/46056)
* \[[`32b020cf84`](https://github.com/nodejs/node/commit/32b020cf84)] - **test**: fix tap escaping with and without --test (Pulkit Gupta) [#46311](https://github.com/nodejs/node/pull/46311)
* \[[`f2bba1bcdb`](https://github.com/nodejs/node/commit/f2bba1bcdb)] - **test**: add trailing commas in `test/node-api` (Antoine du Hamel) [#46384](https://github.com/nodejs/node/pull/46384)
* \[[`f2ebe66fda`](https://github.com/nodejs/node/commit/f2ebe66fda)] - **test**: add trailing commas in `test/message` (Antoine du Hamel) [#46372](https://github.com/nodejs/node/pull/46372)
* \[[`ed564a9985`](https://github.com/nodejs/node/commit/ed564a9985)] - **test**: add trailing commas in `test/pseudo-tty` (Antoine du Hamel) [#46371](https://github.com/nodejs/node/pull/46371)
* \[[`e4437dd409`](https://github.com/nodejs/node/commit/e4437dd409)] - **test**: set common.bits to 64 for loong64 (Shi Pujin) [#45383](https://github.com/nodejs/node/pull/45383)
* \[[`9d40aef736`](https://github.com/nodejs/node/commit/9d40aef736)] - **test**: s390x zlib test case fixes (Adam Majer) [#46367](https://github.com/nodejs/node/pull/46367)
* \[[`ed3fb52716`](https://github.com/nodejs/node/commit/ed3fb52716)] - **test**: fix logInTimeout is not function (theanarkh) [#46348](https://github.com/nodejs/node/pull/46348)
* \[[`d05b0771be`](https://github.com/nodejs/node/commit/d05b0771be)] - **test**: avoid trying to call sysctl directly (Adam Majer) [#46366](https://github.com/nodejs/node/pull/46366)
* \[[`041aac3bbd`](https://github.com/nodejs/node/commit/041aac3bbd)] - **test**: avoid left behind child processes (Richard Lau) [#46276](https://github.com/nodejs/node/pull/46276)
* \[[`837ddcb322`](https://github.com/nodejs/node/commit/837ddcb322)] - **test**: add failing test for readline with carriage return (Alec Mev) [#46075](https://github.com/nodejs/node/pull/46075)
* \[[`75b8db41c6`](https://github.com/nodejs/node/commit/75b8db41c6)] - **test**: reduce `fs-write-optional-params` flakiness (LiviaMedeiros) [#46238](https://github.com/nodejs/node/pull/46238)
* \[[`c0d3fdaf63`](https://github.com/nodejs/node/commit/c0d3fdaf63)] - **test,crypto**: add CFRG curve vectors to wrap/unwrap tests (Filip Skokan) [#46406](https://github.com/nodejs/node/pull/46406)
* \[[`f328f7b15e`](https://github.com/nodejs/node/commit/f328f7b15e)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#46267](https://github.com/nodejs/node/pull/46267)
* \[[`1ef3c53e24`](https://github.com/nodejs/node/commit/1ef3c53e24)] - **test\_runner**: better handle async bootstrap errors (Colin Ihrig) [#46720](https://github.com/nodejs/node/pull/46720)
* \[[`0a690efb76`](https://github.com/nodejs/node/commit/0a690efb76)] - **test\_runner**: add `describe.only` and `it.only` shorthands (Richie McColl) [#46604](https://github.com/nodejs/node/pull/46604)
* \[[`28a1317efe`](https://github.com/nodejs/node/commit/28a1317efe)] - **test\_runner**: bootstrap reporters before running tests (Moshe Atlow) [#46737](https://github.com/nodejs/node/pull/46737)
* \[[`cd3aaa8fac`](https://github.com/nodejs/node/commit/cd3aaa8fac)] - **test\_runner**: emit test-only diagnostic warning (Richie McColl) [#46540](https://github.com/nodejs/node/pull/46540)
* \[[`c19fa45a65`](https://github.com/nodejs/node/commit/c19fa45a65)] - **test\_runner**: centralize CLI option handling (Colin Ihrig) [#46707](https://github.com/nodejs/node/pull/46707)
* \[[`0898145e37`](https://github.com/nodejs/node/commit/0898145e37)] - **test\_runner**: display skipped tests in spec reporter output (Richie McColl) [#46651](https://github.com/nodejs/node/pull/46651)
* \[[`894d7117fa`](https://github.com/nodejs/node/commit/894d7117fa)] - **test\_runner**: parse non-ascii character correctly (Mert Can Altın) [#45736](https://github.com/nodejs/node/pull/45736)
* \[[`5b3c606626`](https://github.com/nodejs/node/commit/5b3c606626)] - **test\_runner**: flatten TAP output when running using `--test` (Moshe Atlow) [#46440](https://github.com/nodejs/node/pull/46440)
* \[[`391ff0dba4`](https://github.com/nodejs/node/commit/391ff0dba4)] - **test\_runner**: allow nesting test within describe (Moshe Atlow) [#46544](https://github.com/nodejs/node/pull/46544)
* \[[`ba784e87b4`](https://github.com/nodejs/node/commit/ba784e87b4)] - **test\_runner**: fix missing test diagnostics (Moshe Atlow) [#46450](https://github.com/nodejs/node/pull/46450)
* \[[`c5f16fb5fb`](https://github.com/nodejs/node/commit/c5f16fb5fb)] - **test\_runner**: top-level diagnostics not ommited when running with --test (Pulkit Gupta) [#46441](https://github.com/nodejs/node/pull/46441)
* \[[`28a775b32f`](https://github.com/nodejs/node/commit/28a775b32f)] - **test\_runner**: add initial code coverage support (Colin Ihrig) [#46017](https://github.com/nodejs/node/pull/46017)
* \[[`0d999e373a`](https://github.com/nodejs/node/commit/0d999e373a)] - **test\_runner**: make built in reporters internal (Colin Ihrig) [#46092](https://github.com/nodejs/node/pull/46092)
* \[[`79f4b426fe`](https://github.com/nodejs/node/commit/79f4b426fe)] - **test\_runner**: report `file` in test runner events (Moshe Atlow) [#46030](https://github.com/nodejs/node/pull/46030)
* \[[`4d50db14b3`](https://github.com/nodejs/node/commit/4d50db14b3)] - **(SEMVER-MINOR)** **test\_runner**: add reporters (Moshe Atlow) [#45712](https://github.com/nodejs/node/pull/45712)
* \[[`5fdf374c74`](https://github.com/nodejs/node/commit/5fdf374c74)] - **test\_runner**: avoid swallowing of asynchronously thrown errors (MURAKAMI Masahiko) [#45264](https://github.com/nodejs/node/pull/45264)
* \[[`23b875806c`](https://github.com/nodejs/node/commit/23b875806c)] - **test\_runner**: update comment to comply with eslint no-fallthrough rule (Antoine du Hamel) [#46258](https://github.com/nodejs/node/pull/46258)
* \[[`00c5495aa3`](https://github.com/nodejs/node/commit/00c5495aa3)] - **tools**: update eslint to 8.33.0 (Node.js GitHub Bot) [#46400](https://github.com/nodejs/node/pull/46400)
* \[[`37a6ce1120`](https://github.com/nodejs/node/commit/37a6ce1120)] - **tools**: update doc to unist-util-select\@4.0.3 unist-util-visit\@4.1.2 (Node.js GitHub Bot) [#46364](https://github.com/nodejs/node/pull/46364)
* \[[`1eaafc7db4`](https://github.com/nodejs/node/commit/1eaafc7db4)] - **tools**: update lint-md-dependencies to rollup\@3.12.0 (Node.js GitHub Bot) [#46398](https://github.com/nodejs/node/pull/46398)
* \[[`a97774603b`](https://github.com/nodejs/node/commit/a97774603b)] - **tools**: require more trailing commas (Antoine du Hamel) [#46346](https://github.com/nodejs/node/pull/46346)
* \[[`03e244a59b`](https://github.com/nodejs/node/commit/03e244a59b)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#46302](https://github.com/nodejs/node/pull/46302)
* \[[`60d714e0c3`](https://github.com/nodejs/node/commit/60d714e0c3)] - **tools**: allow icutrim.py to run on python2 (Michael Dawson) [#46263](https://github.com/nodejs/node/pull/46263)
* \[[`b7950f50de`](https://github.com/nodejs/node/commit/b7950f50de)] - **tools**: update eslint to 8.32.0 (Node.js GitHub Bot) [#46258](https://github.com/nodejs/node/pull/46258)
* \[[`08bafc84f6`](https://github.com/nodejs/node/commit/08bafc84f6)] - **url**: refactor to use more primordials (Antoine du Hamel) [#45966](https://github.com/nodejs/node/pull/45966)
* \[[`02632b42cf`](https://github.com/nodejs/node/commit/02632b42cf)] - **(SEMVER-MINOR)** **v8**: support gc profile (theanarkh) [#46255](https://github.com/nodejs/node/pull/46255)
* \[[`110ead9abb`](https://github.com/nodejs/node/commit/110ead9abb)] - **(SEMVER-MINOR)** **vm**: expose cachedDataRejected for vm.compileFunction (Anna Henningsen) [#46320](https://github.com/nodejs/node/pull/46320)

<a id="18.14.2"></a>

## 2023-02-21, Version 18.14.2 'Hydrogen' (LTS), @MylesBorins

### Notable Changes

* \[[`f864bef32a`](https://github.com/nodejs/node/commit/f864bef32a)] - **deps**: upgrade npm to 9.5.0 (npm team) [#46673](https://github.com/nodejs/node/pull/46673)

### Commits

* \[[`880a65d7ff`](https://github.com/nodejs/node/commit/880a65d7ff)] - **build**: delete `snapshot.blob` file from the project (Juan José Arboleda) [#46626](https://github.com/nodejs/node/pull/46626)
* \[[`cbea56efda`](https://github.com/nodejs/node/commit/cbea56efda)] - **deps**: update undici to 5.20.0 (Node.js GitHub Bot) [#46711](https://github.com/nodejs/node/pull/46711)
* \[[`f864bef32a`](https://github.com/nodejs/node/commit/f864bef32a)] - **deps**: upgrade npm to 9.5.0 (npm team) [#46673](https://github.com/nodejs/node/pull/46673)
* \[[`648041d568`](https://github.com/nodejs/node/commit/648041d568)] - **deps**: upgrade npm to 9.4.0 (npm team) [#46353](https://github.com/nodejs/node/pull/46353)
* \[[`5e1f213f3c`](https://github.com/nodejs/node/commit/5e1f213f3c)] - **deps**: patch V8 to 10.2.154.26 (Michaël Zasso) [#46446](https://github.com/nodejs/node/pull/46446)

<a id="18.14.1"></a>

## 2023-02-16, Version 18.14.1 'Hydrogen' (LTS), @RafaelGSS prepared by @juanarbol

This is a security release.

### Notable Changes

The following CVEs are fixed in this release:

* **[CVE-2023-23918](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-23918)**: Node.js Permissions policies can be bypassed via process.mainModule (High)
* **[CVE-2023-23919](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-23919)**: Node.js OpenSSL error handling issues in nodejs crypto library (Medium)
* **[CVE-2023-23936](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-23936)**: Fetch API in Node.js did not protect against CRLF injection in host headers (Medium)
* **[CVE-2023-24807](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-24807)**: Regular Expression Denial of Service in Headers in Node.js fetch API (Low)
* **[CVE-2023-23920](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-23920)**: Node.js insecure loading of ICU data through ICU\_DATA environment variable (Low)

More detailed information on each of the vulnerabilities can be found in [February 2023 Security Releases](https://nodejs.org/en/blog/vulnerability/february-2023-security-releases/) blog post.

This security release includes OpenSSL security updates as outlined in the recent
[OpenSSL security advisory](https://www.openssl.org/news/secadv/20230207.txt).

### Commits

* \[[`8393ebc72d`](https://github.com/nodejs/node/commit/8393ebc72d)] - **build**: build ICU with ICU\_NO\_USER\_DATA\_OVERRIDE (RafaelGSS) [nodejs-private/node-private#379](https://github.com/nodejs-private/node-private/pull/379)
* \[[`004e34d046`](https://github.com/nodejs/node/commit/004e34d046)] - **crypto**: clear OpenSSL error on invalid ca cert (RafaelGSS) [#46572](https://github.com/nodejs/node/pull/46572)
* \[[`5e0142a852`](https://github.com/nodejs/node/commit/5e0142a852)] - **deps**: cherry-pick Windows ARM64 fix for openssl (Richard Lau) [#46572](https://github.com/nodejs/node/pull/46572)
* \[[`f71fe278a6`](https://github.com/nodejs/node/commit/f71fe278a6)] - **deps**: update archs files for quictls/openssl-3.0.8+quic (RafaelGSS) [#46572](https://github.com/nodejs/node/pull/46572)
* \[[`2c6817e42b`](https://github.com/nodejs/node/commit/2c6817e42b)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.8+quic (RafaelGSS) [#46572](https://github.com/nodejs/node/pull/46572)
* \[[`f0afa0bfe5`](https://github.com/nodejs/node/commit/f0afa0bfe5)] - **deps**: update undici to 5.19.1 (Node.js GitHub Bot) [#46634](https://github.com/nodejs/node/pull/46634)
* \[[`c26a34c13e`](https://github.com/nodejs/node/commit/c26a34c13e)] - **deps**: update undici to 5.18.0 (Node.js GitHub Bot) [#46634](https://github.com/nodejs/node/pull/46634)
* \[[`db93ee4a15`](https://github.com/nodejs/node/commit/db93ee4a15)] - **deps**: update undici to 5.17.1 (Node.js GitHub Bot) [#46634](https://github.com/nodejs/node/pull/46634)
* \[[`b4e49fb02c`](https://github.com/nodejs/node/commit/b4e49fb02c)] - **deps**: update undici to 5.16.0 (Node.js GitHub Bot) [#46634](https://github.com/nodejs/node/pull/46634)
* \[[`90994e6a2c`](https://github.com/nodejs/node/commit/90994e6a2c)] - **deps**: update undici to 5.15.1 (Node.js GitHub Bot) [#46634](https://github.com/nodejs/node/pull/46634)
* \[[`00302fc7ac`](https://github.com/nodejs/node/commit/00302fc7ac)] - **deps**: update undici to 5.15.0 (Node.js GitHub Bot) [#46634](https://github.com/nodejs/node/pull/46634)
* \[[`0e3b796cc5`](https://github.com/nodejs/node/commit/0e3b796cc5)] - **lib**: makeRequireFunction patch when experimental policy (RafaelGSS) [nodejs-private/node-private#371](https://github.com/nodejs-private/node-private/pull/371)
* \[[`7cccd5565f`](https://github.com/nodejs/node/commit/7cccd5565f)] - **policy**: makeRequireFunction on mainModule.require (RafaelGSS) [nodejs-private/node-private#371](https://github.com/nodejs-private/node-private/pull/371)

<a id="18.14.0"></a>

## 2023-02-02, Version 18.14.0 'Hydrogen' (LTS), @BethGriggs prepared by @juanarbol

### Notable changes

#### Updated npm to 9.3.1

Based on the [list of guidelines we've established on integrating `npm` and `node`](https://github.com/npm/cli/wiki/Integrating-with-node),
here is a grouped list of the breaking changes with the reasoning as to why they
fit within the guidelines linked above. Note that all the breaking changes were
made in [9.0.0](https://github.com/npm/cli/releases/tag/v9.0.0).
All subsequent minor and patch releases after `npm@9.0.0` do not contain any
breaking changes.

##### Engines

> Explanation: the node engines supported by `npm@9` make it safe to allow `npm@9` as the default in any LTS version of `14` or `16`, as well as anything later than or including `18.0.0`

* `npm` is now compatible with the following semver range for node: `^14.17.0 || ^16.13.0 || >=18.0.0`

##### Filesystem

> Explanation: when run as root previous versions of npm attempted to manage file ownership automatically on the user's behalf. this behavior was problematic in many cases and has been removed in favor of allowing users to manage their own filesystem permissions

* `npm` will no longer attempt to modify ownership of files it creates.

##### Auth

> Explanation: any errors thrown from users having unsupported auth configurations will show `npm config fix` in the remediation instructions, which will allow the user to automatically have their auth config fixed.

* The presence of auth related settings that are not scoped to a specific
  registry found in a config file is no longer supported and will throw errors.

##### Login

> Explanation: the default `auth-type` has changed and users can opt back into the old behavior with `npm config set auth-type=legacy`. `login` and `adduser` have also been seperated making each command more closely match it's name instead of being aliases for each other.

* Legacy auth types `sso`, `saml` & `legacy` have been consolidated into `"legacy"`.
* `auth-type` defaults to `"web"`
* `login` and `adduser` are now separate commands that send different data to the registry.
* `auth-type` config values `web` and `legacy` only try their respective methods,
  npm no longer tries them all and waits to see which one doesn't fail.

##### Tarball Packing

> Explanation: previously using multiple ignore/allow lists when packing was an undefined behavior, and now the order of operations is strictly defined when packing a tarball making it easier to follow and should only affect users relying on the previously undefined behavior.

* `npm pack` now follows a strict order of operations when applying ignore rules.
  If a `files` array is present in the `package.json`, then rules in `.gitignore`
  and `.npmignore` files from the root will be ignored.

##### Display/Debug/Timing Info

> Explanation: these changes center around the display of information to the terminal including timing and debug log info. We do not anticipate these changes breaking any existing workflows.

* Links generated from git urls will now use `HEAD` instead of `master` as the default ref.
* `timing` has been removed as a value for `--loglevel`.
* `--timing` will show timing information regardless of `--loglevel`, except when `--silent`.
* When run with the `--timing` flag, `npm` now writes timing data to a file
  alongside the debug log data, respecting the `logs-dir` option and falling
  back to `<CACHE>/_logs/` dir, instead of directly inside the cache directory.
* The timing file data is no longer newline delimited JSON, and instead each run
  will create a uniquely named `<ID>-timing.json` file, with the `<ID>` portion
  being the same as the debug log.
* `npm` now outputs some json errors on stdout. Previously `npm` would output
  all json formatted errors on stderr, making it difficult to parse as the
  stderr stream usually has logs already written to it.

##### Config/Command Deprecations or Removals

> Explanation: `install-links` is the only config or command in the list that has an effect on package installs. We fixed a number of issues that came up during prereleases with this change. It will also only be applied to new package trees created without a package-lock.json file. Any install with an existing lock file will not be changed.

* Deprecate boolean install flags in favor of `--install-strategy`.
* `npm config set` will no longer accept deprecated or invalid config options.
* `install-links` config defaults to `"true"`.
* `node-version` config has been removed.
* `npm-version` config has been removed.
* `npm access` subcommands have been renamed.
* `npm birthday` has been removed.
* `npm set-script` has been removed.
* `npm bin` has been removed (use `npx` or `npm exec` to execute binaries).

#### Other notable changes

* **doc**:
  * add parallelism note to os.cpus() (Colin Ihrig) [#45895](https://github.com/nodejs/node/pull/45895)
* **http**:
  * join authorization headers (Marco Ippolito) [#45982](https://github.com/nodejs/node/pull/45982)
  * improved timeout defaults handling (Paolo Insogna) [#45778](https://github.com/nodejs/node/pull/45778)
* **stream**:
  * implement finished() for ReadableStream and WritableStream (Debadree Chatterjee) [#46205](https://github.com/nodejs/node/pull/46205)

### Commits

* \[[`1352f08778`](https://github.com/nodejs/node/commit/1352f08778)] - **assert**: remove `assert.snapshot` (Moshe Atlow) [#46112](https://github.com/nodejs/node/pull/46112)
* \[[`4ee3238643`](https://github.com/nodejs/node/commit/4ee3238643)] - **async\_hooks**: refactor to use `validateObject` (Deokjin Kim) [#46004](https://github.com/nodejs/node/pull/46004)
* \[[`79e0bf9b64`](https://github.com/nodejs/node/commit/79e0bf9b64)] - **benchmark**: include webstreams benchmark (Rafael Gonzaga) [#45876](https://github.com/nodejs/node/pull/45876)
* \[[`ed1ac82469`](https://github.com/nodejs/node/commit/ed1ac82469)] - **benchmark,tools**: use os.availableParallelism() (Deokjin Kim) [#46003](https://github.com/nodejs/node/pull/46003)
* \[[`16ee02f2eb`](https://github.com/nodejs/node/commit/16ee02f2eb)] - **(SEMVER-MINOR)** **buffer**: add buffer.isUtf8 for utf8 validation (Yagiz Nizipli) [#45947](https://github.com/nodejs/node/pull/45947)
* \[[`3bf2371a57`](https://github.com/nodejs/node/commit/3bf2371a57)] - **build**: add extra semi check (Jiawen Geng) [#46194](https://github.com/nodejs/node/pull/46194)
* \[[`560ee24157`](https://github.com/nodejs/node/commit/560ee24157)] - **build**: fix arm64 cross-compile from powershell (Stefan Stojanovic) [#45890](https://github.com/nodejs/node/pull/45890)
* \[[`48e3ad3aca`](https://github.com/nodejs/node/commit/48e3ad3aca)] - **build**: add option to disable shared readonly heap (Anna Henningsen) [#45887](https://github.com/nodejs/node/pull/45887)
* \[[`52a7887b94`](https://github.com/nodejs/node/commit/52a7887b94)] - **(SEMVER-MINOR)** **crypto**: add CryptoKey Symbol.toStringTag (Filip Skokan) [#46042](https://github.com/nodejs/node/pull/46042)
* \[[`a558774a40`](https://github.com/nodejs/node/commit/a558774a40)] - **crypto**: add cipher update/final methods encoding validation (vitpavlenko) [#45990](https://github.com/nodejs/node/pull/45990)
* \[[`599d1dc841`](https://github.com/nodejs/node/commit/599d1dc841)] - **crypto**: ensure auth tag set for chacha20-poly1305 (Ben Noordhuis) [#46185](https://github.com/nodejs/node/pull/46185)
* \[[`24a101698c`](https://github.com/nodejs/node/commit/24a101698c)] - **crypto**: return correct bit length in KeyObject's asymmetricKeyDetails (Filip Skokan) [#46106](https://github.com/nodejs/node/pull/46106)
* \[[`2de50fef84`](https://github.com/nodejs/node/commit/2de50fef84)] - **(SEMVER-MINOR)** **crypto**: add KeyObject Symbol.toStringTag (Filip Skokan) [#46043](https://github.com/nodejs/node/pull/46043)
* \[[`782b6f6f9f`](https://github.com/nodejs/node/commit/782b6f6f9f)] - **crypto**: ensure exported webcrypto EC keys use uncompressed point format (Ben Noordhuis) [#46021](https://github.com/nodejs/node/pull/46021)
* \[[`7a97f3f43b`](https://github.com/nodejs/node/commit/7a97f3f43b)] - **crypto**: fix CryptoKey prototype WPT (Filip Skokan) [#45857](https://github.com/nodejs/node/pull/45857)
* \[[`1a8aa50aa2`](https://github.com/nodejs/node/commit/1a8aa50aa2)] - **crypto**: fix CryptoKey WebIDL conformance (Filip Skokan) [#45855](https://github.com/nodejs/node/pull/45855)
* \[[`c6436450ee`](https://github.com/nodejs/node/commit/c6436450ee)] - **crypto**: fix error when getRandomValues is called without arguments (Filip Skokan) [#45854](https://github.com/nodejs/node/pull/45854)
* \[[`4cdf0002c5`](https://github.com/nodejs/node/commit/4cdf0002c5)] - **debugger**: refactor console in lib/internal/debugger/inspect.js (Debadree Chatterjee) [#45847](https://github.com/nodejs/node/pull/45847)
* \[[`b7fe8c70fa`](https://github.com/nodejs/node/commit/b7fe8c70fa)] - **deps**: update simdutf to 3.1.0 (Node.js GitHub Bot) [#46257](https://github.com/nodejs/node/pull/46257)
* \[[`eaeb870cd7`](https://github.com/nodejs/node/commit/eaeb870cd7)] - **deps**: upgrade npm to 9.3.1 (npm team) [#46242](https://github.com/nodejs/node/pull/46242)
* \[[`7c03a3d676`](https://github.com/nodejs/node/commit/7c03a3d676)] - **deps**: upgrade npm to 9.3.0 (npm team) [#46193](https://github.com/nodejs/node/pull/46193)
* \[[`340d76accb`](https://github.com/nodejs/node/commit/340d76accb)] - **deps**: cherrypick simdutf patch (Jiawen Geng) [#46194](https://github.com/nodejs/node/pull/46194)
* \[[`cce2af4306`](https://github.com/nodejs/node/commit/cce2af4306)] - **deps**: bump googletest to 2023.01.13 (Jiawen Geng) [#46198](https://github.com/nodejs/node/pull/46198)
* \[[`d251a66bed`](https://github.com/nodejs/node/commit/d251a66bed)] - **deps**: add /deps/\*\*/.github/ to .gitignore (Luigi Pinca) [#46091](https://github.com/nodejs/node/pull/46091)
* \[[`874054f469`](https://github.com/nodejs/node/commit/874054f469)] - **deps**: add simdutf version to metadata (Mike Roth) [#46145](https://github.com/nodejs/node/pull/46145)
* \[[`2497702b82`](https://github.com/nodejs/node/commit/2497702b82)] - **deps**: update simdutf to 2.1.0 (Node.js GitHub Bot) [#46128](https://github.com/nodejs/node/pull/46128)
* \[[`c8492b7f4c`](https://github.com/nodejs/node/commit/c8492b7f4c)] - **deps**: update corepack to 0.15.3 (Node.js GitHub Bot) [#46037](https://github.com/nodejs/node/pull/46037)
* \[[`d148f357fd`](https://github.com/nodejs/node/commit/d148f357fd)] - **deps**: update simdutf to 2.0.9 (Node.js GitHub Bot) [#45975](https://github.com/nodejs/node/pull/45975)
* \[[`422a98199c`](https://github.com/nodejs/node/commit/422a98199c)] - **deps**: update to uvwasi 0.0.14 (Colin Ihrig) [#45970](https://github.com/nodejs/node/pull/45970)
* \[[`7812752db0`](https://github.com/nodejs/node/commit/7812752db0)] - **deps**: fix updater github workflow job (Yagiz Nizipli) [#45972](https://github.com/nodejs/node/pull/45972)
* \[[`4063cdcef6`](https://github.com/nodejs/node/commit/4063cdcef6)] - _**Revert**_ "**deps**: disable avx512 for simutf on benchmark ci" (Yagiz Nizipli) [#45948](https://github.com/nodejs/node/pull/45948)
* \[[`64d3e3f3ba`](https://github.com/nodejs/node/commit/64d3e3f3ba)] - **deps**: disable avx512 for simutf on benchmark ci (Yagiz Nizipli) [#45803](https://github.com/nodejs/node/pull/45803)
* \[[`c9845fc334`](https://github.com/nodejs/node/commit/c9845fc334)] - **deps**: add simdutf dependency (Yagiz Nizipli) [#45803](https://github.com/nodejs/node/pull/45803)
* \[[`6963c96547`](https://github.com/nodejs/node/commit/6963c96547)] - **deps**: update timezone to 2022g (Node.js GitHub Bot) [#45731](https://github.com/nodejs/node/pull/45731)
* \[[`874f6c340b`](https://github.com/nodejs/node/commit/874f6c340b)] - **deps**: update undici to 5.14.0 (Node.js GitHub Bot) [#45812](https://github.com/nodejs/node/pull/45812)
* \[[`7599b913d5`](https://github.com/nodejs/node/commit/7599b913d5)] - **deps**: upgrade npm to 9.2.0 (npm team) [#45780](https://github.com/nodejs/node/pull/45780)
* \[[`4844935ff3`](https://github.com/nodejs/node/commit/4844935ff3)] - **deps**: upgrade npm to 9.1.3 (npm team) [#45693](https://github.com/nodejs/node/pull/45693)
* \[[`8dce62c7fe`](https://github.com/nodejs/node/commit/8dce62c7fe)] - **deps**: V8: cherry-pick 5fe919f78321 (Richard Lau) [#45587](https://github.com/nodejs/node/pull/45587)
* \[[`8de642517e`](https://github.com/nodejs/node/commit/8de642517e)] - **dgram**: sync the old handle state to new handle (theanarkh) [#46041](https://github.com/nodejs/node/pull/46041)
* \[[`de2b7a9640`](https://github.com/nodejs/node/commit/de2b7a9640)] - **doc**: fix mismatched arguments of `NodeEventTarget` (Deokjin Kim) [#45678](https://github.com/nodejs/node/pull/45678)
* \[[`6317502d10`](https://github.com/nodejs/node/commit/6317502d10)] - **doc**: update events API example to have runnable code (Deokjin Kim) [#45760](https://github.com/nodejs/node/pull/45760)
* \[[`a9db45eee1`](https://github.com/nodejs/node/commit/a9db45eee1)] - **doc**: add note to tls docs about secureContext availability (Tim Gerk) [#46224](https://github.com/nodejs/node/pull/46224)
* \[[`5294371063`](https://github.com/nodejs/node/commit/5294371063)] - **doc**: add text around collaborative expectations (Michael Dawson) [#46121](https://github.com/nodejs/node/pull/46121)
* \[[`be85d5a6eb`](https://github.com/nodejs/node/commit/be85d5a6eb)] - **doc**: update to match changed `--dns-result-order` default (Mordy Tikotzky) [#46148](https://github.com/nodejs/node/pull/46148)
* \[[`4f2d9ea6da`](https://github.com/nodejs/node/commit/4f2d9ea6da)] - **doc**: add Node-API media link (Kevin Eady) [#46189](https://github.com/nodejs/node/pull/46189)
* \[[`9bfd40466f`](https://github.com/nodejs/node/commit/9bfd40466f)] - **doc**: update http.setMaxIdleHTTPParsers arguments (Debadree Chatterjee) [#46168](https://github.com/nodejs/node/pull/46168)
* \[[`d7a8c076e1`](https://github.com/nodejs/node/commit/d7a8c076e1)] - **doc**: use "file system" instead of "filesystem" (Rich Trott) [#46178](https://github.com/nodejs/node/pull/46178)
* \[[`e54483cd2b`](https://github.com/nodejs/node/commit/e54483cd2b)] - **doc**: https update default request timeout (Marco Ippolito) [#46184](https://github.com/nodejs/node/pull/46184)
* \[[`335110b0fb`](https://github.com/nodejs/node/commit/335110b0fb)] - **doc**: make options of readableStream.pipeTo as optional (Deokjin Kim) [#46180](https://github.com/nodejs/node/pull/46180)
* \[[`ec34cad712`](https://github.com/nodejs/node/commit/ec34cad712)] - **doc**: add PerformanceObserver.supportedEntryTypes to doc (theanarkh) [#45962](https://github.com/nodejs/node/pull/45962)
* \[[`d0f905bd6f`](https://github.com/nodejs/node/commit/d0f905bd6f)] - **doc**: duplex and readable from uncaught execption warning (Marco Ippolito) [#46135](https://github.com/nodejs/node/pull/46135)
* \[[`512feaafa4`](https://github.com/nodejs/node/commit/512feaafa4)] - **doc**: remove outdated sections from `maintaining-v8` (Antoine du Hamel) [#46137](https://github.com/nodejs/node/pull/46137)
* \[[`849a3e2ce7`](https://github.com/nodejs/node/commit/849a3e2ce7)] - **doc**: fix (EC)DHE remark in TLS docs (Tobias Nießen) [#46114](https://github.com/nodejs/node/pull/46114)
* \[[`a3c9c1b4e6`](https://github.com/nodejs/node/commit/a3c9c1b4e6)] - **doc**: fix ERR\_TLS\_RENEGOTIATION\_DISABLED text (Tobias Nießen) [#46122](https://github.com/nodejs/node/pull/46122)
* \[[`1834e94ebb`](https://github.com/nodejs/node/commit/1834e94ebb)] - **doc**: fix spelling in SECURITY.md (Vaishno Chaitanya) [#46124](https://github.com/nodejs/node/pull/46124)
* \[[`3968698af5`](https://github.com/nodejs/node/commit/3968698af5)] - **doc**: abort controller emits error in child process (Debadree Chatterjee) [#46072](https://github.com/nodejs/node/pull/46072)
* \[[`1ec14c2c61`](https://github.com/nodejs/node/commit/1ec14c2c61)] - **doc**: fix `event.cancelBubble` documentation (Deokjin Kim) [#45986](https://github.com/nodejs/node/pull/45986)
* \[[`5539977f80`](https://github.com/nodejs/node/commit/5539977f80)] - **doc**: add personal pronouns option (Filip Skokan) [#46118](https://github.com/nodejs/node/pull/46118)
* \[[`1fabef3a81`](https://github.com/nodejs/node/commit/1fabef3a81)] - **doc**: mention how to run ncu-ci citgm (Rafael Gonzaga) [#46090](https://github.com/nodejs/node/pull/46090)
* \[[`84dc65ab87`](https://github.com/nodejs/node/commit/84dc65ab87)] - **doc**: include updating release optional step (Rafael Gonzaga) [#46089](https://github.com/nodejs/node/pull/46089)
* \[[`76c7ea1e74`](https://github.com/nodejs/node/commit/76c7ea1e74)] - **doc**: describe argument of `Symbol.for` (Deokjin Kim) [#46019](https://github.com/nodejs/node/pull/46019)
* \[[`2307a74990`](https://github.com/nodejs/node/commit/2307a74990)] - **doc**: update isUtf8 description (Yagiz Nizipli) [#45973](https://github.com/nodejs/node/pull/45973)
* \[[`fa5b65ea24`](https://github.com/nodejs/node/commit/fa5b65ea24)] - **doc**: use console.error for error case in timers and tls (Deokjin Kim) [#46002](https://github.com/nodejs/node/pull/46002)
* \[[`29d509c100`](https://github.com/nodejs/node/commit/29d509c100)] - **doc**: fix wrong output of example in `url.protocol` (Deokjin Kim) [#45954](https://github.com/nodejs/node/pull/45954)
* \[[`61dbca2690`](https://github.com/nodejs/node/commit/61dbca2690)] - **doc**: use `os.availableParallelism()` in async\_context and cluster (Deokjin Kim) [#45979](https://github.com/nodejs/node/pull/45979)
* \[[`86b2c8cea2`](https://github.com/nodejs/node/commit/86b2c8cea2)] - **doc**: make EventEmitterAsyncResource's `options` as optional (Deokjin Kim) [#45985](https://github.com/nodejs/node/pull/45985)
* \[[`335acf7748`](https://github.com/nodejs/node/commit/335acf7748)] - **doc**: replace single executable champion in strategic initiatives doc (Darshan Sen) [#45956](https://github.com/nodejs/node/pull/45956)
* \[[`aab35a9388`](https://github.com/nodejs/node/commit/aab35a9388)] - **doc**: update error message of example in repl (Deokjin Kim) [#45920](https://github.com/nodejs/node/pull/45920)
* \[[`53a94a95ff`](https://github.com/nodejs/node/commit/53a94a95ff)] - **doc**: fix typos in packages.md (Eric Mutta) [#45957](https://github.com/nodejs/node/pull/45957)
* \[[`83875f46cf`](https://github.com/nodejs/node/commit/83875f46cf)] - **doc**: remove port from example in `url.hostname` (Deokjin Kim) [#45927](https://github.com/nodejs/node/pull/45927)
* \[[`162d3a94e3`](https://github.com/nodejs/node/commit/162d3a94e3)] - **doc**: show output of example in http (Deokjin Kim) [#45915](https://github.com/nodejs/node/pull/45915)
* \[[`53684e4506`](https://github.com/nodejs/node/commit/53684e4506)] - **(SEMVER-MINOR)** **doc**: add parallelism note to os.cpus() (Colin Ihrig) [#45895](https://github.com/nodejs/node/pull/45895)
* \[[`546e083d36`](https://github.com/nodejs/node/commit/546e083d36)] - **doc**: fix wrong output of example in `url.password` (Deokjin Kim) [#45928](https://github.com/nodejs/node/pull/45928)
* \[[`14c95ecd23`](https://github.com/nodejs/node/commit/14c95ecd23)] - **doc**: fix some history entries in `deprecations.md` (Antoine du Hamel) [#45891](https://github.com/nodejs/node/pull/45891)
* \[[`d94dba973b`](https://github.com/nodejs/node/commit/d94dba973b)] - **doc**: add tip for NODE\_MODULE (theanarkh) [#45797](https://github.com/nodejs/node/pull/45797)
* \[[`662f574c5b`](https://github.com/nodejs/node/commit/662f574c5b)] - **doc**: reduce likelihood of mismerges during release (Richard Lau) [#45864](https://github.com/nodejs/node/pull/45864)
* \[[`48ea28aa30`](https://github.com/nodejs/node/commit/48ea28aa30)] - **doc**: add backticks to webcrypto rsaOaepParams (Filip Skokan) [#45883](https://github.com/nodejs/node/pull/45883)
* \[[`726b285163`](https://github.com/nodejs/node/commit/726b285163)] - **doc**: remove release cleanup step (Michaël Zasso) [#45858](https://github.com/nodejs/node/pull/45858)
* \[[`5eb93f1de9`](https://github.com/nodejs/node/commit/5eb93f1de9)] - **doc**: add stream/promises pipeline and finished to doc (Marco Ippolito) [#45832](https://github.com/nodejs/node/pull/45832)
* \[[`f874d0ba74`](https://github.com/nodejs/node/commit/f874d0ba74)] - **doc**: remove Juan Jose keys (Rafael Gonzaga) [#45827](https://github.com/nodejs/node/pull/45827)
* \[[`67efe2a55e`](https://github.com/nodejs/node/commit/67efe2a55e)] - **doc**: fix wrong output of example in util (Deokjin Kim) [#45825](https://github.com/nodejs/node/pull/45825)
* \[[`b709af31e0`](https://github.com/nodejs/node/commit/b709af31e0)] - **doc**: sort http.createServer() options alphabetically (Luigi Pinca) [#45680](https://github.com/nodejs/node/pull/45680)
* \[[`ebe292113a`](https://github.com/nodejs/node/commit/ebe292113a)] - **doc,crypto**: fix WebCryptoAPI import keyData and export return (Filip Skokan) [#46076](https://github.com/nodejs/node/pull/46076)
* \[[`204757719c`](https://github.com/nodejs/node/commit/204757719c)] - **errors**: refactor to use a method that formats a list string (Daeyeon Jeong) [#45793](https://github.com/nodejs/node/pull/45793)
* \[[`463bb9602e`](https://github.com/nodejs/node/commit/463bb9602e)] - **esm**: mark `importAssertions` as required (Antoine du Hamel) [#46164](https://github.com/nodejs/node/pull/46164)
* \[[`0bdf2db079`](https://github.com/nodejs/node/commit/0bdf2db079)] - **esm**: rewrite loader hooks test (Geoffrey Booth) [#46016](https://github.com/nodejs/node/pull/46016)
* \[[`297773c6d1`](https://github.com/nodejs/node/commit/297773c6d1)] - **events**: change status of `event.returnvalue` to legacy (Deokjin Kim) [#46175](https://github.com/nodejs/node/pull/46175)
* \[[`d088d6e5c3`](https://github.com/nodejs/node/commit/d088d6e5c3)] - **events**: change status of `event.cancelBubble` to legacy (Deokjin Kim) [#46146](https://github.com/nodejs/node/pull/46146)
* \[[`36be0c4ee2`](https://github.com/nodejs/node/commit/36be0c4ee2)] - **events**: change status of `event.srcElement` to legacy (Deokjin Kim) [#46085](https://github.com/nodejs/node/pull/46085)
* \[[`b239f0684a`](https://github.com/nodejs/node/commit/b239f0684a)] - **events**: fix violation of symbol naming convention (Deokjin Kim) [#45978](https://github.com/nodejs/node/pull/45978)
* \[[`aec340b312`](https://github.com/nodejs/node/commit/aec340b312)] - **fs**: refactor to use `validateInteger` (Deokjin Kim) [#46008](https://github.com/nodejs/node/pull/46008)
* \[[`e620de6444`](https://github.com/nodejs/node/commit/e620de6444)] - **http**: refactor to use `validateHeaderName` (Deokjin Kim) [#46143](https://github.com/nodejs/node/pull/46143)
* \[[`3e70b7d863`](https://github.com/nodejs/node/commit/3e70b7d863)] - **http**: writeHead if statusmessage is undefined dont override headers (Marco Ippolito) [#46173](https://github.com/nodejs/node/pull/46173)
* \[[`3d1dd96c4f`](https://github.com/nodejs/node/commit/3d1dd96c4f)] - **http**: refactor to use min of validateNumber for maxTotalSockets (Deokjin Kim) [#46115](https://github.com/nodejs/node/pull/46115)
* \[[`4df1fcc9db`](https://github.com/nodejs/node/commit/4df1fcc9db)] - **(SEMVER-MINOR)** **http**: join authorization headers (Marco Ippolito) [#45982](https://github.com/nodejs/node/pull/45982)
* \[[`8c06e2f645`](https://github.com/nodejs/node/commit/8c06e2f645)] - **http**: replace `var` with `const` on code of comment (Deokjin Kim) [#45951](https://github.com/nodejs/node/pull/45951)
* \[[`3c0c5e0567`](https://github.com/nodejs/node/commit/3c0c5e0567)] - **(SEMVER-MINOR)** **http**: improved timeout defaults handling (Paolo Insogna) [#45778](https://github.com/nodejs/node/pull/45778)
* \[[`edcd4fc576`](https://github.com/nodejs/node/commit/edcd4fc576)] - **lib**: use kEmptyObject and update JSDoc in webstreams (Deokjin Kim) [#46183](https://github.com/nodejs/node/pull/46183)
* \[[`d6fc855b8a`](https://github.com/nodejs/node/commit/d6fc855b8a)] - **lib**: refactor to use validate function (Deokjin Kim) [#46101](https://github.com/nodejs/node/pull/46101)
* \[[`bc17f37b98`](https://github.com/nodejs/node/commit/bc17f37b98)] - **lib**: reuse invalid state errors on webstreams (Rafael Gonzaga) [#46086](https://github.com/nodejs/node/pull/46086)
* \[[`86554bf27c`](https://github.com/nodejs/node/commit/86554bf27c)] - **lib**: fix incorrect use of console intrinsic (Colin Ihrig) [#46044](https://github.com/nodejs/node/pull/46044)
* \[[`7fc7b19124`](https://github.com/nodejs/node/commit/7fc7b19124)] - **lib**: update JSDoc of `getOwnPropertyValueOrDefault` (Deokjin Kim) [#46010](https://github.com/nodejs/node/pull/46010)
* \[[`c1cc1f9e12`](https://github.com/nodejs/node/commit/c1cc1f9e12)] - **lib**: use `kEmptyObject` as default value for options (Deokjin Kim) [#46011](https://github.com/nodejs/node/pull/46011)
* \[[`db617222da`](https://github.com/nodejs/node/commit/db617222da)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#46215](https://github.com/nodejs/node/pull/46215)
* \[[`10afecd0d8`](https://github.com/nodejs/node/commit/10afecd0d8)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#46130](https://github.com/nodejs/node/pull/46130)
* \[[`d8ce990ce6`](https://github.com/nodejs/node/commit/d8ce990ce6)] - **meta**: update comment in `CODEOWNERS` to better reflect current policy (Antoine du Hamel) [#45944](https://github.com/nodejs/node/pull/45944)
* \[[`e3f0194168`](https://github.com/nodejs/node/commit/e3f0194168)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#46040](https://github.com/nodejs/node/pull/46040)
* \[[`d31c478929`](https://github.com/nodejs/node/commit/d31c478929)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45968](https://github.com/nodejs/node/pull/45968)
* \[[`10a276a3e0`](https://github.com/nodejs/node/commit/10a276a3e0)] - **meta**: add `nodejs/loaders` to CODEOWNERS (Geoffrey Booth) [#45940](https://github.com/nodejs/node/pull/45940)
* \[[`56247d7c87`](https://github.com/nodejs/node/commit/56247d7c87)] - **meta**: add `nodejs/test_runner` to CODEOWNERS (Antoine du Hamel) [#45935](https://github.com/nodejs/node/pull/45935)
* \[[`3bef8bc743`](https://github.com/nodejs/node/commit/3bef8bc743)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45899](https://github.com/nodejs/node/pull/45899)
* \[[`baf30ee935`](https://github.com/nodejs/node/commit/baf30ee935)] - **module**: fix unintended mutation (Antoine du Hamel) [#46108](https://github.com/nodejs/node/pull/46108)
* \[[`3ad584c357`](https://github.com/nodejs/node/commit/3ad584c357)] - **net**: handle socket.write(cb) edge case (Santiago Gimeno) [#45922](https://github.com/nodejs/node/pull/45922)
* \[[`2ab35cf0cc`](https://github.com/nodejs/node/commit/2ab35cf0cc)] - **node-api**: disambiguate napi\_add\_finalizer (Chengzhong Wu) [#45401](https://github.com/nodejs/node/pull/45401)
* \[[`6e9676e986`](https://github.com/nodejs/node/commit/6e9676e986)] - **node-api**: generalize finalizer second pass callback (Chengzhong Wu) [#44141](https://github.com/nodejs/node/pull/44141)
* \[[`b2faceff0a`](https://github.com/nodejs/node/commit/b2faceff0a)] - **(SEMVER-MINOR)** **os**: add availableParallelism() (Colin Ihrig) [#45895](https://github.com/nodejs/node/pull/45895)
* \[[`8fac4c5684`](https://github.com/nodejs/node/commit/8fac4c5684)] - **perf\_hooks**: fix checking range of `options.figures` in createHistogram (Deokjin Kim) [#45999](https://github.com/nodejs/node/pull/45999)
* \[[`ea73702847`](https://github.com/nodejs/node/commit/ea73702847)] - **process,worker**: ensure code after exit() effectless (ywave620) [#45620](https://github.com/nodejs/node/pull/45620)
* \[[`784ed594ea`](https://github.com/nodejs/node/commit/784ed594ea)] - **repl**: improve robustness wrt to prototype pollution (Antoine du Hamel) [#45604](https://github.com/nodejs/node/pull/45604)
* \[[`fcfde3412e`](https://github.com/nodejs/node/commit/fcfde3412e)] - **src**: rename internal module declaration as internal bindings (Chengzhong Wu) [#45551](https://github.com/nodejs/node/pull/45551)
* \[[`646cadccd0`](https://github.com/nodejs/node/commit/646cadccd0)] - **src**: fix endianness of simdutf (Yagiz Nizipli) [#46257](https://github.com/nodejs/node/pull/46257)
* \[[`94605b1665`](https://github.com/nodejs/node/commit/94605b1665)] - **src**: replace unreachable code with static\_assert (Tobias Nießen) [#46209](https://github.com/nodejs/node/pull/46209)
* \[[`3ce39bbcb7`](https://github.com/nodejs/node/commit/3ce39bbcb7)] - **src**: hide kMaxDigestMultiplier outside HKDF impl (Tobias Nießen) [#46206](https://github.com/nodejs/node/pull/46206)
* \[[`9648b06e09`](https://github.com/nodejs/node/commit/9648b06e09)] - **src**: distinguish env stopping flags (Chengzhong Wu) [#45907](https://github.com/nodejs/node/pull/45907)
* \[[`53ecd20bbd`](https://github.com/nodejs/node/commit/53ecd20bbd)] - **src**: remove return after abort (Shelley Vohr) [#46172](https://github.com/nodejs/node/pull/46172)
* \[[`c4c8931b9d`](https://github.com/nodejs/node/commit/c4c8931b9d)] - **src**: remove unnecessary semicolons (Shelley Vohr) [#46171](https://github.com/nodejs/node/pull/46171)
* \[[`fab72b1677`](https://github.com/nodejs/node/commit/fab72b1677)] - **src**: use simdutf for converting externalized builtins to UTF-16 (Anna Henningsen) [#46119](https://github.com/nodejs/node/pull/46119)
* \[[`67729961e7`](https://github.com/nodejs/node/commit/67729961e7)] - **src**: use constant strings for memory info names (Chengzhong Wu) [#46087](https://github.com/nodejs/node/pull/46087)
* \[[`0ac4e5dd34`](https://github.com/nodejs/node/commit/0ac4e5dd34)] - **src**: fix typo in node\_snapshotable.cc (Vadim) [#46103](https://github.com/nodejs/node/pull/46103)
* \[[`b454a7665d`](https://github.com/nodejs/node/commit/b454a7665d)] - **src**: keep PipeWrap::Open function consistent with TCPWrap (theanarkh) [#46064](https://github.com/nodejs/node/pull/46064)
* \[[`41f5a29cca`](https://github.com/nodejs/node/commit/41f5a29cca)] - **src**: speed up process.getActiveResourcesInfo() (Darshan Sen) [#46014](https://github.com/nodejs/node/pull/46014)
* \[[`02a61dd6bd`](https://github.com/nodejs/node/commit/02a61dd6bd)] - **src**: fix typo in `node_file.cc` (Vadim) [#45998](https://github.com/nodejs/node/pull/45998)
* \[[`99c033ed98`](https://github.com/nodejs/node/commit/99c033ed98)] - **src**: fix crash on OnStreamRead on Windows (Santiago Gimeno) [#45878](https://github.com/nodejs/node/pull/45878)
* \[[`27d6a8b2b1`](https://github.com/nodejs/node/commit/27d6a8b2b1)] - **src**: fix creating `Isolate`s from addons (Anna Henningsen) [#45885](https://github.com/nodejs/node/pull/45885)
* \[[`9ca31cdba3`](https://github.com/nodejs/node/commit/9ca31cdba3)] - **src**: use string\_view for FastStringKey implementation (Anna Henningsen) [#45914](https://github.com/nodejs/node/pull/45914)
* \[[`e4fc3abfd5`](https://github.com/nodejs/node/commit/e4fc3abfd5)] - **src**: fix UB in overflow checks (Ben Noordhuis) [#45882](https://github.com/nodejs/node/pull/45882)
* \[[`574afac26a`](https://github.com/nodejs/node/commit/574afac26a)] - **src**: check size of args before using for exec\_path (A. Wilcox) [#45902](https://github.com/nodejs/node/pull/45902)
* \[[`f0692468cd`](https://github.com/nodejs/node/commit/f0692468cd)] - **src**: fix tls certificate root store data race (Ben Noordhuis) [#45767](https://github.com/nodejs/node/pull/45767)
* \[[`a749ceda2e`](https://github.com/nodejs/node/commit/a749ceda2e)] - **src**: add undici and acorn to `process.versions` (Debadree Chatterjee) [#45621](https://github.com/nodejs/node/pull/45621)
* \[[`08a6a61575`](https://github.com/nodejs/node/commit/08a6a61575)] - **src,lib**: the handle keeps loop alive in cluster rr mode (theanarkh) [#46161](https://github.com/nodejs/node/pull/46161)
* \[[`a87963de6b`](https://github.com/nodejs/node/commit/a87963de6b)] - **stream**: fix pipeline calling end on destination more than once (Debadree Chatterjee) [#46226](https://github.com/nodejs/node/pull/46226)
* \[[`cde59606cd`](https://github.com/nodejs/node/commit/cde59606cd)] - **(SEMVER-MINOR)** **stream**: implement finished() for ReadableStream and WritableStream (Debadree Chatterjee) [#46205](https://github.com/nodejs/node/pull/46205)
* \[[`441d9de33e`](https://github.com/nodejs/node/commit/441d9de33e)] - **stream**: refactor to use `validateFunction` (Deokjin Kim) [#46007](https://github.com/nodejs/node/pull/46007)
* \[[`325fc08d48`](https://github.com/nodejs/node/commit/325fc08d48)] - **stream**: fix typo in JSDoc (Deokjin Kim) [#45991](https://github.com/nodejs/node/pull/45991)
* \[[`536322fa1c`](https://github.com/nodejs/node/commit/536322fa1c)] - **test**: update postject to 1.0.0-alpha.4 (Node.js GitHub Bot) [#46212](https://github.com/nodejs/node/pull/46212)
* \[[`a3056f4125`](https://github.com/nodejs/node/commit/a3056f4125)] - **test**: refactor to avoid mutation of global by a loader (Michaël Zasso) [#46220](https://github.com/nodejs/node/pull/46220)
* \[[`1790569518`](https://github.com/nodejs/node/commit/1790569518)] - **test**: improve test coverage for WHATWG `TextDecoder` (Juan José) [#45241](https://github.com/nodejs/node/pull/45241)
* \[[`896027c006`](https://github.com/nodejs/node/commit/896027c006)] - **test**: add fix so that test exits if port 42 is unprivileged (Suyash Nayan) [#45904](https://github.com/nodejs/node/pull/45904)
* \[[`257224da0e`](https://github.com/nodejs/node/commit/257224da0e)] - **test**: use `os.availableParallelism()` (Deokjin Kim) [#46003](https://github.com/nodejs/node/pull/46003)
* \[[`7e1462dd02`](https://github.com/nodejs/node/commit/7e1462dd02)] - **test**: update Web Events WPT (Deokjin Kim) [#46051](https://github.com/nodejs/node/pull/46051)
* \[[`40d52fbc5f`](https://github.com/nodejs/node/commit/40d52fbc5f)] - **test**: add test to once() in event lib (Jonathan Diaz) [#46126](https://github.com/nodejs/node/pull/46126)
* \[[`f3518f3337`](https://github.com/nodejs/node/commit/f3518f3337)] - **test**: use `process.hrtime.bigint` instead of `process.hrtime` (Deokjin Kim) [#45877](https://github.com/nodejs/node/pull/45877)
* \[[`4d6dd10464`](https://github.com/nodejs/node/commit/4d6dd10464)] - **test**: print failed JS/parallel tests (Geoffrey Booth) [#45960](https://github.com/nodejs/node/pull/45960)
* \[[`7cb6fef6d6`](https://github.com/nodejs/node/commit/7cb6fef6d6)] - **test**: fix test broken under --node-builtin-modules-path (Geoffrey Booth) [#45894](https://github.com/nodejs/node/pull/45894)
* \[[`55e4140c34`](https://github.com/nodejs/node/commit/55e4140c34)] - **test**: fix mock.method to support class instances (Erick Wendel) [#45608](https://github.com/nodejs/node/pull/45608)
* \[[`286acaa6fe`](https://github.com/nodejs/node/commit/286acaa6fe)] - **test**: update encoding wpt to latest (Yagiz Nizipli) [#45850](https://github.com/nodejs/node/pull/45850)
* \[[`22c1e918ce`](https://github.com/nodejs/node/commit/22c1e918ce)] - **test**: update url wpt to latest (Yagiz Nizipli) [#45852](https://github.com/nodejs/node/pull/45852)
* \[[`5fa6a70bbd`](https://github.com/nodejs/node/commit/5fa6a70bbd)] - **test**: add CryptoKey transferring tests (Filip Skokan) [#45811](https://github.com/nodejs/node/pull/45811)
* \[[`4aaec07266`](https://github.com/nodejs/node/commit/4aaec07266)] - **test**: add postject to fixtures (Darshan Sen) [#45298](https://github.com/nodejs/node/pull/45298)
* \[[`da78f9cbb8`](https://github.com/nodejs/node/commit/da78f9cbb8)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#45860](https://github.com/nodejs/node/pull/45860)
* \[[`3269423032`](https://github.com/nodejs/node/commit/3269423032)] - **test,esm**: validate more edge cases for dynamic imports (Antoine du Hamel) [#46059](https://github.com/nodejs/node/pull/46059)
* \[[`cade2fccf4`](https://github.com/nodejs/node/commit/cade2fccf4)] - **test\_runner**: run t.after() if test body throws (Colin Ihrig) [#45870](https://github.com/nodejs/node/pull/45870)
* \[[`87a0e86604`](https://github.com/nodejs/node/commit/87a0e86604)] - **test\_runner**: parse yaml (Moshe Atlow) [#45815](https://github.com/nodejs/node/pull/45815)
* \[[`757a022443`](https://github.com/nodejs/node/commit/757a022443)] - **tls**: don't treat fatal TLS alerts as EOF (David Benjamin) [#44563](https://github.com/nodejs/node/pull/44563)
* \[[`c6457cbf8d`](https://github.com/nodejs/node/commit/c6457cbf8d)] - **tls**: fix re-entrancy issue with TLS close\_notify (David Benjamin) [#44563](https://github.com/nodejs/node/pull/44563)
* \[[`fcca2d5ea6`](https://github.com/nodejs/node/commit/fcca2d5ea6)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#46214](https://github.com/nodejs/node/pull/46214)
* \[[`09adb86c19`](https://github.com/nodejs/node/commit/09adb86c19)] - **tools**: fix macro name in update-undici (Almeida) [#46217](https://github.com/nodejs/node/pull/46217)
* \[[`1b0cc79785`](https://github.com/nodejs/node/commit/1b0cc79785)] - **tools**: add automation for updating postject dependency (Darshan Sen) [#46157](https://github.com/nodejs/node/pull/46157)
* \[[`38df662119`](https://github.com/nodejs/node/commit/38df662119)] - **tools**: update create-or-update-pull-request-action (Michaël Zasso) [#46169](https://github.com/nodejs/node/pull/46169)
* \[[`3f4c0c0de1`](https://github.com/nodejs/node/commit/3f4c0c0de1)] - **tools**: update eslint to 8.31.0 (Node.js GitHub Bot) [#46131](https://github.com/nodejs/node/pull/46131)
* \[[`f3dc4329e6`](https://github.com/nodejs/node/commit/f3dc4329e6)] - **tools**: update lint-md-dependencies to rollup\@3.9.1 (Node.js GitHub Bot) [#46129](https://github.com/nodejs/node/pull/46129)
* \[[`fafbd1ca72`](https://github.com/nodejs/node/commit/fafbd1ca72)] - **tools**: move update-eslint.sh to dep\_updaters/ (Luigi Pinca) [#46088](https://github.com/nodejs/node/pull/46088)
* \[[`609df01fa9`](https://github.com/nodejs/node/commit/609df01fa9)] - **tools**: make update-eslint.sh work with npm\@9 (Luigi Pinca) [#46088](https://github.com/nodejs/node/pull/46088)
* \[[`31b8cf1a4d`](https://github.com/nodejs/node/commit/31b8cf1a4d)] - **tools**: fix lint rule recommendation (Colin Ihrig) [#46044](https://github.com/nodejs/node/pull/46044)
* \[[`0a80cbdcb1`](https://github.com/nodejs/node/commit/0a80cbdcb1)] - **tools**: update lint-md-dependencies to rollup\@3.9.0 (Node.js GitHub Bot) [#46039](https://github.com/nodejs/node/pull/46039)
* \[[`18503fa7ba`](https://github.com/nodejs/node/commit/18503fa7ba)] - **tools**: update doc to unist-util-select\@4.0.2 (Node.js GitHub Bot) [#46038](https://github.com/nodejs/node/pull/46038)
* \[[`b48e82ec1d`](https://github.com/nodejs/node/commit/b48e82ec1d)] - **tools**: add release host var to promotion script (Ruy Adorno) [#45913](https://github.com/nodejs/node/pull/45913)
* \[[`3b93b0c1f5`](https://github.com/nodejs/node/commit/3b93b0c1f5)] - **tools**: add url to `AUTHORS` update automation (Antoine du Hamel) [#45971](https://github.com/nodejs/node/pull/45971)
* \[[`623b0eba81`](https://github.com/nodejs/node/commit/623b0eba81)] - **tools**: update lint-md-dependencies to rollup\@3.8.1 (Node.js GitHub Bot) [#45967](https://github.com/nodejs/node/pull/45967)
* \[[`b0e88377fe`](https://github.com/nodejs/node/commit/b0e88377fe)] - **tools**: update GitHub workflow action (Mohammed Keyvanzadeh) [#45937](https://github.com/nodejs/node/pull/45937)
* \[[`974442e69d`](https://github.com/nodejs/node/commit/974442e69d)] - **tools**: update lint-md dependencies (Node.js GitHub Bot) [#45813](https://github.com/nodejs/node/pull/45813)
* \[[`5aaa8c3bbf`](https://github.com/nodejs/node/commit/5aaa8c3bbf)] - **tools**: enforce use of trailing commas in `tools/` (Antoine du Hamel) [#45889](https://github.com/nodejs/node/pull/45889)
* \[[`1e32520f72`](https://github.com/nodejs/node/commit/1e32520f72)] - **tools**: add `ArrayPrototypeConcat` to the list of primordials to avoid (Antoine du Hamel) [#44445](https://github.com/nodejs/node/pull/44445)
* \[[`e0cda56204`](https://github.com/nodejs/node/commit/e0cda56204)] - **tools**: fix incorrect version history order (Fabien Michel) [#45728](https://github.com/nodejs/node/pull/45728)
* \[[`7438ff175a`](https://github.com/nodejs/node/commit/7438ff175a)] - **tools**: update eslint to 8.29.0 (Node.js GitHub Bot) [#45733](https://github.com/nodejs/node/pull/45733)
* \[[`1e11247b91`](https://github.com/nodejs/node/commit/1e11247b91)] - _**Revert**_ "**tools**: update V8 gypfiles for RISC-V" (Lu Yahan) [#46156](https://github.com/nodejs/node/pull/46156)
* \[[`0defe4effa`](https://github.com/nodejs/node/commit/0defe4effa)] - **trace\_events**: refactor to use `validateStringArray` (Deokjin Kim) [#46012](https://github.com/nodejs/node/pull/46012)
* \[[`f1dcbe7652`](https://github.com/nodejs/node/commit/f1dcbe7652)] - **util**: add fast path for text-decoder fatal flag (Yagiz Nizipli) [#45803](https://github.com/nodejs/node/pull/45803)
* \[[`277d9da876`](https://github.com/nodejs/node/commit/277d9da876)] - **vm**: refactor to use validate function (Deokjin Kim) [#46176](https://github.com/nodejs/node/pull/46176)
* \[[`96f1b2e731`](https://github.com/nodejs/node/commit/96f1b2e731)] - **vm**: refactor to use `validateStringArray` (Deokjin Kim) [#46020](https://github.com/nodejs/node/pull/46020)

<a id="18.13.0"></a>

## 2023-01-05, Version 18.13.0 'Hydrogen' (LTS), @danielleadams

### Notable changes

#### Add support for externally shared js builtins

By default Node.js is built so that all dependencies are bundled into the Node.js binary itself. Some Node.js distributions prefer to manage dependencies externally. There are existing build options that allow dependencies with native code to be externalized. This commit adds additional options so that dependencies with JavaScript code (including WASM) can also be externalized. This addition does not affect binaries shipped by the Node.js project but will allow other distributions to externalize additional dependencies when needed.

Contributed by Michael Dawson in [#44376](https://github.com/nodejs/node/pull/44376)

#### Introduce `File`

The File class is part of the [FileAPI](https://w3c.github.io/FileAPI/). It can be used anywhere a Blob can, for example in `URL.createObjectURL` and `FormData`. It contains two properties that Blobs do not have: `lastModified`, the last time the file was modified in ms, and `name`, the name of the file.

Contributed by Khafra in [#45139](https://github.com/nodejs/node/pull/45139)

#### Support function mocking on Node.js test runner

The `node:test` module supports mocking during testing via a top-level `mock`
object.

```js
test('spies on an object method', (t) => {
  const number = {
    value: 5,
    add(a) {
      return this.value + a;
    },
  };
  t.mock.method(number, 'add');

  assert.strictEqual(number.add(3), 8);
  assert.strictEqual(number.add.mock.calls.length, 1);
});
```

Contributed by Colin Ihrig in [#45326](https://github.com/nodejs/node/pull/45326)

#### Other notable changes

* **build**:
  * disable v8 snapshot compression by default (Joyee Cheung) [#45716](https://github.com/nodejs/node/pull/45716)
* **crypto**:
  * update root certificates (Luigi Pinca) [#45490](https://github.com/nodejs/node/pull/45490)
* **deps**:
  * update ICU to 72.1 (Michaël Zasso) [#45068](https://github.com/nodejs/node/pull/45068)
* **doc**:
  * add doc-only deprecation for headers/trailers setters (Rich Trott) [#45697](https://github.com/nodejs/node/pull/45697)
  * add Rafael to the tsc (Michael Dawson) [#45691](https://github.com/nodejs/node/pull/45691)
  * deprecate use of invalid ports in `url.parse` (Antoine du Hamel) [#45576](https://github.com/nodejs/node/pull/45576)
  * add lukekarrys to collaborators (Luke Karrys) [#45180](https://github.com/nodejs/node/pull/45180)
  * add anonrig to collaborators (Yagiz Nizipli) [#45002](https://github.com/nodejs/node/pull/45002)
  * deprecate url.parse() (Rich Trott) [#44919](https://github.com/nodejs/node/pull/44919)
* **lib**:
  * drop fetch experimental warning (Matteo Collina) [#45287](https://github.com/nodejs/node/pull/45287)
* **net**:
  * (SEMVER-MINOR) add autoSelectFamily and autoSelectFamilyAttemptTimeout options (Paolo Insogna) [#44731](https://github.com/nodejs/node/pull/44731)
* **src**:
  * (SEMVER-MINOR) add uvwasi version (Jithil P Ponnan) [#45639](https://github.com/nodejs/node/pull/45639)
  * (SEMVER-MINOR) add initial shadow realm support (Chengzhong Wu) [#42869](https://github.com/nodejs/node/pull/42869)
* **test\_runner**:
  * (SEMVER-MINOR) add t.after() hook (Colin Ihrig) [#45792](https://github.com/nodejs/node/pull/45792)
  * (SEMVER-MINOR) don't use a symbol for runHook() (Colin Ihrig) [#45792](https://github.com/nodejs/node/pull/45792)
* **tls**:
  * (SEMVER-MINOR) add "ca" property to certificate object (Ben Noordhuis) [#44935](https://github.com/nodejs/node/pull/44935)
  * remove trustcor root ca certificates (Ben Noordhuis) [#45776](https://github.com/nodejs/node/pull/45776)
* **tools**:
  * update certdata.txt (Luigi Pinca) [#45490](https://github.com/nodejs/node/pull/45490)
* **util**:
  * add fast path for utf8 encoding (Yagiz Nizipli) [#45412](https://github.com/nodejs/node/pull/45412)
  * improve textdecoder decode performance (Yagiz Nizipli) [#45294](https://github.com/nodejs/node/pull/45294)
  * (SEMVER-MINOR) add MIME utilities (#21128) (Bradley Farias) [#21128](https://github.com/nodejs/node/pull/21128)

### Commits

* \[[`40123a6bb0`](https://github.com/nodejs/node/commit/40123a6bb0)] - **(SEMVER-MINOR)** **async\_hooks**: add hook to stop propagation (Gerhard Stöbich) [#45386](https://github.com/nodejs/node/pull/45386)
* \[[`9925d20ed8`](https://github.com/nodejs/node/commit/9925d20ed8)] - **benchmark**: add variety of inputs to text-encoder (Yagiz Nizipli) [#45787](https://github.com/nodejs/node/pull/45787)
* \[[`5e167bd658`](https://github.com/nodejs/node/commit/5e167bd658)] - **benchmark**: make benchmarks runnable in older versions of Node.js (Joyee Cheung) [#45746](https://github.com/nodejs/node/pull/45746)
* \[[`a1421623ac`](https://github.com/nodejs/node/commit/a1421623ac)] - **benchmark**: add v8 serialize benchmark (Yagiz Nizipli) [#45476](https://github.com/nodejs/node/pull/45476)
* \[[`fcf61884cc`](https://github.com/nodejs/node/commit/fcf61884cc)] - **benchmark**: add text-encoder benchmark (Yagiz Nizipli) [#45450](https://github.com/nodejs/node/pull/45450)
* \[[`762d285c98`](https://github.com/nodejs/node/commit/762d285c98)] - **benchmark**: add parameters to text-decoder benchmark (Yagiz Nizipli) [#45363](https://github.com/nodejs/node/pull/45363)
* \[[`ab891ecbff`](https://github.com/nodejs/node/commit/ab891ecbff)] - **benchmark**: fix text-decoder benchmark (Yagiz Nizipli) [#45363](https://github.com/nodejs/node/pull/45363)
* \[[`1ed312a737`](https://github.com/nodejs/node/commit/1ed312a737)] - **benchmark**: add blob benchmark (Yagiz Nizipli) [#44990](https://github.com/nodejs/node/pull/44990)
* \[[`2ee3d81277`](https://github.com/nodejs/node/commit/2ee3d81277)] - **bootstrap**: merge main thread and worker thread initializations (Joyee Cheung) [#44869](https://github.com/nodejs/node/pull/44869)
* \[[`e638ea4f48`](https://github.com/nodejs/node/commit/e638ea4f48)] - **bootstrap**: check more metadata when loading the snapshot (Joyee Cheung) [#44132](https://github.com/nodejs/node/pull/44132)
* \[[`bfcf4f0046`](https://github.com/nodejs/node/commit/bfcf4f0046)] - **buffer**: make decodeUTF8 params loose (Yagiz Nizipli) [#45610](https://github.com/nodejs/node/pull/45610)
* \[[`3a7f3d5993`](https://github.com/nodejs/node/commit/3a7f3d5993)] - **(SEMVER-MINOR)** **buffer**: introduce File (Khafra) [#45139](https://github.com/nodejs/node/pull/45139)
* \[[`345b847aa6`](https://github.com/nodejs/node/commit/345b847aa6)] - **buffer**: fix validation of options in `Blob` constructor (Antoine du Hamel) [#45156](https://github.com/nodejs/node/pull/45156)
* \[[`1ddc438444`](https://github.com/nodejs/node/commit/1ddc438444)] - **build**: disable v8 snapshot compression by default (Joyee Cheung) [#45716](https://github.com/nodejs/node/pull/45716)
* \[[`bd1a2fbd91`](https://github.com/nodejs/node/commit/bd1a2fbd91)] - **build**: add python 3.11 support for android (Mohammed Keyvanzadeh) [#45765](https://github.com/nodejs/node/pull/45765)
* \[[`2b0ace302d`](https://github.com/nodejs/node/commit/2b0ace302d)] - **build**: rework gyp files for zlib (Richard Lau) [#45589](https://github.com/nodejs/node/pull/45589)
* \[[`5ab7a30a06`](https://github.com/nodejs/node/commit/5ab7a30a06)] - **build**: avoid redefined macro (Michaël Zasso) [#45544](https://github.com/nodejs/node/pull/45544)
* \[[`f58b32c22e`](https://github.com/nodejs/node/commit/f58b32c22e)] - **build**: fix env.h for cpp20 (Jiawen Geng) [#45516](https://github.com/nodejs/node/pull/45516)
* \[[`1de1f679ec`](https://github.com/nodejs/node/commit/1de1f679ec)] - _**Revert**_ "**build**: remove precompiled header and debug information for host builds" (Stefan Stojanovic) [#45432](https://github.com/nodejs/node/pull/45432)
* \[[`89d1eb58b0`](https://github.com/nodejs/node/commit/89d1eb58b0)] - **build**: add --v8-disable-object-print flag (MURAKAMI Masahiko) [#45458](https://github.com/nodejs/node/pull/45458)
* \[[`f2a4def232`](https://github.com/nodejs/node/commit/f2a4def232)] - **build**: make scripts in gyp run with right python (Jiawen Geng) [#45435](https://github.com/nodejs/node/pull/45435)
* \[[`473a879c91`](https://github.com/nodejs/node/commit/473a879c91)] - **build**: workaround for node-core-utils (Jiawen Geng) [#45199](https://github.com/nodejs/node/pull/45199)
* \[[`abcc034c61`](https://github.com/nodejs/node/commit/abcc034c61)] - **build**: fix icu-small build with ICU 72.1 (Steven R. Loomis) [#45195](https://github.com/nodejs/node/pull/45195)
* \[[`8a99221a21`](https://github.com/nodejs/node/commit/8a99221a21)] - **build**: remove unused language files (Ben Noordhuis) [#45138](https://github.com/nodejs/node/pull/45138)
* \[[`3fb44f9413`](https://github.com/nodejs/node/commit/3fb44f9413)] - **build**: add GitHub token to auto-start-ci workflow (Richard Lau) [#45185](https://github.com/nodejs/node/pull/45185)
* \[[`2aac993bb2`](https://github.com/nodejs/node/commit/2aac993bb2)] - **build**: add version info to timezone update PR (Darshan Sen) [#45021](https://github.com/nodejs/node/pull/45021)
* \[[`0db19b3c60`](https://github.com/nodejs/node/commit/0db19b3c60)] - **build**: support Python 3.11 (Luigi Pinca) [#45191](https://github.com/nodejs/node/pull/45191)
* \[[`fb008a2e9b`](https://github.com/nodejs/node/commit/fb008a2e9b)] - **build,deps,src**: fix Intel VTune profiling support (Shi Lei) [#45248](https://github.com/nodejs/node/pull/45248)
* \[[`61bc27a5b4`](https://github.com/nodejs/node/commit/61bc27a5b4)] - **build,win**: pass --debug-nghttp2 to configure (Santiago Gimeno) [#45209](https://github.com/nodejs/node/pull/45209)
* \[[`7b68c06988`](https://github.com/nodejs/node/commit/7b68c06988)] - **child\_process**: validate arguments for null bytes (Darshan Sen) [#44782](https://github.com/nodejs/node/pull/44782)
* \[[`bac6b7d900`](https://github.com/nodejs/node/commit/bac6b7d900)] - **crypto**: simplify lazy loading of internal modules (Antoine du Hamel) [#45809](https://github.com/nodejs/node/pull/45809)
* \[[`2fbf95662c`](https://github.com/nodejs/node/commit/2fbf95662c)] - **crypto**: fix CipherBase Update int32 overflow (Marco Ippolito) [#45769](https://github.com/nodejs/node/pull/45769)
* \[[`0100fd445b`](https://github.com/nodejs/node/commit/0100fd445b)] - **crypto**: refactor ArrayBuffer to bigint conversion utils (Antoine du Hamel) [#45567](https://github.com/nodejs/node/pull/45567)
* \[[`fa0a2d8e5d`](https://github.com/nodejs/node/commit/fa0a2d8e5d)] - **crypto**: refactor verify acceptable key usage functions (Filip Skokan) [#45569](https://github.com/nodejs/node/pull/45569)
* \[[`ef64b86d0d`](https://github.com/nodejs/node/commit/ef64b86d0d)] - **crypto**: fix ECDH webcrypto public CryptoKey usages (Filip Skokan) [#45569](https://github.com/nodejs/node/pull/45569)
* \[[`b92b80424a`](https://github.com/nodejs/node/commit/b92b80424a)] - **crypto**: validate CFRG webcrypto JWK import "d" and "x" are a pair (Filip Skokan) [#45569](https://github.com/nodejs/node/pull/45569)
* \[[`621e3c9cd4`](https://github.com/nodejs/node/commit/621e3c9cd4)] - **crypto**: use DataError for CFRG webcrypto raw and jwk import key checks (Filip Skokan) [#45569](https://github.com/nodejs/node/pull/45569)
* \[[`8fcfbeffe1`](https://github.com/nodejs/node/commit/8fcfbeffe1)] - **crypto**: use DataError for webcrypto keyData import failures (Filip Skokan) [#45569](https://github.com/nodejs/node/pull/45569)
* \[[`a976a63138`](https://github.com/nodejs/node/commit/a976a63138)] - **crypto**: fix X25519 and X448 webcrypto public CryptoKey usages (Filip Skokan) [#45569](https://github.com/nodejs/node/pull/45569)
* \[[`27adcc9c4b`](https://github.com/nodejs/node/commit/27adcc9c4b)] - **crypto**: ensure "x" is present when importing private CFRG webcrypto keys (Filip Skokan) [#45569](https://github.com/nodejs/node/pull/45569)
* \[[`f27ebab56c`](https://github.com/nodejs/node/commit/f27ebab56c)] - **crypto**: clear OpenSSL error queue after calling X509\_check\_private\_key() (Filip Skokan) [#45495](https://github.com/nodejs/node/pull/45495)
* \[[`319ae095fb`](https://github.com/nodejs/node/commit/319ae095fb)] - **crypto**: update root certificates (Luigi Pinca) [#45490](https://github.com/nodejs/node/pull/45490)
* \[[`dae92e78d6`](https://github.com/nodejs/node/commit/dae92e78d6)] - **crypto**: clear OpenSSL error queue after calling X509\_verify() (Takuro Sato) [#45377](https://github.com/nodejs/node/pull/45377)
* \[[`1ba1809dfd`](https://github.com/nodejs/node/commit/1ba1809dfd)] - **crypto**: handle more webcrypto errors with OperationError (Filip Skokan) [#45320](https://github.com/nodejs/node/pull/45320)
* \[[`b54f8761ae`](https://github.com/nodejs/node/commit/b54f8761ae)] - **crypto**: handle unsupported AES ciphers in webcrypto (Filip Skokan) [#45321](https://github.com/nodejs/node/pull/45321)
* \[[`57f507f1dd`](https://github.com/nodejs/node/commit/57f507f1dd)] - **crypto**: fix webcrypto HMAC "get key length" in deriveKey and generateKey (Filip Skokan) [#44917](https://github.com/nodejs/node/pull/44917)
* \[[`7565a75ee5`](https://github.com/nodejs/node/commit/7565a75ee5)] - **crypto**: remove webcrypto HKDF and PBKDF2 default-applied lengths (Filip Skokan) [#44945](https://github.com/nodejs/node/pull/44945)
* \[[`631421e8d0`](https://github.com/nodejs/node/commit/631421e8d0)] - **crypto**: simplify webcrypto ECDH deriveBits (Filip Skokan) [#44946](https://github.com/nodejs/node/pull/44946)
* \[[`c4f665f528`](https://github.com/nodejs/node/commit/c4f665f528)] - **deps**: V8: cherry-pick d1d4c648e7ff (Danielle Adams) [#46098](https://github.com/nodejs/node/pull/46098)
* \[[`c04e1df396`](https://github.com/nodejs/node/commit/c04e1df396)] - _**Revert**_ "**deps**: fix zlib compilation for CPUs without SIMD features" (Luigi Pinca) [#45589](https://github.com/nodejs/node/pull/45589)
* \[[`34e708cded`](https://github.com/nodejs/node/commit/34e708cded)] - **deps**: update undici to 5.13.0 (Node.js GitHub Bot) [#45634](https://github.com/nodejs/node/pull/45634)
* \[[`33b0664bbe`](https://github.com/nodejs/node/commit/33b0664bbe)] - **deps**: update corepack to 0.15.2 (Node.js GitHub Bot) [#45635](https://github.com/nodejs/node/pull/45635)
* \[[`7b6d2a8ec0`](https://github.com/nodejs/node/commit/7b6d2a8ec0)] - **deps**: update nghttp2 to 1.51.0 (Yagiz Nizipli) [#45537](https://github.com/nodejs/node/pull/45537)
* \[[`02eabaf409`](https://github.com/nodejs/node/commit/02eabaf409)] - **deps**: update base64 to 0.5.0 (Facundo Tuesca) [#45509](https://github.com/nodejs/node/pull/45509)
* \[[`7d26bf3c08`](https://github.com/nodejs/node/commit/7d26bf3c08)] - **deps**: V8: cherry-pick 9df5ef70ff18 (Yagiz Nizipli) [#45474](https://github.com/nodejs/node/pull/45474)
* \[[`43419ad6bc`](https://github.com/nodejs/node/commit/43419ad6bc)] - **deps**: fix zlib compilation for CPUs without SIMD features (Anna Henningsen) [#45387](https://github.com/nodejs/node/pull/45387)
* \[[`978cfad005`](https://github.com/nodejs/node/commit/978cfad005)] - **deps**: update zlib to upstream 8bbd6c31 (Luigi Pinca) [#45387](https://github.com/nodejs/node/pull/45387)
* \[[`72362f348c`](https://github.com/nodejs/node/commit/72362f348c)] - **deps**: update acorn to 8.8.1 (Node.js GitHub Bot) [#45441](https://github.com/nodejs/node/pull/45441)
* \[[`17a89d1f4e`](https://github.com/nodejs/node/commit/17a89d1f4e)] - **deps**: V8: cherry-pick 031b98b25cba (Michaël Zasso) [#45375](https://github.com/nodejs/node/pull/45375)
* \[[`bbe67c484a`](https://github.com/nodejs/node/commit/bbe67c484a)] - **deps**: upgrade npm to 8.19.3 (npm team) [#45322](https://github.com/nodejs/node/pull/45322)
* \[[`a274d6bc0d`](https://github.com/nodejs/node/commit/a274d6bc0d)] - **deps**: update corepack to 0.15.1 (Node.js GitHub Bot) [#45331](https://github.com/nodejs/node/pull/45331)
* \[[`c9c958e188`](https://github.com/nodejs/node/commit/c9c958e188)] - **deps**: upgrade to libuv 1.44.2 (Luigi Pinca) [#42340](https://github.com/nodejs/node/pull/42340)
* \[[`07b47ad58c`](https://github.com/nodejs/node/commit/07b47ad58c)] - **deps**: update corepack to 0.15.0 (Node.js GitHub Bot) [#45235](https://github.com/nodejs/node/pull/45235)
* \[[`bb6e8b1972`](https://github.com/nodejs/node/commit/bb6e8b1972)] - **deps**: update undici to 5.12.0 (Node.js GitHub Bot) [#45236](https://github.com/nodejs/node/pull/45236)
* \[[`596e3a8f2f`](https://github.com/nodejs/node/commit/596e3a8f2f)] - **deps**: V8: cherry-pick c2792e58035f (Jiawen Geng) [#44961](https://github.com/nodejs/node/pull/44961)
* \[[`2088cb4744`](https://github.com/nodejs/node/commit/2088cb4744)] - **deps**: patch V8 to 10.2.154.23 (Michaël Zasso) [#45997](https://github.com/nodejs/node/pull/45997)
* \[[`6ea555e8ac`](https://github.com/nodejs/node/commit/6ea555e8ac)] - **deps**: V8: cherry-pick 2ada52cffbff (Michaël Zasso) [#45573](https://github.com/nodejs/node/pull/45573)
* \[[`6d8c0f0efd`](https://github.com/nodejs/node/commit/6d8c0f0efd)] - **deps**: update timezone to 2022f (Node.js GitHub Bot) [#45289](https://github.com/nodejs/node/pull/45289)
* \[[`3b73aa416f`](https://github.com/nodejs/node/commit/3b73aa416f)] - **deps**: update ICU to 72.1 (Michaël Zasso) [#45068](https://github.com/nodejs/node/pull/45068)
* \[[`555d1723b1`](https://github.com/nodejs/node/commit/555d1723b1)] - **deps**: update timezone (Node.js GitHub Bot) [#44950](https://github.com/nodejs/node/pull/44950)
* \[[`5c0fcc13f7`](https://github.com/nodejs/node/commit/5c0fcc13f7)] - **deps**: patch V8 to 10.2.154.19 (Michaël Zasso) [#45229](https://github.com/nodejs/node/pull/45229)
* \[[`1a47a7bbed`](https://github.com/nodejs/node/commit/1a47a7bbed)] - **diagnostics\_channel**: fix diagnostics channel memory leak (theanarkh) [#45633](https://github.com/nodejs/node/pull/45633)
* \[[`40a29aabbc`](https://github.com/nodejs/node/commit/40a29aabbc)] - **diagnostics\_channel**: built-in channels should remain experimental (Stephen Belanger) [#45423](https://github.com/nodejs/node/pull/45423)
* \[[`2752f543bc`](https://github.com/nodejs/node/commit/2752f543bc)] - **diagnostics\_channel**: mark as stable (Stephen Belanger) [#45290](https://github.com/nodejs/node/pull/45290)
* \[[`9ceed7a1cc`](https://github.com/nodejs/node/commit/9ceed7a1cc)] - **dns**: fix port validation (Antoine du Hamel) [#45135](https://github.com/nodejs/node/pull/45135)
* \[[`108220cb05`](https://github.com/nodejs/node/commit/108220cb05)] - **doc**: buffer.fill empty value (Marco Ippolito) [#45794](https://github.com/nodejs/node/pull/45794)
* \[[`b5ad92fea2`](https://github.com/nodejs/node/commit/b5ad92fea2)] - **doc**: add args of filter option of fs.cp (MURAKAMI Masahiko) [#45739](https://github.com/nodejs/node/pull/45739)
* \[[`899ba3d3c2`](https://github.com/nodejs/node/commit/899ba3d3c2)] - **doc**: disambiguate `native module` to `addon` (Daeyeon Jeong) [#45673](https://github.com/nodejs/node/pull/45673)
* \[[`6e35803789`](https://github.com/nodejs/node/commit/6e35803789)] - **doc**: using console.error for error cases in crypto and events (emirgoren) [#45640](https://github.com/nodejs/node/pull/45640)
* \[[`fcd0f71979`](https://github.com/nodejs/node/commit/fcd0f71979)] - **doc**: fix actual result of example is different in events (Deokjin Kim) [#45656](https://github.com/nodejs/node/pull/45656)
* \[[`8203c021dc`](https://github.com/nodejs/node/commit/8203c021dc)] - **doc**: add doc-only deprecation for headers/trailers setters (Rich Trott) [#45697](https://github.com/nodejs/node/pull/45697)
* \[[`c1f90a5b7b`](https://github.com/nodejs/node/commit/c1f90a5b7b)] - **doc**: add detail on how api docs are published (Michael Dawson) [#45626](https://github.com/nodejs/node/pull/45626)
* \[[`90e9951d30`](https://github.com/nodejs/node/commit/90e9951d30)] - **doc**: use console.error for error case in child\_process and dgram (Deokjin Kim) [#45690](https://github.com/nodejs/node/pull/45690)
* \[[`d9593ce98b`](https://github.com/nodejs/node/commit/d9593ce98b)] - **doc**: move streaming instruc to doc/contributing (Michael Dawson) [#45582](https://github.com/nodejs/node/pull/45582)
* \[[`a3eb2dd9b4`](https://github.com/nodejs/node/commit/a3eb2dd9b4)] - **doc**: add Rafael to the tsc (Michael Dawson) [#45691](https://github.com/nodejs/node/pull/45691)
* \[[`fa8caa328f`](https://github.com/nodejs/node/commit/fa8caa328f)] - **doc**: add missing line in debugger (Deokjin Kim) [#45632](https://github.com/nodejs/node/pull/45632)
* \[[`3fb5c6d3c5`](https://github.com/nodejs/node/commit/3fb5c6d3c5)] - **doc**: fix actual result of example is different in stream (Deokjin Kim) [#45619](https://github.com/nodejs/node/pull/45619)
* \[[`8a1e556899`](https://github.com/nodejs/node/commit/8a1e556899)] - **doc**: add `options` parameter to eventTarget.removeEventListener (Deokjin Kim) [#45667](https://github.com/nodejs/node/pull/45667)
* \[[`6881188f0f`](https://github.com/nodejs/node/commit/6881188f0f)] - **doc**: define "react-native" community condition (Alex Hunt) [#45367](https://github.com/nodejs/node/pull/45367)
* \[[`53e01f650f`](https://github.com/nodejs/node/commit/53e01f650f)] - **doc**: move os.machine() docs to sorted position (Colin Ihrig) [#45647](https://github.com/nodejs/node/pull/45647)
* \[[`0669712cbd`](https://github.com/nodejs/node/commit/0669712cbd)] - **doc**: use console.error for error case in fs, https, net and process (Deokjin Kim) [#45606](https://github.com/nodejs/node/pull/45606)
* \[[`ebc89f15fe`](https://github.com/nodejs/node/commit/ebc89f15fe)] - **doc**: add link to doc with social processes (Michael Dawson) [#45584](https://github.com/nodejs/node/pull/45584)
* \[[`b771fdb6f8`](https://github.com/nodejs/node/commit/b771fdb6f8)] - **doc**: deprecate use of invalid ports in `url.parse` (Antoine du Hamel) [#45576](https://github.com/nodejs/node/pull/45576)
* \[[`6a36159764`](https://github.com/nodejs/node/commit/6a36159764)] - **doc**: clarify changes in readableFlowing (Kohei Ueno) [#45554](https://github.com/nodejs/node/pull/45554)
* \[[`30a8604143`](https://github.com/nodejs/node/commit/30a8604143)] - **doc**: use console.error for error case in http2 (Deokjin Kim) [#45577](https://github.com/nodejs/node/pull/45577)
* \[[`8e94339891`](https://github.com/nodejs/node/commit/8e94339891)] - **doc**: add version description about fsPromise.constants (chlorine) [#45556](https://github.com/nodejs/node/pull/45556)
* \[[`203ca494a1`](https://github.com/nodejs/node/commit/203ca494a1)] - **doc**: add missing documentation for paramEncoding (Tobias Nießen) [#45523](https://github.com/nodejs/node/pull/45523)
* \[[`31233b2064`](https://github.com/nodejs/node/commit/31233b2064)] - **doc**: fix typo in threat model (Tobias Nießen) [#45558](https://github.com/nodejs/node/pull/45558)
* \[[`e851c43c54`](https://github.com/nodejs/node/commit/e851c43c54)] - **doc**: add Node.js Threat Model (Rafael Gonzaga) [#45223](https://github.com/nodejs/node/pull/45223)
* \[[`34c2876541`](https://github.com/nodejs/node/commit/34c2876541)] - **doc**: run license-builder (github-actions\[bot]) [#45553](https://github.com/nodejs/node/pull/45553)
* \[[`dde8740af3`](https://github.com/nodejs/node/commit/dde8740af3)] - **doc**: add async\_hooks migration note (Geoffrey Booth) [#45335](https://github.com/nodejs/node/pull/45335)
* \[[`e304628fb5`](https://github.com/nodejs/node/commit/e304628fb5)] - **doc**: fix RESOLVE\_ESM\_MATCH in modules.md (翠 / green) [#45280](https://github.com/nodejs/node/pull/45280)
* \[[`8b44c6121e`](https://github.com/nodejs/node/commit/8b44c6121e)] - **doc**: add arm64 to os.machine() (Carter Snook) [#45374](https://github.com/nodejs/node/pull/45374)
* \[[`009adc7e21`](https://github.com/nodejs/node/commit/009adc7e21)] - **doc**: add lint rule to enforce trailing commas (Antoine du Hamel) [#45471](https://github.com/nodejs/node/pull/45471)
* \[[`c6b89abc5f`](https://github.com/nodejs/node/commit/c6b89abc5f)] - **doc**: adjust wording to eliminate awkward typography (Konv) [#45398](https://github.com/nodejs/node/pull/45398)
* \[[`785817e341`](https://github.com/nodejs/node/commit/785817e341)] - **doc**: fix typo in maintaining-dependencies.md (Tobias Nießen) [#45428](https://github.com/nodejs/node/pull/45428)
* \[[`00e02bda3a`](https://github.com/nodejs/node/commit/00e02bda3a)] - **doc**: allow for holidays in triage response (Michael Dawson) [#45267](https://github.com/nodejs/node/pull/45267)
* \[[`bef5206b84`](https://github.com/nodejs/node/commit/bef5206b84)] - **doc**: include last security release date (Juan José Arboleda) [#45368](https://github.com/nodejs/node/pull/45368)
* \[[`846b1aefac`](https://github.com/nodejs/node/commit/846b1aefac)] - **doc**: fix email for Ashley (Michael Dawson) [#45364](https://github.com/nodejs/node/pull/45364)
* \[[`913b669a2b`](https://github.com/nodejs/node/commit/913b669a2b)] - **doc**: fix test runner's only tests section header (Colin Ihrig) [#45343](https://github.com/nodejs/node/pull/45343)
* \[[`691da886f1`](https://github.com/nodejs/node/commit/691da886f1)] - **doc**: run license-builder (github-actions\[bot]) [#45349](https://github.com/nodejs/node/pull/45349)
* \[[`90a28920de`](https://github.com/nodejs/node/commit/90a28920de)] - **doc**: add more info for timer.setInterval (theanarkh) [#45232](https://github.com/nodejs/node/pull/45232)
* \[[`d2fc2aa666`](https://github.com/nodejs/node/commit/d2fc2aa666)] - **doc**: use module names in stability overview table (Filip Skokan) [#45312](https://github.com/nodejs/node/pull/45312)
* \[[`3f69d21b5c`](https://github.com/nodejs/node/commit/3f69d21b5c)] - **doc**: add `node:` prefix for examples (Daeyeon Jeong) [#45328](https://github.com/nodejs/node/pull/45328)
* \[[`a5c9b3c112`](https://github.com/nodejs/node/commit/a5c9b3c112)] - **doc**: update name of Node.js core Slack channel (Rich Trott) [#45293](https://github.com/nodejs/node/pull/45293)
* \[[`2bfd60b06f`](https://github.com/nodejs/node/commit/2bfd60b06f)] - **doc**: fix "task\_processor.js" typo (andreysoktoev) [#45257](https://github.com/nodejs/node/pull/45257)
* \[[`248de57278`](https://github.com/nodejs/node/commit/248de57278)] - **doc**: add history section to `fetch`-related globals (Antoine du Hamel) [#45198](https://github.com/nodejs/node/pull/45198)
* \[[`1f5975ef50`](https://github.com/nodejs/node/commit/1f5975ef50)] - **doc**: clarify moderation in `onboarding.md` (Benjamin Gruenbaum) [#41930](https://github.com/nodejs/node/pull/41930)
* \[[`4e87c36570`](https://github.com/nodejs/node/commit/4e87c36570)] - **doc**: change make lint to make lint-md (RafaelGSS) [#45197](https://github.com/nodejs/node/pull/45197)
* \[[`3381a17d2c`](https://github.com/nodejs/node/commit/3381a17d2c)] - **doc**: add more lts update steps to release guide (Ruy Adorno) [#45177](https://github.com/nodejs/node/pull/45177)
* \[[`64b0495310`](https://github.com/nodejs/node/commit/64b0495310)] - **doc**: add bmuenzenmeyer to triagers (Brian Muenzenmeyer) [#45155](https://github.com/nodejs/node/pull/45155)
* \[[`6ebdb76727`](https://github.com/nodejs/node/commit/6ebdb76727)] - **doc**: update process.release (Filip Skokan) [#45170](https://github.com/nodejs/node/pull/45170)
* \[[`05d89c4722`](https://github.com/nodejs/node/commit/05d89c4722)] - **doc**: add link to triage guide (Brian Muenzenmeyer) [#45154](https://github.com/nodejs/node/pull/45154)
* \[[`f1aa82fd3c`](https://github.com/nodejs/node/commit/f1aa82fd3c)] - **doc**: mark Node.js 12 as End-of-Life (Rafael Gonzaga) [#45186](https://github.com/nodejs/node/pull/45186)
* \[[`da6f308612`](https://github.com/nodejs/node/commit/da6f308612)] - **doc**: add lukekarrys to collaborators (Luke Karrys) [#45180](https://github.com/nodejs/node/pull/45180)
* \[[`17380a1e6a`](https://github.com/nodejs/node/commit/17380a1e6a)] - **doc**: update mark release line lts on release guide (Ruy Adorno) [#45101](https://github.com/nodejs/node/pull/45101)
* \[[`3ddb6ccb2a`](https://github.com/nodejs/node/commit/3ddb6ccb2a)] - **doc**: be more definite and present tense-y (Ben Noordhuis) [#45120](https://github.com/nodejs/node/pull/45120)
* \[[`b09c386208`](https://github.com/nodejs/node/commit/b09c386208)] - **doc**: add major version note to release guide (Ruy Adorno) [#45054](https://github.com/nodejs/node/pull/45054)
* \[[`896b48b549`](https://github.com/nodejs/node/commit/896b48b549)] - **doc**: fix v14.x link maintaining openssl guide (RafaelGSS) [#45071](https://github.com/nodejs/node/pull/45071)
* \[[`33b3d8646b`](https://github.com/nodejs/node/commit/33b3d8646b)] - **doc**: add note about latest GitHub release (Michaël Zasso) [#45111](https://github.com/nodejs/node/pull/45111)
* \[[`5e76bf5cd5`](https://github.com/nodejs/node/commit/5e76bf5cd5)] - **doc**: mention v18.x openssl maintaining guide (Rafael Gonzaga) [#45070](https://github.com/nodejs/node/pull/45070)
* \[[`b4e3f3f095`](https://github.com/nodejs/node/commit/b4e3f3f095)] - **doc**: fix display of "problematic" ASCII characters (John Gardner) [#44373](https://github.com/nodejs/node/pull/44373)
* \[[`8bb23a4da3`](https://github.com/nodejs/node/commit/8bb23a4da3)] - **doc**: mark Node.js v17.x as EOL (KaKa) [#45110](https://github.com/nodejs/node/pull/45110)
* \[[`3f89dcabdb`](https://github.com/nodejs/node/commit/3f89dcabdb)] - **doc**: update Node.js 16 End-of-Life date (Richard Lau) [#45103](https://github.com/nodejs/node/pull/45103)
* \[[`7b23ec47b0`](https://github.com/nodejs/node/commit/7b23ec47b0)] - **doc**: fix typo in parseArgs default value (Tobias Nießen) [#45083](https://github.com/nodejs/node/pull/45083)
* \[[`7da66ef9e7`](https://github.com/nodejs/node/commit/7da66ef9e7)] - **doc**: updated security stewards (Michael Dawson) [#45005](https://github.com/nodejs/node/pull/45005)
* \[[`e5f9a520e2`](https://github.com/nodejs/node/commit/e5f9a520e2)] - **doc**: fix http and http2 writeEarlyHints() parameter (Fabian Meyer) [#45000](https://github.com/nodejs/node/pull/45000)
* \[[`e41a39c2b9`](https://github.com/nodejs/node/commit/e41a39c2b9)] - **doc**: run license-builder (github-actions\[bot]) [#45034](https://github.com/nodejs/node/pull/45034)
* \[[`875de23ee9`](https://github.com/nodejs/node/commit/875de23ee9)] - **doc**: improve the workflow to test release binaries (Rafael Gonzaga) [#45004](https://github.com/nodejs/node/pull/45004)
* \[[`d7fc12f647`](https://github.com/nodejs/node/commit/d7fc12f647)] - **doc**: fix undici version in changelog (Michael Dawson) [#44982](https://github.com/nodejs/node/pull/44982)
* \[[`4494cb2e82`](https://github.com/nodejs/node/commit/4494cb2e82)] - **doc**: add info on fixup to security release process (Michael Dawson) [#44807](https://github.com/nodejs/node/pull/44807)
* \[[`88351b9758`](https://github.com/nodejs/node/commit/88351b9758)] - **doc**: add anonrig to collaborators (Yagiz Nizipli) [#45002](https://github.com/nodejs/node/pull/45002)
* \[[`aebf7453d7`](https://github.com/nodejs/node/commit/aebf7453d7)] - **doc**: add notable changes to latest v18.x release changelog (Danielle Adams) [#44996](https://github.com/nodejs/node/pull/44996)
* \[[`b4cc30e18c`](https://github.com/nodejs/node/commit/b4cc30e18c)] - **doc**: deprecate url.parse() (Rich Trott) [#44919](https://github.com/nodejs/node/pull/44919)
* \[[`4fcbc92788`](https://github.com/nodejs/node/commit/4fcbc92788)] - **doc**: fix backticks in fs API docs (Livia Medeiros) [#44962](https://github.com/nodejs/node/pull/44962)
* \[[`cc1f41a57e`](https://github.com/nodejs/node/commit/cc1f41a57e)] - **doc, async\_hooks**: improve and add migration hints (Gerhard Stöbich) [#45369](https://github.com/nodejs/node/pull/45369)
* \[[`6fb74a995d`](https://github.com/nodejs/node/commit/6fb74a995d)] - **doc, http**: add Uint8Array as allowed type (Gerhard Stöbich) [#45167](https://github.com/nodejs/node/pull/45167)
* \[[`066993a10a`](https://github.com/nodejs/node/commit/066993a10a)] - **esm**: add JSDoc property descriptions for loader (Rich Trott) [#45370](https://github.com/nodejs/node/pull/45370)
* \[[`fa210f91df`](https://github.com/nodejs/node/commit/fa210f91df)] - **esm**: add JSDoc property descriptions for fetch (Rich Trott) [#45370](https://github.com/nodejs/node/pull/45370)
* \[[`2f27d058c2`](https://github.com/nodejs/node/commit/2f27d058c2)] - **esm**: protect ESM loader from prototype pollution (Antoine du Hamel) [#45175](https://github.com/nodejs/node/pull/45175)
* \[[`46ded6b96e`](https://github.com/nodejs/node/commit/46ded6b96e)] - **esm**: protect ESM loader from prototype pollution (Antoine du Hamel) [#45044](https://github.com/nodejs/node/pull/45044)
* \[[`3bb764a58a`](https://github.com/nodejs/node/commit/3bb764a58a)] - **events**: add unique events benchmark (Yagiz Nizipli) [#44657](https://github.com/nodejs/node/pull/44657)
* \[[`b305ad46fd`](https://github.com/nodejs/node/commit/b305ad46fd)] - **fs**: fix fs.rm support for loop symlinks (Nathanael Ruf) [#45439](https://github.com/nodejs/node/pull/45439)
* \[[`c2f0377b8f`](https://github.com/nodejs/node/commit/c2f0377b8f)] - **fs**: update todo message (Yagiz Nizipli) [#45265](https://github.com/nodejs/node/pull/45265)
* \[[`1db20c84e1`](https://github.com/nodejs/node/commit/1db20c84e1)] - **fs**: fix opts.filter issue in cpSync (Tho) [#45143](https://github.com/nodejs/node/pull/45143)
* \[[`da302ce15b`](https://github.com/nodejs/node/commit/da302ce15b)] - **fs**: trace more fs api (theanarkh) [#45095](https://github.com/nodejs/node/pull/45095)
* \[[`9ab00f5fbd`](https://github.com/nodejs/node/commit/9ab00f5fbd)] - **gyp**: fix v8 canary build on aix (Vasili Skurydzin) [#45496](https://github.com/nodejs/node/pull/45496)
* \[[`cbd710bbf4`](https://github.com/nodejs/node/commit/cbd710bbf4)] - **http**: make `OutgoingMessage` more streamlike (Robert Nagy) [#45672](https://github.com/nodejs/node/pull/45672)
* \[[`209e7e3cff`](https://github.com/nodejs/node/commit/209e7e3cff)] - **http**: add debug log for ERR\_UNESCAPED\_CHARACTERS (Aidan Temple) [#45420](https://github.com/nodejs/node/pull/45420)
* \[[`3937118f5e`](https://github.com/nodejs/node/commit/3937118f5e)] - **http**: add JSDoc property descriptions (Rich Trott) [#45370](https://github.com/nodejs/node/pull/45370)
* \[[`f222c95209`](https://github.com/nodejs/node/commit/f222c95209)] - **http**: add priority to common http headers (James M Snell) [#45045](https://github.com/nodejs/node/pull/45045)
* \[[`2882e6042e`](https://github.com/nodejs/node/commit/2882e6042e)] - _**Revert**_ "**http**: do not leak error listeners" (Luigi Pinca) [#44921](https://github.com/nodejs/node/pull/44921)
* \[[`b45878b2f1`](https://github.com/nodejs/node/commit/b45878b2f1)] - **http2**: improve session close/destroy procedures (Santiago Gimeno) [#45115](https://github.com/nodejs/node/pull/45115)
* \[[`a534175aa5`](https://github.com/nodejs/node/commit/a534175aa5)] - **http2**: fix crash on Http2Stream::diagnostic\_name() (Santiago Gimeno) [#45123](https://github.com/nodejs/node/pull/45123)
* \[[`0b9f11bcbe`](https://github.com/nodejs/node/commit/0b9f11bcbe)] - **http2**: fix debugStream method (Santiago Gimeno) [#45129](https://github.com/nodejs/node/pull/45129)
* \[[`bbaca8442a`](https://github.com/nodejs/node/commit/bbaca8442a)] - **lib**: allow Writeable.toWeb() to work on http.Outgoing message (Debadree Chatterjee) [#45642](https://github.com/nodejs/node/pull/45642)
* \[[`1284789371`](https://github.com/nodejs/node/commit/1284789371)] - **lib**: check number of arguments in `EventTarget`'s function (Deokjin Kim) [#45668](https://github.com/nodejs/node/pull/45668)
* \[[`6297e77b1f`](https://github.com/nodejs/node/commit/6297e77b1f)] - **lib**: disambiguate `native module` to `binding` (Daeyeon Jeong) [#45673](https://github.com/nodejs/node/pull/45673)
* \[[`f7c101555a`](https://github.com/nodejs/node/commit/f7c101555a)] - **lib**: disambiguate `native module` to `builtin module` (Daeyeon Jeong) [#45673](https://github.com/nodejs/node/pull/45673)
* \[[`55f800b806`](https://github.com/nodejs/node/commit/55f800b806)] - **lib**: added SuiteContext class (Debadree Chatterjee) [#45687](https://github.com/nodejs/node/pull/45687)
* \[[`1ff8f689fa`](https://github.com/nodejs/node/commit/1ff8f689fa)] - **lib**: add missing type of removeEventListener in question (Deokjin Kim) [#45676](https://github.com/nodejs/node/pull/45676)
* \[[`2c595da5dc`](https://github.com/nodejs/node/commit/2c595da5dc)] - **lib**: do not throw if global property is no longer configurable (Antoine du Hamel) [#45344](https://github.com/nodejs/node/pull/45344)
* \[[`b9d4ac2c7e`](https://github.com/nodejs/node/commit/b9d4ac2c7e)] - **lib**: fix eslint early return (RafaelGSS) [#45409](https://github.com/nodejs/node/pull/45409)
* \[[`4ef86b59b5`](https://github.com/nodejs/node/commit/4ef86b59b5)] - **lib**: fix JSDoc issues (Rich Trott) [#45243](https://github.com/nodejs/node/pull/45243)
* \[[`9ccf8b2ccc`](https://github.com/nodejs/node/commit/9ccf8b2ccc)] - **lib**: use process.nextTick() instead of setImmediate() (Luigi Pinca) [#42340](https://github.com/nodejs/node/pull/42340)
* \[[`8616e9b58b`](https://github.com/nodejs/node/commit/8616e9b58b)] - **lib**: drop fetch experimental warning (Matteo Collina) [#45287](https://github.com/nodejs/node/pull/45287)
* \[[`57897f80cd`](https://github.com/nodejs/node/commit/57897f80cd)] - **lib**: fix TypeError when converting a detached buffer source (Kohei Ueno) [#44020](https://github.com/nodejs/node/pull/44020)
* \[[`ba0e7ae3dd`](https://github.com/nodejs/node/commit/ba0e7ae3dd)] - **lib**: fix `AbortSignal.timeout` parameter validation (dnalborczyk) [#42856](https://github.com/nodejs/node/pull/42856)
* \[[`385d795816`](https://github.com/nodejs/node/commit/385d795816)] - **lib**: fix typo in `pre_execution.js` (Antoine du Hamel) [#45039](https://github.com/nodejs/node/pull/45039)
* \[[`4ab1530b9b`](https://github.com/nodejs/node/commit/4ab1530b9b)] - **lib**: promise version of streams.finished call clean up (Naor Tedgi (Abu Emma)) [#44862](https://github.com/nodejs/node/pull/44862)
* \[[`2a3bd11edd`](https://github.com/nodejs/node/commit/2a3bd11edd)] - **lib**: make properties on Blob and URL enumerable (Khafra) [#44918](https://github.com/nodejs/node/pull/44918)
* \[[`f412834151`](https://github.com/nodejs/node/commit/f412834151)] - **lib**: support more attributes for early hint link (Yagiz Nizipli) [#44874](https://github.com/nodejs/node/pull/44874)
* \[[`1019209306`](https://github.com/nodejs/node/commit/1019209306)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45814](https://github.com/nodejs/node/pull/45814)
* \[[`dc862fe786`](https://github.com/nodejs/node/commit/dc862fe786)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45732](https://github.com/nodejs/node/pull/45732)
* \[[`dc5340018f`](https://github.com/nodejs/node/commit/dc5340018f)] - **meta**: add .mailmap entry for Stefan Stojanovic (Rich Trott) [#45703](https://github.com/nodejs/node/pull/45703)
* \[[`775f659380`](https://github.com/nodejs/node/commit/775f659380)] - **meta**: update AUTHORS info for nstepien (Nicolas Stepien) [#45692](https://github.com/nodejs/node/pull/45692)
* \[[`e2da381609`](https://github.com/nodejs/node/commit/e2da381609)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45637](https://github.com/nodejs/node/pull/45637)
* \[[`29e51e72f9`](https://github.com/nodejs/node/commit/29e51e72f9)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45531](https://github.com/nodejs/node/pull/45531)
* \[[`6f8759bc51`](https://github.com/nodejs/node/commit/6f8759bc51)] - **meta**: update VoltrexMaster's username (Mohammed Keyvanzadeh) [#45503](https://github.com/nodejs/node/pull/45503)
* \[[`404172bb7c`](https://github.com/nodejs/node/commit/404172bb7c)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45443](https://github.com/nodejs/node/pull/45443)
* \[[`221f298078`](https://github.com/nodejs/node/commit/221f298078)] - **meta**: be more proactive about removing from teams (Rich Trott) [#45352](https://github.com/nodejs/node/pull/45352)
* \[[`28b937ae38`](https://github.com/nodejs/node/commit/28b937ae38)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45333](https://github.com/nodejs/node/pull/45333)
* \[[`255e89dc8c`](https://github.com/nodejs/node/commit/255e89dc8c)] - **meta**: update collaborator email address in README (Rich Trott) [#45251](https://github.com/nodejs/node/pull/45251)
* \[[`1a726e9dff`](https://github.com/nodejs/node/commit/1a726e9dff)] - **meta**: fix email address typo in README (Rich Trott) [#45250](https://github.com/nodejs/node/pull/45250)
* \[[`ffd059328b`](https://github.com/nodejs/node/commit/ffd059328b)] - **meta**: remove dont-land-on-v12 auto labeling (Moshe Atlow) [#45233](https://github.com/nodejs/node/pull/45233)
* \[[`d7fe2eac07`](https://github.com/nodejs/node/commit/d7fe2eac07)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45238](https://github.com/nodejs/node/pull/45238)
* \[[`5aa4ea931a`](https://github.com/nodejs/node/commit/5aa4ea931a)] - **meta**: move a collaborator to emeritus (Rich Trott) [#45160](https://github.com/nodejs/node/pull/45160)
* \[[`4fe060e957`](https://github.com/nodejs/node/commit/4fe060e957)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#45036](https://github.com/nodejs/node/pull/45036)
* \[[`9ec6117b65`](https://github.com/nodejs/node/commit/9ec6117b65)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45020](https://github.com/nodejs/node/pull/45020)
* \[[`3197b913aa`](https://github.com/nodejs/node/commit/3197b913aa)] - **module**: require.resolve.paths returns null with node schema (MURAKAMI Masahiko) [#45147](https://github.com/nodejs/node/pull/45147)
* \[[`bf8d48a881`](https://github.com/nodejs/node/commit/bf8d48a881)] - **module**: ensure relative requires work from deleted directories (Bradley Farias) [#42384](https://github.com/nodejs/node/pull/42384)
* \[[`2c0f7d47e4`](https://github.com/nodejs/node/commit/2c0f7d47e4)] - **module**: fix segment deprecation for imports field (Guy Bedford) [#44883](https://github.com/nodejs/node/pull/44883)
* \[[`a1831dacbf`](https://github.com/nodejs/node/commit/a1831dacbf)] - **net**: check `autoSelectFamilyAttemptTimeout` is positive (Deokjin Kim) [#45740](https://github.com/nodejs/node/pull/45740)
* \[[`048795dab0`](https://github.com/nodejs/node/commit/048795dab0)] - **(SEMVER-MINOR)** **net**: add autoSelectFamily and autoSelectFamilyAttemptTimeout options (Paolo Insogna) [#44731](https://github.com/nodejs/node/pull/44731)
* \[[`e4f7bcff8b`](https://github.com/nodejs/node/commit/e4f7bcff8b)] - **net**: remove \_readableState from debug statement (Rich Trott) [#45063](https://github.com/nodejs/node/pull/45063)
* \[[`cd4b0626c2`](https://github.com/nodejs/node/commit/cd4b0626c2)] - **node-api**: address coverity warning (Michael Dawson) [#45563](https://github.com/nodejs/node/pull/45563)
* \[[`581b38af13`](https://github.com/nodejs/node/commit/581b38af13)] - **node-api**: declare type napi\_cleanup\_hook (Chengzhong Wu) [#45391](https://github.com/nodejs/node/pull/45391)
* \[[`44766c6522`](https://github.com/nodejs/node/commit/44766c6522)] - **node-api**: fix immediate napi\_remove\_wrap test (Chengzhong Wu) [#45406](https://github.com/nodejs/node/pull/45406)
* \[[`746175a272`](https://github.com/nodejs/node/commit/746175a272)] - **node-api**: handle no support for external buffers (Michael Dawson) [#45181](https://github.com/nodejs/node/pull/45181)
* \[[`b5811c44cb`](https://github.com/nodejs/node/commit/b5811c44cb)] - **node-api,test**: fix test\_reference\_double\_free crash (Vladimir Morozov) [#44927](https://github.com/nodejs/node/pull/44927)
* \[[`bc5140383c`](https://github.com/nodejs/node/commit/bc5140383c)] - **os**: convert uid and gid to 32-bit signed integers (Luigi Pinca) [#42340](https://github.com/nodejs/node/pull/42340)
* \[[`ff03ed1c22`](https://github.com/nodejs/node/commit/ff03ed1c22)] - **readline**: improve robustness against prototype mutation (Antoine du Hamel) [#45614](https://github.com/nodejs/node/pull/45614)
* \[[`b999983c30`](https://github.com/nodejs/node/commit/b999983c30)] - **repl**: do not define `wasi` on global with no flag (Kohei Ueno) [#45595](https://github.com/nodejs/node/pull/45595)
* \[[`a186a4d7ef`](https://github.com/nodejs/node/commit/a186a4d7ef)] - **report**: add more memory info (theanarkh) [#45254](https://github.com/nodejs/node/pull/45254)
* \[[`a880568afd`](https://github.com/nodejs/node/commit/a880568afd)] - **report**: add rss and use/kernel cpu usage fields (theanarkh) [#45043](https://github.com/nodejs/node/pull/45043)
* \[[`49da5cd0ee`](https://github.com/nodejs/node/commit/49da5cd0ee)] - **report,doc**: define report version semantics (Gireesh Punathil) [#45050](https://github.com/nodejs/node/pull/45050)
* \[[`ae61740325`](https://github.com/nodejs/node/commit/ae61740325)] - **src**: add internal isArrayBufferDetached (Yagiz Nizipli) [#45568](https://github.com/nodejs/node/pull/45568)
* \[[`a58bf148b3`](https://github.com/nodejs/node/commit/a58bf148b3)] - **(SEMVER-MINOR)** **src**: add uvwasi version (Jithil P Ponnan) [#45639](https://github.com/nodejs/node/pull/45639)
* \[[`8e1138d354`](https://github.com/nodejs/node/commit/8e1138d354)] - **src**: simplify NodeBIO::GetMethod initialization (Anna Henningsen) [#45799](https://github.com/nodejs/node/pull/45799)
* \[[`b88ee545f2`](https://github.com/nodejs/node/commit/b88ee545f2)] - **src**: make structuredClone work for process.env (Ben Noordhuis) [#45698](https://github.com/nodejs/node/pull/45698)
* \[[`94e6f08606`](https://github.com/nodejs/node/commit/94e6f08606)] - **src**: mark generated `snapshot_data` as `const` (Anna Henningsen) [#45786](https://github.com/nodejs/node/pull/45786)
* \[[`29f2dabca2`](https://github.com/nodejs/node/commit/29f2dabca2)] - **src**: cleanup on disambiguating native modules (Michael Dawson) [#45665](https://github.com/nodejs/node/pull/45665)
* \[[`ed0a867a4e`](https://github.com/nodejs/node/commit/ed0a867a4e)] - **src**: use `enum class` instead of `enum` in node\_i18n (Deokjin Kim) [#45646](https://github.com/nodejs/node/pull/45646)
* \[[`1e48a5a5b0`](https://github.com/nodejs/node/commit/1e48a5a5b0)] - **src**: address coverity warning in node\_file.cc (Michael Dawson) [#45565](https://github.com/nodejs/node/pull/45565)
* \[[`3f6f8f920f`](https://github.com/nodejs/node/commit/3f6f8f920f)] - **src**: use qualified `std::move` call in node\_http2 (Michaël Zasso) [#45555](https://github.com/nodejs/node/pull/45555)
* \[[`5e63bf3726`](https://github.com/nodejs/node/commit/5e63bf3726)] - **src**: avoid unused variables and functions (Michaël Zasso) [#45542](https://github.com/nodejs/node/pull/45542)
* \[[`3f5a23281c`](https://github.com/nodejs/node/commit/3f5a23281c)] - **src**: add missing include for `std::all_of` (Michaël Zasso) [#45541](https://github.com/nodejs/node/pull/45541)
* \[[`0328208f6c`](https://github.com/nodejs/node/commit/0328208f6c)] - **src**: set an appropriate thread pool size if given `--v8-pool-size=0` (Daeyeon Jeong) [#45513](https://github.com/nodejs/node/pull/45513)
* \[[`fbfd26da7d`](https://github.com/nodejs/node/commit/fbfd26da7d)] - **src**: move FsStatsOffset and kFsStatsBufferLength to node\_file.h (Joyee Cheung) [#45498](https://github.com/nodejs/node/pull/45498)
* \[[`2c6a4fd6fa`](https://github.com/nodejs/node/commit/2c6a4fd6fa)] - **src**: don't run tasks on isolate termination (Santiago Gimeno) [#45444](https://github.com/nodejs/node/pull/45444)
* \[[`9c39b5ec48`](https://github.com/nodejs/node/commit/9c39b5ec48)] - **src**: remove the unused PackageConfig class (Joyee Cheung) [#45478](https://github.com/nodejs/node/pull/45478)
* \[[`70ca26a858`](https://github.com/nodejs/node/commit/70ca26a858)] - **src**: add --max-semi-space-size to the options allowed in NODE\_OPTIONS (Emanuel Hoogeveen) [#44436](https://github.com/nodejs/node/pull/44436)
* \[[`5544ce4bdf`](https://github.com/nodejs/node/commit/5544ce4bdf)] - **src**: condense experimental warning message (Rich Trott) [#45424](https://github.com/nodejs/node/pull/45424)
* \[[`9ac7df1c6a`](https://github.com/nodejs/node/commit/9ac7df1c6a)] - **src**: track contexts in the Environment instead of AsyncHooks (Joyee Cheung) [#45282](https://github.com/nodejs/node/pull/45282)
* \[[`89b3336120`](https://github.com/nodejs/node/commit/89b3336120)] - **src**: resolve TODO related to inspector CVEs (Tobias Nießen) [#45341](https://github.com/nodejs/node/pull/45341)
* \[[`e05ebe8f9b`](https://github.com/nodejs/node/commit/e05ebe8f9b)] - **src**: print nghttp2 logs when using --debug-nghttp2 (Santiago Gimeno) [#45209](https://github.com/nodejs/node/pull/45209)
* \[[`4634aa987d`](https://github.com/nodejs/node/commit/4634aa987d)] - **src**: trace threadpool event (theanarkh) [#44458](https://github.com/nodejs/node/pull/44458)
* \[[`bf028a66ef`](https://github.com/nodejs/node/commit/bf028a66ef)] - **src**: lock-free init\_process\_flags (Jérémy Lal) [#45221](https://github.com/nodejs/node/pull/45221)
* \[[`8c4ac6dcf5`](https://github.com/nodejs/node/commit/8c4ac6dcf5)] - **src**: call uv\_library\_shutdown before DisposePlatform (theanarkh) [#45226](https://github.com/nodejs/node/pull/45226)
* \[[`614d646767`](https://github.com/nodejs/node/commit/614d646767)] - **src**: fix `crypto.privateEncrypt` fails first time (liuxingbaoyu) [#42793](https://github.com/nodejs/node/pull/42793)
* \[[`dee882e94f`](https://github.com/nodejs/node/commit/dee882e94f)] - **src**: let http2 streams end after session close (Santiago Gimeno) [#45153](https://github.com/nodejs/node/pull/45153)
* \[[`325254cc2c`](https://github.com/nodejs/node/commit/325254cc2c)] - **src**: remap invalid file descriptors using `dup2` (Obiwac) [#44461](https://github.com/nodejs/node/pull/44461)
* \[[`f748f5df56`](https://github.com/nodejs/node/commit/f748f5df56)] - **src**: remove unused `contextify_global_private_symbol` (Daeyeon Jeong) [#45128](https://github.com/nodejs/node/pull/45128)
* \[[`169b33a24c`](https://github.com/nodejs/node/commit/169b33a24c)] - **src**: forbid running watch mode in REPL (Moshe Atlow) [#45058](https://github.com/nodejs/node/pull/45058)
* \[[`57b7023257`](https://github.com/nodejs/node/commit/57b7023257)] - **src**: fix test runner coverage (Moshe Atlow) [#45055](https://github.com/nodejs/node/pull/45055)
* \[[`bf17f8dcb8`](https://github.com/nodejs/node/commit/bf17f8dcb8)] - **src**: optimize ALPN callback (Ben Noordhuis) [#44875](https://github.com/nodejs/node/pull/44875)
* \[[`d433d34765`](https://github.com/nodejs/node/commit/d433d34765)] - **src**: simplify ALPN code, remove indirection (Ben Noordhuis) [#44875](https://github.com/nodejs/node/pull/44875)
* \[[`33c78e2340`](https://github.com/nodejs/node/commit/33c78e2340)] - **src**: iwyu in cleanup\_queue.cc (Shelley Vohr) [#44983](https://github.com/nodejs/node/pull/44983)
* \[[`23aa41394f`](https://github.com/nodejs/node/commit/23aa41394f)] - **(SEMVER-MINOR)** **src**: add support for externally shared js builtins (Michael Dawson) [#44376](https://github.com/nodejs/node/pull/44376)
* \[[`2d2e71c189`](https://github.com/nodejs/node/commit/2d2e71c189)] - **src**: refactor BaseObject methods (Joyee Cheung) [#44796](https://github.com/nodejs/node/pull/44796)
* \[[`7b992cc229`](https://github.com/nodejs/node/commit/7b992cc229)] - **src**: create BaseObject with node::Realm (Chengzhong Wu) [#44348](https://github.com/nodejs/node/pull/44348)
* \[[`a7f3bc0dbc`](https://github.com/nodejs/node/commit/a7f3bc0dbc)] - **src**: introduce node::Realm (Chengzhong Wu) [#44179](https://github.com/nodejs/node/pull/44179)
* \[[`b11616be6b`](https://github.com/nodejs/node/commit/b11616be6b)] - **src**: support WeakReference in snapshot (Joyee Cheung) [#44193](https://github.com/nodejs/node/pull/44193)
* \[[`1ca575501a`](https://github.com/nodejs/node/commit/1ca575501a)] - **src**: iterate over base objects to prepare for snapshot (Joyee Cheung) [#44192](https://github.com/nodejs/node/pull/44192)
* \[[`f071028a45`](https://github.com/nodejs/node/commit/f071028a45)] - **src**: fix cppgc incompatibility in v8 (Shelley Vohr) [#43521](https://github.com/nodejs/node/pull/43521)
* \[[`b8290ff7e9`](https://github.com/nodejs/node/commit/b8290ff7e9)] - **(SEMVER-MINOR)** **src**: add initial shadow realm support (Chengzhong Wu) [#42869](https://github.com/nodejs/node/pull/42869)
* \[[`90e8418e58`](https://github.com/nodejs/node/commit/90e8418e58)] - **src,lib**: group properties used as constants from `util` binding (Daeyeon Jeong) [#45539](https://github.com/nodejs/node/pull/45539)
* \[[`12779b3e02`](https://github.com/nodejs/node/commit/12779b3e02)] - **src,lib**: retrieve parsed source map url from v8 (Chengzhong Wu) [#44798](https://github.com/nodejs/node/pull/44798)
* \[[`c5630e2699`](https://github.com/nodejs/node/commit/c5630e2699)] - **src,node-api**: update `napi_is_detached_arraybuffer` (Daeyeon Jeong) [#45538](https://github.com/nodejs/node/pull/45538)
* \[[`122c51b595`](https://github.com/nodejs/node/commit/122c51b595)] - **stream**: use structuredClone instead of v8 (Yagiz Nizipli) [#45611](https://github.com/nodejs/node/pull/45611)
* \[[`2bde576822`](https://github.com/nodejs/node/commit/2bde576822)] - **stream**: use ArrayBufferPrototypeGetByteLength (Yagiz Nizipli) [#45528](https://github.com/nodejs/node/pull/45528)
* \[[`7984e066ca`](https://github.com/nodejs/node/commit/7984e066ca)] - **stream**: add primordials to adapters (Yagiz Nizipli) [#45511](https://github.com/nodejs/node/pull/45511)
* \[[`ac9a4aba5d`](https://github.com/nodejs/node/commit/ac9a4aba5d)] - **stream**: avoid premature close when will not emit close (Robert Nagy) [#45301](https://github.com/nodejs/node/pull/45301)
* \[[`6f080e2968`](https://github.com/nodejs/node/commit/6f080e2968)] - **stream**: fix typo in `adapters.js` (#45515) (Kohei Ueno) [#45515](https://github.com/nodejs/node/pull/45515)
* \[[`d2998b6166`](https://github.com/nodejs/node/commit/d2998b6166)] - **stream**: add fast path for utf8 (Yagiz Nizipli) [#45483](https://github.com/nodejs/node/pull/45483)
* \[[`5f7d2b5c64`](https://github.com/nodejs/node/commit/5f7d2b5c64)] - **stream**: add compose operator (Raz Luvaton) [#44937](https://github.com/nodejs/node/pull/44937)
* \[[`70244d0b57`](https://github.com/nodejs/node/commit/70244d0b57)] - **stream**: fix duplexify premature destroy (Robert Nagy) [#45133](https://github.com/nodejs/node/pull/45133)
* \[[`1fb6e82f84`](https://github.com/nodejs/node/commit/1fb6e82f84)] - **stream**: fix web streams have no Symbol.toStringTag (Jithil P Ponnan) [#45117](https://github.com/nodejs/node/pull/45117)
* \[[`c514751ff3`](https://github.com/nodejs/node/commit/c514751ff3)] - **stream**: don't push null from closed promise #42694 (David Halls) [#45026](https://github.com/nodejs/node/pull/45026)
* \[[`8c2d0f9294`](https://github.com/nodejs/node/commit/8c2d0f9294)] - **stream**: fix `size` function returned from QueuingStrategies (Daeyeon Jeong) [#44867](https://github.com/nodejs/node/pull/44867)
* \[[`7e386f8cb9`](https://github.com/nodejs/node/commit/7e386f8cb9)] - **test**: remove flaky parallel/test-process-wrap test (Ben Noordhuis) [#45806](https://github.com/nodejs/node/pull/45806)
* \[[`38fb2c1cc8`](https://github.com/nodejs/node/commit/38fb2c1cc8)] - **test**: fix invalid output TAP if there newline in test name (Pulkit Gupta) [#45742](https://github.com/nodejs/node/pull/45742)
* \[[`c1162535ae`](https://github.com/nodejs/node/commit/c1162535ae)] - **test**: fix -Wunused-variable on report-fatalerror (Santiago Gimeno) [#45747](https://github.com/nodejs/node/pull/45747)
* \[[`13d2df3c20`](https://github.com/nodejs/node/commit/13d2df3c20)] - **test**: fix test-watch-mode (Stefan Stojanovic) [#45585](https://github.com/nodejs/node/pull/45585)
* \[[`3a5a2ae2ea`](https://github.com/nodejs/node/commit/3a5a2ae2ea)] - **test**: fix test-watch-mode-inspect (Stefan Stojanovic) [#45586](https://github.com/nodejs/node/pull/45586)
* \[[`7dbc742bca`](https://github.com/nodejs/node/commit/7dbc742bca)] - **test**: fix typos in test/parallel (Deokjin Kim) [#45583](https://github.com/nodejs/node/pull/45583)
* \[[`3e87834ac7`](https://github.com/nodejs/node/commit/3e87834ac7)] - **test**: add trailing commas in event tests (Rich Trott) [#45466](https://github.com/nodejs/node/pull/45466)
* \[[`8f7109aea3`](https://github.com/nodejs/node/commit/8f7109aea3)] - **test**: add trailing commas in async-hooks tests (#45549) (Antoine du Hamel) [#45549](https://github.com/nodejs/node/pull/45549)
* \[[`d5b0844fce`](https://github.com/nodejs/node/commit/d5b0844fce)] - **test**: add trailing commas in addons test (#45548) (Antoine du Hamel) [#45548](https://github.com/nodejs/node/pull/45548)
* \[[`b1e17b4590`](https://github.com/nodejs/node/commit/b1e17b4590)] - **test**: add trailing commas in `test/common` (#45550) (Antoine du Hamel) [#45550](https://github.com/nodejs/node/pull/45550)
* \[[`305d4de72f`](https://github.com/nodejs/node/commit/305d4de72f)] - **test**: revise pull request guide text about code (Rich Trott) [#45519](https://github.com/nodejs/node/pull/45519)
* \[[`9f1e5b6a89`](https://github.com/nodejs/node/commit/9f1e5b6a89)] - **test**: enable the WPT for `structuredClone` (Daeyeon Jeong) [#45482](https://github.com/nodejs/node/pull/45482)
* \[[`fac1f1bcd6`](https://github.com/nodejs/node/commit/fac1f1bcd6)] - **test**: add lint rule to enforce trailing commas (Antoine du Hamel) [#45468](https://github.com/nodejs/node/pull/45468)
* \[[`cac449f0a0`](https://github.com/nodejs/node/commit/cac449f0a0)] - **test**: update uses of \_jabber.\_tcp.google.com (Colin Ihrig) [#45451](https://github.com/nodejs/node/pull/45451)
* \[[`19eabd7870`](https://github.com/nodejs/node/commit/19eabd7870)] - **test**: add test to validate changelogs for releases (Richard Lau) [#45325](https://github.com/nodejs/node/pull/45325)
* \[[`85c634d899`](https://github.com/nodejs/node/commit/85c634d899)] - **test**: remove flaky designation for test-worker-http2-stream-terminate (Rich Trott) [#45438](https://github.com/nodejs/node/pull/45438)
* \[[`4283af48b0`](https://github.com/nodejs/node/commit/4283af48b0)] - **test**: fix flaky test-repl-sigint-nested-eval (Rich Trott) [#45354](https://github.com/nodejs/node/pull/45354)
* \[[`7e0332a46b`](https://github.com/nodejs/node/commit/7e0332a46b)] - **test**: skip test-fs-largefile if not enough disk space (Rich Trott) [#45339](https://github.com/nodejs/node/pull/45339)
* \[[`ad3b41c858`](https://github.com/nodejs/node/commit/ad3b41c858)] - **test**: fix catching failed assertion (Pavel Horal) [#45222](https://github.com/nodejs/node/pull/45222)
* \[[`12764fc725`](https://github.com/nodejs/node/commit/12764fc725)] - **test**: defer invocation checks (Luigi Pinca) [#42340](https://github.com/nodejs/node/pull/42340)
* \[[`facb606084`](https://github.com/nodejs/node/commit/facb606084)] - **test**: fix test-socket-write-after-fin-error (Luigi Pinca) [#42340](https://github.com/nodejs/node/pull/42340)
* \[[`ab9c2df11b`](https://github.com/nodejs/node/commit/ab9c2df11b)] - **test**: make `test-eventemitter-asyncresource.js` shorter (Juan José) [#45146](https://github.com/nodejs/node/pull/45146)
* \[[`03a3f30cb1`](https://github.com/nodejs/node/commit/03a3f30cb1)] - **test**: convert test-debugger-pid to async/await (Luke Karrys) [#45179](https://github.com/nodejs/node/pull/45179)
* \[[`d718c675d8`](https://github.com/nodejs/node/commit/d718c675d8)] - **test**: improve test coverage in `test-event-capture-rejections.js` (Juan José) [#45148](https://github.com/nodejs/node/pull/45148)
* \[[`fe91ac15be`](https://github.com/nodejs/node/commit/fe91ac15be)] - **test**: fix timeout of test-heap-prof.js in riscv devices (Yu Gu) [#42674](https://github.com/nodejs/node/pull/42674)
* \[[`a110cac4f3`](https://github.com/nodejs/node/commit/a110cac4f3)] - **test**: deflake test-http2-empty-frame-without-eof (Santiago Gimeno) [#45212](https://github.com/nodejs/node/pull/45212)
* \[[`c890136934`](https://github.com/nodejs/node/commit/c890136934)] - **test**: use common/tmpdir in watch-mode ipc test (Richard Lau) [#45211](https://github.com/nodejs/node/pull/45211)
* \[[`7b8380dd73`](https://github.com/nodejs/node/commit/7b8380dd73)] - **test**: use uv\_sleep() where possible (Santiago Gimeno) [#45124](https://github.com/nodejs/node/pull/45124)
* \[[`7a7dab4160`](https://github.com/nodejs/node/commit/7a7dab4160)] - **test**: fix typo in `test/parallel/test-fs-rm.js` (Tim Shilov) [#44882](https://github.com/nodejs/node/pull/44882)
* \[[`c87770b39b`](https://github.com/nodejs/node/commit/c87770b39b)] - **test**: remove a snapshot blob from test-inspect-address-in-use.js (Daeyeon Jeong) [#45132](https://github.com/nodejs/node/pull/45132)
* \[[`a0f8d8b806`](https://github.com/nodejs/node/commit/a0f8d8b806)] - **test**: add test for Module.\_stat (Darshan Sen) [#44713](https://github.com/nodejs/node/pull/44713)
* \[[`a8030b9517`](https://github.com/nodejs/node/commit/a8030b9517)] - **test**: watch mode inspect restart repeatedly (Moshe Atlow) [#45060](https://github.com/nodejs/node/pull/45060)
* \[[`52808d7d71`](https://github.com/nodejs/node/commit/52808d7d71)] - **test**: remove experimental-wasm-threads flag (Michaël Zasso) [#45074](https://github.com/nodejs/node/pull/45074)
* \[[`b3c4265a95`](https://github.com/nodejs/node/commit/b3c4265a95)] - **test**: remove unnecessary noop function args to `mustCall()` (Antoine du Hamel) [#45047](https://github.com/nodejs/node/pull/45047)
* \[[`868b4a3d67`](https://github.com/nodejs/node/commit/868b4a3d67)] - **test**: mark test-watch-mode\* as flaky on all platforms (Pierrick Bouvier) [#45049](https://github.com/nodejs/node/pull/45049)
* \[[`0d9ccc850e`](https://github.com/nodejs/node/commit/0d9ccc850e)] - **test**: wrap missing `common.mustCall` (Moshe Atlow) [#45064](https://github.com/nodejs/node/pull/45064)
* \[[`8fffd05b85`](https://github.com/nodejs/node/commit/8fffd05b85)] - **test**: remove mentions of `--experimental-async-stack-tagging-api` flag (Simon) [#45051](https://github.com/nodejs/node/pull/45051)
* \[[`fc58b61e82`](https://github.com/nodejs/node/commit/fc58b61e82)] - **test**: improve assertions in `test-repl-unsupported-option.js` (Juan José) [#44953](https://github.com/nodejs/node/pull/44953)
* \[[`e5928abef7`](https://github.com/nodejs/node/commit/e5928abef7)] - **test**: remove unnecessary noop function args to mustCall() (Rich Trott) [#45027](https://github.com/nodejs/node/pull/45027)
* \[[`d86fcbfd6e`](https://github.com/nodejs/node/commit/d86fcbfd6e)] - **test**: update WPT resources (Khaidi Chu) [#44948](https://github.com/nodejs/node/pull/44948)
* \[[`d103d779d3`](https://github.com/nodejs/node/commit/d103d779d3)] - **test**: skip test depending on `overlapped-checker` when not available (Antoine du Hamel) [#45015](https://github.com/nodejs/node/pull/45015)
* \[[`5544c55600`](https://github.com/nodejs/node/commit/5544c55600)] - **test**: improve test coverage for `os` package (Juan José) [#44959](https://github.com/nodejs/node/pull/44959)
* \[[`6e3227b0fd`](https://github.com/nodejs/node/commit/6e3227b0fd)] - **test**: add test to improve coverage in http2-compat-serverresponse (Cesar Mario Diaz) [#44970](https://github.com/nodejs/node/pull/44970)
* \[[`dfc2419ab4`](https://github.com/nodejs/node/commit/dfc2419ab4)] - **test**: improve test coverage in `test-child-process-spawn-argv0.js` (Juan José) [#44955](https://github.com/nodejs/node/pull/44955)
* \[[`89a1c57436`](https://github.com/nodejs/node/commit/89a1c57436)] - **test**: use CHECK instead of EXPECT where necessary (Tobias Nießen) [#44795](https://github.com/nodejs/node/pull/44795)
* \[[`657756fc06`](https://github.com/nodejs/node/commit/657756fc06)] - **test**: refactor promises to async/await (Madhuri) [#44980](https://github.com/nodejs/node/pull/44980)
* \[[`55cf4b5042`](https://github.com/nodejs/node/commit/55cf4b5042)] - **test**: fix textdecoder test for small-icu builds (Richard Lau) [#45225](https://github.com/nodejs/node/pull/45225)
* \[[`e2df332ea7`](https://github.com/nodejs/node/commit/e2df332ea7)] - **test**: add a test to ensure the correctness of timezone upgrades (Darshan Sen) [#45299](https://github.com/nodejs/node/pull/45299)
* \[[`a2e5126227`](https://github.com/nodejs/node/commit/a2e5126227)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#45569](https://github.com/nodejs/node/pull/45569)
* \[[`69c1f2a021`](https://github.com/nodejs/node/commit/69c1f2a021)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#45165](https://github.com/nodejs/node/pull/45165)
* \[[`24e2a4fbd7`](https://github.com/nodejs/node/commit/24e2a4fbd7)] - **test\_runner**: refactor `tap_lexer` to use more primordials (Antoine du Hamel) [#45744](https://github.com/nodejs/node/pull/45744)
* \[[`3cee3ca5ae`](https://github.com/nodejs/node/commit/3cee3ca5ae)] - **test\_runner**: refactor `tap_parser` to use more primordials (Antoine du Hamel) [#45745](https://github.com/nodejs/node/pull/45745)
* \[[`449326639d`](https://github.com/nodejs/node/commit/449326639d)] - **(SEMVER-MINOR)** **test\_runner**: add t.after() hook (Colin Ihrig) [#45792](https://github.com/nodejs/node/pull/45792)
* \[[`370ad455d6`](https://github.com/nodejs/node/commit/370ad455d6)] - **(SEMVER-MINOR)** **test\_runner**: don't use a symbol for runHook() (Colin Ihrig) [#45792](https://github.com/nodejs/node/pull/45792)
* \[[`500024384a`](https://github.com/nodejs/node/commit/500024384a)] - **test\_runner**: add resetCalls to MockFunctionContext (MURAKAMI Masahiko) [#45710](https://github.com/nodejs/node/pull/45710)
* \[[`ed9246f6fc`](https://github.com/nodejs/node/commit/ed9246f6fc)] - **test\_runner**: don't parse TAP from stderr (Colin Ihrig) [#45618](https://github.com/nodejs/node/pull/45618)
* \[[`a56560f6fc`](https://github.com/nodejs/node/commit/a56560f6fc)] - **test\_runner**: add getter and setter to MockTracker (MURAKAMI Masahiko) [#45506](https://github.com/nodejs/node/pull/45506)
* \[[`20f6fa3edd`](https://github.com/nodejs/node/commit/20f6fa3edd)] - **test\_runner**: remove stdout and stderr from error (Colin Ihrig) [#45592](https://github.com/nodejs/node/pull/45592)
* \[[`16bedbabce`](https://github.com/nodejs/node/commit/16bedbabce)] - **test\_runner**: add initial TAP parser (Wassim Chegham) [#43525](https://github.com/nodejs/node/pull/43525)
* \[[`55b64e0b14`](https://github.com/nodejs/node/commit/55b64e0b14)] - **test\_runner**: support watch mode (Moshe Atlow) [#45214](https://github.com/nodejs/node/pull/45214)
* \[[`92909f6855`](https://github.com/nodejs/node/commit/92909f6855)] - **test\_runner**: support function mocking (Colin Ihrig) [#45326](https://github.com/nodejs/node/pull/45326)
* \[[`0f69b6c26d`](https://github.com/nodejs/node/commit/0f69b6c26d)] - **test\_runner**: fix afterEach not running on test failures (Jithil P Ponnan) [#45204](https://github.com/nodejs/node/pull/45204)
* \[[`a67da59313`](https://github.com/nodejs/node/commit/a67da59313)] - **test\_runner**: report tap subtest in order (Moshe Atlow) [#45220](https://github.com/nodejs/node/pull/45220)
* \[[`a59c907e14`](https://github.com/nodejs/node/commit/a59c907e14)] - **test\_runner**: call {before,after}Each() on suites (Colin Ihrig) [#45161](https://github.com/nodejs/node/pull/45161)
* \[[`64860dd01f`](https://github.com/nodejs/node/commit/64860dd01f)] - **test\_runner**: add extra fields in AssertionError YAML (Bryan English) [#44952](https://github.com/nodejs/node/pull/44952)
* \[[`979d8376bd`](https://github.com/nodejs/node/commit/979d8376bd)] - **tls**: remove trustcor root ca certificates (Ben Noordhuis) [#45776](https://github.com/nodejs/node/pull/45776)
* \[[`10cc827ebf`](https://github.com/nodejs/node/commit/10cc827ebf)] - **(SEMVER-MINOR)** **tls**: add "ca" property to certificate object (Ben Noordhuis) [#44935](https://github.com/nodejs/node/pull/44935)
* \[[`8336e32713`](https://github.com/nodejs/node/commit/8336e32713)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#45730](https://github.com/nodejs/node/pull/45730)
* \[[`aa57f6c362`](https://github.com/nodejs/node/commit/aa57f6c362)] - **tools**: add GitHub token permissions to label flaky-test issues (Gabriela Gutierrez) [#45308](https://github.com/nodejs/node/pull/45308)
* \[[`79b3ee14a5`](https://github.com/nodejs/node/commit/79b3ee14a5)] - **tools**: remove dependency vulnerability checker (Facundo Tuesca) [#45675](https://github.com/nodejs/node/pull/45675)
* \[[`496a528149`](https://github.com/nodejs/node/commit/496a528149)] - **tools**: update lint-md-dependencies to rollup\@3.4.0 (Node.js GitHub Bot) [#45638](https://github.com/nodejs/node/pull/45638)
* \[[`346132926d`](https://github.com/nodejs/node/commit/346132926d)] - **tools**: update doc to highlight.js\@11.7.0 (Node.js GitHub Bot) [#45636](https://github.com/nodejs/node/pull/45636)
* \[[`610341f241`](https://github.com/nodejs/node/commit/610341f241)] - **tools**: update eslint to 8.28.0 (Node.js GitHub Bot) [#45532](https://github.com/nodejs/node/pull/45532)
* \[[`682a730131`](https://github.com/nodejs/node/commit/682a730131)] - **tools**: add automation for updating libuv dependency (Facundo Tuesca) [#45362](https://github.com/nodejs/node/pull/45362)
* \[[`360c7be315`](https://github.com/nodejs/node/commit/360c7be315)] - **tools**: add missing step in update-base64.sh script (Facundo Tuesca) [#45509](https://github.com/nodejs/node/pull/45509)
* \[[`e52f0472bb`](https://github.com/nodejs/node/commit/e52f0472bb)] - **tools**: update certdata.txt (Luigi Pinca) [#45490](https://github.com/nodejs/node/pull/45490)
* \[[`ad8c6c91b5`](https://github.com/nodejs/node/commit/ad8c6c91b5)] - **tools**: include current release in the list of released versions (Antoine du Hamel) [#45463](https://github.com/nodejs/node/pull/45463)
* \[[`7641044eff`](https://github.com/nodejs/node/commit/7641044eff)] - **tools**: update lint-md-dependencies to rollup\@3.3.0 (Node.js GitHub Bot) [#45442](https://github.com/nodejs/node/pull/45442)
* \[[`f5a4c52278`](https://github.com/nodejs/node/commit/f5a4c52278)] - **tools**: do not run CQ on non-fast-tracked PRs open for less than 2 days (Moshe Atlow) [#45407](https://github.com/nodejs/node/pull/45407)
* \[[`0f45c90533`](https://github.com/nodejs/node/commit/0f45c90533)] - **tools**: simplify .eslintrc.js (Rich Trott) [#45397](https://github.com/nodejs/node/pull/45397)
* \[[`172cbfefa8`](https://github.com/nodejs/node/commit/172cbfefa8)] - **tools**: simplify regex in ESLint config (Rich Trott) [#45399](https://github.com/nodejs/node/pull/45399)
* \[[`456f048e0e`](https://github.com/nodejs/node/commit/456f048e0e)] - **tools**: enable jsdoc/require-property-description rule (Rich Trott) [#45370](https://github.com/nodejs/node/pull/45370)
* \[[`6fafec351a`](https://github.com/nodejs/node/commit/6fafec351a)] - **tools**: dynamically determine parallelism on GitHub Actions macOS (Rich Trott) [#45350](https://github.com/nodejs/node/pull/45350)
* \[[`2ca30cacee`](https://github.com/nodejs/node/commit/2ca30cacee)] - **tools**: add automation for updating acorn dependency (Facundo Tuesca) [#45357](https://github.com/nodejs/node/pull/45357)
* \[[`04f213b44c`](https://github.com/nodejs/node/commit/04f213b44c)] - **tools**: add documentation regarding our api tooling (Claudio Wunder) [#45270](https://github.com/nodejs/node/pull/45270)
* \[[`c63d825681`](https://github.com/nodejs/node/commit/c63d825681)] - **tools**: allow scripts to run from anywhere (Luigi Pinca) [#45361](https://github.com/nodejs/node/pull/45361)
* \[[`1b0b68037a`](https://github.com/nodejs/node/commit/1b0b68037a)] - **tools**: update eslint to 8.27.0 (Node.js GitHub Bot) [#45358](https://github.com/nodejs/node/pull/45358)
* \[[`398ca2496b`](https://github.com/nodejs/node/commit/398ca2496b)] - **tools**: update eslint to 8.26.0 (Node.js GitHub Bot) [#45243](https://github.com/nodejs/node/pull/45243)
* \[[`3053c65fb1`](https://github.com/nodejs/node/commit/3053c65fb1)] - **tools**: update lint-md-dependencies to rollup\@3.2.5 (Node.js GitHub Bot) [#45332](https://github.com/nodejs/node/pull/45332)
* \[[`510506027b`](https://github.com/nodejs/node/commit/510506027b)] - **tools**: fix stability index generation (Antoine du Hamel) [#45346](https://github.com/nodejs/node/pull/45346)
* \[[`e0a8effbc1`](https://github.com/nodejs/node/commit/e0a8effbc1)] - **tools**: increase macOS cores to 3 on GitHub CI (Rich Trott) [#45340](https://github.com/nodejs/node/pull/45340)
* \[[`75a146f436`](https://github.com/nodejs/node/commit/75a146f436)] - **tools**: add automation for updating base64 dependency (Facundo Tuesca) [#45300](https://github.com/nodejs/node/pull/45300)
* \[[`13390e94af`](https://github.com/nodejs/node/commit/13390e94af)] - **tools**: fix `request-ci-failed` comment (Antoine du Hamel) [#45291](https://github.com/nodejs/node/pull/45291)
* \[[`a93c4f7e41`](https://github.com/nodejs/node/commit/a93c4f7e41)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#45237](https://github.com/nodejs/node/pull/45237)
* \[[`078be3ba21`](https://github.com/nodejs/node/commit/078be3ba21)] - **tools**: fix `request-ci-failed` comment (Antoine du Hamel) [#45218](https://github.com/nodejs/node/pull/45218)
* \[[`62c233c221`](https://github.com/nodejs/node/commit/62c233c221)] - **tools**: keep Emeriti lists case-insensitive alphabetic (Rich Trott) [#45159](https://github.com/nodejs/node/pull/45159)
* \[[`d07a5e25b0`](https://github.com/nodejs/node/commit/d07a5e25b0)] - **tools**: update actions/setup-python to v4 (Yagiz Nizipli) [#45178](https://github.com/nodejs/node/pull/45178)
* \[[`48cf8905f7`](https://github.com/nodejs/node/commit/48cf8905f7)] - **tools**: update V8 gypfiles for RISC-V (Andreas Schwab) [#45149](https://github.com/nodejs/node/pull/45149)
* \[[`0cbd2c6f8d`](https://github.com/nodejs/node/commit/0cbd2c6f8d)] - **tools**: fix `create-or-update-pull-request-action` hash on GHA (Antoine du Hamel) [#45166](https://github.com/nodejs/node/pull/45166)
* \[[`cd911d72ab`](https://github.com/nodejs/node/commit/cd911d72ab)] - **tools**: update gr2m/create-or-update-pull-request-action (Luigi Pinca) [#45022](https://github.com/nodejs/node/pull/45022)
* \[[`ee78e2e0ec`](https://github.com/nodejs/node/commit/ee78e2e0ec)] - **tools**: do not use the set-output command in workflows (Luigi Pinca) [#45024](https://github.com/nodejs/node/pull/45024)
* \[[`f98a6967e6`](https://github.com/nodejs/node/commit/f98a6967e6)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#45019](https://github.com/nodejs/node/pull/45019)
* \[[`71433f3127`](https://github.com/nodejs/node/commit/71433f3127)] - **tools**: refactor dynamic strings creation in shell scripts (Antoine du Hamel) [#45240](https://github.com/nodejs/node/pull/45240)
* \[[`f08c129144`](https://github.com/nodejs/node/commit/f08c129144)] - **tools**: use Python 3.11 in GitHub Actions workflows (Luigi Pinca) [#45191](https://github.com/nodejs/node/pull/45191)
* \[[`76897430ed`](https://github.com/nodejs/node/commit/76897430ed)] - **tools**: have test-asan use ubuntu-20.04 (Filip Skokan) [#45581](https://github.com/nodejs/node/pull/45581)
* \[[`210cbcbf89`](https://github.com/nodejs/node/commit/210cbcbf89)] - **trace\_events**: add new categories (theanarkh) [#45266](https://github.com/nodejs/node/pull/45266)
* \[[`20a39a34e0`](https://github.com/nodejs/node/commit/20a39a34e0)] - **trace\_events**: fix getCategories (theanarkh) [#45092](https://github.com/nodejs/node/pull/45092)
* \[[`597c7fcbab`](https://github.com/nodejs/node/commit/597c7fcbab)] - **url**: remove unnecessary object call to kFormat (Yagiz Nizipli) [#45492](https://github.com/nodejs/node/pull/45492)
* \[[`5d964cd4d4`](https://github.com/nodejs/node/commit/5d964cd4d4)] - **url**: remove \t \n \r in url.parse() similar to WHATWG (Rich Trott) [#45116](https://github.com/nodejs/node/pull/45116)
* \[[`eb81b6f0fb`](https://github.com/nodejs/node/commit/eb81b6f0fb)] - **url**: improve url.parse() compliance with WHATWG URL (Rich Trott) [#45011](https://github.com/nodejs/node/pull/45011)
* \[[`3f18a833ff`](https://github.com/nodejs/node/commit/3f18a833ff)] - **util**: use private symbols in JS land directly (Joyee Cheung) [#45379](https://github.com/nodejs/node/pull/45379)
* \[[`32a3859a90`](https://github.com/nodejs/node/commit/32a3859a90)] - **util**: add fast path for utf8 encoding (Yagiz Nizipli) [#45412](https://github.com/nodejs/node/pull/45412)
* \[[`cd740d0b0d`](https://github.com/nodejs/node/commit/cd740d0b0d)] - **util**: improve text decoder performance (Yagiz Nizipli) [#45388](https://github.com/nodejs/node/pull/45388)
* \[[`1dc91abddf`](https://github.com/nodejs/node/commit/1dc91abddf)] - **util**: improve text-decoder performance (Yagiz Nizipli) [#45363](https://github.com/nodejs/node/pull/45363)
* \[[`4730850972`](https://github.com/nodejs/node/commit/4730850972)] - **util**: improve textdecoder decode performance (Yagiz Nizipli) [#45294](https://github.com/nodejs/node/pull/45294)
* \[[`717b604da7`](https://github.com/nodejs/node/commit/717b604da7)] - **(SEMVER-MINOR)** **util**: add MIME utilities (#21128) (Bradley Farias) [#21128](https://github.com/nodejs/node/pull/21128)
* \[[`39cf8b4f16`](https://github.com/nodejs/node/commit/39cf8b4f16)] - **vm**: make ContextifyContext a BaseObject (Joyee Cheung) [#44796](https://github.com/nodejs/node/pull/44796)
* \[[`09ea75823c`](https://github.com/nodejs/node/commit/09ea75823c)] - **watch**: add CLI flag to preserve output (Debadree Chatterjee) [#45717](https://github.com/nodejs/node/pull/45717)
* \[[`24bfe543c5`](https://github.com/nodejs/node/commit/24bfe543c5)] - **watch**: watch for missing dependencies (Moshe Atlow) [#45348](https://github.com/nodejs/node/pull/45348)

<a id="18.12.1"></a>

## 2022-11-03, Version 18.12.1 'Hydrogen' (LTS), @juanarbol

This is a security release.

### Notable changes

The following CVEs are fixed in this release:

* **[CVE-2022-3602](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-3602)**: X.509 Email Address 4-byte Buffer Overflow (High)
* **[CVE-2022-3786](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-3786)**: X.509 Email Address Variable Length Buffer Overflow (High)
* **[CVE-2022-43548](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-43548)**: DNS rebinding in --inspect via invalid octal IP address (Medium)

More detailed information on each of the vulnerabilities can be found in [November 2022 Security Releases](https://nodejs.org/en/blog/vulnerability/november-2022-security-releases/) blog post.

### Commits

* \[[`39f8a672e3`](https://github.com/nodejs/node/commit/39f8a672e3)] - **deps**: update archs files for quictls/openssl-3.0.7+quic [nodejs/node#45286](https://github.com/nodejs/node/pull/45286)
* \[[`80218127c8`](https://github.com/nodejs/node/commit/80218127c8)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.7+quic [nodejs/node#45286](https://github.com/nodejs/node/pull/45286)
* \[[`165342beac`](https://github.com/nodejs/node/commit/165342beac)] - **inspector**: harden IP address validation again (Tobias Nießen) [nodejs-private/node-private#354](https://github.com/nodejs-private/node-private/pull/354)

<a id="18.12.0"></a>

## 2022-10-25, Version 18.12.0 'Hydrogen' (LTS), @ruyadorno and @RafaelGSS

### Notable Changes

This release marks the transition of Node.js 18.x into Long Term Support (LTS)
with the codename 'Hydrogen'. The 18.x release line now moves into "Active LTS"
and will remain so until October 2023. After that time, it will move into
"Maintenance" until end of life in April 2025.

<a id="18.11.0"></a>

## 2022-10-13, Version 18.11.0 (Current), @danielleadams

### Notable changes

#### watch mode (experimental)

Running in 'watch' mode using `node --watch` restarts the process when an imported file is changed.

Contributed by Moshe Atlow in [#44366](https://github.com/nodejs/node/pull/44366)

#### Other notable changes

* **fs**:
  * (SEMVER-MINOR) add `FileHandle.prototype.readLines` (Antoine du Hamel) [#42590](https://github.com/nodejs/node/pull/42590)
* **http**:
  * (SEMVER-MINOR) add writeEarlyHints function to ServerResponse (Wing) [#44180](https://github.com/nodejs/node/pull/44180)
* **http2**:
  * (SEMVER-MINOR) make early hints generic (Yagiz Nizipli) [#44820](https://github.com/nodejs/node/pull/44820)
* **lib**:
  * (SEMVER-MINOR) refactor transferable AbortSignal (flakey5) [#44048](https://github.com/nodejs/node/pull/44048)
* **src**:
  * (SEMVER-MINOR) add detailed embedder process initialization API (Anna Henningsen) [#44121](https://github.com/nodejs/node/pull/44121)
* **util**:
  * (SEMVER-MINOR) add default value option to parsearg (Manuel Spigolon) [#44631](https://github.com/nodejs/node/pull/44631)

### Commits

* \[[`27b4b782ce`](https://github.com/nodejs/node/commit/27b4b782ce)] - **benchmark**: add vm context global proxy benchmark (Joyee Cheung) [#44796](https://github.com/nodejs/node/pull/44796)
* \[[`4e82521af1`](https://github.com/nodejs/node/commit/4e82521af1)] - **bootstrap**: update comments in bootstrap/node.js (Joyee Cheung) [#44726](https://github.com/nodejs/node/pull/44726)
* \[[`725be0ea50`](https://github.com/nodejs/node/commit/725be0ea50)] - **buffer**: initialize TextDecoder once on blob.text() (Yagiz Nizipli) [#44787](https://github.com/nodejs/node/pull/44787)
* \[[`653c3b1f62`](https://github.com/nodejs/node/commit/653c3b1f62)] - **buffer,lib**: update atob to align wpt's base64.json (Khaidi Chu) [#43901](https://github.com/nodejs/node/pull/43901)
* \[[`37808b3355`](https://github.com/nodejs/node/commit/37808b3355)] - **build**: convert V8 test JSON to JUnit XML (Keyhan Vakil) [#44049](https://github.com/nodejs/node/pull/44049)
* \[[`f92871a52b`](https://github.com/nodejs/node/commit/f92871a52b)] - **build**: update timezone-update.yml (Alex) [#44717](https://github.com/nodejs/node/pull/44717)
* \[[`f85d3471ee`](https://github.com/nodejs/node/commit/f85d3471ee)] - **child\_process**: remove lookup of undefined property (Colin Ihrig) [#44766](https://github.com/nodejs/node/pull/44766)
* \[[`2f5f41c315`](https://github.com/nodejs/node/commit/2f5f41c315)] - **(SEMVER-MINOR)** **cli**: add `--watch` (Moshe Atlow) [#44366](https://github.com/nodejs/node/pull/44366)
* \[[`7fb9cc70f3`](https://github.com/nodejs/node/commit/7fb9cc70f3)] - **cluster**: use inspector utils (Moshe Atlow) [#44592](https://github.com/nodejs/node/pull/44592)
* \[[`99a2c16040`](https://github.com/nodejs/node/commit/99a2c16040)] - **crypto**: add causes to applicable webcrypto's OperationError (Filip Skokan) [#44890](https://github.com/nodejs/node/pull/44890)
* \[[`e0fbba0939`](https://github.com/nodejs/node/commit/e0fbba0939)] - **crypto**: use EVP\_PKEY\_CTX\_set\_dsa\_paramgen\_q\_bits when available (David Benjamin) [#44561](https://github.com/nodejs/node/pull/44561)
* \[[`a90386b0a1`](https://github.com/nodejs/node/commit/a90386b0a1)] - **deps**: update undici to 5.11.0 (Node.js GitHub Bot) [#44929](https://github.com/nodejs/node/pull/44929)
* \[[`aa68d40fbf`](https://github.com/nodejs/node/commit/aa68d40fbf)] - **deps**: update corepack to 0.14.2 (Node.js GitHub Bot) [#44775](https://github.com/nodejs/node/pull/44775)
* \[[`c892f35815`](https://github.com/nodejs/node/commit/c892f35815)] - **deps**: V8: fix debug build (Ben Noordhuis) [#44392](https://github.com/nodejs/node/pull/44392)
* \[[`91514393dc`](https://github.com/nodejs/node/commit/91514393dc)] - **dns**: support dns module in the snapshot (Joyee Cheung) [#44633](https://github.com/nodejs/node/pull/44633)
* \[[`ce3cb29319`](https://github.com/nodejs/node/commit/ce3cb29319)] - **doc**: add fsPromises.readFile() example (Tierney Cyren) [#40237](https://github.com/nodejs/node/pull/40237)
* \[[`97df9b84a2`](https://github.com/nodejs/node/commit/97df9b84a2)] - **doc**: improve building doc for Android (BuShe Pie) [#44888](https://github.com/nodejs/node/pull/44888)
* \[[`8c69da893b`](https://github.com/nodejs/node/commit/8c69da893b)] - **doc**: mention `corepack prepare` supports tag or range (Michael Rienstra) [#44646](https://github.com/nodejs/node/pull/44646)
* \[[`842bc64833`](https://github.com/nodejs/node/commit/842bc64833)] - **doc**: remove Legacy status from querystring (Rich Trott) [#44912](https://github.com/nodejs/node/pull/44912)
* \[[`ddb5402f5f`](https://github.com/nodejs/node/commit/ddb5402f5f)] - **doc**: fix label name in collaborator guide (Rich Trott) [#44920](https://github.com/nodejs/node/pull/44920)
* \[[`d08b024a3d`](https://github.com/nodejs/node/commit/d08b024a3d)] - **doc**: fix typo in Node.js 12 changelog (Lorand Horvath) [#42880](https://github.com/nodejs/node/pull/42880)
* \[[`b6b9c427c5`](https://github.com/nodejs/node/commit/b6b9c427c5)] - **doc**: move release keys we don't use anymore in README (Rich Trott) [#44899](https://github.com/nodejs/node/pull/44899)
* \[[`e92b074b32`](https://github.com/nodejs/node/commit/e92b074b32)] - **doc**: fix grammar in dns docs (#44850) (Colin Ihrig) [#44850](https://github.com/nodejs/node/pull/44850)
* \[[`780144c339`](https://github.com/nodejs/node/commit/780144c339)] - **doc**: remove unnecessary leading commas (Colin Ihrig) [#44854](https://github.com/nodejs/node/pull/44854)
* \[[`6ae9bc8fbc`](https://github.com/nodejs/node/commit/6ae9bc8fbc)] - **doc**: add extra step for reporter pre-approval (Rafael Gonzaga) [#44806](https://github.com/nodejs/node/pull/44806)
* \[[`ccf31d8bca`](https://github.com/nodejs/node/commit/ccf31d8bca)] - **doc**: add anchor link for --preserve-symlinks (Kohei Ueno) [#44858](https://github.com/nodejs/node/pull/44858)
* \[[`7c5c19ee54`](https://github.com/nodejs/node/commit/7c5c19ee54)] - **doc**: update node prefix require.cache example (Simone Busoli) [#44724](https://github.com/nodejs/node/pull/44724)
* \[[`2a5bce6318`](https://github.com/nodejs/node/commit/2a5bce6318)] - **doc**: include last security release date (Vladimir de Turckheim) [#44794](https://github.com/nodejs/node/pull/44794)
* \[[`4efaf4265c`](https://github.com/nodejs/node/commit/4efaf4265c)] - **doc**: remove "currently" and comma splice from child\_process.md (Rich Trott) [#44789](https://github.com/nodejs/node/pull/44789)
* \[[`3627616b40`](https://github.com/nodejs/node/commit/3627616b40)] - **doc,crypto**: mark experimental algorithms more visually (Filip Skokan) [#44892](https://github.com/nodejs/node/pull/44892)
* \[[`3c653cf23a`](https://github.com/nodejs/node/commit/3c653cf23a)] - **doc,crypto**: add missing CFRG curve algorithms to supported lists (Filip Skokan) [#44876](https://github.com/nodejs/node/pull/44876)
* \[[`70f55020d3`](https://github.com/nodejs/node/commit/70f55020d3)] - **doc,crypto**: add null length to crypto.subtle.deriveBits (Filip Skokan) [#44876](https://github.com/nodejs/node/pull/44876)
* \[[`910fbd0ece`](https://github.com/nodejs/node/commit/910fbd0ece)] - **esm**: fix duplicated test (Geoffrey Booth) [#44779](https://github.com/nodejs/node/pull/44779)
* \[[`bc00f3bde1`](https://github.com/nodejs/node/commit/bc00f3bde1)] - **fs**: fix opts.filter issue in cp async (Tho) [#44922](https://github.com/nodejs/node/pull/44922)
* \[[`11d1c23fa0`](https://github.com/nodejs/node/commit/11d1c23fa0)] - **(SEMVER-MINOR)** **fs**: add `FileHandle.prototype.readLines` (Antoine du Hamel) [#42590](https://github.com/nodejs/node/pull/42590)
* \[[`67fb76519a`](https://github.com/nodejs/node/commit/67fb76519a)] - **fs**: improve promise based readFile performance for big files (Ruben Bridgewater) [#44295](https://github.com/nodejs/node/pull/44295)
* \[[`dc6379bdc2`](https://github.com/nodejs/node/commit/dc6379bdc2)] - **fs**: don't hard code name in validatePosition() (Colin Ihrig) [#44767](https://github.com/nodejs/node/pull/44767)
* \[[`eb19b1e97c`](https://github.com/nodejs/node/commit/eb19b1e97c)] - **http**: be more aggressive to reply 400, 408 and 431 (ywave620) [#44818](https://github.com/nodejs/node/pull/44818)
* \[[`4c869c8d9e`](https://github.com/nodejs/node/commit/4c869c8d9e)] - **(SEMVER-MINOR)** **http**: add writeEarlyHints function to ServerResponse (Wing) [#44180](https://github.com/nodejs/node/pull/44180)
* \[[`9c7e66478c`](https://github.com/nodejs/node/commit/9c7e66478c)] - **(SEMVER-MINOR)** **http2**: make early hints generic (Yagiz Nizipli) [#44820](https://github.com/nodejs/node/pull/44820)
* \[[`3f20e5b15c`](https://github.com/nodejs/node/commit/3f20e5b15c)] - **(SEMVER-MINOR)** **lib**: refactor transferable AbortSignal (flakey5) [#44048](https://github.com/nodejs/node/pull/44048)
* \[[`ada7d82b16`](https://github.com/nodejs/node/commit/ada7d82b16)] - **lib**: require JSDoc in internal validators code (Rich Trott) [#44896](https://github.com/nodejs/node/pull/44896)
* \[[`67eaa303af`](https://github.com/nodejs/node/commit/67eaa303af)] - **lib**: add cause to DOMException (flakey5) [#44703](https://github.com/nodejs/node/pull/44703)
* \[[`0db86ee98e`](https://github.com/nodejs/node/commit/0db86ee98e)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#44930](https://github.com/nodejs/node/pull/44930)
* \[[`2efe4d985b`](https://github.com/nodejs/node/commit/2efe4d985b)] - **meta**: label test.js and test.md with test\_runner label (Moshe Atlow) [#44863](https://github.com/nodejs/node/pull/44863)
* \[[`fd9feb3a6c`](https://github.com/nodejs/node/commit/fd9feb3a6c)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#44857](https://github.com/nodejs/node/pull/44857)
* \[[`a854bb39c9`](https://github.com/nodejs/node/commit/a854bb39c9)] - **node-api**: create reference only when needed (Gerhard Stöbich) [#44827](https://github.com/nodejs/node/pull/44827)
* \[[`fd5c26b8db`](https://github.com/nodejs/node/commit/fd5c26b8db)] - **path**: change basename() argument from ext to suffix (Rich Trott) [#44774](https://github.com/nodejs/node/pull/44774)
* \[[`803fbfb168`](https://github.com/nodejs/node/commit/803fbfb168)] - **process**: fix uid/gid validation to avoid crash (Tobias Nießen) [#44910](https://github.com/nodejs/node/pull/44910)
* \[[`9f2dd48fc3`](https://github.com/nodejs/node/commit/9f2dd48fc3)] - **src**: remove uid\_t/gid\_t casts (Tobias Nießen) [#44914](https://github.com/nodejs/node/pull/44914)
* \[[`3abb607f3a`](https://github.com/nodejs/node/commit/3abb607f3a)] - **src**: remove UncheckedMalloc(0) workaround (Tobias Nießen) [#44543](https://github.com/nodejs/node/pull/44543)
* \[[`0606f9298f`](https://github.com/nodejs/node/commit/0606f9298f)] - **src**: deduplicate setting RSA OAEP label (Tobias Nießen) [#44849](https://github.com/nodejs/node/pull/44849)
* \[[`daf3152f7e`](https://github.com/nodejs/node/commit/daf3152f7e)] - **src**: implement GetDetachedness() in MemoryRetainerNode (Joyee Cheung) [#44803](https://github.com/nodejs/node/pull/44803)
* \[[`7ca77dd4ef`](https://github.com/nodejs/node/commit/7ca77dd4ef)] - **src**: avoid X509\_free in loops in crypto\_x509.cc (Tobias Nießen) [#44855](https://github.com/nodejs/node/pull/44855)
* \[[`781ad96227`](https://github.com/nodejs/node/commit/781ad96227)] - **src**: use OnScopeLeave instead of multiple free() (Tobias Nießen) [#44852](https://github.com/nodejs/node/pull/44852)
* \[[`b27b336a7a`](https://github.com/nodejs/node/commit/b27b336a7a)] - **src**: remove ParseIP() in cares\_wrap.cc (Tobias Nießen) [#44771](https://github.com/nodejs/node/pull/44771)
* \[[`f99f5d3c01`](https://github.com/nodejs/node/commit/f99f5d3c01)] - **(SEMVER-MINOR)** **src**: add detailed embedder process initialization API (Anna Henningsen) [#44121](https://github.com/nodejs/node/pull/44121)
* \[[`281fd7a09a`](https://github.com/nodejs/node/commit/281fd7a09a)] - **src,stream**: improve DoWrite() and Write() (ywave620) [#44434](https://github.com/nodejs/node/pull/44434)
* \[[`a33cc22bf7`](https://github.com/nodejs/node/commit/a33cc22bf7)] - **src,worker**: fix race of WorkerHeapSnapshotTaker (ywave620) [#44745](https://github.com/nodejs/node/pull/44745)
* \[[`f300f197da`](https://github.com/nodejs/node/commit/f300f197da)] - **stream**: handle enqueuing chunks when a pending BYOB pull request exists (Daeyeon Jeong) [#44770](https://github.com/nodejs/node/pull/44770)
* \[[`9ac029ea11`](https://github.com/nodejs/node/commit/9ac029ea11)] - **test**: bump memory limit for abort fatal error (Danielle Adams) [#44984](https://github.com/nodejs/node/pull/44984)
* \[[`b9b671f25f`](https://github.com/nodejs/node/commit/b9b671f25f)] - **test**: debug watch mode inspect (Moshe Atlow) [#44861](https://github.com/nodejs/node/pull/44861)
* \[[`2308b71d09`](https://github.com/nodejs/node/commit/2308b71d09)] - **test**: don't clobber RegExp.$\_ on startup (Ben Noordhuis) [#44864](https://github.com/nodejs/node/pull/44864)
* \[[`fe91bebb67`](https://github.com/nodejs/node/commit/fe91bebb67)] - **test**: loosen test for negative timestamps in `test-fs-stat-date` (Livia Medeiros) [#44707](https://github.com/nodejs/node/pull/44707)
* \[[`a080608552`](https://github.com/nodejs/node/commit/a080608552)] - **test**: check `--test` is disallowed in NODE\_OPTIONS (Kohei Ueno) [#44846](https://github.com/nodejs/node/pull/44846)
* \[[`dc2af265d7`](https://github.com/nodejs/node/commit/dc2af265d7)] - **test**: improve lib/internal/source\_map/source\_map.js coverage (MURAKAMI Masahiko) [#42771](https://github.com/nodejs/node/pull/42771)
* \[[`60a05d6dea`](https://github.com/nodejs/node/commit/60a05d6dea)] - **test**: skip some binding tests on IBMi PASE (Richard Lau) [#44810](https://github.com/nodejs/node/pull/44810)
* \[[`8dacedaa3d`](https://github.com/nodejs/node/commit/8dacedaa3d)] - **test**: remove unused variable in addon test (Joyee Cheung) [#44809](https://github.com/nodejs/node/pull/44809)
* \[[`c54cee1c3f`](https://github.com/nodejs/node/commit/c54cee1c3f)] - **test**: check server status in test-tls-psk-client (Richard Lau) [#44824](https://github.com/nodejs/node/pull/44824)
* \[[`ee3c6a4dc5`](https://github.com/nodejs/node/commit/ee3c6a4dc5)] - **test**: use async/await in test-debugger-exceptions (pete3249) [#44690](https://github.com/nodejs/node/pull/44690)
* \[[`9f14625fe5`](https://github.com/nodejs/node/commit/9f14625fe5)] - **test**: use async/await in test-debugger-help (Chandana) [#44686](https://github.com/nodejs/node/pull/44686)
* \[[`8033ad846b`](https://github.com/nodejs/node/commit/8033ad846b)] - **test**: update test-debugger-scripts to use await/async (mmeenapriya) [#44692](https://github.com/nodejs/node/pull/44692)
* \[[`f4f08be384`](https://github.com/nodejs/node/commit/f4f08be384)] - **test**: use await in test-debugger-invalid-json (Anjana Krishnakumar Vellore) [#44689](https://github.com/nodejs/node/pull/44689)
* \[[`d2f36169f3`](https://github.com/nodejs/node/commit/d2f36169f3)] - **test**: use async/await in test-debugger-random-port-with-inspect-port (Monu-Chaudhary) [#44695](https://github.com/nodejs/node/pull/44695)
* \[[`ddf029725b`](https://github.com/nodejs/node/commit/ddf029725b)] - **test**: use async/await in test-debugger-heap-profiler (Brinda Ashar) [#44693](https://github.com/nodejs/node/pull/44693)
* \[[`117f068250`](https://github.com/nodejs/node/commit/117f068250)] - **test**: use async/await in test-debugger-auto-resume (samyuktaprabhu) [#44675](https://github.com/nodejs/node/pull/44675)
* \[[`143c428cae`](https://github.com/nodejs/node/commit/143c428cae)] - **test**: migrated from Promise chains to Async/Await (Rathi N Das) [#44674](https://github.com/nodejs/node/pull/44674)
* \[[`e609a3309c`](https://github.com/nodejs/node/commit/e609a3309c)] - **test**: change promises to async/await in test-debugger-backtrace.js (Juliet Zhang) [#44677](https://github.com/nodejs/node/pull/44677)
* \[[`eeabd23ca6`](https://github.com/nodejs/node/commit/eeabd23ca6)] - **test**: use async/await in test-debugger-sb-before-load (Hope Olaidé) [#44697](https://github.com/nodejs/node/pull/44697)
* \[[`5c63d1464e`](https://github.com/nodejs/node/commit/5c63d1464e)] - **test**: add extra tests for basename with ext option (Connor Burton) [#44772](https://github.com/nodejs/node/pull/44772)
* \[[`f8b2d7a059`](https://github.com/nodejs/node/commit/f8b2d7a059)] - **test**: refactor to async/await (Divya Mohan) [#44694](https://github.com/nodejs/node/pull/44694)
* \[[`9864bde9ab`](https://github.com/nodejs/node/commit/9864bde9ab)] - **test**: modify test-debugger-custom-port.js to use async-await (Priya Shastri) [#44680](https://github.com/nodejs/node/pull/44680)
* \[[`af30823881`](https://github.com/nodejs/node/commit/af30823881)] - **test**: upgrade all 1024 bit RSA keys to 2048 bits (Momtchil Momtchev) [#44498](https://github.com/nodejs/node/pull/44498)
* \[[`0fb669e31f`](https://github.com/nodejs/node/commit/0fb669e31f)] - **test**: update test-debugger-breakpoint-exists.js to use async/await (Archana Kamath) [#44682](https://github.com/nodejs/node/pull/44682)
* \[[`cca253503e`](https://github.com/nodejs/node/commit/cca253503e)] - **test**: use async/await in test-debugger-preserve-breaks (poorvitusam) [#44696](https://github.com/nodejs/node/pull/44696)
* \[[`0b2e8b1681`](https://github.com/nodejs/node/commit/0b2e8b1681)] - **test**: use async/await in test-debugger-profile (surbhirjain) [#44684](https://github.com/nodejs/node/pull/44684)
* \[[`4db72a65cf`](https://github.com/nodejs/node/commit/4db72a65cf)] - **test**: change the promises to async/await in test-debugger-exec-scope.js (Ankita Khiratkar) [#44685](https://github.com/nodejs/node/pull/44685)
* \[[`56c9c98963`](https://github.com/nodejs/node/commit/56c9c98963)] - **test**: fix test-runner-inspect (Moshe Atlow) [#44620](https://github.com/nodejs/node/pull/44620)
* \[[`36227ed862`](https://github.com/nodejs/node/commit/36227ed862)] - **test**: fix watch mode test flake (Moshe Atlow) [#44739](https://github.com/nodejs/node/pull/44739)
* \[[`3abd71a0ea`](https://github.com/nodejs/node/commit/3abd71a0ea)] - **test**: deflake watch mode tests (Moshe Atlow) [#44621](https://github.com/nodejs/node/pull/44621)
* \[[`0c9f38f2be`](https://github.com/nodejs/node/commit/0c9f38f2be)] - **test**: split watch mode inspector tests to sequential (Moshe Atlow) [#44551](https://github.com/nodejs/node/pull/44551)
* \[[`d762a34128`](https://github.com/nodejs/node/commit/d762a34128)] - **test\_runner**: add --test-name-pattern CLI flag (Colin Ihrig)
* \[[`c7ece464a1`](https://github.com/nodejs/node/commit/c7ece464a1)] - **test\_runner**: remove runtime experimental warning (Colin Ihrig) [#44844](https://github.com/nodejs/node/pull/44844)
* \[[`3c1e9d41c8`](https://github.com/nodejs/node/commit/3c1e9d41c8)] - **test\_runner**: support using `--inspect` with `--test` (Moshe Atlow) [#44520](https://github.com/nodejs/node/pull/44520)
* \[[`4bdef48732`](https://github.com/nodejs/node/commit/4bdef48732)] - **tools**: remove faulty early termination logic from update-timezone.mjs (Darshan Sen) [#44870](https://github.com/nodejs/node/pull/44870)
* \[[`19d8574996`](https://github.com/nodejs/node/commit/19d8574996)] - **tools**: fix timezone update tool (Darshan Sen) [#44870](https://github.com/nodejs/node/pull/44870)
* \[[`ad8b8ae7d3`](https://github.com/nodejs/node/commit/ad8b8ae7d3)] - **tools**: update eslint to 8.25.0 (Node.js GitHub Bot) [#44931](https://github.com/nodejs/node/pull/44931)
* \[[`fd99b17a4d`](https://github.com/nodejs/node/commit/fd99b17a4d)] - **tools**: make `utils.SearchFiles` deterministic (Bruno Pitrus) [#44496](https://github.com/nodejs/node/pull/44496)
* \[[`131adece37`](https://github.com/nodejs/node/commit/131adece37)] - **tools**: fix typo in tools/update-authors.mjs (Darshan Sen) [#44780](https://github.com/nodejs/node/pull/44780)
* \[[`ab22777e65`](https://github.com/nodejs/node/commit/ab22777e65)] - **tools**: refactor deprecated format in no-unescaped-regexp-dot (Madhuri) [#44763](https://github.com/nodejs/node/pull/44763)
* \[[`3ad0fae89d`](https://github.com/nodejs/node/commit/3ad0fae89d)] - **tools**: update eslint-check.js to object style (andiemontoyeah) [#44706](https://github.com/nodejs/node/pull/44706)
* \[[`e9d572a9bd`](https://github.com/nodejs/node/commit/e9d572a9bd)] - **tools**: update eslint to 8.24.0 (Node.js GitHub Bot) [#44778](https://github.com/nodejs/node/pull/44778)
* \[[`984b0b4a6c`](https://github.com/nodejs/node/commit/984b0b4a6c)] - **tools**: update lint-md-dependencies to rollup\@2.79.1 (Node.js GitHub Bot) [#44776](https://github.com/nodejs/node/pull/44776)
* \[[`db5aeed702`](https://github.com/nodejs/node/commit/db5aeed702)] - **(SEMVER-MINOR)** **util**: add default value option to parsearg (Manuel Spigolon) [#44631](https://github.com/nodejs/node/pull/44631)
* \[[`576ccdf125`](https://github.com/nodejs/node/commit/576ccdf125)] - **util**: increase robustness with primordials (Jordan Harband) [#41212](https://github.com/nodejs/node/pull/41212)

<a id="18.10.0"></a>

## 2022-09-28, Version 18.10.0 (Current), @RafaelGSS

### Notable changes

* **doc**:
  * (SEMVER-MINOR) deprecate modp1, modp2, and modp5 groups (Tobias Nießen) [#44588](https://github.com/nodejs/node/pull/44588)
  * add legendecas to TSC list (Michael Dawson) [#44662](https://github.com/nodejs/node/pull/44662)
  * move `policy` docs to the `permissions` scope (Rafael Gonzaga) [#44222](https://github.com/nodejs/node/pull/44222)
* **gyp**:
  * libnode for ios app embedding (chexiongsheng) [#44210](https://github.com/nodejs/node/pull/44210)
* **http**:
  * (SEMVER-MINOR) throw error on content-length mismatch (sidwebworks) [#44588](https://github.com/nodejs/node/pull/44378)
* **stream**:
  * (SEMVER-MINOR) add `ReadableByteStream.tee()` (Daeyeon Jeong) [#44505](https://github.com/nodejs/node/pull/44505)

### Commits

* \[[`f497368679`](https://github.com/nodejs/node/commit/f497368679)] - **benchmark**: fix startup benchmark (Evan Lucas) [#44727](https://github.com/nodejs/node/pull/44727)
* \[[`0c9a94684e`](https://github.com/nodejs/node/commit/0c9a94684e)] - **benchmark**: add stream destroy benchmark (SindreXie) [#44533](https://github.com/nodejs/node/pull/44533)
* \[[`9c5c1459a8`](https://github.com/nodejs/node/commit/9c5c1459a8)] - **bootstrap**: clean up inspector console methods during serialization (Joyee Cheung) [#44279](https://github.com/nodejs/node/pull/44279)
* \[[`19f67dba8a`](https://github.com/nodejs/node/commit/19f67dba8a)] - **bootstrap**: remove unused global parameter in per-context scripts (Joyee Cheung) [#44472](https://github.com/nodejs/node/pull/44472)
* \[[`9da11426f6`](https://github.com/nodejs/node/commit/9da11426f6)] - **build**: remove redundant entry in crypto (Jiawen Geng) [#44604](https://github.com/nodejs/node/pull/44604)
* \[[`70898b4e67`](https://github.com/nodejs/node/commit/70898b4e67)] - **build**: rewritten the Android build system (BuShe Pie) [#44207](https://github.com/nodejs/node/pull/44207)
* \[[`a733f7faac`](https://github.com/nodejs/node/commit/a733f7faac)] - _**Revert**_ "**build**: go faster, drop -fno-omit-frame-pointer" (Ben Noordhuis) [#44566](https://github.com/nodejs/node/pull/44566)
* \[[`1315a83333`](https://github.com/nodejs/node/commit/1315a83333)] - **build**: fix bad upstream merge (Stephen Gallagher) [#44642](https://github.com/nodejs/node/pull/44642)
* \[[`993bd9b134`](https://github.com/nodejs/node/commit/993bd9b134)] - **crypto**: restrict PBKDF2 args to signed int (Tobias Nießen) [#44575](https://github.com/nodejs/node/pull/44575)
* \[[`ca5fb67b4e`](https://github.com/nodejs/node/commit/ca5fb67b4e)] - **deps**: update to ngtcp2 0.8.1 and nghttp3 0.7.0 (Tobias Nießen) [#44622](https://github.com/nodejs/node/pull/44622)
* \[[`8da1d6ebc4`](https://github.com/nodejs/node/commit/8da1d6ebc4)] - **deps**: update corepack to 0.14.1 (Node.js GitHub Bot) [#44704](https://github.com/nodejs/node/pull/44704)
* \[[`d36c4a3088`](https://github.com/nodejs/node/commit/d36c4a3088)] - **deps**: update ngtcp2 update instructions (Tobias Nießen) [#44619](https://github.com/nodejs/node/pull/44619)
* \[[`7129106aa0`](https://github.com/nodejs/node/commit/7129106aa0)] - **deps**: upgrade npm to 8.19.2 (npm team) [#44632](https://github.com/nodejs/node/pull/44632)
* \[[`3cc8f4bb56`](https://github.com/nodejs/node/commit/3cc8f4bb56)] - **deps**: update to uvwasi 0.0.13 (Colin Ihrig) [#44524](https://github.com/nodejs/node/pull/44524)
* \[[`4686579d4b`](https://github.com/nodejs/node/commit/4686579d4b)] - **dns**: remove unnecessary parameter from validateOneOf (Yagiz Nizipli) [#44635](https://github.com/nodejs/node/pull/44635)
* \[[`729dd95f1f`](https://github.com/nodejs/node/commit/729dd95f1f)] - **dns**: refactor default resolver (Joyee Cheung) [#44541](https://github.com/nodejs/node/pull/44541)
* \[[`6dc038262a`](https://github.com/nodejs/node/commit/6dc038262a)] - **doc**: mention git node backport (RafaelGSS) [#44764](https://github.com/nodejs/node/pull/44764)
* \[[`fd971f5176`](https://github.com/nodejs/node/commit/fd971f5176)] - **doc**: ensure to revert node\_version changes (Rafael Gonzaga) [#44760](https://github.com/nodejs/node/pull/44760)
* \[[`f274b08f8e`](https://github.com/nodejs/node/commit/f274b08f8e)] - **doc**: fix description for `napi_get_cb_info()` in `n-api.md` (Daeyeon Jeong) [#44761](https://github.com/nodejs/node/pull/44761)
* \[[`2502f2353d`](https://github.com/nodejs/node/commit/2502f2353d)] - **doc**: update the deprecation for exit code to clarify its scope (Daeyeon Jeong) [#44714](https://github.com/nodejs/node/pull/44714)
* \[[`064543d0ae`](https://github.com/nodejs/node/commit/064543d0ae)] - **doc**: update guidance for adding new modules (Michael Dawson) [#44576](https://github.com/nodejs/node/pull/44576)
* \[[`33a2f17534`](https://github.com/nodejs/node/commit/33a2f17534)] - **doc**: add registry number for Electron 22 (Keeley Hammond) [#44748](https://github.com/nodejs/node/pull/44748)
* \[[`10a0d75c26`](https://github.com/nodejs/node/commit/10a0d75c26)] - **doc**: include code examples for webstreams consumers (Lucas Santos) [#44387](https://github.com/nodejs/node/pull/44387)
* \[[`4dbe4a010c`](https://github.com/nodejs/node/commit/4dbe4a010c)] - **doc**: mention where to push security commits (RafaelGSS) [#44691](https://github.com/nodejs/node/pull/44691)
* \[[`82cb8151ad`](https://github.com/nodejs/node/commit/82cb8151ad)] - **doc**: remove extra space on threadpool usage (Connor Burton) [#44734](https://github.com/nodejs/node/pull/44734)
* \[[`6ef9af2748`](https://github.com/nodejs/node/commit/6ef9af2748)] - **doc**: make legacy banner slightly less bright (Rich Trott) [#44665](https://github.com/nodejs/node/pull/44665)
* \[[`b209c83e66`](https://github.com/nodejs/node/commit/b209c83e66)] - **doc**: improve building doc for Windows Powershell (Brian Muenzenmeyer) [#44625](https://github.com/nodejs/node/pull/44625)
* \[[`05b17e9250`](https://github.com/nodejs/node/commit/05b17e9250)] - **doc**: maintain only one list of MODP groups (Tobias Nießen) [#44644](https://github.com/nodejs/node/pull/44644)
* \[[`ec1cbdb69b`](https://github.com/nodejs/node/commit/ec1cbdb69b)] - **doc**: add legendecas to TSC list (Michael Dawson) [#44662](https://github.com/nodejs/node/pull/44662)
* \[[`9341fb4446`](https://github.com/nodejs/node/commit/9341fb4446)] - **doc**: remove comma in README.md (Taha-Chaudhry) [#44599](https://github.com/nodejs/node/pull/44599)
* \[[`3dabb44dda`](https://github.com/nodejs/node/commit/3dabb44dda)] - **doc**: use serial comma in report docs (Daeyeon Jeong) [#44608](https://github.com/nodejs/node/pull/44608)
* \[[`226d90a95a`](https://github.com/nodejs/node/commit/226d90a95a)] - **doc**: use serial comma in stream docs (Daeyeon Jeong) [#44609](https://github.com/nodejs/node/pull/44609)
* \[[`3f710fa636`](https://github.com/nodejs/node/commit/3f710fa636)] - **doc**: remove empty line in YAML block (Claudio Wunder) [#44617](https://github.com/nodejs/node/pull/44617)
* \[[`4ad1b0abc3`](https://github.com/nodejs/node/commit/4ad1b0abc3)] - **(SEMVER-MINOR)** **doc**: deprecate modp1, modp2, and modp5 groups (Tobias Nießen) [#44588](https://github.com/nodejs/node/pull/44588)
* \[[`2d92610525`](https://github.com/nodejs/node/commit/2d92610525)] - **doc**: remove old OpenSSL ENGINE constants (Tobias Nießen) [#44589](https://github.com/nodejs/node/pull/44589)
* \[[`03705639c4`](https://github.com/nodejs/node/commit/03705639c4)] - **doc**: fix heading levels for test runner hooks (Fabian Meyer) [#44603](https://github.com/nodejs/node/pull/44603)
* \[[`6c557346a7`](https://github.com/nodejs/node/commit/6c557346a7)] - **doc**: fix errors in http.md (Luigi Pinca) [#44587](https://github.com/nodejs/node/pull/44587)
* \[[`48d944b71c`](https://github.com/nodejs/node/commit/48d944b71c)] - **doc**: fix vm.Script createCachedData example (Chengzhong Wu) [#44487](https://github.com/nodejs/node/pull/44487)
* \[[`2813323120`](https://github.com/nodejs/node/commit/2813323120)] - **doc**: mention how to get commit release (Rafael Gonzaga) [#44572](https://github.com/nodejs/node/pull/44572)
* \[[`ea7b44d474`](https://github.com/nodejs/node/commit/ea7b44d474)] - **doc**: fix link in `process.md` (Antoine du Hamel) [#44594](https://github.com/nodejs/node/pull/44594)
* \[[`39b65d2fb7`](https://github.com/nodejs/node/commit/39b65d2fb7)] - **doc**: do not use weak MODP group in example (Tobias Nießen) [#44585](https://github.com/nodejs/node/pull/44585)
* \[[`f5549afd90`](https://github.com/nodejs/node/commit/f5549afd90)] - **doc**: remove ebpf from supported tooling list (Rafael Gonzaga) [#44549](https://github.com/nodejs/node/pull/44549)
* \[[`a3360b1f4f`](https://github.com/nodejs/node/commit/a3360b1f4f)] - **doc**: emphasize that createCipher is never secure (Tobias Nießen) [#44538](https://github.com/nodejs/node/pull/44538)
* \[[`4e6f7862ba`](https://github.com/nodejs/node/commit/4e6f7862ba)] - **doc**: document attribute Script.cachedDataRejected (Chengzhong Wu) [#44451](https://github.com/nodejs/node/pull/44451)
* \[[`01e584ecab`](https://github.com/nodejs/node/commit/01e584ecab)] - **doc**: move policy docs to the permissions scope (Rafael Gonzaga) [#44222](https://github.com/nodejs/node/pull/44222)
* \[[`57dac53c22`](https://github.com/nodejs/node/commit/57dac53c22)] - **doc,crypto**: cleanup removed pbkdf2 behaviours (Filip Skokan) [#44733](https://github.com/nodejs/node/pull/44733)
* \[[`c209bd6fb9`](https://github.com/nodejs/node/commit/c209bd6fb9)] - **doc,inspector**: document changes of inspector.close (Chengzhong Wu) [#44628](https://github.com/nodejs/node/pull/44628)
* \[[`9b3b7d6978`](https://github.com/nodejs/node/commit/9b3b7d6978)] - **esm,loader**: tidy ESMLoader internals (Jacob Smith) [#44701](https://github.com/nodejs/node/pull/44701)
* \[[`daf63d2fa3`](https://github.com/nodejs/node/commit/daf63d2fa3)] - **fs**: fix typo in mkdir example (SergeyTsukanov) [#44791](https://github.com/nodejs/node/pull/44791)
* \[[`85ab2f857f`](https://github.com/nodejs/node/commit/85ab2f857f)] - **fs**: remove unused option in `fs.fstatSync()` (Livia Medeiros) [#44613](https://github.com/nodejs/node/pull/44613)
* \[[`a6091f5496`](https://github.com/nodejs/node/commit/a6091f5496)] - **gyp**: libnode for ios app embedding (chexiongsheng) [#44210](https://github.com/nodejs/node/pull/44210)
* \[[`f158656e4c`](https://github.com/nodejs/node/commit/f158656e4c)] - **(SEMVER-MINOR)** **http**: throw error on content-length mismatch (sidwebworks) [#44378](https://github.com/nodejs/node/pull/44378)
* \[[`1b160517f5`](https://github.com/nodejs/node/commit/1b160517f5)] - **inspector**: expose inspector.close on workers (Chengzhong Wu) [#44489](https://github.com/nodejs/node/pull/44489)
* \[[`a2eb55a2c9`](https://github.com/nodejs/node/commit/a2eb55a2c9)] - **lib**: don't match `sourceMappingURL` in strings (Alan Agius) [#44658](https://github.com/nodejs/node/pull/44658)
* \[[`2baf532518`](https://github.com/nodejs/node/commit/2baf532518)] - **lib**: fix reference leak (falsandtru) [#44499](https://github.com/nodejs/node/pull/44499)
* \[[`d8d34ae6bc`](https://github.com/nodejs/node/commit/d8d34ae6bc)] - **lib**: reset `RegExp` statics before running user code (Antoine du Hamel) [#44247](https://github.com/nodejs/node/pull/44247)
* \[[`eb3635184b`](https://github.com/nodejs/node/commit/eb3635184b)] - **lib,test**: fix bug in InternalSocketAddress (Tobias Nießen) [#44618](https://github.com/nodejs/node/pull/44618)
* \[[`74dc4d198f`](https://github.com/nodejs/node/commit/74dc4d198f)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#44777](https://github.com/nodejs/node/pull/44777)
* \[[`97d2ed7296`](https://github.com/nodejs/node/commit/97d2ed7296)] - **meta**: add mailmap entry for dnlup (Rich Trott) [#44716](https://github.com/nodejs/node/pull/44716)
* \[[`35fbd2cc14`](https://github.com/nodejs/node/commit/35fbd2cc14)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#44705](https://github.com/nodejs/node/pull/44705)
* \[[`c5c1bc40a2`](https://github.com/nodejs/node/commit/c5c1bc40a2)] - **meta**: move dnlup to emeriti (dnlup) [#44667](https://github.com/nodejs/node/pull/44667)
* \[[`c62dfe0427`](https://github.com/nodejs/node/commit/c62dfe0427)] - **meta**: update test\_runner in label-pr-config (Shrujal Shah) [#44615](https://github.com/nodejs/node/pull/44615)
* \[[`fe56efd0bc`](https://github.com/nodejs/node/commit/fe56efd0bc)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#44591](https://github.com/nodejs/node/pull/44591)
* \[[`4436ffb536`](https://github.com/nodejs/node/commit/4436ffb536)] - **module**: open stat/readPackage to mutations (Maël Nison) [#44537](https://github.com/nodejs/node/pull/44537)
* \[[`f8ec946c82`](https://github.com/nodejs/node/commit/f8ec946c82)] - **module**: exports & imports map invalid slash deprecation (Guy Bedford) [#44477](https://github.com/nodejs/node/pull/44477)
* \[[`64cb43a2b6`](https://github.com/nodejs/node/commit/64cb43a2b6)] - **node-api**: add deprecation code of uncaught exception (Chengzhong Wu) [#44624](https://github.com/nodejs/node/pull/44624)
* \[[`ce1704c2c7`](https://github.com/nodejs/node/commit/ce1704c2c7)] - **src**: avoid using v8 on Isolate termination (Santiago Gimeno) [#44669](https://github.com/nodejs/node/pull/44669)
* \[[`3036b85d71`](https://github.com/nodejs/node/commit/3036b85d71)] - **src**: remove \<unistd.h> from node\_os.cc (Tobias Nießen) [#44668](https://github.com/nodejs/node/pull/44668)
* \[[`29f57b7899`](https://github.com/nodejs/node/commit/29f57b7899)] - **src**: avoid copy when creating Blob (Tobias Nießen) [#44616](https://github.com/nodejs/node/pull/44616)
* \[[`75cfb13ea6`](https://github.com/nodejs/node/commit/75cfb13ea6)] - **src**: make ReqWrap weak (Rafael Gonzaga) [#44074](https://github.com/nodejs/node/pull/44074)
* \[[`c12abb5ece`](https://github.com/nodejs/node/commit/c12abb5ece)] - **src**: make NearHeapLimitCallback() more robust (Joyee Cheung) [#44581](https://github.com/nodejs/node/pull/44581)
* \[[`81ea507e8e`](https://github.com/nodejs/node/commit/81ea507e8e)] - **src**: dump isolate stats when process exits (daomingq) [#44534](https://github.com/nodejs/node/pull/44534)
* \[[`687844822f`](https://github.com/nodejs/node/commit/687844822f)] - **src**: consolidate environment cleanup queue (Chengzhong Wu) [#44379](https://github.com/nodejs/node/pull/44379)
* \[[`3d42aaaac0`](https://github.com/nodejs/node/commit/3d42aaaac0)] - **stream**: handle a pending pull request from a released reader (Daeyeon Jeong) [#44702](https://github.com/nodejs/node/pull/44702)
* \[[`73ad9db6c5`](https://github.com/nodejs/node/commit/73ad9db6c5)] - **stream**: refactor use es2020 statement (SindreXie) [#44533](https://github.com/nodejs/node/pull/44533)
* \[[`0af6e420b3`](https://github.com/nodejs/node/commit/0af6e420b3)] - **stream**: remove `abortReason` from `WritableStreamDefaultController` (Daeyeon Jeong) [#44540](https://github.com/nodejs/node/pull/44540)
* \[[`2f2f8d5821`](https://github.com/nodejs/node/commit/2f2f8d5821)] - **(SEMVER-MINOR)** **stream**: add `ReadableByteStream.tee()` (Daeyeon Jeong) [#44505](https://github.com/nodejs/node/pull/44505)
* \[[`667e8bf3fb`](https://github.com/nodejs/node/commit/667e8bf3fb)] - **stream**: fix `writableStream.abort()` (Daeyeon Jeong) [#44327](https://github.com/nodejs/node/pull/44327)
* \[[`3112d5dae0`](https://github.com/nodejs/node/commit/3112d5dae0)] - **test**: verify napi\_remove\_wrap with napi\_delete\_reference (Chengzhong Wu) [#44754](https://github.com/nodejs/node/pull/44754)
* \[[`b512436841`](https://github.com/nodejs/node/commit/b512436841)] - **test**: change promises to async/await (Madhulika Sharma) [#44683](https://github.com/nodejs/node/pull/44683)
* \[[`858631f720`](https://github.com/nodejs/node/commit/858631f720)] - **test**: use async/await in test-debugger-invalid-args (Nupur Chauhan) [#44678](https://github.com/nodejs/node/pull/44678)
* \[[`6c9ded810c`](https://github.com/nodejs/node/commit/6c9ded810c)] - **test**: update test-debugger-low-level to use await/async (Meghana Ramesh) [#44688](https://github.com/nodejs/node/pull/44688)
* \[[`945aa74e57`](https://github.com/nodejs/node/commit/945aa74e57)] - **test**: check that sysconf returns a positive value (Tobias Nießen) [#44666](https://github.com/nodejs/node/pull/44666)
* \[[`79f0f48a6f`](https://github.com/nodejs/node/commit/79f0f48a6f)] - **test**: change promise to async/await in debugger-watcher (“Pooja) [#44687](https://github.com/nodejs/node/pull/44687)
* \[[`a56cb65bd6`](https://github.com/nodejs/node/commit/a56cb65bd6)] - **test**: fix addon tests compilation with OpenSSL 1.1.1 (Adam Majer) [#44725](https://github.com/nodejs/node/pull/44725)
* \[[`8a68a80a06`](https://github.com/nodejs/node/commit/8a68a80a06)] - **test**: fix test-performance-measure (smitley) [#44637](https://github.com/nodejs/node/pull/44637)
* \[[`55de0136b3`](https://github.com/nodejs/node/commit/55de0136b3)] - **test**: improve lib/readline.js coverage (MURAKAMI Masahiko) [#42686](https://github.com/nodejs/node/pull/42686)
* \[[`a3095d217f`](https://github.com/nodejs/node/commit/a3095d217f)] - **test**: fix `test-repl` not validating leaked globals properly (Antoine du Hamel) [#44640](https://github.com/nodejs/node/pull/44640)
* \[[`7db2974692`](https://github.com/nodejs/node/commit/7db2974692)] - **test**: ignore stale process cleanup failures on Windows (Joyee Cheung) [#44480](https://github.com/nodejs/node/pull/44480)
* \[[`6c35f338c3`](https://github.com/nodejs/node/commit/6c35f338c3)] - **test**: use python3 instead of python (Luigi Pinca) [#44545](https://github.com/nodejs/node/pull/44545)
* \[[`20e04c6d44`](https://github.com/nodejs/node/commit/20e04c6d44)] - **test**: fix DebugSymbolsTest.ReqWrapList on PPC64LE (Daniel Bevenius) [#44341](https://github.com/nodejs/node/pull/44341)
* \[[`eb25fe73b0`](https://github.com/nodejs/node/commit/eb25fe73b0)] - **test**: add more cases for parse-encoding (Tony Gorez) [#44427](https://github.com/nodejs/node/pull/44427)
* \[[`5ab3bc9419`](https://github.com/nodejs/node/commit/5ab3bc9419)] - **test\_runner**: include stack of uncaught exceptions (Moshe Atlow) [#44614](https://github.com/nodejs/node/pull/44614)
* \[[`752e1472e1`](https://github.com/nodejs/node/commit/752e1472e1)] - **tls**: fix out-of-bounds read in ClientHelloParser (Tobias Nießen) [#44580](https://github.com/nodejs/node/pull/44580)
* \[[`0cddb0af99`](https://github.com/nodejs/node/commit/0cddb0af99)] - **tools**: add update-llhttp.sh (Paolo Insogna) [#44652](https://github.com/nodejs/node/pull/44652)
* \[[`ef0dc47df9`](https://github.com/nodejs/node/commit/ef0dc47df9)] - **tools**: fix typo in update-nghttp2.sh (Luigi Pinca) [#44664](https://github.com/nodejs/node/pull/44664)
* \[[`0df181a5a1`](https://github.com/nodejs/node/commit/0df181a5a1)] - **tools**: add timezone update workflow (Lenvin Gonsalves) [#43988](https://github.com/nodejs/node/pull/43988)
* \[[`dd4348900d`](https://github.com/nodejs/node/commit/dd4348900d)] - **tools**: update eslint to 8.23.1 (Node.js GitHub Bot) [#44639](https://github.com/nodejs/node/pull/44639)
* \[[`b9cfb71e12`](https://github.com/nodejs/node/commit/b9cfb71e12)] - **tools**: update lint-md-dependencies to @rollup/plugin-node-resolve\@14.1.0 (Node.js GitHub Bot) [#44638](https://github.com/nodejs/node/pull/44638)
* \[[`5ae142d7ad`](https://github.com/nodejs/node/commit/5ae142d7ad)] - **tools**: update gyp-next to v0.13.0 (Jiawen Geng) [#44605](https://github.com/nodejs/node/pull/44605)
* \[[`5dd86c3faf`](https://github.com/nodejs/node/commit/5dd86c3faf)] - **tools**: update lint-md-dependencies to @rollup/plugin-node-resolve\@14.0.1 (Node.js GitHub Bot) [#44590](https://github.com/nodejs/node/pull/44590)
* \[[`caad4748cf`](https://github.com/nodejs/node/commit/caad4748cf)] - **tools**: increase timeout of running WPT (Joyee Cheung) [#44574](https://github.com/nodejs/node/pull/44574)
* \[[`5db9779f14`](https://github.com/nodejs/node/commit/5db9779f14)] - **tools**: fix shebang to use python3 by default (Himself65) [#44531](https://github.com/nodejs/node/pull/44531)
* \[[`9aa6a560e9`](https://github.com/nodejs/node/commit/9aa6a560e9)] - **v8**: add setHeapSnapshotNearHeapLimit (theanarkh) [#44420](https://github.com/nodejs/node/pull/44420)
* \[[`360b74e94f`](https://github.com/nodejs/node/commit/360b74e94f)] - **win**: fix fs.realpath.native for long paths (StefanStojanovic) [#44536](https://github.com/nodejs/node/pull/44536)

<a id="18.9.1"></a>

## 2022-09-23, Version 18.9.1 (Current), @RafaelGSS

This is a security release.

### Notable changes

The following CVEs are fixed in this release:

* **[CVE-2022-32212](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-32212)**: DNS rebinding in --inspect on macOS (High)
  * Insufficient fix for macOS devices on v18.5.0
* **[CVE-2022-32222](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-32222)**: Node 18 reads openssl.cnf from /home/iojs/build/ upon startup on MacOS (Medium)
* **[CVE-2022-32213](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-32213)**: HTTP Request Smuggling - Flawed Parsing of Transfer-Encoding (Medium)
  * Insufficient fix on v18.5.0
* **[CVE-2022-32215](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-32215)**: HTTP Request Smuggling - Incorrect Parsing of Multi-line Transfer-Encoding (Medium)
  * Insufficient fix on v18.5.0
* **[CVE-2022-35256](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-35256)**: HTTP Request Smuggling - Incorrect Parsing of Header Fields (Medium)
* **[CVE-2022-35255](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-35255)**: Weak randomness in WebCrypto keygen

More detailed information on each of the vulnerabilities can be found in [September 22nd 2022 Security Releases](https://nodejs.org/en/blog/vulnerability/september-2022-security-releases/) blog post.

#### llhttp updated to 6.0.10

`llhttp` is updated to 6.0.10 which includes fixes for the following vulnerabilities.

* **HTTP Request Smuggling - CVE-2022-32213 bypass via obs-fold mechanic (Medium)([CVE-2022-32213](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-32213) )**: The `llhttp` parser in the `http` module does not correctly parse and validate Transfer-Encoding headers. This can lead to HTTP Request Smuggling (HRS).
* **HTTP Request Smuggling - Incorrect Parsing of Multi-line Transfer-Encoding (Medium)([CVE-2022-32215](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-32215))**: The `llhttp` parser in the `http` module does not correctly handle multi-line Transfer-Encoding headers. This can lead to HTTP Request Smuggling (HRS).
* **HTTP Request Smuggling - Incorrect Parsing of Header Fields (Medium)([CVE-35256](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-35256))**: The llhttp parser in the `http` does not correctly handle header fields that are not terminated with CLRF. This can lead to HTTP Request Smuggling (HRS).

### Commits

* \[[`0c2a5723be`](https://github.com/nodejs/node/commit/0c2a5723be)] - **crypto**: fix weak randomness in WebCrypto keygen (Ben Noordhuis) [nodejs-private/node-private#](https://github.com/nodejs-private/node-private/pull/346)
* \[[`ffb6f4d51d`](https://github.com/nodejs/node/commit/ffb6f4d51d)] - **deps**: MacOS - fix location of OpenSSL config file (Michael Dawson) [nodejs-private/node-private#345](https://github.com/nodejs-private/node-private/pull/345)
* \[[`01bffcdd93`](https://github.com/nodejs/node/commit/01bffcdd93)] - **http**: disable chunked encoding when OBS fold is used (Paolo Insogna) [nodejs-private/node-private#341](https://github.com/nodejs-private/node-private/pull/341)
* \[[`2c379d341d`](https://github.com/nodejs/node/commit/2c379d341d)] - **src**: fix IPv4 non routable validation (RafaelGSS) [nodejs-private/node-private#337](https://github.com/nodejs-private/node-private/pull/337)

<a id="18.9.0"></a>

## 2022-09-08, Version 18.9.0 (Current), @RafaelGSS

### Notable changes

* **doc**
  * add daeyeon to collaborators (Daeyeon Jeong) [#44355](https://github.com/nodejs/node/pull/44355)
* **lib**
  * (SEMVER-MINOR) add diagnostics channel for process and worker (theanarkh) [#44045](https://github.com/nodejs/node/pull/44045)
* **os**
  * (SEMVER-MINOR) add machine method (theanarkh) [#44416](https://github.com/nodejs/node/pull/44416)
* **report**
  * (SEMVER-MINOR) expose report public native apis (Chengzhong Wu) [#44255](https://github.com/nodejs/node/pull/44255)
* **src**
  * (SEMVER-MINOR) expose environment RequestInterrupt api (Chengzhong Wu) [#44362](https://github.com/nodejs/node/pull/44362)
* **vm**
  * include vm context in the embedded snapshot (Joyee Cheung) [#44252](https://github.com/nodejs/node/pull/44252)

### Commits

* \[[`e27e709d3c`](https://github.com/nodejs/node/commit/e27e709d3c)] - **build**: add --libdir flag to configure (Stephen Gallagher) [#44361](https://github.com/nodejs/node/pull/44361)
* \[[`30da2b4d89`](https://github.com/nodejs/node/commit/30da2b4d89)] - **build**: added NINJA env to customize ninja binary (Jeff Dickey) [#44293](https://github.com/nodejs/node/pull/44293)
* \[[`3c5354869e`](https://github.com/nodejs/node/commit/3c5354869e)] - **cluster**: fix cluster rr distribute error (theanarkh) [#44202](https://github.com/nodejs/node/pull/44202)
* \[[`5cefd02618`](https://github.com/nodejs/node/commit/5cefd02618)] - **crypto**: handle invalid prepareAsymmetricKey JWK inputs (Filip Skokan) [#44475](https://github.com/nodejs/node/pull/44475)
* \[[`c868e36385`](https://github.com/nodejs/node/commit/c868e36385)] - **crypto**: add digest name to INVALID\_DIGEST errors (Tobias Nießen) [#44468](https://github.com/nodejs/node/pull/44468)
* \[[`35cbe1ad85`](https://github.com/nodejs/node/commit/35cbe1ad85)] - **crypto**: use actual option name in error message (Tobias Nießen) [#44455](https://github.com/nodejs/node/pull/44455)
* \[[`c3dbe18e4c`](https://github.com/nodejs/node/commit/c3dbe18e4c)] - **crypto**: simplify control flow in HKDF (Tobias Nießen) [#44272](https://github.com/nodejs/node/pull/44272)
* \[[`28781a1f7e`](https://github.com/nodejs/node/commit/28781a1f7e)] - **crypto**: improve RSA-PSS digest error messages (Tobias Nießen) [#44307](https://github.com/nodejs/node/pull/44307)
* \[[`b1eafe14fd`](https://github.com/nodejs/node/commit/b1eafe14fd)] - **debugger**: decrease timeout used to wait for the port to be free (Joyee Cheung) [#44359](https://github.com/nodejs/node/pull/44359)
* \[[`8ef5c40a83`](https://github.com/nodejs/node/commit/8ef5c40a83)] - **deps**: update corepack to 0.14.0 (Node.js GitHub Bot) [#44509](https://github.com/nodejs/node/pull/44509)
* \[[`cf19a79dfc`](https://github.com/nodejs/node/commit/cf19a79dfc)] - **deps**: upgrade npm to 8.19.1 (npm team) [#44486](https://github.com/nodejs/node/pull/44486)
* \[[`c5630ad1a7`](https://github.com/nodejs/node/commit/c5630ad1a7)] - **deps**: V8: backport ff8d67c88449 (Michaël Zasso) [#44423](https://github.com/nodejs/node/pull/44423)
* \[[`255e7fbd08`](https://github.com/nodejs/node/commit/255e7fbd08)] - **deps**: update Acorn to v8.8.0 (Michaël Zasso) [#44437](https://github.com/nodejs/node/pull/44437)
* \[[`754d26a53e`](https://github.com/nodejs/node/commit/754d26a53e)] - **deps**: patch V8 to 10.2.154.15 (Michaël Zasso) [#44294](https://github.com/nodejs/node/pull/44294)
* \[[`1b50ff2600`](https://github.com/nodejs/node/commit/1b50ff2600)] - **deps**: update icu tzdata to 2022b (Matías Zúñiga) [#44283](https://github.com/nodejs/node/pull/44283)
* \[[`1e451dca99`](https://github.com/nodejs/node/commit/1e451dca99)] - **deps**: upgrade llhttp to 6.0.9 (Paolo Insogna) [#44344](https://github.com/nodejs/node/pull/44344)
* \[[`57da3db522`](https://github.com/nodejs/node/commit/57da3db522)] - **deps**: update undici to 5.10.0 (Node.js GitHub Bot) [#44319](https://github.com/nodejs/node/pull/44319)
* \[[`1c87a7e8f6`](https://github.com/nodejs/node/commit/1c87a7e8f6)] - **doc**: add missing parenthesis in TLSSocket section (Tobias Nießen) [#44512](https://github.com/nodejs/node/pull/44512)
* \[[`05006eddb2`](https://github.com/nodejs/node/commit/05006eddb2)] - **doc**: do not use "Returns:" for crypto.constants (Tobias Nießen) [#44481](https://github.com/nodejs/node/pull/44481)
* \[[`54b6ed58bc`](https://github.com/nodejs/node/commit/54b6ed58bc)] - **doc**: use serial comma in addons docs (Tobias Nießen) [#44482](https://github.com/nodejs/node/pull/44482)
* \[[`11452a97b3`](https://github.com/nodejs/node/commit/11452a97b3)] - **doc**: add --update-assert-snapshot to node.1 (Colin Ihrig) [#44429](https://github.com/nodejs/node/pull/44429)
* \[[`ae028e8ac3`](https://github.com/nodejs/node/commit/ae028e8ac3)] - **doc**: improve assert.snapshot() docs (Colin Ihrig) [#44429](https://github.com/nodejs/node/pull/44429)
* \[[`71c869688a`](https://github.com/nodejs/node/commit/71c869688a)] - **doc**: add missing imports in events sample code (Brian Evans) [#44337](https://github.com/nodejs/node/pull/44337)
* \[[`92046e8027`](https://github.com/nodejs/node/commit/92046e8027)] - **doc**: apply scroll-margin-top to h2, h3 elements (metonym) [#44414](https://github.com/nodejs/node/pull/44414)
* \[[`3e6cde5931`](https://github.com/nodejs/node/commit/3e6cde5931)] - **doc**: fix spacing issue in `--build-snapshot` help text (Shohei YOSHIDA) [#44435](https://github.com/nodejs/node/pull/44435)
* \[[`8e41dbb81b`](https://github.com/nodejs/node/commit/8e41dbb81b)] - **doc**: mention cherry-pick edge-case on release (RafaelGSS) [#44408](https://github.com/nodejs/node/pull/44408)
* \[[`cef30f9afc`](https://github.com/nodejs/node/commit/cef30f9afc)] - **doc**: note on release guide to update `main` branch (Ruy Adorno) [#44384](https://github.com/nodejs/node/pull/44384)
* \[[`21437f7a7f`](https://github.com/nodejs/node/commit/21437f7a7f)] - **doc**: fix release guide example consistency (Ruy Adorno) [#44385](https://github.com/nodejs/node/pull/44385)
* \[[`ed52bd0a18`](https://github.com/nodejs/node/commit/ed52bd0a18)] - **doc**: fix style of n-api.md (theanarkh) [#44377](https://github.com/nodejs/node/pull/44377)
* \[[`65c1f4015f`](https://github.com/nodejs/node/commit/65c1f4015f)] - **doc**: add history for net.createServer() options (Luigi Pinca) [#44326](https://github.com/nodejs/node/pull/44326)
* \[[`4a0f750a6c`](https://github.com/nodejs/node/commit/4a0f750a6c)] - **doc**: add daeyeon to collaborators (Daeyeon Jeong) [#44355](https://github.com/nodejs/node/pull/44355)
* \[[`8cc5556f76`](https://github.com/nodejs/node/commit/8cc5556f76)] - **doc**: fix typo in test runner code examples (Moshe Atlow) [#44351](https://github.com/nodejs/node/pull/44351)
* \[[`b660b7467d`](https://github.com/nodejs/node/commit/b660b7467d)] - **doc,worker**: document resourceLimits overrides (Keyhan Vakil) [#43992](https://github.com/nodejs/node/pull/43992)
* \[[`2ed3b30696`](https://github.com/nodejs/node/commit/2ed3b30696)] - **inspector**: prevent integer overflow in open() (Tobias Nießen) [#44367](https://github.com/nodejs/node/pull/44367)
* \[[`b8f08e5e7e`](https://github.com/nodejs/node/commit/b8f08e5e7e)] - **lib**: codify findSourceMap return value when not found (Chengzhong Wu) [#44397](https://github.com/nodejs/node/pull/44397)
* \[[`a86ef1ba3e`](https://github.com/nodejs/node/commit/a86ef1ba3e)] - **lib**: use safe `Promise` alternatives when available (Antoine du Hamel) [#43476](https://github.com/nodejs/node/pull/43476)
* \[[`e519ac7842`](https://github.com/nodejs/node/commit/e519ac7842)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#44511](https://github.com/nodejs/node/pull/44511)
* \[[`c03f28b960`](https://github.com/nodejs/node/commit/c03f28b960)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#44422](https://github.com/nodejs/node/pull/44422)
* \[[`ef08cbddac`](https://github.com/nodejs/node/commit/ef08cbddac)] - **node-api**: avoid calling virtual methods in base's dtor (Chengzhong Wu) [#44424](https://github.com/nodejs/node/pull/44424)
* \[[`256340197c`](https://github.com/nodejs/node/commit/256340197c)] - **node-api**: cleanup redundant static modifiers (Chengzhong Wu) [#44301](https://github.com/nodejs/node/pull/44301)
* \[[`6714736706`](https://github.com/nodejs/node/commit/6714736706)] - **(SEMVER-MINOR)** **os**: add machine method (theanarkh) [#44416](https://github.com/nodejs/node/pull/44416)
* \[[`807b1e5533`](https://github.com/nodejs/node/commit/807b1e5533)] - **report**: get stack trace with cross origin contexts (Chengzhong Wu) [#44398](https://github.com/nodejs/node/pull/44398)
* \[[`b17cc877d0`](https://github.com/nodejs/node/commit/b17cc877d0)] - **report**: fix missing section javascriptHeap on OOMError (Chengzhong Wu) [#44398](https://github.com/nodejs/node/pull/44398)
* \[[`1f23c17ae0`](https://github.com/nodejs/node/commit/1f23c17ae0)] - **(SEMVER-MINOR)** **report**: expose report public native apis (Chengzhong Wu) [#44255](https://github.com/nodejs/node/pull/44255)
* \[[`df259005d9`](https://github.com/nodejs/node/commit/df259005d9)] - **report**: add queue info for udp (theanarkh) [#44345](https://github.com/nodejs/node/pull/44345)
* \[[`fc17b808c9`](https://github.com/nodejs/node/commit/fc17b808c9)] - **src**: rename misleading arg in ClientHelloParser (Tobias Nießen) [#44500](https://github.com/nodejs/node/pull/44500)
* \[[`125ab7da2a`](https://github.com/nodejs/node/commit/125ab7da2a)] - **src**: improve error handling in CloneSSLCerts (Tobias Nießen) [#44410](https://github.com/nodejs/node/pull/44410)
* \[[`aa34f7347b`](https://github.com/nodejs/node/commit/aa34f7347b)] - **src**: fix incorrect comments in crypto (Tobias Nießen) [#44470](https://github.com/nodejs/node/pull/44470)
* \[[`18b720805f`](https://github.com/nodejs/node/commit/18b720805f)] - **src**: avoid casting std::trunc(... / ...) to size\_t (Tobias Nießen) [#44467](https://github.com/nodejs/node/pull/44467)
* \[[`4331bbe2af`](https://github.com/nodejs/node/commit/4331bbe2af)] - **(SEMVER-MINOR)** **src**: expose environment RequestInterrupt api (Chengzhong Wu) [#44362](https://github.com/nodejs/node/pull/44362)
* \[[`c5413a1146`](https://github.com/nodejs/node/commit/c5413a1146)] - **src**: simplify enable\_if logic of `ToStringHelper::BaseConvert` (Feng Yu) [#44306](https://github.com/nodejs/node/pull/44306)
* \[[`dcc1cf4f4e`](https://github.com/nodejs/node/commit/dcc1cf4f4e)] - **src**: add error handling to `uv_uptime` call (Juan José Arboleda) [#44386](https://github.com/nodejs/node/pull/44386)
* \[[`fd611cc272`](https://github.com/nodejs/node/commit/fd611cc272)] - **src**: remove base64\_select\_table and base64\_table (Tobias Nießen) [#44425](https://github.com/nodejs/node/pull/44425)
* \[[`4776b4767b`](https://github.com/nodejs/node/commit/4776b4767b)] - **src**: fix uv\_err\_name memory leak (theanarkh) [#44421](https://github.com/nodejs/node/pull/44421)
* \[[`8db2e66d3a`](https://github.com/nodejs/node/commit/8db2e66d3a)] - **src**: make Endianness an enum class (Tobias Nießen) [#44411](https://github.com/nodejs/node/pull/44411)
* \[[`048e440878`](https://github.com/nodejs/node/commit/048e440878)] - **src**: fix ssize\_t error from nghttp2.h (Darshan Sen) [#44393](https://github.com/nodejs/node/pull/44393)
* \[[`dc1c95ede3`](https://github.com/nodejs/node/commit/dc1c95ede3)] - **src**: trace fs async api (theanarkh) [#44057](https://github.com/nodejs/node/pull/44057)
* \[[`0f4e98ba2c`](https://github.com/nodejs/node/commit/0f4e98ba2c)] - **src**: restore context default IsCodeGenerationFromStringsAllowed value (Chengzhong Wu) [#44324](https://github.com/nodejs/node/pull/44324)
* \[[`05fb650b54`](https://github.com/nodejs/node/commit/05fb650b54)] - **src**: simplify and optimize GetOpenSSLVersion() (Tobias Nießen) [#44395](https://github.com/nodejs/node/pull/44395)
* \[[`7f16177f96`](https://github.com/nodejs/node/commit/7f16177f96)] - **src**: use `if constexpr` where appropriate (Anna Henningsen) [#44291](https://github.com/nodejs/node/pull/44291)
* \[[`2be8acad18`](https://github.com/nodejs/node/commit/2be8acad18)] - **src**: simplify ECDH::GetCurves() (Tobias Nießen) [#44309](https://github.com/nodejs/node/pull/44309)
* \[[`3eb7918f8e`](https://github.com/nodejs/node/commit/3eb7918f8e)] - **src**: make minor improvements to EnabledDebugList (Tobias Nießen) [#44350](https://github.com/nodejs/node/pull/44350)
* \[[`88d9566593`](https://github.com/nodejs/node/commit/88d9566593)] - **src**: remove KeyObjectData::symmetric\_key\_len\_ (Tobias Nießen) [#44346](https://github.com/nodejs/node/pull/44346)
* \[[`768c9cb872`](https://github.com/nodejs/node/commit/768c9cb872)] - **src**: fix multiple format string bugs (Tobias Nießen) [#44314](https://github.com/nodejs/node/pull/44314)
* \[[`6857ee8299`](https://github.com/nodejs/node/commit/6857ee8299)] - **src**: make minor improvements to SecureBuffer (Tobias Nießen) [#44302](https://github.com/nodejs/node/pull/44302)
* \[[`2facf8b8e0`](https://github.com/nodejs/node/commit/2facf8b8e0)] - **stream**: fix setting abort reason in `ReadableStream.pipeTo()` (Daeyeon Jeong) [#44418](https://github.com/nodejs/node/pull/44418)
* \[[`65134d696b`](https://github.com/nodejs/node/commit/65134d696b)] - **stream**: fix `ReadableStreamReader.releaseLock()` (Daeyeon Jeong) [#44292](https://github.com/nodejs/node/pull/44292)
* \[[`4c33e5d4ce`](https://github.com/nodejs/node/commit/4c33e5d4ce)] - **test**: avoid race in file write stream handle tests (Joyee Cheung) [#44380](https://github.com/nodejs/node/pull/44380)
* \[[`0d77342a39`](https://github.com/nodejs/node/commit/0d77342a39)] - **test**: style updates for assert.snapshot() (Colin Ihrig) [#44429](https://github.com/nodejs/node/pull/44429)
* \[[`e36ed44b26`](https://github.com/nodejs/node/commit/e36ed44b26)] - **test**: deflake child process exec timeout tests (Joyee Cheung) [#44390](https://github.com/nodejs/node/pull/44390)
* \[[`0af15c71fb`](https://github.com/nodejs/node/commit/0af15c71fb)] - **test**: make the vm timeout escape tests more lenient (Joyee Cheung) [#44433](https://github.com/nodejs/node/pull/44433)
* \[[`0f071b800e`](https://github.com/nodejs/node/commit/0f071b800e)] - **test**: split heap prof tests (Joyee Cheung) [#44388](https://github.com/nodejs/node/pull/44388)
* \[[`2dd88b8425`](https://github.com/nodejs/node/commit/2dd88b8425)] - **test**: fix multiple incorrect mustNotCall() uses (Tobias Nießen) [#44022](https://github.com/nodejs/node/pull/44022)
* \[[`4ae1f4990c`](https://github.com/nodejs/node/commit/4ae1f4990c)] - **test**: split report OOM tests (Joyee Cheung) [#44389](https://github.com/nodejs/node/pull/44389)
* \[[`3a5fdacdc2`](https://github.com/nodejs/node/commit/3a5fdacdc2)] - **test**: fix WPT runner result (Daeyeon Jeong) [#44238](https://github.com/nodejs/node/pull/44238)
* \[[`e001aafee3`](https://github.com/nodejs/node/commit/e001aafee3)] - **test**: raise sleep times in child process tests (Joyee Cheung) [#44375](https://github.com/nodejs/node/pull/44375)
* \[[`8e2dcafc24`](https://github.com/nodejs/node/commit/8e2dcafc24)] - **test**: remove duplicate test (Luigi Pinca) [#44313](https://github.com/nodejs/node/pull/44313)
* \[[`c65d7fb1fa`](https://github.com/nodejs/node/commit/c65d7fb1fa)] - **test**: add OpenSSL 3.x providers test (Richard Lau) [#44148](https://github.com/nodejs/node/pull/44148)
* \[[`11e9d6e173`](https://github.com/nodejs/node/commit/11e9d6e173)] - **test**: make tmpdir.js importable from esm (Geoffrey Booth) [#44322](https://github.com/nodejs/node/pull/44322)
* \[[`a35c2f9ef4`](https://github.com/nodejs/node/commit/a35c2f9ef4)] - **test\_runner**: fix `duration_ms` to be milliseconds (Moshe Atlow) [#44450](https://github.com/nodejs/node/pull/44450)
* \[[`8175c65b4d`](https://github.com/nodejs/node/commit/8175c65b4d)] - **test\_runner**: support programmatically running `--test` (Moshe Atlow) [#44241](https://github.com/nodejs/node/pull/44241)
* \[[`1cdccbc845`](https://github.com/nodejs/node/commit/1cdccbc845)] - **tls**: remove SecureContext setFreeListLength (Tobias Nießen) [#44300](https://github.com/nodejs/node/pull/44300)
* \[[`70399166f3`](https://github.com/nodejs/node/commit/70399166f3)] - **tls**: use OpenSSL constant for client random size (Tobias Nießen) [#44305](https://github.com/nodejs/node/pull/44305)
* \[[`6fe189b62a`](https://github.com/nodejs/node/commit/6fe189b62a)] - **tools**: update lint-md-dependencies to rollup\@2.79.0 (Node.js GitHub Bot) [#44510](https://github.com/nodejs/node/pull/44510)
* \[[`1e62bb14dd`](https://github.com/nodejs/node/commit/1e62bb14dd)] - **tools**: fix typo in `avoid-prototype-pollution` lint rule (Antoine du Hamel) [#44446](https://github.com/nodejs/node/pull/44446)
* \[[`78c6827688`](https://github.com/nodejs/node/commit/78c6827688)] - **tools**: don't use f-strings in test.py (Santiago Gimeno) [#44407](https://github.com/nodejs/node/pull/44407)
* \[[`443730c419`](https://github.com/nodejs/node/commit/443730c419)] - **tools**: update doc to unist-util-visit\@4.1.1 (Node.js GitHub Bot) [#44370](https://github.com/nodejs/node/pull/44370)
* \[[`96df99375e`](https://github.com/nodejs/node/commit/96df99375e)] - **tools**: update eslint to 8.23.0 (Node.js GitHub Bot) [#44419](https://github.com/nodejs/node/pull/44419)
* \[[`b6709544e9`](https://github.com/nodejs/node/commit/b6709544e9)] - **tools**: refactor `avoid-prototype-pollution` lint rule (Antoine du Hamel) [#43476](https://github.com/nodejs/node/pull/43476)
* \[[`8b0a4afcae`](https://github.com/nodejs/node/commit/8b0a4afcae)] - **tty**: fix TypeError when stream is closed (Antoine du Hamel) [#43803](https://github.com/nodejs/node/pull/43803)
* \[[`c4a45a93f3`](https://github.com/nodejs/node/commit/c4a45a93f3)] - **vm**: avoid unnecessary property getter interceptor calls (Joyee Cheung) [#44252](https://github.com/nodejs/node/pull/44252)
* \[[`736a04aa13`](https://github.com/nodejs/node/commit/736a04aa13)] - **vm**: include vm context in the embedded snapshot (Joyee Cheung) [#44252](https://github.com/nodejs/node/pull/44252)
* \[[`bce827e5d1`](https://github.com/nodejs/node/commit/bce827e5d1)] - **vm**: make ContextifyContext template context-independent (Joyee Cheung) [#44252](https://github.com/nodejs/node/pull/44252)

<a id="18.8.0"></a>

## 2022-08-24, Version 18.8.0 (Current), @ruyadorno

### Notable changes

#### bootstrap: implement run-time user-land snapshots via --build-snapshot and --snapshot-blob

This patch introduces `--build-snapshot` and `--snapshot-blob` options for creating and using user land snapshots.

To generate a snapshot using snapshot.js as an entry point and write the snapshot blob to snapshot.blob:

```bash
echo "globalThis.foo = 'I am from the snapshot'" > snapshot.js
node --snapshot-blob snapshot.blob --build-snapshot snapshot.js
```

To restore application state from snapshot.blob, with index.js as the entry point script for the deserialized application:

```bash
echo "console.log(globalThis.foo)" > index.js
node --snapshot-blob snapshot.blob index.js
# => I am from the snapshot
```

Users can also use the `v8.startupSnapshot` API to specify an entry point at snapshot building time, thus avoiding the need of an additional entry script at deserialization time:

```bash
echo "require('v8').startupSnapshot.setDeserializeMainFunction(() => console.log('I am from the snapshot'))" > snapshot.js
node --snapshot-blob snapshot.blob --build-snapshot snapshot.js
node --snapshot-blob snapshot.blob
# => I am from the snapshot
```

Contributed by Joyee Cheung in [#38905](https://github.com/nodejs/node/pull/38905)

### Other notable changes

* **crypto**:
  * (SEMVER-MINOR) allow zero-length IKM in HKDF and in webcrypto PBKDF2 (Filip Skokan) [#44201](https://github.com/nodejs/node/pull/44201)
  * (SEMVER-MINOR) allow zero-length secret KeyObject (Filip Skokan) [#44201](https://github.com/nodejs/node/pull/44201)
* **deps**:
  * upgrade npm to 8.18.0 (npm team) [#44263](https://github.com/nodejs/node/pull/44263) - Adds a new `npm query` command
* **doc**:
  * add Erick Wendel to collaborators (Erick Wendel) [#44088](https://github.com/nodejs/node/pull/44088)
  * add theanarkh to collaborators (theanarkh) [#44131](https://github.com/nodejs/node/pull/44131)
  * add MoLow to collaborators (Moshe Atlow) [#44214](https://github.com/nodejs/node/pull/44214)
  * add cola119 to collaborators (cola119) [#44248](https://github.com/nodejs/node/pull/44248)
  * deprecate `--trace-atomics-wait` (Keyhan Vakil) [#44093](https://github.com/nodejs/node/pull/44093)
* **http**:
  * (SEMVER-MINOR) make idle http parser count configurable (theanarkh) [#43974](https://github.com/nodejs/node/pull/43974)
* **net**:
  * (SEMVER-MINOR) add local family (theanarkh) [#43975](https://github.com/nodejs/node/pull/43975)
* **src**:
  * (SEMVER-MINOR) print source map error source on demand (Chengzhong Wu) [#43875](https://github.com/nodejs/node/pull/43875)
* **tls**:
  * (SEMVER-MINOR) pass a valid socket on `tlsClientError` (Daeyeon Jeong) [#44021](https://github.com/nodejs/node/pull/44021)

### Commits

* \[[`0e20072e32`](https://github.com/nodejs/node/commit/0e20072e32)] - **assert**: add `getCalls` and `reset` to callTracker (Moshe Atlow) [#44191](https://github.com/nodejs/node/pull/44191)
* \[[`126fbbab74`](https://github.com/nodejs/node/commit/126fbbab74)] - **assert**: add assert.Snapshot (Moshe Atlow) [#44095](https://github.com/nodejs/node/pull/44095)
* \[[`87d7845b4f`](https://github.com/nodejs/node/commit/87d7845b4f)] - **bootstrap**: fixup Error.stackTraceLimit for user-land snapshot (Joyee Cheung) [#44203](https://github.com/nodejs/node/pull/44203)
* \[[`597a5171ee`](https://github.com/nodejs/node/commit/597a5171ee)] - **(SEMVER-MINOR)** **bootstrap**: clean up warning setup during serialization (Joyee Cheung) [#38905](https://github.com/nodejs/node/pull/38905)
* \[[`3561514ff5`](https://github.com/nodejs/node/commit/3561514ff5)] - **(SEMVER-MINOR)** **bootstrap**: implement --snapshot-blob and --build-snapshot (Joyee Cheung) [#38905](https://github.com/nodejs/node/pull/38905)
* \[[`123b2d6795`](https://github.com/nodejs/node/commit/123b2d6795)] - **bootstrap**: turn on FunctionCodeHandling::kKeep in the snapshot builder (Joyee Cheung) [#44104](https://github.com/nodejs/node/pull/44104)
* \[[`e7d101fbd4`](https://github.com/nodejs/node/commit/e7d101fbd4)] - **bootstrap**: support more builtins in the embedded code cache (Joyee Cheung) [#44018](https://github.com/nodejs/node/pull/44018)
* \[[`2ae2828040`](https://github.com/nodejs/node/commit/2ae2828040)] - **build**: enable pointer authentication for branch protection on arm64 (Jeremiah Gowdy) [#43200](https://github.com/nodejs/node/pull/43200)
* \[[`fecec4d3ba`](https://github.com/nodejs/node/commit/fecec4d3ba)] - **build**: add workflow to label flaky-test platform (Rafael Gonzaga) [#44042](https://github.com/nodejs/node/pull/44042)
* \[[`c975c4f674`](https://github.com/nodejs/node/commit/c975c4f674)] - **build**: optimized and fixed building configuration to Android (BuShe) [#44016](https://github.com/nodejs/node/pull/44016)
* \[[`ec1b31e6ad`](https://github.com/nodejs/node/commit/ec1b31e6ad)] - **build**: allow test-internet on forks if not scheduled (Rich Trott) [#44073](https://github.com/nodejs/node/pull/44073)
* \[[`ea48c5673b`](https://github.com/nodejs/node/commit/ea48c5673b)] - **build**: skip test-internet run on forks (Rich Trott) [#44054](https://github.com/nodejs/node/pull/44054)
* \[[`1c0d66e927`](https://github.com/nodejs/node/commit/1c0d66e927)] - **(SEMVER-MINOR)** **crypto**: allow zero-length IKM in HKDF and in webcrypto PBKDF2 (Filip Skokan) [#44201](https://github.com/nodejs/node/pull/44201)
* \[[`07d90c8a71`](https://github.com/nodejs/node/commit/07d90c8a71)] - **(SEMVER-MINOR)** **crypto**: allow zero-length secret KeyObject (Filip Skokan) [#44201](https://github.com/nodejs/node/pull/44201)
* \[[`ac2b10e0c7`](https://github.com/nodejs/node/commit/ac2b10e0c7)] - **crypto**: fix webcrypto deriveBits validations (Filip Skokan) [#44173](https://github.com/nodejs/node/pull/44173)
* \[[`4c902be5a5`](https://github.com/nodejs/node/commit/4c902be5a5)] - **crypto**: fix webcrypto EC key namedCurve validation errors (Filip Skokan) [#44172](https://github.com/nodejs/node/pull/44172)
* \[[`81e1ec4f6f`](https://github.com/nodejs/node/commit/81e1ec4f6f)] - **crypto**: fix webcrypto generateKey() AES key length validation error (Filip Skokan) [#44170](https://github.com/nodejs/node/pull/44170)
* \[[`ad8ef3a56c`](https://github.com/nodejs/node/commit/ad8ef3a56c)] - **crypto**: fix webcrypto operation errors to be OperationError (Filip Skokan) [#44171](https://github.com/nodejs/node/pull/44171)
* \[[`c270b9a0aa`](https://github.com/nodejs/node/commit/c270b9a0aa)] - **deps**: update corepack to 0.13.0 (Node.js GitHub Bot) [#44318](https://github.com/nodejs/node/pull/44318)
* \[[`bce8041d67`](https://github.com/nodejs/node/commit/bce8041d67)] - **deps**: upgrade npm to 8.18.0 (npm team) [#44263](https://github.com/nodejs/node/pull/44263)
* \[[`a26997263b`](https://github.com/nodejs/node/commit/a26997263b)] - **deps**: update corepack to 0.12.3 (Node.js GitHub Bot) [#44229](https://github.com/nodejs/node/pull/44229)
* \[[`b1590bbca2`](https://github.com/nodejs/node/commit/b1590bbca2)] - **deps**: upgrade npm to 8.17.0 (npm team) [#44205](https://github.com/nodejs/node/pull/44205)
* \[[`818271c1c3`](https://github.com/nodejs/node/commit/818271c1c3)] - **deps**: update undici to 5.8.2 (Node.js GitHub Bot) [#44187](https://github.com/nodejs/node/pull/44187)
* \[[`d09bc5402d`](https://github.com/nodejs/node/commit/d09bc5402d)] - **deps**: update undici to 5.8.1 (Node.js GitHub Bot) [#44158](https://github.com/nodejs/node/pull/44158)
* \[[`a92d90b482`](https://github.com/nodejs/node/commit/a92d90b482)] - **deps**: update corepack to 0.12.2 (Node.js GitHub Bot) [#44159](https://github.com/nodejs/node/pull/44159)
* \[[`52a516a281`](https://github.com/nodejs/node/commit/52a516a281)] - **deps**: V8: cherry-pick 9861ce1deae2 (Milad Fa) [#44115](https://github.com/nodejs/node/pull/44115)
* \[[`763b956f07`](https://github.com/nodejs/node/commit/763b956f07)] - **deps**: remove unnecessary file (Brian White) [#44133](https://github.com/nodejs/node/pull/44133)
* \[[`194587e767`](https://github.com/nodejs/node/commit/194587e767)] - **deps**: upgrade npm to 8.16.0 (npm team) [#44119](https://github.com/nodejs/node/pull/44119)
* \[[`116dcccc79`](https://github.com/nodejs/node/commit/116dcccc79)] - **deps**: upgrade base64 to dc6a41ce36e (Brian White) [#44032](https://github.com/nodejs/node/pull/44032)
* \[[`b7aaf3d4ca`](https://github.com/nodejs/node/commit/b7aaf3d4ca)] - **deps**: upgrade npm to 8.15.1 (npm team) [#44013](https://github.com/nodejs/node/pull/44013)
* \[[`a0c57837c4`](https://github.com/nodejs/node/commit/a0c57837c4)] - **deps**: cherry-pick 00704f5a from V8 upstream (Keyhan Vakil) [#43921](https://github.com/nodejs/node/pull/43921)
* \[[`19557ad6a4`](https://github.com/nodejs/node/commit/19557ad6a4)] - **dgram**: add dgram send queue info (theanarkh) [#44149](https://github.com/nodejs/node/pull/44149)
* \[[`a93371205b`](https://github.com/nodejs/node/commit/a93371205b)] - **doc**: fix optionality of callback arg of checkPrime (Tobias Nießen) [#44311](https://github.com/nodejs/node/pull/44311)
* \[[`d3f3bf602d`](https://github.com/nodejs/node/commit/d3f3bf602d)] - **doc**: fix typo (Hana) [#44262](https://github.com/nodejs/node/pull/44262)
* \[[`7a567875b0`](https://github.com/nodejs/node/commit/7a567875b0)] - **doc**: add TypeScript execution requirements (Michael Dawson) [#44030](https://github.com/nodejs/node/pull/44030)
* \[[`e8916fa758`](https://github.com/nodejs/node/commit/e8916fa758)] - **doc**: add cola119 to collaborators (cola119) [#44248](https://github.com/nodejs/node/pull/44248)
* \[[`8c1fe86026`](https://github.com/nodejs/node/commit/8c1fe86026)] - **doc**: fix added version for readable.closed/destroyed (Matthew Peveler) [#44033](https://github.com/nodejs/node/pull/44033)
* \[[`f39a0514d3`](https://github.com/nodejs/node/commit/f39a0514d3)] - **doc**: improved building doc for Android (BuShe) [#44166](https://github.com/nodejs/node/pull/44166)
* \[[`4d26cb9bb2`](https://github.com/nodejs/node/commit/4d26cb9bb2)] - **doc**: add MoLow to collaborators (Moshe Atlow) [#44214](https://github.com/nodejs/node/pull/44214)
* \[[`6bff14b6f1`](https://github.com/nodejs/node/commit/6bff14b6f1)] - **doc**: update tags in adding-new-napi-api.md (Chengzhong Wu) [#44190](https://github.com/nodejs/node/pull/44190)
* \[[`721639a1d4`](https://github.com/nodejs/node/commit/721639a1d4)] - **doc**: fix typo in diagnostics\_channel (Evan Lucas) [#44199](https://github.com/nodejs/node/pull/44199)
* \[[`0fffc24caa`](https://github.com/nodejs/node/commit/0fffc24caa)] - **doc**: add Retry CI in collaborator guide (Livia Medeiros) [#44130](https://github.com/nodejs/node/pull/44130)
* \[[`fb11643e31`](https://github.com/nodejs/node/commit/fb11643e31)] - **doc**: add performance note to `--enable-source-maps` docs (Saurabh Daware) [#43817](https://github.com/nodejs/node/pull/43817)
* \[[`cb7a9e78fd`](https://github.com/nodejs/node/commit/cb7a9e78fd)] - **doc**: remove unused code in call tracker example (Colin Ihrig) [#44127](https://github.com/nodejs/node/pull/44127)
* \[[`8c26daff7c`](https://github.com/nodejs/node/commit/8c26daff7c)] - **doc**: add theanarkh to collaborators (theanarkh) [#44131](https://github.com/nodejs/node/pull/44131)
* \[[`46f8fb8e53`](https://github.com/nodejs/node/commit/46f8fb8e53)] - **doc**: clarify tls.tlsSocket.getCipher().version (Adam Majer) [#44086](https://github.com/nodejs/node/pull/44086)
* \[[`02236032f0`](https://github.com/nodejs/node/commit/02236032f0)] - **doc**: update repository list in onboarding doc (Rich Trott) [#44089](https://github.com/nodejs/node/pull/44089)
* \[[`58f2739e32`](https://github.com/nodejs/node/commit/58f2739e32)] - **doc**: add Erick Wendel to collaborators (Erick Wendel) [#44088](https://github.com/nodejs/node/pull/44088)
* \[[`fe83d514b2`](https://github.com/nodejs/node/commit/fe83d514b2)] - **doc**: update collaborator email (Ruy Adorno) [#44044](https://github.com/nodejs/node/pull/44044)
* \[[`76011dd7f7`](https://github.com/nodejs/node/commit/76011dd7f7)] - **doc**: copyedit `test.md` (Antoine du Hamel) [#44061](https://github.com/nodejs/node/pull/44061)
* \[[`1d6029aa3d`](https://github.com/nodejs/node/commit/1d6029aa3d)] - **doc**: add kvakil to triagers (Keyhan Vakil) [#43996](https://github.com/nodejs/node/pull/43996)
* \[[`7f7a0eb2f5`](https://github.com/nodejs/node/commit/7f7a0eb2f5)] - **doc**: clarify part of onboarding guide regarding adding to teams (Darshan Sen) [#44024](https://github.com/nodejs/node/pull/44024)
* \[[`2ae5d853a7`](https://github.com/nodejs/node/commit/2ae5d853a7)] - **doc**: fix code examples in `crypto.md` (Antoine du Hamel) [#44053](https://github.com/nodejs/node/pull/44053)
* \[[`1b9537b6a5`](https://github.com/nodejs/node/commit/1b9537b6a5)] - **doc**: claim ABI version for Electron 21 (Keeley Hammond) [#44034](https://github.com/nodejs/node/pull/44034)
* \[[`d23dfa4dcb`](https://github.com/nodejs/node/commit/d23dfa4dcb)] - **doc**: remove old reference from crypto/README.md (Tobias Nießen) [#44012](https://github.com/nodejs/node/pull/44012)
* \[[`222ecd6e14`](https://github.com/nodejs/node/commit/222ecd6e14)] - **doc**: add missing env vars to man page (cola119) [#43492](https://github.com/nodejs/node/pull/43492)
* \[[`374b77619b`](https://github.com/nodejs/node/commit/374b77619b)] - **doc**: list supported MODP groups explicitly (Tobias Nießen) [#43986](https://github.com/nodejs/node/pull/43986)
* \[[`72a9ecf94f`](https://github.com/nodejs/node/commit/72a9ecf94f)] - **doc**: fix typo in packages.md (Dominic Saadi) [#44005](https://github.com/nodejs/node/pull/44005)
* \[[`1b328305f0`](https://github.com/nodejs/node/commit/1b328305f0)] - **doc**: fix typos in `test.md` (Antoine du Hamel) [#43997](https://github.com/nodejs/node/pull/43997)
* \[[`7af55dbc40`](https://github.com/nodejs/node/commit/7af55dbc40)] - **doc**: add missing test runner option (Moshe Atlow) [#43989](https://github.com/nodejs/node/pull/43989)
* \[[`e8441a2864`](https://github.com/nodejs/node/commit/e8441a2864)] - **doc,report**: document special filenames (Chengzhong Wu) [#44257](https://github.com/nodejs/node/pull/44257)
* \[[`da7bc5acdf`](https://github.com/nodejs/node/commit/da7bc5acdf)] - **doc,worker**: deprecate `--trace-atomics-wait` (Keyhan Vakil) [#44093](https://github.com/nodejs/node/pull/44093)
* \[[`37a9d7a754`](https://github.com/nodejs/node/commit/37a9d7a754)] - **errors**: refactor to use optional chaining (SindreXie) [#44184](https://github.com/nodejs/node/pull/44184)
* \[[`a6dccc969f`](https://github.com/nodejs/node/commit/a6dccc969f)] - **esm**: do not bind loader hook functions (Antoine du Hamel) [#44122](https://github.com/nodejs/node/pull/44122)
* \[[`5e9c197d85`](https://github.com/nodejs/node/commit/5e9c197d85)] - **esm**: fix loader hooks accepting too many arguments (Jacob Smith) [#44109](https://github.com/nodejs/node/pull/44109)
* \[[`e072c3aa70`](https://github.com/nodejs/node/commit/e072c3aa70)] - **esm**: move package config helpers (Geoffrey Booth) [#43967](https://github.com/nodejs/node/pull/43967)
* \[[`d57178cdfc`](https://github.com/nodejs/node/commit/d57178cdfc)] - **events**: use bitset to save memory (Basit Chonka) [#43700](https://github.com/nodejs/node/pull/43700)
* \[[`4ec3f671af`](https://github.com/nodejs/node/commit/4ec3f671af)] - **fs**: add encoding parameter to benchmarks (Yagiz Nizipli) [#44278](https://github.com/nodejs/node/pull/44278)
* \[[`851264ca90`](https://github.com/nodejs/node/commit/851264ca90)] - **http**: add max for http keepalive (theanarkh) [#44217](https://github.com/nodejs/node/pull/44217)
* \[[`340ca4d8fe`](https://github.com/nodejs/node/commit/340ca4d8fe)] - **http**: fix error message when specifying headerTimeout for createServer (Nick Sia) [#44163](https://github.com/nodejs/node/pull/44163)
* \[[`c340344641`](https://github.com/nodejs/node/commit/c340344641)] - **http**: trace http request / response (theanarkh) [#44102](https://github.com/nodejs/node/pull/44102)
* \[[`a2cd8b316c`](https://github.com/nodejs/node/commit/a2cd8b316c)] - **(SEMVER-MINOR)** **http**: make idle http parser count configurable (theanarkh) [#43974](https://github.com/nodejs/node/pull/43974)
* \[[`5dc39a10bd`](https://github.com/nodejs/node/commit/5dc39a10bd)] - **http**: reuse socket only when it is drained (ywave620) [#43902](https://github.com/nodejs/node/pull/43902)
* \[[`8c2d19b2d6`](https://github.com/nodejs/node/commit/8c2d19b2d6)] - **http**: do not leak error listeners (Paolo Insogna) [#43587](https://github.com/nodejs/node/pull/43587)
* \[[`1a44fbc19e`](https://github.com/nodejs/node/commit/1a44fbc19e)] - **lib**: add diagnostics channel and perf hooks detail (Danielle Adams) [#43984](https://github.com/nodejs/node/pull/43984)
* \[[`8cfc8b0e7b`](https://github.com/nodejs/node/commit/8cfc8b0e7b)] - **lib**: refactor to avoid prototype pollution (Antoine du Hamel) [#43474](https://github.com/nodejs/node/pull/43474)
* \[[`04007f2f51`](https://github.com/nodejs/node/commit/04007f2f51)] - **lib**: fix diagnostics channel (theanarkh) [#44154](https://github.com/nodejs/node/pull/44154)
* \[[`c02bbdd921`](https://github.com/nodejs/node/commit/c02bbdd921)] - **lib**: pass env variables to child process on z/OS (alexcfyung) [#42255](https://github.com/nodejs/node/pull/42255)
* \[[`617ea4af1c`](https://github.com/nodejs/node/commit/617ea4af1c)] - **lib**: add missing env vars to --help (cola119) [#43492](https://github.com/nodejs/node/pull/43492)
* \[[`94912bb09c`](https://github.com/nodejs/node/commit/94912bb09c)] - **lib**: add `Promise` methods to `avoid-prototype-pollution` lint rule (Antoine du Hamel) [#43849](https://github.com/nodejs/node/pull/43849)
* \[[`8977a87504`](https://github.com/nodejs/node/commit/8977a87504)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#44321](https://github.com/nodejs/node/pull/44321)
* \[[`f7be92fe86`](https://github.com/nodejs/node/commit/f7be92fe86)] - **meta**: update `web streams` in label-pr-config (Daeyeon Jeong) [#44235](https://github.com/nodejs/node/pull/44235)
* \[[`2c72ded880`](https://github.com/nodejs/node/commit/2c72ded880)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#44231](https://github.com/nodejs/node/pull/44231)
* \[[`c59dc7a4c1`](https://github.com/nodejs/node/commit/c59dc7a4c1)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#44161](https://github.com/nodejs/node/pull/44161)
* \[[`e0efd9af50`](https://github.com/nodejs/node/commit/e0efd9af50)] - **meta**: add codeowner for src/node\_snapshot\* (Chengzhong Wu) [#44113](https://github.com/nodejs/node/pull/44113)
* \[[`a996f53c78`](https://github.com/nodejs/node/commit/a996f53c78)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#44065](https://github.com/nodejs/node/pull/44065)
* \[[`697dbfb174`](https://github.com/nodejs/node/commit/697dbfb174)] - **meta**: shorten PowerShell snippet for bug-report template (NicoNekoru) [#44011](https://github.com/nodejs/node/pull/44011)
* \[[`05802c2877`](https://github.com/nodejs/node/commit/05802c2877)] - **module**: protect against prototype mutation (Antoine du Hamel) [#44007](https://github.com/nodejs/node/pull/44007)
* \[[`1b3fcf765f`](https://github.com/nodejs/node/commit/1b3fcf765f)] - **(SEMVER-MINOR)** **net**: create diagnostics channels lazily (Joyee Cheung) [#38905](https://github.com/nodejs/node/pull/38905)
* \[[`aa7c053926`](https://github.com/nodejs/node/commit/aa7c053926)] - **net**: remove unused callback (theanarkh) [#44204](https://github.com/nodejs/node/pull/44204)
* \[[`b6b632c09c`](https://github.com/nodejs/node/commit/b6b632c09c)] - **(SEMVER-MINOR)** **net**: add local family (theanarkh) [#43975](https://github.com/nodejs/node/pull/43975)
* \[[`c3d87564d4`](https://github.com/nodejs/node/commit/c3d87564d4)] - **net, dns**: socket should handle its output as input (Adam Majer) [#44083](https://github.com/nodejs/node/pull/44083)
* \[[`3ba75b341b`](https://github.com/nodejs/node/commit/3ba75b341b)] - **(SEMVER-MINOR)** **net,tls**: pass a valid socket on `tlsClientError` (Daeyeon Jeong) [#44021](https://github.com/nodejs/node/pull/44021)
* \[[`0e38fba552`](https://github.com/nodejs/node/commit/0e38fba552)] - **perf\_hooks**: add resourcetiming buffer limit (Chengzhong Wu) [#44220](https://github.com/nodejs/node/pull/44220)
* \[[`b9fd240f63`](https://github.com/nodejs/node/commit/b9fd240f63)] - **perf\_hooks**: fix gc elapsed time (theanarkh) [#44058](https://github.com/nodejs/node/pull/44058)
* \[[`8cf64998e2`](https://github.com/nodejs/node/commit/8cf64998e2)] - **report**: print javascript stack on fatal error (Chengzhong Wu) [#44242](https://github.com/nodejs/node/pull/44242)
* \[[`c842ab36b6`](https://github.com/nodejs/node/commit/c842ab36b6)] - **report**: skip report if uncaught exception is handled (Chengzhong Wu) [#44208](https://github.com/nodejs/node/pull/44208)
* \[[`ab73cc8706`](https://github.com/nodejs/node/commit/ab73cc8706)] - **src**: disambiguate terms used to refer to builtins and addons (Joyee Cheung) [#44135](https://github.com/nodejs/node/pull/44135)
* \[[`e9d19ac64c`](https://github.com/nodejs/node/commit/e9d19ac64c)] - **src**: use imported namespaces in `node_contextify.cc` (Juan José) [#44299](https://github.com/nodejs/node/pull/44299)
* \[[`3dadc95cd2`](https://github.com/nodejs/node/commit/3dadc95cd2)] - **src**: refactor to avoid using a moved object (Tobias Nießen) [#44269](https://github.com/nodejs/node/pull/44269)
* \[[`3765c6335b`](https://github.com/nodejs/node/commit/3765c6335b)] - **src**: extract common context embedder tag checks (Chengzhong Wu) [#44258](https://github.com/nodejs/node/pull/44258)
* \[[`d2dce59729`](https://github.com/nodejs/node/commit/d2dce59729)] - **src**: avoid copying BaseObjectPtrs in loop (Tobias Nießen) [#44270](https://github.com/nodejs/node/pull/44270)
* \[[`9614907104`](https://github.com/nodejs/node/commit/9614907104)] - **src**: remove usage on ScriptCompiler::CompileFunctionInContext (Chengzhong Wu) [#44198](https://github.com/nodejs/node/pull/44198)
* \[[`4e1ffd932e`](https://github.com/nodejs/node/commit/4e1ffd932e)] - **src**: fix --heapsnapshot-near-heap-limit error hint (Chengzhong Wu) [#44216](https://github.com/nodejs/node/pull/44216)
* \[[`960a20928f`](https://github.com/nodejs/node/commit/960a20928f)] - **src**: prevent copying ArrayBufferViewContents (Keyhan Vakil) [#44091](https://github.com/nodejs/node/pull/44091)
* \[[`4755ad5495`](https://github.com/nodejs/node/commit/4755ad5495)] - **src**: remove usages of GetBackingStore in crypto (Keyhan Vakil) [#44079](https://github.com/nodejs/node/pull/44079)
* \[[`a2022e5aff`](https://github.com/nodejs/node/commit/a2022e5aff)] - **src**: remove unowned usages of GetBackingStore (Keyhan Vakil) [#44080](https://github.com/nodejs/node/pull/44080)
* \[[`8e1b7e2b8f`](https://github.com/nodejs/node/commit/8e1b7e2b8f)] - **src**: remove usages of GetBackingStore in node-api (Keyhan Vakil) [#44075](https://github.com/nodejs/node/pull/44075)
* \[[`cddf3eda28`](https://github.com/nodejs/node/commit/cddf3eda28)] - **src**: remove usages of GetBackingStore in modules (Keyhan Vakil) [#44076](https://github.com/nodejs/node/pull/44076)
* \[[`a54e4d4170`](https://github.com/nodejs/node/commit/a54e4d4170)] - **src**: remove usages of GetBackingStore in WASI (Keyhan Vakil) [#44077](https://github.com/nodejs/node/pull/44077)
* \[[`38cdb1f9b6`](https://github.com/nodejs/node/commit/38cdb1f9b6)] - **src**: remove usages of GetBackingStore in startup (Keyhan Vakil) [#44078](https://github.com/nodejs/node/pull/44078)
* \[[`c4783e37d7`](https://github.com/nodejs/node/commit/c4783e37d7)] - **src**: nest namespace report in namespace node (Chengzhong Wu) [#44069](https://github.com/nodejs/node/pull/44069)
* \[[`04bcdf63a0`](https://github.com/nodejs/node/commit/04bcdf63a0)] - **src**: use a typed array internally for process.\_exiting (Darshan Sen) [#43883](https://github.com/nodejs/node/pull/43883)
* \[[`b90b8abdd5`](https://github.com/nodejs/node/commit/b90b8abdd5)] - **src**: fix bug in GetErrorSource() (Tobias Nießen) [#44019](https://github.com/nodejs/node/pull/44019)
* \[[`728e18e025`](https://github.com/nodejs/node/commit/728e18e025)] - **src**: fix to use replacement character (Kohei Ueno) [#43999](https://github.com/nodejs/node/pull/43999)
* \[[`cc6e0fc8ff`](https://github.com/nodejs/node/commit/cc6e0fc8ff)] - **src**: improve SPKAC::ExportChallenge() (Tobias Nießen) [#44002](https://github.com/nodejs/node/pull/44002)
* \[[`9763e2fba9`](https://github.com/nodejs/node/commit/9763e2fba9)] - **src**: fix typo in src/README.md (Anna Henningsen) [#44009](https://github.com/nodejs/node/pull/44009)
* \[[`460397709b`](https://github.com/nodejs/node/commit/460397709b)] - **src**: remove unnecessary cast in crypto\_sig.cc (Tobias Nießen) [#44001](https://github.com/nodejs/node/pull/44001)
* \[[`68ee8e9089`](https://github.com/nodejs/node/commit/68ee8e9089)] - **src**: split property helpers from node::Environment (Chengzhong Wu) [#44056](https://github.com/nodejs/node/pull/44056)
* \[[`9990dc7d18`](https://github.com/nodejs/node/commit/9990dc7d18)] - **src,buffer**: remove unused chars\_written parameter (Keyhan Vakil) [#44092](https://github.com/nodejs/node/pull/44092)
* \[[`ecf82186e0`](https://github.com/nodejs/node/commit/ecf82186e0)] - **src,fs**: refactor duplicated code in fs.readdir (Daeyeon Jeong) [#43204](https://github.com/nodejs/node/pull/43204)
* \[[`ee6412a992`](https://github.com/nodejs/node/commit/ee6412a992)] - **src,lib**: print prinstine source when source map source not found (Chengzhong Wu) [#44052](https://github.com/nodejs/node/pull/44052)
* \[[`4249276783`](https://github.com/nodejs/node/commit/4249276783)] - **(SEMVER-MINOR)** **src,lib**: print source map error source on demand (Chengzhong Wu) [#43875](https://github.com/nodejs/node/pull/43875)
* \[[`1dabdbf05c`](https://github.com/nodejs/node/commit/1dabdbf05c)] - **src,test**: fix typos (SADIK KUZU) [#44110](https://github.com/nodejs/node/pull/44110)
* \[[`a3ac445198`](https://github.com/nodejs/node/commit/a3ac445198)] - **stream**: fix `isDetachedBuffer` validations (Daeyeon Jeong) [#44114](https://github.com/nodejs/node/pull/44114)
* \[[`c079abe017`](https://github.com/nodejs/node/commit/c079abe017)] - **stream**: improve views validation on `BYOBRequest` (Daeyeon Jeong) [#44155](https://github.com/nodejs/node/pull/44155)
* \[[`2f904bc8bf`](https://github.com/nodejs/node/commit/2f904bc8bf)] - **stream**: update TextEncoderStream to align the latest spec (Kohei Ueno) [#44101](https://github.com/nodejs/node/pull/44101)
* \[[`40b817cfb9`](https://github.com/nodejs/node/commit/40b817cfb9)] - **(SEMVER-MINOR)** **test**: test snapshotting TypeScript compiler (Joyee Cheung) [#38905](https://github.com/nodejs/node/pull/38905)
* \[[`d4189ab609`](https://github.com/nodejs/node/commit/d4189ab609)] - **(SEMVER-MINOR)** **test**: add UMD module test with marked (Joyee Cheung) [#38905](https://github.com/nodejs/node/pull/38905)
* \[[`514e5162d2`](https://github.com/nodejs/node/commit/514e5162d2)] - **test**: deflake test-diagnostics-channel-net (Keyhan Vakil) [#44144](https://github.com/nodejs/node/pull/44144)
* \[[`a2707d0f48`](https://github.com/nodejs/node/commit/a2707d0f48)] - **test**: add coverage for invalid RSA-PSS digests (Tobias Nießen) [#44271](https://github.com/nodejs/node/pull/44271)
* \[[`7b6126a59a`](https://github.com/nodejs/node/commit/7b6126a59a)] - **test**: update Web Streams WPT (Daeyeon Jeong) [#44234](https://github.com/nodejs/node/pull/44234)
* \[[`a02492f96c`](https://github.com/nodejs/node/commit/a02492f96c)] - **test**: move "errors" test to "parallel" (Michaël Zasso) [#44233](https://github.com/nodejs/node/pull/44233)
* \[[`b4224dd192`](https://github.com/nodejs/node/commit/b4224dd192)] - **test**: reduce http-server-request-timeouts-mixed flakiness (Nick Sia) [#44169](https://github.com/nodejs/node/pull/44169)
* \[[`f5e2f6c362`](https://github.com/nodejs/node/commit/f5e2f6c362)] - **test**: remove cjs loader from stack traces (Geoffrey Booth) [#44197](https://github.com/nodejs/node/pull/44197)
* \[[`e37314497a`](https://github.com/nodejs/node/commit/e37314497a)] - **test**: add filesystem check to `test-fs-stat-date.mjs` (Livia Medeiros) [#44174](https://github.com/nodejs/node/pull/44174)
* \[[`9755b1f979`](https://github.com/nodejs/node/commit/9755b1f979)] - **test**: mark connection leak test flaky on IBM i (Richard Lau) [#44215](https://github.com/nodejs/node/pull/44215)
* \[[`beaf5f5776`](https://github.com/nodejs/node/commit/beaf5f5776)] - **test**: use `mustSucceed` instead of `mustCall` with `assert.ifError` (MURAKAMI Masahiko) [#44196](https://github.com/nodejs/node/pull/44196)
* \[[`11f74e72a7`](https://github.com/nodejs/node/commit/11f74e72a7)] - **test**: update WPT runner (Filip Skokan) [#43455](https://github.com/nodejs/node/pull/43455)
* \[[`b2a15b6275`](https://github.com/nodejs/node/commit/b2a15b6275)] - **test**: update wpt url status (Kohei Ueno) [#44175](https://github.com/nodejs/node/pull/44175)
* \[[`6b84451d70`](https://github.com/nodejs/node/commit/6b84451d70)] - **test**: update wasm/jsapi web platform tests (Yagiz Nizipli) [#44100](https://github.com/nodejs/node/pull/44100)
* \[[`537d52fa0f`](https://github.com/nodejs/node/commit/537d52fa0f)] - **test**: update hr-time web platform tests (Yagiz Nizipli) [#44100](https://github.com/nodejs/node/pull/44100)
* \[[`79445cb215`](https://github.com/nodejs/node/commit/79445cb215)] - **test**: update console web platform tests (Yagiz Nizipli) [#44100](https://github.com/nodejs/node/pull/44100)
* \[[`70267a0eeb`](https://github.com/nodejs/node/commit/70267a0eeb)] - **test**: move tests with many workers to sequential (Keyhan Vakil) [#44139](https://github.com/nodejs/node/pull/44139)
* \[[`86a7fb0c8a`](https://github.com/nodejs/node/commit/86a7fb0c8a)] - **test**: deflake gc-http-client tests by restricting number of requests (Nick Sia) [#44146](https://github.com/nodejs/node/pull/44146)
* \[[`e17117dfda`](https://github.com/nodejs/node/commit/e17117dfda)] - **test**: move test-vm-break-on-sigint to sequential (Keyhan Vakil) [#44140](https://github.com/nodejs/node/pull/44140)
* \[[`e5113fab05`](https://github.com/nodejs/node/commit/e5113fab05)] - **test**: remove test-http-client-response-timeout flaky designation (Luigi Pinca) [#44145](https://github.com/nodejs/node/pull/44145)
* \[[`f1b5f933d7`](https://github.com/nodejs/node/commit/f1b5f933d7)] - **test**: s390x z15 accelerated zlib fixes (Adam Majer) [#44117](https://github.com/nodejs/node/pull/44117)
* \[[`86bbd5e61a`](https://github.com/nodejs/node/commit/86bbd5e61a)] - **test**: tune down parallelism for some flaky tests (Keyhan Vakil) [#44090](https://github.com/nodejs/node/pull/44090)
* \[[`40e2ca7f66`](https://github.com/nodejs/node/commit/40e2ca7f66)] - **test**: fix `internet/test-inspector-help-page` (Daeyeon Jeong) [#44025](https://github.com/nodejs/node/pull/44025)
* \[[`b19564b9d2`](https://github.com/nodejs/node/commit/b19564b9d2)] - **test**: refactor ESM tests to improve performance (Jacob Smith) [#43784](https://github.com/nodejs/node/pull/43784)
* \[[`d964b308ae`](https://github.com/nodejs/node/commit/d964b308ae)] - **test**: remove test-gc-http-client-timeout from flaky list (Feng Yu) [#43971](https://github.com/nodejs/node/pull/43971)
* \[[`2cab7bb791`](https://github.com/nodejs/node/commit/2cab7bb791)] - **test**: reduce loop times for preventing test from timeout (theanarkh) [#43981](https://github.com/nodejs/node/pull/43981)
* \[[`9244d6d416`](https://github.com/nodejs/node/commit/9244d6d416)] - **test**: fix test-cluster-concurrent-disconnect (Daeyeon Jeong) [#43961](https://github.com/nodejs/node/pull/43961)
* \[[`3c8037a9fa`](https://github.com/nodejs/node/commit/3c8037a9fa)] - **test**: change misleading variable name (Tobias Nießen) [#43990](https://github.com/nodejs/node/pull/43990)
* \[[`82164344e2`](https://github.com/nodejs/node/commit/82164344e2)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#44223](https://github.com/nodejs/node/pull/44223)
* \[[`c0b160c842`](https://github.com/nodejs/node/commit/c0b160c842)] - **test\_runner**: fix test runner hooks failure stack (Moshe Atlow) [#44284](https://github.com/nodejs/node/pull/44284)
* \[[`8ed39397d5`](https://github.com/nodejs/node/commit/8ed39397d5)] - **test\_runner**: refactor to use more primordials (Antoine du Hamel) [#44062](https://github.com/nodejs/node/pull/44062)
* \[[`d8749c3b87`](https://github.com/nodejs/node/commit/d8749c3b87)] - **test\_runner**: verbous error when entire test tree is canceled (Moshe Atlow) [#44060](https://github.com/nodejs/node/pull/44060)
* \[[`0d007471fa`](https://github.com/nodejs/node/commit/0d007471fa)] - **test\_runner**: empty pending tests queue post running (Moshe Atlow) [#44059](https://github.com/nodejs/node/pull/44059)
* \[[`c3fa82f007`](https://github.com/nodejs/node/commit/c3fa82f007)] - **test\_runner**: add before/after/each hooks (Moshe Atlow) [#43730](https://github.com/nodejs/node/pull/43730)
* \[[`50c854bbfe`](https://github.com/nodejs/node/commit/50c854bbfe)] - **test\_runner**: fix top level `describe` queuing (Moshe Atlow) [#43998](https://github.com/nodejs/node/pull/43998)
* \[[`04fdc3e1fa`](https://github.com/nodejs/node/commit/04fdc3e1fa)] - **test\_runner**: graceful termination on `--test` only (Moshe Atlow) [#43977](https://github.com/nodejs/node/pull/43977)
* \[[`51a0310398`](https://github.com/nodejs/node/commit/51a0310398)] - **test\_runner**: validate `concurrency` option (Antoine du Hamel) [#43976](https://github.com/nodejs/node/pull/43976)
* \[[`ecf7b0720a`](https://github.com/nodejs/node/commit/ecf7b0720a)] - **tls**: use logical OR operator (Mohammed Keyvanzadeh) [#44236](https://github.com/nodejs/node/pull/44236)
* \[[`f7c1b838ba`](https://github.com/nodejs/node/commit/f7c1b838ba)] - **tools**: update lint-md-dependencies to rollup\@2.78.1 (Node.js GitHub Bot) [#44320](https://github.com/nodejs/node/pull/44320)
* \[[`36b39db74d`](https://github.com/nodejs/node/commit/36b39db74d)] - **tools**: update ESLint to 8.22.0 (Luigi Pinca) [#44243](https://github.com/nodejs/node/pull/44243)
* \[[`87f75a27fb`](https://github.com/nodejs/node/commit/87f75a27fb)] - **tools**: update lint-md-dependencies to rollup\@2.78.0 (Node.js GitHub Bot) [#44244](https://github.com/nodejs/node/pull/44244)
* \[[`a3cc8ce959`](https://github.com/nodejs/node/commit/a3cc8ce959)] - **tools**: update lint-md-dependencies to rollup\@2.77.3 (Node.js GitHub Bot) [#44230](https://github.com/nodejs/node/pull/44230)
* \[[`873941a43e`](https://github.com/nodejs/node/commit/873941a43e)] - **tools**: update eslint to 8.21.0 (Node.js GitHub Bot) [#44162](https://github.com/nodejs/node/pull/44162)
* \[[`6be7e6d136`](https://github.com/nodejs/node/commit/6be7e6d136)] - **tools**: update lint-md-dependencies to @rollup/plugin-commonjs\@22.0.2 (Node.js GitHub Bot) [#44160](https://github.com/nodejs/node/pull/44160)
* \[[`b252f389d7`](https://github.com/nodejs/node/commit/b252f389d7)] - **tools**: update undici CPE in vuln checking script (Facundo Tuesca) [#44128](https://github.com/nodejs/node/pull/44128)
* \[[`3eacf25789`](https://github.com/nodejs/node/commit/3eacf25789)] - **tools**: update lint-md-dependencies to rollup\@2.77.2 (Node.js GitHub Bot) [#44064](https://github.com/nodejs/node/pull/44064)
* \[[`1175d9036a`](https://github.com/nodejs/node/commit/1175d9036a)] - **tools**: add verbose flag to find-inactive-collaborators (Rich Trott) [#43964](https://github.com/nodejs/node/pull/43964)
* \[[`2cf3ce83d8`](https://github.com/nodejs/node/commit/2cf3ce83d8)] - **trace\_events**: add example (theanarkh) [#43253](https://github.com/nodejs/node/pull/43253)
* \[[`2efce0fe5b`](https://github.com/nodejs/node/commit/2efce0fe5b)] - **typings**: add JSDoc for `internal/validators` (Yagiz Nizipli) [#44181](https://github.com/nodejs/node/pull/44181)

<a id="18.7.0"></a>

## 2022-07-26, Version 18.7.0 (Current), @danielleadams

### Notable changes

* **doc**:
  * add F3n67u to collaborators (Feng Yu) [#43953](https://github.com/nodejs/node/pull/43953)
  * deprecate coercion to integer in process.exit (Daeyeon Jeong) [#43738](https://github.com/nodejs/node/pull/43738)
  * **(SEMVER-MINOR)** deprecate diagnostics\_channel object subscribe method (Stephen Belanger) [#42714](https://github.com/nodejs/node/pull/42714)
* **events**:
  * **(SEMVER-MINOR)** expose CustomEvent on global with CLI flag (Daeyeon Jeong) [#43885](https://github.com/nodejs/node/pull/43885)
  * **(SEMVER-MINOR)** add `CustomEvent` (Daeyeon Jeong) [#43514](https://github.com/nodejs/node/pull/43514)
* **http**:
  * **(SEMVER-MINOR)** add drop request event for http server (theanarkh) [#43806](https://github.com/nodejs/node/pull/43806)
* **lib**:
  * **(SEMVER-MINOR)** improved diagnostics\_channel subscribe/unsubscribe (Stephen Belanger) [#42714](https://github.com/nodejs/node/pull/42714)
* **util**:
  * **(SEMVER-MINOR)** add tokens to parseArgs (John Gee) [#43459](https://github.com/nodejs/node/pull/43459)

### Commits

* \[[`0aa255ab72`](https://github.com/nodejs/node/commit/0aa255ab72)] - **bootstrap**: handle snapshot errors gracefully (Joyee Cheung) [#43531](https://github.com/nodejs/node/pull/43531)
* \[[`0783ddf57e`](https://github.com/nodejs/node/commit/0783ddf57e)] - **buffer**: do not leak memory if buffer is too big (Keyhan Vakil) [#43938](https://github.com/nodejs/node/pull/43938)
* \[[`12657accdd`](https://github.com/nodejs/node/commit/12657accdd)] - **build**: add .gitattributes for npm and other shims (Hrishikesh Kadam) [#43879](https://github.com/nodejs/node/pull/43879)
* \[[`c2db4f4581`](https://github.com/nodejs/node/commit/c2db4f4581)] - **build**: make GitPod less noisy (Rich Trott) [#43829](https://github.com/nodejs/node/pull/43829)
* \[[`364deeadcd`](https://github.com/nodejs/node/commit/364deeadcd)] - **build**: add GitHub token permissions for workflows (Varun Sharma) [#43743](https://github.com/nodejs/node/pull/43743)
* \[[`8b83b4d5be`](https://github.com/nodejs/node/commit/8b83b4d5be)] - **child\_process**: do not need to count length when maxBuffer is Infinity (theanarkh) [#43822](https://github.com/nodejs/node/pull/43822)
* \[[`c1893b7a7c`](https://github.com/nodejs/node/commit/c1893b7a7c)] - **child\_process**: avoid repeated calls to `normalizeSpawnArguments` (木杉) [#43345](https://github.com/nodejs/node/pull/43345)
* \[[`7b276b89b9`](https://github.com/nodejs/node/commit/7b276b89b9)] - **cluster**: send connection to other server when worker drop it (theanarkh) [#43747](https://github.com/nodejs/node/pull/43747)
* \[[`e8c66f92a5`](https://github.com/nodejs/node/commit/e8c66f92a5)] - **crypto**: remove unneeded guard (Rich Trott) [#43856](https://github.com/nodejs/node/pull/43856)
* \[[`c95132e9ea`](https://github.com/nodejs/node/commit/c95132e9ea)] - **deps**: cherry-pick libuv/libuv\@3a7b955 (Ben Noordhuis) [#43950](https://github.com/nodejs/node/pull/43950)
* \[[`cc8d5426d2`](https://github.com/nodejs/node/commit/cc8d5426d2)] - **deps**: cherry-pick libuv/libuv\@abb109f (Ben Noordhuis) [#43950](https://github.com/nodejs/node/pull/43950)
* \[[`7762e463d6`](https://github.com/nodejs/node/commit/7762e463d6)] - **deps**: update corepack to 0.12.1 (Node.js GitHub Bot) [#43965](https://github.com/nodejs/node/pull/43965)
* \[[`1256c4dad5`](https://github.com/nodejs/node/commit/1256c4dad5)] - **deps**: update hast-util-raw (Moshe Atlow) [#43927](https://github.com/nodejs/node/pull/43927)
* \[[`aac97c2d2a`](https://github.com/nodejs/node/commit/aac97c2d2a)] - **deps**: update undici to 5.8.0 (Node.js GitHub Bot) [#43886](https://github.com/nodejs/node/pull/43886)
* \[[`cdff61917d`](https://github.com/nodejs/node/commit/cdff61917d)] - **deps**: clean archs files for OpenSSL (RafaelGSS) [#43735](https://github.com/nodejs/node/pull/43735)
* \[[`fc936a84e4`](https://github.com/nodejs/node/commit/fc936a84e4)] - **deps**: remove not used architectures (RafaelGSS) [#43735](https://github.com/nodejs/node/pull/43735)
* \[[`361a643d8b`](https://github.com/nodejs/node/commit/361a643d8b)] - **deps**: V8: backport f3cad8cec656 (Joyee Cheung) [#43531](https://github.com/nodejs/node/pull/43531)
* \[[`2e1732ebd0`](https://github.com/nodejs/node/commit/2e1732ebd0)] - **deps**: V8: backport 22698d267667 (Chengzhong Wu) [#43751](https://github.com/nodejs/node/pull/43751)
* \[[`979f469d3a`](https://github.com/nodejs/node/commit/979f469d3a)] - **deps**: upgrade npm to 8.15.0 (npm team) [#43917](https://github.com/nodejs/node/pull/43917)
* \[[`4096d81988`](https://github.com/nodejs/node/commit/4096d81988)] - **deps**: upgrade npm to 8.14.0 (npm team) [#43826](https://github.com/nodejs/node/pull/43826)
* \[[`2ec8092e2c`](https://github.com/nodejs/node/commit/2ec8092e2c)] - **deps,src**: use SIMD for normal base64 encoding (Brian White) [#39775](https://github.com/nodejs/node/pull/39775)
* \[[`67b4edde37`](https://github.com/nodejs/node/commit/67b4edde37)] - **dns**: fix getServers return undefined (jiahao.si) [#43922](https://github.com/nodejs/node/pull/43922)
* \[[`7c75539a88`](https://github.com/nodejs/node/commit/7c75539a88)] - **dns**: fix cares memory leak (theanarkh) [#43912](https://github.com/nodejs/node/pull/43912)
* \[[`1f80b88da5`](https://github.com/nodejs/node/commit/1f80b88da5)] - **doc**: update email and mailmap for BethGriggs (Beth Griggs) [#43985](https://github.com/nodejs/node/pull/43985)
* \[[`8a2a6e16eb`](https://github.com/nodejs/node/commit/8a2a6e16eb)] - **doc**: add 15.x - 18.x to Other Versions section (shhh7612) [#43940](https://github.com/nodejs/node/pull/43940)
* \[[`51cb0d42ca`](https://github.com/nodejs/node/commit/51cb0d42ca)] - **doc**: inspector.close undefined in worker threads (Keyhan Vakil) [#43867](https://github.com/nodejs/node/pull/43867)
* \[[`c789c0f5f7`](https://github.com/nodejs/node/commit/c789c0f5f7)] - **doc**: improve documentation for safe `Promise` statics alternatives (Antoine du Hamel) [#43759](https://github.com/nodejs/node/pull/43759)
* \[[`cb9b0e0011`](https://github.com/nodejs/node/commit/cb9b0e0011)] - **doc**: recommend git-node-v8 (Keyhan Vakil) [#43934](https://github.com/nodejs/node/pull/43934)
* \[[`d7e9bd1830`](https://github.com/nodejs/node/commit/d7e9bd1830)] - **doc**: clarify subprocess.stdout/in/err property (Kohei Ueno) [#43910](https://github.com/nodejs/node/pull/43910)
* \[[`808793ebb5`](https://github.com/nodejs/node/commit/808793ebb5)] - **doc**: fix typo in `src/crypto/README.md` (Jianru Lin) [#43968](https://github.com/nodejs/node/pull/43968)
* \[[`bbc455c4f9`](https://github.com/nodejs/node/commit/bbc455c4f9)] - **doc**: remind backporter about v8\_embedder\_string (Keyhan Vakil) [#43924](https://github.com/nodejs/node/pull/43924)
* \[[`a86b66c8b4`](https://github.com/nodejs/node/commit/a86b66c8b4)] - **doc**: fix typo in http.md (Airing) [#43933](https://github.com/nodejs/node/pull/43933)
* \[[`a96af37233`](https://github.com/nodejs/node/commit/a96af37233)] - **doc**: add F3n67u to collaborators (Feng Yu) [#43953](https://github.com/nodejs/node/pull/43953)
* \[[`aa7d4e59f7`](https://github.com/nodejs/node/commit/aa7d4e59f7)] - **doc**: improve test runner timeout docs (Tobias Nießen) [#43836](https://github.com/nodejs/node/pull/43836)
* \[[`80c2fa8212`](https://github.com/nodejs/node/commit/80c2fa8212)] - **doc**: mention Win 32-bit openssl build issue (RafaelGSS) [#43853](https://github.com/nodejs/node/pull/43853)
* \[[`8b8c55df7e`](https://github.com/nodejs/node/commit/8b8c55df7e)] - **doc**: add security release specifics to releases.md (Beth Griggs) [#43835](https://github.com/nodejs/node/pull/43835)
* \[[`42693aaf9f`](https://github.com/nodejs/node/commit/42693aaf9f)] - **doc**: add history info for `global.performance` (Antoine du Hamel) [#43841](https://github.com/nodejs/node/pull/43841)
* \[[`140d6af572`](https://github.com/nodejs/node/commit/140d6af572)] - **doc**: add platform-windows-arm to who to CC (Michael Dawson) [#43808](https://github.com/nodejs/node/pull/43808)
* \[[`976093efe3`](https://github.com/nodejs/node/commit/976093efe3)] - **doc**: document ES2022's Error "cause" property (James Ide) [#43830](https://github.com/nodejs/node/pull/43830)
* \[[`ec7e45e4a2`](https://github.com/nodejs/node/commit/ec7e45e4a2)] - **doc**: include make clean to openssl arch (RafaelGSS) [#43735](https://github.com/nodejs/node/pull/43735)
* \[[`d64dfd53c9`](https://github.com/nodejs/node/commit/d64dfd53c9)] - **doc**: add link to diagnostic tools (Rafael Gonzaga) [#43736](https://github.com/nodejs/node/pull/43736)
* \[[`2910136920`](https://github.com/nodejs/node/commit/2910136920)] - **doc**: update links to MDN page about dynamic imports (Jannis R) [#43847](https://github.com/nodejs/node/pull/43847)
* \[[`d88a9fae79`](https://github.com/nodejs/node/commit/d88a9fae79)] - **doc**: deprecate coercion to integer in process.exit (Daeyeon Jeong) [#43738](https://github.com/nodejs/node/pull/43738)
* \[[`fc843e103d`](https://github.com/nodejs/node/commit/fc843e103d)] - **doc**: add MoLow to triagers (Moshe Atlow) [#43799](https://github.com/nodejs/node/pull/43799)
* \[[`8c8c97da61`](https://github.com/nodejs/node/commit/8c8c97da61)] - **(SEMVER-MINOR)** **doc**: deprecate diagnostics\_channel object subscribe method (Stephen Belanger) [#42714](https://github.com/nodejs/node/pull/42714)
* \[[`9b53a694b5`](https://github.com/nodejs/node/commit/9b53a694b5)] - **doc**: revert anachronistic 'node:' module require()s in API history notes (DeeDeeG) [#43768](https://github.com/nodejs/node/pull/43768)
* \[[`2815bd3002`](https://github.com/nodejs/node/commit/2815bd3002)] - **doc**: clarify release process for new releasers (Rafael Gonzaga) [#43739](https://github.com/nodejs/node/pull/43739)
* \[[`50b3750e67`](https://github.com/nodejs/node/commit/50b3750e67)] - **doc**: fix typo in ngtcp2 readme (Dan Castillo) [#43767](https://github.com/nodejs/node/pull/43767)
* \[[`6bcd40dd85`](https://github.com/nodejs/node/commit/6bcd40dd85)] - **domain**: fix vm promise tracking while keeping isolation (Stephen Belanger) [#43556](https://github.com/nodejs/node/pull/43556)
* \[[`e89e0b470b`](https://github.com/nodejs/node/commit/e89e0b470b)] - **esm**: remove superfluous argument (Rich Trott) [#43884](https://github.com/nodejs/node/pull/43884)
* \[[`0d2921f396`](https://github.com/nodejs/node/commit/0d2921f396)] - **esm**: fix erroneous re-initialization of ESMLoader (Jacob Smith) [#43763](https://github.com/nodejs/node/pull/43763)
* \[[`9b5b8d78c3`](https://github.com/nodejs/node/commit/9b5b8d78c3)] - **esm**: throw on any non-2xx response (LiviaMedeiros) [#43742](https://github.com/nodejs/node/pull/43742)
* \[[`dfc4832ef1`](https://github.com/nodejs/node/commit/dfc4832ef1)] - **(SEMVER-MINOR)** **events**: expose CustomEvent on global with CLI flag (Daeyeon Jeong) [#43885](https://github.com/nodejs/node/pull/43885)
* \[[`e4473952ae`](https://github.com/nodejs/node/commit/e4473952ae)] - **(SEMVER-MINOR)** **events**: add `CustomEvent` (Daeyeon Jeong) [#43514](https://github.com/nodejs/node/pull/43514)
* \[[`100f6deb09`](https://github.com/nodejs/node/commit/100f6deb09)] - **fs**: use signed types for stat data (LiviaMedeiros) [#43714](https://github.com/nodejs/node/pull/43714)
* \[[`25ec71db63`](https://github.com/nodejs/node/commit/25ec71db63)] - **http**: fix http server connection list when close (theanarkh) [#43949](https://github.com/nodejs/node/pull/43949)
* \[[`ca658c8afe`](https://github.com/nodejs/node/commit/ca658c8afe)] - **(SEMVER-MINOR)** **http**: add drop request event for http server (theanarkh) [#43806](https://github.com/nodejs/node/pull/43806)
* \[[`9c699bd8a8`](https://github.com/nodejs/node/commit/9c699bd8a8)] - **http**: wait for pending responses in closeIdleConnections (Paolo Insogna) [#43890](https://github.com/nodejs/node/pull/43890)
* \[[`781d5e54e3`](https://github.com/nodejs/node/commit/781d5e54e3)] - **inspector**: set sampling interval before start (Shelley Vohr) [#43779](https://github.com/nodejs/node/pull/43779)
* \[[`0b5dbb2a56`](https://github.com/nodejs/node/commit/0b5dbb2a56)] - **lib**: refactor PriorityQueue to use private field (Finn Yu) [#43889](https://github.com/nodejs/node/pull/43889)
* \[[`324473ca32`](https://github.com/nodejs/node/commit/324473ca32)] - **(SEMVER-MINOR)** **lib**: improved diagnostics\_channel subscribe/unsubscribe (Stephen Belanger) [#42714](https://github.com/nodejs/node/pull/42714)
* \[[`5aa3b213ac`](https://github.com/nodejs/node/commit/5aa3b213ac)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#43966](https://github.com/nodejs/node/pull/43966)
* \[[`e707552357`](https://github.com/nodejs/node/commit/e707552357)] - **meta**: update `node-api` in label-pr-config (Daeyeon Jeong) [#43794](https://github.com/nodejs/node/pull/43794)
* \[[`8a8de94034`](https://github.com/nodejs/node/commit/8a8de94034)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#43872](https://github.com/nodejs/node/pull/43872)
* \[[`7d49fc766c`](https://github.com/nodejs/node/commit/7d49fc766c)] - **meta**: use platform dropdown on flaky template (Rafael Gonzaga) [#43855](https://github.com/nodejs/node/pull/43855)
* \[[`e4aa50fc3f`](https://github.com/nodejs/node/commit/e4aa50fc3f)] - **meta**: enable blank issues (Matteo Collina) [#43775](https://github.com/nodejs/node/pull/43775)
* \[[`ceb7c150ec`](https://github.com/nodejs/node/commit/ceb7c150ec)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#43770](https://github.com/nodejs/node/pull/43770)
* \[[`29bcd47738`](https://github.com/nodejs/node/commit/29bcd47738)] - **net**: fix socket.\_getpeername (Daeyeon Jeong) [#43010](https://github.com/nodejs/node/pull/43010)
* \[[`380659daf1`](https://github.com/nodejs/node/commit/380659daf1)] - **process**: use `defineProperty` instead of assignment (Mark S. Miller) [#43907](https://github.com/nodejs/node/pull/43907)
* \[[`aba9c8ebea`](https://github.com/nodejs/node/commit/aba9c8ebea)] - **repl**: fix overzealous top-level await (Tobias Nießen) [#43827](https://github.com/nodejs/node/pull/43827)
* \[[`1deb6b73b7`](https://github.com/nodejs/node/commit/1deb6b73b7)] - **repl**: use `SafePromiseAll` and `SafePromiseRace` (Antoine du Hamel) [#43758](https://github.com/nodejs/node/pull/43758)
* \[[`bf8f2e23ff`](https://github.com/nodejs/node/commit/bf8f2e23ff)] - **src**: refactor DH groups to delete crypto\_groups.h (Tobias Nießen) [#43896](https://github.com/nodejs/node/pull/43896)
* \[[`9435fbf8cd`](https://github.com/nodejs/node/commit/9435fbf8cd)] - **src**: remove dead code in base64\_encode (Tobias Nießen) [#43979](https://github.com/nodejs/node/pull/43979)
* \[[`2c47e58ea0`](https://github.com/nodejs/node/commit/2c47e58ea0)] - **src**: fix regression that a source marker is lost (cola119) [#43086](https://github.com/nodejs/node/pull/43086)
* \[[`d084150320`](https://github.com/nodejs/node/commit/d084150320)] - **src**: per-isolate eternal template properties (Chengzhong Wu) [#43802](https://github.com/nodejs/node/pull/43802)
* \[[`9f9d00ccbb`](https://github.com/nodejs/node/commit/9f9d00ccbb)] - **src**: merge NativeModuleEnv into NativeModuleLoader (Joyee Cheung) [#43824](https://github.com/nodejs/node/pull/43824)
* \[[`bb512904e9`](https://github.com/nodejs/node/commit/bb512904e9)] - **src**: use named struct instead of typedef (Tobias Nießen) [#43881](https://github.com/nodejs/node/pull/43881)
* \[[`bb5511e8cc`](https://github.com/nodejs/node/commit/bb5511e8cc)] - **src**: use named enum instead of typedef (Tobias Nießen) [#43880](https://github.com/nodejs/node/pull/43880)
* \[[`5db0c8f667`](https://github.com/nodejs/node/commit/5db0c8f667)] - **src**: pass only Isolate\* and env\_vars to EnabledDebugList::Parse() (Darshan Sen) [#43668](https://github.com/nodejs/node/pull/43668)
* \[[`249365524e`](https://github.com/nodejs/node/commit/249365524e)] - **src**: fix node watchdog race condition (theanarkh) [#43780](https://github.com/nodejs/node/pull/43780)
* \[[`17cb27237d`](https://github.com/nodejs/node/commit/17cb27237d)] - **src**: deduplicate `SetALPN` implementations (Tobias Nießen) [#43756](https://github.com/nodejs/node/pull/43756)
* \[[`b4c75a96be`](https://github.com/nodejs/node/commit/b4c75a96be)] - **src**: fix `napi_check_object_type_tag()` (Daeyeon Jeong) [#43788](https://github.com/nodejs/node/pull/43788)
* \[[`8432d6596f`](https://github.com/nodejs/node/commit/8432d6596f)] - **src**: slim down env-inl.h (Ben Noordhuis) [#43745](https://github.com/nodejs/node/pull/43745)
* \[[`2266a4b6d6`](https://github.com/nodejs/node/commit/2266a4b6d6)] - **stream**: improve `respondWithNewView()` (Daeyeon Jeong) [#43866](https://github.com/nodejs/node/pull/43866)
* \[[`bf3991b406`](https://github.com/nodejs/node/commit/bf3991b406)] - **stream**: fix 0 transform hwm backpressure (Robert Nagy) [#43685](https://github.com/nodejs/node/pull/43685)
* \[[`a057510037`](https://github.com/nodejs/node/commit/a057510037)] - **stream**: initial approach to include strategy options on Readable.toWeb() (txxnano) [#43515](https://github.com/nodejs/node/pull/43515)
* \[[`198cf59d2c`](https://github.com/nodejs/node/commit/198cf59d2c)] - **test**: update WPT encoding tests (Kohei Ueno) [#43958](https://github.com/nodejs/node/pull/43958)
* \[[`f0ed1aed8d`](https://github.com/nodejs/node/commit/f0ed1aed8d)] - **test**: remove test-whatwg-events-add-event-listener-options-once.js (Feng Yu) [#43877](https://github.com/nodejs/node/pull/43877)
* \[[`88505556fe`](https://github.com/nodejs/node/commit/88505556fe)] - **test**: work scheduled in process.nextTick can keep the event loop alive (Andreu Botella) [#43787](https://github.com/nodejs/node/pull/43787)
* \[[`81a21946eb`](https://github.com/nodejs/node/commit/81a21946eb)] - **test**: simplify test-tls-set-secure-context (Tobias Nießen) [#43878](https://github.com/nodejs/node/pull/43878)
* \[[`61cd11a8a7`](https://github.com/nodejs/node/commit/61cd11a8a7)] - **test**: use `common.mustNotMutateObjectDeep()` in fs tests (LiviaMedeiros) [#43819](https://github.com/nodejs/node/pull/43819)
* \[[`b1081dbe12`](https://github.com/nodejs/node/commit/b1081dbe12)] - **test**: fix test http upload timeout (theanarkh) [#43935](https://github.com/nodejs/node/pull/43935)
* \[[`efd5e0e925`](https://github.com/nodejs/node/commit/efd5e0e925)] - **test**: simplify ReplStream.wait() (Tobias Nießen) [#43857](https://github.com/nodejs/node/pull/43857)
* \[[`ef21ad2996`](https://github.com/nodejs/node/commit/ef21ad2996)] - **test**: merge test-crypto-dh-hash with modp18 test (Tobias Nießen) [#43891](https://github.com/nodejs/node/pull/43891)
* \[[`e502c50a90`](https://github.com/nodejs/node/commit/e502c50a90)] - **test**: refactor `test/es-module/test-esm-resolve-type` (Antoine du Hamel) [#43178](https://github.com/nodejs/node/pull/43178)
* \[[`c782c3dc69`](https://github.com/nodejs/node/commit/c782c3dc69)] - **test**: ensure NODE\_EXTRA\_CA\_CERTS not set before tests (KrayzeeKev) [#43858](https://github.com/nodejs/node/pull/43858)
* \[[`bb6787cb57`](https://github.com/nodejs/node/commit/bb6787cb57)] - **test**: add check to test-fs-readfile-tostring-fail (Richard Lau) [#43850](https://github.com/nodejs/node/pull/43850)
* \[[`7571704186`](https://github.com/nodejs/node/commit/7571704186)] - **test**: complete TODO in test/wpt/test-url.js (Kohei Ueno) [#43797](https://github.com/nodejs/node/pull/43797)
* \[[`6f1d2dfb9d`](https://github.com/nodejs/node/commit/6f1d2dfb9d)] - **test**: add test on worker process.exit in async modules (Chengzhong Wu) [#43751](https://github.com/nodejs/node/pull/43751)
* \[[`776cc3abbd`](https://github.com/nodejs/node/commit/776cc3abbd)] - **test**: use `common.mustNotMutateObjectDeep()` in immutability tests (LiviaMedeiros) [#43196](https://github.com/nodejs/node/pull/43196)
* \[[`42f2deb3a0`](https://github.com/nodejs/node/commit/42f2deb3a0)] - **test**: add `common.mustNotMutateObjectDeep()` (LiviaMedeiros) [#43196](https://github.com/nodejs/node/pull/43196)
* \[[`f3fc51c508`](https://github.com/nodejs/node/commit/f3fc51c508)] - **test**: fix coverity warning in test (Michael Dawson) [#43631](https://github.com/nodejs/node/pull/43631)
* \[[`a9ecba2fa8`](https://github.com/nodejs/node/commit/a9ecba2fa8)] - **test**: mark test-http-client-response-timeout flaky (Tobias Nießen) [#43792](https://github.com/nodejs/node/pull/43792)
* \[[`cd0d9ddb7c`](https://github.com/nodejs/node/commit/cd0d9ddb7c)] - **test\_runner**: add support for boolean values for `concurrency` option (Lenvin Gonsalves) [#43887](https://github.com/nodejs/node/pull/43887)
* \[[`f98020138a`](https://github.com/nodejs/node/commit/f98020138a)] - **test\_runner**: validate `timeout` option (Antoine du Hamel) [#43843](https://github.com/nodejs/node/pull/43843)
* \[[`58d15b3687`](https://github.com/nodejs/node/commit/58d15b3687)] - **test\_runner**: pass signal on timeout (Moshe Atlow) [#43911](https://github.com/nodejs/node/pull/43911)
* \[[`8b0248506f`](https://github.com/nodejs/node/commit/8b0248506f)] - **test\_runner**: do not report an error when tests are passing (Antoine du Hamel) [#43919](https://github.com/nodejs/node/pull/43919)
* \[[`aa8053e1fa`](https://github.com/nodejs/node/commit/aa8053e1fa)] - **test\_runner**: recieve and pass AbortSignal (Moshe Atlow) [#43554](https://github.com/nodejs/node/pull/43554)
* \[[`f13e4c1be9`](https://github.com/nodejs/node/commit/f13e4c1be9)] - **test\_runner**: fix `it` concurrency (Moshe Atlow) [#43757](https://github.com/nodejs/node/pull/43757)
* \[[`e404a3ef6d`](https://github.com/nodejs/node/commit/e404a3ef6d)] - **test\_runner**: support timeout for tests (Moshe Atlow) [#43505](https://github.com/nodejs/node/pull/43505)
* \[[`f28198cc05`](https://github.com/nodejs/node/commit/f28198cc05)] - **test\_runner**: catch errors thrown within `describe` (Moshe Atlow) [#43729](https://github.com/nodejs/node/pull/43729)
* \[[`bfe0ac6cd0`](https://github.com/nodejs/node/commit/bfe0ac6cd0)] - **tools**: add more options to track flaky tests (Antoine du Hamel) [#43954](https://github.com/nodejs/node/pull/43954)
* \[[`17a4e5e775`](https://github.com/nodejs/node/commit/17a4e5e775)] - **tools**: add verbose flag to inactive TSC finder (Rich Trott) [#43913](https://github.com/nodejs/node/pull/43913)
* \[[`373304b0c7`](https://github.com/nodejs/node/commit/373304b0c7)] - **tools**: add support for using API key to vuln checking script (Facundo Tuesca) [#43909](https://github.com/nodejs/node/pull/43909)
* \[[`ed45088c14`](https://github.com/nodejs/node/commit/ed45088c14)] - **tools**: support versioned node shared libs on z/OS (alexcfyung) [#42256](https://github.com/nodejs/node/pull/42256)
* \[[`c9ecd6d21f`](https://github.com/nodejs/node/commit/c9ecd6d21f)] - **tools**: update doc to highlight.js\@11.6.0 (Node.js GitHub Bot) [#43870](https://github.com/nodejs/node/pull/43870)
* \[[`c92135aa0f`](https://github.com/nodejs/node/commit/c92135aa0f)] - **tools**: update lint-md-dependencies to rollup\@2.77.0 (Node.js GitHub Bot) [#43871](https://github.com/nodejs/node/pull/43871)
* \[[`e12bf40fd1`](https://github.com/nodejs/node/commit/e12bf40fd1)] - **tools**: update eslint to 8.20.0 (Node.js GitHub Bot) [#43873](https://github.com/nodejs/node/pull/43873)
* \[[`09fe9b30a9`](https://github.com/nodejs/node/commit/09fe9b30a9)] - **tools**: add script for vulnerability checking (Facundo Tuesca) [#43362](https://github.com/nodejs/node/pull/43362)
* \[[`19e8876877`](https://github.com/nodejs/node/commit/19e8876877)] - **trace\_events**: trace net connect event (theanarkh) [#43903](https://github.com/nodejs/node/pull/43903)
* \[[`1af7f24143`](https://github.com/nodejs/node/commit/1af7f24143)] - **util**: remove unicode support todo for perf implications (Rhys) [#43762](https://github.com/nodejs/node/pull/43762)
* \[[`acfc33ca8c`](https://github.com/nodejs/node/commit/acfc33ca8c)] - **(SEMVER-MINOR)** **util**: add tokens to parseArgs (John Gee) [#43459](https://github.com/nodejs/node/pull/43459)
* \[[`f32aec8a6d`](https://github.com/nodejs/node/commit/f32aec8a6d)] - **util**: refactor to use validateObject (Kohei Ueno) [#43769](https://github.com/nodejs/node/pull/43769)
* \[[`d7cfd0c5ba`](https://github.com/nodejs/node/commit/d7cfd0c5ba)] - **v8**: serialize BigInt64Array and BigUint64Array (Ben Noordhuis) [#43571](https://github.com/nodejs/node/pull/43571)

<a id="18.6.0"></a>

## 2022-07-13, Version 18.6.0 (Current), @targos

### Notable Changes

#### Experimental ESM Loader Hooks API

Node.js ESM Loader hooks now support multiple custom loaders, and composition is
achieved via "chaining": `foo-loader` calls `bar-loader` calls `qux-loader`
(a custom loader _must_ now signal a short circuit when intentionally not
calling the next). See the [ESM docs](https://nodejs.org/api/esm.html) for details.

Real-world use-cases are laid out for end-users with working examples in the
article [Custom ESM loaders: Who, what, when, where, why, how](https://dev.to/jakobjingleheimer/custom-esm-loaders-who-what-when-where-why-how-4i1o).

Contributed by Jacob Smith, Geoffrey Booth, and Bradley Farias - <https://github.com/nodejs/node/pull/42623>

### Commits

#### Semver-minor commits

* \[[`0bca7b722e`](https://github.com/nodejs/node/commit/0bca7b722e)] - **(SEMVER-MINOR)** **dns**: export error code constants from `dns/promises` (Feng Yu) [#43176](https://github.com/nodejs/node/pull/43176)
* \[[`da61e2330f`](https://github.com/nodejs/node/commit/da61e2330f)] - **(SEMVER-MINOR)** **esm**: add chaining to loaders (Jacob Smith) [#42623](https://github.com/nodejs/node/pull/42623)
* \[[`8c97f63401`](https://github.com/nodejs/node/commit/8c97f63401)] - **(SEMVER-MINOR)** **http**: add diagnostics channel for http client (theanarkh) [#43580](https://github.com/nodejs/node/pull/43580)
* \[[`b27856d3d4`](https://github.com/nodejs/node/commit/b27856d3d4)] - **(SEMVER-MINOR)** **http**: add perf\_hooks detail for http request and client (theanarkh) [#43361](https://github.com/nodejs/node/pull/43361)
* \[[`9d918d9923`](https://github.com/nodejs/node/commit/9d918d9923)] - **(SEMVER-MINOR)** **module**: add isBuiltIn method (hemanth.hm) [#43396](https://github.com/nodejs/node/pull/43396)
* \[[`a0e7b9983c`](https://github.com/nodejs/node/commit/a0e7b9983c)] - **(SEMVER-MINOR)** **net**: add drop event for net server (theanarkh) [#43582](https://github.com/nodejs/node/pull/43582)
* \[[`4bda6e02a3`](https://github.com/nodejs/node/commit/4bda6e02a3)] - **(SEMVER-MINOR)** **test\_runner**: expose `describe` and `it` (Moshe Atlow) [#43420](https://github.com/nodejs/node/pull/43420)
* \[[`34e83312a4`](https://github.com/nodejs/node/commit/34e83312a4)] - **(SEMVER-MINOR)** **v8**: add v8.startupSnapshot utils (Joyee Cheung) [#43329](https://github.com/nodejs/node/pull/43329)

#### Semver-patch commits

* \[[`ef174eac87`](https://github.com/nodejs/node/commit/ef174eac87)] - **assert**: callTracker throw a specific error message when possible (Moshe Atlow) [#43640](https://github.com/nodejs/node/pull/43640)
* \[[`07836637af`](https://github.com/nodejs/node/commit/07836637af)] - **bootstrap**: move global initialization to js (Alena Khineika) [#43625](https://github.com/nodejs/node/pull/43625)
* \[[`e9ee7e44be`](https://github.com/nodejs/node/commit/e9ee7e44be)] - **build,test**: increase stack size limit on Windows (Tobias Nießen) [#43632](https://github.com/nodejs/node/pull/43632)
* \[[`3ca9e653a6`](https://github.com/nodejs/node/commit/3ca9e653a6)] - **child\_process**: improve ipc write performance (rubikscraft) [#42931](https://github.com/nodejs/node/pull/42931)
* \[[`cad6d990ec`](https://github.com/nodejs/node/commit/cad6d990ec)] - **child\_process**: speed up 'advanced' ipc receiving (rubikscraft) [#42931](https://github.com/nodejs/node/pull/42931)
* \[[`ce3a22a9e3`](https://github.com/nodejs/node/commit/ce3a22a9e3)] - **cluster**: fix closing dgram sockets in cluster workers throws errors (Ouyang Yadong) [#43709](https://github.com/nodejs/node/pull/43709)
* \[[`5d8ee519db`](https://github.com/nodejs/node/commit/5d8ee519db)] - **cluster**: fix fd leak (theanarkh) [#43650](https://github.com/nodejs/node/pull/43650)
* \[[`fa5c4643e2`](https://github.com/nodejs/node/commit/fa5c4643e2)] - **cluster, net**: fix listen pipe with readable and writable in cluster (theanarkh) [#43634](https://github.com/nodejs/node/pull/43634)
* \[[`4df96b501d`](https://github.com/nodejs/node/commit/4df96b501d)] - **crypto**: don't disable TLS 1.3 without suites (Adam Majer) [#43427](https://github.com/nodejs/node/pull/43427)
* \[[`a43928ae78`](https://github.com/nodejs/node/commit/a43928ae78)] - **crypto**: use ByteSource::Builder in To\*Copy (Tobias Nießen) [#43477](https://github.com/nodejs/node/pull/43477)
* \[[`bb326f7ece`](https://github.com/nodejs/node/commit/bb326f7ece)] - **crypto**: handle webcrypto generateKey() usages edge case (Filip Skokan) [#43454](https://github.com/nodejs/node/pull/43454)
* \[[`9bd13bbb3a`](https://github.com/nodejs/node/commit/9bd13bbb3a)] - **crypto**: update Wrapping and unwrapping keys webcrypto example (Filip Skokan) [#43452](https://github.com/nodejs/node/pull/43452)
* \[[`679f19128e`](https://github.com/nodejs/node/commit/679f19128e)] - **crypto**: fix webcrypto generateKey() with empty usages (Filip Skokan) [#43431](https://github.com/nodejs/node/pull/43431)
* \[[`64a9dd7b83`](https://github.com/nodejs/node/commit/64a9dd7b83)] - **crypto**: fix webcrypto digest() invalid algorithm (Filip Skokan) [#43431](https://github.com/nodejs/node/pull/43431)
* \[[`dedb22e965`](https://github.com/nodejs/node/commit/dedb22e965)] - **crypto**: fix webcrypto RSA generateKey() use of publicExponent (Filip Skokan) [#43431](https://github.com/nodejs/node/pull/43431)
* \[[`018f61cb4f`](https://github.com/nodejs/node/commit/018f61cb4f)] - **crypto**: fix webcrypto AES-KW keys accepting encrypt/decrypt usages (Filip Skokan) [#43431](https://github.com/nodejs/node/pull/43431)
* \[[`3ee0bb8d03`](https://github.com/nodejs/node/commit/3ee0bb8d03)] - **crypto**: fix webcrypto deriveBits for non-byte lengths (Filip Skokan) [#43431](https://github.com/nodejs/node/pull/43431)
* \[[`7fc075b23a`](https://github.com/nodejs/node/commit/7fc075b23a)] - **deps**: update undici to 5.7.0 (Node.js GitHub Bot) [#43790](https://github.com/nodejs/node/pull/43790)
* \[[`d6a9e93426`](https://github.com/nodejs/node/commit/d6a9e93426)] - **deps**: patch V8 to 10.2.154.13 (Michaël Zasso) [#43727](https://github.com/nodejs/node/pull/43727)
* \[[`428d03cb94`](https://github.com/nodejs/node/commit/428d03cb94)] - **deps**: update corepack to 0.12.0 (Node.js GitHub Bot) [#43748](https://github.com/nodejs/node/pull/43748)
* \[[`74914698e5`](https://github.com/nodejs/node/commit/74914698e5)] - **deps**: upgrade npm to 8.13.2 (npm team) [#43622](https://github.com/nodejs/node/pull/43622)
* \[[`0636f86ecc`](https://github.com/nodejs/node/commit/0636f86ecc)] - **deps**: upgrade npm to 8.13.1 (npm team) [#43552](https://github.com/nodejs/node/pull/43552)
* \[[`2149acda60`](https://github.com/nodejs/node/commit/2149acda60)] - **dns**: make promise API fully constructed from `lib/internal/dns/promises` (Feng Yu) [#43227](https://github.com/nodejs/node/pull/43227)
* \[[`79ea19e5e2`](https://github.com/nodejs/node/commit/79ea19e5e2)] - **errors**: extract type detection & use in `ERR_INVALID_RETURN_VALUE` (Jacob Smith) [#43558](https://github.com/nodejs/node/pull/43558)
* \[[`80ced1ae31`](https://github.com/nodejs/node/commit/80ced1ae31)] - **esm**: treat `307` and `308` as redirects in HTTPS imports (Kid) [#43689](https://github.com/nodejs/node/pull/43689)
* \[[`953fefe77b`](https://github.com/nodejs/node/commit/953fefe77b)] - **esm**: restore `next<HookName>`'s `context` as optional arg (Jacob Smith) [#43553](https://github.com/nodejs/node/pull/43553)
* \[[`10bcad5c6e`](https://github.com/nodejs/node/commit/10bcad5c6e)] - **esm**: fix chain advances when loader calls next\<HookName> multiple times (Jacob Smith) [#43303](https://github.com/nodejs/node/pull/43303)
* \[[`50d64edd49`](https://github.com/nodejs/node/commit/50d64edd49)] - **esm**: refactor responseURL handling (Guy Bedford) [#43164](https://github.com/nodejs/node/pull/43164)
* \[[`254efd9e3b`](https://github.com/nodejs/node/commit/254efd9e3b)] - **esm**: fix http(s) import via custom loader (Jacob Smith) [#43130](https://github.com/nodejs/node/pull/43130)
* \[[`061ed0e76b`](https://github.com/nodejs/node/commit/061ed0e76b)] - **events**: improve `Event` compatibility (Daeyeon Jeong) [#43461](https://github.com/nodejs/node/pull/43461)
* \[[`66fb059547`](https://github.com/nodejs/node/commit/66fb059547)] - **events**: improve `EventListener` validation (Daeyeon Jeong) [#43491](https://github.com/nodejs/node/pull/43491)
* \[[`12a591a676`](https://github.com/nodejs/node/commit/12a591a676)] - **fs**: refactor realpath with Map and Set (LiviaMedeiros) [#43569](https://github.com/nodejs/node/pull/43569)
* \[[`df501316c1`](https://github.com/nodejs/node/commit/df501316c1)] - **fs**: don't end fs promises on Isolate termination (Santiago Gimeno) [#42910](https://github.com/nodejs/node/pull/42910)
* \[[`e6d4837fad`](https://github.com/nodejs/node/commit/e6d4837fad)] - **http**: fix failing test (Paolo Insogna) [#43641](https://github.com/nodejs/node/pull/43641)
* \[[`491c7619c4`](https://github.com/nodejs/node/commit/491c7619c4)] - **http**: defer reentrant execution of Parser::Execute (Paolo Insogna) [#43369](https://github.com/nodejs/node/pull/43369)
* \[[`d71ba322b0`](https://github.com/nodejs/node/commit/d71ba322b0)] - **http**: fix http agent keep alive (theanarkh) [#43380](https://github.com/nodejs/node/pull/43380)
* \[[`1f4f811de5`](https://github.com/nodejs/node/commit/1f4f811de5)] - **http2**: log debug only when in debug mode (Basit) [#43626](https://github.com/nodejs/node/pull/43626)
* \[[`c8cbec4cef`](https://github.com/nodejs/node/commit/c8cbec4cef)] - **lib**: make `validateObject` less affected by prototype tampering (Antoine du Hamel) [#42929](https://github.com/nodejs/node/pull/42929)
* \[[`dc484b6f6f`](https://github.com/nodejs/node/commit/dc484b6f6f)] - **lib**: implement safe alternatives to `Promise` static methods (Antoine du Hamel) [#43728](https://github.com/nodejs/node/pull/43728)
* \[[`2233567331`](https://github.com/nodejs/node/commit/2233567331)] - **lib**: use null-prototype objects for property descriptors (Antoine du Hamel) [#43473](https://github.com/nodejs/node/pull/43473)
* \[[`b9198d977f`](https://github.com/nodejs/node/commit/b9198d977f)] - **lib**: refactor to avoid unsafe regex primordials (Antoine du Hamel) [#43475](https://github.com/nodejs/node/pull/43475)
* \[[`deaf4bb5cd`](https://github.com/nodejs/node/commit/deaf4bb5cd)] - **lib**: fix TODO in `freeze_intrinsics` (Antoine du Hamel) [#43472](https://github.com/nodejs/node/pull/43472)
* \[[`61e6d7858a`](https://github.com/nodejs/node/commit/61e6d7858a)] - **lib,src**: add source map support for global eval (Chengzhong Wu) [#43428](https://github.com/nodejs/node/pull/43428)
* \[[`58646eaad6`](https://github.com/nodejs/node/commit/58646eaad6)] - **loader**: make `require.resolve` throw for unknown builtin modules (木杉) [#43336](https://github.com/nodejs/node/pull/43336)
* \[[`e914185c44`](https://github.com/nodejs/node/commit/e914185c44)] - **module**: cjs-module-lexer WebAssembly fallback (Guy Bedford) [#43612](https://github.com/nodejs/node/pull/43612)
* \[[`3ad4d37b3c`](https://github.com/nodejs/node/commit/3ad4d37b3c)] - **module**: also enable subpath imports in REPL (Ray) [#43450](https://github.com/nodejs/node/pull/43450)
* \[[`bf4ac4c55f`](https://github.com/nodejs/node/commit/bf4ac4c55f)] - **net**: remove redundant connecting assignment (Ouyang Yadong) [#43710](https://github.com/nodejs/node/pull/43710)
* \[[`ad1d0541c5`](https://github.com/nodejs/node/commit/ad1d0541c5)] - **net**: fix net keepalive and noDelay (theanarkh) [#43561](https://github.com/nodejs/node/pull/43561)
* \[[`f8bdc53e4f`](https://github.com/nodejs/node/commit/f8bdc53e4f)] - **net**: prevent /32 ipv4 mask from matching all ips (supriyo-biswas) [#43381](https://github.com/nodejs/node/pull/43381)
* \[[`47a252257b`](https://github.com/nodejs/node/commit/47a252257b)] - **net**: fix net.Server keepalive and noDelay (theanarkh) [#43497](https://github.com/nodejs/node/pull/43497)
* \[[`d834d216f2`](https://github.com/nodejs/node/commit/d834d216f2)] - **perf\_hooks**: add initiatorType getter (Rafael Gonzaga) [#43593](https://github.com/nodejs/node/pull/43593)
* \[[`02009b7069`](https://github.com/nodejs/node/commit/02009b7069)] - **perf\_hooks**: fix miscounted gc performance entry starttime (#43066) (Xuguang Mei) [#43066](https://github.com/nodejs/node/pull/43066)
* \[[`e9574f3009`](https://github.com/nodejs/node/commit/e9574f3009)] - **readline**: fix to not access a property on an undefined value (Kohei Ueno) [#43543](https://github.com/nodejs/node/pull/43543)
* \[[`fe1f740f61`](https://github.com/nodejs/node/commit/fe1f740f61)] - **src**: merge RunInThisContext() with RunInContext() (Daeyeon Jeong) [#43225](https://github.com/nodejs/node/pull/43225)
* \[[`0f6d19489a`](https://github.com/nodejs/node/commit/0f6d19489a)] - **src**: fix crash on FSReqPromise destructor (Santiago Gimeno) [#43533](https://github.com/nodejs/node/pull/43533)
* \[[`4e6a844207`](https://github.com/nodejs/node/commit/4e6a844207)] - **src**: delegate NodeArrayBufferAllocator to v8's allocator (Jeremy Rose) [#43594](https://github.com/nodejs/node/pull/43594)
* \[[`5ae30bf17a`](https://github.com/nodejs/node/commit/5ae30bf17a)] - **src**: remove a stale comment in `async_hooks` (Daeyeon Jeong) [#43317](https://github.com/nodejs/node/pull/43317)
* \[[`0b432b957e`](https://github.com/nodejs/node/commit/0b432b957e)] - **src**: fix compiler warning in src/heap\_utils.cc (Darshan Sen) [#43579](https://github.com/nodejs/node/pull/43579)
* \[[`d3fc791c3d`](https://github.com/nodejs/node/commit/d3fc791c3d)] - **src**: improve and update ByteSource description (Tobias Nießen) [#43478](https://github.com/nodejs/node/pull/43478)
* \[[`4e0afa4133`](https://github.com/nodejs/node/commit/4e0afa4133)] - **src**: remove CopyBuffer (Tobias Nießen) [#43463](https://github.com/nodejs/node/pull/43463)
* \[[`0659d5e3b0`](https://github.com/nodejs/node/commit/0659d5e3b0)] - **src**: change FormatSize to actually accept a size\_t (Tobias Nießen) [#43464](https://github.com/nodejs/node/pull/43464)
* \[[`66ee1f1e3c`](https://github.com/nodejs/node/commit/66ee1f1e3c)] - **src**: register StreamBase while registering LibuvStreamWrap (Darshan Sen) [#43321](https://github.com/nodejs/node/pull/43321)
* \[[`48ee6b9dc9`](https://github.com/nodejs/node/commit/48ee6b9dc9)] - **src,bootstrap**: remove NodeMainInstance::registry\_ (Darshan Sen) [#43392](https://github.com/nodejs/node/pull/43392)
* \[[`2e181f68a3`](https://github.com/nodejs/node/commit/2e181f68a3)] - **src,stream**: change return type to `Maybe` (Daeyeon Jeong) [#43575](https://github.com/nodejs/node/pull/43575)
* \[[`0f07abc80d`](https://github.com/nodejs/node/commit/0f07abc80d)] - **stream**: finish pipeline if dst closes before src (Robert Nagy) [#43701](https://github.com/nodejs/node/pull/43701)
* \[[`1617a4621e`](https://github.com/nodejs/node/commit/1617a4621e)] - **stream**: pass error on legacy destroy (Giacomo Gregoletto) [#43519](https://github.com/nodejs/node/pull/43519)
* \[[`40f51d8e83`](https://github.com/nodejs/node/commit/40f51d8e83)] - **test\_runner**: protect internals against prototype tampering (Antoine du Hamel) [#43578](https://github.com/nodejs/node/pull/43578)
* \[[`ddf7518520`](https://github.com/nodejs/node/commit/ddf7518520)] - **test\_runner**: cancel on termination (Moshe Atlow) [#43549](https://github.com/nodejs/node/pull/43549)
* \[[`e51d8c6004`](https://github.com/nodejs/node/commit/e51d8c6004)] - **test\_runner**: wait for stderr and stdout to complete (Moshe Atlow) [#43666](https://github.com/nodejs/node/pull/43666)
* \[[`dda64ddfbd`](https://github.com/nodejs/node/commit/dda64ddfbd)] - **test\_runner**: add Subtest to tap protocol output (Moshe Atlow) [#43417](https://github.com/nodejs/node/pull/43417)
* \[[`a1f1d3a7b3`](https://github.com/nodejs/node/commit/a1f1d3a7b3)] - **url**: update WHATWG URL parser to align with latest spec (Feng Yu) [#43190](https://github.com/nodejs/node/pull/43190)
* \[[`5a5c4be5a3`](https://github.com/nodejs/node/commit/5a5c4be5a3)] - **util**: add `AggregateError.prototype.errors` to inspect output (LiviaMedeiros) [#43646](https://github.com/nodejs/node/pull/43646)
* \[[`bdca4d3ccf`](https://github.com/nodejs/node/commit/bdca4d3ccf)] - **util**: remove unnecessary template string (Ruben Bridgewater) [#41082](https://github.com/nodejs/node/pull/41082)
* \[[`6b16836448`](https://github.com/nodejs/node/commit/6b16836448)] - **util**: mark cwd grey while inspecting errors (Ruben Bridgewater) [#41082](https://github.com/nodejs/node/pull/41082)
* \[[`baa22a7b7d`](https://github.com/nodejs/node/commit/baa22a7b7d)] - **util**: avoid inline access to Symbol.iterator (Kohei Ueno) [#43683](https://github.com/nodejs/node/pull/43683)
* \[[`a1f581a61e`](https://github.com/nodejs/node/commit/a1f581a61e)] - **util**: fix TypeError of symbol in template literals (cola119) [#42790](https://github.com/nodejs/node/pull/42790)
* \[[`ba9b2f021f`](https://github.com/nodejs/node/commit/ba9b2f021f)] - **wasi**: use WasmMemoryObject handle for perf (#43544) (snek) [#43544](https://github.com/nodejs/node/pull/43544)

#### Documentation commits

* \[[`e0769554a5`](https://github.com/nodejs/node/commit/e0769554a5)] - **doc**: remove bullet point referring to Node.js 12 (Luigi Pinca) [#43744](https://github.com/nodejs/node/pull/43744)
* \[[`7ffcd85ace`](https://github.com/nodejs/node/commit/7ffcd85ace)] - **doc**: include last security release date (Rafael Gonzaga) [#43774](https://github.com/nodejs/node/pull/43774)
* \[[`4569d6ebcb`](https://github.com/nodejs/node/commit/4569d6ebcb)] - **doc**: add details for July 2022 security releases (Beth Griggs) [#43733](https://github.com/nodejs/node/pull/43733)
* \[[`1bd56339c5`](https://github.com/nodejs/node/commit/1bd56339c5)] - **doc**: remove openssl 1.x reference (Rafael Gonzaga) [#43734](https://github.com/nodejs/node/pull/43734)
* \[[`bf62ffd848`](https://github.com/nodejs/node/commit/bf62ffd848)] - **doc**: remove node-report from support tiers (RafaelGSS) [#43737](https://github.com/nodejs/node/pull/43737)
* \[[`ca5af0dbf7`](https://github.com/nodejs/node/commit/ca5af0dbf7)] - **doc**: update changelog-maker to the new flags (RafaelGSS) [#43696](https://github.com/nodejs/node/pull/43696)
* \[[`088b9266d0`](https://github.com/nodejs/node/commit/088b9266d0)] - **doc**: remove extra 'in's (Colin Ihrig) [#43705](https://github.com/nodejs/node/pull/43705)
* \[[`7679c77347`](https://github.com/nodejs/node/commit/7679c77347)] - **doc**: add Geoffrey Booth to TSC (Rich Trott) [#43706](https://github.com/nodejs/node/pull/43706)
* \[[`d46261ceed`](https://github.com/nodejs/node/commit/d46261ceed)] - **doc**: improve readability of `dns.md` (0xSanyam) [#43694](https://github.com/nodejs/node/pull/43694)
* \[[`ca0fbfd87f`](https://github.com/nodejs/node/commit/ca0fbfd87f)] - **doc**: add note regarding special case of 0 stat.size (Douglas Wilson) [#43690](https://github.com/nodejs/node/pull/43690)
* \[[`267f66b5cc`](https://github.com/nodejs/node/commit/267f66b5cc)] - **doc**: fix default of duplex.allowHalfOpen (Vincent Weevers) [#43665](https://github.com/nodejs/node/pull/43665)
* \[[`46ad2061db`](https://github.com/nodejs/node/commit/46ad2061db)] - **doc**: fix typo in errors.md (Kazuma Ohashi) [#43677](https://github.com/nodejs/node/pull/43677)
* \[[`3a8edb363e`](https://github.com/nodejs/node/commit/3a8edb363e)] - **doc**: improve description of --input-type (cola119) [#43507](https://github.com/nodejs/node/pull/43507)
* \[[`b4b15b71d7`](https://github.com/nodejs/node/commit/b4b15b71d7)] - **doc**: add daeyeon to triagers (Daeyeon Jeong) [#43637](https://github.com/nodejs/node/pull/43637)
* \[[`cb77b3e3f7`](https://github.com/nodejs/node/commit/cb77b3e3f7)] - **doc**: remove appmetrics from tierlist (Tony Gorez) [#43608](https://github.com/nodejs/node/pull/43608)
* \[[`0fe825ac07`](https://github.com/nodejs/node/commit/0fe825ac07)] - **doc**: remove systemtap from tierlist (Tony Gorez) [#43605](https://github.com/nodejs/node/pull/43605)
* \[[`6fc5a13fe0`](https://github.com/nodejs/node/commit/6fc5a13fe0)] - **doc**: add single executable application initiative (Michael Dawson) [#43611](https://github.com/nodejs/node/pull/43611)
* \[[`350e6ae04c`](https://github.com/nodejs/node/commit/350e6ae04c)] - **doc**: remove windows xperf from tierlist (Tony Gorez) [#43607](https://github.com/nodejs/node/pull/43607)
* \[[`a6e98dfd65`](https://github.com/nodejs/node/commit/a6e98dfd65)] - **doc**: remove lttng from tierlist (Tony Gorez) [#43604](https://github.com/nodejs/node/pull/43604)
* \[[`22512427b3`](https://github.com/nodejs/node/commit/22512427b3)] - **doc**: remove dtrace from tierlist (Tony Gorez) [#43606](https://github.com/nodejs/node/pull/43606)
* \[[`a3659e3547`](https://github.com/nodejs/node/commit/a3659e3547)] - **doc**: promote 0x to tier 4 (Tony Gorez) [#43609](https://github.com/nodejs/node/pull/43609)
* \[[`6ede1c2162`](https://github.com/nodejs/node/commit/6ede1c2162)] - **doc**: include CVSS mention (Rafael Gonzaga) [#43602](https://github.com/nodejs/node/pull/43602)
* \[[`23c5de3579`](https://github.com/nodejs/node/commit/23c5de3579)] - **doc**: fix icu-small example (Michael Dawson) [#43591](https://github.com/nodejs/node/pull/43591)
* \[[`54a8a0c9c7`](https://github.com/nodejs/node/commit/54a8a0c9c7)] - **doc**: add `backport-open-vN.x` step to backporting guide (LiviaMedeiros) [#43590](https://github.com/nodejs/node/pull/43590)
* \[[`60b949d8ff`](https://github.com/nodejs/node/commit/60b949d8ff)] - **doc**: move MylesBorins to TSC Emeritus (Myles Borins) [#43524](https://github.com/nodejs/node/pull/43524)
* \[[`08ed28c31e`](https://github.com/nodejs/node/commit/08ed28c31e)] - **doc**: add Juan as a security steward (Michael Dawson) [#43512](https://github.com/nodejs/node/pull/43512)
* \[[`2e799bcd35`](https://github.com/nodejs/node/commit/2e799bcd35)] - **doc**: update link to MDN page about dynamic imports (James Scott-Brown) [#43530](https://github.com/nodejs/node/pull/43530)
* \[[`c8aafe2036`](https://github.com/nodejs/node/commit/c8aafe2036)] - **doc**: fix Visual Studio 2019 download link (Feng Yu) [#43236](https://github.com/nodejs/node/pull/43236)
* \[[`d0c78d21e0`](https://github.com/nodejs/node/commit/d0c78d21e0)] - **doc**: update link of `ICU data slicer` (Feng Yu) [#43483](https://github.com/nodejs/node/pull/43483)
* \[[`324728094c`](https://github.com/nodejs/node/commit/324728094c)] - **doc**: update v8 doc link to v8.dev (Feng Yu) [#43482](https://github.com/nodejs/node/pull/43482)
* \[[`b111331c9c`](https://github.com/nodejs/node/commit/b111331c9c)] - **doc**: add ESM version examples to events api doc (Feng Yu) [#43226](https://github.com/nodejs/node/pull/43226)
* \[[`038decfbc3`](https://github.com/nodejs/node/commit/038decfbc3)] - **doc**: update default branch name in `test/**` (Luigi Pinca) [#43445](https://github.com/nodejs/node/pull/43445)
* \[[`a23051af84`](https://github.com/nodejs/node/commit/a23051af84)] - **doc**: add new useful V8 option (JialuZhang-intel) [#42575](https://github.com/nodejs/node/pull/42575)
* \[[`7f406fd77b`](https://github.com/nodejs/node/commit/7f406fd77b)] - **doc**: remove branch name mention in `src/README.md` (Feng Yu) [#43442](https://github.com/nodejs/node/pull/43442)
* \[[`06fe60a6f9`](https://github.com/nodejs/node/commit/06fe60a6f9)] - **doc**: update default branch name in `Makefile` (Feng Yu) [#43441](https://github.com/nodejs/node/pull/43441)
* \[[`9d61da0aef`](https://github.com/nodejs/node/commit/9d61da0aef)] - **doc**: update main branch name in release guide (Richard Lau) [#43437](https://github.com/nodejs/node/pull/43437)
* \[[`739d3a35ed`](https://github.com/nodejs/node/commit/739d3a35ed)] - **doc**: update main branch name in onboarding.md (Feng Yu) [#43443](https://github.com/nodejs/node/pull/43443)
* \[[`e0fedcfb18`](https://github.com/nodejs/node/commit/e0fedcfb18)] - **doc**: fixup after rename of primary nodejs branch (Michael Dawson) [#43453](https://github.com/nodejs/node/pull/43453)
* \[[`429e0f433b`](https://github.com/nodejs/node/commit/429e0f433b)] - **doc**: update main branch name in doc/contributing/\* (Luigi Pinca) [#43438](https://github.com/nodejs/node/pull/43438)
* \[[`cbaf1207f4`](https://github.com/nodejs/node/commit/cbaf1207f4)] - **doc**: add code examples to node test runner (Wassim Chegham) [#43359](https://github.com/nodejs/node/pull/43359)
* \[[`462e526237`](https://github.com/nodejs/node/commit/462e526237)] - **doc,test**: clarify timingSafeEqual semantics (Tobias Nießen) [#43228](https://github.com/nodejs/node/pull/43228)

#### Other commits

* \[[`7ee0be71f9`](https://github.com/nodejs/node/commit/7ee0be71f9)] - **benchmark**: fix output regression (Brian White) [#43635](https://github.com/nodejs/node/pull/43635)
* \[[`d90a6f9bda`](https://github.com/nodejs/node/commit/d90a6f9bda)] - **benchmark**: fix fork detection (Paolo Insogna) [#43601](https://github.com/nodejs/node/pull/43601)
* \[[`f9c30abcdc`](https://github.com/nodejs/node/commit/f9c30abcdc)] - **benchmark**: forcefully close processes (Paolo Insogna) [#43557](https://github.com/nodejs/node/pull/43557)
* \[[`ebf962c053`](https://github.com/nodejs/node/commit/ebf962c053)] - **build**: enable GitPod prebuilds (Rich Trott) [#43698](https://github.com/nodejs/node/pull/43698)
* \[[`482bd53357`](https://github.com/nodejs/node/commit/482bd53357)] - **build**: clarify missing clang-format tool (Tobias Nießen) [#42762](https://github.com/nodejs/node/pull/42762)
* \[[`919c5ee5c2`](https://github.com/nodejs/node/commit/919c5ee5c2)] - **build**: update main branch name in GH workflow (Feng Yu) [#43481](https://github.com/nodejs/node/pull/43481)
* \[[`3b08dfdc5d`](https://github.com/nodejs/node/commit/3b08dfdc5d)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#43750](https://github.com/nodejs/node/pull/43750)
* \[[`508cbbcbf9`](https://github.com/nodejs/node/commit/508cbbcbf9)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#43660](https://github.com/nodejs/node/pull/43660)
* \[[`d650c9c6b0`](https://github.com/nodejs/node/commit/d650c9c6b0)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#43573](https://github.com/nodejs/node/pull/43573)
* \[[`b9204c9be8`](https://github.com/nodejs/node/commit/b9204c9be8)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#43480](https://github.com/nodejs/node/pull/43480)
* \[[`294f0ef4df`](https://github.com/nodejs/node/commit/294f0ef4df)] - **test**: mark test-net-connect-reset-until-connected flaky on freebsd (Feng Yu) [#43613](https://github.com/nodejs/node/pull/43613)
* \[[`f2f7d7b207`](https://github.com/nodejs/node/commit/f2f7d7b207)] - **test**: remove unnecessary .toString() calls in HTTP tests (Anna Henningsen) [#43731](https://github.com/nodejs/node/pull/43731)
* \[[`38e92fd88f`](https://github.com/nodejs/node/commit/38e92fd88f)] - **test**: mark test-gc-http-client-timeout as flaky on arm (Chengzhong Wu) [#43754](https://github.com/nodejs/node/pull/43754)
* \[[`b164848c55`](https://github.com/nodejs/node/commit/b164848c55)] - **test**: fix typo in file name (Antoine du Hamel) [#43764](https://github.com/nodejs/node/pull/43764)
* \[[`a0b799f645`](https://github.com/nodejs/node/commit/a0b799f645)] - **test**: add test for profile command of node inspect (Kohei Ueno) [#43058](https://github.com/nodejs/node/pull/43058)
* \[[`c4d88b3345`](https://github.com/nodejs/node/commit/c4d88b3345)] - **test**: use Object for `tests` variable in fs trace test (Feng Yu) [#43585](https://github.com/nodejs/node/pull/43585)
* \[[`c9f130e1cf`](https://github.com/nodejs/node/commit/c9f130e1cf)] - **test**: improve code coverage for performance\_entry (Kohei Ueno) [#43434](https://github.com/nodejs/node/pull/43434)
* \[[`0b4956079a`](https://github.com/nodejs/node/commit/0b4956079a)] - **test**: add test to ensure repl doesn't support --input-type (cola119) [#43507](https://github.com/nodejs/node/pull/43507)
* \[[`2adf4e7727`](https://github.com/nodejs/node/commit/2adf4e7727)] - **test**: fix flaky test-perf-hooks-histogram (Santiago Gimeno) [#43567](https://github.com/nodejs/node/pull/43567)
* \[[`043756d540`](https://github.com/nodejs/node/commit/043756d540)] - **test**: fill DOMException names (LiviaMedeiros) [#43615](https://github.com/nodejs/node/pull/43615)
* \[[`e718a6e913`](https://github.com/nodejs/node/commit/e718a6e913)] - **test**: fix Buffer.from(ArrayBufferView) call (LiviaMedeiros) [#43614](https://github.com/nodejs/node/pull/43614)
* \[[`ac72f4e812`](https://github.com/nodejs/node/commit/ac72f4e812)] - **test**: mark test-worker-http2-stream-terminate flaky on all platforms (Finn Yu) [#43620](https://github.com/nodejs/node/pull/43620)
* \[[`dabccef69f`](https://github.com/nodejs/node/commit/dabccef69f)] - **test**: mark flaky tests on smartos (Feng Yu) [#43596](https://github.com/nodejs/node/pull/43596)
* \[[`705e85e736`](https://github.com/nodejs/node/commit/705e85e736)] - **test**: improve code coverage for SourceMap class (italo jose) [#43285](https://github.com/nodejs/node/pull/43285)
* \[[`7c6f548382`](https://github.com/nodejs/node/commit/7c6f548382)] - **test**: mark test-http-server-request-timeouts-mixed flaky on macOS (F3n67u) [#43597](https://github.com/nodejs/node/pull/43597)
* \[[`bd91337988`](https://github.com/nodejs/node/commit/bd91337988)] - **test**: refactor to top-level await (Meek Simbule) [#43500](https://github.com/nodejs/node/pull/43500)
* \[[`9940dc38c1`](https://github.com/nodejs/node/commit/9940dc38c1)] - **test**: skip test-v8-serialize-leak on IBM i (Richard Lau) [#43511](https://github.com/nodejs/node/pull/43511)
* \[[`17b92f0679`](https://github.com/nodejs/node/commit/17b92f0679)] - **test**: use unique file names in fs trace test (Ben Noordhuis) [#43504](https://github.com/nodejs/node/pull/43504)
* \[[`7ca58b8ee7`](https://github.com/nodejs/node/commit/7ca58b8ee7)] - **test**: allow EOVERFLOW errors in fs position tests (Richard Lau) [#43510](https://github.com/nodejs/node/pull/43510)
* \[[`eece34cddb`](https://github.com/nodejs/node/commit/eece34cddb)] - **test**: add WPT tests for dom/events (Daiki Nishikawa) [#43151](https://github.com/nodejs/node/pull/43151)
* \[[`70d297c271`](https://github.com/nodejs/node/commit/70d297c271)] - **test**: replace gc(true) with gc({ type: 'minor' }) (Tobias Nießen) [#43493](https://github.com/nodejs/node/pull/43493)
* \[[`1022c0d0d4`](https://github.com/nodejs/node/commit/1022c0d0d4)] - **test**: fix flaky test-https-server-close- tests (Santiago Gimeno) [#43216](https://github.com/nodejs/node/pull/43216)
* \[[`a9ab41cb38`](https://github.com/nodejs/node/commit/a9ab41cb38)] - **test**: refactor to top-level await (Meek Simbule) [#43366](https://github.com/nodejs/node/pull/43366)
* \[[`b1a7798821`](https://github.com/nodejs/node/commit/b1a7798821)] - **test**: skip test-net-connect-reset-until-connected on SmartOS (Filip Skokan) [#43449](https://github.com/nodejs/node/pull/43449)
* \[[`3b0703fd0d`](https://github.com/nodejs/node/commit/3b0703fd0d)] - **test**: rename `test-eventtarget-whatwg-*.js` (Daeyeon Jeong) [#43467](https://github.com/nodejs/node/pull/43467)
* \[[`5c0a24d5be`](https://github.com/nodejs/node/commit/5c0a24d5be)] - **test**: mark test-worker-http2-stream-terminate flaky on Windows (Darshan Sen) [#43425](https://github.com/nodejs/node/pull/43425)
* \[[`48ae00c0b1`](https://github.com/nodejs/node/commit/48ae00c0b1)] - **test**: improve coverage for load hooks (Antoine du Hamel) [#43374](https://github.com/nodejs/node/pull/43374)
* \[[`2b55b606f7`](https://github.com/nodejs/node/commit/2b55b606f7)] - _**Revert**_ "**test**: mark test\_buffer/test\_finalizer flaky" (Matteo Collina) [#43418](https://github.com/nodejs/node/pull/43418)
* \[[`3948accbf4`](https://github.com/nodejs/node/commit/3948accbf4)] - **test**: make node-api/test\_buffer/test\_finalizer not flaky (Matteo Collina) [#43418](https://github.com/nodejs/node/pull/43418)
* \[[`c954bcd20b`](https://github.com/nodejs/node/commit/c954bcd20b)] - **tools**: remove rpm build scripts (Ben Noordhuis) [#43647](https://github.com/nodejs/node/pull/43647)
* \[[`8a06b7b9d0`](https://github.com/nodejs/node/commit/8a06b7b9d0)] - **tools**: update lint-md-dependencies to rollup\@2.76.0 (Node.js GitHub Bot) [#43749](https://github.com/nodejs/node/pull/43749)
* \[[`aafdf1239e`](https://github.com/nodejs/node/commit/aafdf1239e)] - **tools**: refactor `tools/license2rtf` to ESM (Feng Yu) [#43232](https://github.com/nodejs/node/pull/43232)
* \[[`99ffabf2dd`](https://github.com/nodejs/node/commit/99ffabf2dd)] - **tools**: update eslint to 8.19.0 (Node.js GitHub Bot) [#43662](https://github.com/nodejs/node/pull/43662)
* \[[`c6396c179f`](https://github.com/nodejs/node/commit/c6396c179f)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#43572](https://github.com/nodejs/node/pull/43572)
* \[[`8d14d6e215`](https://github.com/nodejs/node/commit/8d14d6e215)] - **tools**: fix CJS/ESM toggle on small screens (Antoine du Hamel) [#43506](https://github.com/nodejs/node/pull/43506)
* \[[`59d4da699e`](https://github.com/nodejs/node/commit/59d4da699e)] - **tools**: update eslint to 8.18.0 (Node.js GitHub Bot) [#43479](https://github.com/nodejs/node/pull/43479)
* \[[`752380a959`](https://github.com/nodejs/node/commit/752380a959)] - **tools**: update main branch name (Feng Yu) [#43440](https://github.com/nodejs/node/pull/43440)
* \[[`06c367ef8b`](https://github.com/nodejs/node/commit/06c367ef8b)] - **tools**: update lint-md-dependencies to rollup\@2.75.6 (Node.js GitHub Bot) [#43386](https://github.com/nodejs/node/pull/43386)

<a id="18.5.0"></a>

## 2022-07-07, Version 18.5.0 (Current), @RafaelGSS

This is a security release.

### Notable changes

The following CVEs are fixed in this release:

* **[CVE-2022-2097](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-2097)**: OpenSSL - AES OCB fails to encrypt some bytes (Medium)
* **[CVE-2022-32212](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-32212)**: DNS rebinding in --inspect via invalid IP addresses (High)
* **[CVE-2022-32213](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-32213)**: HTTP Request Smuggling - Flawed Parsing of Transfer-Encoding (Medium)
* **[CVE-2022-32214](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-32214)**: HTTP Request Smuggling - Improper Delimiting of Header Fields (Medium)
* **[CVE-2022-32215](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-32215)**: HTTP Request Smuggling - Incorrect Parsing of Multi-line Transfer-Encoding (Medium)
* **[CVE-2022-32222](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-32222)**: Attempt to read openssl.cnf from /home/iojs/build/ upon startup (Medium)
* **[CVE-2022-32223](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-32223)**: DLL Hijacking on Windows (High)

More detailed information on each of the vulnerabilities can be found in [July 7th 2022 Security Releases](https://nodejs.org/en/blog/vulnerability/july-2022-security-releases/) blog post.

#### llhttp updated to 6.0.7

`llhttp` is updated to 6.0.7 which includes fixes for the following vulnerabilities.

* **HTTP Request Smuggling - Flawed Parsing of Transfer-Encoding (Medium)([CVE-2022-32213](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-32214) )**: The `llhttp` parser in the `http` module does not correctly parse and validate Transfer-Encoding headers. This can lead to HTTP Request Smuggling (HRS).
* **HTTP Request Smuggling - Improper Delimiting of Header Fields (Medium)([CVE-2022-32214](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-32214))**: The `llhttp` parser in the `http` module does not strictly use the CRLF sequence to delimit HTTP requests. This can lead to HTTP Request Smuggling.
  * **Note**: This can be considered a breaking change due to disabling LF header delimiting. To enable LF header delimiting you can specify the `--insecure-http-parser` command-line flag, but note that this will additionally enable other insecure behaviours.
* **HTTP Request Smuggling - Incorrect Parsing of Multi-line Transfer-Encoding (Medium)([CVE-2022-32215](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-32215))**: The `llhttp` parser in the `http` module does not correctly handle multi-line Transfer-Encoding headers. This can lead to HTTP Request Smuggling (HRS).

Some of these fixes required breaking changes, so you may be impacted by this update.

#### Default OpenSSL Configuration

To resolve **[CVE-2022-32223](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-32223)**: DLL Hijacking on Windows (High), changes were made to how Node.js loads OpenSSL configuration by default.

**[CVE-2022-32223](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-32223)** could be exploited if the victim has the following dependencies on Windows machine:

* OpenSSL has been installed and `C:\Program Files\Common Files\SSL\openssl.cnf` exists.

Whenever the above conditions are present, `node.exe` will search for `providers.dll` in the current user directory. After that, `node.exe` will try to search for `providers.dll` by the DLL Search Order in Windows. An attacker can place the malicious file `providers.dll` under a variety of paths to exploit this vulnerability.

Node.js can use an OpenSSL configuration file by specifying the environment variable `OPENSSL_CONF`, or using the command-line option `--openssl-conf`, and if none of those are specified will default to reading the default OpenSSL configuration file `openssl.cnf`.

From this release, Node.js will only read a section that is, by default, named `nodejs_conf`. If you were previously relying on the configuration specified in the shared section of the `openssl.cnf` file, you may be affected by this change. You can fall back to the previous behavior to read the default section by:

* Specifying the `--openssl-shared-config` command-line flag; or
* Creating a new `nodejs_conf` section in that file and copying the contents of the default section into the new `nodejs_conf` section.

Note that when specifying `--openssl-shared-config` or defining `nodejs_conf` in your `openssl.cnf`, you should be cautious and review your configuration as it could lead to you being vulnerable to similar DLL exploit attacks.

### Commits

* \[[`dc7af13486`](https://github.com/nodejs/node/commit/dc7af13486)] - **deps**: update archs files for quictls/openssl-3.0.5+quic (RafaelGSS) [#43693](https://github.com/nodejs/node/pull/43693)
* \[[`fa72c534eb`](https://github.com/nodejs/node/commit/fa72c534eb)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.5+quic (RafaelGSS) [#43693](https://github.com/nodejs/node/pull/43693)
* \[[`a5fc2deb43`](https://github.com/nodejs/node/commit/a5fc2deb43)] - **deps**: update default openssl.cnf directory (Michael Dawson) [nodejs-private/node-private#335](https://github.com/nodejs-private/node-private/pull/335)
* \[[`f2407748e3`](https://github.com/nodejs/node/commit/f2407748e3)] - **(SEMVER-MAJOR)** **http**: stricter Transfer-Encoding and header separator parsing (Paolo Insogna) [nodejs-private/node-private#315](https://github.com/nodejs-private/node-private/pull/315)
* \[[`e4af5eba95`](https://github.com/nodejs/node/commit/e4af5eba95)] - **src**: fix IPv4 validation in inspector\_socket (Tobias Nießen) [nodejs-private/node-private#320](https://github.com/nodejs-private/node-private/pull/320)
* \[[`3f0c3e142d`](https://github.com/nodejs/node/commit/3f0c3e142d)] - **(SEMVER-MAJOR)** **src,deps,build,test**: add OpenSSL config appname (Daniel Bevenius) [#43124](https://github.com/nodejs/node/pull/43124)
* \[[`9578158ff8`](https://github.com/nodejs/node/commit/9578158ff8)] - **(SEMVER-MINOR)** **src,doc,test**: add --openssl-shared-config option (Daniel Bevenius) [#43124](https://github.com/nodejs/node/pull/43124)

<a id="18.4.0"></a>

## 2022-06-16, Version 18.4.0 (Current), @danielleadams

### Notable Changes

* **crypto**:
  * remove Node.js-specific webcrypto extensions (Filip Skokan) [#43310](https://github.com/nodejs/node/pull/43310)
  * add CFRG curves to Web Crypto API (Filip Skokan) [#42507](https://github.com/nodejs/node/pull/42507)
* **dns**:
  * accept `'IPv4'` and `'IPv6'` for `family` (Antoine du Hamel) [#43054](https://github.com/nodejs/node/pull/43054)
* **report**:
  * add more heap infos in process report (theanarkh) [#43116](https://github.com/nodejs/node/pull/43116)

### Commits

* \[[`702bfa0b7c`](https://github.com/nodejs/node/commit/702bfa0b7c)] - **async\_hooks**: use `kEmptyObject` (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`f7c4015fd8`](https://github.com/nodejs/node/commit/f7c4015fd8)] - **bootstrap**: consolidate global properties definition (Chengzhong Wu) [#43357](https://github.com/nodejs/node/pull/43357)
* \[[`8d892f5259`](https://github.com/nodejs/node/commit/8d892f5259)] - **build**: add nonpm and nocorepack to vcbuild.bat (Darshan Sen) [#43219](https://github.com/nodejs/node/pull/43219)
* \[[`4109ddc005`](https://github.com/nodejs/node/commit/4109ddc005)] - **child\_process**: use `kEmptyObject` (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`7b5cb14f0c`](https://github.com/nodejs/node/commit/7b5cb14f0c)] - **cluster**: use `kEmptyObject` (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`9f1de2c005`](https://github.com/nodejs/node/commit/9f1de2c005)] - **crypto**: fix webcrypto import of cfrg raw public keys (Filip Skokan) [#43404](https://github.com/nodejs/node/pull/43404)
* \[[`7f02e22998`](https://github.com/nodejs/node/commit/7f02e22998)] - **crypto**: test webcrypto ec raw public key import (Filip Skokan) [#43405](https://github.com/nodejs/node/pull/43405)
* \[[`0a075cb548`](https://github.com/nodejs/node/commit/0a075cb548)] - **crypto**: fix webcrypto JWK EC and OKP import crv check (Filip Skokan) [#43346](https://github.com/nodejs/node/pull/43346)
* \[[`df0903c8e8`](https://github.com/nodejs/node/commit/df0903c8e8)] - **crypto**: use `kEmptyObject` (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`6d0053345e`](https://github.com/nodejs/node/commit/6d0053345e)] - **(SEMVER-MINOR)** **crypto**: remove Node.js-specific webcrypto extensions (Filip Skokan) [#43310](https://github.com/nodejs/node/pull/43310)
* \[[`28c034d6b5`](https://github.com/nodejs/node/commit/28c034d6b5)] - **(SEMVER-MINOR)** **crypto**: add CFRG curves to Web Crypto API (Filip Skokan) [#42507](https://github.com/nodejs/node/pull/42507)
* \[[`fe7fd85109`](https://github.com/nodejs/node/commit/fe7fd85109)] - **deps**: update Corepack to 0.11.2 (Maël Nison) [#43402](https://github.com/nodejs/node/pull/43402)
* \[[`517f17b214`](https://github.com/nodejs/node/commit/517f17b214)] - **deps**: update undici to 5.5.1 (Node.js GitHub Bot) [#43412](https://github.com/nodejs/node/pull/43412)
* \[[`f4c830fbe4`](https://github.com/nodejs/node/commit/f4c830fbe4)] - **deps**: upgrade npm to 8.12.1 (npm CLI robot) [#43301](https://github.com/nodejs/node/pull/43301)
* \[[`0bb84b09a5`](https://github.com/nodejs/node/commit/0bb84b09a5)] - **(SEMVER-MINOR)** **dns**: accept `'IPv4'` and `'IPv6'` for `family` (Antoine du Hamel) [#43054](https://github.com/nodejs/node/pull/43054)
* \[[`f91babe494`](https://github.com/nodejs/node/commit/f91babe494)] - **doc**: packages documentation updates for 12 EOL (Guy Bedford) [#43375](https://github.com/nodejs/node/pull/43375)
* \[[`066f963ec1`](https://github.com/nodejs/node/commit/066f963ec1)] - **doc**: add initial doc on how to update cjs-module-lexer (Michael Dawson) [#43255](https://github.com/nodejs/node/pull/43255)
* \[[`36e5684ae0`](https://github.com/nodejs/node/commit/36e5684ae0)] - **doc**: clarify use of deps/icu-small (Michael Dawson) [#43287](https://github.com/nodejs/node/pull/43287)
* \[[`b9634e7ef3`](https://github.com/nodejs/node/commit/b9634e7ef3)] - **doc**: remove llnode from diag tierlist (Tony Gorez) [#43289](https://github.com/nodejs/node/pull/43289)
* \[[`4caeb10e7b`](https://github.com/nodejs/node/commit/4caeb10e7b)] - **doc**: remove ETW from diag tierlist (Tony Gorez) [#43295](https://github.com/nodejs/node/pull/43295)
* \[[`41955e5ce5`](https://github.com/nodejs/node/commit/41955e5ce5)] - **doc**: use serial comma in report docs (Tobias Nießen) [#43394](https://github.com/nodejs/node/pull/43394)
* \[[`e30d4c1cb0`](https://github.com/nodejs/node/commit/e30d4c1cb0)] - **doc**: add fspromises mkdir example (Tierney Cyren) [#40843](https://github.com/nodejs/node/pull/40843)
* \[[`adec5fa929`](https://github.com/nodejs/node/commit/adec5fa929)] - **doc**: add F3n67u to triagers (Feng Yu) [#43350](https://github.com/nodejs/node/pull/43350)
* \[[`cc3505b192`](https://github.com/nodejs/node/commit/cc3505b192)] - **doc**: fix typo in globals.md (Daeyeon Jeong) [#43365](https://github.com/nodejs/node/pull/43365)
* \[[`052c8eaf6a`](https://github.com/nodejs/node/commit/052c8eaf6a)] - **doc**: use serial comma in webstreams docs (Tobias Nießen) [#43353](https://github.com/nodejs/node/pull/43353)
* \[[`b824a0b7d0`](https://github.com/nodejs/node/commit/b824a0b7d0)] - **doc**: fix specifier example in `esm.md` (hiroki osame) [#43351](https://github.com/nodejs/node/pull/43351)
* \[[`d558b3c028`](https://github.com/nodejs/node/commit/d558b3c028)] - **doc**: add undici to glossary (F3n67u) [#43327](https://github.com/nodejs/node/pull/43327)
* \[[`f9ad98f5cb`](https://github.com/nodejs/node/commit/f9ad98f5cb)] - **doc**: change glossary link in pull request guide to node's glossary doc (Feng Yu) [#43318](https://github.com/nodejs/node/pull/43318)
* \[[`02944a6783`](https://github.com/nodejs/node/commit/02944a6783)] - **doc**: fix typo in util.parseArgs usage example (Michael Ficarra) [#43332](https://github.com/nodejs/node/pull/43332)
* \[[`f2bc6a3f71`](https://github.com/nodejs/node/commit/f2bc6a3f71)] - **doc**: improve description of TZ (Tobias Nießen) [#43334](https://github.com/nodejs/node/pull/43334)
* \[[`9335ea6c35`](https://github.com/nodejs/node/commit/9335ea6c35)] - **doc**: use serial comma in net docs (Tobias Nießen) [#43335](https://github.com/nodejs/node/pull/43335)
* \[[`05f38c6c3e`](https://github.com/nodejs/node/commit/05f38c6c3e)] - **doc**: make clear the result of comparison between Symbol.for (Kohei Ueno) [#43309](https://github.com/nodejs/node/pull/43309)
* \[[`c9aed9de9f`](https://github.com/nodejs/node/commit/c9aed9de9f)] - **doc**: add missing require to stream api doc (Feng Yu) [#43237](https://github.com/nodejs/node/pull/43237)
* \[[`f3188c1c9c`](https://github.com/nodejs/node/commit/f3188c1c9c)] - **doc**: add CIGTM to `glossary.md` (Feng Yu) [#43316](https://github.com/nodejs/node/pull/43316)
* \[[`c572d2d115`](https://github.com/nodejs/node/commit/c572d2d115)] - **doc**: use serial comma in pull request doc (Feng Yu) [#43319](https://github.com/nodejs/node/pull/43319)
* \[[`8a4e1fa002`](https://github.com/nodejs/node/commit/8a4e1fa002)] - **doc**: use serial comma in ESM docs (Tobias Nießen) [#43322](https://github.com/nodejs/node/pull/43322)
* \[[`fff0560a66`](https://github.com/nodejs/node/commit/fff0560a66)] - **doc**: promote cdt to tier 3 (Tony Gorez) [#43290](https://github.com/nodejs/node/pull/43290)
* \[[`7d0f6da97f`](https://github.com/nodejs/node/commit/7d0f6da97f)] - **doc**: fix chromium document link in pull-requests.md (rikapo) [#43265](https://github.com/nodejs/node/pull/43265)
* \[[`4674b0d2a5`](https://github.com/nodejs/node/commit/4674b0d2a5)] - **doc**: fix 404 link of BUILDING.md (Feng Yu) [#43234](https://github.com/nodejs/node/pull/43234)
* \[[`ee392c5c0b`](https://github.com/nodejs/node/commit/ee392c5c0b)] - **doc**: update CHANGELOG\_V18.md (Filip Skokan) [#43298](https://github.com/nodejs/node/pull/43298)
* \[[`5a3a2a197f`](https://github.com/nodejs/node/commit/5a3a2a197f)] - **doc**: add src/crypto to CC list for nodejs/crypto (Tobias Nießen) [#43286](https://github.com/nodejs/node/pull/43286)
* \[[`69ce50396c`](https://github.com/nodejs/node/commit/69ce50396c)] - **doc**: use serial comma in console docs (Tobias Nießen) [#43257](https://github.com/nodejs/node/pull/43257)
* \[[`0c5092c51c`](https://github.com/nodejs/node/commit/0c5092c51c)] - **events**: fix adding abort listener in `events.once` (Daeyeon Jeong) [#43373](https://github.com/nodejs/node/pull/43373)
* \[[`fda2105481`](https://github.com/nodejs/node/commit/fda2105481)] - **events**: use `kEmptyObject` (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`63bf49b143`](https://github.com/nodejs/node/commit/63bf49b143)] - **fs**: use `kEmptyObject` (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`9b764531b9`](https://github.com/nodejs/node/commit/9b764531b9)] - **fs**: export constants from `fs/promises` (Feng Yu) [#43177](https://github.com/nodejs/node/pull/43177)
* \[[`a4409f85f8`](https://github.com/nodejs/node/commit/a4409f85f8)] - **http**: use `kEmptyObject` (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`2ffd54105a`](https://github.com/nodejs/node/commit/2ffd54105a)] - **http2**: use `kEmptyObject` (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`b468b8fe51`](https://github.com/nodejs/node/commit/b468b8fe51)] - **https**: use `kEmptyObject` (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`d2a98dc6cf`](https://github.com/nodejs/node/commit/d2a98dc6cf)] - **inspector**: add missing initialization (Michael Dawson) [#43254](https://github.com/nodejs/node/pull/43254)
* \[[`3b2f7eed39`](https://github.com/nodejs/node/commit/3b2f7eed39)] - **lib**: use `kEmptyObject` in various places (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`4a9511d971`](https://github.com/nodejs/node/commit/4a9511d971)] - **lib**: give names to promisified methods (LiviaMedeiros) [#43218](https://github.com/nodejs/node/pull/43218)
* \[[`b8644606eb`](https://github.com/nodejs/node/commit/b8644606eb)] - **lib**: use null-prototype objects for property descriptors (Antoine du Hamel) [#43270](https://github.com/nodejs/node/pull/43270)
* \[[`64edd6cbc3`](https://github.com/nodejs/node/commit/64edd6cbc3)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#43399](https://github.com/nodejs/node/pull/43399)
* \[[`b05cea57ba`](https://github.com/nodejs/node/commit/b05cea57ba)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#43387](https://github.com/nodejs/node/pull/43387)
* \[[`a8ecec57e3`](https://github.com/nodejs/node/commit/a8ecec57e3)] - **meta**: move one or more collaborators to emeritus (#43183) (Node.js GitHub Bot) [#43183](https://github.com/nodejs/node/pull/43183)
* \[[`60dc36244a`](https://github.com/nodejs/node/commit/60dc36244a)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#43312](https://github.com/nodejs/node/pull/43312)
* \[[`9803b82ac7`](https://github.com/nodejs/node/commit/9803b82ac7)] - **net,dns**: move hasObserver out of perf function (theanarkh) [#43217](https://github.com/nodejs/node/pull/43217)
* \[[`112518fb1d`](https://github.com/nodejs/node/commit/112518fb1d)] - **perf\_hooks**: fix function wrapped by `timerify` to work correctly (Kohei Ueno) [#43330](https://github.com/nodejs/node/pull/43330)
* \[[`a3310d13bf`](https://github.com/nodejs/node/commit/a3310d13bf)] - **perf\_hooks**: use `kEmptyObject` (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`7e8a00a26d`](https://github.com/nodejs/node/commit/7e8a00a26d)] - **readline**: fix question stack overflow (Eugene Chapko) [#43320](https://github.com/nodejs/node/pull/43320)
* \[[`5e98cacf77`](https://github.com/nodejs/node/commit/5e98cacf77)] - **readline**: use `kEmptyObject` (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`66d956ca49`](https://github.com/nodejs/node/commit/66d956ca49)] - **repl**: make autocomplete case-insensitive (Sergey Petushkov) [#41632](https://github.com/nodejs/node/pull/41632)
* \[[`201f3d7f56`](https://github.com/nodejs/node/commit/201f3d7f56)] - **(SEMVER-MINOR)** **report**: add more heap infos in process report (theanarkh) [#43116](https://github.com/nodejs/node/pull/43116)
* \[[`a0568409b6`](https://github.com/nodejs/node/commit/a0568409b6)] - **src**: fix json utils escapes for U+000B (Chengzhong Wu) [#43206](https://github.com/nodejs/node/pull/43206)
* \[[`931ecfa033`](https://github.com/nodejs/node/commit/931ecfa033)] - **src**: fix memory leaks and refactor `ByteSource` (Tobias Nießen) [#43202](https://github.com/nodejs/node/pull/43202)
* \[[`5e65c1f3da`](https://github.com/nodejs/node/commit/5e65c1f3da)] - **src**: convey potential exceptions during StreamPipe construction (Darshan Sen) [#43240](https://github.com/nodejs/node/pull/43240)
* \[[`b200a5ff67`](https://github.com/nodejs/node/commit/b200a5ff67)] - **stream**: use `kEmptyObject` (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`1cc1a57cdb`](https://github.com/nodejs/node/commit/1cc1a57cdb)] - **test**: remove unused argument in test-util-inspect.js (Colin Ihrig) [#43395](https://github.com/nodejs/node/pull/43395)
* \[[`42c2115a82`](https://github.com/nodejs/node/commit/42c2115a82)] - **test**: mark test\_buffer/test\_finalizer flaky (Michael Dawson) [#43414](https://github.com/nodejs/node/pull/43414)
* \[[`71802c32d0`](https://github.com/nodejs/node/commit/71802c32d0)] - **test**: fix address in use error (Caleb Everett) [#43199](https://github.com/nodejs/node/pull/43199)
* \[[`e1b8c85a7a`](https://github.com/nodejs/node/commit/e1b8c85a7a)] - **test**: add test for short-option followed by its value (Kohei Ueno) [#43358](https://github.com/nodejs/node/pull/43358)
* \[[`f8d26c6011`](https://github.com/nodejs/node/commit/f8d26c6011)] - **test**: fix `common.mustNotCall` error message (Antoine du Hamel) [#42917](https://github.com/nodejs/node/pull/42917)
* \[[`18fffe6108`](https://github.com/nodejs/node/commit/18fffe6108)] - **test**: convert then to async/await (Meek Simbule) [#43292](https://github.com/nodejs/node/pull/43292)
* \[[`acd96d80eb`](https://github.com/nodejs/node/commit/acd96d80eb)] - **test**: add `BigInt`s to `common.getArrayBufferViews()` (LiviaMedeiros) [#43235](https://github.com/nodejs/node/pull/43235)
* \[[`e576a7fa50`](https://github.com/nodejs/node/commit/e576a7fa50)] - **test\_runner**: use `kEmptyObject` (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`fecad7a3a5`](https://github.com/nodejs/node/commit/fecad7a3a5)] - **timers**: use `kEmptyObject` (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`e31baca76a`](https://github.com/nodejs/node/commit/e31baca76a)] - **tls**: use `kEmptyObject` (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`7f8f61a749`](https://github.com/nodejs/node/commit/7f8f61a749)] - **tls**: fix performance regression in `convertALPNProtocols()` (LiviaMedeiros) [#43250](https://github.com/nodejs/node/pull/43250)
* \[[`ac9599a718`](https://github.com/nodejs/node/commit/ac9599a718)] - **tools**: report unsafe string and regex primordials as lint errors (Antoine du Hamel) [#43393](https://github.com/nodejs/node/pull/43393)
* \[[`b69d874592`](https://github.com/nodejs/node/commit/b69d874592)] - **tools**: fix `create-or-update-pull-request-action` hash on GHA (Antoine du Hamel) [#43378](https://github.com/nodejs/node/pull/43378)
* \[[`cf8a115983`](https://github.com/nodejs/node/commit/cf8a115983)] - **tools**: add `avoid-prototype-pollution` lint rule (Antoine du Hamel) [#43308](https://github.com/nodejs/node/pull/43308)
* \[[`8c0fe1e184`](https://github.com/nodejs/node/commit/8c0fe1e184)] - **tools**: fix find-inactive actions (LiviaMedeiros) [#43377](https://github.com/nodejs/node/pull/43377)
* \[[`7f45d69f83`](https://github.com/nodejs/node/commit/7f45d69f83)] - **tools**: update lint-md-dependencies to rollup\@2.75.5 (Node.js GitHub Bot) [#43313](https://github.com/nodejs/node/pull/43313)
* \[[`d5d0f01c5a`](https://github.com/nodejs/node/commit/d5d0f01c5a)] - **tools**: update eslint to 8.17.0 (Node.js GitHub Bot) [#43314](https://github.com/nodejs/node/pull/43314)
* \[[`f598fe1585`](https://github.com/nodejs/node/commit/f598fe1585)] - **tools**: use hashes instead of tags for external actions (#43284) (Antoine du Hamel) [#43284](https://github.com/nodejs/node/pull/43284)
* \[[`10f79947d9`](https://github.com/nodejs/node/commit/10f79947d9)] - **tools**: update `codecov/codecov-action` version (Antoine du Hamel) [#43297](https://github.com/nodejs/node/pull/43297)
* \[[`f93848fa50`](https://github.com/nodejs/node/commit/f93848fa50)] - **tools**: update lint-md-dependencies to rollup\@2.75.3 (Node.js GitHub Bot) [#43261](https://github.com/nodejs/node/pull/43261)
* \[[`b3d7dc1de8`](https://github.com/nodejs/node/commit/b3d7dc1de8)] - **tools**: update clang-format 1.7.0 to 1.8.0 (Darshan Sen) [#43241](https://github.com/nodejs/node/pull/43241)
* \[[`812140c65a`](https://github.com/nodejs/node/commit/812140c65a)] - **tools,doc**: add guards against prototype pollution when creating proxies (Antoine du Hamel) [#43391](https://github.com/nodejs/node/pull/43391)
* \[[`56b8cc5cef`](https://github.com/nodejs/node/commit/56b8cc5cef)] - **util**: freeze `kEnumerableProperty` (LiviaMedeiros) [#43390](https://github.com/nodejs/node/pull/43390)
* \[[`b187d55b6d`](https://github.com/nodejs/node/commit/b187d55b6d)] - **util**: add `kEmptyObject` to internal/util (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`024b396275`](https://github.com/nodejs/node/commit/024b396275)] - **vm**: use `kEmptyObject` (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`7fc432fa35`](https://github.com/nodejs/node/commit/7fc432fa35)] - **wasi**: use `kEmptyObject` (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)
* \[[`44b65d0ca7`](https://github.com/nodejs/node/commit/44b65d0ca7)] - **worker**: use `kEmptyObject` (LiviaMedeiros) [#43159](https://github.com/nodejs/node/pull/43159)

<a id="18.3.0"></a>

## 2022-06-01, Version 18.3.0 (Current), @bengl

### Notable Changes

* \[[`dc3b91f351`](https://github.com/nodejs/node/commit/dc3b91f351)] - **deps**: update undici to 5.4.0  (Node.js GitHub Bot) [#43262](https://github.com/nodejs/node/pull/43262)
* \[[`d6cf409d78`](https://github.com/nodejs/node/commit/d6cf409d78)] - **(SEMVER-MINOR)** **util**: add parseArgs module (Benjamin Coe) [#42675](https://github.com/nodejs/node/pull/42675)
* \[[`9539cfa358`](https://github.com/nodejs/node/commit/9539cfa358)] - **(SEMVER-MINOR)** **http**: add uniqueHeaders option to request and createServer (Paolo Insogna) [#41397](https://github.com/nodejs/node/pull/41397)
* \[[`41fdc2617d`](https://github.com/nodejs/node/commit/41fdc2617d)] - **deps**: upgrade npm to 8.11.0 (npm team) [#43210](https://github.com/nodejs/node/pull/43210)
* \[[`0000654e47`](https://github.com/nodejs/node/commit/0000654e47)] - **deps**: patch V8 to 10.2.154.4 (Michaël Zasso) [#43067](https://github.com/nodejs/node/pull/43067)
* \[[`b3c8e609fd`](https://github.com/nodejs/node/commit/b3c8e609fd)] - **(SEMVER-MINOR)** **deps**: update V8 to 10.2.154.2 (Michaël Zasso) [#42740](https://github.com/nodejs/node/pull/42740)
* \[[`3e89b7336d`](https://github.com/nodejs/node/commit/3e89b7336d)] - **(SEMVER-MINOR)** **fs**: make params in writing methods optional (LiviaMedeiros) [#42601](https://github.com/nodejs/node/pull/42601)
* \[[`9539cfa358`](https://github.com/nodejs/node/commit/9539cfa358)] - **(SEMVER-MINOR)** **http**: add uniqueHeaders option to request and createServer (Paolo Insogna) [#41397](https://github.com/nodejs/node/pull/41397)
* \[[`8f5b4570e5`](https://github.com/nodejs/node/commit/8f5b4570e5)] - **(SEMVER-MINOR)** **net**: add ability to reset a tcp socket (pupilTong) [#43112](https://github.com/nodejs/node/pull/43112)
* \[[`5eff7b4a6a`](https://github.com/nodejs/node/commit/5eff7b4a6a)] - **(SEMVER-MINOR)** _**Revert**_ "**build**: make x86 Windows support temporarily experimental" (Michaël Zasso) [#42740](https://github.com/nodejs/node/pull/42740)
  * This means 32-bit Windows binaries are back with this release.

### Commits

* \[[`aefc9dda9a`](https://github.com/nodejs/node/commit/aefc9dda9a)] - **benchmark**: add node-error benchmark (RafaelGSS) [#43077](https://github.com/nodejs/node/pull/43077)
* \[[`85d81a764f`](https://github.com/nodejs/node/commit/85d81a764f)] - **bootstrap**: include code cache in the embedded snapshot (Joyee Cheung) [#43023](https://github.com/nodejs/node/pull/43023)
* \[[`5eff7b4a6a`](https://github.com/nodejs/node/commit/5eff7b4a6a)] - **(SEMVER-MINOR)** _**Revert**_ "**build**: make x86 Windows support temporarily experimental" (Michaël Zasso) [#42740](https://github.com/nodejs/node/pull/42740)
* \[[`d6634707c5`](https://github.com/nodejs/node/commit/d6634707c5)] - **(SEMVER-MINOR)** **build**: run V8 tests with detected Python version (Richard Lau) [#42740](https://github.com/nodejs/node/pull/42740)
* \[[`b8b5e6df67`](https://github.com/nodejs/node/commit/b8b5e6df67)] - **(SEMVER-MINOR)** **build**: reset embedder string to "-node.0" (Michaël Zasso) [#42740](https://github.com/nodejs/node/pull/42740)
* \[[`285ef30877`](https://github.com/nodejs/node/commit/285ef30877)] - **console**: fix console.dir crash on a revoked proxy (Daeyeon Jeong) [#43100](https://github.com/nodejs/node/pull/43100)
* \[[`920d8c5300`](https://github.com/nodejs/node/commit/920d8c5300)] - **crypto**: align webcrypto RSA key import/export with other implementations (Filip Skokan) [#42816](https://github.com/nodejs/node/pull/42816)
* \[[`c76caadf2e`](https://github.com/nodejs/node/commit/c76caadf2e)] - **debugger**: throw a more useful error when the frame is missing (Kohei Ueno) [#42776](https://github.com/nodejs/node/pull/42776)
* \[[`dc3b91f351`](https://github.com/nodejs/node/commit/dc3b91f351)] - **deps**: update undici to 5.4.0  (Node.js GitHub Bot) [#43262](https://github.com/nodejs/node/pull/43262)
* \[[`35250bf2f6`](https://github.com/nodejs/node/commit/35250bf2f6)] - **deps**: regenerate OpenSSL archs files (Daniel Bevenius) [#42973](https://github.com/nodejs/node/pull/42973)
* \[[`ecacc3a727`](https://github.com/nodejs/node/commit/ecacc3a727)] - **deps**: exclude linker scripts for windows builds (Daniel Bevenius) [#42973](https://github.com/nodejs/node/pull/42973)
* \[[`41fdc2617d`](https://github.com/nodejs/node/commit/41fdc2617d)] - **deps**: upgrade npm to 8.11.0 (npm team) [#43210](https://github.com/nodejs/node/pull/43210)
* \[[`87b248e27e`](https://github.com/nodejs/node/commit/87b248e27e)] - **deps**: update undici to 5.3.0 (Node.js GitHub Bot) [#43197](https://github.com/nodejs/node/pull/43197)
* \[[`d42de132a7`](https://github.com/nodejs/node/commit/d42de132a7)] - **deps**: upgrade npm to 8.10.0 (npm team) [#43061](https://github.com/nodejs/node/pull/43061)
* \[[`0000654e47`](https://github.com/nodejs/node/commit/0000654e47)] - **deps**: patch V8 to 10.2.154.4 (Michaël Zasso) [#43067](https://github.com/nodejs/node/pull/43067)
* \[[`742ffefb44`](https://github.com/nodejs/node/commit/742ffefb44)] - **(SEMVER-MINOR)** **deps**: make V8 10.2 ABI-compatible with 10.1 (Michaël Zasso) [#42740](https://github.com/nodejs/node/pull/42740)
* \[[`c626a533c7`](https://github.com/nodejs/node/commit/c626a533c7)] - **deps**: make V8 compilable with older glibc (Michaël Zasso) [#42657](https://github.com/nodejs/node/pull/42657)
* \[[`76546b12a2`](https://github.com/nodejs/node/commit/76546b12a2)] - **deps**: V8: fix v8-cppgc.h for MSVC (Jiawen Geng) [#42657](https://github.com/nodejs/node/pull/42657)
* \[[`45e1fd4473`](https://github.com/nodejs/node/commit/45e1fd4473)] - **deps**: silence V8's warning on CompileFunction (Michaël Zasso) [#40907](https://github.com/nodejs/node/pull/40907)
* \[[`2130891a9a`](https://github.com/nodejs/node/commit/2130891a9a)] - **(SEMVER-MINOR)** **deps**: disable trap handler for Windows cross-compiler (Michaël Zasso) [#40488](https://github.com/nodejs/node/pull/40488)
* \[[`e678b6c63d`](https://github.com/nodejs/node/commit/e678b6c63d)] - **deps**: fix V8 build issue with inline methods (Jiawen Geng) [#38807](https://github.com/nodejs/node/pull/38807)
* \[[`f83323f304`](https://github.com/nodejs/node/commit/f83323f304)] - **deps**: V8: forward declaration of `Rtl*FunctionTable` (Refael Ackermann) [#32116](https://github.com/nodejs/node/pull/32116)
* \[[`4be7584ca6`](https://github.com/nodejs/node/commit/4be7584ca6)] - **deps**: V8: un-cherry-pick bd019bd (Refael Ackermann) [#32116](https://github.com/nodejs/node/pull/32116)
* \[[`b3c8e609fd`](https://github.com/nodejs/node/commit/b3c8e609fd)] - **(SEMVER-MINOR)** **deps**: update V8 to 10.2.154.2 (Michaël Zasso) [#42740](https://github.com/nodejs/node/pull/42740)
* \[[`0afdc3e9a8`](https://github.com/nodejs/node/commit/0afdc3e9a8)] - **doc**: use serial comma in errors docs (Tobias Nießen) [#43242](https://github.com/nodejs/node/pull/43242)
* \[[`00989338b3`](https://github.com/nodejs/node/commit/00989338b3)] - **doc**: add note regarding `%Array.prototype.concat%` in `primordials.md` (Antoine du Hamel) [#43166](https://github.com/nodejs/node/pull/43166)
* \[[`badf72dd0a`](https://github.com/nodejs/node/commit/badf72dd0a)] - **doc**: use serial comma in worker\_threads docs (Tobias Nießen) [#43220](https://github.com/nodejs/node/pull/43220)
* \[[`776e746f0a`](https://github.com/nodejs/node/commit/776e746f0a)] - **doc**: fix napi version for node\_api\_symbol\_for (Danielle Adams) [#42878](https://github.com/nodejs/node/pull/42878)
* \[[`76b46801f8`](https://github.com/nodejs/node/commit/76b46801f8)] - **doc**: document `signal` option for `EventTarget#addEventListener` (Antoine du Hamel) [#43170](https://github.com/nodejs/node/pull/43170)
* \[[`3082c75efd`](https://github.com/nodejs/node/commit/3082c75efd)] - **doc**: make minor adjustments (LiviaMedeiros) [#43175](https://github.com/nodejs/node/pull/43175)
* \[[`598c1f102e`](https://github.com/nodejs/node/commit/598c1f102e)] - **doc**: use serial comma in dgram docs (Tobias Nießen) [#43191](https://github.com/nodejs/node/pull/43191)
* \[[`b772c13a62`](https://github.com/nodejs/node/commit/b772c13a62)] - **doc**: use serial comma in process docs (Tobias Nießen) [#43179](https://github.com/nodejs/node/pull/43179)
* \[[`f90a3d7a54`](https://github.com/nodejs/node/commit/f90a3d7a54)] - **doc**: improved parallel specification (mawaregetsuka) [#42679](https://github.com/nodejs/node/pull/42679)
* \[[`71074eedef`](https://github.com/nodejs/node/commit/71074eedef)] - **doc**: improve callback params for `fs.mkdir` (Daeyeon Jeong) [#43016](https://github.com/nodejs/node/pull/43016)
* \[[`2b8b224077`](https://github.com/nodejs/node/commit/2b8b224077)] - **doc**: remove outdated footnote (Python 2 --> 3 for V8 tests) (DeeDeeG) [#43105](https://github.com/nodejs/node/pull/43105)
* \[[`945f228cf1`](https://github.com/nodejs/node/commit/945f228cf1)] - **doc**: add release key for RafaelGSS (Rafael Gonzaga) [#43131](https://github.com/nodejs/node/pull/43131)
* \[[`56fc712f93`](https://github.com/nodejs/node/commit/56fc712f93)] - **doc**: use serial comma in assert docs (Tobias Nießen) [#43154](https://github.com/nodejs/node/pull/43154)
* \[[`093a3cf2f1`](https://github.com/nodejs/node/commit/093a3cf2f1)] - **doc**: fix errors in Performance hooks doc (OneNail) [#43152](https://github.com/nodejs/node/pull/43152)
* \[[`4d9f43a8a7`](https://github.com/nodejs/node/commit/4d9f43a8a7)] - **doc**: use serial comma in dns docs (Tobias Nießen) [#43145](https://github.com/nodejs/node/pull/43145)
* \[[`4e9393e32f`](https://github.com/nodejs/node/commit/4e9393e32f)] - **doc**: use ASCII apostrophes consistently (Tobias Nießen) [#43114](https://github.com/nodejs/node/pull/43114)
* \[[`b3181851d7`](https://github.com/nodejs/node/commit/b3181851d7)] - **doc**: add strategic initiative for shadow realm (Chengzhong Wu) [#43037](https://github.com/nodejs/node/pull/43037)
* \[[`6f48dfb499`](https://github.com/nodejs/node/commit/6f48dfb499)] - **fs**: add trailing commas (LiviaMedeiros) [#43127](https://github.com/nodejs/node/pull/43127)
* \[[`3e89b7336d`](https://github.com/nodejs/node/commit/3e89b7336d)] - **(SEMVER-MINOR)** **fs**: make params in writing methods optional (LiviaMedeiros) [#42601](https://github.com/nodejs/node/pull/42601)
* \[[`9539cfa358`](https://github.com/nodejs/node/commit/9539cfa358)] - **(SEMVER-MINOR)** **http**: add uniqueHeaders option to request and createServer (Paolo Insogna) [#41397](https://github.com/nodejs/node/pull/41397)
* \[[`b187060f76`](https://github.com/nodejs/node/commit/b187060f76)] - **http2**: set origin name correctly when servername is empty (ofirbarak) [#42838](https://github.com/nodejs/node/pull/42838)
* \[[`a07f5f28f3`](https://github.com/nodejs/node/commit/a07f5f28f3)] - **http2**: improve tests and docs (Daeyeon Jeong) [#42858](https://github.com/nodejs/node/pull/42858)
* \[[`701d40496d`](https://github.com/nodejs/node/commit/701d40496d)] - **lib**: refactor `validateInt32` and `validateUint32` (mawaregetsuka) [#43071](https://github.com/nodejs/node/pull/43071)
* \[[`09da76493a`](https://github.com/nodejs/node/commit/09da76493a)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#43231](https://github.com/nodejs/node/pull/43231)
* \[[`1da6b7c32b`](https://github.com/nodejs/node/commit/1da6b7c32b)] - **meta**: add mailmap entry for legendecas (Chengzhong Wu) [#43156](https://github.com/nodejs/node/pull/43156)
* \[[`1be254b24e`](https://github.com/nodejs/node/commit/1be254b24e)] - **meta**: add mailmap entry for npm team (Luigi Pinca) [#43143](https://github.com/nodejs/node/pull/43143)
* \[[`7d8d9d625a`](https://github.com/nodejs/node/commit/7d8d9d625a)] - **meta**: add mailmap entry for Morgan Roderick (Luigi Pinca) [#43144](https://github.com/nodejs/node/pull/43144)
* \[[`8f5b4570e5`](https://github.com/nodejs/node/commit/8f5b4570e5)] - **(SEMVER-MINOR)** **net**: add ability to reset a tcp socket (pupilTong) [#43112](https://github.com/nodejs/node/pull/43112)
* \[[`11c783fa63`](https://github.com/nodejs/node/commit/11c783fa63)] - **net**: remoteAddress always undefined called before connected (OneNail) [#43011](https://github.com/nodejs/node/pull/43011)
* \[[`9c4e070567`](https://github.com/nodejs/node/commit/9c4e070567)] - **(SEMVER-MINOR)** **node-api**: emit uncaught-exception on unhandled tsfn callbacks (Chengzhong Wu) [#36510](https://github.com/nodejs/node/pull/36510)
* \[[`54d68b1afd`](https://github.com/nodejs/node/commit/54d68b1afd)] - **perf\_hooks**: fix start\_time of perf\_hooks (theanarkh) [#43069](https://github.com/nodejs/node/pull/43069)
* \[[`baa5d0005a`](https://github.com/nodejs/node/commit/baa5d0005a)] - **src**: refactor GetCipherValue and related functions (Tobias Nießen) [#43171](https://github.com/nodejs/node/pull/43171)
* \[[`6ee9fb14e4`](https://github.com/nodejs/node/commit/6ee9fb14e4)] - **src**: make SecureContext fields private (Tobias Nießen) [#43173](https://github.com/nodejs/node/pull/43173)
* \[[`1e2552ba6c`](https://github.com/nodejs/node/commit/1e2552ba6c)] - **src**: reuse GetServerName (Tobias Nießen) [#43168](https://github.com/nodejs/node/pull/43168)
* \[[`d8a14a2486`](https://github.com/nodejs/node/commit/d8a14a2486)] - **src**: fix static analysis warning and use smart ptr (Tobias Nießen) [#43117](https://github.com/nodejs/node/pull/43117)
* \[[`72a767b7ca`](https://github.com/nodejs/node/commit/72a767b7ca)] - **src**: remove SecureContext::operator\* (Tobias Nießen) [#43121](https://github.com/nodejs/node/pull/43121)
* \[[`82fb037388`](https://github.com/nodejs/node/commit/82fb037388)] - **src**: move context snapshot index to SnapshotData (Joyee Cheung) [#43023](https://github.com/nodejs/node/pull/43023)
* \[[`9577878258`](https://github.com/nodejs/node/commit/9577878258)] - **src**: replace TraceEventScope with sync events (Chengzhong Wu) [#42977](https://github.com/nodejs/node/pull/42977)
* \[[`41b69e3cf4`](https://github.com/nodejs/node/commit/41b69e3cf4)] - **src,lib**: migrate to console on context's extra binding (Chengzhong Wu) [#43142](https://github.com/nodejs/node/pull/43142)
* \[[`3e1ed1ee0c`](https://github.com/nodejs/node/commit/3e1ed1ee0c)] - **test**: improve code coverage for inspector connection errors (Kohei Ueno) [#42310](https://github.com/nodejs/node/pull/42310)
* \[[`d7ca2234dc`](https://github.com/nodejs/node/commit/d7ca2234dc)] - **test**: use mustSucceed instead of mustCall with assert.ifError (MURAKAMI Masahiko) [#43188](https://github.com/nodejs/node/pull/43188)
* \[[`9cc5ce6a24`](https://github.com/nodejs/node/commit/9cc5ce6a24)] - **test**: improve readline/emitKeypressEvents.js coverage (OneNail) [#42908](https://github.com/nodejs/node/pull/42908)
* \[[`447bbd0d66`](https://github.com/nodejs/node/commit/447bbd0d66)] - **tls**: fix convertALPNProtocols accepting ArrayBufferViews (LiviaMedeiros) [#43211](https://github.com/nodejs/node/pull/43211)
* \[[`df691464aa`](https://github.com/nodejs/node/commit/df691464aa)] - **tools**: update lint-md-dependencies to rollup\@2.75.1 (Node.js GitHub Bot) [#43230](https://github.com/nodejs/node/pull/43230)
* \[[`d9fb25c936`](https://github.com/nodejs/node/commit/d9fb25c936)] - **tools**: refactor build-addons.js to ESM (Feng Yu) [#43099](https://github.com/nodejs/node/pull/43099)
* \[[`0efeceb4a6`](https://github.com/nodejs/node/commit/0efeceb4a6)] - **tools**: update lint-md-dependencies to rollup\@2.74.1 (Node.js GitHub Bot) [#43172](https://github.com/nodejs/node/pull/43172)
* \[[`ed352a945c`](https://github.com/nodejs/node/commit/ed352a945c)] - **tools**: update eslint to 8.16.0 (Node.js GitHub Bot) [#43174](https://github.com/nodejs/node/pull/43174)
* \[[`1183058908`](https://github.com/nodejs/node/commit/1183058908)] - **tools**: refactor update-authors.js to ESM (Feng Yu) [#43098](https://github.com/nodejs/node/pull/43098)
* \[[`5228028962`](https://github.com/nodejs/node/commit/5228028962)] - **(SEMVER-MINOR)** **tools**: update V8 gypfiles for 10.2 (Michaël Zasso) [#42740](https://github.com/nodejs/node/pull/42740)
* \[[`d6cf409d78`](https://github.com/nodejs/node/commit/d6cf409d78)] - **(SEMVER-MINOR)** **util**: add parseArgs module (Benjamin Coe) [#42675](https://github.com/nodejs/node/pull/42675)
* \[[`d91b489784`](https://github.com/nodejs/node/commit/d91b489784)] - **worker**: fix heap snapshot crash on exit (Chengzhong Wu) [#43123](https://github.com/nodejs/node/pull/43123)

<a id="18.2.0"></a>

## 2022-05-17, Version 18.2.0 (Current), @BethGriggs prepared by @RafaelGSS

### Notable Changes

#### OpenSSL 3.0.3

This update can be treated as a security release as the issues addressed in OpenSSL 3.0.3 slightly affect Node.js 18.
See <https://nodejs.org/en/blog/vulnerability/openssl-fixes-in-regular-releases-may2022/> for more information on how the May 2022 OpenSSL releases affect other Node.js release lines.

* \[[`8e54c19a6e`](https://github.com/nodejs/node/commit/8e54c19a6e)] - **deps**: update archs files for quictls/openssl-3.0.3+quic (RafaelGSS) [#43022](https://github.com/nodejs/node/pull/43022)
* \[[`6365bf808e`](https://github.com/nodejs/node/commit/6365bf808e)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.3 (RafaelGSS) [#43022](https://github.com/nodejs/node/pull/43022)

#### Other Notable Changes

* \[[`13c333e533`](https://github.com/nodejs/node/commit/13c333e533)] - _**Revert**_ "**deps**: add template for generated headers" (Daniel Bevenius) [#42978](https://github.com/nodejs/node/pull/42978)
* \[[`d128356a7f`](https://github.com/nodejs/node/commit/d128356a7f)] - **deps**: update undici to 5.2.0 (Node.js GitHub Bot) [#43059](https://github.com/nodejs/node/pull/43059)
* \[[`2df1624f80`](https://github.com/nodejs/node/commit/2df1624f80)] - **deps**: upgrade npm to 8.9.0 (npm team) [#42968](https://github.com/nodejs/node/pull/42968)
* \[[`6365bf808e`](https://github.com/nodejs/node/commit/6365bf808e)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.3 (RafaelGSS) [#43022](https://github.com/nodejs/node/pull/43022)
* \[[`4a3f678e70`](https://github.com/nodejs/node/commit/4a3f678e70)] - **doc**: add LiviaMedeiros to collaborators (LiviaMedeiros) [#43039](https://github.com/nodejs/node/pull/43039)
* \[[`686c4c1f6f`](https://github.com/nodejs/node/commit/686c4c1f6f)] - **doc**: add release key for Juan Arboleda (Juan José) [#42961](https://github.com/nodejs/node/pull/42961)
* \[[`784d84cf34`](https://github.com/nodejs/node/commit/784d84cf34)] - **(SEMVER-MINOR)** **fs**: add `read(buffer[, options])` versions (LiviaMedeiros) [#42768](https://github.com/nodejs/node/pull/42768)
* \[[`2f192c4be0`](https://github.com/nodejs/node/commit/2f192c4be0)] - **(SEMVER-MINOR)** **http**: added connection closing methods (Paolo Insogna) [#42812](https://github.com/nodejs/node/pull/42812)
* \[[`c92e291beb`](https://github.com/nodejs/node/commit/c92e291beb)] - **(SEMVER-MINOR)** **perf\_hooks**: add PerformanceResourceTiming (RafaelGSS) [#42725](https://github.com/nodejs/node/pull/42725)

### Commits

* \[[`7cac7bb806`](https://github.com/nodejs/node/commit/7cac7bb806)] - **assert**: fix CallTracker wraps the function causes the length to be lost (OneNail) [#42909](https://github.com/nodejs/node/pull/42909)
* \[[`e74a8da287`](https://github.com/nodejs/node/commit/e74a8da287)] - **assert**: make `assert.fail` less affected by prototype tampering (Antoine du Hamel) [#42918](https://github.com/nodejs/node/pull/42918)
* \[[`1146806673`](https://github.com/nodejs/node/commit/1146806673)] - **bootstrap**: stop delaying instantiation of maps in per-context scripts (Darshan Sen) [#42934](https://github.com/nodejs/node/pull/42934)
* \[[`a20310d171`](https://github.com/nodejs/node/commit/a20310d171)] - **bootstrap**: use a context snapshotted with primordials in workers (Joyee Cheung) [#42867](https://github.com/nodejs/node/pull/42867)
* \[[`9ee7d9eb15`](https://github.com/nodejs/node/commit/9ee7d9eb15)] - **bootstrap**: fix wasm\_web\_api external reference registration (Joyee Cheung) [#42903](https://github.com/nodejs/node/pull/42903)
* \[[`cec678a00e`](https://github.com/nodejs/node/commit/cec678a00e)] - **build**: set ASAN workaround (Richard Lau) [#43085](https://github.com/nodejs/node/pull/43085)
* \[[`7c4df42caa`](https://github.com/nodejs/node/commit/7c4df42caa)] - **build**: disable windows-2022 temporarily (Jiawen Geng) [#43093](https://github.com/nodejs/node/pull/43093)
* \[[`0eb32ed976`](https://github.com/nodejs/node/commit/0eb32ed976)] - **build**: fix various shared library build issues (William Marlow) [#41850](https://github.com/nodejs/node/pull/41850)
* \[[`48f4a714b2`](https://github.com/nodejs/node/commit/48f4a714b2)] - **build**: fix indeterminacy of icu\_locales value (Sergey Nazaryev) [#42865](https://github.com/nodejs/node/pull/42865)
* \[[`19c060fd84`](https://github.com/nodejs/node/commit/19c060fd84)] - **crypto**: adjust minimum length in generateKey('hmac', ...) (LiviaMedeiros) [#42944](https://github.com/nodejs/node/pull/42944)
* \[[`183bcc0699`](https://github.com/nodejs/node/commit/183bcc0699)] - **crypto**: clean up parameter validation in HKDF (Tobias Nießen) [#42924](https://github.com/nodejs/node/pull/42924)
* \[[`946f57c7bc`](https://github.com/nodejs/node/commit/946f57c7bc)] - **debugger**: fix inconsistent inspector output of exec new Map() (Kohei Ueno) [#42423](https://github.com/nodejs/node/pull/42423)
* \[[`d128356a7f`](https://github.com/nodejs/node/commit/d128356a7f)] - **deps**: update undici to 5.2.0 (Node.js GitHub Bot) [#43059](https://github.com/nodejs/node/pull/43059)
* \[[`a9703a55ef`](https://github.com/nodejs/node/commit/a9703a55ef)] - **deps**: remove opensslconf template headers (Daniel Bevenius) [#43035](https://github.com/nodejs/node/pull/43035)
* \[[`a4a4f7134b`](https://github.com/nodejs/node/commit/a4a4f7134b)] - **deps**: fix llhttp version number (Michael Dawson) [#43029](https://github.com/nodejs/node/pull/43029)
* \[[`8e54c19a6e`](https://github.com/nodejs/node/commit/8e54c19a6e)] - **deps**: update archs files for quictls/openssl-3.0.3+quic (RafaelGSS) [#43022](https://github.com/nodejs/node/pull/43022)
* \[[`6365bf808e`](https://github.com/nodejs/node/commit/6365bf808e)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.3 (RafaelGSS) [#43022](https://github.com/nodejs/node/pull/43022)
* \[[`e8121ae7fe`](https://github.com/nodejs/node/commit/e8121ae7fe)] - **deps**: regenerate OpenSSL archs files (Daniel Bevenius) [#42978](https://github.com/nodejs/node/pull/42978)
* \[[`13c333e533`](https://github.com/nodejs/node/commit/13c333e533)] - _**Revert**_ "**deps**: add template for generated headers" (Daniel Bevenius) [#42978](https://github.com/nodejs/node/pull/42978)
* \[[`2df1624f80`](https://github.com/nodejs/node/commit/2df1624f80)] - **deps**: upgrade npm to 8.9.0 (npm team) [#42968](https://github.com/nodejs/node/pull/42968)
* \[[`f53ed9d1bb`](https://github.com/nodejs/node/commit/f53ed9d1bb)] - **doc**: use serial comma in fs docs (Tobias Nießen) [#43104](https://github.com/nodejs/node/pull/43104)
* \[[`839824aca8`](https://github.com/nodejs/node/commit/839824aca8)] - **doc**: use serial comma in events docs (Tobias Nießen) [#43113](https://github.com/nodejs/node/pull/43113)
* \[[`9629c74080`](https://github.com/nodejs/node/commit/9629c74080)] - **doc**: use serial comma in modules docs (Tobias Nießen) [#43103](https://github.com/nodejs/node/pull/43103)
* \[[`76096c2d4a`](https://github.com/nodejs/node/commit/76096c2d4a)] - **doc**: use serial comma in util docs (Tobias Nießen) [#43063](https://github.com/nodejs/node/pull/43063)
* \[[`1e9de0dd5a`](https://github.com/nodejs/node/commit/1e9de0dd5a)] - **doc**: remove git:// protocol, adjust nits in onboarding.md (LiviaMedeiros) [#43045](https://github.com/nodejs/node/pull/43045)
* \[[`eb630d7ef9`](https://github.com/nodejs/node/commit/eb630d7ef9)] - **doc**: add maintaining info for shared libary option (Michael Dawson) [#42517](https://github.com/nodejs/node/pull/42517)
* \[[`3816a97bae`](https://github.com/nodejs/node/commit/3816a97bae)] - **doc**: add detail for how to update llhttp (Michael Dawson) [#43028](https://github.com/nodejs/node/pull/43028)
* \[[`330e267a57`](https://github.com/nodejs/node/commit/330e267a57)] - **doc**: use serial comma in buffer docs (Tobias Nießen) [#43048](https://github.com/nodejs/node/pull/43048)
* \[[`0957212390`](https://github.com/nodejs/node/commit/0957212390)] - **doc**: use consistent method symbol (Paolo Insogna) [#42974](https://github.com/nodejs/node/pull/42974)
* \[[`22cb7104cb`](https://github.com/nodejs/node/commit/22cb7104cb)] - **doc**: add Rafael to the security steward for NearForm (Matteo Collina) [#42966](https://github.com/nodejs/node/pull/42966)
* \[[`ef177da3f1`](https://github.com/nodejs/node/commit/ef177da3f1)] - **doc**: mark some node-api functions as experimental (NickNaso) [#42987](https://github.com/nodejs/node/pull/42987)
* \[[`4a3f678e70`](https://github.com/nodejs/node/commit/4a3f678e70)] - **doc**: add LiviaMedeiros to collaborators (LiviaMedeiros) [#43039](https://github.com/nodejs/node/pull/43039)
* \[[`c988a0ed26`](https://github.com/nodejs/node/commit/c988a0ed26)] - **doc**: use serial comma in http docs (Tobias Nießen) [#43026](https://github.com/nodejs/node/pull/43026)
* \[[`4de918b4c1`](https://github.com/nodejs/node/commit/4de918b4c1)] - **doc**: add the preferred name for @himself65 (Himself65) [#43024](https://github.com/nodejs/node/pull/43024)
* \[[`686c4c1f6f`](https://github.com/nodejs/node/commit/686c4c1f6f)] - **doc**: add release key for Juan Arboleda (Juan José) [#42961](https://github.com/nodejs/node/pull/42961)
* \[[`64e0aa116d`](https://github.com/nodejs/node/commit/64e0aa116d)] - **doc**: rename N-API to Node-API in test/README.md (Daeyeon Jeong) [#42946](https://github.com/nodejs/node/pull/42946)
* \[[`65d64553c0`](https://github.com/nodejs/node/commit/65d64553c0)] - **doc**: use serial comma in tls docs (Tobias Nießen) [#43001](https://github.com/nodejs/node/pull/43001)
* \[[`840e61e745`](https://github.com/nodejs/node/commit/840e61e745)] - **doc**: improve commit message example for releases (Juan José) [#42954](https://github.com/nodejs/node/pull/42954)
* \[[`ba3ad7c665`](https://github.com/nodejs/node/commit/ba3ad7c665)] - **doc**: use serial comma in cluster docs (Tobias Nießen) [#42989](https://github.com/nodejs/node/pull/42989)
* \[[`3ab3086008`](https://github.com/nodejs/node/commit/3ab3086008)] - **doc**: fix errors in Web Streams doc (OneNail) [#42862](https://github.com/nodejs/node/pull/42862)
* \[[`1fbfee2497`](https://github.com/nodejs/node/commit/1fbfee2497)] - **doc**: fix examples in cluster.md (OneNail) [#42889](https://github.com/nodejs/node/pull/42889)
* \[[`1237c742f4`](https://github.com/nodejs/node/commit/1237c742f4)] - **doc**: add additional step to security release process (Michael Dawson) [#42916](https://github.com/nodejs/node/pull/42916)
* \[[`88692d8fd6`](https://github.com/nodejs/node/commit/88692d8fd6)] - **doc**: add section regarding property definition in `primordials.md` (Antoine du Hamel) [#42921](https://github.com/nodejs/node/pull/42921)
* \[[`924670f3af`](https://github.com/nodejs/node/commit/924670f3af)] - **doc**: clarify some default values in `fs.md` (LiviaMedeiros) [#42892](https://github.com/nodejs/node/pull/42892)
* \[[`becca06f9b`](https://github.com/nodejs/node/commit/becca06f9b)] - **fs**: remove unnecessary ?? operator (Morgan Roderick) [#43073](https://github.com/nodejs/node/pull/43073)
* \[[`784d84cf34`](https://github.com/nodejs/node/commit/784d84cf34)] - **(SEMVER-MINOR)** **fs**: add `read(buffer[, options])` versions (LiviaMedeiros) [#42768](https://github.com/nodejs/node/pull/42768)
* \[[`2f192c4be0`](https://github.com/nodejs/node/commit/2f192c4be0)] - **(SEMVER-MINOR)** **http**: added connection closing methods (Paolo Insogna) [#42812](https://github.com/nodejs/node/pull/42812)
* \[[`bfbf965eb0`](https://github.com/nodejs/node/commit/bfbf965eb0)] - **http2**: compat support for array headers (OneNail) [#42901](https://github.com/nodejs/node/pull/42901)
* \[[`46a44b3011`](https://github.com/nodejs/node/commit/46a44b3011)] - **lib**: move WebAssembly Web API into separate file (Tobias Nießen) [#42993](https://github.com/nodejs/node/pull/42993)
* \[[`c64b8d3282`](https://github.com/nodejs/node/commit/c64b8d3282)] - **lib,test**: enable wasm/webapi/empty-body WPT (Tobias Nießen) [#42960](https://github.com/nodejs/node/pull/42960)
* \[[`ddd271ec2b`](https://github.com/nodejs/node/commit/ddd271ec2b)] - **meta**: add mailmap entry for ShogunPanda (Paolo Insogna) [#43094](https://github.com/nodejs/node/pull/43094)
* \[[`174ff972f0`](https://github.com/nodejs/node/commit/174ff972f0)] - **meta**: update .mailmap for recent README name change (Rich Trott) [#43027](https://github.com/nodejs/node/pull/43027)
* \[[`16df8ad7c3`](https://github.com/nodejs/node/commit/16df8ad7c3)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#43004](https://github.com/nodejs/node/pull/43004)
* \[[`0ec32d0715`](https://github.com/nodejs/node/commit/0ec32d0715)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#42937](https://github.com/nodejs/node/pull/42937)
* \[[`037ff3da6d`](https://github.com/nodejs/node/commit/037ff3da6d)] - **node-api**: explicitly set \_\_cdecl for API functions (Vladimir Morozov) [#42780](https://github.com/nodejs/node/pull/42780)
* \[[`e2462a2f98`](https://github.com/nodejs/node/commit/e2462a2f98)] - **node-api**: fix napi\_get\_all\_property\_names (Vladimir Morozov) [#42463](https://github.com/nodejs/node/pull/42463)
* \[[`c92e291beb`](https://github.com/nodejs/node/commit/c92e291beb)] - **(SEMVER-MINOR)** **perf\_hooks**: add PerformanceResourceTiming (RafaelGSS) [#42725](https://github.com/nodejs/node/pull/42725)
* \[[`c535db1195`](https://github.com/nodejs/node/commit/c535db1195)] - **src**: delete AllocatedBuffer (Darshan Sen) [#43008](https://github.com/nodejs/node/pull/43008)
* \[[`5dc79298e1`](https://github.com/nodejs/node/commit/5dc79298e1)] - **src**: remove unnecessary comment (Kohei Ueno) [#42952](https://github.com/nodejs/node/pull/42952)
* \[[`38e4c15534`](https://github.com/nodejs/node/commit/38e4c15534)] - **src**: always signal V8 for intercepted properties (Michaël Zasso) [#42963](https://github.com/nodejs/node/pull/42963)
* \[[`cacff07e75`](https://github.com/nodejs/node/commit/cacff07e75)] - **src**: fix memory leak for v8.serialize (liuxingbaoyu) [#42695](https://github.com/nodejs/node/pull/42695)
* \[[`8cfc18e4db`](https://github.com/nodejs/node/commit/8cfc18e4db)] - **src,crypto**: remove uses of AllocatedBuffer from crypto\_rsa.cc (Darshan Sen) [#42852](https://github.com/nodejs/node/pull/42852)
* \[[`0670843b24`](https://github.com/nodejs/node/commit/0670843b24)] - **test**: fix dangerous .map in `test/parallel/test-http-set-trailers.js` (LiviaMedeiros) [#43087](https://github.com/nodejs/node/pull/43087)
* \[[`9eb8bf1d26`](https://github.com/nodejs/node/commit/9eb8bf1d26)] - **test**: reduce flakiness of `test-fs-read-position-validation.mjs` (LiviaMedeiros) [#42999](https://github.com/nodejs/node/pull/42999)
* \[[`41d2f6e8c5`](https://github.com/nodejs/node/commit/41d2f6e8c5)] - **test**: rename handlewrap.hasref tests (Daeyeon Jeong) [#42754](https://github.com/nodejs/node/pull/42754)
* \[[`e058f47277`](https://github.com/nodejs/node/commit/e058f47277)] - **test**: improve observable ICU behaviour coverage (LiviaMedeiros) [#42683](https://github.com/nodejs/node/pull/42683)
* \[[`d23debb4cb`](https://github.com/nodejs/node/commit/d23debb4cb)] - **test**: validate webstream encoder/decoder inspector (Yoshiki Kurihara) [#42747](https://github.com/nodejs/node/pull/42747)
* \[[`b1c18edaa9`](https://github.com/nodejs/node/commit/b1c18edaa9)] - **test**: use`mustSucceed` instead of `mustCall` with `assert.ifError` (MURAKAMI Masahiko) [#42806](https://github.com/nodejs/node/pull/42806)
* \[[`2dc795687a`](https://github.com/nodejs/node/commit/2dc795687a)] - **test**: improve `lib/internal/webstreams/readablestream.js` coverage (MURAKAMI Masahiko) [#42823](https://github.com/nodejs/node/pull/42823)
* \[[`d746207dc2`](https://github.com/nodejs/node/commit/d746207dc2)] - **test**: fix test-crypto-fips.js under shared OpenSSL (Vita Batrla) [#42947](https://github.com/nodejs/node/pull/42947)
* \[[`56c47b5101`](https://github.com/nodejs/node/commit/56c47b5101)] - **test**: use consistent timeouts (Paolo Insogna) [#42893](https://github.com/nodejs/node/pull/42893)
* \[[`68ed3c88d9`](https://github.com/nodejs/node/commit/68ed3c88d9)] - **test**: add test for position validation in fs.read() and fs.readSync() (LiviaMedeiros) [#42837](https://github.com/nodejs/node/pull/42837)
* \[[`72b90fd5f5`](https://github.com/nodejs/node/commit/72b90fd5f5)] - **test**: reduce impact of flaky HTTP server tests (Tobias Nießen) [#42926](https://github.com/nodejs/node/pull/42926)
* \[[`531a0a9980`](https://github.com/nodejs/node/commit/531a0a9980)] - **tools**: update lint-md-dependencies to rollup\@2.73.0 (Node.js GitHub Bot) [#43107](https://github.com/nodejs/node/pull/43107)
* \[[`64daaca46d`](https://github.com/nodejs/node/commit/64daaca46d)] - **tools**: update eslint to 8.15.0 (Node.js GitHub Bot) [#43005](https://github.com/nodejs/node/pull/43005)
* \[[`79872382ef`](https://github.com/nodejs/node/commit/79872382ef)] - **tools**: refactor lint-sh.js to esm module (Feng Yu) [#42942](https://github.com/nodejs/node/pull/42942)
* \[[`265ecdfe07`](https://github.com/nodejs/node/commit/265ecdfe07)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#43003](https://github.com/nodejs/node/pull/43003)
* \[[`e9e1f1e194`](https://github.com/nodejs/node/commit/e9e1f1e194)] - **typings**: fix `os.cpus` invalid return type (Himself65) [#43006](https://github.com/nodejs/node/pull/43006)
* \[[`55ef6e81cb`](https://github.com/nodejs/node/commit/55ef6e81cb)] - **wasm**: add missing init reported by coverity (Michael Dawson) [#42897](https://github.com/nodejs/node/pull/42897)
* \[[`5470578008`](https://github.com/nodejs/node/commit/5470578008)] - **worker**: fix stream racing with terminate (Keyhan Vakil) [#42874](https://github.com/nodejs/node/pull/42874)

<a id="18.1.0"></a>

## 2022-05-03, Version 18.1.0 (Current), @targos

### Notable Changes

* \[[`c46e7bbf69`](https://github.com/nodejs/node/commit/c46e7bbf69)] - **doc**: add @kuriyosh to collaborators (Yoshiki Kurihara) [#42824](https://github.com/nodejs/node/pull/42824)
* \[[`b0f7c4c8f9`](https://github.com/nodejs/node/commit/b0f7c4c8f9)] - **(SEMVER-MINOR)** **lib,src**: implement WebAssembly Web API (Tobias Nießen) [#42701](https://github.com/nodejs/node/pull/42701)
* \[[`78a860ae58`](https://github.com/nodejs/node/commit/78a860ae58)] - **(SEMVER-MINOR)** **test\_runner**: add initial CLI runner (Colin Ihrig) [#42658](https://github.com/nodejs/node/pull/42658)
* \[[`bf9240ae8c`](https://github.com/nodejs/node/commit/bf9240ae8c)] - **(SEMVER-MINOR)** **worker**: add hasRef() to MessagePort (Darshan Sen) [#42849](https://github.com/nodejs/node/pull/42849)

### Commits

* \[[`4694f5bb96`](https://github.com/nodejs/node/commit/4694f5bb96)] - **async\_hooks**: avoid decrementing iterator after erase (Gabriel Bota) [#42749](https://github.com/nodejs/node/pull/42749)
* \[[`459546b4f0`](https://github.com/nodejs/node/commit/459546b4f0)] - **benchmark**: fix misc/startup failure (Antoine du Hamel) [#42746](https://github.com/nodejs/node/pull/42746)
* \[[`6bd24204ea`](https://github.com/nodejs/node/commit/6bd24204ea)] - **bootstrap**: use the isolate snapshot in workers (Joyee Cheung) [#42702](https://github.com/nodejs/node/pull/42702)
* \[[`29c8411f99`](https://github.com/nodejs/node/commit/29c8411f99)] - **bootstrap**: move embedded snapshot to SnapshotBuilder (Joyee Cheung) [#42702](https://github.com/nodejs/node/pull/42702)
* \[[`4050b0d64f`](https://github.com/nodejs/node/commit/4050b0d64f)] - **build**: enable V8's shared read-only heap (Michaël Zasso) [#42809](https://github.com/nodejs/node/pull/42809)
* \[[`f9994e2029`](https://github.com/nodejs/node/commit/f9994e2029)] - **build**: improve reliability of find\_python.cmd script (Luigi Pinca) [#42810](https://github.com/nodejs/node/pull/42810)
* \[[`5d15eb1a14`](https://github.com/nodejs/node/commit/5d15eb1a14)] - **build**: fix format-cpp (Darshan Sen) [#42764](https://github.com/nodejs/node/pull/42764)
* \[[`7c973474bf`](https://github.com/nodejs/node/commit/7c973474bf)] - **build**: improve the format-cpp error message (Darshan Sen) [#42765](https://github.com/nodejs/node/pull/42765)
* \[[`7681e60829`](https://github.com/nodejs/node/commit/7681e60829)] - **crypto**: validate `this` in all webcrypto methods and getters (Filip Skokan) [#42815](https://github.com/nodejs/node/pull/42815)
* \[[`2a4c8263c3`](https://github.com/nodejs/node/commit/2a4c8263c3)] - **deps**: update undici to 5.1.1 (Michaël Zasso) [#42939](https://github.com/nodejs/node/pull/42939)
* \[[`1102922ef9`](https://github.com/nodejs/node/commit/1102922ef9)] - **deps**: upgrade npm to 8.8.0 (npm team) [#42886](https://github.com/nodejs/node/pull/42886)
* \[[`279892987b`](https://github.com/nodejs/node/commit/279892987b)] - **deps**: remove linux-ppc64 architecture (Daniel Bevenius) [#42616](https://github.com/nodejs/node/pull/42616)
* \[[`4abe9879ae`](https://github.com/nodejs/node/commit/4abe9879ae)] - **deps**: remove linux-ppc architecture (Daniel Bevenius) [#42616](https://github.com/nodejs/node/pull/42616)
* \[[`8dc71f2266`](https://github.com/nodejs/node/commit/8dc71f2266)] - **deps**: remove aix-gcc architecture (Daniel Bevenius) [#42616](https://github.com/nodejs/node/pull/42616)
* \[[`6dc1f82384`](https://github.com/nodejs/node/commit/6dc1f82384)] - **deps**: remove archs/aix64-gcc (Daniel Bevenius) [#42616](https://github.com/nodejs/node/pull/42616)
* \[[`e8734a4771`](https://github.com/nodejs/node/commit/e8734a4771)] - **deps**: add note about removing asm archs (Daniel Bevenius) [#42616](https://github.com/nodejs/node/pull/42616)
* \[[`7fae2c9d6e`](https://github.com/nodejs/node/commit/7fae2c9d6e)] - **deps**: add template for generated headers (Daniel Bevenius) [#42616](https://github.com/nodejs/node/pull/42616)
* \[[`294664e32c`](https://github.com/nodejs/node/commit/294664e32c)] - **deps**: upgrade npm to 8.7.0 (npm team) [#42744](https://github.com/nodejs/node/pull/42744)
* \[[`60e461c45d`](https://github.com/nodejs/node/commit/60e461c45d)] - **doc**: reword "test directory" (LiviaMedeiros) [#42817](https://github.com/nodejs/node/pull/42817)
* \[[`227a45ba60`](https://github.com/nodejs/node/commit/227a45ba60)] - **doc**: remove legacy `-J` test.py option from BUILDING.md (LiviaMedeiros) [#42817](https://github.com/nodejs/node/pull/42817)
* \[[`e313dc6ed9`](https://github.com/nodejs/node/commit/e313dc6ed9)] - **doc**: http2.createServer `options` as optional (Daeyeon Jeong) [#42832](https://github.com/nodejs/node/pull/42832)
* \[[`8f2b2280cd`](https://github.com/nodejs/node/commit/8f2b2280cd)] - **doc**: record March 2022 security release steward (Richard Lau) [#42876](https://github.com/nodejs/node/pull/42876)
* \[[`e15d22c024`](https://github.com/nodejs/node/commit/e15d22c024)] - **doc**: initial version of security-model-strategy.md (Michael Dawson) [#42709](https://github.com/nodejs/node/pull/42709)
* \[[`fe65996790`](https://github.com/nodejs/node/commit/fe65996790)] - **doc**: clarify guide on testing internal errors (Livia Medeiros) [#42813](https://github.com/nodejs/node/pull/42813)
* \[[`2f849a460f`](https://github.com/nodejs/node/commit/2f849a460f)] - **doc**: fix markdown formatting in primordials.md (Tobias Nießen) [#42877](https://github.com/nodejs/node/pull/42877)
* \[[`cd2f5a4fd4`](https://github.com/nodejs/node/commit/cd2f5a4fd4)] - **doc**: add primordials guidelines (Antoine du Hamel) [#38635](https://github.com/nodejs/node/pull/38635)
* \[[`2d76f72665`](https://github.com/nodejs/node/commit/2d76f72665)] - **doc**: elevate node-clinic diagnostic tier (RafaelGSS) [#42802](https://github.com/nodejs/node/pull/42802)
* \[[`9b61ac2617`](https://github.com/nodejs/node/commit/9b61ac2617)] - **doc**: update WebAssembly strategy with Wasm Web API (Tobias Nießen) [#42836](https://github.com/nodejs/node/pull/42836)
* \[[`c6c1dc5833`](https://github.com/nodejs/node/commit/c6c1dc5833)] - **doc**: order `vm.Module` linker arguments correctly (Simen Bekkhus) [#42797](https://github.com/nodejs/node/pull/42797)
* \[[`c46e7bbf69`](https://github.com/nodejs/node/commit/c46e7bbf69)] - **doc**: add @kuriyosh to collaborators (Yoshiki Kurihara) [#42824](https://github.com/nodejs/node/pull/42824)
* \[[`59da1339b4`](https://github.com/nodejs/node/commit/59da1339b4)] - **doc**: add maintaining-webassembly.md (Michael Dawson) [#42660](https://github.com/nodejs/node/pull/42660)
* \[[`d9f3f05cab`](https://github.com/nodejs/node/commit/d9f3f05cab)] - **doc**: fix outdated documentation for `family` property (Antoine du Hamel) [#42789](https://github.com/nodejs/node/pull/42789)
* \[[`6fa080cb48`](https://github.com/nodejs/node/commit/6fa080cb48)] - **doc**: delete heapdump from diagnostic tooling support tiers (Tony Gorez) [#42783](https://github.com/nodejs/node/pull/42783)
* \[[`c32f76d49e`](https://github.com/nodejs/node/commit/c32f76d49e)] - **doc**: fix example in assert.md (Livia Medeiros) [#42786](https://github.com/nodejs/node/pull/42786)
* \[[`6225370b2e`](https://github.com/nodejs/node/commit/6225370b2e)] - **doc**: fix version history for Loaders API (Antoine du Hamel) [#42778](https://github.com/nodejs/node/pull/42778)
* \[[`3d65a3b13e`](https://github.com/nodejs/node/commit/3d65a3b13e)] - **doc**: add `node:` prefix for all core modules (Antoine du Hamel) [#42752](https://github.com/nodejs/node/pull/42752)
* \[[`46c880b99b`](https://github.com/nodejs/node/commit/46c880b99b)] - **doc**: clarify core modules that can be loaded without a prefix (Antoine du Hamel) [#42753](https://github.com/nodejs/node/pull/42753)
* \[[`025b3e786a`](https://github.com/nodejs/node/commit/025b3e786a)] - **doc**: consolidate use of multiple-byte units (Antoine du Hamel) [#42587](https://github.com/nodejs/node/pull/42587)
* \[[`962d80b7a1`](https://github.com/nodejs/node/commit/962d80b7a1)] - **doc**: add documentation for inherited methods (Luigi Pinca) [#42691](https://github.com/nodejs/node/pull/42691)
* \[[`222b3e6674`](https://github.com/nodejs/node/commit/222b3e6674)] - **doc**: close tag in n-api.md (Livia Medeiros) [#42751](https://github.com/nodejs/node/pull/42751)
* \[[`4c30936065`](https://github.com/nodejs/node/commit/4c30936065)] - **doc**: copyedit http.OutgoingMessage documentation (Luigi Pinca) [#42733](https://github.com/nodejs/node/pull/42733)
* \[[`d77c59d0f2`](https://github.com/nodejs/node/commit/d77c59d0f2)] - **doc**: improve fragment (`:target`) anchors behavior on HTML version (Antoine du Hamel) [#42739](https://github.com/nodejs/node/pull/42739)
* \[[`c50309cb39`](https://github.com/nodejs/node/commit/c50309cb39)] - **doc**: fix `added:` info for `outgoingMessage.writable*` (Luigi Pinca) [#42737](https://github.com/nodejs/node/pull/42737)
* \[[`6b7c35e807`](https://github.com/nodejs/node/commit/6b7c35e807)] - **doc**: delete mdb\_v8 from diagnostic tooling support tiers (Tony Gorez) [#42626](https://github.com/nodejs/node/pull/42626)
* \[[`2a07a9fc3a`](https://github.com/nodejs/node/commit/2a07a9fc3a)] - **doc**: document the 'close' and 'finish' events (Luigi Pinca) [#42704](https://github.com/nodejs/node/pull/42704)
* \[[`ef5ab8179b`](https://github.com/nodejs/node/commit/ef5ab8179b)] - **doc**: fix `added:` info for `outgoingMessage.{,un}cork()` (Luigi Pinca) [#42711](https://github.com/nodejs/node/pull/42711)
* \[[`a6e1e7a5d7`](https://github.com/nodejs/node/commit/a6e1e7a5d7)] - **doc,test**: add tests and docs for duplex.fromWeb and duplex.toWeb (Erick Wendel) [#42738](https://github.com/nodejs/node/pull/42738)
* \[[`336242a7c6`](https://github.com/nodejs/node/commit/336242a7c6)] - **errors,console**: refactor to use ES2021 syntax (小菜) [#42872](https://github.com/nodejs/node/pull/42872)
* \[[`0e16120d0d`](https://github.com/nodejs/node/commit/0e16120d0d)] - **errors,vm**: update error and use cause (Gus Caplan) [#42820](https://github.com/nodejs/node/pull/42820)
* \[[`a0638a23b0`](https://github.com/nodejs/node/commit/a0638a23b0)] - **esm**: fix imports from non-file module (Antoine du Hamel) [#42881](https://github.com/nodejs/node/pull/42881)
* \[[`dab15f69e3`](https://github.com/nodejs/node/commit/dab15f69e3)] - **esm**: graduate top-level-await to stable (Antoine du Hamel) [#42875](https://github.com/nodejs/node/pull/42875)
* \[[`48bbb73f36`](https://github.com/nodejs/node/commit/48bbb73f36)] - **fs**: fix mkdirSync so ENOSPC is correctly reported (Santiago Gimeno) [#42811](https://github.com/nodejs/node/pull/42811)
* \[[`d33cbabd79`](https://github.com/nodejs/node/commit/d33cbabd79)] - **lib**: remove experimental warning from FormData (Xuguang Mei) [#42807](https://github.com/nodejs/node/pull/42807)
* \[[`ad8269450a`](https://github.com/nodejs/node/commit/ad8269450a)] - **lib,src**: use Response URL as WebAssembly location (Tobias Nießen) [#42842](https://github.com/nodejs/node/pull/42842)
* \[[`b0f7c4c8f9`](https://github.com/nodejs/node/commit/b0f7c4c8f9)] - **(SEMVER-MINOR)** **lib,src**: implement WebAssembly Web API (Tobias Nießen) [#42701](https://github.com/nodejs/node/pull/42701)
* \[[`fdc65032a7`](https://github.com/nodejs/node/commit/fdc65032a7)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#42848](https://github.com/nodejs/node/pull/42848)
* \[[`33ac027fdf`](https://github.com/nodejs/node/commit/33ac027fdf)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#42769](https://github.com/nodejs/node/pull/42769)
* \[[`14893c5984`](https://github.com/nodejs/node/commit/14893c5984)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#42760](https://github.com/nodejs/node/pull/42760)
* \[[`2f38b4812c`](https://github.com/nodejs/node/commit/2f38b4812c)] - **meta**: move mmarchini to emeritus (mary marchini) [#42750](https://github.com/nodejs/node/pull/42750)
* \[[`718d11fdb0`](https://github.com/nodejs/node/commit/718d11fdb0)] - **perf\_hooks**: return different functions in timerify (Himself65) [#42854](https://github.com/nodejs/node/pull/42854)
* \[[`e8083664e1`](https://github.com/nodejs/node/commit/e8083664e1)] - **src**: turn SSL\_CTX\_new CHECK/segfault into JS exception (Anna Henningsen) [#42799](https://github.com/nodejs/node/pull/42799)
* \[[`37ca1102c4`](https://github.com/nodejs/node/commit/37ca1102c4)] - **src**: make --no-node-snapshot a per-process option (Joyee Cheung) [#42864](https://github.com/nodejs/node/pull/42864)
* \[[`1976284a92`](https://github.com/nodejs/node/commit/1976284a92)] - **src**: define fs.constants.S\_IWUSR & S\_IRUSR for Win (Liviu Ionescu) [#42757](https://github.com/nodejs/node/pull/42757)
* \[[`b7e9dd0278`](https://github.com/nodejs/node/commit/b7e9dd0278)] - **src**: use `node:` prefix in example (Antoine du Hamel) [#42794](https://github.com/nodejs/node/pull/42794)
* \[[`1a7af6364d`](https://github.com/nodejs/node/commit/1a7af6364d)] - **src**: large page attributing an id on Linux (David CARLIER) [#42644](https://github.com/nodejs/node/pull/42644)
* \[[`494650c09f`](https://github.com/nodejs/node/commit/494650c09f)] - **src,crypto**: remove uses of AllocatedBuffer from crypto\_ec.cc (Darshan Sen) [#42766](https://github.com/nodejs/node/pull/42766)
* \[[`8e0e576669`](https://github.com/nodejs/node/commit/8e0e576669)] - **test**: add tests for extracting function name (Kohei Ueno) [#42399](https://github.com/nodejs/node/pull/42399)
* \[[`fbda87d966`](https://github.com/nodejs/node/commit/fbda87d966)] - **test**: simplify test-gc-{http-client,net}-\* (Luigi Pinca) [#42782](https://github.com/nodejs/node/pull/42782)
* \[[`3c796f8328`](https://github.com/nodejs/node/commit/3c796f8328)] - **test**: fix `parallel/test-dgram-udp6-link-local-address` (Antoine du Hamel) [#42795](https://github.com/nodejs/node/pull/42795)
* \[[`b85a11c28e`](https://github.com/nodejs/node/commit/b85a11c28e)] - **test**: improve lib/internal/test\_runner/test.js coverage (MURAKAMI Masahiko) [#42745](https://github.com/nodejs/node/pull/42745)
* \[[`59c07a99fb`](https://github.com/nodejs/node/commit/59c07a99fb)] - **test**: check ecdsa psychic signature (Filip Skokan) [#42863](https://github.com/nodejs/node/pull/42863)
* \[[`0725064695`](https://github.com/nodejs/node/commit/0725064695)] - **test**: fix port in net-perf\_hooks (Livia Medeiros) [#42761](https://github.com/nodejs/node/pull/42761)
* \[[`7b701442de`](https://github.com/nodejs/node/commit/7b701442de)] - **test**: skip test that cannot pass under --node-builtin-modules-path (Geoffrey Booth) [#42834](https://github.com/nodejs/node/pull/42834)
* \[[`37364abc58`](https://github.com/nodejs/node/commit/37364abc58)] - **test**: fix flaky HTTP server tests (Tobias Nießen) [#42846](https://github.com/nodejs/node/pull/42846)
* \[[`8476ffb85a`](https://github.com/nodejs/node/commit/8476ffb85a)] - **test**: use `assert.match()` instead of `assert(regex.test())` (Antoine du Hamel) [#42803](https://github.com/nodejs/node/pull/42803)
* \[[`d311916f37`](https://github.com/nodejs/node/commit/d311916f37)] - **test**: fix calculations in test-worker-resource-limits (Joyee Cheung) [#42702](https://github.com/nodejs/node/pull/42702)
* \[[`deb3cf49c7`](https://github.com/nodejs/node/commit/deb3cf49c7)] - **test**: remove the legacy url parser function (Kohei Ueno) [#42656](https://github.com/nodejs/node/pull/42656)
* \[[`be44b1ffcb`](https://github.com/nodejs/node/commit/be44b1ffcb)] - **test**: improve test coverage of internal/blob (Yoshiki Kurihara) [#41513](https://github.com/nodejs/node/pull/41513)
* \[[`78a860ae58`](https://github.com/nodejs/node/commit/78a860ae58)] - **(SEMVER-MINOR)** **test\_runner**: add initial CLI runner (Colin Ihrig) [#42658](https://github.com/nodejs/node/pull/42658)
* \[[`1e7479d34c`](https://github.com/nodejs/node/commit/1e7479d34c)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#42932](https://github.com/nodejs/node/pull/42932)
* \[[`c3c5fe78dc`](https://github.com/nodejs/node/commit/c3c5fe78dc)] - **tools**: bump jsdoccomment from 0.22.1 to 0.29.0 (Rich Trott) [#42857](https://github.com/nodejs/node/pull/42857)
* \[[`97fc00a06e`](https://github.com/nodejs/node/commit/97fc00a06e)] - **tools**: update eslint to 8.14.0 (Node.js GitHub Bot) [#42845](https://github.com/nodejs/node/pull/42845)
* \[[`93fd77a16f`](https://github.com/nodejs/node/commit/93fd77a16f)] - **tools**: update doc to highlight.js\@11.5.1 (Node.js GitHub Bot) [#42758](https://github.com/nodejs/node/pull/42758)
* \[[`47c04813f7`](https://github.com/nodejs/node/commit/47c04813f7)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#42759](https://github.com/nodejs/node/pull/42759)
* \[[`18ae2c39d5`](https://github.com/nodejs/node/commit/18ae2c39d5)] - **tools**: lint osx shell scripts (Livia Medeiros) [#42712](https://github.com/nodejs/node/pull/42712)
* \[[`4af0fbd41e`](https://github.com/nodejs/node/commit/4af0fbd41e)] - **v8**: export cpu\_profiler\_metadata\_size in getHeapCodeStatistics (theanarkh) [#42818](https://github.com/nodejs/node/pull/42818)
* \[[`a19fb609d8`](https://github.com/nodejs/node/commit/a19fb609d8)] - **v8**: export more fields in getHeapStatistics (theanarkh) [#42784](https://github.com/nodejs/node/pull/42784)
* \[[`1b5856a2a9`](https://github.com/nodejs/node/commit/1b5856a2a9)] - **wasi**: remove unecessary null check (Michael Dawson) [#42819](https://github.com/nodejs/node/pull/42819)
* \[[`bf9240ae8c`](https://github.com/nodejs/node/commit/bf9240ae8c)] - **(SEMVER-MINOR)** **worker**: add hasRef() to MessagePort (Darshan Sen) [#42849](https://github.com/nodejs/node/pull/42849)
* \[[`c3922afa1c`](https://github.com/nodejs/node/commit/c3922afa1c)] - **worker**: add hasRef() to the handle object (Darshan Sen) [#42756](https://github.com/nodejs/node/pull/42756)

<a id="18.0.0"></a>

## 2022-04-19, Version 18.0.0 (Current), @BethGriggs

Node.js 18 is here! Highlights include the update of the V8 JavaScript engine to 10.1, global fetch enabled by default, and a core test runner module.

Initially, Node.js 18 will replace Node.js 17 as our ‘Current’ release line. As per the release schedule, Node.js 18 will be the ‘Current’ release for the next 6 months and then promoted to Long-term Support (LTS) in October 2022. Once promoted to long-term support the release will be designated the codename ‘Hydrogen’. Node.js 18 will be supported until April 2025.

### Notable Changes

#### Deprecations and Removals

* **(SEMVER-MAJOR)** **fs**: runtime deprecate string coercion in `fs.write`, `fs.writeFileSync` (Livia Medeiros) [#42607](https://github.com/nodejs/node/pull/42607)
* **(SEMVER-MAJOR)** **dns**: remove `dns.lookup` and `dnsPromises.lookup` options type coercion (Antoine du Hamel) [#41431](https://github.com/nodejs/node/pull/41431)
* **(SEMVER-MAJOR)** **process**: runtime deprecate multipleResolves (Benjamin Gruenbaum) [#41896](https://github.com/nodejs/node/pull/41896)
* **(SEMVER-MAJOR)** **stream**: remove thenable support (Robert Nagy) [#40773](https://github.com/nodejs/node/pull/40773)
* **(SEMVER-MAJOR)** **tls**: move tls.parseCertString to end-of-life (Tobias Nießen) [#41479](https://github.com/nodejs/node/pull/41479)

#### fetch (experimental)

An experimental fetch API is available on the global scope by default. The implementation is based upon [undici](https://undici.nodejs.org/#/), an HTTP/1.1 client written for Node.js by contributors to the project.

```mjs
const res = await fetch('https://nodejs.org/api/documentation.json');
if (res.ok) {
  const data = await res.json();
  console.log(data);
}
```

Through this addition, the following globals are made available: `fetch`, `FormData`, `Headers`, `Request`, `Response`.

Disable this API with the `--no-experimental-fetch` command-line flag.

Contributed by Michaël Zasso in [#41811](https://github.com/nodejs/node/pull/41811).

#### HTTP Timeouts

`server.headersTimeout` which limits the amount of time the parser will wait to receive the complete HTTP headers is now set to `60000` (60 seconds) by default.

`server.requestTimeout` which sets the timeout value in milliseconds for receiving the entire request from the client is now set to `300000` (5 minutes) by default.

If these timeouts expire, the server responds with status 408 without forwarding the request to the request listener and then closes the connection.

Both timeouts must be set to a non-zero value to protect against potential Denial-of-Service attacks in case the server is deployed without a reverse proxy in front.

Contributed by Paolo Insogna in [#41263](https://github.com/nodejs/node/pull/41263).

#### Test Runner module (experimental)

The `node:test` module facilitates the creation of JavaScript tests that report results in TAP format. To access it:

`import test from 'node:test';`

This module is only available under the `node:` scheme.

The following is an example implementation of a parent test with two subtests:

```js
test('top level test', async (t) => {
  await t.test('subtest 1', (t) => {
    assert.strictEqual(1, 1);
  });

  await t.test('subtest 2', (t) => {
    assert.strictEqual(2, 2);
  });
});
```

Read more in <https://nodejs.org/dist/latest-v18.x/docs/api/test.html>.

Contributed by Colin Ihrig in [#42325](https://github.com/nodejs/node/pull/42325).

#### Toolchain and Compiler Upgrades

* Prebuilt binaries for Linux are now built on Red Hat Enterprise Linux (RHEL) 8 and are compatible with Linux distributions based on glibc 2.28 or later, for example, Debian 10, RHEL 8, Ubuntu 20.04.
* Prebuilt binaries for macOS now require macOS 10.15 or later.
* For AIX the minimum supported architecture has been raised from Power 7 to Power 8.

Prebuilt binaries for 32-bit Windows will initially not be available due to issues building the V8 dependency in Node.js. We hope to restore 32-bit Windows binaries for Node.js 18 with a future V8 update.

Node.js does not support running on operating systems that are no longer supported by their vendor. For operating systems where their vendor has planned to end support earlier than April 2025, such as Windows 8.1 (January 2023) and Windows Server 2012 R2 (October 2023), support for Node.js 18 will end at the earlier date.

Full details about the supported toolchains and compilers are documented in the Node.js [BUILDING.md](https://github.com/nodejs/node/blob/v18.x/BUILDING.md#supported-platforms) file.

Contributed by Richard Lau in [#42292](https://github.com/nodejs/node/pull/42292), [#42604](https://github.com/nodejs/node/pull/42604) and [#42659](https://github.com/nodejs/node/pull/42659),and Michaël Zasso in [#42105](https://github.com/nodejs/node/pull/42105) and [#42666](https://github.com/nodejs/node/pull/42666).

#### V8 10.1

The V8 engine is updated to version 10.1, which is part of Chromium 101. Compared to the version included in Node.js 17.9.0, the following new features are included:

* The [`findLast` and `findLastIndex` array methods](https://v8.dev/features/finding-in-arrays).
* Improvements to the [`Intl.Locale` API](https://v8.dev/blog/v8-release-99#intl.locale-extensions).
* The [`Intl.supportedValuesOf` function](https://v8.dev/blog/v8-release-99#intl-enumeration).
* Improved performance of [class fields](https://bugs.chromium.org/p/v8/issues/detail?id=9888) and [private class methods](https://bugs.chromium.org/p/v8/issues/detail?id=10793) (the initialization of them is now as fast as ordinary property stores).

The data format returned by the serialization API (`v8.serialize(value)`) has changed, and cannot be deserialized by earlier versions of Node.js. On the other hand, it is still possible to deserialize the previous format, as the API is backwards-compatible.

Contributed by Michaël Zasso in <https://github.com/nodejs/node/pull/42657>.

#### Web Streams API (experimental)

Node.js now exposes the experimental implementation of the [Web Streams API](https://developer.mozilla.org/en-US/docs/Web/API/Streams_API) on the global scope. This means the following APIs are now globally available:

* `ReadableStream`, `ReadableStreamDefaultReader`, `ReadableStreamBYOBReader`, `ReadableStreamBYOBRequest`, `ReadableByteStreamController`, `ReadableStreamDefaultController`, `TransformStream`, `TransformStreamDefaultController`, `WritableStream`, `WritableStreamDefaultWriter`, `WritableStreamDefaultController`, `ByteLengthQueuingStrategy`, `CountQueuingStrategy`, `TextEncoderStream`, `TextDecoderStream`, `CompressionStream`, `DecompressionStream`.

Contributed James Snell in <https://github.com/nodejs/node/pull/39062>, and Antoine du Hamel in <https://github.com/nodejs/node/pull/42225>.

#### Other Notable Changes

* **(SEMVER-MAJOR)** **buffer**: expose Blob as a global (James M Snell) [#41270](https://github.com/nodejs/node/pull/41270)
* **(SEMVER-MAJOR)** **child\_process**: improve argument validation (Rich Trott) [#41305](https://github.com/nodejs/node/pull/41305)
* **doc**: add RafaelGSS to collaborators (RafaelGSS) [#42718](https://github.com/nodejs/node/pull/42718)
* **(SEMVER-MAJOR)** **http**: make TCP noDelay enabled by default (Paolo Insogna) [#42163](https://github.com/nodejs/node/pull/42163)
* **(SEMVER-MAJOR)** **net**: make `server.address()` return an integer for `family` (Antoine du Hamel) [#41431](https://github.com/nodejs/node/pull/41431)
* **(SEMVER-MAJOR)** **worker**: expose BroadcastChannel as a global (James M Snell) [#41271](https://github.com/nodejs/node/pull/41271)
* **(SEMVER-MAJOR)** **worker**: graduate BroadcastChannel to supported (James M Snell) [#41271](https://github.com/nodejs/node/pull/41271)

### Semver-Major Commits

* \[[`dab8ab2837`](https://github.com/nodejs/node/commit/dab8ab2837)] - **(SEMVER-MAJOR)** **assert,util**: compare RegExp.lastIndex while using deep equal checks (Ruben Bridgewater) [#41020](https://github.com/nodejs/node/pull/41020)
* \[[`cff14bcaef`](https://github.com/nodejs/node/commit/cff14bcaef)] - **(SEMVER-MAJOR)** **buffer**: refactor `byteLength` to remove outdated optimizations (Rongjian Zhang) [#38545](https://github.com/nodejs/node/pull/38545)
* \[[`cea76dbf33`](https://github.com/nodejs/node/commit/cea76dbf33)] - **(SEMVER-MAJOR)** **buffer**: expose Blob as a global (James M Snell) [#41270](https://github.com/nodejs/node/pull/41270)
* \[[`99c18f4786`](https://github.com/nodejs/node/commit/99c18f4786)] - **(SEMVER-MAJOR)** **buffer**: graduate Blob from experimental (James M Snell) [#41270](https://github.com/nodejs/node/pull/41270)
* \[[`35d72bf4ec`](https://github.com/nodejs/node/commit/35d72bf4ec)] - **(SEMVER-MAJOR)** **build**: make x86 Windows support temporarily experimental (Michaël Zasso) [#42666](https://github.com/nodejs/node/pull/42666)
* \[[`1134d8faf8`](https://github.com/nodejs/node/commit/1134d8faf8)] - **(SEMVER-MAJOR)** **build**: bump macOS deployment target to 10.15 (Richard Lau) [#42292](https://github.com/nodejs/node/pull/42292)
* \[[`27eb91d378`](https://github.com/nodejs/node/commit/27eb91d378)] - **(SEMVER-MAJOR)** **build**: downgrade Windows 8.1 and server 2012 R2 to experimental (Michaël Zasso) [#42105](https://github.com/nodejs/node/pull/42105)
* \[[`26c973d4b3`](https://github.com/nodejs/node/commit/26c973d4b3)] - **(SEMVER-MAJOR)** **child\_process**: improve argument validation (Rich Trott) [#41305](https://github.com/nodejs/node/pull/41305)
* \[[`38007df999`](https://github.com/nodejs/node/commit/38007df999)] - **(SEMVER-MAJOR)** **cluster**: make `kill` to be just `process.kill` (Bar Admoni) [#34312](https://github.com/nodejs/node/pull/34312)
* \[[`aed18dfe59`](https://github.com/nodejs/node/commit/aed18dfe59)] - **(SEMVER-MAJOR)** **crypto**: cleanup validation (Mohammed Keyvanzadeh) [#39841](https://github.com/nodejs/node/pull/39841)
* \[[`e1fb6ae02f`](https://github.com/nodejs/node/commit/e1fb6ae02f)] - **(SEMVER-MAJOR)** **crypto**: prettify othername in PrintGeneralName (Tobias Nießen) [#42123](https://github.com/nodejs/node/pull/42123)
* \[[`36fb79030e`](https://github.com/nodejs/node/commit/36fb79030e)] - **(SEMVER-MAJOR)** **crypto**: fix X509Certificate toLegacyObject (Tobias Nießen) [#42124](https://github.com/nodejs/node/pull/42124)
* \[[`563b2ed000`](https://github.com/nodejs/node/commit/563b2ed000)] - **(SEMVER-MAJOR)** **crypto**: use RFC2253 format in PrintGeneralName (Tobias Nießen) [#42002](https://github.com/nodejs/node/pull/42002)
* \[[`18365d8ee6`](https://github.com/nodejs/node/commit/18365d8ee6)] - **(SEMVER-MAJOR)** **crypto**: change default check(Host|Email) behavior (Tobias Nießen) [#41600](https://github.com/nodejs/node/pull/41600)
* \[[`58f3fdcccd`](https://github.com/nodejs/node/commit/58f3fdcccd)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick semver-major commits from 10.2 (Michaël Zasso) [#42657](https://github.com/nodejs/node/pull/42657)
* \[[`fd4f80ce54`](https://github.com/nodejs/node/commit/fd4f80ce54)] - **(SEMVER-MAJOR)** **deps**: update V8 to 10.1.124.6 (Michaël Zasso) [#42657](https://github.com/nodejs/node/pull/42657)
* \[[`974ab4060f`](https://github.com/nodejs/node/commit/974ab4060f)] - **(SEMVER-MAJOR)** **deps**: update V8 to 9.8.177.9 (Michaël Zasso) [#41610](https://github.com/nodejs/node/pull/41610)
* \[[`270253c4e2`](https://github.com/nodejs/node/commit/270253c4e2)] - **(SEMVER-MAJOR)** **deps**: update V8 to 9.7.106.18 (Michaël Zasso) [#40907](https://github.com/nodejs/node/pull/40907)
* \[[`08773e3c04`](https://github.com/nodejs/node/commit/08773e3c04)] - **(SEMVER-MAJOR)** **dns**: remove `dns.lookup` and `dnsPromises.lookup` options type coercion (Antoine du Hamel) [#41431](https://github.com/nodejs/node/pull/41431)
* \[[`3671cc0432`](https://github.com/nodejs/node/commit/3671cc0432)] - **(SEMVER-MAJOR)** **doc**: update minimum glibc requirements for Linux (Richard Lau) [#42659](https://github.com/nodejs/node/pull/42659)
* \[[`646e057680`](https://github.com/nodejs/node/commit/646e057680)] - **(SEMVER-MAJOR)** **doc**: update AIX minimum supported arch (Richard Lau) [#42604](https://github.com/nodejs/node/pull/42604)
* \[[`0bac5478eb`](https://github.com/nodejs/node/commit/0bac5478eb)] - **(SEMVER-MAJOR)** **fs**: runtime deprecate string coercion in `fs.write`, `fs.writeFileSync` (Livia Medeiros) [#42607](https://github.com/nodejs/node/pull/42607)
* \[[`3caa2c1a00`](https://github.com/nodejs/node/commit/3caa2c1a00)] - **(SEMVER-MAJOR)** **http**: refactor headersTimeout and requestTimeout logic (Paolo Insogna) [#41263](https://github.com/nodejs/node/pull/41263)
* \[[`eacd45656a`](https://github.com/nodejs/node/commit/eacd45656a)] - **(SEMVER-MAJOR)** **http**: make TCP noDelay enabled by default (Paolo Insogna) [#42163](https://github.com/nodejs/node/pull/42163)
* \[[`4944ad0b9e`](https://github.com/nodejs/node/commit/4944ad0b9e)] - **(SEMVER-MAJOR)** **lib**: enable fetch by default (Michaël Zasso) [#41811](https://github.com/nodejs/node/pull/41811)
* \[[`8c4b8b201a`](https://github.com/nodejs/node/commit/8c4b8b201a)] - **(SEMVER-MAJOR)** **lib**: replace validator and error (Mohammed Keyvanzadeh) [#41678](https://github.com/nodejs/node/pull/41678)
* \[[`3c4ee5267a`](https://github.com/nodejs/node/commit/3c4ee5267a)] - **(SEMVER-MAJOR)** **module,repl**: support 'node:'-only core modules (Colin Ihrig) [#42325](https://github.com/nodejs/node/pull/42325)
* \[[`3a26db9697`](https://github.com/nodejs/node/commit/3a26db9697)] - **(SEMVER-MAJOR)** **net**: make `server.address()` return an integer for `family` (Antoine du Hamel) [#41431](https://github.com/nodejs/node/pull/41431)
* \[[`e6a7300a10`](https://github.com/nodejs/node/commit/e6a7300a10)] - **(SEMVER-MAJOR)** **process**: disallow some uses of Object.defineProperty() on process.env (Himself65) [#28006](https://github.com/nodejs/node/pull/28006)
* \[[`60b8e79599`](https://github.com/nodejs/node/commit/60b8e79599)] - **(SEMVER-MAJOR)** **process**: runtime deprecate multipleResolves (Benjamin Gruenbaum) [#41896](https://github.com/nodejs/node/pull/41896)
* \[[`d36b60e69a`](https://github.com/nodejs/node/commit/d36b60e69a)] - **(SEMVER-MAJOR)** **readline**: fix question still called after closed (Xuguang Mei) [#42464](https://github.com/nodejs/node/pull/42464)
* \[[`58e645de63`](https://github.com/nodejs/node/commit/58e645de63)] - **(SEMVER-MAJOR)** **stream**: remove thenable support (Robert Nagy) [#40773](https://github.com/nodejs/node/pull/40773)
* \[[`560cbc5849`](https://github.com/nodejs/node/commit/560cbc5849)] - **(SEMVER-MAJOR)** **stream**: expose web streams globals, remove runtime experimental warning (Antoine du Hamel) [#42225](https://github.com/nodejs/node/pull/42225)
* \[[`9fb7ac3bbd`](https://github.com/nodejs/node/commit/9fb7ac3bbd)] - **(SEMVER-MAJOR)** **stream**: need to cleanup event listeners if last stream is readable (Xuguang Mei) [#41954](https://github.com/nodejs/node/pull/41954)
* \[[`ceaa299958`](https://github.com/nodejs/node/commit/ceaa299958)] - **(SEMVER-MAJOR)** **stream**: revert revert `map` spec compliance (Benjamin Gruenbaum) [#41933](https://github.com/nodejs/node/pull/41933)
* \[[`fe7ca085a7`](https://github.com/nodejs/node/commit/fe7ca085a7)] - **(SEMVER-MAJOR)** **stream**: throw invalid arg type from End Of Stream (Jithil P Ponnan) [#41766](https://github.com/nodejs/node/pull/41766)
* \[[`48e784043d`](https://github.com/nodejs/node/commit/48e784043d)] - **(SEMVER-MAJOR)** **stream**: don't emit finish after destroy (Robert Nagy) [#40852](https://github.com/nodejs/node/pull/40852)
* \[[`f2170253b6`](https://github.com/nodejs/node/commit/f2170253b6)] - **(SEMVER-MAJOR)** **stream**: add errored and closed props (Robert Nagy) [#40696](https://github.com/nodejs/node/pull/40696)
* \[[`432d1b50e0`](https://github.com/nodejs/node/commit/432d1b50e0)] - **(SEMVER-MAJOR)** **test**: add initial test module (Colin Ihrig) [#42325](https://github.com/nodejs/node/pull/42325)
* \[[`92567283f4`](https://github.com/nodejs/node/commit/92567283f4)] - **(SEMVER-MAJOR)** **timers**: refactor internal classes to ES2015 syntax (Rabbit) [#37408](https://github.com/nodejs/node/pull/37408)
* \[[`65910c0d6c`](https://github.com/nodejs/node/commit/65910c0d6c)] - **(SEMVER-MAJOR)** **tls**: represent registeredID numerically always (Tobias Nießen) [#41561](https://github.com/nodejs/node/pull/41561)
* \[[`807c7e14f4`](https://github.com/nodejs/node/commit/807c7e14f4)] - **(SEMVER-MAJOR)** **tls**: move tls.parseCertString to end-of-life (Tobias Nießen) [#41479](https://github.com/nodejs/node/pull/41479)
* \[[`f524306077`](https://github.com/nodejs/node/commit/f524306077)] - **(SEMVER-MAJOR)** **url**: throw on NULL in IPv6 hostname (Rich Trott) [#42313](https://github.com/nodejs/node/pull/42313)
* \[[`0187bc5cdc`](https://github.com/nodejs/node/commit/0187bc5cdc)] - **(SEMVER-MAJOR)** **v8**: make v8.writeHeapSnapshot() error codes consistent (Darshan Sen) [#42577](https://github.com/nodejs/node/pull/42577)
* \[[`74b9baa426`](https://github.com/nodejs/node/commit/74b9baa426)] - **(SEMVER-MAJOR)** **v8**: make writeHeapSnapshot throw if fopen fails (Antonio Román) [#41373](https://github.com/nodejs/node/pull/41373)
* \[[`ce4d3adf50`](https://github.com/nodejs/node/commit/ce4d3adf50)] - **(SEMVER-MAJOR)** **worker**: expose BroadcastChannel as a global (James M Snell) [#41271](https://github.com/nodejs/node/pull/41271)
* \[[`6486a304d3`](https://github.com/nodejs/node/commit/6486a304d3)] - **(SEMVER-MAJOR)** **worker**: graduate BroadcastChannel to supported (James M Snell) [#41271](https://github.com/nodejs/node/pull/41271)

### Semver-Minor Commits

* \[[`415726b8c4`](https://github.com/nodejs/node/commit/415726b8c4)] - **(SEMVER-MINOR)** **stream**: add writableAborted (Robert Nagy) [#40802](https://github.com/nodejs/node/pull/40802)
* \[[`54819f08e0`](https://github.com/nodejs/node/commit/54819f08e0)] - **(SEMVER-MINOR)** **test\_runner**: support 'only' tests (Colin Ihrig) [#42514](https://github.com/nodejs/node/pull/42514)

### Semver-Patch Commits

* \[[`7533d08b94`](https://github.com/nodejs/node/commit/7533d08b94)] - **buffer**: fix `atob` input validation (Austin Kelleher) [#42662](https://github.com/nodejs/node/pull/42662)
* \[[`96673bcb96`](https://github.com/nodejs/node/commit/96673bcb96)] - **build**: run clang-format on CI (Darshan Sen) [#42681](https://github.com/nodejs/node/pull/42681)
* \[[`d5462e4558`](https://github.com/nodejs/node/commit/d5462e4558)] - **build**: reset embedder string to "-node.0" (Michaël Zasso) [#42657](https://github.com/nodejs/node/pull/42657)
* \[[`aa52873887`](https://github.com/nodejs/node/commit/aa52873887)] - **build**: add configure option --v8-enable-short-builtin-calls (daomingq) [#42109](https://github.com/nodejs/node/pull/42109)
* \[[`7ee8a7a463`](https://github.com/nodejs/node/commit/7ee8a7a463)] - **build**: reset embedder string to "-node.0" (Michaël Zasso) [#41610](https://github.com/nodejs/node/pull/41610)
* \[[`a189dee52a`](https://github.com/nodejs/node/commit/a189dee52a)] - **build**: reset embedder string to "-node.0" (Michaël Zasso) [#40907](https://github.com/nodejs/node/pull/40907)
* \[[`e8697cfe38`](https://github.com/nodejs/node/commit/e8697cfe38)] - **crypto**: improve prime size argument validation (Tobias Nießen) [#42234](https://github.com/nodejs/node/pull/42234)
* \[[`a9c0689786`](https://github.com/nodejs/node/commit/a9c0689786)] - **crypto**: fix return type prob reported by coverity (Michael Dawson) [#42135](https://github.com/nodejs/node/pull/42135)
* \[[`e938515b41`](https://github.com/nodejs/node/commit/e938515b41)] - **deps**: patch V8 to 10.1.124.8 (Michaël Zasso) [#42730](https://github.com/nodejs/node/pull/42730)
* \[[`eba7d2db7f`](https://github.com/nodejs/node/commit/eba7d2db7f)] - **deps**: V8: cherry-pick ad21d212fc14 (Michaël Zasso) [#42657](https://github.com/nodejs/node/pull/42657)
* \[[`004137e269`](https://github.com/nodejs/node/commit/004137e269)] - **deps**: V8: cherry-pick 4c29cf1b7885 (Michaël Zasso) [#42657](https://github.com/nodejs/node/pull/42657)
* \[[`a052c03033`](https://github.com/nodejs/node/commit/a052c03033)] - **deps**: V8: cherry-pick ca2a787a0b49 (Michaël Zasso) [#42657](https://github.com/nodejs/node/pull/42657)
* \[[`01cea9a8d8`](https://github.com/nodejs/node/commit/01cea9a8d8)] - **deps**: V8: cherry-pick a2cae2180a7a (Michaël Zasso) [#42657](https://github.com/nodejs/node/pull/42657)
* \[[`d9d26b08ef`](https://github.com/nodejs/node/commit/d9d26b08ef)] - **deps**: V8: cherry-pick 87ce4f5d98a5 (Michaël Zasso) [#42657](https://github.com/nodejs/node/pull/42657)
* \[[`64a6328505`](https://github.com/nodejs/node/commit/64a6328505)] - **deps**: make V8 compilable with older glibc (Michaël Zasso) [#42657](https://github.com/nodejs/node/pull/42657)
* \[[`fde59217b9`](https://github.com/nodejs/node/commit/fde59217b9)] - **deps**: V8: fix v8-cppgc.h for MSVC (Jiawen Geng) [#42657](https://github.com/nodejs/node/pull/42657)
* \[[`cdcc82cced`](https://github.com/nodejs/node/commit/cdcc82cced)] - **deps**: silence V8's warning on CompileFunction (Michaël Zasso) [#40907](https://github.com/nodejs/node/pull/40907)
* \[[`2f51e121da`](https://github.com/nodejs/node/commit/2f51e121da)] - **deps**: update Acorn to v8.7.0 (Michaël Zasso) [#42667](https://github.com/nodejs/node/pull/42667)
* \[[`6d4b01774b`](https://github.com/nodejs/node/commit/6d4b01774b)] - **deps**: update ICU to 71.1 (Michaël Zasso) [#42655](https://github.com/nodejs/node/pull/42655)
* \[[`2d84620f86`](https://github.com/nodejs/node/commit/2d84620f86)] - **deps**: upgrade npm to 8.6.0 (npm team) [#42550](https://github.com/nodejs/node/pull/42550)
* \[[`c7ac11fa25`](https://github.com/nodejs/node/commit/c7ac11fa25)] - **deps**: update undici to 5.0.0 (Node.js GitHub Bot) [#42583](https://github.com/nodejs/node/pull/42583)
* \[[`468fffdf66`](https://github.com/nodejs/node/commit/468fffdf66)] - **deps**: V8: cherry-pick 50d5fb7a457c (Michaël Zasso) [#41610](https://github.com/nodejs/node/pull/41610)
* \[[`48708be57b`](https://github.com/nodejs/node/commit/48708be57b)] - **deps**: V8: cherry-pick 79a9d2eb3477 (Michaël Zasso) [#41610](https://github.com/nodejs/node/pull/41610)
* \[[`3c8782f70e`](https://github.com/nodejs/node/commit/3c8782f70e)] - **deps**: silence V8's warning on CompileFunction (Michaël Zasso) [#40907](https://github.com/nodejs/node/pull/40907)
* \[[`9318408c49`](https://github.com/nodejs/node/commit/9318408c49)] - **deps**: silence V8's warning on CompileFunction (Michaël Zasso) [#40907](https://github.com/nodejs/node/pull/40907)
* \[[`e23e345b6c`](https://github.com/nodejs/node/commit/e23e345b6c)] - **deps**: V8: cherry-pick 80bbbb143c24 (Michaël Zasso) [#40907](https://github.com/nodejs/node/pull/40907)
* \[[`696ce7df26`](https://github.com/nodejs/node/commit/696ce7df26)] - **deps**: V8: cherry-pick 1cc12b278e22 (Michaël Zasso) [#40907](https://github.com/nodejs/node/pull/40907)
* \[[`aa88e5e4b9`](https://github.com/nodejs/node/commit/aa88e5e4b9)] - **doc**: revise data imports and node: imports sections (Rich Trott) [#42734](https://github.com/nodejs/node/pull/42734)
* \[[`a058cefe29`](https://github.com/nodejs/node/commit/a058cefe29)] - **doc**: fix ESM JSON/data URL import example (Rich Trott) [#42728](https://github.com/nodejs/node/pull/42728)
* \[[`e61b62b9d4`](https://github.com/nodejs/node/commit/e61b62b9d4)] - **doc**: improve doc for http.ServerResponse inheritance (Luigi Pinca) [#42693](https://github.com/nodejs/node/pull/42693)
* \[[`6669b3857f`](https://github.com/nodejs/node/commit/6669b3857f)] - **doc**: add RafaelGSS to collaborators (RafaelGSS) [#42718](https://github.com/nodejs/node/pull/42718)
* \[[`f825341bab`](https://github.com/nodejs/node/commit/f825341bab)] - **doc**: add NodeEdKeyGenParams to CryptoKey.algorithm (Tobias Nießen) [#42629](https://github.com/nodejs/node/pull/42629)
* \[[`d4d78361f2`](https://github.com/nodejs/node/commit/d4d78361f2)] - **doc**: fix the example for embedders (Momtchil Momtchev) [#42671](https://github.com/nodejs/node/pull/42671)
* \[[`6706be1cdb`](https://github.com/nodejs/node/commit/6706be1cdb)] - **doc**: change AES-GCM IV recommendation in WebCrypto (Tobias Nießen) [#42611](https://github.com/nodejs/node/pull/42611)
* \[[`4508c8caa4`](https://github.com/nodejs/node/commit/4508c8caa4)] - **doc**: fix `added:` info for some methods (Luigi Pinca) [#42661](https://github.com/nodejs/node/pull/42661)
* \[[`951dbc045a`](https://github.com/nodejs/node/commit/951dbc045a)] - **doc**: remove unneeded new in Buffer example (Niklas Mischkulnig) [#42682](https://github.com/nodejs/node/pull/42682)
* \[[`65e838071b`](https://github.com/nodejs/node/commit/65e838071b)] - **doc**: mark worker.id as integer in cluster docs (Tobias Nießen) [#42684](https://github.com/nodejs/node/pull/42684)
* \[[`a82713cbb6`](https://github.com/nodejs/node/commit/a82713cbb6)] - **doc**: recommend `fh.createWriteStream` for fsPromises methods (Antoine du Hamel) [#42653](https://github.com/nodejs/node/pull/42653)
* \[[`13ad8d4e09`](https://github.com/nodejs/node/commit/13ad8d4e09)] - **doc**: fix outgoingMessage.removeHeader() signature (Luigi Pinca) [#42652](https://github.com/nodejs/node/pull/42652)
* \[[`a0461255c0`](https://github.com/nodejs/node/commit/a0461255c0)] - **doc**: mark tlsSocket.authorized as boolean property (Tobias Nießen) [#42647](https://github.com/nodejs/node/pull/42647)
* \[[`3ac7f86c2b`](https://github.com/nodejs/node/commit/3ac7f86c2b)] - **doc**: add missing punctuation in Web Streams doc (Tobias Nießen) [#42672](https://github.com/nodejs/node/pull/42672)
* \[[`b98386c977`](https://github.com/nodejs/node/commit/b98386c977)] - **doc**: add missing article in session ticket section (Tobias Nießen) [#42632](https://github.com/nodejs/node/pull/42632)
* \[[`a113468383`](https://github.com/nodejs/node/commit/a113468383)] - **doc**: link to dynamic import function (Tobias Nießen) [#42634](https://github.com/nodejs/node/pull/42634)
* \[[`dfc2dc8b65`](https://github.com/nodejs/node/commit/dfc2dc8b65)] - **doc**: add note about header values encoding (Shogun) [#42624](https://github.com/nodejs/node/pull/42624)
* \[[`ec5a359ffd`](https://github.com/nodejs/node/commit/ec5a359ffd)] - **doc**: add missing word in rootCertificates section (Tobias Nießen) [#42633](https://github.com/nodejs/node/pull/42633)
* \[[`c08a361f70`](https://github.com/nodejs/node/commit/c08a361f70)] - **doc**: add history entries for DEP0162 on `fs.md` (Antoine du Hamel) [#42608](https://github.com/nodejs/node/pull/42608)
* \[[`4fade6acb4`](https://github.com/nodejs/node/commit/4fade6acb4)] - **doc**: fix brackets position (Livia Medeiros) [#42649](https://github.com/nodejs/node/pull/42649)
* \[[`8055c7ba5d`](https://github.com/nodejs/node/commit/8055c7ba5d)] - **doc**: copyedit corepack.md (Rich Trott) [#42620](https://github.com/nodejs/node/pull/42620)
* \[[`85a65c3260`](https://github.com/nodejs/node/commit/85a65c3260)] - **doc**: delete chakra tt from diagnostic tooling support tiers (Tony Gorez) [#42627](https://github.com/nodejs/node/pull/42627)
* \[[`63bb6dcf0f`](https://github.com/nodejs/node/commit/63bb6dcf0f)] - **doc**: align links in table to top (nikoladev) [#41396](https://github.com/nodejs/node/pull/41396)
* \[[`28d8614add`](https://github.com/nodejs/node/commit/28d8614add)] - **http**: document that ClientRequest inherits from OutgoingMessage (K.C.Ashish Kumar) [#42642](https://github.com/nodejs/node/pull/42642)
* \[[`c37fdacb34`](https://github.com/nodejs/node/commit/c37fdacb34)] - **lib**: use class fields in observe.js (Joyee Cheung) [#42361](https://github.com/nodejs/node/pull/42361)
* \[[`ea0668a27e`](https://github.com/nodejs/node/commit/ea0668a27e)] - **lib**: use class fields in Event and EventTarget (Joyee Cheung) [#42361](https://github.com/nodejs/node/pull/42361)
* \[[`eb7b89c829`](https://github.com/nodejs/node/commit/eb7b89c829)] - **lib**: update class fields TODO in abort\_controller.js (Joyee Cheung) [#42361](https://github.com/nodejs/node/pull/42361)
* \[[`d835b1f1c1`](https://github.com/nodejs/node/commit/d835b1f1c1)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#42677](https://github.com/nodejs/node/pull/42677)
* \[[`29492496e8`](https://github.com/nodejs/node/commit/29492496e8)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#42599](https://github.com/nodejs/node/pull/42599)
* \[[`93c4dc5e5a`](https://github.com/nodejs/node/commit/93c4dc5e5a)] - **module**: ensure 'node:'-only modules can access node\_modules (Colin Ihrig) [#42430](https://github.com/nodejs/node/pull/42430)
* \[[`3a26db9697`](https://github.com/nodejs/node/commit/3a26db9697)] - **net**: make `server.address()` return an integer for `family` (Antoine du Hamel) [#41431](https://github.com/nodejs/node/pull/41431)
* \[[`44fdf953ba`](https://github.com/nodejs/node/commit/44fdf953ba)] - **node-api,src**: fix module registration in MSVC C++ (Vladimir Morozov) [#42459](https://github.com/nodejs/node/pull/42459)
* \[[`3026ca0bf2`](https://github.com/nodejs/node/commit/3026ca0bf2)] - **src**: fix coverity report (Michael Dawson) [#42663](https://github.com/nodejs/node/pull/42663)
* \[[`01fd048c6e`](https://github.com/nodejs/node/commit/01fd048c6e)] - **src**: update NODE\_MODULE\_VERSION to 108 (Michaël Zasso) [#42657](https://github.com/nodejs/node/pull/42657)
* \[[`75a71dc7ae`](https://github.com/nodejs/node/commit/75a71dc7ae)] - **src**: fix alphabetically sorted binding list (Tobias Nießen) [#42687](https://github.com/nodejs/node/pull/42687)
* \[[`457567f72c`](https://github.com/nodejs/node/commit/457567f72c)] - **src**: include crypto in the bootstrap snapshot (Joyee Cheung) [#42203](https://github.com/nodejs/node/pull/42203)
* \[[`aa7dc808f5`](https://github.com/nodejs/node/commit/aa7dc808f5)] - **src**: update ImportModuleDynamically (Camillo Bruni) [#41610](https://github.com/nodejs/node/pull/41610)
* \[[`fa0439e66c`](https://github.com/nodejs/node/commit/fa0439e66c)] - **src**: update NODE\_MODULE\_VERSION to 105 (Michaël Zasso) [#41610](https://github.com/nodejs/node/pull/41610)
* \[[`6ec1664dc8`](https://github.com/nodejs/node/commit/6ec1664dc8)] - **src**: update NODE\_MODULE\_VERSION to 104 (Michaël Zasso) [#40907](https://github.com/nodejs/node/pull/40907)
* \[[`a706342368`](https://github.com/nodejs/node/commit/a706342368)] - **src**: add kNoBrowserGlobals flag for Environment (Cheng Zhao) [#40532](https://github.com/nodejs/node/pull/40532)
* \[[`0c57a37dd0`](https://github.com/nodejs/node/commit/0c57a37dd0)] - **src,crypto**: remove uses of AllocatedBuffer from crypto\_tls.cc (Darshan Sen) [#42589](https://github.com/nodejs/node/pull/42589)
* \[[`be01185844`](https://github.com/nodejs/node/commit/be01185844)] - **src,inspector**: fix empty MaybeLocal crash (Darshan Sen) [#42409](https://github.com/nodejs/node/pull/42409)
* \[[`340b770d3f`](https://github.com/nodejs/node/commit/340b770d3f)] - **stream**: unify writableErrored and readableErrored (Robert Nagy) [#40799](https://github.com/nodejs/node/pull/40799)
* \[[`19064bec34`](https://github.com/nodejs/node/commit/19064bec34)] - **test**: delete test/pummel/test-repl-empty-maybelocal-crash.js (Darshan Sen) [#42720](https://github.com/nodejs/node/pull/42720)
* \[[`9d6af7d1fe`](https://github.com/nodejs/node/commit/9d6af7d1fe)] - **test**: improve `internal/url.js` coverage (Yoshiki Kurihara) [#42650](https://github.com/nodejs/node/pull/42650)
* \[[`d49df5ca8d`](https://github.com/nodejs/node/commit/d49df5ca8d)] - **test**: adapt message tests for V8 10.2 (Michaël Zasso) [#42657](https://github.com/nodejs/node/pull/42657)
* \[[`c6b4e9604f`](https://github.com/nodejs/node/commit/c6b4e9604f)] - **test**: adapt test-worker-debug for V8 10.0 (Michaël Zasso) [#42657](https://github.com/nodejs/node/pull/42657)
* \[[`0854fce8bc`](https://github.com/nodejs/node/commit/0854fce8bc)] - **test**: adapt test-v8-serdes for V8 9.9 (Michaël Zasso) [#42657](https://github.com/nodejs/node/pull/42657)
* \[[`73d53fe9f5`](https://github.com/nodejs/node/commit/73d53fe9f5)] - **test**: only skip slow tests on Raspberry Pi devices (Richard Lau) [#42645](https://github.com/nodejs/node/pull/42645)
* \[[`db7fa9f4b7`](https://github.com/nodejs/node/commit/db7fa9f4b7)] - **test**: allow numeric string for lookupService test (Daeyeon Jeong) [#42596](https://github.com/nodejs/node/pull/42596)
* \[[`0525a147b2`](https://github.com/nodejs/node/commit/0525a147b2)] - **test**: remove an unnecessary `undefined` in wpt (Khaidi Chu) [#41470](https://github.com/nodejs/node/pull/41470)
* \[[`bb762c5bd0`](https://github.com/nodejs/node/commit/bb762c5bd0)] - **test**: simplify test-http-write-callbacks.js (Tobias Nießen) [#42628](https://github.com/nodejs/node/pull/42628)
* \[[`1600869eb7`](https://github.com/nodejs/node/commit/1600869eb7)] - **test**: fix comments in test files (Daeyeon Jeong) [#42536](https://github.com/nodejs/node/pull/42536)
* \[[`82181bb9b8`](https://github.com/nodejs/node/commit/82181bb9b8)] - **test**: fix failure in test/sequential/test-heapdump.js (Darshan Sen) [#41772](https://github.com/nodejs/node/pull/41772)
* \[[`ba5b5acaf1`](https://github.com/nodejs/node/commit/ba5b5acaf1)] - **test**: improve `worker_threads ` coverage (Erick Wendel) [#41818](https://github.com/nodejs/node/pull/41818)
* \[[`f076c36335`](https://github.com/nodejs/node/commit/f076c36335)] - **tools**: update clang-format 1.6.0 to 1.7.0 (Rich Trott) [#42724](https://github.com/nodejs/node/pull/42724)
* \[[`45162bf9e7`](https://github.com/nodejs/node/commit/45162bf9e7)] - **tools**: update clang-format from 1.2.3 to 1.6.0 (Rich Trott) [#42685](https://github.com/nodejs/node/pull/42685)
* \[[`40bc08089d`](https://github.com/nodejs/node/commit/40bc08089d)] - **tools**: update V8 gypfiles for 10.1 (Michaël Zasso) [#42657](https://github.com/nodejs/node/pull/42657)
* \[[`09513cd1a3`](https://github.com/nodejs/node/commit/09513cd1a3)] - **tools**: update eslint to 8.13.0 (Node.js GitHub Bot) [#42678](https://github.com/nodejs/node/pull/42678)
* \[[`b99bb57416`](https://github.com/nodejs/node/commit/b99bb57416)] - **tools**: update gyp-next to v0.12.1 (Michaël Zasso) [#42625](https://github.com/nodejs/node/pull/42625)
* \[[`2468db1f53`](https://github.com/nodejs/node/commit/2468db1f53)] - **tools**: update lint-md-dependencies to @rollup/plugin-commonjs\@21.0.3 (Node.js GitHub Bot) [#42584](https://github.com/nodejs/node/pull/42584)
* \[[`8a3f28a05c`](https://github.com/nodejs/node/commit/8a3f28a05c)] - **tools**: add v8-embedder-state-scope.h to distributed headers (Michaël Zasso) [#41610](https://github.com/nodejs/node/pull/41610)
* \[[`30c4e1d952`](https://github.com/nodejs/node/commit/30c4e1d952)] - **tools**: update V8 gypfiles for 9.8 (Michaël Zasso) [#41610](https://github.com/nodejs/node/pull/41610)
* \[[`1ad44094a2`](https://github.com/nodejs/node/commit/1ad44094a2)] - **tools**: update V8 gypfiles for 9.7 (Michaël Zasso) [#40907](https://github.com/nodejs/node/pull/40907)
* \[[`86b77f7d0f`](https://github.com/nodejs/node/commit/86b77f7d0f)] - **tools,doc**: use V8::DisposePlatform (Michaël Zasso) [#41610](https://github.com/nodejs/node/pull/41610)
* \[[`62e62757b3`](https://github.com/nodejs/node/commit/62e62757b3)] - **tools,test**: fix V8 initialization order (Camillo Bruni) [#42657](https://github.com/nodejs/node/pull/42657)
* \[[`0187bc5cdc`](https://github.com/nodejs/node/commit/0187bc5cdc)] - **v8**: make v8.writeHeapSnapshot() error codes consistent (Darshan Sen) [#42577](https://github.com/nodejs/node/pull/42577)
* \[[`74b9baa426`](https://github.com/nodejs/node/commit/74b9baa426)] - **v8**: make writeHeapSnapshot throw if fopen fails (Antonio Román) [#41373](https://github.com/nodejs/node/pull/41373)

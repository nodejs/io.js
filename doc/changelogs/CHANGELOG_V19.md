# Node.js 19 ChangeLog

<!--lint disable maximum-line-length no-literal-urls prohibited-strings-->

<table>
<tr>
<th>Current</th>
</tr>
<tr>
<td>
<b><a href="#19.9.0">19.9.0</a></b><br/>
<a href="#19.8.1">19.8.1</a><br/>
<a href="#19.8.0">19.8.0</a><br/>
<a href="#19.7.0">19.7.0</a><br/>
<a href="#19.6.1">19.6.1</a><br/>
<a href="#19.6.0">19.6.0</a><br/>
<a href="#19.5.0">19.5.0</a><br/>
<a href="#19.4.0">19.4.0</a><br/>
<a href="#19.3.0">19.3.0</a><br/>
<a href="#19.2.0">19.2.0</a><br/>
<a href="#19.1.0">19.1.0</a><br/>
<a href="#19.0.1">19.0.1</a><br/>
<a href="#19.0.0">19.0.0</a><br/>
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
  * [0.10.x](CHANGELOG_V010.md)
  * [io.js](CHANGELOG_IOJS.md)
  * [Archive](CHANGELOG_ARCHIVE.md)

<a id="19.9.0"></a>

## 2023-04-10, Version 19.9.0 (Current), @RafaelGSS

### Notable Changes

#### Tracing Channel in diagnostic\_channel

`TracingChannel` adds a new, high-performance channel to publish tracing data about the timing and purpose of function executions.

Contributed by Stephen Belanger in [#44943](https://github.com/nodejs/node/pull/44943)

#### New URL.canParse API

A new API was added to the URL. `URL.canParse` checks if an `input` with an optional base value can be parsed correctly
according to WHATWG URL specification.

```js
const isValid = URL.canParse('/foo', 'https://example.org/'); // true
const isNotValid = URL.canParse('/foo'); // false
```

Contributed by Khafra in [#47179](https://github.com/nodejs/node/pull/47179)

#### Other notable changes

events:

* (SEMVER-MINOR) add getMaxListeners method (Khafra) <https://github.com/nodejs/node/pull/47039>
  msi:
* (SEMVER-MINOR) migrate to WiX4 (Stefan Stojanovic) <https://github.com/nodejs/node/pull/45943>
  node-api:
* (SEMVER-MINOR) deprecate napi\_module\_register (Vladimir Morozov) <https://github.com/nodejs/node/pull/46319>
  stream:
* (SEMVER-MINOR) add setter & getter for default highWaterMark (Robert Nagy) <https://github.com/nodejs/node/pull/46929>
  test\_runner:
* (SEMVER-MINOR) expose reporter for use in run api (Chemi Atlow) <https://github.com/nodejs/node/pull/47238>

### Commits

* \[[`2cea7d8141`](https://github.com/nodejs/node/commit/2cea7d8141)] - **benchmark**: fix invalid requirementsURL (Deokjin Kim) [#47378](https://github.com/nodejs/node/pull/47378)
* \[[`6a4076a188`](https://github.com/nodejs/node/commit/6a4076a188)] - **benchmark**: lower URL.canParse runs (Khafra) [#47351](https://github.com/nodejs/node/pull/47351)
* \[[`23a69d9279`](https://github.com/nodejs/node/commit/23a69d9279)] - **buffer**: fix blob range error with many chunks (Khafra) [#47320](https://github.com/nodejs/node/pull/47320)
* \[[`e3d98c3e7a`](https://github.com/nodejs/node/commit/e3d98c3e7a)] - **buffer**: use private properties for brand checks in File (Khafra) [#47154](https://github.com/nodejs/node/pull/47154)
* \[[`9dc6aef98d`](https://github.com/nodejs/node/commit/9dc6aef98d)] - **build**: bump github/codeql-action from 2.2.6 to 2.2.9 (dependabot\[bot]) [#47366](https://github.com/nodejs/node/pull/47366)
* \[[`910d2967f1`](https://github.com/nodejs/node/commit/910d2967f1)] - **build**: update stale action from v7 to v8 (Rich Trott) [#47357](https://github.com/nodejs/node/pull/47357)
* \[[`666df20ad9`](https://github.com/nodejs/node/commit/666df20ad9)] - **build**: remove Python pip `--no-user` option (Christian Clauss) [#47372](https://github.com/nodejs/node/pull/47372)
* \[[`3970537bb4`](https://github.com/nodejs/node/commit/3970537bb4)] - **build**: avoid usage of pipes library (Mohammed Keyvanzadeh) [#47271](https://github.com/nodejs/node/pull/47271)
* \[[`254a03b2eb`](https://github.com/nodejs/node/commit/254a03b2eb)] - **crypto**: unify validation of checkPrime checks (Tobias Nießen) [#47165](https://github.com/nodejs/node/pull/47165)
* \[[`8e1e9edc57`](https://github.com/nodejs/node/commit/8e1e9edc57)] - **deps**: update timezone to 2023c (Node.js GitHub Bot) [#47302](https://github.com/nodejs/node/pull/47302)
* \[[`30c043c2b9`](https://github.com/nodejs/node/commit/30c043c2b9)] - **deps**: update timezone to 2023b (Node.js GitHub Bot) [#47256](https://github.com/nodejs/node/pull/47256)
* \[[`40be01bc9c`](https://github.com/nodejs/node/commit/40be01bc9c)] - **deps**: update simdutf to 3.2.3 (Node.js GitHub Bot) [#47331](https://github.com/nodejs/node/pull/47331)
* \[[`4b09222569`](https://github.com/nodejs/node/commit/4b09222569)] - **deps**: upgrade npm to 9.6.3 (npm team) [#47325](https://github.com/nodejs/node/pull/47325)
* \[[`2a6c23ea5e`](https://github.com/nodejs/node/commit/2a6c23ea5e)] - **deps**: update corepack to 0.17.1 (Node.js GitHub Bot) [#47156](https://github.com/nodejs/node/pull/47156)
* \[[`06b718363d`](https://github.com/nodejs/node/commit/06b718363d)] - **deps**: V8: cherry-pick 3e4952cb2a59 (Richard Lau) [#47236](https://github.com/nodejs/node/pull/47236)
* \[[`7e24498d81`](https://github.com/nodejs/node/commit/7e24498d81)] - **deps**: upgrade npm to 9.6.2 (npm team) [#47108](https://github.com/nodejs/node/pull/47108)
* \[[`7a4beaa182`](https://github.com/nodejs/node/commit/7a4beaa182)] - **deps**: V8: cherry-pick 215ccd593edb (Joyee Cheung) [#47212](https://github.com/nodejs/node/pull/47212)
* \[[`8a69929f23`](https://github.com/nodejs/node/commit/8a69929f23)] - **deps**: V8: cherry-pick 975ff4dbfd1b (Debadree Chatterjee) [#47209](https://github.com/nodejs/node/pull/47209)
* \[[`10569de53f`](https://github.com/nodejs/node/commit/10569de53f)] - **deps**: cherry-pick win/arm64/clang fixes (Cheng Zhao) [#47011](https://github.com/nodejs/node/pull/47011)
* \[[`ff6070eb1d`](https://github.com/nodejs/node/commit/ff6070eb1d)] - **deps**: V8: cherry-pick cb30b8e17429 (Darshan Sen) [#47307](https://github.com/nodejs/node/pull/47307)
* \[[`0bbce034f9`](https://github.com/nodejs/node/commit/0bbce034f9)] - **doc**: add a note about os.cpus() returning an empty list (codedokode) [#47363](https://github.com/nodejs/node/pull/47363)
* \[[`f8511e0b27`](https://github.com/nodejs/node/commit/f8511e0b27)] - **doc**: clarify reports are only evaluated on active versions (Rafael Gonzaga) [#47341](https://github.com/nodejs/node/pull/47341)
* \[[`863b4d9c5b`](https://github.com/nodejs/node/commit/863b4d9c5b)] - **doc**: remove Vladimir de Turckheim from Security release stewards (Vladimir de Turckheim) [#47318](https://github.com/nodejs/node/pull/47318)
* \[[`2192b5b163`](https://github.com/nodejs/node/commit/2192b5b163)] - **doc**: add importing util to example of \`process.report.getReport' (Deokjin Kim) [#47298](https://github.com/nodejs/node/pull/47298)
* \[[`1c21fbfa9a`](https://github.com/nodejs/node/commit/1c21fbfa9a)] - **doc**: vm.SourceTextModule() without context option (Axel Kittenberger) [#47295](https://github.com/nodejs/node/pull/47295)
* \[[`89445fbea9`](https://github.com/nodejs/node/commit/89445fbea9)] - **doc**: make win arm64 tier 2 platform (Stefan Stojanovic) [#47233](https://github.com/nodejs/node/pull/47233)
* \[[`296577a549`](https://github.com/nodejs/node/commit/296577a549)] - **doc**: document process for sharing project news (Michael Dawson) [#47189](https://github.com/nodejs/node/pull/47189)
* \[[`e29a1462c7`](https://github.com/nodejs/node/commit/e29a1462c7)] - **doc**: revise example of assert.CallTracker (Deokjin Kim) [#47252](https://github.com/nodejs/node/pull/47252)
* \[[`bac893adbe`](https://github.com/nodejs/node/commit/bac893adbe)] - **doc**: fix typo in SECURITY.md (Rich Trott) [#47282](https://github.com/nodejs/node/pull/47282)
* \[[`0949f238d1`](https://github.com/nodejs/node/commit/0949f238d1)] - **doc**: use serial comma in cli docs (Tobias Nießen) [#47262](https://github.com/nodejs/node/pull/47262)
* \[[`71246247a9`](https://github.com/nodejs/node/commit/71246247a9)] - **doc**: improve example for Error.captureStackTrace() (Julian Dax) [#46886](https://github.com/nodejs/node/pull/46886)
* \[[`0b2ba441b2`](https://github.com/nodejs/node/commit/0b2ba441b2)] - **doc**: clarify http error events after calling destroy() (Zach Bjornson) [#46903](https://github.com/nodejs/node/pull/46903)
* \[[`a21459e0d5`](https://github.com/nodejs/node/commit/a21459e0d5)] - **doc**: update output of example in AbortController (Deokjin Kim) [#47227](https://github.com/nodejs/node/pull/47227)
* \[[`7a2090c14c`](https://github.com/nodejs/node/commit/7a2090c14c)] - **doc**: drop one-week branch sync on major releases (Rafael Gonzaga) [#47149](https://github.com/nodejs/node/pull/47149)
* \[[`eb4de0043d`](https://github.com/nodejs/node/commit/eb4de0043d)] - **doc**: fix grammar in the collaborator guide (Mohammed Keyvanzadeh) [#47245](https://github.com/nodejs/node/pull/47245)
* \[[`908798ae19`](https://github.com/nodejs/node/commit/908798ae19)] - **doc**: update stream.reduce concurrency note (Raz Luvaton) [#47166](https://github.com/nodejs/node/pull/47166)
* \[[`36c118bc92`](https://github.com/nodejs/node/commit/36c118bc92)] - **doc**: remove use of DEFAULT\_ENCODING in PBKDF2 docs (Tobias Nießen) [#47181](https://github.com/nodejs/node/pull/47181)
* \[[`7ec87fd5ce`](https://github.com/nodejs/node/commit/7ec87fd5ce)] - **doc**: fix typos in async\_context.md (Shubham Sharma) [#47155](https://github.com/nodejs/node/pull/47155)
* \[[`a03aaba996`](https://github.com/nodejs/node/commit/a03aaba996)] - **doc**: update collaborator guide to reflect TSC changes (Rich Trott) [#47126](https://github.com/nodejs/node/pull/47126)
* \[[`c45a6977ec`](https://github.com/nodejs/node/commit/c45a6977ec)] - **doc**: clarify that `fs.create{Read,Write}Stream` support `AbortSignal` (Antoine du Hamel) [#47122](https://github.com/nodejs/node/pull/47122)
* \[[`82c7757177`](https://github.com/nodejs/node/commit/82c7757177)] - **doc**: improve documentation for util.types.isNativeError() (Julian Dax) [#46840](https://github.com/nodejs/node/pull/46840)
* \[[`8f9b9c17d5`](https://github.com/nodejs/node/commit/8f9b9c17d5)] - **doc**: rename the startup performance initiative to startup snapshot (#47111) (Joyee Cheung)
* \[[`c08995e897`](https://github.com/nodejs/node/commit/c08995e897)] - **doc**: indicate that `name` is no longer an optional argument (Daniel Roe) [#47102](https://github.com/nodejs/node/pull/47102)
* \[[`316d626e61`](https://github.com/nodejs/node/commit/316d626e61)] - **doc**: fix "maintaining dependencies" heading typos (Keyhan Vakil) [#47082](https://github.com/nodejs/node/pull/47082)
* \[[`a4b1a7761f`](https://github.com/nodejs/node/commit/a4b1a7761f)] - **esm**: skip file: URL conversion to path when possible (Antoine du Hamel) [#46305](https://github.com/nodejs/node/pull/46305)
* \[[`c5cd6b7f3b`](https://github.com/nodejs/node/commit/c5cd6b7f3b)] - **(SEMVER-MINOR)** **events**: add getMaxListeners method (Khafra) [#47039](https://github.com/nodejs/node/pull/47039)
* \[[`2c2b07ce5f`](https://github.com/nodejs/node/commit/2c2b07ce5f)] - **fs**: invalidate blob created from empty file when written to (Debadree Chatterjee) [#47199](https://github.com/nodejs/node/pull/47199)
* \[[`e33dfce401`](https://github.com/nodejs/node/commit/e33dfce401)] - **inspector**: log response and requests in the inspector for debugging (Joyee Cheung) [#46941](https://github.com/nodejs/node/pull/46941)
* \[[`f6ec81dc05`](https://github.com/nodejs/node/commit/f6ec81dc05)] - **inspector**: fix session.disconnect crash (theanarkh) [#46942](https://github.com/nodejs/node/pull/46942)
* \[[`a738164fed`](https://github.com/nodejs/node/commit/a738164fed)] - **lib**: define Event.isTrusted in the prototype (Santiago Gimeno) [#46974](https://github.com/nodejs/node/pull/46974)
* \[[`7d37dcdd9a`](https://github.com/nodejs/node/commit/7d37dcdd9a)] - **(SEMVER-MINOR)** **lib**: add tracing channel to diagnostics\_channel (Stephen Belanger) [#44943](https://github.com/nodejs/node/pull/44943)
* \[[`16d3dfa0aa`](https://github.com/nodejs/node/commit/16d3dfa0aa)] - **meta**: fix notable-change comment label url (Filip Skokan) [#47300](https://github.com/nodejs/node/pull/47300)
* \[[`2c95f6e18b`](https://github.com/nodejs/node/commit/2c95f6e18b)] - **meta**: clarify the threat model to explain the JSON.parse case (Matteo Collina) [#47276](https://github.com/nodejs/node/pull/47276)
* \[[`22b9acdbf8`](https://github.com/nodejs/node/commit/22b9acdbf8)] - **meta**: update link to collaborators discussion page (Michaël Zasso) [#47211](https://github.com/nodejs/node/pull/47211)
* \[[`dc024d930a`](https://github.com/nodejs/node/commit/dc024d930a)] - **meta**: automate description requests when notable change label is added (Danielle Adams) [#47078](https://github.com/nodejs/node/pull/47078)
* \[[`54195357f3`](https://github.com/nodejs/node/commit/54195357f3)] - **meta**: move TSC voting member(s) to regular member(s) (Node.js GitHub Bot) [#47180](https://github.com/nodejs/node/pull/47180)
* \[[`a3bffbaa11`](https://github.com/nodejs/node/commit/a3bffbaa11)] - **meta**: move TSC voting member to regular membership (Node.js GitHub Bot) [#46985](https://github.com/nodejs/node/pull/46985)
* \[[`d2a6aa6ecd`](https://github.com/nodejs/node/commit/d2a6aa6ecd)] - **meta**: update GOVERNANCE.md to reflect TSC charter changes (Rich Trott) [#47126](https://github.com/nodejs/node/pull/47126)
* \[[`b0aad345bf`](https://github.com/nodejs/node/commit/b0aad345bf)] - **meta**: ask expected behavior reason in bug template (Ben Noordhuis) [#47049](https://github.com/nodejs/node/pull/47049)
* \[[`c03e79b141`](https://github.com/nodejs/node/commit/c03e79b141)] - **(SEMVER-MINOR)** **msi**: migrate to WiX4 (Stefan Stojanovic) [#45943](https://github.com/nodejs/node/pull/45943)
* \[[`ca981be2b9`](https://github.com/nodejs/node/commit/ca981be2b9)] - **(SEMVER-MINOR)** **node-api**: deprecate napi\_module\_register (Vladimir Morozov) [#46319](https://github.com/nodejs/node/pull/46319)
* \[[`77f7200cce`](https://github.com/nodejs/node/commit/77f7200cce)] - **node-api**: extend type-tagging to externals (Gabriel Schulhof) [#47141](https://github.com/nodejs/node/pull/47141)
* \[[`55f3d215b8`](https://github.com/nodejs/node/commit/55f3d215b8)] - **node-api**: document node-api shutdown finalization (Chengzhong Wu) [#45903](https://github.com/nodejs/node/pull/45903)
* \[[`b3fe2ba59b`](https://github.com/nodejs/node/commit/b3fe2ba59b)] - **node-api**: verify cleanup hooks order (Chengzhong Wu) [#46692](https://github.com/nodejs/node/pull/46692)
* \[[`d6a12328a6`](https://github.com/nodejs/node/commit/d6a12328a6)] - **repl**: preserve preview on ESCAPE key press (Xuguang Mei) [#46878](https://github.com/nodejs/node/pull/46878)
* \[[`33b0906640`](https://github.com/nodejs/node/commit/33b0906640)] - **sea**: fix memory leak detected by asan (Darshan Sen) [#47309](https://github.com/nodejs/node/pull/47309)
* \[[`069515153f`](https://github.com/nodejs/node/commit/069515153f)] - **src**: remove usage of `std::shared_ptr<T>::unique()` (Darshan Sen) [#47315](https://github.com/nodejs/node/pull/47315)
* \[[`4405fc879a`](https://github.com/nodejs/node/commit/4405fc879a)] - **src**: use stricter compile-time guidance (Tobias Nießen) [#46509](https://github.com/nodejs/node/pull/46509)
* \[[`bbde68e5de`](https://github.com/nodejs/node/commit/bbde68e5de)] - **src**: remove unused variable in crypto\_x509.cc (Michaël Zasso) [#47344](https://github.com/nodejs/node/pull/47344)
* \[[`7a80312e19`](https://github.com/nodejs/node/commit/7a80312e19)] - **src**: don't reset embeder signal handlers (Dmitry Vyukov) [#47188](https://github.com/nodejs/node/pull/47188)
* \[[`d0a5e7e342`](https://github.com/nodejs/node/commit/d0a5e7e342)] - **src**: fix some recently introduced coverity issues (Michael Dawson) [#47240](https://github.com/nodejs/node/pull/47240)
* \[[`0a4ff2f9a0`](https://github.com/nodejs/node/commit/0a4ff2f9a0)] - **src**: replace impossible THROW with CHECK (Tobias Nießen) [#47168](https://github.com/nodejs/node/pull/47168)
* \[[`2fd0f79963`](https://github.com/nodejs/node/commit/2fd0f79963)] - **src**: fix duplication of externalized builtin code (Keyhan Vakil) [#47079](https://github.com/nodejs/node/pull/47079)
* \[[`36a026bf44`](https://github.com/nodejs/node/commit/36a026bf44)] - **src**: remove dead comments about return\_code\_cache (Keyhan Vakil) [#47083](https://github.com/nodejs/node/pull/47083)
* \[[`aefe26692c`](https://github.com/nodejs/node/commit/aefe26692c)] - **src**: remove SSL\_CTX\_get\_tlsext\_ticket\_keys guards (Tobias Nießen) [#47068](https://github.com/nodejs/node/pull/47068)
* \[[`90f4e16350`](https://github.com/nodejs/node/commit/90f4e16350)] - **src**: fix clang 14 linker error (Keyhan Vakil) [#47057](https://github.com/nodejs/node/pull/47057)
* \[[`b0809a73da`](https://github.com/nodejs/node/commit/b0809a73da)] - **src,http2**: ensure cleanup if a frame is not sent (ywave620) [#47244](https://github.com/nodejs/node/pull/47244)
* \[[`1fc62c7b35`](https://github.com/nodejs/node/commit/1fc62c7b35)] - **(SEMVER-MINOR)** **stream**: add setter & getter for default highWaterMark (#46929) (Robert Nagy) [#46929](https://github.com/nodejs/node/pull/46929)
* \[[`b8c6ceddd5`](https://github.com/nodejs/node/commit/b8c6ceddd5)] - **stream**: expose stream symbols (Robert Nagy) [#45671](https://github.com/nodejs/node/pull/45671)
* \[[`f37825660c`](https://github.com/nodejs/node/commit/f37825660c)] - **stream**: dont wait for next item in take when finished (Raz Luvaton) [#47132](https://github.com/nodejs/node/pull/47132)
* \[[`8eceaaeb4d`](https://github.com/nodejs/node/commit/8eceaaeb4d)] - **test**: fix flaky test-watch-mode-inspect (Moshe Atlow) [#47403](https://github.com/nodejs/node/pull/47403)
* \[[`db95ed0b1b`](https://github.com/nodejs/node/commit/db95ed0b1b)] - **test**: move debugger tests with --port=0 to parallel (Joyee Cheung) [#47274](https://github.com/nodejs/node/pull/47274)
* \[[`041885ebd0`](https://github.com/nodejs/node/commit/041885ebd0)] - **test**: use --port=0 in debugger tests that do not have to work on 9229 (Joyee Cheung) [#47274](https://github.com/nodejs/node/pull/47274)
* \[[`130420b9e1`](https://github.com/nodejs/node/commit/130420b9e1)] - **test**: run doctool tests in parallel (Joyee Cheung) [#47273](https://github.com/nodejs/node/pull/47273)
* \[[`4b4336c34e`](https://github.com/nodejs/node/commit/4b4336c34e)] - **test**: verify tracePromise does not do runStores (Stephen Belanger) [#47349](https://github.com/nodejs/node/pull/47349)
* \[[`54261f3294`](https://github.com/nodejs/node/commit/54261f3294)] - **test**: run WPT files in parallel again (Filip Skokan) [#47283](https://github.com/nodejs/node/pull/47283)
* \[[`e2eb0543be`](https://github.com/nodejs/node/commit/e2eb0543be)] - **test**: update wasm/jsapi WPT (Michaël Zasso) [#47210](https://github.com/nodejs/node/pull/47210)
* \[[`d341d0389f`](https://github.com/nodejs/node/commit/d341d0389f)] - **test**: skip test-wasm-web-api on ARM (Michaël Zasso) [#47299](https://github.com/nodejs/node/pull/47299)
* \[[`567573b16a`](https://github.com/nodejs/node/commit/567573b16a)] - **test**: skip instantiateStreaming-bad-imports WPT (Michaël Zasso) [#47292](https://github.com/nodejs/node/pull/47292)
* \[[`45e7b10287`](https://github.com/nodejs/node/commit/45e7b10287)] - **test**: fix 'checks' validation test for checkPrime (Tobias Nießen) [#47139](https://github.com/nodejs/node/pull/47139)
* \[[`5749dfae70`](https://github.com/nodejs/node/commit/5749dfae70)] - **test**: update URL web-platform-tests (Yagiz Nizipli) [#47135](https://github.com/nodejs/node/pull/47135)
* \[[`49981b93d2`](https://github.com/nodejs/node/commit/49981b93d2)] - **test**: reduce flakiness of test-http-remove-header-stays-removed.js (Debadree Chatterjee) [#46855](https://github.com/nodejs/node/pull/46855)
* \[[`6772aa652a`](https://github.com/nodejs/node/commit/6772aa652a)] - **test**: fix test-child-process-exec-cwd (Stefan Stojanovic) [#47235](https://github.com/nodejs/node/pull/47235)
* \[[`41a69e772b`](https://github.com/nodejs/node/commit/41a69e772b)] - **test**: skip broken tests win arm64 (Stefan Stojanovic) [#47020](https://github.com/nodejs/node/pull/47020)
* \[[`7bcfd18f2c`](https://github.com/nodejs/node/commit/7bcfd18f2c)] - **test**: mark test-http-max-sockets as flaky on win32 (Tobias Nießen) [#47134](https://github.com/nodejs/node/pull/47134)
* \[[`b96808b3e2`](https://github.com/nodejs/node/commit/b96808b3e2)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#47222](https://github.com/nodejs/node/pull/47222)
* \[[`65955f1e46`](https://github.com/nodejs/node/commit/65955f1e46)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#47131](https://github.com/nodejs/node/pull/47131)
* \[[`bc6511a243`](https://github.com/nodejs/node/commit/bc6511a243)] - **test\_runner**: color errors only when colors are available (Moshe Atlow) [#47394](https://github.com/nodejs/node/pull/47394)
* \[[`463361e625`](https://github.com/nodejs/node/commit/463361e625)] - **test\_runner**: hide failing tests title when all tests pass (Moshe Atlow) [#47370](https://github.com/nodejs/node/pull/47370)
* \[[`eb837ce80d`](https://github.com/nodejs/node/commit/eb837ce80d)] - **test\_runner**: stringify AssertError expected and actual (Moshe Atlow) [#47088](https://github.com/nodejs/node/pull/47088)
* \[[`6b87f29000`](https://github.com/nodejs/node/commit/6b87f29000)] - **test\_runner**: add code coverage support to spec reporter (Pulkit Gupta) [#46674](https://github.com/nodejs/node/pull/46674)
* \[[`bd4697a2a3`](https://github.com/nodejs/node/commit/bd4697a2a3)] - **test\_runner**: expose reporter for use in run api (Chemi Atlow) [#47238](https://github.com/nodejs/node/pull/47238)
* \[[`3e7f8e8482`](https://github.com/nodejs/node/commit/3e7f8e8482)] - **test\_runner**: report failing tests after summary (HinataKah0) [#47164](https://github.com/nodejs/node/pull/47164)
* \[[`4530582767`](https://github.com/nodejs/node/commit/4530582767)] - **test\_runner**: count nested tests (Moshe Atlow) [#47094](https://github.com/nodejs/node/pull/47094)
* \[[`5a43586554`](https://github.com/nodejs/node/commit/5a43586554)] - **test\_runner**: accept \x1b as a escape symbol (Debadree Chatterjee) [#47050](https://github.com/nodejs/node/pull/47050)
* \[[`a5ebc896f1`](https://github.com/nodejs/node/commit/a5ebc896f1)] - **test\_runner**: support defining test reporter in NODE\_OPTIONS (Steve Herzog) [#46688](https://github.com/nodejs/node/pull/46688)
* \[[`a65fe5c29a`](https://github.com/nodejs/node/commit/a65fe5c29a)] - **tools**: fix update-openssl.yml compare version (Marco Ippolito) [#47384](https://github.com/nodejs/node/pull/47384)
* \[[`760e13c58d`](https://github.com/nodejs/node/commit/760e13c58d)] - **tools**: ensure failed daily wpt run still generates a report (Filip Skokan) [#47376](https://github.com/nodejs/node/pull/47376)
* \[[`9c975f79f0`](https://github.com/nodejs/node/commit/9c975f79f0)] - **tools**: use ref\_name to get branch pushed on (Debadree Chatterjee) [#47358](https://github.com/nodejs/node/pull/47358)
* \[[`b1d6a15028`](https://github.com/nodejs/node/commit/b1d6a15028)] - **tools**: add a at here tag for slack messages (Debadree Chatterjee) [#47358](https://github.com/nodejs/node/pull/47358)
* \[[`c340de6d51`](https://github.com/nodejs/node/commit/c340de6d51)] - **tools**: disable Codecov commit statuses (Michaël Zasso) [#47306](https://github.com/nodejs/node/pull/47306)
* \[[`034082f0e5`](https://github.com/nodejs/node/commit/034082f0e5)] - **tools**: update eslint to 8.37.0 (Node.js GitHub Bot) [#47333](https://github.com/nodejs/node/pull/47333)
* \[[`03b6650c81`](https://github.com/nodejs/node/commit/03b6650c81)] - **tools**: fix duration\_ms to be milliseconds (Moshe Atlow) [#44490](https://github.com/nodejs/node/pull/44490)
* \[[`30c667ec3a`](https://github.com/nodejs/node/commit/30c667ec3a)] - **tools**: automate brotli update (Marco Ippolito) [#47205](https://github.com/nodejs/node/pull/47205)
* \[[`83791e5459`](https://github.com/nodejs/node/commit/83791e5459)] - **tools**: fix typo in nghttp2 path (Marco Ippolito) [#47330](https://github.com/nodejs/node/pull/47330)
* \[[`53e8dad64a`](https://github.com/nodejs/node/commit/53e8dad64a)] - **tools**: add scorecard workflow (Mateo Nunez) [#47254](https://github.com/nodejs/node/pull/47254)
* \[[`2499677d0b`](https://github.com/nodejs/node/commit/2499677d0b)] - **tools**: pin actions by hash for auto-start-ci.yml (Gabriela Gutierrez) [#46820](https://github.com/nodejs/node/pull/46820)
* \[[`98f64ee724`](https://github.com/nodejs/node/commit/98f64ee724)] - **tools**: standardize base64 update (Marco Ippolito) [#47201](https://github.com/nodejs/node/pull/47201)
* \[[`c1ef1fde8f`](https://github.com/nodejs/node/commit/c1ef1fde8f)] - **tools**: update codecov branch (Rich Trott) [#47285](https://github.com/nodejs/node/pull/47285)
* \[[`9ecf2a4144`](https://github.com/nodejs/node/commit/9ecf2a4144)] - **tools**: update lint-md-dependencies to rollup\@3.20.2 (Node.js GitHub Bot) [#47255](https://github.com/nodejs/node/pull/47255)
* \[[`def7e3d908`](https://github.com/nodejs/node/commit/def7e3d908)] - **tools**: upgrade Windows digital signature to SHA256 (Tobias Nießen) [#47206](https://github.com/nodejs/node/pull/47206)
* \[[`0b78ac53ad`](https://github.com/nodejs/node/commit/0b78ac53ad)] - **tools**: standardize update-llhttp.sh (Marco Ippolito) [#47198](https://github.com/nodejs/node/pull/47198)
* \[[`deb80b1c46`](https://github.com/nodejs/node/commit/deb80b1c46)] - **tools**: add button to copy code example to clipboard (jakecastelli) [#46928](https://github.com/nodejs/node/pull/46928)
* \[[`6dca79f1ce`](https://github.com/nodejs/node/commit/6dca79f1ce)] - **tools**: standardize update-nghttp2.sh (Marco Ippolito) [#47197](https://github.com/nodejs/node/pull/47197)
* \[[`0c613c9347`](https://github.com/nodejs/node/commit/0c613c9347)] - **tools**: fix Slack notification action (Antoine du Hamel) [#47237](https://github.com/nodejs/node/pull/47237)
* \[[`3f49da5113`](https://github.com/nodejs/node/commit/3f49da5113)] - **tools**: notify on Slack when invalid commit lands (Antoine du Hamel) [#47178](https://github.com/nodejs/node/pull/47178)
* \[[`337123d657`](https://github.com/nodejs/node/commit/337123d657)] - **tools**: update daily wpt actions summary (Filip Skokan) [#47138](https://github.com/nodejs/node/pull/47138)
* \[[`78ce8d3469`](https://github.com/nodejs/node/commit/78ce8d3469)] - **tools**: allow test tap output to include unicode characters (Moshe Atlow) [#47175](https://github.com/nodejs/node/pull/47175)
* \[[`8850dacc88`](https://github.com/nodejs/node/commit/8850dacc88)] - **tools**: update lint-md-dependencies to rollup\@3.19.1 (Node.js GitHub Bot) [#47045](https://github.com/nodejs/node/pull/47045)
* \[[`d1ca5b6d47`](https://github.com/nodejs/node/commit/d1ca5b6d47)] - **tools**: align update-ada.sh with other scripts (Tony Gorez) [#47044](https://github.com/nodejs/node/pull/47044)
* \[[`b58d52301e`](https://github.com/nodejs/node/commit/b58d52301e)] - **tools**: update eslint to 8.36.0 (Node.js GitHub Bot) [#47046](https://github.com/nodejs/node/pull/47046)
* \[[`d78bef8a1f`](https://github.com/nodejs/node/commit/d78bef8a1f)] - **tools,meta**: update README and tools to reflect changes in TSC charter (Rich Trott) [#47126](https://github.com/nodejs/node/pull/47126)
* \[[`d243115f41`](https://github.com/nodejs/node/commit/d243115f41)] - **url**: improve URLSearchParams creation performance (Yagiz Nizipli) [#47190](https://github.com/nodejs/node/pull/47190)
* \[[`461ef04f87`](https://github.com/nodejs/node/commit/461ef04f87)] - **url**: add pending-deprecation to `url.parse()` (Yagiz Nizipli) [#47203](https://github.com/nodejs/node/pull/47203)
* \[[`ef62e5a59e`](https://github.com/nodejs/node/commit/ef62e5a59e)] - **(SEMVER-MINOR)** **url**: implement URL.canParse (Khafra) [#47179](https://github.com/nodejs/node/pull/47179)
* \[[`0b565e8f62`](https://github.com/nodejs/node/commit/0b565e8f62)] - **url**: allow extension of user provided URL objects (Antoine du Hamel) [#46989](https://github.com/nodejs/node/pull/46989)
* \[[`cbb362736b`](https://github.com/nodejs/node/commit/cbb362736b)] - **util**: fix inspecting error with a throwing getter for `cause` (Antoine du Hamel) [#47163](https://github.com/nodejs/node/pull/47163)
* \[[`9537672511`](https://github.com/nodejs/node/commit/9537672511)] - **vm**: properly handle defining props on any value (Nicolas DUBIEN) [#46615](https://github.com/nodejs/node/pull/46615)
* \[[`75669e98bf`](https://github.com/nodejs/node/commit/75669e98bf)] - **watch**: fix watch path with equals (Moshe Atlow) [#47369](https://github.com/nodejs/node/pull/47369)

<a id="19.8.1"></a>

## 2023-03-15, Version 19.8.1 (Current), @targos

### Notable Changes

This release contains a single revert of a change that was introduced in v19.8.0
and introduced application crashes.

Fixes: <https://github.com/nodejs/node/issues/47096>

### Commits

* \[[`f7c8aa4cf1`](https://github.com/nodejs/node/commit/f7c8aa4cf1)] - _**Revert**_ "**vm**: fix leak in vm.compileFunction when importModuleDynamically is used" (Michaël Zasso) [#47101](https://github.com/nodejs/node/pull/47101)

<a id="19.8.0"></a>

## 2023-03-14, Version 19.8.0 (Current), @targos

### Notable Changes

* \[[`2fece54ca1`](https://github.com/nodejs/node/commit/2fece54ca1)] - **(SEMVER-MINOR)** **buffer**: add `Buffer.copyBytesFrom(...)` (James M Snell) [#46500](https://github.com/nodejs/node/pull/46500)
* \[[`2eb887549a`](https://github.com/nodejs/node/commit/2eb887549a)] - **(SEMVER-MINOR)** **events**: add `listener` argument to `listenerCount` (Paolo Insogna) [#46523](https://github.com/nodejs/node/pull/46523)
* \[[`c1651bea41`](https://github.com/nodejs/node/commit/c1651bea41)] - **(SEMVER-MINOR)** **lib**: add `AsyncLocalStorage.bind()` and `.snapshot()` (flakey5) [#46387](https://github.com/nodejs/node/pull/46387)
* \[[`36f36b99b0`](https://github.com/nodejs/node/commit/36f36b99b0)] - **(SEMVER-MINOR)** **src**: add `fs.openAsBlob` to support File-backed Blobs (James M Snell) [#45258](https://github.com/nodejs/node/pull/45258)
* \[[`bb9b1c637d`](https://github.com/nodejs/node/commit/bb9b1c637d)] - **(SEMVER-MINOR)** **tls**: support automatic DHE (Tobias Nießen) [#46978](https://github.com/nodejs/node/pull/46978)
* \[[`1e20b05acd`](https://github.com/nodejs/node/commit/1e20b05acd)] - **(SEMVER-MINOR)** **url**: implement `URLSearchParams` `size` getter (James M Snell) [#46308](https://github.com/nodejs/node/pull/46308)
* \[[`60e5f45141`](https://github.com/nodejs/node/commit/60e5f45141)] - **(SEMVER-MINOR)** **wasi**: add support for version when creating WASI (Michael Dawson) [#46469](https://github.com/nodejs/node/pull/46469)
* \[[`a646a22d0f`](https://github.com/nodejs/node/commit/a646a22d0f)] - **(SEMVER-MINOR)** **worker**: add support for worker name in inspector and trace\_events (Debadree Chatterjee) [#46832](https://github.com/nodejs/node/pull/46832)
* \[[`bd5ef380a5`](https://github.com/nodejs/node/commit/bd5ef380a5)] - **doc**: add marco-ippolito to collaborators (Marco Ippolito) [#46816](https://github.com/nodejs/node/pull/46816)

### Commits

* \[[`e11f08e2c2`](https://github.com/nodejs/node/commit/e11f08e2c2)] - **assert**: fix exception message for assert(0) on try catch block (hidecology) [#46760](https://github.com/nodejs/node/pull/46760)
* \[[`a38de61e87`](https://github.com/nodejs/node/commit/a38de61e87)] - **assert**: remove deprecated getFunction() usage (Ruben Bridgewater) [#46661](https://github.com/nodejs/node/pull/46661)
* \[[`e07c9b82b7`](https://github.com/nodejs/node/commit/e07c9b82b7)] - **assert,util**: revert recursive breaking change (Ruben Bridgewater) [#46593](https://github.com/nodejs/node/pull/46593)
* \[[`7f85a2cb6f`](https://github.com/nodejs/node/commit/7f85a2cb6f)] - **assert,util**: improve deep equal comparison performance (Ruben Bridgewater) [#46593](https://github.com/nodejs/node/pull/46593)
* \[[`7cfd31a753`](https://github.com/nodejs/node/commit/7cfd31a753)] - **benchmark**: add a benchmark for URLSearchParams creation and toString() (Debadree Chatterjee) [#46810](https://github.com/nodejs/node/pull/46810)
* \[[`258d5f7b3c`](https://github.com/nodejs/node/commit/258d5f7b3c)] - **benchmark**: replace table in docs with description of file tree structure (Theodor Steiner) [#46991](https://github.com/nodejs/node/pull/46991)
* \[[`0617c5e81b`](https://github.com/nodejs/node/commit/0617c5e81b)] - **benchmark**: stablize encode benchmark (Joyee Cheung) [#46658](https://github.com/nodejs/node/pull/46658)
* \[[`04166fe2fa`](https://github.com/nodejs/node/commit/04166fe2fa)] - **benchmark**: split `Buffer.byteLength` benchmark (Joyee Cheung) [#46616](https://github.com/nodejs/node/pull/46616)
* \[[`760a35144f`](https://github.com/nodejs/node/commit/760a35144f)] - **benchmark**: add benchmark for EventTarget add and remove (Debadree Chatterjee) [#46779](https://github.com/nodejs/node/pull/46779)
* \[[`9890eaa23d`](https://github.com/nodejs/node/commit/9890eaa23d)] - **benchmark**: fix worker startup benchmark (Joyee Cheung) [#46680](https://github.com/nodejs/node/pull/46680)
* \[[`86b36212f6`](https://github.com/nodejs/node/commit/86b36212f6)] - **benchmark**: rework assert benchmarks for correctness (Ruben Bridgewater) [#46593](https://github.com/nodejs/node/pull/46593)
* \[[`cc74821477`](https://github.com/nodejs/node/commit/cc74821477)] - **bootstrap**: print stack trace during environment creation failure (Joyee Cheung) [#46533](https://github.com/nodejs/node/pull/46533)
* \[[`2fece54ca1`](https://github.com/nodejs/node/commit/2fece54ca1)] - **(SEMVER-MINOR)** **buffer**: add Buffer.copyBytesFrom(...) (James M Snell) [#46500](https://github.com/nodejs/node/pull/46500)
* \[[`b3e1034660`](https://github.com/nodejs/node/commit/b3e1034660)] - **buffer**: use v8 fast API calls for `Buffer.byteLength` implementation (Joyee Cheung) [#46616](https://github.com/nodejs/node/pull/46616)
* \[[`4b3b009afd`](https://github.com/nodejs/node/commit/4b3b009afd)] - **build**: fix Visual Studio installation detection for Arm64 (Radek Bartoň) [#46420](https://github.com/nodejs/node/pull/46420)
* \[[`d4899b2b75`](https://github.com/nodejs/node/commit/d4899b2b75)] - **build,test**: add proper support for IBM i (Xu Meng) [#46739](https://github.com/nodejs/node/pull/46739)
* \[[`81592ff073`](https://github.com/nodejs/node/commit/81592ff073)] - **child\_process**: add trailing commas in source files (Antoine du Hamel) [#46758](https://github.com/nodejs/node/pull/46758)
* \[[`16bbbacba8`](https://github.com/nodejs/node/commit/16bbbacba8)] - **cluster**: add trailing commas in source files (Antoine du Hamel) [#46695](https://github.com/nodejs/node/pull/46695)
* \[[`2b7eb56e9b`](https://github.com/nodejs/node/commit/2b7eb56e9b)] - **debugger**: improve validations and documents for watch and unwatch (Eungyu Lee) [#46947](https://github.com/nodejs/node/pull/46947)
* \[[`afbd818669`](https://github.com/nodejs/node/commit/afbd818669)] - **debugger**: add a command to set which lines to check for context (Eungyu Lee) [#46812](https://github.com/nodejs/node/pull/46812)
* \[[`83b529ff27`](https://github.com/nodejs/node/commit/83b529ff27)] - **debugger**: add trailing commas in source files (Antoine du Hamel) [#46714](https://github.com/nodejs/node/pull/46714)
* \[[`84f5a1f942`](https://github.com/nodejs/node/commit/84f5a1f942)] - **deps**: update undici to 5.21.0 (Node.js GitHub Bot) [#47063](https://github.com/nodejs/node/pull/47063)
* \[[`fb1ac98900`](https://github.com/nodejs/node/commit/fb1ac98900)] - **deps**: update simdutf to 3.2.2 (Node.js GitHub Bot) [#46841](https://github.com/nodejs/node/pull/46841)
* \[[`7ab7f97c4e`](https://github.com/nodejs/node/commit/7ab7f97c4e)] - **deps**: update uvwasi to v0.0.16 (Michael Dawson) [#46434](https://github.com/nodejs/node/pull/46434)
* \[[`b825e2db65`](https://github.com/nodejs/node/commit/b825e2db65)] - **deps**: update ada to 1.0.4 (Node.js GitHub Bot) [#46853](https://github.com/nodejs/node/pull/46853)
* \[[`8b1afe3f45`](https://github.com/nodejs/node/commit/8b1afe3f45)] - **deps**: update corepack to 0.17.0 (Node.js GitHub Bot) [#46842](https://github.com/nodejs/node/pull/46842)
* \[[`151fb60b28`](https://github.com/nodejs/node/commit/151fb60b28)] - **deps**: update simdutf to 3.2.1 (Node.js GitHub Bot) [#46800](https://github.com/nodejs/node/pull/46800)
* \[[`92f2f1910e`](https://github.com/nodejs/node/commit/92f2f1910e)] - **deps**: upgrade npm to 9.5.1 (npm team) [#46783](https://github.com/nodejs/node/pull/46783)
* \[[`4e18e0a43a`](https://github.com/nodejs/node/commit/4e18e0a43a)] - **deps**: update ada to 1.0.3 (Node.js GitHub Bot) [#46784](https://github.com/nodejs/node/pull/46784)
* \[[`68dde38c8e`](https://github.com/nodejs/node/commit/68dde38c8e)] - **deps**: update nghttp2 to 1.52.0 (Michaël Zasso) [#46636](https://github.com/nodejs/node/pull/46636)
* \[[`d9069e7614`](https://github.com/nodejs/node/commit/d9069e7614)] - **deps**: fix libuv for android (Julian Dropmann) [#46746](https://github.com/nodejs/node/pull/46746)
* \[[`c786ed3ecc`](https://github.com/nodejs/node/commit/c786ed3ecc)] - **deps**: V8: cherry-pick 90be99fab31c (Michaël Zasso) [#46646](https://github.com/nodejs/node/pull/46646)
* \[[`fb146ee741`](https://github.com/nodejs/node/commit/fb146ee741)] - **deps**: update simdutf to 3.2.0 (Node.js GitHub Bot) [#46621](https://github.com/nodejs/node/pull/46621)
* \[[`adff278c47`](https://github.com/nodejs/node/commit/adff278c47)] - **deps,test**: update postject to 1.0.0-alpha.5 (Node.js GitHub Bot) [#46934](https://github.com/nodejs/node/pull/46934)
* \[[`247dfb7d73`](https://github.com/nodejs/node/commit/247dfb7d73)] - **dgram**: fix unhandled exception aborting a closed udp socket (Ramana Venkata) [#46770](https://github.com/nodejs/node/pull/46770)
* \[[`c310a32857`](https://github.com/nodejs/node/commit/c310a32857)] - **doc**: remove remaining SSL\_OP\_NETSCAPE\_\*\_BUG (Tobias Nießen) [#47066](https://github.com/nodejs/node/pull/47066)
* \[[`89f31a1c7f`](https://github.com/nodejs/node/commit/89f31a1c7f)] - **doc**: fix typo in test.md (Victor Hiairrassary) [#47053](https://github.com/nodejs/node/pull/47053)
* \[[`94882f579f`](https://github.com/nodejs/node/commit/94882f579f)] - **doc**: amend support tier qualifier (Gireesh Punathil) [#42805](https://github.com/nodejs/node/pull/42805)
* \[[`cbdaaf6197`](https://github.com/nodejs/node/commit/cbdaaf6197)] - **doc**: fix typo on esm loaders example (Ruy Adorno) [#47015](https://github.com/nodejs/node/pull/47015)
* \[[`17d3eb02f7`](https://github.com/nodejs/node/commit/17d3eb02f7)] - **doc**: add missing test runner flags to man page (Colin Ihrig) [#46982](https://github.com/nodejs/node/pull/46982)
* \[[`5f0f1c4197`](https://github.com/nodejs/node/commit/5f0f1c4197)] - **doc**: fix history information for `node:diagnostics_channel` (Thomas Hunter II) [#46984](https://github.com/nodejs/node/pull/46984)
* \[[`67e20f53cd`](https://github.com/nodejs/node/commit/67e20f53cd)] - **doc**: fix myUrl is not defined in url (Youngmin Yoo) [#46968](https://github.com/nodejs/node/pull/46968)
* \[[`f903ea502c`](https://github.com/nodejs/node/commit/f903ea502c)] - **doc**: remove useless SSL\_OP\_\* options (Tobias Nießen) [#46954](https://github.com/nodejs/node/pull/46954)
* \[[`5fdd3f454f`](https://github.com/nodejs/node/commit/5fdd3f454f)] - **doc**: fix description of TLS dhparam option (Tobias Nießen) [#46949](https://github.com/nodejs/node/pull/46949)
* \[[`ba5ff15b38`](https://github.com/nodejs/node/commit/ba5ff15b38)] - **doc**: improve fs code example quality (jakecastelli) [#46948](https://github.com/nodejs/node/pull/46948)
* \[[`6f18b947be`](https://github.com/nodejs/node/commit/6f18b947be)] - **doc**: fix port of destination server is not defined in http2 (Deokjin Kim) [#46940](https://github.com/nodejs/node/pull/46940)
* \[[`1b555ae72d`](https://github.com/nodejs/node/commit/1b555ae72d)] - **doc**: use number which is bigger than 1024 as port in http2 (Deokjin Kim) [#46938](https://github.com/nodejs/node/pull/46938)
* \[[`07036cf1af`](https://github.com/nodejs/node/commit/07036cf1af)] - **doc**: add release key for Juan Arboleda (Juan José) [#46922](https://github.com/nodejs/node/pull/46922)
* \[[`553fd5b90a`](https://github.com/nodejs/node/commit/553fd5b90a)] - **doc**: fix links to SSL\_CTX\_set\_options (Tobias Nießen) [#46953](https://github.com/nodejs/node/pull/46953)
* \[[`282bf29884`](https://github.com/nodejs/node/commit/282bf29884)] - **doc**: fix fs missing import (jakecastelli) [#46907](https://github.com/nodejs/node/pull/46907)
* \[[`f9739a85cb`](https://github.com/nodejs/node/commit/f9739a85cb)] - **doc**: add request to hold off publicising sec releases (Michael Dawson) [#46702](https://github.com/nodejs/node/pull/46702)
* \[[`92a61388de`](https://github.com/nodejs/node/commit/92a61388de)] - **doc**: fix stream iterator helpers examples (Benjamin Gruenbaum) [#46897](https://github.com/nodejs/node/pull/46897)
* \[[`8aca3cf410`](https://github.com/nodejs/node/commit/8aca3cf410)] - **doc**: add history info for `node:test` (Antoine du Hamel) [#46851](https://github.com/nodejs/node/pull/46851)
* \[[`c0b6413086`](https://github.com/nodejs/node/commit/c0b6413086)] - **doc**: sort import order (jakecastelli) [#46847](https://github.com/nodejs/node/pull/46847)
* \[[`9d2532e2bb`](https://github.com/nodejs/node/commit/9d2532e2bb)] - **doc**: use destructing import (jakecastelli) [#46847](https://github.com/nodejs/node/pull/46847)
* \[[`48cf9845fe`](https://github.com/nodejs/node/commit/48cf9845fe)] - **doc**: add steps about signing the binary in single-executable docs (Darshan Sen) [#46764](https://github.com/nodejs/node/pull/46764)
* \[[`bd5ef380a5`](https://github.com/nodejs/node/commit/bd5ef380a5)] - **doc**: add marco-ippolito to collaborators (Marco Ippolito) [#46816](https://github.com/nodejs/node/pull/46816)
* \[[`60d1a4887f`](https://github.com/nodejs/node/commit/60d1a4887f)] - **doc**: document how to use the tls.DEFAULT\_CIPHERS (Andreas Martens) [#46482](https://github.com/nodejs/node/pull/46482)
* \[[`00edc50874`](https://github.com/nodejs/node/commit/00edc50874)] - **doc**: add document for profiling and heap snapshot (cola119) [#46787](https://github.com/nodejs/node/pull/46787)
* \[[`fc319d6a4f`](https://github.com/nodejs/node/commit/fc319d6a4f)] - **doc**: add test:coverage event to custom reporter examples (Richie McColl) [#46752](https://github.com/nodejs/node/pull/46752)
* \[[`1b3a25ef22`](https://github.com/nodejs/node/commit/1b3a25ef22)] - **doc**: include context on .toWeb() parameters (Debadree Chatterjee) [#46617](https://github.com/nodejs/node/pull/46617)
* \[[`88057dda3b`](https://github.com/nodejs/node/commit/88057dda3b)] - **doc**: add in security steward for recent release (Michael Dawson) [#46701](https://github.com/nodejs/node/pull/46701)
* \[[`d627164819`](https://github.com/nodejs/node/commit/d627164819)] - **doc**: clarify semver-minor notable changes approach (Beth Griggs) [#46592](https://github.com/nodejs/node/pull/46592)
* \[[`7806cae4fa`](https://github.com/nodejs/node/commit/7806cae4fa)] - **doc**: maintaining nghttp2 (Marco Ippolito) [#46539](https://github.com/nodejs/node/pull/46539)
* \[[`dd66c48a74`](https://github.com/nodejs/node/commit/dd66c48a74)] - **doc**: add emit to NodeEventTarget (Deokjin Kim) [#46356](https://github.com/nodejs/node/pull/46356)
* \[[`458671daeb`](https://github.com/nodejs/node/commit/458671daeb)] - **doc,test**: extend the list of platforms supported by single-executables (Darshan Sen) [#47026](https://github.com/nodejs/node/pull/47026)
* \[[`18f0398242`](https://github.com/nodejs/node/commit/18f0398242)] - **esm**: allow resolve to return import assertions (Geoffrey Booth) [#46153](https://github.com/nodejs/node/pull/46153)
* \[[`5eb5be8c71`](https://github.com/nodejs/node/commit/5eb5be8c71)] - **esm**: move hooks handling into separate class (Geoffrey Booth) [#45869](https://github.com/nodejs/node/pull/45869)
* \[[`9d4d916fe8`](https://github.com/nodejs/node/commit/9d4d916fe8)] - **esm**: fix import assertion warning (Antoine du Hamel) [#46971](https://github.com/nodejs/node/pull/46971)
* \[[`2c621d6e3a`](https://github.com/nodejs/node/commit/2c621d6e3a)] - **esm**: add a runtime warning when using import assertions (Antoine du Hamel) [#46901](https://github.com/nodejs/node/pull/46901)
* \[[`1a23eab614`](https://github.com/nodejs/node/commit/1a23eab614)] - **events**: add trailing commas in source files (Antoine du Hamel) [#46759](https://github.com/nodejs/node/pull/46759)
* \[[`2eb887549a`](https://github.com/nodejs/node/commit/2eb887549a)] - **(SEMVER-MINOR)** **events**: add listener argument to listenerCount (Paolo Insogna) [#46523](https://github.com/nodejs/node/pull/46523)
* \[[`4c12e6eeeb`](https://github.com/nodejs/node/commit/4c12e6eeeb)] - **fs**: add trailing commas in source files (Antoine du Hamel) [#46696](https://github.com/nodejs/node/pull/46696)
* \[[`774eb1995c`](https://github.com/nodejs/node/commit/774eb1995c)] - **http**: use listenerCount when adding noop event (Paolo Insogna) [#46769](https://github.com/nodejs/node/pull/46769)
* \[[`aac5c28091`](https://github.com/nodejs/node/commit/aac5c28091)] - **http**: correctly calculate strict content length (Robert Nagy) [#46601](https://github.com/nodejs/node/pull/46601)
* \[[`e08514e337`](https://github.com/nodejs/node/commit/e08514e337)] - **http**: fix validation of "Link" header (Steve Herzog) [#46466](https://github.com/nodejs/node/pull/46466)
* \[[`6f9cb982a1`](https://github.com/nodejs/node/commit/6f9cb982a1)] - **http**: unify header treatment (Marco Ippolito) [#46528](https://github.com/nodejs/node/pull/46528)
* \[[`05614f8cf6`](https://github.com/nodejs/node/commit/05614f8cf6)] - **lib**: enforce use of trailing commas (Antoine du Hamel) [#46881](https://github.com/nodejs/node/pull/46881)
* \[[`5c7fc9290e`](https://github.com/nodejs/node/commit/5c7fc9290e)] - **lib**: add trailing commas to all public core modules (Antoine du Hamel) [#46848](https://github.com/nodejs/node/pull/46848)
* \[[`08bf01593f`](https://github.com/nodejs/node/commit/08bf01593f)] - **lib**: fix BroadcastChannel initialization location (Shelley Vohr) [#46864](https://github.com/nodejs/node/pull/46864)
* \[[`4e1865126c`](https://github.com/nodejs/node/commit/4e1865126c)] - **lib**: rename internal module declaration as internal bindings (okmttdhr, okp) [#46663](https://github.com/nodejs/node/pull/46663)
* \[[`f914bfff7d`](https://github.com/nodejs/node/commit/f914bfff7d)] - **lib**: add trailing commas to more internal files (Antoine du Hamel) [#46811](https://github.com/nodejs/node/pull/46811)
* \[[`281f176ba4`](https://github.com/nodejs/node/commit/281f176ba4)] - **lib**: fix DOMException property descriptors after being lazy loaded (Filip Skokan) [#46799](https://github.com/nodejs/node/pull/46799)
* \[[`1c6a92b543`](https://github.com/nodejs/node/commit/1c6a92b543)] - **lib**: update punycode to 2.3.0 (Yagiz Nizipli) [#46719](https://github.com/nodejs/node/pull/46719)
* \[[`7b5c00aacd`](https://github.com/nodejs/node/commit/7b5c00aacd)] - **lib**: add trailing commas in `internal/perf` (Antoine du Hamel) [#46697](https://github.com/nodejs/node/pull/46697)
* \[[`c1651bea41`](https://github.com/nodejs/node/commit/c1651bea41)] - **(SEMVER-MINOR)** **lib**: add AsyncLocalStorage.bind() and .snapshot() (flakey5) [#46387](https://github.com/nodejs/node/pull/46387)
* \[[`345c8c343b`](https://github.com/nodejs/node/commit/345c8c343b)] - **lib,src**: fix a few typos in comments (Tobias Nießen) [#46835](https://github.com/nodejs/node/pull/46835)
* \[[`4219c1e893`](https://github.com/nodejs/node/commit/4219c1e893)] - **meta**: add single-executable labels and code owners (Joyee Cheung) [#47004](https://github.com/nodejs/node/pull/47004)
* \[[`b199acd95c`](https://github.com/nodejs/node/commit/b199acd95c)] - **meta**: remove AUTHORS file (Rich Trott) [#46845](https://github.com/nodejs/node/pull/46845)
* \[[`c7f056cbe2`](https://github.com/nodejs/node/commit/c7f056cbe2)] - **meta**: remove unnecessary onboarding step (Rich Trott) [#46793](https://github.com/nodejs/node/pull/46793)
* \[[`4e0b93222c`](https://github.com/nodejs/node/commit/4e0b93222c)] - **meta**: update CODEOWNERS of url implementations (Yagiz Nizipli) [#46775](https://github.com/nodejs/node/pull/46775)
* \[[`9d63ac2724`](https://github.com/nodejs/node/commit/9d63ac2724)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#46726](https://github.com/nodejs/node/pull/46726)
* \[[`40a7b0b993`](https://github.com/nodejs/node/commit/40a7b0b993)] - **net**: fix setting of value in 'setDefaultAutoSelectFamilyAttemptTimeout' (Deokjin Kim) [#47012](https://github.com/nodejs/node/pull/47012)
* \[[`e0d098bd21`](https://github.com/nodejs/node/commit/e0d098bd21)] - **net**: rework autoSelectFamily implementation (Paolo Insogna) [#46587](https://github.com/nodejs/node/pull/46587)
* \[[`58b1f33bd7`](https://github.com/nodejs/node/commit/58b1f33bd7)] - **node-api**: add \_\_wasm32\_\_ guards on async works (Chengzhong Wu) [#46633](https://github.com/nodejs/node/pull/46633)
* \[[`e5b8597f78`](https://github.com/nodejs/node/commit/e5b8597f78)] - **os**: improve network interface performance (Ruben Bridgewater) [#46598](https://github.com/nodejs/node/pull/46598)
* \[[`d3d76c33ea`](https://github.com/nodejs/node/commit/d3d76c33ea)] - **punycode**: add pending deprecation (Antoine du Hamel) [#46719](https://github.com/nodejs/node/pull/46719)
* \[[`56dbb15e7c`](https://github.com/nodejs/node/commit/56dbb15e7c)] - **repl**: remove lastInputPreview conditional check (Duy Mac Van) [#46857](https://github.com/nodejs/node/pull/46857)
* \[[`c7d4ff3f72`](https://github.com/nodejs/node/commit/c7d4ff3f72)] - **repl**: fix .load infinite loop caused by shared use of lineEnding RegExp (Theodor Steiner) [#46742](https://github.com/nodejs/node/pull/46742)
* \[[`4f2bf8c384`](https://github.com/nodejs/node/commit/4f2bf8c384)] - **repl**: add trailing commas in source files (Antoine du Hamel) [#46757](https://github.com/nodejs/node/pull/46757)
* \[[`ed31316c2e`](https://github.com/nodejs/node/commit/ed31316c2e)] - **src**: use std::array for passing argv in node::url (Anna Henningsen) [#47035](https://github.com/nodejs/node/pull/47035)
* \[[`815d2af34d`](https://github.com/nodejs/node/commit/815d2af34d)] - **src**: remove TLSEXT\_TYPE\_alpn guard (Tobias Nießen) [#46956](https://github.com/nodejs/node/pull/46956)
* \[[`b051ac7220`](https://github.com/nodejs/node/commit/b051ac7220)] - **src**: remove use of SSL\_OP\_SINGLE\_DH\_USE (Tobias Nießen) [#46955](https://github.com/nodejs/node/pull/46955)
* \[[`9e65996d16`](https://github.com/nodejs/node/commit/9e65996d16)] - **src**: remove unused `v8::Uint32Array` from encoding (Yagiz Nizipli) [#47003](https://github.com/nodejs/node/pull/47003)
* \[[`6b60f38676`](https://github.com/nodejs/node/commit/6b60f38676)] - **src**: use AliasedUint32Array for encodeInto results (Joyee Cheung) [#46658](https://github.com/nodejs/node/pull/46658)
* \[[`dcba3a0673`](https://github.com/nodejs/node/commit/dcba3a0673)] - **src**: move encoding bindings to a new binding (Joyee Cheung) [#46658](https://github.com/nodejs/node/pull/46658)
* \[[`6740679965`](https://github.com/nodejs/node/commit/6740679965)] - **src**: fix negative nodeTiming milestone values (Chengzhong Wu) [#46588](https://github.com/nodejs/node/pull/46588)
* \[[`074692a6f0`](https://github.com/nodejs/node/commit/074692a6f0)] - **src**: fix missing trailing , (Cheng Zhao) [#46909](https://github.com/nodejs/node/pull/46909)
* \[[`32bd38fb05`](https://github.com/nodejs/node/commit/32bd38fb05)] - **src**: make util.h self-containted (Joyee Cheung) [#46817](https://github.com/nodejs/node/pull/46817)
* \[[`0d9c345f4d`](https://github.com/nodejs/node/commit/0d9c345f4d)] - **src**: remove shadowed variable in OptionsParser (Shelley Vohr) [#46672](https://github.com/nodejs/node/pull/46672)
* \[[`578a2c53a5`](https://github.com/nodejs/node/commit/578a2c53a5)] - **src**: do not track BaseObjects directly in Realm (Joyee Cheung) [#46470](https://github.com/nodejs/node/pull/46470)
* \[[`9fab228115`](https://github.com/nodejs/node/commit/9fab228115)] - **src**: fix cb scope bugs involved in termination (ywave620) [#45596](https://github.com/nodejs/node/pull/45596)
* \[[`c0fcad3827`](https://github.com/nodejs/node/commit/c0fcad3827)] - **src**: use an array for faster binding data lookup (Joyee Cheung) [#46620](https://github.com/nodejs/node/pull/46620)
* \[[`973287a462`](https://github.com/nodejs/node/commit/973287a462)] - **src**: per-realm binding data (Chengzhong Wu) [#46556](https://github.com/nodejs/node/pull/46556)
* \[[`ad5f42d1e9`](https://github.com/nodejs/node/commit/ad5f42d1e9)] - **src**: add SetFastMethodNoSideEffect() (Joyee Cheung) [#46619](https://github.com/nodejs/node/pull/46619)
* \[[`518b890f59`](https://github.com/nodejs/node/commit/518b890f59)] - _**Revert**_ "**src**: let http2 streams end after session close" (Rich Trott) [#46721](https://github.com/nodejs/node/pull/46721)
* \[[`19b5d0750c`](https://github.com/nodejs/node/commit/19b5d0750c)] - **src**: use string\_view for report and related code (Anna Henningsen) [#46723](https://github.com/nodejs/node/pull/46723)
* \[[`36f36b99b0`](https://github.com/nodejs/node/commit/36f36b99b0)] - **(SEMVER-MINOR)** **src**: update Blob implementation to use DataQueue / File-backed Blobs (James M Snell) [#45258](https://github.com/nodejs/node/pull/45258)
* \[[`9b6270afe2`](https://github.com/nodejs/node/commit/9b6270afe2)] - **(SEMVER-MINOR)** **src**: implement DataQueue (James M Snell) [#45258](https://github.com/nodejs/node/pull/45258)
* \[[`d48ed95a66`](https://github.com/nodejs/node/commit/d48ed95a66)] - **(SEMVER-MINOR)** **src, lib**: fixup lint and format issues for DataQueue/Blob (James M Snell) [#45258](https://github.com/nodejs/node/pull/45258)
* \[[`f8866812fd`](https://github.com/nodejs/node/commit/f8866812fd)] - **stream**: enable usage of webstreams on compose() (Debadree Chatterjee) [#46675](https://github.com/nodejs/node/pull/46675)
* \[[`4ad48d9cb9`](https://github.com/nodejs/node/commit/4ad48d9cb9)] - **stream**: always delay construct callback by a nextTick (Matteo Collina) [#46818](https://github.com/nodejs/node/pull/46818)
* \[[`93e91f3dde`](https://github.com/nodejs/node/commit/93e91f3dde)] - **stream**: fix respondWithNewView() errors when view\.byteOffset != 0 (Debadree Chatterjee) [#46465](https://github.com/nodejs/node/pull/46465)
* \[[`1f386570af`](https://github.com/nodejs/node/commit/1f386570af)] - **stream**: fix pipeline callback not called on ended stream (Debadree Chatterjee) [#46600](https://github.com/nodejs/node/pull/46600)
* \[[`c972612c9d`](https://github.com/nodejs/node/commit/c972612c9d)] - **test**: fix flakyness in test-runner reporter test (Moshe Atlow) [#45930](https://github.com/nodejs/node/pull/45930)
* \[[`11509a4a2d`](https://github.com/nodejs/node/commit/11509a4a2d)] - **test**: move `test-tls-autoselectfamily-servername` to `test/internet` (Antoine du Hamel) [#47029](https://github.com/nodejs/node/pull/47029)
* \[[`9556d98054`](https://github.com/nodejs/node/commit/9556d98054)] - **test**: fallback to IPv4 if IPv6 is unavailable (Abdirahim Musse) [#47017](https://github.com/nodejs/node/pull/47017)
* \[[`5b81689efa`](https://github.com/nodejs/node/commit/5b81689efa)] - **test**: simplify test-tls-ecdh-multiple (Tobias Nießen) [#46963](https://github.com/nodejs/node/pull/46963)
* \[[`c8d528e979`](https://github.com/nodejs/node/commit/c8d528e979)] - **test**: update WPT resources, common, streams, FileAPI, broadcastchannel (Filip Skokan) [#46912](https://github.com/nodejs/node/pull/46912)
* \[[`acfd9b8879`](https://github.com/nodejs/node/commit/acfd9b8879)] - **test**: improve test coverage of lib/dns (Anderson Paiva) [#46910](https://github.com/nodejs/node/pull/46910)
* \[[`21153f164d`](https://github.com/nodejs/node/commit/21153f164d)] - **test**: simplify test-tls-ecdh-auto (Tobias Nießen) [#46911](https://github.com/nodejs/node/pull/46911)
* \[[`e5b8896186`](https://github.com/nodejs/node/commit/e5b8896186)] - **test**: move testPath from CWD to temporary directory (Livia Medeiros) [#46890](https://github.com/nodejs/node/pull/46890)
* \[[`db2ace1f94`](https://github.com/nodejs/node/commit/db2ace1f94)] - **test**: assume priv ports start at 1024 if it can't be changed (KrayzeeKev) [#46536](https://github.com/nodejs/node/pull/46536)
* \[[`0e45470fd3`](https://github.com/nodejs/node/commit/0e45470fd3)] - **test**: update web-platform tests for url (Xuguang Mei) [#46860](https://github.com/nodejs/node/pull/46860)
* \[[`6fa142d8f8`](https://github.com/nodejs/node/commit/6fa142d8f8)] - **test**: move socket from CWD to temporary directory (Livia Medeiros) [#46863](https://github.com/nodejs/node/pull/46863)
* \[[`df155b8fd5`](https://github.com/nodejs/node/commit/df155b8fd5)] - **test**: fix os-release check for Ubuntu in SEA test (Anna Henningsen) [#46838](https://github.com/nodejs/node/pull/46838)
* \[[`e585a11fd5`](https://github.com/nodejs/node/commit/e585a11fd5)] - **test**: fix test-net-connect-reset-until-connected (Vita Batrla) [#46781](https://github.com/nodejs/node/pull/46781)
* \[[`f21ed3a63f`](https://github.com/nodejs/node/commit/f21ed3a63f)] - **test**: simplify test-tls-alert (Tobias Nießen) [#46805](https://github.com/nodejs/node/pull/46805)
* \[[`e5fa7a139a`](https://github.com/nodejs/node/commit/e5fa7a139a)] - **test**: fix WPT title when no META title is present (Filip Skokan) [#46804](https://github.com/nodejs/node/pull/46804)
* \[[`bd097ca4bf`](https://github.com/nodejs/node/commit/bd097ca4bf)] - **test**: update encoding WPTs (Filip Skokan) [#46802](https://github.com/nodejs/node/pull/46802)
* \[[`3ab1aabb3f`](https://github.com/nodejs/node/commit/3ab1aabb3f)] - **test**: remove useless WPT init scripts (Filip Skokan) [#46801](https://github.com/nodejs/node/pull/46801)
* \[[`323415535b`](https://github.com/nodejs/node/commit/323415535b)] - **test**: remove useless require('../common') from WPTs (Filip Skokan) [#46796](https://github.com/nodejs/node/pull/46796)
* \[[`76a9634305`](https://github.com/nodejs/node/commit/76a9634305)] - **test**: isolate hr-time specific wpt global init (Filip Skokan) [#46795](https://github.com/nodejs/node/pull/46795)
* \[[`3daf508993`](https://github.com/nodejs/node/commit/3daf508993)] - **test**: stop faking performance idlharness (Filip Skokan) [#46794](https://github.com/nodejs/node/pull/46794)
* \[[`e52ad92b08`](https://github.com/nodejs/node/commit/e52ad92b08)] - **test**: remove unreachable return (jakecastelli) [#46807](https://github.com/nodejs/node/pull/46807)
* \[[`9c7a2e30fb`](https://github.com/nodejs/node/commit/9c7a2e30fb)] - **test**: fix test-v8-collect-gc-profile-in-worker.js (theanarkh) [#46735](https://github.com/nodejs/node/pull/46735)
* \[[`a92be13dad`](https://github.com/nodejs/node/commit/a92be13dad)] - **test**: improve control flow in test-tls-dhe (Tobias Nießen) [#46751](https://github.com/nodejs/node/pull/46751)
* \[[`4e9915e383`](https://github.com/nodejs/node/commit/4e9915e383)] - **test**: include strace openat test (Rafael Gonzaga) [#46150](https://github.com/nodejs/node/pull/46150)
* \[[`2c4f670c6b`](https://github.com/nodejs/node/commit/2c4f670c6b)] - **test**: fix IPv6 checks on IBM i (Abdirahim Musse) [#46546](https://github.com/nodejs/node/pull/46546)
* \[[`b2cfcf9cd8`](https://github.com/nodejs/node/commit/b2cfcf9cd8)] - **test**: fix default WPT titles (Filip Skokan) [#46778](https://github.com/nodejs/node/pull/46778)
* \[[`f4cdc6f20f`](https://github.com/nodejs/node/commit/f4cdc6f20f)] - **test**: remove OpenSSL 1.0.2 error message compat (Tobias Nießen) [#46709](https://github.com/nodejs/node/pull/46709)
* \[[`d5784c79bc`](https://github.com/nodejs/node/commit/d5784c79bc)] - **test**: fix flaky test-watch-mode-files\_watcher (Moshe Atlow) [#46738](https://github.com/nodejs/node/pull/46738)
* \[[`abba45e120`](https://github.com/nodejs/node/commit/abba45e120)] - **test**: remove obsolete util.isDeepStrictEqual tests (Ruben Bridgewater) [#46593](https://github.com/nodejs/node/pull/46593)
* \[[`3401315e4e`](https://github.com/nodejs/node/commit/3401315e4e)] - **test**: use newish OpenSSL feature in test-tls-dhe (Tobias Nießen) [#46708](https://github.com/nodejs/node/pull/46708)
* \[[`95bbd0f7d6`](https://github.com/nodejs/node/commit/95bbd0f7d6)] - **test**: update web-platform tests for url (Yagiz Nizipli) [#46547](https://github.com/nodejs/node/pull/46547)
* \[[`13f14a5efa`](https://github.com/nodejs/node/commit/13f14a5efa)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#47010](https://github.com/nodejs/node/pull/47010)
* \[[`5e31599c26`](https://github.com/nodejs/node/commit/5e31599c26)] - **test\_runner**: default to spec reporter when on TTY environment (Moshe Atlow) [#46969](https://github.com/nodejs/node/pull/46969)
* \[[`18146fc8c1`](https://github.com/nodejs/node/commit/18146fc8c1)] - **test\_runner**: handle errors not bound to tests (Colin Ihrig) [#46962](https://github.com/nodejs/node/pull/46962)
* \[[`7960ccb61e`](https://github.com/nodejs/node/commit/7960ccb61e)] - **test\_runner**: throw if harness is not bootstrapped (Colin Ihrig) [#46962](https://github.com/nodejs/node/pull/46962)
* \[[`b832d77500`](https://github.com/nodejs/node/commit/b832d77500)] - **test\_runner**: track bootstrapping process (Colin Ihrig) [#46962](https://github.com/nodejs/node/pull/46962)
* \[[`debc0adcf0`](https://github.com/nodejs/node/commit/debc0adcf0)] - **test\_runner**: avoid running twice tests in describe (Moshe Atlow) [#46888](https://github.com/nodejs/node/pull/46888)
* \[[`0923cbcfe6`](https://github.com/nodejs/node/commit/0923cbcfe6)] - **test\_runner**: fix reconstruction of errors extracted from YAML (Moshe Atlow) [#46872](https://github.com/nodejs/node/pull/46872)
* \[[`ecf714e1d5`](https://github.com/nodejs/node/commit/ecf714e1d5)] - **test\_runner**: reset count on watch mode (Moshe Atlow) [#46577](https://github.com/nodejs/node/pull/46577)
* \[[`6d32a16319`](https://github.com/nodejs/node/commit/6d32a16319)] - **test\_runner**: bootstrap reporters before running tests (Moshe Atlow) [#46737](https://github.com/nodejs/node/pull/46737)
* \[[`ffa86f7fa9`](https://github.com/nodejs/node/commit/ffa86f7fa9)] - **test\_runner**: emit test-only diagnostic warning (Richie McColl) [#46540](https://github.com/nodejs/node/pull/46540)
* \[[`3a1a7fa741`](https://github.com/nodejs/node/commit/3a1a7fa741)] - **test\_runner**: flatten TAP output when running using `--test` (Moshe Atlow) [#46440](https://github.com/nodejs/node/pull/46440)
* \[[`069ff1cc63`](https://github.com/nodejs/node/commit/069ff1cc63)] - **test\_runner**: remove root tracking set (Colin Ihrig) [#46961](https://github.com/nodejs/node/pull/46961)
* \[[`4b7198c3cb`](https://github.com/nodejs/node/commit/4b7198c3cb)] - **test\_runner**: give the root test a harness reference (Colin Ihrig) [#46962](https://github.com/nodejs/node/pull/46962)
* \[[`762dc7cb7a`](https://github.com/nodejs/node/commit/762dc7cb7a)] - **test\_runner**: align behavior of it and test (Moshe Atlow) [#46889](https://github.com/nodejs/node/pull/46889)
* \[[`aa41f27d53`](https://github.com/nodejs/node/commit/aa41f27d53)] - **test\_runner**: add `describe.only` and `it.only` shorthands (Richie McColl) [#46604](https://github.com/nodejs/node/pull/46604)
* \[[`dfe529b709`](https://github.com/nodejs/node/commit/dfe529b709)] - **test\_runner**: better handle async bootstrap errors (Colin Ihrig) [#46720](https://github.com/nodejs/node/pull/46720)
* \[[`320ddc0a0c`](https://github.com/nodejs/node/commit/320ddc0a0c)] - **test\_runner**: centralize CLI option handling (Colin Ihrig) [#46707](https://github.com/nodejs/node/pull/46707)
* \[[`66016e2a29`](https://github.com/nodejs/node/commit/66016e2a29)] - **test\_runner**: display skipped tests in spec reporter output (Richie McColl) [#46651](https://github.com/nodejs/node/pull/46651)
* \[[`25069a60c7`](https://github.com/nodejs/node/commit/25069a60c7)] - **timers**: use V8 fast API calls (Joyee Cheung) [#46579](https://github.com/nodejs/node/pull/46579)
* \[[`bb9b1c637d`](https://github.com/nodejs/node/commit/bb9b1c637d)] - **(SEMVER-MINOR)** **tls**: support automatic DHE (Tobias Nießen) [#46978](https://github.com/nodejs/node/pull/46978)
* \[[`4df008457d`](https://github.com/nodejs/node/commit/4df008457d)] - **tls**: add trailing commas in source files (Antoine du Hamel) [#46715](https://github.com/nodejs/node/pull/46715)
* \[[`36c48eab31`](https://github.com/nodejs/node/commit/36c48eab31)] - **tools**: add daily WPT Report workflow step summary (Filip Skokan) [#46763](https://github.com/nodejs/node/pull/46763)
* \[[`12a561875b`](https://github.com/nodejs/node/commit/12a561875b)] - **tools**: add undici WPTs to daily WPT Report (Filip Skokan) [#46763](https://github.com/nodejs/node/pull/46763)
* \[[`0f1ecbccca`](https://github.com/nodejs/node/commit/0f1ecbccca)] - **tools**: dont use cached node versions in daily wpt (Filip Skokan) [#47024](https://github.com/nodejs/node/pull/47024)
* \[[`2e7ba3159b`](https://github.com/nodejs/node/commit/2e7ba3159b)] - **tools**: automate cares update (Marco Ippolito) [#46993](https://github.com/nodejs/node/pull/46993)
* \[[`8723844f29`](https://github.com/nodejs/node/commit/8723844f29)] - **tools**: update lint-md-dependencies to rollup\@3.18.0 (Node.js GitHub Bot) [#46935](https://github.com/nodejs/node/pull/46935)
* \[[`f4a59b723d`](https://github.com/nodejs/node/commit/f4a59b723d)] - **tools**: add automation for updating OpenSSL dependency (Facundo Tuesca) [#45605](https://github.com/nodejs/node/pull/45605)
* \[[`ecce6475b9`](https://github.com/nodejs/node/commit/ecce6475b9)] - **tools**: refactor dep\_updaters (Tony Gorez) [#46488](https://github.com/nodejs/node/pull/46488)
* \[[`132fc45d16`](https://github.com/nodejs/node/commit/132fc45d16)] - **tools**: fix daily wpt nightly version selection (Filip Skokan) [#46891](https://github.com/nodejs/node/pull/46891)
* \[[`078600c130`](https://github.com/nodejs/node/commit/078600c130)] - **tools**: update eslint to 8.35.0 (Node.js GitHub Bot) [#46854](https://github.com/nodejs/node/pull/46854)
* \[[`724f9d61a3`](https://github.com/nodejs/node/commit/724f9d61a3)] - **tools**: create llhttp update action (Marco Ippolito) [#46766](https://github.com/nodejs/node/pull/46766)
* \[[`f558797744`](https://github.com/nodejs/node/commit/f558797744)] - **tools**: fix linter message when using global `DOMException` (Antoine du Hamel) [#46822](https://github.com/nodejs/node/pull/46822)
* \[[`f4cbe4ea4b`](https://github.com/nodejs/node/commit/f4cbe4ea4b)] - **tools**: update lint-md-dependencies to rollup\@3.17.3 (Node.js GitHub Bot) [#46843](https://github.com/nodejs/node/pull/46843)
* \[[`4b91420307`](https://github.com/nodejs/node/commit/4b91420307)] - **tools**: upload daily WPT Report to both staging and production (Filip Skokan) [#46803](https://github.com/nodejs/node/pull/46803)
* \[[`2f09d3f9a1`](https://github.com/nodejs/node/commit/2f09d3f9a1)] - **tools**: update lint-md-dependencies to rollup\@3.17.2 (Node.js GitHub Bot) [#46771](https://github.com/nodejs/node/pull/46771)
* \[[`f6bd145768`](https://github.com/nodejs/node/commit/f6bd145768)] - **tools**: run format-cpp on node-api test c files (Chengzhong Wu) [#46694](https://github.com/nodejs/node/pull/46694)
* \[[`694659cecb`](https://github.com/nodejs/node/commit/694659cecb)] - **tools**: cleanup WPT refs in daily-wpt-fyi.yml (Filip Skokan) [#46740](https://github.com/nodejs/node/pull/46740)
* \[[`1756830e36`](https://github.com/nodejs/node/commit/1756830e36)] - **tools**: use actions pinned by commit hash in coverage-linux (Gabriela Gutierrez) [#46294](https://github.com/nodejs/node/pull/46294)
* \[[`25ccaa7b3a`](https://github.com/nodejs/node/commit/25ccaa7b3a)] - **tools**: fix regex strings in Python tools (Jan Osusky) [#46671](https://github.com/nodejs/node/pull/46671)
* \[[`dd400341ad`](https://github.com/nodejs/node/commit/dd400341ad)] - **tools**: fixed path (Marco Ippolito) [#46700](https://github.com/nodejs/node/pull/46700)
* \[[`a560a78962`](https://github.com/nodejs/node/commit/a560a78962)] - **tools**: update nghttp2 action (Marco Ippolito) [#46700](https://github.com/nodejs/node/pull/46700)
* \[[`2ff9b20c3c`](https://github.com/nodejs/node/commit/2ff9b20c3c)] - **tools**: update-nghttp2 preserve config.h (Marco Ippolito) [#46698](https://github.com/nodejs/node/pull/46698)
* \[[`6ff0b801f1`](https://github.com/nodejs/node/commit/6ff0b801f1)] - **tools**: update lint-md-dependencies to rollup\@3.17.1 (Node.js GitHub Bot) [#46712](https://github.com/nodejs/node/pull/46712)
* \[[`b7e027af4d`](https://github.com/nodejs/node/commit/b7e027af4d)] - **tools**: update lint-md-dependencies to rollup\@3.17.0 (Node.js GitHub Bot) [#46712](https://github.com/nodejs/node/pull/46712)
* \[[`617b5b106a`](https://github.com/nodejs/node/commit/617b5b106a)] - **tools**: update wpt.fyi used daily checkout ref (Filip Skokan) [#46730](https://github.com/nodejs/node/pull/46730)
* \[[`63a83b4451`](https://github.com/nodejs/node/commit/63a83b4451)] - **typings**: improve `primordials` typings (Antoine du Hamel) [#46970](https://github.com/nodejs/node/pull/46970)
* \[[`1fa6352853`](https://github.com/nodejs/node/commit/1fa6352853)] - **url**: offload `URLSearchParams` initialization (Yagiz Nizipli) [#46867](https://github.com/nodejs/node/pull/46867)
* \[[`e754277a44`](https://github.com/nodejs/node/commit/e754277a44)] - **url**: fix array overrun in node:url::SetArgs() (Yagiz Nizipli) [#47001](https://github.com/nodejs/node/pull/47001)
* \[[`b1747feb57`](https://github.com/nodejs/node/commit/b1747feb57)] - **url**: set `formatUrl` method as no side effect (Yagiz Nizipli) [#46884](https://github.com/nodejs/node/pull/46884)
* \[[`b8560ec8cc`](https://github.com/nodejs/node/commit/b8560ec8cc)] - **url**: remove unnecessary call to `FunctionPrototypeBind` (Antoine du Hamel) [#46870](https://github.com/nodejs/node/pull/46870)
* \[[`f8765be197`](https://github.com/nodejs/node/commit/f8765be197)] - **url**: remove unused `kFormat` from url (Yagiz Nizipli) [#46867](https://github.com/nodejs/node/pull/46867)
* \[[`b10fe5856b`](https://github.com/nodejs/node/commit/b10fe5856b)] - **url**: improve `isURLThis` detection (Yagiz Nizipli) [#46866](https://github.com/nodejs/node/pull/46866)
* \[[`1e20b05acd`](https://github.com/nodejs/node/commit/1e20b05acd)] - **(SEMVER-MINOR)** **url**: implement URLSearchParams size getter (James M Snell) [#46308](https://github.com/nodejs/node/pull/46308)
* \[[`5a3ad8763b`](https://github.com/nodejs/node/commit/5a3ad8763b)] - **url**: simplify and improve url formatting (Yagiz Nizipli) [#46736](https://github.com/nodejs/node/pull/46736)
* \[[`a52405599c`](https://github.com/nodejs/node/commit/a52405599c)] - **url**: improve performance by removing host (Yagiz Nizipli) [#46547](https://github.com/nodejs/node/pull/46547)
* \[[`9d55a5e5bb`](https://github.com/nodejs/node/commit/9d55a5e5bb)] - **url**: fix url spec compliance issues (Yagiz Nizipli) [#46547](https://github.com/nodejs/node/pull/46547)
* \[[`77b4aca2cc`](https://github.com/nodejs/node/commit/77b4aca2cc)] - **vm**: fix leak in vm.compileFunction when importModuleDynamically is used (Joyee Cheung) [#46785](https://github.com/nodejs/node/pull/46785)
* \[[`b2a80d788a`](https://github.com/nodejs/node/commit/b2a80d788a)] - **wasi**: add wasi sock\_accept stub (Michael Dawson) [#46434](https://github.com/nodejs/node/pull/46434)
* \[[`60e5f45141`](https://github.com/nodejs/node/commit/60e5f45141)] - **(SEMVER-MINOR)** **wasi**: add support for version when creating WASI (Michael Dawson) [#46469](https://github.com/nodejs/node/pull/46469)
* \[[`a646a22d0f`](https://github.com/nodejs/node/commit/a646a22d0f)] - **(SEMVER-MINOR)** **worker**: add support for worker name in inspector and trace\_events (Debadree Chatterjee) [#46832](https://github.com/nodejs/node/pull/46832)

<a id="19.7.0"></a>

## 2023-02-21, Version 19.7.0 (Current), @MylesBorins

### Notable Changes

* \[[`60a612607e`](https://github.com/nodejs/node/commit/60a612607e)] - **deps**: upgrade npm to 9.5.0 (npm team) [#46673](https://github.com/nodejs/node/pull/46673)
* \[[`7d6c27eab1`](https://github.com/nodejs/node/commit/7d6c27eab1)] - **deps**: add ada as a dependency (Yagiz Nizipli) [#46410](https://github.com/nodejs/node/pull/46410)
* \[[`a79a8bf85a`](https://github.com/nodejs/node/commit/a79a8bf85a)] - **doc**: add debadree25 to collaborators (Debadree Chatterjee) [#46716](https://github.com/nodejs/node/pull/46716)
* \[[`0c2c322ee6`](https://github.com/nodejs/node/commit/0c2c322ee6)] - **doc**: add deokjinkim to collaborators (Deokjin Kim) [#46444](https://github.com/nodejs/node/pull/46444)
* \[[`9b23309f53`](https://github.com/nodejs/node/commit/9b23309f53)] - **doc,lib,src,test**: rename --test-coverage (Colin Ihrig) [#46017](https://github.com/nodejs/node/pull/46017)
* \[[`8590eb4830`](https://github.com/nodejs/node/commit/8590eb4830)] - **(SEMVER-MINOR)** **lib**: add aborted() utility function (Debadree Chatterjee) [#46494](https://github.com/nodejs/node/pull/46494)
* \[[`164bfe82cc`](https://github.com/nodejs/node/commit/164bfe82cc)] - **(SEMVER-MINOR)** **src**: add initial support for single executable applications (Darshan Sen) [#45038](https://github.com/nodejs/node/pull/45038)
* \[[`f3908411fd`](https://github.com/nodejs/node/commit/f3908411fd)] - **(SEMVER-MINOR)** **src**: allow optional Isolate termination in node::Stop() (Shelley Vohr) [#46583](https://github.com/nodejs/node/pull/46583)
* \[[`c34bac2fed`](https://github.com/nodejs/node/commit/c34bac2fed)] - **(SEMVER-MINOR)** **src**: allow blobs in addition to `FILE*`s in embedder snapshot API (Anna Henningsen) [#46491](https://github.com/nodejs/node/pull/46491)
* \[[`683a1f8f3e`](https://github.com/nodejs/node/commit/683a1f8f3e)] - **(SEMVER-MINOR)** **src**: allow snapshotting from the embedder API (Anna Henningsen) [#45888](https://github.com/nodejs/node/pull/45888)
* \[[`658d2f4710`](https://github.com/nodejs/node/commit/658d2f4710)] - **(SEMVER-MINOR)** **src**: make build\_snapshot a per-Isolate option, rather than a global one (Anna Henningsen) [#45888](https://github.com/nodejs/node/pull/45888)
* \[[`6801d3753c`](https://github.com/nodejs/node/commit/6801d3753c)] - **(SEMVER-MINOR)** **src**: add snapshot support for embedder API (Anna Henningsen) [#45888](https://github.com/nodejs/node/pull/45888)
* \[[`e77d538d32`](https://github.com/nodejs/node/commit/e77d538d32)] - **(SEMVER-MINOR)** **src**: allow embedder control of code generation policy (Shelley Vohr) [#46368](https://github.com/nodejs/node/pull/46368)
* \[[`633d3f292d`](https://github.com/nodejs/node/commit/633d3f292d)] - **(SEMVER-MINOR)** **stream**: add abort signal for ReadableStream and WritableStream (Debadree Chatterjee) [#46273](https://github.com/nodejs/node/pull/46273)
* \[[`6119289251`](https://github.com/nodejs/node/commit/6119289251)] - **test\_runner**: add initial code coverage support (Colin Ihrig) [#46017](https://github.com/nodejs/node/pull/46017)
* \[[`a51fe3c663`](https://github.com/nodejs/node/commit/a51fe3c663)] - **url**: replace url-parser with ada (Yagiz Nizipli) [#46410](https://github.com/nodejs/node/pull/46410)

### Commits

* \[[`731a7ae9da`](https://github.com/nodejs/node/commit/731a7ae9da)] - **async\_hooks**: add async local storage propagation benchmarks (Chengzhong Wu) [#46414](https://github.com/nodejs/node/pull/46414)
* \[[`05ad792a07`](https://github.com/nodejs/node/commit/05ad792a07)] - **async\_hooks**: remove experimental onPropagate option (James M Snell) [#46386](https://github.com/nodejs/node/pull/46386)
* \[[`6b21170b10`](https://github.com/nodejs/node/commit/6b21170b10)] - **benchmark**: add trailing commas in `benchmark/path` (Antoine du Hamel) [#46628](https://github.com/nodejs/node/pull/46628)
* \[[`4b89ec409f`](https://github.com/nodejs/node/commit/4b89ec409f)] - **benchmark**: add trailing commas in `benchmark/http` (Antoine du Hamel) [#46609](https://github.com/nodejs/node/pull/46609)
* \[[`ff95eb7386`](https://github.com/nodejs/node/commit/ff95eb7386)] - **benchmark**: add trailing commas in `benchmark/crypto` (Antoine du Hamel) [#46553](https://github.com/nodejs/node/pull/46553)
* \[[`638d9b8d4b`](https://github.com/nodejs/node/commit/638d9b8d4b)] - **benchmark**: add trailing commas in `benchmark/url` (Antoine du Hamel) [#46551](https://github.com/nodejs/node/pull/46551)
* \[[`7524871a9b`](https://github.com/nodejs/node/commit/7524871a9b)] - **benchmark**: add trailing commas in `benchmark/http2` (Antoine du Hamel) [#46552](https://github.com/nodejs/node/pull/46552)
* \[[`9d9b3f856f`](https://github.com/nodejs/node/commit/9d9b3f856f)] - **benchmark**: add trailing commas in `benchmark/process` (Antoine du Hamel) [#46481](https://github.com/nodejs/node/pull/46481)
* \[[`6c69ad6d43`](https://github.com/nodejs/node/commit/6c69ad6d43)] - **benchmark**: add trailing commas in `benchmark/misc` (Antoine du Hamel) [#46474](https://github.com/nodejs/node/pull/46474)
* \[[`7f8b292bee`](https://github.com/nodejs/node/commit/7f8b292bee)] - **benchmark**: add trailing commas in `benchmark/buffers` (Antoine du Hamel) [#46473](https://github.com/nodejs/node/pull/46473)
* \[[`897e3c2782`](https://github.com/nodejs/node/commit/897e3c2782)] - **benchmark**: add trailing commas in `benchmark/module` (Antoine du Hamel) [#46461](https://github.com/nodejs/node/pull/46461)
* \[[`7760d40c04`](https://github.com/nodejs/node/commit/7760d40c04)] - **benchmark**: add trailing commas in `benchmark/net` (Antoine du Hamel) [#46439](https://github.com/nodejs/node/pull/46439)
* \[[`8b88d605ca`](https://github.com/nodejs/node/commit/8b88d605ca)] - **benchmark**: add trailing commas in `benchmark/util` (Antoine du Hamel) [#46438](https://github.com/nodejs/node/pull/46438)
* \[[`2c8c9f978d`](https://github.com/nodejs/node/commit/2c8c9f978d)] - **benchmark**: add trailing commas in `benchmark/async_hooks` (Antoine du Hamel) [#46424](https://github.com/nodejs/node/pull/46424)
* \[[`b364b9bd60`](https://github.com/nodejs/node/commit/b364b9bd60)] - **benchmark**: add trailing commas in `benchmark/fs` (Antoine du Hamel) [#46426](https://github.com/nodejs/node/pull/46426)
* \[[`e15ddba7e7`](https://github.com/nodejs/node/commit/e15ddba7e7)] - **build**: add GitHub Action for coverage with --without-intl (Rich Trott) [#37954](https://github.com/nodejs/node/pull/37954)
* \[[`c781a48097`](https://github.com/nodejs/node/commit/c781a48097)] - **build**: do not disable inspector when intl is disabled (Rich Trott) [#37954](https://github.com/nodejs/node/pull/37954)
* \[[`b4deb2fcd5`](https://github.com/nodejs/node/commit/b4deb2fcd5)] - **crypto**: don't assume FIPS is disabled by default (Michael Dawson) [#46532](https://github.com/nodejs/node/pull/46532)
* \[[`60a612607e`](https://github.com/nodejs/node/commit/60a612607e)] - **deps**: upgrade npm to 9.5.0 (npm team) [#46673](https://github.com/nodejs/node/pull/46673)
* \[[`6c997035fc`](https://github.com/nodejs/node/commit/6c997035fc)] - **deps**: update corepack to 0.16.0 (Node.js GitHub Bot) [#46710](https://github.com/nodejs/node/pull/46710)
* \[[`2ed3875eee`](https://github.com/nodejs/node/commit/2ed3875eee)] - **deps**: update undici to 5.20.0 (Node.js GitHub Bot) [#46711](https://github.com/nodejs/node/pull/46711)
* \[[`20cb13bf7f`](https://github.com/nodejs/node/commit/20cb13bf7f)] - **deps**: update ada to v1.0.1 (Yagiz Nizipli) [#46550](https://github.com/nodejs/node/pull/46550)
* \[[`c0983cfc06`](https://github.com/nodejs/node/commit/c0983cfc06)] - **deps**: copy `postject-api.h` and `LICENSE` to the `deps` folder (Darshan Sen) [#46582](https://github.com/nodejs/node/pull/46582)
* \[[`7d6c27eab1`](https://github.com/nodejs/node/commit/7d6c27eab1)] - **deps**: add ada as a dependency (Yagiz Nizipli) [#46410](https://github.com/nodejs/node/pull/46410)
* \[[`7e7e2d037b`](https://github.com/nodejs/node/commit/7e7e2d037b)] - **deps**: update c-ares to 1.19.0 (Michaël Zasso) [#46415](https://github.com/nodejs/node/pull/46415)
* \[[`a79a8bf85a`](https://github.com/nodejs/node/commit/a79a8bf85a)] - **doc**: add debadree25 to collaborators (Debadree Chatterjee) [#46716](https://github.com/nodejs/node/pull/46716)
* \[[`6a8b04d709`](https://github.com/nodejs/node/commit/6a8b04d709)] - **doc**: move bcoe to emeriti (Benjamin Coe) [#46703](https://github.com/nodejs/node/pull/46703)
* \[[`a0a6ee0f54`](https://github.com/nodejs/node/commit/a0a6ee0f54)] - **doc**: add response.strictContentLength to documentation (Marco Ippolito) [#46627](https://github.com/nodejs/node/pull/46627)
* \[[`ffdd64dce3`](https://github.com/nodejs/node/commit/ffdd64dce3)] - **doc**: remove unused functions from example of `streamConsumers.text` (Deokjin Kim) [#46581](https://github.com/nodejs/node/pull/46581)
* \[[`c771d66864`](https://github.com/nodejs/node/commit/c771d66864)] - **doc**: fix test runner examples (Richie McColl) [#46565](https://github.com/nodejs/node/pull/46565)
* \[[`375bb22df9`](https://github.com/nodejs/node/commit/375bb22df9)] - **doc**: update test concurrency description / default values (richiemccoll) [#46457](https://github.com/nodejs/node/pull/46457)
* \[[`a7beac04ba`](https://github.com/nodejs/node/commit/a7beac04ba)] - **doc**: enrich test command with executable (Tony Gorez) [#44347](https://github.com/nodejs/node/pull/44347)
* \[[`aef57cd290`](https://github.com/nodejs/node/commit/aef57cd290)] - **doc**: fix wrong location of `requestTimeout`'s default value (Deokjin Kim) [#46423](https://github.com/nodejs/node/pull/46423)
* \[[`0c2c322ee6`](https://github.com/nodejs/node/commit/0c2c322ee6)] - **doc**: add deokjinkim to collaborators (Deokjin Kim) [#46444](https://github.com/nodejs/node/pull/46444)
* \[[`31d3e3c486`](https://github.com/nodejs/node/commit/31d3e3c486)] - **doc**: fix -C flag usage (三咲智子 Kevin Deng) [#46388](https://github.com/nodejs/node/pull/46388)
* \[[`905a6756a3`](https://github.com/nodejs/node/commit/905a6756a3)] - **doc**: add note about major release rotation (Rafael Gonzaga) [#46436](https://github.com/nodejs/node/pull/46436)
* \[[`33a98c42fa`](https://github.com/nodejs/node/commit/33a98c42fa)] - **doc**: update threat model based on discussions (Michael Dawson) [#46373](https://github.com/nodejs/node/pull/46373)
* \[[`9b23309f53`](https://github.com/nodejs/node/commit/9b23309f53)] - **doc,lib,src,test**: rename --test-coverage (Colin Ihrig) [#46017](https://github.com/nodejs/node/pull/46017)
* \[[`f192b83800`](https://github.com/nodejs/node/commit/f192b83800)] - **esm**: misc test refactors (Geoffrey Booth) [#46631](https://github.com/nodejs/node/pull/46631)
* \[[`7f2cdd36cf`](https://github.com/nodejs/node/commit/7f2cdd36cf)] - **http**: add note about clientError event (Paolo Insogna) [#46584](https://github.com/nodejs/node/pull/46584)
* \[[`d8c527f24f`](https://github.com/nodejs/node/commit/d8c527f24f)] - **http**: use v8::Array::New() with a prebuilt vector (Joyee Cheung) [#46447](https://github.com/nodejs/node/pull/46447)
* \[[`fa600fe003`](https://github.com/nodejs/node/commit/fa600fe003)] - **lib**: add trailing commas in `internal/process` (Antoine du Hamel) [#46687](https://github.com/nodejs/node/pull/46687)
* \[[`4aebee63f0`](https://github.com/nodejs/node/commit/4aebee63f0)] - **lib**: do not crash using workers with disabled shared array buffers (Ruben Bridgewater) [#41023](https://github.com/nodejs/node/pull/41023)
* \[[`a740908588`](https://github.com/nodejs/node/commit/a740908588)] - **lib**: delete module findPath unused params (sinkhaha) [#45371](https://github.com/nodejs/node/pull/45371)
* \[[`8b46c763d9`](https://github.com/nodejs/node/commit/8b46c763d9)] - **lib**: enforce use of trailing commas in more files (Antoine du Hamel) [#46655](https://github.com/nodejs/node/pull/46655)
* \[[`aae0020e27`](https://github.com/nodejs/node/commit/aae0020e27)] - **lib**: enforce use of trailing commas for functions (Antoine du Hamel) [#46629](https://github.com/nodejs/node/pull/46629)
* \[[`da9ebaf138`](https://github.com/nodejs/node/commit/da9ebaf138)] - **lib**: predeclare Event.isTrusted prop descriptor (Santiago Gimeno) [#46527](https://github.com/nodejs/node/pull/46527)
* \[[`35570e970e`](https://github.com/nodejs/node/commit/35570e970e)] - **lib**: tighten `AbortSignal.prototype.throwIfAborted` implementation (Antoine du Hamel) [#46521](https://github.com/nodejs/node/pull/46521)
* \[[`8590eb4830`](https://github.com/nodejs/node/commit/8590eb4830)] - **(SEMVER-MINOR)** **lib**: add aborted() utility function (Debadree Chatterjee) [#46494](https://github.com/nodejs/node/pull/46494)
* \[[`5d1a729f76`](https://github.com/nodejs/node/commit/5d1a729f76)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#46624](https://github.com/nodejs/node/pull/46624)
* \[[`cb9b9ad879`](https://github.com/nodejs/node/commit/cb9b9ad879)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#46513](https://github.com/nodejs/node/pull/46513)
* \[[`17b82c85d9`](https://github.com/nodejs/node/commit/17b82c85d9)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#46504](https://github.com/nodejs/node/pull/46504)
* \[[`bb14a2b098`](https://github.com/nodejs/node/commit/bb14a2b098)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#46411](https://github.com/nodejs/node/pull/46411)
* \[[`152a3c7d1d`](https://github.com/nodejs/node/commit/152a3c7d1d)] - **process**: print versions by sort (Himself65) [#46428](https://github.com/nodejs/node/pull/46428)
* \[[`164bfe82cc`](https://github.com/nodejs/node/commit/164bfe82cc)] - **(SEMVER-MINOR)** **src**: add initial support for single executable applications (Darshan Sen) [#45038](https://github.com/nodejs/node/pull/45038)
* \[[`f3908411fd`](https://github.com/nodejs/node/commit/f3908411fd)] - **(SEMVER-MINOR)** **src**: allow optional Isolate termination in node::Stop() (Shelley Vohr) [#46583](https://github.com/nodejs/node/pull/46583)
* \[[`bdba600d32`](https://github.com/nodejs/node/commit/bdba600d32)] - **src**: remove icu usage from node\_string.cc (Yagiz Nizipli) [#46548](https://github.com/nodejs/node/pull/46548)
* \[[`31fb2e22a0`](https://github.com/nodejs/node/commit/31fb2e22a0)] - **src**: add fflush() to SnapshotData::ToFile() (Anna Henningsen) [#46531](https://github.com/nodejs/node/pull/46531)
* \[[`c34bac2fed`](https://github.com/nodejs/node/commit/c34bac2fed)] - **(SEMVER-MINOR)** **src**: allow blobs in addition to `FILE*`s in embedder snapshot API (Anna Henningsen) [#46491](https://github.com/nodejs/node/pull/46491)
* \[[`c3325bfc0d`](https://github.com/nodejs/node/commit/c3325bfc0d)] - **src**: make edge names in BaseObjects more descriptive in heap snapshots (Joyee Cheung) [#46492](https://github.com/nodejs/node/pull/46492)
* \[[`3c5db8f419`](https://github.com/nodejs/node/commit/3c5db8f419)] - **src**: avoid leaking snapshot fp on error (Tobias Nießen) [#46497](https://github.com/nodejs/node/pull/46497)
* \[[`1a808a4aad`](https://github.com/nodejs/node/commit/1a808a4aad)] - **src**: check return value of ftell() (Tobias Nießen) [#46495](https://github.com/nodejs/node/pull/46495)
* \[[`f72f643549`](https://github.com/nodejs/node/commit/f72f643549)] - **src**: remove unused includes from main thread (Yagiz Nizipli) [#46471](https://github.com/nodejs/node/pull/46471)
* \[[`60c2a863da`](https://github.com/nodejs/node/commit/60c2a863da)] - **src**: use string\_view instead of std::string& (Yagiz Nizipli) [#46471](https://github.com/nodejs/node/pull/46471)
* \[[`f35f6d2218`](https://github.com/nodejs/node/commit/f35f6d2218)] - **src**: use simdutf utf8 to utf16 instead of icu (Yagiz Nizipli) [#46471](https://github.com/nodejs/node/pull/46471)
* \[[`00b81c7afe`](https://github.com/nodejs/node/commit/00b81c7afe)] - **src**: replace icu with simdutf for char counts (Yagiz Nizipli) [#46472](https://github.com/nodejs/node/pull/46472)
* \[[`683a1f8f3e`](https://github.com/nodejs/node/commit/683a1f8f3e)] - **(SEMVER-MINOR)** **src**: allow snapshotting from the embedder API (Anna Henningsen) [#45888](https://github.com/nodejs/node/pull/45888)
* \[[`658d2f4710`](https://github.com/nodejs/node/commit/658d2f4710)] - **(SEMVER-MINOR)** **src**: make build\_snapshot a per-Isolate option, rather than a global one (Anna Henningsen) [#45888](https://github.com/nodejs/node/pull/45888)
* \[[`6801d3753c`](https://github.com/nodejs/node/commit/6801d3753c)] - **(SEMVER-MINOR)** **src**: add snapshot support for embedder API (Anna Henningsen) [#45888](https://github.com/nodejs/node/pull/45888)
* \[[`95065c3185`](https://github.com/nodejs/node/commit/95065c3185)] - **src**: add additional utilities to crypto::SecureContext (James M Snell) [#45912](https://github.com/nodejs/node/pull/45912)
* \[[`efc59d0843`](https://github.com/nodejs/node/commit/efc59d0843)] - **src**: add KeyObjectHandle::HasInstance (James M Snell) [#45912](https://github.com/nodejs/node/pull/45912)
* \[[`a8a2d0e2b1`](https://github.com/nodejs/node/commit/a8a2d0e2b1)] - **src**: add GetCurrentCipherName/Version to crypto\_common (James M Snell) [#45912](https://github.com/nodejs/node/pull/45912)
* \[[`6cf860d3d6`](https://github.com/nodejs/node/commit/6cf860d3d6)] - **src**: back snapshot I/O with a std::vector sink (Joyee Cheung) [#46463](https://github.com/nodejs/node/pull/46463)
* \[[`e77d538d32`](https://github.com/nodejs/node/commit/e77d538d32)] - **(SEMVER-MINOR)** **src**: allow embedder control of code generation policy (Shelley Vohr) [#46368](https://github.com/nodejs/node/pull/46368)
* \[[`7756438c81`](https://github.com/nodejs/node/commit/7756438c81)] - **stream**: add trailing commas in webstream source files (Antoine du Hamel) [#46685](https://github.com/nodejs/node/pull/46685)
* \[[`6b64a945c6`](https://github.com/nodejs/node/commit/6b64a945c6)] - **stream**: add trailing commas in stream source files (Antoine du Hamel) [#46686](https://github.com/nodejs/node/pull/46686)
* \[[`633d3f292d`](https://github.com/nodejs/node/commit/633d3f292d)] - **(SEMVER-MINOR)** **stream**: add abort signal for ReadableStream and WritableStream (Debadree Chatterjee) [#46273](https://github.com/nodejs/node/pull/46273)
* \[[`f91260b32a`](https://github.com/nodejs/node/commit/f91260b32a)] - **stream**: refactor to use `validateAbortSignal` (Antoine du Hamel) [#46520](https://github.com/nodejs/node/pull/46520)
* \[[`6bf7388b62`](https://github.com/nodejs/node/commit/6bf7388b62)] - **stream**: allow transfer of readable byte streams (MrBBot) [#45955](https://github.com/nodejs/node/pull/45955)
* \[[`c2068537fa`](https://github.com/nodejs/node/commit/c2068537fa)] - **stream**: add pipeline() for webstreams (Debadree Chatterjee) [#46307](https://github.com/nodejs/node/pull/46307)
* \[[`4cf4b41c56`](https://github.com/nodejs/node/commit/4cf4b41c56)] - **stream**: add suport for abort signal in finished() for webstreams (Debadree Chatterjee) [#46403](https://github.com/nodejs/node/pull/46403)
* \[[`b844a09fa5`](https://github.com/nodejs/node/commit/b844a09fa5)] - **stream**: dont access Object.prototype.type during TransformStream init (Debadree Chatterjee) [#46389](https://github.com/nodejs/node/pull/46389)
* \[[`6ad01fd7b5`](https://github.com/nodejs/node/commit/6ad01fd7b5)] - **test**: fix `test-net-autoselectfamily` for kernel without IPv6 support (Livia Medeiros) [#45856](https://github.com/nodejs/node/pull/45856)
* \[[`2239e24306`](https://github.com/nodejs/node/commit/2239e24306)] - **test**: fix assertions in test-snapshot-dns-lookup\* (Tobias Nießen) [#46618](https://github.com/nodejs/node/pull/46618)
* \[[`c4ca98e786`](https://github.com/nodejs/node/commit/c4ca98e786)] - **test**: cover publicExponent validation in OpenSSL (Tobias Nießen) [#46632](https://github.com/nodejs/node/pull/46632)
* \[[`e60d3f2b1d`](https://github.com/nodejs/node/commit/e60d3f2b1d)] - **test**: add WPTRunner support for variants and generating WPT reports (Filip Skokan) [#46498](https://github.com/nodejs/node/pull/46498)
* \[[`217f2f6e2a`](https://github.com/nodejs/node/commit/217f2f6e2a)] - **test**: add trailing commas in `test/pummel` (Antoine du Hamel) [#46610](https://github.com/nodejs/node/pull/46610)
* \[[`641e1771c8`](https://github.com/nodejs/node/commit/641e1771c8)] - **test**: enable api-invalid-label.any.js in encoding WPTs (Filip Skokan) [#46506](https://github.com/nodejs/node/pull/46506)
* \[[`89aa161173`](https://github.com/nodejs/node/commit/89aa161173)] - **test**: fix tap parser fails if a test logs a number (Pulkit Gupta) [#46056](https://github.com/nodejs/node/pull/46056)
* \[[`faba8d4a30`](https://github.com/nodejs/node/commit/faba8d4a30)] - **test**: add trailing commas in `test/js-native-api` (Antoine du Hamel) [#46385](https://github.com/nodejs/node/pull/46385)
* \[[`d556ccdd26`](https://github.com/nodejs/node/commit/d556ccdd26)] - **test**: make more crypto tests work with BoringSSL (Shelley Vohr) [#46429](https://github.com/nodejs/node/pull/46429)
* \[[`c7f29b24a6`](https://github.com/nodejs/node/commit/c7f29b24a6)] - **test**: add trailing commas in `test/known_issues` (Antoine du Hamel) [#46408](https://github.com/nodejs/node/pull/46408)
* \[[`a66e7ca6c5`](https://github.com/nodejs/node/commit/a66e7ca6c5)] - **test**: add trailing commas in `test/internet` (Antoine du Hamel) [#46407](https://github.com/nodejs/node/pull/46407)
* \[[`0f75633086`](https://github.com/nodejs/node/commit/0f75633086)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#46575](https://github.com/nodejs/node/pull/46575)
* \[[`ddf5002782`](https://github.com/nodejs/node/commit/ddf5002782)] - **test\_runner**: parse non-ascii character correctly (Mert Can Altın) [#45736](https://github.com/nodejs/node/pull/45736)
* \[[`5b748114d2`](https://github.com/nodejs/node/commit/5b748114d2)] - **test\_runner**: allow nesting test within describe (Moshe Atlow) [#46544](https://github.com/nodejs/node/pull/46544)
* \[[`c526f9f70a`](https://github.com/nodejs/node/commit/c526f9f70a)] - **test\_runner**: fix missing test diagnostics (Moshe Atlow) [#46450](https://github.com/nodejs/node/pull/46450)
* \[[`b31aabb101`](https://github.com/nodejs/node/commit/b31aabb101)] - **test\_runner**: top-level diagnostics not ommited when running with --test (Pulkit Gupta) [#46441](https://github.com/nodejs/node/pull/46441)
* \[[`6119289251`](https://github.com/nodejs/node/commit/6119289251)] - **test\_runner**: add initial code coverage support (Colin Ihrig) [#46017](https://github.com/nodejs/node/pull/46017)
* \[[`6f24f0621e`](https://github.com/nodejs/node/commit/6f24f0621e)] - **timers**: cleanup no-longer relevant TODOs in timers/promises (James M Snell) [#46499](https://github.com/nodejs/node/pull/46499)
* \[[`1cd22e7d19`](https://github.com/nodejs/node/commit/1cd22e7d19)] - **tools**: fix bug in `prefer-primordials` lint rule (Antoine du Hamel) [#46659](https://github.com/nodejs/node/pull/46659)
* \[[`87df34ac28`](https://github.com/nodejs/node/commit/87df34ac28)] - **tools**: fix update-ada script (Yagiz Nizipli) [#46550](https://github.com/nodejs/node/pull/46550)
* \[[`f62b58a623`](https://github.com/nodejs/node/commit/f62b58a623)] - **tools**: add a daily wpt.fyi synchronized report upload (Filip Skokan) [#46498](https://github.com/nodejs/node/pull/46498)
* \[[`803f00aa32`](https://github.com/nodejs/node/commit/803f00aa32)] - **tools**: update eslint to 8.34.0 (Node.js GitHub Bot) [#46625](https://github.com/nodejs/node/pull/46625)
* \[[`f87216bdb2`](https://github.com/nodejs/node/commit/f87216bdb2)] - **tools**: update lint-md-dependencies to rollup\@3.15.0 to-vfile\@7.2.4 (Node.js GitHub Bot) [#46623](https://github.com/nodejs/node/pull/46623)
* \[[`8ee9e48560`](https://github.com/nodejs/node/commit/8ee9e48560)] - **tools**: update doc to remark-html\@15.0.2 to-vfile\@7.2.4 (Node.js GitHub Bot) [#46622](https://github.com/nodejs/node/pull/46622)
* \[[`148c5d9239`](https://github.com/nodejs/node/commit/148c5d9239)] - **tools**: update lint-md-dependencies to rollup\@3.13.0 vfile-reporter\@7.0.5 (Node.js GitHub Bot) [#46503](https://github.com/nodejs/node/pull/46503)
* \[[`51c6c61a58`](https://github.com/nodejs/node/commit/51c6c61a58)] - **tools**: update ESLint custom rules to not use the deprecated format (Antoine du Hamel) [#46460](https://github.com/nodejs/node/pull/46460)
* \[[`a51fe3c663`](https://github.com/nodejs/node/commit/a51fe3c663)] - **url**: replace url-parser with ada (Yagiz Nizipli) [#46410](https://github.com/nodejs/node/pull/46410)
* \[[`129c9e7180`](https://github.com/nodejs/node/commit/129c9e7180)] - **url**: remove unused `URL::ToFilePath()` (Yagiz Nizipli) [#46487](https://github.com/nodejs/node/pull/46487)
* \[[`9a604d67c3`](https://github.com/nodejs/node/commit/9a604d67c3)] - **url**: remove unused `URL::toObject` (Yagiz Nizipli) [#46486](https://github.com/nodejs/node/pull/46486)
* \[[`d6fbebda54`](https://github.com/nodejs/node/commit/d6fbebda54)] - **url**: remove unused `setURLConstructor` function (Yagiz Nizipli) [#46485](https://github.com/nodejs/node/pull/46485)
* \[[`17b3ee33c2`](https://github.com/nodejs/node/commit/17b3ee33c2)] - **vm**: properly support symbols on globals (Nicolas DUBIEN) [#46458](https://github.com/nodejs/node/pull/46458)

<a id="19.6.1"></a>

## 2023-02-16, Version 19.6.1 (Current), @RafaelGSS

This is a security release.

### Notable Changes

The following CVEs are fixed in this release:

* **[CVE-2023-23919](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-23919)**: OpenSSL errors not cleared in error stack (Medium)
* **[CVE-2023-23918](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-23918)**: Experimental Policies bypass via `process.mainModule.require`(High)
* **[CVE-2023-23920](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-23920)**: Insecure loading of ICU data through ICU\_DATA environment variable (Low)

More detailed information on each of the vulnerabilities can be found in [February 2023 Security Releases](https://nodejs.org/en/blog/vulnerability/february-2023-security-releases/) blog post.

This security release includes OpenSSL security updates as outlined in the recent
[OpenSSL security advisory](https://www.openssl.org/news/secadv/20230207.txt) and `undici` security update.

### Commits

* \[[`97d9d55d2f`](https://github.com/nodejs/node/commit/97d9d55d2f)] - **build**: build ICU with ICU\_NO\_USER\_DATA\_OVERRIDE (RafaelGSS) [nodejs-private/node-private#374](https://github.com/nodejs-private/node-private/pull/374)
* \[[`8ac90e6372`](https://github.com/nodejs/node/commit/8ac90e6372)] - **crypto**: clear OpenSSL error on invalid ca cert (RafaelGSS) [nodejs-private/node-private#368](https://github.com/nodejs-private/node-private/pull/368)
* \[[`10a4c47e3a`](https://github.com/nodejs/node/commit/10a4c47e3a)] - **deps**: update undici to 5.19.1 (Node.js GitHub Bot) [#46634](https://github.com/nodejs/node/pull/46634)
* \[[`b10fc75e4a`](https://github.com/nodejs/node/commit/b10fc75e4a)] - **deps**: update undici to 5.18.0 (Node.js GitHub Bot) [#46502](https://github.com/nodejs/node/pull/46502)
* \[[`e9b64ea8b9`](https://github.com/nodejs/node/commit/e9b64ea8b9)] - **deps**: update undici to 5.17.1 (Node.js GitHub Bot) [#46502](https://github.com/nodejs/node/pull/46502)
* \[[`66a24cec47`](https://github.com/nodejs/node/commit/66a24cec47)] - **deps**: cherry-pick Windows ARM64 fix for openssl (Richard Lau) [#46573](https://github.com/nodejs/node/pull/46573)
* \[[`d8559aa6f5`](https://github.com/nodejs/node/commit/d8559aa6f5)] - **deps**: update archs files for quictls/openssl-3.0.8+quic (RafaelGSS) [#46573](https://github.com/nodejs/node/pull/46573)
* \[[`dc477f547d`](https://github.com/nodejs/node/commit/dc477f547d)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.8+quic (RafaelGSS) [#46573](https://github.com/nodejs/node/pull/46573)
* \[[`2aae197670`](https://github.com/nodejs/node/commit/2aae197670)] - **lib**: makeRequireFunction patch when experimental policy (RafaelGSS) [nodejs-private/node-private#358](https://github.com/nodejs-private/node-private/pull/358)
* \[[`6d17b693ec`](https://github.com/nodejs/node/commit/6d17b693ec)] - **policy**: makeRequireFunction on mainModule.require (RafaelGSS) [nodejs-private/node-private#358](https://github.com/nodejs-private/node-private/pull/358)

<a id="19.6.0"></a>

## 2023-02-02, Version 19.6.0 (Current), @ruyadorno

### Notable changes

#### ESM: Leverage loaders when resolving subsequent loaders

Loaders now apply to subsequent loaders, for example: `--experimental-loader ts-node --experimental-loader loader-written-in-typescript`.

#### Upgrade npm to 9.4.0

Added `--install-strategy=linked` option for installations similar to pnpm.

#### Other notable changes

* \[[`a7c9daa497`](https://github.com/nodejs/node/commit/a7c9daa497)] - **(SEMVER-MINOR)** **fs**: add statfs() functions (Colin Ihrig) [#46358](https://github.com/nodejs/node/pull/46358)
* \[[`34d70ce615`](https://github.com/nodejs/node/commit/34d70ce615)] - **(SEMVER-MINOR)** **vm**: expose cachedDataRejected for vm.compileFunction (Anna Henningsen) [#46320](https://github.com/nodejs/node/pull/46320)
* \[[`b4ac794923`](https://github.com/nodejs/node/commit/b4ac794923)] - **(SEMVER-MINOR)** **v8**: support gc profile (theanarkh) [#46255](https://github.com/nodejs/node/pull/46255)
* \[[`d52f60009a`](https://github.com/nodejs/node/commit/d52f60009a)] - **(SEMVER-MINOR)** **src,lib**: add constrainedMemory API for process (theanarkh) [#46218](https://github.com/nodejs/node/pull/46218)
* \[[`5ad6c2088e`](https://github.com/nodejs/node/commit/5ad6c2088e)] - **(SEMVER-MINOR)** **buffer**: add isAscii method (Yagiz Nizipli) [#46046](https://github.com/nodejs/node/pull/46046)
* \[[`fbdc3f7316`](https://github.com/nodejs/node/commit/fbdc3f7316)] - **(SEMVER-MINOR)** **test\_runner**: add reporters (Moshe Atlow) [#45712](https://github.com/nodejs/node/pull/45712)

### Commits

* \[[`524eec70e2`](https://github.com/nodejs/node/commit/524eec70e2)] - **benchmark**: add trailing commas (Antoine du Hamel) [#46370](https://github.com/nodejs/node/pull/46370)
* \[[`f318a85408`](https://github.com/nodejs/node/commit/f318a85408)] - **benchmark**: remove buffer benchmarks redundancy (Brian White) [#45735](https://github.com/nodejs/node/pull/45735)
* \[[`6186b3ea14`](https://github.com/nodejs/node/commit/6186b3ea14)] - **benchmark**: introduce benchmark combination filtering (Brian White) [#45735](https://github.com/nodejs/node/pull/45735)
* \[[`5ad6c2088e`](https://github.com/nodejs/node/commit/5ad6c2088e)] - **(SEMVER-MINOR)** **buffer**: add isAscii method (Yagiz Nizipli) [#46046](https://github.com/nodejs/node/pull/46046)
* \[[`8c6c4338a6`](https://github.com/nodejs/node/commit/8c6c4338a6)] - **build**: export more OpenSSL symbols on Windows (Mohamed Akram) [#45486](https://github.com/nodejs/node/pull/45486)
* \[[`d795d93901`](https://github.com/nodejs/node/commit/d795d93901)] - **build**: fix MSVC 2022 Release compilation (Vladimir Morozov (REDMOND)) [#46228](https://github.com/nodejs/node/pull/46228)
* \[[`8e363cf8e8`](https://github.com/nodejs/node/commit/8e363cf8e8)] - **crypto**: include `hmac.h` in `crypto_util.h` (Adam Langley) [#46279](https://github.com/nodejs/node/pull/46279)
* \[[`c1f3e13c65`](https://github.com/nodejs/node/commit/c1f3e13c65)] - **deps**: update acorn to 8.8.2 (Node.js GitHub Bot) [#46363](https://github.com/nodejs/node/pull/46363)
* \[[`813b160bd7`](https://github.com/nodejs/node/commit/813b160bd7)] - **deps**: upgrade npm to 9.4.0 (npm team) [#46353](https://github.com/nodejs/node/pull/46353)
* \[[`9c2f3cea70`](https://github.com/nodejs/node/commit/9c2f3cea70)] - **deps**: update undici to 5.15.0 (Node.js GitHub Bot) [#46213](https://github.com/nodejs/node/pull/46213)
* \[[`312e10c1e3`](https://github.com/nodejs/node/commit/312e10c1e3)] - **deps**: update to uvwasi 0.0.15 (Colin Ihrig) [#46253](https://github.com/nodejs/node/pull/46253)
* \[[`c7024eec16`](https://github.com/nodejs/node/commit/c7024eec16)] - **doc**: correct the `sed` command for macOS in release process docs (Juan José) [#46397](https://github.com/nodejs/node/pull/46397)
* \[[`996bac044b`](https://github.com/nodejs/node/commit/996bac044b)] - **doc**: include webstreams in finished() and Duplex.from() parameters (Debadree Chatterjee) [#46312](https://github.com/nodejs/node/pull/46312)
* \[[`891d18d55c`](https://github.com/nodejs/node/commit/891d18d55c)] - **doc**: pass string to `textEncoder.encode` as input (Deokjin Kim) [#46421](https://github.com/nodejs/node/pull/46421)
* \[[`968db213f8`](https://github.com/nodejs/node/commit/968db213f8)] - **doc**: add tip for session.post function (theanarkh) [#46354](https://github.com/nodejs/node/pull/46354)
* \[[`a64d7f4e31`](https://github.com/nodejs/node/commit/a64d7f4e31)] - **doc**: add documentation for socket.destroySoon() (Luigi Pinca) [#46337](https://github.com/nodejs/node/pull/46337)
* \[[`975788899f`](https://github.com/nodejs/node/commit/975788899f)] - **doc**: fix commit message using test instead of deps (Tony Gorez) [#46313](https://github.com/nodejs/node/pull/46313)
* \[[`1d44017f52`](https://github.com/nodejs/node/commit/1d44017f52)] - **doc**: add v8 fast api contribution guidelines (Yagiz Nizipli) [#46199](https://github.com/nodejs/node/pull/46199)
* \[[`e2698c05fb`](https://github.com/nodejs/node/commit/e2698c05fb)] - **doc**: fix small typo error (0xflotus) [#46186](https://github.com/nodejs/node/pull/46186)
* \[[`f39fb8c001`](https://github.com/nodejs/node/commit/f39fb8c001)] - **doc**: mark some parameters as optional in webstreams (Deokjin Kim) [#46269](https://github.com/nodejs/node/pull/46269)
* \[[`7a9af38128`](https://github.com/nodejs/node/commit/7a9af38128)] - **doc**: update output of example in `events.getEventListeners` (Deokjin Kim) [#46268](https://github.com/nodejs/node/pull/46268)
* \[[`729642f30b`](https://github.com/nodejs/node/commit/729642f30b)] - **esm**: delete preload mock test (Geoffrey Booth) [#46402](https://github.com/nodejs/node/pull/46402)
* \[[`7aac21e90a`](https://github.com/nodejs/node/commit/7aac21e90a)] - **esm**: leverage loaders when resolving subsequent loaders (Maël Nison) [#43772](https://github.com/nodejs/node/pull/43772)
* \[[`a7c9daa497`](https://github.com/nodejs/node/commit/a7c9daa497)] - **(SEMVER-MINOR)** **fs**: add statfs() functions (Colin Ihrig) [#46358](https://github.com/nodejs/node/pull/46358)
* \[[`1ec6270efa`](https://github.com/nodejs/node/commit/1ec6270efa)] - **http**: res.setHeaders first implementation (Marco Ippolito) [#46109](https://github.com/nodejs/node/pull/46109)
* \[[`d4370259e9`](https://github.com/nodejs/node/commit/d4370259e9)] - **inspector**: allow opening inspector when `NODE_V8_COVERAGE` is set (Moshe Atlow) [#46113](https://github.com/nodejs/node/pull/46113)
* \[[`b966ef9a42`](https://github.com/nodejs/node/commit/b966ef9a42)] - **lib**: remove unnecessary ObjectGetValueSafe (Chengzhong Wu) [#46335](https://github.com/nodejs/node/pull/46335)
* \[[`2b06d66289`](https://github.com/nodejs/node/commit/2b06d66289)] - **lib**: cache parsed source maps to reduce memory footprint (Chengzhong Wu) [#46225](https://github.com/nodejs/node/pull/46225)
* \[[`c38673df91`](https://github.com/nodejs/node/commit/c38673df91)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#46399](https://github.com/nodejs/node/pull/46399)
* \[[`c10e602547`](https://github.com/nodejs/node/commit/c10e602547)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#46303](https://github.com/nodejs/node/pull/46303)
* \[[`9dc026b14a`](https://github.com/nodejs/node/commit/9dc026b14a)] - **meta**: add .mailmap entry (Rich Trott) [#46303](https://github.com/nodejs/node/pull/46303)
* \[[`7c514574f7`](https://github.com/nodejs/node/commit/7c514574f7)] - **meta**: move evanlucas to emeritus (Evan Lucas) [#46274](https://github.com/nodejs/node/pull/46274)
* \[[`3a3a6d87f1`](https://github.com/nodejs/node/commit/3a3a6d87f1)] - **module**: move test reporter loading (Geoffrey Booth) [#45923](https://github.com/nodejs/node/pull/45923)
* \[[`4ae2492a33`](https://github.com/nodejs/node/commit/4ae2492a33)] - **readline**: fix detection of carriage return (Antoine du Hamel) [#46306](https://github.com/nodejs/node/pull/46306)
* \[[`43cad78b7a`](https://github.com/nodejs/node/commit/43cad78b7a)] - **src**: stop tracing agent before shutting down libuv (Santiago Gimeno) [#46380](https://github.com/nodejs/node/pull/46380)
* \[[`360a3f3094`](https://github.com/nodejs/node/commit/360a3f3094)] - **src**: get rid of fp arithmetic in ParseIPv4Host (Tobias Nießen) [#46326](https://github.com/nodejs/node/pull/46326)
* \[[`e7b507a8cf`](https://github.com/nodejs/node/commit/e7b507a8cf)] - **src**: use UNREACHABLE instead of CHECK(falsy) (Tobias Nießen) [#46317](https://github.com/nodejs/node/pull/46317)
* \[[`4c59b60ee8`](https://github.com/nodejs/node/commit/4c59b60ee8)] - **src**: add support for ETW stack walking (José Dapena Paz) [#46203](https://github.com/nodejs/node/pull/46203)
* \[[`640d111f95`](https://github.com/nodejs/node/commit/640d111f95)] - **src**: refactor EndsInANumber in node\_url.cc and adds IsIPv4NumberValid (Miguel Teixeira) [#46227](https://github.com/nodejs/node/pull/46227)
* \[[`fb7bee2b6e`](https://github.com/nodejs/node/commit/fb7bee2b6e)] - **src**: fix c++ exception on bad command line arg (Ben Noordhuis) [#46290](https://github.com/nodejs/node/pull/46290)
* \[[`18c95ec4bd`](https://github.com/nodejs/node/commit/18c95ec4bd)] - **src**: remove unreachable UNREACHABLE (Tobias Nießen) [#46281](https://github.com/nodejs/node/pull/46281)
* \[[`35bf93b01a`](https://github.com/nodejs/node/commit/35bf93b01a)] - **src**: replace custom ASCII validation with simdutf one (Anna Henningsen) [#46271](https://github.com/nodejs/node/pull/46271)
* \[[`8307a4bbcd`](https://github.com/nodejs/node/commit/8307a4bbcd)] - **src**: replace unreachable code with static\_assert (Tobias Nießen) [#46250](https://github.com/nodejs/node/pull/46250)
* \[[`7cf0da020a`](https://github.com/nodejs/node/commit/7cf0da020a)] - **src**: use explicit C++17 fallthrough (Tobias Nießen) [#46251](https://github.com/nodejs/node/pull/46251)
* \[[`d52f60009a`](https://github.com/nodejs/node/commit/d52f60009a)] - **(SEMVER-MINOR)** **src,lib**: add constrainedMemory API for process (theanarkh) [#46218](https://github.com/nodejs/node/pull/46218)
* \[[`2e5e7a9261`](https://github.com/nodejs/node/commit/2e5e7a9261)] - **stream**: remove brandchecks from stream duplexify (Debadree Chatterjee) [#46315](https://github.com/nodejs/node/pull/46315)
* \[[`9675863461`](https://github.com/nodejs/node/commit/9675863461)] - **stream**: fix readable stream as async iterator function (Erick Wendel) [#46147](https://github.com/nodejs/node/pull/46147)
* \[[`232bdd5d16`](https://github.com/nodejs/node/commit/232bdd5d16)] - **test**: add trailing commas in `test/node-api` (Antoine du Hamel) [#46384](https://github.com/nodejs/node/pull/46384)
* \[[`4cc081815d`](https://github.com/nodejs/node/commit/4cc081815d)] - **test**: add trailing commas in `test/message` (Antoine du Hamel) [#46372](https://github.com/nodejs/node/pull/46372)
* \[[`b83c5d9deb`](https://github.com/nodejs/node/commit/b83c5d9deb)] - **test**: add trailing commas in `test/pseudo-tty` (Antoine du Hamel) [#46371](https://github.com/nodejs/node/pull/46371)
* \[[`8a45c9d231`](https://github.com/nodejs/node/commit/8a45c9d231)] - **test**: fix tap escaping with and without --test (Pulkit Gupta) [#46311](https://github.com/nodejs/node/pull/46311)
* \[[`367dc41299`](https://github.com/nodejs/node/commit/367dc41299)] - **test**: set common.bits to 64 for loong64 (Shi Pujin) [#45383](https://github.com/nodejs/node/pull/45383)
* \[[`7385edc7d0`](https://github.com/nodejs/node/commit/7385edc7d0)] - **test**: s390x zlib test case fixes (Adam Majer) [#46367](https://github.com/nodejs/node/pull/46367)
* \[[`d5d837bdee`](https://github.com/nodejs/node/commit/d5d837bdee)] - **test**: fix logInTimeout is not function (theanarkh) [#46348](https://github.com/nodejs/node/pull/46348)
* \[[`a1d79546ac`](https://github.com/nodejs/node/commit/a1d79546ac)] - **test**: avoid trying to call sysctl directly (Adam Majer) [#46366](https://github.com/nodejs/node/pull/46366)
* \[[`747f3689e0`](https://github.com/nodejs/node/commit/747f3689e0)] - **test**: avoid left behind child processes (Richard Lau) [#46276](https://github.com/nodejs/node/pull/46276)
* \[[`940484b7aa`](https://github.com/nodejs/node/commit/940484b7aa)] - **test**: add failing test for readline with carriage return (Alec Mev) [#46075](https://github.com/nodejs/node/pull/46075)
* \[[`d13116a719`](https://github.com/nodejs/node/commit/d13116a719)] - **test,crypto**: add CFRG curve vectors to wrap/unwrap tests (Filip Skokan) [#46406](https://github.com/nodejs/node/pull/46406)
* \[[`398a7477b3`](https://github.com/nodejs/node/commit/398a7477b3)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#46267](https://github.com/nodejs/node/pull/46267)
* \[[`8b473affe8`](https://github.com/nodejs/node/commit/8b473affe8)] - **test\_runner**: make built in reporters internal (Colin Ihrig) [#46092](https://github.com/nodejs/node/pull/46092)
* \[[`a49e17e22b`](https://github.com/nodejs/node/commit/a49e17e22b)] - **test\_runner**: report `file` in test runner events (Moshe Atlow) [#46030](https://github.com/nodejs/node/pull/46030)
* \[[`fbdc3f7316`](https://github.com/nodejs/node/commit/fbdc3f7316)] - **test\_runner**: add reporters (Moshe Atlow) [#45712](https://github.com/nodejs/node/pull/45712)
* \[[`6579de8c47`](https://github.com/nodejs/node/commit/6579de8c47)] - **tools**: update eslint to 8.33.0 (Node.js GitHub Bot) [#46400](https://github.com/nodejs/node/pull/46400)
* \[[`bf62da55ad`](https://github.com/nodejs/node/commit/bf62da55ad)] - **tools**: update doc to unist-util-select\@4.0.3 unist-util-visit\@4.1.2 (Node.js GitHub Bot) [#46364](https://github.com/nodejs/node/pull/46364)
* \[[`b0acf55197`](https://github.com/nodejs/node/commit/b0acf55197)] - **tools**: update lint-md-dependencies to rollup\@3.12.0 (Node.js GitHub Bot) [#46398](https://github.com/nodejs/node/pull/46398)
* \[[`88b904cf24`](https://github.com/nodejs/node/commit/88b904cf24)] - **tools**: require more trailing commas (Antoine du Hamel) [#46346](https://github.com/nodejs/node/pull/46346)
* \[[`4440b3ef87`](https://github.com/nodejs/node/commit/4440b3ef87)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#46302](https://github.com/nodejs/node/pull/46302)
* \[[`e75faff4bd`](https://github.com/nodejs/node/commit/e75faff4bd)] - **tools**: allow icutrim.py to run on python2 (Michael Dawson) [#46263](https://github.com/nodejs/node/pull/46263)
* \[[`e460d16d73`](https://github.com/nodejs/node/commit/e460d16d73)] - **url**: refactor to use more primordials (Antoine du Hamel) [#45966](https://github.com/nodejs/node/pull/45966)
* \[[`b4ac794923`](https://github.com/nodejs/node/commit/b4ac794923)] - **(SEMVER-MINOR)** **v8**: support gc profile (theanarkh) [#46255](https://github.com/nodejs/node/pull/46255)
* \[[`34d70ce615`](https://github.com/nodejs/node/commit/34d70ce615)] - **(SEMVER-MINOR)** **vm**: expose cachedDataRejected for vm.compileFunction (Anna Henningsen) [#46320](https://github.com/nodejs/node/pull/46320)

<a id="19.5.0"></a>

## 2023-01-24, Version 19.5.0 (Current), @RafaelGSS

### Notable Changes

* **http**:
  * (SEMVER-MINOR) join authorization headers (Marco Ippolito) [#45982](https://github.com/nodejs/node/pull/45982)
* **lib:**:
  * add webstreams to Duplex.from() (Debadree Chatterjee) [#46190](https://github.com/nodejs/node/pull/46190)
* **stream**:
  * implement finished() for ReadableStream and WritableStream (Debadree Chatterjee) [#46205](https://github.com/nodejs/node/pull/46205)

### Commits

* \[[`def36946da`](https://github.com/nodejs/node/commit/def36946da)] - **assert**: remove `assert.snapshot` (Moshe Atlow) [#46112](https://github.com/nodejs/node/pull/46112)
* \[[`e1c56ec3fd`](https://github.com/nodejs/node/commit/e1c56ec3fd)] - **benchmark,tools**: use os.availableParallelism() (Deokjin Kim) [#46003](https://github.com/nodejs/node/pull/46003)
* \[[`370f621d4d`](https://github.com/nodejs/node/commit/370f621d4d)] - **build**: add extra semi check (Jiawen Geng) [#46194](https://github.com/nodejs/node/pull/46194)
* \[[`476c6f892d`](https://github.com/nodejs/node/commit/476c6f892d)] - **crypto**: avoid hang when no algorithm available (Richard Lau) [#46237](https://github.com/nodejs/node/pull/46237)
* \[[`8b22310940`](https://github.com/nodejs/node/commit/8b22310940)] - **(SEMVER-MINOR)** **crypto**: add CryptoKey Symbol.toStringTag (Filip Skokan) [#46042](https://github.com/nodejs/node/pull/46042)
* \[[`78be87b9f9`](https://github.com/nodejs/node/commit/78be87b9f9)] - **crypto**: add cipher update/final methods encoding validation (vitpavlenko) [#45990](https://github.com/nodejs/node/pull/45990)
* \[[`dc0cdaa101`](https://github.com/nodejs/node/commit/dc0cdaa101)] - **crypto**: ensure auth tag set for chacha20-poly1305 (Ben Noordhuis) [#46185](https://github.com/nodejs/node/pull/46185)
* \[[`1146f02dc5`](https://github.com/nodejs/node/commit/1146f02dc5)] - **crypto**: return correct bit length in KeyObject's asymmetricKeyDetails (Filip Skokan) [#46106](https://github.com/nodejs/node/pull/46106)
* \[[`961710bb72`](https://github.com/nodejs/node/commit/961710bb72)] - **(SEMVER-MINOR)** **crypto**: add KeyObject Symbol.toStringTag (Filip Skokan) [#46043](https://github.com/nodejs/node/pull/46043)
* \[[`9cfdac6c82`](https://github.com/nodejs/node/commit/9cfdac6c82)] - **deps**: V8: cherry-pick e39af94dd18e (Lu Yahan) [#46142](https://github.com/nodejs/node/pull/46142)
* \[[`26cde8efb7`](https://github.com/nodejs/node/commit/26cde8efb7)] - **deps**: update simdutf to 3.1.0 (Node.js GitHub Bot) [#46257](https://github.com/nodejs/node/pull/46257)
* \[[`3f9fb37130`](https://github.com/nodejs/node/commit/3f9fb37130)] - **deps**: cherrypick simdutf patch (Jiawen Geng) [#46194](https://github.com/nodejs/node/pull/46194)
* \[[`4ff2822836`](https://github.com/nodejs/node/commit/4ff2822836)] - **deps**: bump googletest to 2023.01.13 (Jiawen Geng) [#46198](https://github.com/nodejs/node/pull/46198)
* \[[`49556247d2`](https://github.com/nodejs/node/commit/49556247d2)] - **deps**: add /deps/\*\*/.github/ to .gitignore (Luigi Pinca) [#46091](https://github.com/nodejs/node/pull/46091)
* \[[`0c4df83e0d`](https://github.com/nodejs/node/commit/0c4df83e0d)] - **deps**: add simdutf version to metadata (Mike Roth) [#46145](https://github.com/nodejs/node/pull/46145)
* \[[`69aafc3ddd`](https://github.com/nodejs/node/commit/69aafc3ddd)] - **deps**: update simdutf to 2.1.0 (Node.js GitHub Bot) [#46128](https://github.com/nodejs/node/pull/46128)
* \[[`a266daccb5`](https://github.com/nodejs/node/commit/a266daccb5)] - **deps**: update corepack to 0.15.3 (Node.js GitHub Bot) [#46037](https://github.com/nodejs/node/pull/46037)
* \[[`6cd70573eb`](https://github.com/nodejs/node/commit/6cd70573eb)] - **deps**: upgrade npm to 9.3.1 (npm team) [#46242](https://github.com/nodejs/node/pull/46242)
* \[[`679aae2da8`](https://github.com/nodejs/node/commit/679aae2da8)] - **deps**: upgrade npm to 9.3.0 (npm team) [#46193](https://github.com/nodejs/node/pull/46193)
* \[[`38dd5061f2`](https://github.com/nodejs/node/commit/38dd5061f2)] - **dgram**: sync the old handle state to new handle (theanarkh) [#46041](https://github.com/nodejs/node/pull/46041)
* \[[`e36af49b35`](https://github.com/nodejs/node/commit/e36af49b35)] - **doc**: fix mismatched arguments of `NodeEventTarget` (Deokjin Kim) [#45678](https://github.com/nodejs/node/pull/45678)
* \[[`58b836f7c4`](https://github.com/nodejs/node/commit/58b836f7c4)] - **doc**: update events API example to have runnable code (Deokjin Kim) [#45760](https://github.com/nodejs/node/pull/45760)
* \[[`5c350298b4`](https://github.com/nodejs/node/commit/5c350298b4)] - **doc**: add note to tls docs about secureContext availability (Tim Gerk) [#46224](https://github.com/nodejs/node/pull/46224)
* \[[`90924ce198`](https://github.com/nodejs/node/commit/90924ce198)] - **doc**: add text around collaborative expectations (Michael Dawson) [#46121](https://github.com/nodejs/node/pull/46121)
* \[[`2d328355d4`](https://github.com/nodejs/node/commit/2d328355d4)] - **doc**: update to match changed `--dns-result-order` default (Mordy Tikotzky) [#46148](https://github.com/nodejs/node/pull/46148)
* \[[`1015a606b7`](https://github.com/nodejs/node/commit/1015a606b7)] - **doc**: add Node-API media link (Kevin Eady) [#46189](https://github.com/nodejs/node/pull/46189)
* \[[`6e355efcff`](https://github.com/nodejs/node/commit/6e355efcff)] - **doc**: update http.setMaxIdleHTTPParsers arguments (Debadree Chatterjee) [#46168](https://github.com/nodejs/node/pull/46168)
* \[[`f18ab9405a`](https://github.com/nodejs/node/commit/f18ab9405a)] - **doc**: use "file system" instead of "filesystem" (Rich Trott) [#46178](https://github.com/nodejs/node/pull/46178)
* \[[`1b45713b00`](https://github.com/nodejs/node/commit/1b45713b00)] - **doc**: https update default request timeout (Marco Ippolito) [#46184](https://github.com/nodejs/node/pull/46184)
* \[[`4c88721e2f`](https://github.com/nodejs/node/commit/4c88721e2f)] - **doc**: make options of readableStream.pipeTo as optional (Deokjin Kim) [#46180](https://github.com/nodejs/node/pull/46180)
* \[[`538c53f010`](https://github.com/nodejs/node/commit/538c53f010)] - **doc**: add PerformanceObserver.supportedEntryTypes to doc (theanarkh) [#45962](https://github.com/nodejs/node/pull/45962)
* \[[`eef7489d24`](https://github.com/nodejs/node/commit/eef7489d24)] - **doc**: duplex and readable from uncaught execption warning (Marco Ippolito) [#46135](https://github.com/nodejs/node/pull/46135)
* \[[`686fe585b5`](https://github.com/nodejs/node/commit/686fe585b5)] - **doc**: remove outdated sections from `maintaining-v8` (Antoine du Hamel) [#46137](https://github.com/nodejs/node/pull/46137)
* \[[`2e826ad528`](https://github.com/nodejs/node/commit/2e826ad528)] - **doc**: fix (EC)DHE remark in TLS docs (Tobias Nießen) [#46114](https://github.com/nodejs/node/pull/46114)
* \[[`2e22b29add`](https://github.com/nodejs/node/commit/2e22b29add)] - **doc**: fix ERR\_TLS\_RENEGOTIATION\_DISABLED text (Tobias Nießen) [#46122](https://github.com/nodejs/node/pull/46122)
* \[[`e222a2f1d1`](https://github.com/nodejs/node/commit/e222a2f1d1)] - **doc**: fix spelling in SECURITY.md (Vaishno Chaitanya) [#46124](https://github.com/nodejs/node/pull/46124)
* \[[`7718e82f0d`](https://github.com/nodejs/node/commit/7718e82f0d)] - **doc**: abort controller emits error in child process (Debadree Chatterjee) [#46072](https://github.com/nodejs/node/pull/46072)
* \[[`76408bc1ed`](https://github.com/nodejs/node/commit/76408bc1ed)] - **doc**: fix `event.cancelBubble` documentation (Deokjin Kim) [#45986](https://github.com/nodejs/node/pull/45986)
* \[[`82023f2570`](https://github.com/nodejs/node/commit/82023f2570)] - **doc**: update output of example in inspector (Deokjin Kim) [#46073](https://github.com/nodejs/node/pull/46073)
* \[[`a42fc512b6`](https://github.com/nodejs/node/commit/a42fc512b6)] - **doc**: add personal pronouns option (Filip Skokan) [#46118](https://github.com/nodejs/node/pull/46118)
* \[[`fafae5955d`](https://github.com/nodejs/node/commit/fafae5955d)] - **doc**: mention how to run ncu-ci citgm (Rafael Gonzaga) [#46090](https://github.com/nodejs/node/pull/46090)
* \[[`e1fd2f24d9`](https://github.com/nodejs/node/commit/e1fd2f24d9)] - **doc**: include updating release optional step (Rafael Gonzaga) [#46089](https://github.com/nodejs/node/pull/46089)
* \[[`1996e610fd`](https://github.com/nodejs/node/commit/1996e610fd)] - **doc**: describe argument of `Symbol.for` (Deokjin Kim) [#46019](https://github.com/nodejs/node/pull/46019)
* \[[`b002330216`](https://github.com/nodejs/node/commit/b002330216)] - **doc,crypto**: fix WebCryptoAPI import keyData and export return (Filip Skokan) [#46076](https://github.com/nodejs/node/pull/46076)
* \[[`fa3e0c86c7`](https://github.com/nodejs/node/commit/fa3e0c86c7)] - **esm**: mark `importAssertions` as required (Antoine du Hamel) [#46164](https://github.com/nodejs/node/pull/46164)
* \[[`f85a8e4c59`](https://github.com/nodejs/node/commit/f85a8e4c59)] - **events**: add `initEvent` to Event (Deokjin Kim) [#46069](https://github.com/nodejs/node/pull/46069)
* \[[`5bdfaae680`](https://github.com/nodejs/node/commit/5bdfaae680)] - **events**: change status of `event.returnvalue` to legacy (Deokjin Kim) [#46175](https://github.com/nodejs/node/pull/46175)
* \[[`ad7846fe97`](https://github.com/nodejs/node/commit/ad7846fe97)] - **events**: change status of `event.cancelBubble` to legacy (Deokjin Kim) [#46146](https://github.com/nodejs/node/pull/46146)
* \[[`5304c89682`](https://github.com/nodejs/node/commit/5304c89682)] - **events**: change status of `event.srcElement` to legacy (Deokjin Kim) [#46085](https://github.com/nodejs/node/pull/46085)
* \[[`3dcdab3f16`](https://github.com/nodejs/node/commit/3dcdab3f16)] - **events**: check signal before listener (Deokjin Kim) [#46054](https://github.com/nodejs/node/pull/46054)
* \[[`907d67de76`](https://github.com/nodejs/node/commit/907d67de76)] - **http**: refactor to use `validateHeaderName` (Deokjin Kim) [#46143](https://github.com/nodejs/node/pull/46143)
* \[[`ae5141cb8a`](https://github.com/nodejs/node/commit/ae5141cb8a)] - **http**: writeHead if statusmessage is undefined dont override headers (Marco Ippolito) [#46173](https://github.com/nodejs/node/pull/46173)
* \[[`6e7f9fbc1d`](https://github.com/nodejs/node/commit/6e7f9fbc1d)] - **http**: refactor to use min of validateNumber for maxTotalSockets (Deokjin Kim) [#46115](https://github.com/nodejs/node/pull/46115)
* \[[`069a30bc4e`](https://github.com/nodejs/node/commit/069a30bc4e)] - **(SEMVER-MINOR)** **http**: join authorization headers (Marco Ippolito) [#45982](https://github.com/nodejs/node/pull/45982)
* \[[`68cde4cbcc`](https://github.com/nodejs/node/commit/68cde4cbcc)] - **lib**: add webstreams to Duplex.from() (Debadree Chatterjee) [#46190](https://github.com/nodejs/node/pull/46190)
* \[[`4d73ea708b`](https://github.com/nodejs/node/commit/4d73ea708b)] - **lib**: use kEmptyObject and update JSDoc in webstreams (Deokjin Kim) [#46183](https://github.com/nodejs/node/pull/46183)
* \[[`1cfa2e6762`](https://github.com/nodejs/node/commit/1cfa2e6762)] - **lib**: refactor to use validate function (Deokjin Kim) [#46101](https://github.com/nodejs/node/pull/46101)
* \[[`2eb87f23c9`](https://github.com/nodejs/node/commit/2eb87f23c9)] - **lib**: reuse invalid state errors on webstreams (Rafael Gonzaga) [#46086](https://github.com/nodejs/node/pull/46086)
* \[[`8684dae8d9`](https://github.com/nodejs/node/commit/8684dae8d9)] - **lib**: fix incorrect use of console intrinsic (Colin Ihrig) [#46044](https://github.com/nodejs/node/pull/46044)
* \[[`d044ed1d3e`](https://github.com/nodejs/node/commit/d044ed1d3e)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#46215](https://github.com/nodejs/node/pull/46215)
* \[[`5261560757`](https://github.com/nodejs/node/commit/5261560757)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#46130](https://github.com/nodejs/node/pull/46130)
* \[[`1b557bbee8`](https://github.com/nodejs/node/commit/1b557bbee8)] - **meta**: update comment in `CODEOWNERS` to better reflect current policy (Antoine du Hamel) [#45944](https://github.com/nodejs/node/pull/45944)
* \[[`54896ab011`](https://github.com/nodejs/node/commit/54896ab011)] - **module**: fix unintended mutation (Antoine du Hamel) [#46108](https://github.com/nodejs/node/pull/46108)
* \[[`bd98e5baba`](https://github.com/nodejs/node/commit/bd98e5baba)] - **node-api**: disambiguate napi\_add\_finalizer (Chengzhong Wu) [#45401](https://github.com/nodejs/node/pull/45401)
* \[[`f0508894d6`](https://github.com/nodejs/node/commit/f0508894d6)] - **perf\_hooks**: fix checking range of `options.figures` in createHistogram (Deokjin Kim) [#45999](https://github.com/nodejs/node/pull/45999)
* \[[`e482d5e42d`](https://github.com/nodejs/node/commit/e482d5e42d)] - **src**: fix endianness of simdutf (Yagiz Nizipli) [#46257](https://github.com/nodejs/node/pull/46257)
* \[[`e2c47cdfad`](https://github.com/nodejs/node/commit/e2c47cdfad)] - **src**: make BuiltinLoader threadsafe and non-global (Anna Henningsen) [#45942](https://github.com/nodejs/node/pull/45942)
* \[[`36ae3ccff3`](https://github.com/nodejs/node/commit/36ae3ccff3)] - **src**: replace unreachable code with static\_assert (Tobias Nießen) [#46209](https://github.com/nodejs/node/pull/46209)
* \[[`9d55a1f9a1`](https://github.com/nodejs/node/commit/9d55a1f9a1)] - **src**: hide kMaxDigestMultiplier outside HKDF impl (Tobias Nießen) [#46206](https://github.com/nodejs/node/pull/46206)
* \[[`d3d62ed82c`](https://github.com/nodejs/node/commit/d3d62ed82c)] - **src**: distinguish env stopping flags (Chengzhong Wu) [#45907](https://github.com/nodejs/node/pull/45907)
* \[[`e85f76686c`](https://github.com/nodejs/node/commit/e85f76686c)] - **src**: remove return after abort (Shelley Vohr) [#46172](https://github.com/nodejs/node/pull/46172)
* \[[`7dc9a53b18`](https://github.com/nodejs/node/commit/7dc9a53b18)] - **src**: remove unnecessary semicolons (Shelley Vohr) [#46171](https://github.com/nodejs/node/pull/46171)
* \[[`28af831d5a`](https://github.com/nodejs/node/commit/28af831d5a)] - **src**: use simdutf for converting externalized builtins to UTF-16 (Anna Henningsen) [#46119](https://github.com/nodejs/node/pull/46119)
* \[[`e8eaa490af`](https://github.com/nodejs/node/commit/e8eaa490af)] - **src**: use constant strings for memory info names (Chengzhong Wu) [#46087](https://github.com/nodejs/node/pull/46087)
* \[[`f4559a1354`](https://github.com/nodejs/node/commit/f4559a1354)] - **src**: fix typo in node\_snapshotable.cc (Vadim) [#46103](https://github.com/nodejs/node/pull/46103)
* \[[`ca8ff08a5c`](https://github.com/nodejs/node/commit/ca8ff08a5c)] - **src**: keep PipeWrap::Open function consistent with TCPWrap (theanarkh) [#46064](https://github.com/nodejs/node/pull/46064)
* \[[`a936eaeb34`](https://github.com/nodejs/node/commit/a936eaeb34)] - **src**: speed up process.getActiveResourcesInfo() (Darshan Sen) [#46014](https://github.com/nodejs/node/pull/46014)
* \[[`5cf595659f`](https://github.com/nodejs/node/commit/5cf595659f)] - **src,lib**: the handle keeps loop alive in cluster rr mode (theanarkh) [#46161](https://github.com/nodejs/node/pull/46161)
* \[[`18695595e1`](https://github.com/nodejs/node/commit/18695595e1)] - **stream**: fix pipeline calling end on destination more than once (Debadree Chatterjee) [#46226](https://github.com/nodejs/node/pull/46226)
* \[[`e5f53b51f0`](https://github.com/nodejs/node/commit/e5f53b51f0)] - **stream**: implement finished() for ReadableStream and WritableStream (Debadree Chatterjee) [#46205](https://github.com/nodejs/node/pull/46205)
* \[[`2f23f17f93`](https://github.com/nodejs/node/commit/2f23f17f93)] - **test**: reduce `fs-write-optional-params` flakiness (LiviaMedeiros) [#46238](https://github.com/nodejs/node/pull/46238)
* \[[`255f177108`](https://github.com/nodejs/node/commit/255f177108)] - **test**: enable more case of bad buffer in `fs.write` (Deokjin Kim) [#46236](https://github.com/nodejs/node/pull/46236)
* \[[`c09b2036c7`](https://github.com/nodejs/node/commit/c09b2036c7)] - **test**: update postject to 1.0.0-alpha.4 (Node.js GitHub Bot) [#46212](https://github.com/nodejs/node/pull/46212)
* \[[`4ac5c7180f`](https://github.com/nodejs/node/commit/4ac5c7180f)] - **test**: refactor to avoid mutation of global by a loader (Michaël Zasso) [#46220](https://github.com/nodejs/node/pull/46220)
* \[[`bbf9da8e2c`](https://github.com/nodejs/node/commit/bbf9da8e2c)] - **test**: improve test coverage for WHATWG `TextDecoder` (Juan José) [#45241](https://github.com/nodejs/node/pull/45241)
* \[[`4f491d368c`](https://github.com/nodejs/node/commit/4f491d368c)] - **test**: add fix so that test exits if port 42 is unprivileged (Suyash Nayan) [#45904](https://github.com/nodejs/node/pull/45904)
* \[[`6e2f7228f3`](https://github.com/nodejs/node/commit/6e2f7228f3)] - **test**: use `os.availableParallelism()` (Deokjin Kim) [#46003](https://github.com/nodejs/node/pull/46003)
* \[[`c77b0da512`](https://github.com/nodejs/node/commit/c77b0da512)] - **test**: fix flaky test-runner-exit-code.js (Colin Ihrig) [#46138](https://github.com/nodejs/node/pull/46138)
* \[[`f309e2acb6`](https://github.com/nodejs/node/commit/f309e2acb6)] - **test**: update Web Events WPT (Deokjin Kim) [#46051](https://github.com/nodejs/node/pull/46051)
* \[[`0f60bc9bbc`](https://github.com/nodejs/node/commit/0f60bc9bbc)] - **test**: add test to once() in event lib (Jonathan Diaz) [#46126](https://github.com/nodejs/node/pull/46126)
* \[[`8a8b18678a`](https://github.com/nodejs/node/commit/8a8b18678a)] - **test,esm**: validate more edge cases for dynamic imports (Antoine du Hamel) [#46059](https://github.com/nodejs/node/pull/46059)
* \[[`4d3743938f`](https://github.com/nodejs/node/commit/4d3743938f)] - **test\_runner**: update comment to comply with eslint no-fallthrough rule (Antoine du Hamel) [#46258](https://github.com/nodejs/node/pull/46258)
* \[[`653b108fdc`](https://github.com/nodejs/node/commit/653b108fdc)] - **tools**: update eslint to 8.32.0 (Node.js GitHub Bot) [#46258](https://github.com/nodejs/node/pull/46258)
* \[[`a4b0c916e0`](https://github.com/nodejs/node/commit/a4b0c916e0)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#46214](https://github.com/nodejs/node/pull/46214)
* \[[`f4465e656d`](https://github.com/nodejs/node/commit/f4465e656d)] - **tools**: fix macro name in update-undici (Almeida) [#46217](https://github.com/nodejs/node/pull/46217)
* \[[`1aa4534c6f`](https://github.com/nodejs/node/commit/1aa4534c6f)] - **tools**: add automation for updating postject dependency (Darshan Sen) [#46157](https://github.com/nodejs/node/pull/46157)
* \[[`c150b312cd`](https://github.com/nodejs/node/commit/c150b312cd)] - **tools**: update create-or-update-pull-request-action (Michaël Zasso) [#46169](https://github.com/nodejs/node/pull/46169)
* \[[`c68a043400`](https://github.com/nodejs/node/commit/c68a043400)] - **tools**: update eslint to 8.31.0 (Node.js GitHub Bot) [#46131](https://github.com/nodejs/node/pull/46131)
* \[[`ac90e419d1`](https://github.com/nodejs/node/commit/ac90e419d1)] - **tools**: update lint-md-dependencies to rollup\@3.9.1 (Node.js GitHub Bot) [#46129](https://github.com/nodejs/node/pull/46129)
* \[[`750fcf84eb`](https://github.com/nodejs/node/commit/750fcf84eb)] - **tools**: move update-eslint.sh to dep\_updaters/ (Luigi Pinca) [#46088](https://github.com/nodejs/node/pull/46088)
* \[[`2e8750a18c`](https://github.com/nodejs/node/commit/2e8750a18c)] - **tools**: make update-eslint.sh work with npm\@9 (Luigi Pinca) [#46088](https://github.com/nodejs/node/pull/46088)
* \[[`e90a3a6eff`](https://github.com/nodejs/node/commit/e90a3a6eff)] - **tools**: fix lint rule recommendation (Colin Ihrig) [#46044](https://github.com/nodejs/node/pull/46044)
* \[[`0985ef8bfb`](https://github.com/nodejs/node/commit/0985ef8bfb)] - **tools**: add `ArrayPrototypeConcat` to the list of primordials to avoid (Antoine du Hamel) [#44445](https://github.com/nodejs/node/pull/44445)
* \[[`ed69a3af92`](https://github.com/nodejs/node/commit/ed69a3af92)] - **tools**: add `prefer-proto` rule (Jordan Harband) [#46083](https://github.com/nodejs/node/pull/46083)
* \[[`4c1c20fae2`](https://github.com/nodejs/node/commit/4c1c20fae2)] - **trace\_events**: refactor to use `validateStringArray` (Deokjin Kim) [#46012](https://github.com/nodejs/node/pull/46012)
* \[[`6c8a81d2dc`](https://github.com/nodejs/node/commit/6c8a81d2dc)] - **vm**: refactor to use validate function (Deokjin Kim) [#46176](https://github.com/nodejs/node/pull/46176)

<a id="19.4.0"></a>

## 2023-01-06, Version 19.4.0 (Current), @RafaelGSS

### Notable Changes

* **buffer**:
  * (SEMVER-MINOR) add buffer.isUtf8 for utf8 validation (Yagiz Nizipli) [#45947](https://github.com/nodejs/node/pull/45947)
* **http**:
  * (SEMVER-MINOR) improved timeout defaults handling (Paolo Insogna) [#45778](https://github.com/nodejs/node/pull/45778)
* **net**:
  * add autoSelectFamily global getter and setter (Paolo Insogna) [#45777](https://github.com/nodejs/node/pull/45777)
* **os**:
  * (SEMVER-MINOR) add availableParallelism() (Colin Ihrig) [#45895](https://github.com/nodejs/node/pull/45895)
* **util**:
  * add fast path for text-decoder fatal flag (Yagiz Nizipli) [#45803](https://github.com/nodejs/node/pull/45803)

### Commits

* \[[`54b748acc0`](https://github.com/nodejs/node/commit/54b748acc0)] - **async\_hooks**: refactor to use `validateObject` (Deokjin Kim) [#46004](https://github.com/nodejs/node/pull/46004)
* \[[`cf2ff81f26`](https://github.com/nodejs/node/commit/cf2ff81f26)] - **benchmark**: include webstreams benchmark (Rafael Gonzaga) [#45876](https://github.com/nodejs/node/pull/45876)
* \[[`6e3d7f8c2d`](https://github.com/nodejs/node/commit/6e3d7f8c2d)] - **bootstrap**: optimize modules loaded in the built-in snapshot (Joyee Cheung) [#45849](https://github.com/nodejs/node/pull/45849)
* \[[`d181b76374`](https://github.com/nodejs/node/commit/d181b76374)] - **bootstrap**: make CJS loader snapshotable (Joyee Cheung) [#45849](https://github.com/nodejs/node/pull/45849)
* \[[`508e830765`](https://github.com/nodejs/node/commit/508e830765)] - **bootstrap**: include event\_target into the built-in snapshot (Joyee Cheung) [#45849](https://github.com/nodejs/node/pull/45849)
* \[[`dd77c05480`](https://github.com/nodejs/node/commit/dd77c05480)] - **bootstrap**: support module\_wrap binding in snapshot (Joyee Cheung) [#45849](https://github.com/nodejs/node/pull/45849)
* \[[`fbe399c75c`](https://github.com/nodejs/node/commit/fbe399c75c)] - **(SEMVER-MINOR)** **buffer**: add buffer.isUtf8 for utf8 validation (Yagiz Nizipli) [#45947](https://github.com/nodejs/node/pull/45947)
* \[[`233a66f937`](https://github.com/nodejs/node/commit/233a66f937)] - **build**: fix arm64 cross-compile from powershell (Stefan Stojanovic) [#45890](https://github.com/nodejs/node/pull/45890)
* \[[`e7b98a3da2`](https://github.com/nodejs/node/commit/e7b98a3da2)] - **build**: add option to disable shared readonly heap (Anna Henningsen) [#45887](https://github.com/nodejs/node/pull/45887)
* \[[`777c551edf`](https://github.com/nodejs/node/commit/777c551edf)] - **crypto**: ensure exported webcrypto EC keys use uncompressed point format (Ben Noordhuis) [#46021](https://github.com/nodejs/node/pull/46021)
* \[[`f7dba5bef7`](https://github.com/nodejs/node/commit/f7dba5bef7)] - **crypto**: fix globalThis.crypto this check (Filip Skokan) [#45857](https://github.com/nodejs/node/pull/45857)
* \[[`56f3ad101b`](https://github.com/nodejs/node/commit/56f3ad101b)] - **crypto**: fix CryptoKey prototype WPT (Filip Skokan) [#45857](https://github.com/nodejs/node/pull/45857)
* \[[`c9747f1140`](https://github.com/nodejs/node/commit/c9747f1140)] - **crypto**: use globalThis.crypto over require('crypto').webcrypto (Filip Skokan) [#45817](https://github.com/nodejs/node/pull/45817)
* \[[`6eede72241`](https://github.com/nodejs/node/commit/6eede72241)] - **crypto**: fix CryptoKey WebIDL conformance (Filip Skokan) [#45855](https://github.com/nodejs/node/pull/45855)
* \[[`c9802862b7`](https://github.com/nodejs/node/commit/c9802862b7)] - **crypto**: fix error when getRandomValues is called without arguments (Filip Skokan) [#45854](https://github.com/nodejs/node/pull/45854)
* \[[`3d09754186`](https://github.com/nodejs/node/commit/3d09754186)] - **debugger**: refactor console in lib/internal/debugger/inspect.js (Debadree Chatterjee) [#45847](https://github.com/nodejs/node/pull/45847)
* \[[`fdda2ff53b`](https://github.com/nodejs/node/commit/fdda2ff53b)] - **deps**: V8: cherry-pick 30861a39323d (Aaron Friel) [#45851](https://github.com/nodejs/node/pull/45851)
* \[[`71bf513062`](https://github.com/nodejs/node/commit/71bf513062)] - **deps**: patch V8 to 10.8.168.25 (Michaël Zasso) [#45996](https://github.com/nodejs/node/pull/45996)
* \[[`0552b13232`](https://github.com/nodejs/node/commit/0552b13232)] - **deps**: update simdutf to 2.0.9 (Node.js GitHub Bot) [#45975](https://github.com/nodejs/node/pull/45975)
* \[[`e73be1b3b9`](https://github.com/nodejs/node/commit/e73be1b3b9)] - **deps**: update to uvwasi 0.0.14 (Colin Ihrig) [#45970](https://github.com/nodejs/node/pull/45970)
* \[[`e4323f01c1`](https://github.com/nodejs/node/commit/e4323f01c1)] - **deps**: fix updater github workflow job (Yagiz Nizipli) [#45972](https://github.com/nodejs/node/pull/45972)
* \[[`05fee67238`](https://github.com/nodejs/node/commit/05fee67238)] - _**Revert**_ "**deps**: disable avx512 for simutf on benchmark ci" (Yagiz Nizipli) [#45948](https://github.com/nodejs/node/pull/45948)
* \[[`98fc94a444`](https://github.com/nodejs/node/commit/98fc94a444)] - **deps**: disable avx512 for simutf on benchmark ci (Yagiz Nizipli) [#45803](https://github.com/nodejs/node/pull/45803)
* \[[`344c5ec0ea`](https://github.com/nodejs/node/commit/344c5ec0ea)] - **deps**: add simdutf dependency (Yagiz Nizipli) [#45803](https://github.com/nodejs/node/pull/45803)
* \[[`7bdad948c8`](https://github.com/nodejs/node/commit/7bdad948c8)] - **deps**: V8: backport 8ca9f77d0f7c (Anna Henningsen) [#45871](https://github.com/nodejs/node/pull/45871)
* \[[`29f90cf5af`](https://github.com/nodejs/node/commit/29f90cf5af)] - **deps**: update timezone to 2022g (Node.js GitHub Bot) [#45731](https://github.com/nodejs/node/pull/45731)
* \[[`99fec0bf64`](https://github.com/nodejs/node/commit/99fec0bf64)] - **deps**: update undici to 5.14.0 (Node.js GitHub Bot) [#45812](https://github.com/nodejs/node/pull/45812)
* \[[`faee973fa7`](https://github.com/nodejs/node/commit/faee973fa7)] - **deps**: V8: cherry-pick bc831f8ba33b (Yagiz Nizipli) [#45788](https://github.com/nodejs/node/pull/45788)
* \[[`e2944109c6`](https://github.com/nodejs/node/commit/e2944109c6)] - **deps**: V8: cherry-pick bf0bd4868dde (Michaël Zasso) [#45908](https://github.com/nodejs/node/pull/45908)
* \[[`e113d169ee`](https://github.com/nodejs/node/commit/e113d169ee)] - **doc**: update isUtf8 description (Yagiz Nizipli) [#45973](https://github.com/nodejs/node/pull/45973)
* \[[`9e16406066`](https://github.com/nodejs/node/commit/9e16406066)] - **doc**: sort http.createServer() options alphabetically (Luigi Pinca) [#45680](https://github.com/nodejs/node/pull/45680)
* \[[`49253e1a8f`](https://github.com/nodejs/node/commit/49253e1a8f)] - **doc**: use console.error for error case in timers and tls (Deokjin Kim) [#46002](https://github.com/nodejs/node/pull/46002)
* \[[`8be1b666a7`](https://github.com/nodejs/node/commit/8be1b666a7)] - **doc**: fix wrong output of example in `url.protocol` (Deokjin Kim) [#45954](https://github.com/nodejs/node/pull/45954)
* \[[`9251dce8b2`](https://github.com/nodejs/node/commit/9251dce8b2)] - **doc**: use `os.availableParallelism()` in async\_context and cluster (Deokjin Kim) [#45979](https://github.com/nodejs/node/pull/45979)
* \[[`952e03ae66`](https://github.com/nodejs/node/commit/952e03ae66)] - **doc**: make EventEmitterAsyncResource's `options` as optional (Deokjin Kim) [#45985](https://github.com/nodejs/node/pull/45985)
* \[[`71cc3b3712`](https://github.com/nodejs/node/commit/71cc3b3712)] - **doc**: replace single executable champion in strategic initiatives doc (Darshan Sen) [#45956](https://github.com/nodejs/node/pull/45956)
* \[[`eaf6b63637`](https://github.com/nodejs/node/commit/eaf6b63637)] - **doc**: update error message of example in repl (Deokjin Kim) [#45920](https://github.com/nodejs/node/pull/45920)
* \[[`d8b5b7da75`](https://github.com/nodejs/node/commit/d8b5b7da75)] - **doc**: fix typos in packages.md (Eric Mutta) [#45957](https://github.com/nodejs/node/pull/45957)
* \[[`4457e051c9`](https://github.com/nodejs/node/commit/4457e051c9)] - **doc**: remove port from example in `url.hostname` (Deokjin Kim) [#45927](https://github.com/nodejs/node/pull/45927)
* \[[`908f4fab52`](https://github.com/nodejs/node/commit/908f4fab52)] - **doc**: show output of example in http (Deokjin Kim) [#45915](https://github.com/nodejs/node/pull/45915)
* \[[`faf5c23084`](https://github.com/nodejs/node/commit/faf5c23084)] - **(SEMVER-MINOR)** **doc**: add parallelism note to os.cpus() (Colin Ihrig) [#45895](https://github.com/nodejs/node/pull/45895)
* \[[`9ed547b73c`](https://github.com/nodejs/node/commit/9ed547b73c)] - **doc**: fix wrong output of example in `url.password` (Deokjin Kim) [#45928](https://github.com/nodejs/node/pull/45928)
* \[[`a89f8c1337`](https://github.com/nodejs/node/commit/a89f8c1337)] - **doc**: fix some history entries in `deprecations.md` (Antoine du Hamel) [#45891](https://github.com/nodejs/node/pull/45891)
* \[[`cf30fca23f`](https://github.com/nodejs/node/commit/cf30fca23f)] - **doc**: add tip for NODE\_MODULE (theanarkh) [#45797](https://github.com/nodejs/node/pull/45797)
* \[[`d500445aec`](https://github.com/nodejs/node/commit/d500445aec)] - **doc**: reduce likelihood of mismerges during release (Richard Lau) [#45864](https://github.com/nodejs/node/pull/45864)
* \[[`e229f060e3`](https://github.com/nodejs/node/commit/e229f060e3)] - **doc**: add backticks to webcrypto rsaOaepParams (Filip Skokan) [#45883](https://github.com/nodejs/node/pull/45883)
* \[[`dfa58c1947`](https://github.com/nodejs/node/commit/dfa58c1947)] - **doc**: remove release cleanup step (Michaël Zasso) [#45858](https://github.com/nodejs/node/pull/45858)
* \[[`b93a9670a8`](https://github.com/nodejs/node/commit/b93a9670a8)] - **doc**: add stream/promises pipeline and finished to doc (Marco Ippolito) [#45832](https://github.com/nodejs/node/pull/45832)
* \[[`c86f4a17d6`](https://github.com/nodejs/node/commit/c86f4a17d6)] - **doc**: remove Juan Jose keys (Rafael Gonzaga) [#45827](https://github.com/nodejs/node/pull/45827)
* \[[`c37a119f90`](https://github.com/nodejs/node/commit/c37a119f90)] - **doc**: remove last example use of require('crypto').webcrypto (Filip Skokan) [#45819](https://github.com/nodejs/node/pull/45819)
* \[[`7e047dfcbb`](https://github.com/nodejs/node/commit/7e047dfcbb)] - **doc**: fix wrong output of example in util (Deokjin Kim) [#45825](https://github.com/nodejs/node/pull/45825)
* \[[`8046e0ef53`](https://github.com/nodejs/node/commit/8046e0ef53)] - **errors**: refactor to use a method that formats a list string (Daeyeon Jeong) [#45793](https://github.com/nodejs/node/pull/45793)
* \[[`2d49e0e635`](https://github.com/nodejs/node/commit/2d49e0e635)] - **esm**: rewrite loader hooks test (Geoffrey Booth) [#46016](https://github.com/nodejs/node/pull/46016)
* \[[`47cc0e4bdb`](https://github.com/nodejs/node/commit/47cc0e4bdb)] - **events**: fix violation of symbol naming convention (Deokjin Kim) [#45978](https://github.com/nodejs/node/pull/45978)
* \[[`22a66cff66`](https://github.com/nodejs/node/commit/22a66cff66)] - **fs**: refactor to use `validateInteger` (Deokjin Kim) [#46008](https://github.com/nodejs/node/pull/46008)
* \[[`bc43922949`](https://github.com/nodejs/node/commit/bc43922949)] - **http**: replace `var` with `const` on code of comment (Deokjin Kim) [#45951](https://github.com/nodejs/node/pull/45951)
* \[[`7ea72ee421`](https://github.com/nodejs/node/commit/7ea72ee421)] - **(SEMVER-MINOR)** **http**: improved timeout defaults handling (Paolo Insogna) [#45778](https://github.com/nodejs/node/pull/45778)
* \[[`7f1daedf4c`](https://github.com/nodejs/node/commit/7f1daedf4c)] - **lib**: update JSDoc of `getOwnPropertyValueOrDefault` (Deokjin Kim) [#46010](https://github.com/nodejs/node/pull/46010)
* \[[`28f9089b83`](https://github.com/nodejs/node/commit/28f9089b83)] - **lib**: use `kEmptyObject` as default value for options (Deokjin Kim) [#46011](https://github.com/nodejs/node/pull/46011)
* \[[`f6c6673ec4`](https://github.com/nodejs/node/commit/f6c6673ec4)] - **lib**: lazy-load deps in modules/run\_main.js (Joyee Cheung) [#45849](https://github.com/nodejs/node/pull/45849)
* \[[`e529ea4144`](https://github.com/nodejs/node/commit/e529ea4144)] - **lib**: lazy-load deps in source\_map\_cache.js (Joyee Cheung) [#45849](https://github.com/nodejs/node/pull/45849)
* \[[`943852ab83`](https://github.com/nodejs/node/commit/943852ab83)] - **lib**: add getLazy() method to internal/util (Joyee Cheung) [#45849](https://github.com/nodejs/node/pull/45849)
* \[[`25d0a94453`](https://github.com/nodejs/node/commit/25d0a94453)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#46040](https://github.com/nodejs/node/pull/46040)
* \[[`0a70316ecc`](https://github.com/nodejs/node/commit/0a70316ecc)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45968](https://github.com/nodejs/node/pull/45968)
* \[[`86e30fcb4d`](https://github.com/nodejs/node/commit/86e30fcb4d)] - **meta**: add `nodejs/loaders` to CODEOWNERS (Geoffrey Booth) [#45940](https://github.com/nodejs/node/pull/45940)
* \[[`e95695654d`](https://github.com/nodejs/node/commit/e95695654d)] - **meta**: add `nodejs/test_runner` to CODEOWNERS (Antoine du Hamel) [#45935](https://github.com/nodejs/node/pull/45935)
* \[[`353dab5bdf`](https://github.com/nodejs/node/commit/353dab5bdf)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45899](https://github.com/nodejs/node/pull/45899)
* \[[`0b3512f690`](https://github.com/nodejs/node/commit/0b3512f690)] - **modules**: move callbacks and conditions into modules/esm/utils.js (Joyee Cheung) [#45849](https://github.com/nodejs/node/pull/45849)
* \[[`c6ab449d1b`](https://github.com/nodejs/node/commit/c6ab449d1b)] - **modules**: move modules/cjs/helpers.js to modules/helpers.js (Joyee Cheung) [#45849](https://github.com/nodejs/node/pull/45849)
* \[[`4d62b099b4`](https://github.com/nodejs/node/commit/4d62b099b4)] - **net**: handle socket.write(cb) edge case (Santiago Gimeno) [#45922](https://github.com/nodejs/node/pull/45922)
* \[[`8e6b8dbb41`](https://github.com/nodejs/node/commit/8e6b8dbb41)] - **net**: add autoSelectFamily global getter and setter (Paolo Insogna) [#45777](https://github.com/nodejs/node/pull/45777)
* \[[`f3bb6a38ae`](https://github.com/nodejs/node/commit/f3bb6a38ae)] - **node-api**: generalize finalizer second pass callback (Chengzhong Wu) [#44141](https://github.com/nodejs/node/pull/44141)
* \[[`d71883e271`](https://github.com/nodejs/node/commit/d71883e271)] - **(SEMVER-MINOR)** **os**: add availableParallelism() (Colin Ihrig) [#45895](https://github.com/nodejs/node/pull/45895)
* \[[`4c0850539a`](https://github.com/nodejs/node/commit/4c0850539a)] - **process,worker**: ensure code after exit() effectless (ywave620) [#45620](https://github.com/nodejs/node/pull/45620)
* \[[`24cae6b4a3`](https://github.com/nodejs/node/commit/24cae6b4a3)] - **repl**: improve robustness wrt to prototype pollution (Antoine du Hamel) [#45604](https://github.com/nodejs/node/pull/45604)
* \[[`af25c95b22`](https://github.com/nodejs/node/commit/af25c95b22)] - **src**: fix typo in `node_file.cc` (Vadim) [#45998](https://github.com/nodejs/node/pull/45998)
* \[[`261d6d0726`](https://github.com/nodejs/node/commit/261d6d0726)] - **src**: fix crash on OnStreamRead on Windows (Santiago Gimeno) [#45878](https://github.com/nodejs/node/pull/45878)
* \[[`6c5b7e660b`](https://github.com/nodejs/node/commit/6c5b7e660b)] - **src**: add worker per-isolate binding initialization (Chengzhong Wu) [#45547](https://github.com/nodejs/node/pull/45547)
* \[[`db535b6caa`](https://github.com/nodejs/node/commit/db535b6caa)] - **src**: define per-isolate internal bindings registration callback (Chengzhong Wu) [#45547](https://github.com/nodejs/node/pull/45547)
* \[[`ded87f6dc4`](https://github.com/nodejs/node/commit/ded87f6dc4)] - **src**: fix creating `Isolate`s from addons (Anna Henningsen) [#45885](https://github.com/nodejs/node/pull/45885)
* \[[`c2ed0ccb28`](https://github.com/nodejs/node/commit/c2ed0ccb28)] - **src**: use string\_view for FastStringKey implementation (Anna Henningsen) [#45914](https://github.com/nodejs/node/pull/45914)
* \[[`b995138b96`](https://github.com/nodejs/node/commit/b995138b96)] - **src**: use CreateEnvironment instead of inlining its code where possible (Anna Henningsen) [#45886](https://github.com/nodejs/node/pull/45886)
* \[[`4454f5fd71`](https://github.com/nodejs/node/commit/4454f5fd71)] - **src**: fix UB in overflow checks (Ben Noordhuis) [#45882](https://github.com/nodejs/node/pull/45882)
* \[[`27d3201502`](https://github.com/nodejs/node/commit/27d3201502)] - **src**: check size of args before using for exec\_path (A. Wilcox) [#45902](https://github.com/nodejs/node/pull/45902)
* \[[`2f898f2983`](https://github.com/nodejs/node/commit/2f898f2983)] - **src**: fix tls certificate root store data race (Ben Noordhuis) [#45767](https://github.com/nodejs/node/pull/45767)
* \[[`eff92a61b9`](https://github.com/nodejs/node/commit/eff92a61b9)] - **src**: add undici and acorn to `process.versions` (Debadree Chatterjee) [#45621](https://github.com/nodejs/node/pull/45621)
* \[[`ab22a8ff4b`](https://github.com/nodejs/node/commit/ab22a8ff4b)] - **stream**: refactor to use `validateFunction` (Deokjin Kim) [#46007](https://github.com/nodejs/node/pull/46007)
* \[[`0858956f5f`](https://github.com/nodejs/node/commit/0858956f5f)] - **stream**: fix typo in JSDoc (Deokjin Kim) [#45991](https://github.com/nodejs/node/pull/45991)
* \[[`2807efaea6`](https://github.com/nodejs/node/commit/2807efaea6)] - **test**: use `process.hrtime.bigint` instead of `process.hrtime` (Deokjin Kim) [#45877](https://github.com/nodejs/node/pull/45877)
* \[[`0f5a145973`](https://github.com/nodejs/node/commit/0f5a145973)] - **test**: print failed JS/parallel tests (Geoffrey Booth) [#45960](https://github.com/nodejs/node/pull/45960)
* \[[`c6c094702b`](https://github.com/nodejs/node/commit/c6c094702b)] - **test**: split parallel fs-watch-recursive tests (Yagiz Nizipli) [#45865](https://github.com/nodejs/node/pull/45865)
* \[[`97a8e055be`](https://github.com/nodejs/node/commit/97a8e055be)] - **test**: add all WebCryptoAPI globals to WPTRunner's loadLazyGlobals (Filip Skokan) [#45857](https://github.com/nodejs/node/pull/45857)
* \[[`95ce16d8d9`](https://github.com/nodejs/node/commit/95ce16d8d9)] - **test**: fix test broken under --node-builtin-modules-path (Geoffrey Booth) [#45894](https://github.com/nodejs/node/pull/45894)
* \[[`97868befe7`](https://github.com/nodejs/node/commit/97868befe7)] - **test**: fix mock.method to support class instances (Erick Wendel) [#45608](https://github.com/nodejs/node/pull/45608)
* \[[`71056daf76`](https://github.com/nodejs/node/commit/71056daf76)] - **test**: update encoding wpt to latest (Yagiz Nizipli) [#45850](https://github.com/nodejs/node/pull/45850)
* \[[`10367c4cae`](https://github.com/nodejs/node/commit/10367c4cae)] - **test**: update url wpt to latest (Yagiz Nizipli) [#45852](https://github.com/nodejs/node/pull/45852)
* \[[`53f02cf631`](https://github.com/nodejs/node/commit/53f02cf631)] - **test**: add CryptoKey transferring tests (Filip Skokan) [#45811](https://github.com/nodejs/node/pull/45811)
* \[[`5de08ef275`](https://github.com/nodejs/node/commit/5de08ef275)] - **test**: add postject to fixtures (Darshan Sen) [#45298](https://github.com/nodejs/node/pull/45298)
* \[[`fea122d51e`](https://github.com/nodejs/node/commit/fea122d51e)] - **test**: enable idlharness WebCryptoAPI WPTs (Filip Skokan) [#45822](https://github.com/nodejs/node/pull/45822)
* \[[`3c2ce5635e`](https://github.com/nodejs/node/commit/3c2ce5635e)] - **test**: remove use of --experimental-global-webcrypto flag (Filip Skokan) [#45816](https://github.com/nodejs/node/pull/45816)
* \[[`b5e124537e`](https://github.com/nodejs/node/commit/b5e124537e)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#45860](https://github.com/nodejs/node/pull/45860)
* \[[`7ae24abd7b`](https://github.com/nodejs/node/commit/7ae24abd7b)] - **test\_runner**: use os.availableParallelism() (Colin Ihrig) [#45969](https://github.com/nodejs/node/pull/45969)
* \[[`c5004d42af`](https://github.com/nodejs/node/commit/c5004d42af)] - **test\_runner**: run t.after() if test body throws (Colin Ihrig) [#45870](https://github.com/nodejs/node/pull/45870)
* \[[`bdbb676bee`](https://github.com/nodejs/node/commit/bdbb676bee)] - **test\_runner**: parse yaml (Moshe Atlow) [#45815](https://github.com/nodejs/node/pull/45815)
* \[[`ca9b9b9ce6`](https://github.com/nodejs/node/commit/ca9b9b9ce6)] - **tls**: don't treat fatal TLS alerts as EOF (David Benjamin) [#44563](https://github.com/nodejs/node/pull/44563)
* \[[`d08a574ecf`](https://github.com/nodejs/node/commit/d08a574ecf)] - **tls**: fix re-entrancy issue with TLS close\_notify (David Benjamin) [#44563](https://github.com/nodejs/node/pull/44563)
* \[[`0f0d22a63e`](https://github.com/nodejs/node/commit/0f0d22a63e)] - **tools**: update lint-md-dependencies to rollup\@3.9.0 (Node.js GitHub Bot) [#46039](https://github.com/nodejs/node/pull/46039)
* \[[`5a8d125fc4`](https://github.com/nodejs/node/commit/5a8d125fc4)] - **tools**: update doc to unist-util-select\@4.0.2 (Node.js GitHub Bot) [#46038](https://github.com/nodejs/node/pull/46038)
* \[[`54776ffe80`](https://github.com/nodejs/node/commit/54776ffe80)] - **tools**: add release host var to promotion script (Ruy Adorno) [#45913](https://github.com/nodejs/node/pull/45913)
* \[[`f968fdb78a`](https://github.com/nodejs/node/commit/f968fdb78a)] - **tools**: add url to `AUTHORS` update automation (Antoine du Hamel) [#45971](https://github.com/nodejs/node/pull/45971)
* \[[`7c518cbac1`](https://github.com/nodejs/node/commit/7c518cbac1)] - **tools**: update lint-md-dependencies to rollup\@3.8.1 (Node.js GitHub Bot) [#45967](https://github.com/nodejs/node/pull/45967)
* \[[`1282f7f656`](https://github.com/nodejs/node/commit/1282f7f656)] - **tools**: update GitHub workflow action (Mohammed Keyvanzadeh) [#45937](https://github.com/nodejs/node/pull/45937)
* \[[`f446af78e9`](https://github.com/nodejs/node/commit/f446af78e9)] - **tools**: update lint-md dependencies (Node.js GitHub Bot) [#45813](https://github.com/nodejs/node/pull/45813)
* \[[`794611ade9`](https://github.com/nodejs/node/commit/794611ade9)] - **tools**: enforce use of trailing commas in `tools/` (Antoine du Hamel) [#45889](https://github.com/nodejs/node/pull/45889)
* \[[`124c2b32d9`](https://github.com/nodejs/node/commit/124c2b32d9)] - **tools**: fix incorrect version history order (Fabien Michel) [#45728](https://github.com/nodejs/node/pull/45728)
* \[[`27cf389c22`](https://github.com/nodejs/node/commit/27cf389c22)] - **tools**: update eslint to 8.29.0 (Node.js GitHub Bot) [#45733](https://github.com/nodejs/node/pull/45733)
* \[[`ae842a40b5`](https://github.com/nodejs/node/commit/ae842a40b5)] - **util**: add fast path for text-decoder fatal flag (Yagiz Nizipli) [#45803](https://github.com/nodejs/node/pull/45803)
* \[[`389cc3e1d6`](https://github.com/nodejs/node/commit/389cc3e1d6)] - **vm**: refactor to use `validateStringArray` (Deokjin Kim) [#46020](https://github.com/nodejs/node/pull/46020)
* \[[`7bd6a2c258`](https://github.com/nodejs/node/commit/7bd6a2c258)] - **wasi**: fast calls (snek) [#43697](https://github.com/nodejs/node/pull/43697)

<a id="19.3.0"></a>

## 2022-12-14, Version 19.3.0 (Current), @targos

### Notable Changes

#### Updated npm to 9.2.0

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

* \[[`03db415540`](https://github.com/nodejs/node/commit/03db415540)] - **build**: disable v8 snapshot compression by default (Joyee Cheung) [#45716](https://github.com/nodejs/node/pull/45716)
* \[[`9f51b9e50d`](https://github.com/nodejs/node/commit/9f51b9e50d)] - **doc**: add doc-only deprecation for headers/trailers setters (Rich Trott) [#45697](https://github.com/nodejs/node/pull/45697)
* \[[`b010820c4e`](https://github.com/nodejs/node/commit/b010820c4e)] - **doc**: add Rafael Gonzaga to the TSC (Michael Dawson) [#45691](https://github.com/nodejs/node/pull/45691)
* \[[`b8b13dccd9`](https://github.com/nodejs/node/commit/b8b13dccd9)] - **(SEMVER-MINOR)** **net**: add autoSelectFamily and autoSelectFamilyAttemptTimeout options (Paolo Insogna) [#44731](https://github.com/nodejs/node/pull/44731)
* \[[`5d7cd363ab`](https://github.com/nodejs/node/commit/5d7cd363ab)] - **(SEMVER-MINOR)** **src**: add uvwasi version (Jithil P Ponnan) [#45639](https://github.com/nodejs/node/pull/45639)
* \[[`4165dcddf0`](https://github.com/nodejs/node/commit/95af851a25)] - **(SEMVER-MINOR)** **test\_runner**: add t.after() hook (Colin Ihrig) [#45792](https://github.com/nodejs/node/pull/45792)
* \[[`d1bd7796ad`](https://github.com/nodejs/node/commit/d1bd7796ad)] - **(SEMVER-MINOR)** **test\_runner**: don't use a symbol for runHook() (Colin Ihrig) [#45792](https://github.com/nodejs/node/pull/45792)
* \[[`691f58e76c`](https://github.com/nodejs/node/commit/691f58e76c)] - **tls**: remove trustcor root ca certificates (Ben Noordhuis) [#45776](https://github.com/nodejs/node/pull/45776)

### Commits

* \[[`382efdf460`](https://github.com/nodejs/node/commit/382efdf460)] - **benchmark**: add variety of inputs to text-encoder (Yagiz Nizipli) [#45787](https://github.com/nodejs/node/pull/45787)
* \[[`102c2dc071`](https://github.com/nodejs/node/commit/102c2dc071)] - **benchmark**: make benchmarks runnable in older versions of Node.js (Joyee Cheung) [#45746](https://github.com/nodejs/node/pull/45746)
* \[[`e2caf7ced9`](https://github.com/nodejs/node/commit/e2caf7ced9)] - **bootstrap**: lazy load non-essential modules (Joyee Cheung) [#45659](https://github.com/nodejs/node/pull/45659)
* \[[`49840d443c`](https://github.com/nodejs/node/commit/49840d443c)] - **buffer**: remove unnecessary lazy loading (Antoine du Hamel) [#45807](https://github.com/nodejs/node/pull/45807)
* \[[`17847683dc`](https://github.com/nodejs/node/commit/17847683dc)] - **buffer**: make decodeUTF8 params loose (Yagiz Nizipli) [#45610](https://github.com/nodejs/node/pull/45610)
* \[[`03db415540`](https://github.com/nodejs/node/commit/03db415540)] - **build**: disable v8 snapshot compression by default (Joyee Cheung) [#45716](https://github.com/nodejs/node/pull/45716)
* \[[`95a23e24f3`](https://github.com/nodejs/node/commit/95a23e24f3)] - **build**: add python 3.11 support for android (Mohammed Keyvanzadeh) [#45765](https://github.com/nodejs/node/pull/45765)
* \[[`09bc89daba`](https://github.com/nodejs/node/commit/09bc89daba)] - **build**: rework gyp files for zlib (Richard Lau) [#45589](https://github.com/nodejs/node/pull/45589)
* \[[`b5b56b6b45`](https://github.com/nodejs/node/commit/b5b56b6b45)] - **crypto**: simplify lazy loading of internal modules (Antoine du Hamel) [#45809](https://github.com/nodejs/node/pull/45809)
* \[[`2e4d37e3f0`](https://github.com/nodejs/node/commit/2e4d37e3f0)] - **crypto**: fix CipherBase Update int32 overflow (Marco Ippolito) [#45769](https://github.com/nodejs/node/pull/45769)
* \[[`573eab9235`](https://github.com/nodejs/node/commit/573eab9235)] - **crypto**: refactor ArrayBuffer to bigint conversion utils (Antoine du Hamel) [#45567](https://github.com/nodejs/node/pull/45567)
* \[[`845f805490`](https://github.com/nodejs/node/commit/845f805490)] - **crypto**: refactor verify acceptable key usage functions (Filip Skokan) [#45569](https://github.com/nodejs/node/pull/45569)
* \[[`7cc9998737`](https://github.com/nodejs/node/commit/7cc9998737)] - **crypto**: fix ECDH webcrypto public CryptoKey usages (Filip Skokan) [#45569](https://github.com/nodejs/node/pull/45569)
* \[[`d030963f37`](https://github.com/nodejs/node/commit/d030963f37)] - **crypto**: validate CFRG webcrypto JWK import "d" and "x" are a pair (Filip Skokan) [#45569](https://github.com/nodejs/node/pull/45569)
* \[[`9cd106efdc`](https://github.com/nodejs/node/commit/9cd106efdc)] - **crypto**: use DataError for CFRG webcrypto raw and jwk import key checks (Filip Skokan) [#45569](https://github.com/nodejs/node/pull/45569)
* \[[`9e2e3de6ce`](https://github.com/nodejs/node/commit/9e2e3de6ce)] - **crypto**: use DataError for webcrypto keyData import failures (Filip Skokan) [#45569](https://github.com/nodejs/node/pull/45569)
* \[[`40037b4e79`](https://github.com/nodejs/node/commit/40037b4e79)] - **crypto**: fix X25519 and X448 webcrypto public CryptoKey usages (Filip Skokan) [#45569](https://github.com/nodejs/node/pull/45569)
* \[[`de2b6b97b9`](https://github.com/nodejs/node/commit/de2b6b97b9)] - **crypto**: ensure "x" is present when importing private CFRG webcrypto keys (Filip Skokan) [#45569](https://github.com/nodejs/node/pull/45569)
* \[[`75dbce9a07`](https://github.com/nodejs/node/commit/75dbce9a07)] - **deps**: upgrade npm to 9.2.0 (npm team) [#45780](https://github.com/nodejs/node/pull/45780)
* \[[`677eb62bf2`](https://github.com/nodejs/node/commit/677eb62bf2)] - **deps**: upgrade npm to 9.1.3 (npm team) [#45693](https://github.com/nodejs/node/pull/45693)
* \[[`1d823a6d30`](https://github.com/nodejs/node/commit/1d823a6d30)] - _**Revert**_ "**deps**: fix zlib compilation for CPUs without SIMD features" (Luigi Pinca) [#45589](https://github.com/nodejs/node/pull/45589)
* \[[`6b15994597`](https://github.com/nodejs/node/commit/6b15994597)] - **deps**: update undici to 5.13.0 (Node.js GitHub Bot) [#45634](https://github.com/nodejs/node/pull/45634)
* \[[`fbd2d27789`](https://github.com/nodejs/node/commit/fbd2d27789)] - **deps**: update corepack to 0.15.2 (Node.js GitHub Bot) [#45635](https://github.com/nodejs/node/pull/45635)
* \[[`60c9ac5178`](https://github.com/nodejs/node/commit/60c9ac5178)] - **deps**: update nghttp2 to 1.51.0 (Yagiz Nizipli) [#45537](https://github.com/nodejs/node/pull/45537)
* \[[`c8421204b0`](https://github.com/nodejs/node/commit/c8421204b0)] - **deps**: patch V8 to 10.8.168.21 (Michaël Zasso) [#45749](https://github.com/nodejs/node/pull/45749)
* \[[`c5277417c9`](https://github.com/nodejs/node/commit/c5277417c9)] - **diagnostics\_channel**: fix diagnostics channel memory leak (theanarkh) [#45633](https://github.com/nodejs/node/pull/45633)
* \[[`8a90f5c784`](https://github.com/nodejs/node/commit/8a90f5c784)] - **doc**: buffer.fill empty value (Marco Ippolito) [#45794](https://github.com/nodejs/node/pull/45794)
* \[[`9d6af617ea`](https://github.com/nodejs/node/commit/9d6af617ea)] - **doc**: add args of filter option of fs.cp (MURAKAMI Masahiko) [#45739](https://github.com/nodejs/node/pull/45739)
* \[[`8c728d2f02`](https://github.com/nodejs/node/commit/8c728d2f02)] - **doc**: disambiguate `native module` to `addon` (Daeyeon Jeong) [#45673](https://github.com/nodejs/node/pull/45673)
* \[[`7718ff82a4`](https://github.com/nodejs/node/commit/7718ff82a4)] - **doc**: using console.error for error cases in crypto and events (emirgoren) [#45640](https://github.com/nodejs/node/pull/45640)
* \[[`029060e6e4`](https://github.com/nodejs/node/commit/029060e6e4)] - **doc**: fix actual result of example is different in events (Deokjin Kim) [#45656](https://github.com/nodejs/node/pull/45656)
* \[[`9f51b9e50d`](https://github.com/nodejs/node/commit/9f51b9e50d)] - **doc**: add doc-only deprecation for headers/trailers setters (Rich Trott) [#45697](https://github.com/nodejs/node/pull/45697)
* \[[`801fe30488`](https://github.com/nodejs/node/commit/801fe30488)] - **doc**: add detail on how api docs are published (Michael Dawson) [#45626](https://github.com/nodejs/node/pull/45626)
* \[[`e124e2a6ee`](https://github.com/nodejs/node/commit/e124e2a6ee)] - **doc**: use console.error for error case in child\_process and dgram (Deokjin Kim) [#45690](https://github.com/nodejs/node/pull/45690)
* \[[`1b920287b6`](https://github.com/nodejs/node/commit/1b920287b6)] - **doc**: move streaming instruc to doc/contributing (Michael Dawson) [#45582](https://github.com/nodejs/node/pull/45582)
* \[[`b010820c4e`](https://github.com/nodejs/node/commit/b010820c4e)] - **doc**: add Rafael to the tsc (Michael Dawson) [#45691](https://github.com/nodejs/node/pull/45691)
* \[[`4fb7cf88e2`](https://github.com/nodejs/node/commit/4fb7cf88e2)] - **doc**: add missing line in debugger (Deokjin Kim) [#45632](https://github.com/nodejs/node/pull/45632)
* \[[`c0df265fea`](https://github.com/nodejs/node/commit/c0df265fea)] - **doc**: fix actual result of example is different in stream (Deokjin Kim) [#45619](https://github.com/nodejs/node/pull/45619)
* \[[`027e738064`](https://github.com/nodejs/node/commit/027e738064)] - **doc**: add `options` parameter to eventTarget.removeEventListener (Deokjin Kim) [#45667](https://github.com/nodejs/node/pull/45667)
* \[[`23ff5057b2`](https://github.com/nodejs/node/commit/23ff5057b2)] - **doc**: define "react-native" community condition (Alex Hunt) [#45367](https://github.com/nodejs/node/pull/45367)
* \[[`2e767bf18b`](https://github.com/nodejs/node/commit/2e767bf18b)] - **doc**: move os.machine() docs to sorted position (Colin Ihrig) [#45647](https://github.com/nodejs/node/pull/45647)
* \[[`aabfdef861`](https://github.com/nodejs/node/commit/aabfdef861)] - **doc**: use console.error for error case in fs, https, net and process (Deokjin Kim) [#45606](https://github.com/nodejs/node/pull/45606)
* \[[`3a02d50d35`](https://github.com/nodejs/node/commit/3a02d50d35)] - **doc**: add link to doc with social processes (Michael Dawson) [#45584](https://github.com/nodejs/node/pull/45584)
* \[[`e4316124fa`](https://github.com/nodejs/node/commit/e4316124fa)] - **fs**: fix `nonNativeWatcher` watching folder with existing files (Moshe Atlow) [#45500](https://github.com/nodejs/node/pull/45500)
* \[[`d272faa54d`](https://github.com/nodejs/node/commit/d272faa54d)] - **fs**: fix `nonNativeWatcher` leak of `StatWatchers` (Moshe Atlow) [#45501](https://github.com/nodejs/node/pull/45501)
* \[[`d64e773168`](https://github.com/nodejs/node/commit/d64e773168)] - **http**: make `OutgoingMessage` more streamlike (Robert Nagy) [#45672](https://github.com/nodejs/node/pull/45672)
* \[[`ed8ae88f30`](https://github.com/nodejs/node/commit/ed8ae88f30)] - **lib**: remove unnecessary lazy loading in `internal/encoding` (Antoine du Hamel) [#45810](https://github.com/nodejs/node/pull/45810)
* \[[`302c5240c5`](https://github.com/nodejs/node/commit/302c5240c5)] - **lib**: allow Writeable.toWeb() to work on http.Outgoing message (Debadree Chatterjee) [#45642](https://github.com/nodejs/node/pull/45642)
* \[[`e8745083b9`](https://github.com/nodejs/node/commit/e8745083b9)] - **lib**: check number of arguments in `EventTarget`'s function (Deokjin Kim) [#45668](https://github.com/nodejs/node/pull/45668)
* \[[`9f7bb5ce0e`](https://github.com/nodejs/node/commit/9f7bb5ce0e)] - **lib**: disambiguate `native module` to `binding` (Daeyeon Jeong) [#45673](https://github.com/nodejs/node/pull/45673)
* \[[`353339a552`](https://github.com/nodejs/node/commit/353339a552)] - **lib**: disambiguate `native module` to `builtin module` (Daeyeon Jeong) [#45673](https://github.com/nodejs/node/pull/45673)
* \[[`99410efd19`](https://github.com/nodejs/node/commit/99410efd19)] - **lib**: added SuiteContext class (Debadree Chatterjee) [#45687](https://github.com/nodejs/node/pull/45687)
* \[[`a79f37a0a7`](https://github.com/nodejs/node/commit/a79f37a0a7)] - **lib**: add missing type of removeEventListener in question (Deokjin Kim) [#45676](https://github.com/nodejs/node/pull/45676)
* \[[`e0750467e8`](https://github.com/nodejs/node/commit/e0750467e8)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45814](https://github.com/nodejs/node/pull/45814)
* \[[`376f3468b9`](https://github.com/nodejs/node/commit/376f3468b9)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45732](https://github.com/nodejs/node/pull/45732)
* \[[`a6e2cf2d6f`](https://github.com/nodejs/node/commit/a6e2cf2d6f)] - **meta**: add .mailmap entry for Stefan Stojanovic (Rich Trott) [#45703](https://github.com/nodejs/node/pull/45703)
* \[[`eb9a383d2a`](https://github.com/nodejs/node/commit/eb9a383d2a)] - **meta**: update AUTHORS info for nstepien (Nicolas Stepien) [#45692](https://github.com/nodejs/node/pull/45692)
* \[[`049ef342c6`](https://github.com/nodejs/node/commit/049ef342c6)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45637](https://github.com/nodejs/node/pull/45637)
* \[[`b9c2fc7623`](https://github.com/nodejs/node/commit/b9c2fc7623)] - **net**: check `autoSelectFamilyAttemptTimeout` is positive (Deokjin Kim) [#45740](https://github.com/nodejs/node/pull/45740)
* \[[`b8b13dccd9`](https://github.com/nodejs/node/commit/b8b13dccd9)] - **(SEMVER-MINOR)** **net**: add autoSelectFamily and autoSelectFamilyAttemptTimeout options (Paolo Insogna) [#44731](https://github.com/nodejs/node/pull/44731)
* \[[`6962ef0df1`](https://github.com/nodejs/node/commit/6962ef0df1)] - **readline**: improve robustness against prototype mutation (Antoine du Hamel) [#45614](https://github.com/nodejs/node/pull/45614)
* \[[`7892e23e68`](https://github.com/nodejs/node/commit/7892e23e68)] - **repl**: do not define `wasi` on global with no flag (Kohei Ueno) [#45595](https://github.com/nodejs/node/pull/45595)
* \[[`349b4f8817`](https://github.com/nodejs/node/commit/349b4f8817)] - **src**: add internal isArrayBufferDetached (Yagiz Nizipli) [#45568](https://github.com/nodejs/node/pull/45568)
* \[[`5d7cd363ab`](https://github.com/nodejs/node/commit/5d7cd363ab)] - **(SEMVER-MINOR)** **src**: add uvwasi version (Jithil P Ponnan) [#45639](https://github.com/nodejs/node/pull/45639)
* \[[`8a03684018`](https://github.com/nodejs/node/commit/8a03684018)] - **src**: simplify NodeBIO::GetMethod initialization (Anna Henningsen) [#45799](https://github.com/nodejs/node/pull/45799)
* \[[`b35ebebc0e`](https://github.com/nodejs/node/commit/b35ebebc0e)] - **src**: make structuredClone work for process.env (Ben Noordhuis) [#45698](https://github.com/nodejs/node/pull/45698)
* \[[`81ab54035f`](https://github.com/nodejs/node/commit/81ab54035f)] - **src**: mark generated `snapshot_data` as `const` (Anna Henningsen) [#45786](https://github.com/nodejs/node/pull/45786)
* \[[`79edf257bb`](https://github.com/nodejs/node/commit/79edf257bb)] - **src**: cleanup on disambiguating native modules (Michael Dawson) [#45665](https://github.com/nodejs/node/pull/45665)
* \[[`c9cba2e873`](https://github.com/nodejs/node/commit/c9cba2e873)] - **src**: use `enum class` instead of `enum` in node\_i18n (Deokjin Kim) [#45646](https://github.com/nodejs/node/pull/45646)
* \[[`818028caba`](https://github.com/nodejs/node/commit/818028caba)] - **src**: rename internal module declaration as internal bindings (Chengzhong Wu) [#45551](https://github.com/nodejs/node/pull/45551)
* \[[`2fbe2f9f0a`](https://github.com/nodejs/node/commit/2fbe2f9f0a)] - **src,lib**: group properties used as constants from `util` binding (Daeyeon Jeong) [#45539](https://github.com/nodejs/node/pull/45539)
* \[[`56eee72abb`](https://github.com/nodejs/node/commit/56eee72abb)] - **stream**: use structuredClone instead of v8 (Yagiz Nizipli) [#45611](https://github.com/nodejs/node/pull/45611)
* \[[`b297dd5393`](https://github.com/nodejs/node/commit/b297dd5393)] - **test**: remove flaky parallel/test-process-wrap test (Ben Noordhuis) [#45806](https://github.com/nodejs/node/pull/45806)
* \[[`924f6ab3a1`](https://github.com/nodejs/node/commit/924f6ab3a1)] - **test**: order list alphabetically in `test-bootstrap-modules` (Antoine du Hamel) [#45808](https://github.com/nodejs/node/pull/45808)
* \[[`5c4475dab9`](https://github.com/nodejs/node/commit/5c4475dab9)] - **test**: fix invalid output TAP if there newline in test name (Pulkit Gupta) [#45742](https://github.com/nodejs/node/pull/45742)
* \[[`4c51c5c97a`](https://github.com/nodejs/node/commit/4c51c5c97a)] - **test**: fix -Wunused-variable on report-fatalerror (Santiago Gimeno) [#45747](https://github.com/nodejs/node/pull/45747)
* \[[`764725040c`](https://github.com/nodejs/node/commit/764725040c)] - **test**: fix test-watch-mode (Stefan Stojanovic) [#45585](https://github.com/nodejs/node/pull/45585)
* \[[`cd36250fcb`](https://github.com/nodejs/node/commit/cd36250fcb)] - **test**: fix test-watch-mode-inspect (Stefan Stojanovic) [#45586](https://github.com/nodejs/node/pull/45586)
* \[[`b55bd6e8c1`](https://github.com/nodejs/node/commit/b55bd6e8c1)] - **test**: fix typos in test/parallel (Deokjin Kim) [#45583](https://github.com/nodejs/node/pull/45583)
* \[[`358e2fe217`](https://github.com/nodejs/node/commit/358e2fe217)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#45569](https://github.com/nodejs/node/pull/45569)
* \[[`424419c2b4`](https://github.com/nodejs/node/commit/424419c2b4)] - **test\_runner**: refactor `tap_lexer` to use more primordials (Antoine du Hamel) [#45744](https://github.com/nodejs/node/pull/45744)
* \[[`ffc0f3d7be`](https://github.com/nodejs/node/commit/ffc0f3d7be)] - **test\_runner**: refactor `tap_parser` to use more primordials (Antoine du Hamel) [#45745](https://github.com/nodejs/node/pull/45745)
* \[[`4165dcddf0`](https://github.com/nodejs/node/commit/4165dcddf0)] - **(SEMVER-MINOR)** **test\_runner**: add t.after() hook (Colin Ihrig) [#45792](https://github.com/nodejs/node/pull/45792)
* \[[`d1bd7796ad`](https://github.com/nodejs/node/commit/d1bd7796ad)] - **(SEMVER-MINOR)** **test\_runner**: don't use a symbol for runHook() (Colin Ihrig) [#45792](https://github.com/nodejs/node/pull/45792)
* \[[`6bc7b7e6f4`](https://github.com/nodejs/node/commit/6bc7b7e6f4)] - **test\_runner**: add resetCalls to MockFunctionContext (MURAKAMI Masahiko) [#45710](https://github.com/nodejs/node/pull/45710)
* \[[`3e485365ec`](https://github.com/nodejs/node/commit/3e485365ec)] - **test\_runner**: don't parse TAP from stderr (Colin Ihrig) [#45618](https://github.com/nodejs/node/pull/45618)
* \[[`efc44567c9`](https://github.com/nodejs/node/commit/efc44567c9)] - **test\_runner**: add getter and setter to MockTracker (MURAKAMI Masahiko) [#45506](https://github.com/nodejs/node/pull/45506)
* \[[`c9cbd1d396`](https://github.com/nodejs/node/commit/c9cbd1d396)] - **test\_runner**: remove stdout and stderr from error (Colin Ihrig) [#45592](https://github.com/nodejs/node/pull/45592)
* \[[`691f58e76c`](https://github.com/nodejs/node/commit/691f58e76c)] - **tls**: remove trustcor root ca certificates (Ben Noordhuis) [#45776](https://github.com/nodejs/node/pull/45776)
* \[[`d384b73f76`](https://github.com/nodejs/node/commit/d384b73f76)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#45730](https://github.com/nodejs/node/pull/45730)
* \[[`324ae3d5dd`](https://github.com/nodejs/node/commit/324ae3d5dd)] - **tools**: add GitHub token permissions to label flaky-test issues (Gabriela Gutierrez) [#45308](https://github.com/nodejs/node/pull/45308)
* \[[`418ae9be56`](https://github.com/nodejs/node/commit/418ae9be56)] - **tools**: remove dependency vulnerability checker (Facundo Tuesca) [#45675](https://github.com/nodejs/node/pull/45675)
* \[[`238fc64c38`](https://github.com/nodejs/node/commit/238fc64c38)] - **tools**: update lint-md-dependencies to rollup\@3.4.0 (Node.js GitHub Bot) [#45638](https://github.com/nodejs/node/pull/45638)
* \[[`1b98f17876`](https://github.com/nodejs/node/commit/1b98f17876)] - **tools**: update doc to highlight.js\@11.7.0 (Node.js GitHub Bot) [#45636](https://github.com/nodejs/node/pull/45636)
* \[[`470384e7be`](https://github.com/nodejs/node/commit/470384e7be)] - **util**: use private symbols in JS land directly (Joyee Cheung) [#45379](https://github.com/nodejs/node/pull/45379)
* \[[`cee6f382d8`](https://github.com/nodejs/node/commit/cee6f382d8)] - **watch**: add CLI flag to preserve output (Debadree Chatterjee) [#45717](https://github.com/nodejs/node/pull/45717)

<a id="19.2.0"></a>

## 2022-11-29, Version 19.2.0 (Current), @ruyadorno

### Notable changes

#### Time zone update

Time zone data has been updated to 2022f. This includes changes to Daylight Savings Time (DST) for Fiji and Mexico. For more information, see <https://mm.icann.org/pipermail/tz-announce/2022-October/000075.html>.

#### Other notable changes

* **buffer**
  * (SEMVER-MINOR) introduce `File` class (Khafra) [#45139](https://github.com/nodejs/node/pull/45139)
* **deps**
  * update V8 to 10.8.168.20 (Michaël Zasso) [#45230](https://github.com/nodejs/node/pull/45230)
* **doc**
  * deprecate use of invalid ports in `url.parse` (Antoine du Hamel) [#45576](https://github.com/nodejs/node/pull/45576)
* **util**
  * add fast path for utf8 encoding (Yagiz Nizipli) [#45412](https://github.com/nodejs/node/pull/45412)

### Commits

* \[[`7cff1e14ba`](https://github.com/nodejs/node/commit/7cff1e14ba)] - **(SEMVER-MINOR)** **async\_hooks**: add hook to stop propagation (Gerhard Stöbich) [#45386](https://github.com/nodejs/node/pull/45386)
* \[[`f08f6a64a3`](https://github.com/nodejs/node/commit/f08f6a64a3)] - **benchmark**: add v8 serialize benchmark (Yagiz Nizipli) [#45476](https://github.com/nodejs/node/pull/45476)
* \[[`26ad54c1a2`](https://github.com/nodejs/node/commit/26ad54c1a2)] - **benchmark**: add text-encoder benchmark (Yagiz Nizipli) [#45450](https://github.com/nodejs/node/pull/45450)
* \[[`6c56c9722b`](https://github.com/nodejs/node/commit/6c56c9722b)] - **(SEMVER-MINOR)** **buffer**: introduce File (Khafra) [#45139](https://github.com/nodejs/node/pull/45139)
* \[[`6e1e25d6dd`](https://github.com/nodejs/node/commit/6e1e25d6dd)] - **build**: avoid redefined macro (Michaël Zasso) [#45544](https://github.com/nodejs/node/pull/45544)
* \[[`5c9b2a7c82`](https://github.com/nodejs/node/commit/5c9b2a7c82)] - **build**: fix env.h for cpp20 (Jiawen Geng) [#45516](https://github.com/nodejs/node/pull/45516)
* \[[`54fd8a1966`](https://github.com/nodejs/node/commit/54fd8a1966)] - **build**: reset embedder string to "-node.0" (Michaël Zasso) [#45230](https://github.com/nodejs/node/pull/45230)
* \[[`0f3cf7e5ce`](https://github.com/nodejs/node/commit/0f3cf7e5ce)] - _**Revert**_ "**build**: remove precompiled header and debug information for host builds" (Stefan Stojanovic) [#45432](https://github.com/nodejs/node/pull/45432)
* \[[`62ef1eb4ff`](https://github.com/nodejs/node/commit/62ef1eb4ff)] - **build**: add --v8-disable-object-print flag (MURAKAMI Masahiko) [#45458](https://github.com/nodejs/node/pull/45458)
* \[[`1ce2f56cf6`](https://github.com/nodejs/node/commit/1ce2f56cf6)] - **build**: make scripts in gyp run with right python (Jiawen Geng) [#45435](https://github.com/nodejs/node/pull/45435)
* \[[`9ffe3c051a`](https://github.com/nodejs/node/commit/9ffe3c051a)] - **build,deps,src**: fix Intel VTune profiling support (Shi Lei) [#45248](https://github.com/nodejs/node/pull/45248)
* \[[`bd3accc7b2`](https://github.com/nodejs/node/commit/bd3accc7b2)] - **crypto**: clear OpenSSL error queue after calling X509\_check\_private\_key() (Filip Skokan) [#45495](https://github.com/nodejs/node/pull/45495)
* \[[`724addb293`](https://github.com/nodejs/node/commit/724addb293)] - **crypto**: update root certificates (Luigi Pinca) [#45490](https://github.com/nodejs/node/pull/45490)
* \[[`efe19eb7f5`](https://github.com/nodejs/node/commit/efe19eb7f5)] - **crypto**: clear OpenSSL error queue after calling X509\_verify() (Takuro Sato) [#45377](https://github.com/nodejs/node/pull/45377)
* \[[`f63ae525fa`](https://github.com/nodejs/node/commit/f63ae525fa)] - **deps**: V8: cherry-pick 2ada52cffbff (Michaël Zasso) [#45573](https://github.com/nodejs/node/pull/45573)
* \[[`43e002e3d4`](https://github.com/nodejs/node/commit/43e002e3d4)] - **deps**: update base64 to 0.5.0 (Facundo Tuesca) [#45509](https://github.com/nodejs/node/pull/45509)
* \[[`aaa4ac7735`](https://github.com/nodejs/node/commit/aaa4ac7735)] - **deps**: V8: cherry-pick 9df5ef70ff18 (Yagiz Nizipli) [#45230](https://github.com/nodejs/node/pull/45230)
* \[[`e70c3090ff`](https://github.com/nodejs/node/commit/e70c3090ff)] - **deps**: V8: cherry-pick f1c888e7093e (Michaël Zasso) [#45230](https://github.com/nodejs/node/pull/45230)
* \[[`51eb323c50`](https://github.com/nodejs/node/commit/51eb323c50)] - **deps**: V8: cherry-pick 92a7385171bb (Michaël Zasso) [#45230](https://github.com/nodejs/node/pull/45230)
* \[[`1370b1a769`](https://github.com/nodejs/node/commit/1370b1a769)] - **deps**: fix V8 build on Windows with MSVC (Michaël Zasso) [#45230](https://github.com/nodejs/node/pull/45230)
* \[[`3cd6367e6a`](https://github.com/nodejs/node/commit/3cd6367e6a)] - **deps**: silence irrelevant V8 warning (Michaël Zasso) [#45230](https://github.com/nodejs/node/pull/45230)
* \[[`9348bdd28d`](https://github.com/nodejs/node/commit/9348bdd28d)] - **deps**: V8: fix v8-cppgc.h for MSVC (Jiawen Geng) [#45230](https://github.com/nodejs/node/pull/45230)
* \[[`e9292544b0`](https://github.com/nodejs/node/commit/e9292544b0)] - **deps**: fix V8 build issue with inline methods (Jiawen Geng) [#45230](https://github.com/nodejs/node/pull/45230)
* \[[`a3b9967553`](https://github.com/nodejs/node/commit/a3b9967553)] - **deps**: update V8 to 10.8.168.20 (Michaël Zasso) [#45230](https://github.com/nodejs/node/pull/45230)
* \[[`117efe98b0`](https://github.com/nodejs/node/commit/117efe98b0)] - **deps**: V8: cherry-pick 9df5ef70ff18 (Yagiz Nizipli) [#45474](https://github.com/nodejs/node/pull/45474)
* \[[`628891d4dd`](https://github.com/nodejs/node/commit/628891d4dd)] - **deps**: update timezone to 2022f (Node.js GitHub Bot) [#45289](https://github.com/nodejs/node/pull/45289)
* \[[`45ba14b3be`](https://github.com/nodejs/node/commit/45ba14b3be)] - **deps**: fix zlib compilation for CPUs without SIMD features (Anna Henningsen) [#45387](https://github.com/nodejs/node/pull/45387)
* \[[`c41e67fe1d`](https://github.com/nodejs/node/commit/c41e67fe1d)] - **deps**: update zlib to upstream 8bbd6c31 (Luigi Pinca) [#45387](https://github.com/nodejs/node/pull/45387)
* \[[`413bf9ad39`](https://github.com/nodejs/node/commit/413bf9ad39)] - **deps**: patch V8 to 10.7.193.22 (Michaël Zasso) [#45460](https://github.com/nodejs/node/pull/45460)
* \[[`ad8da86b3f`](https://github.com/nodejs/node/commit/ad8da86b3f)] - **deps**: update acorn to 8.8.1 (Node.js GitHub Bot) [#45441](https://github.com/nodejs/node/pull/45441)
* \[[`17e6031bf0`](https://github.com/nodejs/node/commit/17e6031bf0)] - **deps**: V8: cherry-pick 031b98b25cba (Michaël Zasso) [#45375](https://github.com/nodejs/node/pull/45375)
* \[[`9e0e97c121`](https://github.com/nodejs/node/commit/9e0e97c121)] - **diagnostics\_channel**: built-in channels should remain experimental (Stephen Belanger) [#45423](https://github.com/nodejs/node/pull/45423)
* \[[`44886e55e1`](https://github.com/nodejs/node/commit/44886e55e1)] - **diagnostics\_channel**: mark as stable (Stephen Belanger) [#45290](https://github.com/nodejs/node/pull/45290)
* \[[`b6b5b51687`](https://github.com/nodejs/node/commit/b6b5b51687)] - **doc**: deprecate use of invalid ports in `url.parse` (Antoine du Hamel) [#45576](https://github.com/nodejs/node/pull/45576)
* \[[`d805d5a894`](https://github.com/nodejs/node/commit/d805d5a894)] - **doc**: clarify changes in readableFlowing (Kohei Ueno) [#45554](https://github.com/nodejs/node/pull/45554)
* \[[`015842f3d2`](https://github.com/nodejs/node/commit/015842f3d2)] - **doc**: use console.error for error case in http2 (Deokjin Kim) [#45577](https://github.com/nodejs/node/pull/45577)
* \[[`4345732900`](https://github.com/nodejs/node/commit/4345732900)] - **doc**: add version description about fsPromise.constants (chlorine) [#45556](https://github.com/nodejs/node/pull/45556)
* \[[`16643dbb19`](https://github.com/nodejs/node/commit/16643dbb19)] - **doc**: add missing documentation for paramEncoding (Tobias Nießen) [#45523](https://github.com/nodejs/node/pull/45523)
* \[[`246cd358b5`](https://github.com/nodejs/node/commit/246cd358b5)] - **doc**: fix typo in threat model (Tobias Nießen) [#45558](https://github.com/nodejs/node/pull/45558)
* \[[`5b1df22db0`](https://github.com/nodejs/node/commit/5b1df22db0)] - **doc**: add Node.js Threat Model (Rafael Gonzaga) [#45223](https://github.com/nodejs/node/pull/45223)
* \[[`19d8493c92`](https://github.com/nodejs/node/commit/19d8493c92)] - **doc**: run license-builder (github-actions\[bot]) [#45553](https://github.com/nodejs/node/pull/45553)
* \[[`6f0bc097ea`](https://github.com/nodejs/node/commit/6f0bc097ea)] - **doc**: add async\_hooks migration note (Geoffrey Booth) [#45335](https://github.com/nodejs/node/pull/45335)
* \[[`118de4b44c`](https://github.com/nodejs/node/commit/118de4b44c)] - **doc**: fix RESOLVE\_ESM\_MATCH in modules.md (翠 / green) [#45280](https://github.com/nodejs/node/pull/45280)
* \[[`4de67d1ef4`](https://github.com/nodejs/node/commit/4de67d1ef4)] - **doc**: add arm64 to os.machine() (Carter Snook) [#45374](https://github.com/nodejs/node/pull/45374)
* \[[`1812a89c00`](https://github.com/nodejs/node/commit/1812a89c00)] - **doc**: add lint rule to enforce trailing commas (Antoine du Hamel) [#45471](https://github.com/nodejs/node/pull/45471)
* \[[`4128c27f66`](https://github.com/nodejs/node/commit/4128c27f66)] - **doc**: include v19.1.0 in `CHANGELOG.md` (Rafael Gonzaga) [#45462](https://github.com/nodejs/node/pull/45462)
* \[[`94a6a97ec6`](https://github.com/nodejs/node/commit/94a6a97ec6)] - **doc**: adjust wording to eliminate awkward typography (Konv) [#45398](https://github.com/nodejs/node/pull/45398)
* \[[`a6fe707b62`](https://github.com/nodejs/node/commit/a6fe707b62)] - **doc**: fix typo in maintaining-dependencies.md (Tobias Nießen) [#45428](https://github.com/nodejs/node/pull/45428)
* \[[`8906a4e58e`](https://github.com/nodejs/node/commit/8906a4e58e)] - **esm**: add JSDoc property descriptions for loader (Rich Trott) [#45370](https://github.com/nodejs/node/pull/45370)
* \[[`4e5ad9df50`](https://github.com/nodejs/node/commit/4e5ad9df50)] - **esm**: add JSDoc property descriptions for fetch (Rich Trott) [#45370](https://github.com/nodejs/node/pull/45370)
* \[[`2b760c339e`](https://github.com/nodejs/node/commit/2b760c339e)] - **fs**: fix fs.rm support for loop symlinks (Nathanael Ruf) [#45439](https://github.com/nodejs/node/pull/45439)
* \[[`e0a271e41b`](https://github.com/nodejs/node/commit/e0a271e41b)] - **gyp**: fix v8 canary build on aix (Vasili Skurydzin) [#45496](https://github.com/nodejs/node/pull/45496)
* \[[`eac26c0793`](https://github.com/nodejs/node/commit/eac26c0793)] - _**Revert**_ "**http**: headers(Distinct), trailers(Distinct) setters to be no-op" (Rich Trott) [#45527](https://github.com/nodejs/node/pull/45527)
* \[[`f208db70a0`](https://github.com/nodejs/node/commit/f208db70a0)] - **http**: add debug log for ERR\_UNESCAPED\_CHARACTERS (Aidan Temple) [#45420](https://github.com/nodejs/node/pull/45420)
* \[[`b72b2bab72`](https://github.com/nodejs/node/commit/b72b2bab72)] - **http**: add JSDoc property descriptions (Rich Trott) [#45370](https://github.com/nodejs/node/pull/45370)
* \[[`4c9159a830`](https://github.com/nodejs/node/commit/4c9159a830)] - **lib**: improve transferable abort controller exec (Yagiz Nizipli) [#45525](https://github.com/nodejs/node/pull/45525)
* \[[`5745bcbb41`](https://github.com/nodejs/node/commit/5745bcbb41)] - **lib**: improve AbortController creation duration (Yagiz Nizipli) [#45525](https://github.com/nodejs/node/pull/45525)
* \[[`38767b42fb`](https://github.com/nodejs/node/commit/38767b42fb)] - **lib**: do not throw if global property is no longer configurable (Antoine du Hamel) [#45344](https://github.com/nodejs/node/pull/45344)
* \[[`0d1b1c5df0`](https://github.com/nodejs/node/commit/0d1b1c5df0)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45531](https://github.com/nodejs/node/pull/45531)
* \[[`208ea1a58c`](https://github.com/nodejs/node/commit/208ea1a58c)] - **meta**: update VoltrexMaster's username (Mohammed Keyvanzadeh) [#45503](https://github.com/nodejs/node/pull/45503)
* \[[`d13ea68ef6`](https://github.com/nodejs/node/commit/d13ea68ef6)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45443](https://github.com/nodejs/node/pull/45443)
* \[[`6704e7814f`](https://github.com/nodejs/node/commit/6704e7814f)] - **meta**: be more proactive about removing from teams (Rich Trott) [#45352](https://github.com/nodejs/node/pull/45352)
* \[[`6fdd202c57`](https://github.com/nodejs/node/commit/6fdd202c57)] - **module**: require.resolve.paths returns null with node schema (MURAKAMI Masahiko) [#45147](https://github.com/nodejs/node/pull/45147)
* \[[`38f1ede379`](https://github.com/nodejs/node/commit/38f1ede379)] - **node-api**: address coverity warning (Michael Dawson) [#45563](https://github.com/nodejs/node/pull/45563)
* \[[`4a4f2802ec`](https://github.com/nodejs/node/commit/4a4f2802ec)] - **node-api**: declare type napi\_cleanup\_hook (Chengzhong Wu) [#45391](https://github.com/nodejs/node/pull/45391)
* \[[`8ff16fd8c0`](https://github.com/nodejs/node/commit/8ff16fd8c0)] - **node-api**: fix immediate napi\_remove\_wrap test (Chengzhong Wu) [#45406](https://github.com/nodejs/node/pull/45406)
* \[[`e7a5b3347b`](https://github.com/nodejs/node/commit/e7a5b3347b)] - **src**: address coverity warning in node\_file.cc (Michael Dawson) [#45565](https://github.com/nodejs/node/pull/45565)
* \[[`128c9f6fac`](https://github.com/nodejs/node/commit/128c9f6fac)] - **src**: use qualified `std::move` call in node\_http2 (Michaël Zasso) [#45555](https://github.com/nodejs/node/pull/45555)
* \[[`57bca94cb1`](https://github.com/nodejs/node/commit/57bca94cb1)] - **src**: avoid unused variables and functions (Michaël Zasso) [#45542](https://github.com/nodejs/node/pull/45542)
* \[[`649b31f5e5`](https://github.com/nodejs/node/commit/649b31f5e5)] - **src**: add missing include for `std::all_of` (Michaël Zasso) [#45541](https://github.com/nodejs/node/pull/45541)
* \[[`56f22ea47c`](https://github.com/nodejs/node/commit/56f22ea47c)] - **src**: set an appropriate thread pool size if given `--v8-pool-size=0` (Daeyeon Jeong) [#45513](https://github.com/nodejs/node/pull/45513)
* \[[`cce9e11d2d`](https://github.com/nodejs/node/commit/cce9e11d2d)] - **src**: move FsStatsOffset and kFsStatsBufferLength to node\_file.h (Joyee Cheung) [#45498](https://github.com/nodejs/node/pull/45498)
* \[[`5e5bf0c236`](https://github.com/nodejs/node/commit/5e5bf0c236)] - **src**: don't run tasks on isolate termination (Santiago Gimeno) [#45444](https://github.com/nodejs/node/pull/45444)
* \[[`10e7c2a62c`](https://github.com/nodejs/node/commit/10e7c2a62c)] - **src**: remove the unused PackageConfig class (Joyee Cheung) [#45478](https://github.com/nodejs/node/pull/45478)
* \[[`459d4481d4`](https://github.com/nodejs/node/commit/459d4481d4)] - **src**: add --max-semi-space-size to the options allowed in NODE\_OPTIONS (Emanuel Hoogeveen) [#44436](https://github.com/nodejs/node/pull/44436)
* \[[`a483d1291e`](https://github.com/nodejs/node/commit/a483d1291e)] - **src**: condense experimental warning message (Rich Trott) [#45424](https://github.com/nodejs/node/pull/45424)
* \[[`42507e68ab`](https://github.com/nodejs/node/commit/42507e68ab)] - **src,node-api**: update `napi_is_detached_arraybuffer` (Daeyeon Jeong) [#45538](https://github.com/nodejs/node/pull/45538)
* \[[`f720c5880e`](https://github.com/nodejs/node/commit/f720c5880e)] - **stream**: use ArrayBufferPrototypeGetByteLength (Yagiz Nizipli) [#45528](https://github.com/nodejs/node/pull/45528)
* \[[`c00258e24b`](https://github.com/nodejs/node/commit/c00258e24b)] - **stream**: add primordials to adapters (Yagiz Nizipli) [#45511](https://github.com/nodejs/node/pull/45511)
* \[[`5274a8f7db`](https://github.com/nodejs/node/commit/5274a8f7db)] - **stream**: avoid premature close when will not emit close (Robert Nagy) [#45301](https://github.com/nodejs/node/pull/45301)
* \[[`496912d722`](https://github.com/nodejs/node/commit/496912d722)] - **stream**: fix typo in `adapters.js` (#45515) (Kohei Ueno) [#45515](https://github.com/nodejs/node/pull/45515)
* \[[`8d96e2c723`](https://github.com/nodejs/node/commit/8d96e2c723)] - **stream**: add fast path for utf8 (Yagiz Nizipli) [#45483](https://github.com/nodejs/node/pull/45483)
* \[[`c3fe9072c6`](https://github.com/nodejs/node/commit/c3fe9072c6)] - **test**: add trailing commas in event tests (Rich Trott) [#45466](https://github.com/nodejs/node/pull/45466)
* \[[`bb4c293873`](https://github.com/nodejs/node/commit/bb4c293873)] - **test**: add trailing commas in async-hooks tests (#45549) (Antoine du Hamel) [#45549](https://github.com/nodejs/node/pull/45549)
* \[[`731e8741b2`](https://github.com/nodejs/node/commit/731e8741b2)] - **test**: add trailing commas in addons test (#45548) (Antoine du Hamel) [#45548](https://github.com/nodejs/node/pull/45548)
* \[[`d6c68ce346`](https://github.com/nodejs/node/commit/d6c68ce346)] - **test**: add trailing commas in `test/common` (#45550) (Antoine du Hamel) [#45550](https://github.com/nodejs/node/pull/45550)
* \[[`c9ba0b738d`](https://github.com/nodejs/node/commit/c9ba0b738d)] - **test**: revise pull request guide text about code (Rich Trott) [#45519](https://github.com/nodejs/node/pull/45519)
* \[[`076e9eeaeb`](https://github.com/nodejs/node/commit/076e9eeaeb)] - **test**: fix test-trace-gc-flag (Tony Gorez) [#45230](https://github.com/nodejs/node/pull/45230)
* \[[`72f2df2802`](https://github.com/nodejs/node/commit/72f2df2802)] - **test**: adapt test-v8-stats for V8 update (Michaël Zasso) [#45230](https://github.com/nodejs/node/pull/45230)
* \[[`b491504d77`](https://github.com/nodejs/node/commit/b491504d77)] - **test**: enable the WPT for `structuredClone` (Daeyeon Jeong) [#45482](https://github.com/nodejs/node/pull/45482)
* \[[`1277ffcb55`](https://github.com/nodejs/node/commit/1277ffcb55)] - **test**: add lint rule to enforce trailing commas (Antoine du Hamel) [#45468](https://github.com/nodejs/node/pull/45468)
* \[[`45b54eec55`](https://github.com/nodejs/node/commit/45b54eec55)] - **test**: update uses of \_jabber.\_tcp.google.com (Colin Ihrig) [#45451](https://github.com/nodejs/node/pull/45451)
* \[[`51213c24bd`](https://github.com/nodejs/node/commit/51213c24bd)] - **test**: add test to validate changelogs for releases (Richard Lau) [#45325](https://github.com/nodejs/node/pull/45325)
* \[[`00a3b5f7d5`](https://github.com/nodejs/node/commit/00a3b5f7d5)] - **test**: remove flaky designation for test-worker-http2-stream-terminate (Rich Trott) [#45438](https://github.com/nodejs/node/pull/45438)
* \[[`4fe5c4e167`](https://github.com/nodejs/node/commit/4fe5c4e167)] - **test**: fix flaky test-repl-sigint-nested-eval (Rich Trott) [#45354](https://github.com/nodejs/node/pull/45354)
* \[[`f79dd65333`](https://github.com/nodejs/node/commit/f79dd65333)] - **test**: add a test to ensure the correctness of timezone upgrades (Darshan Sen) [#45299](https://github.com/nodejs/node/pull/45299)
* \[[`016749ba5d`](https://github.com/nodejs/node/commit/016749ba5d)] - **test\_runner**: add initial TAP parser (Wassim Chegham) [#43525](https://github.com/nodejs/node/pull/43525)
* \[[`e9760b4ae8`](https://github.com/nodejs/node/commit/e9760b4ae8)] - **test\_runner**: support watch mode (Moshe Atlow) [#45214](https://github.com/nodejs/node/pull/45214)
* \[[`160c88ec77`](https://github.com/nodejs/node/commit/160c88ec77)] - **tools**: have test-asan use ubuntu-20.04 (Filip Skokan) [#45581](https://github.com/nodejs/node/pull/45581)
* \[[`81f63c2b28`](https://github.com/nodejs/node/commit/81f63c2b28)] - **tools**: update eslint to 8.28.0 (Node.js GitHub Bot) [#45532](https://github.com/nodejs/node/pull/45532)
* \[[`f3f1aed01a`](https://github.com/nodejs/node/commit/f3f1aed01a)] - **tools**: add automation for updating libuv dependency (Facundo Tuesca) [#45362](https://github.com/nodejs/node/pull/45362)
* \[[`d4f30f07b3`](https://github.com/nodejs/node/commit/d4f30f07b3)] - **tools**: add missing step in update-base64.sh script (Facundo Tuesca) [#45509](https://github.com/nodejs/node/pull/45509)
* \[[`cca20330cf`](https://github.com/nodejs/node/commit/cca20330cf)] - **tools**: update certdata.txt (Luigi Pinca) [#45490](https://github.com/nodejs/node/pull/45490)
* \[[`39e873139b`](https://github.com/nodejs/node/commit/39e873139b)] - **tools**: include current release in the list of released versions (Antoine du Hamel) [#45463](https://github.com/nodejs/node/pull/45463)
* \[[`8a34ef4897`](https://github.com/nodejs/node/commit/8a34ef4897)] - **tools**: update lint-md-dependencies to rollup\@3.3.0 (Node.js GitHub Bot) [#45442](https://github.com/nodejs/node/pull/45442)
* \[[`bb36acff42`](https://github.com/nodejs/node/commit/bb36acff42)] - **tools**: do not run CQ on non-fast-tracked PRs open for less than 2 days (Moshe Atlow) [#45407](https://github.com/nodejs/node/pull/45407)
* \[[`93bc2ba509`](https://github.com/nodejs/node/commit/93bc2ba509)] - **tools**: simplify .eslintrc.js (Rich Trott) [#45397](https://github.com/nodejs/node/pull/45397)
* \[[`b7f8a44c64`](https://github.com/nodejs/node/commit/b7f8a44c64)] - **tools**: simplify regex in ESLint config (Rich Trott) [#45399](https://github.com/nodejs/node/pull/45399)
* \[[`36bf87fabf`](https://github.com/nodejs/node/commit/36bf87fabf)] - **tools**: enable jsdoc/require-property-description rule (Rich Trott) [#45370](https://github.com/nodejs/node/pull/45370)
* \[[`7c6281a7d2`](https://github.com/nodejs/node/commit/7c6281a7d2)] - **tools**: dynamically determine parallelism on GitHub Actions macOS (Rich Trott) [#45350](https://github.com/nodejs/node/pull/45350)
* \[[`f441b04c11`](https://github.com/nodejs/node/commit/f441b04c11)] - **trace\_events**: add new categories (theanarkh) [#45266](https://github.com/nodejs/node/pull/45266)
* \[[`6bdd2c3884`](https://github.com/nodejs/node/commit/6bdd2c3884)] - _**Revert**_ "**url**: improve port validation" (Rich Trott) [#45517](https://github.com/nodejs/node/pull/45517)
* \[[`bbba42fcb2`](https://github.com/nodejs/node/commit/bbba42fcb2)] - **url**: remove unnecessary object call to kFormat (Yagiz Nizipli) [#45492](https://github.com/nodejs/node/pull/45492)
* \[[`7c79ba7b27`](https://github.com/nodejs/node/commit/7c79ba7b27)] - **util**: add fast path for utf8 encoding (Yagiz Nizipli) [#45412](https://github.com/nodejs/node/pull/45412)
* \[[`f86f90f839`](https://github.com/nodejs/node/commit/f86f90f839)] - **util**: improve text decoder performance (Yagiz Nizipli) [#45388](https://github.com/nodejs/node/pull/45388)
* \[[`3263ceb21a`](https://github.com/nodejs/node/commit/3263ceb21a)] - **watch**: watch for missing dependencies (Moshe Atlow) [#45348](https://github.com/nodejs/node/pull/45348)

<a id="19.1.0"></a>

## 2022-11-14, Version 19.1.0 (Current), @RafaelGSS

### Notable changes

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

#### fs.watch recursive support on Linux

`fs.watch` supports recursive watch using the `recursive: true` option.

```js
const watcher = fs.watch(testDirectory, { recursive: true });
watcher.on('change', function(event, filename) {
});
```

Contributed by Yagiz Nizipli in [#45098](https://github.com/nodejs/node/pull/45098)

#### Other notable changes

* **deps**
  * update ICU to 72.1 (Michaël Zasso) [#45068](https://github.com/nodejs/node/pull/45068)
* **doc**
  * add lukekarrys to collaborators (Luke Karrys) [#45180](https://github.com/nodejs/node/pull/45180)
  * add anonrig to collaborators (Yagiz Nizipli) [#45002](https://github.com/nodejs/node/pull/45002)
* **lib**
  * drop fetch experimental warning (Matteo Collina) [#45287](https://github.com/nodejs/node/pull/45287)
* **util**
  * (SEMVER-MINOR) add MIME utilities (Bradley Farias) [#21128](https://github.com/nodejs/node/pull/21128)
  * improve textdecoder decode performance (Yagiz Nizipli) [#45294](https://github.com/nodejs/node/pull/45294)

### Commits

* \[[`c9cf399ec7`](https://github.com/nodejs/node/commit/c9cf399ec7)] - **benchmark**: add parameters to text-decoder benchmark (Yagiz Nizipli) [#45363](https://github.com/nodejs/node/pull/45363)
* \[[`79f6bb061d`](https://github.com/nodejs/node/commit/79f6bb061d)] - **benchmark**: fix text-decoder benchmark (Yagiz Nizipli) [#45363](https://github.com/nodejs/node/pull/45363)
* \[[`a27c994ced`](https://github.com/nodejs/node/commit/a27c994ced)] - **benchmark**: add blob benchmark (Yagiz Nizipli) [#44990](https://github.com/nodejs/node/pull/44990)
* \[[`c45b6aee78`](https://github.com/nodejs/node/commit/c45b6aee78)] - **bootstrap**: merge main thread and worker thread initializations (Joyee Cheung) [#44869](https://github.com/nodejs/node/pull/44869)
* \[[`33691208df`](https://github.com/nodejs/node/commit/33691208df)] - **buffer**: fix validation of options in `Blob` constructor (Antoine du Hamel) [#45156](https://github.com/nodejs/node/pull/45156)
* \[[`7b938df296`](https://github.com/nodejs/node/commit/7b938df296)] - **build**: support Python 3.11 (Luigi Pinca) [#45191](https://github.com/nodejs/node/pull/45191)
* \[[`75e0a2d109`](https://github.com/nodejs/node/commit/75e0a2d109)] - **build**: workaround for node-core-utils (Jiawen Geng) [#45199](https://github.com/nodejs/node/pull/45199)
* \[[`f598edbdf4`](https://github.com/nodejs/node/commit/f598edbdf4)] - **build**: fix icu-small build with ICU 72.1 (Steven R. Loomis) [#45195](https://github.com/nodejs/node/pull/45195)
* \[[`29b9f4f90c`](https://github.com/nodejs/node/commit/29b9f4f90c)] - **build**: remove unused language files (Ben Noordhuis) [#45138](https://github.com/nodejs/node/pull/45138)
* \[[`3a1ee940d1`](https://github.com/nodejs/node/commit/3a1ee940d1)] - **build**: add GitHub token to auto-start-ci workflow (Richard Lau) [#45185](https://github.com/nodejs/node/pull/45185)
* \[[`17349a2f42`](https://github.com/nodejs/node/commit/17349a2f42)] - **build**: restore Windows resource file (Richard Lau) [#45042](https://github.com/nodejs/node/pull/45042)
* \[[`24e24bd063`](https://github.com/nodejs/node/commit/24e24bd063)] - **build**: add version info to timezone update PR (Darshan Sen) [#45021](https://github.com/nodejs/node/pull/45021)
* \[[`8d7aa53e6b`](https://github.com/nodejs/node/commit/8d7aa53e6b)] - **build,win**: pass --debug-nghttp2 to configure (Santiago Gimeno) [#45209](https://github.com/nodejs/node/pull/45209)
* \[[`b2e60480f3`](https://github.com/nodejs/node/commit/b2e60480f3)] - **child\_process**: validate arguments for null bytes (Darshan Sen) [#44782](https://github.com/nodejs/node/pull/44782)
* \[[`1f0edde412`](https://github.com/nodejs/node/commit/1f0edde412)] - **crypto**: handle more webcrypto errors with OperationError (Filip Skokan) [#45320](https://github.com/nodejs/node/pull/45320)
* \[[`13fb05e12b`](https://github.com/nodejs/node/commit/13fb05e12b)] - **crypto**: handle unsupported AES ciphers in webcrypto (Filip Skokan) [#45321](https://github.com/nodejs/node/pull/45321)
* \[[`c168cbfbb3`](https://github.com/nodejs/node/commit/c168cbfbb3)] - **deps**: V8: cherry-pick 56816d76c121 (Shi Pujin) [#45353](https://github.com/nodejs/node/pull/45353)
* \[[`1432474abf`](https://github.com/nodejs/node/commit/1432474abf)] - **deps**: upgrade npm to 8.19.3 (npm team) [#45322](https://github.com/nodejs/node/pull/45322)
* \[[`f35d56200d`](https://github.com/nodejs/node/commit/f35d56200d)] - **deps**: update corepack to 0.15.1 (Node.js GitHub Bot) [#45331](https://github.com/nodejs/node/pull/45331)
* \[[`44de2321aa`](https://github.com/nodejs/node/commit/44de2321aa)] - **deps**: patch V8 to 10.7.193.20 (Michaël Zasso) [#45228](https://github.com/nodejs/node/pull/45228)
* \[[`bfe3819f08`](https://github.com/nodejs/node/commit/bfe3819f08)] - **deps**: upgrade to libuv 1.44.2 (Luigi Pinca) [#42340](https://github.com/nodejs/node/pull/42340)
* \[[`0d41df96b3`](https://github.com/nodejs/node/commit/0d41df96b3)] - **deps**: update corepack to 0.15.0 (Node.js GitHub Bot) [#45235](https://github.com/nodejs/node/pull/45235)
* \[[`0d241638ca`](https://github.com/nodejs/node/commit/0d241638ca)] - **deps**: update undici to 5.12.0 (Node.js GitHub Bot) [#45236](https://github.com/nodejs/node/pull/45236)
* \[[`f58996188a`](https://github.com/nodejs/node/commit/f58996188a)] - _**Revert**_ "**deps**: make V8 compilable with older glibc" (Michaël Zasso) [#45162](https://github.com/nodejs/node/pull/45162)
* \[[`8cda730e58`](https://github.com/nodejs/node/commit/8cda730e58)] - **deps**: update ICU to 72.1 (Michaël Zasso) [#45068](https://github.com/nodejs/node/pull/45068)
* \[[`0a6ed6f710`](https://github.com/nodejs/node/commit/0a6ed6f710)] - _**Revert**_ "**deps**: V8: forward declaration of `Rtl*FunctionTable`" (Michaël Zasso) [#45119](https://github.com/nodejs/node/pull/45119)
* \[[`2f7518ada2`](https://github.com/nodejs/node/commit/2f7518ada2)] - **deps**: update timezone (Node.js GitHub Bot) [#44950](https://github.com/nodejs/node/pull/44950)
* \[[`3bfba6df79`](https://github.com/nodejs/node/commit/3bfba6df79)] - **deps**: patch V8 to 10.7.193.16 (Michaël Zasso) [#45023](https://github.com/nodejs/node/pull/45023)
* \[[`b5baaa61b3`](https://github.com/nodejs/node/commit/b5baaa61b3)] - **dns**: fix port validation (Antoine du Hamel) [#45135](https://github.com/nodejs/node/pull/45135)
* \[[`0e9bad97cc`](https://github.com/nodejs/node/commit/0e9bad97cc)] - **doc**: allow for holidays in triage response (Michael Dawson) [#45267](https://github.com/nodejs/node/pull/45267)
* \[[`d4aabb9d3d`](https://github.com/nodejs/node/commit/d4aabb9d3d)] - **doc**: include last security release date (Juan José Arboleda) [#45368](https://github.com/nodejs/node/pull/45368)
* \[[`ba45373164`](https://github.com/nodejs/node/commit/ba45373164)] - **doc**: fix email for Ashley (Michael Dawson) [#45364](https://github.com/nodejs/node/pull/45364)
* \[[`d5e5c75b13`](https://github.com/nodejs/node/commit/d5e5c75b13)] - **doc**: fix test runner's only tests section header (Colin Ihrig) [#45343](https://github.com/nodejs/node/pull/45343)
* \[[`a7c5f31c47`](https://github.com/nodejs/node/commit/a7c5f31c47)] - **doc**: run license-builder (github-actions\[bot]) [#45349](https://github.com/nodejs/node/pull/45349)
* \[[`3de125743e`](https://github.com/nodejs/node/commit/3de125743e)] - **doc**: add more info for timer.setInterval (theanarkh) [#45232](https://github.com/nodejs/node/pull/45232)
* \[[`5a1252d9b4`](https://github.com/nodejs/node/commit/5a1252d9b4)] - **doc**: use module names in stability overview table (Filip Skokan) [#45312](https://github.com/nodejs/node/pull/45312)
* \[[`4d38bf2c5f`](https://github.com/nodejs/node/commit/4d38bf2c5f)] - **doc**: add `node:` prefix for examples (Daeyeon Jeong) [#45328](https://github.com/nodejs/node/pull/45328)
* \[[`b4b6b95f48`](https://github.com/nodejs/node/commit/b4b6b95f48)] - **doc**: update name of Node.js core Slack channel (Rich Trott) [#45293](https://github.com/nodejs/node/pull/45293)
* \[[`7d7e7c316b`](https://github.com/nodejs/node/commit/7d7e7c316b)] - **doc**: fix "task\_processor.js" typo (andreysoktoev) [#45257](https://github.com/nodejs/node/pull/45257)
* \[[`b9039a54af`](https://github.com/nodejs/node/commit/b9039a54af)] - **doc**: add history section to `fetch`-related globals (Antoine du Hamel) [#45198](https://github.com/nodejs/node/pull/45198)
* \[[`d9163f1632`](https://github.com/nodejs/node/commit/d9163f1632)] - **doc**: clarify moderation in `onboarding.md` (Benjamin Gruenbaum) [#41930](https://github.com/nodejs/node/pull/41930)
* \[[`c179c1478b`](https://github.com/nodejs/node/commit/c179c1478b)] - **doc**: change make lint to make lint-md (RafaelGSS) [#45197](https://github.com/nodejs/node/pull/45197)
* \[[`58bec56fab`](https://github.com/nodejs/node/commit/58bec56fab)] - **doc**: add more lts update steps to release guide (Ruy Adorno) [#45177](https://github.com/nodejs/node/pull/45177)
* \[[`8f8d7e76ac`](https://github.com/nodejs/node/commit/8f8d7e76ac)] - **doc**: add bmuenzenmeyer to triagers (Brian Muenzenmeyer) [#45155](https://github.com/nodejs/node/pull/45155)
* \[[`de2df550f6`](https://github.com/nodejs/node/commit/de2df550f6)] - **doc**: update process.release (Filip Skokan) [#45170](https://github.com/nodejs/node/pull/45170)
* \[[`916e8760ba`](https://github.com/nodejs/node/commit/916e8760ba)] - **doc**: add link to triage guide (Brian Muenzenmeyer) [#45154](https://github.com/nodejs/node/pull/45154)
* \[[`54d806853e`](https://github.com/nodejs/node/commit/54d806853e)] - **doc**: mark Node.js 12 as End-of-Life (Rafael Gonzaga) [#45186](https://github.com/nodejs/node/pull/45186)
* \[[`3a26347649`](https://github.com/nodejs/node/commit/3a26347649)] - **doc**: add lukekarrys to collaborators (Luke Karrys) [#45180](https://github.com/nodejs/node/pull/45180)
* \[[`85cb4d795c`](https://github.com/nodejs/node/commit/85cb4d795c)] - **doc**: update mark release line lts on release guide (Ruy Adorno) [#45101](https://github.com/nodejs/node/pull/45101)
* \[[`c23e023a2d`](https://github.com/nodejs/node/commit/c23e023a2d)] - **doc**: be more definite and present tense-y (Ben Noordhuis) [#45120](https://github.com/nodejs/node/pull/45120)
* \[[`519002152b`](https://github.com/nodejs/node/commit/519002152b)] - **doc**: add major version note to release guide (Ruy Adorno) [#45054](https://github.com/nodejs/node/pull/45054)
* \[[`809e8dcbd2`](https://github.com/nodejs/node/commit/809e8dcbd2)] - **doc**: fix v14.x link maintaining openssl guide (RafaelGSS) [#45071](https://github.com/nodejs/node/pull/45071)
* \[[`9d449d389d`](https://github.com/nodejs/node/commit/9d449d389d)] - **doc**: add note about latest GitHub release (Michaël Zasso) [#45111](https://github.com/nodejs/node/pull/45111)
* \[[`ee34a3a1bc`](https://github.com/nodejs/node/commit/ee34a3a1bc)] - **doc**: mention v18.x openssl maintaining guide (Rafael Gonzaga) [#45070](https://github.com/nodejs/node/pull/45070)
* \[[`3e4033a90d`](https://github.com/nodejs/node/commit/3e4033a90d)] - **doc**: fix display of "problematic" ASCII characters (John Gardner) [#44373](https://github.com/nodejs/node/pull/44373)
* \[[`533e38b0b8`](https://github.com/nodejs/node/commit/533e38b0b8)] - **doc**: mark Node.js v17.x as EOL (KaKa) [#45110](https://github.com/nodejs/node/pull/45110)
* \[[`93a34faa39`](https://github.com/nodejs/node/commit/93a34faa39)] - **doc**: update Node.js 16 End-of-Life date (Richard Lau) [#45103](https://github.com/nodejs/node/pull/45103)
* \[[`b4beddef79`](https://github.com/nodejs/node/commit/b4beddef79)] - **doc**: fix typo in parseArgs default value (Tobias Nießen) [#45083](https://github.com/nodejs/node/pull/45083)
* \[[`e8103fd33b`](https://github.com/nodejs/node/commit/e8103fd33b)] - **doc**: updated security stewards (Michael Dawson) [#45005](https://github.com/nodejs/node/pull/45005)
* \[[`5fbccae4f0`](https://github.com/nodejs/node/commit/5fbccae4f0)] - **doc**: fix http and http2 writeEarlyHints() parameter (Fabian Meyer) [#45000](https://github.com/nodejs/node/pull/45000)
* \[[`d47f83251a`](https://github.com/nodejs/node/commit/d47f83251a)] - **doc**: run license-builder (github-actions\[bot]) [#45034](https://github.com/nodejs/node/pull/45034)
* \[[`e6bbc5033d`](https://github.com/nodejs/node/commit/e6bbc5033d)] - **doc**: improve the workflow to test release binaries (Rafael Gonzaga) [#45004](https://github.com/nodejs/node/pull/45004)
* \[[`f0c18f04f0`](https://github.com/nodejs/node/commit/f0c18f04f0)] - **doc**: fix undici version in changelog (Michael Dawson) [#44982](https://github.com/nodejs/node/pull/44982)
* \[[`ffba3218ec`](https://github.com/nodejs/node/commit/ffba3218ec)] - **doc**: add info on fixup to security release process (Michael Dawson) [#44807](https://github.com/nodejs/node/pull/44807)
* \[[`edb92f4510`](https://github.com/nodejs/node/commit/edb92f4510)] - **doc**: add anonrig to collaborators (Yagiz Nizipli) [#45002](https://github.com/nodejs/node/pull/45002)
* \[[`58334a38e8`](https://github.com/nodejs/node/commit/58334a38e8)] - **doc, async\_hooks**: improve and add migration hints (Gerhard Stöbich) [#45369](https://github.com/nodejs/node/pull/45369)
* \[[`7225a7d46b`](https://github.com/nodejs/node/commit/7225a7d46b)] - **doc, http**: add Uint8Array as allowed type (Gerhard Stöbich) [#45167](https://github.com/nodejs/node/pull/45167)
* \[[`40a5e22328`](https://github.com/nodejs/node/commit/40a5e22328)] - **esm**: protect ESM loader from prototype pollution (Antoine du Hamel) [#45175](https://github.com/nodejs/node/pull/45175)
* \[[`2e5d8e7239`](https://github.com/nodejs/node/commit/2e5d8e7239)] - **esm**: protect ESM loader from prototype pollution (Antoine du Hamel) [#45044](https://github.com/nodejs/node/pull/45044)
* \[[`c3dd696081`](https://github.com/nodejs/node/commit/c3dd696081)] - **events**: add unique events benchmark (Yagiz Nizipli) [#44657](https://github.com/nodejs/node/pull/44657)
* \[[`daff3b8b09`](https://github.com/nodejs/node/commit/daff3b8b09)] - **fs**: update todo message (Yagiz Nizipli) [#45265](https://github.com/nodejs/node/pull/45265)
* \[[`670def3d6f`](https://github.com/nodejs/node/commit/670def3d6f)] - **fs**: fix opts.filter issue in cpSync (Tho) [#45143](https://github.com/nodejs/node/pull/45143)
* \[[`34bfef91a9`](https://github.com/nodejs/node/commit/34bfef91a9)] - **(SEMVER-MINOR)** **fs**: add recursive watch to linux (Yagiz Nizipli) [#45098](https://github.com/nodejs/node/pull/45098)
* \[[`d89ca1b443`](https://github.com/nodejs/node/commit/d89ca1b443)] - **fs**: trace more fs api (theanarkh) [#45095](https://github.com/nodejs/node/pull/45095)
* \[[`1a04881494`](https://github.com/nodejs/node/commit/1a04881494)] - **http**: headers(Distinct), trailers(Distinct) setters to be no-op (Madhuri) [#45176](https://github.com/nodejs/node/pull/45176)
* \[[`8abc3f732a`](https://github.com/nodejs/node/commit/8abc3f732a)] - **http**: add priority to common http headers (James M Snell) [#45045](https://github.com/nodejs/node/pull/45045)
* \[[`316354e3d3`](https://github.com/nodejs/node/commit/316354e3d3)] - **http2**: improve session close/destroy procedures (Santiago Gimeno) [#45115](https://github.com/nodejs/node/pull/45115)
* \[[`1635140952`](https://github.com/nodejs/node/commit/1635140952)] - **http2**: fix crash on Http2Stream::diagnostic\_name() (Santiago Gimeno) [#45123](https://github.com/nodejs/node/pull/45123)
* \[[`94b7f5338c`](https://github.com/nodejs/node/commit/94b7f5338c)] - **http2**: fix debugStream method (Santiago Gimeno) [#45129](https://github.com/nodejs/node/pull/45129)
* \[[`3db37e7d1b`](https://github.com/nodejs/node/commit/3db37e7d1b)] - **inspector**: refactor `inspector/promises` to be more robust (Antoine du Hamel) [#45041](https://github.com/nodejs/node/pull/45041)
* \[[`0478e4063f`](https://github.com/nodejs/node/commit/0478e4063f)] - **lib**: add options to the heap snapshot APIs (Joyee Cheung) [#44989](https://github.com/nodejs/node/pull/44989)
* \[[`a8e901555a`](https://github.com/nodejs/node/commit/a8e901555a)] - **lib**: fix JSDoc issues (Rich Trott) [#45243](https://github.com/nodejs/node/pull/45243)
* \[[`74352842bc`](https://github.com/nodejs/node/commit/74352842bc)] - **lib**: use process.nextTick() instead of setImmediate() (Luigi Pinca) [#42340](https://github.com/nodejs/node/pull/42340)
* \[[`9f3d2f6879`](https://github.com/nodejs/node/commit/9f3d2f6879)] - **lib**: drop fetch experimental warning (Matteo Collina) [#45287](https://github.com/nodejs/node/pull/45287)
* \[[`e2181e057b`](https://github.com/nodejs/node/commit/e2181e057b)] - **lib**: fix eslint early return (RafaelGSS) [#45409](https://github.com/nodejs/node/pull/45409)
* \[[`d1726692ee`](https://github.com/nodejs/node/commit/d1726692ee)] - **lib**: fix TypeError when converting a detached buffer source (Kohei Ueno) [#44020](https://github.com/nodejs/node/pull/44020)
* \[[`d7470ad986`](https://github.com/nodejs/node/commit/d7470ad986)] - **lib**: fix `AbortSignal.timeout` parameter validation (dnalborczyk) [#42856](https://github.com/nodejs/node/pull/42856)
* \[[`c7b7f2bec2`](https://github.com/nodejs/node/commit/c7b7f2bec2)] - **lib**: add lint rule to protect against `Object.prototype.then` pollution (Antoine du Hamel) [#45061](https://github.com/nodejs/node/pull/45061)
* \[[`9ed9aa8233`](https://github.com/nodejs/node/commit/9ed9aa8233)] - **lib**: add ability to add separate event name to defineEventHandler (James M Snell) [#45032](https://github.com/nodejs/node/pull/45032)
* \[[`8b4a41e23d`](https://github.com/nodejs/node/commit/8b4a41e23d)] - **lib**: fix typo in `pre_execution.js` (Antoine du Hamel) [#45039](https://github.com/nodejs/node/pull/45039)
* \[[`cc2393c9fe`](https://github.com/nodejs/node/commit/cc2393c9fe)] - **lib**: promise version of streams.finished call clean up (Naor Tedgi (Abu Emma)) [#44862](https://github.com/nodejs/node/pull/44862)
* \[[`17ef1bbc8e`](https://github.com/nodejs/node/commit/17ef1bbc8e)] - **lib**: make properties on Blob and URL enumerable (Khafra) [#44918](https://github.com/nodejs/node/pull/44918)
* \[[`8199841e9c`](https://github.com/nodejs/node/commit/8199841e9c)] - **lib**: support more attributes for early hint link (Yagiz Nizipli) [#44874](https://github.com/nodejs/node/pull/44874)
* \[[`88c3bb609b`](https://github.com/nodejs/node/commit/88c3bb609b)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45333](https://github.com/nodejs/node/pull/45333)
* \[[`a866e8c163`](https://github.com/nodejs/node/commit/a866e8c163)] - **meta**: update collaborator email address in README (Rich Trott) [#45251](https://github.com/nodejs/node/pull/45251)
* \[[`bfbfacad79`](https://github.com/nodejs/node/commit/bfbfacad79)] - **meta**: fix email address typo in README (Rich Trott) [#45250](https://github.com/nodejs/node/pull/45250)
* \[[`0d58bb9531`](https://github.com/nodejs/node/commit/0d58bb9531)] - **meta**: remove dont-land-on-v12 auto labeling (Moshe Atlow) [#45233](https://github.com/nodejs/node/pull/45233)
* \[[`b41b5ba658`](https://github.com/nodejs/node/commit/b41b5ba658)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45238](https://github.com/nodejs/node/pull/45238)
* \[[`ad9a5bb61f`](https://github.com/nodejs/node/commit/ad9a5bb61f)] - **meta**: move a collaborator to emeritus (Rich Trott) [#45160](https://github.com/nodejs/node/pull/45160)
* \[[`ec8683052b`](https://github.com/nodejs/node/commit/ec8683052b)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#45036](https://github.com/nodejs/node/pull/45036)
* \[[`7900810fb3`](https://github.com/nodejs/node/commit/7900810fb3)] - **meta**: update AUTHORS (Node.js GitHub Bot) [#45020](https://github.com/nodejs/node/pull/45020)
* \[[`738144c311`](https://github.com/nodejs/node/commit/738144c311)] - **module**: ensure relative requires work from deleted directories (Bradley Farias) [#42384](https://github.com/nodejs/node/pull/42384)
* \[[`36acf8a13e`](https://github.com/nodejs/node/commit/36acf8a13e)] - **net**: remove \_readableState from debug statement (Rich Trott) [#45063](https://github.com/nodejs/node/pull/45063)
* \[[`aaca54c5c0`](https://github.com/nodejs/node/commit/aaca54c5c0)] - **node-api**: handle no support for external buffers (Michael Dawson) [#45181](https://github.com/nodejs/node/pull/45181)
* \[[`2105f099ea`](https://github.com/nodejs/node/commit/2105f099ea)] - **node-api,test**: fix test\_reference\_double\_free crash (Vladimir Morozov) [#44927](https://github.com/nodejs/node/pull/44927)
* \[[`2fcf851a91`](https://github.com/nodejs/node/commit/2fcf851a91)] - **os**: convert uid and gid to 32-bit signed integers (Luigi Pinca) [#42340](https://github.com/nodejs/node/pull/42340)
* \[[`dfe4237d77`](https://github.com/nodejs/node/commit/dfe4237d77)] - **perf\_hooks**: align toStringTag with other Web Performance implementations (Daeyeon Jeong) [#45157](https://github.com/nodejs/node/pull/45157)
* \[[`9d15da3341`](https://github.com/nodejs/node/commit/9d15da3341)] - **report**: add more memory info (theanarkh) [#45254](https://github.com/nodejs/node/pull/45254)
* \[[`a2620acad7`](https://github.com/nodejs/node/commit/a2620acad7)] - **report**: add rss and use/kernel cpu usage fields (theanarkh) [#45043](https://github.com/nodejs/node/pull/45043)
* \[[`66e1dc4979`](https://github.com/nodejs/node/commit/66e1dc4979)] - **report,doc**: define report version semantics (Gireesh Punathil) [#45050](https://github.com/nodejs/node/pull/45050)
* \[[`86e22b4e19`](https://github.com/nodejs/node/commit/86e22b4e19)] - **src**: track contexts in the Environment instead of AsyncHooks (Joyee Cheung) [#45282](https://github.com/nodejs/node/pull/45282)
* \[[`326d19af3d`](https://github.com/nodejs/node/commit/326d19af3d)] - **src**: resolve TODO related to inspector CVEs (Tobias Nießen) [#45341](https://github.com/nodejs/node/pull/45341)
* \[[`4e45585ca2`](https://github.com/nodejs/node/commit/4e45585ca2)] - **src**: revert is\_release to 0 (RafaelGSS) [#45315](https://github.com/nodejs/node/pull/45315)
* \[[`5d480118fb`](https://github.com/nodejs/node/commit/5d480118fb)] - **src**: print nghttp2 logs when using --debug-nghttp2 (Santiago Gimeno) [#45209](https://github.com/nodejs/node/pull/45209)
* \[[`3e46ebda3c`](https://github.com/nodejs/node/commit/3e46ebda3c)] - **src**: trace threadpool event (theanarkh) [#44458](https://github.com/nodejs/node/pull/44458)
* \[[`97547bcd14`](https://github.com/nodejs/node/commit/97547bcd14)] - **src**: lock-free init\_process\_flags (Jérémy Lal) [#45221](https://github.com/nodejs/node/pull/45221)
* \[[`42db84913b`](https://github.com/nodejs/node/commit/42db84913b)] - **src**: call uv\_library\_shutdown before DisposePlatform (theanarkh) [#45226](https://github.com/nodejs/node/pull/45226)
* \[[`aa4152a1b6`](https://github.com/nodejs/node/commit/aa4152a1b6)] - **src**: fix `crypto.privateEncrypt` fails first time (liuxingbaoyu) [#42793](https://github.com/nodejs/node/pull/42793)
* \[[`243c141b69`](https://github.com/nodejs/node/commit/243c141b69)] - **src**: clarify OptionEnvvarSettings member names (Chengzhong Wu) [#45057](https://github.com/nodejs/node/pull/45057)
* \[[`5335e29ce7`](https://github.com/nodejs/node/commit/5335e29ce7)] - **src**: let http2 streams end after session close (Santiago Gimeno) [#45153](https://github.com/nodejs/node/pull/45153)
* \[[`8d5682266e`](https://github.com/nodejs/node/commit/8d5682266e)] - **src**: remap invalid file descriptors using `dup2` (Obiwac) [#44461](https://github.com/nodejs/node/pull/44461)
* \[[`4e14ed8878`](https://github.com/nodejs/node/commit/4e14ed8878)] - **src**: remove unused `contextify_global_private_symbol` (Daeyeon Jeong) [#45128](https://github.com/nodejs/node/pull/45128)
* \[[`a8412f5677`](https://github.com/nodejs/node/commit/a8412f5677)] - **src**: forbid running watch mode in REPL (Moshe Atlow) [#45058](https://github.com/nodejs/node/pull/45058)
* \[[`162bf0ddff`](https://github.com/nodejs/node/commit/162bf0ddff)] - **src**: fix test runner coverage (Moshe Atlow) [#45055](https://github.com/nodejs/node/pull/45055)
* \[[`e5b1179630`](https://github.com/nodejs/node/commit/e5b1179630)] - **src**: optimize ALPN callback (Ben Noordhuis) [#44875](https://github.com/nodejs/node/pull/44875)
* \[[`9dc21a1f86`](https://github.com/nodejs/node/commit/9dc21a1f86)] - **src**: simplify ALPN code, remove indirection (Ben Noordhuis) [#44875](https://github.com/nodejs/node/pull/44875)
* \[[`5fce8e3495`](https://github.com/nodejs/node/commit/5fce8e3495)] - **src**: iwyu in cleanup\_queue.cc (Shelley Vohr) [#44983](https://github.com/nodejs/node/pull/44983)
* \[[`824dcfc422`](https://github.com/nodejs/node/commit/824dcfc422)] - **src**: return void in InitializeInspector() (Joyee Cheung) [#44903](https://github.com/nodejs/node/pull/44903)
* \[[`7a31ae8ab1`](https://github.com/nodejs/node/commit/7a31ae8ab1)] - **src,lib**: retrieve parsed source map url from v8 (Chengzhong Wu) [#44798](https://github.com/nodejs/node/pull/44798)
* \[[`ccb1c1e9a2`](https://github.com/nodejs/node/commit/ccb1c1e9a2)] - **stream**: add compose operator (Raz Luvaton) [#44937](https://github.com/nodejs/node/pull/44937)
* \[[`e60d9053bc`](https://github.com/nodejs/node/commit/e60d9053bc)] - **stream**: fix duplexify premature destroy (Robert Nagy) [#45133](https://github.com/nodejs/node/pull/45133)
* \[[`bc0ae3e74e`](https://github.com/nodejs/node/commit/bc0ae3e74e)] - **stream**: fix web streams have no Symbol.toStringTag (Jithil P Ponnan) [#45117](https://github.com/nodejs/node/pull/45117)
* \[[`1655532fd2`](https://github.com/nodejs/node/commit/1655532fd2)] - **stream**: don't push null from closed promise #42694 (David Halls) [#45026](https://github.com/nodejs/node/pull/45026)
* \[[`717db1d46a`](https://github.com/nodejs/node/commit/717db1d46a)] - **test**: skip test-fs-largefile if not enough disk space (Rich Trott) [#45339](https://github.com/nodejs/node/pull/45339)
* \[[`4a80aff16e`](https://github.com/nodejs/node/commit/4a80aff16e)] - **test**: fix catching failed assertion (Pavel Horal) [#45222](https://github.com/nodejs/node/pull/45222)
* \[[`66e7821506`](https://github.com/nodejs/node/commit/66e7821506)] - **test**: defer invocation checks (Luigi Pinca) [#42340](https://github.com/nodejs/node/pull/42340)
* \[[`43db0fbd49`](https://github.com/nodejs/node/commit/43db0fbd49)] - **test**: fix test-socket-write-after-fin-error (Luigi Pinca) [#42340](https://github.com/nodejs/node/pull/42340)
* \[[`d5f4d98847`](https://github.com/nodejs/node/commit/d5f4d98847)] - **test**: make `test-eventemitter-asyncresource.js` shorter (Juan José) [#45146](https://github.com/nodejs/node/pull/45146)
* \[[`7428651100`](https://github.com/nodejs/node/commit/7428651100)] - **test**: convert test-debugger-pid to async/await (Luke Karrys) [#45179](https://github.com/nodejs/node/pull/45179)
* \[[`f10f2c1121`](https://github.com/nodejs/node/commit/f10f2c1121)] - **test**: fix textdecoder test for small-icu builds (Richard Lau) [#45225](https://github.com/nodejs/node/pull/45225)
* \[[`eed799bd31`](https://github.com/nodejs/node/commit/eed799bd31)] - **test**: improve test coverage in `test-event-capture-rejections.js` (Juan José) [#45148](https://github.com/nodejs/node/pull/45148)
* \[[`069747bfdd`](https://github.com/nodejs/node/commit/069747bfdd)] - **test**: fix timeout of test-heap-prof.js in riscv devices (Yu Gu) [#42674](https://github.com/nodejs/node/pull/42674)
* \[[`ddb7df76de`](https://github.com/nodejs/node/commit/ddb7df76de)] - **test**: deflake test-http2-empty-frame-without-eof (Santiago Gimeno) [#45212](https://github.com/nodejs/node/pull/45212)
* \[[`02ebde39d3`](https://github.com/nodejs/node/commit/02ebde39d3)] - **test**: use common/tmpdir in watch-mode ipc test (Richard Lau) [#45211](https://github.com/nodejs/node/pull/45211)
* \[[`f9bc40a1fc`](https://github.com/nodejs/node/commit/f9bc40a1fc)] - **test**: use uv\_sleep() where possible (Santiago Gimeno) [#45124](https://github.com/nodejs/node/pull/45124)
* \[[`3c7ea23b8f`](https://github.com/nodejs/node/commit/3c7ea23b8f)] - **test**: fix typo in `test/parallel/test-fs-rm.js` (Tim Shilov) [#44882](https://github.com/nodejs/node/pull/44882)
* \[[`b39dcde056`](https://github.com/nodejs/node/commit/b39dcde056)] - **test**: remove a snapshot blob from test-inspect-address-in-use.js (Daeyeon Jeong) [#45132](https://github.com/nodejs/node/pull/45132)
* \[[`fabed9bdc8`](https://github.com/nodejs/node/commit/fabed9bdc8)] - **test**: add test for Module.\_stat (Darshan Sen) [#44713](https://github.com/nodejs/node/pull/44713)
* \[[`2b3b291c97`](https://github.com/nodejs/node/commit/2b3b291c97)] - **test**: watch mode inspect restart repeatedly (Moshe Atlow) [#45060](https://github.com/nodejs/node/pull/45060)
* \[[`17e86e4188`](https://github.com/nodejs/node/commit/17e86e4188)] - **test**: remove experimental-wasm-threads flag (Michaël Zasso) [#45074](https://github.com/nodejs/node/pull/45074)
* \[[`f0480d68e9`](https://github.com/nodejs/node/commit/f0480d68e9)] - **test**: remove unnecessary noop function args to `mustCall()` (Antoine du Hamel) [#45047](https://github.com/nodejs/node/pull/45047)
* \[[`82e6043118`](https://github.com/nodejs/node/commit/82e6043118)] - **test**: mark test-watch-mode\* as flaky on all platforms (Pierrick Bouvier) [#45049](https://github.com/nodejs/node/pull/45049)
* \[[`26a2ae2489`](https://github.com/nodejs/node/commit/26a2ae2489)] - **test**: wrap missing `common.mustCall` (Moshe Atlow) [#45064](https://github.com/nodejs/node/pull/45064)
* \[[`8662399cda`](https://github.com/nodejs/node/commit/8662399cda)] - **test**: remove mentions of `--experimental-async-stack-tagging-api` flag (Simon) [#45051](https://github.com/nodejs/node/pull/45051)
* \[[`71b8d506ed`](https://github.com/nodejs/node/commit/71b8d506ed)] - **test**: improve assertions in `test-repl-unsupported-option.js` (Juan José) [#44953](https://github.com/nodejs/node/pull/44953)
* \[[`dbc696d363`](https://github.com/nodejs/node/commit/dbc696d363)] - **test**: remove unnecessary noop function args to mustCall() (Rich Trott) [#45027](https://github.com/nodejs/node/pull/45027)
* \[[`c1ca19fb06`](https://github.com/nodejs/node/commit/c1ca19fb06)] - **test**: update WPT resources (Khaidi Chu) [#44948](https://github.com/nodejs/node/pull/44948)
* \[[`43677e5a34`](https://github.com/nodejs/node/commit/43677e5a34)] - **test**: skip test depending on `overlapped-checker` when not available (Antoine du Hamel) [#45015](https://github.com/nodejs/node/pull/45015)
* \[[`3519d74e87`](https://github.com/nodejs/node/commit/3519d74e87)] - **test**: improve test coverage for `os` package (Juan José) [#44959](https://github.com/nodejs/node/pull/44959)
* \[[`ea0cfc9a83`](https://github.com/nodejs/node/commit/ea0cfc9a83)] - **test**: add test to improve coverage in http2-compat-serverresponse (Cesar Mario Diaz) [#44970](https://github.com/nodejs/node/pull/44970)
* \[[`482578682c`](https://github.com/nodejs/node/commit/482578682c)] - **test**: improve test coverage in `test-child-process-spawn-argv0.js` (Juan José) [#44955](https://github.com/nodejs/node/pull/44955)
* \[[`a618dc3c3e`](https://github.com/nodejs/node/commit/a618dc3c3e)] - **test**: use CHECK instead of EXPECT where necessary (Tobias Nießen) [#44795](https://github.com/nodejs/node/pull/44795)
* \[[`c59d3b76e6`](https://github.com/nodejs/node/commit/c59d3b76e6)] - **test**: refactor promises to async/await (Madhuri) [#44980](https://github.com/nodejs/node/pull/44980)
* \[[`36c5927c60`](https://github.com/nodejs/node/commit/36c5927c60)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#45165](https://github.com/nodejs/node/pull/45165)
* \[[`6158d740f3`](https://github.com/nodejs/node/commit/6158d740f3)] - **test\_runner**: support function mocking (Colin Ihrig) [#45326](https://github.com/nodejs/node/pull/45326)
* \[[`920804dc46`](https://github.com/nodejs/node/commit/920804dc46)] - **test\_runner**: avoid swallowing of asynchronously thrown errors (MURAKAMI Masahiko) [#45264](https://github.com/nodejs/node/pull/45264)
* \[[`8e7f9de45e`](https://github.com/nodejs/node/commit/8e7f9de45e)] - **test\_runner**: fix afterEach not running on test failures (Jithil P Ponnan) [#45204](https://github.com/nodejs/node/pull/45204)
* \[[`0040030443`](https://github.com/nodejs/node/commit/0040030443)] - **test\_runner**: report tap subtest in order (Moshe Atlow) [#45220](https://github.com/nodejs/node/pull/45220)
* \[[`afa8291c7c`](https://github.com/nodejs/node/commit/afa8291c7c)] - **test\_runner**: call {before,after}Each() on suites (Colin Ihrig) [#45161](https://github.com/nodejs/node/pull/45161)
* \[[`ff174b0937`](https://github.com/nodejs/node/commit/ff174b0937)] - **test\_runner**: add extra fields in AssertionError YAML (Bryan English) [#44952](https://github.com/nodejs/node/pull/44952)
* \[[`bf868fdfab`](https://github.com/nodejs/node/commit/bf868fdfab)] - **(SEMVER-MINOR)** **tls**: add "ca" property to certificate object (Ben Noordhuis) [#44935](https://github.com/nodejs/node/pull/44935)
* \[[`e8075fd1f8`](https://github.com/nodejs/node/commit/e8075fd1f8)] - **tools**: add automation for updating acorn dependency (Facundo Tuesca) [#45357](https://github.com/nodejs/node/pull/45357)
* \[[`9aa305ff3e`](https://github.com/nodejs/node/commit/9aa305ff3e)] - **tools**: add documentation regarding our api tooling (Claudio Wunder) [#45270](https://github.com/nodejs/node/pull/45270)
* \[[`76cbc07f9b`](https://github.com/nodejs/node/commit/76cbc07f9b)] - **tools**: allow scripts to run from anywhere (Luigi Pinca) [#45361](https://github.com/nodejs/node/pull/45361)
* \[[`aa875a4d6a`](https://github.com/nodejs/node/commit/aa875a4d6a)] - **tools**: update eslint to 8.27.0 (Node.js GitHub Bot) [#45358](https://github.com/nodejs/node/pull/45358)
* \[[`4b71db13ae`](https://github.com/nodejs/node/commit/4b71db13ae)] - **tools**: update eslint to 8.26.0 (Node.js GitHub Bot) [#45243](https://github.com/nodejs/node/pull/45243)
* \[[`63267dfefb`](https://github.com/nodejs/node/commit/63267dfefb)] - **tools**: update lint-md-dependencies to rollup\@3.2.5 (Node.js GitHub Bot) [#45332](https://github.com/nodejs/node/pull/45332)
* \[[`e275859138`](https://github.com/nodejs/node/commit/e275859138)] - **tools**: fix stability index generation (Antoine du Hamel) [#45346](https://github.com/nodejs/node/pull/45346)
* \[[`97fe8bacb1`](https://github.com/nodejs/node/commit/97fe8bacb1)] - **tools**: increase macOS cores to 3 on GitHub CI (Rich Trott) [#45340](https://github.com/nodejs/node/pull/45340)
* \[[`eda4ae51ca`](https://github.com/nodejs/node/commit/eda4ae51ca)] - **tools**: add automation for updating base64 dependency (Facundo Tuesca) [#45300](https://github.com/nodejs/node/pull/45300)
* \[[`2ee052f794`](https://github.com/nodejs/node/commit/2ee052f794)] - **tools**: fix `request-ci-failed` comment (Antoine du Hamel) [#45291](https://github.com/nodejs/node/pull/45291)
* \[[`e118dd88fd`](https://github.com/nodejs/node/commit/e118dd88fd)] - **tools**: refactor dynamic strings creation in shell scripts (Antoine du Hamel) [#45240](https://github.com/nodejs/node/pull/45240)
* \[[`ba89cea683`](https://github.com/nodejs/node/commit/ba89cea683)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#45237](https://github.com/nodejs/node/pull/45237)
* \[[`786f086800`](https://github.com/nodejs/node/commit/786f086800)] - **tools**: use Python 3.11 in GitHub Actions workflows (Luigi Pinca) [#45191](https://github.com/nodejs/node/pull/45191)
* \[[`0738d14fa4`](https://github.com/nodejs/node/commit/0738d14fa4)] - **tools**: fix `request-ci-failed` comment (Antoine du Hamel) [#45218](https://github.com/nodejs/node/pull/45218)
* \[[`49be13ccd8`](https://github.com/nodejs/node/commit/49be13ccd8)] - **tools**: keep Emeriti lists case-insensitive alphabetic (Rich Trott) [#45159](https://github.com/nodejs/node/pull/45159)
* \[[`6e30d2231b`](https://github.com/nodejs/node/commit/6e30d2231b)] - **tools**: update actions/setup-python to v4 (Yagiz Nizipli) [#45178](https://github.com/nodejs/node/pull/45178)
* \[[`a4158692d7`](https://github.com/nodejs/node/commit/a4158692d7)] - **tools**: update V8 gypfiles for RISC-V (Andreas Schwab) [#45149](https://github.com/nodejs/node/pull/45149)
* \[[`c43bc2169f`](https://github.com/nodejs/node/commit/c43bc2169f)] - **tools**: fix `create-or-update-pull-request-action` hash on GHA (Antoine du Hamel) [#45166](https://github.com/nodejs/node/pull/45166)
* \[[`2ccc03ec32`](https://github.com/nodejs/node/commit/2ccc03ec32)] - **tools**: update gr2m/create-or-update-pull-request-action (Luigi Pinca) [#45022](https://github.com/nodejs/node/pull/45022)
* \[[`a70b27629f`](https://github.com/nodejs/node/commit/a70b27629f)] - **tools**: do not use the set-output command in workflows (Luigi Pinca) [#45024](https://github.com/nodejs/node/pull/45024)
* \[[`025e616662`](https://github.com/nodejs/node/commit/025e616662)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#45019](https://github.com/nodejs/node/pull/45019)
* \[[`732f9a78d3`](https://github.com/nodejs/node/commit/732f9a78d3)] - **trace\_events**: fix getCategories (theanarkh) [#45092](https://github.com/nodejs/node/pull/45092)
* \[[`1bc84ce52c`](https://github.com/nodejs/node/commit/1bc84ce52c)] - **url**: remove \t \n \r in url.parse() similar to WHATWG (Rich Trott) [#45116](https://github.com/nodejs/node/pull/45116)
* \[[`84e7388160`](https://github.com/nodejs/node/commit/84e7388160)] - **url**: improve port validation (Rich Trott) [#45012](https://github.com/nodejs/node/pull/45012)
* \[[`02cff4a3d3`](https://github.com/nodejs/node/commit/02cff4a3d3)] - **url**: improve url.parse() compliance with WHATWG URL (Rich Trott) [#45011](https://github.com/nodejs/node/pull/45011)
* \[[`89390a6be2`](https://github.com/nodejs/node/commit/89390a6be2)] - **util**: improve text-decoder performance (Yagiz Nizipli) [#45363](https://github.com/nodejs/node/pull/45363)
* \[[`0deed8daeb`](https://github.com/nodejs/node/commit/0deed8daeb)] - **util**: improve textdecoder decode performance (Yagiz Nizipli) [#45294](https://github.com/nodejs/node/pull/45294)
* \[[`d41f8ffc36`](https://github.com/nodejs/node/commit/d41f8ffc36)] - **(SEMVER-MINOR)** **util**: add MIME utilities (#21128) (Bradley Farias) [#21128](https://github.com/nodejs/node/pull/21128)

<a id="19.0.1"></a>

## 2022-11-04, Version 19.0.1 (Current), @RafaelGSS

This is a security release.

### Notable changes

The following CVEs are fixed in this release:

* **[CVE-2022-3602](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-3602)**: X.509 Email Address 4-byte Buffer Overflow (High)
* **[CVE-2022-3786](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-3786)**: X.509 Email Address Variable Length Buffer Overflow (High)
* **[CVE-2022-43548](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2022-43548)**: DNS rebinding in --inspect via invalid octal IP address (Medium)

More detailed information on each of the vulnerabilities can be found in [November 2022 Security Releases](https://nodejs.org/en/blog/vulnerability/november-2022-security-releases/) blog post.

### Commits

* \[[`e58e8d70a8`](https://github.com/nodejs/node/commit/e58e8d70a8)] - **deps**: update archs files for quictls/openssl-3.0.7+quic (RafaelGSS) [#45286](https://github.com/nodejs/node/pull/45286)
* \[[`85f4548d57`](https://github.com/nodejs/node/commit/85f4548d57)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.7+quic (RafaelGSS) [#45286](https://github.com/nodejs/node/pull/45286)
* \[[`43403f56f7`](https://github.com/nodejs/node/commit/43403f56f7)] - **inspector**: harden IP address validation again (Tobias Nießen) [nodejs-private/node-private#354](https://github.com/nodejs-private/node-private/pull/354)

<a id="19.0.0"></a>

## 2022-10-18, Version 19.0.0 (Current), @RafaelGSS and @ruyadorno

Node.js 19 is here! Highlights include the update of the V8 JavaScript engine to 10.7, HTTP(s)/1.1 KeepAlive enabled by default, and ESM Resolution adjusts.

Node.js 19 will replace Node.js 18 as our ‘Current’ release line when Node.js 18 enters long-term support (LTS) later this month.
As per the release schedule, Node.js 19 will be ‘Current' release for the next 6 months, until April 2023.

### Notable Changes

#### Deprecations and Removals

* \[[`7dd2f41c73`](https://github.com/nodejs/node/commit/7dd2f41c73)] - **(SEMVER-MAJOR)** **module**: runtime deprecate exports double slash maps (Guy Bedford) [#44495](https://github.com/nodejs/node/pull/44495)
* \[[`ada2d053ae`](https://github.com/nodejs/node/commit/ada2d053ae)] - **(SEMVER-MAJOR)** **process**: runtime deprecate coercion to integer in `process.exit()` (Daeyeon Jeong) [#44711](https://github.com/nodejs/node/pull/44711)

#### HTTP(S)/1.1 KeepAlive by default

Starting with this release, Node.js sets `keepAlive` to true by default. This means that any outgoing HTTP(s) connection will automatically use HTTP 1.1 Keep-Alive. The default waiting window is 5 seconds.
Enable keep-alive will deliver better throughput as connections are reused by default.

Additionally the agent is now able to parse the response `Keep-Alive` which the servers might send. This header instructs the client on how much to stay connected.
On the other side, the Node.js HTTP server will now automatically disconnect idle clients (which are using HTTP Keep-Alive to reuse the connection) when `close()` is invoked).

Node.js HTTP(S)/1.1 requests may experience a better throughput/performance by default.

Contributed by Paolo Insogna in [#43522](https://github.com/nodejs/node/pull/43522)

#### DTrace/SystemTap/ETW Support were removed

The main reason is the lack of resources from Node.js team. The complexity to keep the support up-to-date has been proved not worth it without a clear plan to support those tools. Hence, [an issue was raised](https://github.com/nodejs/node/issues/44550) in the Node.js repository to assess a better support, for `DTrace` in specific.

Contributed by Ben Noordhuis in [#43651](https://github.com/nodejs/node/pull/43651) and [#43652](https://github.com/nodejs/node/pull/43652)

#### V8 10.7

The V8 engine is updated to version 10.7, which is part of Chromium 107.
This version include a new feature to the JavaScript API: `Intl.NumberFormat`.

`Intl.NumberFormat` v3 API is a new [TC39 ECMA402 stage 3 proposal](https://github.com/tc39/proposal-intl-numberformat-v3)
extend the pre-existing `Intl.NumberFormat`.

The V8 update was a contribution by Michaël Zasso in [#44741](https://github.com/nodejs/node/pull/44741).

#### llhttp 8.1.0

llhttp has been updated to version 8.1.0.Collectively, this version brings many update to the llhttp API, introducing new callbacks and allow all callback to be pausable.

Contributed by Paolo Insogna in [#44967](https://github.com/nodejs/node/pull/44967)

#### Other Notable Changes

* \[[`46a3afb579`](https://github.com/nodejs/node/commit/46a3afb579)] - **doc**: graduate webcrypto to stable (Filip Skokan) [#44897](https://github.com/nodejs/node/pull/44897)
* \[[`f594cc85b7`](https://github.com/nodejs/node/commit/f594cc85b7)] - **esm**: remove specifier resolution flag (Geoffrey Booth) [#44859](https://github.com/nodejs/node/pull/44859)

### Semver-Major Commits

* \[[`53f73d1cfe`](https://github.com/nodejs/node/commit/53f73d1cfe)] - **(SEMVER-MAJOR)** **build**: enable V8's trap handler on Windows (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`06aaf8a1c4`](https://github.com/nodejs/node/commit/06aaf8a1c4)] - **(SEMVER-MAJOR)** **build**: reset embedder string to "-node.0" (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`aa3a572e6b`](https://github.com/nodejs/node/commit/aa3a572e6b)] - **(SEMVER-MAJOR)** **build**: remove dtrace & etw support (Ben Noordhuis) [#43652](https://github.com/nodejs/node/pull/43652)
* \[[`38f1e2793c`](https://github.com/nodejs/node/commit/38f1e2793c)] - **(SEMVER-MAJOR)** **build**: remove systemtap support (Ben Noordhuis) [#43651](https://github.com/nodejs/node/pull/43651)
* \[[`2849283c4c`](https://github.com/nodejs/node/commit/2849283c4c)] - **(SEMVER-MAJOR)** **crypto**: remove non-standard `webcrypto.Crypto.prototype.CryptoKey` (Antoine du Hamel) [#42083](https://github.com/nodejs/node/pull/42083)
* \[[`a1653ac715`](https://github.com/nodejs/node/commit/a1653ac715)] - **(SEMVER-MAJOR)** **crypto**: do not allow to call setFips from the worker thread (Sergey Petushkov) [#43624](https://github.com/nodejs/node/pull/43624)
* \[[`fd36a8dadb`](https://github.com/nodejs/node/commit/fd36a8dadb)] - **(SEMVER-MAJOR)** **deps**: update llhttp to 8.1.0 (Paolo Insogna) [#44967](https://github.com/nodejs/node/pull/44967)
* \[[`89ecdddaab`](https://github.com/nodejs/node/commit/89ecdddaab)] - **(SEMVER-MAJOR)** **deps**: bump minimum ICU version to 71 (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`66fe446efd`](https://github.com/nodejs/node/commit/66fe446efd)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 0cccb6f27d78 (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`88ed027d57`](https://github.com/nodejs/node/commit/88ed027d57)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 7ddb8399f9f1 (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`26c651c34e`](https://github.com/nodejs/node/commit/26c651c34e)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 1b3a4f0c34a1 (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`c8ff2dfd11`](https://github.com/nodejs/node/commit/c8ff2dfd11)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick b161a0823165 (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`7a8fa2d517`](https://github.com/nodejs/node/commit/7a8fa2d517)] - **(SEMVER-MAJOR)** **deps**: fix V8 build on Windows with MSVC (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`83b0aaa800`](https://github.com/nodejs/node/commit/83b0aaa800)] - **(SEMVER-MAJOR)** **deps**: fix V8 build on SmartOS (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`7a952e8ea5`](https://github.com/nodejs/node/commit/7a952e8ea5)] - **(SEMVER-MAJOR)** **deps**: silence irrelevant V8 warning (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`6bd756d7c6`](https://github.com/nodejs/node/commit/6bd756d7c6)] - **(SEMVER-MAJOR)** **deps**: update V8 to 10.7.193.13 (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`03fb789fb9`](https://github.com/nodejs/node/commit/03fb789fb9)] - **(SEMVER-MAJOR)** **events**: add null check for the signal of EventTarget (Masashi Hirano) [#43153](https://github.com/nodejs/node/pull/43153)
* \[[`a4fa526ddc`](https://github.com/nodejs/node/commit/a4fa526ddc)] - **(SEMVER-MAJOR)** **fs**: add directory autodetection to fsPromises.symlink() (Livia Medeiros) [#42894](https://github.com/nodejs/node/pull/42894)
* \[[`bb4891d8d4`](https://github.com/nodejs/node/commit/bb4891d8d4)] - **(SEMVER-MAJOR)** **fs**: add validateBuffer to improve error (Hirotaka Tagawa / wafuwafu13) [#44769](https://github.com/nodejs/node/pull/44769)
* \[[`950a4411fa`](https://github.com/nodejs/node/commit/950a4411fa)] - **(SEMVER-MAJOR)** **fs**: remove coercion to string in writing methods (Livia Medeiros) [#42796](https://github.com/nodejs/node/pull/42796)
* \[[`41a6d82968`](https://github.com/nodejs/node/commit/41a6d82968)] - **(SEMVER-MAJOR)** **fs**: harden fs.readSync(buffer, options) typecheck (LiviaMedeiros) [#42772](https://github.com/nodejs/node/pull/42772)
* \[[`2275faac2b`](https://github.com/nodejs/node/commit/2275faac2b)] - **(SEMVER-MAJOR)** **fs**: harden fs.read(params, callback) typecheck (LiviaMedeiros) [#42772](https://github.com/nodejs/node/pull/42772)
* \[[`29953a0b88`](https://github.com/nodejs/node/commit/29953a0b88)] - **(SEMVER-MAJOR)** **fs**: harden filehandle.read(params) typecheck (LiviaMedeiros) [#42772](https://github.com/nodejs/node/pull/42772)
* \[[`4267b92604`](https://github.com/nodejs/node/commit/4267b92604)] - **(SEMVER-MAJOR)** **http**: use Keep-Alive by default in global agents (Paolo Insogna) [#43522](https://github.com/nodejs/node/pull/43522)
* \[[`0324529e0f`](https://github.com/nodejs/node/commit/0324529e0f)] - **(SEMVER-MAJOR)** **inspector**: introduce inspector/promises API (Erick Wendel) [#44250](https://github.com/nodejs/node/pull/44250)
* \[[`80270994d6`](https://github.com/nodejs/node/commit/80270994d6)] - **(SEMVER-MAJOR)** **lib**: enable global CustomEvent by default (Daeyeon Jeong) [#44860](https://github.com/nodejs/node/pull/44860)
* \[[`f529f73bd7`](https://github.com/nodejs/node/commit/f529f73bd7)] - **(SEMVER-MAJOR)** **lib**: brand check event handler property receivers (Chengzhong Wu) [#44483](https://github.com/nodejs/node/pull/44483)
* \[[`6de2673a9f`](https://github.com/nodejs/node/commit/6de2673a9f)] - **(SEMVER-MAJOR)** **lib**: enable global WebCrypto by default (Antoine du Hamel) [#42083](https://github.com/nodejs/node/pull/42083)
* \[[`73ba8830d5`](https://github.com/nodejs/node/commit/73ba8830d5)] - **(SEMVER-MAJOR)** **lib**: use private field in AbortController (Joyee Cheung) [#43820](https://github.com/nodejs/node/pull/43820)
* \[[`7dd2f41c73`](https://github.com/nodejs/node/commit/7dd2f41c73)] - **(SEMVER-MAJOR)** **module**: runtime deprecate exports double slash maps (Guy Bedford) [#44495](https://github.com/nodejs/node/pull/44495)
* \[[`22c39b1ddd`](https://github.com/nodejs/node/commit/22c39b1ddd)] - **(SEMVER-MAJOR)** **path**: the dot will be added(path.format) if it is not specified in `ext` (theanarkh) [#44349](https://github.com/nodejs/node/pull/44349)
* \[[`587367d107`](https://github.com/nodejs/node/commit/587367d107)] - **(SEMVER-MAJOR)** **perf\_hooks**: expose webperf global scope interfaces (Chengzhong Wu) [#44483](https://github.com/nodejs/node/pull/44483)
* \[[`364c0e196c`](https://github.com/nodejs/node/commit/364c0e196c)] - **(SEMVER-MAJOR)** **perf\_hooks**: fix webperf idlharness (Chengzhong Wu) [#44483](https://github.com/nodejs/node/pull/44483)
* \[[`ada2d053ae`](https://github.com/nodejs/node/commit/ada2d053ae)] - **(SEMVER-MAJOR)** **process**: runtime deprecate coercion to integer in `process.exit()` (Daeyeon Jeong) [#44711](https://github.com/nodejs/node/pull/44711)
* \[[`e0ab8dd637`](https://github.com/nodejs/node/commit/e0ab8dd637)] - **(SEMVER-MAJOR)** **process**: make process.config read only (Sergey Petushkov) [#43627](https://github.com/nodejs/node/pull/43627)
* \[[`481a959adb`](https://github.com/nodejs/node/commit/481a959adb)] - **(SEMVER-MAJOR)** **readline**: remove `question` method from `InterfaceConstructor` (Antoine du Hamel) [#44606](https://github.com/nodejs/node/pull/44606)
* \[[`c9602ce212`](https://github.com/nodejs/node/commit/c9602ce212)] - **(SEMVER-MAJOR)** **src**: use new v8::OOMErrorCallback API (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`19a70c11e4`](https://github.com/nodejs/node/commit/19a70c11e4)] - **(SEMVER-MAJOR)** **src**: override CreateJob instead of PostJob (Clemens Backes) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`fd52c62bee`](https://github.com/nodejs/node/commit/fd52c62bee)] - **(SEMVER-MAJOR)** **src**: use V8\_ENABLE\_SANDBOX macro (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`c10988db44`](https://github.com/nodejs/node/commit/c10988db44)] - **(SEMVER-MAJOR)** **src**: use non-deprecated V8 inspector API (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`3efe901dd6`](https://github.com/nodejs/node/commit/3efe901dd6)] - **(SEMVER-MAJOR)** **src**: update NODE\_MODULE\_VERSION to 111 (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`77e585657f`](https://github.com/nodejs/node/commit/77e585657f)] - **(SEMVER-MAJOR)** **src**: turn embedder api overload into default argument (Alena Khineika) [#43629](https://github.com/nodejs/node/pull/43629)
* \[[`dabda03ea9`](https://github.com/nodejs/node/commit/dabda03ea9)] - **(SEMVER-MAJOR)** **src**: per-environment time origin value (Chengzhong Wu) [#43781](https://github.com/nodejs/node/pull/43781)
* \[[`2e49b99cc2`](https://github.com/nodejs/node/commit/2e49b99cc2)] - **(SEMVER-MAJOR)** **src,test**: disable freezing V8 flags on initialization (Clemens Backes) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`2b32985c62`](https://github.com/nodejs/node/commit/2b32985c62)] - **(SEMVER-MAJOR)** **stream**: use null for the error argument (Luigi Pinca) [#44312](https://github.com/nodejs/node/pull/44312)
* \[[`36805e8524`](https://github.com/nodejs/node/commit/36805e8524)] - **(SEMVER-MAJOR)** **test**: adapt test-repl for V8 update (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`96ef25793d`](https://github.com/nodejs/node/commit/96ef25793d)] - **(SEMVER-MAJOR)** **test**: adapt test-repl-pretty-\*stack to V8 changes (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`71c193e581`](https://github.com/nodejs/node/commit/71c193e581)] - **(SEMVER-MAJOR)** **test**: adapt to new JSON SyntaxError messages (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`b5f1564880`](https://github.com/nodejs/node/commit/b5f1564880)] - **(SEMVER-MAJOR)** **test**: rename always-opt flag to always-turbofan (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`1acf0339dd`](https://github.com/nodejs/node/commit/1acf0339dd)] - **(SEMVER-MAJOR)** **test**: fix test-hash-seed for new V8 versions (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)
* \[[`57ff476c33`](https://github.com/nodejs/node/commit/57ff476c33)] - **(SEMVER-MAJOR)** **test**: remove duplicate test (Luigi Pinca) [#44051](https://github.com/nodejs/node/pull/44051)
* \[[`77def91bf9`](https://github.com/nodejs/node/commit/77def91bf9)] - **(SEMVER-MAJOR)** **tls,http2**: send fatal alert on ALPN mismatch (Tobias Nießen) [#44031](https://github.com/nodejs/node/pull/44031)
* \[[`4860ad99b9`](https://github.com/nodejs/node/commit/4860ad99b9)] - **(SEMVER-MAJOR)** **tools**: update V8 gypfiles for 10.7 (Michaël Zasso) [#44741](https://github.com/nodejs/node/pull/44741)

### Semver-Minor Commits

* \[[`af0921d877`](https://github.com/nodejs/node/commit/af0921d877)] - **(SEMVER-MINOR)** **esm**: add `--import` flag (Moshe Atlow) [#43942](https://github.com/nodejs/node/pull/43942)
* \[[`0633e9a0b5`](https://github.com/nodejs/node/commit/0633e9a0b5)] - **(SEMVER-MINOR)** **lib**: add diagnostics channel for process and worker (theanarkh) [#44045](https://github.com/nodejs/node/pull/44045)
* \[[`ca5be26b31`](https://github.com/nodejs/node/commit/ca5be26b31)] - **(SEMVER-MINOR)** **src**: add support for externally shared js builtins (Michael Dawson) [#44376](https://github.com/nodejs/node/pull/44376)
* \[[`e86a638305`](https://github.com/nodejs/node/commit/e86a638305)] - **(SEMVER-MINOR)** **src**: add initial shadow realm support (Chengzhong Wu) [#42869](https://github.com/nodejs/node/pull/42869)
* \[[`71ca6d7d6a`](https://github.com/nodejs/node/commit/71ca6d7d6a)] - **(SEMVER-MINOR)** **util**: add `maxArrayLength` option to Set and Map (Kohei Ueno) [#43576](https://github.com/nodejs/node/pull/43576)

### Semver-Patch Commits

* \[[`78508028e3`](https://github.com/nodejs/node/commit/78508028e3)] - **bootstrap**: generate bootstrapper arguments in BuiltinLoader (Joyee Cheung) [#44488](https://github.com/nodejs/node/pull/44488)
* \[[`5291096ca2`](https://github.com/nodejs/node/commit/5291096ca2)] - **bootstrap**: check more metadata when loading the snapshot (Joyee Cheung) [#44132](https://github.com/nodejs/node/pull/44132)
* \[[`d0f73d383d`](https://github.com/nodejs/node/commit/d0f73d383d)] - **build**: go faster, drop -fno-omit-frame-pointer (Ben Noordhuis) [#44452](https://github.com/nodejs/node/pull/44452)
* \[[`214354fc9f`](https://github.com/nodejs/node/commit/214354fc9f)] - **crypto**: fix webcrypto HMAC "get key length" in deriveKey and generateKey (Filip Skokan) [#44917](https://github.com/nodejs/node/pull/44917)
* \[[`40a0757b21`](https://github.com/nodejs/node/commit/40a0757b21)] - **crypto**: remove webcrypto HKDF and PBKDF2 default-applied lengths (Filip Skokan) [#44945](https://github.com/nodejs/node/pull/44945)
* \[[`eeec3eb16a`](https://github.com/nodejs/node/commit/eeec3eb16a)] - **crypto**: simplify webcrypto ECDH deriveBits (Filip Skokan) [#44946](https://github.com/nodejs/node/pull/44946)
* \[[`0be1c57281`](https://github.com/nodejs/node/commit/0be1c57281)] - **deps**: V8: cherry-pick c2792e58035f (Jiawen Geng) [#44961](https://github.com/nodejs/node/pull/44961)
* \[[`488474618c`](https://github.com/nodejs/node/commit/488474618c)] - **deps**: V8: cherry-pick c3dffe6e2bda (Michaël Zasso) [#44958](https://github.com/nodejs/node/pull/44958)
* \[[`34ba631a0b`](https://github.com/nodejs/node/commit/34ba631a0b)] - **deps**: V8: cherry-pick e7f0f26f5ef3 (Michaël Zasso) [#44958](https://github.com/nodejs/node/pull/44958)
* \[[`690a837f4f`](https://github.com/nodejs/node/commit/690a837f4f)] - **deps**: V8: cherry-pick 3d59a3c2c164 (Michaël Zasso) [#44958](https://github.com/nodejs/node/pull/44958)
* \[[`bab8b3aad6`](https://github.com/nodejs/node/commit/bab8b3aad6)] - **deps**: V8: cherry-pick 8b8703953616 (Michaël Zasso) [#44958](https://github.com/nodejs/node/pull/44958)
* \[[`37e5152245`](https://github.com/nodejs/node/commit/37e5152245)] - **doc**: add notable changes to latest v18.x release changelog (Danielle Adams) [#44996](https://github.com/nodejs/node/pull/44996)
* \[[`19a909902a`](https://github.com/nodejs/node/commit/19a909902a)] - **doc**: deprecate url.parse() (Rich Trott) [#44919](https://github.com/nodejs/node/pull/44919)
* \[[`6686d9000b`](https://github.com/nodejs/node/commit/6686d9000b)] - **doc**: fix backticks in fs API docs (Livia Medeiros) [#44962](https://github.com/nodejs/node/pull/44962)
* \[[`46a3afb579`](https://github.com/nodejs/node/commit/46a3afb579)] - **doc**: graduate webcrypto to stable (Filip Skokan) [#44897](https://github.com/nodejs/node/pull/44897)
* \[[`6e3c55cc35`](https://github.com/nodejs/node/commit/6e3c55cc35)] - **doc**: fix v16.17.1 security release changelog (Ruy Adorno) [#44759](https://github.com/nodejs/node/pull/44759)
* \[[`77cb88b91c`](https://github.com/nodejs/node/commit/77cb88b91c)] - **doc**: mark `--import` as experimental (Moshe Atlow) [#44067](https://github.com/nodejs/node/pull/44067)
* \[[`46dcfb3c7b`](https://github.com/nodejs/node/commit/46dcfb3c7b)] - **doc,crypto**: update webcrypto docs for global access (Filip Skokan) [#44723](https://github.com/nodejs/node/pull/44723)
* \[[`f594cc85b7`](https://github.com/nodejs/node/commit/f594cc85b7)] - **esm**: remove specifier resolution flag (Geoffrey Booth) [#44859](https://github.com/nodejs/node/pull/44859)
* \[[`3c040348fe`](https://github.com/nodejs/node/commit/3c040348fe)] - _**Revert**_ "**esm**: convert `resolve` hook to synchronous" (Jacob Smith) [#43526](https://github.com/nodejs/node/pull/43526)
* \[[`90b634a5a5`](https://github.com/nodejs/node/commit/90b634a5a5)] - **esm**: convert `resolve` hook to synchronous (Jacob Smith) [#43363](https://github.com/nodejs/node/pull/43363)
* \[[`7c06eab1dc`](https://github.com/nodejs/node/commit/7c06eab1dc)] - _**Revert**_ "**http**: do not leak error listeners" (Luigi Pinca) [#44921](https://github.com/nodejs/node/pull/44921)
* \[[`464d1c1558`](https://github.com/nodejs/node/commit/464d1c1558)] - **lib**: reset `RegExp` statics before running user code (Antoine du Hamel) [#43741](https://github.com/nodejs/node/pull/43741)
* \[[`15f10515e3`](https://github.com/nodejs/node/commit/15f10515e3)] - **module**: fix segment deprecation for imports field (Guy Bedford) [#44883](https://github.com/nodejs/node/pull/44883)
* \[[`7cdf745fdd`](https://github.com/nodejs/node/commit/7cdf745fdd)] - **perf\_hooks**: convert maxSize to IDL value in setResourceTimingBufferSize (Chengzhong Wu) [#44902](https://github.com/nodejs/node/pull/44902)
* \[[`be525d7d04`](https://github.com/nodejs/node/commit/be525d7d04)] - **src**: consolidate exit codes in the code base (Joyee Cheung) [#44746](https://github.com/nodejs/node/pull/44746)
* \[[`d5ce285c8b`](https://github.com/nodejs/node/commit/d5ce285c8b)] - **src**: refactor BaseObject methods (Joyee Cheung) [#44796](https://github.com/nodejs/node/pull/44796)
* \[[`717465433c`](https://github.com/nodejs/node/commit/717465433c)] - **src**: create BaseObject with node::Realm (Chengzhong Wu) [#44348](https://github.com/nodejs/node/pull/44348)
* \[[`45f2258f74`](https://github.com/nodejs/node/commit/45f2258f74)] - **src**: restore IS\_RELEASE to 0 (Bryan English) [#44758](https://github.com/nodejs/node/pull/44758)
* \[[`1f54fc25cb`](https://github.com/nodejs/node/commit/1f54fc25cb)] - **src**: use automatic memory mgmt in SecretKeyGen (Tobias Nießen) [#44479](https://github.com/nodejs/node/pull/44479)
* \[[`7371d335ac`](https://github.com/nodejs/node/commit/7371d335ac)] - **src**: use V8 entropy source if RAND\_bytes() != 1 (Tobias Nießen) [#44493](https://github.com/nodejs/node/pull/44493)
* \[[`81d9cdb8cd`](https://github.com/nodejs/node/commit/81d9cdb8cd)] - **src**: introduce node::Realm (Chengzhong Wu) [#44179](https://github.com/nodejs/node/pull/44179)
* \[[`ad41c919df`](https://github.com/nodejs/node/commit/ad41c919df)] - **src**: remove v8abbr.h (Tobias Nießen) [#44402](https://github.com/nodejs/node/pull/44402)
* \[[`fddc701d3c`](https://github.com/nodejs/node/commit/fddc701d3c)] - **src**: support diagnostics channel in the snapshot (Joyee Cheung) [#44193](https://github.com/nodejs/node/pull/44193)
* \[[`d70aab663c`](https://github.com/nodejs/node/commit/d70aab663c)] - **src**: support WeakReference in snapshot (Joyee Cheung) [#44193](https://github.com/nodejs/node/pull/44193)
* \[[`4ca398a617`](https://github.com/nodejs/node/commit/4ca398a617)] - **src**: iterate over base objects to prepare for snapshot (Joyee Cheung) [#44192](https://github.com/nodejs/node/pull/44192)
* \[[`8b0e5b19bd`](https://github.com/nodejs/node/commit/8b0e5b19bd)] - **src**: fix cppgc incompatibility in v8 (Shelley Vohr) [#43521](https://github.com/nodejs/node/pull/43521)
* \[[`3fdf6cfad9`](https://github.com/nodejs/node/commit/3fdf6cfad9)] - **stream**: fix `size` function returned from QueuingStrategies (Daeyeon Jeong) [#44867](https://github.com/nodejs/node/pull/44867)
* \[[`331088f4a4`](https://github.com/nodejs/node/commit/331088f4a4)] - _**Revert**_ "**tools**: refactor `tools/license2rtf` to ESM" (Richard Lau) [#43214](https://github.com/nodejs/node/pull/43214)
* \[[`30cb1bf8b8`](https://github.com/nodejs/node/commit/30cb1bf8b8)] - **tools**: refactor `tools/license2rtf` to ESM (Feng Yu) [#43101](https://github.com/nodejs/node/pull/43101)
* \[[`a3ff4bfc66`](https://github.com/nodejs/node/commit/a3ff4bfc66)] - **url**: revert "validate ipv4 part length" (Antoine du Hamel) [#42940](https://github.com/nodejs/node/pull/42940)
* \[[`87d0d7a069`](https://github.com/nodejs/node/commit/87d0d7a069)] - **url**: validate ipv4 part length (Yagiz Nizipli) [#42915](https://github.com/nodejs/node/pull/42915)
* \[[`5b1bcf82f1`](https://github.com/nodejs/node/commit/5b1bcf82f1)] - **vm**: make ContextifyContext a BaseObject (Joyee Cheung) [#44796](https://github.com/nodejs/node/pull/44796)

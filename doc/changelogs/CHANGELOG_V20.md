# Node.js 20 ChangeLog

<!--lint disable maximum-line-length no-literal-urls prohibited-strings-->

<table>
<tr>
<th>LTS 'Iron'</th>
<th>Current</th>
</tr>
<tr>
<td>
<a href="#20.18.2">20.18.2</a><br/>
<a href="#20.18.1">20.18.1</a><br/>
<a href="#20.18.0">20.18.0</a><br/>
<a href="#20.17.0">20.17.0</a><br/>
<a href="#20.16.0">20.16.0</a><br/>
<a href="#20.15.1">20.15.1</a><br/>
<a href="#20.15.0">20.15.0</a><br/>
<a href="#20.14.0">20.14.0</a><br/>
<a href="#20.13.1">20.13.1</a><br/>
<a href="#20.13.0">20.13.0</a><br/>
<a href="#20.12.2">20.12.2</a><br/>
<a href="#20.12.1">20.12.1</a><br/>
<a href="#20.12.0">20.12.0</a><br/>
<a href="#20.11.1">20.11.1</a><br/>
<a href="#20.11.0">20.11.0</a><br/>
<a href="#20.10.0">20.10.0</a><br/>
<a href="#20.9.0">20.9.0</a><br/>
</td>
<td>
<a href="#20.8.1">20.8.1</a><br/>
<a href="#20.8.0">20.8.0</a><br/>
<a href="#20.7.0">20.7.0</a><br/>
<a href="#20.6.1">20.6.1</a><br/>
<a href="#20.6.0">20.6.0</a><br/>
<a href="#20.5.1">20.5.1</a><br/>
<a href="#20.5.0">20.5.0</a><br/>
<a href="#20.4.0">20.4.0</a><br/>
<a href="#20.3.1">20.3.1</a><br/>
<a href="#20.3.0">20.3.0</a><br/>
<a href="#20.2.0">20.2.0</a><br/>
<a href="#20.1.0">20.1.0</a><br/>
<a href="#20.0.0">20.0.0</a><br/>
</td>
</tr>
</table>

* Other Versions
  * [23.x](CHANGELOG_V23.md)
  * [22.x](CHANGELOG_V22.md)
  * [21.x](CHANGELOG_V21.md)
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
  * [4.x](CHANGELOG_V4.md)
  * [0.12.x](CHANGELOG_V012.md)
  * [0.10.x](CHANGELOG_V010.md)
  * [io.js](CHANGELOG_IOJS.md)
  * [Archive](CHANGELOG_ARCHIVE.md)

<a id="20.18.2"></a>

## 2025-01-21, Version 20.18.2 'Iron' (LTS), @RafaelGSS

This is a security release.

### Notable Changes

* CVE-2025-23083 - throw on InternalWorker use when permission model is enabled (High)
* CVE-2025-23085 - src: fix HTTP2 mem leak on premature close and ERR\_PROTO (Medium)
* CVE-2025-23084 - path: fix path traversal in normalize() on Windows (Medium)

Dependency update:

* CVE-2025-22150 - Use of Insufficiently Random Values in undici fetch() (Medium)

### Commits

* \[[`df8b9f2c3e`](https://github.com/nodejs/node/commit/df8b9f2c3e)] - **(CVE-2025-22150)** **deps**: update undici to v6.21.1 (Matteo Collina) [nodejs-private/node-private#663](https://github.com/nodejs-private/node-private/pull/663)
* \[[`42d5821873`](https://github.com/nodejs/node/commit/42d5821873)] - **(CVE-2025-23084)** **path**: fix path traversal in normalize() on Windows (Tobias Nießen) [nodejs-private/node-private#555](https://github.com/nodejs-private/node-private/pull/555)
* \[[`8187a4b9bb`](https://github.com/nodejs/node/commit/8187a4b9bb)] - **src**: fix HTTP2 mem leak on premature close and ERR\_PROTO (RafaelGSS)
* \[[`389f239a28`](https://github.com/nodejs/node/commit/389f239a28)] - **(CVE-2025-23083)** **src,loader,permission**: throw on InternalWorker use (RafaelGSS) [nodejs-private/node-private#652](https://github.com/nodejs-private/node-private/pull/652)

<a id="20.18.1"></a>

## 2024-11-20, Version 20.18.1 'Iron' (LTS), @marco-ippolito

### Notable Changes

* \[[`7a8992b2d6`](https://github.com/nodejs/node/commit/7a8992b2d6)] - **doc**: add abmusse to collaborators (Abdirahim Musse) [#55086](https://github.com/nodejs/node/pull/55086)

### Commits

* \[[`085c3441fe`](https://github.com/nodejs/node/commit/085c3441fe)] - **assert**: show the diff when deep comparing data with a custom message (Giovanni) [#54759](https://github.com/nodejs/node/pull/54759)
* \[[`01f0b0e7b4`](https://github.com/nodejs/node/commit/01f0b0e7b4)] - **benchmark**: adjust config for deepEqual object (Rafael Gonzaga) [#55254](https://github.com/nodejs/node/pull/55254)
* \[[`a45537269b`](https://github.com/nodejs/node/commit/a45537269b)] - **benchmark**: rewrite detect-esm-syntax benchmark (Joyee Cheung) [#55238](https://github.com/nodejs/node/pull/55238)
* \[[`1a0d8ef64f`](https://github.com/nodejs/node/commit/1a0d8ef64f)] - **benchmark**: add no-warnings to process.has bench (Rafael Gonzaga) [#55159](https://github.com/nodejs/node/pull/55159)
* \[[`2be5d611ce`](https://github.com/nodejs/node/commit/2be5d611ce)] - **benchmark**: create benchmark for typescript (Marco Ippolito) [#54904](https://github.com/nodejs/node/pull/54904)
* \[[`a2aa4fa477`](https://github.com/nodejs/node/commit/a2aa4fa477)] - **benchmark**: include ascii to fs/readfile (Rafael Gonzaga) [#54988](https://github.com/nodejs/node/pull/54988)
* \[[`09849105fe`](https://github.com/nodejs/node/commit/09849105fe)] - **benchmark**: add dotenv benchmark (Aviv Keller) [#54278](https://github.com/nodejs/node/pull/54278)
* \[[`6b3c24dbad`](https://github.com/nodejs/node/commit/6b3c24dbad)] - **buffer**: fix out of range for toString (Jason Zhang) [#54553](https://github.com/nodejs/node/pull/54553)
* \[[`f25a5b6dc4`](https://github.com/nodejs/node/commit/f25a5b6dc4)] - **build**: use rclone instead of aws CLI (Michaël Zasso) [#55617](https://github.com/nodejs/node/pull/55617)
* \[[`0bbeb605de`](https://github.com/nodejs/node/commit/0bbeb605de)] - **build**: fix notify-on-review-wanted action (Rafael Gonzaga) [#55304](https://github.com/nodejs/node/pull/55304)
* \[[`5b35836732`](https://github.com/nodejs/node/commit/5b35836732)] - **build**: include `.nycrc` in coverage workflows (Wuli Zuo) [#55210](https://github.com/nodejs/node/pull/55210)
* \[[`f38d1e90e0`](https://github.com/nodejs/node/commit/f38d1e90e0)] - **build**: notify via slack when review-wanted (Rafael Gonzaga) [#55102](https://github.com/nodejs/node/pull/55102)
* \[[`0b985ec4bb`](https://github.com/nodejs/node/commit/0b985ec4bb)] - **build**: remove -v flag to reduce noise (iwuliz) [#55025](https://github.com/nodejs/node/pull/55025)
* \[[`09f75b27a1`](https://github.com/nodejs/node/commit/09f75b27a1)] - **build**: display free disk space after build in the test-macOS workflow (iwuliz) [#55025](https://github.com/nodejs/node/pull/55025)
* \[[`f25760c4a2`](https://github.com/nodejs/node/commit/f25760c4a2)] - **build**: add the option to generate compile\_commands.json in vcbuild.bat (Segev Finer) [#52279](https://github.com/nodejs/node/pull/52279)
* \[[`746e78c4f3`](https://github.com/nodejs/node/commit/746e78c4f3)] - _**Revert**_ "**build**: upgrade clang-format to v18" (Chengzhong Wu) [#54994](https://github.com/nodejs/node/pull/54994)
* \[[`67834ee646`](https://github.com/nodejs/node/commit/67834ee646)] - **build**: print `Running XYZ linter...` for py and yml (Aviv Keller) [#54386](https://github.com/nodejs/node/pull/54386)
* \[[`ae34e276a2`](https://github.com/nodejs/node/commit/ae34e276a2)] - **build**: pin doc workflow to Node.js 20 (Richard Lau) [#55755](https://github.com/nodejs/node/pull/55755)
* \[[`d0e871a706`](https://github.com/nodejs/node/commit/d0e871a706)] - **build,win**: add winget config to set up env (Hüseyin Açacak) [#54729](https://github.com/nodejs/node/pull/54729)
* \[[`93ac799b6b`](https://github.com/nodejs/node/commit/93ac799b6b)] - **cli**: fix spacing for port range error (Aviv Keller) [#54495](https://github.com/nodejs/node/pull/54495)
* \[[`3ba2e7bf97`](https://github.com/nodejs/node/commit/3ba2e7bf97)] - _**Revert**_ "**console**: colorize console error and warn" (Aviv Keller) [#54677](https://github.com/nodejs/node/pull/54677)
* \[[`2f678ea53b`](https://github.com/nodejs/node/commit/2f678ea53b)] - **crypto**: ensure invalid SubtleCrypto JWK data import results in DataError (Filip Skokan) [#55041](https://github.com/nodejs/node/pull/55041)
* \[[`5d28d98542`](https://github.com/nodejs/node/commit/5d28d98542)] - **deps**: update undici to 6.20.0 (Node.js GitHub Bot) [#55329](https://github.com/nodejs/node/pull/55329)
* \[[`0c7f2fc421`](https://github.com/nodejs/node/commit/0c7f2fc421)] - **deps**: update archs files for openssl-3.0.15+quic1 (Node.js GitHub Bot) [#55184](https://github.com/nodejs/node/pull/55184)
* \[[`da15e7edf5`](https://github.com/nodejs/node/commit/da15e7edf5)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.15+quic1 (Node.js GitHub Bot) [#55184](https://github.com/nodejs/node/pull/55184)
* \[[`381f1f9d08`](https://github.com/nodejs/node/commit/381f1f9d08)] - **deps**: update archs files for openssl-3.0.14+quic1 (Node.js GitHub Bot) [#54336](https://github.com/nodejs/node/pull/54336)
* \[[`48d643f78a`](https://github.com/nodejs/node/commit/48d643f78a)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.14+quic1 (Node.js GitHub Bot) [#54336](https://github.com/nodejs/node/pull/54336)
* \[[`7b1796803b`](https://github.com/nodejs/node/commit/7b1796803b)] - **deps**: update timezone to 2024b (Node.js GitHub Bot) [#55056](https://github.com/nodejs/node/pull/55056)
* \[[`8f1956c588`](https://github.com/nodejs/node/commit/8f1956c588)] - **deps**: update acorn-walk to 8.3.4 (Node.js GitHub Bot) [#54950](https://github.com/nodejs/node/pull/54950)
* \[[`20501a7350`](https://github.com/nodejs/node/commit/20501a7350)] - **deps**: update corepack to 0.29.4 (Node.js GitHub Bot) [#54845](https://github.com/nodejs/node/pull/54845)
* \[[`0f81eafecc`](https://github.com/nodejs/node/commit/0f81eafecc)] - **doc**: fix Markdown linter (Antoine du Hamel) [#55344](https://github.com/nodejs/node/pull/55344)
* \[[`df713f0a98`](https://github.com/nodejs/node/commit/df713f0a98)] - _**Revert**_ "**doc**: update test context.assert" (Antoine du Hamel) [#55344](https://github.com/nodejs/node/pull/55344)
* \[[`fd6fc61d2c`](https://github.com/nodejs/node/commit/fd6fc61d2c)] - **doc**: add pmarchini to collaborators (Pietro Marchini) [#55331](https://github.com/nodejs/node/pull/55331)
* \[[`b963db9ee2`](https://github.com/nodejs/node/commit/b963db9ee2)] - **doc**: fix `events.once()` example using `AbortSignal` (Ivo Janssen) [#55144](https://github.com/nodejs/node/pull/55144)
* \[[`50b13bfb12`](https://github.com/nodejs/node/commit/50b13bfb12)] - **doc**: add onboarding details for ambassador program (Marco Ippolito) [#55284](https://github.com/nodejs/node/pull/55284)
* \[[`27564b7811`](https://github.com/nodejs/node/commit/27564b7811)] - **doc**: fix initial default value of autoSelectFamily (Ihor Rohovets) [#55245](https://github.com/nodejs/node/pull/55245)
* \[[`9e7be23aa5`](https://github.com/nodejs/node/commit/9e7be23aa5)] - **doc**: tweak onboarding instructions (Michael Dawson) [#55212](https://github.com/nodejs/node/pull/55212)
* \[[`f412a029c3`](https://github.com/nodejs/node/commit/f412a029c3)] - **doc**: update test context.assert (Pietro Marchini) [#55186](https://github.com/nodejs/node/pull/55186)
* \[[`2f7828debb`](https://github.com/nodejs/node/commit/2f7828debb)] - **doc**: fix unordered error anchors (Antoine du Hamel) [#55242](https://github.com/nodejs/node/pull/55242)
* \[[`d08e4c235b`](https://github.com/nodejs/node/commit/d08e4c235b)] - **doc**: mention addons to experimental permission (Rafael Gonzaga) [#55166](https://github.com/nodejs/node/pull/55166)
* \[[`d65c2458dc`](https://github.com/nodejs/node/commit/d65c2458dc)] - **doc**: use correct dash in stability status (Antoine du Hamel) [#55200](https://github.com/nodejs/node/pull/55200)
* \[[`d9839c16cf`](https://github.com/nodejs/node/commit/d9839c16cf)] - **doc**: fix link in `test/README.md` (Livia Medeiros) [#55165](https://github.com/nodejs/node/pull/55165)
* \[[`1ad659afa4`](https://github.com/nodejs/node/commit/1ad659afa4)] - **doc**: add esm examples to node:net (Alfredo González) [#55134](https://github.com/nodejs/node/pull/55134)
* \[[`81ad69d50f`](https://github.com/nodejs/node/commit/81ad69d50f)] - **doc**: move the YAML changes element (sendoru) [#55112](https://github.com/nodejs/node/pull/55112)
* \[[`7a51a161be`](https://github.com/nodejs/node/commit/7a51a161be)] - **doc**: fix the require resolve algorithm in `modules.md` (chirsz) [#55117](https://github.com/nodejs/node/pull/55117)
* \[[`80edcdf899`](https://github.com/nodejs/node/commit/80edcdf899)] - **doc**: update style guide (Aviv Keller) [#53223](https://github.com/nodejs/node/pull/53223)
* \[[`388c754dd2`](https://github.com/nodejs/node/commit/388c754dd2)] - **doc**: add missing `:` to `run()`'s `globPatterns` (Aviv Keller) [#55135](https://github.com/nodejs/node/pull/55135)
* \[[`94302b6a76`](https://github.com/nodejs/node/commit/94302b6a76)] - **doc**: add abmusse to collaborators (Abdirahim Musse) [#55086](https://github.com/nodejs/node/pull/55086)
* \[[`27ff2da964`](https://github.com/nodejs/node/commit/27ff2da964)] - **doc**: add note about `--expose-internals` (Aviv Keller) [#52861](https://github.com/nodejs/node/pull/52861)
* \[[`df6dc753b7`](https://github.com/nodejs/node/commit/df6dc753b7)] - **doc**: remove `parseREPLKeyword` from REPL documentation (Aviv Keller) [#54749](https://github.com/nodejs/node/pull/54749)
* \[[`4baa5c4d10`](https://github.com/nodejs/node/commit/4baa5c4d10)] - **doc**: change backporting guide with updated info (Aviv Keller) [#53746](https://github.com/nodejs/node/pull/53746)
* \[[`9947fc112f`](https://github.com/nodejs/node/commit/9947fc112f)] - **doc**: add missing definitions to `internal-api.md` (Aviv Keller) [#53303](https://github.com/nodejs/node/pull/53303)
* \[[`a4586f0e94`](https://github.com/nodejs/node/commit/a4586f0e94)] - **doc**: update documentation for externalizing deps (Michael Dawson) [#54792](https://github.com/nodejs/node/pull/54792)
* \[[`70504f8522`](https://github.com/nodejs/node/commit/70504f8522)] - **doc**: update `require(ESM)` history and stability status (Antoine du Hamel) [#55199](https://github.com/nodejs/node/pull/55199)
* \[[`9d0041ac40`](https://github.com/nodejs/node/commit/9d0041ac40)] - **doc**: add release key for aduh95 (Antoine du Hamel) [#55349](https://github.com/nodejs/node/pull/55349)
* \[[`0c1666a52a`](https://github.com/nodejs/node/commit/0c1666a52a)] - **events**: allow null/undefined eventInitDict (Matthew Aitken) [#54643](https://github.com/nodejs/node/pull/54643)
* \[[`453df77f99`](https://github.com/nodejs/node/commit/453df77f99)] - **events**: return `currentTarget` when dispatching (Matthew Aitken) [#54642](https://github.com/nodejs/node/pull/54642)
* \[[`0decaab9db`](https://github.com/nodejs/node/commit/0decaab9db)] - **fs**: acknowledge `signal` option in `filehandle.createReadStream()` (Livia Medeiros) [#55148](https://github.com/nodejs/node/pull/55148)
* \[[`00a2fc7166`](https://github.com/nodejs/node/commit/00a2fc7166)] - **lib**: move `Symbol[Async]Dispose` polyfills to `internal/util` (Antoine du Hamel) [#54853](https://github.com/nodejs/node/pull/54853)
* \[[`8e6b606ac4`](https://github.com/nodejs/node/commit/8e6b606ac4)] - **lib**: remove lib/internal/idna.js (Yagiz Nizipli) [#55050](https://github.com/nodejs/node/pull/55050)
* \[[`c96e5cb664`](https://github.com/nodejs/node/commit/c96e5cb664)] - **lib**: the REPL should survive deletion of Array.prototype methods (Jordan Harband) [#31457](https://github.com/nodejs/node/pull/31457)
* \[[`748ed2e559`](https://github.com/nodejs/node/commit/748ed2e559)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#55300](https://github.com/nodejs/node/pull/55300)
* \[[`8b8d35f015`](https://github.com/nodejs/node/commit/8b8d35f015)] - **meta**: bump mozilla-actions/sccache-action from 0.0.5 to 0.0.6 (dependabot\[bot]) [#55225](https://github.com/nodejs/node/pull/55225)
* \[[`d3441ff0c8`](https://github.com/nodejs/node/commit/d3441ff0c8)] - **meta**: bump actions/checkout from 4.1.7 to 4.2.0 (dependabot\[bot]) [#55224](https://github.com/nodejs/node/pull/55224)
* \[[`1c20908558`](https://github.com/nodejs/node/commit/1c20908558)] - **meta**: bump actions/setup-node from 4.0.3 to 4.0.4 (dependabot\[bot]) [#55223](https://github.com/nodejs/node/pull/55223)
* \[[`8a529abd69`](https://github.com/nodejs/node/commit/8a529abd69)] - **meta**: bump peter-evans/create-pull-request from 7.0.1 to 7.0.5 (dependabot\[bot]) [#55219](https://github.com/nodejs/node/pull/55219)
* \[[`9053d210ab`](https://github.com/nodejs/node/commit/9053d210ab)] - **meta**: add mailmap entry for abmusse (Abdirahim Musse) [#55182](https://github.com/nodejs/node/pull/55182)
* \[[`db2496c125`](https://github.com/nodejs/node/commit/db2496c125)] - **meta**: add more information about nightly releases (Aviv Keller) [#55084](https://github.com/nodejs/node/pull/55084)
* \[[`d2ce003b2f`](https://github.com/nodejs/node/commit/d2ce003b2f)] - **meta**: add `linux` to OS labels in collaborator guide (Aviv Keller) [#54986](https://github.com/nodejs/node/pull/54986)
* \[[`37b0bea247`](https://github.com/nodejs/node/commit/37b0bea247)] - **meta**: remove never-used workflow trigger (Aviv Keller) [#54983](https://github.com/nodejs/node/pull/54983)
* \[[`ae27e2dcd7`](https://github.com/nodejs/node/commit/ae27e2dcd7)] - **meta**: add links to alternative issue trackers (Aviv Keller) [#54401](https://github.com/nodejs/node/pull/54401)
* \[[`6e5d524b0f`](https://github.com/nodejs/node/commit/6e5d524b0f)] - **module**: remove duplicated import (Aviv Keller) [#54942](https://github.com/nodejs/node/pull/54942)
* \[[`3a682cca03`](https://github.com/nodejs/node/commit/3a682cca03)] - **path**: remove repetitive conditional operator in `posix.resolve` (Wiyeong Seo) [#54835](https://github.com/nodejs/node/pull/54835)
* \[[`ac1cb8dfdb`](https://github.com/nodejs/node/commit/ac1cb8dfdb)] - **perf\_hooks**: add missing type argument to getEntriesByName (Luke Taher) [#54767](https://github.com/nodejs/node/pull/54767)
* \[[`85b3edc83b`](https://github.com/nodejs/node/commit/85b3edc83b)] - **repl**: catch `\v` and `\r` in new-line detection (Aviv Keller) [#54512](https://github.com/nodejs/node/pull/54512)
* \[[`df1f04999e`](https://github.com/nodejs/node/commit/df1f04999e)] - **src**: decode native error messages as UTF-8 (Joyee Cheung) [#55024](https://github.com/nodejs/node/pull/55024)
* \[[`86d718177a`](https://github.com/nodejs/node/commit/86d718177a)] - **src**: update clang-tidy and focus on modernization (Yagiz Nizipli) [#53757](https://github.com/nodejs/node/pull/53757)
* \[[`7d01b6a9c5`](https://github.com/nodejs/node/commit/7d01b6a9c5)] - **src**: cleanup per env handles directly without a list (Chengzhong Wu) [#54993](https://github.com/nodejs/node/pull/54993)
* \[[`a730cdb622`](https://github.com/nodejs/node/commit/a730cdb622)] - **src**: remove duplicate code setting AF\_INET (He Yang) [#54939](https://github.com/nodejs/node/pull/54939)
* \[[`f10d9ad283`](https://github.com/nodejs/node/commit/f10d9ad283)] - **stream**: treat null asyncIterator as undefined (Jason Zhang) [#55119](https://github.com/nodejs/node/pull/55119)
* \[[`6027084245`](https://github.com/nodejs/node/commit/6027084245)] - **test**: make `test-loaders-workers-spawned` less flaky (Antoine du Hamel) [#55172](https://github.com/nodejs/node/pull/55172)
* \[[`66a87d19bd`](https://github.com/nodejs/node/commit/66a87d19bd)] - **test**: update multiple assert tests to use node:test (James M Snell) [#54585](https://github.com/nodejs/node/pull/54585)
* \[[`5105188c47`](https://github.com/nodejs/node/commit/5105188c47)] - **test**: update wpt test for encoding (devstone) [#55151](https://github.com/nodejs/node/pull/55151)
* \[[`81bcec0b82`](https://github.com/nodejs/node/commit/81bcec0b82)] - **test**: deflake test/pummel/test-timers.js (jakecastelli) [#55098](https://github.com/nodejs/node/pull/55098)
* \[[`82c402265a`](https://github.com/nodejs/node/commit/82c402265a)] - **test**: deflake test-http-remove-header-stays-removed (Luigi Pinca) [#55004](https://github.com/nodejs/node/pull/55004)
* \[[`78021701ed`](https://github.com/nodejs/node/commit/78021701ed)] - **test**: fix test-tls-junk-closes-server (Michael Dawson) [#55089](https://github.com/nodejs/node/pull/55089)
* \[[`c908b8a2d8`](https://github.com/nodejs/node/commit/c908b8a2d8)] - **test**: fix more tests that fail when path contains a space (Antoine du Hamel) [#55088](https://github.com/nodejs/node/pull/55088)
* \[[`afc1628e73`](https://github.com/nodejs/node/commit/afc1628e73)] - **test**: fix `assertSnapshot` when path contains a quote (Antoine du Hamel) [#55087](https://github.com/nodejs/node/pull/55087)
* \[[`7c88739b83`](https://github.com/nodejs/node/commit/7c88739b83)] - **test**: fix some tests when path contains `%` (Antoine du Hamel) [#55082](https://github.com/nodejs/node/pull/55082)
* \[[`eb4d468671`](https://github.com/nodejs/node/commit/eb4d468671)] - _**Revert**_ "**test**: mark test-fs-watch-non-recursive flaky on Windows" (Luigi Pinca) [#55079](https://github.com/nodejs/node/pull/55079)
* \[[`bc7b5249d4`](https://github.com/nodejs/node/commit/bc7b5249d4)] - **test**: make `test-runner-assert` more robust (Aviv Keller) [#55036](https://github.com/nodejs/node/pull/55036)
* \[[`6c2a1386f7`](https://github.com/nodejs/node/commit/6c2a1386f7)] - **test**: update tls test to support OpenSSL32 (Michael Dawson) [#55030](https://github.com/nodejs/node/pull/55030)
* \[[`96406610fa`](https://github.com/nodejs/node/commit/96406610fa)] - **test**: fix `test-vm-context-dont-contextify` when path contains a space (Antoine du Hamel) [#55026](https://github.com/nodejs/node/pull/55026)
* \[[`39a80eed4f`](https://github.com/nodejs/node/commit/39a80eed4f)] - **test**: adjust tls-set-ciphers for OpenSSL32 (Michael Dawson) [#55016](https://github.com/nodejs/node/pull/55016)
* \[[`bd8fd4fceb`](https://github.com/nodejs/node/commit/bd8fd4fceb)] - **test**: add `util.stripVTControlCharacters` test (RedYetiDev) [#54865](https://github.com/nodejs/node/pull/54865)
* \[[`333b5a02d0`](https://github.com/nodejs/node/commit/333b5a02d0)] - **test**: improve coverage for timer promises schedular (Aviv Keller) [#53370](https://github.com/nodejs/node/pull/53370)
* \[[`f48992f433`](https://github.com/nodejs/node/commit/f48992f433)] - **test**: remove unused common utilities (RedYetiDev) [#54825](https://github.com/nodejs/node/pull/54825)
* \[[`93a098c56d`](https://github.com/nodejs/node/commit/93a098c56d)] - **test**: deflake test-http-header-overflow (Luigi Pinca) [#54978](https://github.com/nodejs/node/pull/54978)
* \[[`f849cf677d`](https://github.com/nodejs/node/commit/f849cf677d)] - **test**: fix `soucre` to `source` (Aviv Keller) [#55038](https://github.com/nodejs/node/pull/55038)
* \[[`1a007ea814`](https://github.com/nodejs/node/commit/1a007ea814)] - **test**: add asserts to validate test assumptions (Michael Dawson) [#54997](https://github.com/nodejs/node/pull/54997)
* \[[`6f53c096f8`](https://github.com/nodejs/node/commit/6f53c096f8)] - **test**: move test-http-max-sockets to parallel (Luigi Pinca) [#54977](https://github.com/nodejs/node/pull/54977)
* \[[`aba9dc775e`](https://github.com/nodejs/node/commit/aba9dc775e)] - **test**: remove test-http-max-sockets flaky designation (Luigi Pinca) [#54976](https://github.com/nodejs/node/pull/54976)
* \[[`ee5624bffe`](https://github.com/nodejs/node/commit/ee5624bffe)] - **test**: adjust key sizes to support OpenSSL32 (Michael Dawson) [#54972](https://github.com/nodejs/node/pull/54972)
* \[[`5c11a61140`](https://github.com/nodejs/node/commit/5c11a61140)] - **test**: update test to support OpenSSL32 (Michael Dawson) [#54968](https://github.com/nodejs/node/pull/54968)
* \[[`62f21470e4`](https://github.com/nodejs/node/commit/62f21470e4)] - **test**: update DOM events web platform tests (Matthew Aitken) [#54642](https://github.com/nodejs/node/pull/54642)
* \[[`426851705c`](https://github.com/nodejs/node/commit/426851705c)] - **test\_runner**: assert entry is a valid object (Edigleysson Silva (Edy)) [#55231](https://github.com/nodejs/node/pull/55231)
* \[[`b1cad519d7`](https://github.com/nodejs/node/commit/b1cad519d7)] - **test\_runner**: use `test:` symbol on second print of parent test (RedYetiDev) [#54956](https://github.com/nodejs/node/pull/54956)
* \[[`63c8f3d436`](https://github.com/nodejs/node/commit/63c8f3d436)] - **test\_runner**: replace ansi clear with ansi reset (Pietro Marchini) [#55013](https://github.com/nodejs/node/pull/55013)
* \[[`0b3fb344f7`](https://github.com/nodejs/node/commit/0b3fb344f7)] - **tools**: add `polyfilled` option to `prefer-primordials` rule (Antoine du Hamel) [#55318](https://github.com/nodejs/node/pull/55318)
* \[[`8981309bd9`](https://github.com/nodejs/node/commit/8981309bd9)] - **tools**: make `choco install` script more readable (Aviv Keller) [#54002](https://github.com/nodejs/node/pull/54002)
* \[[`7310abeae1`](https://github.com/nodejs/node/commit/7310abeae1)] - **tools**: bump Rollup from 4.18.1 to 4.22.4 for `lint-md` (dependabot\[bot]) [#55093](https://github.com/nodejs/node/pull/55093)
* \[[`083311e8af`](https://github.com/nodejs/node/commit/083311e8af)] - **tools**: remove redudant code from eslint require rule (Aviv Keller) [#54892](https://github.com/nodejs/node/pull/54892)
* \[[`ae4b2aece1`](https://github.com/nodejs/node/commit/ae4b2aece1)] - **tools**: update error message for ICU in license-builder (Aviv Keller) [#54742](https://github.com/nodejs/node/pull/54742)
* \[[`3ebd31684d`](https://github.com/nodejs/node/commit/3ebd31684d)] - **tools**: update github\_reporter to 1.7.1 (Node.js GitHub Bot) [#54951](https://github.com/nodejs/node/pull/54951)
* \[[`397be8a10e`](https://github.com/nodejs/node/commit/397be8a10e)] - **tty**: fix links for terminal colors (Aviv Keller) [#54596](https://github.com/nodejs/node/pull/54596)
* \[[`a3c2ef9e98`](https://github.com/nodejs/node/commit/a3c2ef9e98)] - **util**: update ansi regex (Aviv Keller) [#54865](https://github.com/nodejs/node/pull/54865)
* \[[`efdccc88a2`](https://github.com/nodejs/node/commit/efdccc88a2)] - **watch**: preserve output when gracefully restarted (Théo LUDWIG) [#54323](https://github.com/nodejs/node/pull/54323)
* \[[`226836c5ac`](https://github.com/nodejs/node/commit/226836c5ac)] - **worker**: throw InvalidStateError in postMessage after close (devstone) [#55206](https://github.com/nodejs/node/pull/55206)
* \[[`f39ff4d14b`](https://github.com/nodejs/node/commit/f39ff4d14b)] - **worker**: handle `--input-type` more consistently (Antoine du Hamel) [#54979](https://github.com/nodejs/node/pull/54979)
* \[[`30383ffb9a`](https://github.com/nodejs/node/commit/30383ffb9a)] - **zlib**: throw brotli initialization error from c++ (Yagiz Nizipli) [#54698](https://github.com/nodejs/node/pull/54698)

<a id="20.18.0"></a>

## 2024-10-03, Version 20.18.0 'Iron' (LTS), @targos

### Notable Changes

### Experimental Network Inspection Support in Node.js

This update introduces the initial support for network inspection in Node.js.
Currently, this is an experimental feature, so you need to enable it using the `--experimental-network-inspection` flag.
With this feature enabled, you can inspect network activities occurring within a JavaScript application.

To use network inspection, start your Node.js application with the following command:

```console
$ node --inspect-wait --experimental-network-inspection index.js
```

Please note that the network inspection capabilities are in active development.
We are actively working on enhancing this feature and will continue to expand its functionality in future updates.

* Network inspection is limited to the `http` and `https` modules only.
* The Network tab in Chrome DevTools will not be available until the
  [feature request on the Chrome DevTools side](https://issues.chromium.org/issues/353924015) is addressed.

Contributed by Kohei Ueno in [#53593](https://github.com/nodejs/node/pull/53593) and [#54246](https://github.com/nodejs/node/pull/54246)

#### Exposes X509\_V\_FLAG\_PARTIAL\_CHAIN to tls.createSecureContext

This releases introduces a new option to the API `tls.createSecureContext`. From
now on, `tls.createSecureContext({ allowPartialTrustChain: true })` can be used
to treat intermediate (non-self-signed) certificates in the trust CA certificate
list as trusted.

Contributed by Anna Henningsen in [#54790](https://github.com/nodejs/node/pull/54790)

#### New option for vm.createContext() to create a context with a freezable globalThis

Node.js implements a flavor of `vm.createContext()` and friends that creates a context without contextifying its global
object when vm.constants.DONT\_CONTEXTIFY is used. This is suitable when users want to freeze the context
(impossible when the global is contextified i.e. has interceptors installed) or speed up the global access if they
don't need the interceptor behavior.

Contributed by Joyee Cheung in [#54394](https://github.com/nodejs/node/pull/54394)

#### Deprecations

* \[[`64aa31f6e5`](https://github.com/nodejs/node/commit/64aa31f6e5)] - **repl**: doc-deprecate instantiating `node:repl` classes without `new` (Aviv Keller) [#54842](https://github.com/nodejs/node/pull/54842)
* \[[`4c52ee3d7f`](https://github.com/nodejs/node/commit/4c52ee3d7f)] - **zlib**: deprecate instantiating classes without new (Yagiz Nizipli) [#54708](https://github.com/nodejs/node/pull/54708)

#### Other Notable Changes

* \[[`b80da2f964`](https://github.com/nodejs/node/commit/b80da2f964)] - **buffer**: optimize createFromString (Robert Nagy) [#54324](https://github.com/nodejs/node/pull/54324)
* \[[`02b36cbd2d`](https://github.com/nodejs/node/commit/02b36cbd2d)] - **(SEMVER-MINOR)** **lib**: add EventSource Client (Aras Abbasi) [#51575](https://github.com/nodejs/node/pull/51575)
* \[[`879546a9bf`](https://github.com/nodejs/node/commit/879546a9bf)] - **(SEMVER-MINOR)** **src,lib**: add performance.uvMetricsInfo (Rafael Gonzaga) [#54413](https://github.com/nodejs/node/pull/54413)
* \[[`f789f4c92d`](https://github.com/nodejs/node/commit/f789f4c92d)] - **(SEMVER-MINOR)** **test\_runner**: support module mocking (Colin Ihrig) [#52848](https://github.com/nodejs/node/pull/52848)
* \[[`4eb0749b6c`](https://github.com/nodejs/node/commit/4eb0749b6c)] - **(SEMVER-MINOR)** **url**: implement parse method for safer URL parsing (Ali Hassan) [#52280](https://github.com/nodejs/node/pull/52280)

### Commits

* \[[`013c48f0e9`](https://github.com/nodejs/node/commit/013c48f0e9)] - **benchmark**: --no-warnings to avoid DEP/ExpWarn log (Rafael Gonzaga) [#54928](https://github.com/nodejs/node/pull/54928)
* \[[`194fc113ac`](https://github.com/nodejs/node/commit/194fc113ac)] - **benchmark**: add buffer.isAscii benchmark (RafaelGSS) [#54740](https://github.com/nodejs/node/pull/54740)
* \[[`7410d51cb9`](https://github.com/nodejs/node/commit/7410d51cb9)] - **benchmark**: add buffer.isUtf8 bench (RafaelGSS) [#54740](https://github.com/nodejs/node/pull/54740)
* \[[`2393f21e8a`](https://github.com/nodejs/node/commit/2393f21e8a)] - **benchmark**: add access async version to bench (Rafael Gonzaga) [#54747](https://github.com/nodejs/node/pull/54747)
* \[[`b8779721f0`](https://github.com/nodejs/node/commit/b8779721f0)] - **benchmark**: enhance dc publish benchmark (Rafael Gonzaga) [#54745](https://github.com/nodejs/node/pull/54745)
* \[[`4078aa83ff`](https://github.com/nodejs/node/commit/4078aa83ff)] - **benchmark**: add match and doesNotMatch bench (RafaelGSS) [#54734](https://github.com/nodejs/node/pull/54734)
* \[[`66acab9976`](https://github.com/nodejs/node/commit/66acab9976)] - **benchmark**: add rejects and doesNotReject bench (RafaelGSS) [#54734](https://github.com/nodejs/node/pull/54734)
* \[[`6db777fb3a`](https://github.com/nodejs/node/commit/6db777fb3a)] - **benchmark**: add throws and doesNotThrow bench (RafaelGSS) [#54734](https://github.com/nodejs/node/pull/54734)
* \[[`8f101560ce`](https://github.com/nodejs/node/commit/8f101560ce)] - **benchmark**: add strictEqual and notStrictEqual bench (RafaelGSS) [#54734](https://github.com/nodejs/node/pull/54734)
* \[[`2c9e4c936e`](https://github.com/nodejs/node/commit/2c9e4c936e)] - **benchmark**: adds groups to better separate benchmarks (Giovanni Bucci) [#54393](https://github.com/nodejs/node/pull/54393)
* \[[`671c3ac633`](https://github.com/nodejs/node/commit/671c3ac633)] - **benchmark**: fix benchmark for file path and URL conversion (Early Riser) [#54190](https://github.com/nodejs/node/pull/54190)
* \[[`8c8708cb5b`](https://github.com/nodejs/node/commit/8c8708cb5b)] - **benchmark**: use assert.ok searchparams (Rafael Gonzaga) [#54334](https://github.com/nodejs/node/pull/54334)
* \[[`8b71fa79e2`](https://github.com/nodejs/node/commit/8b71fa79e2)] - **benchmark**: add stream.compose benchmark (jakecastelli) [#54308](https://github.com/nodejs/node/pull/54308)
* \[[`93ee36e3a0`](https://github.com/nodejs/node/commit/93ee36e3a0)] - **benchmark**: rename count to n (Rafael Gonzaga) [#54271](https://github.com/nodejs/node/pull/54271)
* \[[`f2971b6f0b`](https://github.com/nodejs/node/commit/f2971b6f0b)] - **benchmark**: change assert() to assert.ok() (Rafael Gonzaga) [#54254](https://github.com/nodejs/node/pull/54254)
* \[[`f48f2c212c`](https://github.com/nodejs/node/commit/f48f2c212c)] - **benchmark**: support --help in CLI (Aviv Keller) [#53358](https://github.com/nodejs/node/pull/53358)
* \[[`0309b0520b`](https://github.com/nodejs/node/commit/0309b0520b)] - **benchmark**: remove force option as force defaults to true (Yelim Koo) [#54203](https://github.com/nodejs/node/pull/54203)
* \[[`b6e8305b2d`](https://github.com/nodejs/node/commit/b6e8305b2d)] - **benchmark**: use assert.ok instead of assert (Rafael Gonzaga) [#54176](https://github.com/nodejs/node/pull/54176)
* \[[`90c660d26a`](https://github.com/nodejs/node/commit/90c660d26a)] - **benchmark**: add require-esm benchmark (Joyee Cheung) [#52166](https://github.com/nodejs/node/pull/52166)
* \[[`1b8584b52e`](https://github.com/nodejs/node/commit/1b8584b52e)] - **benchmark,doc**: add CPU scaling governor to perf (Rafael Gonzaga) [#54723](https://github.com/nodejs/node/pull/54723)
* \[[`0b9161b330`](https://github.com/nodejs/node/commit/0b9161b330)] - **benchmark,doc**: mention bar.R to the list of scripts (Rafael Gonzaga) [#54722](https://github.com/nodejs/node/pull/54722)
* \[[`84bf93b7ea`](https://github.com/nodejs/node/commit/84bf93b7ea)] - **buffer**: allow invalid encoding in from (Robert Nagy) [#54533](https://github.com/nodejs/node/pull/54533)
* \[[`d04246a0d7`](https://github.com/nodejs/node/commit/d04246a0d7)] - **buffer**: optimize byteLength for common encodings (Robert Nagy) [#54342](https://github.com/nodejs/node/pull/54342)
* \[[`f36831f694`](https://github.com/nodejs/node/commit/f36831f694)] - **buffer**: optimize createFromString (Robert Nagy) [#54324](https://github.com/nodejs/node/pull/54324)
* \[[`f5f40c8088`](https://github.com/nodejs/node/commit/f5f40c8088)] - **buffer**: optimize for common encodings (Robert Nagy) [#54319](https://github.com/nodejs/node/pull/54319)
* \[[`76c37703be`](https://github.com/nodejs/node/commit/76c37703be)] - **buffer**: add JSDoc to blob bytes method (Roberto Simonini) [#54117](https://github.com/nodejs/node/pull/54117)
* \[[`3012d31404`](https://github.com/nodejs/node/commit/3012d31404)] - **buffer**: use faster integer argument check (Robert Nagy) [#54089](https://github.com/nodejs/node/pull/54089)
* \[[`3505782801`](https://github.com/nodejs/node/commit/3505782801)] - **buffer**: make indexOf(byte) faster (Tobias Nießen) [#53455](https://github.com/nodejs/node/pull/53455)
* \[[`d285fc1f68`](https://github.com/nodejs/node/commit/d285fc1f68)] - **build**: upgrade clang-format to v18 (Aviv Keller) [#53957](https://github.com/nodejs/node/pull/53957)
* \[[`d288ec3b0a`](https://github.com/nodejs/node/commit/d288ec3b0a)] - **build**: fix conflicting V8 object print flags (Daeyeon Jeong) [#54785](https://github.com/nodejs/node/pull/54785)
* \[[`e862eecac9`](https://github.com/nodejs/node/commit/e862eecac9)] - **build**: do not build with code cache for core coverage collection (Joyee Cheung) [#54633](https://github.com/nodejs/node/pull/54633)
* \[[`f7a606eb96`](https://github.com/nodejs/node/commit/f7a606eb96)] - **build**: turn off `-Wrestrict` (Richard Lau) [#54737](https://github.com/nodejs/node/pull/54737)
* \[[`71ca2665e4`](https://github.com/nodejs/node/commit/71ca2665e4)] - **build**: reclaim disk space on macOS GHA runner (jakecastelli) [#54658](https://github.com/nodejs/node/pull/54658)
* \[[`82d8051c39`](https://github.com/nodejs/node/commit/82d8051c39)] - **build**: don't clean obj.target directory if it doesn't exist (Joyee Cheung) [#54337](https://github.com/nodejs/node/pull/54337)
* \[[`6e550b1f26`](https://github.com/nodejs/node/commit/6e550b1f26)] - **build**: update `ruff` to `0.5.2` (Aviv Keller) [#53909](https://github.com/nodejs/node/pull/53909)
* \[[`e2ea7b26d7`](https://github.com/nodejs/node/commit/e2ea7b26d7)] - **build**: fix ./configure --help format error (Zhenwei Jin) [#53066](https://github.com/nodejs/node/pull/53066)
* \[[`eb2402d569`](https://github.com/nodejs/node/commit/eb2402d569)] - **build**: enable building with shared uvwasi lib (Pooja D P) [#43987](https://github.com/nodejs/node/pull/43987)
* \[[`45732314d4`](https://github.com/nodejs/node/commit/45732314d4)] - **build**: sync V8 warning cflags with BUILD.gn (Michaël Zasso) [#52873](https://github.com/nodejs/node/pull/52873)
* \[[`6e0a2bb54c`](https://github.com/nodejs/node/commit/6e0a2bb54c)] - **build**: harmonize Clang checks (Michaël Zasso) [#52873](https://github.com/nodejs/node/pull/52873)
* \[[`3f78d4eb28`](https://github.com/nodejs/node/commit/3f78d4eb28)] - **cli**: add `--expose-gc` flag available to `NODE_OPTIONS` (Juan José) [#53078](https://github.com/nodejs/node/pull/53078)
* \[[`a110409b2a`](https://github.com/nodejs/node/commit/a110409b2a)] - **console**: use validateOneOf for colorMode validation (HEESEUNG) [#54245](https://github.com/nodejs/node/pull/54245)
* \[[`231ab788ea`](https://github.com/nodejs/node/commit/231ab788ea)] - **crypto**: reject dh,x25519,x448 in {Sign,Verify}Final (Huáng Jùnliàng) [#53774](https://github.com/nodejs/node/pull/53774)
* \[[`a5984e4570`](https://github.com/nodejs/node/commit/a5984e4570)] - **crypto**: return a clearer error when loading an unsupported pkcs12 (Tim Perry) [#54485](https://github.com/nodejs/node/pull/54485)
* \[[`f287cd77bd`](https://github.com/nodejs/node/commit/f287cd77bd)] - **crypto**: remove unused `kHashTypes` internal (Antoine du Hamel) [#54627](https://github.com/nodejs/node/pull/54627)
* \[[`1fc904f8c4`](https://github.com/nodejs/node/commit/1fc904f8c4)] - **deps**: update cjs-module-lexer to 1.4.1 (Node.js GitHub Bot) [#54846](https://github.com/nodejs/node/pull/54846)
* \[[`95b55c39b1`](https://github.com/nodejs/node/commit/95b55c39b1)] - **deps**: update simdutf to 5.5.0 (Node.js GitHub Bot) [#54434](https://github.com/nodejs/node/pull/54434)
* \[[`cf6ded5dd3`](https://github.com/nodejs/node/commit/cf6ded5dd3)] - **deps**: update cjs-module-lexer to 1.4.0 (Node.js GitHub Bot) [#54713](https://github.com/nodejs/node/pull/54713)
* \[[`7f8edce3f1`](https://github.com/nodejs/node/commit/7f8edce3f1)] - **deps**: update c-ares to v1.33.1 (Node.js GitHub Bot) [#54549](https://github.com/nodejs/node/pull/54549)
* \[[`9a4a7b7ecc`](https://github.com/nodejs/node/commit/9a4a7b7ecc)] - **deps**: update undici to 6.19.8 (Node.js GitHub Bot) [#54456](https://github.com/nodejs/node/pull/54456)
* \[[`87ca1d7fee`](https://github.com/nodejs/node/commit/87ca1d7fee)] - **deps**: update simdutf to 5.3.4 (Node.js GitHub Bot) [#54312](https://github.com/nodejs/node/pull/54312)
* \[[`d3a743f182`](https://github.com/nodejs/node/commit/d3a743f182)] - **deps**: update zlib to 1.3.0.1-motley-71660e1 (Node.js GitHub Bot) [#53464](https://github.com/nodejs/node/pull/53464)
* \[[`926981aa9f`](https://github.com/nodejs/node/commit/926981aa9f)] - **deps**: update zlib to 1.3.0.1-motley-c2469fd (Node.js GitHub Bot) [#53464](https://github.com/nodejs/node/pull/53464)
* \[[`654c8d1fdc`](https://github.com/nodejs/node/commit/654c8d1fdc)] - **deps**: update zlib to 1.3.0.1-motley-68e57e6 (Node.js GitHub Bot) [#53464](https://github.com/nodejs/node/pull/53464)
* \[[`2477e79172`](https://github.com/nodejs/node/commit/2477e79172)] - **deps**: update zlib to 1.3.0.1-motley-8b7eff8 (Node.js GitHub Bot) [#53464](https://github.com/nodejs/node/pull/53464)
* \[[`3d8113faf5`](https://github.com/nodejs/node/commit/3d8113faf5)] - **deps**: update zlib to 1.3.0.1-motley-e432200 (Node.js GitHub Bot) [#53464](https://github.com/nodejs/node/pull/53464)
* \[[`ac294e3db4`](https://github.com/nodejs/node/commit/ac294e3db4)] - **deps**: update zlib to 1.3.0.1-motley-887bb57 (Node.js GitHub Bot) [#53464](https://github.com/nodejs/node/pull/53464)
* \[[`239588b968`](https://github.com/nodejs/node/commit/239588b968)] - **deps**: update c-ares to v1.33.0 (Node.js GitHub Bot) [#54198](https://github.com/nodejs/node/pull/54198)
* \[[`6e7de37ed3`](https://github.com/nodejs/node/commit/6e7de37ed3)] - **deps**: update undici to 6.19.7 (Node.js GitHub Bot) [#54286](https://github.com/nodejs/node/pull/54286)
* \[[`38aa9d6ea9`](https://github.com/nodejs/node/commit/38aa9d6ea9)] - **deps**: update acorn to 8.12.1 (Node.js GitHub Bot) [#53465](https://github.com/nodejs/node/pull/53465)
* \[[`d30145f663`](https://github.com/nodejs/node/commit/d30145f663)] - **deps**: update undici to 6.19.5 (Node.js GitHub Bot) [#54076](https://github.com/nodejs/node/pull/54076)
* \[[`c169d9c12b`](https://github.com/nodejs/node/commit/c169d9c12b)] - **deps**: update simdutf to 5.3.1 (Node.js GitHub Bot) [#54196](https://github.com/nodejs/node/pull/54196)
* \[[`92f3447957`](https://github.com/nodejs/node/commit/92f3447957)] - **doc**: add missing EventSource docs to globals (Matthew Aitken) [#55022](https://github.com/nodejs/node/pull/55022)
* \[[`2879ce9681`](https://github.com/nodejs/node/commit/2879ce9681)] - **doc**: fix broken Android building link (Niklas Wenzel) [#54922](https://github.com/nodejs/node/pull/54922)
* \[[`096623b59a`](https://github.com/nodejs/node/commit/096623b59a)] - **doc**: add support link for aduh95 (Antoine du Hamel) [#54866](https://github.com/nodejs/node/pull/54866)
* \[[`1dfd238781`](https://github.com/nodejs/node/commit/1dfd238781)] - **doc**: run license-builder (github-actions\[bot]) [#54854](https://github.com/nodejs/node/pull/54854)
* \[[`a6c748fffb`](https://github.com/nodejs/node/commit/a6c748fffb)] - **doc**: experimental flag for global accessible APIs (Chengzhong Wu) [#54330](https://github.com/nodejs/node/pull/54330)
* \[[`d48a22fa14`](https://github.com/nodejs/node/commit/d48a22fa14)] - **doc**: add `ERR_INVALID_ADDRESS` to `errors.md` (Aviv Keller) [#54661](https://github.com/nodejs/node/pull/54661)
* \[[`4a840cecfa`](https://github.com/nodejs/node/commit/4a840cecfa)] - **doc**: add support link for mcollina (Matteo Collina) [#54786](https://github.com/nodejs/node/pull/54786)
* \[[`ec22d86512`](https://github.com/nodejs/node/commit/ec22d86512)] - **doc**: mark `--conditions` CLI flag as stable (Guy Bedford) [#54209](https://github.com/nodejs/node/pull/54209)
* \[[`77c702ca07`](https://github.com/nodejs/node/commit/77c702ca07)] - **doc**: fix typo in recognizing-contributors (Tobias Nießen) [#54822](https://github.com/nodejs/node/pull/54822)
* \[[`62953ef9fb`](https://github.com/nodejs/node/commit/62953ef9fb)] - **doc**: clarify `--max-old-space-size` and `--max-semi-space-size` units (Alexandre ABRIOUX) [#54477](https://github.com/nodejs/node/pull/54477)
* \[[`e2bab0f2b2`](https://github.com/nodejs/node/commit/e2bab0f2b2)] - **doc**: replace --allow-fs-read by --allow-fs-write in related section (M1CK431) [#54427](https://github.com/nodejs/node/pull/54427)
* \[[`9cbfd5b33a`](https://github.com/nodejs/node/commit/9cbfd5b33a)] - **doc**: add support link for marco-ippolito (Marco Ippolito) [#54789](https://github.com/nodejs/node/pull/54789)
* \[[`53167b29ef`](https://github.com/nodejs/node/commit/53167b29ef)] - **doc**: fix typo (Michael Dawson) [#54640](https://github.com/nodejs/node/pull/54640)
* \[[`87f78a35f7`](https://github.com/nodejs/node/commit/87f78a35f7)] - **doc**: fix webcrypto.md AES-GCM backticks (Filip Skokan) [#54621](https://github.com/nodejs/node/pull/54621)
* \[[`7c83c15221`](https://github.com/nodejs/node/commit/7c83c15221)] - **doc**: add documentation about os.tmpdir() overrides (Joyee Cheung) [#54613](https://github.com/nodejs/node/pull/54613)
* \[[`4bfd832d70`](https://github.com/nodejs/node/commit/4bfd832d70)] - **doc**: add support me link for anonrig (Yagiz Nizipli) [#54611](https://github.com/nodejs/node/pull/54611)
* \[[`22a103e5ec`](https://github.com/nodejs/node/commit/22a103e5ec)] - **doc**: add alert on REPL from TCP socket (Rafael Gonzaga) [#54594](https://github.com/nodejs/node/pull/54594)
* \[[`b6374c24e1`](https://github.com/nodejs/node/commit/b6374c24e1)] - **doc**: fix typo in styleText description (Rafael Gonzaga) [#54616](https://github.com/nodejs/node/pull/54616)
* \[[`2f5b98ee1f`](https://github.com/nodejs/node/commit/2f5b98ee1f)] - **doc**: add getHeapStatistics() property descriptions (Benji Marinacci) [#54584](https://github.com/nodejs/node/pull/54584)
* \[[`482302b99b`](https://github.com/nodejs/node/commit/482302b99b)] - **doc**: fix information about including coverage files (Aviv Keller) [#54527](https://github.com/nodejs/node/pull/54527)
* \[[`b3708e7df4`](https://github.com/nodejs/node/commit/b3708e7df4)] - **doc**: support collaborators - talk amplification (Michael Dawson) [#54508](https://github.com/nodejs/node/pull/54508)
* \[[`c86fe23012`](https://github.com/nodejs/node/commit/c86fe23012)] - **doc**: add note about shasum generation failure (Marco Ippolito) [#54487](https://github.com/nodejs/node/pull/54487)
* \[[`d53e6cf755`](https://github.com/nodejs/node/commit/d53e6cf755)] - **doc**: fix capitalization in module.md (shallow-beach) [#54488](https://github.com/nodejs/node/pull/54488)
* \[[`cdc6713f18`](https://github.com/nodejs/node/commit/cdc6713f18)] - **doc**: add esm examples to node:https (Alfredo González) [#54399](https://github.com/nodejs/node/pull/54399)
* \[[`1ac1fe4e65`](https://github.com/nodejs/node/commit/1ac1fe4e65)] - **doc**: fix error description of the max header size (Egawa Ryo) [#54125](https://github.com/nodejs/node/pull/54125)
* \[[`244542b720`](https://github.com/nodejs/node/commit/244542b720)] - **doc**: add git node security --cleanup (Rafael Gonzaga) [#54381](https://github.com/nodejs/node/pull/54381)
* \[[`69fb71f54c`](https://github.com/nodejs/node/commit/69fb71f54c)] - **doc**: add note on weakness of permission model (Tobias Nießen) [#54268](https://github.com/nodejs/node/pull/54268)
* \[[`83b2cb908b`](https://github.com/nodejs/node/commit/83b2cb908b)] - **doc**: add versions when `--watch-preserve-output` was added (Théo LUDWIG) [#54328](https://github.com/nodejs/node/pull/54328)
* \[[`460fb49483`](https://github.com/nodejs/node/commit/460fb49483)] - **doc**: replace v19 mention in Current release (Rafael Gonzaga) [#54361](https://github.com/nodejs/node/pull/54361)
* \[[`994b46a160`](https://github.com/nodejs/node/commit/994b46a160)] - **doc**: correct peformance entry types (Jason Zhang) [#54263](https://github.com/nodejs/node/pull/54263)
* \[[`f142e668cb`](https://github.com/nodejs/node/commit/f142e668cb)] - **doc**: fix typo in method name in the sea doc (Eliyah Sundström) [#54027](https://github.com/nodejs/node/pull/54027)
* \[[`9529a30dba`](https://github.com/nodejs/node/commit/9529a30dba)] - **doc**: mark process.nextTick legacy (Marco Ippolito) [#51280](https://github.com/nodejs/node/pull/51280)
* \[[`7e25fabb91`](https://github.com/nodejs/node/commit/7e25fabb91)] - **doc**: add esm examples to node:http2 (Alfredo González) [#54292](https://github.com/nodejs/node/pull/54292)
* \[[`6a4f05e384`](https://github.com/nodejs/node/commit/6a4f05e384)] - **doc**: explicitly mention node:fs module restriction (Rafael Gonzaga) [#54269](https://github.com/nodejs/node/pull/54269)
* \[[`53f5c54997`](https://github.com/nodejs/node/commit/53f5c54997)] - **doc**: warn for windows build bug (Jason Zhang) [#54217](https://github.com/nodejs/node/pull/54217)
* \[[`07bde054f3`](https://github.com/nodejs/node/commit/07bde054f3)] - **doc**: make some parameters optional in `tracingChannel.traceCallback` (Deokjin Kim) [#54068](https://github.com/nodejs/node/pull/54068)
* \[[`62bf03b5f1`](https://github.com/nodejs/node/commit/62bf03b5f1)] - **doc**: add esm examples to node:dns (Alfredo González) [#54172](https://github.com/nodejs/node/pull/54172)
* \[[`fb2b19184b`](https://github.com/nodejs/node/commit/fb2b19184b)] - **doc**: add KevinEady as a triager (Chengzhong Wu) [#54179](https://github.com/nodejs/node/pull/54179)
* \[[`24976bfba0`](https://github.com/nodejs/node/commit/24976bfba0)] - **doc**: add esm examples to node:console (Alfredo González) [#54108](https://github.com/nodejs/node/pull/54108)
* \[[`4e7edc40f7`](https://github.com/nodejs/node/commit/4e7edc40f7)] - **doc**: fix sea assets example (Sadzurami) [#54192](https://github.com/nodejs/node/pull/54192)
* \[[`322b5d91e1`](https://github.com/nodejs/node/commit/322b5d91e1)] - **doc**: add links to security steward companies (Aviv Keller) [#52981](https://github.com/nodejs/node/pull/52981)
* \[[`6ab271510e`](https://github.com/nodejs/node/commit/6ab271510e)] - **doc**: move `onread` option from `socket.connect()` to `new net.socket()` (sendoru) [#54194](https://github.com/nodejs/node/pull/54194)
* \[[`39c30ea08f`](https://github.com/nodejs/node/commit/39c30ea08f)] - **doc**: move release key for Myles Borins (Richard Lau) [#54059](https://github.com/nodejs/node/pull/54059)
* \[[`e9fc54804a`](https://github.com/nodejs/node/commit/e9fc54804a)] - **doc**: refresh instructions for building node from source (Liran Tal) [#53768](https://github.com/nodejs/node/pull/53768)
* \[[`f131dc625a`](https://github.com/nodejs/node/commit/f131dc625a)] - **doc**: add documentation for blob.bytes() method (jaexxin) [#54114](https://github.com/nodejs/node/pull/54114)
* \[[`8d41bb900b`](https://github.com/nodejs/node/commit/8d41bb900b)] - **doc**: add missing new lines to custom test reporter examples (Eddie Abbondanzio) [#54152](https://github.com/nodejs/node/pull/54152)
* \[[`2acaeaba77`](https://github.com/nodejs/node/commit/2acaeaba77)] - **doc**: update list of Triagers on the `README.md` (Antoine du Hamel) [#54138](https://github.com/nodejs/node/pull/54138)
* \[[`fff8eb2792`](https://github.com/nodejs/node/commit/fff8eb2792)] - **doc**: expand troubleshooting section (Liran Tal) [#53808](https://github.com/nodejs/node/pull/53808)
* \[[`402121520f`](https://github.com/nodejs/node/commit/402121520f)] - **doc**: clarify `useCodeCache` setting for cross-platform SEA generation (Yelim Koo) [#53994](https://github.com/nodejs/node/pull/53994)
* \[[`272484b8b2`](https://github.com/nodejs/node/commit/272484b8b2)] - **doc**: test for cli options (Aras Abbasi) [#51623](https://github.com/nodejs/node/pull/51623)
* \[[`c4d0ca4710`](https://github.com/nodejs/node/commit/c4d0ca4710)] - **doc, build**: fixup build docs (Aviv Keller) [#54899](https://github.com/nodejs/node/pull/54899)
* \[[`2e3e17748b`](https://github.com/nodejs/node/commit/2e3e17748b)] - **doc, child\_process**: add esm snippets (Aviv Keller) [#53616](https://github.com/nodejs/node/pull/53616)
* \[[`c40b4b4f27`](https://github.com/nodejs/node/commit/c40b4b4f27)] - **doc, meta**: fix broken link in `onboarding.md` (Aviv Keller) [#54886](https://github.com/nodejs/node/pull/54886)
* \[[`beff587b94`](https://github.com/nodejs/node/commit/beff587b94)] - **doc, meta**: add missing `,` to `BUILDING.md` (Aviv Keller) [#54409](https://github.com/nodejs/node/pull/54409)
* \[[`c114585430`](https://github.com/nodejs/node/commit/c114585430)] - **doc, meta**: replace command with link to keys (Aviv Keller) [#53745](https://github.com/nodejs/node/pull/53745)
* \[[`0843077a99`](https://github.com/nodejs/node/commit/0843077a99)] - **doc, test**: simplify test README table (Aviv Keller) [#53971](https://github.com/nodejs/node/pull/53971)
* \[[`2df7bc0e32`](https://github.com/nodejs/node/commit/2df7bc0e32)] - **doc,tools**: enforce use of `node:` prefix (Antoine du Hamel) [#53950](https://github.com/nodejs/node/pull/53950)
* \[[`0dd4639391`](https://github.com/nodejs/node/commit/0dd4639391)] - **esm**: fix support for `URL` instances in `import.meta.resolve` (Antoine du Hamel) [#54690](https://github.com/nodejs/node/pull/54690)
* \[[`f0c55e206d`](https://github.com/nodejs/node/commit/f0c55e206d)] - **fs**: refactor rimraf to avoid using primordials (Yagiz Nizipli) [#54834](https://github.com/nodejs/node/pull/54834)
* \[[`f568384bbd`](https://github.com/nodejs/node/commit/f568384bbd)] - **fs**: refactor handleTimestampsAndMode to remove redundant call (HEESEUNG) [#54369](https://github.com/nodejs/node/pull/54369)
* \[[`2fb7cc9715`](https://github.com/nodejs/node/commit/2fb7cc9715)] - **fs**: fix typings (Yagiz Nizipli) [#53626](https://github.com/nodejs/node/pull/53626)
* \[[`596940cfa0`](https://github.com/nodejs/node/commit/596940cfa0)] - **http**: reduce likelihood of race conditions on keep-alive timeout (jazelly) [#54863](https://github.com/nodejs/node/pull/54863)
* \[[`6e13a7ba02`](https://github.com/nodejs/node/commit/6e13a7ba02)] - **http**: remove prototype primordials (Antoine du Hamel) [#53698](https://github.com/nodejs/node/pull/53698)
* \[[`99f96eb3f7`](https://github.com/nodejs/node/commit/99f96eb3f7)] - **http2**: remove prototype primordials (Antoine du Hamel) [#53696](https://github.com/nodejs/node/pull/53696)
* \[[`41f5eacc1a`](https://github.com/nodejs/node/commit/41f5eacc1a)] - **https**: only use default ALPNProtocols when appropriate (Brian White) [#54411](https://github.com/nodejs/node/pull/54411)
* \[[`59a39520e1`](https://github.com/nodejs/node/commit/59a39520e1)] - **(SEMVER-MINOR)** **inspector**: support `Network.loadingFailed` event (Kohei Ueno) [#54246](https://github.com/nodejs/node/pull/54246)
* \[[`d1007fb1a9`](https://github.com/nodejs/node/commit/d1007fb1a9)] - **inspector**: provide detailed info to fix DevTools frontend errors (Kohei Ueno) [#54156](https://github.com/nodejs/node/pull/54156)
* \[[`3b93507949`](https://github.com/nodejs/node/commit/3b93507949)] - **(SEMVER-MINOR)** **inspector**: add initial support for network inspection (Kohei Ueno) [#53593](https://github.com/nodejs/node/pull/53593)
* \[[`fc37b801c8`](https://github.com/nodejs/node/commit/fc37b801c8)] - **lib**: remove unnecessary async (jakecastelli) [#54829](https://github.com/nodejs/node/pull/54829)
* \[[`d86f24787b`](https://github.com/nodejs/node/commit/d86f24787b)] - **lib**: make WeakRef safe in abort\_controller (jazelly) [#54791](https://github.com/nodejs/node/pull/54791)
* \[[`77c59224e5`](https://github.com/nodejs/node/commit/77c59224e5)] - **lib**: add note about removing `node:sys` module (Rafael Gonzaga) [#54743](https://github.com/nodejs/node/pull/54743)
* \[[`b8c06dce02`](https://github.com/nodejs/node/commit/b8c06dce02)] - **lib**: ensure no holey array in fixed\_queue (Jason Zhang) [#54537](https://github.com/nodejs/node/pull/54537)
* \[[`b85c8ce1fc`](https://github.com/nodejs/node/commit/b85c8ce1fc)] - **lib**: refactor SubtleCrypto experimental warnings (Filip Skokan) [#54620](https://github.com/nodejs/node/pull/54620)
* \[[`e84812c1b5`](https://github.com/nodejs/node/commit/e84812c1b5)] - **lib**: respect terminal capabilities on styleText (Rafael Gonzaga) [#54389](https://github.com/nodejs/node/pull/54389)
* \[[`c004abaf17`](https://github.com/nodejs/node/commit/c004abaf17)] - **lib**: replace spread operator with primordials function (YoonSoo\_Shin) [#54053](https://github.com/nodejs/node/pull/54053)
* \[[`b79aeabc4d`](https://github.com/nodejs/node/commit/b79aeabc4d)] - **lib**: avoid for of loop and remove unnecessary variable in zlib (YoonSoo\_Shin) [#54258](https://github.com/nodejs/node/pull/54258)
* \[[`f4085363c6`](https://github.com/nodejs/node/commit/f4085363c6)] - **lib**: fix unhandled errors in webstream adapters (Fedor Indutny) [#54206](https://github.com/nodejs/node/pull/54206)
* \[[`1ad857e748`](https://github.com/nodejs/node/commit/1ad857e748)] - **lib**: fix typos in comments within internal/streams (YoonSoo\_Shin) [#54093](https://github.com/nodejs/node/pull/54093)
* \[[`02b36cbd2d`](https://github.com/nodejs/node/commit/02b36cbd2d)] - **(SEMVER-MINOR)** **lib**: add EventSource Client (Aras Abbasi) [#51575](https://github.com/nodejs/node/pull/51575)
* \[[`afbf2c0530`](https://github.com/nodejs/node/commit/afbf2c0530)] - **lib,permission**: support Buffer to permission.has (Rafael Gonzaga) [#54104](https://github.com/nodejs/node/pull/54104)
* \[[`54af47395d`](https://github.com/nodejs/node/commit/54af47395d)] - **meta**: bump peter-evans/create-pull-request from 6.1.0 to 7.0.1 (dependabot\[bot]) [#54820](https://github.com/nodejs/node/pull/54820)
* \[[`a0c10f2ed9`](https://github.com/nodejs/node/commit/a0c10f2ed9)] - **meta**: add `Windows ARM64` to flaky-tests list (Aviv Keller) [#54693](https://github.com/nodejs/node/pull/54693)
* \[[`27b06880e1`](https://github.com/nodejs/node/commit/27b06880e1)] - **meta**: bump actions/setup-python from 5.1.1 to 5.2.0 (Rich Trott) [#54691](https://github.com/nodejs/node/pull/54691)
* \[[`8747af1037`](https://github.com/nodejs/node/commit/8747af1037)] - **meta**: update sccache to v0.8.1 (Aviv Keller) [#54720](https://github.com/nodejs/node/pull/54720)
* \[[`3f753d87a6`](https://github.com/nodejs/node/commit/3f753d87a6)] - **meta**: bump step-security/harden-runner from 2.9.0 to 2.9.1 (dependabot\[bot]) [#54704](https://github.com/nodejs/node/pull/54704)
* \[[`6f103ae25d`](https://github.com/nodejs/node/commit/6f103ae25d)] - **meta**: bump actions/upload-artifact from 4.3.4 to 4.4.0 (dependabot\[bot]) [#54703](https://github.com/nodejs/node/pull/54703)
* \[[`3e6a9bb04e`](https://github.com/nodejs/node/commit/3e6a9bb04e)] - **meta**: bump github/codeql-action from 3.25.15 to 3.26.6 (dependabot\[bot]) [#54702](https://github.com/nodejs/node/pull/54702)
* \[[`c666ebc4e4`](https://github.com/nodejs/node/commit/c666ebc4e4)] - **meta**: fix links in `SECURITY.md` (Aviv Keller) [#54696](https://github.com/nodejs/node/pull/54696)
* \[[`4d361b3bed`](https://github.com/nodejs/node/commit/4d361b3bed)] - **meta**: fix `contributing` codeowners (Aviv Keller) [#54641](https://github.com/nodejs/node/pull/54641)
* \[[`36931aa183`](https://github.com/nodejs/node/commit/36931aa183)] - **meta**: remind users to use a supported version in bug reports (Aviv Keller) [#54481](https://github.com/nodejs/node/pull/54481)
* \[[`cf283d9ca7`](https://github.com/nodejs/node/commit/cf283d9ca7)] - **meta**: run coverage-windows when `vcbuild.bat` updated (Aviv Keller) [#54412](https://github.com/nodejs/node/pull/54412)
* \[[`67ca397c9f`](https://github.com/nodejs/node/commit/67ca397c9f)] - **meta**: add test-permission-\* CODEOWNERS (Rafael Gonzaga) [#54267](https://github.com/nodejs/node/pull/54267)
* \[[`b61a2f5b79`](https://github.com/nodejs/node/commit/b61a2f5b79)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#54210](https://github.com/nodejs/node/pull/54210)
* \[[`dd8ab83667`](https://github.com/nodejs/node/commit/dd8ab83667)] - **meta**: add module label for the lib/internal/modules folder (Aviv Keller) [#52858](https://github.com/nodejs/node/pull/52858)
* \[[`db78978d17`](https://github.com/nodejs/node/commit/db78978d17)] - **meta**: bump `actions/upload-artifact` from 4.3.3 to 4.3.4 (dependabot\[bot]) [#54166](https://github.com/nodejs/node/pull/54166)
* \[[`ca808dd9e5`](https://github.com/nodejs/node/commit/ca808dd9e5)] - **meta**: bump `actions/download-artifact` from 4.1.7 to 4.1.8 (dependabot\[bot]) [#54167](https://github.com/nodejs/node/pull/54167)
* \[[`a35d980146`](https://github.com/nodejs/node/commit/a35d980146)] - **meta**: bump actions/setup-python from 5.1.0 to 5.1.1 (dependabot\[bot]) [#54165](https://github.com/nodejs/node/pull/54165)
* \[[`3a103c3a17`](https://github.com/nodejs/node/commit/3a103c3a17)] - **meta**: bump `step-security/harden-runner` from 2.8.1 to 2.9.0 (dependabot\[bot]) [#54169](https://github.com/nodejs/node/pull/54169)
* \[[`775ebbe0e8`](https://github.com/nodejs/node/commit/775ebbe0e8)] - **meta**: bump `actions/setup-node` from 4.0.2 to 4.0.3 (dependabot\[bot]) [#54170](https://github.com/nodejs/node/pull/54170)
* \[[`7d5dd6f1d1`](https://github.com/nodejs/node/commit/7d5dd6f1d1)] - **meta**: bump `github/codeql-action` from 3.25.11 to 3.25.15 (dependabot\[bot]) [#54168](https://github.com/nodejs/node/pull/54168)
* \[[`80dd38dde3`](https://github.com/nodejs/node/commit/80dd38dde3)] - **meta**: bump `ossf/scorecard-action` from 2.3.3 to 2.4.0 (dependabot\[bot]) [#54171](https://github.com/nodejs/node/pull/54171)
* \[[`90b632ee02`](https://github.com/nodejs/node/commit/90b632ee02)] - **module**: warn on detection in typeless package (Geoffrey Booth) [#52168](https://github.com/nodejs/node/pull/52168)
* \[[`3011927aab`](https://github.com/nodejs/node/commit/3011927aab)] - **node-api**: add external buffer creation benchmark (Chengzhong Wu) [#54877](https://github.com/nodejs/node/pull/54877)
* \[[`7611093e11`](https://github.com/nodejs/node/commit/7611093e11)] - **node-api**: add support for UTF-8 and Latin-1 property keys (Mert Can Altin) [#52984](https://github.com/nodejs/node/pull/52984)
* \[[`d65a8f377c`](https://github.com/nodejs/node/commit/d65a8f377c)] - **node-api**: remove RefBase and CallbackWrapper (Vladimir Morozov) [#53590](https://github.com/nodejs/node/pull/53590)
* \[[`309cb1cbd2`](https://github.com/nodejs/node/commit/309cb1cbd2)] - **path**: remove `StringPrototypeCharCodeAt` from `posix.extname` (Aviv Keller) [#54546](https://github.com/nodejs/node/pull/54546)
* \[[`2859b4ba9a`](https://github.com/nodejs/node/commit/2859b4ba9a)] - **path**: change `posix.join` to use array (Wiyeong Seo) [#54331](https://github.com/nodejs/node/pull/54331)
* \[[`c61cee2138`](https://github.com/nodejs/node/commit/c61cee2138)] - **path**: fix relative on Windows (Hüseyin Açacak) [#53991](https://github.com/nodejs/node/pull/53991)
* \[[`329be5cc35`](https://github.com/nodejs/node/commit/329be5cc35)] - **path**: use the correct name in `validateString` (Benjamin Pasero) [#53669](https://github.com/nodejs/node/pull/53669)
* \[[`a9837267cb`](https://github.com/nodejs/node/commit/a9837267cb)] - **repl**: avoid interpreting 'npm' as a command when errors are recoverable (Shima Ryuhei) [#54848](https://github.com/nodejs/node/pull/54848)
* \[[`d6a2317961`](https://github.com/nodejs/node/commit/d6a2317961)] - **repl**: doc-deprecate instantiating `node:repl` classes without `new` (Aviv Keller) [#54842](https://github.com/nodejs/node/pull/54842)
* \[[`7f09d983f3`](https://github.com/nodejs/node/commit/7f09d983f3)] - **sea**: don't set code cache flags when snapshot is used (Joyee Cheung) [#54120](https://github.com/nodejs/node/pull/54120)
* \[[`85542b094c`](https://github.com/nodejs/node/commit/85542b094c)] - **src**: add Cleanable class to Environment (Gabriel Schulhof) [#54880](https://github.com/nodejs/node/pull/54880)
* \[[`8422064127`](https://github.com/nodejs/node/commit/8422064127)] - **src**: remove redundant AESCipherMode (Tobias Nießen) [#54438](https://github.com/nodejs/node/pull/54438)
* \[[`342c32483a`](https://github.com/nodejs/node/commit/342c32483a)] - **src**: handle errors correctly in `permission.cc` (Michaël Zasso) [#54541](https://github.com/nodejs/node/pull/54541)
* \[[`90ff714699`](https://github.com/nodejs/node/commit/90ff714699)] - **src**: return `v8::Object` from error constructors (Michaël Zasso) [#54541](https://github.com/nodejs/node/pull/54541)
* \[[`872856cfcb`](https://github.com/nodejs/node/commit/872856cfcb)] - **src**: improve `buffer.transcode` performance (Yagiz Nizipli) [#54153](https://github.com/nodejs/node/pull/54153)
* \[[`91936ebd12`](https://github.com/nodejs/node/commit/91936ebd12)] - **src**: skip inspector wait in internal workers (Chengzhong Wu) [#54219](https://github.com/nodejs/node/pull/54219)
* \[[`9759049427`](https://github.com/nodejs/node/commit/9759049427)] - **src**: account for OpenSSL unexpected version (Shelley Vohr) [#54038](https://github.com/nodejs/node/pull/54038)
* \[[`87167fa248`](https://github.com/nodejs/node/commit/87167fa248)] - **src**: use `args.This()` instead of `Holder` (Michaël Zasso) [#53474](https://github.com/nodejs/node/pull/53474)
* \[[`b05c56e4be`](https://github.com/nodejs/node/commit/b05c56e4be)] - **src**: simplify `size() == 0` checks (Yagiz Nizipli) [#53440](https://github.com/nodejs/node/pull/53440)
* \[[`d53e53699c`](https://github.com/nodejs/node/commit/d53e53699c)] - **src**: fix execArgv in worker (theanarkh) [#53029](https://github.com/nodejs/node/pull/53029)
* \[[`21776a34b5`](https://github.com/nodejs/node/commit/21776a34b5)] - **src**: make sure pass the `argv` to worker threads (theanarkh) [#52827](https://github.com/nodejs/node/pull/52827)
* \[[`3aaae68ec8`](https://github.com/nodejs/node/commit/3aaae68ec8)] - **(SEMVER-MINOR)** **src,lib**: add performance.uvMetricsInfo (Rafael Gonzaga) [#54413](https://github.com/nodejs/node/pull/54413)
* \[[`ef1c0d7def`](https://github.com/nodejs/node/commit/ef1c0d7def)] - **src,permission**: handle process.chdir on pm (Rafael Gonzaga) [#53175](https://github.com/nodejs/node/pull/53175)
* \[[`0c32918eef`](https://github.com/nodejs/node/commit/0c32918eef)] - **stream**: change stream to use index instead of `for...of` (Wiyeong Seo) [#54474](https://github.com/nodejs/node/pull/54474)
* \[[`337cd412b5`](https://github.com/nodejs/node/commit/337cd412b5)] - **stream**: make checking pendingcb on WritableStream backward compatible (jakecastelli) [#54142](https://github.com/nodejs/node/pull/54142)
* \[[`713fc0c9eb`](https://github.com/nodejs/node/commit/713fc0c9eb)] - **stream**: throw TypeError when criteria fulfilled in getIterator (jakecastelli) [#53825](https://github.com/nodejs/node/pull/53825)
* \[[`9686153616`](https://github.com/nodejs/node/commit/9686153616)] - **stream**: fix util.inspect for compression/decompressionStream (Mert Can Altin) [#52283](https://github.com/nodejs/node/pull/52283)
* \[[`76110b0b43`](https://github.com/nodejs/node/commit/76110b0b43)] - **test**: adjust test-tls-junk-server for OpenSSL32 (Michael Dawson) [#54926](https://github.com/nodejs/node/pull/54926)
* \[[`4092889371`](https://github.com/nodejs/node/commit/4092889371)] - **test**: adjust tls test for OpenSSL32 (Michael Dawson) [#54909](https://github.com/nodejs/node/pull/54909)
* \[[`5d48543a16`](https://github.com/nodejs/node/commit/5d48543a16)] - **test**: fix test-http2-socket-close.js (Hüseyin Açacak) [#54900](https://github.com/nodejs/node/pull/54900)
* \[[`8048c2eaed`](https://github.com/nodejs/node/commit/8048c2eaed)] - **test**: improve test-internal-fs-syncwritestream (Sunghoon) [#54671](https://github.com/nodejs/node/pull/54671)
* \[[`597bc14c90`](https://github.com/nodejs/node/commit/597bc14c90)] - **test**: deflake test-dns (Luigi Pinca) [#54902](https://github.com/nodejs/node/pull/54902)
* \[[`a9fc8d9cfa`](https://github.com/nodejs/node/commit/a9fc8d9cfa)] - **test**: fix test test-tls-dhe for OpenSSL32 (Michael Dawson) [#54903](https://github.com/nodejs/node/pull/54903)
* \[[`1b3b4f4a9f`](https://github.com/nodejs/node/commit/1b3b4f4a9f)] - **test**: use correct file naming syntax for `util-parse-env` (Aviv Keller) [#53705](https://github.com/nodejs/node/pull/53705)
* \[[`9db46b5ea3`](https://github.com/nodejs/node/commit/9db46b5ea3)] - **test**: add missing await (Luigi Pinca) [#54828](https://github.com/nodejs/node/pull/54828)
* \[[`124f715679`](https://github.com/nodejs/node/commit/124f715679)] - **test**: move more url tests to `node:test` (Yagiz Nizipli) [#54636](https://github.com/nodejs/node/pull/54636)
* \[[`d2ec96150a`](https://github.com/nodejs/node/commit/d2ec96150a)] - **test**: strip color chars in `test-runner-run` (Giovanni Bucci) [#54552](https://github.com/nodejs/node/pull/54552)
* \[[`747d9ae72e`](https://github.com/nodejs/node/commit/747d9ae72e)] - **test**: deflake test-http2-misbehaving-multiplex (Luigi Pinca) [#54872](https://github.com/nodejs/node/pull/54872)
* \[[`7b7687eadc`](https://github.com/nodejs/node/commit/7b7687eadc)] - **test**: remove dead code in test-http2-misbehaving-multiplex (Luigi Pinca) [#54860](https://github.com/nodejs/node/pull/54860)
* \[[`60f5f5426d`](https://github.com/nodejs/node/commit/60f5f5426d)] - **test**: reduce test-esm-loader-hooks-inspect-wait flakiness (Luigi Pinca) [#54827](https://github.com/nodejs/node/pull/54827)
* \[[`f5e77385c5`](https://github.com/nodejs/node/commit/f5e77385c5)] - **test**: reduce the allocation size in test-worker-arraybuffer-zerofill (James M Snell) [#54839](https://github.com/nodejs/node/pull/54839)
* \[[`f26cf09d6b`](https://github.com/nodejs/node/commit/f26cf09d6b)] - **test**: fix test-tls-client-mindhsize for OpenSSL32 (Michael Dawson) [#54739](https://github.com/nodejs/node/pull/54739)
* \[[`c6f9afec94`](https://github.com/nodejs/node/commit/c6f9afec94)] - **test**: use platform timeout (jakecastelli) [#54591](https://github.com/nodejs/node/pull/54591)
* \[[`8f49b7c3ee`](https://github.com/nodejs/node/commit/8f49b7c3ee)] - **test**: reduce fs calls in test-fs-existssync-false (Yagiz Nizipli) [#54815](https://github.com/nodejs/node/pull/54815)
* \[[`e2c69c9844`](https://github.com/nodejs/node/commit/e2c69c9844)] - **test**: move test-http-server-request-timeouts-mixed (James M Snell) [#54841](https://github.com/nodejs/node/pull/54841)
* \[[`f7af8ca021`](https://github.com/nodejs/node/commit/f7af8ca021)] - **test**: fix volatile for CauseSegfault with clang (Ivan Trubach) [#54325](https://github.com/nodejs/node/pull/54325)
* \[[`d1bae5ede5`](https://github.com/nodejs/node/commit/d1bae5ede5)] - **test**: set `test-worker-arraybuffer-zerofill` as flaky (Yagiz Nizipli) [#54802](https://github.com/nodejs/node/pull/54802)
* \[[`b5b5cc811f`](https://github.com/nodejs/node/commit/b5b5cc811f)] - **test**: set `test-http-server-request-timeouts-mixed` as flaky (Yagiz Nizipli) [#54802](https://github.com/nodejs/node/pull/54802)
* \[[`9808feecac`](https://github.com/nodejs/node/commit/9808feecac)] - **test**: set `test-single-executable-application-empty` as flaky (Yagiz Nizipli) [#54802](https://github.com/nodejs/node/pull/54802)
* \[[`97d41c62e3`](https://github.com/nodejs/node/commit/97d41c62e3)] - **test**: set `test-macos-app-sandbox` as flaky (Yagiz Nizipli) [#54802](https://github.com/nodejs/node/pull/54802)
* \[[`57ae68001c`](https://github.com/nodejs/node/commit/57ae68001c)] - **test**: set `test-fs-utimes` as flaky (Yagiz Nizipli) [#54802](https://github.com/nodejs/node/pull/54802)
* \[[`38afc4da03`](https://github.com/nodejs/node/commit/38afc4da03)] - **test**: set `test-runner-run-watch` as flaky (Yagiz Nizipli) [#54802](https://github.com/nodejs/node/pull/54802)
* \[[`68e19748a6`](https://github.com/nodejs/node/commit/68e19748a6)] - **test**: set `test-writewrap` as flaky (Yagiz Nizipli) [#54802](https://github.com/nodejs/node/pull/54802)
* \[[`e8cb03d530`](https://github.com/nodejs/node/commit/e8cb03d530)] - **test**: set `test-async-context-frame` as flaky (Yagiz Nizipli) [#54802](https://github.com/nodejs/node/pull/54802)
* \[[`3a56517220`](https://github.com/nodejs/node/commit/3a56517220)] - **test**: set `test-esm-loader-hooks-inspect-wait` as flaky (Yagiz Nizipli) [#54802](https://github.com/nodejs/node/pull/54802)
* \[[`c98cd1227d`](https://github.com/nodejs/node/commit/c98cd1227d)] - **test**: set `test-http2-large-file` as flaky (Yagiz Nizipli) [#54802](https://github.com/nodejs/node/pull/54802)
* \[[`16176a6323`](https://github.com/nodejs/node/commit/16176a6323)] - **test**: set `test-runner-watch-mode-complex` as flaky (Yagiz Nizipli) [#54802](https://github.com/nodejs/node/pull/54802)
* \[[`eed0537533`](https://github.com/nodejs/node/commit/eed0537533)] - **test**: set `test-performance-function` as flaky (Yagiz Nizipli) [#54802](https://github.com/nodejs/node/pull/54802)
* \[[`d0f208d2e9`](https://github.com/nodejs/node/commit/d0f208d2e9)] - **test**: set `test-debugger-heap-profiler` as flaky (Yagiz Nizipli) [#54802](https://github.com/nodejs/node/pull/54802)
* \[[`68891a6363`](https://github.com/nodejs/node/commit/68891a6363)] - **test**: fix `test-process-load-env-file` when path contains `'` (Antoine du Hamel) [#54511](https://github.com/nodejs/node/pull/54511)
* \[[`4f82673139`](https://github.com/nodejs/node/commit/4f82673139)] - **test**: refactor fs-watch tests due to macOS issue (Santiago Gimeno) [#54498](https://github.com/nodejs/node/pull/54498)
* \[[`3606c53fdc`](https://github.com/nodejs/node/commit/3606c53fdc)] - **test**: refactor `test-esm-type-field-errors` (Giovanni Bucci) [#54368](https://github.com/nodejs/node/pull/54368)
* \[[`99566aea97`](https://github.com/nodejs/node/commit/99566aea97)] - **test**: improve output of child process utilities (Joyee Cheung) [#54622](https://github.com/nodejs/node/pull/54622)
* \[[`ed2377c1a1`](https://github.com/nodejs/node/commit/ed2377c1a1)] - **test**: fix test-tls-client-auth test for OpenSSL32 (Michael Dawson) [#54610](https://github.com/nodejs/node/pull/54610)
* \[[`d2a7e45946`](https://github.com/nodejs/node/commit/d2a7e45946)] - **test**: update TLS test for OpenSSL 3.2 (Richard Lau) [#54612](https://github.com/nodejs/node/pull/54612)
* \[[`a50bbca78a`](https://github.com/nodejs/node/commit/a50bbca78a)] - **test**: increase key size for ca2-cert.pem (Michael Dawson) [#54599](https://github.com/nodejs/node/pull/54599)
* \[[`d7ac3262de`](https://github.com/nodejs/node/commit/d7ac3262de)] - **test**: update test-assert-typedarray-deepequal to use node:test (James M Snell) [#54585](https://github.com/nodejs/node/pull/54585)
* \[[`916a73cd8f`](https://github.com/nodejs/node/commit/916a73cd8f)] - **test**: update test-assert to use node:test (James M Snell) [#54585](https://github.com/nodejs/node/pull/54585)
* \[[`10bea1cef5`](https://github.com/nodejs/node/commit/10bea1cef5)] - **test**: merge ongc and gcutil into gc.js (tannal) [#54355](https://github.com/nodejs/node/pull/54355)
* \[[`f145982436`](https://github.com/nodejs/node/commit/f145982436)] - **test**: move a couple of tests over to using node:test (James M Snell) [#54582](https://github.com/nodejs/node/pull/54582)
* \[[`229e102d20`](https://github.com/nodejs/node/commit/229e102d20)] - **test**: fix embedding test for Windows (Vladimir Morozov) [#53659](https://github.com/nodejs/node/pull/53659)
* \[[`fcf82adef0`](https://github.com/nodejs/node/commit/fcf82adef0)] - **test**: use relative paths in test-cli-permission tests (sendoru) [#54188](https://github.com/nodejs/node/pull/54188)
* \[[`4c219b0235`](https://github.com/nodejs/node/commit/4c219b0235)] - **test**: fix timeout not being cleared (Isaac-yz-Liu) [#54242](https://github.com/nodejs/node/pull/54242)
* \[[`e446517a41`](https://github.com/nodejs/node/commit/e446517a41)] - **test**: refactor `test-runner-module-mocking` (Antoine du Hamel) [#54233](https://github.com/nodejs/node/pull/54233)
* \[[`782a6a05ef`](https://github.com/nodejs/node/commit/782a6a05ef)] - **test**: use assert.{s,deepS}trictEqual() (Luigi Pinca) [#54208](https://github.com/nodejs/node/pull/54208)
* \[[`d478db7adc`](https://github.com/nodejs/node/commit/d478db7adc)] - **test**: set test-structuredclone-jstransferable non-flaky (Stefan Stojanovic) [#54115](https://github.com/nodejs/node/pull/54115)
* \[[`c8587ec90d`](https://github.com/nodejs/node/commit/c8587ec90d)] - **test**: update wpt test for streams (devstone) [#54129](https://github.com/nodejs/node/pull/54129)
* \[[`dbc26c2971`](https://github.com/nodejs/node/commit/dbc26c2971)] - **test**: fix typo in test (Sonny) [#54137](https://github.com/nodejs/node/pull/54137)
* \[[`17b7ec4df3`](https://github.com/nodejs/node/commit/17b7ec4df3)] - **test**: add initial pull delay and prototype pollution prevention tests (Sonny) [#54061](https://github.com/nodejs/node/pull/54061)
* \[[`931ff4367a`](https://github.com/nodejs/node/commit/931ff4367a)] - **test**: update wpt test (Mert Can Altin) [#53814](https://github.com/nodejs/node/pull/53814)
* \[[`1c1bd7ce52`](https://github.com/nodejs/node/commit/1c1bd7ce52)] - **test**: update `url` web-platform tests (Yagiz Nizipli) [#53472](https://github.com/nodejs/node/pull/53472)
* \[[`b048eaea5c`](https://github.com/nodejs/node/commit/b048eaea5c)] - **test\_runner**: reimplement `assert.ok` to allow stack parsing (Aviv Keller) [#54776](https://github.com/nodejs/node/pull/54776)
* \[[`c981e61155`](https://github.com/nodejs/node/commit/c981e61155)] - **test\_runner**: improve code coverage cleanup (Colin Ihrig) [#54856](https://github.com/nodejs/node/pull/54856)
* \[[`4f421b37da`](https://github.com/nodejs/node/commit/4f421b37da)] - **test\_runner**: use validateStringArray for `timers.enable()` (Deokjin Kim) [#49534](https://github.com/nodejs/node/pull/49534)
* \[[`27da75ae22`](https://github.com/nodejs/node/commit/27da75ae22)] - **test\_runner**: do not expose internal loader (Antoine du Hamel) [#54106](https://github.com/nodejs/node/pull/54106)
* \[[`56cbc80d28`](https://github.com/nodejs/node/commit/56cbc80d28)] - **test\_runner**: make mock\_loader not confuse CJS and ESM resolution (Sung Ye In) [#53846](https://github.com/nodejs/node/pull/53846)
* \[[`8fd951f7c7`](https://github.com/nodejs/node/commit/8fd951f7c7)] - **test\_runner**: remove outdated comment (Colin Ihrig) [#54146](https://github.com/nodejs/node/pull/54146)
* \[[`65b6fec3ba`](https://github.com/nodejs/node/commit/65b6fec3ba)] - **test\_runner**: run after hooks even if test is aborted (Colin Ihrig) [#54151](https://github.com/nodejs/node/pull/54151)
* \[[`c0b4c8284c`](https://github.com/nodejs/node/commit/c0b4c8284c)] - **test\_runner**: added colors to dot reporter (Giovanni) [#53450](https://github.com/nodejs/node/pull/53450)
* \[[`3000e5df91`](https://github.com/nodejs/node/commit/3000e5df91)] - **test\_runner**: support module detection in module mocks (Geoffrey Booth) [#53642](https://github.com/nodejs/node/pull/53642)
* \[[`f789f4c92d`](https://github.com/nodejs/node/commit/f789f4c92d)] - **(SEMVER-MINOR)** **test\_runner**: support module mocking (Colin Ihrig) [#52848](https://github.com/nodejs/node/pull/52848)
* \[[`82d1c36f51`](https://github.com/nodejs/node/commit/82d1c36f51)] - **test\_runner**: display failed test stack trace with dot reporter (Mihir Bhansali) [#52655](https://github.com/nodejs/node/pull/52655)
* \[[`5358601e31`](https://github.com/nodejs/node/commit/5358601e31)] - **timers**: avoid generating holey internal arrays (Gürgün Dayıoğlu) [#54771](https://github.com/nodejs/node/pull/54771)
* \[[`b6ed97c66d`](https://github.com/nodejs/node/commit/b6ed97c66d)] - **timers**: document ref option for scheduler.wait (Paolo Insogna) [#54605](https://github.com/nodejs/node/pull/54605)
* \[[`f524b8a28b`](https://github.com/nodejs/node/commit/f524b8a28b)] - **timers**: fix validation (Paolo Insogna) [#54404](https://github.com/nodejs/node/pull/54404)
* \[[`bc020f7cb3`](https://github.com/nodejs/node/commit/bc020f7cb3)] - **(SEMVER-MINOR)** **tls**: add `allowPartialTrustChain` flag (Anna Henningsen) [#54790](https://github.com/nodejs/node/pull/54790)
* \[[`d0e6f9168e`](https://github.com/nodejs/node/commit/d0e6f9168e)] - **tls**: remove prototype primordials (Antoine du Hamel) [#53699](https://github.com/nodejs/node/pull/53699)
* \[[`f5c65d0be6`](https://github.com/nodejs/node/commit/f5c65d0be6)] - **tools**: add readability/fn\_size to filter (Rafael Gonzaga) [#54744](https://github.com/nodejs/node/pull/54744)
* \[[`a47bb9b2c2`](https://github.com/nodejs/node/commit/a47bb9b2c2)] - **tools**: add util scripts to land and rebase PRs (Antoine du Hamel) [#54656](https://github.com/nodejs/node/pull/54656)
* \[[`fe3155cefa`](https://github.com/nodejs/node/commit/fe3155cefa)] - **tools**: remove readability/fn\_size rule (Rafael Gonzaga) [#54663](https://github.com/nodejs/node/pull/54663)
* \[[`d6b9cc3acd`](https://github.com/nodejs/node/commit/d6b9cc3acd)] - **tools**: remove unused python files (Aviv Keller) [#53928](https://github.com/nodejs/node/pull/53928)
* \[[`b5fbe9609c`](https://github.com/nodejs/node/commit/b5fbe9609c)] - **tools**: remove header from c-ares license (Aviv Keller) [#54335](https://github.com/nodejs/node/pull/54335)
* \[[`a7fdc608c6`](https://github.com/nodejs/node/commit/a7fdc608c6)] - **tools**: add find pyenv path on windows (Marco Ippolito) [#54314](https://github.com/nodejs/node/pull/54314)
* \[[`f90688cd5b`](https://github.com/nodejs/node/commit/f90688cd5b)] - **tools**: make undici updater build wasm from src (Michael Dawson) [#54128](https://github.com/nodejs/node/pull/54128)
* \[[`a033dff2f2`](https://github.com/nodejs/node/commit/a033dff2f2)] - **tty**: initialize winSize array with values (Michaël Zasso) [#54281](https://github.com/nodejs/node/pull/54281)
* \[[`e635e0956c`](https://github.com/nodejs/node/commit/e635e0956c)] - **typings**: fix TypedArray to a global type (1ilsang) [#54063](https://github.com/nodejs/node/pull/54063)
* \[[`b5bf08f31e`](https://github.com/nodejs/node/commit/b5bf08f31e)] - **typings**: correct param type of `SafePromisePrototypeFinally` (Wuli) [#54727](https://github.com/nodejs/node/pull/54727)
* \[[`628ae4bde5`](https://github.com/nodejs/node/commit/628ae4bde5)] - **typings**: add util.styleText type definition (Rafael Gonzaga) [#54252](https://github.com/nodejs/node/pull/54252)
* \[[`cc37401ea5`](https://github.com/nodejs/node/commit/cc37401ea5)] - **typings**: add missing binding function `writeFileUtf8()` (Jungku Lee) [#54110](https://github.com/nodejs/node/pull/54110)
* \[[`728c3fd6f1`](https://github.com/nodejs/node/commit/728c3fd6f1)] - **url**: modify pathToFileURL to handle extended UNC path (Early Riser) [#54262](https://github.com/nodejs/node/pull/54262)
* \[[`b25563dfcb`](https://github.com/nodejs/node/commit/b25563dfcb)] - **url**: improve resolveObject with ObjectAssign (Early Riser) [#54092](https://github.com/nodejs/node/pull/54092)
* \[[`eededd1ca8`](https://github.com/nodejs/node/commit/eededd1ca8)] - **url**: make URL.parse enumerable (Filip Skokan) [#53720](https://github.com/nodejs/node/pull/53720)
* \[[`4eb0749b6c`](https://github.com/nodejs/node/commit/4eb0749b6c)] - **(SEMVER-MINOR)** **url**: implement parse method for safer URL parsing (Ali Hassan) [#52280](https://github.com/nodejs/node/pull/52280)
* \[[`9e1c2293bf`](https://github.com/nodejs/node/commit/9e1c2293bf)] - **vm**: harden module type checks (Chengzhong Wu) [#52162](https://github.com/nodejs/node/pull/52162)
* \[[`2d90340cb3`](https://github.com/nodejs/node/commit/2d90340cb3)] - **(SEMVER-MINOR)** **vm**: introduce vanilla contexts via vm.constants.DONT\_CONTEXTIFY (Joyee Cheung) [#54394](https://github.com/nodejs/node/pull/54394)
* \[[`4644d05ab5`](https://github.com/nodejs/node/commit/4644d05ab5)] - **zlib**: deprecate instantiating classes without new (Yagiz Nizipli) [#54708](https://github.com/nodejs/node/pull/54708)
* \[[`ecdf6dd444`](https://github.com/nodejs/node/commit/ecdf6dd444)] - **zlib**: simplify validators (Yagiz Nizipli) [#54442](https://github.com/nodejs/node/pull/54442)

<a id="20.17.0"></a>

## 2024-08-21, Version 20.17.0 'Iron' (LTS), @marco-ippolito

### module: support require()ing synchronous ESM graphs

This release adds `require()` support for synchronous ESM graphs under
the flag `--experimental-require-module`.

If `--experimental-require-module` is enabled, and the ECMAScript
module being loaded by `require()` meets the following requirements:

* Explicitly marked as an ES module with a "type": "module" field in the closest package.json or a .mjs extension.
* Fully synchronous (contains no top-level await).

`require()` will load the requested module as an ES Module, and return
the module name space object. In this case it is similar to dynamic
`import()` but is run synchronously and returns the name space object
directly.

Contributed by Joyee Cheung in [#51977](https://github.com/nodejs/node/pull/51977)

### path: add `matchesGlob` method

Glob patterns can now be tested against individual paths via the `path.matchesGlob(path, pattern)` method.

Contributed by Aviv Keller in [#52881](https://github.com/nodejs/node/pull/52881)

### stream: expose DuplexPair API

The function `duplexPair` returns an array with two items,
each being a `Duplex` stream connected to the other side:

```js
const [ sideA, sideB ] = duplexPair();
```

Whatever is written to one stream is made readable on the other. It provides
behavior analogous to a network connection, where the data written by the client
becomes readable by the server, and vice-versa.

Contributed by Austin Wright in [#34111](https://github.com/nodejs/node/pull/34111)

### Other Notable Changes

* \[[`8e64c02b19`](https://github.com/nodejs/node/commit/8e64c02b19)] - **(SEMVER-MINOR)** **http**: add diagnostics channel `http.client.request.error` (Kohei Ueno) [#54054](https://github.com/nodejs/node/pull/54054)
* \[[`ae30674991`](https://github.com/nodejs/node/commit/ae30674991)] - **meta**: add jake to collaborators (jakecastelli) [#54004](https://github.com/nodejs/node/pull/54004)
* \[[`4a3ecbfc9b`](https://github.com/nodejs/node/commit/4a3ecbfc9b)] - **(SEMVER-MINOR)** **stream**: implement `min` option for `ReadableStreamBYOBReader.read` (Mattias Buelens) [#50888](https://github.com/nodejs/node/pull/50888)

### Commits

* \[[`b3a2726cbc`](https://github.com/nodejs/node/commit/b3a2726cbc)] - **assert**: use isError instead of instanceof in innerOk (Pietro Marchini) [#53980](https://github.com/nodejs/node/pull/53980)
* \[[`c7e4c3daf4`](https://github.com/nodejs/node/commit/c7e4c3daf4)] - **benchmark**: add cpSync benchmark (Yagiz Nizipli) [#53612](https://github.com/nodejs/node/pull/53612)
* \[[`a52de8c5ff`](https://github.com/nodejs/node/commit/a52de8c5ff)] - **bootstrap**: print `--help` message using `console.log` (Jacob Hummer) [#51463](https://github.com/nodejs/node/pull/51463)
* \[[`61b90e7c5e`](https://github.com/nodejs/node/commit/61b90e7c5e)] - **build**: update gcovr to 7.2 and codecov config (Benjamin E. Coe) [#54019](https://github.com/nodejs/node/pull/54019)
* \[[`a9c04eaa27`](https://github.com/nodejs/node/commit/a9c04eaa27)] - **build**: ensure v8\_pointer\_compression\_sandbox is enabled on 64bit (Shelley Vohr) [#53884](https://github.com/nodejs/node/pull/53884)
* \[[`342a663d7a`](https://github.com/nodejs/node/commit/342a663d7a)] - **build**: trigger coverage ci when updating codecov (Yagiz Nizipli) [#53929](https://github.com/nodejs/node/pull/53929)
* \[[`5727b4d129`](https://github.com/nodejs/node/commit/5727b4d129)] - **build**: update codecov coverage build count (Yagiz Nizipli) [#53929](https://github.com/nodejs/node/pull/53929)
* \[[`977af25870`](https://github.com/nodejs/node/commit/977af25870)] - **build**: disable test-asan workflow (Michaël Zasso) [#53844](https://github.com/nodejs/node/pull/53844)
* \[[`04798fb104`](https://github.com/nodejs/node/commit/04798fb104)] - **build**: fix build warning of c-ares under GN build (Cheng) [#53750](https://github.com/nodejs/node/pull/53750)
* \[[`5ec5e78574`](https://github.com/nodejs/node/commit/5ec5e78574)] - **build**: fix mac build error of c-ares under GN (Cheng) [#53687](https://github.com/nodejs/node/pull/53687)
* \[[`3d8721f0a4`](https://github.com/nodejs/node/commit/3d8721f0a4)] - **build**: add version-specific library path for AIX (Richard Lau) [#53585](https://github.com/nodejs/node/pull/53585)
* \[[`ffb0bd344d`](https://github.com/nodejs/node/commit/ffb0bd344d)] - **build, tools**: drop leading `/` from `r2dir` (Richard Lau) [#53951](https://github.com/nodejs/node/pull/53951)
* \[[`a2d74f4c31`](https://github.com/nodejs/node/commit/a2d74f4c31)] - **build,tools**: simplify upload of shasum signatures (Michaël Zasso) [#53892](https://github.com/nodejs/node/pull/53892)
* \[[`993bb3b6e7`](https://github.com/nodejs/node/commit/993bb3b6e7)] - **child\_process**: fix incomplete prototype pollution hardening (Liran Tal) [#53781](https://github.com/nodejs/node/pull/53781)
* \[[`137a2e5766`](https://github.com/nodejs/node/commit/137a2e5766)] - **cli**: document `--inspect` port `0` behavior (Aviv Keller) [#53782](https://github.com/nodejs/node/pull/53782)
* \[[`820e6e1737`](https://github.com/nodejs/node/commit/820e6e1737)] - **cli**: update `node.1` to reflect Atom's sunset (Aviv Keller) [#53734](https://github.com/nodejs/node/pull/53734)
* \[[`fa0e8d7b3b`](https://github.com/nodejs/node/commit/fa0e8d7b3b)] - **crypto**: avoid std::function (Tobias Nießen) [#53683](https://github.com/nodejs/node/pull/53683)
* \[[`460240c368`](https://github.com/nodejs/node/commit/460240c368)] - **crypto**: make deriveBits length parameter optional and nullable (Filip Skokan) [#53601](https://github.com/nodejs/node/pull/53601)
* \[[`ceb1d5e00a`](https://github.com/nodejs/node/commit/ceb1d5e00a)] - **crypto**: avoid taking ownership of OpenSSL objects (Tobias Nießen) [#53460](https://github.com/nodejs/node/pull/53460)
* \[[`44268c27eb`](https://github.com/nodejs/node/commit/44268c27eb)] - **deps**: update corepack to 0.29.3 (Node.js GitHub Bot) [#54072](https://github.com/nodejs/node/pull/54072)
* \[[`496975ece0`](https://github.com/nodejs/node/commit/496975ece0)] - **deps**: update c-ares to v1.32.3 (Node.js GitHub Bot) [#54020](https://github.com/nodejs/node/pull/54020)
* \[[`5eea419349`](https://github.com/nodejs/node/commit/5eea419349)] - **deps**: update c-ares to v1.32.2 (Node.js GitHub Bot) [#53865](https://github.com/nodejs/node/pull/53865)
* \[[`8c8e3688c5`](https://github.com/nodejs/node/commit/8c8e3688c5)] - **deps**: update googletest to 4b21f1a (Node.js GitHub Bot) [#53842](https://github.com/nodejs/node/pull/53842)
* \[[`78f6b34c77`](https://github.com/nodejs/node/commit/78f6b34c77)] - **deps**: update minimatch to 10.0.1 (Node.js GitHub Bot) [#53841](https://github.com/nodejs/node/pull/53841)
* \[[`398f7acca3`](https://github.com/nodejs/node/commit/398f7acca3)] - **deps**: update corepack to 0.29.2 (Node.js GitHub Bot) [#53838](https://github.com/nodejs/node/pull/53838)
* \[[`fa8f99d90b`](https://github.com/nodejs/node/commit/fa8f99d90b)] - **deps**: update simdutf to 5.3.0 (Node.js GitHub Bot) [#53837](https://github.com/nodejs/node/pull/53837)
* \[[`a19b28336b`](https://github.com/nodejs/node/commit/a19b28336b)] - **deps**: update ada to 2.9.0 (Node.js GitHub Bot) [#53748](https://github.com/nodejs/node/pull/53748)
* \[[`2f66c7e707`](https://github.com/nodejs/node/commit/2f66c7e707)] - **deps**: upgrade npm to 10.8.2 (npm team) [#53799](https://github.com/nodejs/node/pull/53799)
* \[[`2a2620e7c0`](https://github.com/nodejs/node/commit/2a2620e7c0)] - **deps**: update googletest to 34ad51b (Node.js GitHub Bot) [#53157](https://github.com/nodejs/node/pull/53157)
* \[[`c01ce60ce7`](https://github.com/nodejs/node/commit/c01ce60ce7)] - **deps**: update googletest to 305e5a2 (Node.js GitHub Bot) [#53157](https://github.com/nodejs/node/pull/53157)
* \[[`832328ea01`](https://github.com/nodejs/node/commit/832328ea01)] - **deps**: update c-ares to v1.32.1 (Node.js GitHub Bot) [#53753](https://github.com/nodejs/node/pull/53753)
* \[[`878e9a4ae7`](https://github.com/nodejs/node/commit/878e9a4ae7)] - **deps**: update minimatch to 9.0.5 (Node.js GitHub Bot) [#53646](https://github.com/nodejs/node/pull/53646)
* \[[`4647e6b5c5`](https://github.com/nodejs/node/commit/4647e6b5c5)] - **deps**: update c-ares to v1.32.0 (Node.js GitHub Bot) [#53722](https://github.com/nodejs/node/pull/53722)
* \[[`30310bf887`](https://github.com/nodejs/node/commit/30310bf887)] - **doc**: move numCPUs require to top of file in cluster CJS example (Alfredo González) [#53932](https://github.com/nodejs/node/pull/53932)
* \[[`36170eddca`](https://github.com/nodejs/node/commit/36170eddca)] - **doc**: update security-release process to automated one (Rafael Gonzaga) [#53877](https://github.com/nodejs/node/pull/53877)
* \[[`55f5e76ba7`](https://github.com/nodejs/node/commit/55f5e76ba7)] - **doc**: fix typo in technical-priorities.md (YoonSoo\_Shin) [#54094](https://github.com/nodejs/node/pull/54094)
* \[[`1c0ccc0ca8`](https://github.com/nodejs/node/commit/1c0ccc0ca8)] - **doc**: fix typo in diagnostic tooling support tiers document (Taejin Kim) [#54058](https://github.com/nodejs/node/pull/54058)
* \[[`6a5120ff0f`](https://github.com/nodejs/node/commit/6a5120ff0f)] - **doc**: move GeoffreyBooth to TSC regular member (Geoffrey Booth) [#54047](https://github.com/nodejs/node/pull/54047)
* \[[`ead05aad2a`](https://github.com/nodejs/node/commit/ead05aad2a)] - **doc**: fix typo in recognizing-contributors (Marco Ippolito) [#53990](https://github.com/nodejs/node/pull/53990)
* \[[`25e59aebac`](https://github.com/nodejs/node/commit/25e59aebac)] - **doc**: update boxstarter README (Aviv Keller) [#53785](https://github.com/nodejs/node/pull/53785)
* \[[`a3183fb927`](https://github.com/nodejs/node/commit/a3183fb927)] - **doc**: add info about prefix-only modules to `module.builtinModules` (Grigory) [#53954](https://github.com/nodejs/node/pull/53954)
* \[[`89599e025f`](https://github.com/nodejs/node/commit/89599e025f)] - **doc**: remove `scroll-behavior: smooth;` (Cloyd Lau) [#53942](https://github.com/nodejs/node/pull/53942)
* \[[`139c62e40c`](https://github.com/nodejs/node/commit/139c62e40c)] - **doc**: move --test-coverage-{ex,in}clude to proper location (Colin Ihrig) [#53926](https://github.com/nodejs/node/pull/53926)
* \[[`233aba90ea`](https://github.com/nodejs/node/commit/233aba90ea)] - **doc**: update `api_assets` README for new files (Aviv Keller) [#53676](https://github.com/nodejs/node/pull/53676)
* \[[`44a1cbe98a`](https://github.com/nodejs/node/commit/44a1cbe98a)] - **doc**: add MattiasBuelens to collaborators (Mattias Buelens) [#53895](https://github.com/nodejs/node/pull/53895)
* \[[`f5280ddbc5`](https://github.com/nodejs/node/commit/f5280ddbc5)] - **doc**: fix casing of GitHub handle for two collaborators (Antoine du Hamel) [#53857](https://github.com/nodejs/node/pull/53857)
* \[[`9224e3eef1`](https://github.com/nodejs/node/commit/9224e3eef1)] - **doc**: update release-post nodejs.org script (Rafael Gonzaga) [#53762](https://github.com/nodejs/node/pull/53762)
* \[[`f87eed8de4`](https://github.com/nodejs/node/commit/f87eed8de4)] - **doc**: move MylesBorins to emeritus (Myles Borins) [#53760](https://github.com/nodejs/node/pull/53760)
* \[[`32ac80ae8d`](https://github.com/nodejs/node/commit/32ac80ae8d)] - **doc**: add Rafael to the last security release (Rafael Gonzaga) [#53769](https://github.com/nodejs/node/pull/53769)
* \[[`e71aa7e98b`](https://github.com/nodejs/node/commit/e71aa7e98b)] - **doc**: use mock.callCount() in examples (Sébastien Règne) [#53754](https://github.com/nodejs/node/pull/53754)
* \[[`f64db24312`](https://github.com/nodejs/node/commit/f64db24312)] - **doc**: clarify authenticity of plaintexts in update (Tobias Nießen) [#53784](https://github.com/nodejs/node/pull/53784)
* \[[`51e736ac83`](https://github.com/nodejs/node/commit/51e736ac83)] - **doc**: add option to have support me link (Michael Dawson) [#53312](https://github.com/nodejs/node/pull/53312)
* \[[`9804731d0f`](https://github.com/nodejs/node/commit/9804731d0f)] - **doc**: update `scroll-padding-top` to 4rem (Cloyd Lau) [#53662](https://github.com/nodejs/node/pull/53662)
* \[[`229f7f8b8a`](https://github.com/nodejs/node/commit/229f7f8b8a)] - **doc**: mention v8.setFlagsFromString to pm (Rafael Gonzaga) [#53731](https://github.com/nodejs/node/pull/53731)
* \[[`98d59aa929`](https://github.com/nodejs/node/commit/98d59aa929)] - **doc**: remove the last \<pre> tag (Claudio W) [#53741](https://github.com/nodejs/node/pull/53741)
* \[[`60ee41df08`](https://github.com/nodejs/node/commit/60ee41df08)] - **doc**: exclude voting and regular TSC from spotlight (Michael Dawson) [#53694](https://github.com/nodejs/node/pull/53694)
* \[[`c3536cfa99`](https://github.com/nodejs/node/commit/c3536cfa99)] - **doc**: fix releases guide for recent Git versions (Michaël Zasso) [#53709](https://github.com/nodejs/node/pull/53709)
* \[[`3b632e1871`](https://github.com/nodejs/node/commit/3b632e1871)] - **doc**: require `node:process` in assert doc examples (Alfredo González) [#53702](https://github.com/nodejs/node/pull/53702)
* \[[`754090c110`](https://github.com/nodejs/node/commit/754090c110)] - **doc**: add additional explanation to the wildcard section in permissions (jakecastelli) [#53664](https://github.com/nodejs/node/pull/53664)
* \[[`4346de7267`](https://github.com/nodejs/node/commit/4346de7267)] - **doc**: mark NODE\_MODULE\_VERSION for Node.js 22.0.0 (Michaël Zasso) [#53650](https://github.com/nodejs/node/pull/53650)
* \[[`758178bd72`](https://github.com/nodejs/node/commit/758178bd72)] - **doc**: include node.module\_timer on available categories (Vinicius Lourenço) [#53638](https://github.com/nodejs/node/pull/53638)
* \[[`e0d213df2b`](https://github.com/nodejs/node/commit/e0d213df2b)] - **doc**: fix module customization hook examples (Elliot Goodrich) [#53637](https://github.com/nodejs/node/pull/53637)
* \[[`43ac5a2441`](https://github.com/nodejs/node/commit/43ac5a2441)] - **doc**: fix doc for correct usage with plan & TestContext (Emil Tayeb) [#53615](https://github.com/nodejs/node/pull/53615)
* \[[`5076f0d292`](https://github.com/nodejs/node/commit/5076f0d292)] - **doc**: remove some news issues that are no longer (Michael Dawson) [#53608](https://github.com/nodejs/node/pull/53608)
* \[[`c997dbef34`](https://github.com/nodejs/node/commit/c997dbef34)] - **doc**: add issue for news from ambassadors (Michael Dawson) [#53607](https://github.com/nodejs/node/pull/53607)
* \[[`16d55f1d25`](https://github.com/nodejs/node/commit/16d55f1d25)] - **doc**: add esm example for os (Leonardo Peixoto) [#53604](https://github.com/nodejs/node/pull/53604)
* \[[`156fc536f2`](https://github.com/nodejs/node/commit/156fc536f2)] - **doc**: clarify usage of coverage reporters (Eliphaz Bouye) [#53523](https://github.com/nodejs/node/pull/53523)
* \[[`f8f247bc99`](https://github.com/nodejs/node/commit/f8f247bc99)] - **doc**: document addition testing options (Aviv Keller) [#53569](https://github.com/nodejs/node/pull/53569)
* \[[`73860aca56`](https://github.com/nodejs/node/commit/73860aca56)] - **doc**: clarify that fs.exists() may return false for existing symlink (Tobias Nießen) [#53566](https://github.com/nodejs/node/pull/53566)
* \[[`59c5c5c73e`](https://github.com/nodejs/node/commit/59c5c5c73e)] - **doc**: note http.closeAllConnections excludes upgraded sockets (Rob Hogan) [#53560](https://github.com/nodejs/node/pull/53560)
* \[[`1cd3c8eb27`](https://github.com/nodejs/node/commit/1cd3c8eb27)] - **doc**: fix typo (EhsanKhaki) [#53397](https://github.com/nodejs/node/pull/53397)
* \[[`3c5e593e2a`](https://github.com/nodejs/node/commit/3c5e593e2a)] - **doc, meta**: add PTAL to glossary (Aviv Keller) [#53770](https://github.com/nodejs/node/pull/53770)
* \[[`f336e61257`](https://github.com/nodejs/node/commit/f336e61257)] - **doc, test**: tracing channel hasSubscribers getter (Thomas Hunter II) [#52908](https://github.com/nodejs/node/pull/52908)
* \[[`4187b81439`](https://github.com/nodejs/node/commit/4187b81439)] - **doc, typings**: events.once accepts symbol event type (René) [#53542](https://github.com/nodejs/node/pull/53542)
* \[[`3cdf94d403`](https://github.com/nodejs/node/commit/3cdf94d403)] - **doc,tty**: add documentation for ReadStream and WriteStream (jakecastelli) [#53567](https://github.com/nodejs/node/pull/53567)
* \[[`5d03f6fab7`](https://github.com/nodejs/node/commit/5d03f6fab7)] - **esm**: move hooks test with others (Geoffrey Booth) [#53558](https://github.com/nodejs/node/pull/53558)
* \[[`490f15a99b`](https://github.com/nodejs/node/commit/490f15a99b)] - **fs**: ensure consistency for mkdtemp in both fs and fs/promises (YieldRay) [#53776](https://github.com/nodejs/node/pull/53776)
* \[[`8e64c02b19`](https://github.com/nodejs/node/commit/8e64c02b19)] - **(SEMVER-MINOR)** **http**: add diagnostics channel `http.client.request.error` (Kohei Ueno) [#54054](https://github.com/nodejs/node/pull/54054)
* \[[`0d70c79ebf`](https://github.com/nodejs/node/commit/0d70c79ebf)] - **lib**: optimize copyError with ObjectAssign in primordials (HEESEUNG) [#53999](https://github.com/nodejs/node/pull/53999)
* \[[`a4ff2ac0f0`](https://github.com/nodejs/node/commit/a4ff2ac0f0)] - **lib**: improve cluster/primary code (Ehsan Khakifirooz) [#53756](https://github.com/nodejs/node/pull/53756)
* \[[`c667fbd988`](https://github.com/nodejs/node/commit/c667fbd988)] - **lib**: improve error message when index not found on cjs (Vinicius Lourenço) [#53859](https://github.com/nodejs/node/pull/53859)
* \[[`51ba566171`](https://github.com/nodejs/node/commit/51ba566171)] - **lib**: decorate async stack trace in source maps (Chengzhong Wu) [#53860](https://github.com/nodejs/node/pull/53860)
* \[[`d012dd3d29`](https://github.com/nodejs/node/commit/d012dd3d29)] - **lib**: remove path.resolve from permissions.js (Rafael Gonzaga) [#53729](https://github.com/nodejs/node/pull/53729)
* \[[`1e9ff50446`](https://github.com/nodejs/node/commit/1e9ff50446)] - **lib**: add toJSON to PerformanceMeasure (theanarkh) [#53603](https://github.com/nodejs/node/pull/53603)
* \[[`3a2d8bffa5`](https://github.com/nodejs/node/commit/3a2d8bffa5)] - **lib**: convert WeakMaps in cjs loader with private symbol properties (Chengzhong Wu) [#52095](https://github.com/nodejs/node/pull/52095)
* \[[`e326342bd7`](https://github.com/nodejs/node/commit/e326342bd7)] - **meta**: add `sqlite` to js subsystems (Alex Yang) [#53911](https://github.com/nodejs/node/pull/53911)
* \[[`bfabfb4d17`](https://github.com/nodejs/node/commit/bfabfb4d17)] - **meta**: move tsc member to emeritus (Michael Dawson) [#54029](https://github.com/nodejs/node/pull/54029)
* \[[`ae30674991`](https://github.com/nodejs/node/commit/ae30674991)] - **meta**: add jake to collaborators (jakecastelli) [#54004](https://github.com/nodejs/node/pull/54004)
* \[[`6ca0cfc602`](https://github.com/nodejs/node/commit/6ca0cfc602)] - **meta**: remove license for hljs (Aviv Keller) [#53970](https://github.com/nodejs/node/pull/53970)
* \[[`e6ba121e83`](https://github.com/nodejs/node/commit/e6ba121e83)] - **meta**: make more bug-report information required (Aviv Keller) [#53718](https://github.com/nodejs/node/pull/53718)
* \[[`1864cddd0c`](https://github.com/nodejs/node/commit/1864cddd0c)] - **meta**: store actions secrets in environment (Aviv Keller) [#53930](https://github.com/nodejs/node/pull/53930)
* \[[`c0b24e5071`](https://github.com/nodejs/node/commit/c0b24e5071)] - **meta**: move anonrig to tsc voting members (Yagiz Nizipli) [#53888](https://github.com/nodejs/node/pull/53888)
* \[[`e60b089f7f`](https://github.com/nodejs/node/commit/e60b089f7f)] - **meta**: remove redudant logging from dep updaters (Aviv Keller) [#53783](https://github.com/nodejs/node/pull/53783)
* \[[`bff6995ec3`](https://github.com/nodejs/node/commit/bff6995ec3)] - **meta**: change email address of anonrig (Yagiz Nizipli) [#53829](https://github.com/nodejs/node/pull/53829)
* \[[`c2bb46020a`](https://github.com/nodejs/node/commit/c2bb46020a)] - **meta**: add `node_sqlite.c` to PR label config (Aviv Keller) [#53797](https://github.com/nodejs/node/pull/53797)
* \[[`b8d2bbc6d6`](https://github.com/nodejs/node/commit/b8d2bbc6d6)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#53758](https://github.com/nodejs/node/pull/53758)
* \[[`0ad4b7c1f7`](https://github.com/nodejs/node/commit/0ad4b7c1f7)] - **meta**: use HTML entities in commit-queue comment (Aviv Keller) [#53744](https://github.com/nodejs/node/pull/53744)
* \[[`aa0c5c25d1`](https://github.com/nodejs/node/commit/aa0c5c25d1)] - **meta**: move regular TSC member to emeritus (Michael Dawson) [#53693](https://github.com/nodejs/node/pull/53693)
* \[[`a5f5b4550b`](https://github.com/nodejs/node/commit/a5f5b4550b)] - **meta**: bump codecov/codecov-action from 4.4.1 to 4.5.0 (dependabot\[bot]) [#53675](https://github.com/nodejs/node/pull/53675)
* \[[`f84e215c90`](https://github.com/nodejs/node/commit/f84e215c90)] - **meta**: bump mozilla-actions/sccache-action from 0.0.4 to 0.0.5 (dependabot\[bot]) [#53674](https://github.com/nodejs/node/pull/53674)
* \[[`d5a9c249d3`](https://github.com/nodejs/node/commit/d5a9c249d3)] - **meta**: bump github/codeql-action from 3.25.7 to 3.25.11 (dependabot\[bot]) [#53673](https://github.com/nodejs/node/pull/53673)
* \[[`39d6c780c8`](https://github.com/nodejs/node/commit/39d6c780c8)] - **meta**: bump actions/checkout from 4.1.6 to 4.1.7 (dependabot\[bot]) [#53672](https://github.com/nodejs/node/pull/53672)
* \[[`bb6fe38a34`](https://github.com/nodejs/node/commit/bb6fe38a34)] - **meta**: bump peter-evans/create-pull-request from 6.0.5 to 6.1.0 (dependabot\[bot]) [#53671](https://github.com/nodejs/node/pull/53671)
* \[[`5dcdfb5e6b`](https://github.com/nodejs/node/commit/5dcdfb5e6b)] - **meta**: bump step-security/harden-runner from 2.8.0 to 2.8.1 (dependabot\[bot]) [#53670](https://github.com/nodejs/node/pull/53670)
* \[[`44d901a1c9`](https://github.com/nodejs/node/commit/44d901a1c9)] - **meta**: move member from TSC regular to emeriti (Michael Dawson) [#53599](https://github.com/nodejs/node/pull/53599)
* \[[`0c91186afa`](https://github.com/nodejs/node/commit/0c91186afa)] - **meta**: warnings bypass deprecation cycle (Benjamin Gruenbaum) [#53513](https://github.com/nodejs/node/pull/53513)
* \[[`bcd08bef60`](https://github.com/nodejs/node/commit/bcd08bef60)] - **meta**: prevent constant references to issues in versioning (Aviv Keller) [#53564](https://github.com/nodejs/node/pull/53564)
* \[[`7625dc4927`](https://github.com/nodejs/node/commit/7625dc4927)] - **module**: fix submodules loaded by require() and import() (Joyee Cheung) [#52487](https://github.com/nodejs/node/pull/52487)
* \[[`6c4f4772e3`](https://github.com/nodejs/node/commit/6c4f4772e3)] - **module**: tidy code and comments (Jacob Smith) [#52437](https://github.com/nodejs/node/pull/52437)
* \[[`51b88faeac`](https://github.com/nodejs/node/commit/51b88faeac)] - **module**: disallow CJS <-> ESM edges in a cycle from require(esm) (Joyee Cheung) [#52264](https://github.com/nodejs/node/pull/52264)
* \[[`4dae68ced4`](https://github.com/nodejs/node/commit/4dae68ced4)] - **module**: centralize SourceTextModule compilation for builtin loader (Joyee Cheung) [#52291](https://github.com/nodejs/node/pull/52291)
* \[[`cad46afc07`](https://github.com/nodejs/node/commit/cad46afc07)] - **(SEMVER-MINOR)** **module**: support require()ing synchronous ESM graphs (Joyee Cheung) [#51977](https://github.com/nodejs/node/pull/51977)
* \[[`ac58c829a1`](https://github.com/nodejs/node/commit/ac58c829a1)] - **node-api**: add property keys benchmark (Chengzhong Wu) [#54012](https://github.com/nodejs/node/pull/54012)
* \[[`e6a4104bd1`](https://github.com/nodejs/node/commit/e6a4104bd1)] - **node-api**: rename nogc to basic (Gabriel Schulhof) [#53830](https://github.com/nodejs/node/pull/53830)
* \[[`57b8b8e18e`](https://github.com/nodejs/node/commit/57b8b8e18e)] - **(SEMVER-MINOR)** **path**: add `matchesGlob` method (Aviv Keller) [#52881](https://github.com/nodejs/node/pull/52881)
* \[[`bf6aa53299`](https://github.com/nodejs/node/commit/bf6aa53299)] - **process**: unify experimental warning messages (Aviv Keller) [#53704](https://github.com/nodejs/node/pull/53704)
* \[[`2a3ae16e62`](https://github.com/nodejs/node/commit/2a3ae16e62)] - **src**: expose LookupAndCompile with parameters (Shelley Vohr) [#53886](https://github.com/nodejs/node/pull/53886)
* \[[`0109f9c961`](https://github.com/nodejs/node/commit/0109f9c961)] - **src**: simplify AESCipherTraits::AdditionalConfig (Tobias Nießen) [#53890](https://github.com/nodejs/node/pull/53890)
* \[[`6bafe8a457`](https://github.com/nodejs/node/commit/6bafe8a457)] - **src**: fix -Wshadow warning (Shelley Vohr) [#53885](https://github.com/nodejs/node/pull/53885)
* \[[`4c36d6c47a`](https://github.com/nodejs/node/commit/4c36d6c47a)] - **src**: fix slice of slice of file-backed Blob (Josh Lee) [#53972](https://github.com/nodejs/node/pull/53972)
* \[[`848c2d59fb`](https://github.com/nodejs/node/commit/848c2d59fb)] - **src**: cache invariant code motion (Rafael Gonzaga) [#53879](https://github.com/nodejs/node/pull/53879)
* \[[`acaf5dd1cd`](https://github.com/nodejs/node/commit/acaf5dd1cd)] - **src**: avoid strcmp in ImportJWKAsymmetricKey (Tobias Nießen) [#53813](https://github.com/nodejs/node/pull/53813)
* \[[`b71250aaf9`](https://github.com/nodejs/node/commit/b71250aaf9)] - **src**: replace ToLocalChecked uses with ToLocal in node-file (James M Snell) [#53869](https://github.com/nodejs/node/pull/53869)
* \[[`aff9a5339a`](https://github.com/nodejs/node/commit/aff9a5339a)] - **src**: fix env-file flag to ignore spaces before quotes (Mohit Malhotra) [#53786](https://github.com/nodejs/node/pull/53786)
* \[[`e352a4ef27`](https://github.com/nodejs/node/commit/e352a4ef27)] - **src**: update outdated references to spec sections (Tobias Nießen) [#53832](https://github.com/nodejs/node/pull/53832)
* \[[`1a4da22a60`](https://github.com/nodejs/node/commit/1a4da22a60)] - **src**: use Maybe\<void> in ManagedEVPPKey (Tobias Nießen) [#53811](https://github.com/nodejs/node/pull/53811)
* \[[`0c24b91bd2`](https://github.com/nodejs/node/commit/0c24b91bd2)] - **src**: fix error handling in ExportJWKAsymmetricKey (Tobias Nießen) [#53767](https://github.com/nodejs/node/pull/53767)
* \[[`81cd84c716`](https://github.com/nodejs/node/commit/81cd84c716)] - **src**: use Maybe\<void> in node::crypto::error (Tobias Nießen) [#53766](https://github.com/nodejs/node/pull/53766)
* \[[`8135f3616d`](https://github.com/nodejs/node/commit/8135f3616d)] - **src**: fix typo in node.h (Daeyeon Jeong) [#53759](https://github.com/nodejs/node/pull/53759)
* \[[`e6d735a997`](https://github.com/nodejs/node/commit/e6d735a997)] - **src**: document the Node.js context embedder data (Joyee Cheung) [#53611](https://github.com/nodejs/node/pull/53611)
* \[[`584beaa2ed`](https://github.com/nodejs/node/commit/584beaa2ed)] - **src**: zero-initialize data that are copied into the snapshot (Joyee Cheung) [#53563](https://github.com/nodejs/node/pull/53563)
* \[[`ef5dabd8c6`](https://github.com/nodejs/node/commit/ef5dabd8c6)] - **src**: fix Worker termination when '--inspect-brk' is passed (Daeyeon Jeong) [#53724](https://github.com/nodejs/node/pull/53724)
* \[[`62f4f6f48e`](https://github.com/nodejs/node/commit/62f4f6f48e)] - **src**: remove ArrayBufferAllocator::Reallocate override (Shu-yu Guo) [#52910](https://github.com/nodejs/node/pull/52910)
* \[[`a6dd8643fa`](https://github.com/nodejs/node/commit/a6dd8643fa)] - **src**: reduce unnecessary serialization of CLI options in C++ (Joyee Cheung) [#52451](https://github.com/nodejs/node/pull/52451)
* \[[`31fdb881cf`](https://github.com/nodejs/node/commit/31fdb881cf)] - **src,lib**: expose getCategoryEnabledBuffer to use on node.http (Vinicius Lourenço) [#53602](https://github.com/nodejs/node/pull/53602)
* \[[`2eea8502e1`](https://github.com/nodejs/node/commit/2eea8502e1)] - **src,test**: further cleanup references to osx (Daniel Bayley) [#53820](https://github.com/nodejs/node/pull/53820)
* \[[`7c21bb99a5`](https://github.com/nodejs/node/commit/7c21bb99a5)] - **(SEMVER-MINOR)** **stream**: expose DuplexPair API (Austin Wright) [#34111](https://github.com/nodejs/node/pull/34111)
* \[[`56299f7309`](https://github.com/nodejs/node/commit/56299f7309)] - **stream**: improve inspector ergonomics (Benjamin Gruenbaum) [#53800](https://github.com/nodejs/node/pull/53800)
* \[[`9b82b15230`](https://github.com/nodejs/node/commit/9b82b15230)] - **stream**: update ongoing promise in async iterator return() method (Mattias Buelens) [#52657](https://github.com/nodejs/node/pull/52657)
* \[[`4a3ecbfc9b`](https://github.com/nodejs/node/commit/4a3ecbfc9b)] - **(SEMVER-MINOR)** **stream**: implement `min` option for `ReadableStreamBYOBReader.read` (Mattias Buelens) [#50888](https://github.com/nodejs/node/pull/50888)
* \[[`bd996bf694`](https://github.com/nodejs/node/commit/bd996bf694)] - **test**: do not swallow uncaughtException errors in exit code tests (Meghan Denny) [#54039](https://github.com/nodejs/node/pull/54039)
* \[[`77761af077`](https://github.com/nodejs/node/commit/77761af077)] - **test**: move shared module to `test/common` (Rich Trott) [#54042](https://github.com/nodejs/node/pull/54042)
* \[[`bec88ce138`](https://github.com/nodejs/node/commit/bec88ce138)] - **test**: skip sea tests with more accurate available disk space estimation (Chengzhong Wu) [#53996](https://github.com/nodejs/node/pull/53996)
* \[[`9a98ad47cd`](https://github.com/nodejs/node/commit/9a98ad47cd)] - **test**: remove unnecessary console log (KAYYY) [#53812](https://github.com/nodejs/node/pull/53812)
* \[[`364d09cf0a`](https://github.com/nodejs/node/commit/364d09cf0a)] - **test**: add comments and rename test for timer robustness (Rich Trott) [#54008](https://github.com/nodejs/node/pull/54008)
* \[[`5c5093dc0a`](https://github.com/nodejs/node/commit/5c5093dc0a)] - **test**: add test for one arg timers to increase coverage (Carlos Espa) [#54007](https://github.com/nodejs/node/pull/54007)
* \[[`43ede1ae0b`](https://github.com/nodejs/node/commit/43ede1ae0b)] - **test**: mark 'test/parallel/test-sqlite.js' as flaky (Colin Ihrig) [#54031](https://github.com/nodejs/node/pull/54031)
* \[[`0ad783cb42`](https://github.com/nodejs/node/commit/0ad783cb42)] - **test**: mark test-pipe-file-to-http as flaky (jakecastelli) [#53751](https://github.com/nodejs/node/pull/53751)
* \[[`f2b4fd3544`](https://github.com/nodejs/node/commit/f2b4fd3544)] - **test**: compare paths on Windows without considering case (Early Riser) [#53993](https://github.com/nodejs/node/pull/53993)
* \[[`2e69e5f4d2`](https://github.com/nodejs/node/commit/2e69e5f4d2)] - **test**: skip sea tests in large debug builds (Chengzhong Wu) [#53918](https://github.com/nodejs/node/pull/53918)
* \[[`56c26fe6e5`](https://github.com/nodejs/node/commit/56c26fe6e5)] - **test**: skip --title check on IBM i (Abdirahim Musse) [#53952](https://github.com/nodejs/node/pull/53952)
* \[[`6d0b8ded00`](https://github.com/nodejs/node/commit/6d0b8ded00)] - **test**: reduce flakiness of `test-assert-esm-cjs-message-verify` (Antoine du Hamel) [#53967](https://github.com/nodejs/node/pull/53967)
* \[[`edb75aebd7`](https://github.com/nodejs/node/commit/edb75aebd7)] - **test**: use `PYTHON` executable from env in `assertSnapshot` (Antoine du Hamel) [#53938](https://github.com/nodejs/node/pull/53938)
* \[[`be94e470a6`](https://github.com/nodejs/node/commit/be94e470a6)] - **test**: deflake test-blob-file-backed (Luigi Pinca) [#53920](https://github.com/nodejs/node/pull/53920)
* \[[`c2b0dcd165`](https://github.com/nodejs/node/commit/c2b0dcd165)] - **test**: un-set inspector-async-hook-setup-at-inspect-brk as flaky (Abdirahim Musse) [#53692](https://github.com/nodejs/node/pull/53692)
* \[[`6dc18981ac`](https://github.com/nodejs/node/commit/6dc18981ac)] - **test**: use python3 instead of python in pummel test (Mathis Wiehl) [#53057](https://github.com/nodejs/node/pull/53057)
* \[[`662bf524e1`](https://github.com/nodejs/node/commit/662bf524e1)] - **test**: do not assume cwd in snapshot tests (Antoine du Hamel) [#53146](https://github.com/nodejs/node/pull/53146)
* \[[`a07526702a`](https://github.com/nodejs/node/commit/a07526702a)] - **test**: fix OpenSSL version checks (Richard Lau) [#53503](https://github.com/nodejs/node/pull/53503)
* \[[`2b70018d11`](https://github.com/nodejs/node/commit/2b70018d11)] - **test**: refactor, add assertion to http-request-end (jakecastelli) [#53411](https://github.com/nodejs/node/pull/53411)
* \[[`c0262c1561`](https://github.com/nodejs/node/commit/c0262c1561)] - **test\_runner**: switched to internal readline interface (Emil Tayeb) [#54000](https://github.com/nodejs/node/pull/54000)
* \[[`fb7342246c`](https://github.com/nodejs/node/commit/fb7342246c)] - **test\_runner**: do not throw on mocked clearTimeout() (Aksinya Bykova) [#54005](https://github.com/nodejs/node/pull/54005)
* \[[`367f9e77f3`](https://github.com/nodejs/node/commit/367f9e77f3)] - **test\_runner**: cleanup global event listeners after run (Eddie Abbondanzio) [#53878](https://github.com/nodejs/node/pull/53878)
* \[[`206c668ee7`](https://github.com/nodejs/node/commit/206c668ee7)] - **test\_runner**: remove plan option from run() (Colin Ihrig) [#53834](https://github.com/nodejs/node/pull/53834)
* \[[`8660d481e5`](https://github.com/nodejs/node/commit/8660d481e5)] - **tls**: add setKeyCert() to tls.Socket (Brian White) [#53636](https://github.com/nodejs/node/pull/53636)
* \[[`9c5beabd83`](https://github.com/nodejs/node/commit/9c5beabd83)] - **tools**: fix `SLACK_TITLE` in invalid commit workflow (Antoine du Hamel) [#53912](https://github.com/nodejs/node/pull/53912)
* \[[`4dedf2aead`](https://github.com/nodejs/node/commit/4dedf2aead)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#53840](https://github.com/nodejs/node/pull/53840)
* \[[`642d5c5d30`](https://github.com/nodejs/node/commit/642d5c5d30)] - **tools**: use v8\_features.json to populate config.gypi (Cheng) [#53749](https://github.com/nodejs/node/pull/53749)
* \[[`031206544d`](https://github.com/nodejs/node/commit/031206544d)] - **tools**: update lint-md-dependencies to unified\@11.0.5 (Node.js GitHub Bot) [#53555](https://github.com/nodejs/node/pull/53555)
* \[[`8404421ea6`](https://github.com/nodejs/node/commit/8404421ea6)] - **tools**: replace reference to NodeMainInstance with SnapshotBuilder (codediverdev) [#53544](https://github.com/nodejs/node/pull/53544)
* \[[`2d8490fed5`](https://github.com/nodejs/node/commit/2d8490fed5)] - **typings**: add `fs_dir` types (Yagiz Nizipli) [#53631](https://github.com/nodejs/node/pull/53631)
* \[[`325eae0b3f`](https://github.com/nodejs/node/commit/325eae0b3f)] - **url**: fix typo (KAYYY) [#53827](https://github.com/nodejs/node/pull/53827)
* \[[`7fc45f5e3f`](https://github.com/nodejs/node/commit/7fc45f5e3f)] - **url**: reduce unnecessary string copies (Yagiz Nizipli) [#53628](https://github.com/nodejs/node/pull/53628)
* \[[`1d961facf1`](https://github.com/nodejs/node/commit/1d961facf1)] - **url**: add missing documentation for `URL.parse()` (Yagiz Nizipli) [#53733](https://github.com/nodejs/node/pull/53733)
* \[[`ce877c6d0f`](https://github.com/nodejs/node/commit/ce877c6d0f)] - **util**: fix crashing when emitting new Buffer() deprecation warning #53075 (Aras Abbasi) [#53089](https://github.com/nodejs/node/pull/53089)
* \[[`d6d04279ca`](https://github.com/nodejs/node/commit/d6d04279ca)] - **worker**: allow copied NODE\_OPTIONS in the env setting (Joyee Cheung) [#53596](https://github.com/nodejs/node/pull/53596)

<a id="20.16.0"></a>

## 2024-07-24, Version 20.16.0 'Iron' (LTS), @marco-ippolito

### process: add process.getBuiltinModule(id)

`process.getBuiltinModule(id)` provides a way to load built-in modules
in a globally available function. ES Modules that need to support
other environments can use it to conditionally load a Node.js built-in
when it is run in Node.js, without having to deal with the resolution
error that can be thrown by `import` in a non-Node.js environment or
having to use dynamic `import()` which either turns the module into
an asynchronous module, or turns a synchronous API into an asynchronous one.

```mjs
if (globalThis.process?.getBuiltinModule) {
  // Run in Node.js, use the Node.js fs module.
  const fs = globalThis.process.getBuiltinModule('fs');
  // If `require()` is needed to load user-modules, use createRequire()
  const module = globalThis.process.getBuiltinModule('module');
  const require = module.createRequire(import.meta.url);
  const foo = require('foo');
}
```

If `id` specifies a built-in module available in the current Node.js process,
`process.getBuiltinModule(id)` method returns the corresponding built-in
module. If `id` does not correspond to any built-in module, `undefined`
is returned.

`process.getBuiltinModule(id)` accepts built-in module IDs that are recognized
by `module.isBuiltin(id)`.

The references returned by `process.getBuiltinModule(id)` always point to
the built-in module corresponding to `id` even if users modify
`require.cache` so that `require(id)` returns something else.

Contributed by Joyee Cheung in [#52762](https://github.com/nodejs/node/pull/52762)

### doc: doc-only deprecate OpenSSL engine-based APIs

OpenSSL 3 deprecated support for custom engines with a recommendation to switch to its new provider model.
The `clientCertEngine` option for `https.request()`, `tls.createSecureContext()`, and `tls.createServer()`; the `privateKeyEngine` and `privateKeyIdentifier` for `tls.createSecureContext();` and `crypto.setEngine()` all depend on this functionality from OpenSSL.

Contributed by Richard Lau in [#53329](https://github.com/nodejs/node/pull/53329)

### inspector: fix disable async hooks on Debugger.setAsyncCallStackDepth

`Debugger.setAsyncCallStackDepth` was previously calling the enable function by mistake. As a result, when profiling using Chrome DevTools, the async hooks won't be turned off properly after receiving `Debugger.setAsyncCallStackDepth` with depth 0.

Contributed by Joyee Cheung in [#53473](https://github.com/nodejs/node/pull/53473)

### Other Notable Changes

* \[[`09e2191432`](https://github.com/nodejs/node/commit/09e2191432)] - **(SEMVER-MINOR)** **buffer**: add .bytes() method to Blob (Matthew Aitken) [#53221](https://github.com/nodejs/node/pull/53221)
* \[[`394e00f41c`](https://github.com/nodejs/node/commit/394e00f41c)] - **(SEMVER-MINOR)** **doc**: add context.assert docs (Colin Ihrig) [#53169](https://github.com/nodejs/node/pull/53169)
* \[[`a8601efa5e`](https://github.com/nodejs/node/commit/a8601efa5e)] - **(SEMVER-MINOR)** **doc**: improve explanation about built-in modules (Joyee Cheung) [#52762](https://github.com/nodejs/node/pull/52762)
* \[[`5e76c258f7`](https://github.com/nodejs/node/commit/5e76c258f7)] - **doc**: add StefanStojanovic to collaborators (StefanStojanovic) [#53118](https://github.com/nodejs/node/pull/53118)
* \[[`5e694026f1`](https://github.com/nodejs/node/commit/5e694026f1)] - **doc**: add Marco Ippolito to TSC (Rafael Gonzaga) [#53008](https://github.com/nodejs/node/pull/53008)
* \[[`f3ba1eb72f`](https://github.com/nodejs/node/commit/f3ba1eb72f)] - **(SEMVER-MINOR)** **net**: add new net.server.listen tracing channel (Paolo Insogna) [#53136](https://github.com/nodejs/node/pull/53136)
* \[[`2bcce3255b`](https://github.com/nodejs/node/commit/2bcce3255b)] - **(SEMVER-MINOR)** **src,permission**: --allow-wasi & prevent WASI exec (Rafael Gonzaga) [#53124](https://github.com/nodejs/node/pull/53124)
* \[[`a03a4c7bdd`](https://github.com/nodejs/node/commit/a03a4c7bdd)] - **(SEMVER-MINOR)** **test\_runner**: add context.fullName (Colin Ihrig) [#53169](https://github.com/nodejs/node/pull/53169)
* \[[`69b828f5a5`](https://github.com/nodejs/node/commit/69b828f5a5)] - **(SEMVER-MINOR)** **util**: support `--no-` for argument with boolean type for parseArgs (Zhenwei Jin) [#53107](https://github.com/nodejs/node/pull/53107)

### Commits

* \[[`76fd0ea92e`](https://github.com/nodejs/node/commit/76fd0ea92e)] - **assert,util**: correct comparison when both contain same reference (Daniel Lemire) [#53431](https://github.com/nodejs/node/pull/53431)
* \[[`65308b6692`](https://github.com/nodejs/node/commit/65308b6692)] - **benchmark**: fix api restriction for the permission category (Ryan Tsien) [#51528](https://github.com/nodejs/node/pull/51528)
* \[[`1e2bc2c2d0`](https://github.com/nodejs/node/commit/1e2bc2c2d0)] - **benchmark**: fix napi/ref addon (Michaël Zasso) [#53233](https://github.com/nodejs/node/pull/53233)
* \[[`09e2191432`](https://github.com/nodejs/node/commit/09e2191432)] - **(SEMVER-MINOR)** **buffer**: add .bytes() method to Blob (Matthew Aitken) [#53221](https://github.com/nodejs/node/pull/53221)
* \[[`e1951a4804`](https://github.com/nodejs/node/commit/e1951a4804)] - **build**: fix spacing before NINJA\_ARGS (jakecastelli) [#53181](https://github.com/nodejs/node/pull/53181)
* \[[`76f3bb3460`](https://github.com/nodejs/node/commit/76f3bb3460)] - **build**: generate binlog in out directories (Chengzhong Wu) [#53325](https://github.com/nodejs/node/pull/53325)
* \[[`eded0c187b`](https://github.com/nodejs/node/commit/eded0c187b)] - **build**: support python 3.13 (Chengzhong Wu) [#53190](https://github.com/nodejs/node/pull/53190)
* \[[`1e57c67fdb`](https://github.com/nodejs/node/commit/1e57c67fdb)] - **build**: update ruff to v0.4.5 (Yagiz Nizipli) [#53180](https://github.com/nodejs/node/pull/53180)
* \[[`28e71ede63`](https://github.com/nodejs/node/commit/28e71ede63)] - **build**: add `--skip-tests` to `test-ci-js` target (Antoine du Hamel) [#53105](https://github.com/nodejs/node/pull/53105)
* \[[`bb06778a65`](https://github.com/nodejs/node/commit/bb06778a65)] - **build**: fix building embedtest in GN build (Cheng) [#53145](https://github.com/nodejs/node/pull/53145)
* \[[`117ff5f139`](https://github.com/nodejs/node/commit/117ff5f139)] - **build**: use broader detection for 'help' (Aviv Keller) [#53045](https://github.com/nodejs/node/pull/53045)
* \[[`9aa896e7f5`](https://github.com/nodejs/node/commit/9aa896e7f5)] - **build**: fix -j propagation to ninja (Tobias Nießen) [#53088](https://github.com/nodejs/node/pull/53088)
* \[[`acdbc78955`](https://github.com/nodejs/node/commit/acdbc78955)] - **build**: exit on unsupported host OS for Android (Mohammed Keyvanzadeh) [#52882](https://github.com/nodejs/node/pull/52882)
* \[[`bf3d94478e`](https://github.com/nodejs/node/commit/bf3d94478e)] - **build**: fix `--enable-d8` builds (Richard Lau) [#53106](https://github.com/nodejs/node/pull/53106)
* \[[`99da7d7237`](https://github.com/nodejs/node/commit/99da7d7237)] - **build**: set "clang" in config.gypi in GN build (Cheng) [#53004](https://github.com/nodejs/node/pull/53004)
* \[[`9446278f03`](https://github.com/nodejs/node/commit/9446278f03)] - **crypto**: improve GetECGroupBits signature (Tobias Nießen) [#53364](https://github.com/nodejs/node/pull/53364)
* \[[`dc2a4af68d`](https://github.com/nodejs/node/commit/dc2a4af68d)] - **crypto**: fix propagation of "memory limit exceeded" (Tobias Nießen) [#53300](https://github.com/nodejs/node/pull/53300)
* \[[`c5174f5e60`](https://github.com/nodejs/node/commit/c5174f5e60)] - **deps**: update c-ares to v1.31.0 (Node.js GitHub Bot) [#53554](https://github.com/nodejs/node/pull/53554)
* \[[`28e932dc7a`](https://github.com/nodejs/node/commit/28e932dc7a)] - **deps**: update undici to 6.19.2 (Node.js GitHub Bot) [#53468](https://github.com/nodejs/node/pull/53468)
* \[[`e4f9c663c4`](https://github.com/nodejs/node/commit/e4f9c663c4)] - **deps**: update undici to 6.19.1 (Node.js GitHub Bot) [#53468](https://github.com/nodejs/node/pull/53468)
* \[[`171dc50fdc`](https://github.com/nodejs/node/commit/171dc50fdc)] - **deps**: update undici to 6.19.1 (Node.js GitHub Bot) [#53468](https://github.com/nodejs/node/pull/53468)
* \[[`6bb6a9100d`](https://github.com/nodejs/node/commit/6bb6a9100d)] - **deps**: update undici to 6.19.0 (Node.js GitHub Bot) [#53468](https://github.com/nodejs/node/pull/53468)
* \[[`815d71b4cd`](https://github.com/nodejs/node/commit/815d71b4cd)] - **deps**: update acorn-walk to 8.3.3 (Node.js GitHub Bot) [#53466](https://github.com/nodejs/node/pull/53466)
* \[[`8b5f1d765a`](https://github.com/nodejs/node/commit/8b5f1d765a)] - **deps**: update zlib to 1.3.0.1-motley-209717d (Node.js GitHub Bot) [#53156](https://github.com/nodejs/node/pull/53156)
* \[[`fc73da6f50`](https://github.com/nodejs/node/commit/fc73da6f50)] - **deps**: update c-ares to v1.30.0 (Node.js GitHub Bot) [#53416](https://github.com/nodejs/node/pull/53416)
* \[[`a6b803abd6`](https://github.com/nodejs/node/commit/a6b803abd6)] - **deps**: update undici to 6.18.2 (Node.js GitHub Bot) [#53255](https://github.com/nodejs/node/pull/53255)
* \[[`0f235535bb`](https://github.com/nodejs/node/commit/0f235535bb)] - **deps**: update ada to 2.8.0 (Node.js GitHub Bot) [#53254](https://github.com/nodejs/node/pull/53254)
* \[[`63407269a8`](https://github.com/nodejs/node/commit/63407269a8)] - **deps**: update corepack to 0.28.2 (Node.js GitHub Bot) [#53253](https://github.com/nodejs/node/pull/53253)
* \[[`7a126e8773`](https://github.com/nodejs/node/commit/7a126e8773)] - **deps**: update c-ares to 1.29.0 (Node.js GitHub Bot) [#53155](https://github.com/nodejs/node/pull/53155)
* \[[`0c8fcceefa`](https://github.com/nodejs/node/commit/0c8fcceefa)] - **deps**: upgrade npm to 10.8.1 (npm team) [#53207](https://github.com/nodejs/node/pull/53207)
* \[[`23866979f2`](https://github.com/nodejs/node/commit/23866979f2)] - **deps**: update undici to 6.18.1 (Node.js GitHub Bot) [#53073](https://github.com/nodejs/node/pull/53073)
* \[[`4987a00142`](https://github.com/nodejs/node/commit/4987a00142)] - **deps**: update undici to 6.18.0 (Node.js GitHub Bot) [#53073](https://github.com/nodejs/node/pull/53073)
* \[[`af226d0d9c`](https://github.com/nodejs/node/commit/af226d0d9c)] - **deps**: update undici to 6.17.0 (Node.js GitHub Bot) [#53034](https://github.com/nodejs/node/pull/53034)
* \[[`c9c6bf8bfb`](https://github.com/nodejs/node/commit/c9c6bf8bfb)] - **deps**: update undici to 6.16.1 (Node.js GitHub Bot) [#52948](https://github.com/nodejs/node/pull/52948)
* \[[`b32b62d590`](https://github.com/nodejs/node/commit/b32b62d590)] - **deps**: update undici to 6.15.0 (Matthew Aitken) [#52763](https://github.com/nodejs/node/pull/52763)
* \[[`6e6641bea2`](https://github.com/nodejs/node/commit/6e6641bea2)] - **deps**: update googletest to 33af80a (Node.js GitHub Bot) [#53053](https://github.com/nodejs/node/pull/53053)
* \[[`aa96fbe03e`](https://github.com/nodejs/node/commit/aa96fbe03e)] - **deps**: update zlib to 1.3.0.1-motley-4f653ff (Node.js GitHub Bot) [#53052](https://github.com/nodejs/node/pull/53052)
* \[[`ba3310ded5`](https://github.com/nodejs/node/commit/ba3310ded5)] - **deps**: upgrade npm to 10.8.0 (npm team) [#53014](https://github.com/nodejs/node/pull/53014)
* \[[`8537a2aecf`](https://github.com/nodejs/node/commit/8537a2aecf)] - **doc**: recommend not using libuv node-api function (Michael Dawson) [#53521](https://github.com/nodejs/node/pull/53521)
* \[[`c13600f0db`](https://github.com/nodejs/node/commit/c13600f0db)] - **doc**: add additional guidance for PRs to deps (Michael Dawson) [#53499](https://github.com/nodejs/node/pull/53499)
* \[[`7c3edd952e`](https://github.com/nodejs/node/commit/7c3edd952e)] - **doc**: only apply content-visibility on all.html (Filip Skokan) [#53510](https://github.com/nodejs/node/pull/53510)
* \[[`ac5be14ed8`](https://github.com/nodejs/node/commit/ac5be14ed8)] - **doc**: update the description of the return type for options.filter (Zhenwei Jin) [#52742](https://github.com/nodejs/node/pull/52742)
* \[[`cac300e351`](https://github.com/nodejs/node/commit/cac300e351)] - **doc**: remove first timer badge (Aviv Keller) [#53338](https://github.com/nodejs/node/pull/53338)
* \[[`feb61459fd`](https://github.com/nodejs/node/commit/feb61459fd)] - **doc**: add Buffer.from(string) to functions that use buffer pool (Christian Bates-White) [#52801](https://github.com/nodejs/node/pull/52801)
* \[[`9e0a6e938b`](https://github.com/nodejs/node/commit/9e0a6e938b)] - **doc**: add initial text for ambassadors program (Michael Dawson) [#52857](https://github.com/nodejs/node/pull/52857)
* \[[`55ac53cb0b`](https://github.com/nodejs/node/commit/55ac53cb0b)] - **doc**: define more cases for stream event emissions (Aviv Keller) [#53317](https://github.com/nodejs/node/pull/53317)
* \[[`7128e0f9c9`](https://github.com/nodejs/node/commit/7128e0f9c9)] - **doc**: remove mentions of policy model from security info (Aviv Keller) [#53249](https://github.com/nodejs/node/pull/53249)
* \[[`3e290433df`](https://github.com/nodejs/node/commit/3e290433df)] - **doc**: fix mistakes in the module `load` hook api (István Donkó) [#53349](https://github.com/nodejs/node/pull/53349)
* \[[`3445c08144`](https://github.com/nodejs/node/commit/3445c08144)] - **doc**: doc-only deprecate OpenSSL engine-based APIs (Richard Lau) [#53329](https://github.com/nodejs/node/pull/53329)
* \[[`a3e8cda019`](https://github.com/nodejs/node/commit/a3e8cda019)] - **doc**: mark --heap-prof and related flags stable (Joyee Cheung) [#53343](https://github.com/nodejs/node/pull/53343)
* \[[`0b9daaae4d`](https://github.com/nodejs/node/commit/0b9daaae4d)] - **doc**: mark --cpu-prof and related flags stable (Joyee Cheung) [#53343](https://github.com/nodejs/node/pull/53343)
* \[[`daf91834f6`](https://github.com/nodejs/node/commit/daf91834f6)] - **doc**: remove IRC from man page (Tobias Nießen) [#53344](https://github.com/nodejs/node/pull/53344)
* \[[`4246c8fa31`](https://github.com/nodejs/node/commit/4246c8fa31)] - **doc**: fix broken link in `static-analysis.md` (Richard Lau) [#53345](https://github.com/nodejs/node/pull/53345)
* \[[`955b98a0e4`](https://github.com/nodejs/node/commit/955b98a0e4)] - **doc**: remove cases for keys not containing "\*" in PATTERN\_KEY\_COMPARE (Maarten Zuidhoorn) [#53215](https://github.com/nodejs/node/pull/53215)
* \[[`7832b1815f`](https://github.com/nodejs/node/commit/7832b1815f)] - **doc**: add err param to fs.cp callback (Feng Yu) [#53234](https://github.com/nodejs/node/pull/53234)
* \[[`01533df87f`](https://github.com/nodejs/node/commit/01533df87f)] - **doc**: add `err` param to fs.copyFile callback (Feng Yu) [#53234](https://github.com/nodejs/node/pull/53234)
* \[[`b081bc7d5e`](https://github.com/nodejs/node/commit/b081bc7d5e)] - **doc**: reserve 128 for Electron 32 (Keeley Hammond) [#53203](https://github.com/nodejs/node/pull/53203)
* \[[`6b8460b560`](https://github.com/nodejs/node/commit/6b8460b560)] - **doc**: add note to ninjia build for macOS using -jn flag (jakecastelli) [#53187](https://github.com/nodejs/node/pull/53187)
* \[[`394e00f41c`](https://github.com/nodejs/node/commit/394e00f41c)] - **(SEMVER-MINOR)** **doc**: add context.assert docs (Colin Ihrig) [#53169](https://github.com/nodejs/node/pull/53169)
* \[[`c143d61d0e`](https://github.com/nodejs/node/commit/c143d61d0e)] - **doc**: include ESM import for HTTP (Aviv Keller) [#53165](https://github.com/nodejs/node/pull/53165)
* \[[`a8601efa5e`](https://github.com/nodejs/node/commit/a8601efa5e)] - **(SEMVER-MINOR)** **doc**: improve explanation about built-in modules (Joyee Cheung) [#52762](https://github.com/nodejs/node/pull/52762)
* \[[`560392de3d`](https://github.com/nodejs/node/commit/560392de3d)] - **doc**: fix minor grammar and style issues in SECURITY.md (Rich Trott) [#53168](https://github.com/nodejs/node/pull/53168)
* \[[`9f8e34323d`](https://github.com/nodejs/node/commit/9f8e34323d)] - **doc**: mention pm is not enforced when using fd (Rafael Gonzaga) [#53125](https://github.com/nodejs/node/pull/53125)
* \[[`3ac775b015`](https://github.com/nodejs/node/commit/3ac775b015)] - **doc**: fix format in `esm.md` (Pop Moore) [#53170](https://github.com/nodejs/node/pull/53170)
* \[[`41b08bdcf7`](https://github.com/nodejs/node/commit/41b08bdcf7)] - **doc**: fix wrong variable name in example of `timers.tick()` (Deokjin Kim) [#53147](https://github.com/nodejs/node/pull/53147)
* \[[`698ea7aa5a`](https://github.com/nodejs/node/commit/698ea7aa5a)] - **doc**: fix wrong function name in example of `context.plan()` (Deokjin Kim) [#53140](https://github.com/nodejs/node/pull/53140)
* \[[`a99359d79d`](https://github.com/nodejs/node/commit/a99359d79d)] - **doc**: add note for windows users and symlinks (Aviv Keller) [#53117](https://github.com/nodejs/node/pull/53117)
* \[[`61ec2af292`](https://github.com/nodejs/node/commit/61ec2af292)] - **doc**: move all TLS-PSK documentation to its section (Alba Mendez) [#35717](https://github.com/nodejs/node/pull/35717)
* \[[`5e76c258f7`](https://github.com/nodejs/node/commit/5e76c258f7)] - **doc**: add StefanStojanovic to collaborators (StefanStojanovic) [#53118](https://github.com/nodejs/node/pull/53118)
* \[[`1dc406ba62`](https://github.com/nodejs/node/commit/1dc406ba62)] - **doc**: improve ninja build for --built-in-modules-path (jakecastelli) [#53007](https://github.com/nodejs/node/pull/53007)
* \[[`2854585662`](https://github.com/nodejs/node/commit/2854585662)] - **doc**: avoid hiding by navigation bar in anchor jumping (Cloyd Lau) [#45131](https://github.com/nodejs/node/pull/45131)
* \[[`3f432f829f`](https://github.com/nodejs/node/commit/3f432f829f)] - **doc**: remove unavailable youtube link in pull requests (Deokjin Kim) [#52982](https://github.com/nodejs/node/pull/52982)
* \[[`5e694026f1`](https://github.com/nodejs/node/commit/5e694026f1)] - **doc**: add Marco Ippolito to TSC (Rafael Gonzaga) [#53008](https://github.com/nodejs/node/pull/53008)
* \[[`231e44043e`](https://github.com/nodejs/node/commit/231e44043e)] - **doc**: add missing supported timer values in `timers.enable()` (Deokjin Kim) [#52969](https://github.com/nodejs/node/pull/52969)
* \[[`b8944f6938`](https://github.com/nodejs/node/commit/b8944f6938)] - **doc, http**: add `rejectNonStandardBodyWrites` option, clear its behaviour (jakecastelli) [#53396](https://github.com/nodejs/node/pull/53396)
* \[[`0354584738`](https://github.com/nodejs/node/commit/0354584738)] - **doc, meta**: organize contributing to Node-API guide (Aviv Keller) [#53243](https://github.com/nodejs/node/pull/53243)
* \[[`9ae3719c4e`](https://github.com/nodejs/node/commit/9ae3719c4e)] - **doc, meta**: use markdown rather than HTML in CONTRIBUTING.md (Aviv Keller) [#53235](https://github.com/nodejs/node/pull/53235)
* \[[`621e073c96`](https://github.com/nodejs/node/commit/621e073c96)] - **fs**: do not crash if the watched file is removed while setting up watch (Matteo Collina) [#53452](https://github.com/nodejs/node/pull/53452)
* \[[`f00ee1c377`](https://github.com/nodejs/node/commit/f00ee1c377)] - **fs**: fix cp dir/non-dir mismatch error messages (Mathis Wiehl) [#53150](https://github.com/nodejs/node/pull/53150)
* \[[`655b960418`](https://github.com/nodejs/node/commit/655b960418)] - **http2**: reject failed http2.connect when used with promisify (ehsankhfr) [#53475](https://github.com/nodejs/node/pull/53475)
* \[[`eb0b68bb29`](https://github.com/nodejs/node/commit/eb0b68bb29)] - **inspector**: fix disable async hooks on Debugger.setAsyncCallStackDepth (Joyee Cheung) [#53473](https://github.com/nodejs/node/pull/53473)
* \[[`1c0b89be4c`](https://github.com/nodejs/node/commit/1c0b89be4c)] - **lib**: fix typo in comment (codediverdev) [#53543](https://github.com/nodejs/node/pull/53543)
* \[[`55922d9cb0`](https://github.com/nodejs/node/commit/55922d9cb0)] - **lib**: remove the unused code (theanarkh) [#53463](https://github.com/nodejs/node/pull/53463)
* \[[`06374ef96b`](https://github.com/nodejs/node/commit/06374ef96b)] - **lib**: fix naming convention of `Symbol` (Deokjin Kim) [#53387](https://github.com/nodejs/node/pull/53387)
* \[[`d1a780039a`](https://github.com/nodejs/node/commit/d1a780039a)] - **lib**: fix timer leak (theanarkh) [#53337](https://github.com/nodejs/node/pull/53337)
* \[[`8689ce4b41`](https://github.com/nodejs/node/commit/8689ce4b41)] - **lib**: fix misleading argument of validateUint32 (Tobias Nießen) [#53307](https://github.com/nodejs/node/pull/53307)
* \[[`57d7bbf624`](https://github.com/nodejs/node/commit/57d7bbf624)] - **lib**: fix the name of the fetch global function (Gabriel Bota) [#53227](https://github.com/nodejs/node/pull/53227)
* \[[`23f086c363`](https://github.com/nodejs/node/commit/23f086c363)] - **lib**: do not call callback if socket is closed (theanarkh) [#52829](https://github.com/nodejs/node/pull/52829)
* \[[`f325c54c80`](https://github.com/nodejs/node/commit/f325c54c80)] - **meta**: use correct source for workflow in PR (Aviv Keller) [#53490](https://github.com/nodejs/node/pull/53490)
* \[[`8172412dbe`](https://github.com/nodejs/node/commit/8172412dbe)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#53480](https://github.com/nodejs/node/pull/53480)
* \[[`01b61d65d3`](https://github.com/nodejs/node/commit/01b61d65d3)] - **meta**: fix typo in dependency updates (Aviv Keller) [#53471](https://github.com/nodejs/node/pull/53471)
* \[[`12f5737cd3`](https://github.com/nodejs/node/commit/12f5737cd3)] - **meta**: bump step-security/harden-runner from 2.7.1 to 2.8.0 (dependabot\[bot]) [#53245](https://github.com/nodejs/node/pull/53245)
* \[[`102e4eee3c`](https://github.com/nodejs/node/commit/102e4eee3c)] - **meta**: bump ossf/scorecard-action from 2.3.1 to 2.3.3 (dependabot\[bot]) [#53248](https://github.com/nodejs/node/pull/53248)
* \[[`5ba185580d`](https://github.com/nodejs/node/commit/5ba185580d)] - **meta**: bump actions/checkout from 4.1.4 to 4.1.6 (dependabot\[bot]) [#53247](https://github.com/nodejs/node/pull/53247)
* \[[`9d186cce2b`](https://github.com/nodejs/node/commit/9d186cce2b)] - **meta**: bump github/codeql-action from 3.25.3 to 3.25.7 (dependabot\[bot]) [#53246](https://github.com/nodejs/node/pull/53246)
* \[[`29ab74009e`](https://github.com/nodejs/node/commit/29ab74009e)] - **meta**: bump codecov/codecov-action from 4.3.1 to 4.4.1 (dependabot\[bot]) [#53244](https://github.com/nodejs/node/pull/53244)
* \[[`bd4b593f30`](https://github.com/nodejs/node/commit/bd4b593f30)] - **meta**: remove `initializeCommand` from devcontainer (Aviv Keller) [#53137](https://github.com/nodejs/node/pull/53137)
* \[[`61b1f573cf`](https://github.com/nodejs/node/commit/61b1f573cf)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#53065](https://github.com/nodejs/node/pull/53065)
* \[[`f3ba1eb72f`](https://github.com/nodejs/node/commit/f3ba1eb72f)] - **(SEMVER-MINOR)** **net**: add new net.server.listen tracing channel (Paolo Insogna) [#53136](https://github.com/nodejs/node/pull/53136)
* \[[`67333a5796`](https://github.com/nodejs/node/commit/67333a5796)] - **(SEMVER-MINOR)** **process**: add process.getBuiltinModule(id) (Joyee Cheung) [#52762](https://github.com/nodejs/node/pull/52762)
* \[[`092aa09eb3`](https://github.com/nodejs/node/commit/092aa09eb3)] - **repl**: fix await object patterns without values (Luke Haas) [#53331](https://github.com/nodejs/node/pull/53331)
* \[[`554d25f526`](https://github.com/nodejs/node/commit/554d25f526)] - **src**: reset `process.versions` during pre-execution (Richard Lau) [#53444](https://github.com/nodejs/node/pull/53444)
* \[[`a0879ad628`](https://github.com/nodejs/node/commit/a0879ad628)] - **src**: fix dynamically linked OpenSSL version (Richard Lau) [#53456](https://github.com/nodejs/node/pull/53456)
* \[[`91c05f34de`](https://github.com/nodejs/node/commit/91c05f34de)] - **src**: remove `SetEncoding` from StringEncoder (Yagiz Nizipli) [#53441](https://github.com/nodejs/node/pull/53441)
* \[[`4f49384be5`](https://github.com/nodejs/node/commit/4f49384be5)] - **src**: fix typo in env.cc (EhsanKhaki) [#53418](https://github.com/nodejs/node/pull/53418)
* \[[`9730d1e186`](https://github.com/nodejs/node/commit/9730d1e186)] - **src**: avoid strcmp in favor of operator== (Tobias Nießen) [#53439](https://github.com/nodejs/node/pull/53439)
* \[[`436ad8ceb9`](https://github.com/nodejs/node/commit/436ad8ceb9)] - **src**: print v8::OOMDetails::detail when it's available (Joyee Cheung) [#53360](https://github.com/nodejs/node/pull/53360)
* \[[`f773b289eb`](https://github.com/nodejs/node/commit/f773b289eb)] - **src**: fix IsIPAddress for IPv6 (Hüseyin Açacak) [#53400](https://github.com/nodejs/node/pull/53400)
* \[[`7705efd860`](https://github.com/nodejs/node/commit/7705efd860)] - **src**: fix permission inspector crash (theanarkh) [#53389](https://github.com/nodejs/node/pull/53389)
* \[[`260d8d9ae1`](https://github.com/nodejs/node/commit/260d8d9ae1)] - **src**: use \_\_FUNCSIG\_\_ on Windows in backtrace (Joyee Cheung) [#53135](https://github.com/nodejs/node/pull/53135)
* \[[`3b79e9c24e`](https://github.com/nodejs/node/commit/3b79e9c24e)] - **src**: fix external module env and kDisableNodeOptionsEnv (Rafael Gonzaga) [#52905](https://github.com/nodejs/node/pull/52905)
* \[[`32839c63cb`](https://github.com/nodejs/node/commit/32839c63cb)] - **src**: reduce unnecessary `GetCwd` calls (Yagiz Nizipli) [#53064](https://github.com/nodejs/node/pull/53064)
* \[[`840dd092ce`](https://github.com/nodejs/node/commit/840dd092ce)] - **src**: improve node::Dotenv declarations (Tobias Nießen) [#52973](https://github.com/nodejs/node/pull/52973)
* \[[`2bcce3255b`](https://github.com/nodejs/node/commit/2bcce3255b)] - **(SEMVER-MINOR)** **src,permission**: --allow-wasi & prevent WASI exec (Rafael Gonzaga) [#53124](https://github.com/nodejs/node/pull/53124)
* \[[`e092c62a22`](https://github.com/nodejs/node/commit/e092c62a22)] - **stream**: update outdated highwatermark doc (Jay Kim) [#53494](https://github.com/nodejs/node/pull/53494)
* \[[`71af3e8172`](https://github.com/nodejs/node/commit/71af3e8172)] - **stream**: support dispose in writable (Benjamin Gruenbaum) [#48547](https://github.com/nodejs/node/pull/48547)
* \[[`33a15be32f`](https://github.com/nodejs/node/commit/33a15be32f)] - **stream**: callback should be called when pendingcb is 0 (jakecastelli) [#53438](https://github.com/nodejs/node/pull/53438)
* \[[`1b46ebbf69`](https://github.com/nodejs/node/commit/1b46ebbf69)] - **stream**: make sure \_destroy is called (jakecastelli) [#53213](https://github.com/nodejs/node/pull/53213)
* \[[`9f95d41947`](https://github.com/nodejs/node/commit/9f95d41947)] - **stream**: prevent stream unexpected pause when highWaterMark set to 0 (jakecastelli) [#53261](https://github.com/nodejs/node/pull/53261)
* \[[`d02651c9d6`](https://github.com/nodejs/node/commit/d02651c9d6)] - **stream**: micro-optimize writable condition (Orgad Shaneh) [#53189](https://github.com/nodejs/node/pull/53189)
* \[[`324070c410`](https://github.com/nodejs/node/commit/324070c410)] - **stream**: fix memory usage regression in writable (Orgad Shaneh) [#53188](https://github.com/nodejs/node/pull/53188)
* \[[`48138afd35`](https://github.com/nodejs/node/commit/48138afd35)] - **stream**: fixes for webstreams (Mattias Buelens) [#51168](https://github.com/nodejs/node/pull/51168)
* \[[`24f078a22b`](https://github.com/nodejs/node/commit/24f078a22b)] - **test**: mark `test-benchmark-crypto` as flaky (Antoine du Hamel) [#52955](https://github.com/nodejs/node/pull/52955)
* \[[`0d69ce3474`](https://github.com/nodejs/node/commit/0d69ce3474)] - **test**: extend env for `test-node-output-errors` (Richard Lau) [#53535](https://github.com/nodejs/node/pull/53535)
* \[[`1aaaad8518`](https://github.com/nodejs/node/commit/1aaaad8518)] - **test**: update encoding web-platform tests (Yagiz Nizipli) [#53477](https://github.com/nodejs/node/pull/53477)
* \[[`54e0ba8771`](https://github.com/nodejs/node/commit/54e0ba8771)] - **test**: check against run-time OpenSSL version (Richard Lau) [#53456](https://github.com/nodejs/node/pull/53456)
* \[[`059e47c320`](https://github.com/nodejs/node/commit/059e47c320)] - **test**: update tests for OpenSSL 3.0.14 (Richard Lau) [#53373](https://github.com/nodejs/node/pull/53373)
* \[[`49e6f33021`](https://github.com/nodejs/node/commit/49e6f33021)] - **test**: fix test-http-server-keepalive-req-gc (Etienne Pierre-doray) [#53292](https://github.com/nodejs/node/pull/53292)
* \[[`292d13a289`](https://github.com/nodejs/node/commit/292d13a289)] - **test**: update TLS tests for OpenSSL 3.2 (Richard Lau) [#53384](https://github.com/nodejs/node/pull/53384)
* \[[`82017c90bb`](https://github.com/nodejs/node/commit/82017c90bb)] - **test**: fix test when compiled without engine support (Richard Lau) [#53232](https://github.com/nodejs/node/pull/53232)
* \[[`a54090b385`](https://github.com/nodejs/node/commit/a54090b385)] - **test**: update TLS trace tests for OpenSSL >= 3.2 (Richard Lau) [#53229](https://github.com/nodejs/node/pull/53229)
* \[[`3a1693421d`](https://github.com/nodejs/node/commit/3a1693421d)] - **test**: fix Windows native test suites (Stefan Stojanovic) [#53173](https://github.com/nodejs/node/pull/53173)
* \[[`2b07d01272`](https://github.com/nodejs/node/commit/2b07d01272)] - **test**: skip `test-setproctitle` when `ps` is not available (Antoine du Hamel) [#53104](https://github.com/nodejs/node/pull/53104)
* \[[`0051d1c83d`](https://github.com/nodejs/node/commit/0051d1c83d)] - **test**: increase allocation so it fails for the test (Adam Majer) [#53099](https://github.com/nodejs/node/pull/53099)
* \[[`048cbe3304`](https://github.com/nodejs/node/commit/048cbe3304)] - **test**: remove timers from test-tls-socket-close (Luigi Pinca) [#53019](https://github.com/nodejs/node/pull/53019)
* \[[`8653d9223e`](https://github.com/nodejs/node/commit/8653d9223e)] - **test**: replace `.substr` with `.slice` (Antoine du Hamel) [#53070](https://github.com/nodejs/node/pull/53070)
* \[[`d74bda4241`](https://github.com/nodejs/node/commit/d74bda4241)] - **test**: add AbortController to knownGlobals (Luigi Pinca) [#53020](https://github.com/nodejs/node/pull/53020)
* \[[`f29e1e9838`](https://github.com/nodejs/node/commit/f29e1e9838)] - **test**: skip unstable shadow realm gc tests (Chengzhong Wu) [#52855](https://github.com/nodejs/node/pull/52855)
* \[[`dfa498697e`](https://github.com/nodejs/node/commit/dfa498697e)] - **test,doc**: enable running embedtest for Windows (Vladimir Morozov) [#52646](https://github.com/nodejs/node/pull/52646)
* \[[`0381817f1d`](https://github.com/nodejs/node/commit/0381817f1d)] - **test\_runner**: calculate executed lines using source map (Moshe Atlow) [#53315](https://github.com/nodejs/node/pull/53315)
* \[[`9d3699b5b0`](https://github.com/nodejs/node/commit/9d3699b5b0)] - **test\_runner**: handle file rename and deletion under watch mode (jakecastelli) [#53114](https://github.com/nodejs/node/pull/53114)
* \[[`9a36258ca0`](https://github.com/nodejs/node/commit/9a36258ca0)] - **test\_runner**: refactor to use min/max of `validateInteger` (Deokjin Kim) [#53148](https://github.com/nodejs/node/pull/53148)
* \[[`a03a4c7bdd`](https://github.com/nodejs/node/commit/a03a4c7bdd)] - **(SEMVER-MINOR)** **test\_runner**: add context.fullName (Colin Ihrig) [#53169](https://github.com/nodejs/node/pull/53169)
* \[[`a72157077a`](https://github.com/nodejs/node/commit/a72157077a)] - **test\_runner**: fix t.assert methods (Colin Ihrig) [#53049](https://github.com/nodejs/node/pull/53049)
* \[[`ba764db9ab`](https://github.com/nodejs/node/commit/ba764db9ab)] - **test\_runner**: avoid error when coverage line not found (Moshe Atlow) [#53000](https://github.com/nodejs/node/pull/53000)
* \[[`3a4a0ebd06`](https://github.com/nodejs/node/commit/3a4a0ebd06)] - **test\_runner,doc**: align documentation with actual stdout/stderr behavior (Moshe Atlow) [#53131](https://github.com/nodejs/node/pull/53131)
* \[[`6e6646bdd5`](https://github.com/nodejs/node/commit/6e6646bdd5)] - **tls**: check result of SSL\_CTX\_set\_\*\_proto\_version (Tobias Nießen) [#53459](https://github.com/nodejs/node/pull/53459)
* \[[`2aceed4297`](https://github.com/nodejs/node/commit/2aceed4297)] - **tls**: avoid taking ownership of OpenSSL objects (Tobias Nießen) [#53436](https://github.com/nodejs/node/pull/53436)
* \[[`faa5cac18c`](https://github.com/nodejs/node/commit/faa5cac18c)] - **tls**: use SSL\_get\_peer\_tmp\_key (Tobias Nießen) [#53366](https://github.com/nodejs/node/pull/53366)
* \[[`68fcbb635e`](https://github.com/nodejs/node/commit/68fcbb635e)] - **tls**: fix negative sessionTimeout handling (Tobias Nießen) [#53002](https://github.com/nodejs/node/pull/53002)
* \[[`61a1c43ef1`](https://github.com/nodejs/node/commit/61a1c43ef1)] - **tools**: fix skip detection of test runner output (Richard Lau) [#53545](https://github.com/nodejs/node/pull/53545)
* \[[`53a7b6e1c0`](https://github.com/nodejs/node/commit/53a7b6e1c0)] - **tools**: fix c-ares update script (Marco Ippolito) [#53414](https://github.com/nodejs/node/pull/53414)
* \[[`3bd5f46a15`](https://github.com/nodejs/node/commit/3bd5f46a15)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#53158](https://github.com/nodejs/node/pull/53158)
* \[[`daab9e170f`](https://github.com/nodejs/node/commit/daab9e170f)] - **tools**: do not run Corepack code before it's reviewed (Antoine du Hamel) [#53405](https://github.com/nodejs/node/pull/53405)
* \[[`d18a67f937`](https://github.com/nodejs/node/commit/d18a67f937)] - **tools**: use Ubuntu 24.04 and Clang on GitHub actions (Michaël Zasso) [#53212](https://github.com/nodejs/node/pull/53212)
* \[[`e9b7a52848`](https://github.com/nodejs/node/commit/e9b7a52848)] - **tools**: add stream label on PR when related files being changed in lib (jakecastelli) [#53269](https://github.com/nodejs/node/pull/53269)
* \[[`04d78dd56d`](https://github.com/nodejs/node/commit/04d78dd56d)] - **tools**: remove no-goma arg from make-v8 script (Michaël Zasso) [#53336](https://github.com/nodejs/node/pull/53336)
* \[[`37e725a500`](https://github.com/nodejs/node/commit/37e725a500)] - **tools**: use sccache Github action (Moshe Atlow) [#53316](https://github.com/nodejs/node/pull/53316)
* \[[`2a1fde7e32`](https://github.com/nodejs/node/commit/2a1fde7e32)] - **tools**: update error message for Type Error (Aviv Keller) [#53047](https://github.com/nodejs/node/pull/53047)
* \[[`8f5fb4192d`](https://github.com/nodejs/node/commit/8f5fb4192d)] - _**Revert**_ "**tools**: add --certify-safe to nci-ci" (Antoine du Hamel) [#53098](https://github.com/nodejs/node/pull/53098)
* \[[`69b828f5a5`](https://github.com/nodejs/node/commit/69b828f5a5)] - **(SEMVER-MINOR)** **util**: support `--no-` for argument with boolean type for parseArgs (Zhenwei Jin) [#53107](https://github.com/nodejs/node/pull/53107)
* \[[`1a2f3ab4f5`](https://github.com/nodejs/node/commit/1a2f3ab4f5)] - **watch**: fix variable naming (jakecastelli) [#53101](https://github.com/nodejs/node/pull/53101)

<a id="20.15.1"></a>

## 2024-07-08, Version 20.15.1 'Iron' (LTS), @RafaelGSS

This is a security release.

### Notable Changes

* CVE-2024-36138 - Bypass incomplete fix of CVE-2024-27980 (High)
* CVE-2024-22020 - Bypass network import restriction via data URL (Medium)
* CVE-2024-22018 - fs.lstat bypasses permission model (Low)
* CVE-2024-36137 - fs.fchown/fchmod bypasses permission model (Low)
* CVE-2024-37372 - Permission model improperly processes UNC paths (Low)

### Commits

* \[[`60e184a6e4`](https://github.com/nodejs/node/commit/60e184a6e4)] - **lib,esm**: handle bypass network-import via data: (RafaelGSS) [nodejs-private/node-private#522](https://github.com/nodejs-private/node-private/pull/522)
* \[[`025cbd6936`](https://github.com/nodejs/node/commit/025cbd6936)] - **lib,permission**: support fs.lstat (RafaelGSS) [nodejs-private/node-private#486](https://github.com/nodejs-private/node-private/pull/486)
* \[[`d38ea17341`](https://github.com/nodejs/node/commit/d38ea17341)] - **lib,permission**: disable fchmod/fchown when pm enabled (RafaelGSS) [nodejs-private/node-private#584](https://github.com/nodejs-private/node-private/pull/584)
* \[[`1ba624cd3b`](https://github.com/nodejs/node/commit/1ba624cd3b)] - **src**: handle permissive extension on cmd check (RafaelGSS) [nodejs-private/node-private#596](https://github.com/nodejs-private/node-private/pull/596)
* \[[`2524d00c3d`](https://github.com/nodejs/node/commit/2524d00c3d)] - **src,permission**: fix UNC path resolution (RafaelGSS) [nodejs-private/node-private#581](https://github.com/nodejs-private/node-private/pull/581)
* \[[`484cb0f13c`](https://github.com/nodejs/node/commit/484cb0f13c)] - **src,permission**: resolve path on fs\_permission (Rafael Gonzaga) [#52761](https://github.com/nodejs/node/pull/52761)

<a id="20.15.0"></a>

## 2024-06-20, Version 20.15.0 'Iron' (LTS), @marco-ippolito

### test\_runner: support test plans

It is now possible to count the number of assertions and subtests that are expected to run within a test. If the number of assertions and subtests that run does not match the expected count, the test will fail.

```js
test('top level test', (t) => {
  t.plan(2);
  t.assert.ok('some relevant assertion here');
  t.subtest('subtest', () => {});
});
```

Contributed by Colin Ihrig in [#52860](https://github.com/nodejs/node/pull/52860)

### inspector: introduce the `--inspect-wait` flag

This release introduces the `--inspect-wait` flag, which allows debugger to wait for attachement. This flag is useful when you want to debug the code from the beginning. Unlike `--inspect-brk`, which breaks on the first line, this flag waits for debugger to be connected and then runs the code as soon as a session is established.

Contributed by Kohei Ueno in [#52734](https://github.com/nodejs/node/pull/52734)

### zlib: expose zlib.crc32()

This release exposes the crc32() function from zlib to user-land.

It computes a 32-bit Cyclic Redundancy Check checksum of data. If
value is specified, it is used as the starting value of the checksum,
otherwise, 0 is used as the starting value.

The CRC algorithm is designed to compute checksums and to detect error
in data transmission. It's not suitable for cryptographic authentication.

```js
const zlib = require('node:zlib');
const { Buffer } = require('node:buffer');

let crc = zlib.crc32('hello');  // 907060870
crc = zlib.crc32('world', crc);  // 4192936109

crc = zlib.crc32(Buffer.from('hello', 'utf16le'));  // 1427272415
crc = zlib.crc32(Buffer.from('world', 'utf16le'), crc);  // 4150509955
```

Contributed by Joyee Cheung in [#52692](https://github.com/nodejs/node/pull/52692)

### cli: allow running wasm in limited vmem with --disable-wasm-trap-handler

By default, Node.js enables trap-handler-based WebAssembly bound
checks. As a result, V8 does not need to insert inline bound checks
int the code compiled from WebAssembly which may speedup WebAssembly
execution significantly, but this optimization requires allocating
a big virtual memory cage (currently 10GB). If the Node.js process
does not have access to a large enough virtual memory address space
due to system configurations or hardware limitations, users won't
be able to run any WebAssembly that involves allocation in this
virtual memory cage and will see an out-of-memory error.

```console
$ ulimit -v 5000000
$ node -p "new WebAssembly.Memory({ initial: 10, maximum: 100 });"
[eval]:1
new WebAssembly.Memory({ initial: 10, maximum: 100 });
^

RangeError: WebAssembly.Memory(): could not allocate memory
    at [eval]:1:1
    at runScriptInThisContext (node:internal/vm:209:10)
    at node:internal/process/execution:118:14
    at [eval]-wrapper:6:24
    at runScript (node:internal/process/execution:101:62)
    at evalScript (node:internal/process/execution:136:3)
    at node:internal/main/eval_string:49:3

```

`--disable-wasm-trap-handler` disables this optimization so that
users can at least run WebAssembly (with a less optimial performance)
when the virtual memory address space available to their Node.js
process is lower than what the V8 WebAssembly memory cage needs.

Contributed by Joyee Cheung in [#52766](https://github.com/nodejs/node/pull/52766)

### Other Notable Changes

* \[[`12512c3d0e`](https://github.com/nodejs/node/commit/12512c3d0e)] - **doc**: add pimterry to collaborators (Tim Perry) [#52874](https://github.com/nodejs/node/pull/52874)
* \[[`9d485b40bb`](https://github.com/nodejs/node/commit/9d485b40bb)] - **(SEMVER-MINOR)** **tools**: fix get\_asan\_state() in tools/test.py (Joyee Cheung) [#52766](https://github.com/nodejs/node/pull/52766)
* \[[`e98c305f52`](https://github.com/nodejs/node/commit/e98c305f52)] - **(SEMVER-MINOR)** **tools**: support max\_virtual\_memory test configuration (Joyee Cheung) [#52766](https://github.com/nodejs/node/pull/52766)
* \[[`dce0300896`](https://github.com/nodejs/node/commit/dce0300896)] - **(SEMVER-MINOR)** **tools**: support != in test status files (Joyee Cheung) [#52766](https://github.com/nodejs/node/pull/52766)

### Commits

* \[[`227093bfec`](https://github.com/nodejs/node/commit/227093bfec)] - **assert**: add deep equal check for more Error type (Zhenwei Jin) [#51805](https://github.com/nodejs/node/pull/51805)
* \[[`184cfe5a71`](https://github.com/nodejs/node/commit/184cfe5a71)] - **benchmark**: filter non-present deps from `start-cli-version` (Adam Majer) [#51746](https://github.com/nodejs/node/pull/51746)
* \[[`8b3e83bb53`](https://github.com/nodejs/node/commit/8b3e83bb53)] - **buffer**: even faster atob (Daniel Lemire) [#52443](https://github.com/nodejs/node/pull/52443)
* \[[`8d628c3255`](https://github.com/nodejs/node/commit/8d628c3255)] - **buffer**: use size\_t instead of uint32\_t to avoid segmentation fault (Xavier Stouder) [#48033](https://github.com/nodejs/node/pull/48033)
* \[[`16ae2b2933`](https://github.com/nodejs/node/commit/16ae2b2933)] - **buffer**: remove lines setting indexes to integer value (Zhenwei Jin) [#52588](https://github.com/nodejs/node/pull/52588)
* \[[`48c15d0dcd`](https://github.com/nodejs/node/commit/48c15d0dcd)] - **build**: remove deprecated calls for argument groups (Mohammed Keyvanzadeh) [#52913](https://github.com/nodejs/node/pull/52913)
* \[[`1be8232d17`](https://github.com/nodejs/node/commit/1be8232d17)] - **build**: drop base64 dep in GN build (Cheng) [#52856](https://github.com/nodejs/node/pull/52856)
* \[[`918962d6e7`](https://github.com/nodejs/node/commit/918962d6e7)] - **build**: make simdjson a public dep in GN build (Cheng) [#52755](https://github.com/nodejs/node/pull/52755)
* \[[`5215b6fd8e`](https://github.com/nodejs/node/commit/5215b6fd8e)] - **build, tools**: copy release assets to staging R2 bucket once built (flakey5) [#51394](https://github.com/nodejs/node/pull/51394)
* \[[`473fa73857`](https://github.com/nodejs/node/commit/473fa73857)] - **(SEMVER-MINOR)** **cli**: allow running wasm in limited vmem with --disable-wasm-trap-handler (Joyee Cheung) [#52766](https://github.com/nodejs/node/pull/52766)
* \[[`954d2aded4`](https://github.com/nodejs/node/commit/954d2aded4)] - **cluster**: replace `forEach` with `for-of` loop (Jérôme Benoit) [#50317](https://github.com/nodejs/node/pull/50317)
* \[[`794e450ea7`](https://github.com/nodejs/node/commit/794e450ea7)] - **console**: colorize console error and warn (Jithil P Ponnan) [#51629](https://github.com/nodejs/node/pull/51629)
* \[[`0fb7c18f10`](https://github.com/nodejs/node/commit/0fb7c18f10)] - **crypto**: fix duplicated switch-case return values (Mustafa Ateş UZUN) [#49030](https://github.com/nodejs/node/pull/49030)
* \[[`cd1415c8b2`](https://github.com/nodejs/node/commit/cd1415c8b2)] - _**Revert**_ "**crypto**: make timingSafeEqual faster for Uint8Array" (Tobias Nießen) [#53390](https://github.com/nodejs/node/pull/53390)
* \[[`b774544bb1`](https://github.com/nodejs/node/commit/b774544bb1)] - **deps**: enable unbundling of simdjson, simdutf, ada (Daniel Lemire) [#52924](https://github.com/nodejs/node/pull/52924)
* \[[`da4dbfc5fd`](https://github.com/nodejs/node/commit/da4dbfc5fd)] - **doc**: remove reference to AUTHORS file (Marco Ippolito) [#52960](https://github.com/nodejs/node/pull/52960)
* \[[`2f3f2ff8af`](https://github.com/nodejs/node/commit/2f3f2ff8af)] - **doc**: update hljs with the latest styles (Aviv Keller) [#52911](https://github.com/nodejs/node/pull/52911)
* \[[`3a1d17a9b1`](https://github.com/nodejs/node/commit/3a1d17a9b1)] - **doc**: mention quicker way to build docs (Alex Crawford) [#52937](https://github.com/nodejs/node/pull/52937)
* \[[`be309bd19d`](https://github.com/nodejs/node/commit/be309bd19d)] - **doc**: mention push.followTags config (Rafael Gonzaga) [#52906](https://github.com/nodejs/node/pull/52906)
* \[[`e62c6e2684`](https://github.com/nodejs/node/commit/e62c6e2684)] - **doc**: document pipeline with `end` option (Alois Klink) [#48970](https://github.com/nodejs/node/pull/48970)
* \[[`af27225cf6`](https://github.com/nodejs/node/commit/af27225cf6)] - **doc**: add example for `execFileSync` method and ref to stdio (Evan Shortiss) [#39412](https://github.com/nodejs/node/pull/39412)
* \[[`086626f9b1`](https://github.com/nodejs/node/commit/086626f9b1)] - **doc**: add examples and notes to http server.close et al (mary marchini) [#49091](https://github.com/nodejs/node/pull/49091)
* \[[`3aa3337a00`](https://github.com/nodejs/node/commit/3aa3337a00)] - **doc**: fix `dns.lookup` family `0` and `all` descriptions (Adam Jones) [#51653](https://github.com/nodejs/node/pull/51653)
* \[[`585f2a2e7f`](https://github.com/nodejs/node/commit/585f2a2e7f)] - **doc**: update `fs.realpath` documentation (sinkhaha) [#48170](https://github.com/nodejs/node/pull/48170)
* \[[`4bf3d44e1d`](https://github.com/nodejs/node/commit/4bf3d44e1d)] - **doc**: update fs read documentation for clarity (Mert Can Altin) [#52453](https://github.com/nodejs/node/pull/52453)
* \[[`ae5d47dde3`](https://github.com/nodejs/node/commit/ae5d47dde3)] - **doc**: watermark string behavior (Benjamin Gruenbaum) [#52842](https://github.com/nodejs/node/pull/52842)
* \[[`1e429d10d3`](https://github.com/nodejs/node/commit/1e429d10d3)] - **doc**: exclude commits with baking-for-lts (Marco Ippolito) [#52896](https://github.com/nodejs/node/pull/52896)
* \[[`3df3e37cdb`](https://github.com/nodejs/node/commit/3df3e37cdb)] - **doc**: add names next to release key bash commands (Aviv Keller) [#52878](https://github.com/nodejs/node/pull/52878)
* \[[`12512c3d0e`](https://github.com/nodejs/node/commit/12512c3d0e)] - **doc**: add pimterry to collaborators (Tim Perry) [#52874](https://github.com/nodejs/node/pull/52874)
* \[[`97e0fef019`](https://github.com/nodejs/node/commit/97e0fef019)] - **doc**: add more definitions to GLOSSARY.md (Aviv Keller) [#52798](https://github.com/nodejs/node/pull/52798)
* \[[`91fadac162`](https://github.com/nodejs/node/commit/91fadac162)] - **doc**: make docs more welcoming and descriptive for newcomers (Serkan Özel) [#38056](https://github.com/nodejs/node/pull/38056)
* \[[`a3b20126fd`](https://github.com/nodejs/node/commit/a3b20126fd)] - **doc**: add OpenSSL errors to API docs (John Lamp) [#34213](https://github.com/nodejs/node/pull/34213)
* \[[`9587ae9b5b`](https://github.com/nodejs/node/commit/9587ae9b5b)] - **doc**: simplify copy-pasting of `branch-diff` commands (Antoine du Hamel) [#52757](https://github.com/nodejs/node/pull/52757)
* \[[`6ea72a53c3`](https://github.com/nodejs/node/commit/6ea72a53c3)] - **doc**: add test\_runner to subsystem (Raz Luvaton) [#52774](https://github.com/nodejs/node/pull/52774)
* \[[`972eafd983`](https://github.com/nodejs/node/commit/972eafd983)] - **events**: update MaxListenersExceededWarning message log (sinkhaha) [#51921](https://github.com/nodejs/node/pull/51921)
* \[[`74753ed1fe`](https://github.com/nodejs/node/commit/74753ed1fe)] - **events**: add stop propagation flag to `Event.stopImmediatePropagation` (Mickael Meausoone) [#39463](https://github.com/nodejs/node/pull/39463)
* \[[`75dd009649`](https://github.com/nodejs/node/commit/75dd009649)] - **events**: replace NodeCustomEvent with CustomEvent (Feng Yu) [#43876](https://github.com/nodejs/node/pull/43876)
* \[[`7d38c2e012`](https://github.com/nodejs/node/commit/7d38c2e012)] - **fs**: keep fs.promises.readFile read until EOF is reached (Zhenwei Jin) [#52178](https://github.com/nodejs/node/pull/52178)
* \[[`8cb13120d3`](https://github.com/nodejs/node/commit/8cb13120d3)] - **(SEMVER-MINOR)** **inspector**: introduce the `--inspect-wait` flag (Kohei Ueno) [#52734](https://github.com/nodejs/node/pull/52734)
* \[[`d5ab1de1fd`](https://github.com/nodejs/node/commit/d5ab1de1fd)] - **meta**: move `@anonrig` to TSC regular member (Yagiz Nizipli) [#52932](https://github.com/nodejs/node/pull/52932)
* \[[`f82d086e90`](https://github.com/nodejs/node/commit/f82d086e90)] - **path**: fix toNamespacedPath on Windows (Hüseyin Açacak) [#52915](https://github.com/nodejs/node/pull/52915)
* \[[`121ea13b50`](https://github.com/nodejs/node/commit/121ea13b50)] - **process**: improve event-loop (Aras Abbasi) [#52108](https://github.com/nodejs/node/pull/52108)
* \[[`eceac784aa`](https://github.com/nodejs/node/commit/eceac784aa)] - **repl**: fix disruptive autocomplete without inspector (Nitzan Uziely) [#40661](https://github.com/nodejs/node/pull/40661)
* \[[`89a910be82`](https://github.com/nodejs/node/commit/89a910be82)] - **src**: fix Worker termination in `inspector.waitForDebugger` (Daeyeon Jeong) [#52527](https://github.com/nodejs/node/pull/52527)
* \[[`033f985e8a`](https://github.com/nodejs/node/commit/033f985e8a)] - **src**: use `S_ISDIR` to check if the file is a directory (theanarkh) [#52164](https://github.com/nodejs/node/pull/52164)
* \[[`95128399f8`](https://github.com/nodejs/node/commit/95128399f8)] - **src**: allow preventing debug signal handler start (Shelley Vohr) [#46681](https://github.com/nodejs/node/pull/46681)
* \[[`b162aeae9e`](https://github.com/nodejs/node/commit/b162aeae9e)] - **src**: fix typo Unabled -> Unable (Simon Siefke) [#52820](https://github.com/nodejs/node/pull/52820)
* \[[`2dcbf1894a`](https://github.com/nodejs/node/commit/2dcbf1894a)] - **src**: avoid unused variable 'error' warning (Michaël Zasso) [#52886](https://github.com/nodejs/node/pull/52886)
* \[[`978ee0a635`](https://github.com/nodejs/node/commit/978ee0a635)] - **src**: only apply fix in main thread (Paolo Insogna) [#52702](https://github.com/nodejs/node/pull/52702)
* \[[`8fc52b38c6`](https://github.com/nodejs/node/commit/8fc52b38c6)] - **src**: fix test local edge case (Paolo Insogna) [#52702](https://github.com/nodejs/node/pull/52702)
* \[[`d02907ecc4`](https://github.com/nodejs/node/commit/d02907ecc4)] - **src**: remove misplaced windows code under posix guard in node.cc (Ali Hassan) [#52545](https://github.com/nodejs/node/pull/52545)
* \[[`af29120fa7`](https://github.com/nodejs/node/commit/af29120fa7)] - **stream**: use `ByteLengthQueuingStrategy` when not in `objectMode` (Jason) [#48847](https://github.com/nodejs/node/pull/48847)
* \[[`a5f3dd137c`](https://github.com/nodejs/node/commit/a5f3dd137c)] - **string\_decoder**: throw an error when writing a too long buffer (zhenweijin) [#52215](https://github.com/nodejs/node/pull/52215)
* \[[`65fa95d57d`](https://github.com/nodejs/node/commit/65fa95d57d)] - **test**: add `Debugger.setInstrumentationBreakpoint` known issue (Konstantin Ulitin) [#31137](https://github.com/nodejs/node/pull/31137)
* \[[`0513e07805`](https://github.com/nodejs/node/commit/0513e07805)] - **test**: use `for-of` instead of `forEach` (Gibby Free) [#49790](https://github.com/nodejs/node/pull/49790)
* \[[`1d01325928`](https://github.com/nodejs/node/commit/1d01325928)] - **test**: verify request payload is uploaded consistently (Austin Wright) [#34066](https://github.com/nodejs/node/pull/34066)
* \[[`7dda156872`](https://github.com/nodejs/node/commit/7dda156872)] - **test**: add fuzzer for native/js string conversion (Adam Korczynski) [#51120](https://github.com/nodejs/node/pull/51120)
* \[[`5fb829b340`](https://github.com/nodejs/node/commit/5fb829b340)] - **test**: add fuzzer for `ClientHelloParser` (AdamKorcz) [#51088](https://github.com/nodejs/node/pull/51088)
* \[[`cc74bf789f`](https://github.com/nodejs/node/commit/cc74bf789f)] - **test**: fix broken env fuzzer by initializing process (AdamKorcz) [#51080](https://github.com/nodejs/node/pull/51080)
* \[[`800b6f65cf`](https://github.com/nodejs/node/commit/800b6f65cf)] - **test**: replace `forEach()` in `test-stream-pipe-unpipe-stream` (Dario) [#50786](https://github.com/nodejs/node/pull/50786)
* \[[`d08c9a6a31`](https://github.com/nodejs/node/commit/d08c9a6a31)] - **test**: test pipeline `end` on transform streams (Alois Klink) [#48970](https://github.com/nodejs/node/pull/48970)
* \[[`0be8123ede`](https://github.com/nodejs/node/commit/0be8123ede)] - **test**: improve coverage of lib/readline.js (Rongjian Zhang) [#38646](https://github.com/nodejs/node/pull/38646)
* \[[`410224415c`](https://github.com/nodejs/node/commit/410224415c)] - **test**: updated for each to for of in test file (lyannel) [#50308](https://github.com/nodejs/node/pull/50308)
* \[[`556e9a2127`](https://github.com/nodejs/node/commit/556e9a2127)] - **test**: move `test-http-server-request-timeouts-mixed` to sequential (Madhuri) [#45722](https://github.com/nodejs/node/pull/45722)
* \[[`0638274c07`](https://github.com/nodejs/node/commit/0638274c07)] - **test**: fix DNS cancel tests (Szymon Marczak) [#44432](https://github.com/nodejs/node/pull/44432)
* \[[`311bdc62bd`](https://github.com/nodejs/node/commit/311bdc62bd)] - **test**: add http agent to `executionAsyncResource` (psj-tar-gz) [#34966](https://github.com/nodejs/node/pull/34966)
* \[[`6001b164ab`](https://github.com/nodejs/node/commit/6001b164ab)] - **test**: reduce memory usage of test-worker-stdio (Adam Majer) [#37769](https://github.com/nodejs/node/pull/37769)
* \[[`986bfa26e9`](https://github.com/nodejs/node/commit/986bfa26e9)] - **test**: add common.expectRequiredModule() (Joyee Cheung) [#52868](https://github.com/nodejs/node/pull/52868)
* \[[`2246d4fd1e`](https://github.com/nodejs/node/commit/2246d4fd1e)] - **test**: crypto-rsa-dsa testing for dynamic openssl (Michael Dawson) [#52781](https://github.com/nodejs/node/pull/52781)
* \[[`1dce5dea0b`](https://github.com/nodejs/node/commit/1dce5dea0b)] - **test**: skip some console tests on dumb terminal (Adam Majer) [#37770](https://github.com/nodejs/node/pull/37770)
* \[[`0addeb240c`](https://github.com/nodejs/node/commit/0addeb240c)] - **test**: skip v8-updates/test-linux-perf-logger (Michaël Zasso) [#52821](https://github.com/nodejs/node/pull/52821)
* \[[`56e19e38f3`](https://github.com/nodejs/node/commit/56e19e38f3)] - **test**: drop test-crypto-timing-safe-equal-benchmarks (Rafael Gonzaga) [#52751](https://github.com/nodejs/node/pull/52751)
* \[[`0c5e58958c`](https://github.com/nodejs/node/commit/0c5e58958c)] - **test, crypto**: use correct object on assert (响马) [#51820](https://github.com/nodejs/node/pull/51820)
* \[[`d54aa47ec1`](https://github.com/nodejs/node/commit/d54aa47ec1)] - **(SEMVER-MINOR)** **test\_runner**: support test plans (Colin Ihrig) [#52860](https://github.com/nodejs/node/pull/52860)
* \[[`0289a023a5`](https://github.com/nodejs/node/commit/0289a023a5)] - **test\_runner**: fix watch mode race condition (Moshe Atlow) [#52954](https://github.com/nodejs/node/pull/52954)
* \[[`cf817e192e`](https://github.com/nodejs/node/commit/cf817e192e)] - **test\_runner**: preserve hook promise when executed twice (Moshe Atlow) [#52791](https://github.com/nodejs/node/pull/52791)
* \[[`de541235fe`](https://github.com/nodejs/node/commit/de541235fe)] - **tools**: fix v8-update workflow (Michaël Zasso) [#52957](https://github.com/nodejs/node/pull/52957)
* \[[`f6290bc327`](https://github.com/nodejs/node/commit/f6290bc327)] - **tools**: add --certify-safe to nci-ci (Matteo Collina) [#52940](https://github.com/nodejs/node/pull/52940)
* \[[`0830b3115d`](https://github.com/nodejs/node/commit/0830b3115d)] - **tools**: fix doc update action (Marco Ippolito) [#52890](https://github.com/nodejs/node/pull/52890)
* \[[`9d485b40bb`](https://github.com/nodejs/node/commit/9d485b40bb)] - **(SEMVER-MINOR)** **tools**: fix get\_asan\_state() in tools/test.py (Joyee Cheung) [#52766](https://github.com/nodejs/node/pull/52766)
* \[[`e98c305f52`](https://github.com/nodejs/node/commit/e98c305f52)] - **(SEMVER-MINOR)** **tools**: support max\_virtual\_memory test configuration (Joyee Cheung) [#52766](https://github.com/nodejs/node/pull/52766)
* \[[`dce0300896`](https://github.com/nodejs/node/commit/dce0300896)] - **(SEMVER-MINOR)** **tools**: support != in test status files (Joyee Cheung) [#52766](https://github.com/nodejs/node/pull/52766)
* \[[`57006001ec`](https://github.com/nodejs/node/commit/57006001ec)] - **tools**: prepare custom rules for ESLint v9 (Michaël Zasso) [#52889](https://github.com/nodejs/node/pull/52889)
* \[[`403a4a7557`](https://github.com/nodejs/node/commit/403a4a7557)] - **tools**: update lint-md-dependencies to rollup\@4.17.2 (Node.js GitHub Bot) [#52836](https://github.com/nodejs/node/pull/52836)
* \[[`01eff5860e`](https://github.com/nodejs/node/commit/01eff5860e)] - **tools**: update `gr2m/create-or-update-pull-request-action` (Antoine du Hamel) [#52843](https://github.com/nodejs/node/pull/52843)
* \[[`514f01ed59`](https://github.com/nodejs/node/commit/514f01ed59)] - **tools**: use sccache GitHub action (Michaël Zasso) [#52839](https://github.com/nodejs/node/pull/52839)
* \[[`8f8fb91927`](https://github.com/nodejs/node/commit/8f8fb91927)] - **tools**: specify a commit-message for V8 update workflow (Antoine du Hamel) [#52844](https://github.com/nodejs/node/pull/52844)
* \[[`b83fbf8709`](https://github.com/nodejs/node/commit/b83fbf8709)] - **tools**: fix V8 update workflow (Antoine du Hamel) [#52822](https://github.com/nodejs/node/pull/52822)
* \[[`be9d6f2176`](https://github.com/nodejs/node/commit/be9d6f2176)] - **url,tools,benchmark**: replace deprecated `substr()` (Jungku Lee) [#51546](https://github.com/nodejs/node/pull/51546)
* \[[`7603a51d45`](https://github.com/nodejs/node/commit/7603a51d45)] - **util**: fix `%s` format behavior with `Symbol.toPrimitive` (Chenyu Yang) [#50992](https://github.com/nodejs/node/pull/50992)
* \[[`d7eba50cf3`](https://github.com/nodejs/node/commit/d7eba50cf3)] - **util**: improve `isInsideNodeModules` (uzlopak) [#52147](https://github.com/nodejs/node/pull/52147)
* \[[`4ae4f7e517`](https://github.com/nodejs/node/commit/4ae4f7e517)] - **watch**: allow listening for grouped changes (Matthieu Sieben) [#52722](https://github.com/nodejs/node/pull/52722)
* \[[`1ff8f318c0`](https://github.com/nodejs/node/commit/1ff8f318c0)] - **watch**: enable passthrough ipc in watch mode (Zack) [#50890](https://github.com/nodejs/node/pull/50890)
* \[[`739adf90b1`](https://github.com/nodejs/node/commit/739adf90b1)] - **watch**: fix arguments parsing (Moshe Atlow) [#52760](https://github.com/nodejs/node/pull/52760)
* \[[`5161d95c30`](https://github.com/nodejs/node/commit/5161d95c30)] - **(SEMVER-MINOR)** **zlib**: expose zlib.crc32() (Joyee Cheung) [#52692](https://github.com/nodejs/node/pull/52692)

<a id="20.14.0"></a>

## 2024-05-28, Version 20.14.0 'Iron' (LTS), @marco-ippolito

### Notable Changes

* \[[`28d2baa17c`](https://github.com/nodejs/node/commit/28d2baa17c)] - **src,permission**: throw async errors on async APIs (Rafael Gonzaga) [#52730](https://github.com/nodejs/node/pull/52730)
* \[[`77e2bf029a`](https://github.com/nodejs/node/commit/77e2bf029a)] - **(SEMVER-MINOR)** **test\_runner**: support forced exit (Colin Ihrig) [#52038](https://github.com/nodejs/node/pull/52038)

### Commits

* \[[`e3ad05d8b0`](https://github.com/nodejs/node/commit/e3ad05d8b0)] - **deps**: V8: cherry-pick 500de8bd371b (Richard Lau) [#52676](https://github.com/nodejs/node/pull/52676)
* \[[`053282e661`](https://github.com/nodejs/node/commit/053282e661)] - **deps**: V8: backport c4be0a97f981 (Richard Lau) [#52183](https://github.com/nodejs/node/pull/52183)
* \[[`200dadb879`](https://github.com/nodejs/node/commit/200dadb879)] - **deps**: V8: cherry-pick f8d5e576b814 (Richard Lau) [#52183](https://github.com/nodejs/node/pull/52183)
* \[[`f5cd125e02`](https://github.com/nodejs/node/commit/f5cd125e02)] - **deps**: update googletest to fa6de7f (Node.js GitHub Bot) [#52949](https://github.com/nodejs/node/pull/52949)
* \[[`bbbfd7f4e1`](https://github.com/nodejs/node/commit/bbbfd7f4e1)] - **deps**: update corepack to 0.28.1 (Node.js GitHub Bot) [#52946](https://github.com/nodejs/node/pull/52946)
* \[[`7ba30a57a6`](https://github.com/nodejs/node/commit/7ba30a57a6)] - **deps**: update simdutf to 5.2.8 (Node.js GitHub Bot) [#52727](https://github.com/nodejs/node/pull/52727)
* \[[`b21a480a28`](https://github.com/nodejs/node/commit/b21a480a28)] - **deps**: update simdutf to 5.2.6 (Node.js GitHub Bot) [#52727](https://github.com/nodejs/node/pull/52727)
* \[[`6cfad60d97`](https://github.com/nodejs/node/commit/6cfad60d97)] - **deps**: update googletest to 2d16ed0 (Node.js GitHub Bot) [#51657](https://github.com/nodejs/node/pull/51657)
* \[[`34708d1429`](https://github.com/nodejs/node/commit/34708d1429)] - **deps**: update googletest to d83fee1 (Node.js GitHub Bot) [#51657](https://github.com/nodejs/node/pull/51657)
* \[[`c1d3e558e8`](https://github.com/nodejs/node/commit/c1d3e558e8)] - **deps**: update googletest to 5a37b51 (Node.js GitHub Bot) [#51657](https://github.com/nodejs/node/pull/51657)
* \[[`69959d0fca`](https://github.com/nodejs/node/commit/69959d0fca)] - **deps**: update googletest to 5197b1a (Node.js GitHub Bot) [#51657](https://github.com/nodejs/node/pull/51657)
* \[[`c8305f6057`](https://github.com/nodejs/node/commit/c8305f6057)] - **deps**: update googletest to eff443c (Node.js GitHub Bot) [#51657](https://github.com/nodejs/node/pull/51657)
* \[[`760b788704`](https://github.com/nodejs/node/commit/760b788704)] - **deps**: update googletest to c231e6f (Node.js GitHub Bot) [#51657](https://github.com/nodejs/node/pull/51657)
* \[[`301541cc8f`](https://github.com/nodejs/node/commit/301541cc8f)] - **deps**: update googletest to e4fdb87 (Node.js GitHub Bot) [#51657](https://github.com/nodejs/node/pull/51657)
* \[[`981d57e401`](https://github.com/nodejs/node/commit/981d57e401)] - **deps**: update googletest to 5df0241 (Node.js GitHub Bot) [#51657](https://github.com/nodejs/node/pull/51657)
* \[[`a1817f534d`](https://github.com/nodejs/node/commit/a1817f534d)] - **deps**: update googletest to b75ecf1 (Node.js GitHub Bot) [#51657](https://github.com/nodejs/node/pull/51657)
* \[[`42070ca189`](https://github.com/nodejs/node/commit/42070ca189)] - **deps**: update googletest to 4565741 (Node.js GitHub Bot) [#51657](https://github.com/nodejs/node/pull/51657)
* \[[`edc3e5d056`](https://github.com/nodejs/node/commit/edc3e5d056)] - **deps**: update uvwasi to 0.0.21 (Node.js GitHub Bot) [#52863](https://github.com/nodejs/node/pull/52863)
* \[[`26b1231ffb`](https://github.com/nodejs/node/commit/26b1231ffb)] - **deps**: upgrade npm to 10.7.0 (npm team) [#52767](https://github.com/nodejs/node/pull/52767)
* \[[`e6d9fbece2`](https://github.com/nodejs/node/commit/e6d9fbece2)] - **doc**: update process.versions properties (ishabi) [#52736](https://github.com/nodejs/node/pull/52736)
* \[[`8c1f837c0a`](https://github.com/nodejs/node/commit/8c1f837c0a)] - **doc**: remove mold use on mac for speeding up build (Cong Zhang) [#52252](https://github.com/nodejs/node/pull/52252)
* \[[`d9c5114694`](https://github.com/nodejs/node/commit/d9c5114694)] - **doc**: fix grammatical mistake (codershiba) [#52808](https://github.com/nodejs/node/pull/52808)
* \[[`b350f435b7`](https://github.com/nodejs/node/commit/b350f435b7)] - **meta**: add mailmap entry for legendecas (Chengzhong Wu) [#52795](https://github.com/nodejs/node/pull/52795)
* \[[`61f9f12eff`](https://github.com/nodejs/node/commit/61f9f12eff)] - **meta**: bump actions/checkout from 4.1.1 to 4.1.4 (dependabot\[bot]) [#52787](https://github.com/nodejs/node/pull/52787)
* \[[`ac563667d6`](https://github.com/nodejs/node/commit/ac563667d6)] - **meta**: bump github/codeql-action from 3.24.9 to 3.25.3 (dependabot\[bot]) [#52786](https://github.com/nodejs/node/pull/52786)
* \[[`70611d7924`](https://github.com/nodejs/node/commit/70611d7924)] - **meta**: bump actions/upload-artifact from 4.3.1 to 4.3.3 (dependabot\[bot]) [#52785](https://github.com/nodejs/node/pull/52785)
* \[[`30482ea273`](https://github.com/nodejs/node/commit/30482ea273)] - **meta**: bump actions/download-artifact from 4.1.4 to 4.1.7 (dependabot\[bot]) [#52784](https://github.com/nodejs/node/pull/52784)
* \[[`d1607cdebb`](https://github.com/nodejs/node/commit/d1607cdebb)] - **meta**: bump codecov/codecov-action from 4.1.1 to 4.3.1 (dependabot\[bot]) [#52783](https://github.com/nodejs/node/pull/52783)
* \[[`21f1b6bfc3`](https://github.com/nodejs/node/commit/21f1b6bfc3)] - **meta**: bump step-security/harden-runner from 2.7.0 to 2.7.1 (dependabot\[bot]) [#52782](https://github.com/nodejs/node/pull/52782)
* \[[`0c6019a222`](https://github.com/nodejs/node/commit/0c6019a222)] - **meta**: standardize regex (Aviv Keller) [#52693](https://github.com/nodejs/node/pull/52693)
* \[[`28d2baa17c`](https://github.com/nodejs/node/commit/28d2baa17c)] - **src,permission**: throw async errors on async APIs (Rafael Gonzaga) [#52730](https://github.com/nodejs/node/pull/52730)
* \[[`cffd2cc0c9`](https://github.com/nodejs/node/commit/cffd2cc0c9)] - _**Revert**_ "**stream**: revert fix cloned webstreams not being unref'd" (Marco Ippolito) [#53144](https://github.com/nodejs/node/pull/53144)
* \[[`3dd96f1fab`](https://github.com/nodejs/node/commit/3dd96f1fab)] - **stream**: implement TransformStream cleanup using "transformer.cancel" (Debadree Chatterjee) [#50126](https://github.com/nodejs/node/pull/50126)
* \[[`8e7e778e01`](https://github.com/nodejs/node/commit/8e7e778e01)] - **test**: skip v8-updates/test-linux-perf (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`f8e18869e9`](https://github.com/nodejs/node/commit/f8e18869e9)] - **test**: replace always-opt flag with alway-turbofan (Michaël Zasso) [#50115](https://github.com/nodejs/node/pull/50115)
* \[[`a501860d63`](https://github.com/nodejs/node/commit/a501860d63)] - **test\_runner**: don't await the same promise for each test (Colin Ihrig) [#52185](https://github.com/nodejs/node/pull/52185)
* \[[`e2ae4367f4`](https://github.com/nodejs/node/commit/e2ae4367f4)] - **test\_runner**: run top level tests in a microtask (Colin Ihrig) [#52092](https://github.com/nodejs/node/pull/52092)
* \[[`77e2bf029a`](https://github.com/nodejs/node/commit/77e2bf029a)] - **(SEMVER-MINOR)** **test\_runner**: support forced exit (Colin Ihrig) [#52038](https://github.com/nodejs/node/pull/52038)
* \[[`b7bc63565e`](https://github.com/nodejs/node/commit/b7bc63565e)] - **test\_runner**: ignore todo flag when running suites (Colin Ihrig) [#52117](https://github.com/nodejs/node/pull/52117)
* \[[`be587e3ae3`](https://github.com/nodejs/node/commit/be587e3ae3)] - **test\_runner**: use paths for test locations (Colin Ihrig) [#52010](https://github.com/nodejs/node/pull/52010)
* \[[`743281ab25`](https://github.com/nodejs/node/commit/743281ab25)] - **test\_runner**: support source mapped test locations (Colin Ihrig) [#52010](https://github.com/nodejs/node/pull/52010)
* \[[`4051316d95`](https://github.com/nodejs/node/commit/4051316d95)] - **tools**: update lint-md-dependencies to rollup\@4.17.0 (Node.js GitHub Bot) [#52729](https://github.com/nodejs/node/pull/52729)

<a id="20.13.1"></a>

## 2024-05-09, Version 20.13.1 'Iron' (LTS), @marco-ippolito

### Revert "tools: install npm PowerShell scripts on Windows"

Due to a regression in the npm installation on Windows, this commit reverts the change that installed npm PowerShell scripts on Windows.

### Commits

* \[[`b7d80802cc`](https://github.com/nodejs/node/commit/b7d80802cc)] - _**Revert**_ "**tools**: install npm PowerShell scripts on Windows" (marco-ippolito) [#52897](https://github.com/nodejs/node/pull/52897)

<a id="20.13.0"></a>

## 2024-05-07, Version 20.13.0 'Iron' (LTS), @marco-ippolito

### buffer: improve `base64` and `base64url` performance

The performance of the `base64` and `base64url` encoding and decoding functions has been improved significantly.

Contributed by Yagiz Nizipli in [#52428](https://github.com/nodejs/node/pull/52428)

### crypto: deprecate implicitly shortened GCM tags

This release, introduces a doc-only deprecation of using GCM authentication tags that are shorter than the cipher's block size, unless the user specified the `authTagLength` option.

Contributed by Tobias Nießen in [#52345](https://github.com/nodejs/node/pull/52345)

### events,doc: mark CustomEvent as stable

From this release `CustomEvent` has been marked stable.

Contributed by Daeyeon Jeong in [#52618](https://github.com/nodejs/node/pull/52618)

### fs: add stacktrace to fs/promises

Sync functions in fs throwed an error with a stacktrace which is helpful for debugging. But functions in fs/promises throwed an error without a stacktrace. This commit adds stacktraces by calling `Error.captureStacktrace` and re-throwing the error.

Contributed by 翠 / green in [#49849](https://github.com/nodejs/node/pull/49849)

### report: add `--report-exclude-network` option

New option `--report-exclude-network`, also available as `report.excludeNetwork`, enables the user to exclude networking interfaces in their diagnostic report. On some systems, this can cause the report to take minutes to generate so this option can be used to optimize that.

Contributed by Ethan Arrowood in [#51645](https://github.com/nodejs/node/pull/51645)

### src: add uv\_get\_available\_memory to report and process

From this release it is possible to get the available memory in the system by calling `process.getAvailableMemory()`.

Contributed by theanarkh [#52023](https://github.com/nodejs/node/pull/52023)

### stream: support typed arrays

This commit adds support for typed arrays in streams.

Contributed by IlyasShabi [#51866](https://github.com/nodejs/node/pull/51866)

### util: support array of formats in util.styleText

It is now possible to pass an array of format strings to `util.styleText` to apply multiple formats to the same text.

```js
console.log(util.styleText(['underline', 'italic'], 'My italic underlined message'));
```

Contributed by Marco Ippolito in [#52040](https://github.com/nodejs/node/pull/52040)

### v8: implement v8.queryObjects() for memory leak regression testing

This is similar to the queryObjects() console API provided by the Chromium DevTools console. It can be used to search for objects that have the matching constructor on its prototype chain in the heap after a full garbage collection, which can be useful for memory leak regression tests.
To avoid surprising results, users should avoid using this API on constructors whose implementation they don't control, or on constructors that can be invoked by other parties in the application.

To avoid accidental leaks, this API does not return raw references to the objects found. By default, it returns the count of the objects found. If options.format is 'summary', it returns an array containing brief string representations for each object. The visibility provided in this API is similar to what the heap snapshot provides, while users can save the cost of serialization and parsing and directly filer the target objects during the search.

We have been using this API internally for the test suite, which has been more stable than any other leak regression testing strategies in the CI. With a public implementation we can now use the public API instead.

```js
const { queryObjects } = require('node:v8');
class A { foo = 'bar'; }
console.log(queryObjects(A)); // 0
let a = new A();
console.log(queryObjects(A)); // 1
// [ "A { foo: 'bar' }" ]
console.log(queryObjects(A, { format: 'summary' }));

// Release the object.
a = null;
// Search again. queryObjects() includes a full garbage collection
// so a should disappear.
console.log(queryObjects(A)); // 0

class B extends A { bar = 'qux'; }
// The child class B's prototype has A's prototype on its prototype chain
// so the prototype object shows up too.
console.log(queryObjects(A, { format: 'summary' })); // [ A {}' ]
```

Contributed by Joyee Cheung in [#51927](https://github.com/nodejs/node/pull/51927)

### watch: mark as stable

From this release Watch Mode is considered stable.
When in watch mode, changes in the watched files cause the Node.js process to restart.

Contributed by Moshe Atlow in [#52074](https://github.com/nodejs/node/pull/52074)

### Other Notable Changes

* \[[`f8ad30048d`](https://github.com/nodejs/node/commit/f8ad30048d)] - **benchmark**: add AbortSignal.abort benchmarks (Raz Luvaton) [#52408](https://github.com/nodejs/node/pull/52408)
* \[[`3b41da9a56`](https://github.com/nodejs/node/commit/3b41da9a56)] - **(SEMVER-MINOR)** **deps**: update simdutf to 5.0.0 (Daniel Lemire) [#52138](https://github.com/nodejs/node/pull/52138)
* \[[`0a08c4a7b3`](https://github.com/nodejs/node/commit/0a08c4a7b3)] - **(SEMVER-MINOR)** **deps**: update undici to 6.3.0 (Node.js GitHub Bot) [#51462](https://github.com/nodejs/node/pull/51462)
* \[[`f1b7bda4f5`](https://github.com/nodejs/node/commit/f1b7bda4f5)] - **(SEMVER-MINOR)** **deps**: update undici to 6.2.1 (Node.js GitHub Bot) [#51278](https://github.com/nodejs/node/pull/51278)
* \[[`4acca8ed84`](https://github.com/nodejs/node/commit/4acca8ed84)] - **(SEMVER-MINOR)** **dns**: add order option and support ipv6first (Paolo Insogna) [#52492](https://github.com/nodejs/node/pull/52492)
* \[[`cc67720ff9`](https://github.com/nodejs/node/commit/cc67720ff9)] - **doc**: update release gpg keyserver (marco-ippolito) [#52257](https://github.com/nodejs/node/pull/52257)
* \[[`c2def7df96`](https://github.com/nodejs/node/commit/c2def7df96)] - **doc**: add release key for marco-ippolito (marco-ippolito) [#52257](https://github.com/nodejs/node/pull/52257)
* \[[`807c89cb26`](https://github.com/nodejs/node/commit/807c89cb26)] - **doc**: add UlisesGascon as a collaborator (Ulises Gascón) [#51991](https://github.com/nodejs/node/pull/51991)
* \[[`5e78a20ef9`](https://github.com/nodejs/node/commit/5e78a20ef9)] - **(SEMVER-MINOR)** **doc**: deprecate fs.Stats public constructor (Marco Ippolito) [#51879](https://github.com/nodejs/node/pull/51879)
* \[[`722fe64ff7`](https://github.com/nodejs/node/commit/722fe64ff7)] - **(SEMVER-MINOR)** **lib, url**: add a `windows` option to path parsing (Aviv Keller) [#52509](https://github.com/nodejs/node/pull/52509)
* \[[`d116fa1568`](https://github.com/nodejs/node/commit/d116fa1568)] - **(SEMVER-MINOR)** **net**: add CLI option for autoSelectFamilyAttemptTimeout (Paolo Insogna) [#52474](https://github.com/nodejs/node/pull/52474)
* \[[`6af7b78b0d`](https://github.com/nodejs/node/commit/6af7b78b0d)] - **(SEMVER-MINOR)** **src**: add `string_view` overload to snapshot FromBlob (Anna Henningsen) [#52595](https://github.com/nodejs/node/pull/52595)
* \[[`b3a11b574b`](https://github.com/nodejs/node/commit/b3a11b574b)] - **(SEMVER-MINOR)** **src**: preload function for Environment (Cheng Zhao) [#51539](https://github.com/nodejs/node/pull/51539)
* \[[`41646d9c9e`](https://github.com/nodejs/node/commit/41646d9c9e)] - **(SEMVER-MINOR)** **test\_runner**: add suite() (Colin Ihrig) [#52127](https://github.com/nodejs/node/pull/52127)
* \[[`fc9ba17f6c`](https://github.com/nodejs/node/commit/fc9ba17f6c)] - **(SEMVER-MINOR)** **test\_runner**: add `test:complete` event to reflect execution order (Moshe Atlow) [#51909](https://github.com/nodejs/node/pull/51909)

### Commits

* \[[`6fdd748b21`](https://github.com/nodejs/node/commit/6fdd748b21)] - **benchmark**: reduce the buffer size for blob (Debadree Chatterjee) [#52548](https://github.com/nodejs/node/pull/52548)
* \[[`2274d0c868`](https://github.com/nodejs/node/commit/2274d0c868)] - **benchmark**: inherit stdio/stderr instead of pipe (Ali Hassan) [#52456](https://github.com/nodejs/node/pull/52456)
* \[[`0300598315`](https://github.com/nodejs/node/commit/0300598315)] - **benchmark**: add ipc support to spawn stdio config (Ali Hassan) [#52456](https://github.com/nodejs/node/pull/52456)
* \[[`f8ad30048d`](https://github.com/nodejs/node/commit/f8ad30048d)] - **benchmark**: add AbortSignal.abort benchmarks (Raz Luvaton) [#52408](https://github.com/nodejs/node/pull/52408)
* \[[`7508d48736`](https://github.com/nodejs/node/commit/7508d48736)] - **benchmark**: conditionally use spawn with taskset for cpu pinning (Ali Hassan) [#52253](https://github.com/nodejs/node/pull/52253)
* \[[`ea8e72e185`](https://github.com/nodejs/node/commit/ea8e72e185)] - **benchmark**: add toNamespacedPath bench (Rafael Gonzaga) [#52236](https://github.com/nodejs/node/pull/52236)
* \[[`c00715cc1e`](https://github.com/nodejs/node/commit/c00715cc1e)] - **benchmark**: add style-text benchmark (Rafael Gonzaga) [#52004](https://github.com/nodejs/node/pull/52004)
* \[[`1c1a6935ee`](https://github.com/nodejs/node/commit/1c1a6935ee)] - **buffer**: add missing ARG\_TYPE(ArrayBuffer) for isUtf8 (Jungku Lee) [#52477](https://github.com/nodejs/node/pull/52477)
* \[[`1b2aff7dce`](https://github.com/nodejs/node/commit/1b2aff7dce)] - **buffer**: improve `base64` and `base64url` performance (Yagiz Nizipli) [#52428](https://github.com/nodejs/node/pull/52428)
* \[[`328bded5ab`](https://github.com/nodejs/node/commit/328bded5ab)] - **buffer**: improve `btoa` performance (Yagiz Nizipli) [#52427](https://github.com/nodejs/node/pull/52427)
* \[[`e67bc34326`](https://github.com/nodejs/node/commit/e67bc34326)] - **buffer**: use simdutf for `atob` implementation (Yagiz Nizipli) [#52381](https://github.com/nodejs/node/pull/52381)
* \[[`5abddb45d8`](https://github.com/nodejs/node/commit/5abddb45d8)] - **build**: fix typo in node.gyp (Michaël Zasso) [#52719](https://github.com/nodejs/node/pull/52719)
* \[[`7d1f304f5e`](https://github.com/nodejs/node/commit/7d1f304f5e)] - **build**: fix headers install for shared mode on Win (Segev Finer) [#52442](https://github.com/nodejs/node/pull/52442)
* \[[`6826bbf267`](https://github.com/nodejs/node/commit/6826bbf267)] - **build**: fix arm64 cross-compilation bug on non-arm machines (Mahdi Sharifi) [#52559](https://github.com/nodejs/node/pull/52559)
* \[[`6e85bc431a`](https://github.com/nodejs/node/commit/6e85bc431a)] - **build**: temporary disable ubsan (Rafael Gonzaga) [#52560](https://github.com/nodejs/node/pull/52560)
* \[[`297368a1ed`](https://github.com/nodejs/node/commit/297368a1ed)] - **build**: fix arm64 cross-compilation (Michaël Zasso) [#51256](https://github.com/nodejs/node/pull/51256)
* \[[`93bddb598f`](https://github.com/nodejs/node/commit/93bddb598f)] - **build,tools**: add test-ubsan ci (Rafael Gonzaga) [#46297](https://github.com/nodejs/node/pull/46297)
* \[[`20bb16582f`](https://github.com/nodejs/node/commit/20bb16582f)] - **build,tools,node-api**: fix building node-api tests for Windows Debug (Vladimir Morozov) [#52632](https://github.com/nodejs/node/pull/52632)
* \[[`9af15cfff1`](https://github.com/nodejs/node/commit/9af15cfff1)] - **child\_process**: use internal addAbortListener (Chemi Atlow) [#52081](https://github.com/nodejs/node/pull/52081)
* \[[`a4847c4619`](https://github.com/nodejs/node/commit/a4847c4619)] - **crypto**: simplify assertions in Safe\*Print (David Benjamin) [#49709](https://github.com/nodejs/node/pull/49709)
* \[[`0ec4d9d734`](https://github.com/nodejs/node/commit/0ec4d9d734)] - **crypto**: enable NODE\_EXTRA\_CA\_CERTS with BoringSSL (Shelley Vohr) [#52217](https://github.com/nodejs/node/pull/52217)
* \[[`03e05b092c`](https://github.com/nodejs/node/commit/03e05b092c)] - **crypto**: deprecate implicitly shortened GCM tags (Tobias Nießen) [#52345](https://github.com/nodejs/node/pull/52345)
* \[[`0f784c96ba`](https://github.com/nodejs/node/commit/0f784c96ba)] - **crypto**: make timingSafeEqual faster for Uint8Array (Tobias Nießen) [#52341](https://github.com/nodejs/node/pull/52341)
* \[[`739958e472`](https://github.com/nodejs/node/commit/739958e472)] - **crypto**: reject Ed25519/Ed448 in Sign/Verify prototypes (Filip Skokan) [#52340](https://github.com/nodejs/node/pull/52340)
* \[[`197b61f210`](https://github.com/nodejs/node/commit/197b61f210)] - **crypto**: validate RSA-PSS saltLength in subtle.sign and subtle.verify (Filip Skokan) [#52262](https://github.com/nodejs/node/pull/52262)
* \[[`a6eede33f3`](https://github.com/nodejs/node/commit/a6eede33f3)] - **crypto**: fix `input` validation in `crypto.hash` (Antoine du Hamel) [#52070](https://github.com/nodejs/node/pull/52070)
* \[[`bfa4986e5d`](https://github.com/nodejs/node/commit/bfa4986e5d)] - **deps**: update corepack to 0.28.0 (Node.js GitHub Bot) [#52616](https://github.com/nodejs/node/pull/52616)
* \[[`70546698d7`](https://github.com/nodejs/node/commit/70546698d7)] - **deps**: update ada to 2.7.8 (Node.js GitHub Bot) [#52517](https://github.com/nodejs/node/pull/52517)
* \[[`a135027f84`](https://github.com/nodejs/node/commit/a135027f84)] - **deps**: update icu to 75.1 (Node.js GitHub Bot) [#52573](https://github.com/nodejs/node/pull/52573)
* \[[`c96f1043d4`](https://github.com/nodejs/node/commit/c96f1043d4)] - **deps**: update undici to 6.13.0 (Node.js GitHub Bot) [#52493](https://github.com/nodejs/node/pull/52493)
* \[[`9c330b610b`](https://github.com/nodejs/node/commit/9c330b610b)] - **deps**: update zlib to 1.3.0.1-motley-7d77fb7 (Node.js GitHub Bot) [#52516](https://github.com/nodejs/node/pull/52516)
* \[[`7e5bbeebab`](https://github.com/nodejs/node/commit/7e5bbeebab)] - **deps**: update nghttp2 to 1.61.0 (Node.js GitHub Bot) [#52395](https://github.com/nodejs/node/pull/52395)
* \[[`b42a4735d9`](https://github.com/nodejs/node/commit/b42a4735d9)] - **deps**: update minimatch to 9.0.4 (Node.js GitHub Bot) [#52524](https://github.com/nodejs/node/pull/52524)
* \[[`d34fd21bc2`](https://github.com/nodejs/node/commit/d34fd21bc2)] - **deps**: update simdutf to 5.2.4 (Node.js GitHub Bot) [#52473](https://github.com/nodejs/node/pull/52473)
* \[[`ecc180f830`](https://github.com/nodejs/node/commit/ecc180f830)] - **deps**: upgrade npm to 10.5.2 (npm team) [#52458](https://github.com/nodejs/node/pull/52458)
* \[[`606c183344`](https://github.com/nodejs/node/commit/606c183344)] - **deps**: update simdutf to 5.2.3 (Yagiz Nizipli) [#52381](https://github.com/nodejs/node/pull/52381)
* \[[`0a103e99fe`](https://github.com/nodejs/node/commit/0a103e99fe)] - **deps**: upgrade npm to 10.5.1 (npm team) [#52351](https://github.com/nodejs/node/pull/52351)
* \[[`cce861e670`](https://github.com/nodejs/node/commit/cce861e670)] - **deps**: update c-ares to 1.28.1 (Node.js GitHub Bot) [#52285](https://github.com/nodejs/node/pull/52285)
* \[[`5258b547ea`](https://github.com/nodejs/node/commit/5258b547ea)] - **deps**: update undici to 6.11.1 (Node.js GitHub Bot) [#52328](https://github.com/nodejs/node/pull/52328)
* \[[`923a77c80a`](https://github.com/nodejs/node/commit/923a77c80a)] - **deps**: update undici to 6.10.2 (Node.js GitHub Bot) [#52227](https://github.com/nodejs/node/pull/52227)
* \[[`bd3c6a231c`](https://github.com/nodejs/node/commit/bd3c6a231c)] - **deps**: update zlib to 1.3.0.1-motley-24c07df (Node.js GitHub Bot) [#52199](https://github.com/nodejs/node/pull/52199)
* \[[`3b41da9a56`](https://github.com/nodejs/node/commit/3b41da9a56)] - **(SEMVER-MINOR)** **deps**: update simdutf to 5.0.0 (Daniel Lemire) [#52138](https://github.com/nodejs/node/pull/52138)
* \[[`d6f9ca385c`](https://github.com/nodejs/node/commit/d6f9ca385c)] - **deps**: update zlib to 1.3.0.1-motley-24342f6 (Node.js GitHub Bot) [#52123](https://github.com/nodejs/node/pull/52123)
* \[[`f5512897b0`](https://github.com/nodejs/node/commit/f5512897b0)] - **deps**: update corepack to 0.26.0 (Node.js GitHub Bot) [#52027](https://github.com/nodejs/node/pull/52027)
* \[[`d891275178`](https://github.com/nodejs/node/commit/d891275178)] - **deps**: update ada to 2.7.7 (Node.js GitHub Bot) [#52028](https://github.com/nodejs/node/pull/52028)
* \[[`18838f2db3`](https://github.com/nodejs/node/commit/18838f2db3)] - **deps**: update simdutf to 4.0.9 (Node.js GitHub Bot) [#51655](https://github.com/nodejs/node/pull/51655)
* \[[`503c034abc`](https://github.com/nodejs/node/commit/503c034abc)] - **deps**: update undici to 6.6.2 (Node.js GitHub Bot) [#51667](https://github.com/nodejs/node/pull/51667)
* \[[`256bcba52e`](https://github.com/nodejs/node/commit/256bcba52e)] - **deps**: update undici to 6.6.0 (Node.js GitHub Bot) [#51630](https://github.com/nodejs/node/pull/51630)
* \[[`7a1e321d95`](https://github.com/nodejs/node/commit/7a1e321d95)] - **deps**: update undici to 6.4.0 (Node.js GitHub Bot) [#51527](https://github.com/nodejs/node/pull/51527)
* \[[`dde9e08224`](https://github.com/nodejs/node/commit/dde9e08224)] - **deps**: update ngtcp2 to 1.1.0 (Node.js GitHub Bot) [#51319](https://github.com/nodejs/node/pull/51319)
* \[[`0a08c4a7b3`](https://github.com/nodejs/node/commit/0a08c4a7b3)] - **(SEMVER-MINOR)** **deps**: update undici to 6.3.0 (Node.js GitHub Bot) [#51462](https://github.com/nodejs/node/pull/51462)
* \[[`f1b7bda4f5`](https://github.com/nodejs/node/commit/f1b7bda4f5)] - **(SEMVER-MINOR)** **deps**: update undici to 6.2.1 (Node.js GitHub Bot) [#51278](https://github.com/nodejs/node/pull/51278)
* \[[`ecadd638cd`](https://github.com/nodejs/node/commit/ecadd638cd)] - **deps**: V8: remove references to non-existent flags (Richard Lau) [#52256](https://github.com/nodejs/node/pull/52256)
* \[[`27d364491f`](https://github.com/nodejs/node/commit/27d364491f)] - **dgram**: use internal addAbortListener (Chemi Atlow) [#52081](https://github.com/nodejs/node/pull/52081)
* \[[`b94d11935a`](https://github.com/nodejs/node/commit/b94d11935a)] - **diagnostics\_channel**: early-exit tracing channel trace methods (Stephen Belanger) [#51915](https://github.com/nodejs/node/pull/51915)
* \[[`4acca8ed84`](https://github.com/nodejs/node/commit/4acca8ed84)] - **(SEMVER-MINOR)** **dns**: add order option and support ipv6first (Paolo Insogna) [#52492](https://github.com/nodejs/node/pull/52492)
* \[[`bcc06ac5a9`](https://github.com/nodejs/node/commit/bcc06ac5a9)] - **doc**: remove relative limitation to pm (Rafael Gonzaga) [#52648](https://github.com/nodejs/node/pull/52648)
* \[[`4d5ef4f7af`](https://github.com/nodejs/node/commit/4d5ef4f7af)] - **doc**: fix info string causing duplicated code blocks (Mathieu Leenhardt) [#52660](https://github.com/nodejs/node/pull/52660)
* \[[`d5a316f5ea`](https://github.com/nodejs/node/commit/d5a316f5ea)] - **doc**: run license-builder (github-actions\[bot]) [#52631](https://github.com/nodejs/node/pull/52631)
* \[[`d7434fe411`](https://github.com/nodejs/node/commit/d7434fe411)] - **doc**: deprecate --experimental-policy (RafaelGSS) [#52602](https://github.com/nodejs/node/pull/52602)
* \[[`02a83d89f7`](https://github.com/nodejs/node/commit/02a83d89f7)] - **doc**: add info on contributor spotlight program (Michael Dawson) [#52598](https://github.com/nodejs/node/pull/52598)
* \[[`a905eaace1`](https://github.com/nodejs/node/commit/a905eaace1)] - **doc**: correct unsafe URL example in http docs (Malte Legenhausen) [#52555](https://github.com/nodejs/node/pull/52555)
* \[[`69c21a6522`](https://github.com/nodejs/node/commit/69c21a6522)] - **doc**: replace U+00A0 with U+0020 (Luigi Pinca) [#52590](https://github.com/nodejs/node/pull/52590)
* \[[`5df34c7d0a`](https://github.com/nodejs/node/commit/5df34c7d0a)] - **doc**: sort options alphabetically (Luigi Pinca) [#52589](https://github.com/nodejs/node/pull/52589)
* \[[`b49464bc9d`](https://github.com/nodejs/node/commit/b49464bc9d)] - **doc**: correct stream.finished changes (KaKa) [#52551](https://github.com/nodejs/node/pull/52551)
* \[[`4d051ba89b`](https://github.com/nodejs/node/commit/4d051ba89b)] - **doc**: add RedYetiDev to triage team (Aviv Keller) [#52556](https://github.com/nodejs/node/pull/52556)
* \[[`4ed55bf16c`](https://github.com/nodejs/node/commit/4ed55bf16c)] - **doc**: fix issue detected in markdown lint update (Rich Trott) [#52566](https://github.com/nodejs/node/pull/52566)
* \[[`a8bc40fd07`](https://github.com/nodejs/node/commit/a8bc40fd07)] - **doc**: update test runner coverage limitations (Moshe Atlow) [#52515](https://github.com/nodejs/node/pull/52515)
* \[[`17d5ba9fed`](https://github.com/nodejs/node/commit/17d5ba9fed)] - **doc**: add lint-js-fix into BUILDING.md (jakecastelli) [#52290](https://github.com/nodejs/node/pull/52290)
* \[[`88adbd0991`](https://github.com/nodejs/node/commit/88adbd0991)] - **doc**: remove Internet Explorer mention in BUILDING.md (Rich Trott) [#52455](https://github.com/nodejs/node/pull/52455)
* \[[`e8cb29d66d`](https://github.com/nodejs/node/commit/e8cb29d66d)] - **doc**: accommodate upcoming stricter .md linting (Rich Trott) [#52454](https://github.com/nodejs/node/pull/52454)
* \[[`e2ea984c7b`](https://github.com/nodejs/node/commit/e2ea984c7b)] - **doc**: add Rafael to steward list (Rafael Gonzaga) [#52452](https://github.com/nodejs/node/pull/52452)
* \[[`93d684097a`](https://github.com/nodejs/node/commit/93d684097a)] - **doc**: correct naming convention in C++ style guide (Mohammed Keyvanzadeh) [#52424](https://github.com/nodejs/node/pull/52424)
* \[[`b9bdb947ac`](https://github.com/nodejs/node/commit/b9bdb947ac)] - **doc**: update `process.execArg` example to be more useful (Jacob Smith) [#52412](https://github.com/nodejs/node/pull/52412)
* \[[`f3f67ff84a`](https://github.com/nodejs/node/commit/f3f67ff84a)] - **doc**: call out http(s).globalAgent default (mathis-west-1) [#52392](https://github.com/nodejs/node/pull/52392)
* \[[`392a0d310e`](https://github.com/nodejs/node/commit/392a0d310e)] - **doc**: update the location of `build_with_cmake` (Emmanuel Ferdman) [#52356](https://github.com/nodejs/node/pull/52356)
* \[[`3ad62f1cc7`](https://github.com/nodejs/node/commit/3ad62f1cc7)] - **doc**: reserve 125 for Electron 31 (Shelley Vohr) [#52379](https://github.com/nodejs/node/pull/52379)
* \[[`bfd4c7844b`](https://github.com/nodejs/node/commit/bfd4c7844b)] - **doc**: use consistent plural form of "index" (Rich Trott) [#52373](https://github.com/nodejs/node/pull/52373)
* \[[`6f31cc8361`](https://github.com/nodejs/node/commit/6f31cc8361)] - **doc**: add Rafael to sec release stewards (Rafael Gonzaga) [#52354](https://github.com/nodejs/node/pull/52354)
* \[[`c55a3be789`](https://github.com/nodejs/node/commit/c55a3be789)] - **doc**: document missing options of events.on (Chemi Atlow) [#52080](https://github.com/nodejs/node/pull/52080)
* \[[`1a843f7c6d`](https://github.com/nodejs/node/commit/1a843f7c6d)] - **doc**: add missing space (Augustin Mauroy) [#52360](https://github.com/nodejs/node/pull/52360)
* \[[`8ee20d8693`](https://github.com/nodejs/node/commit/8ee20d8693)] - **doc**: add tips about vcpkg cause build faild on windows (Cong Zhang) [#52181](https://github.com/nodejs/node/pull/52181)
* \[[`a86705c113`](https://github.com/nodejs/node/commit/a86705c113)] - **doc**: replace "below" with "following" (Rich Trott) [#52315](https://github.com/nodejs/node/pull/52315)
* \[[`f3e8d1159a`](https://github.com/nodejs/node/commit/f3e8d1159a)] - **doc**: fix email pattern to be wrapped with `<<` instead of single `<` (Raz Luvaton) [#52284](https://github.com/nodejs/node/pull/52284)
* \[[`cc67720ff9`](https://github.com/nodejs/node/commit/cc67720ff9)] - **doc**: update release gpg keyserver (marco-ippolito) [#52257](https://github.com/nodejs/node/pull/52257)
* \[[`c2def7df96`](https://github.com/nodejs/node/commit/c2def7df96)] - **doc**: add release key for marco-ippolito (marco-ippolito) [#52257](https://github.com/nodejs/node/pull/52257)
* \[[`2509f3be18`](https://github.com/nodejs/node/commit/2509f3be18)] - **doc**: fix arrow vertical alignment in HTML version (Akash Yeole) [#52193](https://github.com/nodejs/node/pull/52193)
* \[[`2abaea3cdc`](https://github.com/nodejs/node/commit/2abaea3cdc)] - **doc**: move TSC members from regular to emeritus (Michael Dawson) [#52209](https://github.com/nodejs/node/pull/52209)
* \[[`65618a3d7b`](https://github.com/nodejs/node/commit/65618a3d7b)] - **doc**: add section explaining todo tests (Colin Ihrig) [#52204](https://github.com/nodejs/node/pull/52204)
* \[[`bf0ed95b04`](https://github.com/nodejs/node/commit/bf0ed95b04)] - **doc**: edit `ChildProcess` `'message'` event docs (theanarkh) [#52154](https://github.com/nodejs/node/pull/52154)
* \[[`3d67b6b5e8`](https://github.com/nodejs/node/commit/3d67b6b5e8)] - **doc**: add mold to speeding up section (Cong Zhang) [#52179](https://github.com/nodejs/node/pull/52179)
* \[[`8ba308a838`](https://github.com/nodejs/node/commit/8ba308a838)] - **doc**: http event order correction (wh0) [#51464](https://github.com/nodejs/node/pull/51464)
* \[[`9771f41069`](https://github.com/nodejs/node/commit/9771f41069)] - **doc**: move gabrielschulhof to TSC emeritus (Gabriel Schulhof) [#52192](https://github.com/nodejs/node/pull/52192)
* \[[`72bd2b0d62`](https://github.com/nodejs/node/commit/72bd2b0d62)] - **doc**: fix `--env-file` docs for valid quotes for defining values (Gabriel Bota) [#52157](https://github.com/nodejs/node/pull/52157)
* \[[`4f19203dfb`](https://github.com/nodejs/node/commit/4f19203dfb)] - **doc**: clarify what is supported in NODE\_OPTIONS (Michael Dawson) [#52076](https://github.com/nodejs/node/pull/52076)
* \[[`5bce596838`](https://github.com/nodejs/node/commit/5bce596838)] - **doc**: fix typos in maintaining-dependencies.md (RoboSchmied) [#52160](https://github.com/nodejs/node/pull/52160)
* \[[`f5241e20cc`](https://github.com/nodejs/node/commit/f5241e20cc)] - **doc**: add spec for contains module syntax (Geoffrey Booth) [#52059](https://github.com/nodejs/node/pull/52059)
* \[[`bda3cdea86`](https://github.com/nodejs/node/commit/bda3cdea86)] - **doc**: optimize the doc about Unix abstract socket (theanarkh) [#52043](https://github.com/nodejs/node/pull/52043)
* \[[`8d7d6eff81`](https://github.com/nodejs/node/commit/8d7d6eff81)] - **doc**: update pnpm link (Superchupu) [#52113](https://github.com/nodejs/node/pull/52113)
* \[[`af7c55f62d`](https://github.com/nodejs/node/commit/af7c55f62d)] - **doc**: remove ableist language from crypto (Jamie King) [#52063](https://github.com/nodejs/node/pull/52063)
* \[[`f8362b0a5a`](https://github.com/nodejs/node/commit/f8362b0a5a)] - **doc**: update collaborator email (Ruy Adorno) [#52088](https://github.com/nodejs/node/pull/52088)
* \[[`48cbd5f71e`](https://github.com/nodejs/node/commit/48cbd5f71e)] - **doc**: state that removing npm is a non-goal (Geoffrey Booth) [#51951](https://github.com/nodejs/node/pull/51951)
* \[[`0ef2708131`](https://github.com/nodejs/node/commit/0ef2708131)] - **doc**: mention NodeSource in RafaelGSS steward list (Rafael Gonzaga) [#52057](https://github.com/nodejs/node/pull/52057)
* \[[`a6473a89be`](https://github.com/nodejs/node/commit/a6473a89be)] - **doc**: remove ArrayBuffer from crypto.hash() data parameter type (fengmk2) [#52069](https://github.com/nodejs/node/pull/52069)
* \[[`ae7a11c787`](https://github.com/nodejs/node/commit/ae7a11c787)] - **doc**: add some commonly used lables up gront (Michael Dawson) [#52006](https://github.com/nodejs/node/pull/52006)
* \[[`01aaddde3c`](https://github.com/nodejs/node/commit/01aaddde3c)] - **doc**: document that `const c2 = vm.createContext(c1); c1 === c2` is true (Daniel Kaplan) [#51960](https://github.com/nodejs/node/pull/51960)
* \[[`912145fac4`](https://github.com/nodejs/node/commit/912145fac4)] - **doc**: clarify what moderation issues are for (Antoine du Hamel) [#51990](https://github.com/nodejs/node/pull/51990)
* \[[`807c89cb26`](https://github.com/nodejs/node/commit/807c89cb26)] - **doc**: add UlisesGascon as a collaborator (Ulises Gascón) [#51991](https://github.com/nodejs/node/pull/51991)
* \[[`53ff3e5682`](https://github.com/nodejs/node/commit/53ff3e5682)] - **doc**: deprecate hmac public constructor (Marco Ippolito) [#51881](https://github.com/nodejs/node/pull/51881)
* \[[`5e78a20ef9`](https://github.com/nodejs/node/commit/5e78a20ef9)] - **(SEMVER-MINOR)** **doc**: deprecate fs.Stats public constructor (Marco Ippolito) [#51879](https://github.com/nodejs/node/pull/51879)
* \[[`7bfb0b43e6`](https://github.com/nodejs/node/commit/7bfb0b43e6)] - **events**: rename high & low watermark for consistency (Chemi Atlow) [#52080](https://github.com/nodejs/node/pull/52080)
* \[[`5e6967359b`](https://github.com/nodejs/node/commit/5e6967359b)] - **events**: extract addAbortListener for safe internal use (Chemi Atlow) [#52081](https://github.com/nodejs/node/pull/52081)
* \[[`6930205272`](https://github.com/nodejs/node/commit/6930205272)] - **events**: remove abort listener from signal in `on` (Neal Beeken) [#51091](https://github.com/nodejs/node/pull/51091)
* \[[`235ab4f99f`](https://github.com/nodejs/node/commit/235ab4f99f)] - **events,doc**: mark CustomEvent as stable (Daeyeon Jeong) [#52618](https://github.com/nodejs/node/pull/52618)
* \[[`ca5b827148`](https://github.com/nodejs/node/commit/ca5b827148)] - **fs**: fix read / readSync positional offset types (Ruy Adorno) [#52603](https://github.com/nodejs/node/pull/52603)
* \[[`e7d0d804b2`](https://github.com/nodejs/node/commit/e7d0d804b2)] - **fs**: fixes recursive fs.watch crash on Linux when deleting files (Matteo Collina) [#52349](https://github.com/nodejs/node/pull/52349)
* \[[`c5fd193d6b`](https://github.com/nodejs/node/commit/c5fd193d6b)] - **fs**: refactor maybeCallback function (Yagiz Nizipli) [#52129](https://github.com/nodejs/node/pull/52129)
* \[[`0a9910c2c1`](https://github.com/nodejs/node/commit/0a9910c2c1)] - **fs**: fix edge case in readFileSync utf8 fast path (Richard Lau) [#52101](https://github.com/nodejs/node/pull/52101)
* \[[`51d7cd5de8`](https://github.com/nodejs/node/commit/51d7cd5de8)] - **fs**: validate fd from cpp on `fchown` (Yagiz Nizipli) [#52051](https://github.com/nodejs/node/pull/52051)
* \[[`33ad86c2be`](https://github.com/nodejs/node/commit/33ad86c2be)] - **fs**: validate fd from cpp on `close` (Yagiz Nizipli) [#52051](https://github.com/nodejs/node/pull/52051)
* \[[`34667c0a7e`](https://github.com/nodejs/node/commit/34667c0a7e)] - **fs**: validate file mode from cpp (Yagiz Nizipli) [#52050](https://github.com/nodejs/node/pull/52050)
* \[[`c530520be3`](https://github.com/nodejs/node/commit/c530520be3)] - **fs**: add stacktrace to fs/promises (翠 / green) [#49849](https://github.com/nodejs/node/pull/49849)
* \[[`edecd464b9`](https://github.com/nodejs/node/commit/edecd464b9)] - **fs,permission**: make handling of buffers consistent (Tobias Nießen) [#52348](https://github.com/nodejs/node/pull/52348)
* \[[`3bcd68337e`](https://github.com/nodejs/node/commit/3bcd68337e)] - **http2**: fix excessive CPU usage when using `allowHTTP1=true` (Eugene) [#52713](https://github.com/nodejs/node/pull/52713)
* \[[`e01015996a`](https://github.com/nodejs/node/commit/e01015996a)] - **http2**: fix h2-over-h2 connection proxying (Tim Perry) [#52368](https://github.com/nodejs/node/pull/52368)
* \[[`9f88736860`](https://github.com/nodejs/node/commit/9f88736860)] - **http2**: use internal addAbortListener (Chemi Atlow) [#52081](https://github.com/nodejs/node/pull/52081)
* \[[`acd7758959`](https://github.com/nodejs/node/commit/acd7758959)] - **lib**: use predefined variable instead of bit operation (Deokjin Kim) [#52580](https://github.com/nodejs/node/pull/52580)
* \[[`18ae7a46f6`](https://github.com/nodejs/node/commit/18ae7a46f6)] - **lib**: refactor lazy loading of undici for fetch method (Victor Chen) [#52275](https://github.com/nodejs/node/pull/52275)
* \[[`64c2c2a7ac`](https://github.com/nodejs/node/commit/64c2c2a7ac)] - **lib**: replace string prototype usage with alternatives (Aviv Keller) [#52440](https://github.com/nodejs/node/pull/52440)
* \[[`ee11b5315c`](https://github.com/nodejs/node/commit/ee11b5315c)] - **lib**: .load .save add proper error message when no file passed (Thomas Mauran) [#52225](https://github.com/nodejs/node/pull/52225)
* \[[`e5521b537f`](https://github.com/nodejs/node/commit/e5521b537f)] - **lib**: fix type error for \_refreshLine (Jackson Tian) [#52133](https://github.com/nodejs/node/pull/52133)
* \[[`d5d6e041c8`](https://github.com/nodejs/node/commit/d5d6e041c8)] - **lib**: emit listening event once when call listen twice (theanarkh) [#52119](https://github.com/nodejs/node/pull/52119)
* \[[`d33fc36784`](https://github.com/nodejs/node/commit/d33fc36784)] - **lib**: make sure clear the old timer in http server (theanarkh) [#52118](https://github.com/nodejs/node/pull/52118)
* \[[`ea4905c0f5`](https://github.com/nodejs/node/commit/ea4905c0f5)] - **lib**: fix listen with handle in cluster worker (theanarkh) [#52056](https://github.com/nodejs/node/pull/52056)
* \[[`8fd8130507`](https://github.com/nodejs/node/commit/8fd8130507)] - **lib, doc**: rename readme.md to README.md (Aviv Keller) [#52471](https://github.com/nodejs/node/pull/52471)
* \[[`722fe64ff7`](https://github.com/nodejs/node/commit/722fe64ff7)] - **(SEMVER-MINOR)** **lib, url**: add a `windows` option to path parsing (Aviv Keller) [#52509](https://github.com/nodejs/node/pull/52509)
* \[[`26691e6032`](https://github.com/nodejs/node/commit/26691e6032)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#52633](https://github.com/nodejs/node/pull/52633)
* \[[`befb90dc83`](https://github.com/nodejs/node/commit/befb90dc83)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#52457](https://github.com/nodejs/node/pull/52457)
* \[[`22b7167e72`](https://github.com/nodejs/node/commit/22b7167e72)] - **meta**: bump actions/download-artifact from 4.1.3 to 4.1.4 (dependabot\[bot]) [#52314](https://github.com/nodejs/node/pull/52314)
* \[[`4dafd3ede2`](https://github.com/nodejs/node/commit/4dafd3ede2)] - **meta**: bump rtCamp/action-slack-notify from 2.2.1 to 2.3.0 (dependabot\[bot]) [#52313](https://github.com/nodejs/node/pull/52313)
* \[[`2760db7640`](https://github.com/nodejs/node/commit/2760db7640)] - **meta**: bump github/codeql-action from 3.24.6 to 3.24.9 (dependabot\[bot]) [#52312](https://github.com/nodejs/node/pull/52312)
* \[[`542aaf9ca9`](https://github.com/nodejs/node/commit/542aaf9ca9)] - **meta**: bump actions/cache from 4.0.1 to 4.0.2 (dependabot\[bot]) [#52311](https://github.com/nodejs/node/pull/52311)
* \[[`df330998d9`](https://github.com/nodejs/node/commit/df330998d9)] - **meta**: bump actions/setup-python from 5.0.0 to 5.1.0 (dependabot\[bot]) [#52310](https://github.com/nodejs/node/pull/52310)
* \[[`5f40fe0cc2`](https://github.com/nodejs/node/commit/5f40fe0cc2)] - **meta**: bump codecov/codecov-action from 4.1.0 to 4.1.1 (dependabot\[bot]) [#52308](https://github.com/nodejs/node/pull/52308)
* \[[`481420f25c`](https://github.com/nodejs/node/commit/481420f25c)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#52300](https://github.com/nodejs/node/pull/52300)
* \[[`3121949f85`](https://github.com/nodejs/node/commit/3121949f85)] - **meta**: pass Codecov upload token to codecov action (Michaël Zasso) [#51982](https://github.com/nodejs/node/pull/51982)
* \[[`882a64e639`](https://github.com/nodejs/node/commit/882a64e639)] - **module**: fix detect-module not retrying as esm for cjs-only errors (Geoffrey Booth) [#52024](https://github.com/nodejs/node/pull/52024)
* \[[`5fcc1d32a8`](https://github.com/nodejs/node/commit/5fcc1d32a8)] - **module**: refactor ESM loader initialization and entry point handling (Joyee Cheung) [#51999](https://github.com/nodejs/node/pull/51999)
* \[[`d116fa1568`](https://github.com/nodejs/node/commit/d116fa1568)] - **(SEMVER-MINOR)** **net**: add CLI option for autoSelectFamilyAttemptTimeout (Paolo Insogna) [#52474](https://github.com/nodejs/node/pull/52474)
* \[[`37abad86ae`](https://github.com/nodejs/node/commit/37abad86ae)] - **net**: use internal addAbortListener (Chemi Atlow) [#52081](https://github.com/nodejs/node/pull/52081)
* \[[`a920489a1f`](https://github.com/nodejs/node/commit/a920489a1f)] - **node-api**: address coverity report (Michael Dawson) [#52584](https://github.com/nodejs/node/pull/52584)
* \[[`0a225a4b40`](https://github.com/nodejs/node/commit/0a225a4b40)] - **node-api**: copy external type tags when they are set (Niels Martignène) [#52426](https://github.com/nodejs/node/pull/52426)
* \[[`f9d95674be`](https://github.com/nodejs/node/commit/f9d95674be)] - **node-api**: make tsfn accept napi\_finalize once more (Gabriel Schulhof) [#51801](https://github.com/nodejs/node/pull/51801)
* \[[`72aabe1139`](https://github.com/nodejs/node/commit/72aabe1139)] - **perf\_hooks**: reduce overhead of createHistogram (Vinícius Lourenço) [#50074](https://github.com/nodejs/node/pull/50074)
* \[[`fb601c3a94`](https://github.com/nodejs/node/commit/fb601c3a94)] - **readline**: use internal addAbortListener (Chemi Atlow) [#52081](https://github.com/nodejs/node/pull/52081)
* \[[`29f09f05f7`](https://github.com/nodejs/node/commit/29f09f05f7)] - **(SEMVER-MINOR)** **report**: add `--report-exclude-network` option (Ethan Arrowood) [#51645](https://github.com/nodejs/node/pull/51645)
* \[[`7e6d923f5b`](https://github.com/nodejs/node/commit/7e6d923f5b)] - **src**: cast to v8::Value before using v8::EmbedderGraph::V8Node (Joyee Cheung) [#52638](https://github.com/nodejs/node/pull/52638)
* \[[`6af7b78b0d`](https://github.com/nodejs/node/commit/6af7b78b0d)] - **(SEMVER-MINOR)** **src**: add `string_view` overload to snapshot FromBlob (Anna Henningsen) [#52595](https://github.com/nodejs/node/pull/52595)
* \[[`27491e55c1`](https://github.com/nodejs/node/commit/27491e55c1)] - **src**: remove regex usage for env file parsing (IlyasShabi) [#52406](https://github.com/nodejs/node/pull/52406)
* \[[`b05e639e27`](https://github.com/nodejs/node/commit/b05e639e27)] - **src**: fix loadEnvFile ENOENT error (mathis-west-1) [#52438](https://github.com/nodejs/node/pull/52438)
* \[[`1b4d2814d1`](https://github.com/nodejs/node/commit/1b4d2814d1)] - **src**: update branch name in node\_revert.h (Tobias Nießen) [#52390](https://github.com/nodejs/node/pull/52390)
* \[[`7e35a169ea`](https://github.com/nodejs/node/commit/7e35a169ea)] - **src**: stop using `v8::BackingStore::Reallocate` (Michaël Zasso) [#52292](https://github.com/nodejs/node/pull/52292)
* \[[`2449d2606a`](https://github.com/nodejs/node/commit/2449d2606a)] - **src**: fix move after use reported by coverity (Michael Dawson) [#52141](https://github.com/nodejs/node/pull/52141)
* \[[`b33eff887d`](https://github.com/nodejs/node/commit/b33eff887d)] - **(SEMVER-MINOR)** **src**: add C++ ProcessEmitWarningSync() (Joyee Cheung) [#51977](https://github.com/nodejs/node/pull/51977)
* \[[`f2c7408927`](https://github.com/nodejs/node/commit/f2c7408927)] - **src**: return a number from process.constrainedMemory() constantly (Chengzhong Wu) [#52039](https://github.com/nodejs/node/pull/52039)
* \[[`7f575c886b`](https://github.com/nodejs/node/commit/7f575c886b)] - **(SEMVER-MINOR)** **src**: add uv\_get\_available\_memory to report and process (theanarkh) [#52023](https://github.com/nodejs/node/pull/52023)
* \[[`e161e62313`](https://github.com/nodejs/node/commit/e161e62313)] - **src**: use dedicated routine to compile function for builtin CJS loader (Joyee Cheung) [#52016](https://github.com/nodejs/node/pull/52016)
* \[[`07322b490f`](https://github.com/nodejs/node/commit/07322b490f)] - **src**: fix reading empty string views in Blob\[De]serializer (Joyee Cheung) [#52000](https://github.com/nodejs/node/pull/52000)
* \[[`e216192390`](https://github.com/nodejs/node/commit/e216192390)] - **src**: refactor out FormatErrorMessage for error formatting (Joyee Cheung) [#51999](https://github.com/nodejs/node/pull/51999)
* \[[`b3a11b574b`](https://github.com/nodejs/node/commit/b3a11b574b)] - **(SEMVER-MINOR)** **src**: preload function for Environment (Cheng Zhao) [#51539](https://github.com/nodejs/node/pull/51539)
* \[[`09bd367ef6`](https://github.com/nodejs/node/commit/09bd367ef6)] - **stream**: make Duplex inherit destroy from Writable (Luigi Pinca) [#52318](https://github.com/nodejs/node/pull/52318)
* \[[`0b853a7576`](https://github.com/nodejs/node/commit/0b853a7576)] - **(SEMVER-MINOR)** **stream**: support typed arrays (IlyasShabi) [#51866](https://github.com/nodejs/node/pull/51866)
* \[[`f8209ffe45`](https://github.com/nodejs/node/commit/f8209ffe45)] - **stream**: add `new` when constructing `ERR_MULTIPLE_CALLBACK` (haze) [#52110](https://github.com/nodejs/node/pull/52110)
* \[[`8442457117`](https://github.com/nodejs/node/commit/8442457117)] - **stream**: use internal addAbortListener (Chemi Atlow) [#52081](https://github.com/nodejs/node/pull/52081)
* \[[`8d20b641a2`](https://github.com/nodejs/node/commit/8d20b641a2)] - _**Revert**_ "**stream**: fix cloned webstreams not being unref'd" (Matteo Collina) [#51491](https://github.com/nodejs/node/pull/51491)
* \[[`a923adffab`](https://github.com/nodejs/node/commit/a923adffab)] - **test**: mark `test-error-serdes` as flaky (Antoine du Hamel) [#52739](https://github.com/nodejs/node/pull/52739)
* \[[`d4f1803f0b`](https://github.com/nodejs/node/commit/d4f1803f0b)] - **test**: mark test as flaky (Michael Dawson) [#52671](https://github.com/nodejs/node/pull/52671)
* \[[`1e88e042c2`](https://github.com/nodejs/node/commit/1e88e042c2)] - **test**: skip test-fs-watch-recursive-delete.js on IBM i (Abdirahim Musse) [#52645](https://github.com/nodejs/node/pull/52645)
* \[[`6da558af8b`](https://github.com/nodejs/node/commit/6da558af8b)] - **test**: ensure that all worker servers are ready (Luigi Pinca) [#52563](https://github.com/nodejs/node/pull/52563)
* \[[`c871fadb85`](https://github.com/nodejs/node/commit/c871fadb85)] - **test**: fix test-tls-ticket-cluster.js (Hüseyin Açacak) [#52431](https://github.com/nodejs/node/pull/52431)
* \[[`b6cb74d775`](https://github.com/nodejs/node/commit/b6cb74d775)] - **test**: split wasi poll test for windows (Hüseyin Açacak) [#52538](https://github.com/nodejs/node/pull/52538)
* \[[`4ad159bb75`](https://github.com/nodejs/node/commit/4ad159bb75)] - **test**: write tests for assertIsArray http2 util (Sinan Sonmez (Chaush)) [#52511](https://github.com/nodejs/node/pull/52511)
* \[[`1f7a28cbe7`](https://github.com/nodejs/node/commit/1f7a28cbe7)] - **test**: fix watch test with require not testing pid (Raz Luvaton) [#52353](https://github.com/nodejs/node/pull/52353)
* \[[`5b758b93d5`](https://github.com/nodejs/node/commit/5b758b93d5)] - **test**: simplify ASan build checks (Michaël Zasso) [#52430](https://github.com/nodejs/node/pull/52430)
* \[[`375c3db5ea`](https://github.com/nodejs/node/commit/375c3db5ea)] - **test**: fix Windows compiler warnings in overlapped-checker (Michaël Zasso) [#52405](https://github.com/nodejs/node/pull/52405)
* \[[`a1dd92cdee`](https://github.com/nodejs/node/commit/a1dd92cdee)] - **test**: add test for skip+todo combinations (Colin Ihrig) [#52204](https://github.com/nodejs/node/pull/52204)
* \[[`8a0b721930`](https://github.com/nodejs/node/commit/8a0b721930)] - **test**: fix incorrect test fixture (Colin Ihrig) [#52185](https://github.com/nodejs/node/pull/52185)
* \[[`dd1f761f3b`](https://github.com/nodejs/node/commit/dd1f761f3b)] - **test**: add missing cctest/test\_path.cc (Yagiz Nizipli) [#52148](https://github.com/nodejs/node/pull/52148)
* \[[`6da446d9e1`](https://github.com/nodejs/node/commit/6da446d9e1)] - **test**: add `spawnSyncAndAssert` util (Antoine du Hamel) [#52132](https://github.com/nodejs/node/pull/52132)
* \[[`d7bfb4e8d8`](https://github.com/nodejs/node/commit/d7bfb4e8d8)] - **test**: reduce flakiness of test-runner-output.mjs (Colin Ihrig) [#52146](https://github.com/nodejs/node/pull/52146)
* \[[`e4981b3d75`](https://github.com/nodejs/node/commit/e4981b3d75)] - **test**: add test for using `--print` with promises (Antoine du Hamel) [#52137](https://github.com/nodejs/node/pull/52137)
* \[[`5cc540078e`](https://github.com/nodejs/node/commit/5cc540078e)] - **test**: un-set test-emit-after-on-destroyed as flaky (Abdirahim Musse) [#51995](https://github.com/nodejs/node/pull/51995)
* \[[`b9eb0035dd`](https://github.com/nodejs/node/commit/b9eb0035dd)] - **test**: skip test for dynamically linked OpenSSL (Richard Lau) [#52542](https://github.com/nodejs/node/pull/52542)
* \[[`32014f5601`](https://github.com/nodejs/node/commit/32014f5601)] - **test**: avoid v8 deadcode on performance function (Vinícius Lourenço) [#50074](https://github.com/nodejs/node/pull/50074)
* \[[`29d2011f51`](https://github.com/nodejs/node/commit/29d2011f51)] - **test\_runner**: better error handing for test hook (Alex Yang) [#52401](https://github.com/nodejs/node/pull/52401)
* \[[`9497097fb3`](https://github.com/nodejs/node/commit/9497097fb3)] - **test\_runner**: fix clearing final timeout in own callback (Ben Richeson) [#52332](https://github.com/nodejs/node/pull/52332)
* \[[`0f690f0b9e`](https://github.com/nodejs/node/commit/0f690f0b9e)] - **test\_runner**: fix recursive run (Moshe Atlow) [#52322](https://github.com/nodejs/node/pull/52322)
* \[[`34ab1a36ee`](https://github.com/nodejs/node/commit/34ab1a36ee)] - **test\_runner**: hide new line when no error in spec reporter (Moshe Atlow) [#52297](https://github.com/nodejs/node/pull/52297)
* \[[`379535abe3`](https://github.com/nodejs/node/commit/379535abe3)] - **test\_runner**: disable highWatermark on TestsStream (Colin Ihrig) [#52287](https://github.com/nodejs/node/pull/52287)
* \[[`35588cff39`](https://github.com/nodejs/node/commit/35588cff39)] - **test\_runner**: run afterEach hooks in correct order (Colin Ihrig) [#52239](https://github.com/nodejs/node/pull/52239)
* \[[`5cd3df8fe1`](https://github.com/nodejs/node/commit/5cd3df8fe1)] - **test\_runner**: simplify test end time tracking (Colin Ihrig) [#52182](https://github.com/nodejs/node/pull/52182)
* \[[`07e4a42e4b`](https://github.com/nodejs/node/commit/07e4a42e4b)] - **test\_runner**: simplify test start time tracking (Colin Ihrig) [#52182](https://github.com/nodejs/node/pull/52182)
* \[[`caec996831`](https://github.com/nodejs/node/commit/caec996831)] - **test\_runner**: emit diagnostics when watch mode drains (Moshe Atlow) [#52130](https://github.com/nodejs/node/pull/52130)
* \[[`41646d9c9e`](https://github.com/nodejs/node/commit/41646d9c9e)] - **(SEMVER-MINOR)** **test\_runner**: add suite() (Colin Ihrig) [#52127](https://github.com/nodejs/node/pull/52127)
* \[[`fd1489a623`](https://github.com/nodejs/node/commit/fd1489a623)] - **test\_runner**: skip each hooks for skipped tests (Colin Ihrig) [#52115](https://github.com/nodejs/node/pull/52115)
* \[[`73b38bfa9e`](https://github.com/nodejs/node/commit/73b38bfa9e)] - **test\_runner**: remove redundant report call (Colin Ihrig) [#52089](https://github.com/nodejs/node/pull/52089)
* \[[`68187c4d9e`](https://github.com/nodejs/node/commit/68187c4d9e)] - **test\_runner**: use internal addAbortListener (Chemi Atlow) [#52081](https://github.com/nodejs/node/pull/52081)
* \[[`61e7ae05ef`](https://github.com/nodejs/node/commit/61e7ae05ef)] - **test\_runner**: use source maps when reporting coverage (Moshe Atlow) [#52060](https://github.com/nodejs/node/pull/52060)
* \[[`e64a25af61`](https://github.com/nodejs/node/commit/e64a25af61)] - **test\_runner**: handle undefined test locations (Colin Ihrig) [#52036](https://github.com/nodejs/node/pull/52036)
* \[[`590decf202`](https://github.com/nodejs/node/commit/590decf202)] - **test\_runner**: avoid overwriting root start time (Colin Ihrig) [#52020](https://github.com/nodejs/node/pull/52020)
* \[[`a4cbb61c65`](https://github.com/nodejs/node/commit/a4cbb61c65)] - **test\_runner**: abort unfinished tests on async error (Colin Ihrig) [#51996](https://github.com/nodejs/node/pull/51996)
* \[[`a223ca4868`](https://github.com/nodejs/node/commit/a223ca4868)] - **test\_runner**: run before hook immediately if test started (Moshe Atlow) [#52003](https://github.com/nodejs/node/pull/52003)
* \[[`956ee74c7e`](https://github.com/nodejs/node/commit/956ee74c7e)] - **test\_runner**: add support for null and date value output (Malthe Borch) [#51920](https://github.com/nodejs/node/pull/51920)
* \[[`fc9ba17f6c`](https://github.com/nodejs/node/commit/fc9ba17f6c)] - **(SEMVER-MINOR)** **test\_runner**: add `test:complete` event to reflect execution order (Moshe Atlow) [#51909](https://github.com/nodejs/node/pull/51909)
* \[[`d5ac979aeb`](https://github.com/nodejs/node/commit/d5ac979aeb)] - **test\_runner**: format coverage report for tap reporter (Pulkit Gupta) [#51119](https://github.com/nodejs/node/pull/51119)
* \[[`c925bc18dc`](https://github.com/nodejs/node/commit/c925bc18dc)] - **tools**: take co-authors into account in `find-inactive-collaborators` (Antoine du Hamel) [#52669](https://github.com/nodejs/node/pull/52669)
* \[[`1d37e772ec`](https://github.com/nodejs/node/commit/1d37e772ec)] - **tools**: fix invalid escape sequence in mkssldef (Michaël Zasso) [#52624](https://github.com/nodejs/node/pull/52624)
* \[[`5b22fc3a81`](https://github.com/nodejs/node/commit/5b22fc3a81)] - **tools**: update lint-md-dependencies to rollup\@4.15.0 (Node.js GitHub Bot) [#52617](https://github.com/nodejs/node/pull/52617)
* \[[`9cf47bb2f1`](https://github.com/nodejs/node/commit/9cf47bb2f1)] - **tools**: update lint-md-dependencies (Rich Trott) [#52581](https://github.com/nodejs/node/pull/52581)
* \[[`c0c60d13c0`](https://github.com/nodejs/node/commit/c0c60d13c0)] - **tools**: fix heading spaces for osx-entitlements.plist (Jackson Tian) [#52561](https://github.com/nodejs/node/pull/52561)
* \[[`7c349d7819`](https://github.com/nodejs/node/commit/7c349d7819)] - **tools**: update lint-md-dependencies to rollup\@4.14.2 vfile-reporter\@8.1.1 (Node.js GitHub Bot) [#52518](https://github.com/nodejs/node/pull/52518)
* \[[`b4d703297b`](https://github.com/nodejs/node/commit/b4d703297b)] - **tools**: use stylistic ESLint plugin for formatting (Michaël Zasso) [#50714](https://github.com/nodejs/node/pull/50714)
* \[[`c6813360c2`](https://github.com/nodejs/node/commit/c6813360c2)] - **tools**: update minimatch index path (Marco Ippolito) [#52523](https://github.com/nodejs/node/pull/52523)
* \[[`8464c0253c`](https://github.com/nodejs/node/commit/8464c0253c)] - **tools**: add a linter for README lists (Antoine du Hamel) [#52476](https://github.com/nodejs/node/pull/52476)
* \[[`55a3fbc842`](https://github.com/nodejs/node/commit/55a3fbc842)] - **tools**: change inactive limit to 12 months (Yagiz Nizipli) [#52425](https://github.com/nodejs/node/pull/52425)
* \[[`74a171f130`](https://github.com/nodejs/node/commit/74a171f130)] - **tools**: update stale bot messaging (Wes Todd) [#52423](https://github.com/nodejs/node/pull/52423)
* \[[`b2a3dcec2a`](https://github.com/nodejs/node/commit/b2a3dcec2a)] - **tools**: update lint-md-dependencies to rollup\@4.14.0 (Node.js GitHub Bot) [#52398](https://github.com/nodejs/node/pull/52398)
* \[[`f71a777e6e`](https://github.com/nodejs/node/commit/f71a777e6e)] - **tools**: update Ruff to v0.3.4 (Michaël Zasso) [#52302](https://github.com/nodejs/node/pull/52302)
* \[[`e3e0c68f8f`](https://github.com/nodejs/node/commit/e3e0c68f8f)] - **tools**: run test-ubsan on ubuntu-latest (Michaël Zasso) [#52375](https://github.com/nodejs/node/pull/52375)
* \[[`893b5aac31`](https://github.com/nodejs/node/commit/893b5aac31)] - **tools**: update lint-md-dependencies to rollup\@4.13.2 (Node.js GitHub Bot) [#52286](https://github.com/nodejs/node/pull/52286)
* \[[`049cca419e`](https://github.com/nodejs/node/commit/049cca419e)] - _**Revert**_ "**tools**: run `build-windows` workflow only on source changes" (Michaël Zasso) [#52320](https://github.com/nodejs/node/pull/52320)
* \[[`b3dfc62cee`](https://github.com/nodejs/node/commit/b3dfc62cee)] - **tools**: use Python 3.12 in GitHub Actions workflows (Michaël Zasso) [#52301](https://github.com/nodejs/node/pull/52301)
* \[[`c7238d0c04`](https://github.com/nodejs/node/commit/c7238d0c04)] - **tools**: allow local updates for llhttp (Paolo Insogna) [#52085](https://github.com/nodejs/node/pull/52085)
* \[[`c39f15cafd`](https://github.com/nodejs/node/commit/c39f15cafd)] - **tools**: install npm PowerShell scripts on Windows (Luke Karrys) [#52009](https://github.com/nodejs/node/pull/52009)
* \[[`b36fea064a`](https://github.com/nodejs/node/commit/b36fea064a)] - **tools**: update lint-md-dependencies to rollup\@4.13.0 (Node.js GitHub Bot) [#52122](https://github.com/nodejs/node/pull/52122)
* \[[`a5204eb915`](https://github.com/nodejs/node/commit/a5204eb915)] - **tools**: fix error reported by coverity in js2c.cc (Michael Dawson) [#52142](https://github.com/nodejs/node/pull/52142)
* \[[`cef4b7ef3d`](https://github.com/nodejs/node/commit/cef4b7ef3d)] - **tools**: sync ubsan workflow with asan (Michaël Zasso) [#52152](https://github.com/nodejs/node/pull/52152)
* \[[`d406976bbe`](https://github.com/nodejs/node/commit/d406976bbe)] - **tools**: update github\_reporter to 1.7.0 (Node.js GitHub Bot) [#52121](https://github.com/nodejs/node/pull/52121)
* \[[`fb100a2ac8`](https://github.com/nodejs/node/commit/fb100a2ac8)] - **tools**: remove gyp-next .github folder (Marco Ippolito) [#52064](https://github.com/nodejs/node/pull/52064)
* \[[`5f1e7a0de2`](https://github.com/nodejs/node/commit/5f1e7a0de2)] - **tools**: update gyp-next to 0.16.2 (Node.js GitHub Bot) [#52062](https://github.com/nodejs/node/pull/52062)
* \[[`1f1253446b`](https://github.com/nodejs/node/commit/1f1253446b)] - **tools**: install manpage to share/man for FreeBSD (Po-Chuan Hsieh) [#51791](https://github.com/nodejs/node/pull/51791)
* \[[`4b8b92fccc`](https://github.com/nodejs/node/commit/4b8b92fccc)] - **tools**: automate gyp-next update (Marco Ippolito) [#52014](https://github.com/nodejs/node/pull/52014)
* \[[`1ec9e58692`](https://github.com/nodejs/node/commit/1ec9e58692)] - **typings**: fix invalid JSDoc declarations (Yagiz Nizipli) [#52659](https://github.com/nodejs/node/pull/52659)
* \[[`2d6b19970b`](https://github.com/nodejs/node/commit/2d6b19970b)] - **(SEMVER-MINOR)** **util**: support array of formats in util.styleText (Marco Ippolito) [#52040](https://github.com/nodejs/node/pull/52040)
* \[[`d30cccdf8c`](https://github.com/nodejs/node/commit/d30cccdf8c)] - **(SEMVER-MINOR)** **v8**: implement v8.queryObjects() for memory leak regression testing (Joyee Cheung) [#51927](https://github.com/nodejs/node/pull/51927)
* \[[`7f3b7fdeff`](https://github.com/nodejs/node/commit/7f3b7fdeff)] - **watch**: fix some node argument not passed to watched process (Raz Luvaton) [#52358](https://github.com/nodejs/node/pull/52358)
* \[[`8ba6f9bc9a`](https://github.com/nodejs/node/commit/8ba6f9bc9a)] - **watch**: use internal addAbortListener (Chemi Atlow) [#52081](https://github.com/nodejs/node/pull/52081)
* \[[`5a922232da`](https://github.com/nodejs/node/commit/5a922232da)] - **watch**: mark as stable (Moshe Atlow) [#52074](https://github.com/nodejs/node/pull/52074)
* \[[`508e968a5f`](https://github.com/nodejs/node/commit/508e968a5f)] - **watch**: batch file restarts (Moshe Atlow) [#51992](https://github.com/nodejs/node/pull/51992)

<a id="20.12.2"></a>

## 2024-04-10, Version 20.12.2 'Iron' (LTS), @RafaelGSS

This is a security release.

### Notable Changes

* CVE-2024-27980 - Command injection via args parameter of `child_process.spawn` without shell option enabled on Windows

### Commits

* \[[`69ffc6d50d`](https://github.com/nodejs/node/commit/69ffc6d50d)] - **src**: disallow direct .bat and .cmd file spawning (Ben Noordhuis) [nodejs-private/node-private#563](https://github.com/nodejs-private/node-private/pull/563)

<a id="20.12.1"></a>

## 2024-04-03, Version 20.12.1 'Iron' (LTS), @RafaelGSS

This is a security release

### Notable Changes

* CVE-2024-27983 - Assertion failed in node::http2::Http2Session::\~Http2Session() leads to HTTP/2 server crash- (High)
* CVE-2024-27982 - HTTP Request Smuggling via Content Length Obfuscation - (Medium)
* llhttp version 9.2.1
* undici version 5.28.4

### Commits

* \[[`bd8f10a257`](https://github.com/nodejs/node/commit/bd8f10a257)] - **deps**: update undici to v5.28.4 (Matteo Collina) [nodejs-private/node-private#576](https://github.com/nodejs-private/node-private/pull/576)
* \[[`5e34540a96`](https://github.com/nodejs/node/commit/5e34540a96)] - **http**: do not allow OBS fold in headers by default (Paolo Insogna) [nodejs-private/node-private#557](https://github.com/nodejs-private/node-private/pull/557)
* \[[`ba1ae6d188`](https://github.com/nodejs/node/commit/ba1ae6d188)] - **src**: ensure to close stream when destroying session (Anna Henningsen) [nodejs-private/node-private#561](https://github.com/nodejs-private/node-private/pull/561)

<a id="20.12.0"></a>

## 2024-03-26, Version 20.12.0 'Iron' (LTS), @richardlau

### Notable Changes

#### crypto: implement crypto.hash()

This patch introduces a helper crypto.hash() that computes
a digest from the input at one shot. This can be 1.2-2x faster
than the object-based createHash() for smaller inputs (<= 5MB)
that are readily available (not streamed) and incur less memory
overhead since no intermediate objects will be created.

```js
const crypto = require('node:crypto');

// Hashing a string and return the result as a hex-encoded string.
const string = 'Node.js';
// 10b3493287f831e81a438811a1ffba01f8cec4b7
console.log(crypto.hash('sha1', string));
```

Contributed by Joyee Cheung in [#51044](https://github.com/nodejs/node/pull/51044).

#### Loading and parsing environment variables

* `process.loadEnvFile(path)`:
  * Use this function to load the `.env` file. If no path is specified, it automatically loads the .env file in the current directory. Example: `process.loadEnvFile()`.
  * Load a specific .env file by specifying its path. Example: `process.loadEnvFile('./development.env')`.

* `util.parseEnv(content)`:
  * Use this function to parse an existing string containing environment variable assignments.
  * Example usage: `require('node:util').parseEnv('HELLO=world')`.

Contributed by Yagiz Nizipli in [#51476](https://github.com/nodejs/node/pull/51476).

#### New connection attempt events

Three new events were added in the `net.createConnection` flow:

* `connectionAttempt`: Emitted when a new connection attempt is established. In case of Happy Eyeballs, this might emitted multiple times.
* `connectionAttemptFailed`: Emitted when a connection attempt failed. In case of Happy Eyeballs, this might emitted multiple times.
* `connectionAttemptTimeout`: Emitted when a connection attempt timed out. In case of Happy Eyeballs, this will not be emitted for the last attempt. This is not emitted at all if Happy Eyeballs is not used.

Additionally, a previous bug has been fixed where a new connection attempt could have been started after a previous one failed and after the connection was destroyed by the user.
This led to a failed assertion.

Contributed by Paolo Insogna in [#51045](https://github.com/nodejs/node/pull/51045).

#### Permission Model changes

Node.js 20.12.0 comes with several fixes for the experimental permission model and two new semver-minor commits.
We're adding a new flag `--allow-addons` to enable addon usage when using the Permission Model.

```console
$ node --experimental-permission --allow-addons
```

Contributed by Rafael Gonzaga in [#51183](https://github.com/nodejs/node/pull/51183)

And relative paths are now supported through the `--allow-fs-*` flags.
Therefore, with this release one can use:

```console
$ node --experimental-permission --allow-fs-read=./index.js
```

To give only read access to the entrypoint of the application.

Contributed by Rafael Gonzaga and Carlos Espa in [#50758](https://github.com/nodejs/node/pull/50758).

#### sea: support embedding assets

Users can now include assets by adding a key-path dictionary
to the configuration as the `assets` field. At build time, Node.js
would read the assets from the specified paths and bundle them into
the preparation blob. In the generated executable, users can retrieve
the assets using the `sea.getAsset()` and `sea.getAssetAsBlob()` API.

```json
{
  "main": "/path/to/bundled/script.js",
  "output": "/path/to/write/the/generated/blob.blob",
  "assets": {
    "a.jpg": "/path/to/a.jpg",
    "b.txt": "/path/to/b.txt"
  }
}
```

The single-executable application can access the assets as follows:

```cjs
const { getAsset } = require('node:sea');
// Returns a copy of the data in an ArrayBuffer
const image = getAsset('a.jpg');
// Returns a string decoded from the asset as UTF8.
const text = getAsset('b.txt', 'utf8');
// Returns a Blob containing the asset without copying.
const blob = getAssetAsBlob('a.jpg');
```

Contributed by Joyee Cheung in [#50960](https://github.com/nodejs/node/pull/50960).

#### Support configurable snapshot through `--build-snapshot-config` flag

We are adding a new flag `--build-snapshot-config` to configure snapshots through a custom JSON configuration file.

```console
$ node --build-snapshot-config=/path/to/myconfig.json
```

When using this flag, additional script files provided on the command line will
not be executed and instead be interpreted as regular command line arguments.

These changes were contributed by Joyee Cheung and Anna Henningsen in [#50453](https://github.com/nodejs/node/pull/50453)

#### Text Styling

* `util.styleText(format, text)`: This function returns a formatted text considering the `format` passed.

A new API has been created to format text based on `util.inspect.colors`, enabling you to style text in different colors (such as red, blue, ...) and emphasis (italic, bold, ...).

```cjs
const { styleText } = require('node:util');
const errorMessage = styleText('red', 'Error! Error!');
console.log(errorMessage);
```

Contributed by Rafael Gonzaga in [#51850](https://github.com/nodejs/node/pull/51850).

#### vm: support using the default loader to handle dynamic import()

This patch adds support for using `vm.constants.USE_MAIN_CONTEXT_DEFAULT_LOADER` as the
`importModuleDynamically` option in all vm APIs that take this option except `vm.SourceTextModule`. This allows users to have a shortcut to support dynamic `import()` in the compiled code without missing the compilation cache if they don't need customization of the loading process. We emit an experimental warning when the `import()` is actually handled by the default loader through this option instead of requiring `--experimental-vm-modules`.

```js
const { Script, constants } = require('node:vm');
const { resolve } = require('node:path');
const { writeFileSync } = require('node:fs');

// Write test.js and test.txt to the directory where the current script
// being run is located.
writeFileSync(resolve(__dirname, 'test.mjs'),
              'export const filename = "./test.json";');
writeFileSync(resolve(__dirname, 'test.json'),
              '{"hello": "world"}');

// Compile a script that loads test.mjs and then test.json
// as if the script is placed in the same directory.
const script = new Script(
  `(async function() {
    const { filename } = await import('./test.mjs');
    return import(filename, { with: { type: 'json' } })
  })();`,
  {
    filename: resolve(__dirname, 'test-with-default.js'),
    importModuleDynamically: constants.USE_MAIN_CONTEXT_DEFAULT_LOADER,
  });

// { default: { hello: 'world' } }
script.runInThisContext().then(console.log);
```

Contributed by Joyee Cheung in [#51244](https://github.com/nodejs/node/pull/51244).

#### Root certificates updated to NSS 3.98

Certificates added:

* Telekom Security TLS ECC Root 2020
* Telekom Security TLS RSA Root 2023

Certificates removed:

* Security Communication Root CA

#### Updated dependencies

* acorn updated to 8.11.3.
* ada updated to 2.7.6.
* base64 updated to 0.5.2.
* brotli updated to 1.1.0.
* c-ares updated to 1.27.0.
* corepack updated to 0.25.2.
* ICU updated to 74.2. Includes CLDR 44.1 and Unicode 15.1.
* nghttp2 updated to 1.60.0.
* npm updated to 10.5.0. Fixes a regression in signals not being passed onto child processes.
* simdutf8 updated to 4.0.8.
* Timezone updated to 2024a.
* zlib updated to 1.3.0.1-motley-40e35a7.

#### Other notable changes

* \[[`4f49e9d000`](https://github.com/nodejs/node/commit/4f49e9d000)] - **(SEMVER-MINOR)** **build**: build opt to set local location of headers (Michael Dawson) [#51525](https://github.com/nodejs/node/pull/51525)
* \[[`ccdb01187b`](https://github.com/nodejs/node/commit/ccdb01187b)] - **doc**: add zcbenz to collaborators (Cheng Zhao) [#51812](https://github.com/nodejs/node/pull/51812)
* \[[`481af53aea`](https://github.com/nodejs/node/commit/481af53aea)] - **doc**: add lemire to collaborators (Daniel Lemire) [#51572](https://github.com/nodejs/node/pull/51572)
* \[[`5ba4d96525`](https://github.com/nodejs/node/commit/5ba4d96525)] - **(SEMVER-MINOR)** **http2**: add h2 compat support for appendHeader (Tim Perry) [#51412](https://github.com/nodejs/node/pull/51412)
* \[[`0861498e8b`](https://github.com/nodejs/node/commit/0861498e8b)] - **(SEMVER-MINOR)** **http2**: add server handshake utility (snek) [#51172](https://github.com/nodejs/node/pull/51172)
* \[[`6b08d006ee`](https://github.com/nodejs/node/commit/6b08d006ee)] - **(SEMVER-MINOR)** **http2**: receive customsettings (Marten Richter) [#51323](https://github.com/nodejs/node/pull/51323)
* \[[`7894989bf0`](https://github.com/nodejs/node/commit/7894989bf0)] - **(SEMVER-MINOR)** **lib**: move encodingsMap to internal/util (Joyee Cheung) [#51044](https://github.com/nodejs/node/pull/51044)
* \[[`a58c98ea85`](https://github.com/nodejs/node/commit/a58c98ea85)] - **(SEMVER-MINOR)** **src**: print string content better in BlobDeserializer (Joyee Cheung) [#50960](https://github.com/nodejs/node/pull/50960)
* \[[`c3c0a3ee5c`](https://github.com/nodejs/node/commit/c3c0a3ee5c)] - **(SEMVER-MINOR)** **src**: support multi-line values for .env file (IlyasShabi) [#51289](https://github.com/nodejs/node/pull/51289)
* \[[`2a921966c6`](https://github.com/nodejs/node/commit/2a921966c6)] - **(SEMVER-MINOR)** **src**: do not coerce dotenv paths (Tobias Nießen) [#51425](https://github.com/nodejs/node/pull/51425)
* \[[`0dee86f295`](https://github.com/nodejs/node/commit/0dee86f295)] - **(SEMVER-MINOR)** **src**: support configurable snapshot (Joyee Cheung) [#50453](https://github.com/nodejs/node/pull/50453)
* \[[`ade6614067`](https://github.com/nodejs/node/commit/ade6614067)] - **(SEMVER-MINOR)** **stream**: add support for `deflate-raw` format to webstreams compression (Damian Krzeminski) [#50097](https://github.com/nodejs/node/pull/50097)
* \[[`fe922f05e4`](https://github.com/nodejs/node/commit/fe922f05e4)] - **(SEMVER-MINOR)** **timers**: export timers.promises (Marco Ippolito) [#51246](https://github.com/nodejs/node/pull/51246)

### Commits

* \[[`cbda4e9fc5`](https://github.com/nodejs/node/commit/cbda4e9fc5)] - **assert,crypto**: make KeyObject and CryptoKey testable for equality (Filip Skokan) [#50897](https://github.com/nodejs/node/pull/50897)
* \[[`92fca59647`](https://github.com/nodejs/node/commit/92fca59647)] - **async\_hooks,inspector**: implement inspector api without async\_wrap (Gabriel Bota) [#51501](https://github.com/nodejs/node/pull/51501)
* \[[`029ca982dc`](https://github.com/nodejs/node/commit/029ca982dc)] - **benchmark**: update iterations of benchmark/async\_hooks/async-local- (Lei Shi) [#51420](https://github.com/nodejs/node/pull/51420)
* \[[`350e9fee8d`](https://github.com/nodejs/node/commit/350e9fee8d)] - **benchmark**: update iterations of benchmark/domain/domain-fn-args.js (Lei Shi) [#51408](https://github.com/nodejs/node/pull/51408)
* \[[`40fda97deb`](https://github.com/nodejs/node/commit/40fda97deb)] - **benchmark**: update iterations of assert/deepequal-typedarrays.js (Lei Shi) [#51419](https://github.com/nodejs/node/pull/51419)
* \[[`1b2e3b7049`](https://github.com/nodejs/node/commit/1b2e3b7049)] - **benchmark**: update iterations of benchmark/assert/deepequal-map.js (Lei Shi) [#51416](https://github.com/nodejs/node/pull/51416)
* \[[`7639259203`](https://github.com/nodejs/node/commit/7639259203)] - **benchmark**: rename startup.js to startup-core.js (Joyee Cheung) [#51669](https://github.com/nodejs/node/pull/51669)
* \[[`4be33b5577`](https://github.com/nodejs/node/commit/4be33b5577)] - **benchmark**: remove dependency on unshipped tools (Adam Majer) [#51146](https://github.com/nodejs/node/pull/51146)
* \[[`bd03a154a9`](https://github.com/nodejs/node/commit/bd03a154a9)] - **benchmark**: update iterations in benchmark/perf\_hooks (Lei Shi) [#50869](https://github.com/nodejs/node/pull/50869)
* \[[`19b943b909`](https://github.com/nodejs/node/commit/19b943b909)] - **benchmark**: update iterations in benchmark/crypto/aes-gcm-throughput.js (Lei Shi) [#50929](https://github.com/nodejs/node/pull/50929)
* \[[`278c990dea`](https://github.com/nodejs/node/commit/278c990dea)] - **benchmark**: update iteration and size in benchmark/crypto/randomBytes.js (Lei Shi) [#50868](https://github.com/nodejs/node/pull/50868)
* \[[`443d4fcff3`](https://github.com/nodejs/node/commit/443d4fcff3)] - **benchmark**: add undici websocket benchmark (Chenyu Yang) [#50586](https://github.com/nodejs/node/pull/50586)
* \[[`3ab6143380`](https://github.com/nodejs/node/commit/3ab6143380)] - **benchmark**: add create-hash benchmark (Joyee Cheung) [#51026](https://github.com/nodejs/node/pull/51026)
* \[[`6a8ff09332`](https://github.com/nodejs/node/commit/6a8ff09332)] - **benchmark**: update interations and len in benchmark/util/text-decoder.js (Lei Shi) [#50938](https://github.com/nodejs/node/pull/50938)
* \[[`22b53bc1fa`](https://github.com/nodejs/node/commit/22b53bc1fa)] - **benchmark**: update iterations of benchmark/util/type-check.js (Lei Shi) [#50937](https://github.com/nodejs/node/pull/50937)
* \[[`f56bda5109`](https://github.com/nodejs/node/commit/f56bda5109)] - **benchmark**: update iterations in benchmark/util/normalize-encoding.js (Lei Shi) [#50934](https://github.com/nodejs/node/pull/50934)
* \[[`4fc83e1ce3`](https://github.com/nodejs/node/commit/4fc83e1ce3)] - **benchmark**: update iterations in benchmark/util/inspect-array.js (Lei Shi) [#50933](https://github.com/nodejs/node/pull/50933)
* \[[`0edddcfc19`](https://github.com/nodejs/node/commit/0edddcfc19)] - **benchmark**: update iterations in benchmark/util/format.js (Lei Shi) [#50932](https://github.com/nodejs/node/pull/50932)
* \[[`f109961fd1`](https://github.com/nodejs/node/commit/f109961fd1)] - **benchmark**: update iterations in benchmark/crypto/hkdf.js (Lei Shi) [#50866](https://github.com/nodejs/node/pull/50866)
* \[[`1e923f11f2`](https://github.com/nodejs/node/commit/1e923f11f2)] - **benchmark**: update iterations in benchmark/crypto/get-ciphers.js (Lei Shi) [#50863](https://github.com/nodejs/node/pull/50863)
* \[[`f13643da06`](https://github.com/nodejs/node/commit/f13643da06)] - **benchmark**: update number of iterations for `util.inspect` (kylo5aby) [#50651](https://github.com/nodejs/node/pull/50651)
* \[[`03b19cbd2a`](https://github.com/nodejs/node/commit/03b19cbd2a)] - **bootstrap**: improve snapshot unsupported builtin warnings (Joyee Cheung) [#50944](https://github.com/nodejs/node/pull/50944)
* \[[`51ea5b60a9`](https://github.com/nodejs/node/commit/51ea5b60a9)] - **build**: fix arm64 host cross-compilation in GN (Cheng Zhao) [#51903](https://github.com/nodejs/node/pull/51903)
* \[[`9f5547afa2`](https://github.com/nodejs/node/commit/9f5547afa2)] - _**Revert**_ "**build**: workaround for node-core-utils" (Richard Lau) [#51975](https://github.com/nodejs/node/pull/51975)
* \[[`58255e73ae`](https://github.com/nodejs/node/commit/58255e73ae)] - **build**: respect the `NODE` env variable in `Makefile` (Antoine du Hamel) [#51743](https://github.com/nodejs/node/pull/51743)
* \[[`0a7419bf0b`](https://github.com/nodejs/node/commit/0a7419bf0b)] - _**Revert**_ "**build**: fix warning in cares under GN build" (Luigi Pinca) [#51865](https://github.com/nodejs/node/pull/51865)
* \[[`4118174b85`](https://github.com/nodejs/node/commit/4118174b85)] - **build**: remove `librt` libs link for Android compatibility (BuShe Pie) [#51632](https://github.com/nodejs/node/pull/51632)
* \[[`012da16b85`](https://github.com/nodejs/node/commit/012da16b85)] - **build**: do not rely on gn\_helpers in GN build (Cheng Zhao) [#51439](https://github.com/nodejs/node/pull/51439)
* \[[`93fcf52990`](https://github.com/nodejs/node/commit/93fcf52990)] - **build**: fix warning in cares under GN build (Cheng Zhao) [#51687](https://github.com/nodejs/node/pull/51687)
* \[[`2176495455`](https://github.com/nodejs/node/commit/2176495455)] - **build**: fix building js2c with GN (Cheng Zhao) [#51818](https://github.com/nodejs/node/pull/51818)
* \[[`d6e702f885`](https://github.com/nodejs/node/commit/d6e702f885)] - **build**: encode non-ASCII Latin1 characters as one byte in JS2C (Joyee Cheung) [#51605](https://github.com/nodejs/node/pull/51605)
* \[[`4f49e9d000`](https://github.com/nodejs/node/commit/4f49e9d000)] - **(SEMVER-MINOR)** **build**: build opt to set local location of headers (Michael Dawson) [#51525](https://github.com/nodejs/node/pull/51525)
* \[[`8e84aad0ef`](https://github.com/nodejs/node/commit/8e84aad0ef)] - **build**: use macOS m1 machines for testing (Yagiz Nizipli) [#51620](https://github.com/nodejs/node/pull/51620)
* \[[`5fce1a17e2`](https://github.com/nodejs/node/commit/5fce1a17e2)] - **build**: check before removing %config% link (liudonghua) [#51437](https://github.com/nodejs/node/pull/51437)
* \[[`46d6dce1a8`](https://github.com/nodejs/node/commit/46d6dce1a8)] - **build**: increase parallel executions in github (Yagiz Nizipli) [#51554](https://github.com/nodejs/node/pull/51554)
* \[[`8b3ead1f3e`](https://github.com/nodejs/node/commit/8b3ead1f3e)] - **build**: remove copyright header in node.gni (Cheng Zhao) [#51535](https://github.com/nodejs/node/pull/51535)
* \[[`d8b86ad363`](https://github.com/nodejs/node/commit/d8b86ad363)] - **build**: update GN build files for ngtcp2 (Cheng Zhao) [#51313](https://github.com/nodejs/node/pull/51313)
* \[[`ba0ffddd2d`](https://github.com/nodejs/node/commit/ba0ffddd2d)] - **build**: fix for VScode "Reopen in Container" (Serg Kryvonos) [#51271](https://github.com/nodejs/node/pull/51271)
* \[[`8b97e2e0a7`](https://github.com/nodejs/node/commit/8b97e2e0a7)] - **build**: add `-flax-vector-conversions` to V8 build (Michaël Zasso) [#51257](https://github.com/nodejs/node/pull/51257)
* \[[`bd528c7dc0`](https://github.com/nodejs/node/commit/bd528c7dc0)] - **build**: fix warnings from uv for gn build (Cheng Zhao) [#51069](https://github.com/nodejs/node/pull/51069)
* \[[`ffe467b062`](https://github.com/nodejs/node/commit/ffe467b062)] - **build,tools**: make addons tests work with GN (Cheng Zhao) [#50737](https://github.com/nodejs/node/pull/50737)
* \[[`448d67109a`](https://github.com/nodejs/node/commit/448d67109a)] - **(SEMVER-MINOR)** **crypto**: implement crypto.hash() (Joyee Cheung) [#51044](https://github.com/nodejs/node/pull/51044)
* \[[`48959dd2b4`](https://github.com/nodejs/node/commit/48959dd2b4)] - **crypto**: update root certificates to NSS 3.98 (Node.js GitHub Bot) [#51794](https://github.com/nodejs/node/pull/51794)
* \[[`68e8b2c492`](https://github.com/nodejs/node/commit/68e8b2c492)] - **crypto**: use EVP\_MD\_fetch and cache EVP\_MD for hashes (Joyee Cheung) [#51034](https://github.com/nodejs/node/pull/51034)
* \[[`adb5d69621`](https://github.com/nodejs/node/commit/adb5d69621)] - **crypto**: update CryptoKey symbol properties (Filip Skokan) [#50897](https://github.com/nodejs/node/pull/50897)
* \[[`df0213fd3d`](https://github.com/nodejs/node/commit/df0213fd3d)] - **deps**: update nghttp2 to 1.60.0 (Node.js GitHub Bot) [#51948](https://github.com/nodejs/node/pull/51948)
* \[[`208dd887a5`](https://github.com/nodejs/node/commit/208dd887a5)] - **deps**: upgrade npm to 10.5.0 (npm team) [#51913](https://github.com/nodejs/node/pull/51913)
* \[[`587e70e1ee`](https://github.com/nodejs/node/commit/587e70e1ee)] - **deps**: update corepack to 0.25.2 (Node.js GitHub Bot) [#51810](https://github.com/nodejs/node/pull/51810)
* \[[`38343c4857`](https://github.com/nodejs/node/commit/38343c4857)] - **deps**: update c-ares to 1.27.0 (Node.js GitHub Bot) [#51846](https://github.com/nodejs/node/pull/51846)
* \[[`c9974f621c`](https://github.com/nodejs/node/commit/c9974f621c)] - **deps**: update c-ares to 1.26.0 (Node.js GitHub Bot) [#51582](https://github.com/nodejs/node/pull/51582)
* \[[`0aa18e1a1c`](https://github.com/nodejs/node/commit/0aa18e1a1c)] - **deps**: update googletest to 6a59382 (Node.js GitHub Bot) [#51580](https://github.com/nodejs/node/pull/51580)
* \[[`f871bc6ddc`](https://github.com/nodejs/node/commit/f871bc6ddc)] - **deps**: update nghttp2 to 1.59.0 (Node.js GitHub Bot) [#51581](https://github.com/nodejs/node/pull/51581)
* \[[`94f8ee8717`](https://github.com/nodejs/node/commit/94f8ee8717)] - **deps**: update corepack to 0.24.1 (Node.js GitHub Bot) [#51459](https://github.com/nodejs/node/pull/51459)
* \[[`c23ce06e6b`](https://github.com/nodejs/node/commit/c23ce06e6b)] - **deps**: update ada to 2.7.6 (Node.js GitHub Bot) [#51542](https://github.com/nodejs/node/pull/51542)
* \[[`372ce69de1`](https://github.com/nodejs/node/commit/372ce69de1)] - **deps**: update ada to 2.7.5 (Node.js GitHub Bot) [#51542](https://github.com/nodejs/node/pull/51542)
* \[[`133719b2c9`](https://github.com/nodejs/node/commit/133719b2c9)] - **deps**: update googletest to 7c07a86 (Node.js GitHub Bot) [#51458](https://github.com/nodejs/node/pull/51458)
* \[[`35675aa07f`](https://github.com/nodejs/node/commit/35675aa07f)] - **deps**: update acorn-walk to 8.3.2 (Node.js GitHub Bot) [#51457](https://github.com/nodejs/node/pull/51457)
* \[[`ca73f55a22`](https://github.com/nodejs/node/commit/ca73f55a22)] - **deps**: update base64 to 0.5.2 (Node.js GitHub Bot) [#51455](https://github.com/nodejs/node/pull/51455)
* \[[`c9dad18191`](https://github.com/nodejs/node/commit/c9dad18191)] - **deps**: compile c-ares with C11 support (Michaël Zasso) [#51410](https://github.com/nodejs/node/pull/51410)
* \[[`a727fa73ee`](https://github.com/nodejs/node/commit/a727fa73ee)] - **deps**: upgrade npm to 10.3.0 (npm team) [#51431](https://github.com/nodejs/node/pull/51431)
* \[[`834bbfd039`](https://github.com/nodejs/node/commit/834bbfd039)] - **deps**: update c-ares to 1.25.0 (Node.js GitHub Bot) [#51385](https://github.com/nodejs/node/pull/51385)
* \[[`4c8fa3e7c2`](https://github.com/nodejs/node/commit/4c8fa3e7c2)] - **deps**: update uvwasi to 0.0.20 and fixup tests (Michael Dawson) [#51355](https://github.com/nodejs/node/pull/51355)
* \[[`bd183ef2af`](https://github.com/nodejs/node/commit/bd183ef2af)] - **deps**: add nghttp3/\*\*/.deps to .gitignore (Luigi Pinca) [#51400](https://github.com/nodejs/node/pull/51400)
* \[[`1d8169995c`](https://github.com/nodejs/node/commit/1d8169995c)] - **deps**: update corepack to 0.24.0 (Node.js GitHub Bot) [#51318](https://github.com/nodejs/node/pull/51318)
* \[[`4dfbbb8789`](https://github.com/nodejs/node/commit/4dfbbb8789)] - **deps**: update acorn to 8.11.3 (Node.js GitHub Bot) [#51317](https://github.com/nodejs/node/pull/51317)
* \[[`7d60877fa3`](https://github.com/nodejs/node/commit/7d60877fa3)] - **deps**: update brotli to 1.1.0 (Node.js GitHub Bot) [#50804](https://github.com/nodejs/node/pull/50804)
* \[[`1b99a3f0af`](https://github.com/nodejs/node/commit/1b99a3f0af)] - **deps**: update zlib to 1.3.0.1-motley-40e35a7 (Node.js GitHub Bot) [#51274](https://github.com/nodejs/node/pull/51274)
* \[[`2270285839`](https://github.com/nodejs/node/commit/2270285839)] - **deps**: update simdutf to 4.0.8 (Node.js GitHub Bot) [#51000](https://github.com/nodejs/node/pull/51000)
* \[[`61d1535d84`](https://github.com/nodejs/node/commit/61d1535d84)] - **deps**: V8: cherry-pick de611e69ad51 (Keyhan Vakil) [#51200](https://github.com/nodejs/node/pull/51200)
* \[[`04323fd595`](https://github.com/nodejs/node/commit/04323fd595)] - **deps**: update googletest to 530d5c8 (Node.js GitHub Bot) [#51191](https://github.com/nodejs/node/pull/51191)
* \[[`454b4f8d7e`](https://github.com/nodejs/node/commit/454b4f8d7e)] - **deps**: update acorn-walk to 8.3.1 (Node.js GitHub Bot) [#50457](https://github.com/nodejs/node/pull/50457)
* \[[`cc693eb908`](https://github.com/nodejs/node/commit/cc693eb908)] - **deps**: update acorn-walk to 8.3.0 (Node.js GitHub Bot) [#50457](https://github.com/nodejs/node/pull/50457)
* \[[`09519c6655`](https://github.com/nodejs/node/commit/09519c6655)] - **deps**: update zlib to 1.3.0.1-motley-dd5fc13 (Node.js GitHub Bot) [#51105](https://github.com/nodejs/node/pull/51105)
* \[[`a2f39e9168`](https://github.com/nodejs/node/commit/a2f39e9168)] - **deps**: V8: cherry-pick 0fd478bcdabd (Joyee Cheung) [#50572](https://github.com/nodejs/node/pull/50572)
* \[[`1aaf156ea7`](https://github.com/nodejs/node/commit/1aaf156ea7)] - **deps**: update zlib to 1.3-22124f5 (Node.js GitHub Bot) [#50910](https://github.com/nodejs/node/pull/50910)
* \[[`3f4e254047`](https://github.com/nodejs/node/commit/3f4e254047)] - **deps**: update googletest to 76bb2af (Node.js GitHub Bot) [#50555](https://github.com/nodejs/node/pull/50555)
* \[[`702684c008`](https://github.com/nodejs/node/commit/702684c008)] - **deps**: update googletest to b10fad3 (Node.js GitHub Bot) [#50555](https://github.com/nodejs/node/pull/50555)
* \[[`4ee7f29657`](https://github.com/nodejs/node/commit/4ee7f29657)] - **deps**: update timezone to 2024a (Michaël Zasso) [#51723](https://github.com/nodejs/node/pull/51723)
* \[[`452d74c8b6`](https://github.com/nodejs/node/commit/452d74c8b6)] - **deps**: update icu to 74.2 (Michaël Zasso) [#51723](https://github.com/nodejs/node/pull/51723)
* \[[`e6fc5a5ee1`](https://github.com/nodejs/node/commit/e6fc5a5ee1)] - **deps**: update timezone to 2023d (Node.js GitHub Bot) [#51461](https://github.com/nodejs/node/pull/51461)
* \[[`4ee0f8306b`](https://github.com/nodejs/node/commit/4ee0f8306b)] - **deps**: update icu to 74.1 (Node.js GitHub Bot) [#50515](https://github.com/nodejs/node/pull/50515)
* \[[`cb49f31480`](https://github.com/nodejs/node/commit/cb49f31480)] - **deps**: cherry-pick libuv/libuv\@d09441c (Richard Lau) [#51976](https://github.com/nodejs/node/pull/51976)
* \[[`ea50540c5e`](https://github.com/nodejs/node/commit/ea50540c5e)] - _**Revert**_ "**deps**: V8: cherry-pick 13192d6e10fa" (kxxt) [#51495](https://github.com/nodejs/node/pull/51495)
* \[[`6fd1617ab4`](https://github.com/nodejs/node/commit/6fd1617ab4)] - **doc**: add policy for distribution (Geoffrey Booth) [#51918](https://github.com/nodejs/node/pull/51918)
* \[[`fc0b389006`](https://github.com/nodejs/node/commit/fc0b389006)] - **doc**: fix actual result of example is different in events (Deokjin Kim) [#51925](https://github.com/nodejs/node/pull/51925)
* \[[`93d6d66339`](https://github.com/nodejs/node/commit/93d6d66339)] - **doc**: clarify Corepack threat model (Antoine du Hamel) [#51917](https://github.com/nodejs/node/pull/51917)
* \[[`276d1d1d65`](https://github.com/nodejs/node/commit/276d1d1d65)] - **doc**: add stability index to crypto.hash() (Joyee Cheung) [#51978](https://github.com/nodejs/node/pull/51978)
* \[[`473af948b5`](https://github.com/nodejs/node/commit/473af948b5)] - **doc**: remove redundant backquote which breaks sentence (JounQin) [#51904](https://github.com/nodejs/node/pull/51904)
* \[[`b52b249b05`](https://github.com/nodejs/node/commit/b52b249b05)] - **doc**: update node-api/node-addon-api team link to sharing project news (Ulises Gascón) [#51877](https://github.com/nodejs/node/pull/51877)
* \[[`a74c373ea4`](https://github.com/nodejs/node/commit/a74c373ea4)] - **doc**: add website team to sharing project news (Ulises Gascón) [#49002](https://github.com/nodejs/node/pull/49002)
* \[[`b7ce547d41`](https://github.com/nodejs/node/commit/b7ce547d41)] - **doc**: update guide link for Event Loop (Shrujal Shah) [#51874](https://github.com/nodejs/node/pull/51874)
* \[[`3dfee7ee33`](https://github.com/nodejs/node/commit/3dfee7ee33)] - **doc**: change `ExperimentalWarnings` to `ExperimentalWarning` (Ameet Kaustav) [#51741](https://github.com/nodejs/node/pull/51741)
* \[[`740d0679e7`](https://github.com/nodejs/node/commit/740d0679e7)] - **doc**: add Paolo to TSC members (Michael Dawson) [#51825](https://github.com/nodejs/node/pull/51825)
* \[[`3240a2f349`](https://github.com/nodejs/node/commit/3240a2f349)] - **doc**: reserve 123 for Electron 30 (Keeley Hammond) [#51803](https://github.com/nodejs/node/pull/51803)
* \[[`597e3db0f9`](https://github.com/nodejs/node/commit/597e3db0f9)] - **doc**: add mention to GPG\_TTY (Rafael Gonzaga) [#51806](https://github.com/nodejs/node/pull/51806)
* \[[`ccdb01187b`](https://github.com/nodejs/node/commit/ccdb01187b)] - **doc**: add zcbenz to collaborators (Cheng Zhao) [#51812](https://github.com/nodejs/node/pull/51812)
* \[[`3a3de00437`](https://github.com/nodejs/node/commit/3a3de00437)] - **doc**: add entry to stewards (Rafael Gonzaga) [#51760](https://github.com/nodejs/node/pull/51760)
* \[[`06b882d2fa`](https://github.com/nodejs/node/commit/06b882d2fa)] - **doc**: update technical priorities for 2023 (Jean Burellier) [#47523](https://github.com/nodejs/node/pull/47523)
* \[[`9a68b47fe1`](https://github.com/nodejs/node/commit/9a68b47fe1)] - **doc**: mark isWebAssemblyCompiledModule eol (Marco Ippolito) [#51442](https://github.com/nodejs/node/pull/51442)
* \[[`8016628710`](https://github.com/nodejs/node/commit/8016628710)] - **doc**: fix `globals.md` introduction (Antoine du Hamel) [#51742](https://github.com/nodejs/node/pull/51742)
* \[[`9ddbe4523f`](https://github.com/nodejs/node/commit/9ddbe4523f)] - **doc**: updates for better json generating (Dmitry Semigradsky) [#51592](https://github.com/nodejs/node/pull/51592)
* \[[`140cf26d47`](https://github.com/nodejs/node/commit/140cf26d47)] - **doc**: document the GN build (Cheng Zhao) [#51676](https://github.com/nodejs/node/pull/51676)
* \[[`ecfb3f18b3`](https://github.com/nodejs/node/commit/ecfb3f18b3)] - **doc**: fix uncaught exception example (Gabriel Schulhof) [#51638](https://github.com/nodejs/node/pull/51638)
* \[[`b3157a08bf`](https://github.com/nodejs/node/commit/b3157a08bf)] - **doc**: clarify execution of `after` hook on test suite completion (Ognjen Jevremović) [#51523](https://github.com/nodejs/node/pull/51523)
* \[[`1dae1873d9`](https://github.com/nodejs/node/commit/1dae1873d9)] - **doc**: fix `dns.lookup` and `dnsPromises.lookup` description (Duncan Chiu) [#51517](https://github.com/nodejs/node/pull/51517)
* \[[`50df052087`](https://github.com/nodejs/node/commit/50df052087)] - **doc**: note that path.normalize deviates from POSIX (Tobias Nießen) [#51513](https://github.com/nodejs/node/pull/51513)
* \[[`481af53aea`](https://github.com/nodejs/node/commit/481af53aea)] - **doc**: add lemire to collaborators (Daniel Lemire) [#51572](https://github.com/nodejs/node/pull/51572)
* \[[`dec0d5d19a`](https://github.com/nodejs/node/commit/dec0d5d19a)] - **doc**: fix historical experimental fetch flag (Kenrick) [#51506](https://github.com/nodejs/node/pull/51506)
* \[[`96c480b1a1`](https://github.com/nodejs/node/commit/96c480b1a1)] - **doc**: fix type of connectionAttempt parameter (Rafael Gonzaga) [#51490](https://github.com/nodejs/node/pull/51490)
* \[[`76968ab112`](https://github.com/nodejs/node/commit/76968ab112)] - **doc**: remove reference to resolved child\_process v8 issue (Ian Kerins) [#51467](https://github.com/nodejs/node/pull/51467)
* \[[`bdd3a2a9fd`](https://github.com/nodejs/node/commit/bdd3a2a9fd)] - **doc**: update typos (Aranđel Šarenac) [#51475](https://github.com/nodejs/node/pull/51475)
* \[[`3532f5587c`](https://github.com/nodejs/node/commit/3532f5587c)] - **doc**: add notes on inspector breakpoints (Chengzhong Wu) [#51417](https://github.com/nodejs/node/pull/51417)
* \[[`0dffb9f049`](https://github.com/nodejs/node/commit/0dffb9f049)] - **doc**: add links in `offboarding.md` (Antoine du Hamel) [#51440](https://github.com/nodejs/node/pull/51440)
* \[[`58d2442f0f`](https://github.com/nodejs/node/commit/58d2442f0f)] - **doc**: fix spelling mistake (u9g) [#51454](https://github.com/nodejs/node/pull/51454)
* \[[`a09f440dbd`](https://github.com/nodejs/node/commit/a09f440dbd)] - **doc**: add check for security reverts (Michael Dawson) [#51376](https://github.com/nodejs/node/pull/51376)
* \[[`401837bfc4`](https://github.com/nodejs/node/commit/401837bfc4)] - **doc**: fix some policy scope typos (Tim Kuijsten) [#51234](https://github.com/nodejs/node/pull/51234)
* \[[`f301f829ba`](https://github.com/nodejs/node/commit/f301f829ba)] - **doc**: improve subtests documentation (Marco Ippolito) [#51379](https://github.com/nodejs/node/pull/51379)
* \[[`1e40f552fd`](https://github.com/nodejs/node/commit/1e40f552fd)] - **doc**: add missing word in `child_process.md` (Joseph Joy) [#50370](https://github.com/nodejs/node/pull/50370)
* \[[`42b4f0f5ab`](https://github.com/nodejs/node/commit/42b4f0f5ab)] - **doc**: fixup alignment of warning subsection (James M Snell) [#51374](https://github.com/nodejs/node/pull/51374)
* \[[`b5bc597871`](https://github.com/nodejs/node/commit/b5bc597871)] - **doc**: the GN files should use Node's license (Cheng Zhao) [#50694](https://github.com/nodejs/node/pull/50694)
* \[[`01a41041d6`](https://github.com/nodejs/node/commit/01a41041d6)] - **doc**: improve localWindowSize event descriptions (Davy Landman) [#51071](https://github.com/nodejs/node/pull/51071)
* \[[`63aa27df10`](https://github.com/nodejs/node/commit/63aa27df10)] - **doc**: mark `--jitless` as experimental (Antoine du Hamel) [#51247](https://github.com/nodejs/node/pull/51247)
* \[[`c8233912e9`](https://github.com/nodejs/node/commit/c8233912e9)] - **doc**: run license-builder (github-actions\[bot]) [#51199](https://github.com/nodejs/node/pull/51199)
* \[[`9e360df521`](https://github.com/nodejs/node/commit/9e360df521)] - **doc**: fix limitations and known issues in pm (Rafael Gonzaga) [#51184](https://github.com/nodejs/node/pull/51184)
* \[[`52d8222d32`](https://github.com/nodejs/node/commit/52d8222d32)] - **doc**: mention node:wasi in the Threat Model (Rafael Gonzaga) [#51211](https://github.com/nodejs/node/pull/51211)
* \[[`cb3270e4c8`](https://github.com/nodejs/node/commit/cb3270e4c8)] - **doc**: remove ambiguous 'considered' (Rich Trott) [#51207](https://github.com/nodejs/node/pull/51207)
* \[[`979e183e0c`](https://github.com/nodejs/node/commit/979e183e0c)] - **doc**: set exit code in custom test runner example (Matteo Collina) [#51056](https://github.com/nodejs/node/pull/51056)
* \[[`eaadebb1f4`](https://github.com/nodejs/node/commit/eaadebb1f4)] - **doc**: remove version from `maintaining-dependencies.md` (Antoine du Hamel) [#51195](https://github.com/nodejs/node/pull/51195)
* \[[`256db6e056`](https://github.com/nodejs/node/commit/256db6e056)] - **doc**: mention native addons are restricted in pm (Rafael Gonzaga) [#51185](https://github.com/nodejs/node/pull/51185)
* \[[`2a61602ab2`](https://github.com/nodejs/node/commit/2a61602ab2)] - **doc**: correct note on behavior of stats.isDirectory (Nick Reilingh) [#50946](https://github.com/nodejs/node/pull/50946)
* \[[`184b8bea5b`](https://github.com/nodejs/node/commit/184b8bea5b)] - **doc**: fix `TestsStream` parent class (Jungku Lee) [#51181](https://github.com/nodejs/node/pull/51181)
* \[[`c61597ffe4`](https://github.com/nodejs/node/commit/c61597ffe4)] - **(SEMVER-MINOR)** **doc**: add documentation for --build-snapshot-config (Anna Henningsen) [#50453](https://github.com/nodejs/node/pull/50453)
* \[[`b88170d602`](https://github.com/nodejs/node/commit/b88170d602)] - **doc**: run license-builder (github-actions\[bot]) [#51111](https://github.com/nodejs/node/pull/51111)
* \[[`f2b4626ab8`](https://github.com/nodejs/node/commit/f2b4626ab8)] - **doc**: deprecate hash constructor (Marco Ippolito) [#51077](https://github.com/nodejs/node/pull/51077)
* \[[`6c241550cd`](https://github.com/nodejs/node/commit/6c241550cd)] - **doc**: add note regarding `--experimental-detect-module` (Shubherthi Mitra) [#51089](https://github.com/nodejs/node/pull/51089)
* \[[`8ee30ea900`](https://github.com/nodejs/node/commit/8ee30ea900)] - **doc**: correct tracingChannel.traceCallback() (Gerhard Stöbich) [#51068](https://github.com/nodejs/node/pull/51068)
* \[[`1cd27b6eff`](https://github.com/nodejs/node/commit/1cd27b6eff)] - **doc**: use length argument in pbkdf2Key (Tobias Nießen) [#51066](https://github.com/nodejs/node/pull/51066)
* \[[`09ad974537`](https://github.com/nodejs/node/commit/09ad974537)] - **doc**: add deprecation notice to `dirent.path` (Antoine du Hamel) [#51059](https://github.com/nodejs/node/pull/51059)
* \[[`1113e58f87`](https://github.com/nodejs/node/commit/1113e58f87)] - **doc**: deprecate `dirent.path` (Antoine du Hamel) [#51020](https://github.com/nodejs/node/pull/51020)
* \[[`37979d750e`](https://github.com/nodejs/node/commit/37979d750e)] - **doc**: add additional details about `--input-type` (Shubham Pandey) [#50796](https://github.com/nodejs/node/pull/50796)
* \[[`3ff00e1e79`](https://github.com/nodejs/node/commit/3ff00e1e79)] - **doc**: add procedure when CVEs don't get published (Rafael Gonzaga) [#50945](https://github.com/nodejs/node/pull/50945)
* \[[`0930be6bd5`](https://github.com/nodejs/node/commit/0930be6bd5)] - **doc**: fix some errors in esm resolution algorithms (Christopher Jeffrey (JJ)) [#50898](https://github.com/nodejs/node/pull/50898)
* \[[`ddc7964b03`](https://github.com/nodejs/node/commit/ddc7964b03)] - **doc**: reserve 121 for Electron 29 (Shelley Vohr) [#50957](https://github.com/nodejs/node/pull/50957)
* \[[`625fd69b76`](https://github.com/nodejs/node/commit/625fd69b76)] - **doc**: run license-builder (github-actions\[bot]) [#50926](https://github.com/nodejs/node/pull/50926)
* \[[`f18269607a`](https://github.com/nodejs/node/commit/f18269607a)] - **doc**: document non-node\_modules-only runtime deprecation (Joyee Cheung) [#50748](https://github.com/nodejs/node/pull/50748)
* \[[`5f8e7a0fdb`](https://github.com/nodejs/node/commit/5f8e7a0fdb)] - **doc**: add doc for Unix abstract socket (theanarkh) [#50904](https://github.com/nodejs/node/pull/50904)
* \[[`e0598787e0`](https://github.com/nodejs/node/commit/e0598787e0)] - **doc**: remove flicker on page load on dark theme (Dima Demakov) [#50942](https://github.com/nodejs/node/pull/50942)
* \[[`2a7047d933`](https://github.com/nodejs/node/commit/2a7047d933)] - **doc,crypto**: further clarify RSA\_PKCS1\_PADDING support (Tobias Nießen) [#51799](https://github.com/nodejs/node/pull/51799)
* \[[`31c4ba4dfd`](https://github.com/nodejs/node/commit/31c4ba4dfd)] - **doc,crypto**: add changelog and note about disabled RSA\_PKCS1\_PADDING (Filip Skokan) [#51782](https://github.com/nodejs/node/pull/51782)
* \[[`90da41548f`](https://github.com/nodejs/node/commit/90da41548f)] - **doc,module**: clarify hook chain execution sequence (Jacob Smith) [#51884](https://github.com/nodejs/node/pull/51884)
* \[[`bb7d7f3d1c`](https://github.com/nodejs/node/commit/bb7d7f3d1c)] - **errors**: fix stacktrace of SystemError (uzlopak) [#49956](https://github.com/nodejs/node/pull/49956)
* \[[`db7459b57b`](https://github.com/nodejs/node/commit/db7459b57b)] - **errors**: improve hideStackFrames (Aras Abbasi) [#49990](https://github.com/nodejs/node/pull/49990)
* \[[`a6b3569121`](https://github.com/nodejs/node/commit/a6b3569121)] - **esm**: improve error when calling `import.meta.resolve` from `data:` URL (Antoine du Hamel) [#49516](https://github.com/nodejs/node/pull/49516)
* \[[`38f4000905`](https://github.com/nodejs/node/commit/38f4000905)] - **esm**: fix hint on invalid module specifier (Antoine du Hamel) [#51223](https://github.com/nodejs/node/pull/51223)
* \[[`e39e37bbd5`](https://github.com/nodejs/node/commit/e39e37bbd5)] - **esm**: fix hook name in error message (Bruce MacNaughton) [#50466](https://github.com/nodejs/node/pull/50466)
* \[[`d9b5cd533c`](https://github.com/nodejs/node/commit/d9b5cd533c)] - **events**: no stopPropagation call in cancelBubble (mert.altin) [#50405](https://github.com/nodejs/node/pull/50405)
* \[[`287a02c4b2`](https://github.com/nodejs/node/commit/287a02c4b2)] - **fs**: load rimraf lazily in fs/promises (Joyee Cheung) [#51617](https://github.com/nodejs/node/pull/51617)
* \[[`bbd1351ef0`](https://github.com/nodejs/node/commit/bbd1351ef0)] - **fs**: remove race condition for recursive watch on Linux (Matteo Collina) [#51406](https://github.com/nodejs/node/pull/51406)
* \[[`1b7ccec5a7`](https://github.com/nodejs/node/commit/1b7ccec5a7)] - **fs**: update jsdoc for `filehandle.createWriteStream` and `appendFile` (Jungku Lee) [#51494](https://github.com/nodejs/node/pull/51494)
* \[[`25056f5024`](https://github.com/nodejs/node/commit/25056f5024)] - **fs**: fix fs.promises.realpath for long paths on Windows (翠 / green) [#51032](https://github.com/nodejs/node/pull/51032)
* \[[`a8fd01a5a2`](https://github.com/nodejs/node/commit/a8fd01a5a2)] - **fs**: make offset, position & length args in fh.read() optional (Pulkit Gupta) [#51087](https://github.com/nodejs/node/pull/51087)
* \[[`721557c6d8`](https://github.com/nodejs/node/commit/721557c6d8)] - **fs**: add missing jsdoc parameters to `readSync` (Yagiz Nizipli) [#51225](https://github.com/nodejs/node/pull/51225)
* \[[`3ce9aacfcd`](https://github.com/nodejs/node/commit/3ce9aacfcd)] - **fs**: remove `internalModuleReadJSON` binding (Yagiz Nizipli) [#51224](https://github.com/nodejs/node/pull/51224)
* \[[`65df2c6787`](https://github.com/nodejs/node/commit/65df2c6787)] - **fs**: improve mkdtemp performance for buffer prefix (Yagiz Nizipli) [#51078](https://github.com/nodejs/node/pull/51078)
* \[[`6705b48012`](https://github.com/nodejs/node/commit/6705b48012)] - **fs**: validate fd synchronously on c++ (Yagiz Nizipli) [#51027](https://github.com/nodejs/node/pull/51027)
* \[[`afd5d67f89`](https://github.com/nodejs/node/commit/afd5d67f89)] - **fs**: throw fchownSync error from c++ (Yagiz Nizipli) [#51075](https://github.com/nodejs/node/pull/51075)
* \[[`bac982bce5`](https://github.com/nodejs/node/commit/bac982bce5)] - **fs**: update params in jsdoc for createReadStream and createWriteStream (Jungku Lee) [#51063](https://github.com/nodejs/node/pull/51063)
* \[[`6764f0c9a8`](https://github.com/nodejs/node/commit/6764f0c9a8)] - **fs**: improve error performance of readvSync (IlyasShabi) [#50100](https://github.com/nodejs/node/pull/50100)
* \[[`0225fce776`](https://github.com/nodejs/node/commit/0225fce776)] - **(SEMVER-MINOR)** **fs**: introduce `dirent.parentPath` (Antoine du Hamel) [#50976](https://github.com/nodejs/node/pull/50976)
* \[[`4adea6c405`](https://github.com/nodejs/node/commit/4adea6c405)] - **fs,test**: add URL to string to fs.watch (Rafael Gonzaga) [#51346](https://github.com/nodejs/node/pull/51346)
* \[[`6bf148e12b`](https://github.com/nodejs/node/commit/6bf148e12b)] - **http**: fix `close` return value mismatch between doc and implementation (kylo5aby) [#51797](https://github.com/nodejs/node/pull/51797)
* \[[`66318602d0`](https://github.com/nodejs/node/commit/66318602d0)] - **http**: split set-cookie when using setHeaders (Marco Ippolito) [#51649](https://github.com/nodejs/node/pull/51649)
* \[[`f7b53d05bd`](https://github.com/nodejs/node/commit/f7b53d05bd)] - **http**: remove misleading warning (Luigi Pinca) [#51204](https://github.com/nodejs/node/pull/51204)
* \[[`9062d30600`](https://github.com/nodejs/node/commit/9062d30600)] - **http**: do not override user-provided options object (KuthorX) [#33633](https://github.com/nodejs/node/pull/33633)
* \[[`4e38dee4ee`](https://github.com/nodejs/node/commit/4e38dee4ee)] - **http**: handle multi-value content-disposition header (Arsalan Ahmad) [#50977](https://github.com/nodejs/node/pull/50977)
* \[[`b560bfbb84`](https://github.com/nodejs/node/commit/b560bfbb84)] - **http2**: close idle connections when allowHTTP1 is true (xsbchen) [#51569](https://github.com/nodejs/node/pull/51569)
* \[[`5ba4d96525`](https://github.com/nodejs/node/commit/5ba4d96525)] - **(SEMVER-MINOR)** **http2**: add h2 compat support for appendHeader (Tim Perry) [#51412](https://github.com/nodejs/node/pull/51412)
* \[[`0861498e8b`](https://github.com/nodejs/node/commit/0861498e8b)] - **(SEMVER-MINOR)** **http2**: add server handshake utility (snek) [#51172](https://github.com/nodejs/node/pull/51172)
* \[[`6b08d006ee`](https://github.com/nodejs/node/commit/6b08d006ee)] - **(SEMVER-MINOR)** **http2**: receive customsettings (Marten Richter) [#51323](https://github.com/nodejs/node/pull/51323)
* \[[`23414a6120`](https://github.com/nodejs/node/commit/23414a6120)] - **http2**: addtl http/2 settings (Marten Richter) [#49025](https://github.com/nodejs/node/pull/49025)
* \[[`3fe59ba224`](https://github.com/nodejs/node/commit/3fe59ba224)] - **inspector**: add NodeRuntime.waitingForDebugger event (mary marchini) [#51560](https://github.com/nodejs/node/pull/51560)
* \[[`44f05e0d30`](https://github.com/nodejs/node/commit/44f05e0d30)] - **lib**: make sure close net server (theanarkh) [#51929](https://github.com/nodejs/node/pull/51929)
* \[[`3be5ff9c45`](https://github.com/nodejs/node/commit/3be5ff9c45)] - **lib**: return directly if udp socket close before lookup (theanarkh) [#51914](https://github.com/nodejs/node/pull/51914)
* \[[`dcbf88f4c7`](https://github.com/nodejs/node/commit/dcbf88f4c7)] - **lib**: account for cwd access from snapshot serialization cb (Anna Henningsen) [#51901](https://github.com/nodejs/node/pull/51901)
* \[[`da8fa484f8`](https://github.com/nodejs/node/commit/da8fa484f8)] - **lib**: fix http client socket path (theanarkh) [#51900](https://github.com/nodejs/node/pull/51900)
* \[[`55011d2c71`](https://github.com/nodejs/node/commit/55011d2c71)] - **lib**: only build the ESM facade for builtins when they are needed (Joyee Cheung) [#51669](https://github.com/nodejs/node/pull/51669)
* \[[`7894989bf0`](https://github.com/nodejs/node/commit/7894989bf0)] - **(SEMVER-MINOR)** **lib**: move encodingsMap to internal/util (Joyee Cheung) [#51044](https://github.com/nodejs/node/pull/51044)
* \[[`9082cc557d`](https://github.com/nodejs/node/commit/9082cc557d)] - **lib**: do not access process.noDeprecation at build time (Joyee Cheung) [#51447](https://github.com/nodejs/node/pull/51447)
* \[[`6679e6b616`](https://github.com/nodejs/node/commit/6679e6b616)] - **lib**: add assertion for user ESM execution (Joyee Cheung) [#51748](https://github.com/nodejs/node/pull/51748)
* \[[`d6e8d03afc`](https://github.com/nodejs/node/commit/d6e8d03afc)] - **lib**: create global console properties at snapshot build time (Joyee Cheung) [#51700](https://github.com/nodejs/node/pull/51700)
* \[[`bd2a3c10ae`](https://github.com/nodejs/node/commit/bd2a3c10ae)] - **lib**: define FormData and fetch etc. in the built-in snapshot (Joyee Cheung) [#51598](https://github.com/nodejs/node/pull/51598)
* \[[`da79876ef0`](https://github.com/nodejs/node/commit/da79876ef0)] - **lib**: allow checking the test result from afterEach (Tim Stableford) [#51485](https://github.com/nodejs/node/pull/51485)
* \[[`bff7e3cf7a`](https://github.com/nodejs/node/commit/bff7e3cf7a)] - **lib**: remove unnecessary refreshHrtimeBuffer() (Joyee Cheung) [#51446](https://github.com/nodejs/node/pull/51446)
* \[[`562947e012`](https://github.com/nodejs/node/commit/562947e012)] - **lib**: fix use of `--frozen-intrinsics` with `--jitless` (Antoine du Hamel) [#51248](https://github.com/nodejs/node/pull/51248)
* \[[`7b83ef749e`](https://github.com/nodejs/node/commit/7b83ef749e)] - **lib**: move function declaration outside of loop (Sanjaiyan Parthipan) [#51242](https://github.com/nodejs/node/pull/51242)
* \[[`0a85b0fd9d`](https://github.com/nodejs/node/commit/0a85b0fd9d)] - **lib**: reduce overhead of `SafePromiseAllSettledReturnVoid` calls (Antoine du Hamel) [#51243](https://github.com/nodejs/node/pull/51243)
* \[[`f4d7f0498e`](https://github.com/nodejs/node/commit/f4d7f0498e)] - **lib**: expose default prepareStackTrace (Chengzhong Wu) [#50827](https://github.com/nodejs/node/pull/50827)
* \[[`5c7a9c8d4a`](https://github.com/nodejs/node/commit/5c7a9c8d4a)] - **lib**: don't parse windows drive letters as schemes (华) [#50580](https://github.com/nodejs/node/pull/50580)
* \[[`9da6384f5a`](https://github.com/nodejs/node/commit/9da6384f5a)] - **lib**: refactor to use validateFunction in diagnostics\_channel (Deokjin Kim) [#50955](https://github.com/nodejs/node/pull/50955)
* \[[`be3205ae24`](https://github.com/nodejs/node/commit/be3205ae24)] - **lib**: streamline process.binding() handling (Joyee Cheung) [#50773](https://github.com/nodejs/node/pull/50773)
* \[[`f4987eb91e`](https://github.com/nodejs/node/commit/f4987eb91e)] - **lib,permission**: handle buffer on fs.symlink (Rafael Gonzaga) [#51212](https://github.com/nodejs/node/pull/51212)
* \[[`861e040b40`](https://github.com/nodejs/node/commit/861e040b40)] - **lib,src**: extract sourceMappingURL from module (unbyte) [#51690](https://github.com/nodejs/node/pull/51690)
* \[[`8a082754e0`](https://github.com/nodejs/node/commit/8a082754e0)] - **lib,src**: replace toUSVString with `toWellFormed()` (Yagiz Nizipli) [#47342](https://github.com/nodejs/node/pull/47342)
* \[[`3badc1139c`](https://github.com/nodejs/node/commit/3badc1139c)] - **(SEMVER-MINOR)** **lib,src,permission**: port path.resolve to C++ (Rafael Gonzaga) [#50758](https://github.com/nodejs/node/pull/50758)
* \[[`4b3cc3ce18`](https://github.com/nodejs/node/commit/4b3cc3ce18)] - **loader**: speed up line length calc used by moduleProvider (Mudit) [#50969](https://github.com/nodejs/node/pull/50969)
* \[[`960d67c51f`](https://github.com/nodejs/node/commit/960d67c51f)] - **meta**: bump github/codeql-action from 3.23.2 to 3.24.6 (dependabot\[bot]) [#51942](https://github.com/nodejs/node/pull/51942)
* \[[`1783b93af2`](https://github.com/nodejs/node/commit/1783b93af2)] - **meta**: bump actions/upload-artifact from 4.3.0 to 4.3.1 (dependabot\[bot]) [#51941](https://github.com/nodejs/node/pull/51941)
* \[[`1db603db2f`](https://github.com/nodejs/node/commit/1db603db2f)] - **meta**: bump codecov/codecov-action from 4.0.1 to 4.1.0 (dependabot\[bot]) [#51940](https://github.com/nodejs/node/pull/51940)
* \[[`2ddec64d5a`](https://github.com/nodejs/node/commit/2ddec64d5a)] - **meta**: bump actions/cache from 4.0.0 to 4.0.1 (dependabot\[bot]) [#51939](https://github.com/nodejs/node/pull/51939)
* \[[`92490421be`](https://github.com/nodejs/node/commit/92490421be)] - **meta**: bump actions/download-artifact from 4.1.1 to 4.1.3 (dependabot\[bot]) [#51938](https://github.com/nodejs/node/pull/51938)
* \[[`f3fa2b72b8`](https://github.com/nodejs/node/commit/f3fa2b72b8)] - **meta**: bump actions/setup-node from 4.0.1 to 4.0.2 (dependabot\[bot]) [#51937](https://github.com/nodejs/node/pull/51937)
* \[[`a62b042e83`](https://github.com/nodejs/node/commit/a62b042e83)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#51726](https://github.com/nodejs/node/pull/51726)
* \[[`491f9f9902`](https://github.com/nodejs/node/commit/491f9f9902)] - **meta**: bump codecov/codecov-action from 3.1.4 to 4.0.1 (dependabot\[bot]) [#51648](https://github.com/nodejs/node/pull/51648)
* \[[`2765077a47`](https://github.com/nodejs/node/commit/2765077a47)] - **meta**: bump actions/download-artifact from 4.1.0 to 4.1.1 (dependabot\[bot]) [#51644](https://github.com/nodejs/node/pull/51644)
* \[[`152a07b854`](https://github.com/nodejs/node/commit/152a07b854)] - **meta**: bump actions/upload-artifact from 4.0.0 to 4.3.0 (dependabot\[bot]) [#51643](https://github.com/nodejs/node/pull/51643)
* \[[`53826920fb`](https://github.com/nodejs/node/commit/53826920fb)] - **meta**: bump step-security/harden-runner from 2.6.1 to 2.7.0 (dependabot\[bot]) [#51641](https://github.com/nodejs/node/pull/51641)
* \[[`3d1dc9b030`](https://github.com/nodejs/node/commit/3d1dc9b030)] - **meta**: bump actions/cache from 3.3.2 to 4.0.0 (dependabot\[bot]) [#51640](https://github.com/nodejs/node/pull/51640)
* \[[`287bdf6bda`](https://github.com/nodejs/node/commit/287bdf6bda)] - **meta**: bump github/codeql-action from 3.22.12 to 3.23.2 (dependabot\[bot]) [#51639](https://github.com/nodejs/node/pull/51639)
* \[[`90068fb0f1`](https://github.com/nodejs/node/commit/90068fb0f1)] - **meta**: add .mailmap entry for lemire (Daniel Lemire) [#51600](https://github.com/nodejs/node/pull/51600)
* \[[`f91786bd70`](https://github.com/nodejs/node/commit/f91786bd70)] - **meta**: mark security-wg codeowner for deps folder (Marco Ippolito) [#51529](https://github.com/nodejs/node/pull/51529)
* \[[`e51221be8d`](https://github.com/nodejs/node/commit/e51221be8d)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#51468](https://github.com/nodejs/node/pull/51468)
* \[[`4a8a012c6d`](https://github.com/nodejs/node/commit/4a8a012c6d)] - **meta**: move RaisinTen to emeritus and remove from strategic initiatives (Darshan Sen) [#51411](https://github.com/nodejs/node/pull/51411)
* \[[`e9276bab3f`](https://github.com/nodejs/node/commit/e9276bab3f)] - **meta**: add .temp and .lock tags to ignore (Rafael Gonzaga) [#51343](https://github.com/nodejs/node/pull/51343)
* \[[`ae6fecbc8d`](https://github.com/nodejs/node/commit/ae6fecbc8d)] - **meta**: bump actions/setup-python from 4.7.1 to 5.0.0 (dependabot\[bot]) [#51335](https://github.com/nodejs/node/pull/51335)
* \[[`f4be49a618`](https://github.com/nodejs/node/commit/f4be49a618)] - **meta**: bump actions/setup-node from 4.0.0 to 4.0.1 (dependabot\[bot]) [#51334](https://github.com/nodejs/node/pull/51334)
* \[[`e24aa7ced1`](https://github.com/nodejs/node/commit/e24aa7ced1)] - **meta**: bump github/codeql-action from 2.22.8 to 3.22.12 (dependabot\[bot]) [#51333](https://github.com/nodejs/node/pull/51333)
* \[[`287c2bcf56`](https://github.com/nodejs/node/commit/287c2bcf56)] - **meta**: bump actions/stale from 8.0.0 to 9.0.0 (dependabot\[bot]) [#51332](https://github.com/nodejs/node/pull/51332)
* \[[`1cad0dfaff`](https://github.com/nodejs/node/commit/1cad0dfaff)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#51329](https://github.com/nodejs/node/pull/51329)
* \[[`eef64b782e`](https://github.com/nodejs/node/commit/eef64b782e)] - **meta**: notify tsc on changes in SECURITY.md (Rafael Gonzaga) [#51259](https://github.com/nodejs/node/pull/51259)
* \[[`95a880f728`](https://github.com/nodejs/node/commit/95a880f728)] - **meta**: update artifact actions to v4 (Michaël Zasso) [#51219](https://github.com/nodejs/node/pull/51219)
* \[[`59805f6879`](https://github.com/nodejs/node/commit/59805f6879)] - **meta**: bump step-security/harden-runner from 2.6.0 to 2.6.1 (dependabot\[bot]) [#50999](https://github.com/nodejs/node/pull/50999)
* \[[`d74e0b97c3`](https://github.com/nodejs/node/commit/d74e0b97c3)] - **meta**: bump github/codeql-action from 2.22.5 to 2.22.8 (dependabot\[bot]) [#50998](https://github.com/nodejs/node/pull/50998)
* \[[`91cd9183d1`](https://github.com/nodejs/node/commit/91cd9183d1)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#50931](https://github.com/nodejs/node/pull/50931)
* \[[`c621491aba`](https://github.com/nodejs/node/commit/c621491aba)] - **module**: fix crash when built-in module export a `default` key (Antoine du Hamel) [#51481](https://github.com/nodejs/node/pull/51481)
* \[[`43a8d3e984`](https://github.com/nodejs/node/commit/43a8d3e984)] - **module**: fix `--preserve-symlinks-main` (per4uk) [#51312](https://github.com/nodejs/node/pull/51312)
* \[[`d8da197f86`](https://github.com/nodejs/node/commit/d8da197f86)] - **module**: move the CJS exports cache to internal/modules/cjs/loader (Joyee Cheung) [#51157](https://github.com/nodejs/node/pull/51157)
* \[[`5fc10ca4d6`](https://github.com/nodejs/node/commit/5fc10ca4d6)] - **module**: load source maps in `commonjs` translator (Hiroki Osame) [#51033](https://github.com/nodejs/node/pull/51033)
* \[[`43e9f0bc65`](https://github.com/nodejs/node/commit/43e9f0bc65)] - **module**: document `parentURL` in register options (Hiroki Osame) [#51039](https://github.com/nodejs/node/pull/51039)
* \[[`870ef5a73f`](https://github.com/nodejs/node/commit/870ef5a73f)] - **net**: fix connect crash when call destroy in lookup handler (theanarkh) [#51826](https://github.com/nodejs/node/pull/51826)
* \[[`caf71e05a6`](https://github.com/nodejs/node/commit/caf71e05a6)] - **net**: fix example IPv4 in dns docs (Aras Abbasi) [#51377](https://github.com/nodejs/node/pull/51377)
* \[[`58a636be0e`](https://github.com/nodejs/node/commit/58a636be0e)] - **(SEMVER-MINOR)** **net**: add connection attempt events (Paolo Insogna) [#51045](https://github.com/nodejs/node/pull/51045)
* \[[`06a29f830a`](https://github.com/nodejs/node/commit/06a29f830a)] - **node-api**: make napi\_get\_buffer\_info check if passed buffer is valid (Janrupf) [#51571](https://github.com/nodejs/node/pull/51571)
* \[[`0fb98438e4`](https://github.com/nodejs/node/commit/0fb98438e4)] - **node-api**: move NAPI\_EXPERIMENTAL definition to gyp file (Gabriel Schulhof) [#51254](https://github.com/nodejs/node/pull/51254)
* \[[`242139fb98`](https://github.com/nodejs/node/commit/242139fb98)] - **node-api**: optimize napi\_set\_property for perf (Mert Can Altın) [#50282](https://github.com/nodejs/node/pull/50282)
* \[[`dc3d70c040`](https://github.com/nodejs/node/commit/dc3d70c040)] - **node-api**: type tag external values without v8::Private (Chengzhong Wu) [#51149](https://github.com/nodejs/node/pull/51149)
* \[[`0ac070ccb7`](https://github.com/nodejs/node/commit/0ac070ccb7)] - **node-api**: segregate nogc APIs from rest via type system (Gabriel Schulhof) [#50060](https://github.com/nodejs/node/pull/50060)
* \[[`de65cada70`](https://github.com/nodejs/node/commit/de65cada70)] - **node-api**: introduce experimental feature flags (Gabriel Schulhof) [#50991](https://github.com/nodejs/node/pull/50991)
* \[[`e192ba18cd`](https://github.com/nodejs/node/commit/e192ba18cd)] - **perf\_hooks**: performance milestone time origin timestamp improvement (IlyasShabi) [#51713](https://github.com/nodejs/node/pull/51713)
* \[[`f94336f95a`](https://github.com/nodejs/node/commit/f94336f95a)] - **repl**: fix `NO_COLORS` env var is ignored (Moshe Atlow) [#51568](https://github.com/nodejs/node/pull/51568)
* \[[`e08649caa0`](https://github.com/nodejs/node/commit/e08649caa0)] - **repl**: fix prepareStackTrace frames array order (Chengzhong Wu) [#50827](https://github.com/nodejs/node/pull/50827)
* \[[`07614072f1`](https://github.com/nodejs/node/commit/07614072f1)] - **sea**: update stability index (Joyee Cheung) [#51774](https://github.com/nodejs/node/pull/51774)
* \[[`eea0d74454`](https://github.com/nodejs/node/commit/eea0d74454)] - **(SEMVER-MINOR)** **sea**: support sea.getRawAsset() (Joyee Cheung) [#50960](https://github.com/nodejs/node/pull/50960)
* \[[`db0efa3f40`](https://github.com/nodejs/node/commit/db0efa3f40)] - **(SEMVER-MINOR)** **sea**: support embedding assets (Joyee Cheung) [#50960](https://github.com/nodejs/node/pull/50960)
* \[[`9b164c6eec`](https://github.com/nodejs/node/commit/9b164c6eec)] - **src**: fix --disable-single-executable-application (Joyee Cheung) [#51808](https://github.com/nodejs/node/pull/51808)
* \[[`306c1d35e5`](https://github.com/nodejs/node/commit/306c1d35e5)] - **src**: simplify direct queries of env vars in C++ land (Joyee Cheung) [#51829](https://github.com/nodejs/node/pull/51829)
* \[[`696063a47c`](https://github.com/nodejs/node/commit/696063a47c)] - **src**: stop the profiler and the inspector before snapshot serialization (Joyee Cheung) [#51815](https://github.com/nodejs/node/pull/51815)
* \[[`be40c8286c`](https://github.com/nodejs/node/commit/be40c8286c)] - **src**: simplify embedder entry point execution (Joyee Cheung) [#51557](https://github.com/nodejs/node/pull/51557)
* \[[`90391ff256`](https://github.com/nodejs/node/commit/90391ff256)] - **src**: compile code eagerly in snapshot builder (Joyee Cheung) [#51672](https://github.com/nodejs/node/pull/51672)
* \[[`3875fa1dc5`](https://github.com/nodejs/node/commit/3875fa1dc5)] - **src**: check empty before accessing string (Cheng Zhao) [#51665](https://github.com/nodejs/node/pull/51665)
* \[[`a58c98ea85`](https://github.com/nodejs/node/commit/a58c98ea85)] - **(SEMVER-MINOR)** **src**: print string content better in BlobDeserializer (Joyee Cheung) [#50960](https://github.com/nodejs/node/pull/50960)
* \[[`62707a9d27`](https://github.com/nodejs/node/commit/62707a9d27)] - **src**: fix vm bug for configurable globalThis (F. Hinkelmann) [#51602](https://github.com/nodejs/node/pull/51602)
* \[[`c3c0a3ee5c`](https://github.com/nodejs/node/commit/c3c0a3ee5c)] - **(SEMVER-MINOR)** **src**: support multi-line values for .env file (IlyasShabi) [#51289](https://github.com/nodejs/node/pull/51289)
* \[[`dc8fe9ebf4`](https://github.com/nodejs/node/commit/dc8fe9ebf4)] - **(SEMVER-MINOR)** **src**: add `process.loadEnvFile` and `util.parseEnv` (Yagiz Nizipli) [#51476](https://github.com/nodejs/node/pull/51476)
* \[[`a5afad2a4d`](https://github.com/nodejs/node/commit/a5afad2a4d)] - **src**: terminate correctly double-quote in env variable (Marco Ippolito) [#51510](https://github.com/nodejs/node/pull/51510)
* \[[`2a921966c6`](https://github.com/nodejs/node/commit/2a921966c6)] - **(SEMVER-MINOR)** **src**: do not coerce dotenv paths (Tobias Nießen) [#51425](https://github.com/nodejs/node/pull/51425)
* \[[`50ec55c268`](https://github.com/nodejs/node/commit/50ec55c268)] - **src**: refactor `GetCreationContext` calls (Jungku Lee) [#51367](https://github.com/nodejs/node/pull/51367)
* \[[`2e65389922`](https://github.com/nodejs/node/commit/2e65389922)] - **src**: do not read string out of bounds (Cheng Zhao) [#51358](https://github.com/nodejs/node/pull/51358)
* \[[`a653531089`](https://github.com/nodejs/node/commit/a653531089)] - **src**: avoid shadowed string in fs\_permission (Shelley Vohr) [#51123](https://github.com/nodejs/node/pull/51123)
* \[[`c190a057ff`](https://github.com/nodejs/node/commit/c190a057ff)] - **src**: avoid draining platform tasks at FreeEnvironment (Chengzhong Wu) [#51290](https://github.com/nodejs/node/pull/51290)
* \[[`00227674f5`](https://github.com/nodejs/node/commit/00227674f5)] - **src**: add fast api for Histogram (James M Snell) [#51296](https://github.com/nodejs/node/pull/51296)
* \[[`4733c8e4df`](https://github.com/nodejs/node/commit/4733c8e4df)] - **src**: refactor `GetCreationContext` calls (Yagiz Nizipli) [#51287](https://github.com/nodejs/node/pull/51287)
* \[[`d76e16bb47`](https://github.com/nodejs/node/commit/d76e16bb47)] - **src**: enter isolate before destructing IsolateData (Ben Noordhuis) [#51138](https://github.com/nodejs/node/pull/51138)
* \[[`4ffdd37d2c`](https://github.com/nodejs/node/commit/4ffdd37d2c)] - **src**: eliminate duplicate code in histogram.cc (James M Snell) [#51263](https://github.com/nodejs/node/pull/51263)
* \[[`2ce8b974a0`](https://github.com/nodejs/node/commit/2ce8b974a0)] - **src**: fix unix abstract socket path for trace event (theanarkh) [#50858](https://github.com/nodejs/node/pull/50858)
* \[[`9b25268cb8`](https://github.com/nodejs/node/commit/9b25268cb8)] - **src**: use BignumPointer and use BN\_clear\_free (James M Snell) [#50454](https://github.com/nodejs/node/pull/50454)
* \[[`a80f660343`](https://github.com/nodejs/node/commit/a80f660343)] - **src**: implement FastByteLengthUtf8 with simdutf::utf8\_length\_from\_latin1 (Daniel Lemire) [#50840](https://github.com/nodejs/node/pull/50840)
* \[[`0dee86f295`](https://github.com/nodejs/node/commit/0dee86f295)] - **(SEMVER-MINOR)** **src**: support configurable snapshot (Joyee Cheung) [#50453](https://github.com/nodejs/node/pull/50453)
* \[[`90b5ed1d1d`](https://github.com/nodejs/node/commit/90b5ed1d1d)] - **src**: implement countObjectsWithPrototype (Joyee Cheung) [#50572](https://github.com/nodejs/node/pull/50572)
* \[[`9365e129ed`](https://github.com/nodejs/node/commit/9365e129ed)] - **src**: register udp\_wrap external references (Joyee Cheung) [#50943](https://github.com/nodejs/node/pull/50943)
* \[[`b05d496b6c`](https://github.com/nodejs/node/commit/b05d496b6c)] - **src**: register spawn\_sync external references (Joyee Cheung) [#50943](https://github.com/nodejs/node/pull/50943)
* \[[`642fb44982`](https://github.com/nodejs/node/commit/642fb44982)] - **src**: register process\_wrap external references (Joyee Cheung) [#50943](https://github.com/nodejs/node/pull/50943)
* \[[`c7c9e81a1a`](https://github.com/nodejs/node/commit/c7c9e81a1a)] - **src**: fix double free reported by coverity (Michael Dawson) [#51046](https://github.com/nodejs/node/pull/51046)
* \[[`358793e28e`](https://github.com/nodejs/node/commit/358793e28e)] - **src**: remove unused headers in `node_file.cc` (Jungku Lee) [#50927](https://github.com/nodejs/node/pull/50927)
* \[[`c705b73a74`](https://github.com/nodejs/node/commit/c705b73a74)] - **src**: implement --trace-promises (Joyee Cheung) [#50899](https://github.com/nodejs/node/pull/50899)
* \[[`97aa67f006`](https://github.com/nodejs/node/commit/97aa67f006)] - **src**: fix dynamically linked zlib version (Richard Lau) [#51007](https://github.com/nodejs/node/pull/51007)
* \[[`d6f46a44f2`](https://github.com/nodejs/node/commit/d6f46a44f2)] - **src**: make ModifyCodeGenerationFromStrings more robust (Joyee Cheung) [#50763](https://github.com/nodejs/node/pull/50763)
* \[[`362135a1f9`](https://github.com/nodejs/node/commit/362135a1f9)] - **src**: disable uncaught exception abortion for ESM syntax detection (Yagiz Nizipli) [#50987](https://github.com/nodejs/node/pull/50987)
* \[[`d82b0d4320`](https://github.com/nodejs/node/commit/d82b0d4320)] - **src**: fix backtrace with tail \[\[noreturn]] abort (Chengzhong Wu) [#50849](https://github.com/nodejs/node/pull/50849)
* \[[`6df3e31bff`](https://github.com/nodejs/node/commit/6df3e31bff)] - **src**: print MKSNAPSHOT debug logs to stderr (Joyee Cheung) [#50759](https://github.com/nodejs/node/pull/50759)
* \[[`fd5efac176`](https://github.com/nodejs/node/commit/fd5efac176)] - **(SEMVER-MINOR)** **src,permission**: add --allow-addon flag (Rafael Gonzaga) [#51183](https://github.com/nodejs/node/pull/51183)
* \[[`b616f6fa06`](https://github.com/nodejs/node/commit/b616f6fa06)] - **src,stream**: improve WriteString (ywave620) [#51155](https://github.com/nodejs/node/pull/51155)
* \[[`16d8cd5b22`](https://github.com/nodejs/node/commit/16d8cd5b22)] - **stream**: do not defer construction by one microtick (Matteo Collina) [#52005](https://github.com/nodejs/node/pull/52005)
* \[[`7931c3bbc8`](https://github.com/nodejs/node/commit/7931c3bbc8)] - **stream**: fix eventNames() to not return not defined events (IlyasShabi) [#51331](https://github.com/nodejs/node/pull/51331)
* \[[`d0a6f3515d`](https://github.com/nodejs/node/commit/d0a6f3515d)] - **stream**: fix cloned webstreams not being unref correctly (tsctx) [#51526](https://github.com/nodejs/node/pull/51526)
* \[[`8750070a47`](https://github.com/nodejs/node/commit/8750070a47)] - **stream**: fix fd is null when calling clearBuffer (kylo5aby) [#50994](https://github.com/nodejs/node/pull/50994)
* \[[`ade6614067`](https://github.com/nodejs/node/commit/ade6614067)] - **(SEMVER-MINOR)** **stream**: add support for `deflate-raw` format to webstreams compression (Damian Krzeminski) [#50097](https://github.com/nodejs/node/pull/50097)
* \[[`905c48fc6e`](https://github.com/nodejs/node/commit/905c48fc6e)] - **test**: add regression test for test\_runner after hook (Colin Ihrig) [#51998](https://github.com/nodejs/node/pull/51998)
* \[[`60f008b65e`](https://github.com/nodejs/node/commit/60f008b65e)] - **test**: reduce flakiness of `test-runner-output` (Antoine du Hamel) [#51952](https://github.com/nodejs/node/pull/51952)
* \[[`0ad88f6a5c`](https://github.com/nodejs/node/commit/0ad88f6a5c)] - **test**: fix flaky http-chunk-extensions-limit test (Ethan Arrowood) [#51943](https://github.com/nodejs/node/pull/51943)
* \[[`3f85c7ac97`](https://github.com/nodejs/node/commit/3f85c7ac97)] - **test**: remove flaky designation (Luigi Pinca) [#51736](https://github.com/nodejs/node/pull/51736)
* \[[`f37648ee5c`](https://github.com/nodejs/node/commit/f37648ee5c)] - **test**: skip SEA tests when SEA generation fails (Joyee Cheung) [#51887](https://github.com/nodejs/node/pull/51887)
* \[[`136b6a998b`](https://github.com/nodejs/node/commit/136b6a998b)] - **test**: fix unreliable assumption in js-native-api/test\_cannot\_run\_js (Joyee Cheung) [#51898](https://github.com/nodejs/node/pull/51898)
* \[[`d90594aefa`](https://github.com/nodejs/node/commit/d90594aefa)] - **test**: deflake test-http2-large-write-multiple-requests (Joyee Cheung) [#51863](https://github.com/nodejs/node/pull/51863)
* \[[`a0b36e33d1`](https://github.com/nodejs/node/commit/a0b36e33d1)] - **test**: fix test-debugger-profile for coverage generation (Joyee Cheung) [#51816](https://github.com/nodejs/node/pull/51816)
* \[[`dd0f164ca3`](https://github.com/nodejs/node/commit/dd0f164ca3)] - **test**: fix test-bootstrap-modules for coverage generation (Joyee Cheung) [#51816](https://github.com/nodejs/node/pull/51816)
* \[[`e4c7d62496`](https://github.com/nodejs/node/commit/e4c7d62496)] - **test**: ensure delay in recursive fs watch tests (Joyee Cheung) [#51842](https://github.com/nodejs/node/pull/51842)
* \[[`963d7d7dea`](https://github.com/nodejs/node/commit/963d7d7dea)] - **test**: fix test-child-process-fork-net (Joyee Cheung) [#51841](https://github.com/nodejs/node/pull/51841)
* \[[`dd708d337e`](https://github.com/nodejs/node/commit/dd708d337e)] - **test**: split wasi tests (Joyee Cheung) [#51836](https://github.com/nodejs/node/pull/51836)
* \[[`853b48d905`](https://github.com/nodejs/node/commit/853b48d905)] - **test**: remove test-fs-stat-bigint flaky designation (Luigi Pinca) [#51735](https://github.com/nodejs/node/pull/51735)
* \[[`fdc7d751de`](https://github.com/nodejs/node/commit/fdc7d751de)] - **test**: skip test-http-correct-hostname on loong64 (Shi Pujin) [#51663](https://github.com/nodejs/node/pull/51663)
* \[[`c33f860d2b`](https://github.com/nodejs/node/commit/c33f860d2b)] - **test**: remove test-cli-node-options flaky designation (Luigi Pinca) [#51716](https://github.com/nodejs/node/pull/51716)
* \[[`f528e965f6`](https://github.com/nodejs/node/commit/f528e965f6)] - **test**: remove test-domain-error-types flaky designation (Luigi Pinca) [#51717](https://github.com/nodejs/node/pull/51717)
* \[[`7e3ee828f1`](https://github.com/nodejs/node/commit/7e3ee828f1)] - **test**: fix `internet/test-inspector-help-page` (Richard Lau) [#51693](https://github.com/nodejs/node/pull/51693)
* \[[`170278c25d`](https://github.com/nodejs/node/commit/170278c25d)] - **test**: remove duplicate entry for flaky test (Luigi Pinca) [#51654](https://github.com/nodejs/node/pull/51654)
* \[[`d0d5bd0e54`](https://github.com/nodejs/node/commit/d0d5bd0e54)] - **test**: remove test-crypto-keygen flaky designation (Luigi Pinca) [#51567](https://github.com/nodejs/node/pull/51567)
* \[[`bca6dcca0b`](https://github.com/nodejs/node/commit/bca6dcca0b)] - **test**: remove test-fs-rmdir-recursive flaky designation (Luigi Pinca) [#51566](https://github.com/nodejs/node/pull/51566)
* \[[`af3f229d6b`](https://github.com/nodejs/node/commit/af3f229d6b)] - **test**: remove common.expectsError calls for asserts (Paulo Chaves) [#51504](https://github.com/nodejs/node/pull/51504)
* \[[`f6fcd200e6`](https://github.com/nodejs/node/commit/f6fcd200e6)] - **test**: mark test-http2-large-file as flaky (Michaël Zasso) [#51549](https://github.com/nodejs/node/pull/51549)
* \[[`1d8e65a230`](https://github.com/nodejs/node/commit/1d8e65a230)] - **test**: use checkIfCollectableByCounting in SourceTextModule leak test (Joyee Cheung) [#51512](https://github.com/nodejs/node/pull/51512)
* \[[`713afed6b0`](https://github.com/nodejs/node/commit/713afed6b0)] - **test**: remove test-file-write-stream4 flaky designation (Luigi Pinca) [#51472](https://github.com/nodejs/node/pull/51472)
* \[[`292d0174df`](https://github.com/nodejs/node/commit/292d0174df)] - **test**: add URL tests to fs-write (Rafael Gonzaga) [#51352](https://github.com/nodejs/node/pull/51352)
* \[[`954e2f2f58`](https://github.com/nodejs/node/commit/954e2f2f58)] - **test**: remove unneeded common.expectsError for asserts (Andrés Morelos) [#51353](https://github.com/nodejs/node/pull/51353)
* \[[`f2dfe0fa80`](https://github.com/nodejs/node/commit/f2dfe0fa80)] - **test**: add regression test for 51586 (Matteo Collina) [#51491](https://github.com/nodejs/node/pull/51491)
* \[[`6ee5f50789`](https://github.com/nodejs/node/commit/6ee5f50789)] - **test**: fix flaky conditions for ppc64 SEA tests (Richard Lau) [#51422](https://github.com/nodejs/node/pull/51422)
* \[[`06a6eef9a4`](https://github.com/nodejs/node/commit/06a6eef9a4)] - **test**: replace forEach() with for...of (Alexander Jones) [#50608](https://github.com/nodejs/node/pull/50608)
* \[[`a98102a6de`](https://github.com/nodejs/node/commit/a98102a6de)] - **test**: replace forEach with for...of (Ospite Privilegiato) [#50787](https://github.com/nodejs/node/pull/50787)
* \[[`e9080a94d3`](https://github.com/nodejs/node/commit/e9080a94d3)] - **test**: replace foreach with for of (lucacapocci94-dev) [#50790](https://github.com/nodejs/node/pull/50790)
* \[[`42b162b06d`](https://github.com/nodejs/node/commit/42b162b06d)] - **test**: replace forEach() with for...of (Jia) [#50610](https://github.com/nodejs/node/pull/50610)
* \[[`cab7737f7e`](https://github.com/nodejs/node/commit/cab7737f7e)] - **test**: fix inconsistency write size in `test-fs-readfile-tostring-fail` (Jungku Lee) [#51141](https://github.com/nodejs/node/pull/51141)
* \[[`15731b4b2f`](https://github.com/nodejs/node/commit/15731b4b2f)] - **test**: replace forEach test-http-server-multiheaders2 (Marco Mac) [#50794](https://github.com/nodejs/node/pull/50794)
* \[[`9cedaa62fa`](https://github.com/nodejs/node/commit/9cedaa62fa)] - **test**: replace forEach with for-of in test-webcrypto-export-import-ec (Chiara Ricciardi) [#51249](https://github.com/nodejs/node/pull/51249)
* \[[`7f301e04be`](https://github.com/nodejs/node/commit/7f301e04be)] - **test**: move to for of loop in test-http-hostname-typechecking.js (Luca Del Puppo) [#50782](https://github.com/nodejs/node/pull/50782)
* \[[`6e62e649df`](https://github.com/nodejs/node/commit/6e62e649df)] - **test**: skip test-watch-mode-inspect on arm (Michael Dawson) [#51210](https://github.com/nodejs/node/pull/51210)
* \[[`c3c2b2b041`](https://github.com/nodejs/node/commit/c3c2b2b041)] - **test**: replace forEach with for of in file test-trace-events-net.js (Ianna83) [#50789](https://github.com/nodejs/node/pull/50789)
* \[[`55c423ba4f`](https://github.com/nodejs/node/commit/55c423ba4f)] - **test**: replace forEach() with for...of in test/parallel/test-util-log.js (Edoardo Dusi) [#50783](https://github.com/nodejs/node/pull/50783)
* \[[`8ac05cf3c4`](https://github.com/nodejs/node/commit/8ac05cf3c4)] - **test**: replace forEach with for of in test-trace-events-api.js (Andrea Pavone) [#50784](https://github.com/nodejs/node/pull/50784)
* \[[`d10d39e8ba`](https://github.com/nodejs/node/commit/d10d39e8ba)] - **test**: replace forEach with for-of in test-v8-serders.js (Mattia Iannone) [#50791](https://github.com/nodejs/node/pull/50791)
* \[[`576adc5e5b`](https://github.com/nodejs/node/commit/576adc5e5b)] - **test**: add URL tests to fs-read in pm (Rafael Gonzaga) [#51213](https://github.com/nodejs/node/pull/51213)
* \[[`996cef51b7`](https://github.com/nodejs/node/commit/996cef51b7)] - **test**: use tmpdir.refresh() in test-esm-loader-resolve-type.mjs (Luigi Pinca) [#51206](https://github.com/nodejs/node/pull/51206)
* \[[`8f2d982342`](https://github.com/nodejs/node/commit/8f2d982342)] - **test**: use tmpdir.refresh() in test-esm-json.mjs (Luigi Pinca) [#51205](https://github.com/nodejs/node/pull/51205)
* \[[`efd6630143`](https://github.com/nodejs/node/commit/efd6630143)] - **test**: fix flakiness in worker\*.test-free-called (Jithil P Ponnan) [#51013](https://github.com/nodejs/node/pull/51013)
* \[[`54a29ee506`](https://github.com/nodejs/node/commit/54a29ee506)] - **test**: deflake test-diagnostics-channel-memory-leak (Joyee Cheung) [#50572](https://github.com/nodejs/node/pull/50572)
* \[[`6319ea6183`](https://github.com/nodejs/node/commit/6319ea6183)] - **test**: test syncrhnous methods of child\_process in snapshot (Joyee Cheung) [#50943](https://github.com/nodejs/node/pull/50943)
* \[[`50df4aee2b`](https://github.com/nodejs/node/commit/50df4aee2b)] - **test**: handle relative https redirect (Richard Lau) [#51121](https://github.com/nodejs/node/pull/51121)
* \[[`9f88f40cae`](https://github.com/nodejs/node/commit/9f88f40cae)] - **test**: fix test runner colored output test (Moshe Atlow) [#51064](https://github.com/nodejs/node/pull/51064)
* \[[`a1feae24cb`](https://github.com/nodejs/node/commit/a1feae24cb)] - **test**: resolve path of embedtest binary correctly (Cheng Zhao) [#50276](https://github.com/nodejs/node/pull/50276)
* \[[`a4f1805c92`](https://github.com/nodejs/node/commit/a4f1805c92)] - **test**: escape cwd in regexp (Jérémy Lal) [#50980](https://github.com/nodejs/node/pull/50980)
* \[[`1c28db8116`](https://github.com/nodejs/node/commit/1c28db8116)] - **test**: replace forEach to for.. test-webcrypto-export-import-cfrg.js (Angelo Parziale) [#50785](https://github.com/nodejs/node/pull/50785)
* \[[`a4f505213e`](https://github.com/nodejs/node/commit/a4f505213e)] - **test**: log more information in SEA tests (Joyee Cheung) [#50759](https://github.com/nodejs/node/pull/50759)
* \[[`c91b817a5c`](https://github.com/nodejs/node/commit/c91b817a5c)] - **test**: consolidate utf8 text fixtures in tests (Joyee Cheung) [#50732](https://github.com/nodejs/node/pull/50732)
* \[[`26a06b093b`](https://github.com/nodejs/node/commit/26a06b093b)] - **test**: give more time to GC in test-shadow-realm-gc-\* (Joyee Cheung) [#50735](https://github.com/nodejs/node/pull/50735)
* \[[`e8f5735149`](https://github.com/nodejs/node/commit/e8f5735149)] - **test**: test surrogate pair filenames on windows (Mert Can Altın) [#51800](https://github.com/nodejs/node/pull/51800)
* \[[`1ab9ff46a5`](https://github.com/nodejs/node/commit/1ab9ff46a5)] - **test**: mark test-wasi as flaky on Windows on ARM (Joyee Cheung) [#51834](https://github.com/nodejs/node/pull/51834)
* \[[`1c47da1453`](https://github.com/nodejs/node/commit/1c47da1453)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#51533](https://github.com/nodejs/node/pull/51533)
* \[[`91c8624608`](https://github.com/nodejs/node/commit/91c8624608)] - **test\_runner**: serialize 'expected' and 'actual' in isolation (Malthe Borch) [#51851](https://github.com/nodejs/node/pull/51851)
* \[[`cea90dcfe3`](https://github.com/nodejs/node/commit/cea90dcfe3)] - **test\_runner**: add ref methods to mocked timers (Marco Ippolito) [#51809](https://github.com/nodejs/node/pull/51809)
* \[[`9ff0df1793`](https://github.com/nodejs/node/commit/9ff0df1793)] - **test\_runner**: check if timeout was cleared by own callback (Ben Richeson) [#51673](https://github.com/nodejs/node/pull/51673)
* \[[`34ecd1e36b`](https://github.com/nodejs/node/commit/34ecd1e36b)] - **test\_runner**: fixed test object is incorrectly passed to setup() (Pulkit Gupta) [#50982](https://github.com/nodejs/node/pull/50982)
* \[[`da17a2538e`](https://github.com/nodejs/node/commit/da17a2538e)] - **test\_runner**: fixed to run after hook if before throws an error (Pulkit Gupta) [#51062](https://github.com/nodejs/node/pull/51062)
* \[[`b8f0ea6f60`](https://github.com/nodejs/node/commit/b8f0ea6f60)] - **test\_runner**: fix infinite loop when files are undefined in test runner (Pulkit Gupta) [#51047](https://github.com/nodejs/node/pull/51047)
* \[[`fe922f05e4`](https://github.com/nodejs/node/commit/fe922f05e4)] - **(SEMVER-MINOR)** **timers**: export timers.promises (Marco Ippolito) [#51246](https://github.com/nodejs/node/pull/51246)
* \[[`f4ac7baf85`](https://github.com/nodejs/node/commit/f4ac7baf85)] - **tools**: fix installing node with shared mode (Cheng Zhao) [#51910](https://github.com/nodejs/node/pull/51910)
* \[[`f07605fa7b`](https://github.com/nodejs/node/commit/f07605fa7b)] - **tools**: update eslint to 8.57.0 (Node.js GitHub Bot) [#51867](https://github.com/nodejs/node/pull/51867)
* \[[`d16b235fca`](https://github.com/nodejs/node/commit/d16b235fca)] - **tools**: update lint-md-dependencies to rollup\@4.12.0 (Node.js GitHub Bot) [#51795](https://github.com/nodejs/node/pull/51795)
* \[[`d27e811a01`](https://github.com/nodejs/node/commit/d27e811a01)] - **tools**: fix missing \[\[fallthrough]] in js2c (Cheng Zhao) [#51845](https://github.com/nodejs/node/pull/51845)
* \[[`7eb69308da`](https://github.com/nodejs/node/commit/7eb69308da)] - **tools**: disable automated libuv updates (Rafael Gonzaga) [#51775](https://github.com/nodejs/node/pull/51775)
* \[[`1f15af425c`](https://github.com/nodejs/node/commit/1f15af425c)] - **tools**: update lint-md-dependencies to rollup\@4.10.0 (Node.js GitHub Bot) [#51720](https://github.com/nodejs/node/pull/51720)
* \[[`c7ae13e6bc`](https://github.com/nodejs/node/commit/c7ae13e6bc)] - **tools**: update github\_reporter to 1.6.0 (Node.js GitHub Bot) [#51658](https://github.com/nodejs/node/pull/51658)
* \[[`0fb079bd85`](https://github.com/nodejs/node/commit/0fb079bd85)] - **tools**: run `build-windows` workflow only on source changes (Antoine du Hamel) [#51596](https://github.com/nodejs/node/pull/51596)
* \[[`c2538e31fa`](https://github.com/nodejs/node/commit/c2538e31fa)] - **tools**: update lint-md-dependencies to rollup\@4.9.6 (Node.js GitHub Bot) [#51583](https://github.com/nodejs/node/pull/51583)
* \[[`e02dbf074b`](https://github.com/nodejs/node/commit/e02dbf074b)] - **tools**: fix loong64 build (Shi Pujin) [#51401](https://github.com/nodejs/node/pull/51401)
* \[[`ce49cb6656`](https://github.com/nodejs/node/commit/ce49cb6656)] - **tools**: set normalizeTD text default to empty string (Marco Ippolito) [#51543](https://github.com/nodejs/node/pull/51543)
* \[[`e8dc5ac552`](https://github.com/nodejs/node/commit/e8dc5ac552)] - **tools**: limit parallelism with ninja in V8 builds (Richard Lau) [#51473](https://github.com/nodejs/node/pull/51473)
* \[[`97470b179b`](https://github.com/nodejs/node/commit/97470b179b)] - **tools**: do not pass invalid flag to C compiler (Michaël Zasso) [#51409](https://github.com/nodejs/node/pull/51409)
* \[[`59af1d7923`](https://github.com/nodejs/node/commit/59af1d7923)] - **tools**: update lint-md-dependencies to rollup\@4.9.5 (Node.js GitHub Bot) [#51460](https://github.com/nodejs/node/pull/51460)
* \[[`6385c7ad57`](https://github.com/nodejs/node/commit/6385c7ad57)] - **tools**: update inspector\_protocol to 83b1154 (Kohei Ueno) [#51309](https://github.com/nodejs/node/pull/51309)
* \[[`5235aaf299`](https://github.com/nodejs/node/commit/5235aaf299)] - **tools**: update github\_reporter to 1.5.4 (Node.js GitHub Bot) [#51395](https://github.com/nodejs/node/pull/51395)
* \[[`4ce2ecb1ce`](https://github.com/nodejs/node/commit/4ce2ecb1ce)] - **tools**: fix version parsing in brotli update script (Richard Lau) [#51373](https://github.com/nodejs/node/pull/51373)
* \[[`86102078f5`](https://github.com/nodejs/node/commit/86102078f5)] - **tools**: update lint-md-dependencies to rollup\@4.9.4 (Node.js GitHub Bot) [#51396](https://github.com/nodejs/node/pull/51396)
* \[[`e658208159`](https://github.com/nodejs/node/commit/e658208159)] - **tools**: remove openssl v1 update script (Marco Ippolito) [#51378](https://github.com/nodejs/node/pull/51378)
* \[[`4372f6a5b8`](https://github.com/nodejs/node/commit/4372f6a5b8)] - **tools**: remove deprecated python api (Alex Yang) [#49731](https://github.com/nodejs/node/pull/49731)
* \[[`2b24059e53`](https://github.com/nodejs/node/commit/2b24059e53)] - **tools**: update lint-md-dependencies to rollup\@4.9.2 (Node.js GitHub Bot) [#51320](https://github.com/nodejs/node/pull/51320)
* \[[`1da2e8d15e`](https://github.com/nodejs/node/commit/1da2e8d15e)] - **tools**: fix dep\_updaters dir updates (Michaël Zasso) [#51294](https://github.com/nodejs/node/pull/51294)
* \[[`b264dda7f2`](https://github.com/nodejs/node/commit/b264dda7f2)] - **tools**: update inspector\_protocol to c488ba2 (cola119) [#51293](https://github.com/nodejs/node/pull/51293)
* \[[`fdb07d5418`](https://github.com/nodejs/node/commit/fdb07d5418)] - **tools**: update inspector\_protocol to 9b4a4aa (cola119) [#51293](https://github.com/nodejs/node/pull/51293)
* \[[`6863fb84a6`](https://github.com/nodejs/node/commit/6863fb84a6)] - **tools**: update inspector\_protocol to 2f51e05 (cola119) [#51293](https://github.com/nodejs/node/pull/51293)
* \[[`6b85f5c6e0`](https://github.com/nodejs/node/commit/6b85f5c6e0)] - **tools**: update inspector\_protocol to d7b099b (cola119) [#51293](https://github.com/nodejs/node/pull/51293)
* \[[`cf029ca24f`](https://github.com/nodejs/node/commit/cf029ca24f)] - **tools**: update inspector\_protocol to 912eb68 (cola119) [#51293](https://github.com/nodejs/node/pull/51293)
* \[[`af119447f5`](https://github.com/nodejs/node/commit/af119447f5)] - **tools**: update inspector\_protocol to 547c5b8 (cola119) [#51293](https://github.com/nodejs/node/pull/51293)
* \[[`5a72506823`](https://github.com/nodejs/node/commit/5a72506823)] - **tools**: update inspector\_protocol to ca525fc (cola119) [#51293](https://github.com/nodejs/node/pull/51293)
* \[[`c7aa3976f9`](https://github.com/nodejs/node/commit/c7aa3976f9)] - **tools**: update lint-md-dependencies to rollup\@4.9.1 (Node.js GitHub Bot) [#51276](https://github.com/nodejs/node/pull/51276)
* \[[`8e02d08a82`](https://github.com/nodejs/node/commit/8e02d08a82)] - **tools**: check timezone current version (Marco Ippolito) [#51178](https://github.com/nodejs/node/pull/51178)
* \[[`fa1e88775d`](https://github.com/nodejs/node/commit/fa1e88775d)] - **tools**: update lint-md-dependencies to rollup\@4.9.0 (Node.js GitHub Bot) [#51193](https://github.com/nodejs/node/pull/51193)
* \[[`04c0bf9cc5`](https://github.com/nodejs/node/commit/04c0bf9cc5)] - **tools**: update eslint to 8.56.0 (Node.js GitHub Bot) [#51194](https://github.com/nodejs/node/pull/51194)
* \[[`e896cbd0d5`](https://github.com/nodejs/node/commit/e896cbd0d5)] - **tools**: update lint-md-dependencies to rollup\@4.7.0 (Node.js GitHub Bot) [#51106](https://github.com/nodejs/node/pull/51106)
* \[[`c7350c2083`](https://github.com/nodejs/node/commit/c7350c2083)] - **tools**: update doc to highlight.js\@11.9.0 unified\@11.0.4 (Node.js GitHub Bot) [#50459](https://github.com/nodejs/node/pull/50459)
* \[[`00dfabf8fb`](https://github.com/nodejs/node/commit/00dfabf8fb)] - **tools**: update eslint to 8.55.0 (Node.js GitHub Bot) [#51025](https://github.com/nodejs/node/pull/51025)
* \[[`f91d56157b`](https://github.com/nodejs/node/commit/f91d56157b)] - **tools**: update lint-md-dependencies to rollup\@4.6.1 (Node.js GitHub Bot) [#51022](https://github.com/nodejs/node/pull/51022)
* \[[`450163cf9b`](https://github.com/nodejs/node/commit/450163cf9b)] - **tools**: add triggers to update release links workflow (Moshe Atlow) [#50974](https://github.com/nodejs/node/pull/50974)
* \[[`b1442024ea`](https://github.com/nodejs/node/commit/b1442024ea)] - **tools**: update lint-md-dependencies to rollup\@4.5.2 (Node.js GitHub Bot) [#50913](https://github.com/nodejs/node/pull/50913)
* \[[`6fc6a62daf`](https://github.com/nodejs/node/commit/6fc6a62daf)] - **tools**: fix current version check (Marco Ippolito) [#50951](https://github.com/nodejs/node/pull/50951)
* \[[`bc6bdda8b1`](https://github.com/nodejs/node/commit/bc6bdda8b1)] - **tools**: fix update-icu.sh (Michaël Zasso) [#51723](https://github.com/nodejs/node/pull/51723)
* \[[`a7a4cce75d`](https://github.com/nodejs/node/commit/a7a4cce75d)] - **typings**: lib/internal/vm.js (Geoffrey Booth) [#50112](https://github.com/nodejs/node/pull/50112)
* \[[`6375540507`](https://github.com/nodejs/node/commit/6375540507)] - **typings**: fix JSDoc in `internal/modules/esm/hooks` (Alex Yang) [#50887](https://github.com/nodejs/node/pull/50887)
* \[[`4bc8e98d7c`](https://github.com/nodejs/node/commit/4bc8e98d7c)] - **url**: don't update URL immediately on update to URLSearchParams (Matt Cowley) [#51520](https://github.com/nodejs/node/pull/51520)
* \[[`2acbcbd8ad`](https://github.com/nodejs/node/commit/2acbcbd8ad)] - **url**: throw error if argument length of revokeObjectURL is 0 (DylanTet) [#50433](https://github.com/nodejs/node/pull/50433)
* \[[`c50134615e`](https://github.com/nodejs/node/commit/c50134615e)] - **(SEMVER-MINOR)** **util**: add styleText API to text formatting (Rafael Gonzaga) [#51850](https://github.com/nodejs/node/pull/51850)
* \[[`f79ac336ad`](https://github.com/nodejs/node/commit/f79ac336ad)] - **util**: pass invalidSubtypeIndex instead of trimmedSubtype to error (Gaurish Sethia) [#51264](https://github.com/nodejs/node/pull/51264)
* \[[`c3b89c310f`](https://github.com/nodejs/node/commit/c3b89c310f)] - **util**: improve performance of function areSimilarFloatArrays (Liu Jia) [#51040](https://github.com/nodejs/node/pull/51040)
* \[[`5202995b48`](https://github.com/nodejs/node/commit/5202995b48)] - **vm**: implement isContext() directly in JS land with private symbol (Joyee Cheung) [#51685](https://github.com/nodejs/node/pull/51685)
* \[[`0211a3d65f`](https://github.com/nodejs/node/commit/0211a3d65f)] - **(SEMVER-MINOR)** **vm**: support using the default loader to handle dynamic import() (Joyee Cheung) [#51244](https://github.com/nodejs/node/pull/51244)
* \[[`07fc077c5d`](https://github.com/nodejs/node/commit/07fc077c5d)] - **vm**: use v8::DeserializeInternalFieldsCallback explicitly (Joyee Cheung) [#50984](https://github.com/nodejs/node/pull/50984)
* \[[`5183e3a4b1`](https://github.com/nodejs/node/commit/5183e3a4b1)] - **watch**: clarify that the fileName parameter can be null (Luigi Pinca) [#51305](https://github.com/nodejs/node/pull/51305)
* \[[`63bf8a66df`](https://github.com/nodejs/node/commit/63bf8a66df)] - **watch**: fix null `fileName` on windows systems (vnc5) [#49891](https://github.com/nodejs/node/pull/49891)
* \[[`07da4e9b58`](https://github.com/nodejs/node/commit/07da4e9b58)] - **watch**: fix infinite loop when passing --watch=true flag (Pulkit Gupta) [#51160](https://github.com/nodejs/node/pull/51160)

<a id="20.11.1"></a>

## 2024-02-14, Version 20.11.1 'Iron' (LTS), @RafaelGSS prepared by @marco-ippolito

### Notable changes

This is a security release.

### Notable changes

* CVE-2024-21892 - Code injection and privilege escalation through Linux capabilities- (High)
* CVE-2024-22019 - http: Reading unprocessed HTTP request with unbounded chunk extension allows DoS attacks- (High)
* CVE-2024-21896 - Path traversal by monkey-patching Buffer internals- (High)
* CVE-2024-22017 - setuid() does not drop all privileges due to io\_uring - (High)
* CVE-2023-46809 - Node.js is vulnerable to the Marvin Attack (timing variant of the Bleichenbacher attack against PKCS#1 v1.5 padding) - (Medium)
* CVE-2024-21891 - Multiple permission model bypasses due to improper path traversal sequence sanitization - (Medium)
* CVE-2024-21890 - Improper handling of wildcards in --allow-fs-read and --allow-fs-write (Medium)
* CVE-2024-22025 - Denial of Service by resource exhaustion in fetch() brotli decoding - (Medium)
* undici version 5.28.3
* libuv version 1.48.0
* OpenSSL version 3.0.13+quic1

### Commits

* \[[`7079c062bb`](https://github.com/nodejs/node/commit/7079c062bb)] - **crypto**: disable PKCS#1 padding for privateDecrypt (Michael Dawson) [nodejs-private/node-private#525](https://github.com/nodejs-private/node-private/pull/525)
* \[[`186a6e1ffb`](https://github.com/nodejs/node/commit/186a6e1ffb)] - **deps**: fix GHSA-f74f-cvh7-c6q6/CVE-2024-24806 (Santiago Gimeno) [#51737](https://github.com/nodejs/node/pull/51737)
* \[[`686da19abb`](https://github.com/nodejs/node/commit/686da19abb)] - **deps**: disable io\_uring support in libuv by default (Tobias Nießen) [nodejs-private/node-private#529](https://github.com/nodejs-private/node-private/pull/529)
* \[[`f7b44bfbce`](https://github.com/nodejs/node/commit/f7b44bfbce)] - **deps**: update archs files for openssl-3.0.13+quic1 (Node.js GitHub Bot) [#51614](https://github.com/nodejs/node/pull/51614)
* \[[`7a30fecea2`](https://github.com/nodejs/node/commit/7a30fecea2)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.13+quic1 (Node.js GitHub Bot) [#51614](https://github.com/nodejs/node/pull/51614)
* \[[`480fc169a8`](https://github.com/nodejs/node/commit/480fc169a8)] - **fs**: protect against modified Buffer internals in possiblyTransformPath (Tobias Nießen) [nodejs-private/node-private#497](https://github.com/nodejs-private/node-private/pull/497)
* \[[`77ac7c3153`](https://github.com/nodejs/node/commit/77ac7c3153)] - **http**: add maximum chunk extension size (Paolo Insogna) [nodejs-private/node-private#519](https://github.com/nodejs-private/node-private/pull/519)
* \[[`ed7d149675`](https://github.com/nodejs/node/commit/ed7d149675)] - **lib**: use cache fs internals against path traversal (RafaelGSS) [nodejs-private/node-private#516](https://github.com/nodejs-private/node-private/pull/516)
* \[[`89bd5fc38f`](https://github.com/nodejs/node/commit/89bd5fc38f)] - **lib**: update undici to v5.28.3 (Matteo Collina) [nodejs-private/node-private#539](https://github.com/nodejs-private/node-private/pull/539)
* \[[`d01dd4291d`](https://github.com/nodejs/node/commit/d01dd4291d)] - **permission**: fix wildcard when children > 1 (Rafael Gonzaga) [#51209](https://github.com/nodejs/node/pull/51209)
* \[[`40ff37dfcc`](https://github.com/nodejs/node/commit/40ff37dfcc)] - **src**: fix HasOnly(capability) in node::credentials (Tobias Nießen) [nodejs-private/node-private#505](https://github.com/nodejs-private/node-private/pull/505)
* \[[`3f6addd590`](https://github.com/nodejs/node/commit/3f6addd590)] - **src,deps**: disable setuid() etc if io\_uring enabled (Tobias Nießen) [nodejs-private/node-private#529](https://github.com/nodejs-private/node-private/pull/529)
* \[[`d6da413aa4`](https://github.com/nodejs/node/commit/d6da413aa4)] - **test,doc**: clarify wildcard usage (RafaelGSS) [nodejs-private/node-private#517](https://github.com/nodejs-private/node-private/pull/517)
* \[[`c213910aea`](https://github.com/nodejs/node/commit/c213910aea)] - **zlib**: pause stream if outgoing buffer is full (Matteo Collina) [nodejs-private/node-private#541](https://github.com/nodejs-private/node-private/pull/541)

<a id="20.11.0"></a>

## 2024-01-09, Version 20.11.0 'Iron' (LTS), @UlisesGascon

### Notable Changes

* \[[`833190fe7c`](https://github.com/nodejs/node/commit/833190fe7c)] - **crypto**: update root certificates to NSS 3.95 (Node.js GitHub Bot) [#50805](https://github.com/nodejs/node/pull/50805)
* \[[`a541b78bdb`](https://github.com/nodejs/node/commit/a541b78bdb)] - **doc**: add MrJithil to collaborators (Jithil P Ponnan) [#50666](https://github.com/nodejs/node/pull/50666)
* \[[`d4be8fad83`](https://github.com/nodejs/node/commit/d4be8fad83)] - **doc**: add Ethan-Arrowood as a collaborator (Ethan Arrowood) [#50393](https://github.com/nodejs/node/pull/50393)
* \[[`c1a196c897`](https://github.com/nodejs/node/commit/c1a196c897)] - **(SEMVER-MINOR)** **esm**: add import.meta.dirname and import.meta.filename (James Sumners) [#48740](https://github.com/nodejs/node/pull/48740)
* \[[`aa3209b880`](https://github.com/nodejs/node/commit/aa3209b880)] - **fs**: add c++ fast path for writeFileSync utf8 (CanadaHonk) [#49884](https://github.com/nodejs/node/pull/49884)
* \[[`8e886a2fff`](https://github.com/nodejs/node/commit/8e886a2fff)] - **(SEMVER-MINOR)** **module**: remove useCustomLoadersIfPresent flag (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`21ab3c0f0b`](https://github.com/nodejs/node/commit/21ab3c0f0b)] - **(SEMVER-MINOR)** **module**: bootstrap module loaders in shadow realm (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`29d91b13e3`](https://github.com/nodejs/node/commit/29d91b13e3)] - **(SEMVER-MINOR)** **src**: add `--disable-warning` option (Ethan Arrowood) [#50661](https://github.com/nodejs/node/pull/50661)
* \[[`11b3e470db`](https://github.com/nodejs/node/commit/11b3e470db)] - **(SEMVER-MINOR)** **src**: create per isolate proxy env template (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`621c4d66c2`](https://github.com/nodejs/node/commit/621c4d66c2)] - **(SEMVER-MINOR)** **src**: make process binding data weak (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`139d6c8d3b`](https://github.com/nodejs/node/commit/139d6c8d3b)] - **stream**: use Array for Readable buffer (Robert Nagy) [#50341](https://github.com/nodejs/node/pull/50341)
* \[[`6206957e8d`](https://github.com/nodejs/node/commit/6206957e8d)] - **stream**: optimize creation (Robert Nagy) [#50337](https://github.com/nodejs/node/pull/50337)
* \[[`e64378643d`](https://github.com/nodejs/node/commit/e64378643d)] - **(SEMVER-MINOR)** **test\_runner**: adds built in lcov reporter (Phil Nash) [#50018](https://github.com/nodejs/node/pull/50018)
* \[[`4a830c2d9d`](https://github.com/nodejs/node/commit/4a830c2d9d)] - **(SEMVER-MINOR)** **test\_runner**: add Date to the supported mock APIs (Lucas Santos) [#48638](https://github.com/nodejs/node/pull/48638)
* \[[`842dc01def`](https://github.com/nodejs/node/commit/842dc01def)] - **(SEMVER-MINOR)** **test\_runner, cli**: add --test-timeout flag (Shubham Pandey) [#50443](https://github.com/nodejs/node/pull/50443)

### Commits

* \[[`e40a559ab1`](https://github.com/nodejs/node/commit/e40a559ab1)] - **benchmark**: update iterations in benchmark/util/splice-one.js (Liu Jia) [#50698](https://github.com/nodejs/node/pull/50698)
* \[[`00f7a5d26f`](https://github.com/nodejs/node/commit/00f7a5d26f)] - **benchmark**: increase the iteration number to an appropriate value (Lei Shi) [#50766](https://github.com/nodejs/node/pull/50766)
* \[[`be6ad3f375`](https://github.com/nodejs/node/commit/be6ad3f375)] - **benchmark**: rewrite import.meta benchmark (Joyee Cheung) [#50683](https://github.com/nodejs/node/pull/50683)
* \[[`9857364129`](https://github.com/nodejs/node/commit/9857364129)] - **benchmark**: add misc/startup-cli-version benchmark (Joyee Cheung) [#50684](https://github.com/nodejs/node/pull/50684)
* \[[`22d729e7f5`](https://github.com/nodejs/node/commit/22d729e7f5)] - **benchmark**: remove punycode from require-builtins fixture (Joyee Cheung) [#50689](https://github.com/nodejs/node/pull/50689)
* \[[`4cf10a149a`](https://github.com/nodejs/node/commit/4cf10a149a)] - **benchmark**: change iterations in benchmark/es/string-concatenations.js (Liu Jia) [#50585](https://github.com/nodejs/node/pull/50585)
* \[[`15c2ed93a8`](https://github.com/nodejs/node/commit/15c2ed93a8)] - **benchmark**: add benchmarks for encodings (Aras Abbasi) [#50348](https://github.com/nodejs/node/pull/50348)
* \[[`8a896428ca`](https://github.com/nodejs/node/commit/8a896428ca)] - **benchmark**: add more cases to Readable.from (Raz Luvaton) [#50351](https://github.com/nodejs/node/pull/50351)
* \[[`dbe6c5f354`](https://github.com/nodejs/node/commit/dbe6c5f354)] - **benchmark**: skip test-benchmark-os on IBMi (Michael Dawson) [#50286](https://github.com/nodejs/node/pull/50286)
* \[[`179b4b6e62`](https://github.com/nodejs/node/commit/179b4b6e62)] - **benchmark**: move permission-fs-read to permission-processhas-fs-read (Aki Hasegawa-Johnson) [#49770](https://github.com/nodejs/node/pull/49770)
* \[[`32d65c001d`](https://github.com/nodejs/node/commit/32d65c001d)] - **buffer**: improve Buffer.equals performance (kylo5aby) [#50621](https://github.com/nodejs/node/pull/50621)
* \[[`80ea83757e`](https://github.com/nodejs/node/commit/80ea83757e)] - **build**: add GN configurations for simdjson (Cheng Zhao) [#50831](https://github.com/nodejs/node/pull/50831)
* \[[`904e645bcd`](https://github.com/nodejs/node/commit/904e645bcd)] - **build**: add configuration flag to enable Maglev (Keyhan Vakil) [#50692](https://github.com/nodejs/node/pull/50692)
* \[[`019efa8a5a`](https://github.com/nodejs/node/commit/019efa8a5a)] - **build**: fix GN configuration for deps/base64 (Cheng Zhao) [#50696](https://github.com/nodejs/node/pull/50696)
* \[[`a645d5ac54`](https://github.com/nodejs/node/commit/a645d5ac54)] - **build**: disable flag v8\_scriptormodule\_legacy\_lifetime (Chengzhong Wu) [#50616](https://github.com/nodejs/node/pull/50616)
* \[[`8705058b09`](https://github.com/nodejs/node/commit/8705058b09)] - **build**: add GN build files (Cheng Zhao) [#47637](https://github.com/nodejs/node/pull/47637)
* \[[`0a5e9c12cf`](https://github.com/nodejs/node/commit/0a5e9c12cf)] - **build**: fix build with Python 3.12 (Luigi Pinca) [#50582](https://github.com/nodejs/node/pull/50582)
* \[[`ff5713dd43`](https://github.com/nodejs/node/commit/ff5713dd43)] - **build**: support Python 3.12 (Shi Pujin) [#50209](https://github.com/nodejs/node/pull/50209)
* \[[`cfd50f229a`](https://github.com/nodejs/node/commit/cfd50f229a)] - **build**: fix building when there is only python3 (Cheng Zhao) [#48462](https://github.com/nodejs/node/pull/48462)
* \[[`833190fe7c`](https://github.com/nodejs/node/commit/833190fe7c)] - **crypto**: update root certificates to NSS 3.95 (Node.js GitHub Bot) [#50805](https://github.com/nodejs/node/pull/50805)
* \[[`54c46dae9e`](https://github.com/nodejs/node/commit/54c46dae9e)] - **deps**: update zlib to 1.2.13.1-motley-5daffc7 (Node.js GitHub Bot) [#50803](https://github.com/nodejs/node/pull/50803)
* \[[`0be84e5a28`](https://github.com/nodejs/node/commit/0be84e5a28)] - **deps**: update undici to 5.27.2 (Node.js GitHub Bot) [#50813](https://github.com/nodejs/node/pull/50813)
* \[[`ec67890824`](https://github.com/nodejs/node/commit/ec67890824)] - **deps**: V8: cherry-pick 0f9ebbc672c7 (Chengzhong Wu) [#50867](https://github.com/nodejs/node/pull/50867)
* \[[`bc2ebb972b`](https://github.com/nodejs/node/commit/bc2ebb972b)] - **deps**: V8: cherry-pick 13192d6e10fa (Levi Zim) [#50552](https://github.com/nodejs/node/pull/50552)
* \[[`656135d70a`](https://github.com/nodejs/node/commit/656135d70a)] - **deps**: update zlib to 1.2.13.1-motley-dfc48fc (Node.js GitHub Bot) [#50456](https://github.com/nodejs/node/pull/50456)
* \[[`41ee4bcc5d`](https://github.com/nodejs/node/commit/41ee4bcc5d)] - **deps**: update ada to 2.7.4 (Node.js GitHub Bot) [#50815](https://github.com/nodejs/node/pull/50815)
* \[[`a40948b5c5`](https://github.com/nodejs/node/commit/a40948b5c5)] - **deps**: update minimatch to 9.0.3 (Node.js GitHub Bot) [#50806](https://github.com/nodejs/node/pull/50806)
* \[[`7be1222c4a`](https://github.com/nodejs/node/commit/7be1222c4a)] - **deps**: update simdutf to 4.0.4 (Node.js GitHub Bot) [#50772](https://github.com/nodejs/node/pull/50772)
* \[[`68e7d49db6`](https://github.com/nodejs/node/commit/68e7d49db6)] - **deps**: upgrade npm to 10.2.4 (npm team) [#50751](https://github.com/nodejs/node/pull/50751)
* \[[`3d82d38336`](https://github.com/nodejs/node/commit/3d82d38336)] - **deps**: escape Python strings correctly (Michaël Zasso) [#50695](https://github.com/nodejs/node/pull/50695)
* \[[`d3870ac957`](https://github.com/nodejs/node/commit/d3870ac957)] - **deps**: update base64 to 0.5.1 (Node.js GitHub Bot) [#50629](https://github.com/nodejs/node/pull/50629)
* \[[`4b219b6ece`](https://github.com/nodejs/node/commit/4b219b6ece)] - **deps**: update corepack to 0.23.0 (Node.js GitHub Bot) [#50563](https://github.com/nodejs/node/pull/50563)
* \[[`6c41b50922`](https://github.com/nodejs/node/commit/6c41b50922)] - **deps**: update nghttp2 to 1.58.0 (Node.js GitHub Bot) [#50441](https://github.com/nodejs/node/pull/50441)
* \[[`3beee0ae8f`](https://github.com/nodejs/node/commit/3beee0ae8f)] - **deps**: update acorn to 8.11.2 (Node.js GitHub Bot) [#50460](https://github.com/nodejs/node/pull/50460)
* \[[`220916fa93`](https://github.com/nodejs/node/commit/220916fa93)] - **deps**: update undici to 5.27.0 (Node.js GitHub Bot) [#50463](https://github.com/nodejs/node/pull/50463)
* \[[`f9960b3545`](https://github.com/nodejs/node/commit/f9960b3545)] - **deps**: update googletest to 116b7e5 (Node.js GitHub Bot) [#50324](https://github.com/nodejs/node/pull/50324)
* \[[`d5c16f897a`](https://github.com/nodejs/node/commit/d5c16f897a)] - **dns**: call handle.setServers() with a valid array (Luigi Pinca) [#50811](https://github.com/nodejs/node/pull/50811)
* \[[`1bd6537c97`](https://github.com/nodejs/node/commit/1bd6537c97)] - **doc**: recommend supported Python versions (Luigi Pinca) [#50407](https://github.com/nodejs/node/pull/50407)
* \[[`402e257520`](https://github.com/nodejs/node/commit/402e257520)] - **doc**: update notable changes in v21.1.0 (Joyee Cheung) [#50388](https://github.com/nodejs/node/pull/50388)
* \[[`032535e270`](https://github.com/nodejs/node/commit/032535e270)] - **doc**: make theme consistent across api and other docs (Dima Demakov) [#50877](https://github.com/nodejs/node/pull/50877)
* \[[`d53842683f`](https://github.com/nodejs/node/commit/d53842683f)] - **doc**: add a section regarding `instanceof` in `primordials.md` (Antoine du Hamel) [#50874](https://github.com/nodejs/node/pull/50874)
* \[[`fe315055a7`](https://github.com/nodejs/node/commit/fe315055a7)] - **doc**: update email to reflect affiliation (Yagiz Nizipli) [#50856](https://github.com/nodejs/node/pull/50856)
* \[[`e14f661950`](https://github.com/nodejs/node/commit/e14f661950)] - **doc**: shard not supported with watch mode (Pulkit Gupta) [#50640](https://github.com/nodejs/node/pull/50640)
* \[[`b3d015de71`](https://github.com/nodejs/node/commit/b3d015de71)] - **doc**: get rid of unnecessary `eslint-skip` comments (Antoine du Hamel) [#50829](https://github.com/nodejs/node/pull/50829)
* \[[`168cbf9cb9`](https://github.com/nodejs/node/commit/168cbf9cb9)] - **doc**: create deprecation code for isWebAssemblyCompiledModule (Marco Ippolito) [#50486](https://github.com/nodejs/node/pull/50486)
* \[[`30baacba41`](https://github.com/nodejs/node/commit/30baacba41)] - **doc**: add CanadaHonk to triagers (CanadaHonk) [#50848](https://github.com/nodejs/node/pull/50848)
* \[[`e6e7cbceac`](https://github.com/nodejs/node/commit/e6e7cbceac)] - **doc**: fix typos in --allow-fs-\* (Tobias Nießen) [#50845](https://github.com/nodejs/node/pull/50845)
* \[[`e22ce9586f`](https://github.com/nodejs/node/commit/e22ce9586f)] - **doc**: update Crypto API doc for x509.keyUsage (Daniel Meechan) [#50603](https://github.com/nodejs/node/pull/50603)
* \[[`549d4422b7`](https://github.com/nodejs/node/commit/549d4422b7)] - **doc**: fix fs.writeFileSync return value documentation (Ryan Zimmerman) [#50760](https://github.com/nodejs/node/pull/50760)
* \[[`3c79e3cdba`](https://github.com/nodejs/node/commit/3c79e3cdba)] - **doc**: update print results(detail) in `PerformanceEntry` (Jungku Lee) [#50723](https://github.com/nodejs/node/pull/50723)
* \[[`aeaf96d06e`](https://github.com/nodejs/node/commit/aeaf96d06e)] - **doc**: fix `Buffer.allocUnsafe` documentation (Mert Can Altın) [#50686](https://github.com/nodejs/node/pull/50686)
* \[[`347e1dd06a`](https://github.com/nodejs/node/commit/347e1dd06a)] - **doc**: run license-builder (github-actions\[bot]) [#50691](https://github.com/nodejs/node/pull/50691)
* \[[`a541b78bdb`](https://github.com/nodejs/node/commit/a541b78bdb)] - **doc**: add MrJithil to collaborators (Jithil P Ponnan) [#50666](https://github.com/nodejs/node/pull/50666)
* \[[`90f415dd61`](https://github.com/nodejs/node/commit/90f415dd61)] - **doc**: fix typo in fs.md (fwio) [#50570](https://github.com/nodejs/node/pull/50570)
* \[[`e2388151ba`](https://github.com/nodejs/node/commit/e2388151ba)] - **doc**: add missing description of argument in `subtle.encrypt` (Deokjin Kim) [#50578](https://github.com/nodejs/node/pull/50578)
* \[[`39cc013465`](https://github.com/nodejs/node/commit/39cc013465)] - **doc**: update pm documentation to include resource (Ranieri Innocenti Spada) [#50601](https://github.com/nodejs/node/pull/50601)
* \[[`ba6d427c23`](https://github.com/nodejs/node/commit/ba6d427c23)] - **doc**: correct attribution in v20.6.0 changelog (Jacob Smith) [#50564](https://github.com/nodejs/node/pull/50564)
* \[[`1b2dab8254`](https://github.com/nodejs/node/commit/1b2dab8254)] - **doc**: update to align `console.table` row to the left (Jungku Lee) [#50553](https://github.com/nodejs/node/pull/50553)
* \[[`5d48ef7778`](https://github.com/nodejs/node/commit/5d48ef7778)] - **doc**: underline links (Rich Trott) [#50481](https://github.com/nodejs/node/pull/50481)
* \[[`5e6057c9d2`](https://github.com/nodejs/node/commit/5e6057c9d2)] - **doc**: remove duplicate word (Gerhard Stöbich) [#50475](https://github.com/nodejs/node/pull/50475)
* \[[`64bf2fd4ee`](https://github.com/nodejs/node/commit/64bf2fd4ee)] - **doc**: fix typo in `webstreams.md` (André Santos) [#50426](https://github.com/nodejs/node/pull/50426)
* \[[`cca55b8414`](https://github.com/nodejs/node/commit/cca55b8414)] - **doc**: add information about Node-API versions >=9 (Michael Dawson) [#50168](https://github.com/nodejs/node/pull/50168)
* \[[`d4be8fad83`](https://github.com/nodejs/node/commit/d4be8fad83)] - **doc**: add Ethan-Arrowood as a collaborator (Ethan Arrowood) [#50393](https://github.com/nodejs/node/pull/50393)
* \[[`0b311838f6`](https://github.com/nodejs/node/commit/0b311838f6)] - **doc**: fix TOC in `releases.md` (Bryce Seefieldt) [#50372](https://github.com/nodejs/node/pull/50372)
* \[[`843d5f84ca`](https://github.com/nodejs/node/commit/843d5f84ca)] - **esm**: fallback to `getSource` when `load` returns nullish `source` (Antoine du Hamel) [#50825](https://github.com/nodejs/node/pull/50825)
* \[[`8d5469c84b`](https://github.com/nodejs/node/commit/8d5469c84b)] - **esm**: do not call `getSource` when format is `commonjs` (Francesco Trotta) [#50465](https://github.com/nodejs/node/pull/50465)
* \[[`b48cf314d3`](https://github.com/nodejs/node/commit/b48cf314d3)] - **esm**: bypass CJS loader in default load under `--default-type=module` (Antoine du Hamel) [#50004](https://github.com/nodejs/node/pull/50004)
* \[[`c1a196c897`](https://github.com/nodejs/node/commit/c1a196c897)] - **(SEMVER-MINOR)** **esm**: add import.meta.dirname and import.meta.filename (James Sumners) [#48740](https://github.com/nodejs/node/pull/48740)
* \[[`435f9c9276`](https://github.com/nodejs/node/commit/435f9c9276)] - **fs**: use default w flag for writeFileSync with utf8 encoding (Murilo Kakazu) [#50990](https://github.com/nodejs/node/pull/50990)
* \[[`aa3209b880`](https://github.com/nodejs/node/commit/aa3209b880)] - **fs**: add c++ fast path for writeFileSync utf8 (CanadaHonk) [#49884](https://github.com/nodejs/node/pull/49884)
* \[[`05e25e0230`](https://github.com/nodejs/node/commit/05e25e0230)] - **fs**: improve error perf of sync `lstat`+`fstat` (CanadaHonk) [#49868](https://github.com/nodejs/node/pull/49868)
* \[[`f94a24cb4b`](https://github.com/nodejs/node/commit/f94a24cb4b)] - **fs**: improve error performance for `rmdirSync` (CanadaHonk) [#49846](https://github.com/nodejs/node/pull/49846)
* \[[`cada22e2a4`](https://github.com/nodejs/node/commit/cada22e2a4)] - **fs**: fix to not return for void function (Jungku Lee) [#50769](https://github.com/nodejs/node/pull/50769)
* \[[`ba40b2e33e`](https://github.com/nodejs/node/commit/ba40b2e33e)] - **fs**: replace deprecated `path._makeLong` in copyFile (CanadaHonk) [#50844](https://github.com/nodejs/node/pull/50844)
* \[[`d1b6bd660a`](https://github.com/nodejs/node/commit/d1b6bd660a)] - **fs**: update param in jsdoc for `readdir` (Jungku Lee) [#50448](https://github.com/nodejs/node/pull/50448)
* \[[`11412e863a`](https://github.com/nodejs/node/commit/11412e863a)] - **fs**: do not throw error on cpSync internals (Yagiz Nizipli) [#50185](https://github.com/nodejs/node/pull/50185)
* \[[`868a464c15`](https://github.com/nodejs/node/commit/868a464c15)] - **fs,url**: move `FromNamespacedPath` to `node_url` (Yagiz Nizipli) [#50090](https://github.com/nodejs/node/pull/50090)
* \[[`de7fe08c7b`](https://github.com/nodejs/node/commit/de7fe08c7b)] - **fs,url**: refactor `FileURLToPath` method (Yagiz Nizipli) [#50090](https://github.com/nodejs/node/pull/50090)
* \[[`186e6e0395`](https://github.com/nodejs/node/commit/186e6e0395)] - **fs,url**: move `FileURLToPath` to node\_url (Yagiz Nizipli) [#50090](https://github.com/nodejs/node/pull/50090)
* \[[`aea7fe54af`](https://github.com/nodejs/node/commit/aea7fe54af)] - **inspector**: use private fields instead of symbols (Yagiz Nizipli) [#50776](https://github.com/nodejs/node/pull/50776)
* \[[`48dbde71d8`](https://github.com/nodejs/node/commit/48dbde71d8)] - **lib**: use primordials for navigator.userAgent (Aras Abbasi) [#50467](https://github.com/nodejs/node/pull/50467)
* \[[`fa220cac87`](https://github.com/nodejs/node/commit/fa220cac87)] - **lib**: remove deprecated string methods (Jithil P Ponnan) [#50592](https://github.com/nodejs/node/pull/50592)
* \[[`f1cf1c385f`](https://github.com/nodejs/node/commit/f1cf1c385f)] - **lib**: fix assert shows diff messages in ESM and CJS (Jithil P Ponnan) [#50634](https://github.com/nodejs/node/pull/50634)
* \[[`3844af288f`](https://github.com/nodejs/node/commit/3844af288f)] - **lib**: make event static properties non writable and configurable (Muthukumar) [#50425](https://github.com/nodejs/node/pull/50425)
* \[[`0a0b416d6c`](https://github.com/nodejs/node/commit/0a0b416d6c)] - **lib**: avoid memory allocation on nodeprecation flag (Vinicius Lourenço) [#50231](https://github.com/nodejs/node/pull/50231)
* \[[`e7551d5770`](https://github.com/nodejs/node/commit/e7551d5770)] - **lib**: align console.table row to the left (Jithil P Ponnan) [#50135](https://github.com/nodejs/node/pull/50135)
* \[[`0c85cebdf2`](https://github.com/nodejs/node/commit/0c85cebdf2)] - **meta**: clarify nomination process according to Node.js charter (Matteo Collina) [#50834](https://github.com/nodejs/node/pull/50834)
* \[[`f4070dd8d4`](https://github.com/nodejs/node/commit/f4070dd8d4)] - **meta**: clarify recommendation for bug reproductions (Antoine du Hamel) [#50882](https://github.com/nodejs/node/pull/50882)
* \[[`2ddeead436`](https://github.com/nodejs/node/commit/2ddeead436)] - **meta**: move cjihrig to TSC regular member (Colin Ihrig) [#50816](https://github.com/nodejs/node/pull/50816)
* \[[`34a789d9be`](https://github.com/nodejs/node/commit/34a789d9be)] - **meta**: add web-standards as WPTs owner (Filip Skokan) [#50636](https://github.com/nodejs/node/pull/50636)
* \[[`40bbffa266`](https://github.com/nodejs/node/commit/40bbffa266)] - **meta**: bump github/codeql-action from 2.21.9 to 2.22.5 (dependabot\[bot]) [#50513](https://github.com/nodejs/node/pull/50513)
* \[[`c49553631d`](https://github.com/nodejs/node/commit/c49553631d)] - **meta**: bump step-security/harden-runner from 2.5.1 to 2.6.0 (dependabot\[bot]) [#50512](https://github.com/nodejs/node/pull/50512)
* \[[`99df0138b0`](https://github.com/nodejs/node/commit/99df0138b0)] - **meta**: bump ossf/scorecard-action from 2.2.0 to 2.3.1 (dependabot\[bot]) [#50509](https://github.com/nodejs/node/pull/50509)
* \[[`9db6227ac6`](https://github.com/nodejs/node/commit/9db6227ac6)] - **meta**: fix spacing in collaborator list (Antoine du Hamel) [#50641](https://github.com/nodejs/node/pull/50641)
* \[[`2589a5a566`](https://github.com/nodejs/node/commit/2589a5a566)] - **meta**: bump actions/setup-python from 4.7.0 to 4.7.1 (dependabot\[bot]) [#50510](https://github.com/nodejs/node/pull/50510)
* \[[`5a86661a95`](https://github.com/nodejs/node/commit/5a86661a95)] - **meta**: add crypto as crypto and webcrypto docs owner (Filip Skokan) [#50579](https://github.com/nodejs/node/pull/50579)
* \[[`ac8d2b9cc2`](https://github.com/nodejs/node/commit/ac8d2b9cc2)] - **meta**: bump actions/setup-node from 3.8.1 to 4.0.0 (dependabot\[bot]) [#50514](https://github.com/nodejs/node/pull/50514)
* \[[`bee2c0cf11`](https://github.com/nodejs/node/commit/bee2c0cf11)] - **meta**: bump actions/checkout from 4.1.0 to 4.1.1 (dependabot\[bot]) [#50511](https://github.com/nodejs/node/pull/50511)
* \[[`91a0944e5f`](https://github.com/nodejs/node/commit/91a0944e5f)] - **meta**: add <ethan.arrowood@vercel.com> to mailmap (Ethan Arrowood) [#50491](https://github.com/nodejs/node/pull/50491)
* \[[`8d3cf8c4ee`](https://github.com/nodejs/node/commit/8d3cf8c4ee)] - **meta**: add web-standards as web api visibility owner (Chengzhong Wu) [#50418](https://github.com/nodejs/node/pull/50418)
* \[[`807c12de36`](https://github.com/nodejs/node/commit/807c12de36)] - **meta**: mention other notable changes section (Rafael Gonzaga) [#50309](https://github.com/nodejs/node/pull/50309)
* \[[`21ab3c0f0b`](https://github.com/nodejs/node/commit/21ab3c0f0b)] - **(SEMVER-MINOR)** **module**: bootstrap module loaders in shadow realm (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`8e886a2fff`](https://github.com/nodejs/node/commit/8e886a2fff)] - **(SEMVER-MINOR)** **module**: remove useCustomLoadersIfPresent flag (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`77e8361213`](https://github.com/nodejs/node/commit/77e8361213)] - **module**: execute `--import` sequentially (Antoine du Hamel) [#50474](https://github.com/nodejs/node/pull/50474)
* \[[`fffc4951ac`](https://github.com/nodejs/node/commit/fffc4951ac)] - **module**: add application/json in accept header when fetching json module (Marco Ippolito) [#50119](https://github.com/nodejs/node/pull/50119)
* \[[`f808e7a650`](https://github.com/nodejs/node/commit/f808e7a650)] - **net**: check pipe mode and path (theanarkh) [#50770](https://github.com/nodejs/node/pull/50770)
* \[[`cf3a4c5b84`](https://github.com/nodejs/node/commit/cf3a4c5b84)] - **node-api**: factor out common code into macros (Gabriel Schulhof) [#50664](https://github.com/nodejs/node/pull/50664)
* \[[`a7d8f6b529`](https://github.com/nodejs/node/commit/a7d8f6b529)] - **perf\_hooks**: implement performance.now() with fast API calls (Joyee Cheung) [#50492](https://github.com/nodejs/node/pull/50492)
* \[[`076dc7540b`](https://github.com/nodejs/node/commit/076dc7540b)] - **permission**: do not create symlinks if target is relative (Tobias Nießen) [#49156](https://github.com/nodejs/node/pull/49156)
* \[[`43160dcd2d`](https://github.com/nodejs/node/commit/43160dcd2d)] - **permission**: mark const functions as such (Tobias Nießen) [#50705](https://github.com/nodejs/node/pull/50705)
* \[[`7a661d7ad9`](https://github.com/nodejs/node/commit/7a661d7ad9)] - **permission**: address coverity warning (Michael Dawson) [#50215](https://github.com/nodejs/node/pull/50215)
* \[[`b2b4132c3e`](https://github.com/nodejs/node/commit/b2b4132c3e)] - **src**: iterate on import attributes array correctly (Michaël Zasso) [#50703](https://github.com/nodejs/node/pull/50703)
* \[[`11b3e470db`](https://github.com/nodejs/node/commit/11b3e470db)] - **(SEMVER-MINOR)** **src**: create per isolate proxy env template (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`d00412a083`](https://github.com/nodejs/node/commit/d00412a083)] - **(SEMVER-MINOR)** **src**: create fs\_dir per isolate properties (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`14cc3b9b90`](https://github.com/nodejs/node/commit/14cc3b9b90)] - **(SEMVER-MINOR)** **src**: create worker per isolate properties (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`621c4d66c2`](https://github.com/nodejs/node/commit/621c4d66c2)] - **(SEMVER-MINOR)** **src**: make process binding data weak (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`07a4e94e84`](https://github.com/nodejs/node/commit/07a4e94e84)] - **src**: assert return value of BN\_bn2binpad (Tobias Nießen) [#50860](https://github.com/nodejs/node/pull/50860)
* \[[`158db2d61e`](https://github.com/nodejs/node/commit/158db2d61e)] - **src**: fix coverity warning (Michael Dawson) [#50846](https://github.com/nodejs/node/pull/50846)
* \[[`94363bb3fd`](https://github.com/nodejs/node/commit/94363bb3fd)] - **src**: fix compatility with upcoming V8 12.1 APIs (Cheng Zhao) [#50709](https://github.com/nodejs/node/pull/50709)
* \[[`29d91b13e3`](https://github.com/nodejs/node/commit/29d91b13e3)] - **(SEMVER-MINOR)** **src**: add `--disable-warning` option (Ethan Arrowood) [#50661](https://github.com/nodejs/node/pull/50661)
* \[[`f054c337f8`](https://github.com/nodejs/node/commit/f054c337f8)] - **src**: add IsolateScopes before using isolates (Keyhan Vakil) [#50680](https://github.com/nodejs/node/pull/50680)
* \[[`d08eb382cd`](https://github.com/nodejs/node/commit/d08eb382cd)] - **src**: avoid copying strings in FSPermission::Apply (Tobias Nießen) [#50662](https://github.com/nodejs/node/pull/50662)
* \[[`6620df1c05`](https://github.com/nodejs/node/commit/6620df1c05)] - **src**: remove erroneous default argument in RadixTree (Tobias Nießen) [#50736](https://github.com/nodejs/node/pull/50736)
* \[[`436c3aef15`](https://github.com/nodejs/node/commit/436c3aef15)] - **src**: fix JSONParser leaking internal V8 scopes (Keyhan Vakil) [#50688](https://github.com/nodejs/node/pull/50688)
* \[[`6f46d31018`](https://github.com/nodejs/node/commit/6f46d31018)] - **src**: return error --env-file if file is not found (Ardi Nugraha) [#50588](https://github.com/nodejs/node/pull/50588)
* \[[`3d43fd359c`](https://github.com/nodejs/node/commit/3d43fd359c)] - **src**: avoid silent coercion to signed/unsigned int (Tobias Nießen) [#50663](https://github.com/nodejs/node/pull/50663)
* \[[`c253e39b56`](https://github.com/nodejs/node/commit/c253e39b56)] - **src**: handle errors from uv\_pipe\_connect2() (Deokjin Kim) [#50657](https://github.com/nodejs/node/pull/50657)
* \[[`3a9713bb5a`](https://github.com/nodejs/node/commit/3a9713bb5a)] - **src**: use v8::Isolate::TryGetCurrent() in DumpJavaScriptBacktrace() (Joyee Cheung) [#50518](https://github.com/nodejs/node/pull/50518)
* \[[`94f8a925a8`](https://github.com/nodejs/node/commit/94f8a925a8)] - **src**: print more information in C++ assertions (Joyee Cheung) [#50242](https://github.com/nodejs/node/pull/50242)
* \[[`23f830616b`](https://github.com/nodejs/node/commit/23f830616b)] - **src**: hide node::credentials::HasOnly outside unit (Tobias Nießen) [#50450](https://github.com/nodejs/node/pull/50450)
* \[[`b7ecb0a390`](https://github.com/nodejs/node/commit/b7ecb0a390)] - **src**: readiterable entries may be empty (Matthew Aitken) [#50398](https://github.com/nodejs/node/pull/50398)
* \[[`4ef1d68715`](https://github.com/nodejs/node/commit/4ef1d68715)] - **src**: implement structuredClone in native (Joyee Cheung) [#50330](https://github.com/nodejs/node/pull/50330)
* \[[`9346f15138`](https://github.com/nodejs/node/commit/9346f15138)] - **src**: use find instead of char-by-char in FromFilePath() (Daniel Lemire) [#50288](https://github.com/nodejs/node/pull/50288)
* \[[`8414fb4d2a`](https://github.com/nodejs/node/commit/8414fb4d2a)] - **src**: add commit hash shorthand in zlib version (Jithil P Ponnan) [#50158](https://github.com/nodejs/node/pull/50158)
* \[[`a878e3abb0`](https://github.com/nodejs/node/commit/a878e3abb0)] - **stream**: fix enumerability of ReadableStream.from (Mattias Buelens) [#50779](https://github.com/nodejs/node/pull/50779)
* \[[`95ed4ffc1e`](https://github.com/nodejs/node/commit/95ed4ffc1e)] - **stream**: fix enumerability of ReadableStream.prototype.values (Mattias Buelens) [#50779](https://github.com/nodejs/node/pull/50779)
* \[[`4cf155ca0c`](https://github.com/nodejs/node/commit/4cf155ca0c)] - **stream**: add Symbol.toStringTag to Compression Streams (Filip Skokan) [#50712](https://github.com/nodejs/node/pull/50712)
* \[[`6012e3e781`](https://github.com/nodejs/node/commit/6012e3e781)] - **stream**: fix Writable.destroy performance regression (Robert Nagy) [#50478](https://github.com/nodejs/node/pull/50478)
* \[[`dd5206820c`](https://github.com/nodejs/node/commit/dd5206820c)] - **stream**: pre-allocate \_events (Robert Nagy) [#50428](https://github.com/nodejs/node/pull/50428)
* \[[`829b82ed0f`](https://github.com/nodejs/node/commit/829b82ed0f)] - **stream**: remove no longer relevant comment (Robert Nagy) [#50446](https://github.com/nodejs/node/pull/50446)
* \[[`98ae1b4132`](https://github.com/nodejs/node/commit/98ae1b4132)] - **stream**: use bit fields for construct/destroy (Robert Nagy) [#50408](https://github.com/nodejs/node/pull/50408)
* \[[`08a0c6c56c`](https://github.com/nodejs/node/commit/08a0c6c56c)] - **stream**: improve from perf (Raz Luvaton) [#50359](https://github.com/nodejs/node/pull/50359)
* \[[`59f7316b8f`](https://github.com/nodejs/node/commit/59f7316b8f)] - **stream**: avoid calls to listenerCount (Robert Nagy) [#50357](https://github.com/nodejs/node/pull/50357)
* \[[`9d52430eb9`](https://github.com/nodejs/node/commit/9d52430eb9)] - **stream**: readable use bitmap accessors (Robert Nagy) [#50350](https://github.com/nodejs/node/pull/50350)
* \[[`139d6c8d3b`](https://github.com/nodejs/node/commit/139d6c8d3b)] - **stream**: use Array for Readable buffer (Robert Nagy) [#50341](https://github.com/nodejs/node/pull/50341)
* \[[`6206957e8d`](https://github.com/nodejs/node/commit/6206957e8d)] - **stream**: optimize creation (Robert Nagy) [#50337](https://github.com/nodejs/node/pull/50337)
* \[[`f87921de3b`](https://github.com/nodejs/node/commit/f87921de3b)] - **stream**: refactor writable \_write (Robert Nagy) [#50198](https://github.com/nodejs/node/pull/50198)
* \[[`b338f3d3c2`](https://github.com/nodejs/node/commit/b338f3d3c2)] - **stream**: avoid getter for defaultEncoding (Robert Nagy) [#50203](https://github.com/nodejs/node/pull/50203)
* \[[`1862235a26`](https://github.com/nodejs/node/commit/1862235a26)] - **test**: fix message v8 not normalising alphanumeric paths (Jithil P Ponnan) [#50730](https://github.com/nodejs/node/pull/50730)
* \[[`7c28a4ca8f`](https://github.com/nodejs/node/commit/7c28a4ca8f)] - **test**: fix dns test case failures after c-ares update to 1.21.0+ (Brad House) [#50743](https://github.com/nodejs/node/pull/50743)
* \[[`4544593d31`](https://github.com/nodejs/node/commit/4544593d31)] - **test**: replace forEach with for of (Conor Watson) [#50594](https://github.com/nodejs/node/pull/50594)
* \[[`96143a3293`](https://github.com/nodejs/node/commit/96143a3293)] - **test**: replace forEach to for at test-webcrypto-sign-verify-ecdsa.js (Alessandro Di Nisio) [#50795](https://github.com/nodejs/node/pull/50795)
* \[[`107b5e63c5`](https://github.com/nodejs/node/commit/107b5e63c5)] - **test**: replace foreach with for in test-https-simple.js (Shikha Mehta) [#49793](https://github.com/nodejs/node/pull/49793)
* \[[`9b2e5e9db4`](https://github.com/nodejs/node/commit/9b2e5e9db4)] - **test**: add note about unresolved spec issue (Mattias Buelens) [#50779](https://github.com/nodejs/node/pull/50779)
* \[[`edce637c1a`](https://github.com/nodejs/node/commit/edce637c1a)] - **test**: add note about readable streams with type owning (Mattias Buelens) [#50779](https://github.com/nodejs/node/pull/50779)
* \[[`641044670b`](https://github.com/nodejs/node/commit/641044670b)] - **test**: replace forEach with for-of in test-url-relative (vitosorriso) [#50788](https://github.com/nodejs/node/pull/50788)
* \[[`75ee78438c`](https://github.com/nodejs/node/commit/75ee78438c)] - **test**: replace forEach() with for ... of in test-tls-getprotocol.js (Steve Goode) [#50600](https://github.com/nodejs/node/pull/50600)
* \[[`24f9d3fbeb`](https://github.com/nodejs/node/commit/24f9d3fbeb)] - **test**: enable idlharness tests for encoding (Mattias Buelens) [#50778](https://github.com/nodejs/node/pull/50778)
* \[[`a9d290956e`](https://github.com/nodejs/node/commit/a9d290956e)] - **test**: replace forEach in whatwg-encoding-custom-interop (Honza Machala) [#50607](https://github.com/nodejs/node/pull/50607)
* \[[`6584dd80f7`](https://github.com/nodejs/node/commit/6584dd80f7)] - **test**: replace forEach() with for-loop (Jan) [#50596](https://github.com/nodejs/node/pull/50596)
* \[[`be54a22869`](https://github.com/nodejs/node/commit/be54a22869)] - **test**: improve test-bootstrap-modules.js (Joyee Cheung) [#50708](https://github.com/nodejs/node/pull/50708)
* \[[`660e70e73b`](https://github.com/nodejs/node/commit/660e70e73b)] - **test**: skip parallel/test-macos-app-sandbox if disk space < 120MB (Joyee Cheung) [#50764](https://github.com/nodejs/node/pull/50764)
* \[[`5712c41122`](https://github.com/nodejs/node/commit/5712c41122)] - **test**: replace foreach with for (Markus Muschol) [#50599](https://github.com/nodejs/node/pull/50599)
* \[[`49e5f47b1c`](https://github.com/nodejs/node/commit/49e5f47b1c)] - **test**: test streambase has already has a consumer (Jithil P Ponnan) [#48059](https://github.com/nodejs/node/pull/48059)
* \[[`bb7d764c8e`](https://github.com/nodejs/node/commit/bb7d764c8e)] - **test**: change forEach to for...of in path extname (Kyriakos Markakis) [#50667](https://github.com/nodejs/node/pull/50667)
* \[[`4d28ced079`](https://github.com/nodejs/node/commit/4d28ced079)] - **test**: replace forEach with for...of (Ryan Williams) [#50611](https://github.com/nodejs/node/pull/50611)
* \[[`92a153ecde`](https://github.com/nodejs/node/commit/92a153ecde)] - **test**: migrate message v8 tests from Python to JS (Joshua LeMay) [#50421](https://github.com/nodejs/node/pull/50421)
* \[[`a376284d8a`](https://github.com/nodejs/node/commit/a376284d8a)] - **test**: use destructuring for accessing setting values (Honza Jedlička) [#50609](https://github.com/nodejs/node/pull/50609)
* \[[`7b9b1fba27`](https://github.com/nodejs/node/commit/7b9b1fba27)] - **test**: replace forEach() with for .. of (Evgenia Blajer) [#50605](https://github.com/nodejs/node/pull/50605)
* \[[`9397b2da7e`](https://github.com/nodejs/node/commit/9397b2da7e)] - **test**: replace forEach() with for ... of in test-readline-keys.js (William Liang) [#50604](https://github.com/nodejs/node/pull/50604)
* \[[`9043ba4cfb`](https://github.com/nodejs/node/commit/9043ba4cfb)] - **test**: replace forEach() with for ... of in test-http2-single-headers.js (spiritualized) [#50606](https://github.com/nodejs/node/pull/50606)
* \[[`9f911d31f6`](https://github.com/nodejs/node/commit/9f911d31f6)] - **test**: replace forEach with for of (john-mcinall) [#50602](https://github.com/nodejs/node/pull/50602)
* \[[`8a5f36fe74`](https://github.com/nodejs/node/commit/8a5f36fe74)] - **test**: remove unused file (James Sumners) [#50528](https://github.com/nodejs/node/pull/50528)
* \[[`9950203340`](https://github.com/nodejs/node/commit/9950203340)] - **test**: replace forEach with for of (Kevin Kühnemund) [#50597](https://github.com/nodejs/node/pull/50597)
* \[[`03ba28f102`](https://github.com/nodejs/node/commit/03ba28f102)] - **test**: replace forEach with for of (CorrWu) [#49785](https://github.com/nodejs/node/pull/49785)
* \[[`ea61261b54`](https://github.com/nodejs/node/commit/ea61261b54)] - **test**: replace forEach with for \[...] of (Gabriel Bota) [#50615](https://github.com/nodejs/node/pull/50615)
* \[[`4349790913`](https://github.com/nodejs/node/commit/4349790913)] - **test**: add WPT report test duration (Filip Skokan) [#50574](https://github.com/nodejs/node/pull/50574)
* \[[`7cacddfcc1`](https://github.com/nodejs/node/commit/7cacddfcc1)] - **test**: replace forEach() with for ... of loop in test-global.js (Kajol) [#49772](https://github.com/nodejs/node/pull/49772)
* \[[`889f58d07f`](https://github.com/nodejs/node/commit/889f58d07f)] - **test**: skip test-diagnostics-channel-memory-leak.js (Joyee Cheung) [#50327](https://github.com/nodejs/node/pull/50327)
* \[[`41644ee071`](https://github.com/nodejs/node/commit/41644ee071)] - **test**: improve `UV_THREADPOOL_SIZE` tests on `.env` (Yagiz Nizipli) [#49213](https://github.com/nodejs/node/pull/49213)
* \[[`1db44b9a53`](https://github.com/nodejs/node/commit/1db44b9a53)] - **test**: recognize wpt completion error (Chengzhong Wu) [#50429](https://github.com/nodejs/node/pull/50429)
* \[[`ecfc951ddc`](https://github.com/nodejs/node/commit/ecfc951ddc)] - **test**: report error wpt test results (Chengzhong Wu) [#50429](https://github.com/nodejs/node/pull/50429)
* \[[`deb0351d95`](https://github.com/nodejs/node/commit/deb0351d95)] - **test**: replace forEach() with for...of (Ram) [#49794](https://github.com/nodejs/node/pull/49794)
* \[[`f885dfe5e3`](https://github.com/nodejs/node/commit/f885dfe5e3)] - **test**: replace forEach() with for...of in test-trace-events-http (Chand) [#49795](https://github.com/nodejs/node/pull/49795)
* \[[`9dc63c56db`](https://github.com/nodejs/node/commit/9dc63c56db)] - **test**: replace forEach with for...of in test-fs-realpath-buffer-encoding (Niya Shiyas) [#49804](https://github.com/nodejs/node/pull/49804)
* \[[`600d1260da`](https://github.com/nodejs/node/commit/600d1260da)] - **test**: fix timeout of test-cpu-prof-dir-worker.js in LoongArch devices (Shi Pujin) [#50363](https://github.com/nodejs/node/pull/50363)
* \[[`099f5cfa0a`](https://github.com/nodejs/node/commit/099f5cfa0a)] - **test**: fix vm assertion actual and expected order (Chengzhong Wu) [#50371](https://github.com/nodejs/node/pull/50371)
* \[[`a31f9bfe01`](https://github.com/nodejs/node/commit/a31f9bfe01)] - **test**: v8: Add test-linux-perf-logger test suite (Luke Albao) [#50352](https://github.com/nodejs/node/pull/50352)
* \[[`6c59114947`](https://github.com/nodejs/node/commit/6c59114947)] - **test**: ensure never settling promises are detected (Antoine du Hamel) [#50318](https://github.com/nodejs/node/pull/50318)
* \[[`9830ae4bf7`](https://github.com/nodejs/node/commit/9830ae4bf7)] - **test\_runner**: add tests for various mock timer issues (Mika Fischer) [#50384](https://github.com/nodejs/node/pull/50384)
* \[[`2c72ed85fb`](https://github.com/nodejs/node/commit/2c72ed85fb)] - **test\_runner**: pass abortSignal to test files (Moshe Atlow) [#50630](https://github.com/nodejs/node/pull/50630)
* \[[`c33a84af11`](https://github.com/nodejs/node/commit/c33a84af11)] - **test\_runner**: replace forEach with for of (Tom Haddad) [#50595](https://github.com/nodejs/node/pull/50595)
* \[[`29c68a22bb`](https://github.com/nodejs/node/commit/29c68a22bb)] - **test\_runner**: output errors of suites (Moshe Atlow) [#50361](https://github.com/nodejs/node/pull/50361)
* \[[`e64378643d`](https://github.com/nodejs/node/commit/e64378643d)] - **(SEMVER-MINOR)** **test\_runner**: adds built in lcov reporter (Phil Nash) [#50018](https://github.com/nodejs/node/pull/50018)
* \[[`4aaaff413b`](https://github.com/nodejs/node/commit/4aaaff413b)] - **test\_runner**: test return value of mocked promisified timers (Mika Fischer) [#50331](https://github.com/nodejs/node/pull/50331)
* \[[`4a830c2d9d`](https://github.com/nodejs/node/commit/4a830c2d9d)] - **(SEMVER-MINOR)** **test\_runner**: add Date to the supported mock APIs (Lucas Santos) [#48638](https://github.com/nodejs/node/pull/48638)
* \[[`842dc01def`](https://github.com/nodejs/node/commit/842dc01def)] - **(SEMVER-MINOR)** **test\_runner, cli**: add --test-timeout flag (Shubham Pandey) [#50443](https://github.com/nodejs/node/pull/50443)
* \[[`613a9072b7`](https://github.com/nodejs/node/commit/613a9072b7)] - **tls**: fix order of setting cipher before setting cert and key (Kumar Rishav) [#50186](https://github.com/nodejs/node/pull/50186)
* \[[`d905c61e16`](https://github.com/nodejs/node/commit/d905c61e16)] - **tls**: use `validateFunction` for `options.SNICallback` (Deokjin Kim) [#50530](https://github.com/nodejs/node/pull/50530)
* \[[`c8d6dd58e7`](https://github.com/nodejs/node/commit/c8d6dd58e7)] - **tools**: add macOS notarization verification step (Ulises Gascón) [#50833](https://github.com/nodejs/node/pull/50833)
* \[[`c9bd0b0c0f`](https://github.com/nodejs/node/commit/c9bd0b0c0f)] - **tools**: use macOS keychain to notarize the releases (Ulises Gascón) [#50715](https://github.com/nodejs/node/pull/50715)
* \[[`932a5d7b2c`](https://github.com/nodejs/node/commit/932a5d7b2c)] - **tools**: update eslint to 8.54.0 (Node.js GitHub Bot) [#50809](https://github.com/nodejs/node/pull/50809)
* \[[`d7114d97be`](https://github.com/nodejs/node/commit/d7114d97be)] - **tools**: update lint-md-dependencies to rollup\@4.5.0 (Node.js GitHub Bot) [#50807](https://github.com/nodejs/node/pull/50807)
* \[[`93085cf844`](https://github.com/nodejs/node/commit/93085cf844)] - **tools**: add workflow to update release links (Michaël Zasso) [#50710](https://github.com/nodejs/node/pull/50710)
* \[[`66764c5d04`](https://github.com/nodejs/node/commit/66764c5d04)] - **tools**: recognize GN files in dep\_updaters (Cheng Zhao) [#50693](https://github.com/nodejs/node/pull/50693)
* \[[`2a451e176a`](https://github.com/nodejs/node/commit/2a451e176a)] - **tools**: remove unused file (Ulises Gascon) [#50622](https://github.com/nodejs/node/pull/50622)
* \[[`8ce6403230`](https://github.com/nodejs/node/commit/8ce6403230)] - **tools**: change minimatch install strategy (Marco Ippolito) [#50476](https://github.com/nodejs/node/pull/50476)
* \[[`97778e2e77`](https://github.com/nodejs/node/commit/97778e2e77)] - **tools**: update lint-md-dependencies to rollup\@4.3.1 (Node.js GitHub Bot) [#50675](https://github.com/nodejs/node/pull/50675)
* \[[`797f6a9ba8`](https://github.com/nodejs/node/commit/797f6a9ba8)] - **tools**: add macOS notarization stapler (Ulises Gascón) [#50625](https://github.com/nodejs/node/pull/50625)
* \[[`8fa1319352`](https://github.com/nodejs/node/commit/8fa1319352)] - **tools**: update eslint to 8.53.0 (Node.js GitHub Bot) [#50559](https://github.com/nodejs/node/pull/50559)
* \[[`592f57970f`](https://github.com/nodejs/node/commit/592f57970f)] - **tools**: update lint-md-dependencies to rollup\@4.3.0 (Node.js GitHub Bot) [#50556](https://github.com/nodejs/node/pull/50556)
* \[[`2fd78fc39e`](https://github.com/nodejs/node/commit/2fd78fc39e)] - **tools**: compare ICU checksums before file changes (Michaël Zasso) [#50522](https://github.com/nodejs/node/pull/50522)
* \[[`631d710fc4`](https://github.com/nodejs/node/commit/631d710fc4)] - **tools**: improve update acorn-walk script (Marco Ippolito) [#50473](https://github.com/nodejs/node/pull/50473)
* \[[`33fd2af2ab`](https://github.com/nodejs/node/commit/33fd2af2ab)] - **tools**: update lint-md-dependencies to rollup\@4.2.0 (Node.js GitHub Bot) [#50496](https://github.com/nodejs/node/pull/50496)
* \[[`22b7a74838`](https://github.com/nodejs/node/commit/22b7a74838)] - **tools**: update gyp-next to v0.16.1 (Michaël Zasso) [#50380](https://github.com/nodejs/node/pull/50380)
* \[[`f5ccab5005`](https://github.com/nodejs/node/commit/f5ccab5005)] - **tools**: skip ruff on tools/gyp (Michaël Zasso) [#50380](https://github.com/nodejs/node/pull/50380)
* \[[`408fd90508`](https://github.com/nodejs/node/commit/408fd90508)] - **tools**: update lint-md-dependencies to rollup\@4.1.5 unified\@11.0.4 (Node.js GitHub Bot) [#50461](https://github.com/nodejs/node/pull/50461)
* \[[`685f936ccd`](https://github.com/nodejs/node/commit/685f936ccd)] - **tools**: avoid npm install in deps installation (Marco Ippolito) [#50413](https://github.com/nodejs/node/pull/50413)
* \[[`7d43c5a094`](https://github.com/nodejs/node/commit/7d43c5a094)] - _**Revert**_ "**tools**: update doc dependencies" (Richard Lau) [#50414](https://github.com/nodejs/node/pull/50414)
* \[[`8fd67c2e3e`](https://github.com/nodejs/node/commit/8fd67c2e3e)] - **tools**: update doc dependencies (Node.js GitHub Bot) [#49988](https://github.com/nodejs/node/pull/49988)
* \[[`586becb507`](https://github.com/nodejs/node/commit/586becb507)] - **tools**: run coverage CI only on relevant files (Antoine du Hamel) [#50349](https://github.com/nodejs/node/pull/50349)
* \[[`2d06eea6c5`](https://github.com/nodejs/node/commit/2d06eea6c5)] - **tools**: update eslint to 8.52.0 (Node.js GitHub Bot) [#50326](https://github.com/nodejs/node/pull/50326)
* \[[`6a897baf16`](https://github.com/nodejs/node/commit/6a897baf16)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#50190](https://github.com/nodejs/node/pull/50190)
* \[[`e6e7f39b9e`](https://github.com/nodejs/node/commit/e6e7f39b9e)] - **util**: improve performance of normalizeEncoding (kylo5aby) [#50721](https://github.com/nodejs/node/pull/50721)
* \[[`3b6b1afa47`](https://github.com/nodejs/node/commit/3b6b1afa47)] - **v8,tools**: expose necessary V8 defines (Cheng Zhao) [#50820](https://github.com/nodejs/node/pull/50820)
* \[[`2664012617`](https://github.com/nodejs/node/commit/2664012617)] - **vm**: allow dynamic import with a referrer realm (Chengzhong Wu) [#50360](https://github.com/nodejs/node/pull/50360)
* \[[`c6c0a74b54`](https://github.com/nodejs/node/commit/c6c0a74b54)] - **wasi**: document security sandboxing status (Guy Bedford) [#50396](https://github.com/nodejs/node/pull/50396)
* \[[`989814093e`](https://github.com/nodejs/node/commit/989814093e)] - **win,tools**: upgrade Windows signing to smctl (Stefan Stojanovic) [#50956](https://github.com/nodejs/node/pull/50956)

<a id="20.10.0"></a>

## 2023-11-22, Version 20.10.0 'Iron' (LTS), @targos

### Notable Changes

#### `--experimental-default-type` flag to flip module defaults

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

#### Detect ESM syntax in ambiguous JavaScript

The new flag `--experimental-detect-module` can be used to automatically run ES
modules when their syntax can be detected. For “ambiguous” files, which are
`.js` or extensionless files with no `package.json` with a `type` field, Node.js
will parse the file to detect ES module syntax; if found, it will run the file
as an ES module, otherwise it will run the file as a CommonJS module. The same
applies to string input via `--eval` or `STDIN`.

We hope to make detection enabled by default in a future version of Node.js.
Detection increases startup time, so we encourage everyone—especially package
authors—to add a `type` field to `package.json`, even for the default
`"type": "commonjs"`. The presence of a `type` field, or explicit extensions
such as `.mjs` or `.cjs`, will opt out of detection.

Contributed by Geoffrey Booth in [#50096](https://github.com/nodejs/node/pull/50096).

#### New `flush` option in file system functions

When writing to files, it is possible that data is not immediately flushed to
permanent storage. This allows subsequent read operations to see stale data.
This PR adds a `'flush'` option to the `fs.writeFile` family of functions which
forces the data to be flushed at the end of a successful write operation.

Contributed by Colin Ihrig in [#50009](https://github.com/nodejs/node/pull/50009) and [#50095](https://github.com/nodejs/node/pull/50095).

#### Experimental WebSocket client

Adds a `--experimental-websocket` flag that adds a [`WebSocket`](https://developer.mozilla.org/en-US/docs/Web/API/WebSocket)
global, as [standardized by WHATWG](https://websockets.spec.whatwg.org/#the-websocket-interface).

Contributed by Matthew Aitken in [#49830](https://github.com/nodejs/node/pull/49830).

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

#### Other notable changes

* \[[`21453ae555`](https://github.com/nodejs/node/commit/21453ae555)] - **(SEMVER-MINOR)** **deps**: update uvwasi to 0.0.19 (Node.js GitHub Bot) [#49908](https://github.com/nodejs/node/pull/49908)
* \[[`ee65e44c31`](https://github.com/nodejs/node/commit/ee65e44c31)] - **esm**: use import attributes instead of import assertions (Antoine du Hamel) [#50140](https://github.com/nodejs/node/pull/50140)
* \[[`ffdc357167`](https://github.com/nodejs/node/commit/ffdc357167)] - **(SEMVER-MINOR)** **stream**: allow passing stream class to `stream.compose` (Alex Yang) [#50187](https://github.com/nodejs/node/pull/50187)
* \[[`4861ad6431`](https://github.com/nodejs/node/commit/4861ad6431)] - **stream**: improve performance of readable stream reads (Raz Luvaton) [#50173](https://github.com/nodejs/node/pull/50173)
* \[[`4b27087b30`](https://github.com/nodejs/node/commit/4b27087b30)] - **stream**: optimize Writable (Robert Nagy) [#50012](https://github.com/nodejs/node/pull/50012)
* \[[`709c6c0cab`](https://github.com/nodejs/node/commit/709c6c0cab)] - **(SEMVER-MINOR)** **test\_runner, cli**: add --test-concurrency flag (Colin Ihrig) [#49996](https://github.com/nodejs/node/pull/49996)
* \[[`57efd5292c`](https://github.com/nodejs/node/commit/57efd5292c)] - **(SEMVER-MINOR)** **vm**: use import attributes instead of import assertions (Antoine du Hamel) [#50141](https://github.com/nodejs/node/pull/50141)

### Commits

* \[[`73757a5f42`](https://github.com/nodejs/node/commit/73757a5f42)] - **benchmark**: fix race condition on fs benchs (Vinicius Lourenço) [#50035](https://github.com/nodejs/node/pull/50035)
* \[[`23269717bb`](https://github.com/nodejs/node/commit/23269717bb)] - **benchmark**: add warmup to accessSync bench (Rafael Gonzaga) [#50073](https://github.com/nodejs/node/pull/50073)
* \[[`88611d199a`](https://github.com/nodejs/node/commit/88611d199a)] - **benchmark**: improved config for blob,file benchmark (Vinícius Lourenço) [#49730](https://github.com/nodejs/node/pull/49730)
* \[[`b70757476c`](https://github.com/nodejs/node/commit/b70757476c)] - **benchmark**: added new benchmarks for blob (Vinícius Lourenço) [#49730](https://github.com/nodejs/node/pull/49730)
* \[[`458d9a82e3`](https://github.com/nodejs/node/commit/458d9a82e3)] - **buffer**: remove unnecessary assignment in fromString (Tobias Nießen) [#50199](https://github.com/nodejs/node/pull/50199)
* \[[`878c0b332e`](https://github.com/nodejs/node/commit/878c0b332e)] - **build**: fix IBM i build with Python 3.9 (Richard Lau) [#48056](https://github.com/nodejs/node/pull/48056)
* \[[`773320e1de`](https://github.com/nodejs/node/commit/773320e1de)] - **crypto**: ensure valid point on elliptic curve in SubtleCrypto.importKey (Filip Skokan) [#50234](https://github.com/nodejs/node/pull/50234)
* \[[`edb0ffd7d4`](https://github.com/nodejs/node/commit/edb0ffd7d4)] - **crypto**: use X509\_ALGOR accessors instead of reaching into X509\_ALGOR (David Benjamin) [#50057](https://github.com/nodejs/node/pull/50057)
* \[[`3f98c06dbb`](https://github.com/nodejs/node/commit/3f98c06dbb)] - **crypto**: account for disabled SharedArrayBuffer (Shelley Vohr) [#50034](https://github.com/nodejs/node/pull/50034)
* \[[`55485ff1cc`](https://github.com/nodejs/node/commit/55485ff1cc)] - **crypto**: return clear errors when loading invalid PFX data (Tim Perry) [#49566](https://github.com/nodejs/node/pull/49566)
* \[[`68ec1e5eeb`](https://github.com/nodejs/node/commit/68ec1e5eeb)] - **deps**: upgrade npm to 10.2.3 (npm team) [#50531](https://github.com/nodejs/node/pull/50531)
* \[[`b00c11ad7c`](https://github.com/nodejs/node/commit/b00c11ad7c)] - **deps**: V8: cherry-pick d90d4533b053 (Michaël Zasso) [#50077](https://github.com/nodejs/node/pull/50077)
* \[[`e63aef91b4`](https://github.com/nodejs/node/commit/e63aef91b4)] - **deps**: V8: cherry-pick f7d000a7ae7b (Luke Albao) [#50077](https://github.com/nodejs/node/pull/50077)
* \[[`4b243b553a`](https://github.com/nodejs/node/commit/4b243b553a)] - **deps**: V8: cherry-pick 9721082687c9 (Shi Pujin) [#50077](https://github.com/nodejs/node/pull/50077)
* \[[`9d3cdcbebf`](https://github.com/nodejs/node/commit/9d3cdcbebf)] - **deps**: V8: cherry-pick 840650f2ff4e (Michaël Zasso) [#50077](https://github.com/nodejs/node/pull/50077)
* \[[`0c40b513fd`](https://github.com/nodejs/node/commit/0c40b513fd)] - **deps**: V8: cherry-pick a1efa5343880 (Michaël Zasso) [#50077](https://github.com/nodejs/node/pull/50077)
* \[[`68cddd79f7`](https://github.com/nodejs/node/commit/68cddd79f7)] - **deps**: update archs files for openssl-3.0.12+quic1 (Node.js GitHub Bot) [#50411](https://github.com/nodejs/node/pull/50411)
* \[[`3308189180`](https://github.com/nodejs/node/commit/3308189180)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.12+quic1 (Node.js GitHub Bot) [#50411](https://github.com/nodejs/node/pull/50411)
* \[[`b61707e535`](https://github.com/nodejs/node/commit/b61707e535)] - **deps**: update ada to 2.7.2 (Node.js GitHub Bot) [#50338](https://github.com/nodejs/node/pull/50338)
* \[[`1aecf0c17b`](https://github.com/nodejs/node/commit/1aecf0c17b)] - **deps**: update corepack to 0.22.0 (Node.js GitHub Bot) [#50325](https://github.com/nodejs/node/pull/50325)
* \[[`f5924f174c`](https://github.com/nodejs/node/commit/f5924f174c)] - **deps**: update c-ares to 1.20.1 (Node.js GitHub Bot) [#50082](https://github.com/nodejs/node/pull/50082)
* \[[`b705e19a95`](https://github.com/nodejs/node/commit/b705e19a95)] - **deps**: update c-ares to 1.20.0 (Node.js GitHub Bot) [#50082](https://github.com/nodejs/node/pull/50082)
* \[[`f72cbb7e02`](https://github.com/nodejs/node/commit/f72cbb7e02)] - **deps**: V8: cherry-pick 25902244ad1a (Joyee Cheung) [#50156](https://github.com/nodejs/node/pull/50156)
* \[[`6547bd2493`](https://github.com/nodejs/node/commit/6547bd2493)] - **deps**: V8: cherry-pick ea996ad04a68 (Antoine du Hamel) [#50183](https://github.com/nodejs/node/pull/50183)
* \[[`16fd730e95`](https://github.com/nodejs/node/commit/16fd730e95)] - **deps**: V8: cherry-pick a0fd3209dda8 (Antoine du Hamel) [#50183](https://github.com/nodejs/node/pull/50183)
* \[[`614c3620c3`](https://github.com/nodejs/node/commit/614c3620c3)] - **deps**: update corepack to 0.21.0 (Node.js GitHub Bot) [#50088](https://github.com/nodejs/node/pull/50088)
* \[[`545aa74ae2`](https://github.com/nodejs/node/commit/545aa74ae2)] - **deps**: update simdutf to 3.2.18 (Node.js GitHub Bot) [#50091](https://github.com/nodejs/node/pull/50091)
* \[[`9302806c0a`](https://github.com/nodejs/node/commit/9302806c0a)] - **deps**: update zlib to 1.2.13.1-motley-fef5869 (Node.js GitHub Bot) [#50085](https://github.com/nodejs/node/pull/50085)
* \[[`03bf5c5d9a`](https://github.com/nodejs/node/commit/03bf5c5d9a)] - **deps**: update googletest to 2dd1c13 (Node.js GitHub Bot) [#50081](https://github.com/nodejs/node/pull/50081)
* \[[`cd8e90690b`](https://github.com/nodejs/node/commit/cd8e90690b)] - **deps**: update googletest to e47544a (Node.js GitHub Bot) [#49982](https://github.com/nodejs/node/pull/49982)
* \[[`40672cfe53`](https://github.com/nodejs/node/commit/40672cfe53)] - **deps**: update ada to 2.6.10 (Node.js GitHub Bot) [#49984](https://github.com/nodejs/node/pull/49984)
* \[[`34c7eb0eb2`](https://github.com/nodejs/node/commit/34c7eb0eb2)] - **deps**: fix call to undeclared functions 'ntohl' and 'htons' (MatteoBax) [#49979](https://github.com/nodejs/node/pull/49979)
* \[[`03654b44b6`](https://github.com/nodejs/node/commit/03654b44b6)] - **deps**: update ada to 2.6.9 (Node.js GitHub Bot) [#49340](https://github.com/nodejs/node/pull/49340)
* \[[`4c740b1dd8`](https://github.com/nodejs/node/commit/4c740b1dd8)] - **deps**: update ada to 2.6.8 (Node.js GitHub Bot) [#49340](https://github.com/nodejs/node/pull/49340)
* \[[`759cf5a760`](https://github.com/nodejs/node/commit/759cf5a760)] - **deps**: update ada to 2.6.7 (Node.js GitHub Bot) [#49340](https://github.com/nodejs/node/pull/49340)
* \[[`31a4e9781a`](https://github.com/nodejs/node/commit/31a4e9781a)] - **deps**: update ada to 2.6.5 (Node.js GitHub Bot) [#49340](https://github.com/nodejs/node/pull/49340)
* \[[`2ca867f2ab`](https://github.com/nodejs/node/commit/2ca867f2ab)] - **deps**: update ada to 2.6.3 (Node.js GitHub Bot) [#49340](https://github.com/nodejs/node/pull/49340)
* \[[`21453ae555`](https://github.com/nodejs/node/commit/21453ae555)] - **(SEMVER-MINOR)** **deps**: update uvwasi to 0.0.19 (Node.js GitHub Bot) [#49908](https://github.com/nodejs/node/pull/49908)
* \[[`7ca1228be8`](https://github.com/nodejs/node/commit/7ca1228be8)] - **deps**: V8: cherry-pick 8ec2651fbdd8 (Abdirahim Musse) [#49862](https://github.com/nodejs/node/pull/49862)
* \[[`3cc41d253c`](https://github.com/nodejs/node/commit/3cc41d253c)] - **deps**: upgrade npm to 10.2.0 (npm team) [#50027](https://github.com/nodejs/node/pull/50027)
* \[[`61b4afb7dd`](https://github.com/nodejs/node/commit/61b4afb7dd)] - **deps**: update undici to 5.26.4 (Node.js GitHub Bot) [#50274](https://github.com/nodejs/node/pull/50274)
* \[[`ea28738336`](https://github.com/nodejs/node/commit/ea28738336)] - **doc**: add loong64 info into platform list (Shi Pujin) [#50086](https://github.com/nodejs/node/pull/50086)
* \[[`00c12b7a20`](https://github.com/nodejs/node/commit/00c12b7a20)] - **doc**: update release process LTS step (Richard Lau) [#50299](https://github.com/nodejs/node/pull/50299)
* \[[`a9ba29ba10`](https://github.com/nodejs/node/commit/a9ba29ba10)] - **doc**: fix release process table of contents (Richard Lau) [#50216](https://github.com/nodejs/node/pull/50216)
* \[[`4b5033519e`](https://github.com/nodejs/node/commit/4b5033519e)] - **doc**: update api `stream.compose` (Alex Yang) [#50206](https://github.com/nodejs/node/pull/50206)
* \[[`d4659e2080`](https://github.com/nodejs/node/commit/d4659e2080)] - **doc**: add ReflectConstruct to known perf issues (Vinicius Lourenço) [#50111](https://github.com/nodejs/node/pull/50111)
* \[[`ffa94612fd`](https://github.com/nodejs/node/commit/ffa94612fd)] - **doc**: fix typo in dgram docs (Peter Johnson) [#50211](https://github.com/nodejs/node/pull/50211)
* \[[`f37b577b14`](https://github.com/nodejs/node/commit/f37b577b14)] - **doc**: fix H4ad collaborator sort (Vinicius Lourenço) [#50218](https://github.com/nodejs/node/pull/50218)
* \[[`c75264b1f9`](https://github.com/nodejs/node/commit/c75264b1f9)] - **doc**: add H4ad to collaborators (Vinícius Lourenço) [#50217](https://github.com/nodejs/node/pull/50217)
* \[[`5025e24ac7`](https://github.com/nodejs/node/commit/5025e24ac7)] - **doc**: update release-stewards with last sec-release (Rafael Gonzaga) [#50179](https://github.com/nodejs/node/pull/50179)
* \[[`63379313d5`](https://github.com/nodejs/node/commit/63379313d5)] - **doc**: add command to keep major branch sync (Rafael Gonzaga) [#50102](https://github.com/nodejs/node/pull/50102)
* \[[`85de4b8254`](https://github.com/nodejs/node/commit/85de4b8254)] - **doc**: add loong64 to list of architectures (Shi Pujin) [#50172](https://github.com/nodejs/node/pull/50172)
* \[[`ff8e1b860e`](https://github.com/nodejs/node/commit/ff8e1b860e)] - **doc**: update security release process (Michael Dawson) [#50166](https://github.com/nodejs/node/pull/50166)
* \[[`33470d965c`](https://github.com/nodejs/node/commit/33470d965c)] - **doc**: improve ccache explanation (Chengzhong Wu) [#50133](https://github.com/nodejs/node/pull/50133)
* \[[`7b97c44e2a`](https://github.com/nodejs/node/commit/7b97c44e2a)] - **doc**: move danielleadams to TSC non-voting member (Danielle Adams) [#50142](https://github.com/nodejs/node/pull/50142)
* \[[`3d03ca9f31`](https://github.com/nodejs/node/commit/3d03ca9f31)] - **doc**: fix description of `fs.readdir` `recursive` option (RamdohokarAngha) [#48902](https://github.com/nodejs/node/pull/48902)
* \[[`aab045ec4b`](https://github.com/nodejs/node/commit/aab045ec4b)] - **doc**: mention files read before env setup (Rafael Gonzaga) [#50072](https://github.com/nodejs/node/pull/50072)
* \[[`26a7608a24`](https://github.com/nodejs/node/commit/26a7608a24)] - **doc**: move permission model to Active Development (Rafael Gonzaga) [#50068](https://github.com/nodejs/node/pull/50068)
* \[[`d7bbf7f2c4`](https://github.com/nodejs/node/commit/d7bbf7f2c4)] - **doc**: add command to get patch minors and majors (Rafael Gonzaga) [#50067](https://github.com/nodejs/node/pull/50067)
* \[[`9830165e34`](https://github.com/nodejs/node/commit/9830165e34)] - **doc**: use precise promise terminology in fs (Benjamin Gruenbaum) [#50029](https://github.com/nodejs/node/pull/50029)
* \[[`585cbb211d`](https://github.com/nodejs/node/commit/585cbb211d)] - **doc**: use precise terminology in test runner (Benjamin Gruenbaum) [#50028](https://github.com/nodejs/node/pull/50028)
* \[[`2862f07124`](https://github.com/nodejs/node/commit/2862f07124)] - **doc**: clarify explaination text on how to run the example (Anshul Sinha) [#39020](https://github.com/nodejs/node/pull/39020)
* \[[`fe47c8ad91`](https://github.com/nodejs/node/commit/fe47c8ad91)] - **doc**: reserve 119 for Electron 28 (David Sanders) [#50020](https://github.com/nodejs/node/pull/50020)
* \[[`36ecd2c588`](https://github.com/nodejs/node/commit/36ecd2c588)] - **doc**: update Collaborator pronouns (Tierney Cyren) [#50005](https://github.com/nodejs/node/pull/50005)
* \[[`315d82a73e`](https://github.com/nodejs/node/commit/315d82a73e)] - **doc**: update link to Abstract Modules Records spec (Rich Trott) [#49961](https://github.com/nodejs/node/pull/49961)
* \[[`f63a92bb6c`](https://github.com/nodejs/node/commit/f63a92bb6c)] - **doc**: updated building docs for windows (Claudio W) [#49767](https://github.com/nodejs/node/pull/49767)
* \[[`ad17126501`](https://github.com/nodejs/node/commit/ad17126501)] - **doc**: update CHANGELOG\_V20 about vm fixes (Joyee Cheung) [#49951](https://github.com/nodejs/node/pull/49951)
* \[[`24458e2ac3`](https://github.com/nodejs/node/commit/24458e2ac3)] - **doc**: document dangerous symlink behavior (Tobias Nießen) [#49154](https://github.com/nodejs/node/pull/49154)
* \[[`337a676d1f`](https://github.com/nodejs/node/commit/337a676d1f)] - **doc**: add main ARIA landmark to API docs (Rich Trott) [#49882](https://github.com/nodejs/node/pull/49882)
* \[[`959ef7ac6b`](https://github.com/nodejs/node/commit/959ef7ac6b)] - **doc**: add navigation ARIA landmark to doc ToC (Rich Trott) [#49882](https://github.com/nodejs/node/pull/49882)
* \[[`a60fbf2ab3`](https://github.com/nodejs/node/commit/a60fbf2ab3)] - **doc**: mark Node.js 19 as End-of-Life (Richard Lau) [#48283](https://github.com/nodejs/node/pull/48283)
* \[[`c255575699`](https://github.com/nodejs/node/commit/c255575699)] - **errors**: improve performance of determine-specific-type (Aras Abbasi) [#49696](https://github.com/nodejs/node/pull/49696)
* \[[`e66991e6b2`](https://github.com/nodejs/node/commit/e66991e6b2)] - **errors**: improve formatList in errors.js (Aras Abbasi) [#49642](https://github.com/nodejs/node/pull/49642)
* \[[`c71e548b65`](https://github.com/nodejs/node/commit/c71e548b65)] - **errors**: improve performance of instantiation (Aras Abbasi) [#49654](https://github.com/nodejs/node/pull/49654)
* \[[`3b867e4256`](https://github.com/nodejs/node/commit/3b867e4256)] - **esm**: do not give wrong hints when detecting file format (Antoine du Hamel) [#50314](https://github.com/nodejs/node/pull/50314)
* \[[`a589a1a905`](https://github.com/nodejs/node/commit/a589a1a905)] - **(SEMVER-MINOR)** **esm**: detect ESM syntax in ambiguous JavaScript (Geoffrey Booth) [#50096](https://github.com/nodejs/node/pull/50096)
* \[[`c64490e9aa`](https://github.com/nodejs/node/commit/c64490e9aa)] - **esm**: improve check for ESM syntax (Geoffrey Booth) [#50127](https://github.com/nodejs/node/pull/50127)
* \[[`ee65e44c31`](https://github.com/nodejs/node/commit/ee65e44c31)] - **esm**: use import attributes instead of import assertions (Antoine du Hamel) [#50140](https://github.com/nodejs/node/pull/50140)
* \[[`4de838fdeb`](https://github.com/nodejs/node/commit/4de838fdeb)] - **esm**: bypass CommonJS loader under --default-type (Geoffrey Booth) [#49986](https://github.com/nodejs/node/pull/49986)
* \[[`27e02b633d`](https://github.com/nodejs/node/commit/27e02b633d)] - **esm**: unflag extensionless javascript and wasm in module scope (Geoffrey Booth) [#49974](https://github.com/nodejs/node/pull/49974)
* \[[`1e762ddf63`](https://github.com/nodejs/node/commit/1e762ddf63)] - **esm**: improve `getFormatOfExtensionlessFile` speed (Yagiz Nizipli) [#49965](https://github.com/nodejs/node/pull/49965)
* \[[`112cc7f9f2`](https://github.com/nodejs/node/commit/112cc7f9f2)] - **esm**: improve JSDoc annotation of internal functions (Antoine du Hamel) [#49959](https://github.com/nodejs/node/pull/49959)
* \[[`c48cd84188`](https://github.com/nodejs/node/commit/c48cd84188)] - **esm**: fix cache collision on JSON files using file: URL (Antoine du Hamel) [#49887](https://github.com/nodejs/node/pull/49887)
* \[[`dc80ccef25`](https://github.com/nodejs/node/commit/dc80ccef25)] - **esm**: --experimental-default-type flag to flip module defaults (Geoffrey Booth) [#49869](https://github.com/nodejs/node/pull/49869)
* \[[`01039795a2`](https://github.com/nodejs/node/commit/01039795a2)] - **esm**: require braces for modules code (Geoffrey Booth) [#49657](https://github.com/nodejs/node/pull/49657)
* \[[`e49ebf8f9a`](https://github.com/nodejs/node/commit/e49ebf8f9a)] - **fs**: improve error performance for `readSync` (Jungku Lee) [#50033](https://github.com/nodejs/node/pull/50033)
* \[[`eb33f70260`](https://github.com/nodejs/node/commit/eb33f70260)] - **fs**: improve error performance for `fsyncSync` (Jungku Lee) [#49880](https://github.com/nodejs/node/pull/49880)
* \[[`8d0edc6399`](https://github.com/nodejs/node/commit/8d0edc6399)] - **fs**: improve error performance for `mkdirSync` (CanadaHonk) [#49847](https://github.com/nodejs/node/pull/49847)
* \[[`36df27509e`](https://github.com/nodejs/node/commit/36df27509e)] - **fs**: improve error performance of `realpathSync` (Yagiz Nizipli) [#49962](https://github.com/nodejs/node/pull/49962)
* \[[`4242cb7d7f`](https://github.com/nodejs/node/commit/4242cb7d7f)] - **fs**: improve error performance of `lchownSync` (Yagiz Nizipli) [#49962](https://github.com/nodejs/node/pull/49962)
* \[[`89e7878e44`](https://github.com/nodejs/node/commit/89e7878e44)] - **fs**: improve error performance of `symlinkSync` (Yagiz Nizipli) [#49962](https://github.com/nodejs/node/pull/49962)
* \[[`af6a0611fe`](https://github.com/nodejs/node/commit/af6a0611fe)] - **fs**: improve error performance of `readlinkSync` (Yagiz Nizipli) [#49962](https://github.com/nodejs/node/pull/49962)
* \[[`12cda31c52`](https://github.com/nodejs/node/commit/12cda31c52)] - **fs**: improve error performance of `mkdtempSync` (Yagiz Nizipli) [#49962](https://github.com/nodejs/node/pull/49962)
* \[[`9dba771acb`](https://github.com/nodejs/node/commit/9dba771acb)] - **fs**: improve error performance of `linkSync` (Yagiz Nizipli) [#49962](https://github.com/nodejs/node/pull/49962)
* \[[`ea7902de13`](https://github.com/nodejs/node/commit/ea7902de13)] - **fs**: improve error performance of `chownSync` (Yagiz Nizipli) [#49962](https://github.com/nodejs/node/pull/49962)
* \[[`39f31a38cf`](https://github.com/nodejs/node/commit/39f31a38cf)] - **fs**: improve error performance of `renameSync` (Yagiz Nizipli) [#49962](https://github.com/nodejs/node/pull/49962)
* \[[`35164fa466`](https://github.com/nodejs/node/commit/35164fa466)] - **(SEMVER-MINOR)** **fs**: add flush option to appendFile() functions (Colin Ihrig) [#50095](https://github.com/nodejs/node/pull/50095)
* \[[`06aa4b9fe9`](https://github.com/nodejs/node/commit/06aa4b9fe9)] - **fs**: improve error performance of `readdirSync` (Yagiz Nizipli) [#50131](https://github.com/nodejs/node/pull/50131)
* \[[`b5aecebcd6`](https://github.com/nodejs/node/commit/b5aecebcd6)] - **fs**: fix `unlinkSync` typings (Yagiz Nizipli) [#49859](https://github.com/nodejs/node/pull/49859)
* \[[`6ddea07225`](https://github.com/nodejs/node/commit/6ddea07225)] - **fs**: improve error perf of sync `chmod`+`fchmod` (CanadaHonk) [#49859](https://github.com/nodejs/node/pull/49859)
* \[[`841367078e`](https://github.com/nodejs/node/commit/841367078e)] - **fs**: improve error perf of sync `*times` (CanadaHonk) [#49864](https://github.com/nodejs/node/pull/49864)
* \[[`eb52f73e3e`](https://github.com/nodejs/node/commit/eb52f73e3e)] - **fs**: improve error performance of writevSync (IlyasShabi) [#50038](https://github.com/nodejs/node/pull/50038)
* \[[`d1aa62f1f5`](https://github.com/nodejs/node/commit/d1aa62f1f5)] - **fs**: add flush option to createWriteStream() (Colin Ihrig) [#50093](https://github.com/nodejs/node/pull/50093)
* \[[`57eb06edff`](https://github.com/nodejs/node/commit/57eb06edff)] - **fs**: improve error performance for `ftruncateSync` (André Alves) [#50032](https://github.com/nodejs/node/pull/50032)
* \[[`22e3eb659a`](https://github.com/nodejs/node/commit/22e3eb659a)] - **fs**: add flush option to writeFile() functions (Colin Ihrig) [#50009](https://github.com/nodejs/node/pull/50009)
* \[[`d7132d9214`](https://github.com/nodejs/node/commit/d7132d9214)] - **fs**: improve error performance for `fdatasyncSync` (Jungku Lee) [#49898](https://github.com/nodejs/node/pull/49898)
* \[[`bc2c0410d3`](https://github.com/nodejs/node/commit/bc2c0410d3)] - **fs**: throw errors from sync branches instead of separate implementations (Joyee Cheung) [#49913](https://github.com/nodejs/node/pull/49913)
* \[[`f46bcf1749`](https://github.com/nodejs/node/commit/f46bcf1749)] - **http**: refactor to make servername option normalization testable (Rongjian Zhang) [#38733](https://github.com/nodejs/node/pull/38733)
* \[[`1bfcf817af`](https://github.com/nodejs/node/commit/1bfcf817af)] - **http2**: allow streams to complete gracefully after goaway (Michael Lumish) [#50202](https://github.com/nodejs/node/pull/50202)
* \[[`5c66ec9e66`](https://github.com/nodejs/node/commit/5c66ec9e66)] - **inspector**: simplify dispatchProtocolMessage (Daniel Lemire) [#49780](https://github.com/nodejs/node/pull/49780)
* \[[`251ae1dd72`](https://github.com/nodejs/node/commit/251ae1dd72)] - **lib**: improve performance of validateStringArray and validateBooleanArray (Aras Abbasi) [#49756](https://github.com/nodejs/node/pull/49756)
* \[[`d9c791a508`](https://github.com/nodejs/node/commit/d9c791a508)] - **lib**: fix compileFunction throws range error for negative numbers (Jithil P Ponnan) [#49855](https://github.com/nodejs/node/pull/49855)
* \[[`24cbc550c2`](https://github.com/nodejs/node/commit/24cbc550c2)] - **lib**: reduce overhead of validateObject (Vinicius Lourenço) [#49928](https://github.com/nodejs/node/pull/49928)
* \[[`b80e9497f3`](https://github.com/nodejs/node/commit/b80e9497f3)] - **lib**: make fetch sync and return a Promise (Matthew Aitken) [#49936](https://github.com/nodejs/node/pull/49936)
* \[[`d9eda6761b`](https://github.com/nodejs/node/commit/d9eda6761b)] - **lib**: fix `primordials` typings (Sam Verschueren) [#49895](https://github.com/nodejs/node/pull/49895)
* \[[`3e0d47c1f4`](https://github.com/nodejs/node/commit/3e0d47c1f4)] - **lib**: update params in jsdoc for `HTTPRequestOptions` (Jungku Lee) [#49872](https://github.com/nodejs/node/pull/49872)
* \[[`a01050dec4`](https://github.com/nodejs/node/commit/a01050dec4)] - **(SEMVER-MINOR)** **lib**: add WebSocket client (Matthew Aitken) [#49830](https://github.com/nodejs/node/pull/49830)
* \[[`5bca8feed2`](https://github.com/nodejs/node/commit/5bca8feed2)] - **lib,test**: do not hardcode Buffer.kMaxLength (Michaël Zasso) [#49876](https://github.com/nodejs/node/pull/49876)
* \[[`e8ebed7a24`](https://github.com/nodejs/node/commit/e8ebed7a24)] - **meta**: move Trott to TSC regular member (Rich Trott) [#50297](https://github.com/nodejs/node/pull/50297)
* \[[`27e957cea8`](https://github.com/nodejs/node/commit/27e957cea8)] - **meta**: ping TSC for offboarding (Tobias Nießen) [#50147](https://github.com/nodejs/node/pull/50147)
* \[[`fab39062d5`](https://github.com/nodejs/node/commit/fab39062d5)] - **meta**: bump actions/upload-artifact from 3.1.2 to 3.1.3 (dependabot\[bot]) [#50000](https://github.com/nodejs/node/pull/50000)
* \[[`46ec82496c`](https://github.com/nodejs/node/commit/46ec82496c)] - **meta**: bump actions/cache from 3.3.1 to 3.3.2 (dependabot\[bot]) [#50003](https://github.com/nodejs/node/pull/50003)
* \[[`a634fb431e`](https://github.com/nodejs/node/commit/a634fb431e)] - **meta**: bump github/codeql-action from 2.21.5 to 2.21.9 (dependabot\[bot]) [#50002](https://github.com/nodejs/node/pull/50002)
* \[[`c221f72911`](https://github.com/nodejs/node/commit/c221f72911)] - **meta**: bump actions/checkout from 3.6.0 to 4.1.0 (dependabot\[bot]) [#50001](https://github.com/nodejs/node/pull/50001)
* \[[`d356e5e395`](https://github.com/nodejs/node/commit/d356e5e395)] - **meta**: update website team with new name (Rich Trott) [#49883](https://github.com/nodejs/node/pull/49883)
* \[[`2ff4e71452`](https://github.com/nodejs/node/commit/2ff4e71452)] - **module**: move helpers out of cjs loader (Geoffrey Booth) [#49912](https://github.com/nodejs/node/pull/49912)
* \[[`142ac3f82d`](https://github.com/nodejs/node/commit/142ac3f82d)] - **module, esm**: jsdoc for modules files (Geoffrey Booth) [#49523](https://github.com/nodejs/node/pull/49523)
* \[[`e2f0ef2a60`](https://github.com/nodejs/node/commit/e2f0ef2a60)] - **node-api**: update headers for better wasm support (Toyo Li) [#49037](https://github.com/nodejs/node/pull/49037)
* \[[`db2a07fcd6`](https://github.com/nodejs/node/commit/db2a07fcd6)] - **node-api**: run finalizers directly from GC (Vladimir Morozov) [#42651](https://github.com/nodejs/node/pull/42651)
* \[[`c25716be8b`](https://github.com/nodejs/node/commit/c25716be8b)] - **os**: cache homedir, remove getCheckedFunction (Aras Abbasi) [#50037](https://github.com/nodejs/node/pull/50037)
* \[[`e8f024b4db`](https://github.com/nodejs/node/commit/e8f024b4db)] - **perf\_hooks**: reduce overhead of new user timings (Vinicius Lourenço) [#49914](https://github.com/nodejs/node/pull/49914)
* \[[`a517be0a5a`](https://github.com/nodejs/node/commit/a517be0a5a)] - **perf\_hooks**: reducing overhead of performance observer entry list (Vinicius Lourenço) [#50008](https://github.com/nodejs/node/pull/50008)
* \[[`42e49ec381`](https://github.com/nodejs/node/commit/42e49ec381)] - **perf\_hooks**: reduce overhead of new resource timings (Vinicius Lourenço) [#49837](https://github.com/nodejs/node/pull/49837)
* \[[`c99e51ed1b`](https://github.com/nodejs/node/commit/c99e51ed1b)] - **src**: generate default snapshot with --predictable (Joyee Cheung) [#48749](https://github.com/nodejs/node/pull/48749)
* \[[`47164e238f`](https://github.com/nodejs/node/commit/47164e238f)] - **src**: fix TLSWrap lifetime bug in ALPN callback (Ben Noordhuis) [#49635](https://github.com/nodejs/node/pull/49635)
* \[[`e1df69e73e`](https://github.com/nodejs/node/commit/e1df69e73e)] - **src**: set port in node\_options to uint16\_t (Yagiz Nizipli) [#49151](https://github.com/nodejs/node/pull/49151)
* \[[`1eb2af29b4`](https://github.com/nodejs/node/commit/1eb2af29b4)] - **src**: name scoped lock (Mohammed Keyvanzadeh) [#50010](https://github.com/nodejs/node/pull/50010)
* \[[`5131fde655`](https://github.com/nodejs/node/commit/5131fde655)] - **src**: use exact return value for `uv_os_getenv` (Yagiz Nizipli) [#49149](https://github.com/nodejs/node/pull/49149)
* \[[`ba169be5ca`](https://github.com/nodejs/node/commit/ba169be5ca)] - **src**: move const variable in `node_file.h` to `node_file.cc` (Jungku Lee) [#49688](https://github.com/nodejs/node/pull/49688)
* \[[`5a2351d3ab`](https://github.com/nodejs/node/commit/5a2351d3ab)] - **src**: remove unused variable (Michaël Zasso) [#49665](https://github.com/nodejs/node/pull/49665)
* \[[`f2f993a32f`](https://github.com/nodejs/node/commit/f2f993a32f)] - **stream**: simplify prefinish (Robert Nagy) [#50204](https://github.com/nodejs/node/pull/50204)
* \[[`6d7274e3ca`](https://github.com/nodejs/node/commit/6d7274e3ca)] - **stream**: reduce scope of readable bitmap details (Robert Nagy) [#49963](https://github.com/nodejs/node/pull/49963)
* \[[`ffdc357167`](https://github.com/nodejs/node/commit/ffdc357167)] - **(SEMVER-MINOR)** **stream**: allow pass stream class to `stream.compose` (Alex Yang) [#50187](https://github.com/nodejs/node/pull/50187)
* \[[`4861ad6431`](https://github.com/nodejs/node/commit/4861ad6431)] - **stream**: call helper function from push and unshift (Raz Luvaton) [#50173](https://github.com/nodejs/node/pull/50173)
* \[[`e60b3ab31b`](https://github.com/nodejs/node/commit/e60b3ab31b)] - **stream**: use private symbol for bitmap state (Robert Nagy) [#49993](https://github.com/nodejs/node/pull/49993)
* \[[`ecbfb23f6b`](https://github.com/nodejs/node/commit/ecbfb23f6b)] - **stream**: lazy allocate back pressure buffer (Robert Nagy) [#50013](https://github.com/nodejs/node/pull/50013)
* \[[`88c739bef4`](https://github.com/nodejs/node/commit/88c739bef4)] - **stream**: avoid unnecessary drain for sync stream (Robert Nagy) [#50014](https://github.com/nodejs/node/pull/50014)
* \[[`4b27087b30`](https://github.com/nodejs/node/commit/4b27087b30)] - **stream**: optimize Writable (Robert Nagy) [#50012](https://github.com/nodejs/node/pull/50012)
* \[[`def55f80a1`](https://github.com/nodejs/node/commit/def55f80a1)] - **stream**: avoid tick in writable hot path (Robert Nagy) [#49966](https://github.com/nodejs/node/pull/49966)
* \[[`35ec93115d`](https://github.com/nodejs/node/commit/35ec93115d)] - **stream**: writable state bitmap (Robert Nagy) [#49899](https://github.com/nodejs/node/pull/49899)
* \[[`6e0f0fafe4`](https://github.com/nodejs/node/commit/6e0f0fafe4)] - **test**: use ppc and ppc64 to skip SEA tests on PowerPC (Joyee Cheung) [#50828](https://github.com/nodejs/node/pull/50828)
* \[[`a528bbceca`](https://github.com/nodejs/node/commit/a528bbceca)] - **test**: mark SEA tests as flaky on PowerPC (Joyee Cheung) [#50750](https://github.com/nodejs/node/pull/50750)
* \[[`4e34f9a26e`](https://github.com/nodejs/node/commit/4e34f9a26e)] - **test**: relax version check with shared OpenSSL (Luigi Pinca) [#50505](https://github.com/nodejs/node/pull/50505)
* \[[`41ca1132eb`](https://github.com/nodejs/node/commit/41ca1132eb)] - **test**: fix crypto-dh error message for OpenSSL 3.x (Kerem Kat) [#50395](https://github.com/nodejs/node/pull/50395)
* \[[`a6a05e8a88`](https://github.com/nodejs/node/commit/a6a05e8a88)] - **test**: fix testsuite against zlib version 1.3 (Dominique Leuenberger) [#50364](https://github.com/nodejs/node/pull/50364)
* \[[`8dd895e574`](https://github.com/nodejs/node/commit/8dd895e574)] - **test**: replace forEach with for..of in test-process-env (Niya Shiyas) [#49825](https://github.com/nodejs/node/pull/49825)
* \[[`81886c66d1`](https://github.com/nodejs/node/commit/81886c66d1)] - **test**: replace forEach with for..of in test-http-url (Niya Shiyas) [#49840](https://github.com/nodejs/node/pull/49840)
* \[[`7d8a18b257`](https://github.com/nodejs/node/commit/7d8a18b257)] - **test**: improve watch mode test (Moshe Atlow) [#50319](https://github.com/nodejs/node/pull/50319)
* \[[`baa04b79ca`](https://github.com/nodejs/node/commit/baa04b79ca)] - **test**: set `test-watch-mode-inspect` as flaky (Yagiz Nizipli) [#50259](https://github.com/nodejs/node/pull/50259)
* \[[`3d9130bc2e`](https://github.com/nodejs/node/commit/3d9130bc2e)] - _**Revert**_ "**test**: set `test-esm-loader-resolve-type` as flaky" (Antoine du Hamel) [#50315](https://github.com/nodejs/node/pull/50315)
* \[[`72626f9a35`](https://github.com/nodejs/node/commit/72626f9a35)] - **test**: replace forEach with for..of in test-http-perf\_hooks.js (Niya Shiyas) [#49818](https://github.com/nodejs/node/pull/49818)
* \[[`379a7255e8`](https://github.com/nodejs/node/commit/379a7255e8)] - **test**: replace forEach with for..of in test-net-isipv4.js (Niya Shiyas) [#49822](https://github.com/nodejs/node/pull/49822)
* \[[`b55fcd75da`](https://github.com/nodejs/node/commit/b55fcd75da)] - **test**: deflake `test-esm-loader-resolve-type` (Antoine du Hamel) [#50273](https://github.com/nodejs/node/pull/50273)
* \[[`0134af3eeb`](https://github.com/nodejs/node/commit/0134af3eeb)] - **test**: replace forEach with for..of in test-http2-server (Niya Shiyas) [#49819](https://github.com/nodejs/node/pull/49819)
* \[[`8c15281d06`](https://github.com/nodejs/node/commit/8c15281d06)] - **test**: replace forEach with for..of in test-http2-client-destroy.js (Niya Shiyas) [#49820](https://github.com/nodejs/node/pull/49820)
* \[[`c37a75a898`](https://github.com/nodejs/node/commit/c37a75a898)] - **test**: update `url` web platform tests (Yagiz Nizipli) [#50264](https://github.com/nodejs/node/pull/50264)
* \[[`ab5985d0e9`](https://github.com/nodejs/node/commit/ab5985d0e9)] - **test**: set `test-emit-after-on-destroyed` as flaky (Yagiz Nizipli) [#50246](https://github.com/nodejs/node/pull/50246)
* \[[`50181a19b8`](https://github.com/nodejs/node/commit/50181a19b8)] - **test**: set inspector async stack test as flaky (Yagiz Nizipli) [#50244](https://github.com/nodejs/node/pull/50244)
* \[[`b9e0fed995`](https://github.com/nodejs/node/commit/b9e0fed995)] - **test**: set test-worker-nearheaplimit-deadlock flaky (StefanStojanovic) [#50277](https://github.com/nodejs/node/pull/50277)
* \[[`2cfc4007d1`](https://github.com/nodejs/node/commit/2cfc4007d1)] - **test**: set `test-cli-node-options` as flaky (Yagiz Nizipli) [#50296](https://github.com/nodejs/node/pull/50296)
* \[[`788714b28f`](https://github.com/nodejs/node/commit/788714b28f)] - **test**: reduce the number of requests and parsers (Luigi Pinca) [#50240](https://github.com/nodejs/node/pull/50240)
* \[[`0dce19c8f6`](https://github.com/nodejs/node/commit/0dce19c8f6)] - **test**: set crypto-timing test as flaky (Yagiz Nizipli) [#50232](https://github.com/nodejs/node/pull/50232)
* \[[`5d4b5ff1b8`](https://github.com/nodejs/node/commit/5d4b5ff1b8)] - **test**: set `test-structuredclone-*` as flaky (Yagiz Nizipli) [#50261](https://github.com/nodejs/node/pull/50261)
* \[[`5c56081d67`](https://github.com/nodejs/node/commit/5c56081d67)] - **test**: deflake `test-loaders-workers-spawned` (Antoine du Hamel) [#50251](https://github.com/nodejs/node/pull/50251)
* \[[`3441e1982d`](https://github.com/nodejs/node/commit/3441e1982d)] - **test**: improve code coverage of diagnostics\_channel (Jithil P Ponnan) [#50053](https://github.com/nodejs/node/pull/50053)
* \[[`696ba93329`](https://github.com/nodejs/node/commit/696ba93329)] - **test**: set `test-esm-loader-resolve-type` as flaky (Yagiz Nizipli) [#50226](https://github.com/nodejs/node/pull/50226)
* \[[`8b260c5d6b`](https://github.com/nodejs/node/commit/8b260c5d6b)] - **test**: set inspector async hook test as flaky (Yagiz Nizipli) [#50252](https://github.com/nodejs/node/pull/50252)
* \[[`f3296d25e8`](https://github.com/nodejs/node/commit/f3296d25e8)] - **test**: skip test-benchmark-os.js on IBM i (Abdirahim Musse) [#50208](https://github.com/nodejs/node/pull/50208)
* \[[`fefe17b02e`](https://github.com/nodejs/node/commit/fefe17b02e)] - **test**: set parallel http server test as flaky (Yagiz Nizipli) [#50227](https://github.com/nodejs/node/pull/50227)
* \[[`228c87f329`](https://github.com/nodejs/node/commit/228c87f329)] - **test**: set test-worker-nearheaplimit-deadlock flaky (Stefan Stojanovic) [#50238](https://github.com/nodejs/node/pull/50238)
* \[[`c2c2506eab`](https://github.com/nodejs/node/commit/c2c2506eab)] - **test**: set `test-runner-watch-mode` as flaky (Yagiz Nizipli) [#50221](https://github.com/nodejs/node/pull/50221)
* \[[`16a07983d4`](https://github.com/nodejs/node/commit/16a07983d4)] - **test**: set sea snapshot tests as flaky (Yagiz Nizipli) [#50223](https://github.com/nodejs/node/pull/50223)
* \[[`7cd406a0b8`](https://github.com/nodejs/node/commit/7cd406a0b8)] - **test**: fix defect path traversal tests (Tobias Nießen) [#50124](https://github.com/nodejs/node/pull/50124)
* \[[`1cf3f8da32`](https://github.com/nodejs/node/commit/1cf3f8da32)] - **test**: replace forEach with for..of in test-net-isipv6.js (Niya Shiyas) [#49823](https://github.com/nodejs/node/pull/49823)
* \[[`214997a99e`](https://github.com/nodejs/node/commit/214997a99e)] - **test**: reduce number of repetition in test-heapdump-shadowrealm.js (Chengzhong Wu) [#50104](https://github.com/nodejs/node/pull/50104)
* \[[`9d836516e6`](https://github.com/nodejs/node/commit/9d836516e6)] - **test**: replace forEach with for..of in test-parse-args.mjs (Niya Shiyas) [#49824](https://github.com/nodejs/node/pull/49824)
* \[[`fee8b24603`](https://github.com/nodejs/node/commit/fee8b24603)] - **test**: replace forEach() in test-net-perf\_hooks with for of (Narcisa Codreanu) [#49831](https://github.com/nodejs/node/pull/49831)
* \[[`4c58b92ba8`](https://github.com/nodejs/node/commit/4c58b92ba8)] - **test**: change forEach to for...of (Tiffany Lastimosa) [#49799](https://github.com/nodejs/node/pull/49799)
* \[[`01b01527d7`](https://github.com/nodejs/node/commit/01b01527d7)] - **test**: update skip for moved `test-wasm-web-api` (Richard Lau) [#49958](https://github.com/nodejs/node/pull/49958)
* \[[`179e293103`](https://github.com/nodejs/node/commit/179e293103)] - _**Revert**_ "**test**: mark test-runner-output as flaky" (Luigi Pinca) [#49905](https://github.com/nodejs/node/pull/49905)
* \[[`829eb99afd`](https://github.com/nodejs/node/commit/829eb99afd)] - **test**: disambiguate AIX and IBM i (Richard Lau) [#48056](https://github.com/nodejs/node/pull/48056)
* \[[`126407d438`](https://github.com/nodejs/node/commit/126407d438)] - **test**: deflake test-perf-hooks.js (Joyee Cheung) [#49892](https://github.com/nodejs/node/pull/49892)
* \[[`393fd5b7c9`](https://github.com/nodejs/node/commit/393fd5b7c9)] - **test**: migrate message error tests from Python to JS (Yiyun Lei) [#49721](https://github.com/nodejs/node/pull/49721)
* \[[`5dfe4236f8`](https://github.com/nodejs/node/commit/5dfe4236f8)] - **test**: fix edge snapshot stack traces (Geoffrey Booth) [#49659](https://github.com/nodejs/node/pull/49659)
* \[[`d164e537bf`](https://github.com/nodejs/node/commit/d164e537bf)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#50039](https://github.com/nodejs/node/pull/50039)
* \[[`55a03fedae`](https://github.com/nodejs/node/commit/55a03fedae)] - **test\_runner**: add test location for FileTests (Colin Ihrig) [#49999](https://github.com/nodejs/node/pull/49999)
* \[[`10b35cfb6e`](https://github.com/nodejs/node/commit/10b35cfb6e)] - **test\_runner**: replace spurious if with else (Colin Ihrig) [#49943](https://github.com/nodejs/node/pull/49943)
* \[[`27558c4314`](https://github.com/nodejs/node/commit/27558c4314)] - **test\_runner**: catch reporter errors (Moshe Atlow) [#49646](https://github.com/nodejs/node/pull/49646)
* \[[`709c6c0cab`](https://github.com/nodejs/node/commit/709c6c0cab)] - **(SEMVER-MINOR)** **test\_runner, cli**: add --test-concurrency flag (Colin Ihrig) [#49996](https://github.com/nodejs/node/pull/49996)
* \[[`64ef108dd9`](https://github.com/nodejs/node/commit/64ef108dd9)] - **test\_runner,test**: fix flaky test-runner-cli-concurrency.js (Colin Ihrig) [#50108](https://github.com/nodejs/node/pull/50108)
* \[[`d2def152d9`](https://github.com/nodejs/node/commit/d2def152d9)] - **tls**: reduce TLS 'close' event listener warnings (Tim Perry) [#50136](https://github.com/nodejs/node/pull/50136)
* \[[`294b650f5c`](https://github.com/nodejs/node/commit/294b650f5c)] - **tls**: handle cases where the raw socket is destroyed (Luigi Pinca) [#49980](https://github.com/nodejs/node/pull/49980)
* \[[`52b5693949`](https://github.com/nodejs/node/commit/52b5693949)] - **tls**: ciphers allow bang syntax (Chemi Atlow) [#49712](https://github.com/nodejs/node/pull/49712)
* \[[`05ee35028b`](https://github.com/nodejs/node/commit/05ee35028b)] - **tools**: update comment in `update-uncidi.sh` and `acorn_version.h` (Jungku Lee) [#50175](https://github.com/nodejs/node/pull/50175)
* \[[`35b160e6a3`](https://github.com/nodejs/node/commit/35b160e6a3)] - **tools**: refactor checkimports.py (Mohammed Keyvanzadeh) [#50011](https://github.com/nodejs/node/pull/50011)
* \[[`b959d36b77`](https://github.com/nodejs/node/commit/b959d36b77)] - **tools**: fix comments referencing dep\_updaters scripts (Keksonoid) [#50165](https://github.com/nodejs/node/pull/50165)
* \[[`bd5d5331b0`](https://github.com/nodejs/node/commit/bd5d5331b0)] - **tools**: remove no-return-await lint rule (翠 / green) [#50118](https://github.com/nodejs/node/pull/50118)
* \[[`b9adf3d66e`](https://github.com/nodejs/node/commit/b9adf3d66e)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#50083](https://github.com/nodejs/node/pull/50083)
* \[[`4978bdc4ec`](https://github.com/nodejs/node/commit/4978bdc4ec)] - **tools**: update eslint to 8.51.0 (Node.js GitHub Bot) [#50084](https://github.com/nodejs/node/pull/50084)
* \[[`e323a367fd`](https://github.com/nodejs/node/commit/e323a367fd)] - **tools**: remove genv8constants.py (Ben Noordhuis) [#50023](https://github.com/nodejs/node/pull/50023)
* \[[`1cc6cbff26`](https://github.com/nodejs/node/commit/1cc6cbff26)] - **tools**: update eslint to 8.50.0 (Node.js GitHub Bot) [#49989](https://github.com/nodejs/node/pull/49989)
* \[[`924231be2a`](https://github.com/nodejs/node/commit/924231be2a)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#49983](https://github.com/nodejs/node/pull/49983)
* \[[`732b5661ea`](https://github.com/nodejs/node/commit/732b5661ea)] - **tools**: add navigation ARIA landmark to generated API ToC (Rich Trott) [#49882](https://github.com/nodejs/node/pull/49882)
* \[[`353a14278e`](https://github.com/nodejs/node/commit/353a14278e)] - **tools**: update github\_reporter to 1.5.3 (Node.js GitHub Bot) [#49877](https://github.com/nodejs/node/pull/49877)
* \[[`0aaab45d7c`](https://github.com/nodejs/node/commit/0aaab45d7c)] - **tools**: improve macOS notarization process output readability (Ulises Gascón) [#50389](https://github.com/nodejs/node/pull/50389)
* \[[`ad326033e2`](https://github.com/nodejs/node/commit/ad326033e2)] - **tools**: remove unused `version` function (Ulises Gascón) [#50390](https://github.com/nodejs/node/pull/50390)
* \[[`2f32472544`](https://github.com/nodejs/node/commit/2f32472544)] - **tools**: drop support for osx notarization with gon (Ulises Gascón) [#50291](https://github.com/nodejs/node/pull/50291)
* \[[`3b1c15aeb0`](https://github.com/nodejs/node/commit/3b1c15aeb0)] - **tools**: use osx notarytool for future releases (Ulises Gascon) [#48701](https://github.com/nodejs/node/pull/48701)
* \[[`0ddb87ede3`](https://github.com/nodejs/node/commit/0ddb87ede3)] - **typings**: use `Symbol.dispose` and `Symbol.asyncDispose` in types (Niklas Mollenhauer) [#50123](https://github.com/nodejs/node/pull/50123)
* \[[`bf5b2115a0`](https://github.com/nodejs/node/commit/bf5b2115a0)] - **util**: remove internal mime fns from benchmarks (Aras Abbasi) [#50201](https://github.com/nodejs/node/pull/50201)
* \[[`ac02cdb0ad`](https://github.com/nodejs/node/commit/ac02cdb0ad)] - **util**: lazy parse mime parameters (Aras Abbasi) [#49889](https://github.com/nodejs/node/pull/49889)
* \[[`9853fd96df`](https://github.com/nodejs/node/commit/9853fd96df)] - **vm**: reject in importModuleDynamically without --experimental-vm-modules (Joyee Cheung) [#50137](https://github.com/nodejs/node/pull/50137)
* \[[`3697c19c80`](https://github.com/nodejs/node/commit/3697c19c80)] - **vm**: use internal versions of compileFunction and Script (Joyee Cheung) [#50137](https://github.com/nodejs/node/pull/50137)
* \[[`56bbc30a44`](https://github.com/nodejs/node/commit/56bbc30a44)] - **vm**: unify host-defined option generation in vm.compileFunction (Joyee Cheung) [#50137](https://github.com/nodejs/node/pull/50137)
* \[[`57efd5292c`](https://github.com/nodejs/node/commit/57efd5292c)] - **(SEMVER-MINOR)** **vm**: use import attributes instead of import assertions (Antoine du Hamel) [#50141](https://github.com/nodejs/node/pull/50141)
* \[[`17581c2716`](https://github.com/nodejs/node/commit/17581c2716)] - **vm**: use default HDO when importModuleDynamically is not set (Joyee Cheung) [#49950](https://github.com/nodejs/node/pull/49950)
* \[[`65e18aa8e7`](https://github.com/nodejs/node/commit/65e18aa8e7)] - **wasi**: address coverity warning (Michael Dawson) [#49866](https://github.com/nodejs/node/pull/49866)
* \[[`5b695d6a8d`](https://github.com/nodejs/node/commit/5b695d6a8d)] - **wasi**: fix up wasi tests for ibmi (Michael Dawson) [#49953](https://github.com/nodejs/node/pull/49953)
* \[[`b86e1f5cbd`](https://github.com/nodejs/node/commit/b86e1f5cbd)] - **(SEMVER-MINOR)** **wasi**: updates required for latest uvwasi version (Michael Dawson) [#49908](https://github.com/nodejs/node/pull/49908)
* \[[`b4d149b4d6`](https://github.com/nodejs/node/commit/b4d149b4d6)] - **worker**: handle detached `MessagePort` from a different context (Juan José) [#49150](https://github.com/nodejs/node/pull/49150)
* \[[`f564ed4e05`](https://github.com/nodejs/node/commit/f564ed4e05)] - **zlib**: fix discovery of cpu-features.h for android (MatteoBax) [#49828](https://github.com/nodejs/node/pull/49828)

<a id="20.9.0"></a>

## 2023-10-24, Version 20.9.0 'Iron' (LTS), @richardlau

### Notable Changes

This release marks the transition of Node.js 20.x into Long Term Support (LTS)
with the codename 'Iron'. The 20.x release line now moves into "Active LTS"
and will remain so until October 2024. After that time, it will move into
"Maintenance" until end of life in April 2026.

### Known issue

Collecting code coverage via the `NODE_V8_COVERAGE` environment variable may
lead to a hang. This is not thought to be a regression in Node.js 20 (some
reports are on Node.js 18). For more information, including some potential
workarounds, see issue [#49344](https://github.com/nodejs/node/issues/49344).

<a id="20.8.1"></a>

## 2023-10-13, Version 20.8.1 (Current), @RafaelGSS

This is a security release.

### Notable Changes

The following CVEs are fixed in this release:

* [CVE-2023-44487](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-44487): `nghttp2` Security Release (High)
* [CVE-2023-45143](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-45143): `undici` Security Release (High)
* [CVE-2023-39332](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-39332): Path traversal through path stored in Uint8Array (High)
* [CVE-2023-39331](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-39331): Permission model improperly protects against path traversal (High)
* [CVE-2023-38552](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-38552):  Integrity checks according to policies can be circumvented (Medium)
* [CVE-2023-39333](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-39333): Code injection via WebAssembly export names (Low)

More detailed information on each of the vulnerabilities can be found in [October 2023 Security Releases](https://nodejs.org/en/blog/vulnerability/october-2023-security-releases/) blog post.

### Commits

* \[[`c86883e844`](https://github.com/nodejs/node/commit/c86883e844)] - **deps**: update nghttp2 to 1.57.0 (James M Snell) [#50121](https://github.com/nodejs/node/pull/50121)
* \[[`2860631359`](https://github.com/nodejs/node/commit/2860631359)] - **deps**: update undici to v5.26.3 (Matteo Collina) [#50153](https://github.com/nodejs/node/pull/50153)
* \[[`cd37838bf8`](https://github.com/nodejs/node/commit/cd37838bf8)] - **lib**: let deps require `node` prefixed modules (Matthew Aitken) [#50047](https://github.com/nodejs/node/pull/50047)
* \[[`f5c90b2951`](https://github.com/nodejs/node/commit/f5c90b2951)] - **module**: fix code injection through export names (Tobias Nießen) [nodejs-private/node-private#461](https://github.com/nodejs-private/node-private/pull/461)
* \[[`fa5dae1944`](https://github.com/nodejs/node/commit/fa5dae1944)] - **permission**: fix Uint8Array path traversal (Tobias Nießen) [nodejs-private/node-private#456](https://github.com/nodejs-private/node-private/pull/456)
* \[[`cd35275111`](https://github.com/nodejs/node/commit/cd35275111)] - **permission**: improve path traversal protection (Tobias Nießen) [nodejs-private/node-private#456](https://github.com/nodejs-private/node-private/pull/456)
* \[[`a4cb7fc7c0`](https://github.com/nodejs/node/commit/a4cb7fc7c0)] - **policy**: use tamper-proof integrity check function (Tobias Nießen) [nodejs-private/node-private#462](https://github.com/nodejs-private/node-private/pull/462)

<a id="20.8.0"></a>

## 2023-09-28, Version 20.8.0 (Current), @ruyadorno

### Notable Changes

#### Stream performance improvements

Performance improvements to writable and readable streams, improving the creation and destruction by ±15% and reducing the memory overhead each stream takes in Node.js

Contributed by Benjamin Gruenbaum in [#49745](https://github.com/nodejs/node/pull/49745) and Raz Luvaton in [#49834](https://github.com/nodejs/node/pull/49834).

Performance improvements for readable webstream, improving readable stream async iterator consumption by ±140% and improving readable stream `pipeTo` consumption by ±60%

Contributed by Raz Luvaton in [#49662](https://github.com/nodejs/node/pull/49662) and [#49690](https://github.com/nodejs/node/pull/49690).

#### Rework of memory management in `vm` APIs with the `importModuleDynamically` option

This rework addressed a series of long-standing memory leaks and use-after-free issues in the following APIs that support `importModuleDynamically`:

* `vm.Script`
* `vm.compileFunction`
* `vm.SyntheticModule`
* `vm.SourceTextModule`

This should enable affected users to upgrade from older versions of Node.js.

Contributed by Joyee Cheung in [#48510](https://github.com/nodejs/node/pull/48510).

#### Other notable changes

* \[[`32d4d29d02`](https://github.com/nodejs/node/commit/32d4d29d02)] - **deps**: add v8::Object::SetInternalFieldForNodeCore() (Joyee Cheung) [#49874](https://github.com/nodejs/node/pull/49874)
* \[[`0e686d096b`](https://github.com/nodejs/node/commit/0e686d096b)] - **doc**: deprecate `fs.F_OK`, `fs.R_OK`, `fs.W_OK`, `fs.X_OK` (Livia Medeiros) [#49683](https://github.com/nodejs/node/pull/49683)
* \[[`a5dd057540`](https://github.com/nodejs/node/commit/a5dd057540)] - **doc**: deprecate `util.toUSVString` (Yagiz Nizipli) [#49725](https://github.com/nodejs/node/pull/49725)
* \[[`7b6a73172f`](https://github.com/nodejs/node/commit/7b6a73172f)] - **doc**: deprecate calling `promisify` on a function that returns a promise (Antoine du Hamel) [#49647](https://github.com/nodejs/node/pull/49647)
* \[[`1beefd5f16`](https://github.com/nodejs/node/commit/1beefd5f16)] - **esm**: set all hooks as release candidate (Geoffrey Booth) [#49597](https://github.com/nodejs/node/pull/49597)
* \[[`b0ce78a75b`](https://github.com/nodejs/node/commit/b0ce78a75b)] - **module**: fix the leak in SourceTextModule and ContextifySript (Joyee Cheung) [#48510](https://github.com/nodejs/node/pull/48510)
* \[[`4e578f8ab1`](https://github.com/nodejs/node/commit/4e578f8ab1)] - **module**: fix leak of vm.SyntheticModule (Joyee Cheung) [#48510](https://github.com/nodejs/node/pull/48510)
* \[[`69e4218772`](https://github.com/nodejs/node/commit/69e4218772)] - **module**: use symbol in WeakMap to manage host defined options (Joyee Cheung) [#48510](https://github.com/nodejs/node/pull/48510)
* \[[`14ece0aa76`](https://github.com/nodejs/node/commit/14ece0aa76)] - **(SEMVER-MINOR)** **src**: allow embedders to override NODE\_MODULE\_VERSION (Cheng Zhao) [#49279](https://github.com/nodejs/node/pull/49279)
* \[[`9fd67fbff0`](https://github.com/nodejs/node/commit/9fd67fbff0)] - **stream**: use bitmap in writable state (Raz Luvaton) [#49834](https://github.com/nodejs/node/pull/49834)
* \[[`0ccd4638ac`](https://github.com/nodejs/node/commit/0ccd4638ac)] - **stream**: use bitmap in readable state (Benjamin Gruenbaum) [#49745](https://github.com/nodejs/node/pull/49745)
* \[[`7c5e322346`](https://github.com/nodejs/node/commit/7c5e322346)] - **stream**: improve webstream readable async iterator performance (Raz Luvaton) [#49662](https://github.com/nodejs/node/pull/49662)
* \[[`80b342cc38`](https://github.com/nodejs/node/commit/80b342cc38)] - **(SEMVER-MINOR)** **test\_runner**: accept `testOnly` in `run` (Moshe Atlow) [#49753](https://github.com/nodejs/node/pull/49753)
* \[[`17a05b141d`](https://github.com/nodejs/node/commit/17a05b141d)] - **(SEMVER-MINOR)** **test\_runner**: add junit reporter (Moshe Atlow) [#49614](https://github.com/nodejs/node/pull/49614)

### Commits

* \[[`4879e3fbbe`](https://github.com/nodejs/node/commit/4879e3fbbe)] - **benchmark**: add a benchmark for read() of ReadableStreams (Debadree Chatterjee) [#49622](https://github.com/nodejs/node/pull/49622)
* \[[`78a6c73157`](https://github.com/nodejs/node/commit/78a6c73157)] - **benchmark**: shorten pipe-to by reducing number of chunks (Raz Luvaton) [#49577](https://github.com/nodejs/node/pull/49577)
* \[[`4126a6e4c9`](https://github.com/nodejs/node/commit/4126a6e4c9)] - **benchmark**: fix webstream pipe-to (Raz Luvaton) [#49552](https://github.com/nodejs/node/pull/49552)
* \[[`6010a91825`](https://github.com/nodejs/node/commit/6010a91825)] - **bootstrap**: do not expand argv1 for snapshots (Joyee Cheung) [#49506](https://github.com/nodejs/node/pull/49506)
* \[[`8480280c4b`](https://github.com/nodejs/node/commit/8480280c4b)] - **bootstrap**: only use the isolate snapshot when compiling code cache (Joyee Cheung) [#49288](https://github.com/nodejs/node/pull/49288)
* \[[`b30754aa87`](https://github.com/nodejs/node/commit/b30754aa87)] - **build**: run embedtest using node executable (Joyee Cheung) [#49506](https://github.com/nodejs/node/pull/49506)
* \[[`31db0b8e2b`](https://github.com/nodejs/node/commit/31db0b8e2b)] - **build**: add --write-snapshot-as-array-literals to configure.py (Joyee Cheung) [#49312](https://github.com/nodejs/node/pull/49312)
* \[[`6fcb51d3ba`](https://github.com/nodejs/node/commit/6fcb51d3ba)] - **debugger**: use `internal/url.URL` instead of `url.parse` (LiviaMedeiros) [#49590](https://github.com/nodejs/node/pull/49590)
* \[[`32d4d29d02`](https://github.com/nodejs/node/commit/32d4d29d02)] - **deps**: add v8::Object::SetInternalFieldForNodeCore() (Joyee Cheung) [#49874](https://github.com/nodejs/node/pull/49874)
* \[[`ad37cadc3f`](https://github.com/nodejs/node/commit/ad37cadc3f)] - **deps**: V8: backport de9a5de2274f (Joyee Cheung) [#49703](https://github.com/nodejs/node/pull/49703)
* \[[`cdd1c66222`](https://github.com/nodejs/node/commit/cdd1c66222)] - **deps**: V8: cherry-pick b33bf2dfd261 (Joyee Cheung) [#49703](https://github.com/nodejs/node/pull/49703)
* \[[`61d18d6473`](https://github.com/nodejs/node/commit/61d18d6473)] - **deps**: update undici to 5.24.0 (Node.js GitHub Bot) [#49559](https://github.com/nodejs/node/pull/49559)
* \[[`b8a4fef393`](https://github.com/nodejs/node/commit/b8a4fef393)] - **deps**: remove pthread-fixes.c from uv.gyp (Ben Noordhuis) [#49744](https://github.com/nodejs/node/pull/49744)
* \[[`6c86c0683c`](https://github.com/nodejs/node/commit/6c86c0683c)] - **deps**: update googletest to d1467f5 (Node.js GitHub Bot) [#49676](https://github.com/nodejs/node/pull/49676)
* \[[`1424404742`](https://github.com/nodejs/node/commit/1424404742)] - **deps**: update nghttp2 to 1.56.0 (Node.js GitHub Bot) [#49582](https://github.com/nodejs/node/pull/49582)
* \[[`15b54ff95d`](https://github.com/nodejs/node/commit/15b54ff95d)] - **deps**: update googletest to 8a6feab (Node.js GitHub Bot) [#49463](https://github.com/nodejs/node/pull/49463)
* \[[`2ceab877c2`](https://github.com/nodejs/node/commit/2ceab877c2)] - **deps**: update corepack to 0.20.0 (Node.js GitHub Bot) [#49464](https://github.com/nodejs/node/pull/49464)
* \[[`4814872ddc`](https://github.com/nodejs/node/commit/4814872ddc)] - **doc**: fix `DEP0176` number (LiviaMedeiros) [#49858](https://github.com/nodejs/node/pull/49858)
* \[[`0e686d096b`](https://github.com/nodejs/node/commit/0e686d096b)] - **doc**: deprecate `fs.F_OK`, `fs.R_OK`, `fs.W_OK`, `fs.X_OK` (Livia Medeiros) [#49683](https://github.com/nodejs/node/pull/49683)
* \[[`5877c403a2`](https://github.com/nodejs/node/commit/5877c403a2)] - **doc**: add mertcanaltin as a triager (mert.altin) [#49826](https://github.com/nodejs/node/pull/49826)
* \[[`864fe56432`](https://github.com/nodejs/node/commit/864fe56432)] - **doc**: add `git node backport` way to the backporting guide (Raz Luvaton) [#49760](https://github.com/nodejs/node/pull/49760)
* \[[`e0f93492d5`](https://github.com/nodejs/node/commit/e0f93492d5)] - **doc**: improve documentation about ICU data fallback (Joyee Cheung) [#49666](https://github.com/nodejs/node/pull/49666)
* \[[`a5dd057540`](https://github.com/nodejs/node/commit/a5dd057540)] - **doc**: deprecate `util.toUSVString` (Yagiz Nizipli) [#49725](https://github.com/nodejs/node/pull/49725)
* \[[`774c1cfd52`](https://github.com/nodejs/node/commit/774c1cfd52)] - **doc**: add missing function call to example for `util.promisify` (Jungku Lee) [#49719](https://github.com/nodejs/node/pull/49719)
* \[[`fe78a34845`](https://github.com/nodejs/node/commit/fe78a34845)] - **doc**: update output of example in `mimeParams.set()` (Deokjin Kim) [#49718](https://github.com/nodejs/node/pull/49718)
* \[[`4175ea33bd`](https://github.com/nodejs/node/commit/4175ea33bd)] - **doc**: add missed `inspect` with numericSeparator to example (Deokjin Kim) [#49717](https://github.com/nodejs/node/pull/49717)
* \[[`3a88571972`](https://github.com/nodejs/node/commit/3a88571972)] - **doc**: fix history comments (Antoine du Hamel) [#49701](https://github.com/nodejs/node/pull/49701)
* \[[`db4ab1ccbb`](https://github.com/nodejs/node/commit/db4ab1ccbb)] - **doc**: add missing history info for `import.meta.resolve` (Antoine du Hamel) [#49700](https://github.com/nodejs/node/pull/49700)
* \[[`a304d1ee19`](https://github.com/nodejs/node/commit/a304d1ee19)] - **doc**: link maintaining deps to pull-request.md (Marco Ippolito) [#49716](https://github.com/nodejs/node/pull/49716)
* \[[`35294486ad`](https://github.com/nodejs/node/commit/35294486ad)] - **doc**: fix print results in `events` (Jungku Lee) [#49548](https://github.com/nodejs/node/pull/49548)
* \[[`9f0b0e15c9`](https://github.com/nodejs/node/commit/9f0b0e15c9)] - **doc**: alphabetize cli.md sections (Geoffrey Booth) [#49668](https://github.com/nodejs/node/pull/49668)
* \[[`7b6a73172f`](https://github.com/nodejs/node/commit/7b6a73172f)] - **doc**: deprecate calling `promisify` on a function that returns a promise (Antoine du Hamel) [#49647](https://github.com/nodejs/node/pull/49647)
* \[[`d316b32fff`](https://github.com/nodejs/node/commit/d316b32fff)] - **doc**: update `corepack.md` to account for 0.20.0 changes (Antoine du Hamel) [#49486](https://github.com/nodejs/node/pull/49486)
* \[[`c2eac7dc7c`](https://github.com/nodejs/node/commit/c2eac7dc7c)] - **doc**: remove `@anonrig` from performance initiative (Yagiz Nizipli) [#49641](https://github.com/nodejs/node/pull/49641)
* \[[`3d839fbf87`](https://github.com/nodejs/node/commit/3d839fbf87)] - **doc**: mark Node.js 16 as End-of-Life (Richard Lau) [#49651](https://github.com/nodejs/node/pull/49651)
* \[[`53fb5aead8`](https://github.com/nodejs/node/commit/53fb5aead8)] - **doc**: save user preference for JS flavor (Vidar Eldøy) [#49526](https://github.com/nodejs/node/pull/49526)
* \[[`e3594d5658`](https://github.com/nodejs/node/commit/e3594d5658)] - **doc**: update documentation for node:process warning (Shubham Pandey) [#49517](https://github.com/nodejs/node/pull/49517)
* \[[`8e033c3963`](https://github.com/nodejs/node/commit/8e033c3963)] - **doc**: rename possibly confusing variable and CSS class (Antoine du Hamel) [#49536](https://github.com/nodejs/node/pull/49536)
* \[[`d0e0eb4bb3`](https://github.com/nodejs/node/commit/d0e0eb4bb3)] - **doc**: update outdated history info (Antoine du Hamel) [#49530](https://github.com/nodejs/node/pull/49530)
* \[[`b4724e2e3a`](https://github.com/nodejs/node/commit/b4724e2e3a)] - **doc**: close a parenthesis (Sébastien Règne) [#49525](https://github.com/nodejs/node/pull/49525)
* \[[`0471c5798e`](https://github.com/nodejs/node/commit/0471c5798e)] - **doc**: cast GetInternalField() return type to v8::Value in addons.md (Joyee Cheung) [#49439](https://github.com/nodejs/node/pull/49439)
* \[[`9f8bea3dda`](https://github.com/nodejs/node/commit/9f8bea3dda)] - **doc**: fix documentation for input option in child\_process (Ariel Weiss) [#49481](https://github.com/nodejs/node/pull/49481)
* \[[`f3fea92f8a`](https://github.com/nodejs/node/commit/f3fea92f8a)] - **doc**: fix missing imports in `test.run` code examples (Oshri Asulin) [#49489](https://github.com/nodejs/node/pull/49489)
* \[[`e426b77b67`](https://github.com/nodejs/node/commit/e426b77b67)] - **doc**: fix documentation for fs.createWriteStream highWaterMark option (Mert Can Altın) [#49456](https://github.com/nodejs/node/pull/49456)
* \[[`2b119108ff`](https://github.com/nodejs/node/commit/2b119108ff)] - **doc**: updated releasers instructions for node.js website (Claudio W) [#49427](https://github.com/nodejs/node/pull/49427)
* \[[`b9d4a80183`](https://github.com/nodejs/node/commit/b9d4a80183)] - **doc**: edit `import.meta.resolve` documentation (Antoine du Hamel) [#49247](https://github.com/nodejs/node/pull/49247)
* \[[`f67433f666`](https://github.com/nodejs/node/commit/f67433f666)] - **doc,tools**: switch to `@node-core/utils` (Michaël Zasso) [#49851](https://github.com/nodejs/node/pull/49851)
* \[[`142e256fc5`](https://github.com/nodejs/node/commit/142e256fc5)] - **errors**: improve classRegExp in errors.js (Uzlopak) [#49643](https://github.com/nodejs/node/pull/49643)
* \[[`6377f1bce2`](https://github.com/nodejs/node/commit/6377f1bce2)] - **errors**: use `determineSpecificType` in more error messages (Antoine du Hamel) [#49580](https://github.com/nodejs/node/pull/49580)
* \[[`05f0fcb4c4`](https://github.com/nodejs/node/commit/05f0fcb4c4)] - **esm**: identify parent importing a url with invalid host (Jacob Smith) [#49736](https://github.com/nodejs/node/pull/49736)
* \[[`8a6f5fb8f3`](https://github.com/nodejs/node/commit/8a6f5fb8f3)] - **esm**: fix return type of `import.meta.resolve` (Antoine du Hamel) [#49698](https://github.com/nodejs/node/pull/49698)
* \[[`a6140f1b8c`](https://github.com/nodejs/node/commit/a6140f1b8c)] - **esm**: update loaders warning (Geoffrey Booth) [#49633](https://github.com/nodejs/node/pull/49633)
* \[[`521a9327e0`](https://github.com/nodejs/node/commit/521a9327e0)] - **esm**: fix support for `URL` instances in `register` (Antoine du Hamel) [#49655](https://github.com/nodejs/node/pull/49655)
* \[[`3a9ea0925a`](https://github.com/nodejs/node/commit/3a9ea0925a)] - **esm**: clarify ERR\_REQUIRE\_ESM errors (Daniel Compton) [#49521](https://github.com/nodejs/node/pull/49521)
* \[[`1beefd5f16`](https://github.com/nodejs/node/commit/1beefd5f16)] - **esm**: set all hooks as release candidate (Geoffrey Booth) [#49597](https://github.com/nodejs/node/pull/49597)
* \[[`be48267888`](https://github.com/nodejs/node/commit/be48267888)] - **esm**: remove return value for `Module.register` (Antoine du Hamel) [#49529](https://github.com/nodejs/node/pull/49529)
* \[[`e74a075124`](https://github.com/nodejs/node/commit/e74a075124)] - **esm**: refactor test-esm-loader-resolve-type (Geoffrey Booth) [#49493](https://github.com/nodejs/node/pull/49493)
* \[[`17823b3533`](https://github.com/nodejs/node/commit/17823b3533)] - **esm**: refactor test-esm-named-exports (Geoffrey Booth) [#49493](https://github.com/nodejs/node/pull/49493)
* \[[`f34bd15ac1`](https://github.com/nodejs/node/commit/f34bd15ac1)] - **esm**: refactor mocking test (Geoffrey Booth) [#49465](https://github.com/nodejs/node/pull/49465)
* \[[`ec323bbd99`](https://github.com/nodejs/node/commit/ec323bbd99)] - **fs**: replace `SetMethodNoSideEffect` in node\_file (CanadaHonk) [#49857](https://github.com/nodejs/node/pull/49857)
* \[[`6acf800123`](https://github.com/nodejs/node/commit/6acf800123)] - **fs**: improve error performance for `unlinkSync` (CanadaHonk) [#49856](https://github.com/nodejs/node/pull/49856)
* \[[`31702c9403`](https://github.com/nodejs/node/commit/31702c9403)] - **fs**: improve `readFileSync` with file descriptors (Yagiz Nizipli) [#49691](https://github.com/nodejs/node/pull/49691)
* \[[`835f9fe7b9`](https://github.com/nodejs/node/commit/835f9fe7b9)] - **fs**: fix file descriptor validator (Yagiz Nizipli) [#49752](https://github.com/nodejs/node/pull/49752)
* \[[`b618fe262f`](https://github.com/nodejs/node/commit/b618fe262f)] - **fs**: improve error performance of `opendirSync` (Yagiz Nizipli) [#49705](https://github.com/nodejs/node/pull/49705)
* \[[`938471ef55`](https://github.com/nodejs/node/commit/938471ef55)] - **fs**: improve error performance of sync methods (Yagiz Nizipli) [#49593](https://github.com/nodejs/node/pull/49593)
* \[[`db3fc6d087`](https://github.com/nodejs/node/commit/db3fc6d087)] - **fs**: fix readdir and opendir recursive with unknown file types (William Marlow) [#49603](https://github.com/nodejs/node/pull/49603)
* \[[`0f020ed22d`](https://github.com/nodejs/node/commit/0f020ed22d)] - **gyp**: put cctest filenames in variables (Cheng Zhao) [#49178](https://github.com/nodejs/node/pull/49178)
* \[[`0ce1e94d12`](https://github.com/nodejs/node/commit/0ce1e94d12)] - **lib**: update encoding sets in `WHATWG API` (Jungku Lee) [#49610](https://github.com/nodejs/node/pull/49610)
* \[[`efd6815a7a`](https://github.com/nodejs/node/commit/efd6815a7a)] - **lib**: fix `internalBinding` typings (Yagiz Nizipli) [#49742](https://github.com/nodejs/node/pull/49742)
* \[[`1287d5b74e`](https://github.com/nodejs/node/commit/1287d5b74e)] - **lib**: allow byob reader for 'blob.stream()' (Debadree Chatterjee) [#49713](https://github.com/nodejs/node/pull/49713)
* \[[`bbc710522d`](https://github.com/nodejs/node/commit/bbc710522d)] - **lib**: reset the cwd cache before execution (Maël Nison) [#49684](https://github.com/nodejs/node/pull/49684)
* \[[`f62d649e4d`](https://github.com/nodejs/node/commit/f62d649e4d)] - **lib**: use internal `fileURLToPath` (Deokjin Kim) [#49558](https://github.com/nodejs/node/pull/49558)
* \[[`e515046941`](https://github.com/nodejs/node/commit/e515046941)] - **lib**: use internal `pathToFileURL` (Livia Medeiros) [#49553](https://github.com/nodejs/node/pull/49553)
* \[[`00608e8070`](https://github.com/nodejs/node/commit/00608e8070)] - **lib**: check SharedArrayBuffer availability in freeze\_intrinsics.js (Milan Burda) [#49482](https://github.com/nodejs/node/pull/49482)
* \[[`8bfbe7079c`](https://github.com/nodejs/node/commit/8bfbe7079c)] - **meta**: fix linter error (Antoine du Hamel) [#49755](https://github.com/nodejs/node/pull/49755)
* \[[`58f7a9e096`](https://github.com/nodejs/node/commit/58f7a9e096)] - **meta**: add primordials strategic initiative (Benjamin Gruenbaum) [#49706](https://github.com/nodejs/node/pull/49706)
* \[[`5366027756`](https://github.com/nodejs/node/commit/5366027756)] - **meta**: bump github/codeql-action from 2.21.2 to 2.21.5 (dependabot\[bot]) [#49438](https://github.com/nodejs/node/pull/49438)
* \[[`fe26b74082`](https://github.com/nodejs/node/commit/fe26b74082)] - **meta**: bump rtCamp/action-slack-notify from 2.2.0 to 2.2.1 (dependabot\[bot]) [#49437](https://github.com/nodejs/node/pull/49437)
* \[[`b0ce78a75b`](https://github.com/nodejs/node/commit/b0ce78a75b)] - **module**: fix the leak in SourceTextModule and ContextifySript (Joyee Cheung) [#48510](https://github.com/nodejs/node/pull/48510)
* \[[`4e578f8ab1`](https://github.com/nodejs/node/commit/4e578f8ab1)] - **module**: fix leak of vm.SyntheticModule (Joyee Cheung) [#48510](https://github.com/nodejs/node/pull/48510)
* \[[`69e4218772`](https://github.com/nodejs/node/commit/69e4218772)] - **module**: use symbol in WeakMap to manage host defined options (Joyee Cheung) [#48510](https://github.com/nodejs/node/pull/48510)
* \[[`96874e8fbc`](https://github.com/nodejs/node/commit/96874e8fbc)] - **node-api**: enable uncaught exceptions policy by default (Chengzhong Wu) [#49313](https://github.com/nodejs/node/pull/49313)
* \[[`b931aeadfd`](https://github.com/nodejs/node/commit/b931aeadfd)] - **perf\_hooks**: reduce overhead of new performance\_entries (Vinicius Lourenço) [#49803](https://github.com/nodejs/node/pull/49803)
* \[[`ad043bac31`](https://github.com/nodejs/node/commit/ad043bac31)] - **process**: add custom dir support for heapsnapshot-signal (Jithil P Ponnan) [#47854](https://github.com/nodejs/node/pull/47854)
* \[[`8a7c10194c`](https://github.com/nodejs/node/commit/8a7c10194c)] - **repl**: don't accumulate excess indentation in .load (Daniel X Moore) [#49461](https://github.com/nodejs/node/pull/49461)
* \[[`10a2adeed5`](https://github.com/nodejs/node/commit/10a2adeed5)] - **src**: improve error message when ICU data cannot be initialized (Joyee Cheung) [#49666](https://github.com/nodejs/node/pull/49666)
* \[[`ce37688bac`](https://github.com/nodejs/node/commit/ce37688bac)] - **src**: remove unnecessary todo (Rafael Gonzaga) [#49227](https://github.com/nodejs/node/pull/49227)
* \[[`f611583b71`](https://github.com/nodejs/node/commit/f611583b71)] - **src**: use SNAPSHOT\_SERDES to log snapshot ser/deserialization (Joyee Cheung) [#49637](https://github.com/nodejs/node/pull/49637)
* \[[`a597cb8457`](https://github.com/nodejs/node/commit/a597cb8457)] - **src**: port Pipe to uv\_pipe\_bind2, uv\_pipe\_connect2 (Geoff Goodman) [#49667](https://github.com/nodejs/node/pull/49667)
* \[[`fb21062338`](https://github.com/nodejs/node/commit/fb21062338)] - **src**: set --rehash-snapshot explicitly (Joyee Cheung) [#49556](https://github.com/nodejs/node/pull/49556)
* \[[`14ece0aa76`](https://github.com/nodejs/node/commit/14ece0aa76)] - **(SEMVER-MINOR)** **src**: allow embedders to override NODE\_MODULE\_VERSION (Cheng Zhao) [#49279](https://github.com/nodejs/node/pull/49279)
* \[[`4b5e23c71b`](https://github.com/nodejs/node/commit/4b5e23c71b)] - **src**: set ModuleWrap internal fields only once (Joyee Cheung) [#49391](https://github.com/nodejs/node/pull/49391)
* \[[`2d3f5c7cab`](https://github.com/nodejs/node/commit/2d3f5c7cab)] - **src**: fix fs\_type\_to\_name default value (Mustafa Ateş Uzun) [#49239](https://github.com/nodejs/node/pull/49239)
* \[[`cfbcb1059c`](https://github.com/nodejs/node/commit/cfbcb1059c)] - **src**: fix comment on StreamResource (rogertyang) [#49193](https://github.com/nodejs/node/pull/49193)
* \[[`39fb83ad16`](https://github.com/nodejs/node/commit/39fb83ad16)] - **src**: do not rely on the internal field being default to undefined (Joyee Cheung) [#49413](https://github.com/nodejs/node/pull/49413)
* \[[`9fd67fbff0`](https://github.com/nodejs/node/commit/9fd67fbff0)] - **stream**: use bitmap in writable state (Raz Luvaton) [#49834](https://github.com/nodejs/node/pull/49834)
* \[[`0ccd4638ac`](https://github.com/nodejs/node/commit/0ccd4638ac)] - **stream**: use bitmap in readable state (Benjamin Gruenbaum) [#49745](https://github.com/nodejs/node/pull/49745)
* \[[`b29d927010`](https://github.com/nodejs/node/commit/b29d927010)] - **stream**: improve readable webstream `pipeTo` (Raz Luvaton) [#49690](https://github.com/nodejs/node/pull/49690)
* \[[`7c5e322346`](https://github.com/nodejs/node/commit/7c5e322346)] - **stream**: improve webstream readable async iterator performance (Raz Luvaton) [#49662](https://github.com/nodejs/node/pull/49662)
* \[[`be211ef818`](https://github.com/nodejs/node/commit/be211ef818)] - **test**: deflake test-vm-contextified-script-leak (Joyee Cheung) [#49710](https://github.com/nodejs/node/pull/49710)
* \[[`355f10dab2`](https://github.com/nodejs/node/commit/355f10dab2)] - **test**: use checkIfCollectable in vm leak tests (Joyee Cheung) [#49671](https://github.com/nodejs/node/pull/49671)
* \[[`17cfc531aa`](https://github.com/nodejs/node/commit/17cfc531aa)] - **test**: add checkIfCollectable to test/common/gc.js (Joyee Cheung) [#49671](https://github.com/nodejs/node/pull/49671)
* \[[`e49a573752`](https://github.com/nodejs/node/commit/e49a573752)] - **test**: add os setPriority, getPriority test coverage (Wael) [#38771](https://github.com/nodejs/node/pull/38771)
* \[[`5f02711522`](https://github.com/nodejs/node/commit/5f02711522)] - **test**: deflake test-runner-output (Moshe Atlow) [#49878](https://github.com/nodejs/node/pull/49878)
* \[[`cd9754d6a7`](https://github.com/nodejs/node/commit/cd9754d6a7)] - **test**: mark test-runner-output as flaky (Joyee Cheung) [#49854](https://github.com/nodejs/node/pull/49854)
* \[[`5ad00424dd`](https://github.com/nodejs/node/commit/5ad00424dd)] - **test**: use mustSucceed instead of mustCall (SiddharthDevulapalli) [#49788](https://github.com/nodejs/node/pull/49788)
* \[[`3db9b40081`](https://github.com/nodejs/node/commit/3db9b40081)] - **test**: refactor test-readline-async-iterators into a benchmark (Shubham Pandey) [#49237](https://github.com/nodejs/node/pull/49237)
* \[[`2cc5ad7859`](https://github.com/nodejs/node/commit/2cc5ad7859)] - _**Revert**_ "**test**: mark test-http-regr-gh-2928 as flaky" (Luigi Pinca) [#49708](https://github.com/nodejs/node/pull/49708)
* \[[`e5185b053c`](https://github.com/nodejs/node/commit/e5185b053c)] - **test**: use `fs.constants` for `fs.access` constants (Livia Medeiros) [#49685](https://github.com/nodejs/node/pull/49685)
* \[[`b9e5b43462`](https://github.com/nodejs/node/commit/b9e5b43462)] - **test**: deflake test-http-regr-gh-2928 (Luigi Pinca) [#49574](https://github.com/nodejs/node/pull/49574)
* \[[`1fffda504e`](https://github.com/nodejs/node/commit/1fffda504e)] - **test**: fix argument computation in embedtest (Joyee Cheung) [#49506](https://github.com/nodejs/node/pull/49506)
* \[[`6e56f2db52`](https://github.com/nodejs/node/commit/6e56f2db52)] - **test**: skip test-child-process-stdio-reuse-readable-stdio on Windows (Joyee Cheung) [#49621](https://github.com/nodejs/node/pull/49621)
* \[[`ab3afb330d`](https://github.com/nodejs/node/commit/ab3afb330d)] - **test**: mark test-runner-watch-mode as flaky (Joyee Cheung) [#49627](https://github.com/nodejs/node/pull/49627)
* \[[`185d9b50db`](https://github.com/nodejs/node/commit/185d9b50db)] - **test**: deflake test-tls-socket-close (Luigi Pinca) [#49575](https://github.com/nodejs/node/pull/49575)
* \[[`c70c74a9e6`](https://github.com/nodejs/node/commit/c70c74a9e6)] - **test**: show more info on failure in test-cli-syntax-require.js (Joyee Cheung) [#49561](https://github.com/nodejs/node/pull/49561)
* \[[`ed7c6d1114`](https://github.com/nodejs/node/commit/ed7c6d1114)] - **test**: mark test-http-regr-gh-2928 as flaky (Joyee Cheung) [#49565](https://github.com/nodejs/node/pull/49565)
* \[[`3599eebab9`](https://github.com/nodejs/node/commit/3599eebab9)] - **test**: use spawnSyncAndExitWithoutError in sea tests (Joyee Cheung) [#49543](https://github.com/nodejs/node/pull/49543)
* \[[`f79b153e89`](https://github.com/nodejs/node/commit/f79b153e89)] - **test**: use spawnSyncAndExitWithoutError in test/common/sea.js (Joyee Cheung) [#49543](https://github.com/nodejs/node/pull/49543)
* \[[`c079c73769`](https://github.com/nodejs/node/commit/c079c73769)] - **test**: use setImmediate() in test-heapdump-shadowrealm.js (Joyee Cheung) [#49573](https://github.com/nodejs/node/pull/49573)
* \[[`667a92493c`](https://github.com/nodejs/node/commit/667a92493c)] - **test**: skip test-child-process-pipe-dataflow\.js on Windows (Joyee Cheung) [#49563](https://github.com/nodejs/node/pull/49563)
* \[[`91af0a9a3c`](https://github.com/nodejs/node/commit/91af0a9a3c)] - _**Revert**_ "**test**: ignore the copied entry\_point.c" (Chengzhong Wu) [#49515](https://github.com/nodejs/node/pull/49515)
* \[[`567afc71b8`](https://github.com/nodejs/node/commit/567afc71b8)] - **test**: avoid copying test source files (Chengzhong Wu) [#49515](https://github.com/nodejs/node/pull/49515)
* \[[`ced25a976d`](https://github.com/nodejs/node/commit/ced25a976d)] - **test**: increase coverage of `Module.register` and `initialize` hook (Antoine du Hamel) [#49532](https://github.com/nodejs/node/pull/49532)
* \[[`be02fbdb8a`](https://github.com/nodejs/node/commit/be02fbdb8a)] - **test**: isolate `globalPreload` tests (Geoffrey Booth) [#49545](https://github.com/nodejs/node/pull/49545)
* \[[`f214428845`](https://github.com/nodejs/node/commit/f214428845)] - **test**: split test-crypto-dh to avoid timeout on slow machines in the CI (Joyee Cheung) [#49492](https://github.com/nodejs/node/pull/49492)
* \[[`3987094569`](https://github.com/nodejs/node/commit/3987094569)] - **test**: make `test-dotenv-node-options` locale-independent (Livia Medeiros) [#49470](https://github.com/nodejs/node/pull/49470)
* \[[`34c1741792`](https://github.com/nodejs/node/commit/34c1741792)] - **test**: add test for urlstrings usage in `node:fs` (Livia Medeiros) [#49471](https://github.com/nodejs/node/pull/49471)
* \[[`c3c6c4f007`](https://github.com/nodejs/node/commit/c3c6c4f007)] - **test**: make test-worker-prof more robust (Joyee Cheung) [#49274](https://github.com/nodejs/node/pull/49274)
* \[[`843df1a4da`](https://github.com/nodejs/node/commit/843df1a4da)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#49714](https://github.com/nodejs/node/pull/49714)
* \[[`80b342cc38`](https://github.com/nodejs/node/commit/80b342cc38)] - **(SEMVER-MINOR)** **test\_runner**: accept `testOnly` in `run` (Moshe Atlow) [#49753](https://github.com/nodejs/node/pull/49753)
* \[[`76865515b9`](https://github.com/nodejs/node/commit/76865515b9)] - **test\_runner**: fix test runner watch mode when no positional arguments (Moshe Atlow) [#49578](https://github.com/nodejs/node/pull/49578)
* \[[`17a05b141d`](https://github.com/nodejs/node/commit/17a05b141d)] - **(SEMVER-MINOR)** **test\_runner**: add junit reporter (Moshe Atlow) [#49614](https://github.com/nodejs/node/pull/49614)
* \[[`5672e38457`](https://github.com/nodejs/node/commit/5672e38457)] - **test\_runner**: add jsdocs to mock.js (Caio Borghi) [#49555](https://github.com/nodejs/node/pull/49555)
* \[[`b4d42a8f2b`](https://github.com/nodejs/node/commit/b4d42a8f2b)] - **test\_runner**: fix invalid timer call (Erick Wendel) [#49477](https://github.com/nodejs/node/pull/49477)
* \[[`f755e6786b`](https://github.com/nodejs/node/commit/f755e6786b)] - **test\_runner**: add jsdocs to MockTimers (Erick Wendel) [#49476](https://github.com/nodejs/node/pull/49476)
* \[[`e7285d4bf0`](https://github.com/nodejs/node/commit/e7285d4bf0)] - **test\_runner**: fix typescript coverage (Moshe Atlow) [#49406](https://github.com/nodejs/node/pull/49406)
* \[[`07a2e29bf3`](https://github.com/nodejs/node/commit/07a2e29bf3)] - **tools**: support updating @reporters/github manually (Moshe Atlow) [#49871](https://github.com/nodejs/node/pull/49871)
* \[[`5ac6722031`](https://github.com/nodejs/node/commit/5ac6722031)] - **tools**: skip ruff on tools/node\_modules (Moshe Atlow) [#49838](https://github.com/nodejs/node/pull/49838)
* \[[`462228bd24`](https://github.com/nodejs/node/commit/462228bd24)] - **tools**: fix uvwasi updater (Michael Dawson) [#49682](https://github.com/nodejs/node/pull/49682)
* \[[`ff81bfb958`](https://github.com/nodejs/node/commit/ff81bfb958)] - **tools**: update lint-md-dependencies to rollup\@3.29.2 (Node.js GitHub Bot) [#49679](https://github.com/nodejs/node/pull/49679)
* \[[`08ffc6344c`](https://github.com/nodejs/node/commit/08ffc6344c)] - **tools**: restrict internal code from using public `url` module (LiviaMedeiros) [#49590](https://github.com/nodejs/node/pull/49590)
* \[[`728ebf6c97`](https://github.com/nodejs/node/commit/728ebf6c97)] - **tools**: update eslint to 8.49.0 (Node.js GitHub Bot) [#49586](https://github.com/nodejs/node/pull/49586)
* \[[`20d038ffb1`](https://github.com/nodejs/node/commit/20d038ffb1)] - **tools**: update lint-md-dependencies to rollup\@3.29.0 unified\@11.0.3 (Node.js GitHub Bot) [#49584](https://github.com/nodejs/node/pull/49584)
* \[[`210c15bd12`](https://github.com/nodejs/node/commit/210c15bd12)] - **tools**: allow passing absolute path of config.gypi in js2c (Cheng Zhao) [#49162](https://github.com/nodejs/node/pull/49162)
* \[[`e341efe173`](https://github.com/nodejs/node/commit/e341efe173)] - **tools**: configure never-stale label correctly (Michaël Zasso) [#49498](https://github.com/nodejs/node/pull/49498)
* \[[`a8a8a498ce`](https://github.com/nodejs/node/commit/a8a8a498ce)] - **tools**: update doc dependencies (Node.js GitHub Bot) [#49467](https://github.com/nodejs/node/pull/49467)
* \[[`ac06607f9e`](https://github.com/nodejs/node/commit/ac06607f9e)] - **typings**: fix missing property in `ExportedHooks` (Antoine du Hamel) [#49567](https://github.com/nodejs/node/pull/49567)
* \[[`097b59807a`](https://github.com/nodejs/node/commit/097b59807a)] - **url**: improve invalid url performance (Yagiz Nizipli) [#49692](https://github.com/nodejs/node/pull/49692)
* \[[`7c2060cfac`](https://github.com/nodejs/node/commit/7c2060cfac)] - **util**: add `getCwdSafe` internal util fn (João Lenon) [#48434](https://github.com/nodejs/node/pull/48434)
* \[[`c23c60f545`](https://github.com/nodejs/node/commit/c23c60f545)] - **zlib**: disable CRC32 SIMD optimization (Luigi Pinca) [#49511](https://github.com/nodejs/node/pull/49511)

<a id="20.7.0"></a>

## 2023-09-18, Version 20.7.0 (Current), @UlisesGascon

### Notable Changes

* \[[`022f1b70c1`](https://github.com/nodejs/node/commit/022f1b70c1)] - **src**: support multiple `--env-file` declarations (Yagiz Nizipli) [#49542](https://github.com/nodejs/node/pull/49542)
* \[[`4a1d1cad61`](https://github.com/nodejs/node/commit/4a1d1cad61)] - **crypto**: update root certificates to NSS 3.93 (Node.js GitHub Bot) [#49341](https://github.com/nodejs/node/pull/49341)
* \[[`a1a65f593c`](https://github.com/nodejs/node/commit/a1a65f593c)] - **deps**: upgrade npm to 10.1.0 (npm team) [#49570](https://github.com/nodejs/node/pull/49570)
* \[[`6c2480cad9`](https://github.com/nodejs/node/commit/6c2480cad9)] - **(SEMVER-MINOR)** **deps**: upgrade npm to 10.0.0 (npm team) [#49423](https://github.com/nodejs/node/pull/49423)
* \[[`bef900e56b`](https://github.com/nodejs/node/commit/bef900e56b)] - **doc**: move and rename loaders section (Geoffrey Booth) [#49261](https://github.com/nodejs/node/pull/49261)
* \[[`db4ce8a593`](https://github.com/nodejs/node/commit/db4ce8a593)] - **doc**: add release key for Ulises Gascon (Ulises Gascón) [#49196](https://github.com/nodejs/node/pull/49196)
* \[[`11c85ffa98`](https://github.com/nodejs/node/commit/11c85ffa98)] - **(SEMVER-MINOR)** **lib**: add api to detect whether source-maps are enabled (翠 / green) [#46391](https://github.com/nodejs/node/pull/46391)
* \[[`ec51e25ed7`](https://github.com/nodejs/node/commit/ec51e25ed7)] - **src,permission**: add multiple allow-fs-\* flags (Carlos Espa) [#49047](https://github.com/nodejs/node/pull/49047)
* \[[`efdc95fbc0`](https://github.com/nodejs/node/commit/efdc95fbc0)] - **(SEMVER-MINOR)** **test\_runner**: expose location of tests (Colin Ihrig) [#48975](https://github.com/nodejs/node/pull/48975)

### Commits

* \[[`e84515594e`](https://github.com/nodejs/node/commit/e84515594e)] - **benchmark**: use `tmpdir.resolve()` (Livia Medeiros) [#49137](https://github.com/nodejs/node/pull/49137)
* \[[`f37444e896`](https://github.com/nodejs/node/commit/f37444e896)] - **bootstrap**: build code cache from deserialized isolate (Joyee Cheung) [#49099](https://github.com/nodejs/node/pull/49099)
* \[[`af6dc1754d`](https://github.com/nodejs/node/commit/af6dc1754d)] - **bootstrap**: do not generate code cache in an unfinalized isolate (Joyee Cheung) [#49108](https://github.com/nodejs/node/pull/49108)
* \[[`cade5716df`](https://github.com/nodejs/node/commit/cade5716df)] - **build**: add symlink to `compile_commands.json` file if needed (Juan José) [#49260](https://github.com/nodejs/node/pull/49260)
* \[[`34a2590b05`](https://github.com/nodejs/node/commit/34a2590b05)] - **build**: expand when we run internet tests (Michael Dawson) [#49218](https://github.com/nodejs/node/pull/49218)
* \[[`f637fd46ab`](https://github.com/nodejs/node/commit/f637fd46ab)] - **build**: fix typo `libray` -> `library` (configure.py) (michalbiesek) [#49106](https://github.com/nodejs/node/pull/49106)
* \[[`ef3d8dd493`](https://github.com/nodejs/node/commit/ef3d8dd493)] - **crypto**: remove webcrypto EdDSA key checks and properties (Filip Skokan) [#49408](https://github.com/nodejs/node/pull/49408)
* \[[`4a1d1cad61`](https://github.com/nodejs/node/commit/4a1d1cad61)] - **crypto**: update root certificates to NSS 3.93 (Node.js GitHub Bot) [#49341](https://github.com/nodejs/node/pull/49341)
* \[[`7eb10a38ea`](https://github.com/nodejs/node/commit/7eb10a38ea)] - **crypto**: remove getDefaultEncoding() (Tobias Nießen) [#49170](https://github.com/nodejs/node/pull/49170)
* \[[`772496c030`](https://github.com/nodejs/node/commit/772496c030)] - **crypto**: remove default encoding from DiffieHellman (Tobias Nießen) [#49169](https://github.com/nodejs/node/pull/49169)
* \[[`c795083232`](https://github.com/nodejs/node/commit/c795083232)] - **crypto**: remove default encoding from Hash/Hmac (Tobias Nießen) [#49167](https://github.com/nodejs/node/pull/49167)
* \[[`08197aa010`](https://github.com/nodejs/node/commit/08197aa010)] - **crypto**: remove default encoding from sign/verify (Tobias Nießen) [#49145](https://github.com/nodejs/node/pull/49145)
* \[[`a1a65f593c`](https://github.com/nodejs/node/commit/a1a65f593c)] - **deps**: upgrade npm to 10.1.0 (npm team) [#49570](https://github.com/nodejs/node/pull/49570)
* \[[`6c2480cad9`](https://github.com/nodejs/node/commit/6c2480cad9)] - **(SEMVER-MINOR)** **deps**: upgrade npm to 10.0.0 (npm team) [#49423](https://github.com/nodejs/node/pull/49423)
* \[[`84195d9584`](https://github.com/nodejs/node/commit/84195d9584)] - **deps**: add missing thread-common.c in uv.gyp (Santiago Gimeno) [#49410](https://github.com/nodejs/node/pull/49410)
* \[[`5b70b68b3d`](https://github.com/nodejs/node/commit/5b70b68b3d)] - **deps**: V8: cherry-pick eadaef581c29 (Adam Majer) [#49401](https://github.com/nodejs/node/pull/49401)
* \[[`fe34d632e8`](https://github.com/nodejs/node/commit/fe34d632e8)] - **deps**: update zlib to 1.2.13.1-motley-f5fd0ad (Node.js GitHub Bot) [#49252](https://github.com/nodejs/node/pull/49252)
* \[[`db4ce8a593`](https://github.com/nodejs/node/commit/db4ce8a593)] - **doc**: add release key for Ulises Gascon (Ulises Gascón) [#49196](https://github.com/nodejs/node/pull/49196)
* \[[`e5f3a694cf`](https://github.com/nodejs/node/commit/e5f3a694cf)] - **doc**: fix node-api call example (Chengzhong Wu) [#49395](https://github.com/nodejs/node/pull/49395)
* \[[`021345a724`](https://github.com/nodejs/node/commit/021345a724)] - **doc**: add news issue for Diagnostics WG (Michael Dawson) [#49306](https://github.com/nodejs/node/pull/49306)
* \[[`f82347266b`](https://github.com/nodejs/node/commit/f82347266b)] - **doc**: clarify policy expectations (Rafael Gonzaga) [#48947](https://github.com/nodejs/node/pull/48947)
* \[[`73cfd9c895`](https://github.com/nodejs/node/commit/73cfd9c895)] - **doc**: add print results for examples in `StringDecoder` (Jungku Lee) [#49326](https://github.com/nodejs/node/pull/49326)
* \[[`63ab591416`](https://github.com/nodejs/node/commit/63ab591416)] - **doc**: update outdated reference to NIST SP 800-131A (Tobias Nießen) [#49316](https://github.com/nodejs/node/pull/49316)
* \[[`935dfe2afd`](https://github.com/nodejs/node/commit/935dfe2afd)] - **doc**: use `cjs` as block code's type in `MockTimers` (Deokjin Kim) [#49309](https://github.com/nodejs/node/pull/49309)
* \[[`7c0cd2fb87`](https://github.com/nodejs/node/commit/7c0cd2fb87)] - **doc**: update `options.filter` description for `fs.cp` (Shubham Pandey) [#49289](https://github.com/nodejs/node/pull/49289)
* \[[`f72e79ea67`](https://github.com/nodejs/node/commit/f72e79ea67)] - **doc**: add riscv64 to list of architectures (Stewart X Addison) [#49284](https://github.com/nodejs/node/pull/49284)
* \[[`d19c710064`](https://github.com/nodejs/node/commit/d19c710064)] - **doc**: avoid "not currently recommended" (Tobias Nießen) [#49300](https://github.com/nodejs/node/pull/49300)
* \[[`ae656101c0`](https://github.com/nodejs/node/commit/ae656101c0)] - **doc**: update module hooks docs (Geoffrey Booth) [#49265](https://github.com/nodejs/node/pull/49265)
* \[[`fefbdb92f2`](https://github.com/nodejs/node/commit/fefbdb92f2)] - **doc**: modify param description for end(),write() in `StringDecoder` (Jungku Lee) [#49285](https://github.com/nodejs/node/pull/49285)
* \[[`59e66a1ebe`](https://github.com/nodejs/node/commit/59e66a1ebe)] - **doc**: use NODE\_API\_SUPPORTED\_VERSION\_MAX in release doc (Cheng Zhao) [#49268](https://github.com/nodejs/node/pull/49268)
* \[[`ac3b88449b`](https://github.com/nodejs/node/commit/ac3b88449b)] - **doc**: fix typo in `stream.finished` documentation (Antoine du Hamel) [#49271](https://github.com/nodejs/node/pull/49271)
* \[[`7428ebf6c3`](https://github.com/nodejs/node/commit/7428ebf6c3)] - **doc**: update description for `percent_encode` sets in `WHATWG API` (Jungku Lee) [#49258](https://github.com/nodejs/node/pull/49258)
* \[[`bef900e56b`](https://github.com/nodejs/node/commit/bef900e56b)] - **doc**: move and rename loaders section (Geoffrey Booth) [#49261](https://github.com/nodejs/node/pull/49261)
* \[[`a22e0d9696`](https://github.com/nodejs/node/commit/a22e0d9696)] - **doc**: clarify use of Uint8Array for n-api (Fedor Indutny) [#48742](https://github.com/nodejs/node/pull/48742)
* \[[`1704f24cb9`](https://github.com/nodejs/node/commit/1704f24cb9)] - **doc**: add signature for `module.register` (Geoffrey Booth) [#49251](https://github.com/nodejs/node/pull/49251)
* \[[`5a363bb01b`](https://github.com/nodejs/node/commit/5a363bb01b)] - **doc**: caveat unavailability of `import.meta.resolve` in custom loaders (Jacob Smith) [#49242](https://github.com/nodejs/node/pull/49242)
* \[[`8101f2b259`](https://github.com/nodejs/node/commit/8101f2b259)] - **doc**: use same name in the doc as in the code (Hyunjin Kim) [#49216](https://github.com/nodejs/node/pull/49216)
* \[[`edf278d60d`](https://github.com/nodejs/node/commit/edf278d60d)] - **doc**: add notable-change label mention to PR template (Rafael Gonzaga) [#49188](https://github.com/nodejs/node/pull/49188)
* \[[`3df2251a6a`](https://github.com/nodejs/node/commit/3df2251a6a)] - **doc**: add h1 summary to security release process (Rafael Gonzaga) [#49112](https://github.com/nodejs/node/pull/49112)
* \[[`9fcd99a744`](https://github.com/nodejs/node/commit/9fcd99a744)] - **doc**: update to semver-minor releases by default (Rafael Gonzaga) [#49175](https://github.com/nodejs/node/pull/49175)
* \[[`777931f499`](https://github.com/nodejs/node/commit/777931f499)] - **doc**: fix wording in napi\_async\_init (Tobias Nießen) [#49180](https://github.com/nodejs/node/pull/49180)
* \[[`f45c8e10c0`](https://github.com/nodejs/node/commit/f45c8e10c0)] - **doc,test**: add known path resolution issue in permission model (Tobias Nießen) [#49155](https://github.com/nodejs/node/pull/49155)
* \[[`a6cfea3f74`](https://github.com/nodejs/node/commit/a6cfea3f74)] - **esm**: align sync and async load implementations (Antoine du Hamel) [#49152](https://github.com/nodejs/node/pull/49152)
* \[[`9fac310b33`](https://github.com/nodejs/node/commit/9fac310b33)] - **fs**: add the options param description in openAsBlob() (Yeseul Lee) [#49308](https://github.com/nodejs/node/pull/49308)
* \[[`92772a8175`](https://github.com/nodejs/node/commit/92772a8175)] - **fs**: remove redundant code in readableWebStream() (Deokjin Kim) [#49298](https://github.com/nodejs/node/pull/49298)
* \[[`88ba79b083`](https://github.com/nodejs/node/commit/88ba79b083)] - **fs**: make sure to write entire buffer (Robert Nagy) [#49211](https://github.com/nodejs/node/pull/49211)
* \[[`11c85ffa98`](https://github.com/nodejs/node/commit/11c85ffa98)] - **(SEMVER-MINOR)** **lib**: add api to detect whether source-maps are enabled (翠 / green) [#46391](https://github.com/nodejs/node/pull/46391)
* \[[`c12711ebfe`](https://github.com/nodejs/node/commit/c12711ebfe)] - **lib**: implement WeakReference on top of JS WeakRef (Joyee Cheung) [#49053](https://github.com/nodejs/node/pull/49053)
* \[[`9a0891f88d`](https://github.com/nodejs/node/commit/9a0891f88d)] - **meta**: bump step-security/harden-runner from 2.5.0 to 2.5.1 (dependabot\[bot]) [#49435](https://github.com/nodejs/node/pull/49435)
* \[[`ae67f41ef1`](https://github.com/nodejs/node/commit/ae67f41ef1)] - **meta**: bump actions/checkout from 3.5.3 to 3.6.0 (dependabot\[bot]) [#49436](https://github.com/nodejs/node/pull/49436)
* \[[`71b4411fb2`](https://github.com/nodejs/node/commit/71b4411fb2)] - **meta**: bump actions/setup-node from 3.7.0 to 3.8.1 (dependabot\[bot]) [#49434](https://github.com/nodejs/node/pull/49434)
* \[[`83b7d3a395`](https://github.com/nodejs/node/commit/83b7d3a395)] - **meta**: remove modules team from CODEOWNERS (Benjamin Gruenbaum) [#49412](https://github.com/nodejs/node/pull/49412)
* \[[`81ff68c45c`](https://github.com/nodejs/node/commit/81ff68c45c)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#49264](https://github.com/nodejs/node/pull/49264)
* \[[`ab975233cc`](https://github.com/nodejs/node/commit/ab975233cc)] - **meta**: mention nodejs/tsc when changing GH templates (Rafael Gonzaga) [#49189](https://github.com/nodejs/node/pull/49189)
* \[[`ceaa5494de`](https://github.com/nodejs/node/commit/ceaa5494de)] - **meta**: add test/reporters to codeowners (Chemi Atlow) [#49186](https://github.com/nodejs/node/pull/49186)
* \[[`de0a51b7cf`](https://github.com/nodejs/node/commit/de0a51b7cf)] - **net**: improve performance of isIPv4 and isIPv6 (Uzlopak) [#49568](https://github.com/nodejs/node/pull/49568)
* \[[`8d0913bf95`](https://github.com/nodejs/node/commit/8d0913bf95)] - **net**: use asserts in JS Socket Stream to catch races in future (Tim Perry) [#49400](https://github.com/nodejs/node/pull/49400)
* \[[`2486836a7d`](https://github.com/nodejs/node/commit/2486836a7d)] - **net**: fix crash due to simultaneous close/shutdown on JS Stream Sockets (Tim Perry) [#49400](https://github.com/nodejs/node/pull/49400)
* \[[`7a808340cd`](https://github.com/nodejs/node/commit/7a808340cd)] - **node-api**: fix compiler warning in node\_api.h (Michael Graeb) [#49103](https://github.com/nodejs/node/pull/49103)
* \[[`30f26a99f4`](https://github.com/nodejs/node/commit/30f26a99f4)] - **permission**: ensure to resolve path when calling mkdtemp (RafaelGSS) [nodejs-private/node-private#440](https://github.com/nodejs-private/node-private/pull/440)
* \[[`5051c75a5b`](https://github.com/nodejs/node/commit/5051c75a5b)] - **policy**: fix path to URL conversion (Antoine du Hamel) [#49133](https://github.com/nodejs/node/pull/49133)
* \[[`173aed4757`](https://github.com/nodejs/node/commit/173aed4757)] - **report**: fix recent coverity warning (Michael Dawson) [#48954](https://github.com/nodejs/node/pull/48954)
* \[[`d7ff78b442`](https://github.com/nodejs/node/commit/d7ff78b442)] - **sea**: generate code cache with deserialized isolate (Joyee Cheung) [#49226](https://github.com/nodejs/node/pull/49226)
* \[[`022f1b70c1`](https://github.com/nodejs/node/commit/022f1b70c1)] - **src**: support multiple `--env-file` declarations (Yagiz Nizipli) [#49542](https://github.com/nodejs/node/pull/49542)
* \[[`154b1c2115`](https://github.com/nodejs/node/commit/154b1c2115)] - **src**: don't overwrite environment from .env file (Phil Nash) [#49424](https://github.com/nodejs/node/pull/49424)
* \[[`dc4de1c69b`](https://github.com/nodejs/node/commit/dc4de1c69b)] - **src**: modify code for empty string (pluris) [#49336](https://github.com/nodejs/node/pull/49336)
* \[[`701c46f967`](https://github.com/nodejs/node/commit/701c46f967)] - **src**: remove unused PromiseWrap-related code (Joyee Cheung) [#49335](https://github.com/nodejs/node/pull/49335)
* \[[`4a094dc7af`](https://github.com/nodejs/node/commit/4a094dc7af)] - **src**: rename IsAnyByteSource to IsAnyBufferSource (Tobias Nießen) [#49346](https://github.com/nodejs/node/pull/49346)
* \[[`55d6649175`](https://github.com/nodejs/node/commit/55d6649175)] - **src**: support snapshot deserialization in RAIIIsolate (Joyee Cheung) [#49226](https://github.com/nodejs/node/pull/49226)
* \[[`dc092864ef`](https://github.com/nodejs/node/commit/dc092864ef)] - **src**: remove unused function `GetName()` in node\_perf (Jungku Lee) [#49244](https://github.com/nodejs/node/pull/49244)
* \[[`f2552a410e`](https://github.com/nodejs/node/commit/f2552a410e)] - **src**: use ARES\_SUCCESS instead of 0 (Jungku Lee) [#49048](https://github.com/nodejs/node/pull/49048)
* \[[`4a9ae31519`](https://github.com/nodejs/node/commit/4a9ae31519)] - **src**: add a condition if the argument of `DomainToUnicode` is empty (Jungku Lee) [#49097](https://github.com/nodejs/node/pull/49097)
* \[[`f460362cdf`](https://github.com/nodejs/node/commit/f460362cdf)] - **src**: remove C++ WeakReference implementation (Joyee Cheung) [#49053](https://github.com/nodejs/node/pull/49053)
* \[[`2a35383b3e`](https://github.com/nodejs/node/commit/2a35383b3e)] - **src**: use per-realm GetBindingData() wherever applicable (Joyee Cheung) [#49007](https://github.com/nodejs/node/pull/49007)
* \[[`184bbddcf5`](https://github.com/nodejs/node/commit/184bbddcf5)] - **src**: add per-realm GetBindingData() method (Joyee Cheung) [#49007](https://github.com/nodejs/node/pull/49007)
* \[[`e9946885f9`](https://github.com/nodejs/node/commit/e9946885f9)] - **src**: serialize both BaseObject slots (Joyee Cheung) [#48996](https://github.com/nodejs/node/pull/48996)
* \[[`ec51e25ed7`](https://github.com/nodejs/node/commit/ec51e25ed7)] - **src,permission**: add multiple allow-fs-\* flags (Carlos Espa) [#49047](https://github.com/nodejs/node/pull/49047)
* \[[`8aac95de4b`](https://github.com/nodejs/node/commit/8aac95de4b)] - **stream**: improve tee perf by reduce `ReflectConstruct` usages (Raz Luvaton) [#49546](https://github.com/nodejs/node/pull/49546)
* \[[`0eea7fd8fb`](https://github.com/nodejs/node/commit/0eea7fd8fb)] - **stream**: use Buffer.from when constructor is a Buffer (Matthew Aitken) [#49250](https://github.com/nodejs/node/pull/49250)
* \[[`b961d9bd52`](https://github.com/nodejs/node/commit/b961d9bd52)] - **stream**: add `highWaterMark` for the map operator (Raz Luvaton) [#49249](https://github.com/nodejs/node/pull/49249)
* \[[`ca1384166d`](https://github.com/nodejs/node/commit/ca1384166d)] - **test**: fix warning for comment in embedtest (Jungku Lee) [#49416](https://github.com/nodejs/node/pull/49416)
* \[[`2a35782809`](https://github.com/nodejs/node/commit/2a35782809)] - **test**: simplify test-crypto-dh-group-setters (Tobias Nießen) [#49404](https://github.com/nodejs/node/pull/49404)
* \[[`6740f3c209`](https://github.com/nodejs/node/commit/6740f3c209)] - **test**: verify dynamic import call with absolute path strings (Chengzhong Wu) [#49275](https://github.com/nodejs/node/pull/49275)
* \[[`6ed47bd8fb`](https://github.com/nodejs/node/commit/6ed47bd8fb)] - **test**: reduce length in crypto keygen tests (Joyee Cheung) [#49221](https://github.com/nodejs/node/pull/49221)
* \[[`4faa30c553`](https://github.com/nodejs/node/commit/4faa30c553)] - **test**: split JWK async elliptic curve keygen tests (Joyee Cheung) [#49221](https://github.com/nodejs/node/pull/49221)
* \[[`e04a2603d8`](https://github.com/nodejs/node/commit/e04a2603d8)] - **test**: split test-crypto-keygen.js (Joyee Cheung) [#49221](https://github.com/nodejs/node/pull/49221)
* \[[`0d23c1d4ce`](https://github.com/nodejs/node/commit/0d23c1d4ce)] - **test**: rename test-crypto-modp1-error (Tobias Nießen) [#49348](https://github.com/nodejs/node/pull/49348)
* \[[`48e41569e2`](https://github.com/nodejs/node/commit/48e41569e2)] - **test**: migrate message source map tests from Python to JS (Yiyun Lei) [#49238](https://github.com/nodejs/node/pull/49238)
* \[[`a11e64e09c`](https://github.com/nodejs/node/commit/a11e64e09c)] - **test**: fix compiler warning in NodeCryptoEnv (Tobias Nießen) [#49206](https://github.com/nodejs/node/pull/49206)
* \[[`345543938f`](https://github.com/nodejs/node/commit/345543938f)] - **test**: handle EUNATCH (Abdirahim Musse) [#48050](https://github.com/nodejs/node/pull/48050)
* \[[`e391f4b197`](https://github.com/nodejs/node/commit/e391f4b197)] - **test**: use `tmpdir.resolve()` (Livia Medeiros) [#49136](https://github.com/nodejs/node/pull/49136)
* \[[`910378f93f`](https://github.com/nodejs/node/commit/910378f93f)] - **test**: reduce flakiness of `test-esm-loader-hooks` (Antoine du Hamel) [#49248](https://github.com/nodejs/node/pull/49248)
* \[[`4a85f70462`](https://github.com/nodejs/node/commit/4a85f70462)] - **test**: add spawnSyncAndExit() and spawnSyncAndExitWithoutError() (Joyee Cheung) [#49200](https://github.com/nodejs/node/pull/49200)
* \[[`9610008b79`](https://github.com/nodejs/node/commit/9610008b79)] - **test**: make test-perf-hooks more robust and work with workers (Joyee Cheung) [#49197](https://github.com/nodejs/node/pull/49197)
* \[[`dc8fff9a75`](https://github.com/nodejs/node/commit/dc8fff9a75)] - **test**: use gcUntil() in test-v8-serialize-leak (Joyee Cheung) [#49168](https://github.com/nodejs/node/pull/49168)
* \[[`ca9f801332`](https://github.com/nodejs/node/commit/ca9f801332)] - **test**: make WeakReference tests robust (Joyee Cheung) [#49053](https://github.com/nodejs/node/pull/49053)
* \[[`de103a4686`](https://github.com/nodejs/node/commit/de103a4686)] - **test**: add test for effect of UV\_THREADPOOL\_SIZE (Tobias Nießen) [#49165](https://github.com/nodejs/node/pull/49165)
* \[[`47d24f144b`](https://github.com/nodejs/node/commit/47d24f144b)] - **test**: use expectSyncExit{WithErrors} in snapshot tests (Joyee Cheung) [#49020](https://github.com/nodejs/node/pull/49020)
* \[[`c441f5a097`](https://github.com/nodejs/node/commit/c441f5a097)] - **test**: add expectSyncExitWithoutError() and expectSyncExit() utils (Joyee Cheung) [#49020](https://github.com/nodejs/node/pull/49020)
* \[[`4d184b5251`](https://github.com/nodejs/node/commit/4d184b5251)] - **test**: remove --no-warnings flag in test\_runner fixtures (Raz Luvaton) [#48989](https://github.com/nodejs/node/pull/48989)
* \[[`25e967a90b`](https://github.com/nodejs/node/commit/25e967a90b)] - **test**: reorder test files fixtures for better understanding (Raz Luvaton) [#48787](https://github.com/nodejs/node/pull/48787)
* \[[`fac56dbcc0`](https://github.com/nodejs/node/commit/fac56dbcc0)] - **test,benchmark**: use `tmpdir.fileURL()` (Livia Medeiros) [#49138](https://github.com/nodejs/node/pull/49138)
* \[[`36763fa532`](https://github.com/nodejs/node/commit/36763fa532)] - **test\_runner**: preserve original property descriptor (Erick Wendel) [#49433](https://github.com/nodejs/node/pull/49433)
* \[[`40e9fcdbea`](https://github.com/nodejs/node/commit/40e9fcdbea)] - **test\_runner**: add support for setImmediate (Erick Wendel) [#49397](https://github.com/nodejs/node/pull/49397)
* \[[`23216f1935`](https://github.com/nodejs/node/commit/23216f1935)] - **test\_runner**: report covered lines, functions and branches to reporters (Phil Nash) [#49320](https://github.com/nodejs/node/pull/49320)
* \[[`283f2806b1`](https://github.com/nodejs/node/commit/283f2806b1)] - **test\_runner**: expose spec reporter as newable function (Chemi Atlow) [#49184](https://github.com/nodejs/node/pull/49184)
* \[[`546ad5f770`](https://github.com/nodejs/node/commit/546ad5f770)] - **test\_runner**: reland run global after() hook earlier (Colin Ihrig) [#49116](https://github.com/nodejs/node/pull/49116)
* \[[`efdc95fbc0`](https://github.com/nodejs/node/commit/efdc95fbc0)] - **(SEMVER-MINOR)** **test\_runner**: expose location of tests (Colin Ihrig) [#48975](https://github.com/nodejs/node/pull/48975)
* \[[`4bc0a8fe99`](https://github.com/nodejs/node/commit/4bc0a8fe99)] - **test\_runner**: fix global after not failing the tests (Raz Luvaton) [#48913](https://github.com/nodejs/node/pull/48913)
* \[[`08738b2664`](https://github.com/nodejs/node/commit/08738b2664)] - **test\_runner**: fix timeout in \*Each hook failing further tests (Raz Luvaton) [#48925](https://github.com/nodejs/node/pull/48925)
* \[[`c2f1830f66`](https://github.com/nodejs/node/commit/c2f1830f66)] - **test\_runner**: cleanup test timeout abort listener (Raz Luvaton) [#48915](https://github.com/nodejs/node/pull/48915)
* \[[`75333f38b2`](https://github.com/nodejs/node/commit/75333f38b2)] - **test\_runner**: fix global before not called when no global test exists (Raz Luvaton) [#48877](https://github.com/nodejs/node/pull/48877)
* \[[`b28b85adf8`](https://github.com/nodejs/node/commit/b28b85adf8)] - **tls**: remove redundant code in onConnectSecure() (Deokjin Kim) [#49457](https://github.com/nodejs/node/pull/49457)
* \[[`83fc4dccbc`](https://github.com/nodejs/node/commit/83fc4dccbc)] - **tls**: refactor to use validateFunction (Deokjin Kim) [#49422](https://github.com/nodejs/node/pull/49422)
* \[[`8949cc79dd`](https://github.com/nodejs/node/commit/8949cc79dd)] - **tls**: ensure TLS Sockets are closed if the underlying wrap closes (Tim Perry) [#49327](https://github.com/nodejs/node/pull/49327)
* \[[`1df56e6f01`](https://github.com/nodejs/node/commit/1df56e6f01)] - **tools**: update eslint to 8.48.0 (Node.js GitHub Bot) [#49343](https://github.com/nodejs/node/pull/49343)
* \[[`ef50ec5b57`](https://github.com/nodejs/node/commit/ef50ec5b57)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#49342](https://github.com/nodejs/node/pull/49342)
* \[[`9a8fb4fc34`](https://github.com/nodejs/node/commit/9a8fb4fc34)] - **tools**: remove v8\_dump\_build\_config action (Cheng Zhao) [#49301](https://github.com/nodejs/node/pull/49301)
* \[[`91b2d4314b`](https://github.com/nodejs/node/commit/91b2d4314b)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#49253](https://github.com/nodejs/node/pull/49253)
* \[[`b51946ebdd`](https://github.com/nodejs/node/commit/b51946ebdd)] - **tools**: fix github reporter appended multiple times (Moshe Atlow) [#49199](https://github.com/nodejs/node/pull/49199)
* \[[`ae40cb1612`](https://github.com/nodejs/node/commit/ae40cb1612)] - **url**: validate `pathToFileURL(path)` argument as string (LiviaMedeiros) [#49161](https://github.com/nodejs/node/pull/49161)
* \[[`e787673dcf`](https://github.com/nodejs/node/commit/e787673dcf)] - **url**: handle unicode hostname if empty (Yagiz Nizipli) [#49396](https://github.com/nodejs/node/pull/49396)
* \[[`6ee74be87f`](https://github.com/nodejs/node/commit/6ee74be87f)] - **vm**: store MicrotaskQueue in ContextifyContext directly (Joyee Cheung) [#48982](https://github.com/nodejs/node/pull/48982)
* \[[`0179c6dc8f`](https://github.com/nodejs/node/commit/0179c6dc8f)] - **worker**: protect against user mutating well-known prototypes (Antoine du Hamel) [#49270](https://github.com/nodejs/node/pull/49270)

<a id="20.6.1"></a>

## 2023-09-08, Version 20.6.1 (Current), @RafaelGSS

### Commits

* \[[`8acbe6d8e8`](https://github.com/nodejs/node/commit/8acbe6d8e8)] - **esm**: fix loading of CJS modules from ESM (Antoine du Hamel) [#49500](https://github.com/nodejs/node/pull/49500)

<a id="20.6.0"></a>

## 2023-09-04, Version 20.6.0 (Current), @juanarbol prepared by @UlisesGascon

### Notable changes

#### built-in `.env` file support

Starting from Node.js v20.6.0, Node.js supports `.env` files for configuring environment variables.

Your configuration file should follow the INI file format, with each line containing a key-value pair for an environment variable.
To initialize your Node.js application with predefined configurations, use the following CLI command: `node --env-file=config.env index.js`.

For example, you can access the following environment variable using `process.env.PASSWORD` when your application is initialized:

```text
PASSWORD=nodejs
```

In addition to environment variables, this change allows you to define your `NODE_OPTIONS` directly in the `.env` file, eliminating the need to include it in your `package.json`.

This feature was contributed by Yagiz Nizipli in [#48890](https://github.com/nodejs/node/pull/48890).

#### `import.meta.resolve` unflagged

In ES modules, [`import.meta.resolve(specifier)`](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/import.meta/resolve) can be used to get an absolute URL string to which `specifier` resolves, similar to `require.resolve` in CommonJS. This aligns Node.js with browsers and other server-side runtimes.

This feature was contributed by Guy Bedford in <https://github.com/nodejs/node/pull/49028>

#### New `node:module` API `register` for module customization hooks; new `initialize` hook

There is a new API `register` available on `node:module` to specify a file that exports module customization hooks, and pass data to the hooks, and establish communication channels with them. The “define the file with the hooks” part was previously handled by a flag `--experimental-loader`, but when the hooks moved into a dedicated thread in 20.0.0 there was a need to provide a way to communicate between the main (application) thread and the hooks thread. This can now be done by calling `register` from the main thread and passing data, including `MessageChannel` instances.

We encourage users to migrate to an approach that uses [`--import`](https://nodejs.org/api/cli.html#--importmodule) with `register`, such as:

```bash
node --import ./file-that-calls-register.js ./app.js
```

Using `--import` ensures that the customization hooks are registered before any application code runs, even the entry point.

This feature was contributed by João Lenon and Jacob Smith in <https://github.com/nodejs/node/pull/46826>, Izaak Schroeder and Jacob Smith in <https://github.com/nodejs/node/pull/48842> and <https://github.com/nodejs/node/pull/48559>

#### Module customization `load` hook can now support CommonJS

Authors of module customization hooks can how handle both ES module and CommonJS sources in the `load` hook. This works for CommonJS modules referenced via either `import` or `require`, so long as [the main entry point of the application is handled by the ES module loader](https://nodejs.org/api/cli.html#program-entry-point) (such as because the entry point is an ES module file, or if the `--import` flag is passed). This should simplify the customization of the Node.js module loading process, as package authors can customize more of Node.js without relying on deprecated APIs such as `require.extensions`.

This feature was contributed by Antoine du Hamel in <https://github.com/nodejs/node/pull/47999>

#### Node.js C++ addons now have experimental support for cppgc (Oilpan), a C++ garbage collection library in V8.

Now when Node.js starts up, it makes sure that there is a `v8::CppHeap` attached to the V8 isolate. This enables users to allocate in the `v8::CppHeap` using `<cppgc/*>` headers from V8, which are now also included into the Node.js headers available to addons. Note that since Node.js only bundles the cppgc library coming from V8, [the ABI stability](https://nodejs.org/en/docs/guides/abi-stability#abi-stability-in-nodejs) of cppgc is currently not guaranteed in semver-minor and -patch updates, but we do not expect the ABI to break often, as it has been stable and battle-tested in Chromium for years. We may consider including cppgc into the ABI stability guarantees when it gets enough adoption internally and externally.

To help addon authors create JavaScript-to-C++ references of which V8's garbage collector can be aware, a helper function [`node::SetCppgcReference(isolate, js_object, cppgc_object)`](https://github.com/nodejs/node/blob/v20.6.0/test/addons/cppgc-object/binding.cc) has been added to `node.h`. V8 may provide a native alternative in the future, which could then replace this Node.js-specific helper. In the mean time, users can use this API to avoid having to hard-code the layout of JavaScript wrapper objects. An example of how to create garbage-collected C++ objects in the unified heap and wrap it in a JavaScript object can be found in the [Node.js addon tests](https://github.com/nodejs/node/blob/v20.6.0/test/addons/cppgc-object/binding.cc).

The existing `node::ObjectWrap` helper would continue to work, while cppgc-based object management serves as an alternative with some advantages mentioned in [the V8 blog post about Oilpan](https://v8.dev/blog/oilpan-library).

This feature was contributed by Daryl Haresign and Joyee Cheung in <https://github.com/nodejs/node/pull/48660> and <https://github.com/nodejs/node/pull/45704>.

#### Other notable changes

* \[[`d6862b085c`](https://github.com/nodejs/node/commit/d6862b085c)] - **deps**: V8: cherry-pick 93275031284c (Joyee Cheung) [#48660](https://github.com/nodejs/node/pull/48660)
* \[[`00fc8bb8b3`](https://github.com/nodejs/node/commit/00fc8bb8b3)] - **doc**: add rluvaton to collaborators (Raz Luvaton) [#49215](https://github.com/nodejs/node/pull/49215)
* \[[`d649339abd`](https://github.com/nodejs/node/commit/d649339abd)] - **doc**: add new TSC members (Michael Dawson) [#48841](https://github.com/nodejs/node/pull/48841)
* \[[`67f9896247`](https://github.com/nodejs/node/commit/67f9896247)] - **(SEMVER-MINOR)** **inspector**: open add `SymbolDispose` (Chemi Atlow) [#48765](https://github.com/nodejs/node/pull/48765)
* \[[`5aef593db3`](https://github.com/nodejs/node/commit/5aef593db3)] - **module**: implement `register` utility (João Lenon) [#46826](https://github.com/nodejs/node/pull/46826)

### Commits

* \[[`771abcb5da`](https://github.com/nodejs/node/commit/771abcb5da)] - **benchmark**: add benchmarks for the test\_runner (Raz Luvaton) [#48931](https://github.com/nodejs/node/pull/48931)
* \[[`6b27bb0dab`](https://github.com/nodejs/node/commit/6b27bb0dab)] - **benchmark**: add pm startup benchmark (Rafael Gonzaga) [#48905](https://github.com/nodejs/node/pull/48905)
* \[[`1f35c0ca55`](https://github.com/nodejs/node/commit/1f35c0ca55)] - **child\_process**: harden against prototype pollution (Livia Medeiros) [#48726](https://github.com/nodejs/node/pull/48726)
* \[[`d6862b085c`](https://github.com/nodejs/node/commit/d6862b085c)] - **deps**: V8: cherry-pick 93275031284c (Joyee Cheung) [#48660](https://github.com/nodejs/node/pull/48660)
* \[[`f71e383948`](https://github.com/nodejs/node/commit/f71e383948)] - **deps**: update simdutf to 3.2.17 (Node.js GitHub Bot) [#49019](https://github.com/nodejs/node/pull/49019)
* \[[`e14f0456ae`](https://github.com/nodejs/node/commit/e14f0456ae)] - **deps**: update googletest to 7e33b6a (Node.js GitHub Bot) [#49034](https://github.com/nodejs/node/pull/49034)
* \[[`bfaa0fb500`](https://github.com/nodejs/node/commit/bfaa0fb500)] - **deps**: update zlib to 1.2.13.1-motley-526382e (Node.js GitHub Bot) [#49033](https://github.com/nodejs/node/pull/49033)
* \[[`b79c652c85`](https://github.com/nodejs/node/commit/b79c652c85)] - **deps**: update undici to 5.23.0 (Node.js GitHub Bot) [#49021](https://github.com/nodejs/node/pull/49021)
* \[[`6ead86145c`](https://github.com/nodejs/node/commit/6ead86145c)] - **deps**: update googletest to c875c4e (Node.js GitHub Bot) [#48964](https://github.com/nodejs/node/pull/48964)
* \[[`4b0e50501e`](https://github.com/nodejs/node/commit/4b0e50501e)] - **deps**: update ada to 2.6.0 (Node.js GitHub Bot) [#48896](https://github.com/nodejs/node/pull/48896)
* \[[`d960ee0ba3`](https://github.com/nodejs/node/commit/d960ee0ba3)] - **deps**: upgrade npm to 9.8.1 (npm team) [#48838](https://github.com/nodejs/node/pull/48838)
* \[[`d92b0139ca`](https://github.com/nodejs/node/commit/d92b0139ca)] - **deps**: update zlib to 1.2.13.1-motley-61dc0bd (Node.js GitHub Bot) [#48788](https://github.com/nodejs/node/pull/48788)
* \[[`2a7835c376`](https://github.com/nodejs/node/commit/2a7835c376)] - **deps**: V8: cherry-pick 9f4b7699f68e (Joyee Cheung) [#48830](https://github.com/nodejs/node/pull/48830)
* \[[`c8e17829ac`](https://github.com/nodejs/node/commit/c8e17829ac)] - **deps**: V8: cherry-pick c1a54d5ffcd1 (Joyee Cheung) [#48830](https://github.com/nodejs/node/pull/48830)
* \[[`318e075b6f`](https://github.com/nodejs/node/commit/318e075b6f)] - **deps**: update googletest to cc36671 (Node.js GitHub Bot) [#48789](https://github.com/nodejs/node/pull/48789)
* \[[`114e088267`](https://github.com/nodejs/node/commit/114e088267)] - **diagnostics\_channel**: fix last subscriber removal (Gabriel Schulhof) [#48933](https://github.com/nodejs/node/pull/48933)
* \[[`00fc8bb8b3`](https://github.com/nodejs/node/commit/00fc8bb8b3)] - **doc**: add rluvaton to collaborators (Raz Luvaton) [#49215](https://github.com/nodejs/node/pull/49215)
* \[[`21949c45b6`](https://github.com/nodejs/node/commit/21949c45b6)] - **doc**: add print results for examples in `WebStreams` (Jungku Lee) [#49143](https://github.com/nodejs/node/pull/49143)
* \[[`032107a6fe`](https://github.com/nodejs/node/commit/032107a6fe)] - **doc**: fix `Type` notation in webstreams (Deokjin Kim) [#49121](https://github.com/nodejs/node/pull/49121)
* \[[`91d41e7c5a`](https://github.com/nodejs/node/commit/91d41e7c5a)] - **doc**: fix name of the flag in `initialize()` docs (Antoine du Hamel) [#49158](https://github.com/nodejs/node/pull/49158)
* \[[`aa4caf810e`](https://github.com/nodejs/node/commit/aa4caf810e)] - **doc**: make the NODE\_VERSION\_IS\_RELEASE revert clear (Rafael Gonzaga) [#49114](https://github.com/nodejs/node/pull/49114)
* \[[`f888a1dbe3`](https://github.com/nodejs/node/commit/f888a1dbe3)] - **doc**: update process.binding deprecation text (Tobias Nießen) [#49086](https://github.com/nodejs/node/pull/49086)
* \[[`89fa3faf92`](https://github.com/nodejs/node/commit/89fa3faf92)] - **doc**: update with latest security release (Rafael Gonzaga) [#49085](https://github.com/nodejs/node/pull/49085)
* \[[`3d36e7a941`](https://github.com/nodejs/node/commit/3d36e7a941)] - **doc**: add description for `--port` flag of `node inspect` (Michael Bianco) [#48785](https://github.com/nodejs/node/pull/48785)
* \[[`e9d9ca12a3`](https://github.com/nodejs/node/commit/e9d9ca12a3)] - **doc**: add missing period (Rich Trott) [#49094](https://github.com/nodejs/node/pull/49094)
* \[[`7e7b554de0`](https://github.com/nodejs/node/commit/7e7b554de0)] - **doc**: add ESM examples in http.md (btea) [#47763](https://github.com/nodejs/node/pull/47763)
* \[[`48f8ccfd54`](https://github.com/nodejs/node/commit/48f8ccfd54)] - **doc**: detailed description of keystrokes Ctrl-Y and Meta-Y (Ray) [#43529](https://github.com/nodejs/node/pull/43529)
* \[[`195885c8f8`](https://github.com/nodejs/node/commit/195885c8f8)] - **doc**: add "type" to test runner event details (Phil Nash) [#49014](https://github.com/nodejs/node/pull/49014)
* \[[`6ce25f8415`](https://github.com/nodejs/node/commit/6ce25f8415)] - **doc**: reserve 118 for Electron 27 (David Sanders) [#49023](https://github.com/nodejs/node/pull/49023)
* \[[`9c26c0f296`](https://github.com/nodejs/node/commit/9c26c0f296)] - **doc**: clarify use of process.env in worker threads on Windows (Daeyeon Jeong) [#49008](https://github.com/nodejs/node/pull/49008)
* \[[`7186e02aa0`](https://github.com/nodejs/node/commit/7186e02aa0)] - **doc**: remove v14 mention (Rafael Gonzaga) [#49005](https://github.com/nodejs/node/pull/49005)
* \[[`9641ac6c65`](https://github.com/nodejs/node/commit/9641ac6c65)] - **doc**: drop github actions check in sec release process (Rafael Gonzaga) [#48978](https://github.com/nodejs/node/pull/48978)
* \[[`f3d62abb19`](https://github.com/nodejs/node/commit/f3d62abb19)] - **doc**: improved joinDuplicateHeaders definition (Matteo Bianchi) [#48859](https://github.com/nodejs/node/pull/48859)
* \[[`0db104a08b`](https://github.com/nodejs/node/commit/0db104a08b)] - **doc**: fix second parameter name of `events.addAbortListener` (Deokjin Kim) [#48922](https://github.com/nodejs/node/pull/48922)
* \[[`5173c559b7`](https://github.com/nodejs/node/commit/5173c559b7)] - **doc**: add new reporter events to custom reporter examples (Chemi Atlow) [#48903](https://github.com/nodejs/node/pull/48903)
* \[[`660da785e6`](https://github.com/nodejs/node/commit/660da785e6)] - **doc**: run license-builder (github-actions\[bot]) [#48898](https://github.com/nodejs/node/pull/48898)
* \[[`092f9fe92a`](https://github.com/nodejs/node/commit/092f9fe92a)] - **doc**: change duration to duration\_ms on test documentation (Ardi\_Nugraha) [#48892](https://github.com/nodejs/node/pull/48892)
* \[[`5e4730858d`](https://github.com/nodejs/node/commit/5e4730858d)] - **doc**: improve requireHostHeader (Guido Penta) [#48860](https://github.com/nodejs/node/pull/48860)
* \[[`045e3c549a`](https://github.com/nodejs/node/commit/045e3c549a)] - **doc**: add ver of 18.x where Node-api 9 is supported (Michael Dawson) [#48876](https://github.com/nodejs/node/pull/48876)
* \[[`c20d35df34`](https://github.com/nodejs/node/commit/c20d35df34)] - **doc**: include experimental features assessment (Rafael Gonzaga) [#48824](https://github.com/nodejs/node/pull/48824)
* \[[`d649339abd`](https://github.com/nodejs/node/commit/d649339abd)] - **doc**: add new TSC members (Michael Dawson) [#48841](https://github.com/nodejs/node/pull/48841)
* \[[`aeac327f2b`](https://github.com/nodejs/node/commit/aeac327f2b)] - **doc**: refactor node-api support matrix (Michael Dawson) [#48774](https://github.com/nodejs/node/pull/48774)
* \[[`388c7d9232`](https://github.com/nodejs/node/commit/388c7d9232)] - **doc**: declare `path` on example of `async_hooks.executionAsyncId()` (Deokjin Kim) [#48556](https://github.com/nodejs/node/pull/48556)
* \[[`fe20528c8e`](https://github.com/nodejs/node/commit/fe20528c8e)] - **doc**: remove the . in the end to reduce confusing (Jason) [#48719](https://github.com/nodejs/node/pull/48719)
* \[[`e69c8e173f`](https://github.com/nodejs/node/commit/e69c8e173f)] - **doc**: nodejs-social over nodejs/tweet (Rafael Gonzaga) [#48769](https://github.com/nodejs/node/pull/48769)
* \[[`ea547849fd`](https://github.com/nodejs/node/commit/ea547849fd)] - **doc**: expand on squashing and rebasing to land a PR (Chengzhong Wu) [#48751](https://github.com/nodejs/node/pull/48751)
* \[[`31442b96a5`](https://github.com/nodejs/node/commit/31442b96a5)] - **esm**: fix `globalPreload` warning (Antoine du Hamel) [#49069](https://github.com/nodejs/node/pull/49069)
* \[[`eb1215878b`](https://github.com/nodejs/node/commit/eb1215878b)] - **esm**: unflag import.meta.resolve (Guy Bedford) [#49028](https://github.com/nodejs/node/pull/49028)
* \[[`57b24a34e6`](https://github.com/nodejs/node/commit/57b24a34e6)] - **esm**: import.meta.resolve exact module not found errors should return (Guy Bedford) [#49038](https://github.com/nodejs/node/pull/49038)
* \[[`f23b2a3066`](https://github.com/nodejs/node/commit/f23b2a3066)] - **esm**: protect `ERR_UNSUPPORTED_DIR_IMPORT` against prototype pollution (Antoine du Hamel) [#49060](https://github.com/nodejs/node/pull/49060)
* \[[`386e826a56`](https://github.com/nodejs/node/commit/386e826a56)] - **esm**: add `initialize` hook, integrate with `register` (Izaak Schroeder) [#48842](https://github.com/nodejs/node/pull/48842)
* \[[`74a2e1e0ab`](https://github.com/nodejs/node/commit/74a2e1e0ab)] - **esm**: fix typo `parentUrl` -> `parentURL` (Antoine du Hamel) [#48999](https://github.com/nodejs/node/pull/48999)
* \[[`0a4f7c669a`](https://github.com/nodejs/node/commit/0a4f7c669a)] - **esm**: unflag `Module.register` and allow nested loader `import()` (Izaak Schroeder) [#48559](https://github.com/nodejs/node/pull/48559)
* \[[`a5597470ce`](https://github.com/nodejs/node/commit/a5597470ce)] - **esm**: add back `globalPreload` tests and fix failing ones (Antoine du Hamel) [#48779](https://github.com/nodejs/node/pull/48779)
* \[[`d568600b42`](https://github.com/nodejs/node/commit/d568600b42)] - **events**: remove weak listener for event target (Raz Luvaton) [#48952](https://github.com/nodejs/node/pull/48952)
* \[[`3d942d9842`](https://github.com/nodejs/node/commit/3d942d9842)] - **fs**: fix readdir recursive sync & callback (Ethan Arrowood) [#48698](https://github.com/nodejs/node/pull/48698)
* \[[`c14ff69d69`](https://github.com/nodejs/node/commit/c14ff69d69)] - **fs**: mention `URL` in NUL character error message (LiviaMedeiros) [#48828](https://github.com/nodejs/node/pull/48828)
* \[[`d634d781d7`](https://github.com/nodejs/node/commit/d634d781d7)] - **fs**: make `mkdtemp` accept buffers and URL (LiviaMedeiros) [#48828](https://github.com/nodejs/node/pull/48828)
* \[[`4515a285a4`](https://github.com/nodejs/node/commit/4515a285a4)] - **fs**: remove redundant `nullCheck` (Livia Medeiros) [#48826](https://github.com/nodejs/node/pull/48826)
* \[[`742597b14a`](https://github.com/nodejs/node/commit/742597b14a)] - **http**: start connections checking interval on listen (Paolo Insogna) [#48611](https://github.com/nodejs/node/pull/48611)
* \[[`67f9896247`](https://github.com/nodejs/node/commit/67f9896247)] - **(SEMVER-MINOR)** **inspector**: open add `SymbolDispose` (Chemi Atlow) [#48765](https://github.com/nodejs/node/pull/48765)
* \[[`b66a3c1c96`](https://github.com/nodejs/node/commit/b66a3c1c96)] - **lib**: fix MIME overmatch in data URLs (André Alves) [#49104](https://github.com/nodejs/node/pull/49104)
* \[[`dca8678a22`](https://github.com/nodejs/node/commit/dca8678a22)] - **lib**: fix to add resolve() before return at Blob.stream()'s source.pull() (bellbind) [#48935](https://github.com/nodejs/node/pull/48935)
* \[[`420b85c00f`](https://github.com/nodejs/node/commit/420b85c00f)] - **lib**: remove invalid parameter to toASCII (Yagiz Nizipli) [#48878](https://github.com/nodejs/node/pull/48878)
* \[[`a12ce11b09`](https://github.com/nodejs/node/commit/a12ce11b09)] - **lib,permission**: drop repl autocomplete when pm enabled (Rafael Gonzaga) [#48920](https://github.com/nodejs/node/pull/48920)
* \[[`458eaf5e75`](https://github.com/nodejs/node/commit/458eaf5e75)] - **meta**: bump github/codeql-action from 2.20.1 to 2.21.2 (dependabot\[bot]) [#48986](https://github.com/nodejs/node/pull/48986)
* \[[`4f88cb10e0`](https://github.com/nodejs/node/commit/4f88cb10e0)] - **meta**: bump step-security/harden-runner from 2.4.1 to 2.5.0 (dependabot\[bot]) [#48985](https://github.com/nodejs/node/pull/48985)
* \[[`22fc2a6ec6`](https://github.com/nodejs/node/commit/22fc2a6ec6)] - **meta**: bump actions/setup-node from 3.6.0 to 3.7.0 (dependabot\[bot]) [#48984](https://github.com/nodejs/node/pull/48984)
* \[[`40103adabd`](https://github.com/nodejs/node/commit/40103adabd)] - **meta**: bump actions/setup-python from 4.6.1 to 4.7.0 (dependabot\[bot]) [#48983](https://github.com/nodejs/node/pull/48983)
* \[[`84c0c6848c`](https://github.com/nodejs/node/commit/84c0c6848c)] - **meta**: add mailmap entry for atlowChemi (Chemi Atlow) [#48810](https://github.com/nodejs/node/pull/48810)
* \[[`1a6e9450b8`](https://github.com/nodejs/node/commit/1a6e9450b8)] - **module**: make CJS load from ESM loader (Antoine du Hamel) [#47999](https://github.com/nodejs/node/pull/47999)
* \[[`a5322c4b4a`](https://github.com/nodejs/node/commit/a5322c4b4a)] - **module**: ensure successful import returns the same result (Antoine du Hamel) [#46662](https://github.com/nodejs/node/pull/46662)
* \[[`5aef593db3`](https://github.com/nodejs/node/commit/5aef593db3)] - **module**: implement `register` utility (João Lenon) [#46826](https://github.com/nodejs/node/pull/46826)
* \[[`015c4f788d`](https://github.com/nodejs/node/commit/015c4f788d)] - **node-api**: avoid macro redefinition (Tobias Nießen) [#48879](https://github.com/nodejs/node/pull/48879)
* \[[`53ee98566b`](https://github.com/nodejs/node/commit/53ee98566b)] - **permission**: move PrintTree into unnamed namespace (Tobias Nießen) [#48874](https://github.com/nodejs/node/pull/48874)
* \[[`30ea480135`](https://github.com/nodejs/node/commit/30ea480135)] - **permission**: fix data types in PrintTree (Tobias Nießen) [#48770](https://github.com/nodejs/node/pull/48770)
* \[[`8380800375`](https://github.com/nodejs/node/commit/8380800375)] - **readline**: add paste bracket mode (Jakub Jankiewicz) [#47150](https://github.com/nodejs/node/pull/47150)
* \[[`bc009d0c10`](https://github.com/nodejs/node/commit/bc009d0c10)] - **sea**: add support for V8 bytecode-only caching (Darshan Sen) [#48191](https://github.com/nodejs/node/pull/48191)
* \[[`f2f4ce9e29`](https://github.com/nodejs/node/commit/f2f4ce9e29)] - **src**: use effective cppgc wrapper id to deduce non-cppgc id (Joyee Cheung) [#48660](https://github.com/nodejs/node/pull/48660)
* \[[`bf7ff369f6`](https://github.com/nodejs/node/commit/bf7ff369f6)] - **src**: add built-in `.env` file support (Yagiz Nizipli) [#48890](https://github.com/nodejs/node/pull/48890)
* \[[`8d6948f8e2`](https://github.com/nodejs/node/commit/8d6948f8e2)] - **src**: remove duplicated code in `GenerateSingleExecutableBlob()` (Jungku Lee) [#49119](https://github.com/nodejs/node/pull/49119)
* \[[`b030004cee`](https://github.com/nodejs/node/commit/b030004cee)] - **src**: refactor vector writing in snapshot builder (Joyee Cheung) [#48851](https://github.com/nodejs/node/pull/48851)
* \[[`497df8288d`](https://github.com/nodejs/node/commit/497df8288d)] - **src**: add ability to overload fast api functions (Yagiz Nizipli) [#48993](https://github.com/nodejs/node/pull/48993)
* \[[`e5b0dfa359`](https://github.com/nodejs/node/commit/e5b0dfa359)] - **src**: remove redundant code for uv\_handle\_type (Jungku Lee) [#49061](https://github.com/nodejs/node/pull/49061)
* \[[`f126b9e3d1`](https://github.com/nodejs/node/commit/f126b9e3d1)] - **src**: modernize use-equals-default (Jason) [#48735](https://github.com/nodejs/node/pull/48735)
* \[[`db4370fc3e`](https://github.com/nodejs/node/commit/db4370fc3e)] - **src**: avoid string copy in BuiltinLoader::GetBuiltinIds (Yagiz Nizipli) [#48721](https://github.com/nodejs/node/pull/48721)
* \[[`9d13503c4e`](https://github.com/nodejs/node/commit/9d13503c4e)] - **src**: fix callback\_queue.h missing header (Jason) [#48733](https://github.com/nodejs/node/pull/48733)
* \[[`6c389df3aa`](https://github.com/nodejs/node/commit/6c389df3aa)] - **src**: cast v8::Object::GetInternalField() return value to v8::Value (Joyee Cheung) [#48943](https://github.com/nodejs/node/pull/48943)
* \[[`7b9adff0be`](https://github.com/nodejs/node/commit/7b9adff0be)] - **src**: do not pass user input to format string (Antoine du Hamel) [#48973](https://github.com/nodejs/node/pull/48973)
* \[[`e0fdb7b092`](https://github.com/nodejs/node/commit/e0fdb7b092)] - **src**: remove ContextEmbedderIndex::kBindingDataStoreIndex (Joyee Cheung) [#48836](https://github.com/nodejs/node/pull/48836)
* \[[`578c3d1e14`](https://github.com/nodejs/node/commit/578c3d1e14)] - **src**: use ARES\_SUCCESS instead of 0 (Hyunjin Kim) [#48834](https://github.com/nodejs/node/pull/48834)
* \[[`ed23426aac`](https://github.com/nodejs/node/commit/ed23426aac)] - **src**: save the performance milestone time origin in the AliasedArray (Joyee Cheung) [#48708](https://github.com/nodejs/node/pull/48708)
* \[[`5dec186663`](https://github.com/nodejs/node/commit/5dec186663)] - **src**: support snapshot in single executable applications (Joyee Cheung) [#46824](https://github.com/nodejs/node/pull/46824)
* \[[`d759d4f631`](https://github.com/nodejs/node/commit/d759d4f631)] - **src**: remove unnecessary temporary creation (Jason) [#48734](https://github.com/nodejs/node/pull/48734)
* \[[`409cc692db`](https://github.com/nodejs/node/commit/409cc692db)] - **src**: fix nullptr access on realm (Jan Olaf Krems) [#48802](https://github.com/nodejs/node/pull/48802)
* \[[`07d0fd61b1`](https://github.com/nodejs/node/commit/07d0fd61b1)] - **src**: remove OnScopeLeaveImpl's move assignment overload (Jason) [#48732](https://github.com/nodejs/node/pull/48732)
* \[[`41cc3efa23`](https://github.com/nodejs/node/commit/41cc3efa23)] - **src**: use string\_view for utf-8 string creation (Yagiz Nizipli) [#48722](https://github.com/nodejs/node/pull/48722)
* \[[`62a46d9335`](https://github.com/nodejs/node/commit/62a46d9335)] - **src,permission**: restrict by default when pm enabled (Rafael Gonzaga) [#48907](https://github.com/nodejs/node/pull/48907)
* \[[`099159ce04`](https://github.com/nodejs/node/commit/099159ce04)] - **src,tools**: initialize cppgc (Daryl Haresign) [#48660](https://github.com/nodejs/node/pull/48660)
* \[[`600c08d197`](https://github.com/nodejs/node/commit/600c08d197)] - **stream**: improve WebStreams performance (Raz Luvaton) [#49089](https://github.com/nodejs/node/pull/49089)
* \[[`609b25fa99`](https://github.com/nodejs/node/commit/609b25fa99)] - **stream**: implement ReadableStream.from (Debadree Chatterjee) [#48395](https://github.com/nodejs/node/pull/48395)
* \[[`750cca2738`](https://github.com/nodejs/node/commit/750cca2738)] - **test**: use `tmpdir.resolve()` (Livia Medeiros) [#49128](https://github.com/nodejs/node/pull/49128)
* \[[`6595367649`](https://github.com/nodejs/node/commit/6595367649)] - **test**: use `tmpdir.resolve()` (Livia Medeiros) [#49127](https://github.com/nodejs/node/pull/49127)
* \[[`661b055e75`](https://github.com/nodejs/node/commit/661b055e75)] - **test**: use `tmpdir.resolve()` in fs tests (Livia Medeiros) [#49126](https://github.com/nodejs/node/pull/49126)
* \[[`b3c56d206f`](https://github.com/nodejs/node/commit/b3c56d206f)] - **test**: use `tmpdir.resolve()` in fs tests (Livia Medeiros) [#49125](https://github.com/nodejs/node/pull/49125)
* \[[`3ddb155d16`](https://github.com/nodejs/node/commit/3ddb155d16)] - **test**: fix assertion message in test\_async.c (Tobias Nießen) [#49146](https://github.com/nodejs/node/pull/49146)
* \[[`1d17c1032d`](https://github.com/nodejs/node/commit/1d17c1032d)] - **test**: refactor `test-esm-loader-hooks` for easier debugging (Antoine du Hamel) [#49131](https://github.com/nodejs/node/pull/49131)
* \[[`13bd7a0293`](https://github.com/nodejs/node/commit/13bd7a0293)] - **test**: add `tmpdir.resolve()` (Livia Medeiros) [#49079](https://github.com/nodejs/node/pull/49079)
* \[[`89b1bce56d`](https://github.com/nodejs/node/commit/89b1bce56d)] - **test**: document `fixtures.fileURL()` (Livia Medeiros) [#49083](https://github.com/nodejs/node/pull/49083)
* \[[`2fcb855c76`](https://github.com/nodejs/node/commit/2fcb855c76)] - **test**: reduce flakiness of `test-esm-loader-hooks` (Antoine du Hamel) [#49105](https://github.com/nodejs/node/pull/49105)
* \[[`7816e040df`](https://github.com/nodejs/node/commit/7816e040df)] - **test**: stabilize the inspector-open-dispose test (Chemi Atlow) [#49000](https://github.com/nodejs/node/pull/49000)
* \[[`e70e9747e4`](https://github.com/nodejs/node/commit/e70e9747e4)] - **test**: print instruction for creating missing snapshot in assertSnapshot (Raz Luvaton) [#48914](https://github.com/nodejs/node/pull/48914)
* \[[`669ac03520`](https://github.com/nodejs/node/commit/669ac03520)] - **test**: add `tmpdir.fileURL()` (Livia Medeiros) [#49040](https://github.com/nodejs/node/pull/49040)
* \[[`b945d7be35`](https://github.com/nodejs/node/commit/b945d7be35)] - **test**: use `spawn` and `spawnPromisified` instead of `exec` (Antoine du Hamel) [#48991](https://github.com/nodejs/node/pull/48991)
* \[[`b3a7427583`](https://github.com/nodejs/node/commit/b3a7427583)] - **test**: refactor `test-node-output-errors` (Antoine du Hamel) [#48992](https://github.com/nodejs/node/pull/48992)
* \[[`6c3e5c4d69`](https://github.com/nodejs/node/commit/6c3e5c4d69)] - **test**: use `fixtures.fileURL` when appropriate (Antoine du Hamel) [#48990](https://github.com/nodejs/node/pull/48990)
* \[[`9138b78bcb`](https://github.com/nodejs/node/commit/9138b78bcb)] - **test**: validate error code rather than message (Antoine du Hamel) [#48972](https://github.com/nodejs/node/pull/48972)
* \[[`b4ca4a6f80`](https://github.com/nodejs/node/commit/b4ca4a6f80)] - **test**: fix snapshot tests when cwd contains spaces or backslashes (Antoine du Hamel) [#48959](https://github.com/nodejs/node/pull/48959)
* \[[`d4398d458c`](https://github.com/nodejs/node/commit/d4398d458c)] - **test**: order `common.mjs` in ASCII order (Antoine du Hamel) [#48960](https://github.com/nodejs/node/pull/48960)
* \[[`b5991f5250`](https://github.com/nodejs/node/commit/b5991f5250)] - **test**: fix some assumptions in tests (Antoine du Hamel) [#48958](https://github.com/nodejs/node/pull/48958)
* \[[`62e23f83f9`](https://github.com/nodejs/node/commit/62e23f83f9)] - **test**: improve internal/worker/io.js coverage (Yoshiki Kurihara) [#42387](https://github.com/nodejs/node/pull/42387)
* \[[`314bd6095c`](https://github.com/nodejs/node/commit/314bd6095c)] - **test**: fix `es-module/test-esm-initialization` (Antoine du Hamel) [#48880](https://github.com/nodejs/node/pull/48880)
* \[[`3680a66df4`](https://github.com/nodejs/node/commit/3680a66df4)] - **test**: validate host with commas on url.parse (Yagiz Nizipli) [#48878](https://github.com/nodejs/node/pull/48878)
* \[[`24c3742372`](https://github.com/nodejs/node/commit/24c3742372)] - **test**: delete test-net-bytes-per-incoming-chunk-overhead (Michaël Zasso) [#48811](https://github.com/nodejs/node/pull/48811)
* \[[`e01cce50f5`](https://github.com/nodejs/node/commit/e01cce50f5)] - **test**: skip experimental test with pointer compression (Colin Ihrig) [#48738](https://github.com/nodejs/node/pull/48738)
* \[[`d5e93b1074`](https://github.com/nodejs/node/commit/d5e93b1074)] - **test**: fix flaky test-string-decode.js on x86 (Stefan Stojanovic) [#48750](https://github.com/nodejs/node/pull/48750)
* \[[`9136667d7d`](https://github.com/nodejs/node/commit/9136667d7d)] - **test\_runner**: dont set exit code on todo tests (Moshe Atlow) [#48929](https://github.com/nodejs/node/pull/48929)
* \[[`52c94908c0`](https://github.com/nodejs/node/commit/52c94908c0)] - **test\_runner**: fix todo and only in spec reporter (Moshe Atlow) [#48929](https://github.com/nodejs/node/pull/48929)
* \[[`5ccfb8d515`](https://github.com/nodejs/node/commit/5ccfb8d515)] - **test\_runner**: unwrap error message in TAP reporter (Colin Ihrig) [#48942](https://github.com/nodejs/node/pull/48942)
* \[[`fa19b0ed05`](https://github.com/nodejs/node/commit/fa19b0ed05)] - **test\_runner**: add `__proto__` null (Raz Luvaton) [#48663](https://github.com/nodejs/node/pull/48663)
* \[[`65d23940bf`](https://github.com/nodejs/node/commit/65d23940bf)] - **test\_runner**: fix async callback in describe not awaited (Raz Luvaton) [#48856](https://github.com/nodejs/node/pull/48856)
* \[[`4bd5e55b43`](https://github.com/nodejs/node/commit/4bd5e55b43)] - **test\_runner**: fix test\_runner `test:fail` event type (Ethan Arrowood) [#48854](https://github.com/nodejs/node/pull/48854)
* \[[`41058beed8`](https://github.com/nodejs/node/commit/41058beed8)] - **test\_runner**: call abort on test finish (Raz Luvaton) [#48827](https://github.com/nodejs/node/pull/48827)
* \[[`821b11a59f`](https://github.com/nodejs/node/commit/821b11a59f)] - **tls**: fix bugs of double TLS (rogertyang) [#48969](https://github.com/nodejs/node/pull/48969)
* \[[`4439327e73`](https://github.com/nodejs/node/commit/4439327e73)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#49122](https://github.com/nodejs/node/pull/49122)
* \[[`21dc844309`](https://github.com/nodejs/node/commit/21dc844309)] - **tools**: use spec reporter in actions (Moshe Atlow) [#49129](https://github.com/nodejs/node/pull/49129)
* \[[`3471758696`](https://github.com/nodejs/node/commit/3471758696)] - **tools**: use @reporters/github when running in github actions (Moshe Atlow) [#49129](https://github.com/nodejs/node/pull/49129)
* \[[`95a6e7661e`](https://github.com/nodejs/node/commit/95a6e7661e)] - **tools**: add @reporters/github to tools (Moshe Atlow) [#49129](https://github.com/nodejs/node/pull/49129)
* \[[`995cbf93eb`](https://github.com/nodejs/node/commit/995cbf93eb)] - **tools**: update eslint to 8.47.0 (Node.js GitHub Bot) [#49124](https://github.com/nodejs/node/pull/49124)
* \[[`ed065bc56e`](https://github.com/nodejs/node/commit/ed065bc56e)] - **tools**: update lint-md-dependencies to rollup\@3.27.2 (Node.js GitHub Bot) [#49035](https://github.com/nodejs/node/pull/49035)
* \[[`a5f37178ad`](https://github.com/nodejs/node/commit/a5f37178ad)] - **tools**: limit the number of auto start CIs (Antoine du Hamel) [#49067](https://github.com/nodejs/node/pull/49067)
* \[[`c1bd680f89`](https://github.com/nodejs/node/commit/c1bd680f89)] - **tools**: update eslint to 8.46.0 (Node.js GitHub Bot) [#48966](https://github.com/nodejs/node/pull/48966)
* \[[`e09a6b4821`](https://github.com/nodejs/node/commit/e09a6b4821)] - **tools**: update lint-md-dependencies to rollup\@3.27.0 (Node.js GitHub Bot) [#48965](https://github.com/nodejs/node/pull/48965)
* \[[`0cd2393bd9`](https://github.com/nodejs/node/commit/0cd2393bd9)] - **tools**: update lint-md-dependencies to rollup\@3.26.3 (Node.js GitHub Bot) [#48888](https://github.com/nodejs/node/pull/48888)
* \[[`41929a2906`](https://github.com/nodejs/node/commit/41929a2906)] - **tools**: update lint-md-dependencies to @rollup/plugin-commonjs\@25.0.3 (Node.js GitHub Bot) [#48791](https://github.com/nodejs/node/pull/48791)
* \[[`1761bdfbd9`](https://github.com/nodejs/node/commit/1761bdfbd9)] - **tools**: update eslint to 8.45.0 (Node.js GitHub Bot) [#48793](https://github.com/nodejs/node/pull/48793)
* \[[`b82f05cc4b`](https://github.com/nodejs/node/commit/b82f05cc4b)] - **typings**: update JSDoc for `cwd` in `child_process` (LiviaMedeiros) [#49029](https://github.com/nodejs/node/pull/49029)
* \[[`be7b511255`](https://github.com/nodejs/node/commit/be7b511255)] - **typings**: sync JSDoc with the actual implementation (Hyunjin Kim) [#48853](https://github.com/nodejs/node/pull/48853)
* \[[`45c860035d`](https://github.com/nodejs/node/commit/45c860035d)] - **url**: overload `canParse` V8 fast api method (Yagiz Nizipli) [#48993](https://github.com/nodejs/node/pull/48993)
* \[[`60d614157b`](https://github.com/nodejs/node/commit/60d614157b)] - **url**: fix `isURL` detection by checking `path` (Zhuo Zhang) [#48928](https://github.com/nodejs/node/pull/48928)
* \[[`b12c3b5240`](https://github.com/nodejs/node/commit/b12c3b5240)] - **url**: ensure getter access do not mutate observable symbols (Antoine du Hamel) [#48897](https://github.com/nodejs/node/pull/48897)
* \[[`30fb7b7535`](https://github.com/nodejs/node/commit/30fb7b7535)] - **url**: reduce `pathToFileURL` cpp calls (Yagiz Nizipli) [#48709](https://github.com/nodejs/node/pull/48709)
* \[[`c3dbd0c1e4`](https://github.com/nodejs/node/commit/c3dbd0c1e4)] - **util**: use `primordials.ArrayPrototypeIndexOf` instead of mutable method (DaisyDogs07) [#48586](https://github.com/nodejs/node/pull/48586)
* \[[`b79b2927ca`](https://github.com/nodejs/node/commit/b79b2927ca)] - **watch**: decrease debounce rate (Moshe Atlow) [#48926](https://github.com/nodejs/node/pull/48926)
* \[[`a12996298e`](https://github.com/nodejs/node/commit/a12996298e)] - **watch**: use debounce instead of throttle (Moshe Atlow) [#48926](https://github.com/nodejs/node/pull/48926)

<a id="20.5.1"></a>

## 2023-08-09, Version 20.5.1 (Current), @RafaelGSS

This is a security release.

### Notable Changes

The following CVEs are fixed in this release:

* [CVE-2023-32002](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-32002):  Policies can be bypassed via Module.\_load (High)
* [CVE-2023-32558](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-32558): process.binding() can bypass the permission model through path traversal (High)
* [CVE-2023-32004](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-32004): Permission model can be bypassed by specifying a path traversal sequence in a Buffer (High)
* [CVE-2023-32006](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-32006): Policies can be bypassed by module.constructor.createRequire (Medium)
* [CVE-2023-32559](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-32559): Policies can be bypassed via process.binding (Medium)
* [CVE-2023-32005](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-32005): fs.statfs can bypass the permission model (Low)
* [CVE-2023-32003](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-32003): fs.mkdtemp() and fs.mkdtempSync() can bypass the permission model (Low)
* OpenSSL Security Releases
  * [OpenSSL security advisory 14th July](https://mta.openssl.org/pipermail/openssl-announce/2023-July/000264.html).
  * [OpenSSL security advisory 19th July](https://mta.openssl.org/pipermail/openssl-announce/2023-July/000265.html).
  * [OpenSSL security advisory 31st July](https://mta.openssl.org/pipermail/openssl-announce/2023-July/000267.html)

More detailed information on each of the vulnerabilities can be found in [August 2023 Security Releases](https://nodejs.org/en/blog/vulnerability/august-2023-security-releases/) blog post.

### Commits

* \[[`92300b51b4`](https://github.com/nodejs/node/commit/92300b51b4)] - **deps**: update archs files for openssl-3.0.10+quic1 (Node.js GitHub Bot) [#49036](https://github.com/nodejs/node/pull/49036)
* \[[`559698abf2`](https://github.com/nodejs/node/commit/559698abf2)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.10+quic1 (Node.js GitHub Bot) [#49036](https://github.com/nodejs/node/pull/49036)
* \[[`1bf3429e8e`](https://github.com/nodejs/node/commit/1bf3429e8e)] - **lib,permission**: restrict process.binding when pm is enabled (RafaelGSS) [nodejs-private/node-private#438](https://github.com/nodejs-private/node-private/pull/438)
* \[[`98a83a67e6`](https://github.com/nodejs/node/commit/98a83a67e6)] - **permission**: ensure to resolve path when calling mkdtemp (RafaelGSS) [nodejs-private/node-private#464](https://github.com/nodejs-private/node-private/pull/464)
* \[[`1f0cde466b`](https://github.com/nodejs/node/commit/1f0cde466b)] - **permission**: handle buffer path on fs calls (RafaelGSS) [nodejs-private/node-private#439](https://github.com/nodejs-private/node-private/pull/439)
* \[[`bd094d60ea`](https://github.com/nodejs/node/commit/bd094d60ea)] - **permission**: handle fstatfs and add pm supported list (RafaelGSS) [nodejs-private/node-private#441](https://github.com/nodejs-private/node-private/pull/441)
* \[[`7337d21484`](https://github.com/nodejs/node/commit/7337d21484)] - **policy**: handle Module.constructor and main.extensions bypass (RafaelGSS) [nodejs-private/node-private#417](https://github.com/nodejs-private/node-private/pull/417)
* \[[`cf348ec640`](https://github.com/nodejs/node/commit/cf348ec640)] - **policy**: disable process.binding() when enabled (Tobias Nießen) [nodejs-private/node-private#397](https://github.com/nodejs-private/node-private/pull/397)

<a id="20.5.0"></a>

## 2023-07-18, Version 20.5.0 (Current), @juanarbol

### Notable Changes

* \[[`45be29d89f`](https://github.com/nodejs/node/commit/45be29d89f)] - **doc**: add atlowChemi to collaborators (atlowChemi) [#48757](https://github.com/nodejs/node/pull/48757)
* \[[`a316808136`](https://github.com/nodejs/node/commit/a316808136)] - **(SEMVER-MINOR)** **events**: allow safely adding listener to abortSignal (Chemi Atlow) [#48596](https://github.com/nodejs/node/pull/48596)
* \[[`986b46a567`](https://github.com/nodejs/node/commit/986b46a567)] - **fs**: add a fast-path for readFileSync utf-8 (Yagiz Nizipli) [#48658](https://github.com/nodejs/node/pull/48658)
* \[[`0ef73ff6f0`](https://github.com/nodejs/node/commit/0ef73ff6f0)] - **(SEMVER-MINOR)** **test\_runner**: add shards support (Raz Luvaton) [#48639](https://github.com/nodejs/node/pull/48639)

### Commits

* \[[`eb0aba59b8`](https://github.com/nodejs/node/commit/eb0aba59b8)] - **bootstrap**: use correct descriptor for Symbol.{dispose,asyncDispose} (Jordan Harband) [#48703](https://github.com/nodejs/node/pull/48703)
* \[[`e2d0195dcf`](https://github.com/nodejs/node/commit/e2d0195dcf)] - **bootstrap**: hide experimental web globals with flag kNoBrowserGlobals (Chengzhong Wu) [#48545](https://github.com/nodejs/node/pull/48545)
* \[[`67a1018389`](https://github.com/nodejs/node/commit/67a1018389)] - **build**: do not pass target toolchain flags to host toolchain (Ivan Trubach) [#48597](https://github.com/nodejs/node/pull/48597)
* \[[`7d843bb942`](https://github.com/nodejs/node/commit/7d843bb942)] - **child\_process**: use addAbortListener (atlowChemi) [#48550](https://github.com/nodejs/node/pull/48550)
* \[[`4e08160f8c`](https://github.com/nodejs/node/commit/4e08160f8c)] - **child\_process**: support `Symbol.dispose` (Moshe Atlow) [#48551](https://github.com/nodejs/node/pull/48551)
* \[[`ef7728bf36`](https://github.com/nodejs/node/commit/ef7728bf36)] - **deps**: update nghttp2 to 1.55.1 (Node.js GitHub Bot) [#48790](https://github.com/nodejs/node/pull/48790)
* \[[`1454f02499`](https://github.com/nodejs/node/commit/1454f02499)] - **deps**: update nghttp2 to 1.55.0 (Node.js GitHub Bot) [#48746](https://github.com/nodejs/node/pull/48746)
* \[[`fa94debf46`](https://github.com/nodejs/node/commit/fa94debf46)] - **deps**: update minimatch to 9.0.3 (Node.js GitHub Bot) [#48704](https://github.com/nodejs/node/pull/48704)
* \[[`c73cfcc144`](https://github.com/nodejs/node/commit/c73cfcc144)] - **deps**: update acorn to 8.10.0 (Node.js GitHub Bot) [#48713](https://github.com/nodejs/node/pull/48713)
* \[[`b7a076a052`](https://github.com/nodejs/node/commit/b7a076a052)] - **deps**: V8: cherry-pick cb00db4dba6c (Keyhan Vakil) [#48671](https://github.com/nodejs/node/pull/48671)
* \[[`150e15536b`](https://github.com/nodejs/node/commit/150e15536b)] - **deps**: upgrade npm to 9.8.0 (npm team) [#48665](https://github.com/nodejs/node/pull/48665)
* \[[`c47b2cbd35`](https://github.com/nodejs/node/commit/c47b2cbd35)] - **dgram**: socket add `asyncDispose` (atlowChemi) [#48717](https://github.com/nodejs/node/pull/48717)
* \[[`002ce31cca`](https://github.com/nodejs/node/commit/002ce31cca)] - **dgram**: use addAbortListener (atlowChemi) [#48550](https://github.com/nodejs/node/pull/48550)
* \[[`45be29d89f`](https://github.com/nodejs/node/commit/45be29d89f)] - **doc**: add atlowChemi to collaborators (atlowChemi) [#48757](https://github.com/nodejs/node/pull/48757)
* \[[`69b55d2261`](https://github.com/nodejs/node/commit/69b55d2261)] - **doc**: fix ambiguity in http.md and https.md (an5er) [#48692](https://github.com/nodejs/node/pull/48692)
* \[[`caccb051c7`](https://github.com/nodejs/node/commit/caccb051c7)] - **doc**: clarify transform.\_transform() callback argument logic (Rafael Sofi-zada) [#48680](https://github.com/nodejs/node/pull/48680)
* \[[`999ae0c8c3`](https://github.com/nodejs/node/commit/999ae0c8c3)] - **doc**: fix copy node executable in Windows (Yoav Vainrich) [#48624](https://github.com/nodejs/node/pull/48624)
* \[[`7daefaeb44`](https://github.com/nodejs/node/commit/7daefaeb44)] - **doc**: drop \<b> of v20 changelog (Rafael Gonzaga) [#48649](https://github.com/nodejs/node/pull/48649)
* \[[`dd7ea3e1df`](https://github.com/nodejs/node/commit/dd7ea3e1df)] - **doc**: mention git node release prepare (Rafael Gonzaga) [#48644](https://github.com/nodejs/node/pull/48644)
* \[[`cc7809df21`](https://github.com/nodejs/node/commit/cc7809df21)] - **esm**: fix emit deprecation on legacy main resolve (Antoine du Hamel) [#48664](https://github.com/nodejs/node/pull/48664)
* \[[`67b13d1dba`](https://github.com/nodejs/node/commit/67b13d1dba)] - **events**: fix bug listenerCount don't compare wrapped listener (yuzheng14) [#48592](https://github.com/nodejs/node/pull/48592)
* \[[`a316808136`](https://github.com/nodejs/node/commit/a316808136)] - **(SEMVER-MINOR)** **events**: allow safely adding listener to abortSignal (Chemi Atlow) [#48596](https://github.com/nodejs/node/pull/48596)
* \[[`986b46a567`](https://github.com/nodejs/node/commit/986b46a567)] - **fs**: add a fast-path for readFileSync utf-8 (Yagiz Nizipli) [#48658](https://github.com/nodejs/node/pull/48658)
* \[[`e4333ac41f`](https://github.com/nodejs/node/commit/e4333ac41f)] - **http2**: use addAbortListener (atlowChemi) [#48550](https://github.com/nodejs/node/pull/48550)
* \[[`4a0b66e4f9`](https://github.com/nodejs/node/commit/4a0b66e4f9)] - **http2**: send RST code 8 on AbortController signal (Devraj Mehta) [#48573](https://github.com/nodejs/node/pull/48573)
* \[[`1295c76fce`](https://github.com/nodejs/node/commit/1295c76fce)] - **lib**: use addAbortListener (atlowChemi) [#48550](https://github.com/nodejs/node/pull/48550)
* \[[`dff6c25a36`](https://github.com/nodejs/node/commit/dff6c25a36)] - **meta**: bump actions/checkout from 3.5.2 to 3.5.3 (dependabot\[bot]) [#48625](https://github.com/nodejs/node/pull/48625)
* \[[`b5cb69ceaa`](https://github.com/nodejs/node/commit/b5cb69ceaa)] - **meta**: bump step-security/harden-runner from 2.4.0 to 2.4.1 (dependabot\[bot]) [#48626](https://github.com/nodejs/node/pull/48626)
* \[[`332e480b46`](https://github.com/nodejs/node/commit/332e480b46)] - **meta**: bump ossf/scorecard-action from 2.1.3 to 2.2.0 (dependabot\[bot]) [#48628](https://github.com/nodejs/node/pull/48628)
* \[[`25c5a0aaee`](https://github.com/nodejs/node/commit/25c5a0aaee)] - **meta**: bump github/codeql-action from 2.3.6 to 2.20.1 (dependabot\[bot]) [#48627](https://github.com/nodejs/node/pull/48627)
* \[[`6406f50ab1`](https://github.com/nodejs/node/commit/6406f50ab1)] - **module**: add SourceMap.lineLengths (Isaac Z. Schlueter) [#48461](https://github.com/nodejs/node/pull/48461)
* \[[`cfa69bd48c`](https://github.com/nodejs/node/commit/cfa69bd48c)] - **net**: server add `asyncDispose` (atlowChemi) [#48717](https://github.com/nodejs/node/pull/48717)
* \[[`ac11264cc5`](https://github.com/nodejs/node/commit/ac11264cc5)] - **net**: use addAbortListener (atlowChemi) [#48550](https://github.com/nodejs/node/pull/48550)
* \[[`82d6b13bf6`](https://github.com/nodejs/node/commit/82d6b13bf6)] - **permission**: add debug log when inserting fs nodes (Rafael Gonzaga) [#48677](https://github.com/nodejs/node/pull/48677)
* \[[`f4333b1cdd`](https://github.com/nodejs/node/commit/f4333b1cdd)] - **permission**: v8.writeHeapSnapshot and process.report (Rafael Gonzaga) [#48564](https://github.com/nodejs/node/pull/48564)
* \[[`f691dca6c9`](https://github.com/nodejs/node/commit/f691dca6c9)] - **readline**: use addAbortListener (atlowChemi) [#48550](https://github.com/nodejs/node/pull/48550)
* \[[`227e6bd898`](https://github.com/nodejs/node/commit/227e6bd898)] - **src**: pass syscall on `fs.readFileSync` fail operation (Yagiz Nizipli) [#48815](https://github.com/nodejs/node/pull/48815)
* \[[`a9a4b73653`](https://github.com/nodejs/node/commit/a9a4b73653)] - **src**: make BaseObject iteration order deterministic (Joyee Cheung) [#48702](https://github.com/nodejs/node/pull/48702)
* \[[`d99ea4845a`](https://github.com/nodejs/node/commit/d99ea4845a)] - **src**: remove kEagerCompile for CompileFunction (Keyhan Vakil) [#48671](https://github.com/nodejs/node/pull/48671)
* \[[`df363d0010`](https://github.com/nodejs/node/commit/df363d0010)] - **src**: deduplicate X509 getter implementations (Tobias Nießen) [#48563](https://github.com/nodejs/node/pull/48563)
* \[[`9cf2e1f55b`](https://github.com/nodejs/node/commit/9cf2e1f55b)] - **src,lib**: reducing C++ calls of esm legacy main resolve (Vinicius Lourenço) [#48325](https://github.com/nodejs/node/pull/48325)
* \[[`daeb21dde9`](https://github.com/nodejs/node/commit/daeb21dde9)] - **stream**: fix deadlock when pipeing to full sink (Robert Nagy) [#48691](https://github.com/nodejs/node/pull/48691)
* \[[`5a382d02d6`](https://github.com/nodejs/node/commit/5a382d02d6)] - **stream**: use addAbortListener (atlowChemi) [#48550](https://github.com/nodejs/node/pull/48550)
* \[[`6e82077dd4`](https://github.com/nodejs/node/commit/6e82077dd4)] - **test**: deflake test-net-throttle (Luigi Pinca) [#48599](https://github.com/nodejs/node/pull/48599)
* \[[`d378b2c822`](https://github.com/nodejs/node/commit/d378b2c822)] - **test**: move test-net-throttle to parallel (Luigi Pinca) [#48599](https://github.com/nodejs/node/pull/48599)
* \[[`dfa0aee5bf`](https://github.com/nodejs/node/commit/dfa0aee5bf)] - _**Revert**_ "**test**: remove test-crypto-keygen flaky designation" (Luigi Pinca) [#48652](https://github.com/nodejs/node/pull/48652)
* \[[`0ef73ff6f0`](https://github.com/nodejs/node/commit/0ef73ff6f0)] - **(SEMVER-MINOR)** **test\_runner**: add shards support (Raz Luvaton) [#48639](https://github.com/nodejs/node/pull/48639)
* \[[`e2442bb7ef`](https://github.com/nodejs/node/commit/e2442bb7ef)] - **timers**: support Symbol.dispose (Moshe Atlow) [#48633](https://github.com/nodejs/node/pull/48633)
* \[[`4398ade426`](https://github.com/nodejs/node/commit/4398ade426)] - **tools**: run fetch\_deps.py with Python 3 (Richard Lau) [#48729](https://github.com/nodejs/node/pull/48729)
* \[[`38ce95d054`](https://github.com/nodejs/node/commit/38ce95d054)] - **tools**: update doc to unist-util-select\@5.0.0 unist-util-visit\@5.0.0 (Node.js GitHub Bot) [#48714](https://github.com/nodejs/node/pull/48714)
* \[[`b25e78a998`](https://github.com/nodejs/node/commit/b25e78a998)] - **tools**: update lint-md-dependencies to rollup\@3.26.2 (Node.js GitHub Bot) [#48705](https://github.com/nodejs/node/pull/48705)
* \[[`a1f4ff7c59`](https://github.com/nodejs/node/commit/a1f4ff7c59)] - **tools**: update eslint to 8.44.0 (Node.js GitHub Bot) [#48632](https://github.com/nodejs/node/pull/48632)
* \[[`42dc6eb698`](https://github.com/nodejs/node/commit/42dc6eb698)] - **tools**: update lint-md-dependencies to rollup\@3.26.0 (Node.js GitHub Bot) [#48631](https://github.com/nodejs/node/pull/48631)
* \[[`07bfcc45ab`](https://github.com/nodejs/node/commit/07bfcc45ab)] - **url**: fix `canParse` false value when v8 optimizes (Yagiz Nizipli) [#48817](https://github.com/nodejs/node/pull/48817)

<a id="20.4.0"></a>

## 2023-07-05, Version 20.4.0 (Current), @RafaelGSS

### Notable Changes

#### Mock Timers

The new feature allows developers to write more reliable and predictable tests for time-dependent functionality.
It includes `MockTimers` with the ability to mock `setTimeout`, `setInterval` from `globals`, `node:timers`, and `node:timers/promises`.

The feature provides a simple API to advance time, enable specific timers, and release all timers.

```mjs
import assert from 'node:assert';
import { test } from 'node:test';

test('mocks setTimeout to be executed synchronously without having to actually wait for it', (context) => {
  const fn = context.mock.fn();
  // Optionally choose what to mock
  context.mock.timers.enable(['setTimeout']);
  const nineSecs = 9000;
  setTimeout(fn, nineSecs);

  const threeSeconds = 3000;
  context.mock.timers.tick(threeSeconds);
  context.mock.timers.tick(threeSeconds);
  context.mock.timers.tick(threeSeconds);

  assert.strictEqual(fn.mock.callCount(), 1);
});
```

This feature was contributed by Erick Wendel in [#47775](https://github.com/nodejs/node/pull/47775).

#### Support to the explicit resource management proposal

Node is adding support to the [explicit resource management](https://github.com/tc39/proposal-explicit-resource-management)
proposal to its resources allowing users of TypeScript/babel to use `using`/`await using` with
V8 support for everyone else on the way.

This feature was contributed by Moshe Atlow and Benjamin Gruenbaum in [#48518](https://github.com/nodejs/node/pull/48518).

#### Other notable changes

* \[[`fe333d2584`](https://github.com/nodejs/node/commit/fe333d2584)] - **crypto**: update root certificates to NSS 3.90 (Node.js GitHub Bot) [#48416](https://github.com/nodejs/node/pull/48416)
* \[[`60c2ea4e79`](https://github.com/nodejs/node/commit/60c2ea4e79)] - **doc**: add vmoroz to collaborators (Vladimir Morozov) [#48527](https://github.com/nodejs/node/pull/48527)
* \[[`5cacdf9e6b`](https://github.com/nodejs/node/commit/5cacdf9e6b)] - **doc**: add kvakil to collaborators (Keyhan Vakil) [#48449](https://github.com/nodejs/node/pull/48449)
* \[[`504d1d7bdc`](https://github.com/nodejs/node/commit/504d1d7bdc)] - **(SEMVER-MINOR)** **tls**: add ALPNCallback server option for dynamic ALPN negotiation (Tim Perry) [#45190](https://github.com/nodejs/node/pull/45190)

### Commits

* \[[`8a611a387f`](https://github.com/nodejs/node/commit/8a611a387f)] - **benchmark**: add bar.R (Rafael Gonzaga) [#47729](https://github.com/nodejs/node/pull/47729)
* \[[`12fa716cf9`](https://github.com/nodejs/node/commit/12fa716cf9)] - **benchmark**: refactor crypto oneshot (Filip Skokan) [#48267](https://github.com/nodejs/node/pull/48267)
* \[[`d6ecbde592`](https://github.com/nodejs/node/commit/d6ecbde592)] - **benchmark**: add crypto.create\*Key (Filip Skokan) [#48284](https://github.com/nodejs/node/pull/48284)
* \[[`e60b6dedd8`](https://github.com/nodejs/node/commit/e60b6dedd8)] - **bootstrap**: unify snapshot builder and embedder entry points (Joyee Cheung) [#48242](https://github.com/nodejs/node/pull/48242)
* \[[`40662957b1`](https://github.com/nodejs/node/commit/40662957b1)] - **bootstrap**: simplify initialization of source map handlers (Joyee Cheung) [#48304](https://github.com/nodejs/node/pull/48304)
* \[[`6551538079`](https://github.com/nodejs/node/commit/6551538079)] - **build**: fix `configure --link-module` (Richard Lau) [#48522](https://github.com/nodejs/node/pull/48522)
* \[[`f7f32089e7`](https://github.com/nodejs/node/commit/f7f32089e7)] - **build**: sync libuv header change (Jiawen Geng) [#48429](https://github.com/nodejs/node/pull/48429)
* \[[`f60205c915`](https://github.com/nodejs/node/commit/f60205c915)] - **build**: update action to close stale PRs (Michael Dawson) [#48196](https://github.com/nodejs/node/pull/48196)
* \[[`4f4d0b802e`](https://github.com/nodejs/node/commit/4f4d0b802e)] - **child\_process**: improve spawn performance on Linux (Keyhan Vakil) [#48523](https://github.com/nodejs/node/pull/48523)
* \[[`fe333d2584`](https://github.com/nodejs/node/commit/fe333d2584)] - **crypto**: update root certificates to NSS 3.90 (Node.js GitHub Bot) [#48416](https://github.com/nodejs/node/pull/48416)
* \[[`89aaf16237`](https://github.com/nodejs/node/commit/89aaf16237)] - **crypto**: remove OPENSSL\_FIPS guard for OpenSSL 3 (Richard Lau) [#48392](https://github.com/nodejs/node/pull/48392)
* \[[`6199e1946c`](https://github.com/nodejs/node/commit/6199e1946c)] - **deps**: upgrade to libuv 1.46.0 (Santiago Gimeno) [#48618](https://github.com/nodejs/node/pull/48618)
* \[[`1b2b930fda`](https://github.com/nodejs/node/commit/1b2b930fda)] - **deps**: add loong64 config into openssl gypi (Shi Pujin) [#48043](https://github.com/nodejs/node/pull/48043)
* \[[`ba8d048929`](https://github.com/nodejs/node/commit/ba8d048929)] - **deps**: update acorn to 8.9.0 (Node.js GitHub Bot) [#48484](https://github.com/nodejs/node/pull/48484)
* \[[`d96f921d06`](https://github.com/nodejs/node/commit/d96f921d06)] - **deps**: update zlib to 1.2.13.1-motley-f81f385 (Node.js GitHub Bot) [#48541](https://github.com/nodejs/node/pull/48541)
* \[[`ed1d047e8f`](https://github.com/nodejs/node/commit/ed1d047e8f)] - **deps**: update googletest to ec4fed9 (Node.js GitHub Bot) [#48538](https://github.com/nodejs/node/pull/48538)
* \[[`f43d718c67`](https://github.com/nodejs/node/commit/f43d718c67)] - **deps**: update minimatch to 9.0.2 (Node.js GitHub Bot) [#48542](https://github.com/nodejs/node/pull/48542)
* \[[`2f66147cbf`](https://github.com/nodejs/node/commit/2f66147cbf)] - **deps**: update corepack to 0.19.0 (Node.js GitHub Bot) [#48540](https://github.com/nodejs/node/pull/48540)
* \[[`d91b0fde73`](https://github.com/nodejs/node/commit/d91b0fde73)] - **deps**: V8: cherry-pick 1a782f6543ae (Keyhan Vakil) [#48523](https://github.com/nodejs/node/pull/48523)
* \[[`112335e342`](https://github.com/nodejs/node/commit/112335e342)] - **deps**: update corepack to 0.18.1 (Node.js GitHub Bot) [#48483](https://github.com/nodejs/node/pull/48483)
* \[[`2b141c413f`](https://github.com/nodejs/node/commit/2b141c413f)] - **deps**: update icu to 73.2 (Node.js GitHub Bot) [#48502](https://github.com/nodejs/node/pull/48502)
* \[[`188b34d4a1`](https://github.com/nodejs/node/commit/188b34d4a1)] - **deps**: upgrade npm to 9.7.2 (npm team) [#48514](https://github.com/nodejs/node/pull/48514)
* \[[`bf0444b5d9`](https://github.com/nodejs/node/commit/bf0444b5d9)] - **deps**: update zlib to 1.2.13.1-motley-3ca9f16 (Node.js GitHub Bot) [#48413](https://github.com/nodejs/node/pull/48413)
* \[[`b339d80a56`](https://github.com/nodejs/node/commit/b339d80a56)] - **deps**: upgrade npm to 9.7.1 (npm team) [#48378](https://github.com/nodejs/node/pull/48378)
* \[[`4132931b87`](https://github.com/nodejs/node/commit/4132931b87)] - **deps**: update simdutf to 3.2.14 (Node.js GitHub Bot) [#48344](https://github.com/nodejs/node/pull/48344)
* \[[`8cd56c1e85`](https://github.com/nodejs/node/commit/8cd56c1e85)] - **deps**: update ada to 2.5.1 (Node.js GitHub Bot) [#48319](https://github.com/nodejs/node/pull/48319)
* \[[`78cffcd645`](https://github.com/nodejs/node/commit/78cffcd645)] - **deps**: update zlib to 982b036 (Node.js GitHub Bot) [#48327](https://github.com/nodejs/node/pull/48327)
* \[[`6d00c2e33b`](https://github.com/nodejs/node/commit/6d00c2e33b)] - **doc**: fix options order (Luigi Pinca) [#48617](https://github.com/nodejs/node/pull/48617)
* \[[`7ad2d3a5d1`](https://github.com/nodejs/node/commit/7ad2d3a5d1)] - **doc**: update security release stewards (Rafael Gonzaga) [#48569](https://github.com/nodejs/node/pull/48569)
* \[[`cc3a056fdd`](https://github.com/nodejs/node/commit/cc3a056fdd)] - **doc**: update return type for describe (Shrujal Shah) [#48572](https://github.com/nodejs/node/pull/48572)
* \[[`99ae0b98af`](https://github.com/nodejs/node/commit/99ae0b98af)] - **doc**: run license-builder (github-actions\[bot]) [#48552](https://github.com/nodejs/node/pull/48552)
* \[[`9750d8205c`](https://github.com/nodejs/node/commit/9750d8205c)] - **doc**: add description of autoAllocateChunkSize in ReadableStream (Debadree Chatterjee) [#48004](https://github.com/nodejs/node/pull/48004)
* \[[`417927bb41`](https://github.com/nodejs/node/commit/417927bb41)] - **doc**: fix `filename` type in `watch` result (Dmitry Semigradsky) [#48032](https://github.com/nodejs/node/pull/48032)
* \[[`ca2ae86bd7`](https://github.com/nodejs/node/commit/ca2ae86bd7)] - **doc**: unnest `mime` and `MIMEParams` from MIMEType constructor (Dmitry Semigradsky) [#47950](https://github.com/nodejs/node/pull/47950)
* \[[`bda1228135`](https://github.com/nodejs/node/commit/bda1228135)] - **doc**: update security-release-process.md (Rafael Gonzaga) [#48504](https://github.com/nodejs/node/pull/48504)
* \[[`60c2ea4e79`](https://github.com/nodejs/node/commit/60c2ea4e79)] - **doc**: add vmoroz to collaborators (Vladimir Morozov) [#48527](https://github.com/nodejs/node/pull/48527)
* \[[`37bc0eac4a`](https://github.com/nodejs/node/commit/37bc0eac4a)] - **doc**: improve inspector.close() description (mary marchini) [#48494](https://github.com/nodejs/node/pull/48494)
* \[[`2a403cdad5`](https://github.com/nodejs/node/commit/2a403cdad5)] - **doc**: link to Runtime Keys in export conditions (Jacob Hummer) [#48408](https://github.com/nodejs/node/pull/48408)
* \[[`e2d579e644`](https://github.com/nodejs/node/commit/e2d579e644)] - **doc**: update fs flags documentation (sinkhaha) [#48463](https://github.com/nodejs/node/pull/48463)
* \[[`38bf290115`](https://github.com/nodejs/node/commit/38bf290115)] - **doc**: revise `error.md` introduction (Antoine du Hamel) [#48423](https://github.com/nodejs/node/pull/48423)
* \[[`641a2e9c6d`](https://github.com/nodejs/node/commit/641a2e9c6d)] - **doc**: add preveen-stack to triagers (Preveen P) [#48387](https://github.com/nodejs/node/pull/48387)
* \[[`4ab5e8d2e3`](https://github.com/nodejs/node/commit/4ab5e8d2e3)] - **doc**: refine when file is undefined in test events (Moshe Atlow) [#48451](https://github.com/nodejs/node/pull/48451)
* \[[`5cacdf9e6b`](https://github.com/nodejs/node/commit/5cacdf9e6b)] - **doc**: add kvakil to collaborators (Keyhan Vakil) [#48449](https://github.com/nodejs/node/pull/48449)
* \[[`b9c643e3ef`](https://github.com/nodejs/node/commit/b9c643e3ef)] - **doc**: add additional info on TSFN dispatch (Michael Dawson) [#48367](https://github.com/nodejs/node/pull/48367)
* \[[`17a0e1d1bf`](https://github.com/nodejs/node/commit/17a0e1d1bf)] - **doc**: add link for news from security wg (Michael Dawson) [#48396](https://github.com/nodejs/node/pull/48396)
* \[[`3a62994a4f`](https://github.com/nodejs/node/commit/3a62994a4f)] - **doc**: fix typo in events.md (Darshan Sen) [#48436](https://github.com/nodejs/node/pull/48436)
* \[[`e10a4cdf68`](https://github.com/nodejs/node/commit/e10a4cdf68)] - **doc**: run license-builder (github-actions\[bot]) [#48336](https://github.com/nodejs/node/pull/48336)
* \[[`19fde638fd`](https://github.com/nodejs/node/commit/19fde638fd)] - **fs**: call the callback with an error if writeSync fails (killa) [#47949](https://github.com/nodejs/node/pull/47949)
* \[[`4cad9fd8bd`](https://github.com/nodejs/node/commit/4cad9fd8bd)] - **fs**: remove unneeded return statement (Luigi Pinca) [#48526](https://github.com/nodejs/node/pull/48526)
* \[[`d367b73f43`](https://github.com/nodejs/node/commit/d367b73f43)] - **fs**: use kResistStopPropagation (Chemi Atlow) [#48521](https://github.com/nodejs/node/pull/48521)
* \[[`e50c3169af`](https://github.com/nodejs/node/commit/e50c3169af)] - **fs, stream**: initial `Symbol.dispose` and `Symbol.asyncDispose` support (Moshe Atlow) [#48518](https://github.com/nodejs/node/pull/48518)
* \[[`7d8a0b6eb7`](https://github.com/nodejs/node/commit/7d8a0b6eb7)] - **http**: null the joinDuplicateHeaders property on cleanup (Luigi Pinca) [#48608](https://github.com/nodejs/node/pull/48608)
* \[[`94ebb02f59`](https://github.com/nodejs/node/commit/94ebb02f59)] - **http**: server add async dispose (atlowChemi) [#48548](https://github.com/nodejs/node/pull/48548)
* \[[`c6a69e31a3`](https://github.com/nodejs/node/commit/c6a69e31a3)] - **http**: remove useless ternary in test (geekreal) [#48481](https://github.com/nodejs/node/pull/48481)
* \[[`2f0f40328f`](https://github.com/nodejs/node/commit/2f0f40328f)] - **http**: fix for handling on boot timers headers and request (Franciszek Koltuniuk) [#48291](https://github.com/nodejs/node/pull/48291)
* \[[`5378ad8ab1`](https://github.com/nodejs/node/commit/5378ad8ab1)] - **http2**: server add `asyncDispose` (atlowChemi) [#48548](https://github.com/nodejs/node/pull/48548)
* \[[`97a58c5970`](https://github.com/nodejs/node/commit/97a58c5970)] - **https**: server add `asyncDispose` (atlowChemi) [#48548](https://github.com/nodejs/node/pull/48548)
* \[[`40ae6eb6aa`](https://github.com/nodejs/node/commit/40ae6eb6aa)] - **https**: fix connection checking interval not clearing on server close (Nitzan Uziely) [#48383](https://github.com/nodejs/node/pull/48383)
* \[[`15530fea4c`](https://github.com/nodejs/node/commit/15530fea4c)] - **lib**: merge cjs and esm package json reader caches (Yagiz Nizipli) [#48477](https://github.com/nodejs/node/pull/48477)
* \[[`32bda81c31`](https://github.com/nodejs/node/commit/32bda81c31)] - **lib**: reduce url getters on `makeRequireFunction` (Yagiz Nizipli) [#48492](https://github.com/nodejs/node/pull/48492)
* \[[`0da03f01ba`](https://github.com/nodejs/node/commit/0da03f01ba)] - **lib**: remove duplicated requires in check\_syntax (Yagiz Nizipli) [#48508](https://github.com/nodejs/node/pull/48508)
* \[[`97b00c347d`](https://github.com/nodejs/node/commit/97b00c347d)] - **lib**: add option to force handling stopped events (Chemi Atlow) [#48301](https://github.com/nodejs/node/pull/48301)
* \[[`fe16749649`](https://github.com/nodejs/node/commit/fe16749649)] - **lib**: fix output message when repl is used with pm (Rafael Gonzaga) [#48438](https://github.com/nodejs/node/pull/48438)
* \[[`8c2c02d28a`](https://github.com/nodejs/node/commit/8c2c02d28a)] - **lib**: create weakRef only if any signals provided (Chemi Atlow) [#48448](https://github.com/nodejs/node/pull/48448)
* \[[`b6ae411ea9`](https://github.com/nodejs/node/commit/b6ae411ea9)] - **lib**: remove obsolete deletion of bufferBinding.zeroFill (Chengzhong Wu) [#47881](https://github.com/nodejs/node/pull/47881)
* \[[`562b3d4856`](https://github.com/nodejs/node/commit/562b3d4856)] - **lib**: move web global bootstrapping to the expected file (Chengzhong Wu) [#47881](https://github.com/nodejs/node/pull/47881)
* \[[`f9c0d5acac`](https://github.com/nodejs/node/commit/f9c0d5acac)] - **lib**: fix blob.stream() causing hanging promises (Debadree Chatterjee) [#48232](https://github.com/nodejs/node/pull/48232)
* \[[`0162a0f5bf`](https://github.com/nodejs/node/commit/0162a0f5bf)] - **lib**: add support for inherited custom inspection methods (Antoine du Hamel) [#48306](https://github.com/nodejs/node/pull/48306)
* \[[`159ab6627a`](https://github.com/nodejs/node/commit/159ab6627a)] - **lib**: reduce URL invocations on http2 origins (Yagiz Nizipli) [#48338](https://github.com/nodejs/node/pull/48338)
* \[[`f0709fdc59`](https://github.com/nodejs/node/commit/f0709fdc59)] - **module**: add SourceMap.findOrigin (Isaac Z. Schlueter) [#47790](https://github.com/nodejs/node/pull/47790)
* \[[`4ec2d925b1`](https://github.com/nodejs/node/commit/4ec2d925b1)] - **module**: reduce url invocations in esm/load.js (Yagiz Nizipli) [#48337](https://github.com/nodejs/node/pull/48337)
* \[[`2c363971cc`](https://github.com/nodejs/node/commit/2c363971cc)] - **net**: improve network family autoselection handle handling (Paolo Insogna) [#48464](https://github.com/nodejs/node/pull/48464)
* \[[`dbf9e9ffc8`](https://github.com/nodejs/node/commit/dbf9e9ffc8)] - **node-api**: provide napi\_define\_properties fast path (Gabriel Schulhof) [#48440](https://github.com/nodejs/node/pull/48440)
* \[[`87ad657777`](https://github.com/nodejs/node/commit/87ad657777)] - **node-api**: implement external strings (Gabriel Schulhof) [#48339](https://github.com/nodejs/node/pull/48339)
* \[[`4efa6807ea`](https://github.com/nodejs/node/commit/4efa6807ea)] - **permission**: handle end nodes with children cases (Rafael Gonzaga) [#48531](https://github.com/nodejs/node/pull/48531)
* \[[`84fe811108`](https://github.com/nodejs/node/commit/84fe811108)] - **repl**: display dynamic import variant in static import error messages (Hemanth HM) [#48129](https://github.com/nodejs/node/pull/48129)
* \[[`bdcc037470`](https://github.com/nodejs/node/commit/bdcc037470)] - **report**: disable js stack when no context is entered (Chengzhong Wu) [#48495](https://github.com/nodejs/node/pull/48495)
* \[[`97bd9ccd04`](https://github.com/nodejs/node/commit/97bd9ccd04)] - **src**: fix uninitialized field access in AsyncHooks (Jan Olaf Krems) [#48566](https://github.com/nodejs/node/pull/48566)
* \[[`404958fc96`](https://github.com/nodejs/node/commit/404958fc96)] - **src**: fix Coverity issue regarding unnecessary copy (Yagiz Nizipli) [#48565](https://github.com/nodejs/node/pull/48565)
* \[[`c4b8edea24`](https://github.com/nodejs/node/commit/c4b8edea24)] - **src**: refactor `SplitString` in util (Yagiz Nizipli) [#48491](https://github.com/nodejs/node/pull/48491)
* \[[`5bc13a4772`](https://github.com/nodejs/node/commit/5bc13a4772)] - **src**: revert IS\_RELEASE (Rafael Gonzaga) [#48505](https://github.com/nodejs/node/pull/48505)
* \[[`4971e46051`](https://github.com/nodejs/node/commit/4971e46051)] - **src**: add V8 fast api to `guessHandleType` (Yagiz Nizipli) [#48349](https://github.com/nodejs/node/pull/48349)
* \[[`954e46e792`](https://github.com/nodejs/node/commit/954e46e792)] - **src**: return uint32 for `guessHandleType` (Yagiz Nizipli) [#48349](https://github.com/nodejs/node/pull/48349)
* \[[`05009675da`](https://github.com/nodejs/node/commit/05009675da)] - **src**: make realm binding data store weak (Chengzhong Wu) [#47688](https://github.com/nodejs/node/pull/47688)
* \[[`120ac74352`](https://github.com/nodejs/node/commit/120ac74352)] - **src**: remove aliased buffer weak callback (Chengzhong Wu) [#47688](https://github.com/nodejs/node/pull/47688)
* \[[`6591826e99`](https://github.com/nodejs/node/commit/6591826e99)] - **src**: handle wasm out of bound in osx will raise SIGBUS correctly (Congcong Cai) [#46561](https://github.com/nodejs/node/pull/46561)
* \[[`1b84ddeec2`](https://github.com/nodejs/node/commit/1b84ddeec2)] - **src**: implement constants binding directly (Joyee Cheung) [#48186](https://github.com/nodejs/node/pull/48186)
* \[[`06d49c1f10`](https://github.com/nodejs/node/commit/06d49c1f10)] - **src**: implement natives binding without special casing (Joyee Cheung) [#48186](https://github.com/nodejs/node/pull/48186)
* \[[`325441abf5`](https://github.com/nodejs/node/commit/325441abf5)] - **src**: add missing to\_ascii method in dns queries (Daniel Lemire) [#48354](https://github.com/nodejs/node/pull/48354)
* \[[`84d0eb74b8`](https://github.com/nodejs/node/commit/84d0eb74b8)] - **stream**: fix premature pipeline end (Robert Nagy) [#48435](https://github.com/nodejs/node/pull/48435)
* \[[`3df7368735`](https://github.com/nodejs/node/commit/3df7368735)] - **test**: add missing assertions to test-runner-cli (Moshe Atlow) [#48593](https://github.com/nodejs/node/pull/48593)
* \[[`07eb310b0d`](https://github.com/nodejs/node/commit/07eb310b0d)] - **test**: remove test-crypto-keygen flaky designation (Luigi Pinca) [#48575](https://github.com/nodejs/node/pull/48575)
* \[[`75aa0a7682`](https://github.com/nodejs/node/commit/75aa0a7682)] - **test**: remove test-timers-immediate-queue flaky designation (Luigi Pinca) [#48575](https://github.com/nodejs/node/pull/48575)
* \[[`a9756f3126`](https://github.com/nodejs/node/commit/a9756f3126)] - **test**: add Symbol.dispose support to mock timers (Benjamin Gruenbaum) [#48549](https://github.com/nodejs/node/pull/48549)
* \[[`0f912a7248`](https://github.com/nodejs/node/commit/0f912a7248)] - **test**: mark test-child-process-stdio-reuse-readable-stdio flaky (Luigi Pinca) [#48537](https://github.com/nodejs/node/pull/48537)
* \[[`30f4bc4985`](https://github.com/nodejs/node/commit/30f4bc4985)] - **test**: make IsolateData per-isolate in cctest (Joyee Cheung) [#48450](https://github.com/nodejs/node/pull/48450)
* \[[`407ce3fdcb`](https://github.com/nodejs/node/commit/407ce3fdcb)] - **test**: define NAPI\_VERSION before including node\_api.h (Chengzhong Wu) [#48376](https://github.com/nodejs/node/pull/48376)
* \[[`24a8fa95f0`](https://github.com/nodejs/node/commit/24a8fa95f0)] - **test**: remove unnecessary noop function args to `mustNotCall()` (Antoine du Hamel) [#48513](https://github.com/nodejs/node/pull/48513)
* \[[`09af579775`](https://github.com/nodejs/node/commit/09af579775)] - **test**: skip test-runner-watch-mode on IBMi (Moshe Atlow) [#48473](https://github.com/nodejs/node/pull/48473)
* \[[`77cb1ee0b2`](https://github.com/nodejs/node/commit/77cb1ee0b2)] - **test**: add missing \<algorithm> include for std::find (Sam James) [#48380](https://github.com/nodejs/node/pull/48380)
* \[[`7c790ca03c`](https://github.com/nodejs/node/commit/7c790ca03c)] - **test**: fix flaky test-watch-mode (Moshe Atlow) [#48147](https://github.com/nodejs/node/pull/48147)
* \[[`1398829746`](https://github.com/nodejs/node/commit/1398829746)] - **test**: fix `test-net-autoselectfamily` for kernel without IPv6 support (Livia Medeiros) [#48265](https://github.com/nodejs/node/pull/48265)
* \[[`764119ba4b`](https://github.com/nodejs/node/commit/764119ba4b)] - **test**: update url web-platform tests (Yagiz Nizipli) [#48319](https://github.com/nodejs/node/pull/48319)
* \[[`f1ead59629`](https://github.com/nodejs/node/commit/f1ead59629)] - **test**: ignore the copied entry\_point.c (Luigi Pinca) [#48297](https://github.com/nodejs/node/pull/48297)
* \[[`fc5d1bddcb`](https://github.com/nodejs/node/commit/fc5d1bddcb)] - **test**: refactor test-gc-http-client-timeout (Luigi Pinca) [#48292](https://github.com/nodejs/node/pull/48292)
* \[[`46a3d068a0`](https://github.com/nodejs/node/commit/46a3d068a0)] - **test**: update encoding web-platform tests (Yagiz Nizipli) [#48320](https://github.com/nodejs/node/pull/48320)
* \[[`141e5aad83`](https://github.com/nodejs/node/commit/141e5aad83)] - **test**: update FileAPI web-platform tests (Yagiz Nizipli) [#48322](https://github.com/nodejs/node/pull/48322)
* \[[`83cfc67099`](https://github.com/nodejs/node/commit/83cfc67099)] - **test**: update user-timing web-platform tests (Yagiz Nizipli) [#48321](https://github.com/nodejs/node/pull/48321)
* \[[`2c56835a33`](https://github.com/nodejs/node/commit/2c56835a33)] - **test\_runner**: fixed `test` shorthands return type (Shocker) [#48555](https://github.com/nodejs/node/pull/48555)
* \[[`7d01c8894a`](https://github.com/nodejs/node/commit/7d01c8894a)] - **(SEMVER-MINOR)** **test\_runner**: add initial draft for fakeTimers (Erick Wendel) [#47775](https://github.com/nodejs/node/pull/47775)
* \[[`de4f14c249`](https://github.com/nodejs/node/commit/de4f14c249)] - **test\_runner**: add enqueue and dequeue events (Moshe Atlow) [#48428](https://github.com/nodejs/node/pull/48428)
* \[[`5ebe3a4ea7`](https://github.com/nodejs/node/commit/5ebe3a4ea7)] - **test\_runner**: make `--test-name-pattern` recursive (Moshe Atlow) [#48382](https://github.com/nodejs/node/pull/48382)
* \[[`93bf447308`](https://github.com/nodejs/node/commit/93bf447308)] - **test\_runner**: refactor coverage report output for readability (Damien Seguin) [#47791](https://github.com/nodejs/node/pull/47791)
* \[[`504d1d7bdc`](https://github.com/nodejs/node/commit/504d1d7bdc)] - **(SEMVER-MINOR)** **tls**: add ALPNCallback server option for dynamic ALPN negotiation (Tim Perry) [#45190](https://github.com/nodejs/node/pull/45190)
* \[[`203c3cf4ca`](https://github.com/nodejs/node/commit/203c3cf4ca)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#48544](https://github.com/nodejs/node/pull/48544)
* \[[`333907b19d`](https://github.com/nodejs/node/commit/333907b19d)] - **tools**: speedup compilation of js2c output (Keyhan Vakil) [#48160](https://github.com/nodejs/node/pull/48160)
* \[[`10bd5f4d97`](https://github.com/nodejs/node/commit/10bd5f4d97)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#48486](https://github.com/nodejs/node/pull/48486)
* \[[`52de27b9fe`](https://github.com/nodejs/node/commit/52de27b9fe)] - **tools**: pin ruff version number (Rich Trott) [#48505](https://github.com/nodejs/node/pull/48505)
* \[[`4345526644`](https://github.com/nodejs/node/commit/4345526644)] - **tools**: replace sed with perl (Luigi Pinca) [#48499](https://github.com/nodejs/node/pull/48499)
* \[[`6c590835f3`](https://github.com/nodejs/node/commit/6c590835f3)] - **tools**: automate update openssl v16 (Marco Ippolito) [#48377](https://github.com/nodejs/node/pull/48377)
* \[[`90b5335338`](https://github.com/nodejs/node/commit/90b5335338)] - **tools**: update eslint to 8.43.0 (Node.js GitHub Bot) [#48487](https://github.com/nodejs/node/pull/48487)
* \[[`cd83530a11`](https://github.com/nodejs/node/commit/cd83530a11)] - **tools**: update doc to to-vfile\@8.0.0 (Node.js GitHub Bot) [#48485](https://github.com/nodejs/node/pull/48485)
* \[[`e500b439bd`](https://github.com/nodejs/node/commit/e500b439bd)] - **tools**: prepare tools/doc for to-vfile 8.0.0 (Rich Trott) [#48485](https://github.com/nodejs/node/pull/48485)
* \[[`d623616813`](https://github.com/nodejs/node/commit/d623616813)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#48417](https://github.com/nodejs/node/pull/48417)
* \[[`a2e107dde4`](https://github.com/nodejs/node/commit/a2e107dde4)] - **tools**: update create-or-update-pull-request-action (Richard Lau) [#48398](https://github.com/nodejs/node/pull/48398)
* \[[`8009e2c3be`](https://github.com/nodejs/node/commit/8009e2c3be)] - **tools**: update eslint-plugin-jsdoc (Richard Lau) [#48393](https://github.com/nodejs/node/pull/48393)
* \[[`10385c8565`](https://github.com/nodejs/node/commit/10385c8565)] - **tools**: add version update to external dependencies (Andrea Fassina) [#48081](https://github.com/nodejs/node/pull/48081)
* \[[`b1cef81b18`](https://github.com/nodejs/node/commit/b1cef81b18)] - **tools**: update eslint to 8.42.0 (Node.js GitHub Bot) [#48328](https://github.com/nodejs/node/pull/48328)
* \[[`0923dc0b8e`](https://github.com/nodejs/node/commit/0923dc0b8e)] - **tools**: disable jsdoc/no-defaults rule (Luigi Pinca) [#48328](https://github.com/nodejs/node/pull/48328)
* \[[`b03146da85`](https://github.com/nodejs/node/commit/b03146da85)] - **typings**: remove unused primordials (Yagiz Nizipli) [#48509](https://github.com/nodejs/node/pull/48509)
* \[[`e9c9d187b9`](https://github.com/nodejs/node/commit/e9c9d187b9)] - **typings**: fix JSDoc in ESM loader modules (Antoine du Hamel) [#48424](https://github.com/nodejs/node/pull/48424)
* \[[`fafe651d23`](https://github.com/nodejs/node/commit/fafe651d23)] - **url**: conform to origin getter spec changes (Yagiz Nizipli) [#48319](https://github.com/nodejs/node/pull/48319)

<a id="20.3.1"></a>

## 2023-06-20, Version 20.3.1 (Current), @RafaelGSS

This is a security release.

### Notable Changes

The following CVEs are fixed in this release:

* [CVE-2023-30581](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-30581): `mainModule.__proto__` Bypass Experimental Policy Mechanism (High)
* [CVE-2023-30584](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-30584): Path Traversal Bypass in Experimental Permission Model (High)
* [CVE-2023-30587](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-30587): Bypass of Experimental Permission Model via Node.js Inspector (High)
* [CVE-2023-30582](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-30582): Inadequate Permission Model Allows Unauthorized File Watching (Medium)
* [CVE-2023-30583](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-30583): Bypass of Experimental Permission Model via fs.openAsBlob() (Medium)
* [CVE-2023-30585](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-30585): Privilege escalation via Malicious Registry Key manipulation during Node.js installer repair process (Medium)
* [CVE-2023-30586](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-30586): Bypass of Experimental Permission Model via Arbitrary OpenSSL Engines (Medium)
* [CVE-2023-30588](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-30588): Process interuption due to invalid Public Key information in x509 certificates (Medium)
* [CVE-2023-30589](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-30589): HTTP Request Smuggling via Empty headers separated by CR (Medium)
* [CVE-2023-30590](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-30590): DiffieHellman does not generate keys after setting a private key (Medium)
* OpenSSL Security Releases
  * [OpenSSL security advisory 28th March](https://www.openssl.org/news/secadv/20230328.txt).
  * [OpenSSL security advisory 20th April](https://www.openssl.org/news/secadv/20230420.txt).
  * [OpenSSL security advisory 30th May](https://www.openssl.org/news/secadv/20230530.txt)

More detailed information on each of the vulnerabilities can be found in [June 2023 Security Releases](https://nodejs.org/en/blog/vulnerability/june-2023-security-releases/) blog post.

### Commits

* \[[`dac08dafc9`](https://github.com/nodejs/node/commit/dac08dafc9)] - **crypto**: handle cert with invalid SPKI gracefully (Tobias Nießen) [nodejs-private/node-private#393](https://github.com/nodejs-private/node-private/pull/393)
* \[[`d274c3babc`](https://github.com/nodejs/node/commit/d274c3babc)] - **crypto,https,tls**: disable engines if perms enabled (Tobias Nießen) [nodejs-private/node-private#409](https://github.com/nodejs-private/node-private/pull/409)
* \[[`5621c1de38`](https://github.com/nodejs/node/commit/5621c1de38)] - **deps**: update archs files for openssl-3.0.9-quic1 (Node.js GitHub Bot) [#48402](https://github.com/nodejs/node/pull/48402)
* \[[`771caa9f1c`](https://github.com/nodejs/node/commit/771caa9f1c)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.9-quic1 (Node.js GitHub Bot) [#48402](https://github.com/nodejs/node/pull/48402)
* \[[`0459bf9c99`](https://github.com/nodejs/node/commit/0459bf9c99)] - **doc,test**: clarify behavior of DH generateKeys (Tobias Nießen) [nodejs-private/node-private#426](https://github.com/nodejs-private/node-private/pull/426)
* \[[`27e20501aa`](https://github.com/nodejs/node/commit/27e20501aa)] - **http**: disable request smuggling via empty headers (Paolo Insogna) [nodejs-private/node-private#427](https://github.com/nodejs-private/node-private/pull/427)
* \[[`9c17e335f1`](https://github.com/nodejs/node/commit/9c17e335f1)] - **msi**: do not create AppData\Roaming\npm (Tobias Nießen) [nodejs-private/node-private#408](https://github.com/nodejs-private/node-private/pull/408)
* \[[`b51124c637`](https://github.com/nodejs/node/commit/b51124c637)] - **permission**: handle fs path traversal (RafaelGSS) [nodejs-private/node-private#403](https://github.com/nodejs-private/node-private/pull/403)
* \[[`ebc5927adc`](https://github.com/nodejs/node/commit/ebc5927adc)] - **permission**: handle fs.openAsBlob (RafaelGSS) [nodejs-private/node-private#405](https://github.com/nodejs-private/node-private/pull/405)
* \[[`c39a43bff5`](https://github.com/nodejs/node/commit/c39a43bff5)] - **permission**: handle fs.watchFile (RafaelGSS) [nodejs-private/node-private#404](https://github.com/nodejs-private/node-private/pull/404)
* \[[`d0a8264ec9`](https://github.com/nodejs/node/commit/d0a8264ec9)] - **policy**: handle mainModule.\_\_proto\_\_ bypass (RafaelGSS) [nodejs-private/node-private#416](https://github.com/nodejs-private/node-private/pull/416)
* \[[`3df13d5a79`](https://github.com/nodejs/node/commit/3df13d5a79)] - **src,permission**: restrict inspector when pm enabled (RafaelGSS) [nodejs-private/node-private#410](https://github.com/nodejs-private/node-private/pull/410)

<a id="20.3.0"></a>

## 2023-06-08, Version 20.3.0 (Current), @targos

### Notable Changes

* \[[`bfcb3d1d9a`](https://github.com/nodejs/node/commit/bfcb3d1d9a)] - **deps**: upgrade to libuv 1.45.0, including significant performance improvements to file system operations on Linux (Santiago Gimeno) [#48078](https://github.com/nodejs/node/pull/48078)
* \[[`5094d1b292`](https://github.com/nodejs/node/commit/5094d1b292)] - **doc**: add Ruy Adorno to list of TSC members (Michael Dawson) [#48172](https://github.com/nodejs/node/pull/48172)
* \[[`2f5dbca690`](https://github.com/nodejs/node/commit/2f5dbca690)] - **doc**: mark Node.js 14 as End-of-Life (Richard Lau) [#48023](https://github.com/nodejs/node/pull/48023)
* \[[`b1828b325e`](https://github.com/nodejs/node/commit/b1828b325e)] - **(SEMVER-MINOR)** **lib**: implement `AbortSignal.any()` (Chemi Atlow) [#47821](https://github.com/nodejs/node/pull/47821)
* \[[`f380953103`](https://github.com/nodejs/node/commit/f380953103)] - **module**: change default resolver to not throw on unknown scheme (Gil Tayar) [#47824](https://github.com/nodejs/node/pull/47824)
* \[[`a94f87ed99`](https://github.com/nodejs/node/commit/a94f87ed99)] - **(SEMVER-MINOR)** **node-api**: define version 9 (Chengzhong Wu) [#48151](https://github.com/nodejs/node/pull/48151)
* \[[`9e2b13dfa7`](https://github.com/nodejs/node/commit/9e2b13dfa7)] - **stream**: deprecate `asIndexedPairs` (Chemi Atlow) [#48102](https://github.com/nodejs/node/pull/48102)

### Commits

* \[[`35c96156d1`](https://github.com/nodejs/node/commit/35c96156d1)] - **benchmark**: use `cluster.isPrimary` instead of `cluster.isMaster` (Deokjin Kim) [#48002](https://github.com/nodejs/node/pull/48002)
* \[[`3e6e3abf32`](https://github.com/nodejs/node/commit/3e6e3abf32)] - **bootstrap**: throw ERR\_NOT\_SUPPORTED\_IN\_SNAPSHOT in unsupported operation (Joyee Cheung) [#47887](https://github.com/nodejs/node/pull/47887)
* \[[`c480559347`](https://github.com/nodejs/node/commit/c480559347)] - **bootstrap**: put is\_building\_snapshot state in IsolateData (Joyee Cheung) [#47887](https://github.com/nodejs/node/pull/47887)
* \[[`50c0a15535`](https://github.com/nodejs/node/commit/50c0a15535)] - **build**: set v8\_enable\_webassembly=false when lite mode is enabled (Cheng Shao) [#48248](https://github.com/nodejs/node/pull/48248)
* \[[`4562805cf6`](https://github.com/nodejs/node/commit/4562805cf6)] - **build**: speed up compilation of mksnapshot output (Keyhan Vakil) [#48162](https://github.com/nodejs/node/pull/48162)
* \[[`8b89f13933`](https://github.com/nodejs/node/commit/8b89f13933)] - **build**: add action to close stale PRs (Michael Dawson) [#48051](https://github.com/nodejs/node/pull/48051)
* \[[`5d92202220`](https://github.com/nodejs/node/commit/5d92202220)] - **build**: replace js2c.py with js2c.cc (Joyee Cheung) [#46997](https://github.com/nodejs/node/pull/46997)
* \[[`6cf2adc36e`](https://github.com/nodejs/node/commit/6cf2adc36e)] - **cluster**: use ObjectPrototypeHasOwnProperty (Daeyeon Jeong) [#48141](https://github.com/nodejs/node/pull/48141)
* \[[`f564b03c38`](https://github.com/nodejs/node/commit/f564b03c38)] - **crypto**: use openssl's own memory BIOs in crypto\_context.cc (GauriSpears) [#47160](https://github.com/nodejs/node/pull/47160)
* \[[`ac8dd61fc3`](https://github.com/nodejs/node/commit/ac8dd61fc3)] - **crypto**: remove default encoding from cipher (Tobias Nießen) [#47998](https://github.com/nodejs/node/pull/47998)
* \[[`15c2de4407`](https://github.com/nodejs/node/commit/15c2de4407)] - **crypto**: fix setEngine() when OPENSSL\_NO\_ENGINE set (Tobias Nießen) [#47977](https://github.com/nodejs/node/pull/47977)
* \[[`9e2dd5b5e2`](https://github.com/nodejs/node/commit/9e2dd5b5e2)] - **deps**: update zlib to 337322d (Node.js GitHub Bot) [#48218](https://github.com/nodejs/node/pull/48218)
* \[[`bfcb3d1d9a`](https://github.com/nodejs/node/commit/bfcb3d1d9a)] - **deps**: upgrade to libuv 1.45.0 (Santiago Gimeno) [#48078](https://github.com/nodejs/node/pull/48078)
* \[[`13930f092f`](https://github.com/nodejs/node/commit/13930f092f)] - **deps**: update ada to 2.5.0 (Node.js GitHub Bot) [#48223](https://github.com/nodejs/node/pull/48223)
* \[[`3047caebec`](https://github.com/nodejs/node/commit/3047caebec)] - **deps**: set `CARES_RANDOM_FILE` for c-ares (Richard Lau) [#48156](https://github.com/nodejs/node/pull/48156)
* \[[`0db79a0872`](https://github.com/nodejs/node/commit/0db79a0872)] - **deps**: update histogram 0.11.8 (Marco Ippolito) [#47742](https://github.com/nodejs/node/pull/47742)
* \[[`99af6716f5`](https://github.com/nodejs/node/commit/99af6716f5)] - **deps**: update histogram to 0.11.7 (Marco Ippolito) [#47742](https://github.com/nodejs/node/pull/47742)
* \[[`d4922bc985`](https://github.com/nodejs/node/commit/d4922bc985)] - **deps**: update c-ares to 1.19.1 (Node.js GitHub Bot) [#48115](https://github.com/nodejs/node/pull/48115)
* \[[`f6ccdb289f`](https://github.com/nodejs/node/commit/f6ccdb289f)] - **deps**: update simdutf to 3.2.12 (Node.js GitHub Bot) [#48118](https://github.com/nodejs/node/pull/48118)
* \[[`3ed0afc778`](https://github.com/nodejs/node/commit/3ed0afc778)] - **deps**: update minimatch to 9.0.1 (Node.js GitHub Bot) [#48094](https://github.com/nodejs/node/pull/48094)
* \[[`df7540fb73`](https://github.com/nodejs/node/commit/df7540fb73)] - **deps**: update ada to 2.4.2 (Node.js GitHub Bot) [#48092](https://github.com/nodejs/node/pull/48092)
* \[[`07df5c48e8`](https://github.com/nodejs/node/commit/07df5c48e8)] - **deps**: update corepack to 0.18.0 (Node.js GitHub Bot) [#48091](https://github.com/nodejs/node/pull/48091)
* \[[`d95a5bb559`](https://github.com/nodejs/node/commit/d95a5bb559)] - **deps**: update uvwasi to 0.0.18 (Node.js GitHub Bot) [#47866](https://github.com/nodejs/node/pull/47866)
* \[[`443477e041`](https://github.com/nodejs/node/commit/443477e041)] - **deps**: update uvwasi to 0.0.17 (Node.js GitHub Bot) [#47866](https://github.com/nodejs/node/pull/47866)
* \[[`03f67d6d6d`](https://github.com/nodejs/node/commit/03f67d6d6d)] - **deps**: upgrade npm to 9.6.7 (npm team) [#48062](https://github.com/nodejs/node/pull/48062)
* \[[`d3e3a911fd`](https://github.com/nodejs/node/commit/d3e3a911fd)] - **deps**: update nghttp2 to 1.53.0 (Node.js GitHub Bot) [#47997](https://github.com/nodejs/node/pull/47997)
* \[[`f7c4daaf67`](https://github.com/nodejs/node/commit/f7c4daaf67)] - **deps**: update ada to 2.4.1 (Node.js GitHub Bot) [#48036](https://github.com/nodejs/node/pull/48036)
* \[[`c6a752560d`](https://github.com/nodejs/node/commit/c6a752560d)] - **deps**: add loongarch64 into openssl Makefile and gen openssl-loongarch64 (Shi Pujin) [#46401](https://github.com/nodejs/node/pull/46401)
* \[[`d194241716`](https://github.com/nodejs/node/commit/d194241716)] - **deps**: update undici to 5.22.1 (Node.js GitHub Bot) [#47994](https://github.com/nodejs/node/pull/47994)
* \[[`02e919f4a2`](https://github.com/nodejs/node/commit/02e919f4a2)] - **deps,test**: update postject to 1.0.0-alpha.6 (Node.js GitHub Bot) [#48072](https://github.com/nodejs/node/pull/48072)
* \[[`2c19f596ad`](https://github.com/nodejs/node/commit/2c19f596ad)] - **doc**: clarify array args to Buffer.from() (Bryan English) [#48274](https://github.com/nodejs/node/pull/48274)
* \[[`d681e5f456`](https://github.com/nodejs/node/commit/d681e5f456)] - **doc**: document watch option for node:test run() (Moshe Atlow) [#48256](https://github.com/nodejs/node/pull/48256)
* \[[`96e54ddbca`](https://github.com/nodejs/node/commit/96e54ddbca)] - **doc**: reserve 117 for Electron 26 (Calvin) [#48245](https://github.com/nodejs/node/pull/48245)
* \[[`9aff8c7818`](https://github.com/nodejs/node/commit/9aff8c7818)] - **doc**: update documentation for FIPS support (Richard Lau) [#48194](https://github.com/nodejs/node/pull/48194)
* \[[`8c5338648f`](https://github.com/nodejs/node/commit/8c5338648f)] - **doc**: improve the documentation of the stdio option (Kumar Arnav) [#48110](https://github.com/nodejs/node/pull/48110)
* \[[`11918d705f`](https://github.com/nodejs/node/commit/11918d705f)] - **doc**: update Buffer.allocUnsafe description (sinkhaha) [#48183](https://github.com/nodejs/node/pull/48183)
* \[[`2b51ee5e22`](https://github.com/nodejs/node/commit/2b51ee5e22)] - **doc**: update codeowners with website team (Claudio Wunder) [#48197](https://github.com/nodejs/node/pull/48197)
* \[[`360df25d04`](https://github.com/nodejs/node/commit/360df25d04)] - **doc**: fix broken link to new folder doc/contributing/maintaining (Andrea Fassina) [#48205](https://github.com/nodejs/node/pull/48205)
* \[[`13e95e21a4`](https://github.com/nodejs/node/commit/13e95e21a4)] - **doc**: add atlowChemi to triagers (Chemi Atlow) [#48104](https://github.com/nodejs/node/pull/48104)
* \[[`5f83ce530f`](https://github.com/nodejs/node/commit/5f83ce530f)] - **doc**: fix typo in readline completer function section (Vadym) [#48188](https://github.com/nodejs/node/pull/48188)
* \[[`3c82165d27`](https://github.com/nodejs/node/commit/3c82165d27)] - **doc**: remove broken link for keygen (Rich Trott) [#48176](https://github.com/nodejs/node/pull/48176)
* \[[`0ca90a1e6d`](https://github.com/nodejs/node/commit/0ca90a1e6d)] - **doc**: add `auto` intrinsic height to prevent jitter/flicker (Daniel Holbert) [#48195](https://github.com/nodejs/node/pull/48195)
* \[[`f117855092`](https://github.com/nodejs/node/commit/f117855092)] - **doc**: add version info on the SEA docs (Antoine du Hamel) [#48173](https://github.com/nodejs/node/pull/48173)
* \[[`5094d1b292`](https://github.com/nodejs/node/commit/5094d1b292)] - **doc**: add Ruy to list of TSC members (Michael Dawson) [#48172](https://github.com/nodejs/node/pull/48172)
* \[[`39d8140227`](https://github.com/nodejs/node/commit/39d8140227)] - **doc**: update socket.remote\* properties documentation (Saba Kharanauli) [#48139](https://github.com/nodejs/node/pull/48139)
* \[[`5497c13efe`](https://github.com/nodejs/node/commit/5497c13efe)] - **doc**: update outdated section on TLSv1.3-PSK (Tobias Nießen) [#48123](https://github.com/nodejs/node/pull/48123)
* \[[`281dfaf727`](https://github.com/nodejs/node/commit/281dfaf727)] - **doc**: improve HMAC key recommendations (Tobias Nießen) [#48121](https://github.com/nodejs/node/pull/48121)
* \[[`bd311b6c70`](https://github.com/nodejs/node/commit/bd311b6c70)] - **doc**: clarify mkdir() recursive behavior (Stephen Odogwu) [#48109](https://github.com/nodejs/node/pull/48109)
* \[[`5b061c8922`](https://github.com/nodejs/node/commit/5b061c8922)] - **doc**: fix typo in crypto legacy streams API section (Tobias Nießen) [#48122](https://github.com/nodejs/node/pull/48122)
* \[[`10ccb2bd81`](https://github.com/nodejs/node/commit/10ccb2bd81)] - **doc**: update SEA source link (Rich Trott) [#48080](https://github.com/nodejs/node/pull/48080)
* \[[`415bf7f532`](https://github.com/nodejs/node/commit/415bf7f532)] - **doc**: clarify tty.isRaw (Roberto Vidal) [#48055](https://github.com/nodejs/node/pull/48055)
* \[[`0ac4b33c76`](https://github.com/nodejs/node/commit/0ac4b33c76)] - **doc**: correct line break for Windows terminals (Alex Schwartz) [#48083](https://github.com/nodejs/node/pull/48083)
* \[[`f30ba5c320`](https://github.com/nodejs/node/commit/f30ba5c320)] - **doc**: fix Windows code snippet tags (Antoine du Hamel) [#48100](https://github.com/nodejs/node/pull/48100)
* \[[`12fef9b68c`](https://github.com/nodejs/node/commit/12fef9b68c)] - **doc**: harmonize fenced code snippet flags (Antoine du Hamel) [#48082](https://github.com/nodejs/node/pull/48082)
* \[[`13f163eace`](https://github.com/nodejs/node/commit/13f163eace)] - **doc**: use secure key length for HMAC generateKey (Tobias Nießen) [#48052](https://github.com/nodejs/node/pull/48052)
* \[[`1e3e7c9f33`](https://github.com/nodejs/node/commit/1e3e7c9f33)] - **doc**: update broken EVP\_BytesToKey link (Rich Trott) [#48064](https://github.com/nodejs/node/pull/48064)
* \[[`5917ba1838`](https://github.com/nodejs/node/commit/5917ba1838)] - **doc**: update broken spkac link (Rich Trott) [#48063](https://github.com/nodejs/node/pull/48063)
* \[[`0e4a3b7db1`](https://github.com/nodejs/node/commit/0e4a3b7db1)] - **doc**: document node-api version process (Chengzhong Wu) [#47972](https://github.com/nodejs/node/pull/47972)
* \[[`85bbaa94ea`](https://github.com/nodejs/node/commit/85bbaa94ea)] - **doc**: update process.versions properties (Saba Kharanauli) [#48019](https://github.com/nodejs/node/pull/48019)
* \[[`7660eb591a`](https://github.com/nodejs/node/commit/7660eb591a)] - **doc**: fix typo in binding functions (Deokjin Kim) [#48003](https://github.com/nodejs/node/pull/48003)
* \[[`2f5dbca690`](https://github.com/nodejs/node/commit/2f5dbca690)] - **doc**: mark Node.js 14 as End-of-Life (Richard Lau) [#48023](https://github.com/nodejs/node/pull/48023)
* \[[`3b94a739f2`](https://github.com/nodejs/node/commit/3b94a739f2)] - **doc**: clarify CRYPTO\_CUSTOM\_ENGINE\_NOT\_SUPPORTED (Tobias Nießen) [#47976](https://github.com/nodejs/node/pull/47976)
* \[[`9e381cfa89`](https://github.com/nodejs/node/commit/9e381cfa89)] - **doc**: add heading for permission model limitations (Tobias Nießen) [#47989](https://github.com/nodejs/node/pull/47989)
* \[[`802db923e0`](https://github.com/nodejs/node/commit/802db923e0)] - **doc,vm**: clarify usage of cachedData in vm.compileFunction() (Darshan Sen) [#48193](https://github.com/nodejs/node/pull/48193)
* \[[`11a3434810`](https://github.com/nodejs/node/commit/11a3434810)] - **esm**: remove support for arrays in `import` internal method (Antoine du Hamel) [#48296](https://github.com/nodejs/node/pull/48296)
* \[[`3b00f3afef`](https://github.com/nodejs/node/commit/3b00f3afef)] - **esm**: handle `globalPreload` hook returning a nullish value (Antoine du Hamel) [#48249](https://github.com/nodejs/node/pull/48249)
* \[[`3c7846d7e1`](https://github.com/nodejs/node/commit/3c7846d7e1)] - **esm**: handle more error types thrown from the loader thread (Antoine du Hamel) [#48247](https://github.com/nodejs/node/pull/48247)
* \[[`60ce2bcabc`](https://github.com/nodejs/node/commit/60ce2bcabc)] - **http**: send implicit headers on HEAD with no body (Matteo Collina) [#48108](https://github.com/nodejs/node/pull/48108)
* \[[`72de4e7170`](https://github.com/nodejs/node/commit/72de4e7170)] - **lib**: do not disable linter for entire files (Antoine du Hamel) [#48299](https://github.com/nodejs/node/pull/48299)
* \[[`10cc60fc91`](https://github.com/nodejs/node/commit/10cc60fc91)] - **lib**: use existing `isWindows` variable (sinkhaha) [#48134](https://github.com/nodejs/node/pull/48134)
* \[[`a90010aae9`](https://github.com/nodejs/node/commit/a90010aae9)] - **lib**: support FORCE\_COLOR for non TTY streams (Moshe Atlow) [#48034](https://github.com/nodejs/node/pull/48034)
* \[[`b1828b325e`](https://github.com/nodejs/node/commit/b1828b325e)] - **(SEMVER-MINOR)** **lib**: implement AbortSignal.any() (Chemi Atlow) [#47821](https://github.com/nodejs/node/pull/47821)
* \[[`8f1b86961f`](https://github.com/nodejs/node/commit/8f1b86961f)] - **meta**: bump github/codeql-action from 2.3.3 to 2.3.6 (dependabot\[bot]) [#48287](https://github.com/nodejs/node/pull/48287)
* \[[`1b87ccdf70`](https://github.com/nodejs/node/commit/1b87ccdf70)] - **meta**: bump actions/setup-python from 4.6.0 to 4.6.1 (dependabot\[bot]) [#48286](https://github.com/nodejs/node/pull/48286)
* \[[`10715aea26`](https://github.com/nodejs/node/commit/10715aea26)] - **meta**: bump codecov/codecov-action from 3.1.3 to 3.1.4 (dependabot\[bot]) [#48285](https://github.com/nodejs/node/pull/48285)
* \[[`79f73778ab`](https://github.com/nodejs/node/commit/79f73778ab)] - **meta**: remove dont-land-on-v14 auto labeling (Shrujal Shah) [#48031](https://github.com/nodejs/node/pull/48031)
* \[[`9c5711f3ea`](https://github.com/nodejs/node/commit/9c5711f3ea)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#48010](https://github.com/nodejs/node/pull/48010)
* \[[`6d6bf3ee52`](https://github.com/nodejs/node/commit/6d6bf3ee52)] - **module**: reduce the number of URL initializations (Yagiz Nizipli) [#48272](https://github.com/nodejs/node/pull/48272)
* \[[`f380953103`](https://github.com/nodejs/node/commit/f380953103)] - **module**: change default resolver to not throw on unknown scheme (Gil Tayar) [#47824](https://github.com/nodejs/node/pull/47824)
* \[[`950185b0c0`](https://github.com/nodejs/node/commit/950185b0c0)] - **net**: fix address iteration with autoSelectFamily (Fedor Indutny) [#48258](https://github.com/nodejs/node/pull/48258)
* \[[`5ddca72e62`](https://github.com/nodejs/node/commit/5ddca72e62)] - **net**: fix family autoselection SSL connection handling (Paolo Insogna) [#48189](https://github.com/nodejs/node/pull/48189)
* \[[`750e53ca3c`](https://github.com/nodejs/node/commit/750e53ca3c)] - **net**: fix family autoselection timeout handling (Paolo Insogna) [#47860](https://github.com/nodejs/node/pull/47860)
* \[[`a94f87ed99`](https://github.com/nodejs/node/commit/a94f87ed99)] - **(SEMVER-MINOR)** **node-api**: define version 9 (Chengzhong Wu) [#48151](https://github.com/nodejs/node/pull/48151)
* \[[`e834979818`](https://github.com/nodejs/node/commit/e834979818)] - **node-api**: add status napi\_cannot\_run\_js (Gabriel Schulhof) [#47986](https://github.com/nodejs/node/pull/47986)
* \[[`eafe0c3ec6`](https://github.com/nodejs/node/commit/eafe0c3ec6)] - **node-api**: napi\_ref on all types is experimental (Vladimir Morozov) [#47975](https://github.com/nodejs/node/pull/47975)
* \[[`9a034746f5`](https://github.com/nodejs/node/commit/9a034746f5)] - **src**: add Realm document in the src README.md (Chengzhong Wu) [#47932](https://github.com/nodejs/node/pull/47932)
* \[[`b8f4070f71`](https://github.com/nodejs/node/commit/b8f4070f71)] - **src**: check node\_extra\_ca\_certs after openssl cfg (Raghu Saxena) [#48159](https://github.com/nodejs/node/pull/48159)
* \[[`0347a18056`](https://github.com/nodejs/node/commit/0347a18056)] - **src**: include missing header in node\_sea.h (Joyee Cheung) [#48152](https://github.com/nodejs/node/pull/48152)
* \[[`45c3782c20`](https://github.com/nodejs/node/commit/45c3782c20)] - **src**: remove INT\_MAX asserts in SecretKeyGenTraits (Tobias Nießen) [#48053](https://github.com/nodejs/node/pull/48053)
* \[[`b25e7045ad`](https://github.com/nodejs/node/commit/b25e7045ad)] - **src**: avoid prototype access in binding templates (Joyee Cheung) [#47913](https://github.com/nodejs/node/pull/47913)
* \[[`33aa373eec`](https://github.com/nodejs/node/commit/33aa373eec)] - **src**: use Blob{Des|S}erializer for SEA blobs (Joyee Cheung) [#47962](https://github.com/nodejs/node/pull/47962)
* \[[`9e2b13dfa7`](https://github.com/nodejs/node/commit/9e2b13dfa7)] - **stream**: deprecate asIndexedPairs (Chemi Atlow) [#48102](https://github.com/nodejs/node/pull/48102)
* \[[`96c323dee2`](https://github.com/nodejs/node/commit/96c323dee2)] - **test**: mark test-child-process-pipe-dataflow as flaky (Moshe Atlow) [#48334](https://github.com/nodejs/node/pull/48334)
* \[[`9875885357`](https://github.com/nodejs/node/commit/9875885357)] - **test**: adapt tests for OpenSSL 3.1 (OttoHollmann) [#47859](https://github.com/nodejs/node/pull/47859)
* \[[`3440d7c6bf`](https://github.com/nodejs/node/commit/3440d7c6bf)] - **test**: unflake test-vm-timeout-escape-nexttick (Santiago Gimeno) [#48078](https://github.com/nodejs/node/pull/48078)
* \[[`215b2bc72c`](https://github.com/nodejs/node/commit/215b2bc72c)] - **test**: fix zlib version regex (Luigi Pinca) [#48227](https://github.com/nodejs/node/pull/48227)
* \[[`e12ee59d26`](https://github.com/nodejs/node/commit/e12ee59d26)] - **test**: use lower security level in s\_client (Luigi Pinca) [#48192](https://github.com/nodejs/node/pull/48192)
* \[[`1dabc7390c`](https://github.com/nodejs/node/commit/1dabc7390c)] - _**Revert**_ "**test**: unskip negative-settimeout.any.js WPT" (Filip Skokan) [#48182](https://github.com/nodejs/node/pull/48182)
* \[[`c1c4796a86`](https://github.com/nodejs/node/commit/c1c4796a86)] - **test**: mark test\_cannot\_run\_js as flaky (Keyhan Vakil) [#48181](https://github.com/nodejs/node/pull/48181)
* \[[`8c49d74002`](https://github.com/nodejs/node/commit/8c49d74002)] - **test**: fix flaky test-runner-watch-mode (Moshe Atlow) [#48144](https://github.com/nodejs/node/pull/48144)
* \[[`6388766862`](https://github.com/nodejs/node/commit/6388766862)] - **test**: skip test-http-pipeline-flood on IBM i (Abdirahim Musse) [#48048](https://github.com/nodejs/node/pull/48048)
* \[[`8d2a3b1952`](https://github.com/nodejs/node/commit/8d2a3b1952)] - **test**: ignore helper files in WPTs (Filip Skokan) [#48079](https://github.com/nodejs/node/pull/48079)
* \[[`7a96d825fd`](https://github.com/nodejs/node/commit/7a96d825fd)] - **test**: move `test-cluster-primary-error` flaky test (Yagiz Nizipli) [#48039](https://github.com/nodejs/node/pull/48039)
* \[[`a80dd3a8b3`](https://github.com/nodejs/node/commit/a80dd3a8b3)] - **test**: fix suite signal (Benjamin Gruenbaum) [#47800](https://github.com/nodejs/node/pull/47800)
* \[[`a41cfd183f`](https://github.com/nodejs/node/commit/a41cfd183f)] - **test**: fix parsing test flags (Daeyeon Jeong) [#48012](https://github.com/nodejs/node/pull/48012)
* \[[`4d4e506f2b`](https://github.com/nodejs/node/commit/4d4e506f2b)] - **test,doc,sea**: run SEA tests on ppc64 (Darshan Sen) [#48111](https://github.com/nodejs/node/pull/48111)
* \[[`44411fc40c`](https://github.com/nodejs/node/commit/44411fc40c)] - **test\_runner**: apply `runOnly` on suites (Moshe Atlow) [#48279](https://github.com/nodejs/node/pull/48279)
* \[[`3f259b7a30`](https://github.com/nodejs/node/commit/3f259b7a30)] - **test\_runner**: emit `test:watch:drained` event (Moshe Atlow) [#48259](https://github.com/nodejs/node/pull/48259)
* \[[`c9f8e8c562`](https://github.com/nodejs/node/commit/c9f8e8c562)] - **test\_runner**: stop watch mode when abortSignal aborted (Moshe Atlow) [#48259](https://github.com/nodejs/node/pull/48259)
* \[[`f3268d64cb`](https://github.com/nodejs/node/commit/f3268d64cb)] - **test\_runner**: fix global after hook (Moshe Atlow) [#48231](https://github.com/nodejs/node/pull/48231)
* \[[`15336c3139`](https://github.com/nodejs/node/commit/15336c3139)] - **test\_runner**: remove redundant check from coverage (Colin Ihrig) [#48070](https://github.com/nodejs/node/pull/48070)
* \[[`750d3e8606`](https://github.com/nodejs/node/commit/750d3e8606)] - **test\_runner**: pass FORCE\_COLOR to child process (Moshe Atlow) [#48057](https://github.com/nodejs/node/pull/48057)
* \[[`3278542243`](https://github.com/nodejs/node/commit/3278542243)] - **test\_runner**: dont split lines on `test:stdout` (Moshe Atlow) [#48057](https://github.com/nodejs/node/pull/48057)
* \[[`027c531766`](https://github.com/nodejs/node/commit/027c531766)] - **test\_runner**: fix test deserialize edge cases (Moshe Atlow) [#48106](https://github.com/nodejs/node/pull/48106)
* \[[`2b797a6d39`](https://github.com/nodejs/node/commit/2b797a6d39)] - **test\_runner**: delegate stderr and stdout formatting to reporter (Shiba) [#48045](https://github.com/nodejs/node/pull/48045)
* \[[`23d310bee8`](https://github.com/nodejs/node/commit/23d310bee8)] - **test\_runner**: display dot report as wide as the terminal width (Raz Luvaton) [#48038](https://github.com/nodejs/node/pull/48038)
* \[[`fd2620dcf1`](https://github.com/nodejs/node/commit/fd2620dcf1)] - **tls**: reapply servername on happy eyeballs connect (Fedor Indutny) [#48255](https://github.com/nodejs/node/pull/48255)
* \[[`62f847d0b3`](https://github.com/nodejs/node/commit/62f847d0b3)] - **tools**: update rollup lint-md-dependencies (Node.js GitHub Bot) [#48329](https://github.com/nodejs/node/pull/48329)
* \[[`3e97826a66`](https://github.com/nodejs/node/commit/3e97826a66)] - _**Revert**_ "**tools**: open issue when update workflow fails" (Marco Ippolito) [#48312](https://github.com/nodejs/node/pull/48312)
* \[[`5f08bfe35f`](https://github.com/nodejs/node/commit/5f08bfe35f)] - **tools**: don't gitignore base64 config.h (Ben Noordhuis) [#48174](https://github.com/nodejs/node/pull/48174)
* \[[`ded0e2d755`](https://github.com/nodejs/node/commit/ded0e2d755)] - **tools**: update LICENSE and license-builder.sh (Santiago Gimeno) [#48078](https://github.com/nodejs/node/pull/48078)
* \[[`07aa264366`](https://github.com/nodejs/node/commit/07aa264366)] - **tools**: automate histogram update (Marco Ippolito) [#48171](https://github.com/nodejs/node/pull/48171)
* \[[`1416b75eaa`](https://github.com/nodejs/node/commit/1416b75eaa)] - **tools**: use shasum instead of sha256sum (Luigi Pinca) [#48229](https://github.com/nodejs/node/pull/48229)
* \[[`b81e9d9b7b`](https://github.com/nodejs/node/commit/b81e9d9b7b)] - **tools**: harmonize `dep_updaters` scripts (Antoine du Hamel) [#48201](https://github.com/nodejs/node/pull/48201)
* \[[`a60bc41e53`](https://github.com/nodejs/node/commit/a60bc41e53)] - **tools**: deps update authenticate github api request (Andrea Fassina) [#48200](https://github.com/nodejs/node/pull/48200)
* \[[`7478ed014e`](https://github.com/nodejs/node/commit/7478ed014e)] - **tools**: order dependency jobs alphabetically (Luca) [#48184](https://github.com/nodejs/node/pull/48184)
* \[[`568a705799`](https://github.com/nodejs/node/commit/568a705799)] - **tools**: refactor v8\_pch config (Michaël Zasso) [#47364](https://github.com/nodejs/node/pull/47364)
* \[[`801573ba46`](https://github.com/nodejs/node/commit/801573ba46)] - **tools**: log and verify sha256sum (Andrea Fassina) [#48088](https://github.com/nodejs/node/pull/48088)
* \[[`db62325e18`](https://github.com/nodejs/node/commit/db62325e18)] - **tools**: open issue when update workflow fails (Marco Ippolito) [#48018](https://github.com/nodejs/node/pull/48018)
* \[[`ad8a68856d`](https://github.com/nodejs/node/commit/ad8a68856d)] - **tools**: alphabetize CODEOWNERS (Rich Trott) [#48124](https://github.com/nodejs/node/pull/48124)
* \[[`4cf5a9edaf`](https://github.com/nodejs/node/commit/4cf5a9edaf)] - **tools**: use latest upstream commit for zlib updates (Andrea Fassina) [#48054](https://github.com/nodejs/node/pull/48054)
* \[[`8d93af381b`](https://github.com/nodejs/node/commit/8d93af381b)] - **tools**: add security-wg as dep updaters owner (Marco Ippolito) [#48113](https://github.com/nodejs/node/pull/48113)
* \[[`5325be1d99`](https://github.com/nodejs/node/commit/5325be1d99)] - **tools**: port js2c.py to C++ (Joyee Cheung) [#46997](https://github.com/nodejs/node/pull/46997)
* \[[`6c60d90277`](https://github.com/nodejs/node/commit/6c60d90277)] - **tools**: fix race condition when npm installing (Tobias Nießen) [#48101](https://github.com/nodejs/node/pull/48101)
* \[[`0ab840a58f`](https://github.com/nodejs/node/commit/0ab840a58f)] - **tools**: refloat 7 Node.js patches to cpplint.py (Rich Trott) [#48098](https://github.com/nodejs/node/pull/48098)
* \[[`a298193378`](https://github.com/nodejs/node/commit/a298193378)] - **tools**: update cpplint to 1.6.1 (Yagiz Nizipli) [#48098](https://github.com/nodejs/node/pull/48098)
* \[[`f6725751b7`](https://github.com/nodejs/node/commit/f6725751b7)] - **tools**: update eslint to 8.41.0 (Node.js GitHub Bot) [#48097](https://github.com/nodejs/node/pull/48097)
* \[[`6539361f4e`](https://github.com/nodejs/node/commit/6539361f4e)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#48096](https://github.com/nodejs/node/pull/48096)
* \[[`5d94dbb951`](https://github.com/nodejs/node/commit/5d94dbb951)] - **tools**: update doc to remark-parse\@10.0.2 (Node.js GitHub Bot) [#48095](https://github.com/nodejs/node/pull/48095)
* \[[`2226088048`](https://github.com/nodejs/node/commit/2226088048)] - **tools**: add debug logs (Marco Ippolito) [#48060](https://github.com/nodejs/node/pull/48060)
* \[[`0c8c383583`](https://github.com/nodejs/node/commit/0c8c383583)] - **tools**: fix zconf.h path (Luigi Pinca) [#48089](https://github.com/nodejs/node/pull/48089)
* \[[`6adaf4c648`](https://github.com/nodejs/node/commit/6adaf4c648)] - **tools**: update remark-preset-lint-node to 4.0.0 (Node.js GitHub Bot) [#47995](https://github.com/nodejs/node/pull/47995)
* \[[`92b3334231`](https://github.com/nodejs/node/commit/92b3334231)] - **url**: clean vertical alignment of docs (Robin Ury) [#48037](https://github.com/nodejs/node/pull/48037)
* \[[`ebb6536775`](https://github.com/nodejs/node/commit/ebb6536775)] - **url**: call `ada::can_parse` directly (Yagiz Nizipli) [#47919](https://github.com/nodejs/node/pull/47919)
* \[[`ed4514294a`](https://github.com/nodejs/node/commit/ed4514294a)] - **vm**: properly handle defining symbol props (Nicolas DUBIEN) [#47572](https://github.com/nodejs/node/pull/47572)

<a id="20.2.0"></a>

## 2023-05-16, Version 20.2.0 (Current), @targos

### Notable Changes

* \[[`c092df9094`](https://github.com/nodejs/node/commit/c092df9094)] - **doc**: add ovflowd to collaborators (Claudio Wunder) [#47844](https://github.com/nodejs/node/pull/47844)
* \[[`4197a9a5a0`](https://github.com/nodejs/node/commit/4197a9a5a0)] - **(SEMVER-MINOR)** **http**: prevent writing to the body when not allowed by HTTP spec (Gerrard Lindsay) [#47732](https://github.com/nodejs/node/pull/47732)
* \[[`c4596b9ce7`](https://github.com/nodejs/node/commit/c4596b9ce7)] - **(SEMVER-MINOR)** **sea**: add option to disable the experimental SEA warning (Darshan Sen) [#47588](https://github.com/nodejs/node/pull/47588)
* \[[`17befe008c`](https://github.com/nodejs/node/commit/17befe008c)] - **(SEMVER-MINOR)** **test\_runner**: add `skip`, `todo`, and `only` shorthands to `test` (Chemi Atlow) [#47909](https://github.com/nodejs/node/pull/47909)
* \[[`a0634d7f89`](https://github.com/nodejs/node/commit/a0634d7f89)] - **(SEMVER-MINOR)** **url**: add value argument to `URLSearchParams` `has` and `delete` methods (Sankalp Shubham) [#47885](https://github.com/nodejs/node/pull/47885)

### Commits

* \[[`456fca0d9c`](https://github.com/nodejs/node/commit/456fca0d9c)] - **bootstrap**: initialize per-isolate properties of bindings separately (Joyee Cheung) [#47768](https://github.com/nodejs/node/pull/47768)
* \[[`d6d12bf978`](https://github.com/nodejs/node/commit/d6d12bf978)] - **bootstrap**: log isolate data info in mksnapshot debug logs (Joyee Cheung) [#47768](https://github.com/nodejs/node/pull/47768)
* \[[`e457d89a1b`](https://github.com/nodejs/node/commit/e457d89a1b)] - **buffer**: combine checking range of sourceStart in `buf.copy` (Deokjin Kim) [#47758](https://github.com/nodejs/node/pull/47758)
* \[[`00668fcfb4`](https://github.com/nodejs/node/commit/00668fcfb4)] - **child\_process**: use signal.reason in child process abort (Debadree Chatterjee) [#47817](https://github.com/nodejs/node/pull/47817)
* \[[`d7993474ea`](https://github.com/nodejs/node/commit/d7993474ea)] - **crypto**: remove default encoding from scrypt (Tobias Nießen) [#47943](https://github.com/nodejs/node/pull/47943)
* \[[`09fb74a7cc`](https://github.com/nodejs/node/commit/09fb74a7cc)] - **crypto**: fix webcrypto private/secret import with empty usages (Filip Skokan) [#47877](https://github.com/nodejs/node/pull/47877)
* \[[`e9c6ee74f3`](https://github.com/nodejs/node/commit/e9c6ee74f3)] - **crypto**: remove default encoding from pbkdf2 (Tobias Nießen) [#47869](https://github.com/nodejs/node/pull/47869)
* \[[`b7f13a8679`](https://github.com/nodejs/node/commit/b7f13a8679)] - **deps**: update simdutf to 3.2.9 (Node.js GitHub Bot) [#47983](https://github.com/nodejs/node/pull/47983)
* \[[`b16f6da153`](https://github.com/nodejs/node/commit/b16f6da153)] - **deps**: V8: cherry-pick 5f025d1ca2ca (Michaël Zasso) [#47610](https://github.com/nodejs/node/pull/47610)
* \[[`99f8fcab45`](https://github.com/nodejs/node/commit/99f8fcab45)] - **deps**: V8: cherry-pick a8a11a87cb72 (Michaël Zasso) [#47610](https://github.com/nodejs/node/pull/47610)
* \[[`c2b14b4c78`](https://github.com/nodejs/node/commit/c2b14b4c78)] - **deps**: update ada to 2.4.0 (Node.js GitHub Bot) [#47922](https://github.com/nodejs/node/pull/47922)
* \[[`cad42e7a56`](https://github.com/nodejs/node/commit/cad42e7a56)] - **deps**: V8: cherry-pick 1b471b796022 (Lu Yahan) [#47399](https://github.com/nodejs/node/pull/47399)
* \[[`7b2f17ca59`](https://github.com/nodejs/node/commit/7b2f17ca59)] - **deps**: upgrade npm to 9.6.6 (npm team) [#47862](https://github.com/nodejs/node/pull/47862)
* \[[`d23b1af562`](https://github.com/nodejs/node/commit/d23b1af562)] - **deps**: update ada to 2.3.1 (Node.js GitHub Bot) [#47893](https://github.com/nodejs/node/pull/47893)
* \[[`72340c98fb`](https://github.com/nodejs/node/commit/72340c98fb)] - **dgram**: convert macro to template (Tobias Nießen) [#47891](https://github.com/nodejs/node/pull/47891)
* \[[`9be922892f`](https://github.com/nodejs/node/commit/9be922892f)] - **dns**: call `ada::idna::to_ascii` directly from c++ (Yagiz Nizipli) [#47920](https://github.com/nodejs/node/pull/47920)
* \[[`4a1e97156a`](https://github.com/nodejs/node/commit/4a1e97156a)] - **doc**: add missing deprecated blocks to cluster (Tobias Nießen) [#47981](https://github.com/nodejs/node/pull/47981)
* \[[`13118a19ee`](https://github.com/nodejs/node/commit/13118a19ee)] - **doc**: update description of global (Tobias Nießen) [#47969](https://github.com/nodejs/node/pull/47969)
* \[[`372796440b`](https://github.com/nodejs/node/commit/372796440b)] - **doc**: update measure memory rejection information (Yash Ladha) [#41639](https://github.com/nodejs/node/pull/41639)
* \[[`7ecc6740e4`](https://github.com/nodejs/node/commit/7ecc6740e4)] - **doc**: fix broken link to TC39 import attributes proposal (Rich Trott) [#47954](https://github.com/nodejs/node/pull/47954)
* \[[`b9771c95c7`](https://github.com/nodejs/node/commit/b9771c95c7)] - **doc**: fix broken link (Rich Trott) [#47953](https://github.com/nodejs/node/pull/47953)
* \[[`6f5ba92e61`](https://github.com/nodejs/node/commit/6f5ba92e61)] - **doc**: remove broken link (Rich Trott) [#47942](https://github.com/nodejs/node/pull/47942)
* \[[`c9ffc555f1`](https://github.com/nodejs/node/commit/c9ffc555f1)] - **doc**: document make lint-md-clean (Matteo Collina) [#47926](https://github.com/nodejs/node/pull/47926)
* \[[`7ed99e8ba5`](https://github.com/nodejs/node/commit/7ed99e8ba5)] - **doc**: mark global object as legacy (Mert Can Altın) [#47819](https://github.com/nodejs/node/pull/47819)
* \[[`bf39f2d252`](https://github.com/nodejs/node/commit/bf39f2d252)] - **doc**: ntfs junction points must link to directories (Ben Noordhuis) [#47907](https://github.com/nodejs/node/pull/47907)
* \[[`4dfc3890d8`](https://github.com/nodejs/node/commit/4dfc3890d8)] - **doc**: improve `permission.has` description (Daeyeon Jeong) [#47875](https://github.com/nodejs/node/pull/47875)
* \[[`93f1aa2856`](https://github.com/nodejs/node/commit/93f1aa2856)] - **doc**: fix params names (Dmitry Semigradsky) [#47853](https://github.com/nodejs/node/pull/47853)
* \[[`9a362aa2fb`](https://github.com/nodejs/node/commit/9a362aa2fb)] - **doc**: update supported version of FreeBSD to 12.4 (Michaël Zasso) [#47838](https://github.com/nodejs/node/pull/47838)
* \[[`89c70dc6e6`](https://github.com/nodejs/node/commit/89c70dc6e6)] - **doc**: add stability experimental to pm (Rafael Gonzaga) [#47890](https://github.com/nodejs/node/pull/47890)
* \[[`f96fb2eee7`](https://github.com/nodejs/node/commit/f96fb2eee7)] - **doc**: swap Matteo with Rafael in the stewards (Rafael Gonzaga) [#47841](https://github.com/nodejs/node/pull/47841)
* \[[`1666a146e3`](https://github.com/nodejs/node/commit/1666a146e3)] - **doc**: add valgrind suppression details (Kevin Eady) [#47760](https://github.com/nodejs/node/pull/47760)
* \[[`e53e8231ff`](https://github.com/nodejs/node/commit/e53e8231ff)] - **doc**: replace EOL versions in README (Tobias Nießen) [#47833](https://github.com/nodejs/node/pull/47833)
* \[[`c092df9094`](https://github.com/nodejs/node/commit/c092df9094)] - **doc**: add ovflowd to collaborators (Claudio Wunder) [#47844](https://github.com/nodejs/node/pull/47844)
* \[[`f7106765b3`](https://github.com/nodejs/node/commit/f7106765b3)] - **doc**: update BUILDING.md previous versions links (Tobias Nießen) [#47835](https://github.com/nodejs/node/pull/47835)
* \[[`811b43c215`](https://github.com/nodejs/node/commit/811b43c215)] - **doc,test**: update the v8.startupSnapshot doc and test the example (Joyee Cheung) [#47468](https://github.com/nodejs/node/pull/47468)
* \[[`1ec640ac70`](https://github.com/nodejs/node/commit/1ec640ac70)] - **esm**: do not use `'beforeExit'` on the main thread (Antoine du Hamel) [#47964](https://github.com/nodejs/node/pull/47964)
* \[[`106dc612d6`](https://github.com/nodejs/node/commit/106dc612d6)] - **fs**: make readdir recursive algorithm iterative (Ethan Arrowood) [#47650](https://github.com/nodejs/node/pull/47650)
* \[[`a0da2348a8`](https://github.com/nodejs/node/commit/a0da2348a8)] - **fs**: move fs\_use\_promises\_symbol to per-isolate symbols (Joyee Cheung) [#47768](https://github.com/nodejs/node/pull/47768)
* \[[`4197a9a5a0`](https://github.com/nodejs/node/commit/4197a9a5a0)] - **(SEMVER-MINOR)** **http**: prevent writing to the body when not allowed by HTTP spec (Gerrard Lindsay) [#47732](https://github.com/nodejs/node/pull/47732)
* \[[`a4d6543598`](https://github.com/nodejs/node/commit/a4d6543598)] - **http2**: improve nghttp2 error callback (Tobias Nießen) [#47840](https://github.com/nodejs/node/pull/47840)
* \[[`a4fed6c580`](https://github.com/nodejs/node/commit/a4fed6c580)] - **lib**: update comment (sinkhaha) [#47884](https://github.com/nodejs/node/pull/47884)
* \[[`fd8bec7b2b`](https://github.com/nodejs/node/commit/fd8bec7b2b)] - **meta**: bump step-security/harden-runner from 2.3.1 to 2.4.0 (Rich Trott) [#47980](https://github.com/nodejs/node/pull/47980)
* \[[`f5b4b6d5dc`](https://github.com/nodejs/node/commit/f5b4b6d5dc)] - **meta**: bump github/codeql-action from 2.3.2 to 2.3.3 (Rich Trott) [#47979](https://github.com/nodejs/node/pull/47979)
* \[[`c05c0a2359`](https://github.com/nodejs/node/commit/c05c0a2359)] - **meta**: bump actions/setup-python from 4.5.0 to 4.6.0 (Rich Trott) [#47968](https://github.com/nodejs/node/pull/47968)
* \[[`2a3d6d97cb`](https://github.com/nodejs/node/commit/2a3d6d97cb)] - **meta**: add security-wg ping to permission.js (Rafael Gonzaga) [#47941](https://github.com/nodejs/node/pull/47941)
* \[[`6c158e8dd1`](https://github.com/nodejs/node/commit/6c158e8dd1)] - **meta**: bump step-security/harden-runner from 2.2.1 to 2.3.1 (dependabot\[bot]) [#47808](https://github.com/nodejs/node/pull/47808)
* \[[`f7a8094d37`](https://github.com/nodejs/node/commit/f7a8094d37)] - **meta**: bump actions/setup-python from 4.5.0 to 4.6.0 (dependabot\[bot]) [#47806](https://github.com/nodejs/node/pull/47806)
* \[[`0f58e48792`](https://github.com/nodejs/node/commit/0f58e48792)] - **meta**: bump actions/checkout from 3.3.0 to 3.5.2 (dependabot\[bot]) [#47805](https://github.com/nodejs/node/pull/47805)
* \[[`652b06dd82`](https://github.com/nodejs/node/commit/652b06dd82)] - **meta**: remove extra space in scorecard workflow (Mestery) [#47805](https://github.com/nodejs/node/pull/47805)
* \[[`9f06eaccaf`](https://github.com/nodejs/node/commit/9f06eaccaf)] - **meta**: bump github/codeql-action from 2.2.9 to 2.3.2 (dependabot\[bot]) [#47809](https://github.com/nodejs/node/pull/47809)
* \[[`977fd7cf35`](https://github.com/nodejs/node/commit/977fd7cf35)] - **meta**: bump codecov/codecov-action from 3.1.1 to 3.1.3 (dependabot\[bot]) [#47807](https://github.com/nodejs/node/pull/47807)
* \[[`c19385c154`](https://github.com/nodejs/node/commit/c19385c154)] - **module**: refactor to use `normalizeRequirableId` in the CJS module loader (Darshan Sen) [#47896](https://github.com/nodejs/node/pull/47896)
* \[[`739113f2fc`](https://github.com/nodejs/node/commit/739113f2fc)] - **module**: block requiring `test/reporters` without scheme (Moshe Atlow) [#47831](https://github.com/nodejs/node/pull/47831)
* \[[`f489c6710c`](https://github.com/nodejs/node/commit/f489c6710c)] - **(NODE-API-SEMVER-MAJOR)** **node-api**: get Node API version used by addon (Vladimir Morozov) [#45715](https://github.com/nodejs/node/pull/45715)
* \[[`7222f9d74b`](https://github.com/nodejs/node/commit/7222f9d74b)] - **path**: indicate index of wrong resolve() parameter (sosoba) [#47660](https://github.com/nodejs/node/pull/47660)
* \[[`7dd32f1536`](https://github.com/nodejs/node/commit/7dd32f1536)] - **permission**: remove unused function declaration (Deokjin Kim) [#47957](https://github.com/nodejs/node/pull/47957)
* \[[`af86625a05`](https://github.com/nodejs/node/commit/af86625a05)] - **permission**: resolve reference to absolute path only for fs permission (Daeyeon Jeong) [#47930](https://github.com/nodejs/node/pull/47930)
* \[[`1625ae11fe`](https://github.com/nodejs/node/commit/1625ae11fe)] - **quic**: address recent coverity warning (Michael Dawson) [#47753](https://github.com/nodejs/node/pull/47753)
* \[[`c4596b9ce7`](https://github.com/nodejs/node/commit/c4596b9ce7)] - **(SEMVER-MINOR)** **sea**: add option to disable the experimental SEA warning (Darshan Sen) [#47588](https://github.com/nodejs/node/pull/47588)
* \[[`1a7fc186bc`](https://github.com/nodejs/node/commit/1a7fc186bc)] - **sea**: allow requiring core modules with the "node:" prefix (Darshan Sen) [#47779](https://github.com/nodejs/node/pull/47779)
* \[[`786a1c5398`](https://github.com/nodejs/node/commit/786a1c5398)] - **src**: deduplicate X509Certificate::Fingerprint\* (Tobias Nießen) [#47978](https://github.com/nodejs/node/pull/47978)
* \[[`060c1d502b`](https://github.com/nodejs/node/commit/060c1d502b)] - **src**: stop copying code cache, part 2 (Keyhan Vakil) [#47958](https://github.com/nodejs/node/pull/47958)
* \[[`1aec718619`](https://github.com/nodejs/node/commit/1aec718619)] - **(SEMVER-MINOR)** **src**: add cjs\_module\_lexer\_version base64\_version (Jithil P Ponnan) [#45629](https://github.com/nodejs/node/pull/45629)
* \[[`0c06bfd8dc`](https://github.com/nodejs/node/commit/0c06bfd8dc)] - **src**: move BlobSerializerDeserializer to a separate header file (Darshan Sen) [#47933](https://github.com/nodejs/node/pull/47933)
* \[[`bd553e7521`](https://github.com/nodejs/node/commit/bd553e7521)] - **src**: rename SKIP\_CHECK\_SIZE to SKIP\_CHECK\_STRLEN (Tobias Nießen) [#47845](https://github.com/nodejs/node/pull/47845)
* \[[`190596c189`](https://github.com/nodejs/node/commit/190596c189)] - **src**: register external references for source code (Keyhan Vakil) [#47055](https://github.com/nodejs/node/pull/47055)
* \[[`4293cc47f4`](https://github.com/nodejs/node/commit/4293cc47f4)] - **src**: support V8 experimental shared values in messaging (Shu-yu Guo) [#47706](https://github.com/nodejs/node/pull/47706)
* \[[`9bc5d78f0c`](https://github.com/nodejs/node/commit/9bc5d78f0c)] - **src**: register ext reference for Fingerprint512 (Tobias Nießen) [#47892](https://github.com/nodejs/node/pull/47892)
* \[[`a11507e23b`](https://github.com/nodejs/node/commit/a11507e23b)] - **src**: stop copying code cache (Keyhan Vakil) [#47144](https://github.com/nodejs/node/pull/47144)
* \[[`515c9b8de6`](https://github.com/nodejs/node/commit/515c9b8de6)] - **src**: clarify the parameter name in `Permission::Apply` (Daeyeon Jeong) [#47874](https://github.com/nodejs/node/pull/47874)
* \[[`c4217613f5`](https://github.com/nodejs/node/commit/c4217613f5)] - **src**: fix creating an ArrayBuffer from a Blob created with `openAsBlob` (Daeyeon Jeong) [#47691](https://github.com/nodejs/node/pull/47691)
* \[[`4bc17fd67b`](https://github.com/nodejs/node/commit/4bc17fd67b)] - **src**: avoid strcmp() with Utf8Value (Tobias Nießen) [#47827](https://github.com/nodejs/node/pull/47827)
* \[[`d358317f70`](https://github.com/nodejs/node/commit/d358317f70)] - **src**: get binding data store directly from the realm (Joyee Cheung) [#47437](https://github.com/nodejs/node/pull/47437)
* \[[`b04d51a0b5`](https://github.com/nodejs/node/commit/b04d51a0b5)] - **src**: prefer data accessor of string and vector (Mohammed Keyvanzadeh) [#47750](https://github.com/nodejs/node/pull/47750)
* \[[`2952cc576c`](https://github.com/nodejs/node/commit/2952cc576c)] - **src**: add per-isolate SetFastMethod and Set\[Fast]MethodNoSideEffect (Joyee Cheung) [#47768](https://github.com/nodejs/node/pull/47768)
* \[[`010d2ecf94`](https://github.com/nodejs/node/commit/010d2ecf94)] - **test**: mark test-esm-loader-http-imports as flaky (Tobias Nießen) [#47987](https://github.com/nodejs/node/pull/47987)
* \[[`bb33c74c07`](https://github.com/nodejs/node/commit/bb33c74c07)] - **test**: add getRandomValues return length (Jithil P Ponnan) [#46357](https://github.com/nodejs/node/pull/46357)
* \[[`6e019586f7`](https://github.com/nodejs/node/commit/6e019586f7)] - **test**: unskip negative-settimeout.any.js WPT (Filip Skokan) [#47946](https://github.com/nodejs/node/pull/47946)
* \[[`8f547afe5f`](https://github.com/nodejs/node/commit/8f547afe5f)] - **test**: use appropriate usages for a negative import test (Filip Skokan) [#47878](https://github.com/nodejs/node/pull/47878)
* \[[`7e34f77518`](https://github.com/nodejs/node/commit/7e34f77518)] - **test**: fix webcrypto wrap unwrap tests (Filip Skokan) [#47876](https://github.com/nodejs/node/pull/47876)
* \[[`30f4f35244`](https://github.com/nodejs/node/commit/30f4f35244)] - **test**: fix output tests when path includes node version (Moshe Atlow) [#47843](https://github.com/nodejs/node/pull/47843)
* \[[`54607bfd68`](https://github.com/nodejs/node/commit/54607bfd68)] - **test**: reduce WPT concurrency (Filip Skokan) [#47834](https://github.com/nodejs/node/pull/47834)
* \[[`17945a2495`](https://github.com/nodejs/node/commit/17945a2495)] - **test**: migrate a pseudo\_tty test to use assertSnapshot (Moshe Atlow) [#47803](https://github.com/nodejs/node/pull/47803)
* \[[`c9233679e8`](https://github.com/nodejs/node/commit/c9233679e8)] - **test**: fix WPT state when process exits but workers are still running (Filip Skokan) [#47826](https://github.com/nodejs/node/pull/47826)
* \[[`34bfb69b5b`](https://github.com/nodejs/node/commit/34bfb69b5b)] - **test**: migrate message tests to use assertSnapshot (Moshe Atlow) [#47498](https://github.com/nodejs/node/pull/47498)
* \[[`d25c785c2a`](https://github.com/nodejs/node/commit/d25c785c2a)] - **test**: allow SIGBUS in signal-handler abort test (Michaël Zasso) [#47851](https://github.com/nodejs/node/pull/47851)
* \[[`aa2c7e00d7`](https://github.com/nodejs/node/commit/aa2c7e00d7)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#47921](https://github.com/nodejs/node/pull/47921)
* \[[`da27542058`](https://github.com/nodejs/node/commit/da27542058)] - **test\_runner**: use v8.serialize instead of TAP (Moshe Atlow) [#47867](https://github.com/nodejs/node/pull/47867)
* \[[`17befe008c`](https://github.com/nodejs/node/commit/17befe008c)] - **(SEMVER-MINOR)** **test\_runner**: add shorthands to `test` (Chemi Atlow) [#47909](https://github.com/nodejs/node/pull/47909)
* \[[`42db1d50a0`](https://github.com/nodejs/node/commit/42db1d50a0)] - **test\_runner**: fix ordering of test hooks (Phil Nash) [#47931](https://github.com/nodejs/node/pull/47931)
* \[[`d81c54e3a8`](https://github.com/nodejs/node/commit/d81c54e3a8)] - **test\_runner**: omit inaccessible files from coverage (Colin Ihrig) [#47850](https://github.com/nodejs/node/pull/47850)
* \[[`a4e261e910`](https://github.com/nodejs/node/commit/a4e261e910)] - **tools**: debug log for nghttp3 (Marco Ippolito) [#47992](https://github.com/nodejs/node/pull/47992)
* \[[`f6ff318d4c`](https://github.com/nodejs/node/commit/f6ff318d4c)] - **tools**: automate icu-small update (Marco Ippolito) [#47727](https://github.com/nodejs/node/pull/47727)
* \[[`706c305381`](https://github.com/nodejs/node/commit/706c305381)] - **tools**: update lint-md-dependencies to rollup\@3.21.5 (Node.js GitHub Bot) [#47903](https://github.com/nodejs/node/pull/47903)
* \[[`e22c686ca9`](https://github.com/nodejs/node/commit/e22c686ca9)] - **tools**: update eslint to 8.40.0 (Node.js GitHub Bot) [#47906](https://github.com/nodejs/node/pull/47906)
* \[[`36f7cfac93`](https://github.com/nodejs/node/commit/36f7cfac93)] - **tools**: update eslint to 8.39.0 (Node.js GitHub Bot) [#47789](https://github.com/nodejs/node/pull/47789)
* \[[`7323902a40`](https://github.com/nodejs/node/commit/7323902a40)] - **tools**: fix jsdoc lint (Moshe Atlow) [#47789](https://github.com/nodejs/node/pull/47789)
* \[[`a0634d7f89`](https://github.com/nodejs/node/commit/a0634d7f89)] - **(SEMVER-MINOR)** **url**: add value argument to has and delete methods (Sankalp Shubham) [#47885](https://github.com/nodejs/node/pull/47885)
* \[[`1b06c1e003`](https://github.com/nodejs/node/commit/1b06c1e003)] - **url**: improve `isURL` detection (Yagiz Nizipli) [#47886](https://github.com/nodejs/node/pull/47886)
* \[[`2bd869d20c`](https://github.com/nodejs/node/commit/2bd869d20c)] - **vm**: fix crash when setting \_\_proto\_\_ on context's globalThis (Feng Yu) [#47939](https://github.com/nodejs/node/pull/47939)
* \[[`e6685f9e82`](https://github.com/nodejs/node/commit/e6685f9e82)] - **vm,lib**: refactor microtaskQueue assignment logic (Khaidi Chu) [#47765](https://github.com/nodejs/node/pull/47765)
* \[[`47fea13dac`](https://github.com/nodejs/node/commit/47fea13dac)] - **worker**: support more cases when (de)serializing errors (Moshe Atlow) [#47925](https://github.com/nodejs/node/pull/47925)
* \[[`6f3876c035`](https://github.com/nodejs/node/commit/6f3876c035)] - **worker**: use snapshot in workers spawned by workers (Joyee Cheung) [#47731](https://github.com/nodejs/node/pull/47731)

<a id="20.1.0"></a>

## 2023-05-03, Version 20.1.0 (Current), @targos

### Notable Changes

* \[[`5e99598639`](https://github.com/nodejs/node/commit/5e99598639)] - **assert**: deprecate `CallTracker` (Moshe Atlow) [#47740](https://github.com/nodejs/node/pull/47740)
* \[[`2d97c89c6f`](https://github.com/nodejs/node/commit/2d97c89c6f)] - **crypto**: update root certificates to NSS 3.89 (Node.js GitHub Bot) [#47659](https://github.com/nodejs/node/pull/47659)
* \[[`ce8820e292`](https://github.com/nodejs/node/commit/ce8820e292)] - **(SEMVER-MINOR)** **dns**: expose `getDefaultResultOrder` (btea) [#46973](https://github.com/nodejs/node/pull/46973)
* \[[`9d30f469aa`](https://github.com/nodejs/node/commit/9d30f469aa)] - **doc**: add KhafraDev to collaborators (Matthew Aitken) [#47510](https://github.com/nodejs/node/pull/47510)
* \[[`439ea47a77`](https://github.com/nodejs/node/commit/439ea47a77)] - **(SEMVER-MINOR)** **fs**: add `recursive` option to `readdir` and `opendir` (Ethan Arrowood) [#41439](https://github.com/nodejs/node/pull/41439)
* \[[`a54e898dc8`](https://github.com/nodejs/node/commit/a54e898dc8)] - **(SEMVER-MINOR)** **fs**: add support for `mode` flag to specify the copy behavior of the `cp` methods (Tetsuharu Ohzeki) [#47084](https://github.com/nodejs/node/pull/47084)
* \[[`4fa773964b`](https://github.com/nodejs/node/commit/4fa773964b)] - **(SEMVER-MINOR)** **http**: add `highWaterMark` option `http.createServer` (HinataKah0) [#47405](https://github.com/nodejs/node/pull/47405)
* \[[`2b411f4b42`](https://github.com/nodejs/node/commit/2b411f4b42)] - **(SEMVER-MINOR)** **stream**: preserve object mode in `compose` (Raz Luvaton) [#47413](https://github.com/nodejs/node/pull/47413)
* \[[`5327483f31`](https://github.com/nodejs/node/commit/5327483f31)] - **(SEMVER-MINOR)** **test\_runner**: add `testNamePatterns` to `run` API (Chemi Atlow) [#47628](https://github.com/nodejs/node/pull/47628)
* \[[`bdd02a467d`](https://github.com/nodejs/node/commit/bdd02a467d)] - **(SEMVER-MINOR)** **test\_runner**: execute `before` hook on test (Chemi Atlow) [#47586](https://github.com/nodejs/node/pull/47586)
* \[[`0e70c187bc`](https://github.com/nodejs/node/commit/0e70c187bc)] - **(SEMVER-MINOR)** **test\_runner**: support combining coverage reports (Colin Ihrig) [#47686](https://github.com/nodejs/node/pull/47686)
* \[[`75c1d1b66e`](https://github.com/nodejs/node/commit/75c1d1b66e)] - **(SEMVER-MINOR)** **wasi**: make `returnOnExit` true by default (Michael Dawson) [#47390](https://github.com/nodejs/node/pull/47390)

### Commits

* \[[`33d1bd3e02`](https://github.com/nodejs/node/commit/33d1bd3e02)] - **assert**: deprecate callTracker (Moshe Atlow) [#47740](https://github.com/nodejs/node/pull/47740)
* \[[`6d87355e83`](https://github.com/nodejs/node/commit/6d87355e83)] - **benchmark**: add eventtarget creation bench (Rafael Gonzaga) [#47774](https://github.com/nodejs/node/pull/47774)
* \[[`40324a1dea`](https://github.com/nodejs/node/commit/40324a1dea)] - **benchmark**: differentiate whatwg and legacy url (Yagiz Nizipli) [#47377](https://github.com/nodejs/node/pull/47377)
* \[[`936d7cb069`](https://github.com/nodejs/node/commit/936d7cb069)] - **benchmark**: add a benchmark for `defaultResolve` (Antoine du Hamel) [#47543](https://github.com/nodejs/node/pull/47543)
* \[[`202042ee93`](https://github.com/nodejs/node/commit/202042ee93)] - **bootstrap**: support namespaced builtins in snapshot scripts (Joyee Cheung) [#47467](https://github.com/nodejs/node/pull/47467)
* \[[`30af5cee55`](https://github.com/nodejs/node/commit/30af5cee55)] - **build**: use pathlib for paths (Mohammed Keyvanzadeh) [#47581](https://github.com/nodejs/node/pull/47581)
* \[[`089c9c51e9`](https://github.com/nodejs/node/commit/089c9c51e9)] - **build**: refactor configure.py (Mohammed Keyvanzadeh) [#47667](https://github.com/nodejs/node/pull/47667)
* \[[`5b851c8074`](https://github.com/nodejs/node/commit/5b851c8074)] - **build**: add devcontainer configuration (Tierney Cyren) [#40825](https://github.com/nodejs/node/pull/40825)
* \[[`35e8b3b467`](https://github.com/nodejs/node/commit/35e8b3b467)] - **build**: bump ossf/scorecard-action from 2.1.2 to 2.1.3 (dependabot\[bot]) [#47367](https://github.com/nodejs/node/pull/47367)
* \[[`78c08243df`](https://github.com/nodejs/node/commit/78c08243df)] - **build**: replace Python linter flake8 with ruff (Christian Clauss) [#47519](https://github.com/nodejs/node/pull/47519)
* \[[`2d97c89c6f`](https://github.com/nodejs/node/commit/2d97c89c6f)] - **crypto**: update root certificates to NSS 3.89 (Node.js GitHub Bot) [#47659](https://github.com/nodejs/node/pull/47659)
* \[[`420feb41cf`](https://github.com/nodejs/node/commit/420feb41cf)] - **crypto**: remove INT\_MAX restriction in randomBytes (Tobias Nießen) [#47559](https://github.com/nodejs/node/pull/47559)
* \[[`6046779dd9`](https://github.com/nodejs/node/commit/6046779dd9)] - **deps**: disable V8 concurrent sparkplug compilation (Michaël Zasso) [#47450](https://github.com/nodejs/node/pull/47450)
* \[[`00d461e93f`](https://github.com/nodejs/node/commit/00d461e93f)] - **deps**: V8: cherry-pick c5ab3e4f0c5a (Richard Lau) [#47736](https://github.com/nodejs/node/pull/47736)
* \[[`d08dd8069f`](https://github.com/nodejs/node/commit/d08dd8069f)] - **deps**: update ada to 2.3.0 (Node.js GitHub Bot) [#47737](https://github.com/nodejs/node/pull/47737)
* \[[`996245976b`](https://github.com/nodejs/node/commit/996245976b)] - **deps**: update undici to 5.22.0 (Node.js GitHub Bot) [#47679](https://github.com/nodejs/node/pull/47679)
* \[[`f3ee3126df`](https://github.com/nodejs/node/commit/f3ee3126df)] - **deps**: update ada to 2.2.0 (Node.js GitHub Bot) [#47678](https://github.com/nodejs/node/pull/47678)
* \[[`1391d3b9ff`](https://github.com/nodejs/node/commit/1391d3b9ff)] - **deps**: add minimatch as a dependency (Moshe Atlow) [#47499](https://github.com/nodejs/node/pull/47499)
* \[[`315454350d`](https://github.com/nodejs/node/commit/315454350d)] - **deps**: update ada to 2.1.0 (Node.js GitHub Bot) [#47598](https://github.com/nodejs/node/pull/47598)
* \[[`7f7735cad9`](https://github.com/nodejs/node/commit/7f7735cad9)] - **deps**: update ICU to 73.1 release (Steven R. Loomis) [#47456](https://github.com/nodejs/node/pull/47456)
* \[[`13105c12b7`](https://github.com/nodejs/node/commit/13105c12b7)] - **deps**: patch V8 to 11.3.244.8 (Michaël Zasso) [#47536](https://github.com/nodejs/node/pull/47536)
* \[[`ede69d272a`](https://github.com/nodejs/node/commit/ede69d272a)] - **deps**: update undici to 5.21.2 (Node.js GitHub Bot) [#47508](https://github.com/nodejs/node/pull/47508)
* \[[`64b5a5f872`](https://github.com/nodejs/node/commit/64b5a5f872)] - **deps**: update simdutf to 3.2.8 (Node.js GitHub Bot) [#47507](https://github.com/nodejs/node/pull/47507)
* \[[`2664536796`](https://github.com/nodejs/node/commit/2664536796)] - **deps**: V8: cherry-pick 8e10685ff918 (Jiawen Geng) [#47440](https://github.com/nodejs/node/pull/47440)
* \[[`ba9ec91f0e`](https://github.com/nodejs/node/commit/ba9ec91f0e)] - **deps**: update undici to 5.21.1 (Node.js GitHub Bot) [#47488](https://github.com/nodejs/node/pull/47488)
* \[[`ce8820e292`](https://github.com/nodejs/node/commit/ce8820e292)] - **(SEMVER-MINOR)** **dns**: expose getDefaultResultOrder (btea) [#46973](https://github.com/nodejs/node/pull/46973)
* \[[`4c26e28c33`](https://github.com/nodejs/node/commit/4c26e28c33)] - **doc**: create maintaining folder for deps (Marco Ippolito) [#47589](https://github.com/nodejs/node/pull/47589)
* \[[`aa0ef3eabd`](https://github.com/nodejs/node/commit/aa0ef3eabd)] - **doc**: fix --allow-\* CLI flag references (Tobias Nießen) [#47804](https://github.com/nodejs/node/pull/47804)
* \[[`98603b6fd3`](https://github.com/nodejs/node/commit/98603b6fd3)] - **doc**: clarify fs permissions only affect fs module (Tobias Nießen) [#47782](https://github.com/nodejs/node/pull/47782)
* \[[`3befe5dac9`](https://github.com/nodejs/node/commit/3befe5dac9)] - **doc**: add copy node executable guide on windows (XLor) [#47781](https://github.com/nodejs/node/pull/47781)
* \[[`98450d9892`](https://github.com/nodejs/node/commit/98450d9892)] - **doc**: remove MoLow from Triagers (Moshe Atlow) [#47792](https://github.com/nodejs/node/pull/47792)
* \[[`d75036410d`](https://github.com/nodejs/node/commit/d75036410d)] - **doc**: fix typo in webstreams.md (Christian Takle) [#47766](https://github.com/nodejs/node/pull/47766)
* \[[`ceba37a74f`](https://github.com/nodejs/node/commit/ceba37a74f)] - **doc**: move BethGriggs to regular member (Rich Trott) [#47776](https://github.com/nodejs/node/pull/47776)
* \[[`b954ea9781`](https://github.com/nodejs/node/commit/b954ea9781)] - **doc**: mark signing the binary is macOS and Windows only in SEA (Xuguang Mei) [#47722](https://github.com/nodejs/node/pull/47722)
* \[[`26bccbcd10`](https://github.com/nodejs/node/commit/26bccbcd10)] - **doc**: move addaleax to TSC emeriti (Anna Henningsen) [#47752](https://github.com/nodejs/node/pull/47752)
* \[[`20b0de242f`](https://github.com/nodejs/node/commit/20b0de242f)] - **doc**: add link to news for Node.js core (Michael Dawson) [#47704](https://github.com/nodejs/node/pull/47704)
* \[[`5709133dc7`](https://github.com/nodejs/node/commit/5709133dc7)] - **doc**: fix a typo in `permissions.md` (Daeyeon Jeong) [#47730](https://github.com/nodejs/node/pull/47730)
* \[[`c5c40a89f2`](https://github.com/nodejs/node/commit/c5c40a89f2)] - **doc**: async\_hooks asynchronous content example add mjs code (btea) [#47401](https://github.com/nodejs/node/pull/47401)
* \[[`a1403a8df2`](https://github.com/nodejs/node/commit/a1403a8df2)] - **doc**: clarify concurrency model of test runner (Tobias Nießen) [#47642](https://github.com/nodejs/node/pull/47642)
* \[[`c0c23fbe42`](https://github.com/nodejs/node/commit/c0c23fbe42)] - **doc**: fix a typo in `fs.openAsBlob` (Daeyeon Jeong) [#47693](https://github.com/nodejs/node/pull/47693)
* \[[`4cef98812d`](https://github.com/nodejs/node/commit/4cef98812d)] - **doc**: fix typos (Mohammed Keyvanzadeh) [#47685](https://github.com/nodejs/node/pull/47685)
* \[[`f30ef242ef`](https://github.com/nodejs/node/commit/f30ef242ef)] - **doc**: fix capitalization of ASan (Mohammed Keyvanzadeh) [#47676](https://github.com/nodejs/node/pull/47676)
* \[[`78a3503406`](https://github.com/nodejs/node/commit/78a3503406)] - **doc**: fix typos in SECURITY.md (Mohammed Keyvanzadeh) [#47677](https://github.com/nodejs/node/pull/47677)
* \[[`9101630e05`](https://github.com/nodejs/node/commit/9101630e05)] - **doc**: update error code of buffer (Deokjin Kim) [#47617](https://github.com/nodejs/node/pull/47617)
* \[[`183f0c3e79`](https://github.com/nodejs/node/commit/183f0c3e79)] - **doc**: change offset of example in `Buffer.copyBytesFrom` (Deokjin Kim) [#47606](https://github.com/nodejs/node/pull/47606)
* \[[`d11ff4bc53`](https://github.com/nodejs/node/commit/d11ff4bc53)] - **doc**: improve fs permissions description (Tobias Nießen) [#47596](https://github.com/nodejs/node/pull/47596)
* \[[`b58920c3a9`](https://github.com/nodejs/node/commit/b58920c3a9)] - **doc**: remove markdown link from heading (Tobias Nießen) [#47585](https://github.com/nodejs/node/pull/47585)
* \[[`c36634e880`](https://github.com/nodejs/node/commit/c36634e880)] - **doc**: fix history ordering of `WASI` constructor (Antoine du Hamel) [#47611](https://github.com/nodejs/node/pull/47611)
* \[[`d3fadd889d`](https://github.com/nodejs/node/commit/d3fadd889d)] - **doc**: fix release-post script location (Rafael Gonzaga) [#47517](https://github.com/nodejs/node/pull/47517)
* \[[`2a0bbe7883`](https://github.com/nodejs/node/commit/2a0bbe7883)] - **doc**: fix typo in webcrypto metadata (Tobias Nießen) [#47595](https://github.com/nodejs/node/pull/47595)
* \[[`b0b16ee9f6`](https://github.com/nodejs/node/commit/b0b16ee9f6)] - **doc**: add link for news from uvwasi team (Michael Dawson) [#47531](https://github.com/nodejs/node/pull/47531)
* \[[`7ca416af15`](https://github.com/nodejs/node/commit/7ca416af15)] - **doc**: add missing setEncoding call in ESM example (Anna Henningsen) [#47558](https://github.com/nodejs/node/pull/47558)
* \[[`f9abd59b41`](https://github.com/nodejs/node/commit/f9abd59b41)] - **doc**: update darwin-x64 toolchain used for Node.js 20 releases (Michaël Zasso) [#47546](https://github.com/nodejs/node/pull/47546)
* \[[`0dc508070f`](https://github.com/nodejs/node/commit/0dc508070f)] - **doc**: fix split infinitive in Hooks caveat (Jacob Smith) [#47550](https://github.com/nodejs/node/pull/47550)
* \[[`4046280475`](https://github.com/nodejs/node/commit/4046280475)] - **doc**: fix typo in util.types.isNativeError() (Julian Dax) [#47532](https://github.com/nodejs/node/pull/47532)
* \[[`9d30f469aa`](https://github.com/nodejs/node/commit/9d30f469aa)] - **doc**: add KhafraDev to collaborators (Matthew Aitken) [#47510](https://github.com/nodejs/node/pull/47510)
* \[[`537c17ec48`](https://github.com/nodejs/node/commit/537c17ec48)] - **doc**: create maintaining-brotli.md (Marco Ippolito) [#47380](https://github.com/nodejs/node/pull/47380)
* \[[`09ff9eafd9`](https://github.com/nodejs/node/commit/09ff9eafd9)] - **doc,fs**: update description of fs.stat() method (Mert Can Altın) [#47654](https://github.com/nodejs/node/pull/47654)
* \[[`185d6090cd`](https://github.com/nodejs/node/commit/185d6090cd)] - **doc,test**: fix concurrency option of test() (Tobias Nießen) [#47734](https://github.com/nodejs/node/pull/47734)
* \[[`a793cf401d`](https://github.com/nodejs/node/commit/a793cf401d)] - **esm**: rename `URLCanParse` to be consistent (Antoine du Hamel) [#47668](https://github.com/nodejs/node/pull/47668)
* \[[`fbb6b72f87`](https://github.com/nodejs/node/commit/fbb6b72f87)] - **esm**: remove support for deprecated hooks (Antoine du Hamel) [#47580](https://github.com/nodejs/node/pull/47580)
* \[[`c150976c4f`](https://github.com/nodejs/node/commit/c150976c4f)] - **esm**: initialize `import.meta` on eval (Antoine du Hamel) [#47551](https://github.com/nodejs/node/pull/47551)
* \[[`55f70f6395`](https://github.com/nodejs/node/commit/55f70f6395)] - **esm**: propagate `process.exit` from the loader thread to the main thread (Antoine du Hamel) [#47548](https://github.com/nodejs/node/pull/47548)
* \[[`269482f61f`](https://github.com/nodejs/node/commit/269482f61f)] - **esm**: avoid accessing lazy getters for urls (Yagiz Nizipli) [#47542](https://github.com/nodejs/node/pull/47542)
* \[[`889add68e5`](https://github.com/nodejs/node/commit/889add68e5)] - **esm**: avoid try/catch when validating urls (Yagiz Nizipli) [#47541](https://github.com/nodejs/node/pull/47541)
* \[[`439ea47a77`](https://github.com/nodejs/node/commit/439ea47a77)] - **(SEMVER-MINOR)** **fs**: add recursive option to readdir and opendir (Ethan Arrowood) [#41439](https://github.com/nodejs/node/pull/41439)
* \[[`a54e898dc8`](https://github.com/nodejs/node/commit/a54e898dc8)] - **(SEMVER-MINOR)** **fs**: add support for mode flag to specify the copy behavior (Tetsuharu Ohzeki) [#47084](https://github.com/nodejs/node/pull/47084)
* \[[`96f93cc500`](https://github.com/nodejs/node/commit/96f93cc500)] - **(SEMVER-MINOR)** **http**: remove internal error in assignSocket (Matteo Collina) [#47723](https://github.com/nodejs/node/pull/47723)
* \[[`4fa773964b`](https://github.com/nodejs/node/commit/4fa773964b)] - **(SEMVER-MINOR)** **http**: add highWaterMark opt in http.createServer (HinataKah0) [#47405](https://github.com/nodejs/node/pull/47405)
* \[[`94a5abb1e0`](https://github.com/nodejs/node/commit/94a5abb1e0)] - **inspector**: add tips for Session (theanarkh) [#47195](https://github.com/nodejs/node/pull/47195)
* \[[`21ff33127a`](https://github.com/nodejs/node/commit/21ff33127a)] - **lib**: improve esm resolve performance (Yagiz Nizipli) [#46652](https://github.com/nodejs/node/pull/46652)
* \[[`b8bdaf86c4`](https://github.com/nodejs/node/commit/b8bdaf86c4)] - **lib**: disallow file-backed blob cloning (James M Snell) [#47574](https://github.com/nodejs/node/pull/47574)
* \[[`e8bc03b372`](https://github.com/nodejs/node/commit/e8bc03b372)] - **lib**: use webidl DOMString converter in EventTarget (Matthew Aitken) [#47514](https://github.com/nodejs/node/pull/47514)
* \[[`91e4a7cdee`](https://github.com/nodejs/node/commit/91e4a7cdee)] - **loader**: use default loader as cascaded loader in the in loader worker (Joyee Cheung) [#47620](https://github.com/nodejs/node/pull/47620)
* \[[`d5089fe00a`](https://github.com/nodejs/node/commit/d5089fe00a)] - **meta**: fix dependabot commit message (Mestery) [#47810](https://github.com/nodejs/node/pull/47810)
* \[[`92794400ce`](https://github.com/nodejs/node/commit/92794400ce)] - **meta**: ping nodejs/startup for startup test changes (Joyee Cheung) [#47771](https://github.com/nodejs/node/pull/47771)
* \[[`8d43689077`](https://github.com/nodejs/node/commit/8d43689077)] - **meta**: add mailmap entry for KhafraDev (Rich Trott) [#47512](https://github.com/nodejs/node/pull/47512)
* \[[`4d02901935`](https://github.com/nodejs/node/commit/4d02901935)] - **node-api**: test passing NULL to napi\_define\_class (Gabriel Schulhof) [#47567](https://github.com/nodejs/node/pull/47567)
* \[[`568256dca0`](https://github.com/nodejs/node/commit/568256dca0)] - **node-api**: test passing NULL to number APIs (Gabriel Schulhof) [#47549](https://github.com/nodejs/node/pull/47549)
* \[[`12f0fa386d`](https://github.com/nodejs/node/commit/12f0fa386d)] - **node-api**: remove unused mark\_arraybuffer\_as\_untransferable (Chengzhong Wu) [#47557](https://github.com/nodejs/node/pull/47557)
* \[[`e8ea83416a`](https://github.com/nodejs/node/commit/e8ea83416a)] - **quic**: add more QUIC implementation (James M Snell) [#47494](https://github.com/nodejs/node/pull/47494)
* \[[`af227b159d`](https://github.com/nodejs/node/commit/af227b159d)] - **readline**: fix issue with newline-less last line (Ian Harris) [#47317](https://github.com/nodejs/node/pull/47317)
* \[[`e948bec969`](https://github.com/nodejs/node/commit/e948bec969)] - **src**: avoid copying string in fs\_permission (Yagiz Nizipli) [#47746](https://github.com/nodejs/node/pull/47746)
* \[[`dc43ce7706`](https://github.com/nodejs/node/commit/dc43ce7706)] - **src**: replace idna functions with ada::idna (Yagiz Nizipli) [#47735](https://github.com/nodejs/node/pull/47735)
* \[[`1f9e7ce7e8`](https://github.com/nodejs/node/commit/1f9e7ce7e8)] - **src**: fix typo in comment in quic/sessionticket.cc (Tobias Nießen) [#47754](https://github.com/nodejs/node/pull/47754)
* \[[`2acb57b777`](https://github.com/nodejs/node/commit/2acb57b777)] - **src**: mark fatal error functions as noreturn (Chengzhong Wu) [#47695](https://github.com/nodejs/node/pull/47695)
* \[[`4431df7481`](https://github.com/nodejs/node/commit/4431df7481)] - **src**: split BlobSerializer/BlobDeserializer (Joyee Cheung) [#47458](https://github.com/nodejs/node/pull/47458)
* \[[`bf9a52cb3d`](https://github.com/nodejs/node/commit/bf9a52cb3d)] - **src**: prevent changing FunctionTemplateInfo after publish (Shelley Vohr) [#46979](https://github.com/nodejs/node/pull/46979)
* \[[`872e6706ca`](https://github.com/nodejs/node/commit/872e6706ca)] - **src**: add v8 fast api for url canParse (Matthew Aitken) [#47552](https://github.com/nodejs/node/pull/47552)
* \[[`cfafe431f2`](https://github.com/nodejs/node/commit/cfafe431f2)] - **src**: make AliasedBuffers in the binding data weak (Joyee Cheung) [#47354](https://github.com/nodejs/node/pull/47354)
* \[[`cf48db0034`](https://github.com/nodejs/node/commit/cf48db0034)] - **src**: use v8::Boolean(b) over b ? True() : False() (Tobias Nießen) [#47554](https://github.com/nodejs/node/pull/47554)
* \[[`ba255eda37`](https://github.com/nodejs/node/commit/ba255eda37)] - **src**: fix typo in process.env accessor error message (Moritz Raho) [#47014](https://github.com/nodejs/node/pull/47014)
* \[[`daf0c78232`](https://github.com/nodejs/node/commit/daf0c78232)] - **src**: replace static const string\_view by static constexpr (Daniel Lemire) [#47524](https://github.com/nodejs/node/pull/47524)
* \[[`57e7ed7f47`](https://github.com/nodejs/node/commit/57e7ed7f47)] - **src**: fix CSPRNG when length exceeds INT\_MAX (Tobias Nießen) [#47515](https://github.com/nodejs/node/pull/47515)
* \[[`cda36bfd8f`](https://github.com/nodejs/node/commit/cda36bfd8f)] - **src**: use correct variable in node\_builtins.cc (Michaël Zasso) [#47343](https://github.com/nodejs/node/pull/47343)
* \[[`adc1601ccd`](https://github.com/nodejs/node/commit/adc1601ccd)] - **src**: slim down stream\_base-inl.h (lilsweetcaligula) [#46972](https://github.com/nodejs/node/pull/46972)
* \[[`f88132f1b8`](https://github.com/nodejs/node/commit/f88132f1b8)] - **stream**: prevent pipeline hang with generator functions (Debadree Chatterjee) [#47712](https://github.com/nodejs/node/pull/47712)
* \[[`2b411f4b42`](https://github.com/nodejs/node/commit/2b411f4b42)] - **(SEMVER-MINOR)** **stream**: preserve object mode in compose (Raz Luvaton) [#47413](https://github.com/nodejs/node/pull/47413)
* \[[`159cf02920`](https://github.com/nodejs/node/commit/159cf02920)] - **test**: refactor to use `getEventListeners` in timers (Deokjin Kim) [#47759](https://github.com/nodejs/node/pull/47759)
* \[[`97a3d39b8f`](https://github.com/nodejs/node/commit/97a3d39b8f)] - **test**: add and use tmpdir.hasEnoughSpace() (Tobias Nießen) [#47767](https://github.com/nodejs/node/pull/47767)
* \[[`5bb7b26bb5`](https://github.com/nodejs/node/commit/5bb7b26bb5)] - **test**: remove spaces from test runner test names (Tobias Nießen) [#47733](https://github.com/nodejs/node/pull/47733)
* \[[`84fa9fd725`](https://github.com/nodejs/node/commit/84fa9fd725)] - **test**: refactor WPTRunner and enable parallel WPT execution (Filip Skokan) [#47635](https://github.com/nodejs/node/pull/47635)
* \[[`9d3768eb01`](https://github.com/nodejs/node/commit/9d3768eb01)] - _**Revert**_ "**test**: run WPT files in parallel again" (Filip Skokan) [#47627](https://github.com/nodejs/node/pull/47627)
* \[[`826f4041d1`](https://github.com/nodejs/node/commit/826f4041d1)] - **test**: mark test-cluster-primary-error flaky on asan (Yagiz Nizipli) [#47422](https://github.com/nodejs/node/pull/47422)
* \[[`e5251e31eb`](https://github.com/nodejs/node/commit/e5251e31eb)] - **test\_runner**: fix --require with --experimental-loader (Moshe Atlow) [#47751](https://github.com/nodejs/node/pull/47751)
* \[[`6ee5e42c73`](https://github.com/nodejs/node/commit/6ee5e42c73)] - **(SEMVER-MINOR)** **test\_runner**: support combining coverage reports (Colin Ihrig) [#47686](https://github.com/nodejs/node/pull/47686)
* \[[`f8581e7629`](https://github.com/nodejs/node/commit/f8581e7629)] - **test\_runner**: remove no-op validation (Colin Ihrig) [#47687](https://github.com/nodejs/node/pull/47687)
* \[[`40b38797c5`](https://github.com/nodejs/node/commit/40b38797c5)] - **test\_runner**: fix test runner concurrency (Moshe Atlow) [#47675](https://github.com/nodejs/node/pull/47675)
* \[[`2d7cac0c5b`](https://github.com/nodejs/node/commit/2d7cac0c5b)] - **test\_runner**: fix test counting (Moshe Atlow) [#47675](https://github.com/nodejs/node/pull/47675)
* \[[`5a9b71a52e`](https://github.com/nodejs/node/commit/5a9b71a52e)] - **test\_runner**: fix nested hooks (Moshe Atlow) [#47648](https://github.com/nodejs/node/pull/47648)
* \[[`5327483f31`](https://github.com/nodejs/node/commit/5327483f31)] - **(SEMVER-MINOR)** **test\_runner**: add testNamePatterns to run api (Chemi Atlow) [#47628](https://github.com/nodejs/node/pull/47628)
* \[[`b6fb7914ca`](https://github.com/nodejs/node/commit/b6fb7914ca)] - **test\_runner**: support coverage of unnamed functions (Colin Ihrig) [#47652](https://github.com/nodejs/node/pull/47652)
* \[[`1f120a396f`](https://github.com/nodejs/node/commit/1f120a396f)] - **test\_runner**: move coverage collection to root.postRun() (Colin Ihrig) [#47651](https://github.com/nodejs/node/pull/47651)
* \[[`bdd02a467d`](https://github.com/nodejs/node/commit/bdd02a467d)] - **(SEMVER-MINOR)** **test\_runner**: execute before hook on test (Chemi Atlow) [#47586](https://github.com/nodejs/node/pull/47586)
* \[[`ec24abaa03`](https://github.com/nodejs/node/commit/ec24abaa03)] - **test\_runner**: avoid reporting parents of failing tests in summary (Moshe Atlow) [#47579](https://github.com/nodejs/node/pull/47579)
* \[[`4203057740`](https://github.com/nodejs/node/commit/4203057740)] - **test\_runner**: fix spec skip detection (Moshe Atlow) [#47537](https://github.com/nodejs/node/pull/47537)
* \[[`57c69987ba`](https://github.com/nodejs/node/commit/57c69987ba)] - **tls**: accept SecureContext object in server.addContext() (HinataKah0) [#47570](https://github.com/nodejs/node/pull/47570)
* \[[`c620eb80a0`](https://github.com/nodejs/node/commit/c620eb80a0)] - **tools**: update doc to highlight.js\@11.8.0 (Node.js GitHub Bot) [#47786](https://github.com/nodejs/node/pull/47786)
* \[[`326c3f1593`](https://github.com/nodejs/node/commit/326c3f1593)] - **tools**: add the missing LoongArch64 definition in the v8.gyp file (Sun Haiyong) [#47641](https://github.com/nodejs/node/pull/47641)
* \[[`8d1588acdc`](https://github.com/nodejs/node/commit/8d1588acdc)] - **tools**: update lint-md-dependencies to rollup\@3.21.1 (Node.js GitHub Bot) [#47787](https://github.com/nodejs/node/pull/47787)
* \[[`226e5b83ee`](https://github.com/nodejs/node/commit/226e5b83ee)] - **tools**: move update-npm to dep updaters (Marco Ippolito) [#47619](https://github.com/nodejs/node/pull/47619)
* \[[`9d0bef6c0a`](https://github.com/nodejs/node/commit/9d0bef6c0a)] - **tools**: fix update-v8-patch cache (Marco Ippolito) [#47725](https://github.com/nodejs/node/pull/47725)
* \[[`63e8c95a66`](https://github.com/nodejs/node/commit/63e8c95a66)] - **tools**: automate v8 patch update (Marco Ippolito) [#47594](https://github.com/nodejs/node/pull/47594)
* \[[`d2994e52d3`](https://github.com/nodejs/node/commit/d2994e52d3)] - **tools**: fix skip message in update-cjs-module-lexer (Tobias Nießen) [#47701](https://github.com/nodejs/node/pull/47701)
* \[[`ccf9c37b43`](https://github.com/nodejs/node/commit/ccf9c37b43)] - **tools**: update lint-md-dependencies to @rollup/plugin-commonjs\@24.1.0 (Node.js GitHub Bot) [#47577](https://github.com/nodejs/node/pull/47577)
* \[[`0887fa0464`](https://github.com/nodejs/node/commit/0887fa0464)] - **tools**: keep PR titles/description up-to-date (Tobias Nießen) [#47621](https://github.com/nodejs/node/pull/47621)
* \[[`b8927ddf16`](https://github.com/nodejs/node/commit/b8927ddf16)] - **tools**: fix updating root certificates (Richard Lau) [#47607](https://github.com/nodejs/node/pull/47607)
* \[[`87cae0cb59`](https://github.com/nodejs/node/commit/87cae0cb59)] - **tools**: update PR label config (Mohammed Keyvanzadeh) [#47593](https://github.com/nodejs/node/pull/47593)
* \[[`c17f2688b8`](https://github.com/nodejs/node/commit/c17f2688b8)] - _**Revert**_ "**tools**: ensure failed daily wpt run still generates a report" (Filip Skokan) [#47627](https://github.com/nodejs/node/pull/47627)
* \[[`fbe7d73234`](https://github.com/nodejs/node/commit/fbe7d73234)] - **tools**: add execution permission to uvwasi script (Mert Can Altın) [#47600](https://github.com/nodejs/node/pull/47600)
* \[[`e3f4ff439e`](https://github.com/nodejs/node/commit/e3f4ff439e)] - **tools**: add update script for googletest (Tobias Nießen) [#47482](https://github.com/nodejs/node/pull/47482)
* \[[`7c552e650a`](https://github.com/nodejs/node/commit/7c552e650a)] - **tools**: add option to run workflow with specific tool id (Michaël Zasso) [#47591](https://github.com/nodejs/node/pull/47591)
* \[[`1509312170`](https://github.com/nodejs/node/commit/1509312170)] - **tools**: automate zlib update (Marco Ippolito) [#47417](https://github.com/nodejs/node/pull/47417)
* \[[`6af7f1ee03`](https://github.com/nodejs/node/commit/6af7f1ee03)] - **tools**: add url and whatwg-url labels automatically (Yagiz Nizipli) [#47545](https://github.com/nodejs/node/pull/47545)
* \[[`ff73c05d54`](https://github.com/nodejs/node/commit/ff73c05d54)] - **tools**: add performance label to benchmark changes (Yagiz Nizipli) [#47545](https://github.com/nodejs/node/pull/47545)
* \[[`9e3e0b0a84`](https://github.com/nodejs/node/commit/9e3e0b0a84)] - **tools**: automate uvwasi dependency update (Ranieri Innocenti Spada) [#47509](https://github.com/nodejs/node/pull/47509)
* \[[`233b628f22`](https://github.com/nodejs/node/commit/233b628f22)] - **tools**: add missing pinned dependencies (Mateo Nunez) [#47346](https://github.com/nodejs/node/pull/47346)
* \[[`e4d95859f5`](https://github.com/nodejs/node/commit/e4d95859f5)] - **tools**: automate ngtcp2 and nghttp3 update (Marco Ippolito) [#47402](https://github.com/nodejs/node/pull/47402)
* \[[`2e8338126b`](https://github.com/nodejs/node/commit/2e8338126b)] - **tools**: move update-undici.sh to dep\_updaters and create maintain md (Marco Ippolito) [#47380](https://github.com/nodejs/node/pull/47380)
* \[[`8712eafc87`](https://github.com/nodejs/node/commit/8712eafc87)] - **typings**: fix syntax error in tsconfig (Mohammed Keyvanzadeh) [#47584](https://github.com/nodejs/node/pull/47584)
* \[[`e4b6b79f18`](https://github.com/nodejs/node/commit/e4b6b79f18)] - **url**: reduce revokeObjectURL cpp calls (Yagiz Nizipli) [#47728](https://github.com/nodejs/node/pull/47728)
* \[[`9aae76727f`](https://github.com/nodejs/node/commit/9aae76727f)] - **url**: handle URL.canParse without base parameter (Yagiz Nizipli) [#47547](https://github.com/nodejs/node/pull/47547)
* \[[`180d365439`](https://github.com/nodejs/node/commit/180d365439)] - **url**: validate URL constructor arg length (Matthew Aitken) [#47513](https://github.com/nodejs/node/pull/47513)
* \[[`4839fc4369`](https://github.com/nodejs/node/commit/4839fc4369)] - **url**: validate argument length in canParse (Matthew Aitken) [#47513](https://github.com/nodejs/node/pull/47513)
* \[[`606523d37e`](https://github.com/nodejs/node/commit/606523d37e)] - **v8**: fix ERR\_NOT\_BUILDING\_SNAPSHOT is not a constructor (Chengzhong Wu) [#47721](https://github.com/nodejs/node/pull/47721)
* \[[`75c1d1b66e`](https://github.com/nodejs/node/commit/75c1d1b66e)] - **(SEMVER-MINOR)** **wasi**: make returnOnExit true by default (Michael Dawson) [#47390](https://github.com/nodejs/node/pull/47390)

<a id="20.0.0"></a>

## 2023-04-18, Version 20.0.0 (Current), @RafaelGSS

We're excited to announce the release of Node.js 20! Highlights include the new Node.js Permission Model,
a synchronous `import.meta.resolve`, a stable test\_runner, updates of the V8 JavaScript engine to 11.3, Ada to 2.0,
and more!

As a reminder, Node.js 20 will enter long-term support (LTS) in October, but until then, it will be the "Current" release for the next six months.
We encourage you to explore the new features and benefits offered by this latest release and evaluate their potential impact on your applications.

### Notable Changes

#### Permission Model

Node.js now has an experimental feature called the Permission Model.
It allows developers to restrict access to specific resources during program execution, such as file system operations,
child process spawning, and worker thread creation.
The API exists behind a flag `--experimental-permission` which when enabled will restrict access to all available permissions.
By using this feature, developers can prevent their applications from accessing or modifying sensitive data or running potentially harmful code.
More information about the Permission Model can be found in the [Node.js documentation](https://nodejs.org/api/permissions.html#process-based-permissions).

The Permission Model was a contribution by Rafael Gonzaga in [#44004](https://github.com/nodejs/node/pull/44004).

#### Custom ESM loader hooks run on dedicated thread

ESM hooks supplied via loaders (`--experimental-loader=foo.mjs`) now run in a dedicated thread, isolated from the main thread.
This provides a separate scope for loaders and ensures no cross-contamination between loaders and application code.

**Synchronous `import.meta.resolve()`**

In alignment with browser behavior, this function now returns synchronously.
Despite this, user loader `resolve` hooks can still be defined as async functions (or as sync functions, if the author prefers).
Even when there are async `resolve` hooks loaded, `import.meta.resolve` will still return synchronously for application code.

Contributed by Anna Henningsen, Antoine du Hamel, Geoffrey Booth, Guy Bedford, Jacob Smith, and Michaël Zasso in [#44710](https://github.com/nodejs/node/pull/44710)

#### V8 11.3

The V8 engine is updated to version 11.3, which is part of Chromium 113.
This version includes three new features to the JavaScript API:

* [String.prototype.isWellFormed and toWellFormed](https://chromestatus.com/feature/5200195346759680)
* [Methods that change Array and TypedArray by copy](https://chromestatus.com/feature/5068609911521280)
* [Resizable ArrayBuffer and growable SharedArrayBuffer](https://chromestatus.com/feature/4668361878274048)
* [RegExp v flag with set notation + properties of strings](https://chromestatus.com/feature/5144156542861312)
* [WebAssembly Tail Call](https://chromestatus.com/feature/5423405012615168)

The V8 update was a contribution by Michaël Zasso in [#47251](https://github.com/nodejs/node/pull/47251).

#### Stable Test Runner

The recent update to Node.js, version 20, includes an important change to the test\_runner module. The module has been marked as stable after a recent update.
Previously, the test\_runner module was experimental, but this change marks it as a stable module that is ready for production use.

Contributed by Colin Ihrig in [#46983](https://github.com/nodejs/node/pull/46983)

#### Ada 2.0

Node.js v20 comes with the latest version of the URL parser, Ada. This update brings significant performance improvements
to URL parsing, including enhancements to the `url.domainToASCII` and `url.domainToUnicode` functions in `node:url`.

Ada 2.0 has been integrated into the Node.js codebase, ensuring that all parts of the application can benefit from the
improved performance. Additionally, Ada 2.0 features a significant performance boost over its predecessor, Ada 1.0.4,
while also eliminating the need for the ICU requirement for URL hostname parsing.

Contributed by Yagiz Nizipli and Daniel Lemire in [#47339](https://github.com/nodejs/node/pull/47339)

#### Preparing single executable apps now requires injecting a Blob

Building a single executable app now requires injecting a blob prepared by
Node.js from a JSON config instead of injecting the raw JS file.
This opens up the possibility of embedding multiple co-existing resources into the SEA (Single Executable Apps).

Contributed by Joyee Cheung in [#47125](https://github.com/nodejs/node/pull/47125)

#### Web Crypto API

Web Crypto API functions' arguments are now coerced and validated as per their WebIDL definitions like in other Web Crypto API implementations.
This further improves interoperability with other implementations of Web Crypto API.

This change was made by Filip Skokan in [#46067](https://github.com/nodejs/node/pull/46067).

#### Official support for ARM64 Windows

Node.js now includes binaries for ARM64 Windows, allowing for native execution on the platform.
The MSI, zip/7z packages, and executable are available from the Node.js download site along with all other platforms.
The CI system was updated and all changes are now fully tested on ARM64 Windows, to prevent regressions and ensure compatibility.

ARM64 Windows was upgraded to tier 2 support by Stefan Stojanovic in [#47233](https://github.com/nodejs/node/pull/47233).

#### WASI version must now be specified

When `new WASI()` is called, the version option is now required and has no default value.
Any code that relied on the default for the version will need to be updated to request a specific version.

This change was made by Michael Dawson in [#47391](https://github.com/nodejs/node/pull/47391).

#### Deprecations and Removals

* \[[`3bed5f11e0`](https://github.com/nodejs/node/commit/3bed5f11e0)] - **(SEMVER-MAJOR)** **url**: runtime-deprecate url.parse() with invalid ports (Rich Trott) [#45526](https://github.com/nodejs/node/pull/45526)

`url.parse()` accepts URLs with ports that are not numbers. This behavior might result in host name spoofing with unexpected input.
These URLs will throw an error in future versions of Node.js, as the WHATWG URL API does already.
Starting with Node.js 20, these URLS cause `url.parse()` to emit a warning.

### Semver-Major Commits

* \[[`9fafb0a090`](https://github.com/nodejs/node/commit/9fafb0a090)] - **(SEMVER-MAJOR)** **async\_hooks**: deprecate the AsyncResource.bind asyncResource property (James M Snell) [#46432](https://github.com/nodejs/node/pull/46432)
* \[[`1948d37595`](https://github.com/nodejs/node/commit/1948d37595)] - **(SEMVER-MAJOR)** **buffer**: check INSPECT\_MAX\_BYTES with validateNumber (Umuoy) [#46599](https://github.com/nodejs/node/pull/46599)
* \[[`7bc0e6a4e7`](https://github.com/nodejs/node/commit/7bc0e6a4e7)] - **(SEMVER-MAJOR)** **buffer**: graduate File from experimental and expose as global (Khafra) [#47153](https://github.com/nodejs/node/pull/47153)
* \[[`671ffd7825`](https://github.com/nodejs/node/commit/671ffd7825)] - **(SEMVER-MAJOR)** **buffer**: use min/max of `validateNumber` (Deokjin Kim) [#45796](https://github.com/nodejs/node/pull/45796)
* \[[`ab1614d280`](https://github.com/nodejs/node/commit/ab1614d280)] - **(SEMVER-MAJOR)** **build**: reset embedder string to "-node.0" (Michaël Zasso) [#47251](https://github.com/nodejs/node/pull/47251)
* \[[`c1bcdbcf79`](https://github.com/nodejs/node/commit/c1bcdbcf79)] - **(SEMVER-MAJOR)** **build**: warn for gcc versions earlier than 10.1 (Richard Lau) [#46806](https://github.com/nodejs/node/pull/46806)
* \[[`649f68fc1e`](https://github.com/nodejs/node/commit/649f68fc1e)] - **(SEMVER-MAJOR)** **build**: reset embedder string to "-node.0" (Yagiz Nizipli) [#45579](https://github.com/nodejs/node/pull/45579)
* \[[`9374700d7a`](https://github.com/nodejs/node/commit/9374700d7a)] - **(SEMVER-MAJOR)** **crypto**: remove DEFAULT\_ENCODING (Tobias Nießen) [#47182](https://github.com/nodejs/node/pull/47182)
* \[[`1640aeb680`](https://github.com/nodejs/node/commit/1640aeb680)] - **(SEMVER-MAJOR)** **crypto**: remove obsolete SSL\_OP\_\* constants (Tobias Nießen) [#47073](https://github.com/nodejs/node/pull/47073)
* \[[`c2e4b1fa9a`](https://github.com/nodejs/node/commit/c2e4b1fa9a)] - **(SEMVER-MAJOR)** **crypto**: remove ALPN\_ENABLED (Tobias Nießen) [#47028](https://github.com/nodejs/node/pull/47028)
* \[[`3ef38c4bd7`](https://github.com/nodejs/node/commit/3ef38c4bd7)] - **(SEMVER-MAJOR)** **crypto**: use WebIDL converters in WebCryptoAPI (Filip Skokan) [#46067](https://github.com/nodejs/node/pull/46067)
* \[[`08af023b1f`](https://github.com/nodejs/node/commit/08af023b1f)] - **(SEMVER-MAJOR)** **crypto**: runtime deprecate replaced rsa-pss keygen parameters (Filip Skokan) [#45653](https://github.com/nodejs/node/pull/45653)
* \[[`7eb0ac3cb6`](https://github.com/nodejs/node/commit/7eb0ac3cb6)] - **(SEMVER-MAJOR)** **deps**: patch V8 to support compilation on win-arm64 (Michaël Zasso) [#47251](https://github.com/nodejs/node/pull/47251)
* \[[`a7c129f286`](https://github.com/nodejs/node/commit/a7c129f286)] - **(SEMVER-MAJOR)** **deps**: silence irrelevant V8 warning (Michaël Zasso) [#47251](https://github.com/nodejs/node/pull/47251)
* \[[`6f5655a18e`](https://github.com/nodejs/node/commit/6f5655a18e)] - **(SEMVER-MAJOR)** **deps**: always define V8\_EXPORT\_PRIVATE as no-op (Michaël Zasso) [#47251](https://github.com/nodejs/node/pull/47251)
* \[[`f226350fcb`](https://github.com/nodejs/node/commit/f226350fcb)] - **(SEMVER-MAJOR)** **deps**: update V8 to 11.3.244.4 (Michaël Zasso) [#47251](https://github.com/nodejs/node/pull/47251)
* \[[`d6dae7420e`](https://github.com/nodejs/node/commit/d6dae7420e)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick f1c888e7093e (Michaël Zasso) [#45579](https://github.com/nodejs/node/pull/45579)
* \[[`56c436533e`](https://github.com/nodejs/node/commit/56c436533e)] - **(SEMVER-MAJOR)** **deps**: fix V8 build on Windows with MSVC (Michaël Zasso) [#45579](https://github.com/nodejs/node/pull/45579)
* \[[`51ab98c71b`](https://github.com/nodejs/node/commit/51ab98c71b)] - **(SEMVER-MAJOR)** **deps**: silence irrelevant V8 warning (Michaël Zasso) [#45579](https://github.com/nodejs/node/pull/45579)
* \[[`9f84d3eea8`](https://github.com/nodejs/node/commit/9f84d3eea8)] - **(SEMVER-MAJOR)** **deps**: V8: fix v8-cppgc.h for MSVC (Jiawen Geng) [#45579](https://github.com/nodejs/node/pull/45579)
* \[[`f2318cd4b5`](https://github.com/nodejs/node/commit/f2318cd4b5)] - **(SEMVER-MAJOR)** **deps**: fix V8 build issue with inline methods (Jiawen Geng) [#45579](https://github.com/nodejs/node/pull/45579)
* \[[`16e03e7968`](https://github.com/nodejs/node/commit/16e03e7968)] - **(SEMVER-MAJOR)** **deps**: update V8 to 10.9.194.4 (Yagiz Nizipli) [#45579](https://github.com/nodejs/node/pull/45579)
* \[[`6473f5e7f7`](https://github.com/nodejs/node/commit/6473f5e7f7)] - **(SEMVER-MAJOR)** **doc**: update toolchains used for Node.js 20 releases (Richard Lau) [#47352](https://github.com/nodejs/node/pull/47352)
* \[[`cc18fd9608`](https://github.com/nodejs/node/commit/cc18fd9608)] - **(SEMVER-MAJOR)** **events**: refactor to use `validateNumber` (Deokjin Kim) [#45770](https://github.com/nodejs/node/pull/45770)
* \[[`ff92b40ffc`](https://github.com/nodejs/node/commit/ff92b40ffc)] - **(SEMVER-MAJOR)** **http**: close the connection after sending a body without declared length (Tim Perry) [#46333](https://github.com/nodejs/node/pull/46333)
* \[[`2a29df6464`](https://github.com/nodejs/node/commit/2a29df6464)] - **(SEMVER-MAJOR)** **http**: keep HTTP/1.1 conns alive even if the Connection header is removed (Tim Perry) [#46331](https://github.com/nodejs/node/pull/46331)
* \[[`391dc74a10`](https://github.com/nodejs/node/commit/391dc74a10)] - **(SEMVER-MAJOR)** **http**: throw error if options of http.Server is array (Deokjin Kim) [#46283](https://github.com/nodejs/node/pull/46283)
* \[[`ed3604cd64`](https://github.com/nodejs/node/commit/ed3604cd64)] - **(SEMVER-MAJOR)** **http**: server check Host header, to meet RFC 7230 5.4 requirement (wwwzbwcom) [#45597](https://github.com/nodejs/node/pull/45597)
* \[[`88d71dc301`](https://github.com/nodejs/node/commit/88d71dc301)] - **(SEMVER-MAJOR)** **lib**: refactor to use min/max of `validateNumber` (Deokjin Kim) [#45772](https://github.com/nodejs/node/pull/45772)
* \[[`e4d641f02a`](https://github.com/nodejs/node/commit/e4d641f02a)] - **(SEMVER-MAJOR)** **lib**: refactor to use validators in http2 (Debadree Chatterjee) [#46174](https://github.com/nodejs/node/pull/46174)
* \[[`0f3e531096`](https://github.com/nodejs/node/commit/0f3e531096)] - **(SEMVER-MAJOR)** **lib**: performance improvement on readline async iterator (Thiago Oliveira Santos) [#41276](https://github.com/nodejs/node/pull/41276)
* \[[`5b5898ac86`](https://github.com/nodejs/node/commit/5b5898ac86)] - **(SEMVER-MAJOR)** **lib,src**: update exit codes as per todos (Debadree Chatterjee) [#45841](https://github.com/nodejs/node/pull/45841)
* \[[`55321bafd1`](https://github.com/nodejs/node/commit/55321bafd1)] - **(SEMVER-MAJOR)** **net**: enable autoSelectFamily by default (Paolo Insogna) [#46790](https://github.com/nodejs/node/pull/46790)
* \[[`2d0d99733b`](https://github.com/nodejs/node/commit/2d0d99733b)] - **(SEMVER-MAJOR)** **process**: remove `process.exit()`, `process.exitCode` coercion to integer (Daeyeon Jeong) [#43716](https://github.com/nodejs/node/pull/43716)
* \[[`dc06df31b6`](https://github.com/nodejs/node/commit/dc06df31b6)] - **(SEMVER-MAJOR)** **readline**: refactor to use `validateNumber` (Deokjin Kim) [#45801](https://github.com/nodejs/node/pull/45801)
* \[[`295b2f3ff4`](https://github.com/nodejs/node/commit/295b2f3ff4)] - **(SEMVER-MAJOR)** **src**: update NODE\_MODULE\_VERSION to 115 (Michaël Zasso) [#47251](https://github.com/nodejs/node/pull/47251)
* \[[`3803b028dd`](https://github.com/nodejs/node/commit/3803b028dd)] - **(SEMVER-MAJOR)** **src**: share common code paths for SEA and embedder script (Anna Henningsen) [#46825](https://github.com/nodejs/node/pull/46825)
* \[[`e8bddac3e9`](https://github.com/nodejs/node/commit/e8bddac3e9)] - **(SEMVER-MAJOR)** **src**: apply ABI-breaking API simplifications (Anna Henningsen) [#46705](https://github.com/nodejs/node/pull/46705)
* \[[`f84de0ad4c`](https://github.com/nodejs/node/commit/f84de0ad4c)] - **(SEMVER-MAJOR)** **src**: use uint32\_t for process initialization flags enum (Anna Henningsen) [#46427](https://github.com/nodejs/node/pull/46427)
* \[[`a6242772ec`](https://github.com/nodejs/node/commit/a6242772ec)] - **(SEMVER-MAJOR)** **src**: fix ArrayBuffer::Detach deprecation (Michaël Zasso) [#45579](https://github.com/nodejs/node/pull/45579)
* \[[`dd5c39a808`](https://github.com/nodejs/node/commit/dd5c39a808)] - **(SEMVER-MAJOR)** **src**: update NODE\_MODULE\_VERSION to 112 (Yagiz Nizipli) [#45579](https://github.com/nodejs/node/pull/45579)
* \[[`63eca7fec0`](https://github.com/nodejs/node/commit/63eca7fec0)] - **(SEMVER-MAJOR)** **stream**: validate readable defaultEncoding (Marco Ippolito) [#46430](https://github.com/nodejs/node/pull/46430)
* \[[`9e7093f416`](https://github.com/nodejs/node/commit/9e7093f416)] - **(SEMVER-MAJOR)** **stream**: validate writable defaultEncoding (Marco Ippolito) [#46322](https://github.com/nodejs/node/pull/46322)
* \[[`fb91ee4f26`](https://github.com/nodejs/node/commit/fb91ee4f26)] - **(SEMVER-MAJOR)** **test**: make trace-gc-flag tests less strict (Yagiz Nizipli) [#45579](https://github.com/nodejs/node/pull/45579)
* \[[`eca618071e`](https://github.com/nodejs/node/commit/eca618071e)] - **(SEMVER-MAJOR)** **test**: adapt test-v8-stats for V8 update (Michaël Zasso) [#45579](https://github.com/nodejs/node/pull/45579)
* \[[`c03354d3e0`](https://github.com/nodejs/node/commit/c03354d3e0)] - **(SEMVER-MAJOR)** **test**: test case for multiple res.writeHead and res.getHeader (Marco Ippolito) [#45508](https://github.com/nodejs/node/pull/45508)
* \[[`c733cc0c7f`](https://github.com/nodejs/node/commit/c733cc0c7f)] - **(SEMVER-MAJOR)** **test\_runner**: mark module as stable (Colin Ihrig) [#46983](https://github.com/nodejs/node/pull/46983)
* \[[`7ce223273d`](https://github.com/nodejs/node/commit/7ce223273d)] - **(SEMVER-MAJOR)** **tools**: update V8 gypfiles for 11.1 (Michaël Zasso) [#47251](https://github.com/nodejs/node/pull/47251)
* \[[`ca4bd3023e`](https://github.com/nodejs/node/commit/ca4bd3023e)] - **(SEMVER-MAJOR)** **tools**: update V8 gypfiles for 11.0 (Michaël Zasso) [#47251](https://github.com/nodejs/node/pull/47251)
* \[[`58b06a269a`](https://github.com/nodejs/node/commit/58b06a269a)] - **(SEMVER-MAJOR)** **tools**: update V8 gypfiles (Michaël Zasso) [#45579](https://github.com/nodejs/node/pull/45579)
* \[[`027841c964`](https://github.com/nodejs/node/commit/027841c964)] - **(SEMVER-MAJOR)** **url**: use private properties for brand check (Yagiz Nizipli) [#46904](https://github.com/nodejs/node/pull/46904)
* \[[`3bed5f11e0`](https://github.com/nodejs/node/commit/3bed5f11e0)] - **(SEMVER-MAJOR)** **url**: runtime-deprecate url.parse() with invalid ports (Rich Trott) [#45526](https://github.com/nodejs/node/pull/45526)
* \[[`7c76fddf25`](https://github.com/nodejs/node/commit/7c76fddf25)] - **(SEMVER-MAJOR)** **util,doc**: mark parseArgs() as stable (Colin Ihrig) [#46718](https://github.com/nodejs/node/pull/46718)
* \[[`4b52727976`](https://github.com/nodejs/node/commit/4b52727976)] - **(SEMVER-MAJOR)** **wasi**: make version non-optional (Michael Dawson) [#47391](https://github.com/nodejs/node/pull/47391)

### Semver-Minor Commits

* \[[`d4b440bfac`](https://github.com/nodejs/node/commit/d4b440bfac)] - **(SEMVER-MINOR)** **fs**: implement byob mode for readableWebStream() (Debadree Chatterjee) [#46933](https://github.com/nodejs/node/pull/46933)
* \[[`00c222593e`](https://github.com/nodejs/node/commit/00c222593e)] - **(SEMVER-MINOR)** **src,process**: add permission model (Rafael Gonzaga) [#44004](https://github.com/nodejs/node/pull/44004)
* \[[`978b57d750`](https://github.com/nodejs/node/commit/978b57d750)] - **(SEMVER-MINOR)** **wasi**: no longer require flag to enable wasi (Michael Dawson) [#47286](https://github.com/nodejs/node/pull/47286)

### Semver-Patch Commits

* \[[`e50c6b9a22`](https://github.com/nodejs/node/commit/e50c6b9a22)] - **bootstrap**: do not expand process.argv\[1] for snapshot entry points (Joyee Cheung) [#47466](https://github.com/nodejs/node/pull/47466)
* \[[`c81e1143e4`](https://github.com/nodejs/node/commit/c81e1143e4)] - **bootstrap**: store internal loaders in C++ via a binding (Joyee Cheung) [#47215](https://github.com/nodejs/node/pull/47215)
* \[[`8e673bdb84`](https://github.com/nodejs/node/commit/8e673bdb84)] - **build**: add node-core-utils to setup (Jiawen Geng) [#47442](https://github.com/nodejs/node/pull/47442)
* \[[`5b561d72a6`](https://github.com/nodejs/node/commit/5b561d72a6)] - **build**: sync cares source change (Jiawen Geng) [#47359](https://github.com/nodejs/node/pull/47359)
* \[[`8e6ee53e4e`](https://github.com/nodejs/node/commit/8e6ee53e4e)] - **build**: remove non-exist build file (Jiawen Geng) [#47361](https://github.com/nodejs/node/pull/47361)
* \[[`9a4d21d1d9`](https://github.com/nodejs/node/commit/9a4d21d1d9)] - **build, deps, tools**: avoid excessive LTO (Konstantin Demin) [#47313](https://github.com/nodejs/node/pull/47313)
* \[[`48c01485cd`](https://github.com/nodejs/node/commit/48c01485cd)] - **crypto**: replace THROW with CHECK for scrypt keylen (Tobias Nießen) [#47407](https://github.com/nodejs/node/pull/47407)
* \[[`4c1a27716b`](https://github.com/nodejs/node/commit/4c1a27716b)] - **crypto**: re-add padding for AES-KW wrapped JWKs (Filip Skokan) [#46563](https://github.com/nodejs/node/pull/46563)
* \[[`b66eb15d12`](https://github.com/nodejs/node/commit/b66eb15d12)] - **deps**: update simdutf to 3.2.7 (Node.js GitHub Bot) [#47473](https://github.com/nodejs/node/pull/47473)
* \[[`3fc11477ba`](https://github.com/nodejs/node/commit/3fc11477ba)] - **deps**: update corepack to 0.17.2 (Node.js GitHub Bot) [#47474](https://github.com/nodejs/node/pull/47474)
* \[[`c1776531ab`](https://github.com/nodejs/node/commit/c1776531ab)] - **deps**: upgrade npm to 9.6.4 (npm team) [#47432](https://github.com/nodejs/node/pull/47432)
* \[[`e7ca09f310`](https://github.com/nodejs/node/commit/e7ca09f310)] - **deps**: update zlib to upstream 5edb52d4 (Luigi Pinca) [#47151](https://github.com/nodejs/node/pull/47151)
* \[[`88387ccd12`](https://github.com/nodejs/node/commit/88387ccd12)] - **deps**: update ada to 2.0.0 (Node.js GitHub Bot) [#47339](https://github.com/nodejs/node/pull/47339)
* \[[`9f468cc37e`](https://github.com/nodejs/node/commit/9f468cc37e)] - **deps**: cherry-pick Windows ARM64 fix for openssl (Richard Lau) [#46570](https://github.com/nodejs/node/pull/46570)
* \[[`eeab210b1b`](https://github.com/nodejs/node/commit/eeab210b1b)] - **deps**: update archs files for quictls/openssl-3.0.8+quic (RafaelGSS) [#46570](https://github.com/nodejs/node/pull/46570)
* \[[`d93d7716c7`](https://github.com/nodejs/node/commit/d93d7716c7)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.8+quic (RafaelGSS) [#46571](https://github.com/nodejs/node/pull/46571)
* \[[`0f69ec4dd7`](https://github.com/nodejs/node/commit/0f69ec4dd7)] - **deps**: patch V8 to 10.9.194.9 (Michaël Zasso) [#45995](https://github.com/nodejs/node/pull/45995)
* \[[`5890d09644`](https://github.com/nodejs/node/commit/5890d09644)] - **deps**: patch V8 to 10.9.194.6 (Michaël Zasso) [#45748](https://github.com/nodejs/node/pull/45748)
* \[[`c02a7e7e93`](https://github.com/nodejs/node/commit/c02a7e7e93)] - **diagnostics\_channel**: fix ref counting bug when reaching zero subscribers (Stephen Belanger) [#47520](https://github.com/nodejs/node/pull/47520)
* \[[`c7ad5bb37d`](https://github.com/nodejs/node/commit/c7ad5bb37d)] - **doc**: info on handling unintended breaking changes (Michael Dawson) [#47426](https://github.com/nodejs/node/pull/47426)
* \[[`7d2d40ed0d`](https://github.com/nodejs/node/commit/7d2d40ed0d)] - **doc**: add performance initiative (Yagiz Nizipli) [#47424](https://github.com/nodejs/node/pull/47424)
* \[[`d56c0f7318`](https://github.com/nodejs/node/commit/d56c0f7318)] - **doc**: do not create a backup file (Luigi Pinca) [#47151](https://github.com/nodejs/node/pull/47151)
* \[[`412d27b65b`](https://github.com/nodejs/node/commit/412d27b65b)] - **doc**: add MoLow to the TSC (Colin Ihrig) [#47436](https://github.com/nodejs/node/pull/47436)
* \[[`f131cca0c0`](https://github.com/nodejs/node/commit/f131cca0c0)] - **doc**: reserve 116 for Electron 25 (Keeley Hammond) [#47375](https://github.com/nodejs/node/pull/47375)
* \[[`1022c6f424`](https://github.com/nodejs/node/commit/1022c6f424)] - **doc**: add experimental stages (Geoffrey Booth) [#46100](https://github.com/nodejs/node/pull/46100)
* \[[`42d3d74717`](https://github.com/nodejs/node/commit/42d3d74717)] - **doc**: clarify release notes for Node.js 16.19.0 (Richard Lau) [#45846](https://github.com/nodejs/node/pull/45846)
* \[[`533c6512da`](https://github.com/nodejs/node/commit/533c6512da)] - **doc**: clarify release notes for Node.js 14.21.2 (Richard Lau) [#45846](https://github.com/nodejs/node/pull/45846)
* \[[`97165fc1a6`](https://github.com/nodejs/node/commit/97165fc1a6)] - **doc**: fix doc metadata for Node.js 16.19.0 (Richard Lau) [#45863](https://github.com/nodejs/node/pull/45863)
* \[[`a266b8b702`](https://github.com/nodejs/node/commit/a266b8b702)] - **doc**: add registry number for Electron 23 & 24 (Keeley Hammond) [#45661](https://github.com/nodejs/node/pull/45661)
* \[[`2613a9ced9`](https://github.com/nodejs/node/commit/2613a9ced9)] - **esm**: move hook execution to separate thread (Jacob Smith) [#44710](https://github.com/nodejs/node/pull/44710)
* \[[`841f6b3abf`](https://github.com/nodejs/node/commit/841f6b3abf)] - **esm**: increase test coverage of edge cases (Antoine du Hamel) [#47033](https://github.com/nodejs/node/pull/47033)
* \[[`0d575fe61a`](https://github.com/nodejs/node/commit/0d575fe61a)] - **gyp**: put filenames in variables (Cheng Zhao) [#46965](https://github.com/nodejs/node/pull/46965)
* \[[`41b186722c`](https://github.com/nodejs/node/commit/41b186722c)] - **lib**: distinguish webidl interfaces with the extended property "Exposed" (Chengzhong Wu) [#46809](https://github.com/nodejs/node/pull/46809)
* \[[`9b7db62276`](https://github.com/nodejs/node/commit/9b7db62276)] - **lib**: makeRequireFunction patch when experimental policy (RafaelGSS) [nodejs-private/node-private#358](https://github.com/nodejs-private/node-private/pull/358)
* \[[`d43b532789`](https://github.com/nodejs/node/commit/d43b532789)] - **lib**: refactor to use `validateBuffer` (Deokjin Kim) [#46489](https://github.com/nodejs/node/pull/46489)
* \[[`9a76a2521b`](https://github.com/nodejs/node/commit/9a76a2521b)] - **meta**: ping security-wg team on permission model changes (Rafael Gonzaga) [#47483](https://github.com/nodejs/node/pull/47483)
* \[[`a4dadde1ba`](https://github.com/nodejs/node/commit/a4dadde1ba)] - **meta**: ping startup and realm team on src/node\_realm\* changes (Joyee Cheung) [#47448](https://github.com/nodejs/node/pull/47448)
* \[[`631c3ef3de`](https://github.com/nodejs/node/commit/631c3ef3de)] - **module**: do less CJS module loader initialization at run time (Joyee Cheung) [#47194](https://github.com/nodejs/node/pull/47194)
* \[[`8bcf0a42f7`](https://github.com/nodejs/node/commit/8bcf0a42f7)] - **permission**: fix chmod,chown improve fs coverage (Rafael Gonzaga) [#47529](https://github.com/nodejs/node/pull/47529)
* \[[`54d17ff4b5`](https://github.com/nodejs/node/commit/54d17ff4b5)] - **permission**: support fs.mkdtemp (Rafael Gonzaga) [#47470](https://github.com/nodejs/node/pull/47470)
* \[[`b441b5dc65`](https://github.com/nodejs/node/commit/b441b5dc65)] - **permission**: drop process.permission.deny (Rafael Gonzaga) [#47335](https://github.com/nodejs/node/pull/47335)
* \[[`aa30e16716`](https://github.com/nodejs/node/commit/aa30e16716)] - **permission**: fix some vulnerabilities in fs (Tobias Nießen) [#47091](https://github.com/nodejs/node/pull/47091)
* \[[`1726da9300`](https://github.com/nodejs/node/commit/1726da9300)] - **permission**: add path separator to loader check (Rafael Gonzaga) [#47030](https://github.com/nodejs/node/pull/47030)
* \[[`b164038c86`](https://github.com/nodejs/node/commit/b164038c86)] - **permission**: fix spawnSync permission check (RafaelGSS) [#46975](https://github.com/nodejs/node/pull/46975)
* \[[`af91400886`](https://github.com/nodejs/node/commit/af91400886)] - **policy**: makeRequireFunction on mainModule.require (RafaelGSS) [nodejs-private/node-private#358](https://github.com/nodejs-private/node-private/pull/358)
* \[[`f8b4e26aee`](https://github.com/nodejs/node/commit/f8b4e26aee)] - **quic**: add more QUIC impl (James M Snell) [#47348](https://github.com/nodejs/node/pull/47348)
* \[[`d65ae9f678`](https://github.com/nodejs/node/commit/d65ae9f678)] - **quic**: add additional quic implementation utilities (James M Snell) [#47289](https://github.com/nodejs/node/pull/47289)
* \[[`9b104be502`](https://github.com/nodejs/node/commit/9b104be502)] - **quic**: do not dereference shared\_ptr after move (Tobias Nießen) [#47294](https://github.com/nodejs/node/pull/47294)
* \[[`09a4bb152f`](https://github.com/nodejs/node/commit/09a4bb152f)] - **quic**: add multiple internal utilities (James M Snell) [#47263](https://github.com/nodejs/node/pull/47263)
* \[[`2bde0059ca`](https://github.com/nodejs/node/commit/2bde0059ca)] - **sea**: use JSON configuration and blob content for SEA (Joyee Cheung) [#47125](https://github.com/nodejs/node/pull/47125)
* \[[`78c7475493`](https://github.com/nodejs/node/commit/78c7475493)] - **src**: allow simdutf::convert\_\* functions to return zero (Daniel Lemire) [#47471](https://github.com/nodejs/node/pull/47471)
* \[[`5250947a53`](https://github.com/nodejs/node/commit/5250947a53)] - **src**: track ShadowRealm native objects correctly in the heap snapshot (Joyee Cheung) [#47389](https://github.com/nodejs/node/pull/47389)
* \[[`8059764621`](https://github.com/nodejs/node/commit/8059764621)] - **src**: use the internal field to determine if an object is a BaseObject (Joyee Cheung) [#47217](https://github.com/nodejs/node/pull/47217)
* \[[`698508afa8`](https://github.com/nodejs/node/commit/698508afa8)] - **src**: bootstrap prepare stack trace callback in shadow realm (Chengzhong Wu) [#47107](https://github.com/nodejs/node/pull/47107)
* \[[`e6b4d30a2f`](https://github.com/nodejs/node/commit/e6b4d30a2f)] - **src**: bootstrap Web \[Exposed=\*] APIs in the shadow realm (Chengzhong Wu) [#46809](https://github.com/nodejs/node/pull/46809)
* \[[`3646a66044`](https://github.com/nodejs/node/commit/3646a66044)] - **src**: fix AliasedBuffer memory attribution in heap snapshots (Joyee Cheung) [#46817](https://github.com/nodejs/node/pull/46817)
* \[[`8b2126f63f`](https://github.com/nodejs/node/commit/8b2126f63f)] - **src**: move AliasedBuffer implementation to -inl.h (Joyee Cheung) [#46817](https://github.com/nodejs/node/pull/46817)
* \[[`3abbc3829a`](https://github.com/nodejs/node/commit/3abbc3829a)] - **src**: fix useless call in permission.cc (Tobias Nießen) [#46833](https://github.com/nodejs/node/pull/46833)
* \[[`7b1e153530`](https://github.com/nodejs/node/commit/7b1e153530)] - **src**: simplify exit code accesses (Daeyeon Jeong) [#45125](https://github.com/nodejs/node/pull/45125)
* \[[`7359b92a41`](https://github.com/nodejs/node/commit/7359b92a41)] - **test**: remove unnecessary status check on test-release-npm (RafaelGSS) [#47516](https://github.com/nodejs/node/pull/47516)
* \[[`a5a5d2fb7e`](https://github.com/nodejs/node/commit/a5a5d2fb7e)] - **test**: mark test/parallel/test-file-write-stream4 as flaky (Yagiz Nizipli) [#47423](https://github.com/nodejs/node/pull/47423)
* \[[`81ad73a205`](https://github.com/nodejs/node/commit/81ad73a205)] - **test**: remove unused callback variables (angellovc) [#47167](https://github.com/nodejs/node/pull/47167)
* \[[`757a586ead`](https://github.com/nodejs/node/commit/757a586ead)] - **test**: migrate test runner message tests to snapshot (Moshe Atlow) [#47392](https://github.com/nodejs/node/pull/47392)
* \[[`86f890539f`](https://github.com/nodejs/node/commit/86f890539f)] - **test**: remove stale entry from known\_issues.status (Richard Lau) [#47454](https://github.com/nodejs/node/pull/47454)
* \[[`1f3773d0c1`](https://github.com/nodejs/node/commit/1f3773d0c1)] - **test**: move more inspector sequential tests to parallel (Joyee Cheung) [#47412](https://github.com/nodejs/node/pull/47412)
* \[[`617b8d44c6`](https://github.com/nodejs/node/commit/617b8d44c6)] - **test**: use random port in test-inspector-enabled (Joyee Cheung) [#47412](https://github.com/nodejs/node/pull/47412)
* \[[`ade0170c4f`](https://github.com/nodejs/node/commit/ade0170c4f)] - **test**: use random port in test-inspector-debug-brk-flag (Joyee Cheung) [#47412](https://github.com/nodejs/node/pull/47412)
* \[[`1a78632cd3`](https://github.com/nodejs/node/commit/1a78632cd3)] - **test**: use random port in NodeInstance.startViaSignal() (Joyee Cheung) [#47412](https://github.com/nodejs/node/pull/47412)
* \[[`23f66b137e`](https://github.com/nodejs/node/commit/23f66b137e)] - **test**: move test-shadow-realm-gc.js to known\_issues (Joyee Cheung) [#47355](https://github.com/nodejs/node/pull/47355)
* \[[`9dfd0394c5`](https://github.com/nodejs/node/commit/9dfd0394c5)] - **test**: remove useless WPT init scripts (Khafra) [#47221](https://github.com/nodejs/node/pull/47221)
* \[[`1cfe058778`](https://github.com/nodejs/node/commit/1cfe058778)] - **test**: fix test-permission-deny-fs-wildcard (win32) (Tobias Nießen) [#47095](https://github.com/nodejs/node/pull/47095)
* \[[`b8ef1b476e`](https://github.com/nodejs/node/commit/b8ef1b476e)] - **test**: add coverage for custom loader hooks with permission model (Antoine du Hamel) [#46977](https://github.com/nodejs/node/pull/46977)
* \[[`4a7c3e9c50`](https://github.com/nodejs/node/commit/4a7c3e9c50)] - **test**: fix file path in permission symlink test (Livia Medeiros) [#46859](https://github.com/nodejs/node/pull/46859)
* \[[`10005de6a8`](https://github.com/nodejs/node/commit/10005de6a8)] - **tools**: make `js2c.py` usable for other build systems (Cheng Zhao) [#46930](https://github.com/nodejs/node/pull/46930)
* \[[`1e2f9aca72`](https://github.com/nodejs/node/commit/1e2f9aca72)] - **tools**: move update-acorn.sh to dep\_updaters and create maintaining md (Marco Ippolito) [#47382](https://github.com/nodejs/node/pull/47382)
* \[[`174662a463`](https://github.com/nodejs/node/commit/174662a463)] - **tools**: update eslint to 8.38.0 (Node.js GitHub Bot) [#47475](https://github.com/nodejs/node/pull/47475)
* \[[`a58ca61f35`](https://github.com/nodejs/node/commit/a58ca61f35)] - **tools**: update eslint to 8.38.0 (Node.js GitHub Bot) [#47475](https://github.com/nodejs/node/pull/47475)
* \[[`37d12730ab`](https://github.com/nodejs/node/commit/37d12730ab)] - **tools**: automate cjs-module-lexer dependency update (Marco Ippolito) [#47446](https://github.com/nodejs/node/pull/47446)
* \[[`4fbfa3c9f2`](https://github.com/nodejs/node/commit/4fbfa3c9f2)] - **tools**: fix notify-on-push Slack messages (Antoine du Hamel) [#47453](https://github.com/nodejs/node/pull/47453)
* \[[`b1f2ff1242`](https://github.com/nodejs/node/commit/b1f2ff1242)] - **tools**: update lint-md-dependencies to @rollup/plugin-node-resolve\@15.0.2 (Node.js GitHub Bot) [#47431](https://github.com/nodejs/node/pull/47431)
* \[[`26b2584b84`](https://github.com/nodejs/node/commit/26b2584b84)] - **tools**: add root certificate update script (Richard Lau) [#47425](https://github.com/nodejs/node/pull/47425)
* \[[`553b052648`](https://github.com/nodejs/node/commit/553b052648)] - **tools**: remove targets for individual test suites in `Makefile` (Antoine du Hamel) [#46892](https://github.com/nodejs/node/pull/46892)
* \[[`747ff43e5b`](https://github.com/nodejs/node/commit/747ff43e5b)] - **url**: more sophisticated brand check for URLSearchParams (Timothy Gu) [#47414](https://github.com/nodejs/node/pull/47414)
* \[[`e727eb066f`](https://github.com/nodejs/node/commit/e727eb066f)] - **url**: do not use object as hashmap (Timothy Gu) [#47415](https://github.com/nodejs/node/pull/47415)
* \[[`81c7875eb7`](https://github.com/nodejs/node/commit/81c7875eb7)] - **url**: drop ICU requirement for parsing hostnames (Yagiz Nizipli) [#47339](https://github.com/nodejs/node/pull/47339)
* \[[`a4895df94a`](https://github.com/nodejs/node/commit/a4895df94a)] - **url**: use ada::url\_aggregator for parsing urls (Yagiz Nizipli) [#47339](https://github.com/nodejs/node/pull/47339)

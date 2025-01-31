# Node.js 23 ChangeLog

<!--lint disable maximum-line-length no-literal-urls prohibited-strings-->

<table>
<tr>
<th>Current</th>
</tr>
<tr>
<td>
<a href="#23.7.0">23.7.0</a><br/>
<a href="#23.6.1">23.6.1</a><br/>
<a href="#23.6.0">23.6.0</a><br/>
<a href="#23.5.0">23.5.0</a><br/>
<a href="#23.4.0">23.4.0</a><br/>
<a href="#23.3.0">23.3.0</a><br/>
<a href="#23.2.0">23.2.0</a><br/>
<a href="#23.1.0">23.1.0</a><br/>
<a href="#23.0.0">23.0.0</a><br/>
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
  * [4.x](CHANGELOG_V4.md)
  * [0.12.x](CHANGELOG_V012.md)
  * [0.10.x](CHANGELOG_V010.md)
  * [io.js](CHANGELOG_IOJS.md)
  * [Archive](CHANGELOG_ARCHIVE.md)

<a id="23.7.0"></a>

## 2025-01-30, Version 23.7.0 (Current), @aduh95

### Notable Changes

* \[[`36dd9ecc41`](https://github.com/nodejs/node/commit/36dd9ecc41)] - **crypto**: update root certificates to NSS 3.107 (Node.js GitHub Bot) [#56566](https://github.com/nodejs/node/pull/56566)
* \[[`9414d3cbf1`](https://github.com/nodejs/node/commit/9414d3cbf1)] - **(SEMVER-MINOR)** **fs**: allow `exclude` option in globs to accept glob patterns (Daeyeon Jeong) [#56489](https://github.com/nodejs/node/pull/56489)
* \[[`9c5c3b3115`](https://github.com/nodejs/node/commit/9c5c3b3115)] - **(SEMVER-MINOR)** **module**: add ERR\_UNSUPPORTED\_TYPESCRIPT\_SYNTAX (Marco Ippolito) [#56610](https://github.com/nodejs/node/pull/56610)
* \[[`1e201fd5fd`](https://github.com/nodejs/node/commit/1e201fd5fd)] - **(SEMVER-MINOR)** **sqlite**: support TypedArray and DataView in `StatementSync` (Alex Yang) [#56385](https://github.com/nodejs/node/pull/56385)
* \[[`48c813fb67`](https://github.com/nodejs/node/commit/48c813fb67)] - **(SEMVER-MINOR)** **src**: add --disable-sigusr1 to prevent signal i/o thread (Rafael Gonzaga) [#56441](https://github.com/nodejs/node/pull/56441)
* \[[`cf16123785`](https://github.com/nodejs/node/commit/cf16123785)] - **(SEMVER-MINOR)** **src,worker**: add isInternalWorker (Carlos Espa) [#56469](https://github.com/nodejs/node/pull/56469)
* \[[`13bdd9c961`](https://github.com/nodejs/node/commit/13bdd9c961)] - **(SEMVER-MINOR)** **test\_runner**: add TestContext.prototype.waitFor() (Colin Ihrig) [#56595](https://github.com/nodejs/node/pull/56595)
* \[[`00a1943858`](https://github.com/nodejs/node/commit/00a1943858)] - **(SEMVER-MINOR)** **test\_runner**: add t.assert.fileSnapshot() (Colin Ihrig) [#56459](https://github.com/nodejs/node/pull/56459)
* \[[`3143566045`](https://github.com/nodejs/node/commit/3143566045)] - **(SEMVER-MINOR)** **test\_runner**: add assert.register() API (Colin Ihrig) [#56434](https://github.com/nodejs/node/pull/56434)

### Commits

* \[[`334a3ac7c6`](https://github.com/nodejs/node/commit/334a3ac7c6)] - **assert**: make myers\_diff function more performant (Giovanni Bucci) [#56303](https://github.com/nodejs/node/pull/56303)
* \[[`eb2bf460b7`](https://github.com/nodejs/node/commit/eb2bf460b7)] - **assert**: make partialDeepStrictEqual work with urls and File prototypes (Giovanni Bucci) [#56231](https://github.com/nodejs/node/pull/56231)
* \[[`d184453b90`](https://github.com/nodejs/node/commit/d184453b90)] - **assert**: show diff when doing partial comparisons (Giovanni Bucci) [#56211](https://github.com/nodejs/node/pull/56211)
* \[[`4aa1afd607`](https://github.com/nodejs/node/commit/4aa1afd607)] - **benchmark**: add validateStream to styleText bench (Rafael Gonzaga) [#56556](https://github.com/nodejs/node/pull/56556)
* \[[`8bbdb1203e`](https://github.com/nodejs/node/commit/8bbdb1203e)] - **child\_process**: fix parsing messages with splitted length field (Maksim Gorkov) [#56106](https://github.com/nodejs/node/pull/56106)
* \[[`d83d89a08e`](https://github.com/nodejs/node/commit/d83d89a08e)] - **crypto**: add missing return value check (Michael Dawson) [#56615](https://github.com/nodejs/node/pull/56615)
* \[[`36dd9ecc41`](https://github.com/nodejs/node/commit/36dd9ecc41)] - **crypto**: update root certificates to NSS 3.107 (Node.js GitHub Bot) [#56566](https://github.com/nodejs/node/pull/56566)
* \[[`3915152c36`](https://github.com/nodejs/node/commit/3915152c36)] - **crypto**: fix checkPrime crash with large buffers (Santiago Gimeno) [#56559](https://github.com/nodejs/node/pull/56559)
* \[[`c8d1dcb063`](https://github.com/nodejs/node/commit/c8d1dcb063)] - **crypto**: fix warning of ignoring return value (Cheng) [#56527](https://github.com/nodejs/node/pull/56527)
* \[[`1994eaaf52`](https://github.com/nodejs/node/commit/1994eaaf52)] - **crypto**: make generatePrime/checkPrime interruptible (James M Snell) [#56460](https://github.com/nodejs/node/pull/56460)
* \[[`5f1ee05390`](https://github.com/nodejs/node/commit/5f1ee05390)] - **deps**: update corepack to 0.31.0 (Node.js GitHub Bot) [#56795](https://github.com/nodejs/node/pull/56795)
* \[[`9cfac712b8`](https://github.com/nodejs/node/commit/9cfac712b8)] - **deps**: move inspector\_protocol to deps (Chengzhong Wu) [#56649](https://github.com/nodejs/node/pull/56649)
* \[[`b2ec816a31`](https://github.com/nodejs/node/commit/b2ec816a31)] - **deps**: macro ENODATA is deprecated in libc++ (Cheng) [#56698](https://github.com/nodejs/node/pull/56698)
* \[[`edd9361499`](https://github.com/nodejs/node/commit/edd9361499)] - **deps**: fixup some minor coverity warnings (James M Snell) [#56612](https://github.com/nodejs/node/pull/56612)
* \[[`9ffe3ad4b1`](https://github.com/nodejs/node/commit/9ffe3ad4b1)] - **deps**: update libuv to 1.50.0 (Node.js GitHub Bot) [#56616](https://github.com/nodejs/node/pull/56616)
* \[[`73ad3ca238`](https://github.com/nodejs/node/commit/73ad3ca238)] - **deps**: update amaro to 0.3.0 (Node.js GitHub Bot) [#56568](https://github.com/nodejs/node/pull/56568)
* \[[`0657f6270a`](https://github.com/nodejs/node/commit/0657f6270a)] - **deps**: update amaro to 0.2.2 (Node.js GitHub Bot) [#56568](https://github.com/nodejs/node/pull/56568)
* \[[`47fad8cbc0`](https://github.com/nodejs/node/commit/47fad8cbc0)] - **deps**: update simdutf to 6.0.3 (Node.js GitHub Bot) [#56567](https://github.com/nodejs/node/pull/56567)
* \[[`c9a211ae29`](https://github.com/nodejs/node/commit/c9a211ae29)] - **diagnostics\_channel**: capture console messages (Stephen Belanger) [#56292](https://github.com/nodejs/node/pull/56292)
* \[[`cf5d2d6598`](https://github.com/nodejs/node/commit/cf5d2d6598)] - **doc**: move anatoli to emeritus (Michael Dawson) [#56592](https://github.com/nodejs/node/pull/56592)
* \[[`5dd08d10be`](https://github.com/nodejs/node/commit/5dd08d10be)] - **doc**: fix styles of the expandable TOC (Antoine du Hamel) [#56755](https://github.com/nodejs/node/pull/56755)
* \[[`09fb3adf80`](https://github.com/nodejs/node/commit/09fb3adf80)] - **doc**: add "Skip to content" button (Antoine du Hamel) [#56750](https://github.com/nodejs/node/pull/56750)
* \[[`ad012ca1f3`](https://github.com/nodejs/node/commit/ad012ca1f3)] - **doc**: improve accessibility of expandable lists (Antoine du Hamel) [#56749](https://github.com/nodejs/node/pull/56749)
* \[[`38acdb57eb`](https://github.com/nodejs/node/commit/38acdb57eb)] - **doc**: add note regarding commit message trailers (Dario Piotrowicz) [#56736](https://github.com/nodejs/node/pull/56736)
* \[[`f4a9b134c0`](https://github.com/nodejs/node/commit/f4a9b134c0)] - **doc**: fix typo in example code for util.styleText (Robin Mehner) [#56720](https://github.com/nodejs/node/pull/56720)
* \[[`8a61aaa734`](https://github.com/nodejs/node/commit/8a61aaa734)] - **doc**: fix inconsistencies in `WeakSet` and `WeakMap` comparison details (Shreyans Pathak) [#56683](https://github.com/nodejs/node/pull/56683)
* \[[`4ade128184`](https://github.com/nodejs/node/commit/4ade128184)] - **doc**: add RafaelGSS as latest sec release stewards (Rafael Gonzaga) [#56682](https://github.com/nodejs/node/pull/56682)
* \[[`e1e1200b79`](https://github.com/nodejs/node/commit/e1e1200b79)] - **doc**: clarify cjs/esm diff in `queueMicrotask()` vs `process.nextTick()` (Dario Piotrowicz) [#56659](https://github.com/nodejs/node/pull/56659)
* \[[`57a7b931fb`](https://github.com/nodejs/node/commit/57a7b931fb)] - **doc**: `WeakSet` and `WeakMap` comparison details (Shreyans Pathak) [#56648](https://github.com/nodejs/node/pull/56648)
* \[[`56b21489f4`](https://github.com/nodejs/node/commit/56b21489f4)] - **doc**: mention prepare --security (Rafael Gonzaga) [#56617](https://github.com/nodejs/node/pull/56617)
* \[[`67f39b597a`](https://github.com/nodejs/node/commit/67f39b597a)] - **doc**: tweak info on reposts in ambassador program (Michael Dawson) [#56589](https://github.com/nodejs/node/pull/56589)
* \[[`6381e0761d`](https://github.com/nodejs/node/commit/6381e0761d)] - **doc**: add type stripping to ambassadors program (Marco Ippolito) [#56598](https://github.com/nodejs/node/pull/56598)
* \[[`9bd438acd3`](https://github.com/nodejs/node/commit/9bd438acd3)] - **doc**: improve internal documentation on built-in snapshot (Joyee Cheung) [#56505](https://github.com/nodejs/node/pull/56505)
* \[[`f54118c84a`](https://github.com/nodejs/node/commit/f54118c84a)] - **doc**: correct customization hook types & clarify descriptions (Jacob Smith) [#56454](https://github.com/nodejs/node/pull/56454)
* \[[`6af5053153`](https://github.com/nodejs/node/commit/6af5053153)] - **doc**: document CLI way to open the nodejs/bluesky PR (Antoine du Hamel) [#56506](https://github.com/nodejs/node/pull/56506)
* \[[`4a77a9e1eb`](https://github.com/nodejs/node/commit/4a77a9e1eb)] - **doc**: add history info for Permission Model (Antoine du Hamel) [#56707](https://github.com/nodejs/node/pull/56707)
* \[[`097b8b4889`](https://github.com/nodejs/node/commit/097b8b4889)] - **doc**: add note for features using `InternalWorker` with permission model (Antoine du Hamel) [#56706](https://github.com/nodejs/node/pull/56706)
* \[[`f600466c73`](https://github.com/nodejs/node/commit/f600466c73)] - **doc**: add section about using npx with permission model (Rafael Gonzaga) [#56539](https://github.com/nodejs/node/pull/56539)
* \[[`c2d5a0c629`](https://github.com/nodejs/node/commit/c2d5a0c629)] - **doc**: update gcc-version for ubuntu-lts (Kunal Kumar) [#56553](https://github.com/nodejs/node/pull/56553)
* \[[`202af46793`](https://github.com/nodejs/node/commit/202af46793)] - **doc**: fix parentheses in options (Tobias Nießen) [#56563](https://github.com/nodejs/node/pull/56563)
* \[[`4e4b0c63d0`](https://github.com/nodejs/node/commit/4e4b0c63d0)] - **doc**: fix location of NO\_COLOR in CLI docs (Colin Ihrig) [#56525](https://github.com/nodejs/node/pull/56525)
* \[[`92eeeb98a5`](https://github.com/nodejs/node/commit/92eeeb98a5)] - **doc**: include CVE to EOL lines as sec release process (Rafael Gonzaga) [#56520](https://github.com/nodejs/node/pull/56520)
* \[[`233a6a93a1`](https://github.com/nodejs/node/commit/233a6a93a1)] - **doc**: add esm examples to node:trace\_events (Alfredo González) [#56514](https://github.com/nodejs/node/pull/56514)
* \[[`d9cff6c73f`](https://github.com/nodejs/node/commit/d9cff6c73f)] - **doc**: reserve NMV 133 for Electron 35 (Keeley Hammond) [#56513](https://github.com/nodejs/node/pull/56513)
* \[[`6047fd7c5c`](https://github.com/nodejs/node/commit/6047fd7c5c)] - **doc**: add message for Ambassadors to promote (Michael Dawson) [#56235](https://github.com/nodejs/node/pull/56235)
* \[[`a4045c9488`](https://github.com/nodejs/node/commit/a4045c9488)] - **doc**: allow request for TSC reviews via the GitHub UI (Antoine du Hamel) [#56493](https://github.com/nodejs/node/pull/56493)
* \[[`dd3f94873e`](https://github.com/nodejs/node/commit/dd3f94873e)] - **esm**: fix jsdoc type refs to `ModuleJobBase` in esm/loader (Jacob Smith) [#56499](https://github.com/nodejs/node/pull/56499)
* \[[`9414d3cbf1`](https://github.com/nodejs/node/commit/9414d3cbf1)] - **(SEMVER-MINOR)** **fs**: allow `exclude` option in globs to accept glob patterns (Daeyeon Jeong) [#56489](https://github.com/nodejs/node/pull/56489)
* \[[`4202045673`](https://github.com/nodejs/node/commit/4202045673)] - **http2**: omit server name when HTTP2 host is IP address (islandryu) [#56530](https://github.com/nodejs/node/pull/56530)
* \[[`f48a562776`](https://github.com/nodejs/node/commit/f48a562776)] - **inspector**: roll inspector\_protocol (Chengzhong Wu) [#56649](https://github.com/nodejs/node/pull/56649)
* \[[`9a954fbf4a`](https://github.com/nodejs/node/commit/9a954fbf4a)] - **inspector**: add undici http tracking support (Chengzhong Wu) [#56488](https://github.com/nodejs/node/pull/56488)
* \[[`f185e8a34a`](https://github.com/nodejs/node/commit/f185e8a34a)] - **inspector**: report loadingFinished until the response data is consumed (Chengzhong Wu) [#56372](https://github.com/nodejs/node/pull/56372)
* \[[`2fb007fdce`](https://github.com/nodejs/node/commit/2fb007fdce)] - **lib**: allow skipping source maps in node\_modules (Chengzhong Wu) [#56639](https://github.com/nodejs/node/pull/56639)
* \[[`2f69dc2659`](https://github.com/nodejs/node/commit/2f69dc2659)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#56580](https://github.com/nodejs/node/pull/56580)
* \[[`0d869963e0`](https://github.com/nodejs/node/commit/0d869963e0)] - **meta**: add codeowners of security release document (Rafael Gonzaga) [#56521](https://github.com/nodejs/node/pull/56521)
* \[[`59510ab819`](https://github.com/nodejs/node/commit/59510ab819)] - **module**: fix bad `require.resolve` with option paths for `.` and `..` (Dario Piotrowicz) [#56735](https://github.com/nodejs/node/pull/56735)
* \[[`58d2dad67d`](https://github.com/nodejs/node/commit/58d2dad67d)] - **module**: integrate TypeScript into compile cache (Joyee Cheung) [#56629](https://github.com/nodejs/node/pull/56629)
* \[[`9f99a6acb5`](https://github.com/nodejs/node/commit/9f99a6acb5)] - **module**: use more defensive code when handling SWC errors (Antoine du Hamel) [#56646](https://github.com/nodejs/node/pull/56646)
* \[[`7347d34053`](https://github.com/nodejs/node/commit/7347d34053)] - **module**: fixing url change in load sync hook chain (Vitalii Akimov) [#56402](https://github.com/nodejs/node/pull/56402)
* \[[`9c5c3b3115`](https://github.com/nodejs/node/commit/9c5c3b3115)] - **(SEMVER-MINOR)** **module**: add ERR\_UNSUPPORTED\_TYPESCRIPT\_SYNTAX (Marco Ippolito) [#56610](https://github.com/nodejs/node/pull/56610)
* \[[`afd1f91a1e`](https://github.com/nodejs/node/commit/afd1f91a1e)] - **module**: fix jsdoc for `format` parameter in cjs/loader (pacexy) [#56501](https://github.com/nodejs/node/pull/56501)
* \[[`86d783fa51`](https://github.com/nodejs/node/commit/86d783fa51)] - **module**: rethrow amaro error message (Marco Ippolito) [#56568](https://github.com/nodejs/node/pull/56568)
* \[[`7b6df4a97a`](https://github.com/nodejs/node/commit/7b6df4a97a)] - **process**: fix symbol key and mark experimental new `node:process` methods (Antoine du Hamel) [#56517](https://github.com/nodejs/node/pull/56517)
* \[[`21362cc4f4`](https://github.com/nodejs/node/commit/21362cc4f4)] - **punycode**: limit deprecation warning (Colin Ihrig) [#56632](https://github.com/nodejs/node/pull/56632)
* \[[`93f60a1c15`](https://github.com/nodejs/node/commit/93f60a1c15)] - **sqlite**: disable memstatus APIs at build time (Colin Ihrig) [#56541](https://github.com/nodejs/node/pull/56541)
* \[[`1e201fd5fd`](https://github.com/nodejs/node/commit/1e201fd5fd)] - **(SEMVER-MINOR)** **sqlite**: support TypedArray and DataView in `StatementSync` (Alex Yang) [#56385](https://github.com/nodejs/node/pull/56385)
* \[[`3aca628a11`](https://github.com/nodejs/node/commit/3aca628a11)] - **sqlite**: enable SQL math functions (Colin Ihrig) [#56447](https://github.com/nodejs/node/pull/56447)
* \[[`575251ae6a`](https://github.com/nodejs/node/commit/575251ae6a)] - **src**: add nullptr handling from X509\_STORE\_new() (Burkov Egor) [#56700](https://github.com/nodejs/node/pull/56700)
* \[[`8fb03d8f43`](https://github.com/nodejs/node/commit/8fb03d8f43)] - **src**: move more crypto to ncrypto (James M Snell) [#56653](https://github.com/nodejs/node/pull/56653)
* \[[`55a0135261`](https://github.com/nodejs/node/commit/55a0135261)] - **src**: add default value for RSACipherConfig mode field (Burkov Egor) [#56701](https://github.com/nodejs/node/pull/56701)
* \[[`83c56da328`](https://github.com/nodejs/node/commit/83c56da328)] - **src**: fix build with GCC 15 (tjuhaszrh) [#56740](https://github.com/nodejs/node/pull/56740)
* \[[`872d68d87c`](https://github.com/nodejs/node/commit/872d68d87c)] - **src**: fix to generate path from wchar\_t via wstring (yamachu) [#56696](https://github.com/nodejs/node/pull/56696)
* \[[`2b6a82dcea`](https://github.com/nodejs/node/commit/2b6a82dcea)] - **src**: replace NoArrayBufferZeroFillScope with v8 option (James M Snell) [#56658](https://github.com/nodejs/node/pull/56658)
* \[[`a5f9023297`](https://github.com/nodejs/node/commit/a5f9023297)] - **src**: initialize FSReqWrapSync in path that uses it (Michaël Zasso) [#56613](https://github.com/nodejs/node/pull/56613)
* \[[`90f70ed8dd`](https://github.com/nodejs/node/commit/90f70ed8dd)] - **src**: use cppgc to manage ContextifyContext (Joyee Cheung) [#56522](https://github.com/nodejs/node/pull/56522)
* \[[`0b1ac9653e`](https://github.com/nodejs/node/commit/0b1ac9653e)] - **src**: handle duplicate paths granted (Rafael Gonzaga) [#56591](https://github.com/nodejs/node/pull/56591)
* \[[`33f5345002`](https://github.com/nodejs/node/commit/33f5345002)] - **src**: update ECKeyPointer in ncrypto (James M Snell) [#56526](https://github.com/nodejs/node/pull/56526)
* \[[`c7b95fcf95`](https://github.com/nodejs/node/commit/c7b95fcf95)] - **src**: update ECPointPointer in ncrypto (James M Snell) [#56526](https://github.com/nodejs/node/pull/56526)
* \[[`c008b15108`](https://github.com/nodejs/node/commit/c008b15108)] - **src**: update ECGroupPointer in ncrypto (James M Snell) [#56526](https://github.com/nodejs/node/pull/56526)
* \[[`5673dc7de7`](https://github.com/nodejs/node/commit/5673dc7de7)] - **src**: update ECDASSigPointer implementation in ncrypto (James M Snell) [#56526](https://github.com/nodejs/node/pull/56526)
* \[[`87ba48b2c6`](https://github.com/nodejs/node/commit/87ba48b2c6)] - **src**: cleaning up more crypto internals for ncrypto (James M Snell) [#56526](https://github.com/nodejs/node/pull/56526)
* \[[`48c813fb67`](https://github.com/nodejs/node/commit/48c813fb67)] - **(SEMVER-MINOR)** **src**: add --disable-sigusr1 to prevent signal i/o thread (Rafael Gonzaga) [#56441](https://github.com/nodejs/node/pull/56441)
* \[[`50c65eed78`](https://github.com/nodejs/node/commit/50c65eed78)] - **src**: fix undefined script name in error source (Chengzhong Wu) [#56502](https://github.com/nodejs/node/pull/56502)
* \[[`b3c66d2493`](https://github.com/nodejs/node/commit/b3c66d2493)] - **src**: refactor --trace-env to reuse option selection and handling (Joyee Cheung) [#56293](https://github.com/nodejs/node/pull/56293)
* \[[`17d59efe3c`](https://github.com/nodejs/node/commit/17d59efe3c)] - **src**: minor cleanups on OneByteString usage (James M Snell) [#56482](https://github.com/nodejs/node/pull/56482)
* \[[`3e6e0106f6`](https://github.com/nodejs/node/commit/3e6e0106f6)] - **src**: move more crypto impl detail to ncrypto dep (James M Snell) [#56421](https://github.com/nodejs/node/pull/56421)
* \[[`5e1ddd5d4c`](https://github.com/nodejs/node/commit/5e1ddd5d4c)] - **src**: fixup more ToLocalChecked uses in node\_file (James M Snell) [#56484](https://github.com/nodejs/node/pull/56484)
* \[[`aa3fd2f58f`](https://github.com/nodejs/node/commit/aa3fd2f58f)] - **src**: make some minor ToLocalChecked cleanups (James M Snell) [#56483](https://github.com/nodejs/node/pull/56483)
* \[[`7dd8165b0b`](https://github.com/nodejs/node/commit/7dd8165b0b)] - **src**: lock the thread properly in snapshot builder (Joyee Cheung) [#56327](https://github.com/nodejs/node/pull/56327)
* \[[`edafab7248`](https://github.com/nodejs/node/commit/edafab7248)] - **src**: drain platform tasks before creating startup snapshot (Chengzhong Wu) [#56403](https://github.com/nodejs/node/pull/56403)
* \[[`e1887d2c58`](https://github.com/nodejs/node/commit/e1887d2c58)] - **src**: use LocalVector in more places (James M Snell) [#56457](https://github.com/nodejs/node/pull/56457)
* \[[`cf16123785`](https://github.com/nodejs/node/commit/cf16123785)] - **(SEMVER-MINOR)** **src,worker**: add isInternalWorker (Carlos Espa) [#56469](https://github.com/nodejs/node/pull/56469)
* \[[`df78515664`](https://github.com/nodejs/node/commit/df78515664)] - **stream**: fix typo in ReadableStreamBYOBReader.readIntoRequests (Mattias Buelens) [#56560](https://github.com/nodejs/node/pull/56560)
* \[[`4ff79fb22a`](https://github.com/nodejs/node/commit/4ff79fb22a)] - **test**: reduce number of written chunks (Luigi Pinca) [#56757](https://github.com/nodejs/node/pull/56757)
* \[[`2e7b7b7674`](https://github.com/nodejs/node/commit/2e7b7b7674)] - **test**: fix invalid common.mustSucceed() usage (Luigi Pinca) [#56756](https://github.com/nodejs/node/pull/56756)
* \[[`0af368ce5e`](https://github.com/nodejs/node/commit/0af368ce5e)] - **test**: use strict mode in global setters test (Rich Trott) [#56742](https://github.com/nodejs/node/pull/56742)
* \[[`e49f3e944c`](https://github.com/nodejs/node/commit/e49f3e944c)] - **test**: cleanup and simplify test-crypto-aes-wrap (James M Snell) [#56748](https://github.com/nodejs/node/pull/56748)
* \[[`85f7bbf4e4`](https://github.com/nodejs/node/commit/85f7bbf4e4)] - **test**: do not use common.isMainThread (Luigi Pinca) [#56768](https://github.com/nodejs/node/pull/56768)
* \[[`36b02bf1b1`](https://github.com/nodejs/node/commit/36b02bf1b1)] - **test**: make some requires lazy in common/index (James M Snell) [#56715](https://github.com/nodejs/node/pull/56715)
* \[[`bcb35c3fb7`](https://github.com/nodejs/node/commit/bcb35c3fb7)] - **test**: add test that uses multibyte for path and resolves modules (yamachu) [#56696](https://github.com/nodejs/node/pull/56696)
* \[[`917f98b29c`](https://github.com/nodejs/node/commit/917f98b29c)] - **test**: replace more uses of `global` with `globalThis` (James M Snell) [#56712](https://github.com/nodejs/node/pull/56712)
* \[[`bf34a49206`](https://github.com/nodejs/node/commit/bf34a49206)] - **test**: make common/index slightly less node.js specific (James M Snell) [#56712](https://github.com/nodejs/node/pull/56712)
* \[[`ef2ed71389`](https://github.com/nodejs/node/commit/ef2ed71389)] - **test**: rely less on duplicative common test harness utilities (James M Snell) [#56712](https://github.com/nodejs/node/pull/56712)
* \[[`e654c8b84a`](https://github.com/nodejs/node/commit/e654c8b84a)] - **test**: simplify common/index.js (James M Snell) [#56712](https://github.com/nodejs/node/pull/56712)
* \[[`a62345e73b`](https://github.com/nodejs/node/commit/a62345e73b)] - **test**: move hasMultiLocalhost to common/net (James M Snell) [#56716](https://github.com/nodejs/node/pull/56716)
* \[[`6edf04ee5e`](https://github.com/nodejs/node/commit/6edf04ee5e)] - **test**: move crypto related common utilities in common/crypto (James M Snell) [#56714](https://github.com/nodejs/node/pull/56714)
* \[[`c7a132229f`](https://github.com/nodejs/node/commit/c7a132229f)] - **test**: add missing test for env file (Jonas) [#56642](https://github.com/nodejs/node/pull/56642)
* \[[`2a219eddf6`](https://github.com/nodejs/node/commit/2a219eddf6)] - **test**: enforce strict mode in test-zlib-const (Rich Trott) [#56689](https://github.com/nodejs/node/pull/56689)
* \[[`f885496d9c`](https://github.com/nodejs/node/commit/f885496d9c)] - **test**: fix localization data for ICU 74.2 (Antoine du Hamel) [#56661](https://github.com/nodejs/node/pull/56661)
* \[[`eb3148fb5c`](https://github.com/nodejs/node/commit/eb3148fb5c)] - **test**: use --permission instead of --experimental-permission (Rafael Gonzaga) [#56685](https://github.com/nodejs/node/pull/56685)
* \[[`86d7ba09c4`](https://github.com/nodejs/node/commit/86d7ba09c4)] - **test**: test-stream-compose.js doesn't need internals (Meghan Denny) [#56619](https://github.com/nodejs/node/pull/56619)
* \[[`676276889e`](https://github.com/nodejs/node/commit/676276889e)] - **test**: add maxCount and gcOptions to gcUntil() (Joyee Cheung) [#56522](https://github.com/nodejs/node/pull/56522)
* \[[`5b7a012144`](https://github.com/nodejs/node/commit/5b7a012144)] - **test**: add line break at end of file (Rafael Gonzaga) [#56588](https://github.com/nodejs/node/pull/56588)
* \[[`27cfec619f`](https://github.com/nodejs/node/commit/27cfec619f)] - **test**: mark test-worker-prof as flaky on smartos (Joyee Cheung) [#56583](https://github.com/nodejs/node/pull/56583)
* \[[`7e58da68c1`](https://github.com/nodejs/node/commit/7e58da68c1)] - **test**: update ts eval snapshots (Marco Ippolito) [#56568](https://github.com/nodejs/node/pull/56568)
* \[[`b1c54439ae`](https://github.com/nodejs/node/commit/b1c54439ae)] - **test**: update test-child-process-bad-stdio to use node:test (Colin Ihrig) [#56562](https://github.com/nodejs/node/pull/56562)
* \[[`0d772a963e`](https://github.com/nodejs/node/commit/0d772a963e)] - **test**: disable openssl 3.4.0 incompatible tests (Jelle van der Waa) [#56160](https://github.com/nodejs/node/pull/56160)
* \[[`6fa6d699ff`](https://github.com/nodejs/node/commit/6fa6d699ff)] - **test**: make test-crypto-hash compatible with OpenSSL > 3.4.0 (Jelle van der Waa) [#56160](https://github.com/nodejs/node/pull/56160)
* \[[`90e12f2945`](https://github.com/nodejs/node/commit/90e12f2945)] - **test**: clarify fork inherit permission flags (Rafael Gonzaga) [#56523](https://github.com/nodejs/node/pull/56523)
* \[[`323f96f7b3`](https://github.com/nodejs/node/commit/323f96f7b3)] - **test**: add error only reporter for node:test (Carlos Espa) [#56438](https://github.com/nodejs/node/pull/56438)
* \[[`cbbcaf9108`](https://github.com/nodejs/node/commit/cbbcaf9108)] - **test**: mark test-http-server-request-timeouts-mixed as flaky (Joyee Cheung) [#56503](https://github.com/nodejs/node/pull/56503)
* \[[`295db19ba2`](https://github.com/nodejs/node/commit/295db19ba2)] - **test**: update error code in tls-psk-circuit for for OpenSSL 3.4 (sebastianas) [#56420](https://github.com/nodejs/node/pull/56420)
* \[[`f7563780a6`](https://github.com/nodejs/node/commit/f7563780a6)] - **test**: update compiled sqlite tests to match other tests (Colin Ihrig) [#56446](https://github.com/nodejs/node/pull/56446)
* \[[`8feb2737e7`](https://github.com/nodejs/node/commit/8feb2737e7)] - **test**: add initial test426 coverage (Chengzhong Wu) [#56436](https://github.com/nodejs/node/pull/56436)
* \[[`b9cd7895c0`](https://github.com/nodejs/node/commit/b9cd7895c0)] - **test**: update test-set-http-max-http-headers to use node:test (Colin Ihrig) [#56439](https://github.com/nodejs/node/pull/56439)
* \[[`332ce548cb`](https://github.com/nodejs/node/commit/332ce548cb)] - **test**: update test-child-process-windows-hide to use node:test (Colin Ihrig) [#56437](https://github.com/nodejs/node/pull/56437)
* \[[`e2668c0e00`](https://github.com/nodejs/node/commit/e2668c0e00)] - **test\_runner**: print failing assertion only once with spec reporter (Pietro Marchini) [#56662](https://github.com/nodejs/node/pull/56662)
* \[[`f97cd5b02b`](https://github.com/nodejs/node/commit/f97cd5b02b)] - **test\_runner**: remove unused errors (Pietro Marchini) [#56607](https://github.com/nodejs/node/pull/56607)
* \[[`13bdd9c961`](https://github.com/nodejs/node/commit/13bdd9c961)] - **(SEMVER-MINOR)** **test\_runner**: add TestContext.prototype.waitFor() (Colin Ihrig) [#56595](https://github.com/nodejs/node/pull/56595)
* \[[`00a1943858`](https://github.com/nodejs/node/commit/00a1943858)] - **(SEMVER-MINOR)** **test\_runner**: add t.assert.fileSnapshot() (Colin Ihrig) [#56459](https://github.com/nodejs/node/pull/56459)
* \[[`c4979ebfb2`](https://github.com/nodejs/node/commit/c4979ebfb2)] - **test\_runner**: run single test file benchmark (Pietro Marchini) [#56479](https://github.com/nodejs/node/pull/56479)
* \[[`839a06e908`](https://github.com/nodejs/node/commit/839a06e908)] - **test\_runner**: differentiate test types in enqueue dequeue events (Eddie Abbondanzio) [#54049](https://github.com/nodejs/node/pull/54049)
* \[[`3143566045`](https://github.com/nodejs/node/commit/3143566045)] - **(SEMVER-MINOR)** **test\_runner**: add assert.register() API (Colin Ihrig) [#56434](https://github.com/nodejs/node/pull/56434)
* \[[`3aa864904f`](https://github.com/nodejs/node/commit/3aa864904f)] - **test\_runner**: finish marking snapshot testing as stable (Colin Ihrig) [#56425](https://github.com/nodejs/node/pull/56425)
* \[[`b7b0768cda`](https://github.com/nodejs/node/commit/b7b0768cda)] - **tls**: fix error stack conversion in cryptoErrorListToException() (Joyee Cheung) [#56554](https://github.com/nodejs/node/pull/56554)
* \[[`8f59f5ba47`](https://github.com/nodejs/node/commit/8f59f5ba47)] - **tools**: update doc to new version (Node.js GitHub Bot) [#56259](https://github.com/nodejs/node/pull/56259)
* \[[`ebf4527730`](https://github.com/nodejs/node/commit/ebf4527730)] - **tools**: update inspector\_protocol roller (Chengzhong Wu) [#56649](https://github.com/nodejs/node/pull/56649)
* \[[`649cf0c0f6`](https://github.com/nodejs/node/commit/649cf0c0f6)] - **tools**: do not throw on missing `create-release-proposal.sh` (Antoine du Hamel) [#56704](https://github.com/nodejs/node/pull/56704)
* \[[`69cb44e315`](https://github.com/nodejs/node/commit/69cb44e315)] - **tools**: fix tools-deps-update (Daniel Lemire) [#56684](https://github.com/nodejs/node/pull/56684)
* \[[`02f36ca11b`](https://github.com/nodejs/node/commit/02f36ca11b)] - **tools**: do not throw on missing `create-release-proposal.sh` (Antoine du Hamel) [#56695](https://github.com/nodejs/node/pull/56695)
* \[[`bcc1c65066`](https://github.com/nodejs/node/commit/bcc1c65066)] - **tools**: fix permissions in `lint-release-proposal` workflow (Antoine du Hamel) [#56614](https://github.com/nodejs/node/pull/56614)
* \[[`ab4cfef600`](https://github.com/nodejs/node/commit/ab4cfef600)] - **tools**: remove github reporter (Carlos Espa) [#56468](https://github.com/nodejs/node/pull/56468)
* \[[`477e674a2a`](https://github.com/nodejs/node/commit/477e674a2a)] - **tools**: edit `create-release-proposal` workflow (Antoine du Hamel) [#56540](https://github.com/nodejs/node/pull/56540)
* \[[`5f6785b1cb`](https://github.com/nodejs/node/commit/5f6785b1cb)] - **tools**: validate commit list as part of `lint-release-commit` (Antoine du Hamel) [#56291](https://github.com/nodejs/node/pull/56291)
* \[[`2a0fbd8731`](https://github.com/nodejs/node/commit/2a0fbd8731)] - **tools**: fix loong64 build failed (Xiao-Tao) [#56466](https://github.com/nodejs/node/pull/56466)
* \[[`aea088f79e`](https://github.com/nodejs/node/commit/aea088f79e)] - **tools**: disable unneeded rule ignoring in Python linting (Rich Trott) [#56429](https://github.com/nodejs/node/pull/56429)
* \[[`7a0dd2d04f`](https://github.com/nodejs/node/commit/7a0dd2d04f)] - **tools**: use a configurable value for number of open dependabot PRs (Antoine du Hamel) [#56427](https://github.com/nodejs/node/pull/56427)
* \[[`c249c9715a`](https://github.com/nodejs/node/commit/c249c9715a)] - **tools**: bump the eslint group in /tools/eslint with 4 updates (dependabot\[bot]) [#56426](https://github.com/nodejs/node/pull/56426)
* \[[`a9d332a16f`](https://github.com/nodejs/node/commit/a9d332a16f)] - **util**: inspect: do not crash on an Error stack that contains a Symbol (Jordan Harband) [#56573](https://github.com/nodejs/node/pull/56573)
* \[[`6a16012fd7`](https://github.com/nodejs/node/commit/6a16012fd7)] - **util**: inspect: do not crash on an Error with a regex `name` (Jordan Harband) [#56574](https://github.com/nodejs/node/pull/56574)
* \[[`c7f16192f4`](https://github.com/nodejs/node/commit/c7f16192f4)] - **util**: rename CallSite.column to columnNumber (Chengzhong Wu) [#56584](https://github.com/nodejs/node/pull/56584)
* \[[`e652781934`](https://github.com/nodejs/node/commit/e652781934)] - **util**: do not crash on inspecting function with `Symbol` name (Jordan Harband) [#56572](https://github.com/nodejs/node/pull/56572)
* \[[`d066acfcf9`](https://github.com/nodejs/node/commit/d066acfcf9)] - **util**: expose CallSite.scriptId (Chengzhong Wu) [#56551](https://github.com/nodejs/node/pull/56551)
* \[[`e1b0f44d19`](https://github.com/nodejs/node/commit/e1b0f44d19)] - **watch**: reload env file for --env-file-if-exists (Jonas) [#56643](https://github.com/nodejs/node/pull/56643)
* \[[`538e19489f`](https://github.com/nodejs/node/commit/538e19489f)] - **worker**: refactor stdio to improve performance (Matteo Collina) [#56630](https://github.com/nodejs/node/pull/56630)
* \[[`aab53e6965`](https://github.com/nodejs/node/commit/aab53e6965)] - **worker**: flush stdout and stderr on exit (Matteo Collina) [#56428](https://github.com/nodejs/node/pull/56428)

<a id="23.6.1"></a>

## 2025-01-21, Version 23.6.1 (Current), @RafaelGSS

This is a security release.

### Notable Changes

* CVE-2025-23083 - src,loader,permission: throw on InternalWorker use when permission model is enabled (High)
* CVE-2025-23085 - src: fix HTTP2 mem leak on premature close and ERR\_PROTO (Medium)
* CVE-2025-23084 - path: fix path traversal in normalize() on Windows (Medium)

Dependency update:

* CVE-2025-22150 - Use of Insufficiently Random Values in undici fetch() (Medium)

### Commits

* \[[`f2ad4d3af8`](https://github.com/nodejs/node/commit/f2ad4d3af8)] - **(CVE-2025-22150)** **deps**: update undici to v6.21.1 (Matteo Collina) [nodejs-private/node-private#654](https://github.com/nodejs-private/node-private/pull/654)
* \[[`0afc6f9600`](https://github.com/nodejs/node/commit/0afc6f9600)] - **(CVE-2025-23084)** **path**: fix path traversal in normalize() on Windows (RafaelGSS) [nodejs-private/node-private#555](https://github.com/nodejs-private/node-private/pull/555)
* \[[`3c7686163e`](https://github.com/nodejs/node/commit/3c7686163e)] - **(CVE-2025-23085)** **src**: fix HTTP2 mem leak on premature close and ERR\_PROTO (RafaelGSS) [nodejs-private/node-private#650](https://github.com/nodejs-private/node-private/pull/650)
* \[[`51938f023a`](https://github.com/nodejs/node/commit/51938f023a)] - **(CVE-2025-23083)** **src,loader,permission**: throw on InternalWorker use (RafaelGSS) [nodejs-private/node-private#629](https://github.com/nodejs-private/node-private/pull/629)

<a id="23.6.0"></a>

## 2025-01-07, Version 23.6.0 (Current), @marco-ippolito

### Notable Changes

#### Unflagging --experimental-strip-types

This release enables the flag `--experimental-strip-types` by default.
Node.js will be able to execute TypeScript files without additional configuration:

```bash
node file.ts
```

There are some limitations in the supported syntax documented at <https://nodejs.org/api/typescript.html#type-stripping>
This feature is experimental and is subject to change.

Contributed by Marco Ippolito in [#56350](https://github.com/nodejs/node/pull/56350)

### Other Notable Changes

* \[[`c1023284c3`](https://github.com/nodejs/node/commit/c1023284c3)] - **(SEMVER-MINOR)** **lib**: add typescript support to STDIN eval (Marco Ippolito) [#56359](https://github.com/nodejs/node/pull/56359)
* \[[`8dc39e5e2e`](https://github.com/nodejs/node/commit/8dc39e5e2e)] - **(SEMVER-MINOR)** **process**: add process.ref() and process.unref() methods (James M Snell) [#56400](https://github.com/nodejs/node/pull/56400)
* \[[`8b20cc212b`](https://github.com/nodejs/node/commit/8b20cc212b)] - **(SEMVER-MINOR)** **worker**: add eval ts input (Marco Ippolito) [#56394](https://github.com/nodejs/node/pull/56394)

### Commits

* \[[`7b4d288116`](https://github.com/nodejs/node/commit/7b4d288116)] - **assert**: make partialDeepStrictEqual throw when comparing \[0] with \[-0] (Giovanni) [#56237](https://github.com/nodejs/node/pull/56237)
* \[[`0ec2ed0a0b`](https://github.com/nodejs/node/commit/0ec2ed0a0b)] - **build**: fix GN build for ngtcp2 (Cheng) [#56300](https://github.com/nodejs/node/pull/56300)
* \[[`ab3e64630b`](https://github.com/nodejs/node/commit/ab3e64630b)] - **build**: test macos-13 on GitHub actions (Michaël Zasso) [#56307](https://github.com/nodejs/node/pull/56307)
* \[[`46fb69daca`](https://github.com/nodejs/node/commit/46fb69daca)] - **build**: build v8 with -fvisibility=hidden on macOS (Joyee Cheung) [#56275](https://github.com/nodejs/node/pull/56275)
* \[[`9d4930b993`](https://github.com/nodejs/node/commit/9d4930b993)] - **deps**: update simdutf to 5.7.2 (Node.js GitHub Bot) [#56388](https://github.com/nodejs/node/pull/56388)
* \[[`6afe36397e`](https://github.com/nodejs/node/commit/6afe36397e)] - **deps**: update amaro to 0.2.1 (Node.js GitHub Bot) [#56390](https://github.com/nodejs/node/pull/56390)
* \[[`195990a0ee`](https://github.com/nodejs/node/commit/195990a0ee)] - **deps**: update googletest to 7d76a23 (Node.js GitHub Bot) [#56387](https://github.com/nodejs/node/pull/56387)
* \[[`b9c0852fc6`](https://github.com/nodejs/node/commit/b9c0852fc6)] - **deps**: update googletest to e54519b (Node.js GitHub Bot) [#56370](https://github.com/nodejs/node/pull/56370)
* \[[`eaefd90128`](https://github.com/nodejs/node/commit/eaefd90128)] - **deps**: update ngtcp2 to 1.10.0 (Node.js GitHub Bot) [#56334](https://github.com/nodejs/node/pull/56334)
* \[[`06de0c65cf`](https://github.com/nodejs/node/commit/06de0c65cf)] - **deps**: update simdutf to 5.7.0 (Node.js GitHub Bot) [#56332](https://github.com/nodejs/node/pull/56332)
* \[[`03df76cdec`](https://github.com/nodejs/node/commit/03df76cdec)] - **doc**: add example for piping ReadableStream (Gabriel Schulhof) [#56415](https://github.com/nodejs/node/pull/56415)
* \[[`38ce249b07`](https://github.com/nodejs/node/commit/38ce249b07)] - **doc**: expand description of `parseArg`'s `default` (Kevin Gibbons) [#54431](https://github.com/nodejs/node/pull/54431)
* \[[`ecc718cef2`](https://github.com/nodejs/node/commit/ecc718cef2)] - **doc**: use `<ul>` instead of `<ol>` in `SECURITY.md` (Antoine du Hamel) [#56346](https://github.com/nodejs/node/pull/56346)
* \[[`3db4809130`](https://github.com/nodejs/node/commit/3db4809130)] - **doc**: clarify that WASM is trusted (Matteo Collina) [#56345](https://github.com/nodejs/node/pull/56345)
* \[[`384ccbacd5`](https://github.com/nodejs/node/commit/384ccbacd5)] - **doc**: update macOS and Xcode versions for releases (Michaël Zasso) [#56337](https://github.com/nodejs/node/pull/56337)
* \[[`3943986e88`](https://github.com/nodejs/node/commit/3943986e88)] - **doc**: fix the `crc32` documentation (Kevin Toshihiro Uehara) [#55898](https://github.com/nodejs/node/pull/55898)
* \[[`710b8fc6ed`](https://github.com/nodejs/node/commit/710b8fc6ed)] - **doc**: add entry to changelog about SQLite Session Extension (Bart Louwers) [#56318](https://github.com/nodejs/node/pull/56318)
* \[[`4c978b4d77`](https://github.com/nodejs/node/commit/4c978b4d77)] - **doc**: fix links in `module.md` (Antoine du Hamel) [#56283](https://github.com/nodejs/node/pull/56283)
* \[[`cdb631efe7`](https://github.com/nodejs/node/commit/cdb631efe7)] - **esm**: add experimental support for addon modules (Chengzhong Wu) [#55844](https://github.com/nodejs/node/pull/55844)
* \[[`db83d2f0ee`](https://github.com/nodejs/node/commit/db83d2f0ee)] - _**Revert**_ "**events**: add hasEventListener util for validate" (origranot) [#56282](https://github.com/nodejs/node/pull/56282)
* \[[`c2baae84ce`](https://github.com/nodejs/node/commit/c2baae84ce)] - **lib**: refactor execution.js (Marco Ippolito) [#56358](https://github.com/nodejs/node/pull/56358)
* \[[`c1023284c3`](https://github.com/nodejs/node/commit/c1023284c3)] - **(SEMVER-MINOR)** **lib**: add typescript support to STDIN eval (Marco Ippolito) [#56359](https://github.com/nodejs/node/pull/56359)
* \[[`e4b795ec4a`](https://github.com/nodejs/node/commit/e4b795ec4a)] - **lib**: optimize `prepareStackTrace` on builtin frames (Chengzhong Wu) [#56299](https://github.com/nodejs/node/pull/56299)
* \[[`d1b009b623`](https://github.com/nodejs/node/commit/d1b009b623)] - **lib**: suppress source map lookup exceptions (Chengzhong Wu) [#56299](https://github.com/nodejs/node/pull/56299)
* \[[`c2837f0805`](https://github.com/nodejs/node/commit/c2837f0805)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#56342](https://github.com/nodejs/node/pull/56342)
* \[[`72336233f2`](https://github.com/nodejs/node/commit/72336233f2)] - **meta**: move MoLow to TSC regular member (Moshe Atlow) [#56276](https://github.com/nodejs/node/pull/56276)
* \[[`4f77920a9d`](https://github.com/nodejs/node/commit/4f77920a9d)] - **module**: fix async resolution error within the sync `findPackageJSON` (Jacob Smith) [#56382](https://github.com/nodejs/node/pull/56382)
* \[[`e5ba216501`](https://github.com/nodejs/node/commit/e5ba216501)] - **(SEMVER-MINOR)** **module**: unflag --experimental-strip-types (Marco Ippolito) [#56350](https://github.com/nodejs/node/pull/56350)
* \[[`959f133a22`](https://github.com/nodejs/node/commit/959f133a22)] - **module**: support eval with ts syntax detection (Marco Ippolito) [#56285](https://github.com/nodejs/node/pull/56285)
* \[[`717cfa4fac`](https://github.com/nodejs/node/commit/717cfa4fac)] - **module**: use buffer.toString base64 (Chengzhong Wu) [#56315](https://github.com/nodejs/node/pull/56315)
* \[[`c2f4d8d688`](https://github.com/nodejs/node/commit/c2f4d8d688)] - **node-api**: define version 10 (Gabriel Schulhof) [#55676](https://github.com/nodejs/node/pull/55676)
* \[[`417a8ebdec`](https://github.com/nodejs/node/commit/417a8ebdec)] - **node-api**: remove deprecated attribute from napi\_module\_register (Vladimir Morozov) [#56162](https://github.com/nodejs/node/pull/56162)
* \[[`8dc39e5e2e`](https://github.com/nodejs/node/commit/8dc39e5e2e)] - **(SEMVER-MINOR)** **process**: add process.ref() and process.unref() methods (James M Snell) [#56400](https://github.com/nodejs/node/pull/56400)
* \[[`d194f1ab5f`](https://github.com/nodejs/node/commit/d194f1ab5f)] - **sqlite**: pass conflict type to conflict resolution handler (Bart Louwers) [#56352](https://github.com/nodejs/node/pull/56352)
* \[[`29f5d70452`](https://github.com/nodejs/node/commit/29f5d70452)] - **src**: use v8::LocalVector consistently with other minor cleanups (James M Snell) [#56417](https://github.com/nodejs/node/pull/56417)
* \[[`2a5543b78e`](https://github.com/nodejs/node/commit/2a5543b78e)] - **src**: use starts\_with in fs\_permission.cc (ishabi) [#55811](https://github.com/nodejs/node/pull/55811)
* \[[`3a3f5c9a64`](https://github.com/nodejs/node/commit/3a3f5c9a64)] - **stream**: validate undefined sizeAlgorithm in WritableStream (Jason Zhang) [#56067](https://github.com/nodejs/node/pull/56067)
* \[[`6e6f6b071a`](https://github.com/nodejs/node/commit/6e6f6b071a)] - **test**: add ts eval snapshots (Marco Ippolito) [#56358](https://github.com/nodejs/node/pull/56358)
* \[[`8a87e39052`](https://github.com/nodejs/node/commit/8a87e39052)] - **test**: remove empty lines from snapshots (Marco Ippolito) [#56358](https://github.com/nodejs/node/pull/56358)
* \[[`510649f617`](https://github.com/nodejs/node/commit/510649f617)] - **test**: use unusual chars in the path to ensure our tests are robust (Antoine du Hamel) [#48409](https://github.com/nodejs/node/pull/48409)
* \[[`54f6d681a0`](https://github.com/nodejs/node/commit/54f6d681a0)] - **test**: remove flaky designation (Luigi Pinca) [#56369](https://github.com/nodejs/node/pull/56369)
* \[[`20ace0bb01`](https://github.com/nodejs/node/commit/20ace0bb01)] - **test**: remove test-worker-arraybuffer-zerofill flaky designation (Luigi Pinca) [#56364](https://github.com/nodejs/node/pull/56364)
* \[[`b757e40525`](https://github.com/nodejs/node/commit/b757e40525)] - **test**: remove test-net-write-fully-async-hex-string flaky designation (Luigi Pinca) [#56365](https://github.com/nodejs/node/pull/56365)
* \[[`64556baddc`](https://github.com/nodejs/node/commit/64556baddc)] - **test**: improve abort signal dropping test (Edy Silva) [#56339](https://github.com/nodejs/node/pull/56339)
* \[[`accbdad329`](https://github.com/nodejs/node/commit/accbdad329)] - **test**: enable ts test on win arm64 (Marco Ippolito) [#56349](https://github.com/nodejs/node/pull/56349)
* \[[`4188ee00d1`](https://github.com/nodejs/node/commit/4188ee00d1)] - **test**: deflake test-watch-file-shared-dependency (Luigi Pinca) [#56344](https://github.com/nodejs/node/pull/56344)
* \[[`079cee0609`](https://github.com/nodejs/node/commit/079cee0609)] - **test**: skip `test-sqlite-extensions` when SQLite is not built by us (Antoine du Hamel) [#56341](https://github.com/nodejs/node/pull/56341)
* \[[`96a38044ee`](https://github.com/nodejs/node/commit/96a38044ee)] - **test**: increase spin for eventloop test on s390 (Michael Dawson) [#56228](https://github.com/nodejs/node/pull/56228)
* \[[`c062ffc242`](https://github.com/nodejs/node/commit/c062ffc242)] - **test**: add coverage for pipeline (jakecastelli) [#56278](https://github.com/nodejs/node/pull/56278)
* \[[`d4404f0d0e`](https://github.com/nodejs/node/commit/d4404f0d0e)] - **test**: migrate message eval tests from Python to JS (Yiyun Lei) [#50482](https://github.com/nodejs/node/pull/50482)
* \[[`9369942745`](https://github.com/nodejs/node/commit/9369942745)] - **test**: check typescript loader (Marco Ippolito) [#54657](https://github.com/nodejs/node/pull/54657)
* \[[`4930244484`](https://github.com/nodejs/node/commit/4930244484)] - **test**: remove async-hooks/test-writewrap flaky designation (Luigi Pinca) [#56048](https://github.com/nodejs/node/pull/56048)
* \[[`7819bfec69`](https://github.com/nodejs/node/commit/7819bfec69)] - **test**: deflake test-esm-loader-hooks-inspect-brk (Luigi Pinca) [#56050](https://github.com/nodejs/node/pull/56050)
* \[[`e9762bf005`](https://github.com/nodejs/node/commit/e9762bf005)] - **test**: add test case for listeners (origranot) [#56282](https://github.com/nodejs/node/pull/56282)
* \[[`c1627e9d19`](https://github.com/nodejs/node/commit/c1627e9d19)] - **test**: make `test-permission-sqlite-load-extension` more robust (Antoine du Hamel) [#56295](https://github.com/nodejs/node/pull/56295)
* \[[`97d854e1d5`](https://github.com/nodejs/node/commit/97d854e1d5)] - **test\_runner,cli**: mark test isolation as stable (Colin Ihrig) [#56298](https://github.com/nodejs/node/pull/56298)
* \[[`a4f336fdd4`](https://github.com/nodejs/node/commit/a4f336fdd4)] - **tools**: fix `require-common-first` lint rule from subfolder (Antoine du Hamel) [#56325](https://github.com/nodejs/node/pull/56325)
* \[[`dc3dafcb50`](https://github.com/nodejs/node/commit/dc3dafcb50)] - **tools**: add release line label when opening release proposal (Antoine du Hamel) [#56317](https://github.com/nodejs/node/pull/56317)
* \[[`2a5ac932ac`](https://github.com/nodejs/node/commit/2a5ac932ac)] - **url**: use resolved path to convert UNC paths to URL (Antoine du Hamel) [#56302](https://github.com/nodejs/node/pull/56302)
* \[[`8b20cc212b`](https://github.com/nodejs/node/commit/8b20cc212b)] - **(SEMVER-MINOR)** **worker**: add eval ts input (Marco Ippolito) [#56394](https://github.com/nodejs/node/pull/56394)

<a id="23.5.0"></a>

## 2024-12-19, Version 23.5.0 (Current), @aduh95

### Notable Changes

#### WebCryptoAPI Ed25519 and X25519 algorithms are now stable

Following the merge of Curve25519 into the
[Web Cryptography API Editor's Draft](https://w3c.github.io/webcrypto/) the
`Ed25519` and `X25519` algorithm identifiers are now stable and will no longer
emit an ExperimentalWarning upon use.

Contributed by Filip Skokan in [#56142](https://github.com/nodejs/node/pull/56142).

#### On-thread hooks are back

This release introduces `module.registerHooks()` for registering module loader
customization hooks that are run for all modules loaded by `require()`, `import`
and functions returned by `createRequire()` in the same thread, which makes them
easier for CJS monkey-patchers to migrate to.

```mjs
import assert from 'node:assert';
import { registerHooks, createRequire } from 'node:module';
import { writeFileSync } from 'node:fs';

writeFileSync('./bar.js', 'export const id = 123;', 'utf8');

registerHooks({
  resolve(specifier, context, nextResolve) {
    const replaced = specifier.replace('foo', 'bar');
    return nextResolve(replaced, context);
  },
  load(url, context, nextLoad) {
    const result = nextLoad(url, context);
    return {
      ...result,
      source: result.source.toString().replace('123', '456'),
    };
  },
});

// Checks that it works with require.
const require = createRequire(import.meta.url);
const required = require('./foo.js');  // Redirected by resolve hook to bar.js
assert.strictEqual(required.id, 456);  // Replaced by load hook to 456

// Checks that it works with import.
const imported = await import('./foo.js');  // Redirected by resolve hook to bar.js
assert.strictEqual(imported.id, 456);  // Replaced by load hook to 456
```

This complements the `module.register()` hooks - the new hooks fit better
internally and cover all corners in the module graph; whereas
`module.register()` previously could not cover `require()` while it was
on-thread, and still cannot cover `createRequire()` after being moved
off-thread.

They are also run in the same thread as the modules being loaded and where the
hooks are registered, which means they are easier to debug (no more
`console.log()` getting lost) and do not have the many deadlock issues haunting
the `module.register()` hooks. The new API also takes functions directly so that
it's easier for intermediate loader packages to take user options from files
that the hooks can't be aware of, like many existing CJS monkey-patchers do.

Contributed by Joyee Cheung in [#55698](https://github.com/nodejs/node/pull/55698).

#### Other notable changes

* \[[`59cae91465`](https://github.com/nodejs/node/commit/59cae91465)] - **(SEMVER-MINOR)** **dgram**: support blocklist in udp (theanarkh) [#56087](https://github.com/nodejs/node/pull/56087)
* \[[`72f79b44ed`](https://github.com/nodejs/node/commit/72f79b44ed)] - **doc**: stabilize util.styleText (Rafael Gonzaga) [#56265](https://github.com/nodejs/node/pull/56265)
* \[[`b5a2c0777d`](https://github.com/nodejs/node/commit/b5a2c0777d)] - **(SEMVER-MINOR)** **module**: add prefix-only modules to `module.builtinModules` (Jordan Harband) [#56185](https://github.com/nodejs/node/pull/56185)
* \[[`9863d27566`](https://github.com/nodejs/node/commit/9863d27566)] - **(SEMVER-MINOR)** **module**: only emit require(esm) warning under --trace-require-module (Joyee Cheung) [#56194](https://github.com/nodejs/node/pull/56194)
* \[[`8e780bc5ae`](https://github.com/nodejs/node/commit/8e780bc5ae)] - **(SEMVER-MINOR)** **module**: use synchronous hooks for preparsing in import(cjs) (Joyee Cheung) [#55698](https://github.com/nodejs/node/pull/55698)
* \[[`65bc8e847f`](https://github.com/nodejs/node/commit/65bc8e847f)] - **(SEMVER-MINOR)** **report**: fix typos in report keys and bump the version (Yuan-Ming Hsu) [#56068](https://github.com/nodejs/node/pull/56068)
* \[[`0ab36e1937`](https://github.com/nodejs/node/commit/0ab36e1937)] - **(SEMVER-MINOR)** **sqlite**: aggregate constants in a single property (Edigleysson Silva (Edy)) [#56213](https://github.com/nodejs/node/pull/56213)
* \[[`efcc5d90c5`](https://github.com/nodejs/node/commit/efcc5d90c5)] - **(SEMVER-MINOR)** **src,lib**: stabilize permission model (Rafael Gonzaga) [#56201](https://github.com/nodejs/node/pull/56201)

### Commits

* \[[`2314e4916e`](https://github.com/nodejs/node/commit/2314e4916e)] - **assert**: make Maps be partially compared in partialDeepStrictEqual (Giovanni Bucci) [#56195](https://github.com/nodejs/node/pull/56195)
* \[[`cfbdff7b45`](https://github.com/nodejs/node/commit/cfbdff7b45)] - **assert**: make partialDeepStrictEqual work with ArrayBuffers (Giovanni Bucci) [#56098](https://github.com/nodejs/node/pull/56098)
* \[[`f264dd6d20`](https://github.com/nodejs/node/commit/f264dd6d20)] - **buffer**: document concat zero-fill (Duncan) [#55562](https://github.com/nodejs/node/pull/55562)
* \[[`4831b87d83`](https://github.com/nodejs/node/commit/4831b87d83)] - **build**: set DESTCPU correctly for 'make binary' on loongarch64 (吴小白) [#56271](https://github.com/nodejs/node/pull/56271)
* \[[`1497bb405e`](https://github.com/nodejs/node/commit/1497bb405e)] - **build**: fix missing fp16 dependency in d8 builds (Joyee Cheung) [#56266](https://github.com/nodejs/node/pull/56266)
* \[[`445c8c7489`](https://github.com/nodejs/node/commit/445c8c7489)] - **build**: add major release action (Rafael Gonzaga) [#56199](https://github.com/nodejs/node/pull/56199)
* \[[`f4faedfa69`](https://github.com/nodejs/node/commit/f4faedfa69)] - **build**: fix C string encoding for `PRODUCT_DIR_ABS` (Anna Henningsen) [#56111](https://github.com/nodejs/node/pull/56111)
* \[[`6f49c8006c`](https://github.com/nodejs/node/commit/6f49c8006c)] - **build**: use variable for simdutf path (Shelley Vohr) [#56196](https://github.com/nodejs/node/pull/56196)
* \[[`fcaa2c82a6`](https://github.com/nodejs/node/commit/fcaa2c82a6)] - **build**: fix GN build on macOS (Joyee Cheung) [#56141](https://github.com/nodejs/node/pull/56141)
* \[[`08e5309f4f`](https://github.com/nodejs/node/commit/08e5309f4f)] - _**Revert**_ "**build**: avoid compiling with VS v17.12" (Gerhard Stöbich) [#56151](https://github.com/nodejs/node/pull/56151)
* \[[`c2fb38cfdf`](https://github.com/nodejs/node/commit/c2fb38cfdf)] - **crypto**: graduate WebCryptoAPI Ed25519 and X25519 algorithms as stable (Filip Skokan) [#56142](https://github.com/nodejs/node/pull/56142)
* \[[`8658833884`](https://github.com/nodejs/node/commit/8658833884)] - **deps**: update nghttp3 to 1.6.0 (Node.js GitHub Bot) [#56258](https://github.com/nodejs/node/pull/56258)
* \[[`7c941d4610`](https://github.com/nodejs/node/commit/7c941d4610)] - **deps**: update simdutf to 5.6.4 (Node.js GitHub Bot) [#56255](https://github.com/nodejs/node/pull/56255)
* \[[`4e9113eada`](https://github.com/nodejs/node/commit/4e9113eada)] - **deps**: update libuv to 1.49.2 (Luigi Pinca) [#56224](https://github.com/nodejs/node/pull/56224)
* \[[`db6aba12e4`](https://github.com/nodejs/node/commit/db6aba12e4)] - **deps**: update c-ares to v1.34.4 (Node.js GitHub Bot) [#56256](https://github.com/nodejs/node/pull/56256)
* \[[`25bb462bc2`](https://github.com/nodejs/node/commit/25bb462bc2)] - **deps**: define V8\_PRESERVE\_MOST as no-op on Windows (Stefan Stojanovic) [#56238](https://github.com/nodejs/node/pull/56238)
* \[[`54308c51bb`](https://github.com/nodejs/node/commit/54308c51bb)] - **deps**: update sqlite to 3.47.2 (Node.js GitHub Bot) [#56178](https://github.com/nodejs/node/pull/56178)
* \[[`59cae91465`](https://github.com/nodejs/node/commit/59cae91465)] - **(SEMVER-MINOR)** **dgram**: support blocklist in udp (theanarkh) [#56087](https://github.com/nodejs/node/pull/56087)
* \[[`52c18e605e`](https://github.com/nodejs/node/commit/52c18e605e)] - **doc**: fix color contrast issue in light mode (Rich Trott) [#56272](https://github.com/nodejs/node/pull/56272)
* \[[`72f79b44ed`](https://github.com/nodejs/node/commit/72f79b44ed)] - **doc**: stabilize util.styleText (Rafael Gonzaga) [#56265](https://github.com/nodejs/node/pull/56265)
* \[[`0d08756d0c`](https://github.com/nodejs/node/commit/0d08756d0c)] - **doc**: clarify util.aborted resource usage (Kunal Kumar) [#55780](https://github.com/nodejs/node/pull/55780)
* \[[`f94f21080b`](https://github.com/nodejs/node/commit/f94f21080b)] - **doc**: add esm examples to node:repl (Alfredo González) [#55432](https://github.com/nodejs/node/pull/55432)
* \[[`7a10ef88d9`](https://github.com/nodejs/node/commit/7a10ef88d9)] - **doc**: add esm examples to node:readline (Alfredo González) [#55335](https://github.com/nodejs/node/pull/55335)
* \[[`cc7a7c391b`](https://github.com/nodejs/node/commit/cc7a7c391b)] - **doc**: fix 'which' to 'that' and add commas (Selveter Senitro) [#56216](https://github.com/nodejs/node/pull/56216)
* \[[`c5b086250e`](https://github.com/nodejs/node/commit/c5b086250e)] - **doc**: fix winget config path (Alex Yang) [#56233](https://github.com/nodejs/node/pull/56233)
* \[[`71c38a24d4`](https://github.com/nodejs/node/commit/71c38a24d4)] - **doc**: add esm examples to node:tls (Alfredo González) [#56229](https://github.com/nodejs/node/pull/56229)
* \[[`394fffbbde`](https://github.com/nodejs/node/commit/394fffbbde)] - **doc**: add esm examples to node:perf\_hooks (Alfredo González) [#55257](https://github.com/nodejs/node/pull/55257)
* \[[`7b2a6ee61e`](https://github.com/nodejs/node/commit/7b2a6ee61e)] - **doc**: `sea.getRawAsset(key)` always returns an ArrayBuffer (沈鸿飞) [#56206](https://github.com/nodejs/node/pull/56206)
* \[[`8092dcf27e`](https://github.com/nodejs/node/commit/8092dcf27e)] - **doc**: update announce documentation for releases (Rafael Gonzaga) [#56200](https://github.com/nodejs/node/pull/56200)
* \[[`2974667815`](https://github.com/nodejs/node/commit/2974667815)] - **doc**: update blog link to /vulnerability (Rafael Gonzaga) [#56198](https://github.com/nodejs/node/pull/56198)
* \[[`f3b3ff85e0`](https://github.com/nodejs/node/commit/f3b3ff85e0)] - **doc**: call out import.meta is only supported in ES modules (Anton Kastritskii) [#56186](https://github.com/nodejs/node/pull/56186)
* \[[`a9e67280e7`](https://github.com/nodejs/node/commit/a9e67280e7)] - **doc**: add ambassador message - benefits of Node.js (Michael Dawson) [#56085](https://github.com/nodejs/node/pull/56085)
* \[[`e4922ab15f`](https://github.com/nodejs/node/commit/e4922ab15f)] - **doc**: fix incorrect link to style guide (Yuan-Ming Hsu) [#56181](https://github.com/nodejs/node/pull/56181)
* \[[`114a3e5a05`](https://github.com/nodejs/node/commit/114a3e5a05)] - **doc**: fix c++ addon hello world sample (Edigleysson Silva (Edy)) [#56172](https://github.com/nodejs/node/pull/56172)
* \[[`f1c2d2f65e`](https://github.com/nodejs/node/commit/f1c2d2f65e)] - **doc**: update blog release-post link (Ruy Adorno) [#56123](https://github.com/nodejs/node/pull/56123)
* \[[`d48b5224c0`](https://github.com/nodejs/node/commit/d48b5224c0)] - **doc**: fix module.md headings (Chengzhong Wu) [#56131](https://github.com/nodejs/node/pull/56131)
* \[[`4cc0493a0b`](https://github.com/nodejs/node/commit/4cc0493a0b)] - **fs**: make mutating `options` in Callback `readdir()` not affect results (LiviaMedeiros) [#56057](https://github.com/nodejs/node/pull/56057)
* \[[`8d485f1c09`](https://github.com/nodejs/node/commit/8d485f1c09)] - **fs**: make mutating `options` in Promises `readdir()` not affect results (LiviaMedeiros) [#56057](https://github.com/nodejs/node/pull/56057)
* \[[`595851b5ed`](https://github.com/nodejs/node/commit/595851b5ed)] - **fs,win**: fix readdir for named pipe (Hüseyin Açacak) [#56110](https://github.com/nodejs/node/pull/56110)
* \[[`075b36b7b4`](https://github.com/nodejs/node/commit/075b36b7b4)] - **http**: add setDefaultHeaders option to http.request (Tim Perry) [#56112](https://github.com/nodejs/node/pull/56112)
* \[[`febd969c46`](https://github.com/nodejs/node/commit/febd969c46)] - **http2**: remove duplicate codeblock (Vitaly Aminev) [#55915](https://github.com/nodejs/node/pull/55915)
* \[[`b0ebd23e52`](https://github.com/nodejs/node/commit/b0ebd23e52)] - **http2**: support ALPNCallback option (ZYSzys) [#56187](https://github.com/nodejs/node/pull/56187)
* \[[`f10239fde7`](https://github.com/nodejs/node/commit/f10239fde7)] - **lib**: remove redundant global regexps (Gürgün Dayıoğlu) [#56182](https://github.com/nodejs/node/pull/56182)
* \[[`fd55d3cbdd`](https://github.com/nodejs/node/commit/fd55d3cbdd)] - **lib**: clean up persisted signals when they are settled (Edigleysson Silva (Edy)) [#56001](https://github.com/nodejs/node/pull/56001)
* \[[`889094fdbc`](https://github.com/nodejs/node/commit/889094fdbc)] - **lib**: handle Float16Array in node:v8 serdes (Bartek Iwańczuk) [#55996](https://github.com/nodejs/node/pull/55996)
* \[[`5aec513207`](https://github.com/nodejs/node/commit/5aec513207)] - **lib**: disable default memory leak warning for AbortSignal (Lenz Weber-Tronic) [#55816](https://github.com/nodejs/node/pull/55816)
* \[[`b5a2c0777d`](https://github.com/nodejs/node/commit/b5a2c0777d)] - **(SEMVER-MINOR)** **module**: add prefix-only modules to `module.builtinModules` (Jordan Harband) [#56185](https://github.com/nodejs/node/pull/56185)
* \[[`9863d27566`](https://github.com/nodejs/node/commit/9863d27566)] - **(SEMVER-MINOR)** **module**: only emit require(esm) warning under --trace-require-module (Joyee Cheung) [#56194](https://github.com/nodejs/node/pull/56194)
* \[[`5665e86da6`](https://github.com/nodejs/node/commit/5665e86da6)] - **module**: prevent main thread exiting before esm worker ends (Shima Ryuhei) [#56183](https://github.com/nodejs/node/pull/56183)
* \[[`8e780bc5ae`](https://github.com/nodejs/node/commit/8e780bc5ae)] - **(SEMVER-MINOR)** **module**: use synchronous hooks for preparsing in import(cjs) (Joyee Cheung) [#55698](https://github.com/nodejs/node/pull/55698)
* \[[`e5bb6c2303`](https://github.com/nodejs/node/commit/e5bb6c2303)] - **(SEMVER-MINOR)** **module**: implement module.registerHooks() (Joyee Cheung) [#55698](https://github.com/nodejs/node/pull/55698)
* \[[`f883bedceb`](https://github.com/nodejs/node/commit/f883bedceb)] - **node-api**: allow napi\_delete\_reference in finalizers (Chengzhong Wu) [#55620](https://github.com/nodejs/node/pull/55620)
* \[[`65bc8e847f`](https://github.com/nodejs/node/commit/65bc8e847f)] - **(SEMVER-MINOR)** **report**: fix typos in report keys and bump the version (Yuan-Ming Hsu) [#56068](https://github.com/nodejs/node/pull/56068)
* \[[`a6f0cfa468`](https://github.com/nodejs/node/commit/a6f0cfa468)] - **sea**: only assert snapshot main function for main threads (Joyee Cheung) [#56120](https://github.com/nodejs/node/pull/56120)
* \[[`0ab36e1937`](https://github.com/nodejs/node/commit/0ab36e1937)] - **(SEMVER-MINOR)** **sqlite**: aggregate constants in a single property (Edigleysson Silva (Edy)) [#56213](https://github.com/nodejs/node/pull/56213)
* \[[`4745798225`](https://github.com/nodejs/node/commit/4745798225)] - **sqlite**: add support for custom functions (Colin Ihrig) [#55985](https://github.com/nodejs/node/pull/55985)
* \[[`53cc0cc744`](https://github.com/nodejs/node/commit/53cc0cc744)] - **sqlite**: support `db.loadExtension` (Alex Yang) [#53900](https://github.com/nodejs/node/pull/53900)
* \[[`3968599702`](https://github.com/nodejs/node/commit/3968599702)] - **src**: fix outdated js2c.cc references (Chengzhong Wu) [#56133](https://github.com/nodejs/node/pull/56133)
* \[[`efcc5d90c5`](https://github.com/nodejs/node/commit/efcc5d90c5)] - **(SEMVER-MINOR)** **src,lib**: stabilize permission model (Rafael Gonzaga) [#56201](https://github.com/nodejs/node/pull/56201)
* \[[`a4a83613cb`](https://github.com/nodejs/node/commit/a4a83613cb)] - **stream**: commit pull-into descriptors after filling from queue (Mattias Buelens) [#56072](https://github.com/nodejs/node/pull/56072)
* \[[`3298ef4891`](https://github.com/nodejs/node/commit/3298ef4891)] - **test**: remove test-sqlite-statement-sync flaky designation (Luigi Pinca) [#56051](https://github.com/nodejs/node/pull/56051)
* \[[`1d8cc6179d`](https://github.com/nodejs/node/commit/1d8cc6179d)] - **test**: use --permission over --experimental-permission (Rafael Gonzaga) [#56239](https://github.com/nodejs/node/pull/56239)
* \[[`5d252b7a67`](https://github.com/nodejs/node/commit/5d252b7a67)] - **test**: remove exludes for sea tests on PPC (Michael Dawson) [#56217](https://github.com/nodejs/node/pull/56217)
* \[[`8288f57724`](https://github.com/nodejs/node/commit/8288f57724)] - **test**: fix test-abortsignal-drop-settled-signals flakiness (Edigleysson Silva (Edy)) [#56197](https://github.com/nodejs/node/pull/56197)
* \[[`683cc15796`](https://github.com/nodejs/node/commit/683cc15796)] - **test**: move localizationd data from `test-icu-env` to external file (Livia Medeiros) [#55618](https://github.com/nodejs/node/pull/55618)
* \[[`a0c4a5f122`](https://github.com/nodejs/node/commit/a0c4a5f122)] - **test**: update WPT for url to 6fa3fe8a92 (Node.js GitHub Bot) [#56136](https://github.com/nodejs/node/pull/56136)
* \[[`a0e3926285`](https://github.com/nodejs/node/commit/a0e3926285)] - **test**: remove `hasOpenSSL3x` utils (Antoine du Hamel) [#56164](https://github.com/nodejs/node/pull/56164)
* \[[`041a49094e`](https://github.com/nodejs/node/commit/041a49094e)] - **test**: update streams wpt (Mattias Buelens) [#56072](https://github.com/nodejs/node/pull/56072)
* \[[`ea9a675f56`](https://github.com/nodejs/node/commit/ea9a675f56)] - **test\_runner**: exclude test files from coverage by default (Pietro Marchini) [#56060](https://github.com/nodejs/node/pull/56060)
* \[[`118cd9998f`](https://github.com/nodejs/node/commit/118cd9998f)] - **tools**: fix `node:` enforcement for docs (Antoine du Hamel) [#56284](https://github.com/nodejs/node/pull/56284)
* \[[`c4c56daae8`](https://github.com/nodejs/node/commit/c4c56daae8)] - **tools**: update github\_reporter to 1.7.2 (Node.js GitHub Bot) [#56205](https://github.com/nodejs/node/pull/56205)
* \[[`78743b1533`](https://github.com/nodejs/node/commit/78743b1533)] - **tools**: add REPLACEME check to workflow (Mert Can Altin) [#56251](https://github.com/nodejs/node/pull/56251)
* \[[`002ee71d9b`](https://github.com/nodejs/node/commit/002ee71d9b)] - **tools**: use `github.actor` instead of bot username for release proposals (Antoine du Hamel) [#56232](https://github.com/nodejs/node/pull/56232)
* \[[`d25d16efeb`](https://github.com/nodejs/node/commit/d25d16efeb)] - _**Revert**_ "**tools**: disable automated libuv updates" (Luigi Pinca) [#56223](https://github.com/nodejs/node/pull/56223)
* \[[`b395e0c8c9`](https://github.com/nodejs/node/commit/b395e0c8c9)] - **tools**: update gyp-next to 0.19.1 (Anna Henningsen) [#56111](https://github.com/nodejs/node/pull/56111)
* \[[`a5aaf31c50`](https://github.com/nodejs/node/commit/a5aaf31c50)] - **tools**: fix release proposal linter to support more than 1 folk preparing (Antoine du Hamel) [#56203](https://github.com/nodejs/node/pull/56203)
* \[[`fa667d609e`](https://github.com/nodejs/node/commit/fa667d609e)] - **tools**: remove has\_absl\_stringify from gyp file (Michaël Zasso) [#56157](https://github.com/nodejs/node/pull/56157)
* \[[`65b541e70e`](https://github.com/nodejs/node/commit/65b541e70e)] - **tools**: enable linter for `tools/icu/**` (Livia Medeiros) [#56176](https://github.com/nodejs/node/pull/56176)
* \[[`28a4b6ff58`](https://github.com/nodejs/node/commit/28a4b6ff58)] - **tools**: use commit title as PR title when creating release proposal (Antoine du Hamel) [#56165](https://github.com/nodejs/node/pull/56165)
* \[[`e20eef659f`](https://github.com/nodejs/node/commit/e20eef659f)] - **tools**: update gyp-next to 0.19.0 (Node.js GitHub Bot) [#56158](https://github.com/nodejs/node/pull/56158)
* \[[`efcc829085`](https://github.com/nodejs/node/commit/efcc829085)] - **tools**: bump the eslint group in /tools/eslint with 4 updates (dependabot\[bot]) [#56099](https://github.com/nodejs/node/pull/56099)
* \[[`5620b2be8a`](https://github.com/nodejs/node/commit/5620b2be8a)] - **tools**: improve release proposal PR opening (Antoine du Hamel) [#56161](https://github.com/nodejs/node/pull/56161)
* \[[`3e17a8e78e`](https://github.com/nodejs/node/commit/3e17a8e78e)] - **util**: harden more built-in classes against prototype pollution (Antoine du Hamel) [#56225](https://github.com/nodejs/node/pull/56225)
* \[[`13815417c7`](https://github.com/nodejs/node/commit/13815417c7)] - **util**: fix Latin1 decoding to return string output (Mert Can Altin) [#56222](https://github.com/nodejs/node/pull/56222)
* \[[`77397c5013`](https://github.com/nodejs/node/commit/77397c5013)] - **util**: do not rely on mutable `Object` and `Function`' `constructor` prop (Antoine du Hamel) [#56188](https://github.com/nodejs/node/pull/56188)
* \[[`84f98e0a74`](https://github.com/nodejs/node/commit/84f98e0a74)] - **v8,tools**: expose experimental wasm revectorize feature (Yolanda-Chen) [#54896](https://github.com/nodejs/node/pull/54896)
* \[[`8325fa5c04`](https://github.com/nodejs/node/commit/8325fa5c04)] - **worker**: fix crash when a worker joins after exit (Stephen Belanger) [#56191](https://github.com/nodejs/node/pull/56191)

<a id="23.4.0"></a>

## 2024-12-10, Version 23.4.0 (Current), @aduh95 prepared by @targos

### Notable Changes

#### Introducing experimental `assert.partialDeepStrictEqual`

Sometimes, when writing test, we want to validate that some specific properties
are present, and the mere presence of additional keys are not exactly relevant
for that specific test. For this use case, we can now use
`assert.partialDeepStrictEqual`, which should be familiar to those already using
`assert.deepStrictEqual`, with the main difference that it does not require all
properties in the `actual` parameter to be present in the `expected` parameter.

Here are a few examples of usage:

```js
assert.partialDeepStrictEqual(
  { a: 1, b: 2, c: 3 },
  { a: 1, b: 2 },
);

assert.partialDeepStrictEqual(
  [1, 2, 3, 4],
  [2, 3],
);

assert.partialDeepStrictEqual(
  { a: { b: { c: 1, d: 2 } }, e: 3 },
  { a: { b: { c: 1 } } },
);

assert.partialDeepStrictEqual(
  { a: { b: { c: 1, d: 2 } }, e: 3 },
  { a: { b: { c: 1 } } },
);

assert.partialDeepStrictEqual(
  new Set([{ a: 1 }, { b: 1 }]),
  new Set([{ a: 1 }]),
);

assert.partialDeepStrictEqual(
  { a: new Set([{ a: 1 }, { b: 1 }]), b: new Map(), c: [1, 2, 3] },
  { a: new Set([{ a: 1 }]), c: [2] },
);
```

Contributed by Giovanni Bucci in [#54630](https://github.com/nodejs/node/pull/54630).

#### Other notable changes

* \[[`816d37a187`](https://github.com/nodejs/node/commit/816d37a187)] - **(SEMVER-MINOR)** **cli**: implement `--trace-env` and `--trace-env-[js|native]-stack` (Joyee Cheung) [#55604](https://github.com/nodejs/node/pull/55604)
* \[[`59d6891872`](https://github.com/nodejs/node/commit/59d6891872)] - **doc**: add LJHarb to collaborators (Jordan Harband) [#56132](https://github.com/nodejs/node/pull/56132)
* \[[`565b04a7be`](https://github.com/nodejs/node/commit/565b04a7be)] - **(SEMVER-MINOR)** **net**: add `BlockList.isBlockList(value)` (James M Snell) [#56078](https://github.com/nodejs/node/pull/56078)
* \[[`c9698ed6a4`](https://github.com/nodejs/node/commit/c9698ed6a4)] - **(SEMVER-MINOR)** **net**: support `blockList` in `net.connect` (theanarkh) [#56075](https://github.com/nodejs/node/pull/56075)
* \[[`30d604180d`](https://github.com/nodejs/node/commit/30d604180d)] - **(SEMVER-MINOR)** **net**: support `blockList` in `net.Server` (theanarkh) [#56079](https://github.com/nodejs/node/pull/56079)
* \[[`9fba5e1df1`](https://github.com/nodejs/node/commit/9fba5e1df1)] - **(SEMVER-MINOR)** **net**: add `SocketAddress.parse` (James M Snell) [#56076](https://github.com/nodejs/node/pull/56076)
* \[[`4cdb03201e`](https://github.com/nodejs/node/commit/4cdb03201e)] - **(SEMVER-MINOR)** **process**: deprecate `features.{ipv6,uv}` and `features.tls_*` (René) [#55545](https://github.com/nodejs/node/pull/55545)
* \[[`efb9f05f59`](https://github.com/nodejs/node/commit/efb9f05f59)] - **(SEMVER-MINOR)** **sqlite**: unflag `node:sqlite` module (Colin Ihrig) [#55890](https://github.com/nodejs/node/pull/55890)
* \[[`d777d4a52d`](https://github.com/nodejs/node/commit/d777d4a52d)] - **(SEMVER-MINOR)** **sqlite**: add `StatementSync.prototype.iterate` method (tpoisseau) [#54213](https://github.com/nodejs/node/pull/54213)

### Commits

* \[[`5b0ce376a2`](https://github.com/nodejs/node/commit/5b0ce376a2)] - **assert**: optimize partial comparison of two `Set`s (Antoine du Hamel) [#55970](https://github.com/nodejs/node/pull/55970)
* \[[`a4f57f0293`](https://github.com/nodejs/node/commit/a4f57f0293)] - **(SEMVER-MINOR)** **assert**: add partialDeepStrictEqual (Giovanni Bucci) [#54630](https://github.com/nodejs/node/pull/54630)
* \[[`1b81a7d003`](https://github.com/nodejs/node/commit/1b81a7d003)] - **build**: allow overriding clang usage (Shelley Vohr) [#56016](https://github.com/nodejs/node/pull/56016)
* \[[`39c901307f`](https://github.com/nodejs/node/commit/39c901307f)] - **build**: remove defaults for create-release-proposal (Rafael Gonzaga) [#56042](https://github.com/nodejs/node/pull/56042)
* \[[`7133c0459f`](https://github.com/nodejs/node/commit/7133c0459f)] - **build**: avoid compiling with VS v17.12 (Stefan Stojanovic) [#55930](https://github.com/nodejs/node/pull/55930)
* \[[`ce53f1689f`](https://github.com/nodejs/node/commit/ce53f1689f)] - **build**: set node\_arch to target\_cpu in GN (Shelley Vohr) [#55967](https://github.com/nodejs/node/pull/55967)
* \[[`2023b09d27`](https://github.com/nodejs/node/commit/2023b09d27)] - **build**: add create release proposal action (Rafael Gonzaga) [#55690](https://github.com/nodejs/node/pull/55690)
* \[[`26ec99634c`](https://github.com/nodejs/node/commit/26ec99634c)] - **build**: use variable for crypto dep path (Shelley Vohr) [#55928](https://github.com/nodejs/node/pull/55928)
* \[[`f48e289580`](https://github.com/nodejs/node/commit/f48e289580)] - **build**: fix GN build for sqlite (Cheng) [#55912](https://github.com/nodejs/node/pull/55912)
* \[[`fffabca6b8`](https://github.com/nodejs/node/commit/fffabca6b8)] - **build**: compile bundled simdutf conditionally (Jakub Jirutka) [#55886](https://github.com/nodejs/node/pull/55886)
* \[[`d8eb83c5c5`](https://github.com/nodejs/node/commit/d8eb83c5c5)] - **build**: compile bundled simdjson conditionally (Jakub Jirutka) [#55886](https://github.com/nodejs/node/pull/55886)
* \[[`83e02dc482`](https://github.com/nodejs/node/commit/83e02dc482)] - **build**: compile bundled ada conditionally (Jakub Jirutka) [#55886](https://github.com/nodejs/node/pull/55886)
* \[[`816d37a187`](https://github.com/nodejs/node/commit/816d37a187)] - **(SEMVER-MINOR)** **cli**: implement --trace-env and --trace-env-\[js|native]-stack (Joyee Cheung) [#55604](https://github.com/nodejs/node/pull/55604)
* \[[`53c0f2f186`](https://github.com/nodejs/node/commit/53c0f2f186)] - **crypto**: ensure CryptoKey usages and algorithm are cached objects (Filip Skokan) [#56108](https://github.com/nodejs/node/pull/56108)
* \[[`93d36bf1c8`](https://github.com/nodejs/node/commit/93d36bf1c8)] - **crypto**: allow non-multiple of 8 in SubtleCrypto.deriveBits (Filip Skokan) [#55296](https://github.com/nodejs/node/pull/55296)
* \[[`8680b8030c`](https://github.com/nodejs/node/commit/8680b8030c)] - **deps**: update ngtcp2 to 1.9.1 (Node.js GitHub Bot) [#56095](https://github.com/nodejs/node/pull/56095)
* \[[`78a2a6ca1e`](https://github.com/nodejs/node/commit/78a2a6ca1e)] - **deps**: upgrade npm to 10.9.2 (npm team) [#56135](https://github.com/nodejs/node/pull/56135)
* \[[`52dfe5af4b`](https://github.com/nodejs/node/commit/52dfe5af4b)] - **deps**: update sqlite to 3.47.1 (Node.js GitHub Bot) [#56094](https://github.com/nodejs/node/pull/56094)
* \[[`3852b5c8d1`](https://github.com/nodejs/node/commit/3852b5c8d1)] - **deps**: update zlib to 1.3.0.1-motley-82a5fec (Node.js GitHub Bot) [#55980](https://github.com/nodejs/node/pull/55980)
* \[[`f99f95f62f`](https://github.com/nodejs/node/commit/f99f95f62f)] - **deps**: update corepack to 0.30.0 (Node.js GitHub Bot) [#55977](https://github.com/nodejs/node/pull/55977)
* \[[`96e846de89`](https://github.com/nodejs/node/commit/96e846de89)] - **deps**: update ngtcp2 to 1.9.0 (Node.js GitHub Bot) [#55975](https://github.com/nodejs/node/pull/55975)
* \[[`d180a8aedb`](https://github.com/nodejs/node/commit/d180a8aedb)] - **deps**: update simdutf to 5.6.3 (Node.js GitHub Bot) [#55973](https://github.com/nodejs/node/pull/55973)
* \[[`288416a764`](https://github.com/nodejs/node/commit/288416a764)] - **deps**: upgrade npm to 10.9.1 (npm team) [#55951](https://github.com/nodejs/node/pull/55951)
* \[[`cf3f7ac512`](https://github.com/nodejs/node/commit/cf3f7ac512)] - **deps**: update zlib to 1.3.0.1-motley-7e2e4d7 (Node.js GitHub Bot) [#54432](https://github.com/nodejs/node/pull/54432)
* \[[`7768b3d054`](https://github.com/nodejs/node/commit/7768b3d054)] - **deps**: update simdjson to 3.10.1 (Node.js GitHub Bot) [#54678](https://github.com/nodejs/node/pull/54678)
* \[[`9c6103833b`](https://github.com/nodejs/node/commit/9c6103833b)] - **deps**: update simdutf to 5.6.2 (Node.js GitHub Bot) [#55889](https://github.com/nodejs/node/pull/55889)
* \[[`7b133d6220`](https://github.com/nodejs/node/commit/7b133d6220)] - **dgram**: check udp buffer size to avoid fd leak (theanarkh) [#56084](https://github.com/nodejs/node/pull/56084)
* \[[`e4529b8179`](https://github.com/nodejs/node/commit/e4529b8179)] - **doc**: add report version and history section (Chengzhong Wu) [#56130](https://github.com/nodejs/node/pull/56130)
* \[[`718625a03a`](https://github.com/nodejs/node/commit/718625a03a)] - **doc**: mention `-a` flag for the release script (Ruy Adorno) [#56124](https://github.com/nodejs/node/pull/56124)
* \[[`59d6891872`](https://github.com/nodejs/node/commit/59d6891872)] - **doc**: add LJHarb to collaborators (Jordan Harband) [#56132](https://github.com/nodejs/node/pull/56132)
* \[[`d7ed32404a`](https://github.com/nodejs/node/commit/d7ed32404a)] - **doc**: add create-release-action to process (Rafael Gonzaga) [#55993](https://github.com/nodejs/node/pull/55993)
* \[[`3b4ef93371`](https://github.com/nodejs/node/commit/3b4ef93371)] - **doc**: rename file to advocacy-ambassador-program.md (Tobias Nießen) [#56046](https://github.com/nodejs/node/pull/56046)
* \[[`59e4087d5e`](https://github.com/nodejs/node/commit/59e4087d5e)] - **doc**: add added tag and fix typo sqlite.md (Bart Louwers) [#56012](https://github.com/nodejs/node/pull/56012)
* \[[`a1b26608ae`](https://github.com/nodejs/node/commit/a1b26608ae)] - **doc**: remove unused import from sample code (Blended Bram) [#55570](https://github.com/nodejs/node/pull/55570)
* \[[`498f44ad73`](https://github.com/nodejs/node/commit/498f44ad73)] - **doc**: add FAQ to releases section (Rafael Gonzaga) [#55992](https://github.com/nodejs/node/pull/55992)
* \[[`d48348afaa`](https://github.com/nodejs/node/commit/d48348afaa)] - **doc**: move history entry to class description (Luigi Pinca) [#55991](https://github.com/nodejs/node/pull/55991)
* \[[`96926ce13c`](https://github.com/nodejs/node/commit/96926ce13c)] - **doc**: add history entry for textEncoder.encodeInto() (Luigi Pinca) [#55990](https://github.com/nodejs/node/pull/55990)
* \[[`e92d51d511`](https://github.com/nodejs/node/commit/e92d51d511)] - **doc**: improve GN build documentation a bit (Shelley Vohr) [#55968](https://github.com/nodejs/node/pull/55968)
* \[[`6be3824d6f`](https://github.com/nodejs/node/commit/6be3824d6f)] - **doc**: fix deprecation codes (Filip Skokan) [#56018](https://github.com/nodejs/node/pull/56018)
* \[[`fa2b35d28d`](https://github.com/nodejs/node/commit/fa2b35d28d)] - **doc**: remove confusing and outdated sentence (Luigi Pinca) [#55988](https://github.com/nodejs/node/pull/55988)
* \[[`baed2763df`](https://github.com/nodejs/node/commit/baed2763df)] - **doc**: deprecate passing invalid types in `fs.existsSync` (Carlos Espa) [#55892](https://github.com/nodejs/node/pull/55892)
* \[[`a3f7db6b6d`](https://github.com/nodejs/node/commit/a3f7db6b6d)] - **doc**: add doc for PerformanceObserver.takeRecords() (skyclouds2001) [#55786](https://github.com/nodejs/node/pull/55786)
* \[[`770572423b`](https://github.com/nodejs/node/commit/770572423b)] - **doc**: add vetted courses to the ambassador benefits (Matteo Collina) [#55934](https://github.com/nodejs/node/pull/55934)
* \[[`98f8f4a8a9`](https://github.com/nodejs/node/commit/98f8f4a8a9)] - **doc**: order `node:crypto` APIs alphabetically (Julian Gassner) [#55831](https://github.com/nodejs/node/pull/55831)
* \[[`1e0decb44c`](https://github.com/nodejs/node/commit/1e0decb44c)] - **doc**: doc how to add message for promotion (Michael Dawson) [#55843](https://github.com/nodejs/node/pull/55843)
* \[[`ff48c29724`](https://github.com/nodejs/node/commit/ff48c29724)] - **doc**: add esm example for zlib (Leonardo Peixoto) [#55946](https://github.com/nodejs/node/pull/55946)
* \[[`ccc5a6d552`](https://github.com/nodejs/node/commit/ccc5a6d552)] - **doc**: document approach for building wasm in deps (Michael Dawson) [#55940](https://github.com/nodejs/node/pull/55940)
* \[[`c8bb8a6ac5`](https://github.com/nodejs/node/commit/c8bb8a6ac5)] - **doc**: fix Node.js 23 column in CHANGELOG.md (Richard Lau) [#55935](https://github.com/nodejs/node/pull/55935)
* \[[`9d078802ad`](https://github.com/nodejs/node/commit/9d078802ad)] - **doc**: remove RedYetiDev from triagers team (Aviv Keller) [#55947](https://github.com/nodejs/node/pull/55947)
* \[[`5a2a757119`](https://github.com/nodejs/node/commit/5a2a757119)] - **doc**: add esm examples to node:timers (Alfredo González) [#55857](https://github.com/nodejs/node/pull/55857)
* \[[`f711a48e15`](https://github.com/nodejs/node/commit/f711a48e15)] - **doc**: fix relative path mention in --allow-fs (Rafael Gonzaga) [#55791](https://github.com/nodejs/node/pull/55791)
* \[[`219f5f2627`](https://github.com/nodejs/node/commit/219f5f2627)] - **doc**: include git node release --promote to steps (Rafael Gonzaga) [#55835](https://github.com/nodejs/node/pull/55835)
* \[[`f9d25ed3e4`](https://github.com/nodejs/node/commit/f9d25ed3e4)] - **doc**: add history entry for import assertion removal (Antoine du Hamel) [#55883](https://github.com/nodejs/node/pull/55883)
* \[[`efb9f05f59`](https://github.com/nodejs/node/commit/efb9f05f59)] - **(SEMVER-MINOR)** **doc,lib,src,test**: unflag sqlite module (Colin Ihrig) [#55890](https://github.com/nodejs/node/pull/55890)
* \[[`a37e5fe5f8`](https://github.com/nodejs/node/commit/a37e5fe5f8)] - **fs**: lazily load ReadFileContext (Gürgün Dayıoğlu) [#55998](https://github.com/nodejs/node/pull/55998)
* \[[`9289374248`](https://github.com/nodejs/node/commit/9289374248)] - **http2**: fix memory leak caused by premature listener removing (ywave620) [#55966](https://github.com/nodejs/node/pull/55966)
* \[[`49af1c33ac`](https://github.com/nodejs/node/commit/49af1c33ac)] - **lib**: add validation for options in compileFunction (Taejin Kim) [#56023](https://github.com/nodejs/node/pull/56023)
* \[[`8faf91846b`](https://github.com/nodejs/node/commit/8faf91846b)] - **lib**: fix `fs.readdir` recursive async (Rafael Gonzaga) [#56041](https://github.com/nodejs/node/pull/56041)
* \[[`a2382303d7`](https://github.com/nodejs/node/commit/a2382303d7)] - **lib**: refactor code to improve readability (Pietro Marchini) [#55995](https://github.com/nodejs/node/pull/55995)
* \[[`30f26ba254`](https://github.com/nodejs/node/commit/30f26ba254)] - **lib**: avoid excluding symlinks in recursive fs.readdir with filetypes (Juan José) [#55714](https://github.com/nodejs/node/pull/55714)
* \[[`9b272ae339`](https://github.com/nodejs/node/commit/9b272ae339)] - **meta**: bump github/codeql-action from 3.27.0 to 3.27.5 (dependabot\[bot]) [#56103](https://github.com/nodejs/node/pull/56103)
* \[[`fb0e6ca68b`](https://github.com/nodejs/node/commit/fb0e6ca68b)] - **meta**: bump actions/checkout from 4.1.7 to 4.2.2 (dependabot\[bot]) [#56102](https://github.com/nodejs/node/pull/56102)
* \[[`0ab611513c`](https://github.com/nodejs/node/commit/0ab611513c)] - **meta**: bump step-security/harden-runner from 2.10.1 to 2.10.2 (dependabot\[bot]) [#56101](https://github.com/nodejs/node/pull/56101)
* \[[`ff4839b8ab`](https://github.com/nodejs/node/commit/ff4839b8ab)] - **meta**: bump actions/setup-node from 4.0.3 to 4.1.0 (dependabot\[bot]) [#56100](https://github.com/nodejs/node/pull/56100)
* \[[`f262207356`](https://github.com/nodejs/node/commit/f262207356)] - **meta**: add releasers as CODEOWNERS to proposal action (Rafael Gonzaga) [#56043](https://github.com/nodejs/node/pull/56043)
* \[[`b6005b3fac`](https://github.com/nodejs/node/commit/b6005b3fac)] - **module**: mark evaluation rejection in require(esm) as handled (Joyee Cheung) [#56122](https://github.com/nodejs/node/pull/56122)
* \[[`b8ab5332a9`](https://github.com/nodejs/node/commit/b8ab5332a9)] - **module**: remove --experimental-default-type (Geoffrey Booth) [#56092](https://github.com/nodejs/node/pull/56092)
* \[[`4be5047030`](https://github.com/nodejs/node/commit/4be5047030)] - **module**: do not warn when require(esm) comes from node\_modules (Joyee Cheung) [#55960](https://github.com/nodejs/node/pull/55960)
* \[[`c9698ed6a4`](https://github.com/nodejs/node/commit/c9698ed6a4)] - **(SEMVER-MINOR)** **net**: support blocklist in net.connect (theanarkh) [#56075](https://github.com/nodejs/node/pull/56075)
* \[[`9fba5e1df1`](https://github.com/nodejs/node/commit/9fba5e1df1)] - **(SEMVER-MINOR)** **net**: add SocketAddress.parse (James M Snell) [#56076](https://github.com/nodejs/node/pull/56076)
* \[[`565b04a7be`](https://github.com/nodejs/node/commit/565b04a7be)] - **(SEMVER-MINOR)** **net**: add net.BlockList.isBlockList(value) (James M Snell) [#56078](https://github.com/nodejs/node/pull/56078)
* \[[`30d604180d`](https://github.com/nodejs/node/commit/30d604180d)] - **(SEMVER-MINOR)** **net**: support blocklist for net.Server (theanarkh) [#56079](https://github.com/nodejs/node/pull/56079)
* \[[`4cdb03201e`](https://github.com/nodejs/node/commit/4cdb03201e)] - **(SEMVER-MINOR)** **process**: deprecate `features.{ipv6,uv}` and `features.tls_*` (René) [#55545](https://github.com/nodejs/node/pull/55545)
* \[[`d09e57b26d`](https://github.com/nodejs/node/commit/d09e57b26d)] - **quic**: update more QUIC implementation (James M Snell) [#55986](https://github.com/nodejs/node/pull/55986)
* \[[`1fb30d6e86`](https://github.com/nodejs/node/commit/1fb30d6e86)] - **quic**: multiple updates to quic impl (James M Snell) [#55971](https://github.com/nodejs/node/pull/55971)
* \[[`9e4f7aa808`](https://github.com/nodejs/node/commit/9e4f7aa808)] - **sqlite**: deps include `sqlite3ext.h` (Alex Yang) [#56010](https://github.com/nodejs/node/pull/56010)
* \[[`d777d4a52d`](https://github.com/nodejs/node/commit/d777d4a52d)] - **(SEMVER-MINOR)** **sqlite**: add `StatementSync.prototype.iterate` method (tpoisseau) [#54213](https://github.com/nodejs/node/pull/54213)
* \[[`66451bb9ba`](https://github.com/nodejs/node/commit/66451bb9ba)] - **src**: use spaceship operator in SocketAddress (James M Snell) [#56059](https://github.com/nodejs/node/pull/56059)
* \[[`ad9ebe417a`](https://github.com/nodejs/node/commit/ad9ebe417a)] - **src**: add missing qualifiers to env.cc (Yagiz Nizipli) [#56062](https://github.com/nodejs/node/pull/56062)
* \[[`56c4da240d`](https://github.com/nodejs/node/commit/56c4da240d)] - **src**: use std::string\_view for process emit fns (Yagiz Nizipli) [#56086](https://github.com/nodejs/node/pull/56086)
* \[[`26ab8e9823`](https://github.com/nodejs/node/commit/26ab8e9823)] - **src**: remove dead code in async\_wrap (Gerhard Stöbich) [#56065](https://github.com/nodejs/node/pull/56065)
* \[[`4dea44e468`](https://github.com/nodejs/node/commit/4dea44e468)] - **src**: avoid copy on getV8FastApiCallCount (Yagiz Nizipli) [#56081](https://github.com/nodejs/node/pull/56081)
* \[[`b778a4fe46`](https://github.com/nodejs/node/commit/b778a4fe46)] - **src**: fix check fd (theanarkh) [#56000](https://github.com/nodejs/node/pull/56000)
* \[[`971f5f54df`](https://github.com/nodejs/node/commit/971f5f54df)] - **src**: safely remove the last line from dotenv (Shima Ryuhei) [#55982](https://github.com/nodejs/node/pull/55982)
* \[[`497a9aea1c`](https://github.com/nodejs/node/commit/497a9aea1c)] - **src**: fix kill signal on Windows (Hüseyin Açacak) [#55514](https://github.com/nodejs/node/pull/55514)
* \[[`8a935489f9`](https://github.com/nodejs/node/commit/8a935489f9)] - **src,build**: add no user defined deduction guides of CTAD check (Chengzhong Wu) [#56071](https://github.com/nodejs/node/pull/56071)
* \[[`5edb8d5919`](https://github.com/nodejs/node/commit/5edb8d5919)] - **test**: remove test-fs-utimes flaky designation (Luigi Pinca) [#56052](https://github.com/nodejs/node/pull/56052)
* \[[`046e642a80`](https://github.com/nodejs/node/commit/046e642a80)] - **test**: ensure `cli.md` is in alphabetical order (Antoine du Hamel) [#56025](https://github.com/nodejs/node/pull/56025)
* \[[`da354f46cd`](https://github.com/nodejs/node/commit/da354f46cd)] - **test**: update WPT for WebCryptoAPI to 3e3374efde (Node.js GitHub Bot) [#56093](https://github.com/nodejs/node/pull/56093)
* \[[`9486c7ce4c`](https://github.com/nodejs/node/commit/9486c7ce4c)] - **test**: update WPT for WebCryptoAPI to 76dfa54e5d (Node.js GitHub Bot) [#56093](https://github.com/nodejs/node/pull/56093)
* \[[`a8809fc0f5`](https://github.com/nodejs/node/commit/a8809fc0f5)] - **test**: move test-worker-arraybuffer-zerofill to parallel (Luigi Pinca) [#56053](https://github.com/nodejs/node/pull/56053)
* \[[`6194435b9e`](https://github.com/nodejs/node/commit/6194435b9e)] - **test**: update WPT for url to 67880a4eb83ca9aa732eec4b35a1971ff5bf37ff (Node.js GitHub Bot) [#55999](https://github.com/nodejs/node/pull/55999)
* \[[`f7567d46d8`](https://github.com/nodejs/node/commit/f7567d46d8)] - **test**: make HTTP/1.0 connection test more robust (Arne Keller) [#55959](https://github.com/nodejs/node/pull/55959)
* \[[`c157e026fc`](https://github.com/nodejs/node/commit/c157e026fc)] - **test**: convert readdir test to use test runner (Thomas Chetwin) [#55750](https://github.com/nodejs/node/pull/55750)
* \[[`29362ce673`](https://github.com/nodejs/node/commit/29362ce673)] - **test**: make x509 crypto tests work with BoringSSL (Shelley Vohr) [#55927](https://github.com/nodejs/node/pull/55927)
* \[[`493e16c852`](https://github.com/nodejs/node/commit/493e16c852)] - **test**: fix determining lower priority (Livia Medeiros) [#55908](https://github.com/nodejs/node/pull/55908)
* \[[`99858ceb9f`](https://github.com/nodejs/node/commit/99858ceb9f)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#55997](https://github.com/nodejs/node/pull/55997)
* \[[`7c3a4d4bcd`](https://github.com/nodejs/node/commit/7c3a4d4bcd)] - **test\_runner**: refactor Promise chain in run() (Colin Ihrig) [#55958](https://github.com/nodejs/node/pull/55958)
* \[[`95e8c4ef6c`](https://github.com/nodejs/node/commit/95e8c4ef6c)] - **test\_runner**: refactor build Promise in Suite() (Colin Ihrig) [#55958](https://github.com/nodejs/node/pull/55958)
* \[[`c048865199`](https://github.com/nodejs/node/commit/c048865199)] - **test\_runner**: simplify hook running logic (Colin Ihrig) [#55963](https://github.com/nodejs/node/pull/55963)
* \[[`8197815fe8`](https://github.com/nodejs/node/commit/8197815fe8)] - **test\_runner**: mark snapshot testing as stable (Colin Ihrig) [#55897](https://github.com/nodejs/node/pull/55897)
* \[[`8a5d8c7669`](https://github.com/nodejs/node/commit/8a5d8c7669)] - **test\_runner**: mark context.plan() as stable (Colin Ihrig) [#55895](https://github.com/nodejs/node/pull/55895)
* \[[`790a2ca3b7`](https://github.com/nodejs/node/commit/790a2ca3b7)] - **tools**: update `create-release-proposal` workflow (Antoine du Hamel) [#56054](https://github.com/nodejs/node/pull/56054)
* \[[`98ce4652e2`](https://github.com/nodejs/node/commit/98ce4652e2)] - **tools**: fix update-undici script (Michaël Zasso) [#56069](https://github.com/nodejs/node/pull/56069)
* \[[`d6a6c8ace1`](https://github.com/nodejs/node/commit/d6a6c8ace1)] - **tools**: allow dispatch of `tools.yml` from forks (Antoine du Hamel) [#56008](https://github.com/nodejs/node/pull/56008)
* \[[`cc96fce5eb`](https://github.com/nodejs/node/commit/cc96fce5eb)] - **tools**: fix nghttp3 updater script (Antoine du Hamel) [#56007](https://github.com/nodejs/node/pull/56007)
* \[[`2cd939cb95`](https://github.com/nodejs/node/commit/2cd939cb95)] - **tools**: filter release keys to reduce interactivity (Antoine du Hamel) [#55950](https://github.com/nodejs/node/pull/55950)
* \[[`4b3919f1be`](https://github.com/nodejs/node/commit/4b3919f1be)] - **tools**: update WPT updater (Antoine du Hamel) [#56003](https://github.com/nodejs/node/pull/56003)
* \[[`54c46b8464`](https://github.com/nodejs/node/commit/54c46b8464)] - **tools**: add WPT updater for specific subsystems (Mert Can Altin) [#54460](https://github.com/nodejs/node/pull/54460)
* \[[`32b1681b7f`](https://github.com/nodejs/node/commit/32b1681b7f)] - **tools**: use tokenless Codecov uploads (Michaël Zasso) [#55943](https://github.com/nodejs/node/pull/55943)
* \[[`475141e370`](https://github.com/nodejs/node/commit/475141e370)] - **tools**: add linter for release commit proposals (Antoine du Hamel) [#55923](https://github.com/nodejs/node/pull/55923)
* \[[`d093820f64`](https://github.com/nodejs/node/commit/d093820f64)] - **tools**: lint js in `doc/**/*.md` (Livia Medeiros) [#55904](https://github.com/nodejs/node/pull/55904)
* \[[`72eb710f0f`](https://github.com/nodejs/node/commit/72eb710f0f)] - **tools**: fix riscv64 build failed (Lu Yahan) [#52888](https://github.com/nodejs/node/pull/52888)
* \[[`882b70c83f`](https://github.com/nodejs/node/commit/882b70c83f)] - **tools**: bump cross-spawn from 7.0.3 to 7.0.5 in /tools/eslint (dependabot\[bot]) [#55894](https://github.com/nodejs/node/pull/55894)
* \[[`9eccd7dba9`](https://github.com/nodejs/node/commit/9eccd7dba9)] - **util**: add fast path for Latin1 decoding (Mert Can Altin) [#55275](https://github.com/nodejs/node/pull/55275)

<a id="23.3.0"></a>

## 2024-11-20, Version 23.3.0 (Current), @RafaelGSS

### Notable Changes

#### SQLite Session Extension

Basic support for the [SQLite Session Extension](https://www.sqlite.org/sessionintro.html)
got added to the experimental `node:sqlite` module.

```js
const sourceDb = new DatabaseSync(':memory:');
const targetDb = new DatabaseSync(':memory:');

sourceDb.exec('CREATE TABLE data(key INTEGER PRIMARY KEY, value TEXT)');
targetDb.exec('CREATE TABLE data(key INTEGER PRIMARY KEY, value TEXT)');

const session = sourceDb.createSession();

const insert = sourceDb.prepare('INSERT INTO data (key, value) VALUES (?, ?)');
insert.run(1, 'hello');
insert.run(2, 'world');

const changeset = session.changeset();
targetDb.applyChangeset(changeset);
// Now that the changeset has been applied, targetDb contains the same data as sourceDb.
```

Of note to distributors when dynamically linking with SQLite (using the `--shared-sqlite`
flag): compiling SQLite with `SQLITE_ENABLE_SESSION` and `SQLITE_ENABLE_PREUPDATE_HOOK`
defines is now required.

Contributed by Bart Louwers in [#54181](https://github.com/nodejs/node/pull/54181).

#### Other Notable Changes

* \[[`5767b76c30`](https://github.com/nodejs/node/commit/5767b76c30)] - **doc**: enforce strict policy to semver-major releases (Rafael Gonzaga) [#55732](https://github.com/nodejs/node/pull/55732)
* \[[`ccb69bb8d5`](https://github.com/nodejs/node/commit/ccb69bb8d5)] - **(SEMVER-MINOR)** **src**: add cli option to preserve env vars on dr (Rafael Gonzaga) [#55697](https://github.com/nodejs/node/pull/55697)
* \[[`d4e792643d`](https://github.com/nodejs/node/commit/d4e792643d)] - **(SEMVER-MINOR)** **util**: add sourcemap support to getCallSites (Marco Ippolito) [#55589](https://github.com/nodejs/node/pull/55589)
* \[[`00e092bb4b`](https://github.com/nodejs/node/commit/00e092bb4b)] - **(SEMVER-MINOR)** **util**: fix util.getCallSites plurality (Chengzhong Wu) [#55626](https://github.com/nodejs/node/pull/55626)

### Commits

* \[[`9862912d41`](https://github.com/nodejs/node/commit/9862912d41)] - **assert**: differentiate cases where `cause` is `undefined` or missing (Antoine du Hamel) [#55738](https://github.com/nodejs/node/pull/55738)
* \[[`32e5bbca95`](https://github.com/nodejs/node/commit/32e5bbca95)] - **benchmark**: add `test-reporters` (Aviv Keller) [#55757](https://github.com/nodejs/node/pull/55757)
* \[[`c2103354e6`](https://github.com/nodejs/node/commit/c2103354e6)] - **benchmark**: add `test_runner/mock-fn` (Aviv Keller) [#55771](https://github.com/nodejs/node/pull/55771)
* \[[`472d55e3e4`](https://github.com/nodejs/node/commit/472d55e3e4)] - **build**: implement node\_use\_amaro flag in GN build (Cheng) [#55798](https://github.com/nodejs/node/pull/55798)
* \[[`77735674eb`](https://github.com/nodejs/node/commit/77735674eb)] - **build**: use glob for dependencies of out/Makefile (Richard Lau) [#55789](https://github.com/nodejs/node/pull/55789)
* \[[`bba7323d51`](https://github.com/nodejs/node/commit/bba7323d51)] - **build**: apply cpp linting and formatting to ncrypto (Aviv Keller) [#55362](https://github.com/nodejs/node/pull/55362)
* \[[`e0c222525e`](https://github.com/nodejs/node/commit/e0c222525e)] - **crypto**: allow length=0 for HKDF and PBKDF2 in SubtleCrypto.deriveBits (Filip Skokan) [#55866](https://github.com/nodejs/node/pull/55866)
* \[[`cad557ec53`](https://github.com/nodejs/node/commit/cad557ec53)] - **deps**: update simdutf to 5.6.1 (Node.js GitHub Bot) [#55850](https://github.com/nodejs/node/pull/55850)
* \[[`dc8aca3692`](https://github.com/nodejs/node/commit/dc8aca3692)] - **deps**: update undici to 6.21.0 (Node.js GitHub Bot) [#55851](https://github.com/nodejs/node/pull/55851)
* \[[`e0db9ede4f`](https://github.com/nodejs/node/commit/e0db9ede4f)] - **deps**: update c-ares to v1.34.3 (Node.js GitHub Bot) [#55803](https://github.com/nodejs/node/pull/55803)
* \[[`e147935144`](https://github.com/nodejs/node/commit/e147935144)] - **deps**: update icu to 76.1 (Node.js GitHub Bot) [#55551](https://github.com/nodejs/node/pull/55551)
* \[[`e0ef65b8d5`](https://github.com/nodejs/node/commit/e0ef65b8d5)] - **doc**: remove non-working example (Antoine du Hamel) [#55856](https://github.com/nodejs/node/pull/55856)
* \[[`ec953bca09`](https://github.com/nodejs/node/commit/ec953bca09)] - **doc**: add `node:sqlite` to mandatory `node:` prefix list (翠 / green) [#55846](https://github.com/nodejs/node/pull/55846)
* \[[`1b863b96d5`](https://github.com/nodejs/node/commit/1b863b96d5)] - **doc**: add `-S` flag release preparation example (Antoine du Hamel) [#55836](https://github.com/nodejs/node/pull/55836)
* \[[`a8311847d1`](https://github.com/nodejs/node/commit/a8311847d1)] - **doc**: clarify UV\_THREADPOOL\_SIZE env var usage (Preveen P) [#55832](https://github.com/nodejs/node/pull/55832)
* \[[`787e51e603`](https://github.com/nodejs/node/commit/787e51e603)] - **doc**: add notable-change mention to sec release (Rafael Gonzaga) [#55830](https://github.com/nodejs/node/pull/55830)
* \[[`e56265cc18`](https://github.com/nodejs/node/commit/e56265cc18)] - **doc**: fix history info for `URL.prototype.toJSON` (Antoine du Hamel) [#55818](https://github.com/nodejs/node/pull/55818)
* \[[`c5afdaf5cb`](https://github.com/nodejs/node/commit/c5afdaf5cb)] - **doc**: correct max-semi-space-size statement (Joe Bowbeer) [#55812](https://github.com/nodejs/node/pull/55812)
* \[[`65ffb2cae3`](https://github.com/nodejs/node/commit/65ffb2cae3)] - **doc**: update unflag info of `import.meta.resolve` (skyclouds2001) [#55810](https://github.com/nodejs/node/pull/55810)
* \[[`9aeb671677`](https://github.com/nodejs/node/commit/9aeb671677)] - **doc**: run license-builder (github-actions\[bot]) [#55813](https://github.com/nodejs/node/pull/55813)
* \[[`df5ea1a5b3`](https://github.com/nodejs/node/commit/df5ea1a5b3)] - **doc**: clarify triager role (Gireesh Punathil) [#55775](https://github.com/nodejs/node/pull/55775)
* \[[`aa12de0f03`](https://github.com/nodejs/node/commit/aa12de0f03)] - **doc**: sort --report-exclude alphabetically (Rafael Gonzaga) [#55788](https://github.com/nodejs/node/pull/55788)
* \[[`8576ca9897`](https://github.com/nodejs/node/commit/8576ca9897)] - **doc**: clarify removal of experimental API does not require a deprecation (Antoine du Hamel) [#55746](https://github.com/nodejs/node/pull/55746)
* \[[`5767b76c30`](https://github.com/nodejs/node/commit/5767b76c30)] - **doc**: enforce strict policy to semver-major releases (Rafael Gonzaga) [#55732](https://github.com/nodejs/node/pull/55732)
* \[[`1f2fcf1dc8`](https://github.com/nodejs/node/commit/1f2fcf1dc8)] - **doc**: add history entries for JSON modules stabilization (Antoine du Hamel) [#55855](https://github.com/nodejs/node/pull/55855)
* \[[`83ba688d8f`](https://github.com/nodejs/node/commit/83ba688d8f)] - **esm**: fix import.meta.resolve crash (Marco Ippolito) [#55777](https://github.com/nodejs/node/pull/55777)
* \[[`bdb6d12e7a`](https://github.com/nodejs/node/commit/bdb6d12e7a)] - **events**: add hasEventListener util for validate (Sunghoon) [#55230](https://github.com/nodejs/node/pull/55230)
* \[[`d41cb49516`](https://github.com/nodejs/node/commit/d41cb49516)] - **fs**: prevent unwanted `dependencyOwners` removal (Carlos Espa) [#55565](https://github.com/nodejs/node/pull/55565)
* \[[`db0d648d8f`](https://github.com/nodejs/node/commit/db0d648d8f)] - **fs**: fix bufferSize option for opendir recursive (Ethan Arrowood) [#55744](https://github.com/nodejs/node/pull/55744)
* \[[`693fda0802`](https://github.com/nodejs/node/commit/693fda0802)] - **lib**: remove unused file `fetch_module` (Michaël Zasso) [#55880](https://github.com/nodejs/node/pull/55880)
* \[[`156873303a`](https://github.com/nodejs/node/commit/156873303a)] - **lib**: prefer symbol to number in webidl `type` function (Antoine du Hamel) [#55737](https://github.com/nodejs/node/pull/55737)
* \[[`cfe28b161a`](https://github.com/nodejs/node/commit/cfe28b161a)] - **lib**: remove unnecessary optional chaining (Gürgün Dayıoğlu) [#55728](https://github.com/nodejs/node/pull/55728)
* \[[`bbb8f5914d`](https://github.com/nodejs/node/commit/bbb8f5914d)] - **lib**: use `Promise.withResolvers()` in timers (Yagiz Nizipli) [#55720](https://github.com/nodejs/node/pull/55720)
* \[[`11e1bdd409`](https://github.com/nodejs/node/commit/11e1bdd409)] - **module**: tidy code string concat → string templates (Jacob Smith) [#55820](https://github.com/nodejs/node/pull/55820)
* \[[`9c99255468`](https://github.com/nodejs/node/commit/9c99255468)] - **permission**: ignore internalModuleStat on module loading (Rafael Gonzaga) [#55797](https://github.com/nodejs/node/pull/55797)
* \[[`5a437c446f`](https://github.com/nodejs/node/commit/5a437c446f)] - **report**: fix network queries in getReport libuv with exclude-network (Adrien Foulon) [#55602](https://github.com/nodejs/node/pull/55602)
* \[[`bcbba723de`](https://github.com/nodejs/node/commit/bcbba723de)] - **sqlite**: add support for SQLite Session Extension (Bart Louwers) [#54181](https://github.com/nodejs/node/pull/54181)
* \[[`49d55228de`](https://github.com/nodejs/node/commit/49d55228de)] - **src**: use env strings to create sqlite results (Michaël Zasso) [#55785](https://github.com/nodejs/node/pull/55785)
* \[[`58d7a6ec10`](https://github.com/nodejs/node/commit/58d7a6ec10)] - _**Revert**_ "**src**: migrate `String::Value` to `String::ValueView`" (Michaël Zasso) [#55828](https://github.com/nodejs/node/pull/55828)
* \[[`16786a6df8`](https://github.com/nodejs/node/commit/16786a6df8)] - **src**: improve `node:os` userInfo performance (Yagiz Nizipli) [#55719](https://github.com/nodejs/node/pull/55719)
* \[[`ccb69bb8d5`](https://github.com/nodejs/node/commit/ccb69bb8d5)] - **(SEMVER-MINOR)** **src**: add cli option to preserve env vars on dr (Rafael Gonzaga) [#55697](https://github.com/nodejs/node/pull/55697)
* \[[`770670c52c`](https://github.com/nodejs/node/commit/770670c52c)] - **test**: fix permission fixtures lint (Rafael Gonzaga) [#55819](https://github.com/nodejs/node/pull/55819)
* \[[`84c47478d0`](https://github.com/nodejs/node/commit/84c47478d0)] - **test**: improve test coverage for child process message sending (Juan José) [#55710](https://github.com/nodejs/node/pull/55710)
* \[[`e1f54e2527`](https://github.com/nodejs/node/commit/e1f54e2527)] - **test**: ensure that test priority is not higher than current priority (Livia Medeiros) [#55739](https://github.com/nodejs/node/pull/55739)
* \[[`e1b42e7637`](https://github.com/nodejs/node/commit/e1b42e7637)] - **test**: add buffer to fs\_permission tests (Rafael Gonzaga) [#55734](https://github.com/nodejs/node/pull/55734)
* \[[`d1ad43e9ae`](https://github.com/nodejs/node/commit/d1ad43e9ae)] - **test**: improve test coverage for `ServerResponse` (Juan José) [#55711](https://github.com/nodejs/node/pull/55711)
* \[[`034505e037`](https://github.com/nodejs/node/commit/034505e037)] - **test\_runner**: error on mocking an already mocked date (Aviv Keller) [#55858](https://github.com/nodejs/node/pull/55858)
* \[[`44324aa7e9`](https://github.com/nodejs/node/commit/44324aa7e9)] - **tools**: bump @eslint/plugin-kit from 0.2.0 to 0.2.3 in /tools/eslint (dependabot\[bot]) [#55875](https://github.com/nodejs/node/pull/55875)
* \[[`3cfacd3fbb`](https://github.com/nodejs/node/commit/3cfacd3fbb)] - **tools**: fix exclude labels for commit-queue (Richard Lau) [#55809](https://github.com/nodejs/node/pull/55809)
* \[[`8111a7655d`](https://github.com/nodejs/node/commit/8111a7655d)] - **tools**: make commit-queue check blocked label (Marco Ippolito) [#55781](https://github.com/nodejs/node/pull/55781)
* \[[`419ea068fb`](https://github.com/nodejs/node/commit/419ea068fb)] - **tools**: remove non-existent file from eslint config (Aviv Keller) [#55772](https://github.com/nodejs/node/pull/55772)
* \[[`7814669377`](https://github.com/nodejs/node/commit/7814669377)] - **tools**: fix c-ares updater script for Node.js 18 (Richard Lau) [#55717](https://github.com/nodejs/node/pull/55717)
* \[[`3a9733cc4f`](https://github.com/nodejs/node/commit/3a9733cc4f)] - **util**: do not mark experimental feature as deprecated (Antoine du Hamel) [#55740](https://github.com/nodejs/node/pull/55740)
* \[[`d4e792643d`](https://github.com/nodejs/node/commit/d4e792643d)] - **(SEMVER-MINOR)** **util**: add sourcemap support to getCallSites (Marco Ippolito) [#55589](https://github.com/nodejs/node/pull/55589)
* \[[`00e092bb4b`](https://github.com/nodejs/node/commit/00e092bb4b)] - **(SEMVER-MINOR)** **util**: fix util.getCallSites plurality (Chengzhong Wu) [#55626](https://github.com/nodejs/node/pull/55626)

<a id="23.2.0"></a>

## 2024-11-11, Version 23.2.0 (Current), @aduh95

### Notable Changes

#### Update root certificates to NSS 3.104

This is the version of NSS that shipped in Firefox 131.0 on 2024-10-01.

Certificates added:

* FIRMAPROFESIONAL CA ROOT-A WEB
* TWCA CYBER Root CA
* SecureSign Root CA12
* SecureSign Root CA14
* SecureSign Root CA15

#### Other notable changes

* \[[`fa61dced44`](https://github.com/nodejs/node/commit/fa61dced44)] - **doc**: move typescript support to active development (Marco Ippolito) [#55536](https://github.com/nodejs/node/pull/55536)
* \[[`9dcca5441b`](https://github.com/nodejs/node/commit/9dcca5441b)] - **doc**: add jazelly to collaborators (Jason Zhang) [#55531](https://github.com/nodejs/node/pull/55531)
* \[[`f628fc43cb`](https://github.com/nodejs/node/commit/f628fc43cb)] - **(SEMVER-MINOR)** **fs**: make `dirent.path` writable (Antoine du Hamel) [#55547](https://github.com/nodejs/node/pull/55547)
* \[[`25b1422337`](https://github.com/nodejs/node/commit/25b1422337)] - **(SEMVER-MINOR)** **http**: add diagnostic channel `http.client.request.created` (Marco Ippolito) [#55586](https://github.com/nodejs/node/pull/55586)
* \[[`adda37f00c`](https://github.com/nodejs/node/commit/adda37f00c)] - **(SEMVER-MINOR)** **module**: add `findPackageJSON` util (Jacob Smith) [#55412](https://github.com/nodejs/node/pull/55412)
* \[[`69dd1e13c3`](https://github.com/nodejs/node/commit/69dd1e13c3)] - **(SEMVER-MINOR)** **module**: add `module.stripTypeScriptTypes` (Marco Ippolito) [#55282](https://github.com/nodejs/node/pull/55282)

### Commits

* \[[`9dbb255efb`](https://github.com/nodejs/node/commit/9dbb255efb)] - **assert**: fix `deepStrictEqual` on errors when `cause` is not undefined (Edigleysson Silva (Edy)) [#55406](https://github.com/nodejs/node/pull/55406)
* \[[`7af76ef0b3`](https://github.com/nodejs/node/commit/7af76ef0b3)] - **assert**: fix the string length check for printing the simple diff (Giovanni Bucci) [#55474](https://github.com/nodejs/node/pull/55474)
* \[[`34483a299b`](https://github.com/nodejs/node/commit/34483a299b)] - **benchmark**: add nodeTiming.uvmetricsinfo bench (RafaelGSS) [#55614](https://github.com/nodejs/node/pull/55614)
* \[[`b79e4835ab`](https://github.com/nodejs/node/commit/b79e4835ab)] - **build**: use rclone instead of aws CLI (Michaël Zasso) [#55617](https://github.com/nodejs/node/pull/55617)
* \[[`7ab1f46b8a`](https://github.com/nodejs/node/commit/7ab1f46b8a)] - **build**: stop pre-compiling `lint-md` (Aviv Keller) [#55266](https://github.com/nodejs/node/pull/55266)
* \[[`4887214e23`](https://github.com/nodejs/node/commit/4887214e23)] - **build**: fix building with system icu 76 (Michael Cho) [#55563](https://github.com/nodejs/node/pull/55563)
* \[[`f8df27aa5a`](https://github.com/nodejs/node/commit/f8df27aa5a)] - **build**: fix GN arg used in generate\_config\_gypi.py (Shelley Vohr) [#55530](https://github.com/nodejs/node/pull/55530)
* \[[`bb78904548`](https://github.com/nodejs/node/commit/bb78904548)] - **build**: fix GN build for sqlite and nghttp2 (Shelley Vohr) [#55529](https://github.com/nodejs/node/pull/55529)
* \[[`535f1b0d4c`](https://github.com/nodejs/node/commit/535f1b0d4c)] - **crypto**: update root certificates to NSS 3.104 (Richard Lau) [#55681](https://github.com/nodejs/node/pull/55681)
* \[[`9b351b0749`](https://github.com/nodejs/node/commit/9b351b0749)] - **crypto**: fix `RSA_PKCS1_PADDING` error message (Richard Lau) [#55629](https://github.com/nodejs/node/pull/55629)
* \[[`4b192daac0`](https://github.com/nodejs/node/commit/4b192daac0)] - **deps**: update acorn to 8.14.0 (Node.js GitHub Bot) [#55699](https://github.com/nodejs/node/pull/55699)
* \[[`dfb764cbc6`](https://github.com/nodejs/node/commit/dfb764cbc6)] - **deps**: update sqlite to 3.47.0 (Node.js GitHub Bot) [#55557](https://github.com/nodejs/node/pull/55557)
* \[[`3477492588`](https://github.com/nodejs/node/commit/3477492588)] - **deps**: update amaro to 0.2.0 (Node.js GitHub Bot) [#55601](https://github.com/nodejs/node/pull/55601)
* \[[`3a1d490535`](https://github.com/nodejs/node/commit/3a1d490535)] - **deps**: update nghttp2 to 1.64.0 (Node.js GitHub Bot) [#55559](https://github.com/nodejs/node/pull/55559)
* \[[`50552fdc92`](https://github.com/nodejs/node/commit/50552fdc92)] - **deps**: update acorn to 8.13.0 (Node.js GitHub Bot) [#55558](https://github.com/nodejs/node/pull/55558)
* \[[`1b82013f06`](https://github.com/nodejs/node/commit/1b82013f06)] - **deps**: update undici to 6.20.1 (Node.js GitHub Bot) [#55503](https://github.com/nodejs/node/pull/55503)
* \[[`09060045b1`](https://github.com/nodejs/node/commit/09060045b1)] - **dns**: stop using deprecated `ares_query` (Aviv Keller) [#55430](https://github.com/nodejs/node/pull/55430)
* \[[`2d0914f337`](https://github.com/nodejs/node/commit/2d0914f337)] - **doc**: consolidate history table of `CustomEvent` (Edigleysson Silva) [#55758](https://github.com/nodejs/node/pull/55758)
* \[[`cbe09b579f`](https://github.com/nodejs/node/commit/cbe09b579f)] - **doc**: add path aliases typescript doc (Carlos Espa) [#55766](https://github.com/nodejs/node/pull/55766)
* \[[`89aa83842a`](https://github.com/nodejs/node/commit/89aa83842a)] - **doc**: add esm example in `path.md` (Aviv Keller) [#55745](https://github.com/nodejs/node/pull/55745)
* \[[`ee12431298`](https://github.com/nodejs/node/commit/ee12431298)] - **doc**: consistent use of word child process (Gireesh Punathil) [#55654](https://github.com/nodejs/node/pull/55654)
* \[[`20cb52d1d8`](https://github.com/nodejs/node/commit/20cb52d1d8)] - **doc**: clarity to available addon options (Preveen P) [#55715](https://github.com/nodejs/node/pull/55715)
* \[[`bffbaa13a2`](https://github.com/nodejs/node/commit/bffbaa13a2)] - **doc**: update `--max-semi-space-size` description (Joe Bowbeer) [#55495](https://github.com/nodejs/node/pull/55495)
* \[[`505ff199b6`](https://github.com/nodejs/node/commit/505ff199b6)] - **doc**: broken `PerformanceObserver` code sample (Dom Harrington) [#54227](https://github.com/nodejs/node/pull/54227)
* \[[`b8ca9d89f4`](https://github.com/nodejs/node/commit/b8ca9d89f4)] - **doc**: add write flag when open file as the demo code's intention (robberfree) [#54626](https://github.com/nodejs/node/pull/54626)
* \[[`6662752b62`](https://github.com/nodejs/node/commit/6662752b62)] - **doc**: add a note on console stream behavior (Gireesh Punathil) [#55616](https://github.com/nodejs/node/pull/55616)
* \[[`9743fa44ed`](https://github.com/nodejs/node/commit/9743fa44ed)] - **doc**: remove mention of ECDH-ES in crypto.diffieHellman (Filip Skokan) [#55611](https://github.com/nodejs/node/pull/55611)
* \[[`5de2567644`](https://github.com/nodejs/node/commit/5de2567644)] - **doc**: improve c++ embedder API doc (Gireesh Punathil) [#55597](https://github.com/nodejs/node/pull/55597)
* \[[`f355054ec7`](https://github.com/nodejs/node/commit/f355054ec7)] - **doc**: capitalize "MIT License" (Aviv Keller) [#55575](https://github.com/nodejs/node/pull/55575)
* \[[`fa61dced44`](https://github.com/nodejs/node/commit/fa61dced44)] - **doc**: move typescript support to active development (Marco Ippolito) [#55536](https://github.com/nodejs/node/pull/55536)
* \[[`f77bf65059`](https://github.com/nodejs/node/commit/f77bf65059)] - **doc**: add suggested tsconfig for type stripping (Marco Ippolito) [#55534](https://github.com/nodejs/node/pull/55534)
* \[[`f00ad27132`](https://github.com/nodejs/node/commit/f00ad27132)] - **doc**: add esm examples to node:string\_decoder (Alfredo González) [#55507](https://github.com/nodejs/node/pull/55507)
* \[[`9dcca5441b`](https://github.com/nodejs/node/commit/9dcca5441b)] - **doc**: add jazelly to collaborators (Jason Zhang) [#55531](https://github.com/nodejs/node/pull/55531)
* \[[`f628fc43cb`](https://github.com/nodejs/node/commit/f628fc43cb)] - **(SEMVER-MINOR)** **fs**: make `dirent.path` writable (Antoine du Hamel) [#55547](https://github.com/nodejs/node/pull/55547)
* \[[`dd9b6833c7`](https://github.com/nodejs/node/commit/dd9b6833c7)] - _**Revert**_ "**fs,win**: fix bug in paths with trailing slashes" (Rod Vagg) [#55527](https://github.com/nodejs/node/pull/55527)
* \[[`8d0526f1f4`](https://github.com/nodejs/node/commit/8d0526f1f4)] - **http**: add diagnostic channel `http.server.response.created` (Marco Ippolito) [#55622](https://github.com/nodejs/node/pull/55622)
* \[[`25b1422337`](https://github.com/nodejs/node/commit/25b1422337)] - **(SEMVER-MINOR)** **http**: add diagnostic channel `http.client.request.created` (Marco Ippolito) [#55586](https://github.com/nodejs/node/pull/55586)
* \[[`f92f20b930`](https://github.com/nodejs/node/commit/f92f20b930)] - **http**: don't emit error after destroy (Robert Nagy) [#55457](https://github.com/nodejs/node/pull/55457)
* \[[`137aa5c9f6`](https://github.com/nodejs/node/commit/137aa5c9f6)] - **http2**: fix client async storage persistence (Orgad Shaneh) [#55460](https://github.com/nodejs/node/pull/55460)
* \[[`d1965f9f5b`](https://github.com/nodejs/node/commit/d1965f9f5b)] - **lib**: implement webidl dictionary converter and use it in structuredClone (Jason Zhang) [#55489](https://github.com/nodejs/node/pull/55489)
* \[[`bf552fa3cc`](https://github.com/nodejs/node/commit/bf552fa3cc)] - **lib**: prefer number to string in webidl `type` function (Jason Zhang) [#55489](https://github.com/nodejs/node/pull/55489)
* \[[`7bfd295416`](https://github.com/nodejs/node/commit/7bfd295416)] - **meta**: bump actions/setup-python from 5.2.0 to 5.3.0 (dependabot\[bot]) [#55688](https://github.com/nodejs/node/pull/55688)
* \[[`21e3b7b2f4`](https://github.com/nodejs/node/commit/21e3b7b2f4)] - **meta**: bump actions/setup-node from 4.0.4 to 4.1.0 (dependabot\[bot]) [#55687](https://github.com/nodejs/node/pull/55687)
* \[[`2ae8d3b2ff`](https://github.com/nodejs/node/commit/2ae8d3b2ff)] - **meta**: bump rtCamp/action-slack-notify from 2.3.0 to 2.3.2 (dependabot\[bot]) [#55686](https://github.com/nodejs/node/pull/55686)
* \[[`42e6c47086`](https://github.com/nodejs/node/commit/42e6c47086)] - **meta**: bump actions/upload-artifact from 4.4.0 to 4.4.3 (dependabot\[bot]) [#55685](https://github.com/nodejs/node/pull/55685)
* \[[`9042e9acc9`](https://github.com/nodejs/node/commit/9042e9acc9)] - **meta**: bump actions/cache from 4.0.2 to 4.1.2 (dependabot\[bot]) [#55684](https://github.com/nodejs/node/pull/55684)
* \[[`5c2e4729cc`](https://github.com/nodejs/node/commit/5c2e4729cc)] - **meta**: bump actions/checkout from 4.2.0 to 4.2.2 (dependabot\[bot]) [#55683](https://github.com/nodejs/node/pull/55683)
* \[[`d79c8bf7a1`](https://github.com/nodejs/node/commit/d79c8bf7a1)] - **meta**: bump github/codeql-action from 3.26.10 to 3.27.0 (dependabot\[bot]) [#55682](https://github.com/nodejs/node/pull/55682)
* \[[`d0ea9815f6`](https://github.com/nodejs/node/commit/d0ea9815f6)] - **meta**: make review-wanted message minimal (Aviv Keller) [#55607](https://github.com/nodejs/node/pull/55607)
* \[[`b1ca7ab0a1`](https://github.com/nodejs/node/commit/b1ca7ab0a1)] - **meta**: show PR/issue title on review-wanted (Aviv Keller) [#55606](https://github.com/nodejs/node/pull/55606)
* \[[`19b1edfc5c`](https://github.com/nodejs/node/commit/19b1edfc5c)] - **module**: simplify --inspect-brk handling (Joyee Cheung) [#55679](https://github.com/nodejs/node/pull/55679)
* \[[`869e88c6a8`](https://github.com/nodejs/node/commit/869e88c6a8)] - **module**: simplify `findPackageJSON` implementation (Antoine du Hamel) [#55543](https://github.com/nodejs/node/pull/55543)
* \[[`56c46ab686`](https://github.com/nodejs/node/commit/56c46ab686)] - **module**: unify TypeScript and .mjs handling in CommonJS (Joyee Cheung) [#55590](https://github.com/nodejs/node/pull/55590)
* \[[`d3be3da6f8`](https://github.com/nodejs/node/commit/d3be3da6f8)] - **module**: fix error thrown from require(esm) hitting TLA repeatedly (Joyee Cheung) [#55520](https://github.com/nodejs/node/pull/55520)
* \[[`b3971bbf13`](https://github.com/nodejs/node/commit/b3971bbf13)] - **module**: trim off internal stack frames for require(esm) warnings (Joyee Cheung) [#55496](https://github.com/nodejs/node/pull/55496)
* \[[`a9e08cfe6d`](https://github.com/nodejs/node/commit/a9e08cfe6d)] - **module**: allow ESM that failed to be required to be re-imported (Joyee Cheung) [#55502](https://github.com/nodejs/node/pull/55502)
* \[[`adda37f00c`](https://github.com/nodejs/node/commit/adda37f00c)] - **(SEMVER-MINOR)** **module**: add `findPackageJSON` util (Jacob Smith) [#55412](https://github.com/nodejs/node/pull/55412)
* \[[`69dd1e13c3`](https://github.com/nodejs/node/commit/69dd1e13c3)] - **(SEMVER-MINOR)** **module**: add module.stripTypeScriptTypes (Marco Ippolito) [#55282](https://github.com/nodejs/node/pull/55282)
* \[[`6ab59c81b6`](https://github.com/nodejs/node/commit/6ab59c81b6)] - **os**: improve path check with direct index access (Mert Can Altin) [#55434](https://github.com/nodejs/node/pull/55434)
* \[[`038ac01d26`](https://github.com/nodejs/node/commit/038ac01d26)] - **path,win**: fix bug in resolve and normalize (Hüseyin Açacak) [#55623](https://github.com/nodejs/node/pull/55623)
* \[[`7aa250afda`](https://github.com/nodejs/node/commit/7aa250afda)] - **sqlite**: improve error handling using MaybeLocal (Tobias Nießen) [#55571](https://github.com/nodejs/node/pull/55571)
* \[[`2ec4ae7c16`](https://github.com/nodejs/node/commit/2ec4ae7c16)] - **sqlite**: add readOnly option (Tobias Nießen) [#55567](https://github.com/nodejs/node/pull/55567)
* \[[`88c7f5b489`](https://github.com/nodejs/node/commit/88c7f5b489)] - **sqlite**: refactor open options (Tobias Nießen) [#55442](https://github.com/nodejs/node/pull/55442)
* \[[`7853462a61`](https://github.com/nodejs/node/commit/7853462a61)] - **src**: provide workaround for container-overflow (Daniel Lemire) [#55591](https://github.com/nodejs/node/pull/55591)
* \[[`0302efe4b2`](https://github.com/nodejs/node/commit/0302efe4b2)] - **src**: move more key related stuff to ncrypto (James M Snell) [#55368](https://github.com/nodejs/node/pull/55368)
* \[[`d26dedf41d`](https://github.com/nodejs/node/commit/d26dedf41d)] - **src**: refactor ECDHBitsJob signature (Filip Skokan) [#55610](https://github.com/nodejs/node/pull/55610)
* \[[`4c34891454`](https://github.com/nodejs/node/commit/4c34891454)] - **src**: fix dns crash when failed to create NodeAresTask (theanarkh) [#55521](https://github.com/nodejs/node/pull/55521)
* \[[`467618418a`](https://github.com/nodejs/node/commit/467618418a)] - **src**: use NewFromUtf8Literal in NODE\_DEFINE\_CONSTANT (Charles Kerr) [#55581](https://github.com/nodejs/node/pull/55581)
* \[[`016baaebbe`](https://github.com/nodejs/node/commit/016baaebbe)] - **src**: do not run IsWindowsBatchFile on non-windows (Yagiz Nizipli) [#55560](https://github.com/nodejs/node/pull/55560)
* \[[`efa142c108`](https://github.com/nodejs/node/commit/efa142c108)] - **src**: migrate `String::Value` to `String::ValueView` (Aviv Keller) [#55458](https://github.com/nodejs/node/pull/55458)
* \[[`cfa4d960c8`](https://github.com/nodejs/node/commit/cfa4d960c8)] - **src,lib**: optimize nodeTiming.uvMetricsInfo (RafaelGSS) [#55614](https://github.com/nodejs/node/pull/55614)
* \[[`19da4de475`](https://github.com/nodejs/node/commit/19da4de475)] - **test**: update `performance-timeline` wpt (RedYetiDev) [#55197](https://github.com/nodejs/node/pull/55197)
* \[[`10b68ed975`](https://github.com/nodejs/node/commit/10b68ed975)] - **test**: ignore unrelated events in FW watch tests (Carlos Espa) [#55605](https://github.com/nodejs/node/pull/55605)
* \[[`7d93c0c3ae`](https://github.com/nodejs/node/commit/7d93c0c3ae)] - **test**: refactor some esm tests (Antoine du Hamel) [#55472](https://github.com/nodejs/node/pull/55472)
* \[[`815e2524a6`](https://github.com/nodejs/node/commit/815e2524a6)] - **test**: split up test-runner-mock-timers test (Julian Gassner) [#55506](https://github.com/nodejs/node/pull/55506)
* \[[`6aa797de4e`](https://github.com/nodejs/node/commit/6aa797de4e)] - **test**: remove unneeded listeners (Luigi Pinca) [#55486](https://github.com/nodejs/node/pull/55486)
* \[[`649d767a40`](https://github.com/nodejs/node/commit/649d767a40)] - **test**: increase coverage of `pathToFileURL` (Antoine du Hamel) [#55493](https://github.com/nodejs/node/pull/55493)
* \[[`71cc20a3a5`](https://github.com/nodejs/node/commit/71cc20a3a5)] - **test**: avoid `apply()` calls with large amount of elements (Livia Medeiros) [#55501](https://github.com/nodejs/node/pull/55501)
* \[[`2d19614020`](https://github.com/nodejs/node/commit/2d19614020)] - **test**: increase test coverage for `http.OutgoingMessage.appendHeader()` (Juan José) [#55467](https://github.com/nodejs/node/pull/55467)
* \[[`aebf676569`](https://github.com/nodejs/node/commit/aebf676569)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#55703](https://github.com/nodejs/node/pull/55703)
* \[[`53a7d8e75b`](https://github.com/nodejs/node/commit/53a7d8e75b)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#55512](https://github.com/nodejs/node/pull/55512)
* \[[`0ea74f3d02`](https://github.com/nodejs/node/commit/0ea74f3d02)] - **test,crypto**: make crypto tests work with BoringSSL (Shelley Vohr) [#55491](https://github.com/nodejs/node/pull/55491)
* \[[`3234dc6100`](https://github.com/nodejs/node/commit/3234dc6100)] - **test\_runner**: pass `options` directly to `TestCoverage` (Aviv Keller) [#55578](https://github.com/nodejs/node/pull/55578)
* \[[`15028dd073`](https://github.com/nodejs/node/commit/15028dd073)] - **tools**: update ESLint to 9.14.0 (dependabot\[bot]) [#55689](https://github.com/nodejs/node/pull/55689)
* \[[`961cbc9c0f`](https://github.com/nodejs/node/commit/961cbc9c0f)] - **tools**: use `util.parseArgs` in `lint-md` (Aviv Keller) [#55694](https://github.com/nodejs/node/pull/55694)
* \[[`8fc962f1af`](https://github.com/nodejs/node/commit/8fc962f1af)] - **tools**: fix root certificate updater (Richard Lau) [#55681](https://github.com/nodejs/node/pull/55681)
* \[[`d0b2d6be84`](https://github.com/nodejs/node/commit/d0b2d6be84)] - **tools**: compact jq output in daily-wpt-fyi.yml action (Filip Skokan) [#55695](https://github.com/nodejs/node/pull/55695)
* \[[`cba05cda38`](https://github.com/nodejs/node/commit/cba05cda38)] - **tools**: run daily WPT.fyi report on all supported releases (Filip Skokan) [#55619](https://github.com/nodejs/node/pull/55619)
* \[[`7ce7eab324`](https://github.com/nodejs/node/commit/7ce7eab324)] - **tools**: lint README lists more strictly (Antoine du Hamel) [#55625](https://github.com/nodejs/node/pull/55625)
* \[[`c2fcda45ca`](https://github.com/nodejs/node/commit/c2fcda45ca)] - **typings**: fix `ModulesBinding` types (Antoine du Hamel) [#55549](https://github.com/nodejs/node/pull/55549)
* \[[`2b9928561d`](https://github.com/nodejs/node/commit/2b9928561d)] - **url**: refactor `pathToFileURL` to native (Antoine du Hamel) [#55476](https://github.com/nodejs/node/pull/55476)
* \[[`4129bc72e2`](https://github.com/nodejs/node/commit/4129bc72e2)] - **util**: do not catch on circular `@@toStringTag` errors (Aviv Keller) [#55544](https://github.com/nodejs/node/pull/55544)

<a id="23.1.0"></a>

## 2024-10-24, Version 23.1.0 (Current), @aduh95

### Notable Changes

#### `Buffer` now work with resizable `ArrayBuffer`

When a `Buffer` is created using a resizable `ArrayBuffer`, the `Buffer` length
will now correctly change as the underlying `ArrayBuffer` size is changed.

```js
const ab = new ArrayBuffer(10, { maxByteLength: 20 });
const buffer = Buffer.from(ab);
console.log(buffer.byteLength); // 10
ab.resize(15);
console.log(buffer.byteLength); // 15
ab.resize(5);
console.log(buffer.byteLength); // 5
```

Contributed by James M Snell in [#55377](https://github.com/nodejs/node/pull/55377).

#### `MockTimers` test runner API is now stable

`MockTimers`, introduced in April 2023, has just reached **stable status**. This
API provides comprehensive support for mocking `Date` and all major timers in
Node.js, including `setTimeout`, `setInterval`, and `setImmediate`, both from
the `node:timers`, `node:timers/promises` modules and global objects. After
months of refinement, developers can now fully rely on `MockTimers` for testing
time-based operations with confidence, ensuring better control over asynchronous
behavior in their Node.js applications.

Example usage with initial `Date` object as time set:

```mjs
import { mock } from 'node:test';
mock.timers.enable({ apis: ['Date'], now: new Date('1970-01-01') });
```

Contributed by Erick Wendel in [#55398](https://github.com/nodejs/node/pull/55398).

#### JSON modules and import attributes are now stable

The two proposals reached stage 4 of the TC39 process, at the October 2024
meeting. The Node.js implementation already matches exactly the semantics
required by the proposals.

Contributed by Nicolò Ribaudo by [#55333](https://github.com/nodejs/node/pull/55333).

#### Other Notable Changes

* \[[`4ba31b7f20`](https://github.com/nodejs/node/commit/4ba31b7f20)] - **(SEMVER-MINOR)** **assert**: make `assertion_error` use Myers diff algorithm (Giovanni Bucci) [#54862](https://github.com/nodejs/node/pull/54862)
* \[[`dcbc5fbe65`](https://github.com/nodejs/node/commit/dcbc5fbe65)] - **(SEMVER-MINOR)** **lib**: add `UV_UDP_REUSEPORT` for udp (theanarkh) [#55403](https://github.com/nodejs/node/pull/55403)
* \[[`ec867ac7ce`](https://github.com/nodejs/node/commit/ec867ac7ce)] - **(SEMVER-MINOR)** **net**: add `UV_TCP_REUSEPORT` for tcp (theanarkh) [#55408](https://github.com/nodejs/node/pull/55408)

### Commits

* \[[`4ba31b7f20`](https://github.com/nodejs/node/commit/4ba31b7f20)] - **(SEMVER-MINOR)** **assert**: make assertion\_error use Myers diff algorithm (Giovanni Bucci) [#54862](https://github.com/nodejs/node/pull/54862)
* \[[`fe667bea28`](https://github.com/nodejs/node/commit/fe667bea28)] - **assert**: fix deepEqual always return true on URL (Xuguang Mei) [#50853](https://github.com/nodejs/node/pull/50853)
* \[[`aca03d9083`](https://github.com/nodejs/node/commit/aca03d9083)] - **benchmark**: add --runs support to run.js (Rafael Gonzaga) [#55158](https://github.com/nodejs/node/pull/55158)
* \[[`c5abf50692`](https://github.com/nodejs/node/commit/c5abf50692)] - **benchmark**: adjust byte size for buffer-copy (Rafael Gonzaga) [#55295](https://github.com/nodejs/node/pull/55295)
* \[[`d3618b2334`](https://github.com/nodejs/node/commit/d3618b2334)] - **benchmark**: adjust config for deepEqual object (Rafael Gonzaga) [#55254](https://github.com/nodejs/node/pull/55254)
* \[[`c05582da3d`](https://github.com/nodejs/node/commit/c05582da3d)] - **(SEMVER-MINOR)** **buffer**: make Buffer work with resizable ArrayBuffer (James M Snell) [#55377](https://github.com/nodejs/node/pull/55377)
* \[[`194bb0fca5`](https://github.com/nodejs/node/commit/194bb0fca5)] - **build**: fix GN build for cares/uv deps (Cheng) [#55477](https://github.com/nodejs/node/pull/55477)
* \[[`8eb5359592`](https://github.com/nodejs/node/commit/8eb5359592)] - **build**: fix uninstall script for AIX 7.1 (Cloorc) [#55438](https://github.com/nodejs/node/pull/55438)
* \[[`32f7d5ad1c`](https://github.com/nodejs/node/commit/32f7d5ad1c)] - **build**: conditionally compile bundled sqlite (Richard Lau) [#55409](https://github.com/nodejs/node/pull/55409)
* \[[`2147e496e7`](https://github.com/nodejs/node/commit/2147e496e7)] - **build**: tidy up cares.gyp (Richard Lau) [#55445](https://github.com/nodejs/node/pull/55445)
* \[[`2beae50c77`](https://github.com/nodejs/node/commit/2beae50c77)] - **build**: synchronize list of c-ares source files (Richard Lau) [#55445](https://github.com/nodejs/node/pull/55445)
* \[[`f48d30eb9f`](https://github.com/nodejs/node/commit/f48d30eb9f)] - **build**: fix path concatenation (Mohammed Keyvanzadeh) [#55387](https://github.com/nodejs/node/pull/55387)
* \[[`d42522eec5`](https://github.com/nodejs/node/commit/d42522eec5)] - **build**: fix make errors that occur in Makefile (minkyu\_kim) [#55287](https://github.com/nodejs/node/pull/55287)
* \[[`52da293471`](https://github.com/nodejs/node/commit/52da293471)] - **cli**: add `--heap-prof` flag available to `NODE_OPTIONS` (Juan José) [#54259](https://github.com/nodejs/node/pull/54259)
* \[[`adead26815`](https://github.com/nodejs/node/commit/adead26815)] - **crypto**: include openssl/rand.h explicitly (Shelley Vohr) [#55425](https://github.com/nodejs/node/pull/55425)
* \[[`df2f1adf9e`](https://github.com/nodejs/node/commit/df2f1adf9e)] - **deps**: V8: cherry-pick f915fa4c9f41 (Chengzhong Wu) [#55484](https://github.com/nodejs/node/pull/55484)
* \[[`bfc10a975f`](https://github.com/nodejs/node/commit/bfc10a975f)] - **deps**: update googletest to df1544b (Node.js GitHub Bot) [#55465](https://github.com/nodejs/node/pull/55465)
* \[[`45ef1809bd`](https://github.com/nodejs/node/commit/45ef1809bd)] - **deps**: update c-ares to v1.34.2 (Node.js GitHub Bot) [#55463](https://github.com/nodejs/node/pull/55463)
* \[[`c2b5ebfeca`](https://github.com/nodejs/node/commit/c2b5ebfeca)] - **deps**: update ada to 2.9.1 (Node.js GitHub Bot) [#54679](https://github.com/nodejs/node/pull/54679)
* \[[`903863cafa`](https://github.com/nodejs/node/commit/903863cafa)] - **deps**: update simdutf to 5.6.0 (Node.js GitHub Bot) [#55379](https://github.com/nodejs/node/pull/55379)
* \[[`008fb5f7f4`](https://github.com/nodejs/node/commit/008fb5f7f4)] - **deps**: patch V8 to 12.9.202.28 (Node.js GitHub Bot) [#55371](https://github.com/nodejs/node/pull/55371)
* \[[`8b282228ae`](https://github.com/nodejs/node/commit/8b282228ae)] - **deps**: update c-ares to v1.34.1 (Node.js GitHub Bot) [#55369](https://github.com/nodejs/node/pull/55369)
* \[[`54d55f2337`](https://github.com/nodejs/node/commit/54d55f2337)] - _**Revert**_ "**deps**: disable io\_uring support in libuv by default" (Santiago Gimeno) [#55114](https://github.com/nodejs/node/pull/55114)
* \[[`bfb3c621c4`](https://github.com/nodejs/node/commit/bfb3c621c4)] - **deps**: update libuv to 1.49.1 (Santiago Gimeno) [#55114](https://github.com/nodejs/node/pull/55114)
* \[[`055d2b8919`](https://github.com/nodejs/node/commit/055d2b8919)] - **deps**: update amaro to 0.1.9 (Node.js GitHub Bot) [#55348](https://github.com/nodejs/node/pull/55348)
* \[[`c028d21b44`](https://github.com/nodejs/node/commit/c028d21b44)] - **diagnostics\_channel**: fix unsubscribe during publish (simon-id) [#55116](https://github.com/nodejs/node/pull/55116)
* \[[`b4b6ddb777`](https://github.com/nodejs/node/commit/b4b6ddb777)] - **dns**: honor the order option (Luigi Pinca) [#55392](https://github.com/nodejs/node/pull/55392)
* \[[`37352cef7f`](https://github.com/nodejs/node/commit/37352cef7f)] - **doc**: changed the command used to verify SHASUMS256 (adriancuadrado) [#55420](https://github.com/nodejs/node/pull/55420)
* \[[`66bcf4c065`](https://github.com/nodejs/node/commit/66bcf4c065)] - **doc**: move dual package shipping docs to separate repo (Joyee Cheung) [#55444](https://github.com/nodejs/node/pull/55444)
* \[[`04b41bda03`](https://github.com/nodejs/node/commit/04b41bda03)] - **doc**: add note about stdio streams in child\_process (Ederin (Ed) Igharoro) [#55322](https://github.com/nodejs/node/pull/55322)
* \[[`689d3a3e41`](https://github.com/nodejs/node/commit/689d3a3e41)] - **doc**: add `isBigIntObject` to documentation (leviscar) [#55450](https://github.com/nodejs/node/pull/55450)
* \[[`784c825a27`](https://github.com/nodejs/node/commit/784c825a27)] - **doc**: remove outdated remarks about `highWaterMark` in fs (Ian Kerins) [#55462](https://github.com/nodejs/node/pull/55462)
* \[[`1ec25e8573`](https://github.com/nodejs/node/commit/1ec25e8573)] - **doc**: move Danielle Adams key to old gpg keys (RafaelGSS) [#55399](https://github.com/nodejs/node/pull/55399)
* \[[`7d5bb097eb`](https://github.com/nodejs/node/commit/7d5bb097eb)] - **doc**: move Bryan English key to old gpg keys (RafaelGSS) [#55399](https://github.com/nodejs/node/pull/55399)
* \[[`2967471f67`](https://github.com/nodejs/node/commit/2967471f67)] - **doc**: move Beth Griggs keys to old gpg keys (RafaelGSS) [#55399](https://github.com/nodejs/node/pull/55399)
* \[[`0be3a7505f`](https://github.com/nodejs/node/commit/0be3a7505f)] - **doc**: add changelog for mocktimers (Erick Wendel) [#55398](https://github.com/nodejs/node/pull/55398)
* \[[`e15f779794`](https://github.com/nodejs/node/commit/e15f779794)] - **doc**: spell out condition restrictions (Jan Martin) [#55187](https://github.com/nodejs/node/pull/55187)
* \[[`c3f2216a7d`](https://github.com/nodejs/node/commit/c3f2216a7d)] - **doc**: add instructions for WinGet build (Hüseyin Açacak) [#55356](https://github.com/nodejs/node/pull/55356)
* \[[`bdc2c3bb94`](https://github.com/nodejs/node/commit/bdc2c3bb94)] - **doc**: add missing return values in buffer docs (Karl Horky) [#55273](https://github.com/nodejs/node/pull/55273)
* \[[`41f68f59af`](https://github.com/nodejs/node/commit/41f68f59af)] - **doc**: fix ambasador markdown list (Rafael Gonzaga) [#55361](https://github.com/nodejs/node/pull/55361)
* \[[`bbd5318729`](https://github.com/nodejs/node/commit/bbd5318729)] - **esm**: add a fallback when importer in not a file (Antoine du Hamel) [#55471](https://github.com/nodejs/node/pull/55471)
* \[[`22d77773fd`](https://github.com/nodejs/node/commit/22d77773fd)] - **esm**: fix inconsistency with `importAssertion` in `resolve` hook (Wei Zhu) [#55365](https://github.com/nodejs/node/pull/55365)
* \[[`48bb87b059`](https://github.com/nodejs/node/commit/48bb87b059)] - **esm**: mark import attributes and JSON module as stable (Nicolò Ribaudo) [#55333](https://github.com/nodejs/node/pull/55333)
* \[[`8ceefebaf2`](https://github.com/nodejs/node/commit/8ceefebaf2)] - **events**: optimize EventTarget.addEventListener (Robert Nagy) [#55312](https://github.com/nodejs/node/pull/55312)
* \[[`45f960cab6`](https://github.com/nodejs/node/commit/45f960cab6)] - **fs**: pass correct path to `DirentFromStats` during `glob` (Aviv Keller) [#55071](https://github.com/nodejs/node/pull/55071)
* \[[`d9494a7641`](https://github.com/nodejs/node/commit/d9494a7641)] - **fs**: use `wstring` on Windows paths (jazelly) [#55171](https://github.com/nodejs/node/pull/55171)
* \[[`0f1d13e359`](https://github.com/nodejs/node/commit/0f1d13e359)] - **lib**: ensure FORCE\_COLOR forces color output in non-TTY environments (Pietro Marchini) [#55404](https://github.com/nodejs/node/pull/55404)
* \[[`dcbc5fbe65`](https://github.com/nodejs/node/commit/dcbc5fbe65)] - **(SEMVER-MINOR)** **lib**: add UV\_UDP\_REUSEPORT for udp (theanarkh) [#55403](https://github.com/nodejs/node/pull/55403)
* \[[`714f272423`](https://github.com/nodejs/node/commit/714f272423)] - **lib**: remove startsWith/endsWith primordials for char checks (Gürgün Dayıoğlu) [#55407](https://github.com/nodejs/node/pull/55407)
* \[[`4e5c90bb41`](https://github.com/nodejs/node/commit/4e5c90bb41)] - **lib**: replace `createDeferredPromise` util with `Promise.withResolvers` (Yagiz Nizipli) [#54836](https://github.com/nodejs/node/pull/54836)
* \[[`db18aca47a`](https://github.com/nodejs/node/commit/db18aca47a)] - **lib**: add flag to drop connection when running in cluster mode (theanarkh) [#54927](https://github.com/nodejs/node/pull/54927)
* \[[`dd848f2d1e`](https://github.com/nodejs/node/commit/dd848f2d1e)] - **lib**: test\_runner#mock:timers respeced timeout\_max behaviour (BadKey) [#55375](https://github.com/nodejs/node/pull/55375)
* \[[`a9473bb8e3`](https://github.com/nodejs/node/commit/a9473bb8e3)] - **lib**: remove settled dependant signals when they are GCed (Edigleysson Silva (Edy)) [#55354](https://github.com/nodejs/node/pull/55354)
* \[[`07ad987aa1`](https://github.com/nodejs/node/commit/07ad987aa1)] - **lib**: convert transfer sequence to array in js (Jason Zhang) [#55317](https://github.com/nodejs/node/pull/55317)
* \[[`d54d3b24c3`](https://github.com/nodejs/node/commit/d54d3b24c3)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#55381](https://github.com/nodejs/node/pull/55381)
* \[[`12d709bd27`](https://github.com/nodejs/node/commit/12d709bd27)] - **meta**: assign CODEOWNERS for /deps/ncrypto/\* (Filip Skokan) [#55426](https://github.com/nodejs/node/pull/55426)
* \[[`0130780eec`](https://github.com/nodejs/node/commit/0130780eec)] - **meta**: change color to blue notify review-wanted (Rafael Gonzaga) [#55423](https://github.com/nodejs/node/pull/55423)
* \[[`335a507027`](https://github.com/nodejs/node/commit/335a507027)] - **meta**: bump codecov/codecov-action from 4.5.0 to 4.6.0 (dependabot\[bot]) [#55222](https://github.com/nodejs/node/pull/55222)
* \[[`5ffc721d09`](https://github.com/nodejs/node/commit/5ffc721d09)] - **meta**: bump github/codeql-action from 3.26.6 to 3.26.10 (dependabot\[bot]) [#55221](https://github.com/nodejs/node/pull/55221)
* \[[`d9fde2c45b`](https://github.com/nodejs/node/commit/d9fde2c45b)] - **meta**: bump step-security/harden-runner from 2.9.1 to 2.10.1 (dependabot\[bot]) [#55220](https://github.com/nodejs/node/pull/55220)
* \[[`2c960a212e`](https://github.com/nodejs/node/commit/2c960a212e)] - **module**: include module information in require(esm) warning (Joyee Cheung) [#55397](https://github.com/nodejs/node/pull/55397)
* \[[`a12dbf03d9`](https://github.com/nodejs/node/commit/a12dbf03d9)] - **module**: simplify ts under node\_modules check (Marco Ippolito) [#55440](https://github.com/nodejs/node/pull/55440)
* \[[`ec867ac7ce`](https://github.com/nodejs/node/commit/ec867ac7ce)] - **(SEMVER-MINOR)** **net**: add UV\_TCP\_REUSEPORT for tcp (theanarkh) [#55408](https://github.com/nodejs/node/pull/55408)
* \[[`9e320279a2`](https://github.com/nodejs/node/commit/9e320279a2)] - _**Revert**_ "**path**: fix bugs and inconsistencies" (Aviv Keller) [#55414](https://github.com/nodejs/node/pull/55414)
* \[[`1ce8928db3`](https://github.com/nodejs/node/commit/1ce8928db3)] - **sqlite**: cache column names in stmt.all() (Fedor Indutny) [#55373](https://github.com/nodejs/node/pull/55373)
* \[[`cc775d314a`](https://github.com/nodejs/node/commit/cc775d314a)] - **src**: switch from `Get/SetPrototype` to `Get/SetPrototypeV2` (Aviv Keller) [#55453](https://github.com/nodejs/node/pull/55453)
* \[[`89c96ade53`](https://github.com/nodejs/node/commit/89c96ade53)] - **src**: remove icu based `ToASCII` and `ToUnicode` (Yagiz Nizipli) [#55156](https://github.com/nodejs/node/pull/55156)
* \[[`57dbbf8402`](https://github.com/nodejs/node/commit/57dbbf8402)] - **src**: fix winapi\_strerror error string (Hüseyin Açacak) [#55207](https://github.com/nodejs/node/pull/55207)
* \[[`a490bb8745`](https://github.com/nodejs/node/commit/a490bb8745)] - **src**: remove uv\_\_node\_patch\_is\_using\_io\_uring (Santiago Gimeno) [#55114](https://github.com/nodejs/node/pull/55114)
* \[[`0da1632937`](https://github.com/nodejs/node/commit/0da1632937)] - **src,lib**: introduce `util.getSystemErrorMessage(err)` (Juan José) [#54075](https://github.com/nodejs/node/pull/54075)
* \[[`6764273127`](https://github.com/nodejs/node/commit/6764273127)] - **stream**: propagate AbortSignal reason (Marvin ROGER) [#55473](https://github.com/nodejs/node/pull/55473)
* \[[`4dc2791cdd`](https://github.com/nodejs/node/commit/4dc2791cdd)] - **test**: add repl preview timeout test (Chengzhong Wu) [#55484](https://github.com/nodejs/node/pull/55484)
* \[[`8634e054d4`](https://github.com/nodejs/node/commit/8634e054d4)] - **test**: make test-node-output-v8-warning more flexible (Shelley Vohr) [#55401](https://github.com/nodejs/node/pull/55401)
* \[[`6c8564b55d`](https://github.com/nodejs/node/commit/6c8564b55d)] - **test**: fix addons and node-api test assumptions (Antoine du Hamel) [#55441](https://github.com/nodejs/node/pull/55441)
* \[[`94e863cdb7`](https://github.com/nodejs/node/commit/94e863cdb7)] - **test**: update wpt test for webmessaging/broadcastchannel (devstone) [#55205](https://github.com/nodejs/node/pull/55205)
* \[[`c10c6715cd`](https://github.com/nodejs/node/commit/c10c6715cd)] - **test**: deflake `test-cluster-shared-handle-bind-privileged-port` (Aviv Keller) [#55378](https://github.com/nodejs/node/pull/55378)
* \[[`6f7379a048`](https://github.com/nodejs/node/commit/6f7379a048)] - **test**: fix invalid `file:` URL in `test-fs-path-dir` (Antoine du Hamel) [#55454](https://github.com/nodejs/node/pull/55454)
* \[[`dd5a08d022`](https://github.com/nodejs/node/commit/dd5a08d022)] - **test**: update `console` wpt (Aviv Keller) [#55192](https://github.com/nodejs/node/pull/55192)
* \[[`9b7b4a6b25`](https://github.com/nodejs/node/commit/9b7b4a6b25)] - **test**: remove duplicate tests (Luigi Pinca) [#55393](https://github.com/nodejs/node/pull/55393)
* \[[`eb2fab3da1`](https://github.com/nodejs/node/commit/eb2fab3da1)] - **test**: update test\_util.cc for coverage (minkyu\_kim) [#55291](https://github.com/nodejs/node/pull/55291)
* \[[`59923d137e`](https://github.com/nodejs/node/commit/59923d137e)] - **test**: update `compression` wpt (Aviv Keller) [#55191](https://github.com/nodejs/node/pull/55191)
* \[[`1b63a822ac`](https://github.com/nodejs/node/commit/1b63a822ac)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#55427](https://github.com/nodejs/node/pull/55427)
* \[[`97c6448f63`](https://github.com/nodejs/node/commit/97c6448f63)] - **test\_runner**: mark mockTimers as stable (Erick Wendel) [#55398](https://github.com/nodejs/node/pull/55398)
* \[[`69ee56aacd`](https://github.com/nodejs/node/commit/69ee56aacd)] - **test\_runner**: add support for scheduler.wait on mock timers (Erick Wendel) [#55244](https://github.com/nodejs/node/pull/55244)
* \[[`d9f0407cf6`](https://github.com/nodejs/node/commit/d9f0407cf6)] - **test\_runner**: require `--enable-source-maps` for sourcemap coverage (Aviv Keller) [#55359](https://github.com/nodejs/node/pull/55359)
* \[[`2ac2c5a7e7`](https://github.com/nodejs/node/commit/2ac2c5a7e7)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#55470](https://github.com/nodejs/node/pull/55470)
* \[[`10f6b90f7d`](https://github.com/nodejs/node/commit/10f6b90f7d)] - **tools**: update gyp-next to 0.18.3 (Node.js GitHub Bot) [#55464](https://github.com/nodejs/node/pull/55464)
* \[[`65936a8bb6`](https://github.com/nodejs/node/commit/65936a8bb6)] - **tools**: add script to synch c-ares source lists (Richard Lau) [#55445](https://github.com/nodejs/node/pull/55445)
* \[[`1da4168486`](https://github.com/nodejs/node/commit/1da4168486)] - **tools**: add `polyfilled` option to `prefer-primordials` rule (Antoine du Hamel) [#55318](https://github.com/nodejs/node/pull/55318)
* \[[`3b2b3a8df2`](https://github.com/nodejs/node/commit/3b2b3a8df2)] - **tools**: fix typos (Nathan Baulch) [#55061](https://github.com/nodejs/node/pull/55061)
* \[[`736c085a5d`](https://github.com/nodejs/node/commit/736c085a5d)] - **typings**: add missing type of `ArrayBufferPrototypeGetByteLength` (Wuli Zuo) [#55439](https://github.com/nodejs/node/pull/55439)
* \[[`7b3e38b855`](https://github.com/nodejs/node/commit/7b3e38b855)] - **url**: handle "unsafe" characters properly in `pathToFileURL` (Antoine du Hamel) [#54545](https://github.com/nodejs/node/pull/54545)

<a id="23.0.0"></a>

## 2024-10-16, Version 23.0.0 (Current), @RafaelGSS

We’re excited to announce the release of Node.js 23! Key highlights include:

* Enabling `require(esm)` by default for Node.js applications
* Removing support for Windows 32-bit systems
* Stabilizing the `node --run` command
* Enhancements to the test runner, including glob pattern support for coverage files

Node.js 23 will replace Node.js 22 as the ‘Current’ release line when Node.js 22 enters long-term support (LTS) later this month.
According to the release schedule, Node.js 23 will remain the ‘Current’ release for the next six months, until April 2025.

### Other Notable Changes

* \[[`7ad0cc3e57`](https://github.com/nodejs/node/commit/7ad0cc3e57)] - **(SEMVER-MAJOR)** **build**: remove support for 32-bit Windows (Michaël Zasso) [#53184](https://github.com/nodejs/node/pull/53184)
* \[[`83eb4f2855`](https://github.com/nodejs/node/commit/83eb4f2855)] - **(SEMVER-MINOR)** **deps**: V8: cherry-pick cd10ad7cdbe5 (Joyee Cheung) [#52535](https://github.com/nodejs/node/pull/52535)
* \[[`b8493a5789`](https://github.com/nodejs/node/commit/b8493a5789)] - **doc**: add abmusse to collaborators (Abdirahim Musse) [#55086](https://github.com/nodejs/node/pull/55086)
* \[[`7fab6e8885`](https://github.com/nodejs/node/commit/7fab6e8885)] - **(SEMVER-MAJOR)** **doc**: use gcc 12 on AIX for Node.js >=23 (Richard Lau) [#54338](https://github.com/nodejs/node/pull/54338)
* \[[`d473606040`](https://github.com/nodejs/node/commit/d473606040)] - **(SEMVER-MINOR)** **lib**: propagate aborted state to dependent signals before firing events (jazelly) [#54826](https://github.com/nodejs/node/pull/54826)
* \[[`06206af181`](https://github.com/nodejs/node/commit/06206af181)] - **(SEMVER-MINOR)** **module**: unflag --experimental-require-module (Joyee Cheung) [#55085](https://github.com/nodejs/node/pull/55085)
* \[[`0b9249e335`](https://github.com/nodejs/node/commit/0b9249e335)] - **(SEMVER-MINOR)** **module**: implement the "module-sync" exports condition (Joyee Cheung) [#54648](https://github.com/nodejs/node/pull/54648)
* \[[`92a25abca9`](https://github.com/nodejs/node/commit/92a25abca9)] - **(SEMVER-MINOR)** **path**: add `matchGlob` method (Aviv Keller) [#52881](https://github.com/nodejs/node/pull/52881)
* \[[`12dd4c7575`](https://github.com/nodejs/node/commit/12dd4c7575)] - **src**: mark node --run as stable (Yagiz Nizipli) [#53763](https://github.com/nodejs/node/pull/53763)
* \[[`4174b73153`](https://github.com/nodejs/node/commit/4174b73153)] - **test**: support glob matching coverage files (Aviv Keller) [#53553](https://github.com/nodejs/node/pull/53553)

### Semver-Major Commits

* \[[`764b13d75c`](https://github.com/nodejs/node/commit/764b13d75c)] - **(SEMVER-MAJOR)** **assert,util**: change WeakMap and WeakSet comparison handling (Cristian Barlutiu) [#53495](https://github.com/nodejs/node/pull/53495)
* \[[`3800d60c66`](https://github.com/nodejs/node/commit/3800d60c66)] - **(SEMVER-MAJOR)** **buffer**: throw when writing beyond buffer" (Robert Nagy) [#54588](https://github.com/nodejs/node/pull/54588)
* \[[`17fd32790a`](https://github.com/nodejs/node/commit/17fd32790a)] - **(SEMVER-MAJOR)** **buffer**: make File cloneable (Matthew Aitken) [#47613](https://github.com/nodejs/node/pull/47613)
* \[[`f68d7d2acc`](https://github.com/nodejs/node/commit/f68d7d2acc)] - **(SEMVER-MAJOR)** **build**: reset embedder string to "-node.0" (Michaël Zasso) [#54536](https://github.com/nodejs/node/pull/54536)
* \[[`9d0748c5df`](https://github.com/nodejs/node/commit/9d0748c5df)] - **(SEMVER-MAJOR)** **build**: disable ICF for mksnapshot (Leszek Swirski) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`b7bcf3e121`](https://github.com/nodejs/node/commit/b7bcf3e121)] - **(SEMVER-MAJOR)** **build**: include v8-sandbox.h header in distribution (Michaël Zasso) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`1dfa3b8255`](https://github.com/nodejs/node/commit/1dfa3b8255)] - **(SEMVER-MAJOR)** **build**: reset embedder string to "-node.0" (Michaël Zasso) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`046343ea9d`](https://github.com/nodejs/node/commit/046343ea9d)] - **(SEMVER-MAJOR)** **build**: warn for GCC versions earlier than 12.2 (Michaël Zasso) [#54081](https://github.com/nodejs/node/pull/54081)
* \[[`a5decd4c8d`](https://github.com/nodejs/node/commit/a5decd4c8d)] - **(SEMVER-MAJOR)** **build**: drop experimental support for Windows <10 (Michaël Zasso) [#54079](https://github.com/nodejs/node/pull/54079)
* \[[`7ad0cc3e57`](https://github.com/nodejs/node/commit/7ad0cc3e57)] - **(SEMVER-MAJOR)** **build**: remove support for 32-bit Windows (Michaël Zasso) [#53184](https://github.com/nodejs/node/pull/53184)
* \[[`c7e42092f3`](https://github.com/nodejs/node/commit/c7e42092f3)] - **(SEMVER-MAJOR)** **build**: compile with C++20 support (Michaël Zasso) [#45427](https://github.com/nodejs/node/pull/45427)
* \[[`e2b7e41e23`](https://github.com/nodejs/node/commit/e2b7e41e23)] - **(SEMVER-MAJOR)** **child\_process**: remove unused internal event (Rich Trott) [#53793](https://github.com/nodejs/node/pull/53793)
* \[[`4f1fe8a015`](https://github.com/nodejs/node/commit/4f1fe8a015)] - **(SEMVER-MAJOR)** **cli**: remove deprecated V8 flag (Omer Katz) [#54761](https://github.com/nodejs/node/pull/54761)
* \[[`8f37492b65`](https://github.com/nodejs/node/commit/8f37492b65)] - **(SEMVER-MAJOR)** **cli**: move --trace-atomics-wait to eol (Marco Ippolito) [#52747](https://github.com/nodejs/node/pull/52747)
* \[[`f7e73cd1f2`](https://github.com/nodejs/node/commit/f7e73cd1f2)] - **(SEMVER-MAJOR)** **cli**: remove --no-experimental-global-customevent flag (Daeyeon Jeong) [#52723](https://github.com/nodejs/node/pull/52723)
* \[[`311504125f`](https://github.com/nodejs/node/commit/311504125f)] - **(SEMVER-MAJOR)** **cli**: remove --no-experimental-fetch flag (Filip Skokan) [#52611](https://github.com/nodejs/node/pull/52611)
* \[[`a30ae50860`](https://github.com/nodejs/node/commit/a30ae50860)] - **(SEMVER-MAJOR)** **cli**: remove --no-experimental-global-webcrypto flag (Filip Skokan) [#52564](https://github.com/nodejs/node/pull/52564)
* \[[`afe56aa58b`](https://github.com/nodejs/node/commit/afe56aa58b)] - **(SEMVER-MAJOR)** **crypto**: runtime deprecate crypto.fips (Yagiz Nizipli) [#55019](https://github.com/nodejs/node/pull/55019)
* \[[`33a6d1fe3a`](https://github.com/nodejs/node/commit/33a6d1fe3a)] - **(SEMVER-MAJOR)** **crypto**: remove ERR\_CRYPTO\_SCRYPT\_INVALID\_PARAMETER (Tobias Nießen) [#53305](https://github.com/nodejs/node/pull/53305)
* \[[`ff826069a8`](https://github.com/nodejs/node/commit/ff826069a8)] - **(SEMVER-MAJOR)** **crypto**: move DEP0182 to runtime deprecation (Tobias Nießen) [#52552](https://github.com/nodejs/node/pull/52552)
* \[[`6e150f9527`](https://github.com/nodejs/node/commit/6e150f9527)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 97199f686e2f (Michaël Zasso) [#54536](https://github.com/nodejs/node/pull/54536)
* \[[`1e16779fa1`](https://github.com/nodejs/node/commit/1e16779fa1)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 01a47f3ffff2 (Michaël Zasso) [#54536](https://github.com/nodejs/node/pull/54536)
* \[[`762a440e68`](https://github.com/nodejs/node/commit/762a440e68)] - **(SEMVER-MAJOR)** **deps**: patch V8 to support older Clang versions (Michaël Zasso) [#54536](https://github.com/nodejs/node/pull/54536)
* \[[`95f2213eed`](https://github.com/nodejs/node/commit/95f2213eed)] - **(SEMVER-MAJOR)** **deps**: always define V8\_NODISCARD as no-op (Michaël Zasso) [#54536](https://github.com/nodejs/node/pull/54536)
* \[[`09d997f181`](https://github.com/nodejs/node/commit/09d997f181)] - **(SEMVER-MAJOR)** **deps**: fix FP16 bitcasts.h (Stefan Stojanovic) [#54536](https://github.com/nodejs/node/pull/54536)
* \[[`1866363854`](https://github.com/nodejs/node/commit/1866363854)] - **(SEMVER-MAJOR)** **deps**: patch V8 to support compilation with MSVC (StefanStojanovic) [#54536](https://github.com/nodejs/node/pull/54536)
* \[[`6f4f22f84c`](https://github.com/nodejs/node/commit/6f4f22f84c)] - **(SEMVER-MAJOR)** **deps**: patch V8 to avoid duplicated zlib symbol (Michaël Zasso) [#54536](https://github.com/nodejs/node/pull/54536)
* \[[`dfff61475e`](https://github.com/nodejs/node/commit/dfff61475e)] - **(SEMVER-MAJOR)** **deps**: disable V8 concurrent sparkplug compilation (Michaël Zasso) [#54536](https://github.com/nodejs/node/pull/54536)
* \[[`69ad89f8eb`](https://github.com/nodejs/node/commit/69ad89f8eb)] - **(SEMVER-MAJOR)** **deps**: always define V8\_EXPORT\_PRIVATE as no-op (Michaël Zasso) [#54536](https://github.com/nodejs/node/pull/54536)
* \[[`5ab3140dfb`](https://github.com/nodejs/node/commit/5ab3140dfb)] - **(SEMVER-MAJOR)** **deps**: update V8 to 12.9.202.18 (Michaël Zasso) [#54536](https://github.com/nodejs/node/pull/54536)
* \[[`fba06eb34a`](https://github.com/nodejs/node/commit/fba06eb34a)] - **(SEMVER-MAJOR)** **deps**: remove bogus V8 DCHECK (Michaël Zasso) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`5355603fb5`](https://github.com/nodejs/node/commit/5355603fb5)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 00e9eeb3fb2c (Michaël Zasso) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`bcc1e2716c`](https://github.com/nodejs/node/commit/bcc1e2716c)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick b1397772c70c (Michaël Zasso) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`415bc750a5`](https://github.com/nodejs/node/commit/415bc750a5)] - **(SEMVER-MAJOR)** **deps**: V8: cherry-pick 35888fee7bba (Joyee Cheung) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`28f3e5c9d1`](https://github.com/nodejs/node/commit/28f3e5c9d1)] - **(SEMVER-MAJOR)** **deps**: always define V8\_NODISCARD as no-op (Michaël Zasso) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`a41c381cde`](https://github.com/nodejs/node/commit/a41c381cde)] - **(SEMVER-MAJOR)** **deps**: fix FP16 bitcasts.h (Stefan Stojanovic) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`16c9348e60`](https://github.com/nodejs/node/commit/16c9348e60)] - **(SEMVER-MAJOR)** **deps**: V8: revert CL 5331688 (Michaël Zasso) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`dc4e702a45`](https://github.com/nodejs/node/commit/dc4e702a45)] - **(SEMVER-MAJOR)** **deps**: patch V8 to support compilation with MSVC (StefanStojanovic) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`f626acc328`](https://github.com/nodejs/node/commit/f626acc328)] - **(SEMVER-MAJOR)** **deps**: silence internal V8 deprecation warning (Michaël Zasso) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`ed187faa64`](https://github.com/nodejs/node/commit/ed187faa64)] - **(SEMVER-MAJOR)** **deps**: patch V8 to avoid duplicated zlib symbol (Michaël Zasso) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`ed029bded7`](https://github.com/nodejs/node/commit/ed029bded7)] - **(SEMVER-MAJOR)** **deps**: avoid compilation error with ASan (Michaël Zasso) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`e600de93cf`](https://github.com/nodejs/node/commit/e600de93cf)] - **(SEMVER-MAJOR)** **deps**: disable V8 concurrent sparkplug compilation (Michaël Zasso) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`cc36db7c06`](https://github.com/nodejs/node/commit/cc36db7c06)] - **(SEMVER-MAJOR)** **deps**: always define V8\_EXPORT\_PRIVATE as no-op (Michaël Zasso) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`9d7cd9b864`](https://github.com/nodejs/node/commit/9d7cd9b864)] - **(SEMVER-MAJOR)** **deps**: update V8 to 12.8.374.13 (Michaël Zasso) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`4f70132972`](https://github.com/nodejs/node/commit/4f70132972)] - **(SEMVER-MAJOR)** **doc**: reflect toolchains used for official binaries (Richard Lau) [#54967](https://github.com/nodejs/node/pull/54967)
* \[[`7fab6e8885`](https://github.com/nodejs/node/commit/7fab6e8885)] - **(SEMVER-MAJOR)** **doc**: use gcc 12 on AIX for Node.js >=23 (Richard Lau) [#54338](https://github.com/nodejs/node/pull/54338)
* \[[`1d5ed725e9`](https://github.com/nodejs/node/commit/1d5ed725e9)] - **(SEMVER-MAJOR)** **esm**: export 'module.exports' on ESM CJS wrapper (Guy Bedford) [#53848](https://github.com/nodejs/node/pull/53848)
* \[[`d5c29ba12d`](https://github.com/nodejs/node/commit/d5c29ba12d)] - **(SEMVER-MAJOR)** **events**: set EventEmitterAsyncResource fields private (Yagiz Nizipli) [#54889](https://github.com/nodejs/node/pull/54889)
* \[[`f202322ea4`](https://github.com/nodejs/node/commit/f202322ea4)] - **(SEMVER-MAJOR)** **fs**: adjust typecheck for `type` in `fs.symlink()` (Livia Medeiros) [#49741](https://github.com/nodejs/node/pull/49741)
* \[[`15e7563062`](https://github.com/nodejs/node/commit/15e7563062)] - **(SEMVER-MAJOR)** **fs**: runtime deprecate `dirent.path` (Antoine du Hamel) [#51050](https://github.com/nodejs/node/pull/51050)
* \[[`00b2f07f9d`](https://github.com/nodejs/node/commit/00b2f07f9d)] - **(SEMVER-MAJOR)** **fs,win**: fix bug in paths with trailing slashes (Hüseyin Açacak) [#54160](https://github.com/nodejs/node/pull/54160)
* \[[`e973c3e94b`](https://github.com/nodejs/node/commit/e973c3e94b)] - **(SEMVER-MAJOR)** **lib**: validate signals with interface converter (Jason Zhang) [#54965](https://github.com/nodejs/node/pull/54965)
* \[[`a5a946d8a5`](https://github.com/nodejs/node/commit/a5a946d8a5)] - **(SEMVER-MAJOR)** **lib**: implement interface converter in webidl (Jason Zhang) [#54965](https://github.com/nodejs/node/pull/54965)
* \[[`6ed93b4d69`](https://github.com/nodejs/node/commit/6ed93b4d69)] - **(SEMVER-MAJOR)** **lib**: expose global CloseEvent (Matthew Aitken) [#53355](https://github.com/nodejs/node/pull/53355)
* \[[`52322aa42a`](https://github.com/nodejs/node/commit/52322aa42a)] - **(SEMVER-MAJOR)** **net**: validate host name for server listen (Jason Zhang) [#54470](https://github.com/nodejs/node/pull/54470)
* \[[`efbba60e5b`](https://github.com/nodejs/node/commit/efbba60e5b)] - **(SEMVER-MAJOR)** **path**: fix bugs and inconsistencies (Hüseyin Açacak) [#54224](https://github.com/nodejs/node/pull/54224)
* \[[`c237eabf4c`](https://github.com/nodejs/node/commit/c237eabf4c)] - **(SEMVER-MAJOR)** **process**: remove `process.assert` (Aviv Keller) [#55035](https://github.com/nodejs/node/pull/55035)
* \[[`17a17164d6`](https://github.com/nodejs/node/commit/17a17164d6)] - **(SEMVER-MAJOR)** **src**: update NODE\_MODULE\_VERSION to 131 (Michaël Zasso) [#54536](https://github.com/nodejs/node/pull/54536)
* \[[`f0134fa6c3`](https://github.com/nodejs/node/commit/f0134fa6c3)] - **(SEMVER-MAJOR)** **src**: stop using deprecated fields of `v8::FastApiCallbackOptions` (Andreas Haas) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`0be79f4deb`](https://github.com/nodejs/node/commit/0be79f4deb)] - **(SEMVER-MAJOR)** **src**: remove dependency on wrapper-descriptor-based CppHeap (Joyee Cheung) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`525b3f22d1`](https://github.com/nodejs/node/commit/525b3f22d1)] - **(SEMVER-MAJOR)** **src**: add source location to v8::TaskRunner (François Doray) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`e945bd9525`](https://github.com/nodejs/node/commit/e945bd9525)] - **(SEMVER-MAJOR)** **src**: update NODE\_MODULE\_VERSION to 129 (Michaël Zasso) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`bb8d2936ab`](https://github.com/nodejs/node/commit/bb8d2936ab)] - **(SEMVER-MAJOR)** **src**: do not use soon-to-be-deprecated V8 API (Igor Sheludko) [#53174](https://github.com/nodejs/node/pull/53174)
* \[[`75884678d7`](https://github.com/nodejs/node/commit/75884678d7)] - **(SEMVER-MAJOR)** **src**: add UV\_PIPE\_NO\_TRUNCATE for bind in pipe\_wrap.cc (theanarkh) [#52347](https://github.com/nodejs/node/pull/52347)
* \[[`922feb1ff5`](https://github.com/nodejs/node/commit/922feb1ff5)] - **(SEMVER-MAJOR)** **stream**: pipe to a closed or destroyed stream is not allowed in pipeline (jakecastelli) [#53241](https://github.com/nodejs/node/pull/53241)
* \[[`ffe0dc5b87`](https://github.com/nodejs/node/commit/ffe0dc5b87)] - **(SEMVER-MAJOR)** **string\_decoder**: refactor encoding validation (Yagiz Nizipli) [#54957](https://github.com/nodejs/node/pull/54957)
* \[[`df9efba2ce`](https://github.com/nodejs/node/commit/df9efba2ce)] - **(SEMVER-MAJOR)** **test**: update v8-stats test for V8 12.6 (Michaël Zasso) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`dbaef339aa`](https://github.com/nodejs/node/commit/dbaef339aa)] - **(SEMVER-MAJOR)** **test\_runner**: detect only tests when --test is not used (Colin Ihrig) [#54881](https://github.com/nodejs/node/pull/54881)
* \[[`eb7e18fe94`](https://github.com/nodejs/node/commit/eb7e18fe94)] - **(SEMVER-MAJOR)** **test\_runner**: always make spec the default reporter (Colin Ihrig) [#54548](https://github.com/nodejs/node/pull/54548)
* \[[`0db38f0f99`](https://github.com/nodejs/node/commit/0db38f0f99)] - **(SEMVER-MAJOR)** **test\_runner**: expose lcov reporter as newable function (Chemi Atlow) [#52403](https://github.com/nodejs/node/pull/52403)
* \[[`f5ed3386fd`](https://github.com/nodejs/node/commit/f5ed3386fd)] - **(SEMVER-MAJOR)** **timers**: emit warning if delay is negative or NaN (jakecastelli) [#46678](https://github.com/nodejs/node/pull/46678)
* \[[`f666a1b754`](https://github.com/nodejs/node/commit/f666a1b754)] - **(SEMVER-MAJOR)** **tls**: fix 'ERR\_TLS\_PSK\_SET\_IDENTIY\_HINT\_FAILED' typo (Aviv Keller) [#52627](https://github.com/nodejs/node/pull/52627)
* \[[`c8c108f9b0`](https://github.com/nodejs/node/commit/c8c108f9b0)] - **(SEMVER-MAJOR)** **tools**: add additonal include dirs for V8 on AIX (Abdirahim Musse) [#54536](https://github.com/nodejs/node/pull/54536)
* \[[`64e8646618`](https://github.com/nodejs/node/commit/64e8646618)] - **(SEMVER-MAJOR)** **tools**: update V8 gypfiles for 12.8 (Michaël Zasso) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`dc352a5ff2`](https://github.com/nodejs/node/commit/dc352a5ff2)] - **(SEMVER-MAJOR)** **tools**: update V8 gypfiles for 12.7 (Richard Lau) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`8044051ce3`](https://github.com/nodejs/node/commit/8044051ce3)] - **(SEMVER-MAJOR)** **tools**: update V8 gypfiles for 12.6 (Michaël Zasso) [#54077](https://github.com/nodejs/node/pull/54077)
* \[[`982f6ad516`](https://github.com/nodejs/node/commit/982f6ad516)] - **(SEMVER-MAJOR)** **util**: move util.log to eol (marco-ippolito) [#52744](https://github.com/nodejs/node/pull/52744)
* \[[`1d817dcb52`](https://github.com/nodejs/node/commit/1d817dcb52)] - **(SEMVER-MAJOR)** **util**: move util.isPrimitive to eol (marco-ippolito) [#52744](https://github.com/nodejs/node/pull/52744)
* \[[`72240942ed`](https://github.com/nodejs/node/commit/72240942ed)] - **(SEMVER-MAJOR)** **util**: move util.isFunction to eol (marco-ippolito) [#52744](https://github.com/nodejs/node/pull/52744)
* \[[`dc379626ab`](https://github.com/nodejs/node/commit/dc379626ab)] - **(SEMVER-MAJOR)** **util**: move util.isError to eol (marco-ippolito) [#52744](https://github.com/nodejs/node/pull/52744)
* \[[`b5cae4fea6`](https://github.com/nodejs/node/commit/b5cae4fea6)] - **(SEMVER-MAJOR)** **util**: move util.isDate to eol (marco-ippolito) [#52744](https://github.com/nodejs/node/pull/52744)
* \[[`bd559e3e5a`](https://github.com/nodejs/node/commit/bd559e3e5a)] - **(SEMVER-MAJOR)** **util**: move util.isObject to eol (marco-ippolito) [#52744](https://github.com/nodejs/node/pull/52744)
* \[[`d3068b9cfa`](https://github.com/nodejs/node/commit/d3068b9cfa)] - **(SEMVER-MAJOR)** **util**: move util.isRegExp to eol (marco-ippolito) [#52744](https://github.com/nodejs/node/pull/52744)
* \[[`a59c7aeb27`](https://github.com/nodejs/node/commit/a59c7aeb27)] - **(SEMVER-MAJOR)** **util**: move util.isUndefined to eol (marco-ippolito) [#52744](https://github.com/nodejs/node/pull/52744)
* \[[`05e72c939a`](https://github.com/nodejs/node/commit/05e72c939a)] - **(SEMVER-MAJOR)** **util**: move util.isSymbol to eol (marco-ippolito) [#52744](https://github.com/nodejs/node/pull/52744)
* \[[`832a77c003`](https://github.com/nodejs/node/commit/832a77c003)] - **(SEMVER-MAJOR)** **util**: move util.isString to eol (marco-ippolito) [#52744](https://github.com/nodejs/node/pull/52744)
* \[[`708f57ea49`](https://github.com/nodejs/node/commit/708f57ea49)] - **(SEMVER-MAJOR)** **util**: move util.isNumber to eol (marco-ippolito) [#52744](https://github.com/nodejs/node/pull/52744)
* \[[`6ec403fe91`](https://github.com/nodejs/node/commit/6ec403fe91)] - **(SEMVER-MAJOR)** **util**: move util.isNullOrUndefined to eol (marco-ippolito) [#52744](https://github.com/nodejs/node/pull/52744)
* \[[`7cd8bb26d1`](https://github.com/nodejs/node/commit/7cd8bb26d1)] - **(SEMVER-MAJOR)** **util**: move util.isNull to eol (marco-ippolito) [#52744](https://github.com/nodejs/node/pull/52744)
* \[[`e32b0c1eab`](https://github.com/nodejs/node/commit/e32b0c1eab)] - **(SEMVER-MAJOR)** **util**: move util.isBuffer to eol (marco-ippolito) [#52744](https://github.com/nodejs/node/pull/52744)
* \[[`be528ab11e`](https://github.com/nodejs/node/commit/be528ab11e)] - **(SEMVER-MAJOR)** **util**: move util.isBoolean to eol (marco-ippolito) [#52744](https://github.com/nodejs/node/pull/52744)
* \[[`ac97a532f5`](https://github.com/nodejs/node/commit/ac97a532f5)] - **(SEMVER-MAJOR)** **util**: move util.\_extend to eol (marco-ippolito) [#52744](https://github.com/nodejs/node/pull/52744)
* \[[`e225f00034`](https://github.com/nodejs/node/commit/e225f00034)] - **(SEMVER-MAJOR)** **zlib**: remove `zlib.bytesRead` (Yagiz Nizipli) [#55020](https://github.com/nodejs/node/pull/55020)

### Semver-Minor Commits

* \[[`90e3e5e173`](https://github.com/nodejs/node/commit/90e3e5e173)] - **(SEMVER-MINOR)** **crypto**: add KeyObject.prototype.toCryptoKey (Filip Skokan) [#55262](https://github.com/nodejs/node/pull/55262)
* \[[`29f31c6a76`](https://github.com/nodejs/node/commit/29f31c6a76)] - **(SEMVER-MINOR)** **crypto**: add Date fields for `validTo` and `validFrom` (Andrew Moon) [#54159](https://github.com/nodejs/node/pull/54159)
* \[[`83eb4f2855`](https://github.com/nodejs/node/commit/83eb4f2855)] - **(SEMVER-MINOR)** **deps**: V8: cherry-pick cd10ad7cdbe5 (Joyee Cheung) [#52535](https://github.com/nodejs/node/pull/52535)
* \[[`6c6562ce8b`](https://github.com/nodejs/node/commit/6c6562ce8b)] - **(SEMVER-MINOR)** **http2**: expose nghttp2\_option\_set\_stream\_reset\_rate\_limit as an option (Maël Nison) [#54875](https://github.com/nodejs/node/pull/54875)
* \[[`d473606040`](https://github.com/nodejs/node/commit/d473606040)] - **(SEMVER-MINOR)** **lib**: propagate aborted state to dependent signals before firing events (jazelly) [#54826](https://github.com/nodejs/node/pull/54826)
* \[[`772b35bdc4`](https://github.com/nodejs/node/commit/772b35bdc4)] - **(SEMVER-MINOR)** **module**: support loading entrypoint as url (RedYetiDev) [#54933](https://github.com/nodejs/node/pull/54933)
* \[[`06206af181`](https://github.com/nodejs/node/commit/06206af181)] - **(SEMVER-MINOR)** **module**: unflag --experimental-require-module (Joyee Cheung) [#55085](https://github.com/nodejs/node/pull/55085)
* \[[`0b9249e335`](https://github.com/nodejs/node/commit/0b9249e335)] - **(SEMVER-MINOR)** **module**: implement the "module-sync" exports condition (Joyee Cheung) [#54648](https://github.com/nodejs/node/pull/54648)
* \[[`62383cd113`](https://github.com/nodejs/node/commit/62383cd113)] - **(SEMVER-MINOR)** **module**: implement flushCompileCache() (Joyee Cheung) [#54971](https://github.com/nodejs/node/pull/54971)
* \[[`4dfed556ba`](https://github.com/nodejs/node/commit/4dfed556ba)] - **(SEMVER-MINOR)** **module**: throw when invalid argument is passed to enableCompileCache() (Joyee Cheung) [#54971](https://github.com/nodejs/node/pull/54971)
* \[[`9a73aa0d15`](https://github.com/nodejs/node/commit/9a73aa0d15)] - **(SEMVER-MINOR)** **module**: write compile cache to temporary file and then rename it (Joyee Cheung) [#54971](https://github.com/nodejs/node/pull/54971)
* \[[`92a25abca9`](https://github.com/nodejs/node/commit/92a25abca9)] - **(SEMVER-MINOR)** **path**: add `matchGlob` method (Aviv Keller) [#52881](https://github.com/nodejs/node/pull/52881)
* \[[`b0f025208f`](https://github.com/nodejs/node/commit/b0f025208f)] - **(SEMVER-MINOR)** **process**: add process.features.require\_module (Joyee Cheung) [#55241](https://github.com/nodejs/node/pull/55241)
* \[[`bf11e5793b`](https://github.com/nodejs/node/commit/bf11e5793b)] - **(SEMVER-MINOR)** **test\_runner**: support custom arguments in `run()` (Aviv Keller) [#55126](https://github.com/nodejs/node/pull/55126)
* \[[`059e08bb21`](https://github.com/nodejs/node/commit/059e08bb21)] - **(SEMVER-MINOR)** **test\_runner**: add 'test:summary' event (Colin Ihrig) [#54851](https://github.com/nodejs/node/pull/54851)
* \[[`f79fd03f41`](https://github.com/nodejs/node/commit/f79fd03f41)] - **(SEMVER-MINOR)** **test\_runner**: add support for coverage via run() (Chemi Atlow) [#53937](https://github.com/nodejs/node/pull/53937)
* \[[`d2ad9b4fb6`](https://github.com/nodejs/node/commit/d2ad9b4fb6)] - **(SEMVER-MINOR)** **worker**: add `markAsUncloneable` api (Jason Zhang) [#55234](https://github.com/nodejs/node/pull/55234)

### Semver-Patch Commits

* \[[`e1d8b4f038`](https://github.com/nodejs/node/commit/e1d8b4f038)] - **assert**: show the diff when deep comparing data with a custom message (Giovanni) [#54759](https://github.com/nodejs/node/pull/54759)
* \[[`4eeeab09f0`](https://github.com/nodejs/node/commit/4eeeab09f0)] - **benchmark**: rewrite detect-esm-syntax benchmark (Joyee Cheung) [#55238](https://github.com/nodejs/node/pull/55238)
* \[[`834316d541`](https://github.com/nodejs/node/commit/834316d541)] - **benchmark**: add no-warnings to process.has bench (Rafael Gonzaga) [#55159](https://github.com/nodejs/node/pull/55159)
* \[[`00d4f8073c`](https://github.com/nodejs/node/commit/00d4f8073c)] - **benchmark**: create benchmark for typescript (Marco Ippolito) [#54904](https://github.com/nodejs/node/pull/54904)
* \[[`96ec7eede9`](https://github.com/nodejs/node/commit/96ec7eede9)] - **benchmark**: add webstorage benchmark (jakecastelli) [#55040](https://github.com/nodejs/node/pull/55040)
* \[[`29357cb0ef`](https://github.com/nodejs/node/commit/29357cb0ef)] - **benchmark**: include ascii to fs/readfile (Rafael Gonzaga) [#54988](https://github.com/nodejs/node/pull/54988)
* \[[`53cba82e55`](https://github.com/nodejs/node/commit/53cba82e55)] - **benchmark**: add dotenv benchmark (Aviv Keller) [#54278](https://github.com/nodejs/node/pull/54278)
* \[[`4062b3fb43`](https://github.com/nodejs/node/commit/4062b3fb43)] - **buffer**: coerce extrema to int in `blob.slice` (Antoine du Hamel) [#55141](https://github.com/nodejs/node/pull/55141)
* \[[`f805d0be95`](https://github.com/nodejs/node/commit/f805d0be95)] - **buffer**: correctly apply prototype to cloned `File` / `Blob` (Aviv Keller) [#55138](https://github.com/nodejs/node/pull/55138)
* \[[`da5887d8e9`](https://github.com/nodejs/node/commit/da5887d8e9)] - **buffer**: extract Blob's .arrayBuffer() & webidl changes (Matthew Aitken) [#53372](https://github.com/nodejs/node/pull/53372)
* \[[`0d4387ebe2`](https://github.com/nodejs/node/commit/0d4387ebe2)] - **buffer**: use simdutf convert\_latin1\_to\_utf8\_safe (Robert Nagy) [#54798](https://github.com/nodejs/node/pull/54798)
* \[[`ae1e2b53b7`](https://github.com/nodejs/node/commit/ae1e2b53b7)] - **build**: fix notify-on-review-wanted action (Rafael Gonzaga) [#55304](https://github.com/nodejs/node/pull/55304)
* \[[`22bc15764b`](https://github.com/nodejs/node/commit/22bc15764b)] - **build**: include `.nycrc` in coverage workflows (Wuli Zuo) [#55210](https://github.com/nodejs/node/pull/55210)
* \[[`28ffa4b751`](https://github.com/nodejs/node/commit/28ffa4b751)] - **build**: fix not valid json in coverage (jakecastelli) [#55179](https://github.com/nodejs/node/pull/55179)
* \[[`1398c04c47`](https://github.com/nodejs/node/commit/1398c04c47)] - **build**: notify via slack when review-wanted (Rafael Gonzaga) [#55102](https://github.com/nodejs/node/pull/55102)
* \[[`b2c42dbcbb`](https://github.com/nodejs/node/commit/b2c42dbcbb)] - **build**: add more information to Makefile help (Aviv Keller) [#53381](https://github.com/nodejs/node/pull/53381)
* \[[`a1cd3c8777`](https://github.com/nodejs/node/commit/a1cd3c8777)] - **build**: update ruff and add `lint-py-fix` (Aviv Keller) [#54410](https://github.com/nodejs/node/pull/54410)
* \[[`6a6c957be7`](https://github.com/nodejs/node/commit/6a6c957be7)] - **build**: remove -v flag to reduce noise (iwuliz) [#55025](https://github.com/nodejs/node/pull/55025)
* \[[`5f6bb7d007`](https://github.com/nodejs/node/commit/5f6bb7d007)] - **build**: display free disk space after build in the test-macOS workflow (iwuliz) [#55025](https://github.com/nodejs/node/pull/55025)
* \[[`415b82d8b8`](https://github.com/nodejs/node/commit/415b82d8b8)] - **build**: support up to python 3.13 in android-configure (Aviv Keller) [#54529](https://github.com/nodejs/node/pull/54529)
* \[[`beb1892036`](https://github.com/nodejs/node/commit/beb1892036)] - **build**: add the option to generate compile\_commands.json in vcbuild.bat (Segev Finer) [#52279](https://github.com/nodejs/node/pull/52279)
* \[[`81cc72996a`](https://github.com/nodejs/node/commit/81cc72996a)] - **build**: fix eslint makefile target (Aviv Keller) [#54999](https://github.com/nodejs/node/pull/54999)
* \[[`7e00be7650`](https://github.com/nodejs/node/commit/7e00be7650)] - _**Revert**_ "**build**: upgrade clang-format to v18" (Chengzhong Wu) [#54994](https://github.com/nodejs/node/pull/54994)
* \[[`96e057093f`](https://github.com/nodejs/node/commit/96e057093f)] - **build**: print `Running XYZ linter...` for py and yml (Aviv Keller) [#54386](https://github.com/nodejs/node/pull/54386)
* \[[`ab5e58bf29`](https://github.com/nodejs/node/commit/ab5e58bf29)] - _**Revert**_ "**build**: only generate specified build type files" (Chengzhong Wu) [#53580](https://github.com/nodejs/node/pull/53580)
* \[[`6cb940a546`](https://github.com/nodejs/node/commit/6cb940a546)] - **build**: only generate specified build type files (Chengzhong Wu) [#53511](https://github.com/nodejs/node/pull/53511)
* \[[`27f8d9e9d2`](https://github.com/nodejs/node/commit/27f8d9e9d2)] - **build,win**: enable pch for clang-cl (Stefan Stojanovic) [#55249](https://github.com/nodejs/node/pull/55249)
* \[[`bbf08c6a1b`](https://github.com/nodejs/node/commit/bbf08c6a1b)] - **build,win**: add winget config to set up env (Hüseyin Açacak) [#54729](https://github.com/nodejs/node/pull/54729)
* \[[`653b96527a`](https://github.com/nodejs/node/commit/653b96527a)] - **build,win**: float VS 17.11 compilation patch (Stefan Stojanovic) [#54970](https://github.com/nodejs/node/pull/54970)
* \[[`0c5fa57bc7`](https://github.com/nodejs/node/commit/0c5fa57bc7)] - **cli**: ensure --run has proper pwd (Yagiz Nizipli) [#54949](https://github.com/nodejs/node/pull/54949)
* \[[`65768bca59`](https://github.com/nodejs/node/commit/65768bca59)] - **cli**: fix spacing for port range error (Aviv Keller) [#54495](https://github.com/nodejs/node/pull/54495)
* \[[`2d77ba5d30`](https://github.com/nodejs/node/commit/2d77ba5d30)] - _**Revert**_ "**console**: colorize console error and warn" (Aviv Keller) [#54677](https://github.com/nodejs/node/pull/54677)
* \[[`b64006c0ed`](https://github.com/nodejs/node/commit/b64006c0ed)] - **crypto**: ensure invalid SubtleCrypto JWK data import results in DataError (Filip Skokan) [#55041](https://github.com/nodejs/node/pull/55041)
* \[[`7a3027d563`](https://github.com/nodejs/node/commit/7a3027d563)] - **deps**: update undici to 6.20.0 (Node.js GitHub Bot) [#55329](https://github.com/nodejs/node/pull/55329)
* \[[`54b5ec94e0`](https://github.com/nodejs/node/commit/54b5ec94e0)] - **deps**: patch V8 to 12.9.202.26 (Node.js GitHub Bot) [#55161](https://github.com/nodejs/node/pull/55161)
* \[[`20d8b85d34`](https://github.com/nodejs/node/commit/20d8b85d34)] - **deps**: upgrade npm to 10.9.0 (npm team) [#55255](https://github.com/nodejs/node/pull/55255)
* \[[`fe45be207b`](https://github.com/nodejs/node/commit/fe45be207b)] - **deps**: V8: backport 0d5d6e71bbb0 (Yagiz Nizipli) [#55115](https://github.com/nodejs/node/pull/55115)
* \[[`5ff9b072b2`](https://github.com/nodejs/node/commit/5ff9b072b2)] - **deps**: update archs files for openssl-3.0.15+quic1 (Node.js GitHub Bot) [#55184](https://github.com/nodejs/node/pull/55184)
* \[[`302e6afe8c`](https://github.com/nodejs/node/commit/302e6afe8c)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.15+quic1 (Node.js GitHub Bot) [#55184](https://github.com/nodejs/node/pull/55184)
* \[[`5f78e2c880`](https://github.com/nodejs/node/commit/5f78e2c880)] - **deps**: update timezone to 2024b (Node.js GitHub Bot) [#55056](https://github.com/nodejs/node/pull/55056)
* \[[`5ed3296051`](https://github.com/nodejs/node/commit/5ed3296051)] - **deps**: patch V8 to 12.9.202.19 (Node.js GitHub Bot) [#55057](https://github.com/nodejs/node/pull/55057)
* \[[`a6ece28604`](https://github.com/nodejs/node/commit/a6ece28604)] - **deps**: update acorn-walk to 8.3.4 (Node.js GitHub Bot) [#54950](https://github.com/nodejs/node/pull/54950)
* \[[`a428b21066`](https://github.com/nodejs/node/commit/a428b21066)] - **deps**: update corepack to 0.29.4 (Node.js GitHub Bot) [#54845](https://github.com/nodejs/node/pull/54845)
* \[[`260f1f4608`](https://github.com/nodejs/node/commit/260f1f4608)] - **deps**: patch V8 to 12.8.374.33 (Node.js GitHub Bot) [#54952](https://github.com/nodejs/node/pull/54952)
* \[[`b887942e6b`](https://github.com/nodejs/node/commit/b887942e6b)] - **deps**: patch V8 to 12.8.374.32 (Node.js GitHub Bot) [#54884](https://github.com/nodejs/node/pull/54884)
* \[[`9087056060`](https://github.com/nodejs/node/commit/9087056060)] - **deps**: patch V8 to 12.8.374.31 (Michaël Zasso) [#54682](https://github.com/nodejs/node/pull/54682)
* \[[`6bce6f69c6`](https://github.com/nodejs/node/commit/6bce6f69c6)] - _**Revert**_ "**deps**: remove bogus V8 DCHECK" (Michaël Zasso) [#54682](https://github.com/nodejs/node/pull/54682)
* \[[`0c771c35fa`](https://github.com/nodejs/node/commit/0c771c35fa)] - **deps**: patch V8 to 12.8.374.22 (Node.js GitHub Bot) [#54435](https://github.com/nodejs/node/pull/54435)
* \[[`543d1a9cb9`](https://github.com/nodejs/node/commit/543d1a9cb9)] - **deps**: update archs files for openssl-3.0.14+quic1 (Node.js GitHub Bot) [#54336](https://github.com/nodejs/node/pull/54336)
* \[[`94d062bc78`](https://github.com/nodejs/node/commit/94d062bc78)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.14+quic1 (Node.js GitHub Bot) [#54336](https://github.com/nodejs/node/pull/54336)
* \[[`8e33f20a64`](https://github.com/nodejs/node/commit/8e33f20a64)] - _**Revert**_ "**deps**: V8: cherry-pick 9ebca66a5740" (Joyee Cheung) [#53582](https://github.com/nodejs/node/pull/53582)
* \[[`4c730aed7f`](https://github.com/nodejs/node/commit/4c730aed7f)] - **deps**: V8: cherry-pick 9ebca66a5740 (Chengzhong Wu) [#53522](https://github.com/nodejs/node/pull/53522)
* \[[`e9904fe49a`](https://github.com/nodejs/node/commit/e9904fe49a)] - **doc**: edit onboarding guide to clarify when mailmap addition is needed (Antoine du Hamel) [#55334](https://github.com/nodejs/node/pull/55334)
* \[[`acd698a5c8`](https://github.com/nodejs/node/commit/acd698a5c8)] - **doc**: fix the return type of outgoingMessage.setHeaders() (Jimmy Leung) [#55290](https://github.com/nodejs/node/pull/55290)
* \[[`d620755661`](https://github.com/nodejs/node/commit/d620755661)] - **doc**: add release key for aduh95 (Antoine du Hamel) [#55349](https://github.com/nodejs/node/pull/55349)
* \[[`4a3fffaf58`](https://github.com/nodejs/node/commit/4a3fffaf58)] - **doc**: move `ERR_INVALID_PERFORMANCE_MARK` to legacy errors (Antoine du Hamel) [#55247](https://github.com/nodejs/node/pull/55247)
* \[[`e79ae1bf0c`](https://github.com/nodejs/node/commit/e79ae1bf0c)] - **doc**: reserve 132 for Electron 34 (Michaela Laurencin) [#55306](https://github.com/nodejs/node/pull/55306)
* \[[`33fe88a0b3`](https://github.com/nodejs/node/commit/33fe88a0b3)] - **doc**: add pmarchini to collaborators (Pietro Marchini) [#55331](https://github.com/nodejs/node/pull/55331)
* \[[`755b89772d`](https://github.com/nodejs/node/commit/755b89772d)] - **doc**: fix `events.once()` example using `AbortSignal` (Ivo Janssen) [#55144](https://github.com/nodejs/node/pull/55144)
* \[[`accb239272`](https://github.com/nodejs/node/commit/accb239272)] - **doc**: add onboarding details for ambassador program (Marco Ippolito) [#55284](https://github.com/nodejs/node/pull/55284)
* \[[`a301596c41`](https://github.com/nodejs/node/commit/a301596c41)] - **doc**: move `ERR_NAPI_TSFN_START/STOP_IDLE_LOOP` to legacy errors (Antoine du Hamel) [#55248](https://github.com/nodejs/node/pull/55248)
* \[[`32efeea0c0`](https://github.com/nodejs/node/commit/32efeea0c0)] - **doc**: fix initial default value of autoSelectFamily (Ihor Rohovets) [#55245](https://github.com/nodejs/node/pull/55245)
* \[[`cc9b9a7f70`](https://github.com/nodejs/node/commit/cc9b9a7f70)] - **doc**: tweak onboarding instructions (Michael Dawson) [#55212](https://github.com/nodejs/node/pull/55212)
* \[[`c9cffb73b3`](https://github.com/nodejs/node/commit/c9cffb73b3)] - **doc**: update test context.assert (Pietro Marchini) [#55186](https://github.com/nodejs/node/pull/55186)
* \[[`348d865652`](https://github.com/nodejs/node/commit/348d865652)] - **doc**: update `require(ESM)` history and stability status (Antoine du Hamel) [#55199](https://github.com/nodejs/node/pull/55199)
* \[[`14b53df33c`](https://github.com/nodejs/node/commit/14b53df33c)] - **doc**: fix unordered error anchors (Antoine du Hamel) [#55242](https://github.com/nodejs/node/pull/55242)
* \[[`dec10991e7`](https://github.com/nodejs/node/commit/dec10991e7)] - **doc**: mention addons to experimental permission (Rafael Gonzaga) [#55166](https://github.com/nodejs/node/pull/55166)
* \[[`cebf21dfa5`](https://github.com/nodejs/node/commit/cebf21dfa5)] - **doc**: use correct dash in stability status (Antoine du Hamel) [#55200](https://github.com/nodejs/node/pull/55200)
* \[[`0f02810fc9`](https://github.com/nodejs/node/commit/0f02810fc9)] - **doc**: fix link in `test/README.md` (Livia Medeiros) [#55165](https://github.com/nodejs/node/pull/55165)
* \[[`22b4b7c626`](https://github.com/nodejs/node/commit/22b4b7c626)] - **doc**: fix typos (Nathan Baulch) [#55066](https://github.com/nodejs/node/pull/55066)
* \[[`e6427e1d87`](https://github.com/nodejs/node/commit/e6427e1d87)] - **doc**: add esm examples to node:net (Alfredo González) [#55134](https://github.com/nodejs/node/pull/55134)
* \[[`6d1cd506b5`](https://github.com/nodejs/node/commit/6d1cd506b5)] - **doc**: remove outdated https import reference (Edigleysson Silva (Edy)) [#55111](https://github.com/nodejs/node/pull/55111)
* \[[`5368cdcf8a`](https://github.com/nodejs/node/commit/5368cdcf8a)] - **doc**: move the YAML changes element (sendoru) [#55112](https://github.com/nodejs/node/pull/55112)
* \[[`23743f63fb`](https://github.com/nodejs/node/commit/23743f63fb)] - **doc**: remove random horizontal separators in `process.md` (Antoine du Hamel) [#55149](https://github.com/nodejs/node/pull/55149)
* \[[`18acff0d01`](https://github.com/nodejs/node/commit/18acff0d01)] - **doc**: put --env-file-if-exists=config right under --env-file=config (Edigleysson Silva (Edy)) [#55131](https://github.com/nodejs/node/pull/55131)
* \[[`fd787c96e1`](https://github.com/nodejs/node/commit/fd787c96e1)] - **doc**: fix the require resolve algorithm in `modules.md` (chirsz) [#55117](https://github.com/nodejs/node/pull/55117)
* \[[`668e523392`](https://github.com/nodejs/node/commit/668e523392)] - **doc**: update style guide (Aviv Keller) [#53223](https://github.com/nodejs/node/pull/53223)
* \[[`ae82b455d1`](https://github.com/nodejs/node/commit/ae82b455d1)] - **doc**: add missing `:` to `run()`'s `globPatterns` (Aviv Keller) [#55135](https://github.com/nodejs/node/pull/55135)
* \[[`7f480818b7`](https://github.com/nodejs/node/commit/7f480818b7)] - **doc**: correct `cleanup` option in stream.(promises.)finished (René) [#55043](https://github.com/nodejs/node/pull/55043)
* \[[`b8493a5789`](https://github.com/nodejs/node/commit/b8493a5789)] - **doc**: add abmusse to collaborators (Abdirahim Musse) [#55086](https://github.com/nodejs/node/pull/55086)
* \[[`f20c42e964`](https://github.com/nodejs/node/commit/f20c42e964)] - **doc**: add note about `--expose-internals` (Aviv Keller) [#52861](https://github.com/nodejs/node/pull/52861)
* \[[`1c61a83444`](https://github.com/nodejs/node/commit/1c61a83444)] - **doc**: remove `parseREPLKeyword` from REPL documentation (Aviv Keller) [#54749](https://github.com/nodejs/node/pull/54749)
* \[[`65362f0181`](https://github.com/nodejs/node/commit/65362f0181)] - **doc**: add missing EventSource docs to globals (Matthew Aitken) [#55022](https://github.com/nodejs/node/pull/55022)
* \[[`5e25c2a79a`](https://github.com/nodejs/node/commit/5e25c2a79a)] - **doc**: cover --experimental-test-module-mocks flag (Jonathan Sharpe) [#55021](https://github.com/nodejs/node/pull/55021)
* \[[`99433a2d7a`](https://github.com/nodejs/node/commit/99433a2d7a)] - **doc**: add more details for localStorage and sessionStorage (Batuhan Tomo) [#53881](https://github.com/nodejs/node/pull/53881)
* \[[`b446a587ba`](https://github.com/nodejs/node/commit/b446a587ba)] - **doc**: mark v21 as End-of-Life (Aviv Keller) [#54984](https://github.com/nodejs/node/pull/54984)
* \[[`5e87577b4f`](https://github.com/nodejs/node/commit/5e87577b4f)] - **doc**: change backporting guide with updated info (Aviv Keller) [#53746](https://github.com/nodejs/node/pull/53746)
* \[[`de47b3122a`](https://github.com/nodejs/node/commit/de47b3122a)] - **doc**: add missing definitions to `internal-api.md` (Aviv Keller) [#53303](https://github.com/nodejs/node/pull/53303)
* \[[`421977cd48`](https://github.com/nodejs/node/commit/421977cd48)] - **doc**: fix history of `process.features` (Antoine du Hamel) [#54982](https://github.com/nodejs/node/pull/54982)
* \[[`305137faae`](https://github.com/nodejs/node/commit/305137faae)] - **doc**: fix typo callsite.lineNumber (Rafael Gonzaga) [#54969](https://github.com/nodejs/node/pull/54969)
* \[[`7feff2434d`](https://github.com/nodejs/node/commit/7feff2434d)] - **doc**: update documentation for externalizing deps (Michael Dawson) [#54792](https://github.com/nodejs/node/pull/54792)
* \[[`cb20c5b9f4`](https://github.com/nodejs/node/commit/cb20c5b9f4)] - **doc**: add documentation for process.features (Marco Ippolito) [#54897](https://github.com/nodejs/node/pull/54897)
* \[[`24302c9fe9`](https://github.com/nodejs/node/commit/24302c9fe9)] - **doc**: fix typo in CppgcMixin docs (Joyee Cheung) [#54762](https://github.com/nodejs/node/pull/54762)
* \[[`7327e44a05`](https://github.com/nodejs/node/commit/7327e44a05)] - **doc**: sort versions to fix the linter error (Rafael Gonzaga) [#54229](https://github.com/nodejs/node/pull/54229)
* \[[`fb852798dc`](https://github.com/nodejs/node/commit/fb852798dc)] - **esm**: do not interpret `"main"` as a URL (Antoine du Hamel) [#55003](https://github.com/nodejs/node/pull/55003)
* \[[`8fd90938f9`](https://github.com/nodejs/node/commit/8fd90938f9)] - **esm**: remove --no-import-harmony-assertions (Shu-yu Guo) [#54890](https://github.com/nodejs/node/pull/54890)
* \[[`a9081b5391`](https://github.com/nodejs/node/commit/a9081b5391)] - **events**: allow null/undefined eventInitDict (Matthew Aitken) [#54643](https://github.com/nodejs/node/pull/54643)
* \[[`0de1cf004c`](https://github.com/nodejs/node/commit/0de1cf004c)] - **events**: return `currentTarget` when dispatching (Matthew Aitken) [#54642](https://github.com/nodejs/node/pull/54642)
* \[[`9f9069d313`](https://github.com/nodejs/node/commit/9f9069d313)] - **fs**: fix linter issue (Antoine du Hamel) [#55353](https://github.com/nodejs/node/pull/55353)
* \[[`36ca010bef`](https://github.com/nodejs/node/commit/36ca010bef)] - **fs**: acknowledge `signal` option in `filehandle.createReadStream()` (Livia Medeiros) [#55148](https://github.com/nodejs/node/pull/55148)
* \[[`7fe5bcd29e`](https://github.com/nodejs/node/commit/7fe5bcd29e)] - **fs**: check subdir correctly in cpSync (Jason Zhang) [#55033](https://github.com/nodejs/node/pull/55033)
* \[[`090add7864`](https://github.com/nodejs/node/commit/090add7864)] - **fs**: refactoring declaratively with `Array.fromAsync` (Sonny) [#54644](https://github.com/nodejs/node/pull/54644)
* \[[`77ca5ca075`](https://github.com/nodejs/node/commit/77ca5ca075)] - **fs**: convert to u8 string for filesystem path (Jason Zhang) [#54653](https://github.com/nodejs/node/pull/54653)
* \[[`cf2bce6386`](https://github.com/nodejs/node/commit/cf2bce6386)] - **fs**: fix regression on rmsync (Yagiz Nizipli) [#53982](https://github.com/nodejs/node/pull/53982)
* \[[`7168295e7a`](https://github.com/nodejs/node/commit/7168295e7a)] - **fs**: move `rmSync` implementation to c++ (Yagiz Nizipli) [#53617](https://github.com/nodejs/node/pull/53617)
* \[[`71785889c8`](https://github.com/nodejs/node/commit/71785889c8)] - **lib**: prefer logical assignment (Aviv Keller) [#55044](https://github.com/nodejs/node/pull/55044)
* \[[`78f421de88`](https://github.com/nodejs/node/commit/78f421de88)] - **lib**: fix module print timing when specifier includes `"` (Antoine du Hamel) [#55150](https://github.com/nodejs/node/pull/55150)
* \[[`d5eb9a378e`](https://github.com/nodejs/node/commit/d5eb9a378e)] - **lib**: remove `Symbol[Async]Dispose` polyfills (Michaël Zasso) [#55276](https://github.com/nodejs/node/pull/55276)
* \[[`4c045351c1`](https://github.com/nodejs/node/commit/4c045351c1)] - **lib**: fix typos (Nathan Baulch) [#55065](https://github.com/nodejs/node/pull/55065)
* \[[`574f2dd517`](https://github.com/nodejs/node/commit/574f2dd517)] - **lib**: prefer optional chaining (Aviv Keller) [#55045](https://github.com/nodejs/node/pull/55045)
* \[[`76edde5cd0`](https://github.com/nodejs/node/commit/76edde5cd0)] - **lib**: remove lib/internal/idna.js (Yagiz Nizipli) [#55050](https://github.com/nodejs/node/pull/55050)
* \[[`7014e50ca3`](https://github.com/nodejs/node/commit/7014e50ca3)] - **lib**: the REPL should survive deletion of Array.prototype methods (Jordan Harband) [#31457](https://github.com/nodejs/node/pull/31457)
* \[[`5c22d19f44`](https://github.com/nodejs/node/commit/5c22d19f44)] - **lib, tools**: remove duplicate requires (Aviv Keller) [#54987](https://github.com/nodejs/node/pull/54987)
* \[[`24648b5769`](https://github.com/nodejs/node/commit/24648b5769)] - **lib,esm**: handle bypass network-import via data: (Rafael Gonzaga) [#53764](https://github.com/nodejs/node/pull/53764)
* \[[`1d38bd1122`](https://github.com/nodejs/node/commit/1d38bd1122)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#55300](https://github.com/nodejs/node/pull/55300)
* \[[`98788dace6`](https://github.com/nodejs/node/commit/98788dace6)] - **meta**: bump mozilla-actions/sccache-action from 0.0.5 to 0.0.6 (dependabot\[bot]) [#55225](https://github.com/nodejs/node/pull/55225)
* \[[`8de2695fe5`](https://github.com/nodejs/node/commit/8de2695fe5)] - **meta**: bump actions/checkout from 4.1.7 to 4.2.0 (dependabot\[bot]) [#55224](https://github.com/nodejs/node/pull/55224)
* \[[`ccae9c0fef`](https://github.com/nodejs/node/commit/ccae9c0fef)] - **meta**: bump actions/setup-node from 4.0.3 to 4.0.4 (dependabot\[bot]) [#55223](https://github.com/nodejs/node/pull/55223)
* \[[`fd4959c67a`](https://github.com/nodejs/node/commit/fd4959c67a)] - **meta**: bump peter-evans/create-pull-request from 7.0.1 to 7.0.5 (dependabot\[bot]) [#55219](https://github.com/nodejs/node/pull/55219)
* \[[`c08bb75618`](https://github.com/nodejs/node/commit/c08bb75618)] - **meta**: add mailmap entry for abmusse (Abdirahim Musse) [#55182](https://github.com/nodejs/node/pull/55182)
* \[[`18800da280`](https://github.com/nodejs/node/commit/18800da280)] - **meta**: add more information about nightly releases (Aviv Keller) [#55084](https://github.com/nodejs/node/pull/55084)
* \[[`eda98728da`](https://github.com/nodejs/node/commit/eda98728da)] - **meta**: add `linux` to OS labels in collaborator guide (Aviv Keller) [#54986](https://github.com/nodejs/node/pull/54986)
* \[[`8aa57918c2`](https://github.com/nodejs/node/commit/8aa57918c2)] - **meta**: remove never-used workflow trigger (Aviv Keller) [#54983](https://github.com/nodejs/node/pull/54983)
* \[[`c6ae161237`](https://github.com/nodejs/node/commit/c6ae161237)] - **meta**: remove unneeded ignore rules from ruff (Aviv Keller) [#54360](https://github.com/nodejs/node/pull/54360)
* \[[`ccc7ce09f2`](https://github.com/nodejs/node/commit/ccc7ce09f2)] - **meta**: remove `build-windows.yml` (Aviv Keller) [#54662](https://github.com/nodejs/node/pull/54662)
* \[[`f88fe776ef`](https://github.com/nodejs/node/commit/f88fe776ef)] - **meta**: add links to alternative issue trackers (Aviv Keller) [#54401](https://github.com/nodejs/node/pull/54401)
* \[[`90f56dbad9`](https://github.com/nodejs/node/commit/90f56dbad9)] - **module**: throw ERR\_NO\_TYPESCRIPT when compiled without amaro (Marco Ippolito) [#55332](https://github.com/nodejs/node/pull/55332)
* \[[`31a37e777d`](https://github.com/nodejs/node/commit/31a37e777d)] - **module**: wrap swc error in ERR\_INVALID\_TYPESCRIPT\_SYNTAX (Marco Ippolito) [#55316](https://github.com/nodejs/node/pull/55316)
* \[[`3fb7426f83`](https://github.com/nodejs/node/commit/3fb7426f83)] - **module**: check --experimental-require-module separately from detection (Joyee Cheung) [#55250](https://github.com/nodejs/node/pull/55250)
* \[[`bdd590be73`](https://github.com/nodejs/node/commit/bdd590be73)] - **module**: use kNodeModulesRE to detect node\_modules (Joyee Cheung) [#55243](https://github.com/nodejs/node/pull/55243)
* \[[`5e4da33d97`](https://github.com/nodejs/node/commit/5e4da33d97)] - **module**: add internal type def for `flushCompileCache` (Jacob Smith) [#55226](https://github.com/nodejs/node/pull/55226)
* \[[`d24c7313f7`](https://github.com/nodejs/node/commit/d24c7313f7)] - **module**: support 'module.exports' interop export in require(esm) (Guy Bedford) [#54563](https://github.com/nodejs/node/pull/54563)
* \[[`12f92b04f4`](https://github.com/nodejs/node/commit/12f92b04f4)] - **module**: remove duplicated import (Aviv Keller) [#54942](https://github.com/nodejs/node/pull/54942)
* \[[`be4babb3c2`](https://github.com/nodejs/node/commit/be4babb3c2)] - **module**: report unfinished TLA in ambiguous modules (Antoine du Hamel) [#54980](https://github.com/nodejs/node/pull/54980)
* \[[`3ac5b49d85`](https://github.com/nodejs/node/commit/3ac5b49d85)] - **module**: refator ESM loader for adding future synchronous hooks (Joyee Cheung) [#54769](https://github.com/nodejs/node/pull/54769)
* \[[`3c4ef343ee`](https://github.com/nodejs/node/commit/3c4ef343ee)] - **module**: remove bogus assertion in CJS entrypoint handling with --import (Joyee Cheung) [#54592](https://github.com/nodejs/node/pull/54592)
* \[[`e35902cddb`](https://github.com/nodejs/node/commit/e35902cddb)] - **module**: fix discrepancy between .ts and .js (Marco Ippolito) [#54461](https://github.com/nodejs/node/pull/54461)
* \[[`fdf838aee6`](https://github.com/nodejs/node/commit/fdf838aee6)] - **node-api**: add napi\_create\_buffer\_from\_arraybuffer method (Mert Can Altin) [#54505](https://github.com/nodejs/node/pull/54505)
* \[[`87e7aeb672`](https://github.com/nodejs/node/commit/87e7aeb672)] - **os**: use const with early return for path (Trivikram Kamat) [#54959](https://github.com/nodejs/node/pull/54959)
* \[[`e42ca5c1a9`](https://github.com/nodejs/node/commit/e42ca5c1a9)] - **path**: remove repetitive conditional operator in `posix.resolve` (Wiyeong Seo) [#54835](https://github.com/nodejs/node/pull/54835)
* \[[`04750afb1e`](https://github.com/nodejs/node/commit/04750afb1e)] - **perf\_hooks**: add missing type argument to getEntriesByName (Luke Taher) [#54767](https://github.com/nodejs/node/pull/54767)
* \[[`f98d9c125c`](https://github.com/nodejs/node/commit/f98d9c125c)] - **process**: fix `process.features.typescript` when Amaro is unavailable (Antoine du Hamel) [#55323](https://github.com/nodejs/node/pull/55323)
* \[[`bbdfeebd9e`](https://github.com/nodejs/node/commit/bbdfeebd9e)] - **process**: add `process.features.typescript` (Aviv Keller) [#54295](https://github.com/nodejs/node/pull/54295)
* \[[`cdae315706`](https://github.com/nodejs/node/commit/cdae315706)] - **quic**: start adding in the internal quic js api (James M Snell) [#53256](https://github.com/nodejs/node/pull/53256)
* \[[`c6d20a034d`](https://github.com/nodejs/node/commit/c6d20a034d)] - **repl**: catch `\v` and `\r` in new-line detection (Aviv Keller) [#54512](https://github.com/nodejs/node/pull/54512)
* \[[`09d10b50dc`](https://github.com/nodejs/node/commit/09d10b50dc)] - **sqlite**: disable DQS misfeature by default (Tobias Nießen) [#55297](https://github.com/nodejs/node/pull/55297)
* \[[`7af434fc19`](https://github.com/nodejs/node/commit/7af434fc19)] - **sqlite**: make sourceSQL and expandedSQL string-valued properties (Tobias Nießen) [#54721](https://github.com/nodejs/node/pull/54721)
* \[[`a49abec6c3`](https://github.com/nodejs/node/commit/a49abec6c3)] - **sqlite**: enable foreign key constraints by default (Tobias Nießen) [#54777](https://github.com/nodejs/node/pull/54777)
* \[[`14353387eb`](https://github.com/nodejs/node/commit/14353387eb)] - **src**: implement IsInsideNodeModules() in C++ (Joyee Cheung) [#55286](https://github.com/nodejs/node/pull/55286)
* \[[`18536d95e2`](https://github.com/nodejs/node/commit/18536d95e2)] - **src**: apply getCallSite optimization (RafaelGSS) [#55174](https://github.com/nodejs/node/pull/55174)
* \[[`317d2450f9`](https://github.com/nodejs/node/commit/317d2450f9)] - **src**: modernize likely/unlikely hints (Yagiz Nizipli) [#55155](https://github.com/nodejs/node/pull/55155)
* \[[`33bbf3751b`](https://github.com/nodejs/node/commit/33bbf3751b)] - **src**: fixup Error.stackTraceLimit during snapshot building (Joyee Cheung) [#55121](https://github.com/nodejs/node/pull/55121)
* \[[`65fbc95949`](https://github.com/nodejs/node/commit/65fbc95949)] - **src**: parse --stack-trace-limit and use it in --trace-\* flags (Joyee Cheung) [#55121](https://github.com/nodejs/node/pull/55121)
* \[[`858bce5698`](https://github.com/nodejs/node/commit/858bce5698)] - **src**: make minor tweaks to quic c++ for c++20 (James M Snell) [#53256](https://github.com/nodejs/node/pull/53256)
* \[[`ac53a5b29d`](https://github.com/nodejs/node/commit/ac53a5b29d)] - **src**: move more key handling to ncrypto (James M Snell) [#55108](https://github.com/nodejs/node/pull/55108)
* \[[`f5d454ac7e`](https://github.com/nodejs/node/commit/f5d454ac7e)] - **src**: add receiver to fast api callback methods (Carlos Espa) [#54408](https://github.com/nodejs/node/pull/54408)
* \[[`b5fb2ff81e`](https://github.com/nodejs/node/commit/b5fb2ff81e)] - **src**: fix typos (Nathan Baulch) [#55064](https://github.com/nodejs/node/pull/55064)
* \[[`812806a757`](https://github.com/nodejs/node/commit/812806a757)] - **src**: move more stuff over to use Maybe\<void> (James M Snell) [#54831](https://github.com/nodejs/node/pull/54831)
* \[[`84966703e0`](https://github.com/nodejs/node/commit/84966703e0)] - **src**: track BaseObjects with an efficient list (Chengzhong Wu) [#55104](https://github.com/nodejs/node/pull/55104)
* \[[`02cdf7b809`](https://github.com/nodejs/node/commit/02cdf7b809)] - **src**: decode native error messages as UTF-8 (Joyee Cheung) [#55024](https://github.com/nodejs/node/pull/55024)
* \[[`6fb9f56994`](https://github.com/nodejs/node/commit/6fb9f56994)] - **src**: update clang-tidy and focus on modernization (Yagiz Nizipli) [#53757](https://github.com/nodejs/node/pull/53757)
* \[[`773e7c67cf`](https://github.com/nodejs/node/commit/773e7c67cf)] - **src**: do not call path.back() when it is empty (Cheng) [#55072](https://github.com/nodejs/node/pull/55072)
* \[[`c4681d55ae`](https://github.com/nodejs/node/commit/c4681d55ae)] - **src**: move evp stuff to ncrypto (James M Snell) [#54911](https://github.com/nodejs/node/pull/54911)
* \[[`5a966714c1`](https://github.com/nodejs/node/commit/5a966714c1)] - **src**: revert filesystem::path changes (Yagiz Nizipli) [#55015](https://github.com/nodejs/node/pull/55015)
* \[[`12dd4c7575`](https://github.com/nodejs/node/commit/12dd4c7575)] - **src**: mark node --run as stable (Yagiz Nizipli) [#53763](https://github.com/nodejs/node/pull/53763)
* \[[`8b8fc53c9a`](https://github.com/nodejs/node/commit/8b8fc53c9a)] - **src**: cleanup per env handles directly without a list (Chengzhong Wu) [#54993](https://github.com/nodejs/node/pull/54993)
* \[[`fd8c762fab`](https://github.com/nodejs/node/commit/fd8c762fab)] - **src**: add unistd.h import if node posix credentials is defined (Jonas) [#54528](https://github.com/nodejs/node/pull/54528)
* \[[`d496d44145`](https://github.com/nodejs/node/commit/d496d44145)] - **src**: remove duplicate code setting AF\_INET (He Yang) [#54939](https://github.com/nodejs/node/pull/54939)
* \[[`d2a4f92920`](https://github.com/nodejs/node/commit/d2a4f92920)] - **src**: use `Maybe<void>` where bool isn't needed (Michaël Zasso) [#54575](https://github.com/nodejs/node/pull/54575)
* \[[`8191e1f575`](https://github.com/nodejs/node/commit/8191e1f575)] - **src**: improve utf8 string generation performance (Yagiz Nizipli) [#54873](https://github.com/nodejs/node/pull/54873)
* \[[`9f5977fdac`](https://github.com/nodejs/node/commit/9f5977fdac)] - **src**: simplify string\_bytes with views (Daniel Lemire) [#54876](https://github.com/nodejs/node/pull/54876)
* \[[`849db10fb3`](https://github.com/nodejs/node/commit/849db10fb3)] - **src**: add helpers for creating cppgc-managed wrappers (Joyee Cheung) [#52295](https://github.com/nodejs/node/pull/52295)
* \[[`4568df4c6d`](https://github.com/nodejs/node/commit/4568df4c6d)] - **src**: support v8::Data in heap utils (Joyee Cheung) [#52295](https://github.com/nodejs/node/pull/52295)
* \[[`4f1c27af8c`](https://github.com/nodejs/node/commit/4f1c27af8c)] - **src**: handle errors correctly in webstorage (Michaël Zasso) [#54544](https://github.com/nodejs/node/pull/54544)
* \[[`c062b5242a`](https://github.com/nodejs/node/commit/c062b5242a)] - **src**: use correct way to signal interceptor error (Michaël Zasso) [#54418](https://github.com/nodejs/node/pull/54418)
* \[[`097a52848e`](https://github.com/nodejs/node/commit/097a52848e)] - **src**: do not save c\_str of a temp string (Cheng) [#53941](https://github.com/nodejs/node/pull/53941)
* \[[`3111ed7011`](https://github.com/nodejs/node/commit/3111ed7011)] - **stream**: handle undefined chunks correctly in decode stream (devstone) [#55153](https://github.com/nodejs/node/pull/55153)
* \[[`87a79cd8a1`](https://github.com/nodejs/node/commit/87a79cd8a1)] - **stream**: treat null asyncIterator as undefined (Jason Zhang) [#55119](https://github.com/nodejs/node/pull/55119)
* \[[`0e52836c35`](https://github.com/nodejs/node/commit/0e52836c35)] - **stream**: set stream prototype to closest transferable superclass (Jason Zhang) [#55067](https://github.com/nodejs/node/pull/55067)
* \[[`82dab76d63`](https://github.com/nodejs/node/commit/82dab76d63)] - **test**: fix tests when Amaro is unavailable (Richard Lau) [#55320](https://github.com/nodejs/node/pull/55320)
* \[[`fdc23b2f6b`](https://github.com/nodejs/node/commit/fdc23b2f6b)] - **test**: use more informative errors in `test-runner-cli` (Antoine du Hamel) [#55321](https://github.com/nodejs/node/pull/55321)
* \[[`a05cb0d1b0`](https://github.com/nodejs/node/commit/a05cb0d1b0)] - **test**: make `test-loaders-workers-spawned` less flaky (Antoine du Hamel) [#55172](https://github.com/nodejs/node/pull/55172)
* \[[`6c92c1391a`](https://github.com/nodejs/node/commit/6c92c1391a)] - **test**: add resource to internal module stat test (RafaelGSS) [#55157](https://github.com/nodejs/node/pull/55157)
* \[[`1d95b79b66`](https://github.com/nodejs/node/commit/1d95b79b66)] - **test**: move coverage source map tests to new file (Aviv Keller) [#55123](https://github.com/nodejs/node/pull/55123)
* \[[`2755551c3c`](https://github.com/nodejs/node/commit/2755551c3c)] - **test**: adding more tests for strip-types (Kevin Toshihiro Uehara) [#54929](https://github.com/nodejs/node/pull/54929)
* \[[`371ed85e4e`](https://github.com/nodejs/node/commit/371ed85e4e)] - **test**: update wpt test for encoding (devstone) [#55151](https://github.com/nodejs/node/pull/55151)
* \[[`99e0d0d218`](https://github.com/nodejs/node/commit/99e0d0d218)] - **test**: add `escapePOSIXShell` util (Antoine du Hamel) [#55125](https://github.com/nodejs/node/pull/55125)
* \[[`56c1786475`](https://github.com/nodejs/node/commit/56c1786475)] - **test**: remove unnecessary `await` in test-watch-mode (Wuli) [#55142](https://github.com/nodejs/node/pull/55142)
* \[[`28c7394319`](https://github.com/nodejs/node/commit/28c7394319)] - **test**: fix typos (Nathan Baulch) [#55063](https://github.com/nodejs/node/pull/55063)
* \[[`fbc6fcb018`](https://github.com/nodejs/node/commit/fbc6fcb018)] - **test**: remove duplicated test descriptions (Christos Koutsiaris) [#54140](https://github.com/nodejs/node/pull/54140)
* \[[`66a2cb210a`](https://github.com/nodejs/node/commit/66a2cb210a)] - **test**: deflake test/pummel/test-timers.js (jakecastelli) [#55098](https://github.com/nodejs/node/pull/55098)
* \[[`9bb6a1a790`](https://github.com/nodejs/node/commit/9bb6a1a790)] - **test**: deflake test-http-remove-header-stays-removed (Luigi Pinca) [#55004](https://github.com/nodejs/node/pull/55004)
* \[[`0f7bdcc17f`](https://github.com/nodejs/node/commit/0f7bdcc17f)] - **test**: fix test-tls-junk-closes-server (Michael Dawson) [#55089](https://github.com/nodejs/node/pull/55089)
* \[[`2118e32d9b`](https://github.com/nodejs/node/commit/2118e32d9b)] - **test**: fix more tests that fail when path contains a space (Antoine du Hamel) [#55088](https://github.com/nodejs/node/pull/55088)
* \[[`bdddc04dff`](https://github.com/nodejs/node/commit/bdddc04dff)] - **test**: fix `assertSnapshot` when path contains a quote (Antoine du Hamel) [#55087](https://github.com/nodejs/node/pull/55087)
* \[[`7d0ce254e8`](https://github.com/nodejs/node/commit/7d0ce254e8)] - **test**: fix some tests when path contains `%` (Antoine du Hamel) [#55082](https://github.com/nodejs/node/pull/55082)
* \[[`61ad74fb0f`](https://github.com/nodejs/node/commit/61ad74fb0f)] - _**Revert**_ "**test**: mark test-fs-watch-non-recursive flaky on Windows" (Luigi Pinca) [#55079](https://github.com/nodejs/node/pull/55079)
* \[[`02e8972169`](https://github.com/nodejs/node/commit/02e8972169)] - **test**: remove interval and give more time to unsync (Pietro Marchini) [#55006](https://github.com/nodejs/node/pull/55006)
* \[[`3c5ceff85f`](https://github.com/nodejs/node/commit/3c5ceff85f)] - **test**: deflake test-inspector-strip-types (Luigi Pinca) [#55058](https://github.com/nodejs/node/pull/55058)
* \[[`8b70e6bdee`](https://github.com/nodejs/node/commit/8b70e6bdee)] - **test**: make `test-runner-assert` more robust (Aviv Keller) [#55036](https://github.com/nodejs/node/pull/55036)
* \[[`2cec716c48`](https://github.com/nodejs/node/commit/2cec716c48)] - **test**: update tls test to support OpenSSL32 (Michael Dawson) [#55030](https://github.com/nodejs/node/pull/55030)
* \[[`1fcb128771`](https://github.com/nodejs/node/commit/1fcb128771)] - **test**: do not assume `process.execPath` contains no spaces (Antoine du Hamel) [#55028](https://github.com/nodejs/node/pull/55028)
* \[[`7ecc48d061`](https://github.com/nodejs/node/commit/7ecc48d061)] - **test**: fix `test-vm-context-dont-contextify` when path contains a space (Antoine du Hamel) [#55026](https://github.com/nodejs/node/pull/55026)
* \[[`cfe58cfdc4`](https://github.com/nodejs/node/commit/cfe58cfdc4)] - **test**: adjust tls-set-ciphers for OpenSSL32 (Michael Dawson) [#55016](https://github.com/nodejs/node/pull/55016)
* \[[`941635473d`](https://github.com/nodejs/node/commit/941635473d)] - **test**: add `util.stripVTControlCharacters` test (RedYetiDev) [#54865](https://github.com/nodejs/node/pull/54865)
* \[[`b23d1c37b9`](https://github.com/nodejs/node/commit/b23d1c37b9)] - **test**: improve coverage for timer promises schedular (Aviv Keller) [#53370](https://github.com/nodejs/node/pull/53370)
* \[[`a65e4418e5`](https://github.com/nodejs/node/commit/a65e4418e5)] - **test**: remove `getCallSite` from common (RedYetiDev) [#54947](https://github.com/nodejs/node/pull/54947)
* \[[`5116578b8a`](https://github.com/nodejs/node/commit/5116578b8a)] - **test**: remove unused common utilities (RedYetiDev) [#54825](https://github.com/nodejs/node/pull/54825)
* \[[`a9677db91b`](https://github.com/nodejs/node/commit/a9677db91b)] - **test**: deflake test-http-header-overflow (Luigi Pinca) [#54978](https://github.com/nodejs/node/pull/54978)
* \[[`9be0057859`](https://github.com/nodejs/node/commit/9be0057859)] - **test**: fix `soucre` to `source` (Aviv Keller) [#55038](https://github.com/nodejs/node/pull/55038)
* \[[`29b9c72b05`](https://github.com/nodejs/node/commit/29b9c72b05)] - **test**: add asserts to validate test assumptions (Michael Dawson) [#54997](https://github.com/nodejs/node/pull/54997)
* \[[`e35299ae62`](https://github.com/nodejs/node/commit/e35299ae62)] - **test**: add runner watch mode isolation tests (Pietro Marchini) [#54888](https://github.com/nodejs/node/pull/54888)
* \[[`2a1607cc2e`](https://github.com/nodejs/node/commit/2a1607cc2e)] - **test**: fix invalid wasm test (Aviv Keller) [#54935](https://github.com/nodejs/node/pull/54935)
* \[[`a6ed2148a0`](https://github.com/nodejs/node/commit/a6ed2148a0)] - **test**: move test-http-max-sockets to parallel (Luigi Pinca) [#54977](https://github.com/nodejs/node/pull/54977)
* \[[`636b3432d3`](https://github.com/nodejs/node/commit/636b3432d3)] - **test**: remove test-http-max-sockets flaky designation (Luigi Pinca) [#54976](https://github.com/nodejs/node/pull/54976)
* \[[`291d90acbc`](https://github.com/nodejs/node/commit/291d90acbc)] - **test**: refactor test-whatwg-webstreams-encoding to be shorter (David Dong) [#54569](https://github.com/nodejs/node/pull/54569)
* \[[`6dfa3e46d3`](https://github.com/nodejs/node/commit/6dfa3e46d3)] - **test**: adjust key sizes to support OpenSSL32 (Michael Dawson) [#54972](https://github.com/nodejs/node/pull/54972)
* \[[`f8b7a17146`](https://github.com/nodejs/node/commit/f8b7a17146)] - **test**: update test to support OpenSSL32 (Michael Dawson) [#54968](https://github.com/nodejs/node/pull/54968)
* \[[`b470e2fcb2`](https://github.com/nodejs/node/commit/b470e2fcb2)] - **test**: update DOM events web platform tests (Matthew Aitken) [#54642](https://github.com/nodejs/node/pull/54642)
* \[[`9cbef482df`](https://github.com/nodejs/node/commit/9cbef482df)] - **test**: update multiple assert tests to use node:test (James M Snell) [#54585](https://github.com/nodejs/node/pull/54585)
* \[[`259163802c`](https://github.com/nodejs/node/commit/259163802c)] - **test**: validate promise-version `setTimeout` behavior with `NaN` (Benjamin Gruenbaum) [#53622](https://github.com/nodejs/node/pull/53622)
* \[[`4174b73153`](https://github.com/nodejs/node/commit/4174b73153)] - **test**: support glob matching coverage files (Aviv Keller) [#53553](https://github.com/nodejs/node/pull/53553)
* \[[`0e187e4a21`](https://github.com/nodejs/node/commit/0e187e4a21)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#55029](https://github.com/nodejs/node/pull/55029)
* \[[`ccd4faf4bf`](https://github.com/nodejs/node/commit/ccd4faf4bf)] - _**Revert**_ "**test\_runner**: ignore unmapped lines for coverage" (Aviv Keller) [#55339](https://github.com/nodejs/node/pull/55339)
* \[[`3a42085ee4`](https://github.com/nodejs/node/commit/3a42085ee4)] - **test\_runner**: ignore unmapped lines for coverage (Edigleysson Silva (Edy)) [#55228](https://github.com/nodejs/node/pull/55228)
* \[[`9a9409ff1f`](https://github.com/nodejs/node/commit/9a9409ff1f)] - **test\_runner**: throw on invalid source map (Aviv Keller) [#55055](https://github.com/nodejs/node/pull/55055)
* \[[`980b91a1ef`](https://github.com/nodejs/node/commit/980b91a1ef)] - **test\_runner**: assert entry is a valid object (Edigleysson Silva (Edy)) [#55231](https://github.com/nodejs/node/pull/55231)
* \[[`1c7795e52e`](https://github.com/nodejs/node/commit/1c7795e52e)] - **test\_runner**: add cwd option to run (Pietro Marchini) [#54705](https://github.com/nodejs/node/pull/54705)
* \[[`103b8439ca`](https://github.com/nodejs/node/commit/103b8439ca)] - **test\_runner**: avoid spread operator on arrays (Antoine du Hamel) [#55143](https://github.com/nodejs/node/pull/55143)
* \[[`27dab9d916`](https://github.com/nodejs/node/commit/27dab9d916)] - **test\_runner**: support typescript files in default glob (Aviv Keller) [#55081](https://github.com/nodejs/node/pull/55081)
* \[[`e32521a7b9`](https://github.com/nodejs/node/commit/e32521a7b9)] - **test\_runner**: close and flush destinations on forced exit (Colin Ihrig) [#55099](https://github.com/nodejs/node/pull/55099)
* \[[`aac8ba7bd7`](https://github.com/nodejs/node/commit/aac8ba7bd7)] - **test\_runner**: fix mocking modules with quote in their URL (Antoine du Hamel) [#55083](https://github.com/nodejs/node/pull/55083)
* \[[`4f881790e9`](https://github.com/nodejs/node/commit/4f881790e9)] - **test\_runner**: report error on missing sourcemap source (Aviv Keller) [#55037](https://github.com/nodejs/node/pull/55037)
* \[[`b264cbe5e8`](https://github.com/nodejs/node/commit/b264cbe5e8)] - **test\_runner**: use `test:` symbol on second print of parent test (RedYetiDev) [#54956](https://github.com/nodejs/node/pull/54956)
* \[[`0c8c107aaa`](https://github.com/nodejs/node/commit/0c8c107aaa)] - **test\_runner**: replace ansi clear with ansi reset (Pietro Marchini) [#55013](https://github.com/nodejs/node/pull/55013)
* \[[`bb405210c5`](https://github.com/nodejs/node/commit/bb405210c5)] - **test\_runner**: support typescript module mocking (Marco Ippolito) [#54878](https://github.com/nodejs/node/pull/54878)
* \[[`50136a167d`](https://github.com/nodejs/node/commit/50136a167d)] - **test\_runner**: avoid coverage report partial file names (Pietro Marchini) [#54379](https://github.com/nodejs/node/pull/54379)
* \[[`4988bb549e`](https://github.com/nodejs/node/commit/4988bb549e)] - **tools**: enforce ordering of error codes in `errors.md` (Antoine du Hamel) [#55324](https://github.com/nodejs/node/pull/55324)
* \[[`5a3da7b4e4`](https://github.com/nodejs/node/commit/5a3da7b4e4)] - **tools**: enforce errors to not be documented in legacy section (Aviv Keller) [#55218](https://github.com/nodejs/node/pull/55218)
* \[[`8dbca2d35b`](https://github.com/nodejs/node/commit/8dbca2d35b)] - **tools**: update gyp-next to 0.18.2 (Node.js GitHub Bot) [#55160](https://github.com/nodejs/node/pull/55160)
* \[[`b2161d3a13`](https://github.com/nodejs/node/commit/b2161d3a13)] - **tools**: bump the eslint group in /tools/eslint with 4 updates (dependabot\[bot]) [#55227](https://github.com/nodejs/node/pull/55227)
* \[[`e7d27320c3`](https://github.com/nodejs/node/commit/e7d27320c3)] - **tools**: only check teams on the default branch (Antoine du Hamel) [#55124](https://github.com/nodejs/node/pull/55124)
* \[[`e8127db032`](https://github.com/nodejs/node/commit/e8127db032)] - **tools**: make `choco install` script more readable (Aviv Keller) [#54002](https://github.com/nodejs/node/pull/54002)
* \[[`779e6bdd5e`](https://github.com/nodejs/node/commit/779e6bdd5e)] - **tools**: bump Rollup from 4.18.1 to 4.22.4 for `lint-md` (dependabot\[bot]) [#55093](https://github.com/nodejs/node/pull/55093)
* \[[`0257102299`](https://github.com/nodejs/node/commit/0257102299)] - **tools**: unlock versions of irrelevant DB deps (Michaël Zasso) [#55042](https://github.com/nodejs/node/pull/55042)
* \[[`f43424ac2d`](https://github.com/nodejs/node/commit/f43424ac2d)] - **tools**: remove redudant code from eslint require rule (Aviv Keller) [#54892](https://github.com/nodejs/node/pull/54892)
* \[[`6a52e81260`](https://github.com/nodejs/node/commit/6a52e81260)] - **tools**: update error message for ICU in license-builder (Aviv Keller) [#54742](https://github.com/nodejs/node/pull/54742)
* \[[`cde6dccb65`](https://github.com/nodejs/node/commit/cde6dccb65)] - **tools**: refactor js2c.cc to use c++20 (Yagiz Nizipli) [#54849](https://github.com/nodejs/node/pull/54849)
* \[[`59c7c55aad`](https://github.com/nodejs/node/commit/59c7c55aad)] - **tools**: bump the eslint group in /tools/eslint with 7 updates (dependabot\[bot]) [#54821](https://github.com/nodejs/node/pull/54821)
* \[[`c6269cb069`](https://github.com/nodejs/node/commit/c6269cb069)] - **tools**: fix path of abseil file in v8.gyp (Michaël Zasso) [#54659](https://github.com/nodejs/node/pull/54659)
* \[[`d17fefcd71`](https://github.com/nodejs/node/commit/d17fefcd71)] - **tools**: update github\_reporter to 1.7.1 (Node.js GitHub Bot) [#54951](https://github.com/nodejs/node/pull/54951)
* \[[`29a4fcf918`](https://github.com/nodejs/node/commit/29a4fcf918)] - **tty**: fix links for terminal colors (Aviv Keller) [#54596](https://github.com/nodejs/node/pull/54596)
* \[[`e42ad5e80c`](https://github.com/nodejs/node/commit/e42ad5e80c)] - **util**: update ansi regex (Aviv Keller) [#54865](https://github.com/nodejs/node/pull/54865)
* \[[`b5aae52c71`](https://github.com/nodejs/node/commit/b5aae52c71)] - _**Revert**_ "**util**: move util.\_extend to eol" (Marco Ippolito) [#53429](https://github.com/nodejs/node/pull/53429)
* \[[`deb5effe01`](https://github.com/nodejs/node/commit/deb5effe01)] - **v8**: out of bounds copy (Robert Nagy) [#55261](https://github.com/nodejs/node/pull/55261)
* \[[`3b0617dd19`](https://github.com/nodejs/node/commit/3b0617dd19)] - **vm**: migrate ContextifyScript to cppgc (Joyee Cheung) [#52295](https://github.com/nodejs/node/pull/52295)
* \[[`35b8e5cb0c`](https://github.com/nodejs/node/commit/35b8e5cb0c)] - _**Revert**_ "**vm,src**: add property query interceptors" (Michaël Zasso) [#53348](https://github.com/nodejs/node/pull/53348)
* \[[`d1f18b0bf1`](https://github.com/nodejs/node/commit/d1f18b0bf1)] - **vm,src**: add property query interceptors (Michaël Zasso) [#53172](https://github.com/nodejs/node/pull/53172)
* \[[`89a2f565b7`](https://github.com/nodejs/node/commit/89a2f565b7)] - **watch**: preserve output when gracefully restarted (Théo LUDWIG) [#54323](https://github.com/nodejs/node/pull/54323)
* \[[`6b9413e41a`](https://github.com/nodejs/node/commit/6b9413e41a)] - **worker**: throw InvalidStateError in postMessage after close (devstone) [#55206](https://github.com/nodejs/node/pull/55206)
* \[[`6031a4bc7c`](https://github.com/nodejs/node/commit/6031a4bc7c)] - **worker**: handle `--input-type` more consistently (Antoine du Hamel) [#54979](https://github.com/nodejs/node/pull/54979)
* \[[`5b3f3c5a3b`](https://github.com/nodejs/node/commit/5b3f3c5a3b)] - **zlib**: throw brotli initialization error from c++ (Yagiz Nizipli) [#54698](https://github.com/nodejs/node/pull/54698)
* \[[`c42d8461b0`](https://github.com/nodejs/node/commit/c42d8461b0)] - **zlib**: remove prototype primordials usage (Yagiz Nizipli) [#54695](https://github.com/nodejs/node/pull/54695)

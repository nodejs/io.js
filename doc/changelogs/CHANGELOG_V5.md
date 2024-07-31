# Node.js 5 ChangeLog

<!--lint disable maximum-line-length no-literal-urls prohibited-strings-->

<table>
<tr>
<th>Stable</th>
</tr>
<tr>
<td>
<a href="#5.12.0">5.12.0</a><br/>
<a href="#5.11.1">5.11.1</a><br/>
<a href="#5.11.0">5.11.0</a><br/>
<a href="#5.10.1">5.10.1</a><br/>
<a href="#5.10.0">5.10.0</a><br/>
<a href="#5.9.1">5.9.1</a><br/>
<a href="#5.9.0">5.9.0</a><br/>
<a href="#5.8.0">5.8.0</a><br/>
<a href="#5.7.1">5.7.1</a><br/>
<a href="#5.7.0">5.7.0</a><br/>
<a href="#5.6.0">5.6.0</a><br/>
<a href="#5.5.0">5.5.0</a><br/>
<a href="#5.4.1">5.4.1</a><br/>
<a href="#5.4.0">5.4.0</a><br/>
<a href="#5.3.0">5.3.0</a><br/>
<a href="#5.2.0">5.2.0</a><br/>
<a href="#5.1.1">5.1.1</a><br/>
<a href="#5.1.0">5.1.0</a><br/>
<a href="#5.0.0">5.0.0</a><br/>
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
  * [4.x](CHANGELOG_V4.md)
  * [0.12.x](CHANGELOG_V012.md)
  * [0.10.x](CHANGELOG_V010.md)
  * [io.js](CHANGELOG_IOJS.md)
  * [Archive](CHANGELOG_ARCHIVE.md)

_Note_: Official support for the v5 release line is scheduled to expire
around June 2016. Users of v5 should upgrade to [Node.js v6](CHANGELOG_V6.md).

<a id="5.12.0"></a>

## 2016-06-23, Version 5.12.0 (Stable), @evanlucas

### Notable changes

This is a security release. All Node.js users should consult the security release summary at <https://nodejs.org/en/blog/vulnerability/june-2016-security-releases> for details on patched vulnerabilities.

* **buffer**
  * backport allocUnsafeSlow (Сковорода Никита Андреевич) [#7169](https://github.com/nodejs/node/pull/7169)
  * ignore negative allocation lengths (Anna Henningsen) [#7221](https://github.com/nodejs/node/pull/7221)
* **deps**: backport 3a9bfec from v8 upstream (Ben Noordhuis) [nodejs/node-private#40](https://github.com/nodejs/node-private/pull/40)
  * Fixes a Buffer overflow vulnerability discovered in v8. More details can be found in the CVE (CVE-2016-1699).

### Commits

* \[[`0ca0827b71`](https://github.com/nodejs/node/commit/0ca0827b71)] - **(SEMVER-MINOR)** **buffer**: backport allocUnsafeSlow (Сковорода Никита Андреевич) [#7169](https://github.com/nodejs/node/pull/7169)
* \[[`27785aeb37`](https://github.com/nodejs/node/commit/27785aeb37)] - **buffer**: ignore negative allocation lengths (Anna Henningsen) [#7221](https://github.com/nodejs/node/pull/7221)
* \[[`34b96c1322`](https://github.com/nodejs/node/commit/34b96c1322)] - **deps**: backport 3a9bfec from v8 upstream (Ben Noordhuis) [nodejs/node-private#40](https://github.com/nodejs/node-private/pull/40)
* \[[`2ebeb82852`](https://github.com/nodejs/node/commit/2ebeb82852)] - **test**: fix test-net-\* error code check for getaddrinfo(3) (Natanael Copa) [#5099](https://github.com/nodejs/node/pull/5099)
* \[[`03d36aea4f`](https://github.com/nodejs/node/commit/03d36aea4f)] - **(SEMVER-MINOR)** **test**: add buffer testcase for resetting kZeroFill (Сковорода Никита Андреевич) [#7169](https://github.com/nodejs/node/pull/7169)

<a id="5.11.1"></a>

## 2016-05-05, Version 5.11.1 (Stable), @evanlucas

### Notable changes

* **buffer**: safeguard against accidental kNoZeroFill (Сковорода Никита Андреевич) [nodejs/node-private#35](https://github.com/nodejs/node-private/pull/35)
* **deps**: upgrade openssl sources to 1.0.2h (Shigeki Ohtsu) [#6552](https://github.com/nodejs/node/pull/6552)

### Commits

* \[[`35f06df782`](https://github.com/nodejs/node/commit/35f06df782)] - **buffer**: safeguard against accidental kNoZeroFill (Сковорода Никита Андреевич) [nodejs/node-private#35](https://github.com/nodejs/node-private/pull/35)
* \[[`99920480ae`](https://github.com/nodejs/node/commit/99920480ae)] - **buffer**: fix a typo in Buffer example code (Mr C0B) [#6361](https://github.com/nodejs/node/pull/6361)
* \[[`d9f7b025d4`](https://github.com/nodejs/node/commit/d9f7b025d4)] - **deps**: update openssl asm and asm\_obsolete files (Shigeki Ohtsu) [#6552](https://github.com/nodejs/node/pull/6552)
* \[[`f316fd20a0`](https://github.com/nodejs/node/commit/f316fd20a0)] - **deps**: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) [nodejs/io.js#1836](https://github.com/nodejs/io.js/pull/1836)
* \[[`263cc34657`](https://github.com/nodejs/node/commit/263cc34657)] - **deps**: fix asm build error of openssl in x86\_win32 (Shigeki Ohtsu) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`889d1151de`](https://github.com/nodejs/node/commit/889d1151de)] - **deps**: fix openssl assembly error on ia32 win32 (Fedor Indutny) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`ba49b636b8`](https://github.com/nodejs/node/commit/ba49b636b8)] - **deps**: copy all openssl header files to include dir (Shigeki Ohtsu) [#6552](https://github.com/nodejs/node/pull/6552)
* \[[`cdad83a789`](https://github.com/nodejs/node/commit/cdad83a789)] - **deps**: upgrade openssl sources to 1.0.2h (Shigeki Ohtsu) [#6552](https://github.com/nodejs/node/pull/6552)
* \[[`c1ddefdd79`](https://github.com/nodejs/node/commit/c1ddefdd79)] - **openssl**: fix keypress requirement in apps on win32 (Shigeki Ohtsu) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`bec5d50f1e`](https://github.com/nodejs/node/commit/bec5d50f1e)] - **test**: fix alpn tests for openssl1.0.2h (Shigeki Ohtsu) [#6552](https://github.com/nodejs/node/pull/6552)

<a id="5.11.0"></a>

## 2016-04-20, Version 5.11.0 (Stable), @thealphanerd

## Notable Changes

* **Buffer**:
  * `Buffer.prototype.compare` can now compare sub-ranges of two Buffers. (James M Snell) [#5880](https://github.com/nodejs/node/pull/5880)
* **deps**:
  * update to http-parser 2.7.0 (Fedor Indutny) [#6279](https://github.com/nodejs/node/pull/6279)
  * update ESLint to 2.7.0 (silverwind) [#6132](https://github.com/nodejs/node/pull/6132)
* **net**:
  * adds support for passing DNS lookup hints to `createConnection()` (Colin Ihrig) [#6000](https://github.com/nodejs/node/pull/6000)
* **node**:
  * Make the builtin libraries available for the `--eval` and `--print` CLI options (Anna Henningsen) [#6207](https://github.com/nodejs/node/pull/6207)
* **npm**:
  * upgrade npm to 3.8.6 (Kat Marchán) [#6153](https://github.com/nodejs/node/pull/6153)
* **repl**:
  * Pressing enter in the repl will repeat the last command by default if no input has been received. This behaviour was in node previously and was not removed intentionally. (Rich Trott) [#6090](https://github.com/nodejs/node/pull/6090)
* **src**:
  * add SIGINFO to supported signals (James Reggio) [#6093](https://github.com/nodejs/node/pull/6093)
* **streams**:
  * Fix a regression that caused by net streams requesting multiple chunks synchronously when combined with cork/uncork (Matteo Collina) [#6164](https://github.com/nodejs/node/pull/6164)
* **zlib**:
  * The flushing flag is now configurable allowing for decompression of partial data (Anna Henningsen) [#6069](https://github.com/nodejs/node/pull/6069)

### Commits

* \[[`14fcb1dded`](https://github.com/nodejs/node/commit/14fcb1dded)] - **assert**: respect assert.doesNotThrow message. (Ilya Shaisultanov) [#2407](https://github.com/nodejs/node/pull/2407)
* \[[`332f7382bb`](https://github.com/nodejs/node/commit/332f7382bb)] - **benchmark**: add module loader benchmark parameter (Brian White) [#5172](https://github.com/nodejs/node/pull/5172)
* \[[`473f086a94`](https://github.com/nodejs/node/commit/473f086a94)] - **(SEMVER-MINOR)** **buffer**: add Buffer.prototype.compare by offset (James M Snell) [#5880](https://github.com/nodejs/node/pull/5880)
* \[[`d44540f5af`](https://github.com/nodejs/node/commit/d44540f5af)] - **buffer**: standardize array index check (Trevor Norris) [#6084](https://github.com/nodejs/node/pull/6084)
* \[[`bd12d72e0c`](https://github.com/nodejs/node/commit/bd12d72e0c)] - **build**: fix make tar-headers for Linux (Gibson Fahnestock) [#5978](https://github.com/nodejs/node/pull/5978)
* \[[`3c8d404a82`](https://github.com/nodejs/node/commit/3c8d404a82)] - **build**: allow test-ci to run tests in parallel (Johan Bergström) [#6208](https://github.com/nodejs/node/pull/6208)
* \[[`a5f8d0c6ef`](https://github.com/nodejs/node/commit/a5f8d0c6ef)] - **build**: remove -f{data,function}-sections flags (Ben Noordhuis) [#6077](https://github.com/nodejs/node/pull/6077)
* \[[`adfb1a4bb0`](https://github.com/nodejs/node/commit/adfb1a4bb0)] - **child\_process**: add nullptr checks after allocs (Anna Henningsen) [#6256](https://github.com/nodejs/node/pull/6256)
* \[[`1fb40524ee`](https://github.com/nodejs/node/commit/1fb40524ee)] - **(SEMVER-MINOR)** **debugger**: run last command on presssing enter (Rich Trott) [#6090](https://github.com/nodejs/node/pull/6090)
* \[[`5305831184`](https://github.com/nodejs/node/commit/5305831184)] - **deps**: update to http-parser 2.7.0 (Fedor Indutny) [#6279](https://github.com/nodejs/node/pull/6279)
* \[[`8ae200c768`](https://github.com/nodejs/node/commit/8ae200c768)] - **deps**: floating fix for npm's test-node script (Kat Marchán) [#6153](https://github.com/nodejs/node/pull/6153)
* \[[`e3e544eb96`](https://github.com/nodejs/node/commit/e3e544eb96)] - **deps**: upgrade npm to 3.8.6 (Kat Marchán) [#6153](https://github.com/nodejs/node/pull/6153)
* \[[`a7104e4516`](https://github.com/nodejs/node/commit/a7104e4516)] - **deps**: cherry-pick 1383d00 from v8 upstream (Fedor Indutny) [#6179](https://github.com/nodejs/node/pull/6179)
* \[[`632e6b9617`](https://github.com/nodejs/node/commit/632e6b9617)] - **deps**: backport 125ac66 from v8 upstream (Myles Borins) [#6086](https://github.com/nodejs/node/pull/6086)
* \[[`4b8376986a`](https://github.com/nodejs/node/commit/4b8376986a)] - **doc**: git mv to .md (Robert Jefe Lindstaedt) [#4747](https://github.com/nodejs/node/pull/4747)
* \[[`e6f4a175d4`](https://github.com/nodejs/node/commit/e6f4a175d4)] - **doc**: add full example for zlib.flush() (Anna Henningsen) [#6172](https://github.com/nodejs/node/pull/6172)
* \[[`50f3f10ce6`](https://github.com/nodejs/node/commit/50f3f10ce6)] - **doc**: note that zlib.flush acts after pending writes (Anna Henningsen) [#6172](https://github.com/nodejs/node/pull/6172)
* \[[`985685d170`](https://github.com/nodejs/node/commit/985685d170)] - **doc**: fix broken references (Alexander Gromnitsky) [#6100](https://github.com/nodejs/node/pull/6100)
* \[[`d66d883a85`](https://github.com/nodejs/node/commit/d66d883a85)] - **doc**: path.format provide more examples (John Eversole) [#5838](https://github.com/nodejs/node/pull/5838)
* \[[`dc1552e321`](https://github.com/nodejs/node/commit/dc1552e321)] - **doc**: replace functions with arrow functions (abouthiroppy) [#6203](https://github.com/nodejs/node/pull/6203)
* \[[`fa04dfc307`](https://github.com/nodejs/node/commit/fa04dfc307)] - **doc**: DCO anchor that doesn't change (William Kapke) [#6257](https://github.com/nodejs/node/pull/6257)
* \[[`b49a5b33b5`](https://github.com/nodejs/node/commit/b49a5b33b5)] - **doc**: fix http response event, Agent#getName (Matthew Douglass) [#5993](https://github.com/nodejs/node/pull/5993)
* \[[`3b00d7a5b1`](https://github.com/nodejs/node/commit/3b00d7a5b1)] - **doc**: document intention and dangers of fs module Buffer API (Nikolai Vavilov) [#6020](https://github.com/nodejs/node/pull/6020)
* \[[`3bc31526bb`](https://github.com/nodejs/node/commit/3bc31526bb)] - **doc**: explain differences in console.assert between node and browsers (James M Snell) [#6169](https://github.com/nodejs/node/pull/6169)
* \[[`3f73502662`](https://github.com/nodejs/node/commit/3f73502662)] - **doc**: native module reloading is not supported (Bryan English) [#6168](https://github.com/nodejs/node/pull/6168)
* \[[`5f9c8297f1`](https://github.com/nodejs/node/commit/5f9c8297f1)] - **doc**: clarify fs.watch() and inodes on linux, os x (Joran Dirk Greef) [#6099](https://github.com/nodejs/node/pull/6099)
* \[[`f3c0b78ae4`](https://github.com/nodejs/node/commit/f3c0b78ae4)] - **doc**: add domain postmortem (Trevor Norris) [#6159](https://github.com/nodejs/node/pull/6159)
* \[[`a91834e743`](https://github.com/nodejs/node/commit/a91834e743)] - **doc**: add stefanmb to collaborators (Stefan Budeanu) [#6227](https://github.com/nodejs/node/pull/6227)
* \[[`117348d082`](https://github.com/nodejs/node/commit/117348d082)] - **doc**: add iWuzHere to collaborators (Imran Iqbal) [#6226](https://github.com/nodejs/node/pull/6226)
* \[[`a1c46b63e8`](https://github.com/nodejs/node/commit/a1c46b63e8)] - **doc**: add santigimeno to collaborators (Santiago Gimeno) [#6225](https://github.com/nodejs/node/pull/6225)
* \[[`976e4bb3da`](https://github.com/nodejs/node/commit/976e4bb3da)] - **doc**: add addaleax to collaborators (Anna Henningsen) [#6224](https://github.com/nodejs/node/pull/6224)
* \[[`4fa949ef75`](https://github.com/nodejs/node/commit/4fa949ef75)] - **doc**: fix incorrect references in buffer docs (Amery) [#6194](https://github.com/nodejs/node/pull/6194)
* \[[`b26fea1595`](https://github.com/nodejs/node/commit/b26fea1595)] - **doc**: add copy about how to curl SHA256.txt (Myles Borins) [#6120](https://github.com/nodejs/node/pull/6120)
* \[[`daaad47099`](https://github.com/nodejs/node/commit/daaad47099)] - **doc**: clarification for maxBuffer and Unicode output (James M Snell) [#6030](https://github.com/nodejs/node/pull/6030)
* \[[`5e6915f374`](https://github.com/nodejs/node/commit/5e6915f374)] - **doc**: describe child.kill() pitfalls on linux (Robert Jefe Lindstaedt) [#2098](https://github.com/nodejs/node/issues/2098)
* \[[`a40d0e8f9d`](https://github.com/nodejs/node/commit/a40d0e8f9d)] - **doc**: fix scrolling on iOS devices (Luigi Pinca) [#5878](https://github.com/nodejs/node/pull/5878)
* \[[`a81fca4f99`](https://github.com/nodejs/node/commit/a81fca4f99)] - **doc**: add topic - event loop, timers, `nextTick()` (Jeff Harris) [#4936](https://github.com/nodejs/node/pull/4936)
* \[[`440d1172fd`](https://github.com/nodejs/node/commit/440d1172fd)] - **doc**: add example using algorithms not directly exposed (Brad Hill) [#6108](https://github.com/nodejs/node/pull/6108)
* \[[`96ad5c5303`](https://github.com/nodejs/node/commit/96ad5c5303)] - **doc**: update openssl LICENSE using license-builder.sh (Steven R. Loomis) [#6065](https://github.com/nodejs/node/pull/6065)
* \[[`07829b0bc9`](https://github.com/nodejs/node/commit/07829b0bc9)] - **doc**: simple doc typo fix (Brendon Pierson) [#6041](https://github.com/nodejs/node/pull/6041)
* \[[`bc0ee06226`](https://github.com/nodejs/node/commit/bc0ee06226)] - **doc**: note about Android support (Rich Trott) [#6040](https://github.com/nodejs/node/pull/6040)
* \[[`60a73a2ed2`](https://github.com/nodejs/node/commit/60a73a2ed2)] - **doc**: fix a typo in 5.10.1's changelog (Vladimir Varankin) [#6076](https://github.com/nodejs/node/pull/6076)
* \[[`b57be92c1b`](https://github.com/nodejs/node/commit/b57be92c1b)] - **gitignore**: adding .vs/ directory to .gitignore (Mike Kaufman) [#6070](https://github.com/nodejs/node/pull/6070)
* \[[`6e891c7ad4`](https://github.com/nodejs/node/commit/6e891c7ad4)] - **gitignore**: ignore VS 2015 \*.VC.opendb files (Mike Kaufman) [#6070](https://github.com/nodejs/node/pull/6070)
* \[[`abd101be1a`](https://github.com/nodejs/node/commit/abd101be1a)] - **http**: disallow sending obviously invalid status codes (Brian White) [#6291](https://github.com/nodejs/node/pull/6291)
* \[[`16b23b2c28`](https://github.com/nodejs/node/commit/16b23b2c28)] - **http**: skip body and next message of CONNECT res (Fedor Indutny) [#6279](https://github.com/nodejs/node/pull/6279)
* \[[`a259ee4018`](https://github.com/nodejs/node/commit/a259ee4018)] - **http**: unref socket timer on parser execute (Fedor Indutny) [#6286](https://github.com/nodejs/node/pull/6286)
* \[[`d4abca5b27`](https://github.com/nodejs/node/commit/d4abca5b27)] - **lib**: remove bootstrap global context indirection (Jeremiah Senkpiel) [#5881](https://github.com/nodejs/node/pull/5881)
* \[[`c8783aff21`](https://github.com/nodejs/node/commit/c8783aff21)] - **lib,test,tools**: alignment on variable assignments (Rich Trott) [#6242](https://github.com/nodejs/node/pull/6242)
* \[[`d5d4f194f1`](https://github.com/nodejs/node/commit/d5d4f194f1)] - **net**: replace `__defineGetter__` with defineProperty (Fedor Indutny) [#6284](https://github.com/nodejs/node/pull/6284)
* \[[`6d9c0c9aa7`](https://github.com/nodejs/node/commit/6d9c0c9aa7)] - **(SEMVER-MINOR)** **net**: support DNS hints in createConnection() (Colin Ihrig) [#6000](https://github.com/nodejs/node/pull/6000)
* \[[`457f24f19c`](https://github.com/nodejs/node/commit/457f24f19c)] - **(SEMVER-MINOR)** **node**: make builtin libs available for `--eval` (Anna Henningsen) [#6207](https://github.com/nodejs/node/pull/6207)
* \[[`fc89d17656`](https://github.com/nodejs/node/commit/fc89d17656)] - **path**: fixing a test that breaks on some machines. (Mike Kaufman) [#6067](https://github.com/nodejs/node/pull/6067)
* \[[`1d408099b7`](https://github.com/nodejs/node/commit/1d408099b7)] - **process**: fix incorrect usage of assert.fail() (Rich Trott) [#6211](https://github.com/nodejs/node/pull/6211)
* \[[`07c9f981d6`](https://github.com/nodejs/node/commit/07c9f981d6)] - **(SEMVER-MINOR)** **repl**: keep the built-in modules non-enumerable (Anna Henningsen) [#6207](https://github.com/nodejs/node/pull/6207)
* \[[`5382deaa18`](https://github.com/nodejs/node/commit/5382deaa18)] - **repl**: don’t complete non-simple expressions (Anna Henningsen) [#6192](https://github.com/nodejs/node/pull/6192)
* \[[`2254f1a175`](https://github.com/nodejs/node/commit/2254f1a175)] - **repl**: refactor repl.js (Rich Trott) [#6071](https://github.com/nodejs/node/pull/6071)
* \[[`7d54d85269`](https://github.com/nodejs/node/commit/7d54d85269)] - **(SEMVER-MINOR)** **src**: add SIGINFO to supported signals (James Reggio) [#6093](https://github.com/nodejs/node/pull/6093)
* \[[`fbc99ba4f1`](https://github.com/nodejs/node/commit/fbc99ba4f1)] - **src**: add missing 'inline' keywords (Ben Noordhuis) [#6056](https://github.com/nodejs/node/pull/6056)
* \[[`20bb92f5c8`](https://github.com/nodejs/node/commit/20bb92f5c8)] - **src**: use size\_t for http parser array size fields (Ben Noordhuis) [#5969](https://github.com/nodejs/node/pull/5969)
* \[[`2fd8be2dbe`](https://github.com/nodejs/node/commit/2fd8be2dbe)] - **src**: replace ARRAY\_SIZE with typesafe arraysize (Ben Noordhuis) [#5969](https://github.com/nodejs/node/pull/5969)
* \[[`4392b4aee0`](https://github.com/nodejs/node/commit/4392b4aee0)] - **stream**: Fix readableState.awaitDrain mechanism (Anna Henningsen) [#6023](https://github.com/nodejs/node/pull/6023)
* \[[`20dcdd365b`](https://github.com/nodejs/node/commit/20dcdd365b)] - **stream\_base**: expose `bytesRead` getter (Fedor Indutny) [#6284](https://github.com/nodejs/node/pull/6284)
* \[[`f69416c06e`](https://github.com/nodejs/node/commit/f69416c06e)] - **streams**: support unlimited synchronous cork/uncork cycles (Matteo Collina) [#6164](https://github.com/nodejs/node/pull/6164)
* \[[`4bfed26d1a`](https://github.com/nodejs/node/commit/4bfed26d1a)] - **test**: add zlib close-after-error regression test (Anna Henningsen) [#6270](https://github.com/nodejs/node/pull/6270)
* \[[`99d0a61441`](https://github.com/nodejs/node/commit/99d0a61441)] - **test**: move more tests from sequential to parallel (Santiago Gimeno) [#6187](https://github.com/nodejs/node/pull/6187)
* \[[`96be986a77`](https://github.com/nodejs/node/commit/96be986a77)] - **test**: assert - fixed error messages to match the tests (surya panikkal) [#6241](https://github.com/nodejs/node/pull/6241)
* \[[`4e4efb756e`](https://github.com/nodejs/node/commit/4e4efb756e)] - **test**: add test for responses to HTTP CONNECT req (Josh Leder) [#6279](https://github.com/nodejs/node/pull/6279)
* \[[`5b42ef5dfe`](https://github.com/nodejs/node/commit/5b42ef5dfe)] - **test**: move debugger tests to sequential (Rich Trott) [#6205](https://github.com/nodejs/node/pull/6205)
* \[[`9856b804e9`](https://github.com/nodejs/node/commit/9856b804e9)] - **test**: move some test from sequential to parallel (Santiago Gimeno) [#6087](https://github.com/nodejs/node/pull/6087)
* \[[`1d130d0203`](https://github.com/nodejs/node/commit/1d130d0203)] - **test**: move the debugger tests back to parallel (Santiago Gimeno) [#6246](https://github.com/nodejs/node/pull/6246)
* \[[`c0e9c94868`](https://github.com/nodejs/node/commit/c0e9c94868)] - **test**: fix issues for ESLint 2.7.0 (silverwind) [#6132](https://github.com/nodejs/node/pull/6132)
* \[[`056a258173`](https://github.com/nodejs/node/commit/056a258173)] - **test**: fix flaky test-http-set-timeout-server (Santiago Gimeno) [#6248](https://github.com/nodejs/node/pull/6248)
* \[[`be993fcf6c`](https://github.com/nodejs/node/commit/be993fcf6c)] - **test**: fix test-net-settimeout flakiness (Santiago Gimeno) [#6166](https://github.com/nodejs/node/pull/6166)
* \[[`a38b614ae9`](https://github.com/nodejs/node/commit/a38b614ae9)] - **test**: fix flaky test-child-process-fork-net (Rich Trott) [#6138](https://github.com/nodejs/node/pull/6138)
* \[[`476535cc0e`](https://github.com/nodejs/node/commit/476535cc0e)] - **test**: fix flaky test-http-client-abort (Rich Trott) [#6124](https://github.com/nodejs/node/pull/6124)
* \[[`6bb7999bd6`](https://github.com/nodejs/node/commit/6bb7999bd6)] - **test**: refactor test-file-write-stream3 (Rich Trott) [#6050](https://github.com/nodejs/node/pull/6050)
* \[[`a27e95231e`](https://github.com/nodejs/node/commit/a27e95231e)] - **test**: enforce strict mode for test-domain-crypto (Rich Trott) [#6047](https://github.com/nodejs/node/pull/6047)
* \[[`8da4bad1c9`](https://github.com/nodejs/node/commit/8da4bad1c9)] - **test**: fix pummel test failures (Rich Trott) [#6012](https://github.com/nodejs/node/pull/6012)
* \[[`edd8a15508`](https://github.com/nodejs/node/commit/edd8a15508)] - **test,repl**: use deepStrictEqual for false-y values (Jeremiah Senkpiel) [#6196](https://github.com/nodejs/node/pull/6196)
* \[[`48ecc0b6b5`](https://github.com/nodejs/node/commit/48ecc0b6b5)] - **test,tools**: enable linting for undefined vars (Rich Trott) [#6255](https://github.com/nodejs/node/pull/6255)
* \[[`d809c84bf8`](https://github.com/nodejs/node/commit/d809c84bf8)] - **test,vm**: enable strict mode for vm tests (Rich Trott) [#6209](https://github.com/nodejs/node/pull/6209)
* \[[`4a1dfdcc0f`](https://github.com/nodejs/node/commit/4a1dfdcc0f)] - **tools**: lint rule for assert.fail() (Rich Trott) [#6261](https://github.com/nodejs/node/pull/6261)
* \[[`fff6a84da5`](https://github.com/nodejs/node/commit/fff6a84da5)] - **tools**: enable linting for v8\_prof\_processor.js (Rich Trott) [#6262](https://github.com/nodejs/node/pull/6262)
* \[[`a2ca347803`](https://github.com/nodejs/node/commit/a2ca347803)] - **tools**: move message listener to worker objects (Brian White) [#6212](https://github.com/nodejs/node/pull/6212)
* \[[`f201b01bf7`](https://github.com/nodejs/node/commit/f201b01bf7)] - **tools**: improve js linter (Brian White) [#5638](https://github.com/nodejs/node/pull/5638)
* \[[`be070d775f`](https://github.com/nodejs/node/commit/be070d775f)] - **tools**: lint for alignment of variable assignments (Rich Trott) [#6242](https://github.com/nodejs/node/pull/6242)
* \[[`d9b8758f47`](https://github.com/nodejs/node/commit/d9b8758f47)] - **tools**: update ESLint to 2.7.0 (silverwind) [#6132](https://github.com/nodejs/node/pull/6132)
* \[[`a6056c453e`](https://github.com/nodejs/node/commit/a6056c453e)] - **tools**: fix license-builder.sh again for ICU (Steven R. Loomis) [#6068](https://github.com/nodejs/node/pull/6068)
* \[[`fabc33a075`](https://github.com/nodejs/node/commit/fabc33a075)] - **tools**: remove simplejson dependency (Sakthipriyan Vairamani) [#6101](https://github.com/nodejs/node/pull/6101)
* \[[`d516412cd5`](https://github.com/nodejs/node/commit/d516412cd5)] - **tools,doc**: parse types in braces everywhere (Alexander Makarenko) [#5329](https://github.com/nodejs/node/pull/5329)
* \[[`69eb4a6834`](https://github.com/nodejs/node/commit/69eb4a6834)] - **tools,doc**: fix json for grouped optional params (firedfox) [#5977](https://github.com/nodejs/node/pull/5977)
* \[[`a2dd848764`](https://github.com/nodejs/node/commit/a2dd848764)] - **tools,doc**: fix incomplete json produced by doctool (firedfox) [#5966](https://github.com/nodejs/node/pull/5966)
* \[[`bad006f2e1`](https://github.com/nodejs/node/commit/bad006f2e1)] - **zlib**: fix use after null when calling .close (James Lal) [#5982](https://github.com/nodejs/node/pull/5982)
* \[[`83bc0a2ac9`](https://github.com/nodejs/node/commit/83bc0a2ac9)] - **(SEMVER-MINOR)** **zlib**: Make the finish flush flag configurable (Anna Henningsen) [#6069](https://github.com/nodejs/node/pull/6069)
* \[[`2c23e14d5d`](https://github.com/nodejs/node/commit/2c23e14d5d)] - **(SEMVER-MINOR)** **zlib**: detect gzip files when using unzip\* (Anna Henningsen) [#5884](https://github.com/nodejs/node/pull/5884)
* \[[`61167c3e23`](https://github.com/nodejs/node/commit/61167c3e23)] - **zlib**: fix gzip member head/buffer boundary issue (Anna Henningsen) [#5973](https://github.com/nodejs/node/pull/5973)

<a id="5.10.1"></a>

## 2016-04-05, Version 5.10.1 (Stable), @thealphanerd

### Notable changes

* **http**:
  * Enclose IPv6 Host header in square brackets. This will enable proper separation of the host address from any port reference (Mihai Potra) [#5314](https://github.com/nodejs/node/pull/5314)

* **path**:
  * Make win32.isAbsolute more consistent (Brian White) [#6028](https://github.com/nodejs/node/pull/6028)

### Commits

* \[[`0f5a51ae4b`](https://github.com/nodejs/node/commit/0f5a51ae4b)] - **assert**: Check typed array view type in deepEqual (Anna Henningsen) [#5910](https://github.com/nodejs/node/pull/5910)
* \[[`e966d1f5db`](https://github.com/nodejs/node/commit/e966d1f5db)] - **buffer**: don't set `kNoZeroFill` flag in allocUnsafe (Vladimir Kurchatkin) [#6007](https://github.com/nodejs/node/pull/6007)
* \[[`3f75751c2e`](https://github.com/nodejs/node/commit/3f75751c2e)] - **build**: introduce ci targets for lint/benchmark (Johan Bergström) [#5921](https://github.com/nodejs/node/pull/5921)
* \[[`781290b61d`](https://github.com/nodejs/node/commit/781290b61d)] - **doc**: refine child\_process detach behaviour (Robert Jefe Lindstaedt) [#5330](https://github.com/nodejs/node/pull/5330)
* \[[`aa9fb03202`](https://github.com/nodejs/node/commit/aa9fb03202)] - **doc**: use HTTPS for links where possible (Rich Trott) [#6019](https://github.com/nodejs/node/pull/6019)
* \[[`dd25984838`](https://github.com/nodejs/node/commit/dd25984838)] - **doc**: note assert.throws() pitfall (Rich Trott) [#6029](https://github.com/nodejs/node/pull/6029)
* \[[`f879f5e68a`](https://github.com/nodejs/node/commit/f879f5e68a)] - **doc**: document unspecified behavior for buf.write\* methods (James M Snell) [#5925](https://github.com/nodejs/node/pull/5925)
* \[[`f12c3861e0`](https://github.com/nodejs/node/commit/f12c3861e0)] - **doc**: clarify stdout/stderr arguments to callback (James M Snell) [#6015](https://github.com/nodejs/node/pull/6015)
* \[[`ce173716be`](https://github.com/nodejs/node/commit/ce173716be)] - **doc**: add 'Command Line Options' to 'View on single page' (firedfox) [#6011](https://github.com/nodejs/node/pull/6011)
* \[[`7337ef6422`](https://github.com/nodejs/node/commit/7337ef6422)] - **doc**: minor argument formatting in stream.markdown (James M Snell) [#6016](https://github.com/nodejs/node/pull/6016)
* \[[`0ae5d027c6`](https://github.com/nodejs/node/commit/0ae5d027c6)] - **doc**: clarify that \_\_dirname is module local (James M Snell) [#6018](https://github.com/nodejs/node/pull/6018)
* \[[`8bec8aa41f`](https://github.com/nodejs/node/commit/8bec8aa41f)] - **doc**: consolidate timers docs in timers.markdown (Bryan English) [#5837](https://github.com/nodejs/node/pull/5837)
* \[[`0a13099c42`](https://github.com/nodejs/node/commit/0a13099c42)] - **etw**: add event messages (João Reis) [#5936](https://github.com/nodejs/node/pull/5936)
* \[[`c6ac6f2ea1`](https://github.com/nodejs/node/commit/c6ac6f2ea1)] - **http**: Corrects IPv6 address in Host header (Mihai Potra) [#5314](https://github.com/nodejs/node/pull/5314)
* \[[`8317778925`](https://github.com/nodejs/node/commit/8317778925)] - **meta**: add "joining a wg" section to WORKING\_GROUPS.md (Matteo Collina) [#5488](https://github.com/nodejs/node/pull/5488)
* \[[`f3f19ee5e2`](https://github.com/nodejs/node/commit/f3f19ee5e2)] - **net**: refactor self=this to arrow functions (Benjamin Gruenbaum) [#5857](https://github.com/nodejs/node/pull/5857)
* \[[`1c4007927d`](https://github.com/nodejs/node/commit/1c4007927d)] - **path**: fix win32.isAbsolute() inconsistency (Brian White) [#6028](https://github.com/nodejs/node/pull/6028)
* \[[`059b607a4f`](https://github.com/nodejs/node/commit/059b607a4f)] - **test**: make use of globals explicit (Rich Trott) [#6014](https://github.com/nodejs/node/pull/6014)
* \[[`cc8fcc5a07`](https://github.com/nodejs/node/commit/cc8fcc5a07)] - **test**: be explicit about polluting of `global` (Rich Trott) [#6017](https://github.com/nodejs/node/pull/6017)
* \[[`7db7a820b9`](https://github.com/nodejs/node/commit/7db7a820b9)] - **test**: make arch available in status files (Santiago Gimeno) [#5997](https://github.com/nodejs/node/pull/5997)
* \[[`02f2ebd9b4`](https://github.com/nodejs/node/commit/02f2ebd9b4)] - **test**: explicitly set global in test-repl (Rich Trott) [#6026](https://github.com/nodejs/node/pull/6026)
* \[[`2ab1237137`](https://github.com/nodejs/node/commit/2ab1237137)] - **test**: fix flaky test-net-socket-timeout-unref (Rich Trott) [#6003](https://github.com/nodejs/node/pull/6003)
* \[[`0127c2bd39`](https://github.com/nodejs/node/commit/0127c2bd39)] - **test**: fix test-dns.js flakiness (Rich Trott) [#5996](https://github.com/nodejs/node/pull/5996)
* \[[`6052ced37f`](https://github.com/nodejs/node/commit/6052ced37f)] - **test**: fix error message checks in test-module-loading (James M Snell) [#5986](https://github.com/nodejs/node/pull/5986)
* \[[`a40b0cb673`](https://github.com/nodejs/node/commit/a40b0cb673)] - **test**: refactor http-end-throw-socket-handling (Santiago Gimeno) [#5676](https://github.com/nodejs/node/pull/5676)
* \[[`96bb315262`](https://github.com/nodejs/node/commit/96bb315262)] - **test**: ensure \_handle property existence (Rich Trott) [#5916](https://github.com/nodejs/node/pull/5916)
* \[[`4f1fa2adeb`](https://github.com/nodejs/node/commit/4f1fa2adeb)] - **test**: fix offending max-len linter error (Sakthipriyan Vairamani) [#5980](https://github.com/nodejs/node/pull/5980)
* \[[`f14d71ccea`](https://github.com/nodejs/node/commit/f14d71ccea)] - **test**: stdin is not always a net.Socket (Jeremiah Senkpiel) [#5935](https://github.com/nodejs/node/pull/5935)
* \[[`50a062e691`](https://github.com/nodejs/node/commit/50a062e691)] - **tools**: remove obsolete lint config file (Rich Trott) [#5959](https://github.com/nodejs/node/pull/5959)
* \[[`7491fdcfe9`](https://github.com/nodejs/node/commit/7491fdcfe9)] - **tools**: remove disabling of already-disabled rule (Rich Trott) [#6013](https://github.com/nodejs/node/pull/6013)

<a id="5.10.0"></a>

## 2016-03-31, Version 5.10.0 (Stable), @evanlucas

### Notable changes

* **buffer**:
  * make byteLength work with ArrayBuffer & DataView (Jackson Tian) [#5255](https://github.com/nodejs/node/pull/5255)
  * backport --zero-fill-buffers command line option (James M Snell) [#5744](https://github.com/nodejs/node/pull/5744)
  * backport new buffer constructor APIs (James M Snell) [#5763](https://github.com/nodejs/node/pull/5763)
  * add swap16() and swap32() methods (James M Snell) [#5724](https://github.com/nodejs/node/pull/5724)
* **fs**: add the fs.mkdtemp() function. (Florian MARGAINE) [#5333](https://github.com/nodejs/node/pull/5333)
* **net**: emit host in lookup event (HUANG Wei) [#5598](https://github.com/nodejs/node/pull/5598)
* **node**: --no-browser-globals configure flag (Fedor Indutny) [#5853](https://github.com/nodejs/node/pull/5853)
* **npm**: Upgrade to v3.8.3. Fixes a security flaw in the use of authentication tokens in HTTP requests that
  would allow an attacker to set up a server that could collect tokens from users of the command-line interface.
  Authentication tokens have previously been sent with every request made by the CLI for logged-in users,
  regardless of the destination of the request. This update fixes this by only including those tokens for requests
  made against the registry or registries used for the current install. (Forrest L Norvell) [npm/node#6](https://github.com/npm/node/pull/6)
* **repl**: support standalone blocks (Prince J Wesley) [#5581](https://github.com/nodejs/node/pull/5581)
* **src**: override v8 thread defaults using cli options (Tom Gallacher) [#4344](https://github.com/nodejs/node/pull/4344)

### Commits

* \[[`2cbbaafca9`](https://github.com/nodejs/node/commit/2cbbaafca9)] - **async\_wrap**: don't abort on callback exception (Trevor Norris) [#5756](https://github.com/nodejs/node/pull/5756)
* \[[`6f16882733`](https://github.com/nodejs/node/commit/6f16882733)] - **async\_wrap**: notify post if intercepted exception (Trevor Norris) [#5756](https://github.com/nodejs/node/pull/5756)
* \[[`a4856122d3`](https://github.com/nodejs/node/commit/a4856122d3)] - **async\_wrap**: setupHooks now accepts object (Trevor Norris) [#5756](https://github.com/nodejs/node/pull/5756)
* \[[`ee83c956c5`](https://github.com/nodejs/node/commit/ee83c956c5)] - **(SEMVER-MINOR)** **buffer**: make byteLength work with ArrayBuffer & DataView (Jackson Tian) [#5255](https://github.com/nodejs/node/pull/5255)
* \[[`1f8e4b54ce`](https://github.com/nodejs/node/commit/1f8e4b54ce)] - **(SEMVER-MINOR)** **buffer**: add swap16() and swap32() methods (James M Snell) [#5724](https://github.com/nodejs/node/pull/5724)
* \[[`bdf933bece`](https://github.com/nodejs/node/commit/bdf933bece)] - **buffer**: changing let in for loops back to var (Gareth Ellis) [#5819](https://github.com/nodejs/node/pull/5819)
* \[[`c1534e7eaf`](https://github.com/nodejs/node/commit/c1534e7eaf)] - **(SEMVER-MINOR)** **buffer**: backport new buffer constructor APIs (James M Snell) [#5763](https://github.com/nodejs/node/pull/5763)
* \[[`3c02727055`](https://github.com/nodejs/node/commit/3c02727055)] - **(SEMVER-MINOR)** **buffer**: backport --zero-fill-buffers command line option (James M Snell) [#5744](https://github.com/nodejs/node/pull/5744)
* \[[`58b5c1e19f`](https://github.com/nodejs/node/commit/58b5c1e19f)] - **build**: add suport for x86 architecture (Robert Chiras) [#5544](https://github.com/nodejs/node/pull/5544)
* \[[`389f5a85e6`](https://github.com/nodejs/node/commit/389f5a85e6)] - **build**: add script to create Android .mk files (Robert Chiras) [#5544](https://github.com/nodejs/node/pull/5544)
* \[[`5ee5fa292f`](https://github.com/nodejs/node/commit/5ee5fa292f)] - **build**: add missing `openssl_fips%` to common.gypi (Fedor Indutny) [#5919](https://github.com/nodejs/node/pull/5919)
* \[[`5681ffecf7`](https://github.com/nodejs/node/commit/5681ffecf7)] - **build**: enable compilation for linuxOne (Michael Dawson) [#5941](https://github.com/nodejs/node/pull/5941)
* \[[`660ec9f889`](https://github.com/nodejs/node/commit/660ec9f889)] - **child\_process**: refactor self=this in socket\_list (Benjamin Gruenbaum) [#5860](https://github.com/nodejs/node/pull/5860)
* \[[`0928584444`](https://github.com/nodejs/node/commit/0928584444)] - **deps**: upgrade npm to 3.8.3 (Forrest L Norvell)
* \[[`ec1813199d`](https://github.com/nodejs/node/commit/ec1813199d)] - **deps**: backport 8d00c2c from v8 upstream (Ben Noordhuis) [#5577](https://github.com/nodejs/node/pull/5577)
* \[[`2a5c6d7006`](https://github.com/nodejs/node/commit/2a5c6d7006)] - **dns**: Refactor forEach to map (Benjamin Gruenbaum) [#5803](https://github.com/nodejs/node/pull/5803)
* \[[`6a6112a2f3`](https://github.com/nodejs/node/commit/6a6112a2f3)] - **dns**: Use object without protoype for map (Benjamin Gruenbaum) [#5843](https://github.com/nodejs/node/pull/5843)
* \[[`8fa0b5c1da`](https://github.com/nodejs/node/commit/8fa0b5c1da)] - **doc**: Add @mhdawson back to the CTC (James M Snell) [#5633](https://github.com/nodejs/node/pull/5633)
* \[[`858a524325`](https://github.com/nodejs/node/commit/858a524325)] - **doc**: typo: interal->internal. (Corey Kosak) [#5849](https://github.com/nodejs/node/pull/5849)
* \[[`5676a35bd9`](https://github.com/nodejs/node/commit/5676a35bd9)] - **doc**: explain path.format expected properties (John Eversole) [#5801](https://github.com/nodejs/node/pull/5801)
* \[[`29778393a0`](https://github.com/nodejs/node/commit/29778393a0)] - **doc**: use consistent event name parameter (Benjamin Gruenbaum) [#5850](https://github.com/nodejs/node/pull/5850)
* \[[`949b17ff6d`](https://github.com/nodejs/node/commit/949b17ff6d)] - **doc**: fix order of end tags of list after heading (firedfox) [#5874](https://github.com/nodejs/node/pull/5874)
* \[[`8e790b7a0c`](https://github.com/nodejs/node/commit/8e790b7a0c)] - **doc**: add instructions to only sign a release (Jeremiah Senkpiel) [#5876](https://github.com/nodejs/node/pull/5876)
* \[[`f1f9aff855`](https://github.com/nodejs/node/commit/f1f9aff855)] - **doc**: fix doc for Buffer.readInt32LE() (ghaiklor) [#5890](https://github.com/nodejs/node/pull/5890)
* \[[`731f7b8055`](https://github.com/nodejs/node/commit/731f7b8055)] - **etw**: fix descriptors of events 9 and 23 (João Reis) [#5742](https://github.com/nodejs/node/pull/5742)
* \[[`ccd81889fa`](https://github.com/nodejs/node/commit/ccd81889fa)] - **etw,build**: always generate .rc and .h files (João Reis) [#5657](https://github.com/nodejs/node/pull/5657)
* \[[`80155d398c`](https://github.com/nodejs/node/commit/80155d398c)] - **(SEMVER-MINOR)** **fs**: add the fs.mkdtemp() function. (Florian MARGAINE) [#5333](https://github.com/nodejs/node/pull/5333)
* \[[`bb28770aa1`](https://github.com/nodejs/node/commit/bb28770aa1)] - **governance**: remove target size for CTC (Rich Trott) [#5879](https://github.com/nodejs/node/pull/5879)
* \[[`63c601bc15`](https://github.com/nodejs/node/commit/63c601bc15)] - **http**: speed up checkIsHttpToken (Jackson Tian) [#4790](https://github.com/nodejs/node/pull/4790)
* \[[`ec6af31eba`](https://github.com/nodejs/node/commit/ec6af31eba)] - **lib**: rename /node.js to /bootstrap\_node.js (Jeremiah Senkpiel) [#5103](https://github.com/nodejs/node/pull/5103)
* \[[`91466b855f`](https://github.com/nodejs/node/commit/91466b855f)] - **lib**: refactor code with startsWith/endsWith (Jackson Tian) [#5753](https://github.com/nodejs/node/pull/5753)
* \[[`4bf2acaa1e`](https://github.com/nodejs/node/commit/4bf2acaa1e)] - **lib,src**: move src/node.js to lib/internal/node.js (Jeremiah Senkpiel) [#5103](https://github.com/nodejs/node/pull/5103)
* \[[`015cef25eb`](https://github.com/nodejs/node/commit/015cef25eb)] - **lib,src**: refactor src/node.js into internal files (Jeremiah Senkpiel) [#5103](https://github.com/nodejs/node/pull/5103)
* \[[`b07bc5d996`](https://github.com/nodejs/node/commit/b07bc5d996)] - **(SEMVER-MINOR)** **net**: emit host in lookup event (HUANG Wei) [#5598](https://github.com/nodejs/node/pull/5598)
* \[[`8363ede855`](https://github.com/nodejs/node/commit/8363ede855)] - **(SEMVER-MINOR)** **node**: --no-browser-globals configure flag (Fedor Indutny) [#5853](https://github.com/nodejs/node/pull/5853)
* \[[`a2ad21645f`](https://github.com/nodejs/node/commit/a2ad21645f)] - **querystring**: don't stringify bad surrogate pair (Brian White) [#5858](https://github.com/nodejs/node/pull/5858)
* \[[`427173204e`](https://github.com/nodejs/node/commit/427173204e)] - **(SEMVER-MINOR)** **repl**: support standalone blocks (Prince J Wesley) [#5581](https://github.com/nodejs/node/pull/5581)
* \[[`bfd723f3ba`](https://github.com/nodejs/node/commit/bfd723f3ba)] - **src**: Add missing `using v8::MaybeLocal` (Anna Henningsen) [#5974](https://github.com/nodejs/node/pull/5974)
* \[[`0d0c57ff5e`](https://github.com/nodejs/node/commit/0d0c57ff5e)] - **(SEMVER-MINOR)** **src**: override v8 thread defaults using cli options (Tom Gallacher) [#4344](https://github.com/nodejs/node/pull/4344)
* \[[`f9d0166291`](https://github.com/nodejs/node/commit/f9d0166291)] - **src**: reword command and add ternary (Trevor Norris) [#5756](https://github.com/nodejs/node/pull/5756)
* \[[`f1488bb24c`](https://github.com/nodejs/node/commit/f1488bb24c)] - **src,http\_parser**: remove KickNextTick call (Trevor Norris) [#5756](https://github.com/nodejs/node/pull/5756)
* \[[`c5c7ae8e14`](https://github.com/nodejs/node/commit/c5c7ae8e14)] - **test**: add known\_issues test for GH-2148 (Rich Trott) [#5920](https://github.com/nodejs/node/pull/5920)
* \[[`6113f6af45`](https://github.com/nodejs/node/commit/6113f6af45)] - **test**: mitigate flaky test-https-agent (Rich Trott) [#5939](https://github.com/nodejs/node/pull/5939)
* \[[`0acca7654f`](https://github.com/nodejs/node/commit/0acca7654f)] - **test**: fix flaky test-repl (Brian White) [#5914](https://github.com/nodejs/node/pull/5914)
* \[[`aebe6245b7`](https://github.com/nodejs/node/commit/aebe6245b7)] - **test**: add test for piping large input from stdin (Anna Henningsen) [#5949](https://github.com/nodejs/node/pull/5949)
* \[[`a19de97d2f`](https://github.com/nodejs/node/commit/a19de97d2f)] - **test**: remove the use of curl in the test suite (Santiago Gimeno) [#5750](https://github.com/nodejs/node/pull/5750)
* \[[`6928a17aa3`](https://github.com/nodejs/node/commit/6928a17aa3)] - **test**: exclude new fs watch test for AIX (Michael Dawson) [#5937](https://github.com/nodejs/node/pull/5937)
* \[[`3238bff3b3`](https://github.com/nodejs/node/commit/3238bff3b3)] - **test**: confirm globals not used internally (Rich Trott) [#5882](https://github.com/nodejs/node/pull/5882)
* \[[`a41fd93f68`](https://github.com/nodejs/node/commit/a41fd93f68)] - **test**: fix flaky test-net-socket-timeout (Brian White) [#5902](https://github.com/nodejs/node/pull/5902)
* \[[`82a50d3def`](https://github.com/nodejs/node/commit/82a50d3def)] - **test**: move dns test to test/internet (Ben Noordhuis) [#5905](https://github.com/nodejs/node/pull/5905)
* \[[`fb0c5bcac2`](https://github.com/nodejs/node/commit/fb0c5bcac2)] - **test**: fix flaky test-http-set-timeout (Rich Trott) [#5856](https://github.com/nodejs/node/pull/5856)
* \[[`8344a522a8`](https://github.com/nodejs/node/commit/8344a522a8)] - **test**: fix test-debugger-client.js (Rich Trott) [#5851](https://github.com/nodejs/node/pull/5851)
* \[[`7ec5397954`](https://github.com/nodejs/node/commit/7ec5397954)] - **timers**: fixing API refs to use safe internal refs (Kyle Simpson) [#5882](https://github.com/nodejs/node/pull/5882)
* \[[`cb676cf3e7`](https://github.com/nodejs/node/commit/cb676cf3e7)] - **tools**: fix json doc generation (firedfox) [#5943](https://github.com/nodejs/node/pull/5943)
* \[[`77bed269ad`](https://github.com/nodejs/node/commit/77bed269ad)] - **win,build**: build and test add-ons on test-ci (Bogdan Lobor) [#5886](https://github.com/nodejs/node/pull/5886)
* \[[`afcd276ecc`](https://github.com/nodejs/node/commit/afcd276ecc)] - **zlib**: Fix handling of gzip magic bytes mid-file (Anna Henningsen) [#5863](https://github.com/nodejs/node/pull/5863)

<a id="5.9.1"></a>

## 2016-03-23, Version 5.9.1 (Stable), @Fishrock123

### Notable changes

* **buffer**: Now properly throws `RangeError`s on out-of-bounds writes (Matt Loring) [#5605](https://github.com/nodejs/node/pull/5605).
  * This effects `write{Float|Double}` when the `noAssert` option is not used.
* **timers**:
  * Returned timeout objects now have a `Timeout` constructor name (Jeremiah Senkpiel) [#5793](https://github.com/nodejs/node/pull/5793).
  * Performance of `Immediate` processing is now \~20-40% faster (Brian White) [#4169](https://github.com/nodejs/node/pull/4169).
* **vm**: Fixed a contextify regression introduced in v5.9.0 (Ali Ijaz Sheikh) [#5800](https://github.com/nodejs/node/pull/5800).

### Commits

* \[[`341b3d01c8`](https://github.com/nodejs/node/commit/341b3d01c8)] - **benchmark**: fix linting errors (Rich Trott) [#5840](https://github.com/nodejs/node/pull/5840)
* \[[`72fb796bed`](https://github.com/nodejs/node/commit/72fb796bed)] - **buffer**: throw range error before truncating write (Matt Loring) [#5605](https://github.com/nodejs/node/pull/5605)
* \[[`c5d83695e1`](https://github.com/nodejs/node/commit/c5d83695e1)] - **contextify**: tie lifetimes of context & sandbox (Ali Ijaz Sheikh) [#5800](https://github.com/nodejs/node/pull/5800)
* \[[`ae24d05451`](https://github.com/nodejs/node/commit/ae24d05451)] - **deps**: remove unused openssl files (Ben Noordhuis) [#5619](https://github.com/nodejs/node/pull/5619)
* \[[`54abbe7e6f`](https://github.com/nodejs/node/commit/54abbe7e6f)] - **dns**: use template literals (Benjamin Gruenbaum) [#5809](https://github.com/nodejs/node/pull/5809)
* \[[`3fef69bf15`](https://github.com/nodejs/node/commit/3fef69bf15)] - **dns**: use isIp consistently (Benjamin Gruenbaum) [#5804](https://github.com/nodejs/node/pull/5804)
* \[[`d2d0fe9d34`](https://github.com/nodejs/node/commit/d2d0fe9d34)] - **doc**: update crypto docs to use good defaults (Bill Automata) [#5505](https://github.com/nodejs/node/pull/5505)
* \[[`1631f06477`](https://github.com/nodejs/node/commit/1631f06477)] - **doc**: add CTC meeting minutes 2016-02-10 (Rod Vagg) [#5273](https://github.com/nodejs/node/pull/5273)
* \[[`7ab597d646`](https://github.com/nodejs/node/commit/7ab597d646)] - **doc**: add CTC meeting minutes 2016-02-03 (Rod Vagg) [#5272](https://github.com/nodejs/node/pull/5272)
* \[[`e20d0b8802`](https://github.com/nodejs/node/commit/e20d0b8802)] - **doc**: explain error message on missing main file (Wolfgang Steiner) [#5812](https://github.com/nodejs/node/pull/5812)
* \[[`e99082e32d`](https://github.com/nodejs/node/commit/e99082e32d)] - **doc**: add a cli options doc page (Jeremiah Senkpiel) [#5787](https://github.com/nodejs/node/pull/5787)
* \[[`0ffd794b27`](https://github.com/nodejs/node/commit/0ffd794b27)] - **doc**: Add windows example for Path.format (Mithun Patel) [#5700](https://github.com/nodejs/node/pull/5700)
* \[[`f53cc37578`](https://github.com/nodejs/node/commit/f53cc37578)] - **doc**: grammar, clarity and links in timers doc (Bryan English) [#5792](https://github.com/nodejs/node/pull/5792)
* \[[`3ada8cc09a`](https://github.com/nodejs/node/commit/3ada8cc09a)] - **doc**: align doc/api/tls.markdown with style guide (Stefano Vozza) [#5706](https://github.com/nodejs/node/pull/5706)
* \[[`5d28ce3942`](https://github.com/nodejs/node/commit/5d28ce3942)] - **doc**: topic blocking vs non-blocking (Jarrett Widman) [#5326](https://github.com/nodejs/node/pull/5326)
* \[[`d9b4e15f75`](https://github.com/nodejs/node/commit/d9b4e15f75)] - **doc**: fix typo in synchronous randomBytes example (Andrea Giammarchi) [#5781](https://github.com/nodejs/node/pull/5781)
* \[[`d8318c2226`](https://github.com/nodejs/node/commit/d8318c2226)] - **doc**: fix crypto update() signatures (Brian White) [#5500](https://github.com/nodejs/node/pull/5500)
* \[[`15c5662959`](https://github.com/nodejs/node/commit/15c5662959)] - **doc**: fix multiline return comments in querystring (Claudio Rodriguez) [#5705](https://github.com/nodejs/node/pull/5705)
* \[[`75f723c0aa`](https://github.com/nodejs/node/commit/75f723c0aa)] - **doc**: fix invalid path doc comments (Rich Trott) [#5670](https://github.com/nodejs/node/pull/5670)
* \[[`724b87d75c`](https://github.com/nodejs/node/commit/724b87d75c)] - **doc**: explain path.format() algorithm (Rich Trott) [#5688](https://github.com/nodejs/node/pull/5688)
* \[[`89df17ed0b`](https://github.com/nodejs/node/commit/89df17ed0b)] - **doc**: fix return value of write methods (Felix Böhm) [#5736](https://github.com/nodejs/node/pull/5736)
* \[[`5ab51ee151`](https://github.com/nodejs/node/commit/5ab51ee151)] - **doc**: reformat & improve node.1 manual page (Jeremiah Senkpiel) [#5497](https://github.com/nodejs/node/pull/5497)
* \[[`f34a00cee2`](https://github.com/nodejs/node/commit/f34a00cee2)] - **docs**: fix man pages link if tok type is code (Mithun Patel) [#5721](https://github.com/nodejs/node/pull/5721)
* \[[`3bff3111f4`](https://github.com/nodejs/node/commit/3bff3111f4)] - **https**: fix ssl socket leak when keepalive is used (Alexander Penev) [#5713](https://github.com/nodejs/node/pull/5713)
* \[[`7b21c09b73`](https://github.com/nodejs/node/commit/7b21c09b73)] - **lib**: simplify code with String.prototype.repeat() (Jackson Tian) [#5359](https://github.com/nodejs/node/pull/5359)
* \[[`c75f97f43b`](https://github.com/nodejs/node/commit/c75f97f43b)] - **lib**: reduce usage of `self = this` (Jackson Tian) [#5231](https://github.com/nodejs/node/pull/5231)
* \[[`1ccf9b4a56`](https://github.com/nodejs/node/commit/1ccf9b4a56)] - **net**: remove unused `var self = this` from old code (Benjamin Gruenbaum) [#5224](https://github.com/nodejs/node/pull/5224)
* \[[`6e5835b8cd`](https://github.com/nodejs/node/commit/6e5835b8cd)] - **path**: refactor path.format() repeated code (Rich Trott) [#5673](https://github.com/nodejs/node/pull/5673)
* \[[`15c7b3a127`](https://github.com/nodejs/node/commit/15c7b3a127)] - **src,tools**: use template literals (Rich Trott) [#5778](https://github.com/nodejs/node/pull/5778)
* \[[`ca971b0d77`](https://github.com/nodejs/node/commit/ca971b0d77)] - **test**: smaller chunk size for smaller person.jpg (Jérémy Lal) [#5813](https://github.com/nodejs/node/pull/5813)
* \[[`f95fc175eb`](https://github.com/nodejs/node/commit/f95fc175eb)] - **test**: strip non-free icc profile from person.jpg (Jérémy Lal) [#5813](https://github.com/nodejs/node/pull/5813)
* \[[`7c2c7b0577`](https://github.com/nodejs/node/commit/7c2c7b0577)] - **test**: remove timer from test-http-1.0 (Santiago Gimeno) [#5129](https://github.com/nodejs/node/pull/5129)
* \[[`70512e51a4`](https://github.com/nodejs/node/commit/70512e51a4)] - **test**: repl tab completion test (Santiago Gimeno) [#5534](https://github.com/nodejs/node/pull/5534)
* \[[`89f091d621`](https://github.com/nodejs/node/commit/89f091d621)] - **test**: make test-net-connect-options-ipv6.js better (Michael Dawson) [#5791](https://github.com/nodejs/node/pull/5791)
* \[[`d2fa64490f`](https://github.com/nodejs/node/commit/d2fa64490f)] - **test**: fix `test-cluster-worker-kill` (Santiago Gimeno) [#5814](https://github.com/nodejs/node/pull/5814)
* \[[`f0d885a0a9`](https://github.com/nodejs/node/commit/f0d885a0a9)] - **test**: fix flaky test-cluster-shared-leak (Claudio Rodriguez) [#5802](https://github.com/nodejs/node/pull/5802)
* \[[`b352cc7db4`](https://github.com/nodejs/node/commit/b352cc7db4)] - **test**: minimize test-http-get-pipeline-problem (Rich Trott) [#5728](https://github.com/nodejs/node/pull/5728)
* \[[`21770c3806`](https://github.com/nodejs/node/commit/21770c3806)] - **test**: reduce brittleness of tab complete test (Matt Loring) [#5772](https://github.com/nodejs/node/pull/5772)
* \[[`46f0e02620`](https://github.com/nodejs/node/commit/46f0e02620)] - **timers**: fix lint from 4fe02e2 (Jeremiah Senkpiel) [#5825](https://github.com/nodejs/node/pull/5825)
* \[[`20a68e9eef`](https://github.com/nodejs/node/commit/20a68e9eef)] - **timers**: give Timeouts a constructor name (Jeremiah Senkpiel) [#5793](https://github.com/nodejs/node/pull/5793)
* \[[`d3654d80f3`](https://github.com/nodejs/node/commit/d3654d80f3)] - **timers**: improve setImmediate() performance (Brian White) [#4169](https://github.com/nodejs/node/pull/4169)
* \[[`b1a4870200`](https://github.com/nodejs/node/commit/b1a4870200)] - **tools**: remove unused imports (Sakthipriyan Vairamani) [#5765](https://github.com/nodejs/node/pull/5765)

<a id="5.9.0"></a>

## 2016-03-16, Version 5.9.0 (Stable), @evanlucas

### Notable changes

* **contextify**: Fixed a memory consumption issue related to heavy use of `vm.createContext` and `vm.runInNewContext`. (Ali Ijaz Sheikh)
  <https://github.com/nodejs/node/pull/5392>
* **governance**: The following members have been added as collaborators:
  * Andreas Madsen (@AndreasMadsen)
  * Benjamin Gruenbaum (@benjamingr)
  * Claudio Rodriguez (@claudiorodriguez)
  * Glen Keane (@thekemkid)
  * Jeremy Whitlock (@whitlockjc)
  * Matt Loring (@matthewloring)
  * Phillip Johnsen (@phillipj)
* **lib**: copy arguments object instead of leaking it (Nathan Woltman)
  <https://github.com/nodejs/node/pull/4361>
* **src**: allow both -i and -e flags to be used at the same time (Rich Trott)
  <https://github.com/nodejs/node/pull/5655>
* **timers**: Internal Node.js timeouts now use the same logic path as those created with `setTimeout()` (Jeremiah Senkpiel) [#4007](https://github.com/nodejs/node/pull/4007)
  * This may cause a slightly different performance profile in some situations. So far, it has shown to be positive in most cases.
* **v8**: backport fb4ccae from v8 upstream (Vladimir Krivosheev) #4231
  * breakout events from v8 to offer better support for external debuggers
* **zlib**: add support for concatenated members (Kári Tristan Helgason)
  <https://github.com/nodejs/node/pull/5120>
  * Previously, if multiple members were in the same archive, only the first would be read. The others are no longer thrown away.

### Commits

* \[[`03b99bf8b9`](https://github.com/nodejs/node/commit/03b99bf8b9)] - **build**: don't install github templates (Johan Bergström) [#5612](https://github.com/nodejs/node/pull/5612)
* \[[`a7819da15a`](https://github.com/nodejs/node/commit/a7819da15a)] - _**Revert**_ "**build**: run lint before tests" (Rich Trott) [#5602](https://github.com/nodejs/node/pull/5602)
* \[[`5e9cac4333`](https://github.com/nodejs/node/commit/5e9cac4333)] - **console**: check that stderr is writable (Rich Trott) [#5635](https://github.com/nodejs/node/pull/5635)
* \[[`0662fcf209`](https://github.com/nodejs/node/commit/0662fcf209)] - **contextify**: cache sandbox and context in locals (Ali Ijaz Sheikh) [#5392](https://github.com/nodejs/node/pull/5392)
* \[[`4f2c839d46`](https://github.com/nodejs/node/commit/4f2c839d46)] - **contextify**: replace deprecated SetWeak usage (Ali Ijaz Sheikh) [#5392](https://github.com/nodejs/node/pull/5392)
* \[[`bfff07b4dd`](https://github.com/nodejs/node/commit/bfff07b4dd)] - **contextify**: cleanup weak ref for sandbox (Ali Ijaz Sheikh) [#5392](https://github.com/nodejs/node/pull/5392)
* \[[`93f60cdc54`](https://github.com/nodejs/node/commit/93f60cdc54)] - **contextify**: cleanup weak ref for global proxy (Ali Ijaz Sheikh) [#5392](https://github.com/nodejs/node/pull/5392)
* \[[`b6c355de0d`](https://github.com/nodejs/node/commit/b6c355de0d)] - **(SEMVER-MINOR)** **deps**: backport fb4ccae from v8 upstream (develar) [#4231](https://github.com/nodejs/node/pull/4231)
* \[[`29510aa4fd`](https://github.com/nodejs/node/commit/29510aa4fd)] - **deps**: update openssl config (Shigeki Ohtsu) [#5630](https://github.com/nodejs/node/pull/5630)
* \[[`532d1bf9ce`](https://github.com/nodejs/node/commit/532d1bf9ce)] - **deps**: sync deps/http\_parser with nodejs/http\_parser (James M Snell) [#5600](https://github.com/nodejs/node/pull/5600)
* \[[`d5d64c327b`](https://github.com/nodejs/node/commit/d5d64c327b)] - **doc**: clarify commit message rules (Wyatt Preul) [#5661](https://github.com/nodejs/node/pull/5661)
* \[[`8c4c84fe5b`](https://github.com/nodejs/node/commit/8c4c84fe5b)] - **doc**: add Testing WG (Rich Trott) [#5461](https://github.com/nodejs/node/pull/5461)
* \[[`434af03825`](https://github.com/nodejs/node/commit/434af03825)] - **doc**: Add note about use of JSON.stringify() (Mithun Patel) [#5723](https://github.com/nodejs/node/pull/5723)
* \[[`62926d85bd`](https://github.com/nodejs/node/commit/62926d85bd)] - **doc**: clarify type of first argument in zlib (Kirill Fomichev) [#5685](https://github.com/nodejs/node/pull/5685)
* \[[`eb73574349`](https://github.com/nodejs/node/commit/eb73574349)] - **doc**: clarify when writable.write callback is called (Kevin Locke) [#4810](https://github.com/nodejs/node/pull/4810)
* \[[`c579507034`](https://github.com/nodejs/node/commit/c579507034)] - **doc**: fix typo in api/addons (Daijiro Wachi) [#5678](https://github.com/nodejs/node/pull/5678)
* \[[`8e45c9d9ea`](https://github.com/nodejs/node/commit/8e45c9d9ea)] - **doc**: fix typo in api/dgram (Daijiro Wachi) [#5678](https://github.com/nodejs/node/pull/5678)
* \[[`44a9b100c5`](https://github.com/nodejs/node/commit/44a9b100c5)] - **doc**: fix typo in api/fs (Daijiro Wachi) [#5678](https://github.com/nodejs/node/pull/5678)
* \[[`b667573bcb`](https://github.com/nodejs/node/commit/b667573bcb)] - **doc**: update fansworld-claudio username on README (Claudio Rodriguez) [#5680](https://github.com/nodejs/node/pull/5680)
* \[[`9794abb5d1`](https://github.com/nodejs/node/commit/9794abb5d1)] - **doc**: add onboarding resources (Jeremiah Senkpiel) [#3726](https://github.com/nodejs/node/pull/3726)
* \[[`31e39fbd7a`](https://github.com/nodejs/node/commit/31e39fbd7a)] - **doc**: remove non-standard use of hyphens (Stefano Vozza)
* \[[`f3e9daa825`](https://github.com/nodejs/node/commit/f3e9daa825)] - **doc**: add clarification on birthtime in fs stat (Kári Tristan Helgason) [#5479](https://github.com/nodejs/node/pull/5479)
* \[[`c379ec6522`](https://github.com/nodejs/node/commit/c379ec6522)] - **doc**: move build instructions to a new document (Johan Bergström) [#5634](https://github.com/nodejs/node/pull/5634)
* \[[`2a442b3dfc`](https://github.com/nodejs/node/commit/2a442b3dfc)] - **doc**: update removeListener behaviour (Vaibhav) [#5201](https://github.com/nodejs/node/pull/5201)
* \[[`f6ee0996e0`](https://github.com/nodejs/node/commit/f6ee0996e0)] - **doc**: fix typo in child\_process docs (Benjamin Gruenbaum) [#5681](https://github.com/nodejs/node/pull/5681)
* \[[`dd12661173`](https://github.com/nodejs/node/commit/dd12661173)] - **doc**: include typo in 'unhandledRejection' example (Robert C Jensen) [#5654](https://github.com/nodejs/node/pull/5654)
* \[[`f7aecd6e94`](https://github.com/nodejs/node/commit/f7aecd6e94)] - **doc**: add thekemkid to collaborators (Glen Keane) [#5667](https://github.com/nodejs/node/pull/5667)
* \[[`b81711acfb`](https://github.com/nodejs/node/commit/b81711acfb)] - **doc**: add phillipj to collaborators (Phillip Johnsen) [#5663](https://github.com/nodejs/node/pull/5663)
* \[[`a33f2486f0`](https://github.com/nodejs/node/commit/a33f2486f0)] - **doc**: add fansworld-claudio to collaborators (Claudio Rodriguez) [#5668](https://github.com/nodejs/node/pull/5668)
* \[[`285d5e7ba6`](https://github.com/nodejs/node/commit/285d5e7ba6)] - **doc**: add AndreasMadsen to collaborators (Andreas Madsen) [#5666](https://github.com/nodejs/node/pull/5666)
* \[[`8e1f6706e3`](https://github.com/nodejs/node/commit/8e1f6706e3)] - **doc**: add benjamingr to collaborator list (Benjamin Gruenbaum) [#5664](https://github.com/nodejs/node/pull/5664)
* \[[`f7842cbb24`](https://github.com/nodejs/node/commit/f7842cbb24)] - **doc**: add whitlockjc to collaborators (Jeremy Whitlock) [#5665](https://github.com/nodejs/node/pull/5665)
* \[[`dd6f4ec2e4`](https://github.com/nodejs/node/commit/dd6f4ec2e4)] - **doc**: add mattloring to collaborators (Matt Loring) [#5662](https://github.com/nodejs/node/pull/5662)
* \[[`9ebd559a55`](https://github.com/nodejs/node/commit/9ebd559a55)] - **doc**: fix markdown links (Steve Mao) [#5641](https://github.com/nodejs/node/pull/5641)
* \[[`62d267e1ff`](https://github.com/nodejs/node/commit/62d267e1ff)] - **doc**: fix dns.resolveCname description typo (axvm) [#5622](https://github.com/nodejs/node/pull/5622)
* \[[`9f8e2e2979`](https://github.com/nodejs/node/commit/9f8e2e2979)] - **doc**: update release tweet template (Jeremiah Senkpiel) [#5628](https://github.com/nodejs/node/pull/5628)
* \[[`4d6fe300fe`](https://github.com/nodejs/node/commit/4d6fe300fe)] - **doc**: fix v5.8.0 changelog heading (Jeremiah Senkpiel) [#5559](https://github.com/nodejs/node/pull/5559)
* \[[`4c1fdaeb2a`](https://github.com/nodejs/node/commit/4c1fdaeb2a)] - **docs**: update link to iojs+release ci job (Myles Borins) [#5632](https://github.com/nodejs/node/pull/5632)
* \[[`205bed0bec`](https://github.com/nodejs/node/commit/205bed0bec)] - **lib**: copy arguments object instead of leaking it (Nathan Woltman) [#4361](https://github.com/nodejs/node/pull/4361)
* \[[`b16f67a0b9`](https://github.com/nodejs/node/commit/b16f67a0b9)] - **net**: make `isIPv4` and `isIPv6` more efficient (Vladimir Kurchatkin) [#5478](https://github.com/nodejs/node/pull/5478)
* \[[`4ecd996baa`](https://github.com/nodejs/node/commit/4ecd996baa)] - **(SEMVER-MINOR)** **src**: allow combination of -i and -e cli flags (Rich Trott) [#5655](https://github.com/nodejs/node/pull/5655)
* \[[`f225459496`](https://github.com/nodejs/node/commit/f225459496)] - **test**: improve test-npm-install (Santiago Gimeno) [#5613](https://github.com/nodejs/node/pull/5613)
* \[[`cceae5ae78`](https://github.com/nodejs/node/commit/cceae5ae78)] - **test**: eval a strict function (Kári Tristan Helgason) [#5250](https://github.com/nodejs/node/pull/5250)
* \[[`9a44c8c337`](https://github.com/nodejs/node/commit/9a44c8c337)] - **test**: add batch of known issue tests (cjihrig) [#5653](https://github.com/nodejs/node/pull/5653)
* \[[`1b7b1ed2c9`](https://github.com/nodejs/node/commit/1b7b1ed2c9)] - **timers**: greatly improve code comments (Jeremiah Senkpiel) [#4007](https://github.com/nodejs/node/pull/4007)
* \[[`769254b0ba`](https://github.com/nodejs/node/commit/769254b0ba)] - **timers**: refactor timers (Jeremiah Senkpiel) [#4007](https://github.com/nodejs/node/pull/4007)
* \[[`0b545fb3f8`](https://github.com/nodejs/node/commit/0b545fb3f8)] - **win,build**: support Visual C++ Build Tools 2015 (João Reis) [#5627](https://github.com/nodejs/node/pull/5627)
* \[[`ef774ff9a8`](https://github.com/nodejs/node/commit/ef774ff9a8)] - **(SEMVER-MINOR)** **zlib**: add support for concatenated members (Kári Tristan Helgason) [#5120](https://github.com/nodejs/node/pull/5120)

<a id="5.8.0"></a>

## 2016-03-08, Version 5.8.0 (Stable), @Fishrock123

### Notable changes

* **child\_process**: `send()` now accepts an options parameter (cjihrig) [#5283](https://github.com/nodejs/node/pull/5283).
  * Currently the only option is `keepOpen`, which keeps the underlying socket open after the message is sent.
* **constants**: `ENGINE_METHOD_RSA` is now correctly exposed (Sam Roberts) [#5463](https://github.com/nodejs/node/pull/5463).
* Fixed two regressions which originated in v5.7.0:
  * **http**: Errors inside of http client callbacks now propagate correctly (Trevor Norris) [#5591](https://github.com/nodejs/node/pull/5591).
  * **path**: Fixed normalization of absolute paths (Evan Lucas) [#5589](https://github.com/nodejs/node/pull/5589).
* **repl**: `start()` no longer requires an options parameter (cjihrig) [#5388](https://github.com/nodejs/node/pull/5388).
* **util**: Improved `format()` performance 50-300% (Evan Lucas) [#5360](https://github.com/nodejs/node/pull/5360).

### Commits

* \[[`12ca84fc7f`](https://github.com/nodejs/node/commit/12ca84fc7f)] - **benchmark**: add util.format benchmark (Evan Lucas) [#5360](https://github.com/nodejs/node/pull/5360)
* \[[`b955d02266`](https://github.com/nodejs/node/commit/b955d02266)] - **benchmark**: fix lint errors (Rich Trott) [#5517](https://github.com/nodejs/node/pull/5517)
* \[[`2abf866b6e`](https://github.com/nodejs/node/commit/2abf866b6e)] - **build**: update Node.js logo on Win installer (Robert Jefe Lindstaedt) [#5531](https://github.com/nodejs/node/pull/5531)
* \[[`86900f8f2b`](https://github.com/nodejs/node/commit/86900f8f2b)] - **build**: correctly detect clang version (Stefan Budeanu) [#5553](https://github.com/nodejs/node/pull/5553)
* \[[`a3017992e4`](https://github.com/nodejs/node/commit/a3017992e4)] - **(SEMVER-MINOR)** **child\_process**: add keepOpen option to send() (cjihrig) [#5283](https://github.com/nodejs/node/pull/5283)
* \[[`6d4887ccc2`](https://github.com/nodejs/node/commit/6d4887ccc2)] - **(SEMVER-MINOR)** **child\_process**: support options in send() (cjihrig) [#5283](https://github.com/nodejs/node/pull/5283)
* \[[`9db827c7aa`](https://github.com/nodejs/node/commit/9db827c7aa)] - **(SEMVER-MINOR)** **constants**: define ENGINE\_METHOD\_RSA (Sam Roberts) [#5463](https://github.com/nodejs/node/pull/5463)
* \[[`85013456cd`](https://github.com/nodejs/node/commit/85013456cd)] - **deps**: upgrade to npm 3.7.3 (Kat Marchán) [#5369](https://github.com/nodejs/node/pull/5369)
* \[[`67e9f65958`](https://github.com/nodejs/node/commit/67e9f65958)] - **dgram**: default send address to 127.0.0.1 or ::1 (Matteo Collina) [#5493](https://github.com/nodejs/node/pull/5493)
* \[[`3c92352c8c`](https://github.com/nodejs/node/commit/3c92352c8c)] - **doc**: document directories in test directory (Michael Barrett) [#5557](https://github.com/nodejs/node/pull/5557)
* \[[`7be726f86a`](https://github.com/nodejs/node/commit/7be726f86a)] - **doc**: add info to docs on how to submit docs patch (Sequoia McDowell) [#4591](https://github.com/nodejs/node/pull/4591)
* \[[`eb5a95e04a`](https://github.com/nodejs/node/commit/eb5a95e04a)] - **doc**: fix typo in fs.symlink (Michaël Zasso) [#5560](https://github.com/nodejs/node/pull/5560)
* \[[`9ad901ef44`](https://github.com/nodejs/node/commit/9ad901ef44)] - **doc**: improve unhandledException doc copy (James M Snell) [#5287](https://github.com/nodejs/node/pull/5287)
* \[[`3bd96fdb0f`](https://github.com/nodejs/node/commit/3bd96fdb0f)] - **doc**: update link green to match homepage (silverwind) [#5548](https://github.com/nodejs/node/pull/5548)
* \[[`cb7e4fbac9`](https://github.com/nodejs/node/commit/cb7e4fbac9)] - **doc**: update V8 URL (Craig Akimoto) [#5530](https://github.com/nodejs/node/pull/5530)
* \[[`b54a26fa61`](https://github.com/nodejs/node/commit/b54a26fa61)] - **(SEMVER-MINOR)** **doc**: correct name of engine methods (Sam Roberts) [#5463](https://github.com/nodejs/node/pull/5463)
* \[[`f3971f5817`](https://github.com/nodejs/node/commit/f3971f5817)] - **path**: fix normalize for absolutes (Evan Lucas) [#5589](https://github.com/nodejs/node/pull/5589)
* \[[`e572e421b4`](https://github.com/nodejs/node/commit/e572e421b4)] - **(SEMVER-MINOR)** **repl**: accept no arguments to start() (cjihrig) [#5388](https://github.com/nodejs/node/pull/5388)
* \[[`5e6d706758`](https://github.com/nodejs/node/commit/5e6d706758)] - **src,http**: fix uncaughtException miss in http (Trevor Norris) [#5591](https://github.com/nodejs/node/pull/5591)
* \[[`9dc94d7b09`](https://github.com/nodejs/node/commit/9dc94d7b09)] - **test**: add test-npm-install to parallel tests suite (Myles Borins) [#5166](https://github.com/nodejs/node/pull/5166)
* \[[`4f20f31b3e`](https://github.com/nodejs/node/commit/4f20f31b3e)] - **test**: remove broken debugger scenarios (Rich Trott) [#5532](https://github.com/nodejs/node/pull/5532)
* \[[`29e26b38c5`](https://github.com/nodejs/node/commit/29e26b38c5)] - **test**: bug repro for vm function redefinition (cjihrig) [#5528](https://github.com/nodejs/node/pull/5528)
* \[[`e6210d5f50`](https://github.com/nodejs/node/commit/e6210d5f50)] - **test**: prevent flakey test on pi2 (Trevor Norris) [#5537](https://github.com/nodejs/node/pull/5537)
* \[[`40b36baa2f`](https://github.com/nodejs/node/commit/40b36baa2f)] - **test**: check memoryUsage properties (Wyatt Preul) [#5546](https://github.com/nodejs/node/pull/5546)
* \[[`048c0f4738`](https://github.com/nodejs/node/commit/048c0f4738)] - **tools**: reduce verbosity of cpplint (Sakthipriyan Vairamani) [#5578](https://github.com/nodejs/node/pull/5578)
* \[[`7965c897e0`](https://github.com/nodejs/node/commit/7965c897e0)] - **tools**: enable no-self-assign ESLint rule (Rich Trott) [#5552](https://github.com/nodejs/node/pull/5552)
* \[[`5aa17dc136`](https://github.com/nodejs/node/commit/5aa17dc136)] - **tools**: support testing known issues (cjihrig) [#5528](https://github.com/nodejs/node/pull/5528)
* \[[`9a3e87e9a8`](https://github.com/nodejs/node/commit/9a3e87e9a8)] - **tools**: enable linting for benchmarks (Rich Trott) [#5517](https://github.com/nodejs/node/pull/5517)
* \[[`c4fa2a6715`](https://github.com/nodejs/node/commit/c4fa2a6715)] - **tools**: enable no-extra-parens in ESLint (Rich Trott) [#5512](https://github.com/nodejs/node/pull/5512)
* \[[`971edde0cb`](https://github.com/nodejs/node/commit/971edde0cb)] - **util**: improve format() performance further (Brian White) [#5360](https://github.com/nodejs/node/pull/5360)
* \[[`c32d460747`](https://github.com/nodejs/node/commit/c32d460747)] - **util**: improve util.format performance (Evan Lucas) [#5360](https://github.com/nodejs/node/pull/5360)

<a id="5.7.1"></a>

## 2016-03-02, Version 5.7.1 (Stable), @Fishrock123

### Notable changes

* **governance**: The Core Technical Committee (CTC) added four new members to help guide Node.js core development: Evan Lucas, Rich Trott, Ali Ijaz Sheikh and Сковорода Никита Андреевич (Nikita Skovoroda).
* **openssl**: Upgrade from 1.0.2f to 1.0.2g (Ben Noordhuis) [#5507](https://github.com/nodejs/node/pull/5507).
  * Fix a double-free defect in parsing malformed DSA keys that may potentially be used for DoS or memory corruption attacks. It is likely to be very difficult to use this defect for a practical attack and is therefore considered low severity for Node.js users. More info is available at [CVE-2016-0705](https://www.openssl.org/news/vulnerabilities.html#2016-0705).
  * Fix a defect that can cause memory corruption in certain very rare cases relating to the internal `BN_hex2bn()` and `BN_dec2bn()` functions. It is believed that Node.js is not invoking the code paths that use these functions so practical attacks via Node.js using this defect are _unlikely_ to be possible. More info is available at [CVE-2016-0797](https://www.openssl.org/news/vulnerabilities.html#2016-0797).
  * Fix a defect that makes the _[CacheBleed Attack](https://ssrg.nicta.com.au/projects/TS/cachebleed/)_ possible. This defect enables attackers to execute side-channel attacks leading to the potential recovery of entire RSA private keys. It only affects the Intel Sandy Bridge (and possibly older) microarchitecture when using hyper-threading. Newer microarchitectures, including Haswell, are unaffected. More info is available at [CVE-2016-0702](https://www.openssl.org/news/vulnerabilities.html#2016-0702).
* Fixed several regressions that appeared in v5.7.0:
  * **`path.relative()`**:
    * Output is no longer unnecessarily verbose (Brian White) [#5389](https://github.com/nodejs/node/pull/5389).
    * Resolving UNC paths on Windows now works correctly (Owen Smith) [#5456](https://github.com/nodejs/node/pull/5456).
    * Resolving paths with prefixes now works correctly from the root directory (Owen Smith) [#5490](https://github.com/nodejs/node/pull/5490).
  * **url**: Fixed an off-by-one error with `parse()` (Brian White) [#5394](https://github.com/nodejs/node/pull/5394).
  * **dgram**: Now correctly handles a default address case when offset and length are specified (Matteo Collina) [#5407](https://github.com/nodejs/node/pull/5407).

### Commits

* \[[`7cae774d9b`](https://github.com/nodejs/node/commit/7cae774d9b)] - **benchmark**: refactor to eliminate redeclared vars (Rich Trott) [#5468](https://github.com/nodejs/node/pull/5468)
* \[[`6aebe16669`](https://github.com/nodejs/node/commit/6aebe16669)] - **benchmark**: add benchmark for buf.compare() (Rich Trott) [#5441](https://github.com/nodejs/node/pull/5441)
* \[[`00660f55c8`](https://github.com/nodejs/node/commit/00660f55c8)] - **benchmark**: move string-decoder to its own category (Andreas Madsen) [#5177](https://github.com/nodejs/node/pull/5177)
* \[[`4650cb3818`](https://github.com/nodejs/node/commit/4650cb3818)] - **benchmark**: fix configuation parameters (Andreas Madsen) [#5177](https://github.com/nodejs/node/pull/5177)
* \[[`3ccb275139`](https://github.com/nodejs/node/commit/3ccb275139)] - **benchmark**: merge url.js with url-resolve.js (Andreas Madsen) [#5177](https://github.com/nodejs/node/pull/5177)
* \[[`c1e7dbffaa`](https://github.com/nodejs/node/commit/c1e7dbffaa)] - **benchmark**: move misc to categorized directories (Andreas Madsen) [#5177](https://github.com/nodejs/node/pull/5177)
* \[[`2f9fee6e8e`](https://github.com/nodejs/node/commit/2f9fee6e8e)] - **benchmark**: use strict mode (Rich Trott) [#5336](https://github.com/nodejs/node/pull/5336)
* \[[`4c09e7f359`](https://github.com/nodejs/node/commit/4c09e7f359)] - **build**: remove --quiet from eslint invocation (firedfox) [#5519](https://github.com/nodejs/node/pull/5519)
* \[[`2c619f2012`](https://github.com/nodejs/node/commit/2c619f2012)] - **build**: run lint before tests (Rich Trott) [#5470](https://github.com/nodejs/node/pull/5470)
* \[[`f349a9a2cf`](https://github.com/nodejs/node/commit/f349a9a2cf)] - **build**: update Node.js logo on OSX installer (Rod Vagg) [#5401](https://github.com/nodejs/node/pull/5401)
* \[[`88f393588a`](https://github.com/nodejs/node/commit/88f393588a)] - **crypto**: PBKDF2 works with `int` not `ssize_t` (Fedor Indutny) [#5397](https://github.com/nodejs/node/pull/5397)
* \[[`1e86804503`](https://github.com/nodejs/node/commit/1e86804503)] - **deps**: upgrade openssl to 1.0.2g (Ben Noordhuis) [#5507](https://github.com/nodejs/node/pull/5507)
* \[[`d3f9b84be8`](https://github.com/nodejs/node/commit/d3f9b84be8)] - **dgram**: handle default address case when offset and length are specified (Matteo Collina)
* \[[`f1f3832934`](https://github.com/nodejs/node/commit/f1f3832934)] - **doc**: update NAN urls in ROADMAP.md and doc/releases.md (ronkorving) [#5472](https://github.com/nodejs/node/pull/5472)
* \[[`51bc062dab`](https://github.com/nodejs/node/commit/51bc062dab)] - **doc**: add CTC meeting minutes 2016-02-17 (Rod Vagg) [#5410](https://github.com/nodejs/node/pull/5410)
* \[[`795c85ba1c`](https://github.com/nodejs/node/commit/795c85ba1c)] - **doc**: fix typo in child\_process documentation (Evan Lucas) [#5474](https://github.com/nodejs/node/pull/5474)
* \[[`0a56e9690b`](https://github.com/nodejs/node/commit/0a56e9690b)] - **doc**: add note for binary safe string reading (Anton Andesen) [#5155](https://github.com/nodejs/node/pull/5155)
* \[[`ea8331e15f`](https://github.com/nodejs/node/commit/ea8331e15f)] - **doc**: improvements to crypto.markdown copy (Alexander Makarenko) [#5230](https://github.com/nodejs/node/pull/5230)
* \[[`378a772034`](https://github.com/nodejs/node/commit/378a772034)] - **doc**: `require` behavior on case-insensitive systems (Hugo Wood)
* \[[`06b7eb6636`](https://github.com/nodejs/node/commit/06b7eb6636)] - **doc**: document base64url encoding support (Tristan Slominski) [#5243](https://github.com/nodejs/node/pull/5243)
* \[[`8ec3d904cb`](https://github.com/nodejs/node/commit/8ec3d904cb)] - **doc**: improve httpVersionMajor / httpVersionMajor (Jackson Tian) [#5296](https://github.com/nodejs/node/pull/5296)
* \[[`534e88f56c`](https://github.com/nodejs/node/commit/534e88f56c)] - **doc**: fix relative links in net docs (Evan Lucas) [#5358](https://github.com/nodejs/node/pull/5358)
* \[[`7b98a30976`](https://github.com/nodejs/node/commit/7b98a30976)] - **doc**: fix crypto function indentation level (Brian White) [#5460](https://github.com/nodejs/node/pull/5460)
* \[[`c0fd802cc2`](https://github.com/nodejs/node/commit/c0fd802cc2)] - **doc**: link to man pages (<dcposch@dcpos.ch>) [#5073](https://github.com/nodejs/node/pull/5073)
* \[[`f8c6701e22`](https://github.com/nodejs/node/commit/f8c6701e22)] - **doc**: add missing property in cluster example (Rafael Cepeda) [#5305](https://github.com/nodejs/node/pull/5305)
* \[[`3bfe0483f0`](https://github.com/nodejs/node/commit/3bfe0483f0)] - **doc**: corrected name of argument in socket.send (Chris Dew) [#5449](https://github.com/nodejs/node/pull/5449)
* \[[`c8725f5e95`](https://github.com/nodejs/node/commit/c8725f5e95)] - **doc**: fix links in tls, cluster docs (Alexander Makarenko) [#5364](https://github.com/nodejs/node/pull/5364)
* \[[`7f2cf9af5c`](https://github.com/nodejs/node/commit/7f2cf9af5c)] - **doc**: explicit about VS 2015 support in readme (Phillip Johnsen) [#5406](https://github.com/nodejs/node/pull/5406)
* \[[`12d3cdbfea`](https://github.com/nodejs/node/commit/12d3cdbfea)] - **doc**: remove out-of-date matter from internal docs (Rich Trott) [#5421](https://github.com/nodejs/node/pull/5421)
* \[[`43853679f7`](https://github.com/nodejs/node/commit/43853679f7)] - **doc**: copyedit util doc (Rich Trott) [#5399](https://github.com/nodejs/node/pull/5399)
* \[[`903e8d09e1`](https://github.com/nodejs/node/commit/903e8d09e1)] - **doc**: fix typo in pbkdf2Sync code sample (Marc Cuva) [#5306](https://github.com/nodejs/node/pull/5306)
* \[[`79b1c22c9f`](https://github.com/nodejs/node/commit/79b1c22c9f)] - **doc**: fix buf.readInt16LE output (Chinedu Francis Nwafili) [#5282](https://github.com/nodejs/node/pull/5282)
* \[[`e46915f2f3`](https://github.com/nodejs/node/commit/e46915f2f3)] - **doc**: note util.isError() @@toStringTag limitations (cjihrig) [#5414](https://github.com/nodejs/node/pull/5414)
* \[[`935fd21fff`](https://github.com/nodejs/node/commit/935fd21fff)] - **doc**: clarify error handling in net.createServer (Dirceu Pereira Tiegs) [#5353](https://github.com/nodejs/node/pull/5353)
* \[[`93dce6d4fe`](https://github.com/nodejs/node/commit/93dce6d4fe)] - **doc**: document fs.datasync(Sync) (Ron Korving) [#5402](https://github.com/nodejs/node/pull/5402)
* \[[`96daf51358`](https://github.com/nodejs/node/commit/96daf51358)] - **doc**: add Evan Lucas to the CTC (Rod Vagg) [#5275](https://github.com/nodejs/node/pull/5275)
* \[[`31b405d0cf`](https://github.com/nodejs/node/commit/31b405d0cf)] - **doc**: add Rich Trott to the CTC (Rod Vagg) [#5276](https://github.com/nodejs/node/pull/5276)
* \[[`bcd154e402`](https://github.com/nodejs/node/commit/bcd154e402)] - **doc**: add Ali Ijaz Sheikh to the CTC (Rod Vagg) [#5277](https://github.com/nodejs/node/pull/5277)
* \[[`9d0330c804`](https://github.com/nodejs/node/commit/9d0330c804)] - **doc**: add Сковорода Никита Андреевич to the CTC (Rod Vagg) [#5278](https://github.com/nodejs/node/pull/5278)
* \[[`365cc63783`](https://github.com/nodejs/node/commit/365cc63783)] - **doc**: add "building node with ninja" guide (Jeremiah Senkpiel) [#4767](https://github.com/nodejs/node/pull/4767)
* \[[`2b00c315e1`](https://github.com/nodejs/node/commit/2b00c315e1)] - **doc**: mention prototype check in deepStrictEqual() (cjihrig) [#5367](https://github.com/nodejs/node/pull/5367)
* \[[`ff988b3ee6`](https://github.com/nodejs/node/commit/ff988b3ee6)] - **doc,tools,test**: lint doc-based addon tests (Rich Trott) [#5427](https://github.com/nodejs/node/pull/5427)
* \[[`d77c3bf204`](https://github.com/nodejs/node/commit/d77c3bf204)] - **http\_parser**: use `MakeCallback` (Trevor Norris) [#5419](https://github.com/nodejs/node/pull/5419)
* \[[`e3421ac296`](https://github.com/nodejs/node/commit/e3421ac296)] - **lib**: freelist: use .pop() for allocation (Anton Khlynovskiy) [#2174](https://github.com/nodejs/node/pull/2174)
* \[[`91d218d096`](https://github.com/nodejs/node/commit/91d218d096)] - **path**: fix path.relative() for prefixes at root (Owen Smith) [#5490](https://github.com/nodejs/node/pull/5490)
* \[[`ef7a088906`](https://github.com/nodejs/node/commit/ef7a088906)] - **path**: fix win32 parse() (Zheng Chaoping) [#5484](https://github.com/nodejs/node/pull/5484)
* \[[`871396ce8f`](https://github.com/nodejs/node/commit/871396ce8f)] - **path**: fix win32 relative() for UNC paths (Owen Smith) [#5456](https://github.com/nodejs/node/pull/5456)
* \[[`91782f1888`](https://github.com/nodejs/node/commit/91782f1888)] - **path**: fix win32 relative() when "to" is a prefix (Owen Smith) [#5456](https://github.com/nodejs/node/pull/5456)
* \[[`30cec18eeb`](https://github.com/nodejs/node/commit/30cec18eeb)] - **path**: fix verbose relative() output (Brian White) [#5389](https://github.com/nodejs/node/pull/5389)
* \[[`2b88523836`](https://github.com/nodejs/node/commit/2b88523836)] - **repl**: fix stack trace column number in strict mode (Prince J Wesley) [#5416](https://github.com/nodejs/node/pull/5416)
* \[[`51db48f741`](https://github.com/nodejs/node/commit/51db48f741)] - **src,tools**: remove null sentinel from source array (Ben Noordhuis) [#5418](https://github.com/nodejs/node/pull/5418)
* \[[`03a5daba55`](https://github.com/nodejs/node/commit/03a5daba55)] - **src,tools**: drop nul byte from built-in source code (Ben Noordhuis) [#5418](https://github.com/nodejs/node/pull/5418)
* \[[`17d14f3346`](https://github.com/nodejs/node/commit/17d14f3346)] - **src,tools**: allow utf-8 in built-in js source code (Ben Noordhuis) [#5418](https://github.com/nodejs/node/pull/5418)
* \[[`12ae6abc69`](https://github.com/nodejs/node/commit/12ae6abc69)] - **test**: increase timeout for test-tls-fast-writing (Rich Trott) [#5466](https://github.com/nodejs/node/pull/5466)
* \[[`81348e8855`](https://github.com/nodejs/node/commit/81348e8855)] - **test**: apply Linux workaround to Linux only (Rich Trott) [#5471](https://github.com/nodejs/node/pull/5471)
* \[[`c4d9cdb7d0`](https://github.com/nodejs/node/commit/c4d9cdb7d0)] - **test**: allow options for v8 testing (Michael Dawson) [#5502](https://github.com/nodejs/node/pull/5502)
* \[[`d1a82c6824`](https://github.com/nodejs/node/commit/d1a82c6824)] - **test**: retry on known SmartOS bug (Rich Trott) [#5454](https://github.com/nodejs/node/pull/5454)
* \[[`c7f8a13043`](https://github.com/nodejs/node/commit/c7f8a13043)] - **test**: remove unneeded bind() and related comments (Aayush Naik) [#5023](https://github.com/nodejs/node/pull/5023)
* \[[`cc4cbb10df`](https://github.com/nodejs/node/commit/cc4cbb10df)] - **test**: fix flaky child-process-fork-regr-gh-2847 (Santiago Gimeno) [#5422](https://github.com/nodejs/node/pull/5422)
* \[[`0ebbf6cd53`](https://github.com/nodejs/node/commit/0ebbf6cd53)] - **test**: remove flaky designation from fixed tests (Rich Trott) [#5459](https://github.com/nodejs/node/pull/5459)
* \[[`c83725c604`](https://github.com/nodejs/node/commit/c83725c604)] - **test**: add test-cases for posix path.relative() (Owen Smith) [#5456](https://github.com/nodejs/node/pull/5456)
* \[[`22bb7c9d27`](https://github.com/nodejs/node/commit/22bb7c9d27)] - **test**: fix test runner arg regression (Stefan Budeanu) [#5446](https://github.com/nodejs/node/pull/5446)
* \[[`8c67b94b11`](https://github.com/nodejs/node/commit/8c67b94b11)] - **test**: refactor test-dgram-send-callback-recursive (Santiago Gimeno) [#5079](https://github.com/nodejs/node/pull/5079)
* \[[`2c21d34a2f`](https://github.com/nodejs/node/commit/2c21d34a2f)] - **test**: refactor test-dgram-udp4 (Santiago Gimeno) [#5339](https://github.com/nodejs/node/pull/5339)
* \[[`479a43c876`](https://github.com/nodejs/node/commit/479a43c876)] - **test**: allow passing args to executable (Stefan Budeanu) [#5376](https://github.com/nodejs/node/pull/5376)
* \[[`ff75023812`](https://github.com/nodejs/node/commit/ff75023812)] - **test**: fix test-timers.reliability on OS X (Rich Trott) [#5379](https://github.com/nodejs/node/pull/5379)
* \[[`991f82b4bd`](https://github.com/nodejs/node/commit/991f82b4bd)] - **test**: mitigate flaky test-http-agent (Rich Trott) [#5346](https://github.com/nodejs/node/pull/5346)
* \[[`0f54553a99`](https://github.com/nodejs/node/commit/0f54553a99)] - **test**: increase timeouts on some unref timers tests (Jeremiah Senkpiel) [#5352](https://github.com/nodejs/node/pull/5352)
* \[[`25c01cd779`](https://github.com/nodejs/node/commit/25c01cd779)] - **tls**: fix assert in context.\_external accessor (Ben Noordhuis) [#5521](https://github.com/nodejs/node/pull/5521)
* \[[`5ffd7430d1`](https://github.com/nodejs/node/commit/5ffd7430d1)] - **tools**: apply custom buffer lint rule to /lib only (Rich Trott) [#5371](https://github.com/nodejs/node/pull/5371)
* \[[`fa5d28f246`](https://github.com/nodejs/node/commit/fa5d28f246)] - **tools**: enable additional lint rules (Rich Trott) [#5357](https://github.com/nodejs/node/pull/5357)
* \[[`b44b701e5b`](https://github.com/nodejs/node/commit/b44b701e5b)] - **tools,benchmark**: increase lint compliance (Rich Trott) [#5429](https://github.com/nodejs/node/pull/5429)
* \[[`9424fa5732`](https://github.com/nodejs/node/commit/9424fa5732)] - **url**: group slashed protocols by protocol name (nettofarah) [#5380](https://github.com/nodejs/node/pull/5380)
* \[[`dfe45f13e7`](https://github.com/nodejs/node/commit/dfe45f13e7)] - **url**: fix off-by-one error with parse() (Brian White) [#5394](https://github.com/nodejs/node/pull/5394)

<a id="5.7.0"></a>

## 2016-02-23, Version 5.7.0 (Stable), @rvagg

### Notable changes

* **buffer**:
  * You can now supply an `encoding` argument when filling a Buffer `Buffer#fill(string[, start[, end]][, encoding])`, supplying an existing Buffer will also work with `Buffer#fill(buffer[, start[, end]])`. See the [API documentation](https://nodejs.org/api/buffer.html#buffer_buf_fill_value_offset_end_encoding) for details on how this works. (Trevor Norris) [#4935](https://github.com/nodejs/node/pull/4935)
  * `Buffer#indexOf()` no longer requires a `byteOffset` argument if you also wish to specify an `encoding`: `Buffer#indexOf(val[, byteOffset][, encoding])`. (Trevor Norris) [#4803](https://github.com/nodejs/node/pull/4803)
* **child\_process**: `spawn()` and `spawnSync()` now support a `'shell'` option to allow for optional execution of the given command inside a shell. If set to `true`, `cmd.exe` will be used on Windows and `/bin/sh` elsewhere. A path to a custom shell can also be passed to override these defaults. On Windows, this option allows `.bat.` and `.cmd` files to be executed with `spawn()` and `spawnSync()`. (Colin Ihrig) [#4598](https://github.com/nodejs/node/pull/4598)
* **http\_parser**: Update to http-parser 2.6.2 to fix an unintentionally strict limitation of allowable header characters (James M Snell) [#5237](https://github.com/nodejs/node/pull/5237)
* **dgram**: `socket.send()` now supports accepts an array of Buffers or Strings as the first argument. See the [API docs](https://nodejs.org/download/nightly/v6.0.0-nightly201602102848f84332/docs/api/dgram.html#dgram_socket_send_msg_offset_length_port_address_callback) for details on how this works. (Matteo Collina) [#4374](https://github.com/nodejs/node/pull/4374)
* **http**: Fix a bug where handling headers will mistakenly trigger an `'upgrade'` event where the server is just advertising its protocols. This bug can prevent HTTP clients from communicating with HTTP/2 enabled servers. (Fedor Indutny) [#4337](https://github.com/nodejs/node/pull/4337)
* **net**: Added a `listening` Boolean property to `net` and `http` servers to indicate whether the server is listening for connections. (José Moreira) [#4743](https://github.com/nodejs/node/pull/4743)
* **node**: The C++ `node::MakeCallback()` API is now reentrant and calling it from inside another `MakeCallback()` call no longer causes the `nextTick` queue or Promises microtask queue to be processed out of order. (Trevor Norris) [#4507](https://github.com/nodejs/node/pull/4507)
* **tls**: Add a new `tlsSocket.getProtocol()` method to get the negotiated TLS protocol version of the current connection. (Brian White) [#4995](https://github.com/nodejs/node/pull/4995)
* **vm**: Introduce new `'produceCachedData'` and `'cachedData'` options to `new vm.Script()` to interact with V8's code cache. When a new `vm.Script` object is created with the `'produceCachedData'` set to `true` a `Buffer` with V8's code cache data will be produced and stored in `cachedData` property of the returned object. This data in turn may be supplied back to another `vm.Script()` object with a `'cachedData'` option if the supplied source is the same. Successfully executing a script from cached data can speed up instantiation time. See the [API docs](https://nodejs.org/api/vm.html#vm_new_vm_script_code_options) for details. (Fedor Indutny) [#4777](https://github.com/nodejs/node/pull/4777)
* **performance**: Improvements in:
  * `process.nextTick()` (Ruben Bridgewater) [#5092](https://github.com/nodejs/node/pull/5092)
  * `path` module (Brian White) [#5123](https://github.com/nodejs/node/pull/5123)
  * `querystring` module (Brian White) [#5012](https://github.com/nodejs/node/pull/5012)
  * `streams` module when processing small chunks (Matteo Collina) [#4354](https://github.com/nodejs/node/pull/4354)

### Commits

* \[[`3a96fa0030`](https://github.com/nodejs/node/commit/3a96fa0030)] - **async\_wrap**: add parent uid to init hook (Andreas Madsen) [#4600](https://github.com/nodejs/node/pull/4600)
* \[[`4ef04c7c4c`](https://github.com/nodejs/node/commit/4ef04c7c4c)] - **async\_wrap**: make uid the first argument in init (Andreas Madsen) [#4600](https://github.com/nodejs/node/pull/4600)
* \[[`4afe801f90`](https://github.com/nodejs/node/commit/4afe801f90)] - **async\_wrap**: add uid to all asyncWrap hooks (Andreas Madsen) [#4600](https://github.com/nodejs/node/pull/4600)
* \[[`edf8f8a7da`](https://github.com/nodejs/node/commit/edf8f8a7da)] - **benchmark**: split path benchmarks (Brian White) [#5123](https://github.com/nodejs/node/pull/5123)
* \[[`8d713d8d51`](https://github.com/nodejs/node/commit/8d713d8d51)] - **benchmark**: allow empty parameters (Brian White) [#5123](https://github.com/nodejs/node/pull/5123)
* \[[`eb6d07327a`](https://github.com/nodejs/node/commit/eb6d07327a)] - **(SEMVER-MINOR)** **buffer**: add encoding parameter to fill() (Trevor Norris) [#4935](https://github.com/nodejs/node/pull/4935)
* \[[`60d2048b6c`](https://github.com/nodejs/node/commit/60d2048b6c)] - **(SEMVER-MINOR)** **buffer**: properly retrieve binary length of needle (Trevor Norris) [#4803](https://github.com/nodejs/node/pull/4803)
* \[[`4c67d74607`](https://github.com/nodejs/node/commit/4c67d74607)] - **(SEMVER-MINOR)** **buffer**: allow encoding param to collapse (Trevor Norris) [#4803](https://github.com/nodejs/node/pull/4803)
* \[[`5fa4117bfc`](https://github.com/nodejs/node/commit/5fa4117bfc)] - **build**: add a help message and removed a TODO. (Ojas Shirekar) [#5080](https://github.com/nodejs/node/pull/5080)
* \[[`09bfb865af`](https://github.com/nodejs/node/commit/09bfb865af)] - **build**: remove redundant TODO in configure (Ojas Shirekar) [#5080](https://github.com/nodejs/node/pull/5080)
* \[[`3dfc11c516`](https://github.com/nodejs/node/commit/3dfc11c516)] - **build**: remove Makefile.build (Ojas Shirekar) [#5080](https://github.com/nodejs/node/pull/5080)
* \[[`fc78d3d6a7`](https://github.com/nodejs/node/commit/fc78d3d6a7)] - **build**: skip msi build if WiX is not found (Tsarevich Dmitry) [#5220](https://github.com/nodejs/node/pull/5220)
* \[[`356acb39d7`](https://github.com/nodejs/node/commit/356acb39d7)] - **build**: treat aarch64 as arm64 (Johan Bergström) [#5191](https://github.com/nodejs/node/pull/5191)
* \[[`3b83d42b4a`](https://github.com/nodejs/node/commit/3b83d42b4a)] - **build**: fix build when python path contains spaces (Felix Becker) [#4841](https://github.com/nodejs/node/pull/4841)
* \[[`9e6ad2d8ff`](https://github.com/nodejs/node/commit/9e6ad2d8ff)] - **child\_process**: fix data loss with readable event (Brian White) [#5036](https://github.com/nodejs/node/pull/5036)
* \[[`ecc797600f`](https://github.com/nodejs/node/commit/ecc797600f)] - **(SEMVER-MINOR)** **child\_process**: add shell option to spawn() (cjihrig) [#4598](https://github.com/nodejs/node/pull/4598)
* \[[`efd6f68dce`](https://github.com/nodejs/node/commit/efd6f68dce)] - **cluster**: dont rely on `this` in `fork` (Igor Klopov) [#5216](https://github.com/nodejs/node/pull/5216)
* \[[`df93d60caf`](https://github.com/nodejs/node/commit/df93d60caf)] - **console**: apply null as `this` for util.format (Jackson Tian) [#5222](https://github.com/nodejs/node/pull/5222)
* \[[`c397ba8fa3`](https://github.com/nodejs/node/commit/c397ba8fa3)] - **contextify**: use offset/length from Uint8Array (Fedor Indutny) [#4947](https://github.com/nodejs/node/pull/4947)
* \[[`3048ac0b57`](https://github.com/nodejs/node/commit/3048ac0b57)] - **crypto**: have fixed NodeBIOs return EOF (Adam Langley) [#5105](https://github.com/nodejs/node/pull/5105)
* \[[`af074846f5`](https://github.com/nodejs/node/commit/af074846f5)] - **debugger**: remove unneeded callback check (Rich Trott) [#5319](https://github.com/nodejs/node/pull/5319)
* \[[`7bac743f36`](https://github.com/nodejs/node/commit/7bac743f36)] - **debugger**: assert test before accessing this.binding (Prince J Wesley) [#5145](https://github.com/nodejs/node/pull/5145)
* \[[`18c94e5a8d`](https://github.com/nodejs/node/commit/18c94e5a8d)] - **deps**: remove unnecessary files (Brian White) [#5212](https://github.com/nodejs/node/pull/5212)
* \[[`967cf97bf0`](https://github.com/nodejs/node/commit/967cf97bf0)] - **deps**: cherry-pick 2e4da65 from v8's 4.8 upstream (Michael Dawson) [#5293](https://github.com/nodejs/node/pull/5293)
* \[[`bbdf2684d5`](https://github.com/nodejs/node/commit/bbdf2684d5)] - **deps**: update to http-parser 2.6.2 (James M Snell) [#5237](https://github.com/nodejs/node/pull/5237)
* \[[`127dd6275a`](https://github.com/nodejs/node/commit/127dd6275a)] - _**Revert**_ "**deps**: sync with upstream c-ares/c-ares\@4ef6817" (Ben Noordhuis) [#5199](https://github.com/nodejs/node/pull/5199)
* \[[`35c3832994`](https://github.com/nodejs/node/commit/35c3832994)] - **deps**: sync with upstream c-ares/c-ares\@4ef6817 (Fedor Indutny) [#5199](https://github.com/nodejs/node/pull/5199)
* \[[`b4db31822f`](https://github.com/nodejs/node/commit/b4db31822f)] - **dgram**: scope redeclared variables (Rich Trott) [#4940](https://github.com/nodejs/node/pull/4940)
* \[[`368c1d1098`](https://github.com/nodejs/node/commit/368c1d1098)] - **(SEMVER-MINOR)** **dgram**: support dgram.send with multiple buffers (Matteo Collina) [#4374](https://github.com/nodejs/node/pull/4374)
* \[[`a8862f59eb`](https://github.com/nodejs/node/commit/a8862f59eb)] - **doc**: update repo docs to use 'CTC' (Alexis Campailla) [#5304](https://github.com/nodejs/node/pull/5304)
* \[[`6cf8ec5bd1`](https://github.com/nodejs/node/commit/6cf8ec5bd1)] - **doc**: s/http/https in Myles Borins' GitHub link (Rod Vagg) [#5356](https://github.com/nodejs/node/pull/5356)
* \[[`0389e3803c`](https://github.com/nodejs/node/commit/0389e3803c)] - **doc**: clarify child\_process.execFile{,Sync} file arg (Kevin Locke) [#5310](https://github.com/nodejs/node/pull/5310)
* \[[`c48290d9b7`](https://github.com/nodejs/node/commit/c48290d9b7)] - **doc**: fix buf.length slice example (Chinedu Francis Nwafili) [#5259](https://github.com/nodejs/node/pull/5259)
* \[[`a6e437c619`](https://github.com/nodejs/node/commit/a6e437c619)] - **doc**: fix buffer\[index] example (Chinedu Francis Nwafili) [#5253](https://github.com/nodejs/node/pull/5253)
* \[[`73ef1bd423`](https://github.com/nodejs/node/commit/73ef1bd423)] - **doc**: fix template string (Rafael Cepeda) [#5240](https://github.com/nodejs/node/pull/5240)
* \[[`fa04daa384`](https://github.com/nodejs/node/commit/fa04daa384)] - **doc**: clarify exceptions during uncaughtException (Noah Rose) [#5180](https://github.com/nodejs/node/pull/5180)
* \[[`22f132e61d`](https://github.com/nodejs/node/commit/22f132e61d)] - **doc**: improvements to console.markdown copy (Alexander Makarenko) [#5225](https://github.com/nodejs/node/pull/5225)
* \[[`48fa6f6063`](https://github.com/nodejs/node/commit/48fa6f6063)] - **doc**: update process.send() signature (cjihrig) [#5284](https://github.com/nodejs/node/pull/5284)
* \[[`35d89d4662`](https://github.com/nodejs/node/commit/35d89d4662)] - **doc**: fix net.createConnection() example (Brian White) [#5219](https://github.com/nodejs/node/pull/5219)
* \[[`149007c9f0`](https://github.com/nodejs/node/commit/149007c9f0)] - **doc**: replace node-forward link in CONTRIBUTING.md (Ben Noordhuis) [#5227](https://github.com/nodejs/node/pull/5227)
* \[[`a6aaf2caab`](https://github.com/nodejs/node/commit/a6aaf2caab)] - **doc**: improve scrolling, various CSS tweaks (Roman Reiss) [#5198](https://github.com/nodejs/node/pull/5198)
* \[[`18b00deeac`](https://github.com/nodejs/node/commit/18b00deeac)] - **doc**: update DCO to v1.1 (Mikeal Rogers) [#5170](https://github.com/nodejs/node/pull/5170)
* \[[`3955bc4cd0`](https://github.com/nodejs/node/commit/3955bc4cd0)] - **doc**: fix minor inconsistencies in repl doc (Rich Trott) [#5193](https://github.com/nodejs/node/pull/5193)
* \[[`287bce7b48`](https://github.com/nodejs/node/commit/287bce7b48)] - **doc**: merging behavior of writeHead vs setHeader (Alejandro Oviedo) [#5081](https://github.com/nodejs/node/pull/5081)
* \[[`529e749d88`](https://github.com/nodejs/node/commit/529e749d88)] - **doc**: fix type references for link gen, link css (Claudio Rodriguez) [#4741](https://github.com/nodejs/node/pull/4741)
* \[[`275f6dbcbb`](https://github.com/nodejs/node/commit/275f6dbcbb)] - **(SEMVER-MINOR)** **doc**: correct tlsSocket.getCipher() description (Brian White) [#4995](https://github.com/nodejs/node/pull/4995)
* \[[`b706b0c2c5`](https://github.com/nodejs/node/commit/b706b0c2c5)] - **http**: remove old, confusing comment (Brian White) [#5233](https://github.com/nodejs/node/pull/5233)
* \[[`ed36235248`](https://github.com/nodejs/node/commit/ed36235248)] - **http**: remove unnecessary check (Brian White) [#5233](https://github.com/nodejs/node/pull/5233)
* \[[`7e82a566b3`](https://github.com/nodejs/node/commit/7e82a566b3)] - **(SEMVER-MINOR)** **http**: allow async createConnection() (Brian White) [#4638](https://github.com/nodejs/node/pull/4638)
* \[[`411d813323`](https://github.com/nodejs/node/commit/411d813323)] - **http**: do not emit `upgrade` on advertisement (Fedor Indutny) [#4337](https://github.com/nodejs/node/pull/4337)
* \[[`bbc786b50f`](https://github.com/nodejs/node/commit/bbc786b50f)] - **http,util**: fix typos in comments (Alexander Makarenko) [#5279](https://github.com/nodejs/node/pull/5279)
* \[[`a2d198c702`](https://github.com/nodejs/node/commit/a2d198c702)] - **net**: use `_server` for internal book-keeping (Fedor Indutny) [#5262](https://github.com/nodejs/node/pull/5262)
* \[[`18d24e60c5`](https://github.com/nodejs/node/commit/18d24e60c5)] - **(SEMVER-MINOR)** **net**: add net.listening boolean property over a getter (José Moreira) [#4743](https://github.com/nodejs/node/pull/4743)
* \[[`9cee86e3e9`](https://github.com/nodejs/node/commit/9cee86e3e9)] - **node**: set process.\_eventsCount to 0 on startup (Evan Lucas) [#5208](https://github.com/nodejs/node/pull/5208)
* \[[`f2e4f621c5`](https://github.com/nodejs/node/commit/f2e4f621c5)] - **node**: improve process.nextTick performance (Ruben Bridgewater) [#5092](https://github.com/nodejs/node/pull/5092)
* \[[`1c6f927bd1`](https://github.com/nodejs/node/commit/1c6f927bd1)] - **path**: fix input type checking regression (Brian White) [#5244](https://github.com/nodejs/node/pull/5244)
* \[[`4dae8caf7a`](https://github.com/nodejs/node/commit/4dae8caf7a)] - **path**: performance improvements on all platforms (Brian White) [#5123](https://github.com/nodejs/node/pull/5123)
* \[[`46be1f4d0c`](https://github.com/nodejs/node/commit/46be1f4d0c)] - **querystring**: improve escape() performance (Brian White) [#5012](https://github.com/nodejs/node/pull/5012)
* \[[`27e323e8c1`](https://github.com/nodejs/node/commit/27e323e8c1)] - **querystring**: improve unescapeBuffer() performance (Brian White) [#5012](https://github.com/nodejs/node/pull/5012)
* \[[`301023b2b4`](https://github.com/nodejs/node/commit/301023b2b4)] - **querystring**: improve parse() performance (Brian White) [#5012](https://github.com/nodejs/node/pull/5012)
* \[[`98907c716b`](https://github.com/nodejs/node/commit/98907c716b)] - **(SEMVER-MINOR)** **repl**: allow multiline function call (Zirak) [#3823](https://github.com/nodejs/node/pull/3823)
* \[[`c551da8cb4`](https://github.com/nodejs/node/commit/c551da8cb4)] - **repl**: handle quotes within regexp literal (Prince J Wesley) [#5117](https://github.com/nodejs/node/pull/5117)
* \[[`15091ccca2`](https://github.com/nodejs/node/commit/15091ccca2)] - **src**: remove unnecessary check (Brian White) [#5233](https://github.com/nodejs/node/pull/5233)
* \[[`830bb04d90`](https://github.com/nodejs/node/commit/830bb04d90)] - **src**: remove TryCatch in MakeCallback (Trevor Norris) [#4507](https://github.com/nodejs/node/pull/4507)
* \[[`7f22c8c8a6`](https://github.com/nodejs/node/commit/7f22c8c8a6)] - **src**: remove unused TickInfo::in\_tick() (Trevor Norris) [#4507](https://github.com/nodejs/node/pull/4507)
* \[[`406eb1f516`](https://github.com/nodejs/node/commit/406eb1f516)] - **src**: remove unused of TickInfo::last\_threw() (Trevor Norris) [#4507](https://github.com/nodejs/node/pull/4507)
* \[[`bcec2fecbd`](https://github.com/nodejs/node/commit/bcec2fecbd)] - **src**: add AsyncCallbackScope (Trevor Norris) [#4507](https://github.com/nodejs/node/pull/4507)
* \[[`2cb1594279`](https://github.com/nodejs/node/commit/2cb1594279)] - **src**: fix MakeCallback error handling (Trevor Norris) [#4507](https://github.com/nodejs/node/pull/4507)
* \[[`8d6e679a90`](https://github.com/nodejs/node/commit/8d6e679a90)] - **src,test,tools**: modify for more stringent linting (Rich Trott) [#5214](https://github.com/nodejs/node/pull/5214)
* \[[`7684b0fcdf`](https://github.com/nodejs/node/commit/7684b0fcdf)] - **stream**: fix no data on partial decode (Brian White) [#5226](https://github.com/nodejs/node/pull/5226)
* \[[`f706cb0189`](https://github.com/nodejs/node/commit/f706cb0189)] - **streams**: 5% throughput gain when sending small chunks (Matteo Collina) [#4354](https://github.com/nodejs/node/pull/4354)
* \[[`25513a473a`](https://github.com/nodejs/node/commit/25513a473a)] - **string\_decoder**: fix performance regression (Brian White) [#5134](https://github.com/nodejs/node/pull/5134)
* \[[`0e85530d8c`](https://github.com/nodejs/node/commit/0e85530d8c)] - **test**: use String.prototype.repeat() for clarity (Rich Trott) [#5311](https://github.com/nodejs/node/pull/5311)
* \[[`5683efb90a`](https://github.com/nodejs/node/commit/5683efb90a)] - **test**: remove flaky mark for test-debug-no-context (Rich Trott) [#5317](https://github.com/nodejs/node/pull/5317)
* \[[`c55bb79ace`](https://github.com/nodejs/node/commit/c55bb79ace)] - **test**: add test for https server close event (Braydon Fuller) [#5106](https://github.com/nodejs/node/pull/5106)
* \[[`138ee983b0`](https://github.com/nodejs/node/commit/138ee983b0)] - **test**: refactor test-http-destroyed-socket-write2 (Santiago Gimeno) [#4970](https://github.com/nodejs/node/pull/4970)
* \[[`df7d91f36b`](https://github.com/nodejs/node/commit/df7d91f36b)] - **test**: mitigate flaky test-debug-no-context (Rich Trott) [#5269](https://github.com/nodejs/node/pull/5269)
* \[[`d9177e7c26`](https://github.com/nodejs/node/commit/d9177e7c26)] - **test**: test-process-getactivehandles is flaky (Alexis Campailla) [#5303](https://github.com/nodejs/node/pull/5303)
* \[[`f5cc04732f`](https://github.com/nodejs/node/commit/f5cc04732f)] - **test**: mark test-http-regr-gh-2928 flaky (Rich Trott) [#5280](https://github.com/nodejs/node/pull/5280)
* \[[`78b349d5d1`](https://github.com/nodejs/node/commit/78b349d5d1)] - **test**: disable fs watch tests for AIX (Michael Dawson) [#5187](https://github.com/nodejs/node/pull/5187)
* \[[`82ee5e94df`](https://github.com/nodejs/node/commit/82ee5e94df)] - **test**: mark test-http-agent flaky (Rich Trott) [#5209](https://github.com/nodejs/node/pull/5209)
* \[[`1494d6f213`](https://github.com/nodejs/node/commit/1494d6f213)] - **test**: minimal repl eval option test (Rich Trott) [#5192](https://github.com/nodejs/node/pull/5192)
* \[[`e7bf951136`](https://github.com/nodejs/node/commit/e7bf951136)] - **test**: add addons test for MakeCallback (Trevor Norris) [#4507](https://github.com/nodejs/node/pull/4507)
* \[[`98596a94fa`](https://github.com/nodejs/node/commit/98596a94fa)] - **(SEMVER-MINOR)** **test**: run v8 tests from node tree (Bryon Leung) [#4704](https://github.com/nodejs/node/pull/4704)
* \[[`69c544f245`](https://github.com/nodejs/node/commit/69c544f245)] - **test**: fix flaky test-http-regr-gh-2928 (Rich Trott) [#5154](https://github.com/nodejs/node/pull/5154)
* \[[`7c88410507`](https://github.com/nodejs/node/commit/7c88410507)] - **test**: fix child-process-fork-regr-gh-2847 again (Santiago Gimeno) [#5179](https://github.com/nodejs/node/pull/5179)
* \[[`2c2cb6700d`](https://github.com/nodejs/node/commit/2c2cb6700d)] - **test**: remove unneeded common.indirectInstanceOf() (Rich Trott) [#5149](https://github.com/nodejs/node/pull/5149)
* \[[`6340974f21`](https://github.com/nodejs/node/commit/6340974f21)] - **test**: don't run test-tick-processor.js on Aix (Michael Dawson) [#5093](https://github.com/nodejs/node/pull/5093)
* \[[`a8f4db236c`](https://github.com/nodejs/node/commit/a8f4db236c)] - **test**: improve path tests (Brian White) [#5123](https://github.com/nodejs/node/pull/5123)
* \[[`8301773c1e`](https://github.com/nodejs/node/commit/8301773c1e)] - **test**: fix child-process-fork-regr-gh-2847 (Santiago Gimeno) [#5121](https://github.com/nodejs/node/pull/5121)
* \[[`f2bd86775b`](https://github.com/nodejs/node/commit/f2bd86775b)] - **test**: update arrow function style (cjihrig) [#4813](https://github.com/nodejs/node/pull/4813)
* \[[`aed04b85c2`](https://github.com/nodejs/node/commit/aed04b85c2)] - **tls**: nullify `.ssl` on handle close (Fedor Indutny) [#5168](https://github.com/nodejs/node/pull/5168)
* \[[`c3f8aab652`](https://github.com/nodejs/node/commit/c3f8aab652)] - **(SEMVER-MINOR)** **tls**: add getProtocol() to TLS sockets (Brian White) [#4995](https://github.com/nodejs/node/pull/4995)
* \[[`7fc2e3161f`](https://github.com/nodejs/node/commit/7fc2e3161f)] - **tools**: add Node.js-specific ESLint rules (Rich Trott) [#5320](https://github.com/nodejs/node/pull/5320)
* \[[`983325cb0c`](https://github.com/nodejs/node/commit/983325cb0c)] - **tools**: replace obsolete ESLint rules (Rich Trott) [#5214](https://github.com/nodejs/node/pull/5214)
* \[[`f601d040b5`](https://github.com/nodejs/node/commit/f601d040b5)] - **tools**: update ESLint to version 2.1.0 (Rich Trott) [#5214](https://github.com/nodejs/node/pull/5214)
* \[[`13af565759`](https://github.com/nodejs/node/commit/13af565759)] - **tools**: remove obsolete lint rules (Rich Trott) [#5214](https://github.com/nodejs/node/pull/5214)
* \[[`c566f44f1b`](https://github.com/nodejs/node/commit/c566f44f1b)] - **tools**: add recommended ES6 lint rules (Rich Trott) [#5210](https://github.com/nodejs/node/pull/5210)
* \[[`b611caa0ba`](https://github.com/nodejs/node/commit/b611caa0ba)] - **tools**: add recommended linting rules (Rich Trott) [#5188](https://github.com/nodejs/node/pull/5188)
* \[[`b1a16d1202`](https://github.com/nodejs/node/commit/b1a16d1202)] - **tools**: remove excessive comments from .eslintrc (Rich Trott) [#5151](https://github.com/nodejs/node/pull/5151)
* \[[`c4ed5ece4d`](https://github.com/nodejs/node/commit/c4ed5ece4d)] - **tools**: enable no-proto rule for linter (Jackson Tian) [#5140](https://github.com/nodejs/node/pull/5140)
* \[[`86f8477b56`](https://github.com/nodejs/node/commit/86f8477b56)] - **tools**: disallow mixed spaces and tabs for indents (Rich Trott) [#5135](https://github.com/nodejs/node/pull/5135)
* \[[`21fd1496a9`](https://github.com/nodejs/node/commit/21fd1496a9)] - **tools**: alphabetize eslint stylistic issues section (Rich Trott)
* \[[`22c8d50a1f`](https://github.com/nodejs/node/commit/22c8d50a1f)] - **tools**: parse types into links in doc html gen (Claudio Rodriguez) [#4741](https://github.com/nodejs/node/pull/4741)
* \[[`5c54d4987d`](https://github.com/nodejs/node/commit/5c54d4987d)] - **tools**: enable no-redeclare rule for linter (Rich Trott) [#5047](https://github.com/nodejs/node/pull/5047)
* \[[`a3a0cf603a`](https://github.com/nodejs/node/commit/a3a0cf603a)] - **tools**: add arrow function rules to eslint (cjihrig) [#4813](https://github.com/nodejs/node/pull/4813)
* \[[`bcc26f747f`](https://github.com/nodejs/node/commit/bcc26f747f)] - **tools,doc**: fix linting errors (Rich Trott) [#5161](https://github.com/nodejs/node/pull/5161)
* \[[`47274704aa`](https://github.com/nodejs/node/commit/47274704aa)] - **url**: fix lint and deopt issues (Brian White) [#5300](https://github.com/nodejs/node/pull/5300)
* \[[`729ad75860`](https://github.com/nodejs/node/commit/729ad75860)] - **url**: improve url.parse() performance (Brian White) [#4892](https://github.com/nodejs/node/pull/4892)
* \[[`6c8378b15b`](https://github.com/nodejs/node/commit/6c8378b15b)] - **vm**: fix `produceCachedData` (Jiho Choi) [#5343](https://github.com/nodejs/node/pull/5343)
* \[[`d1cacb814f`](https://github.com/nodejs/node/commit/d1cacb814f)] - **(SEMVER-MINOR)** **vm**: introduce `cachedData`/`produceCachedData` (Fedor Indutny) [#4777](https://github.com/nodejs/node/pull/4777)

<a id="5.6.0"></a>

## 2016-02-09, Version 5.6.0 (Stable), @jasnell

This is an important security release. All Node.js users should consult the security release summary at nodejs.org for details on patched vulnerabilities.

### Notable changes

* **http**: fix defects in HTTP header parsing for requests and responses that can allow request smuggling (CVE-2016-2086) or response splitting (CVE-2016-2216). HTTP header parsing now aligns more closely with the HTTP spec including restricting the acceptable characters.
* **http-parser**: upgrade from 2.6.0 to 2.6.1
* **npm**: upgrade npm from 3.3.12 to 3.6.0 (Rebecca Turner) [#4958](https://github.com/nodejs/node/pull/4958)
* **openssl**: upgrade from 1.0.2e to 1.0.2f. To mitigate against the Logjam attack, TLS clients now reject Diffie-Hellman handshakes with parameters shorter than 1024-bits, up from the previous limit of 768-bits.

### Commits

* \[[`3b6283c163`](https://github.com/nodejs/node/commit/3b6283c163)] - **benchmark**: add a constant declaration for `net` (Minwoo Jung) [#3950](https://github.com/nodejs/node/pull/3950)
* \[[`3175f7450e`](https://github.com/nodejs/node/commit/3175f7450e)] - **buffer**: remove duplicated code in fromObject (HUANG Wei) [#4948](https://github.com/nodejs/node/pull/4948)
* \[[`58d67e26a2`](https://github.com/nodejs/node/commit/58d67e26a2)] - **buffer**: validate list elements in Buffer.concat (Michaël Zasso) [#4951](https://github.com/nodejs/node/pull/4951)
* \[[`bafc86f00e`](https://github.com/nodejs/node/commit/bafc86f00e)] - **buffer**: refactor redeclared variables (Rich Trott) [#4886](https://github.com/nodejs/node/pull/4886)
* \[[`0fa4d90b94`](https://github.com/nodejs/node/commit/0fa4d90b94)] - **build**: Add VARIATION variable to binary target (Stefan Budeanu) [#4631](https://github.com/nodejs/node/pull/4631)
* \[[`ec62789152`](https://github.com/nodejs/node/commit/ec62789152)] - **crypto**: fix memory leak in LoadPKCS12 (Fedor Indutny) [#5109](https://github.com/nodejs/node/pull/5109)
* \[[`d9e934c71f`](https://github.com/nodejs/node/commit/d9e934c71f)] - **crypto**: add `pfx` certs as CA certs too (Fedor Indutny) [#5109](https://github.com/nodejs/node/pull/5109)
* \[[`0d4b538175`](https://github.com/nodejs/node/commit/0d4b538175)] - **crypto**: use SSL\_CTX\_clear\_extra\_chain\_certs. (Adam Langley) [#4919](https://github.com/nodejs/node/pull/4919)
* \[[`abb0f6cd53`](https://github.com/nodejs/node/commit/abb0f6cd53)] - **crypto**: fix build when OCSP-stapling not provided (Adam Langley) [#4914](https://github.com/nodejs/node/pull/4914)
* \[[`755619c554`](https://github.com/nodejs/node/commit/755619c554)] - **crypto**: use a const SSL\_CIPHER (Adam Langley) [#4913](https://github.com/nodejs/node/pull/4913)
* \[[`4f4c8ab3b4`](https://github.com/nodejs/node/commit/4f4c8ab3b4)] - **(SEMVER-MINOR)** **deps**: update http-parser to version 2.6.1 (James M Snell)
* \[[`f0bd176d6d`](https://github.com/nodejs/node/commit/f0bd176d6d)] - **deps**: reapply c-ares floating patch (Ben Noordhuis) [#5090](https://github.com/nodejs/node/pull/5090)
* \[[`f1a0827417`](https://github.com/nodejs/node/commit/f1a0827417)] - **deps**: sync with upstream bagder/c-ares\@2bae2d5 (Fedor Indutny) [#5090](https://github.com/nodejs/node/pull/5090)
* \[[`cbf36de8f1`](https://github.com/nodejs/node/commit/cbf36de8f1)] - **deps**: upgrade npm to 3.6.0 (Rebecca Turner) [#4958](https://github.com/nodejs/node/pull/4958)
* \[[`dd97d07a0d`](https://github.com/nodejs/node/commit/dd97d07a0d)] - **deps**: backport 8d00c2c from v8 upstream (Gibson Fahnestock) [#5024](https://github.com/nodejs/node/pull/5024)
* \[[`b75263094b`](https://github.com/nodejs/node/commit/b75263094b)] - **deps**: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) [#1836](https://github.com/nodejs/node/pull/1836)
* \[[`b312b7914f`](https://github.com/nodejs/node/commit/b312b7914f)] - **deps**: upgrade openssl sources to 1.0.2f (Myles Borins) [#4961](https://github.com/nodejs/node/pull/4961)
* \[[`fa0457ed04`](https://github.com/nodejs/node/commit/fa0457ed04)] - **dns**: throw a TypeError in lookupService with invalid port (Evan Lucas) [#4839](https://github.com/nodejs/node/pull/4839)
* \[[`c4c8b3bf2e`](https://github.com/nodejs/node/commit/c4c8b3bf2e)] - **doc**: fix dgram doc indentation (Rich Trott) [#5118](https://github.com/nodejs/node/pull/5118)
* \[[`027cd2719f`](https://github.com/nodejs/node/commit/027cd2719f)] - **doc**: clarify code of conduct reporting (Julie Pagano) [#5107](https://github.com/nodejs/node/pull/5107)
* \[[`9f7aa6f868`](https://github.com/nodejs/node/commit/9f7aa6f868)] - **doc**: clarify dgram socket.send() multi-buffer support (Matteo Collina) [#5130](https://github.com/nodejs/node/pull/5130)
* \[[`a96ae2cb37`](https://github.com/nodejs/node/commit/a96ae2cb37)] - **doc**: console is asynchronous unless it's a file (Ben Noordhuis) [#5133](https://github.com/nodejs/node/pull/5133)
* \[[`4c54c8f309`](https://github.com/nodejs/node/commit/4c54c8f309)] - **doc**: fix typo in dgram doc (Rich Trott) [#5114](https://github.com/nodejs/node/pull/5114)
* \[[`9c93ea3d51`](https://github.com/nodejs/node/commit/9c93ea3d51)] - **doc**: fix links order in Buffer doc (Alexander Makarenko) [#5076](https://github.com/nodejs/node/pull/5076)
* \[[`a0ba378880`](https://github.com/nodejs/node/commit/a0ba378880)] - **doc**: minor improvement in OS docs (Alexander Makarenko) [#5006](https://github.com/nodejs/node/pull/5006)
* \[[`1e2108a6b7`](https://github.com/nodejs/node/commit/1e2108a6b7)] - **doc**: fix links in Addons docs (Alexander Makarenko) [#5072](https://github.com/nodejs/node/pull/5072)
* \[[`e5134b1701`](https://github.com/nodejs/node/commit/e5134b1701)] - **doc**: fix inconsistent styling (Brian White) [#4996](https://github.com/nodejs/node/pull/4996)
* \[[`dde160378e`](https://github.com/nodejs/node/commit/dde160378e)] - **doc**: fix link in cluster documentation (Timothy Gu) [#5068](https://github.com/nodejs/node/pull/5068)
* \[[`e5254c12f4`](https://github.com/nodejs/node/commit/e5254c12f4)] - **doc**: fix reference to  API `hash.final` (Minwoo Jung) [#5050](https://github.com/nodejs/node/pull/5050)
* \[[`87fd9968a8`](https://github.com/nodejs/node/commit/87fd9968a8)] - **doc**: clarify optional arguments of Buffer methods (Michaël Zasso) [#5008](https://github.com/nodejs/node/pull/5008)
* \[[`9908eced24`](https://github.com/nodejs/node/commit/9908eced24)] - **doc**: uppercase 'RSA-SHA256' in crypto.markdown (Rainer Oviir) [#5044](https://github.com/nodejs/node/pull/5044)
* \[[`bf0383bbea`](https://github.com/nodejs/node/commit/bf0383bbea)] - **doc**: apply consistent styling for functions (Rich Trott) [#4974](https://github.com/nodejs/node/pull/4974)
* \[[`8c7f4bab2d`](https://github.com/nodejs/node/commit/8c7f4bab2d)] - **doc**: multiple improvements in Stream docs (Alexander Makarenko) [#5009](https://github.com/nodejs/node/pull/5009)
* \[[`ee013715b9`](https://github.com/nodejs/node/commit/ee013715b9)] - **doc**: improve styling consistency in VM docs (Alexander Makarenko) [#5005](https://github.com/nodejs/node/pull/5005)
* \[[`9824b0d132`](https://github.com/nodejs/node/commit/9824b0d132)] - **doc**: fix anchor links from stream to http and events (piepmatz) [#5007](https://github.com/nodejs/node/pull/5007)
* \[[`2c85f79569`](https://github.com/nodejs/node/commit/2c85f79569)] - **doc**: minor improvement to HTTPS doc (Alexander Makarenko) [#5002](https://github.com/nodejs/node/pull/5002)
* \[[`9cf1370017`](https://github.com/nodejs/node/commit/9cf1370017)] - **doc**: improve styling consistency in Buffer docs (Alexander Makarenko) [#5001](https://github.com/nodejs/node/pull/5001)
* \[[`2750cb0613`](https://github.com/nodejs/node/commit/2750cb0613)] - **doc**: consistent styling for functions in TLS docs (Alexander Makarenko) [#5000](https://github.com/nodejs/node/pull/5000)
* \[[`4758bf13a5`](https://github.com/nodejs/node/commit/4758bf13a5)] - **doc**: update npm LICENSE using license-builder.sh (Rebecca Turner) [#4958](https://github.com/nodejs/node/pull/4958)
* \[[`3b08b5d22c`](https://github.com/nodejs/node/commit/3b08b5d22c)] - **doc**: fix minor typo in process doc (Prayag Verma) [#5018](https://github.com/nodejs/node/pull/5018)
* \[[`129977c9c7`](https://github.com/nodejs/node/commit/129977c9c7)] - **doc**: fix typo in Readme.md (Prayag Verma) [#5017](https://github.com/nodejs/node/pull/5017)
* \[[`5de3dc557f`](https://github.com/nodejs/node/commit/5de3dc557f)] - **doc**: fix `notDeepEqual` API (Minwoo Jung) [#4971](https://github.com/nodejs/node/pull/4971)
* \[[`d47dadcc1f`](https://github.com/nodejs/node/commit/d47dadcc1f)] - **doc**: make buffer methods styles consistent (Timothy Gu) [#4873](https://github.com/nodejs/node/pull/4873)
* \[[`17888b122c`](https://github.com/nodejs/node/commit/17888b122c)] - **doc**: fix JSON generation for aliased methods (Timothy Gu) [#4871](https://github.com/nodejs/node/pull/4871)
* \[[`396e4b9199`](https://github.com/nodejs/node/commit/396e4b9199)] - **doc**: add more details to process.env (Evan Lucas) [#4924](https://github.com/nodejs/node/pull/4924)
* \[[`bc11bf4659`](https://github.com/nodejs/node/commit/bc11bf4659)] - **doc**: don't use "interface" as a variable name (ChALkeR) [#4900](https://github.com/nodejs/node/pull/4900)
* \[[`bcf55d2f44`](https://github.com/nodejs/node/commit/bcf55d2f44)] - **doc**: spell writable consistently (Peter Lyons) [#4954](https://github.com/nodejs/node/pull/4954)
* \[[`4a6d0ac436`](https://github.com/nodejs/node/commit/4a6d0ac436)] - **doc**: update eol handling in readline (Kári Tristan Helgason) [#4927](https://github.com/nodejs/node/pull/4927)
* \[[`e65d3638c0`](https://github.com/nodejs/node/commit/e65d3638c0)] - **doc**: replace function expressions with arrows (Benjamin Gruenbaum) [#4832](https://github.com/nodejs/node/pull/4832)
* \[[`423a58d66f`](https://github.com/nodejs/node/commit/423a58d66f)] - **doc**: show links consistently in deprecations (Sakthipriyan Vairamani) [#4907](https://github.com/nodejs/node/pull/4907)
* \[[`fd87659139`](https://github.com/nodejs/node/commit/fd87659139)] - **doc**: add docs working group (Bryan English) [#4244](https://github.com/nodejs/node/pull/4244)
* \[[`19ed619cff`](https://github.com/nodejs/node/commit/19ed619cff)] - **doc**: remove unnecessary bind(this) (Dmitriy Lazarev) [#4797](https://github.com/nodejs/node/pull/4797)
* \[[`5129930786`](https://github.com/nodejs/node/commit/5129930786)] - **doc**: keep the names in sorted order (Sakthipriyan Vairamani) [#4876](https://github.com/nodejs/node/pull/4876)
* \[[`3c46c10d54`](https://github.com/nodejs/node/commit/3c46c10d54)] - **doc**: fix nonsensical grammar in Buffer::write (Jimb Esser) [#4863](https://github.com/nodejs/node/pull/4863)
* \[[`a1af6fc1a7`](https://github.com/nodejs/node/commit/a1af6fc1a7)] - **doc**: add `servername` parameter docs (Alexander Makarenko) [#4729](https://github.com/nodejs/node/pull/4729)
* \[[`f4eeba8467`](https://github.com/nodejs/node/commit/f4eeba8467)] - **doc**: fix code type of markdowns (Jackson Tian) [#4858](https://github.com/nodejs/node/pull/4858)
* \[[`fa1d453359`](https://github.com/nodejs/node/commit/fa1d453359)] - **doc**: check for errors in 'listen' event (Benjamin Gruenbaum) [#4834](https://github.com/nodejs/node/pull/4834)
* \[[`f462320f74`](https://github.com/nodejs/node/commit/f462320f74)] - **doc**: undo move http.IncomingMessage.statusMessage (Jeff Harris) [#4822](https://github.com/nodejs/node/pull/4822)
* \[[`711245e5ac`](https://github.com/nodejs/node/commit/711245e5ac)] - **doc**: style fixes for the TOC (Roman Reiss) [#4748](https://github.com/nodejs/node/pull/4748)
* \[[`611c2f6fdf`](https://github.com/nodejs/node/commit/611c2f6fdf)] - **doc**: proper markdown escaping -> \_\_, \*, \_ (Robert Jefe Lindstaedt) [#4805](https://github.com/nodejs/node/pull/4805)
* \[[`5a860d9cb7`](https://github.com/nodejs/node/commit/5a860d9cb7)] - **doc**: Examples work when data exceeds buffer size (Glen Arrowsmith) [#4811](https://github.com/nodejs/node/pull/4811)
* \[[`71ba14de86`](https://github.com/nodejs/node/commit/71ba14de86)] - **doc**: update list of personal traits in CoC (Kat Marchán) [#4801](https://github.com/nodejs/node/pull/4801)
* \[[`97eedfc57a`](https://github.com/nodejs/node/commit/97eedfc57a)] - **doc**: harmonize $ node command line notation (Robert Jefe Lindstaedt) [#4806](https://github.com/nodejs/node/pull/4806)
* \[[`2dde0f08c9`](https://github.com/nodejs/node/commit/2dde0f08c9)] - **doc**: add buf.indexOf encoding param with example (Karl Skomski) [#3373](https://github.com/nodejs/node/pull/3373)
* \[[`66c74548de`](https://github.com/nodejs/node/commit/66c74548de)] - **doc**: fenced all code blocks, typo fixes (Robert Jefe Lindstaedt) [#4733](https://github.com/nodejs/node/pull/4733)
* \[[`54e8845b5e`](https://github.com/nodejs/node/commit/54e8845b5e)] - **fs**: refactor redeclared variables (Rich Trott) [#4959](https://github.com/nodejs/node/pull/4959)
* \[[`fa940cf9bc`](https://github.com/nodejs/node/commit/fa940cf9bc)] - **fs**: remove unused branches (Benjamin Gruenbaum) [#4795](https://github.com/nodejs/node/pull/4795)
* \[[`7bef1b7907`](https://github.com/nodejs/node/commit/7bef1b7907)] - **(SEMVER-MINOR)** **http**: strictly forbid invalid characters from headers (James M Snell)
* \[[`9b03af254a`](https://github.com/nodejs/node/commit/9b03af254a)] - **http**: remove reference to onParserExecute (Tom Atkinson) [#4773](https://github.com/nodejs/node/pull/4773)
* \[[`101de9de3f`](https://github.com/nodejs/node/commit/101de9de3f)] - **https**: evict cached sessions on error (Fedor Indutny) [#4982](https://github.com/nodejs/node/pull/4982)
* \[[`b2c8b7f6d3`](https://github.com/nodejs/node/commit/b2c8b7f6d3)] - **internal/child\_process**: call postSend on error (Fedor Indutny) [#4752](https://github.com/nodejs/node/pull/4752)
* \[[`55030922e5`](https://github.com/nodejs/node/commit/55030922e5)] - **lib**: scope loop variables (Rich Trott) [#4965](https://github.com/nodejs/node/pull/4965)
* \[[`725ad5b1ce`](https://github.com/nodejs/node/commit/725ad5b1ce)] - **lib**: remove string\_decoder.js var redeclarations (Rich Trott) [#4978](https://github.com/nodejs/node/pull/4978)
* \[[`c09eb44a59`](https://github.com/nodejs/node/commit/c09eb44a59)] - **module**: refactor redeclared variable (Rich Trott) [#4962](https://github.com/nodejs/node/pull/4962)
* \[[`612ce66c78`](https://github.com/nodejs/node/commit/612ce66c78)] - **net**: refactor redeclared variables (Rich Trott) [#4963](https://github.com/nodejs/node/pull/4963)
* \[[`c9b05dafe0`](https://github.com/nodejs/node/commit/c9b05dafe0)] - **net**: move isLegalPort to internal/net (Evan Lucas) [#4882](https://github.com/nodejs/node/pull/4882)
* \[[`7003a4e3d8`](https://github.com/nodejs/node/commit/7003a4e3d8)] - **node\_contextify**: do not incept debug context (Myles Borins) [#4815](https://github.com/nodejs/node/issues/4815)
* \[[`5a77c095a6`](https://github.com/nodejs/node/commit/5a77c095a6)] - **process**: support symbol events (cjihrig) [#4798](https://github.com/nodejs/node/pull/4798)
* \[[`85743c0e92`](https://github.com/nodejs/node/commit/85743c0e92)] - **querystring**: check that maxKeys is finite (Myles Borins) [#5066](https://github.com/nodejs/node/pull/5066)
* \[[`5a10fe932c`](https://github.com/nodejs/node/commit/5a10fe932c)] - **querystring**: use String.prototype.split's limit (Manuel Valls) [#2288](https://github.com/nodejs/node/pull/2288)
* \[[`2844cc03dc`](https://github.com/nodejs/node/commit/2844cc03dc)] - **repl**: remove variable redeclaration (Rich Trott) [#4977](https://github.com/nodejs/node/pull/4977)
* \[[`b5b5bb1e3c`](https://github.com/nodejs/node/commit/b5b5bb1e3c)] - **src**: avoid compiler warning in node\_revert.cc (James M Snell)
* \[[`d387591bbb`](https://github.com/nodejs/node/commit/d387591bbb)] - **(SEMVER-MINOR)** **src**: add --security-revert command line flag (James M Snell)
* \[[`95615196de`](https://github.com/nodejs/node/commit/95615196de)] - **src**: clean up usage of `__proto__` (Jackson Tian) [#5069](https://github.com/nodejs/node/pull/5069)
* \[[`e93b024214`](https://github.com/nodejs/node/commit/e93b024214)] - **src**: remove no longer relevant comments (Chris911) [#4843](https://github.com/nodejs/node/pull/4843)
* \[[`a2c257a3ef`](https://github.com/nodejs/node/commit/a2c257a3ef)] - **src**: fix negative values in process.hrtime() (Ben Noordhuis) [#4757](https://github.com/nodejs/node/pull/4757)
* \[[`b46f3b84d4`](https://github.com/nodejs/node/commit/b46f3b84d4)] - **src,deps**: replace LoadLibrary by LoadLibraryW (Cheng Zhao) [iojs/io.js#226](https://github.com/iojs/io.js/pull/226)
* \[[`ee8d4bb075`](https://github.com/nodejs/node/commit/ee8d4bb075)] - **stream**: prevent object map change in TransformState (Evan Lucas) [#5032](https://github.com/nodejs/node/pull/5032)
* \[[`c8b6de244e`](https://github.com/nodejs/node/commit/c8b6de244e)] - **stream**: refactor redeclared variables (Rich Trott) [#4816](https://github.com/nodejs/node/pull/4816)
* \[[`9dcc45e9c5`](https://github.com/nodejs/node/commit/9dcc45e9c5)] - **test**: enable to work pkcs12 test in FIPS mode (Shigeki Ohtsu) [#5150](https://github.com/nodejs/node/pull/5150)
* \[[`e4390664ae`](https://github.com/nodejs/node/commit/e4390664ae)] - **test**: disable gh-5100 test when in FIPS mode (Fedor Indutny) [#5144](https://github.com/nodejs/node/pull/5144)
* \[[`cf3aa911ec`](https://github.com/nodejs/node/commit/cf3aa911ec)] - **test**: fix flaky test-dgram-pingpong (Rich Trott) [#5125](https://github.com/nodejs/node/pull/5125)
* \[[`63884f57dd`](https://github.com/nodejs/node/commit/63884f57dd)] - **test**: mark flaky tests on Raspberry Pi (Rich Trott) [#5082](https://github.com/nodejs/node/pull/5082)
* \[[`09917c99d8`](https://github.com/nodejs/node/commit/09917c99d8)] - **test**: fix `net-socket-timeout-unref` flakiness (Santiago Gimeno) [#4772](https://github.com/nodejs/node/pull/4772)
* \[[`83da19aa48`](https://github.com/nodejs/node/commit/83da19aa48)] - **test**: fix redeclared test-event-emitter-\* vars (Rich Trott) [#4985](https://github.com/nodejs/node/pull/4985)
* \[[`87b27c913d`](https://github.com/nodejs/node/commit/87b27c913d)] - **test**: fix redeclared test-intl var (Rich Trott) [#4988](https://github.com/nodejs/node/pull/4988)
* \[[`e98772d68e`](https://github.com/nodejs/node/commit/e98772d68e)] - **test**: remove redeclared var in test-domain (Rich Trott) [#4984](https://github.com/nodejs/node/pull/4984)
* \[[`443d0463ca`](https://github.com/nodejs/node/commit/443d0463ca)] - **test**: add common.platformTimeout() to dgram test (Rich Trott) [#4938](https://github.com/nodejs/node/pull/4938)
* \[[`90219c3398`](https://github.com/nodejs/node/commit/90219c3398)] - **test**: fix flaky cluster test on Windows 10 (Rich Trott) [#4934](https://github.com/nodejs/node/pull/4934)
* \[[`3488fa81b5`](https://github.com/nodejs/node/commit/3488fa81b5)] - **test**: fix variable redeclarations (Rich Trott) [#4992](https://github.com/nodejs/node/pull/4992)
* \[[`7dc0905d4d`](https://github.com/nodejs/node/commit/7dc0905d4d)] - **test**: fix redeclared test-util-\* vars (Rich Trott) [#4994](https://github.com/nodejs/node/pull/4994)
* \[[`53e7d605c9`](https://github.com/nodejs/node/commit/53e7d605c9)] - **test**: fix redeclared vars in sequential tests (Rich Trott) [#4999](https://github.com/nodejs/node/pull/4999)
* \[[`a62ace9f7e`](https://github.com/nodejs/node/commit/a62ace9f7e)] - **test**: fix tls-no-rsa-key flakiness (Santiago Gimeno) [#4043](https://github.com/nodejs/node/pull/4043)
* \[[`9b8f025816`](https://github.com/nodejs/node/commit/9b8f025816)] - **test**: fix redeclared vars in test-url (Rich Trott) [#4993](https://github.com/nodejs/node/pull/4993)
* \[[`51fb8845d5`](https://github.com/nodejs/node/commit/51fb8845d5)] - **test**: fix redeclared test-path vars (Rich Trott) [#4991](https://github.com/nodejs/node/pull/4991)
* \[[`b16b360ae8`](https://github.com/nodejs/node/commit/b16b360ae8)] - **test**: fix var redeclarations in test-os (Rich Trott) [#4990](https://github.com/nodejs/node/pull/4990)
* \[[`d6199773e8`](https://github.com/nodejs/node/commit/d6199773e8)] - **test**: fix test-net-\* variable redeclarations (Rich Trott) [#4989](https://github.com/nodejs/node/pull/4989)
* \[[`9dd5b3e01b`](https://github.com/nodejs/node/commit/9dd5b3e01b)] - **test**: fix redeclared test-http-\* vars (Rich Trott) [#4987](https://github.com/nodejs/node/pull/4987)
* \[[`835bf13c1d`](https://github.com/nodejs/node/commit/835bf13c1d)] - **test**: fix var redeclarations in test-fs-\* (Rich Trott) [#4986](https://github.com/nodejs/node/pull/4986)
* \[[`71d7a4457d`](https://github.com/nodejs/node/commit/71d7a4457d)] - **test**: fix redeclared vars in test-vm-\* (Rich Trott) [#4997](https://github.com/nodejs/node/pull/4997)
* \[[`38459402a5`](https://github.com/nodejs/node/commit/38459402a5)] - **test**: fix inconsistent styling in test-url (Brian White) [#5014](https://github.com/nodejs/node/pull/5014)
* \[[`4934798c0d`](https://github.com/nodejs/node/commit/4934798c0d)] - **test**: pummel test fixes (Rich Trott) [#4998](https://github.com/nodejs/node/pull/4998)
* \[[`3970504298`](https://github.com/nodejs/node/commit/3970504298)] - **test**: remove var redeclarations in test-crypto-\* (Rich Trott) [#4981](https://github.com/nodejs/node/pull/4981)
* \[[`a2881e2187`](https://github.com/nodejs/node/commit/a2881e2187)] - **test**: remove test-cluster-\* var redeclarations (Rich Trott) [#4980](https://github.com/nodejs/node/pull/4980)
* \[[`c3d93299c2`](https://github.com/nodejs/node/commit/c3d93299c2)] - **test**: fix test-http-extra-response flakiness (Santiago Gimeno) [#4979](https://github.com/nodejs/node/pull/4979)
* \[[`0384a43885`](https://github.com/nodejs/node/commit/0384a43885)] - **test**: Add assertion for TLS peer certificate fingerprint (Alan Cohen) [#4923](https://github.com/nodejs/node/pull/4923)
* \[[`48a353fe41`](https://github.com/nodejs/node/commit/48a353fe41)] - **test**: scope redeclared vars in test-child-process\* (Rich Trott) [#4944](https://github.com/nodejs/node/pull/4944)
* \[[`89d1149467`](https://github.com/nodejs/node/commit/89d1149467)] - **test**: fix test-tls-zero-clear-in flakiness (Santiago Gimeno) [#4888](https://github.com/nodejs/node/pull/4888)
* \[[`f7ed47341a`](https://github.com/nodejs/node/commit/f7ed47341a)] - **test**: remove Object.observe from tests (Vladimir Kurchatkin) [#4769](https://github.com/nodejs/node/pull/4769)
* \[[`d95e53dc3b`](https://github.com/nodejs/node/commit/d95e53dc3b)] - **test**: refactor switch (Rich Trott) [#4870](https://github.com/nodejs/node/pull/4870)
* \[[`7f1e3e929a`](https://github.com/nodejs/node/commit/7f1e3e929a)] - **test**: remove race condition in http flood test (Rich Trott) [#4793](https://github.com/nodejs/node/pull/4793)
* \[[`6539c64e67`](https://github.com/nodejs/node/commit/6539c64e67)] - **test**: scope redeclared variable (Rich Trott) [#4854](https://github.com/nodejs/node/pull/4854)
* \[[`62fb941557`](https://github.com/nodejs/node/commit/62fb941557)] - **test**: fix irregular whitespace issue (Roman Reiss) [#4864](https://github.com/nodejs/node/pull/4864)
* \[[`3b225209f0`](https://github.com/nodejs/node/commit/3b225209f0)] - **test**: fs.link() test runs on same device (Drew Folta) [#4861](https://github.com/nodejs/node/pull/4861)
* \[[`1860eae110`](https://github.com/nodejs/node/commit/1860eae110)] - **test**: refactor test-net-settimeout (Rich Trott) [#4799](https://github.com/nodejs/node/pull/4799)
* \[[`ae9a8cd053`](https://github.com/nodejs/node/commit/ae9a8cd053)] - **test**: mark test-tick-processor flaky (Rich Trott) [#4809](https://github.com/nodejs/node/pull/4809)
* \[[`57cea9e421`](https://github.com/nodejs/node/commit/57cea9e421)] - **test**: remove test-http-exit-delay (Rich Trott) [#4786](https://github.com/nodejs/node/pull/4786)
* \[[`2119c76d5a`](https://github.com/nodejs/node/commit/2119c76d5a)] - **test**: refactor test-fs-watch (Rich Trott) [#4776](https://github.com/nodejs/node/pull/4776)
* \[[`e487b72459`](https://github.com/nodejs/node/commit/e487b72459)] - **test**: move cluster tests to parallel (Rich Trott) [#4774](https://github.com/nodejs/node/pull/4774)
* \[[`8c694a658c`](https://github.com/nodejs/node/commit/8c694a658c)] - **test**: improve test-cluster-disconnect-suicide-race (Rich Trott) [#4739](https://github.com/nodejs/node/pull/4739)
* \[[`14f5bb7a99`](https://github.com/nodejs/node/commit/14f5bb7a99)] - **test,buffer**: refactor redeclarations (Rich Trott) [#4893](https://github.com/nodejs/node/pull/4893)
* \[[`62479e3406`](https://github.com/nodejs/node/commit/62479e3406)] - **tls**: scope loop vars with let (Rich Trott) [#4853](https://github.com/nodejs/node/pull/4853)
* \[[`d6fbd81a7a`](https://github.com/nodejs/node/commit/d6fbd81a7a)] - **tls\_wrap**: reach error reporting for UV\_EPROTO (Fedor Indutny) [#4885](https://github.com/nodejs/node/pull/4885)
* \[[`f75d06bf10`](https://github.com/nodejs/node/commit/f75d06bf10)] - **tools**: lint for empty character classes in regex (Rich Trott) [#5115](https://github.com/nodejs/node/pull/5115)
* \[[`53cbd0564f`](https://github.com/nodejs/node/commit/53cbd0564f)] - **tools**: lint for spacing around unary operators (Rich Trott) [#5063](https://github.com/nodejs/node/pull/5063)
* \[[`7fa5959c59`](https://github.com/nodejs/node/commit/7fa5959c59)] - **tools**: fix redeclared vars in doc/json.js (Rich Trott) [#5047](https://github.com/nodejs/node/pull/5047)
* \[[`e95fd6ae70`](https://github.com/nodejs/node/commit/e95fd6ae70)] - **tools**: apply linting to doc tools (Rich Trott) [#4973](https://github.com/nodejs/node/pull/4973)
* \[[`777ed82162`](https://github.com/nodejs/node/commit/777ed82162)] - **tools**: fix detecting constructor for JSON doc (Timothy Gu) [#4966](https://github.com/nodejs/node/pull/4966)
* \[[`5d55f59c85`](https://github.com/nodejs/node/commit/5d55f59c85)] - **tools**: add property types in JSON documentation (Timothy Gu) [#4884](https://github.com/nodejs/node/pull/4884)
* \[[`fd5c56698e`](https://github.com/nodejs/node/commit/fd5c56698e)] - **tools**: add support for subkeys in release tools (Myles Borins) [#4807](https://github.com/nodejs/node/pull/4807)
* \[[`34df6a5c0c`](https://github.com/nodejs/node/commit/34df6a5c0c)] - **tools**: enable assorted ESLint error rules (Roman Reiss) [#4864](https://github.com/nodejs/node/pull/4864)
* \[[`386ad7e0b5`](https://github.com/nodejs/node/commit/386ad7e0b5)] - **tools**: fix setting path containing an ampersand (Brian White) [#4804](https://github.com/nodejs/node/pull/4804)
* \[[`e415eb27e5`](https://github.com/nodejs/node/commit/e415eb27e5)] - **url**: change scoping of variables with let (Kári Tristan Helgason) [#4867](https://github.com/nodejs/node/pull/4867)

<a id="5.5.0"></a>

## 2016-01-20, Version 5.5.0 (Stable), @evanlucas

### Notable Changes

* **events**: make sure console functions exist (Dave) [#4479](https://github.com/nodejs/node/pull/4479)
* **fs**: add autoClose option to fs.createWriteStream (Saquib) [#3679](https://github.com/nodejs/node/pull/3679)
* **http**: improves expect header handling (Daniel Sellers) [#4501](https://github.com/nodejs/node/pull/4501)
* **node**: allow preload modules with -i (Evan Lucas) [#4696](https://github.com/nodejs/node/pull/4696)
* **v8,src**: expose statistics about heap spaces (`v8.getHeapSpaceStatistics()`) (Ben Ripkens) [#4463](https://github.com/nodejs/node/pull/4463)
* Minor performance improvements:
  * **lib**: Use arrow functions instead of bind where possible (Minwoo Jung) [#3622](https://github.com/nodejs/node/pull/3622).
    * (Mistakenly missing from v5.4.0)
  * **module**: cache stat() results more aggressively (Ben Noordhuis) [#4575](https://github.com/nodejs/node/pull/4575)
  * **querystring**: improve parse() performance (Brian White) [#4675](https://github.com/nodejs/node/pull/4675)

### Known issues

* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).
* Unicode characters in filesystem paths are not handled consistently across platforms or Node.js APIs. See [#2088](https://github.com/nodejs/node/issues/2088), [#3401](https://github.com/nodejs/node/issues/3401) and [#3519](https://github.com/nodejs/node/issues/3519).

### Commits

* \[[`8d0ca10752`](https://github.com/nodejs/node/commit/8d0ca10752)] - **buffer**: make byteLength work with Buffer correctly (Jackson Tian) [#4738](https://github.com/nodejs/node/pull/4738)
* \[[`83d2b7707e`](https://github.com/nodejs/node/commit/83d2b7707e)] - **buffer**: remove unnecessary TODO comments (Peter Geiss) [#4719](https://github.com/nodejs/node/pull/4719)
* \[[`8182ec094d`](https://github.com/nodejs/node/commit/8182ec094d)] - **build**: add option to select VS version (julien.waechter) [#4645](https://github.com/nodejs/node/pull/4645)
* \[[`4383acd9f4`](https://github.com/nodejs/node/commit/4383acd9f4)] - **build**: fix and refactor VTune config in vcbuild.bat (Rod Vagg) [#4192](https://github.com/nodejs/node/pull/4192)
* \[[`be0b0b8cb9`](https://github.com/nodejs/node/commit/be0b0b8cb9)] - **build**: minor corrections in VTune configure text (Rod Vagg) [#4192](https://github.com/nodejs/node/pull/4192)
* \[[`9571be12f6`](https://github.com/nodejs/node/commit/9571be12f6)] - **cluster**: fix race condition setting suicide prop (Santiago Gimeno) [#4349](https://github.com/nodejs/node/pull/4349)
* \[[`ebd9addcd1`](https://github.com/nodejs/node/commit/ebd9addcd1)] - **crypto**: clear error stack in ECDH::Initialize (Fedor Indutny) [#4689](https://github.com/nodejs/node/pull/4689)
* \[[`66b9c0d8bd`](https://github.com/nodejs/node/commit/66b9c0d8bd)] - **debugger**: remove variable redeclarations (Rich Trott) [#4633](https://github.com/nodejs/node/pull/4633)
* \[[`88b2889679`](https://github.com/nodejs/node/commit/88b2889679)] - **dgram**: prevent disabled optimization of bind() (Brian White) [#4613](https://github.com/nodejs/node/pull/4613)
* \[[`8a11b8c0ef`](https://github.com/nodejs/node/commit/8a11b8c0ef)] - **doc**: restore ICU third-party software licenses (Richard Lau) [#4762](https://github.com/nodejs/node/pull/4762)
* \[[`212a44df03`](https://github.com/nodejs/node/commit/212a44df03)] - **doc**: clarify protocol default in http.request() (cjihrig) [#4714](https://github.com/nodejs/node/pull/4714)
* \[[`3297036345`](https://github.com/nodejs/node/commit/3297036345)] - **doc**: update branch-diff arguments in release doc (Rod Vagg) [#4691](https://github.com/nodejs/node/pull/4691)
* \[[`666c089e68`](https://github.com/nodejs/node/commit/666c089e68)] - **doc**: fix named anchors in addons.markdown and http.markdown (Michael Theriot) [#4708](https://github.com/nodejs/node/pull/4708)
* \[[`310530b7ec`](https://github.com/nodejs/node/commit/310530b7ec)] - **doc**: add path property to Write/ReadStream in fs.markdown (Claudio Rodriguez) [#4368](https://github.com/nodejs/node/pull/4368)
* \[[`3470574cb6`](https://github.com/nodejs/node/commit/3470574cb6)] - **doc**: clarify explanation of first stream section (Vitor Cortez) [#4234](https://github.com/nodejs/node/pull/4234)
* \[[`d91646b9c7`](https://github.com/nodejs/node/commit/d91646b9c7)] - **doc**: rebuild LICENSE using tools/license-builder.sh (Rod Vagg) [#4194](https://github.com/nodejs/node/pull/4194)
* \[[`265e2f557b`](https://github.com/nodejs/node/commit/265e2f557b)] - **doc**: fix typo in doc/node.1 (Jérémy Lal) [#4680](https://github.com/nodejs/node/pull/4680)
* \[[`4c132fe61e`](https://github.com/nodejs/node/commit/4c132fe61e)] - **doc**: make references clickable (Roman Klauke) [#4654](https://github.com/nodejs/node/pull/4654)
* \[[`d139704ff7`](https://github.com/nodejs/node/commit/d139704ff7)] - **doc**: improve child\_process.execFile() code example (Ryan Sobol) [#4504](https://github.com/nodejs/node/pull/4504)
* \[[`eeb6fdcd0f`](https://github.com/nodejs/node/commit/eeb6fdcd0f)] - **doc**: add docs for more stream options (zoubin) [#4639](https://github.com/nodejs/node/pull/4639)
* \[[`b6ab6d2de5`](https://github.com/nodejs/node/commit/b6ab6d2de5)] - **doc**: add branch-diff example to releases.md (Myles Borins) [#4636](https://github.com/nodejs/node/pull/4636)
* \[[`287325c5e8`](https://github.com/nodejs/node/commit/287325c5e8)] - **docs**: update gpg key for Myles Borins (Myles Borins) [#4657](https://github.com/nodejs/node/pull/4657)
* \[[`65825b79aa`](https://github.com/nodejs/node/commit/65825b79aa)] - **docs**: fix npm command in releases.md (Myles Borins) [#4656](https://github.com/nodejs/node/pull/4656)
* \[[`f9a59c1d3b`](https://github.com/nodejs/node/commit/f9a59c1d3b)] - **(SEMVER-MINOR)** **events**: make sure console functions exist (Dave) [#4479](https://github.com/nodejs/node/pull/4479)
* \[[`6039a7c1b5`](https://github.com/nodejs/node/commit/6039a7c1b5)] - **(SEMVER-MINOR)** **fs**: add autoClose option to fs.createWriteStream (Saquib) [#3679](https://github.com/nodejs/node/pull/3679)
* \[[`ed55169834`](https://github.com/nodejs/node/commit/ed55169834)] - **gitignore**: never ignore debug module (Michaël Zasso) [#2286](https://github.com/nodejs/node/pull/2286)
* \[[`d755432fa9`](https://github.com/nodejs/node/commit/d755432fa9)] - **(SEMVER-MINOR)** **http**: improves expect header handling (Daniel Sellers) [#4501](https://github.com/nodejs/node/pull/4501)
* \[[`7ce0e04f44`](https://github.com/nodejs/node/commit/7ce0e04f44)] - **lib**: fix style issues after eslint update (Michaël Zasso) [nodejs/io.js#2286](https://github.com/nodejs/io.js/pull/2286)
* \[[`ae5bcf9528`](https://github.com/nodejs/node/commit/ae5bcf9528)] - **lib**: use arrow functions instead of bind (Minwoo Jung) [#3622](https://github.com/nodejs/node/pull/3622)
* \[[`0ec093cd41`](https://github.com/nodejs/node/commit/0ec093cd41)] - **lib,test**: remove extra semicolons (Michaël Zasso) [#2205](https://github.com/nodejs/node/pull/2205)
* \[[`d8f5bd4fe1`](https://github.com/nodejs/node/commit/d8f5bd4fe1)] - **module**: avoid ArgumentsAdaptorTrampoline frame (Ben Noordhuis) [#4575](https://github.com/nodejs/node/pull/4575)
* \[[`83f8d98806`](https://github.com/nodejs/node/commit/83f8d98806)] - **module**: cache stat() results more aggressively (Ben Noordhuis) [#4575](https://github.com/nodejs/node/pull/4575)
* \[[`ff64a4c395`](https://github.com/nodejs/node/commit/ff64a4c395)] - **(SEMVER-MINOR)** **node**: allow preload modules with -i (Evan Lucas) [#4696](https://github.com/nodejs/node/pull/4696)
* \[[`4bc1a47761`](https://github.com/nodejs/node/commit/4bc1a47761)] - **querystring**: improve parse() performance (Brian White) [#4675](https://github.com/nodejs/node/pull/4675)
* \[[`ad63d350d4`](https://github.com/nodejs/node/commit/ad63d350d4)] - **readline**: Remove XXX and output debuglog (Kohei TAKATA) [#4690](https://github.com/nodejs/node/pull/4690)
* \[[`da550aa063`](https://github.com/nodejs/node/commit/da550aa063)] - **repl**: make sure historyPath is trimmed (Evan Lucas) [#4539](https://github.com/nodejs/node/pull/4539)
* \[[`a2c257a3ef`](https://github.com/nodejs/node/commit/a2c257a3ef)] - **src**: fix negative values in process.hrtime() (Ben Noordhuis) [#4757](https://github.com/nodejs/node/pull/4757)
* \[[`8bad51977a`](https://github.com/nodejs/node/commit/8bad51977a)] - **src**: return UV\_EAI\_NODATA on empty lookup (cjihrig) [#4715](https://github.com/nodejs/node/pull/4715)
* \[[`761cf2bf6a`](https://github.com/nodejs/node/commit/761cf2bf6a)] - **src**: don't check failure with ERR\_peek\_error() (Ben Noordhuis) [#4731](https://github.com/nodejs/node/pull/4731)
* \[[`426ff820f5`](https://github.com/nodejs/node/commit/426ff820f5)] - **stream**: prevent object map change in ReadableState (Evan Lucas) [#4761](https://github.com/nodejs/node/pull/4761)
* \[[`e65f1f7954`](https://github.com/nodejs/node/commit/e65f1f7954)] - **test**: fix tls-multi-key race condition (Santiago Gimeno) [#3966](https://github.com/nodejs/node/pull/3966)
* \[[`3727ae0d7d`](https://github.com/nodejs/node/commit/3727ae0d7d)] - **test**: use addon.md block headings as test dir names (Rod Vagg) [#4412](https://github.com/nodejs/node/pull/4412)
* \[[`a347cd793f`](https://github.com/nodejs/node/commit/a347cd793f)] - **test**: make test-cluster-disconnect-leak reliable (Rich Trott) [#4736](https://github.com/nodejs/node/pull/4736)
* \[[`a39b28bb5a`](https://github.com/nodejs/node/commit/a39b28bb5a)] - **test**: fix issues for space-in-parens ESLint rule (Roman Reiss) [#4753](https://github.com/nodejs/node/pull/4753)
* \[[`d1aabd6264`](https://github.com/nodejs/node/commit/d1aabd6264)] - **test**: fix style issues after eslint update (Michaël Zasso) [nodejs/io.js#2286](https://github.com/nodejs/io.js/pull/2286)
* \[[`e98bcfa2cb`](https://github.com/nodejs/node/commit/e98bcfa2cb)] - **test**: remove 1 second delay from test (Rich Trott) [#4616](https://github.com/nodejs/node/pull/4616)
* \[[`6cfd0b5a32`](https://github.com/nodejs/node/commit/6cfd0b5a32)] - **test**: fix flaky test-net-socket-local-address (cjihrig) [#4650](https://github.com/nodejs/node/pull/4650)
* \[[`e22cc6c2eb`](https://github.com/nodejs/node/commit/e22cc6c2eb)] - **test**: fix race in test-net-server-pause-on-connect (Rich Trott) [#4637](https://github.com/nodejs/node/pull/4637)
* \[[`9164c00bdb`](https://github.com/nodejs/node/commit/9164c00bdb)] - **test**: move resource intensive tests to sequential (Rich Trott) [#4615](https://github.com/nodejs/node/pull/4615)
* \[[`d8ba2c0de4`](https://github.com/nodejs/node/commit/d8ba2c0de4)] - **test**: fix `http-upgrade-client` flakiness (Santiago Gimeno) [#4602](https://github.com/nodejs/node/pull/4602)
* \[[`6018fa1f57`](https://github.com/nodejs/node/commit/6018fa1f57)] - **test**: fix `http-upgrade-agent` flakiness (Santiago Gimeno) [#4520](https://github.com/nodejs/node/pull/4520)
* \[[`8f4f5b3ca5`](https://github.com/nodejs/node/commit/8f4f5b3ca5)] - **tools**: enable space-in-parens ESLint rule (Roman Reiss) [#4753](https://github.com/nodejs/node/pull/4753)
* \[[`162e16afdb`](https://github.com/nodejs/node/commit/162e16afdb)] - **tools**: enable no-extra-semi rule in eslint (Michaël Zasso) [#2205](https://github.com/nodejs/node/pull/2205)
* \[[`031b87d42d`](https://github.com/nodejs/node/commit/031b87d42d)] - **tools**: add license-builder.sh to construct LICENSE (Rod Vagg) [#4194](https://github.com/nodejs/node/pull/4194)
* \[[`ec8e0ae697`](https://github.com/nodejs/node/commit/ec8e0ae697)] - **tools**: fix style issue after eslint update (Michaël Zasso) [nodejs/io.js#2286](https://github.com/nodejs/io.js/pull/2286)
* \[[`4d5ee7a512`](https://github.com/nodejs/node/commit/4d5ee7a512)] - **tools**: update eslint config (Michaël Zasso) [nodejs/io.js#2286](https://github.com/nodejs/io.js/pull/2286)
* \[[`2d441493a4`](https://github.com/nodejs/node/commit/2d441493a4)] - **tools**: update eslint to v1.10.3 (Michaël Zasso) [nodejs/io.js#2286](https://github.com/nodejs/io.js/pull/2286)
* \[[`fe23f4241f`](https://github.com/nodejs/node/commit/fe23f4241f)] - **tools**: fix license-builder.sh for ICU (Richard Lau) [#4762](https://github.com/nodejs/node/pull/4762)
* \[[`5f57005ec9`](https://github.com/nodejs/node/commit/5f57005ec9)] - **(SEMVER-MINOR)** **v8,src**: expose statistics about heap spaces (Ben Ripkens) [#4463](https://github.com/nodejs/node/pull/4463)

<a id="5.4.1"></a>

## 2016-01-12, Version 5.4.1 (Stable), @TheAlphaNerd

### Notable Changes

* Minor performance improvements:
  * **module**: move unnecessary work for early return (Andres Suarez) [#3579](https://github.com/nodejs/node/pull/3579)
* Various bug fixes
* Various doc fixes
* Various test improvements

### Known issues

* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).
* Unicode characters in filesystem paths are not handled consistently across platforms or Node.js APIs. See [#2088](https://github.com/nodejs/node/issues/2088), [#3401](https://github.com/nodejs/node/issues/3401) and [#3519](https://github.com/nodejs/node/issues/3519).

### commits

* \[[`ff539c5bb5`](https://github.com/nodejs/node/commit/ff539c5bb5)] - **cluster**: ignore queryServer msgs on disconnection (Santiago Gimeno) [#4465](https://github.com/nodejs/node/pull/4465)
* \[[`00148b3de1`](https://github.com/nodejs/node/commit/00148b3de1)] - **deps**: backport 066747e from upstream V8 (Ali Ijaz Sheikh) [#4625](https://github.com/nodejs/node/pull/4625)
* \[[`3912b5cbda`](https://github.com/nodejs/node/commit/3912b5cbda)] - **doc**: adds usage of readline line-by-line parsing (Robert Jefe Lindstaedt) [#4609](https://github.com/nodejs/node/pull/4609)
* \[[`102fb7d3a1`](https://github.com/nodejs/node/commit/102fb7d3a1)] - **doc**: remove "above" and "below" references (Richard Sun) [#4499](https://github.com/nodejs/node/pull/4499)
* \[[`df87176ae0`](https://github.com/nodejs/node/commit/df87176ae0)] - **doc**: update stylesheet to match frontpage (Roman Reiss) [#4621](https://github.com/nodejs/node/pull/4621)
* \[[`ede98d1f98`](https://github.com/nodejs/node/commit/ede98d1f98)] - **doc**: stronger suggestion for userland assert (Wyatt Preul) [#4535](https://github.com/nodejs/node/pull/4535)
* \[[`fdfc72c977`](https://github.com/nodejs/node/commit/fdfc72c977)] - **doc**: label http.IncomingMessage as a Class (Sequoia McDowell) [#4589](https://github.com/nodejs/node/pull/4589)
* \[[`b181e26975`](https://github.com/nodejs/node/commit/b181e26975)] - **doc**: document http's server.listen return value (Sequoia McDowell) [#4590](https://github.com/nodejs/node/pull/4590)
* \[[`97aaeb8519`](https://github.com/nodejs/node/commit/97aaeb8519)] - **doc**: fix description about the latest-codename (Minwoo Jung) [#4583](https://github.com/nodejs/node/pull/4583)
* \[[`0126615d1e`](https://github.com/nodejs/node/commit/0126615d1e)] - **doc**: add Evan Lucas to Release Team (Evan Lucas) [#4579](https://github.com/nodejs/node/pull/4579)
* \[[`ec73c69412`](https://github.com/nodejs/node/commit/ec73c69412)] - **doc**: add Myles Borins to Release Team (Myles Borins) [#4578](https://github.com/nodejs/node/pull/4578)
* \[[`e703c9a4e2`](https://github.com/nodejs/node/commit/e703c9a4e2)] - **doc**: bring releases.md up to date (cjihrig) [#4540](https://github.com/nodejs/node/pull/4540)
* \[[`ac1108d5e7`](https://github.com/nodejs/node/commit/ac1108d5e7)] - **doc**: add missing backtick for readline (Brian White) [#4549](https://github.com/nodejs/node/pull/4549)
* \[[`09bc0c6a05`](https://github.com/nodejs/node/commit/09bc0c6a05)] - **doc**: improvements to crypto.markdown copy (James M Snell) [#4435](https://github.com/nodejs/node/pull/4435)
* \[[`787c5d96bd`](https://github.com/nodejs/node/commit/787c5d96bd)] - **http**: remove variable redeclaration (Rich Trott) [#4612](https://github.com/nodejs/node/pull/4612)
* \[[`145b66820f`](https://github.com/nodejs/node/commit/145b66820f)] - **module**: move unnecessary work for early return (Andres Suarez) [#3579](https://github.com/nodejs/node/pull/3579)
* \[[`ffb7deb443`](https://github.com/nodejs/node/commit/ffb7deb443)] - **net**: remove hot path comment from connect (Evan Lucas) [#4648](https://github.com/nodejs/node/pull/4648)
* \[[`799aa74d90`](https://github.com/nodejs/node/commit/799aa74d90)] - **net**: fix dns lookup for android (Josh Dague) [#4580](https://github.com/nodejs/node/pull/4580)
* \[[`9accebe087`](https://github.com/nodejs/node/commit/9accebe087)] - **net, doc**: fix line wrapping lint in net.js (James M Snell) [#4588](https://github.com/nodejs/node/pull/4588)
* \[[`37a546b490`](https://github.com/nodejs/node/commit/37a546b490)] - **src**: remove redeclarations of variables (Rich Trott) [#4605](https://github.com/nodejs/node/pull/4605)
* \[[`b515ccc2a1`](https://github.com/nodejs/node/commit/b515ccc2a1)] - **stream**: remove useless if test in transform (zoubin) [#4617](https://github.com/nodejs/node/pull/4617)
* \[[`ea6e26d904`](https://github.com/nodejs/node/commit/ea6e26d904)] - **test**: remove duplicate fork module import (Rich Trott) [#4634](https://github.com/nodejs/node/pull/4634)
* \[[`b14b2aec5e`](https://github.com/nodejs/node/commit/b14b2aec5e)] - **test**: require common module only once (Rich Trott) [#4611](https://github.com/nodejs/node/pull/4611)
* \[[`f28a640505`](https://github.com/nodejs/node/commit/f28a640505)] - **test**: only include http module once (Rich Trott) [#4606](https://github.com/nodejs/node/pull/4606)
* \[[`6f9a96f497`](https://github.com/nodejs/node/commit/6f9a96f497)] - **test**: fix flaky unrefed timers test (Rich Trott) [#4599](https://github.com/nodejs/node/pull/4599)
* \[[`b70eec8f7b`](https://github.com/nodejs/node/commit/b70eec8f7b)] - **tls\_legacy**: do not read on OpenSSL's stack (Fedor Indutny) [#4624](https://github.com/nodejs/node/pull/4624)

<a id="5.4.0"></a>

## 2016-01-06, Version 5.4.0 (Stable), @Fishrock123

### Notable changes

* **http**:
  * A new status code was added: 451 - "Unavailable For Legal Reasons" (Max Barinov) [#4377](https://github.com/nodejs/node/pull/4377).
  * Idle sockets that have been kept alive now handle errors (José F. Romaniello) [#4482](https://github.com/nodejs/node/pull/4482).
* This release also includes several minor performance improvements:
  * **assert**: deepEqual is now speedier when comparing TypedArrays (Claudio Rodriguez) [#4330](https://github.com/nodejs/node/pull/4330).
  * **lib**: Use arrow functions instead of bind where possible (Minwoo Jung) [node#3622](https://github.com/nodejs/node/pull/3622).
  * **node**: Improved accessor perf of `process.env` (Trevor Norris) [#3780](https://github.com/nodejs/node/pull/3780).
  * **node**: Improved performance of `process.hrtime()` (Trevor Norris) [#3780](https://github.com/nodejs/node/pull/3780), (Evan Lucas) [#4484](https://github.com/nodejs/node/pull/4484).
  * **node**: Improved GetActiveHandles performance (Trevor Norris) [#3780](https://github.com/nodejs/node/pull/3780).
  * **util**: Use faster iteration in `util.format()` (Jackson Tian) [#3964](https://github.com/nodejs/node/pull/3964).

### Known issues

* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).
* Unicode characters in filesystem paths are not handled consistently across platforms or Node.js APIs. See [#2088](https://github.com/nodejs/node/issues/2088), [#3401](https://github.com/nodejs/node/issues/3401) and [#3519](https://github.com/nodejs/node/issues/3519).

### Commits

* \[[`d265fc821a`](https://github.com/nodejs/node/commit/d265fc821a)] - **assert**: typed array deepequal performance fix (Claudio Rodriguez) [#4330](https://github.com/nodejs/node/pull/4330)
* \[[`6d8053ab56`](https://github.com/nodejs/node/commit/6d8053ab56)] - **buffer**: faster case for create Buffer from new Buffer(0) (Jackson Tian) [#4326](https://github.com/nodejs/node/pull/4326)
* \[[`8781c59878`](https://github.com/nodejs/node/commit/8781c59878)] - **buffer**: refactor create buffer (Jackson Tian) [#4340](https://github.com/nodejs/node/pull/4340)
* \[[`252628294b`](https://github.com/nodejs/node/commit/252628294b)] - **child\_process**: guard against race condition (Rich Trott) [#4418](https://github.com/nodejs/node/pull/4418)
* \[[`fcf632bc6a`](https://github.com/nodejs/node/commit/fcf632bc6a)] - **crypto**: load PFX chain the same way as regular one (Fedor Indutny) [#4165](https://github.com/nodejs/node/pull/4165)
* \[[`a5094a35a0`](https://github.com/nodejs/node/commit/a5094a35a0)] - **debugger**: guard against call from non-node context (Ben Noordhuis) [#4328](https://github.com/nodejs/node/pull/4328)
* \[[`b4c51c5b76`](https://github.com/nodejs/node/commit/b4c51c5b76)] - **deps**: backport 200315c from V8 upstream (Vladimir Kurchatkin) [#4128](https://github.com/nodejs/node/pull/4128)
* \[[`334e73942e`](https://github.com/nodejs/node/commit/334e73942e)] - **doc**: fix heading level error in Buffer doc (Shigeki Ohtsu) [#4537](https://github.com/nodejs/node/pull/4537)
* \[[`5be0259181`](https://github.com/nodejs/node/commit/5be0259181)] - **doc**: close backtick in process.title description (Dave) [#4534](https://github.com/nodejs/node/pull/4534)
* \[[`35aec4c14d`](https://github.com/nodejs/node/commit/35aec4c14d)] - **doc**: fix numbering in stream.markdown (Richard Sun) [#4538](https://github.com/nodejs/node/pull/4538)
* \[[`982f3227a5`](https://github.com/nodejs/node/commit/982f3227a5)] - **doc**: improvements to dgram.markdown copy (James M Snell) [#4437](https://github.com/nodejs/node/pull/4437)
* \[[`6cdfa38d23`](https://github.com/nodejs/node/commit/6cdfa38d23)] - **doc**: improvements to errors.markdown copy (James M Snell) [#4454](https://github.com/nodejs/node/pull/4454)
* \[[`6c7bcd5007`](https://github.com/nodejs/node/commit/6c7bcd5007)] - **doc**: fix website wg mislink (jona) [#4357](https://github.com/nodejs/node/pull/4357)
* \[[`eee50821dc`](https://github.com/nodejs/node/commit/eee50821dc)] - **doc**: mention that http.Server inherits from net.Server (Ryan Sobol) [#4455](https://github.com/nodejs/node/pull/4455)
* \[[`c745b4d5f8`](https://github.com/nodejs/node/commit/c745b4d5f8)] - **doc**: explain ClientRequest#setTimeout time unit (Ben Ripkens) [#4458](https://github.com/nodejs/node/pull/4458)
* \[[`40076bf7f8`](https://github.com/nodejs/node/commit/40076bf7f8)] - **doc**: fix spelling error in lib/url.js comment (Nik Nyby) [#4390](https://github.com/nodejs/node/pull/4390)
* \[[`5a223d64e3`](https://github.com/nodejs/node/commit/5a223d64e3)] - **doc**: add anchors for \_transform \_flush \_writev in stream.markdown (iamchenxin) [#4448](https://github.com/nodejs/node/pull/4448)
* \[[`e8bbeecc4c`](https://github.com/nodejs/node/commit/e8bbeecc4c)] - **doc**: improvements to debugger.markdown copy (James M Snell) [#4436](https://github.com/nodejs/node/pull/4436)
* \[[`ccd75fe3fb`](https://github.com/nodejs/node/commit/ccd75fe3fb)] - **doc**: improvements to events.markdown copy (James M Snell) [#4468](https://github.com/nodejs/node/pull/4468)
* \[[`ed15962777`](https://github.com/nodejs/node/commit/ed15962777)] - **doc**: improvements to dns.markdown copy (James M Snell) [#4449](https://github.com/nodejs/node/pull/4449)
* \[[`e177cc9fdf`](https://github.com/nodejs/node/commit/e177cc9fdf)] - **doc**: improvements to console.markdown copy (James M Snell) [#4428](https://github.com/nodejs/node/pull/4428)
* \[[`c1bc9a1023`](https://github.com/nodejs/node/commit/c1bc9a1023)] - **doc**: improve child\_process.markdown copy (James M Snell) [#4383](https://github.com/nodejs/node/pull/4383)
* \[[`150f62847c`](https://github.com/nodejs/node/commit/150f62847c)] - **doc**: copyedit setTimeout() documentation (Rich Trott) [#4434](https://github.com/nodejs/node/pull/4434)
* \[[`9e667354be`](https://github.com/nodejs/node/commit/9e667354be)] - **doc**: fix formatting in process.markdown (Rich Trott) [#4433](https://github.com/nodejs/node/pull/4433)
* \[[`bc1c0dc3fb`](https://github.com/nodejs/node/commit/bc1c0dc3fb)] - **doc**: catch the WORKING\_GROUPS.md bootstrap docs up to date (James M Snell) [#4367](https://github.com/nodejs/node/pull/4367)
* \[[`c835ba3601`](https://github.com/nodejs/node/commit/c835ba3601)] - **doc**: improve assert.markdown copy (James M Snell) [#4360](https://github.com/nodejs/node/pull/4360)
* \[[`e79eda74c0`](https://github.com/nodejs/node/commit/e79eda74c0)] - **doc**: copyedit releases.md (Rich Trott) [#4384](https://github.com/nodejs/node/pull/4384)
* \[[`6450d8667f`](https://github.com/nodejs/node/commit/6450d8667f)] - **doc**: improve grammar in tls docs (Adri Van Houdt) [#4315](https://github.com/nodejs/node/pull/4315)
* \[[`474a0f081a`](https://github.com/nodejs/node/commit/474a0f081a)] - **doc**: improvements to buffer.markdown copy (James M Snell) [#4370](https://github.com/nodejs/node/pull/4370)
* \[[`57684d650e`](https://github.com/nodejs/node/commit/57684d650e)] - **doc**: improve addons.markdown copy (James M Snell) [#4320](https://github.com/nodejs/node/pull/4320)
* \[[`04dd861221`](https://github.com/nodejs/node/commit/04dd861221)] - **doc**: fix, modernize examples in docs (James M Snell) [#4282](https://github.com/nodejs/node/pull/4282)
* \[[`5ce6e99474`](https://github.com/nodejs/node/commit/5ce6e99474)] - **doc**: Typo in buffer.markdown referencing buf.write() (chrisjohn404) [#4324](https://github.com/nodejs/node/pull/4324)
* \[[`699bf2c464`](https://github.com/nodejs/node/commit/699bf2c464)] - **doc**: fix link in addons.markdown (Nicholas Young) [#4331](https://github.com/nodejs/node/pull/4331)
* \[[`e742422757`](https://github.com/nodejs/node/commit/e742422757)] - **fs**: use pushValueToArray for readdir(Sync) (Trevor Norris) [#3780](https://github.com/nodejs/node/pull/3780)
* \[[`1dd2d015d2`](https://github.com/nodejs/node/commit/1dd2d015d2)] - **(SEMVER-MINOR)** **http**: handle errors on idle sockets (José F. Romaniello) [#4482](https://github.com/nodejs/node/pull/4482)
* \[[`083ae166bb`](https://github.com/nodejs/node/commit/083ae166bb)] - **http**: use `self.keepAlive` instead of `self.options.keepAlive` (Damian Schenkelman) [#4407](https://github.com/nodejs/node/pull/4407)
* \[[`ffb4a6e0e4`](https://github.com/nodejs/node/commit/ffb4a6e0e4)] - **http**: fix non-string header value concatenation (Brian White) [#4460](https://github.com/nodejs/node/pull/4460)
* \[[`c77fd6829a`](https://github.com/nodejs/node/commit/c77fd6829a)] - **(SEMVER-MINOR)** **http**: 451 status code "Unavailable For Legal Reasons" (Max Barinov) [#4377](https://github.com/nodejs/node/pull/4377)
* \[[`8f7af9a489`](https://github.com/nodejs/node/commit/8f7af9a489)] - **http**: remove excess calls to removeSocket (Dave) [#4172](https://github.com/nodejs/node/pull/4172)
* \[[`b841967103`](https://github.com/nodejs/node/commit/b841967103)] - **http**: Remove an unnecessary assignment (Bo Borgerson) [#4323](https://github.com/nodejs/node/pull/4323)
* \[[`b8366e76dd`](https://github.com/nodejs/node/commit/b8366e76dd)] - **http\_parser**: use pushValueToArray for headers (Trevor Norris) [#3780](https://github.com/nodejs/node/pull/3780)
* \[[`ca97e7276e`](https://github.com/nodejs/node/commit/ca97e7276e)] - **https**: use `servername` in agent key (Fedor Indutny) [#4389](https://github.com/nodejs/node/pull/4389)
* \[[`b5aaccc6af`](https://github.com/nodejs/node/commit/b5aaccc6af)] - **lib**: remove unused modules (Rich Trott) [#4396](https://github.com/nodejs/node/pull/4396)
* \[[`921fb540c1`](https://github.com/nodejs/node/commit/921fb540c1)] - **node**: improve performance of process.hrtime() (Evan Lucas) [#4484](https://github.com/nodejs/node/pull/4484)
* \[[`ecef817a28`](https://github.com/nodejs/node/commit/ecef817a28)] - **node**: improve accessor perf of process.env (Trevor Norris) [#3780](https://github.com/nodejs/node/pull/3780)
* \[[`89f056bdf3`](https://github.com/nodejs/node/commit/89f056bdf3)] - **node**: improve performance of hrtime() (Trevor Norris) [#3780](https://github.com/nodejs/node/pull/3780)
* \[[`c8fc217dc7`](https://github.com/nodejs/node/commit/c8fc217dc7)] - **node**: improve GetActiveHandles performance (Trevor Norris) [#3780](https://github.com/nodejs/node/pull/3780)
* \[[`8464667071`](https://github.com/nodejs/node/commit/8464667071)] - **node**: fix erroneously named function call (Trevor Norris) [#3780](https://github.com/nodejs/node/pull/3780)
* \[[`e57fd51a5e`](https://github.com/nodejs/node/commit/e57fd51a5e)] - **os**: fix crash in GetInterfaceAddresses (Martin Bark) [#4272](https://github.com/nodejs/node/pull/4272)
* \[[`65c40d753f`](https://github.com/nodejs/node/commit/65c40d753f)] - **repl**: remove unused function (Rich Trott)
* \[[`3d41a44dba`](https://github.com/nodejs/node/commit/3d41a44dba)] - **repl**: Fixed node repl history edge case. (Mudit Ameta) [#4108](https://github.com/nodejs/node/pull/4108)
* \[[`d11930d604`](https://github.com/nodejs/node/commit/d11930d604)] - **repl**: use String#repeat instead of Array#join (Evan Lucas) [#3900](https://github.com/nodejs/node/pull/3900)
* \[[`4220d25626`](https://github.com/nodejs/node/commit/4220d25626)] - **test**: fix linting for the v5.x branch (Jeremiah Senkpiel) [#4547](https://github.com/nodejs/node/pull/4547)
* \[[`4b14f1c983`](https://github.com/nodejs/node/commit/4b14f1c983)] - **test**: remove unused vars (Rich Trott) [#4536](https://github.com/nodejs/node/pull/4536)
* \[[`2a69ab32ec`](https://github.com/nodejs/node/commit/2a69ab32ec)] - **test**: add test-domain-exit-dispose-again back (Julien Gilli) [#4256](https://github.com/nodejs/node/pull/4256)
* \[[`ae0246641c`](https://github.com/nodejs/node/commit/ae0246641c)] - **test**: remove unused vars from parallel tests (Rich Trott) [#4511](https://github.com/nodejs/node/pull/4511)
* \[[`984db93e7c`](https://github.com/nodejs/node/commit/984db93e7c)] - **test**: fix flaky test-cluster-shared-leak (Rich Trott) [#4510](https://github.com/nodejs/node/pull/4510)
* \[[`30b0d7583a`](https://github.com/nodejs/node/commit/30b0d7583a)] - **test**: fix flaky streams test (Rich Trott) [#4516](https://github.com/nodejs/node/pull/4516)
* \[[`46fefbc1b5`](https://github.com/nodejs/node/commit/46fefbc1b5)] - **test**: fix flaky test-http-agent-keepalive (Rich Trott) [#4524](https://github.com/nodejs/node/pull/4524)
* \[[`e04a8401d9`](https://github.com/nodejs/node/commit/e04a8401d9)] - **test**: remove flaky designations for tests (Rich Trott) [#4519](https://github.com/nodejs/node/pull/4519)
* \[[`a703b1bf73`](https://github.com/nodejs/node/commit/a703b1bf73)] - **test**: remove time check (Rich Trott) [#4494](https://github.com/nodejs/node/pull/4494)
* \[[`02b3a5be52`](https://github.com/nodejs/node/commit/02b3a5be52)] - **test**: refactor test-fs-empty-readStream (Rich Trott) [#4490](https://github.com/nodejs/node/pull/4490)
* \[[`ab3e5c1417`](https://github.com/nodejs/node/commit/ab3e5c1417)] - **test**: write to tmp dir rather than fixture dir (Rich Trott) [#4489](https://github.com/nodejs/node/pull/4489)
* \[[`06043fdfa3`](https://github.com/nodejs/node/commit/06043fdfa3)] - **test**: remove unused modules (Rich Trott) [#4475](https://github.com/nodejs/node/pull/4475)
* \[[`f1a66bc249`](https://github.com/nodejs/node/commit/f1a66bc249)] - **test**: clarify role of domains in test (Rich Trott) [#4474](https://github.com/nodejs/node/pull/4474)
* \[[`08a3490dd6`](https://github.com/nodejs/node/commit/08a3490dd6)] - **test**: inherit JOBS from environment (Johan Bergström) [#4495](https://github.com/nodejs/node/pull/4495)
* \[[`3bfc18763a`](https://github.com/nodejs/node/commit/3bfc18763a)] - **test**: improve assert message (Rich Trott) [#4461](https://github.com/nodejs/node/pull/4461)
* \[[`d46d850461`](https://github.com/nodejs/node/commit/d46d850461)] - **test**: shorten path for bogus socket (Rich Trott) [#4478](https://github.com/nodejs/node/pull/4478)
* \[[`f68f86cd0a`](https://github.com/nodejs/node/commit/f68f86cd0a)] - **test**: fix race condition in test-http-client-onerror (Devin Nakamura) [#4346](https://github.com/nodejs/node/pull/4346)
* \[[`ec0b6362cf`](https://github.com/nodejs/node/commit/ec0b6362cf)] - **test**: remove unused assert module imports (Rich Trott) [#4438](https://github.com/nodejs/node/pull/4438)
* \[[`ba2445046c`](https://github.com/nodejs/node/commit/ba2445046c)] - **test**: don't use cwd for relative path (Johan Bergström) [#4477](https://github.com/nodejs/node/pull/4477)
* \[[`5110e4deed`](https://github.com/nodejs/node/commit/5110e4deed)] - **test**: don't assume a certain folder structure (Johan Bergström) [#3325](https://github.com/nodejs/node/pull/3325)
* \[[`55c6946400`](https://github.com/nodejs/node/commit/55c6946400)] - **test**: make temp path customizable (Johan Bergström) [#3325](https://github.com/nodejs/node/pull/3325)
* \[[`b19d19efaa`](https://github.com/nodejs/node/commit/b19d19efaa)] - **test**: extend timeout in Debug mode (Rich Trott) [#4431](https://github.com/nodejs/node/pull/4431)
* \[[`c6a99ddd37`](https://github.com/nodejs/node/commit/c6a99ddd37)] - **test**: remove unused variables from net tests (Rich Trott) [#4430](https://github.com/nodejs/node/pull/4430)
* \[[`54004f0e26`](https://github.com/nodejs/node/commit/54004f0e26)] - **test**: remove unused vars in ChildProcess tests (Rich Trott) [#4425](https://github.com/nodejs/node/pull/4425)
* \[[`e72112f90e`](https://github.com/nodejs/node/commit/e72112f90e)] - **test**: fix flaky cluster-disconnect-race (Brian White) [#4457](https://github.com/nodejs/node/pull/4457)
* \[[`715afc9bbd`](https://github.com/nodejs/node/commit/715afc9bbd)] - **test**: fix flaky cluster-net-send (Brian White) [#4444](https://github.com/nodejs/node/pull/4444)
* \[[`03c4bc704f`](https://github.com/nodejs/node/commit/03c4bc704f)] - **test**: fix flaky child-process-fork-regr-gh-2847 (Brian White) [#4442](https://github.com/nodejs/node/pull/4442)
* \[[`684eb32072`](https://github.com/nodejs/node/commit/684eb32072)] - **test**: remove unused variables from HTTPS tests (Rich Trott) [#4426](https://github.com/nodejs/node/pull/4426)
* \[[`585c01f674`](https://github.com/nodejs/node/commit/585c01f674)] - **test**: remove unused variables from TLS tests (Rich Trott) [#4424](https://github.com/nodejs/node/pull/4424)
* \[[`c36ca37e2a`](https://github.com/nodejs/node/commit/c36ca37e2a)] - **test**: remove unused variables form http tests (Rich Trott) [#4422](https://github.com/nodejs/node/pull/4422)
* \[[`c639d0f1fe`](https://github.com/nodejs/node/commit/c639d0f1fe)] - **test**: mark test-debug-no-context is flaky (Rich Trott) [#4421](https://github.com/nodejs/node/pull/4421)
* \[[`cd79ec268d`](https://github.com/nodejs/node/commit/cd79ec268d)] - **test**: remove unnecessary assignments (Rich Trott) [#4408](https://github.com/nodejs/node/pull/4408)
* \[[`0799a9abaf`](https://github.com/nodejs/node/commit/0799a9abaf)] - **test**: remove unused var from test-assert.js (Rich Trott) [#4405](https://github.com/nodejs/node/pull/4405)
* \[[`3710028a85`](https://github.com/nodejs/node/commit/3710028a85)] - **test**: remove unused `util` imports (Rich Trott) [#4397](https://github.com/nodejs/node/pull/4397)
* \[[`8c9d0c1f6f`](https://github.com/nodejs/node/commit/8c9d0c1f6f)] - **test**: refactor test-net-connect-options-ipv6 (Rich Trott) [#4395](https://github.com/nodejs/node/pull/4395)
* \[[`874209022f`](https://github.com/nodejs/node/commit/874209022f)] - **test**: fix http-response-multiheaders (Santiago Gimeno) [#3958](https://github.com/nodejs/node/pull/3958)
* \[[`71b79bcf54`](https://github.com/nodejs/node/commit/71b79bcf54)] - **test**: test each block in addon.md contains js & cc (Rod Vagg) [#4411](https://github.com/nodejs/node/pull/4411)
* \[[`00b37de243`](https://github.com/nodejs/node/commit/00b37de243)] - **test**: fix domain-top-level-error-handler-throw (Santiago Gimeno) [#4364](https://github.com/nodejs/node/pull/4364)
* \[[`6d14b6520f`](https://github.com/nodejs/node/commit/6d14b6520f)] - **test**: use platformTimeout() in more places (Brian White) [#4387](https://github.com/nodejs/node/pull/4387)
* \[[`82f74caa56`](https://github.com/nodejs/node/commit/82f74caa56)] - **test**: fix flaky test-net-error-twice (Brian White) [#4342](https://github.com/nodejs/node/pull/4342)
* \[[`96501e55be`](https://github.com/nodejs/node/commit/96501e55be)] - **test**: try other ipv6 localhost alternatives (Brian White) [#4325](https://github.com/nodejs/node/pull/4325)
* \[[`69343d6d2e`](https://github.com/nodejs/node/commit/69343d6d2e)] - **tls\_wrap**: clear errors on return (Fedor Indutny) [#4515](https://github.com/nodejs/node/pull/4515)
* \[[`ca9812cf4d`](https://github.com/nodejs/node/commit/ca9812cf4d)] - **tools**: fix warning in doc parsing (Shigeki Ohtsu) [#4537](https://github.com/nodejs/node/pull/4537)
* \[[`386030b524`](https://github.com/nodejs/node/commit/386030b524)] - **tools**: implement no-unused-vars for eslint (Rich Trott) [#4536](https://github.com/nodejs/node/pull/4536)
* \[[`14a947fc70`](https://github.com/nodejs/node/commit/14a947fc70)] - **tools**: run tick processor without forking (Matt Loring) [#4224](https://github.com/nodejs/node/pull/4224)
* \[[`8039ca06eb`](https://github.com/nodejs/node/commit/8039ca06eb)] - **util**: faster arrayToHash (Jackson Tian)  [#3964](https://github.com/nodejs/node/pull/3964)

<a id="5.3.0"></a>

## 2015-12-16, Version 5.3.0 (Stable), @cjihrig

### Notable changes

* **buffer**:
  * `Buffer.prototype.includes()` has been added to keep parity with TypedArrays. (Alexander Martin) [#3567](https://github.com/nodejs/node/pull/3567).
* **domains**:
  * Fix handling of uncaught exceptions. (Julien Gilli) [#3654](https://github.com/nodejs/node/pull/3654).
* **https**:
  * Added support for disabling session caching. (Fedor Indutny) [#4252](https://github.com/nodejs/node/pull/4252).
* **repl**:
  * Allow third party modules to be imported using `require()`. This corrects a regression from 5.2.0. (Ben Noordhuis) [#4215](https://github.com/nodejs/node/pull/4215).
* **deps**:
  * Upgrade libuv to 1.8.0. (Saúl Ibarra Corretgé) [#4276](https://github.com/nodejs/node/pull/4276).

### Known issues

* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).
* Unicode characters in filesystem paths are not handled consistently across platforms or Node.js APIs. See [#2088](https://github.com/nodejs/node/issues/2088), [#3401](https://github.com/nodejs/node/issues/3401) and [#3519](https://github.com/nodejs/node/issues/3519).

### Commits

* \[[`6ca5ea3860`](https://github.com/nodejs/node/commit/6ca5ea3860)] - 2015-12-09, Version 5.2.0 (Stable) (Rod Vagg) [#4181](https://github.com/nodejs/node/pull/4181)
* \[[`da5cdc2207`](https://github.com/nodejs/node/commit/da5cdc2207)] - **assert**: accommodate ES6 classes that extend Error (Rich Trott) [#4166](https://github.com/nodejs/node/pull/4166)
* \[[`67e181986a`](https://github.com/nodejs/node/commit/67e181986a)] - **(SEMVER-MINOR)** **buffer**: add includes() for parity with TypedArray (Alexander Martin) [#3567](https://github.com/nodejs/node/pull/3567)
* \[[`84dea1bd0c`](https://github.com/nodejs/node/commit/84dea1bd0c)] - **configure**: fix arm vfpv2 (Jörg Krause) [#4203](https://github.com/nodejs/node/pull/4203)
* \[[`a7f5dfd14c`](https://github.com/nodejs/node/commit/a7f5dfd14c)] - **configure**: use \_\_ARM\_ARCH to determine arm version (João Reis) [#4123](https://github.com/nodejs/node/pull/4123)
* \[[`0e3912be0b`](https://github.com/nodejs/node/commit/0e3912be0b)] - **configure**: respect CC\_host in host arch detection (João Reis) [#4117](https://github.com/nodejs/node/pull/4117)
* \[[`69b94ec55c`](https://github.com/nodejs/node/commit/69b94ec55c)] - **deps**: upgrade libuv to 1.8.0 (Saúl Ibarra Corretgé) [#4276](https://github.com/nodejs/node/pull/4276)
* \[[`a8854e5b59`](https://github.com/nodejs/node/commit/a8854e5b59)] - **doc**: document the cache parameter for fs.realpathSync (Jackson Tian) [#4285](https://github.com/nodejs/node/pull/4285)
* \[[`9e1b7aa874`](https://github.com/nodejs/node/commit/9e1b7aa874)] - **doc**: document backlog for server.listen() variants (Jan Schär) [#4025](https://github.com/nodejs/node/pull/4025)
* \[[`435d571f22`](https://github.com/nodejs/node/commit/435d571f22)] - **doc**: update AUTHORS list (Rod Vagg) [#4183](https://github.com/nodejs/node/pull/4183)
* \[[`3b3061365a`](https://github.com/nodejs/node/commit/3b3061365a)] - **doc**: update irc channels: #node.js and #node-dev (Nelson Pecora) [#2743](https://github.com/nodejs/node/pull/2743)
* \[[`9538fd02e5`](https://github.com/nodejs/node/commit/9538fd02e5)] - **doc**: clarify error events in HTTP module documentation (Lenny Markus) [#4275](https://github.com/nodejs/node/pull/4275)
* \[[`c6efd535e4`](https://github.com/nodejs/node/commit/c6efd535e4)] - **doc**: fix improper http.get sample code (Hideki Yamamura) [#4263](https://github.com/nodejs/node/pull/4263)
* \[[`498c9adb08`](https://github.com/nodejs/node/commit/498c9adb08)] - **doc**: add CTC meeting minutes 2015-10-28 (Rod Vagg) [#3661](https://github.com/nodejs/node/pull/3661)
* \[[`671347cf13`](https://github.com/nodejs/node/commit/671347cf13)] - **doc**: fixup socket.remoteAddress (Arthur Gautier) [#4198](https://github.com/nodejs/node/pull/4198)
* \[[`f050cab3d8`](https://github.com/nodejs/node/commit/f050cab3d8)] - **doc**: copyedit console doc (Rich Trott) [#4225](https://github.com/nodejs/node/pull/4225)
* \[[`1a21a5368b`](https://github.com/nodejs/node/commit/1a21a5368b)] - **doc**: move description of 'equals' method to right place (janriemer) [#4227](https://github.com/nodejs/node/pull/4227)
* \[[`9a9c5259bf`](https://github.com/nodejs/node/commit/9a9c5259bf)] - **doc**: Fixing broken links to the v8 wiki (Tom Gallacher) [#4241](https://github.com/nodejs/node/pull/4241)
* \[[`37ed05b8c1`](https://github.com/nodejs/node/commit/37ed05b8c1)] - **doc**: copyedit child\_process doc (Rich Trott) [#4188](https://github.com/nodejs/node/pull/4188)
* \[[`e47ae5808b`](https://github.com/nodejs/node/commit/e47ae5808b)] - **doc**: copyedit buffer doc (Rich Trott) [#4187](https://github.com/nodejs/node/pull/4187)
* \[[`70fb06a90b`](https://github.com/nodejs/node/commit/70fb06a90b)] - **doc**: clarify assert.fail doc (Rich Trott) [#4186](https://github.com/nodejs/node/pull/4186)
* \[[`e3187cc81e`](https://github.com/nodejs/node/commit/e3187cc81e)] - **doc**: copyedit addons doc (Rich Trott) [#4185](https://github.com/nodejs/node/pull/4185)
* \[[`931ab967ff`](https://github.com/nodejs/node/commit/931ab967ff)] - **doc**: add calvinmetcalf to collaborators (Calvin Metcalf) [#4218](https://github.com/nodejs/node/pull/4218)
* \[[`01ce23148b`](https://github.com/nodejs/node/commit/01ce23148b)] - **doc**: add mcollina to collaborators (Matteo Collina) [#4220](https://github.com/nodejs/node/pull/4220)
* \[[`bd8753aabf`](https://github.com/nodejs/node/commit/bd8753aabf)] - **doc**: add rmg to collaborators (Ryan Graham) [#4219](https://github.com/nodejs/node/pull/4219)
* \[[`73a9a6fc92`](https://github.com/nodejs/node/commit/73a9a6fc92)] - **doc**: harmonize description of `ca` argument (Ben Noordhuis) [#4213](https://github.com/nodejs/node/pull/4213)
* \[[`dfc8bedbc5`](https://github.com/nodejs/node/commit/dfc8bedbc5)] - **doc**: change references from node to Node.js (Roman Klauke) [#4177](https://github.com/nodejs/node/pull/4177)
* \[[`7a518788e9`](https://github.com/nodejs/node/commit/7a518788e9)] - **doc, test**: symbols as event names (Bryan English) [#4151](https://github.com/nodejs/node/pull/4151)
* \[[`425a3545d2`](https://github.com/nodejs/node/commit/425a3545d2)] - **(SEMVER-MINOR)** **domains**: fix handling of uncaught exceptions (Julien Gilli) [#3654](https://github.com/nodejs/node/pull/3654)
* \[[`acef181fde`](https://github.com/nodejs/node/commit/acef181fde)] - **(SEMVER-MINOR)** **https**: support disabling session caching (Fedor Indutny) [#4252](https://github.com/nodejs/node/pull/4252)
* \[[`2a60e2ad71`](https://github.com/nodejs/node/commit/2a60e2ad71)] - **module,src**: do not wrap modules with -1 lineOffset (cjihrig) [#4298](https://github.com/nodejs/node/pull/4298)
* \[[`d3c498b1b7`](https://github.com/nodejs/node/commit/d3c498b1b7)] - **node**: remove unused variables in AppendExceptionLine (Yazhong Liu) [#4264](https://github.com/nodejs/node/pull/4264)
* \[[`aad6b9f0eb`](https://github.com/nodejs/node/commit/aad6b9f0eb)] - **repl**: display error message when loading directory (Prince J Wesley) [#4170](https://github.com/nodejs/node/pull/4170)
* \[[`213ede6cee`](https://github.com/nodejs/node/commit/213ede6cee)] - **repl**: fix require('3rdparty') regression (Ben Noordhuis) [#4215](https://github.com/nodejs/node/pull/4215)
* \[[`f176b31e74`](https://github.com/nodejs/node/commit/f176b31e74)] - **src**: remove \_\_builtin\_bswap16 call (Ben Noordhuis) [#4290](https://github.com/nodejs/node/pull/4290)
* \[[`ce2471673f`](https://github.com/nodejs/node/commit/ce2471673f)] - **src**: remove unused BITS\_PER\_LONG macro (Ben Noordhuis) [#4290](https://github.com/nodejs/node/pull/4290)
* \[[`b799a74709`](https://github.com/nodejs/node/commit/b799a74709)] - **src**: fix line numbers on core errors (cjihrig) [#4254](https://github.com/nodejs/node/pull/4254)
* \[[`c311b61430`](https://github.com/nodejs/node/commit/c311b61430)] - **src**: fix deprecation message for ErrnoException (Martin von Gagern) [#4269](https://github.com/nodejs/node/pull/4269)
* \[[`2859f9ef92`](https://github.com/nodejs/node/commit/2859f9ef92)] - **test**: fix debug-port-cluster flakiness (Ben Noordhuis) [#4310](https://github.com/nodejs/node/pull/4310)
* \[[`cb0b4a6bc0`](https://github.com/nodejs/node/commit/cb0b4a6bc0)] - **test**: add test for debugging one line files (cjihrig) [#4298](https://github.com/nodejs/node/pull/4298)
* \[[`0b9c3a30d6`](https://github.com/nodejs/node/commit/0b9c3a30d6)] - **test**: add test for tls.parseCertString (Evan Lucas) [#4283](https://github.com/nodejs/node/pull/4283)
* \[[`7598ed6cc0`](https://github.com/nodejs/node/commit/7598ed6cc0)] - **test**: parallelize test-repl-persistent-history (Jeremiah Senkpiel) [#4247](https://github.com/nodejs/node/pull/4247)
* \[[`668449ad14`](https://github.com/nodejs/node/commit/668449ad14)] - **test**: use regular timeout times for ARMv8 (Jeremiah Senkpiel) [#4248](https://github.com/nodejs/node/pull/4248)
* \[[`23e7703c85`](https://github.com/nodejs/node/commit/23e7703c85)] - **test**: fix http-many-ended-pipelines flakiness (Santiago Gimeno) [#4041](https://github.com/nodejs/node/pull/4041)
* \[[`3b94991bda`](https://github.com/nodejs/node/commit/3b94991bda)] - **test**: fix tls-inception flakiness (Santiago Gimeno) [#4195](https://github.com/nodejs/node/pull/4195)
* \[[`86a3bd09b0`](https://github.com/nodejs/node/commit/86a3bd09b0)] - **test**: fix tls-inception (Santiago Gimeno) [#4195](https://github.com/nodejs/node/pull/4195)
* \[[`1e89830a11`](https://github.com/nodejs/node/commit/1e89830a11)] - **test**: don't assume openssl s\_client supports -ssl3 (Ben Noordhuis) [#4204](https://github.com/nodejs/node/pull/4204)
* \[[`c5b4f6bc99`](https://github.com/nodejs/node/commit/c5b4f6bc99)] - **(SEMVER-MINOR)** **tls**: introduce `secureContext` for `tls.connect` (Fedor Indutny) [#4246](https://github.com/nodejs/node/pull/4246)
* \[[`e0bb118a1d`](https://github.com/nodejs/node/commit/e0bb118a1d)] - **tls\_wrap**: inherit from the `AsyncWrap` first (Fedor Indutny) [#4268](https://github.com/nodejs/node/pull/4268)
* \[[`d63cceeb10`](https://github.com/nodejs/node/commit/d63cceeb10)] - **tools**: add .editorconfig (ronkorving) [#2993](https://github.com/nodejs/node/pull/2993)
* \[[`4b267df93e`](https://github.com/nodejs/node/commit/4b267df93e)] - **udp**: remove a needless instanceof Buffer check (ronkorving) [#4301](https://github.com/nodejs/node/pull/4301)

<a id="5.2.0"></a>

## 2015-12-09, Version 5.2.0 (Stable), @rvagg

### Notable changes

* **build**:
  * Add support for Intel's VTune JIT profiling when compiled with `--enable-vtune-profiling`. For more information about VTune, see <https://software.intel.com/en-us/node/544211>. (Chunyang Dai) [#3785](https://github.com/nodejs/node/pull/3785).
  * Properly enable V8 snapshots by default. Due to a configuration error, snapshots have been kept off by default when the intention is for the feature to be enabled. (Fedor Indutny) [#3962](https://github.com/nodejs/node/pull/3962).
* **crypto**:
  * Simplify use of ECDH (Elliptic Curve Diffie-Hellman) objects (created via `crypto.createECDH(curve_name)`) with private keys that are not dynamically generated via `generateKeys()`. The public key is now computed when explicitly setting a private key. Added validity checks to reduce the possibility of computing weak or invalid shared secrets. Also, deprecated the `setPublicKey()` method for ECDH objects as its usage is unnecessary and can lead to inconsistent state. (Michael Ruddy) [#3511](https://github.com/nodejs/node/pull/3511).
  * Update root certificates from the current list stored maintained by Mozilla NSS. (Ben Noordhuis) [#3951](https://github.com/nodejs/node/pull/3951).
  * Multiple CA certificates can now be passed with the `ca` option to TLS methods as an array of strings or in a single new-line separated string. (Ben Noordhuis) [#4099](https://github.com/nodejs/node/pull/4099)
* **tools**: Include a tick processor in core, exposed via the `--prof-process` command-line argument which can be used to process V8 profiling output files generated when using the `--prof` command-line argument. (Matt Loring) [#4021](https://github.com/nodejs/node/pull/4021).

### Known issues

* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).
* Unicode characters in filesystem paths are not handled consistently across platforms or Node.js APIs. See [#2088](https://github.com/nodejs/node/issues/2088), [#3401](https://github.com/nodejs/node/issues/3401) and [#3519](https://github.com/nodejs/node/issues/3519).

### Commits

* \[[`08a3f29fd4`](https://github.com/nodejs/node/commit/08a3f29fd4)] - **buffer**: fix range checking for slowToString (Matt Loring) [#4019](https://github.com/nodejs/node/pull/4019)
* \[[`e3a8e8bba4`](https://github.com/nodejs/node/commit/e3a8e8bba4)] - **buffer**: Prevent Buffer constructor deopt (Bryce Baril) [#4158](https://github.com/nodejs/node/pull/4158)
* \[[`0e18e68324`](https://github.com/nodejs/node/commit/0e18e68324)] - **buffer**: fix writeInt{B,L}E for some neg values (Peter A. Bigot) [#3994](https://github.com/nodejs/node/pull/3994)
* \[[`ab5b529dd2`](https://github.com/nodejs/node/commit/ab5b529dd2)] - **buffer**: default to UTF8 in byteLength() (Tom Gallacher) [#4010](https://github.com/nodejs/node/pull/4010)
* \[[`fcf0e8ebdf`](https://github.com/nodejs/node/commit/fcf0e8ebdf)] - **buffer**: move checkFloat from lib into src (Matt Loring) [#3763](https://github.com/nodejs/node/pull/3763)
* \[[`12649f4496`](https://github.com/nodejs/node/commit/12649f4496)] - **build**: add "--partly-static" build options (Super Zheng) [#4152](https://github.com/nodejs/node/pull/4152)
* \[[`a76d788119`](https://github.com/nodejs/node/commit/a76d788119)] - **build**: update signtool description, add url (Rod Vagg) [#4011](https://github.com/nodejs/node/pull/4011)
* \[[`ed255abdc1`](https://github.com/nodejs/node/commit/ed255abdc1)] - **(SEMVER-MINOR)** **build,src**: add Intel Vtune profiling support (Chunyang Dai) [#3785](https://github.com/nodejs/node/pull/3785)
* \[[`7793c364fc`](https://github.com/nodejs/node/commit/7793c364fc)] - **child\_process**: flush consuming streams (Dave) [#4071](https://github.com/nodejs/node/pull/4071)
* \[[`f29c5d6e70`](https://github.com/nodejs/node/commit/f29c5d6e70)] - **configure**: `v8_use_snapshot` should be `true` (Fedor Indutny) [#3962](https://github.com/nodejs/node/pull/3962)
* \[[`da5ac55c83`](https://github.com/nodejs/node/commit/da5ac55c83)] - **(SEMVER-MINOR)** **crypto**: simplify using pre-existing keys with ECDH (Michael Ruddy) [#3511](https://github.com/nodejs/node/pull/3511)
* \[[`cfc97641ee`](https://github.com/nodejs/node/commit/cfc97641ee)] - **crypto**: fix native module compilation with FIPS (Stefan Budeanu) [#4023](https://github.com/nodejs/node/pull/4023)
* \[[`b81b45dabd`](https://github.com/nodejs/node/commit/b81b45dabd)] - **crypto**: update root certificates (Ben Noordhuis) [#3951](https://github.com/nodejs/node/pull/3951)
* \[[`def681a07e`](https://github.com/nodejs/node/commit/def681a07e)] - **crypto**: disable crypto.createCipher in FIPS mode (Stefan Budeanu) [#3754](https://github.com/nodejs/node/pull/3754)
* \[[`ce423f3624`](https://github.com/nodejs/node/commit/ce423f3624)] - **debugger**: introduce exec method for debugger (Jackson Tian)
* \[[`99fd1ec28d`](https://github.com/nodejs/node/commit/99fd1ec28d)] - **deps**: backport 819b40a from V8 upstream (Michaël Zasso) [#3937](https://github.com/nodejs/node/pull/3937)
* \[[`82252b2a17`](https://github.com/nodejs/node/commit/82252b2a17)] - **doc**: add brief Node.js overview to README (wurde) [#4174](https://github.com/nodejs/node/pull/4174)
* \[[`634c5f1f81`](https://github.com/nodejs/node/commit/634c5f1f81)] - **doc**: url.format - true slash postfix behaviour (fansworld-claudio) [#4119](https://github.com/nodejs/node/pull/4119)
* \[[`6f957a70d8`](https://github.com/nodejs/node/commit/6f957a70d8)] - **doc**: s/node.js/Node.js in readme (Rod Vagg) [#3998](https://github.com/nodejs/node/pull/3998)
* \[[`0cd4a52392`](https://github.com/nodejs/node/commit/0cd4a52392)] - **doc**: improve child\_process.markdown wording (yorkie) [#4138](https://github.com/nodejs/node/pull/4138)
* \[[`fd5ed6888d`](https://github.com/nodejs/node/commit/fd5ed6888d)] - **doc**: add JungMinu to collaborators (Minwoo Jung) [#4143](https://github.com/nodejs/node/pull/4143)
* \[[`fa0cdf75d9`](https://github.com/nodejs/node/commit/fa0cdf75d9)] - **doc**: add iarna to collaborators (Rebecca Turner) [#4144](https://github.com/nodejs/node/pull/4144)
* \[[`424eb962b1`](https://github.com/nodejs/node/commit/424eb962b1)] - **doc**: add zkat to collaborators (Kat Marchán) [#4142](https://github.com/nodejs/node/pull/4142)
* \[[`85b601224b`](https://github.com/nodejs/node/commit/85b601224b)] - **doc**: add HTTP working group (James M Snell) [#3919](https://github.com/nodejs/node/pull/3919)
* \[[`f4164bd8df`](https://github.com/nodejs/node/commit/f4164bd8df)] - **doc**: update links to use https where possible (jpersson) [#4054](https://github.com/nodejs/node/pull/4054)
* \[[`3169eed1e3`](https://github.com/nodejs/node/commit/3169eed1e3)] - **doc**: add links and backticks around names (jpersson) [#4054](https://github.com/nodejs/node/pull/4054)
* \[[`f3417e2574`](https://github.com/nodejs/node/commit/f3417e2574)] - **doc**: standardize references to node.js in docs (Scott Buchanan) [#4136](https://github.com/nodejs/node/pull/4136)
* \[[`95dd60c657`](https://github.com/nodejs/node/commit/95dd60c657)] - **doc**: reword https.Agent example text (Jan Krems) [#4075](https://github.com/nodejs/node/pull/4075)
* \[[`c61237d3ea`](https://github.com/nodejs/node/commit/c61237d3ea)] - **doc**: fix internal link to child.send() (Luigi Pinca) [#4089](https://github.com/nodejs/node/pull/4089)
* \[[`aaeced915e`](https://github.com/nodejs/node/commit/aaeced915e)] - **doc**: fix the exception description (yorkie) [#3658](https://github.com/nodejs/node/pull/3658)
* \[[`a2b7596ac0`](https://github.com/nodejs/node/commit/a2b7596ac0)] - **doc**: fix color of linked code blocks (jpersson) [#4068](https://github.com/nodejs/node/pull/4068)
* \[[`f3c50f5fb5`](https://github.com/nodejs/node/commit/f3c50f5fb5)] - **doc**: fix rare case of misaligned columns (Roman Reiss) [#3948](https://github.com/nodejs/node/pull/3948)
* \[[`f0a2e2cdec`](https://github.com/nodejs/node/commit/f0a2e2cdec)] - **doc**: message.header duplication correction (Bryan English) [#3997](https://github.com/nodejs/node/pull/3997)
* \[[`b1dfa8bebb`](https://github.com/nodejs/node/commit/b1dfa8bebb)] - **doc**: fix typo in README (Rich Trott) [#4000](https://github.com/nodejs/node/pull/4000)
* \[[`4602e01221`](https://github.com/nodejs/node/commit/4602e01221)] - **doc**: replace sane with reasonable (Lewis Cowper) [#3980](https://github.com/nodejs/node/pull/3980)
* \[[`4849a54386`](https://github.com/nodejs/node/commit/4849a54386)] - **doc**: Adding best practises for crypto.pbkdf2 (Tom Gallacher) [#3290](https://github.com/nodejs/node/pull/3290)
* \[[`77251d99de`](https://github.com/nodejs/node/commit/77251d99de)] - **doc**: numeric flags to fs.open (Carl Lei) [#3641](https://github.com/nodejs/node/pull/3641)
* \[[`f4ca007b42`](https://github.com/nodejs/node/commit/f4ca007b42)] - **doc**: clarify that fs streams expect blocking fd (Carl Lei) [#3641](https://github.com/nodejs/node/pull/3641)
* \[[`26eeae8016`](https://github.com/nodejs/node/commit/26eeae8016)] - **doc**: fix broken references (Alexander Gromnitsky) [#3944](https://github.com/nodejs/node/pull/3944)
* \[[`f90227b0e8`](https://github.com/nodejs/node/commit/f90227b0e8)] - **doc**: move fs.existsSync() deprecation message (Martin Forsberg) [#3942](https://github.com/nodejs/node/pull/3942)
* \[[`bbcb2a2e65`](https://github.com/nodejs/node/commit/bbcb2a2e65)] - **doc**: clarify module loading behavior (cjihrig) [#3920](https://github.com/nodejs/node/pull/3920)
* \[[`0997178037`](https://github.com/nodejs/node/commit/0997178037)] - **doc**: add reference for buffer.inspect() (cjihrig) [#3921](https://github.com/nodejs/node/pull/3921)
* \[[`6c16c40283`](https://github.com/nodejs/node/commit/6c16c40283)] - **doc**: clarify v5.1.1 notable items (Rod Vagg) [#4156](https://github.com/nodejs/node/pull/4156)
* \[[`4c8800c2de`](https://github.com/nodejs/node/commit/4c8800c2de)] - **fs,doc**: use `target` instead of `destination` (yorkie) [#3912](https://github.com/nodejs/node/pull/3912)
* \[[`1f0e8dca8e`](https://github.com/nodejs/node/commit/1f0e8dca8e)] - **installer**: install the tick processor (Matt Loring) [#3032](https://github.com/nodejs/node/pull/3032)
* \[[`e8e4e0718b`](https://github.com/nodejs/node/commit/e8e4e0718b)] - **meta**: remove use of profanity in source (Myles Borins) [#4122](https://github.com/nodejs/node/pull/4122)
* \[[`13834caa28`](https://github.com/nodejs/node/commit/13834caa28)] - **module**: fix column offsets in errors (Tristian Flanagan) [#2867](https://github.com/nodejs/node/pull/2867)
* \[[`8988e1e117`](https://github.com/nodejs/node/commit/8988e1e117)] - **module,repl**: remove repl require() hack (Ben Noordhuis) [#4026](https://github.com/nodejs/node/pull/4026)
* \[[`baac81d95f`](https://github.com/nodejs/node/commit/baac81d95f)] - **net**: add local address/port for better errors (Jan Schär) [#3946](https://github.com/nodejs/node/pull/3946)
* \[[`12754c5dc3`](https://github.com/nodejs/node/commit/12754c5dc3)] - **net**: small code cleanup (Jan Schär) [#3943](https://github.com/nodejs/node/pull/3943)
* \[[`8a5e4345fd`](https://github.com/nodejs/node/commit/8a5e4345fd)] - **node**: s/doNTCallbackX/nextTickCallbackWithXArgs/ (Rod Vagg) [#4167](https://github.com/nodejs/node/pull/4167)
* \[[`0869ef3c55`](https://github.com/nodejs/node/commit/0869ef3c55)] - **(SEMVER-MINOR)** **repl**: allow leading period in multiline input (Zirak) [#3835](https://github.com/nodejs/node/pull/3835)
* \[[`aaab108dfe`](https://github.com/nodejs/node/commit/aaab108dfe)] - **repl**: attach location info to syntax errors (cjihrig) [#4013](https://github.com/nodejs/node/pull/4013)
* \[[`b08126dc9d`](https://github.com/nodejs/node/commit/b08126dc9d)] - **src**: refactor vcbuild configure args creation (Rod Vagg) [#3399](https://github.com/nodejs/node/pull/3399)
* \[[`da3137d0c5`](https://github.com/nodejs/node/commit/da3137d0c5)] - **src**: don't print garbage errors (cjihrig) [#4112](https://github.com/nodejs/node/pull/4112)
* \[[`9e9346fa32`](https://github.com/nodejs/node/commit/9e9346fa32)] - **src**: use GetCurrentProcessId() for process.pid (Ben Noordhuis) [#4163](https://github.com/nodejs/node/pull/4163)
* \[[`d969c0965c`](https://github.com/nodejs/node/commit/d969c0965c)] - **src**: define Is\* util functions with macros (cjihrig) [#4118](https://github.com/nodejs/node/pull/4118)
* \[[`458facdf66`](https://github.com/nodejs/node/commit/458facdf66)] - **src**: define getpid() based on OS (cjihrig) [#4146](https://github.com/nodejs/node/pull/4146)
* \[[`7e18f2ec62`](https://github.com/nodejs/node/commit/7e18f2ec62)] - **(SEMVER-MINOR)** **src**: add BE support to StringBytes::Encode() (Bryon Leung) [#3410](https://github.com/nodejs/node/pull/3410)
* \[[`756ab9caad`](https://github.com/nodejs/node/commit/756ab9caad)] - **stream**: be less eager with readable flag (Brian White) [#4141](https://github.com/nodejs/node/pull/4141)
* \[[`8f845ba28a`](https://github.com/nodejs/node/commit/8f845ba28a)] - **stream\_wrap**: error if stream has StringDecoder (Fedor Indutny) [#4031](https://github.com/nodejs/node/pull/4031)
* \[[`1c1af81ea0`](https://github.com/nodejs/node/commit/1c1af81ea0)] - **streams**: update .readable/.writable to false (Brian White) [#4083](https://github.com/nodejs/node/pull/4083)
* \[[`1d50819c85`](https://github.com/nodejs/node/commit/1d50819c85)] - **test**: check range fix for slowToString (Sakthipriyan Vairamani) [#4019](https://github.com/nodejs/node/pull/4019)
* \[[`0c2a0dc859`](https://github.com/nodejs/node/commit/0c2a0dc859)] - **test**: skip long path tests on non-Windows (Rafał Pocztarski) [#4116](https://github.com/nodejs/node/pull/4116)
* \[[`8a60aa1303`](https://github.com/nodejs/node/commit/8a60aa1303)] - **test**: don't check the # of chunks in test-http-1.0 (Santiago Gimeno) [#3961](https://github.com/nodejs/node/pull/3961)
* \[[`e84aeec883`](https://github.com/nodejs/node/commit/e84aeec883)] - **test**: mark test-cluster-shared-leak flaky (Rich Trott) [#4162](https://github.com/nodejs/node/pull/4162)
* \[[`b3f3b2e157`](https://github.com/nodejs/node/commit/b3f3b2e157)] - **test**: fix cluster-worker-isdead (Santiago Gimeno) [#3954](https://github.com/nodejs/node/pull/3954)
* \[[`da6be4d31a`](https://github.com/nodejs/node/commit/da6be4d31a)] - **test**: fix time resolution constraint (Gireesh Punathil) [#3981](https://github.com/nodejs/node/pull/3981)
* \[[`9d16729b20`](https://github.com/nodejs/node/commit/9d16729b20)] - **test**: skip instead of fail when mem constrained (Michael Cornacchia) [#3697](https://github.com/nodejs/node/pull/3697)
* \[[`be41eb751b`](https://github.com/nodejs/node/commit/be41eb751b)] - **test**: refactor test-http-exit-delay (Rich Trott) [#4055](https://github.com/nodejs/node/pull/4055)
* \[[`4b43bf0385`](https://github.com/nodejs/node/commit/4b43bf0385)] - **test**: fix flaky test-net-socket-local-address (Rich Trott) [#4109](https://github.com/nodejs/node/pull/4109)
* \[[`cb55c67a00`](https://github.com/nodejs/node/commit/cb55c67a00)] - **test**: improve cluster-disconnect-handles test (Brian White) [#4084](https://github.com/nodejs/node/pull/4084)
* \[[`2b5b127e14`](https://github.com/nodejs/node/commit/2b5b127e14)] - **test**: fix cluster-disconnect-handles flakiness (Santiago Gimeno) [#4009](https://github.com/nodejs/node/pull/4009)
* \[[`430264817b`](https://github.com/nodejs/node/commit/430264817b)] - **test**: add test for repl.defineCommand() (Bryan English) [#3908](https://github.com/nodejs/node/pull/3908)
* \[[`22b0971222`](https://github.com/nodejs/node/commit/22b0971222)] - **test**: eliminate multicast test FreeBSD flakiness (Rich Trott) [#4042](https://github.com/nodejs/node/pull/4042)
* \[[`c50003746b`](https://github.com/nodejs/node/commit/c50003746b)] - **test**: mark test flaky on FreeBSD (Rich Trott) [#4016](https://github.com/nodejs/node/pull/4016)
* \[[`69c95bbdb7`](https://github.com/nodejs/node/commit/69c95bbdb7)] - **test**: move ArrayStream to common (cjihrig) [#4027](https://github.com/nodejs/node/pull/4027)
* \[[`d94a70ec51`](https://github.com/nodejs/node/commit/d94a70ec51)] - **test**: fix test-domain-exit-dispose-again (Julien Gilli) [#3990](https://github.com/nodejs/node/pull/3990)
* \[[`00b839a2b8`](https://github.com/nodejs/node/commit/00b839a2b8)] - **test**: use platform-based timeout for reliability (Rich Trott) [#4015](https://github.com/nodejs/node/pull/4015)
* \[[`054a216b6f`](https://github.com/nodejs/node/commit/054a216b6f)] - **test**: mark cluster-net-send test flaky on windows (Rich Trott) [#4006](https://github.com/nodejs/node/pull/4006)
* \[[`d0621c5649`](https://github.com/nodejs/node/commit/d0621c5649)] - **test**: mark fork regression test flaky on windows (Rich Trott) [#4005](https://github.com/nodejs/node/pull/4005)
* \[[`19ed33df80`](https://github.com/nodejs/node/commit/19ed33df80)] - **test**: skip test if in FreeBSD jail (Rich Trott) [#3995](https://github.com/nodejs/node/pull/3995)
* \[[`a863e8d667`](https://github.com/nodejs/node/commit/a863e8d667)] - **test**: remove flaky status for cluster test (Rich Trott) [#3975](https://github.com/nodejs/node/pull/3975)
* \[[`dd0d15fc47`](https://github.com/nodejs/node/commit/dd0d15fc47)] - **test**: add TAP diagnostic message for retried tests (Rich Trott) [#3960](https://github.com/nodejs/node/pull/3960)
* \[[`1fe4d30efc`](https://github.com/nodejs/node/commit/1fe4d30efc)] - **test**: retry on smartos if ECONNREFUSED (Rich Trott) [#3941](https://github.com/nodejs/node/pull/3941)
* \[[`665a35d45e`](https://github.com/nodejs/node/commit/665a35d45e)] - **test**: address flaky test-http-client-timeout-event (Rich Trott) [#3968](https://github.com/nodejs/node/pull/3968)
* \[[`f9fe0aee53`](https://github.com/nodejs/node/commit/f9fe0aee53)] - **test**: numeric flags to fs.open (Carl Lei) [#3641](https://github.com/nodejs/node/pull/3641)
* \[[`54aafa17af`](https://github.com/nodejs/node/commit/54aafa17af)] - **test**: http complete list of non-concat headers (Bryan English) [#3930](https://github.com/nodejs/node/pull/3930)
* \[[`788541b40c`](https://github.com/nodejs/node/commit/788541b40c)] - **test**: fix race condition in unrefd interval test (Michael Cornacchia) [#3550](https://github.com/nodejs/node/pull/3550)
* \[[`e129d83996`](https://github.com/nodejs/node/commit/e129d83996)] - **test**: skip/replace weak crypto tests in FIPS mode (Stefan Budeanu) [#3757](https://github.com/nodejs/node/pull/3757)
* \[[`bc27379453`](https://github.com/nodejs/node/commit/bc27379453)] - **test**: avoid test timeouts on rpi (Stefan Budeanu) [#3902](https://github.com/nodejs/node/pull/3902)
* \[[`272732e76b`](https://github.com/nodejs/node/commit/272732e76b)] - **test**: fix flaky test-child-process-spawnsync-input (Rich Trott) [#3889](https://github.com/nodejs/node/pull/3889)
* \[[`781f8c0d1e`](https://github.com/nodejs/node/commit/781f8c0d1e)] - **test**: add OS X to module loading error test (Evan Lucas) [#3901](https://github.com/nodejs/node/pull/3901)
* \[[`f99c6363de`](https://github.com/nodejs/node/commit/f99c6363de)] - **test**: module loading error fix solaris #3798 (fansworld-claudio) [#3855](https://github.com/nodejs/node/pull/3855)
* \[[`1279adc756`](https://github.com/nodejs/node/commit/1279adc756)] - **timers**: optimize callback call: bind -> arrow (Andrei Sedoi) [#4038](https://github.com/nodejs/node/pull/4038)
* \[[`80f7f65464`](https://github.com/nodejs/node/commit/80f7f65464)] - **(SEMVER-MINOR)** **tls**: support reading multiple cas from one input (Ben Noordhuis) [#4099](https://github.com/nodejs/node/pull/4099)
* \[[`939f305d56`](https://github.com/nodejs/node/commit/939f305d56)] - **tls\_wrap**: slice buffer properly in `ClearOut` (Fedor Indutny) [#4184](https://github.com/nodejs/node/pull/4184)
* \[[`6d4a03d3d2`](https://github.com/nodejs/node/commit/6d4a03d3d2)] - **(SEMVER-MINOR)** **tools**: list missing whitespace/if-one-line cpplint (Ben Noordhuis) [#4099](https://github.com/nodejs/node/pull/4099)
* \[[`1c1c1a0f2b`](https://github.com/nodejs/node/commit/1c1c1a0f2b)] - **(SEMVER-MINOR)** **tools**: add --prof-process flag to node binary (Matt Loring) [#4021](https://github.com/nodejs/node/pull/4021)
* \[[`d7a7d3e6f7`](https://github.com/nodejs/node/commit/d7a7d3e6f7)] - **tools**: update certdata.txt (Ben Noordhuis) [#3951](https://github.com/nodejs/node/pull/3951)
* \[[`1b434e0654`](https://github.com/nodejs/node/commit/1b434e0654)] - **util**: determine object types in C++ (cjihrig) [#4100](https://github.com/nodejs/node/pull/4100)
* \[[`c93e2678f0`](https://github.com/nodejs/node/commit/c93e2678f0)] - **util**: fix constructor/instanceof checks (Brian White) [#3385](https://github.com/nodejs/node/pull/3385)
* \[[`098a3113e1`](https://github.com/nodejs/node/commit/098a3113e1)] - **util**: move .decorateErrorStack to internal/util (Ben Noordhuis) [#4026](https://github.com/nodejs/node/pull/4026)
* \[[`e68ea16c32`](https://github.com/nodejs/node/commit/e68ea16c32)] - **util**: add decorateErrorStack() (cjihrig) [#4013](https://github.com/nodejs/node/pull/4013)
* \[[`c584c3e08f`](https://github.com/nodejs/node/commit/c584c3e08f)] - **util,src**: allow lookup of hidden values (cjihrig) [#3988](https://github.com/nodejs/node/pull/3988)

<a id="5.1.1"></a>

## 2015-12-04, Version 5.1.1 (Stable), @rvagg

### Notable changes

* **http**: Fix CVE-2015-8027, a bug whereby an HTTP socket may no longer have a parser associated with it but a pipelined request attempts to trigger a pause or resume on the non-existent parser, a potential denial-of-service vulnerability. (Fedor Indutny)
* **openssl**: Upgrade to 1.0.2e, containing fixes for:
  * CVE-2015-3193 "BN\_mod\_exp may produce incorrect results on x86\_64", an attack may be possible against a Node.js TLS server using DHE key exchange. Details are available at <http://openssl.org/news/secadv/20151203.txt>.
  * CVE-2015-3194 "Certificate verify crash with missing PSS parameter", a potential denial-of-service vector for Node.js TLS servers using client certificate authentication; TLS clients are also impacted. Details are available at <http://openssl.org/news/secadv/20151203.txt>.
    (Shigeki Ohtsu) [#4134](https://github.com/nodejs/node/pull/4134)
* **v8**: Backport fix for CVE-2015-6764, a bug in `JSON.stringify()` that can result in out-of-bounds reads for arrays. (Ben Noordhuis)

### Known issues

* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).
* Unicode characters in filesystem paths are not handled consistently across platforms or Node.js APIs. See [#2088](https://github.com/nodejs/node/issues/2088), [#3401](https://github.com/nodejs/node/issues/3401) and [#3519](https://github.com/nodejs/node/issues/3519).

### Commits

* \[[`678398f250`](https://github.com/nodejs/node/commit/678398f250)] - **deps**: backport a7e50a5 from upstream v8 (Ben Noordhuis)
* \[[`76a552c938`](https://github.com/nodejs/node/commit/76a552c938)] - **deps**: backport 6df9a1d from upstream v8 (Ben Noordhuis)
* \[[`533881f889`](https://github.com/nodejs/node/commit/533881f889)] - **deps**: upgrade openssl sources to 1.0.2e (Shigeki Ohtsu) [#4134](https://github.com/nodejs/node/pull/4134)
* \[[`12e70fafd3`](https://github.com/nodejs/node/commit/12e70fafd3)] - **http**: fix pipeline regression (Fedor Indutny)

<a id="5.1.0"></a>

## 2015-11-17, Version 5.1.0 (Stable), @Fishrock123

### Notable changes

* **buffer**: The `noAssert` option for many buffer functions will now silently drop invalid write values rather than crashing (Minqi Pan) [#3767](https://github.com/nodejs/node/pull/3767).
  * This makes the behavior match what the docs suggest.
* **child\_process**: `child.send()` now properly returns a boolean like the docs suggest (Rich Trott) [#3577](https://github.com/nodejs/node/pull/3577).
* **doc**: All of the API docs have been re-ordered so as to read in alphabetical order (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662).
* **http\_parser**: update http-parser to 2.6.0 from 2.5.0 (James M Snell) [#3569](https://github.com/nodejs/node/pull/3569).
  * Now supports the following HTTP methods: `LINK`, `UNLINK`, `BIND`, `REBIND`, `UNBIND`.
  * Also added ACL and IPv6 Zone ID support.
* **npm**: upgrade npm to 3.3.12 from v3.3.6 (Rebecca Turner) [#3685](https://github.com/nodejs/node/pull/3685).
  * See the release notes for [v3.3.7](https://github.com/npm/npm/releases/tag/v3.3.7), [v3.3.8](https://github.com/npm/npm/releases/tag/v3.3.8), [v3.3.9](https://github.com/npm/npm/releases/tag/v3.3.9), [v3.3.10](https://github.com/npm/npm/releases/tag/v3.3.10), [v3.3.11](https://github.com/npm/npm/releases/tag/v3.3.11), and [v3.3.12](https://github.com/npm/npm/releases/tag/v3.3.12) for more details.
* **repl**: The REPL no longer crashes if the [persistent history](https://nodejs.org/api/repl.html#repl_persistent_history) file cannot be opened (Evan Lucas) [#3630](https://github.com/nodejs/node/pull/3630).
* **tls**: The default `sessionIdContext` now uses SHA1 in FIPS mode rather than MD5 (Stefan Budeanu) [#3755](https://github.com/nodejs/node/pull/3755).
* **v8**: Added some more useful post-mortem data (Fedor Indutny) [#3779](https://github.com/nodejs/node/pull/3779).

### Known issues

* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).
* Unicode characters in filesystem paths are not handled consistently across platforms or Node.js APIs. See [#2088](https://github.com/nodejs/node/issues/2088), [#3401](https://github.com/nodejs/node/issues/3401) and [#3519](https://github.com/nodejs/node/issues/3519).

### Commits

* \[[`b663d2bbb5`](https://github.com/nodejs/node/commit/b663d2bbb5)] - **async\_wrap**: call callback in destructor (Trevor Norris) [#3461](https://github.com/nodejs/node/pull/3461)
* \[[`eccbec99ea`](https://github.com/nodejs/node/commit/eccbec99ea)] - **async\_wrap**: new instances get uid (Trevor Norris) [#3461](https://github.com/nodejs/node/pull/3461)
* \[[`5d34c81a5c`](https://github.com/nodejs/node/commit/5d34c81a5c)] - **async\_wrap**: allow some hooks to be optional (Trevor Norris) [#3461](https://github.com/nodejs/node/pull/3461)
* \[[`7bff0138e2`](https://github.com/nodejs/node/commit/7bff0138e2)] - **buffer**: let WriteFloatGeneric silently drop values (Minqi Pan) [#3767](https://github.com/nodejs/node/pull/3767)
* \[[`56673693cd`](https://github.com/nodejs/node/commit/56673693cd)] - **buffer**: neuter external `nullptr` buffers (Fedor Indutny) [#3624](https://github.com/nodejs/node/pull/3624)
* \[[`2d0ca0293a`](https://github.com/nodejs/node/commit/2d0ca0293a)] - **build**: fix configuring with prebuilt libraries (Markus Tzoe) [#3135](https://github.com/nodejs/node/pull/3135)
* \[[`2a69b6820f`](https://github.com/nodejs/node/commit/2a69b6820f)] - **build**: fix --with-intl=system-icu for x-compile (Steven R. Loomis) [#3808](https://github.com/nodejs/node/pull/3808)
* \[[`8f5a2550a7`](https://github.com/nodejs/node/commit/8f5a2550a7)] - **build**: omit -gline-tables-only for --enable-asan (Ben Noordhuis) [#3680](https://github.com/nodejs/node/pull/3680)
* \[[`84bb74547d`](https://github.com/nodejs/node/commit/84bb74547d)] - **child\_process**: add safety checks on stdio access (cjihrig) [#3799](https://github.com/nodejs/node/pull/3799)
* \[[`e888471a11`](https://github.com/nodejs/node/commit/e888471a11)] - **child\_process**: don't fork bomb ourselves from -e (Ben Noordhuis) [#3575](https://github.com/nodejs/node/pull/3575)
* \[[`47f3735e88`](https://github.com/nodejs/node/commit/47f3735e88)] - **cluster**: send suicide message on disconnect (cjihrig) [#3720](https://github.com/nodejs/node/pull/3720)
* \[[`d64a56cba5`](https://github.com/nodejs/node/commit/d64a56cba5)] - **cluster**: remove handles when disconnecting worker (Ben Noordhuis) [#3677](https://github.com/nodejs/node/pull/3677)
* \[[`5ed30da5a0`](https://github.com/nodejs/node/commit/5ed30da5a0)] - **console**: use 'label' argument for time and timeEnd (Roman Reiss) [#3590](https://github.com/nodejs/node/pull/3590)
* \[[`7a290abea6`](https://github.com/nodejs/node/commit/7a290abea6)] - **crypto**: DSA parameter validation in FIPS mode (Stefan Budeanu) [#3756](https://github.com/nodejs/node/pull/3756)
* \[[`2c9fb147be`](https://github.com/nodejs/node/commit/2c9fb147be)] - **crypto**: Improve error checking and reporting (Stefan Budeanu) [#3753](https://github.com/nodejs/node/pull/3753)
* \[[`66dccaf0cd`](https://github.com/nodejs/node/commit/66dccaf0cd)] - **debugger**: also exit when the repl emits 'exit' (Felix Böhm) [#2369](https://github.com/nodejs/node/pull/2369)
* \[[`fd0253be4d`](https://github.com/nodejs/node/commit/fd0253be4d)] - **deps**: backport bc2e393 from v8 upstream (evan.lucas) [#3792](https://github.com/nodejs/node/pull/3792)
* \[[`59077acc3d`](https://github.com/nodejs/node/commit/59077acc3d)] - **deps**: cherry-pick 68e89fb from v8's upstream (Fedor Indutny) [#3779](https://github.com/nodejs/node/pull/3779)
* \[[`9ef81ff5d3`](https://github.com/nodejs/node/commit/9ef81ff5d3)] - **deps**: update V8 to 4.6.85.31 (Michaël Zasso) [#3698](https://github.com/nodejs/node/pull/3698)
* \[[`b48dbf9fce`](https://github.com/nodejs/node/commit/b48dbf9fce)] - **deps**: upgrade npm to 3.3.12 (Rebecca Turner) [#3685](https://github.com/nodejs/node/pull/3685)
* \[[`7caeb14e11`](https://github.com/nodejs/node/commit/7caeb14e11)] - **(SEMVER-MINOR)** **deps**: update http-parser to 2.6.0 (James M Snell) [#3569](https://github.com/nodejs/node/pull/3569)
* \[[`08e0de59fa`](https://github.com/nodejs/node/commit/08e0de59fa)] - **deps**: upgrade npm to 3.3.10 (Rebecca Turner) [#3599](https://github.com/nodejs/node/pull/3599)
* \[[`ac9e4ffe8e`](https://github.com/nodejs/node/commit/ac9e4ffe8e)] - **dns**: prevent undefined values in results (Junliang Yan) [#3696](https://github.com/nodejs/node/pull/3696)
* \[[`ea67d870f4`](https://github.com/nodejs/node/commit/ea67d870f4)] - **doc**: document release types in readme (Rod Vagg) [#3482](https://github.com/nodejs/node/pull/3482)
* \[[`60d3daa65c`](https://github.com/nodejs/node/commit/60d3daa65c)] - **doc**: replace head of readme with updated text (Rod Vagg) [#3482](https://github.com/nodejs/node/pull/3482)
* \[[`df1fdba2ae`](https://github.com/nodejs/node/commit/df1fdba2ae)] - **doc**: sort repl alphabetically (Tristian Flanagan) [#3859](https://github.com/nodejs/node/pull/3859)
* \[[`7ecd5422c8`](https://github.com/nodejs/node/commit/7ecd5422c8)] - **doc**: address use of profanity in code of conduct (James M Snell) [#3827](https://github.com/nodejs/node/pull/3827)
* \[[`c2393d1f2a`](https://github.com/nodejs/node/commit/c2393d1f2a)] - **doc**: consistent reference-style links (Bryan English) [#3845](https://github.com/nodejs/node/pull/3845)
* \[[`96f53c6b02`](https://github.com/nodejs/node/commit/96f53c6b02)] - **doc**: add link to \[customizing util.inspect colors]. (Jesse McCarthy) [#3749](https://github.com/nodejs/node/pull/3749)
* \[[`132297d3f6`](https://github.com/nodejs/node/commit/132297d3f6)] - **doc**: Updated streams simplified constructor API (Tom Gallacher) [#3602](https://github.com/nodejs/node/pull/3602)
* \[[`d137f0fd28`](https://github.com/nodejs/node/commit/d137f0fd28)] - **doc**: add warning about Windows process groups (Roman Klauke) [#3681](https://github.com/nodejs/node/pull/3681)
* \[[`45ff31cf94`](https://github.com/nodejs/node/commit/45ff31cf94)] - **doc**: added what buf.copy returns (Manuel B) [#3555](https://github.com/nodejs/node/pull/3555)
* \[[`5d1faa28cb`](https://github.com/nodejs/node/commit/5d1faa28cb)] - **doc**: reword message.headers to indicate they are not read-only (Tristian Flanagan) [#3814](https://github.com/nodejs/node/pull/3814)
* \[[`25c3807051`](https://github.com/nodejs/node/commit/25c3807051)] - **doc**: clarify duplicate header handling (Bryan English) [#3810](https://github.com/nodejs/node/pull/3810)
* \[[`ae2d1ee302`](https://github.com/nodejs/node/commit/ae2d1ee302)] - **doc**: repl: add defineComand and displayPrompt (Bryan English) [#3765](https://github.com/nodejs/node/pull/3765)
* \[[`09e524d013`](https://github.com/nodejs/node/commit/09e524d013)] - **doc**: sort tls alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`7e60b81c81`](https://github.com/nodejs/node/commit/7e60b81c81)] - **doc**: sort stream alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`cd931a8a13`](https://github.com/nodejs/node/commit/cd931a8a13)] - **doc**: sort net alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`cfa8198af8`](https://github.com/nodejs/node/commit/cfa8198af8)] - **doc**: sort process alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`e1a512607a`](https://github.com/nodejs/node/commit/e1a512607a)] - **doc**: sort zlib alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`0996b97240`](https://github.com/nodejs/node/commit/0996b97240)] - **doc**: sort util alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`df07072b66`](https://github.com/nodejs/node/commit/df07072b66)] - **doc**: sort https alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`6e9d01c7d8`](https://github.com/nodejs/node/commit/6e9d01c7d8)] - **doc**: sort http alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`18da02fa0f`](https://github.com/nodejs/node/commit/18da02fa0f)] - **doc**: sort modules alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`29054ffc0c`](https://github.com/nodejs/node/commit/29054ffc0c)] - **doc**: sort readline alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`389ead37ef`](https://github.com/nodejs/node/commit/389ead37ef)] - **doc**: sort repl alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`d383d624de`](https://github.com/nodejs/node/commit/d383d624de)] - **doc**: sort string\_decoder alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`0d2262887c`](https://github.com/nodejs/node/commit/0d2262887c)] - **doc**: sort timers alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`14b5a95d03`](https://github.com/nodejs/node/commit/14b5a95d03)] - **doc**: sort tty alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`d4dda77e4a`](https://github.com/nodejs/node/commit/d4dda77e4a)] - **doc**: sort url alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`39b8259bd5`](https://github.com/nodejs/node/commit/39b8259bd5)] - **doc**: sort vm alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`d357b3090e`](https://github.com/nodejs/node/commit/d357b3090e)] - **doc**: sort querystring alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`1f56abaa98`](https://github.com/nodejs/node/commit/1f56abaa98)] - **doc**: sort punycode alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`bc63667456`](https://github.com/nodejs/node/commit/bc63667456)] - **doc**: sort path alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`22961e011c`](https://github.com/nodejs/node/commit/22961e011c)] - **doc**: sort os alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`4ba18489d3`](https://github.com/nodejs/node/commit/4ba18489d3)] - **doc**: sort globals alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`c3f5ea704f`](https://github.com/nodejs/node/commit/c3f5ea704f)] - **doc**: sort fs alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`ce3ac8dd1e`](https://github.com/nodejs/node/commit/ce3ac8dd1e)] - **doc**: sort events alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`63a78749b8`](https://github.com/nodejs/node/commit/63a78749b8)] - **doc**: sort errors alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`488326da8d`](https://github.com/nodejs/node/commit/488326da8d)] - **doc**: sort dgram alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`e1c357e881`](https://github.com/nodejs/node/commit/e1c357e881)] - **doc**: sort crypto alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`4118fd5794`](https://github.com/nodejs/node/commit/4118fd5794)] - **doc**: sort dns alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`3e046acc50`](https://github.com/nodejs/node/commit/3e046acc50)] - **doc**: sort console alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`05f1af7124`](https://github.com/nodejs/node/commit/05f1af7124)] - **doc**: sort cluster alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`5c30e5dada`](https://github.com/nodejs/node/commit/5c30e5dada)] - **doc**: sort child\_process alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`fb6a09cd0e`](https://github.com/nodejs/node/commit/fb6a09cd0e)] - **doc**: sort buffer alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`c7c05d8f02`](https://github.com/nodejs/node/commit/c7c05d8f02)] - **doc**: sort assert alphabetically (Tristian Flanagan) [#3662](https://github.com/nodejs/node/pull/3662)
* \[[`f2c2e53321`](https://github.com/nodejs/node/commit/f2c2e53321)] - **doc**: add note to util.isBuffer (Evan Lucas) [#3790](https://github.com/nodejs/node/pull/3790)
* \[[`35fb9f91eb`](https://github.com/nodejs/node/commit/35fb9f91eb)] - **doc**: Describe FIPSDIR environment variable (Stefan Budeanu) [#3752](https://github.com/nodejs/node/pull/3752)
* \[[`da911f158b`](https://github.com/nodejs/node/commit/da911f158b)] - **doc**: update lts description in the collaborator guide (James M Snell) [#3668](https://github.com/nodejs/node/pull/3668)
* \[[`597f8751d1`](https://github.com/nodejs/node/commit/597f8751d1)] - **doc**: add note on tls connection meta data methods (Tyler Henkel) [#3746](https://github.com/nodejs/node/pull/3746)
* \[[`a32d9e31dc`](https://github.com/nodejs/node/commit/a32d9e31dc)] - **doc**: add romankl to collaborators (Roman Klauke) [#3725](https://github.com/nodejs/node/pull/3725)
* \[[`e5b9109d12`](https://github.com/nodejs/node/commit/e5b9109d12)] - **doc**: add thealphanerd to collaborators (Myles Borins) [#3723](https://github.com/nodejs/node/pull/3723)
* \[[`a05a0b47e3`](https://github.com/nodejs/node/commit/a05a0b47e3)] - **doc**: add saghul as a collaborator (Saúl Ibarra Corretgé) [#3724](https://github.com/nodejs/node/pull/3724)
* \[[`b14d9c5f16`](https://github.com/nodejs/node/commit/b14d9c5f16)] - **doc**: add method links in events.markdown (Alejandro Oviedo) [#3187](https://github.com/nodejs/node/pull/3187)
* \[[`44f779b112`](https://github.com/nodejs/node/commit/44f779b112)] - **doc**: add caveats of algs and key size in crypto (Shigeki Ohtsu) [#3479](https://github.com/nodejs/node/pull/3479)
* \[[`a0db5fb355`](https://github.com/nodejs/node/commit/a0db5fb355)] - **doc**: stdout/stderr can block when directed to file (Ben Noordhuis) [#3170](https://github.com/nodejs/node/pull/3170)
* \[[`409f29972e`](https://github.com/nodejs/node/commit/409f29972e)] - **doc**: rename iojs-\* groups to nodejs-\* (Steven R. Loomis) [#3634](https://github.com/nodejs/node/pull/3634)
* \[[`801866280e`](https://github.com/nodejs/node/commit/801866280e)] - **doc**: fix wrong date and known issue in changelog.md (James M Snell) [#3650](https://github.com/nodejs/node/pull/3650)
* \[[`325c4c7af5`](https://github.com/nodejs/node/commit/325c4c7af5)] - **doc**: fix function param order in assert doc (David Woods) [#3533](https://github.com/nodejs/node/pull/3533)
* \[[`045e04e531`](https://github.com/nodejs/node/commit/045e04e531)] - **doc**: typo fix in readme.md (Sam P Gallagher-Bishop) [#3649](https://github.com/nodejs/node/pull/3649)
* \[[`7fd8f1371e`](https://github.com/nodejs/node/commit/7fd8f1371e)] - **doc**: add note about timeout delay > TIMEOUT\_MAX (Guilherme Souza) [#3512](https://github.com/nodejs/node/pull/3512)
* \[[`7d0b589644`](https://github.com/nodejs/node/commit/7d0b589644)] - **doc**: fix crypto spkac function descriptions (Jason Gerfen) [#3614](https://github.com/nodejs/node/pull/3614)
* \[[`efa19bdcb5`](https://github.com/nodejs/node/commit/efa19bdcb5)] - **doc**: add final full stop in CONTRIBUTING.md (Emily Aviva Kapor-Mater) [#3576](https://github.com/nodejs/node/pull/3576)
* \[[`90723afe32`](https://github.com/nodejs/node/commit/90723afe32)] - **doc**: made code spans more visible in the API docs (phijohns) [#3573](https://github.com/nodejs/node/pull/3573)
* \[[`530bb9144f`](https://github.com/nodejs/node/commit/530bb9144f)] - **docs**: improve discoverability of Code of Conduct (Ashley Williams) [#3774](https://github.com/nodejs/node/pull/3774)
* \[[`73e40f0327`](https://github.com/nodejs/node/commit/73e40f0327)] - **docs**: fs - change links to buffer encoding to Buffer class anchor (fansworld-claudio) [#2796](https://github.com/nodejs/node/pull/2796)
* \[[`7a84fa6c60`](https://github.com/nodejs/node/commit/7a84fa6c60)] - **docs**: fs - remove encoding list and link to buffer (fansworld-claudio) [#2796](https://github.com/nodejs/node/pull/2796)
* \[[`2aa6a6d998`](https://github.com/nodejs/node/commit/2aa6a6d998)] - **fs**: return null error on readFile() success (Zheng Chaoping) [#3740](https://github.com/nodejs/node/pull/3740)
* \[[`c96400c572`](https://github.com/nodejs/node/commit/c96400c572)] - **gitignore**: don't ignore 'debug' in deps/npm (Rebecca Turner) [#3599](https://github.com/nodejs/node/pull/3599)
* \[[`a7f28a098e`](https://github.com/nodejs/node/commit/a7f28a098e)] - **http**: remove unneeded cb check from setTimeout() (Ashok Suthar) [#3631](https://github.com/nodejs/node/pull/3631)
* \[[`d2b5dcb2de`](https://github.com/nodejs/node/commit/d2b5dcb2de)] - **lib**: return boolean from child.send() (Rich Trott) [#3577](https://github.com/nodejs/node/pull/3577)
* \[[`5c54fa0095`](https://github.com/nodejs/node/commit/5c54fa0095)] - **module**: cache regular expressions (Evan Lucas) [#3869](https://github.com/nodejs/node/pull/3869)
* \[[`89285db128`](https://github.com/nodejs/node/commit/89285db128)] - **module**: remove unnecessary JSON.stringify (Andres Suarez) [#3578](https://github.com/nodejs/node/pull/3578)
* \[[`fd3f0d8e6e`](https://github.com/nodejs/node/commit/fd3f0d8e6e)] - **querystring**: Parse multiple separator characters (Yosuke Furukawa) [#3807](https://github.com/nodejs/node/pull/3807)
* \[[`75dbafc3f8`](https://github.com/nodejs/node/commit/75dbafc3f8)] - **repl**: To exit, press ^C again or type .exit. (Hemanth.HM) [#3368](https://github.com/nodejs/node/pull/3368)
* \[[`5073da0481`](https://github.com/nodejs/node/commit/5073da0481)] - **repl**: don't crash if cannot open history file (Evan Lucas) [#3630](https://github.com/nodejs/node/pull/3630)
* \[[`59cd28114d`](https://github.com/nodejs/node/commit/59cd28114d)] - **src**: Add missing va\_end before return (Ömer Fadıl Usta) [#3565](https://github.com/nodejs/node/pull/3565)
* \[[`02e012e984`](https://github.com/nodejs/node/commit/02e012e984)] - **src**: force line buffering for stderr (Rich Trott) [#3701](https://github.com/nodejs/node/pull/3701)
* \[[`2498e29344`](https://github.com/nodejs/node/commit/2498e29344)] - **src**: Revert "nix stdin \_readableState.reading" (Roman Reiss) [#3490](https://github.com/nodejs/node/pull/3490)
* \[[`65cd03cda6`](https://github.com/nodejs/node/commit/65cd03cda6)] - **src**: wrap source before doing syntax check (Evan Lucas) [#3587](https://github.com/nodejs/node/pull/3587)
* \[[`d72bb1e96a`](https://github.com/nodejs/node/commit/d72bb1e96a)] - _**Revert**_ "**src**: fix stuck debugger process" (Ben Noordhuis) [#3585](https://github.com/nodejs/node/pull/3585)
* \[[`047abbd6eb`](https://github.com/nodejs/node/commit/047abbd6eb)] - **test**: move test-specific function out of common (Rich Trott) [#3871](https://github.com/nodejs/node/pull/3871)
* \[[`19a36ff355`](https://github.com/nodejs/node/commit/19a36ff355)] - **test**: fix flaky SmartOS test (Rich Trott) [#3830](https://github.com/nodejs/node/pull/3830)
* \[[`4bb27baf8d`](https://github.com/nodejs/node/commit/4bb27baf8d)] - **test**: skip test if FreeBSD jail will break it (Rich Trott) [#3839](https://github.com/nodejs/node/pull/3839)
* \[[`1c1e70864b`](https://github.com/nodejs/node/commit/1c1e70864b)] - **test**: fix path to module for repl test on Windows (Michael Cornacchia) [#3608](https://github.com/nodejs/node/pull/3608)
* \[[`413ca53107`](https://github.com/nodejs/node/commit/413ca53107)] - **test**: increase crypto strength for FIPS standard (Stefan Budeanu) [#3758](https://github.com/nodejs/node/pull/3758)
* \[[`2ec5e17d16`](https://github.com/nodejs/node/commit/2ec5e17d16)] - **test**: add test-zlib-flush-drain (Myles Borins) [#3534](https://github.com/nodejs/node/pull/3534)
* \[[`de707f0876`](https://github.com/nodejs/node/commit/de707f0876)] - **test**: add hasFipsCrypto to test/common.js (Stefan Budeanu) [#3756](https://github.com/nodejs/node/pull/3756)
* \[[`828b786e48`](https://github.com/nodejs/node/commit/828b786e48)] - **test**: add test for invalid DSA key size (Stefan Budeanu) [#3756](https://github.com/nodejs/node/pull/3756)
* \[[`252e810059`](https://github.com/nodejs/node/commit/252e810059)] - **test**: Fix test-cluster-worker-exit.js for AIX (Imran Iqbal) [#3666](https://github.com/nodejs/node/pull/3666)
* \[[`91248b1094`](https://github.com/nodejs/node/commit/91248b1094)] - **test**: run pipeline flood test in parallel (Rich Trott) [#3811](https://github.com/nodejs/node/pull/3811)
* \[[`583f58e5d6`](https://github.com/nodejs/node/commit/583f58e5d6)] - **test**: stronger crypto in test fixtures (Stefan Budeanu) [#3759](https://github.com/nodejs/node/pull/3759)
* \[[`2e67db3104`](https://github.com/nodejs/node/commit/2e67db3104)] - **test**: refactor test-http-pipeline-flood (Rich Trott) [#3636](https://github.com/nodejs/node/pull/3636)
* \[[`1ab59ab9b3`](https://github.com/nodejs/node/commit/1ab59ab9b3)] - **test**: fix flaky test test-http-pipeline-flood (Devin Nakamura) [#3636](https://github.com/nodejs/node/pull/3636)
* \[[`1c8a7c6351`](https://github.com/nodejs/node/commit/1c8a7c6351)] - **test**: enhance fs-watch-recursive test (Sakthipriyan Vairamani) [#2599](https://github.com/nodejs/node/pull/2599)
* \[[`81997840f2`](https://github.com/nodejs/node/commit/81997840f2)] - **test**: fix test-module-loading-error for musl (Hugues Malphettes) [#3657](https://github.com/nodejs/node/pull/3657)
* \[[`9cdceac782`](https://github.com/nodejs/node/commit/9cdceac782)] - **test**: use really invalid hostname (Sakthipriyan Vairamani) [#3711](https://github.com/nodejs/node/pull/3711)
* \[[`f3594e77b2`](https://github.com/nodejs/node/commit/f3594e77b2)] - **test**: fix test-net-persistent-keepalive for AIX (Imran Iqbal) [#3646](https://github.com/nodejs/node/pull/3646)
* \[[`81522480f1`](https://github.com/nodejs/node/commit/81522480f1)] - **test**: more regression tests for minDHSize option (Ben Noordhuis) [#3629](https://github.com/nodejs/node/pull/3629)
* \[[`935b97769e`](https://github.com/nodejs/node/commit/935b97769e)] - **test**: add regression test for 512 bits DH key (Ben Noordhuis) [#3629](https://github.com/nodejs/node/pull/3629)
* \[[`e302c33bb0`](https://github.com/nodejs/node/commit/e302c33bb0)] - **test**: mark http-pipeline-flood flaky (Rich Trott) [#3616](https://github.com/nodejs/node/pull/3616)
* \[[`5977963bce`](https://github.com/nodejs/node/commit/5977963bce)] - **test**: remove flaky designation from ls-no-sslv3 (Rich Trott) [#3620](https://github.com/nodejs/node/pull/3620)
* \[[`1e98d90db8`](https://github.com/nodejs/node/commit/1e98d90db8)] - **test**: add regression test for --debug-brk -e 0 (Ben Noordhuis) [#3585](https://github.com/nodejs/node/pull/3585)
* \[[`2f16be2b70`](https://github.com/nodejs/node/commit/2f16be2b70)] - **tls**: Use SHA1 for sessionIdContext in FIPS mode (Stefan Budeanu) [#3755](https://github.com/nodejs/node/pull/3755)
* \[[`05f0549b50`](https://github.com/nodejs/node/commit/05f0549b50)] - **tls**: copy client CAs and cert store on CertCb (Fedor Indutny) [#3537](https://github.com/nodejs/node/pull/3537)
* \[[`bea35424a2`](https://github.com/nodejs/node/commit/bea35424a2)] - **tools**: add tap output to cpplint (Johan Bergström) [#3448](https://github.com/nodejs/node/pull/3448)
* \[[`d036b35349`](https://github.com/nodejs/node/commit/d036b35349)] - **tools**: enforce `throw new Error()` with lint rule (Rich Trott) [#3714](https://github.com/nodejs/node/pull/3714)
* \[[`38bb0d864e`](https://github.com/nodejs/node/commit/38bb0d864e)] - **tools**: Use `throw new Error()` consistently (Rich Trott) [#3714](https://github.com/nodejs/node/pull/3714)
* \[[`e40d28283a`](https://github.com/nodejs/node/commit/e40d28283a)] - **tools**: update npm test tooling for 3.3.10+ (Rebecca Turner) [#3599](https://github.com/nodejs/node/pull/3599)
* \[[`cbd358ce33`](https://github.com/nodejs/node/commit/cbd358ce33)] - **tools**: fix gyp to work on MacOSX without XCode (Shigeki Ohtsu) [iojs/io.js#1325](https://github.com/iojs/io.js/pull/1325)
* \[[`3137e46cb8`](https://github.com/nodejs/node/commit/3137e46cb8)] - **tools**: update gyp to b3cef02 (Imran Iqbal) [#3487](https://github.com/nodejs/node/pull/3487)
* \[[`d61cb90ee3`](https://github.com/nodejs/node/commit/d61cb90ee3)] - **util**: use Object.create(null) for dictionary object (Minwoo Jung) [#3831](https://github.com/nodejs/node/pull/3831)
* \[[`9a45c21e6c`](https://github.com/nodejs/node/commit/9a45c21e6c)] - **util**: use regexp instead of str.replace().join() (qinjia) [#3689](https://github.com/nodejs/node/pull/3689)
* \[[`33ffc62670`](https://github.com/nodejs/node/commit/33ffc62670)] - **zlib**: only apply drain listener if given callback (Craig Cavalier) [#3534](https://github.com/nodejs/node/pull/3534)
* \[[`d70deabf90`](https://github.com/nodejs/node/commit/d70deabf90)] - **zlib**: pass kind to recursive calls to flush (Myles Borins) [#3534](https://github.com/nodejs/node/pull/3534)

<a id="5.0.0"></a>

## 2015-10-29, Version 5.0.0 (Stable), @rvagg

### Notable Changes

* **buffer**: _(Breaking)_ Removed both `'raw'` and `'raws'` encoding types from `Buffer`, these have been deprecated for a long time (Sakthipriyan Vairamani) [#2859](https://github.com/nodejs/node/pull/2859).
* **console**: _(Breaking)_ Values reported by `console.time()` now have 3 decimals of accuracy added (Michaël Zasso) [#3166](https://github.com/nodejs/node/pull/3166).
* **fs**:
  * `fs.readFile*()`, `fs.writeFile*()`, and `fs.appendFile*()` now also accept a file descriptor as their first argument (Johannes Wüller) [#3163](https://github.com/nodejs/node/pull/3163).
  * _(Breaking)_ In `fs.readFile()`, if an encoding is specified and the internal `toString()` fails the error is no longer _thrown_ but is passed to the callback (Evan Lucas) [#3485](https://github.com/nodejs/node/pull/3485).
  * _(Breaking)_ In `fs.read()` (using the `fs.read(fd, length, position, encoding, callback)` form), if the internal `toString()` fails the error is no longer _thrown_ but is passed to the callback (Evan Lucas) [#3503](https://github.com/nodejs/node/pull/3503).
* **http**:
  * Fixed a bug where pipelined http requests would stall (Fedor Indutny) [#3342](https://github.com/nodejs/node/pull/3342).
  * _(Breaking)_ When parsing HTTP, don't add duplicates of the following headers: `Retry-After`, `ETag`, `Last-Modified`, `Server`, `Age`, `Expires`. This is in addition to the following headers which already block duplicates: `Content-Type`, `Content-Length`, `User-Agent`, `Referer`, `Host`, `Authorization`, `Proxy-Authorization`, `If-Modified-Since`, `If-Unmodified-Since`, `From`, `Location`, `Max-Forwards` (James M Snell) [#3090](https://github.com/nodejs/node/pull/3090).
  * _(Breaking)_ The `callback` argument to `OutgoingMessage#setTimeout()` must be a function or a `TypeError` is thrown (James M Snell) [#3090](https://github.com/nodejs/node/pull/3090).
  * _(Breaking)_ HTTP methods and header names must now conform to the RFC 2616 "token" rule, a list of allowed characters that excludes control characters and a number of _separator_ characters. Specifically, methods and header names must now match ``/^[a-zA-Z0-9_!#$%&'*+.^`|~-]+$/`` or a `TypeError` will be thrown (James M Snell) [#2526](https://github.com/nodejs/node/pull/2526).
* **node**:
  * _(Breaking)_ Deprecated the `_linklist` module (Rich Trott) [#3078](https://github.com/nodejs/node/pull/3078).
  * _(Breaking)_ Removed `require.paths` and `require.registerExtension()`, both had been previously set to throw `Error` when accessed (Sakthipriyan Vairamani) [#2922](https://github.com/nodejs/node/pull/2922).
* **npm**: Upgraded to version 3.3.6 from 2.14.7, see <https://github.com/npm/npm/releases/tag/v3.3.6> for more details. This is a major version bump for npm and it has seen a significant amount of change. Please see the original [npm v3.0.0 release notes](https://github.com/npm/npm/blob/master/CHANGELOG.md#v300-2015-06-25) for a list of major changes (Rebecca Turner) [#3310](https://github.com/nodejs/node/pull/3310).
* **src**: _(Breaking)_ Bumped `NODE_MODULE_VERSION` to `47` from `46`, this is necessary due to the V8 upgrade. Native add-ons will need to be recompiled (Rod Vagg) [#3400](https://github.com/nodejs/node/pull/3400).
* **timers**: Attempt to reuse the timer handle for `setTimeout().unref()`. This fixes a long-standing known issue where unrefed timers would perviously hold `beforeExit` open (Fedor Indutny) [#3407](https://github.com/nodejs/node/pull/3407).
* **tls**:
  * Added ALPN Support (Shigeki Ohtsu) [#2564](https://github.com/nodejs/node/pull/2564).
  * TLS options can now be passed in an object to `createSecurePair()` (Коренберг Марк) [#2441](https://github.com/nodejs/node/pull/2441).
  * _(Breaking)_ The default minimum DH key size for `tls.connect()` is now 1024 bits and a warning is shown when DH key size is less than 2048 bits. This a security consideration to prevent "logjam" attacks. A new `minDHSize` TLS option can be used to override the default. (Shigeki Ohtsu) [#1831](https://github.com/nodejs/node/pull/1831).
* **util**:
  * _(Breaking)_ `util.p()` was deprecated for years, and has now been removed (Wyatt Preul) [#3432](https://github.com/nodejs/node/pull/3432).
  * _(Breaking)_ `util.inherits()` can now work with ES6 classes. This is considered a breaking change because of potential subtle side-effects caused by a change from directly reassigning the prototype of the constructor using `ctor.prototype = Object.create(superCtor.prototype, { constructor: { ... } })` to using `Object.setPrototypeOf(ctor.prototype, superCtor.prototype)` (Michaël Zasso) [#3455](https://github.com/nodejs/node/pull/3455).
* **v8**: _(Breaking)_ Upgraded to 4.6.85.25 from 4.5.103.35  (Ali Ijaz Sheikh) [#3351](https://github.com/nodejs/node/pull/3351).
  * Implements the spread operator, see <https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/Spread_operator> for further information.
  * Implements `new.target`, see <https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/new.target> for further information.
* **zlib**: Decompression now throws on truncated input (e.g. unexpected end of file) (Yuval Brik) [#2595](https://github.com/nodejs/node/pull/2595).

### Known issues

* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).
* Unicode characters in filesystem paths are not handled consistently across platforms or Node.js APIs. See [#2088](https://github.com/nodejs/node/issues/2088), [#3401](https://github.com/nodejs/node/issues/3401) and [#3519](https://github.com/nodejs/node/issues/3519).

### Commits

* \[[`6a04cc0a43`](https://github.com/nodejs/node/commit/6a04cc0a43)] - **buffer**: fix value check for writeUInt{B,L}E (Trevor Norris) [#3500](https://github.com/nodejs/node/pull/3500)
* \[[`1a41feb559`](https://github.com/nodejs/node/commit/1a41feb559)] - **buffer**: don't CHECK on zero-sized realloc (Ben Noordhuis) [#3499](https://github.com/nodejs/node/pull/3499)
* \[[`5f6579d366`](https://github.com/nodejs/node/commit/5f6579d366)] - **(SEMVER-MAJOR)** **buffer**: remove raw & raws encoding (Sakthipriyan Vairamani) [#2859](https://github.com/nodejs/node/pull/2859)
* \[[`70fca2a81e`](https://github.com/nodejs/node/commit/70fca2a81e)] - **build**: Updates for AIX npm support - part 1 (Michael Dawson) [#3114](https://github.com/nodejs/node/pull/3114)
* \[[`b36b4f385a`](https://github.com/nodejs/node/commit/b36b4f385a)] - **build**: rectify --link-module help text (Minqi Pan) [#3379](https://github.com/nodejs/node/pull/3379)
* \[[`a89eeca590`](https://github.com/nodejs/node/commit/a89eeca590)] - **console**: rename argument of time and timeEnd (Michaël Zasso) [#3166](https://github.com/nodejs/node/pull/3166)
* \[[`870108aaa8`](https://github.com/nodejs/node/commit/870108aaa8)] - **(SEMVER-MAJOR)** **console**: sub-millisecond accuracy for console.time (Michaël Zasso) [#3166](https://github.com/nodejs/node/pull/3166)
* \[[`0a43697ce8`](https://github.com/nodejs/node/commit/0a43697ce8)] - **deps**: backport 010897c from V8 upstream (Ali Ijaz Sheikh) [#3520](https://github.com/nodejs/node/pull/3520)
* \[[`8c0318ce8d`](https://github.com/nodejs/node/commit/8c0318ce8d)] - **deps**: backport 8d6a228 from the v8's upstream (Fedor Indutny) [#3549](https://github.com/nodejs/node/pull/3549)
* \[[`2974debc6e`](https://github.com/nodejs/node/commit/2974debc6e)] - **deps**: update V8 to 4.6.85.28 (Michaël Zasso) [#3484](https://github.com/nodejs/node/pull/3484)
* \[[`f76af49b13`](https://github.com/nodejs/node/commit/f76af49b13)] - **deps**: fix upgrade to npm 3.3.6 (Rebecca Turner) [#3494](https://github.com/nodejs/node/pull/3494)
* \[[`32b51c97ec`](https://github.com/nodejs/node/commit/32b51c97ec)] - **deps**: upgrade npm to 3.3.6 (Rebecca Turner) [#3310](https://github.com/nodejs/node/pull/3310)
* \[[`770cd229f9`](https://github.com/nodejs/node/commit/770cd229f9)] - **deps**: upgrade V8 to 4.6.85.25 (Ali Ijaz Sheikh) [#3351](https://github.com/nodejs/node/pull/3351)
* \[[`972a0c8515`](https://github.com/nodejs/node/commit/972a0c8515)] - **deps**: backport 0d01728 from v8's upstream (Fedor Indutny) [#3351](https://github.com/nodejs/node/pull/3351)
* \[[`1fdec65203`](https://github.com/nodejs/node/commit/1fdec65203)] - **deps**: improve ArrayBuffer performance in v8 (Fedor Indutny) [#3351](https://github.com/nodejs/node/pull/3351)
* \[[`5cd1fd836a`](https://github.com/nodejs/node/commit/5cd1fd836a)] - **deps**: backport 56a0a79 from V8 upstream (Julien Gilli) [#3351](https://github.com/nodejs/node/pull/3351)
* \[[`7fb128d8df`](https://github.com/nodejs/node/commit/7fb128d8df)] - **deps**: cherry-pick backports to V8 (Michaël Zasso) [#3351](https://github.com/nodejs/node/pull/3351)
* \[[`d8011d1683`](https://github.com/nodejs/node/commit/d8011d1683)] - **(SEMVER-MAJOR)** **deps**: upgrade V8 to 4.6.85.23 (Michaël Zasso) [#3351](https://github.com/nodejs/node/pull/3351)
* \[[`a334ddc467`](https://github.com/nodejs/node/commit/a334ddc467)] - _**Revert**_ "**deps**: backport 03ef3cd from V8 upstream" (Ali Ijaz Sheikh) [#3237](https://github.com/nodejs/node/pull/3237)
* \[[`6fff47ffac`](https://github.com/nodejs/node/commit/6fff47ffac)] - **deps**: backport 03ef3cd from V8 upstream (Ali Ijaz Sheikh) [#3165](https://github.com/nodejs/node/pull/3165)
* \[[`680dda8023`](https://github.com/nodejs/node/commit/680dda8023)] - **dns**: remove nonexistant exports.ADNAME (Roman Reiss) [#3051](https://github.com/nodejs/node/pull/3051)
* \[[`239ad899a3`](https://github.com/nodejs/node/commit/239ad899a3)] - **doc**: add LTS info to COLLABORATOR\_GUIDE.md (Myles Borins) [#3442](https://github.com/nodejs/node/pull/3442)
* \[[`5e76587fdf`](https://github.com/nodejs/node/commit/5e76587fdf)] - **doc**: createServer's key option can be an array (Sakthipriyan Vairamani) [#3123](https://github.com/nodejs/node/pull/3123)
* \[[`0317c880da`](https://github.com/nodejs/node/commit/0317c880da)] - **doc**: add TSC meeting minutes 2015-10-21 (Rod Vagg) [#3480](https://github.com/nodejs/node/pull/3480)
* \[[`cd245b12e0`](https://github.com/nodejs/node/commit/cd245b12e0)] - **doc**: clarify API buffer.concat (Martii) [#3255](https://github.com/nodejs/node/pull/3255)
* \[[`ff9ef893fd`](https://github.com/nodejs/node/commit/ff9ef893fd)] - **doc**: add TSC meeting minutes 2015-10-14 (Rod Vagg) [#3463](https://github.com/nodejs/node/pull/3463)
* \[[`605c5a7754`](https://github.com/nodejs/node/commit/605c5a7754)] - **doc**: clarify the use of `option.detached` (Kyle Smith) [#3250](https://github.com/nodejs/node/pull/3250)
* \[[`cf75a175e5`](https://github.com/nodejs/node/commit/cf75a175e5)] - **doc**: more use-cases for promise events (Domenic Denicola) [#3438](https://github.com/nodejs/node/pull/3438)
* \[[`1b75d4bda3`](https://github.com/nodejs/node/commit/1b75d4bda3)] - **doc**: update WORKING\_GROUPS.md - add missing groups (Michael Dawson) [#3450](https://github.com/nodejs/node/pull/3450)
* \[[`c658de2f99`](https://github.com/nodejs/node/commit/c658de2f99)] - **doc**: add TSC meeting minutes 2015-09-30 (Rod Vagg) [#3235](https://github.com/nodejs/node/pull/3235)
* \[[`d0b8c5d3a4`](https://github.com/nodejs/node/commit/d0b8c5d3a4)] - **doc**: add TSC meeting minutes 2015-10-07 (Rod Vagg) [#3364](https://github.com/nodejs/node/pull/3364)
* \[[`b483afcb20`](https://github.com/nodejs/node/commit/b483afcb20)] - **doc**: binary encoding is not deprecated (Trevor Norris) [#3441](https://github.com/nodejs/node/pull/3441)
* \[[`b607366a1c`](https://github.com/nodejs/node/commit/b607366a1c)] - **doc**: add information about Assert behavior and maintenance (Rich Trott) [#3330](https://github.com/nodejs/node/pull/3330)
* \[[`086103b32e`](https://github.com/nodejs/node/commit/086103b32e)] - **doc**: show keylen in pbkdf2 as a byte length (calebboyd) [#3334](https://github.com/nodejs/node/pull/3334)
* \[[`f6ebc8277b`](https://github.com/nodejs/node/commit/f6ebc8277b)] - **doc**: reword description of console.time (Michaël Zasso) [#3166](https://github.com/nodejs/node/pull/3166)
* \[[`503f279527`](https://github.com/nodejs/node/commit/503f279527)] - **doc**: fix indent in tls resumption example (Roman Reiss) [#3372](https://github.com/nodejs/node/pull/3372)
* \[[`dae9fae0fe`](https://github.com/nodejs/node/commit/dae9fae0fe)] - **doc**: label v4.2.1 as LTS in changelog heading (Phillip Johnsen) [#3360](https://github.com/nodejs/node/pull/3360)
* \[[`4fc638804c`](https://github.com/nodejs/node/commit/4fc638804c)] - **doc**: update V8 options in man page (Michaël Zasso) [#3351](https://github.com/nodejs/node/pull/3351)
* \[[`a441aa6e1d`](https://github.com/nodejs/node/commit/a441aa6e1d)] - **doc**: update WORKING\_GROUPS.md to include Intl (Steven R. Loomis) [#3251](https://github.com/nodejs/node/pull/3251)
* \[[`81503e597b`](https://github.com/nodejs/node/commit/81503e597b)] - **doc**: fix typo in changelog (Timothy Gu) [#3353](https://github.com/nodejs/node/pull/3353)
* \[[`3ef2e4acf3`](https://github.com/nodejs/node/commit/3ef2e4acf3)] - **doc**: fix typos in changelog (reggi) [#3291](https://github.com/nodejs/node/pull/3291)
* \[[`b9279aa193`](https://github.com/nodejs/node/commit/b9279aa193)] - **doc**: remove old note, 'cluster' is marked stable (Balázs Galambosi) [#3314](https://github.com/nodejs/node/pull/3314)
* \[[`cdfa271164`](https://github.com/nodejs/node/commit/cdfa271164)] - **doc**: update AUTHORS list (Rod Vagg)
* \[[`47b06f6bb1`](https://github.com/nodejs/node/commit/47b06f6bb1)] - **docs**: add missing shell option to execSync (fansworld-claudio) [#3440](https://github.com/nodejs/node/pull/3440)
* \[[`4c9abbd1bb`](https://github.com/nodejs/node/commit/4c9abbd1bb)] - **fs**: reduced duplicate code in fs.write() (ronkorving) [#2947](https://github.com/nodejs/node/pull/2947)
* \[[`2bb147535e`](https://github.com/nodejs/node/commit/2bb147535e)] - **(SEMVER-MAJOR)** **fs**: don't throw in read if buffer too big (Evan Lucas) [#3503](https://github.com/nodejs/node/pull/3503)
* \[[`7added3b39`](https://github.com/nodejs/node/commit/7added3b39)] - **(SEMVER-MAJOR)** **fs**: pass err to callback if buffer is too big (Evan Lucas) [#3485](https://github.com/nodejs/node/pull/3485)
* \[[`5e0759f6fd`](https://github.com/nodejs/node/commit/5e0759f6fd)] - **(SEMVER-MINOR)** **fs**: add file descriptor support to \*File() funcs (Johannes Wüller) [#3163](https://github.com/nodejs/node/pull/3163)
* \[[`d1a2e5357e`](https://github.com/nodejs/node/commit/d1a2e5357e)] - **gitignore**: don't ignore debug source directory in V8 (Michaël Zasso) [#3351](https://github.com/nodejs/node/pull/3351)
* \[[`ab03635fb1`](https://github.com/nodejs/node/commit/ab03635fb1)] - **http**: fix stalled pipeline bug (Fedor Indutny) [#3342](https://github.com/nodejs/node/pull/3342)
* \[[`e655a437b3`](https://github.com/nodejs/node/commit/e655a437b3)] - **(SEMVER-MAJOR)** **http**: do not allow multiple instances of certain response headers (James M Snell) [#3090](https://github.com/nodejs/node/pull/3090)
* \[[`0094a8dad7`](https://github.com/nodejs/node/commit/0094a8dad7)] - **(SEMVER-MAJOR)** **http**: add callback is function check (James M Snell) [#3090](https://github.com/nodejs/node/pull/3090)
* \[[`6192c9892f`](https://github.com/nodejs/node/commit/6192c9892f)] - **(SEMVER-MAJOR)** **http**: add checkIsHttpToken check for header fields (James M Snell) [#2526](https://github.com/nodejs/node/pull/2526)
* \[[`c9786bb680`](https://github.com/nodejs/node/commit/c9786bb680)] - **(SEMVER-MAJOR)** http{s}: don't connect to localhost on invalid URL (Sakthipriyan Vairamani) [#2967](https://github.com/nodejs/node/pull/2967)
* \[[`1929d5be73`](https://github.com/nodejs/node/commit/1929d5be73)] - **lib**: fix cluster handle leak (Rich Trott) [#3510](https://github.com/nodejs/node/pull/3510)
* \[[`97d081709e`](https://github.com/nodejs/node/commit/97d081709e)] - **lib**: avoid REPL exit on completion error (Rich Trott) [#3358](https://github.com/nodejs/node/pull/3358)
* \[[`f236b3a904`](https://github.com/nodejs/node/commit/f236b3a904)] - **(SEMVER-MINOR)** **lib,doc**: return boolean from child.send() (Rich Trott) [#3516](https://github.com/nodejs/node/pull/3516)
* \[[`6e887cc630`](https://github.com/nodejs/node/commit/6e887cc630)] - **lib,test**: update let to const where applicable (Sakthipriyan Vairamani) [#3152](https://github.com/nodejs/node/pull/3152)
* \[[`47befffc53`](https://github.com/nodejs/node/commit/47befffc53)] - **(SEMVER-MAJOR)** **lib,test**: deprecate \_linklist (Rich Trott) [#3078](https://github.com/nodejs/node/pull/3078)
* \[[`d5ce53458e`](https://github.com/nodejs/node/commit/d5ce53458e)] - **lttng**: update flags for gc tracing (Glen Keane) [#3388](https://github.com/nodejs/node/pull/3388)
* \[[`6ad458b752`](https://github.com/nodejs/node/commit/6ad458b752)] - **(SEMVER-MAJOR)** **module**: remove unnecessary property and method (Sakthipriyan Vairamani) [#2922](https://github.com/nodejs/node/pull/2922)
* \[[`ae196175f4`](https://github.com/nodejs/node/commit/ae196175f4)] - **node**: improve GetActiveRequests performance (Trevor Norris) [#3375](https://github.com/nodejs/node/pull/3375)
* \[[`bd4311bc9c`](https://github.com/nodejs/node/commit/bd4311bc9c)] - **repl**: handle comments properly (Sakthipriyan Vairamani) [#3515](https://github.com/nodejs/node/pull/3515)
* \[[`ce391ed849`](https://github.com/nodejs/node/commit/ce391ed849)] - **(SEMVER-MAJOR)** **repl**: event ordering: delay 'close' until 'flushHistory' (Jeremiah Senkpiel) [#3435](https://github.com/nodejs/node/pull/3435)
* \[[`4c80c02ac7`](https://github.com/nodejs/node/commit/4c80c02ac7)] - **repl**: limit persistent history correctly on load (Jeremiah Senkpiel) [#2356](https://github.com/nodejs/node/pull/2356)
* \[[`134a60c785`](https://github.com/nodejs/node/commit/134a60c785)] - **src**: fix race condition in debug signal on exit (Ben Noordhuis) [#3528](https://github.com/nodejs/node/pull/3528)
* \[[`bf7c3dabb4`](https://github.com/nodejs/node/commit/bf7c3dabb4)] - **(SEMVER-MAJOR)** **src**: bump NODE\_MODULE\_VERSION To 47 (Rod Vagg) [#3400](https://github.com/nodejs/node/pull/3400)
* \[[`2d3560767e`](https://github.com/nodejs/node/commit/2d3560767e)] - **src**: fix exception message encoding on Windows (Brian White) [#3288](https://github.com/nodejs/node/pull/3288)
* \[[`ff877e93e1`](https://github.com/nodejs/node/commit/ff877e93e1)] - **src**: fix stuck debugger process (Liang-Chi Hsieh) [#2778](https://github.com/nodejs/node/pull/2778)
* \[[`8854183fe5`](https://github.com/nodejs/node/commit/8854183fe5)] - **stream**: avoid unnecessary concat of a single buffer. (Calvin Metcalf) [#3300](https://github.com/nodejs/node/pull/3300)
* \[[`85b74de9de`](https://github.com/nodejs/node/commit/85b74de9de)] - **stream**: fix signature of \_write() in a comment (Fábio Santos) [#3248](https://github.com/nodejs/node/pull/3248)
* \[[`b8cea49c88`](https://github.com/nodejs/node/commit/b8cea49c88)] - **test**: fix heap-profiler link error LNK1194 on win (Junliang Yan) [#3572](https://github.com/nodejs/node/pull/3572)
* \[[`4a5dbeab43`](https://github.com/nodejs/node/commit/4a5dbeab43)] - **test**: fix missing unistd.h on windows (Junliang Yan) [#3532](https://github.com/nodejs/node/pull/3532)
* \[[`74e2328b3a`](https://github.com/nodejs/node/commit/74e2328b3a)] - **test**: split independent tests into separate files (Rich Trott) [#3548](https://github.com/nodejs/node/pull/3548)
* \[[`8c6c0f915a`](https://github.com/nodejs/node/commit/8c6c0f915a)] - **test**: use port number from env in tls socket test (Stefan Budeanu) [#3557](https://github.com/nodejs/node/pull/3557)
* \[[`1a968e67a5`](https://github.com/nodejs/node/commit/1a968e67a5)] - **test**: improve tests for util.inherits (Michaël Zasso) [#3507](https://github.com/nodejs/node/pull/3507)
* \[[`9d8d752456`](https://github.com/nodejs/node/commit/9d8d752456)] - **test**: print helpful err msg on test-dns-ipv6.js (Junliang Yan) [#3501](https://github.com/nodejs/node/pull/3501)
* \[[`60de9f8d7b`](https://github.com/nodejs/node/commit/60de9f8d7b)] - **test**: wrap assert.fail when passed to callback (Myles Borins) [#3453](https://github.com/nodejs/node/pull/3453)
* \[[`cd83f7ed7f`](https://github.com/nodejs/node/commit/cd83f7ed7f)] - **test**: add node::MakeCallback() test coverage (Ben Noordhuis) [#3478](https://github.com/nodejs/node/pull/3478)
* \[[`08da5c2a06`](https://github.com/nodejs/node/commit/08da5c2a06)] - **test**: disable test-tick-processor - aix and be ppc (Michael Dawson) [#3491](https://github.com/nodejs/node/pull/3491)
* \[[`7c35fbcb14`](https://github.com/nodejs/node/commit/7c35fbcb14)] - **test**: harden test-child-process-fork-regr-gh-2847 (Michael Dawson) [#3459](https://github.com/nodejs/node/pull/3459)
* \[[`ad2b272417`](https://github.com/nodejs/node/commit/ad2b272417)] - **test**: fix test-net-keepalive for AIX (Imran Iqbal) [#3458](https://github.com/nodejs/node/pull/3458)
* \[[`04fb14cc35`](https://github.com/nodejs/node/commit/04fb14cc35)] - **test**: fix flaky test-child-process-emfile (Rich Trott) [#3430](https://github.com/nodejs/node/pull/3430)
* \[[`eef0f0cd63`](https://github.com/nodejs/node/commit/eef0f0cd63)] - **test**: remove flaky status from eval\_messages test (Rich Trott) [#3420](https://github.com/nodejs/node/pull/3420)
* \[[`bbbd81eab2`](https://github.com/nodejs/node/commit/bbbd81eab2)] - **test**: skip test-dns-ipv6.js if ipv6 is unavailable (Junliang Yan) [#3444](https://github.com/nodejs/node/pull/3444)
* \[[`f78c8e7426`](https://github.com/nodejs/node/commit/f78c8e7426)] - **test**: fix flaky test for symlinks (Rich Trott) [#3418](https://github.com/nodejs/node/pull/3418)
* \[[`28e9a4f41b`](https://github.com/nodejs/node/commit/28e9a4f41b)] - **test**: repl-persistent-history is no longer flaky (Jeremiah Senkpiel) [#3437](https://github.com/nodejs/node/pull/3437)
* \[[`9e981556e5`](https://github.com/nodejs/node/commit/9e981556e5)] - **test**: cleanup, improve repl-persistent-history (Jeremiah Senkpiel) [#2356](https://github.com/nodejs/node/pull/2356)
* \[[`ee2e641e0a`](https://github.com/nodejs/node/commit/ee2e641e0a)] - **test**: add Symbol test for assert.deepEqual() (Rich Trott) [#3327](https://github.com/nodejs/node/pull/3327)
* \[[`e2b8393ee8`](https://github.com/nodejs/node/commit/e2b8393ee8)] - **test**: port domains regression test from v0.10 (Jonas Dohse) [#3356](https://github.com/nodejs/node/pull/3356)
* \[[`676e61872f`](https://github.com/nodejs/node/commit/676e61872f)] - **test**: apply correct assert.fail() arguments (Rich Trott) [#3378](https://github.com/nodejs/node/pull/3378)
* \[[`bbdbef9274`](https://github.com/nodejs/node/commit/bbdbef9274)] - **test**: fix tests after V8 upgrade (Michaël Zasso) [#3351](https://github.com/nodejs/node/pull/3351)
* \[[`6c032a8333`](https://github.com/nodejs/node/commit/6c032a8333)] - **test**: replace util with backtick strings (Myles Borins) [#3359](https://github.com/nodejs/node/pull/3359)
* \[[`f45c315763`](https://github.com/nodejs/node/commit/f45c315763)] - **test**: fix domain with abort-on-uncaught on PPC (Julien Gilli) [#3354](https://github.com/nodejs/node/pull/3354)
* \[[`e3d9d25083`](https://github.com/nodejs/node/commit/e3d9d25083)] - **test**: add test-child-process-emfile fail message (Rich Trott) [#3335](https://github.com/nodejs/node/pull/3335)
* \[[`6f14b3a7db`](https://github.com/nodejs/node/commit/6f14b3a7db)] - **test**: remove util from common (Rich Trott) [#3324](https://github.com/nodejs/node/pull/3324)
* \[[`7d94611ac9`](https://github.com/nodejs/node/commit/7d94611ac9)] - **test**: split up buffer tests for reliability (Rich Trott) [#3323](https://github.com/nodejs/node/pull/3323)
* \[[`3202456baa`](https://github.com/nodejs/node/commit/3202456baa)] - **test**: remove util properties from common (Rich Trott) [#3304](https://github.com/nodejs/node/pull/3304)
* \[[`31c971d641`](https://github.com/nodejs/node/commit/31c971d641)] - **test**: parallelize long-running test (Rich Trott) [#3287](https://github.com/nodejs/node/pull/3287)
* \[[`5bbc6df7de`](https://github.com/nodejs/node/commit/5bbc6df7de)] - **test**: change call to deprecated util.isError() (Rich Trott) [#3084](https://github.com/nodejs/node/pull/3084)
* \[[`522e3d3cd3`](https://github.com/nodejs/node/commit/522e3d3cd3)] - **timers**: reuse timer in `setTimeout().unref()` (Fedor Indutny) [#3407](https://github.com/nodejs/node/pull/3407)
* \[[`b64ce5960f`](https://github.com/nodejs/node/commit/b64ce5960f)] - **tls**: remove util and calls to util.format (Myles Borins) [#3456](https://github.com/nodejs/node/pull/3456)
* \[[`c64af7d99e`](https://github.com/nodejs/node/commit/c64af7d99e)] - **tls**: TLSSocket options default isServer false (Yuval Brik) [#2614](https://github.com/nodejs/node/pull/2614)
* \[[`2296a4fc0f`](https://github.com/nodejs/node/commit/2296a4fc0f)] - **(SEMVER-MINOR)** **tls**: add `options` argument to createSecurePair (Коренберг Марк) [#2441](https://github.com/nodejs/node/pull/2441)
* \[[`0140e1b5e3`](https://github.com/nodejs/node/commit/0140e1b5e3)] - **tls**: output warning of setDHParam to console.trace (Shigeki Ohtsu) [#1831](https://github.com/nodejs/node/pull/1831)
* \[[`f72e178a78`](https://github.com/nodejs/node/commit/f72e178a78)] - **(SEMVER-MAJOR)** **tls**: add minDHSize option to tls.connect() (Shigeki Ohtsu) [#1831](https://github.com/nodejs/node/pull/1831)
* \[[`6d92ebac11`](https://github.com/nodejs/node/commit/6d92ebac11)] - **tls**: add TLSSocket.getEphemeralKeyInfo() (Shigeki Ohtsu) [#1831](https://github.com/nodejs/node/pull/1831)
* \[[`62ad1d0113`](https://github.com/nodejs/node/commit/62ad1d0113)] - **(SEMVER-MINOR)** **tls, crypto**: add ALPN Support (Shigeki Ohtsu) [#2564](https://github.com/nodejs/node/pull/2564)
* \[[`5029f41b2f`](https://github.com/nodejs/node/commit/5029f41b2f)] - **(SEMVER-MINOR)** **tls,crypto**: move NPN protcol data to hidden value (Shigeki Ohtsu) [#2564](https://github.com/nodejs/node/pull/2564)
* \[[`701e38c25f`](https://github.com/nodejs/node/commit/701e38c25f)] - **tools**: enable prefer-const eslint rule (Sakthipriyan Vairamani) [#3152](https://github.com/nodejs/node/pull/3152)
* \[[`6e78382605`](https://github.com/nodejs/node/commit/6e78382605)] - **tools**: ensure npm always uses the local node (Jeremiah Senkpiel) [#3489](https://github.com/nodejs/node/pull/3489)
* \[[`3c3435d017`](https://github.com/nodejs/node/commit/3c3435d017)] - **tools**: update test-npm to work with npm 3 (Rebecca Turner) [#3489](https://github.com/nodejs/node/pull/3489)
* \[[`b4f4c24539`](https://github.com/nodejs/node/commit/b4f4c24539)] - **tools**: use absolute paths in test-npm (Rebecca Turner) [#3309](https://github.com/nodejs/node/pull/3309)
* \[[`80573153b8`](https://github.com/nodejs/node/commit/80573153b8)] - **(SEMVER-MAJOR)** **util**: make inherits work with classes (Michaël Zasso) [#3455](https://github.com/nodejs/node/pull/3455)
* \[[`412252ca04`](https://github.com/nodejs/node/commit/412252ca04)] - **(SEMVER-MAJOR)** **util**: Remove p, has been deprecated for years (Wyatt Preul) [#3432](https://github.com/nodejs/node/pull/3432)
* \[[`718c304a4f`](https://github.com/nodejs/node/commit/718c304a4f)] - **v8**: pull fix for builtin code size on PPC (Michael Dawson) [#3474](https://github.com/nodejs/node/pull/3474)
* \[[`6936468de2`](https://github.com/nodejs/node/commit/6936468de2)] - **vm**: remove Watchdog dependency on Environment (Ido Ben-Yair) [#3274](https://github.com/nodejs/node/pull/3274)
* \[[`80169b1f0a`](https://github.com/nodejs/node/commit/80169b1f0a)] - **(SEMVER-MAJOR)** **zlib**: decompression throw on truncated input (Yuval Brik) [#2595](https://github.com/nodejs/node/pull/2595)

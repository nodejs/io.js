# io.js ChangeLog

<!--lint disable maximum-line-length no-literal-urls prohibited-strings-->

<table>
<tr>
<th>v3</th>
<th>v2</th>
<th>v1</th>
</tr>
<tr>
<td valign="top">
<a href="#3.3.1">3.3.1</a><br/>
<a href="#3.3.0">3.3.0</a><br/>
<a href="#3.2.0">3.2.0</a><br/>
<a href="#3.1.0">3.1.0</a><br/>
<a href="#3.0.0">3.0.0</a><br/>
</td>
<td valign="top">
<a href="#2.5.0">2.5.0</a><br/>
<a href="#2.4.0">2.4.0</a><br/>
<a href="#2.3.4">2.3.4</a><br/>
<a href="#2.3.3">2.3.3</a><br/>
<a href="#2.3.2">2.3.2</a><br/>
<a href="#2.3.1">2.3.1</a><br/>
<a href="#2.3.0">2.3.0</a><br/>
<a href="#2.2.1">2.2.1</a><br/>
<a href="#2.2.0">2.2.0</a><br/>
<a href="#2.1.0">2.1.0</a><br/>
<a href="#2.0.2">2.0.2</a><br/>
<a href="#2.0.1">2.0.1</a><br/>
<a href="#2.0.0">2.0.0</a><br/>
</td>
<td valign="top">
<a href="#1.8.4">1.8.4</a><br/>
<a href="#1.8.3">1.8.3</a><br/>
<a href="#1.8.2">1.8.2</a><br/>
<a href="#1.8.1">1.8.1</a><br/>
<a href="#1.7.1">1.7.1</a><br/>
<a href="#1.7.0">1.7.0</a><br/>
<a href="#1.6.4">1.6.4</a><br/>
<a href="#1.6.3">1.6.3</a><br/>
<a href="#1.6.2">1.6.2</a><br/>
<a href="#1.6.1">1.6.1</a><br/>
<a href="#1.6.0">1.6.0</a><br/>
<a href="#1.5.1">1.5.1</a><br/>
<a href="#1.5.0">1.5.0</a><br/>
<a href="#1.4.3">1.4.3</a><br/>
<a href="#1.4.2">1.4.2</a><br/>
<a href="#1.4.1">1.4.1</a><br/>
<a href="#1.4.1">1.4.0</a><br/>
<a href="#1.3.0">1.3.0</a><br/>
<a href="#1.2.0">1.2.0</a><br/>
<a href="#1.1.0">1.1.0</a><br/>
<a href="#1.0.4">1.0.4</a><br/>
<a href="#1.0.3">1.0.3</a><br/>
<a href="#1.0.2">1.0.2</a><br/>
<a href="#1.0.1">1.0.1</a><br/>
<a href="#1.0.0">1.0.0</a><br/>
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
  * [Archive](CHANGELOG_ARCHIVE.md)

<a id="3.3.1"></a>

## 2015-09-15, io.js Version 3.3.1 @rvagg

### Notable changes

* **buffer**: Fixed a minor errors that was causing crashes (Michaël Zasso) [#2635](https://github.com/nodejs/node/pull/2635),
* **child\_process**: Fix error that was causing crashes (Evan Lucas) [#2727](https://github.com/nodejs/node/pull/2727)
* **crypto**: Replace use of rwlocks, unsafe on Windows XP / 2003 (Ben Noordhuis) [#2723](https://github.com/nodejs/node/pull/2723)
* **libuv**: Upgrade from 1.7.3 to 1.7.4 (Saúl Ibarra Corretgé) [#2817](https://github.com/nodejs/node/pull/2817)
* **node**: Fix faulty `process.release.libUrl` on Windows (Rod Vagg) [#2699](https://github.com/nodejs/node/pull/2699)
* **node-gyp**: Float v3.0.3 which has improved support for Node.js and io.js v0.10 to v4+ (Rod Vagg) [#2700](https://github.com/nodejs/node/pull/2700)
* **npm**: Upgrade to version 2.14.3 from 2.13.3, includes a security update, see <https://github.com/npm/npm/releases/tag/v2.14.2> for more details, (Kat Marchán) [#2696](https://github.com/nodejs/node/pull/2696).
* **timers**: Improved timer performance from porting the 0.12 implementation, plus minor fixes (Jeremiah Senkpiel) [#2540](https://github.com/nodejs/node/pull/2540), (Julien Gilli) [nodejs/node-v0.x-archive#8751](https://github.com/nodejs/node-v0.x-archive/pull/8751) [nodejs/node-v0.x-archive#8905](https://github.com/nodejs/node-v0.x-archive/pull/8905)

### Known issues

See <https://github.com/nodejs/io.js/labels/confirmed-bug> for complete and current list of known issues.

* Some uses of computed object shorthand properties are not handled correctly by the current version of V8. e.g. `[{ [prop]: val }]` evaluates to `[{}]`. [#2507](https://github.com/nodejs/node/issues/2507)
* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/io.js/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/io.js/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/io.js/issues/760).
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/io.js/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/io.js/issues/1435).

### Commits

* \[[`b73ff52fe6`](https://github.com/nodejs/node/commit/b73ff52fe6)] - **bindings**: close after reading module struct (Fedor Indutny) [#2792](https://github.com/nodejs/node/pull/2792)
* \[[`aa1140e59a`](https://github.com/nodejs/node/commit/aa1140e59a)] - **buffer**: SlowBuffer only accept valid numeric values (Michaël Zasso) [#2635](https://github.com/nodejs/node/pull/2635)
* \[[`574475d56e`](https://github.com/nodejs/node/commit/574475d56e)] - **build**: clean up the generated tap file (Sakthipriyan Vairamani) [#2837](https://github.com/nodejs/node/pull/2837)
* \[[`aa0001271e`](https://github.com/nodejs/node/commit/aa0001271e)] - **build**: remote commands on staging in single session (Rod Vagg) [#2717](https://github.com/nodejs/node/pull/2717)
* \[[`1428661095`](https://github.com/nodejs/node/commit/1428661095)] - **build**: fix v8\_enable\_handle\_zapping override (Karl Skomski) [#2731](https://github.com/nodejs/node/pull/2731)
* \[[`5a51edd718`](https://github.com/nodejs/node/commit/5a51edd718)] - **build**: add --enable-asan with builtin leakcheck (Karl Skomski) [#2376](https://github.com/nodejs/node/pull/2376)
* \[[`618caa5de0`](https://github.com/nodejs/node/commit/618caa5de0)] - **child\_process**: use stdio.fd even if it is 0 (Evan Lucas) [#2727](https://github.com/nodejs/node/pull/2727)
* \[[`7be4e49cb6`](https://github.com/nodejs/node/commit/7be4e49cb6)] - **child\_process**: check execFile and fork args (James M Snell) [#2667](https://github.com/nodejs/node/pull/2667)
* \[[`7f5d6e72c6`](https://github.com/nodejs/node/commit/7f5d6e72c6)] - **cluster**: allow shared reused dgram sockets (Fedor Indutny) [#2548](https://github.com/nodejs/node/pull/2548)
* \[[`e68c7ec498`](https://github.com/nodejs/node/commit/e68c7ec498)] - **contextify**: ignore getters during initialization (Fedor Indutny) [nodejs/io.js#2091](https://github.com/nodejs/io.js/pull/2091)
* \[[`610fa964aa`](https://github.com/nodejs/node/commit/610fa964aa)] - **cpplint**: make it possible to run outside git repo (Ben Noordhuis) [#2710](https://github.com/nodejs/node/pull/2710)
* \[[`4237373dd7`](https://github.com/nodejs/node/commit/4237373dd7)] - **crypto**: replace rwlocks with simple mutexes (Ben Noordhuis) [#2723](https://github.com/nodejs/node/pull/2723)
* \[[`777eb00306`](https://github.com/nodejs/node/commit/777eb00306)] - **deps**: upgraded to node-gyp\@3.0.3 in npm (Kat Marchán) [#2822](https://github.com/nodejs/node/pull/2822)
* \[[`b729ad384b`](https://github.com/nodejs/node/commit/b729ad384b)] - **deps**: upgrade to npm 2.14.3 (Kat Marchán) [#2822](https://github.com/nodejs/node/pull/2822)
* \[[`b09fde761c`](https://github.com/nodejs/node/commit/b09fde761c)] - **deps**: update libuv to version 1.7.4 (Saúl Ibarra Corretgé) [#2817](https://github.com/nodejs/node/pull/2817)
* \[[`4cf225daad`](https://github.com/nodejs/node/commit/4cf225daad)] - **deps**: float node-gyp v3.0.0 (Rod Vagg) [#2700](https://github.com/nodejs/node/pull/2700)
* \[[`118f48c0f3`](https://github.com/nodejs/node/commit/118f48c0f3)] - **deps**: create .npmrc during npm tests (Kat Marchán) [#2696](https://github.com/nodejs/node/pull/2696)
* \[[`b3fee8e6a6`](https://github.com/nodejs/node/commit/b3fee8e6a6)] - **deps**: upgrade to npm 2.14.2 (Kat Marchán) [#2696](https://github.com/nodejs/node/pull/2696)
* \[[`4593539b92`](https://github.com/nodejs/node/commit/4593539b92)] - **deps**: backport 75e43a6 from v8 upstream (saper) [#2636](https://github.com/nodejs/node/pull/2636)
* \[[`2d1438cfe0`](https://github.com/nodejs/node/commit/2d1438cfe0)] - **doc**: fix broken link in repl.markdown (Danny Nemer) [#2827](https://github.com/nodejs/node/pull/2827)
* \[[`9dd9c85a48`](https://github.com/nodejs/node/commit/9dd9c85a48)] - **doc**: fix typos in README (Ionică Bizău) [#2852](https://github.com/nodejs/node/pull/2852)
* \[[`476125d403`](https://github.com/nodejs/node/commit/476125d403)] - **doc**: add tunniclm as a collaborator (Mike Tunnicliffe) [#2826](https://github.com/nodejs/node/pull/2826)
* \[[`0603a92d48`](https://github.com/nodejs/node/commit/0603a92d48)] - **doc**: fix two doc errors in stream and process (Jeremiah Senkpiel) [#2549](https://github.com/nodejs/node/pull/2549)
* \[[`da2902ddfd`](https://github.com/nodejs/node/commit/da2902ddfd)] - **doc**: use "Calls" over "Executes" for consistency (Minwoo Jung) [#2800](https://github.com/nodejs/node/pull/2800)
* \[[`5e93bc4fba`](https://github.com/nodejs/node/commit/5e93bc4fba)] - **doc**: use US English for consistency (Anne-Gaelle Colom) [#2784](https://github.com/nodejs/node/pull/2784)
* \[[`3ee7fbcefd`](https://github.com/nodejs/node/commit/3ee7fbcefd)] - **doc**: use 3rd person singular for consistency (Anne-Gaelle Colom) [#2765](https://github.com/nodejs/node/pull/2765)
* \[[`4fdccb9eb7`](https://github.com/nodejs/node/commit/4fdccb9eb7)] - **doc**: fix comma splice in Assertion Testing doc (Rich Trott) [#2728](https://github.com/nodejs/node/pull/2728)
* \[[`28c2d310d6`](https://github.com/nodejs/node/commit/28c2d310d6)] - **doc**: update AUTHORS list (Rod Vagg)
* \[[`324c073fb9`](https://github.com/nodejs/node/commit/324c073fb9)] - **doc**: add TSC meeting minutes 2015-09-02 (Rod Vagg) [#2674](https://github.com/nodejs/node/pull/2674)
* \[[`8929445686`](https://github.com/nodejs/node/commit/8929445686)] - **doc**: update url doc to account for escaping (Jeremiah Senkpiel) [#2605](https://github.com/nodejs/node/pull/2605)
* \[[`512dad6883`](https://github.com/nodejs/node/commit/512dad6883)] - **doc**: reorder collaborators by their usernames (Johan Bergström) [#2322](https://github.com/nodejs/node/pull/2322)
* \[[`8372ea2ca5`](https://github.com/nodejs/node/commit/8372ea2ca5)] - **doc,test**: enable recursive file watching in Windows (Sakthipriyan Vairamani) [#2649](https://github.com/nodejs/node/pull/2649)
* \[[`daf6c533cc`](https://github.com/nodejs/node/commit/daf6c533cc)] - **events,lib**: don't require EE#listenerCount() (Jeremiah Senkpiel) [#2661](https://github.com/nodejs/node/pull/2661)
* \[[`d8371a801e`](https://github.com/nodejs/node/commit/d8371a801e)] - **http\_server**: fix resume after socket close (Fedor Indutny) [#2824](https://github.com/nodejs/node/pull/2824)
* \[[`7f7d4fdddd`](https://github.com/nodejs/node/commit/7f7d4fdddd)] - **node-gyp**: float 3.0.1, minor fix for download url (Rod Vagg) [#2737](https://github.com/nodejs/node/pull/2737)
* \[[`91cee73294`](https://github.com/nodejs/node/commit/91cee73294)] - **src**: use ZCtxt as a source for v8::Isolates (Roman Klauke) [#2547](https://github.com/nodejs/node/pull/2547)
* \[[`ac98e13b95`](https://github.com/nodejs/node/commit/ac98e13b95)] - **src**: s/ia32/x86 for process.release.libUrl for win (Rod Vagg) [#2699](https://github.com/nodejs/node/pull/2699)
* \[[`ca6c3223e1`](https://github.com/nodejs/node/commit/ca6c3223e1)] - **src**: use standard conform snprintf on windows (Karl Skomski) [#2404](https://github.com/nodejs/node/pull/2404)
* \[[`b028978a53`](https://github.com/nodejs/node/commit/b028978a53)] - **src**: fix buffer overflow for long exception lines (Karl Skomski) [#2404](https://github.com/nodejs/node/pull/2404)
* \[[`e73eafd7e7`](https://github.com/nodejs/node/commit/e73eafd7e7)] - **src**: fix memory leak in ExternString (Karl Skomski) [#2402](https://github.com/nodejs/node/pull/2402)
* \[[`d370306de1`](https://github.com/nodejs/node/commit/d370306de1)] - **src**: only set v8 flags if argc > 1 (Evan Lucas) [#2646](https://github.com/nodejs/node/pull/2646)
* \[[`ed087836af`](https://github.com/nodejs/node/commit/ed087836af)] - **streams**: refactor LazyTransform to internal/ (Brendan Ashworth) [#2566](https://github.com/nodejs/node/pull/2566)
* \[[`993c22fe0e`](https://github.com/nodejs/node/commit/993c22fe0e)] - **test**: remove disabled test (Rich Trott) [#2841](https://github.com/nodejs/node/pull/2841)
* \[[`1474f29d1f`](https://github.com/nodejs/node/commit/1474f29d1f)] - **test**: split up internet dns tests (Rich Trott) [#2802](https://github.com/nodejs/node/pull/2802)
* \[[`601a97622b`](https://github.com/nodejs/node/commit/601a97622b)] - **test**: increase dgram timeout for armv6 (Rich Trott) [#2808](https://github.com/nodejs/node/pull/2808)
* \[[`1dad19ba81`](https://github.com/nodejs/node/commit/1dad19ba81)] - **test**: remove valid hostname check in test-dns.js (Rich Trott) [#2785](https://github.com/nodejs/node/pull/2785)
* \[[`f3d5891a3f`](https://github.com/nodejs/node/commit/f3d5891a3f)] - **test**: expect error for test\_lookup\_ipv6\_hint on FreeBSD (Rich Trott) [#2724](https://github.com/nodejs/node/pull/2724)
* \[[`2ffb21baf1`](https://github.com/nodejs/node/commit/2ffb21baf1)] - **test**: fix use of `common` before required (Rod Vagg) [#2685](https://github.com/nodejs/node/pull/2685)
* \[[`b2c5479a14`](https://github.com/nodejs/node/commit/b2c5479a14)] - **test**: refactor to eliminate flaky test (Rich Trott) [#2609](https://github.com/nodejs/node/pull/2609)
* \[[`fcfd15f8f9`](https://github.com/nodejs/node/commit/fcfd15f8f9)] - **test**: mark eval\_messages as flaky (Alexis Campailla) [#2648](https://github.com/nodejs/node/pull/2648)
* \[[`1865cad7ae`](https://github.com/nodejs/node/commit/1865cad7ae)] - **test**: mark test-vm-syntax-error-stderr as flaky (João Reis) [#2662](https://github.com/nodejs/node/pull/2662)
* \[[`b0014ecd27`](https://github.com/nodejs/node/commit/b0014ecd27)] - **test**: mark test-repl-persistent-history as flaky (João Reis) [#2659](https://github.com/nodejs/node/pull/2659)
* \[[`74ff9bc86c`](https://github.com/nodejs/node/commit/74ff9bc86c)] - **timers**: minor \_unrefActive fixes and improvements (Jeremiah Senkpiel) [#2540](https://github.com/nodejs/node/pull/2540)
* \[[`5d14a6eca7`](https://github.com/nodejs/node/commit/5d14a6eca7)] - **timers**: don't mutate unref list while iterating it (Julien Gilli) [#2540](https://github.com/nodejs/node/pull/2540)
* \[[`6e744c58f2`](https://github.com/nodejs/node/commit/6e744c58f2)] - **timers**: Avoid linear scan in \_unrefActive. (Julien Gilli) [#2540](https://github.com/nodejs/node/pull/2540)
* \[[`07fbf835ad`](https://github.com/nodejs/node/commit/07fbf835ad)] - **tools**: open `test.tap` file in write-binary mode (Sakthipriyan Vairamani) [#2837](https://github.com/nodejs/node/pull/2837)
* \[[`6d9198f7f1`](https://github.com/nodejs/node/commit/6d9198f7f1)] - **tools**: add missing tick processor polyfill (Matt Loring) [#2694](https://github.com/nodejs/node/pull/2694)
* \[[`7b16597527`](https://github.com/nodejs/node/commit/7b16597527)] - **tools**: fix flakiness in test-tick-processor (Matt Loring) [#2694](https://github.com/nodejs/node/pull/2694)
* \[[`ef83029356`](https://github.com/nodejs/node/commit/ef83029356)] - **tools**: remove hyphen in TAP result (Sakthipriyan Vairamani) [#2718](https://github.com/nodejs/node/pull/2718)
* \[[`ac45ef9157`](https://github.com/nodejs/node/commit/ac45ef9157)] - **win,msi**: fix documentation shortcut url (Brian White) [#2781](https://github.com/nodejs/node/pull/2781)

<a id="3.3.0"></a>

## 2015-09-02, Version 3.3.0, @rvagg

### Notable changes

* **build**: Add a `--link-module` option to `configure` that can be used to bundle additional JavaScript modules into a built binary (Bradley Meck) [#2497](https://github.com/nodejs/node/pull/2497)
* **docs**: Merge outstanding doc updates from joyent/node (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* **http\_parser**: Significant performance improvement by having `http.Server` consume all initial data from its `net.Socket` and parsing directly without having to enter JavaScript. Any `'data'` listeners on the `net.Socket` will result in the data being "unconsumed" into JavaScript, thereby undoing any performance gains. (Fedor Indutny) [#2355](https://github.com/nodejs/node/pull/2355)
* **libuv**: Upgrade to 1.7.3 (from 1.6.1), see [ChangeLog](https://github.com/libuv/libuv/blob/v1.x/ChangeLog) for details (Saúl Ibarra Corretgé) [#2310](https://github.com/nodejs/node/pull/2310)
* **V8**: Upgrade to 4.4.63.30 (from 4.4.63.26) (Michaël Zasso) [#2482](https://github.com/nodejs/node/pull/2482)

### Known issues

See <https://github.com/nodejs/io.js/labels/confirmed-bug> for complete and current list of known issues.

* Some uses of computed object shorthand properties are not handled correctly by the current version of V8. e.g. `[{ [prop]: val }]` evaluates to `[{}]`. [#2507](https://github.com/nodejs/node/issues/2507)
* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/io.js/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/io.js/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/io.js/issues/760).
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/io.js/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/io.js/issues/1435).

### Commits

* \[[`1a531b4e44`](https://github.com/nodejs/node/commit/1a531b4e44)] - **(SEMVER-MINOR)** Introduce --link-module to ./configure (Bradley Meck) [#2497](https://github.com/nodejs/node/pull/2497)
* \[[`d2f314c190`](https://github.com/nodejs/node/commit/d2f314c190)] - **build**: fix borked chmod call for release uploads (Rod Vagg) [#2645](https://github.com/nodejs/node/pull/2645)
* \[[`3172e9c541`](https://github.com/nodejs/node/commit/3172e9c541)] - **build**: set file permissions before uploading (Rod Vagg) [#2623](https://github.com/nodejs/node/pull/2623)
* \[[`a860d7fae1`](https://github.com/nodejs/node/commit/a860d7fae1)] - **build**: change staging directory on new server (Rod Vagg) [#2623](https://github.com/nodejs/node/pull/2623)
* \[[`50c0baa8d7`](https://github.com/nodejs/node/commit/50c0baa8d7)] - **build**: rename 'doc' directory to 'docs' for upload (Rod Vagg) [#2623](https://github.com/nodejs/node/pull/2623)
* \[[`0a0577cf5f`](https://github.com/nodejs/node/commit/0a0577cf5f)] - **build**: fix bad cherry-pick for vcbuild.bat build-release (Rod Vagg) [#2625](https://github.com/nodejs/node/pull/2625)
* \[[`34de90194b`](https://github.com/nodejs/node/commit/34de90194b)] - **build**: only define NODE\_V8\_OPTIONS if not empty (Evan Lucas) [#2532](https://github.com/nodejs/node/pull/2532)
* \[[`944174b189`](https://github.com/nodejs/node/commit/944174b189)] - **build**: make ci test addons in test/addons (Ben Noordhuis) [#2428](https://github.com/nodejs/node/pull/2428)
* \[[`e955f9a1b0`](https://github.com/nodejs/node/commit/e955f9a1b0)] - **crypto**: Use OPENSSL\_cleanse to shred the data. (Сковорода Никита Андреевич) [#2575](https://github.com/nodejs/node/pull/2575)
* \[[`395d736b9d`](https://github.com/nodejs/node/commit/395d736b9d)] - **debugger**: use strict equality comparison (Minwoo Jung) [#2558](https://github.com/nodejs/node/pull/2558)
* \[[`1d0e5210a8`](https://github.com/nodejs/node/commit/1d0e5210a8)] - **deps**: upgrade libuv to 1.7.3 (Saúl Ibarra Corretgé) [#2310](https://github.com/nodejs/node/pull/2310)
* \[[`34ef53364f`](https://github.com/nodejs/node/commit/34ef53364f)] - **deps**: update V8 to 4.4.63.30 (Michaël Zasso) [#2482](https://github.com/nodejs/node/pull/2482)
* \[[`23579a5f4a`](https://github.com/nodejs/node/commit/23579a5f4a)] - **doc**: add TSC meeting minutes 2015-08-12 (Rod Vagg) [#2438](https://github.com/nodejs/node/pull/2438)
* \[[`0cc59299a4`](https://github.com/nodejs/node/commit/0cc59299a4)] - **doc**: add TSC meeting minutes 2015-08-26 (Rod Vagg) [#2591](https://github.com/nodejs/node/pull/2591)
* \[[`6efa96e33a`](https://github.com/nodejs/node/commit/6efa96e33a)] - **doc**: merge CHANGELOG.md with joyent/node ChangeLog (Minqi Pan) [#2536](https://github.com/nodejs/node/pull/2536)
* \[[`f75d54607b`](https://github.com/nodejs/node/commit/f75d54607b)] - **doc**: clarify cluster behaviour with no workers (Jeremiah Senkpiel) [#2606](https://github.com/nodejs/node/pull/2606)
* \[[`8936302121`](https://github.com/nodejs/node/commit/8936302121)] - **doc**: minor clarification in buffer.markdown (Сковорода Никита Андреевич) [#2574](https://github.com/nodejs/node/pull/2574)
* \[[`0db0e53753`](https://github.com/nodejs/node/commit/0db0e53753)] - **doc**: add @jasnell and @sam-github to release team (Rod Vagg) [#2455](https://github.com/nodejs/node/pull/2455)
* \[[`c16e100593`](https://github.com/nodejs/node/commit/c16e100593)] - **doc**: reorg release team to separate section (Rod Vagg) [#2455](https://github.com/nodejs/node/pull/2455)
* \[[`e3e00143fd`](https://github.com/nodejs/node/commit/e3e00143fd)] - **doc**: fix bad merge on modules.markdown (James M Snell)
* \[[`2f62455880`](https://github.com/nodejs/node/commit/2f62455880)] - **doc**: minor additional corrections and improvements (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`3bd08aac4b`](https://github.com/nodejs/node/commit/3bd08aac4b)] - **doc**: minor grammatical update in crypto.markdown (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`f707189370`](https://github.com/nodejs/node/commit/f707189370)] - **doc**: minor grammatical update (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`6c98cf0266`](https://github.com/nodejs/node/commit/6c98cf0266)] - **doc**: remove repeated statement in globals.markdown (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`48e6ccf8c2`](https://github.com/nodejs/node/commit/48e6ccf8c2)] - **doc**: remove 'dudes' from documentation (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`b5d68f8076`](https://github.com/nodejs/node/commit/b5d68f8076)] - **doc**: update tense in child\_process.markdown (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`242e3fe3ba`](https://github.com/nodejs/node/commit/242e3fe3ba)] - **doc**: fixed worker.id type (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`ea9ee15c21`](https://github.com/nodejs/node/commit/ea9ee15c21)] - **doc**: port is optional for socket.bind() (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`0ff6657a50`](https://github.com/nodejs/node/commit/0ff6657a50)] - **doc**: fix minor types and grammar in fs docs (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`94d83c04f2`](https://github.com/nodejs/node/commit/94d83c04f2)] - **doc**: update parameter name in net.markdown (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`04111ce40f`](https://github.com/nodejs/node/commit/04111ce40f)] - **doc**: small typo in domain.markdown (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`c9fdd1bbbf`](https://github.com/nodejs/node/commit/c9fdd1bbbf)] - **doc**: fixed typo in net.markdown (missing comma) (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`27c07b3f8e`](https://github.com/nodejs/node/commit/27c07b3f8e)] - **doc**: update description of fs.exists in fs.markdown (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`52018e73d9`](https://github.com/nodejs/node/commit/52018e73d9)] - **doc**: clarification on the 'close' event (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`f6d3b87a25`](https://github.com/nodejs/node/commit/f6d3b87a25)] - **doc**: improve working in stream.markdown (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`b5da89431a`](https://github.com/nodejs/node/commit/b5da89431a)] - **doc**: update path.extname documentation (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`1d4ea609db`](https://github.com/nodejs/node/commit/1d4ea609db)] - **doc**: small clarifications to modules.markdown (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`c888985591`](https://github.com/nodejs/node/commit/c888985591)] - **doc**: code style cleanups in repl.markdown (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`105b493595`](https://github.com/nodejs/node/commit/105b493595)] - **doc**: correct grammar in cluster.markdown (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`51b86ccac7`](https://github.com/nodejs/node/commit/51b86ccac7)] - **doc**: Clarify the module.parent is set once (James M Snell) [#2378](https://github.com/nodejs/node/pull/2378)
* \[[`d2ffecba2d`](https://github.com/nodejs/node/commit/d2ffecba2d)] - **doc**: add internal modules notice (Jeremiah Senkpiel) [#2523](https://github.com/nodejs/node/pull/2523)
* \[[`b36debd5cb`](https://github.com/nodejs/node/commit/b36debd5cb)] - **env**: introduce `KickNextTick` (Fedor Indutny) [#2355](https://github.com/nodejs/node/pull/2355)
* \[[`1bc446863f`](https://github.com/nodejs/node/commit/1bc446863f)] - **http\_parser**: consume StreamBase instance (Fedor Indutny) [#2355](https://github.com/nodejs/node/pull/2355)
* \[[`ce04b735cc`](https://github.com/nodejs/node/commit/ce04b735cc)] - **src**: only memcmp if length > 0 in Buffer::Compare (Karl Skomski) [#2544](https://github.com/nodejs/node/pull/2544)
* \[[`31823e37c7`](https://github.com/nodejs/node/commit/31823e37c7)] - **src**: DRY getsockname/getpeername code (Ben Noordhuis) [#956](https://github.com/nodejs/node/pull/956)
* \[[`13fd96dda3`](https://github.com/nodejs/node/commit/13fd96dda3)] - **src**: missing Exception::Error in node\_http\_parser (Jeremiah Senkpiel) [#2550](https://github.com/nodejs/node/pull/2550)
* \[[`42e075ae02`](https://github.com/nodejs/node/commit/42e075ae02)] - **test**: improve performance of stringbytes test (Trevor Norris) [#2544](https://github.com/nodejs/node/pull/2544)
* \[[`fc726399fd`](https://github.com/nodejs/node/commit/fc726399fd)] - **test**: unmark test-process-argv-0.js as flaky (Rich Trott) [#2613](https://github.com/nodejs/node/pull/2613)
* \[[`7727ba1394`](https://github.com/nodejs/node/commit/7727ba1394)] - **test**: lint and refactor to avoid autocrlf issue (Roman Reiss) [#2494](https://github.com/nodejs/node/pull/2494)
* \[[`c56aa829f0`](https://github.com/nodejs/node/commit/c56aa829f0)] - **test**: use tmpDir instead of fixturesDir (Sakthipriyan Vairamani) [#2583](https://github.com/nodejs/node/pull/2583)
* \[[`5e65181ea4`](https://github.com/nodejs/node/commit/5e65181ea4)] - **test**: handling failure cases properly (Sakthipriyan Vairamani) [#2206](https://github.com/nodejs/node/pull/2206)
* \[[`c48b95e847`](https://github.com/nodejs/node/commit/c48b95e847)] - **test**: initial list of flaky tests (Alexis Campailla) [#2424](https://github.com/nodejs/node/pull/2424)
* \[[`94e88498ba`](https://github.com/nodejs/node/commit/94e88498ba)] - **test**: pass args to test-ci via env variable (Alexis Campailla) [#2424](https://github.com/nodejs/node/pull/2424)
* \[[`09987c7a1c`](https://github.com/nodejs/node/commit/09987c7a1c)] - **test**: support flaky tests in test-ci (Alexis Campailla) [#2424](https://github.com/nodejs/node/pull/2424)
* \[[`08b83c8b45`](https://github.com/nodejs/node/commit/08b83c8b45)] - **test**: add test configuration templates (Alexis Campailla) [#2424](https://github.com/nodejs/node/pull/2424)
* \[[`8f8ab6fa57`](https://github.com/nodejs/node/commit/8f8ab6fa57)] - **test**: runner should return 0 on flaky tests (Alexis Campailla) [#2424](https://github.com/nodejs/node/pull/2424)
* \[[`0cfd3be9c6`](https://github.com/nodejs/node/commit/0cfd3be9c6)] - **test**: runner support for flaky tests (Alexis Campailla) [#2424](https://github.com/nodejs/node/pull/2424)
* \[[`3492d2d4c6`](https://github.com/nodejs/node/commit/3492d2d4c6)] - **test**: make test-process-argv-0 robust (Rich Trott) [#2541](https://github.com/nodejs/node/pull/2541)
* \[[`a96cc31710`](https://github.com/nodejs/node/commit/a96cc31710)] - **test**: speed up test-child-process-spawnsync.js (Rich Trott) [#2542](https://github.com/nodejs/node/pull/2542)
* \[[`856baf4c67`](https://github.com/nodejs/node/commit/856baf4c67)] - **test**: make spawnSync() test robust (Rich Trott) [#2535](https://github.com/nodejs/node/pull/2535)
* \[[`3aa6bbb648`](https://github.com/nodejs/node/commit/3aa6bbb648)] - **tools**: update release.sh to work with new website (Rod Vagg) [#2623](https://github.com/nodejs/node/pull/2623)
* \[[`f2f0fe45ff`](https://github.com/nodejs/node/commit/f2f0fe45ff)] - **tools**: make add-on scraper print filenames (Ben Noordhuis) [#2428](https://github.com/nodejs/node/pull/2428)
* \[[`bb24c4a418`](https://github.com/nodejs/node/commit/bb24c4a418)] - **win,msi**: correct installation path registry keys (João Reis) [#2565](https://github.com/nodejs/node/pull/2565)
* \[[`752977b888`](https://github.com/nodejs/node/commit/752977b888)] - **win,msi**: change InstallScope to perMachine (João Reis) [#2565](https://github.com/nodejs/node/pull/2565)

<a id="3.2.0"></a>

## 2015-08-25, Version 3.2.0, @rvagg

### Notable changes

* **events**: Added `EventEmitter#listenerCount(event)` as a replacement for `EventEmitter.listenerCount(emitter, event)`, which has now been marked as deprecated in the docs. (Sakthipriyan Vairamani) [#2349](https://github.com/nodejs/node/pull/2349)
* **module**: Fixed an error with preloaded modules when the current working directory doesn't exist. (Bradley Meck) [#2353](https://github.com/nodejs/node/pull/2353)
* **node**: Startup time is now about 5% faster when not passing V8 flags. (Evan Lucas) [#2483](https://github.com/nodejs/node/pull/2483)
* **repl**: Tab-completion now works better with arrays. (James M Snell) [#2409](https://github.com/nodejs/node/pull/2409)
* **string\_bytes**: Fixed an unaligned write in the handling of UCS2 encoding. (Fedor Indutny) [#2480](https://github.com/nodejs/node/pull/2480)
* **tls**: Added a new `--tls-cipher-list` flag that can be used to override the built-in default cipher list. (James M Snell) [#2412](https://github.com/nodejs/node/pull/2412) _Note: it is suggested you use the built-in cipher list as it has been carefully selected to reflect current security best practices and risk mitigation._

### Known issues

See <https://github.com/nodejs/io.js/labels/confirmed-bug> for complete and current list of known issues.

* Some uses of computed object shorthand properties are not handled correctly by the current version of V8. e.g. `[{ [prop]: val }]` evaluates to `[{}]`. [#2507](https://github.com/nodejs/node/issues/2507)
* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/io.js/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/io.js/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/io.js/issues/760).
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/io.js/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/io.js/issues/1435).

### Commits

* \[[`1cd794f129`](https://github.com/nodejs/node/commit/1cd794f129)] - **buffer**: reapply 07c0667 (Fedor Indutny) [#2487](https://github.com/nodejs/node/pull/2487)
* \[[`156781dedd`](https://github.com/nodejs/node/commit/156781dedd)] - **build**: use required platform in android-configure (Evan Lucas) [#2501](https://github.com/nodejs/node/pull/2501)
* \[[`77075ec906`](https://github.com/nodejs/node/commit/77075ec906)] - **crypto**: fix mem {de}allocation in ExportChallenge (Karl Skomski) [#2359](https://github.com/nodejs/node/pull/2359)
* \[[`cb30414d9e`](https://github.com/nodejs/node/commit/cb30414d9e)] - **doc**: sync CHANGELOG.md from master (Roman Reiss) [#2524](https://github.com/nodejs/node/pull/2524)
* \[[`9330f5ef45`](https://github.com/nodejs/node/commit/9330f5ef45)] - **doc**: make the deprecations consistent (Sakthipriyan Vairamani) [#2450](https://github.com/nodejs/node/pull/2450)
* \[[`09437e0146`](https://github.com/nodejs/node/commit/09437e0146)] - **doc**: fix comments in tls\_wrap.cc and \_http\_client.js (Minwoo Jung) [#2489](https://github.com/nodejs/node/pull/2489)
* \[[`c9867fed29`](https://github.com/nodejs/node/commit/c9867fed29)] - **doc**: document response.finished in http.markdown (hackerjs) [#2414](https://github.com/nodejs/node/pull/2414)
* \[[`7f23a83c42`](https://github.com/nodejs/node/commit/7f23a83c42)] - **doc**: update AUTHORS list (Rod Vagg) [#2505](https://github.com/nodejs/node/pull/2505)
* \[[`cd0c362f67`](https://github.com/nodejs/node/commit/cd0c362f67)] - **doc**: update AUTHORS list (Rod Vagg) [#2318](https://github.com/nodejs/node/pull/2318)
* \[[`2c7b9257ea`](https://github.com/nodejs/node/commit/2c7b9257ea)] - **doc**: add TSC meeting minutes 2015-07-29 (Rod Vagg) [#2437](https://github.com/nodejs/node/pull/2437)
* \[[`aaefde793e`](https://github.com/nodejs/node/commit/aaefde793e)] - **doc**: add TSC meeting minutes 2015-08-19 (Rod Vagg) [#2460](https://github.com/nodejs/node/pull/2460)
* \[[`51ef9106f5`](https://github.com/nodejs/node/commit/51ef9106f5)] - **doc**: add TSC meeting minutes 2015-06-03 (Rod Vagg) [#2453](https://github.com/nodejs/node/pull/2453)
* \[[`7130b4cf1d`](https://github.com/nodejs/node/commit/7130b4cf1d)] - **doc**: fix links to original converged repo (Rod Vagg) [#2454](https://github.com/nodejs/node/pull/2454)
* \[[`14f2aee1df`](https://github.com/nodejs/node/commit/14f2aee1df)] - **doc**: fix links to original gh issues for TSC meetings (Rod Vagg) [#2454](https://github.com/nodejs/node/pull/2454)
* \[[`87a9ef0a40`](https://github.com/nodejs/node/commit/87a9ef0a40)] - **doc**: add audio recording links to TSC meeting minutes (Rod Vagg) [#2454](https://github.com/nodejs/node/pull/2454)
* \[[`f5cf24afbc`](https://github.com/nodejs/node/commit/f5cf24afbc)] - **doc**: add TSC meeting minutes 2015-07-22 (Rod Vagg) [#2436](https://github.com/nodejs/node/pull/2436)
* \[[`3f821b96eb`](https://github.com/nodejs/node/commit/3f821b96eb)] - **doc**: fix spelling mistake in node.js comment (Jacob Edelman) [#2391](https://github.com/nodejs/node/pull/2391)
* \[[`3e6a6fcdd6`](https://github.com/nodejs/node/commit/3e6a6fcdd6)] - **(SEMVER-MINOR)** **events**: deprecate static listenerCount function (Sakthipriyan Vairamani) [#2349](https://github.com/nodejs/node/pull/2349)
* \[[`023386c852`](https://github.com/nodejs/node/commit/023386c852)] - **fs**: replace bad\_args macro with concrete error msg (Roman Klauke) [#2495](https://github.com/nodejs/node/pull/2495)
* \[[`d1c27b2e29`](https://github.com/nodejs/node/commit/d1c27b2e29)] - **module**: fix module preloading when cwd is ENOENT (Bradley Meck) [#2353](https://github.com/nodejs/node/pull/2353)
* \[[`5d7486941b`](https://github.com/nodejs/node/commit/5d7486941b)] - **repl**: filter integer keys from repl tab complete list (James M Snell) [#2409](https://github.com/nodejs/node/pull/2409)
* \[[`7f02443a9a`](https://github.com/nodejs/node/commit/7f02443a9a)] - **repl**: dont throw ENOENT on NODE\_REPL\_HISTORY\_FILE (Todd Kennedy) [#2451](https://github.com/nodejs/node/pull/2451)
* \[[`56a2ae9cef`](https://github.com/nodejs/node/commit/56a2ae9cef)] - **src**: improve startup time (Evan Lucas) [#2483](https://github.com/nodejs/node/pull/2483)
* \[[`14653c7429`](https://github.com/nodejs/node/commit/14653c7429)] - **stream**: rename poorly named function (Ben Noordhuis) [#2479](https://github.com/nodejs/node/pull/2479)
* \[[`1c6e014bfa`](https://github.com/nodejs/node/commit/1c6e014bfa)] - **stream**: micro-optimize high water mark calculation (Ben Noordhuis) [#2479](https://github.com/nodejs/node/pull/2479)
* \[[`f1f4b4c46d`](https://github.com/nodejs/node/commit/f1f4b4c46d)] - **stream**: fix off-by-factor-16 error in comment (Ben Noordhuis) [#2479](https://github.com/nodejs/node/pull/2479)
* \[[`2d3f09bd76`](https://github.com/nodejs/node/commit/2d3f09bd76)] - **stream\_base**: various improvements (Fedor Indutny) [#2351](https://github.com/nodejs/node/pull/2351)
* \[[`c1ce423b35`](https://github.com/nodejs/node/commit/c1ce423b35)] - **string\_bytes**: fix unaligned write in UCS2 (Fedor Indutny) [#2480](https://github.com/nodejs/node/pull/2480)
* \[[`e4d0e86165`](https://github.com/nodejs/node/commit/e4d0e86165)] - **test**: refactor test-https-simple.js (Rich Trott) [#2433](https://github.com/nodejs/node/pull/2433)
* \[[`0ea5c8d737`](https://github.com/nodejs/node/commit/0ea5c8d737)] - **test**: remove test-timers-first-fire (João Reis) [#2458](https://github.com/nodejs/node/pull/2458)
* \[[`536c3d0537`](https://github.com/nodejs/node/commit/536c3d0537)] - **test**: use reserved IP in test-net-connect-timeout (Rich Trott) [#2257](https://github.com/nodejs/node/pull/2257)
* \[[`5df06fd8df`](https://github.com/nodejs/node/commit/5df06fd8df)] - **test**: add spaces after keywords (Brendan Ashworth)
* \[[`e714b5620e`](https://github.com/nodejs/node/commit/e714b5620e)] - **test**: remove unreachable code (Michaël Zasso) [#2289](https://github.com/nodejs/node/pull/2289)
* \[[`3579f3a2a4`](https://github.com/nodejs/node/commit/3579f3a2a4)] - **test**: disallow unreachable code (Michaël Zasso) [#2289](https://github.com/nodejs/node/pull/2289)
* \[[`3545e236fc`](https://github.com/nodejs/node/commit/3545e236fc)] - **test**: reduce timeouts in test-net-keepalive (Brendan Ashworth) [#2429](https://github.com/nodejs/node/pull/2429)
* \[[`b60e690023`](https://github.com/nodejs/node/commit/b60e690023)] - **test**: improve test-net-server-pause-on-connect (Brendan Ashworth) [#2429](https://github.com/nodejs/node/pull/2429)
* \[[`11d1b8fcaf`](https://github.com/nodejs/node/commit/11d1b8fcaf)] - **test**: improve test-net-pingpong (Brendan Ashworth) [#2429](https://github.com/nodejs/node/pull/2429)
* \[[`5fef5c6562`](https://github.com/nodejs/node/commit/5fef5c6562)] - **(SEMVER-MINOR)** **tls**: add --tls-cipher-list command line switch (James M Snell) [#2412](https://github.com/nodejs/node/pull/2412)
* \[[`d9b70f9cbf`](https://github.com/nodejs/node/commit/d9b70f9cbf)] - **tls**: handle empty cert in checkServerIndentity (Mike Atkins) [#2343](https://github.com/nodejs/node/pull/2343)
* \[[`4f8e34c202`](https://github.com/nodejs/node/commit/4f8e34c202)] - **tools**: add license boilerplate to check-imports.sh (James M Snell) [#2386](https://github.com/nodejs/node/pull/2386)
* \[[`b76b9197f9`](https://github.com/nodejs/node/commit/b76b9197f9)] - **tools**: enable space-after-keywords in eslint (Brendan Ashworth)
* \[[`64a8f30a70`](https://github.com/nodejs/node/commit/64a8f30a70)] - **tools**: fix anchors in generated documents (Sakthipriyan Vairamani) [#2491](https://github.com/nodejs/node/pull/2491)
* \[[`22e344ea10`](https://github.com/nodejs/node/commit/22e344ea10)] - **win**: fix custom actions for WiX older than 3.9 (João Reis) [#2365](https://github.com/nodejs/node/pull/2365)
* \[[`b5bd3ebfc8`](https://github.com/nodejs/node/commit/b5bd3ebfc8)] - **win**: fix custom actions on Visual Studio != 2013 (Julien Gilli) [#2365](https://github.com/nodejs/node/pull/2365)

<a id="3.1.0"></a>

## 2015-08-18, Version 3.1.0, @Fishrock123

### Notable changes

* **buffer**: Fixed a couple large memory leaks (Ben Noordhuis) [#2352](https://github.com/nodejs/node/pull/2352).
* **crypto**:
  * Fixed a couple of minor memory leaks (Karl Skomski) [#2375](https://github.com/nodejs/node/pull/2375).
  * Signing now checks for OpenSSL errors (Minqi Pan) [#2342](https://github.com/nodejs/node/pull/2342). **Note that this may expose previously hidden errors in user code.**
* **intl**: Intl support using small-icu is now enabled by default in builds (Steven R. Loomis) [#2264](https://github.com/nodejs/node/pull/2264).
  * [`String#normalize()`](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/String/normalize) can now be used for unicode normalization.
  * The [`Intl`](https://developer.mozilla.org/en/docs/Web/JavaScript/Reference/Global_Objects/Intl) object and various `String` and `Number` methods are present, but only support the English locale.
  * For support of all locales, node must be built with [full-icu](https://github.com/nodejs/node#build-with-full-icu-support-all-locales-supported-by-icu).
* **tls**: Fixed tls throughput being much lower after an incorrect merge (Fedor Indutny) [#2381](https://github.com/nodejs/node/pull/2381).
* **tools**: The v8 tick processor now comes bundled with node (Matt Loring) [#2090](https://github.com/nodejs/node/pull/2090).
  * This can be used by producing performance profiling output by running node with `--perf`, then running your appropriate platform's script on the output as found in [tools/v8-prof](https://github.com/nodejs/node/tree/master/tools/v8-prof).
* **util**: `util.inspect(obj)` now prints the constructor name of the object if there is one (Christopher Monsanto) [#1935](https://github.com/nodejs/io.js/pull/1935).

### Known issues

See <https://github.com/nodejs/io.js/labels/confirmed-bug> for complete and current list of known issues.

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/io.js/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/io.js/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/io.js/issues/760).
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/io.js/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/io.js/issues/1435).

### Commits

* \[[`3645dc62ed`](https://github.com/nodejs/node/commit/3645dc62ed)] - **build**: work around VS2015 issue in ICU <56 (Steven R. Loomis) [#2283](https://github.com/nodejs/node/pull/2283)
* \[[`1f12e03266`](https://github.com/nodejs/node/commit/1f12e03266)] - **(SEMVER-MINOR)** **build**: intl: converge from joyent/node (Steven R. Loomis) [#2264](https://github.com/nodejs/node/pull/2264)
* \[[`071640abdd`](https://github.com/nodejs/node/commit/071640abdd)] - **build**: Intl: bump ICU4C from 54 to 55 (Steven R. Loomis) [#2293](https://github.com/nodejs/node/pull/2293)
* \[[`07a88b0c8b`](https://github.com/nodejs/node/commit/07a88b0c8b)] - **build**: update manifest to include Windows 10 (Lucien Greathouse) [#2332](https://github.com/nodejs/io.js/pull/2332)
* \[[`0bb099f444`](https://github.com/nodejs/node/commit/0bb099f444)] - **build**: expand \~ in install prefix early (Ben Noordhuis) [#2307](https://github.com/nodejs/io.js/pull/2307)
* \[[`7fe6dd8f5d`](https://github.com/nodejs/node/commit/7fe6dd8f5d)] - **crypto**: check for OpenSSL errors when signing (Minqi Pan) [#2342](https://github.com/nodejs/node/pull/2342)
* \[[`605f6ee904`](https://github.com/nodejs/node/commit/605f6ee904)] - **crypto**: fix memory leak in PBKDF2Request (Karl Skomski) [#2375](https://github.com/nodejs/node/pull/2375)
* \[[`ba6eb8af12`](https://github.com/nodejs/node/commit/ba6eb8af12)] - **crypto**: fix memory leak in ECDH::SetPrivateKey (Karl Skomski) [#2375](https://github.com/nodejs/node/pull/2375)
* \[[`6a16368611`](https://github.com/nodejs/node/commit/6a16368611)] - **crypto**: fix memory leak in PublicKeyCipher::Cipher (Karl Skomski) [#2375](https://github.com/nodejs/node/pull/2375)
* \[[`a760a87803`](https://github.com/nodejs/node/commit/a760a87803)] - **crypto**: fix memory leak in SafeX509ExtPrint (Karl Skomski) [#2375](https://github.com/nodejs/node/pull/2375)
* \[[`f45487cd6e`](https://github.com/nodejs/node/commit/f45487cd6e)] - **crypto**: fix memory leak in SetDHParam (Karl Skomski) [#2375](https://github.com/nodejs/node/pull/2375)
* \[[`2ff183dd86`](https://github.com/nodejs/node/commit/2ff183dd86)] - **doc**: Update FIPS instructions in README.md (Michael Dawson) [#2278](https://github.com/nodejs/node/pull/2278)
* \[[`6483bc2e8f`](https://github.com/nodejs/node/commit/6483bc2e8f)] - **doc**: clarify options for fs.watchFile() (Rich Trott) [#2425](https://github.com/nodejs/node/pull/2425)
* \[[`e76822f454`](https://github.com/nodejs/node/commit/e76822f454)] - **doc**: multiple documentation updates cherry picked from v0.12 (James M Snell) [#2302](https://github.com/nodejs/io.js/pull/2302)
* \[[`1738c9680b`](https://github.com/nodejs/node/commit/1738c9680b)] - **net**: ensure Socket reported address is current (Ryan Graham) [#2095](https://github.com/nodejs/io.js/pull/2095)
* \[[`844d3f0e3e`](https://github.com/nodejs/node/commit/844d3f0e3e)] - **path**: use '===' instead of '==' for comparison (Sam Stites) [#2388](https://github.com/nodejs/node/pull/2388)
* \[[`7118b8a882`](https://github.com/nodejs/node/commit/7118b8a882)] - **path**: remove dead code in favor of unit tests (Nathan Woltman) [#2282](https://github.com/nodejs/io.js/pull/2282)
* \[[`34f2cfa806`](https://github.com/nodejs/node/commit/34f2cfa806)] - **src**: better error message on failed Buffer malloc (Karl Skomski) [#2422](https://github.com/nodejs/node/pull/2422)
* \[[`b196c1da3c`](https://github.com/nodejs/node/commit/b196c1da3c)] - **src**: fix memory leak in DLOpen (Karl Skomski) [#2375](https://github.com/nodejs/node/pull/2375)
* \[[`d1307b2995`](https://github.com/nodejs/node/commit/d1307b2995)] - **src**: don't use fopen() in require() fast path (Ben Noordhuis) [#2377](https://github.com/nodejs/node/pull/2377)
* \[[`455ec570d1`](https://github.com/nodejs/node/commit/455ec570d1)] - **src**: rename Buffer::Use() to Buffer::New() (Ben Noordhuis) [#2352](https://github.com/nodejs/node/pull/2352)
* \[[`fd63e1ce2b`](https://github.com/nodejs/node/commit/fd63e1ce2b)] - **src**: introduce internal Buffer::Copy() function (Ben Noordhuis) [#2352](https://github.com/nodejs/node/pull/2352)
* \[[`5586ceca13`](https://github.com/nodejs/node/commit/5586ceca13)] - **src**: move internal functions out of node\_buffer.h (Ben Noordhuis) [#2352](https://github.com/nodejs/node/pull/2352)
* \[[`bff9bcddb6`](https://github.com/nodejs/node/commit/bff9bcddb6)] - **src**: plug memory leaks (Ben Noordhuis) [#2352](https://github.com/nodejs/node/pull/2352)
* \[[`ccf12df4f3`](https://github.com/nodejs/node/commit/ccf12df4f3)] - **(SEMVER-MINOR)** **src**: add total\_available\_size to v8 statistics (Roman Klauke) [#2348](https://github.com/nodejs/io.js/pull/2348)
* \[[`194eeb841b`](https://github.com/nodejs/node/commit/194eeb841b)] - **test**: drop Isolate::GetCurrent() from addon tests (Ben Noordhuis) [#2427](https://github.com/nodejs/node/pull/2427)
* \[[`46cdb2f6e2`](https://github.com/nodejs/node/commit/46cdb2f6e2)] - **test**: lint addon tests (Ben Noordhuis) [#2427](https://github.com/nodejs/node/pull/2427)
* \[[`850c794882`](https://github.com/nodejs/node/commit/850c794882)] - **test**: refactor test-fs-watchfile.js (Rich Trott) [#2393](https://github.com/nodejs/node/pull/2393)
* \[[`a3160c0a33`](https://github.com/nodejs/node/commit/a3160c0a33)] - **test**: correct spelling of 'childProcess' (muddletoes) [#2389](https://github.com/nodejs/node/pull/2389)
* \[[`e51f90d747`](https://github.com/nodejs/node/commit/e51f90d747)] - **test**: option to run a subset of tests (João Reis) [#2260](https://github.com/nodejs/io.js/pull/2260)
* \[[`cc46d3bca3`](https://github.com/nodejs/node/commit/cc46d3bca3)] - **test**: clarify dropMembership() call (Rich Trott) [#2062](https://github.com/nodejs/io.js/pull/2062)
* \[[`0ee4df9c7a`](https://github.com/nodejs/node/commit/0ee4df9c7a)] - **test**: make listen-fd-cluster/server more robust (Sam Roberts) [#1944](https://github.com/nodejs/io.js/pull/1944)
* \[[`cf9ba81398`](https://github.com/nodejs/node/commit/cf9ba81398)] - **test**: address timing issues in simple http tests (Gireesh Punathil) [#2294](https://github.com/nodejs/io.js/pull/2294)
* \[[`cbb75c4f86`](https://github.com/nodejs/node/commit/cbb75c4f86)] - **tls**: fix throughput issues after incorrect merge (Fedor Indutny) [#2381](https://github.com/nodejs/node/pull/2381)
* \[[`94b765f409`](https://github.com/nodejs/node/commit/94b765f409)] - **tls**: fix check for reused session (Fedor Indutny) [#2312](https://github.com/nodejs/io.js/pull/2312)
* \[[`e83a41ad65`](https://github.com/nodejs/node/commit/e83a41ad65)] - **tls**: introduce internal `onticketkeycallback` (Fedor Indutny) [#2312](https://github.com/nodejs/io.js/pull/2312)
* \[[`fb0f5d733f`](https://github.com/nodejs/node/commit/fb0f5d733f)] - **(SEMVER-MINOR)** **tools**: run the tick processor without building v8 (Matt Loring) [#2090](https://github.com/nodejs/node/pull/2090)
* \[[`7606bdb897`](https://github.com/nodejs/node/commit/7606bdb897)] - **(SEMVER-MINOR)** **util**: display constructor when inspecting objects (Christopher Monsanto) [#1935](https://github.com/nodejs/io.js/pull/1935)

<a id="3.0.0"></a>

## 2015-08-04, Version 3.0.0, @rvagg

### Notable changes

* **buffer**:
  * Due to changes in V8, it has been necessary to reimplement `Buffer` on top of V8's `Uint8Array`. Every effort has been made to minimize the performance impact, however `Buffer` instantiation is measurably slower. Access operations may be faster in some circumstances but the exact performance profile and difference over previous versions will depend on how `Buffer` is used within applications. (Trevor Norris) [#1825](https://github.com/nodejs/node/pull/1825).
  * `Buffer` can now take `ArrayBuffer`s as a constructor argument (Trevor Norris) [#2002](https://github.com/nodejs/node/pull/2002).
  * When a single buffer is passed to `Buffer.concat()`, a new, copied `Buffer` object will be returned; previous behavior was to return the original `Buffer` object (Sakthipriyan Vairamani) [#1937](https://github.com/nodejs/node/pull/1937).
* **build**: PPC support has been added to core to allow compiling on pLinux BE and LE (AIX support coming soon) (Michael Dawson) [#2124](https://github.com/nodejs/node/pull/2124).
* **dgram**: If an error occurs within `socket.send()` and a callback has been provided, the error is only passed as the first argument to the callback and not emitted on the `socket` object; previous behavior was to do both (Matteo Collina & Chris Dickinson) [#1796](https://github.com/nodejs/node/pull/1796)
* **freelist**: Deprecate the undocumented `freelist` core module (Sakthipriyan Vairamani) [#2176](https://github.com/nodejs/node/pull/2176).
* **http**:
  * Status codes now all use the official [IANA names](https://www.iana.org/assignments/http-status-codes) as per [RFC7231](https://tools.ietf.org/html/rfc7231), e.g. `http.STATUS_CODES[414]` now returns `'URI Too Long'` rather than `'Request-URI Too Large'` (jomo) [#1470](https://github.com/nodejs/node/pull/1470).
  * Calling .getName() on an HTTP agent no longer returns a trailing colon, HTTPS agents will no longer return an extra colon near the middle of the string (Brendan Ashworth) [#1617](https://github.com/nodejs/node/pull/1617).
* **node**:
  * `NODE_MODULE_VERSION` has been bumped to `45` to reflect the break in ABI (Rod Vagg) [#2096](https://github.com/nodejs/node/pull/2096).
  * Introduce a new `process.release` object that contains a `name` property set to `'io.js'` and `sourceUrl`, `headersUrl` and `libUrl` (Windows only) properties containing URLs for the relevant resources; this is intended to be used by node-gyp (Rod Vagg) [#2154](https://github.com/nodejs/node/pull/2154).
  * The version of node-gyp bundled with io.js now downloads and uses a tarball of header files from iojs.org rather than the full source for compiling native add-ons; it is hoped this is a temporary floating patch and the change will be upstreamed to node-gyp soon (Rod Vagg) [#2066](https://github.com/nodejs/node/pull/2066).
* **repl**: Persistent history is now enabled by default. The history file is located at \~/.node\_repl\_history, which can be overridden by the new environment variable `NODE_REPL_HISTORY`. This deprecates the previous `NODE_REPL_HISTORY_FILE` variable. Additionally, the format of the file has been changed to plain text to better handle file corruption. (Jeremiah Senkpiel) [#2224](https://github.com/nodejs/node/pull/2224).
* **smalloc**: The `smalloc` module has been removed as it is no longer possible to provide the API due to changes in V8 (Ben Noordhuis) [#2022](https://github.com/nodejs/node/pull/2022).
* **tls**: Add `server.getTicketKeys()` and `server.setTicketKeys()` methods for [TLS session key](https://www.ietf.org/rfc/rfc5077.txt) rotation (Fedor Indutny) [#2227](https://github.com/nodejs/node/pull/2227).
* **v8**: Upgraded to 4.4.63.26
  * ES6: Enabled [computed property names](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/Object_initializer#Computed_property_names)
  * ES6: `Array` can now be subclassed in strict mode
  * ES6: Implement [rest parameters](https://developer.mozilla.org/en/docs/Web/JavaScript/Reference/Functions/rest_parameters) in staging, use the `--harmony-rest-parameters` command line flag
  * ES6: Implement the [spread operator](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/Spread_operator) in staging, use the `--harmony-spreadcalls` command line flag
  * Removed `SetIndexedPropertiesToExternalArrayData` and related APIs, forcing a shift to `Buffer` to be reimplemented based on `Uint8Array`
  * Introduction of `Maybe` and `MaybeLocal` C++ API for objects which _may_ or _may not_ have a value.
  * Added support for PPC

See also <https://github.com/nodejs/node/wiki/Breaking-Changes#300-from-2x> for a summary of the breaking changes (SEMVER-MAJOR).

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760).
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`60a974d200`](https://github.com/nodejs/node/commit/60a974d200)] - **buffer**: fix missing null/undefined check (Trevor Norris) [#2195](https://github.com/nodejs/node/pull/2195)
* \[[`e6ab2d92bc`](https://github.com/nodejs/node/commit/e6ab2d92bc)] - **buffer**: fix not return on error (Trevor Norris) [#2225](https://github.com/nodejs/node/pull/2225)
* \[[`1057d1186b`](https://github.com/nodejs/node/commit/1057d1186b)] - **buffer**: rename internal/buffer\_new\.js to buffer.js (Ben Noordhuis) [#2022](https://github.com/nodejs/node/pull/2022)
* \[[`4643b8b667`](https://github.com/nodejs/node/commit/4643b8b667)] - **(SEMVER-MINOR)** **buffer**: allow ArrayBuffer as Buffer argument (Trevor Norris) [#2002](https://github.com/nodejs/node/pull/2002)
* \[[`e5ada116cd`](https://github.com/nodejs/node/commit/e5ada116cd)] - **buffer**: minor cleanup from rebase (Trevor Norris) [#2003](https://github.com/nodejs/node/pull/2003)
* \[[`b625ab4242`](https://github.com/nodejs/node/commit/b625ab4242)] - **buffer**: fix usage of kMaxLength (Trevor Norris) [#2003](https://github.com/nodejs/node/pull/2003)
* \[[`eea66e2a7b`](https://github.com/nodejs/node/commit/eea66e2a7b)] - **(SEMVER-MAJOR)** **buffer**: fix case of one buffer passed to concat (Sakthipriyan Vairamani) [#1937](https://github.com/nodejs/node/pull/1937)
* \[[`8664084166`](https://github.com/nodejs/node/commit/8664084166)] - **buffer**: make additional changes to native API (Trevor Norris) [#1825](https://github.com/nodejs/node/pull/1825)
* \[[`36f78f4c1c`](https://github.com/nodejs/node/commit/36f78f4c1c)] - **buffer**: switch API to return MaybeLocal\<T> (Trevor Norris) [#1825](https://github.com/nodejs/node/pull/1825)
* \[[`571ec13841`](https://github.com/nodejs/node/commit/571ec13841)] - **buffer**: switch to using Maybe\<T> API (Trevor Norris) [#1825](https://github.com/nodejs/node/pull/1825)
* \[[`d75f5c8d0e`](https://github.com/nodejs/node/commit/d75f5c8d0e)] - **buffer**: finish implementing FreeCallback (Trevor Norris) [#1825](https://github.com/nodejs/node/pull/1825)
* \[[`63da0dfd3a`](https://github.com/nodejs/node/commit/63da0dfd3a)] - **buffer**: implement Uint8Array backed Buffer (Trevor Norris) [#1825](https://github.com/nodejs/node/pull/1825)
* \[[`23be6ca189`](https://github.com/nodejs/node/commit/23be6ca189)] - **buffer**: allow ARGS\_THIS to accept a name (Trevor Norris) [#1825](https://github.com/nodejs/node/pull/1825)
* \[[`971de5e417`](https://github.com/nodejs/node/commit/971de5e417)] - **build**: prepare Windows installer for i18n support (Frederic Hemberger) [#2247](https://github.com/nodejs/node/pull/2247)
* \[[`2ba8b23661`](https://github.com/nodejs/node/commit/2ba8b23661)] - **build**: add 'x86' option back in to configure (Rod Vagg) [#2233](https://github.com/nodejs/node/pull/2233)
* \[[`b4226e797a`](https://github.com/nodejs/node/commit/b4226e797a)] - **build**: first set of updates to enable PPC support (Michael Dawson) [#2124](https://github.com/nodejs/node/pull/2124)
* \[[`24dd016deb`](https://github.com/nodejs/node/commit/24dd016deb)] - **build**: produce symbol map files on windows (Ali Ijaz Sheikh) [#2243](https://github.com/nodejs/node/pull/2243)
* \[[`423d8944ce`](https://github.com/nodejs/node/commit/423d8944ce)] - **cluster**: do not unconditionally set --debug-port (cjihrig) [#1949](https://github.com/nodejs/node/pull/1949)
* \[[`fa98b97171`](https://github.com/nodejs/node/commit/fa98b97171)] - **cluster**: add handle ref/unref stubs in rr mode (Ben Noordhuis) [#2274](https://github.com/nodejs/node/pull/2274)
* \[[`944f68046c`](https://github.com/nodejs/node/commit/944f68046c)] - **crypto**: remove kMaxLength on randomBytes() (Trevor Norris) [#1825](https://github.com/nodejs/node/pull/1825)
* \[[`3d3c687012`](https://github.com/nodejs/node/commit/3d3c687012)] - **deps**: update V8 to 4.4.63.26 (Michaël Zasso) [#2220](https://github.com/nodejs/node/pull/2220)
* \[[`3aad4fa89a`](https://github.com/nodejs/node/commit/3aad4fa89a)] - **deps**: upgrade v8 to 4.4.63.12 (Ben Noordhuis) [#2092](https://github.com/nodejs/node/pull/2092)
* \[[`70d1f32f56`](https://github.com/nodejs/node/commit/70d1f32f56)] - **(SEMVER-MAJOR)** **deps**: update v8 to 4.4.63.9 (Ben Noordhuis) [#2022](https://github.com/nodejs/node/pull/2022)
* \[[`deb7ee93a7`](https://github.com/nodejs/node/commit/deb7ee93a7)] - **deps**: backport 7b24219346 from v8 upstream (Rod Vagg) [#1805](https://github.com/nodejs/node/pull/1805)
* \[[`d58e780504`](https://github.com/nodejs/node/commit/d58e780504)] - **(SEMVER-MAJOR)** **deps**: update v8 to 4.3.61.21 (Chris Dickinson) [iojs/io.js#1632](https://github.com/iojs/io.js/pull/1632)
* \[[`2a63cf612b`](https://github.com/nodejs/node/commit/2a63cf612b)] - **deps**: make node-gyp work with io.js (cjihrig) [iojs/io.js#990](https://github.com/iojs/io.js/pull/990)
* \[[`bf63266460`](https://github.com/nodejs/node/commit/bf63266460)] - **deps**: upgrade to npm 2.13.3 (Kat Marchán) [#2284](https://github.com/nodejs/node/pull/2284)
* \[[`ef2c8cd4ec`](https://github.com/nodejs/node/commit/ef2c8cd4ec)] - **(SEMVER-MAJOR)** **dgram**: make send cb act as "error" event handler (Matteo Collina) [#1796](https://github.com/nodejs/node/pull/1796)
* \[[`3da057fef6`](https://github.com/nodejs/node/commit/3da057fef6)] - **(SEMVER-MAJOR)** **dgram**: make send cb act as "error" event handler (Chris Dickinson) [#1796](https://github.com/nodejs/node/pull/1796)
* \[[`df1994fe53`](https://github.com/nodejs/node/commit/df1994fe53)] - _**Revert**_ "**dns**: remove AI\_V4MAPPED hint flag on FreeBSD" (cjihrig) [iojs/io.js#1555](https://github.com/iojs/io.js/pull/1555)
* \[[`1721968b22`](https://github.com/nodejs/node/commit/1721968b22)] - **doc**: document repl persistent history changes (Jeremiah Senkpiel) [#2224](https://github.com/nodejs/node/pull/2224)
* \[[`d12df7f159`](https://github.com/nodejs/node/commit/d12df7f159)] - **doc**: update v8 flags in man page (Michaël Zasso) [iojs/io.js#1701](https://github.com/iojs/io.js/pull/1701)
* \[[`d168d01b04`](https://github.com/nodejs/node/commit/d168d01b04)] - **doc**: properly inheriting from EventEmitter (Sakthipriyan Vairamani) [#2168](https://github.com/nodejs/node/pull/2168)
* \[[`500f2538cc`](https://github.com/nodejs/node/commit/500f2538cc)] - **doc**: a listener, not "an" listener (Sam Roberts) [#1025](https://github.com/nodejs/node/pull/1025)
* \[[`54627a919d`](https://github.com/nodejs/node/commit/54627a919d)] - **doc**: server close event does not have an argument (Sam Roberts) [#1025](https://github.com/nodejs/node/pull/1025)
* \[[`ed85c95a9c`](https://github.com/nodejs/node/commit/ed85c95a9c)] - **doc,test**: documents behaviour of non-existent file (Sakthipriyan Vairamani) [#2169](https://github.com/nodejs/node/pull/2169)
* \[[`2965442308`](https://github.com/nodejs/node/commit/2965442308)] - **(SEMVER-MAJOR)** **http**: fix agent.getName() and add tests (Brendan Ashworth) [#1617](https://github.com/nodejs/node/pull/1617)
* \[[`2d9456e3e6`](https://github.com/nodejs/node/commit/2d9456e3e6)] - **(SEMVER-MAJOR)** **http**: use official IANA Status Codes (jomo) [#1470](https://github.com/nodejs/node/pull/1470)
* \[[`11e4249227`](https://github.com/nodejs/node/commit/11e4249227)] - **(SEMVER-MAJOR)** **http\_server**: `prefinish` vs `finish` (Fedor Indutny) [#1411](https://github.com/nodejs/node/pull/1411)
* \[[`9bc2e26720`](https://github.com/nodejs/node/commit/9bc2e26720)] - **net**: do not set V4MAPPED on FreeBSD (Julien Gilli) [iojs/io.js#1555](https://github.com/iojs/io.js/pull/1555)
* \[[`ba9ccf227e`](https://github.com/nodejs/node/commit/ba9ccf227e)] - **node**: remove redundant --use-old-buffer (Rod Vagg) [#2275](https://github.com/nodejs/node/pull/2275)
* \[[`ef65321083`](https://github.com/nodejs/node/commit/ef65321083)] - **(SEMVER-MAJOR)** **node**: do not override `message`/`stack` of error (Fedor Indutny) [#2108](https://github.com/nodejs/node/pull/2108)
* \[[`9f727f5e03`](https://github.com/nodejs/node/commit/9f727f5e03)] - **node-gyp**: detect RC build with x.y.z-rc.n format (Rod Vagg) [#2171](https://github.com/nodejs/node/pull/2171)
* \[[`e52f963632`](https://github.com/nodejs/node/commit/e52f963632)] - **node-gyp**: download header tarball for compile (Rod Vagg) [#2066](https://github.com/nodejs/node/pull/2066)
* \[[`902c9ca51d`](https://github.com/nodejs/node/commit/902c9ca51d)] - **node-gyp**: make aware of nightly, next-nightly & rc (Rod Vagg) [#2066](https://github.com/nodejs/node/pull/2066)
* \[[`4cffaa3f55`](https://github.com/nodejs/node/commit/4cffaa3f55)] - **(SEMVER-MINOR)** **readline**: allow tabs in input (Rich Trott) [#1761](https://github.com/nodejs/node/pull/1761)
* \[[`ed6c249104`](https://github.com/nodejs/node/commit/ed6c249104)] - **(SEMVER-MAJOR)** **repl**: persist history in plain text (Jeremiah Senkpiel) [#2224](https://github.com/nodejs/node/pull/2224)
* \[[`f7d5e4c618`](https://github.com/nodejs/node/commit/f7d5e4c618)] - **(SEMVER-MINOR)** **repl**: default persistence to \~/.node\_repl\_history (Jeremiah Senkpiel) [#2224](https://github.com/nodejs/node/pull/2224)
* \[[`ea05e760cd`](https://github.com/nodejs/node/commit/ea05e760cd)] - **repl**: don't clobber RegExp.$ properties (Sakthipriyan Vairamani) [#2137](https://github.com/nodejs/node/pull/2137)
* \[[`d20093246b`](https://github.com/nodejs/node/commit/d20093246b)] - **src**: disable vector ICs on arm (Michaël Zasso) [#2220](https://github.com/nodejs/node/pull/2220)
* \[[`04fd4fad46`](https://github.com/nodejs/node/commit/04fd4fad46)] - **(SEMVER-MINOR)** **src**: introduce process.release object (Rod Vagg) [#2154](https://github.com/nodejs/node/pull/2154)
* \[[`9d34bd1147`](https://github.com/nodejs/node/commit/9d34bd1147)] - **src**: increment NODE\_MODULE\_VERSION to 45 (Rod Vagg) [#2096](https://github.com/nodejs/node/pull/2096)
* \[[`ceee8d2807`](https://github.com/nodejs/node/commit/ceee8d2807)] - **test**: add tests for persistent repl history (Jeremiah Senkpiel) [#2224](https://github.com/nodejs/node/pull/2224)
* \[[`8e1a8ffe24`](https://github.com/nodejs/node/commit/8e1a8ffe24)] - **test**: remove two obsolete pummel tests (Ben Noordhuis) [#2022](https://github.com/nodejs/node/pull/2022)
* \[[`ae731ec0fa`](https://github.com/nodejs/node/commit/ae731ec0fa)] - **test**: don't use arguments.callee (Ben Noordhuis) [#2022](https://github.com/nodejs/node/pull/2022)
* \[[`21d31c08e7`](https://github.com/nodejs/node/commit/21d31c08e7)] - **test**: remove obsolete harmony flags (Chris Dickinson)
* \[[`64cf71195c`](https://github.com/nodejs/node/commit/64cf71195c)] - **test**: change the hostname to an invalid name (Sakthipriyan Vairamani) [#2287](https://github.com/nodejs/node/pull/2287)
* \[[`80a1cf7425`](https://github.com/nodejs/node/commit/80a1cf7425)] - **test**: fix messages and use return to skip tests (Sakthipriyan Vairamani) [#2290](https://github.com/nodejs/node/pull/2290)
* \[[`d5ab92bcc1`](https://github.com/nodejs/node/commit/d5ab92bcc1)] - **test**: use common.isWindows consistently (Sakthipriyan Vairamani) [#2269](https://github.com/nodejs/node/pull/2269)
* \[[`bc733f7065`](https://github.com/nodejs/node/commit/bc733f7065)] - **test**: fix fs.readFile('/dev/stdin') tests (Ben Noordhuis) [#2265](https://github.com/nodejs/node/pull/2265)
* \[[`3cbb5870e5`](https://github.com/nodejs/node/commit/3cbb5870e5)] - **tools**: expose skip output to test runner (Johan Bergström) [#2130](https://github.com/nodejs/node/pull/2130)
* \[[`3b021efe11`](https://github.com/nodejs/node/commit/3b021efe11)] - **vm**: fix symbol access (Domenic Denicola) [#1773](https://github.com/nodejs/node/pull/1773)
* \[[`7b81e4ba36`](https://github.com/nodejs/node/commit/7b81e4ba36)] - **vm**: remove unnecessary access checks (Domenic Denicola) [#1773](https://github.com/nodejs/node/pull/1773)
* \[[`659dadd410`](https://github.com/nodejs/node/commit/659dadd410)] - **vm**: fix property descriptors of sandbox properties (Domenic Denicola) [#1773](https://github.com/nodejs/node/pull/1773)
* \[[`9bac1dbae9`](https://github.com/nodejs/node/commit/9bac1dbae9)] - **win,node-gyp**: enable delay-load hook by default (Bert Belder) [iojs/io.js#1433](https://github.com/iojs/io.js/pull/1433)

<a id="2.5.0"></a>

## 2015-07-28, Version 2.5.0, @cjihrig

### Notable changes

* **https**: TLS sessions in Agent are reused (Fedor Indutny) [#2228](https://github.com/nodejs/node/pull/2228)
* **src**: base64 decoding is now 50% faster (Ben Noordhuis) [#2193](https://github.com/nodejs/node/pull/2193)
* **npm**: Upgraded to v2.13.2, release notes can be found in <https://github.com/npm/npm/releases/tag/v2.13.2> (Kat Marchán) [#2241](https://github.com/nodejs/node/pull/2241).

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Using multiple REPL instances in parallel may cause some REPL history corruption or loss. [#1634](https://github.com/nodejs/node/issues/1634)
* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760).
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`bf2cd225a8`](https://github.com/nodejs/node/commit/bf2cd225a8)] - **process**: resize stderr on SIGWINCH (Jeremiah Senkpiel) [#2231](https://github.com/nodejs/node/pull/2231)
* \[[`99d9d7e716`](https://github.com/nodejs/node/commit/99d9d7e716)] - **benchmark**: add remaining path benchmarks & optimize (Nathan Woltman) [#2103](https://github.com/nodejs/node/pull/2103)
* \[[`66fc8ca22b`](https://github.com/nodejs/node/commit/66fc8ca22b)] - **(SEMVER-MINOR)** **cluster**: emit 'message' event on cluster master (Sam Roberts) [#861](https://github.com/nodejs/node/pull/861)
* \[[`eb35968de7`](https://github.com/nodejs/node/commit/eb35968de7)] - **crypto**: fix legacy SNICallback (Fedor Indutny) [#1720](https://github.com/nodejs/node/pull/1720)
* \[[`fef190cea6`](https://github.com/nodejs/node/commit/fef190cea6)] - **deps**: make node-gyp work with io.js (cjihrig) [iojs/io.js#990](https://github.com/iojs/io.js/pull/990)
* \[[`b73a7465c5`](https://github.com/nodejs/node/commit/b73a7465c5)] - **deps**: upgrade to npm 2.13.2 (Kat Marchán) [#2241](https://github.com/nodejs/node/pull/2241)
* \[[`0a7bf81d2f`](https://github.com/nodejs/node/commit/0a7bf81d2f)] - **deps**: update V8 to 4.2.77.21 (Ali Ijaz Sheikh) [#2238](https://github.com/nodejs/node/issues/2238)
* \[[`73cdcdd581`](https://github.com/nodejs/node/commit/73cdcdd581)] - **deps**: make node-gyp work with io.js (cjihrig) [iojs/io.js#990](https://github.com/iojs/io.js/pull/990)
* \[[`04893a736d`](https://github.com/nodejs/node/commit/04893a736d)] - **deps**: upgrade to npm 2.13.1 (Kat Marchán) [#2210](https://github.com/nodejs/node/pull/2210)
* \[[`a3c1b9720e`](https://github.com/nodejs/node/commit/a3c1b9720e)] - **doc**: add GPG fingerprint for cjihrig (cjihrig) [#2217](https://github.com/nodejs/node/pull/2217)
* \[[`d9f857df3b`](https://github.com/nodejs/node/commit/d9f857df3b)] - **doc**: note about custom inspect functions (Sakthipriyan Vairamani) [#2142](https://github.com/nodejs/node/pull/2142)
* \[[`4ef2b5fbfb`](https://github.com/nodejs/node/commit/4ef2b5fbfb)] - **doc**: Replace util.debug with console.error (Yosuke Furukawa) [#2214](https://github.com/nodejs/node/pull/2214)
* \[[`b612f085ec`](https://github.com/nodejs/node/commit/b612f085ec)] - **doc**: add joaocgreis as a collaborator (João Reis) [#2208](https://github.com/nodejs/node/pull/2208)
* \[[`6b85d5a4b3`](https://github.com/nodejs/node/commit/6b85d5a4b3)] - **doc**: add TSC meeting minutes 2015-07-15 (Rod Vagg) [#2191](https://github.com/nodejs/node/pull/2191)
* \[[`c7d8b09162`](https://github.com/nodejs/node/commit/c7d8b09162)] - **doc**: recompile before testing core module changes (Phillip Johnsen) [#2051](https://github.com/nodejs/node/pull/2051)
* \[[`9afee6785e`](https://github.com/nodejs/node/commit/9afee6785e)] - **http**: Check this.connection before using it (Sakthipriyan Vairamani) [#2172](https://github.com/nodejs/node/pull/2172)
* \[[`2ca5a3db47`](https://github.com/nodejs/node/commit/2ca5a3db47)] - **https**: reuse TLS sessions in Agent (Fedor Indutny) [#2228](https://github.com/nodejs/node/pull/2228)
* \[[`fef87fee1d`](https://github.com/nodejs/node/commit/fef87fee1d)] - **(SEMVER-MINOR)** **lib,test**: add freelist deprecation and test (Sakthipriyan Vairamani) [#2176](https://github.com/nodejs/node/pull/2176)
* \[[`503b089dd8`](https://github.com/nodejs/node/commit/503b089dd8)] - **net**: don't throw on immediately destroyed socket (Evan Lucas) [#2251](https://github.com/nodejs/node/pull/2251)
* \[[`93660c8b8e`](https://github.com/nodejs/node/commit/93660c8b8e)] - **node**: remove bad fn call and check (Trevor Norris) [#2157](https://github.com/nodejs/node/pull/2157)
* \[[`afd7e37ee0`](https://github.com/nodejs/node/commit/afd7e37ee0)] - **repl**: better empty line handling (Sakthipriyan Vairamani) [#2163](https://github.com/nodejs/node/pull/2163)
* \[[`81ea52aa01`](https://github.com/nodejs/node/commit/81ea52aa01)] - **repl**: improving line continuation handling (Sakthipriyan Vairamani) [#2163](https://github.com/nodejs/node/pull/2163)
* \[[`30edb5aee9`](https://github.com/nodejs/node/commit/30edb5aee9)] - **repl**: preventing REPL crash with inherited properties (Sakthipriyan Vairamani) [#2163](https://github.com/nodejs/node/pull/2163)
* \[[`77fa385e5d`](https://github.com/nodejs/node/commit/77fa385e5d)] - **repl**: fixing `undefined` in invalid REPL keyword error (Sakthipriyan Vairamani) [#2163](https://github.com/nodejs/node/pull/2163)
* \[[`8fd3ce100e`](https://github.com/nodejs/node/commit/8fd3ce100e)] - **src**: make base64 decoding 50% faster (Ben Noordhuis) [#2193](https://github.com/nodejs/node/pull/2193)
* \[[`c786d6341d`](https://github.com/nodejs/node/commit/c786d6341d)] - **test**: do not use public IPs for timeout testing (Rich Trott) [#2057](https://github.com/nodejs/node/pull/2057)
* \[[`4e78cd71c0`](https://github.com/nodejs/node/commit/4e78cd71c0)] - **test**: skip IPv6 part before testing it (Sakthipriyan Vairamani) [#2226](https://github.com/nodejs/node/pull/2226)
* \[[`ac70bc8240`](https://github.com/nodejs/node/commit/ac70bc8240)] - **test**: fix valgrind uninitialized memory warning (Ben Noordhuis) [#2193](https://github.com/nodejs/node/pull/2193)
* \[[`ac7d3fa0d9`](https://github.com/nodejs/node/commit/ac7d3fa0d9)] - **test**: add -no\_rand\_screen to s\_client opts on Win (Shigeki Ohtsu) [#2209](https://github.com/nodejs/node/pull/2209)
* \[[`79c865a53f`](https://github.com/nodejs/node/commit/79c865a53f)] - **test**: changing process.exit to return while skipping tests (Sakthipriyan Vairamani) [#2109](https://github.com/nodejs/node/pull/2109)
* \[[`69298d36cf`](https://github.com/nodejs/node/commit/69298d36cf)] - **test**: formatting skip messages for TAP parsing (Sakthipriyan Vairamani) [#2109](https://github.com/nodejs/node/pull/2109)
* \[[`543dabb609`](https://github.com/nodejs/node/commit/543dabb609)] - **timers**: improve Timer.now() performance (Ben Noordhuis) [#2256](https://github.com/nodejs/node/pull/2256)
* \[[`3663b124e6`](https://github.com/nodejs/node/commit/3663b124e6)] - **timers**: remove unused Timer.again() (Ben Noordhuis) [#2256](https://github.com/nodejs/node/pull/2256)
* \[[`bcce5cf9bb`](https://github.com/nodejs/node/commit/bcce5cf9bb)] - **timers**: remove unused Timer.getRepeat() (Ben Noordhuis) [#2256](https://github.com/nodejs/node/pull/2256)
* \[[`f2c83bd202`](https://github.com/nodejs/node/commit/f2c83bd202)] - **timers**: remove unused Timer.setRepeat() (Ben Noordhuis) [#2256](https://github.com/nodejs/node/pull/2256)
* \[[`e11fc67225`](https://github.com/nodejs/node/commit/e11fc67225)] - **(SEMVER-MINOR)** **tls**: add `getTicketKeys()`/`setTicketKeys()` (Fedor Indutny) [#2227](https://github.com/nodejs/node/pull/2227)
* \[[`68b06e94e3`](https://github.com/nodejs/node/commit/68b06e94e3)] - **tools**: use local or specified $NODE for test-npm (Jeremiah Senkpiel) [#1984](https://github.com/nodejs/node/pull/1984)
* \[[`ab479659c7`](https://github.com/nodejs/node/commit/ab479659c7)] - **util**: delay creation of debug context (Ali Ijaz Sheikh) [#2248](https://github.com/nodejs/node/pull/2248)
* \[[`6391f4d2fd`](https://github.com/nodejs/node/commit/6391f4d2fd)] - **util**: removing redundant checks in is\* functions (Sakthipriyan Vairamani) [#2179](https://github.com/nodejs/node/pull/2179)
* \[[`b148c0dff3`](https://github.com/nodejs/node/commit/b148c0dff3)] - **win,node-gyp**: enable delay-load hook by default (Bert Belder) [iojs/io.js#1433](https://github.com/iojs/io.js/pull/1433)
* \[[`f90f1e75bb`](https://github.com/nodejs/node/commit/f90f1e75bb)] - **win,node-gyp**: enable delay-load hook by default (Bert Belder) [iojs/io.js#1433](https://github.com/iojs/io.js/pull/1433)

<a id="2.4.0"></a>

## 2015-07-17, Version 2.4.0, @Fishrock123

### Notable changes

* **src**: Added a new `--track-heap-objects` flag to track heap object allocations for heap snapshots (Bradley Meck) [#2135](https://github.com/nodejs/node/pull/2135).
* **readline**: Fixed a freeze that affected the repl if the keypress event handler threw (Alex Kocharin) [#2107](https://github.com/nodejs/node/pull/2107).
* **npm**: Upgraded to v2.13.0, release notes can be found in <https://github.com/npm/npm/releases/tag/v2.13.0> (Forrest L Norvell) [#2152](https://github.com/nodejs/node/pull/2152).

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760).
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`f95f9ef6ea`](https://github.com/nodejs/node/commit/f95f9ef6ea)] - **build**: always use prefix=/ for tar-headers (Rod Vagg) [#2082](https://github.com/nodejs/node/pull/2082)
* \[[`12bc397207`](https://github.com/nodejs/node/commit/12bc397207)] - **build**: run-ci makefile rule (Alexis Campailla) [#2134](https://github.com/nodejs/node/pull/2134)
* \[[`84012c99e0`](https://github.com/nodejs/node/commit/84012c99e0)] - **build**: fix vcbuild merge issues (Alexis Campailla) [#2131](https://github.com/nodejs/node/pull/2131)
* \[[`47e2c5c828`](https://github.com/nodejs/node/commit/47e2c5c828)] - **build**: bail early if clean is invoked (Johan Bergström) [#2127](https://github.com/nodejs/node/pull/2127)
* \[[`5acad6b163`](https://github.com/nodejs/node/commit/5acad6b163)] - **child\_process**: fix arguments comments (Roman Reiss) [#2161](https://github.com/nodejs/node/pull/2161)
* \[[`3c4121c418`](https://github.com/nodejs/node/commit/3c4121c418)] - **deps**: make node-gyp work with io.js (cjihrig) [iojs/io.js#990](https://github.com/iojs/io.js/pull/990)
* \[[`938cc757bb`](https://github.com/nodejs/node/commit/938cc757bb)] - **deps**: upgrade to npm 2.13.0 (Forrest L Norvell) [#2152](https://github.com/nodejs/node/pull/2152)
* \[[`6f306e0ed2`](https://github.com/nodejs/node/commit/6f306e0ed2)] - **doc**: add targos as a collaborator (Michaël Zasso) [#2200](https://github.com/nodejs/node/pull/2200)
* \[[`c019d9a239`](https://github.com/nodejs/node/commit/c019d9a239)] - **doc**: add thefourtheye as a collaborator (Sakthipriyan Vairamani) [#2199](https://github.com/nodejs/node/pull/2199)
* \[[`4e92dbc26b`](https://github.com/nodejs/node/commit/4e92dbc26b)] - **doc**: add TSC members from the combined project (Jeremiah Senkpiel) [#2085](https://github.com/nodejs/node/pull/2085)
* \[[`6c3aabf455`](https://github.com/nodejs/node/commit/6c3aabf455)] - **doc**: add TSC meeting minutes 2015-07-08 (Rod Vagg) [#2184](https://github.com/nodejs/node/pull/2184)
* \[[`30a0d47d51`](https://github.com/nodejs/node/commit/30a0d47d51)] - **doc**: add TSC meeting minutes 2015-07-01 (Rod Vagg) [#2132](https://github.com/nodejs/node/pull/2132)
* \[[`23efb05cc3`](https://github.com/nodejs/node/commit/23efb05cc3)] - **doc**: document fs.watchFile behaviour on ENOENT (Brendan Ashworth) [#2093](https://github.com/nodejs/node/pull/2093)
* \[[`65963ec26f`](https://github.com/nodejs/node/commit/65963ec26f)] - **doc,test**: empty strings in path module (Sakthipriyan Vairamani) [#2106](https://github.com/nodejs/node/pull/2106)
* \[[`0ab81e6f58`](https://github.com/nodejs/node/commit/0ab81e6f58)] - **docs**: link to more up-to-date v8 docs (Jeremiah Senkpiel) [#2196](https://github.com/nodejs/node/pull/2196)
* \[[`1afc0c9e86`](https://github.com/nodejs/node/commit/1afc0c9e86)] - **fs**: fix error on bad listener type (Brendan Ashworth) [#2093](https://github.com/nodejs/node/pull/2093)
* \[[`2ba84606a6`](https://github.com/nodejs/node/commit/2ba84606a6)] - **path**: assert path.join() arguments equally (Phillip Johnsen) [#2159](https://github.com/nodejs/node/pull/2159)
* \[[`bd01603201`](https://github.com/nodejs/node/commit/bd01603201)] - **readline**: fix freeze if `keypress` event throws (Alex Kocharin) [#2107](https://github.com/nodejs/node/pull/2107)
* \[[`59f6b5da2a`](https://github.com/nodejs/node/commit/59f6b5da2a)] - **repl**: Prevent crash when tab-completed with Proxy (Sakthipriyan Vairamani) [#2120](https://github.com/nodejs/node/pull/2120)
* \[[`cf14a2427c`](https://github.com/nodejs/node/commit/cf14a2427c)] - **(SEMVER-MINOR)** **src**: add --track-heap-objects (Bradley Meck) [#2135](https://github.com/nodejs/node/pull/2135)
* \[[`2b4b600660`](https://github.com/nodejs/node/commit/2b4b600660)] - **test**: fix test-debug-port-from-cmdline (João Reis) [#2186](https://github.com/nodejs/node/pull/2186)
* \[[`d4ceb16da2`](https://github.com/nodejs/node/commit/d4ceb16da2)] - **test**: properly clean up temp directory (Roman Reiss) [#2164](https://github.com/nodejs/node/pull/2164)
* \[[`842eb5b853`](https://github.com/nodejs/node/commit/842eb5b853)] - **test**: add test for dgram.setTTL (Evan Lucas) [#2121](https://github.com/nodejs/node/pull/2121)
* \[[`cff7300a57`](https://github.com/nodejs/node/commit/cff7300a57)] - **win,node-gyp**: enable delay-load hook by default (Bert Belder) [iojs/io.js#1433](https://github.com/iojs/io.js/pull/1433)

<a id="2.3.4"></a>

## 2015-07-09, Version 2.3.4, @Fishrock123

### Notable changes

* **openssl**: Upgrade to 1.0.2d, fixes CVE-2015-1793 (Alternate Chains Certificate Forgery) (Shigeki Ohtsu) [#2141](https://github.com/nodejs/node/pull/2141).
* **npm**: Upgraded to v2.12.1, release notes can be found in <https://github.com/npm/npm/releases/tag/v2.12.0> and <https://github.com/npm/npm/releases/tag/v2.12.1> (Kat Marchán) [#2112](https://github.com/nodejs/node/pull/2112).

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760).
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`0d15161c24`](https://github.com/nodejs/node/commit/0d15161c24)] - **benchmark**: Add some path benchmarks for #1778 (Nathan Woltman) [#1778](https://github.com/nodejs/node/pull/1778)
* \[[`c70e68fa32`](https://github.com/nodejs/node/commit/c70e68fa32)] - **deps**: update deps/openssl/conf/arch/\*/opensslconf.h (Shigeki Ohtsu) [#2141](https://github.com/nodejs/node/pull/2141)
* \[[`ca93f7f2e6`](https://github.com/nodejs/node/commit/ca93f7f2e6)] - **deps**: upgrade openssl sources to 1.0.2d (Shigeki Ohtsu) [#2141](https://github.com/nodejs/node/pull/2141)
* \[[`b18c841ec1`](https://github.com/nodejs/node/commit/b18c841ec1)] - **deps**: make node-gyp work with io.js (cjihrig) [iojs/io.js#990](https://github.com/iojs/io.js/pull/990)
* \[[`863cdbdd08`](https://github.com/nodejs/node/commit/863cdbdd08)] - **deps**: upgrade to npm 2.12.1 (Kat Marchán) [#2112](https://github.com/nodejs/node/pull/2112)
* \[[`84b3915764`](https://github.com/nodejs/node/commit/84b3915764)] - **doc**: document current release procedure (Rod Vagg) [#2099](https://github.com/nodejs/node/pull/2099)
* \[[`46140334cd`](https://github.com/nodejs/node/commit/46140334cd)] - **doc**: update AUTHORS list (Rod Vagg) [#2100](https://github.com/nodejs/node/pull/2100)
* \[[`bca53dce76`](https://github.com/nodejs/node/commit/bca53dce76)] - **path**: refactor for performance and consistency (Nathan Woltman) [#1778](https://github.com/nodejs/node/pull/1778)
* \[[`6bef15afe7`](https://github.com/nodejs/node/commit/6bef15afe7)] - **src**: remove traceSyncIO property from process (Bradley Meck) [#2143](https://github.com/nodejs/node/pull/2143)
* \[[`2ba1740ba1`](https://github.com/nodejs/node/commit/2ba1740ba1)] - **test**: add missing crypto checks (Johan Bergström) [#2129](https://github.com/nodejs/node/pull/2129)
* \[[`180fd392ca`](https://github.com/nodejs/node/commit/180fd392ca)] - **test**: refactor test-repl-tab-complete (Sakthipriyan Vairamani) [#2122](https://github.com/nodejs/node/pull/2122)
* \[[`fb05c8e27d`](https://github.com/nodejs/node/commit/fb05c8e27d)] - _**Revert**_ "**test**: add test for missing `close`/`finish` event" (Fedor Indutny)
* \[[`9436a860cb`](https://github.com/nodejs/node/commit/9436a860cb)] - **test**: add test for missing `close`/`finish` event (Mark Plomer) [iojs/io.js#1373](https://github.com/iojs/io.js/pull/1373)
* \[[`ee3ce2ed88`](https://github.com/nodejs/node/commit/ee3ce2ed88)] - **tools**: install gdbinit from v8 to $PREFIX/share (Ali Ijaz Sheikh) [#2123](https://github.com/nodejs/node/pull/2123)
* \[[`dd523c75da`](https://github.com/nodejs/node/commit/dd523c75da)] - **win,node-gyp**: enable delay-load hook by default (Bert Belder) [iojs/io.js#1433](https://github.com/iojs/io.js/pull/1433)

<a id="1.8.4"></a>

## 2015-07-09, Version 1.8.4, @Fishrock123

**Maintenance release**

### Notable changes

* **openssl**: Upgrade to 1.0.2d, fixes CVE-2015-1793 (Alternate Chains Certificate Forgery) [#2141](https://github.com/nodejs/node/pull/2141).

### Known issues

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).
* readline: split escapes are processed incorrectly, see [#1403](https://github.com/nodejs/node/issues/1403)

### Commits

* \[[`c70e68fa32`](https://github.com/nodejs/node/commit/c70e68fa32)] - **deps**: update deps/openssl/conf/arch/\*/opensslconf.h (Shigeki Ohtsu) [#2141](https://github.com/nodejs/node/pull/2141)
* \[[`ca93f7f2e6`](https://github.com/nodejs/node/commit/ca93f7f2e6)] - **deps**: upgrade openssl sources to 1.0.2d (Shigeki Ohtsu) [#2141](https://github.com/nodejs/node/pull/2141)

<a id="2.3.3"></a>

## 2015-07-04, Version 2.3.3, @Fishrock123

### Notable changes

* **deps**: Fixed an out-of-band write in utf8 decoder. **This is an important security update** as it can be used to cause a denial of service attack.

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal. [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760).
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion. [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

## Commits

* \[[`030f8045c7`](https://github.com/nodejs/node/commit/030f8045c7)] - **deps**: fix out-of-band write in utf8 decoder (Fedor Indutny)
* \[[`0f09b8db28`](https://github.com/nodejs/node/commit/0f09b8db28)] - **doc**: don't recommend domains for error handling (Benjamin Gruenbaum) [#2056](https://github.com/nodejs/node/pull/2056)
* \[[`9cd44bb2b6`](https://github.com/nodejs/node/commit/9cd44bb2b6)] - **util**: prepend '(node) ' to deprecation messages (Sakthipriyan Vairamani) [#1892](https://github.com/nodejs/node/pull/1892)

<a id="1.8.3"></a>

## 2015-07-04, Version 1.8.3, @rvagg

**Maintenance release**

## Notable changes

* **v8**: Fixed an out-of-band write in utf8 decoder. **This is an important security update** as it can be used to cause a denial of service attack.
* **openssl**: Upgrade to 1.0.2b and 1.0.2c, introduces DHE man-in-the-middle protection (Logjam) and fixes malformed ECParameters causing infinite loop (CVE-2015-1788). See the [security advisory](https://www.openssl.org/news/secadv_20150611.txt) for full details. (Shigeki Ohtsu) [#1950](https://github.com/nodejs/node/pull/1950) [#1958](https://github.com/nodejs/node/pull/1958)
* **build**:
  * Added support for compiling with Microsoft Visual C++ 2015
  * Started building and distributing headers-only tarballs along with binaries

### Known issues

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).
* readline: split escapes are processed incorrectly, see [#1403](https://github.com/nodejs/node/issues/1403)

### Commits

* \[[`d8f260d33b`](https://github.com/nodejs/node/commit/d8f260d33b)] - **build**: add tar-headers target for headers-only tar (Rod Vagg) [#1975](https://github.com/nodejs/node/pull/1975)
* \[[`00ba429674`](https://github.com/nodejs/node/commit/00ba429674)] - **build**: update build targets for io.js (Rod Vagg) [#1938](https://github.com/nodejs/node/pull/1938)
* \[[`39e2207ff1`](https://github.com/nodejs/node/commit/39e2207ff1)] - **build**: fix cherry-pick ooops, fix comment wording (Rod Vagg) [#2036](https://github.com/nodejs/node/pull/2036)
* \[[`561919a67a`](https://github.com/nodejs/node/commit/561919a67a)] - **build**: add MSVS 2015 support (Rod Vagg) [#2036](https://github.com/nodejs/node/pull/2036)
* \[[`8e1134c04c`](https://github.com/nodejs/node/commit/8e1134c04c)] - **build**: remove lint from test-ci on windows (Johan Bergström) [#2004](https://github.com/nodejs/node/pull/2004)
* \[[`e52e99085e`](https://github.com/nodejs/node/commit/e52e99085e)] - **build**: don't run lint from test-ci (Johan Bergström) [#1965](https://github.com/nodejs/node/pull/1965)
* \[[`c5d1ec7fea`](https://github.com/nodejs/node/commit/c5d1ec7fea)] - **build**: simplify execution of built binary (Johan Bergström) [#1955](https://github.com/nodejs/node/pull/1955)
* \[[`2ce147551a`](https://github.com/nodejs/node/commit/2ce147551a)] - **build,win**: set env before generating projects (Alexis Campailla) [joyent/node#20109](https://github.com/joyent/node/pull/20109)
* \[[`78de5f85f2`](https://github.com/nodejs/node/commit/78de5f85f2)] - **deps**: fix out-of-band write in utf8 decoder (Ben Noordhuis)
* \[[`83ee07b6be`](https://github.com/nodejs/node/commit/83ee07b6be)] - **deps**: copy all openssl header files to include dir (Shigeki Ohtsu) [#2016](https://github.com/nodejs/node/pull/2016)
* \[[`a97125520d`](https://github.com/nodejs/node/commit/a97125520d)] - **deps**: update UPGRADING.md doc to openssl-1.0.2c (Shigeki Ohtsu) [#1958](https://github.com/nodejs/node/pull/1958)
* \[[`0e2d068e0b`](https://github.com/nodejs/node/commit/0e2d068e0b)] - **deps**: replace all headers in openssl (Shigeki Ohtsu) [#1958](https://github.com/nodejs/node/pull/1958)
* \[[`310b8d1120`](https://github.com/nodejs/node/commit/310b8d1120)] - **deps**: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) [#1836](https://github.com/nodejs/node/pull/1836)
* \[[`a472946747`](https://github.com/nodejs/node/commit/a472946747)] - **deps**: fix asm build error of openssl in x86\_win32 (Shigeki Ohtsu) [nodejs/node#1389](https://github.com/nodejs/node/pull/1389)
* \[[`b2467e3ebf`](https://github.com/nodejs/node/commit/b2467e3ebf)] - **deps**: fix openssl assembly error on ia32 win32 (Fedor Indutny) [nodejs/node#1389](https://github.com/nodejs/node/pull/1389)
* \[[`e548abb800`](https://github.com/nodejs/node/commit/e548abb800)] - **deps**: upgrade openssl sources to 1.0.2c (Shigeki Ohtsu) [#1958](https://github.com/nodejs/node/pull/1958)
* \[[`1feaa68e85`](https://github.com/nodejs/node/commit/1feaa68e85)] - **deps**: update asm files for openssl-1.0.2b (Shigeki Ohtsu) [#1950](https://github.com/nodejs/node/pull/1950)
* \[[`151720fae7`](https://github.com/nodejs/node/commit/151720fae7)] - **deps**: replace all headers in openssl (Shigeki Ohtsu) [#1950](https://github.com/nodejs/node/pull/1950)
* \[[`139da6a02a`](https://github.com/nodejs/node/commit/139da6a02a)] - **deps**: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) [#1836](https://github.com/nodejs/node/pull/1836)
* \[[`283642827a`](https://github.com/nodejs/node/commit/283642827a)] - **deps**: fix asm build error of openssl in x86\_win32 (Shigeki Ohtsu) [nodejs/node#1389](https://github.com/nodejs/node/pull/1389)
* \[[`d593b552de`](https://github.com/nodejs/node/commit/d593b552de)] - **deps**: fix openssl assembly error on ia32 win32 (Fedor Indutny) [nodejs/node#1389](https://github.com/nodejs/node/pull/1389)
* \[[`2a3367a4bd`](https://github.com/nodejs/node/commit/2a3367a4bd)] - **deps**: upgrade openssl sources to 1.0.2b (Shigeki Ohtsu) [#1950](https://github.com/nodejs/node/pull/1950)
* \[[`5c29c0c519`](https://github.com/nodejs/node/commit/5c29c0c519)] - **openssl**: fix keypress requirement in apps on win32 (Shigeki Ohtsu) [nodejs/node#1389](https://github.com/nodejs/node/pull/1389)
* \[[`2cd7f73d9f`](https://github.com/nodejs/node/commit/2cd7f73d9f)] - **openssl**: fix keypress requirement in apps on win32 (Shigeki Ohtsu) [nodejs/node#1389](https://github.com/nodejs/node/pull/1389)
* \[[`c65484a74d`](https://github.com/nodejs/node/commit/c65484a74d)] - **tls**: make server not use DHE in less than 1024bits (Shigeki Ohtsu) [#1739](https://github.com/nodejs/node/pull/1739)
* \[[`77f518403f`](https://github.com/nodejs/node/commit/77f518403f)] - **win,node-gyp**: make delay-load hook C89 compliant (Sharat M R) [TooTallNate/node-gyp#616](https://github.com/TooTallNate/node-gyp/pull/616)

<a id="2.3.2"></a>

## 2015-07-01, Version 2.3.2, @rvagg

### Notable changes

* **build**:
  * Added support for compiling with Microsoft Visual C++ 2015
  * Started building and distributing headers-only tarballs along with binaries

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

## Commits

* \[[`9180140231`](https://github.com/nodejs/node/commit/9180140231)] - **\_stream\_wrap**: prevent use after free in TLS (Fedor Indutny) [#1910](https://github.com/nodejs/node/pull/1910)
* \[[`05a73c0f25`](https://github.com/nodejs/node/commit/05a73c0f25)] - **benchmark**: make concurrent requests configurable (Rich Trott) [#2068](https://github.com/nodejs/node/pull/2068)
* \[[`f52d73352e`](https://github.com/nodejs/node/commit/f52d73352e)] - **benchmark**: fix typo in README (Rich Trott) [#2067](https://github.com/nodejs/node/pull/2067)
* \[[`1cd9eeb556`](https://github.com/nodejs/node/commit/1cd9eeb556)] - **buffer**: prevent abort on bad proto (Trevor Norris) [#2012](https://github.com/nodejs/node/pull/2012)
* \[[`8350f3a3a2`](https://github.com/nodejs/node/commit/8350f3a3a2)] - **buffer**: optimize Buffer#toString() (Ben Noordhuis) [#2027](https://github.com/nodejs/node/pull/2027)
* \[[`628a3ab093`](https://github.com/nodejs/node/commit/628a3ab093)] - **build**: add tar-headers target for headers-only tar (Rod Vagg) [#1975](https://github.com/nodejs/node/pull/1975)
* \[[`dcbb9e1da6`](https://github.com/nodejs/node/commit/dcbb9e1da6)] - **build**: update build targets for io.js (Rod Vagg) [#1938](https://github.com/nodejs/node/pull/1938)
* \[[`c87c34c242`](https://github.com/nodejs/node/commit/c87c34c242)] - **build**: fix cherry-pick ooops, fix comment wording (Rod Vagg) [#2036](https://github.com/nodejs/node/pull/2036)
* \[[`4208dc4fef`](https://github.com/nodejs/node/commit/4208dc4fef)] - **build**: add MSVS 2015 support (Rod Vagg) [#2036](https://github.com/nodejs/node/pull/2036)
* \[[`834a365113`](https://github.com/nodejs/node/commit/834a365113)] - **build**: DTrace is enabled by default on darwin (Evan Lucas) [#2019](https://github.com/nodejs/node/pull/2019)
* \[[`c0c0d73269`](https://github.com/nodejs/node/commit/c0c0d73269)] - **build,win**: set env before generating projects (Alexis Campailla) [joyent/node#20109](https://github.com/joyent/node/pull/20109)
* \[[`9e890fe8b4`](https://github.com/nodejs/node/commit/9e890fe8b4)] - **crypto**: fix VerifyCallback in case of verify error (Shigeki Ohtsu) [#2064](https://github.com/nodejs/node/pull/2064)
* \[[`1f371e3988`](https://github.com/nodejs/node/commit/1f371e3988)] - **deps**: copy all openssl header files to include dir (Shigeki Ohtsu) [#2016](https://github.com/nodejs/node/pull/2016)
* \[[`c370bd3aea`](https://github.com/nodejs/node/commit/c370bd3aea)] - **doc**: make the abbreviation 1MM clear (Ivan Yan) [#2053](https://github.com/nodejs/node/pull/2053)
* \[[`54d5437566`](https://github.com/nodejs/node/commit/54d5437566)] - **doc**: Added sample command to test iojs build (Jimmy Hsu) [#850](https://github.com/nodejs/node/pull/850)
* \[[`f1f1b7e597`](https://github.com/nodejs/node/commit/f1f1b7e597)] - **doc**: add TSC meeting minutes 2015-06-17 (Rod Vagg) [#2048](https://github.com/nodejs/node/pull/2048)
* \[[`dbd5dc932d`](https://github.com/nodejs/node/commit/dbd5dc932d)] - **doc**: clarify prerequisites in benchmark/README.md (Jeremiah Senkpiel) [#2034](https://github.com/nodejs/node/pull/2034)
* \[[`50dbc8e143`](https://github.com/nodejs/node/commit/50dbc8e143)] - **doc**: add TSC meeting minutes 2015-05-27 (Rod Vagg) [#2037](https://github.com/nodejs/node/pull/2037)
* \[[`941ad362a7`](https://github.com/nodejs/node/commit/941ad362a7)] - **doc**: archive io.js TC minutes (Rod Vagg)
* \[[`644b2eaa89`](https://github.com/nodejs/node/commit/644b2eaa89)] - **doc**: rename tc-meetings to tsc-meetings (Rod Vagg)
* \[[`1330ee3b27`](https://github.com/nodejs/node/commit/1330ee3b27)] - **doc**: add TC meeting 2015-05-13 minutes (Rod Vagg) [#1700](https://github.com/nodejs/node/pull/1700)
* \[[`392e8fd64e`](https://github.com/nodejs/node/commit/392e8fd64e)] - **doc**: add @shigeki and @mscdex to TC (Rod Vagg) [#2008](https://github.com/nodejs/node/pull/2008)
* \[[`af249fa8a1`](https://github.com/nodejs/node/commit/af249fa8a1)] - **net**: wrap connect in nextTick (Evan Lucas) [#2054](https://github.com/nodejs/node/pull/2054)
* \[[`7f63449fde`](https://github.com/nodejs/node/commit/7f63449fde)] - **net**: fix debug for dnsopts (Evan Lucas) [#2059](https://github.com/nodejs/node/pull/2059)
* \[[`eabed2f518`](https://github.com/nodejs/node/commit/eabed2f518)] - **repl**: remove obsolete TODO (Rich Trott) [#2081](https://github.com/nodejs/node/pull/2081)
* \[[`a198c68b56`](https://github.com/nodejs/node/commit/a198c68b56)] - **repl**: make 'Unexpected token' errors recoverable (Julien Gilli) [#2052](https://github.com/nodejs/node/pull/2052)
* \[[`d735b2c6ef`](https://github.com/nodejs/node/commit/d735b2c6ef)] - **repl**: fix tab completion for a non-global context (Sangmin Yoon) [#2052](https://github.com/nodejs/node/pull/2052)
* \[[`8cee8f54fc`](https://github.com/nodejs/node/commit/8cee8f54fc)] - **src**: nix stdin \_readableState.reading manipulation (Chris Dickinson) [#454](https://github.com/nodejs/node/pull/454)
* \[[`856c11f8c8`](https://github.com/nodejs/node/commit/856c11f8c8)] - **test**: purge stale disabled tests (Rich Trott) [#2045](https://github.com/nodejs/node/pull/2045)
* \[[`4d5089e181`](https://github.com/nodejs/node/commit/4d5089e181)] - **test**: do not swallow OpenSSL support error (Rich Trott) [#2042](https://github.com/nodejs/node/pull/2042)
* \[[`06721fe005`](https://github.com/nodejs/node/commit/06721fe005)] - **test**: fix test-repl-tab-complete.js (cjihrig) [#2052](https://github.com/nodejs/node/pull/2052)
* \[[`8e9089ac35`](https://github.com/nodejs/node/commit/8e9089ac35)] - **test**: check for error on Windows (Rich Trott) [#2035](https://github.com/nodejs/node/pull/2035)
* \[[`776a65ebcd`](https://github.com/nodejs/node/commit/776a65ebcd)] - **test**: remove obsolete TODO comments (Rich Trott) [#2033](https://github.com/nodejs/node/pull/2033)
* \[[`bdfeb798ad`](https://github.com/nodejs/node/commit/bdfeb798ad)] - **test**: remove obsolete TODO comments (Rich Trott) [#2032](https://github.com/nodejs/node/pull/2032)
* \[[`58e914f9bc`](https://github.com/nodejs/node/commit/58e914f9bc)] - **tools**: fix gyp to work on MacOSX without XCode (Shigeki Ohtsu) [iojs/io.js#1325](https://github.com/iojs/io.js/pull/1325)
* \[[`99cbbc0a13`](https://github.com/nodejs/node/commit/99cbbc0a13)] - **tools**: update gyp to 25ed9ac (Ben Noordhuis) [#2074](https://github.com/nodejs/node/pull/2074)
* \[[`e3f9335c40`](https://github.com/nodejs/node/commit/e3f9335c40)] - **tools**: re-enable comma-spacing linter rule (Roman Reiss) [#2072](https://github.com/nodejs/node/pull/2072)
* \[[`d91e10b3bd`](https://github.com/nodejs/node/commit/d91e10b3bd)] - **tools**: update eslint to 0.24.0 (Roman Reiss) [#2072](https://github.com/nodejs/node/pull/2072)
* \[[`6c61ca5325`](https://github.com/nodejs/node/commit/6c61ca5325)] - **url**: fix typo in comment (Rich Trott) [#2071](https://github.com/nodejs/node/pull/2071)
* \[[`1a51f0058c`](https://github.com/nodejs/node/commit/1a51f0058c)] - **v8**: cherry-pick JitCodeEvent patch from upstream (Ben Noordhuis) [#2075](https://github.com/nodejs/node/pull/2075)

<a id="2.3.1"></a>

## 2015-06-23, Version 2.3.1, @rvagg

### Notable changes

* **module**: The number of syscalls made during a `require()` have been significantly reduced again (see [#1801](https://github.com/nodejs/node/pull/1801) from v2.2.0 for previous work), which should lead to a performance improvement (Pierre Inglebert) [#1920](https://github.com/nodejs/node/pull/1920).
* **npm**:
  * Upgrade to [v2.11.2](https://github.com/npm/npm/releases/tag/v2.11.2) (Rebecca Turner) [#1956](https://github.com/nodejs/node/pull/1956).
  * Upgrade to [v2.11.3](https://github.com/npm/npm/releases/tag/v2.11.3) (Forrest L Norvell) [#2018](https://github.com/nodejs/node/pull/2018).
* **zlib**: A bug was discovered where the process would abort if the final part of a zlib decompression results in a buffer that would exceed the maximum length of `0x3fffffff` bytes (\~1GiB). This was likely to only occur during buffered decompression (rather than streaming). This is now fixed and will instead result in a thrown `RangeError` (Michaël Zasso) [#1811](https://github.com/nodejs/node/pull/1811).

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

## Commits

* \[[`e56758a5e0`](https://github.com/nodejs/node/commit/e56758a5e0)] - **async-wrap**: add provider id and object info cb (Trevor Norris) [#1896](https://github.com/nodejs/node/pull/1896)
* \[[`d5637e67c9`](https://github.com/nodejs/node/commit/d5637e67c9)] - **buffer**: fix cyclic dependency with util (Brendan Ashworth) [#1988](https://github.com/nodejs/node/pull/1988)
* \[[`c5353d7c62`](https://github.com/nodejs/node/commit/c5353d7c62)] - **build**: remove lint from test-ci on windows (Johan Bergström) [#2004](https://github.com/nodejs/node/pull/2004)
* \[[`c207e8d223`](https://github.com/nodejs/node/commit/c207e8d223)] - **build**: fix pkg-config output parsing in configure (Ben Noordhuis) [#1986](https://github.com/nodejs/node/pull/1986)
* \[[`8d8a26e8f7`](https://github.com/nodejs/node/commit/8d8a26e8f7)] - **build**: don't run lint from test-ci (Johan Bergström) [#1965](https://github.com/nodejs/node/pull/1965)
* \[[`1ec53c044d`](https://github.com/nodejs/node/commit/1ec53c044d)] - **build**: simplify execution of built binary (Johan Bergström) [#1955](https://github.com/nodejs/node/pull/1955)
* \[[`3beb880716`](https://github.com/nodejs/node/commit/3beb880716)] - **crypto**: add cert check to CNNIC Whitelist (Shigeki Ohtsu) [#1895](https://github.com/nodejs/node/pull/1895)
* \[[`48c0fb8b1a`](https://github.com/nodejs/node/commit/48c0fb8b1a)] - **deps**: make node-gyp work with io.js (cjihrig) [iojs/io.js#990](https://github.com/iojs/io.js/pull/990)
* \[[`6a359b1ce9`](https://github.com/nodejs/node/commit/6a359b1ce9)] - **deps**: upgrade to npm 2.11.3 (Forrest L Norvell) [#2018](https://github.com/nodejs/node/pull/2018)
* \[[`6aab2f3b9a`](https://github.com/nodejs/node/commit/6aab2f3b9a)] - **deps**: make node-gyp work with io.js (cjihrig) [iojs/io.js#990](https://github.com/iojs/io.js/pull/990)
* \[[`3e12561b55`](https://github.com/nodejs/node/commit/3e12561b55)] - **deps**: upgrade to npm 2.11.2 (Rebecca Turner) [#1956](https://github.com/nodejs/node/pull/1956)
* \[[`8ac50819b6`](https://github.com/nodejs/node/commit/8ac50819b6)] - **doc**: add security section to README.md (Rod Vagg) [#1948](https://github.com/nodejs/node/pull/1948)
* \[[`1f93b63b11`](https://github.com/nodejs/node/commit/1f93b63b11)] - **doc**: change the info to the same as in gitconfig (Christian Tellnes) [#2000](https://github.com/nodejs/node/pull/2000)
* \[[`0cf94e6856`](https://github.com/nodejs/node/commit/0cf94e6856)] - **doc**: mention CI in Collaborator Guide (Rich Trott) [#1995](https://github.com/nodejs/node/pull/1995)
* \[[`7a3006efe4`](https://github.com/nodejs/node/commit/7a3006efe4)] - **doc**: add TOC links to Collaborator Guide (Rich Trott) [#1994](https://github.com/nodejs/node/pull/1994)
* \[[`30638b150f`](https://github.com/nodejs/node/commit/30638b150f)] - **doc**: add TSC meeting notes 2015-06-10 (Bert Belder) [#1943](https://github.com/nodejs/node/pull/1943)
* \[[`c4ec04136b`](https://github.com/nodejs/node/commit/c4ec04136b)] - **doc**: reformat authors section (Johan Bergström) [#1966](https://github.com/nodejs/node/pull/1966)
* \[[`96165f9be2`](https://github.com/nodejs/node/commit/96165f9be2)] - **doc**: minor clarification in the modules API doc. (Сковорода Никита Андреевич) [#1983](https://github.com/nodejs/node/pull/1983)
* \[[`5c2707c1b2`](https://github.com/nodejs/node/commit/5c2707c1b2)] - **doc**: benchmark/README.md copyedit (Rich Trott) [#1970](https://github.com/nodejs/node/pull/1970)
* \[[`74fdf732d0`](https://github.com/nodejs/node/commit/74fdf732d0)] - **doc**: copyedit COLLABORATOR\_GUIDE.md (Rich Trott) [#1964](https://github.com/nodejs/node/pull/1964)
* \[[`5fe6e83640`](https://github.com/nodejs/node/commit/5fe6e83640)] - **doc**: copyedit GOVERNANCE.md (Rich Trott) [#1963](https://github.com/nodejs/node/pull/1963)
* \[[`428526544c`](https://github.com/nodejs/node/commit/428526544c)] - **doc**: add ChALkeR as collaborator (Сковорода Никита Андреевич) [#1927](https://github.com/nodejs/node/pull/1927)
* \[[`5dfe0d5d61`](https://github.com/nodejs/node/commit/5dfe0d5d61)] - **doc**: remove irrelevant SEMVER-MINOR & MAJOR (Rod Vagg)
* \[[`fb8811d95e`](https://github.com/nodejs/node/commit/fb8811d95e)] - **lib,test**: fix whitespace issues (Roman Reiss) [#1971](https://github.com/nodejs/node/pull/1971)
* \[[`a4f4909f3d`](https://github.com/nodejs/node/commit/a4f4909f3d)] - **module**: fix stat with long paths on Windows (Michaël Zasso) [#2013](https://github.com/nodejs/node/pull/2013)
* \[[`a71ee93afe`](https://github.com/nodejs/node/commit/a71ee93afe)] - **module**: reduce syscalls during require search (Pierre Inglebert) [#1920](https://github.com/nodejs/node/pull/1920)
* \[[`671e64ac73`](https://github.com/nodejs/node/commit/671e64ac73)] - **module**: allow long paths for require on Windows (Michaël Zasso)
* \[[`061342a500`](https://github.com/nodejs/node/commit/061342a500)] - **net**: Defer reading until listeners could be added (James Hartig) [#1496](https://github.com/nodejs/node/pull/1496)
* \[[`5d2b846d11`](https://github.com/nodejs/node/commit/5d2b846d11)] - **test**: assert tmp and fixture dirs different (Rich Trott) [#2015](https://github.com/nodejs/node/pull/2015)
* \[[`b0990ef45d`](https://github.com/nodejs/node/commit/b0990ef45d)] - **test**: confirm symlink (Rich Trott) [#2014](https://github.com/nodejs/node/pull/2014)
* \[[`3ba4f71fc4`](https://github.com/nodejs/node/commit/3ba4f71fc4)] - **test**: check result as early as possible (Rich Trott) [#2007](https://github.com/nodejs/node/pull/2007)
* \[[`0abcf44d6b`](https://github.com/nodejs/node/commit/0abcf44d6b)] - **test**: add Buffer slice UTF-8 test (Rich Trott) [#1989](https://github.com/nodejs/node/pull/1989)
* \[[`88c1831ff4`](https://github.com/nodejs/node/commit/88c1831ff4)] - **test**: tmpdir creation failures should fail tests (Rich Trott) [#1976](https://github.com/nodejs/node/pull/1976)
* \[[`52a822d944`](https://github.com/nodejs/node/commit/52a822d944)] - **test**: fix test-cluster-worker-disconnect (Santiago Gimeno) [#1919](https://github.com/nodejs/node/pull/1919)
* \[[`7c79490bfb`](https://github.com/nodejs/node/commit/7c79490bfb)] - **test**: only refresh tmpDir for tests that need it (Rich Trott) [#1954](https://github.com/nodejs/node/pull/1954)
* \[[`88d7904c0b`](https://github.com/nodejs/node/commit/88d7904c0b)] - **test**: remove test repetition (Rich Trott) [#1874](https://github.com/nodejs/node/pull/1874)
* \[[`91dfb5e094`](https://github.com/nodejs/node/commit/91dfb5e094)] - **tools**: make test-npm work without global npm (Jeremiah Senkpiel) [#1926](https://github.com/nodejs/node/pull/1926)
* \[[`3777f41562`](https://github.com/nodejs/node/commit/3777f41562)] - **tools**: enable whitespace related rules in eslint (Roman Reiss) [#1971](https://github.com/nodejs/node/pull/1971)
* \[[`626432d843`](https://github.com/nodejs/node/commit/626432d843)] - **util**: dont repeat isBuffer (Brendan Ashworth) [#1988](https://github.com/nodejs/node/pull/1988)
* \[[`1d79f572f1`](https://github.com/nodejs/node/commit/1d79f572f1)] - **util**: move deprecate() to internal module (Brendan Ashworth) [#1988](https://github.com/nodejs/node/pull/1988)
* \[[`4b4b1760b5`](https://github.com/nodejs/node/commit/4b4b1760b5)] - **v8**: cherry-pick uclibc build patch from upstream (Ben Noordhuis) [#1974](https://github.com/nodejs/node/pull/1974)
* \[[`5d0cee46bb`](https://github.com/nodejs/node/commit/5d0cee46bb)] - **vm**: remove unnecessary HandleScopes (Ben Noordhuis) [#2001](https://github.com/nodejs/node/pull/2001)
* \[[`0ecf9457b5`](https://github.com/nodejs/node/commit/0ecf9457b5)] - **win,node-gyp**: enable delay-load hook by default (Bert Belder) [iojs/io.js#1433](https://github.com/iojs/io.js/pull/1433)
* \[[`953b3e75e8`](https://github.com/nodejs/node/commit/953b3e75e8)] - **win,node-gyp**: enable delay-load hook by default (Bert Belder) [iojs/io.js#1433](https://github.com/iojs/io.js/pull/1433)
* \[[`3806d875d3`](https://github.com/nodejs/node/commit/3806d875d3)] - **zlib**: prevent uncaught exception in zlibBuffer (Michaël Zasso) [#1811](https://github.com/nodejs/node/pull/1811)

<a id="2.3.0"></a>

## 2015-06-13, Version 2.3.0, @rvagg

### Notable changes

* **libuv**: Upgraded to 1.6.0 and 1.6.1, see [full ChangeLog](https://github.com/libuv/libuv/blob/60e515d9e6f3d86c0eedad583805201f32ea3aed/ChangeLog#L1-L36) for details. (Saúl Ibarra Corretgé) [#1905](https://github.com/nodejs/node/pull/1905) [#1889](https://github.com/nodejs/node/pull/1889). Highlights include:
  * Fix TTY becoming blocked on OS X
  * Fix UDP send callbacks to not to be synchronous
  * Add `uv_os_homedir()` (exposed as `os.homedir()`, see below)
* **npm**: See full [release notes](https://github.com/npm/npm/releases/tag/v2.11.1) for details. (Kat Marchán) [#1899](https://github.com/nodejs/node/pull/1899). Highlight:
  * Use GIT\_SSH\_COMMAND (available as of Git 2.3)
* **openssl**:
  * Upgrade to 1.0.2b and 1.0.2c, introduces DHE man-in-the-middle protection (Logjam) and fixes malformed ECParameters causing infinite loop (CVE-2015-1788). See the [security advisory](https://www.openssl.org/news/secadv_20150611.txt) for full details. (Shigeki Ohtsu) [#1950](https://github.com/nodejs/node/pull/1950) [#1958](https://github.com/nodejs/node/pull/1958)
  * Support [FIPS](https://en.wikipedia.org/wiki/Federal_Information_Processing_Standards) mode of OpenSSL, see [README](https://github.com/nodejs/node#building-iojs-with-fips-compliant-openssl) for instructions. (Fedor Indutny) [#1890](https://github.com/nodejs/node/pull/1890)
* **os**: Add `os.homedir()` method. (Colin Ihrig) [#1791](https://github.com/nodejs/node/pull/1791)
* **smalloc**: Deprecate whole module. (Vladimir Kurchatkin) [#1822](https://github.com/nodejs/node/pull/1822)
* Add new collaborators:
  * Alex Kocharin ([@rlidwka](https://github.com/rlidwka))
  * Christopher Monsanto ([@monsanto](https://github.com/monsanto))
  * Ali Ijaz Sheikh ([@ofrobots](https://github.com/ofrobots))
  * Oleg Elifantiev ([@Olegas](https://github.com/Olegas))
  * Domenic Denicola ([@domenic](https://github.com/domenic))
  * Rich Trott ([@Trott](https://github.com/Trott))

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

## Commits

* \[[`9c0a1b8cfc`](https://github.com/nodejs/node/commit/9c0a1b8cfc)] - **cluster**: wait on servers closing before disconnect (Oleg Elifantiev) [#1400](https://github.com/nodejs/node/pull/1400)
* \[[`0f68377f69`](https://github.com/nodejs/node/commit/0f68377f69)] - **crypto**: support FIPS mode of OpenSSL (Fedor Indutny) [#1890](https://github.com/nodejs/node/pull/1890)
* \[[`38d1afc24d`](https://github.com/nodejs/node/commit/38d1afc24d)] - **(SEMVER-MINOR)** **crypto**: add getCurves() to get supported ECs (Brian White) [#1914](https://github.com/nodejs/node/pull/1914)
* \[[`a4dbf45b59`](https://github.com/nodejs/node/commit/a4dbf45b59)] - **crypto**: update root certificates (Ben Noordhuis) [#1833](https://github.com/nodejs/node/pull/1833)
* \[[`81029c639a`](https://github.com/nodejs/node/commit/81029c639a)] - **debugger**: improve ESRCH error message (Jackson Tian) [#1863](https://github.com/nodejs/node/pull/1863)
* \[[`2a7fd0ad32`](https://github.com/nodejs/node/commit/2a7fd0ad32)] - **deps**: update UPGRADING.md doc to openssl-1.0.2c (Shigeki Ohtsu) [#1958](https://github.com/nodejs/node/pull/1958)
* \[[`6b3df929e0`](https://github.com/nodejs/node/commit/6b3df929e0)] - **deps**: replace all headers in openssl (Shigeki Ohtsu) [#1958](https://github.com/nodejs/node/pull/1958)
* \[[`664a659696`](https://github.com/nodejs/node/commit/664a659696)] - **deps**: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) [#1836](https://github.com/nodejs/node/pull/1836)
* \[[`42a8de2ac6`](https://github.com/nodejs/node/commit/42a8de2ac6)] - **deps**: fix asm build error of openssl in x86\_win32 (Shigeki Ohtsu) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`c66c3d9fa3`](https://github.com/nodejs/node/commit/c66c3d9fa3)] - **deps**: fix openssl assembly error on ia32 win32 (Fedor Indutny) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`86737cf0a0`](https://github.com/nodejs/node/commit/86737cf0a0)] - **deps**: upgrade openssl sources to 1.0.2c (Shigeki Ohtsu) [#1958](https://github.com/nodejs/node/pull/1958)
* \[[`94804969b7`](https://github.com/nodejs/node/commit/94804969b7)] - **deps**: update asm files for openssl-1.0.2b (Shigeki Ohtsu) [#1950](https://github.com/nodejs/node/pull/1950)
* \[[`38444915e0`](https://github.com/nodejs/node/commit/38444915e0)] - **deps**: replace all headers in openssl (Shigeki Ohtsu) [#1950](https://github.com/nodejs/node/pull/1950)
* \[[`f62b613252`](https://github.com/nodejs/node/commit/f62b613252)] - **deps**: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) [#1836](https://github.com/nodejs/node/pull/1836)
* \[[`f624d0122c`](https://github.com/nodejs/node/commit/f624d0122c)] - **deps**: fix asm build error of openssl in x86\_win32 (Shigeki Ohtsu) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`dcd67cc8d7`](https://github.com/nodejs/node/commit/dcd67cc8d7)] - **deps**: fix openssl assembly error on ia32 win32 (Fedor Indutny) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`c21b24decf`](https://github.com/nodejs/node/commit/c21b24decf)] - **deps**: upgrade openssl sources to 1.0.2b (Shigeki Ohtsu) [#1950](https://github.com/nodejs/node/pull/1950)
* \[[`2dc819b09a`](https://github.com/nodejs/node/commit/2dc819b09a)] - **deps**: make node-gyp work with io.js (cjihrig) [iojs/io.js#990](https://github.com/iojs/io.js/pull/990)
* \[[`f41b7f12b5`](https://github.com/nodejs/node/commit/f41b7f12b5)] - **deps**: upgrade to npm 2.11.1 (Kat Marchán) [#1899](https://github.com/nodejs/node/pull/1899)
* \[[`a5bd466440`](https://github.com/nodejs/node/commit/a5bd466440)] - **deps**: update libuv to version 1.6.1 (Saúl Ibarra Corretgé) [#1905](https://github.com/nodejs/node/pull/1905)
* \[[`aa33db3238`](https://github.com/nodejs/node/commit/aa33db3238)] - **deps**: update libuv to version 1.6.0 (Saúl Ibarra Corretgé) [#1889](https://github.com/nodejs/node/pull/1889)
* \[[`0ee497f0b4`](https://github.com/nodejs/node/commit/0ee497f0b4)] - **deps**: add -no\_rand\_screen to openssl s\_client (Shigeki Ohtsu) [#1836](https://github.com/nodejs/node/pull/1836)
* \[[`b5cd2f0986`](https://github.com/nodejs/node/commit/b5cd2f0986)] - **dgram**: partially revert 18d457b (Saúl Ibarra Corretgé) [#1889](https://github.com/nodejs/node/pull/1889)
* \[[`a3cc43d0a4`](https://github.com/nodejs/node/commit/a3cc43d0a4)] - **doc**: add Trott as collaborator (Rich Trott) [#1962](https://github.com/nodejs/node/pull/1962)
* \[[`cf5020fc02`](https://github.com/nodejs/node/commit/cf5020fc02)] - **doc**: add domenic as collaborator (Domenic Denicola) [#1942](https://github.com/nodejs/node/pull/1942)
* \[[`11ed5f31ab`](https://github.com/nodejs/node/commit/11ed5f31ab)] - **doc**: add Olegas as collaborator (Oleg Elifantiev) [#1930](https://github.com/nodejs/node/pull/1930)
* \[[`f500e1833b`](https://github.com/nodejs/node/commit/f500e1833b)] - **doc**: add ofrobots as collaborator (Ali Ijaz Sheikh)
* \[[`717724611a`](https://github.com/nodejs/node/commit/717724611a)] - **doc**: add monsanto as collaborator (Christopher Monsanto) [#1932](https://github.com/nodejs/node/pull/1932)
* \[[`7192b6688c`](https://github.com/nodejs/node/commit/7192b6688c)] - **doc**: add rlidwka as collaborator (Alex Kocharin) [#1929](https://github.com/nodejs/node/pull/1929)
* \[[`9f3a03f0d4`](https://github.com/nodejs/node/commit/9f3a03f0d4)] - **doc**: add references to crypto.getCurves() (Roman Reiss) [#1918](https://github.com/nodejs/node/pull/1918)
* \[[`ff39ecb914`](https://github.com/nodejs/node/commit/ff39ecb914)] - **doc**: remove comma splice (Rich Trott) [#1900](https://github.com/nodejs/node/pull/1900)
* \[[`deb8b87dc9`](https://github.com/nodejs/node/commit/deb8b87dc9)] - **doc**: add note about available ECC curves (Ryan Petschek) [#1913](https://github.com/nodejs/node/pull/1913)
* \[[`89a5b9040e`](https://github.com/nodejs/node/commit/89a5b9040e)] - **doc**: fix http.IncomingMessage.socket documentation (Сковорода Никита Андреевич) [#1867](https://github.com/nodejs/node/pull/1867)
* \[[`d29034b34b`](https://github.com/nodejs/node/commit/d29034b34b)] - **doc**: adjust changelog to clarify `client` revert (Rod Vagg) [#1859](https://github.com/nodejs/node/pull/1859)
* \[[`a79dece8ad`](https://github.com/nodejs/node/commit/a79dece8ad)] - **docs**: add return value for sync fs functions (Tyler Anton) [#1770](https://github.com/nodejs/node/pull/1770)
* \[[`1cb72c14c4`](https://github.com/nodejs/node/commit/1cb72c14c4)] - **docs**: delete unused/duplicate css files (Robert Kowalski) [#1770](https://github.com/nodejs/node/pull/1770)
* \[[`53a4eb3198`](https://github.com/nodejs/node/commit/53a4eb3198)] - **fs**: make SyncWriteStream non-enumerable (Sakthipriyan Vairamani) [#1870](https://github.com/nodejs/node/pull/1870)
* \[[`a011c3243f`](https://github.com/nodejs/node/commit/a011c3243f)] - **fs**: minor refactoring (Sakthipriyan Vairamani) [#1870](https://github.com/nodejs/node/pull/1870)
* \[[`8841132f30`](https://github.com/nodejs/node/commit/8841132f30)] - **fs**: remove inStatWatchers and use Map for lookup (Sakthipriyan Vairamani) [#1870](https://github.com/nodejs/node/pull/1870)
* \[[`67a11b9bcc`](https://github.com/nodejs/node/commit/67a11b9bcc)] - **fs**: removing unnecessary nullCheckCallNT (Sakthipriyan Vairamani) [#1870](https://github.com/nodejs/node/pull/1870)
* \[[`09f2a67bd8`](https://github.com/nodejs/node/commit/09f2a67bd8)] - **fs**: improve error message descriptions (Sakthipriyan Vairamani) [#1870](https://github.com/nodejs/node/pull/1870)
* \[[`2dcef83b5f`](https://github.com/nodejs/node/commit/2dcef83b5f)] - **fs**: use `kMaxLength` from binding (Vladimir Kurchatkin) [#1903](https://github.com/nodejs/node/pull/1903)
* \[[`353e26e3c7`](https://github.com/nodejs/node/commit/353e26e3c7)] - **(SEMVER-MINOR)** **fs**: Add string encoding option for Stream method (Yosuke Furukawa) [#1845](https://github.com/nodejs/node/pull/1845)
* \[[`8357c5084b`](https://github.com/nodejs/node/commit/8357c5084b)] - **fs**: set encoding on fs.createWriteStream (Yosuke Furukawa) [#1844](https://github.com/nodejs/node/pull/1844)
* \[[`02c345020a`](https://github.com/nodejs/node/commit/02c345020a)] - **gitignore**: don't ignore the debug npm module (Kat Marchán) [#1908](https://github.com/nodejs/node/pull/1908)
* \[[`b5b8ff117c`](https://github.com/nodejs/node/commit/b5b8ff117c)] - **lib**: don't use global Buffer (Roman Reiss) [#1794](https://github.com/nodejs/node/pull/1794)
* \[[`a251657058`](https://github.com/nodejs/node/commit/a251657058)] - **node**: mark promises as handled as soon as possible (Vladimir Kurchatkin) [#1952](https://github.com/nodejs/node/pull/1952)
* \[[`2eb170874a`](https://github.com/nodejs/node/commit/2eb170874a)] - **openssl**: fix keypress requirement in apps on win32 (Shigeki Ohtsu) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`a130132c8f`](https://github.com/nodejs/node/commit/a130132c8f)] - **openssl**: fix keypress requirement in apps on win32 (Shigeki Ohtsu) [iojs/io.js#1389](https://github.com/iojs/io.js/pull/1389)
* \[[`6e78e5feaa`](https://github.com/nodejs/node/commit/6e78e5feaa)] - **(SEMVER-MINOR)** **os**: add homedir() (cjihrig) [#1791](https://github.com/nodejs/node/pull/1791)
* \[[`d9e250295b`](https://github.com/nodejs/node/commit/d9e250295b)] - _**Revert**_ "**readline**: allow tabs in input" (Jeremiah Senkpiel) [#1961](https://github.com/nodejs/node/pull/1961)
* \[[`4b3d493c4b`](https://github.com/nodejs/node/commit/4b3d493c4b)] - **readline**: allow tabs in input (Rich Trott) [#1761](https://github.com/nodejs/node/pull/1761)
* \[[`6d95f4ff92`](https://github.com/nodejs/node/commit/6d95f4ff92)] - **(SEMVER-MINOR)** **smalloc**: deprecate whole module (Vladimir Kurchatkin) [#1822](https://github.com/nodejs/node/pull/1822)
* \[[`8c71a9241d`](https://github.com/nodejs/node/commit/8c71a9241d)] - **src**: hide InitializeICUDirectory symbol (Ben Noordhuis) [#1815](https://github.com/nodejs/node/pull/1815)
* \[[`5b6f575c1f`](https://github.com/nodejs/node/commit/5b6f575c1f)] - _**Revert**_ "**src**: add getopt option parser" (Evan Lucas) [#1862](https://github.com/nodejs/node/pull/1862)
* \[[`c0e7bf2d8c`](https://github.com/nodejs/node/commit/c0e7bf2d8c)] - **src**: add getopt option parser (Evan Lucas) [#1804](https://github.com/nodejs/node/pull/1804)
* \[[`8ea6844d26`](https://github.com/nodejs/node/commit/8ea6844d26)] - **test**: add test for failed save in REPL (Rich Trott) [#1818](https://github.com/nodejs/node/pull/1818)
* \[[`03ce84dfa1`](https://github.com/nodejs/node/commit/03ce84dfa1)] - **test**: fix cluster-worker-wait-server-close races (Sam Roberts) [#1953](https://github.com/nodejs/node/pull/1953)
* \[[`a6b8ee19b8`](https://github.com/nodejs/node/commit/a6b8ee19b8)] - **test**: create temp dir in common.js (Rich Trott) [#1877](https://github.com/nodejs/node/pull/1877)
* \[[`ff8202c6f4`](https://github.com/nodejs/node/commit/ff8202c6f4)] - **test**: fix undeclared variable access (Roman Reiss) [#1794](https://github.com/nodejs/node/pull/1794)
* \[[`d9ddd7d345`](https://github.com/nodejs/node/commit/d9ddd7d345)] - **test**: remove TODO comment (Rich Trott) [#1820](https://github.com/nodejs/node/pull/1820)
* \[[`6537fd4b55`](https://github.com/nodejs/node/commit/6537fd4b55)] - **test**: remove TODO (Rich Trott) [#1875](https://github.com/nodejs/node/pull/1875)
* \[[`a804026c9b`](https://github.com/nodejs/node/commit/a804026c9b)] - **test**: fix broken FreeBSD test (Santiago Gimeno) [#1881](https://github.com/nodejs/node/pull/1881)
* \[[`43a82f8a71`](https://github.com/nodejs/node/commit/43a82f8a71)] - **test**: fix test-sync-io-option (Evan Lucas) [#1840](https://github.com/nodejs/node/pull/1840)
* \[[`4ed25f664d`](https://github.com/nodejs/node/commit/4ed25f664d)] - **test**: add -no\_rand\_screen for tls-server-verify (Shigeki Ohtsu) [#1836](https://github.com/nodejs/node/pull/1836)
* \[[`4cf323d23d`](https://github.com/nodejs/node/commit/4cf323d23d)] - **test**: kill child in tls-server-verify for speed up (Shigeki Ohtsu) [#1836](https://github.com/nodejs/node/pull/1836)
* \[[`e6ccdcc1fe`](https://github.com/nodejs/node/commit/e6ccdcc1fe)] - **test**: improve console output of tls-server-verify (João Reis) [#1836](https://github.com/nodejs/node/pull/1836)
* \[[`975e5956f0`](https://github.com/nodejs/node/commit/975e5956f0)] - **test**: run tls-server-verify servers in parallel (João Reis) [#1836](https://github.com/nodejs/node/pull/1836)
* \[[`b18604ba2c`](https://github.com/nodejs/node/commit/b18604ba2c)] - **test**: running tls-server-verify clients in parallel (João Reis) [#1836](https://github.com/nodejs/node/pull/1836)
* \[[`f78c722df5`](https://github.com/nodejs/node/commit/f78c722df5)] - **test**: remove hardwired references to 'iojs' (Rod Vagg) [#1882](https://github.com/nodejs/node/pull/1882)
* \[[`bd99e8de8e`](https://github.com/nodejs/node/commit/bd99e8de8e)] - **test**: more test coverage for maxConnections (Rich Trott) [#1855](https://github.com/nodejs/node/pull/1855)
* \[[`b9267189a5`](https://github.com/nodejs/node/commit/b9267189a5)] - **test**: fix test-child-process-stdout-flush-exit (Santiago Gimeno) [#1868](https://github.com/nodejs/node/pull/1868)
* \[[`d20f018dcf`](https://github.com/nodejs/node/commit/d20f018dcf)] - **test**: loosen condition to detect infinite loop (Yosuke Furukawa) [#1857](https://github.com/nodejs/node/pull/1857)
* \[[`e0e96acc6f`](https://github.com/nodejs/node/commit/e0e96acc6f)] - **test**: remove smalloc add-on test (Ben Noordhuis) [#1835](https://github.com/nodejs/node/pull/1835)
* \[[`8704c58fc4`](https://github.com/nodejs/node/commit/8704c58fc4)] - **test**: remove unneeded comment task (Rich Trott) [#1858](https://github.com/nodejs/node/pull/1858)
* \[[`8732977536`](https://github.com/nodejs/node/commit/8732977536)] - **tls**: fix references to undefined `cb` (Fedor Indutny) [#1951](https://github.com/nodejs/node/pull/1951)
* \[[`75930bb38c`](https://github.com/nodejs/node/commit/75930bb38c)] - **tls**: prevent use-after-free (Fedor Indutny) [#1702](https://github.com/nodejs/node/pull/1702)
* \[[`5795e835a1`](https://github.com/nodejs/node/commit/5795e835a1)] - **tls**: emit errors on close whilst async action (Fedor Indutny) [#1702](https://github.com/nodejs/node/pull/1702)
* \[[`59d9734e21`](https://github.com/nodejs/node/commit/59d9734e21)] - **tls\_wrap**: invoke queued callbacks in DestroySSL (Fedor Indutny) [#1702](https://github.com/nodejs/node/pull/1702)
* \[[`6e4d30286d`](https://github.com/nodejs/node/commit/6e4d30286d)] - **tools**: enable/add additional eslint rules (Roman Reiss) [#1794](https://github.com/nodejs/node/pull/1794)
* \[[`098354a9f8`](https://github.com/nodejs/node/commit/098354a9f8)] - **tools**: update certdata.txt (Ben Noordhuis) [#1833](https://github.com/nodejs/node/pull/1833)
* \[[`a2d921d6a0`](https://github.com/nodejs/node/commit/a2d921d6a0)] - **tools**: customize mk-ca-bundle.pl (Ben Noordhuis) [#1833](https://github.com/nodejs/node/pull/1833)
* \[[`5be9efca40`](https://github.com/nodejs/node/commit/5be9efca40)] - **tools**: update mk-ca-bundle.pl to HEAD of upstream (Ben Noordhuis) [#1833](https://github.com/nodejs/node/pull/1833)
* \[[`1baba0580d`](https://github.com/nodejs/node/commit/1baba0580d)] - **tools**: Fix copying contents of deps/npm (thefourtheye) [#1853](https://github.com/nodejs/node/pull/1853)
* \[[`628845b816`](https://github.com/nodejs/node/commit/628845b816)] - **(SEMVER-MINOR)** **util**: introduce `printDeprecationMessage` function (Vladimir Kurchatkin) [#1822](https://github.com/nodejs/node/pull/1822)
* \[[`91d0a8b19c`](https://github.com/nodejs/node/commit/91d0a8b19c)] - **win,node-gyp**: enable delay-load hook by default (Bert Belder) [iojs/io.js#1433](https://github.com/iojs/io.js/pull/1433)

<a id="2.2.1"></a>

## 2015-06-01, Version 2.2.1, @rvagg

### Notable changes

* **http**: Reverts the move of the `client` property of `IncomingMessage` to its prototype. Although undocumented, this property was used and assumed to be an "own property" in the wild, most notably by [request](https://github.com/request/request) which is used by npm. (Michaël Zasso) [#1852](https://github.com/nodejs/node/pull/1852).

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`c5a1009903`](https://github.com/nodejs/node/commit/c5a1009903)] - **build**: avoid passing empty strings to build flags (Johan Bergström) [#1789](https://github.com/nodejs/node/pull/1789)
* \[[`5d83401086`](https://github.com/nodejs/node/commit/5d83401086)] - **doc**: put SEMVER-MINOR on pre-load module fix 2.2.0 (Rod Vagg)
* \[[`4d6b768e5d`](https://github.com/nodejs/node/commit/4d6b768e5d)] - **http**: revert deprecation of client property (Michaël Zasso) [#1852](https://github.com/nodejs/node/pull/1852)

<a id="2.2.0"></a>

## 2015-05-31, Version 2.2.0, @rvagg

### Notable changes

* **node**: Speed-up `require()` by replacing usage of `fs.statSync()` and `fs.readFileSync()` with internal variants that are faster for this use-case and do not create as many objects for the garbage collector to clean up. The primary two benefits are: significant increase in application start-up time on typical applications and better start-up time for the debugger by eliminating almost all of the thousands of exception events. (Ben Noordhuis) [#1801](https://github.com/nodejs/node/pull/1801).
* **node**: Resolution of pre-load modules (`-r` or `--require`) now follows the standard `require()` rules rather than just resolving paths, so you can now pre-load modules in node\_modules. (Ali Ijaz Sheikh) [#1812](https://github.com/nodejs/node/pull/1812).
* **npm**: Upgraded npm to v2.11.0. New hooks for `preversion`, `version`, and `postversion` lifecycle events, some SPDX-related license changes and license file inclusions. See the [release notes](https://github.com/npm/npm/releases/tag/v2.11.0) for full details.

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`a77c330c32`](https://github.com/nodejs/node/commit/a77c330c32)] - **(SEMVER-MINOR)** **child\_process**: expose ChildProcess constructor (Evan Lucas) [#1760](https://github.com/nodejs/node/pull/1760)
* \[[`3a1bc067d4`](https://github.com/nodejs/node/commit/3a1bc067d4)] - _**Revert**_ "**core**: set PROVIDER type as Persistent class id" (Ben Noordhuis) [#1827](https://github.com/nodejs/node/pull/1827)
* \[[`f9fd554500`](https://github.com/nodejs/node/commit/f9fd554500)] - **deps**: make node-gyp work with io.js (cjihrig) [iojs/io.js#990](https://github.com/iojs/io.js/pull/990)
* \[[`c1afa53648`](https://github.com/nodejs/node/commit/c1afa53648)] - **deps**: upgrade npm to 2.11.0 (Forrest L Norvell) [iojs/io.js#1829](https://github.com/iojs/io.js/pull/1829)
* \[[`ff794498e7`](https://github.com/nodejs/node/commit/ff794498e7)] - **doc**: `fs.*File()` also accept encoding strings (Rich Trott) [#1806](https://github.com/nodejs/node/pull/1806)
* \[[`98649fd31a`](https://github.com/nodejs/node/commit/98649fd31a)] - **doc**: add documentation for AtExit hook (Steve Sharp) [#1014](https://github.com/nodejs/node/pull/1014)
* \[[`eb1856dfd1`](https://github.com/nodejs/node/commit/eb1856dfd1)] - **doc**: clarify stability of fs.watch and relatives (Rich Trott) [#1775](https://github.com/nodejs/node/pull/1775)
* \[[`a74c2c9458`](https://github.com/nodejs/node/commit/a74c2c9458)] - **doc**: state url decoding behavior (Josh Gummersall) [#1731](https://github.com/nodejs/node/pull/1731)
* \[[`ba76a9d872`](https://github.com/nodejs/node/commit/ba76a9d872)] - **doc**: remove bad semver-major entry from CHANGELOG (Rod Vagg) [#1782](https://github.com/nodejs/node/pull/1782)
* \[[`a6a3f8c78d`](https://github.com/nodejs/node/commit/a6a3f8c78d)] - **doc**: fix changelog s/2.0.3/2.1.0 (Rod Vagg)
* \[[`2c686fd3ce`](https://github.com/nodejs/node/commit/2c686fd3ce)] - **http**: flush stored header (Vladimir Kurchatkin) [#1695](https://github.com/nodejs/node/pull/1695)
* \[[`1eec5f091a`](https://github.com/nodejs/node/commit/1eec5f091a)] - **http**: simplify code and remove unused properties (Brian White) [#1572](https://github.com/nodejs/node/pull/1572)
* \[[`1bbf8d0720`](https://github.com/nodejs/node/commit/1bbf8d0720)] - **lib**: speed up require(), phase 2 (Ben Noordhuis) [#1801](https://github.com/nodejs/node/pull/1801)
* \[[`b14fd1a720`](https://github.com/nodejs/node/commit/b14fd1a720)] - **lib**: speed up require(), phase 1 (Ben Noordhuis) [#1801](https://github.com/nodejs/node/pull/1801)
* \[[`5abd4ac079`](https://github.com/nodejs/node/commit/5abd4ac079)] - **lib**: simplify nextTick() usage (Brian White) [#1612](https://github.com/nodejs/node/pull/1612)
* \[[`5759722cfa`](https://github.com/nodejs/node/commit/5759722cfa)] - **(SEMVER-MINOR)** **src**: fix module search path for preload modules (Ali Ijaz Sheikh) [#1812](https://github.com/nodejs/node/pull/1812)
* \[[`a65762cab6`](https://github.com/nodejs/node/commit/a65762cab6)] - **src**: remove old code (Brendan Ashworth) [#1819](https://github.com/nodejs/node/pull/1819)
* \[[`93a44d5228`](https://github.com/nodejs/node/commit/93a44d5228)] - **src**: fix deferred events not working with -e (Ben Noordhuis) [#1793](https://github.com/nodejs/node/pull/1793)
* \[[`8059393934`](https://github.com/nodejs/node/commit/8059393934)] - **test**: check error type from net.Server.listen() (Rich Trott) [#1821](https://github.com/nodejs/node/pull/1821)
* \[[`4e90c82cdb`](https://github.com/nodejs/node/commit/4e90c82cdb)] - **test**: add heap profiler add-on regression test (Ben Noordhuis) [#1828](https://github.com/nodejs/node/pull/1828)
* \[[`6dfca71af0`](https://github.com/nodejs/node/commit/6dfca71af0)] - **test**: don't lint autogenerated test/addons/doc-\*/ (Ben Noordhuis) [#1793](https://github.com/nodejs/node/pull/1793)
* \[[`c2b8b30836`](https://github.com/nodejs/node/commit/c2b8b30836)] - **test**: remove stray copyright notices (Ben Noordhuis) [#1793](https://github.com/nodejs/node/pull/1793)
* \[[`280fb01daf`](https://github.com/nodejs/node/commit/280fb01daf)] - **test**: fix deprecation warning in addons test (Ben Noordhuis) [#1793](https://github.com/nodejs/node/pull/1793)
* \[[`8606793999`](https://github.com/nodejs/node/commit/8606793999)] - **tools**: pass constant to logger instead of string (Johan Bergström) [#1842](https://github.com/nodejs/node/pull/1842)
* \[[`fbd2b59716`](https://github.com/nodejs/node/commit/fbd2b59716)] - **tools**: add objectLiteralShorthandProperties to .eslintrc (Evan Lucas) [#1760](https://github.com/nodejs/node/pull/1760)
* \[[`53e98cc1b4`](https://github.com/nodejs/node/commit/53e98cc1b4)] - **win,node-gyp**: enable delay-load hook by default (Bert Belder) [#1763](https://github.com/nodejs/node/pull/1763)

<a id="2.1.0"></a>

## 2015-05-24, Version 2.1.0, @rvagg

### Notable changes

* **crypto**: Diffie-Hellman key exchange (DHE) parameters (`'dhparams'`) must now be 1024 bits or longer or an error will be thrown. A warning will also be printed to the console if you supply less than 2048 bits. See <https://weakdh.org/> for further context on this security concern. (Shigeki Ohtsu) [#1739](https://github.com/nodejs/node/pull/1739).
* **node**: A new `--trace-sync-io` command line flag will print a warning and a stack trace whenever a synchronous API is used. This can be used to track down synchronous calls that may be slowing down an application. (Trevor Norris) [#1707](https://github.com/nodejs/node/pull/1707).
* **node**: To allow for chaining of methods, the `setTimeout()`, `setKeepAlive()`, `setNoDelay()`, `ref()` and `unref()` methods used in `'net'`, `'dgram'`, `'http'`, `'https'` and `'tls'` now return the current instance instead of `undefined` (Roman Reiss & Evan Lucas) [#1699](https://github.com/nodejs/node/pull/1699) [#1768](https://github.com/nodejs/node/pull/1768) [#1779](https://github.com/nodejs/node/pull/1779).
* **npm**: Upgraded to v2.10.1, release notes can be found in <https://github.com/npm/npm/releases/tag/v2.10.1> and <https://github.com/npm/npm/releases/tag/v2.10.0>.
* **util**: A significant speed-up (in the order of 35%) for the common-case of a single string argument to `util.format()`, used by `console.log()` (Сковорода Никита Андреевич) [#1749](https://github.com/nodejs/node/pull/1749).

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`9da168b71f`](https://github.com/nodejs/node/commit/9da168b71f)] - **buffer**: optimize Buffer.byteLength (Brendan Ashworth) [#1713](https://github.com/nodejs/node/pull/1713)
* \[[`2b1c01c2cc`](https://github.com/nodejs/node/commit/2b1c01c2cc)] - **build**: refactor pkg-config for shared libraries (Johan Bergström) [#1603](https://github.com/nodejs/node/pull/1603)
* \[[`3c44100558`](https://github.com/nodejs/node/commit/3c44100558)] - **core**: set PROVIDER type as Persistent class id (Trevor Norris) [#1730](https://github.com/nodejs/node/pull/1730)
* \[[`c1de6d249e`](https://github.com/nodejs/node/commit/c1de6d249e)] - **(SEMVER-MINOR)** **core**: implement runtime flag to trace sync io (Trevor Norris) [#1707](https://github.com/nodejs/node/pull/1707)
* \[[`9e7099fa4e`](https://github.com/nodejs/node/commit/9e7099fa4e)] - **deps**: make node-gyp work with io.js (cjihrig) [iojs/io.js#990](https://github.com/iojs/io.js/pull/990)
* \[[`c54d057598`](https://github.com/nodejs/node/commit/c54d057598)] - **deps**: upgrade to npm 2.10.1 (Rebecca Turner) [#1763](https://github.com/nodejs/node/pull/1763)
* \[[`367ffd167d`](https://github.com/nodejs/node/commit/367ffd167d)] - **doc**: update AUTHORS list (Rod Vagg) [#1776](https://github.com/nodejs/node/pull/1776)
* \[[`2bb2f06b3e`](https://github.com/nodejs/node/commit/2bb2f06b3e)] - **doc**: fix typo in CONTRIBUTING.md (Rich Trott) [#1755](https://github.com/nodejs/node/pull/1755)
* \[[`515afc6367`](https://github.com/nodejs/node/commit/515afc6367)] - **doc**: path is ignored in url.format (Maurice Butler) [#1753](https://github.com/nodejs/node/pull/1753)
* \[[`f0a8bc3f84`](https://github.com/nodejs/node/commit/f0a8bc3f84)] - **doc**: fix spelling in CHANGELOG (Felipe Batista)
* \[[`86dd244d9b`](https://github.com/nodejs/node/commit/86dd244d9b)] - **doc**: add notes to child\_process.fork() and .exec() (Rich Trott) [#1718](https://github.com/nodejs/node/pull/1718)
* \[[`066274794c`](https://github.com/nodejs/node/commit/066274794c)] - **doc**: update links from iojs/io.js to nodejs/io.js (Frederic Hemberger) [#1715](https://github.com/nodejs/node/pull/1715)
* \[[`cb381fe3e0`](https://github.com/nodejs/node/commit/cb381fe3e0)] - **(SEMVER-MINOR)** **net**: return this from setNoDelay and setKeepAlive (Roman Reiss) [#1779](https://github.com/nodejs/node/pull/1779)
* \[[`85d9983009`](https://github.com/nodejs/node/commit/85d9983009)] - **net**: persist net.Socket options before connect (Evan Lucas) [#1518](https://github.com/nodejs/node/pull/1518)
* \[[`39dde3222e`](https://github.com/nodejs/node/commit/39dde3222e)] - **(SEMVER-MINOR)** **net,dgram**: return this from ref and unref methods (Roman Reiss) [#1768](https://github.com/nodejs/node/pull/1768)
* \[[`5773438913`](https://github.com/nodejs/node/commit/5773438913)] - **test**: fix jslint error (Michaël Zasso) [#1743](https://github.com/nodejs/node/pull/1743)
* \[[`867631986f`](https://github.com/nodejs/node/commit/867631986f)] - **test**: fix test-sync-io-option (Santiago Gimeno) [#1734](https://github.com/nodejs/node/pull/1734)
* \[[`f29762f4dd`](https://github.com/nodejs/node/commit/f29762f4dd)] - **test**: enable linting for tests (Roman Reiss) [#1721](https://github.com/nodejs/node/pull/1721)
* \[[`2a71f02988`](https://github.com/nodejs/node/commit/2a71f02988)] - **tls**: emit errors happening before handshake finish (Malte-Thorben Bruns) [#1769](https://github.com/nodejs/node/pull/1769)
* \[[`80342f649d`](https://github.com/nodejs/node/commit/80342f649d)] - **tls**: use `.destroy(err)` instead of destroy+emit (Fedor Indutny) [#1711](https://github.com/nodejs/node/pull/1711)
* \[[`9b35be5810`](https://github.com/nodejs/node/commit/9b35be5810)] - **tls**: make server not use DHE in less than 1024bits (Shigeki Ohtsu) [#1739](https://github.com/nodejs/node/pull/1739)
* \[[`214d02040e`](https://github.com/nodejs/node/commit/214d02040e)] - **util**: speed up common case of formatting string (Сковорода Никита Андреевич) [#1749](https://github.com/nodejs/node/pull/1749)
* \[[`d144e96fbf`](https://github.com/nodejs/node/commit/d144e96fbf)] - **win,node-gyp**: enable delay-load hook by default (Bert Belder) [#1763](https://github.com/nodejs/node/pull/1763)
* \[[`0d6d3dda95`](https://github.com/nodejs/node/commit/0d6d3dda95)] - **win,node-gyp**: make delay-load hook C89 compliant (Sharat M R) [TooTallNate/node-gyp#616](https://github.com/TooTallNate/node-gyp/pull/616)

<a id="1.8.2"></a>

## 2015-05-17, Version 1.8.2, @rvagg

**Maintenance release**

## Notable changes

* **crypto**: significantly reduced memory usage for TLS (Fedor Indutny & Сковорода Никита Андреевич) [#1529](https://github.com/nodejs/node/pull/1529)
* **npm**: Upgrade npm to 2.9.0. See the [v2.8.4](https://github.com/npm/npm/releases/tag/v2.8.4) and [v2.9.0](https://github.com/npm/npm/releases/tag/v2.9.0) release notes for details. Summary:
  * Add support for default author field to make `npm init -y` work without user-input (@othiym23) [npm/npm/d8eee6cf9d](https://github.com/npm/npm/commit/d8eee6cf9d2ff7aca68dfaed2de76824a3e0d9)
  * Include local modules in `npm outdated` and `npm update` (@ArnaudRinquin) [npm/npm#7426](https://github.com/npm/npm/issues/7426)
  * The prefix used before the version number on `npm version` is now configurable via `tag-version-prefix` (@kkragenbrink) [npm/npm#8014](https://github.com/npm/npm/issues/8014)

### Known issues

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).
* readline: split escapes are processed incorrectly, see [#1403](https://github.com/nodejs/node/issues/1403)

### Commits

* \[[`5404cbc745`](https://github.com/nodejs/node/commit/5404cbc745)] - **buffer**: fix copy() segfault with zero arguments (Trevor Norris) [nodejs/node#1520](https://github.com/nodejs/node/pull/1520)
* \[[`65dd10e9c0`](https://github.com/nodejs/node/commit/65dd10e9c0)] - **build**: remove -J from test-ci (Rod Vagg) [nodejs/node#1544](https://github.com/nodejs/node/pull/1544)
* \[[`74060bb60e`](https://github.com/nodejs/node/commit/74060bb60e)] - **crypto**: track external memory for SSL structures (Fedor Indutny) [nodejs/node#1529](https://github.com/nodejs/node/pull/1529)
* \[[`f10f379240`](https://github.com/nodejs/node/commit/f10f379240)] - **deps**: make node-gyp work with io.js (cjihrig) [nodejs/node#990](https://github.com/nodejs/node/pull/990)
* \[[`ba0e744c2c`](https://github.com/nodejs/node/commit/ba0e744c2c)] - **deps**: upgrade npm to 2.9.0 (Forrest L Norvell) [nodejs/node#1583](https://github.com/nodejs/node/pull/1583)
* \[[`b3a7da1091`](https://github.com/nodejs/node/commit/b3a7da1091)] - **deps**: update http\_parser to 2.5.0 (Fedor Indutny) [nodejs/node#1517](https://github.com/nodejs/node/pull/1517)
* \[[`4030545af6`](https://github.com/nodejs/node/commit/4030545af6)] - **fs**: validate fd on fs.write (Julian Duque) [#1553](https://github.com/nodejs/node/pull/1553)
* \[[`898d423820`](https://github.com/nodejs/node/commit/898d423820)] - **string\_decoder**: don't cache Buffer.isEncoding (Brian White) [nodejs/node#1548](https://github.com/nodejs/node/pull/1548)
* \[[`32a6dbcf23`](https://github.com/nodejs/node/commit/32a6dbcf23)] - **test**: extend timeouts for ARMv6 (Rod Vagg) [nodejs/node#1554](https://github.com/nodejs/node/pull/1554)
* \[[`5896fe5cd3`](https://github.com/nodejs/node/commit/5896fe5cd3)] - **test**: adjust Makefile/test-ci, add to vcbuild.bat (Rod Vagg) [nodejs/node#1530](https://github.com/nodejs/node/pull/1530)
* \[[`b72e4bc596`](https://github.com/nodejs/node/commit/b72e4bc596)] - **tls**: destroy singleUse context immediately (Fedor Indutny) [nodejs/node#1529](https://github.com/nodejs/node/pull/1529)
* \[[`1cfc455dc5`](https://github.com/nodejs/node/commit/1cfc455dc5)] - **tls**: zero SSL\_CTX freelist for a singleUse socket (Fedor Indutny) [nodejs/node#1529](https://github.com/nodejs/node/pull/1529)
* \[[`7ada680519`](https://github.com/nodejs/node/commit/7ada680519)] - **tls**: destroy SSL once it is out of use (Fedor Indutny) [nodejs/node#1529](https://github.com/nodejs/node/pull/1529)
* \[[`71274b0263`](https://github.com/nodejs/node/commit/71274b0263)] - **tls\_wrap**: use localhost if options.host is empty (Guilherme Souza) [nodejs/node#1493](https://github.com/nodejs/node/pull/1493)
* \[[`0eb74a8b6c`](https://github.com/nodejs/node/commit/0eb74a8b6c)] - **win,node-gyp**: optionally allow node.exe/iojs.exe to be renamed (Bert Belder) [nodejs/node#1266](https://github.com/nodejs/node/pull/1266)

<a id="2.0.2"></a>

## 2015-05-15, Version 2.0.2, @Fishrock123

### Notable changes

* **win,node-gyp**: the delay-load hook for windows addons has now been correctly enabled by default, it had wrongly defaulted to off in the release version of 2.0.0 (Bert Belder) [#1433](https://github.com/nodejs/node/pull/1433)
* **os**: `tmpdir()`'s trailing slash stripping has been refined to fix an issue when the temp directory is at '/'. Also considers which slash is used by the operating system. (cjihrig) [#1673](https://github.com/nodejs/node/pull/1673)
* **tls**: default ciphers have been updated to use gcm and aes128 (Mike MacCana) [#1660](https://github.com/nodejs/node/pull/1660)
* **build**: v8 snapshots have been re-enabled by default as suggested by the v8 team, since prior security issues have been resolved. This should give some perf improvements to both startup and vm context creation. (Trevor Norris) [#1663](https://github.com/nodejs/node/pull/1663)
* **src**: fixed preload modules not working when other flags were used before `--require` (Yosuke Furukawa) [#1694](https://github.com/nodejs/node/pull/1694)
* **dgram**: fixed `send()`'s callback not being asynchronous (Yosuke Furukawa) [#1313](https://github.com/nodejs/node/pull/1313)
* **readline**: emitKeys now keeps buffering data until it has enough to parse. This fixes an issue with parsing split escapes. (Alex Kocharin) [#1601](https://github.com/nodejs/node/pull/1601)
* **cluster**: works now properly emit 'disconnect' to `cluser.worker` (Oleg Elifantiev) [#1386](https://github.com/nodejs/node/pull/1386)
* **events**: uncaught errors now provide some context (Evan Lucas) [#1654](https://github.com/nodejs/node/pull/1654)

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).

### Commits

* \[[`8a0e5295b4`](https://github.com/nodejs/node/commit/8a0e5295b4)] - **build**: use backslashes for paths on windows (Johan Bergström) [#1698](https://github.com/nodejs/node/pull/1698)
* \[[`20c9a52227`](https://github.com/nodejs/node/commit/20c9a52227)] - **build**: move --with-intl to intl optgroup (Johan Bergström) [#1680](https://github.com/nodejs/node/pull/1680)
* \[[`36cdc7c8ac`](https://github.com/nodejs/node/commit/36cdc7c8ac)] - **build**: re-enable V8 snapshots (Trevor Norris) [#1663](https://github.com/nodejs/node/pull/1663)
* \[[`5883a59b21`](https://github.com/nodejs/node/commit/5883a59b21)] - **cluster**: disconnect event not emitted correctly (Oleg Elifantiev) [#1386](https://github.com/nodejs/node/pull/1386)
* \[[`0f850f7ae7`](https://github.com/nodejs/node/commit/0f850f7ae7)] - **deps**: provide TXT chunk info in c-ares (Fedor Indutny)
* \[[`7e1c0e75ed`](https://github.com/nodejs/node/commit/7e1c0e75ed)] - **deps**: sync with upstream bagder/c-ares\@bba4dc5 (Ben Noordhuis) [#1678](https://github.com/nodejs/node/pull/1678)
* \[[`18d457bd34`](https://github.com/nodejs/node/commit/18d457bd34)] - **dgram**: call send callback asynchronously (Yosuke Furukawa) [#1313](https://github.com/nodejs/node/pull/1313)
* \[[`8b9a1537ad`](https://github.com/nodejs/node/commit/8b9a1537ad)] - **events**: provide better error message for unhandled error (Evan Lucas) [#1654](https://github.com/nodejs/node/pull/1654)
* \[[`19ffb5cf1c`](https://github.com/nodejs/node/commit/19ffb5cf1c)] - **lib**: fix eslint styles (Yosuke Furukawa) [#1539](https://github.com/nodejs/node/pull/1539)
* \[[`76937051f8`](https://github.com/nodejs/node/commit/76937051f8)] - **os**: refine tmpdir() trailing slash stripping (cjihrig) [#1673](https://github.com/nodejs/node/pull/1673)
* \[[`aed6bce906`](https://github.com/nodejs/node/commit/aed6bce906)] - **readline**: turn emitKeys into a streaming parser (Alex Kocharin) [#1601](https://github.com/nodejs/node/pull/1601)
* \[[`0a461e5360`](https://github.com/nodejs/node/commit/0a461e5360)] - **src**: fix preload when used with prior flags (Yosuke Furukawa) [#1694](https://github.com/nodejs/node/pull/1694)
* \[[`931a0d4634`](https://github.com/nodejs/node/commit/931a0d4634)] - **src**: add type check to v8.setFlagsFromString() (Roman Klauke) [#1652](https://github.com/nodejs/node/pull/1652)
* \[[`08d08668c9`](https://github.com/nodejs/node/commit/08d08668c9)] - **src,deps**: replace LoadLibrary by LoadLibraryW (Cheng Zhao) [#226](https://github.com/nodejs/node/pull/226)
* \[[`4e2f999a62`](https://github.com/nodejs/node/commit/4e2f999a62)] - **test**: fix infinite loop detection (Yosuke Furukawa) [#1681](https://github.com/nodejs/node/pull/1681)
* \[[`5755fc099f`](https://github.com/nodejs/node/commit/5755fc099f)] - **tls**: update default ciphers to use gcm and aes128 (Mike MacCana) [#1660](https://github.com/nodejs/node/pull/1660)
* \[[`966acb9916`](https://github.com/nodejs/node/commit/966acb9916)] - **tools**: remove closure\_linter to eslint on windows (Yosuke Furukawa) [#1685](https://github.com/nodejs/node/pull/1685)
* \[[`c58264e58b`](https://github.com/nodejs/node/commit/c58264e58b)] - **tools**: make eslint work on subdirectories (Roman Reiss) [#1686](https://github.com/nodejs/node/pull/1686)
* \[[`0b21ab13b7`](https://github.com/nodejs/node/commit/0b21ab13b7)] - **tools**: refactor `make test-npm` into test-npm.sh (Jeremiah Senkpiel) [#1662](https://github.com/nodejs/node/pull/1662)
* \[[`f07b3b600b`](https://github.com/nodejs/node/commit/f07b3b600b)] - **tools**: set eslint comma-spacing to 'warn' (Roman Reiss) [#1672](https://github.com/nodejs/node/pull/1672)
* \[[`f9dd34d301`](https://github.com/nodejs/node/commit/f9dd34d301)] - **tools**: replace closure-linter with eslint (Yosuke Furukawa) [#1539](https://github.com/nodejs/node/pull/1539)
* \[[`64d3210c98`](https://github.com/nodejs/node/commit/64d3210c98)] - **win,node-gyp**: enable delay-load hook by default (Bert Belder) [#1667](https://github.com/nodejs/node/issues/1667)

<a id="2.0.1"></a>

## 2015-05-07, Version 2.0.1, @rvagg

### Notable changes

* **async\_wrap**: (Trevor Norris) [#1614](https://github.com/nodejs/node/pull/1614)
  * it is now possible to filter by providers
  * bit flags have been removed and replaced with method calls on the binding object
  * _note that this is an unstable API so feature additions and breaking changes won't change io.js semver_
* **libuv**: resolves numerous io.js issues:
  * [#862](https://github.com/nodejs/node/issues/862) prevent spawning child processes with invalid stdio file descriptors
  * [#1397](https://github.com/nodejs/node/issues/1397) fix EPERM error with fs.access(W\_OK) on Windows
  * [#1621](https://github.com/nodejs/node/issues/1621) build errors associated with the bundled libuv
  * [#1512](https://github.com/nodejs/node/issues/1512) should properly fix Windows termination errors
* **addons**: the `NODE_DEPRECATED` macro was causing problems when compiling addons with older compilers, this should now be resolved (Ben Noordhuis) [#1626](https://github.com/nodejs/node/pull/1626)
* **V8**: upgrade V8 from 4.2.77.18 to 4.2.77.20 with minor fixes, including a bug preventing builds on FreeBSD

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).
* readline: split escapes are processed incorrectly, see [#1403](https://github.com/nodejs/node/issues/1403)

### Commits

* \[[`7dde95a8bd`](https://github.com/nodejs/node/commit/7dde95a8bd)] - **async-wrap**: remove before/after calls in init (Trevor Norris) [#1614](https://github.com/nodejs/node/pull/1614)
* \[[`bd42ba056a`](https://github.com/nodejs/node/commit/bd42ba056a)] - **async-wrap**: set flags using functions (Trevor Norris) [#1614](https://github.com/nodejs/node/pull/1614)
* \[[`4b2c786449`](https://github.com/nodejs/node/commit/4b2c786449)] - **async-wrap**: pass PROVIDER as first arg to init (Trevor Norris) [#1614](https://github.com/nodejs/node/pull/1614)
* \[[`84bf609fd2`](https://github.com/nodejs/node/commit/84bf609fd2)] - **async-wrap**: don't call init callback unnecessarily (Trevor Norris) [#1614](https://github.com/nodejs/node/pull/1614)
* \[[`04cc03b029`](https://github.com/nodejs/node/commit/04cc03b029)] - **deps**: update libuv to 1.5.0 (Saúl Ibarra Corretgé) [#1646](https://github.com/nodejs/node/pull/1646)
* \[[`b16d9c28e8`](https://github.com/nodejs/node/commit/b16d9c28e8)] - **deps**: upgrade v8 to 4.2.77.20 (Ben Noordhuis) [#1639](https://github.com/nodejs/node/pull/1639)
* \[[`9ec3109272`](https://github.com/nodejs/node/commit/9ec3109272)] - **doc**: add TC meeting 2015-04-29 minutes (Rod Vagg) [#1585](https://github.com/nodejs/node/pull/1585)
* \[[`2c7206254c`](https://github.com/nodejs/node/commit/2c7206254c)] - **doc**: fix typo in readme.md (AQNOUCH Mohammed) [#1643](https://github.com/nodejs/node/pull/1643)
* \[[`71dc7152ee`](https://github.com/nodejs/node/commit/71dc7152ee)] - **doc**: fix PR link in CHANGELOG (Brian White) [#1624](https://github.com/nodejs/node/pull/1624)
* \[[`b97b96d05a`](https://github.com/nodejs/node/commit/b97b96d05a)] - **install**: fix NameError (thefourtheye) [#1628](https://github.com/nodejs/node/pull/1628)
* \[[`6ccbe75384`](https://github.com/nodejs/node/commit/6ccbe75384)] - **js\_stream**: fix buffer index in DoWrite (Shigeki Ohtsu) [#1635](https://github.com/nodejs/node/pull/1635)
* \[[`c43855c49c`](https://github.com/nodejs/node/commit/c43855c49c)] - **src**: export the ParseEncoding function on Windows (Ivan Kozik) [#1596](https://github.com/nodejs/node/pull/1596)
* \[[`8315b22390`](https://github.com/nodejs/node/commit/8315b22390)] - **src**: fix pedantic cpplint whitespace warnings (Ben Noordhuis) [#1640](https://github.com/nodejs/node/pull/1640)
* \[[`b712af79a7`](https://github.com/nodejs/node/commit/b712af79a7)] - **src**: fix NODE\_DEPRECATED macro with old compilers (Ben Noordhuis) [#1626](https://github.com/nodejs/node/pull/1626)
* \[[`2ed10f1349`](https://github.com/nodejs/node/commit/2ed10f1349)] - **src**: fix minor inefficiency in Buffer::New() call (Ben Noordhuis) [#1577](https://github.com/nodejs/node/pull/1577)
* \[[`f696c9efab`](https://github.com/nodejs/node/commit/f696c9efab)] - **src**: fix deprecated use of Buffer::New() (Ben Noordhuis) [#1577](https://github.com/nodejs/node/pull/1577)
* \[[`0c8f13df8f`](https://github.com/nodejs/node/commit/0c8f13df8f)] - **tools**: remove unused GuessWordSize function (thefourtheye) [#1638](https://github.com/nodejs/node/pull/1638)

<a id="2.0.0"></a>

## 2015-05-04, Version 2.0.0, @rvagg

### Breaking changes

Full details at <https://github.com/nodejs/node/wiki/Breaking-Changes#200-from-1x>

* V8 upgrade to 4.2, minor changes to C++ API
* `os.tmpdir()` is now cross-platform consistent and no longer returns a path with a trailing slash on any platform
* While not a _breaking change_ the 'smalloc' module has been deprecated in anticipation of it becoming unsupportable with a future upgrade to V8 4.4. See [#1451](https://github.com/nodejs/node/issues/1451)  for further information.

_Note: a new version of the 'url' module was reverted prior to release as it was decided the potential for breakage across the npm ecosystem was too great and that more compatibility work needed to be done before releasing it. See [#1602](https://github.com/nodejs/node/pull/1602) for further information._

### Notable changes

* **crypto**: significantly reduced memory usage for TLS (Fedor Indutny & Сковорода Никита Андреевич) [#1529](https://github.com/nodejs/node/pull/1529)
* **net**: `socket.connect()` now accepts a `'lookup'` option for a custom DNS resolution mechanism, defaults to `dns.lookup()` (Evan Lucas) [#1505](https://github.com/nodejs/node/pull/1505)
* **npm**: Upgrade npm to 2.9.0. See the [v2.8.4](https://github.com/npm/npm/releases/tag/v2.8.4) and [v2.9.0](https://github.com/npm/npm/releases/tag/v2.9.0) release notes for details. Notable items:
  * Add support for default author field to make `npm init -y` work without user-input (@othiym23) [npm/npm/d8eee6cf9d](https://github.com/npm/npm/commit/d8eee6cf9d2ff7aca68dfaed2de76824a3e0d9af)
  * Include local modules in `npm outdated` and `npm update` (@ArnaudRinquin) [npm/npm#7426](https://github.com/npm/npm/issues/7426)
  * The prefix used before the version number on `npm version` is now configurable via `tag-version-prefix` (@kkragenbrink) [npm/npm#8014](https://github.com/npm/npm/issues/8014)
* **os**: `os.tmpdir()` is now cross-platform consistent and will no longer returns a path with a trailing slash on any platform (Christian Tellnes) [#747](https://github.com/nodejs/node/pull/747)
* **process**:
  * `process.nextTick()` performance has been improved by between 2-42% across the benchmark suite, notable because this is heavily used across core (Brian White) [#1571](https://github.com/nodejs/node/pull/1571)
  * New `process.geteuid()`, `process.seteuid(id)`, `process.getegid()` and `process.setegid(id)` methods allow you to get and set effective UID and GID of the process (Evan Lucas) [#1536](https://github.com/nodejs/node/pull/1536)
* **repl**:
  * REPL history can be persisted across sessions if the `NODE_REPL_HISTORY_FILE` environment variable is set to a user accessible file, `NODE_REPL_HISTORY_SIZE` can set the maximum history size and defaults to `1000` (Chris Dickinson) [#1513](https://github.com/nodejs/node/pull/1513)
  * The REPL can be placed in to one of three modes using the `NODE_REPL_MODE` environment variable: `sloppy`, `strict` or `magic` (default); the new `magic` mode will automatically run "strict mode only" statements in strict mode (Chris Dickinson) [#1513](https://github.com/nodejs/node/pull/1513)
* **smalloc**: the 'smalloc' module has been deprecated due to changes coming in V8 4.4 that will render it unusable
* **util**: add Promise, Map and Set inspection support (Christopher Monsanto) [#1471](https://github.com/nodejs/node/pull/1471)
* **V8**: upgrade to 4.2.77.18, see the [ChangeLog](https://chromium.googlesource.com/v8/v8/+/refs/heads/4.2.77/ChangeLog) for full details. Notable items:
  * Classes have moved out of staging; the `class` keyword is now usable in strict mode without flags
  * Object literal enhancements have moved out of staging; shorthand method and property syntax is now usable (`{ method() { }, property }`)
  * Rest parameters (`function(...args) {}`) are implemented in staging behind the `--harmony-rest-parameters` flag
  * Computed property names (`{['foo'+'bar']:'bam'}`) are implemented in staging behind the `--harmony-computed-property-names` flag
  * Unicode escapes (`'\u{xxxx}'`) are implemented in staging behind the `--harmony_unicode` flag and the `--harmony_unicode_regexps` flag for use in regular expressions
* **Windows**:
  * Random process termination on Windows fixed (Fedor Indutny)  [#1512](https://github.com/nodejs/node/issues/1512) / [#1563](https://github.com/nodejs/node/pull/1563)
  * The delay-load hook introduced to fix issues with process naming (iojs.exe / node.exe) has been made opt-out for native add-ons. Native add-ons should include `'win_delay_load_hook': 'false'` in their binding.gyp to disable this feature if they experience problems . (Bert Belder) [#1433](https://github.com/nodejs/node/pull/1433)
* **Governance**:
  * Rod Vagg (@rvagg) was added to the Technical Committee (TC)
  * Jeremiah Senkpiel (@Fishrock123) was added to the Technical Committee (TC)

### Known issues

See <https://github.com/nodejs/node/labels/confirmed-bug> for complete and current list of known issues.

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).
* readline: split escapes are processed incorrectly, see [#1403](https://github.com/nodejs/node/issues/1403)

### Commits

* \[[`5404cbc745`](https://github.com/nodejs/node/commit/5404cbc745)] - **buffer**: fix copy() segfault with zero arguments (Trevor Norris) [#1520](https://github.com/nodejs/node/pull/1520)
* \[[`3d3083b91f`](https://github.com/nodejs/node/commit/3d3083b91f)] - **buffer**: little improve for Buffer.concat method (Jackson Tian) [#1437](https://github.com/nodejs/node/pull/1437)
* \[[`e67542ae17`](https://github.com/nodejs/node/commit/e67542ae17)] - **build**: disable -Og when building with clang (Ben Noordhuis) [#1609](https://github.com/nodejs/node/pull/1609)
* \[[`78f4b038f8`](https://github.com/nodejs/node/commit/78f4b038f8)] - **build**: turn on debug-safe optimizations with -Og (Ben Noordhuis) [#1569](https://github.com/nodejs/node/pull/1569)
* \[[`a5dcff827a`](https://github.com/nodejs/node/commit/a5dcff827a)] - **build**: Use option groups in configure output (Johan Bergström) [#1533](https://github.com/nodejs/node/pull/1533)
* \[[`2a3c8c187e`](https://github.com/nodejs/node/commit/2a3c8c187e)] - **build**: remove -J from test-ci (Rod Vagg) [#1544](https://github.com/nodejs/node/pull/1544)
* \[[`e6874dd0f9`](https://github.com/nodejs/node/commit/e6874dd0f9)] - **crypto**: track external memory for SSL structures (Fedor Indutny) [#1529](https://github.com/nodejs/node/pull/1529)
* \[[`935c9d3fa7`](https://github.com/nodejs/node/commit/935c9d3fa7)] - **deps**: make node-gyp work with io.js (cjihrig) [#990](https://github.com/nodejs/node/pull/990)
* \[[`56e4255382`](https://github.com/nodejs/node/commit/56e4255382)] - **deps**: upgrade npm to 2.9.0 (Forrest L Norvell) [#1573](https://github.com/nodejs/node/pull/1573)
* \[[`509b59ea7c`](https://github.com/nodejs/node/commit/509b59ea7c)] - **deps**: enable v8 postmortem debugging again (Ben Noordhuis) [#1232](https://github.com/nodejs/node/pull/1232)
* \[[`01652c7709`](https://github.com/nodejs/node/commit/01652c7709)] - **deps**: upgrade v8 to 4.2.77.18 (Chris Dickinson) [#1506](https://github.com/nodejs/node/pull/1506)
* \[[`01e6632d70`](https://github.com/nodejs/node/commit/01e6632d70)] - **deps**: upgrade v8 to 4.2.77.15 (Ben Noordhuis) [#1399](https://github.com/nodejs/node/pull/1399)
* \[[`db4ded5903`](https://github.com/nodejs/node/commit/db4ded5903)] - **deps**: enable v8 postmortem debugging again (Ben Noordhuis) [#1232](https://github.com/nodejs/node/pull/1232)
* \[[`36cd5fb9d2`](https://github.com/nodejs/node/commit/36cd5fb9d2)] - **(SEMVER-MAJOR)** **deps**: upgrade v8 to 4.2.77.13 (Ben Noordhuis) [#1232](https://github.com/nodejs/node/pull/1232)
* \[[`b3a7da1091`](https://github.com/nodejs/node/commit/b3a7da1091)] - **deps**: update http\_parser to 2.5.0 (Fedor Indutny) [#1517](https://github.com/nodejs/node/pull/1517)
* \[[`ac1fb39ce8`](https://github.com/nodejs/node/commit/ac1fb39ce8)] - **doc**: add rvagg to the TC (Rod Vagg) [#1613](https://github.com/nodejs/node/pull/1613)
* \[[`dacc1fa35c`](https://github.com/nodejs/node/commit/dacc1fa35c)] - **doc**: update AUTHORS list (Rod Vagg) [#1586](https://github.com/nodejs/node/pull/1586)
* \[[`2a3a1909ab`](https://github.com/nodejs/node/commit/2a3a1909ab)] - **doc**: add require() lines to child.stdio example (Nick Raienko) [#1504](https://github.com/nodejs/node/pull/1504)
* \[[`02388dbf40`](https://github.com/nodejs/node/commit/02388dbf40)] - **doc**: fix some cross-references (Alexander Gromnitsky) [#1584](https://github.com/nodejs/node/pull/1584)
* \[[`57c4cc26e2`](https://github.com/nodejs/node/commit/57c4cc26e2)] - **doc**: add TC meeting 2015-04-22 minutes (Rod Vagg) [#1556](https://github.com/nodejs/node/pull/1556)
* \[[`b4ad5d7050`](https://github.com/nodejs/node/commit/b4ad5d7050)] - **doc**: improve http.request and https.request opts (Roman Reiss) [#1551](https://github.com/nodejs/node/pull/1551)
* \[[`7dc8eec0a6`](https://github.com/nodejs/node/commit/7dc8eec0a6)] - **doc**: deprecate smalloc module (Ben Noordhuis) [#1566](https://github.com/nodejs/node/pull/1566)
* \[[`1bcdf46ca7`](https://github.com/nodejs/node/commit/1bcdf46ca7)] - **doc**: add TC meeting 2015-04-15 minutes (Rod Vagg) [#1498](https://github.com/nodejs/node/pull/1498)
* \[[`391cae3595`](https://github.com/nodejs/node/commit/391cae3595)] - **doc**: Add Known issues to v1.7.0/1.7.1 CHANGELOG (Yosuke Furukawa) [#1473](https://github.com/nodejs/node/pull/1473)
* \[[`e55fdc47a7`](https://github.com/nodejs/node/commit/e55fdc47a7)] - **doc**: fix util.deprecate example (Nick Raienko) [#1535](https://github.com/nodejs/node/pull/1535)
* \[[`5178f93bc0`](https://github.com/nodejs/node/commit/5178f93bc0)] - **doc**: Add Addon API (NAN) to working group list (Julian Duque) [#1523](https://github.com/nodejs/node/pull/1523)
* \[[`f3cc50f811`](https://github.com/nodejs/node/commit/f3cc50f811)] - **doc**: add TC meeting 2015-04-08 minutes (Rod Vagg) [#1497](https://github.com/nodejs/node/pull/1497)
* \[[`bb254b533b`](https://github.com/nodejs/node/commit/bb254b533b)] - **doc**: update branch to master (Roman Reiss) [#1511](https://github.com/nodejs/node/pull/1511)
* \[[`22aafa5597`](https://github.com/nodejs/node/commit/22aafa5597)] - **doc**: add Fishrock123 to the TC (Jeremiah Senkpiel) [#1507](https://github.com/nodejs/node/pull/1507)
* \[[`b16a328ede`](https://github.com/nodejs/node/commit/b16a328ede)] - **doc**: add spaces to child.kill example (Nick Raienko) [#1503](https://github.com/nodejs/node/pull/1503)
* \[[`26327757f8`](https://github.com/nodejs/node/commit/26327757f8)] - **doc**: update AUTHORS list (Rod Vagg) [#1476](https://github.com/nodejs/node/pull/1476)
* \[[`f9c681cf62`](https://github.com/nodejs/node/commit/f9c681cf62)] - **fs**: validate fd on fs.write (Julian Duque) [#1553](https://github.com/nodejs/node/pull/1553)
* \[[`801b47acc5`](https://github.com/nodejs/node/commit/801b47acc5)] - **gitignore**: ignore xcode workspaces and projects (Roman Klauke) [#1562](https://github.com/nodejs/node/pull/1562)
* \[[`d5ce47e433`](https://github.com/nodejs/node/commit/d5ce47e433)] - **(SEMVER-MINOR)** **lib**: deprecate the smalloc module (Ben Noordhuis) [#1564](https://github.com/nodejs/node/pull/1564)
* \[[`7384ca83f9`](https://github.com/nodejs/node/commit/7384ca83f9)] - **module**: remove '' from Module.globalPaths (Chris Yip) [#1488](https://github.com/nodejs/node/pull/1488)
* \[[`b4f5898395`](https://github.com/nodejs/node/commit/b4f5898395)] - **net**: ensure Write/ShutdownWrap references handle (Fedor Indutny) [#1590](https://github.com/nodejs/node/pull/1590)
* \[[`4abe2fa1cf`](https://github.com/nodejs/node/commit/4abe2fa1cf)] - **(SEMVER-MINOR)** **net**: add lookup option to Socket.prototype.connect (Evan Lucas) [#1505](https://github.com/nodejs/node/pull/1505)
* \[[`1bef717476`](https://github.com/nodejs/node/commit/1bef717476)] - **(SEMVER-MINOR)** **net**: cleanup connect logic (Evan Lucas) [#1505](https://github.com/nodejs/node/pull/1505)
* \[[`c7782c0af8`](https://github.com/nodejs/node/commit/c7782c0af8)] - **node**: improve nextTick performance (Brian White) [#1571](https://github.com/nodejs/node/pull/1571)
* \[[`b57cc51d8d`](https://github.com/nodejs/node/commit/b57cc51d8d)] - **(SEMVER-MAJOR)** **os**: remove trailing slash from os.tmpdir() (Christian Tellnes) [#747](https://github.com/nodejs/node/pull/747)
* \[[`ca219b00d1`](https://github.com/nodejs/node/commit/ca219b00d1)] - **repl**: fix for a+ fd clearing the file on read (Chris Dickinson) [#1605](https://github.com/nodejs/node/pull/1605)
* \[[`051d482b15`](https://github.com/nodejs/node/commit/051d482b15)] - **repl**: fix \_debugger by properly proxying repl (Chris Dickinson) [#1605](https://github.com/nodejs/node/pull/1605)
* \[[`2e2fce0502`](https://github.com/nodejs/node/commit/2e2fce0502)] - **repl**: fix persistent history and env variable name (Roman Reiss) [#1593](https://github.com/nodejs/node/pull/1593)
* \[[`ea5195ccaf`](https://github.com/nodejs/node/commit/ea5195ccaf)] - **repl**: do not save history for non-terminal repl (Fedor Indutny) [#1575](https://github.com/nodejs/node/pull/1575)
* \[[`0450ce7db2`](https://github.com/nodejs/node/commit/0450ce7db2)] - **repl**: add mode detection, cli persistent history (Chris Dickinson) [#1513](https://github.com/nodejs/node/pull/1513)
* \[[`c1b9913e1f`](https://github.com/nodejs/node/commit/c1b9913e1f)] - **(SEMVER-MAJOR)** **src**: bump NODE\_MODULE\_VERSION due to V8 API (Rod Vagg) [#1532](https://github.com/nodejs/node/pull/1532)
* \[[`279f6116aa`](https://github.com/nodejs/node/commit/279f6116aa)] - **src**: fix -Wmissing-field-initializers warning (Ben Noordhuis) [#1606](https://github.com/nodejs/node/pull/1606)
* \[[`73062521a4`](https://github.com/nodejs/node/commit/73062521a4)] - **src**: deprecate smalloc public functions (Ben Noordhuis) [#1565](https://github.com/nodejs/node/pull/1565)
* \[[`ccb199af17`](https://github.com/nodejs/node/commit/ccb199af17)] - **src**: fix deprecation warnings (Ben Noordhuis) [#1565](https://github.com/nodejs/node/pull/1565)
* \[[`609fa0de03`](https://github.com/nodejs/node/commit/609fa0de03)] - **src**: fix NODE\_DEPRECATED macro (Ben Noordhuis) [#1565](https://github.com/nodejs/node/pull/1565)
* \[[`3c92ca2b5c`](https://github.com/nodejs/node/commit/3c92ca2b5c)] - **(SEMVER-MINOR)** **src**: add ability to get/set effective uid/gid (Evan Lucas) [#1536](https://github.com/nodejs/node/pull/1536)
* \[[`30b7349176`](https://github.com/nodejs/node/commit/30b7349176)] - **stream\_base**: dispatch reqs in the stream impl (Fedor Indutny) [#1563](https://github.com/nodejs/node/pull/1563)
* \[[`0fa6c4a6fc`](https://github.com/nodejs/node/commit/0fa6c4a6fc)] - **string\_decoder**: don't cache Buffer.isEncoding (Brian White) [#1548](https://github.com/nodejs/node/pull/1548)
* \[[`f9b226c1c1`](https://github.com/nodejs/node/commit/f9b226c1c1)] - **test**: extend timeouts for ARMv6 (Rod Vagg) [#1554](https://github.com/nodejs/node/pull/1554)
* \[[`bfae8236b1`](https://github.com/nodejs/node/commit/bfae8236b1)] - **test**: fix test-net-dns-custom-lookup test assertion (Evan Lucas) [#1531](https://github.com/nodejs/node/pull/1531)
* \[[`547213913b`](https://github.com/nodejs/node/commit/547213913b)] - **test**: adjust Makefile/test-ci, add to vcbuild.bat (Rod Vagg) [#1530](https://github.com/nodejs/node/pull/1530)
* \[[`550c2638c0`](https://github.com/nodejs/node/commit/550c2638c0)] - **tls**: use `SSL_set_cert_cb` for async SNI/OCSP (Fedor Indutny) [#1464](https://github.com/nodejs/node/pull/1464)
* \[[`1787416376`](https://github.com/nodejs/node/commit/1787416376)] - **tls**: destroy singleUse context immediately (Fedor Indutny) [#1529](https://github.com/nodejs/node/pull/1529)
* \[[`2684c902c4`](https://github.com/nodejs/node/commit/2684c902c4)] - **tls**: zero SSL\_CTX freelist for a singleUse socket (Fedor Indutny) [#1529](https://github.com/nodejs/node/pull/1529)
* \[[`2d241b3b82`](https://github.com/nodejs/node/commit/2d241b3b82)] - **tls**: destroy SSL once it is out of use (Fedor Indutny) [#1529](https://github.com/nodejs/node/pull/1529)
* \[[`f7620fb96d`](https://github.com/nodejs/node/commit/f7620fb96d)] - **tls\_wrap**: Unlink TLSWrap and SecureContext objects (Сковорода Никита Андреевич) [#1580](https://github.com/nodejs/node/pull/1580)
* \[[`a7d74633f2`](https://github.com/nodejs/node/commit/a7d74633f2)] - **tls\_wrap**: use localhost if options.host is empty (Guilherme Souza) [#1493](https://github.com/nodejs/node/pull/1493)
* \[[`702997c1f0`](https://github.com/nodejs/node/commit/702997c1f0)] - _**Revert**_ "**url**: significantly improve the performance of the url module" (Rod Vagg) [#1602](https://github.com/nodejs/node/pull/1602)
* \[[`0daed24883`](https://github.com/nodejs/node/commit/0daed24883)] - _**Revert**_ "**url**: delete href cache on all setter code paths" (Rod Vagg) [#1602](https://github.com/nodejs/node/pull/1602)
* \[[`0f39ef4ca1`](https://github.com/nodejs/node/commit/0f39ef4ca1)] - _**Revert**_ "**url**: fix treatment of some values as non-empty" (Rod Vagg) [#1602](https://github.com/nodejs/node/pull/1602)
* \[[`66877216bd`](https://github.com/nodejs/node/commit/66877216bd)] - **url**: fix treatment of some values as non-empty (Petka Antonov) [#1589](https://github.com/nodejs/node/pull/1589)
* \[[`dbdd81a91b`](https://github.com/nodejs/node/commit/dbdd81a91b)] - **url**: delete href cache on all setter code paths (Petka Antonov) [#1589](https://github.com/nodejs/node/pull/1589)
* \[[`3fd7fc429c`](https://github.com/nodejs/node/commit/3fd7fc429c)] - **url**: significantly improve the performance of the url module (Petka Antonov) [#1561](https://github.com/nodejs/node/pull/1561)
* \[[`bf7ac08dd0`](https://github.com/nodejs/node/commit/bf7ac08dd0)] - **util**: add Map and Set inspection support (Christopher Monsanto) [#1471](https://github.com/nodejs/node/pull/1471)
* \[[`30e83d2e84`](https://github.com/nodejs/node/commit/30e83d2e84)] - **win,node-gyp**: optionally allow node.exe/iojs.exe to be renamed (Bert Belder) [#1266](https://github.com/nodejs/node/pull/1266)
* \[[`3bda6cbfa4`](https://github.com/nodejs/node/commit/3bda6cbfa4)] - **(SEMVER-MAJOR)** **win,node-gyp**: enable delay-load hook by default (Bert Belder) [#1433](https://github.com/nodejs/node/pull/1433)

<a id="1.8.1"></a>

## 2015-04-20, Version 1.8.1, @chrisdickinson

### Notable changes

* **NOTICE**: Skipped v1.8.0 due to problems with release tooling.
  See [#1436](https://github.com/nodejs/node/issues/1436) for details.
* **build**: Support for building io.js as a static library (Marat Abdullin) [#1341](https://github.com/nodejs/node/pull/1341)
* **deps**: Upgrade openssl to 1.0.2a (Shigeki Ohtsu) [#1389](https://github.com/nodejs/node/pull/1389)
  * Users should see performance improvements when using the crypto API.
    See [here](https://github.com/nodejs/node/wiki/Crypto-Performance-Notes-for-OpenSSL-1.0.2a-on-iojs-v1.8.0)
    for details.
* **npm**: Upgrade npm to 2.8.3. See the [release notes](https://github.com/npm/npm/releases/tag/v2.8.3) for details. Includes improved git support. Summary:
  * [`387f889`](https://github.com/npm/npm/commit/387f889c0e8fb617d9cc9a42ed0a3ec49424ab5d)
    [#7961](https://github.com/npm/npm/issues/7961) Ensure that hosted git SSH
    URLs always have a valid protocol when stored in `resolved` fields in
    `npm-shrinkwrap.json`. ([@othiym23](https://github.com/othiym23))
  * [`394c2f5`](https://github.com/npm/npm/commit/394c2f5a1227232c0baf42fbba1402aafe0d6ffb)
    Switch the order in which hosted Git providers are checked to `git:`,
    `git+https:`, then `git+ssh:` (from `git:`, `git+ssh:`, then `git+https:`) in
    an effort to go from most to least likely to succeed, to make for less
    confusing error message. ([@othiym23](https://github.com/othiym23))
  * [`431c3bf`](https://github.com/npm/npm/commit/431c3bf6cdec50f9f0c735f478cb2f3f337d3313)
    [#7699](https://github.com/npm/npm/issues/7699) `npm-registry-client@6.3.2`:
    Don't send body with HTTP GET requests when logging in.
    ([@smikes](https://github.com/smikes))
  * [`15efe12`](https://github.com/npm/npm/commit/15efe124753257728a0ddc64074fa5a4b9c2eb30)
    [#7872](https://github.com/npm/npm/issues/7872) Use the new version of
    `hosted-git-info` to pass along credentials embedded in git URLs. Test it.
    Test it a lot. ([@othiym23](https://github.com/othiym23))
  * [`b027319`](https://github.com/npm/npm/commit/b0273190c71eba14395ddfdd1d9f7ba625297523)
    [#7920](https://github.com/npm/npm/issues/7920) Scoped packages with
    `peerDependencies` were installing the `peerDependencies` into the wrong
    directory. ([@ewie](https://github.com/ewie))
  * [`6b0f588`](https://github.com/npm/npm/commit/6b0f58877f37df9904490ffbaaad33862bd36dce)
    [#7867](https://github.com/npm/npm/issues/7867) Use git shorthand and git
    URLs as presented by user. Support new `hosted-git-info` shortcut syntax.
    Save shorthand in `package.json`. Try cloning via `git:`, `git+ssh:`, and
    `git+https:`, in that order, when supported by the underlying hosting
    provider. ([@othiym23](https://github.com/othiym23))
* **src**: Allow multiple arguments to be passed to process.nextTick (Trevor Norris) [#1077](https://github.com/nodejs/node/pull/1077)
* **module**: The interaction of `require('.')` with `NODE_PATH` has been restored and deprecated. This functionality
  will be removed at a later point. (Roman Reiss) [#1363](https://github.com/nodejs/node/pull/1363)

### Known issues

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)
* `url.resolve` may transfer the auth portion of the url when resolving between two full hosts, see [#1435](https://github.com/nodejs/node/issues/1435).
* readline: split escapes are processed incorrectly, see [#1403](https://github.com/nodejs/node/issues/1403)

### Commits

* \[[`53ed89d927`](https://github.com/nodejs/node/commit/53ed89d927)] - _**Revert**_ "**build**: use %PYTHON% instead of python" (Rod Vagg) [#1475](https://github.com/nodejs/node/pull/1475)
* \[[`f23b96352b`](https://github.com/nodejs/node/commit/f23b96352b)] - **src**: revert NODE\_MODULE\_VERSION to 43 (Chris Dickinson) [#1460](https://github.com/nodejs/node/pull/1460)
* \[[`431673ebd1`](https://github.com/nodejs/node/commit/431673ebd1)] - **buffer**: fast-case for empty string in byteLength (Jackson Tian) [#1441](https://github.com/nodejs/node/pull/1441)
* \[[`1b22bad35f`](https://github.com/nodejs/node/commit/1b22bad35f)] - **build**: fix logic for shared library flags (Jeremiah Senkpiel) [#1454](https://github.com/nodejs/node/pull/1454)
* \[[`91943a99d5`](https://github.com/nodejs/node/commit/91943a99d5)] - **build**: use %PYTHON% instead of python (Rod Vagg) [#1444](https://github.com/nodejs/node/pull/1444)
* \[[`c7769d417b`](https://github.com/nodejs/node/commit/c7769d417b)] - **build**: Expose xz compression level (Johan Bergström) [#1428](https://github.com/nodejs/node/pull/1428)
* \[[`a530b2baf1`](https://github.com/nodejs/node/commit/a530b2baf1)] - **build**: fix error message in configure (Shigeki Ohtsu) [#1389](https://github.com/nodejs/node/pull/1389)
* \[[`92dfb794f9`](https://github.com/nodejs/node/commit/92dfb794f9)] - **build**: enable ssl support on arm64 (Shigeki Ohtsu) [#1389](https://github.com/nodejs/node/pull/1389)
* \[[`7de0dcde83`](https://github.com/nodejs/node/commit/7de0dcde83)] - **deps**: make node-gyp work with io.js (cjihrig) [#990](https://github.com/nodejs/node/pull/990)
* \[[`4870213f9e`](https://github.com/nodejs/node/commit/4870213f9e)] - **deps**: upgrade npm to 2.8.3 (Forrest L Norvell)
* \[[`49bb7ded2c`](https://github.com/nodejs/node/commit/49bb7ded2c)] - **deps**: fix git case sensitivity issue in npm (Chris Dickinson) [#1456](https://github.com/nodejs/node/pull/1456)
* \[[`4830b4bce8`](https://github.com/nodejs/node/commit/4830b4bce8)] - **deps**: add docs to upgrade openssl (Shigeki Ohtsu) [#1389](https://github.com/nodejs/node/pull/1389)
* \[[`11bec72c87`](https://github.com/nodejs/node/commit/11bec72c87)] - **deps**: update asm files for openssl-1.0.2a (Shigeki Ohtsu) [#1389](https://github.com/nodejs/node/pull/1389)
* \[[`53924d8ebe`](https://github.com/nodejs/node/commit/53924d8ebe)] - **deps**: update asm Makefile for openssl-1.0.2a (Shigeki Ohtsu) [#1389](https://github.com/nodejs/node/pull/1389)
* \[[`418e839456`](https://github.com/nodejs/node/commit/418e839456)] - **deps**: update openssl.gyp/gypi for openssl-1.0.2a (Shigeki Ohtsu) [#1389](https://github.com/nodejs/node/pull/1389)
* \[[`02f12ab666`](https://github.com/nodejs/node/commit/02f12ab666)] - **deps**: update opensslconf.h for 1.0.2a (Shigeki Ohtsu) [#1389](https://github.com/nodejs/node/pull/1389)
* \[[`eb7a23595f`](https://github.com/nodejs/node/commit/eb7a23595f)] - **deps**: add x32 and arm64 support for opensslconf.h (Shigeki Ohtsu) [#1389](https://github.com/nodejs/node/pull/1389)
* \[[`033a663127`](https://github.com/nodejs/node/commit/033a663127)] - **deps**: replace all headers in openssl (Shigeki Ohtsu) [#1389](https://github.com/nodejs/node/pull/1389)
* \[[`ae8831f240`](https://github.com/nodejs/node/commit/ae8831f240)] - **deps**: backport openssl patch of alt cert chains 1 (Shigeki Ohtsu) [#1389](https://github.com/nodejs/node/pull/1389)
* \[[`71316c46d9`](https://github.com/nodejs/node/commit/71316c46d9)] - **deps**: fix asm build error of openssl in x86\_win32 (Shigeki Ohtsu) [#1389](https://github.com/nodejs/node/pull/1389)
* \[[`d293a4f096`](https://github.com/nodejs/node/commit/d293a4f096)] - **deps**: fix openssl assembly error on ia32 win32 (Fedor Indutny) [#1389](https://github.com/nodejs/node/pull/1389)
* \[[`e4872d7405`](https://github.com/nodejs/node/commit/e4872d7405)] - **deps**: upgrade openssl to 1.0.2a (Shigeki Ohtsu) [#1389](https://github.com/nodejs/node/pull/1389)
* \[[`a1c9ef3142`](https://github.com/nodejs/node/commit/a1c9ef3142)] - **deps, build**: add support older assembler (Shigeki Ohtsu) [#1389](https://github.com/nodejs/node/pull/1389)
* \[[`76f219c128`](https://github.com/nodejs/node/commit/76f219c128)] - **doc**: Document forced pushing with git (Johan Bergström) [#1420](https://github.com/nodejs/node/pull/1420)
* \[[`12e51d56c1`](https://github.com/nodejs/node/commit/12e51d56c1)] - **doc**: add Addon API WG (Rod Vagg) [#1226](https://github.com/nodejs/node/pull/1226)
* \[[`7956a13dad`](https://github.com/nodejs/node/commit/7956a13dad)] - **http**: logically respect maxSockets (fengmk2) [#1242](https://github.com/nodejs/node/pull/1242)
* \[[`5b844e140b`](https://github.com/nodejs/node/commit/5b844e140b)] - **module**: fix style (Roman Reiss) [#1453](https://github.com/nodejs/node/pull/1453)
* \[[`3ad82c335d`](https://github.com/nodejs/node/commit/3ad82c335d)] - **(SEMVER-MINOR)** **module**: handle NODE\_PATH in require('.') (Roman Reiss) [#1363](https://github.com/nodejs/node/pull/1363)
* \[[`cd60ff0328`](https://github.com/nodejs/node/commit/cd60ff0328)] - **net**: add fd into listen2 debug info (Jackson Tian) [#1442](https://github.com/nodejs/node/pull/1442)
* \[[`10e31ba56c`](https://github.com/nodejs/node/commit/10e31ba56c)] - **(SEMVER-MINOR)** **node**: allow multiple arguments passed to nextTick (Trevor Norris) [#1077](https://github.com/nodejs/node/pull/1077)
* \[[`116c54692a`](https://github.com/nodejs/node/commit/116c54692a)] - **openssl**: fix keypress requirement in apps on win32 (Shigeki Ohtsu) [#1389](https://github.com/nodejs/node/pull/1389)
* \[[`62f5f4cec9`](https://github.com/nodejs/node/commit/62f5f4cec9)] - **src**: remove duplicate byteLength from Buffer (Jackson Tian) [#1438](https://github.com/nodejs/node/pull/1438)
* \[[`51d0808c90`](https://github.com/nodejs/node/commit/51d0808c90)] - **stream**: remove duplicated expression (Yazhong Liu) [#1444](https://github.com/nodejs/node/pull/1444)
* \[[`deb9d23d7b`](https://github.com/nodejs/node/commit/deb9d23d7b)] - **test**: fix error message check for openssl-1.0.2a (Shigeki Ohtsu) [#1389](https://github.com/nodejs/node/pull/1389)
* \[[`ca8c9ec2c8`](https://github.com/nodejs/node/commit/ca8c9ec2c8)] - **win,node-gyp**: optionally allow node.exe/iojs.exe to be renamed (Bert Belder) [#1266](https://github.com/nodejs/node/pull/1266)

<a id="1.7.1"></a>

## 2015-04-14, Version 1.7.1, @rvagg

### Notable changes

* **build**: A syntax error in the Makefile for release builds caused 1.7.0 to be DOA and unreleased. (Rod Vagg) [#1421](https://github.com/nodejs/node/pull/1421).

### Known issues

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)
* readline: split escapes are processed incorrectly, see [#1403](https://github.com/nodejs/node/issues/1403)

### Commits

* \[[`aee86a21f2`](https://github.com/nodejs/node/commit/aee86a21f2)] - **build**: fix RELEASE check (Rod Vagg) [#1421](https://github.com/nodejs/node/pull/1421)

<a id="1.7.0"></a>

## 2015-04-14, Version 1.7.0, @rvagg

### Notable changes

* **C++ API**: Fedor Indutny contributed a feature to V8 which has been backported to the V8 bundled in io.js. `SealHandleScope` allows a C++ add-on author to _seal_ a `HandleScope` to prevent further, unintended allocations within it. Currently only enabled for debug builds of io.js. This feature helped detect the leak in [#1075](https://github.com/nodejs/node/issues/1075) and is now activated on the root `HandleScope` in io.js. (Fedor Indutny) [#1395](https://github.com/nodejs/node/pull/1395).
* **ARM**: This release includes significant work to improve the state of ARM support for builds and tests. The io.js CI cluster's ARMv6, ARMv7 and ARMv8 build servers are now all (mostly) reporting passing builds and tests.
  * ARMv8 64-bit (AARCH64) is now properly supported, including a backported fix in libuv that was mistakenly detecting the existence of `epoll_wait()`. (Ben Noordhuis) [#1365](https://github.com/nodejs/node/pull/1365).
  * ARMv6: [#1376](https://github.com/nodejs/node/issues/1376) reported a problem with `Math.exp()` on ARMv6 (incl Raspberry Pi). The culprit is erroneous codegen for ARMv6 when using the "fast math" feature of V8. `--nofast_math` has been turned on for all ARMv6 variants by default to avoid this, fast math can be turned back on with `--fast_math`. (Ben Noordhuis) [#1398](https://github.com/nodejs/node/pull/1398).
  * Tests: timeouts have been tuned specifically for slower platforms, detected as ARMv6 and ARMv7. (Roman Reiss) [#1366](https://github.com/nodejs/node/pull/1366).
* **npm**: Upgrade npm to 2.7.6. See the [release notes](https://github.com/npm/npm/releases/tag/v2.7.6) for details. Summary:
  * [`b747593`](https://github.com/npm/npm/commit/b7475936f473f029e6a027ba1b16277523747d0b)[#7630](https://github.com/npm/npm/issues/7630) Don't automatically log all git failures as errors. `maybeGithub` needs to be able to fail without logging to support its fallback logic. ([@othiym23](https://github.com/othiym23))
  * [`78005eb`](https://github.com/npm/npm/commit/78005ebb6f4103c20f077669c3929b7ea46a4c0d)[#7743](https://github.com/npm/npm/issues/7743) Always quote arguments passed to `npm run-script`. This allows build systems and the like to safely escape glob patterns passed as arguments to `run-scripts` with `npm run-script <script> -- <arguments>`. This is a tricky change to test, and may be reverted or moved to `npm@3` if it turns out it breaks things for users. ([@mantoni](https://github.com/mantoni))
  * [`da015ee`](https://github.com/npm/npm/commit/da015eee45f6daf384598151d06a9b57ffce136e)[#7074](https://github.com/npm/npm/issues/7074) `read-package-json@1.3.3`: `read-package-json` no longer caches `package.json` files, which trades a very small performance loss for the elimination of a large class of really annoying race conditions. See [#7074](https://github.com/npm/npm/issues/7074) for the grisly details. ([@othiym23](https://github.com/othiym23))

### Known issues

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)
* readline: split escapes are processed incorrectly, see [#1403](https://github.com/nodejs/node/issues/1403)

### Commits

* \[[`d2b62a4973`](https://github.com/nodejs/node/commit/d2b62a4973)] - **benchmark**: don't check wrk in non-http benchmark (Jackson Tian) [#1368](https://github.com/nodejs/node/pull/1368)
* \[[`fd90b33b94`](https://github.com/nodejs/node/commit/fd90b33b94)] - **build**: validate options passed to configure (Johan Bergström) [#1335](https://github.com/nodejs/node/pull/1335)
* \[[`04b02f5e34`](https://github.com/nodejs/node/commit/04b02f5e34)] - **build**: Remove deprecated flags (Johan Bergström) [#1407](https://github.com/nodejs/node/pull/1407)
* \[[`39d395c966`](https://github.com/nodejs/node/commit/39d395c966)] - **build**: minor changes to fix rpm build (Dan Varga) [#1408](https://github.com/nodejs/node/pull/1408)
* \[[`f9a2d31b32`](https://github.com/nodejs/node/commit/f9a2d31b32)] - **build**: Simplify fetching release version (Johan Bergström) [#1405](https://github.com/nodejs/node/pull/1405)
* \[[`cd38a4af8f`](https://github.com/nodejs/node/commit/cd38a4af8f)] - **build**: support building io.js as a static library (Marat Abdullin) [#1341](https://github.com/nodejs/node/pull/1341)
* \[[`d726a177ed`](https://github.com/nodejs/node/commit/d726a177ed)] - **build**: Remove building against a shared V8 (Johan Bergström) [#1331](https://github.com/nodejs/node/pull/1331)
* \[[`a5244d3a39`](https://github.com/nodejs/node/commit/a5244d3a39)] - **(SEMVER-MINOR)** **deps**: backport 1f8555 from v8's upstream (Fedor Indutny) [#1395](https://github.com/nodejs/node/pull/1395)
* \[[`09d4a286ea`](https://github.com/nodejs/node/commit/09d4a286ea)] - **deps**: make node-gyp work with io.js (cjihrig) [#990](https://github.com/nodejs/node/pull/990)
* \[[`cc8376ae67`](https://github.com/nodejs/node/commit/cc8376ae67)] - **deps**: upgrade npm to 2.7.6 (Forrest L Norvell) [#1390](https://github.com/nodejs/node/pull/1390)
* \[[`5b0e5755a0`](https://github.com/nodejs/node/commit/5b0e5755a0)] - **deps**: generate opensslconf.h for architectures (Shigeki Ohtsu) [#1377](https://github.com/nodejs/node/pull/1377)
* \[[`7d14aa0222`](https://github.com/nodejs/node/commit/7d14aa0222)] - **deps**: add Makefile to generate opensslconf.h (Shigeki Ohtsu) [#1377](https://github.com/nodejs/node/pull/1377)
* \[[`29a3301461`](https://github.com/nodejs/node/commit/29a3301461)] - **deps**: make opensslconf.h include each target arch (Shigeki Ohtsu) [#1377](https://github.com/nodejs/node/pull/1377)
* \[[`93a1a07ef4`](https://github.com/nodejs/node/commit/93a1a07ef4)] - **doc**: remove keepAlive options from http.request (Jeremiah Senkpiel) [#1392](https://github.com/nodejs/node/pull/1392)
* \[[`3ad6ea7c38`](https://github.com/nodejs/node/commit/3ad6ea7c38)] - **doc**: remove redundant parameter in `end` listener. (Alex Yursha) [#1387](https://github.com/nodejs/node/pull/1387)
* \[[`2bc3532461`](https://github.com/nodejs/node/commit/2bc3532461)] - **doc**: document Console class (Jackson Tian) [#1388](https://github.com/nodejs/node/pull/1388)
* \[[`69bc1382b7`](https://github.com/nodejs/node/commit/69bc1382b7)] - **doc**: properly indent http.Agent keepAlive options (Jeremiah Senkpiel) [#1384](https://github.com/nodejs/node/pull/1384)
* \[[`b464d467a2`](https://github.com/nodejs/node/commit/b464d467a2)] - **doc**: update curl usage in COLLABORATOR\_GUIDE (Roman Reiss) [#1382](https://github.com/nodejs/node/pull/1382)
* \[[`61c0e7b70f`](https://github.com/nodejs/node/commit/61c0e7b70f)] - **doc**: update CONTRIBUTING links. (Andrew Crites) [#1380](https://github.com/nodejs/node/pull/1380)
* \[[`8d467e521c`](https://github.com/nodejs/node/commit/8d467e521c)] - **doc**: add TC meeting 2015-03-18 minutes (Rod Vagg) [#1370](https://github.com/nodejs/node/pull/1370)
* \[[`8ba9c4a7c2`](https://github.com/nodejs/node/commit/8ba9c4a7c2)] - **doc**: add TC meeting 2015-04-01 minutes (Rod Vagg) [#1371](https://github.com/nodejs/node/pull/1371)
* \[[`48facf93ad`](https://github.com/nodejs/node/commit/48facf93ad)] - **doc**: update AUTHORS list (Rod Vagg) [#1372](https://github.com/nodejs/node/pull/1372)
* \[[`1219e7466c`](https://github.com/nodejs/node/commit/1219e7466c)] - **lib**: reduce process.binding() calls (Brendan Ashworth) [#1367](https://github.com/nodejs/node/pull/1367)
* \[[`264a8f3a1b`](https://github.com/nodejs/node/commit/264a8f3a1b)] - **linux**: fix epoll\_pwait() fallback on arm64 (Ben Noordhuis) [#1365](https://github.com/nodejs/node/pull/1365)
* \[[`f0bf6bb024`](https://github.com/nodejs/node/commit/f0bf6bb024)] - **readline**: fix calling constructor without new (Alex Kocharin) [#1385](https://github.com/nodejs/node/pull/1385)
* \[[`ff74931107`](https://github.com/nodejs/node/commit/ff74931107)] - **smalloc**: do not track external memory (Fedor Indutny) [#1375](https://github.com/nodejs/node/pull/1375)
* \[[`a07c69113a`](https://github.com/nodejs/node/commit/a07c69113a)] - **(SEMVER-MINOR)** **src**: use global SealHandleScope (Fedor Indutny) [#1395](https://github.com/nodejs/node/pull/1395)
* \[[`a4d88475fa`](https://github.com/nodejs/node/commit/a4d88475fa)] - **src**: disable fast math only on armv6 (Ben Noordhuis) [#1398](https://github.com/nodejs/node/pull/1398)
* \[[`e306c78f83`](https://github.com/nodejs/node/commit/e306c78f83)] - **src**: disable fast math on arm (Ben Noordhuis) [#1398](https://github.com/nodejs/node/pull/1398)
* \[[`7049d7b474`](https://github.com/nodejs/node/commit/7049d7b474)] - **test**: increase timeouts on ARM (Roman Reiss) [#1366](https://github.com/nodejs/node/pull/1366)
* \[[`3066f2c0c3`](https://github.com/nodejs/node/commit/3066f2c0c3)] - **test**: double test timeout on arm machines (Ben Noordhuis) [#1357](https://github.com/nodejs/node/pull/1357)
* \[[`66db9241cb`](https://github.com/nodejs/node/commit/66db9241cb)] - **tools**: Remove unused files (Johan Bergström) [#1406](https://github.com/nodejs/node/pull/1406)
* \[[`8bc8bd4bc2`](https://github.com/nodejs/node/commit/8bc8bd4bc2)] - **tools**: add to install deps/openssl/config/archs (Shigeki Ohtsu) [#1377](https://github.com/nodejs/node/pull/1377)
* \[[`907aaf325a`](https://github.com/nodejs/node/commit/907aaf325a)] - **win,node-gyp**: optionally allow node.exe/iojs.exe to be renamed (Bert Belder) [#1266](https://github.com/nodejs/node/pull/1266)
* \[[`372bf83818`](https://github.com/nodejs/node/commit/372bf83818)] - **zlib**: make constants keep readonly (Jackson Tian) [#1361](https://github.com/nodejs/node/pull/1361)

<a id="1.6.4"></a>

## 2015-04-06, Version 1.6.4, @Fishrock123

### Notable changes

* **npm**: upgrade npm to 2.7.5. See [npm CHANGELOG.md](https://github.com/npm/npm/blob/master/CHANGELOG.md#v275-2015-03-26) for details. Includes two important security fixes. Summary:
  * [`300834e`](https://github.com/npm/npm/commit/300834e91a4e2a95fb7fb59c309e7c3fc91d2312)
    `tar@2.0.0`: Normalize symbolic links that point to targets outside the
    extraction root. This prevents packages containing symbolic links from
    overwriting targets outside the expected paths for a package. Thanks to [Tim
    Cuthbertson](http://gfxmonk.net/) and the team at [Lift
    Security](https://liftsecurity.io/) for working with the npm team to identify
    this issue. ([@othiym23](https://github.com/othiym23))
  * [`0dc6875`](https://github.com/npm/npm/commit/0dc68757cffd5397c280bc71365d106523a5a052)
    `semver@4.3.2`: Package versions can be no more than 256 characters long.
    This prevents a situation in which parsing the version number can use
    exponentially more time and memory to parse, leading to a potential denial of
    service. Thanks to Adam Baldwin at Lift Security for bringing this to our
    attention.  ([@isaacs](https://github.com/isaacs))
  * [`eab6184`](https://github.com/npm/npm/commit/eab618425c51e3aa4416da28dcd8ca4ba63aec41)
    [#7766](https://github.com/npm/npm/issues/7766) One last tweak to ensure that
    GitHub shortcuts work with private repositories.
    ([@iarna](https://github.com/iarna))
  * [`a840a13`](https://github.com/npm/npm/commit/a840a13bbf0330157536381ea8e58d0bd93b4c05)
    [#7746](https://github.com/npm/npm/issues/7746) Only fix up git URL paths when
    there are paths to fix up. ([@othiym23](https://github.com/othiym23))
* **openssl**: preliminary work has been done for an upcoming upgrade to OpenSSL 1.0.2a [#1325](https://github.com/nodejs/node/pull/1325) (Shigeki Ohtsu). See [#589](https://github.com/nodejs/node/issues/589) for additional details.
* **timers**: a minor memory leak when timers are unreferenced was fixed, alongside some related timers issues [#1330](https://github.com/nodejs/node/pull/1330) (Fedor Indutny). This appears to have fixed the remaining leak reported in [#1075](https://github.com/nodejs/node/issues/1075).
* **android**: it is now possible to compile io.js for Android and related devices [#1307](https://github.com/nodejs/node/pull/1307) (Giovanny Andres Gongora Granada).

### Known issues

* Some problems with unreferenced timers running during `beforeExit` are still to be resolved. See [#1264](https://github.com/nodejs/node/issues/1264).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* Not possible to build io.js as a static library [#686](https://github.com/nodejs/node/issues/686)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)

### Commits

* \[[`3a69b7689b`](https://github.com/io.js/io.js/commit/3a69b7689b)] - **benchmark**: add rsa/aes-gcm performance test (Shigeki Ohtsu) [iojs/io.js#1325](https://github.com/nodejs/node/pull/1325)
* \[[`1c709f3aa9`](https://github.com/io.js/io.js/commit/1c709f3aa9)] - **benchmark**: add/remove hash algorithm (Shigeki Ohtsu) [iojs/io.js#1325](https://github.com/nodejs/node/pull/1325)
* \[[`a081c7c522`](https://github.com/io.js/io.js/commit/a081c7c522)] - **benchmark**: fix chunky client benchmark execution (Brian White) [iojs/io.js#1257](https://github.com/nodejs/node/pull/1257)
* \[[`65d4d25f52`](https://github.com/io.js/io.js/commit/65d4d25f52)] - **build**: default to armv7+vfpv3 for android (Giovanny Andres Gongora Granada) [iojs/io.js#1307](https://github.com/nodejs/node/pull/1307)
* \[[`6a134f7d70`](https://github.com/io.js/io.js/commit/6a134f7d70)] - **build**: avoid passing private flags from pmake (Johan Bergström) [iojs/io.js#1334](https://github.com/nodejs/node/pull/1334)
* \[[`5094a0fde3`](https://github.com/io.js/io.js/commit/5094a0fde3)] - **build**: Pass BSDmakefile args to gmake (Johan Bergström) [iojs/io.js#1298](https://github.com/nodejs/node/pull/1298)
* \[[`f782824d48`](https://github.com/io.js/io.js/commit/f782824d48)] - **deps**: refactor openssl.gyp (Shigeki Ohtsu) [iojs/io.js#1325](https://github.com/nodejs/node/pull/1325)
* \[[`21f4fb6215`](https://github.com/io.js/io.js/commit/21f4fb6215)] - **deps**: update gyp to e1c8fcf7 (Shigeki Ohtsu) [iojs/io.js#1325](https://github.com/nodejs/node/pull/1325)
* \[[`dac903f9b6`](https://github.com/io.js/io.js/commit/dac903f9b6)] - **deps**: make node-gyp work with io.js (cjihrig) [iojs/io.js#990](https://github.com/nodejs/node/pull/990)
* \[[`5eb983e0b3`](https://github.com/io.js/io.js/commit/5eb983e0b3)] - **deps**: upgrade npm to 2.7.5 (Forrest L Norvell) [iojs/io.js#1337](https://github.com/nodejs/node/pull/1337)
* \[[`008078862e`](https://github.com/io.js/io.js/commit/008078862e)] - **deps**: check in gtest, add util unit test (Ben Noordhuis) [iojs/io.js#1199](https://github.com/nodejs/node/pull/1199)
* \[[`48d69cf1bb`](https://github.com/io.js/io.js/commit/48d69cf1bb)] - _**Revert**_ "**doc**: fix typo in CHANGELOG.md" (Giovanny Andres Gongora Granada) [iojs/io.js#1349](https://github.com/nodejs/node/pull/1349)
* \[[`679596c848`](https://github.com/io.js/io.js/commit/679596c848)] - **doc**: add Docker WG (Peter Petrov) [iojs/io.js#1134](https://github.com/nodejs/node/pull/1134)
* \[[`d8578bad25`](https://github.com/io.js/io.js/commit/d8578bad25)] - **doc**: fix minor typos in COLLABORATOR\_GUIDE.md (Kelsey) [iojs/io.js#1320](https://github.com/nodejs/node/pull/1320)
* \[[`bde2b3e397`](https://github.com/io.js/io.js/commit/bde2b3e397)] - **doc**: fix typo in CHANGELOG.md (Giovanny Andres Gongora Granada) [iojs/io.js#1342](https://github.com/nodejs/node/pull/1342)
* \[[`8c6c376a94`](https://github.com/io.js/io.js/commit/8c6c376a94)] - **doc**: add GPG fingerprint for Fishrock123 (Jeremiah Senkpiel) [iojs/io.js#1324](https://github.com/nodejs/node/pull/1324)
* \[[`ccbea18960`](https://github.com/io.js/io.js/commit/ccbea18960)] - **doc**: better formatting for collaborator GPG keys (Jeremiah Senkpiel) [iojs/io.js#1324](https://github.com/nodejs/node/pull/1324)
* \[[`87053e8aee`](https://github.com/io.js/io.js/commit/87053e8aee)] - **doc**: add back quote to boolean variable 'true' (Kohei TAKATA) [iojs/io.js#1338](https://github.com/nodejs/node/pull/1338)
* \[[`634e9629a0`](https://github.com/io.js/io.js/commit/634e9629a0)] - **doc**: add TC meeting minutes 2015-03-04 (Rod Vagg) [iojs/io.js#1123](https://github.com/nodejs/node/pull/1123)
* \[[`245ba1d658`](https://github.com/io.js/io.js/commit/245ba1d658)] - **doc**: fix util.isObject documentation (Jeremiah Senkpiel) [iojs/io.js#1295](https://github.com/nodejs/node/pull/1295)
* \[[`ad937752ee`](https://github.com/io.js/io.js/commit/ad937752ee)] - **doc,src**: remove references to --max-stack-size (Aria Stewart) [iojs/io.js#1327](https://github.com/nodejs/node/pull/1327)
* \[[`15f058f609`](https://github.com/io.js/io.js/commit/15f058f609)] - **gyp**: fix build with python 2.6 (Fedor Indutny) [iojs/io.js#1325](https://github.com/nodejs/node/pull/1325)
* \[[`4dc6ae2181`](https://github.com/io.js/io.js/commit/4dc6ae2181)] - **lib**: remove unused variables (Brian White) [iojs/io.js#1290](https://github.com/nodejs/node/pull/1290)
* \[[`b6e22c4bd5`](https://github.com/io.js/io.js/commit/b6e22c4bd5)] - **src**: setup cluster workers before preloading (Ali Ijaz Sheikh) [iojs/io.js#1314](https://github.com/nodejs/node/pull/1314)
* \[[`4a801c211c`](https://github.com/io.js/io.js/commit/4a801c211c)] - **src**: drop homegrown thread pool, use libplatform (Ben Noordhuis) [iojs/io.js#1329](https://github.com/nodejs/node/pull/1329)
* \[[`f1e5a13516`](https://github.com/io.js/io.js/commit/f1e5a13516)] - **src**: wrap MIN definition in infdef (Johan Bergström) [iojs/io.js#1322](https://github.com/nodejs/node/pull/1322)
* \[[`6f72d87c27`](https://github.com/io.js/io.js/commit/6f72d87c27)] - **test**: add test for a unref'ed timer leak (Fedor Indutny) [iojs/io.js#1330](https://github.com/nodejs/node/pull/1330)
* \[[`416499c872`](https://github.com/io.js/io.js/commit/416499c872)] - **timers**: remove redundant code (Fedor Indutny) [iojs/io.js#1330](https://github.com/nodejs/node/pull/1330)
* \[[`d22b2a934a`](https://github.com/io.js/io.js/commit/d22b2a934a)] - **timers**: do not restart the interval after close (Fedor Indutny) [iojs/io.js#1330](https://github.com/nodejs/node/pull/1330)
* \[[`cca5efb086`](https://github.com/io.js/io.js/commit/cca5efb086)] - **timers**: don't close interval timers when unrefd (Julien Gilli)
* \[[`0e061975d7`](https://github.com/io.js/io.js/commit/0e061975d7)] - **timers**: fix unref() memory leak (Trevor Norris) [iojs/io.js#1330](https://github.com/nodejs/node/pull/1330)
* \[[`ec7fbf2bb2`](https://github.com/io.js/io.js/commit/ec7fbf2bb2)] - **tools**: fix install source path for openssl headers (Oguz Bastemur) [iojs/io.js#1354](https://github.com/nodejs/node/pull/1354)
* \[[`644ece1f67`](https://github.com/io.js/io.js/commit/644ece1f67)] - **tools**: remove gyp test directory (Shigeki Ohtsu) [iojs/io.js#1350](https://github.com/nodejs/node/pull/1350)
* \[[`eb459c8151`](https://github.com/io.js/io.js/commit/eb459c8151)] - **tools**: fix gyp to work on MacOSX without XCode (Shigeki Ohtsu) [iojs/io.js#1325](https://github.com/nodejs/node/pull/1325)
* \[[`1e94057c05`](https://github.com/io.js/io.js/commit/1e94057c05)] - **url**: fix resolving from non-file to file URLs. (Jeffrey Jagoda) [iojs/io.js#1277](https://github.com/nodejs/node/pull/1277)
* \[[`382bd9d2e0`](https://github.com/io.js/io.js/commit/382bd9d2e0)] - **v8**: back-port openbsd/amd64 build fix (Ben Noordhuis) [iojs/io.js#1318](https://github.com/nodejs/node/pull/1318)
* \[[`efadffe861`](https://github.com/io.js/io.js/commit/efadffe861)] - **win,node-gyp**: optionally allow node.exe/iojs.exe to be renamed (Bert Belder) [iojs/io.js#1266](https://github.com/nodejs/node/pull/1266)

<a id="1.6.3"></a>

## 2015-03-31, Version 1.6.3, @rvagg

### Notable changes

* **fs**: corruption can be caused by `fs.writeFileSync()` and append-mode `fs.writeFile()` and `fs.writeFileSync()` under certain circumstances, reported in [#1058](https://github.com/nodejs/node/issues/1058), fixed in [#1063](https://github.com/nodejs/node/pull/1063) (Olov Lassus).
* **iojs**: an "internal modules" API has been introduced to allow core code to share JavaScript modules internally only without having to expose them as a public API, this feature is for core-only [#848](https://github.com/nodejs/node/pull/848) (Vladimir Kurchatkin).
* **timers**: two minor problems with timers have been fixed:
  * `Timer#close()` is now properly idempotent [#1288](https://github.com/nodejs/node/issues/1288) (Petka Antonov).
  * `setTimeout()` will only run the callback once now after an `unref()` during the callback [#1231](https://github.com/nodejs/node/pull/1231) (Roman Reiss).
  * NOTE: there are still other unresolved concerns with the timers code, such as [#1152](https://github.com/nodejs/node/pull/1152).
* **Windows**: a "delay-load hook" has been added for compiled add-ons on Windows that should alleviate some of the problems that Windows users may be experiencing with add-ons in io.js [#1251](https://github.com/nodejs/node/pull/1251) (Bert Belder).
* **V8**: minor bug-fix upgrade for V8 to 4.1.0.27.
* **npm**: upgrade npm to 2.7.4. See [npm CHANGELOG.md](https://github.com/npm/npm/blob/master/CHANGELOG.md#v274-2015-03-20) for details. Summary:
  * [`1549106`](https://github.com/npm/npm/commit/1549106f518000633915686f5f1ccc6afcf77f8f) [#7641](https://github.com/npm/npm/issues/7641) Due to 448efd0, running `npm shrinkwrap --dev` caused production dependencies to no longer be included in `npm-shrinkwrap.json`. Whoopsie! ([@othiym23](https://github.com/othiym23))
  * [`fb0ac26`](https://github.com/npm/npm/commit/fb0ac26eecdd76f6eaa4a96a865b7c6f52ce5aa5) [#7579](https://github.com/npm/npm/issues/7579) Only block removing files and links when we're sure npm isn't responsible for them. This change is hard to summarize, because if things are working correctly you should never see it, but if you want more context, just [go read the commit message](https://github.com/npm/npm/commit/fb0ac26eecdd76f6eaa4a96a865b7c6f52ce5aa5), which lays it all out. ([@othiym23](https://github.com/othiym23))
  * [`051c473`](https://github.com/npm/npm/commit/051c4738486a826300f205b71590781ce7744f01) [#7552](https://github.com/npm/npm/issues/7552) `bundledDependencies` are now properly included in the installation context. This is another fantastically hard-to-summarize bug, and once again, I encourage you to [read the commit message](https://github.com/npm/npm/commit/051c4738486a826300f205b71590781ce7744f01) if you're curious about the details. The snappy takeaway is that this unbreaks many use cases for `ember-cli`. ([@othiym23](https://github.com/othiym23))
  * [`fe1bc38`](https://github.com/npm/npm/commit/fe1bc387a14475e373557de669e03d9d006d3173)[#7672](https://github.com/npm/npm/issues/7672) `npm-registry-client@3.1.2`: Fix client-side certificate handling by correcting property name. ([@atamon](https://github.com/atamon))
  * [`89ce829`](https://github.com/npm/npm/commit/89ce829a00b526d0518f5cd855c323bffe182af0)[#7630](https://github.com/npm/npm/issues/7630) `hosted-git-info@1.5.3`: Part 3 of ensuring that GitHub shorthand is handled consistently. ([@othiym23](https://github.com/othiym23))
  * [`63313eb`](https://github.com/npm/npm/commit/63313eb0c37891c355546fd1093010c8a0c3cd81)[#7630](https://github.com/npm/npm/issues/7630) `realize-package-specifier@2.2.0`: Part 2 of ensuring that GitHub shorthand is handled consistently. ([@othiym23](https://github.com/othiym23))
  * [`3ed41bf`](https://github.com/npm/npm/commit/3ed41bf64a1bb752bb3155c74dd6ffbbd28c89c9)[#7630](https://github.com/npm/npm/issues/7630) `npm-package-arg@3.1.1`: Part 1 of ensuring that GitHub shorthand is handled consistently. ([@othiym23](https://github.com/othiym23))

### Known issues

* Some problems exist with timers and `unref()` still to be resolved. See [#1152](https://github.com/nodejs/node/pull/1152).
* Possible small memory leak(s) may still exist but have yet to be properly identified, details at [#1075](https://github.com/nodejs/node/issues/1075).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* Not possible to build io.js as a static library [#686](https://github.com/nodejs/node/issues/686)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)

### Commits

* \[[`7dd5e824be`](https://github.com/nodejs/node/commit/7dd5e824be)] - **assert**: simplify logic of testing buffer equality (Alex Yursha) [#1171](https://github.com/nodejs/node/pull/1171)
* \[[`a2ea16838f`](https://github.com/nodejs/node/commit/a2ea16838f)] - **debugger**: don't spawn child process in remote mode (Jackson Tian) [#1282](https://github.com/nodejs/node/pull/1282)
* \[[`2752da4b64`](https://github.com/nodejs/node/commit/2752da4b64)] - **deps**: make node-gyp work with io.js (cjihrig) [#990](https://github.com/nodejs/node/pull/990)
* \[[`f166cdecf1`](https://github.com/nodejs/node/commit/f166cdecf1)] - **deps**: upgrade npm to 2.7.4 (Forrest L Norvell)
* \[[`318d9d8fd7`](https://github.com/nodejs/node/commit/318d9d8fd7)] - **deps**: upgrade v8 to 4.1.0.27 (Ben Noordhuis) [#1289](https://github.com/nodejs/node/pull/1289)
* \[[`269e46be37`](https://github.com/nodejs/node/commit/269e46be37)] - **deps**: make node-gyp work with io.js (cjihrig) [#990](https://github.com/nodejs/node/pull/990)
* \[[`b542fb94a4`](https://github.com/nodejs/node/commit/b542fb94a4)] - **deps**: upgrade npm to 2.7.3 (Forrest L Norvell) [#1219](https://github.com/nodejs/node/pull/1219)
* \[[`73de13511d`](https://github.com/nodejs/node/commit/73de13511d)] - **doc**: add WG links in WORKING\_GROUPS.md & fix nits (Farrin Reid) [#1113](https://github.com/nodejs/node/pull/1113)
* \[[`19641b17be`](https://github.com/nodejs/node/commit/19641b17be)] - **doc**: decouple sidebar scrolling (Roman Reiss) [#1274](https://github.com/nodejs/node/pull/1274)
* \[[`dbccf8d3ed`](https://github.com/nodejs/node/commit/dbccf8d3ed)] - **doc**: fix spelling error in feature flags (Phillip Lamplugh) [#1286](https://github.com/nodejs/node/pull/1286)
* \[[`5e609e9324`](https://github.com/nodejs/node/commit/5e609e9324)] - _**Revert**_ "**doc**: clarify real name requirement" (Jeremiah Senkpiel) [#1276](https://github.com/nodejs/node/pull/1276)
* \[[`45814216ee`](https://github.com/nodejs/node/commit/45814216ee)] - **doc**: fix format docs discrepancy (Brendan Ashworth) [#1255](https://github.com/nodejs/node/pull/1255)
* \[[`4e9bf93e9c`](https://github.com/nodejs/node/commit/4e9bf93e9c)] - **doc**: clarify real name requirement (Roman Reiss) [#1250](https://github.com/nodejs/node/pull/1250)
* \[[`e84dd5f651`](https://github.com/nodejs/node/commit/e84dd5f651)] - **doc**: document repl on-demand module loading (Roman Reiss) [#1249](https://github.com/nodejs/node/pull/1249)
* \[[`c9207f7fc2`](https://github.com/nodejs/node/commit/c9207f7fc2)] - **fs**: fix corruption in writeFile and writeFileSync (Olov Lassus) [#1063](https://github.com/nodejs/node/pull/1063)
* \[[`2db758c562`](https://github.com/nodejs/node/commit/2db758c562)] - **iojs**: introduce internal modules (Vladimir Kurchatkin) [#848](https://github.com/nodejs/node/pull/848)
* \[[`36f017afaf`](https://github.com/nodejs/node/commit/36f017afaf)] - **js2c**: fix module id generation on windows (Ben Noordhuis) [#1281](https://github.com/nodejs/node/pull/1281)
* \[[`1832743e18`](https://github.com/nodejs/node/commit/1832743e18)] - **lib**: add missing `new` for errors lib/\*.js (Mayhem) [#1246](https://github.com/nodejs/node/pull/1246)
* \[[`ea37ac04f4`](https://github.com/nodejs/node/commit/ea37ac04f4)] - **src**: ignore ENOTCONN on shutdown race with child (Ben Noordhuis) [#1214](https://github.com/nodejs/node/pull/1214)
* \[[`f06b16f2e9`](https://github.com/nodejs/node/commit/f06b16f2e9)] - **src**: fix minor memleak in preload-modules (Ali Ijaz Sheikh) [#1265](https://github.com/nodejs/node/pull/1265)
* \[[`2903410aa8`](https://github.com/nodejs/node/commit/2903410aa8)] - **src**: don't lazy-load timer globals (Ben Noordhuis) [#1280](https://github.com/nodejs/node/pull/1280)
* \[[`2e5b87a147`](https://github.com/nodejs/node/commit/2e5b87a147)] - **src**: remove unnecessary environment lookups (Ben Noordhuis) [#1238](https://github.com/nodejs/node/pull/1238)
* \[[`7e88a9322c`](https://github.com/nodejs/node/commit/7e88a9322c)] - **src**: make accessors immune to context confusion (Ben Noordhuis) [#1238](https://github.com/nodejs/node/pull/1238)
* \[[`c8fa8ccdbc`](https://github.com/nodejs/node/commit/c8fa8ccdbc)] - **streams**: use strict on \_stream\_wrap (Brendan Ashworth) [#1279](https://github.com/nodejs/node/pull/1279)
* \[[`8a945814dd`](https://github.com/nodejs/node/commit/8a945814dd)] - **string\_decoder**: optimize write() (Brian White) [#1209](https://github.com/nodejs/node/pull/1209)
* \[[`8d1c87ea0a`](https://github.com/nodejs/node/commit/8d1c87ea0a)] - **test**: fix race in parallel/test-vm-debug-context (Ben Noordhuis) [#1294](https://github.com/nodejs/node/pull/1294)
* \[[`955c1508da`](https://github.com/nodejs/node/commit/955c1508da)] - **test**: reduce sequential/test-fs-watch flakiness (Roman Reiss) [#1275](https://github.com/nodejs/node/pull/1275)
* \[[`77c2da10fd`](https://github.com/nodejs/node/commit/77c2da10fd)] - **timers**: make Timer.close idempotent (Petka Antonov) [#1288](https://github.com/nodejs/node/pull/1288)
* \[[`776b73b243`](https://github.com/nodejs/node/commit/776b73b243)] - **timers**: cleanup interval handling (Jeremiah Senkpiel) [#1272](https://github.com/nodejs/node/pull/1272)
* \[[`caf0b36de3`](https://github.com/nodejs/node/commit/caf0b36de3)] - **timers**: assure setTimeout callback only runs once (Roman Reiss) [#1231](https://github.com/nodejs/node/pull/1231)
* \[[`2ccc8f3970`](https://github.com/nodejs/node/commit/2ccc8f3970)] - **tls\_wrap**: fix this incredibly stupid leak (Fedor Indutny) [#1244](https://github.com/nodejs/node/pull/1244)
* \[[`e74b5d278c`](https://github.com/nodejs/node/commit/e74b5d278c)] - **tls\_wrap**: fix BIO leak on SSL error (Fedor Indutny) [#1244](https://github.com/nodejs/node/pull/1244)
* \[[`ba93c583bc`](https://github.com/nodejs/node/commit/ba93c583bc)] - **win,node-gyp**: optionally allow node.exe/iojs.exe to be renamed (Bert Belder) [#1266](https://github.com/nodejs/node/pull/1266)
* \[[`08acf1352c`](https://github.com/nodejs/node/commit/08acf1352c)] - **win,node-gyp**: make delay-load hook optional (Bert Belder) [#1266](https://github.com/nodejs/node/pull/1266)
* \[[`3d46fefe0c`](https://github.com/nodejs/node/commit/3d46fefe0c)] - **win,node-gyp**: allow node.exe/iojs.exe to be renamed (Bert Belder) [#1251](https://github.com/nodejs/node/pull/1251)

<a id="1.6.2"></a>

## 2015-03-23, Version 1.6.2, @rvagg

### Notable changes

* **Windows**: The ongoing work in improving the state of Windows support has resulted in full test suite passes once again. As noted in the release notes for v1.4.2, CI system and configuration problems prevented it from properly reporting problems with the Windows tests, the problems with the CI and the codebase appear to have been fully resolved.
* **FreeBSD**: A [kernel bug](https://lists.freebsd.org/pipermail/freebsd-current/2015-March/055043.html) impacting io.js/Node.js was [discovered](https://github.com/joyent/node/issues/9326) and a patch has been introduced to prevent it causing problems for io.js (Fedor Indutny) [#1218](https://github.com/nodejs/node/pull/1218).
* **module**: you can now `require('.')` instead of having to `require('./')`, this is considered a bugfix (Michaël Zasso) [#1185](https://github.com/nodejs/node/pull/1185).
* **v8**: updated to 4.1.0.25 including patches for `--max_old_space_size` values above `4096` and Solaris support, both of which are already included in io.js.

### Known issues

* Possible small memory leak(s) may still exist but have yet to be properly identified, details at [#1075](https://github.com/nodejs/node/issues/1075).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* Not possible to build io.js as a static library [#686](https://github.com/nodejs/node/issues/686)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)

### Commits

* \[[`fe4434b77a`](https://github.com/nodejs/node/commit/fe4434b77a)] - **deps**: upgrade v8 to 4.1.0.25 (Johan Bergström) [#1224](https://github.com/nodejs/node/pull/1224)
* \[[`d8f383ba3f`](https://github.com/nodejs/node/commit/d8f383ba3f)] - **doc**: update AUTHORS list (Rod Vagg) [#1234](https://github.com/nodejs/node/pull/1234)
* \[[`bc9c1a5a7b`](https://github.com/nodejs/node/commit/bc9c1a5a7b)] - **doc**: fix typo in CHANGELOG (Mathieu Darse) [#1230](https://github.com/nodejs/node/pull/1230)
* \[[`99c79f8d41`](https://github.com/nodejs/node/commit/99c79f8d41)] - **doc**: call js function in null context (Ben Noordhuis) [#1125](https://github.com/nodejs/node/pull/1125)
* \[[`55abf34be5`](https://github.com/nodejs/node/commit/55abf34be5)] - **doc**: don't use `using namespace v8` (Ben Noordhuis) [#1125](https://github.com/nodejs/node/pull/1125)
* \[[`c4e1b82120`](https://github.com/nodejs/node/commit/c4e1b82120)] - **doc**: replace v8::Handle\<T> with v8::Local\<T> (Ben Noordhuis) [#1125](https://github.com/nodejs/node/pull/1125)
* \[[`2f1b78347c`](https://github.com/nodejs/node/commit/2f1b78347c)] - **doc**: remove unnecessary v8::HandleScopes (Ben Noordhuis) [#1125](https://github.com/nodejs/node/pull/1125)
* \[[`409d413363`](https://github.com/nodejs/node/commit/409d413363)] - **doc**: remove uses of v8::Isolate::GetCurrent() (Ben Noordhuis) [#1125](https://github.com/nodejs/node/pull/1125)
* \[[`33fea6ed5f`](https://github.com/nodejs/node/commit/33fea6ed5f)] - **lib**: don't penalize setInterval() common case (Ben Noordhuis) [#1221](https://github.com/nodejs/node/pull/1221)
* \[[`31da9758a0`](https://github.com/nodejs/node/commit/31da9758a0)] - **lib**: don't penalize setTimeout() common case (Ben Noordhuis) [#1221](https://github.com/nodejs/node/pull/1221)
* \[[`6fc5e95354`](https://github.com/nodejs/node/commit/6fc5e95354)] - **module**: allow require('.') (Michaël Zasso) [#1185](https://github.com/nodejs/node/pull/1185)
* \[[`9ae1a61214`](https://github.com/nodejs/node/commit/9ae1a61214)] - **node**: ensure that streams2 won't `.end()` stdin (Fedor Indutny) [#1233](https://github.com/nodejs/node/pull/1233)
* \[[`b64983d77c`](https://github.com/nodejs/node/commit/b64983d77c)] - **src**: reset signal handler to SIG\_DFL on FreeBSD (Fedor Indutny) [#1218](https://github.com/nodejs/node/pull/1218)
* \[[`9705a34e96`](https://github.com/nodejs/node/commit/9705a34e96)] - **test**: move sequential/test-signal-unregister (Ben Noordhuis) [#1227](https://github.com/nodejs/node/pull/1227)
* \[[`10a9c00563`](https://github.com/nodejs/node/commit/10a9c00563)] - **test**: fix timing issue in signal test (Ben Noordhuis) [#1227](https://github.com/nodejs/node/pull/1227)
* \[[`999fbe9d96`](https://github.com/nodejs/node/commit/999fbe9d96)] - **test**: fix crypto-binary-default bad crypto check (Brendan Ashworth) [#1141](https://github.com/nodejs/node/pull/1141)
* \[[`2b3b2d392f`](https://github.com/nodejs/node/commit/2b3b2d392f)] - **test**: add setTimeout/setInterval multi-arg tests (Ben Noordhuis) [#1221](https://github.com/nodejs/node/pull/1221)
* \[[`849319a260`](https://github.com/nodejs/node/commit/849319a260)] - **util**: Check input to util.inherits (Connor Peet) [#1240](https://github.com/nodejs/node/pull/1240)
* \[[`cf081a4712`](https://github.com/nodejs/node/commit/cf081a4712)] - **vm**: fix crash on fatal error in debug context (Ben Noordhuis) [#1229](https://github.com/nodejs/node/pull/1229)

<a id="1.6.1"></a>

## 2015-03-20, Version 1.6.1, @rvagg

### Notable changes

* **path**: New type-checking on `path.resolve()` [#1153](https://github.com/nodejs/node/pull/1153) uncovered some edge-cases being relied upon in the wild, most notably `path.dirname(undefined)`. Type-checking has been loosened for `path.dirname()`, `path.basename()`, and `path.extname()` (Colin Ihrig) [#1216](https://github.com/nodejs/node/pull/1216).
* **querystring**: Internal optimizations in `querystring.parse()` and `querystring.stringify()` [#847](https://github.com/nodejs/node/pull/847) prevented `Number` literals from being properly converted via `querystring.escape()` [#1208](https://github.com/nodejs/node/issues/1208), exposing a blind-spot in the test suite. The bug and the tests have now been fixed (Jeremiah Senkpiel) [#1213](https://github.com/nodejs/node/pull/1213).

### Known issues

* Possible remaining TLS-related memory leak(s), details at [#1075](https://github.com/nodejs/node/issues/1075).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* Not possible to build io.js as a static library [#686](https://github.com/nodejs/node/issues/686)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)

### Commits

* \[[`3b9eab9779`](https://github.com/nodejs/node/commit/3b9eab9779)] - **build**: make check aliases test (Johan Bergström) [#1211](https://github.com/nodejs/node/pull/1211)
* \[[`4c731042d4`](https://github.com/nodejs/node/commit/4c731042d4)] - **configure**: use cc and c++ as defaults on os x (Ben Noordhuis) [#1210](https://github.com/nodejs/node/pull/1210)
* \[[`8de78e470d`](https://github.com/nodejs/node/commit/8de78e470d)] - **path**: reduce type checking on some methods (cjihrig) [#1216](https://github.com/nodejs/node/pull/1216)
* \[[`c9aec2b716`](https://github.com/nodejs/node/commit/c9aec2b716)] - **querystring**: fix broken stringifyPrimitive (Jeremiah Senkpiel) [#1213](https://github.com/nodejs/node/pull/1213)
* \[[`a89f5c2156`](https://github.com/nodejs/node/commit/a89f5c2156)] - **querystring**: parse numbers correctly (Jeremiah Senkpiel) [#1213](https://github.com/nodejs/node/pull/1213)
* \[[`2034137385`](https://github.com/nodejs/node/commit/2034137385)] - **smalloc**: don't mix malloc() and new char\[] (Ben Noordhuis) [#1205](https://github.com/nodejs/node/pull/1205)

<a id="1.6.0"></a>

## 2015-03-19, Version 1.6.0, @chrisdickinson

### Notable changes

* **node**: a new `-r` or `--require` command-line option can be used to pre-load modules at start-up (Ali Ijaz Sheikh) [#881](https://github.com/nodejs/node/pull/881).
* **querystring**: `parse()` and `stringify()` are now faster (Brian White) [#847](https://github.com/nodejs/node/pull/847).
* **http**: the `http.ClientRequest#flush()` method has been deprecated and replaced with `http.ClientRequest#flushHeaders()` to match the same change now in Node.js v0.12 as per [joyent/node#9048](https://github.com/joyent/node/pull/9048) (Yosuke Furukawa) [#1156](https://github.com/nodejs/node/pull/1156).
* **net**: allow `server.listen()` to accept a `String` option for `port`, e.g. `{ port: "1234" }`, to match the same option being accepted in `net.connect()` as of [joyent/node#9268](https://github.com/joyent/node/pull/9268) (Ben Noordhuis) [#1116](https://github.com/nodejs/node/pull/1116).
* **tls**: further work on the reported memory leak although there appears to be a minor leak remaining for the use-case in question, track progress at [#1075](https://github.com/nodejs/node/issues/1075).
* **v8**: backport a fix for an integer overflow when `--max_old_space_size` values above `4096` are used (Ben Noordhuis) [#1166](https://github.com/nodejs/node/pull/1166).
* **platforms**: the io.js CI system now reports passes on **FreeBSD** and **SmartOS** (_Solaris_).
* **npm**: upgrade npm to 2.7.1. See [npm CHANGELOG.md](https://github.com/npm/npm/blob/master/CHANGELOG.md#v271-2015-03-05) for details. Summary:
  * [`6823807`](https://github.com/npm/npm/commit/6823807bba) [#7121](https://github.com/npm/npm/issues/7121) `npm install --save` for Git dependencies saves the URL passed in, instead of the temporary directory used to clone the remote repo. Fixes using Git dependencies when shrinkwwapping. In the process, rewrote the Git dependency caching code. Again. No more single-letter variable names, and a much clearer workflow. ([@othiym23](https://github.com/othiym23))
  * [`abdd040`](https://github.com/npm/npm/commit/abdd040da9) read-package-json\@1.3.2: Provide more helpful error messages when JSON parse errors are encountered by using a more forgiving JSON parser than JSON.parse. ([@smikes](https://github.com/smikes))
  * [`c56cfcd`](https://github.com/npm/npm/commit/c56cfcd79c) [#7525](https://github.com/npm/npm/issues/7525) `npm dedupe` handles scoped packages. ([@KidkArolis](https://github.com/KidkArolis))
  * [`4ef1412`](https://github.com/npm/npm/commit/4ef1412d00) [#7075](https://github.com/npm/npm/issues/7075) If you try to tag a release as a valid semver range, `npm publish` and `npm tag` will error early instead of proceeding. ([@smikes](https://github.com/smikes))

### Known issues

* Possible remaining TLS-related memory leak(s), details at [#1075](https://github.com/nodejs/node/issues/1075).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* Not possible to build io.js as a static library [#686](https://github.com/nodejs/node/issues/686)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)

### Commits

* \[[`a84ea66b35`](https://github.com/nodejs/node/commit/a84ea66b35)] - **deps**: upgrade to openssl-1.0.1m (Shigeki Ohtsu) [#1206](https://github.com/nodejs/node/pull/1206)
* \[[`3bc445f6c2`](https://github.com/nodejs/node/commit/3bc445f6c2)] - **doc**: fix a broken collaborator github link (Aleksanteri Negru-Vode) [#1204](https://github.com/nodejs/node/pull/1204)
* \[[`813a536126`](https://github.com/nodejs/node/commit/813a536126)] - **buffer**: removing duplicate code (Thorsten Lorenz) [#1144](https://github.com/nodejs/node/pull/1144)
* \[[`1514b82355`](https://github.com/nodejs/node/commit/1514b82355)] - **(SEMVER-MINOR) src**: add -r/--require flags for preloading modules (Ali Ijaz Sheikh) [#881](https://github.com/nodejs/node/pull/881)
* \[[`f600111d82`](https://github.com/nodejs/node/commit/f600111d82)] - **test**: cache lazy properties, fix style nits (Rod Vagg) [#1196](https://github.com/nodejs/node/pull/1196)
* \[[`3038b8ee6a`](https://github.com/nodejs/node/commit/3038b8ee6a)] - **test**: double timeout in tls-wrap-timeout.js (Fedor Indutny) [#1201](https://github.com/nodejs/node/pull/1201)
* \[[`dd37fb4c48`](https://github.com/nodejs/node/commit/dd37fb4c48)] - **build**: remove incorrect argument in vcbuild.bat (Jeremiah Senkpiel) [#1198](https://github.com/nodejs/node/pull/1198)
* \[[`2b2e48a4b9`](https://github.com/nodejs/node/commit/2b2e48a4b9)] - **lib**: don't error in repl when cwd doesn't exist (Ben Noordhuis) [#1194](https://github.com/nodejs/node/pull/1194)
* \[[`2c6f79c08c`](https://github.com/nodejs/node/commit/2c6f79c08c)] - **src**: don't error at startup when cwd doesn't exist (Ben Noordhuis) [#1194](https://github.com/nodejs/node/pull/1194)
* \[[`c15e81afdd`](https://github.com/nodejs/node/commit/c15e81afdd)] - **test**: Introduce knowledge of FreeBSD jails (Johan Bergström) [#1167](https://github.com/nodejs/node/pull/1167)
* \[[`fe0f015c51`](https://github.com/nodejs/node/commit/fe0f015c51)] - **src**: fix crypto bio integer wraparound on 32 bits (Ben Noordhuis) [#1192](https://github.com/nodejs/node/pull/1192)
* \[[`2b63bcd247`](https://github.com/nodejs/node/commit/2b63bcd247)] - **doc**: add yosuke-furukawa as collaborator (Yosuke Furukawa) [#1183](https://github.com/nodejs/node/pull/1183)
* \[[`69350baaef`](https://github.com/nodejs/node/commit/69350baaef)] - **doc**: update test section in CONTRIBUTING.md (Ben Noordhuis) [#1181](https://github.com/nodejs/node/pull/1181)
* \[[`3c8ae2d934`](https://github.com/nodejs/node/commit/3c8ae2d934)] - **doc**: add petkaantonov as collaborator (Petka Antonov) [#1179](https://github.com/nodejs/node/pull/1179)
* \[[`92c1ad97c0`](https://github.com/nodejs/node/commit/92c1ad97c0)] - **doc**: add silverwind as collaborator (Roman Reiss) [#1176](https://github.com/nodejs/node/pull/1176)
* \[[`14c74d5326`](https://github.com/nodejs/node/commit/14c74d5326)] - **doc**: add jbergstroem as collaborator (Johan Bergström) [#1175](https://github.com/nodejs/node/pull/1175)
* \[[`8b2363d2fd`](https://github.com/nodejs/node/commit/8b2363d2fd)] - **configure**: use gcc and g++ as CC and CXX defaults (Ben Noordhuis) [#1174](https://github.com/nodejs/node/pull/1174)
* \[[`08ec897f82`](https://github.com/nodejs/node/commit/08ec897f82)] - **doc**: fix typo in buffer module documentation (Alex Yursha) [#1169](https://github.com/nodejs/node/pull/1169)
* \[[`c638dad567`](https://github.com/nodejs/node/commit/c638dad567)] - **benchmark**: add output format option \[csv] (Brendan Ashworth) [#777](https://github.com/nodejs/node/pull/777)
* \[[`97d8d4928d`](https://github.com/nodejs/node/commit/97d8d4928d)] - **benchmark**: add plot\_csv R graphing script (Brendan Ashworth) [#777](https://github.com/nodejs/node/pull/777)
* \[[`22793da485`](https://github.com/nodejs/node/commit/22793da485)] - **v8**: fix --max\_old\_space\_size=4096 integer overflow (Ben Noordhuis) [#1166](https://github.com/nodejs/node/pull/1166)
* \[[`b2e00e38dc`](https://github.com/nodejs/node/commit/b2e00e38dc)] - **(SEMVER-MINOR) http**: add flushHeaders and deprecate flush (Yosuke Furukawa) [#1156](https://github.com/nodejs/node/pull/1156)
* \[[`68d4bed2fd`](https://github.com/nodejs/node/commit/68d4bed2fd)] - **make**: remove node\_dtrace from cpplint excludes (Julien Gilli) [joyent/node#8741](https://github.com/joyent/node/pull/8741)
* \[[`30666f22ca`](https://github.com/nodejs/node/commit/30666f22ca)] - **net**: use cached peername to resolve remote fields (James Hartig) [joyent/node#9366](https://github.com/joyent/node/pull/9366)
* \[[`e6e616fdcb`](https://github.com/nodejs/node/commit/e6e616fdcb)] - **doc**: fix '\\' typos on Windows (Steven Vercruysse) [joyent/node#9412](https://github.com/joyent/node/pull/9412)
* \[[`89bf6c05e9`](https://github.com/nodejs/node/commit/89bf6c05e9)] - **build**: allow custom PackageMaker path (Julien Gilli) [joyent/node#9377](https://github.com/joyent/node/pull/9377)
* \[[`f58e59649d`](https://github.com/nodejs/node/commit/f58e59649d)] - **lib**: remove broken NODE\_MODULE\_CONTEXTS feature (Ben Noordhuis) [#1162](https://github.com/nodejs/node/pull/1162)
* \[[`2551c1d2ca`](https://github.com/nodejs/node/commit/2551c1d2ca)] - **src**: use Number::New() for heapTotal/heapUsed (Ben Noordhuis) [#1148](https://github.com/nodejs/node/pull/1148)
* \[[`4f394998ba`](https://github.com/nodejs/node/commit/4f394998ba)] - **src**: don't create js string twice on error (Ben Noordhuis) [#1148](https://github.com/nodejs/node/pull/1148)
* \[[`eb995d6822`](https://github.com/nodejs/node/commit/eb995d6822)] - **path**: add type checking for path inputs (cjihrig) [#1153](https://github.com/nodejs/node/pull/1153)
* \[[`a28945b128`](https://github.com/nodejs/node/commit/a28945b128)] - **doc**: reflect new require('events') behaviour (Alex Yursha) [#975](https://github.com/nodejs/node/pull/975)
* \[[`85a92a37ef`](https://github.com/nodejs/node/commit/85a92a37ef)] - **querystring**: optimize parse and stringify (Brian White) [#847](https://github.com/nodejs/node/pull/847)
* \[[`65d0a8eca8`](https://github.com/nodejs/node/commit/65d0a8eca8)] - **deps**: make node-gyp work with io.js (cjihrig) [#990](https://github.com/nodejs/node/pull/990)
* \[[`7d0baf1741`](https://github.com/nodejs/node/commit/7d0baf1741)] - **deps**: upgrade npm to 2.7.1 (Forrest L Norvell) [#1142](https://github.com/nodejs/node/pull/1142)
* \[[`4eb8810a27`](https://github.com/nodejs/node/commit/4eb8810a27)] - **tls**: re-enable `.writev()` on TLSWrap (Fedor Indutny) [#1155](https://github.com/nodejs/node/pull/1155)
* \[[`e90ed790c3`](https://github.com/nodejs/node/commit/e90ed790c3)] - **tls**: fix leak on `DoWrite()` errors (Fedor Indutny) [#1154](https://github.com/nodejs/node/pull/1154)
* \[[`056ed4b0c9`](https://github.com/nodejs/node/commit/056ed4b0c9)] - **src**: revert -r/--require flags (Chris Dickinson) [#1150](https://github.com/nodejs/node/pull/1150)
* \[[`7a5b023bac`](https://github.com/nodejs/node/commit/7a5b023bac)] - **doc**: fix vm module examples (FangDun Cai) [#1147](https://github.com/nodejs/node/pull/1147)
* \[[`7bde3f1a8f`](https://github.com/nodejs/node/commit/7bde3f1a8f)] - **(SEMVER-MINOR) src**: add -r/--require flags for preloading modules (Ali Ijaz Sheikh) [#881](https://github.com/nodejs/node/pull/881)
* \[[`53e200acc2`](https://github.com/nodejs/node/commit/53e200acc2)] - **test**: fix test-http-content-length (Jeremiah Senkpiel) [#1145](https://github.com/nodejs/node/pull/1145)
* \[[`d8c4a932c9`](https://github.com/nodejs/node/commit/d8c4a932c9)] - **crypto**: add deprecated ValiCert CA for cross cert (Shigeki Ohtsu) [#1135](https://github.com/nodejs/node/pull/1135)
* \[[`82f067e60b`](https://github.com/nodejs/node/commit/82f067e60b)] - **test**: fix ext commands to be double quoted (Shigeki Ohtsu) [#1122](https://github.com/nodejs/node/pull/1122)
* \[[`5ecdc0314d`](https://github.com/nodejs/node/commit/5ecdc0314d)] - **test**: add test for reading a large file through a pipe (Santiago Gimeno) [#1074](https://github.com/nodejs/node/pull/1074)
* \[[`a6af709489`](https://github.com/nodejs/node/commit/a6af709489)] - **fs**: use stat.st\_size only to read regular files (Santiago Gimeno) [#1074](https://github.com/nodejs/node/pull/1074)
* \[[`0782c24993`](https://github.com/nodejs/node/commit/0782c24993)] - **test**: fix readfile-zero-byte-liar test (Santiago Gimeno) [#1074](https://github.com/nodejs/node/pull/1074)
* \[[`e2c9040995`](https://github.com/nodejs/node/commit/e2c9040995)] - **src**: do not leak handles on debug and exit (Fedor Indutny) [#1133](https://github.com/nodejs/node/pull/1133)
* \[[`8c4f0df464`](https://github.com/nodejs/node/commit/8c4f0df464)] - **v8**: fix build on solaris platforms (Johan Bergström) [#1079](https://github.com/nodejs/node/pull/1079)
* \[[`41c9daa143`](https://github.com/nodejs/node/commit/41c9daa143)] - **build**: fix incorrect set in vcbuild.bat (Bert Belder)
* \[[`07c066724c`](https://github.com/nodejs/node/commit/07c066724c)] - **buffer**: align chunks on 8-byte boundary (Fedor Indutny) [#1126](https://github.com/nodejs/node/pull/1126)
* \[[`d33a647b4b`](https://github.com/nodejs/node/commit/d33a647b4b)] - **doc**: make tools/update-authors.sh cross-platform (Ben Noordhuis) [#1121](https://github.com/nodejs/node/pull/1121)
* \[[`8453fbc879`](https://github.com/nodejs/node/commit/8453fbc879)] - **https**: don't overwrite servername option (skenqbx) [#1110](https://github.com/nodejs/node/pull/1110)
* \[[`60dac07b06`](https://github.com/nodejs/node/commit/60dac07b06)] - **doc**: add Malte-Thorben Bruns to .mailmap (Ben Noordhuis) [#1118](https://github.com/nodejs/node/pull/1118)
* \[[`480b48244f`](https://github.com/nodejs/node/commit/480b48244f)] - **(SEMVER-MINOR) lib**: allow server.listen({ port: "1234" }) (Ben Noordhuis) [#1116](https://github.com/nodejs/node/pull/1116)
* \[[`80e14d736e`](https://github.com/nodejs/node/commit/80e14d736e)] - **doc**: move checkServerIdentity option to tls.connect() (skenqbx) [#1107](https://github.com/nodejs/node/pull/1107)
* \[[`684a5878b6`](https://github.com/nodejs/node/commit/684a5878b6)] - **doc**: fix missing periods in url.markdown (Ryuichi Okumura) [#1115](https://github.com/nodejs/node/pull/1115)
* \[[`8431fc53f1`](https://github.com/nodejs/node/commit/8431fc53f1)] - **tls\_wrap**: proxy handle methods in prototype (Fedor Indutny) [#1108](https://github.com/nodejs/node/pull/1108)
* \[[`8070b1ff99`](https://github.com/nodejs/node/commit/8070b1ff99)] - **buffer**: Don't assign .parent if none exists (Trevor Norris) [#1109](https://github.com/nodejs/node/pull/1109)

<a id="1.5.1"></a>

## 2015-03-09, Version 1.5.1, @rvagg

### Notable changes

* **tls**: The reported TLS memory leak has been at least partially resolved via various commits in this release. Current testing indicated that there _may_ still be some leak problems. Track complete progress at [#1075](https://github.com/nodejs/node/issues/1075).
* **http**: Fixed an error reported at [joyent/node#9348](https://github.com/joyent/node/issues/9348) and [npm/npm#7349](https://github.com/npm/npm/issues/7349). Pending data was not being fully read upon an `'error'` event leading to an assertion failure on `socket.destroy()`. (Fedor Indutny) [#1103](https://github.com/nodejs/node/pull/1103)

### Known issues

* Possible remaining TLS-related memory leak(s), details at [#1075](https://github.com/nodejs/node/issues/1075).
* Windows still reports some minor test failures and we are continuing to address all of these as a priority. See [#1005](https://github.com/nodejs/node/issues/1005).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* Not possible to build io.js as a static library [#686](https://github.com/nodejs/node/issues/686)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)

### Commits

* \[[`030a92347d`](https://github.com/nodejs/node/commit/030a92347d)] - **benchmark**: chunky http client benchmark variation (Rudi Cilibrasi) [#228](https://github.com/nodejs/node/pull/228)
* \[[`3b57819b58`](https://github.com/nodejs/node/commit/3b57819b58)] - **crypto**: fix leak in SafeX509ExtPrint (Fedor Indutny) [#1087](https://github.com/nodejs/node/pull/1087)
* \[[`f8c893dd39`](https://github.com/nodejs/node/commit/f8c893dd39)] - **doc**: fix confusion markdown in util.markdown (Yazhong Liu) [#1097](https://github.com/nodejs/node/pull/1097)
* \[[`e763220f66`](https://github.com/nodejs/node/commit/e763220f66)] - **doc**: update clang version prerequisite (Brendan Ashworth) [#1094](https://github.com/nodejs/node/pull/1094)
* \[[`0f7c8ebeea`](https://github.com/nodejs/node/commit/0f7c8ebeea)] - **doc**: replace article "an" with "a" in net docs (Evan Lucas) [#1093](https://github.com/nodejs/node/pull/1093)
* \[[`cf565b5516`](https://github.com/nodejs/node/commit/cf565b5516)] - **fs**: fix .write() not coercing non-string values (Jeremiah Senkpiel) [#1102](https://github.com/nodejs/node/pull/1102)
* \[[`1a3ca8223e`](https://github.com/nodejs/node/commit/1a3ca8223e)] - **http\_client**: ensure empty socket on error (Fedor Indutny) [#1103](https://github.com/nodejs/node/pull/1103)
* \[[`8670613d2d`](https://github.com/nodejs/node/commit/8670613d2d)] - **node\_crypto\_bio**: adjust external memory size (Fedor Indutny) [#1085](https://github.com/nodejs/node/pull/1085)
* \[[`528d8786ff`](https://github.com/nodejs/node/commit/528d8786ff)] - **src**: fix memory leak in fs.writeSync error path (Ben Noordhuis) [#1092](https://github.com/nodejs/node/pull/1092)
* \[[`648fc63cd1`](https://github.com/nodejs/node/commit/648fc63cd1)] - **src**: fix mismatched delete\[] in src/node\_file.cc (Ben Noordhuis) [#1092](https://github.com/nodejs/node/pull/1092)
* \[[`9f7c9811e2`](https://github.com/nodejs/node/commit/9f7c9811e2)] - **src**: add missing Context::Scope (Ben Noordhuis) [#1084](https://github.com/nodejs/node/pull/1084)
* \[[`fe36076c78`](https://github.com/nodejs/node/commit/fe36076c78)] - **stream\_base**: WriteWrap::New/::Dispose (Fedor Indutny) [#1090](https://github.com/nodejs/node/pull/1090)
* \[[`7f4c95e160`](https://github.com/nodejs/node/commit/7f4c95e160)] - **tls**: do not leak WriteWrap objects (Fedor Indutny) [#1090](https://github.com/nodejs/node/pull/1090)
* \[[`4bd3620382`](https://github.com/nodejs/node/commit/4bd3620382)] - **url**: remove redundant assignment in url.parse (Alex Kocharin) [#1095](https://github.com/nodejs/node/pull/1095)

<a id="1.5.0"></a>

## 2015-03-06, Version 1.5.0, @rvagg

### Notable changes

* **buffer**: New `Buffer#indexOf()` method, modelled off [`Array#indexOf()`](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Array/indexOf). Accepts a String, Buffer or a Number. Strings are interpreted as UTF8. (Trevor Norris) [#561](https://github.com/nodejs/node/pull/561)
* **fs**: `options` object properties in `'fs'` methods no longer perform a `hasOwnProperty()` check, thereby allowing options objects to have prototype properties that apply. (Jonathan Ong) [#635](https://github.com/nodejs/node/pull/635)
* **tls**: A likely TLS memory leak was reported by PayPal. Some of the recent changes in **stream\_wrap** appear to be to blame. The initial fix is in [#1078](https://github.com/nodejs/node/pull/1078), you can track the progress toward closing the leak at [#1075](https://github.com/nodejs/node/issues/1075) (Fedor Indutny).
* **npm**: Upgrade npm to 2.7.0. See [npm CHANGELOG.md](https://github.com/npm/npm/blob/master/CHANGELOG.md#v270-2015-02-26) for details including why this is a semver-minor when it could have been semver-major. Summary:
  * [`145af65`](https://github.com/npm/npm/commit/145af6587f45de135cc876be2027ed818ed4ca6a)
    [#4887](https://github.com/npm/npm/issues/4887) Replace calls to the
    `node-gyp` script bundled with npm by passing the
    `--node-gyp=/path/to/node-gyp` option to npm. Swap in `pangyp` or a version
    of `node-gyp` modified to work better with io.js without having to touch
    npm's code!  ([@ackalker](https://github.com/ackalker))
  * [`2f6a1df`](https://github.com/npm/npm/commit/2f6a1df3e1e3e0a3bc4abb69e40f59a64204e7aa)
    [#1999](https://github.com/npm/npm/issues/1999) Only run `stop` and `start`
    scripts (plus their pre- and post- scripts) when there's no `restart` script
    defined. This makes it easier to support graceful restarts of services
    managed by npm.  ([@watilde](https://github.com/watilde) /
    [@scien](https://github.com/scien))
  * [`448efd0`](https://github.com/npm/npm/commit/448efd0eaa6f97af0889bf47efc543a1ea2f8d7e)
    [#2853](https://github.com/npm/npm/issues/2853) Add support for `--dev` and
    `--prod` to `npm ls`, so that you can list only the trees of production or
    development dependencies, as desired.
    ([@watilde](https://github.com/watilde))
  * [`a0a8777`](https://github.com/npm/npm/commit/a0a87777af8bee180e4e9321699f050c29ed5ac4)
    [#7463](https://github.com/npm/npm/issues/7463) Split the list printed by
    `npm run-script` into lifecycle scripts and scripts directly invoked via `npm
    run-script`. ([@watilde](https://github.com/watilde))
  * [`a5edc17`](https://github.com/npm/npm/commit/a5edc17d5ef1435b468a445156a4a109df80f92b)
    [#6749](https://github.com/npm/npm/issues/6749) `init-package-json@1.3.1`:
    Support for passing scopes to `npm init` so packages are initialized as part
    of that scope / organization / team. ([@watilde](https://github.com/watilde))
* **TC**: Colin Ihrig (@cjihrig) resigned from the TC due to his desire to do more code and fewer meetings.

### Known issues

* Possible TLS-related memory leak, details at [#1075](https://github.com/nodejs/node/issues/1075).
* Windows still reports some minor test failures and we are continuing to address all of these as a priority. See [#1005](https://github.com/nodejs/node/issues/1005).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* Not possible to build io.js as a static library [#686](https://github.com/nodejs/node/issues/686)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)

### Commits

* \[[`b27931b0fe`](https://github.com/nodejs/node/commit/b27931b0fe)] - **benchmark**: fix `wrk` check (Brian White) [#1076](https://github.com/nodejs/node/pull/1076)
* \[[`2b79052494`](https://github.com/nodejs/node/commit/2b79052494)] - **benchmark**: check for wrk ahead of running benchmarks (Johan Bergström) [#982](https://github.com/nodejs/node/pull/982)
* \[[`31421afe89`](https://github.com/nodejs/node/commit/31421afe89)] - **buffer**: reword Buffer.concat error message (Chris Dickinson) [joyent/node#8723](https://github.com/joyent/node/pull/8723)
* \[[`78581c8d90`](https://github.com/nodejs/node/commit/78581c8d90)] - **(SEMVER-MINOR) buffer**: add indexOf() method (Trevor Norris) [#561](https://github.com/nodejs/node/pull/561)
* \[[`37bb1df7c4`](https://github.com/nodejs/node/commit/37bb1df7c4)] - **build**: remove mdb from io.js (Johan Bergström) [#1023](https://github.com/nodejs/node/pull/1023)
* \[[`726671cb0e`](https://github.com/nodejs/node/commit/726671cb0e)] - **build**: add basic mips/mipsel support (Ben Noordhuis) [#1045](https://github.com/nodejs/node/pull/1045)
* \[[`a45d4f8fd6`](https://github.com/nodejs/node/commit/a45d4f8fd6)] - **build**: remove tools/wrk from the tree (Johan Bergström) [#982](https://github.com/nodejs/node/pull/982)
* \[[`dee07e2983`](https://github.com/nodejs/node/commit/dee07e2983)] - **deps**: make node-gyp work with io.js (cjihrig) [#990](https://github.com/nodejs/node/pull/990)
* \[[`fe14802fb7`](https://github.com/nodejs/node/commit/fe14802fb7)] - **deps**: upgrade npm to 2.7.0 (Forrest L Norvell) [#1080](https://github.com/nodejs/node/pull/1080)
* \[[`31142415de`](https://github.com/nodejs/node/commit/31142415de)] - **doc**: add TC meeting 2015-02-18 minutes (Rod Vagg) [#1051](https://github.com/nodejs/node/pull/1051)
* \[[`6190a2236b`](https://github.com/nodejs/node/commit/6190a2236b)] - **doc**: remove cjihrig from TC (cjihrig) [#1056](https://github.com/nodejs/node/pull/1056)
* \[[`9741291fe9`](https://github.com/nodejs/node/commit/9741291fe9)] - **doc**: fix child\_process heading depth (Sam Roberts) [#1038](https://github.com/nodejs/node/pull/1038)
* \[[`c8110692a5`](https://github.com/nodejs/node/commit/c8110692a5)] - **doc**: add explanations for querystring (Robert Kowalski) [joyent/node#9259](https://github.com/joyent/node/pull/9259)
* \[[`8fb711e06c`](https://github.com/nodejs/node/commit/8fb711e06c)] - **doc**: fix default value of opts.decodeURIComponent (h7lin) [joyent/node#9259](https://github.com/joyent/node/pull/9259)
* \[[`6433ad1eef`](https://github.com/nodejs/node/commit/6433ad1eef)] - **doc**: add missing newline in CHANGELOG (Rod Vagg)
* \[[`555a7c48cf`](https://github.com/nodejs/node/commit/555a7c48cf)] - **events**: optimize listener array cloning (Brian White) [#1050](https://github.com/nodejs/node/pull/1050)
* \[[`4d0329ebeb`](https://github.com/nodejs/node/commit/4d0329ebeb)] - **(SEMVER-MINOR) fs**: remove unnecessary usage of .hasOwnProperty() (Jonathan Ong) [#635](https://github.com/nodejs/node/pull/635)
* \[[`4874182065`](https://github.com/nodejs/node/commit/4874182065)] - **http**: send Content-Length when possible (Christian Tellnes) [#1062](https://github.com/nodejs/node/pull/1062)
* \[[`08133f45c7`](https://github.com/nodejs/node/commit/08133f45c7)] - **http**: optimize outgoing requests (Brendan Ashworth) [#605](https://github.com/nodejs/node/pull/605)
* \[[`dccb69a21a`](https://github.com/nodejs/node/commit/dccb69a21a)] - **js\_stream**: fix leak of instances (Fedor Indutny) [#1078](https://github.com/nodejs/node/pull/1078)
* \[[`4ddd6406ce`](https://github.com/nodejs/node/commit/4ddd6406ce)] - **lib**: avoid .toLowerCase() call in Buffer#write() (Ben Noordhuis) [#1048](https://github.com/nodejs/node/pull/1048)
* \[[`bbf54a554a`](https://github.com/nodejs/node/commit/bbf54a554a)] - **lib**: hand-optimize Buffer constructor (Ben Noordhuis) [#1048](https://github.com/nodejs/node/pull/1048)
* \[[`9d2b89d06c`](https://github.com/nodejs/node/commit/9d2b89d06c)] - **net**: allow port 0 in connect() (cjihrig) [joyent/node#9268](https://github.com/joyent/node/pull/9268)
* \[[`e0835c9cda`](https://github.com/nodejs/node/commit/e0835c9cda)] - **node**: improve performance of nextTick (Trevor Norris) [#985](https://github.com/nodejs/node/pull/985)
* \[[`8f5f12bb48`](https://github.com/nodejs/node/commit/8f5f12bb48)] - **smalloc**: export constants from C++ (Vladimir Kurchatkin) [#920](https://github.com/nodejs/node/pull/920)
* \[[`0697f8b44d`](https://github.com/nodejs/node/commit/0697f8b44d)] - **smalloc**: validate arguments in js (Vladimir Kurchatkin) [#920](https://github.com/nodejs/node/pull/920)
* \[[`1640dedb3b`](https://github.com/nodejs/node/commit/1640dedb3b)] - **src**: fix ucs-2 buffer encoding regression (Ben Noordhuis) [#1042](https://github.com/nodejs/node/pull/1042)
* \[[`2eda2d6096`](https://github.com/nodejs/node/commit/2eda2d6096)] - **src**: fix external string length calculation (Ben Noordhuis) [#1042](https://github.com/nodejs/node/pull/1042)
* \[[`4aea16f214`](https://github.com/nodejs/node/commit/4aea16f214)] - **src**: rename confusingly named local variable (Ben Noordhuis) [#1042](https://github.com/nodejs/node/pull/1042)
* \[[`c9ee654290`](https://github.com/nodejs/node/commit/c9ee654290)] - **src**: simplify node::Utf8Value() (Ben Noordhuis) [#1042](https://github.com/nodejs/node/pull/1042)
* \[[`364cc7e08a`](https://github.com/nodejs/node/commit/364cc7e08a)] - **src**: remove NODE\_INVALID\_UTF8 environment variable (Ben Noordhuis) [#1042](https://github.com/nodejs/node/pull/1042)
* \[[`826cde8661`](https://github.com/nodejs/node/commit/826cde8661)] - **src**: fix gc heuristic for external twobyte strings (Ben Noordhuis) [#1042](https://github.com/nodejs/node/pull/1042)
* \[[`f5b7e18243`](https://github.com/nodejs/node/commit/f5b7e18243)] - **src**: remove unused code (Ben Noordhuis) [#1042](https://github.com/nodejs/node/pull/1042)
* \[[`4ae64b2626`](https://github.com/nodejs/node/commit/4ae64b2626)] - **src**: extract node env init out of process init (Petka Antonov) [#980](https://github.com/nodejs/node/pull/980)
* \[[`b150c9839e`](https://github.com/nodejs/node/commit/b150c9839e)] - **src**: fix -Wempty-body compiler warnings (Ben Noordhuis) [#974](https://github.com/nodejs/node/pull/974)
* \[[`fb284e2e4d`](https://github.com/nodejs/node/commit/fb284e2e4d)] - **src**: fix compiler warning in smalloc.cc (Ben Noordhuis) [#1055](https://github.com/nodejs/node/pull/1055)
* \[[`583a868bcd`](https://github.com/nodejs/node/commit/583a868bcd)] - **stream\_wrap**: add HandleScope's in uv callbacks (Fedor Indutny) [#1078](https://github.com/nodejs/node/pull/1078)
* \[[`e2fb733a95`](https://github.com/nodejs/node/commit/e2fb733a95)] - **test**: simplify parallel/test-stringbytes-external (Ben Noordhuis) [#1042](https://github.com/nodejs/node/pull/1042)
* \[[`7b554b1a8f`](https://github.com/nodejs/node/commit/7b554b1a8f)] - **test**: don't spawn child processes in domain test (Ben Noordhuis) [#974](https://github.com/nodejs/node/pull/974)
* \[[`b72fa03057`](https://github.com/nodejs/node/commit/b72fa03057)] - **test**: adds a test for undefined value in setHeader (Ken Perkins) [#970](https://github.com/nodejs/node/pull/970)
* \[[`563771d8b1`](https://github.com/nodejs/node/commit/563771d8b1)] - **test**: split parts out of host-headers test into its own test (Johan Bergström) [#1049](https://github.com/nodejs/node/pull/1049)
* \[[`671fbd5a9d`](https://github.com/nodejs/node/commit/671fbd5a9d)] - **test**: refactor all tests that depends on crypto (Johan Bergström) [#1049](https://github.com/nodejs/node/pull/1049)
* \[[`c7ad320472`](https://github.com/nodejs/node/commit/c7ad320472)] - **test**: check for openssl cli and provide path if it exists (Johan Bergström) [#1049](https://github.com/nodejs/node/pull/1049)
* \[[`71776f9057`](https://github.com/nodejs/node/commit/71776f9057)] - **test**: remove unused https imports (Johan Bergström) [#1049](https://github.com/nodejs/node/pull/1049)
* \[[`3d5726c4ad`](https://github.com/nodejs/node/commit/3d5726c4ad)] - **test**: introduce a helper that checks if crypto is available (Johan Bergström) [#1049](https://github.com/nodejs/node/pull/1049)
* \[[`d0e7c359a7`](https://github.com/nodejs/node/commit/d0e7c359a7)] - **test**: don't assume process.versions.openssl always is available (Johan Bergström) [#1049](https://github.com/nodejs/node/pull/1049)
* \[[`e1bf6709dc`](https://github.com/nodejs/node/commit/e1bf6709dc)] - **test**: fix racey-ness in tls-inception (Fedor Indutny) [#1040](https://github.com/nodejs/node/pull/1040)
* \[[`fd3ea29902`](https://github.com/nodejs/node/commit/fd3ea29902)] - **test**: fix test-fs-access when uid is 0 (Johan Bergström) [#1037](https://github.com/nodejs/node/pull/1037)
* \[[`5abfa930b8`](https://github.com/nodejs/node/commit/5abfa930b8)] - **test**: make destroyed-socket-write2.js more robust (Michael Dawson) [joyent/node#9270](https://github.com/joyent/node/pull/9270)
* \[[`1009130495`](https://github.com/nodejs/node/commit/1009130495)] - **tests**: fix race in test-http-curl-chunk-problem (Julien Gilli) [joyent/node#9301](https://github.com/joyent/node/pull/9301)
* \[[`bd1bd7e38d`](https://github.com/nodejs/node/commit/bd1bd7e38d)] - **timer**: Improve performance of callbacks (Ruben Verborgh) [#406](https://github.com/nodejs/node/pull/406)
* \[[`7b3b8acfa6`](https://github.com/nodejs/node/commit/7b3b8acfa6)] - **tls**: accept empty `net.Socket`s (Fedor Indutny) [#1046](https://github.com/nodejs/node/pull/1046)
* \[[`c09c90c1a9`](https://github.com/nodejs/node/commit/c09c90c1a9)] - **tls\_wrap**: do not hold persistent ref to parent (Fedor Indutny) [#1078](https://github.com/nodejs/node/pull/1078)
* \[[`3446ff417b`](https://github.com/nodejs/node/commit/3446ff417b)] - **tty**: do not add `shutdown` method to handle (Fedor Indutny) [#1073](https://github.com/nodejs/node/pull/1073)
* \[[`abb00cc915`](https://github.com/nodejs/node/commit/abb00cc915)] - **url**: throw for invalid values to url.format (Christian Tellnes) [#1036](https://github.com/nodejs/node/pull/1036)
* \[[`abd3ecfbd1`](https://github.com/nodejs/node/commit/abd3ecfbd1)] - **win,test**: fix test-stdin-from-file (Bert Belder) [#1067](https://github.com/nodejs/node/pull/1067)

<a id="1.4.3"></a>

## 2015-03-02, Version 1.4.3, @rvagg

### Notable changes

* **stream**: Fixed problems for platforms without `writev()` support, particularly Windows. Changes introduced in 1.4.1, via [#926](https://github.com/nodejs/node/pull/926), broke some functionality for these platforms, this has now been addressed. [#1008](https://github.com/nodejs/node/pull/1008) (Fedor Indutny)
* **arm**: We have the very beginnings of ARMv8 / ARM64 / AARCH64 support. An upgrade to OpenSSL 1.0.2 is one requirement for full support. [#1028](https://github.com/nodejs/node/pull/1028) (Ben Noordhuis)
* Add new collaborator: Julian Duque ([@julianduque](https://github.com/julianduque))

### Known issues

* Windows still reports some minor test failures and we are continuing to address all of these ASAP. See [#1005](https://github.com/nodejs/node/issues/1005).
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* Not possible to build io.js as a static library [#686](https://github.com/nodejs/node/issues/686)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)

### Commits

* \[[`ca3c50b789`](https://github.com/nodejs/node/commit/ca3c50b789)] - **build**: add basic arm64 support (Ben Noordhuis) [#1028](https://github.com/nodejs/node/pull/1028)
* \[[`08e89b1880`](https://github.com/nodejs/node/commit/08e89b1880)] - **doc**: update AUTHORS list (Rod Vagg) [#1018](https://github.com/nodejs/node/pull/1018)
* \[[`ea02d90cd0`](https://github.com/nodejs/node/commit/ea02d90cd0)] - **doc**: add julianduque as collaborator (Julian Duque) [#1021](https://github.com/nodejs/node/pull/1021)
* \[[`dfe7a17784`](https://github.com/nodejs/node/commit/dfe7a17784)] - **doc**: fix typos and sources in WORKING\_GROUPS.md (&! (bitandbang)) [#1022](https://github.com/nodejs/node/pull/1022)
* \[[`6d26990d32`](https://github.com/nodejs/node/commit/6d26990d32)] - **doc**: Clean up net.Socket (Ryan Scheel) [#951](https://github.com/nodejs/node/pull/951)
* \[[`c380ac6e98`](https://github.com/nodejs/node/commit/c380ac6e98)] - **doc**: suggest alternatives to deprecated APs (Benjamin Gruenbaum) [#1007](https://github.com/nodejs/node/pull/1007)
* \[[`3d6440cf2a`](https://github.com/nodejs/node/commit/3d6440cf2a)] - **src**: fix --without-ssl build (Ben Noordhuis) [#1027](https://github.com/nodejs/node/pull/1027)
* \[[`2b47fd2eb6`](https://github.com/nodejs/node/commit/2b47fd2eb6)] - **stream\_base**: `.writev()` has limited support (Fedor Indutny) [#1008](https://github.com/nodejs/node/pull/1008)

<a id="1.4.2"></a>

## 2015-02-28, Version 1.4.2, @rvagg

### Notable changes

* **tls**: A typo introduced in the TLSWrap changes in [#840](https://github.com/nodejs/node/pull/840) only encountered as a bug on Windows was not caught by the io.js CI system due to problems with the Windows build script and the Windows CI configuration, see Known Issues below. Fixed in [#994](https://github.com/nodejs/node/pull/994) & [#1004](https://github.com/nodejs/node/pull/1004). (Fedor Indutny)
* **npm**: Upgrade npm to 2.6.1. See [npm CHANGELOG.md](https://github.com/npm/npm/blob/master/CHANGELOG.md#v260-2015-02-12) for details. Summary:
  * [`8b98f0e`](https://github.com/npm/npm/commit/8b98f0e709d77a8616c944aebd48ab726f726f76)
    [#4471](https://github.com/npm/npm/issues/4471) `npm outdated` (and only `npm
    outdated`) now defaults to `--depth=0`. This also has the excellent but unexpected effect of making `npm update -g` work the way almost everyone wants it to. See the [docs for
    `--depth`](https://github.com/npm/npm/blob/82f484672adb1a3caf526a8a48832789495bb43d/doc/misc/npm-config.md#depth)
    for the mildly confusing details. ([@smikes](https://github.com/smikes))
  * [`aa79194`](https://github.com/npm/npm/commit/aa791942a9f3c8af6a650edec72a675deb7a7c6e)
    [#6565](https://github.com/npm/npm/issues/6565) Tweak `peerDependency`
    deprecation warning to include which peer dependency on which package is
    going to need to change. ([@othiym23](https://github.com/othiym23))
  * [`5fa067f`](https://github.com/npm/npm/commit/5fa067fd47682ac3cdb12a2b009d8ca59b05f992)
    [#7171](https://github.com/npm/npm/issues/7171) Tweak `engineStrict`
    deprecation warning to include which `package.json` is using it.
    ([@othiym23](https://github.com/othiym23))
* Add new collaborators:
  * Robert Kowalski ([@robertkowalski](https://github.com/robertkowalski))
  * Christian Vaagland Tellnes ([@tellnes](https://github.com/tellnes))
  * Brian White ([@mscdex](https://github.com/mscdex))

### Known issues

* Windows support has some outstanding failures that have not been properly picked up by the io.js CI system due to a combination of factors including human, program and Jenkins errors. See [#1005](https://github.com/nodejs/node/issues/1005) for details & discussion. Expect these problems to be addressed ASAP.
* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* Not possible to build io.js as a static library [#686](https://github.com/nodejs/node/issues/686)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)

### Commits

* \[[`25da0742ee`](https://github.com/nodejs/node/commit/25da0742ee)] - **build**: improve vcbuild.bat (Bert Belder) [#998](https://github.com/nodejs/node/pull/998)
* \[[`b8310cbd3e`](https://github.com/nodejs/node/commit/b8310cbd3e)] - **build**: reduce tarball size by 8-10% (Johan Bergström) [#961](https://github.com/nodejs/node/pull/961)
* \[[`58a612ea9d`](https://github.com/nodejs/node/commit/58a612ea9d)] - **deps**: make node-gyp work with io.js (cjihrig) [#990](https://github.com/nodejs/node/pull/990)
* \[[`2a2fe5c4f2`](https://github.com/nodejs/node/commit/2a2fe5c4f2)] - **deps**: upgrade npm to 2.6.1 (Forrest L Norvell) [#990](https://github.com/nodejs/node/pull/990)
* \[[`84ee2722a3`](https://github.com/nodejs/node/commit/84ee2722a3)] - **doc**: minor formatting fixes. (Tim Oxley) [#996](https://github.com/nodejs/node/pull/996)
* \[[`cf0306cd71`](https://github.com/nodejs/node/commit/cf0306cd71)] - **doc**: update stability index (Chris Dickinson) [#943](https://github.com/nodejs/node/pull/943)
* \[[`fb2439a699`](https://github.com/nodejs/node/commit/fb2439a699)] - **doc**: add robertkowalski as collaborator (Robert Kowalski) [#977](https://github.com/nodejs/node/pull/977)
* \[[`f83d380647`](https://github.com/nodejs/node/commit/f83d380647)] - **doc**: update os.markdown (Benjamin Gruenbaum) [#976](https://github.com/nodejs/node/pull/976)
* \[[`ae7a23351f`](https://github.com/nodejs/node/commit/ae7a23351f)] - **doc**: add roadmap, i18n, tracing, evangelism WGs (Mikeal Rogers) [#911](https://github.com/nodejs/node/pull/911)
* \[[`14174a95a5`](https://github.com/nodejs/node/commit/14174a95a5)] - **doc**: document roadmap, workgroups (Mikeal Rogers)
* \[[`865ee313cf`](https://github.com/nodejs/node/commit/865ee313cf)] - **doc**: add tellnes as collaborator (Christian Tellnes) [#973](https://github.com/nodejs/node/pull/973)
* \[[`01296923db`](https://github.com/nodejs/node/commit/01296923db)] - **doc**: add mscdex as collaborator (Brian White) [#972](https://github.com/nodejs/node/pull/972)
* \[[`675cffb33e`](https://github.com/nodejs/node/commit/675cffb33e)] - **http**: don't confuse automatic headers for others (Christian Tellnes) [#828](https://github.com/nodejs/node/pull/828)
* \[[`7887e119ed`](https://github.com/nodejs/node/commit/7887e119ed)] - **install**: new performance counters provider guid (Russell Dempsey)
* \[[`4d1fa2ca97`](https://github.com/nodejs/node/commit/4d1fa2ca97)] - **src**: add check for already defined macro NOMINMAX (Pavel Medvedev) [#986](https://github.com/nodejs/node/pull/986)
* \[[`1ab7e80838`](https://github.com/nodejs/node/commit/1ab7e80838)] - **tls**: proxy `handle.reading` back to parent handle (Fedor Indutny) [#1004](https://github.com/nodejs/node/pull/1004)
* \[[`755461219d`](https://github.com/nodejs/node/commit/755461219d)] - **tls**: fix typo `handle._reading` => `handle.reading` (Fedor Indutny) [#994](https://github.com/nodejs/node/pull/994)

<a id="1.4.1"></a>

## 2015-02-26, Version 1.4.1, @rvagg

_Note: version **1.4.0** was tagged and built but not released. A libuv bug was discovered in the process so the release was aborted. The tag was straight after [`a558cd0a61`](https://github.com/nodejs/node/commit/a558cd0a61) but has since been removed. We have jumped to 1.4.1 to avoid confusion._

### Notable changes

* **process** / **promises**: An `'unhandledRejection'` event is now emitted on `process` whenever a `Promise` is rejected and no error handler is attached to the `Promise` within a turn of the event loop. A `'rejectionHandled'` event is now emitted whenever a `Promise` was rejected and an error handler was attached to it later than after an event loop turn. See the [process](https://iojs.org/api/process.html) documentation for more detail. [#758](https://github.com/nodejs/node/pull/758) (Petka Antonov)
* **streams**: you can now use regular streams as an underlying socket for `tls.connect()` [#926](https://github.com/nodejs/node/pull/926) (Fedor Indutny)
* **http**: A new `'abort'` event emitted when a `http.ClientRequest` is aborted by the client. [#945](https://github.com/nodejs/node/pull/945) (Evan Lucas)
* **V8**: Upgrade V8 to 4.1.0.21. Includes an embargoed fix, details should be available at <https://code.google.com/p/chromium/issues/detail?id=430201> when embargo is lifted. A breaking ABI change has been held back from this upgrade, possibly to be included when io.js merges V8 4.2. See [#952](https://github.com/nodejs/node/pull/952) for discussion.
* **npm**: Upgrade npm to 2.6.0. Includes features to support the new registry and to prepare for `npm@3`. See [npm CHANGELOG.md](https://github.com/npm/npm/blob/master/CHANGELOG.md#v260-2015-02-12) for details. Summary:
  * [`38c4825`](https://github.com/npm/npm/commit/38c48254d3d217b4babf5027cb39492be4052fc2) [#5068](https://github.com/npm/npm/issues/5068) Add new logout command, and make it do something useful on both bearer-based and basic-based authed clients. ([@othiym23](https://github.com/othiym23))
  * [`c8e08e6`](https://github.com/npm/npm/commit/c8e08e6d91f4016c80f572aac5a2080df0f78098) [#6565](https://github.com/npm/npm/issues/6565) Warn that `peerDependency` behavior is changing and add a note to the docs. ([@othiym23](https://github.com/othiym23))
  * [`7c81a5f`](https://github.com/npm/npm/commit/7c81a5f5f058941f635a92f22641ea68e79b60db) [#7171](https://github.com/npm/npm/issues/7171) Warn that `engineStrict` in `package.json` will be going away in the next major version of npm (coming soon!) ([@othiym23](https://github.com/othiym23))
* **libuv**: Upgrade to 1.4.2. See [libuv ChangeLog](https://github.com/libuv/libuv/blob/v1.x/ChangeLog) for details of fixes.

### Known issues

* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* Not possible to build io.js as a static library [#686](https://github.com/nodejs/node/issues/686)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)

### Commits

* \[[`8a1e22af3a`](https://github.com/nodejs/node/commit/8a1e22af3a)] - **benchmark**: pass execArgv to the benchmarking process (Petka Antonov) [#928](https://github.com/nodejs/node/pull/928)
* \[[`234e6916b8`](https://github.com/nodejs/node/commit/234e6916b8)] - **build**: Fix incorrect reference (Johan Bergström) [#924](https://github.com/nodejs/node/pull/924)
* \[[`e00c938d24`](https://github.com/nodejs/node/commit/e00c938d24)] - **build**: make test-ci output TAP to stdout and log (Rod Vagg) [#938](https://github.com/nodejs/node/pull/938)
* \[[`b2a0d8f65e`](https://github.com/nodejs/node/commit/b2a0d8f65e)] - **deps**: update libuv to 1.4.2 (Ben Noordhuis) [#966](https://github.com/nodejs/node/pull/966)
* \[[`a558cd0a61`](https://github.com/nodejs/node/commit/a558cd0a61)] - **deps**: revert v8 abi change (Ben Noordhuis) [#952](https://github.com/nodejs/node/pull/952)
* \[[`54532a9761`](https://github.com/nodejs/node/commit/54532a9761)] - **deps**: fix postmortem support in v8 (Fedor Indutny) [#706](https://github.com/nodejs/node/pull/706)
* \[[`78f4837926`](https://github.com/nodejs/node/commit/78f4837926)] - **deps**: upgrade v8 to 4.1.0.21 (Ben Noordhuis) [#952](https://github.com/nodejs/node/pull/952)
* \[[`739fda16a9`](https://github.com/nodejs/node/commit/739fda16a9)] - **deps**: update libuv to 1.4.1 (Ben Noordhuis) [#940](https://github.com/nodejs/node/pull/940)
* \[[`da730c76e9`](https://github.com/nodejs/node/commit/da730c76e9)] - **deps**: enable node-gyp iojs.lib download checksum (Ben Noordhuis) [#918](https://github.com/nodejs/node/pull/918)
* \[[`97b424365a`](https://github.com/nodejs/node/commit/97b424365a)] - **deps**: make node-gyp work again on windows (Bert Belder)
* \[[`19e3d5e10a`](https://github.com/nodejs/node/commit/19e3d5e10a)] - **deps**: make node-gyp fetch tarballs from iojs.org (Ben Noordhuis) [#343](https://github.com/nodejs/node/pull/343)
* \[[`1e2fa1537f`](https://github.com/nodejs/node/commit/1e2fa1537f)] - **deps**: upgrade npm to 2.6.0 (Forrest L Norvell) [#904](https://github.com/nodejs/node/pull/904)
* \[[`2e2cf81476`](https://github.com/nodejs/node/commit/2e2cf81476)] - **doc**: fix process.stdout reference to console.log (Brendan Ashworth) [#964](https://github.com/nodejs/node/pull/964)
* \[[`2e63bad7eb`](https://github.com/nodejs/node/commit/2e63bad7eb)] - **doc**: link & formatting of SHAs in commit list (Tim Oxley) [#967](https://github.com/nodejs/node/pull/967)
* \[[`c5050d8e4d`](https://github.com/nodejs/node/commit/c5050d8e4d)] - **doc**: fix 'dhparam' description of tls.createServer (silverwind) [#968](https://github.com/nodejs/node/pull/968)
* \[[`06ee782f24`](https://github.com/nodejs/node/commit/06ee782f24)] - **doc**: document 'unhandledRejection' and 'rejectionHandled' (Benjamin Gruenbaum) [#946](https://github.com/nodejs/node/pull/946)
* \[[`b65dade102`](https://github.com/nodejs/node/commit/b65dade102)] - **doc**: update documentation.markdown for io.js. (Ryan Scheel) [#950](https://github.com/nodejs/node/pull/950)
* \[[`87e4bfd582`](https://github.com/nodejs/node/commit/87e4bfd582)] - **doc**: link cluster worker.send() to child.send() (Sam Roberts) [#839](https://github.com/nodejs/node/pull/839)
* \[[`cb22bc9b8a`](https://github.com/nodejs/node/commit/cb22bc9b8a)] - **doc**: fix footer sizing (Jeremiah Senkpiel) [#860](https://github.com/nodejs/node/pull/860)
* \[[`3ab9b92e90`](https://github.com/nodejs/node/commit/3ab9b92e90)] - **doc**: fix stream `_writev` header size (René Kooi) [#916](https://github.com/nodejs/node/pull/916)
* \[[`4fcbb8aaaf`](https://github.com/nodejs/node/commit/4fcbb8aaaf)] - **doc**: use HTTPS URL for the API documentation page (Shinnosuke Watanabe) [#913](https://github.com/nodejs/node/pull/913)
* \[[`329f364ea2`](https://github.com/nodejs/node/commit/329f364ea2)] - **doc**: fix PR reference in CHANGELOG (Brian White) [#903](https://github.com/nodejs/node/pull/903)
* \[[`0ac57317aa`](https://github.com/nodejs/node/commit/0ac57317aa)] - **doc**: fix typo, rephrase cipher change in CHANGELOG (Rod Vagg) [#902](https://github.com/nodejs/node/pull/902)
* \[[`1f40b2a636`](https://github.com/nodejs/node/commit/1f40b2a636)] - **fs**: add type checking to makeCallback() (cjihrig) [#866](https://github.com/nodejs/node/pull/866)
* \[[`c82e580a50`](https://github.com/nodejs/node/commit/c82e580a50)] - **fs**: properly handle fd passed to truncate() (Bruno Jouhier) [joyent/node#9161](https://github.com/joyent/node/pull/9161)
* \[[`2ca22aacbd`](https://github.com/nodejs/node/commit/2ca22aacbd)] - **(SEMVER-MINOR) http**: emit abort event from ClientRequest (Evan Lucas) [#945](https://github.com/nodejs/node/pull/945)
* \[[`d8eb974a98`](https://github.com/nodejs/node/commit/d8eb974a98)] - **net**: make Server.prototype.unref() persistent (cjihrig) [#897](https://github.com/nodejs/node/pull/897)
* \[[`872702d9b7`](https://github.com/nodejs/node/commit/872702d9b7)] - **(SEMVER-MINOR) node**: implement unhandled rejection tracking (Petka Antonov) [#758](https://github.com/nodejs/node/pull/758)
* \[[`b41dbc2737`](https://github.com/nodejs/node/commit/b41dbc2737)] - **readline**: use native `codePointAt` (Vladimir Kurchatkin) [#825](https://github.com/nodejs/node/pull/825)
* \[[`26ebe9805e`](https://github.com/nodejs/node/commit/26ebe9805e)] - **smalloc**: extend user API (Trevor Norris) [#905](https://github.com/nodejs/node/pull/905)
* \[[`e435a0114d`](https://github.com/nodejs/node/commit/e435a0114d)] - **src**: fix intermittent SIGSEGV in resolveTxt (Evan Lucas) [#960](https://github.com/nodejs/node/pull/960)
* \[[`0af4c9ea74`](https://github.com/nodejs/node/commit/0af4c9ea74)] - **src**: fix domains + --abort-on-uncaught-exception (Chris Dickinson) [#922](https://github.com/nodejs/node/pull/922)
* \[[`89e133a1d8`](https://github.com/nodejs/node/commit/89e133a1d8)] - **stream\_base**: remove static JSMethod declarations (Fedor Indutny) [#957](https://github.com/nodejs/node/pull/957)
* \[[`b9686233fc`](https://github.com/nodejs/node/commit/b9686233fc)] - **stream\_base**: introduce StreamBase (Fedor Indutny) [#840](https://github.com/nodejs/node/pull/840)
* \[[`1738c77835`](https://github.com/nodejs/node/commit/1738c77835)] - **(SEMVER-MINOR) streams**: introduce StreamWrap and JSStream (Fedor Indutny) [#926](https://github.com/nodejs/node/pull/926)
* \[[`506c7fd40b`](https://github.com/nodejs/node/commit/506c7fd40b)] - **test**: fix infinite spawn cycle in stdio test (Ben Noordhuis) [#948](https://github.com/nodejs/node/pull/948)
* \[[`a7bdce249c`](https://github.com/nodejs/node/commit/a7bdce249c)] - **test**: support writing test output to file (Johan Bergström) [#934](https://github.com/nodejs/node/pull/934)
* \[[`0df54303c1`](https://github.com/nodejs/node/commit/0df54303c1)] - **test**: common.js -> common (Brendan Ashworth) [#917](https://github.com/nodejs/node/pull/917)
* \[[`ed3b057e9f`](https://github.com/nodejs/node/commit/ed3b057e9f)] - **util**: handle symbols properly in format() (cjihrig) [#931](https://github.com/nodejs/node/pull/931)

<a id="1.3.0"></a>

## 2015-02-20, Version 1.3.0, @rvagg

### Notable changes

* **url**: `url.resolve('/path/to/file', '.')` now returns `/path/to/` with the trailing slash, `url.resolve('/', '.')` returns `/`. [#278](https://github.com/nodejs/node/issues/278) (Amir Saboury)
* **tls**: The default cipher suite used by `tls` and `https` has been changed to one that achieves Perfect Forward Secrecy with all modern browsers. Additionally, insecure RC4 ciphers have been excluded. If you absolutely require RC4, please specify your own cipher suites. [#826](https://github.com/nodejs/node/issues/826) (Roman Reiss)

### Known issues

* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* Not possible to build io.js as a static library [#686](https://github.com/nodejs/node/issues/686)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774)
* Calling `dns.setServers()` while a DNS query is in progress can cause the process to crash on a failed assertion [#894](https://github.com/nodejs/node/issues/894)

### Commits

* \[[`35ed79932c`](https://github.com/nodejs/node/commit/35ed79932c)] - **benchmark**: add a few querystring benchmarks (Brian White) [#846](https://github.com/nodejs/node/pull/846)
* \[[`c6fd2c5e95`](https://github.com/nodejs/node/commit/c6fd2c5e95)] - **buffer**: fix pool offset adjustment (Trevor Norris)
* \[[`36a779560a`](https://github.com/nodejs/node/commit/36a779560a)] - **buffer**: expose internals on binding (Vladimir Kurchatkin) [#770](https://github.com/nodejs/node/pull/770)
* \[[`e63b51793b`](https://github.com/nodejs/node/commit/e63b51793b)] - **crypto**: fix to check ext method for shared lib (Shigeki Ohtsu) [#800](https://github.com/nodejs/node/pull/800)
* \[[`afdef70fcc`](https://github.com/nodejs/node/commit/afdef70fcc)] - **doc**: update AUTHORS list (Rod Vagg) [#900](https://github.com/nodejs/node/pull/900)
* \[[`1bf91878e7`](https://github.com/nodejs/node/commit/1bf91878e7)] - **doc**: add TC meeting 2015-02-04 minutes (Rod Vagg) [#876](https://github.com/nodejs/node/pull/876)
* \[[`9e05c8d2fc`](https://github.com/nodejs/node/commit/9e05c8d2fc)] - **doc**: remove outdated language on consensus (Emily Rose)
* \[[`ed240f44f7`](https://github.com/nodejs/node/commit/ed240f44f7)] - **doc**: document 'ciphers' option of tls.connect (Roman Reiss) [#845](https://github.com/nodejs/node/pull/845)
* \[[`0555b3c785`](https://github.com/nodejs/node/commit/0555b3c785)] - **doc**: fix typo miliseconds -> milliseconds (jigsaw) [#865](https://github.com/nodejs/node/pull/865)
* \[[`fc6507dd4e`](https://github.com/nodejs/node/commit/fc6507dd4e)] - **doc**: add comma in README to increase clarity (Jimmy Hsu)
* \[[`f0296933f8`](https://github.com/nodejs/node/commit/f0296933f8)] - **doc**: correct `it's` to `its` in process (Charmander) [#837](https://github.com/nodejs/node/pull/837)
* \[[`e81731ad18`](https://github.com/nodejs/node/commit/e81731ad18)] - **doc**: add geek as collaborator (Wyatt Preul) [#835](https://github.com/nodejs/node/pull/835)
* \[[`4ca7cca84a`](https://github.com/nodejs/node/commit/4ca7cca84a)] - **doc**: grammar fix in smalloc (Debjeet Biswas) [joyent/node#9164](https://github.com/joyent/node/pull/9164)
* \[[`30dca66958`](https://github.com/nodejs/node/commit/30dca66958)] - **doc**: fix code syntax (Dan Dascalescu) [joyent/node#9198](https://github.com/joyent/node/pull/9198)
* \[[`8c1df7a8a8`](https://github.com/nodejs/node/commit/8c1df7a8a8)] - **doc**: use correct signature for assert() (Andrei Sedoi) [joyent/node#9003](https://github.com/joyent/node/pull/9003)
* \[[`ba40942ad2`](https://github.com/nodejs/node/commit/ba40942ad2)] - **doc**: fix sentence grammar timers.markdown (Omer Wazir) [#815](https://github.com/nodejs/node/pull/815)
* \[[`789ff959be`](https://github.com/nodejs/node/commit/789ff959be)] - **doc**: increase mark class contrast ratio (Omer Wazir) [#824](https://github.com/nodejs/node/pull/824)
* \[[`122a1758d1`](https://github.com/nodejs/node/commit/122a1758d1)] - **doc**: better font-smoothing for firefox (Jeremiah Senkpiel) [#820](https://github.com/nodejs/node/pull/820)
* \[[`982b143ab3`](https://github.com/nodejs/node/commit/982b143ab3)] - **doc**: disable font ligatures (Roman Reiss) [#816](https://github.com/nodejs/node/pull/816)
* \[[`cb5560bd62`](https://github.com/nodejs/node/commit/cb5560bd62)] - **doc**: Close code span correctly (Omer Wazir) [#814](https://github.com/nodejs/node/pull/814)
* \[[`c3c2fbdf83`](https://github.com/nodejs/node/commit/c3c2fbdf83)] - **doc**: change effect to affect in errors.md (Ryan Seys) [#799](https://github.com/nodejs/node/pull/799)
* \[[`b620129715`](https://github.com/nodejs/node/commit/b620129715)] - **doc**: add sam-github as collaborator (Sam Roberts) [#791](https://github.com/nodejs/node/pull/791)
* \[[`e80f803298`](https://github.com/nodejs/node/commit/e80f803298)] - **doc**: remove Caine section from contributing guide (Michaël Zasso) [#804](https://github.com/nodejs/node/pull/804)
* \[[`400d6e56f9`](https://github.com/nodejs/node/commit/400d6e56f9)] - **doc**: fix libuv link (Yosuke Furukawa) [#803](https://github.com/nodejs/node/pull/803)
* \[[`15d156e3ec`](https://github.com/nodejs/node/commit/15d156e3ec)] - **doc**: fix wording in fs.appendFile (Rudolf Meijering) [#801](https://github.com/nodejs/node/pull/801)
* \[[`dbf75924f1`](https://github.com/nodejs/node/commit/dbf75924f1)] - **doc**: update error links (Chris Dickinson) [#793](https://github.com/nodejs/node/pull/793)
* \[[`7061669dba`](https://github.com/nodejs/node/commit/7061669dba)] - **events**: optimize adding and removing of listeners (Brian White) [#785](https://github.com/nodejs/node/pull/785)
* \[[`630f636334`](https://github.com/nodejs/node/commit/630f636334)] - **events**: move slow path to separate function too (Brian White) [#785](https://github.com/nodejs/node/pull/785)
* \[[`ecef87177a`](https://github.com/nodejs/node/commit/ecef87177a)] - **fs**: ensure nullCheck() callback is a function (cjihrig) [#887](https://github.com/nodejs/node/pull/887)
* \[[`6a2b204bbc`](https://github.com/nodejs/node/commit/6a2b204bbc)] - **module**: replace NativeModule.require (Herbert Vojčík) [joyent/node#9201](https://github.com/joyent/node/pull/9201)
* \[[`9b6b05556f`](https://github.com/nodejs/node/commit/9b6b05556f)] - **net**: unref timer in parent sockets (Fedor Indutny) [#891](https://github.com/nodejs/node/pull/891)
* \[[`cca8de6709`](https://github.com/nodejs/node/commit/cca8de6709)] - **net**: remove use of arguments in Server constructor (cjihrig)
* \[[`0cff0521c3`](https://github.com/nodejs/node/commit/0cff0521c3)] - **net**: throw on invalid socket timeouts (cjihrig) [joyent/node#8884](https://github.com/joyent/node/pull/8884)
* \[[`b5f25a963c`](https://github.com/nodejs/node/commit/b5f25a963c)] - **src**: ensure that file descriptors 0-2 are valid (Ben Noordhuis) [#875](https://github.com/nodejs/node/pull/875)
* \[[`a956791f69`](https://github.com/nodejs/node/commit/a956791f69)] - **src**: fix typo in error message (Ben Noordhuis) [#875](https://github.com/nodejs/node/pull/875)
* \[[`fb28c91074`](https://github.com/nodejs/node/commit/fb28c91074)] - **src**: fix add-on builds, partially revert 8aed9d66 (Ben Noordhuis) [#868](https://github.com/nodejs/node/pull/868)
* \[[`4bb3184d8d`](https://github.com/nodejs/node/commit/4bb3184d8d)] - **src**: reduce AsyncWrap memory footprint (Ben Noordhuis) [#667](https://github.com/nodejs/node/pull/667)
* \[[`7e779b4593`](https://github.com/nodejs/node/commit/7e779b4593)] - **src**: remove obsoleted queue.h header (Ben Noordhuis) [#667](https://github.com/nodejs/node/pull/667)
* \[[`38dc0cd8f4`](https://github.com/nodejs/node/commit/38dc0cd8f4)] - **src**: switch from QUEUE to intrusive list (Ben Noordhuis) [#667](https://github.com/nodejs/node/pull/667)
* \[[`58eb00c693`](https://github.com/nodejs/node/commit/58eb00c693)] - **src**: add typesafe intrusive list (Ben Noordhuis) [#667](https://github.com/nodejs/node/pull/667)
* \[[`8aed9d6610`](https://github.com/nodejs/node/commit/8aed9d6610)] - **src**: cleanup `Isolate::GetCurrent()` (Vladimir Kurchatkin) [#807](https://github.com/nodejs/node/pull/807)
* \[[`7c22372303`](https://github.com/nodejs/node/commit/7c22372303)] - **src**: remove trailing whitespace (Vladimir Kurchatkin) [#798](https://github.com/nodejs/node/pull/798)
* \[[`20f8e7f17a`](https://github.com/nodejs/node/commit/20f8e7f17a)] - **test**: remove flaky test functionality (Rod Vagg) [#812](https://github.com/nodejs/node/pull/812)
* \[[`30e340ad9d`](https://github.com/nodejs/node/commit/30e340ad9d)] - **test**: fix parallel/test-tls-getcipher (Roman Reiss) [#853](https://github.com/nodejs/node/pull/853)
* \[[`d53b636d94`](https://github.com/nodejs/node/commit/d53b636d94)] - **test**: verify fields in spawn{Sync} errors (cjihrig) [#838](https://github.com/nodejs/node/pull/838)
* \[[`3b1b4de903`](https://github.com/nodejs/node/commit/3b1b4de903)] - **test**: Timeout#unref() does not return instance (Jan Schär) [joyent/node#9171](https://github.com/joyent/node/pull/9171)
* \[[`becb4e980e`](https://github.com/nodejs/node/commit/becb4e980e)] - **test**: distribute crypto tests into separate files (Brendan Ashworth) [#827](https://github.com/nodejs/node/pull/827)
* \[[`77f35861d0`](https://github.com/nodejs/node/commit/77f35861d0)] - **(SEMVER-MINOR) tls**: more secure defaults (Roman Reiss) [#826](https://github.com/nodejs/node/pull/826)
* \[[`faa687b4be`](https://github.com/nodejs/node/commit/faa687b4be)] - **url**: reslove urls with . and .. (Amir Saboury) [#278](https://github.com/nodejs/node/pull/278)

<a id="1.2.0"></a>

## 2015-02-10, Version 1.2.0, @rvagg

### Notable changes

* **stream**:
  * Simpler stream construction, see [readable-stream/issues#102](https://github.com/nodejs/readable-stream/issues/102) for details. This extends the streams base objects to make their constructors accept default implementation methods, reducing the boilerplate required to implement custom streams. An updated version of readable-stream will eventually be released to match this change in core. (@sonewman)
* **dns**:
  * `lookup()` now supports an `'all'` boolean option, default to `false` but when turned on will cause the method to return an array of _all_ resolved names for an address, see, [#744](https://github.com/nodejs/node/pull/744) (@silverwind)
* **assert**:
  * Remove `prototype` property comparison in `deepEqual()`, considered a bugfix, see [#636](https://github.com/nodejs/node/pull/636) (@vkurchatkin)
  * Introduce a `deepStrictEqual()` method to mirror `deepEqual()` but performs strict equality checks on primitives, see [#639](https://github.com/nodejs/node/pull/639) (@vkurchatkin)
* **tracing**:
  * Add [LTTng](http://lttng.org/) (Linux Trace Toolkit Next Generation) when compiled with the  `--with-lttng` option. Trace points match those available for DTrace and ETW. [#702](https://github.com/nodejs/node/pull/702) (@thekemkid)
* **docs**:
  * Lots of doc updates, see individual commits
  * New **Errors** page discussing JavaScript errors, V8 specifics, and io.js specific error details. (@chrisdickinson)
* **npm** upgrade to 2.5.1, short changelog:
  * [npm/0e8d473](https://github.com/npm/npm/commit/0e8d4736a1cbdda41ae8eba8a02c7ff7ce80c2ff) [#7281](https://github.com/npm/npm/issues/7281) `npm-registry-mock@1.0.0`: Clean up API, set `connection: close`, which makes tests pass on io.js 1.1.x.
    ([@robertkowalski](https://github.com/robertkowalski))
  * [npm/f9313a0](https://github.com/npm/npm/commit/f9313a066c9889a0ee898d8a35676e40b8101e7f)
    [#7226](https://github.com/npm/npm/issues/7226) Ensure that all request
    settings are copied onto the agent.
    ([@othiym23](https://github.com/othiym23))
  * [npm/fec4c96](https://github.com/npm/npm/commit/fec4c967ee235030bf31393e8605e9e2811f4a39)
    Allow `--no-proxy` to override `HTTP_PROXY` setting in environment.
    ([@othiym23](https://github.com/othiym23))
  * [npm/9d61e96](https://github.com/npm/npm/commit/9d61e96fb1f48687a85c211e4e0cd44c7f95a38e)
    `npm outdated --long` now includes a column showing the type of dependency.
    ([@watilde](https://github.com/watilde))
* **libuv** upgrade to 1.4.0, see [libuv ChangeLog](https://github.com/libuv/libuv/blob/v1.x/ChangeLog)
* Add new collaborators:
  * Aleksey Smolenchuk (@lxe)
  * Shigeki Ohtsu (@shigeki)

### Known issues

* Surrogate pair in REPL can freeze terminal [#690](https://github.com/nodejs/node/issues/690)
* Not possible to build io.js as a static library [#686](https://github.com/nodejs/node/issues/686)
* `process.send()` is not synchronous as the docs suggest, a regression introduced in 1.0.2, see [#760](https://github.com/nodejs/node/issues/760) and fix in [#774](https://github.com/nodejs/node/issues/774) that should appear in the next patch release.

### Commits

* \[[`7e2235a`](https://github.com/nodejs/node/commit/7e2235aebb067b84974e001f9fa4d83f45b7c9dd)] - doc: add error documentation (Chris Dickinson)
* \[[`d832be4`](https://github.com/nodejs/node/commit/d832be4aa3c68c29017339a65593f62cb73179bc)] - doc: update AUTHORS list (Rod Vagg)
* \[[`aea9b89`](https://github.com/nodejs/node/commit/aea9b89b5c2e3fb9fdbd96c7483eb1f60d09a39e)] - doc: add shigeki as collaborator (Shigeki Ohtsu)
* \[[`e653080`](https://github.com/nodejs/node/commit/e65308053c871352be948b9001737df01aad1965)] - fs: improve `readFile` performance (Vladimir Kurchatkin)
* \[[`9681fca`](https://github.com/nodejs/node/commit/9681fcacf0fd477f999a52f6ff4151d4125d49d0)] - deps: update libuv to 1.4.0 (Saúl Ibarra Corretgé)
* \[[`5e825d1`](https://github.com/nodejs/node/commit/5e825d1073b57a87fc9a77751ed3e21c86970082)] - tracing: add lttng support for tracing on linux (Glen Keane)
* \[[`b677b84`](https://github.com/nodejs/node/commit/b677b844fc1de328a0f2b0151bdfc045cb5d0c81)] - events: optimize various functions (Brian White)
* \[[`c86e383`](https://github.com/nodejs/node/commit/c86e383c41f35b17ba79cc1c6dbfff674214177d)] - test: fix test failure with shared openssl (Shigeki Ohtsu)
* \[[`1151016`](https://github.com/nodejs/node/commit/1151016d0a13dcb5973f602d0717c2da6abca551)] - doc: fix typo in crypto (Haoliang Gao)
* \[[`7c56868`](https://github.com/nodejs/node/commit/7c568684b834a6a3c4d15bb29d2f95cf76773cb8)] - doc: change the order of crypto.publicDecrypt (Haoliang Gao)
* \[[`3f473ef`](https://github.com/nodejs/node/commit/3f473ef141fdc7059928ebc4542b00e2f126ab07)] - assert: introduce `deepStrictEqual` (Vladimir Kurchatkin)
* \[[`828d19a`](https://github.com/nodejs/node/commit/828d19a1f696840acf43b70125b85b0d61ff5056)] - doc: fix dns.lookup options example (Roman Reiss)
* \[[`90d2b35`](https://github.com/nodejs/node/commit/90d2b352810bc352620e61e0dacc8573faf11dfb)] - doc: update antiquated process.versions output (Ben Noordhuis)
* \[[`789bbb9`](https://github.com/nodejs/node/commit/789bbb91d3eb30fa2a51e9b064592d6a461a6fe5)] - doc: update node.js references in api docs (Ben Noordhuis)
* \[[`c22e5ac`](https://github.com/nodejs/node/commit/c22e5ace846f93b4531a39b0e055f89a46598f63)] - https: simpler argument check (Michaël Zasso)
* \[[`b9d3928`](https://github.com/nodejs/node/commit/b9d3928f80992a812795a974cbae02288fc5049c)] - util: simplify `isPrimitive` (Vladimir Kurchatkin)
* \[[`2c3121c`](https://github.com/nodejs/node/commit/2c3121c606967f8595d671601493e623a7157385)] - benchmark: bump eventemitter number of iterations (Ben Noordhuis)
* \[[`633a990`](https://github.com/nodejs/node/commit/633a9908487efadda6a86026a36d5325a28805c6)] - dns: allow dns.lookup() to return all addresses (Roman Reiss)
* \[[`1cd1d7a`](https://github.com/nodejs/node/commit/1cd1d7a182c2d16c28c778ddcd72bbeac6bc5c75)] - buffer: don't compare same buffers (Vladimir Kurchatkin)
* \[[`847b9d2`](https://github.com/nodejs/node/commit/847b9d212a404e5906ea9f366c458332c0318c53)] - benchmark: add more EventEmitter benchmarks (Brian White)
* \[[`96597bc`](https://github.com/nodejs/node/commit/96597bc5927c57737c3bea943dd163d69ac76a96)] - doc: add lxe as collaborator (Aleksey Smolenchuk)
* \[[`7a301e2`](https://github.com/nodejs/node/commit/7a301e29de1e4ab5f39165beb6d0b41435c221dd)] - deps: make node-gyp work again on windows (Bert Belder)
* \[[`b188a34`](https://github.com/nodejs/node/commit/b188a3459d9d8a6d0c5fd391f1aefba281407083)] - deps: make node-gyp fetch tarballs from iojs.org (Ben Noordhuis)
* \[[`af1bf49`](https://github.com/nodejs/node/commit/af1bf49852b7a8bcc9b9b6dd718edea0b18e3cb6)] - deps: upgrade npm to 2.5.1 (Forrest L Norvell)
* \[[`9dc9ec3`](https://github.com/nodejs/node/commit/9dc9ec3ce6ba6f3dd4020e00f5863e207fa08a75)] - lib: make debug client connect to 127.0.0.1 (Ben Noordhuis)
* \[[`e7573f9`](https://github.com/nodejs/node/commit/e7573f9111f6b85c599ec225714d76e08ec8a4dc)] - assert: don't compare object `prototype` property (Vladimir Kurchatkin)
* \[[`8d11799`](https://github.com/nodejs/node/commit/8d1179952aefaa0086ff5540671cfd6ff612594b)] - asyncwrap: fix nullptr parent check (Trevor Norris)
* \[[`62512bb`](https://github.com/nodejs/node/commit/62512bb29cd000dd5ce848258c10f3211f153bd5)] - test: accept EPROTONOSUPPORT ipv6 error (Ben Noordhuis)
* \[[`05f4dff`](https://github.com/nodejs/node/commit/05f4dff97519ada5d3149a16ca9e5a04df948a61)] - asyncwrap: fix constructor condition for early ret (Trevor Norris)
* \[[`10277d2`](https://github.com/nodejs/node/commit/10277d2e57ee7fe9e0e3f63f10b9ea521e86e7f0)] - docs: include mention of new crypto methods (Calvin Metcalf)
* \[[`9a8f186`](https://github.com/nodejs/node/commit/9a8f18613da4956c963377e2ad55cdd3dabc32aa)] - child\_process: add debug and error details (Zach Bruggeman)
* \[[`6f7a978`](https://github.com/nodejs/node/commit/6f7a9784eaef82a1aa6cf53bbbd7224c446876a0)] - crypto: clear error on return in TLS methods (Fedor Indutny)
* \[[`50daee7`](https://github.com/nodejs/node/commit/50daee7243a3f987e1a28d93c43f913471d6885a)] - stream: simpler stream construction (Sam Newman)
* \[[`e0730ee`](https://github.com/nodejs/node/commit/e0730eeaa5231841a7eba080c8170e41278c3c52)] - benchmark: allow compare via fine-grained filters (Brian White)
* \[[`96ffcb9`](https://github.com/nodejs/node/commit/96ffcb9a210a2fa1248ae5931290193573512a96)] - src: reduce cpu profiler overhead (Ben Noordhuis)
* \[[`3e675e4`](https://github.com/nodejs/node/commit/3e675e44b59f1be8e5581de000f3cb17ef747c14)] - benchmark: don't use template strings (Evan Lucas)
* \[[`8ac8b76`](https://github.com/nodejs/node/commit/8ac8b760ac74e5a6938a49e563406716804672cb)] - doc: simplified pure consensus seeking (Mikeal Rogers)
* \[[`0a54b6a`](https://github.com/nodejs/node/commit/0a54b6a134a6815e30d1f78f8c8612d4a00399ad)] - doc: update streams wg charter (Chris Dickinson)
* \[[`b8ead4a`](https://github.com/nodejs/node/commit/b8ead4a23f8b0717204878235d61cfce3f3fdd30)] - Adjusting for feedback in the PR. (Mikeal Rogers)
* \[[`3af7f30`](https://github.com/nodejs/node/commit/3af7f30a7cceb1e418e5cd26c65a8ec5cc589d09)] - Initial documentation for working groups. (Mikeal Rogers)
* \[[`513724e`](https://github.com/nodejs/node/commit/513724efcc42ed150391915050fe60402f8dd48d)] - doc: add GPG fingerprint for chrisdickinson (Chris Dickinson)
* \[[`4168198`](https://github.com/nodejs/node/commit/41681983921d323da79b6d45e4ae0f8edb541e18)] - doc: add TC meeting 2015-01-28 minutes (Rod Vagg)

<a id="1.1.0"></a>

## 2015-02-03, Version 1.1.0, @chrisdickinson

### Notable changes

* debug: fix v8 post-mortem debugging.
* crypto: publicEncrypt now supports password-protected private keys.
* crypto: \~30% speedup on hashing functions.
* crypto: added privateEncrypt/publicDecrypt functions.
* errors
  * better formatting via util.inspect
  * more descriptive errors from fs. This necessitated a `NODE_MODULE_VERSION` bump.
  * more descriptive errors from http.setHeader
* dep updates:
  * npm: upgrade to 2.4.1
  * http-parser: rollback to 2.3.0
  * libuv: update to 1.3.0
  * v8: update to 4.1.0.14
* http.request: inherited properties on options are now respected
* add iterable interface to buffers (`for (let byte of buffer.values()) { }`)
* fs: fix fd leak on `fs.createReadStream`. See 497fd72 for details.
* installer: on Windows, emit WM\_SETTINGCHANGE after install to make other running
  processes aware of the PATH changes.
* Added new collaborators:
  * Vladimir Kurchatkin (@vkurchatkin)
  * Micleușanu Nicu (@micnic)

### Known issues

* Surrogate pair in REPL can freeze terminal (<https://github.com/nodejs/node/issues/690>)
* Not possible to build io.js as a static library (<https://github.com/nodejs/node/issues/686>)

### Commits

* \[[`df48faf`](https://github.com/nodejs/node/commit/df48fafa92c4ff0caee54c2f7fa214073cbd787e)] - tools: add release tool and docs, remove old tools (Rod Vagg)
* \[[`14684d3`](https://github.com/nodejs/node/commit/14684d3d67ad7c04bec7b63377343dab3e389470)] - v8abbr: ASCIISTRINGTAG => ONEBYTESTRINGTAG (Fedor Indutny)
* \[[`6a5d731`](https://github.com/nodejs/node/commit/6a5d731f602b547074f4367a7eb3964395080c94)] - gyp: enable postmortem support, fix dtrace paths (Fedor Indutny)
* \[[`8b88ff8`](https://github.com/nodejs/node/commit/8b88ff85f106eed03bf677b9ab3b842f4edbdc6b)] - deps: fix postmortem support in v8 (Fedor Indutny)
* \[[`d0b0bb4`](https://github.com/nodejs/node/commit/d0b0bb4ae00f596042bebe1ae61ae685bfbebf7d)] - dtrace: fix removal of unused probes (Glen Keane)
* \[[`3e67d7e`](https://github.com/nodejs/node/commit/3e67d7e46b80c90faa360d1d0e44dacc444e8e4f)] - http: replace util.\_extend() with \[].slice() (Jonathan Ong)
* \[[`89dd8e0`](https://github.com/nodejs/node/commit/89dd8e062f462106a6f7d3e92e9d18906445f851)] - benchmark: clean up common.js (Brendan Ashworth)
* \[[`6561274`](https://github.com/nodejs/node/commit/6561274d2377d9fd9c55fa3ce2eb2e53c14d898e)] - crypto: support passwords in publicEncrypt (Calvin Metcalf)
* \[[`e9eb2ec`](https://github.com/nodejs/node/commit/e9eb2ec1c491e82dda27fe07d0eaf14ff569351b)] - process: fix regression in unlistening signals (Sam Roberts)
* \[[`233e333`](https://github.com/nodejs/node/commit/233e333b183edeea6b740911061c7dc526078260)] - events: remove indeterminancy from event ordering (Sam Roberts)
* \[[`d75fecf`](https://github.com/nodejs/node/commit/d75fecf6fd7a1ef9d3d84a70ab832e7c062f5880)] - src: remove unused dtrace probes (Glen Keane)
* \[[`8c0742f`](https://github.com/nodejs/node/commit/8c0742f43759d35da99f2475f81a026c2818c66a)] - net: check close callback is function (Yosuke Furukawa)
* \[[`207e48c`](https://github.com/nodejs/node/commit/207e48c93459da5e47f2efd408cfad6328bb0e25)] - dgram: check close callback is function (Yosuke Furukawa)
* \[[`6ac8bdc`](https://github.com/nodejs/node/commit/6ac8bdc0aba5f60f4b4f2da5abd36d664062aa40)] - lib: reduce util.is\*() usage (cjihrig)
* \[[`bce7a26`](https://github.com/nodejs/node/commit/bce7a2608eb198eee6ecd7991062efd6daeeb440)] - deps: make node-gyp work again on windows (Bert Belder)
* \[[`1bdd74d`](https://github.com/nodejs/node/commit/1bdd74d20a3c979d51929a1039824d90abca2cdb)] - deps: make node-gyp fetch tarballs from iojs.org (Ben Noordhuis)
* \[[`faf34ff`](https://github.com/nodejs/node/commit/faf34ffbd321f4657bd99fb82931e1c9a4dda6af)] - deps: upgrade npm to 2.4.1 (Forrest L Norvell)
* \[[`40e29dc`](https://github.com/nodejs/node/commit/40e29dcbbf33d919f5cc0cbab5fa65a282adb04b)] - assert: use util.inspect() to create error messages (cjihrig)
* \[[`bc2c85c`](https://github.com/nodejs/node/commit/bc2c85ceef7ac034830e4a4357d0aef69cd6e386)] - fs: improve error messages (Bert Belder)
* \[[`0767c2f`](https://github.com/nodejs/node/commit/0767c2feb1cb6921acd18be3392d331e093b2b4c)] - lib: fix max size check in Buffer constructor (Ben Noordhuis)
* \[[`65b1e4f`](https://github.com/nodejs/node/commit/65b1e4f56f1f49dccd19b65dee2856df05b06c89)] - dgram: implicit binds should be exclusive (Sam Roberts)
* \[[`083c421`](https://github.com/nodejs/node/commit/083c421b5ca08576897b5da396085a462010780e)] - benchmark: remove extra spacing in http options (Brendan Ashworth)
* \[[`e17e6fb`](https://github.com/nodejs/node/commit/e17e6fb2faa04193eddf8062fbd49f1612b4dbff)] - util: use on-stack buffer for Utf8Value (Fedor Indutny)
* \[[`3d4e96f`](https://github.com/nodejs/node/commit/3d4e96f3ceea1d30b4affb66133016a3c2811005)] - crypto: use on-stack storage in HashUpdate (Fedor Indutny)
* \[[`aca2011`](https://github.com/nodejs/node/commit/aca20112519decef44474a2ee9936049e2a38b67)] - string\_bytes: introduce InlineDecoder (Fedor Indutny)
* \[[`c6367e7`](https://github.com/nodejs/node/commit/c6367e7f2a68b2418a98dfe9e829f17f62ba403a)] - node: speed up ParseEncoding (Fedor Indutny)
* \[[`7604e6d`](https://github.com/nodejs/node/commit/7604e6decc441a1110567e98f20f7ee122179d54)] - docs: add note about default padding in crypto (Calvin Metcalf)
* \[[`cf3e908`](https://github.com/nodejs/node/commit/cf3e908b70dfb345711cbca6c8e5373d085b05ea)] - http: more descriptive setHeader errors (Qasim Zaidi)
* \[[`cbc1262`](https://github.com/nodejs/node/commit/cbc1262bd952a6c52937abe47a0af625965fba65)] - deps: upgrade v8 to 4.1.0.14 (Ben Noordhuis)
* \[[`00f822f`](https://github.com/nodejs/node/commit/00f822f276c08465db3f6c70f154e9f28cc372d6)] - doc: add micnic as collaborator (Micleusanu Nicu)
* \[[`514b1d9`](https://github.com/nodejs/node/commit/514b1d964b2e67d0594c6a44a22fbc29fe71454b)] - doc: add more info to benchmark/README.md (Fishrock123)
* \[[`097fde7`](https://github.com/nodejs/node/commit/097fde7129a3acc660beb372cecd9daf1164a7f2)] - deps: update libuv to 1.3.0 (Saúl Ibarra Corretgé)
* \[[`6ad236c`](https://github.com/nodejs/node/commit/6ad236c9b6a344a88ec2f1f173d5f920984b77b7)] - build: configure formatting, add final message (Roman Reiss)
* \[[`dd47a8c`](https://github.com/nodejs/node/commit/dd47a8c78547db14ea0c7fc2f3375e8c9cb1a129)] - src: set default signal dispositions at start-up (Ben Noordhuis)
* \[[`63ae1d2`](https://github.com/nodejs/node/commit/63ae1d203aba94b9a35400acdf00ff968fb6eb05)] - src: rework early debug signal handling (Ben Noordhuis)
* \[[`5756f92`](https://github.com/nodejs/node/commit/5756f92f464fd0f2d04dd05bc30b350010885f74)] - src: do platform-specific initialization earlier (Ben Noordhuis)
* \[[`24bd4e0`](https://github.com/nodejs/node/commit/24bd4e055562d8eb8a0d8db907c1715cc37e90b4)] - test: add http upgrade header regression test (Ben Noordhuis)
* \[[`6605096`](https://github.com/nodejs/node/commit/660509694cfd4de59df0548eabbe18c97d75c63a)] - deps: roll back http\_parser to 2.3.0 (Ben Noordhuis)
* \[[`90ddb46`](https://github.com/nodejs/node/commit/90ddb46d522c37d2bc2eb68a6e0c9d52f9fbba42)] - crypto: remove use of this.\_readableState (Calvin Metcalf)
* \[[`45d8d9f`](https://github.com/nodejs/node/commit/45d8d9f8262983d7d6434f4500b4e88b63052cd5)] - buffer: implement `iterable` interface (Vladimir Kurchatkin)
* \[[`3cbb5cd`](https://github.com/nodejs/node/commit/3cbb5cdfdb621baec5dc3a2ac505be37f1718086)] - console: allow Object.prototype fields as labels (cjihrig)
* \[[`87e62bd`](https://github.com/nodejs/node/commit/87e62bd4c87e8674e3d1c432506e9b4991784ee2)] - crypto: implement privateEncrypt/publicDecrypt (Fedor Indutny)
* \[[`b50fea4`](https://github.com/nodejs/node/commit/b50fea4d490278b291321e6b96c49cf20bee1552)] - watchdog: fix timeout for early polling return (Saúl Ibarra Corretgé)
* \[[`b5166cb`](https://github.com/nodejs/node/commit/b5166cb7d269cd1bf90d1768f82767b05b9ac1f8)] - benchmark: add bench-(url & events) make targets (Yosuke Furukawa)
* \[[`5843ae8`](https://github.com/nodejs/node/commit/5843ae8dfba5db83f2c04ed2db847049cbd2ab0d)] - Revert "doc: clarify fs.symlink and fs.symlinkSync parameters" (Bert Belder)
* \[[`668bde8`](https://github.com/nodejs/node/commit/668bde8ac0d16382cbc98c904d8b5f55fd9fd9f0)] - win,msi: broadcast WM\_SETTINGCHANGE after install (Mathias Küsel)
* \[[`69ce064`](https://github.com/nodejs/node/commit/69ce0641dc6a84c90ffdd0906790cd945f1c3629)] - build: remove artifacts on distclean (Johan Bergström)
* \[[`1953886`](https://github.com/nodejs/node/commit/1953886126a2ab3e7291a73767ee4302a391a208)] - test: fs.createReadStream().destroy() fd leak (Rod Vagg)
* \[[`497fd72`](https://github.com/nodejs/node/commit/497fd72e21d2d1216e8457928d1a8082349fd0e5)] - fs: fix fd leak in ReadStream.destroy() (Alex Kocharin)
* \[[`8b09ae7`](https://github.com/nodejs/node/commit/8b09ae76f1d854a0db579fc0737df4809ce6087d)] - doc: add links for http\_parser/libuv upgrades (Michael Hart)
* \[[`683e096`](https://github.com/nodejs/node/commit/683e09603e3418ed13333bac05876cb7d52453f5)] - src: remove excessive license boilerplate (Aleksey Smolenchuk)
* \[[`5c7ab96`](https://github.com/nodejs/node/commit/5c7ab96b90d1ab35e03e32a249d50e7651dee6ef)] - doc: fix net.Server.listen bind behavior (Andres Suarez)
* \[[`84b05d4`](https://github.com/nodejs/node/commit/84b05d48d943e5b5e88485be129755277bedd1cb)] - doc: update writable streams default encoding (Johnny Ray Austin)
* \[[`1855267`](https://github.com/nodejs/node/commit/18552677d7e4468b093f28e721d1c02ce001b558)] - doc: fix minor grammar mistake in streams docs (ttrfwork)
* \[[`4f68369`](https://github.com/nodejs/node/commit/4f68369643cbbbcc6b12028091bb8064e89ce02d)] - build: disable v8 snapshots (Ben Noordhuis)
* \[[`c0a9d1b`](https://github.com/nodejs/node/commit/c0a9d1bc74e1aa5ed1f5a934509c1984142e0eab)] - versions: add http-parser patchlevel (Johan Bergström)
* \[[`7854811`](https://github.com/nodejs/node/commit/785481149d59fddead9007d469e2578204f24cfb)] - child\_process: clone spawn options argument (cjihrig)
* \[[`88aaff9`](https://github.com/nodejs/node/commit/88aaff9aa6dd2aa2baadaf9b8d5f08e89fb77402)] - deps: update http\_parser to 2.4.2 (Fedor Indutny)
* \[[`804ab7e`](https://github.com/nodejs/node/commit/804ab7ebaaf5d87499e3cbce03184f064264dd2a)] - doc: add seishun as a collaborator (Nikolai Vavilov)
* \[[`301a968`](https://github.com/nodejs/node/commit/301a968a40152c1ad3562482b4044458a13ebc4f)] - child\_process: remove redundant condition (Vladimir Kurchatkin)
* \[[`06cfff9`](https://github.com/nodejs/node/commit/06cfff935012ed2826cac56284cea982630cbc27)] - http: don't bother making a copy of the options (Jonathan Ong)
* \[[`55c222c`](https://github.com/nodejs/node/commit/55c222ceba8e2b22fb5639082906faace520ec4e)] - doc: add vkurchatkin as collaborator (Vladimir Kurchatkin)
* \[[`50ac4b7`](https://github.com/nodejs/node/commit/50ac4b7e2a823f92f0e102b804ec73f00eacb216)] - Working on 1.0.5 (Rod Vagg)
* \[[`d1fc9c6`](https://github.com/nodejs/node/commit/d1fc9c6caec68883401fe601d99f3a69fee52556)] - 2015-01-24 io.js v1.0.4 Release (Rod Vagg)

<a id="1.0.4"></a>

## 2015-01-24, Version 1.0.4, @rvagg

### Notable changes

* npm upgrade to 2.3.0 fixes Windows "uid is undefined" errors
* crypto.pseudoRandomBytes() is now an alias for crypto.randomBytes()
  and will block if there is insufficient entropy to produce secure
  values. See <https://github.com/nodejs/node/commit/e5e5980> for details.
* Patch for V8 to properly detect ARMv6; binaries now work again on
  ARMv6 (Raspberry Pi etc.)
* Minor V8 upgrade from 4.1.0.7 to 4.1.0.12
* 'punycode' core module bumped from stability level 2-Unstable,
  to 3-Stable
* Added new collaborators:
  * Thorsten Lorenz (@thlorenz)
  * Stephen Belanger (@qard)
  * Jeremiah Senkpiel (@fishrock123)
  * Evan Lucas (@evanlucas)
  * Brendan Ashworth (@brendanashworth)

### Commits

* \[[`bb766d2`](https://github.com/nodejs/node/commit/bb766d2c47e8a416ce9f1e4f693f124afe857c1a)] - doc: update "net" section in node to io.js changes (Andres Suarez)
* \[[`73ddaa6`](https://github.com/nodejs/node/commit/73ddaa629c145af1632ac67d5d7d3a2abeabdf24)] - tools: remove old updateAuthors.awk script (Rod Vagg)
* \[[`6230bf9`](https://github.com/nodejs/node/commit/6230bf9b79a6c451d678693004d52249fe9c1702)] - doc: update AUTHORS list (Rod Vagg)
* \[[`33186fa`](https://github.com/nodejs/node/commit/33186fa7d89aef988e5cf24801de891d325afd7d)] - doc: adds brendanashworth as collaborator (Brendan Ashworth)
* \[[`8f9502a`](https://github.com/nodejs/node/commit/8f9502a20a8851cfbf5f6181a52813baec23fe0f)] - doc: add evanlucas to collaborators (Evan Lucas)
* \[[`35a4f11`](https://github.com/nodejs/node/commit/35a4f1107eeab39f9cd0e5b9abe6a314e1f6ddd7)] - doc: alphabetize all.markdown (Brendan Ashworth)
* \[[`a0831c5`](https://github.com/nodejs/node/commit/a0831c580d50b54fd4add58071341b3b7ec83499)] - doc: add Fishrock123 to collaborators (Fishrock123)
* \[[`5412487`](https://github.com/nodejs/node/commit/54124874dcc7eee1e8909cf2056c7f69722be4aa)] - doc: add qard to collaborators (Stephen Belanger)
* \[[`8b55048`](https://github.com/nodejs/node/commit/8b55048d670d22d4e6d93710fe039d576a2b71bc)] - deps: make node-gyp work again on windows (Bert Belder)
* \[[`82227f3`](https://github.com/nodejs/node/commit/82227f35110dcefa5a02e068a78dc3eb4aa0d3bc)] - deps: make node-gyp fetch tarballs from iojs.org (Ben Noordhuis)
* \[[`f5b35db`](https://github.com/nodejs/node/commit/f5b35dbda45c466eda888a4451591c66e8671faf)] - deps: upgrade npm to 2.3.0 (Forrest L Norvell)
* \[[`f3fed51`](https://github.com/nodejs/node/commit/f3fed5193caaac151acd555a7523068ee269801c)] - doc: adding thlorenz to list of collaborators (Thorsten Lorenz)
* \[[`8de89ec`](https://github.com/nodejs/node/commit/8de89ec465d8f1e31521e0b888c19b0a3309cd88)] - lib: move default address logic to `net._listen2` (Vladimir Kurchatkin)
* \[[`3143d73`](https://github.com/nodejs/node/commit/3143d732f6efd82da76e9c53ad192ac14071bf70)] - test: delete parallel/test-process-active-wraps (Ben Noordhuis)
* \[[`4f95b5d`](https://github.com/nodejs/node/commit/4f95b5d8253ef64e3673b9fa178c41dc8109b72b)] - test: fix parallel/test-http-destroyed-socket-write2 (Ben Noordhuis)
* \[[`5ba307a`](https://github.com/nodejs/node/commit/5ba307a97879342ff81aa813ffd7da46b6411b1c)] - test: fix parallel/test-dgram-error-message-address (Ben Noordhuis)
* \[[`f4c536b`](https://github.com/nodejs/node/commit/f4c536b749735a0240da08386d6784767f95cb5d)] - debugger: don't override module binding (Vladimir Kurchatkin)
* \[[`40ffed8`](https://github.com/nodejs/node/commit/40ffed8f3f4392d6e110769ca06d86d6295fc645)] - stream: use nop as write() callback if omitted (cjihrig)
* \[[`df0d790`](https://github.com/nodejs/node/commit/df0d790107edf635dc233f3338b3c2e68db58cc7)] - doc: dns.lookupService has wrong header level (Icer Liang)
* \[[`8b1db9c`](https://github.com/nodejs/node/commit/8b1db9c0a7dc39261218a0fac2dd6cf4fbb6a7b4)] - doc: note in docs about missing interfaces (Todd Kennedy)
* \[[`2928ac6`](https://github.com/nodejs/node/commit/2928ac68e524bb5cacd522507bac0a147d01cd75)] - doc: bump punycode api stability to 'stable' (Ben Noordhuis)
* \[[`328e67b`](https://github.com/nodejs/node/commit/328e67b58bc6dbcbed8ec452e6903ea6f121dc59)] - doc: add TC meeting 2015-01-21 minutes (Rod Vagg)
* \[[`e5e5980`](https://github.com/nodejs/node/commit/e5e598060eb43faf2142184d523a04f0ca2d95c3)] - lib,src: make pseudoRandomBytes alias randomBytes (Calvin Metcalf)
* \[[`c6cd460`](https://github.com/nodejs/node/commit/c6cd46041c70794d89634da380555fb613c2e0ab)] - configure: remove unused arm\_neon variable (Ben Noordhuis)
* \[[`7d9d756`](https://github.com/nodejs/node/commit/7d9d7560cfbd24172ede690e74cedbb4b26e32c9)] - configure: disable vfpv3 on armv6 (Ben Noordhuis)
* \[[`297cadb`](https://github.com/nodejs/node/commit/297cadbab6a37fa4f14811452e4621770a321371)] - deps: fix v8 armv6 run-time detection (Ben Noordhuis)
* \[[`d481bb6`](https://github.com/nodejs/node/commit/d481bb68c4f2cf01ec7d26dcc91862b265b7effa)] - doc: more explicit crypto.pseudoRandomBytes docs (Calvin Metcalf)
* \[[`7d46247`](https://github.com/nodejs/node/commit/7d462479f6aad374fab90dd10bb07a8097f750aa)] - src: s/node/io.js/ in `iojs --help` message (Ben Noordhuis)
* \[[`069c0df`](https://github.com/nodejs/node/commit/069c0dfb1cbfeb7c9c66a30f1fb5f065a9e22ee6)] - deps: upgrade v8 to 4.1.0.12 (Ben Noordhuis)
* \[[`ada2a43`](https://github.com/nodejs/node/commit/ada2a4308c5a70728d01ea7447c0a7a153a9b703)] - doc: add TC meeting 2015-01-13 minutes (Rod Vagg)
* \[[`60402b9`](https://github.com/nodejs/node/commit/60402b924b4b38196a658a023fad945421710457)] - docs: remove incorrect entry from changelog (Bert Belder)
* \[[`8b98096`](https://github.com/nodejs/node/commit/8b98096c921f8a210b05aed64e0b2f1440667a7c)] - fs: make fs.access() flags read only (Jackson Tian)
* \[[`804e7aa`](https://github.com/nodejs/node/commit/804e7aa9ab0b34fa88709ef0980b960abca5e059)] - lib: use const to define constants (cjihrig)
* \[[`803883b`](https://github.com/nodejs/node/commit/803883bb1a701da12c285fd735233eed7627eada)] - v8: fix template literal NULL pointer deref (Ben Noordhuis)
* \[[`5435cf2`](https://github.com/nodejs/node/commit/5435cf2f1619721745c7a8ac06b4f833d0b80d25)] - v8: optimize `getHeapStatistics` (Vladimir Kurchatkin)
* \[[`5d01463`](https://github.com/nodejs/node/commit/5d014637b618af7eac6ab0fce8d67884598c7b35)] - benchmark: print score to five decimal places (Yosuke Furukawa)
* \[[`752585d`](https://github.com/nodejs/node/commit/752585db6355ead7e6484f321e053b8d543c0a67)] - src: silence clang warnings (Trevor Norris)
* \[[`22e1aea`](https://github.com/nodejs/node/commit/22e1aea8a025b6439493dec4d44afe4c9f454c86)] - src: set node\_is\_initialized in node::Init (Cheng Zhao)
* \[[`668420d`](https://github.com/nodejs/node/commit/668420d1f7685f49843bbf81ee3b4733a1989852)] - src: clean up unused macros in node\_file.cc (Ben Noordhuis)
* \[[`52f624e`](https://github.com/nodejs/node/commit/52f624e72a419d3fd7f7f8ccc2d22ebdb0ba4fff)] - src: rename ASSERT macros in node\_crypto.cc (Ben Noordhuis)
* \[[`e95cfe1`](https://github.com/nodejs/node/commit/e95cfe14e343c5abed96a8d3cb9397c0c84abecc)] - src: add ASSERT\_EQ style macros (Ben Noordhuis)
* \[[`ee9cd00`](https://github.com/nodejs/node/commit/ee9cd004d8a211871439fc77c0696b79c5d0e52d)] - lib: fix TypeError with EventEmitter#on() abuse (Ben Noordhuis)
* \[[`77d6807`](https://github.com/nodejs/node/commit/77d68070dafe56b5593ad92759a57c64de6b4cf1)] - test: fix event-emitter-get-max-listeners style (Ben Noordhuis)
* \[[`767ee73`](https://github.com/nodejs/node/commit/767ee7348624803e6f90cf111df8b917fac442fc)] - test: strip copyright boilerplate (Ben Noordhuis)
* \[[`86eda17`](https://github.com/nodejs/node/commit/86eda173b16b6ece9712e066661a0ac5db6795e8)] - fs: define constants with const (cjihrig)

<a id="1.0.3"></a>

## 2015-01-20, Version 1.0.3, @rvagg

### Notable changes

* V8 upgrade from 3.31 to 4.1, this is not a major upgrade, the version number "4.1" signifies tracking towards Chrome 41. The 3.31 branch is now not tracking towards a stable release.
* Re-enable Windows XP / 2003 support
* npm upgrade to 2.2.0
* Improved FreeBSD support

### Known issues

* ARMv6 builds still not working, there is a hold-up in V8 on this, issue #283
* Template strings can cause segfaults in V8 4.1, <https://codereview.chromium.org/857433004>, also issue #333

### Commits

* \[[`9419e1f`](https://github.com/nodejs/node/commit/9419e1fb698e1a9319fec5c4777686d62fad4a51)] - src: fix inconsistency between a check and error (toastynerd)
* \[[`03ee4d8`](https://github.com/nodejs/node/commit/03ee4d854744e83f99bc5857b98f75139c448564)] - fs: add error code on null byte paths (cjihrig)
* \[[`e2558f0`](https://github.com/nodejs/node/commit/e2558f02dfb671fc74f5768d4401a826efb5c117)] - net: fix error details in connect() (cjihrig)
* \[[`4af5746`](https://github.com/nodejs/node/commit/4af5746993a6b91c88973b6debcee19c6cd35185)] - win,build: remove duplicate definition (Bert Belder)
* \[[`e8d0850`](https://github.com/nodejs/node/commit/e8d08503c7821e8c92e9fa236ed7328e9bdfe62a)] - win: bring back xp/2k3 support (Bert Belder)
* \[[`4dd22b9`](https://github.com/nodejs/node/commit/4dd22b946ebfec81a7c4a61aa9c6ed528e317802)] - cluster: avoid race enabling debugger in worker (Timothy J Fontaine)
* \[[`6b91c78`](https://github.com/nodejs/node/commit/6b91c78e201948937a4524027a6778aa7f82fb0a)] - test: reland changes from [`11c1bae`](https://github.com/nodejs/node/commit/11c1bae734dae3a017f2c4f3f71b5e679a9ddfa6) (Ben Noordhuis)
* \[[`992a1e7`](https://github.com/nodejs/node/commit/992a1e7f5f87606276af8504c2d57cc5a966830a)] - test: debug-signal-cluster should not be racey (Timothy J Fontaine)
* \[[`cdf0df1`](https://github.com/nodejs/node/commit/cdf0df13d85391b3b8ac36fa5b70da7f21072619)] - test: temporarily back out changes from [`11c1bae`](https://github.com/nodejs/node/commit/11c1bae734dae3a017f2c4f3f71b5e679a9ddfa6) (Ben Noordhuis)
* \[[`1ea607c`](https://github.com/nodejs/node/commit/1ea607cb299b0bb59d7d557e01b21b3c615d689e)] - test: move sequential/test-debug-port-from-cmdline (Ben Noordhuis)
* \[[`2f33e00`](https://github.com/nodejs/node/commit/2f33e00d716d692e84b02768430664fd92298c98)] - test: fix test-debug-port-from-cmdline.js (Julien Gilli)
* \[[`b7365c1`](https://github.com/nodejs/node/commit/b7365c15597253e906590045aa6f3f07f6e76b52)] - repl: make REPL support multiline template literals (Xiaowei Li)
* \[[`2253d30`](https://github.com/nodejs/node/commit/2253d30d9cbba42abc1faa183e4480cac69c4222)] - build: remove unused variable (Johan Bergström)
* \[[`ab04a43`](https://github.com/nodejs/node/commit/ab04a434761cf66d107481d58798f36d3cb49d46)] - doc: add optional sudo to make install in README (Glen Keane)
* \[[`1b1cd1c`](https://github.com/nodejs/node/commit/1b1cd1c3f8e21b34a8e1355e545057a661acaa15)] - build: shorten configurate script print on stdout (Roman Reiss)
* \[[`d566ded`](https://github.com/nodejs/node/commit/d566ded26b996c27afeb7fc208709bb6096bfa13)] - deps: fix V8 debugger bugs (Jay Jaeho Lee)
* \[[`6f36630`](https://github.com/nodejs/node/commit/6f36630f55efcbe5954a52ac22bbb0a378020e98)] - doc: fix util.isBuffer examples (Thomas Jensen)
* \[[`3abfb56`](https://github.com/nodejs/node/commit/3abfb56f9b012da0d1e1deaec1529ea7384a0a71)] - benchmark: fix tcp bench after internal api change (Yosuke Furukawa)
* \[[`50177fb`](https://github.com/nodejs/node/commit/50177fb13cae68067845cca7622798eb7a34f8e9)] - benchmark: stop v8 benchmark clobbering RegExp (Ben Noordhuis)
* \[[`1952219`](https://github.com/nodejs/node/commit/19522197ef28275344ad2f1e0799ce8106276ec1)] - deps: make node-gyp work again on windows (Bert Belder)
* \[[`a28de9b`](https://github.com/nodejs/node/commit/a28de9bd3684f54379ccf101f62656771002205d)] - deps: make node-gyp fetch tarballs from iojs.org (Ben Noordhuis)
* \[[`9dc8f59`](https://github.com/nodejs/node/commit/9dc8f59fea5a294df039f70e523be2d45aef1324)] - deps: upgrade npm to 2.2.0 (Forrest L Norvell)
* \[[`e8ad773`](https://github.com/nodejs/node/commit/e8ad773b56a94fad2cd8a454453a7214a8ce92d1)] - src: remove --noharmony\_classes again (Ben Noordhuis)
* \[[`334020e`](https://github.com/nodejs/node/commit/334020e016a72952a9a3b3f7e9179145c7e167ad)] - deps: fix v8 build on FreeBSD (Fedor Indutny)
* \[[`5e7ebc7`](https://github.com/nodejs/node/commit/5e7ebc7af6d08d4e31cf66f4ae22d29c688ef814)] - deps: upgrade v8 to 4.1.0.7 (Ben Noordhuis)
* \[[`ea7750b`](https://github.com/nodejs/node/commit/ea7750bddd8051f39fa538905e05f9bf1d1afa5f)] - benchmark: add filter option for benchmark (Yosuke Furukawa)
* \[[`4764eef`](https://github.com/nodejs/node/commit/4764eef9b2efdf17cafeb4ec40898c6669a84e3b)] - doc: fixed punctuation (Brenard Cubacub)
* \[[`de224d6`](https://github.com/nodejs/node/commit/de224d6e6c9381e71ffee965dbda928802cc438e)] - configure: remove --ninja switch (Ben Noordhuis)
* \[[`48774ec`](https://github.com/nodejs/node/commit/48774ec027a28cca17656659d316bb7ed8d6f33c)] - configure: print warning for old compilers (Ben Noordhuis)
* \[[`daf9562`](https://github.com/nodejs/node/commit/daf9562d918b7926186471cd0db60cec2f72547a)] - doc: change to iojs from node in the usage message (Jongyeol Choi)
* \[[`3fde649`](https://github.com/nodejs/node/commit/3fde64937a3a0c8ed941ee97b07e1828b392a672)] - build: add tools/gflags to PYTHONPATH (Shigeki Ohtsu)
* \[[`8b22df1`](https://github.com/nodejs/node/commit/8b22df15ae0e3499b2e057ffd8a6f65cbf978da3)] - doc: add python-gflags LICENSE block (Shigeki Ohtsu)
* \[[`6242229`](https://github.com/nodejs/node/commit/62422297f52523d2214136cd5514e2453197e3e8)] - tools: add python-gflags module (Shigeki Ohtsu)

<a id="1.0.2"></a>

## 2015-01-16, Version 1.0.2, @rvagg

### Notable changes

* Windows installer fixes
* Bundled node-gyp fixes for Windows
* [http\_parser v2.4.1 upgrade](https://github.com/joyent/http-parser/compare/v2.3...v2.4.1)
* [libuv v1.2.1 upgrade](https://github.com/libuv/libuv/compare/v1.2.0...v1.2.1)

### Commits

* \[[`265cb76`](https://github.com/nodejs/node/commit/265cb76517d81408afb72506c778f0c0b889f4dc)] - build: add new installer config for OS X (Rod Vagg)
* \[[`8cf6079`](https://github.com/nodejs/node/commit/8cf6079a6a7f5d1afb06606b7c51acf9b1a046a0)] - doc: update AUTHORS list (Rod Vagg)
* \[[`c80a944`](https://github.com/nodejs/node/commit/c80a9449b309f9c52a5910b7ac6ba0c84ee1b6f6)] - doc: Add http keepalive behavior to CHANGELOG.md (Isaac Z. Schlueter)
* \[[`9b81c3e`](https://github.com/nodejs/node/commit/9b81c3e77ffd733645956129a38fdc2fddd08b50)] - doc: fix author attribution (Tom Hughes)
* \[[`fd30eb2`](https://github.com/nodejs/node/commit/fd30eb21526bdaa5aabb15523b0a766e0cbbe535)] - src: fix jslint errors (Yosuke Furukawa)
* \[[`946eabd`](https://github.com/nodejs/node/commit/946eabd18f623b438e17164b14c98066f7054168)] - tools: update closure linter to 2.3.17 (Yosuke Furukawa)
* \[[`9e62ae4`](https://github.com/nodejs/node/commit/9e62ae4304a0bee3aec8c5fb743eb17d78b1cd35)] - \_debug\_agent: use `readableObjectMode` option (Vladimir Kurchatkin)
* \[[`eec4c81`](https://github.com/nodejs/node/commit/eec4c8168be1f0a01db3576ae99f7756eea01151)] - doc: fix formatting in LICENSE for RTF generation (Rod Vagg)
* \[[`e789103`](https://github.com/nodejs/node/commit/e7891034c269dccf8d6264acc4b7421e19a905f6)] - doc: fix 404s for syntax highlighting js (Phil Hughes)
* \[[`ca039b4`](https://github.com/nodejs/node/commit/ca039b4616915b95130ba5ee5a2cf9f5c768645e)] - src: define AI\_V4MAPPED for OpenBSD (Aaron Bieber)
* \[[`753fcaa`](https://github.com/nodejs/node/commit/753fcaa27066b34a99ee1c02b43a32744fc92a3c)] - doc: extend example of http.request by end event (Michal Tehnik)
* \[[`8440cac`](https://github.com/nodejs/node/commit/8440cacb100ae83c2b2c02e82a87c73a66380c21)] - src: fix documentation url in help message (Shigeki Ohtsu)
* \[[`24def66`](https://github.com/nodejs/node/commit/24def662936ae8c15770ede0344cd7a7402a63ef)] - win,msi: warn that older io.js needs manual uninstall (Bert Belder)
* \[[`59d9361`](https://github.com/nodejs/node/commit/59d93613d8e1e8507b5c8462c52dd3cbda98e99b)] - win,msi: change UpgradeCode (Bert Belder)
* \[[`5de334c`](https://github.com/nodejs/node/commit/5de334c23096492014a097ff487f07ad8eaee6d2)] - deps: make node-gyp work again on windows (Bert Belder)
* \[[`07bd05b`](https://github.com/nodejs/node/commit/07bd05ba332e078c1ba76635921f5448a3e884cf)] - deps: update libuv to 1.2.1 (Saúl Ibarra Corretgé)
* \[[`e177377`](https://github.com/nodejs/node/commit/e177377a4bc0cdbaecb8b17a58e57c73b4ca0090)] - doc: mention io.js alongside Node in Punycode docs (Mathias Bynens)
* \[[`598efcb`](https://github.com/nodejs/node/commit/598efcbe7f4d795622f038e0ba28c7b119927a14)] - deps: update http\_parser to 2.4.1 (Fedor Indutny)
* \[[`3dd7ebb`](https://github.com/nodejs/node/commit/3dd7ebb0ba181960fb6d7131e11243a6ec85458d)] - doc: update cluster entry in CHANGELOG (Ben Noordhuis)
* \[[`0c5de1f`](https://github.com/nodejs/node/commit/0c5de1ff813de9661d33cb9aefc4a9540b58b147)] - doc: fix double smalloc example (Mathias Buus)

<a id="1.0.1"></a>

## 2015-01-14, Version 1.0.1, @rvagg

Rebuild due to stale build worker git reflogs for 1.0.0 release

* doc: improve write style consistency (Rui Marinho)
* win,msi: correct doc website link (Bert Belder)

***

<a id="1.0.0"></a>
Below is a summary of the user-facing changes to be found in the io.js v1.0.0 release as compared to the
current _stable_ Node.js release, v0.10.35. At the time of the v1.0.0 release, the latest _unstable_
Node.js release is v0.11.14 with much progress made towards a v0.11.15 release. The io.js codebase inherits
the majority of the changes found in the v0.11 branch of the [joyent/node](https://github.com/joyent/node)
repository and therefore can be seen as an extension to v0.11.

## Summary of changes from Node.js v0.10.35 to io.js v1.0.0

### General

* The V8 JavaScript engine bundled with io.js was upgraded dramatically, from version 3.14.5.9 in Node.js v0.10.35 and 3.26.33 in Node.js v0.11.14 to 3.31.74.1 for io.js v1.0.0. This brings along many fixes and performance improvements, as well as additional support for new ES6 language features! For more information on this, check out [the io.js ES6 page](https://iojs.org/es6.html).
* Other bundled technologies were upgraded:
  * c-ares: 1.9.0-DEV to 1.10.0-DEV
  * http\_parser: 1.0 to 2.3
  * libuv: 0.10.30 to 1.2.0
  * npm: 1.4.28 to 2.1.18
  * openssl: 1.0.1j to 1.0.1k
  * punycode: 1.2.0 to 1.3.2.
* Performance and stability improvements on all platforms.

### buffer

<https://iojs.org/api/buffer.html>

* Added `buf.writeUIntLE`, `buf.writeUIntBE`, `buf.writeIntLE`, `buf.writeIntBE`, `buf.readUIntLE`, `buf.readUIntBE`, `buf.readIntLE` and `buf.readIntBE` methods that read and write value up to 6 bytes.
* Added `Buffer.compare()` which does a `memcmp()` on two Buffer instances. Instances themselves also have a `compare()`.
* Added `buffer.equals()` that checks equality of Buffers by their contents.
* Added `new Buffer(otherBuffer)` constructor.
* Tweaked `SlowBuffer`'s semantics.
* Updated the output of `buffer.toJSON()` to not be the same as an array. Instead it is an object specifically tagged as a buffer, which can be recovered by passing it to (a new overload of) the `Buffer` constructor.

### child\_process

<https://iojs.org/api/child_process.html>

* Added a `shell` option to `child_process.exec`.
* Added synchronous counterparts for the child process functions: `child_process.spawnSync`, `child_process.execSync`, and `child_process.execFileSync`.
* Added the path to any `ENOENT` errors, for easier debugging.

### console

<https://iojs.org/api/console.html>

* Added an `options` parameter to `console.dir`.

### cluster

<https://iojs.org/api/cluster.html>

* Updated `cluster` to use round-robin load balancing by default on non-Windows platforms. The scheduling policy is configurable however.
* `--debug` has been made cluster-aware.
* Many bug fixes.

### crypto

<https://iojs.org/api/crypto.html>

* Added support for custom generator values to `DiffieHellman` (defaulting to 2 for backwards compatibility).
* Added support for custom pbkdf2 digest methods.
* Added support for elliptic curve-based Diffie-Hellman.
* Added support for loading and setting the engine for some/all OpenSSL functions.
* Added support for passing in a passphrase for decrypting the signing key to `Sign.sign()`.
* Added support for private key passphrase in every method that accepts it.
* Added support for RSA public/private encryption/decryption functionality.
* Added support for setting and getting of authentication tags and setting additional authentication data when using ciphers such as AES-GCM.

### dgram

<https://iojs.org/api/dgram.html>

* Added support for receiving empty UDP packets.

### dns

<https://iojs.org/api/dns.html>

* Added `dns.resolveSoa`, `dns.getServers`, and `dns.setServers` methods.
* Added `hostname` on error messages when available.
* Improved error handling consistency.

### events

<https://iojs.org/api/events.html>

* Added chaining support to `EventEmitter.setMaxListeners`.
* Updated `require('events')` to return the `EventEmitter` constructor, allowing the module to be used like `var EventEmitter = require('events')` instead of `var EventEmitter = require('events').EventEmitter`.

### fs

<https://iojs.org/api/fs.html>

* Added `fs.access`, and deprecated `fs.exists`. Please read the documentation carefully.
* Added more informative errors and method call site details when the `NODE_DEBUG` environment is set to ease debugging.
* Added option to `fs.watch` for recursive sub-directory support (OS X only).
* Fixed missing callbacks errors just being printed instead of thrown.

### http

<https://iojs.org/api/http.html>

* Added support for `response.write` and `response.end` to receive a callback to know when the operation completes.
* Added support for 308 status code (see RFC 7238).
* Added `http.METHODS` array, listing the HTTP methods supported by the parser.
* Added `request.flush` method.
* Added `response.getHeader('header')` method that may be used before headers are flushed.
* Added `response.statusMessage` property.
* Added Client Keep-Alive behavior.  Set `keepAlive:true` in request options to reuse connections indefinitely.
* Added `rawHeaders` and `rawTrailers` members on incoming message.
* Removed default chunked encoding on `DELETE` and `OPTIONS`.

### net

<https://iojs.org/api/net.html>

* Changed `net.Server.listen` such that, when the bind address is omitted, IPv6 is tried first, and IPv4 is used as a fallback.

### os

<https://iojs.org/api/os.html>

* Added MAC addresses, netmasks and scope IDs for IPv6 addresses to `os.networkInterfaces` method output.
* Updated `os.tmpdir` on Windows to use the `%SystemRoot%` or `%WINDIR%` environment variables instead of the hard-coded value of `c:\windows` when determining the temporary directory location.

### path

<https://iojs.org/api/path.html>

* Added `path.isAbsolute` and `path.parse` methods.
* Added `path.win32` and `path.posix` objects that contain platform-specific versions of the various `path` functions.
* Improved `path.join` performance.

### process

<https://iojs.org/api/process.html>

* Added `beforeExit` event.
* Added `process.mainModule` and `process.exitCode`.

### querystring

<https://iojs.org/api/querystring.html>

* Added the ability to pass custom versions of `encodeURIComponent` and `decodeURIComponent` when stringifying or parsing a querystring.
* Fixed several issues with the formatting of query strings in edge cases.

### smalloc

<https://iojs.org/api/smalloc.html>

`smalloc` is a new core module for doing (external) raw memory allocation/deallocation/copying in JavaScript.

### streams

<https://iojs.org/api/stream.html>

The changes to streams are not as drastic as the transition from streams1 to streams2: they are a
refinement of existing ideas, and should make the API slightly less surprising for humans and faster
for computers. As a whole the changes are referred to as "streams3", but the changes should largely go
unnoticed by the majority of stream consumers and implementers.

#### Readable streams

The distinction between "flowing" and "non-flowing" modes has been refined. Entering "flowing" mode is
no longer an irreversible operation—it is possible to return to "non-flowing" mode from "flowing" mode.
Additionally, the two modes now flow through the same machinery instead of replacing methods. Any time
data is returned as a result of a `.read` call that data will _also_ be emitted on the `"data"` event.

As before, adding a listener for the `"readable"` or `"data"` event will start flowing the stream; as
will piping to another stream.

#### Writable streams

The ability to "bulk write" to underlying resources has been added to `Writable` streams. For stream
implementers, one can signal that a stream is bulk-writable by specifying a [\_writev](https://iojs.org/api/stream.html#stream_writable_writev_chunks_callback) method.
Bulk writes will occur in two situations:

1. When a bulk-writable stream is clearing its backlog of buffered write requests,
2. or if an end user has made use of the new `.cork()` and `.uncork()` API methods.

`.cork` and `.uncork` allow the end user to control the buffering behavior of writable streams separate
from exerting backpressure. `.cork` indicates that the stream should accept new writes (up to `highWaterMark`),
while `.uncork` resets that behavior and attempts to bulk-write all buffered writes to the underlying resource.

The only core stream API that **currently** implements `_writev` is `net.Socket`.

In addition to the bulk-write changes, the performance of repeated small writes to non-bulk-writable streams
(such as `fs.WriteStream`) has been drastically improved. Users piping high volume log streams to disk should
see an improvement.

For a detailed overview of how streams3 interact, [see this diagram](https://cloud.githubusercontent.com/assets/37303/5728694/f9a3e300-9b20-11e4-9e14-a6938b3327f0.png).

### timers

<https://iojs.org/api/timers.html>

* Removed `process.maxTickDepth`, allowing `process.nextTick` to be used recursively without limit.
* Updated `setImmediate` to process the full queue each turn of the event loop, instead of one per queue.

### tls

<https://iojs.org/api/tls.html>

* Added `detailed` boolean flag to `getPeerCertificate` to return detailed certificate information (with raw DER bytes).
* Added `renegotiate(options, callback)` method for session renegotiation.
* Added `setMaxSendFragment` method for varying TLS fragment size.
* Added a `dhparam` option for DH ciphers.
* Added a `ticketKeys` option for TLS ticket AES encryption keys setup.
* Added async OCSP-stapling callback.
* Added async session storage events.
* Added async SNI callback.
* Added multi-key server support (for example, ECDSA+RSA server).
* Added optional callback to `checkServerIdentity` for manual certificate validation in userland.
* Added support for ECDSA/ECDHE cipher.
* Implemented TLS streams in C++, boosting their performance.
* Moved `createCredentials` to `tls` and renamed it to `createSecureContext`.
* Removed SSLv2 and SSLv3 support.

### url

<https://iojs.org/api/url.html>

* Improved escaping of certain characters.
* Improved parsing speed.

### util

<https://iojs.org/api/util.html>

* Added `util.debuglog`.
* Added a plethora of new type-testing methods. See [the docs](https://iojs.org/api/util.html).
* Updated `util.format` to receive several changes:
  * `-0` is now displayed as such, instead of as `0`.
  * Anything that is `instanceof Error` is now formatted as an error.
  * Circular references in JavaScript objects are now handled for the `%j` specifier.
  * Custom `inspect` functions are now allowed to return an object.
  * Custom `inspect` functions now receive any arguments passed to `util.inspect`.

## v8

<https://iojs.org/api/v8.html>

`v8` is a new core module for interfacing directly with the V8 engine.

### vm

<https://iojs.org/api/vm.html>

The `vm` module has been rewritten to work better, based on the excellent [Contextify](https://github.com/brianmcd/contextify) native module. All of the functionality of Contextify is now in core, with improvements!

* Added `vm.isContext(object)` method to determine whether `object` has been contextified.
* Added `vm.runInDebugContext(code)` method to compile and execute `code` inside the V8 debug context.
* Updated `vm.createContext(sandbox)` to "contextify" the sandbox, making it suitable for use as a global for `vm` scripts, and then return it. It no longer creates a separate context object.
* Updated most `vm` and `vm.Script` methods to accept an `options` object, allowing you to configure a timeout for the script, the error display behavior, and sometimes the filename (for stack traces).
* Updated the supplied sandbox object to be used directly as the global, remove error-prone copying of properties back and forth between the supplied sandbox object and the global that appears inside the scripts run by the `vm` module.

For more information, see the `vm` documentation linked above.

### zlib

<https://iojs.org/api/zlib.html>

* Added support for `zlib.flush` to specify a particular flush method (defaulting to `Z_FULL_FLUSH`).
* Added support for `zlib.params` to dynamically update the compression level and strategy when deflating.
* Added synchronous versions of the zlib methods.

### C++ API Changes

<https://iojs.org/api/addons.html>

In general it is recommended that you use [NAN](https://github.com/rvagg/nan) as a compatibility layer for your addons. This will also help with future changes in the V8 and Node/io.js C++ API. Most of the following changes are already handled by NAN-specific wrappers.

#### V8 highlights

* Exposed method signature has changed from `Handle<Value> Method(const Arguments& args)` to `void Method(const v8::FunctionCallbackInfo<Value>& args)` with the newly introduced `FunctionCallbackInfo` also taking the return value via `args.GetReturnValue().Set(value)` instead of `scope.Close(value)`, `Arguments` has been removed.
* Exposed setter signature has changed from `void Setter(Local<String> property, Local<Value> value, const v8::AccessorInfo& args)` `void Setter(Local<String> property, Local<Value> value, const v8::PropertyCallbackInfo<void>& args)`.
* Exposed getter signature has changed from `void Getter(Local<String> property, Local<Value> value, const v8::AccessorInfo& args)` `void Getter(Local<String> property, Local<Value> value, const v8::PropertyCallbackInfo<Value>& args)`.
* Exposed property setter signature has changed from `Handle<Value> Setter(Local<String> property, Local<Value> value, const v8::AccessorInfo& args)` `void Setter(Local<String> property, Local<Value> value, const v8::PropertyCallbackInfo<Value>& args)`.
* Exposed property getter signature has changed from `Handle<Value> Getter(Local<String> property, Local<Value> value, const v8::AccessorInfo& args)` `void Getter(Local<String> property, Local<Value> value, const v8::PropertyCallbackInfo<Value>& args)`.
* Similar changes have been made to property enumerators, property deleters, property query, index getter, index setter, index enumerator, index deleter, index query.
* V8 objects instantiated in C++ now require an `Isolate*` argument as the first argument. In most cases it is OK to simply pass `v8::Isolate::GetCurrent()`, e.g. `Date::New(Isolate::GetCurrent(), time)`, or `String::NewFromUtf8(Isolate::GetCurrent(), "foobar")`.
* `HandleScope scope` now requires an `Isolate*` argument, i.e. `HandleScope scope(isolate)`, in most cases `v8::Isolate::GetCurrent()` is OK.
* Similar changes have been made to `Locker` and `Unlocker`.
* V8 objects that need to "escape" a scope should be enclosed in a `EscapableHandleScope` rather than a `HandleScope` and should be returned with `scope.Escape(value)`.
* Exceptions are now thrown from isolates with `isolate->ThrowException(ExceptionObject)`.
* `Context::GetCurrent()` must now be done on an isolate, e.g. `Isolate::GetCurrent()->GetCurrentContext()`.
* `String::NewSymbol()` has been removed, use plain strings instead.
* `String::New()` has been removed, use `String::NewFromUtf8()` instead.
* `Persistent` objects no longer inherit from `Handle` and cannot be instantiated with another object. Instead, the `Persistent` should simply be declared, e.g. `Persistent<Type> handle` and then have a `Local` assigned to it with `handle.Reset(isolate, value)`. To get a `Local` from a `Persistent` you must instantiate it as the argument, i.e. `Local::New(Isolate*, Persistent)`.

#### Node / io.js

* Updated `node::Buffer::New()` to return a `Handle` directly so you no longer need to fetch the `handle_` property.
* Updated `node::MakeCallback()` to require an `Isolate*` as the first argument. Generally `Isolate::GetCurrent()` will be OK for this.

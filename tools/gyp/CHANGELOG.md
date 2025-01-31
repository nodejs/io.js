# Changelog

## [0.19.1](https://github.com/nodejs/gyp-next/compare/v0.19.0...v0.19.1) (2024-12-09)


### Bug Fixes

* fixup for break in EscapeForCString ([#274](https://github.com/nodejs/gyp-next/issues/274)) ([610f661](https://github.com/nodejs/gyp-next/commit/610f661da877a358c8b3cbc106b528fb1d0b8095))

## [0.19.0](https://github.com/nodejs/gyp-next/compare/v0.18.3...v0.19.0) (2024-12-03)


### Features

* provide escaped version of `PRODUCT_DIR_ABS` ([#271](https://github.com/nodejs/gyp-next/issues/271)) ([3bf3b1c](https://github.com/nodejs/gyp-next/commit/3bf3b1cda26f16c645e0fdd5582ffbf49d9a2580))

## [0.18.3](https://github.com/nodejs/gyp-next/compare/v0.18.2...v0.18.3) (2024-10-08)


### Bug Fixes

* enable pch for clang on windows ([#268](https://github.com/nodejs/gyp-next/issues/268)) ([cc5838c](https://github.com/nodejs/gyp-next/commit/cc5838c4e9260bf459d71de53fbb2eebd1a6f508))

## [0.18.2](https://github.com/nodejs/gyp-next/compare/v0.18.1...v0.18.2) (2024-09-23)


### Bug Fixes

* do not assume that /usr/bin/env exists on macOS ([#216](https://github.com/nodejs/gyp-next/issues/216)) ([706d04a](https://github.com/nodejs/gyp-next/commit/706d04aba5bd18f311dc56f84720e99f64c73466))
* fix E721 lint errors ([#206](https://github.com/nodejs/gyp-next/issues/206)) ([d1299a4](https://github.com/nodejs/gyp-next/commit/d1299a49d313eccabecf97ccb56fc033afad39ad))

## [0.18.1](https://github.com/nodejs/gyp-next/compare/v0.18.0...v0.18.1) (2024-05-26)


### Bug Fixes

* **ci:** add Python 3.13 pre-release to test matrix ([#257](https://github.com/nodejs/gyp-next/issues/257)) ([8597203](https://github.com/nodejs/gyp-next/commit/8597203b687325c7516367135e026586279d0583))


### Documentation

* vendor docs from gyp.gsrc.io ([#254](https://github.com/nodejs/gyp-next/issues/254)) ([8d7ba6e](https://github.com/nodejs/gyp-next/commit/8d7ba6e784dedf1122a0456150c739d2a09ecf57))

## [0.18.0](https://github.com/nodejs/gyp-next/compare/v0.17.0...v0.18.0) (2024-05-08)


### Features

* support language standard keys in msvs_settings ([#252](https://github.com/nodejs/gyp-next/issues/252)) ([322f6d5](https://github.com/nodejs/gyp-next/commit/322f6d5d5233967522f3e55c623a8e7d7281e024))

## [0.17.0](https://github.com/nodejs/gyp-next/compare/v0.16.2...v0.17.0) (2024-04-29)


### Features

* generate compile_commands.json with ninja ([#228](https://github.com/nodejs/gyp-next/issues/228)) ([7b20b46](https://github.com/nodejs/gyp-next/commit/7b20b4673d8cf46ff61898eb19569007d55c854a))


### Bug Fixes

* failed to detect flavor if compiler path include white spaces ([#240](https://github.com/nodejs/gyp-next/issues/240)) ([f3b9753](https://github.com/nodejs/gyp-next/commit/f3b9753e7526377020e7d40e66b624db771cf84a))
* support cross compiling for wasm with make generator ([#222](https://github.com/nodejs/gyp-next/issues/222)) ([de0e1c9](https://github.com/nodejs/gyp-next/commit/de0e1c9a5791d1bf4bc3103f878ab74814864ab4))
* support empty dictionary keys in input ([#245](https://github.com/nodejs/gyp-next/issues/245)) ([178459f](https://github.com/nodejs/gyp-next/commit/178459ff343a2771d5f30f04467d2f032d6b3565))
* update Ruff to 0.3.1 ([876ccaf](https://github.com/nodejs/gyp-next/commit/876ccaf5629e1b95e13aaa2b0eb6cbd08fa80593))

## [0.16.2](https://github.com/nodejs/gyp-next/compare/v0.16.1...v0.16.2) (2024-03-07)


### Bug Fixes

* avoid quoting cflag name and parameter with space separator ([#223](https://github.com/nodejs/gyp-next/issues/223)) ([2b9703d](https://github.com/nodejs/gyp-next/commit/2b9703dbd5b3b8a935faf257c6103033b47bf8bf))

## [0.16.1](https://github.com/nodejs/gyp-next/compare/v0.16.0...v0.16.1) (2023-10-25)


### Bug Fixes

* add quotes for command in msvs generator ([#217](https://github.com/nodejs/gyp-next/issues/217)) ([d3b7bcd](https://github.com/nodejs/gyp-next/commit/d3b7bcdec90d6c1b1affc15ece706e63007b7264))

## [0.16.0](https://github.com/nodejs/gyp-next/compare/v0.15.1...v0.16.0) (2023-10-23)


### Features

* add VCToolsVersion for msvs ([#209](https://github.com/nodejs/gyp-next/issues/209)) ([0e35ab8](https://github.com/nodejs/gyp-next/commit/0e35ab812d890fb75cf89a19ea72bc93dd6ba186))

## [0.15.1](https://github.com/nodejs/gyp-next/compare/v0.15.0...v0.15.1) (2023-09-08)


### Bug Fixes

* some Python lint issues ([#200](https://github.com/nodejs/gyp-next/issues/200)) ([d2dfe4e](https://github.com/nodejs/gyp-next/commit/d2dfe4e66b64c16b38bef984782db93d12674f05))
* use generator_output as output_dir ([#191](https://github.com/nodejs/gyp-next/issues/191)) ([35ffeb1](https://github.com/nodejs/gyp-next/commit/35ffeb1da8ef3fc8311e2e812cff550568f7e8a2))

## [0.15.0](https://github.com/nodejs/gyp-next/compare/v0.14.1...v0.15.0) (2023-03-30)


### Features

* **msvs:** add SpectreMitigation attribute ([#190](https://github.com/nodejs/gyp-next/issues/190)) ([853e464](https://github.com/nodejs/gyp-next/commit/853e4643b6737224a5aa0720a4108461a0230991))

## [0.14.1](https://github.com/nodejs/gyp-next/compare/v0.14.0...v0.14.1) (2023-02-19)


### Bug Fixes

* flake8 extended-ignore ([#186](https://github.com/nodejs/gyp-next/issues/186)) ([c38493c](https://github.com/nodejs/gyp-next/commit/c38493c2556aa63b6dc40ab585c18aef5ca270d3))
* No build_type in default_variables ([#183](https://github.com/nodejs/gyp-next/issues/183)) ([ac262fe](https://github.com/nodejs/gyp-next/commit/ac262fe82453c4e8dc47529338d157eb0b5ec0fb))


### Documentation

* README.md: Add pipx installation and run instructions ([#165](https://github.com/nodejs/gyp-next/issues/165)) ([4d28b15](https://github.com/nodejs/gyp-next/commit/4d28b155568dc35f11c7f86124d1dd42ba428bed))

## [0.14.0](https://github.com/nodejs/gyp-next/compare/v0.13.0...v0.14.0) (2022-10-08)


### Features

* Add command line argument for `gyp --version` ([#164](https://github.com/nodejs/gyp-next/issues/164)) ([5c9f4d0](https://github.com/nodejs/gyp-next/commit/5c9f4d05678dd855e18ed2327219e5d18e5374db))
* ninja build for iOS ([#174](https://github.com/nodejs/gyp-next/issues/174)) ([b6f2714](https://github.com/nodejs/gyp-next/commit/b6f271424e0033d7ed54d437706695af2ba7a1bf))
* **zos:** support IBM Open XL C/C++ & PL/I compilers on z/OS ([#178](https://github.com/nodejs/gyp-next/issues/178)) ([43a7211](https://github.com/nodejs/gyp-next/commit/43a72110ae3fafb13c9625cc7a969624b27cda47))


### Bug Fixes

* lock windows env ([#163](https://github.com/nodejs/gyp-next/issues/163)) ([44bd0dd](https://github.com/nodejs/gyp-next/commit/44bd0ddc93ea0b5770a44dd326a2e4ae62c21442))
* move configuration information into pyproject.toml ([#176](https://github.com/nodejs/gyp-next/issues/176)) ([d69d8ec](https://github.com/nodejs/gyp-next/commit/d69d8ece6dbff7af4f2ea073c9fd170baf8cb7f7))
* node.js debugger adds stderr (but exit code is 0) -> shouldn't throw ([#179](https://github.com/nodejs/gyp-next/issues/179)) ([1a457d9](https://github.com/nodejs/gyp-next/commit/1a457d9ed08cfd30c9fa551bc5cf0d90fb583787))

## [0.13.0](https://www.github.com/nodejs/gyp-next/compare/v0.12.1...v0.13.0) (2022-05-11)


### Features

* add PRODUCT_DIR_ABS variable ([#151](https://www.github.com/nodejs/gyp-next/issues/151)) ([80d2626](https://www.github.com/nodejs/gyp-next/commit/80d26263581db829b61b312a7bdb5cc791df7824))


### Bug Fixes

* execvp: printf: Argument list too long ([#147](https://www.github.com/nodejs/gyp-next/issues/147)) ([c4e14f3](https://www.github.com/nodejs/gyp-next/commit/c4e14f301673fadbac3ab7882d0b5f4d02530cb9))

### [0.12.1](https://www.github.com/nodejs/gyp-next/compare/v0.12.0...v0.12.1) (2022-04-06)


### Bug Fixes

* **msvs:** avoid fixing path for arguments with "=" ([#143](https://www.github.com/nodejs/gyp-next/issues/143)) ([7e8f16e](https://www.github.com/nodejs/gyp-next/commit/7e8f16eb165e042e64bec98fa6c2a0232a42c26b))

## [0.12.0](https://www.github.com/nodejs/gyp-next/compare/v0.11.0...v0.12.0) (2022-04-04)


### Features

* support building shared libraries on z/OS ([#137](https://www.github.com/nodejs/gyp-next/issues/137)) ([293bcfa](https://www.github.com/nodejs/gyp-next/commit/293bcfa4c25c6adb743377adafc45a80fee492c6))

## [0.11.0](https://www.github.com/nodejs/gyp-next/compare/v0.10.1...v0.11.0) (2022-03-04)


### Features

* Add proper support for IBM i ([#140](https://www.github.com/nodejs/gyp-next/issues/140)) ([fdda4a3](https://www.github.com/nodejs/gyp-next/commit/fdda4a3038b8a7042ad960ce7a223687c24a21b1))

### [0.10.1](https://www.github.com/nodejs/gyp-next/compare/v0.10.0...v0.10.1) (2021-11-24)


### Bug Fixes

* **make:** only generate makefile for multiple toolsets if requested ([#133](https://www.github.com/nodejs/gyp-next/issues/133)) ([f463a77](https://www.github.com/nodejs/gyp-next/commit/f463a77705973289ea38fec1b244c922ac438e26))

## [0.10.0](https://www.github.com/nodejs/gyp-next/compare/v0.9.6...v0.10.0) (2021-08-26)


### Features

* **msvs:** add support for Visual Studio 2022 ([#124](https://www.github.com/nodejs/gyp-next/issues/124)) ([4bd9215](https://www.github.com/nodejs/gyp-next/commit/4bd9215c44d300f06e916aec1d6327c22b78272d))

### [0.9.6](https://www.github.com/nodejs/gyp-next/compare/v0.9.5...v0.9.6) (2021-08-23)


### Bug Fixes

* align flake8 test ([#122](https://www.github.com/nodejs/gyp-next/issues/122)) ([f1faa8d](https://www.github.com/nodejs/gyp-next/commit/f1faa8d3081e1a47e917ff910892f00dff16cf8a))
* **msvs:** fix paths again in action command arguments ([#121](https://www.github.com/nodejs/gyp-next/issues/121)) ([7159dfb](https://www.github.com/nodejs/gyp-next/commit/7159dfbc5758c9ec717e215f2c36daf482c846a1))

### [0.9.5](https://www.github.com/nodejs/gyp-next/compare/v0.9.4...v0.9.5) (2021-08-18)


### Bug Fixes

* add python 3.6 to node-gyp integration test ([3462d4c](https://www.github.com/nodejs/gyp-next/commit/3462d4ce3c31cce747513dc7ca9760c81d57c60e))
* revert for windows compatibility ([d078e7d](https://www.github.com/nodejs/gyp-next/commit/d078e7d7ae080ddae243188f6415f940376a7368))
* support msvs_quote_cmd in ninja generator ([#117](https://www.github.com/nodejs/gyp-next/issues/117)) ([46486ac](https://www.github.com/nodejs/gyp-next/commit/46486ac6e9329529d51061e006a5b39631e46729))

### [0.9.4](https://www.github.com/nodejs/gyp-next/compare/v0.9.3...v0.9.4) (2021-08-09)


### Bug Fixes

* .S is an extension for asm file on Windows ([#115](https://www.github.com/nodejs/gyp-next/issues/115)) ([d2fad44](https://www.github.com/nodejs/gyp-next/commit/d2fad44ef3a79ca8900f1307060153ded57053fc))

### [0.9.3](https://www.github.com/nodejs/gyp-next/compare/v0.9.2...v0.9.3) (2021-07-07)


### Bug Fixes

* build failure with ninja and Python 3 on Windows ([#113](https://www.github.com/nodejs/gyp-next/issues/113)) ([c172d10](https://www.github.com/nodejs/gyp-next/commit/c172d105deff5db4244e583942215918fa80dd3c))

### [0.9.2](https://www.github.com/nodejs/gyp-next/compare/v0.9.1...v0.9.2) (2021-05-21)


### Bug Fixes

* add support of utf8 encoding ([#105](https://www.github.com/nodejs/gyp-next/issues/105)) ([4d0f93c](https://www.github.com/nodejs/gyp-next/commit/4d0f93c249286d1f0c0f665f5fe7346119f98cf1))

### [0.9.1](https://www.github.com/nodejs/gyp-next/compare/v0.9.0...v0.9.1) (2021-05-14)


### Bug Fixes

* py lint ([3b6a8ee](https://www.github.com/nodejs/gyp-next/commit/3b6a8ee7a66193a8a6867eba9e1d2b70bdf04402))

## [0.9.0](https://www.github.com/nodejs/gyp-next/compare/v0.8.1...v0.9.0) (2021-05-13)


### Features

* use LDFLAGS_host for host toolset ([#98](https://www.github.com/nodejs/gyp-next/issues/98)) ([bea5c7b](https://www.github.com/nodejs/gyp-next/commit/bea5c7bd67d6ad32acbdce79767a5481c70675a2))


### Bug Fixes

* msvs.py: remove overindentation ([#102](https://www.github.com/nodejs/gyp-next/issues/102)) ([3f83e99](https://www.github.com/nodejs/gyp-next/commit/3f83e99056d004d9579ceb786e06b624ddc36529))
* update gyp.el to change case to cl-case ([#93](https://www.github.com/nodejs/gyp-next/issues/93)) ([13d5b66](https://www.github.com/nodejs/gyp-next/commit/13d5b66aab35985af9c2fb1174fdc6e1c1407ecc))

### [0.8.1](https://www.github.com/nodejs/gyp-next/compare/v0.8.0...v0.8.1) (2021-02-18)


### Bug Fixes

* update shebang lines from python to python3 ([#94](https://www.github.com/nodejs/gyp-next/issues/94)) ([a1b0d41](https://www.github.com/nodejs/gyp-next/commit/a1b0d4171a8049a4ab7a614202063dec332f2df4))

## [0.8.0](https://www.github.com/nodejs/gyp-next/compare/v0.7.0...v0.8.0) (2021-01-15)


### ⚠ BREAKING CHANGES

* remove support for Python 2

### Bug Fixes

* revert posix build job ([#86](https://www.github.com/nodejs/gyp-next/issues/86)) ([39dc34f](https://www.github.com/nodejs/gyp-next/commit/39dc34f0799c074624005fb9bbccf6e028607f9d))


### gyp

* Remove support for Python 2 ([#88](https://www.github.com/nodejs/gyp-next/issues/88)) ([22e4654](https://www.github.com/nodejs/gyp-next/commit/22e465426fd892403c95534229af819a99c3f8dc))

## [0.7.0](https://www.github.com/nodejs/gyp-next/compare/v0.6.2...v0.7.0) (2020-12-17)


### ⚠ BREAKING CHANGES

* **msvs:** On Windows, arguments passed to the "action" commands are no longer transformed to replace slashes with backslashes.

### Features

* **xcode:** --cross-compiling overrides arch-specific settings ([973bae0](https://www.github.com/nodejs/gyp-next/commit/973bae0b7b08be7b680ecae9565fbd04b3e0787d))


### Bug Fixes

* **msvs:** do not fix paths in action command arguments ([fc22f83](https://www.github.com/nodejs/gyp-next/commit/fc22f8335e2016da4aae4f4233074bd651d2faea))
* cmake on python 3 ([fd61f5f](https://www.github.com/nodejs/gyp-next/commit/fd61f5faa5275ec8fc98e3c7868c0dd46f109540))
* ValueError: invalid mode: 'rU' while trying to load binding.gyp ([d0504e6](https://www.github.com/nodejs/gyp-next/commit/d0504e6700ce48f44957a4d5891b142a60be946f))
* xcode cmake parsing ([eefe8d1](https://www.github.com/nodejs/gyp-next/commit/eefe8d10e99863bc4ac7e2ed32facd608d400d4b))

### [0.6.2](https://www.github.com/nodejs/gyp-next/compare/v0.6.1...v0.6.2) (2020-10-16)


### Bug Fixes

* do not rewrite absolute paths to avoid long paths ([#74](https://www.github.com/nodejs/gyp-next/issues/74)) ([c2ccc1a](https://www.github.com/nodejs/gyp-next/commit/c2ccc1a81f7f94433a94f4d01a2e820db4c4331a))
* only include MARMASM when toolset is target ([5a2794a](https://www.github.com/nodejs/gyp-next/commit/5a2794aefb58f0c00404ff042b61740bc8b8d5cd))

### [0.6.1](https://github.com/nodejs/gyp-next/compare/v0.6.0...v0.6.1) (2020-10-14)


### Bug Fixes

* Correctly rename object files for absolute paths in MSVS generator.

## [0.6.0](https://github.com/nodejs/gyp-next/compare/v0.5.0...v0.6.0) (2020-10-13)


### Features

* The Makefile generator will now output shared libraries directly to the product directory on all platforms (previously only macOS).

## [0.5.0](https://github.com/nodejs/gyp-next/compare/v0.4.0...v0.5.0) (2020-09-30)


### Features

* Extended compile_commands_json generator to consider more file extensions than just `c` and `cc`. `cpp` and `cxx` are now supported.
* Source files with duplicate basenames are now supported.

### Removed

* The `--no-duplicate-basename-check` option was removed.
* The `msvs_enable_marmasm` configuration option was removed in favor of auto-inclusion of the "marmasm" sections for Windows on ARM.

## [0.4.0](https://github.com/nodejs/gyp-next/compare/v0.3.0...v0.4.0) (2020-07-14)


### Features

* Added support for passing arbitrary architectures to Xcode builds, enables `arm64` builds.

### Bug Fixes

* Fixed a bug on Solaris where copying archives failed.

## [0.3.0](https://github.com/nodejs/gyp-next/compare/v0.2.1...v0.3.0) (2020-06-06)


### Features

* Added support for MSVC cross-compilation. This allows compilation on x64 for a Windows ARM target.

### Bug Fixes

* Fixed XCode CLT version detection on macOS Catalina.

### [0.2.1](https://github.com/nodejs/gyp-next/compare/v0.2.0...v0.2.1) (2020-05-05)


### Bug Fixes

* Relicensed to Node.js contributors.
* Fixed Windows bug introduced in v0.2.0.

## [0.2.0](https://github.com/nodejs/gyp-next/releases/tag/v0.2.0) (2020-04-06)

This is the first release of this project, based on https://chromium.googlesource.com/external/gyp with changes made over the years in Node.js and node-gyp.

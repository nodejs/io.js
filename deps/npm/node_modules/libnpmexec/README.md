# libnpmexec

[![npm version](https://img.shields.io/npm/v/libnpmexec.svg)](https://npm.im/libnpmexec)
[![license](https://img.shields.io/npm/l/libnpmexec.svg)](https://npm.im/libnpmexec)
[![CI - libnpmexec](https://github.com/npm/cli/actions/workflows/ci-libnpmexec.yml/badge.svg)](https://github.com/npm/cli/actions/workflows/ci-libnpmexec.yml)

The `npm exec` (`npx`) Programmatic API

## Install

`npm install libnpmexec`

## Usage:

```js
const libexec = require('libnpmexec')
await libexec({
  args: ['yosay', 'Bom dia!'],
  cache: '~/.npm/_cacache',
  npxCache: '~/.npm/_npx',
  yes: true,
})
```

## API:

### `libexec(opts)`

- `opts`:
  - `args`: List of pkgs to execute **Array<String>**, defaults to `[]`
  - `call`: An alternative command to run when using `packages` option **String**, defaults to empty string.
  - `cache`: The path location to where the npm cache folder is placed **String**
  - `npxCache`: The path location to where the npx cache folder is placed **String**
  - `chalk`: Chalk instance to use for colors? **Required**
  - `localBin`: Location to the `node_modules/.bin` folder of the local project to start scanning for bin files **String**, defaults to `./node_modules/.bin`. **libexec** will walk up the directory structure looking for `node_modules/.bin` folders in parent folders that might satisfy the current `arg` and will use that bin if found.
  - `locationMsg`: Overrides "at location" message when entering interactive mode **String**
  - `globalBin`: Location to the global space bin folder, same as: `$(npm bin -g)` **String**, defaults to empty string.
  - `packages`: A list of packages to be used (possibly fetch from the registry) **Array<String>**, defaults to `[]`
  - `path`: Location to where to read local project info (`package.json`) **String**, defaults to `.`
  - `runPath`: Location to where to execute the script **String**, defaults to `.`
  - `scriptShell`: Default shell to be used **String**, defaults to `sh` on POSIX systems, `process.env.ComSpec` OR `cmd` on Windows
  - `yes`: Should skip download confirmation prompt when fetching missing packages from the registry? **Boolean**
  - `registry`, `cache`, and more options that are forwarded to [@npmcli/arborist](https://github.com/npm/arborist/) and [pacote](https://github.com/npm/pacote/#options) **Object**

## LICENSE

[ISC](./LICENSE)

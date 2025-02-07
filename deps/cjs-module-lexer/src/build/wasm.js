'use strict'

const WASM_BUILDER_CONTAINER = 'ghcr.io/nodejs/wasm-builder@sha256:975f391d907e42a75b8c72eb77c782181e941608687d4d8694c3e9df415a0970' // v0.0.9

const WASM_OPT = './wasm-opt'

const { execSync } = require('node:child_process')
const { writeFileSync, readFileSync, existsSync, mkdirSync } = require('node:fs')
const { join, resolve } = require('node:path')

const ROOT = resolve(__dirname, '../')

let platform = process.env.WASM_PLATFORM
if (!platform && process.argv[2]) {
  platform = execSync('docker info -f "{{.OSType}}/{{.Architecture}}"').toString().trim()
}

if (process.argv[2] === '--docker') {
  let cmd = `docker run --rm --platform=${platform.toString().trim()} `
  if (process.platform === 'linux') {
    cmd += ` --user ${process.getuid()}:${process.getegid()}`
  }

  if (!existsSync(`${ROOT}/dist`)){
    mkdirSync(`${ROOT}/dist`);
  }

  cmd += ` --mount type=bind,source=${ROOT}/lib,target=/home/node/build/lib \
           --mount type=bind,source=${ROOT}/src,target=/home/node/build/src \
           --mount type=bind,source=${ROOT}/dist,target=/home/node/build/dist \
           --mount type=bind,source=${ROOT}/node_modules,target=/home/node/build/node_modules \
           --mount type=bind,source=${ROOT}/build/wasm.js,target=/home/node/build/wasm.js \
           --mount type=bind,source=${ROOT}/build/Makefile,target=/home/node/build/Makefile \
           --mount type=bind,source=${ROOT}/build.js,target=/home/node/build/build.js \
           --mount type=bind,source=${ROOT}/package.json,target=/home/node/build/package.json \
           --mount type=bind,source=${ROOT}/include-wasm,target=/home/node/build/include-wasm \
           -t ${WASM_BUILDER_CONTAINER} node wasm.js`
  console.log(`> ${cmd}\n\n`)
  execSync(cmd, { stdio: 'inherit' })
  process.exit(0)
}

const hasOptimizer = (function () {
  try { execSync(`${WASM_OPT} --version`); return true } catch (error) { return false }
})()

// Build wasm binary
console.log('Building wasm');
execSync(`make lib/lexer.wasm`, { stdio: 'inherit' })
if (hasOptimizer) {
  console.log('Optimizing wasm');
  execSync(`make optimize`, { stdio: 'inherit' })
}
execSync(`node build.js`, { stdio: 'inherit' })

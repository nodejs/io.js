'use strict'

const WASM_BUILDER_CONTAINER = 'ghcr.io/nodejs/wasm-builder@sha256:975f391d907e42a75b8c72eb77c782181e941608687d4d8694c3e9df415a0970' // v0.0.9

const { execSync } = require('node:child_process')
const { writeFileSync, readFileSync } = require('node:fs')
const { join, resolve } = require('node:path')

const ROOT = resolve(__dirname, '../')
const WASM_SRC = resolve(__dirname, '../deps/llhttp')
const WASM_OUT = resolve(__dirname, '../lib/llhttp')

// These are defined by build environment
const WASM_CC = process.env.WASM_CC || 'clang'
let WASM_CFLAGS = process.env.WASM_CFLAGS || '--sysroot=/usr/share/wasi-sysroot -target wasm32-unknown-wasi'
let WASM_LDFLAGS = process.env.WASM_LDFLAGS || ''
const WASM_LDLIBS = process.env.WASM_LDLIBS || ''
const WASM_OPT = process.env.WASM_OPT || './wasm-opt'

// For compatibility with Node.js' `configure --shared-builtin-undici/undici-path ...`
const EXTERNAL_PATH = process.env.EXTERNAL_PATH

// These are relevant for undici and should not be overridden
WASM_CFLAGS += ' -Ofast -fno-exceptions -fvisibility=hidden -mexec-model=reactor'
WASM_LDFLAGS += ' -Wl,-error-limit=0 -Wl,-O3 -Wl,--lto-O3 -Wl,--strip-all'
WASM_LDFLAGS += ' -Wl,--allow-undefined -Wl,--export-dynamic -Wl,--export-table'
WASM_LDFLAGS += ' -Wl,--export=malloc -Wl,--export=free -Wl,--no-entry'

const WASM_OPT_FLAGS = '-O4 --converge --strip-debug --strip-dwarf --strip-producers'

const writeWasmChunk = (path, dest) => {
  const base64 = readFileSync(join(WASM_OUT, path)).toString('base64')
  writeFileSync(join(WASM_OUT, dest), `'use strict'

const { Buffer } = require('node:buffer')

const wasmBase64 = '${base64}'

let wasmBuffer

Object.defineProperty(module, 'exports', {
  get: () => {
    return wasmBuffer
      ? wasmBuffer
      : (wasmBuffer = Buffer.from(wasmBase64, 'base64'))
  }
})
`)
}

let platform = process.env.WASM_PLATFORM
if (!platform && process.argv[2]) {
  platform = execSync('docker info -f "{{.OSType}}/{{.Architecture}}"').toString().trim()
}

if (process.argv[2] === '--docker') {
  let cmd = `docker run --rm --platform=${platform.toString().trim()} `
  if (process.platform === 'linux') {
    cmd += ` --user ${process.getuid()}:${process.getegid()}`
  }

  cmd += ` --mount type=bind,source=${ROOT}/lib/llhttp,target=/home/node/build/lib/llhttp \
           --mount type=bind,source=${ROOT}/build,target=/home/node/build/build \
           --mount type=bind,source=${ROOT}/deps,target=/home/node/build/deps \
           -t ${WASM_BUILDER_CONTAINER} node build/wasm.js`
  console.log(`> ${cmd}\n\n`)
  execSync(cmd, { stdio: 'inherit' })
  process.exit(0)
}

const hasApk = (function () {
  try { execSync('command -v apk'); return true } catch (error) { return false }
})()
const hasOptimizer = (function () {
  try { execSync(`${WASM_OPT} --version`); return true } catch (error) { return false }
})()
if (hasApk) {
  // Gather information about the tools used for the build
  const buildInfo = execSync('apk info -v').toString()
  if (!buildInfo.includes('wasi-sdk')) {
    console.log('Failed to generate build environment information')
    process.exit(-1)
  }
  console.log(buildInfo)
}

// Build wasm binary
execSync(`${WASM_CC} ${WASM_CFLAGS} ${WASM_LDFLAGS} \
${join(WASM_SRC, 'src')}/*.c \
-I${join(WASM_SRC, 'include')} \
-o ${join(WASM_OUT, 'llhttp.wasm')} \
${WASM_LDLIBS}`, { stdio: 'inherit' })

if (hasOptimizer) {
  execSync(`${WASM_OPT} ${WASM_OPT_FLAGS} -o ${join(WASM_OUT, 'llhttp.wasm')} ${join(WASM_OUT, 'llhttp.wasm')}`, { stdio: 'inherit' })
}
writeWasmChunk('llhttp.wasm', 'llhttp-wasm.js')

// Build wasm simd binary
execSync(`${WASM_CC} ${WASM_CFLAGS} -msimd128 ${WASM_LDFLAGS} \
${join(WASM_SRC, 'src')}/*.c \
-I${join(WASM_SRC, 'include')} \
-o ${join(WASM_OUT, 'llhttp_simd.wasm')} \
${WASM_LDLIBS}`, { stdio: 'inherit' })

if (hasOptimizer) {
  execSync(`${WASM_OPT} ${WASM_OPT_FLAGS} --enable-simd -o ${join(WASM_OUT, 'llhttp_simd.wasm')} ${join(WASM_OUT, 'llhttp_simd.wasm')}`, { stdio: 'inherit' })
}
writeWasmChunk('llhttp_simd.wasm', 'llhttp_simd-wasm.js')

// For compatibility with Node.js' `configure --shared-builtin-undici/undici-path ...`
if (EXTERNAL_PATH) {
  writeFileSync(join(ROOT, 'loader.js'), `
'use strict'
globalThis.__UNDICI_IS_NODE__ = true
module.exports = require('node:module').createRequire('${EXTERNAL_PATH}/loader.js')('./index-fetch.js')
delete globalThis.__UNDICI_IS_NODE__
`)
}

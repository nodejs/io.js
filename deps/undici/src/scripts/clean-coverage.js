'use strict'

const { rmSync } = require('node:fs')
const { resolve } = require('node:path')

if (process.env.NODE_V8_COVERAGE) {
  if (process.env.NODE_V8_COVERAGE.endsWith('/tmp')) {
    rmSync(resolve(__dirname, process.env.NODE_V8_COVERAGE, '..'), { recursive: true, force: true })
  } else {
    rmSync(resolve(__dirname, process.env.NODE_V8_COVERAGE), { recursive: true, force: true })
  }
} else {
  console.log(resolve(__dirname, 'coverage'))
  rmSync(resolve(__dirname, '../coverage'), { recursive: true, force: true })
}

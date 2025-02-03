'use strict'

const { platform } = require('node:os')
const { writeFileSync } = require('node:fs')
const { resolve } = require('node:path')

if (platform() === 'win32') {
  writeFileSync(
    resolve(__dirname, '.npmrc'),
    'script-shell = "C:\\windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe"\n'
  )
}

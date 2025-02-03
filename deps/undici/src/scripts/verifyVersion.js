'use strict'

/* istanbul ignore file */

const [major, minor, patch] = process.versions.node.split('.').map(v => Number(v))
const required = process.argv.pop().split('.').map(v => Number(v))

const badMajor = major < required[0]
const badMinor = major === required[0] && minor < required[1]
const badPatch = major === required[0] && minor === required[1] && patch < required[2]

if (badMajor || badMinor || badPatch) {
  console.log(`Required Node.js >=${required.join('.')}, got ${process.versions.node}`)
  console.log('Skipping')
} else {
  process.exit(1)
}

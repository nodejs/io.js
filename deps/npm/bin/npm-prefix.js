#!/usr/bin/env node
// This is a single-use bin to help windows discover the proper prefix for npm
// without having to load all of npm first
// It does not accept argv params

const path = require('node:path')
const Config = require('@npmcli/config')
const { definitions, flatten, shorthands } = require('@npmcli/config/lib/definitions')
const config = new Config({
  npmPath: path.dirname(__dirname),
  // argv is explicitly not looked at since prefix is not something that can be changed via argv
  argv: [],
  definitions,
  flatten,
  shorthands,
  excludeNpmCwd: false,
})

async function main () {
  try {
    await config.load()
    // eslint-disable-next-line no-console
    console.log(config.globalPrefix)
  } catch (err) {
    // eslint-disable-next-line no-console
    console.error(err)
    process.exit(1)
  }
}
main()

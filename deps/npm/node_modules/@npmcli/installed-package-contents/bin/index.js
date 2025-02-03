#! /usr/bin/env node

const { relative } = require('path')
const pkgContents = require('../')

const usage = `Usage:
  installed-package-contents <path> [-d<n> --depth=<n>]

Lists the files installed for a package specified by <path>.

Options:
  -d<n> --depth=<n>   Provide a numeric value ("Infinity" is allowed)
                      to specify how deep in the file tree to traverse.
                      Default=1
  -h --help           Show this usage information`

const options = {}

process.argv.slice(2).forEach(arg => {
  let match
  if ((match = arg.match(/^(?:--depth=|-d)([0-9]+|Infinity)/))) {
    options.depth = +match[1]
  } else if (arg === '-h' || arg === '--help') {
    console.log(usage)
    process.exit(0)
  } else {
    options.path = arg
  }
})

if (!options.path) {
  console.error('ERROR: no path provided')
  console.error(usage)
  process.exit(1)
}

const cwd = process.cwd()

pkgContents(options)
  .then(list => list.sort().forEach(p => console.log(relative(cwd, p))))
  .catch(/* istanbul ignore next - pretty unusual */ er => {
    console.error(er)
    process.exit(1)
  })

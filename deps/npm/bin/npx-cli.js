#!/usr/bin/env node

const cli = require('../lib/cli.js')

// run the resulting command as `npm exec ...args`
process.argv[1] = require.resolve('./npm-cli.js')
process.argv.splice(2, 0, 'exec')

// TODO: remove the affordances for removed items in npm v9
const removedSwitches = new Set([
  'always-spawn',
  'ignore-existing',
  'shell-auto-fallback',
])

const removedOpts = new Set([
  'npm',
  'node-arg',
  'n',
])

const removed = new Set([
  ...removedSwitches,
  ...removedOpts,
])

const { definitions, shorthands } = require('@npmcli/config/lib/definitions')
const npmSwitches = Object.entries(definitions)
  .filter(([, { type }]) => type === Boolean ||
    (Array.isArray(type) && type.includes(Boolean)))
  .map(([key]) => key)

// things that don't take a value
const switches = new Set([
  ...removedSwitches,
  ...npmSwitches,
  'no-install',
  'quiet',
  'q',
  'version',
  'v',
  'help',
  'h',
])

// things that do take a value
const opts = new Set([
  ...removedOpts,
  'package',
  'p',
  'cache',
  'userconfig',
  'call',
  'c',
  'shell',
  'npm',
  'node-arg',
  'n',
])

// break out of loop when we find a positional argument or --
// If we find a positional arg, we shove -- in front of it, and
// let the normal npm cli handle the rest.
let i
let sawRemovedFlags = false
for (i = 3; i < process.argv.length; i++) {
  const arg = process.argv[i]
  if (arg === '--') {
    break
  } else if (/^-/.test(arg)) {
    const [key, ...v] = arg.replace(/^-+/, '').split('=')

    switch (key) {
      case 'p':
        process.argv[i] = ['--package', ...v].join('=')
        break

      case 'shell':
        process.argv[i] = ['--script-shell', ...v].join('=')
        break

      case 'no-install':
        process.argv[i] = '--yes=false'
        break

      default:
        // resolve shorthands and run again
        if (shorthands[key] && !removed.has(key)) {
          const a = [...shorthands[key]]
          if (v.length) {
            a.push(v.join('='))
          }
          process.argv.splice(i, 1, ...a)
          i--
          continue
        }
        break
    }

    if (removed.has(key)) {
      // eslint-disable-next-line no-console
      console.error(`npx: the --${key} argument has been removed.`)
      sawRemovedFlags = true
      process.argv.splice(i, 1)
      i--
    }

    if (v.length === 0 && !switches.has(key) &&
        (opts.has(key) || !/^-/.test(process.argv[i + 1]))) {
      // value will be next argument, skip over it.
      if (removed.has(key)) {
        // also remove the value for the cut key.
        process.argv.splice(i + 1, 1)
      } else {
        i++
      }
    }
  } else {
    // found a positional arg, put -- in front of it, and we're done
    process.argv.splice(i, 0, '--')
    break
  }
}

if (sawRemovedFlags) {
  // eslint-disable-next-line no-console
  console.error('See `npm help exec` for more information')
}

cli(process)

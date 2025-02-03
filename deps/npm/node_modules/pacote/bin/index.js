#!/usr/bin/env node

const run = conf => {
  const pacote = require('../')
  switch (conf._[0]) {
    case 'resolve':
    case 'manifest':
    case 'packument':
      if (conf._[0] === 'resolve' && conf.long) {
        return pacote.manifest(conf._[1], conf).then(mani => ({
          resolved: mani._resolved,
          integrity: mani._integrity,
          from: mani._from,
        }))
      }
      return pacote[conf._[0]](conf._[1], conf)

    case 'tarball':
      if (!conf._[2] || conf._[2] === '-') {
        return pacote.tarball.stream(conf._[1], stream => {
          stream.pipe(
            conf.testStdout ||
            /* istanbul ignore next */
            process.stdout
          )
          // make sure it resolves something falsey
          return stream.promise().then(() => {
            return false
          })
        }, conf)
      } else {
        return pacote.tarball.file(conf._[1], conf._[2], conf)
      }

    case 'extract':
      return pacote.extract(conf._[1], conf._[2], conf)

    default: /* istanbul ignore next */ {
      throw new Error(`bad command: ${conf._[0]}`)
    }
  }
}

const version = require('../package.json').version
const usage = () =>
`Pacote - The JavaScript Package Handler, v${version}

Usage:

  pacote resolve <spec>
    Resolve a specifier and output the fully resolved target
    Returns integrity and from if '--long' flag is set.

  pacote manifest <spec>
    Fetch a manifest and print to stdout

  pacote packument <spec>
    Fetch a full packument and print to stdout

  pacote tarball <spec> [<filename>]
    Fetch a package tarball and save to <filename>
    If <filename> is missing or '-', the tarball will be streamed to stdout.

  pacote extract <spec> <folder>
    Extract a package to the destination folder.

Configuration values all match the names of configs passed to npm, or
options passed to Pacote.  Additional flags for this executable:

  --long     Print an object from 'resolve', including integrity and spec.
  --json     Print result objects as JSON rather than node's default.
             (This is the default if stdout is not a TTY.)
  --help -h  Print this helpful text.

For example '--cache=/path/to/folder' will use that folder as the cache.
`

const shouldJSON = (conf, result) =>
  conf.json ||
  !process.stdout.isTTY &&
  conf.json === undefined &&
  result &&
  typeof result === 'object'

const pretty = (conf, result) =>
  shouldJSON(conf, result) ? JSON.stringify(result, 0, 2) : result

let addedLogListener = false
const main = args => {
  const conf = parse(args)
  if (conf.help || conf.h) {
    return console.log(usage())
  }

  if (!addedLogListener) {
    process.on('log', console.error)
    addedLogListener = true
  }

  try {
    return run(conf)
      .then(result => result && console.log(pretty(conf, result)))
      .catch(er => {
        console.error(er)
        process.exit(1)
      })
  } catch (er) {
    console.error(er.message)
    console.error(usage())
  }
}

const parseArg = arg => {
  const split = arg.slice(2).split('=')
  const k = split.shift()
  const v = split.join('=')
  const no = /^no-/.test(k) && !v
  const key = (no ? k.slice(3) : k)
    .replace(/^tag$/, 'defaultTag')
    .replace(/-([a-z])/g, (_, c) => c.toUpperCase())
  const value = v ? v.replace(/^~/, process.env.HOME) : !no
  return { key, value }
}

const parse = args => {
  const conf = {
    _: [],
    cache: process.env.HOME + '/.npm/_cacache',
  }
  let dashdash = false
  args.forEach(arg => {
    if (dashdash) {
      conf._.push(arg)
    } else if (arg === '--') {
      dashdash = true
    } else if (arg === '-h') {
      conf.help = true
    } else if (/^--/.test(arg)) {
      const { key, value } = parseArg(arg)
      conf[key] = value
    } else {
      conf._.push(arg)
    }
  })
  return conf
}

if (module === require.main) {
  main(process.argv.slice(2))
} else {
  module.exports = {
    main,
    run,
    usage,
    parseArg,
    parse,
  }
}

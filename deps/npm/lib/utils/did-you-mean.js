const Npm = require('../npm')
const { distance } = require('fastest-levenshtein')
const { commands } = require('./cmd-list.js')

const runScripts = ['stop', 'start', 'test', 'restart']

const isClose = (scmd, cmd) => distance(scmd, cmd) < scmd.length * 0.4

const didYouMean = (pkg, scmd) => {
  const { scripts = {}, bin = {} } = pkg || {}

  const best = [
    ...commands
      .filter(cmd => isClose(scmd, cmd) && scmd !== cmd)
      .map(str => [str, Npm.cmd(str).description]),
    ...Object.keys(scripts)
      // We would already be suggesting this in `npm x` so omit them here
      .filter(cmd => isClose(scmd, cmd) && !runScripts.includes(cmd))
      .map(str => [`run ${str}`, `run the "${str}" package script`]),
    ...Object.keys(bin)
      .filter(cmd => isClose(scmd, cmd))
      /* eslint-disable-next-line max-len */
      .map(str => [`exec ${str}`, `run the "${str}" command from either this or a remote npm package`]),
  ]

  if (best.length === 0) {
    return ''
  }

  return `\n\nDid you mean ${best.length === 1 ? 'this' : 'one of these'}?\n` +
    best.slice(0, 3).map(([msg, comment]) => `  npm ${msg} # ${comment}`).join('\n')
}

module.exports = didYouMean

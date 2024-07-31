'use strict'

async function rebuild (gyp, argv) {
  gyp.todo.push(
    { name: 'clean', args: [] }
    , { name: 'configure', args: argv }
    , { name: 'build', args: [] }
  )
}

module.exports = rebuild
module.exports.usage = 'Runs "clean", "configure" and "build" all at once'

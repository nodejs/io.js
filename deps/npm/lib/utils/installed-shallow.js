const { readdirScoped } = require('@npmcli/fs')

const installedShallow = async (npm, opts) => {
  const names = async global => {
    const paths = await readdirScoped(global ? npm.globalDir : npm.localDir)
    return paths.map(p => p.replace(/\\/g, '/'))
  }
  const { conf: { argv: { remain } } } = opts
  if (remain.length > 3) {
    return null
  }

  const { global } = npm.flatOptions
  const locals = global ? [] : await names(false)
  const globals = (await names(true)).map(n => global ? n : `${n} -g`)
  return [...locals, ...globals]
}

module.exports = installedShallow

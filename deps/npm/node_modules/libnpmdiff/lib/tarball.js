const { relative } = require('node:path')

const Arborist = require('@npmcli/arborist')
const npa = require('npm-package-arg')
const pkgContents = require('@npmcli/installed-package-contents')
const pacote = require('pacote')
const { tarCreateOptions } = pacote.DirFetcher
const tar = require('tar')

// returns a simplified tarball when reading files from node_modules folder,
// thus avoiding running the prepare scripts and the extra logic from packlist
const nodeModulesTarball = (manifest) =>
  pkgContents({ path: manifest._resolved, depth: 1 })
    .then(files =>
      files.map(file => relative(manifest._resolved, file))
    )
    .then(files =>
      tar.c(tarCreateOptions(manifest), files).concat()
    )

const tarball = (manifest, opts) => {
  const resolved = manifest._resolved
  const where = opts.where || process.cwd()

  const fromNodeModules = npa(resolved).type === 'directory'
    && /node_modules[\\/](@[^\\/]+\/)?[^\\/]+[\\/]?$/.test(relative(where, resolved))

  if (fromNodeModules) {
    return nodeModulesTarball(manifest, opts)
  }

  return pacote.tarball(manifest._resolved, {
    ...opts,
    Arborist,
  })
}

module.exports = tarball

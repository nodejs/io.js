const { get } = require('./fetcher.js')
const GitFetcher = require('./git.js')
const RegistryFetcher = require('./registry.js')
const FileFetcher = require('./file.js')
const DirFetcher = require('./dir.js')
const RemoteFetcher = require('./remote.js')

const tarball = (spec, opts) => get(spec, opts).tarball()
tarball.stream = (spec, handler, opts) => get(spec, opts).tarballStream(handler)
tarball.file = (spec, dest, opts) => get(spec, opts).tarballFile(dest)

module.exports = {
  GitFetcher,
  RegistryFetcher,
  FileFetcher,
  DirFetcher,
  RemoteFetcher,
  resolve: (spec, opts) => get(spec, opts).resolve(),
  extract: (spec, dest, opts) => get(spec, opts).extract(dest),
  manifest: (spec, opts) => get(spec, opts).manifest(),
  packument: (spec, opts) => get(spec, opts).packument(),
  tarball,
}

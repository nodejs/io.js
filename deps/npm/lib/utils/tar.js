const tar = require('tar')
const ssri = require('ssri')
const { log, output } = require('proc-log')
const formatBytes = require('./format-bytes.js')
const localeCompare = require('@isaacs/string-locale-compare')('en', {
  sensitivity: 'case',
  numeric: true,
})

const logTar = (tarball, { unicode = false, json, key } = {}) => {
  if (json) {
    output.buffer(key == null ? tarball : { [key]: tarball })
    return
  }
  log.notice('')
  log.notice('', `${unicode ? '📦 ' : 'package:'} ${tarball.name}@${tarball.version}`)
  log.notice('Tarball Contents')
  if (tarball.files.length) {
    log.notice(
      '',
      tarball.files.map(f =>
        /^node_modules\//.test(f.path) ? null : `${formatBytes(f.size, false)} ${f.path}`
      ).filter(f => f).join('\n')
    )
  }
  if (tarball.bundled.length) {
    log.notice('Bundled Dependencies')
    tarball.bundled.forEach(name => log.notice('', name))
  }
  log.notice('Tarball Details')
  log.notice('', `name: ${tarball.name}`)
  log.notice('', `version: ${tarball.version}`)
  if (tarball.filename) {
    log.notice('', `filename: ${tarball.filename}`)
  }
  log.notice('', `package size: ${formatBytes(tarball.size)}`)
  log.notice('', `unpacked size: ${formatBytes(tarball.unpackedSize)}`)
  log.notice('', `shasum: ${tarball.shasum}`)
  log.notice('', `integrity: ${tarball.integrity.toString().slice(0, 20)}[...]${tarball.integrity.toString().slice(80)}`)
  if (tarball.bundled.length) {
    log.notice('', `bundled deps: ${tarball.bundled.length}`)
    log.notice('', `bundled files: ${tarball.entryCount - tarball.files.length}`)
    log.notice('', `own files: ${tarball.files.length}`)
  }
  log.notice('', `total files: ${tarball.entryCount}`)
  log.notice('', '')
}

const getContents = async (manifest, tarball) => {
  const files = []
  const bundled = new Set()
  let totalEntries = 0
  let totalEntrySize = 0

  // reads contents of tarball
  const stream = tar.t({
    onentry (entry) {
      totalEntries++
      totalEntrySize += entry.size
      const p = entry.path
      if (p.startsWith('package/node_modules/') && p !== 'package/node_modules/') {
        const name = p.match(/^package\/node_modules\/((?:@[^/]+\/)?[^/]+)/)[1]
        bundled.add(name)
      }
      files.push({
        path: entry.path.replace(/^package\//, ''),
        size: entry.size,
        mode: entry.mode,
      })
    },
  })
  stream.end(tarball)

  const integrity = ssri.fromData(tarball, {
    algorithms: ['sha1', 'sha512'],
  })

  const comparator = ({ path: a }, { path: b }) => localeCompare(a, b)

  const isUpper = str => {
    const ch = str.charAt(0)
    return ch === ch.toUpperCase()
  }

  const uppers = files.filter(file => isUpper(file.path))
  const others = files.filter(file => !isUpper(file.path))

  uppers.sort(comparator)
  others.sort(comparator)

  const shasum = integrity.sha1[0].hexDigest()
  return {
    id: manifest._id || `${manifest.name}@${manifest.version}`,
    name: manifest.name,
    version: manifest.version,
    size: tarball.length,
    unpackedSize: totalEntrySize,
    shasum,
    integrity: ssri.parse(integrity.sha512[0]),
    // @scope/packagename.tgz => scope-packagename.tgz
    // we can safely use these global replace rules due to npm package naming rules
    filename: `${manifest.name.replace('@', '').replace('/', '-')}-${manifest.version}.tgz`,
    files: uppers.concat(others),
    entryCount: totalEntries,
    bundled: Array.from(bundled),
  }
}

module.exports = { logTar, getContents }

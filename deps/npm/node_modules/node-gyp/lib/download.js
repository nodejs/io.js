const fetch = require('make-fetch-happen')
const { promises: fs } = require('graceful-fs')
const log = require('./log')

async function download (gyp, url) {
  log.http('GET', url)

  const requestOpts = {
    headers: {
      'User-Agent': `node-gyp v${gyp.version} (node ${process.version})`,
      Connection: 'keep-alive'
    },
    proxy: gyp.opts.proxy,
    noProxy: gyp.opts.noproxy
  }

  const cafile = gyp.opts.cafile
  if (cafile) {
    requestOpts.ca = await readCAFile(cafile)
  }

  const res = await fetch(url, requestOpts)
  log.http(res.status, res.url)

  return res
}

async function readCAFile (filename) {
  // The CA file can contain multiple certificates so split on certificate
  // boundaries.  [\S\s]*? is used to match everything including newlines.
  const ca = await fs.readFile(filename, 'utf8')
  const re = /(-----BEGIN CERTIFICATE-----[\S\s]*?-----END CERTIFICATE-----)/g
  return ca.match(re)
}

module.exports = {
  download,
  readCAFile
}

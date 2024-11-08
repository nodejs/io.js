'use strict'

const {
  mkdir,
  readFile,
  rm,
  stat,
  truncate,
  writeFile,
} = require('fs/promises')
const pMap = require('p-map')
const contentPath = require('./content/path')
const fsm = require('fs-minipass')
const glob = require('./util/glob.js')
const index = require('./entry-index')
const path = require('path')
const ssri = require('ssri')

const hasOwnProperty = (obj, key) =>
  Object.prototype.hasOwnProperty.call(obj, key)

const verifyOpts = (opts) => ({
  concurrency: 20,
  log: { silly () {} },
  ...opts,
})

module.exports = verify

async function verify (cache, opts) {
  opts = verifyOpts(opts)
  opts.log.silly('verify', 'verifying cache at', cache)

  const steps = [
    markStartTime,
    fixPerms,
    garbageCollect,
    rebuildIndex,
    cleanTmp,
    writeVerifile,
    markEndTime,
  ]

  const stats = {}
  for (const step of steps) {
    const label = step.name
    const start = new Date()
    const s = await step(cache, opts)
    if (s) {
      Object.keys(s).forEach((k) => {
        stats[k] = s[k]
      })
    }
    const end = new Date()
    if (!stats.runTime) {
      stats.runTime = {}
    }
    stats.runTime[label] = end - start
  }
  stats.runTime.total = stats.endTime - stats.startTime
  opts.log.silly(
    'verify',
    'verification finished for',
    cache,
    'in',
    `${stats.runTime.total}ms`
  )
  return stats
}

async function markStartTime () {
  return { startTime: new Date() }
}

async function markEndTime () {
  return { endTime: new Date() }
}

async function fixPerms (cache, opts) {
  opts.log.silly('verify', 'fixing cache permissions')
  await mkdir(cache, { recursive: true })
  return null
}

// Implements a naive mark-and-sweep tracing garbage collector.
//
// The algorithm is basically as follows:
// 1. Read (and filter) all index entries ("pointers")
// 2. Mark each integrity value as "live"
// 3. Read entire filesystem tree in `content-vX/` dir
// 4. If content is live, verify its checksum and delete it if it fails
// 5. If content is not marked as live, rm it.
//
async function garbageCollect (cache, opts) {
  opts.log.silly('verify', 'garbage collecting content')
  const indexStream = index.lsStream(cache)
  const liveContent = new Set()
  indexStream.on('data', (entry) => {
    if (opts.filter && !opts.filter(entry)) {
      return
    }

    // integrity is stringified, re-parse it so we can get each hash
    const integrity = ssri.parse(entry.integrity)
    for (const algo in integrity) {
      liveContent.add(integrity[algo].toString())
    }
  })
  await new Promise((resolve, reject) => {
    indexStream.on('end', resolve).on('error', reject)
  })
  const contentDir = contentPath.contentDir(cache)
  const files = await glob(path.join(contentDir, '**'), {
    follow: false,
    nodir: true,
    nosort: true,
  })
  const stats = {
    verifiedContent: 0,
    reclaimedCount: 0,
    reclaimedSize: 0,
    badContentCount: 0,
    keptSize: 0,
  }
  await pMap(
    files,
    async (f) => {
      const split = f.split(/[/\\]/)
      const digest = split.slice(split.length - 3).join('')
      const algo = split[split.length - 4]
      const integrity = ssri.fromHex(digest, algo)
      if (liveContent.has(integrity.toString())) {
        const info = await verifyContent(f, integrity)
        if (!info.valid) {
          stats.reclaimedCount++
          stats.badContentCount++
          stats.reclaimedSize += info.size
        } else {
          stats.verifiedContent++
          stats.keptSize += info.size
        }
      } else {
        // No entries refer to this content. We can delete.
        stats.reclaimedCount++
        const s = await stat(f)
        await rm(f, { recursive: true, force: true })
        stats.reclaimedSize += s.size
      }
      return stats
    },
    { concurrency: opts.concurrency }
  )
  return stats
}

async function verifyContent (filepath, sri) {
  const contentInfo = {}
  try {
    const { size } = await stat(filepath)
    contentInfo.size = size
    contentInfo.valid = true
    await ssri.checkStream(new fsm.ReadStream(filepath), sri)
  } catch (err) {
    if (err.code === 'ENOENT') {
      return { size: 0, valid: false }
    }
    if (err.code !== 'EINTEGRITY') {
      throw err
    }

    await rm(filepath, { recursive: true, force: true })
    contentInfo.valid = false
  }
  return contentInfo
}

async function rebuildIndex (cache, opts) {
  opts.log.silly('verify', 'rebuilding index')
  const entries = await index.ls(cache)
  const stats = {
    missingContent: 0,
    rejectedEntries: 0,
    totalEntries: 0,
  }
  const buckets = {}
  for (const k in entries) {
    /* istanbul ignore else */
    if (hasOwnProperty(entries, k)) {
      const hashed = index.hashKey(k)
      const entry = entries[k]
      const excluded = opts.filter && !opts.filter(entry)
      excluded && stats.rejectedEntries++
      if (buckets[hashed] && !excluded) {
        buckets[hashed].push(entry)
      } else if (buckets[hashed] && excluded) {
        // skip
      } else if (excluded) {
        buckets[hashed] = []
        buckets[hashed]._path = index.bucketPath(cache, k)
      } else {
        buckets[hashed] = [entry]
        buckets[hashed]._path = index.bucketPath(cache, k)
      }
    }
  }
  await pMap(
    Object.keys(buckets),
    (key) => {
      return rebuildBucket(cache, buckets[key], stats, opts)
    },
    { concurrency: opts.concurrency }
  )
  return stats
}

async function rebuildBucket (cache, bucket, stats) {
  await truncate(bucket._path)
  // This needs to be serialized because cacache explicitly
  // lets very racy bucket conflicts clobber each other.
  for (const entry of bucket) {
    const content = contentPath(cache, entry.integrity)
    try {
      await stat(content)
      await index.insert(cache, entry.key, entry.integrity, {
        metadata: entry.metadata,
        size: entry.size,
        time: entry.time,
      })
      stats.totalEntries++
    } catch (err) {
      if (err.code === 'ENOENT') {
        stats.rejectedEntries++
        stats.missingContent++
      } else {
        throw err
      }
    }
  }
}

function cleanTmp (cache, opts) {
  opts.log.silly('verify', 'cleaning tmp directory')
  return rm(path.join(cache, 'tmp'), { recursive: true, force: true })
}

async function writeVerifile (cache, opts) {
  const verifile = path.join(cache, '_lastverified')
  opts.log.silly('verify', 'writing verifile to ' + verifile)
  return writeFile(verifile, `${Date.now()}`)
}

module.exports.lastRun = lastRun

async function lastRun (cache) {
  const data = await readFile(path.join(cache, '_lastverified'), { encoding: 'utf8' })
  return new Date(+data)
}

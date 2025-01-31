const jsDiff = require('diff')

const shouldPrintPatch = require('./should-print-patch.js')

const colors = {
  // red
  removed: { open: '\x1B[31m', close: '\x1B[39m' },
  // green
  added: { open: '\x1B[32m', close: '\x1B[39m' },
  // blue
  header: { open: '\x1B[34m', close: '\x1B[39m' },
  // cyan
  section: { open: '\x1B[36m', close: '\x1B[39m' },
}

const color = (colorStr, colorId) => {
  const { open, close } = colors[colorId]
  // avoid highlighting the "\n" (would highlight till the end of the line)
  return colorStr.replace(/[^\n\r]+/g, open + '$&' + close)
}

const formatDiff = async ({ files, opts = {}, refs, versions }) => {
  let res = ''
  const srcPrefix = opts.diffNoPrefix ? '' : opts.diffSrcPrefix || 'a/'
  const dstPrefix = opts.diffNoPrefix ? '' : opts.diffDstPrefix || 'b/'

  for (const filename of files.values()) {
    const names = {
      a: `${srcPrefix}${filename}`,
      b: `${dstPrefix}${filename}`,
    }

    let fileMode = ''
    const filenames = {
      a: refs.get(`a/${filename}`),
      b: refs.get(`b/${filename}`),
    }
    const contents = {
      a: filenames.a && filenames.a.content,
      b: filenames.b && filenames.b.content,
    }
    const modes = {
      a: filenames.a && filenames.a.mode,
      b: filenames.b && filenames.b.mode,
    }

    if (contents.a === contents.b && modes.a === modes.b) {
      continue
    }

    if (opts.diffNameOnly) {
      res += `${filename}\n`
      continue
    }

    let patch = ''
    let headerLength = 0
    const header = str => {
      headerLength++
      patch += `${str}\n`
    }

    // manually build a git diff-compatible header
    header(`diff --git ${names.a} ${names.b}`)
    if (modes.a === modes.b) {
      fileMode = filenames.a.mode
    } else {
      if (modes.a && !modes.b) {
        header(`deleted file mode ${modes.a}`)
      } else if (!modes.a && modes.b) {
        header(`new file mode ${modes.b}`)
      } else {
        header(`old mode ${modes.a}`)
        header(`new mode ${modes.b}`)
      }
    }
    /* eslint-disable-next-line max-len */
    header(`index ${opts.tagVersionPrefix || 'v'}${versions.a}..${opts.tagVersionPrefix || 'v'}${versions.b} ${fileMode}`)

    if (await shouldPrintPatch(filename)) {
      patch += jsDiff.createTwoFilesPatch(
        names.a,
        names.b,
        contents.a || '',
        contents.b || '',
        '',
        '',
        {
          context: opts.diffUnified === 0 ? 0 : opts.diffUnified || 3,
          ignoreWhitespace: opts.diffIgnoreAllSpace,
        }
      ).replace(
        '===================================================================\n',
        ''
      ).replace(/\t\n/g, '\n') // strip trailing tabs
      headerLength += 2
    } else {
      header(`--- ${names.a}`)
      header(`+++ ${names.b}`)
    }

    if (opts.color) {
      // this RegExp will include all the `\n` chars into the lines, easier to join
      const lines = patch.split(/^/m)
      res += color(lines.slice(0, headerLength).join(''), 'header')
      res += lines.slice(headerLength).join('')
        .replace(/^-.*/gm, color('$&', 'removed'))
        .replace(/^\+.*/gm, color('$&', 'added'))
        .replace(/^@@.+@@/gm, color('$&', 'section'))
    } else {
      res += patch
    }
  }

  return res.trim()
}

module.exports = formatDiff

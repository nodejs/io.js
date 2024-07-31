const { dirname, relative, join, resolve, basename } = require('path')
const linkGently = require('./link-gently.js')
const manTarget = require('./man-target.js')

const linkMans = async ({ path, pkg, top, force }) => {
  const target = manTarget({ path, top })
  if (!target || !Array.isArray(pkg?.man) || !pkg.man.length) {
    return []
  }

  const links = []
  // `new Set` to filter out duplicates
  for (let man of new Set(pkg.man)) {
    if (!man || typeof man !== 'string') {
      continue
    }
    // break any links to c:\\blah or /foo/blah or ../blah
    man = join('/', man).replace(/\\|:/g, '/').slice(1)
    const parseMan = man.match(/\.([0-9]+)(\.gz)?$/)
    if (!parseMan) {
      throw Object.assign(new Error('invalid man entry name\n' +
        'Man files must end with a number, ' +
        'and optionally a .gz suffix if they are compressed.'
      ), {
        code: 'EBADMAN',
        path,
        pkgid: pkg._id,
        man,
      })
    }

    const section = parseMan[1]
    const base = basename(man)
    const absFrom = resolve(path, man)
    /* istanbul ignore if - that unpossible */
    if (absFrom.indexOf(path) !== 0) {
      throw Object.assign(new Error('invalid man entry'), {
        code: 'EBADMAN',
        path,
        pkgid: pkg._id,
        man,
      })
    }

    const to = resolve(target, 'man' + section, base)
    const from = relative(dirname(to), absFrom)

    links.push(linkGently({ from, to, path, absFrom, force }))
  }
  return Promise.all(links)
}

module.exports = linkMans

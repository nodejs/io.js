const { readdir } = require('fs/promises')
const { join } = require('path')

const readdirScoped = async (dir) => {
  const results = []

  for (const item of await readdir(dir)) {
    if (item.startsWith('@')) {
      for (const scopedItem of await readdir(join(dir, item))) {
        results.push(join(item, scopedItem))
      }
    } else {
      results.push(item)
    }
  }

  return results
}

module.exports = readdirScoped

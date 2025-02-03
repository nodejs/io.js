const Star = require('./star.js')

class Unstar extends Star {
  static description = 'Remove an item from your favorite packages'
  static name = 'unstar'
}

module.exports = Unstar

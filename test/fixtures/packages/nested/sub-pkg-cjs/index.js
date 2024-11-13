const { findPackageJSON } = require('node:module');

module.exports = findPackageJSON('..', __filename);

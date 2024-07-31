const { register } = require('node:module');
const { pathToFileURL } = require('node:url');

register('./hooks.mjs', pathToFileURL(__filename));

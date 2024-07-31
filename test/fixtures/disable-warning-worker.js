'use strict';
const path = require('node:path');
const { Worker } = require('node:worker_threads');
new Worker(path.join(__dirname, './disable-warning.js'));

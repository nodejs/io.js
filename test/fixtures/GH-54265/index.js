// index.js
const Module = require("module");
const requireDep2 = require("./dep1.js");

const globalCache = Module._cache;
Module._cache = Object.create(null);
require("./require-hook.js");
Module._cache = globalCache;

requireDep2();

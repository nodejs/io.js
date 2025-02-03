// require-hook.js
const Module = require("module");
const requireDep2 = require("./dep1.js");

const originalJSLoader = Module._extensions[".js"];
Module._extensions[".js"] = function customJSLoader(module, filename) {
  requireDep2();
  return originalJSLoader(module, filename);
};

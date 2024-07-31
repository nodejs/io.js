import { createRequire } from "node:module";
const require = createRequire(import.meta.url);
const exports = require('./required-cjs');
console.log(exports.hello);
export default exports;

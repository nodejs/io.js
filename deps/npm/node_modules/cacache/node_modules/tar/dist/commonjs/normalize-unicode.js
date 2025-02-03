"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.normalizeUnicode = void 0;
// warning: extremely hot code path.
// This has been meticulously optimized for use
// within npm install on large package trees.
// Do not edit without careful benchmarking.
const normalizeCache = Object.create(null);
const { hasOwnProperty } = Object.prototype;
const normalizeUnicode = (s) => {
    if (!hasOwnProperty.call(normalizeCache, s)) {
        normalizeCache[s] = s.normalize('NFD');
    }
    return normalizeCache[s];
};
exports.normalizeUnicode = normalizeUnicode;
//# sourceMappingURL=normalize-unicode.js.map
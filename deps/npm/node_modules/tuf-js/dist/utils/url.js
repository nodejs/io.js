"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.join = join;
const url_1 = require("url");
function join(base, path) {
    return new url_1.URL(ensureTrailingSlash(base) + removeLeadingSlash(path)).toString();
}
function ensureTrailingSlash(path) {
    return path.endsWith('/') ? path : path + '/';
}
function removeLeadingSlash(path) {
    return path.startsWith('/') ? path.slice(1) : path;
}

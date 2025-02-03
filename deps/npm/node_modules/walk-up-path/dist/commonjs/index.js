"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.walkUp = void 0;
const path_1 = require("path");
const walkUp = function* (path) {
    for (path = (0, path_1.resolve)(path); path;) {
        yield path;
        const pp = (0, path_1.dirname)(path);
        if (pp === path) {
            break;
        }
        else {
            path = pp;
        }
    }
};
exports.walkUp = walkUp;
//# sourceMappingURL=index.js.map
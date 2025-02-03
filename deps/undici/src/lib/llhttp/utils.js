"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.enumToMap = void 0;
function enumToMap(obj, filter = [], exceptions = []) {
    var _a, _b;
    const emptyFilter = ((_a = filter === null || filter === void 0 ? void 0 : filter.length) !== null && _a !== void 0 ? _a : 0) === 0;
    const emptyExceptions = ((_b = exceptions === null || exceptions === void 0 ? void 0 : exceptions.length) !== null && _b !== void 0 ? _b : 0) === 0;
    return Object.fromEntries(Object.entries(obj).filter(([, value]) => {
        return (typeof value === 'number' &&
            (emptyFilter || filter.includes(value)) &&
            (emptyExceptions || !exceptions.includes(value)));
    }));
}
exports.enumToMap = enumToMap;
//# sourceMappingURL=utils.js.map
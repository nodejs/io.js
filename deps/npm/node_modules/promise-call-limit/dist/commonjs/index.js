"use strict";
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || function (mod) {
    if (mod && mod.__esModule) return mod;
    var result = {};
    if (mod != null) for (var k in mod) if (k !== "default" && Object.prototype.hasOwnProperty.call(mod, k)) __createBinding(result, mod, k);
    __setModuleDefault(result, mod);
    return result;
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.callLimit = void 0;
const os = __importStar(require("node:os"));
// availableParallelism available only since node v19, for older versions use
// cpus() cpus() can return an empty list if /proc is not mounted, use 1 in
// this case
/* c8 ignore start */
const defLimit = 'availableParallelism' in os
    ? Math.max(1, os.availableParallelism() - 1)
    : Math.max(1, os.cpus().length - 1);
const callLimit = (queue, { limit = defLimit, rejectLate } = {}) => new Promise((res, rej) => {
    let active = 0;
    let current = 0;
    const results = [];
    // Whether or not we rejected, distinct from the rejection just in case the rejection itself is falsey
    let rejected = false;
    let rejection;
    const reject = (er) => {
        if (rejected)
            return;
        rejected = true;
        rejection ??= er;
        if (!rejectLate)
            rej(rejection);
    };
    let resolved = false;
    const resolve = () => {
        if (resolved || active > 0)
            return;
        resolved = true;
        res(results);
    };
    const run = () => {
        const c = current++;
        if (c >= queue.length)
            return rejected ? reject() : resolve();
        active++;
        const step = queue[c];
        /* c8 ignore start */
        if (!step)
            throw new Error('walked off queue');
        /* c8 ignore stop */
        results[c] = step()
            .then(result => {
            active--;
            results[c] = result;
            return result;
        }, er => {
            active--;
            reject(er);
        })
            .then(result => {
            if (rejected && active === 0)
                return rej(rejection);
            run();
            return result;
        });
    };
    for (let i = 0; i < limit; i++)
        run();
});
exports.callLimit = callLimit;
//# sourceMappingURL=index.js.map
"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.read = void 0;
const mute_stream_1 = __importDefault(require("mute-stream"));
const readline_1 = require("readline");
async function read({ default: def, input = process.stdin, output = process.stdout, completer, prompt = '', silent, timeout, edit, terminal, replace, }) {
    if (typeof def !== 'undefined' &&
        typeof def !== 'string' &&
        typeof def !== 'number') {
        throw new Error('default value must be string or number');
    }
    let editDef = false;
    const defString = def?.toString();
    prompt = prompt.trim() + ' ';
    terminal = !!(terminal || output.isTTY);
    if (defString) {
        if (silent) {
            prompt += '(<default hidden>) ';
            // TODO: add tests for edit
            /* c8 ignore start */
        }
        else if (edit) {
            editDef = true;
            /* c8 ignore stop */
        }
        else {
            prompt += '(' + defString + ') ';
        }
    }
    const m = new mute_stream_1.default({ replace, prompt });
    m.pipe(output, { end: false });
    output = m;
    return new Promise((resolve, reject) => {
        const rl = (0, readline_1.createInterface)({ input, output, terminal, completer });
        // TODO: add tests for timeout
        /* c8 ignore start */
        const timer = timeout && setTimeout(() => onError(new Error('timed out')), timeout);
        /* c8 ignore stop */
        m.unmute();
        rl.setPrompt(prompt);
        rl.prompt();
        if (silent) {
            m.mute();
            // TODO: add tests for edit + default
            /* c8 ignore start */
        }
        else if (editDef && defString) {
            const rlEdit = rl;
            rlEdit.line = defString;
            rlEdit.cursor = defString.length;
            rlEdit._refreshLine();
        }
        /* c8 ignore stop */
        const done = () => {
            rl.close();
            clearTimeout(timer);
            m.mute();
            m.end();
        };
        // TODO: add tests for rejecting
        /* c8 ignore start */
        const onError = (er) => {
            done();
            reject(er);
        };
        /* c8 ignore stop */
        rl.on('error', onError);
        rl.on('line', line => {
            // TODO: add tests for silent
            /* c8 ignore start */
            if (silent && terminal) {
                m.unmute();
            }
            /* c8 ignore stop */
            done();
            // TODO: add tests for default
            /* c8 ignore start */
            // truncate the \n at the end.
            return resolve(line.replace(/\r?\n?$/, '') || defString || '');
            /* c8 ignore stop */
        });
        // TODO: add tests for sigint
        /* c8 ignore start */
        rl.on('SIGINT', () => {
            rl.close();
            onError(new Error('canceled'));
        });
        /* c8 ignore stop */
    });
}
exports.read = read;
//# sourceMappingURL=read.js.map
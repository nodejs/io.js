"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.mkdirSync = exports.mkdir = void 0;
const chownr_1 = require("chownr");
const fs_1 = __importDefault(require("fs"));
const mkdirp_1 = require("mkdirp");
const node_path_1 = __importDefault(require("node:path"));
const cwd_error_js_1 = require("./cwd-error.js");
const normalize_windows_path_js_1 = require("./normalize-windows-path.js");
const symlink_error_js_1 = require("./symlink-error.js");
const cGet = (cache, key) => cache.get((0, normalize_windows_path_js_1.normalizeWindowsPath)(key));
const cSet = (cache, key, val) => cache.set((0, normalize_windows_path_js_1.normalizeWindowsPath)(key), val);
const checkCwd = (dir, cb) => {
    fs_1.default.stat(dir, (er, st) => {
        if (er || !st.isDirectory()) {
            er = new cwd_error_js_1.CwdError(dir, er?.code || 'ENOTDIR');
        }
        cb(er);
    });
};
/**
 * Wrapper around mkdirp for tar's needs.
 *
 * The main purpose is to avoid creating directories if we know that
 * they already exist (and track which ones exist for this purpose),
 * and prevent entries from being extracted into symlinked folders,
 * if `preservePaths` is not set.
 */
const mkdir = (dir, opt, cb) => {
    dir = (0, normalize_windows_path_js_1.normalizeWindowsPath)(dir);
    // if there's any overlap between mask and mode,
    // then we'll need an explicit chmod
    /* c8 ignore next */
    const umask = opt.umask ?? 0o22;
    const mode = opt.mode | 0o0700;
    const needChmod = (mode & umask) !== 0;
    const uid = opt.uid;
    const gid = opt.gid;
    const doChown = typeof uid === 'number' &&
        typeof gid === 'number' &&
        (uid !== opt.processUid || gid !== opt.processGid);
    const preserve = opt.preserve;
    const unlink = opt.unlink;
    const cache = opt.cache;
    const cwd = (0, normalize_windows_path_js_1.normalizeWindowsPath)(opt.cwd);
    const done = (er, created) => {
        if (er) {
            cb(er);
        }
        else {
            cSet(cache, dir, true);
            if (created && doChown) {
                (0, chownr_1.chownr)(created, uid, gid, er => done(er));
            }
            else if (needChmod) {
                fs_1.default.chmod(dir, mode, cb);
            }
            else {
                cb();
            }
        }
    };
    if (cache && cGet(cache, dir) === true) {
        return done();
    }
    if (dir === cwd) {
        return checkCwd(dir, done);
    }
    if (preserve) {
        return (0, mkdirp_1.mkdirp)(dir, { mode }).then(made => done(null, made ?? undefined), // oh, ts
        done);
    }
    const sub = (0, normalize_windows_path_js_1.normalizeWindowsPath)(node_path_1.default.relative(cwd, dir));
    const parts = sub.split('/');
    mkdir_(cwd, parts, mode, cache, unlink, cwd, undefined, done);
};
exports.mkdir = mkdir;
const mkdir_ = (base, parts, mode, cache, unlink, cwd, created, cb) => {
    if (!parts.length) {
        return cb(null, created);
    }
    const p = parts.shift();
    const part = (0, normalize_windows_path_js_1.normalizeWindowsPath)(node_path_1.default.resolve(base + '/' + p));
    if (cGet(cache, part)) {
        return mkdir_(part, parts, mode, cache, unlink, cwd, created, cb);
    }
    fs_1.default.mkdir(part, mode, onmkdir(part, parts, mode, cache, unlink, cwd, created, cb));
};
const onmkdir = (part, parts, mode, cache, unlink, cwd, created, cb) => (er) => {
    if (er) {
        fs_1.default.lstat(part, (statEr, st) => {
            if (statEr) {
                statEr.path =
                    statEr.path && (0, normalize_windows_path_js_1.normalizeWindowsPath)(statEr.path);
                cb(statEr);
            }
            else if (st.isDirectory()) {
                mkdir_(part, parts, mode, cache, unlink, cwd, created, cb);
            }
            else if (unlink) {
                fs_1.default.unlink(part, er => {
                    if (er) {
                        return cb(er);
                    }
                    fs_1.default.mkdir(part, mode, onmkdir(part, parts, mode, cache, unlink, cwd, created, cb));
                });
            }
            else if (st.isSymbolicLink()) {
                return cb(new symlink_error_js_1.SymlinkError(part, part + '/' + parts.join('/')));
            }
            else {
                cb(er);
            }
        });
    }
    else {
        created = created || part;
        mkdir_(part, parts, mode, cache, unlink, cwd, created, cb);
    }
};
const checkCwdSync = (dir) => {
    let ok = false;
    let code = undefined;
    try {
        ok = fs_1.default.statSync(dir).isDirectory();
    }
    catch (er) {
        code = er?.code;
    }
    finally {
        if (!ok) {
            throw new cwd_error_js_1.CwdError(dir, code ?? 'ENOTDIR');
        }
    }
};
const mkdirSync = (dir, opt) => {
    dir = (0, normalize_windows_path_js_1.normalizeWindowsPath)(dir);
    // if there's any overlap between mask and mode,
    // then we'll need an explicit chmod
    /* c8 ignore next */
    const umask = opt.umask ?? 0o22;
    const mode = opt.mode | 0o700;
    const needChmod = (mode & umask) !== 0;
    const uid = opt.uid;
    const gid = opt.gid;
    const doChown = typeof uid === 'number' &&
        typeof gid === 'number' &&
        (uid !== opt.processUid || gid !== opt.processGid);
    const preserve = opt.preserve;
    const unlink = opt.unlink;
    const cache = opt.cache;
    const cwd = (0, normalize_windows_path_js_1.normalizeWindowsPath)(opt.cwd);
    const done = (created) => {
        cSet(cache, dir, true);
        if (created && doChown) {
            (0, chownr_1.chownrSync)(created, uid, gid);
        }
        if (needChmod) {
            fs_1.default.chmodSync(dir, mode);
        }
    };
    if (cache && cGet(cache, dir) === true) {
        return done();
    }
    if (dir === cwd) {
        checkCwdSync(cwd);
        return done();
    }
    if (preserve) {
        return done((0, mkdirp_1.mkdirpSync)(dir, mode) ?? undefined);
    }
    const sub = (0, normalize_windows_path_js_1.normalizeWindowsPath)(node_path_1.default.relative(cwd, dir));
    const parts = sub.split('/');
    let created = undefined;
    for (let p = parts.shift(), part = cwd; p && (part += '/' + p); p = parts.shift()) {
        part = (0, normalize_windows_path_js_1.normalizeWindowsPath)(node_path_1.default.resolve(part));
        if (cGet(cache, part)) {
            continue;
        }
        try {
            fs_1.default.mkdirSync(part, mode);
            created = created || part;
            cSet(cache, part, true);
        }
        catch (er) {
            const st = fs_1.default.lstatSync(part);
            if (st.isDirectory()) {
                cSet(cache, part, true);
                continue;
            }
            else if (unlink) {
                fs_1.default.unlinkSync(part);
                fs_1.default.mkdirSync(part, mode);
                created = created || part;
                cSet(cache, part, true);
                continue;
            }
            else if (st.isSymbolicLink()) {
                return new symlink_error_js_1.SymlinkError(part, part + '/' + parts.join('/'));
            }
        }
    }
    return done(created);
};
exports.mkdirSync = mkdirSync;
//# sourceMappingURL=mkdir.js.map
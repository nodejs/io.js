import fs from 'node:fs';
import path from 'node:path';
import { CWD, EXTENSIONS, cjsRequire } from './constants.js';
export const tryPkg = (pkg) => {
    try {
        return cjsRequire.resolve(pkg);
    }
    catch (_a) { }
};
export const isPkgAvailable = (pkg) => !!tryPkg(pkg);
export const tryFile = (filePath, includeDir = false) => {
    if (typeof filePath === 'string') {
        return fs.existsSync(filePath) &&
            (includeDir || fs.statSync(filePath).isFile())
            ? filePath
            : '';
    }
    for (const file of filePath !== null && filePath !== void 0 ? filePath : []) {
        if (tryFile(file, includeDir)) {
            return file;
        }
    }
    return '';
};
export const tryExtensions = (filepath, extensions = EXTENSIONS) => {
    const ext = [...extensions, ''].find(ext => tryFile(filepath + ext));
    return ext == null ? '' : filepath + ext;
};
export const findUp = (searchEntry, searchFileOrIncludeDir, includeDir) => {
    console.assert(path.isAbsolute(searchEntry));
    if (!tryFile(searchEntry, true) ||
        (searchEntry !== CWD && !searchEntry.startsWith(CWD + path.sep))) {
        return '';
    }
    searchEntry = path.resolve(fs.statSync(searchEntry).isDirectory()
        ? searchEntry
        : path.resolve(searchEntry, '..'));
    const isSearchFile = typeof searchFileOrIncludeDir === 'string';
    const searchFile = isSearchFile ? searchFileOrIncludeDir : 'package.json';
    do {
        const searched = tryFile(path.resolve(searchEntry, searchFile), isSearchFile && includeDir);
        if (searched) {
            return searched;
        }
        searchEntry = path.resolve(searchEntry, '..');
    } while (searchEntry === CWD || searchEntry.startsWith(CWD + path.sep));
    return '';
};
//# sourceMappingURL=helpers.js.map
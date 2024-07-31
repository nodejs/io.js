"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.TUFClient = void 0;
/*
Copyright 2023 The Sigstore Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
const fs_1 = __importDefault(require("fs"));
const path_1 = __importDefault(require("path"));
const tuf_js_1 = require("tuf-js");
const _1 = require(".");
const target_1 = require("./target");
const TARGETS_DIR_NAME = 'targets';
class TUFClient {
    constructor(options) {
        const url = new URL(options.mirrorURL);
        const repoName = encodeURIComponent(url.host + url.pathname.replace(/\/$/, ''));
        const cachePath = path_1.default.join(options.cachePath, repoName);
        initTufCache(cachePath);
        seedCache({
            cachePath,
            mirrorURL: options.mirrorURL,
            tufRootPath: options.rootPath,
            forceInit: options.forceInit,
        });
        this.updater = initClient({
            mirrorURL: options.mirrorURL,
            cachePath,
            forceCache: options.forceCache,
            retry: options.retry,
            timeout: options.timeout,
        });
    }
    async refresh() {
        return this.updater.refresh();
    }
    getTarget(targetName) {
        return (0, target_1.readTarget)(this.updater, targetName);
    }
}
exports.TUFClient = TUFClient;
// Initializes the TUF cache directory structure including the initial
// root.json file. If the cache directory does not exist, it will be
// created. If the targets directory does not exist, it will be created.
// If the root.json file does not exist, it will be copied from the
// rootPath argument.
function initTufCache(cachePath) {
    const targetsPath = path_1.default.join(cachePath, TARGETS_DIR_NAME);
    if (!fs_1.default.existsSync(cachePath)) {
        fs_1.default.mkdirSync(cachePath, { recursive: true });
    }
    if (!fs_1.default.existsSync(targetsPath)) {
        fs_1.default.mkdirSync(targetsPath);
    }
}
// Populates the TUF cache with the initial root.json file. If the root.json
// file does not exist (or we're forcing re-initialization), copy it from either
// the rootPath argument or from one of the repo seeds.
function seedCache({ cachePath, mirrorURL, tufRootPath, forceInit, }) {
    const cachedRootPath = path_1.default.join(cachePath, 'root.json');
    // If the root.json file does not exist (or we're forcing re-initialization),
    // populate it either from the supplied rootPath or from one of the repo seeds.
    if (!fs_1.default.existsSync(cachedRootPath) || forceInit) {
        if (tufRootPath) {
            fs_1.default.copyFileSync(tufRootPath, cachedRootPath);
        }
        else {
            /* eslint-disable @typescript-eslint/no-var-requires */
            const seeds = require('../seeds.json');
            const repoSeed = seeds[mirrorURL];
            if (!repoSeed) {
                throw new _1.TUFError({
                    code: 'TUF_INIT_CACHE_ERROR',
                    message: `No root.json found for mirror: ${mirrorURL}`,
                });
            }
            fs_1.default.writeFileSync(cachedRootPath, Buffer.from(repoSeed['root.json'], 'base64'));
            // Copy any seed targets into the cache
            Object.entries(repoSeed.targets).forEach(([targetName, target]) => {
                fs_1.default.writeFileSync(path_1.default.join(cachePath, TARGETS_DIR_NAME, targetName), Buffer.from(target, 'base64'));
            });
        }
    }
}
function initClient(options) {
    const config = {
        fetchTimeout: options.timeout,
        fetchRetry: options.retry,
    };
    return new tuf_js_1.Updater({
        metadataBaseUrl: options.mirrorURL,
        targetBaseUrl: `${options.mirrorURL}/targets`,
        metadataDir: options.cachePath,
        targetDir: path_1.default.join(options.cachePath, TARGETS_DIR_NAME),
        forceCache: options.forceCache,
        config,
    });
}

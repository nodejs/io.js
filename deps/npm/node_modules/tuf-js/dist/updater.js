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
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.Updater = void 0;
const models_1 = require("@tufjs/models");
const debug_1 = __importDefault(require("debug"));
const fs = __importStar(require("fs"));
const path = __importStar(require("path"));
const config_1 = require("./config");
const error_1 = require("./error");
const fetcher_1 = require("./fetcher");
const store_1 = require("./store");
const url = __importStar(require("./utils/url"));
const log = (0, debug_1.default)('tuf:cache');
class Updater {
    constructor(options) {
        const { metadataDir, metadataBaseUrl, targetDir, targetBaseUrl, fetcher, config, } = options;
        this.dir = metadataDir;
        this.metadataBaseUrl = metadataBaseUrl;
        this.targetDir = targetDir;
        this.targetBaseUrl = targetBaseUrl;
        this.forceCache = options.forceCache ?? false;
        const data = this.loadLocalMetadata(models_1.MetadataKind.Root);
        this.trustedSet = new store_1.TrustedMetadataStore(data);
        this.config = { ...config_1.defaultConfig, ...config };
        this.fetcher =
            fetcher ||
                new fetcher_1.DefaultFetcher({
                    timeout: this.config.fetchTimeout,
                    retry: this.config.fetchRetries ?? this.config.fetchRetry,
                });
    }
    // refresh and load the metadata before downloading the target
    // refresh should be called once after the client is initialized
    async refresh() {
        // If forceCache is true, try to load the timestamp from local storage
        // without fetching it from the remote. Otherwise, load the root and
        // timestamp from the remote per the TUF spec.
        if (this.forceCache) {
            // If anything fails, load the root and timestamp from the remote. This
            // should cover any situation where the local metadata is corrupted or
            // expired.
            try {
                await this.loadTimestamp({ checkRemote: false });
            }
            catch (error) {
                await this.loadRoot();
                await this.loadTimestamp();
            }
        }
        else {
            await this.loadRoot();
            await this.loadTimestamp();
        }
        await this.loadSnapshot();
        await this.loadTargets(models_1.MetadataKind.Targets, models_1.MetadataKind.Root);
    }
    // Returns the TargetFile instance with information for the given target path.
    //
    // Implicitly calls refresh if it hasn't already been called.
    async getTargetInfo(targetPath) {
        if (!this.trustedSet.targets) {
            await this.refresh();
        }
        return this.preorderDepthFirstWalk(targetPath);
    }
    async downloadTarget(targetInfo, filePath, targetBaseUrl) {
        const targetPath = filePath || this.generateTargetPath(targetInfo);
        if (!targetBaseUrl) {
            if (!this.targetBaseUrl) {
                throw new error_1.ValueError('Target base URL not set');
            }
            targetBaseUrl = this.targetBaseUrl;
        }
        let targetFilePath = targetInfo.path;
        const consistentSnapshot = this.trustedSet.root.signed.consistentSnapshot;
        if (consistentSnapshot && this.config.prefixTargetsWithHash) {
            const hashes = Object.values(targetInfo.hashes);
            const { dir, base } = path.parse(targetFilePath);
            const filename = `${hashes[0]}.${base}`;
            targetFilePath = dir ? `${dir}/${filename}` : filename;
        }
        const targetUrl = url.join(targetBaseUrl, targetFilePath);
        // Client workflow 5.7.3: download target file
        await this.fetcher.downloadFile(targetUrl, targetInfo.length, async (fileName) => {
            // Verify hashes and length of downloaded file
            await targetInfo.verify(fs.createReadStream(fileName));
            // Copy file to target path
            log('WRITE %s', targetPath);
            fs.copyFileSync(fileName, targetPath);
        });
        return targetPath;
    }
    async findCachedTarget(targetInfo, filePath) {
        if (!filePath) {
            filePath = this.generateTargetPath(targetInfo);
        }
        try {
            if (fs.existsSync(filePath)) {
                await targetInfo.verify(fs.createReadStream(filePath));
                return filePath;
            }
        }
        catch (error) {
            return; // File not found
        }
        return; // File not found
    }
    loadLocalMetadata(fileName) {
        const filePath = path.join(this.dir, `${fileName}.json`);
        log('READ %s', filePath);
        return fs.readFileSync(filePath);
    }
    // Sequentially load and persist on local disk every newer root metadata
    // version available on the remote.
    // Client workflow 5.3: update root role
    async loadRoot() {
        // Client workflow 5.3.2: version of trusted root metadata file
        const rootVersion = this.trustedSet.root.signed.version;
        const lowerBound = rootVersion + 1;
        const upperBound = lowerBound + this.config.maxRootRotations;
        for (let version = lowerBound; version <= upperBound; version++) {
            const rootUrl = url.join(this.metadataBaseUrl, `${version}.root.json`);
            try {
                // Client workflow 5.3.3: download new root metadata file
                const bytesData = await this.fetcher.downloadBytes(rootUrl, this.config.rootMaxLength);
                // Client workflow 5.3.4 - 5.4.7
                this.trustedSet.updateRoot(bytesData);
                // Client workflow 5.3.8: persist root metadata file
                this.persistMetadata(models_1.MetadataKind.Root, bytesData);
            }
            catch (error) {
                break;
            }
        }
    }
    // Load local and remote timestamp metadata.
    // Client workflow 5.4: update timestamp role
    async loadTimestamp({ checkRemote } = { checkRemote: true }) {
        // Load local and remote timestamp metadata
        try {
            const data = this.loadLocalMetadata(models_1.MetadataKind.Timestamp);
            this.trustedSet.updateTimestamp(data);
            // If checkRemote is disabled, return here to avoid fetching the remote
            // timestamp metadata.
            if (!checkRemote) {
                return;
            }
        }
        catch (error) {
            // continue
        }
        //Load from remote (whether local load succeeded or not)
        const timestampUrl = url.join(this.metadataBaseUrl, 'timestamp.json');
        // Client workflow 5.4.1: download timestamp metadata file
        const bytesData = await this.fetcher.downloadBytes(timestampUrl, this.config.timestampMaxLength);
        try {
            // Client workflow 5.4.2 - 5.4.4
            this.trustedSet.updateTimestamp(bytesData);
        }
        catch (error) {
            // If new timestamp version is same as current, discardd the new one.
            // This is normal and should NOT raise an error.
            if (error instanceof error_1.EqualVersionError) {
                return;
            }
            // Re-raise any other error
            throw error;
        }
        // Client workflow 5.4.5: persist timestamp metadata
        this.persistMetadata(models_1.MetadataKind.Timestamp, bytesData);
    }
    // Load local and remote snapshot metadata.
    // Client workflow 5.5: update snapshot role
    async loadSnapshot() {
        //Load local (and if needed remote) snapshot metadata
        try {
            const data = this.loadLocalMetadata(models_1.MetadataKind.Snapshot);
            this.trustedSet.updateSnapshot(data, true);
        }
        catch (error) {
            if (!this.trustedSet.timestamp) {
                throw new ReferenceError('No timestamp metadata');
            }
            const snapshotMeta = this.trustedSet.timestamp.signed.snapshotMeta;
            const maxLength = snapshotMeta.length || this.config.snapshotMaxLength;
            const version = this.trustedSet.root.signed.consistentSnapshot
                ? snapshotMeta.version
                : undefined;
            const snapshotUrl = url.join(this.metadataBaseUrl, version ? `${version}.snapshot.json` : 'snapshot.json');
            try {
                // Client workflow 5.5.1: download snapshot metadata file
                const bytesData = await this.fetcher.downloadBytes(snapshotUrl, maxLength);
                // Client workflow 5.5.2 - 5.5.6
                this.trustedSet.updateSnapshot(bytesData);
                // Client workflow 5.5.7: persist snapshot metadata file
                this.persistMetadata(models_1.MetadataKind.Snapshot, bytesData);
            }
            catch (error) {
                throw new error_1.RuntimeError(`Unable to load snapshot metadata error ${error}`);
            }
        }
    }
    // Load local and remote targets metadata.
    // Client workflow 5.6: update targets role
    async loadTargets(role, parentRole) {
        if (this.trustedSet.getRole(role)) {
            return this.trustedSet.getRole(role);
        }
        try {
            const buffer = this.loadLocalMetadata(role);
            this.trustedSet.updateDelegatedTargets(buffer, role, parentRole);
        }
        catch (error) {
            // Local 'role' does not exist or is invalid: update from remote
            if (!this.trustedSet.snapshot) {
                throw new ReferenceError('No snapshot metadata');
            }
            const metaInfo = this.trustedSet.snapshot.signed.meta[`${role}.json`];
            // TODO: use length for fetching
            const maxLength = metaInfo.length || this.config.targetsMaxLength;
            const version = this.trustedSet.root.signed.consistentSnapshot
                ? metaInfo.version
                : undefined;
            const metadataUrl = url.join(this.metadataBaseUrl, version ? `${version}.${role}.json` : `${role}.json`);
            try {
                // Client workflow 5.6.1: download targets metadata file
                const bytesData = await this.fetcher.downloadBytes(metadataUrl, maxLength);
                // Client workflow 5.6.2 - 5.6.6
                this.trustedSet.updateDelegatedTargets(bytesData, role, parentRole);
                // Client workflow 5.6.7: persist targets metadata file
                this.persistMetadata(role, bytesData);
            }
            catch (error) {
                throw new error_1.RuntimeError(`Unable to load targets error ${error}`);
            }
        }
        return this.trustedSet.getRole(role);
    }
    async preorderDepthFirstWalk(targetPath) {
        // Interrogates the tree of target delegations in order of appearance
        // (which implicitly order trustworthiness), and returns the matching
        // target found in the most trusted role.
        // List of delegations to be interrogated. A (role, parent role) pair
        // is needed to load and verify the delegated targets metadata.
        const delegationsToVisit = [
            {
                roleName: models_1.MetadataKind.Targets,
                parentRoleName: models_1.MetadataKind.Root,
            },
        ];
        const visitedRoleNames = new Set();
        // Client workflow 5.6.7: preorder depth-first traversal of the graph of
        // target delegations
        while (visitedRoleNames.size <= this.config.maxDelegations &&
            delegationsToVisit.length > 0) {
            //  Pop the role name from the top of the stack.
            // eslint-disable-next-line @typescript-eslint/no-non-null-assertion
            const { roleName, parentRoleName } = delegationsToVisit.pop();
            // Skip any visited current role to prevent cycles.
            // Client workflow 5.6.7.1: skip already-visited roles
            if (visitedRoleNames.has(roleName)) {
                continue;
            }
            // The metadata for 'role_name' must be downloaded/updated before
            // its targets, delegations, and child roles can be inspected.
            const targets = (await this.loadTargets(roleName, parentRoleName))
                ?.signed;
            if (!targets) {
                continue;
            }
            const target = targets.targets?.[targetPath];
            if (target) {
                return target;
            }
            // After preorder check, add current role to set of visited roles.
            visitedRoleNames.add(roleName);
            if (targets.delegations) {
                const childRolesToVisit = [];
                // NOTE: This may be a slow operation if there are many delegated roles.
                const rolesForTarget = targets.delegations.rolesForTarget(targetPath);
                for (const { role: childName, terminating } of rolesForTarget) {
                    childRolesToVisit.push({
                        roleName: childName,
                        parentRoleName: roleName,
                    });
                    // Client workflow 5.6.7.2.1
                    if (terminating) {
                        delegationsToVisit.splice(0); // empty the array
                        break;
                    }
                }
                childRolesToVisit.reverse();
                delegationsToVisit.push(...childRolesToVisit);
            }
        }
        return; // no matching target found
    }
    generateTargetPath(targetInfo) {
        if (!this.targetDir) {
            throw new error_1.ValueError('Target directory not set');
        }
        // URL encode target path
        const filePath = encodeURIComponent(targetInfo.path);
        return path.join(this.targetDir, filePath);
    }
    persistMetadata(metaDataName, bytesData) {
        try {
            const filePath = path.join(this.dir, `${metaDataName}.json`);
            log('WRITE %s', filePath);
            fs.writeFileSync(filePath, bytesData.toString('utf8'));
        }
        catch (error) {
            throw new error_1.PersistError(`Failed to persist metadata ${metaDataName} error: ${error}`);
        }
    }
}
exports.Updater = Updater;

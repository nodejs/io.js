/**
 * @fileoverview Flat Config Array
 * @author Nicholas C. Zakas
 */

"use strict";

//-----------------------------------------------------------------------------
// Requirements
//-----------------------------------------------------------------------------

const { ConfigArray, ConfigArraySymbol } = require("@eslint/config-array");
const { flatConfigSchema, hasMethod } = require("./flat-config-schema");
const { RuleValidator } = require("./rule-validator");
const { defaultConfig } = require("./default-config");

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------

/**
 * Fields that are considered metadata and not part of the config object.
 */
const META_FIELDS = new Set(["name"]);

const ruleValidator = new RuleValidator();

/**
 * Splits a plugin identifier in the form a/b/c into two parts: a/b and c.
 * @param {string} identifier The identifier to parse.
 * @returns {{objectName: string, pluginName: string}} The parts of the plugin
 *      name.
 */
function splitPluginIdentifier(identifier) {
    const parts = identifier.split("/");

    return {
        objectName: parts.pop(),
        pluginName: parts.join("/")
    };
}

/**
 * Returns the name of an object in the config by reading its `meta` key.
 * @param {Object} object The object to check.
 * @returns {string?} The name of the object if found or `null` if there
 *      is no name.
 */
function getObjectId(object) {

    // first check old-style name
    let name = object.name;

    if (!name) {

        if (!object.meta) {
            return null;
        }

        name = object.meta.name;

        if (!name) {
            return null;
        }
    }

    // now check for old-style version
    let version = object.version;

    if (!version) {
        version = object.meta && object.meta.version;
    }

    // if there's a version then append that
    if (version) {
        return `${name}@${version}`;
    }

    return name;
}

/**
 * Wraps a config error with details about where the error occurred.
 * @param {Error} error The original error.
 * @param {number} originalLength The original length of the config array.
 * @param {number} baseLength The length of the base config.
 * @returns {TypeError} The new error with details.
 */
function wrapConfigErrorWithDetails(error, originalLength, baseLength) {

    let location = "user-defined";
    let configIndex = error.index;

    /*
     * A config array is set up in this order:
     * 1. Base config
     * 2. Original configs
     * 3. User-defined configs
     * 4. CLI-defined configs
     *
     * So we need to adjust the index to account for the base config.
     *
     * - If the index is less than the base length, it's in the base config
     *   (as specified by `baseConfig` argument to `FlatConfigArray` constructor).
     * - If the index is greater than the base length but less than the original
     *   length + base length, it's in the original config. The original config
     *   is passed to the `FlatConfigArray` constructor as the first argument.
     * - Otherwise, it's in the user-defined config, which is loaded from the
     *   config file and merged with any command-line options.
     */
    if (error.index < baseLength) {
        location = "base";
    } else if (error.index < originalLength + baseLength) {
        location = "original";
        configIndex = error.index - baseLength;
    } else {
        configIndex = error.index - originalLength - baseLength;
    }

    return new TypeError(
        `${error.message.slice(0, -1)} at ${location} index ${configIndex}.`,
        { cause: error }
    );
}

/**
 * Converts a languageOptions object to a JSON representation.
 * @param {Record<string, any>} languageOptions The options to create a JSON
 *     representation of.
 * @param {string} objectKey The key of the object being converted.
 * @returns {Record<string, any>} The JSON representation of the languageOptions.
 * @throws {TypeError} If a function is found in the languageOptions.
 */
function languageOptionsToJSON(languageOptions, objectKey = "languageOptions") {

    const result = {};

    for (const [key, value] of Object.entries(languageOptions)) {
        if (value) {
            if (typeof value === "object") {
                const name = getObjectId(value);

                if (name && hasMethod(value)) {
                    result[key] = name;
                } else {
                    result[key] = languageOptionsToJSON(value, key);
                }
                continue;
            }

            if (typeof value === "function") {
                throw new TypeError(`Cannot serialize key "${key}" in ${objectKey}: Function values are not supported.`);
            }

        }

        result[key] = value;
    }

    return result;
}

const originalBaseConfig = Symbol("originalBaseConfig");
const originalLength = Symbol("originalLength");
const baseLength = Symbol("baseLength");

//-----------------------------------------------------------------------------
// Exports
//-----------------------------------------------------------------------------

/**
 * Represents an array containing configuration information for ESLint.
 */
class FlatConfigArray extends ConfigArray {

    /**
     * Creates a new instance.
     * @param {*[]} configs An array of configuration information.
     * @param {{basePath: string, shouldIgnore: boolean, baseConfig: FlatConfig}} options The options
     *      to use for the config array instance.
     */
    constructor(configs, {
        basePath,
        shouldIgnore = true,
        baseConfig = defaultConfig
    } = {}) {
        super(configs, {
            basePath,
            schema: flatConfigSchema
        });

        /**
         * The original length of the array before any modifications.
         * @type {number}
         */
        this[originalLength] = this.length;

        if (baseConfig[Symbol.iterator]) {
            this.unshift(...baseConfig);
        } else {
            this.unshift(baseConfig);
        }

        /**
         * The length of the array after applying the base config.
         * @type {number}
         */
        this[baseLength] = this.length - this[originalLength];

        /**
         * The base config used to build the config array.
         * @type {Array<FlatConfig>}
         */
        this[originalBaseConfig] = baseConfig;
        Object.defineProperty(this, originalBaseConfig, { writable: false });

        /**
         * Determines if `ignores` fields should be honored.
         * If true, then all `ignores` fields are honored.
         * if false, then only `ignores` fields in the baseConfig are honored.
         * @type {boolean}
         */
        this.shouldIgnore = shouldIgnore;
        Object.defineProperty(this, "shouldIgnore", { writable: false });
    }

    /**
     * Normalizes the array by calling the superclass method and catching/rethrowing
     * any ConfigError exceptions with additional details.
     * @param {any} [context] The context to use to normalize the array.
     * @returns {Promise<FlatConfigArray>} A promise that resolves when the array is normalized.
     */
    normalize(context) {
        return super.normalize(context)
            .catch(error => {
                if (error.name === "ConfigError") {
                    throw wrapConfigErrorWithDetails(error, this[originalLength], this[baseLength]);
                }

                throw error;

            });
    }

    /**
     * Normalizes the array by calling the superclass method and catching/rethrowing
     * any ConfigError exceptions with additional details.
     * @param {any} [context] The context to use to normalize the array.
     * @returns {FlatConfigArray} The current instance.
     * @throws {TypeError} If the config is invalid.
     */
    normalizeSync(context) {

        try {

            return super.normalizeSync(context);

        } catch (error) {

            if (error.name === "ConfigError") {
                throw wrapConfigErrorWithDetails(error, this[originalLength], this[baseLength]);
            }

            throw error;

        }

    }

    /* eslint-disable class-methods-use-this -- Desired as instance method */
    /**
     * Replaces a config with another config to allow us to put strings
     * in the config array that will be replaced by objects before
     * normalization.
     * @param {Object} config The config to preprocess.
     * @returns {Object} The preprocessed config.
     */
    [ConfigArraySymbol.preprocessConfig](config) {

        /*
         * If a config object has `ignores` and no other non-meta fields, then it's an object
         * for global ignores. If `shouldIgnore` is false, that object shouldn't apply,
         * so we'll remove its `ignores`.
         */
        if (
            !this.shouldIgnore &&
            !this[originalBaseConfig].includes(config) &&
            config.ignores &&
            Object.keys(config).filter(key => !META_FIELDS.has(key)).length === 1
        ) {
            /* eslint-disable-next-line no-unused-vars -- need to strip off other keys */
            const { ignores, ...otherKeys } = config;

            return otherKeys;
        }

        return config;
    }

    /**
     * Finalizes the config by replacing plugin references with their objects
     * and validating rule option schemas.
     * @param {Object} config The config to finalize.
     * @returns {Object} The finalized config.
     * @throws {TypeError} If the config is invalid.
     */
    [ConfigArraySymbol.finalizeConfig](config) {

        const { plugins, language, languageOptions, processor } = config;
        let parserName, processorName, languageName;
        let invalidParser = false,
            invalidProcessor = false,
            invalidLanguage = false;

        // Check parser value
        if (languageOptions && languageOptions.parser) {
            const { parser } = languageOptions;

            if (typeof parser === "object") {
                parserName = getObjectId(parser);

                if (!parserName) {
                    invalidParser = true;
                }

            } else {
                invalidParser = true;
            }
        }

        // Check language value
        if (language) {
            if (typeof language === "string") {
                const { pluginName, objectName: localLanguageName } = splitPluginIdentifier(language);

                languageName = language;

                if (!plugins || !plugins[pluginName] || !plugins[pluginName].languages || !plugins[pluginName].languages[localLanguageName]) {
                    throw new TypeError(`Key "language": Could not find "${localLanguageName}" in plugin "${pluginName}".`);
                }

                config.language = plugins[pluginName].languages[localLanguageName];
            } else {
                invalidLanguage = true;
            }

            try {
                config.language.validateLanguageOptions(config.languageOptions);
            } catch (error) {
                throw new TypeError(`Key "languageOptions": ${error.message}`, { cause: error });
            }
        }

        // Check processor value
        if (processor) {
            if (typeof processor === "string") {
                const { pluginName, objectName: localProcessorName } = splitPluginIdentifier(processor);

                processorName = processor;

                if (!plugins || !plugins[pluginName] || !plugins[pluginName].processors || !plugins[pluginName].processors[localProcessorName]) {
                    throw new TypeError(`Key "processor": Could not find "${localProcessorName}" in plugin "${pluginName}".`);
                }

                config.processor = plugins[pluginName].processors[localProcessorName];
            } else if (typeof processor === "object") {
                processorName = getObjectId(processor);

                if (!processorName) {
                    invalidProcessor = true;
                }

            } else {
                invalidProcessor = true;
            }
        }

        ruleValidator.validate(config);

        // apply special logic for serialization into JSON
        /* eslint-disable object-shorthand -- shorthand would change "this" value */
        Object.defineProperty(config, "toJSON", {
            value: function() {

                if (invalidParser) {
                    throw new Error("Could not serialize parser object (missing 'meta' object).");
                }

                if (invalidProcessor) {
                    throw new Error("Could not serialize processor object (missing 'meta' object).");
                }

                if (invalidLanguage) {
                    throw new Error("Caching is not supported when language is an object.");
                }

                return {
                    ...this,
                    plugins: Object.entries(plugins).map(([namespace, plugin]) => {

                        const pluginId = getObjectId(plugin);

                        if (!pluginId) {
                            return namespace;
                        }

                        return `${namespace}:${pluginId}`;
                    }),
                    language: languageName,
                    languageOptions: languageOptionsToJSON(languageOptions),
                    processor: processorName
                };
            }
        });
        /* eslint-enable object-shorthand -- ok to enable now */

        return config;
    }
    /* eslint-enable class-methods-use-this -- Desired as instance method */

}

exports.FlatConfigArray = FlatConfigArray;

/**
 * @fileoverview Shared functions to work with configs.
 * @author Nicholas C. Zakas
 */

"use strict";

//------------------------------------------------------------------------------
// Typedefs
//------------------------------------------------------------------------------

/** @typedef {import("../shared/types").Rule} Rule */

//------------------------------------------------------------------------------
// Private Members
//------------------------------------------------------------------------------

// JSON schema that disallows passing any options
const noOptionsSchema = Object.freeze({
    type: "array",
    minItems: 0,
    maxItems: 0
});

//-----------------------------------------------------------------------------
// Functions
//-----------------------------------------------------------------------------

/**
 * Parses a ruleId into its plugin and rule parts.
 * @param {string} ruleId The rule ID to parse.
 * @returns {{pluginName:string,ruleName:string}} The plugin and rule
 *      parts of the ruleId;
 */
function parseRuleId(ruleId) {
    let pluginName, ruleName;

    // distinguish between core rules and plugin rules
    if (ruleId.includes("/")) {

        // mimic scoped npm packages
        if (ruleId.startsWith("@")) {
            pluginName = ruleId.slice(0, ruleId.lastIndexOf("/"));
        } else {
            pluginName = ruleId.slice(0, ruleId.indexOf("/"));
        }

        ruleName = ruleId.slice(pluginName.length + 1);
    } else {
        pluginName = "@";
        ruleName = ruleId;
    }

    return {
        pluginName,
        ruleName
    };
}

/**
 * Retrieves a rule instance from a given config based on the ruleId.
 * @param {string} ruleId The rule ID to look for.
 * @param {FlatConfig} config The config to search.
 * @returns {import("../shared/types").Rule|undefined} The rule if found
 *      or undefined if not.
 */
function getRuleFromConfig(ruleId, config) {

    const { pluginName, ruleName } = parseRuleId(ruleId);

    const plugin = config.plugins && config.plugins[pluginName];
    const rule = plugin && plugin.rules && plugin.rules[ruleName];

    return rule;
}

/**
 * Gets a complete options schema for a rule.
 * @param {Rule} rule A rule object
 * @throws {TypeError} If `meta.schema` is specified but is not an array, object or `false`.
 * @returns {Object|null} JSON Schema for the rule's options. `null` if `meta.schema` is `false`.
 */
function getRuleOptionsSchema(rule) {

    if (!rule.meta) {
        return { ...noOptionsSchema }; // default if `meta.schema` is not specified
    }

    const schema = rule.meta.schema;

    if (typeof schema === "undefined") {
        return { ...noOptionsSchema }; // default if `meta.schema` is not specified
    }

    // `schema:false` is an allowed explicit opt-out of options validation for the rule
    if (schema === false) {
        return null;
    }

    if (typeof schema !== "object" || schema === null) {
        throw new TypeError("Rule's `meta.schema` must be an array or object");
    }

    // ESLint-specific array form needs to be converted into a valid JSON Schema definition
    if (Array.isArray(schema)) {
        if (schema.length) {
            return {
                type: "array",
                items: schema,
                minItems: 0,
                maxItems: schema.length
            };
        }

        // `schema:[]` is an explicit way to specify that the rule does not accept any options
        return { ...noOptionsSchema };
    }

    // `schema:<object>` is assumed to be a valid JSON Schema definition
    return schema;
}


//-----------------------------------------------------------------------------
// Exports
//-----------------------------------------------------------------------------

module.exports = {
    parseRuleId,
    getRuleFromConfig,
    getRuleOptionsSchema
};

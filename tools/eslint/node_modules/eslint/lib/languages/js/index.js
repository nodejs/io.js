/**
 * @fileoverview JavaScript Language Object
 * @author Nicholas C. Zakas
 */

"use strict";

//-----------------------------------------------------------------------------
// Requirements
//-----------------------------------------------------------------------------

const { SourceCode } = require("./source-code");
const createDebug = require("debug");
const astUtils = require("../../shared/ast-utils");
const eslintScope = require("eslint-scope");
const evk = require("eslint-visitor-keys");
const { validateLanguageOptions } = require("./validate-language-options");

//-----------------------------------------------------------------------------
// Type Definitions
//-----------------------------------------------------------------------------

/** @typedef {import("@eslint/core").File} File */
/** @typedef {import("@eslint/core").Language} Language */
/** @typedef {import("@eslint/core").OkParseResult} OkParseResult */

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------

const debug = createDebug("eslint:languages:js");
const DEFAULT_ECMA_VERSION = 5;

/**
 * Analyze scope of the given AST.
 * @param {ASTNode} ast The `Program` node to analyze.
 * @param {LanguageOptions} languageOptions The parser options.
 * @param {Record<string, string[]>} visitorKeys The visitor keys.
 * @returns {ScopeManager} The analysis result.
 */
function analyzeScope(ast, languageOptions, visitorKeys) {
    const parserOptions = languageOptions.parserOptions;
    const ecmaFeatures = parserOptions.ecmaFeatures || {};
    const ecmaVersion = languageOptions.ecmaVersion || DEFAULT_ECMA_VERSION;

    return eslintScope.analyze(ast, {
        ignoreEval: true,
        nodejsScope: ecmaFeatures.globalReturn,
        impliedStrict: ecmaFeatures.impliedStrict,
        ecmaVersion: typeof ecmaVersion === "number" ? ecmaVersion : 6,
        sourceType: languageOptions.sourceType || "script",
        childVisitorKeys: visitorKeys || evk.KEYS,
        fallback: evk.getKeys
    });
}

//-----------------------------------------------------------------------------
// Exports
//-----------------------------------------------------------------------------

/**
 * @type {Language}
 */
module.exports = {

    fileType: "text",
    lineStart: 1,
    columnStart: 0,
    nodeTypeKey: "type",
    visitorKeys: evk.KEYS,

    validateLanguageOptions,

    /**
     * Determines if a given node matches a given selector class.
     * @param {string} className The class name to check.
     * @param {ASTNode} node The node to check.
     * @param {Array<ASTNode>} ancestry The ancestry of the node.
     * @returns {boolean} True if there's a match, false if not.
     * @throws {Error} When an unknown class name is passed.
     */
    matchesSelectorClass(className, node, ancestry) {

        /*
         * Copyright (c) 2013, Joel Feenstra
         * All rights reserved.
         *
         * Redistribution and use in source and binary forms, with or without
         * modification, are permitted provided that the following conditions are met:
         *    * Redistributions of source code must retain the above copyright
         *      notice, this list of conditions and the following disclaimer.
         *    * Redistributions in binary form must reproduce the above copyright
         *      notice, this list of conditions and the following disclaimer in the
         *      documentation and/or other materials provided with the distribution.
         *    * Neither the name of the ESQuery nor the names of its contributors may
         *      be used to endorse or promote products derived from this software without
         *      specific prior written permission.
         *
         * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
         * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
         * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
         * DISCLAIMED. IN NO EVENT SHALL JOEL FEENSTRA BE LIABLE FOR ANY
         * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
         * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
         * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
         * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
         * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
         * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
         */

        switch (className.toLowerCase()) {

            case "statement":
                if (node.type.slice(-9) === "Statement") {
                    return true;
                }

                // fallthrough: interface Declaration <: Statement { }

            case "declaration":
                return node.type.slice(-11) === "Declaration";

            case "pattern":
                if (node.type.slice(-7) === "Pattern") {
                    return true;
                }

                // fallthrough: interface Expression <: Node, Pattern { }

            case "expression":
                return node.type.slice(-10) === "Expression" ||
                    node.type.slice(-7) === "Literal" ||
                    (
                        node.type === "Identifier" &&
                        (ancestry.length === 0 || ancestry[0].type !== "MetaProperty")
                    ) ||
                    node.type === "MetaProperty";

            case "function":
                return node.type === "FunctionDeclaration" ||
                    node.type === "FunctionExpression" ||
                    node.type === "ArrowFunctionExpression";

            default:
                throw new Error(`Unknown class name: ${className}`);
        }
    },

    /**
     * Parses the given file into an AST.
     * @param {File} file The virtual file to parse.
     * @param {Object} options Additional options passed from ESLint.
     * @param {LanguageOptions} options.languageOptions The language options.
     * @returns {Object} The result of parsing.
     */
    parse(file, { languageOptions }) {

        // Note: BOM already removed
        const { body: text, path: filePath } = file;
        const textToParse = text.replace(astUtils.shebangPattern, (match, captured) => `//${captured}`);
        const { ecmaVersion, sourceType, parser } = languageOptions;
        const parserOptions = Object.assign(
            { ecmaVersion, sourceType },
            languageOptions.parserOptions,
            {
                loc: true,
                range: true,
                raw: true,
                tokens: true,
                comment: true,
                eslintVisitorKeys: true,
                eslintScopeManager: true,
                filePath
            }
        );

        /*
         * Check for parsing errors first. If there's a parsing error, nothing
         * else can happen. However, a parsing error does not throw an error
         * from this method - it's just considered a fatal error message, a
         * problem that ESLint identified just like any other.
         */
        try {
            debug("Parsing:", filePath);
            const parseResult = (typeof parser.parseForESLint === "function")
                ? parser.parseForESLint(textToParse, parserOptions)
                : { ast: parser.parse(textToParse, parserOptions) };

            debug("Parsing successful:", filePath);

            const {
                ast,
                services: parserServices = {},
                visitorKeys = evk.KEYS,
                scopeManager
            } = parseResult;

            return {
                ok: true,
                ast,
                parserServices,
                visitorKeys,
                scopeManager
            };
        } catch (ex) {

            // If the message includes a leading line number, strip it:
            const message = ex.message.replace(/^line \d+:/iu, "").trim();

            debug("%s\n%s", message, ex.stack);

            return {
                ok: false,
                errors: [{
                    message,
                    line: ex.lineNumber,
                    column: ex.column
                }]
            };
        }

    },

    /**
     * Creates a new `SourceCode` object from the given information.
     * @param {File} file The virtual file to create a `SourceCode` object from.
     * @param {OkParseResult} parseResult The result returned from `parse()`.
     * @param {Object} options Additional options passed from ESLint.
     * @param {LanguageOptions} options.languageOptions The language options.
     * @returns {SourceCode} The new `SourceCode` object.
     */
    createSourceCode(file, parseResult, { languageOptions }) {

        const { body: text, path: filePath, bom: hasBOM } = file;
        const { ast, parserServices, visitorKeys } = parseResult;

        debug("Scope analysis:", filePath);
        const scopeManager = parseResult.scopeManager || analyzeScope(ast, languageOptions, visitorKeys);

        debug("Scope analysis successful:", filePath);

        return new SourceCode({
            text,
            ast,
            hasBOM,
            parserServices,
            scopeManager,
            visitorKeys
        });
    }

};

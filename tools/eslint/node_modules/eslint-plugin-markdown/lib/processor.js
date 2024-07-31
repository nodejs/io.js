/**
 * @fileoverview Processes Markdown files for consumption by ESLint.
 * @author Brandon Mills
 */

/**
 * @typedef {import('eslint/lib/shared/types').LintMessage} Message
 * @typedef {Object} ASTNode
 * @property {string} type The type of node.
 * @property {string} [lang] The language that the node is in
 * @typedef {Object} RangeMap
 * @property {number} indent Number of code block indent characters trimmed from
 *     the beginning of the line during extraction.
 * @property {number} js Offset from the start of the code block's range in the
 *     extracted JS.
 * @property {number} md Offset from the start of the code block's range in the
 *     original Markdown.
 * @typedef {Object} BlockBase
 * @property {string} baseIndentText Leading whitespace text for the block.
 * @property {string[]} comments Comments inside of the JavaScript code.
 * @property {RangeMap[]} rangeMap A list of offset-based adjustments, where
 *    lookups are done based on the `js` key, which represents the range in the
 *    linted JS, and the `md` key is the offset delta that, when added to the JS
 *    range, returns the corresponding location in the original Markdown source.
 * @typedef {ASTNode & BlockBase} Block
 */

"use strict";

const parse = require("mdast-util-from-markdown");
const pkg = require("../package.json");

const UNSATISFIABLE_RULES = new Set([
    "eol-last", // The Markdown parser strips trailing newlines in code fences
    "unicode-bom" // Code blocks will begin in the middle of Markdown files
]);
const SUPPORTS_AUTOFIX = true;

/**
 * @type {Map<string, Block[]>}
 */
const blocksCache = new Map();

/**
 * Performs a depth-first traversal of the Markdown AST.
 * @param {ASTNode} node A Markdown AST node.
 * @param {{[key: string]: (node: ASTNode) => void}} callbacks A map of node types to callbacks.
 * @returns {void}
 */
function traverse(node, callbacks) {
    if (callbacks[node.type]) {
        callbacks[node.type](node);
    } else {
        callbacks["*"]();
    }

    if (typeof node.children !== "undefined") {
        for (let i = 0; i < node.children.length; i++) {
            traverse(node.children[i], callbacks);
        }
    }
}

/**
 * Extracts `eslint-*` or `global` comments from HTML comments if present.
 * @param {string} html The text content of an HTML AST node.
 * @returns {string} The comment's text without the opening and closing tags or
 *     an empty string if the text is not an ESLint HTML comment.
 */
function getComment(html) {
    const commentStart = "<!--";
    const commentEnd = "-->";
    const regex = /^(eslint\b|global\s)/u;

    if (
        html.slice(0, commentStart.length) !== commentStart ||
        html.slice(-commentEnd.length) !== commentEnd
    ) {
        return "";
    }

    const comment = html.slice(commentStart.length, -commentEnd.length);

    if (!regex.test(comment.trim())) {
        return "";
    }

    return comment;
}

// Before a code block, blockquote characters (`>`) are also considered
// "whitespace".
const leadingWhitespaceRegex = /^[>\s]*/u;

/**
 * Gets the offset for the first column of the node's first line in the
 * original source text.
 * @param {ASTNode} node A Markdown code block AST node.
 * @returns {number} The offset for the first column of the node's first line.
 */
function getBeginningOfLineOffset(node) {
    return node.position.start.offset - node.position.start.column + 1;
}

/**
 * Gets the leading text, typically whitespace with possible blockquote chars,
 * used to indent a code block.
 * @param {string} text The text of the file.
 * @param {ASTNode} node A Markdown code block AST node.
 * @returns {string} The text from the start of the first line to the opening
 *     fence of the code block.
 */
function getIndentText(text, node) {
    return leadingWhitespaceRegex.exec(
        text.slice(getBeginningOfLineOffset(node))
    )[0];
}

/**
 * When applying fixes, the postprocess step needs to know how to map fix ranges
 * from their location in the linted JS to the original offset in the Markdown.
 * Configuration comments and indentation trimming both complicate this process.
 *
 * Configuration comments appear in the linted JS but not in the Markdown code
 * block. Fixes to configuration comments would cause undefined behavior and
 * should be ignored during postprocessing. Fixes to actual code after
 * configuration comments need to be mapped back to the code block after
 * removing any offset due to configuration comments.
 *
 * Fenced code blocks can be indented by up to three spaces at the opening
 * fence. Inside of a list, for example, this indent can be in addition to the
 * indent already required for list item children. Leading whitespace inside
 * indented code blocks is trimmed up to the level of the opening fence and does
 * not appear in the linted code. Further, lines can have less leading
 * whitespace than the opening fence, so not all lines are guaranteed to have
 * the same column offset as the opening fence.
 *
 * The source code of a non-configuration-comment line in the linted JS is a
 * suffix of the corresponding line in the Markdown code block. There are no
 * differences within the line, so the mapping need only provide the offset
 * delta at the beginning of each line.
 * @param {string} text The text of the file.
 * @param {ASTNode} node A Markdown code block AST node.
 * @param {string[]} comments List of configuration comment strings that will be
 *     inserted at the beginning of the code block.
 * @returns {RangeMap[]} A list of offset-based adjustments, where lookups are
 *     done based on the `js` key, which represents the range in the linted JS,
 *     and the `md` key is the offset delta that, when added to the JS range,
 *     returns the corresponding location in the original Markdown source.
 */
function getBlockRangeMap(text, node, comments) {

    /*
     * The parser sets the fenced code block's start offset to wherever content
     * should normally begin (typically the first column of the line, but more
     * inside a list item, for example). The code block's opening fence may be
     * further indented by up to three characters. If the code block has
     * additional indenting, the opening fence's first backtick may be up to
     * three whitespace characters after the start offset.
     */
    const startOffset = getBeginningOfLineOffset(node);

    /*
     * Extract the Markdown source to determine the leading whitespace for each
     * line.
     */
    const code = text.slice(startOffset, node.position.end.offset);
    const lines = code.split("\n");

    /*
     * The parser trims leading whitespace from each line of code within the
     * fenced code block up to the opening fence's first backtick. The first
     * backtick's column is the AST node's starting column plus any additional
     * indentation.
     */
    const baseIndent = getIndentText(text, node).length;

    /*
     * Track the length of any inserted configuration comments at the beginning
     * of the linted JS and start the JS offset lookup keys at this index.
     */
    const commentLength = comments.reduce((len, comment) => len + comment.length + 1, 0);

    /*
     * In case there are configuration comments, initialize the map so that the
     * first lookup index is always 0. If there are no configuration comments,
     * the lookup index will also be 0, and the lookup should always go to the
     * last range that matches, skipping this initialization entry.
     */
    const rangeMap = [{
        indent: baseIndent,
        js: 0,
        md: 0
    }];

    // Start the JS offset after any configuration comments.
    let jsOffset = commentLength;

    /*
     * Start the Markdown offset at the beginning of the block's first line of
     * actual code. The first line of the block is always the opening fence, so
     * the code begins on the second line.
     */
    let mdOffset = startOffset + lines[0].length + 1;

    /*
     * For each line, determine how much leading whitespace was trimmed due to
     * indentation. Increase the JS lookup offset by the length of the line
     * post-trimming and the Markdown offset by the total line length.
     */
    for (let i = 0; i + 1 < lines.length; i++) {
        const line = lines[i + 1];
        const leadingWhitespaceLength = leadingWhitespaceRegex.exec(line)[0].length;

        // The parser trims leading whitespace up to the level of the opening
        // fence, so keep any additional indentation beyond that.
        const trimLength = Math.min(baseIndent, leadingWhitespaceLength);

        rangeMap.push({
            indent: trimLength,
            js: jsOffset,

            // Advance `trimLength` character from the beginning of the Markdown
            // line to the beginning of the equivalent JS line, then compute the
            // delta.
            md: mdOffset + trimLength - jsOffset
        });

        // Accumulate the current line in the offsets, and don't forget the
        // newline.
        mdOffset += line.length + 1;
        jsOffset += line.length - trimLength + 1;
    }

    return rangeMap;
}

const languageToFileExtension = {
    javascript: "js",
    ecmascript: "js",
    typescript: "ts",
    markdown: "md"
};

/**
 * Extracts lintable code blocks from Markdown text.
 * @param {string} text The text of the file.
 * @param {string} filename The filename of the file
 * @returns {Array<{ filename: string, text: string }>} Source code blocks to lint.
 */
function preprocess(text, filename) {
    const ast = parse(text);
    const blocks = [];

    blocksCache.set(filename, blocks);

    /**
     * During the depth-first traversal, keep track of any sequences of HTML
     * comment nodes containing `eslint-*` or `global` comments. If a code
     * block immediately follows such a sequence, insert the comments at the
     * top of the code block. Any non-ESLint comment or other node type breaks
     * and empties the sequence.
     * @type {string[]}
     */
    let htmlComments = [];

    traverse(ast, {
        "*"() {
            htmlComments = [];
        },
        code(node) {
            if (node.lang) {
                const comments = [];

                for (const comment of htmlComments) {
                    if (comment.trim() === "eslint-skip") {
                        htmlComments = [];
                        return;
                    }

                    comments.push(`/*${comment}*/`);
                }

                htmlComments = [];

                blocks.push({
                    ...node,
                    baseIndentText: getIndentText(text, node),
                    comments,
                    rangeMap: getBlockRangeMap(text, node, comments)
                });
            }
        },
        html(node) {
            const comment = getComment(node.value);

            if (comment) {
                htmlComments.push(comment);
            } else {
                htmlComments = [];
            }
        }
    });

    return blocks.map((block, index) => {
        const [language] = block.lang.trim().split(" ");
        const fileExtension = Object.hasOwn(languageToFileExtension, language) ? languageToFileExtension[language] : language;

        return {
            filename: `${index}.${fileExtension}`,
            text: [
                ...block.comments,
                block.value,
                ""
            ].join("\n")
        };
    });
}

/**
 * Creates a map function that adjusts messages in a code block.
 * @param {Block} block A code block.
 * @returns {(message: Message) => Message} A function that adjusts messages in a code block.
 */
function adjustBlock(block) {
    const leadingCommentLines = block.comments.reduce((count, comment) => count + comment.split("\n").length, 0);

    const blockStart = block.position.start.line;

    /**
     * Adjusts ESLint messages to point to the correct location in the Markdown.
     * @param {Message} message A message from ESLint.
     * @returns {Message} The same message, but adjusted to the correct location.
     */
    return function adjustMessage(message) {
        if (!Number.isInteger(message.line)) {
            return {
                ...message,
                line: blockStart,
                column: block.position.start.column
            };
        }

        const lineInCode = message.line - leadingCommentLines;

        if (lineInCode < 1 || lineInCode >= block.rangeMap.length) {
            return null;
        }

        const out = {
            line: lineInCode + blockStart,
            column: message.column + block.rangeMap[lineInCode].indent
        };

        if (Number.isInteger(message.endLine)) {
            out.endLine = message.endLine - leadingCommentLines + blockStart;
        }

        const adjustedFix = {};

        if (message.fix) {
            adjustedFix.fix = {
                range: message.fix.range.map(range => {

                    // Advance through the block's range map to find the last
                    // matching range by finding the first range too far and
                    // then going back one.
                    let i = 1;

                    while (i < block.rangeMap.length && block.rangeMap[i].js <= range) {
                        i++;
                    }

                    // Apply the mapping delta for this range.
                    return range + block.rangeMap[i - 1].md;
                }),
                text: message.fix.text.replace(/\n/gu, `\n${block.baseIndentText}`)
            };
        }

        return { ...message, ...out, ...adjustedFix };
    };
}

/**
 * Excludes unsatisfiable rules from the list of messages.
 * @param {Message} message A message from the linter.
 * @returns {boolean} True if the message should be included in output.
 */
function excludeUnsatisfiableRules(message) {
    return message && !UNSATISFIABLE_RULES.has(message.ruleId);
}

/**
 * Transforms generated messages for output.
 * @param {Array<Message[]>} messages An array containing one array of messages
 *     for each code block returned from `preprocess`.
 * @param {string} filename The filename of the file
 * @returns {Message[]} A flattened array of messages with mapped locations.
 */
function postprocess(messages, filename) {
    const blocks = blocksCache.get(filename);

    blocksCache.delete(filename);

    return messages.flatMap((group, i) => {
        const adjust = adjustBlock(blocks[i]);

        return group.map(adjust).filter(excludeUnsatisfiableRules);
    });
}

module.exports = {
    meta: {
        name: `${pkg.name}/markdown`,
        version: pkg.version
    },
    preprocess,
    postprocess,
    supportsAutofix: SUPPORTS_AUTOFIX
};

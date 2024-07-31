// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

import fs from 'fs';
import path from 'path';

import highlightJs from 'highlight.js';
import raw from 'rehype-raw';
import htmlStringify from 'rehype-stringify';
import gfm from 'remark-gfm';
import markdown from 'remark-parse';
import remark2rehype from 'remark-rehype';
import { unified } from 'unified';
import { visit } from 'unist-util-visit';

import * as common from './common.mjs';
import * as typeParser from './type-parser.mjs';
import buildCSSForFlavoredJS from './buildCSSForFlavoredJS.mjs';

const dynamicSizes = { __proto__: null };

const { highlight, getLanguage } = highlightJs;

const docPath = new URL('../../doc/', import.meta.url);

// Add class attributes to index navigation links.
function navClasses() {
  return (tree) => {
    visit(tree, { type: 'element', tagName: 'a' }, (node) => {
      node.properties.class = 'nav-' +
        node.properties.href.replace('.html', '').replace(/\W+/g, '-');
    });
  };
}

const gtocPath = new URL('./api/index.md', docPath);
const gtocMD = fs.readFileSync(gtocPath, 'utf8')
  .replace(/\(([^#?]+?)\.md\)/ig, (_, filename) => `(${filename}.html)`)
  .replace(/^<!--.*?-->/gms, '');
const gtocHTML = unified()
  .use(markdown)
  .use(gfm)
  .use(remark2rehype, { allowDangerousHtml: true })
  .use(raw)
  .use(navClasses)
  .use(htmlStringify)
  .processSync(gtocMD).toString();

const templatePath = new URL('./template.html', docPath);
const template = fs.readFileSync(templatePath, 'utf8');

function processContent(content) {
  content = content.toString();
  // Increment header tag levels to avoid multiple h1 tags in a doc.
  // This means we can't already have an <h6>.
  if (content.includes('<h6>')) {
    throw new Error('Cannot increment a level 6 header');
  }
  // `++level` to convert the string to a number and increment it.
  content = content.replace(/(?<=<\/?h)[1-5](?=[^<>]*>)/g, (level) => ++level);
  // Wrap h3 tags in section tags unless they are immediately preceded by a
  // section tag. The latter happens when GFM footnotes are generated. We don't
  // want to add another section tag to the footnotes section at the end of the
  // document because that will result in an empty section element. While not an
  // HTML error, it's enough for validator.w3.org to print a warning.
  let firstTime = true;
  return content
    .replace(/(?<!<section [^>]+>)<h3/g, (heading) => {
      if (firstTime) {
        firstTime = false;
        return '<section>' + heading;
      }
      return '</section><section>' + heading;
    }) + (firstTime ? '' : '</section>');
}

export function toHTML({ input, content, filename, nodeVersion, versions }) {
  const dynamicSizesForThisFile = dynamicSizes[filename];

  filename = path.basename(filename, '.md');

  const id = filename.replace(/\W+/g, '-');

  let HTML = template.replace('__ID__', id)
                     .replace(/__FILENAME__/g, filename)
                     .replace('__SECTION__', content.section)
                     .replace(/__VERSION__/g, nodeVersion)
                     .replace(/__TOC__/g, content.toc)
                     .replace('__JS_FLAVORED_DYNAMIC_CSS__', buildCSSForFlavoredJS(dynamicSizesForThisFile))
                     .replace(/__TOC_PICKER__/g, tocPicker(id, content))
                     .replace(/__GTOC_PICKER__/g, gtocPicker(id))
                     .replace(/__GTOC__/g, gtocHTML.replace(
                       `class="nav-${id}"`, `class="nav-${id} active"`))
                     .replace('__EDIT_ON_GITHUB__', editOnGitHub(filename))
                     .replace('__CONTENT__', processContent(content));

  const docCreated = input.match(
    /<!--\s*introduced_in\s*=\s*v([0-9]+)\.([0-9]+)\.[0-9]+\s*-->/);
  if (docCreated) {
    HTML = HTML.replace('__ALTDOCS__', altDocs(filename, docCreated, versions));
  } else {
    console.error(`Failed to add alternative version links to ${filename}`);
    HTML = HTML.replace('__ALTDOCS__', '');
  }

  return HTML;
}

// Set the section name based on the first header.  Default to 'Index'.
export function firstHeader() {
  return (tree, file) => {
    let heading;
    visit(tree, (node) => {
      if (node.type === 'heading') {
        heading = node;
        return false;
      }
    });

    if (heading && heading.children.length) {
      const recursiveTextContent = (node) =>
        node.value || node.children.map(recursiveTextContent).join('');
      file.section = recursiveTextContent(heading);
    } else {
      file.section = 'Index';
    }
  };
}

// Handle general body-text replacements.
// For example, link man page references to the actual page.
export function preprocessText({ nodeVersion }) {
  return (tree) => {
    visit(tree, null, (node) => {
      if (common.isSourceLink(node.value)) {
        const [path] = node.value.match(/(?<=<!-- source_link=).*(?= -->)/);
        node.value = `<p><strong>Source Code:</strong> <a href="https://github.com/nodejs/node/blob/${nodeVersion}/${path}">${path}</a></p>`;
      } else if (node.type === 'text' && node.value) {
        const value = linkJsTypeDocs(linkManPages(node.value));
        if (value !== node.value) {
          node.type = 'html';
          node.value = value;
        }
      }
    });
  };
}

// Syscalls which appear in the docs, but which only exist in BSD / macOS.
const BSD_ONLY_SYSCALLS = new Set(['lchmod']);
const MAN_PAGE = /(^|\s)([a-z.]+)\((\d)([a-z]?)\)/gm;

// Handle references to man pages, eg "open(2)" or "lchmod(2)".
// Returns modified text, with such refs replaced with HTML links, for example
// '<a href="http://man7.org/linux/man-pages/man2/open.2.html">open(2)</a>'.
function linkManPages(text) {
  return text.replace(
    MAN_PAGE, (match, beginning, name, number, optionalCharacter) => {
      // Name consists of lowercase letters,
      // number is a single digit with an optional lowercase letter.
      const displayAs = `<code>${name}(${number}${optionalCharacter})</code>`;

      if (BSD_ONLY_SYSCALLS.has(name)) {
        return `${beginning}<a href="https://www.freebsd.org/cgi/man.cgi?query=${name}&sektion=${number}">${displayAs}</a>`;
      }

      return `${beginning}<a href="http://man7.org/linux/man-pages/man${number}/${name}.${number}${optionalCharacter}.html">${displayAs}</a>`;
    });
}

const TYPE_SIGNATURE = /\{[^}]+\}/g;
function linkJsTypeDocs(text) {
  const parts = text.split('`');

  // Handle types, for example the source Markdown might say
  // "This argument should be a {number} or {string}".
  for (let i = 0; i < parts.length; i += 2) {
    const typeMatches = parts[i].match(TYPE_SIGNATURE);
    if (typeMatches) {
      typeMatches.forEach((typeMatch) => {
        parts[i] = parts[i].replace(typeMatch, typeParser.toLink(typeMatch));
      });
    }
  }

  return parts.join('`');
}

const isJSFlavorSnippet = (node) => node.lang === 'cjs' || node.lang === 'mjs';

// Preprocess headers, stability blockquotes, and YAML blocks.
export function preprocessElements({ filename }) {
  return (tree) => {
    const STABILITY_RE = /(.*:)\s*(\d)([\s\S]*)/;
    let headingIndex = -1;
    let heading = null;

    visit(tree, null, (node, index, parent) => {
      if (node.type === 'heading') {
        headingIndex = index;
        heading = node;
      } else if (node.type === 'code') {
        if (!node.lang) {
          console.warn(
            `No language set in ${filename}, line ${node.position.start.line}`,
          );
        }
        const className = isJSFlavorSnippet(node) ?
          `language-js ${node.lang}` :
          `language-${node.lang}`;

        const highlighted =
          `<code class='${className}'>${(getLanguage(node.lang || '') ? highlight(node.value, { language: node.lang }) : node).value}</code>`;
        node.type = 'html';

        const copyButton = '<button class="copy-button">copy</button>';

        if (isJSFlavorSnippet(node)) {
          const previousNode = parent.children[index - 1] || {};
          const nextNode = parent.children[index + 1] || {};

          const charCountFirstTwoLines = Math.max(...node.value.split('\n', 2).map((str) => str.length));

          if (!isJSFlavorSnippet(previousNode) &&
              isJSFlavorSnippet(nextNode) &&
              nextNode.lang !== node.lang) {
            // Saving the highlight code as value to be added in the next node.
            node.value = highlighted;
            node.charCountFirstTwoLines = charCountFirstTwoLines;
          } else if (isJSFlavorSnippet(previousNode) &&
                     previousNode.lang !== node.lang) {
            const actualCharCount = Math.max(charCountFirstTwoLines, previousNode.charCountFirstTwoLines);
            (dynamicSizes[filename] ??= new Set()).add(actualCharCount);
            node.value = `<pre class="with-${actualCharCount}-chars">` +
              '<input class="js-flavor-toggle" type="checkbox"' +
              // If CJS comes in second, ESM should display by default.
              (node.lang === 'cjs' ? ' checked' : '') +
              ' aria-label="Show modern ES modules syntax">' +
              previousNode.value +
              highlighted +
              copyButton +
              '</pre>';
            node.lang = null;
            previousNode.value = '';
            previousNode.lang = null;
          } else {
            // Isolated JS snippet, no need to add the checkbox.
            node.value = `<pre>${highlighted} ${copyButton}</pre>`;
          }
        } else {
          node.value = `<pre>${highlighted} ${copyButton}</pre>`;
        }
      } else if (node.type === 'html' && common.isYAMLBlock(node.value)) {
        node.value = parseYAML(node.value);

      } else if (node.type === 'blockquote') {
        const paragraph = node.children[0].type === 'paragraph' &&
          node.children[0];
        const text = paragraph && paragraph.children[0].type === 'text' &&
          paragraph.children[0];
        if (text && text.value.includes('Stability:')) {
          const [, prefix, number, explication] =
            text.value.match(STABILITY_RE);

          // Stability indices are never more than 3 nodes away from their
          // heading.
          const isStabilityIndex = index - headingIndex <= 3;

          if (heading && isStabilityIndex) {
            heading.stability = number;
            headingIndex = -1;
            heading = null;
          }

          // Do not link to the section we are already in.
          const noLinking = filename.includes('documentation') &&
            heading !== null && heading.children[0].value === 'Stability index';

          // Collapse blockquote and paragraph into a single node
          node.type = 'paragraph';
          node.children.shift();
          node.children.unshift(...paragraph.children);

          // Insert div with prefix and number
          node.children.unshift({
            type: 'html',
            value: `<div class="api_stability api_stability_${number}">` +
              (noLinking ? '' :
                '<a href="documentation.html#stability-index">') +
              `${prefix} ${number}${noLinking ? '' : '</a>'}`
                .replace(/\n/g, ' '),
          });

          // Remove prefix and number from text
          text.value = explication;

          // close div
          node.children.push({ type: 'html', value: '</div>' });
        }
      }
    });
  };
}

function parseYAML(text) {
  const meta = common.extractAndParseYAML(text);
  let result = '<div class="api_metadata">\n';

  const added = { description: '' };
  const deprecated = { description: '' };
  const removed = { description: '' };

  if (meta.added) {
    added.version = meta.added;
    added.description = `<span>Added in: ${added.version.join(', ')}</span>`;
  }

  if (meta.deprecated) {
    deprecated.version = meta.deprecated;
    deprecated.description =
        `<span>Deprecated since: ${deprecated.version.join(', ')}</span>`;
  }

  if (meta.removed) {
    removed.version = meta.removed;
    removed.description = `<span>Removed in: ${removed.version.join(', ')}</span>`;
  }

  if (meta.changes.length > 0) {
    if (deprecated.description) meta.changes.push(deprecated);
    if (removed.description) meta.changes.push(removed);

    meta.changes.sort((a, b) => versionSort(a.version, b.version));
    if (added.description) meta.changes.push(added);

    result += '<details class="changelog"><summary>History</summary>\n' +
            '<table>\n<tr><th>Version</th><th>Changes</th></tr>\n';

    meta.changes.forEach((change) => {
      const description = unified()
        .use(markdown)
        .use(gfm)
        .use(remark2rehype, { allowDangerousHtml: true })
        .use(raw)
        .use(htmlStringify)
        .processSync(change.description).toString();

      const version = common.arrify(change.version).join(', ');

      result += `<tr><td>${version}</td>\n` +
                  `<td>${description}</td></tr>\n`;
    });

    result += '</table>\n</details>\n';
  } else {
    result += `${added.description}${deprecated.description}${removed.description}\n`;
  }

  if (meta.napiVersion) {
    result += `<span>N-API version: ${meta.napiVersion.join(', ')}</span>\n`;
  }

  result += '</div>';
  return result;
}

function minVersion(a) {
  return common.arrify(a).reduce((min, e) => {
    return !min || versionSort(min, e) < 0 ? e : min;
  });
}

const numberRe = /^\d*/;
function versionSort(a, b) {
  a = minVersion(a).trim();
  b = minVersion(b).trim();
  let i = 0; // Common prefix length.
  while (i < a.length && i < b.length && a[i] === b[i]) i++;
  a = a.substring(i);
  b = b.substring(i);
  return +b.match(numberRe)[0] - +a.match(numberRe)[0];
}

const DEPRECATION_HEADING_PATTERN = /^DEP\d+:/;
export function buildToc({ filename, apilinks }) {
  return (tree, file) => {
    const idCounters = { __proto__: null };
    const legacyIdCounters = { __proto__: null };
    let toc = '';
    let depth = 0;

    visit(tree, null, (node) => {
      if (node.type !== 'heading') return;

      if (node.depth - depth > 1) {
        throw new Error(
          `Inappropriate heading level:\n${JSON.stringify(node)}`,
        );
      }

      depth = node.depth;
      const realFilename = path.basename(filename, '.md');
      const headingText = file.value.slice(
        node.children[0].position.start.offset,
        node.position.end.offset).trim();
      const id = getId(headingText, idCounters);
      // Use previous ID generator to create alias
      const legacyId = getLegacyId(`${realFilename}_${headingText}`, legacyIdCounters);

      const isDeprecationHeading =
        DEPRECATION_HEADING_PATTERN.test(headingText);
      if (isDeprecationHeading) {
        if (!node.data) node.data = {};
        if (!node.data.hProperties) node.data.hProperties = {};
        node.data.hProperties.id =
          headingText.substring(0, headingText.indexOf(':'));
      }

      const hasStability = node.stability !== undefined;
      toc += ' '.repeat((depth - 1) * 2) +
        (hasStability ? `* <span class="stability_${node.stability}">` : '* ') +
        `<a href="#${isDeprecationHeading ? node.data.hProperties.id : id}">${headingText}</a>${hasStability ? '</span>' : ''}\n`;

      let anchor =
         `<span><a class="mark" href="#${id}" id="${id}">#</a></span>`;

      // Add alias anchor to preserve old links
      anchor += `<a aria-hidden="true" class="legacy" id="${legacyId}"></a>`;

      if (realFilename === 'errors' && headingText.startsWith('ERR_')) {
        anchor +=
          `<span><a class="mark" href="#${headingText}" id="${headingText}">#</a></span>`;
      }

      const api = headingText.replace(/^.*:\s+/, '').replace(/\(.*/, '');
      if (apilinks[api]) {
        anchor = `<a class="srclink" href=${apilinks[api]}>[src]</a>${anchor}`;
      }

      node.children.push({ type: 'html', value: anchor });
    });

    if (toc !== '') {
      const inner = unified()
        .use(markdown)
        .use(gfm)
        .use(remark2rehype, { allowDangerousHtml: true })
        .use(raw)
        .use(htmlStringify)
        .processSync(toc).toString();

      file.toc = `<details role="navigation" id="toc" open><summary>Table of contents</summary>${inner}</details>`;
      file.tocPicker = `<div class="toc">${inner}</div>`;
    } else {
      file.toc = file.tocPicker = '<!-- TOC -->';
    }
  };
}

// ID generator that mirrors Github's heading anchor parser
const punctuation = /[^\w\- ]/g;
function getId(text, idCounters) {
  text = text.toLowerCase()
             .replace(punctuation, '')
             .replace(/ /g, '-');
  if (idCounters[text] !== undefined) {
    return `${text}_${++idCounters[text]}`;
  }
  idCounters[text] = 0;
  return text;
}

// This ID generator is purely to generate aliases
// so we can preserve old doc links
const notAlphaNumerics = /[^a-z0-9]+/g;
const edgeUnderscores = /^_+|_+$/g;
const notAlphaStart = /^[^a-z]/;
function getLegacyId(text, idCounters) {
  text = text.toLowerCase()
             .replace(notAlphaNumerics, '_')
             .replace(edgeUnderscores, '')
             .replace(notAlphaStart, '_$&');
  if (idCounters[text] !== undefined) {
    return `${text}_${++idCounters[text]}`;
  }
  idCounters[text] = 0;
  return text;
}

function altDocs(filename, docCreated, versions) {
  const [, docCreatedMajor, docCreatedMinor] = docCreated.map(Number);
  const host = 'https://nodejs.org';

  const getHref = (versionNum) =>
    `${host}/docs/latest-v${versionNum}/api/${filename}.html`;

  const wrapInListItem = (version) =>
    `<li><a href="${getHref(version.num)}">${version.num}${version.lts ? ' <b>LTS</b>' : ''}</a></li>`;

  function isDocInVersion(version) {
    const [versionMajor, versionMinor] = version.num.split('.').map(Number);
    if (docCreatedMajor > versionMajor) return false;
    if (docCreatedMajor < versionMajor) return true;
    if (Number.isNaN(versionMinor)) return true;
    return docCreatedMinor <= versionMinor;
  }

  const list = versions.filter(isDocInVersion).map(wrapInListItem).join('\n');

  return list ? `
    <li class="picker-header">
      <a href="#">
        <span class="picker-arrow"></span>
        Other versions
      </a>
      <div class="picker"><ol id="alt-docs">${list}</ol></div>
    </li>
  ` : '';
}

function editOnGitHub(filename) {
  return `<li class="edit_on_github"><a href="https://github.com/nodejs/node/edit/main/doc/api/${filename}.md">Edit on GitHub</a></li>`;
}

function gtocPicker(id) {
  if (id === 'index') {
    return '';
  }

  // Highlight the current module and add a link to the index
  const gtoc = gtocHTML.replace(
    `class="nav-${id}"`, `class="nav-${id} active"`,
  ).replace('</ul>', `
      <li>
        <a href="index.html">Index</a>
      </li>
    </ul>
  `);

  return `
    <li class="picker-header">
      <a href="#">
        <span class="picker-arrow"></span>
        Index
      </a>

      <div class="picker">${gtoc}</div>
    </li>
  `;
}

function tocPicker(id, content) {
  if (id === 'index') {
    return '';
  }

  return `
    <li class="picker-header">
      <a href="#">
        <span class="picker-arrow"></span>
        Table of contents
      </a>

      <div class="picker">${content.tocPicker}</div>
    </li>
  `;
}

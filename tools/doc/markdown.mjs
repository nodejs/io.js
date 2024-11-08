import { visit } from 'unist-util-visit';

export const referenceToLocalMdFile = /^(?![+a-z]+:)([^#?]+)\.md(#.+)?$/i;

export function replaceLinks({ filename, linksMapper }) {
  return (tree) => {
    const fileHtmlUrls = linksMapper[filename];

    visit(tree, (node) => {
      node.url &&= node.url.replace(
        referenceToLocalMdFile,
        (_, filename, hash) => `${filename}.html${hash || ''}`,
      );
    });
    visit(tree, 'definition', (node) => {
      const htmlUrl = fileHtmlUrls?.[node.identifier];

      if (htmlUrl && typeof htmlUrl === 'string') {
        node.url = htmlUrl;
      }
    });
  };
}

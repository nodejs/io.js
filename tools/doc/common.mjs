import yaml from 'js-yaml';

export function isYAMLBlock(text) {
  return /^<!-- YAML/.test(text);
}

export function isSourceLink(text) {
  return /^<!-- source_link=([^\s/]+\/)+\w+\.\w+ -->/.test(text);
}

export function arrify(value) {
  return Array.isArray(value) ? value : [value];
}

export function extractAndParseYAML(text) {
  text = text.trim()
             .replace(/^<!-- YAML/, '')
             .replace(/-->$/, '');

  // js-yaml.load() throws on error.
  const meta = yaml.load(text);

  // Since semver-minors can trickle down to previous major versions,
  // features may have been added in multiple versions.
  meta.added &&= arrify(meta.added);

  meta.napiVersion &&= arrify(meta.napiVersion);

  // Treat deprecated like added for consistency.
  meta.deprecated &&= arrify(meta.deprecated);

  meta.removed &&= arrify(meta.removed);

  meta.changes ||= [];

  return meta;
}

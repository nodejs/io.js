// Script to update certdata.txt from NSS.
import { execFileSync } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import { createWriteStream } from 'node:fs';
import { basename, join, relative } from 'node:path';
import { Readable } from 'node:stream';
import { pipeline } from 'node:stream/promises';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';

const __filename = fileURLToPath(import.meta.url);

const getCertdataURL = (version) => {
  const tag = `NSS_${version.replaceAll('.', '_')}_RTM`;
  const certdataURL = `https://raw.githubusercontent.com/nss-dev/nss/refs/tags/${tag}/lib/ckfw/builtins/certdata.txt`;
  return certdataURL;
};

const getFirefoxReleases = async (everything = false) => {
  const releaseDataURL = `https://nucleus.mozilla.org/rna/all-releases.json${everything ? '?all=true' : ''}`;
  if (values.verbose) {
    console.log(`Fetching Firefox release data from ${releaseDataURL}.`);
  }
  const releaseData = await fetch(releaseDataURL);
  if (!releaseData.ok) {
    console.error(`Failed to fetch ${releaseDataURL}: ${releaseData.status}: ${releaseData.statusText}.`);
    process.exit(-1);
  }
  return (await releaseData.json()).filter((release) => {
    // We're only interested in public releases of Firefox.
    return (release.product === 'Firefox' && release.channel === 'Release' && release.is_public === true);
  }).sort((a, b) => {
    // Sort results by release date.
    return new Date(b.release_date) - new Date(a.release_date);
  });
};

const getFirefoxRelease = async (version) => {
  let releases = await getFirefoxReleases();
  let found;
  if (version === undefined) {
    // No version specified. Find the most recent.
    if (releases.length > 0) {
      found = releases[0];
    } else {
      if (values.verbose) {
        console.log('Unable to find release data for Firefox. Searching full release data.');
      }
      releases = await getFirefoxReleases(true);
      found = releases[0];
    }
  } else {
    // Search for the specified release.
    found = releases.find((release) => release.version === version);
    if (found === undefined) {
      if (values.verbose) {
        console.log(`Unable to find release data for Firefox ${version}. Searching full release data.`);
      }
      releases = await getFirefoxReleases(true);
      found = releases.find((release) => release.version === version);
    }
  }
  return found;
};

const getNSSVersion = async (release) => {
  const latestFirefox = release.version;
  const firefoxTag = `FIREFOX_${latestFirefox.replace('.', '_')}_RELEASE`;
  const tagInfoURL = `https://hg.mozilla.org/releases/mozilla-release/raw-file/${firefoxTag}/security/nss/TAG-INFO`;
  if (values.verbose) {
    console.log(`Fetching NSS tag from ${tagInfoURL}.`);
  }
  const tagInfo = await fetch(tagInfoURL);
  if (!tagInfo.ok) {
    console.error(`Failed to fetch ${tagInfoURL}: ${tagInfo.status}: ${tagInfo.statusText}`);
  }
  const tag = await tagInfo.text();
  if (values.verbose) {
    console.log(`Found tag ${tag}.`);
  }
  // Tag will be of form `NSS_x_y_RTM`. Convert to `x.y`.
  return tag.split('_').slice(1, -1).join('.');
};

const options = {
  help: {
    type: 'boolean',
  },
  file: {
    short: 'f',
    type: 'string',
  },
  verbose: {
    short: 'v',
    type: 'boolean',
  },
};
const {
  positionals,
  values,
} = parseArgs({
  allowPositionals: true,
  options,
});

if (values.help) {
  console.log(`Usage: ${basename(__filename)} [OPTION]... [RELEASE]...`);
  console.log();
  console.log('Updates certdata.txt to NSS version contained in Firefox RELEASE (default: most recent release).');
  console.log('');
  console.log('  -f, --file=FILE  writes a commit message reflecting the change to the');
  console.log('                     specified FILE');
  console.log('  -v, --verbose    writes progress to stdout');
  console.log('      --help       display this help and exit');
  process.exit(0);
}

const firefoxRelease = await getFirefoxRelease(positionals[0]);
// Retrieve metadata for the NSS release being updated to.
const version = await getNSSVersion(firefoxRelease);
if (values.verbose) {
  console.log(`Updating to NSS version ${version}`);
}

// Fetch certdata.txt and overwrite the local copy.
const certdataURL = getCertdataURL(version);
if (values.verbose) {
  console.log(`Fetching ${certdataURL}`);
}
const checkoutDir = join(__filename, '..', '..', '..');
const certdata = await fetch(certdataURL);
const certdataFile = join(checkoutDir, 'tools', 'certdata.txt');
if (!certdata.ok) {
  console.error(`Failed to fetch ${certdataURL}: ${certdata.status}: ${certdata.statusText}`);
  process.exit(-1);
}
if (values.verbose) {
  console.log(`Writing ${certdataFile}`);
}
await pipeline(certdata.body, createWriteStream(certdataFile));

// Run tools/mk-ca-bundle.pl to generate src/node_root_certs.h.
if (values.verbose) {
  console.log('Running tools/mk-ca-bundle.pl');
}
const opts = { encoding: 'utf8' };
const mkCABundleTool = join(checkoutDir, 'tools', 'mk-ca-bundle.pl');
const mkCABundleOut = execFileSync(mkCABundleTool,
                                   values.verbose ? [ '-v' ] : [],
                                   opts);
if (values.verbose) {
  console.log(mkCABundleOut);
}

// Determine certificates added and/or removed.
const certHeaderFile = relative(process.cwd(), join(checkoutDir, 'src', 'node_root_certs.h'));
const diff = execFileSync('git', [ 'diff-files', '-u', '--', certHeaderFile ], opts);
if (values.verbose) {
  console.log(diff);
}
const certsAddedRE = /^\+\/\* (.*) \*\//gm;
const certsRemovedRE = /^-\/\* (.*) \*\//gm;
const added = [ ...diff.matchAll(certsAddedRE) ].map((m) => m[1]);
const removed = [ ...diff.matchAll(certsRemovedRE) ].map((m) => m[1]);

const commitMsg = [
  `crypto: update root certificates to NSS ${version}`,
  '',
  `This is the certdata.txt[0] from NSS ${version}.`,
  '',
];
if (firefoxRelease) {
  commitMsg.push(`This is the version of NSS that shipped in Firefox ${firefoxRelease.version} on ${firefoxRelease.release_date}.`);
  commitMsg.push('');
}
if (added.length > 0) {
  commitMsg.push('Certificates added:');
  commitMsg.push(...added.map((cert) => `- ${cert}`));
  commitMsg.push('');
}
if (removed.length > 0) {
  commitMsg.push('Certificates removed:');
  commitMsg.push(...removed.map((cert) => `- ${cert}`));
  commitMsg.push('');
}
commitMsg.push(`[0] ${certdataURL}`);
const delimiter = randomUUID();
const properties = [
  `NEW_VERSION=${version}`,
  `COMMIT_MSG<<${delimiter}`,
  ...commitMsg,
  delimiter,
  '',
].join('\n');
if (values.verbose) {
  console.log(properties);
}
const propertyFile = values.file;
if (propertyFile !== undefined) {
  console.log(`Writing to ${propertyFile}`);
  await pipeline(Readable.from(properties), createWriteStream(propertyFile));
}

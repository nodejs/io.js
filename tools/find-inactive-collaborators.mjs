#!/usr/bin/env node

// Identify inactive collaborators. "Inactive" is not quite right, as the things
// this checks for are not the entirety of collaborator activities. Still, it is
// a pretty good proxy. Feel free to suggest or implement further metrics.

import cp from 'node:child_process';
import fs from 'node:fs';
import readline from 'node:readline';
import { parseArgs } from 'node:util';

const args = parseArgs({
  allowPositionals: true,
  options: { verbose: { type: 'boolean', short: 'v' } },
});

const verbose = args.values.verbose;
const SINCE = args.positionals[0] || '12 months ago';

async function runGitCommand(cmd, mapFn) {
  const childProcess = cp.spawn('/bin/sh', ['-c', cmd], {
    cwd: new URL('..', import.meta.url),
    encoding: 'utf8',
    stdio: ['inherit', 'pipe', 'inherit'],
  });
  const lines = readline.createInterface({
    input: childProcess.stdout,
  });
  const errorHandler = new Promise(
    (_, reject) => childProcess.on('error', reject),
  );
  let returnValue = mapFn ? new Set() : '';
  await Promise.race([errorHandler, Promise.resolve()]);
  // If no mapFn, return the value. If there is a mapFn, use it to make a Set to
  // return.
  for await (const line of lines) {
    await Promise.race([errorHandler, Promise.resolve()]);
    if (mapFn) {
      const val = mapFn(line);
      if (val) {
        returnValue.add(val);
      }
    } else {
      returnValue += line;
    }
  }
  return Promise.race([errorHandler, Promise.resolve(returnValue)]);
}

// Get all commit contributors during the time period.
const contributors = await runGitCommand(
  `git log --pretty='format:%aN <%aE>%n%(trailers:only,valueonly,key=Co-authored-by)%n%(trailers:only,valueonly,key=Reviewed-by)' --since="${SINCE}" HEAD`,
  String,
);

async function getCollaboratorsFromReadme() {
  const readmeText = readline.createInterface({
    input: fs.createReadStream(new URL('../README.md', import.meta.url)),
    crlfDelay: Infinity,
  });
  const returnedArray = [];
  let foundCollaboratorHeading = false;
  for await (const line of readmeText) {
    // If we've found the collaborator heading already, stop processing at the
    // next heading.
    if (foundCollaboratorHeading && line.startsWith('#')) {
      break;
    }

    const isCollaborator = foundCollaboratorHeading && line.length;

    if (line === '### Collaborators') {
      foundCollaboratorHeading = true;
    }
    if (line.startsWith('  **') && isCollaborator) {
      const [, name, email] = /^ {2}\*\*([^*]+)\*\* <<(.+)>>/.exec(line);
      const mailmap = await runGitCommand(
        `git check-mailmap '${name} <${email}>'`,
      );
      if (mailmap !== `${name} <${email}>`) {
        console.log(`README entry for Collaborator does not match mailmap:\n  ${name} <${email}> => ${mailmap}`);
      }
      returnedArray.push({
        name,
        email,
        mailmap,
      });
    }
  }

  if (!foundCollaboratorHeading) {
    throw new Error('Could not find Collaborator section of README');
  }

  return returnedArray;
}

async function moveCollaboratorToEmeritus(peopleToMove) {
  const readmeText = readline.createInterface({
    input: fs.createReadStream(new URL('../README.md', import.meta.url)),
    crlfDelay: Infinity,
  });
  let fileContents = '';
  let inCollaboratorsSection = false;
  let inCollaboratorEmeritusSection = false;
  let collaboratorFirstLine = '';
  const textToMove = [];
  for await (const line of readmeText) {
    // If we've been processing collaborator emeriti and we reach the end of
    // the list, print out the remaining entries to be moved because they come
    // alphabetically after the last item.
    if (inCollaboratorEmeritusSection && line === '' &&
        fileContents.endsWith('>\n')) {
      while (textToMove.length) {
        fileContents += textToMove.pop();
      }
    }

    // If we've found the collaborator heading already, stop processing at the
    // next heading.
    if (line.startsWith('#')) {
      inCollaboratorsSection = false;
      inCollaboratorEmeritusSection = false;
    }

    const isCollaborator = inCollaboratorsSection && line.length;
    const isCollaboratorEmeritus = inCollaboratorEmeritusSection && line.length;

    if (line === '### Collaborators') {
      inCollaboratorsSection = true;
    }
    if (line === '### Collaborator emeriti') {
      inCollaboratorEmeritusSection = true;
    }

    if (isCollaborator) {
      if (line.startsWith('* ')) {
        collaboratorFirstLine = line;
      } else if (line.startsWith('  **')) {
        const [, name, email] = /^ {2}\*\*([^*]+)\*\* <<(.+)>>/.exec(line);
        if (peopleToMove.some((entry) => {
          return entry.name === name && entry.email === email;
        })) {
          textToMove.push(`${collaboratorFirstLine}\n${line}\n`);
        } else {
          fileContents += `${collaboratorFirstLine}\n${line}\n`;
        }
      } else {
        fileContents += `${line}\n`;
      }
    }

    if (isCollaboratorEmeritus) {
      if (line.startsWith('* ')) {
        collaboratorFirstLine = line;
      } else if (line.startsWith('  **')) {
        const currentLine = `${collaboratorFirstLine}\n${line}\n`;
        // If textToMove is empty, this still works because when undefined is
        // used in a comparison with <, the result is always false.
        while (textToMove[0]?.toLowerCase() < currentLine.toLowerCase()) {
          fileContents += textToMove.shift();
        }
        fileContents += currentLine;
      } else {
        fileContents += `${line}\n`;
      }
    }

    if (!isCollaborator && !isCollaboratorEmeritus) {
      fileContents += `${line}\n`;
    }
  }

  return fileContents;
}

// Get list of current collaborators from README.md.
const collaborators = await getCollaboratorsFromReadme();

if (verbose) {
  console.log(`Since ${SINCE}:\n`);
  console.log(`* ${contributors.size.toLocaleString()} contributors`);
  console.log(`* ${collaborators.length.toLocaleString()} collaborators currently in the project.`);
}
const inactive = collaborators.filter((collaborator) =>
  !contributors.has(collaborator.mailmap),
);

if (inactive.length) {
  console.log('\nInactive collaborators:\n');
  console.log(inactive.map((entry) => `* ${entry.name}`).join('\n'));
  if (process.env.GITHUB_ACTIONS) {
    console.log('\nGenerating new README.md file...');
    const newReadmeText = await moveCollaboratorToEmeritus(inactive);
    fs.writeFileSync(new URL('../README.md', import.meta.url), newReadmeText);
  }
}

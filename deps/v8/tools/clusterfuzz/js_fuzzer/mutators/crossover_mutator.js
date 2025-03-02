// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Expression mutator.
 */

'use strict';

const babelTemplate = require('@babel/template').default;
const babelTraverse = require('@babel/traverse').default;
const babelTypes = require('@babel/types');
const babylon = require('@babel/parser');

const common = require('./common.js');
const random = require('../random.js');
const mutator = require('./mutator.js');
const sourceHelpers = require('../source_helpers.js');
const tryCatch = require('./try_catch.js');

const BABYLON_RELAXED_SUPER_OPTIONS = Object.assign(
    {}, sourceHelpers.BABYLON_OPTIONS);
BABYLON_RELAXED_SUPER_OPTIONS['allowSuperOutsideMethod'] = true;

// Drop desired try-catch wrapping only with a small probability.
const WRAP_TC_IF_NEEDED_PROB = 0.8;

/**
 * Check if a snippet containing `super` can be inserted at `path`
 * without creating a syntax error.
 *
 * Syntax errors would be raised if super is used within a class that
 * doesn't have a superclass or if a super() call is used outside of
 * a constructor. Member expressions like super.x are allowed.
 */
function validateSuper(path, source) {
  const ast = babylon.parse(source, BABYLON_RELAXED_SUPER_OPTIONS);

  // TODO(389069288): The DB currently only stores if a snippet contains
  // super. We could move this further differentiation into the DB.
  // But since it's rarely used, we just re-parse the snippet here on the fly
  // for now.
  // Furthermore: Some snippets might actually provide a self-contained
  // `super`, properly enclosed by an extending class. For these we could be
  // more permissive, but they are rare.
  let call = false;
  babelTraverse(ast, {
    Super(path) {
      if (babelTypes.isCallExpression(path.parent)) {
        call = true;
      }
    },
  });

  const surroundingMethod = path.findParent(x => x.isClassMethod());
  const surroundingClass = surroundingMethod.findParent(x => x.isClass());
  return surroundingClass && surroundingClass.node.superClass &&
         (!call || surroundingMethod.node.kind == 'constructor');
}

/**
 * Checks if the expression contains a member expression with an identifier.
 *
 * These get extra try-catch wrapping as such accesses are often wrong out of
 * context. The try-catch mutator doesn't always wrap all expressions.
 */
function needsTryCatch(source) {
  const ast = babylon.parse(source, BABYLON_RELAXED_SUPER_OPTIONS);

  // TODO(389069288): We could precalculate this and store it in the DB
  // together with the snippets.
  let member = false;
  babelTraverse(ast, {
    MemberExpression(path) {
      if (babelTypes.isIdentifier(path.node.property)) {
        member = true;
      }
    },
  });
  return member;
}

class CrossOverMutator extends mutator.Mutator {
  constructor(settings, db) {
    super();
    this.settings = settings;
    this.db = db;
  }

  get visitor() {
    const thisMutator = this;

    return [{
      ExpressionStatement(path) {
        if (!random.choose(thisMutator.settings.MUTATE_CROSSOVER_INSERT)) {
          return;
        }

        const canHaveSuper = Boolean(path.findParent(x => x.isClassMethod()));
        const randomExpression = thisMutator.db.getRandomStatement(
            {canHaveSuper: canHaveSuper});

        if (randomExpression.needsSuper &&
            !validateSuper(path, randomExpression.source)) {
          return;
        }

        // Insert the statement.
        let toInsert = babelTemplate(
            randomExpression.source,
            sourceHelpers.BABYLON_REPLACE_VAR_OPTIONS);
        const dependencies = {};

        if (randomExpression.dependencies) {
          const variables = common.availableVariables(path);
          if (!variables.length) {
            return;
          }
          for (const dependency of randomExpression.dependencies) {
            dependencies[dependency] = random.single(variables);
          }
        }

        try {
          toInsert = toInsert(dependencies);
        } catch (e) {
          if (thisMutator.settings.testing) {
            // Fail early in tests.
            throw e;
          }
          console.log('ERROR: Failed to parse:', randomExpression.source);
          console.log(e);
          return;
        }

        if (random.choose(WRAP_TC_IF_NEEDED_PROB) &&
            needsTryCatch(randomExpression.source)) {
          toInsert = tryCatch.wrapTryCatch(toInsert);
        }

        thisMutator.annotate(
            toInsert,
            'Crossover from ' + randomExpression.originalPath);

        if (random.choose(0.5)) {
          thisMutator.insertBeforeSkip(path, toInsert);
        } else {
          thisMutator.insertAfterSkip(path, toInsert);
        }

        path.skip();
      },
    }, {
    }];
  }
}

module.exports = {
  CrossOverMutator: CrossOverMutator,
};

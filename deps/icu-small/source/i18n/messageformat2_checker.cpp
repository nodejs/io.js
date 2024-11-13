// © 2024 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#if !UCONFIG_NO_MF2

#include "messageformat2_allocation.h"
#include "messageformat2_checker.h"
#include "messageformat2_macros.h"
#include "uvector.h" // U_ASSERT

U_NAMESPACE_BEGIN

namespace message2 {

/*
Checks data model errors
(see https://github.com/unicode-org/message-format-wg/blob/main/spec/formatting.md#error-handling )

The following are checked here:
Variant Key Mismatch
Duplicate Variant
Missing Fallback Variant (called NonexhaustivePattern here)
Missing Selector Annotation
Duplicate Declaration
  - Most duplicate declaration errors are checked by the parser,
    but the checker checks for declarations of input variables
    that were previously implicitly declared
(Duplicate option names and duplicate declarations are checked by the parser)
*/

// Type environments
// -----------------

TypeEnvironment::TypeEnvironment(UErrorCode& status) {
    CHECK_ERROR(status);

    UVector* temp;
    temp = createStringVectorNoAdopt(status);
    CHECK_ERROR(status);
    annotated.adoptInstead(temp);
    temp = createStringVectorNoAdopt(status);
    CHECK_ERROR(status);
    unannotated.adoptInstead(temp);
    temp = createStringVectorNoAdopt(status);
    CHECK_ERROR(status);
    freeVars.adoptInstead(temp);
}

 static bool has(const UVector& v, const VariableName& var) {
     return v.contains(const_cast<void*>(static_cast<const void*>(&var)));
 }

// Returns true if `var` was either previously used (implicit declaration),
// or is in scope by an explicit declaration
bool TypeEnvironment::known(const VariableName& var) const {
    return has(*annotated, var) || has(*unannotated, var) || has(*freeVars, var);
}

TypeEnvironment::Type TypeEnvironment::get(const VariableName& var) const {
    U_ASSERT(annotated.isValid());
    if (has(*annotated, var)) {
        return Annotated;
    }
    U_ASSERT(unannotated.isValid());
    if (has(*unannotated, var)) {
        return Unannotated;
    }
    U_ASSERT(freeVars.isValid());
    if (has(*freeVars, var)) {
        return FreeVariable;
    }
    // This case is a "free variable without an implicit declaration",
    // i.e. one used only in a selector expression and not in a declaration RHS
    return Unannotated;
}

void TypeEnvironment::extend(const VariableName& var, TypeEnvironment::Type t, UErrorCode& status) {
    if (t == Unannotated) {
        U_ASSERT(unannotated.isValid());
        // See comment below
        unannotated->addElement(const_cast<void*>(static_cast<const void*>(&var)), status);
        return;
    }

    if (t == FreeVariable) {
        U_ASSERT(freeVars.isValid());
        // See comment below
        freeVars->addElement(const_cast<void*>(static_cast<const void*>(&var)), status);
        return;
    }

    U_ASSERT(annotated.isValid());
    // This is safe because elements of `annotated` are never written
    // and the lifetime of `var` is guaranteed to include the lifetime of
    // `annotated`
    annotated->addElement(const_cast<void*>(static_cast<const void*>(&var)), status);
}

TypeEnvironment::~TypeEnvironment() {}

// ---------------------

static bool areDefaultKeys(const Key* keys, int32_t len) {
    U_ASSERT(len > 0);
    for (int32_t i = 0; i < len; i++) {
        if (!keys[i].isWildcard()) {
            return false;
        }
    }
    return true;
}

void Checker::addFreeVars(TypeEnvironment& t, const Operand& rand, UErrorCode& status) {
    CHECK_ERROR(status);

    if (rand.isVariable()) {
        const VariableName& v = rand.asVariable();
        if (!t.known(v)) {
            t.extend(v, TypeEnvironment::Type::FreeVariable, status);
        }
    }
}

void Checker::addFreeVars(TypeEnvironment& t, const OptionMap& opts, UErrorCode& status) {
    for (int32_t i = 0; i < opts.size(); i++) {
        const Option& o = opts.getOption(i, status);
        CHECK_ERROR(status);
        addFreeVars(t, o.getValue(), status);
    }
}

void Checker::addFreeVars(TypeEnvironment& t, const Operator& rator, UErrorCode& status) {
    CHECK_ERROR(status);

    addFreeVars(t, rator.getOptionsInternal(), status);
}

void Checker::addFreeVars(TypeEnvironment& t, const Expression& rhs, UErrorCode& status) {
    CHECK_ERROR(status);

    if (rhs.isFunctionCall()) {
        const Operator* rator = rhs.getOperator(status);
        U_ASSERT(U_SUCCESS(status));
        addFreeVars(t, *rator, status);
    }
    addFreeVars(t, rhs.getOperand(), status);
}

void Checker::checkVariants(UErrorCode& status) {
    CHECK_ERROR(status);

    U_ASSERT(!dataModel.hasPattern());

    // Check that each variant has a key list with size
    // equal to the number of selectors
    const Variant* variants = dataModel.getVariantsInternal();

    // Check that one variant includes only wildcards
    bool defaultExists = false;
    bool duplicatesExist = false;

    for (int32_t i = 0; i < dataModel.numVariants(); i++) {
        const SelectorKeys& k = variants[i].getKeys();
        const Key* keys = k.getKeysInternal();
        int32_t len = k.len;
        if (len != dataModel.numSelectors()) {
            // Variant key mismatch
            errors.addError(StaticErrorType::VariantKeyMismatchError, status);
            return;
        }
        defaultExists |= areDefaultKeys(keys, len);

        // Check if this variant's keys are duplicated by any other variant's keys
        if (!duplicatesExist) {
            // This check takes quadratic time, but it can be optimized if checking
            // this property turns out to be a bottleneck.
            for (int32_t j = 0; j < i; j++) {
                const SelectorKeys& k1 = variants[j].getKeys();
                const Key* keys1 = k1.getKeysInternal();
                bool allEqual = true;
                // This variant was already checked,
                // so we know keys1.len == len
                for (int32_t kk = 0; kk < len; kk++) {
                    if (!(keys[kk] == keys1[kk])) {
                        allEqual = false;
                        break;
                    }
                }
                if (allEqual) {
                    duplicatesExist = true;
                }
            }
        }
    }

    if (duplicatesExist) {
        errors.addError(StaticErrorType::DuplicateVariant, status);
    }
    if (!defaultExists) {
        errors.addError(StaticErrorType::NonexhaustivePattern, status);
    }
}

void Checker::requireAnnotated(const TypeEnvironment& t, const Expression& selectorExpr, UErrorCode& status) {
    CHECK_ERROR(status);

    if (selectorExpr.isFunctionCall()) {
        return; // No error
    }
    const Operand& rand = selectorExpr.getOperand();
    if (rand.isVariable()) {
        if (t.get(rand.asVariable()) == TypeEnvironment::Type::Annotated) {
            return; // No error
        }
    }
    // If this code is reached, an error was detected
    errors.addError(StaticErrorType::MissingSelectorAnnotation, status);
}

void Checker::checkSelectors(const TypeEnvironment& t, UErrorCode& status) {
    U_ASSERT(!dataModel.hasPattern());

    // Check each selector; if it's not annotated, emit a
    // "missing selector annotation" error
    const Expression* selectors = dataModel.getSelectorsInternal();
    for (int32_t i = 0; i < dataModel.numSelectors(); i++) {
        requireAnnotated(t, selectors[i], status);
    }
}

TypeEnvironment::Type typeOf(TypeEnvironment& t, const Expression& expr) {
    if (expr.isFunctionCall()) {
        return TypeEnvironment::Type::Annotated;
    }
    const Operand& rand = expr.getOperand();
    U_ASSERT(!rand.isNull());
    if (rand.isLiteral()) {
        return TypeEnvironment::Type::Unannotated;
    }
    U_ASSERT(rand.isVariable());
    return t.get(rand.asVariable());
}

void Checker::checkDeclarations(TypeEnvironment& t, UErrorCode& status) {
    CHECK_ERROR(status);

    // For each declaration, extend the type environment with its type
    // Only a very simple type system is necessary: variables
    // have the type "annotated", "unannotated", or "free".
    // For "missing selector annotation" checking, free variables
    // (message arguments) are treated as unannotated.
    // Free variables are also used for checking duplicate declarations.
    const Binding* env = dataModel.getLocalVariablesInternal();
    for (int32_t i = 0; i < dataModel.bindingsLen; i++) {
        const Binding& b = env[i];
        const VariableName& lhs = b.getVariable();
        const Expression& rhs = b.getValue();

        // First, add free variables from the RHS of b
        // This must be done first so we can catch:
        // .local $foo = {$foo}
        // (where the RHS is the first use of $foo)
        if (b.isLocal()) {
            addFreeVars(t, rhs, status);

            // Next, check if the LHS equals any free variables
            // whose implicit declarations are in scope
            if (t.known(lhs) && t.get(lhs) == TypeEnvironment::Type::FreeVariable) {
                errors.addError(StaticErrorType::DuplicateDeclarationError, status);
            }
        } else {
            // Input declaration; if b has no annotation, there's nothing to check
            if (!b.isLocal() && b.hasAnnotation()) {
                const OptionMap& opts = b.getOptionsInternal();
                // For .input declarations, we just need to add any variables
                // referenced in the options
                addFreeVars(t, opts, status);
             }
            // Next, check if the LHS equals any free variables
            // whose implicit declarations are in scope
            if (t.known(lhs) && t.get(lhs) == TypeEnvironment::Type::FreeVariable) {
                errors.addError(StaticErrorType::DuplicateDeclarationError, status);
            }
        }
        // Next, extend the type environment with a binding from lhs to its type
        t.extend(lhs, typeOf(t, rhs), status);
    }
}

void Checker::check(UErrorCode& status) {
    CHECK_ERROR(status);

    TypeEnvironment typeEnv(status);
    checkDeclarations(typeEnv, status);
    // Pattern message
    if (dataModel.hasPattern()) {
        return;
    } else {
      // Selectors message
      checkSelectors(typeEnv, status);
      checkVariants(status);
    }
}

} // namespace message2
U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_MF2 */

#endif /* #if !UCONFIG_NO_FORMATTING */

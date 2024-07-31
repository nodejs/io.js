// © 2024 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#if !UCONFIG_NO_MF2

#include "messageformat2_allocation.h"
#include "messageformat2_errors.h"
#include "messageformat2_macros.h"
#include "uvector.h" // U_ASSERT

U_NAMESPACE_BEGIN

namespace message2 {

    // Errors
    // -----------

    void DynamicErrors::setReservedError(UErrorCode& status) {
        addError(DynamicError(DynamicErrorType::ReservedError), status);
    }

    void DynamicErrors::setFormattingError(const FunctionName& formatterName, UErrorCode& status) {
        addError(DynamicError(DynamicErrorType::FormattingError, formatterName), status);
    }

    void DynamicErrors::setFormattingError(UErrorCode& status) {
        addError(DynamicError(DynamicErrorType::FormattingError, UnicodeString("unknown formatter")), status);
    }

    void DynamicErrors::setOperandMismatchError(const FunctionName& formatterName, UErrorCode& status) {
        addError(DynamicError(DynamicErrorType::OperandMismatchError, formatterName), status);
    }

    void StaticErrors::setDuplicateOptionName(UErrorCode& status) {
        addError(StaticError(StaticErrorType::DuplicateOptionName), status);
    }

    void StaticErrors::setMissingSelectorAnnotation(UErrorCode& status) {
        addError(StaticError(StaticErrorType::MissingSelectorAnnotation), status);
    }

    void DynamicErrors::setSelectorError(const FunctionName& selectorName, UErrorCode& status) {
        addError(DynamicError(DynamicErrorType::SelectorError, selectorName), status);
    }

    void DynamicErrors::setUnknownFunction(const FunctionName& functionName, UErrorCode& status) {
        addError(DynamicError(DynamicErrorType::UnknownFunction, functionName), status);
    }

    void DynamicErrors::setUnresolvedVariable(const VariableName& v, UErrorCode& status) {
        addError(DynamicError(DynamicErrorType::UnresolvedVariable, v), status);
    }

    DynamicErrors::DynamicErrors(const StaticErrors& e, UErrorCode& status) : staticErrors(e) {
        resolutionAndFormattingErrors.adoptInstead(createUVector(status));
    }

    StaticErrors::StaticErrors(UErrorCode& status) {
        syntaxAndDataModelErrors.adoptInstead(createUVector(status));
    }

    StaticErrors::StaticErrors(StaticErrors&& other) noexcept {
        U_ASSERT(other.syntaxAndDataModelErrors.isValid());
        syntaxAndDataModelErrors.adoptInstead(other.syntaxAndDataModelErrors.orphan());
        dataModelError = other.dataModelError;
        missingSelectorAnnotationError = other.missingSelectorAnnotationError;
        syntaxError = other.syntaxError;
    }

    StaticErrors::StaticErrors(const StaticErrors& other, UErrorCode& errorCode) {
        CHECK_ERROR(errorCode);

        U_ASSERT(other.syntaxAndDataModelErrors.isValid());
        syntaxAndDataModelErrors.adoptInstead(createUVector(errorCode));
        CHECK_ERROR(errorCode);
        for (int32_t i = 0; i < other.syntaxAndDataModelErrors->size(); i++) {
            StaticError* e = static_cast<StaticError*>(other.syntaxAndDataModelErrors->elementAt(i));
            U_ASSERT(e != nullptr);
            StaticError* copy = new StaticError(*e);
            if (copy == nullptr) {
                errorCode = U_MEMORY_ALLOCATION_ERROR;
                return;
            }
            syntaxAndDataModelErrors->adoptElement(copy, errorCode);
        }
        dataModelError = other.dataModelError;
        missingSelectorAnnotationError = other.missingSelectorAnnotationError;
        syntaxError = other.syntaxError;
    }

    int32_t DynamicErrors::count() const {
        U_ASSERT(resolutionAndFormattingErrors.isValid() && staticErrors.syntaxAndDataModelErrors.isValid());
        return resolutionAndFormattingErrors->size() + staticErrors.syntaxAndDataModelErrors->size();
    }

    bool DynamicErrors::hasError() const {
        return count() > 0;
    }

    bool DynamicErrors::hasStaticError() const {
        U_ASSERT(staticErrors.syntaxAndDataModelErrors.isValid());
        return staticErrors.syntaxAndDataModelErrors->size() > 0;
    }

    const DynamicError& DynamicErrors::first() const {
        U_ASSERT(resolutionAndFormattingErrors->size() > 0);
        return *static_cast<DynamicError*>(resolutionAndFormattingErrors->elementAt(0));
    }

    void DynamicErrors::checkErrors(UErrorCode& status) const {
        if (status != U_ZERO_ERROR) {
            return;
        }

        // Just handle the first error
        // TODO: Eventually want to return all errors to caller
        if (count() == 0) {
            return;
        }
        if (staticErrors.syntaxAndDataModelErrors->size() > 0) {
            switch (staticErrors.first().type) {
            case StaticErrorType::DuplicateDeclarationError: {
                status = U_MF_DUPLICATE_DECLARATION_ERROR;
                break;
            }
            case StaticErrorType::DuplicateOptionName: {
                status = U_MF_DUPLICATE_OPTION_NAME_ERROR;
                break;
            }
            case StaticErrorType::VariantKeyMismatchError: {
                status = U_MF_VARIANT_KEY_MISMATCH_ERROR;
                break;
            }
            case StaticErrorType::NonexhaustivePattern: {
                status = U_MF_NONEXHAUSTIVE_PATTERN_ERROR;
                break;
            }
            case StaticErrorType::MissingSelectorAnnotation: {
                status = U_MF_MISSING_SELECTOR_ANNOTATION_ERROR;
                break;
            }
            case StaticErrorType::SyntaxError: {
                status = U_MF_SYNTAX_ERROR;
                break;
            }
            case StaticErrorType::UnsupportedStatementError: {
                status = U_MF_UNSUPPORTED_STATEMENT_ERROR;
            }
            }
        } else {
            U_ASSERT(resolutionAndFormattingErrors->size() > 0);
            switch (first().type) {
            case DynamicErrorType::UnknownFunction: {
                status = U_MF_UNKNOWN_FUNCTION_ERROR;
                break;
            }
            case DynamicErrorType::UnresolvedVariable: {
                status = U_MF_UNRESOLVED_VARIABLE_ERROR;
                break;
            }
            case DynamicErrorType::FormattingError: {
                status = U_MF_FORMATTING_ERROR;
                break;
            }
            case DynamicErrorType::OperandMismatchError: {
                status = U_MF_OPERAND_MISMATCH_ERROR;
                break;
            }
            case DynamicErrorType::ReservedError: {
                status = U_MF_UNSUPPORTED_EXPRESSION_ERROR;
                break;
            }
            case DynamicErrorType::SelectorError: {
                status = U_MF_SELECTOR_ERROR;
                break;
            }
            }
        }
    }

    void StaticErrors::addSyntaxError(UErrorCode& status) {
        addError(StaticError(StaticErrorType::SyntaxError), status);
    }

    void StaticErrors::addError(StaticError&& e, UErrorCode& status) {
        CHECK_ERROR(status);

        void* errorP = static_cast<void*>(create<StaticError>(std::move(e), status));
        U_ASSERT(syntaxAndDataModelErrors.isValid());

        switch (e.type) {
        case StaticErrorType::SyntaxError: {
            syntaxError = true;
            break;
        }
        case StaticErrorType::DuplicateDeclarationError: {
            dataModelError = true;
            break;
        }
        case StaticErrorType::DuplicateOptionName: {
            dataModelError = true;
            break;
        }
        case StaticErrorType::VariantKeyMismatchError: {
            dataModelError = true;
            break;
        }
        case StaticErrorType::NonexhaustivePattern: {
            dataModelError = true;
            break;
        }
        case StaticErrorType::MissingSelectorAnnotation: {
            missingSelectorAnnotationError = true;
            dataModelError = true;
            break;
        }
        case StaticErrorType::UnsupportedStatementError: {
            dataModelError = true;
            break;
        }
        }
        syntaxAndDataModelErrors->adoptElement(errorP, status);
    }

    void DynamicErrors::addError(DynamicError&& e, UErrorCode& status) {
        CHECK_ERROR(status);

        void* errorP = static_cast<void*>(create<DynamicError>(std::move(e), status));
        U_ASSERT(resolutionAndFormattingErrors.isValid());

        switch (e.type) {
        case DynamicErrorType::UnresolvedVariable: {
            unresolvedVariableError = true;
            resolutionAndFormattingErrors->adoptElement(errorP, status);
            break;
        }
        case DynamicErrorType::FormattingError: {
            formattingError = true;
            resolutionAndFormattingErrors->adoptElement(errorP, status);
            break;
        }
        case DynamicErrorType::OperandMismatchError: {
            formattingError = true;
            resolutionAndFormattingErrors->adoptElement(errorP, status);
            break;
        }
        case DynamicErrorType::ReservedError: {
            resolutionAndFormattingErrors->adoptElement(errorP, status);
            break;
        }
        case DynamicErrorType::SelectorError: {
            selectorError = true;
            resolutionAndFormattingErrors->adoptElement(errorP, status);
            break;
        }
        case DynamicErrorType::UnknownFunction: {
            unknownFunctionError = true;
            resolutionAndFormattingErrors->adoptElement(errorP, status);
            break;
        }
        }
    }

    const StaticError& StaticErrors::first() const {
        U_ASSERT(syntaxAndDataModelErrors.isValid() && syntaxAndDataModelErrors->size() > 0);
        return *static_cast<StaticError*>(syntaxAndDataModelErrors->elementAt(0));
    }

    StaticErrors::~StaticErrors() {}
    DynamicErrors::~DynamicErrors() {}

    template<typename ErrorType>
    Error<ErrorType>::~Error() {}

    template<>
    Error<StaticErrorType>::~Error() {}
    template<>
    Error<DynamicErrorType>::~Error() {}

} // namespace message2

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_MF2 */

#endif /* #if !UCONFIG_NO_FORMATTING */

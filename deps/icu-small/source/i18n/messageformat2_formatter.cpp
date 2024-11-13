// © 2024 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#if !UCONFIG_NO_MF2

#include "unicode/messageformat2.h"
#include "messageformat2_allocation.h"
#include "messageformat2_checker.h"
#include "messageformat2_errors.h"
#include "messageformat2_evaluation.h"
#include "messageformat2_function_registry_internal.h"
#include "messageformat2_macros.h"
#include "messageformat2_parser.h"
#include "messageformat2_serializer.h"
#include "uvector.h" // U_ASSERT

U_NAMESPACE_BEGIN

namespace message2 {

    // MessageFormatter::Builder

    // -------------------------------------
    // Creates a MessageFormat instance based on the pattern.

    void MessageFormatter::Builder::clearState() {
        normalizedInput.remove();
        delete errors;
        errors = nullptr;
    }

    MessageFormatter::Builder& MessageFormatter::Builder::setPattern(const UnicodeString& pat,
                                                                     UParseError& parseError,
                                                                     UErrorCode& errorCode) {
        clearState();
        // Create errors
        errors = create<StaticErrors>(StaticErrors(errorCode), errorCode);
        THIS_ON_ERROR(errorCode);

        // Parse the pattern
        MFDataModel::Builder tree(errorCode);
        Parser(pat, tree, *errors, normalizedInput).parse(parseError, errorCode);

        // Fail on syntax errors
        if (errors->hasSyntaxError()) {
            errors->checkErrors(errorCode);
            // Check that the checkErrors() method set the error code
            U_ASSERT(U_FAILURE(errorCode));
        }

        // Build the data model based on what was parsed
        dataModel = tree.build(errorCode);
        hasDataModel = true;
        hasPattern = true;
        pattern = pat;

        return *this;
    }

    // Precondition: `reg` is non-null
    // Does not adopt `reg`
    MessageFormatter::Builder& MessageFormatter::Builder::setFunctionRegistry(const MFFunctionRegistry& reg) {
        customMFFunctionRegistry = &reg;
        return *this;
    }

    MessageFormatter::Builder& MessageFormatter::Builder::setLocale(const Locale& loc) {
        locale = loc;
        return *this;
    }

    MessageFormatter::Builder& MessageFormatter::Builder::setDataModel(MFDataModel&& newDataModel) {
        clearState();
        hasPattern = false;
        hasDataModel = true;
        dataModel = std::move(newDataModel);

        return *this;
    }

    MessageFormatter::Builder&
        MessageFormatter::Builder::setErrorHandlingBehavior(
           MessageFormatter::UMFErrorHandlingBehavior type) {
               signalErrors = type == U_MF_STRICT;
               return *this;
    }

    /*
      This build() method is non-destructive, which entails the risk that
      its borrowed MFFunctionRegistry and (if the setDataModel() method was called)
      MFDataModel pointers could become invalidated.
    */
    MessageFormatter MessageFormatter::Builder::build(UErrorCode& errorCode) const {
        return MessageFormatter(*this, errorCode);
    }

    MessageFormatter::Builder::Builder(UErrorCode& errorCode) : locale(Locale::getDefault()), customMFFunctionRegistry(nullptr) {
        // Initialize errors
        errors = new StaticErrors(errorCode);
        CHECK_ERROR(errorCode);
        if (errors == nullptr) {
            errorCode = U_MEMORY_ALLOCATION_ERROR;
        }
    }

    MessageFormatter::Builder::~Builder() {
        if (errors != nullptr) {
            delete errors;
            errors = nullptr;
        }
    }

    // MessageFormatter

    MessageFormatter::MessageFormatter(const MessageFormatter::Builder& builder, UErrorCode &success) : locale(builder.locale), customMFFunctionRegistry(builder.customMFFunctionRegistry) {
        CHECK_ERROR(success);

        // Set up the standard function registry
        MFFunctionRegistry::Builder standardFunctionsBuilder(success);

        FormatterFactory* dateTime = StandardFunctions::DateTimeFactory::dateTime(success);
        FormatterFactory* date = StandardFunctions::DateTimeFactory::date(success);
        FormatterFactory* time = StandardFunctions::DateTimeFactory::time(success);
        FormatterFactory* number = new StandardFunctions::NumberFactory();
        FormatterFactory* integer = new StandardFunctions::IntegerFactory();
        standardFunctionsBuilder.adoptFormatter(FunctionName(UnicodeString("datetime")), dateTime, success)
            .adoptFormatter(FunctionName(UnicodeString("date")), date, success)
            .adoptFormatter(FunctionName(UnicodeString("time")), time, success)
            .adoptFormatter(FunctionName(UnicodeString("number")), number, success)
            .adoptFormatter(FunctionName(UnicodeString("integer")), integer, success)
            .adoptSelector(FunctionName(UnicodeString("number")), new StandardFunctions::PluralFactory(UPLURAL_TYPE_CARDINAL), success)
            .adoptSelector(FunctionName(UnicodeString("integer")), new StandardFunctions::PluralFactory(StandardFunctions::PluralFactory::integer()), success)
            .adoptSelector(FunctionName(UnicodeString("string")), new StandardFunctions::TextFactory(), success);
        CHECK_ERROR(success);
        standardMFFunctionRegistry = standardFunctionsBuilder.build();
        CHECK_ERROR(success);
        standardMFFunctionRegistry.checkStandard();

        normalizedInput = builder.normalizedInput;
        signalErrors = builder.signalErrors;

        // Build data model
        // First, check that there is a data model
        // (which might have been set by setDataModel(), or to
        // the data model parsed from the pattern by setPattern())

        if (!builder.hasDataModel) {
            success = U_INVALID_STATE_ERROR;
            return;
        }

        dataModel = builder.dataModel;
        if (builder.errors != nullptr) {
            errors = new StaticErrors(*builder.errors, success);
        } else {
            // Initialize errors
            LocalPointer<StaticErrors> errorsNew(new StaticErrors(success));
            CHECK_ERROR(success);
            errors = errorsNew.orphan();
        }

        // Note: we currently evaluate variables lazily,
        // without memoization. This call is still necessary
        // to check out-of-scope uses of local variables in
        // right-hand sides (unresolved variable errors can
        // only be checked when arguments are known)

        // Check for resolution errors
        Checker(dataModel, *errors).check(success);
    }

    void MessageFormatter::cleanup() noexcept {
        if (errors != nullptr) {
            delete errors;
            errors = nullptr;
        }
    }

    MessageFormatter& MessageFormatter::operator=(MessageFormatter&& other) noexcept {
        cleanup();

        locale = std::move(other.locale);
        standardMFFunctionRegistry = std::move(other.standardMFFunctionRegistry);
        customMFFunctionRegistry = other.customMFFunctionRegistry;
        dataModel = std::move(other.dataModel);
        normalizedInput = std::move(other.normalizedInput);
        signalErrors = other.signalErrors;
        errors = other.errors;
        other.errors = nullptr;
        return *this;
    }

    const MFDataModel& MessageFormatter::getDataModel() const { return dataModel; }

    UnicodeString MessageFormatter::getPattern() const {
        // Converts the current data model back to a string
        UnicodeString result;
        Serializer serializer(getDataModel(), result);
        serializer.serialize();
        return result;
    }

    // Precondition: custom function registry exists
    const MFFunctionRegistry& MessageFormatter::getCustomMFFunctionRegistry() const {
        U_ASSERT(hasCustomMFFunctionRegistry());
        return *customMFFunctionRegistry;
    }

    MessageFormatter::~MessageFormatter() {
        cleanup();
    }

    // Selector and formatter lookup
    // -----------------------------

    // Postcondition: selector != nullptr || U_FAILURE(status)
    Selector* MessageFormatter::getSelector(MessageContext& context, const FunctionName& functionName, UErrorCode& status) const {
        NULL_ON_ERROR(status);
        U_ASSERT(isSelector(functionName));

        const SelectorFactory* selectorFactory = lookupSelectorFactory(context, functionName, status);
        NULL_ON_ERROR(status);
        if (selectorFactory == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return nullptr;
        }
        // Create a specific instance of the selector
        auto result = selectorFactory->createSelector(getLocale(), status);
        NULL_ON_ERROR(status);
        return result;
    }

    // Returns an owned pointer
    Formatter* MessageFormatter::getFormatter(const FunctionName& functionName, UErrorCode& status) const {
        NULL_ON_ERROR(status);

        // Create the formatter

        // First, look up the formatter factory for this function
        FormatterFactory* formatterFactory = lookupFormatterFactory(functionName, status);
        NULL_ON_ERROR(status);

        U_ASSERT(formatterFactory != nullptr);

        // Create a specific instance of the formatter
        Formatter* formatter = formatterFactory->createFormatter(locale, status);
        NULL_ON_ERROR(status);
        if (formatter == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return nullptr;
        }
        return formatter;
    }

    bool MessageFormatter::getDefaultFormatterNameByType(const UnicodeString& type, FunctionName& name) const {
        U_ASSERT(hasCustomMFFunctionRegistry());
        const MFFunctionRegistry& reg = getCustomMFFunctionRegistry();
        return reg.getDefaultFormatterNameByType(type, name);
    }

    // ---------------------------------------------------
    // Function registry

    bool MessageFormatter::isBuiltInSelector(const FunctionName& functionName) const {
        return standardMFFunctionRegistry.hasSelector(functionName);
    }

    bool MessageFormatter::isBuiltInFormatter(const FunctionName& functionName) const {
        return standardMFFunctionRegistry.hasFormatter(functionName);
    }

    // https://github.com/unicode-org/message-format-wg/issues/409
    // Unknown function = unknown function error
    // Formatter used as selector  = selector error
    // Selector used as formatter = formatting error
    const SelectorFactory* MessageFormatter::lookupSelectorFactory(MessageContext& context, const FunctionName& functionName, UErrorCode& status) const {
        DynamicErrors& err = context.getErrors();

        if (isBuiltInSelector(functionName)) {
            return standardMFFunctionRegistry.getSelector(functionName);
        }
        if (isBuiltInFormatter(functionName)) {
            err.setSelectorError(functionName, status);
            return nullptr;
        }
        if (hasCustomMFFunctionRegistry()) {
            const MFFunctionRegistry& customMFFunctionRegistry = getCustomMFFunctionRegistry();
            const SelectorFactory* selectorFactory = customMFFunctionRegistry.getSelector(functionName);
            if (selectorFactory != nullptr) {
                return selectorFactory;
            }
            if (customMFFunctionRegistry.getFormatter(functionName) != nullptr) {
                err.setSelectorError(functionName, status);
                return nullptr;
            }
        }
        // Either there is no custom function registry and the function
        // isn't built-in, or the function doesn't exist in either the built-in
        // or custom registry.
        // Unknown function error
        err.setUnknownFunction(functionName, status);
        return nullptr;
    }

    FormatterFactory* MessageFormatter::lookupFormatterFactory(const FunctionName& functionName,
                                                               UErrorCode& status) const {
        NULL_ON_ERROR(status);

        if (isBuiltInFormatter(functionName)) {
            return standardMFFunctionRegistry.getFormatter(functionName);
        }
        if (isBuiltInSelector(functionName)) {
            status = U_MF_FORMATTING_ERROR;
            return nullptr;
        }
        if (hasCustomMFFunctionRegistry()) {
            const MFFunctionRegistry& customMFFunctionRegistry = getCustomMFFunctionRegistry();
            FormatterFactory* formatterFactory = customMFFunctionRegistry.getFormatter(functionName);
            if (formatterFactory != nullptr) {
                return formatterFactory;
            }
            if (customMFFunctionRegistry.getSelector(functionName) != nullptr) {
                status = U_MF_FORMATTING_ERROR;
                return nullptr;
            }
        }
        // Either there is no custom function registry and the function
        // isn't built-in, or the function doesn't exist in either the built-in
        // or custom registry.
        // Unknown function error
        status = U_MF_UNKNOWN_FUNCTION_ERROR;
        return nullptr;
    }

    bool MessageFormatter::isCustomFormatter(const FunctionName& fn) const {
        return hasCustomMFFunctionRegistry() && getCustomMFFunctionRegistry().getFormatter(fn) != nullptr;
    }


    bool MessageFormatter::isCustomSelector(const FunctionName& fn) const {
        return hasCustomMFFunctionRegistry() && getCustomMFFunctionRegistry().getSelector(fn) != nullptr;
    }

} // namespace message2

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_MF2 */

#endif /* #if !UCONFIG_NO_FORMATTING */

// © 2024 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#if !UCONFIG_NO_MF2

#include "unicode/messageformat2_arguments.h"
#include "unicode/messageformat2_data_model_names.h"
#include "uvector.h" // U_ASSERT

U_NAMESPACE_BEGIN

namespace message2 {

    using namespace data_model;

    // ------------------------------------------------------
    // MessageArguments

    using Arguments = MessageArguments;

    const Formattable* Arguments::getArgument(const VariableName& arg, UErrorCode& errorCode) const {
        if (U_SUCCESS(errorCode)) {
            U_ASSERT(argsLen == 0 || arguments.isValid());
            for (int32_t i = 0; i < argsLen; i++) {
                if (argumentNames[i] == arg) {
                    return &arguments[i];
                }
            }
            errorCode = U_ILLEGAL_ARGUMENT_ERROR;
        }
        return nullptr;
    }

    MessageArguments::~MessageArguments() {}

    // Message arguments
    // -----------------

    MessageArguments& MessageArguments::operator=(MessageArguments&& other) noexcept {
        U_ASSERT(other.arguments.isValid() || other.argsLen == 0);
        argsLen = other.argsLen;
        if (argsLen != 0) {
            argumentNames.adoptInstead(other.argumentNames.orphan());
            arguments.adoptInstead(other.arguments.orphan());
        }
        return *this;
    }

} // namespace message2

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_MF2 */

#endif /* #if !UCONFIG_NO_FORMATTING */

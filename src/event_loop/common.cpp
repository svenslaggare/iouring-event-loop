#include "common.h"

namespace event_loop {
    EventLoopException::EventLoopException(const std::string& operation, int errorCode)
        : mMessage(fmt::format("Operation '{}' failed due to: {}.", operation, *tryExtractError(errorCode))),
          mErrorCode(-errorCode) {

    }

    int EventLoopException::throwIfFailed(int result, const std::string& operation) {
        if (result < 0) {
            throw EventLoopException(operation, result);
        }

        return result;
    }

    int EventLoopException::errorCode() const {
        return mErrorCode;
    }

    const char* EventLoopException::what() const noexcept {
        return mMessage.c_str();
    }

    std::string errorNumberToString(int errorNumber) {
        return strerror(errorNumber);
    }

    std::optional<std::string> tryExtractError(int result) {
        if (result >= 0) {
            return {};
        } else {
            return { errorNumberToString(-result) };
        }
    }
}
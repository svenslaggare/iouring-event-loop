#include "common.h"

namespace event_loop {
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
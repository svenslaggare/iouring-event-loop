#pragma once

#include <stop_token>
#include <string>
#include <optional>

#include "fmt/format.h"

namespace event_loop {
    using Fd = int;
    using EventId = std::uint64_t;
    using Result = std::int32_t;

    class EventLoop;
    struct EventContext {
        EventLoop& eventLoop;
        std::stop_source& stopSource;
        Result result;

        inline std::size_t resultAsSize() const {
            return result > 0 ? (std::size_t)result : 0;
        }
    };

    struct Event {
        EventId id = 0;

        explicit Event(EventId id)
            : id(id) {

        }
        virtual ~Event() = default;

        virtual std::string name() const = 0;
        virtual bool handle(EventContext& context) = 0;
    };

    class EventLoopException : public std::exception {
    private:
        int mErrorCode = 0;
        std::string mMessage;
    public:
        explicit EventLoopException(const std::string& operation, int errorCode);
        static int throwIfFailed(int result, const std::string& operation);

        int errorCode() const;
        const char* what() const noexcept override;
    };

    std::string errorNumberToString(int errorNumber);
    std::optional<std::string> tryExtractError(int result);

    template<typename T>
    struct TypedFd {
        Fd fd = -1;
        explicit TypedFd(Fd fd)
            : fd(fd) {

        }

        explicit operator bool() const {
            return fd >= 0;
        }

        bool operator<(const T& other) const {
            return fd < other.fd;
        }

        bool operator==(const T& other) const {
            return fd == other.fd;
        }

        bool operator!=(const T& other) const {
            return fd != other.fd;
        }
    };

    template<typename T>
    std::ostream& operator<<(std::ostream& stream, const TypedFd<T>& strongFd) {
        stream << strongFd.fd;
        return stream;
    }

    struct AnyFd : public TypedFd<AnyFd> {
        using TypedFd::TypedFd;

        template<typename T>
        AnyFd(const TypedFd<T> other)
            : TypedFd(other.fd) {

        }
    };

    struct Socket : public TypedFd<Socket> {
        using TypedFd::TypedFd;
    };

    struct File : public TypedFd<File> {
        using TypedFd::TypedFd;

        static inline File stdinFile() {
            return File { STDIN_FILENO };
        }
    };
}
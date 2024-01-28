#pragma once

#include <functional>
#include <cstdint>
#include <filesystem>
#include <variant>

#include <netinet/in.h>
#include <sys/un.h>
#include <linux/time_types.h>
#include <sys/stat.h>

#include "common.h"
#include "buffer.h"

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

namespace event_loop {
    struct CloseEvent : public Event {
        AnyFd fd;

        struct Response {
            AnyFd fd;
        };

        using Callback = std::function<void (EventContext& context, const Response&)>;
        Callback callback;

        CloseEvent(EventId id, AnyFd fd, Callback callback);

        std::string name() const override;
        bool handle(EventContext& context) override;
    };

    struct TimerEvent : public Event {
        using Clock = std::chrono::high_resolution_clock;

        Clock::time_point startTime;
        std::chrono::nanoseconds duration;
        __kernel_timespec eventDelay {};

        struct Response {
            double elapsed = 0.0;
        };

        using Callback = std::function<bool (EventContext& context, const Response&)>;
        Callback callback;

        TimerEvent(EventId id, std::chrono::nanoseconds duration, Callback callback);

        std::string name() const override;
        bool handle(EventContext& context) override;
    };

    enum class SocketType {
        Inet,
        Unix
    };

    using SocketAddress = std::variant<sockaddr_in, sockaddr_un>;
    SocketAddress defaultFor(SocketType type);

    struct AcceptEvent : public Event {
        Socket server;

        SocketAddress clientAddress;
        socklen_t clientAddressLength = sizeof(socklen_t);

        struct Response {
            Socket client;
            SocketAddress clientAddress {};
        };

        using Callback = std::function<bool (EventContext& context, const Response&)>;
        Callback callback;

        AcceptEvent(EventId id, Socket server, SocketType type, Callback callback);

        std::string name() const override;
        bool handle(EventContext& context) override;
    };

    struct ConnectEvent : public Event {
        Socket client;
        SocketAddress serverAddress;

        struct Response {
            Socket client;
            SocketAddress serverAddress;
            std::optional<std::string> error;

            const sockaddr_in& serverAddressInet() const;
            const sockaddr_un& serverAddressUnix() const;
        };

        using Callback = std::function<void (EventContext& context, const Response&)>;
        Callback callback;

        ConnectEvent(EventId id, Socket client, sockaddr_in serverAddress, Callback callback);
        ConnectEvent(EventId id, Socket client, sockaddr_un serverAddress, Callback callback);

        std::string name() const override;
        bool handle(EventContext& context) override;
    };

    struct ReceiveEvent : public Event {
        Socket client;
        Buffer buffer;

        struct Response {
            Socket client;
            std::uint8_t* data = nullptr;
            std::size_t size = 0;
        };

        using Callback = std::function<bool (EventContext& context, const Response&)>;
        Callback callback;

        ReceiveEvent(EventId id, Socket client, Buffer buffer, Callback callback);

        std::string name() const override;
        bool handle(EventContext& context) override;
    };

    struct SendEvent : public Event {
        Socket client;
        Buffer data;

        struct Response {
            Socket client;
            std::size_t size = 0;
        };

        using Callback = std::function<void (EventContext& context, const Response&)>;
        Callback callback;

        SendEvent(EventId id, Socket client, Buffer data, Callback callback);

        std::string name() const override;
        bool handle(EventContext& context) override;
    };

    struct OpenFileEvent : public Event {
        std::filesystem::path path;
        int flags = 0;
        mode_t mode = 0;

        struct Response {
            File file;
        };

        using Callback = std::function<void (EventContext& context, const Response&)>;
        Callback callback;

        OpenFileEvent(EventId id, std::filesystem::path path, int flags, mode_t mode, Callback callback);

        std::string name() const override;
        bool handle(EventContext& context) override;
    };

    struct ReadFileEvent : public Event {
        File file;
        std::uint64_t offset = 0;
        Buffer buffer;

        struct Response {
            File file;
            std::uint8_t* data = nullptr;
            std::size_t size = 0;
            std::uint64_t offset = 0;
        };

        using Callback = std::function<bool (EventContext& context, const Response&)>;
        Callback callback;

        ReadFileEvent(EventId id, File file, Buffer buffer, std::uint64_t offset, Callback callback);

        std::string name() const override;
        bool handle(EventContext& context) override;
    };

    struct WriteFileEvent : public Event {
        File file;
        Buffer data;

        struct Response {
            File file;
            std::size_t size = 0;
        };

        using Callback = std::function<void (EventContext& context, const Response&)>;
        Callback callback;

        WriteFileEvent(EventId id, File file, Buffer data, Callback callback);

        std::string name() const override;
        bool handle(EventContext& context) override;
    };

    struct ReadFileStatsEvent : public Event {
        std::filesystem::path path;
        int flags = 0;
        unsigned int mask = 0;
        struct statx stats {};

        struct Response {
            std::optional<struct statx> stats;
        };

        using Callback = std::function<void (EventContext& context, const Response&)>;
        Callback callback;

        ReadFileStatsEvent(EventId id, std::filesystem::path path, Callback callback);

        std::string name() const override;
        bool handle(EventContext& context) override;
    };

    struct ReadLineEvent {
        struct Response {
            std::string line;
        };

        using Callback = std::function<bool (EventContext& context, const Response&)>;
    };
}
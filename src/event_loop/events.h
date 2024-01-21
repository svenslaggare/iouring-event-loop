#pragma once

#include <functional>
#include <cstdint>
#include <filesystem>

#include <netinet/in.h>
#include <linux/time_types.h>

#include "common.h"
#include "buffer.h"

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
        __kernel_timespec timespec {};

        struct Response {
            double elapsed = 0.0;
        };

        using Callback = std::function<bool (EventContext& context, const Response&)>;
        Callback callback;

        TimerEvent(EventId id, std::chrono::nanoseconds duration, Callback callback);

        std::string name() const override;
        bool handle(EventContext& context) override;
    };

    struct AcceptEvent : public Event {
        Socket server;

        sockaddr_in clientAddress {};
        socklen_t clientAddressLength = sizeof(socklen_t);

        struct Response {
            Socket client;
            sockaddr_in clientAddress {};
        };

        using Callback = std::function<bool (EventContext& context, const Response&)>;
        Callback callback;

        AcceptEvent(EventId id, Socket server, Callback callback);

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
}
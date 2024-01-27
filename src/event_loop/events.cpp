#include "events.h"
#include "loop.h"

namespace event_loop {
    CloseEvent::CloseEvent(EventId id, AnyFd fd, CloseEvent::Callback callback)
        : Event(id),
          fd(fd),
          callback(std::move(callback)) {

    }

    std::string CloseEvent::name() const {
        return "Close";
    }

    bool CloseEvent::handle(EventContext& context) {
        if (!callback) {
            return false;
        }

        callback(context, { fd });
        return false;
    }

    TimerEvent::TimerEvent(EventId id, std::chrono::nanoseconds duration, TimerEvent::Callback callback)
        : Event(id),
          startTime(Clock::now()),
          duration(duration),
          callback(std::move(callback)) {

    }

    std::string TimerEvent::name() const {
        return "Timer";
    }

    bool TimerEvent::handle(EventContext& context) {
        auto elapsed = Clock::now() - startTime;
        if (elapsed >= duration) {
            if (!callback) {
                return false;
            }

            auto elapsedSeconds = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() / 1.0E9;
            if (callback(context, { elapsedSeconds })) {
                startTime = Clock::now();
                context.eventLoop.timer(*this, nullptr);
                return true;
            } else {
                return false;
            }
        } else {
            // Deadline has not passed (due to other event), reschedule the event
            context.eventLoop.timer(*this, nullptr);
            return true;
        }
    }

    AcceptEvent::AcceptEvent(EventId id, Socket server, Callback callback)
        : Event(id),
          server(server),
          callback(std::move(callback)) {

    }

    std::string AcceptEvent::name() const {
        return "Accept";
    }

    bool AcceptEvent::handle(EventContext& context) {
        if (!callback) {
            return false;
        }

        if (callback(context, { Socket { context.result }, clientAddress }) && context.result > 0) {
            // Zero the data
            clientAddress = {};

            // Reuse
            context.eventLoop.accept(*this, nullptr);
            return true;
        }

        return false;
    }

    ConnectEvent::ConnectEvent(EventId id, Socket client, sockaddr_in serverAddress, ConnectEvent::Callback callback)
        : Event(id),
          client(client),
          serverAddress(serverAddress),
          callback(std::move(callback)) {

    }

    ConnectEvent::ConnectEvent(EventId id, Socket client, sockaddr_un serverAddress, ConnectEvent::Callback callback)
        : Event(id),
          client(client),
          serverAddress(serverAddress),
          callback(std::move(callback))  {

    }

    const sockaddr_in& ConnectEvent::Response::serverAddressInet() const {
        return std::get<sockaddr_in>(serverAddress);
    }

    const sockaddr_un& ConnectEvent::Response::serverAddressUnix() const {
        return std::get<sockaddr_un>(serverAddress);
    }

    std::string ConnectEvent::name() const {
        return "Connect";
    }

    bool ConnectEvent::handle(EventContext& context) {
        if (!callback) {
            return false;
        }

        callback(context, { client, serverAddress, tryExtractError(context.result) });
        return false;
    }

    ReceiveEvent::ReceiveEvent(EventId id, Socket client, Buffer buffer, Callback callback)
        : Event(id),
          client(client), buffer(std::move(buffer)),
          callback(std::move(callback)) {

    }

    std::string ReceiveEvent::name() const {
        return "Receive";
    }

    bool ReceiveEvent::handle(EventContext& context) {
        if (!callback) {
            return false;
        }

        if (callback(context, { client, buffer.data(), context.resultAsSize() }) && context.result > 0) {
            // Zero the data
            buffer.clear();

            // Reuse
            context.eventLoop.receive(*this, nullptr);
            return true;
        }

        return false;
    }

    SendEvent::SendEvent(EventId id, Socket client, Buffer data, Callback callback)
        : Event(id),
          client(client), data(std::move(data)),
          callback(std::move(callback)) {

    }

    std::string SendEvent::name() const {
        return "Send";
    }

    bool SendEvent::handle(EventContext& context) {
        if (!callback) {
            return false;
        }

        callback(context, { client, context.resultAsSize() });
        return false;
    }

    OpenFileEvent::OpenFileEvent(EventId id, std::filesystem::path path, int flags, mode_t mode, OpenFileEvent::Callback callback)
        : Event(id),
          path(std::move(path)), flags(flags), mode(mode),
          callback(std::move(callback)) {

    }

    std::string OpenFileEvent::name() const {
        return "OpenFile";
    }

    bool OpenFileEvent::handle(EventContext& context) {
        if (!callback) {
            return false;
        }

        callback(context, { File { context.result }});
        return false;
    }

    ReadFileEvent::ReadFileEvent(EventId id, File file, Buffer buffer, std::uint64_t offset, ReadFileEvent::Callback callback)
        : Event(id),
          file(file), buffer(std::move(buffer)), offset(offset),
          callback(std::move(callback)) {

    }

    std::string ReadFileEvent::name() const {
        return "ReadFile";
    }

    bool ReadFileEvent::handle(EventContext& context) {
        if (!callback) {
            return false;
        }

        if (callback(context, { file, buffer.data(), context.resultAsSize(), offset }) && context.result > 0) {
            offset += context.result;

            // Zero the data
            buffer.clear();

            // Reuse
            context.eventLoop.readFile(*this, nullptr);
            return true;
        }

        return false;
    }

    WriteFileEvent::WriteFileEvent(EventId id, File file, Buffer data, Callback callback)
        : Event(id),
          file(file), data(std::move(data)),
          callback(std::move(callback)) {

    }

    std::string WriteFileEvent::name() const {
        return "WriteFile";
    }

    bool WriteFileEvent::handle(EventContext& context) {
        if (!callback) {
            return false;
        }

        callback(context, { file, context.resultAsSize() });
        return false;
    }

    ReadFileStatsEvent::ReadFileStatsEvent(EventId id, std::filesystem::path path, ReadFileStatsEvent::Callback callback)
        : Event(id),
          path(std::move(path)),
          callback(std::move(callback))
    {

    }

    std::string ReadFileStatsEvent::name() const {
        return "ReadFileStats";
    }

    bool ReadFileStatsEvent::handle(EventContext& context) {
        if (!callback) {
            return false;
        }

        Response response;
        if (context.result >= 0) {
            response.stats = { stats };
        }

        callback(context, response);
        return false;
    }
}
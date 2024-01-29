#pragma once

#include <cstdint>
#include <exception>
#include <string>
#include <stop_token>
#include <unordered_map>
#include <filesystem>

#include "fmt/format.h"

#include <netinet/in.h>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/un.h>

#include <liburing.h>
#include <iostream>
#include <vector>

#include "common.h"
#include "events.h"
#include "buffer.h"

namespace event_loop {
    class TcpListener {
    private:
        Socket mSocket;
        sockaddr_in mAddress;
    public:
        explicit TcpListener(Socket socket, sockaddr_in address);

        Socket socket() const;
        const sockaddr_in& address() const;
    };

    class UnixListener {
    private:
        Socket mSocket;
        sockaddr_un mAddress;
    public:
        explicit UnixListener(Socket socket, sockaddr_un address);

        Socket socket() const;
        const sockaddr_un& address() const;
    };

    class EventLoop;
    class SubmitGuard {
    private:
        EventLoop& mEventLoop;
        std::size_t mSubmitted = 0;
    public:
        explicit SubmitGuard(EventLoop& eventLoop);
        ~SubmitGuard();

        SubmitGuard(const SubmitGuard&) = delete;
        SubmitGuard& operator=(const SubmitGuard&) = delete;

        void submit();
    };

    class EventLoop {
    public:
        using DispatchedCallback = std::function<void (EventLoop&)>;
    private:
        io_uring mRing {};

        EventId mNextEventId = 1;
        std::unordered_map<EventId, std::unique_ptr<Event>> mEvents;

        std::mutex mDispatchedMutex;
        std::vector<DispatchedCallback> mDispatched;

        BufferManager mBufferManager;
    public:
        explicit EventLoop(std::uint32_t depth = 256);
        ~EventLoop();

        EventLoop(const EventLoop&) = delete;
        EventLoop& operator=(const EventLoop&) = delete;

        void run(std::stop_source& stopSource);
        bool runOnce(std::stop_source& stopSource, std::chrono::duration<double> maxDuration);

        /**
         * Request the given callback (potentially from another thread) to be executed on the event loop thread
         */
        void dispatch(DispatchedCallback callback);

        // Generic
        void close(AnyFd fd, CloseEvent::Callback callback, SubmitGuard* submit = nullptr);

        // Timer
        void timer(std::chrono::duration<double> duration, TimerEvent::Callback callback, SubmitGuard* submit = nullptr);

        // Sockets
        TcpListener tcpListen(in_addr address, std::uint16_t port, int backlog = 32);
        Socket udpReceiver(in_addr address, std::uint16_t port);
        UnixListener unixListen(const std::string& path, int backlog = 32);

        void accept(const TcpListener& listener, AcceptEvent::Callback callback, SubmitGuard* submit = nullptr);
        void accept(const UnixListener& listener, AcceptEvent::Callback callback, SubmitGuard* submit = nullptr);
        void connect(in_addr_t address, std::uint16_t port, ConnectEvent::Callback callback, SubmitGuard* submit = nullptr);
        void connect(const std::string& path, ConnectEvent::Callback callback, SubmitGuard* submit = nullptr);

        void receive(Socket client, Buffer buffer, ReceiveEvent::Callback callback, SubmitGuard* submit = nullptr);
        void send(Socket client, Buffer data, SendEvent::Callback callback, SubmitGuard* submit = nullptr);

        // File
        void openFile(std::filesystem::path path, OpenFileEvent::Callback callback, SubmitGuard* submit = nullptr);
        void openFile(std::filesystem::path path, int flags, mode_t mode, OpenFileEvent::Callback callback, SubmitGuard* submit = nullptr);
        void readFile(File file, Buffer buffer, std::uint64_t offset, ReadFileEvent::Callback callback, SubmitGuard* submit = nullptr);
        void writeFile(File file, Buffer data, WriteFileEvent::Callback callback, SubmitGuard* submit = nullptr);
        void readFileStats(std::filesystem::path path, ReadFileStatsEvent::Callback callback, SubmitGuard* submit = nullptr);

        // Standard I/O
        void readLine(Buffer buffer, ReadLineEvent::Callback callback, SubmitGuard* submit = nullptr);
        void printStdout(const std::string_view& string, WriteFileEvent::Callback callback, SubmitGuard* submit = nullptr);
        void printStderr(const std::string_view& string, WriteFileEvent::Callback callback, SubmitGuard* submit = nullptr);

        Buffer allocate(std::size_t size);
        void deallocate(Buffer buffer);
    private:
        friend class SubmitGuard;

        friend class TimerEvent;
        friend class ReceiveEvent;
        friend class AcceptEvent;
        friend class ReadFileEvent;

        void executeDispatched();

        void close(CloseEvent& event, SubmitGuard* submit);

        void timer(TimerEvent& event, SubmitGuard* submit);

        void accept(AcceptEvent& event, SubmitGuard* submit);
        void connect(ConnectEvent& event, SubmitGuard* submit);
        void receive(ReceiveEvent& event, SubmitGuard* submit);
        void send(SendEvent& event, SubmitGuard* submit);

        void openFile(OpenFileEvent& event, SubmitGuard* submit);
        void readFile(ReadFileEvent& event, SubmitGuard* submit);
        void writeFile(WriteFileEvent& event, SubmitGuard* submit);
        void readFileStats(ReadFileStatsEvent& event, SubmitGuard* submit);

        void printFile(File file, const std::string_view& string, WriteFileEvent::Callback callback, SubmitGuard* submit);

        void submitRing(SubmitGuard* submit);
        io_uring_sqe* getSqe();

        template<typename T, typename ...Args>
        T& createEvent(Args&&... args) {
            auto id = mNextEventId;
            mNextEventId++;
            auto [iterator, _] = mEvents.emplace(id, std::make_unique<T>(id, std::forward<Args>(args)...));
            return *(T*)iterator->second.get();
        }

        void removeEvent(EventId id);
    };
}
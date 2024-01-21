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

#include <liburing.h>
#include <iostream>
#include <vector>

#include "common.h"
#include "events.h"
#include "buffer.h"

namespace event_loop {
    class TcpServer {
    private:
        Socket mSocket;
        sockaddr_in mAddress;
    public:
        explicit TcpServer(Socket socket, sockaddr_in address)
            : mSocket(socket),
              mAddress(address)  {

        }

        Socket socket() const {
            return mSocket;
        }

        const sockaddr_in& address() const {
            return mAddress;
        }
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
    private:
        io_uring mRing {};

        EventId mNextEventId = 1;
        std::unordered_map<EventId, std::unique_ptr<Event>> mEvents;
    public:
        EventLoop();
        ~EventLoop();

        EventLoop(const EventLoop&) = delete;
        EventLoop& operator=(const EventLoop&) = delete;

        void run(std::stop_source& stopSource);

        // Generic
        void close(AnyFd fd, CloseEvent::Callback callback, SubmitGuard* submit = nullptr);

        // Timer
        void timer(std::chrono::duration<double> duration, TimerEvent::Callback callback, SubmitGuard* submit = nullptr);

        // Network
        TcpServer tcpListen(in_addr address, std::uint16_t port, int backlog = 10);
        void accept(TcpServer& tcpServer, AcceptEvent::Callback callback, SubmitGuard* submit = nullptr);
        void receive(Socket client, Buffer buffer, ReceiveEvent::Callback callback, SubmitGuard* submit = nullptr);
        void send(Socket client, Buffer data, SendEvent::Callback callback, SubmitGuard* submit = nullptr);

        // File
        void openFile(std::filesystem::path path, OpenFileEvent::Callback callback, SubmitGuard* submit = nullptr);
        void openFile(std::filesystem::path path, int flags, mode_t mode, OpenFileEvent::Callback callback, SubmitGuard* submit = nullptr);
        void readFile(File file, Buffer buffer, std::uint64_t offset, ReadFileEvent::Callback callback, SubmitGuard* submit = nullptr);
        void writeFile(File file, Buffer data, WriteFileEvent::Callback callback, SubmitGuard* submit = nullptr);
    private:
        friend class SubmitGuard;

        friend class TimerEvent;
        friend class ReceiveEvent;
        friend class AcceptEvent;
        friend class ReadFileEvent;

        void close(CloseEvent& event, SubmitGuard* submit);

        void timer(TimerEvent& event, SubmitGuard* submit);

        void accept(AcceptEvent& event, SubmitGuard* submit);
        void receive(ReceiveEvent& event, SubmitGuard* submit);
        void send(SendEvent& event, SubmitGuard* submit);

        void openFile(OpenFileEvent& event, SubmitGuard* submit);
        void readFile(ReadFileEvent& event, SubmitGuard* submit);
        void writeFile(WriteFileEvent& event, SubmitGuard* submit);

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
#include "loop.h"
#include "events.h"

#include <fcntl.h>

#include <mutex>

namespace event_loop {
    namespace {
        __kernel_timespec createKernelTimeSpec(std::chrono::nanoseconds delay) {
            __kernel_timespec timespec {};

            if (delay.count() > 0) {
                timespec.tv_sec = delay.count() / std::chrono::nanoseconds::period::den;
                timespec.tv_nsec = delay.count() % std::chrono::nanoseconds::period::den;
            }

            return timespec;
        }
    }

    TcpListener::TcpListener(Socket socket, sockaddr_in address)
        : mSocket(socket), mAddress(address)
    {

    }

    Socket TcpListener::socket() const {
        return mSocket;
    }

    const sockaddr_in& TcpListener::address() const {
        return mAddress;
    }

    UnixListener::UnixListener(Socket socket, sockaddr_un address)
        : mSocket(socket), mAddress(address) {

    }

    Socket UnixListener::socket() const {
        return mSocket;
    }

    const sockaddr_un& UnixListener::address() const {
        return mAddress;
    }

    SubmitGuard::SubmitGuard(EventLoop& eventLoop)
        : mEventLoop(eventLoop) {

    }

    SubmitGuard::~SubmitGuard() {
        if (mSubmitted > 0) {
            mEventLoop.submitRing(nullptr);
            mSubmitted = 0;
        }
    }

    void SubmitGuard::submit() {
        mSubmitted++;
    }

    EventLoop::EventLoop(std::uint32_t depth) {
        EventLoopException::throwIfFailed(io_uring_queue_init(depth, &mRing, 0), "io_uring_queue_init");
    }

    EventLoop::~EventLoop() {
        io_uring_queue_exit(&mRing);
    }

    void EventLoop::run(std::stop_source& stopSource) {
        using namespace std::chrono_literals;

        while (!stopSource.stop_requested()) {
            io_uring_cqe* cqe = nullptr;
            auto delay = createKernelTimeSpec(std::chrono::duration_cast<std::chrono::nanoseconds>(0.5s));
            auto result = io_uring_wait_cqe_timeout(&mRing, &cqe, &delay);
            if (result == -ETIME) {
                executeDispatched();
                continue;
            }

            EventLoopException::throwIfFailed(result, "io_uring_wait_cqe_timeout");

            auto eventId = cqe->user_data;
            auto& event = mEvents[eventId];
//            std::cout << "Event: " << event->id << ", type: " << event->name() << ", status: " << cqe->res << std::endl;

            EventContext context { *this, stopSource, cqe->res };
            if (!event->handle(context)) {
                removeEvent(eventId);
            }

            io_uring_cqe_seen(&mRing, cqe);
            executeDispatched();
        }
    }

    void EventLoop::dispatch(DispatchedCallback callback) {
        std::scoped_lock guard(mDispatchedMutex);
        mDispatched.push_back(std::move(callback));
    }

    void EventLoop::executeDispatched() {
        std::scoped_lock guard(mDispatchedMutex);

        for (auto& dispatch : mDispatched) {
            dispatch(*this);
        }
        mDispatched.clear();
    }

    void EventLoop::close(AnyFd fd, CloseEvent::Callback callback, SubmitGuard* submit) {
        auto& event = createEvent<CloseEvent>(fd, std::move(callback));
        try {
            close(event, submit);
        } catch (const EventLoopException& e) {
            removeEvent(event.id);
            throw;
        }
    }

    void EventLoop::close(CloseEvent& event, SubmitGuard* submit) {
        auto sqe = getSqe();

        io_uring_prep_close(sqe, event.fd.fd);
        sqe->user_data = event.id;

        submitRing(submit);
    }

    void EventLoop::timer(std::chrono::duration<double> duration, TimerEvent::Callback callback, SubmitGuard* submit) {
        auto durationNs = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
        auto& event = createEvent<TimerEvent>(durationNs, std::move(callback));
        try {
            timer(event, submit);
        } catch (const EventLoopException& e) {
            removeEvent(event.id);
            throw;
        }
    }

    void EventLoop::timer(TimerEvent& event, SubmitGuard* submit) {
        auto sqe = getSqe();

        auto now = TimerEvent::Clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - event.startTime);
        auto sleepTime = std::max(std::chrono::nanoseconds(0), event.duration - elapsed);
        event.eventDelay = createKernelTimeSpec(sleepTime);

        io_uring_prep_timeout(sqe, &event.eventDelay, 1, 0);
        sqe->user_data = event.id;

        submitRing(submit);
    }

    TcpListener EventLoop::tcpListen(in_addr address, std::uint16_t port, int backlog) {
        auto socketFd = EventLoopException::throwIfFailed(socket(PF_INET, SOCK_STREAM, 0), "socket");

        int enable = 1;
        EventLoopException::throwIfFailed(
            setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)),
            "setsockopt(SO_REUSEADDR)"
        );

        sockaddr_in socketAddress {};
        socketAddress.sin_family = AF_INET;
        socketAddress.sin_addr = address;
        socketAddress.sin_port = htons(port);
        socketAddress.sin_addr.s_addr = htonl(INADDR_ANY);

        EventLoopException::throwIfFailed(
            bind(socketFd, (const sockaddr*)&socketAddress, sizeof(socketAddress)),
            "bind"
        );

        EventLoopException::throwIfFailed(listen(socketFd, backlog), "listen");
        return TcpListener { Socket { socketFd }, socketAddress };
    }

    Socket EventLoop::udpReceiver(in_addr address, std::uint16_t port) {
        auto socketFd = EventLoopException::throwIfFailed(socket(PF_INET, SOCK_DGRAM, 0), "socket");

        sockaddr_in serverAddress {};
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_addr = address;
        serverAddress.sin_port = htons(port);
        serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);

        EventLoopException::throwIfFailed(
            bind(socketFd, (const sockaddr*)&serverAddress, sizeof(serverAddress)),
            "bind"
        );

        return Socket { socketFd };
    }

    UnixListener EventLoop::unixListen(const std::string& path, int backlog) {
        auto socketFd = EventLoopException::throwIfFailed(socket(PF_UNIX, SOCK_STREAM, 0), "socket");

        sockaddr_un socketAddress {};
        socketAddress.sun_family = AF_UNIX;
        strcpy(socketAddress.sun_path, path.c_str());
        EventLoopException::throwIfFailed(unlink(path.c_str()), "unlink");

        EventLoopException::throwIfFailed(
            bind(socketFd, (const sockaddr*)&socketAddress, sizeof(socketAddress)),
            "bind"
        );

        EventLoopException::throwIfFailed(listen(socketFd, backlog), "listen");
        return UnixListener { Socket { socketFd }, socketAddress };
    }

    void EventLoop::accept(const TcpListener& listener, AcceptEvent::Callback callback, SubmitGuard* submit) {
        auto& event = createEvent<AcceptEvent>(listener.socket(), SocketType::Inet, std::move(callback));
        try {
            accept(event, submit);
        } catch (const EventLoopException& e) {
            removeEvent(event.id);
            throw;
        }
    }

    void EventLoop::accept(const UnixListener& listener, AcceptEvent::Callback callback, SubmitGuard* submit) {
        auto& event = createEvent<AcceptEvent>(listener.socket(), SocketType::Unix, std::move(callback));
        try {
            accept(event, submit);
        } catch (const EventLoopException& e) {
            removeEvent(event.id);
            throw;
        }
    }

    void EventLoop::accept(AcceptEvent& event, SubmitGuard* submit) {
        auto sqe = getSqe();

        std::visit(overloaded {
            [&](const sockaddr_in& address) {
                io_uring_prep_accept(sqe, event.server.fd, (sockaddr*)&address, &event.clientAddressLength, 0);
            },
            [&](const sockaddr_un& address) {
                io_uring_prep_accept(sqe, event.server.fd, (sockaddr*)&address, &event.clientAddressLength, 0);
            },
        }, event.clientAddress);
        sqe->user_data = event.id;

        submitRing(submit);
    }

    void EventLoop::connect(in_addr_t address, std::uint16_t port, ConnectEvent::Callback callback, SubmitGuard* submit) {
        Socket clientSocket { EventLoopException::throwIfFailed(socket(AF_INET, SOCK_STREAM, 0), "socket") };

        sockaddr_in serverAddress {};
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_addr.s_addr = address;
        serverAddress.sin_port = htons(port);

        auto& event = createEvent<ConnectEvent>(clientSocket, serverAddress, std::move(callback));
        try {
            connect(event, submit);
        } catch (const EventLoopException& e) {
            removeEvent(event.id);
            throw;
        }
    }

    void EventLoop::connect(const std::string& path, ConnectEvent::Callback callback, SubmitGuard* submit) {
        Socket clientSocket { EventLoopException::throwIfFailed(socket(AF_UNIX, SOCK_STREAM, 0), "socket") };

        sockaddr_un serverAddress {};
        serverAddress.sun_family = AF_UNIX;
        strcpy(serverAddress.sun_path, path.c_str());

        auto& event = createEvent<ConnectEvent>(clientSocket, serverAddress, std::move(callback));
        try {
            connect(event, submit);
        } catch (const EventLoopException& e) {
            removeEvent(event.id);
            throw;
        }
    }

    void EventLoop::connect(ConnectEvent& event, SubmitGuard* submit) {
        auto sqe = getSqe();

        std::visit(overloaded {
            [&](const sockaddr_in& serverAddress) {
                io_uring_prep_connect(sqe, event.client.fd, (sockaddr*)&serverAddress, sizeof(serverAddress));
            },
            [&](const sockaddr_un& serverAddress) {
                io_uring_prep_connect(sqe, event.client.fd, (sockaddr*)&serverAddress, sizeof(serverAddress));
            },
        }, event.serverAddress);

        sqe->user_data = event.id;

        submitRing(submit);
    }

    void EventLoop::receive(Socket client, Buffer buffer, ReceiveEvent::Callback callback, SubmitGuard* submit) {
        auto& event = createEvent<ReceiveEvent>(client, std::move(buffer), std::move(callback));
        try {
            receive(event, submit);
        } catch (const EventLoopException& e) {
            removeEvent(event.id);
            throw;
        }
    }

    void EventLoop::receive(ReceiveEvent& event, SubmitGuard* submit) {
        auto sqe = getSqe();

        io_uring_prep_recv(sqe, event.client.fd, event.buffer.data(), event.buffer.size(), 0);
        sqe->user_data = event.id;

        submitRing(submit);
    }

    void EventLoop::send(Socket client, Buffer data, SendEvent::Callback callback, SubmitGuard* submit) {
        auto& event = createEvent<SendEvent>(client, std::move(data), std::move(callback));
        try {
            send(event, submit);
        } catch (const EventLoopException& e) {
            removeEvent(event.id);
            throw;
        }
    }

    void EventLoop::send(SendEvent& event, SubmitGuard* submit) {
        auto sqe = getSqe();

        io_uring_prep_send(sqe, event.client.fd, event.data.data(), event.data.size(), 0);
        sqe->user_data = event.id;

        submitRing(submit);
    }

    void EventLoop::openFile(std::filesystem::path path, OpenFileEvent::Callback callback, SubmitGuard* submit) {
        openFile(std::move(path), 0, 0, std::move(callback));
    }

    void EventLoop::openFile(std::filesystem::path path, int flags, mode_t mode, OpenFileEvent::Callback callback, SubmitGuard* submit) {
        auto& event = createEvent<OpenFileEvent>(std::move(path), flags, mode, std::move(callback));
        try {
            openFile(event, submit);
        } catch (const EventLoopException& e) {
            removeEvent(event.id);
            throw;
        }
    }

    void EventLoop::openFile(OpenFileEvent& event, SubmitGuard* submit) {
        auto sqe = getSqe();

        io_uring_prep_openat(sqe, AT_FDCWD, event.path.c_str(), event.flags, event.mode);
        sqe->user_data = event.id;

        submitRing(submit);
    }

    void EventLoop::readFile(File file, Buffer buffer, std::uint64_t offset, ReadFileEvent::Callback callback, SubmitGuard* submit) {
        auto& event = createEvent<ReadFileEvent>(file, std::move(buffer), offset, std::move(callback));
        try {
            readFile(event, submit);
        } catch (const EventLoopException& e) {
            removeEvent(event.id);
            throw;
        }
    }

    void EventLoop::readFile(ReadFileEvent& event, SubmitGuard* submit) {
        auto sqe = getSqe();

        io_uring_prep_read(sqe, event.file.fd, event.buffer.data(), event.buffer.size(), event.offset);
        sqe->user_data = event.id;

        submitRing(submit);
    }

    void EventLoop::writeFile(File file, Buffer data, WriteFileEvent::Callback callback, SubmitGuard* submit) {
        auto& event = createEvent<WriteFileEvent>(file, std::move(data), std::move(callback));
        try {
            writeFile(event, submit);
        } catch (const EventLoopException& e) {
            removeEvent(event.id);
            throw;
        }
    }

    void EventLoop::writeFile(WriteFileEvent& event, SubmitGuard* submit) {
        auto sqe = getSqe();

        io_uring_prep_write(sqe, event.file.fd, event.data.data(), event.data.size(), 0);
        sqe->user_data = event.id;

        submitRing(submit);
    }

    void EventLoop::readFileStats(std::filesystem::path path, ReadFileStatsEvent::Callback callback, SubmitGuard* submit) {
        auto& event = createEvent<ReadFileStatsEvent>(std::move(path), std::move(callback));
        try {
            readFileStats(event, submit);
        } catch (const EventLoopException& e) {
            removeEvent(event.id);
            throw;
        }
    }

    void EventLoop::readFileStats(ReadFileStatsEvent& event, SubmitGuard* submit) {
        auto sqe = getSqe();

        io_uring_prep_statx(sqe, AT_FDCWD, event.path.c_str(), event.flags, event.mask, &event.stats);
        sqe->user_data = event.id;

        submitRing(submit);
    }

    void EventLoop::readLine(Buffer buffer, ReadLineEvent::Callback callback, SubmitGuard* submit) {
        readFile(
            File::stdinFile(),
            std::move(buffer),
            0,
            [line = std::string(), callback = std::move(callback)](EventContext& context, const ReadFileEvent::Response& response) mutable {
                if (!callback) {
                    return false;
                }

                char* text = (char*)response.data;
                for (std::size_t i = 0; i < response.size; i++) {
                    auto current = text[i];
                    line += current;
                    if (current == '\n') {
                        if (!callback(context, { line })) {
                            return false;
                        }

                        line.clear();
                    }
                }

                return true;
            },
            submit
        );
    }

    void EventLoop::printFile(File file, const std::string_view& string, WriteFileEvent::Callback callback, SubmitGuard* submit) {
        auto buffer = mBufferManager.allocate(string.size());
        memcpy(buffer.data(), string.data(), string.size());
        writeFile(
            file,
            buffer,
            [buffer, callback = std::move(callback)](EventContext& context, const WriteFileEvent::Response& response) {
                context.eventLoop.deallocate(buffer);

                if (callback) {
                    callback(context, response);
                }
            },
            submit
        );
    }

    void EventLoop::printStdout(const std::string_view& string, WriteFileEvent::Callback callback, SubmitGuard* submit) {
        printFile(File::stdoutFile(), string, std::move(callback), submit);
    }

    void EventLoop::printStderr(const std::string_view& string, WriteFileEvent::Callback callback, SubmitGuard* submit) {
        printFile(File::stderrFile(), string, std::move(callback), submit);
    }

    Buffer EventLoop::allocate(std::size_t size) {
        return mBufferManager.allocate(size);
    }

    void EventLoop::deallocate(Buffer buffer) {
        mBufferManager.deallocate(std::move(buffer));
    }

    void EventLoop::submitRing(SubmitGuard* submit) {
        if (submit != nullptr) {
            submit->submit();
        } else {
            EventLoopException::throwIfFailed(io_uring_submit(&mRing), "io_uring_submit");
        }
    }

    io_uring_sqe* EventLoop::getSqe() {
        io_uring_sqe* sqe = io_uring_get_sqe(&mRing);
        if (sqe == nullptr) {
            throw EventLoopException("io_uring_get_sqe", -1);
        }

        return sqe;
    }

    void EventLoop::removeEvent(EventId id) {
        mEvents.erase(id);
    }
}

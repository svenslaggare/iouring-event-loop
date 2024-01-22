#include "loop.h"
#include "events.h"

#include <fcntl.h>
#include <cxxabi.h>
#include <mutex>

namespace event_loop {
    namespace {
        __kernel_timespec createKernelTimeSpec(std::chrono::nanoseconds delay) {
            __kernel_timespec timespec {};
            timespec.tv_sec = delay.count() / std::chrono::nanoseconds::period::den;
            timespec.tv_nsec = delay.count() % std::chrono::nanoseconds::period::den;
            return timespec;
        }
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

        sockaddr_in serverAddress {};
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_addr = address;
        serverAddress.sin_port = htons(port);
        serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);

        EventLoopException::throwIfFailed(
            bind(socketFd, (const sockaddr*)&serverAddress, sizeof(serverAddress)),
            "bind"
        );

        EventLoopException::throwIfFailed(listen(socketFd, backlog), "listen");
        return TcpListener { Socket { socketFd }, serverAddress };
    }

    void EventLoop::accept(TcpListener& listener, AcceptEvent::Callback callback, SubmitGuard* submit) {
        auto& event = createEvent<AcceptEvent>(listener.socket(), std::move(callback));
        try {
            accept(event, submit);
        } catch (const EventLoopException& e) {
            removeEvent(event.id);
            throw;
        }
    }

    void EventLoop::accept(AcceptEvent& event, SubmitGuard* submit) {
        auto sqe = getSqe();

        io_uring_prep_accept(sqe, event.server.fd, (sockaddr*)&event.clientAddress, &event.clientAddressLength, 0);
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

    void EventLoop::connect(ConnectEvent& event, SubmitGuard* submit) {
        auto sqe = getSqe();

        io_uring_prep_connect(sqe, event.client.fd, (sockaddr*)&event.serverAddress, sizeof(event.serverAddress));
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

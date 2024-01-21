#include <iostream>
#include <set>

#include <fcntl.h>
#include <map>

#include "event_loop/loop.h"
#include "event_loop/buffer.h"

std::tuple<std::string, std::uint16_t> getEndpoint(const sockaddr_in& address) {
    char charsIp[INET_ADDRSTRLEN] = {};
    inet_ntop(address.sin_family, &(address.sin_addr), charsIp, INET_ADDRSTRLEN);
    return { std::string(charsIp), ntohs(address.sin_port) };
}

struct ChatClient {
    event_loop::Socket socket;
    sockaddr_in address {};
};

std::ostream& operator<<(std::ostream& os, const ChatClient& client) {
    auto [ip, port] = getEndpoint(client.address);
    os << client.socket << " = " << ip << ":" << port;
    return os;
}

int mainChatServer(int argc, char* argv[]) {
    using namespace std::chrono_literals;
    using namespace event_loop;

    std::stop_source stopSource;
    EventLoop eventLoop;

    auto tcpListener = eventLoop.tcpListen({}, 9000);

    auto [serverIp, severPort] = getEndpoint(tcpListener.address());
    std::cout << "Server socket: " << tcpListener.socket() << " = " << serverIp << ":" << severPort << std::endl;

    std::map<Socket, ChatClient> clients;
    auto sendCallback = [&clients](EventContext& context, const SendEvent::Response& response) {
        if (response.size == 0) {
            clients.erase(response.client);
        }
    };

    eventLoop.accept(tcpListener, [&](EventContext& context, const AcceptEvent::Response& response) {
        ChatClient client { response.client, response.clientAddress };
        std::cout << "Accepted client: " << client << std::endl;
        clients.insert({ response.client, client });

        context.eventLoop.receive(response.client, Buffer { 1024 }, [&clients, &sendCallback](EventContext& context, const ReceiveEvent::Response& response) {
            if (response.size == 0) {
                clients.erase(response.client);
                return false;
            }

            std::string text { (char*)response.data, response.size };
            std::cout << "Message: " << text;

            if (text == "exit\n") {
                clients.erase(response.client);
                context.eventLoop.close(response.client, {});
                return false;
            }

            auto output = Buffer::fromString("Other: " + text);

            SubmitGuard submitGuard(context.eventLoop);
            for (auto& [_, currentClient] : clients) {
                if (response.client != currentClient.socket) {
                    context.eventLoop.send(currentClient.socket, output, sendCallback, &submitGuard);
                }
            }

            return true;
        });

        return true;
    });

    eventLoop.timer(7.5s, [&clients, &sendCallback](EventContext& context, const TimerEvent::Response& response) {
        std::cout << "Broadcasting message (elapsed: " << response.elapsed << ")" << std::endl;

        auto output = Buffer::fromString("Hello, All!\n");

        SubmitGuard submitGuard(context.eventLoop);
        for (auto& [_, currentClient] : clients) {
            context.eventLoop.send(currentClient.socket, output, sendCallback, &submitGuard);
        }

        return true;
    });

    eventLoop.run(stopSource);

    return 0;
}

int mainFile(int argc, char* argv[]) {
    using namespace event_loop;

    std::stop_source stopSource;
    EventLoop eventLoop;

    eventLoop.openFile("/home/antjans/lorem.txt", [](EventContext& context, const OpenFileEvent::Response& response) {
        std::cout << "Opened file: " << response.file << std::endl;
        if (response.file) {
            std::string text;
            context.eventLoop.readFile(response.file, Buffer { 256 }, 0, [text](EventContext& context, const ReadFileEvent::Response& response) mutable {
                text += std::string { (char*)response.data, response.size };

                if (response.size == 0) {
                    std::cout << text;
                    return false;
                } else {
                    return true;
                }
            });
        }
    });

    eventLoop.openFile("/home/antjans/output.txt", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, [](EventContext& context, const OpenFileEvent::Response& response) {
        std::cout << "Opened file: " << response.file << std::endl;
        if (response.file) {
            auto output = Buffer::fromString("Hello, World, all my friends!");

            context.eventLoop.writeFile(response.file, std::move(output), [](EventContext& context, const WriteFileEvent::Response& response) {
                context.eventLoop.close(response.file, {});
            });
        }
    });

    eventLoop.run(stopSource);

    return 0;
}

int main(int argc, char* argv[]) {
    return mainChatServer(argc, argv);
//    return mainFile(argc, argv);
}
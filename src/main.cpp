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

    auto [serverIp, serverPort] = getEndpoint(tcpListener.address());
    std::cout << "Server socket: " << tcpListener.socket() << " = " << serverIp << ":" << serverPort << std::endl;

    std::map<Socket, ChatClient> clients;
    auto removeClient = [&](Socket client) {
        if (clients.erase(client) > 0) {
            std::cout << "Client: " << client << " disconnected" << std::endl;
        }
    };

    auto sendCallback = [&removeClient](EventContext& context, const SendEvent::Response& response) {
        if (response.size == 0) {
            removeClient(response.client);
        }
    };

    eventLoop.accept(tcpListener, [&](EventContext& context, const AcceptEvent::Response& response) {
        ChatClient client { response.client, response.clientAddress };
        std::cout << "Accepted client: " << client << std::endl;
        clients.insert({ response.client, client });

        context.eventLoop.receive(response.client, Buffer { 1024 }, [&clients, &sendCallback, &removeClient](EventContext& context, const ReceiveEvent::Response& response) {
            if (response.size == 0) {
                removeClient(response.client);
                return false;
            }

            std::string text { (char*)response.data, response.size };
            std::cout << "Message: " << text;

            if (text == "exit\n") {
                removeClient(response.client);
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

    eventLoop.dispatch([](EventLoop& eventLoop) {
       std::cout << "Dispatched callback..." << std::endl;
    });

    eventLoop.run(stopSource);

    return 0;
}

int mainChatClient(int argc, char* argv[]) {
    using namespace std::chrono_literals;
    using namespace event_loop;

    std::stop_source stopSource;
    EventLoop eventLoop;

    eventLoop.connect(inet_addr("127.0.0.1"), 9000, [](EventContext& context, const ConnectEvent::Response& response) {
        if (response.error) {
            std::cout << "Failed to connect due to: " << *response.error << std::endl;
            return;
        }

        auto [serverIp, serverPort] = getEndpoint(response.serverAddressInet());
        std::cout << "Connected to server: " << response.client << " = " << serverIp << ":" << serverPort << std::endl;

        context.eventLoop.receive(response.client, Buffer { 1024 }, [](EventContext& context, const ReceiveEvent::Response& response) {
            std::string text { (char*)response.data, response.size };
            std::cout << text;
            return true;
        });

        auto client = response.client;
        context.eventLoop.readLine(Buffer { 256 }, [client](EventContext& context, const ReadLineEvent::Response    & response) {
            context.eventLoop.send(client, Buffer::fromString(response.line), {});
            return true;
        });
    });

    eventLoop.run(stopSource);

    return 0;
}

int mainUdpServer(int argc, char* argv[]) {
    using namespace std::chrono_literals;
    using namespace event_loop;

    std::stop_source stopSource;
    EventLoop eventLoop;

    auto udpSocket = eventLoop.udpReceiver({}, 9000);

    eventLoop.receive(udpSocket, Buffer { 1024 }, [](EventContext& context, const ReceiveEvent::Response& response) {
        if (response.size == 0) {
            return false;
        }

        std::string text { (char*)response.data, response.size };
        std::cout << "Message: " << text;

        return true;
    });

    eventLoop.run(stopSource);

    return 0;
}

int mainChatServerUDS(int argc, char* argv[]) {
    using namespace std::chrono_literals;
    using namespace event_loop;

    std::stop_source stopSource;
    EventLoop eventLoop;

    auto udsListener = eventLoop.udsListen("test.sock");

    std::cout << "Server socket: " << udsListener.socket() << std::endl;

    std::map<Socket, ChatClient> clients;
    auto removeClient = [&](Socket client) {
        if (clients.erase(client) > 0) {
            std::cout << "Client: " << client << " disconnected" << std::endl;
        }
    };

    auto sendCallback = [&removeClient](EventContext& context, const SendEvent::Response& response) {
        if (response.size == 0) {
            removeClient(response.client);
        }
    };

    eventLoop.accept(udsListener, [&](EventContext& context, const AcceptEvent::Response& response) {
        ChatClient client { response.client, response.clientAddress };
        std::cout << "Accepted client: " << client << std::endl;
        clients.insert({ response.client, client });

        context.eventLoop.receive(response.client, Buffer { 1024 }, [&clients, &sendCallback, &removeClient](EventContext& context, const ReceiveEvent::Response& response) {
            if (response.size == 0) {
                removeClient(response.client);
                return false;
            }

            std::string text { (char*)response.data, response.size };
            std::cout << "Message: " << text;

            if (text == "exit\n") {
                removeClient(response.client);
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

int mainChatClientUDS(int argc, char* argv[]) {
    using namespace std::chrono_literals;
    using namespace event_loop;

    std::stop_source stopSource;
    EventLoop eventLoop;

    eventLoop.connect("test.sock", [](EventContext& context, const ConnectEvent::Response& response) {
        if (response.error) {
            std::cout << "Failed to connect due to: " << *response.error << std::endl;
            return;
        }

        std::cout << "Connected to server: " << response.client << " - " << response.serverAddressUnix().sun_path << std::endl;

        context.eventLoop.receive(response.client, Buffer { 1024 }, [](EventContext& context, const ReceiveEvent::Response& response) {
            std::string text { (char*)response.data, response.size };
            std::cout << text;
            return true;
        });

        auto client = response.client;
        context.eventLoop.readLine(Buffer { 256 }, [client](EventContext& context, const ReadLineEvent::Response    & response) {
            context.eventLoop.send(client, Buffer::fromString(response.line), {});
            return true;
        });
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
                }  else {
                    return true;
                }
            });
        }
    });

    eventLoop.readFileStats("/home/antjans/lorem.txt", [](EventContext& context, const ReadFileStatsEvent::Response& response) {
        if (response.stats) {
            std::cout << response.stats->stx_size << std::endl;
        } else {
            std::cout << "Failed to open file due to: " << *tryExtractError(context.result) << std::endl;
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
    std::string command = "server";
    if (argc >= 2) {
        command = argv[1];
    }

    if (command == "server") {
        return mainChatServer(argc, argv);
    } else if (command == "client") {
        return mainChatClient(argc, argv);
    } else if (command == "udp_server") {
        return mainUdpServer(argc, argv);
    } else if (command == "uds_server") {
        return mainChatServerUDS(argc, argv);
    } else if (command == "uds_client") {
        return mainChatClientUDS(argc, argv);
    } else if (command == "file") {
        return mainFile(argc, argv);
    }
}
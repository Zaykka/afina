#include "ServerImpl.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <spdlog/logger.h>

#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <afina/logging/Service.h>
#include <thread>
#include "protocol/Parser.h"

namespace Afina {
namespace Network {
namespace MTblocking {

// See Server.h
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps, std::shared_ptr<Logging::Service> pl) : Server(ps, pl) {}

// See Server.h
ServerImpl::~ServerImpl() {}

// See Server.h
void ServerImpl::Start(uint16_t port, uint32_t n_accept, uint32_t n_workers) {
    max_workers = n_workers;
    workers = 0;
    _logger = pLogging->select("network");
    _logger->info("Start mt_blocking network service");

    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0) {
        throw std::runtime_error("Unable to mask SIGPIPE");
    }

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;         // IPv4
    server_addr.sin_port = htons(port);       // TCP port number
    server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any address

    _server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_server_socket == -1) {
        throw std::runtime_error("Failed to open socket");
    }

    int opts = 1;
    if (setsockopt(_server_socket, SOL_SOCKET, SO_REUSEADDR, &opts, sizeof(opts)) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket setsockopt() failed");
    }

    if (bind(_server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket bind() failed");
    }

    if (listen(_server_socket, 5) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket listen() failed");
    }

    running.store(true);
    _thread = std::thread(&ServerImpl::OnRun, this);
}

// See Server.h
void ServerImpl::Stop() {
    running.store(false);
    {
        std::unique_lock<std::mutex> lock(sockets_block);
        max_workers = 0;
    }
    close(_server_socket);
    //shutdown(_server_socket, SHUT_RDWR);
}

// See Server.h
void ServerImpl::Join() {
//    assert(_thread.joinable());
//    _thread.join();
//    close(_server_socket);
    std::unique_lock<std::mutex> lock(sockets_block);
    while (running || workers) {
        ended.wait(lock);
    }
}

// See Server.h
void ServerImpl::OnRun() {
    // Here is connection state
    // - parser: parse state of the stream
    // - command_to_execute: last command parsed out of stream
    // - arg_remains: how many bytes to read from stream to get command argument
    // - argument_for_command: buffer stores argument
//    std::size_t arg_remains;
//    Protocol::Parser parser;
//    std::string argument_for_command;
//    std::unique_ptr<Execute::Command> command_to_execute;
    while (running.load()) {
        _logger->debug("waiting for connection...");

        // The call to accept() blocks until the incoming connection arrives
        int client_socket;
        struct sockaddr client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        if ((client_socket = accept(_server_socket, (struct sockaddr *)&client_addr, &client_addr_len)) == -1) {
            continue;
        }

        // Got new connection
        if (_logger->should_log(spdlog::level::debug)) {
            std::string host = "unknown", port = "-1";

            char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
            if (getnameinfo(&client_addr, client_addr_len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
                            NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
                host = hbuf;
                port = sbuf;
            }
            _logger->debug("Accepted connection on descriptor {} (host={}, port={})\n", client_socket, host, port);
        }

        // Configure read timeout
        {
            struct timeval tv;
            tv.tv_sec = 5; // TODO: make it configurable
            tv.tv_usec = 0;
            setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
        }

//        // TODO: Start new thread and process data from/to connection
//        {
//            static const std::string msg = "TODO: start new thread and process memcached protocol instead";
//            if (send(client_socket, msg.data(), msg.size(), 0) <= 0) {
//                _logger->error("Failed to write response to client: {}", strerror(errno));
//            }
//            close(client_socket);
//        }

        std::unique_lock<std::mutex> lock(sockets_block);

        if (workers < max_workers) {
            workers++;
            //sockets.insert(client_socket);
            lock.unlock();
            std::thread th(&ServerImpl::Worker, this, client_socket);
            th.detach();
        } else {
            close(client_socket);
        }
    }

    // Cleanup on exit...
    _logger->warn("Network stopped");
}

void ServerImpl::Worker(int client_socket) {
    std::size_t arg_remains;
    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;

    int readed = 0;
    std::string args; // = ""
    bool keep_going=true;
    char buffer[2048];

    try {
        while(keep_going && (readed = read(client_socket, buffer+readed, sizeof(buffer) - readed)) > 0) {
            while(readed) {
                if (!command_to_execute) {
                    size_t parsed;
                    if (parser.Parse(buffer, readed, parsed)) {
                        command_to_execute = parser.Build(arg_remains);

                        if (arg_remains) {
                            arg_remains += 2;
                        }
                    }
                    if (parsed) {
                        readed -= parsed;
                        std::memmove(buffer, buffer+parsed, readed);
                    } else if (!command_to_execute) {
                        break;
                    }
                }

                if (command_to_execute && arg_remains > 0) {
                    auto len  = std::min(int(arg_remains), readed);
                    args.append(buffer, len);
                    readed -= len;
                    arg_remains -= len;
                    std::memmove(buffer, buffer + len, readed);
                }

                if (command_to_execute && !arg_remains) {
                    std::string res;
                    command_to_execute->Execute(*pStorage, args, res);
                    res += "\r\n";
                    if (send(client_socket, res.data(), res.size(),0) == -1) {
                        throw std::runtime_error("Send failed");
                    }

                    parser.Reset();
                    command_to_execute.reset();
                    args.clear();
                    if (!running) {
                        keep_going = false;
                        readed = 0;
                        break;
                    }
                }
            }
        }
        if (readed ==0) {
            _logger->debug("Connection closed");
        } else {
            throw std::runtime_error("Can't read");
        }
    }
    catch (std::exception er) {
        _logger->error("{}",er.what());
    }

    close(client_socket);
    {
        std::unique_lock<std::mutex> lock(sockets_block);
        //sockets.erase(client_socket);
        workers--;
        if (!running && !workers) {
            ended.notify_all();
        }
    }
}
} // namespace MTblocking
} // namespace Network
} // namespace Afina

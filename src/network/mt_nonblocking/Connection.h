#ifndef AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H

#include <cstring>

#include <sys/epoll.h>
#include <spdlog/logger.h>
#include <afina/execute/Command.h>
#include <protocol/Parser.h>


namespace Afina {
    namespace Network {
        namespace MTnonblock {

            class Connection {
            public:
                Connection(int s, std::shared_ptr<Afina::Storage> ps, std::shared_ptr<spdlog::logger> pl) :
                        _socket(s), _logger(std::move(pl)), pStorage(std::move(ps)) {
                    std::memset(&_event, 0, sizeof(struct epoll_event));
                    _event.data.ptr = this;
                }

                inline bool isAlive() const { return _is_alive; }

                void Start();

            protected:
                void OnError();
                void OnClose();
                void DoRead();
                void DoWrite();

            private:
                friend class Worker;
                friend class ServerImpl;

                int _socket;
                struct epoll_event _event;

                std::mutex _mutex;

                std::shared_ptr<spdlog::logger> _logger;

                bool _is_alive = true;
                char client_buffer[4096];
                std::size_t arg_remains;
                Protocol::Parser parser;
                std::string argument_for_command;
                std::unique_ptr<Execute::Command> command_to_execute;
                std::shared_ptr<Afina::Storage> pStorage;

                std::vector<std::string> buffer;

                int cur_pos = 0;
                int now_pos = 0;
            };

        } // namespace MTnonblock
    } // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
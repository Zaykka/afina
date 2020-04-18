#include "Connection.h"

#include <iostream>

namespace Afina {
    namespace Network {
        namespace MTnonblock {

// See Connection.h
            void Connection::Start() {
                std::lock_guard<std::mutex> lock(_mutex);
                _event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR;
                _event.data.fd = _socket;
                _logger->debug("Connection on {} socket started", _socket);
            }

// See Connection.h
            void Connection::OnError() {
                std::lock_guard<std::mutex> lock(_mutex);
                _is_alive = false;
                _logger->error("Connection on {} socket has error", _socket);

            }

// See Connection.h
            void Connection::OnClose() {
                std::lock_guard<std::mutex> lock(_mutex);
                _is_alive = false;
                _logger->debug("Connection on {} socket closed", _socket);
            }

// See Connection.h
            void Connection::DoRead() {
                std::lock_guard<std::mutex> lock(_mutex);

                _logger->debug("Do read on {}", _socket);

                try {
                    int readed_bytes = -1;
                    while ((readed_bytes = read(_socket, client_buffer + now_pos, sizeof(client_buffer) - now_pos)) > 0) {
                        _logger->debug("Got {} bytes from socket", readed_bytes);
                        now_pos += readed_bytes;
                        // Single block of data readed from the socket could trigger inside actions a multiple times,
                        // for example:
                        // - read#0: [<command1 start>]
                        // - read#1: [<command1 end> <argument> <command2> <argument for command 2> <command3> ... ]
                        while (now_pos > 0) {
                            _logger->debug("Process {} bytes", now_pos);
                            // There is no command yet
                            if (!command_to_execute) {
                                std::size_t parsed = 0;
                                if (parser.Parse(client_buffer, now_pos, parsed)) {
                                    // There is no command to be launched, continue to parse input stream
                                    // Here we are, current chunk finished some command, process it
                                    _logger->debug("Found new command: {} in {} bytes", parser.Name(), parsed);
                                    command_to_execute = parser.Build(arg_remains);
                                    if (arg_remains > 0) {
                                        arg_remains += 2;
                                    }
                                }

                                // Parsed might fails to consume any bytes from input stream. In real life that could happens,
                                // for example, because we are working with UTF-16 chars and only 1 byte left in stream
                                if (parsed == 0) {
                                    break;
                                } else {
                                    std::memmove(client_buffer, client_buffer + parsed, now_pos - parsed);
                                    now_pos -= parsed;
                                }
                            }

                            // There is command, but we still wait for argument to arrive...
                            if (command_to_execute && arg_remains > 0) {
                                _logger->debug("Fill argument: {} bytes of {}", now_pos, arg_remains);
                                // There is some parsed command, and now we are reading argument
                                std::size_t to_read = std::min(arg_remains, std::size_t(now_pos));
                                argument_for_command.append(client_buffer, to_read);

                                std::memmove(client_buffer, client_buffer + to_read, now_pos - to_read);
                                arg_remains -= to_read;
                                now_pos -= to_read;
                            }

                            // Thre is command & argument - RUN!
                            if (command_to_execute && arg_remains == 0) {
                                _logger->debug("Start command execution");

                                std::string result;
                                command_to_execute->Execute(*pStorage, argument_for_command, result);

                                // Send response
                                result += "\r\n";
                                buffer.push_back(result);
                                _event.events |= EPOLLOUT;

                                // Prepare for the next command
                                command_to_execute.reset();
                                argument_for_command.resize(0);
                                parser.Reset();
                            }
                        } // while (readed_bytes)
                    }

                    _is_alive = false;
                    if (readed_bytes == 0) {
                        _logger->debug("Connection closed");
                    } else {
                        throw std::runtime_error(std::string(strerror(errno)));
                    }
                } catch (std::runtime_error &ex) {
                    _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
                }

            }


// See Connection.h
            void Connection::DoWrite() {
                std::lock_guard<std::mutex> lock(_mutex);
                _logger->debug("Do write on {}", _socket);

                int size = buffer.size();
                struct iovec iovecs[size];
                for (int i = 0; i < size; i++) {
                    iovecs[i].iov_len = buffer[i].size();
                    iovecs[i].iov_base = &(buffer[i][0]);
                }

                iovecs[0].iov_base = static_cast<char *>(iovecs[0].iov_base) + cur_pos;

                int write_bytes = writev(_socket, iovecs, size);

                if (write_bytes <= 0) {
                    _is_alive = false;
                    throw std::runtime_error("Failed to send response");
                }

                cur_pos += write_bytes;

                int i = 0;

                while ((i < size) && ((cur_pos - iovecs[i].iov_len) >= 0)) {
                    i++;
                    cur_pos -= iovecs[i].iov_len;
                }
                buffer.erase(buffer.begin(), buffer.begin() + i);
                if (buffer.empty()) {
                    _event.events =  EPOLLIN | EPOLLRDHUP | EPOLLERR;
                }
            }

        } // namespace MTnonblock
    } // namespace Network
} // namespace Afina
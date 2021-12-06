#include "tcp_sponge_socket.hh"

#include "parser.hh"
#include "tun.hh"
#include "util.hh"

#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

using namespace std;

static constexpr size_t TCP_TICK_MS = 10;

//! \param[in] condition is a function returning true if loop should continue
template <typename AdaptT>
void TCPSpongeSocket<AdaptT>::_tcp_loop(const function<bool()> &condition) {
    auto base_time = timestamp_ms();
    while (condition()) {
        auto ret = _eventloop.wait_next_event(TCP_TICK_MS);
        if (ret == EventLoop::Result::Exit or _abort) {
            break;
        }

        if (_tcp.value().active()) {
            const auto next_time = timestamp_ms();
            _tcp.value().tick(next_time - base_time);
            _datagram_adapter.tick(next_time - base_time);
            base_time = next_time;
        }
    }
}

//! \param[in] data_socket_pair is a pair of connected AF_UNIX SOCK_STREAM sockets
//! \param[in] datagram_interface is the interface for reading and writing datagrams
template <typename AdaptT>
TCPSpongeSocket<AdaptT>::TCPSpongeSocket(pair<FileDescriptor, FileDescriptor> data_socket_pair,
                                         AdaptT &&datagram_interface)
    : LocalStreamSocket(move(data_socket_pair.first))
    , _thread_data(move(data_socket_pair.second))
    , _datagram_adapter(move(datagram_interface)) {
    _thread_data.set_blocking(false);
}

template <typename AdaptT>
void TCPSpongeSocket<AdaptT>::_initialize_TCP(const TCPConfig &config) {
    _tcp.emplace(config);

    // Set up the event loop

    // There are four possible events to handle:
    //
    // 1) Incoming datagram received (needs to be given to
    //    TCPConnection::segment_received method)
    //
    // 2) Outbound bytes received from local application via a write()
    //    call (needs to be read from the local stream socket and
    //    given to TCPConnection::data_written method)
    //
    // 3) Incoming bytes reassembled by the TCPConnection
    //    (needs to be read from the inbound_stream and written
    //    to the local stream socket back to the application)
    //
    // 4) Outbound segment generated by TCP (needs to be
    //    given to underlying datagram socket)

    // rule 1: read from filtered packet stream and dump into TCPConnection
    _eventloop.add_rule(
        _datagram_adapter,
        Direction::In,
        [&] {
            auto seg = _datagram_adapter.read();
            if (seg) {
                _tcp->segment_received(move(seg.value()));
            }

            // debugging output:
            if (_thread_data.eof() and _tcp.value().bytes_in_flight() == 0 and not _fully_acked) {
                cerr<<"rule111111111111111111111111111111111111111111111111111111111111111\r\n"<<endl;
                cerr << "DEBUG: Outbound stream to " << _datagram_adapter.config().destination.to_string()
                     << " has been fully acknowledged.\n";
                _fully_acked = true;
            }
        },
        [&] {

            return _tcp->active(); });

    // rule 2: read from pipe into outbound buffer
    _eventloop.add_rule(
        _thread_data,
        Direction::In,
        [&] {

            const auto data = _thread_data.read(_tcp->remaining_outbound_capacity());
            const auto len = data.size();
            const auto amount_written = _tcp->write(move(data));
            cerr<<"acutal write "<<amount_written<< " data size"<<len<<endl;
            if (amount_written != len) {
                throw runtime_error("TCPConnection::write() accepted less than advertised length");
            }
            if (_thread_data.eof()) {
                _tcp->end_input_stream();
                _outbound_shutdown = true;
                cerr<<"rule22222222222222222222222222222222222222222222222222222222222222\r\n"<<endl;
                // debugging output:
                cerr << "DEBUG: Outbound stream to " << _datagram_adapter.config().destination.to_string()
                     << " finished (" << _tcp.value().bytes_in_flight() << " byte"
                     << (_tcp.value().bytes_in_flight() == 1 ? "" : "s") << " still in flight).\n";
            }
        },
        [&] {
            return (_tcp->active()) and (not _outbound_shutdown) and (_tcp->remaining_outbound_capacity() > 0); },
        [&] {
            _tcp->end_input_stream();
            _outbound_shutdown = true;
        });

    // rule 3: read from inbound buffer into pipe
    _eventloop.add_rule(
        _thread_data,
        Direction::Out,
        [&] {
            ByteStream &inbound = _tcp->inbound_stream();
            // Write from the inbound_stream into
            // the pipe, handling the possibility of a partial
            // write (i.e., only pop what was actually written).
            const size_t amount_to_write = min(size_t(65536), inbound.buffer_size());
            const std::string buffer = inbound.peek_output(amount_to_write);
            const auto bytes_written = _thread_data.write(move(buffer), false);
            inbound.pop_output(bytes_written);
            cerr<<"receiver fetch inbound "<<bytes_written<<" buffer.size "<< buffer.size()<<endl;
            if (inbound.eof() or inbound.error()) {
                _thread_data.shutdown(SHUT_WR);
                _inbound_shutdown = true;
                cerr<<"rule33333333333333333333333333333333333333333333333333333333333333333"<<endl;
                // debugging output:
                cerr << "DEBUG: Inbound stream from " << _datagram_adapter.config().destination.to_string()
                     << " finished " << (inbound.error() ? "with an error/reset.\n" : "cleanly.\n");
                if (_tcp.value().state() == TCPState::State::TIME_WAIT) {
                    cerr << "DEBUG: Waiting for lingering segments (e.g. retransmissions of FIN) from peer...\n";
                }
            }
        },
        [&] {

            return (not _tcp->inbound_stream().buffer_empty()) or
                   ((_tcp->inbound_stream().eof() or _tcp->inbound_stream().error()) and not _inbound_shutdown);
        });

    // rule 4: read outbound segments from TCPConnection and send as datagrams
    _eventloop.add_rule(
        _datagram_adapter,
        Direction::Out,
        [&] {
            while (not _tcp->segments_out().empty()) {
                _datagram_adapter.write(_tcp->segments_out().front());
                _tcp->segments_out().pop();
            }
        },
        [&] {
            return not _tcp->segments_out().empty(); });
}

//! \brief Call [socketpair](\ref man2::socketpair) and return connected Unix-domain sockets of specified type
//! \param[in] type is the type of AF_UNIX sockets to create (e.g., SOCK_SEQPACKET)
//! \returns a std::pair of connected sockets
static inline pair<FileDescriptor, FileDescriptor> socket_pair_helper(const int type) {
    int fds[2];
    SystemCall("socketpair", ::socketpair(AF_UNIX, type, 0, static_cast<int *>(fds)));
    return {FileDescriptor(fds[0]), FileDescriptor(fds[1])};
}

//! \param[in] datagram_interface is the underlying interface (e.g. to UDP, IP, or Ethernet)
template <typename AdaptT>
TCPSpongeSocket<AdaptT>::TCPSpongeSocket(AdaptT &&datagram_interface)
    : TCPSpongeSocket(socket_pair_helper(SOCK_STREAM), move(datagram_interface)) {}

template <typename AdaptT>
TCPSpongeSocket<AdaptT>::~TCPSpongeSocket() {
    try {
        if (_tcp_thread.joinable()) {
            cerr << "Warning: unclean shutdown of TCPSpongeSocket\n";
            // force the other side to exit
            _abort.store(true);
            _tcp_thread.join();
        }
    } catch (const exception &e) {
        cerr << "Exception destructing TCPSpongeSocket: " << e.what() << endl;
    }
}

template <typename AdaptT>
void TCPSpongeSocket<AdaptT>::wait_until_closed() {
    shutdown(SHUT_RDWR);
    if (_tcp_thread.joinable()) {
        cerr << "DEBUG: Waiting for clean shutdown... ";
        _tcp_thread.join();
        cerr << "done.\n";
    }
}

//! \param[in] c_tcp is the TCPConfig for the TCPConnection
//! \param[in] c_ad is the FdAdapterConfig for the FdAdapter
template <typename AdaptT>
void TCPSpongeSocket<AdaptT>::connect(const TCPConfig &c_tcp, const FdAdapterConfig &c_ad) {
    if (_tcp) {
        throw runtime_error("connect() with TCPConnection already initialized");
    }

    _initialize_TCP(c_tcp);

    _datagram_adapter.config_mut() = c_ad;

    cerr << "DEBUG: Connecting to " << c_ad.destination.to_string() << "... ";

    _tcp->connect();

    const TCPState expected_state = TCPState::State::SYN_SENT;

    if (_tcp->state() != expected_state) {
        throw runtime_error("After TCPConnection::connect(), state was " + _tcp->state().name() + " but expected " +
                            expected_state.name());
    }

    _tcp_loop([&] { return _tcp->state() == TCPState::State::SYN_SENT; });
    cerr << "done.\n";

    _tcp_thread = thread(&TCPSpongeSocket::_tcp_main, this);
}

//! \param[in] c_tcp is the TCPConfig for the TCPConnection
//! \param[in] c_ad is the FdAdapterConfig for the FdAdapter
template <typename AdaptT>
void TCPSpongeSocket<AdaptT>::listen_and_accept(const TCPConfig &c_tcp, const FdAdapterConfig &c_ad) {
    if (_tcp) {
        throw runtime_error("listen_and_accept() with TCPConnection already initialized");
    }

    _initialize_TCP(c_tcp);

    _datagram_adapter.config_mut() = c_ad;
    _datagram_adapter.set_listening(true);

    cerr << "DEBUG: Listening for incoming connection... ";
    _tcp_loop([&] {
        const auto s = _tcp->state();
        return (s == TCPState::State::LISTEN or s == TCPState::State::SYN_RCVD or s == TCPState::State::SYN_SENT);
    });
    cerr << "new connection from " << _datagram_adapter.config().destination.to_string() << ".\n";

    _tcp_thread = thread(&TCPSpongeSocket::_tcp_main, this);
}

template <typename AdaptT>
void TCPSpongeSocket<AdaptT>::_tcp_main() {
    try {
        if (not _tcp.has_value()) {
            throw runtime_error("no TCP");
        }
        _tcp_loop([] { return true; });
        shutdown(SHUT_RDWR);
        if (not _tcp.value().active()) {
            cerr << "DEBUG: TCP connection finished "
                 << (_tcp.value().state() == TCPState::State::RESET ? "uncleanly" : "cleanly.\n");
        }
        _tcp.reset();
    } catch (const exception &e) {
        cerr << "Exception in TCPConnection runner thread: " << e.what() << "\n";
        throw e;
    }
}

//! Specialization of TCPSpongeSocket for TCPOverUDPSocketAdapter
template class TCPSpongeSocket<TCPOverUDPSocketAdapter>;

//! Specialization of TCPSpongeSocket for TCPOverIPv4OverTunFdAdapter
template class TCPSpongeSocket<TCPOverIPv4OverTunFdAdapter>;

//! Specialization of TCPSpongeSocket for LossyTCPOverUDPSocketAdapter
template class TCPSpongeSocket<LossyTCPOverUDPSocketAdapter>;

//! Specialization of TCPSpongeSocket for LossyTCPOverIPv4OverTunFdAdapter
template class TCPSpongeSocket<LossyTCPOverIPv4OverTunFdAdapter>;

CS144TCPSocket::CS144TCPSocket() : TCPOverIPv4SpongeSocket(TCPOverIPv4OverTunFdAdapter(TunFD("tun144"))) {}

void CS144TCPSocket::connect(const Address &address) {
    TCPConfig tcp_config;
    tcp_config.rt_timeout = 100;

    FdAdapterConfig multiplexer_config;
    multiplexer_config.source = {"169.254.144.9", to_string(uint16_t(random_device()()))};
    multiplexer_config.destination = address;

    TCPOverIPv4SpongeSocket::connect(tcp_config, multiplexer_config);
}

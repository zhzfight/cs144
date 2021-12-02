#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _receiver.window_size(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _ticks_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    std::cout << "get seg:" << seg.header().to_string() << endl;
    // convey to _receiver, extract the data in payload,
    bool in_window = _receiver.segment_received(seg);
    std::cout << "is this in window" << in_window << endl;
    _ticks_since_last_segment_received = 0;
    if (seg.header().rst && in_window) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _linger_after_streams_finish = false;
        return;
    }

    if (seg.length_in_sequence_space() != 0) {
        _recent_need_to_ack_instantly = true;
    }

    // ???
    if (_receiver.ackno().has_value() and (seg.length_in_sequence_space() == 0) and
        seg.header().seqno == _receiver.ackno().value() - 1) {
        _sender.send_empty_segment();
        send();
        return;
    }

    // ack the _sender's _flight
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
        if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
            TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED) {
            _fin_be_ack = true;
        }
    }
    // is this need to return ack instantly?

    if (seg.header().fin) {
        std::cout << "_receiver:" << TCPState::state_summary(_receiver)
                  << " _sender:" << TCPState::state_summary(_sender) << endl;
        if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
            TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED) {
            std::cout << "set linger to false" << endl;
            _linger_after_streams_finish = false;
        }

        _recent_need_to_ack_instantly = true;
        if (seg.header().ack) {
            send();
            return;
        }
    }
    if (seg.header().syn) {
        _recent_need_to_ack_instantly = true;
    }

    // fill the window
    _sender.fill_window();

    send();
}

bool TCPConnection::active() const {
    std::cout << "finbeack " << _fin_be_ack << " receiver " << _receiver.stream_out().input_ended()
              << _linger_after_streams_finish;
    return !_fin_be_ack || !_receiver.stream_out().input_ended() || _linger_after_streams_finish;
}

size_t TCPConnection::write(const string &data) {
    size_t actual_write = _sender.stream_in().write(data);
    _sender.fill_window();
    send();
    return actual_write;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _ticks_since_last_segment_received += ms_since_last_tick;
    //_ticks_since_last_ack += ms_since_last_tick;
    if (_linger_after_streams_finish) {
        if (_ticks_since_last_segment_received >= 10 * _cfg.rt_timeout) {
            _linger_after_streams_finish = false;
        }
    }
    _sender.tick(ms_since_last_tick);
    if (_sender.consecutive_retransmissions() >= TCPConfig::MAX_RETX_ATTEMPTS) {
        delete this;
    }
    send();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    send();
}

void TCPConnection::connect() {
    _sender.fill_window();
    send();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            // Your code here: need to send a RST segment to the peer

            TCPSegment seg;
            seg.header().rst = true;
            seg.header().seqno = _sender.next_seqno();
            segments_out().push(seg);
            send();

            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
            _linger_after_streams_finish = false;
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
void TCPConnection::send() {
    if (_sender.segments_out().empty()) {
        std::cout << "sender segment_out empty "
                  << "ack " << _recent_need_to_ack_instantly << " receiver has value" << _receiver.ackno().has_value()
                  << endl;
        if ((_recent_need_to_ack_instantly) && _receiver.ackno().has_value()) {
            TCPSegment seg;
            seg.header().ack = true;
            seg.header().seqno = _sender.next_seqno();
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
            std::cout << "lazy send ack:" << seg.header().to_string() << endl;
            segments_out().push(seg);

            _recent_need_to_ack_instantly = false;
            //_ticks_since_last_ack = 0;
        }
        return;
    }

    std::cout << "sender segment_out not empty" << endl;
    while (!_sender.segments_out().empty()) {
        if (_sender.segments_out().size() == 1) {
            if (_receiver.ackno().has_value()) {
                _sender.segments_out().back().header().ack = true;
                _sender.segments_out().back().header().ackno = _receiver.ackno().value();
                std::cout << "atach ack" << _sender.segments_out().back().header().ackno << endl;
                _sender.segments_out().back().header().win = _receiver.window_size();
                _recent_need_to_ack_instantly = false;
                //_ticks_since_last_ack=0;
            }
        }
        _segments_out.push(_sender.segments_out().front());
        std::cout << "send seg:" << _sender.segments_out().front().header().to_string() << endl;
        _sender.segments_out().pop();
    }
}

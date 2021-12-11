#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;
size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _ticks_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {

    if (seg.header().rst) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        return;
    }

    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::LISTEN) {
        if (!seg.header().syn) {
            return;
        }
    }
    _ticks_since_last_segment_received = 0;

    if (TCPState::state_summary(_receiver) != TCPReceiverStateSummary::FIN_RECV) {
        _receiver.segment_received(seg);
    }
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED) {
        _linger_after_streams_finish = false;
    }

    if (seg.length_in_sequence_space() != 0) {
        _recent_need_to_ack_instantly = true;
    }
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
    }

    _sender.fill_window();
    send();
    //cerr<<"sender input end "<<_sender.stream_in().input_ended()<<endl;
}

bool TCPConnection::active() const {
    if (_sender.stream_in().error() || _receiver.stream_out().error()) {
        return false;
    }
    return !(TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED &&
             TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV && !_linger_after_streams_finish);
}

size_t TCPConnection::write(const string &data) {
    if (_sender.stream_in().input_ended()) {
        return 0;
    }
    size_t actual_write = _sender.stream_in().write(data);
    if (actual_write > 0) {
        _sender.fill_window();
        send();
    }
    return actual_write;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _ticks_since_last_segment_received += ms_since_last_tick;
    //_ticks_since_last_ack += ms_since_last_tick;
    if (TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED &&
        TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV && _linger_after_streams_finish) {
        if (_ticks_since_last_segment_received >= 10 * _cfg.rt_timeout) {
            _linger_after_streams_finish = false;
        }
    }

    if (_sender.consecutive_retransmissions() >= TCPConfig::MAX_RETX_ATTEMPTS) {
        TCPSegment seg;
        seg.header().rst = true;
        segments_out().push(seg);
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        return;
    } else {
        _sender.tick(ms_since_last_tick);
        send();
    }
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
        //cerr << "deconstruct tcpconnection" << endl;
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            // Your code here: need to send a RST segment to the peer

            TCPSegment seg;
            seg.header().rst = true;
            segments_out().push(seg);
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::send() {
    if (_sender.segments_out().empty()) {
        if (_recent_need_to_ack_instantly && _receiver.ackno().has_value()) {
            TCPSegment seg;
            seg.header().ack = true;
            seg.header().seqno = _sender.next_seqno();
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
            segments_out().push(seg);
            _recent_need_to_ack_instantly = false;
        }
        return;
    }

    while (!_sender.segments_out().empty()) {
        if (_receiver.ackno().has_value()) {
            _sender.segments_out().front().header().ack = true;
            _sender.segments_out().front().header().ackno = _receiver.ackno().value();
            _sender.segments_out().front().header().win = _receiver.window_size();
            _recent_need_to_ack_instantly = false;
        }

        _segments_out.push(_sender.segments_out().front());
        _sender.segments_out().pop();
    }
}

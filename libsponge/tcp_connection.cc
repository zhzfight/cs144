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
    cerr<<"get seg"<<seg.header().to_string()<<endl<<
        " sender start"<<TCPState::state_summary(_sender)<<" sender buffer "
         <<_sender.stream_in().input_ended()<<"  "<<_sender.stream_in().buffer_size()
         <<" receiver "<<TCPState::state_summary(_receiver)<<endl;


    /*
     * in state syn_sent,
     * if get a segment with syn, go through down,
     * if get a segment with rst, reset the _sender,_receiver,active,_linger and return
     * otherwise...
     */
    if (TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_SENT &&
        TCPState::state_summary(_receiver) == TCPReceiverStateSummary::LISTEN) {
        if (seg.header().syn) {
            _recent_need_to_ack_instantly = true;

        } else if (seg.header().rst) {
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
            _linger_after_streams_finish = false;
            _stream_closed = true;
            _receiver.stream_out().end_input();
            _sender.stream_in().end_input();
            return;
        } else {
            return;
        }
    }

    /*
     * in state listen, get a segment with syn, go through down, otherwise return.
     */
    if (TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED &&
        TCPState::state_summary(_receiver) == TCPReceiverStateSummary::LISTEN) {
        if (seg.header().syn) {
            _recent_need_to_ack_instantly = true;

        } else {
            return;
        }
    }

    /*
     * in state established, if get a fin, it is a passive close, should set the _linger to false, and return an ack
     */
    if (TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED &&
        TCPState::state_summary(_receiver) == TCPReceiverStateSummary::SYN_RECV) {
        if (seg.header().fin) {
            _linger_after_streams_finish = false;
            _recent_need_to_ack_instantly = true;

        }
    }

    /*
     * in state fin_wait2, if get a fin, should return an ack.
     */
    if (TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED &&
        TCPState::state_summary(_receiver) == TCPReceiverStateSummary::SYN_RECV) {
        if (seg.header().fin) {
            _stream_closed = true;

            _recent_need_to_ack_instantly = true;
        }
    }
    _ticks_since_last_segment_received = 0;
    bool in_window = _receiver.segment_received(seg);
    if (!in_window) {
        _recent_need_to_ack_instantly = true;
    } else {
        if (seg.header().rst) {
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
            _linger_after_streams_finish = false;
            _stream_closed = true;
            _receiver.stream_out().end_input();
            _sender.stream_in().end_input();
            return;
        }
        if (seg.length_in_sequence_space()!=0) {
            _recent_need_to_ack_instantly = true;
        }
    }
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
    }

    /*
     * fin_wait_2 finished, step into time wait
     */
    if (TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED &&
        TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV) {
        _stream_closed = true;
    }


    _sender.fill_window();
    send();
}

bool TCPConnection::active() const {
    if (_receiver.stream_out().error()||_sender.stream_in().error()){
        return false;
    }
    return !(TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED &&
               TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV&&!_linger_after_streams_finish);

}

size_t TCPConnection::write(const string &data) {
    if (_sender.stream_in().input_ended()){
        return 0;
    }
    size_t actual_write = _sender.stream_in().write(data);
    _sender.fill_window();
    send();
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
        seg.header().seqno = _sender.next_seqno();
        segments_out().push(seg);
        send();

        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();

        _stream_closed = true;
        _linger_after_streams_finish = false;
        return;
    }
    _sender.tick(ms_since_last_tick);
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

            _stream_closed = true;
            _linger_after_streams_finish = false;
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
int total_send=0;
void TCPConnection::send() {
    if (_sender.segments_out().empty()) {
        if (_recent_need_to_ack_instantly && _receiver.ackno().has_value()) {

            TCPSegment seg;
            seg.header().ack = true;
            seg.header().seqno = _sender.next_seqno();
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
            segments_out().push(seg);

            total_send+=seg.length_in_sequence_space();

            _recent_need_to_ack_instantly = false;
            //_ticks_since_last_ack = 0;
        }
        return;
    }

    while (!_sender.segments_out().empty()) {
        if (_receiver.ackno().has_value()) {
            _sender.segments_out().front().header().ack = true;
            _sender.segments_out().front().header().ackno = _receiver.ackno().value();

            _sender.segments_out().front().header().win = _receiver.window_size();
            _recent_need_to_ack_instantly = false;
            //_ticks_since_last_ack=0;

        }
        cerr<<"send seg with data "<<_sender.segments_out().front().length_in_sequence_space()<<" now total send "<<total_send<<endl;
        total_send+=_sender.segments_out().front().length_in_sequence_space();
        _segments_out.push(_sender.segments_out().front());

        _sender.segments_out().pop();
    }
}


#include "tcp_receiver.hh"
#include <iostream>
// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    //cerr<<"segment received"<<endl;
    if (!_connection_set && seg.header().syn) {
        //cerr<<"connect set and initial isn"<<endl;
        _connection_set = true;
        _ISN = seg.header().seqno;
    }
    //cerr<<"absolute seq "<<unwrap(seg.header().seqno , _ISN, _checkpoint)<<endl;
    _reassembler.push_substring(seg.payload().copy(),
                                unwrap(seg.header().seqno , _ISN, _checkpoint)+(seg.header().syn ? 1 : 0)-1,
                                seg.header().fin);

    _checkpoint=_reassembler.get_cur_index();



}

optional<WrappingInt32> TCPReceiver::ackno() const {
    optional<WrappingInt32> ackno;
    if (!_connection_set) {
        return ackno;
    }
    ackno = wrap(_checkpoint, _ISN) + 1+stream_out().input_ended();
    return ackno;
}

size_t TCPReceiver::window_size() const { return stream_out().remaining_capacity(); }

#include "tcp_receiver.hh"
#include <iostream>
// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

bool TCPReceiver::segment_received(const TCPSegment &seg) {
    if (!_connection_set && seg.header().syn) {
        _connection_set = true;
        _ISN = seg.header().seqno;
    }

    bool in_window=_reassembler.push_substring(seg.payload().copy(),
                                unwrap(seg.header().seqno , _ISN, _checkpoint)-1+ (seg.header().syn ? 1 : 0),
                                seg.header().fin);

    _checkpoint=_reassembler.get_cur_index();
    return in_window;

    //std::cout<<"checkpoint to "<<_checkpoint<<" index: "<<unwrap(seg.header().seqno + (seg.header().syn ? 1 : 0), _ISN, _checkpoint)-1
     //         <<" seqno: "<<seg.header().seqno<<endl;
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

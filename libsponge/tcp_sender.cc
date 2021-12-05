#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <iostream>
#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _RTO{retx_timeout}
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    //std::cout << "fill window size: " << _window_size << endl;
    if (_window_size == 0) {
        if (_bytes_in_flight == 0) {
            _window_size = 1;
            _win_zero_flag = true;
        } else {
            return;
        }
    } else {
        _win_zero_flag = false;
    }

    TCPSegment seg;
    seg.header().seqno = next_seqno();
    if (!_set_connect) {
        _set_connect = true;
        seg.header().syn = true;
        --_window_size;
        _flight.push(seg);
        _next_seqno += seg.length_in_sequence_space();
        _bytes_in_flight += seg.length_in_sequence_space();
        send(seg);
        _timer_start = true;
        _ticks = 0;
        // std::cout << "timer start ticks: " << _ticks << endl;
        return;
    }
    if (_closed) {
        return;
    }

    size_t would_read = _window_size < TCPConfig::MAX_PAYLOAD_SIZE ? _window_size : TCPConfig::MAX_PAYLOAD_SIZE;
    string data = stream_in().read(would_read);
    // std::cout<<"data empty:"<<data.empty()<<" inputended: "<<stream_in().input_ended()<<endl;
    if (data.empty() && !stream_in().input_ended()) {
        //std::cout<<"return because data empty"<<endl;
        return;
    }
    _window_size -= data.size();
    Buffer buffer(std::move(data));
    seg.payload() = std::move(buffer);
    if (_window_size && stream_in().input_ended()) {
        _window_size--;
        seg.header().fin = true;
        _closed = true;
    }
    // std::cout << "windowsize:" << _window_size << endl;
    _flight.push(seg);
    _next_seqno += seg.length_in_sequence_space();
    _bytes_in_flight += seg.length_in_sequence_space();

    send(seg);
    if (!_timer_start) {
        _timer_start = true;
        _ticks = 0;
        // std::cout << "timer start ticks: " << _ticks << endl;
    }
    fill_window();
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t absolute_ackno = unwrap(ackno, _isn, _checkpoint);
    // std::cout << "recack:absoack " << absolute_ackno << " checkpoint " << _checkpoint<<endl;
    if (unwrap(ackno, _isn, _checkpoint) > _checkpoint + _bytes_in_flight) {
        return;
    }
    if (absolute_ackno > _checkpoint) {
        _RTO = _initial_retransmission_timeout;
        _consecutive_retransmissions = 0;
        while (!_flight.empty()) {

            uint64_t first_seg_seqno =
                unwrap(_flight.front().header().seqno, _isn, _checkpoint) + _flight.front().length_in_sequence_space();
            if (first_seg_seqno <= absolute_ackno) {
                _bytes_in_flight -= first_seg_seqno - _checkpoint;
                _checkpoint = first_seg_seqno;
                _flight.pop();
            } else {
                // std::cout<<"bytes in flight "<<_bytes_in_flight<<" - "<<absolute_ackno - _checkpoint<<" = ";
                _bytes_in_flight -= absolute_ackno - _checkpoint;
                _checkpoint = absolute_ackno;
                break;
            }
        }
        _ticks = 0;
        //std::cout << "bigger ack timer reset to 0" << endl;
        if (_flight.empty()) {
            // std::cout << "no outstanding,timer stop" << endl;
            _timer_start = false;
        }
        // std::cout << "afterack:absoack " << absolute_ackno << " checkpoint " << _checkpoint<<endl;
    }
    //std::cout<<"now flight "<<_flight.size()<<endl;
    _window_size = window_size - _bytes_in_flight;
    // std::cout << "after ack now windowsize: " << _window_size << " bytes in flight:"<<_bytes_in_flight<<endl;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (!_timer_start) {
        return;
    }
    _ticks += ms_since_last_tick;
    // std::cout << "now ticks: " << _ticks << " _rtos: " << _RTO << endl;
    if (_ticks >= _RTO) {
        send(_flight.front());
        _ticks = 0;


        _consecutive_retransmissions++;

        if (!_win_zero_flag) {
            _RTO *= 2;
        }
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    TCPSegment seg;


    send(seg);
}

#include "stream_reassembler.hh"
#include <iostream>
// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity)
    , _capacity(capacity)
    , _unassembled(capacity, '\0')
    , _bitmap(capacity, false)
    , _unassembled_bytes(0)
    , _cur(0)
    , _eof(false) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
bool StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    std::cout<<"index "<<index<<" data "<<data.size()<<" _cur "<<_cur<<" cap "<<stream_out().remaining_capacity()<<endl;
    if (index >= _cur + stream_out().remaining_capacity()) {
        if (index > _cur + stream_out().remaining_capacity()){
            return false;
        }else{
            return true;
        }

    }

    if (index + data.size() <= _cur) {
        if (eof) {
            _eof = eof;
        }
        if (_eof && empty()) {
            stream_out().end_input();
        }
        if (index+data.size()<_cur){
            return false;
        }else{
            return true;
        }
    }
    size_t begin = _cur > index ? _cur : index;
    size_t end = index + data.size() < _cur + stream_out().remaining_capacity()
                     ? index + data.size()
                     : _cur + stream_out().remaining_capacity();

    for (; begin < end; begin++) {
        if (!_bitmap[begin - _cur]) {
            _unassembled_bytes++;
            _unassembled[begin - _cur] = data[begin - index];
            _bitmap[begin - _cur] = true;
        }
    }
    if (end - index == data.size() && eof) {
        _eof = true;
    }
    string write_in = "";
    while (_bitmap.front()) {
        write_in += _unassembled.front();
        _unassembled.pop_front();
        _unassembled.push_back('\0');
        _bitmap.pop_front();
        _bitmap.push_back(false);
    }
    size_t len = write_in.size();

    if (len > 0) {
        _cur += len;
        _unassembled_bytes -= len;
        stream_out().write(write_in);
    }

    if (_eof && empty()) {
        stream_out().end_input();
    }
    return true;
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_bytes; }

bool StreamReassembler::empty() const { return unassembled_bytes() == 0; }

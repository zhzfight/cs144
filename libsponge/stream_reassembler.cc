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
    , _unassembled_bytes(0)
    , _cur(0)
    , _eof(false) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    //cerr<<"push string index "<<index<<" "<<data.size()<<" eof "<<eof<<endl;
    if (index >= _cur + stream_out().remaining_capacity()) {
        //cerr<<"out of window _cur + stream_out.remaining_capacity "<<_cur + stream_out().remaining_capacity()<<endl;
        return;
    }
    if (index + data.size() <= _cur) {
        //cerr<<"out of window _cur "<<_cur<<" unassembled byte "<<_unassembled_bytes<<endl;
        if (eof) {
            _eof = eof;
        }
        if (_eof && empty()) {
            //cerr<<"receiver end!!!"<<endl;
            stream_out().end_input();
        }
        return;
    }
    /*
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
*/
    size_t pos = _cur > index ? _cur - index : 0;
    size_t nums = index + data.size() > _cur + stream_out().remaining_capacity()
                      ? _cur + stream_out().remaining_capacity() - pos - index
                      : data.size() - pos;

    if (pos + nums == data.size() && eof) {
        _eof = true;
    }
    string str = data.substr(pos, nums);

    mergerIntoBuffer(str, index + pos);
    string write_in = "";
    //cerr<<" actual write pos nums "<<pos<<" "<<nums<<endl;
    while (!_buffer.empty() && _buffer.front().first == _cur) {
        write_in += _buffer.front().second;
        _cur += _buffer.front().second.size();
        _buffer.pop_front();
    }
    size_t len = write_in.size();
    //cerr<<"final assemble "<<write_in.size()<<endl;
    if (len > 0) {
        _unassembled_bytes -= len;
        stream_out().write(write_in);
    }

    if (_eof && empty()) {
        stream_out().end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_bytes; }

bool StreamReassembler::empty() const { return unassembled_bytes() == 0; }

void StreamReassembler::mergerIntoBuffer(std::string &data, const size_t index) {
    int len = _buffer.size();
    if (len == 0) {
        _buffer.emplace_back(index, data);
        _unassembled_bytes += data.size();

        return;
    }
    size_t left = index;
    size_t right = index + data.size() - 1;

    int i;
    for (i = len; i-- > 0;) {
        if (_buffer[i].first + _buffer[i].second.size() - 1 < left) {
            _buffer.insert(_buffer.begin() + i + 1, {left, data});
            _unassembled_bytes += right - left + 1;
            i = -2;
        } else if (_buffer[i].first + _buffer[i].second.size() >= left && _buffer[i].first <= right) {
            if (_buffer[i].first < left) {
                data = _buffer[i].second.substr(0, left - _buffer[i].first) + data;

                left = _buffer[i].first;
            }
            if (_buffer[i].first + _buffer[i].second.size() > right) {
                data = data + _buffer[i].second.substr(right - _buffer[i].first + 1);
                right = _buffer[i].first + _buffer[i].second.size();
            }

            _buffer.erase(_buffer.begin() + i);
            _unassembled_bytes -= _buffer[i].second.size();
            // merge data
        }
    }
    if (i == -1) {
        _buffer.insert(_buffer.begin(), {left, data});
        _unassembled_bytes += data.size();
    }
}

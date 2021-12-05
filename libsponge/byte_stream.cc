#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) : capacity_(capacity) {}

size_t ByteStream::write(const string &data) {
    size_t actual_write = remaining_capacity() < data.size() ? remaining_capacity() : data.size();
    buffer_.append(data.begin(),data.begin()+actual_write);
    /*
    for (size_t i = 0; i < actual_write; i++) {
        buffer_.push_back(data[i]);
    }
     */
    total_written += actual_write;
    return actual_write;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t actual_read = buffer_size() < len ? buffer_size() : len;
    return string().assign(buffer_.begin(), buffer_.begin() + actual_read);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t actual_read = buffer_size() < len ? buffer_size() : len;
    buffer_.erase(buffer_.begin(), buffer_.begin() + actual_read);
    total_read += actual_read;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string str = peek_output(len);
    pop_output(len);
    return str;
}

void ByteStream::end_input() { input_end_ = true; }

bool ByteStream::input_ended() const { return input_end_; }

size_t ByteStream::buffer_size() const { return buffer_.size(); }

bool ByteStream::buffer_empty() const { return buffer_.empty(); }

bool ByteStream::eof() const { return input_ended() && buffer_empty(); }

size_t ByteStream::bytes_written() const { return total_written; }

size_t ByteStream::bytes_read() const { return total_read; }

size_t ByteStream::remaining_capacity() const {
    return capacity_ - buffer_size(); }

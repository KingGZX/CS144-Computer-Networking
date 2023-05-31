#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { 
    return _flight_bytes;
}

void TCPSender::fill_window() {
    /*
    -------------  First time: my own logic --------------
    没发送SYN就发送SYN, 否则依照 可读字节数，最大字节数和window_size决定发送的数据
    --- Version1 错误， 此函数实际上不是简单的 if else逻辑
    而应该是只要可以发送则一直发送

    且无需自行设置bool变量来判断是否发送SYN
    */
    // Sender需要管理的报文头主要内容: SYN FIN 标志符, Sequence Number(concretely, Absolute Sequence Number)，payload数据荷载
    // 先发送 SYN 根据 lab pdf 的 Q&A，在我们第一次收到对端的 ACK or win_size 前 我们仅仅假设对端的 win_size = 1， 我们单独发送一个 SYN
    while(!_set_fin){       // 只要没关闭connection，也即管道数据仍未读完(因为只有读完我们才设置FIN) 就一直读就完事了
        TCPSegment seg;
        TCPHeader head;
        head.seqno = next_seqno();
        if(_next_seqno == 0){
            head.syn = true;
            seg.header() = head;
        }
        size_t seqlen = seg.length_in_sequence_space();
         // 即使对端无空间，那么也发送一个Byte出去，这样才能保持通信；否则即使对端有空出来的位置了可能也就失联了
        size_t _m_win_sz = _win_size == 0 ? 1 : _win_size;      // means ----"modified window size"---- // 
        if(seqlen < _m_win_sz){                                 // window 还能支撑剩余空间的情况下 再附加payload
            size_t write_sz = min(_m_win_sz, TCPConfig::MAX_PAYLOAD_SIZE);                          // 不能超过最大限制
            write_sz = min(write_sz, _stream.bytes_written() - _stream.bytes_read());               // 不能超过管道可读数量
            Buffer payload(_stream.read(write_sz));
            seqlen += write_sz;
            seg.payload() = payload;
        }
        // 如果管道读完了那么需要设置FIN
        if(_stream.input_ended() && _stream.bytes_read() == _stream.bytes_written()){
            if(seqlen < _m_win_sz) {     // 得放得下才行呢 (即在对方窗口限制内)
                seg.header().fin = true;
                _next_seqno += 1;
                seqlen += 1;
                _set_fin = true;
            }
        }
        _next_seqno += seqlen;      // 虽然我们不知道发出去这个segment会不会被确认，但反正没收到+重传计时器到点了自然会重传
        // for transmitting and retransmitting
        // 但是守则里指出，如果仅仅是一个空数据包那么并不能称之为outstanding，无需加入重传队列(我这儿也主动设置为不发送)
        if(seqlen > 0){
            _flight_bytes += seqlen;
            _segments_out.push(seg);
            _wait_ack.push(seg);
        }
        else return;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) { 
    // DUMMY_CODE(ackno, window_size); 
    _ack = ackno;
    _win_size = window_size;
    // pop the segemnt waited to be acknowledged

}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    DUMMY_CODE(ms_since_last_tick); 
}

unsigned int TCPSender::consecutive_retransmissions() const { 
    return {}; 
}

void TCPSender::send_empty_segment() {
    
}

#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

#include <iostream>

// ==== God dammn it, I finish the shit ==== 


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
    , _stream(capacity)
    , _timer(retx_timeout) {}

uint64_t TCPSender::bytes_in_flight() const { 
    return _flight_bytes;
}

void TCPSender::fill_window() {
    if(_set_fin) return;
    // 因为可能有重复的旧的 ack 我们需要进行判别
    if(_ackno + (_window_sz != 0 ? _window_sz : 1) <= _next_seqno) return;
    size_t _win_size = _ackno + (_window_sz != 0 ? _window_sz : 1) - next_seqno_absolute();
    /*
    -------------  First time: my own logic --------------
    没发送SYN就发送SYN, 否则依照 可读字节数，最大字节数和window_size决定发送的数据
    --- Version1 错误， 此函数实际上不是简单的 if else逻辑
    而应该是只要可以发送则一直发送

    且无需自行设置bool变量来判断是否发送SYN
    */
    // Sender需要管理的报文头主要内容: SYN FIN 标志符, Sequence Number(concretely, Absolute Sequence Number)，payload数据荷载
    // 先发送 SYN 根据 lab pdf 的 Q&A，在我们第一次收到对端的 ACK or win_size 前 我们仅仅假设对端的 win_size = 1， 我们单独发送一个 SYN
    while(_win_size > 0 && !_set_fin){       // 只要没关闭connection，也即管道数据仍未读完(因为只有读完我们才设置FIN) 就一直读就完事了
        TCPSegment seg;
        TCPHeader& head = seg.header();
        head.seqno = next_seqno();
        size_t seqlen = 0;  // 记录本报文的长度，防止超过对端的window限制
        if(_next_seqno == 0){
            head.syn = true;
            seqlen += 1;
        }
        Buffer& buffer = seg.payload();
        buffer = stream_in().read(min(_win_size - seqlen, TCPConfig::MAX_PAYLOAD_SIZE));
        seqlen += buffer.size();
        // 如果管道读完了那么需要设置FIN
        if(_stream.eof() && seqlen < _win_size){
            // 得放得下才行呢 (即在对方窗口限制内) 因为FIN也占用一个Byte
            head.fin = true;
            seqlen += 1;
            _set_fin = true;
        }
        _next_seqno += seqlen;      // 虽然我们不知道发出去这个segment会不会被确认，但反正没收到+重传计时器到点了自然会重传
        // for transmitting and retransmitting
        // 但是守则里指出，如果仅仅是一个空数据包那么并不能称之为outstanding，无需加入重传队列(我这儿也主动设置为不发送)
        if(seqlen > 0){
            _flight_bytes += seqlen;
            _segments_out.push(seg);
            // start timer
            if(!_timer.started())
                _timer.start();
            _wait_ack.push(seg);
            // modify window size
            _win_size -= seqlen;
            if(!_win_size) return;
        }
        else return;        
        // 这个就说明管道里没有数据可读了，此时也是直接退出即可。否则就会造成一种超时现象：
        // 还有剩余的 window_size 空间可填， 但管道已经没数据了，while 成死循环等待管道数据了
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) { 
    // DUMMY_CODE(ackno, window_size); 
    // pop the segemnt waited to be acknowledged
    // ===== 不能直接用 WrappingInt32 比较大小, 最好是转换回 absolute sequence number 比较大小 ====== //
    bool _newdata_acked = false;
    // Byte in flight 是已发送且未被确认的数量
    // 那么用当前的 next_abs_seqno - flight_bytes 就是已发送且已被确认的数量     数量对应index要减去1
    // 已被确认的数量就是全部已经进入对端 ByteStream 的数量, 其实可以根据此直接推断出 checkpoint
    // 得到 checkpoint 后可以转换 当前 ackno 和 待确认的所有sqeno 为 absolute sequence number， 然后直接比较大小即可
    // uint64_t checkpoint = next_seqno_absolute() - bytes_in_flight() - 1;
    uint64_t checkpoint = _next_seqno;
    uint64_t abs_ack_no = unwrap(ackno, _isn, checkpoint);
    _ackno = abs_ack_no;

    // ===== 有一个不可能的 ack 需要忽略的 test =======
    if(abs_ack_no > next_seqno_absolute()) {
        // for debugging
        // std::cerr << "impossible ack number" << std::endl;
        return;
    }
    // 合法 ack 才更新 window
    _window_sz = window_size;           // 这个是真实收到的size, 也包括0，0的话就不用计时器指数退避
    // _win_size = window_size == 0 ? 1 : window_size;   // 这个是因为要保持通信, 假如对端发送 ack + win = 0, 那么理论上我们直接不用回都可以了，但为了保持通信，我们当作1处理

    while(!_wait_ack.empty()) {
        TCPSegment temp = _wait_ack.front();
        uint64_t abs_seg_no = unwrap(temp.header().seqno, _isn, checkpoint);
        // abs_ack_no 是在 absolute sequence number视图中我们需要的下一个Byte
        // abs_seg_no 是在 absolute sequence number视图中某个segemnt的头字节对应的 index
        // 如果 abs_ack_no >= abs_seg_no + len(seg) 即可认为这个段被完全确认了
        // 因为 abs_seg_no + len 是下一个段的头字节的 index
        // 那么 abs_ack_no - 1 就是所有已确认的index, abs_seg_no + len - 1 就是本段的最大index
        // 只要前者大于等于后者,那么本段即被完全确认
        if(abs_ack_no >= abs_seg_no + temp.length_in_sequence_space()) {
            _flight_bytes -=  temp.length_in_sequence_space();
            // checkpoint += temp.length_in_sequence_space();
            _wait_ack.pop();
            _newdata_acked = true;
        }
        else {
            // 其实这就是个 部分确认的 ack，真实的TCP 可以根据部分确认来切割数据块
            // 但lab tutorial pdf 的 Q&A 回答了我们无需做到那个程度
            // 我们只需要丢弃这样部分确认的ack就好了。。。。

            // 但由于我们 ack 后 总是会再次去 fill window，那么我们应该把 _win_size 设置为0 就不会发出去一些数据

            // 同时 在 test 14   send_window 中有一个test是
            // 发送 SYN 收到 ack 并通知 window_size = 7
            // 写入 "1234567" 再次写入 eof
            // 发送   "1234567"
            // 收到了 ack 但 ack 值 仍旧是 isn + 1 (即对 SYN 的确认)  但 window = 8！！！
            // 此时 我们发出去 一个单独的 FIN 数据包 
            /*
            // 所以我想大概是因为? 如果通知窗口大小如果 比重传队列的 第一个 segment 还要小的话就不发送任何东西 ? 
            if(_win_size <= temp.length_in_sequence_space())
                _win_size = 0;
            */

            // 上面这些废话放在 fill_window 开头 特判了， 不再胡乱修改window size.....
            break;
        }
    }
    // reset some timer properties and if we still have outstanding segments, resend it
    if(_newdata_acked){
        _timer.reset();
        _consecutive_transmit_num = 0;
        if(!_wait_ack.empty()){
            // ===== 之前一直以为 收到新(ack)(数据被确认) 后, 若有仍未被确认的则应该发送出去，再启动定时器
            // ===== 其实不然，我们不用先发出去，我们应该先启动定时器等着，等一段时间没收到 ack 等expire了再发送出去。。。。 
            // _segments_out.push(_wait_ack.front());
            _timer.start();
        }
        else{
            _timer.stop();
        }
    }
    // introduces in tutorial pdf, if advertised window size is greater than 0, try filling window again

    // ====== 大无语事件，明明写着如果又有空间通知过来的话就应该 fill window 一下，想不到，不用自己写出来，系统主动帮你fill了。。。。
    // ====== 一直 test 这个点不过 都懵逼了 老是多个 1 =========

    // ====== 我又来了。。。。。 Lab4 改回来的, 收到ack后要是有数据还是得fill进来的。系统并没有帮你调用
    // 但反正我也不知道之前 Lab3 造了什么孽这句注释掉就对了，但因为先改过了 Receiver 可能就是什么毛病吧.....
    // 反正打开后 Lab3是对的 Lab4的某个点也对上了
    // fill_window();      // 无所谓 window_size 是否为 0 了 因为会变为 1
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    // DUMMY_CODE(ms_since_last_tick); 
    _timer.tictoc(_window_sz, ms_since_last_tick, _consecutive_transmit_num);
    // if our timer expire, retransmit!
    if(_timer.isexpired() && !_wait_ack.empty()){
        _segments_out.push(_wait_ack.front());
        _timer.start();
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { 
    return _consecutive_transmit_num;
}

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    // 每个报文都要有自己的序号，第一版忘记加序号了
    seg.header().seqno = next_seqno();
    // seg.header().ackno = _ack;    需要的是在 TCPConnection 中由 receiver 计算出来的 ackno
    // seg.payload() = Buffer();
    _segments_out.push(seg);
}

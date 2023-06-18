#include "tcp_connection.hh"

#include <iostream>

// Lab4 一个重要点就是
//  ======   要把 Sender 的发送队列 全部弹出到 Connection 的发送队列上 ======
//  这样才能实现真实发送.


// 每次发送完毕后都试试看可以关闭了没 否则会无效 linger? 导致超时

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { 
    // OutBound 就是出去的口 也就是 Sender 所以要看Sender还能装多少
    return _sender.stream_in().remaining_capacity(); 
}

size_t TCPConnection::bytes_in_flight() const { 
    // 照抄上一个 Lab 的即可
    return _sender.bytes_in_flight(); 
}

size_t TCPConnection::unassembled_bytes() const { 
    // 这个是 receive 的属性 也是直接抄即可
    return _receiver.unassembled_bytes(); 
}

size_t TCPConnection::time_since_last_segment_received() const { 
    return _wait_segment_time; 
}

void TCPConnection::segment_received(const TCPSegment &seg) { 
    // if(!_active) return;
    // 按照 pdf 的指示来  ======= 最大的前提就是得确保在连接状态下 =======
    _wait_segment_time = 0;

    // 1. 若 RST 即 reset, 那么 出入口都设置成错误状态并且关闭连接
    if (seg.header().rst) {
        _active = false;
        _receiver.stream_out().set_error();
        _sender.stream_in().set_error();
        return;
        // 他说 要 kill it permanently , 我不知道这样对不对
        // this->~TCPConnection();
    }
    else {
        // 毕竟是收到一条 数据报文 肯定要交给recevier处理咯
        _receiver.segment_received(seg);

        // 只有 收到 SYN 之后一切的后续步骤才合理
        optional<WrappingInt32> ackno = _receiver.ackno();
        if(ackno.has_value()){          // has_value 如果返回一个空值的话, 说明 本方 _set_syn 失败了
            // 如若 ack 那么 这是一个 确认 会附带通知 ackno 和 window_size 要通知 Sender进行更新
            if(seg.header().ack){
                // 更新 对端 的 ack 状况 和 window size， 这样我们在 fill window 时候才是执行的最新状况
                _sender.ack_received(seg.header().ackno, seg.header().win);
            }
            // ===之前把这个放在 报文长度判断里面 那么如果对方仅仅是回复ack的话就不再发送数据了...... 那就错啦！！
            _sender.fill_window();
            // 如果此报文不是空报文，那么我们得确保有一个回复，保证连接不会死亡
            if(seg.length_in_sequence_space()){
                // 或许 管道并没有可发送的的数据了 那么我们需要单独发送一个 ack 的报文
                if(_sender.segments_out().empty()) {
                    _sender.send_empty_segment();
                }
            }
            // 把待发送的数据全发出去
            send_segments();
            //if(seg.header().fin){           // 私以为， 只有 带上了 FIN 才有可能 inbound 会结束
            // 如果 inbound 已经结束，也就是说 "对端" 没有数据了 那么，如果本端还未到达eof,则无需linger
            if((!_sender.stream_in().eof()) && _receiver.stream_out().input_ended()){
                // std::cout << "set linger to false" << std::endl;
                _linger_after_streams_finish = false;
            }
            //}
        }
        // 加一个关闭的代码, 否则的话 只有在 Tick 之后才会实现关闭 -----可能就会超时？
        try_clean_close();
    }
}

bool TCPConnection::active() const { 
    return _active; 
}

size_t TCPConnection::write(const string &data) {
    // 写入管道 并返回 最大写入量
    size_t writesize = _sender.stream_in().write(data);
    // 然后让sender去发送呗
    _sender.fill_window();
    send_segments();
    try_clean_close();
    return writesize;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) { 
    /*
    // 额 不能在这样设置   ---- linger 只能在 receive 那里进行判断
    // 即 对端发完 本地还没有发完 无需linger
    // 如果已经linger了 那么... 就别 linger 了
    if(_linger_after_streams_finish && _wait_segment_time >= _cfg.rt_timeout * 10) {
        _linger_after_streams_finish = false;
    }
    */
    
    // 当然是要通知 Sender 重传计时器
    _sender.tick(ms_since_last_tick);

    // 超过最大重传次数就abort连接
    if(_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS){
        // 我觉得这个因为 不占用任何 Byte 所以其实不是一个 outstanding segemnt， 那么不需要加入重传队列，那么直接放到Connection的队列中
        send_rst_seg();
        _active = false;
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        // this->~TCPConnection();  // 发不出去咋办。。。。。
        return;
    }

    _wait_segment_time += ms_since_last_tick;

    // 这里需要注意: 因为 tick 后可能触发 _sender 的重传机制了，那么此时实际上需要把待发送的报文段全部放进来队列里面的
    send_segments();

    // 主动关闭: 满足prereq #1: 本地_receiver已经end且不存在未assemble的bytes了
    // 满足 prereq #3: 本地_sender已经发送完毕且对端已经完全确认发送的所有数据了
    // 已经linger了一定时间了, 为什么要linger呢? 因为本地给对端发送完最后一个ack后, 我们不知道这个ack对端收到没(因为TCP不会对单纯的ack报文做回复), 所以等待一定时间没收到回复就默认对端收到我们最后的ack了 

    // 被动关闭: 满足prereq #1      满足 prereq #3
    //  linger 为false.     因为我们只有在, 本地的_receiver接收完毕(对端已经发送了FIN主动关闭了) 先于 _sender发送完毕 时才会置 linger 为 false 
    // 所以其实就是说对端已经优先执行主动关闭了, 那么我们本地把自己的数据发送完就可以自行进行 "被动关闭了"
    try_clean_close();


    /*
    // 抄别人的, 目前还不知道有没有效果； 就是相当于又去读一次数据了

    // 没用 好像还会使我多几个超时案例 妈的
    if(_receiver.ackno().has_value()) _sender.fill_window();
    send_segments();
    try_clean_close();
    */
}

void TCPConnection::end_input_stream() {
    // receiver 仍旧工作, 但我们发送到对端的数据已经结束啦
    _sender.stream_in().end_input();
    // 把 FIN 加到要发送出去的最后一个报文段中
    _sender.fill_window();              // 自然会把 FIN 发送出去
    send_segments();

    // 真的需要么？？？ 我刚发出去这个怎么可能 bytes in flight为0呢?
    // try_clean_close();
}

void TCPConnection::connect() {
    // 好像直接fill window 就行了, 反正未建立通信前 fill window 总归只是发送SYN
    /*
    // 单纯发送一个 SYN 数据包
    TCPSegment seg;
    seg.header().seqno = _sender.next_seqno();
    seg.header().syn = true;
    // _sender.segments_out().push(seg);
    _segments_out.push(seg);

    // 应该还需要放到等待确认的队列
    */

    _sender.fill_window();
    send_segments();
}

void TCPConnection::send_segments(){
    while(!_sender.segments_out().empty()){
        // 这里写的太简单了 因为没把recevier计算出来的东西带进去
        // _segments_out.push(_sender.segments_out().front());
        TCPSegment seg = _sender.segments_out().front();
        optional<WrappingInt32> ackno = _receiver.ackno();
        if(ackno.has_value()){
            seg.header().ack = true;
            seg.header().ackno = ackno.value();
            seg.header().win = static_cast<uint16_t>(_receiver.window_size());
        }
        //static_cast<uint16_t>(_receiver.window_size());
        _segments_out.push(seg);
        _sender.segments_out().pop();
    }
}

void TCPConnection::try_clean_close(){
    // ====擦.这个 linger_time 之前一直用了 10 * _cfg.TIMEOUT_DFLT 判断 导致出问题很多
    if(_sender.stream_in().eof() && _receiver.stream_out().input_ended() && bytes_in_flight() == 0 && 
        _sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2 && 
        (_wait_segment_time >= 10 * _cfg.rt_timeout || !_linger_after_streams_finish)){
        _active = false;
    }
}

void TCPConnection::send_rst_seg(){
    TCPSegment seg;
    seg.header().rst = true;
    seg.header().seqno = _sender.next_seqno();
    optional<WrappingInt32> ackno = _receiver.ackno();
    if(ackno.has_value()){
        seg.header().ack = true;
        seg.header().ackno = ackno.value();
        seg.header().win = static_cast<uint16_t>(_receiver.window_size());
    }
    _segments_out.push(seg);
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            send_rst_seg();
            _receiver.stream_out().set_error();
            _sender.stream_in().set_error();
            _active = false;

            send_segments();
            // this->~TCPConnection();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

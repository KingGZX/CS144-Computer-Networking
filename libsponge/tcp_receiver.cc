#include "tcp_receiver.hh"

#include <iostream>

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

/*
-----------------IMPORTANT------------------

我们需要的 checkpoint 是定义在 absolute sequence number 空间的 index，包含了 SYN FIN，从0开始且64bit长

而我们的 stream 的 bytes_written() 实际上是 stream index 空间的 index。即实际不把 SYN FIN 写入管道中读取，所以其
把真实 payload 的第一个 Byte 作为 0 号 index。 整体来说：
在 FIN 到来之前， index 与 abs seqno 小 1； FIN 之后的数据，小 2.

所以，虽然理论上来讲 我们得到 bytes_written() 后这个值 减去 1 才是最大的已经 reassembled 的 Byte；
但由于我们忽略了那个 SYN  和 FIN 这个值需要补上 1 或 2 才是 checkpoint （！！因为 checkpoint 是在 abs seqno空间定义的！！）
*/

void TCPReceiver::segment_received(const TCPSegment &seg) {
    // DUMMY_CODE(seg);
    bool eof = false;
    const TCPHeader& header = seg.header();
    WrappingInt32 _headerseq = header.seqno; 
    // 一般来说 seqno 代表的是payload 荷载数据的第一个Byte的index
    // 但当头包含 SYN 时，实际上第一个index代表的是 SYN而不是数据！ （SYN 和 FIN 均占用一个index）
    if(header.syn){
        _isn = header.seqno;
        _headerseq = _headerseq + static_cast<uint32_t> (1);
        _set_isn = true;
    }
    if(header.fin){
        eof = true;
    }
    if(_set_isn){   // 只有收到 SYN 后的系列数据包才是有效的
        // checkpoint 是已经assembled的最大值，可以观察 lab pdf 里的那张图 我们把进入ByteStream的最后一个字节叫做最大的已经reassembled的byte 
        // 直观来讲 bytes_written() - 1 才是最后一个 index，但由于 stream 中忽略了 SYN ， 我们重新给他加回去
        uint64_t abs_seq_no = unwrap(_headerseq, _isn, _reassembler.stream_out().bytes_written());

        /*
        abs_seq_no 是 payload 荷载数据的第一个Byte对应的index
        要知道 absolute sequence number 第一个index 0 是被 SYN 占用的，
        但是！
        StreamBuffer中并不真正装载 SYN，所以实际对应的 abs_seq_no 应该减去 1 才是对应的 stream index，具体可参见 lab2 pdf 表格
        */
        // std::cout << abs_seq_no << std::endl;
        // std::cout << stream_out().bytes_written() << std::endl;

        /*
        总之 我的 reassembler好像实现的功能太多了点。。。。会导致测试错误？？
        */

        // 不合法seqno 因为这个位置是被SYN占用的  或者 过载(来了一个远超容量的seqno)
        if(abs_seq_no == 0 || (abs_seq_no > stream_out().bytes_written() 
        && abs_seq_no - stream_out().bytes_written() > _capacity)) return;       
        // std::cout << abs_seq_no - stream_out().bytes_written() << std::endl;  
        _reassembler.push_substring(seg.payload().copy(), abs_seq_no - 1, eof);
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const { 
    if(!_set_isn)
        return {};
    // 如上所述，单纯的 bytes_written() 忘记考虑了 SYN 占用的字节，所以加上去 
    // 最后再加上 isn 就是下一个我们希望接收到的 Byte 的 index.
    uint64_t writtensize = _reassembler.stream_out().bytes_written() + 1;
    /*
    这个是因为好像是 收到 FIN 后我们仍希望继续保持通信？？？？

    // ====== 根据 Lab4 应该是有一个 linger 的时间 ？？ ====== // 

    总之就是有这样的测试数据
    那么需要再加上一个 FIN 占用的字节
    */
    if(stream_out().input_ended())
        return WrappingInt32(_isn + static_cast<uint32_t>(writtensize + 1));

    return WrappingInt32(_isn + static_cast<uint32_t>(writtensize)); 
}

size_t TCPReceiver::window_size() const { 
    // 就是 已经放入ByteStream中， 但是未被读取的量
    return stream_out().remaining_capacity(); 
 }

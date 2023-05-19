#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    // DUMMY_CODE(n, isn);
    /*
    2^32 对 uint32_t 数是一个轮回 那么取模即可
    */
    uint64_t loop = static_cast<uint64_t>(1) << 32;
    uint64_t additive = n % loop;
    // 取模后 这个数必然小于 2^32 ， 可以直接强制转换成uint32_t 并调用重载加号
    additive = static_cast<uint32_t>(additive);
    return WrappingInt32{isn + additive};

    /*
    ----------------------IMPORTTANT---------------------
    经观察高人，愚蠢的自己发现
    不用那么麻烦
    直接将uint64_t 强制转换成 uint32_t 在内存中的做法就是直接截取低位32bit 即可实现我们复杂的additive计算

    因为一个64bit数可以表示为

    a * 2^63 + .... + z * 2^32 + ......
    高位这些数据对2^32 取模后均为0，所以实际上我们确实仅仅需要低位32bit数据
    */
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    // DUMMY_CODE(n, isn, checkpoint);
    /*
    根据Lab中的提示，就是说 absolute seqno 和 checkpoint 在uint64_t下的距离
    经过wrap 后的距离 应该是 n 和 wrap(checkpoint)的距离
    两者应该相等
    */
    // 1. 先计算 checkpoint 在wrap后的 值 
    WrappingInt32 wrapcheckpoint = wrap(checkpoint, isn);
    // 2. 计算 wrapcheckpoint 和 seqno n 之间的距离
    const uint32_t distance = static_cast<uint32_t>(n - wrapcheckpoint);
    // 3. 考虑如下一种情况，假设 n = 2^32 - 1        wrapcheckpoint = 1
    // 那么实际上用  wrapcheckpoint - n    才是更近的选择    

    // 我们知道unsigned int 32 最大的间距是 2^32  即 0 - (2 ^ 32 - 1)，以一半为界
    if(distance < (static_cast<uint32_t>(1) << 31)){
        return checkpoint + static_cast<uint64_t>(distance);
    }
    else{
        // 由于 我们的 absolute seqno 总是从0开始的 
        // 试想 situation
        /*
        checkpoint: 10
        distance: 2^32 - 22
        那么在else控制语句块中我们实际采用了distance 22

        此时 10 - 22 会到 2^64 - 12样子
        这其实是不合理的，因为我们认为一个报文段长度不会超过 2^64 所以我们总是用uint64 中大的值 - 小的值 作为 真实距离


        并不是像uint32中循环着求距离 (即 1 - (2^32 - 1) 实际上为2)
        */
        uint64_t temp_ans = checkpoint - ((static_cast<uint64_t>(1) << 32) - static_cast<uint64_t>(distance));
        if(temp_ans > checkpoint) return checkpoint + static_cast<uint64_t>(distance);
        return temp_ans;
    }
}

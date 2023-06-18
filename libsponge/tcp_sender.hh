#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <functional>
#include <queue>

#include <iostream>

// =========== TCP Sender 100% finish


//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
class TCPSender {
  private:
    //! our initial sequence number, the number for our SYN.
    WrappingInt32 _isn;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> _segments_out{};

    //! retransmission timer for the connection
    unsigned int _initial_retransmission_timeout;

    //! outgoing stream of bytes that have not yet been sent
    ByteStream _stream;

    //! the (absolute) sequence number for the next byte to be sent
    uint64_t _next_seqno{0};

    // if this is the first time we send to peer endpoint, SYN is needed.
    // window size is also need to be kept

    /*
    ===== _window_sz 是 收到对端通知的 window size
    ===== _win_size 也是，但在 fill_window 时 这个值会由于数据包的发送缩小
    ===== Reason:
    在过 "指数退避" test 的时候 发现有一个测试案例
    : 发一个 SYN 没收到 ack, 此时 _win_size 实际已经是 0 了， 因为初始值为1，用掉了 SYN
    : 过了 _initial_timeout 的时间 重发成功
    : 过了 2 * _initial_timeout - 1 的时间, 我之前的代码仍旧重发了, 说明我的指数退避失败了
    经过debug 是因为 在 tick 函数内，第一个参数我传入了 _win_size 那么 timer 的 expire 并不会 double RTO
    所以我新加了 一个 _window_sz 这个值是固定的, 只有初始状态下的 1 和 每次 ack 后的更新

    同时 _win_size 在收到对端为 0 的 window_size 后会自动更新为 1
    但 _window_sz 仍旧是 0 ，这样一来在重传的时候就不会去 double time 了.
    */
    // size_t _win_size{1};        // 第一份SYN发送前 默认只发送 1 Byte
    size_t _window_sz{1};

    uint64_t _ackno{0};

    // for bytes in flight calculating
    uint64_t _flight_bytes{0};

    // for close connection
    bool _set_fin{false};

    // for retransmitting
    std::queue<TCPSegment> _wait_ack{};

    // A retransmission timer
    // 一个重要功能就是 指数退避 防止网络拥塞
    class Timer {
        private:
          bool active{false};
          bool expired{false};
          unsigned int _init_timeout;       // 初始的不动的RTO，和Sender类中的 _initial_retransmission_timeout 一个意思
          unsigned int _timeout;            // 指数退避的 RTO， 会 double 的
          unsigned int _cur_time{0};        // 每次 Sender 一个 时钟嘀嗒 都会积攒一定时间

        public:
          Timer(const uint16_t retx_timeout):_init_timeout(retx_timeout), _timeout(retx_timeout){};
          void start(){expired = false, active = true, _cur_time = 0;};
          // if expired, return true and restransmit segments
          void tictoc(uint16_t win_size, unsigned int _tictoc_time, unsigned int& _consecutive_num) {
            if(active){
              _cur_time += _tictoc_time;
              if(_cur_time >= _timeout) {
                active = false;
                expire(win_size, _consecutive_num);
                expired = true;
              }
            }
          }
          void expire(uint16_t win_size, unsigned int& _consecutive_num) {
            if(win_size) {               // note that we do this only if advertised window size is greater than 0
              _consecutive_num += 1;
              _timeout *= 2;
            }
          };
          void reset(){
            _timeout = _init_timeout;
          };
          void stop(){active = false;};
          bool isexpired(){return expired;};
          bool started(){return active;};
    };
  Timer _timer;

  // to test whether the connection is hopeless
  unsigned int _consecutive_transmit_num{0};


  public:
    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //! \name "Input" interface for the writer
    //!@{
    ByteStream &stream_in() { return _stream; }
    const ByteStream &stream_in() const { return _stream; }
    //!@}

    //! \name Methods that can cause the TCPSender to send a segment
    //!@{

    //! \brief A new acknowledgment was received
    void ack_received(const WrappingInt32 ackno, const uint16_t window_size);

    //! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
    void send_empty_segment();

    //! \brief create and send segments to fill as much of the window as possible
    void fill_window();

    //! \brief Notifies the TCPSender of the passage of time
    void tick(const size_t ms_since_last_tick);
    //!@}

    //! \name Accessors
    //!@{

    //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
    //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
    //! (see TCPSegment::length_in_sequence_space())
    size_t bytes_in_flight() const;

    //! \brief Number of consecutive retransmissions that have occurred in a row
    unsigned int consecutive_retransmissions() const;

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,
    //! which will need to fill in the fields that are set by the TCPReceiver
    //! (ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return _segments_out; }
    //!@}

    //! \name What is the next sequence number? (used for testing)
    //!@{

    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return _next_seqno; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(_next_seqno, _isn); }
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH

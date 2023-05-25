#ifndef SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
#define SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH

#include "byte_stream.hh"

#include <cstdint>
#include <string>

//利用操作系统里回收内存碎片的知识，建立一条乱序数据的链表
/*
链表的某个节点就是一块data，并且标记了
*/
struct Node{
  string _data;
  //         其中用 blockstart 代表当前写入数据的 index
  //         用 blockend 代表当前写入数据的末尾 index
  size_t blockstart;
  size_t blockend;
  bool eof;
  Node* next;
  Node(string data):_data(data), blockstart(0), blockend(0), eof(false), next(nullptr){};
  Node(const Node& obj):_data(obj._data), blockstart(obj.blockstart), blockend(obj.blockend), eof(obj.eof), next(obj.next){};
  Node& operator=(const Node& p){
    Node x(p);
    return *this;
  };
};


//! \brief A class that assembles a series of excerpts from a byte stream (possibly out of order,
//! possibly overlapping) into an in-order byte stream.
class StreamReassembler {
  private:
    // Your code here -- add private members as necessary.

    ByteStream _output;     //!< The reassembled in-order byte stream
    size_t _capacity;       //!< The maximum number of bytes
    size_t _remaincapacity; // remainning capacity in link list
    Node* _head;            // 乱序数据链成有序链表的表头

  public:
    //! \brief Construct a `StreamReassembler` that will store up to `capacity` bytes.
    //! \note This capacity limits both the bytes that have been reassembled,
    //! and those that have not yet been reassembled.
    StreamReassembler(const size_t capacity);
    StreamReassembler(const StreamReassembler& p):_output(p._output), _capacity(p._capacity), _remaincapacity(p._remaincapacity), _head(p._head) {};
    StreamReassembler& operator=(const StreamReassembler& p){
      StreamReassembler obj(p);
      return *this;
    }

    //! \brief Receive a substring and write any newly contiguous bytes into the stream.
    //!
    //! The StreamReassembler will stay within the memory limits of the `capacity`.
    //! Bytes that would exceed the capacity are silently discarded.
    //!
    //! \param data the substring
    //! \param index indicates the index (place in sequence) of the first byte in `data`
    //! \param eof the last byte of `data` will be the last byte in the entire stream
    void push_substring(const std::string &data, const uint64_t index, const bool eof);

    //! \name Access the reassembled byte stream
    //!@{
    const ByteStream &stream_out() const { return _output; }
    ByteStream &stream_out() { return _output; }
    //!@}

    //! The number of bytes in the substrings stored but not yet reassembled
    //!
    //! \note If the byte at a particular index has been pushed more than once, it
    //! should only be counted once for the purpose of this function.
    // 在我的实现中 其实就是在链表里的所有数据的大小
    size_t unassembled_bytes() const;

    //! \brief Is the internal state empty (other than the output stream)?
    //! \returns `true` if no substrings are waiting to be assembled
    bool empty() const;

    void insert_to_linklist(string data, size_t index, Node* &head, bool eof);
    void fetch_valid_data();
};

#endif  // SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH

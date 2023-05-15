#include "stream_reassembler.hh"
#include <iostream>
// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

// 创建一个内置ByteStream
StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity), _remaincapacity(capacity), _head(nullptr) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
//! \param data the substring
//! \param index indicates the index (place in sequence) of the first byte in `data`
//! \param eof the last byte of `data` will be the last byte in the entire stream
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    string _data(data);         // 改为string更方便操作
    //先看到来的数据是否可以插入，即对比index和writtensize的关系
    // bytes_written是已写入本地ByteStream的字节数，如果来的这个index刚好是 writtensize值 那么恰好可以放进去的 注意序号从0开始
    // 如果这个writtensize值大于index，那么要把多余的这部分裁剪掉
    // 如果这个writtensize值小于index，那么说明中间漏了一些字节数据
    if(index <= _output.bytes_written()){
        // 看看是否来的数据有要裁减的地方
        size_t offset = 0;
        // 根据上述判断，我们要裁剪掉一点多余的部分
        // 由于我们希望下一个来到的字节就是序号为writtensize的数据
        // 所以我们提前判断当前来的字节流是不是完全重复，即从index开始加上本字节流的长度完全小于writtensize的话，说明当前字节流早已进入过ByteStream中
        // 否的的话我们就进行裁剪，把overlap的数据裁剪掉
        if(index + data.length() >= _output.bytes_written()){
            offset = _output.bytes_written() - index;
            _data = _data.substr(offset);
        }
        else return;        // ignore the sequence, because all the data has been in output

        // for the comming data , judge the real size it can write in 就是看看ByteStream能不能写下那么多呢
        size_t _writesize = min(_data.length(), _output.remaining_capacity());
        // 能写的情况下才写入
        if(_writesize >= _data.length()){
            _output.write(_data);
            if(eof)
                _output.end_input();
        }
        // 否则的话，把能写的部分先写入ByteStream，其余的放入到链表中
        else{
            // we can just write a portion of data into output, the rest should be kept in link list
            string _writedata = _data.substr(0, _writesize);
            string _keepdata = _data.substr(_writesize);
            _output.write(_writedata);
            insert_to_linklist(_keepdata, index + offset + _writesize, _head, eof);
        }
    }
    else{
        // 这就是基于判断中的第三个情况，此时的数据完全没有能写入的部分，全部放进链表中
        // 把序号过大的数据写入到链表中
        insert_to_linklist(_data, index, _head, eof);
    }
    // 一旦有空间且链表有数据就去看看有没有能放的
    fetch_valid_data();
}

size_t StreamReassembler::unassembled_bytes() const { return _capacity - _remaincapacity; }

bool StreamReassembler::empty() const { return _remaincapacity == _capacity; }

void StreamReassembler::insert_to_linklist(string data, size_t index, Node* head, bool eof){
    // 数据为空 或者 这个Reassembler管道没内存 存数据了 则无操作
    if(data.length() == 0 ||  _remaincapacity == 0) return ;
    // 判断能写入缓存(Not ByteStream)的数量
    size_t _writesize = min(data.length(), _remaincapacity);
    if(_head == nullptr){
        /*
        创建头节点，按需写入相应信息
        其中用 blockstart 代表当前写入数据的 index
        用 blockend 代表当前写入数据的末尾 index
        */
        string _writedata = data.substr(0, _writesize);
        _head = new Node(_writedata);
        _head->blockstart = index;
        _head->blockend = index + _writesize - 1;
        _remaincapacity -= _writesize;
        // 即使这个数据块有eof, 也得判断这个数据块是不是全部被写入了
        if(_writesize == data.length() && eof)
            _head->eof = eof;
        return;
    }
    else{
        // there is a possibility that we should create a new head node;
        /*
        我们认为_head代表的blockstart是最小的未进入ByteStream的数据index
        e.g. 我们得知ByteStream的writtensize为 23，即我们希望下一个来到的数据的index为23.（因为index从0开始）

        结果先来了一个数据包，index = 33.。。。。。我们作为头节点插入了
        现在又来了一个数据包，index = 28，那么我们就要将这个数据包作为新的头节点。
        */
        if(index < _head->blockstart){
            _writesize = min(_writesize, _head->blockstart - index);
            string _writedata = data.substr(0, _writesize);
            Node* temp = new Node(_writedata);
            temp->blockstart = index;
            temp->blockend = index + _writesize - 1;
            temp->next = _head;
            _head = temp;
            _remaincapacity -= _writesize;
            /*
            还是刚刚那个情况。
            假如我们的链表头的节点的block的index 是 [33, 36]
            现在进来一个数据块 [28, 44]
            那么根据上面的代码，我们先把 [28, 32]作为新的头节点
            然后呢，我们把剩下的数据块 [33, 44]作为新的数据块递归地插入链表，由后面else处的代码执行插入操作。
            */
            string _data = data.substr(_writesize); 
            insert_to_linklist(_data, index + _writesize, _head->next, eof);
        }
        else{
            Node* p = head;
            while(p){
                if(index + _writesize - 1 > p->blockend){
                    if(p->next && (p->next->blockstart == p->blockend + 1 || index > p->next->blockstart))
                        p = p->next;
                    else
                        break;
                }
                else return;
            }
            size_t offset;
            if(index > p->blockend) offset = 0;
            else offset = p->blockend + 1 - index;
            if(p->next){
                string _writedata = data.substr(offset);
                _writesize = min(min(_writesize, _writedata.length()), p->next->blockstart - p->blockend - 1);
                _writedata = data.substr(offset, _writesize);
                Node* temp = new Node(_writedata);
                temp->blockstart = index + offset;
                temp->blockend = temp->blockstart + _writesize - 1;
                temp->next = p->next;
                p->next = temp;
                _remaincapacity -= _writesize;
                string _data = data.substr(offset + _writesize);
                insert_to_linklist(_data, temp->blockend + 1, temp->next, eof);
            }
            else{
                string _writedata = data.substr(offset);
                _writesize = min(_writesize, _writedata.length());
                _writedata = _writedata.substr(0, _writesize);
                Node* temp = new Node(_writedata);
                p->next = temp;
                temp->blockstart = index + offset;
                temp->blockend = temp->blockstart + _writesize - 1;
                _remaincapacity -= _writesize;
                if(_writesize + offset == data.length() && eof)
                    temp->eof = eof;
                return;
            }
        }
    }
}

void StreamReassembler::fetch_valid_data(){
    while(_head && _output.remaining_capacity()){
        if(_head->blockstart > _output.bytes_written()){
            // still incontiniuous
            return;
        }
        else{
            if(_head->blockend < _output.bytes_written()){
                // all these bytes have been in stream
                Node* p = _head;
                _remaincapacity += p->_data.length();
                _head = _head->next;
                delete p;
            }
            else{
                size_t offset = _output.bytes_written() - _head->blockstart;
                string _data = _head->_data.substr(offset);
                if(_data.length() > _output.remaining_capacity()){
                    string _writedata = _data.substr(0, _output.remaining_capacity());
                    _output.write(_writedata);
                    _data = _data.substr(_output.remaining_capacity());
                    _head->_data = _data;
                    _head->blockstart += (offset + _writedata.length());
                    _remaincapacity += (offset + _writedata.length());
                    return;
                }
                else{
                    _output.write(_data);
                    Node* p = _head;
                    _remaincapacity += p->_data.length();
                    if(p->eof)
                        _output.end_input();
                    _head = _head->next;
                    delete p;
                }
            }
        }
    }
}

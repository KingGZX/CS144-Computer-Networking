#include "stream_reassembler.hh"
#include <iostream>
// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity), _remaincapacity(capacity), _head(nullptr) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    string _data(data);         // 改为string更方便操作
    //先看到来的数据是否可以插入，即对比index和writtensize的关系
    if(index <= _output.bytes_written()){
        // 看看是否来的数据有要裁减的地方
        size_t offset = 0;
        if(index + data.length() >= _output.bytes_written()){
            offset = _output.bytes_written() - index;
            _data = _data.substr(offset);
        }
        else return;        // ignore the sequence, because all the data has been in output
        // for the comming data , judge the real size it can write in
        size_t _writesize = min(_data.length(), _output.remaining_capacity());
        //_data = _data.substr(0, _writesize);
        if(_writesize >= _data.length()){
            _output.write(_data);
            if(eof)
                _output.end_input();
        }
        else{
            // we can just write a portion of data into output, the rest should be kept in link list
            string _writedata = _data.substr(0, _writesize);
            string _keepdata = _data.substr(_writesize);
            _output.write(_writedata);
            insert_to_linklist(_keepdata, index + offset + _writesize, _head, eof);
        }
    }
    else{
        // 把序号过大的数据写入到链表中
        insert_to_linklist(_data, index, _head, eof);
    }
    fetch_valid_data();
}

size_t StreamReassembler::unassembled_bytes() const { return _capacity - _remaincapacity; }

bool StreamReassembler::empty() const { return _remaincapacity == _capacity; }

void StreamReassembler::insert_to_linklist(string data, size_t index, Node* head, bool eof){
    if(data.length() == 0 ||  _remaincapacity == 0) return ;
    size_t _writesize = min(data.length(), _remaincapacity);
    if(_head == nullptr){
        string _writedata = data.substr(0, _writesize);
        _head = new Node(_writedata);
        _head->blockstart = index;
        _head->blockend = index + _writesize - 1;
        _remaincapacity -= _writesize;
        if(_writesize == data.length() && eof)
            _head->eof = eof;
        return;
    }
    else{
        // there is a possibility that we should create a new head node;
        if(index < _head->blockstart){
            _writesize = min(_writesize, _head->blockstart - index);
            string _writedata = data.substr(0, _writesize);
            Node* temp = new Node(_writedata);
            temp->blockstart = index;
            temp->blockend = index + _writesize - 1;
            temp->next = _head;
            _head = temp;
            _remaincapacity -= _writesize;
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

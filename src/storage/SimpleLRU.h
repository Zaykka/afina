#ifndef AFINA_STORAGE_SIMPLE_LRU_H
#define AFINA_STORAGE_SIMPLE_LRU_H

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include </home/zay/CLionProjects/ts/include/afina/Storage.h>

namespace Afina {
namespace Backend {

/**
 * # Map based implementation
 * That is NOT thread safe implementaiton!!
 */
class SimpleLRU : public Afina::Storage {
public:
    SimpleLRU(size_t max_size = 1024) : _max_size(max_size), _cur_size(0) {
        _lru_head = std::unique_ptr<lru_node>(new lru_node);
        _lru_head->prev = nullptr;
        _lru_head->next = std::unique_ptr<lru_node>(_lru_tail);
        _lru_tail->prev = _lru_head.get();
    }

    ~SimpleLRU() {
        _lru_index.clear();
        if (_lru_head != nullptr) {
            while (_lru_head) {
                auto tmp = std::move(_lru_head);
                _lru_head = std::move(tmp->next);
                tmp.reset();
            }
        }
         // TODO: Here is stack overflow
    }


    // Implements Afina::Storage interface
    bool Put(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool PutIfAbsent(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Set(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Delete(const std::string &key) override;

    // Implements Afina::Storage interface
    bool Get(const std::string &key, std::string &value) override;

private:
    // LRU cache node
    using lru_node = struct lru_node {
        std::string key;
        std::string value;
        lru_node *prev;
        std::unique_ptr<lru_node> next;
    };

    // Maximum number of bytes could be stored in this cache.
    // i.e all (keys+values) must be less the _max_size
    std::size_t _max_size;
    std::size_t _cur_size;

    // Main storage of lru_nodes, elements in this list ordered descending by "freshness": in the head
    // element that wasn't used for longest time.
    //
    // List owns all nodes
    std::unique_ptr<lru_node> _lru_head;
    lru_node *const _lru_tail = new lru_node;
    // Index of nodes from list above, allows fast random access to elements by lru_node#key
    std::map<std::reference_wrapper<const std::string>, std::reference_wrapper<lru_node>, std::less<std::string>> _lru_index;

    typedef std::map<std::reference_wrapper<const std::string>, std::reference_wrapper<SimpleLRU::lru_node>, std::less<std::string>>::iterator _lru_idx_iter;

    lru_node *add_to_tail(std::string key, std::string value);

    void set_new_val(const std::string &key, const std::string &value, _lru_idx_iter tmp);

    void delete_rarely_used();

    void move_to_tail(_lru_idx_iter &it);

    _lru_idx_iter search_key(const std::string &key, const std::string &value);

};

} // namespace Backend
} // namespace Afina

#endif // AFINA_STORAGE_SIMPLE_LRU_H

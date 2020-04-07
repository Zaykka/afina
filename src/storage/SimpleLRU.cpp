#include "SimpleLRU.h"

namespace Afina {
    namespace Backend {

        bool SimpleLRU::Put(const std::string &key, const std::string &value) {
            if (key.size() + value.size() > _max_size) {
                return false;
            }

            auto tmp = search_key(key, value);

            if (tmp != _lru_index.end()) {
                set_new_val(key, value, tmp);
            }
            return true;
        }

        void SimpleLRU::delete_rarely_used() {
            lru_node *most_rarely = _lru_head->next.get();
            if (!most_rarely) {
                return;
            }
            _cur_size -= most_rarely->key.size() + most_rarely->value.size();
            _lru_index.erase(most_rarely->key);
            most_rarely->next->prev = _lru_head.get();
            _lru_head->next = std::move(most_rarely->next);
        }

        void SimpleLRU::move_to_tail(_lru_idx_iter &it) {
            it->second.get().next->prev = it->second.get().prev;
            std::swap(it->second.get().prev->next, it->second.get().next);
            it->second.get().prev = _lru_tail->prev;
            std::swap(it->second.get().next, _lru_tail->prev->next);
            _lru_tail->prev = &it->second.get();
        }

        SimpleLRU::lru_node* SimpleLRU::add_to_tail(std::string key, std::string value) {
            _cur_size += key.size() + value.size();
            auto *new_node = new lru_node{std::move(key), std::move(value), _lru_tail->prev, nullptr};
            new_node->next = std::unique_ptr<lru_node>(new_node);
            std::swap(new_node->next, _lru_tail->prev->next);
            _lru_tail->prev =  new_node;
            return new_node;
        }


        std::map<std::reference_wrapper<const std::string>, std::reference_wrapper<SimpleLRU::lru_node>, std::less<std::string>>::iterator
        SimpleLRU::search_key(const std::string &key, const std::string &value) {
            auto it = _lru_index.find(std::reference_wrapper<const std::string>(key));

            if (it != _lru_index.end()) {
                return it;
            }

            while (key.size() + value.size() + _cur_size > _max_size) {
                delete_rarely_used();
            }

            lru_node *new_node = add_to_tail(key, value);
            _lru_index.emplace(std::reference_wrapper<const std::string>(new_node->key),
                               std::reference_wrapper<SimpleLRU::lru_node>(*new_node));
            return _lru_index.end();
        }
        void SimpleLRU::set_new_val(const std::string &key, const std::string &value, _lru_idx_iter tmp) {
            while (value.size() + _cur_size - tmp->second.get().value.size() > _max_size) {
                delete_rarely_used();
            }
            _cur_size += value.size() - tmp->second.get().value.size();
            tmp->second.get().value = value;
            tmp->second.get().key = key;
            move_to_tail(tmp);
        }
// See MapBasedGlobalLockImpl.h
        bool SimpleLRU::PutIfAbsent(const std::string &key, const std::string &value) {
            if (key.size() + value.size() > _max_size) {
                return false;
            }
            return search_key(key, value) == _lru_index.end();
        }

// See MapBasedGlobalLockImpl.h
        bool SimpleLRU::Set(const std::string &key, const std::string &value) {
            auto tmp = _lru_index.find(std::reference_wrapper<const std::string>(key));

            if (key.size() + value.size() > _max_size || tmp == _lru_index.end()) {
                return false;
            }

            set_new_val(key, value, tmp);
            return true;
        }

// See MapBasedGlobalLockImpl.h
        bool SimpleLRU::Delete(const std::string &key) {
            auto tmp = _lru_index.find(std::reference_wrapper<const std::string>(key));
            if (tmp == _lru_index.end()) {
                return false;
            }


            _cur_size -= tmp->second.get().value.size() + tmp->second.get().key.size();
            lru_node* node = &tmp->second.get();
            _lru_index.erase(key);

            node->next.get()->prev = node->prev;
            node->prev->next = std::move(node->next);
            return true;
        }

// See MapBasedGlobalLockImpl.h
        bool SimpleLRU::Get(const std::string &key, std::string &value) {
            auto tmp = _lru_index.find(std::reference_wrapper<const std::string>(key));
            if (tmp == _lru_index.end()) {
                return false;
            }

            value = tmp->second.get().value;
            return true;
        }

    } // namespace Backend
} // namespace Afina

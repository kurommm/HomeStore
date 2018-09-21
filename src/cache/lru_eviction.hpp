//
// Created by Kadayam, Hari on 27/10/17.
//

#pragma once

#include <mutex>
#include "cache_common.hpp"

namespace homestore {

// This structure represents each entry into the evictable location
struct LRUEvictRecord : public boost::intrusive::list_base_hook<> {
    int inserted;
};

class LRUEvictionPolicy {
public:
    typedef LRUEvictRecord RecordType;
    typedef std::function< bool(const LRUEvictRecord &, bool &) > CanEjectCallback;

    LRUEvictionPolicy(int num_entries) {
    }

    ~LRUEvictionPolicy() {
        std::lock_guard< decltype(m_list_guard) > guard(m_list_guard);
        auto it = m_list.begin();
        while (it !=  m_list.end()) {
            it = m_list.erase(it);
        }
    }
 
    void add(LRUEvictRecord &rec) {
        std::lock_guard< decltype(m_list_guard) > guard(m_list_guard);
        m_list.push_back(rec);
        rec.inserted = true;
    }

    void remove(LRUEvictRecord &rec) {
        std::lock_guard< decltype(m_list_guard) > guard(m_list_guard);
        auto it = m_list.iterator_to(rec);
        assert(rec.inserted == true);
        m_list.erase(it);
        rec.inserted = false;
    }

    void eject_next_candidate(const CanEjectCallback &cb) {
        std::lock_guard< decltype(m_list_guard) > guard(m_list_guard);

        auto count = 0U;
        auto itend = m_list.end();
        bool stop = false;
        for (auto it = m_list.begin(); it != itend; ++it) {
            LRUEvictRecord *rec = &(*it);
            /* return the next element */
            assert(rec->inserted == true);
            it = m_list.erase(it);
            rec->inserted = false;
            if (cb(*rec, stop)) {
                if (stop) {
                    return;
                }
                /* point to the last element before delete */
                --it;
            } else {
                /* reinsert it at the same position */
                it = m_list.insert(it, *rec);
                rec->inserted = true;
                count++;
            }
            if (count) { 
                LOGINFOMOD(cache_vmod_evict, "LRU ejection had to skip {} entries", count); 
            }
        }

        // No available candidate to evict
        // TODO: Throw no space available exception.
        return;
    }

    void upvote(LRUEvictRecord &rec) {
        std::lock_guard< decltype(m_list_guard) > guard(m_list_guard);
        if (rec.inserted) {
            m_list.erase(m_list.iterator_to(rec));
            m_list.push_back(rec);
        }
    }

    void downvote(LRUEvictRecord &rec) {
        std::lock_guard< decltype(m_list_guard) > guard(m_list_guard);
        if (rec.inserted) {
            m_list.erase(m_list.iterator_to(rec));
            m_list.push_front(rec);
         }
    }

private:
    std::mutex m_list_guard;
    boost::intrusive::list < LRUEvictRecord > m_list;
};

}

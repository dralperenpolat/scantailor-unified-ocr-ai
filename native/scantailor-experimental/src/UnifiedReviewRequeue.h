// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "PageId.h"
#include <set>
#include <vector>

// Runs on the GUI thread. Workers use immutable snapshots; only the changed
// identities are queued, never a stale result or the whole book.
class UnifiedReviewRequeue {
    std::vector<int> m_active;
    std::set<PageId> m_dirty;
public:
    void begin(std::vector<int> const& ids){m_active=ids;m_dirty.clear();}
    template<class Samples,class Matches> void inspect(Samples const& samples,Matches matches){
        for(int index:m_active)if(index>=0&&size_t(index)<samples.size()&&!matches(samples[index]))m_dirty.insert(samples[index].page.id());
    }
    size_t size()const{return m_dirty.size();}
    std::set<PageId> take(){auto pages=std::move(m_dirty);m_dirty.clear();m_active.clear();return pages;}
};

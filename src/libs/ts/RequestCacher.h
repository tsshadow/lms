
#pragma once

#include <map>
#include <string>
#include <vector>
#include <vector>
#include "database/ClusterId.hpp"

class RequestCacher {
public:
    static RequestCacher &getInstance() {
        static RequestCacher instance; // Guaranteed to be destroyed.
        // Instantiated on first use.
        return instance;
    };


    bool hasCache(const std::vector<lms::db::ClusterId> &aClusters) {
        return !mLookupTable[aClusters].empty();
    }

    bool getCache(const std::vector<lms::db::ClusterId> &aClusters) {
        return mLookupTable[aClusters];
    }

    void addToCache(const std::vector<lms::db::ClusterId> &aClusters, std::vector<int> aValues) {
        mLookupTable.emplace(aClusters, aValues);
    }

    void invalidateCache()
    {
        mLookupTable.clear();
    }

    std::map<std::vector<lms::db::ClusterId>, std::vector<int>> mLookupTable;
};

#pragma once

#include <string>
#include <vector>

#include "structs/Journal.h"
#include "structs/SuperBlock.h"

namespace JournalManager {
    constexpr int kJournalCount = 50;

    bool isExt3(const SuperBlock& sb);
    int64_t journalStart(int64_t partStart);
    void initialize(const std::string& diskPath, int64_t partStart);
    void append(const std::string& diskPath, int64_t partStart, const SuperBlock& sb,
                const std::string& operation, const std::string& path, const std::string& content);
    std::vector<JournalEntry> readAll(const std::string& diskPath, int64_t partStart, const SuperBlock& sb);
}

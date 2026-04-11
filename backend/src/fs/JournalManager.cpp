#include "fs/JournalManager.h"

#include "disk/BinaryIO.h"

#include <cstring>
#include <ctime>

namespace JournalManager {
    bool isExt3(const SuperBlock& sb) {
        return sb.s_filesystem_type == 3;
    }

    int64_t journalStart(int64_t partStart) {
        return partStart + static_cast<int64_t>(sizeof(SuperBlock));
    }

    void initialize(const std::string& diskPath, int64_t partStart) {
        JournalEntry empty{};
        empty.active = '0';
        for (int i = 0; i < kJournalCount; ++i) {
            BinaryIO::writeAt(diskPath, journalStart(partStart) + static_cast<int64_t>(i) * sizeof(JournalEntry), &empty, sizeof(JournalEntry));
        }
    }

    void append(const std::string& diskPath, int64_t partStart, const SuperBlock& sb,
                const std::string& operation, const std::string& path, const std::string& content) {
        if (!isExt3(sb)) return;

        int freeIndex = -1;
        JournalEntry entry{};
        for (int i = 0; i < kJournalCount; ++i) {
            BinaryIO::readAt(diskPath, journalStart(partStart) + static_cast<int64_t>(i) * sizeof(JournalEntry), &entry, sizeof(JournalEntry));
            if (entry.active != '1') {
                freeIndex = i;
                break;
            }
        }
        if (freeIndex < 0) freeIndex = kJournalCount - 1;

        std::memset(&entry, 0, sizeof(JournalEntry));
        entry.active = '1';
        std::strncpy(entry.operation, operation.c_str(), sizeof(entry.operation) - 1);
        std::strncpy(entry.path, path.c_str(), sizeof(entry.path) - 1);
        std::strncpy(entry.content, content.c_str(), sizeof(entry.content) - 1);
        entry.timestamp = static_cast<int64_t>(std::time(nullptr));
        BinaryIO::writeAt(diskPath, journalStart(partStart) + static_cast<int64_t>(freeIndex) * sizeof(JournalEntry), &entry, sizeof(JournalEntry));
    }

    std::vector<JournalEntry> readAll(const std::string& diskPath, int64_t partStart, const SuperBlock& sb) {
        std::vector<JournalEntry> entries;
        if (!isExt3(sb)) return entries;

        for (int i = 0; i < kJournalCount; ++i) {
            JournalEntry entry{};
            BinaryIO::readAt(diskPath, journalStart(partStart) + static_cast<int64_t>(i) * sizeof(JournalEntry), &entry, sizeof(JournalEntry));
            if (entry.active == '1') entries.push_back(entry);
        }
        return entries;
    }
}

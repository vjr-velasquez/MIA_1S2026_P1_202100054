#ifndef STRUCTS_JOURNAL_H
#define STRUCTS_JOURNAL_H

#include <cstdint>

#pragma pack(push, 1)
struct JournalEntry {
    char active;
    char operation[16];
    char path[128];
    char content[128];
    int64_t timestamp;
};
#pragma pack(pop)

#endif

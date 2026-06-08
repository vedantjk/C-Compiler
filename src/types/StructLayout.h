#pragma once

#include "types.h"

#include <string>
#include <unordered_map>
#include <vector>

// Computed layout of a struct type: the byte offset of every member (in
// declaration order), the total size including trailing padding, and the
// alignment (the maximum member alignment).
struct MemberEntry
{
    std::string name;
    std::shared_ptr<Type> type;
    int offset;
};

struct StructLayout
{
    int size = 0;
    int alignment = 1;
    std::vector<MemberEntry> members;
};

// Per-translation-unit registry of struct layouts, keyed by each tag's resolved
// (uniquely mangled) name. The semantic analyzer fills this in as it validates
// each definition; TACKY and codegen read it for member offsets, object sizes,
// and System V ABI classification. A tag that is declared but not defined has no
// entry here — that absence is exactly what makes its type incomplete.
//
// One process compiles one translation unit, so a single global table is enough.
inline std::unordered_map<std::string, StructLayout> &structLayoutTable()
{
    static std::unordered_map<std::string, StructLayout> table;
    return table;
}

inline const StructLayout *findStructLayout(const std::string &tag)
{
    auto &t = structLayoutTable();
    auto it = t.find(tag);
    return it == t.end() ? nullptr : &it->second;
}

inline const StructLayout *structLayoutOf(const std::shared_ptr<Type> &t)
{
    if (const auto s = std::dynamic_pointer_cast<StructType>(t))
        return findStructLayout(s->getName());
    return nullptr;
}

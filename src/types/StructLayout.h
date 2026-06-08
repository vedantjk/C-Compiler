#pragma once

#include "types.h"

#include <string>
#include <unordered_map>
#include <utility>
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

// ---------------------------------------------------------------------------
// System V AMD64 eightbyte classification (reduced to our scalar set).
//
// A struct of <=16 bytes travels in one or two registers; each eightbyte is SSE
// when every scalar overlapping it is a `double`, otherwise INTEGER. A struct
// >16 bytes is class MEMORY: passed on the stack, returned via a hidden pointer.
// The classification is needed by both TACKY (to size temps) and codegen (to
// pick the register banks), so it lives here next to the layout table.
// ---------------------------------------------------------------------------
enum class Eightbyte
{
    Integer,
    SSE
};

struct StructABI
{
    long long size = 0;
    int align = 1;
    bool inMemory = false;          // size > 16
    std::vector<Eightbyte> classes; // one entry per eightbyte (empty if inMemory)
};

// Byte size of any type, recursing into arrays and structs (mirrors the TACKY /
// SA size routines but kept local so the header is self-contained).
inline long long abiSizeOf(const std::shared_ptr<Type> &t)
{
    if (const auto a = std::dynamic_pointer_cast<ArrayType>(t))
        return static_cast<long long>(a->getSize()) * abiSizeOf(a->getInner());
    if (const auto s = std::dynamic_pointer_cast<StructType>(t))
    {
        const StructLayout *l = findStructLayout(s->getName());
        return l ? l->size : 0;
    }
    if (std::dynamic_pointer_cast<CharType>(t) || std::dynamic_pointer_cast<SignedCharType>(t) ||
        std::dynamic_pointer_cast<UnsignedCharType>(t))
        return 1;
    if (std::dynamic_pointer_cast<IntType>(t) || std::dynamic_pointer_cast<UnsignedIntType>(t))
        return 4;
    return 8; // long, unsigned long, double, pointer
}

// Append the scalar leaves of `type` (each is `<double?, byteOffset>`) placed at
// the given base offset. Each scalar lies wholly within one eightbyte because of
// natural alignment, so only its starting offset matters.
inline void abiFlattenLeaves(const std::shared_ptr<Type> &type, long long base,
                             std::vector<std::pair<bool, long long>> &out)
{
    if (const auto s = std::dynamic_pointer_cast<StructType>(type))
    {
        if (const StructLayout *l = findStructLayout(s->getName()))
            for (const auto &m : l->members)
                abiFlattenLeaves(m.type, base + m.offset, out);
        return;
    }
    if (const auto a = std::dynamic_pointer_cast<ArrayType>(type))
    {
        const long long es = abiSizeOf(a->getInner());
        for (size_t i = 0; i < a->getSize(); ++i)
            abiFlattenLeaves(a->getInner(), base + static_cast<long long>(i) * es, out);
        return;
    }
    out.push_back({std::dynamic_pointer_cast<DoubleType>(type) != nullptr, base});
}

inline StructABI classifyStruct(const std::string &tag)
{
    StructABI abi;
    const StructLayout *layout = findStructLayout(tag);
    if (!layout)
        return abi;
    abi.size = layout->size;
    abi.align = layout->alignment;
    if (abi.size > 16)
    {
        abi.inMemory = true;
        return abi;
    }
    const int n = static_cast<int>((abi.size + 7) / 8); // 1 or 2 eightbytes
    abi.classes.assign(n, Eightbyte::SSE);              // SSE unless a non-double leaf appears
    std::vector<std::pair<bool, long long>> leaves;
    for (const auto &m : layout->members)
        abiFlattenLeaves(m.type, m.offset, leaves);
    for (const auto &[isDouble, off] : leaves)
    {
        const int eb = static_cast<int>(off / 8);
        if (eb < n && !isDouble)
            abi.classes[eb] = Eightbyte::Integer;
    }
    return abi;
}

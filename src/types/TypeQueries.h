#pragma once

#include "StructLayout.h"
#include "types.h"

#include <memory>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Canonical size/align queries for any Type.
//
// Two alignment variants exist because the rule differs by context:
//   typeAlignOf   — natural type alignment, NO 16-byte array rule.  Used when
//                   laying out struct members: `char arr[19]` inside a struct
//                   aligns to 1 (gcc behaviour; the 16-rule would over-pad).
//   objectAlignOf — allocation alignment, WITH the 16-byte array rule.  Used
//                   for standalone local/static variable slots (SysV / book
//                   rule: arrays of >=16 bytes get 16-byte stack alignment).
// ---------------------------------------------------------------------------

// Byte size of any type: arrays recurse (count * element size); structs come
// from their computed layout (0 if still incomplete — callers gate on
// completeness); char kinds are 1; int/uint are 4; everything else is 8
// (long, unsigned long, double, pointer).
inline long long sizeOfType(const std::shared_ptr<Type> &t)
{
    if (const auto a = std::dynamic_pointer_cast<ArrayType>(t))
        return static_cast<long long>(a->getSize()) * sizeOfType(a->getInner());
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

// Natural type alignment — NO 16-byte-array rule. Use for struct member layout.
inline int typeAlignOf(const std::shared_ptr<Type> &t)
{
    if (const auto a = std::dynamic_pointer_cast<ArrayType>(t))
        return typeAlignOf(a->getInner());
    if (const auto s = std::dynamic_pointer_cast<StructType>(t))
    {
        const StructLayout *l = findStructLayout(s->getName());
        return l ? l->alignment : 1;
    }
    return static_cast<int>(sizeOfType(t));
}

// Allocation alignment — WITH 16-byte-array rule. Use for standalone object
// slots (local arrays, static variables).
inline int objectAlignOf(const std::shared_ptr<Type> &t)
{
    if (const auto a = std::dynamic_pointer_cast<ArrayType>(t))
    {
        const int base = objectAlignOf(a->getInner());
        return sizeOfType(t) >= 16 ? 16 : base;
    }
    if (const auto s = std::dynamic_pointer_cast<StructType>(t))
    {
        const StructLayout *l = findStructLayout(s->getName());
        return l ? l->alignment : 1;
    }
    return static_cast<int>(sizeOfType(t));
}

// ---------------------------------------------------------------------------
// System V AMD64 eightbyte classification (reduced to our scalar set).
//
// A struct of <=16 bytes travels in one or two registers; each eightbyte is SSE
// when every scalar overlapping it is a `double`, otherwise INTEGER. A struct
// >16 bytes is class MEMORY: passed on the stack, returned via a hidden pointer.
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
        const long long es = sizeOfType(a->getInner());
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

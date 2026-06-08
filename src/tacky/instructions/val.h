#pragma once
#include <string>
#include <variant>

enum class ConstantType
{
    INT,
    LONG,
    UINT,
    ULONG,
    DOUBLE,
    POINTER,
    SCHAR, // char / signed char (1 byte, signed)
    UCHAR  // unsigned char (1 byte, unsigned)
};

// Width in bytes: char types are 1, int/uint are 4, long/ulong/pointer are 8.
inline int ctBytes(const ConstantType t)
{
    if (t == ConstantType::SCHAR || t == ConstantType::UCHAR)
        return 1;
    return (t == ConstantType::LONG || t == ConstantType::ULONG || t == ConstantType::POINTER) ? 8
                                                                                               : 4;
}

inline bool isUnsignedCt(const ConstantType t)
{
    // Pointers compare as unsigned quantities (an address with the high bit set
    // must order above a low one), so POINTER counts as unsigned here.
    return t == ConstantType::UINT || t == ConstantType::ULONG || t == ConstantType::POINTER ||
           t == ConstantType::UCHAR;
}

inline bool isIntCt(const ConstantType t)
{
    return t == ConstantType::INT || t == ConstantType::LONG || t == ConstantType::SCHAR;
}

inline bool isDouble(const ConstantType t) { return t == ConstantType::DOUBLE; }

class TackyConstant
{
  public:
    unsigned long long value;
    ConstantType type;
    explicit TackyConstant(const unsigned long long value_, const ConstantType type_)
        : value(value_), type(type_) {};
};

class TackyFloatingConstant
{
  public:
    double value;
    ConstantType type;
    explicit TackyFloatingConstant(const double value_, const ConstantType type_)
        : value(value_), type(type_) {};
};

class TackyVar
{
  public:
    std::string name;
    ConstantType type; // width of the value this virtual register holds

    // Self-typing fields (additive; not yet read by codegen — side-maps are still
    // authoritative). Populated at the same sites that write the side-maps so the
    // next refactor stage (CG2) can drop the maps and read these instead.
    bool isStatic = false; // true iff the name is in staticNames (RIP-relative)
    long long objSize = 0; // aggregate slot size in bytes; 0 => scalar
    int objAlign = 0;      // aggregate slot alignment; 0 => scalar
    std::string structTag; // non-empty => struct; codegen calls classifyStruct(structTag)

    TackyVar(std::string name_, const ConstantType type_) : name(std::move(name_)), type(type_) {};
};

using TackyVal = std::variant<TackyConstant, TackyVar, TackyFloatingConstant>;

class DereferencedPointer
{
  public:
    TackyVal ptr; // the pointer operand; ptr's type is POINTER
    explicit DereferencedPointer(TackyVal ptr_) : ptr(std::move(ptr_)) {}
};

using ExpResult = std::variant<TackyVal, DereferencedPointer>;

// Both alternatives carry a `type`, so every TackyVal is self-describing.
inline ConstantType typeOf(const TackyVal &v)
{
    return std::visit([](const auto &x) { return x.type; }, v);
}

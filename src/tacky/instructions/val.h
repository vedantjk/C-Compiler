#pragma once
#include <string>
#include <variant>

enum class ConstantType
{
    INT,
    LONG,
    UINT,
    ULONG,
    DOUBLE
};

// Width in bytes: int/uint are 4, long/ulong are 8.
inline int ctBytes(const ConstantType t)
{
    return (t == ConstantType::LONG || t == ConstantType::ULONG) ? 8 : 4;
}

inline bool isUnsignedCt(const ConstantType t)
{
    return t == ConstantType::UINT || t == ConstantType::ULONG;
}

inline bool isIntCt(const ConstantType t)
{
    return t == ConstantType::INT || t == ConstantType::LONG;
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
    TackyVar(std::string name_, const ConstantType type_) : name(std::move(name_)), type(type_) {};
};

using TackyVal = std::variant<TackyConstant, TackyVar, TackyFloatingConstant>;

// Both alternatives carry a `type`, so every TackyVal is self-describing.
inline ConstantType typeOf(const TackyVal &v)
{
    return std::visit([](const auto &x) { return x.type; }, v);
}

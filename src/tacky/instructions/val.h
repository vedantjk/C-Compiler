#pragma once
#include <string>
#include <variant>

enum class ConstantType
{
    INT,
    LONG
};

class TackyConstant
{
  public:
    long long value;
    ConstantType type;
    explicit TackyConstant(const long long value_, const ConstantType type_)
        : value(value_), type(type_) {};
};

class TackyVar
{
  public:
    std::string name;
    ConstantType type; // width of the value this virtual register holds
    TackyVar(std::string name_, const ConstantType type_) : name(std::move(name_)), type(type_) {};
};

using TackyVal = std::variant<TackyConstant, TackyVar>;

// Both alternatives carry a `type`, so every TackyVal is self-describing.
inline ConstantType typeOf(const TackyVal &v)
{
    return std::visit([](const auto &x) { return x.type; }, v);
}

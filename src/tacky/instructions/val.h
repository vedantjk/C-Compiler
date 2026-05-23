#pragma once
#include <string>
#include <variant>

class TackyConstant
{
    public:
    std::string value;
      explicit TackyConstant(std::string value_) : value(std::move(value_)) {};
};

class TackyVar
{
    public:
    std::string name;
    TackyVar(std::string name_) : name(std::move(name_)) {};
};

using TackyVal = std::variant<TackyConstant, TackyVar>;

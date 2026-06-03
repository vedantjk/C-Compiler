#pragma once
#include "../../utils/common.h"
#include "val.h"
#include <optional>

class TackyInstruction
{
  public:
    int line, column;
    TackyInstruction(const int line_, const int column_) : line(line_), column(column_) {};
    virtual ~TackyInstruction() = default;
};

class TackyReturn : public TackyInstruction
{
  public:
    std::optional<TackyVal> val;
    TackyReturn(const int line_, const int column_, std::optional<TackyVal> val_)
        : TackyInstruction(line_, column_), val(std::move(val_)) {};
};

class TackyUnary : public TackyInstruction
{
  public:
    UnaryOp op;
    TackyVal src, dst;
    TackyUnary(const int line_, const int column_, UnaryOp op_, TackyVal src_, TackyVal dst_)
        : TackyInstruction(line_, column_), op(op_), src(std::move(src_)), dst(std::move(dst_)) {};
};

class TackyBinary : public TackyInstruction
{
  public:
    BinaryOp op;
    TackyVal src1, src2, dst;
    TackyBinary(const int line_, const int col_, BinaryOp op_, TackyVal src1_, TackyVal src2_,
                TackyVal dst_)
        : TackyInstruction(line_, col_), op(op_), src1(std::move(src1_)), src2(std::move(src2_)),
          dst(std::move(dst_))
    {
    }
};

class TackyCopy : public TackyInstruction
{
  public:
    TackyVal src, dst;
    TackyCopy(int line_, int col_, TackyVal src_, TackyVal dst_)
        : TackyInstruction(line_, col_), src(std::move(src_)), dst(std::move(dst_))
    {
    }
};

class TackySignExtend : public TackyInstruction
{
  public:
    TackyVal src, dst;
    TackySignExtend(int line_, int col_, TackyVal src_, TackyVal dst_)
        : TackyInstruction(line_, col_), src(std::move(src_)), dst(std::move(dst_))
    {
    }
};

class TackyTruncate : public TackyInstruction
{
  public:
    TackyVal src, dst;
    TackyTruncate(int line_, int col_, TackyVal src_, TackyVal dst_)
        : TackyInstruction(line_, col_), src(std::move(src_)), dst(std::move(dst_))
    {
    }
};

class TackyJump : public TackyInstruction
{
  public:
    std::string identifier;
    TackyJump(int line_, int col_, std::string identifier_)
        : TackyInstruction(line_, col_), identifier(std::move(identifier_))
    {
    }
};

class TackyJumpIfZero : public TackyInstruction
{
  public:
    TackyVal condition;
    std::string identifier;
    TackyJumpIfZero(int line_, int col_, TackyVal condition_, std::string identifier_)
        : TackyInstruction(line_, col_), condition(std::move(condition_)),
          identifier(std::move(identifier_))
    {
    }
};

class TackyJumpIfNotZero : public TackyInstruction
{
  public:
    TackyVal condition;
    std::string identifier;
    TackyJumpIfNotZero(int line_, int col_, TackyVal condition_, std::string identifier_)
        : TackyInstruction(line_, col_), condition(std::move(condition_)),
          identifier(std::move(identifier_))
    {
    }
};

class TackyLabel : public TackyInstruction
{
  public:
    std::string identifier;
    TackyLabel(int line_, int col_, std::string identifier_)
        : TackyInstruction(line_, col_), identifier(std::move(identifier_))
    {
    }
};

class TackyFunctionCall : public TackyInstruction
{
  public:
    std::string funcName;
    std::vector<TackyVal> args;
    TackyVal dst;

    TackyFunctionCall(int line_, int col_, std::string funcName_, std::vector<TackyVal> args_,
                      TackyVal dst_)
        : TackyInstruction(line_, col_), funcName(std::move(funcName_)), args(std::move(args_)),
          dst(std::move(dst_))
    {
    }
};
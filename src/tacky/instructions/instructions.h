#pragma once
#include "../../utils/common.h"
#include "val.h"
#include <optional>
#include <utility>

enum class TackyKind
{
    Return,
    Unary,
    Binary,
    Copy,
    SignExtend,
    Truncate,
    ZeroExtend,
    DoubleToInt,
    DoubleToUInt,
    IntToDouble,
    UIntToDouble,
    Jump,
    JumpIfZero,
    JumpIfNotZero,
    Label,
    FunctionCall,
    GetAddress,
    Load,
    Store,
    CopyToOffset,
};

class TackyInstruction
{
  public:
    const TackyKind kind;
    int line, column;
    TackyInstruction(TackyKind k, const int line_, const int column_)
        : kind(k), line(line_), column(column_) {};
    virtual ~TackyInstruction() = default;
    TackyKind getKind() const { return kind; }
};

class TackyReturn : public TackyInstruction
{
  public:
    std::optional<TackyVal> val;
    TackyReturn(const int line_, const int column_, std::optional<TackyVal> val_)
        : TackyInstruction(TackyKind::Return, line_, column_), val(std::move(val_)) {};
    static bool classof(TackyKind k) { return k == TackyKind::Return; }
};

class TackyUnary : public TackyInstruction
{
  public:
    UnaryOp op;
    TackyVal src, dst;
    TackyUnary(const int line_, const int column_, UnaryOp op_, TackyVal src_, TackyVal dst_)
        : TackyInstruction(TackyKind::Unary, line_, column_), op(op_), src(std::move(src_)),
          dst(std::move(dst_)) {};
    static bool classof(TackyKind k) { return k == TackyKind::Unary; }
};

class TackyBinary : public TackyInstruction
{
  public:
    BinaryOp op;
    TackyVal src1, src2, dst;
    TackyBinary(const int line_, const int col_, BinaryOp op_, TackyVal src1_, TackyVal src2_,
                TackyVal dst_)
        : TackyInstruction(TackyKind::Binary, line_, col_), op(op_), src1(std::move(src1_)),
          src2(std::move(src2_)), dst(std::move(dst_))
    {
    }
    static bool classof(TackyKind k) { return k == TackyKind::Binary; }
};

class TackyCopy : public TackyInstruction
{
  public:
    TackyVal src, dst;
    TackyCopy(int line_, int col_, TackyVal src_, TackyVal dst_)
        : TackyInstruction(TackyKind::Copy, line_, col_), src(std::move(src_)), dst(std::move(dst_))
    {
    }
    static bool classof(TackyKind k) { return k == TackyKind::Copy; }
};

class TackySignExtend : public TackyInstruction
{
  public:
    TackyVal src, dst;
    TackySignExtend(int line_, int col_, TackyVal src_, TackyVal dst_)
        : TackyInstruction(TackyKind::SignExtend, line_, col_), src(std::move(src_)),
          dst(std::move(dst_))
    {
    }
    static bool classof(TackyKind k) { return k == TackyKind::SignExtend; }
};

class TackyTruncate : public TackyInstruction
{
  public:
    TackyVal src, dst;
    TackyTruncate(int line_, int col_, TackyVal src_, TackyVal dst_)
        : TackyInstruction(TackyKind::Truncate, line_, col_), src(std::move(src_)),
          dst(std::move(dst_))
    {
    }
    static bool classof(TackyKind k) { return k == TackyKind::Truncate; }
};

class TackyZeroExtend : public TackyInstruction
{
  public:
    TackyVal src, dst;
    TackyZeroExtend(int line_, int col_, TackyVal src_, TackyVal dst_)
        : TackyInstruction(TackyKind::ZeroExtend, line_, col_), src(std::move(src_)),
          dst(std::move(dst_))
    {
    }
    static bool classof(TackyKind k) { return k == TackyKind::ZeroExtend; }
};

class TackyDoubleToInt : public TackyInstruction
{
  public:
    TackyVal src, dst;
    TackyDoubleToInt(int line_, int col_, TackyVal src_, TackyVal dst_)
        : TackyInstruction(TackyKind::DoubleToInt, line_, col_), src(std::move(src_)),
          dst(std::move(dst_))
    {
    }
    static bool classof(TackyKind k) { return k == TackyKind::DoubleToInt; }
};

class TackyDoubleToUInt : public TackyInstruction
{
  public:
    TackyVal src, dst;
    TackyDoubleToUInt(int line_, int col_, TackyVal src_, TackyVal dst_)
        : TackyInstruction(TackyKind::DoubleToUInt, line_, col_), src(std::move(src_)),
          dst(std::move(dst_))
    {
    }
    static bool classof(TackyKind k) { return k == TackyKind::DoubleToUInt; }
};

class TackyIntToDouble : public TackyInstruction
{
  public:
    TackyVal src, dst;
    TackyIntToDouble(int line_, int col_, TackyVal src_, TackyVal dst_)
        : TackyInstruction(TackyKind::IntToDouble, line_, col_), src(std::move(src_)),
          dst(std::move(dst_))
    {
    }
    static bool classof(TackyKind k) { return k == TackyKind::IntToDouble; }
};

class TackyUIntToDouble : public TackyInstruction
{
  public:
    TackyVal src, dst;
    TackyUIntToDouble(int line_, int col_, TackyVal src_, TackyVal dst_)
        : TackyInstruction(TackyKind::UIntToDouble, line_, col_), src(std::move(src_)),
          dst(std::move(dst_))
    {
    }
    static bool classof(TackyKind k) { return k == TackyKind::UIntToDouble; }
};

class TackyJump : public TackyInstruction
{
  public:
    std::string identifier;
    TackyJump(int line_, int col_, std::string identifier_)
        : TackyInstruction(TackyKind::Jump, line_, col_), identifier(std::move(identifier_))
    {
    }
    static bool classof(TackyKind k) { return k == TackyKind::Jump; }
};

class TackyJumpIfZero : public TackyInstruction
{
  public:
    TackyVal condition;
    std::string identifier;
    TackyJumpIfZero(int line_, int col_, TackyVal condition_, std::string identifier_)
        : TackyInstruction(TackyKind::JumpIfZero, line_, col_), condition(std::move(condition_)),
          identifier(std::move(identifier_))
    {
    }
    static bool classof(TackyKind k) { return k == TackyKind::JumpIfZero; }
};

class TackyJumpIfNotZero : public TackyInstruction
{
  public:
    TackyVal condition;
    std::string identifier;
    TackyJumpIfNotZero(int line_, int col_, TackyVal condition_, std::string identifier_)
        : TackyInstruction(TackyKind::JumpIfNotZero, line_, col_), condition(std::move(condition_)),
          identifier(std::move(identifier_))
    {
    }
    static bool classof(TackyKind k) { return k == TackyKind::JumpIfNotZero; }
};

class TackyLabel : public TackyInstruction
{
  public:
    std::string identifier;
    TackyLabel(int line_, int col_, std::string identifier_)
        : TackyInstruction(TackyKind::Label, line_, col_), identifier(std::move(identifier_))
    {
    }
    static bool classof(TackyKind k) { return k == TackyKind::Label; }
};

class TackyFunctionCall : public TackyInstruction
{
  public:
    std::string funcName;
    std::vector<TackyVal> args;
    TackyVal dst;
    bool variadic;

    TackyFunctionCall(int line_, int col_, std::string funcName_, std::vector<TackyVal> args_,
                      TackyVal dst_, bool variadic_ = false)
        : TackyInstruction(TackyKind::FunctionCall, line_, col_), funcName(std::move(funcName_)),
          args(std::move(args_)), dst(std::move(dst_)), variadic(variadic_)
    {
    }
    static bool classof(TackyKind k) { return k == TackyKind::FunctionCall; }
};

class TackyGetAddress : public TackyInstruction
{
  public:
    TackyVal src, dst;
    TackyGetAddress(int line_, int col_, TackyVal src_, TackyVal dst_)
        : TackyInstruction(TackyKind::GetAddress, line_, col_), src(std::move(src_)),
          dst(std::move(dst_))
    {
    }
    static bool classof(TackyKind k) { return k == TackyKind::GetAddress; }
};

class TackyLoad : public TackyInstruction
{
  public:
    TackyVal srcPtr, dst;
    TackyLoad(int line_, int col_, TackyVal srcPtr_, TackyVal dst_)
        : TackyInstruction(TackyKind::Load, line_, col_), srcPtr(std::move(srcPtr_)),
          dst(std::move(dst_))
    {
    }
    static bool classof(TackyKind k) { return k == TackyKind::Load; }
};

class TackyStore : public TackyInstruction
{
  public:
    TackyVal src, dstPtr;
    TackyStore(int line_, int col_, TackyVal src_, TackyVal dstPtr_)
        : TackyInstruction(TackyKind::Store, line_, col_), src(std::move(src_)),
          dstPtr(std::move(dstPtr_))
    {
    }
    static bool classof(TackyKind k) { return k == TackyKind::Store; }
};

// Store `src` into the aggregate object `dst` at byte `offset`. Used to fill array
// elements during initialization; codegen lowers it to a mov into the object's
// stack slot (or static data) at slot + offset.
class TackyCopyToOffset : public TackyInstruction
{
  public:
    TackyVal src;
    std::string dst;
    int offset;
    TackyCopyToOffset(int line_, int col_, TackyVal src_, std::string dst_, int offset_)
        : TackyInstruction(TackyKind::CopyToOffset, line_, col_), src(std::move(src_)),
          dst(std::move(dst_)), offset(offset_)
    {
    }
    static bool classof(TackyKind k) { return k == TackyKind::CopyToOffset; }
};

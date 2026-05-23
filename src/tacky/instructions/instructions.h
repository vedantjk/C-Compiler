#pragma once
#include "val.h"
#include "../../utils/common.h"
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

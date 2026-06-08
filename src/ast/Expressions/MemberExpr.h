#pragma once

#include "Expressions.h"
#include <iostream>
#include <memory>
#include <utility>

class MemberExpr : public Expression
{
  public:
    std::unique_ptr<Expression> object;
    std::string field;
    bool isArrow;

    MemberExpr(std::unique_ptr<Expression> object, std::string field, bool isArrow, int line,
               int col)
        : Expression(NodeKind::MemberExpr, line, col), object(std::move(object)),
          field(std::move(field)), isArrow(isArrow)
    {
    }

    static bool classof(NodeKind k) { return k == NodeKind::MemberExpr; }

    void print(std::ostream &out, int tab) const override
    {
        object->print(out, tab);
        if (isArrow)
        {
            out << "->";
        }
        else
            out << ".";
        out << field;
    }
};

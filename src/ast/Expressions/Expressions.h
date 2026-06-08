#pragma once

#include "../ASTNodes/ASTNode.h"

class Type;

class Expression : public ASTNode
{
  public:
    std::shared_ptr<Type> resolvedType;
    bool isLvalue = false;

    Expression(NodeKind k, int line_, int col_) : ASTNode(k, line_, col_) {}
    virtual ~Expression() = default;

    static bool classof(NodeKind k)
    {
        return k >= NodeKind::_FirstExpr && k <= NodeKind::_LastExpr;
    }
};

#pragma once

#include "../ASTNodes/ASTNode.h"

class Statement : public ASTNode
{
  public:
    Statement(NodeKind k, int line, int col) : ASTNode(k, line, col) {}
    virtual ~Statement() = default;

    static bool classof(NodeKind k)
    {
        return k >= NodeKind::_FirstStmt && k <= NodeKind::_LastStmt;
    }
};

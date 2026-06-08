#pragma once

#include "../ASTNodes/ASTNode.h"

class TopLevelNode : public ASTNode
{
  public:
    TopLevelNode(NodeKind k, int line, int col) : ASTNode(k, line, col) {}
    virtual ~TopLevelNode() = default;

    static bool classof(NodeKind k) { return k >= NodeKind::_FirstTop && k <= NodeKind::_LastTop; }
};

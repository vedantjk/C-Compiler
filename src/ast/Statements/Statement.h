#pragma once

#include "../ASTNodes/ASTNode.h"

class Statement : public ASTNode
{
  public:
    Statement(int line, int col) : ASTNode(line, col) {}
    virtual ~Statement() = default;
};
#pragma once

#include "../ASTNodes/ASTNode.h"

class Type;

class Expression : public ASTNode
{
  public:
    std::shared_ptr<Type> resolvedType;
    bool isLvalue = false;

    Expression(int line_, int col_) : ASTNode(line_, col_) {}
    virtual ~Expression() = default;
};

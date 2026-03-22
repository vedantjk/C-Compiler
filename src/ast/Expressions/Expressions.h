#pragma once

#include "../ASTNodes/ASTNode.h"


class Expression : public ASTNode
{
    public:
    Expression(int line_, int col_) : ASTNode(line_, col_) {}
      virtual ~Expression() = default;
};

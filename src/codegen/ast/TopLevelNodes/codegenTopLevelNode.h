#pragma once
#include "../ASTNodes/codegenASTNode.h"

class codegenTopLevelNode : public codegenASTNode
{
  public:
    codegenTopLevelNode(const int line_, const int col_) : codegenASTNode(line_, col_) {};
};
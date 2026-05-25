#pragma once
#include "../ASTNodes/TackyASTNode.h"

class TackyTopLevelNode : public TackyASTNode
{
  public:
    TackyTopLevelNode(const int line_, const int col_) : TackyASTNode(line_, col_) {};
};

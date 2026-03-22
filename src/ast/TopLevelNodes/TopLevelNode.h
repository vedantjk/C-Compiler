#pragma once

#include "../ASTNodes/ASTNode.h"

class TopLevelNode : public ASTNode
{
  public: 
	  TopLevelNode(int line, int col) : ASTNode(line, col) {}
    virtual ~TopLevelNode() = default;
};
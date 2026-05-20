#pragma once

class codegenASTNode
{
  public:
    int line, column;

    codegenASTNode(const int line_, const int column_) : line(line_), column(column_) {};
    virtual ~codegenASTNode() = default;
};
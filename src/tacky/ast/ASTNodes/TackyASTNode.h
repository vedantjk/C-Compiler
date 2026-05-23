#pragma once

class TackyASTNode
{
  public:
    int line, column;

    TackyASTNode(const int line_, const int column_) : line(line_), column(column_) {};
    virtual ~TackyASTNode() = default;
};

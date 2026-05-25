#pragma once

#include <iostream>
#include <memory>

struct Symbol;

class ASTNode
{
  public:
    int line;
    int col;
    std::shared_ptr<Symbol> symbol;
    ASTNode(int line_, int col_) : line(line_), col(col_) {}

    virtual ~ASTNode() = default;

    virtual void print(std::ostream &out, int tab) const = 0;

    int getLine() const { return line; }
    int getCol() const { return col; }
};
#pragma once

#include <iostream>
#include <memory>

class ASTNode
{
    int line;
    int col;

public:

    ASTNode(int line_, int col_) : line(line_), col(col_) {}
    
    virtual ~ASTNode() = default;

    virtual void print(std::ostream &out, int tab) const = 0;
    
    int getLine() const { return line; }
    int getCol() const { return col; }
};
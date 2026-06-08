#pragma once

#include "../TopLevelNodes/Function.h"
#include "./Statement.h"
#include <memory>
#include <ostream>
#include <utility>

// A function declaration (forward prototype) appearing at block scope, e.g.
// `int foo(void);` inside a function body. It wraps the parsed prototype so the
// enclosing block's statement list can carry it; its visibility is scoped to
// that block.
class FunctionDeclStmt : public Statement
{
  public:
    std::shared_ptr<Function> declaration;

    FunctionDeclStmt(int line_, int col_, std::shared_ptr<Function> declaration_)
        : Statement(NodeKind::FunctionDeclStmt, line_, col_), declaration(std::move(declaration_))
    {
    }

    static bool classof(NodeKind k) { return k == NodeKind::FunctionDeclStmt; }

    void print(std::ostream &out, int tab) const override
    {
        for (int i = 0; i < tab; i++)
        {
            out << "  ";
        }
        if (declaration)
            declaration->print(out, tab);
    }
};

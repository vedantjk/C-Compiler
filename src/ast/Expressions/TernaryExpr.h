#include "Expressions.h"
class TernaryExpr : public Expression
{
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Expression> thenBranch;
    std::shared_ptr<Expression> elseBranch;
    public:
    TernaryExpr(std::shared_ptr<Expression> condition, std::shared_ptr<Expression> ifBranch, std::shared_ptr<Expression> elseBranch, int line, int col) :
    Expression(line, col), condition(std::move(condition)), thenBranch(std::move(ifBranch)), elseBranch(std::move(elseBranch)){}

    void print(std::ostream &out, int tab) const override
    {
        out << "( ";
        condition->print(out, tab);
        out<<" ? ";
        thenBranch->print(out, tab);
        out << " : ";
        elseBranch->print(out, tab);
        out << " )";
    }
};
#pragma once

#include "Expressions.h"
#include "VariableExpr.h"
#include <memory>
#include <ostream>
#include <vector>

class FunctionCallExpr : public Expression
{
  public:
    std::shared_ptr<VariableExpr> functionName;
    std::vector<std::shared_ptr<Expression>> parameters;
    // Set by semantic analysis from the callee's resolved type; codegen uses it to
    // decide whether to set %al (vector-register count) before the call.
    bool calleeVariadic = false;

    FunctionCallExpr(int line_, int col_, std::shared_ptr<VariableExpr> name_,
                     std::vector<std::shared_ptr<Expression>> parameters_)
        : Expression(line_, col_), functionName(std::move(name_)),
          parameters(std::move(parameters_))
    {
    }

    void print(std::ostream &out, int tab) const override
    {
        functionName->print(out, tab);
        out << "(";
        for (int i = 0; i < (int)parameters.size(); i++)
        {
            parameters[i]->print(out, tab);
            if (i != (int)parameters.size() - 1)
                out << ", ";
        }
        out << ")";
    }
};
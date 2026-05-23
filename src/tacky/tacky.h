#pragma once
#include "../ast/ASTNodes/Program.h"
#include "../ast/Expressions/IntLiterals.h"
#include "../ast/Expressions/UnaryExpr.h"
#include "../ast/Statements/BlockStmt.h"
#include "../ast/Statements/ReturnStmt.h"
#include "../ast/TopLevelNodes/Function.h"
#include "../tacky/ast/ASTNodes/TackyProgram.h"
#include "../tacky/ast/TopLevelNodes/TackyFunction.h"
#include "../tacky/instructions/instructions.h"
#include "../tacky/instructions/val.h"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

class TackyDriver
{
    public:

    int tmpCounter = 0;
    std::string makeTemp() { return "tmp." + std::to_string(tmpCounter++); }

    TackyVal processIntLiteral(const std::shared_ptr<IntLiterals>& intLiteral)
    {
        return TackyConstant{intLiteral->value};
    }

    TackyVal processUnaryExpr(const std::shared_ptr<UnaryExpr>& unaryExpr, std::vector<std::unique_ptr<TackyInstruction>>& instructions)
    {
        UnaryOp op;
        if (unaryExpr->op == "~")
        {
            op = UnaryOp::Complement;
        }else if (unaryExpr->op == "-")
        {
            op = UnaryOp::Negate;
        }
        else throw std::runtime_error("unhandled unary op: " + unaryExpr->op);

        TackyVal inner = processExpression(unaryExpr->operand, instructions);
        TackyVar dst{makeTemp()};

        instructions.push_back(std::make_unique<TackyUnary>(unaryExpr->line, unaryExpr->col, op, std::move(inner), dst));

        return dst;
    }

    TackyVal processExpression(const std::shared_ptr<Expression>& expression, std::vector<std::unique_ptr<TackyInstruction>>& instructions)
    {
        if (const auto& p = std::dynamic_pointer_cast<UnaryExpr>(expression))
        {
            return processUnaryExpr(p, instructions);
        }
        if (const auto &p = std::dynamic_pointer_cast<IntLiterals>(expression))
        {
            return processIntLiteral(p);
        }
        throw std::runtime_error("TackyDriver::processExpression: unhandled expression kind");
    }

    void processReturnStmt(const std::shared_ptr<ReturnStmt>& returnStmt, std::vector<std::unique_ptr<TackyInstruction>>& instructions)
    {
        std::optional<TackyVal> returnVal;
        if (returnStmt->returnExpression)
        {
            returnVal = processExpression(returnStmt->returnExpression, instructions);
        }
        instructions.push_back(std::make_unique<TackyReturn>(returnStmt->line, returnStmt->col, std::move(returnVal)));
    }

    void processBlockStmt(const std::shared_ptr<BlockStmt>& blockStmt, std::vector<std::unique_ptr<TackyInstruction>>& instructions)
    {
        for (const auto& stmt : blockStmt->statements)
        {
            if (const auto& p = std::dynamic_pointer_cast<ReturnStmt>(stmt))
            {
                processReturnStmt(p, instructions);
            }
        }
    }

    std::unique_ptr<TackyFunction> processFunction(const std::shared_ptr<Function>& functionNode)
    {
        std::vector<std::unique_ptr<TackyInstruction>> instructions;
        processBlockStmt(functionNode->statements, instructions);
        return std::make_unique<TackyFunction>(functionNode->line, functionNode->col, functionNode->name, std::move(instructions));
    }

    std::unique_ptr<TackyProgram> tacky(const std::shared_ptr<Program>& prog)
    {
        std::vector<std::unique_ptr<TackyTopLevelNode>> nodes;
        for (const auto& node : prog->nodes)
        {
            if (auto p = std::dynamic_pointer_cast<Function>(node))
            {
                if (!p->statements) continue;
                nodes.push_back(processFunction(p));
            }
        }
        return std::make_unique<TackyProgram>(prog->line, prog->col, std::move(nodes));
    }
};

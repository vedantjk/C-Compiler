#pragma once
#include "../ast/ASTNodes/Program.h"
#include "../ast/Expressions/BinaryExpr.h"
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
#include <unordered_map>
#include <vector>

class TackyDriver
{
  public:
    std::unordered_map<std::string, int> tempCounters;

    std::string makeTemp(const std::string &&name)
    {
        tempCounters[name]++;
        return name + std::to_string(tempCounters[name]);
    }

    TackyVal processIntLiteral(const std::shared_ptr<IntLiterals> &intLiteral)
    {
        return TackyConstant{intLiteral->value};
    }

    TackyVal processUnaryExpr(const std::shared_ptr<UnaryExpr> &unaryExpr,
                              std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        UnaryOp op;
        if (unaryExpr->op == "~")
        {
            op = UnaryOp::Complement;
        }
        else if (unaryExpr->op == "-")
        {
            op = UnaryOp::Negate;
        }
        else if (unaryExpr->op == "!")
        {
            op = UnaryOp::Not;
        }
        else
            throw std::runtime_error("unhandled unary op: " + unaryExpr->op);

        TackyVal inner = processExpression(unaryExpr->operand, instructions);
        TackyVar dst{makeTemp("tmp.")};

        instructions.push_back(std::make_unique<TackyUnary>(unaryExpr->line, unaryExpr->col, op,
                                                            std::move(inner), dst));

        return dst;
    }

    TackyVal processLogicalAnd(const std::shared_ptr<BinaryExpr> &binaryExpr,
                               std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        TackyVar result(makeTemp("tmp."));
        std::string false_label = makeTemp("false_label.");
        std::string end_label = makeTemp("end_label.");
        auto lhs = processExpression(binaryExpr->left, instructions);
        instructions.push_back(
            std::make_unique<TackyJumpIfZero>(binaryExpr->line, binaryExpr->col, lhs, false_label));
        auto rhs = processExpression(binaryExpr->right, instructions);
        instructions.push_back(
            std::make_unique<TackyJumpIfZero>(binaryExpr->line, binaryExpr->col, rhs, false_label));
        instructions.push_back(std::make_unique<TackyCopy>(binaryExpr->line, binaryExpr->col,
                                                           TackyConstant("1"), result));
        instructions.push_back(
            std::make_unique<TackyJump>(binaryExpr->line, binaryExpr->col, end_label));
        instructions.push_back(
            std::make_unique<TackyLabel>(binaryExpr->line, binaryExpr->col, false_label));
        instructions.push_back(std::make_unique<TackyCopy>(binaryExpr->line, binaryExpr->col,
                                                           TackyConstant("0"), result));
        instructions.push_back(
            std::make_unique<TackyLabel>(binaryExpr->line, binaryExpr->col, end_label));
        return result;
    }

    TackyVal processLogicalOr(const std::shared_ptr<BinaryExpr> &binaryExpr,
                              std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        TackyVar result(makeTemp("tmp."));
        std::string true_label = makeTemp("true_label.");
        std::string end_label = makeTemp("end_label.");
        auto lhs = processExpression(binaryExpr->left, instructions);
        instructions.push_back(std::make_unique<TackyJumpIfNotZero>(
            binaryExpr->line, binaryExpr->col, lhs, true_label));
        auto rhs = processExpression(binaryExpr->right, instructions);
        instructions.push_back(std::make_unique<TackyJumpIfNotZero>(
            binaryExpr->line, binaryExpr->col, rhs, true_label));
        instructions.push_back(std::make_unique<TackyCopy>(binaryExpr->line, binaryExpr->col,
                                                           TackyConstant("0"), result));
        instructions.push_back(
            std::make_unique<TackyJump>(binaryExpr->line, binaryExpr->col, end_label));
        instructions.push_back(
            std::make_unique<TackyLabel>(binaryExpr->line, binaryExpr->col, true_label));
        instructions.push_back(std::make_unique<TackyCopy>(binaryExpr->line, binaryExpr->col,
                                                           TackyConstant("1"), result));
        instructions.push_back(
            std::make_unique<TackyLabel>(binaryExpr->line, binaryExpr->col, end_label));
        return result;
    }

    TackyVal processBinaryExpr(const std::shared_ptr<BinaryExpr> &binaryExpr,
                               std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        BinaryOp op;
        if (binaryExpr->binaryOp == "+")
        {
            op = BinaryOp::Add;
        }
        else if (binaryExpr->binaryOp == "-")
        {
            op = BinaryOp::Subtract;
        }
        else if (binaryExpr->binaryOp == "*")
        {
            op = BinaryOp::Multiply;
        }
        else if (binaryExpr->binaryOp == "/")
        {
            op = BinaryOp::Divide;
        }
        else if (binaryExpr->binaryOp == "%")
        {
            op = BinaryOp::Remainder;
        }
        else if (binaryExpr->binaryOp == "&")
        {
            op = BinaryOp::BitwiseAnd;
        }
        else if (binaryExpr->binaryOp == "|")
        {
            op = BinaryOp::BitwiseOr;
        }
        else if (binaryExpr->binaryOp == "^")
        {
            op = BinaryOp::BitwiseXor;
        }
        else if (binaryExpr->binaryOp == "<<")
        {
            op = BinaryOp::LeftShift;
        }
        else if (binaryExpr->binaryOp == ">>")
        {
            op = BinaryOp::RightShift;
        }
        else if (binaryExpr->binaryOp == "==")
        {
            op = BinaryOp::Equal;
        }
        else if (binaryExpr->binaryOp == "!=")
        {
            op = BinaryOp::NotEqual;
        }
        else if (binaryExpr->binaryOp == "<")
        {
            op = BinaryOp::LessThan;
        }
        else if (binaryExpr->binaryOp == "<=")
        {
            op = BinaryOp::LessOrEqual;
        }
        else if (binaryExpr->binaryOp == ">")
        {
            op = BinaryOp::GreaterThan;
        }
        else if (binaryExpr->binaryOp == ">=")
        {
            op = BinaryOp::GreaterThanOrEqual;
        }
        else if (binaryExpr->binaryOp == "&&")
        {
            return processLogicalAnd(binaryExpr, instructions);
        }
        else if (binaryExpr->binaryOp == "||")
        {
            return processLogicalOr(binaryExpr, instructions);
        }
        else
            throw std::runtime_error("unhandled binary op: " + binaryExpr->binaryOp);

        auto v1 = processExpression(binaryExpr->left, instructions);
        auto v2 = processExpression(binaryExpr->right, instructions);
        TackyVar dst{makeTemp("tmp.")};

        instructions.push_back(std::make_unique<TackyBinary>(binaryExpr->line, binaryExpr->col, op,
                                                             std::move(v1), std::move(v2), dst));

        return dst;
    }

    TackyVal processVariableExpr(const std::shared_ptr<VariableExpr> &variableExpr,
                                 std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        return TackyVar(variableExpr->name);
    }

    TackyVal processAssignExpr(const std::shared_ptr<AssignExpr> &assignExpr,
                               std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        auto lhs = processExpression(assignExpr->lhs, instructions);
        auto rhs = processExpression(assignExpr->rhs, instructions);
        instructions.push_back(
            std::make_unique<TackyCopy>(assignExpr->line, assignExpr->col, rhs, lhs));
        return lhs;
    }

    TackyVal processExpression(const std::shared_ptr<Expression> &expression,
                               std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        if (const auto &p = std::dynamic_pointer_cast<UnaryExpr>(expression))
        {
            return processUnaryExpr(p, instructions);
        }
        if (const auto &p = std::dynamic_pointer_cast<IntLiterals>(expression))
        {
            return processIntLiteral(p);
        }
        if (const auto &p = std::dynamic_pointer_cast<BinaryExpr>(expression))
        {
            return processBinaryExpr(p, instructions);
        }
        if (const auto &p = std::dynamic_pointer_cast<VariableExpr>(expression))
        {
            return processVariableExpr(p, instructions);
        }
        if (const auto &p = std::dynamic_pointer_cast<AssignExpr>(expression))
        {
            return processAssignExpr(p, instructions);
        }
        throw std::runtime_error("TackyDriver::processExpression: unhandled expression kind");
    }

    TackyVal processVarDecl(const std::shared_ptr<VarDecl> &varDecl,
                            std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        return TackyVar(varDecl->name);
    }

    void processReturnStmt(const std::shared_ptr<ReturnStmt> &returnStmt,
                           std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        std::optional<TackyVal> returnVal;
        if (returnStmt->returnExpression)
        {
            returnVal = processExpression(returnStmt->returnExpression, instructions);
        }
        instructions.push_back(
            std::make_unique<TackyReturn>(returnStmt->line, returnStmt->col, std::move(returnVal)));
    }

    void processDeclareStmt(const std::shared_ptr<DeclareStmt> &declareStmt,
                            std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        if (const auto *vars =
                std::get_if<std::vector<std::shared_ptr<VarDecl>>>(&declareStmt->variables))
        {
            for (auto &vd : *vars)
            {
                auto var = processVarDecl(vd, instructions);
                if (vd->initialization)
                {
                    auto val = processExpression(vd->initialization, instructions);
                    instructions.push_back(
                        std::make_unique<TackyCopy>(declareStmt->line, declareStmt->col, val, var));
                }
            }
        }
        else
        {
            // TODO
        }
    }

    void processExprStmt(const std::shared_ptr<ExprStmt> &exprStmt,
                         std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        processExpression(exprStmt->expr, instructions);
    }

    void processBlockStmt(const std::shared_ptr<BlockStmt> &blockStmt,
                          std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        for (const auto &stmt : blockStmt->statements)
        {
            if (const auto &p = std::dynamic_pointer_cast<ReturnStmt>(stmt))
            {
                processReturnStmt(p, instructions);
            }
            else if (const auto &p = std::dynamic_pointer_cast<DeclareStmt>(stmt))
            {
                processDeclareStmt(p, instructions);
            }
            else if (const auto &p = std::dynamic_pointer_cast<ExprStmt>(stmt))
            {
                processExprStmt(p, instructions);
            }
        }
    }

    std::unique_ptr<TackyFunction> processFunction(const std::shared_ptr<Function> &functionNode)
    {
        std::vector<std::unique_ptr<TackyInstruction>> instructions;
        processBlockStmt(functionNode->statements, instructions);
        return std::make_unique<TackyFunction>(functionNode->line, functionNode->col,
                                               functionNode->name, std::move(instructions));
    }

    std::unique_ptr<TackyProgram> tacky(const std::shared_ptr<Program> &prog)
    {
        std::vector<std::unique_ptr<TackyTopLevelNode>> nodes;
        for (const auto &node : prog->nodes)
        {
            if (auto p = std::dynamic_pointer_cast<Function>(node))
            {
                if (!p->statements)
                    continue;
                nodes.push_back(processFunction(p));
            }
        }
        return std::make_unique<TackyProgram>(prog->line, prog->col, std::move(nodes));
    }
};

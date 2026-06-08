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
#include "ast/TopLevelNodes/TackyStaticVariable.h"

#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

class TackyDriver
{
    ConstantType returnConstantType(const std::shared_ptr<Type> &literalType)
    {
        if (std::dynamic_pointer_cast<UnsignedLongType>(literalType))
            return ConstantType::ULONG;
        if (std::dynamic_pointer_cast<UnsignedIntType>(literalType))
            return ConstantType::UINT;
        if (std::dynamic_pointer_cast<LongType>(literalType))
            return ConstantType::LONG;
        if (std::dynamic_pointer_cast<DoubleType>(literalType))
            return ConstantType::DOUBLE;
        if (std::dynamic_pointer_cast<PointerType>(literalType))
            return ConstantType::POINTER;
        return ConstantType::INT;
    }

    static bool isUnsignedType(const std::shared_ptr<Type> &t)
    {
        return std::dynamic_pointer_cast<UnsignedIntType>(t) != nullptr ||
               std::dynamic_pointer_cast<UnsignedLongType>(t) != nullptr;
    }

  public:
    std::unordered_map<std::string, int> tempCounters;
    std::set<Symbol *> seenVarDecls;
    std::string makeTemp(const std::string &&name)
    {
        tempCounters[name]++;
        return "." + name + std::to_string(tempCounters[name]);
    }

    ExpResult processLvalue(const std::shared_ptr<Expression> &e,
                            std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        if (auto u = std::dynamic_pointer_cast<UnaryExpr>(e); u && u->op == "*")
            return DereferencedPointer{processExpression(u->operand, instructions)};
        // plain variable (or anything else that's a simple operand)
        return processExpression(e, instructions);
    }

    TackyVal processFloatingLiteral(const std::shared_ptr<FloatingLiterals> &floatLiteral)
    {
        return TackyFloatingConstant{floatLiteral->value, returnConstantType(floatLiteral->type)};
    }

    TackyVal processIntLiteral(const std::shared_ptr<IntLiterals> &intLiteral)
    {
        return TackyConstant{intLiteral->value, returnConstantType(intLiteral->type)};
    }

    TackyVal processIncrementDecrement(const std::shared_ptr<UnaryExpr> &unaryExpr,
                                       std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        if (unaryExpr->op == "++")
        {
            if (unaryExpr->isPostFix)
            {
                auto operand = processExpression(unaryExpr->operand, instructions);
                auto copyVar =
                    TackyVar(makeTemp("tmp."), returnConstantType(unaryExpr->resolvedType));
                instructions.push_back(
                    std::make_unique<TackyCopy>(unaryExpr->line, unaryExpr->col, operand, copyVar));
                instructions.push_back(std::make_unique<TackyBinary>(
                    unaryExpr->line, unaryExpr->col, BinaryOp::Add, operand,
                    TackyConstant(1, ConstantType::INT), operand));
                return copyVar;
            }
            auto operand = processExpression(unaryExpr->operand, instructions);
            instructions.push_back(std::make_unique<TackyBinary>(
                unaryExpr->line, unaryExpr->col, BinaryOp::Add, operand,
                TackyConstant(1, ConstantType::INT), operand));
            return operand;
        }
        if (unaryExpr->isPostFix)
        {
            auto operand = processExpression(unaryExpr->operand, instructions);
            auto copyVar = TackyVar(makeTemp("tmp."), returnConstantType(unaryExpr->resolvedType));
            instructions.push_back(
                std::make_unique<TackyCopy>(unaryExpr->line, unaryExpr->col, operand, copyVar));
            instructions.push_back(std::make_unique<TackyBinary>(
                unaryExpr->line, unaryExpr->col, BinaryOp::Subtract, operand,
                TackyConstant(1, ConstantType::INT), operand));
            return copyVar;
        }
        auto operand = processExpression(unaryExpr->operand, instructions);
        instructions.push_back(
            std::make_unique<TackyBinary>(unaryExpr->line, unaryExpr->col, BinaryOp::Subtract,
                                          operand, TackyConstant(1, ConstantType::INT), operand));
        return operand;
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
        else if (unaryExpr->op == "++" || unaryExpr->op == "--")
        {
            return processIncrementDecrement(unaryExpr, instructions);
        }
        else if (unaryExpr->op == "*")
        {
            auto ptr = processExpression(unaryExpr->operand, instructions); // the pointer
            TackyVar dst{makeTemp("tmp."), returnConstantType(unaryExpr->resolvedType)};
            instructions.push_back(
                std::make_unique<TackyLoad>(unaryExpr->line, unaryExpr->col, ptr, dst));
            return dst;
        }
        else if (unaryExpr->op == "&")
        {
            auto inner = processLvalue(unaryExpr->operand, instructions);

            if (auto *dp = std::get_if<DereferencedPointer>(&inner))
            {
                return dp->ptr;
            }

            auto *operand = std::get_if<TackyVal>(&inner);
            TackyVar dst{makeTemp("tmp."), returnConstantType(unaryExpr->resolvedType)};
            instructions.push_back(
                std::make_unique<TackyGetAddress>(unaryExpr->line, unaryExpr->col, *operand, dst));
            return dst;
        }
        else
            throw std::runtime_error("unhandled unary op: " + unaryExpr->op);

        TackyVal inner = processExpression(unaryExpr->operand, instructions);
        TackyVar dst{makeTemp("tmp."), returnConstantType(unaryExpr->resolvedType)};

        instructions.push_back(std::make_unique<TackyUnary>(unaryExpr->line, unaryExpr->col, op,
                                                            std::move(inner), dst));

        return dst;
    }

    TackyVal processLogicalAnd(const std::shared_ptr<BinaryExpr> &binaryExpr,
                               std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        TackyVar result(makeTemp("tmp."), returnConstantType(binaryExpr->resolvedType));
        std::string false_label = makeTemp("false_label.");
        std::string end_label = makeTemp("end_label.");
        auto lhs = processExpression(binaryExpr->left, instructions);
        instructions.push_back(
            std::make_unique<TackyJumpIfZero>(binaryExpr->line, binaryExpr->col, lhs, false_label));
        auto rhs = processExpression(binaryExpr->right, instructions);
        instructions.push_back(
            std::make_unique<TackyJumpIfZero>(binaryExpr->line, binaryExpr->col, rhs, false_label));
        instructions.push_back(std::make_unique<TackyCopy>(
            binaryExpr->line, binaryExpr->col, TackyConstant(1, ConstantType::INT), result));
        instructions.push_back(
            std::make_unique<TackyJump>(binaryExpr->line, binaryExpr->col, end_label));
        instructions.push_back(
            std::make_unique<TackyLabel>(binaryExpr->line, binaryExpr->col, false_label));
        instructions.push_back(std::make_unique<TackyCopy>(
            binaryExpr->line, binaryExpr->col, TackyConstant(0, ConstantType::INT), result));
        instructions.push_back(
            std::make_unique<TackyLabel>(binaryExpr->line, binaryExpr->col, end_label));
        return result;
    }

    TackyVal processLogicalOr(const std::shared_ptr<BinaryExpr> &binaryExpr,
                              std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        TackyVar result(makeTemp("tmp."), returnConstantType(binaryExpr->resolvedType));
        std::string true_label = makeTemp("true_label.");
        std::string end_label = makeTemp("end_label.");
        auto lhs = processExpression(binaryExpr->left, instructions);
        instructions.push_back(std::make_unique<TackyJumpIfNotZero>(
            binaryExpr->line, binaryExpr->col, lhs, true_label));
        auto rhs = processExpression(binaryExpr->right, instructions);
        instructions.push_back(std::make_unique<TackyJumpIfNotZero>(
            binaryExpr->line, binaryExpr->col, rhs, true_label));
        instructions.push_back(std::make_unique<TackyCopy>(
            binaryExpr->line, binaryExpr->col, TackyConstant(0, ConstantType::INT), result));
        instructions.push_back(
            std::make_unique<TackyJump>(binaryExpr->line, binaryExpr->col, end_label));
        instructions.push_back(
            std::make_unique<TackyLabel>(binaryExpr->line, binaryExpr->col, true_label));
        instructions.push_back(std::make_unique<TackyCopy>(
            binaryExpr->line, binaryExpr->col, TackyConstant(1, ConstantType::INT), result));
        instructions.push_back(
            std::make_unique<TackyLabel>(binaryExpr->line, binaryExpr->col, end_label));
        return result;
    }

    // Maps an operator spelling to its BinaryOp. Handles plain operators, their
    // compound-assignment forms (e.g. "+=" → Add), and relational operators.
    static BinaryOp stringToBinaryOp(const std::string &op)
    {
        static const std::unordered_map<std::string, BinaryOp> table = {
            {"+", BinaryOp::Add},         {"+=", BinaryOp::Add},
            {"-", BinaryOp::Subtract},    {"-=", BinaryOp::Subtract},
            {"*", BinaryOp::Multiply},    {"*=", BinaryOp::Multiply},
            {"/", BinaryOp::Divide},      {"/=", BinaryOp::Divide},
            {"%", BinaryOp::Remainder},   {"%=", BinaryOp::Remainder},
            {"&", BinaryOp::BitwiseAnd},  {"&=", BinaryOp::BitwiseAnd},
            {"|", BinaryOp::BitwiseOr},   {"|=", BinaryOp::BitwiseOr},
            {"^", BinaryOp::BitwiseXor},  {"^=", BinaryOp::BitwiseXor},
            {"<<", BinaryOp::LeftShift},  {"<<=", BinaryOp::LeftShift},
            {">>", BinaryOp::RightShift}, {">>=", BinaryOp::RightShift},
            {"==", BinaryOp::Equal},      {"!=", BinaryOp::NotEqual},
            {"<", BinaryOp::LessThan},    {"<=", BinaryOp::LessOrEqual},
            {">", BinaryOp::GreaterThan}, {">=", BinaryOp::GreaterThanOrEqual},
        };
        auto it = table.find(op);
        if (it == table.end())
            throw std::runtime_error("stringToBinaryOp: unknown operator '" + op + "'");
        return it->second;
    }

    TackyVal processBinaryExpr(const std::shared_ptr<BinaryExpr> &binaryExpr,
                               std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        if (binaryExpr->binaryOp == "&&")
            return processLogicalAnd(binaryExpr, instructions);
        if (binaryExpr->binaryOp == "||")
            return processLogicalOr(binaryExpr, instructions);

        BinaryOp op = stringToBinaryOp(binaryExpr->binaryOp);

        auto v1 = processExpression(binaryExpr->left, instructions);
        auto v2 = processExpression(binaryExpr->right, instructions);
        TackyVar dst{makeTemp("tmp."), returnConstantType(binaryExpr->resolvedType)};

        instructions.push_back(std::make_unique<TackyBinary>(binaryExpr->line, binaryExpr->col, op,
                                                             std::move(v1), std::move(v2), dst));

        return dst;
    }

    TackyVal processVariableExpr(const std::shared_ptr<VariableExpr> &variableExpr,
                                 std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        return TackyVar(variableExpr->symbol ? variableExpr->symbol->uniqueName
                                             : variableExpr->name,
                        returnConstantType(variableExpr->resolvedType));
    }

    TackyVal processAssignExpr(const std::shared_ptr<AssignExpr> &assignExpr,
                               std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        auto lhs = processLvalue(assignExpr->lhs, instructions);
        auto rhs = processExpression(assignExpr->rhs, instructions);

        if (auto *v = std::get_if<TackyVal>(&lhs))
        {
            if (assignExpr->op != "=")
            {
                BinaryOp op = stringToBinaryOp(assignExpr->op);
                instructions.push_back(std::make_unique<TackyBinary>(
                    assignExpr->line, assignExpr->col, op, *v, rhs, *v));
            }
            else
            {
                instructions.push_back(
                    std::make_unique<TackyCopy>(assignExpr->line, assignExpr->col, rhs, *v));
            }
            return *v;
        }
        if (auto *v = std::get_if<DereferencedPointer>(&lhs))
        {
            TackyVal stored = rhs;
            if (assignExpr->op != "=")
            {
                // *p OP= rhs  ->  load *p, combine, store back
                ConstantType ct = returnConstantType(assignExpr->lhs->resolvedType); // type of *p
                TackyVar cur{makeTemp("tmp."), ct};
                instructions.push_back(
                    std::make_unique<TackyLoad>(assignExpr->line, assignExpr->col, v->ptr, cur));
                TackyVar res{makeTemp("tmp."), ct};
                BinaryOp op = stringToBinaryOp(assignExpr->op);
                instructions.push_back(std::make_unique<TackyBinary>(
                    assignExpr->line, assignExpr->col, op, cur, rhs, res));
                stored = res;
            }
            instructions.push_back(
                std::make_unique<TackyStore>(assignExpr->line, assignExpr->col, stored, v->ptr));
            return stored;
        }
        return rhs;
    }

    TackyVal processTernaryExpr(const std::shared_ptr<TernaryExpr> &ternaryExpr,
                                std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        TackyVar temp = TackyVar(makeTemp("tmp."), returnConstantType(ternaryExpr->resolvedType));
        std::string false_label = makeTemp("ternary_false.");
        std::string end_label = makeTemp("end.");
        auto condition = processExpression(ternaryExpr->condition, instructions);
        instructions.push_back(std::make_unique<TackyJumpIfZero>(
            ternaryExpr->line, ternaryExpr->col, condition, false_label));
        auto trueExpr = processExpression(ternaryExpr->thenBranch, instructions);
        instructions.push_back(
            std::make_unique<TackyCopy>(ternaryExpr->line, ternaryExpr->col, trueExpr, temp));
        instructions.push_back(
            std::make_unique<TackyJump>(ternaryExpr->line, ternaryExpr->col, end_label));
        instructions.push_back(
            std::make_unique<TackyLabel>(ternaryExpr->line, ternaryExpr->col, false_label));
        auto falseExpr = processExpression(ternaryExpr->elseBranch, instructions);
        instructions.push_back(
            std::make_unique<TackyCopy>(ternaryExpr->line, ternaryExpr->col, falseExpr, temp));
        instructions.push_back(
            std::make_unique<TackyLabel>(ternaryExpr->line, ternaryExpr->col, end_label));
        return temp;
    }

    TackyVal processFunctionCallExpr(const std::shared_ptr<FunctionCallExpr> &functionCallExpr,
                                     std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        std::vector<TackyVal> args;
        for (const auto &param : functionCallExpr->parameters)
        {
            args.push_back(processExpression(param, instructions));
        }
        TackyVar dst{makeTemp("tmp."), returnConstantType(functionCallExpr->resolvedType)};
        instructions.push_back(std::make_unique<TackyFunctionCall>(
            functionCallExpr->line, functionCallExpr->col, functionCallExpr->functionName->name,
            args, dst, functionCallExpr->calleeVariadic));
        return dst;
    }

    TackyVal processCastExpr(const std::shared_ptr<CastExpr> &castExpr,
                             std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        auto result = processExpression(castExpr->operand, instructions);
        const auto &srcType = castExpr->operand->resolvedType;
        const auto &dstType = castExpr->type;

        const ConstantType srcCt = returnConstantType(srcType);
        const ConstantType dstCt = returnConstantType(dstType);
        if (isDouble(srcCt) && isIntCt(dstCt))
        {
            TackyVar intDst{makeTemp("tmp."), dstCt};
            instructions.push_back(
                std::make_unique<TackyDoubleToInt>(castExpr->line, castExpr->col, result, intDst));
            return intDst;
        }
        if (isDouble(srcCt) && isUnsignedCt(dstCt))
        {
            TackyVar uIntDst{makeTemp("tmp."), dstCt};
            instructions.push_back(std::make_unique<TackyDoubleToUInt>(
                castExpr->line, castExpr->col, result, uIntDst));
            return uIntDst;
        }
        if (isIntCt(srcCt) && isDouble(dstCt))
        {
            TackyVar doubleCt{makeTemp("tmp."), dstCt};
            instructions.push_back(std::make_unique<TackyIntToDouble>(castExpr->line, castExpr->col,
                                                                      result, doubleCt));
            return doubleCt;
        }
        if (isUnsignedCt(srcCt) && isDouble(dstCt))
        {
            TackyVar doubleCt{makeTemp("tmp."), dstCt};
            instructions.push_back(std::make_unique<TackyUIntToDouble>(
                castExpr->line, castExpr->col, result, doubleCt));
            return doubleCt;
        }

        // Same width: the bit pattern is unchanged. Identical types are a no-op,
        // but a signedness-only change (int<->uint, long<->ulong) must still copy
        // into a temp of the destination type so the value carries the new
        // signedness for later operations (e.g. picking signed vs unsigned setcc).
        if (ctBytes(srcCt) == ctBytes(dstCt))
        {
            if (srcCt == dstCt)
                return result;
            TackyVar sameWidthDst{makeTemp("tmp."), dstCt};
            instructions.push_back(
                std::make_unique<TackyCopy>(castExpr->line, castExpr->col, result, sameWidthDst));
            return sameWidthDst;
        }

        TackyVar dst{makeTemp("tmp."), dstCt};

        if (ctBytes(dstCt) < ctBytes(srcCt))
        {
            // Narrowing 8 -> 4: keep the low 32 bits.
            instructions.push_back(
                std::make_unique<TackyTruncate>(castExpr->line, castExpr->col, result, dst));
        }
        else if (isUnsignedType(srcType))
        {
            // Widening from an unsigned source: zero-fill the upper bits.
            instructions.push_back(
                std::make_unique<TackyZeroExtend>(castExpr->line, castExpr->col, result, dst));
        }
        else
        {
            // Widening from a signed source: replicate the sign bit.
            instructions.push_back(
                std::make_unique<TackySignExtend>(castExpr->line, castExpr->col, result, dst));
        }
        return dst;
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
        if (const auto &p = std::dynamic_pointer_cast<FloatingLiterals>(expression))
        {
            return processFloatingLiteral(p);
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
        if (const auto &p = std::dynamic_pointer_cast<TernaryExpr>(expression))
        {
            return processTernaryExpr(p, instructions);
        }
        if (const auto &p = std::dynamic_pointer_cast<FunctionCallExpr>(expression))
        {
            return processFunctionCallExpr(p, instructions);
        }
        if (const auto &p = std::dynamic_pointer_cast<CastExpr>(expression))
        {
            return processCastExpr(p, instructions);
        }
        throw std::runtime_error("TackyDriver::processExpression: unhandled expression kind");
    }

    TackyVal processVarDecl(const std::shared_ptr<VarDecl> &varDecl,
                            std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        return TackyVar(varDecl->symbol ? varDecl->symbol->uniqueName : varDecl->name,
                        returnConstantType(varDecl->type));
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
                if (vd->symbol->duration == StorageDuration::Static)
                {
                    seenVarDecls.insert(vd->symbol.get());
                }
                else if (vd->symbol->linkage == Linkage::External &&
                         vd->symbol->duration == StorageDuration::Automatic)
                {
                }
                else
                {
                    auto var = processVarDecl(vd, instructions);
                    if (vd->initialization)
                    {
                        auto val = processExpression(vd->initialization, instructions);
                        instructions.push_back(std::make_unique<TackyCopy>(
                            declareStmt->line, declareStmt->col, val, var));
                    }
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

    void processIfStmt(const std::shared_ptr<IfStmt> &ifStmt,
                       std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        std::string end_label = makeTemp("end.");

        auto condition = processExpression(ifStmt->condition, instructions);
        if (ifStmt->elseBlock)
        {
            std::string else_label = makeTemp("else.");
            instructions.push_back(std::make_unique<TackyJumpIfZero>(ifStmt->line, ifStmt->col,
                                                                     condition, else_label));
            processBlockStmt(ifStmt->thenBlock, instructions);
            instructions.push_back(
                std::make_unique<TackyJump>(ifStmt->line, ifStmt->col, end_label));
            instructions.push_back(
                std::make_unique<TackyLabel>(ifStmt->line, ifStmt->col, else_label));
            processBlockStmt(ifStmt->elseBlock, instructions);
        }
        else
        {
            instructions.push_back(
                std::make_unique<TackyJumpIfZero>(ifStmt->line, ifStmt->col, condition, end_label));
            processBlockStmt(ifStmt->thenBlock, instructions);
        }
        instructions.push_back(std::make_unique<TackyLabel>(ifStmt->line, ifStmt->col, end_label));
    }

    void processBreakStmt(const std::shared_ptr<BreakStmt> &breakStmt,
                          std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        instructions.push_back(std::make_unique<TackyJump>(
            breakStmt->line, breakStmt->col, ".break_label" + std::to_string(breakStmt->label)));
    }

    void processContinueStmt(const std::shared_ptr<ContinueStmt> &continueStmt,
                             std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        instructions.push_back(
            std::make_unique<TackyJump>(continueStmt->line, continueStmt->col,
                                        ".continue_label" + std::to_string(continueStmt->label)));
    }

    void processDoWhileStmt(const std::shared_ptr<DoWhileStmt> &doWhileStmt,
                            std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        std::string labelId = std::to_string(doWhileStmt->label);
        instructions.push_back(std::make_unique<TackyLabel>(doWhileStmt->line, doWhileStmt->col,
                                                            ".start_label" + labelId));
        processBlockStmt(doWhileStmt->block, instructions);
        instructions.push_back(std::make_unique<TackyLabel>(doWhileStmt->line, doWhileStmt->col,
                                                            ".continue_label" + labelId));
        auto result = processExpression(doWhileStmt->condition, instructions);
        instructions.push_back(std::make_unique<TackyJumpIfNotZero>(
            doWhileStmt->line, doWhileStmt->col, result, ".start_label" + labelId));
        instructions.push_back(std::make_unique<TackyLabel>(doWhileStmt->line, doWhileStmt->col,
                                                            ".break_label" + labelId));
    }

    void processWhileStmt(const std::shared_ptr<WhileStmt> &whileStmt,
                          std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        std::string labelId = std::to_string(whileStmt->label);
        instructions.push_back(std::make_unique<TackyLabel>(whileStmt->line, whileStmt->col,
                                                            ".continue_label" + labelId));
        auto result = processExpression(whileStmt->condition, instructions);
        instructions.push_back(std::make_unique<TackyJumpIfZero>(whileStmt->line, whileStmt->col,
                                                                 result, ".break_label" + labelId));
        processBlockStmt(whileStmt->whileBlock, instructions);
        instructions.push_back(std::make_unique<TackyJump>(whileStmt->line, whileStmt->col,
                                                           ".continue_label" + labelId));
        instructions.push_back(std::make_unique<TackyLabel>(whileStmt->line, whileStmt->col,
                                                            ".break_label" + labelId));
    }

    void processForStmt(const std::shared_ptr<ForStmt> &forStmt,
                        std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        std::string labelId = std::to_string(forStmt->label);

        if (forStmt->initialization)
        {
            processStatement(forStmt->initialization, instructions);
        }
        instructions.push_back(
            std::make_unique<TackyLabel>(forStmt->line, forStmt->col, ".start_label" + labelId));
        if (forStmt->condition)
        {
            auto condStmt = std::dynamic_pointer_cast<ExprStmt>(forStmt->condition);
            auto condVal = processExpression(condStmt->expr, instructions);
            instructions.push_back(std::make_unique<TackyJumpIfZero>(
                forStmt->line, forStmt->col, condVal, ".break_label" + labelId));
        }
        if (forStmt->forBlock)
        {
            processBlockStmt(forStmt->forBlock, instructions);
        }
        instructions.push_back(
            std::make_unique<TackyLabel>(forStmt->line, forStmt->col, ".continue_label" + labelId));
        if (forStmt->update)
        {
            processStatement(forStmt->update, instructions);
        }
        instructions.push_back(
            std::make_unique<TackyJump>(forStmt->line, forStmt->col, ".start_label" + labelId));
        instructions.push_back(
            std::make_unique<TackyLabel>(forStmt->line, forStmt->col, ".break_label" + labelId));
    }

    void processStatement(const std::shared_ptr<Statement> &stmt,
                          std::vector<std::unique_ptr<TackyInstruction>> &instructions)
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
        else if (const auto &p = std::dynamic_pointer_cast<IfStmt>(stmt))
        {
            processIfStmt(p, instructions);
        }
        else if (const auto &p = std::dynamic_pointer_cast<BlockStmt>(stmt))
        {
            processBlockStmt(p, instructions);
        }
        else if (const auto &p = std::dynamic_pointer_cast<WhileStmt>(stmt))
        {
            processWhileStmt(p, instructions);
        }
        else if (const auto &p = std::dynamic_pointer_cast<DoWhileStmt>(stmt))
        {
            processDoWhileStmt(p, instructions);
        }
        else if (const auto &p = std::dynamic_pointer_cast<ForStmt>(stmt))
        {
            processForStmt(p, instructions);
        }
        else if (const auto &p = std::dynamic_pointer_cast<ContinueStmt>(stmt))
        {
            processContinueStmt(p, instructions);
        }
        else if (const auto &p = std::dynamic_pointer_cast<BreakStmt>(stmt))
        {
            processBreakStmt(p, instructions);
        }
    }

    void processBlockStmt(const std::shared_ptr<BlockStmt> &blockStmt,
                          std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        for (const auto &stmt : blockStmt->statements)
        {
            processStatement(stmt, instructions);
        }
    }

    std::unique_ptr<TackyFunction> processFunction(const std::shared_ptr<Function> &functionNode)
    {
        std::vector<std::unique_ptr<TackyInstruction>> instructions;
        std::vector<std::pair<std::string, ConstantType>> params;
        for (const auto &param : functionNode->parameters)
        {
            params.push_back({param.name, returnConstantType(param.type)});
        }
        processBlockStmt(functionNode->statements, instructions);
        // Every function gets an implicit `return 0` appended. If control reaches
        // it, no earlier return fired (e.g. the body falls off the end); if an
        // earlier return already fired, this is dead code after a ret. Either way
        // it guarantees the function ends in a return so codegen emits a final ret.
        instructions.push_back(std::make_unique<TackyReturn>(functionNode->line, functionNode->col,
                                                             TackyConstant(0, ConstantType::INT)));
        bool global = functionNode->symbol && functionNode->symbol->linkage == Linkage::External;
        return std::make_unique<TackyFunction>(functionNode->line, functionNode->col,
                                               functionNode->name, global, std::move(instructions),
                                               params);
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
            else if (auto p = std::dynamic_pointer_cast<VarDecl>(node))
            {
                seenVarDecls.insert(p->symbol.get());
            }
        }

        for (const auto &symbol : seenVarDecls)
        {
            if (!symbol->defined && !symbol->tentative)
                continue;
            const ConstantType ct = returnConstantType(symbol->type);
            std::variant<long long, double> init =
                isDouble(ct)
                    ? std::variant<long long, double>{symbol->constInitDouble.value_or(0.0)}
                    : std::variant<long long, double>{symbol->constInit.value_or(0LL)};
            nodes.push_back(std::make_unique<TackyStaticVariable>(
                symbol->line, symbol->column, symbol->uniqueName,
                symbol->linkage == Linkage::External, init, ct));
        }
        auto program = std::make_unique<TackyProgram>(prog->line, prog->col, std::move(nodes));
        // Record every static-storage name (incl. extern-only declarations) so
        // codegen resolves their references to RIP-relative data, not stack slots.
        for (const auto &symbol : seenVarDecls)
            program->staticNames.insert(symbol->uniqueName);
        return program;
    }
};

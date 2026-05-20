#include "ast/ASTNodes/Program.h"
#include "ast/ASTNodes/codegenProgram.h"
#include "ast/Expressions/IntLiterals.h"
#include "ast/Statements/ReturnStmt.h"
#include "ast/TopLevelNodes/Function.h"
#include "ast/TopLevelNodes/codegenFunction.h"

#include <memory>
class codegenDriver
{
    public:

    std::unique_ptr<Operand> processIntLiteral(std::shared_ptr<IntLiterals> intLiteral, std::vector<std::unique_ptr<Instruction>>& instructions)
    {
        auto immediate = std::make_unique<Immediate>(intLiteral->value);
        return immediate;
    }

    std::unique_ptr<Operand> processExpression(std::shared_ptr<Expression> expression, std::vector<std::unique_ptr<Instruction>>& instructions)
    {
        if (auto p = std::dynamic_pointer_cast<IntLiterals>(expression))
        {
            return processIntLiteral(p, instructions);
        }
        return nullptr;
    }

    void processReturnStmt(std::shared_ptr<ReturnStmt> returnStmt, std::vector<std::unique_ptr<Instruction>>& instructions)
    {
        if (returnStmt->returnExpression)
        {
            auto source = processExpression(returnStmt->returnExpression, instructions);
            auto destination = std::make_unique<Register>();
            instructions.push_back(std::make_unique<MoveInstruction>(std::move(source), std::move(destination)));
        }
        instructions.push_back(std::make_unique<ReturnInstruction>());
    }

    void processBlockStmt(std::shared_ptr<BlockStmt> blockStmt, std::vector<std::unique_ptr<Instruction>>& instructions)
    {
        for (auto& stmt : blockStmt->statements)
        {
            if (auto p = std::dynamic_pointer_cast<ReturnStmt>(stmt))
            {
                processReturnStmt(p, instructions);
            }
        }
    }

    std::unique_ptr<codegenFunction> processFunction(std::shared_ptr<Function> functionNode)
    {
        std::vector<std::unique_ptr<Instruction>> instructions;
        processBlockStmt(functionNode->statements, instructions);
        return std::make_unique<codegenFunction>(functionNode->line, functionNode->col, functionNode->name, std::move(instructions));
    }

    std::unique_ptr<codegenProgram> codegen(std::shared_ptr<Program>& prog)
    {
        std::vector<std::unique_ptr<codegenTopLevelNode>> nodes;
        for (const auto& node : prog->nodes)
        {
            if (auto p = std::dynamic_pointer_cast<Function>(node))
            {
                if (!p->statements) continue;
                nodes.push_back(processFunction(p));
            }
        }

        return std::make_unique<codegenProgram>(prog->line, prog->col, std::move(nodes));
    }
};
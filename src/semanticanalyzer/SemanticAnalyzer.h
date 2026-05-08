#pragma once
#include "../ast/ASTNodes/ASTNode.h"
#include "../ast/ASTNodes/Program.h"
#include "../ast/Expressions/FunctionCallExpr.h"
#include "../ast/Expressions/IntLiterals.h"
#include "../ast/Statements/DeclareStmt.h"
#include "../ast/Statements/ExprStmt.h"
#include "../ast/TopLevelNodes/Function.h"
#include "../ast/TopLevelNodes/StructDecl.h"
#include "../ast/TopLevelNodes/VarDecl.h"
#include "../symboltable/SymbolTable.h"
#include "../ast/Statements/ReturnStmt.h"

class SemanticAnalyzer
{
    SymbolTable symbolTable;
    std::shared_ptr<Type> currentReturnType;
    public:
    SemanticAnalyzer() = default;

    void check(const std::string &name, const std::shared_ptr<Symbol> &symbol, const Kind kind,
               const int line, const int col, const std::shared_ptr<ASTNode>& node)
    {
        if (!symbolTable.insert(name, symbol, kind))
        {
            const auto existing = symbolTable.find(name, kind);
            std::cerr << "Semantic error at line " << line<<", col "<<col <<": redeclaration of "<< kindToString(kind) <<" '"<< name <<"'. Previous declaration ("<< kindToString(existing->kind) <<") at line "<<existing->line<<".\n";
            return;
        }
        node->symbol = symbol;
        symbol->node = node;
    }

    void analyzeExpr(const std::shared_ptr<Expression>& expr) const
    {
        if (auto x = std::dynamic_pointer_cast<IntLiterals>(expr))
        {
            x->resolvedType = IntType::getInstance();
            x->isLvalue = false;
            return;
        }
        // TODO: other expression kinds
    }


    void analyzeFunctionCallExpr(const std::shared_ptr<FunctionCallExpr> &expr) const
    {
        std::string functionName = expr->functionName->name;
        const auto checkFunctionExistence = symbolTable.find(functionName, Kind::FUNCTION);
        if (checkFunctionExistence == nullptr)
        {
            std::cerr << "Semantic error at line "<<expr->getLine()<<", col "<<expr->getCol()<<": function "<<functionName<<" not declared\n";
        }else
        {
            auto functionType = std::dynamic_pointer_cast<FunctionType>(checkFunctionExistence->type);
            if (functionType->paramTypes.size() != expr->parameters.size())
            {
                std::cerr << "Semantic error at line "<<expr->getLine()<<", col "<<expr->getCol()<<": function call has " << expr->parameters.size() << " parameters. Expected " << functionType->paramTypes.size() << "\n";
            }else
            {
                for (int i = 0; i < expr->parameters.size(); i++)
                {
                    analyzeExpr(expr->parameters[i]);
                    if (functionType->paramTypes[i] != expr->parameters[i]->resolvedType)
                    {
                        std::cerr <<"Semantic error at line "<<expr->getLine()<<", col "<<expr->getCol()<<": mismatched param types, expected " <<functionType->paramTypes[i]->toString() << " got " << expr->parameters[i]->resolvedType->toString() << "\n";
                    }
                }
            }
        }
    }

    void analyzeExprStmt(const std::shared_ptr<ExprStmt> &exprStmt) const
    {
        if (auto x = std::dynamic_pointer_cast<FunctionCallExpr>(exprStmt->expr))
        {
            analyzeFunctionCallExpr(x);
        }
    }

    void analyzeStructDecl(const std::shared_ptr<StructDecl> &structDecl)
    {
        const auto structSymbol = std::make_shared<Symbol>(structDecl->name, structDecl->baseType, structDecl->getLine(), structDecl->getCol(), Kind::STRUCT_TAG);
        check(structDecl->name, structSymbol, Kind::STRUCT_TAG, structDecl->getLine(), structDecl->getCol(), structDecl);
    }

    void analyzeVarDecl(const std::shared_ptr<VarDecl> &variable)
    {
        const auto variableSymbol = std::make_shared<Symbol>(variable->name, variable->type, variable->getLine(), variable->getCol(), Kind::VARIABLE);
        check(variable->name, variableSymbol, Kind::VARIABLE, variable->getLine(), variable->getCol(), variable);
    }

    void analyzeDeclareStmt(const std::shared_ptr<DeclareStmt> &stmt)
    {
        if (std::holds_alternative<std::vector<std::shared_ptr<VarDecl>>>(stmt->variables))
        {
            for (const auto& variables = std::get<std::vector<std::shared_ptr<VarDecl>>>(stmt->variables);
                 auto & var : variables)
            {
                analyzeVarDecl(var);
            }
        }else
        {
            const auto & structDecl = std::get<std::shared_ptr<StructDecl>>(stmt->variables);
            analyzeStructDecl(structDecl);
        }
    }

    void analyzeReturnStmt(const std::shared_ptr<ReturnStmt> &stmt)
    {
        if (stmt->returnExpression != nullptr)
        {
            analyzeExpr(stmt->returnExpression);
            if (currentReturnType != stmt->returnExpression->resolvedType)
            {
                std::cerr << "Semantic error at line " <<stmt->returnExpression->getLine() << ", col " << stmt->returnExpression->col << ": expected type - " << currentReturnType->toString() << ", got " << stmt->returnExpression->resolvedType->toString() <<"\n";
            }
        }else
        {
            if (auto x = std::dynamic_pointer_cast<VoidType>(currentReturnType))
            {
                std::cerr << "Semantic error at line " << stmt->line << ", col "<< stmt->col <<": Function with void return type has non-void return type\n";
            }

        }
    }

    void analyzeStatements(const std::shared_ptr<BlockStmt>& blockStmt)
    {
        for (const auto& statement : blockStmt->statements)
        {
            if (auto x = std::dynamic_pointer_cast<DeclareStmt>(statement))
            {
                analyzeDeclareStmt(x);
            }else if ( auto x = std::dynamic_pointer_cast<ExprStmt>(statement))
            {
                analyzeExprStmt(x);
            }else if (auto x = std::dynamic_pointer_cast<BlockStmt>(statement))
            {
                symbolTable.enterScope();
                analyzeStatements(x);
                symbolTable.exitScope();
            }else if (auto x = std::dynamic_pointer_cast<ReturnStmt>(statement))
            {
                analyzeReturnStmt(x);
            }
        }
    }


    void analyzeFunction(std::shared_ptr<Function>& node)
    {
        auto prev = currentReturnType;
        currentReturnType = node->type;

        std::vector<std::shared_ptr<Type>> paramTypes;
        paramTypes.reserve(node->parameters.size());
        for (const auto& param : node->parameters)
        {
            paramTypes.push_back(param.type);
        }
        auto functionType = std::make_shared<FunctionType>(node->type, paramTypes, node->variadic);
        const auto function = std::make_shared<Symbol>(node->name, functionType, node->getLine(), node->getCol(),
                                         Kind::FUNCTION);
        check(node->name, function, Kind::FUNCTION, node->getLine(), node->getCol(), node);
        symbolTable.enterScope();
        for (auto& param : node->parameters)
        {
            const auto paramSymbol = std::make_shared<Symbol>(param.name, param.type, param.line, param.col, Kind::PARAMETER);
            if (!symbolTable.insert(param.name, paramSymbol, Kind::PARAMETER))
            {
                std::cerr << "Semantic error at line " << param.line << ", col " << param.col << ": duplicate parameter '" << param.name << "'\n";
            }
        }
        analyzeStatements(node->statements);

        symbolTable.exitScope();
        currentReturnType = prev;
    }

    void validate(const std::shared_ptr<Program> &program)
    {
        for (const auto& node : program->nodes)
        {
            if (auto x = std::dynamic_pointer_cast<Function>(node))
            {
                analyzeFunction(x);
            }else if (auto x = std::dynamic_pointer_cast<VarDecl>(node))
            {
                analyzeVarDecl(x);
            }else if (auto x = std::dynamic_pointer_cast<StructDecl>(node))
            {
                analyzeStructDecl(x);
            }else
            {
                throw std::runtime_error("Should not be possible to come here");
            }
        }
    }

};
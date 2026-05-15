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

inline bool isArithmeticOp(const std::string& op)
{
    return op == "+" || op == "-" || op == "*" || op == "/" || op == "%";
}

inline bool isComparisonOp(const std::string& op)
{
    return op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" || op == "!=";
}

inline bool isLogicalOp(const std::string& op)
{
    return op == "||" || op == "&&";
}

inline bool isBitwiseOp(const std::string& op)
{
    return op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>";
}

inline bool isComma(const std::string& op)
{
    return op == ",";
}

inline bool isInteger(const std::shared_ptr<Type>& t)
{
    return std::dynamic_pointer_cast<IntType>(t) != nullptr
        || std::dynamic_pointer_cast<CharType>(t) != nullptr;
}

inline bool isPointer(const std::shared_ptr<Type>& t)
{
    const auto x = std::dynamic_pointer_cast<PointerType>(t);
    return x != nullptr;
}

inline bool isVoid(const std::shared_ptr<Type>& t)
{
    const auto x = std::dynamic_pointer_cast<VoidType>(t);
    return x != nullptr;
}

inline bool isScalar(const std::shared_ptr<Type>& t)
{
    return isInteger(t) || isPointer(t);
}

class SemanticAnalyzer
{
    SymbolTable symbolTable;
    std::shared_ptr<Type> currentReturnType;
    int loopDepth = 0;
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

   void declareFunction(const std::shared_ptr<Function>& node,
                       const std::shared_ptr<FunctionType>& fnType)
  {
      auto existing = symbolTable.find(node->name, Kind::FUNCTION);

      if (!existing) {
          // First time seeing this name — fresh insert.
          auto sym = std::make_shared<Symbol>(node->name, fnType,
                                              node->getLine(), node->getCol(),
                                              Kind::FUNCTION);
          symbolTable.insert(node->name, sym, Kind::FUNCTION);
          node->symbol = sym;
          sym->node = node;
          return;
      }

      if (existing->kind != Kind::FUNCTION) {
          std::cerr << "Semantic error at line " << node->getLine() << ", col " << node->getCol()
                    << ": '" << node->name << "' redeclared as different kind ("
                    << kindToString(existing->kind) << " at line " << existing->line << ").\n";
          return;
      }

      if (!existing->type->equals(*fnType)) {
          std::cerr << "Semantic error at line " << node->getLine() << ", col " << node->getCol()
                    << ": conflicting types for '" << node->name
                    << "' (was " << existing->type->toString()
                    << ", now " << fnType->toString() << ").\n";
          return;
      }

      if (node->statements) {
          auto prevNode = std::dynamic_pointer_cast<Function>(existing->node.lock());
          if (prevNode && prevNode->statements) {
              std::cerr << "Semantic error at line " << node->getLine() << ", col " << node->getCol()
                        << ": redefinition of '" << node->name
                        << "' (previous definition at line " << existing->line << ").\n";
              return;
          }
          existing->node = node;     // this body is now the canonical decl
      }
      node->symbol = existing;       // share the existing Symbol either way
  }

    bool isNullPointerConstant(const std::shared_ptr<Expression>& e) {
        if (const auto lit = std::dynamic_pointer_cast<IntLiterals>(e))
            return lit->value == "0";
        if (const auto cast = std::dynamic_pointer_cast<CastExpr>(e)) {
            const auto ptr = std::dynamic_pointer_cast<PointerType>(cast->type);
            if (!ptr) return false;
            if (!std::dynamic_pointer_cast<VoidType>(ptr->getInner())) return false;
            return isNullPointerConstant(cast->operand);
        }
        return false;
    }

    size_t decodedLength(const std::string& s) {
        // s is "..." (with outer quotes)
        size_t n = 0;
        for (size_t i = 1; i + 1 < s.size(); ++i) {
            if (s[i] == '\\') ++i;   // skip the backslash, count the next char as 1
            ++n;
        }
        return n + 1;                // +1 for null terminator
    }

    void analyzeExpr(const std::shared_ptr<Expression>& expr)
    {
        if (auto x = std::dynamic_pointer_cast<IntLiterals>(expr))
        {
            x->resolvedType = IntType::getInstance();
            x->isLvalue = false;
        }else if (auto x = std::dynamic_pointer_cast<StringLiterals>(expr))
        {
            const size_t size = decodedLength(x->literal);
            const auto type = CharType::getInstance();
            auto stringType = std::make_shared<ArrayType>(type, size);
            x->resolvedType = stringType;
            x->isLvalue = false;
        }else if (auto x = std::dynamic_pointer_cast<AssignExpr>(expr))
        {
            analyzeExpr(x->lhs);
            analyzeExpr(x->rhs);

            const auto lType = x->lhs->resolvedType;
            const auto rType = x->rhs->resolvedType;
            if (!x->lhs->isLvalue)
            {
                std::cerr << "Semantic error at line "<< x->lhs->getLine() <<", col "<<x->lhs->getCol()
                    <<": Left expression must be an lvalue, got rvalue of type: " << lType->toString() << ".\n";
            }

            if (x->op == "=")
            {
                if (!(isScalar(lType) && isScalar(rType)) && !(isPointer(lType) && isNullPointerConstant(x->rhs)))
                {
                    std::cerr << "Semantic error at line "<< x->lhs->getLine() <<", col "<<x->lhs->getCol()
                        <<": Left expression and right expression are not same type, left type: "
                        << lType->toString() << ", right type: " << rType->toString() << ".\n";
                }
            }else if (x->op == "+=" || x->op == "-=")
            {
                if (!isScalar(lType))
                {
                    std::cerr << "Semantic error at line "<< x->lhs->getLine() <<", col "<<x->lhs->getCol()
                        <<": Arithmetic assignment needs integer or pointer for left expression, got " << lType->toString() << ".\n";
                }
                if (!isInteger(rType))
                {
                    std::cerr << "Semantic error at line "<< x->rhs->getLine() <<", col "<<x->rhs->getCol()
                        <<": Arithmetic assignment needs integer for right expression, got " << rType->toString() << ".\n";
                }
            }else if (x->op == "*=" || x->op == "/=" || x->op == "%=")
            {
                if (!isInteger(lType))
                {
                    std::cerr << "Semantic error at line "<< x->lhs->getLine() <<", col "<<x->lhs->getCol()
                        <<": Arithmetic assignment needs integer for left expression, got " << lType->toString() << ".\n";
                }
                if (!isInteger(rType))
                {
                    std::cerr << "Semantic error at line "<< x->rhs->getLine() <<", col "<<x->rhs->getCol()
                        <<": Arithmetic assignment needs integer for right expression, got " << rType->toString() << ".\n";
                }
            }else if (x->op == "&=" || x->op == "^=" || x->op == "|=" || x->op == "<<=" || x->op == ">>=")
            {
                if (!isInteger(lType))
                {
                    std::cerr << "Semantic error at line "<< x->lhs->getLine() <<", col "<<x->lhs->getCol()
                        <<": Bitwise assignment needs integer for left expression, got " << lType->toString() << ".\n";
                }
                if (!isInteger(rType))
                {
                    std::cerr << "Semantic error at line "<< x->rhs->getLine() <<", col "<<x->rhs->getCol()
                        <<": Bitwise assignment needs integer for right expression, got " << rType->toString() << ".\n";
                }
            }

            x->resolvedType = x->lhs->resolvedType;
            x->isLvalue = false;
        }else if (auto x = std::dynamic_pointer_cast<BinaryExpr>(expr))
        {
            analyzeExpr(x->left);
            analyzeExpr(x->right);

            const auto lType = x->left->resolvedType;
            const auto rType = x->right->resolvedType;

            if (isArithmeticOp(x->binaryOp))
            {
                if (!isInteger(lType))
                {
                    std::cerr << "Semantic error at line "<< x->left->getLine() <<", col "<<x->left->getCol()
                        <<": Arithmetic operator needs integer for left expression, got " << lType->toString() << ".\n";
                }
                if (!isInteger(rType))
                {
                    std::cerr << "Semantic error at line "<< x->right->getLine() <<", col "<<x->right->getCol()
                        <<": Arithmetic operator needs integer for right expression, got " << rType->toString() << ".\n";
                }
                x->resolvedType = IntType::getInstance();
                x->isLvalue = false;
            }else if (isComparisonOp(x->binaryOp))
            {
                const bool isEquality = (x->binaryOp == "==" || x->binaryOp == "!=");

                if (!isScalar(lType))
                {
                    std::cerr << "Semantic error at line "<< x->left->getLine() <<", col "<<x->left->getCol()
                        <<": Comparison operator needs integer or pointer for left expression, got " << lType->toString() << ".\n";
                }
                if (!isScalar(rType))
                {
                    std::cerr << "Semantic error at line "<< x->right->getLine() <<", col "<<x->right->getCol()
                        <<": Comparison operator needs integer or pointer for right expression, got " << rType->toString() << ".\n";
                }

                if (isScalar(lType) && isScalar(rType))
                {
                    const bool bothArith = isInteger(lType) && isInteger(rType);
                    const bool bothPtr   = isPointer(lType) && isPointer(rType);

                    if (bothPtr && !lType->equals(*rType))
                    {
                        std::cerr << "Semantic error at line "<< x->right->getLine() <<", col "<<x->right->getCol()
                            <<": Comparison operator needs matching pointer types, left: " << lType->toString()
                            <<", right: " << rType->toString() << ".\n";
                    }
                    else if (!bothArith && !bothPtr)
                    {
                        const bool nullOK = isEquality &&
                            ((isPointer(lType) && isNullPointerConstant(x->right))
                          || (isPointer(rType) && isNullPointerConstant(x->left)));
                        if (!nullOK)
                        {
                            std::cerr << "Semantic error at line "<< x->right->getLine() <<", col "<<x->right->getCol()
                                <<": Comparison operator needs both arithmetic or both pointer, left: " << lType->toString()
                                <<", right: " << rType->toString() << ".\n";
                        }
                    }
                }

                x->resolvedType = IntType::getInstance();
                x->isLvalue = false;
            }else if (isLogicalOp(x->binaryOp))
            {
                if (!isScalar(lType))
                {
                    std::cerr << "Semantic error at line "<< x->left->getLine() <<", col "<<x->left->getCol()
                        <<": Logical operator needs integer or pointer for left expression, got " << lType->toString() << ".\n";
                }
                if (!isScalar(rType))
                {
                    std::cerr << "Semantic error at line "<< x->right->getLine() <<", col "<<x->right->getCol()
                        <<": Logical operator needs integer or pointer for right expression, got " << rType->toString() << ".\n";
                }

                x->resolvedType = IntType::getInstance();
                x->isLvalue = false;
            }else if (isBitwiseOp(x->binaryOp))
            {
                if (!isInteger(lType))
                {
                    std::cerr << "Semantic error at line "<< x->left->getLine() <<", col "<<x->left->getCol()
                        <<": Bitwise operator needs integer for left expression, got " << lType->toString() << ".\n";
                }
                if (!isInteger(rType))
                {
                    std::cerr << "Semantic error at line "<< x->right->getLine() <<", col "<<x->right->getCol()
                        <<": Bitwise operator needs integer for right expression, got " << rType->toString() << ".\n";
                }

                x->resolvedType = IntType::getInstance();
                x->isLvalue = false;
            }else if (isComma(x->binaryOp))
            {
                x->resolvedType = rType;
                x->isLvalue = x->right->isLvalue;
            }

        }else if (auto x = std::dynamic_pointer_cast<CastExpr>(expr))
        {
            analyzeExpr(x->operand);

            if (!isScalar(x->operand->resolvedType) || (!isScalar(x->type) && !isVoid(x->type)))
            {
                std::cerr << "Semantic error at line "<< x->getLine() <<", col "<<x->getCol()
                        <<": Cannot cast to or from struct, got " << x->operand->resolvedType->toString()
                        << " and " << x->type->toString() << ".\n";
            }

            x->resolvedType = x->type;
            x->isLvalue = false;
        }else if (auto x = std::dynamic_pointer_cast<FunctionCallExpr>(expr))
        {
            analyzeFunctionCallExpr(x);
        }else if (auto x = std::dynamic_pointer_cast<InitExpr>(expr))
        {
            x->resolvedType = IntType::getInstance();
            x->isLvalue = false;
        }else if (auto x = std::dynamic_pointer_cast<MemberExpr>(expr))
        {
            analyzeExpr(x->object);
            const auto& objType = x->object->resolvedType;
            const int objLine = x->object->getLine();
            const int objCol  = x->object->getCol();

            auto recoverAsInt = [&]() {
                x->resolvedType = IntType::getInstance();
                x->isLvalue = true;
            };

            if (x->isArrow && !isPointer(objType))
            {
                std::cerr << "Semantic error at line " << objLine << ", col " << objCol
                          << ": '->' needs pointer, received: " << objType->toString() << ".\n";
            }
            else if (!x->isArrow && isPointer(objType))
            {
                std::cerr << "Semantic error at line " << objLine << ", col " << objCol
                          << ": '.' needs object, received: " << objType->toString() << ".\n";
            }

            std::shared_ptr<StructType> baseType;
            if (auto ptr = std::dynamic_pointer_cast<PointerType>(objType))
                baseType = std::dynamic_pointer_cast<StructType>(ptr->getInner());
            else
                baseType = std::dynamic_pointer_cast<StructType>(objType);

            if (!baseType)
            {
                std::cerr << "Semantic error at line " << objLine << ", col " << objCol
                          << ": expected struct or pointer to struct, got "
                          << objType->toString() << ".\n";
                recoverAsInt();
                return;
            }

            auto baseSymbol = symbolTable.find(baseType->getName(), Kind::STRUCT_TAG);
            if (!baseSymbol)
            {
                std::cerr << "Semantic error at line " << objLine << ", col " << objCol
                          << ": struct not defined: " << baseType->getName() << ".\n";
                recoverAsInt();
                return;
            }

            auto structNode = std::dynamic_pointer_cast<StructDecl>(baseSymbol->node.lock());
            // should never fire.
            if (!structNode)
            {
                std::cerr << "Internal error at line " << objLine << ", col " << objCol
                          << ": struct '" << baseType->getName()
                          << "' has no associated declaration.\n";
                recoverAsInt();
                return;
            }

            auto it = std::find_if(structNode->fields.begin(), structNode->fields.end(),
                [&](const StructField& f) { return f.name == x->field; });

            if (it == structNode->fields.end())
            {
                std::cerr << "Semantic error at line " << x->getLine() << ", col " << x->getCol()
                          << ": member '" << x->field << "' not defined in struct '"
                          << structNode->name << "'.\n";
                recoverAsInt();
                return;
            }

            x->resolvedType = it->type;
            x->isLvalue = true;
        }else if (auto x = std::dynamic_pointer_cast<SizeOfExpr>(expr))
        {
            if (x->expr)
                analyzeExpr(x->expr);

            x->resolvedType = IntType::getInstance();
            x->isLvalue = false;
        }else if (auto x = std::dynamic_pointer_cast<SubscriptExpr>(expr))
        {
            analyzeExpr(x->lvalue);
            analyzeExpr(x->index);

            if (!isInteger(x->index->resolvedType))
            {
                std::cerr << "Semantic error at line " << x->index->getLine() << ", col " << x->index->getCol()
                    << ": required integer index, received '" << x->index->resolvedType->toString() << "'.\n";
            }

            auto lt = x->lvalue->resolvedType;
            std::shared_ptr<Type> finalType = IntType::getInstance();
            if (auto arr = std::dynamic_pointer_cast<ArrayType>(lt))      finalType = arr->getInner();
            else if (auto ptr = std::dynamic_pointer_cast<PointerType>(lt)) finalType = ptr->getInner();
            else
            {
                std::cerr<< "Semantic error at line " << x->getLine() << ", col " << x->getCol()
                    << ": required pointer or array type, received '" << x->lvalue->resolvedType->toString() << "'.\n";
            }

            x->resolvedType = finalType;
            x->isLvalue = true;
        }else if (auto x = std::dynamic_pointer_cast<TernaryExpr>(expr))
        {
            analyzeExpr(x->condition);
            analyzeExpr(x->thenBranch);
            analyzeExpr(x->elseBranch);

            if (!isScalar(x->condition->resolvedType))
            {
                std::cerr << "Semantic error at line " << x->condition->getLine() << ", col " << x->condition->getCol()
                    << ": required integer or pointer condition, received '" << x->condition->resolvedType->toString() << "'.\n";
            }

            const auto tType = x->thenBranch->resolvedType;
            const auto eType = x->elseBranch->resolvedType;

            if (tType->equals(*eType))
            {
                x->resolvedType = tType;
            }else if (isPointer(tType) && isNullPointerConstant(x->elseBranch))
            {
                x->resolvedType = tType;
            }else if (isPointer(eType) && isNullPointerConstant(x->thenBranch))
            {
                x->resolvedType = eType;
            }else
            {
                std::cerr << "Semantic error at line " << x->getLine() << ", col " << x->getCol()
                    << ": ternary branches have incompatible types, then: '" << tType->toString()
                    << "', else: '" << eType->toString() << "'.\n";
                x->resolvedType = tType;
            }

            x->isLvalue = false;
        }else if (auto x = std::dynamic_pointer_cast<UnaryExpr>(expr))
        {
            analyzeExpr(x->operand);
            if (x->op == "-" || x->op == "+" || x->op == "~")
            {
                if (!isInteger(x->operand->resolvedType))
                {
                    std::cerr << "Semantic error at line " << x->getLine() << ", col " << x->getCol()
                    << ": required integer operand, received '" << x->operand->resolvedType->toString() << "'.\n";
                }
                x->resolvedType = IntType::getInstance();
                x->isLvalue = false;
            }else if (x->op == "!")
            {
                if (!isScalar(x->operand->resolvedType))
                {
                    std::cerr << "Semantic error at line " << x->getLine() << ", col " << x->getCol()
                    << ": required integer or pointer operand, received '" << x->operand->resolvedType->toString() << "'.\n";
                }
                x->resolvedType = IntType::getInstance();
                x->isLvalue = false;
            }else if (x->op == "*")
            {
                auto resolvedType = x->operand->resolvedType;
                if (!isPointer(resolvedType))
                {
                    std::cerr << "Semantic error at line " << x->getLine() << ", col " << x->getCol()
                    << ": required pointer operand, received '" << x->operand->resolvedType->toString() << "'.\n";
                    // to make sure nothing crashes downstream
                    x->resolvedType = IntType::getInstance();
                    x->isLvalue = true;
                }
                if (const auto pointerType = std::dynamic_pointer_cast<PointerType>(resolvedType))
                {
                    x->resolvedType = pointerType->getInner();
                    x->isLvalue = true;
                }
            }else if (x->op == "&")
            {
                if (!x->operand->isLvalue)
                {
                    std::cerr << "Semantic error at line " << x->getLine() << ", col " << x->getCol()
                    << ": required lvalue, received rvalue of type '" << x->operand->resolvedType->toString() << "'.\n";
                }
                x->resolvedType = std::make_shared<PointerType>(x->operand->resolvedType);
                x->isLvalue = false;
            }else if (x->op == "++" || x->op == "--")
            {
                if (!x->operand->isLvalue)
                {
                    std::cerr << "Semantic error at line " << x->getLine() << ", col " << x->getCol()
                    << ": required lvalue, received rvalue of type '" << x->operand->resolvedType->toString() << "'.\n";
                }
                if (!isScalar(x->operand->resolvedType))
                {
                    std::cerr << "Semantic error at line " << x->getLine() << ", col " << x->getCol()
                    << ": required integer or pointer operand, received '" << x->operand->resolvedType->toString() << "'.\n";
                }
                x->resolvedType = x->operand->resolvedType;
                x->isLvalue = false;
            }
        }else if (auto x = std::dynamic_pointer_cast<VariableExpr>(expr))
        {
            auto sym = symbolTable.find(x->name, Kind::VARIABLE);
            if (!sym) sym = symbolTable.find(x->name, Kind::PARAMETER);

            if (!sym)
            {
                std::cerr << "Semantic error at line " << x->getLine() << ", col " << x->getCol()
                    << ": use of undeclared identifier '" << x->name << "'.\n";
                x->resolvedType = IntType::getInstance();  // placeholder so downstream doesn't crash
                x->isLvalue = false;
            }
            else
            {
                x->resolvedType = sym->type;
                x->isLvalue = true;
                x->symbol = sym;
            }
        }else
        {
            throw std::runtime_error("Reached invalid expression at line " + std::to_string(expr->getLine())
                + ", col " + std::to_string(expr->getCol()));
        }

    }

    void analyzeFunctionCallExpr(const std::shared_ptr<FunctionCallExpr> &expr)
    {
        std::string functionName = expr->functionName->name;
        const auto checkFunctionExistence = symbolTable.find(functionName, Kind::FUNCTION);
        if (checkFunctionExistence == nullptr)
        {
            std::cerr << "Semantic error at line "<<expr->getLine()<<", col "<<expr->getCol()<<": function "<<functionName<<" not declared\n";
            expr->resolvedType = IntType::getInstance();
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
                    if (!functionType->paramTypes[i]->equals(*expr->parameters[i]->resolvedType))
                    {
                        std::cerr <<"Semantic error at line "<<expr->getLine()<<", col "<<expr->getCol()<<": mismatched param types, expected " <<functionType->paramTypes[i]->toString() << " got " << expr->parameters[i]->resolvedType->toString() << "\n";
                    }
                }
            }
            expr->resolvedType = functionType->returnType;
        }
        expr->isLvalue = false;
    }

    void analyzeExprStmt(const std::shared_ptr<ExprStmt> &exprStmt)
    {
        analyzeExpr(exprStmt->expr);
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
        if (!currentReturnType)
        {
            std::cerr << "Semantic error at line " <<stmt->getLine() << ", col "
            << stmt->col << ": return statement not inside a function.\n";

            return;
        }
        if (stmt->returnExpression != nullptr)
        {
            analyzeExpr(stmt->returnExpression);
            if (!currentReturnType->equals(*stmt->returnExpression->resolvedType))
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

    void analyzeIfStmt(const std::shared_ptr<IfStmt> &ifStmt)
    {
        analyzeExpr(ifStmt->condition);

        if (!isScalar(ifStmt->condition->resolvedType))
        {
            std::cerr << "Semantic error at line " << ifStmt->condition->getLine() << ", col " << ifStmt->condition->getCol()
            << ": expected pointer or integer type, got - " << ifStmt->condition->resolvedType->toString() <<"\n";
        }

        symbolTable.enterScope();
        analyzeStatements(ifStmt->thenBlock);
        symbolTable.exitScope();

        if (ifStmt->elseBlock)
        {
            symbolTable.enterScope();
            analyzeStatements(ifStmt->elseBlock);
            symbolTable.exitScope();
        }

    }

    void analyzeWhileStmt(const std::shared_ptr<WhileStmt> &whileStmt)
    {
        analyzeExpr(whileStmt->condition);

        if (!isScalar(whileStmt->condition->resolvedType))
        {
            std::cerr << "Semantic error at line " << whileStmt->condition->getLine() << ", col " << whileStmt->condition->getCol()
            << ": expected pointer or integer type, got - " << whileStmt->condition->resolvedType->toString() <<"\n";
        }

        loopDepth+=1;
        symbolTable.enterScope();
        analyzeStatements(whileStmt->whileBlock);
        symbolTable.exitScope();
        loopDepth-=1;
    }

    void analyzeDoWhileStmt(const std::shared_ptr<DoWhileStmt> &doWhileStmt)
    {
        analyzeExpr(doWhileStmt->condition);

        if (!isScalar(doWhileStmt->condition->resolvedType))
        {
            std::cerr << "Semantic error at line " << doWhileStmt->condition->getLine() << ", col " << doWhileStmt->condition->getCol()
            << ": expected pointer or integer type, got - " << doWhileStmt->condition->resolvedType->toString() <<"\n";
        }

        loopDepth+=1;
        symbolTable.enterScope();
        analyzeStatements(doWhileStmt->block);
        symbolTable.exitScope();
        loopDepth-=1;
    }

    void analyzeForStmt(const std::shared_ptr<ForStmt> &forStmt)
    {
        if (auto x = std::dynamic_pointer_cast<ExprStmt>(forStmt->initialization))
        {
            analyzeExprStmt(x);
        }
        if (auto x = std::dynamic_pointer_cast<ExprStmt>(forStmt->condition))
        {
            analyzeExprStmt(x);
            if (!isScalar(x->expr->resolvedType))
            {
                std::cerr << "Semantic error at line " << x->expr->getLine() << ", col " << x->expr->getCol()
                << " condition in for loop must be pointer or integer got - " << x->expr->resolvedType->toString() <<"\n";
            }
        }
        if (auto x = std::dynamic_pointer_cast<ExprStmt>(forStmt->update))
        {
            analyzeExprStmt(x);
        }

        loopDepth+=1;
        symbolTable.enterScope();
        analyzeStatements(forStmt->forBlock);
        symbolTable.exitScope();
        loopDepth-=1;
    }

    void analyzeBreakContinueStmt(const std::shared_ptr<Statement> &stmt)
    {
        if (loopDepth <= 0)
        {
            std::cerr << "Semantic error at line " << stmt->getLine() << ", col " << stmt->getCol()
            <<" , no loop statements found\n";
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
                analyzeStatements(x);
            }else if (auto x = std::dynamic_pointer_cast<ReturnStmt>(statement))
            {
                analyzeReturnStmt(x);
            }else if (auto x = std::dynamic_pointer_cast<IfStmt>(statement))
            {
                analyzeIfStmt(x);
            }else if (auto x = std::dynamic_pointer_cast<WhileStmt>(statement))
            {
                analyzeWhileStmt(x);
            }else if (auto x = std::dynamic_pointer_cast<DoWhileStmt>(statement))
            {
                analyzeDoWhileStmt(x);
            }else if (auto x = std::dynamic_pointer_cast<ForStmt>(statement))
            {
                analyzeForStmt(x);
            }else if (auto x = std::dynamic_pointer_cast<BreakStmt>(statement))
            {
                analyzeBreakContinueStmt(x);
            }else if (auto x = std::dynamic_pointer_cast<ContinueStmt>(statement))
            {
                analyzeBreakContinueStmt(x);
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
        declareFunction(node, functionType);
        symbolTable.enterScope();
        for (auto& param : node->parameters)
        {
            const auto paramSymbol = std::make_shared<Symbol>(param.name, param.type, param.line, param.col, Kind::PARAMETER);
            if (!symbolTable.insert(param.name, paramSymbol, Kind::PARAMETER))
            {
                std::cerr << "Semantic error at line " << param.line << ", col " << param.col << ": duplicate parameter '" << param.name << "'\n";
            }
        }
        if (node->statements)
        {
            analyzeStatements(node->statements);
        }

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
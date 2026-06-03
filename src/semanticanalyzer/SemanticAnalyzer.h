#pragma once
#include "../ast/ASTNodes/ASTNode.h"
#include "../ast/ASTNodes/Program.h"
#include "../ast/Expressions/CastExpr.h"
#include "../ast/Expressions/FunctionCallExpr.h"
#include "../ast/Expressions/IntLiterals.h"
#include "../ast/Statements/BreakStmt.h"
#include "../ast/Statements/ContinueStmt.h"
#include "../ast/Statements/DeclareStmt.h"
#include "../ast/Statements/ExprStmt.h"
#include "../ast/Statements/ReturnStmt.h"
#include "../ast/TopLevelNodes/Function.h"
#include "../ast/TopLevelNodes/StructDecl.h"
#include "../ast/TopLevelNodes/VarDecl.h"
#include "../symboltable/SymbolTable.h"
#include "../utils/diagnostic.h"

inline bool isArithmeticOp(const std::string &op)
{
    return op == "+" || op == "-" || op == "*" || op == "/" || op == "%";
}

inline bool isComparisonOp(const std::string &op)
{
    return op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" || op == "!=";
}

inline bool isLogicalOp(const std::string &op) { return op == "||" || op == "&&"; }

inline bool isBitwiseOp(const std::string &op)
{
    return op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>";
}

inline bool isComma(const std::string &op) { return op == ","; }

inline bool isInteger(const std::shared_ptr<Type> &t)
{
    return std::dynamic_pointer_cast<IntType>(t) != nullptr ||
           std::dynamic_pointer_cast<LongType>(t) != nullptr ||
           std::dynamic_pointer_cast<CharType>(t) != nullptr;
}

inline bool isPointer(const std::shared_ptr<Type> &t)
{
    const auto x = std::dynamic_pointer_cast<PointerType>(t);
    return x != nullptr;
}

inline bool isArray(const std::shared_ptr<Type> &t)
{
    const auto x = std::dynamic_pointer_cast<ArrayType>(t);
    return x != nullptr;
}

inline bool isVoid(const std::shared_ptr<Type> &t)
{
    const auto x = std::dynamic_pointer_cast<VoidType>(t);
    return x != nullptr;
}

inline bool isScalar(const std::shared_ptr<Type> &t) { return isInteger(t) || isPointer(t); }

// Width in bytes of a scalar type; 0 for non-scalars. Used for usual-arithmetic
// conversions, where the wider integer type wins.
inline int typeSize(const std::shared_ptr<Type> &t)
{
    if (std::dynamic_pointer_cast<CharType>(t))
        return 1;
    if (std::dynamic_pointer_cast<IntType>(t))
        return 4;
    if (std::dynamic_pointer_cast<LongType>(t))
        return 8;
    if (std::dynamic_pointer_cast<PointerType>(t))
        return 8;
    return 0;
}

// Common type of two arithmetic operands (callers gate on isInteger): identical
// types stay; otherwise the wider one (so int + long -> long).
inline std::shared_ptr<Type> getCommonType(const std::shared_ptr<Type> &a,
                                           const std::shared_ptr<Type> &b)
{
    if (a->equals(*b))
        return a;
    return typeSize(a) >= typeSize(b) ? a : b;
}

class SemanticAnalyzer
{
    SymbolTable symbolTable;
    Diagnostic::DiagnosticEngine &diag;
    std::shared_ptr<Type> currentReturnType;
    int loopLabel = 0;
    int labelCounter = 0;

  public:
    explicit SemanticAnalyzer(Diagnostic::DiagnosticEngine &diag) : diag(diag) {}

    void error(int line, int col, const std::string &msg)
    {
        diag.report(Diagnostic::DiagLevel::SEMANTIC, {line, col}, msg);
    }

    void check(const std::string &name, const std::shared_ptr<Symbol> &symbol, const Kind kind,
               const int line, const int col, const std::shared_ptr<ASTNode> &node)
    {
        if (!symbolTable.insert(name, symbol, kind))
        {
            const auto existing = symbolTable.find(name, kind);
            error(line, col,
                  std::string("redeclaration of ") + kindToString(kind) + " '" + name +
                      "'. Previous declaration (" + kindToString(existing->kind) + ") at line " +
                      std::to_string(existing->line) + ".");
            return;
        }
        node->symbol = symbol;
        symbol->node = node;
    }

    void declareFunction(const std::shared_ptr<Function> &node,
                         const std::shared_ptr<FunctionType> &fnType)
    {
        const auto sc = node->storageClass;
        // Functions are TU-wide entities: reconcile against the one linkage-registry
        // symbol even when the declaration appears inside a block.
        auto g = symbolTable.findLinked(node->name);
        const bool gLinked = g && g->kind == Kind::FUNCTION && g->linkage != Linkage::None;
        // static → internal; extern/none → follows a prior linked decl, else external.
        const Linkage linkage = (sc == StorageClass::Static)
                                    ? Linkage::Internal
                                    : (gLinked ? g->linkage : Linkage::External);

        if (!g)
        {
            auto sym = std::make_shared<Symbol>(node->name, fnType, node->getLine(), node->getCol(),
                                                Kind::FUNCTION);
            sym->linkage = linkage;
            sym->defined = (node->statements != nullptr);
            symbolTable.insertLinked(node->name, sym);
            node->symbol = sym;
            sym->node = node;
        }
        else if (g->kind != Kind::FUNCTION)
        {
            error(node->getLine(), node->getCol(),
                  "'" + node->name + "' redeclared as different kind (" + kindToString(g->kind) +
                      " at line " + std::to_string(g->line) + ").");
            return;
        }
        else
        {
            if (!g->type->equals(*fnType))
            {
                error(node->getLine(), node->getCol(),
                      "conflicting types for '" + node->name + "' (was " + g->type->toString() +
                          ", now " + fnType->toString() + ").");
                return;
            }
            if (g->linkage != linkage)
            {
                error(node->getLine(), node->getCol(),
                      "conflicting linkage for '" + node->name +
                          "' (previous declaration at line " + std::to_string(g->line) + ").");
                return;
            }
            if (node->statements)
            {
                auto prevNode = std::dynamic_pointer_cast<Function>(g->node.lock());
                if (prevNode && prevNode->statements)
                {
                    error(node->getLine(), node->getCol(),
                          "redefinition of '" + node->name + "' (previous definition at line " +
                              std::to_string(g->line) + ").");
                    return;
                }
                g->node = node; // this body is now the canonical decl
                g->defined = true;
            }
            node->symbol = g; // share the existing Symbol
        }

        // Make the function visible for name resolution in the current scope
        // (file scope for a top-level function, the block for a local prototype).
        auto same = symbolTable.findSameScope(node->name, Kind::FUNCTION);
        if (!same)
            symbolTable.bindCurrent(node->name, node->symbol, Kind::FUNCTION);
        else if (same->linkage == Linkage::None)
            error(node->getLine(), node->getCol(),
                  "conflicting declaration of '" + node->name + "' (previous declaration at line " +
                      std::to_string(same->line) + ").");
    }

    bool isNullPointerConstant(const std::shared_ptr<Expression> &e)
    {
        if (const auto lit = std::dynamic_pointer_cast<IntLiterals>(e))
            return lit->value == 0;
        if (const auto cast = std::dynamic_pointer_cast<CastExpr>(e))
        {
            const auto ptr = std::dynamic_pointer_cast<PointerType>(cast->type);
            if (!ptr)
                return false;
            if (!std::dynamic_pointer_cast<VoidType>(ptr->getInner()))
                return false;
            return isNullPointerConstant(cast->operand);
        }
        return false;
    }

    // Reconcile an already-analyzed rvalue to `target` by wrapping it in a
    // synthesized CastExpr when the types differ. The new node is pre-typed and is
    // NOT re-run through analyzeExpr. Codegen later lowers these into the actual
    // sign-extend / truncate.
    std::shared_ptr<Expression> convertTo(const std::shared_ptr<Expression> &e,
                                          const std::shared_ptr<Type> &target)
    {
        if (e->resolvedType->equals(*target))
            return e;
        auto cast = std::make_shared<CastExpr>(target, e, e->getLine(), e->getCol());
        cast->resolvedType = target;
        cast->isLvalue = false;
        return cast;
    }

    size_t decodedLength(const std::string &s)
    {
        // s is "..." (with outer quotes)
        size_t n = 0;
        for (size_t i = 1; i + 1 < s.size(); ++i)
        {
            if (s[i] == '\\')
                ++i; // skip the backslash, count the next char as 1
            ++n;
        }
        return n + 1; // +1 for null terminator
    }

    void analyzeExpr(const std::shared_ptr<Expression> &expr)
    {
        if (auto x = std::dynamic_pointer_cast<IntLiterals>(expr))
        {
            x->resolvedType = x->type; // parser typed it Int or Long by value/suffix
            x->isLvalue = false;
        }
        else if (auto x = std::dynamic_pointer_cast<StringLiterals>(expr))
        {
            const size_t size = decodedLength(x->literal);
            const auto type = CharType::getInstance();
            auto stringType = std::make_shared<ArrayType>(type, size);
            x->resolvedType = stringType;
            x->isLvalue = false;
        }
        else if (auto x = std::dynamic_pointer_cast<AssignExpr>(expr))
        {
            analyzeExpr(x->lhs);
            analyzeExpr(x->rhs);

            const auto lType = x->lhs->resolvedType;
            const auto rType = x->rhs->resolvedType;
            if (!x->lhs->isLvalue)
            {
                error(x->lhs->getLine(), x->lhs->getCol(),
                      "Left expression must be an lvalue, got rvalue of type: " +
                          lType->toString() + ".");
            }

            if (x->op == "=")
            {
                bool sameStruct = std::dynamic_pointer_cast<StructType>(lType) &&
                                  std::dynamic_pointer_cast<StructType>(rType) &&
                                  lType->equals(*rType);
                if (!(isScalar(lType) && isScalar(rType)) &&
                    !(isPointer(lType) && isNullPointerConstant(x->rhs)) && !sameStruct)
                {
                    error(x->lhs->getLine(), x->lhs->getCol(),
                          "Left expression and right expression are not same type, left type: " +
                              lType->toString() + ", right type: " + rType->toString() + ".");
                }
                else if (isInteger(lType) && isInteger(rType))
                {
                    x->rhs = convertTo(x->rhs, lType); // convert-by-assignment
                }
            }
            else if (x->op == "+=" || x->op == "-=")
            {
                if (!isScalar(lType))
                {
                    error(
                        x->lhs->getLine(), x->lhs->getCol(),
                        "Arithmetic assignment needs integer or pointer for left expression, got " +
                            lType->toString() + ".");
                }
                if (!isInteger(rType))
                {
                    error(x->rhs->getLine(), x->rhs->getCol(),
                          "Arithmetic assignment needs integer for right expression, got " +
                              rType->toString() + ".");
                }
            }
            else if (x->op == "*=" || x->op == "/=" || x->op == "%=")
            {
                if (!isInteger(lType))
                {
                    error(x->lhs->getLine(), x->lhs->getCol(),
                          "Arithmetic assignment needs integer for left expression, got " +
                              lType->toString() + ".");
                }
                if (!isInteger(rType))
                {
                    error(x->rhs->getLine(), x->rhs->getCol(),
                          "Arithmetic assignment needs integer for right expression, got " +
                              rType->toString() + ".");
                }
            }
            else if (x->op == "&=" || x->op == "^=" || x->op == "|=" || x->op == "<<=" ||
                     x->op == ">>=")
            {
                if (!isInteger(lType))
                {
                    error(x->lhs->getLine(), x->lhs->getCol(),
                          "Bitwise assignment needs integer for left expression, got " +
                              lType->toString() + ".");
                }
                if (!isInteger(rType))
                {
                    error(x->rhs->getLine(), x->rhs->getCol(),
                          "Bitwise assignment needs integer for right expression, got " +
                              rType->toString() + ".");
                }
            }

            // For a compound arithmetic/bitwise assignment the op is done in place at
            // the lhs width, so the rhs must be widened/narrowed to match (a `long +=
            // int` would otherwise read an 8-byte operand from a 4-byte slot). Shifts
            // are excluded: the count is taken via %cl regardless of its width.
            const bool compoundInPlace = x->op == "+=" || x->op == "-=" || x->op == "*=" ||
                                         x->op == "/=" || x->op == "%=" || x->op == "&=" ||
                                         x->op == "^=" || x->op == "|=";
            if (compoundInPlace && isInteger(lType) && isInteger(rType))
                x->rhs = convertTo(x->rhs, lType);

            x->resolvedType = x->lhs->resolvedType;
            x->isLvalue = false;
        }
        else if (auto x = std::dynamic_pointer_cast<BinaryExpr>(expr))
        {
            analyzeExpr(x->left);
            analyzeExpr(x->right);

            const auto lType = x->left->resolvedType;
            const auto rType = x->right->resolvedType;

            if (isArithmeticOp(x->binaryOp))
            {
                if (x->binaryOp == "+" || x->binaryOp == "-")
                {
                    if (!isScalar(lType))
                    {
                        error(x->left->getLine(), x->left->getCol(),
                              "Arithmetic operator needs integer or pointer for left expression, "
                              "got " +
                                  lType->toString() + ".");
                    }
                    if (!isScalar(rType))
                    {
                        error(x->right->getLine(), x->right->getCol(),
                              "Arithmetic operator needs integer or pointer for right expression, "
                              "got " +
                                  rType->toString() + ".");
                    }

                    bool leftIsPointer = isPointer(lType);
                    bool rightIsPointer = isPointer(rType);

                    if (!leftIsPointer && !rightIsPointer)
                    {
                        if (isInteger(lType) && isInteger(rType))
                        {
                            auto common = getCommonType(lType, rType);
                            x->left = convertTo(x->left, common);
                            x->right = convertTo(x->right, common);
                            x->resolvedType = common;
                        }
                        else
                        {
                            x->resolvedType = IntType::getInstance();
                        }
                    }
                    else if (leftIsPointer && !rightIsPointer)
                    {
                        x->resolvedType = lType;
                    }
                    else if (!leftIsPointer && rightIsPointer)
                    {
                        if (x->binaryOp == "-")
                        {
                            error(x->right->getLine(), x->right->getCol(),
                                  "'-' Cannot have rhs as a pointer, got " + rType->toString() +
                                      ".");
                        }
                        x->resolvedType = rType;
                    }
                    else if (leftIsPointer && rightIsPointer)
                    {
                        if (x->binaryOp == "+")
                        {
                            error(x->right->getLine(), x->right->getCol(),
                                  "'+' Cannot add pointers, got " + rType->toString() + ".");
                        }
                        if (!lType->equals(*rType) && x->binaryOp == "-")
                        {
                            error(x->right->getLine(), x->right->getCol(),
                                  "Need pointers of same type for subtraction, got lhs:" +
                                      lType->toString() + ", rhs " + rType->toString() + ".");
                        }
                        x->resolvedType = IntType::getInstance();
                    }

                    x->isLvalue = false;
                }
                else
                {
                    if (!isInteger(lType))
                    {
                        error(x->left->getLine(), x->left->getCol(),
                              "Arithmetic operator needs integer for left expression, got " +
                                  lType->toString() + ".");
                    }
                    if (!isInteger(rType))
                    {
                        error(x->right->getLine(), x->right->getCol(),
                              "Arithmetic operator needs integer for right expression, got " +
                                  rType->toString() + ".");
                    }
                    if (isInteger(lType) && isInteger(rType))
                    {
                        auto common = getCommonType(lType, rType);
                        x->left = convertTo(x->left, common);
                        x->right = convertTo(x->right, common);
                        x->resolvedType = common;
                    }
                    else
                    {
                        x->resolvedType = IntType::getInstance();
                    }
                    x->isLvalue = false;
                }
            }
            else if (isComparisonOp(x->binaryOp))
            {
                const bool isEquality = (x->binaryOp == "==" || x->binaryOp == "!=");

                if (!isScalar(lType))
                {
                    error(x->left->getLine(), x->left->getCol(),
                          "Comparison operator needs integer or pointer for left expression, got " +
                              lType->toString() + ".");
                }
                if (!isScalar(rType))
                {
                    error(
                        x->right->getLine(), x->right->getCol(),
                        "Comparison operator needs integer or pointer for right expression, got " +
                            rType->toString() + ".");
                }

                if (isScalar(lType) && isScalar(rType))
                {
                    const bool bothArith = isInteger(lType) && isInteger(rType);
                    const bool bothPtr = isPointer(lType) && isPointer(rType);

                    if (bothPtr && !lType->equals(*rType))
                    {
                        error(x->right->getLine(), x->right->getCol(),
                              "Comparison operator needs matching pointer types, left: " +
                                  lType->toString() + ", right: " + rType->toString() + ".");
                    }
                    else if (!bothArith && !bothPtr)
                    {
                        const bool nullOK =
                            isEquality && ((isPointer(lType) && isNullPointerConstant(x->right)) ||
                                           (isPointer(rType) && isNullPointerConstant(x->left)));
                        if (!nullOK)
                        {
                            error(x->right->getLine(), x->right->getCol(),
                                  "Comparison operator needs both arithmetic or both pointer, "
                                  "left: " +
                                      lType->toString() + ", right: " + rType->toString() + ".");
                        }
                    }
                }

                if (isInteger(lType) && isInteger(rType))
                {
                    auto common = getCommonType(lType, rType);
                    x->left = convertTo(x->left, common);
                    x->right = convertTo(x->right, common);
                }

                x->resolvedType = IntType::getInstance();
                x->isLvalue = false;
            }
            else if (isLogicalOp(x->binaryOp))
            {
                if (!isScalar(lType))
                {
                    error(x->left->getLine(), x->left->getCol(),
                          "Logical operator needs integer or pointer for left expression, got " +
                              lType->toString() + ".");
                }
                if (!isScalar(rType))
                {
                    error(x->right->getLine(), x->right->getCol(),
                          "Logical operator needs integer or pointer for right expression, got " +
                              rType->toString() + ".");
                }

                x->resolvedType = IntType::getInstance();
                x->isLvalue = false;
            }
            else if (isBitwiseOp(x->binaryOp))
            {
                if (!isInteger(lType))
                {
                    error(x->left->getLine(), x->left->getCol(),
                          "Bitwise operator needs integer for left expression, got " +
                              lType->toString() + ".");
                }
                if (!isInteger(rType))
                {
                    error(x->right->getLine(), x->right->getCol(),
                          "Bitwise operator needs integer for right expression, got " +
                              rType->toString() + ".");
                }

                if (!(isInteger(lType) && isInteger(rType)))
                {
                    x->resolvedType = IntType::getInstance();
                }
                else if (x->binaryOp == "<<" || x->binaryOp == ">>")
                {
                    // Shift result is the left operand's type; the shift count type
                    // is independent, so the operands are not brought to a common type.
                    x->resolvedType = lType;
                }
                else
                {
                    auto common = getCommonType(lType, rType);
                    x->left = convertTo(x->left, common);
                    x->right = convertTo(x->right, common);
                    x->resolvedType = common;
                }
                x->isLvalue = false;
            }
            else if (isComma(x->binaryOp))
            {
                x->resolvedType = rType;
                x->isLvalue = x->right->isLvalue;
            }
        }
        else if (auto x = std::dynamic_pointer_cast<CastExpr>(expr))
        {
            analyzeExpr(x->operand);

            if (!isScalar(x->operand->resolvedType) || (!isScalar(x->type) && !isVoid(x->type)))
            {
                error(x->getLine(), x->getCol(),
                      "Cannot cast to or from struct, got " + x->operand->resolvedType->toString() +
                          " and " + x->type->toString() + ".");
            }

            x->resolvedType = x->type;
            x->isLvalue = false;
        }
        else if (auto x = std::dynamic_pointer_cast<FunctionCallExpr>(expr))
        {
            analyzeFunctionCallExpr(x);
        }
        else if (auto x = std::dynamic_pointer_cast<InitExpr>(expr))
        {
            for (const auto &element : x->elements)
            {
                analyzeExpr(element);
            }
            x->resolvedType = IntType::getInstance();
            x->isLvalue = false;
        }
        else if (auto x = std::dynamic_pointer_cast<MemberExpr>(expr))
        {
            analyzeExpr(x->object);
            const auto &objType = x->object->resolvedType;
            const int objLine = x->object->getLine();
            const int objCol = x->object->getCol();

            auto recoverAsInt = [&]()
            {
                x->resolvedType = IntType::getInstance();
                x->isLvalue = true;
            };

            if (x->isArrow && !isPointer(objType))
            {
                error(objLine, objCol,
                      "'->' needs pointer, received: " + objType->toString() + ".");
            }
            else if (!x->isArrow && isPointer(objType))
            {
                error(objLine, objCol, "'.' needs object, received: " + objType->toString() + ".");
            }

            std::shared_ptr<StructType> baseType;
            if (auto ptr = std::dynamic_pointer_cast<PointerType>(objType))
                baseType = std::dynamic_pointer_cast<StructType>(ptr->getInner());
            else
                baseType = std::dynamic_pointer_cast<StructType>(objType);

            if (!baseType)
            {
                error(objLine, objCol,
                      "expected struct or pointer to struct, got " + objType->toString() + ".");
                recoverAsInt();
                return;
            }

            auto baseSymbol = symbolTable.find(baseType->getName(), Kind::STRUCT_TAG);
            if (!baseSymbol)
            {
                error(objLine, objCol, "struct not defined: " + baseType->getName() + ".");
                recoverAsInt();
                return;
            }

            auto structNode = std::dynamic_pointer_cast<StructDecl>(baseSymbol->node.lock());
            // should never fire.
            if (!structNode)
            {
                error(objLine, objCol,
                      "internal: struct '" + baseType->getName() +
                          "' has no associated declaration.");
                recoverAsInt();
                return;
            }

            auto it = std::find_if(structNode->fields.begin(), structNode->fields.end(),
                                   [&](const StructField &f) { return f.name == x->field; });

            if (it == structNode->fields.end())
            {
                error(x->getLine(), x->getCol(),
                      "member '" + x->field + "' not defined in struct '" + structNode->name +
                          "'.");
                recoverAsInt();
                return;
            }

            x->resolvedType = it->type;
            x->isLvalue = true;
        }
        else if (auto x = std::dynamic_pointer_cast<SizeOfExpr>(expr))
        {
            if (x->expr)
                analyzeExpr(x->expr);

            if (auto st = std::dynamic_pointer_cast<StructType>(x->type))
            {
                if (!symbolTable.find(st->getName(), Kind::STRUCT_TAG))
                {
                    error(x->getLine(), x->getCol(),
                          "sizeof references undeclared struct '" + st->getName() + "'.");
                }
            }

            x->resolvedType = IntType::getInstance();
            x->isLvalue = false;
        }
        else if (auto x = std::dynamic_pointer_cast<SubscriptExpr>(expr))
        {
            analyzeExpr(x->lvalue);
            analyzeExpr(x->index);

            if (!isInteger(x->index->resolvedType))
            {
                error(x->index->getLine(), x->index->getCol(),
                      "required integer index, received '" + x->index->resolvedType->toString() +
                          "'.");
            }

            auto lt = x->lvalue->resolvedType;
            std::shared_ptr<Type> finalType = IntType::getInstance();
            if (auto arr = std::dynamic_pointer_cast<ArrayType>(lt))
                finalType = arr->getInner();
            else if (auto ptr = std::dynamic_pointer_cast<PointerType>(lt))
                finalType = ptr->getInner();
            else
            {
                error(x->getLine(), x->getCol(),
                      "required pointer or array type, received '" +
                          x->lvalue->resolvedType->toString() + "'.");
            }

            x->resolvedType = finalType;
            x->isLvalue = true;
        }
        else if (auto x = std::dynamic_pointer_cast<TernaryExpr>(expr))
        {
            analyzeExpr(x->condition);
            analyzeExpr(x->thenBranch);
            analyzeExpr(x->elseBranch);

            if (!isScalar(x->condition->resolvedType))
            {
                error(x->condition->getLine(), x->condition->getCol(),
                      "required integer or pointer condition, received '" +
                          x->condition->resolvedType->toString() + "'.");
            }

            const auto tType = x->thenBranch->resolvedType;
            const auto eType = x->elseBranch->resolvedType;

            if (tType->equals(*eType))
            {
                x->resolvedType = tType;
            }
            else if (isPointer(tType) && isNullPointerConstant(x->elseBranch))
            {
                x->resolvedType = tType;
            }
            else if (isPointer(eType) && isNullPointerConstant(x->thenBranch))
            {
                x->resolvedType = eType;
            }
            else if (isInteger(tType) && isInteger(eType))
            {
                auto common = getCommonType(tType, eType);
                x->thenBranch = convertTo(x->thenBranch, common);
                x->elseBranch = convertTo(x->elseBranch, common);
                x->resolvedType = common;
            }
            else
            {
                error(x->getLine(), x->getCol(),
                      "ternary branches have incompatible types, then: '" + tType->toString() +
                          "', else: '" + eType->toString() + "'.");
                x->resolvedType = tType;
            }

            x->isLvalue = false;
        }
        else if (auto x = std::dynamic_pointer_cast<UnaryExpr>(expr))
        {
            analyzeExpr(x->operand);
            if (x->op == "-" || x->op == "+" || x->op == "~")
            {
                if (!isInteger(x->operand->resolvedType))
                {
                    error(x->getLine(), x->getCol(),
                          "required integer operand, received '" +
                              x->operand->resolvedType->toString() + "'.");
                }
                // Negation/complement keep the operand's (promoted) type, so `-longVal`
                // stays long rather than being truncated to int.
                x->resolvedType = x->operand->resolvedType;
                x->isLvalue = false;
            }
            else if (x->op == "!")
            {
                if (!isScalar(x->operand->resolvedType))
                {
                    error(x->getLine(), x->getCol(),
                          "required integer or pointer operand, received '" +
                              x->operand->resolvedType->toString() + "'.");
                }
                x->resolvedType = IntType::getInstance();
                x->isLvalue = false;
            }
            else if (x->op == "*")
            {
                auto resolvedType = x->operand->resolvedType;
                if (!isPointer(resolvedType))
                {
                    error(x->getLine(), x->getCol(),
                          "required pointer operand, received '" +
                              x->operand->resolvedType->toString() + "'.");
                    // to make sure nothing crashes downstream
                    x->resolvedType = IntType::getInstance();
                    x->isLvalue = true;
                }
                if (const auto pointerType = std::dynamic_pointer_cast<PointerType>(resolvedType))
                {
                    x->resolvedType = pointerType->getInner();
                    x->isLvalue = true;
                }
            }
            else if (x->op == "&")
            {
                if (!x->operand->isLvalue)
                {
                    error(x->getLine(), x->getCol(),
                          "required lvalue, received rvalue of type '" +
                              x->operand->resolvedType->toString() + "'.");
                }
                x->resolvedType = std::make_shared<PointerType>(x->operand->resolvedType);
                x->isLvalue = false;
            }
            else if (x->op == "++" || x->op == "--")
            {
                if (!x->operand->isLvalue)
                {
                    error(x->getLine(), x->getCol(),
                          "required lvalue, received rvalue of type '" +
                              x->operand->resolvedType->toString() + "'.");
                }
                if (!isScalar(x->operand->resolvedType))
                {
                    error(x->getLine(), x->getCol(),
                          "required integer or pointer operand, received '" +
                              x->operand->resolvedType->toString() + "'.");
                }
                x->resolvedType = x->operand->resolvedType;
                x->isLvalue = false;
            }
        }
        else if (auto x = std::dynamic_pointer_cast<VariableExpr>(expr))
        {
            auto sym = symbolTable.find(x->name, Kind::VARIABLE);
            if (!sym)
                sym = symbolTable.find(x->name, Kind::PARAMETER);
            if (!sym)
                sym = symbolTable.find(x->name, Kind::FUNCTION);

            if (!sym)
            {
                error(x->getLine(), x->getCol(), "use of undeclared identifier '" + x->name + "'.");
                x->resolvedType = IntType::getInstance(); // placeholder so downstream doesn't crash
                x->isLvalue = false;
            }
            else
            {
                x->resolvedType = sym->type;
                x->isLvalue = sym->kind != Kind::FUNCTION;
                x->symbol = sym;
            }
        }
        else
        {
            throw std::runtime_error("Reached invalid expression at line " +
                                     std::to_string(expr->getLine()) + ", col " +
                                     std::to_string(expr->getCol()));
        }
    }

    void analyzeFunctionCallExpr(const std::shared_ptr<FunctionCallExpr> &expr)
    {
        std::string functionName = expr->functionName->name;
        const auto checkFunctionExistence = symbolTable.find(functionName, Kind::FUNCTION);
        if (checkFunctionExistence == nullptr)
        {
            error(expr->getLine(), expr->getCol(), "function " + functionName + " not declared");
            expr->resolvedType = IntType::getInstance();
        }
        else if (checkFunctionExistence->kind != Kind::FUNCTION)
        {
            error(expr->getLine(), expr->getCol(),
                  "'" + functionName + "' is not a function (declared as " +
                      kindToString(checkFunctionExistence->kind) + " at line " +
                      std::to_string(checkFunctionExistence->line) + ").");
            expr->resolvedType = IntType::getInstance();
        }
        else
        {
            auto functionType =
                std::dynamic_pointer_cast<FunctionType>(checkFunctionExistence->type);
            if ((functionType->isVariadic &&
                 functionType->paramTypes.size() > expr->parameters.size()) ||
                (!functionType->isVariadic &&
                 functionType->paramTypes.size() != expr->parameters.size()))
            {
                error(expr->getLine(), expr->getCol(),
                      "function call has " + std::to_string(expr->parameters.size()) +
                          " parameters. Expected " +
                          std::to_string(functionType->paramTypes.size()));
            }
            else
            {
                for (int i = 0; i < functionType->paramTypes.size(); i++)
                {
                    analyzeExpr(expr->parameters[i]);
                    auto argType = expr->parameters[i]->resolvedType;
                    const auto &paramType = functionType->paramTypes[i];
                    if (isInteger(argType) && isInteger(paramType))
                    {
                        expr->parameters[i] = convertTo(expr->parameters[i], paramType);
                    }
                    else if (!canDecayTo(argType, paramType))
                    {
                        error(expr->getLine(), expr->getCol(),
                              "mismatched param types, expected " + paramType->toString() +
                                  " got " + argType->toString());
                    }
                }
                for (int i = functionType->paramTypes.size(); i < expr->parameters.size(); i++)
                {
                    analyzeExpr(expr->parameters[i]);
                }
            }
            expr->resolvedType = functionType->returnType;
        }
        expr->isLvalue = false;
    }

    void analyzeExprStmt(const std::shared_ptr<ExprStmt> &exprStmt) { analyzeExpr(exprStmt->expr); }

    void analyzeStructDecl(const std::shared_ptr<StructDecl> &structDecl)
    {
        const auto structSymbol =
            std::make_shared<Symbol>(structDecl->name, structDecl->baseType, structDecl->getLine(),
                                     structDecl->getCol(), Kind::STRUCT_TAG);
        check(structDecl->name, structSymbol, Kind::STRUCT_TAG, structDecl->getLine(),
              structDecl->getCol(), structDecl);

        std::unordered_map<std::string, int> seen;
        for (const auto &f : structDecl->fields)
        {
            auto it = seen.find(f.name);
            if (it != seen.end())
            {
                error(f.line, f.column,
                      "redeclaration of struct field '" + f.name +
                          "'. Previous declaration at line " + std::to_string(it->second) + ".");
            }
            else
            {
                seen.emplace(f.name, f.line);
            }
        }
    }

    bool returnsAlways(const std::shared_ptr<Statement> &stmt)
    {
        if (!stmt)
            return false;
        if (std::dynamic_pointer_cast<ReturnStmt>(stmt))
            return true;
        if (auto b = std::dynamic_pointer_cast<BlockStmt>(stmt))
        {
            if (b->statements.empty())
                return false;
            return returnsAlways(b->statements.back());
        }
        if (auto i = std::dynamic_pointer_cast<IfStmt>(stmt))
        {
            if (!i->elseBlock)
                return false;
            return returnsAlways(i->thenBlock) && returnsAlways(i->elseBlock);
        }
        return false;
    }

    // Linkage from scope + storage class, with the extern-follows-prior rule:
    // an `extern` declaration takes the linkage of a prior *linked* declaration,
    // else external. File-scope plain → external; file-scope static → internal;
    // block-scope plain/static → no linkage.
    Linkage computeLinkage(std::optional<StorageClass> sc, bool fileScope,
                           const std::shared_ptr<Symbol> &prior)
    {
        const bool priorLinked = prior && prior->linkage != Linkage::None;
        if (fileScope)
        {
            if (sc == StorageClass::Static)
                return Linkage::Internal;
            if (sc == StorageClass::Extern)
                return priorLinked ? prior->linkage : Linkage::External;
            return Linkage::External;
        }
        if (sc == StorageClass::Extern)
            return priorLinked ? prior->linkage : Linkage::External;
        return Linkage::None;
    }

    // Fold an integer constant expression. Returns false for anything with a
    // runtime value (variable reads, calls, non-constant operands).
    bool evalConstInt(const std::shared_ptr<Expression> &e, long long &out)
    {
        if (auto lit = std::dynamic_pointer_cast<IntLiterals>(e))
        {
            try
            {
                out = lit->value;
            }
            catch (...)
            {
                return false;
            }
            return true;
        }
        if (auto u = std::dynamic_pointer_cast<UnaryExpr>(e))
        {
            long long v;
            if (!evalConstInt(u->operand, v))
                return false;
            if (u->op == "-")
            {
                out = -v;
                return true;
            }
            if (u->op == "+")
            {
                out = v;
                return true;
            }
            if (u->op == "~")
            {
                out = ~v;
                return true;
            }
            if (u->op == "!")
            {
                out = !v;
                return true;
            }
            return false;
        }
        if (auto b = std::dynamic_pointer_cast<BinaryExpr>(e))
        {
            long long l, r;
            if (!evalConstInt(b->left, l) || !evalConstInt(b->right, r))
                return false;
            const auto &op = b->binaryOp;
            if (op == "+")
            {
                out = l + r;
                return true;
            }
            if (op == "-")
            {
                out = l - r;
                return true;
            }
            if (op == "*")
            {
                out = l * r;
                return true;
            }
            if (op == "/")
            {
                if (r == 0)
                    return false;
                out = l / r;
                return true;
            }
            if (op == "%")
            {
                if (r == 0)
                    return false;
                out = l % r;
                return true;
            }
            if (op == "&")
            {
                out = l & r;
                return true;
            }
            if (op == "|")
            {
                out = l | r;
                return true;
            }
            if (op == "^")
            {
                out = l ^ r;
                return true;
            }
            if (op == "<<")
            {
                out = l << r;
                return true;
            }
            if (op == ">>")
            {
                out = l >> r;
                return true;
            }
            if (op == "<")
            {
                out = l < r;
                return true;
            }
            if (op == ">")
            {
                out = l > r;
                return true;
            }
            if (op == "<=")
            {
                out = l <= r;
                return true;
            }
            if (op == ">=")
            {
                out = l >= r;
                return true;
            }
            if (op == "==")
            {
                out = l == r;
                return true;
            }
            if (op == "!=")
            {
                out = l != r;
                return true;
            }
            if (op == "&&")
            {
                out = l && r;
                return true;
            }
            if (op == "||")
            {
                out = l || r;
                return true;
            }
            return false;
        }
        if (auto c = std::dynamic_pointer_cast<CastExpr>(e))
        {
            long long v;
            if (!evalConstInt(c->operand, v))
                return false;
            // Apply the cast's value conversion at compile time: narrowing to a
            // smaller integer truncates; widening to long keeps the value.
            if (std::dynamic_pointer_cast<IntType>(c->type))
                out = static_cast<int>(v);
            else if (std::dynamic_pointer_cast<CharType>(c->type))
                out = static_cast<signed char>(v);
            else
                out = v;
            return true;
        }
        return false;
    }

    // Is `e` a valid initializer for a static-duration object? Integer constant
    // expressions, string literals, null, address-of-a-name, and constant
    // brace lists qualify; anything with a runtime value does not.
    bool isConstantInitializer(const std::shared_ptr<Expression> &e)
    {
        long long tmp;
        if (evalConstInt(e, tmp))
            return true;
        if (std::dynamic_pointer_cast<StringLiterals>(e))
            return true;
        if (isNullPointerConstant(e))
            return true;
        if (auto u = std::dynamic_pointer_cast<UnaryExpr>(e); u && u->op == "&")
            return std::dynamic_pointer_cast<VariableExpr>(u->operand) != nullptr;
        if (auto in = std::dynamic_pointer_cast<InitExpr>(e))
        {
            for (const auto &el : in->elements)
                if (!isConstantInitializer(el))
                    return false;
            return true;
        }
        return false;
    }

    // Declare a variable with linkage/duration handling. Merges linked
    // redeclarations (file-scope vars, block-scope extern) into one symbol and
    // rejects conflicts; no-linkage locals follow the ordinary redeclaration rule.
    void declareVariable(const std::shared_ptr<VarDecl> &var, bool fileScope,
                         std::optional<StorageClass> sc, bool hasInit)
    {
        const std::string &name = var->name;
        auto prior = symbolTable.find(name, Kind::VARIABLE);
        const Linkage linkage = computeLinkage(sc, fileScope, prior);
        // File-scope vars, block statics, and any `extern` (which refers to a
        // static-duration object defined elsewhere) all have static duration.
        const StorageDuration dur =
            (fileScope || sc == StorageClass::Static || sc == StorageClass::Extern)
                ? StorageDuration::Static
                : StorageDuration::Automatic;

        if (linkage != Linkage::None)
        {
            // A linked variable refers to one entity TU-wide; reconcile in the
            // linkage registry, which is separate from name-resolution scopes.
            auto g = symbolTable.findLinked(name);
            if (g)
            {
                if (g->kind != Kind::VARIABLE)
                {
                    error(var->getLine(), var->getCol(),
                          "'" + name + "' redeclared as different kind (" + kindToString(g->kind) +
                              " at line " + std::to_string(g->line) + ").");
                    return;
                }
                if (!g->type->equals(*var->type))
                    error(var->getLine(), var->getCol(),
                          "conflicting types for '" + name + "' (was " + g->type->toString() +
                              ", now " + var->type->toString() + ").");
                if (g->linkage != linkage)
                    error(var->getLine(), var->getCol(),
                          "conflicting linkage for '" + name + "' (previous declaration at line " +
                              std::to_string(g->line) + ").");
                if (hasInit)
                {
                    if (g->defined)
                        error(var->getLine(), var->getCol(),
                              "redefinition of '" + name + "' (previous definition at line " +
                                  std::to_string(g->line) + ").");
                    g->defined = true;
                    g->tentative = false;
                    g->node = var;
                }
                else if (sc != StorageClass::Extern && !g->defined)
                {
                    g->tentative = true;
                }
                var->symbol = g;
            }
            else
            {
                auto sym = std::make_shared<Symbol>(name, var->type, var->getLine(), var->getCol(),
                                                    Kind::VARIABLE);
                sym->linkage = linkage;
                sym->duration = dur;
                if (hasInit)
                    sym->defined = true;
                else if (sc != StorageClass::Extern)
                    sym->tentative = true;
                symbolTable.insertLinked(name, sym);
                sym->node = var;
                var->symbol = sym;
            }

            // Make it *visible* in the current scope (file scope for a file-scope
            // var, the block for a block-scope extern). A same-scope no-linkage
            // declaration of the same name conflicts.
            auto same = symbolTable.findSameScope(name, Kind::VARIABLE);
            if (!same)
                symbolTable.bindCurrent(name, var->symbol, Kind::VARIABLE);
            else if (same->linkage == Linkage::None)
                error(var->getLine(), var->getCol(),
                      "conflicting declaration of '" + name + "' (previous declaration at line " +
                          std::to_string(same->line) + ").");
            return;
        }

        // No linkage: block-scope local (plain or `static`).
        auto sym = std::make_shared<Symbol>(name, var->type, var->getLine(), var->getCol(),
                                            Kind::VARIABLE);
        sym->linkage = Linkage::None;
        sym->duration = dur;
        if (dur == StorageDuration::Static)
            sym->defined = true; // a block-scope static always defines (zero-init) storage
        if (!symbolTable.insert(name, sym, Kind::VARIABLE))
        {
            auto existing = symbolTable.findSameScope(name, Kind::VARIABLE);
            error(var->getLine(), var->getCol(),
                  "redeclaration of variable '" + name + "'" +
                      (existing ? " (previous declaration at line " +
                                      std::to_string(existing->line) + ")"
                                : "") +
                      ".");
            return;
        }
        sym->node = var;
        var->symbol = sym;
    }

    void analyzeVarDecl(const std::shared_ptr<VarDecl> &variable)
    {
        const bool fileScope = variable->global;
        const auto sc = variable->storageClass;
        const bool hasInit = variable->initialization != nullptr;

        if (!fileScope && sc == StorageClass::Extern && hasInit)
            error(variable->getLine(), variable->getCol(),
                  "block-scope 'extern' variable '" + variable->name +
                      "' cannot have an initializer.");

        declareVariable(variable, fileScope, sc, hasInit);

        if (variable->initialization)
        {
            analyzeExpr(variable->initialization);

            // Static-duration variables require a constant initializer.
            if (variable->symbol && variable->symbol->duration == StorageDuration::Static)
            {
                long long val;
                if (!isConstantInitializer(variable->initialization))
                    error(variable->getLine(), variable->getCol(),
                          "initializer for '" + variable->name +
                              "' with static storage duration must be a constant expression.");
                else if (evalConstInt(variable->initialization, val))
                {
                    // Narrow the folded constant to the declared width at compile time
                    // (e.g. `static int g = 4294967296L;` stores 0, not the full value).
                    if (std::dynamic_pointer_cast<IntType>(variable->type))
                        val = static_cast<int>(val);
                    else if (std::dynamic_pointer_cast<CharType>(variable->type))
                        val = static_cast<signed char>(val);
                    variable->symbol->constInit = val;
                }
            }

            if (auto x = std::dynamic_pointer_cast<InitExpr>(variable->initialization))
            {
                if (!isArray(variable->type))
                {
                    error(variable->getLine(), variable->getCol(),
                          "Init expression requires array as LHS, got " +
                              variable->type->toString());
                    return;
                }
                std::shared_ptr<ArrayType> initArrayType =
                    std::dynamic_pointer_cast<ArrayType>(variable->type);
                auto innerType = initArrayType->getInner();
                for (const auto &element : x->elements)
                {
                    if (!innerType->equals(*element->resolvedType))
                    {
                        error(variable->getLine(), variable->getCol(),
                              "Mismatch in init elements, expected " + innerType->toString() +
                                  " received, " + element->resolvedType->toString());
                    }
                }
                if (initArrayType->getSize() < x->elements.size())
                {
                    error(variable->getLine(), variable->getCol(),
                          "Mismatch in init elements, expected " +
                              std::to_string(initArrayType->getSize()) + " received, " +
                              std::to_string(x->elements.size()));
                }
            }
            else
            {
                auto lType = variable->type;
                auto rType = variable->initialization->resolvedType;

                auto lArr = std::dynamic_pointer_cast<ArrayType>(lType);
                auto rArr = std::dynamic_pointer_cast<ArrayType>(rType);
                bool stringInit = lArr && rArr && isInteger(lArr->getInner()) &&
                                  isInteger(rArr->getInner()) && rArr->getSize() <= lArr->getSize();

                bool sameStruct = std::dynamic_pointer_cast<StructType>(lType) &&
                                  std::dynamic_pointer_cast<StructType>(rType) &&
                                  lType->equals(*rType);

                const bool staticDuration =
                    variable->symbol && variable->symbol->duration == StorageDuration::Static;

                if (!(isScalar(lType) && (isScalar(rType) || canDecayTo(rType, lType))) &&
                    !(isPointer(lType) && isNullPointerConstant(variable->initialization)) &&
                    !stringInit && !sameStruct)
                {
                    error(variable->getLine(), variable->getCol(),
                          "Left expression and right expression are not same type, left type: " +
                              lType->toString() + ", right type: " + rType->toString() + ".");
                }
                else if (isInteger(lType) && isInteger(rType) && !staticDuration)
                {
                    // Convert-by-assignment for automatic vars; static initializers
                    // are folded as constants above and must stay cast-free.
                    variable->initialization = convertTo(variable->initialization, lType);
                }
            }
        }
    }

    void analyzeDeclareStmt(const std::shared_ptr<DeclareStmt> &stmt)
    {
        if (std::holds_alternative<std::vector<std::shared_ptr<VarDecl>>>(stmt->variables))
        {
            for (const auto &variables =
                     std::get<std::vector<std::shared_ptr<VarDecl>>>(stmt->variables);
                 auto &var : variables)
            {
                analyzeVarDecl(var);
            }
        }
        else
        {
            const auto &structDecl = std::get<std::shared_ptr<StructDecl>>(stmt->variables);
            analyzeStructDecl(structDecl);
        }
    }

    void analyzeReturnStmt(const std::shared_ptr<ReturnStmt> &stmt)
    {
        if (!currentReturnType)
        {
            error(stmt->getLine(), stmt->col, "return statement not inside a function.");
            return;
        }
        if (stmt->returnExpression != nullptr)
        {
            analyzeExpr(stmt->returnExpression);
            const auto retType = stmt->returnExpression->resolvedType;
            if (isInteger(currentReturnType) && isInteger(retType))
            {
                stmt->returnExpression = convertTo(stmt->returnExpression, currentReturnType);
            }
            else if (!currentReturnType->equals(*retType))
            {
                error(stmt->returnExpression->getLine(), stmt->returnExpression->col,
                      "expected type - " + currentReturnType->toString() + ", got " +
                          retType->toString());
            }
        }
        else
        {
            if (auto x = std::dynamic_pointer_cast<VoidType>(currentReturnType))
            {
                error(stmt->line, stmt->col,
                      "Function with void return type has non-void return type");
            }
        }
    }

    void analyzeIfStmt(const std::shared_ptr<IfStmt> &ifStmt)
    {
        analyzeExpr(ifStmt->condition);

        if (!isScalar(ifStmt->condition->resolvedType))
        {
            error(ifStmt->condition->getLine(), ifStmt->condition->getCol(),
                  "expected pointer or integer type, got - " +
                      ifStmt->condition->resolvedType->toString());
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
        int prevLoopLabel = loopLabel;
        loopLabel = ++labelCounter;
        whileStmt->label = loopLabel;
        analyzeExpr(whileStmt->condition);

        if (!isScalar(whileStmt->condition->resolvedType))
        {
            error(whileStmt->condition->getLine(), whileStmt->condition->getCol(),
                  "expected pointer or integer type, got - " +
                      whileStmt->condition->resolvedType->toString());
        }

        symbolTable.enterScope();
        analyzeStatements(whileStmt->whileBlock);
        symbolTable.exitScope();
        loopLabel = prevLoopLabel;
    }

    void analyzeDoWhileStmt(const std::shared_ptr<DoWhileStmt> &doWhileStmt)
    {
        int prevLoopLabel = loopLabel;
        loopLabel = ++labelCounter;
        doWhileStmt->label = loopLabel;
        analyzeExpr(doWhileStmt->condition);

        if (!isScalar(doWhileStmt->condition->resolvedType))
        {
            error(doWhileStmt->condition->getLine(), doWhileStmt->condition->getCol(),
                  "expected pointer or integer type, got - " +
                      doWhileStmt->condition->resolvedType->toString());
        }

        symbolTable.enterScope();
        analyzeStatements(doWhileStmt->block);
        symbolTable.exitScope();
        loopLabel = prevLoopLabel;
    }

    void analyzeForStmt(const std::shared_ptr<ForStmt> &forStmt)
    {
        int prevLoopLabel = loopLabel;
        loopLabel = ++labelCounter;
        forStmt->label = loopLabel;
        symbolTable.enterScope();

        if (auto x = std::dynamic_pointer_cast<ExprStmt>(forStmt->initialization))
        {
            analyzeExprStmt(x);
        }
        else if (auto d = std::dynamic_pointer_cast<DeclareStmt>(forStmt->initialization))
        {
            analyzeDeclareStmt(d);
        }
        if (auto x = std::dynamic_pointer_cast<ExprStmt>(forStmt->condition))
        {
            analyzeExprStmt(x);
            if (!isScalar(x->expr->resolvedType))
            {
                error(x->expr->getLine(), x->expr->getCol(),
                      "condition in for loop must be pointer or integer got - " +
                          x->expr->resolvedType->toString());
            }
        }
        if (auto x = std::dynamic_pointer_cast<ExprStmt>(forStmt->update))
        {
            analyzeExprStmt(x);
        }

        symbolTable.enterScope();
        analyzeStatements(forStmt->forBlock);
        symbolTable.exitScope();

        symbolTable.exitScope();
        loopLabel = prevLoopLabel;
    }

    void analyzeBreakContinueStmt(const std::shared_ptr<Statement> &stmt)
    {
        if (loopLabel <= 0)
        {
            error(stmt->getLine(), stmt->getCol(), "no loop statements found");
        }
        if (auto p = std::dynamic_pointer_cast<BreakStmt>(stmt))
        {
            p->label = loopLabel;
        }
        if (auto p = std::dynamic_pointer_cast<ContinueStmt>(stmt))
        {
            p->label = loopLabel;
        }
    }

    void analyzeFunctionDeclStmt(const std::shared_ptr<FunctionDeclStmt> &stmt)
    {
        if (stmt->declaration->storageClass == StorageClass::Static)
            error(stmt->declaration->getLine(), stmt->declaration->getCol(),
                  "block-scope function '" + stmt->declaration->name +
                      "' cannot be declared 'static'.");

        std::vector<std::shared_ptr<Type>> paramTypes;
        paramTypes.reserve(stmt->declaration->parameters.size());
        for (const auto &param : stmt->declaration->parameters)
        {
            paramTypes.push_back(param.type);
        }
        auto functionType = std::make_shared<FunctionType>(stmt->declaration->type, paramTypes,
                                                           stmt->declaration->variadic);
        declareFunction(stmt->declaration, functionType);
    }

    void analyzeStatements(const std::shared_ptr<BlockStmt> &blockStmt)
    {
        for (const auto &statement : blockStmt->statements)
        {
            if (auto x = std::dynamic_pointer_cast<DeclareStmt>(statement))
            {
                analyzeDeclareStmt(x);
            }
            else if (auto x = std::dynamic_pointer_cast<ExprStmt>(statement))
            {
                analyzeExprStmt(x);
            }
            else if (auto x = std::dynamic_pointer_cast<BlockStmt>(statement))
            {
                symbolTable.enterScope();
                analyzeStatements(x);
                symbolTable.exitScope();
            }
            else if (auto x = std::dynamic_pointer_cast<ReturnStmt>(statement))
            {
                analyzeReturnStmt(x);
            }
            else if (auto x = std::dynamic_pointer_cast<IfStmt>(statement))
            {
                analyzeIfStmt(x);
            }
            else if (auto x = std::dynamic_pointer_cast<WhileStmt>(statement))
            {
                analyzeWhileStmt(x);
            }
            else if (auto x = std::dynamic_pointer_cast<DoWhileStmt>(statement))
            {
                analyzeDoWhileStmt(x);
            }
            else if (auto x = std::dynamic_pointer_cast<ForStmt>(statement))
            {
                analyzeForStmt(x);
            }
            else if (auto x = std::dynamic_pointer_cast<BreakStmt>(statement))
            {
                analyzeBreakContinueStmt(x);
            }
            else if (auto x = std::dynamic_pointer_cast<ContinueStmt>(statement))
            {
                analyzeBreakContinueStmt(x);
            }
            else if (auto x = std::dynamic_pointer_cast<FunctionDeclStmt>(statement))
            {
                analyzeFunctionDeclStmt(x);
            }
        }
    }

    void analyzeFunction(std::shared_ptr<Function> &node)
    {
        auto prev = currentReturnType;
        currentReturnType = node->type;

        std::vector<std::shared_ptr<Type>> paramTypes;
        paramTypes.reserve(node->parameters.size());
        for (const auto &param : node->parameters)
        {
            paramTypes.push_back(param.type);
        }
        auto functionType = std::make_shared<FunctionType>(node->type, paramTypes, node->variadic);
        declareFunction(node, functionType);
        symbolTable.enterScope();
        for (auto &param : node->parameters)
        {
            const auto paramSymbol = std::make_shared<Symbol>(param.name, param.type, param.line,
                                                              param.col, Kind::PARAMETER);
            if (!symbolTable.insert(param.name, paramSymbol, Kind::PARAMETER))
            {
                error(param.line, param.col, "duplicate parameter '" + param.name + "'");
            }
        }

        if (node->name == "main")
        {
            auto &stmts = node->statements->statements;
            if (stmts.empty() || dynamic_cast<ReturnStmt *>(stmts.back().get()) == nullptr)
            {
                stmts.push_back(std::make_shared<ReturnStmt>(
                    -1, -1, std::make_shared<IntLiterals>(-1, -1, 0, IntType::getInstance())));
            }
        }

        if (node->statements)
        {
            analyzeStatements(node->statements);
            // if (!isVoid(node->type) && !returnsAlways(node->statements))
            // {
            //     error(node->getLine(), node->getCol(),
            //           "not all control flow paths in '" + node->name + "' return a value.");
            // }
        }

        symbolTable.exitScope();
        currentReturnType = prev;
    }

    void validate(const std::shared_ptr<Program> &program)
    {
        for (const auto &node : program->nodes)
        {
            if (auto x = std::dynamic_pointer_cast<Function>(node))
            {
                analyzeFunction(x);
            }
            else if (auto x = std::dynamic_pointer_cast<VarDecl>(node))
            {
                analyzeVarDecl(x);
            }
            else if (auto x = std::dynamic_pointer_cast<StructDecl>(node))
            {
                analyzeStructDecl(x);
            }
            else
            {
                throw std::runtime_error("Should not be possible to come here");
            }
        }
    }
};

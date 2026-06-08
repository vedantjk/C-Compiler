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
           std::dynamic_pointer_cast<CharType>(t) != nullptr ||
           std::dynamic_pointer_cast<UnsignedIntType>(t) != nullptr ||
           std::dynamic_pointer_cast<UnsignedLongType>(t) != nullptr;
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

inline bool isDouble(const std::shared_ptr<Type> &t)
{
    return std::dynamic_pointer_cast<DoubleType>(t) != nullptr;
}

inline bool isScalar(const std::shared_ptr<Type> &t)
{
    return isInteger(t) || isPointer(t) || isDouble(t);
}

inline bool isUnsigned(const std::shared_ptr<Type> &t)
{
    return std::dynamic_pointer_cast<UnsignedIntType>(t) != nullptr ||
           std::dynamic_pointer_cast<UnsignedLongType>(t) != nullptr;
}

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
    if (std::dynamic_pointer_cast<UnsignedIntType>(t))
        return 4;
    if (std::dynamic_pointer_cast<UnsignedLongType>(t))
        return 8;
    if (std::dynamic_pointer_cast<PointerType>(t))
        return 8;
    return 0;
}

// Common type of two arithmetic operands (callers gate on isInteger), i.e. the
// usual arithmetic conversions. Same signedness: the wider type wins. Mixed
// signedness: the unsigned type wins unless the signed type is strictly wider
// (a wider signed type represents all the unsigned values, so long + unsigned
// int -> long).
inline std::shared_ptr<Type> getCommonType(const std::shared_ptr<Type> &a,
                                           const std::shared_ptr<Type> &b)
{
    if (a->equals(*b))
        return a;

    if (isDouble(a) || isDouble(b))
    {
        return DoubleType::getInstance();
    }

    const int sizeA = typeSize(a), sizeB = typeSize(b);
    if (isUnsigned(a) == isUnsigned(b))
        return sizeA >= sizeB ? a : b;

    const auto &uns = isUnsigned(a) ? a : b;
    const auto &sgn = isUnsigned(a) ? b : a;
    const int unsSize = isUnsigned(a) ? sizeA : sizeB;
    const int sgnSize = isUnsigned(a) ? sizeB : sizeA;
    return sgnSize > unsSize ? sgn : uns;
}

// Reduce a folded constant (held in a 64-bit bit-bucket) to the value an object
// of the given type actually stores: 4-byte types keep their low 32 bits (signed
// or unsigned), char its low 8, 8-byte types keep all 64.
inline long long reduceToType(long long v, const std::shared_ptr<Type> &t)
{
    if (std::dynamic_pointer_cast<IntType>(t))
        return static_cast<int>(v);
    if (std::dynamic_pointer_cast<UnsignedIntType>(t))
        return static_cast<unsigned int>(v);
    if (std::dynamic_pointer_cast<CharType>(t))
        return static_cast<signed char>(v);
    return v;
}

// Object size in bytes; arrays recurse (count * element size).
inline long long sizeOfTypeBytes(const std::shared_ptr<Type> &t)
{
    if (const auto a = std::dynamic_pointer_cast<ArrayType>(t))
        return static_cast<long long>(a->getSize()) * sizeOfTypeBytes(a->getInner());
    if (std::dynamic_pointer_cast<CharType>(t))
        return 1;
    if (std::dynamic_pointer_cast<IntType>(t) || std::dynamic_pointer_cast<UnsignedIntType>(t))
        return 4;
    return 8; // long, unsigned long, double, pointer
}

// Natural alignment; arrays align like their element, but objects >= 16 bytes get
// 16-byte alignment (SysV / book rule).
inline int alignOfTypeBytes(const std::shared_ptr<Type> &t)
{
    if (const auto a = std::dynamic_pointer_cast<ArrayType>(t))
        return sizeOfTypeBytes(t) >= 16 ? 16 : alignOfTypeBytes(a->getInner());
    return static_cast<int>(sizeOfTypeBytes(t));
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

    // C's assignment conversions, shared by `=`, initializers, `return`, and call
    // arguments: an rvalue is assignable to a target when the types are equal, both
    // are arithmetic, or the target is a pointer and the rvalue is a null pointer
    // constant. Notably int<->pointer, double<->pointer, and mismatched pointer
    // types are NOT assignable. (Array-to-pointer decay is handled separately by
    // callers via canDecayTo.)
    bool isAssignmentCompatible(const std::shared_ptr<Type> &target,
                                const std::shared_ptr<Expression> &rhs)
    {
        const auto &src = rhs->resolvedType;
        if (src->equals(*target))
            return true;
        if ((isInteger(src) || isDouble(src)) && (isInteger(target) || isDouble(target)))
            return true;
        if (isPointer(target) && isNullPointerConstant(rhs))
            return true;
        return false;
    }

    // Validate an assignment-style conversion and, when valid, return the rvalue
    // converted to the target type (arithmetic conversion or null-constant ->
    // pointer). On a type mismatch, report an error and return the rvalue as-is.
    std::shared_ptr<Expression> convertByAssignment(const std::shared_ptr<Expression> &e,
                                                    const std::shared_ptr<Type> &target, int line,
                                                    int col)
    {
        if (!isAssignmentCompatible(target, e))
        {
            error(line, col,
                  "cannot convert " + e->resolvedType->toString() + " to " + target->toString());
            return e;
        }
        return convertTo(e, target);
    }

    // Array-to-pointer decay: in any value context an array becomes a pointer to
    // its first element. We synthesize &arr typed as pointer-to-element (same
    // address, scalar type) so the rest of the pipeline reuses the address-of
    // path. Non-array expressions pass through unchanged.
    std::shared_ptr<Expression> decayArray(const std::shared_ptr<Expression> &e)
    {
        const auto arr = std::dynamic_pointer_cast<ArrayType>(e->resolvedType);
        if (!arr)
            return e;
        auto addr = std::make_shared<UnaryExpr>(e->getLine(), e->getCol(), "&", e);
        addr->resolvedType = std::make_shared<PointerType>(arr->getInner());
        addr->isLvalue = false;
        return addr;
    }

    // analyzeExpr followed by array-to-pointer decay, for sub-expressions used as
    // rvalues. Contexts that need the array itself (operands of `&`, `sizeof`, and
    // the target of an assignment) call analyzeExpr directly instead.
    void analyzeAndDecay(std::shared_ptr<Expression> &slot)
    {
        analyzeExpr(slot);
        slot = decayArray(slot);
    }

    // A parameter declared with array type is adjusted to a pointer to its element
    // type (the outermost dimension is dropped). Applied before the function type
    // is built so that declarations differing only in that dimension still match.
    std::shared_ptr<Type> adjustParamType(const std::shared_ptr<Type> &t)
    {
        if (const auto arr = std::dynamic_pointer_cast<ArrayType>(t))
            return std::make_shared<PointerType>(arr->getInner());
        return t;
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
        else if (auto x = std::dynamic_pointer_cast<FloatingLiterals>(expr))
        {
            x->resolvedType = x->type;
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
            analyzeAndDecay(x->rhs);

            const auto lType = x->lhs->resolvedType;
            const auto rType = x->rhs->resolvedType;
            if (!x->lhs->isLvalue)
            {
                error(x->lhs->getLine(), x->lhs->getCol(),
                      "Left expression must be an lvalue, got rvalue of type: " +
                          lType->toString() + ".");
            }
            // Arrays are non-modifiable lvalues: assigning to one (or to a
            // dereferenced pointer-to-array) is a constraint violation.
            if (isArray(lType))
            {
                error(x->lhs->getLine(), x->lhs->getCol(),
                      "cannot assign to an array (type " + lType->toString() + ").");
            }

            if (x->op == "=")
            {
                x->rhs = convertByAssignment(x->rhs, lType, x->lhs->getLine(), x->lhs->getCol());
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
                else if (isPointer(lType))
                {
                    // ptr += n / ptr -= n: the right operand is an integer offset.
                    if (!isInteger(rType))
                        error(
                            x->rhs->getLine(), x->rhs->getCol(),
                            "Pointer compound assignment needs an integer right expression, got " +
                                rType->toString() + ".");
                }
                else if (!isInteger(rType) && !isDouble(rType))
                {
                    error(x->rhs->getLine(), x->rhs->getCol(),
                          "Arithmetic assignment needs an arithmetic right expression, got " +
                              rType->toString() + ".");
                }
            }
            else if (x->op == "*=" || x->op == "/=" || x->op == "%=")
            {
                // '%=' is integer-only; '*='/'/=' also accept floating-point operands.
                const bool modulo = x->op == "%=";
                auto ok = [&](const std::shared_ptr<Type> &t)
                { return modulo ? isInteger(t) : (isInteger(t) || isDouble(t)); };
                const std::string need = modulo ? "integer" : "arithmetic";
                if (!ok(lType))
                    error(x->lhs->getLine(), x->lhs->getCol(),
                          "Arithmetic assignment needs " + need + " for left expression, got " +
                              lType->toString() + ".");
                if (!ok(rType))
                    error(x->rhs->getLine(), x->rhs->getCol(),
                          "Arithmetic assignment needs " + need + " for right expression, got " +
                              rType->toString() + ".");
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
            // Pointer compound assignment widens the offset to long (it is scaled by
            // the element size in TACKY, like ordinary pointer arithmetic); an
            // arithmetic lhs brings the rhs to its own type so the in-place op runs
            // at the lhs width.
            if (compoundInPlace && isPointer(lType))
                x->rhs = convertTo(x->rhs, LongType::getInstance());
            else if (compoundInPlace && (isInteger(lType) || isDouble(lType)))
                x->rhs = convertTo(x->rhs, lType);

            x->resolvedType = x->lhs->resolvedType;
            x->isLvalue = false;
        }
        else if (auto x = std::dynamic_pointer_cast<BinaryExpr>(expr))
        {
            analyzeAndDecay(x->left);
            analyzeAndDecay(x->right);

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
                        const bool bothArith = (isInteger(lType) || isDouble(lType)) &&
                                               (isInteger(rType) || isDouble(rType));
                        if (bothArith)
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
                        // ptr +/- int: the integer is the (scaled) offset. Widen it
                        // to long so codegen always scales a 64-bit index.
                        if (!isInteger(rType))
                            error(x->right->getLine(), x->right->getCol(),
                                  "pointer arithmetic needs an integer operand, got " +
                                      rType->toString() + ".");
                        else
                            x->right = convertTo(x->right, LongType::getInstance());
                        x->resolvedType = lType;
                    }
                    else if (!leftIsPointer && rightIsPointer)
                    {
                        // int + ptr is the only legal mixed form (int - ptr is not).
                        if (x->binaryOp == "-")
                            error(x->right->getLine(), x->right->getCol(),
                                  "'-' Cannot have rhs as a pointer, got " + rType->toString() +
                                      ".");
                        if (!isInteger(lType))
                            error(x->left->getLine(), x->left->getCol(),
                                  "pointer arithmetic needs an integer operand, got " +
                                      lType->toString() + ".");
                        else
                            x->left = convertTo(x->left, LongType::getInstance());
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
                        // Pointer difference yields a signed element count (ptrdiff).
                        x->resolvedType = LongType::getInstance();
                    }

                    x->isLvalue = false;
                }
                else
                {
                    // '%' is integer-only; '*' and '/' also accept floating-point operands.
                    const bool allowDouble = (x->binaryOp != "%");
                    auto acceptable = [&](const std::shared_ptr<Type> &t)
                    { return isInteger(t) || (allowDouble && isDouble(t)); };
                    const std::string need = allowDouble ? "arithmetic" : "integer";

                    if (!acceptable(lType))
                    {
                        error(x->left->getLine(), x->left->getCol(),
                              "Arithmetic operator needs " + need +
                                  " type for left expression, got " + lType->toString() + ".");
                    }
                    if (!acceptable(rType))
                    {
                        error(x->right->getLine(), x->right->getCol(),
                              "Arithmetic operator needs " + need +
                                  " type for right expression, got " + rType->toString() + ".");
                    }
                    if (acceptable(lType) && acceptable(rType))
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
                    const bool bothArith = (isInteger(lType) || isDouble(lType)) &&
                                           (isInteger(rType) || isDouble(rType));
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

                if ((isInteger(lType) || isDouble(lType)) && (isInteger(rType) || isDouble(rType)))
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
            analyzeAndDecay(x->operand);

            const auto &srcType = x->operand->resolvedType;
            if (!isScalar(srcType) || (!isScalar(x->type) && !isVoid(x->type)))
            {
                error(x->getLine(), x->getCol(),
                      "Cannot cast to or from struct, got " + srcType->toString() + " and " +
                          x->type->toString() + ".");
            }
            // A pointer and a double share no representation, so neither converts to
            // the other; such a cast is a constraint violation.
            else if ((isPointer(srcType) && isDouble(x->type)) ||
                     (isDouble(srcType) && isPointer(x->type)))
            {
                error(x->getLine(), x->getCol(),
                      "cannot cast between pointer and double, got " + srcType->toString() +
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
            // a[i] == *(a + i): after decay both operands are values, one a pointer
            // and one an integer, in either order (i[a] is legal C). The result is
            // an lvalue of the pointed-to type.
            analyzeAndDecay(x->lvalue);
            analyzeAndDecay(x->index);

            const auto lt = x->lvalue->resolvedType;
            const auto it = x->index->resolvedType;

            std::shared_ptr<Type> elemType = IntType::getInstance();
            if (isPointer(lt) && isInteger(it))
            {
                elemType = std::dynamic_pointer_cast<PointerType>(lt)->getInner();
                x->index = convertTo(x->index, LongType::getInstance());
            }
            else if (isInteger(lt) && isPointer(it))
            {
                elemType = std::dynamic_pointer_cast<PointerType>(it)->getInner();
                x->lvalue = convertTo(x->lvalue, LongType::getInstance());
            }
            else
            {
                error(x->getLine(), x->getCol(),
                      "subscript needs one pointer/array operand and one integer operand, got '" +
                          lt->toString() + "' and '" + it->toString() + "'.");
            }

            x->resolvedType = elemType;
            x->isLvalue = true;
        }
        else if (auto x = std::dynamic_pointer_cast<TernaryExpr>(expr))
        {
            analyzeAndDecay(x->condition);
            analyzeAndDecay(x->thenBranch);
            analyzeAndDecay(x->elseBranch);

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
            else if ((isInteger(tType) || isDouble(tType)) && (isInteger(eType) || isDouble(eType)))
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
            // Value-context unary operators see a decayed operand; `&` wants the
            // array itself, and `++`/`--` need a modifiable lvalue (an array is
            // neither, so it is left to fail the lvalue/scalar checks below).
            if (x->op != "&" && x->op != "++" && x->op != "--")
                x->operand = decayArray(x->operand);
            if (x->op == "-" || x->op == "+" || x->op == "~")
            {
                // '~' is integer-only; unary '-'/'+' also accept floating-point.
                const auto operandType = x->operand->resolvedType;
                const bool ok = (x->op == "~") ? isInteger(operandType)
                                               : (isInteger(operandType) || isDouble(operandType));
                if (!ok)
                {
                    error(x->getLine(), x->getCol(),
                          "required " + std::string(x->op == "~" ? "integer" : "arithmetic") +
                              " operand, received '" + operandType->toString() + "'.");
                }
                // Negation/complement keep the operand's (promoted) type, so `-longVal`
                // stays long rather than being truncated to int.
                x->resolvedType = operandType;
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
            expr->calleeVariadic = functionType->isVariadic;
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
                    analyzeAndDecay(expr->parameters[i]);
                    expr->parameters[i] =
                        convertByAssignment(expr->parameters[i], functionType->paramTypes[i],
                                            expr->getLine(), expr->getCol());
                }
                for (int i = functionType->paramTypes.size(); i < expr->parameters.size(); i++)
                {
                    analyzeAndDecay(expr->parameters[i]);
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

    bool evalConstDouble(const std::shared_ptr<Expression> &e, double &out)
    {
        if (auto lit = std::dynamic_pointer_cast<FloatingLiterals>(e))
        {
            out = lit->value;
            return true;
        }
        if (auto u = std::dynamic_pointer_cast<UnaryExpr>(e))
        {
            double v;
            if (!evalConstDouble(u->operand, v))
                return false;
            if (u->op == "-")
                out = -v;
            else if (u->op == "+")
                out = v;
            else
                return false;

            return true;
        }
        if (auto b = std::dynamic_pointer_cast<BinaryExpr>(e))
        {
            double l, r;
            if (!evalConstDouble(b->left, l) || !evalConstDouble(b->right, r))
                return false;
            const auto &op = b->binaryOp;
            if (op == "+")
                out = l + r;
            else if (op == "-")
                out = l - r;
            else if (op == "*")
                out = l * r;
            else if (op == "/")
                out = l / r;
            else
                return false; // comparisons/logicals are int-typed, not folded here
            return true;
        }
        if (auto c = std::dynamic_pointer_cast<CastExpr>(e))
        {
            if (isDouble(c->operand->resolvedType))
                return evalConstDouble(c->operand, out); // double -> double
            long long v;
            if (!evalConstInt(c->operand, v)) // int -> double
                return false;
            out = isUnsigned(c->operand->resolvedType)
                      ? static_cast<double>(static_cast<unsigned long long>(v))
                      : static_cast<double>(v);
            return true;
        }
        return false;
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
                out = -v;
            else if (u->op == "+")
                out = v;
            else if (u->op == "~")
                out = ~v;
            else if (u->op == "!")
                out = !v;
            else
                return false;
            // `-` / `~` on an unsigned operand wrap mod 2^width; the reduction to
            // the result type makes that happen. `!` already yields int 0/1.
            out = reduceToType(out, u->resolvedType);
            return true;
        }
        if (auto b = std::dynamic_pointer_cast<BinaryExpr>(e))
        {
            long long l, r;
            if (!evalConstInt(b->left, l) || !evalConstInt(b->right, r))
                return false;
            const auto &op = b->binaryOp;
            // Operands have already been converted to their common type, so the
            // left operand's type is the domain the op runs in (and for shifts it
            // is the left operand that picks signed-arithmetic vs. logical).
            const auto opType = b->left->resolvedType;
            const bool uns = isUnsigned(opType);
            const int sz = typeSize(opType);

            if (op == "+")
                out = l + r;
            else if (op == "-")
                out = l - r;
            else if (op == "*")
                out = l * r;
            else if (op == "/")
            {
                if (r == 0)
                    return false;
                if (uns)
                    out = sz == 8 ? static_cast<long long>(static_cast<unsigned long long>(l) /
                                                           static_cast<unsigned long long>(r))
                                  : static_cast<unsigned int>(l) / static_cast<unsigned int>(r);
                else
                    out = sz == 8 ? l / r : static_cast<int>(l) / static_cast<int>(r);
            }
            else if (op == "%")
            {
                if (r == 0)
                    return false;
                if (uns)
                    out = sz == 8 ? static_cast<long long>(static_cast<unsigned long long>(l) %
                                                           static_cast<unsigned long long>(r))
                                  : static_cast<unsigned int>(l) % static_cast<unsigned int>(r);
                else
                    out = sz == 8 ? l % r : static_cast<int>(l) % static_cast<int>(r);
            }
            else if (op == "&")
                out = l & r;
            else if (op == "|")
                out = l | r;
            else if (op == "^")
                out = l ^ r;
            else if (op == "<<")
                out = l << r;
            else if (op == ">>")
            {
                if (uns)
                    out = sz == 8 ? static_cast<long long>(static_cast<unsigned long long>(l) >> r)
                                  : static_cast<unsigned int>(l) >> r;
                else
                    out = sz == 8 ? l >> r : static_cast<int>(l) >> r;
            }
            else if (op == "<")
                out = uns ? (sz == 8 ? static_cast<unsigned long long>(l) <
                                           static_cast<unsigned long long>(r)
                                     : static_cast<unsigned int>(l) < static_cast<unsigned int>(r))
                          : (sz == 8 ? l < r : static_cast<int>(l) < static_cast<int>(r));
            else if (op == ">")
                out = uns ? (sz == 8 ? static_cast<unsigned long long>(l) >
                                           static_cast<unsigned long long>(r)
                                     : static_cast<unsigned int>(l) > static_cast<unsigned int>(r))
                          : (sz == 8 ? l > r : static_cast<int>(l) > static_cast<int>(r));
            else if (op == "<=")
                out = uns ? (sz == 8 ? static_cast<unsigned long long>(l) <=
                                           static_cast<unsigned long long>(r)
                                     : static_cast<unsigned int>(l) <= static_cast<unsigned int>(r))
                          : (sz == 8 ? l <= r : static_cast<int>(l) <= static_cast<int>(r));
            else if (op == ">=")
                out = uns ? (sz == 8 ? static_cast<unsigned long long>(l) >=
                                           static_cast<unsigned long long>(r)
                                     : static_cast<unsigned int>(l) >= static_cast<unsigned int>(r))
                          : (sz == 8 ? l >= r : static_cast<int>(l) >= static_cast<int>(r));
            else if (op == "==")
                out = l == r;
            else if (op == "!=")
                out = l != r;
            else if (op == "&&")
                out = l && r;
            else if (op == "||")
                out = l || r;
            else
                return false;

            // Reduce the result to the node's type so the next step in the fold
            // sees the canonical value (comparisons/logicals are int 0/1 already).
            out = reduceToType(out, b->resolvedType);
            return true;
        }
        if (auto c = std::dynamic_pointer_cast<CastExpr>(e))
        {
            if (isDouble(c->operand->resolvedType))
            {
                // double -> integer cast: truncate toward zero, then narrow. An
                // unsigned target must convert through unsigned long long, since a
                // value >= 2^63 overflows a signed long long (UB / wrong bits).
                double dv;
                if (!evalConstDouble(c->operand, dv))
                    return false;
                const long long bits =
                    isUnsigned(c->type)
                        ? static_cast<long long>(static_cast<unsigned long long>(dv))
                        : static_cast<long long>(dv);
                out = reduceToType(bits, c->type);
                return true;
            }
            long long v;
            if (!evalConstInt(c->operand, v))
                return false;
            // Apply the cast's value conversion at compile time: reduce to the
            // target type's width and signedness (long / unsigned long keep all 64).
            out = reduceToType(v, c->type);
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
        double tmp_d;
        if (evalConstInt(e, tmp))
            return true;
        if (evalConstDouble(e, tmp_d))
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

    // Type-check (and, for automatic-duration objects, convert) an initializer
    // against its target type. Brace lists recurse into array element types;
    // multi-dimensional arrays nest. A list shorter than the array leaves the
    // trailing elements implicitly zero. Leaf scalar initializers are already
    // analyzed (by the analyzeExpr pass over the whole initializer), so here we
    // only decay/convert them. Static initializers are validated but not
    // materialized (their constants are folded separately for the data section).
    void checkInitializer(const std::shared_ptr<Type> &targetType,
                          std::shared_ptr<Expression> &init, bool isStatic, int line, int col)
    {
        if (auto initList = std::dynamic_pointer_cast<InitExpr>(init))
        {
            const auto arr = std::dynamic_pointer_cast<ArrayType>(targetType);
            if (!arr)
            {
                error(line, col,
                      "compound initializer cannot initialize scalar type " +
                          targetType->toString() + ".");
                return;
            }
            if (initList->elements.size() > arr->getSize())
            {
                error(line, col,
                      "too many elements in initializer for " + targetType->toString() + " (" +
                          std::to_string(arr->getSize()) + " expected, " +
                          std::to_string(initList->elements.size()) + " given).");
                return;
            }
            for (auto &element : initList->elements)
                checkInitializer(arr->getInner(), element, isStatic, line, col);
            initList->resolvedType = targetType;
            return;
        }

        // A single scalar initializer for this (sub-)object.
        if (const auto arr = std::dynamic_pointer_cast<ArrayType>(targetType))
        {
            // The only non-brace initializer an array accepts is a string literal
            // initializing a character array.
            const auto strLit = std::dynamic_pointer_cast<StringLiterals>(init);
            if (strLit && isInteger(arr->getInner()))
            {
                const auto strArr = std::dynamic_pointer_cast<ArrayType>(init->resolvedType);
                if (strArr && strArr->getSize() > arr->getSize())
                    error(line, col, "string literal too long for " + targetType->toString() + ".");
                return;
            }
            error(line, col,
                  "cannot initialize array type " + targetType->toString() +
                      " with a scalar initializer.");
            return;
        }

        init = decayArray(init);
        if (isStatic)
        {
            // Conversions aren't materialized for static initializers; the constant
            // was folded by the caller. Just validate assignability.
            if (!isAssignmentCompatible(targetType, init))
                error(line, col,
                      "cannot initialize " + targetType->toString() + " with " +
                          init->resolvedType->toString() + ".");
        }
        else
        {
            init = convertByAssignment(init, targetType, line, col);
        }
    }

    // Fold a constant static initializer into its flat data image. Brace lists
    // recurse into element offsets and append a trailing Zero for any uninitialized
    // tail; scalar leaves are converted to the (sub-)object type.
    void buildStaticInits(const std::shared_ptr<Type> &targetType,
                          const std::shared_ptr<Expression> &init, std::vector<StaticInit> &out)
    {
        if (const auto initList = std::dynamic_pointer_cast<InitExpr>(init))
        {
            const auto arr = std::dynamic_pointer_cast<ArrayType>(targetType);
            const auto elem = arr->getInner();
            for (const auto &element : initList->elements)
                buildStaticInits(elem, element, out);
            const long long pad =
                static_cast<long long>(arr->getSize() - initList->elements.size()) *
                sizeOfTypeBytes(elem);
            if (pad > 0)
                out.push_back(StaticInit::zero(pad));
            return;
        }

        if (isDouble(targetType))
        {
            double d;
            long long i;
            if (evalConstDouble(init, d))
                out.push_back(StaticInit::dbl(d));
            else if (evalConstInt(init, i))
                out.push_back(
                    StaticInit::dbl(isUnsigned(init->resolvedType)
                                        ? static_cast<double>(static_cast<unsigned long long>(i))
                                        : static_cast<double>(i)));
            else
                out.push_back(StaticInit::dbl(0.0));
            return;
        }

        // Integer or pointer target: fold to an integer, narrowed to its width.
        long long v = 0;
        long long i;
        double d;
        if (evalConstInt(init, i))
            v = i;
        else if (evalConstDouble(init, d))
            v = isUnsigned(targetType) ? static_cast<long long>(static_cast<unsigned long long>(d))
                                       : static_cast<long long>(d);
        v = reduceToType(v, targetType);
        out.push_back(sizeOfTypeBytes(targetType) == 8 ? StaticInit::i64(v) : StaticInit::i32(v));
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

        const bool staticDuration =
            variable->symbol && variable->symbol->duration == StorageDuration::Static;

        if (variable->initialization)
        {
            analyzeExpr(variable->initialization);

            // Static-duration variables require a constant initializer.
            if (staticDuration && !isConstantInitializer(variable->initialization))
                error(variable->getLine(), variable->getCol(),
                      "initializer for '" + variable->name +
                          "' with static storage duration must be a constant expression.");

            checkInitializer(variable->type, variable->initialization, staticDuration,
                             variable->getLine(), variable->getCol());

            if (staticDuration && variable->symbol)
            {
                // A definition's image wins over any zero image left by an earlier
                // tentative redeclaration of the same object.
                variable->symbol->staticInits.clear();
                buildStaticInits(variable->type, variable->initialization,
                                 variable->symbol->staticInits);
            }
        }
        else if (staticDuration && variable->symbol && variable->symbol->staticInits.empty())
        {
            // No initializer: the whole object is zero (.bss), unless a defining
            // declaration of the same object already supplied an image.
            variable->symbol->staticInits = {StaticInit::zero(sizeOfTypeBytes(variable->type))};
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
            analyzeAndDecay(stmt->returnExpression);
            stmt->returnExpression =
                convertByAssignment(stmt->returnExpression, currentReturnType,
                                    stmt->returnExpression->getLine(), stmt->returnExpression->col);
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
        analyzeAndDecay(ifStmt->condition);

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
        analyzeAndDecay(whileStmt->condition);

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
        analyzeAndDecay(doWhileStmt->condition);

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

        if (isArray(stmt->declaration->type))
            error(stmt->declaration->getLine(), stmt->declaration->getCol(),
                  "function '" + stmt->declaration->name + "' cannot return array type " +
                      stmt->declaration->type->toString() + ".");

        std::vector<std::shared_ptr<Type>> paramTypes;
        paramTypes.reserve(stmt->declaration->parameters.size());
        for (auto &param : stmt->declaration->parameters)
        {
            param.type = adjustParamType(param.type);
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

        if (isArray(node->type))
            error(node->getLine(), node->getCol(),
                  "function '" + node->name + "' cannot return array type " +
                      node->type->toString() + ".");

        std::vector<std::shared_ptr<Type>> paramTypes;
        paramTypes.reserve(node->parameters.size());
        for (auto &param : node->parameters)
        {
            param.type = adjustParamType(param.type);
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

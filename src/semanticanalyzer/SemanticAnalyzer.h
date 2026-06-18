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
#include "../support/RTTI.h"
#include "../symboltable/SymbolTable.h"
#include "../types/TypeQueries.h"
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

// The three character types (char, signed char, unsigned char). They are integer
// types but are promoted to int before taking part in most expressions.
inline bool isCharacter(const std::shared_ptr<Type> &t)
{
    const TypeKind k = t->getKind();
    return k >= TypeKind::Char && k <= TypeKind::UChar;
}

inline bool isInteger(const std::shared_ptr<Type> &t)
{
    const TypeKind k = t->getKind();
    return k >= TypeKind::Char && k <= TypeKind::ULong;
}

inline bool isPointer(const std::shared_ptr<Type> &t) { return t->getKind() == TypeKind::Pointer; }

inline bool isArray(const std::shared_ptr<Type> &t) { return t->getKind() == TypeKind::Array; }

inline bool isStruct(const std::shared_ptr<Type> &t) { return t->getKind() == TypeKind::Struct; }

inline bool isVoid(const std::shared_ptr<Type> &t) { return t->getKind() == TypeKind::Void; }

inline bool isVoidPointer(const std::shared_ptr<Type> &t)
{
    if (t->getKind() != TypeKind::Pointer)
        return false;
    return isVoid(static_cast<const PointerType *>(t.get())->getInner());
}

// A type is complete when an object of it has a known size. void is incomplete;
// a struct is complete once its definition has been processed (a layout exists
// for its resolved tag); an array is complete iff its element is (so void[3] and,
// recursively, void[3][4] are incomplete).
inline bool isComplete(const std::shared_ptr<Type> &t)
{
    const TypeKind k = t->getKind();
    if (k == TypeKind::Void)
        return false;
    if (k == TypeKind::Array)
        return isComplete(static_cast<const ArrayType *>(t.get())->getInner());
    if (k == TypeKind::Struct)
        return findStructLayout(static_cast<const StructType *>(t.get())->getName()) != nullptr;
    return true;
}

inline bool isDouble(const std::shared_ptr<Type> &t) { return t->getKind() == TypeKind::Double; }

inline bool isScalar(const std::shared_ptr<Type> &t)
{
    return isInteger(t) || isPointer(t) || isDouble(t);
}

inline bool isUnsigned(const std::shared_ptr<Type> &t)
{
    const TypeKind k = t->getKind();
    return k == TypeKind::UChar || k == TypeKind::UInt || k == TypeKind::ULong;
}

// Width in bytes of a scalar type; 0 for non-scalars. Used for usual-arithmetic
// conversions, where the wider integer type wins.
inline int typeSize(const std::shared_ptr<Type> &t)
{
    switch (t->getKind())
    {
    case TypeKind::Char:
    case TypeKind::SChar:
    case TypeKind::UChar:
        return 1;
    case TypeKind::Int:
    case TypeKind::UInt:
        return 4;
    case TypeKind::Long:
    case TypeKind::ULong:
    case TypeKind::Pointer:
        return 8;
    default:
        return 0;
    }
}

// Common type of two arithmetic operands (callers gate on isInteger), i.e. the
// usual arithmetic conversions. Same signedness: the wider type wins. Mixed
// signedness: the unsigned type wins unless the signed type is strictly wider
// (a wider signed type represents all the unsigned values, so long + unsigned
// int -> long).
inline std::shared_ptr<Type> getCommonType(const std::shared_ptr<Type> &aIn,
                                           const std::shared_ptr<Type> &bIn)
{
    // Integer promotion: a character operand acts as int in arithmetic, so the
    // common type is computed over the promoted types (char + char -> int).
    const std::shared_ptr<Type> a = isCharacter(aIn) ? IntType::getInstance() : aIn;
    const std::shared_ptr<Type> b = isCharacter(bIn) ? IntType::getInstance() : bIn;

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
    switch (t->getKind())
    {
    case TypeKind::Int:
        return static_cast<int>(v);
    case TypeKind::UInt:
        return static_cast<unsigned int>(v);
    case TypeKind::Char:
    case TypeKind::SChar:
        return static_cast<signed char>(v);
    case TypeKind::UChar:
        return static_cast<unsigned char>(v);
    default:
        return v;
    }
}

class SemanticAnalyzer
{
    SymbolTable symbolTable;
    Diagnostic::DiagnosticEngine &diag;
    std::shared_ptr<Type> currentReturnType;
    int loopLabel = 0;
    int labelCounter = 0;
    int structCounter = 0;

  public:
    explicit SemanticAnalyzer(Diagnostic::DiagnosticEngine &diag) : diag(diag) {}

    void error(int line, int col, const std::string &msg)
    {
        diag.report(Diagnostic::DiagLevel::SEMANTIC, {line, col}, msg);
    }

    void check(const std::string &name, const std::shared_ptr<Symbol> &symbol, const Kind kind,
               const int line, const int col, ASTNode &node)
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
        node.symbol = symbol;
        symbol->node = &node;
    }

    void declareFunction(Function &node, const std::shared_ptr<FunctionType> &fnType)
    {
        const auto sc = node.storageClass;
        // Functions are TU-wide entities: reconcile against the one linkage-registry
        // symbol even when the declaration appears inside a block.
        auto g = symbolTable.findLinked(node.name);
        const bool gLinked = g && g->kind == Kind::FUNCTION && g->linkage != Linkage::None;
        // static → internal; extern/none → follows a prior linked decl, else external.
        const Linkage linkage = (sc == StorageClass::Static)
                                    ? Linkage::Internal
                                    : (gLinked ? g->linkage : Linkage::External);

        if (!g)
        {
            auto sym = std::make_shared<Symbol>(node.name, fnType, node.getLine(), node.getCol(),
                                                Kind::FUNCTION);
            sym->linkage = linkage;
            sym->defined = (node.statements != nullptr);
            symbolTable.insertLinked(node.name, sym);
            node.symbol = sym;
            sym->node = &node;
        }
        else if (g->kind != Kind::FUNCTION)
        {
            error(node.getLine(), node.getCol(),
                  "'" + node.name + "' redeclared as different kind (" + kindToString(g->kind) +
                      " at line " + std::to_string(g->line) + ").");
            return;
        }
        else
        {
            if (!g->type->equals(*fnType))
            {
                error(node.getLine(), node.getCol(),
                      "conflicting types for '" + node.name + "' (was " + g->type->toString() +
                          ", now " + fnType->toString() + ").");
                return;
            }
            if (g->linkage != linkage)
            {
                error(node.getLine(), node.getCol(),
                      "conflicting linkage for '" + node.name + "' (previous declaration at line " +
                          std::to_string(g->line) + ").");
                return;
            }
            if (node.statements)
            {
                auto prevNode = dynamic_cast<Function *>(g->node);
                if (prevNode && prevNode->statements)
                {
                    error(node.getLine(), node.getCol(),
                          "redefinition of '" + node.name + "' (previous definition at line " +
                              std::to_string(g->line) + ").");
                    return;
                }
                g->node = &node; // this body is now the canonical decl
                g->defined = true;
            }
            node.symbol = g; // share the existing Symbol
        }

        // Make the function visible for name resolution in the current scope
        // (file scope for a top-level function, the block for a local prototype).
        auto same = symbolTable.findSameScope(node.name, Kind::FUNCTION);
        if (!same)
            symbolTable.bindCurrent(node.name, node.symbol, Kind::FUNCTION);
        else if (same->linkage == Linkage::None)
            error(node.getLine(), node.getCol(),
                  "conflicting declaration of '" + node.name + "' (previous declaration at line " +
                      std::to_string(same->line) + ").");
    }

    bool isNullPointerConstant(const Expression *e)
    {
        if (!e)
            return false;
        if (const auto *lit = dyn_cast<IntLiterals>(e))
            return lit->value == 0;
        if (const auto *cast = dyn_cast<CastExpr>(e))
        {
            const auto ptr = std::dynamic_pointer_cast<PointerType>(cast->type);
            if (!ptr)
                return false;
            if (!std::dynamic_pointer_cast<VoidType>(ptr->getInner()))
                return false;
            return isNullPointerConstant(cast->operand.get());
        }
        return false;
    }

    // Reconcile an already-analyzed rvalue to `target` by wrapping it in a
    // synthesized CastExpr when the types differ. The new node is pre-typed and is
    // NOT re-run through analyzeExpr. Codegen later lowers these into the actual
    // sign-extend / truncate.
    std::unique_ptr<Expression> convertTo(std::unique_ptr<Expression> e,
                                          const std::shared_ptr<Type> &target)
    {
        if (e->resolvedType->equals(*target))
            return e;
        int line = e->getLine(), col = e->getCol();
        auto cast = std::make_unique<CastExpr>(target, std::move(e), line, col);
        cast->resolvedType = target;
        cast->isLvalue = false;
        return cast;
    }

    // Integer promotion of a single operand: a character-typed rvalue is promoted
    // to int in place (e.g. unary `-`/`~` and shift left operands operate on the
    // promoted value, so `-uc` is int -1 rather than wrapping in unsigned char).
    void promote(std::unique_ptr<Expression> &e)
    {
        if (isCharacter(e->resolvedType))
            e = convertTo(std::move(e), IntType::getInstance());
    }

    // C's assignment conversions, shared by `=`, initializers, `return`, and call
    // arguments: an rvalue is assignable to a target when the types are equal, both
    // are arithmetic, or the target is a pointer and the rvalue is a null pointer
    // constant. Notably int<->pointer, double<->pointer, and mismatched pointer
    // types are NOT assignable. (Array-to-pointer decay is handled separately by
    // callers via canDecayTo.)
    bool isAssignmentCompatible(const std::shared_ptr<Type> &target, const Expression *rhs)
    {
        const auto &src = rhs->resolvedType;
        if (src->equals(*target))
            return true;
        if ((isInteger(src) || isDouble(src)) && (isInteger(target) || isDouble(target)))
            return true;
        if (isPointer(target) && isNullPointerConstant(rhs))
            return true;
        // void * converts to/from any other object pointer type, either direction
        // (but never to/from a non-pointer such as an integer).
        if (isPointer(target) && isPointer(src) && (isVoidPointer(target) || isVoidPointer(src)))
            return true;
        return false;
    }

    // Validate an assignment-style conversion and, when valid, return the rvalue
    // converted to the target type (arithmetic conversion or null-constant ->
    // pointer). On a type mismatch, report an error and return the rvalue as-is.
    std::unique_ptr<Expression> convertByAssignment(std::unique_ptr<Expression> e,
                                                    const std::shared_ptr<Type> &target, int line,
                                                    int col)
    {
        if (!isAssignmentCompatible(target, e.get()))
        {
            error(line, col,
                  "cannot convert " + e->resolvedType->toString() + " to " + target->toString());
            return e;
        }
        return convertTo(std::move(e), target);
    }

    // Array-to-pointer decay: in any value context an array becomes a pointer to
    // its first element. We synthesize &arr typed as pointer-to-element (same
    // address, scalar type) so the rest of the pipeline reuses the address-of
    // path. Non-array expressions pass through unchanged.
    std::unique_ptr<Expression> decayArray(std::unique_ptr<Expression> e)
    {
        const auto arr = std::dynamic_pointer_cast<ArrayType>(e->resolvedType);
        if (!arr)
            return e;
        int line = e->getLine(), col = e->getCol();
        auto addr = std::make_unique<UnaryExpr>(line, col, "&", std::move(e));
        addr->resolvedType = std::make_shared<PointerType>(arr->getInner());
        addr->isLvalue = false;
        return addr;
    }

    // analyzeExpr followed by array-to-pointer decay, for sub-expressions used as
    // rvalues. Contexts that need the array itself (operands of `&`, `sizeof`, and
    // the target of an assignment) call analyzeExpr directly instead.
    void analyzeAndDecay(std::unique_ptr<Expression> &slot)
    {
        analyzeExpr(*slot);
        slot = decayArray(std::move(slot));
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

    // Reject array types whose element is incomplete, anywhere in a declared type.
    // The element of an array must have a known size, so void[3] is illegal — and
    // so are the nested forms void[3][4] and void(*)[3] reached through pointers,
    // arrays, and function parameter/return types.
    void validateType(const std::shared_ptr<Type> &t, int line, int col)
    {
        if (const auto a = std::dynamic_pointer_cast<ArrayType>(t))
        {
            if (!isComplete(a->getInner()))
                error(line, col,
                      "array element type must be complete, got " + a->getInner()->toString() +
                          ".");
            validateType(a->getInner(), line, col);
        }
        else if (const auto p = std::dynamic_pointer_cast<PointerType>(t))
        {
            validateType(p->getInner(), line, col);
        }
        else if (const auto f = std::dynamic_pointer_cast<FunctionType>(t))
        {
            for (const auto &pt : f->paramTypes)
                validateType(pt, line, col);
            validateType(f->returnType, line, col);
        }
    }

    // A parameter is validated against its declared type, before array-to-pointer
    // adjustment: a void parameter is meaningless, and an array parameter's
    // element type must be complete (void foo[3] adjusts to void * but is still
    // illegal because the pre-adjustment element type is incomplete).
    void validateParam(const Parameter &param)
    {
        if (isVoid(param.type))
            error(param.line, param.col, "parameter '" + param.name + "' has type void.");
        validateType(param.type, param.line, param.col);
    }

    // Rewrite every struct tag occurring in a used type to its resolved (mangled)
    // name, so that shadowed same-named structs stay distinct in the layout table
    // and downstream. A source tag (one with no '.') must already be in scope;
    // an unresolvable reference is a use of an undeclared struct type. The '.' in
    // a mangled name marks an already-resolved tag, making this idempotent.
    void resolveTags(const std::shared_ptr<Type> &t, int line, int col)
    {
        if (const auto s = std::dynamic_pointer_cast<StructType>(t))
        {
            if (s->getName().find('.') != std::string::npos)
                return; // already resolved
            const auto sym = symbolTable.find(s->getName(), Kind::STRUCT_TAG);
            if (!sym)
            {
                error(line, col, "use of undeclared struct type '" + s->getName() + "'.");
                return;
            }
            s->setName(sym->uniqueName);
        }
        else if (const auto p = std::dynamic_pointer_cast<PointerType>(t))
            resolveTags(p->getInner(), line, col);
        else if (const auto a = std::dynamic_pointer_cast<ArrayType>(t))
            resolveTags(a->getInner(), line, col);
        else if (const auto f = std::dynamic_pointer_cast<FunctionType>(t))
        {
            for (const auto &pt : f->paramTypes)
                resolveTags(pt, line, col);
            resolveTags(f->returnType, line, col);
        }
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

    void analyzeExpr(Expression &expr)
    {
        switch (expr.getKind())
        {
        case NodeKind::IntLiterals:
        {
            auto *x = cast<IntLiterals>(&expr);
            x->resolvedType = x->type; // parser typed it Int or Long by value/suffix
            x->isLvalue = false;
            break;
        }
        case NodeKind::FloatingLiterals:
        {
            auto *x = cast<FloatingLiterals>(&expr);
            x->resolvedType = x->type;
            x->isLvalue = false;
            break;
        }
        case NodeKind::StringLiterals:
        {
            auto *x = cast<StringLiterals>(&expr);
            const size_t size = decodedLength(x->literal);
            const auto type = CharType::getInstance();
            auto stringType = std::make_shared<ArrayType>(type, size);
            x->resolvedType = stringType;
            // A string literal is an array object with static storage, hence an
            // lvalue (`&"..."` is valid). Assignment to it is still rejected by the
            // non-modifiable-array rule.
            x->isLvalue = true;
            break;
        }
        case NodeKind::AssignExpr:
        {
            auto *x = cast<AssignExpr>(&expr);
            analyzeExpr(*x->lhs);
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
                // Copying a whole struct needs a complete type on both sides.
                if (!isComplete(lType))
                    error(x->lhs->getLine(), x->lhs->getCol(),
                          "cannot assign to incomplete type " + lType->toString() + ".");
                x->rhs = convertByAssignment(std::move(x->rhs), lType, x->lhs->getLine(),
                                             x->lhs->getCol());
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
            const bool isShift = x->op == "<<=" || x->op == ">>=";
            if (compoundInPlace && isPointer(lType))
            {
                // Pointer compound assignment widens the offset to long (it is scaled
                // by the element size in TACKY, like ordinary pointer arithmetic).
                x->rhs = convertTo(std::move(x->rhs), LongType::getInstance());
            }
            else if ((compoundInPlace || isShift) && isCharacter(lType))
            {
                // A character lhs promotes to int: the op runs in the promoted/common
                // type and the result is converted back to char in TACKY. Shifts run
                // in the promoted lhs type (the count keeps its own type).
                const auto compute = isShift ? IntType::getInstance() : getCommonType(lType, rType);
                if (!isShift)
                    x->rhs = convertTo(std::move(x->rhs), compute);
                x->computeType = compute;
            }
            else if (compoundInPlace && (isInteger(lType) || isDouble(lType)))
            {
                // A non-character arithmetic lhs brings the rhs to its own type so the
                // in-place op runs at the lhs width.
                x->rhs = convertTo(std::move(x->rhs), lType);
            }

            x->resolvedType = x->lhs->resolvedType;
            x->isLvalue = false;
            break;
        }
        case NodeKind::BinaryExpr:
        {
            auto *x = cast<BinaryExpr>(&expr);
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

                    // Pointer arithmetic scales by the pointed-to type's size, so it
                    // needs a complete element type: no arithmetic on a void * (or
                    // any pointer to an incomplete type).
                    auto innerOf = [](const std::shared_ptr<Type> &t)
                    { return std::dynamic_pointer_cast<PointerType>(t)->getInner(); };
                    if (leftIsPointer && !isComplete(innerOf(lType)))
                        error(x->left->getLine(), x->left->getCol(),
                              "pointer arithmetic requires a complete pointee type, got " +
                                  lType->toString() + ".");
                    if (rightIsPointer && !isComplete(innerOf(rType)))
                        error(x->right->getLine(), x->right->getCol(),
                              "pointer arithmetic requires a complete pointee type, got " +
                                  rType->toString() + ".");

                    if (!leftIsPointer && !rightIsPointer)
                    {
                        const bool bothArith = (isInteger(lType) || isDouble(lType)) &&
                                               (isInteger(rType) || isDouble(rType));
                        if (bothArith)
                        {
                            auto common = getCommonType(lType, rType);
                            x->left = convertTo(std::move(x->left), common);
                            x->right = convertTo(std::move(x->right), common);
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
                            x->right = convertTo(std::move(x->right), LongType::getInstance());
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
                            x->left = convertTo(std::move(x->left), LongType::getInstance());
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
                        x->left = convertTo(std::move(x->left), common);
                        x->right = convertTo(std::move(x->right), common);
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
                        // == and != also accept a void * against any object pointer
                        // (the common type is void *); the relational operators
                        // still require identical pointer types.
                        const bool voidOk =
                            isEquality && (isVoidPointer(lType) || isVoidPointer(rType));
                        if (!voidOk)
                            error(x->right->getLine(), x->right->getCol(),
                                  "Comparison operator needs matching pointer types, left: " +
                                      lType->toString() + ", right: " + rType->toString() + ".");
                    }
                    else if (!bothArith && !bothPtr)
                    {
                        const bool nullOK =
                            isEquality &&
                            ((isPointer(lType) && isNullPointerConstant(x->right.get())) ||
                             (isPointer(rType) && isNullPointerConstant(x->left.get())));
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
                    x->left = convertTo(std::move(x->left), common);
                    x->right = convertTo(std::move(x->right), common);
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
                    // Shift result is the (promoted) left operand's type; the shift
                    // count type is independent, so the operands are not brought to a
                    // common type. A char left operand promotes to int.
                    promote(x->left);
                    x->resolvedType = x->left->resolvedType;
                }
                else
                {
                    auto common = getCommonType(lType, rType);
                    x->left = convertTo(std::move(x->left), common);
                    x->right = convertTo(std::move(x->right), common);
                    x->resolvedType = common;
                }
                x->isLvalue = false;
            }
            else if (isComma(x->binaryOp))
            {
                x->resolvedType = rType;
                x->isLvalue = x->right->isLvalue;
            }
            break;
        }
        case NodeKind::CastExpr:
        {
            auto *x = cast<CastExpr>(&expr);
            analyzeAndDecay(x->operand);
            resolveTags(x->type, x->getLine(), x->getCol());
            validateType(x->type, x->getLine(), x->getCol());

            const auto &srcType = x->operand->resolvedType;
            // A cast to void discards the operand's value and accepts any operand
            // type, except one whose value can't be formed (an incomplete struct).
            // A void operand is fine. Every other cast is between scalars.
            if (isVoid(x->type))
            {
                if (!isVoid(srcType) && !isComplete(srcType))
                    error(x->getLine(), x->getCol(),
                          "cannot cast incomplete type " + srcType->toString() + " to void.");
            }
            else if (!isScalar(srcType) || !isScalar(x->type))
            {
                error(x->getLine(), x->getCol(),
                      "can only cast between scalar types, got " + srcType->toString() + " and " +
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
            break;
        }
        case NodeKind::FunctionCallExpr:
        {
            auto *x = cast<FunctionCallExpr>(&expr);
            analyzeFunctionCallExpr(*x);
            break;
        }
        case NodeKind::InitExpr:
        {
            auto *x = cast<InitExpr>(&expr);
            for (auto &element : x->elements)
            {
                analyzeExpr(*element);
            }
            x->resolvedType = IntType::getInstance();
            x->isLvalue = false;
            break;
        }
        case NodeKind::MemberExpr:
        {
            auto *x = cast<MemberExpr>(&expr);
            // `p->m` may apply to an array that decays to a pointer (e.g. an array
            // struct member: `s->arr->m`); a struct operand of `.` never decays.
            analyzeAndDecay(x->object);
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

            // The struct must be complete to reach into it (this also rejects
            // `p->m` where p points to an incomplete struct).
            const StructLayout *layout = findStructLayout(baseType->getName());
            if (!layout)
            {
                error(objLine, objCol,
                      "member access on incomplete struct type " + baseType->toString() + ".");
                recoverAsInt();
                return;
            }

            auto it = std::find_if(layout->members.begin(), layout->members.end(),
                                   [&](const MemberEntry &m) { return m.name == x->field; });
            if (it == layout->members.end())
            {
                error(x->getLine(), x->getCol(),
                      "no member '" + x->field + "' in " + baseType->toString() + ".");
                recoverAsInt();
                return;
            }

            x->resolvedType = it->type;
            // `p->m` is always an lvalue; `s.m` is an lvalue only when s is (so the
            // member of a struct returned by value is a non-lvalue, like the value).
            x->isLvalue = x->isArrow ? true : x->object->isLvalue;
            break;
        }
        case NodeKind::SizeOfExpr:
        {
            auto *x = cast<SizeOfExpr>(&expr);
            // The operand is analyzed for its type only: it is not decayed (sizeof
            // of an array is the whole array, not a pointer) and, since the result
            // folds to a constant, it is never evaluated. sizeof(type) uses the
            // type directly.
            std::shared_ptr<Type> operandType;
            if (x->expr)
            {
                analyzeExpr(*x->expr);
                operandType = x->expr->resolvedType;
            }
            else
            {
                resolveTags(x->type, x->getLine(), x->getCol());
                operandType = x->type;
            }

            // sizeof needs a complete object type; void, incomplete structs,
            // incomplete arrays, and function designators have no size.
            if (std::dynamic_pointer_cast<FunctionType>(operandType) || !isComplete(operandType))
                error(x->getLine(), x->getCol(),
                      "cannot apply sizeof to type " + operandType->toString() + ".");

            x->resolvedType = UnsignedLongType::getInstance();
            x->isLvalue = false;
            break;
        }
        case NodeKind::SubscriptExpr:
        {
            auto *x = cast<SubscriptExpr>(&expr);
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
                x->index = convertTo(std::move(x->index), LongType::getInstance());
            }
            else if (isInteger(lt) && isPointer(it))
            {
                elemType = std::dynamic_pointer_cast<PointerType>(it)->getInner();
                x->lvalue = convertTo(std::move(x->lvalue), LongType::getInstance());
            }
            else
            {
                error(x->getLine(), x->getCol(),
                      "subscript needs one pointer/array operand and one integer operand, got '" +
                          lt->toString() + "' and '" + it->toString() + "'.");
            }

            // a[i] dereferences a + i, so the element type must be complete: a
            // void * (pointer to incomplete) cannot be subscripted.
            if (!isComplete(elemType))
                error(x->getLine(), x->getCol(),
                      "cannot subscript a pointer to an incomplete type.");

            x->resolvedType = elemType;
            x->isLvalue = true;
            break;
        }
        case NodeKind::TernaryExpr:
        {
            auto *x = cast<TernaryExpr>(&expr);
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
                // A struct-typed conditional must have a complete type so its value
                // can be materialized.
                if (isStruct(tType) && !isComplete(tType))
                    error(x->getLine(), x->getCol(),
                          "conditional has incomplete struct type " + tType->toString() + ".");
                x->resolvedType = tType;
            }
            else if (isPointer(tType) && isNullPointerConstant(x->elseBranch.get()))
            {
                x->resolvedType = tType;
            }
            else if (isPointer(eType) && isNullPointerConstant(x->thenBranch.get()))
            {
                x->resolvedType = eType;
            }
            else if (isPointer(tType) && isPointer(eType) &&
                     (isVoidPointer(tType) || isVoidPointer(eType)))
            {
                // The common type of void * and any object pointer is void *; both
                // branches convert to it.
                auto voidPtr = std::make_shared<PointerType>(VoidType::getInstance());
                x->thenBranch = convertTo(std::move(x->thenBranch), voidPtr);
                x->elseBranch = convertTo(std::move(x->elseBranch), voidPtr);
                x->resolvedType = voidPtr;
            }
            else if ((isInteger(tType) || isDouble(tType)) && (isInteger(eType) || isDouble(eType)))
            {
                auto common = getCommonType(tType, eType);
                x->thenBranch = convertTo(std::move(x->thenBranch), common);
                x->elseBranch = convertTo(std::move(x->elseBranch), common);
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
            break;
        }
        case NodeKind::UnaryExpr:
        {
            auto *x = cast<UnaryExpr>(&expr);
            // `&*p` cancels: the dereference is never actually performed, so p may
            // point to an incomplete type. Type it as p's own type and skip the
            // deref's completeness check (which would wrongly reject `&*p`).
            if (x->op == "&")
            {
                if (auto *inner = dyn_cast<UnaryExpr>(x->operand.get()); inner && inner->op == "*")
                {
                    analyzeAndDecay(inner->operand);
                    const auto innerPtr =
                        std::dynamic_pointer_cast<PointerType>(inner->operand->resolvedType);
                    if (!innerPtr)
                    {
                        error(x->getLine(), x->getCol(),
                              "required pointer operand, received '" +
                                  inner->operand->resolvedType->toString() + "'.");
                        x->resolvedType = IntType::getInstance();
                        x->isLvalue = false;
                        return;
                    }
                    inner->resolvedType = innerPtr->getInner();
                    inner->isLvalue = true;
                    x->resolvedType = inner->operand->resolvedType;
                    x->isLvalue = false;
                    return;
                }
            }

            analyzeExpr(*x->operand);
            // Value-context unary operators see a decayed operand; `&` wants the
            // array itself, and `++`/`--` need a modifiable lvalue (an array is
            // neither, so it is left to fail the lvalue/scalar checks below).
            if (x->op != "&" && x->op != "++" && x->op != "--")
                x->operand = decayArray(std::move(x->operand));
            if (x->op == "-" || x->op == "+" || x->op == "~")
            {
                // '~' is integer-only; unary '-'/'+' also accept floating-point.
                const bool ok = (x->op == "~") ? isInteger(x->operand->resolvedType)
                                               : (isInteger(x->operand->resolvedType) ||
                                                  isDouble(x->operand->resolvedType));
                if (!ok)
                {
                    error(x->getLine(), x->getCol(),
                          "required " + std::string(x->op == "~" ? "integer" : "arithmetic") +
                              " operand, received '" + x->operand->resolvedType->toString() + "'.");
                }
                // Character operands are promoted to int before the op, so `-uc`/`~uc`
                // can't wrap in char width. Wider types keep their type (`-longVal`
                // stays long).
                promote(x->operand);
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
                    if (!isComplete(pointerType->getInner()))
                        error(x->getLine(), x->getCol(),
                              "cannot dereference a pointer to incomplete type " +
                                  pointerType->getInner()->toString() + ".");
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
            break;
        }
        case NodeKind::VariableExpr:
        {
            auto *x = cast<VariableExpr>(&expr);
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
            break;
        }
        default:
            throw std::runtime_error("Reached invalid expression at line " +
                                     std::to_string(expr.getLine()) + ", col " +
                                     std::to_string(expr.getCol()));
        }
    }

    void analyzeFunctionCallExpr(FunctionCallExpr &expr)
    {
        std::string functionName = expr.functionName->name;
        const auto checkFunctionExistence = symbolTable.find(functionName, Kind::FUNCTION);
        if (checkFunctionExistence == nullptr)
        {
            error(expr.getLine(), expr.getCol(), "function " + functionName + " not declared");
            expr.resolvedType = IntType::getInstance();
        }
        else if (checkFunctionExistence->kind != Kind::FUNCTION)
        {
            error(expr.getLine(), expr.getCol(),
                  "'" + functionName + "' is not a function (declared as " +
                      kindToString(checkFunctionExistence->kind) + " at line " +
                      std::to_string(checkFunctionExistence->line) + ").");
            expr.resolvedType = IntType::getInstance();
        }
        else
        {
            auto functionType =
                std::dynamic_pointer_cast<FunctionType>(checkFunctionExistence->type);
            expr.calleeVariadic = functionType->isVariadic;
            if ((functionType->isVariadic &&
                 functionType->paramTypes.size() > expr.parameters.size()) ||
                (!functionType->isVariadic &&
                 functionType->paramTypes.size() != expr.parameters.size()))
            {
                error(expr.getLine(), expr.getCol(),
                      "function call has " + std::to_string(expr.parameters.size()) +
                          " parameters. Expected " +
                          std::to_string(functionType->paramTypes.size()));
            }
            else
            {
                for (int i = 0; i < (int)functionType->paramTypes.size(); i++)
                {
                    analyzeAndDecay(expr.parameters[i]);
                    // Passing a struct by value needs a complete parameter type.
                    if (!isComplete(functionType->paramTypes[i]))
                        error(expr.getLine(), expr.getCol(),
                              "argument " + std::to_string(i + 1) + " has incomplete type " +
                                  functionType->paramTypes[i]->toString() + ".");
                    expr.parameters[i] = convertByAssignment(std::move(expr.parameters[i]),
                                                             functionType->paramTypes[i],
                                                             expr.getLine(), expr.getCol());
                }
                for (int i = (int)functionType->paramTypes.size(); i < (int)expr.parameters.size();
                     i++)
                {
                    analyzeAndDecay(expr.parameters[i]);
                }
            }
            // Calling a function whose return type is an incomplete struct can't
            // produce a usable value (a void return is fine — there's no value).
            if (!isVoid(functionType->returnType) && !isComplete(functionType->returnType))
                error(expr.getLine(), expr.getCol(),
                      "call to function returning incomplete type " +
                          functionType->returnType->toString() + ".");
            expr.resolvedType = functionType->returnType;
        }
        expr.isLvalue = false;
    }

    void analyzeExprStmt(ExprStmt &exprStmt)
    {
        analyzeExpr(*exprStmt.expr);
        // A full expression evaluated as a statement (an expression statement, or a
        // for-loop's init/condition/update clause) has its value discarded — which
        // requires a value to exist. A void expression is fine (there's no value),
        // but an incomplete struct/union type has no formable value: reject it.
        const auto &t = exprStmt.expr->resolvedType;
        if (t && !isVoid(t) && !isComplete(t))
            error(exprStmt.expr->getLine(), exprStmt.expr->getCol(),
                  "expression statement has incomplete type " + t->toString() + ".");
    }

    void analyzeStructDecl(StructDecl &structDecl)
    {
        const std::string source = structDecl.name;
        auto existing = symbolTable.findSameScope(source, Kind::STRUCT_TAG);

        const auto mangle = [&]() { return source + "." + std::to_string(++structCounter); };
        const auto setResolvedName = [&](const std::string &unique)
        {
            structDecl.name = unique;
            if (auto st = std::dynamic_pointer_cast<StructType>(structDecl.baseType))
                st->setName(unique);
        };

        // Forward declaration `struct s;`: introduce an incomplete tag in this
        // scope if none is present; otherwise it is a no-op (a tag already in this
        // scope, complete or not, keeps its meaning).
        if (!structDecl.isComplete)
        {
            if (!existing)
            {
                existing =
                    std::make_shared<Symbol>(source, structDecl.baseType, structDecl.getLine(),
                                             structDecl.getCol(), Kind::STRUCT_TAG);
                symbolTable.insert(source, existing, Kind::STRUCT_TAG);
                existing->uniqueName = mangle();
                existing->node = &structDecl;
            }
            setResolvedName(existing->uniqueName);
            return;
        }

        // Definition `struct s { ... };`.
        std::shared_ptr<Symbol> sym;
        if (existing)
        {
            if (findStructLayout(existing->uniqueName))
            {
                error(structDecl.getLine(), structDecl.getCol(),
                      "redefinition of struct '" + source + "'.");
                return;
            }
            sym = existing; // completing a previously forward-declared tag
        }
        else
        {
            sym = std::make_shared<Symbol>(source, structDecl.baseType, structDecl.getLine(),
                                           structDecl.getCol(), Kind::STRUCT_TAG);
            symbolTable.insert(source, sym, Kind::STRUCT_TAG);
            sym->uniqueName = mangle();
            sym->node = &structDecl;
        }
        const std::string unique = sym->uniqueName;
        setResolvedName(unique);

        // The tag is now in scope (still incomplete), so a member that names this
        // very struct by value resolves to it and is caught below as an incomplete
        // member, while `struct s *` self-pointers resolve correctly.
        std::unordered_map<std::string, int> seen;
        StructLayout layout;
        long long offset = 0;
        int align = 1;
        bool ok = true;
        for (auto &f : structDecl.fields)
        {
            if (auto it = seen.find(f.name); it != seen.end())
            {
                error(f.line, f.column,
                      "redeclaration of struct field '" + f.name +
                          "'. Previous declaration at line " + std::to_string(it->second) + ".");
                ok = false;
                continue;
            }
            seen.emplace(f.name, f.line);

            resolveTags(f.type, f.line, f.column);
            if (std::dynamic_pointer_cast<FunctionType>(f.type))
            {
                error(f.line, f.column,
                      "struct member '" + f.name + "' cannot have function type.");
                ok = false;
                continue;
            }
            validateType(f.type, f.line, f.column);
            if (!isComplete(f.type))
            {
                error(f.line, f.column,
                      "struct member '" + f.name + "' has incomplete type " + f.type->toString() +
                          ".");
                ok = false;
                continue;
            }

            const int a = typeAlignOf(f.type);
            offset = (offset + a - 1) / a * a;
            layout.members.push_back({f.name, f.type, static_cast<int>(offset)});
            offset += sizeOfType(f.type);
            if (a > align)
                align = a;
        }

        if (!ok)
            return; // don't register a half-built layout (which would read complete)

        layout.alignment = align;
        layout.size = static_cast<int>((offset + align - 1) / align * align);
        structLayoutTable()[unique] = layout;
    }

    bool returnsAlways(const Statement *stmt)
    {
        if (!stmt)
            return false;
        if (isa<ReturnStmt>(stmt))
            return true;
        if (const auto *b = dyn_cast<BlockStmt>(stmt))
        {
            if (b->statements.empty())
                return false;
            return returnsAlways(b->statements.back().get());
        }
        if (const auto *i = dyn_cast<IfStmt>(stmt))
        {
            if (!i->elseBlock)
                return false;
            return returnsAlways(i->thenBlock.get()) && returnsAlways(i->elseBlock.get());
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

    bool evalConstDouble(const Expression *e, double &out)
    {
        switch (e->getKind())
        {
        case NodeKind::FloatingLiterals:
        {
            const auto *lit = cast<FloatingLiterals>(e);
            out = lit->value;
            return true;
        }
        case NodeKind::UnaryExpr:
        {
            const auto *u = cast<UnaryExpr>(e);
            double v;
            if (!evalConstDouble(u->operand.get(), v))
                return false;
            if (u->op == "-")
                out = -v;
            else if (u->op == "+")
                out = v;
            else
                return false;

            return true;
        }
        case NodeKind::BinaryExpr:
        {
            const auto *b = cast<BinaryExpr>(e);
            double l, r;
            if (!evalConstDouble(b->left.get(), l) || !evalConstDouble(b->right.get(), r))
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
        case NodeKind::CastExpr:
        {
            const auto *c = cast<CastExpr>(e);
            if (isDouble(c->operand->resolvedType))
                return evalConstDouble(c->operand.get(), out); // double -> double
            long long v;
            if (!evalConstInt(c->operand.get(), v)) // int -> double
                return false;
            out = isUnsigned(c->operand->resolvedType)
                      ? static_cast<double>(static_cast<unsigned long long>(v))
                      : static_cast<double>(v);
            return true;
        }
        default:
            return false;
        }
    }

    // Fold an integer constant expression. Returns false for anything with a
    // runtime value (variable reads, calls, non-constant operands).
    bool evalConstInt(const Expression *e, long long &out)
    {
        switch (e->getKind())
        {
        case NodeKind::IntLiterals:
        {
            const auto *lit = cast<IntLiterals>(e);
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
        case NodeKind::UnaryExpr:
        {
            const auto *u = cast<UnaryExpr>(e);
            long long v;
            if (!evalConstInt(u->operand.get(), v))
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
        case NodeKind::BinaryExpr:
        {
            const auto *b = cast<BinaryExpr>(e);
            long long l, r;
            if (!evalConstInt(b->left.get(), l) || !evalConstInt(b->right.get(), r))
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
        case NodeKind::CastExpr:
        {
            const auto *c = cast<CastExpr>(e);
            if (isDouble(c->operand->resolvedType))
            {
                // double -> integer cast: truncate toward zero, then narrow. An
                // unsigned target must convert through unsigned long long, since a
                // value >= 2^63 overflows a signed long long (UB / wrong bits).
                double dv;
                if (!evalConstDouble(c->operand.get(), dv))
                    return false;
                const long long bits =
                    isUnsigned(c->type)
                        ? static_cast<long long>(static_cast<unsigned long long>(dv))
                        : static_cast<long long>(dv);
                out = reduceToType(bits, c->type);
                return true;
            }
            long long v;
            if (!evalConstInt(c->operand.get(), v))
                return false;
            // Apply the cast's value conversion at compile time: reduce to the
            // target type's width and signedness (long / unsigned long keep all 64).
            out = reduceToType(v, c->type);
            return true;
        }
        default:
            return false;
        }
    }

    // Is `e` a valid initializer for a static-duration object? Integer constant
    // expressions, string literals, null, address-of-a-name, and constant
    // brace lists qualify; anything with a runtime value does not.
    bool isConstantInitializer(const Expression *e)
    {
        long long tmp;
        double tmp_d;
        if (evalConstInt(e, tmp))
            return true;
        if (evalConstDouble(e, tmp_d))
            return true;
        if (isa<StringLiterals>(e))
            return true;
        if (isNullPointerConstant(e))
            return true;
        if (const auto *u = dyn_cast<UnaryExpr>(e); u && u->op == "&")
            return isa<VariableExpr>(u->operand.get());
        if (const auto *in = dyn_cast<InitExpr>(e))
        {
            for (const auto &el : in->elements)
                if (!isConstantInitializer(el.get()))
                    return false;
            return true;
        }
        return false;
    }

    // Declare a variable with linkage/duration handling. Merges linked
    // redeclarations (file-scope vars, block-scope extern) into one symbol and
    // rejects conflicts; no-linkage locals follow the ordinary redeclaration rule.
    void declareVariable(VarDecl &var, bool fileScope, std::optional<StorageClass> sc, bool hasInit)
    {
        const std::string &name = var.name;
        resolveTags(var.type, var.getLine(), var.getCol());
        // A defined object must have a complete type: no void variables, no
        // incomplete structs, and no arrays of incomplete element type (including
        // nested under pointers). A pure `extern` declaration may be incomplete.
        if (isVoid(var.type))
            error(var.getLine(), var.getCol(), "variable '" + name + "' declared with type void.");
        else if (sc != StorageClass::Extern && !isComplete(var.type))
            error(var.getLine(), var.getCol(),
                  "variable '" + name + "' has incomplete type " + var.type->toString() + ".");
        validateType(var.type, var.getLine(), var.getCol());

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
                    error(var.getLine(), var.getCol(),
                          "'" + name + "' redeclared as different kind (" + kindToString(g->kind) +
                              " at line " + std::to_string(g->line) + ").");
                    return;
                }
                if (!g->type->equals(*var.type))
                    error(var.getLine(), var.getCol(),
                          "conflicting types for '" + name + "' (was " + g->type->toString() +
                              ", now " + var.type->toString() + ").");
                if (g->linkage != linkage)
                    error(var.getLine(), var.getCol(),
                          "conflicting linkage for '" + name + "' (previous declaration at line " +
                              std::to_string(g->line) + ").");
                if (hasInit)
                {
                    if (g->defined)
                        error(var.getLine(), var.getCol(),
                              "redefinition of '" + name + "' (previous definition at line " +
                                  std::to_string(g->line) + ").");
                    g->defined = true;
                    g->tentative = false;
                    g->node = &var;
                }
                else if (sc != StorageClass::Extern && !g->defined)
                {
                    g->tentative = true;
                }
                var.symbol = g;
            }
            else
            {
                auto sym = std::make_shared<Symbol>(name, var.type, var.getLine(), var.getCol(),
                                                    Kind::VARIABLE);
                sym->linkage = linkage;
                sym->duration = dur;
                if (hasInit)
                    sym->defined = true;
                else if (sc != StorageClass::Extern)
                    sym->tentative = true;
                symbolTable.insertLinked(name, sym);
                sym->node = &var;
                var.symbol = sym;
            }

            // Make it *visible* in the current scope (file scope for a file-scope
            // var, the block for a block-scope extern). A same-scope no-linkage
            // declaration of the same name conflicts.
            auto same = symbolTable.findSameScope(name, Kind::VARIABLE);
            if (!same)
                symbolTable.bindCurrent(name, var.symbol, Kind::VARIABLE);
            else if (same->linkage == Linkage::None)
                error(var.getLine(), var.getCol(),
                      "conflicting declaration of '" + name + "' (previous declaration at line " +
                          std::to_string(same->line) + ").");
            return;
        }

        // No linkage: block-scope local (plain or `static`).
        auto sym =
            std::make_shared<Symbol>(name, var.type, var.getLine(), var.getCol(), Kind::VARIABLE);
        sym->linkage = Linkage::None;
        sym->duration = dur;
        if (dur == StorageDuration::Static)
            sym->defined = true; // a block-scope static always defines (zero-init) storage
        if (!symbolTable.insert(name, sym, Kind::VARIABLE))
        {
            auto existing = symbolTable.findSameScope(name, Kind::VARIABLE);
            error(var.getLine(), var.getCol(),
                  "redeclaration of variable '" + name + "'" +
                      (existing ? " (previous declaration at line " +
                                      std::to_string(existing->line) + ")"
                                : "") +
                      ".");
            return;
        }
        sym->node = &var;
        var.symbol = sym;
    }

    // Type-check (and, for automatic-duration objects, convert) an initializer
    // against its target type. Brace lists recurse into array element types;
    // multi-dimensional arrays nest. A list shorter than the array leaves the
    // trailing elements implicitly zero. Leaf scalar initializers are already
    // analyzed (by the analyzeExpr pass over the whole initializer), so here we
    // only decay/convert them. Static initializers are validated but not
    // materialized (their constants are folded separately for the data section).
    void checkInitializer(const std::shared_ptr<Type> &targetType,
                          std::unique_ptr<Expression> &init, bool isStatic, int line, int col)
    {
        if (auto *initList = dyn_cast<InitExpr>(init.get()))
        {
            if (const auto arr = std::dynamic_pointer_cast<ArrayType>(targetType))
            {
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
            if (const auto layout = structLayoutOf(targetType))
            {
                // Bind each initializer to the corresponding member; a short list
                // leaves the trailing members zero-initialized.
                if (initList->elements.size() > layout->members.size())
                {
                    error(line, col,
                          "too many elements in initializer for " + targetType->toString() + " (" +
                              std::to_string(layout->members.size()) + " expected, " +
                              std::to_string(initList->elements.size()) + " given).");
                    return;
                }
                for (size_t i = 0; i < initList->elements.size(); ++i)
                    checkInitializer(layout->members[i].type, initList->elements[i], isStatic, line,
                                     col);
                initList->resolvedType = targetType;
                return;
            }
            error(line, col,
                  "compound initializer cannot initialize scalar type " + targetType->toString() +
                      ".");
            return;
        }

        // A single scalar initializer for this (sub-)object.
        if (const auto arr = std::dynamic_pointer_cast<ArrayType>(targetType))
        {
            // The only non-brace initializer an array accepts is a string literal
            // initializing a character array.
            const auto *strLit = dyn_cast<StringLiterals>(init.get());
            if (strLit && isCharacter(arr->getInner()))
            {
                // init->resolvedType is char[chars+1] (counting the null). The null
                // may be dropped on an exact fit, so it's the character count that
                // must not exceed the array length.
                const auto strArr = std::dynamic_pointer_cast<ArrayType>(init->resolvedType);
                if (strArr && strArr->getSize() - 1 > arr->getSize())
                    error(line, col, "string literal too long for " + targetType->toString() + ".");
                return;
            }
            if (strLit)
            {
                error(line, col,
                      "string literal cannot initialize non-character array " +
                          targetType->toString() + ".");
                return;
            }
            error(line, col,
                  "cannot initialize array type " + targetType->toString() +
                      " with a scalar initializer.");
            return;
        }

        init = decayArray(std::move(init));
        if (isStatic)
        {
            // Conversions aren't materialized for static initializers; the constant
            // was folded by the caller. Just validate assignability.
            if (!isAssignmentCompatible(targetType, init.get()))
                error(line, col,
                      "cannot initialize " + targetType->toString() + " with " +
                          init->resolvedType->toString() + ".");
        }
        else
        {
            init = convertByAssignment(std::move(init), targetType, line, col);
        }
    }

    // Fold a constant static initializer into its flat data image. Brace lists
    // recurse into element offsets and append a trailing Zero for any uninitialized
    // tail; scalar leaves are converted to the (sub-)object type.
    void buildStaticInits(const std::shared_ptr<Type> &targetType, const Expression *init,
                          std::vector<StaticInit> &out)
    {
        if (const auto *initList = dyn_cast<InitExpr>(init))
        {
            if (const auto arr = std::dynamic_pointer_cast<ArrayType>(targetType))
            {
                const auto elem = arr->getInner();
                for (const auto &element : initList->elements)
                    buildStaticInits(elem, element.get(), out);
                const long long pad =
                    static_cast<long long>(arr->getSize() - initList->elements.size()) *
                    sizeOfType(elem);
                if (pad > 0)
                    out.push_back(StaticInit::zero(pad));
                return;
            }
            const auto layout = structLayoutOf(targetType);
            // Walk members in offset order, emitting zero padding for the gaps
            // between members and for any uninitialized trailing members.
            long long cur = 0;
            for (size_t i = 0; i < initList->elements.size(); ++i)
            {
                const auto &m = layout->members[i];
                if (m.offset > cur)
                    out.push_back(StaticInit::zero(m.offset - cur));
                buildStaticInits(m.type, initList->elements[i].get(), out);
                cur = m.offset + sizeOfType(m.type);
            }
            if (layout->size > cur)
                out.push_back(StaticInit::zero(layout->size - cur));
            return;
        }

        // A string literal initializing a character array: the raw bytes followed by
        // a zero tail (which carries the null terminator when there is room for it).
        if (const auto *s = dyn_cast<StringLiterals>(init))
        {
            if (const auto arr = std::dynamic_pointer_cast<ArrayType>(targetType))
            {
                const std::string bytes = decodeStringLiteral(s->literal);
                out.push_back(StaticInit::str(bytes, /*nullTerminated=*/false));
                const long long pad =
                    static_cast<long long>(arr->getSize()) - static_cast<long long>(bytes.size());
                if (pad > 0)
                    out.push_back(StaticInit::zero(pad));
                return;
            }
        }
        // A string literal initializing a `char *` is decayed to `&"..."` by the
        // analyzer; store a pointer to the interned string (TACKY assigns the label
        // and emits the backing bytes).
        if (const auto *u = dyn_cast<UnaryExpr>(init);
            u && u->op == "&" && std::dynamic_pointer_cast<PointerType>(targetType))
        {
            if (const auto *s = dyn_cast<StringLiterals>(u->operand.get()))
            {
                out.push_back(StaticInit::ptrString(decodeStringLiteral(s->literal)));
                return;
            }
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
        const long long width = sizeOfType(targetType);
        out.push_back(width == 8   ? StaticInit::i64(v)
                      : width == 1 ? StaticInit::i8(v)
                                   : StaticInit::i32(v));
    }

    void analyzeVarDecl(VarDecl &variable)
    {
        const bool fileScope = variable.global;
        const auto sc = variable.storageClass;
        const bool hasInit = variable.initialization != nullptr;

        if (!fileScope && sc == StorageClass::Extern && hasInit)
            error(variable.getLine(), variable.getCol(),
                  "block-scope 'extern' variable '" + variable.name +
                      "' cannot have an initializer.");

        declareVariable(variable, fileScope, sc, hasInit);

        const bool staticDuration =
            variable.symbol && variable.symbol->duration == StorageDuration::Static;

        if (variable.initialization)
        {
            // An object with an initializer must have a complete type: a struct or
            // union with no known layout has no slots to initialize. (A bare
            // `extern struct s x;` with no initializer stays legal while s is
            // incomplete, which is why this is gated on having an initializer.)
            // Bail out here so the static-init builder never walks a missing layout.
            if (!isComplete(variable.type))
            {
                error(variable.getLine(), variable.getCol(),
                      "variable '" + variable.name + "' has an initializer but incomplete type " +
                          variable.type->toString() + ".");
                return;
            }

            analyzeExpr(*variable.initialization);

            // Static-duration variables require a constant initializer.
            if (staticDuration && !isConstantInitializer(variable.initialization.get()))
                error(variable.getLine(), variable.getCol(),
                      "initializer for '" + variable.name +
                          "' with static storage duration must be a constant expression.");

            checkInitializer(variable.type, variable.initialization, staticDuration,
                             variable.getLine(), variable.getCol());

            if (staticDuration && variable.symbol)
            {
                // A definition's image wins over any zero image left by an earlier
                // tentative redeclaration of the same object.
                variable.symbol->staticInits.clear();
                buildStaticInits(variable.type, variable.initialization.get(),
                                 variable.symbol->staticInits);
            }
        }
        else if (staticDuration && variable.symbol && variable.symbol->staticInits.empty())
        {
            // No initializer: the whole object is zero (.bss), unless a defining
            // declaration of the same object already supplied an image.
            variable.symbol->staticInits = {StaticInit::zero(sizeOfType(variable.type))};
        }
    }

    void analyzeDeclareStmt(DeclareStmt &stmt)
    {
        if (std::holds_alternative<std::vector<std::unique_ptr<VarDecl>>>(stmt.variables))
        {
            for (auto &var : std::get<std::vector<std::unique_ptr<VarDecl>>>(stmt.variables))
            {
                analyzeVarDecl(*var);
            }
        }
        else
        {
            analyzeStructDecl(*std::get<std::unique_ptr<StructDecl>>(stmt.variables));
        }
    }

    void analyzeReturnStmt(ReturnStmt &stmt)
    {
        if (!currentReturnType)
        {
            error(stmt.getLine(), stmt.col, "return statement not inside a function.");
            return;
        }
        if (stmt.returnExpression != nullptr)
        {
            analyzeAndDecay(stmt.returnExpression);
            int retLine = stmt.returnExpression->getLine();
            int retCol = stmt.returnExpression->col;
            stmt.returnExpression = convertByAssignment(std::move(stmt.returnExpression),
                                                        currentReturnType, retLine, retCol);
        }
        else
        {
            // A bare `return;` is only valid in a void function; a value-returning
            // function must return a value.
            if (!isVoid(currentReturnType))
                error(stmt.line, stmt.col, "non-void function must return a value.");
        }
    }

    void analyzeIfStmt(IfStmt &ifStmt)
    {
        analyzeAndDecay(ifStmt.condition);

        if (!isScalar(ifStmt.condition->resolvedType))
        {
            error(ifStmt.condition->getLine(), ifStmt.condition->getCol(),
                  "expected pointer or integer type, got - " +
                      ifStmt.condition->resolvedType->toString());
        }

        symbolTable.enterScope();
        analyzeStatements(*ifStmt.thenBlock);
        symbolTable.exitScope();

        if (ifStmt.elseBlock)
        {
            symbolTable.enterScope();
            analyzeStatements(*ifStmt.elseBlock);
            symbolTable.exitScope();
        }
    }

    void analyzeWhileStmt(WhileStmt &whileStmt)
    {
        int prevLoopLabel = loopLabel;
        loopLabel = ++labelCounter;
        whileStmt.label = loopLabel;
        analyzeAndDecay(whileStmt.condition);

        if (!isScalar(whileStmt.condition->resolvedType))
        {
            error(whileStmt.condition->getLine(), whileStmt.condition->getCol(),
                  "expected pointer or integer type, got - " +
                      whileStmt.condition->resolvedType->toString());
        }

        symbolTable.enterScope();
        analyzeStatements(*whileStmt.whileBlock);
        symbolTable.exitScope();
        loopLabel = prevLoopLabel;
    }

    void analyzeDoWhileStmt(DoWhileStmt &doWhileStmt)
    {
        int prevLoopLabel = loopLabel;
        loopLabel = ++labelCounter;
        doWhileStmt.label = loopLabel;
        analyzeAndDecay(doWhileStmt.condition);

        if (!isScalar(doWhileStmt.condition->resolvedType))
        {
            error(doWhileStmt.condition->getLine(), doWhileStmt.condition->getCol(),
                  "expected pointer or integer type, got - " +
                      doWhileStmt.condition->resolvedType->toString());
        }

        symbolTable.enterScope();
        analyzeStatements(*doWhileStmt.block);
        symbolTable.exitScope();
        loopLabel = prevLoopLabel;
    }

    void analyzeForStmt(ForStmt &forStmt)
    {
        int prevLoopLabel = loopLabel;
        loopLabel = ++labelCounter;
        forStmt.label = loopLabel;
        symbolTable.enterScope();

        if (forStmt.initialization)
        {
            if (auto *x = dyn_cast<ExprStmt>(forStmt.initialization.get()))
                analyzeExprStmt(*x);
            else if (auto *d = dyn_cast<DeclareStmt>(forStmt.initialization.get()))
                analyzeDeclareStmt(*d);
        }
        if (forStmt.condition)
        {
            if (auto *x = dyn_cast<ExprStmt>(forStmt.condition.get()))
            {
                analyzeExprStmt(*x);
                if (!isScalar(x->expr->resolvedType))
                {
                    error(x->expr->getLine(), x->expr->getCol(),
                          "condition in for loop must be pointer or integer got - " +
                              x->expr->resolvedType->toString());
                }
            }
        }
        if (forStmt.update)
        {
            if (auto *x = dyn_cast<ExprStmt>(forStmt.update.get()))
                analyzeExprStmt(*x);
        }

        symbolTable.enterScope();
        analyzeStatements(*forStmt.forBlock);
        symbolTable.exitScope();

        symbolTable.exitScope();
        loopLabel = prevLoopLabel;
    }

    void analyzeBreakContinueStmt(Statement &stmt)
    {
        if (loopLabel <= 0)
        {
            error(stmt.getLine(), stmt.getCol(), "no loop statements found");
        }
        if (auto *p = dyn_cast<BreakStmt>(&stmt))
        {
            p->label = loopLabel;
        }
        if (auto *p = dyn_cast<ContinueStmt>(&stmt))
        {
            p->label = loopLabel;
        }
    }

    void analyzeFunctionDeclStmt(FunctionDeclStmt &stmt)
    {
        if (stmt.declaration->storageClass == StorageClass::Static)
            error(stmt.declaration->getLine(), stmt.declaration->getCol(),
                  "block-scope function '" + stmt.declaration->name +
                      "' cannot be declared 'static'.");

        if (isArray(stmt.declaration->type))
            error(stmt.declaration->getLine(), stmt.declaration->getCol(),
                  "function '" + stmt.declaration->name + "' cannot return array type " +
                      stmt.declaration->type->toString() + ".");

        resolveTags(stmt.declaration->type, stmt.declaration->getLine(),
                    stmt.declaration->getCol());
        validateType(stmt.declaration->type, stmt.declaration->getLine(),
                     stmt.declaration->getCol());

        std::vector<std::shared_ptr<Type>> paramTypes;
        paramTypes.reserve(stmt.declaration->parameters.size());
        for (auto &param : stmt.declaration->parameters)
        {
            resolveTags(param.type, param.line, param.col);
            validateParam(param);
            param.type = adjustParamType(param.type);
            paramTypes.push_back(param.type);
        }
        auto functionType = std::make_shared<FunctionType>(stmt.declaration->type, paramTypes,
                                                           stmt.declaration->variadic);
        declareFunction(*stmt.declaration, functionType);
    }

    void analyzeStatements(BlockStmt &blockStmt)
    {
        for (auto &statement : blockStmt.statements)
        {
            switch (statement->getKind())
            {
            case NodeKind::DeclareStmt:
            {
                auto *x = cast<DeclareStmt>(statement.get());
                analyzeDeclareStmt(*x);
                break;
            }
            case NodeKind::ExprStmt:
            {
                auto *x = cast<ExprStmt>(statement.get());
                analyzeExprStmt(*x);
                break;
            }
            case NodeKind::BlockStmt:
            {
                auto *x = cast<BlockStmt>(statement.get());
                symbolTable.enterScope();
                analyzeStatements(*x);
                symbolTable.exitScope();
                break;
            }
            case NodeKind::ReturnStmt:
            {
                auto *x = cast<ReturnStmt>(statement.get());
                analyzeReturnStmt(*x);
                break;
            }
            case NodeKind::IfStmt:
            {
                auto *x = cast<IfStmt>(statement.get());
                analyzeIfStmt(*x);
                break;
            }
            case NodeKind::WhileStmt:
            {
                auto *x = cast<WhileStmt>(statement.get());
                analyzeWhileStmt(*x);
                break;
            }
            case NodeKind::DoWhileStmt:
            {
                auto *x = cast<DoWhileStmt>(statement.get());
                analyzeDoWhileStmt(*x);
                break;
            }
            case NodeKind::ForStmt:
            {
                auto *x = cast<ForStmt>(statement.get());
                analyzeForStmt(*x);
                break;
            }
            case NodeKind::BreakStmt:
            case NodeKind::ContinueStmt:
            {
                analyzeBreakContinueStmt(*statement);
                break;
            }
            case NodeKind::FunctionDeclStmt:
            {
                auto *x = cast<FunctionDeclStmt>(statement.get());
                analyzeFunctionDeclStmt(*x);
                break;
            }
            default:
                break;
            }
        }
    }

    void analyzeFunction(Function &node)
    {
        auto prev = currentReturnType;
        currentReturnType = node.type;

        if (isArray(node.type))
            error(node.getLine(), node.getCol(),
                  "function '" + node.name + "' cannot return array type " + node.type->toString() +
                      ".");

        resolveTags(node.type, node.getLine(), node.getCol());
        validateType(node.type, node.getLine(), node.getCol());

        std::vector<std::shared_ptr<Type>> paramTypes;
        paramTypes.reserve(node.parameters.size());
        for (auto &param : node.parameters)
        {
            resolveTags(param.type, param.line, param.col);
            validateParam(param);
            param.type = adjustParamType(param.type);
            paramTypes.push_back(param.type);
        }

        // Declaring with an incomplete struct param/return is fine, but defining
        // (or, elsewhere, calling) the function requires complete types so it can
        // actually pass and return those objects.
        if (node.statements)
        {
            if (!isVoid(node.type) && !isComplete(node.type))
                error(node.getLine(), node.getCol(),
                      "definition of '" + node.name + "' returns incomplete type " +
                          node.type->toString() + ".");
            for (const auto &param : node.parameters)
                if (!isComplete(param.type))
                    error(param.line, param.col,
                          "parameter '" + param.name + "' has incomplete type " +
                              param.type->toString() + ".");
        }

        auto functionType = std::make_shared<FunctionType>(node.type, paramTypes, node.variadic);
        declareFunction(node, functionType);
        symbolTable.enterScope();
        for (auto &param : node.parameters)
        {
            const auto paramSymbol = std::make_shared<Symbol>(param.name, param.type, param.line,
                                                              param.col, Kind::PARAMETER);
            if (!symbolTable.insert(param.name, paramSymbol, Kind::PARAMETER))
            {
                error(param.line, param.col, "duplicate parameter '" + param.name + "'");
            }
            // Use the mangled name downstream so a parameter never aliases a
            // file-scope object of the same spelling (TACKY emits the prologue move
            // into this name; the body resolves uses to the same symbol).
            param.name = paramSymbol->uniqueName;
        }

        if (node.name == "main")
        {
            auto &stmts = node.statements->statements;
            if (stmts.empty() || !isa<ReturnStmt>(stmts.back().get()))
            {
                stmts.push_back(std::make_unique<ReturnStmt>(
                    -1, -1, std::make_unique<IntLiterals>(-1, -1, 0, IntType::getInstance())));
            }
        }

        if (node.statements)
        {
            analyzeStatements(*node.statements);
            // if (!isVoid(node.type) && !returnsAlways(node.statements.get()))
            // {
            //     error(node.getLine(), node.getCol(),
            //           "not all control flow paths in '" + node.name + "' return a value.");
            // }
        }

        symbolTable.exitScope();
        currentReturnType = prev;
    }

    void validate(Program &program)
    {
        for (auto &node : program.nodes)
        {
            if (auto *x = dyn_cast<Function>(node.get()))
            {
                analyzeFunction(*x);
            }
            else if (auto *x = dyn_cast<VarDecl>(node.get()))
            {
                analyzeVarDecl(*x);
            }
            else if (auto *x = dyn_cast<StructDecl>(node.get()))
            {
                analyzeStructDecl(*x);
            }
            else
            {
                throw std::runtime_error("Should not be possible to come here");
            }
        }
    }
};

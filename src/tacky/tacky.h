#pragma once
#include "../ast/ASTNodes/Program.h"
#include "../ast/Expressions/BinaryExpr.h"
#include "../ast/Expressions/IntLiterals.h"
#include "../ast/Expressions/MemberExpr.h"
#include "../ast/Expressions/SizeOfExpr.h"
#include "../ast/Expressions/StringLiterals.h"
#include "../ast/Expressions/SubscriptExpr.h"
#include "../ast/Expressions/UnaryExpr.h"
#include "../ast/Statements/BlockStmt.h"
#include "../ast/Statements/ReturnStmt.h"
#include "../ast/TopLevelNodes/Function.h"
#include "../support/RTTI.h"
#include "../tacky/ast/ASTNodes/TackyProgram.h"
#include "../tacky/ast/TopLevelNodes/TackyFunction.h"
#include "../tacky/instructions/instructions.h"
#include "../tacky/instructions/val.h"
#include "../types/TypeQueries.h"
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
        // char / signed char are signed 1-byte; unsigned char is unsigned 1-byte.
        if (std::dynamic_pointer_cast<UnsignedCharType>(literalType))
            return ConstantType::UCHAR;
        if (std::dynamic_pointer_cast<CharType>(literalType) ||
            std::dynamic_pointer_cast<SignedCharType>(literalType))
            return ConstantType::SCHAR;
        return ConstantType::INT;
    }

    static bool isUnsignedType(const std::shared_ptr<Type> &t)
    {
        return std::dynamic_pointer_cast<UnsignedIntType>(t) != nullptr ||
               std::dynamic_pointer_cast<UnsignedLongType>(t) != nullptr ||
               std::dynamic_pointer_cast<UnsignedCharType>(t) != nullptr;
    }

    static bool isPtrType(const std::shared_ptr<Type> &t)
    {
        return std::dynamic_pointer_cast<PointerType>(t) != nullptr;
    }

    // Element size a pointer steps by: the size of what it points to.
    static long long pointeeSize(const std::shared_ptr<Type> &ptrType)
    {
        const auto p = std::dynamic_pointer_cast<PointerType>(ptrType);
        return p ? sizeOfType(p->getInner()) : 1;
    }

    // Record an aggregate local so codegen can size its stack slot. Arrays carry
    // only size/align; structs additionally carry their System V classification.
    void recordArrayObject(const std::string &name, const std::shared_ptr<Type> &t)
    {
        if (std::dynamic_pointer_cast<ArrayType>(t))
            arrayObjects[name] = ArrayObject{sizeOfType(t), objectAlignOf(t)};
        else if (const auto s = std::dynamic_pointer_cast<StructType>(t))
            structObjects[name] = classifyStruct(s->getName());
    }

    static bool isStructType(const std::shared_ptr<Type> &t)
    {
        return std::dynamic_pointer_cast<StructType>(t) != nullptr;
    }

  public:
    std::unordered_map<std::string, int> tempCounters;
    std::set<Symbol *> seenVarDecls;
    std::unordered_map<std::string, ArrayObject> arrayObjects;
    std::unordered_map<std::string, StructABI> structObjects;

    // Anonymous read-only constants for string literals used as values. Each gets a
    // label, its decoded bytes, and an alignment; emitted as null-terminated data.
    struct StringConstant
    {
        std::string label;
        std::string bytes;
        int align;
    };
    std::vector<StringConstant> stringConstants;
    int stringCounter = 0;

    std::string makeTemp(const std::string &&name)
    {
        tempCounters[name]++;
        return "." + name + std::to_string(tempCounters[name]);
    }

    // Intern a string literal as a static constant and return a value referring to
    // that object (by label). Taking its address (`&`/decay) yields a char*.
    // Intern raw bytes as an anonymous null-terminated string constant and return
    // its label. Shared by string-literal values and static `char *` initializers.
    std::string internBytes(const std::string &bytes)
    {
        const std::string label = makeTemp("Lstr.");
        const long long n = static_cast<long long>(bytes.size()) + 1; // + null terminator
        const int align = n >= 16 ? 16 : 1;
        stringConstants.push_back({label, bytes, align});
        return label;
    }

    TackyVal internStringLiteral(const StringLiterals *s)
    {
        return TackyVar(internBytes(decodeStringLiteral(s->literal)), ConstantType::POINTER);
    }

    // Element address ptrExpr +/- index*elementSize, the shared core of pointer
    // arithmetic and subscripting. Returns a POINTER-typed value.
    TackyVal scaledPointerAdd(const Expression *ptrExpr, const Expression *idxExpr, bool subtract,
                              int line, int col,
                              std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        auto ptr = processExpression(ptrExpr, instructions);
        auto idx = processExpression(idxExpr, instructions);
        const long long scale = pointeeSize(ptrExpr->resolvedType);

        TackyVar scaled{makeTemp("tmp."), ConstantType::LONG};
        instructions.push_back(std::make_unique<TackyBinary>(
            line, col, BinaryOp::Multiply, idx, TackyConstant(scale, ConstantType::LONG), scaled));

        TackyVar dst{makeTemp("tmp."), ConstantType::POINTER};
        instructions.push_back(std::make_unique<TackyBinary>(
            line, col, subtract ? BinaryOp::Subtract : BinaryOp::Add, ptr, scaled, dst));
        return dst;
    }

    // The address of a subscript a[i] == *(a + i): one operand is a pointer
    // (after decay), the other an integer, in either order.
    TackyVal subscriptAddress(const SubscriptExpr *sub,
                              std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        const bool lvalueIsPtr = isPtrType(sub->lvalue->resolvedType);
        const Expression *ptrExpr = lvalueIsPtr ? sub->lvalue.get() : sub->index.get();
        const Expression *idxExpr = lvalueIsPtr ? sub->index.get() : sub->lvalue.get();
        return scaledPointerAdd(ptrExpr, idxExpr, false, sub->line, sub->col, instructions);
    }

    ExpResult processLvalue(const Expression *e,
                            std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        if (const auto *u = dyn_cast<UnaryExpr>(e); u && u->op == "*")
            return DereferencedPointer{processExpression(u->operand.get(), instructions)};
        if (const auto *sub = dyn_cast<SubscriptExpr>(e))
            return DereferencedPointer{subscriptAddress(sub, instructions)};
        // A member access lvalue is the member's address (base + constant offset).
        if (const auto *m = dyn_cast<MemberExpr>(e))
            return DereferencedPointer{memberAddress(m, instructions)};
        // A string literal is an array object; its lvalue is the interned constant,
        // so `&"..."` becomes GetAddress of that constant's label.
        if (const auto *s = dyn_cast<StringLiterals>(e))
            return internStringLiteral(s);
        // plain variable (or anything else that's a simple operand)
        return processExpression(e, instructions);
    }

    static std::string structTagOf(const std::shared_ptr<Type> &t)
    {
        return std::dynamic_pointer_cast<StructType>(t)->getName();
    }

    // Address (a POINTER value) of any lvalue, exactly as the unary `&` operator
    // computes it: a dereferenced lvalue yields the pointer itself, anything else
    // is taken with GetAddress.
    TackyVal addressOf(const Expression *e,
                       std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        auto lv = processLvalue(e, instructions);
        if (auto *dp = std::get_if<DereferencedPointer>(&lv))
            return dp->ptr;
        auto *v = std::get_if<TackyVal>(&lv);
        TackyVar dst{makeTemp("tmp."), ConstantType::POINTER};
        instructions.push_back(
            std::make_unique<TackyGetAddress>(e->getLine(), e->getCol(), *v, dst));
        return dst;
    }

    // base + off as a POINTER value; off 0 returns base unchanged.
    TackyVal ptrPlus(TackyVal base, long long off, int line, int col,
                     std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        if (off == 0)
            return base;
        TackyVar dst{makeTemp("tmp."), ConstantType::POINTER};
        instructions.push_back(std::make_unique<TackyBinary>(
            line, col, BinaryOp::Add, base, TackyConstant(off, ConstantType::LONG), dst));
        return dst;
    }

    // Address of named object `name` at byte `offset`, as a POINTER value.
    TackyVal namedObjectAddress(const std::string &name, long long offset, int line, int col,
                                std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        TackyVar base{makeTemp("tmp."), ConstantType::POINTER};
        instructions.push_back(std::make_unique<TackyGetAddress>(
            line, col, TackyVar(name, ConstantType::POINTER), base));
        return ptrPlus(base, offset, line, col, instructions);
    }

    // Address (POINTER value) of the member `e.field` / `e->field`. For `->` the
    // base is the pointer operand; for `.` it is the address of the struct lvalue.
    TackyVal memberAddress(const MemberExpr *e,
                           std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        TackyVal base = e->isArrow ? processExpression(e->object.get(), instructions)
                                   : aggregateAddress(e->object.get(), instructions);
        const auto &ot = e->object->resolvedType;
        std::shared_ptr<StructType> st;
        if (auto p = std::dynamic_pointer_cast<PointerType>(ot))
            st = std::dynamic_pointer_cast<StructType>(p->getInner());
        else
            st = std::dynamic_pointer_cast<StructType>(ot);
        const StructLayout *layout = findStructLayout(st->getName());
        long long off = 0;
        for (const auto &m : layout->members)
            if (m.name == e->field)
            {
                off = m.offset;
                break;
            }
        return ptrPlus(base, off, e->line, e->col, instructions);
    }

    // Copy `size` bytes from the object at `srcAddr` to the object at `dstAddr`,
    // in 8/4/1-byte chunks (a struct's size is always a multiple of its alignment,
    // so this never reads past either object — important for page-boundary tests).
    void emitStructCopy(const TackyVal &dstAddr, const TackyVal &srcAddr, long long size, int line,
                        int col, std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        long long off = 0;
        auto chunk = [&](long long width, ConstantType ct)
        {
            while (size - off >= width)
            {
                TackyVal sp = ptrPlus(srcAddr, off, line, col, instructions);
                TackyVar tmp{makeTemp("tmp."), ct};
                instructions.push_back(std::make_unique<TackyLoad>(line, col, sp, tmp));
                TackyVal dp = ptrPlus(dstAddr, off, line, col, instructions);
                instructions.push_back(std::make_unique<TackyStore>(line, col, tmp, dp));
                off += width;
            }
        };
        chunk(8, ConstantType::LONG);
        chunk(4, ConstantType::INT);
        chunk(1, ConstantType::SCHAR);
    }

    // Materialize a struct-typed value into a named struct object and return that
    // object's name. A plain variable is already such an object; anything else is
    // copied into a fresh temporary.
    std::string freshStructTemp(const std::shared_ptr<Type> &type)
    {
        std::string name = makeTemp("struct.");
        structObjects[name] = classifyStruct(structTagOf(type));
        return name;
    }

    std::string structValueObject(const Expression *e,
                                  std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        if (const auto *v = dyn_cast<VariableExpr>(e))
        {
            const std::string name = v->symbol ? v->symbol->uniqueName : v->name;
            structObjects[name] = classifyStruct(structTagOf(e->resolvedType));
            return name;
        }
        const std::string name = freshStructTemp(e->resolvedType);
        TackyVal dstAddr = namedObjectAddress(name, 0, e->getLine(), e->getCol(), instructions);
        TackyVal srcAddr = aggregateAddress(e, instructions);
        emitStructCopy(dstAddr, srcAddr, sizeOfType(e->resolvedType), e->getLine(), e->getCol(),
                       instructions);
        return name;
    }

    // Address (POINTER value) of a struct-typed expression's storage. Lvalues give
    // their storage address directly; an assignment performs its copy and yields
    // the destination; a ternary selects a branch into a temporary; any other
    // rvalue (e.g. a struct-returning call) is materialized into a temporary.
    TackyVal aggregateAddress(const Expression *e,
                              std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        if (const auto *a = dyn_cast<AssignExpr>(e))
        {
            TackyVal dstAddr = addressOf(a->lhs.get(), instructions);
            TackyVal srcAddr = aggregateAddress(a->rhs.get(), instructions);
            emitStructCopy(dstAddr, srcAddr, sizeOfType(a->lhs->resolvedType), a->line, a->col,
                           instructions);
            return dstAddr;
        }
        if (const auto *t = dyn_cast<TernaryExpr>(e))
        {
            const std::string name = freshStructTemp(e->resolvedType);
            const long long size = sizeOfType(e->resolvedType);
            const std::string elseLabel = makeTemp("ternary_false.");
            const std::string endLabel = makeTemp("end.");
            auto cond = processExpression(t->condition.get(), instructions);
            instructions.push_back(
                std::make_unique<TackyJumpIfZero>(t->line, t->col, cond, elseLabel));
            TackyVal dThen = namedObjectAddress(name, 0, t->line, t->col, instructions);
            emitStructCopy(dThen, aggregateAddress(t->thenBranch.get(), instructions), size,
                           t->line, t->col, instructions);
            instructions.push_back(std::make_unique<TackyJump>(t->line, t->col, endLabel));
            instructions.push_back(std::make_unique<TackyLabel>(t->line, t->col, elseLabel));
            TackyVal dElse = namedObjectAddress(name, 0, t->line, t->col, instructions);
            emitStructCopy(dElse, aggregateAddress(t->elseBranch.get(), instructions), size,
                           t->line, t->col, instructions);
            instructions.push_back(std::make_unique<TackyLabel>(t->line, t->col, endLabel));
            return namedObjectAddress(name, 0, t->line, t->col, instructions);
        }
        if (isa<FunctionCallExpr>(e))
        {
            // The call lowers to a temporary struct object; take its address.
            TackyVal obj = processExpression(e, instructions);
            TackyVar dst{makeTemp("tmp."), ConstantType::POINTER};
            instructions.push_back(
                std::make_unique<TackyGetAddress>(e->getLine(), e->getCol(), obj, dst));
            return dst;
        }
        return addressOf(e, instructions);
    }

    TackyVal processFloatingLiteral(const FloatingLiterals *floatLiteral)
    {
        return TackyFloatingConstant{floatLiteral->value, returnConstantType(floatLiteral->type)};
    }

    TackyVal processIntLiteral(const IntLiterals *intLiteral)
    {
        return TackyConstant{intLiteral->value, returnConstantType(intLiteral->type)};
    }

    // The +/-1 step for ++/--: for a pointer it's the (byte) element size as a
    // long; for a double it's 1.0; otherwise 1 of the value's own type.
    TackyVal incrementStep(const std::shared_ptr<Type> &type)
    {
        const ConstantType ct = returnConstantType(type);
        if (isPtrType(type))
            return TackyConstant(pointeeSize(type), ConstantType::LONG);
        if (ct == ConstantType::DOUBLE)
            return TackyFloatingConstant(1.0, ConstantType::DOUBLE);
        return TackyConstant(1, ct);
    }

    TackyVal processIncrementDecrement(const UnaryExpr *unaryExpr,
                                       std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        const int line = unaryExpr->line, col = unaryExpr->col;
        const ConstantType ct = returnConstantType(unaryExpr->resolvedType);
        const BinaryOp op = unaryExpr->op == "++" ? BinaryOp::Add : BinaryOp::Subtract;
        const TackyVal delta = incrementStep(unaryExpr->resolvedType);

        auto lv = processLvalue(unaryExpr->operand.get(), instructions);

        // Plain modifiable lvalue (a variable): update it in place.
        if (auto *v = std::get_if<TackyVal>(&lv))
        {
            TackyVar oldVal{makeTemp("tmp."), ct};
            if (unaryExpr->isPostFix)
                instructions.push_back(std::make_unique<TackyCopy>(line, col, *v, oldVal));
            instructions.push_back(std::make_unique<TackyBinary>(line, col, op, *v, delta, *v));
            return unaryExpr->isPostFix ? TackyVal{oldVal} : *v;
        }

        // (*p)++ / arr[i]++: load through the pointer, update, store back. The
        // address is computed once (processLvalue), so side effects fire once.
        auto *dp = std::get_if<DereferencedPointer>(&lv);
        TackyVar cur{makeTemp("tmp."), ct};
        instructions.push_back(std::make_unique<TackyLoad>(line, col, dp->ptr, cur));
        TackyVar updated{makeTemp("tmp."), ct};
        instructions.push_back(std::make_unique<TackyBinary>(line, col, op, cur, delta, updated));
        instructions.push_back(std::make_unique<TackyStore>(line, col, updated, dp->ptr));
        return unaryExpr->isPostFix ? TackyVal{cur} : TackyVal{updated};
    }

    TackyVal processUnaryExpr(const UnaryExpr *unaryExpr,
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
            auto ptr = processExpression(unaryExpr->operand.get(), instructions); // the pointer
            TackyVar dst{makeTemp("tmp."), returnConstantType(unaryExpr->resolvedType)};
            instructions.push_back(
                std::make_unique<TackyLoad>(unaryExpr->line, unaryExpr->col, ptr, dst));
            return dst;
        }
        else if (unaryExpr->op == "&")
        {
            return addressOf(unaryExpr->operand.get(), instructions);
        }
        else
            throw std::runtime_error("unhandled unary op: " + unaryExpr->op);

        TackyVal inner = processExpression(unaryExpr->operand.get(), instructions);
        TackyVar dst{makeTemp("tmp."), returnConstantType(unaryExpr->resolvedType)};

        instructions.push_back(std::make_unique<TackyUnary>(unaryExpr->line, unaryExpr->col, op,
                                                            std::move(inner), dst));

        return dst;
    }

    TackyVal processLogicalAnd(const BinaryExpr *binaryExpr,
                               std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        TackyVar result(makeTemp("tmp."), returnConstantType(binaryExpr->resolvedType));
        std::string false_label = makeTemp("false_label.");
        std::string end_label = makeTemp("end_label.");
        auto lhs = processExpression(binaryExpr->left.get(), instructions);
        instructions.push_back(
            std::make_unique<TackyJumpIfZero>(binaryExpr->line, binaryExpr->col, lhs, false_label));
        auto rhs = processExpression(binaryExpr->right.get(), instructions);
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

    TackyVal processLogicalOr(const BinaryExpr *binaryExpr,
                              std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        TackyVar result(makeTemp("tmp."), returnConstantType(binaryExpr->resolvedType));
        std::string true_label = makeTemp("true_label.");
        std::string end_label = makeTemp("end_label.");
        auto lhs = processExpression(binaryExpr->left.get(), instructions);
        instructions.push_back(std::make_unique<TackyJumpIfNotZero>(
            binaryExpr->line, binaryExpr->col, lhs, true_label));
        auto rhs = processExpression(binaryExpr->right.get(), instructions);
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

    TackyVal processPointerArithmetic(const BinaryExpr *binaryExpr, bool lPtr, bool rPtr,
                                      std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        if (lPtr && rPtr)
        {
            // ptr - ptr (the analyzer guarantees the op is '-' and the types match):
            // byte distance / element size = signed element count.
            const long long scale = pointeeSize(binaryExpr->left->resolvedType);
            auto a = processExpression(binaryExpr->left.get(), instructions);
            auto b = processExpression(binaryExpr->right.get(), instructions);
            TackyVar diff{makeTemp("tmp."), ConstantType::LONG};
            instructions.push_back(std::make_unique<TackyBinary>(binaryExpr->line, binaryExpr->col,
                                                                 BinaryOp::Subtract, a, b, diff));
            TackyVar dst{makeTemp("tmp."), ConstantType::LONG};
            instructions.push_back(
                std::make_unique<TackyBinary>(binaryExpr->line, binaryExpr->col, BinaryOp::Divide,
                                              diff, TackyConstant(scale, ConstantType::LONG), dst));
            return dst;
        }

        // ptr +/- int, or int + ptr: scale the integer operand by the element size.
        const Expression *ptrExpr = lPtr ? binaryExpr->left.get() : binaryExpr->right.get();
        const Expression *idxExpr = lPtr ? binaryExpr->right.get() : binaryExpr->left.get();
        const bool subtract = binaryExpr->binaryOp == "-";
        return scaledPointerAdd(ptrExpr, idxExpr, subtract, binaryExpr->line, binaryExpr->col,
                                instructions);
    }

    TackyVal processBinaryExpr(const BinaryExpr *binaryExpr,
                               std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        if (binaryExpr->binaryOp == "&&")
            return processLogicalAnd(binaryExpr, instructions);
        if (binaryExpr->binaryOp == "||")
            return processLogicalOr(binaryExpr, instructions);

        const bool lPtr = isPtrType(binaryExpr->left->resolvedType);
        const bool rPtr = isPtrType(binaryExpr->right->resolvedType);
        if ((binaryExpr->binaryOp == "+" || binaryExpr->binaryOp == "-") && (lPtr || rPtr))
            return processPointerArithmetic(binaryExpr, lPtr, rPtr, instructions);

        BinaryOp op = stringToBinaryOp(binaryExpr->binaryOp);

        auto v1 = processExpression(binaryExpr->left.get(), instructions);
        auto v2 = processExpression(binaryExpr->right.get(), instructions);
        TackyVar dst{makeTemp("tmp."), returnConstantType(binaryExpr->resolvedType)};

        instructions.push_back(std::make_unique<TackyBinary>(binaryExpr->line, binaryExpr->col, op,
                                                             std::move(v1), std::move(v2), dst));

        return dst;
    }

    TackyVal processVariableExpr(const VariableExpr *variableExpr,
                                 std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        return TackyVar(variableExpr->symbol ? variableExpr->symbol->uniqueName
                                             : variableExpr->name,
                        returnConstantType(variableExpr->resolvedType));
    }

    // For `ptr += n` / `ptr -= n` the integer is scaled by the element size, like
    // ordinary pointer arithmetic; for any other compound assignment the rhs is
    // used as-is.
    TackyVal compoundDelta(const std::shared_ptr<Type> &lhsType, const std::string &op,
                           const TackyVal &rhs, int line, int col,
                           std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        if (isPtrType(lhsType) && (op == "+=" || op == "-="))
        {
            TackyVar scaled{makeTemp("tmp."), ConstantType::LONG};
            instructions.push_back(std::make_unique<TackyBinary>(
                line, col, BinaryOp::Multiply, rhs,
                TackyConstant(pointeeSize(lhsType), ConstantType::LONG), scaled));
            return scaled;
        }
        return rhs;
    }

    TackyVal processAssignExpr(const AssignExpr *assignExpr,
                               std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        const int line = assignExpr->line, col = assignExpr->col;

        // Whole-struct assignment is a byte copy (only `=` is valid on structs).
        // aggregateAddress performs the copy and yields the destination address.
        if (isStructType(assignExpr->lhs->resolvedType))
            return aggregateAddress(assignExpr, instructions);

        auto lhs = processLvalue(assignExpr->lhs.get(), instructions);
        auto rhs = processExpression(assignExpr->rhs.get(), instructions);

        const auto lhsType = assignExpr->lhs->resolvedType;

        if (auto *v = std::get_if<TackyVal>(&lhs))
        {
            if (assignExpr->op != "=")
            {
                BinaryOp op = stringToBinaryOp(assignExpr->op);
                if (assignExpr->computeType)
                {
                    // Character lhs: promote to the computation type, do the op, then
                    // convert the result back to the (narrow) lhs type.
                    TackyVal lhsC =
                        convertValue(*v, lhsType, assignExpr->computeType, line, col, instructions);
                    TackyVar res{makeTemp("tmp."), returnConstantType(assignExpr->computeType)};
                    instructions.push_back(
                        std::make_unique<TackyBinary>(line, col, op, lhsC, rhs, res));
                    TackyVal back = convertValue(res, assignExpr->computeType, lhsType, line, col,
                                                 instructions);
                    instructions.push_back(std::make_unique<TackyCopy>(line, col, back, *v));
                }
                else
                {
                    TackyVal delta =
                        compoundDelta(lhsType, assignExpr->op, rhs, line, col, instructions);
                    instructions.push_back(
                        std::make_unique<TackyBinary>(line, col, op, *v, delta, *v));
                }
            }
            else
            {
                instructions.push_back(std::make_unique<TackyCopy>(line, col, rhs, *v));
            }
            return *v;
        }
        if (auto *v = std::get_if<DereferencedPointer>(&lhs))
        {
            TackyVal stored = rhs;
            if (assignExpr->op != "=")
            {
                // *p OP= rhs  ->  load *p, combine, store back
                ConstantType ct = returnConstantType(lhsType); // type of *p
                TackyVar cur{makeTemp("tmp."), ct};
                instructions.push_back(std::make_unique<TackyLoad>(line, col, v->ptr, cur));
                BinaryOp op = stringToBinaryOp(assignExpr->op);
                if (assignExpr->computeType)
                {
                    // Character lvalue: promote, operate, convert back to char.
                    TackyVal lhsC = convertValue(cur, lhsType, assignExpr->computeType, line, col,
                                                 instructions);
                    TackyVar res{makeTemp("tmp."), returnConstantType(assignExpr->computeType)};
                    instructions.push_back(
                        std::make_unique<TackyBinary>(line, col, op, lhsC, rhs, res));
                    stored = convertValue(res, assignExpr->computeType, lhsType, line, col,
                                          instructions);
                }
                else
                {
                    TackyVar res{makeTemp("tmp."), ct};
                    TackyVal delta =
                        compoundDelta(lhsType, assignExpr->op, rhs, line, col, instructions);
                    instructions.push_back(
                        std::make_unique<TackyBinary>(line, col, op, cur, delta, res));
                    stored = res;
                }
            }
            instructions.push_back(std::make_unique<TackyStore>(line, col, stored, v->ptr));
            return stored;
        }
        return rhs;
    }

    TackyVal processTernaryExpr(const TernaryExpr *ternaryExpr,
                                std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        TackyVar temp = TackyVar(makeTemp("tmp."), returnConstantType(ternaryExpr->resolvedType));
        std::string false_label = makeTemp("ternary_false.");
        std::string end_label = makeTemp("end.");
        auto condition = processExpression(ternaryExpr->condition.get(), instructions);
        instructions.push_back(std::make_unique<TackyJumpIfZero>(
            ternaryExpr->line, ternaryExpr->col, condition, false_label));
        auto trueExpr = processExpression(ternaryExpr->thenBranch.get(), instructions);
        instructions.push_back(
            std::make_unique<TackyCopy>(ternaryExpr->line, ternaryExpr->col, trueExpr, temp));
        instructions.push_back(
            std::make_unique<TackyJump>(ternaryExpr->line, ternaryExpr->col, end_label));
        instructions.push_back(
            std::make_unique<TackyLabel>(ternaryExpr->line, ternaryExpr->col, false_label));
        auto falseExpr = processExpression(ternaryExpr->elseBranch.get(), instructions);
        instructions.push_back(
            std::make_unique<TackyCopy>(ternaryExpr->line, ternaryExpr->col, falseExpr, temp));
        instructions.push_back(
            std::make_unique<TackyLabel>(ternaryExpr->line, ternaryExpr->col, end_label));
        return temp;
    }

    TackyVal processFunctionCallExpr(const FunctionCallExpr *functionCallExpr,
                                     std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        std::vector<TackyVal> args;
        for (const auto &param : functionCallExpr->parameters)
        {
            // A by-value struct argument is passed as its named object (codegen
            // splits it into eightbytes per the calling convention).
            if (isStructType(param->resolvedType))
                args.push_back(
                    TackyVar(structValueObject(param.get(), instructions), ConstantType::POINTER));
            else
                args.push_back(processExpression(param.get(), instructions));
        }
        // A struct return materializes into a fresh struct object the caller owns.
        const bool structRet = isStructType(functionCallExpr->resolvedType);
        TackyVar dst{structRet ? freshStructTemp(functionCallExpr->resolvedType) : makeTemp("tmp."),
                     returnConstantType(functionCallExpr->resolvedType)};
        instructions.push_back(std::make_unique<TackyFunctionCall>(
            functionCallExpr->line, functionCallExpr->col, functionCallExpr->functionName->name,
            args, dst, functionCallExpr->calleeVariadic));
        return dst;
    }

    // Convert an already-computed value from srcType to dstType, emitting the
    // needed extend/truncate/SSE-conversion. Shared by explicit casts and the
    // promote-compute-truncate of character compound assignment.
    TackyVal convertValue(TackyVal result, const std::shared_ptr<Type> &srcType,
                          const std::shared_ptr<Type> &dstType, int line, int col,
                          std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        const ConstantType srcCt = returnConstantType(srcType);
        const ConstantType dstCt = returnConstantType(dstType);

        // char <-> double has no direct SSE form (cvtsi2sd/cvttsd2si take no byte
        // operand), so route through a 4-byte int: char -> int -> double, or
        // double -> int -> char.
        if (ctBytes(srcCt) == 1 && isDouble(dstCt))
        {
            TackyVar intTmp{makeTemp("tmp."), ConstantType::INT};
            if (isUnsignedType(srcType))
                instructions.push_back(
                    std::make_unique<TackyZeroExtend>(line, col, result, intTmp));
            else
                instructions.push_back(
                    std::make_unique<TackySignExtend>(line, col, result, intTmp));
            TackyVar dbl{makeTemp("tmp."), dstCt};
            instructions.push_back(std::make_unique<TackyIntToDouble>(line, col, intTmp, dbl));
            return dbl;
        }
        if (isDouble(srcCt) && ctBytes(dstCt) == 1)
        {
            TackyVar intTmp{makeTemp("tmp."), ConstantType::INT};
            instructions.push_back(std::make_unique<TackyDoubleToInt>(line, col, result, intTmp));
            TackyVar ch{makeTemp("tmp."), dstCt};
            instructions.push_back(std::make_unique<TackyTruncate>(line, col, intTmp, ch));
            return ch;
        }

        if (isDouble(srcCt) && isIntCt(dstCt))
        {
            TackyVar intDst{makeTemp("tmp."), dstCt};
            instructions.push_back(std::make_unique<TackyDoubleToInt>(line, col, result, intDst));
            return intDst;
        }
        if (isDouble(srcCt) && isUnsignedCt(dstCt))
        {
            TackyVar uIntDst{makeTemp("tmp."), dstCt};
            instructions.push_back(std::make_unique<TackyDoubleToUInt>(line, col, result, uIntDst));
            return uIntDst;
        }
        if (isIntCt(srcCt) && isDouble(dstCt))
        {
            TackyVar doubleCt{makeTemp("tmp."), dstCt};
            instructions.push_back(std::make_unique<TackyIntToDouble>(line, col, result, doubleCt));
            return doubleCt;
        }
        if (isUnsignedCt(srcCt) && isDouble(dstCt))
        {
            TackyVar doubleCt{makeTemp("tmp."), dstCt};
            instructions.push_back(
                std::make_unique<TackyUIntToDouble>(line, col, result, doubleCt));
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
            instructions.push_back(std::make_unique<TackyCopy>(line, col, result, sameWidthDst));
            return sameWidthDst;
        }

        TackyVar dst{makeTemp("tmp."), dstCt};

        if (ctBytes(dstCt) < ctBytes(srcCt))
        {
            // Narrowing: keep the low bytes.
            instructions.push_back(std::make_unique<TackyTruncate>(line, col, result, dst));
        }
        else if (isUnsignedType(srcType))
        {
            // Widening from an unsigned source: zero-fill the upper bits.
            instructions.push_back(std::make_unique<TackyZeroExtend>(line, col, result, dst));
        }
        else
        {
            // Widening from a signed source: replicate the sign bit.
            instructions.push_back(std::make_unique<TackySignExtend>(line, col, result, dst));
        }
        return dst;
    }

    TackyVal processCastExpr(const CastExpr *castExpr,
                             std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        auto result = processExpression(castExpr->operand.get(), instructions);
        // A cast to void only keeps the operand's side effects; the value it
        // produces is discarded, so emit no conversion.
        if (std::dynamic_pointer_cast<VoidType>(castExpr->type))
            return result;
        return convertValue(result, castExpr->operand->resolvedType, castExpr->type, castExpr->line,
                            castExpr->col, instructions);
    }

    TackyVal processExpression(const Expression *expression,
                               std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        if (const auto *p = dyn_cast<UnaryExpr>(expression))
        {
            return processUnaryExpr(p, instructions);
        }
        if (const auto *p = dyn_cast<IntLiterals>(expression))
        {
            return processIntLiteral(p);
        }
        if (const auto *p = dyn_cast<FloatingLiterals>(expression))
        {
            return processFloatingLiteral(p);
        }
        if (const auto *p = dyn_cast<BinaryExpr>(expression))
        {
            return processBinaryExpr(p, instructions);
        }
        if (const auto *p = dyn_cast<VariableExpr>(expression))
        {
            return processVariableExpr(p, instructions);
        }
        if (const auto *p = dyn_cast<AssignExpr>(expression))
        {
            return processAssignExpr(p, instructions);
        }
        if (const auto *p = dyn_cast<TernaryExpr>(expression))
        {
            return processTernaryExpr(p, instructions);
        }
        if (const auto *p = dyn_cast<FunctionCallExpr>(expression))
        {
            return processFunctionCallExpr(p, instructions);
        }
        if (const auto *p = dyn_cast<CastExpr>(expression))
        {
            return processCastExpr(p, instructions);
        }
        if (const auto *p = dyn_cast<SubscriptExpr>(expression))
        {
            // a[i] read: compute the element address and load through it.
            auto addr = subscriptAddress(p, instructions);
            TackyVar dst{makeTemp("tmp."), returnConstantType(p->resolvedType)};
            instructions.push_back(std::make_unique<TackyLoad>(p->line, p->col, addr, dst));
            return dst;
        }
        if (const auto *p = dyn_cast<StringLiterals>(expression))
        {
            // A string used directly as a value decays to a pointer to its first
            // byte: take the address of the interned constant.
            auto obj = internStringLiteral(p);
            TackyVar dst{makeTemp("tmp."), ConstantType::POINTER};
            instructions.push_back(std::make_unique<TackyGetAddress>(p->line, p->col, obj, dst));
            return dst;
        }
        if (const auto *p = dyn_cast<SizeOfExpr>(expression))
        {
            // sizeof folds to a compile-time unsigned long; its operand is never
            // evaluated, so the operand subtree is not lowered.
            const auto &sizedType = p->expr ? p->expr->resolvedType : p->type;
            return TackyConstant{sizeOfType(sizedType), ConstantType::ULONG};
        }
        if (const auto *p = dyn_cast<MemberExpr>(expression))
        {
            // A scalar member used as a value: take its address and load through it.
            // (Aggregate members are reached via array decay / aggregateAddress, so
            // a struct-typed member never arrives here.)
            auto addr = memberAddress(p, instructions);
            TackyVar dst{makeTemp("tmp."), returnConstantType(p->resolvedType)};
            instructions.push_back(std::make_unique<TackyLoad>(p->line, p->col, addr, dst));
            return dst;
        }
        throw std::runtime_error("TackyDriver::processExpression: unhandled expression kind");
    }

    TackyVal processVarDecl(const VarDecl &varDecl,
                            std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        return TackyVar(varDecl.symbol ? varDecl.symbol->uniqueName : varDecl.name,
                        returnConstantType(varDecl.type));
    }

    // Zero `nbytes` of object `dst` starting at `offset`, in quadword, longword,
    // then byte chunks (char arrays leave sub-4 tails).
    void emitZeroFill(const std::string &dst, long long offset, long long nbytes, int line, int col,
                      std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        while (nbytes >= 8)
        {
            instructions.push_back(std::make_unique<TackyCopyToOffset>(
                line, col, TackyConstant(0, ConstantType::LONG), dst, static_cast<int>(offset)));
            offset += 8;
            nbytes -= 8;
        }
        while (nbytes >= 4)
        {
            instructions.push_back(std::make_unique<TackyCopyToOffset>(
                line, col, TackyConstant(0, ConstantType::INT), dst, static_cast<int>(offset)));
            offset += 4;
            nbytes -= 4;
        }
        while (nbytes >= 1)
        {
            instructions.push_back(std::make_unique<TackyCopyToOffset>(
                line, col, TackyConstant(0, ConstantType::SCHAR), dst, static_cast<int>(offset)));
            offset += 1;
            nbytes -= 1;
        }
    }

    // Lower an automatic-duration initializer into the object `dst` at `offset`.
    // Brace lists recurse into element offsets; a list shorter than the array
    // zero-fills the trailing elements. Scalar leaves store via CopyToOffset.
    void emitInitializer(const std::shared_ptr<Type> &targetType, const Expression *init,
                         const std::string &dst, long long offset,
                         std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        if (const auto *initList = dyn_cast<InitExpr>(init))
        {
            // Struct brace initializer: bind element i to member i, zero-filling the
            // padding gaps between members and any uninitialized trailing members.
            if (const auto layout = structLayoutOf(targetType))
            {
                long long cur = 0;
                for (size_t i = 0; i < initList->elements.size(); ++i)
                {
                    const auto &m = layout->members[i];
                    if (m.offset > cur)
                        emitZeroFill(dst, offset + cur, m.offset - cur, init->getLine(),
                                     init->getCol(), instructions);
                    emitInitializer(m.type, initList->elements[i].get(), dst, offset + m.offset,
                                    instructions);
                    cur = m.offset + sizeOfType(m.type);
                }
                if (layout->size > cur)
                    emitZeroFill(dst, offset + cur, layout->size - cur, init->getLine(),
                                 init->getCol(), instructions);
                return;
            }

            const auto arr = std::dynamic_pointer_cast<ArrayType>(targetType);
            const long long elemSize = sizeOfType(arr->getInner());
            for (size_t i = 0; i < initList->elements.size(); ++i)
                emitInitializer(arr->getInner(), initList->elements[i].get(), dst,
                                offset + static_cast<long long>(i) * elemSize, instructions);

            const long long written = static_cast<long long>(initList->elements.size()) * elemSize;
            const long long total = sizeOfType(targetType);
            emitZeroFill(dst, offset + written, total - written, init->getLine(), init->getCol(),
                         instructions);
            return;
        }

        // A struct member/element initialized by a (non-brace) struct expression:
        // byte-copy that value into the object at this offset.
        if (isStructType(targetType))
        {
            TackyVal dstAddr =
                namedObjectAddress(dst, offset, init->getLine(), init->getCol(), instructions);
            TackyVal srcAddr = aggregateAddress(init, instructions);
            emitStructCopy(dstAddr, srcAddr, sizeOfType(targetType), init->getLine(),
                           init->getCol(), instructions);
            return;
        }

        // A string literal initializing a character array: copy each byte into the
        // object, then zero-fill the tail (which supplies the null terminator when
        // the array has room for it).
        if (const auto *s = dyn_cast<StringLiterals>(init))
        {
            if (std::dynamic_pointer_cast<ArrayType>(targetType))
            {
                const std::string bytes = decodeStringLiteral(s->literal);
                for (size_t i = 0; i < bytes.size(); ++i)
                    instructions.push_back(std::make_unique<TackyCopyToOffset>(
                        s->getLine(), s->getCol(),
                        TackyConstant(static_cast<unsigned char>(bytes[i]), ConstantType::SCHAR),
                        dst, static_cast<int>(offset + static_cast<long long>(i))));
                const long long total = sizeOfType(targetType);
                emitZeroFill(dst, offset + static_cast<long long>(bytes.size()),
                             total - static_cast<long long>(bytes.size()), s->getLine(),
                             s->getCol(), instructions);
                return;
            }
        }

        auto val = processExpression(init, instructions);
        instructions.push_back(std::make_unique<TackyCopyToOffset>(
            init->getLine(), init->getCol(), val, dst, static_cast<int>(offset)));
    }

    void processReturnStmt(const ReturnStmt &returnStmt,
                           std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        std::optional<TackyVal> returnVal;
        if (returnStmt.returnExpression)
        {
            // A struct return value is handed to codegen as its named object; codegen
            // packs it into return registers or writes it through the hidden pointer.
            if (isStructType(returnStmt.returnExpression->resolvedType))
                returnVal =
                    TackyVar(structValueObject(returnStmt.returnExpression.get(), instructions),
                             ConstantType::POINTER);
            else
                returnVal = processExpression(returnStmt.returnExpression.get(), instructions);
        }
        instructions.push_back(
            std::make_unique<TackyReturn>(returnStmt.line, returnStmt.col, std::move(returnVal)));
    }

    void processDeclareStmt(const DeclareStmt &declareStmt,
                            std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        if (const auto *vars =
                std::get_if<std::vector<std::unique_ptr<VarDecl>>>(&declareStmt.variables))
        {
            for (const auto &vd : *vars)
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
                    auto var = processVarDecl(*vd, instructions);
                    const std::string &name = vd->symbol ? vd->symbol->uniqueName : vd->name;
                    recordArrayObject(name, vd->type);
                    if (vd->initialization)
                    {
                        if (std::dynamic_pointer_cast<ArrayType>(vd->type) ||
                            (isStructType(vd->type) && isa<InitExpr>(vd->initialization.get())))
                            emitInitializer(vd->type, vd->initialization.get(), name, 0,
                                            instructions);
                        else if (isStructType(vd->type))
                        {
                            // Copy-initialize from another struct value.
                            TackyVal dstAddr = namedObjectAddress(name, 0, declareStmt.line,
                                                                  declareStmt.col, instructions);
                            TackyVal srcAddr =
                                aggregateAddress(vd->initialization.get(), instructions);
                            emitStructCopy(dstAddr, srcAddr, sizeOfType(vd->type), declareStmt.line,
                                           declareStmt.col, instructions);
                        }
                        else
                        {
                            auto val = processExpression(vd->initialization.get(), instructions);
                            instructions.push_back(std::make_unique<TackyCopy>(
                                declareStmt.line, declareStmt.col, val, var));
                        }
                    }
                }
            }
        }
        else
        {
            // TODO
        }
    }

    void processExprStmt(const ExprStmt &exprStmt,
                         std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        processExpression(exprStmt.expr.get(), instructions);
    }

    void processIfStmt(const IfStmt &ifStmt,
                       std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        std::string end_label = makeTemp("end.");

        auto condition = processExpression(ifStmt.condition.get(), instructions);
        if (ifStmt.elseBlock)
        {
            std::string else_label = makeTemp("else.");
            instructions.push_back(
                std::make_unique<TackyJumpIfZero>(ifStmt.line, ifStmt.col, condition, else_label));
            processBlockStmt(*ifStmt.thenBlock, instructions);
            instructions.push_back(std::make_unique<TackyJump>(ifStmt.line, ifStmt.col, end_label));
            instructions.push_back(
                std::make_unique<TackyLabel>(ifStmt.line, ifStmt.col, else_label));
            processBlockStmt(*ifStmt.elseBlock, instructions);
        }
        else
        {
            instructions.push_back(
                std::make_unique<TackyJumpIfZero>(ifStmt.line, ifStmt.col, condition, end_label));
            processBlockStmt(*ifStmt.thenBlock, instructions);
        }
        instructions.push_back(std::make_unique<TackyLabel>(ifStmt.line, ifStmt.col, end_label));
    }

    void processBreakStmt(const BreakStmt &breakStmt,
                          std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        instructions.push_back(std::make_unique<TackyJump>(
            breakStmt.line, breakStmt.col, ".break_label" + std::to_string(breakStmt.label)));
    }

    void processContinueStmt(const ContinueStmt &continueStmt,
                             std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        instructions.push_back(
            std::make_unique<TackyJump>(continueStmt.line, continueStmt.col,
                                        ".continue_label" + std::to_string(continueStmt.label)));
    }

    void processDoWhileStmt(const DoWhileStmt &doWhileStmt,
                            std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        std::string labelId = std::to_string(doWhileStmt.label);
        instructions.push_back(std::make_unique<TackyLabel>(doWhileStmt.line, doWhileStmt.col,
                                                            ".start_label" + labelId));
        processBlockStmt(*doWhileStmt.block, instructions);
        instructions.push_back(std::make_unique<TackyLabel>(doWhileStmt.line, doWhileStmt.col,
                                                            ".continue_label" + labelId));
        auto result = processExpression(doWhileStmt.condition.get(), instructions);
        instructions.push_back(std::make_unique<TackyJumpIfNotZero>(
            doWhileStmt.line, doWhileStmt.col, result, ".start_label" + labelId));
        instructions.push_back(std::make_unique<TackyLabel>(doWhileStmt.line, doWhileStmt.col,
                                                            ".break_label" + labelId));
    }

    void processWhileStmt(const WhileStmt &whileStmt,
                          std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        std::string labelId = std::to_string(whileStmt.label);
        instructions.push_back(std::make_unique<TackyLabel>(whileStmt.line, whileStmt.col,
                                                            ".continue_label" + labelId));
        auto result = processExpression(whileStmt.condition.get(), instructions);
        instructions.push_back(std::make_unique<TackyJumpIfZero>(whileStmt.line, whileStmt.col,
                                                                 result, ".break_label" + labelId));
        processBlockStmt(*whileStmt.whileBlock, instructions);
        instructions.push_back(std::make_unique<TackyJump>(whileStmt.line, whileStmt.col,
                                                           ".continue_label" + labelId));
        instructions.push_back(
            std::make_unique<TackyLabel>(whileStmt.line, whileStmt.col, ".break_label" + labelId));
    }

    void processForStmt(const ForStmt &forStmt,
                        std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        std::string labelId = std::to_string(forStmt.label);

        if (forStmt.initialization)
        {
            processStatement(*forStmt.initialization, instructions);
        }
        instructions.push_back(
            std::make_unique<TackyLabel>(forStmt.line, forStmt.col, ".start_label" + labelId));
        if (forStmt.condition)
        {
            const auto *condStmt = dyn_cast<ExprStmt>(forStmt.condition.get());
            auto condVal = processExpression(condStmt->expr.get(), instructions);
            instructions.push_back(std::make_unique<TackyJumpIfZero>(
                forStmt.line, forStmt.col, condVal, ".break_label" + labelId));
        }
        if (forStmt.forBlock)
        {
            processBlockStmt(*forStmt.forBlock, instructions);
        }
        instructions.push_back(
            std::make_unique<TackyLabel>(forStmt.line, forStmt.col, ".continue_label" + labelId));
        if (forStmt.update)
        {
            processStatement(*forStmt.update, instructions);
        }
        instructions.push_back(
            std::make_unique<TackyJump>(forStmt.line, forStmt.col, ".start_label" + labelId));
        instructions.push_back(
            std::make_unique<TackyLabel>(forStmt.line, forStmt.col, ".break_label" + labelId));
    }

    void processStatement(const Statement &stmt,
                          std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        if (const auto *p = dyn_cast<ReturnStmt>(&stmt))
        {
            processReturnStmt(*p, instructions);
        }
        else if (const auto *p = dyn_cast<DeclareStmt>(&stmt))
        {
            processDeclareStmt(*p, instructions);
        }
        else if (const auto *p = dyn_cast<ExprStmt>(&stmt))
        {
            processExprStmt(*p, instructions);
        }
        else if (const auto *p = dyn_cast<IfStmt>(&stmt))
        {
            processIfStmt(*p, instructions);
        }
        else if (const auto *p = dyn_cast<BlockStmt>(&stmt))
        {
            processBlockStmt(*p, instructions);
        }
        else if (const auto *p = dyn_cast<WhileStmt>(&stmt))
        {
            processWhileStmt(*p, instructions);
        }
        else if (const auto *p = dyn_cast<DoWhileStmt>(&stmt))
        {
            processDoWhileStmt(*p, instructions);
        }
        else if (const auto *p = dyn_cast<ForStmt>(&stmt))
        {
            processForStmt(*p, instructions);
        }
        else if (const auto *p = dyn_cast<ContinueStmt>(&stmt))
        {
            processContinueStmt(*p, instructions);
        }
        else if (const auto *p = dyn_cast<BreakStmt>(&stmt))
        {
            processBreakStmt(*p, instructions);
        }
    }

    void processBlockStmt(const BlockStmt &blockStmt,
                          std::vector<std::unique_ptr<TackyInstruction>> &instructions)
    {
        for (const auto &stmt : blockStmt.statements)
        {
            processStatement(*stmt, instructions);
        }
    }

    std::unique_ptr<TackyFunction> processFunction(const Function &functionNode)
    {
        std::vector<std::unique_ptr<TackyInstruction>> instructions;
        std::vector<std::pair<std::string, ConstantType>> params;
        for (const auto &param : functionNode.parameters)
        {
            // A by-value struct parameter needs a sized slot and a classification so
            // codegen can reassemble it from its incoming registers/stack bytes.
            if (isStructType(param.type))
                structObjects[param.name] = classifyStruct(structTagOf(param.type));
            params.push_back({param.name, returnConstantType(param.type)});
        }
        processBlockStmt(*functionNode.statements, instructions);
        // Every function gets an implicit `return 0` appended. If control reaches
        // it, no earlier return fired (e.g. the body falls off the end); if an
        // earlier return already fired, this is dead code after a ret. Either way
        // it guarantees the function ends in a return so codegen emits a final ret.
        instructions.push_back(std::make_unique<TackyReturn>(functionNode.line, functionNode.col,
                                                             TackyConstant(0, ConstantType::INT)));
        bool global = functionNode.symbol && functionNode.symbol->linkage == Linkage::External;
        auto fn =
            std::make_unique<TackyFunction>(functionNode.line, functionNode.col, functionNode.name,
                                            global, std::move(instructions), params);
        // Function::type is the return type; a struct return drives the hidden-pointer
        // (MEMORY) or register packing convention in codegen.
        if (isStructType(functionNode.type))
        {
            fn->returnsStruct = true;
            fn->returnABI = classifyStruct(structTagOf(functionNode.type));
        }
        return fn;
    }

    std::unique_ptr<TackyProgram> tacky(Program &prog)
    {
        std::vector<std::unique_ptr<TackyTopLevelNode>> nodes;
        for (const auto &node : prog.nodes)
        {
            if (const auto *p = dyn_cast<Function>(node.get()))
            {
                if (!p->statements)
                    continue;
                nodes.push_back(processFunction(*p));
            }
            else if (const auto *p = dyn_cast<VarDecl>(node.get()))
            {
                seenVarDecls.insert(p->symbol.get());
            }
        }

        for (const auto &symbol : seenVarDecls)
        {
            if (!symbol->defined && !symbol->tentative)
                continue;
            std::vector<StaticInit> inits = symbol->staticInits;
            if (inits.empty())
                inits.push_back(StaticInit::zero(sizeOfType(symbol->type)));
            // A static `char *` initialized from a string literal: intern the bytes
            // and replace the placeholder with a pointer to that constant's label.
            for (auto &si : inits)
                if (si.kind == StaticInit::Kind::PointerString)
                    si = StaticInit::ptrLabel(internBytes(si.strVal));
            nodes.push_back(std::make_unique<TackyStaticVariable>(
                symbol->line, symbol->column, symbol->uniqueName,
                symbol->linkage == Linkage::External, std::move(inits),
                objectAlignOf(symbol->type)));
        }
        // Emit interned string literals as null-terminated static constants.
        for (const auto &sc : stringConstants)
        {
            std::vector<StaticInit> inits{StaticInit::str(sc.bytes, /*nullTerminated=*/true)};
            nodes.push_back(std::make_unique<TackyStaticVariable>(prog.line, prog.col, sc.label,
                                                                  /*global=*/false,
                                                                  std::move(inits), sc.align));
        }

        auto program = std::make_unique<TackyProgram>(prog.line, prog.col, std::move(nodes));
        // Record every static-storage name (incl. extern-only declarations) so
        // codegen resolves their references to RIP-relative data, not stack slots.
        for (const auto &symbol : seenVarDecls)
            program->staticNames.insert(symbol->uniqueName);
        // String-constant labels are static data too: references lower to RIP data.
        for (const auto &sc : stringConstants)
            program->staticNames.insert(sc.label);
        // Static-storage struct objects also need a classification (e.g. when one is
        // passed or returned by value), though their storage is data, not a slot.
        for (const auto &symbol : seenVarDecls)
            if (isStructType(symbol->type))
                structObjects[symbol->uniqueName] = classifyStruct(structTagOf(symbol->type));
        program->arrayObjects = std::move(arrayObjects);
        program->structObjects = std::move(structObjects);
        return program;
    }
};

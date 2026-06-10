#pragma once
#include "../support/RTTI.h"
#include "ast/ASTNodes/TackyProgram.h"
#include "ast/TopLevelNodes/TackyFunction.h"
#include "instructions/instructions.h"

#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Optimization
// ---------------------------------------------------------------------------

// Which passes are enabled. --optimize turns on all four; each pass also has its
// own flag so they can be exercised in isolation.
struct OptimizationFlags
{
    bool foldConstants = false;
    bool eliminateUnreachableCode = false;
    bool propagateCopies = false;
    bool eliminateDeadStores = false;

    bool any() const
    {
        return foldConstants || eliminateUnreachableCode || propagateCopies || eliminateDeadStores;
    }
};

// A node of the control-flow graph: a maximal straight-line run of instructions,
// plus its adjacency. `id` is the block's index into CFG::blocks, which is also
// how predecessors/successors refer to it. ENTRY and EXIT are empty blocks at the
// front and back of CFG::blocks.
struct BasicBlock
{
    int id = -1;          // index into CFG::blocks; assigned by makeControlFlowGraph
    bool removed = false; // flagged dead by a pass; skipped when flattening / iterating
    std::vector<std::unique_ptr<TackyInstruction>> instructions;
    std::vector<int> predecessors;
    std::vector<int> successors;
};

// The control-flow graph for one function. blocks[0] is ENTRY, blocks.back() is
// EXIT (both instruction-free); the real blocks sit between them in program order,
// so concatenating every block's instructions reproduces the original list.
struct CFG
{
    std::vector<BasicBlock> blocks;
    int entryId() const { return 0; }
    int exitId() const { return static_cast<int>(blocks.size()) - 1; }

    // A directed edge is mirrored in both endpoints: `to` is a successor of `from`,
    // and `from` is a predecessor of `to`.
    void addEdge(int from, int to)
    {
        blocks[from].successors.push_back(to);
        blocks[to].predecessors.push_back(from);
    }
    void removeEdge(int from, int to)
    {
        std::erase(blocks[from].successors, to);
        std::erase(blocks[to].predecessors, from);
    }
};

// ---------------------------------------------------------------------------
// The four passes (stubs to fill in).
//
// Each is a no-op placeholder so the pipeline builds and the test suite stays
// green until it is implemented. Constant folding rewrites the instruction list
// directly; the other three operate on the CFG. Every pass sets `changed` to
// true iff it modified anything, so the driver can iterate to a fixed point.
//
// makeControlFlowGraph / cfgToInstructions start as a faithful single-block
// identity round-trip, so enabling a CFG pass before its body exists leaves the
// program unchanged rather than corrupting it. Split into real basic blocks when
// you implement them.
// ---------------------------------------------------------------------------

inline unsigned long long normalize(unsigned long long bits, ConstantType t)
{
    const int w = ctBytes(t); // 1, 4, or 8
    if (w == 8)
        return bits;
    const int shift = 64 - 8 * w;
    return isUnsignedCt(t) ? bits & ((1ull << (8 * w)) - 1)
                           : (unsigned long long)(((long long)(bits << shift)) >> shift);
}

// Fold a unary op over a constant operand. `dstType` is the instruction's result
// type (u->dst's type); `!` always yields int.
inline TackyVal evalUnary(UnaryOp op, const TackyVal &src, ConstantType dstType)
{
    // Floating operand: only `-` and `!` are valid on a double.
    if (const auto *f = std::get_if<TackyFloatingConstant>(&src))
    {
        if (op == UnaryOp::Negate)
            return TackyFloatingConstant(-f->value, ConstantType::DOUBLE);
        return TackyConstant(f->value == 0.0 ? 1 : 0, ConstantType::INT); // !
    }

    const auto &c = std::get<TackyConstant>(src);
    switch (op)
    {
    case UnaryOp::Complement:
        return TackyConstant(normalize(~c.value, dstType), dstType);
    case UnaryOp::Negate:
        return TackyConstant(normalize(0ull - c.value, dstType), dstType);
    case UnaryOp::Not:
        return TackyConstant(normalize(c.value, c.type) == 0 ? 1 : 0, ConstantType::INT);
    default:
        break;
    }
    return c;
}

// Fold a binary op over two constant operands. `dstType` is the result type
// (b->dst's type). Operand signedness/width comes from the operands themselves;
// for shifts that is the left operand, which is also the result type. Total: a
// caller must reject integer divide/remainder by zero before calling.
inline TackyVal evalBinary(BinaryOp op, const TackyVal &lhs, const TackyVal &rhs,
                           ConstantType dstType)
{
    // Double operands: SA converts both sides to a common type, so a binary never
    // mixes an int and a double — if one side is floating, both are.
    if (const auto *fl = std::get_if<TackyFloatingConstant>(&lhs))
    {
        const double x = fl->value;
        const double y = std::get<TackyFloatingConstant>(rhs).value;
        switch (op)
        {
        case BinaryOp::Add:
            return TackyFloatingConstant(x + y, ConstantType::DOUBLE);
        case BinaryOp::Subtract:
            return TackyFloatingConstant(x - y, ConstantType::DOUBLE);
        case BinaryOp::Multiply:
            return TackyFloatingConstant(x * y, ConstantType::DOUBLE);
        case BinaryOp::Divide:
            return TackyFloatingConstant(x / y, ConstantType::DOUBLE);
        case BinaryOp::Equal:
            return TackyConstant((x == y) ? 1 : 0, ConstantType::INT);
        case BinaryOp::NotEqual:
            return TackyConstant((x != y) ? 1 : 0, ConstantType::INT);
        case BinaryOp::LessThan:
            return TackyConstant((x < y) ? 1 : 0, ConstantType::INT);
        case BinaryOp::LessOrEqual:
            return TackyConstant((x <= y) ? 1 : 0, ConstantType::INT);
        case BinaryOp::GreaterThan:
            return TackyConstant((x > y) ? 1 : 0, ConstantType::INT);
        case BinaryOp::GreaterThanOrEqual:
            return TackyConstant((x >= y) ? 1 : 0, ConstantType::INT);
        default:
            break; // %, bitwise, shifts never apply to doubles
        }
        return lhs; // unreachable for well-formed TACKY
    }

    const auto &c1 = std::get<TackyConstant>(lhs);
    const auto &c2 = std::get<TackyConstant>(rhs);
    const bool uns = isUnsignedCt(c1.type); // operands share type (left's, for shifts)

    // Canonical 64-bit forms: signed types arrive sign-extended, unsigned zero-extended.
    const unsigned long long u1 = normalize(c1.value, c1.type);
    const unsigned long long u2 = normalize(c2.value, c2.type);
    const long long s1 = static_cast<long long>(u1);
    const long long s2 = static_cast<long long>(u2);
    const int shMask = 8 * ctBytes(c1.type) - 1; // x86 masks the shift count to the operand width

    auto I = [](bool cond) { return TackyConstant(cond ? 1 : 0, ConstantType::INT); };
    auto R = [&](unsigned long long bits)
    { return TackyConstant(normalize(bits, dstType), dstType); };

    switch (op)
    {
    case BinaryOp::Add:
        return R(u1 + u2);
    case BinaryOp::Subtract:
        return R(u1 - u2);
    case BinaryOp::Multiply:
        return R(u1 * u2);
    case BinaryOp::BitwiseAnd:
        return R(u1 & u2);
    case BinaryOp::BitwiseOr:
        return R(u1 | u2);
    case BinaryOp::BitwiseXor:
        return R(u1 ^ u2);
    case BinaryOp::Divide:
        return uns ? R(u1 / u2) : R(static_cast<unsigned long long>(s1 / s2));
    case BinaryOp::Remainder:
        return uns ? R(u1 % u2) : R(static_cast<unsigned long long>(s1 % s2));
    case BinaryOp::LeftShift:
        return R(u1 << (u2 & shMask));
    case BinaryOp::RightShift: // logical (shr) for unsigned, arithmetic (sar) for signed
        return uns ? R(u1 >> (u2 & shMask))
                   : R(static_cast<unsigned long long>(s1 >> (u2 & shMask)));
    case BinaryOp::Equal:
        return I(u1 == u2);
    case BinaryOp::NotEqual:
        return I(u1 != u2);
    case BinaryOp::LessThan:
        return uns ? I(u1 < u2) : I(s1 < s2);
    case BinaryOp::LessOrEqual:
        return uns ? I(u1 <= u2) : I(s1 <= s2);
    case BinaryOp::GreaterThan:
        return uns ? I(u1 > u2) : I(s1 > s2);
    case BinaryOp::GreaterThanOrEqual:
        return uns ? I(u1 >= u2) : I(s1 >= s2);
    default:
        break; // DivDouble / And / Or / Xor are codegen-only
    }
    return lhs; // unreachable for well-formed TACKY
}

// True when both operands are integer constants and the op is an integer divide
// or remainder by zero — folding that would trap at compile time on a path that
// might be unreachable, so it is left in place.
inline bool isIntDivByZero(const TackyBinary *b)
{
    if (b->op != BinaryOp::Divide && b->op != BinaryOp::Remainder)
        return false;
    const auto *d = std::get_if<TackyConstant>(&b->src2);
    return d && normalize(d->value, d->type) == 0;
}

// If `cond` is a compile-time constant, return whether it is zero — which decides
// a constant-condition jump. A variable condition returns nullopt (not foldable).
inline std::optional<bool> constIsZero(const TackyVal &cond)
{
    if (const auto *c = std::get_if<TackyConstant>(&cond))
        return normalize(c->value, c->type) == 0;
    if (const auto *f = std::get_if<TackyFloatingConstant>(&cond))
        return f->value == 0.0; // -0.0 counts as zero; NaN is nonzero
    return std::nullopt;
}

// Fold a width/representation conversion over a constant operand. `dstType` is the
// destination value's type. Integer<->integer conversions just re-key the source
// bits to the destination width; the int<->double ones go through the host's
// conversion, which matches the target SSE conversion for in-range values.
inline TackyVal evalConvert(TackyKind kind, const TackyVal &src, ConstantType dstType)
{
    switch (kind)
    {
    case TackyKind::SignExtend:
    case TackyKind::ZeroExtend:
    case TackyKind::Truncate:
    {
        // Canonicalize at the source type (sign/zero fill), then re-key to dst width.
        const auto &c = std::get<TackyConstant>(src);
        return TackyConstant(normalize(normalize(c.value, c.type), dstType), dstType);
    }
    case TackyKind::IntToDouble:
    {
        const auto &c = std::get<TackyConstant>(src);
        return TackyFloatingConstant(
            static_cast<double>(static_cast<long long>(normalize(c.value, c.type))),
            ConstantType::DOUBLE);
    }
    case TackyKind::UIntToDouble:
    {
        const auto &c = std::get<TackyConstant>(src);
        return TackyFloatingConstant(static_cast<double>(normalize(c.value, c.type)),
                                     ConstantType::DOUBLE);
    }
    case TackyKind::DoubleToInt:
    {
        const double d = std::get<TackyFloatingConstant>(src).value;
        return TackyConstant(
            normalize(static_cast<unsigned long long>(static_cast<long long>(d)), dstType),
            dstType);
    }
    case TackyKind::DoubleToUInt:
    {
        const double d = std::get<TackyFloatingConstant>(src).value;
        return TackyConstant(normalize(static_cast<unsigned long long>(d), dstType), dstType);
    }
    default:
        return src; // not a conversion (unreachable)
    }
}

// All seven conversion instructions share the same (src, dst) shape but no common
// base that exposes them, so this centralizes the per-kind cast: it returns the
// operands for a foldable conversion, or nullopt for anything else.
struct ConversionOperands
{
    const TackyVal *src;
    const TackyVal *dst;
};
inline std::optional<ConversionOperands> asConversion(TackyInstruction *i)
{
    switch (i->getKind())
    {
    case TackyKind::SignExtend:
        return ConversionOperands{&cast<TackySignExtend>(i)->src, &cast<TackySignExtend>(i)->dst};
    case TackyKind::ZeroExtend:
        return ConversionOperands{&cast<TackyZeroExtend>(i)->src, &cast<TackyZeroExtend>(i)->dst};
    case TackyKind::Truncate:
        return ConversionOperands{&cast<TackyTruncate>(i)->src, &cast<TackyTruncate>(i)->dst};
    case TackyKind::DoubleToInt:
        return ConversionOperands{&cast<TackyDoubleToInt>(i)->src, &cast<TackyDoubleToInt>(i)->dst};
    case TackyKind::DoubleToUInt:
        return ConversionOperands{&cast<TackyDoubleToUInt>(i)->src,
                                  &cast<TackyDoubleToUInt>(i)->dst};
    case TackyKind::IntToDouble:
        return ConversionOperands{&cast<TackyIntToDouble>(i)->src, &cast<TackyIntToDouble>(i)->dst};
    case TackyKind::UIntToDouble:
        return ConversionOperands{&cast<TackyUIntToDouble>(i)->src,
                                  &cast<TackyUIntToDouble>(i)->dst};
    default:
        return std::nullopt;
    }
}

inline void foldConstants(std::vector<std::unique_ptr<TackyInstruction>> &instructions,
                          bool &changed)
{
    std::vector<std::unique_ptr<TackyInstruction>> result;
    result.reserve(instructions.size());

    for (auto &instruction : instructions)
    {
        if (auto *u = dyn_cast<TackyUnary>(instruction.get()))
        {
            if (std::holds_alternative<TackyConstant>(u->src) ||
                std::holds_alternative<TackyFloatingConstant>(u->src))
            {
                TackyVal folded = evalUnary(u->op, u->src, typeOf(u->dst));
                result.push_back(std::make_unique<TackyCopy>(u->line, u->column, folded, u->dst));
                changed = true;
                continue;
            }
        }
        else if (auto *b = dyn_cast<TackyBinary>(instruction.get()))
        {
            const bool bothInt = std::holds_alternative<TackyConstant>(b->src1) &&
                                 std::holds_alternative<TackyConstant>(b->src2);
            const bool bothFloat = std::holds_alternative<TackyFloatingConstant>(b->src1) &&
                                   std::holds_alternative<TackyFloatingConstant>(b->src2);
            if ((bothInt || bothFloat) && !isIntDivByZero(b))
            {
                TackyVal folded = evalBinary(b->op, b->src1, b->src2, typeOf(b->dst));
                result.push_back(std::make_unique<TackyCopy>(b->line, b->column, folded, b->dst));
                changed = true;
                continue;
            }
        }
        else if (auto *jz = dyn_cast<TackyJumpIfZero>(instruction.get()))
        {
            if (auto zero = constIsZero(jz->condition))
            {
                changed = true;
                if (*zero) // condition zero -> branch always taken
                    result.push_back(
                        std::make_unique<TackyJump>(jz->line, jz->column, jz->identifier));
                // nonzero -> never taken: drop it (push nothing)
                continue;
            }
        }
        else if (auto *jnz = dyn_cast<TackyJumpIfNotZero>(instruction.get()))
        {
            if (auto zero = constIsZero(jnz->condition))
            {
                changed = true;
                if (!*zero) // condition nonzero -> branch always taken
                    result.push_back(
                        std::make_unique<TackyJump>(jnz->line, jnz->column, jnz->identifier));
                // zero -> never taken: drop it
                continue;
            }
        }
        else if (auto conv = asConversion(instruction.get()))
        {
            if (!std::holds_alternative<TackyVar>(*conv->src))
            {
                TackyVal folded =
                    evalConvert(instruction->getKind(), *conv->src, typeOf(*conv->dst));
                result.push_back(std::make_unique<TackyCopy>(instruction->line, instruction->column,
                                                             folded, *conv->dst));
                changed = true;
                continue;
            }
        }

        // not folded: keep the original instruction
        result.push_back(std::move(instruction));
    }

    instructions = std::move(result);
}

// Build the control-flow graph for a function body. The instruction list is first
// partitioned into basic blocks: a label opens a new block, a jump/branch/return
// closes the current one, and any leftover trailing instructions form a final
// block. The blocks are then laid out as [ENTRY, real blocks in program order,
// EXIT] and numbered by their index, which is also how edges refer to them.
// Finally the edges are wired from each block's terminator: ENTRY flows to the
// first real block (or EXIT if there is none); a jump goes to its target block; a
// conditional branch goes to both its target and the next block (fall-through); a
// return goes to EXIT; and a block with no terminator falls through to the next.
// Predecessor lists are kept as the mirror of successors.
inline CFG makeControlFlowGraph(std::vector<std::unique_ptr<TackyInstruction>> instructions)
{
    std::vector<BasicBlock> blocks;
    BasicBlock current;
    auto closeBlock = [&]()
    {
        if (!current.instructions.empty())
        {
            blocks.push_back(std::move(current));
            current = BasicBlock{};
        }
    };

    for (auto &instr : instructions)
    {
        const TackyKind k = instr->getKind();
        if (k == TackyKind::Label)
        {
            closeBlock();
            current.instructions.push_back(std::move(instr));
        }
        else if (k == TackyKind::Jump || k == TackyKind::JumpIfZero ||
                 k == TackyKind::JumpIfNotZero || k == TackyKind::Return)
        {
            current.instructions.push_back(std::move(instr));
            closeBlock();
        }
        else
        {
            current.instructions.push_back(std::move(instr));
        }
    }
    closeBlock();

    CFG cfg;
    cfg.blocks.reserve(blocks.size() + 2);
    cfg.blocks.push_back(BasicBlock{}); // ENTRY
    for (auto &b : blocks)
        cfg.blocks.push_back(std::move(b));
    cfg.blocks.push_back(BasicBlock{}); // EXIT
    for (int i = 0; i < static_cast<int>(cfg.blocks.size()); ++i)
        cfg.blocks[i].id = i;

    const int entry = cfg.entryId();
    const int exit = cfg.exitId();
    const int firstReal = 1;
    const int lastReal = exit - 1; // < firstReal when there are no real blocks

    std::unordered_map<std::string, int> labelBlock;
    for (int i = firstReal; i <= lastReal; ++i)
        if (const auto *lbl = dyn_cast<TackyLabel>(cfg.blocks[i].instructions.front().get()))
            labelBlock[lbl->identifier] = i;

    cfg.addEdge(entry, lastReal >= firstReal ? firstReal : exit);

    for (int i = firstReal; i <= lastReal; ++i)
    {
        const int fallthrough = (i == lastReal) ? exit : i + 1;
        TackyInstruction *last = cfg.blocks[i].instructions.back().get();

        if (const auto *j = dyn_cast<TackyJump>(last))
            cfg.addEdge(i, labelBlock.at(j->identifier));
        else if (const auto *jz = dyn_cast<TackyJumpIfZero>(last))
        {
            cfg.addEdge(i, labelBlock.at(jz->identifier));
            cfg.addEdge(i, fallthrough);
        }
        else if (const auto *jnz = dyn_cast<TackyJumpIfNotZero>(last))
        {
            cfg.addEdge(i, labelBlock.at(jnz->identifier));
            cfg.addEdge(i, fallthrough);
        }
        else if (isa<TackyReturn>(last))
            cfg.addEdge(i, exit);
        else
            cfg.addEdge(i, fallthrough);
    }

    return cfg;
}

// The book's "eliminate unreachable code", three cleanups over the CFG:
//   1. Unreachable blocks — a depth-first walk from ENTRY marks the reachable
//      blocks; each unmarked block is dead, so its edges are dropped (keeping the
//      survivors' adjacency accurate) and it is flagged removed for
//      cfgToInstructions and later passes to skip. ENTRY and EXIT are structural and
//      never removed: EXIT can legitimately be unreachable (a function that never
//      returns), and removing it would only have makeControlFlowGraph recreate it
//      next round, spinning the fixed-point loop forever.
//   2. Redundant jumps — a block's terminating jump whose every target is just the
//      next live block is pointless, since falling through reaches the same place.
//   3. Useless labels — a leading label on a block reached only from the previous
//      live block (never an explicit jump target) carries no information.
// Steps 2 and 3 only delete instructions, so they leave edges untouched; they run
// after step 1 and skip dead blocks. A back-edge (loop) keeps its label, because the
// jumping block is a predecessor other than the fall-through one.
inline void eliminateUnreachableCode(CFG &cfg, bool &changed)
{
    std::vector<bool> reachable(cfg.blocks.size(), false);
    std::vector<int> stack{cfg.entryId()};
    reachable[cfg.entryId()] = true;
    while (!stack.empty())
    {
        const int b = stack.back();
        stack.pop_back();
        for (const int s : cfg.blocks[b].successors)
            if (!reachable[s])
            {
                reachable[s] = true;
                stack.push_back(s);
            }
    }

    for (auto &block : cfg.blocks)
    {
        if (block.id == cfg.entryId() || block.id == cfg.exitId() || reachable[block.id])
            continue;
        // Copy the neighbor lists: removeEdge mutates them as it goes.
        for (const int s : std::vector<int>(block.successors))
            cfg.removeEdge(block.id, s);
        for (const int p : std::vector<int>(block.predecessors))
            cfg.removeEdge(p, block.id);
        block.removed = true;
        changed = true;
    }

    // The next/previous live block in program order: the fall-through target and the
    // default predecessor that steps 2 and 3 compare against.
    auto nextLive = [&](int i)
    {
        for (int j = i + 1; j < static_cast<int>(cfg.blocks.size()); ++j)
            if (!cfg.blocks[j].removed)
                return j;
        return cfg.exitId();
    };
    auto prevLive = [&](int i)
    {
        for (int j = i - 1; j >= 0; --j)
            if (!cfg.blocks[j].removed)
                return j;
        return cfg.entryId();
    };

    for (int i = 1; i < cfg.exitId(); ++i)
    {
        BasicBlock &b = cfg.blocks[i];
        if (b.removed || b.instructions.empty())
            continue;
        const TackyKind k = b.instructions.back()->getKind();
        if (k != TackyKind::Jump && k != TackyKind::JumpIfZero && k != TackyKind::JumpIfNotZero)
            continue;
        const int nxt = nextLive(i);
        bool allToNext = !b.successors.empty();
        for (const int s : b.successors)
            if (s != nxt)
                allToNext = false;
        if (allToNext)
        {
            b.instructions.pop_back();
            changed = true;
        }
    }

    for (int i = 1; i < cfg.exitId(); ++i)
    {
        BasicBlock &b = cfg.blocks[i];
        if (b.removed || b.instructions.empty())
            continue;
        if (b.instructions.front()->getKind() != TackyKind::Label)
            continue;
        const int dp = prevLive(i);
        bool allFromPrev = !b.predecessors.empty();
        for (const int p : b.predecessors)
            if (p != dp)
                allFromPrev = false;
        if (allFromPrev)
        {
            b.instructions.erase(b.instructions.begin());
            changed = true;
        }
    }
}

// A canonical string for a TackyVal, used to key copy facts in a set so the meet
// (intersection) and fixed-point comparison work on plain string keys. The kind
// prefix and the type keep an `int x` distinct from an `unsigned x` and a constant
// from a variable of the same spelling; a double is keyed by its exact bit pattern.
inline std::string tackyValKey(const TackyVal &v)
{
    if (const auto *var = std::get_if<TackyVar>(&v))
        return "v" + std::to_string(static_cast<int>(var->type)) + ":" + var->name;
    if (const auto *c = std::get_if<TackyConstant>(&v))
        return "c" + std::to_string(static_cast<int>(c->type)) + ":" + std::to_string(c->value);
    const auto &f = std::get<TackyFloatingConstant>(v);
    unsigned long long bits;
    std::memcpy(&bits, &f.value, sizeof(bits));
    return "f:" + std::to_string(bits);
}
inline std::string copyFactKey(const TackyVal &src, const TackyVal &dst)
{
    return tackyValKey(src) + "=>" + tackyValKey(dst);
}

// A reaching copy `dst = src`, keyed by copyFactKey in a CopySet. Two facts with
// the same key are identical, so set operations only ever compare keys.
struct CopyFact
{
    TackyVal src;
    TackyVal dst;
};
using CopySet = std::map<std::string, CopyFact>;

// The book's "copy propagation": a forward, all-paths reaching-copies analysis
// followed by a rewrite. A copy `dst = src` reaches a point when it still holds
// there (neither operand redefined since), so a use of `dst` may be replaced by
// `src`. The lattice value is a set of (src, dst) facts and the meet is set
// intersection (a copy must reach along every path) — so every block starts at the
// universal set U of all copies while ENTRY starts empty. The transfer over a block
// walks its instructions: a Copy kills facts mentioning its dst then gens the new
// fact, but only when both sides share a type so a propagation can never change
// signedness; any other instruction that defines a variable kills facts mentioning
// it; a call or a store additionally kills facts touching an aliased variable — one
// that is static or has had its address taken, since it can be clobbered through a
// pointer or by the callee. A worklist iterates OUT[b] = transfer(meet of preds) to
// a fixed point. A second walk then replaces each used operand by its reaching
// copy's source (never the address-of operand of a GetAddress) and drops any copy
// that the rewrite turns into a self-copy. The outer driver rebuilds the CFG and
// reruns until nothing changes.
inline void propagateCopies(CFG &cfg, bool &changed)
{
    const int entry = cfg.entryId();
    const int exit = cfg.exitId();

    // Aliased variables: static storage, or address taken anywhere in the function.
    // A store through any pointer, or any call, may touch one of these.
    std::unordered_set<std::string> addressTaken;
    for (auto &blk : cfg.blocks)
    {
        if (blk.removed)
            continue;
        for (auto &uptr : blk.instructions)
            if (auto *ga = dyn_cast<TackyGetAddress>(uptr.get()))
                if (const auto *v = std::get_if<TackyVar>(&ga->src))
                    addressTaken.insert(v->name);
    }
    auto isAliasedVal = [&](const TackyVal &v)
    {
        const auto *var = std::get_if<TackyVar>(&v);
        return var && (var->isStatic || addressTaken.count(var->name));
    };

    // gen-able: dst and src share a type and the copy is not `x = x`. Used both to
    // seed the universal set U and to decide what a Copy gens during transfer.
    auto genable = [](const TackyCopy *cp)
    {
        if (typeOf(cp->src) != typeOf(cp->dst))
            return false;
        const auto *sv = std::get_if<TackyVar>(&cp->src);
        const auto *dv = std::get_if<TackyVar>(&cp->dst);
        return !(sv && dv && sv->name == dv->name);
    };

    CopySet universe;
    for (auto &blk : cfg.blocks)
    {
        if (blk.removed)
            continue;
        for (auto &uptr : blk.instructions)
            if (auto *cp = dyn_cast<TackyCopy>(uptr.get()))
                if (genable(cp))
                    universe.insert_or_assign(copyFactKey(cp->src, cp->dst),
                                              CopyFact{cp->src, cp->dst});
    }

    // The destination variable an instruction defines, if it names one (a store
    // defines memory, not a variable; a copy-to-offset's dst is the named aggregate).
    auto defVarName = [](TackyInstruction *instr) -> std::optional<std::string>
    {
        auto asVar = [](const TackyVal &v) -> std::optional<std::string>
        {
            if (const auto *var = std::get_if<TackyVar>(&v))
                return var->name;
            return std::nullopt;
        };
        if (auto conv = asConversion(instr))
            return asVar(*conv->dst);
        switch (instr->getKind())
        {
        case TackyKind::Unary:
            return asVar(cast<TackyUnary>(instr)->dst);
        case TackyKind::Binary:
            return asVar(cast<TackyBinary>(instr)->dst);
        case TackyKind::Load:
            return asVar(cast<TackyLoad>(instr)->dst);
        case TackyKind::GetAddress:
            return asVar(cast<TackyGetAddress>(instr)->dst);
        default:
            return std::nullopt;
        }
    };

    auto killByName = [](CopySet &cur, const std::string &n)
    {
        for (auto it = cur.begin(); it != cur.end();)
        {
            const auto *sv = std::get_if<TackyVar>(&it->second.src);
            const auto *dv = std::get_if<TackyVar>(&it->second.dst);
            if ((sv && sv->name == n) || (dv && dv->name == n))
                it = cur.erase(it);
            else
                ++it;
        }
    };
    auto killAliased = [&](CopySet &cur)
    {
        for (auto it = cur.begin(); it != cur.end();)
        {
            if (isAliasedVal(it->second.src) || isAliasedVal(it->second.dst))
                it = cur.erase(it);
            else
                ++it;
        }
    };

    auto transfer = [&](TackyInstruction *instr, CopySet &cur)
    {
        switch (instr->getKind())
        {
        case TackyKind::Copy:
        {
            auto *cp = cast<TackyCopy>(instr);
            // `dst = src` when `src = dst` already holds is redundant: it changes
            // nothing, so leave the set (and the existing fact) untouched.
            if (cur.count(copyFactKey(cp->dst, cp->src)))
                return;
            killByName(cur, std::get<TackyVar>(cp->dst).name);
            if (genable(cp))
                cur.insert_or_assign(copyFactKey(cp->src, cp->dst), CopyFact{cp->src, cp->dst});
            return;
        }
        case TackyKind::FunctionCall:
        {
            auto *fc = cast<TackyFunctionCall>(instr);
            killAliased(cur);
            if (const auto *dv = std::get_if<TackyVar>(&fc->dst))
                killByName(cur, dv->name);
            return;
        }
        case TackyKind::Store:
            killAliased(cur);
            return;
        case TackyKind::CopyToOffset:
            killByName(cur, cast<TackyCopyToOffset>(instr)->dst);
            return;
        default:
            if (auto def = defVarName(instr))
                killByName(cur, *def);
            return;
        }
    };

    // Meet: the reaching copies at the start of `b` are those reaching it along every
    // live predecessor (intersection). ENTRY's empty OUT forces the first block to
    // start empty; the U-initialized OUT of an as-yet-unprocessed back-edge block
    // contributes everything, so a loop converges as that block's OUT shrinks.
    auto meet = [&](int b, const std::vector<CopySet> &out) -> CopySet
    {
        std::vector<int> preds;
        for (const int p : cfg.blocks[b].predecessors)
            if (!cfg.blocks[p].removed)
                preds.push_back(p);
        if (preds.empty())
            return CopySet{};
        CopySet result = out[preds[0]];
        for (size_t i = 1; i < preds.size(); ++i)
        {
            const CopySet &o = out[preds[i]];
            for (auto it = result.begin(); it != result.end();)
            {
                if (o.count(it->first))
                    ++it;
                else
                    it = result.erase(it);
            }
        }
        return result;
    };

    auto sameKeys = [](const CopySet &a, const CopySet &b)
    {
        if (a.size() != b.size())
            return false;
        for (auto ia = a.begin(), ib = b.begin(); ia != a.end(); ++ia, ++ib)
            if (ia->first != ib->first)
                return false;
        return true;
    };

    const int n = static_cast<int>(cfg.blocks.size());
    std::vector<CopySet> out(n);
    for (int i = 0; i < n; ++i)
        if (i != entry && !cfg.blocks[i].removed)
            out[i] = universe;

    std::vector<int> worklist;
    std::vector<char> queued(n, false);
    for (int i = 0; i < n; ++i)
        if (i != entry && i != exit && !cfg.blocks[i].removed)
        {
            worklist.push_back(i);
            queued[i] = true;
        }
    while (!worklist.empty())
    {
        const int b = worklist.back();
        worklist.pop_back();
        queued[b] = false;

        CopySet newOut = meet(b, out);
        for (auto &uptr : cfg.blocks[b].instructions)
            transfer(uptr.get(), newOut);
        if (sameKeys(newOut, out[b]))
            continue;
        out[b] = std::move(newOut);
        for (const int s : cfg.blocks[b].successors)
            if (s != entry && s != exit && !cfg.blocks[s].removed && !queued[s])
            {
                worklist.push_back(s);
                queued[s] = true;
            }
    }

    // Rewrite: replay the transfer through each block from its reaching-copies IN
    // set, replacing every used operand whose variable has a reaching copy by that
    // copy's source. A GetAddress operand is left alone (its address is what matters),
    // and a copy that becomes `x = x` after rewriting is dropped.
    auto replaceUse = [&](TackyVal &operand, const CopySet &cur)
    {
        const auto *var = std::get_if<TackyVar>(&operand);
        if (!var)
            return;
        for (const auto &kv : cur)
        {
            const auto *dv = std::get_if<TackyVar>(&kv.second.dst);
            if (dv && dv->name == var->name)
            {
                operand = kv.second.src;
                changed = true;
                return;
            }
        }
    };
    auto rewriteUses = [&](TackyInstruction *instr, const CopySet &cur)
    {
        switch (instr->getKind())
        {
        case TackyKind::Copy:
            replaceUse(cast<TackyCopy>(instr)->src, cur);
            break;
        case TackyKind::Unary:
            replaceUse(cast<TackyUnary>(instr)->src, cur);
            break;
        case TackyKind::Binary:
        {
            auto *b = cast<TackyBinary>(instr);
            replaceUse(b->src1, cur);
            replaceUse(b->src2, cur);
            break;
        }
        case TackyKind::SignExtend:
            replaceUse(cast<TackySignExtend>(instr)->src, cur);
            break;
        case TackyKind::ZeroExtend:
            replaceUse(cast<TackyZeroExtend>(instr)->src, cur);
            break;
        case TackyKind::Truncate:
            replaceUse(cast<TackyTruncate>(instr)->src, cur);
            break;
        case TackyKind::DoubleToInt:
            replaceUse(cast<TackyDoubleToInt>(instr)->src, cur);
            break;
        case TackyKind::DoubleToUInt:
            replaceUse(cast<TackyDoubleToUInt>(instr)->src, cur);
            break;
        case TackyKind::IntToDouble:
            replaceUse(cast<TackyIntToDouble>(instr)->src, cur);
            break;
        case TackyKind::UIntToDouble:
            replaceUse(cast<TackyUIntToDouble>(instr)->src, cur);
            break;
        case TackyKind::Return:
        {
            auto *r = cast<TackyReturn>(instr);
            if (r->val)
                replaceUse(*r->val, cur);
            break;
        }
        case TackyKind::JumpIfZero:
            replaceUse(cast<TackyJumpIfZero>(instr)->condition, cur);
            break;
        case TackyKind::JumpIfNotZero:
            replaceUse(cast<TackyJumpIfNotZero>(instr)->condition, cur);
            break;
        case TackyKind::FunctionCall:
            for (auto &a : cast<TackyFunctionCall>(instr)->args)
                replaceUse(a, cur);
            break;
        case TackyKind::Load:
            replaceUse(cast<TackyLoad>(instr)->srcPtr, cur);
            break;
        case TackyKind::Store:
        {
            auto *s = cast<TackyStore>(instr);
            replaceUse(s->src, cur);
            replaceUse(s->dstPtr, cur);
            break;
        }
        case TackyKind::CopyToOffset:
            replaceUse(cast<TackyCopyToOffset>(instr)->src, cur);
            break;
        default:
            break; // GetAddress, Jump, Label: nothing to rewrite
        }
    };

    auto isSelfCopy = [](TackyInstruction *instr)
    {
        auto *cp = dyn_cast<TackyCopy>(instr);
        if (!cp)
            return false;
        const auto *sv = std::get_if<TackyVar>(&cp->src);
        const auto *dv = std::get_if<TackyVar>(&cp->dst);
        return sv && dv && sv->name == dv->name;
    };

    for (int i = entry + 1; i < exit; ++i)
    {
        BasicBlock &blk = cfg.blocks[i];
        if (blk.removed || blk.instructions.empty())
            continue;
        CopySet cur = meet(i, out);
        std::vector<std::unique_ptr<TackyInstruction>> kept;
        kept.reserve(blk.instructions.size());
        for (auto &uptr : blk.instructions)
        {
            rewriteUses(uptr.get(), cur);
            transfer(uptr.get(), cur);
            if (isSelfCopy(uptr.get()))
            {
                changed = true;
                continue;
            }
            kept.push_back(std::move(uptr));
        }
        blk.instructions = std::move(kept);
    }
}

// The book's "dead store elimination": the backward, any-path twin of copy
// propagation. A variable is live at a point if some path from there reads it
// before redefining it; the lattice value is a set of live variable names and the
// meet is union (live on any successor path), so every set starts empty and the
// boundary IN[EXIT] is the static variables — they outlive the call. The backward
// transfer over a block kills each instruction's destination then gens its used
// operands; a Load or a FunctionCall additionally gens every aliased variable
// (static or address-taken), because either may read one through a pointer that no
// name resolves to. A worklist iterates IN[b] = transfer(union of successors) to a
// fixed point. Then a backward walk drops every dead store: a pure, scalar-result
// instruction (copy/unary/binary/conversion/get-address/load) whose destination is
// not live afterward. Calls and stores are never dropped (side effects), and a
// copy-to-offset is never dropped (it only partially defines its aggregate). The
// outer driver reruns until nothing changes.
inline void eliminateDeadStores(CFG &cfg, bool &changed)
{
    const int entry = cfg.entryId();
    const int exit = cfg.exitId();

    // Visit every value operand of an instruction (uses and defs alike) — used only
    // to discover which static variables appear in this function.
    auto eachVal = [](TackyInstruction *instr, auto &&fn)
    {
        switch (instr->getKind())
        {
        case TackyKind::Return:
        {
            auto *r = cast<TackyReturn>(instr);
            if (r->val)
                fn(*r->val);
            break;
        }
        case TackyKind::Unary:
        {
            auto *u = cast<TackyUnary>(instr);
            fn(u->src);
            fn(u->dst);
            break;
        }
        case TackyKind::Binary:
        {
            auto *b = cast<TackyBinary>(instr);
            fn(b->src1);
            fn(b->src2);
            fn(b->dst);
            break;
        }
        case TackyKind::Copy:
        {
            auto *c = cast<TackyCopy>(instr);
            fn(c->src);
            fn(c->dst);
            break;
        }
        case TackyKind::JumpIfZero:
            fn(cast<TackyJumpIfZero>(instr)->condition);
            break;
        case TackyKind::JumpIfNotZero:
            fn(cast<TackyJumpIfNotZero>(instr)->condition);
            break;
        case TackyKind::FunctionCall:
        {
            auto *f = cast<TackyFunctionCall>(instr);
            for (auto &a : f->args)
                fn(a);
            fn(f->dst);
            break;
        }
        case TackyKind::GetAddress:
        {
            auto *g = cast<TackyGetAddress>(instr);
            fn(g->src);
            fn(g->dst);
            break;
        }
        case TackyKind::Load:
        {
            auto *l = cast<TackyLoad>(instr);
            fn(l->srcPtr);
            fn(l->dst);
            break;
        }
        case TackyKind::Store:
        {
            auto *s = cast<TackyStore>(instr);
            fn(s->src);
            fn(s->dstPtr);
            break;
        }
        case TackyKind::CopyToOffset:
            fn(cast<TackyCopyToOffset>(instr)->src);
            break;
        default:
            if (auto conv = asConversion(instr))
            {
                fn(*conv->src);
                fn(*conv->dst);
            }
            break;
        }
    };

    // Static variables (the exit boundary) and aliased variables (static or
    // address-taken — what a call or load may touch indirectly).
    std::unordered_set<std::string> staticVars;
    std::unordered_set<std::string> aliased;
    for (auto &blk : cfg.blocks)
    {
        if (blk.removed)
            continue;
        for (auto &uptr : blk.instructions)
        {
            if (auto *ga = dyn_cast<TackyGetAddress>(uptr.get()))
                if (const auto *v = std::get_if<TackyVar>(&ga->src))
                    aliased.insert(v->name);
            eachVal(uptr.get(),
                    [&](const TackyVal &v)
                    {
                        if (const auto *var = std::get_if<TackyVar>(&v); var && var->isStatic)
                        {
                            staticVars.insert(var->name);
                            aliased.insert(var->name);
                        }
                    });
        }
    }

    auto useVar = [](const TackyVal &v, std::unordered_set<std::string> &live)
    {
        if (const auto *var = std::get_if<TackyVar>(&v))
            live.insert(var->name);
    };
    auto killVar = [](const TackyVal &v, std::unordered_set<std::string> &live)
    {
        if (const auto *var = std::get_if<TackyVar>(&v))
            live.erase(var->name);
    };
    auto addAliased = [&](std::unordered_set<std::string> &live)
    {
        for (const auto &n : aliased)
            live.insert(n);
    };

    // Backward transfer: live-before = (live-after - def) ∪ uses, with a load or a
    // call also re-livening every aliased variable.
    auto transfer = [&](TackyInstruction *instr, std::unordered_set<std::string> &live)
    {
        switch (instr->getKind())
        {
        case TackyKind::Copy:
        {
            auto *c = cast<TackyCopy>(instr);
            killVar(c->dst, live);
            useVar(c->src, live);
            break;
        }
        case TackyKind::Unary:
        {
            auto *u = cast<TackyUnary>(instr);
            killVar(u->dst, live);
            useVar(u->src, live);
            break;
        }
        case TackyKind::Binary:
        {
            auto *b = cast<TackyBinary>(instr);
            killVar(b->dst, live);
            useVar(b->src1, live);
            useVar(b->src2, live);
            break;
        }
        case TackyKind::Load:
        {
            auto *l = cast<TackyLoad>(instr);
            killVar(l->dst, live);
            useVar(l->srcPtr, live);
            addAliased(live); // a load may read any aliased variable through its pointer
            break;
        }
        case TackyKind::Store:
        {
            auto *s = cast<TackyStore>(instr);
            useVar(s->src, live);
            useVar(s->dstPtr, live);
            break;
        }
        case TackyKind::GetAddress:
            killVar(cast<TackyGetAddress>(instr)->dst, live); // src is addressed, not read
            break;
        case TackyKind::FunctionCall:
        {
            auto *f = cast<TackyFunctionCall>(instr);
            killVar(f->dst, live);
            for (auto &a : f->args)
                useVar(a, live);
            addAliased(live); // the callee may read any aliased variable
            break;
        }
        case TackyKind::CopyToOffset:
            useVar(cast<TackyCopyToOffset>(instr)->src, live); // partial def: no kill
            break;
        case TackyKind::Return:
        {
            auto *r = cast<TackyReturn>(instr);
            if (r->val)
                useVar(*r->val, live);
            break;
        }
        case TackyKind::JumpIfZero:
            useVar(cast<TackyJumpIfZero>(instr)->condition, live);
            break;
        case TackyKind::JumpIfNotZero:
            useVar(cast<TackyJumpIfNotZero>(instr)->condition, live);
            break;
        case TackyKind::Jump:
        case TackyKind::Label:
            break;
        default:
            if (auto conv = asConversion(instr))
            {
                killVar(*conv->dst, live);
                useVar(*conv->src, live);
            }
            break;
        }
    };

    // The OUT (live-after) set at the end of a block: the union of its live
    // successors' IN sets. EXIT's IN is the static boundary, so a return block picks
    // it up here.
    auto liveOut = [&](int b, const std::vector<std::unordered_set<std::string>> &in)
    {
        std::unordered_set<std::string> o;
        for (const int s : cfg.blocks[b].successors)
            if (!cfg.blocks[s].removed)
                for (const auto &nm : in[s])
                    o.insert(nm);
        return o;
    };

    const int n = static_cast<int>(cfg.blocks.size());
    std::vector<std::unordered_set<std::string>> in(n);
    in[exit] = staticVars; // boundary: statics are live on function exit

    std::vector<int> worklist;
    std::vector<char> queued(n, false);
    for (int i = 0; i < n; ++i)
        if (i != entry && i != exit && !cfg.blocks[i].removed)
        {
            worklist.push_back(i);
            queued[i] = true;
        }
    while (!worklist.empty())
    {
        const int b = worklist.back();
        worklist.pop_back();
        queued[b] = false;

        std::unordered_set<std::string> live = liveOut(b, in);
        auto &instrs = cfg.blocks[b].instructions;
        for (auto it = instrs.rbegin(); it != instrs.rend(); ++it)
            transfer(it->get(), live);
        if (live == in[b])
            continue;
        in[b] = std::move(live);
        for (const int p : cfg.blocks[b].predecessors)
            if (p != entry && p != exit && !cfg.blocks[p].removed && !queued[p])
            {
                worklist.push_back(p);
                queued[p] = true;
            }
    }

    // A dead store is a pure, scalar-result instruction whose destination is not
    // live afterward. This returns that scalar destination for the removable kinds
    // (and nullptr for everything else — calls, stores, copy-to-offset, aggregates),
    // so the removal test is simply "destScalar present and not live".
    auto destScalar = [](TackyInstruction *instr) -> const TackyVar *
    {
        const TackyVal *d = nullptr;
        switch (instr->getKind())
        {
        case TackyKind::Copy:
            d = &cast<TackyCopy>(instr)->dst;
            break;
        case TackyKind::Unary:
            d = &cast<TackyUnary>(instr)->dst;
            break;
        case TackyKind::Binary:
            d = &cast<TackyBinary>(instr)->dst;
            break;
        case TackyKind::GetAddress:
            d = &cast<TackyGetAddress>(instr)->dst;
            break;
        case TackyKind::Load:
            d = &cast<TackyLoad>(instr)->dst;
            break;
        default:
            if (auto conv = asConversion(instr))
                d = conv->dst;
            break;
        }
        if (!d)
            return nullptr;
        const auto *var = std::get_if<TackyVar>(d);
        return (var && var->objSize == 0) ? var : nullptr; // scalars only
    };

    for (int i = entry + 1; i < exit; ++i)
    {
        BasicBlock &blk = cfg.blocks[i];
        if (blk.removed || blk.instructions.empty())
            continue;
        std::unordered_set<std::string> live = liveOut(i, in);
        std::vector<std::unique_ptr<TackyInstruction>> keptReversed;
        keptReversed.reserve(blk.instructions.size());
        for (auto it = blk.instructions.rbegin(); it != blk.instructions.rend(); ++it)
        {
            TackyInstruction *instr = it->get();
            if (const TackyVar *dv = destScalar(instr); dv && !live.count(dv->name))
            {
                changed = true; // dead: drop it, and skip its transfer so its uses stay dead
                continue;
            }
            transfer(instr, live);
            keptReversed.push_back(std::move(*it));
        }
        std::vector<std::unique_ptr<TackyInstruction>> kept;
        kept.reserve(keptReversed.size());
        for (auto it = keptReversed.rbegin(); it != keptReversed.rend(); ++it)
            kept.push_back(std::move(*it));
        blk.instructions = std::move(kept);
    }
}

inline std::vector<std::unique_ptr<TackyInstruction>> cfgToInstructions(CFG &cfg)
{
    // Concatenate every live block's instructions in CFG order (ENTRY/EXIT are
    // empty); blocks a pass flagged removed are skipped.
    std::vector<std::unique_ptr<TackyInstruction>> instructions;
    for (auto &block : cfg.blocks)
    {
        if (block.removed)
            continue;
        for (auto &instr : block.instructions)
            instructions.push_back(std::move(instr));
    }
    return instructions;
}

// ---------------------------------------------------------------------------
// Driver: run the enabled passes over one function until a full round makes no
// change (a fixed point). Constant folding runs first, on the raw instruction
// list, so the CFG the later passes build already reflects the folded constants.
// ---------------------------------------------------------------------------

inline void optimizeFunction(TackyFunction &fn, const OptimizationFlags &flags)
{
    // An empty body (e.g. a forward declaration's stub) has nothing to optimize.
    if (fn.instructions.empty())
        return;

    bool changed = true;
    while (changed)
    {
        changed = false;

        if (flags.foldConstants)
            foldConstants(fn.instructions, changed);

        if (flags.eliminateUnreachableCode || flags.propagateCopies || flags.eliminateDeadStores)
        {
            CFG cfg = makeControlFlowGraph(std::move(fn.instructions));
            if (flags.eliminateUnreachableCode)
                eliminateUnreachableCode(cfg, changed);
            if (flags.propagateCopies)
                propagateCopies(cfg, changed);
            if (flags.eliminateDeadStores)
                eliminateDeadStores(cfg, changed);
            fn.instructions = cfgToInstructions(cfg);
        }
    }
}

inline void optimize(TackyProgram &prog, const OptimizationFlags &flags)
{
    if (!flags.any())
        return;
    for (auto &node : prog.nodes)
    {
        if (auto *fn = dynamic_cast<TackyFunction *>(node.get()))
            optimizeFunction(*fn, flags);
    }
}

// ---------------------------------------------------------------------------
// Verifier — label-resolution check only.
// Throws std::runtime_error if a jump targets a label not defined in the
// same function. All other invariants are intentionally left out to avoid
// false-positives on valid IR.
// ---------------------------------------------------------------------------

inline void verify(const TackyProgram &prog)
{
    for (const auto &node : prog.nodes)
    {
        const auto *fn = dynamic_cast<const TackyFunction *>(node.get());
        if (!fn)
            continue;

        // First pass: collect every label defined in this function.
        std::unordered_set<std::string> definedLabels;
        for (const auto &instr : fn->instructions)
        {
            if (const auto *lbl = dyn_cast<TackyLabel>(instr.get()))
                definedLabels.insert(lbl->identifier);
        }

        // Second pass: every jump target must be in definedLabels.
        for (const auto &instr : fn->instructions)
        {
            const std::string *target = nullptr;

            if (const auto *j = dyn_cast<TackyJump>(instr.get()))
                target = &j->identifier;
            else if (const auto *jz = dyn_cast<TackyJumpIfZero>(instr.get()))
                target = &jz->identifier;
            else if (const auto *jnz = dyn_cast<TackyJumpIfNotZero>(instr.get()))
                target = &jnz->identifier;

            if (target && definedLabels.find(*target) == definedLabels.end())
                throw std::runtime_error("TACKY verify: jump to undefined label '" + *target +
                                         "' in function " + fn->name);
        }
    }
}

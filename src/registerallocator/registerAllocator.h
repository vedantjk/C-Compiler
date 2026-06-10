#pragma once
#include "../cfg/CFG.h"
#include "../codegen/ast/ASTNodes/codegenProgram.h"
#include "../codegen/ast/TopLevelNodes/codegenFunction.h"
#include "../codegen/instructions/instructions.h"
#include "../codegen/instructions/operand.h"
#include "../support/RTTI.h"

#include <initializer_list>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct Node
{
    std::unique_ptr<Operand> id;
    std::vector<std::weak_ptr<Node>> neighbours;
    double spillCost = 0.0;
    int color = -1;
    bool pruned = false;
    Node(std::unique_ptr<Operand> id_) : id(std::move(id_)) {}
};

// Classify assembly instructions for the shared CFG builder (cfg/CFG.h): which instructions
// open a block (labels) or end one (jumps, branches, returns), and a terminator's target.
struct AsmCFGClassify
{
    bool isLabel(const Instruction &i) const { return i.getKind() == InstrKind::Label; }
    std::string labelOf(const Instruction &i) const { return cast<Label>(&i)->identifier; }
    cfg::TermKind termKind(const Instruction &i) const
    {
        switch (i.getKind())
        {
        case InstrKind::JumpInstruction:
            return cfg::TermKind::Jump;
        case InstrKind::JumpCCInstruction:
            return cfg::TermKind::Branch;
        case InstrKind::ReturnInstruction:
            return cfg::TermKind::Return;
        default:
            return cfg::TermKind::None;
        }
    }
    std::string targetOf(const Instruction &i) const
    {
        switch (i.getKind())
        {
        case InstrKind::JumpInstruction:
            return cast<JumpInstruction>(&i)->identifier;
        case InstrKind::JumpCCInstruction:
            return cast<JumpCCInstruction>(&i)->identifier;
        default:
            return {};
        }
    }
};

class RegisterAllocator
{
    std::vector<std::shared_ptr<Node>> geNodes;
    std::vector<std::shared_ptr<Node>> sseNodes;
    std::size_t geBaseCount = 0;
    std::size_t sseBaseCount = 0;

    // Build a base graph: one node per allocatable hardware register, fully connected.
    // Every register interferes with every other (no two physical registers can share a
    // color), so the hardware registers form a clique. Their spill cost is infinite so the
    // spill-metric step never picks a hardware register. Pseudoregister nodes and the
    // liveness-derived edges are layered on top of this per function.
    static void buildBaseGraph(std::initializer_list<RegisterName> pool,
                               std::vector<std::shared_ptr<Node>> &nodes)
    {
        nodes.reserve(pool.size());
        for (RegisterName r : pool)
        {
            nodes.push_back(std::make_shared<Node>(std::make_unique<Register>(r, 8)));
            nodes.back()->spillCost = std::numeric_limits<double>::infinity();
        }

        const std::size_t n = nodes.size();
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = i + 1; j < n; ++j)
            {
                nodes[i]->neighbours.push_back(nodes[j]);
                nodes[j]->neighbours.push_back(nodes[i]);
            }
    }

    // Allocatable GP pool: 12 registers, excluding R10/R11 (fixup scratch) and SP/BP.
    void createGEBaseGraph()
    {
        buildBaseGraph({RegisterName::AX, RegisterName::BX, RegisterName::CX, RegisterName::DX,
                        RegisterName::DI, RegisterName::SI, RegisterName::R8, RegisterName::R9,
                        RegisterName::R12, RegisterName::R13, RegisterName::R14, RegisterName::R15},
                       geNodes);
    }

    // Allocatable SSE pool: XMM0-XMM13, excluding XMM14/XMM15 (fixup scratch).
    void createSSEBaseGraph()
    {
        buildBaseGraph({RegisterName::XMM0, RegisterName::XMM1, RegisterName::XMM2,
                        RegisterName::XMM3, RegisterName::XMM4, RegisterName::XMM5,
                        RegisterName::XMM6, RegisterName::XMM7, RegisterName::XMM8,
                        RegisterName::XMM9, RegisterName::XMM10, RegisterName::XMM11,
                        RegisterName::XMM12, RegisterName::XMM13},
                       sseNodes);
    }

    // Start the next function from the persistent base: drop the previous function's pseudo
    // nodes, trim each base node's neighbours back to its clique prefix (the symmetric
    // edges to those pseudos are gone), and clear the per-function color/pruned state. The
    // clique itself is never rebuilt.
    void clearPseudos()
    {
        geNodes.resize(geBaseCount);
        sseNodes.resize(sseBaseCount);
        const std::size_t geClique = geBaseCount ? geBaseCount - 1 : 0;
        const std::size_t sseClique = sseBaseCount ? sseBaseCount - 1 : 0;
        for (auto &nd : geNodes)
        {
            nd->neighbours.resize(geClique);
            nd->color = -1;
            nd->pruned = false;
        }
        for (auto &nd : sseNodes)
        {
            nd->neighbours.resize(sseClique);
            nd->color = -1;
            nd->pruned = false;
        }
    }

    // Add one node per distinct allocation candidate in `fn`: a PseudoRegister that is
    // neither static (those live in a RIP-relative Data slot) nor aliased nor already
    // added. A pseudo is aliased when its address is taken, which surfaces as the source
    // of a Lea, so those are gathered first and excluded — they must stay in memory.
    // Doubles route to the SSE graph and every other width to the GP graph; aggregates are
    // PseudoMem, a different operand kind, so the PseudoRegister cast already skips them.
    void addFunctionPseudos(codegenFunction &fn)
    {
        clearPseudos();

        std::unordered_set<std::string> aliased;
        for (auto &instr : fn.instructions)
            if (auto *lea = dynamic_cast<LeaInstruction *>(instr.get()))
                if (auto *p = dynamic_cast<PseudoRegister *>(lea->src.get()))
                    aliased.insert(p->name);

        std::unordered_set<std::string> seen;
        auto consider = [&](std::unique_ptr<Operand> &slot)
        {
            auto *p = dynamic_cast<PseudoRegister *>(slot.get());
            if (!p || p->isStatic || aliased.count(p->name) || !seen.insert(p->name).second)
                return;
            auto &nodes = (p->type == AssemblyType::DOUBLE) ? sseNodes : geNodes;
            nodes.push_back(
                std::make_shared<Node>(std::make_unique<PseudoRegister>(p->name, p->type)));
        };
        for (auto &instr : fn.instructions)
            forEachSlot(*instr, consider);
    }

    // A set of live registers, keyed so hardware registers and pseudoregisters share one
    // set: "r:<RegisterName>" for a hardware register, "p:<name>" for a pseudo.
    using RegSet = std::unordered_set<std::string>;

    static std::string regKeyName(RegisterName r)
    {
        return "r:" + std::to_string(static_cast<int>(r));
    }
    static std::string keyOf(const Operand &op)
    {
        if (auto *r = dynamic_cast<const Register *>(&op))
            return regKeyName(r->name);
        if (auto *p = dynamic_cast<const PseudoRegister *>(&op))
            return "p:" + p->name;
        return {};
    }
    static bool isPseudo(const Operand &op) { return dynamic_cast<const PseudoRegister *>(&op); }
    static bool isSSE(const Operand &op)
    {
        if (auto *r = dynamic_cast<const Register *>(&op))
            return isXmm(r->name);
        if (auto *p = dynamic_cast<const PseudoRegister *>(&op))
            return p->type == AssemblyType::DOUBLE;
        return false;
    }

    // The registers an operand reads in a use position: a register/pseudo reads itself, a
    // memory operand reads its base; immediates and data read nothing.
    static void addUse(const Operand &op, RegSet &uses)
    {
        if (auto *m = dynamic_cast<const Memory *>(&op))
            uses.insert(regKeyName(m->reg));
        else if (const std::string k = keyOf(op); !k.empty())
            uses.insert(k);
    }

    // The SysV caller-saved registers a call clobbers (GP plus every XMM).
    static const std::vector<RegisterName> &callerSaved()
    {
        static const std::vector<RegisterName> regs = {
            RegisterName::AX,    RegisterName::CX,    RegisterName::DX,    RegisterName::SI,
            RegisterName::DI,    RegisterName::R8,    RegisterName::R9,    RegisterName::R10,
            RegisterName::R11,   RegisterName::XMM0,  RegisterName::XMM1,  RegisterName::XMM2,
            RegisterName::XMM3,  RegisterName::XMM4,  RegisterName::XMM5,  RegisterName::XMM6,
            RegisterName::XMM7,  RegisterName::XMM8,  RegisterName::XMM9,  RegisterName::XMM10,
            RegisterName::XMM11, RegisterName::XMM12, RegisterName::XMM13, RegisterName::XMM14,
            RegisterName::XMM15};
        return regs;
    }

    // The registers an instruction reads (uses) and writes (defs), including the effects
    // not visible in its operands: a call reads its argument registers and clobbers all
    // caller-saved ones, a return reads the return register, idiv/div/cdq touch AX/DX, and
    // a variable-count shift is lowered through CX. A read-modify-write operand (a unary or
    // a binary accumulator) is both a use and a def; a memory destination writes memory,
    // not a register, but still reads its base.
    static void useDef(Instruction &instr, RegSet &uses, RegSet &defs)
    {
        auto use = [&](const Operand &op) { addUse(op, uses); };
        auto def = [&](const Operand &op)
        {
            if (auto *m = dynamic_cast<const Memory *>(&op))
                uses.insert(regKeyName(m->reg));
            else if (const std::string k = keyOf(op); !k.empty())
                defs.insert(k);
        };

        switch (instr.getKind())
        {
        case InstrKind::MoveInstruction:
        {
            auto *m = cast<MoveInstruction>(&instr);
            use(*m->src);
            def(*m->dst);
            break;
        }
        case InstrKind::MoveSXInstruction:
        {
            auto *m = cast<MoveSXInstruction>(&instr);
            use(*m->src);
            def(*m->dst);
            break;
        }
        case InstrKind::MoveZeroExtendInstruction:
        {
            auto *m = cast<MoveZeroExtendInstruction>(&instr);
            use(*m->src);
            def(*m->dst);
            break;
        }
        case InstrKind::LeaInstruction:
        {
            auto *m = cast<LeaInstruction>(&instr);
            use(*m->src);
            def(*m->dst);
            break;
        }
        case InstrKind::CVTSI2SD:
        {
            auto *m = cast<CVTSI2SD>(&instr);
            use(*m->src);
            def(*m->dst);
            break;
        }
        case InstrKind::CVTTSD2SI:
        {
            auto *m = cast<CVTTSD2SI>(&instr);
            use(*m->src);
            def(*m->dst);
            break;
        }
        case InstrKind::UnaryInstruction:
        {
            auto *u = cast<UnaryInstruction>(&instr);
            use(*u->operand);
            def(*u->operand);
            break;
        }
        case InstrKind::BinaryInstruction:
        {
            auto *b = cast<BinaryInstruction>(&instr);
            use(*b->src);
            use(*b->dst);
            def(*b->dst);
            if (b->op == BinaryOp::LeftShift || b->op == BinaryOp::RightShift)
                defs.insert(regKeyName(RegisterName::CX));
            break;
        }
        case InstrKind::CmpInstruction:
        {
            auto *c = cast<CmpInstruction>(&instr);
            use(*c->a);
            use(*c->b);
            break;
        }
        case InstrKind::SetCCInstruction:
            def(*cast<SetCCInstruction>(&instr)->a);
            break;
        case InstrKind::PushInstruction:
            use(*cast<PushInstruction>(&instr)->a);
            break;
        case InstrKind::PopInstruction:
            def(*cast<PopInstruction>(&instr)->reg);
            break;
        case InstrKind::IDivInstruction:
        case InstrKind::DivInstruction:
        {
            const Operand &op = instr.getKind() == InstrKind::IDivInstruction
                                    ? *cast<IDivInstruction>(&instr)->operand
                                    : *cast<DivInstruction>(&instr)->operand;
            use(op);
            uses.insert(regKeyName(RegisterName::AX));
            uses.insert(regKeyName(RegisterName::DX));
            defs.insert(regKeyName(RegisterName::AX));
            defs.insert(regKeyName(RegisterName::DX));
            break;
        }
        case InstrKind::CdqInstruction:
            uses.insert(regKeyName(RegisterName::AX));
            defs.insert(regKeyName(RegisterName::DX));
            break;
        case InstrKind::CallInstruction:
        {
            auto *c = cast<CallInstruction>(&instr);
            for (RegisterName r : c->argRegs)
                uses.insert(regKeyName(r));
            for (RegisterName r : callerSaved())
                defs.insert(regKeyName(r));
            break;
        }
        case InstrKind::ReturnInstruction:
            for (RegisterName r : cast<ReturnInstruction>(&instr)->returnRegs)
                uses.insert(regKeyName(r));
            break;
        case InstrKind::JumpInstruction:
        case InstrKind::JumpCCInstruction:
        case InstrKind::Label:
            break;
        }
    }

    // Backward, may-analysis liveness over the assembly CFG: LIVE_OUT[b] is the union of
    // its successors' LIVE_IN, and the per-block transfer walks instructions in reverse
    // applying (live - defs) ∪ uses. A worklist iterates to a fixed point, requeuing
    // predecessors whenever a block's LIVE_IN grows. Returns LIVE_IN for every block.
    static std::vector<RegSet> analyzeLiveness(cfg::CFG<Instruction> &graph)
    {
        const int n = static_cast<int>(graph.blocks.size());
        std::vector<RegSet> liveIn(n);
        std::vector<int> work;
        std::vector<char> queued(n, false);
        for (int i = 0; i < n; ++i)
        {
            work.push_back(i);
            queued[i] = true;
        }
        while (!work.empty())
        {
            const int b = work.back();
            work.pop_back();
            queued[b] = false;

            RegSet live;
            for (const int s : graph.blocks[b].successors)
                for (const auto &k : liveIn[s])
                    live.insert(k);
            auto &instrs = graph.blocks[b].instructions;
            for (auto it = instrs.rbegin(); it != instrs.rend(); ++it)
            {
                RegSet uses, defs;
                useDef(**it, uses, defs);
                for (const auto &d : defs)
                    live.erase(d);
                for (const auto &u : uses)
                    live.insert(u);
            }
            if (live != liveIn[b])
            {
                liveIn[b] = std::move(live);
                for (const int p : graph.blocks[b].predecessors)
                    if (!queued[p])
                    {
                        work.push_back(p);
                        queued[p] = true;
                    }
            }
        }
        return liveIn;
    }

    // Turn the liveness result into interference edges: replay each block backward from its
    // LIVE_OUT set, and at every instruction make each defined register interfere with all
    // registers live afterward — except, for a plain move, its source (so the two stay
    // coalescing candidates). Edges are undirected (both endpoints list each other), within
    // a register class, and deduplicated; an edge between two hardware registers is skipped
    // since the base clique already has it.
    void addInterferenceEdges(cfg::CFG<Instruction> &graph, const std::vector<RegSet> &liveIn)
    {
        std::unordered_map<std::string, std::shared_ptr<Node>> nodeByKey;
        for (auto &nd : geNodes)
            nodeByKey[keyOf(*nd->id)] = nd;
        for (auto &nd : sseNodes)
            nodeByKey[keyOf(*nd->id)] = nd;

        std::unordered_set<std::string> added;
        auto tryAddEdge = [&](const std::string &ka, const std::string &kb)
        {
            if (ka == kb)
                return;
            auto ia = nodeByKey.find(ka), ib = nodeByKey.find(kb);
            if (ia == nodeByKey.end() || ib == nodeByKey.end())
                return;
            if (isSSE(*ia->second->id) != isSSE(*ib->second->id))
                return;
            if (!isPseudo(*ia->second->id) && !isPseudo(*ib->second->id))
                return; // two hardware registers: the base clique already has this edge
            if (!added.insert(ka < kb ? ka + "|" + kb : kb + "|" + ka).second)
                return;
            ia->second->neighbours.push_back(ib->second);
            ib->second->neighbours.push_back(ia->second);
        };

        for (auto &block : graph.blocks)
        {
            RegSet live;
            for (const int s : block.successors)
                for (const auto &k : liveIn[s])
                    live.insert(k);
            for (auto it = block.instructions.rbegin(); it != block.instructions.rend(); ++it)
            {
                Instruction &instr = **it;
                RegSet uses, defs;
                useDef(instr, uses, defs);
                std::string movSrc;
                if (auto *m = dynamic_cast<MoveInstruction *>(&instr))
                    movSrc = keyOf(*m->src);
                for (const auto &d : defs)
                    for (const auto &l : live)
                    {
                        if (l == d || (!movSrc.empty() && l == movSrc))
                            continue;
                        tryAddEdge(d, l);
                    }
                for (const auto &d : defs)
                    live.erase(d);
                for (const auto &u : uses)
                    live.insert(u);
            }
        }
    }

    // Spill cost per pseudo: an estimate of the load/store traffic added if it is spilled
    // to memory. A use becomes a load and a def a store, so add one per membership in an
    // instruction's use/def sets — a read-modify-write operand naturally scores two.
    // Unweighted for now; scaling each reference by 10^loop-depth is the usual refinement
    // once loop nesting is available.
    void computeSpillCosts(cfg::CFG<Instruction> &graph)
    {
        std::unordered_map<std::string, std::shared_ptr<Node>> pseudoByKey;
        for (auto &nd : geNodes)
            if (isPseudo(*nd->id))
                pseudoByKey[keyOf(*nd->id)] = nd;
        for (auto &nd : sseNodes)
            if (isPseudo(*nd->id))
                pseudoByKey[keyOf(*nd->id)] = nd;

        for (auto &block : graph.blocks)
            for (auto &instr : block.instructions)
            {
                RegSet uses, defs;
                useDef(*instr, uses, defs);
                for (const auto &k : uses)
                    if (auto it = pseudoByKey.find(k); it != pseudoByKey.end())
                        it->second->spillCost += 1.0;
                for (const auto &k : defs)
                    if (auto it = pseudoByKey.find(k); it != pseudoByKey.end())
                        it->second->spillCost += 1.0;
            }
    }

    // The allocatable callee-saved GP registers (the ones whose use forces a prologue
    // save/restore). They take the highest available color so pseudos, which take the
    // lowest, prefer the caller-saved registers. No XMM is callee-saved under SysV.
    static bool isCalleeSavedReg(RegisterName r)
    {
        switch (r)
        {
        case RegisterName::BX:
        case RegisterName::R12:
        case RegisterName::R13:
        case RegisterName::R14:
        case RegisterName::R15:
            return true;
        default:
            return false;
        }
    }
    static bool isCalleeSavedHardReg(const Operand &op)
    {
        auto *r = dynamic_cast<const Register *>(&op);
        return r && isCalleeSavedReg(r->name);
    }

    // The book's graph coloring (Chaitin-Briggs), one register class at a time, with the
    // recursion unrolled into an explicit prune stack. Prune phase: repeatedly remove a node
    // of degree < k, or — when none remains — the cheapest spill candidate (lowest
    // spillCost / degree). Color phase: pop in reverse and give each node a color none of
    // its colored neighbours use, taking the lowest such color (a callee-saved hardware
    // register takes the highest). A node that finds no free color stays pruned and
    // uncolored: an actual spill.
    static void colorGraph(std::vector<std::shared_ptr<Node>> &nodes, int k)
    {
        auto degree = [](const Node &nd)
        {
            int d = 0;
            for (const auto &w : nd.neighbours)
                if (auto n = w.lock(); n && !n->pruned)
                    ++d;
            return d;
        };

        std::vector<Node *> stack;
        for (;;)
        {
            Node *chosen = nullptr;
            for (auto &nd : nodes)
                if (!nd->pruned && degree(*nd) < k)
                {
                    chosen = nd.get();
                    break;
                }
            if (!chosen)
            {
                double best = std::numeric_limits<double>::infinity();
                for (auto &nd : nodes)
                    if (!nd->pruned)
                    {
                        const double metric = nd->spillCost / degree(*nd);
                        if (metric < best)
                        {
                            best = metric;
                            chosen = nd.get();
                        }
                    }
            }
            if (!chosen)
                break;
            chosen->pruned = true;
            stack.push_back(chosen);
        }

        for (auto it = stack.rbegin(); it != stack.rend(); ++it)
        {
            Node *nd = *it;
            std::vector<bool> used(k + 1, false); // colors 1..k
            for (const auto &w : nd->neighbours)
                if (auto nbr = w.lock(); nbr && nbr->color >= 1 && nbr->color <= k)
                    used[nbr->color] = true;
            int color = -1;
            if (isCalleeSavedHardReg(*nd->id))
            {
                for (int c = k; c >= 1; --c)
                    if (!used[c])
                    {
                        color = c;
                        break;
                    }
            }
            else
            {
                for (int c = 1; c <= k; ++c)
                    if (!used[c])
                    {
                        color = c;
                        break;
                    }
            }
            if (color != -1)
            {
                nd->color = color;
                nd->pruned = false;
            }
        }
    }

    // Map each colored pseudo to a hardware register. The colored hardware-register nodes
    // give color -> register; a pseudo sharing that color is assigned that register. Pseudos
    // left uncolored (spilled) get no entry. Adds into `regMap` so both classes share one.
    static void createRegisterMap(const std::vector<std::shared_ptr<Node>> &nodes,
                                  std::unordered_map<std::string, RegisterName> &regMap)
    {
        std::unordered_map<int, RegisterName> colorToReg;
        for (const auto &nd : nodes)
            if (auto *r = dynamic_cast<const Register *>(nd->id.get()); r && nd->color != -1)
                colorToReg[nd->color] = r->name;
        for (const auto &nd : nodes)
            if (auto *p = dynamic_cast<const PseudoRegister *>(nd->id.get()); p && nd->color != -1)
                if (auto it = colorToReg.find(nd->color); it != colorToReg.end())
                    regMap[p->name] = it->second;
    }

    static int regBytes(AssemblyType t)
    {
        switch (t)
        {
        case AssemblyType::BYTE:
            return 1;
        case AssemblyType::LONGWORD:
            return 4;
        case AssemblyType::QUADWORD:
        case AssemblyType::DOUBLE:
            return 8;
        }
        return 8;
    }

    // An 8-byte stack-pointer adjustment (subq/addq $8, %rsp), used to pad the callee-saved
    // saves to an even count so the stack stays 16-byte aligned at calls.
    static std::unique_ptr<Instruction> stackAdjust(BinaryOp op)
    {
        return std::make_unique<BinaryInstruction>(std::make_unique<Immediate>(8),
                                                   std::make_unique<Register>(RegisterName::SP, 8),
                                                   op, AssemblyType::QUADWORD);
    }

    // Apply the coloring: rewrite each colored pseudo operand to its hardware register, drop
    // the moves that coalescing turned into register-to-same-register, and save/restore the
    // callee-saved registers the allocation used (pushed at entry, popped in reverse before
    // each return, with a lone padding adjustment keeping the stack 16-byte aligned when an
    // odd number is saved). Pseudos with no color are spills, left for the existing
    // stack-slot pass to lower.
    void rewritePseudos(codegenFunction &fn,
                        const std::unordered_map<std::string, RegisterName> &regMap)
    {
        // Replace `slot` with its assigned register at the width this operand position uses
        // — set by the instruction, not the pseudo's declared type (a memory operand hid
        // this; a register encodes its width in the name).
        auto assign = [&](std::unique_ptr<Operand> &slot, int bytes)
        {
            auto *p = dynamic_cast<PseudoRegister *>(slot.get());
            if (!p)
                return;
            if (auto it = regMap.find(p->name); it != regMap.end())
                slot = std::make_unique<Register>(it->second, bytes);
        };
        for (auto &instr : fn.instructions)
        {
            Instruction *i = instr.get();
            switch (i->getKind())
            {
            case InstrKind::MoveInstruction:
            {
                auto *m = cast<MoveInstruction>(i);
                assign(m->src, regBytes(m->type));
                assign(m->dst, regBytes(m->type));
                break;
            }
            case InstrKind::MoveSXInstruction:
            {
                auto *m = cast<MoveSXInstruction>(i);
                assign(m->src, regBytes(m->srcType));
                assign(m->dst, regBytes(m->dstType));
                break;
            }
            case InstrKind::MoveZeroExtendInstruction:
            {
                auto *m = cast<MoveZeroExtendInstruction>(i);
                assign(m->src, regBytes(m->srcType));
                assign(m->dst, regBytes(m->dstType));
                break;
            }
            case InstrKind::UnaryInstruction:
            {
                auto *u = cast<UnaryInstruction>(i);
                assign(u->operand, regBytes(u->type));
                break;
            }
            case InstrKind::BinaryInstruction:
            {
                auto *b = cast<BinaryInstruction>(i);
                assign(b->src, regBytes(b->type));
                assign(b->dst, regBytes(b->type));
                break;
            }
            case InstrKind::CmpInstruction:
            {
                auto *c = cast<CmpInstruction>(i);
                assign(c->a, regBytes(c->type));
                assign(c->b, regBytes(c->type));
                break;
            }
            case InstrKind::IDivInstruction:
                assign(cast<IDivInstruction>(i)->operand, regBytes(cast<IDivInstruction>(i)->type));
                break;
            case InstrKind::DivInstruction:
                assign(cast<DivInstruction>(i)->operand, regBytes(cast<DivInstruction>(i)->type));
                break;
            case InstrKind::SetCCInstruction:
                assign(cast<SetCCInstruction>(i)->a, 1);
                break;
            case InstrKind::PushInstruction:
                assign(cast<PushInstruction>(i)->a, 8);
                break;
            case InstrKind::PopInstruction:
                assign(cast<PopInstruction>(i)->reg, 8);
                break;
            case InstrKind::LeaInstruction:
            {
                auto *l = cast<LeaInstruction>(i);
                assign(l->src, 8);
                assign(l->dst, 8);
                break;
            }
            case InstrKind::CVTSI2SD:
            {
                auto *c = cast<CVTSI2SD>(i);
                assign(c->src, regBytes(c->type));
                assign(c->dst, 8);
                break;
            }
            case InstrKind::CVTTSD2SI:
            {
                auto *c = cast<CVTTSD2SI>(i);
                assign(c->src, 8);
                assign(c->dst, regBytes(c->type));
                break;
            }
            default:
                break;
            }
        }

        std::set<RegisterName> calleeSet;
        for (const auto &kv : regMap)
            if (isCalleeSavedReg(kv.second))
                calleeSet.insert(kv.second);
        const std::vector<RegisterName> callee(calleeSet.begin(), calleeSet.end());
        const bool pad = callee.size() % 2 == 1;

        std::vector<std::unique_ptr<Instruction>> out;
        if (pad)
            out.push_back(stackAdjust(BinaryOp::Subtract));
        for (RegisterName r : callee)
            out.push_back(std::make_unique<PushInstruction>(std::make_unique<Register>(r, 8)));

        for (auto &instr : fn.instructions)
        {
            if (auto *m = dynamic_cast<MoveInstruction *>(instr.get()))
            {
                auto *s = dynamic_cast<Register *>(m->src.get());
                auto *d = dynamic_cast<Register *>(m->dst.get());
                if (s && d && s->name == d->name)
                    continue; // coalesced move: source and destination share a register
            }
            if (dynamic_cast<ReturnInstruction *>(instr.get()))
            {
                for (auto rit = callee.rbegin(); rit != callee.rend(); ++rit)
                    out.push_back(
                        std::make_unique<PopInstruction>(std::make_unique<Register>(*rit, 8)));
                if (pad)
                    out.push_back(stackAdjust(BinaryOp::Add));
            }
            out.push_back(std::move(instr));
        }
        fn.instructions = std::move(out);
    }

    // Build the interference graph for one function, color it, map the colored pseudos to
    // hardware registers, and rewrite the body to use them. The CFG owns the instructions
    // while it lives, so the body is flattened back onto the function before the rewrite.
    void buildFunctionGraph(codegenFunction &fn)
    {
        addFunctionPseudos(fn);
        auto graph = cfg::makeControlFlowGraph(std::move(fn.instructions), AsmCFGClassify{});
        addInterferenceEdges(graph, analyzeLiveness(graph));
        computeSpillCosts(graph);
        colorGraph(geNodes, static_cast<int>(geBaseCount));
        colorGraph(sseNodes, static_cast<int>(sseBaseCount));
        std::unordered_map<std::string, RegisterName> regMap;
        createRegisterMap(geNodes, regMap);
        createRegisterMap(sseNodes, regMap);
        fn.instructions = cfg::flatten(graph);
        rewritePseudos(fn, regMap);
    }

  public:
    RegisterAllocator()
    {
        createGEBaseGraph();
        createSSEBaseGraph();
        geBaseCount = geNodes.size();
        sseBaseCount = sseNodes.size();
    }

    void buildGraph(codegenProgram &program)
    {
        for (auto &node : program.nodes)
            if (auto *fn = dynamic_cast<codegenFunction *>(node.get()))
                buildFunctionGraph(*fn);
    }
};

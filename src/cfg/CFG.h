#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// A control-flow graph generic over the instruction type `I`. The partition-and-wire
// logic is the same for any linear IR, so it lives here once; a per-IR classifier policy
// supplies the only IR-specific facts (which instructions open or close a block, and a
// terminator's target). Nothing in this header depends on TACKY or the assembly IR.
namespace cfg
{

// How a basic block ends. None means it has no explicit terminator and falls through to
// the next block in program order.
enum class TermKind
{
    None,
    Jump,
    Branch,
    Return
};

template <class I> struct BasicBlock
{
    int id = -1;          // index into CFG::blocks; assigned by makeControlFlowGraph
    bool removed = false; // flagged dead by a pass; skipped when flattening / iterating
    std::vector<std::unique_ptr<I>> instructions;
    std::vector<int> predecessors;
    std::vector<int> successors;
};

// blocks[0] is ENTRY, blocks.back() is EXIT (both instruction-free); the real blocks sit
// between them in program order, so concatenating every block's instructions reproduces
// the original list.
template <class I> struct CFG
{
    std::vector<BasicBlock<I>> blocks;
    int entryId() const { return 0; }
    int exitId() const { return static_cast<int>(blocks.size()) - 1; }

    // A directed edge is mirrored in both endpoints: `to` is a successor of `from`, and
    // `from` is a predecessor of `to`.
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

// Partition `instructions` into basic blocks (a label opens a block; a jump, branch, or
// return closes one, and any trailing instructions form a final block), lay them out as
// [ENTRY, real blocks in program order, EXIT] numbered by index, and wire the edges from
// each block's terminator: ENTRY to the first real block (or EXIT if there is none); a
// jump to its target; a branch to both its target and the fall-through; a return to EXIT;
// a block with no terminator to the fall-through. `cls` supplies the IR-specific
// predicates (isLabel/labelOf/termKind/targetOf); predecessor lists mirror successors.
template <class I, class Classify>
CFG<I> makeControlFlowGraph(std::vector<std::unique_ptr<I>> instructions, const Classify &cls)
{
    std::vector<BasicBlock<I>> blocks;
    BasicBlock<I> current;
    auto closeBlock = [&]()
    {
        if (!current.instructions.empty())
        {
            blocks.push_back(std::move(current));
            current = BasicBlock<I>{};
        }
    };

    for (auto &instr : instructions)
    {
        if (cls.isLabel(*instr))
        {
            closeBlock();
            current.instructions.push_back(std::move(instr));
        }
        else if (cls.termKind(*instr) != TermKind::None)
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

    CFG<I> cfg;
    cfg.blocks.reserve(blocks.size() + 2);
    cfg.blocks.push_back(BasicBlock<I>{}); // ENTRY
    for (auto &b : blocks)
        cfg.blocks.push_back(std::move(b));
    cfg.blocks.push_back(BasicBlock<I>{}); // EXIT
    for (int i = 0; i < static_cast<int>(cfg.blocks.size()); ++i)
        cfg.blocks[i].id = i;

    const int entry = cfg.entryId();
    const int exit = cfg.exitId();
    const int firstReal = 1;
    const int lastReal = exit - 1; // < firstReal when there are no real blocks

    std::unordered_map<std::string, int> labelBlock;
    for (int i = firstReal; i <= lastReal; ++i)
        if (cls.isLabel(*cfg.blocks[i].instructions.front()))
            labelBlock[cls.labelOf(*cfg.blocks[i].instructions.front())] = i;

    cfg.addEdge(entry, lastReal >= firstReal ? firstReal : exit);

    for (int i = firstReal; i <= lastReal; ++i)
    {
        const int fallthrough = (i == lastReal) ? exit : i + 1;
        const I &last = *cfg.blocks[i].instructions.back();
        switch (cls.termKind(last))
        {
        case TermKind::Jump:
            cfg.addEdge(i, labelBlock.at(cls.targetOf(last)));
            break;
        case TermKind::Branch:
            cfg.addEdge(i, labelBlock.at(cls.targetOf(last)));
            cfg.addEdge(i, fallthrough);
            break;
        case TermKind::Return:
            cfg.addEdge(i, exit);
            break;
        case TermKind::None:
            cfg.addEdge(i, fallthrough);
            break;
        }
    }

    return cfg;
}

// Concatenate every live block's instructions back into a flat list (ENTRY/EXIT are empty
// and `removed` blocks are skipped), reproducing the function body after a CFG round-trip.
template <class I> std::vector<std::unique_ptr<I>> flatten(CFG<I> &graph)
{
    std::vector<std::unique_ptr<I>> out;
    for (auto &block : graph.blocks)
    {
        if (block.removed)
            continue;
        for (auto &instr : block.instructions)
            out.push_back(std::move(instr));
    }
    return out;
}

} // namespace cfg

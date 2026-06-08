#pragma once
#include "../support/RTTI.h"
#include "ast/ASTNodes/TackyProgram.h"
#include "ast/TopLevelNodes/TackyFunction.h"
#include "instructions/instructions.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// Pass infrastructure
// ---------------------------------------------------------------------------

struct TackyPass
{
    virtual ~TackyPass() = default;
    virtual void run(TackyFunction &fn) = 0;
    virtual const char *name() const = 0;
};

class PassManager
{
    std::vector<std::unique_ptr<TackyPass>> passes;

  public:
    void add(std::unique_ptr<TackyPass> p) { passes.push_back(std::move(p)); }

    void run(TackyProgram &prog)
    {
        for (auto &node : prog.nodes)
        {
            if (auto *fn = dynamic_cast<TackyFunction *>(node.get()))
            {
                for (auto &pass : passes)
                    pass->run(*fn);
            }
        }
    }
};

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

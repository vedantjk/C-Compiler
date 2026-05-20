#pragma once
#include "../ASTNodes/codegenProgram.h"
#include "../TopLevelNodes/codegenFunction.h"
#include "../../instructions/instructions.h"
#include <memory>
#include <ostream>

class codegenASTPrinter
{
    std::ostream &out;

    void visit(const codegenProgram& node) const
    {
        out << "    .text\n";
        for (const auto& child : node.nodes)
        {
            dispatch(*child);
        }
    }

    void visit(const codegenFunction& node) const
    {
        out << "    .globl " << node.name << "\n";
        out << node.name << ":\n";
        for (const auto& child : node.instructions)
        {
            out <<"    ";
            dispatch(*child);
            out << std::endl;
        }
    }

    void dispatch(const codegenASTNode &node) const
    {
        if (auto* p = dynamic_cast<const codegenProgram*>(&node))
        {
            visit(*p);
        }else if (auto* p = dynamic_cast<const codegenFunction*>(&node))
        {
            visit(*p);
        }
    }

    void visit(const MoveInstruction & node) const
    {
        out << "movl    ";
        dispatch(*node.src);
        out << ", ";
        dispatch(*node.dst);
    }

    void visit(const ReturnInstruction& node) const
    {
        out << "ret";
    }

    void dispatch(const Instruction& node) const
    {
        if (auto* p = dynamic_cast<const MoveInstruction*>(&node))
        {
            visit(*p);
        }else if (auto* p = dynamic_cast<const ReturnInstruction*>(&node))
        {
            visit(*p);
        }
    }

    void visit(const Immediate& node) const
    {
        out << "$" << node.value;
    }

    void visit(const Register& node) const
    {
        out << "%eax";
    }

    void dispatch(const Operand& node) const
    {
        if (auto* p = dynamic_cast<const Immediate*>(&node))
        {
            visit(*p);
        }else if (auto* p = dynamic_cast<const Register*>(&node))
        {
            visit(*p);
        }
    }

    public:

    codegenASTPrinter(std::ostream& out_) : out(out_) {}

    void print(const codegenProgram& node) const
    {
        dispatch(node);
    }
};

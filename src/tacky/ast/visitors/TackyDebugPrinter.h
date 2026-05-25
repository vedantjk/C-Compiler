#pragma once
#include "../../../utils/common.h"
#include "../../instructions/instructions.h"
#include "../../instructions/val.h"
#include "../ASTNodes/TackyASTNode.h"
#include "../ASTNodes/TackyProgram.h"
#include "../TopLevelNodes/TackyFunction.h"

#include <ostream>
#include <stdexcept>
#include <variant>

class TackyDebugPrinter
{
    std::ostream &out;

    void visit(const TackyConstant &c) const { out << c.value; }

    void visit(const TackyVar &v) const { out << v.name; }

    void visit(const TackyVal &val) const
    {
        std::visit([this](const auto &v) { visit(v); }, val);
    }

    void visit(UnaryOp op) const
    {
        if (op == UnaryOp::Complement)
            out << "~";
        else if (op == UnaryOp::Negate)
            out << "-";
        else if (op == UnaryOp::Not)
            out << "!";
    }

    void visit(BinaryOp op) const
    {
        switch (op)
        {
        case BinaryOp::Add:
            out << "+";
            break;
        case BinaryOp::Subtract:
            out << "-";
            break;
        case BinaryOp::Multiply:
            out << "*";
            break;
        case BinaryOp::Divide:
            out << "/";
            break;
        case BinaryOp::Remainder:
            out << "%";
            break;
        case BinaryOp::BitwiseAnd:
            out << "&";
            break;
        case BinaryOp::BitwiseOr:
            out << "|";
            break;
        case BinaryOp::BitwiseXor:
            out << "^";
            break;
        case BinaryOp::LeftShift:
            out << "<<";
            break;
        case BinaryOp::RightShift:
            out << ">>";
            break;
        case BinaryOp::Equal:
            out << "==";
            break;
        case BinaryOp::NotEqual:
            out << "!=";
            break;
        case BinaryOp::LessThan:
            out << "<";
            break;
        case BinaryOp::LessOrEqual:
            out << "<=";
            break;
        case BinaryOp::GreaterThan:
            out << ">";
            break;
        case BinaryOp::GreaterThanOrEqual:
            out << ">=";
            break;
        }
    }

    void visit(const TackyReturn &node) const
    {
        out << "return";
        if (node.val.has_value())
        {
            out << " ";
            visit(*node.val);
        }
    }

    void visit(const TackyUnary &node) const
    {
        visit(node.dst);
        out << " = ";
        visit(node.op);
        out << " ";
        visit(node.src);
    }

    void visit(const TackyBinary &node) const
    {
        visit(node.dst);
        out << " = ";
        visit(node.src1);
        out << " ";
        visit(node.op);
        out << " ";
        visit(node.src2);
    }

    void visit(const TackyCopy &node) const
    {
        visit(node.dst);
        out << " = ";
        visit(node.src);
    }

    void visit(const TackyJump &node) const { out << "jump " << node.identifier; }

    void visit(const TackyJumpIfZero &node) const
    {
        out << "jump_if_zero ";
        visit(node.condition);
        out << ", " << node.identifier;
    }

    void visit(const TackyJumpIfNotZero &node) const
    {
        out << "jump_if_not_zero ";
        visit(node.condition);
        out << ", " << node.identifier;
    }

    void visit(const TackyLabel &node) const { out << node.identifier << ":"; }

    void dispatch(const TackyInstruction &node) const
    {
        if (auto *p = dynamic_cast<const TackyReturn *>(&node))
        {
            visit(*p);
            return;
        }
        if (auto *p = dynamic_cast<const TackyUnary *>(&node))
        {
            visit(*p);
            return;
        }
        if (auto *p = dynamic_cast<const TackyBinary *>(&node))
        {
            visit(*p);
            return;
        }
        if (auto *p = dynamic_cast<const TackyCopy *>(&node))
        {
            visit(*p);
            return;
        }
        if (auto *p = dynamic_cast<const TackyJump *>(&node))
        {
            visit(*p);
            return;
        }
        if (auto *p = dynamic_cast<const TackyJumpIfZero *>(&node))
        {
            visit(*p);
            return;
        }
        if (auto *p = dynamic_cast<const TackyJumpIfNotZero *>(&node))
        {
            visit(*p);
            return;
        }
        if (auto *p = dynamic_cast<const TackyLabel *>(&node))
        {
            visit(*p);
            return;
        }
        throw std::runtime_error("TackyDebugPrinter: unknown TackyInstruction kind");
    }

    void visit(const TackyFunction &node) const
    {
        out << "function " << node.name << ":\n";
        for (const auto &instr : node.instructions)
        {
            // Labels print flush-left as block markers; other instructions indent.
            if (!dynamic_cast<const TackyLabel *>(instr.get()))
                out << "    ";
            dispatch(*instr);
            out << "\n";
        }
    }

    void visit(const TackyProgram &node) const
    {
        for (const auto &child : node.nodes)
        {
            dispatch(*child);
        }
    }

    void dispatch(const TackyASTNode &node) const
    {
        if (auto *p = dynamic_cast<const TackyProgram *>(&node))
        {
            visit(*p);
            return;
        }
        if (auto *p = dynamic_cast<const TackyFunction *>(&node))
        {
            visit(*p);
            return;
        }
        throw std::runtime_error("TackyDebugPrinter: unknown TackyASTNode kind");
    }

  public:
    explicit TackyDebugPrinter(std::ostream &out_) : out(out_) {}

    void print(const TackyProgram &node) const { dispatch(node); }
};

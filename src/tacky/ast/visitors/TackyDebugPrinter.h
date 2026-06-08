#pragma once
#include "../../../utils/common.h"
#include "../../instructions/instructions.h"
#include "../../instructions/val.h"
#include "../ASTNodes/TackyASTNode.h"
#include "../ASTNodes/TackyProgram.h"
#include "../TopLevelNodes/TackyFunction.h"

#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <variant>

class TackyDebugPrinter
{
    std::ostream &out;

    void visit(const TackyConstant &c) const { out << c.value; }

    // Tag doubles with a trailing 'd' and full precision so they're distinct from
    // ints and exact in the dump (debug-only output).
    void visit(const TackyFloatingConstant &c) const
    {
        out << std::setprecision(17) << c.value << "d";
    }

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

    // Width/representation conversions all share the `dst = <name> src` shape.
    template <typename T> void visitConvert(const T &node, const char *name) const
    {
        visit(node.dst);
        out << " = " << name << " ";
        visit(node.src);
    }

    void visit(const TackyFunctionCall &node) const
    {
        visit(node.dst);
        out << " = call " << node.funcName << "(";
        for (size_t i = 0; i < node.args.size(); ++i)
        {
            if (i != 0)
                out << ", ";
            visit(node.args[i]);
        }
        out << ")";
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

    void visit(const TackyGetAddress &node) const
    {
        visit(node.dst);
        out << " = &";
        visit(node.src);
    }

    void visit(const TackyLoad &node) const
    {
        visit(node.dst);
        out << " = load ";
        visit(node.srcPtr);
    }

    void visit(const TackyStore &node) const
    {
        out << "store ";
        visit(node.src);
        out << " -> ";
        visit(node.dstPtr);
    }

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
        if (auto *p = dynamic_cast<const TackySignExtend *>(&node))
        {
            visitConvert(*p, "sign_extend");
            return;
        }
        if (auto *p = dynamic_cast<const TackyTruncate *>(&node))
        {
            visitConvert(*p, "truncate");
            return;
        }
        if (auto *p = dynamic_cast<const TackyZeroExtend *>(&node))
        {
            visitConvert(*p, "zero_extend");
            return;
        }
        if (auto *p = dynamic_cast<const TackyDoubleToInt *>(&node))
        {
            visitConvert(*p, "double_to_int");
            return;
        }
        if (auto *p = dynamic_cast<const TackyDoubleToUInt *>(&node))
        {
            visitConvert(*p, "double_to_uint");
            return;
        }
        if (auto *p = dynamic_cast<const TackyIntToDouble *>(&node))
        {
            visitConvert(*p, "int_to_double");
            return;
        }
        if (auto *p = dynamic_cast<const TackyUIntToDouble *>(&node))
        {
            visitConvert(*p, "uint_to_double");
            return;
        }
        if (auto *p = dynamic_cast<const TackyFunctionCall *>(&node))
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
        if (auto *p = dynamic_cast<const TackyGetAddress *>(&node))
        {
            visit(*p);
            return;
        }
        if (auto *p = dynamic_cast<const TackyLoad *>(&node))
        {
            visit(*p);
            return;
        }
        if (auto *p = dynamic_cast<const TackyStore *>(&node))
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

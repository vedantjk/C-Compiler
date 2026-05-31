#pragma once

#include "../../symboltable/SymbolTable.h"
#include "../../types/types.h"
#include "../ASTNodes/ASTNode.h"
#include "../ASTNodes/Program.h"
#include "../Expressions/AssignExpr.h"
#include "../Expressions/BinaryExpr.h"
#include "../Expressions/CastExpr.h"
#include "../Expressions/Expressions.h"
#include "../Expressions/FunctionCallExpr.h"
#include "../Expressions/InitExpr.h"
#include "../Expressions/IntLiterals.h"
#include "../Expressions/MemberExpr.h"
#include "../Expressions/SizeOfExpr.h"
#include "../Expressions/StringLiterals.h"
#include "../Expressions/SubscriptExpr.h"
#include "../Expressions/TernaryExpr.h"
#include "../Expressions/UnaryExpr.h"
#include "../Expressions/VariableExpr.h"
#include "../Statements/BlockStmt.h"
#include "../Statements/DeclareStmt.h"
#include "../Statements/DoWhileStmt.h"
#include "../Statements/ExprStmt.h"
#include "../Statements/ForStmt.h"
#include "../Statements/FunctionDeclStmt.h"
#include "../Statements/IfStmt.h"
#include "../Statements/ReturnStmt.h"
#include "../Statements/Statement.h"
#include "../Statements/WhileStmt.h"
#include "../StorageClass.h"
#include "../TopLevelNodes/Function.h"
#include "../TopLevelNodes/StructDecl.h"
#include "../TopLevelNodes/TopLevelNode.h"
#include "../TopLevelNodes/VarDecl.h"
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

class ASTDebugPrinter
{
    std::ostream &out;
    int indent = 0;

    void writeIndent() const
    {
        for (int i = 0; i < indent; ++i)
            out << "  ";
    }

    static std::string typeStr(const std::shared_ptr<Type> &t)
    {
        return t ? t->toString() : "<null>";
    }

    static std::string storageClassStr(const std::optional<StorageClass> storageClass)
    {
        if (storageClass != std::nullopt)
        {
            return toString(*storageClass);
        }
        return "";
    }

    static std::string boolStr(bool b) { return b ? "true" : "false"; }

    static std::string symbolStr(const std::shared_ptr<Symbol> &s)
    {
        if (!s)
            return "<unresolved>";
        std::ostringstream o;
        o << "{name='" << s->name << "', kind=" << kindToString(s->kind)
          << ", type=" << typeStr(s->type) << ", decl=" << s->line << ":" << s->column << "}";
        return o.str();
    }

    void printHeader(const std::string &kind, const ASTNode &n,
                     const std::string &fields = "") const
    {
        writeIndent();
        out << "[" << kind << "] " << n.line << ":" << n.col;
        if (!fields.empty())
            out << " " << fields;
        out << " symbol=" << symbolStr(n.symbol) << "\n";
    }

    void printExprHeader(const std::string &kind, const Expression &n,
                         const std::string &fields = "") const
    {
        writeIndent();
        out << "[" << kind << "] " << n.line << ":" << n.col;
        if (!fields.empty())
            out << " " << fields;
        out << " resolvedType=" << typeStr(n.resolvedType) << " isLvalue=" << boolStr(n.isLvalue)
            << " symbol=" << symbolStr(n.symbol) << "\n";
    }

    void printChild(const std::string &label, const std::shared_ptr<ASTNode> &child)
    {
        writeIndent();
        out << label << ":";
        if (!child)
        {
            out << " <null>\n";
            return;
        }
        out << "\n";
        ++indent;
        dispatch(child);
        --indent;
    }

    void dispatch(const std::shared_ptr<ASTNode> &node)
    {
        if (!node)
        {
            writeIndent();
            out << "<null>\n";
            return;
        }

        if (auto x = std::dynamic_pointer_cast<Program>(node))
        {
            visitProgram(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<Function>(node))
        {
            visitFunction(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<StructDecl>(node))
        {
            visitStructDecl(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<VarDecl>(node))
        {
            visitVarDecl(x);
            return;
        }

        if (auto x = std::dynamic_pointer_cast<BlockStmt>(node))
        {
            visitBlockStmt(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<DeclareStmt>(node))
        {
            visitDeclareStmt(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<ExprStmt>(node))
        {
            visitExprStmt(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<IfStmt>(node))
        {
            visitIfStmt(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<WhileStmt>(node))
        {
            visitWhileStmt(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<DoWhileStmt>(node))
        {
            visitDoWhileStmt(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<ForStmt>(node))
        {
            visitForStmt(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<ReturnStmt>(node))
        {
            visitReturnStmt(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<FunctionDeclStmt>(node))
        {
            visitFunctionDeclStmt(x);
            return;
        }

        if (auto x = std::dynamic_pointer_cast<IntLiterals>(node))
        {
            visitIntLiterals(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<StringLiterals>(node))
        {
            visitStringLiterals(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<VariableExpr>(node))
        {
            visitVariableExpr(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<BinaryExpr>(node))
        {
            visitBinaryExpr(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<UnaryExpr>(node))
        {
            visitUnaryExpr(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<AssignExpr>(node))
        {
            visitAssignExpr(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<TernaryExpr>(node))
        {
            visitTernaryExpr(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<CastExpr>(node))
        {
            visitCastExpr(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<FunctionCallExpr>(node))
        {
            visitFunctionCallExpr(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<SubscriptExpr>(node))
        {
            visitSubscriptExpr(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<MemberExpr>(node))
        {
            visitMemberExpr(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<SizeOfExpr>(node))
        {
            visitSizeOfExpr(x);
            return;
        }
        if (auto x = std::dynamic_pointer_cast<InitExpr>(node))
        {
            visitInitExpr(x);
            return;
        }

        writeIndent();
        out << "<unknown ASTNode kind>\n";
    }

    // ---- Top-level ----

    void visitProgram(const std::shared_ptr<Program> &n)
    {
        printHeader("Program", *n);
        ++indent;
        writeIndent();
        out << "nodes [" << n->nodes.size() << "]:\n";
        ++indent;
        for (const auto &tl : n->nodes)
            dispatch(tl);
        --indent;
        --indent;
    }

    void visitFunction(const std::shared_ptr<Function> &n)
    {
        std::ostringstream f;
        f << "name='" << n->name << "'"
          << " type=" << typeStr(n->type) << " variadic=" << boolStr(n->variadic)
          << " specifier=" << storageClassStr(n->storageClass);
        printHeader("Function", *n, f.str());
        ++indent;
        writeIndent();
        out << "parameters [" << n->parameters.size() << "]:\n";
        ++indent;
        for (const auto &p : n->parameters)
        {
            writeIndent();
            out << "[Parameter] " << p.line << ":" << p.col << " name='" << p.name << "'"
                << " type=" << typeStr(p.type) << "\n";
        }
        --indent;
        printChild("body", n->statements);
        --indent;
    }

    void visitStructDecl(const std::shared_ptr<StructDecl> &n)
    {
        std::ostringstream f;
        f << "name='" << n->name << "'"
          << " baseType=" << typeStr(n->baseType);
        printHeader("StructDecl", *n, f.str());
        ++indent;
        writeIndent();
        out << "fields [" << n->fields.size() << "]:\n";
        ++indent;
        for (const auto &field : n->fields)
        {
            writeIndent();
            out << "[StructField] " << field.line << ":" << field.column << " name='" << field.name
                << "'"
                << " type=" << typeStr(field.type) << "\n";
        }
        --indent;
        --indent;
    }

    void visitVarDecl(const std::shared_ptr<VarDecl> &n)
    {
        std::ostringstream f;
        f << "name='" << n->name << "'"
          << " type=" << typeStr(n->type) << " global=" << boolStr(n->global)
          << " specifier=" << storageClassStr(n->storageClass);
        printHeader("VarDecl", *n, f.str());
        ++indent;
        printChild("initialization", n->initialization);
        --indent;
    }

    // ---- Statements ----

    void visitBlockStmt(const std::shared_ptr<BlockStmt> &n)
    {
        printHeader("BlockStmt", *n);
        ++indent;
        writeIndent();
        out << "statements [" << n->statements.size() << "]:\n";
        ++indent;
        for (const auto &s : n->statements)
            dispatch(s);
        --indent;
        --indent;
    }

    void visitDeclareStmt(const std::shared_ptr<DeclareStmt> &n)
    {
        printHeader("DeclareStmt", *n);
        ++indent;
        if (std::holds_alternative<std::vector<std::shared_ptr<VarDecl>>>(n->variables))
        {
            const auto &vars = std::get<std::vector<std::shared_ptr<VarDecl>>>(n->variables);
            writeIndent();
            out << "variables [" << vars.size() << "]:\n";
            ++indent;
            for (const auto &v : vars)
                dispatch(v);
            --indent;
        }
        else
        {
            const auto &sd = std::get<std::shared_ptr<StructDecl>>(n->variables);
            printChild("struct", sd);
        }
        --indent;
    }

    void visitExprStmt(const std::shared_ptr<ExprStmt> &n)
    {
        std::ostringstream f;
        f << "printSemiColon=" << boolStr(n->printSemiColon);
        printHeader("ExprStmt", *n, f.str());
        ++indent;
        printChild("expr", n->expr);
        --indent;
    }

    void visitIfStmt(const std::shared_ptr<IfStmt> &n)
    {
        printHeader("IfStmt", *n);
        ++indent;
        printChild("condition", n->condition);
        printChild("thenBlock", n->thenBlock);
        printChild("elseBlock", n->elseBlock);
        --indent;
    }

    void visitWhileStmt(const std::shared_ptr<WhileStmt> &n)
    {
        printHeader("WhileStmt", *n);
        ++indent;
        printChild("condition", n->condition);
        printChild("whileBlock", n->whileBlock);
        --indent;
    }

    void visitDoWhileStmt(const std::shared_ptr<DoWhileStmt> &n)
    {
        printHeader("DoWhileStmt", *n);
        ++indent;
        printChild("block", n->block);
        printChild("condition", n->condition);
        --indent;
    }

    void visitForStmt(const std::shared_ptr<ForStmt> &n)
    {
        printHeader("ForStmt", *n);
        ++indent;
        printChild("initialization", n->initialization);
        printChild("condition", n->condition);
        printChild("update", n->update);
        printChild("forBlock", n->forBlock);
        --indent;
    }

    void visitReturnStmt(const std::shared_ptr<ReturnStmt> &n)
    {
        printHeader("ReturnStmt", *n);
        ++indent;
        printChild("returnExpression", n->returnExpression);
        --indent;
    }

    void visitFunctionDeclStmt(const std::shared_ptr<FunctionDeclStmt> &n)
    {
        printHeader("FunctionDeclStmt", *n);
        ++indent;
        printChild("declaration", n->declaration);
        --indent;
    }

    // ---- Expressions ----

    void visitIntLiterals(const std::shared_ptr<IntLiterals> &n) const
    {
        std::ostringstream f;
        f << "value='" << n->value << "'";
        printExprHeader("IntLiterals", *n, f.str());
    }

    void visitStringLiterals(const std::shared_ptr<StringLiterals> &n) const
    {
        std::ostringstream f;
        f << "literal='" << n->literal << "'";
        printExprHeader("StringLiterals", *n, f.str());
    }

    void visitVariableExpr(const std::shared_ptr<VariableExpr> &n) const
    {
        std::ostringstream f;
        f << "name='" << n->name << "'";
        printExprHeader("VariableExpr", *n, f.str());
    }

    void visitBinaryExpr(const std::shared_ptr<BinaryExpr> &n)
    {
        std::ostringstream f;
        f << "op='" << n->binaryOp << "'";
        printExprHeader("BinaryExpr", *n, f.str());
        ++indent;
        printChild("left", n->left);
        printChild("right", n->right);
        --indent;
    }

    void visitUnaryExpr(const std::shared_ptr<UnaryExpr> &n)
    {
        std::ostringstream f;
        f << "op='" << n->op << "' isPostFix=" << boolStr(n->isPostFix);
        printExprHeader("UnaryExpr", *n, f.str());
        ++indent;
        printChild("operand", n->operand);
        --indent;
    }

    void visitAssignExpr(const std::shared_ptr<AssignExpr> &n)
    {
        std::ostringstream f;
        f << "op='" << n->op << "'";
        printExprHeader("AssignExpr", *n, f.str());
        ++indent;
        printChild("lhs", n->lhs);
        printChild("rhs", n->rhs);
        --indent;
    }

    void visitTernaryExpr(const std::shared_ptr<TernaryExpr> &n)
    {
        printExprHeader("TernaryExpr", *n);
        ++indent;
        printChild("condition", n->condition);
        printChild("thenBranch", n->thenBranch);
        printChild("elseBranch", n->elseBranch);
        --indent;
    }

    void visitCastExpr(const std::shared_ptr<CastExpr> &n)
    {
        std::ostringstream f;
        f << "type=" << typeStr(n->type);
        printExprHeader("CastExpr", *n, f.str());
        ++indent;
        printChild("operand", n->operand);
        --indent;
    }

    void visitFunctionCallExpr(const std::shared_ptr<FunctionCallExpr> &n)
    {
        printExprHeader("FunctionCallExpr", *n);
        ++indent;
        printChild("functionName", n->functionName);
        writeIndent();
        out << "parameters [" << n->parameters.size() << "]:\n";
        ++indent;
        for (const auto &p : n->parameters)
            dispatch(p);
        --indent;
        --indent;
    }

    void visitSubscriptExpr(const std::shared_ptr<SubscriptExpr> &n)
    {
        printExprHeader("SubscriptExpr", *n);
        ++indent;
        printChild("lvalue", n->lvalue);
        printChild("index", n->index);
        --indent;
    }

    void visitMemberExpr(const std::shared_ptr<MemberExpr> &n)
    {
        std::ostringstream f;
        f << "field='" << n->field << "' isArrow=" << boolStr(n->isArrow);
        printExprHeader("MemberExpr", *n, f.str());
        ++indent;
        printChild("object", n->object);
        --indent;
    }

    void visitSizeOfExpr(const std::shared_ptr<SizeOfExpr> &n)
    {
        std::ostringstream f;
        f << "type=" << typeStr(n->type);
        printExprHeader("SizeOfExpr", *n, f.str());
        ++indent;
        printChild("expr", n->expr);
        --indent;
    }

    void visitInitExpr(const std::shared_ptr<InitExpr> &n)
    {
        printExprHeader("InitExpr", *n);
        ++indent;
        writeIndent();
        out << "elements [" << n->elements.size() << "]:\n";
        ++indent;
        for (const auto &e : n->elements)
            dispatch(e);
        --indent;
        --indent;
    }

  public:
    explicit ASTDebugPrinter(std::ostream &out_) : out(out_) {}

    void print(const std::shared_ptr<ASTNode> &node) { dispatch(node); }
};

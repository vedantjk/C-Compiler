#pragma once

#include "../../support/RTTI.h"
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

    // printChild with raw pointer — handles nullable edges
    void printChild(const std::string &label, const ASTNode *child)
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

    void dispatch(const ASTNode *node)
    {
        if (!node)
        {
            writeIndent();
            out << "<null>\n";
            return;
        }

        switch (node->getKind())
        {
        case NodeKind::Program:
            visitProgram(cast<Program>(node));
            break;
        case NodeKind::Function:
            visitFunction(cast<Function>(node));
            break;
        case NodeKind::StructDecl:
            visitStructDecl(cast<StructDecl>(node));
            break;
        case NodeKind::VarDecl:
            visitVarDecl(cast<VarDecl>(node));
            break;
        case NodeKind::BlockStmt:
            visitBlockStmt(cast<BlockStmt>(node));
            break;
        case NodeKind::DeclareStmt:
            visitDeclareStmt(cast<DeclareStmt>(node));
            break;
        case NodeKind::ExprStmt:
            visitExprStmt(cast<ExprStmt>(node));
            break;
        case NodeKind::IfStmt:
            visitIfStmt(cast<IfStmt>(node));
            break;
        case NodeKind::WhileStmt:
            visitWhileStmt(cast<WhileStmt>(node));
            break;
        case NodeKind::DoWhileStmt:
            visitDoWhileStmt(cast<DoWhileStmt>(node));
            break;
        case NodeKind::ForStmt:
            visitForStmt(cast<ForStmt>(node));
            break;
        case NodeKind::ReturnStmt:
            visitReturnStmt(cast<ReturnStmt>(node));
            break;
        case NodeKind::FunctionDeclStmt:
            visitFunctionDeclStmt(cast<FunctionDeclStmt>(node));
            break;
        case NodeKind::IntLiterals:
            visitIntLiterals(cast<IntLiterals>(node));
            break;
        case NodeKind::StringLiterals:
            visitStringLiterals(cast<StringLiterals>(node));
            break;
        case NodeKind::VariableExpr:
            visitVariableExpr(cast<VariableExpr>(node));
            break;
        case NodeKind::BinaryExpr:
            visitBinaryExpr(cast<BinaryExpr>(node));
            break;
        case NodeKind::UnaryExpr:
            visitUnaryExpr(cast<UnaryExpr>(node));
            break;
        case NodeKind::AssignExpr:
            visitAssignExpr(cast<AssignExpr>(node));
            break;
        case NodeKind::TernaryExpr:
            visitTernaryExpr(cast<TernaryExpr>(node));
            break;
        case NodeKind::CastExpr:
            visitCastExpr(cast<CastExpr>(node));
            break;
        case NodeKind::FunctionCallExpr:
            visitFunctionCallExpr(cast<FunctionCallExpr>(node));
            break;
        case NodeKind::SubscriptExpr:
            visitSubscriptExpr(cast<SubscriptExpr>(node));
            break;
        case NodeKind::MemberExpr:
            visitMemberExpr(cast<MemberExpr>(node));
            break;
        case NodeKind::SizeOfExpr:
            visitSizeOfExpr(cast<SizeOfExpr>(node));
            break;
        case NodeKind::InitExpr:
            visitInitExpr(cast<InitExpr>(node));
            break;
        default:
            writeIndent();
            out << "<unknown ASTNode kind>\n";
            break;
        }
    }

    // ---- Top-level ----

    void visitProgram(const Program *n)
    {
        printHeader("Program", *n);
        ++indent;
        writeIndent();
        out << "nodes [" << n->nodes.size() << "]:\n";
        ++indent;
        for (const auto &tl : n->nodes)
            dispatch(tl.get());
        --indent;
        --indent;
    }

    void visitFunction(const Function *n)
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
        printChild("body", n->statements.get());
        --indent;
    }

    void visitStructDecl(const StructDecl *n)
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

    void visitVarDecl(const VarDecl *n)
    {
        std::ostringstream f;
        f << "name='" << n->name << "'"
          << " type=" << typeStr(n->type) << " global=" << boolStr(n->global)
          << " specifier=" << storageClassStr(n->storageClass);
        printHeader("VarDecl", *n, f.str());
        ++indent;
        printChild("initialization", n->initialization.get());
        --indent;
    }

    // ---- Statements ----

    void visitBlockStmt(const BlockStmt *n)
    {
        printHeader("BlockStmt", *n);
        ++indent;
        writeIndent();
        out << "statements [" << n->statements.size() << "]:\n";
        ++indent;
        for (const auto &s : n->statements)
            dispatch(s.get());
        --indent;
        --indent;
    }

    void visitDeclareStmt(const DeclareStmt *n)
    {
        printHeader("DeclareStmt", *n);
        ++indent;
        if (std::holds_alternative<std::vector<std::unique_ptr<VarDecl>>>(n->variables))
        {
            const auto &vars = std::get<std::vector<std::unique_ptr<VarDecl>>>(n->variables);
            writeIndent();
            out << "variables [" << vars.size() << "]:\n";
            ++indent;
            for (const auto &v : vars)
                dispatch(v.get());
            --indent;
        }
        else
        {
            const auto &sd = std::get<std::unique_ptr<StructDecl>>(n->variables);
            printChild("struct", sd.get());
        }
        --indent;
    }

    void visitExprStmt(const ExprStmt *n)
    {
        std::ostringstream f;
        f << "printSemiColon=" << boolStr(n->printSemiColon);
        printHeader("ExprStmt", *n, f.str());
        ++indent;
        printChild("expr", n->expr.get());
        --indent;
    }

    void visitIfStmt(const IfStmt *n)
    {
        printHeader("IfStmt", *n);
        ++indent;
        printChild("condition", n->condition.get());
        printChild("thenBlock", n->thenBlock.get());
        printChild("elseBlock", n->elseBlock.get());
        --indent;
    }

    void visitWhileStmt(const WhileStmt *n)
    {
        printHeader("WhileStmt", *n);
        ++indent;
        printChild("condition", n->condition.get());
        printChild("whileBlock", n->whileBlock.get());
        --indent;
    }

    void visitDoWhileStmt(const DoWhileStmt *n)
    {
        printHeader("DoWhileStmt", *n);
        ++indent;
        printChild("block", n->block.get());
        printChild("condition", n->condition.get());
        --indent;
    }

    void visitForStmt(const ForStmt *n)
    {
        printHeader("ForStmt", *n);
        ++indent;
        printChild("initialization", n->initialization.get());
        printChild("condition", n->condition.get());
        printChild("update", n->update.get());
        printChild("forBlock", n->forBlock.get());
        --indent;
    }

    void visitReturnStmt(const ReturnStmt *n)
    {
        printHeader("ReturnStmt", *n);
        ++indent;
        printChild("returnExpression", n->returnExpression.get());
        --indent;
    }

    void visitFunctionDeclStmt(const FunctionDeclStmt *n)
    {
        printHeader("FunctionDeclStmt", *n);
        ++indent;
        printChild("declaration", n->declaration.get());
        --indent;
    }

    // ---- Expressions ----

    void visitIntLiterals(const IntLiterals *n) const
    {
        std::ostringstream f;
        f << "value='" << n->value << "'";
        printExprHeader("IntLiterals", *n, f.str());
    }

    void visitStringLiterals(const StringLiterals *n) const
    {
        std::ostringstream f;
        f << "literal='" << n->literal << "'";
        printExprHeader("StringLiterals", *n, f.str());
    }

    void visitVariableExpr(const VariableExpr *n) const
    {
        std::ostringstream f;
        f << "name='" << n->name << "'";
        printExprHeader("VariableExpr", *n, f.str());
    }

    void visitBinaryExpr(const BinaryExpr *n)
    {
        std::ostringstream f;
        f << "op='" << n->binaryOp << "'";
        printExprHeader("BinaryExpr", *n, f.str());
        ++indent;
        printChild("left", n->left.get());
        printChild("right", n->right.get());
        --indent;
    }

    void visitUnaryExpr(const UnaryExpr *n)
    {
        std::ostringstream f;
        f << "op='" << n->op << "' isPostFix=" << boolStr(n->isPostFix);
        printExprHeader("UnaryExpr", *n, f.str());
        ++indent;
        printChild("operand", n->operand.get());
        --indent;
    }

    void visitAssignExpr(const AssignExpr *n)
    {
        std::ostringstream f;
        f << "op='" << n->op << "'";
        printExprHeader("AssignExpr", *n, f.str());
        ++indent;
        printChild("lhs", n->lhs.get());
        printChild("rhs", n->rhs.get());
        --indent;
    }

    void visitTernaryExpr(const TernaryExpr *n)
    {
        printExprHeader("TernaryExpr", *n);
        ++indent;
        printChild("condition", n->condition.get());
        printChild("thenBranch", n->thenBranch.get());
        printChild("elseBranch", n->elseBranch.get());
        --indent;
    }

    void visitCastExpr(const CastExpr *n)
    {
        std::ostringstream f;
        f << "type=" << typeStr(n->type);
        printExprHeader("CastExpr", *n, f.str());
        ++indent;
        printChild("operand", n->operand.get());
        --indent;
    }

    void visitFunctionCallExpr(const FunctionCallExpr *n)
    {
        printExprHeader("FunctionCallExpr", *n);
        ++indent;
        printChild("functionName", n->functionName.get());
        writeIndent();
        out << "parameters [" << n->parameters.size() << "]:\n";
        ++indent;
        for (const auto &p : n->parameters)
            dispatch(p.get());
        --indent;
        --indent;
    }

    void visitSubscriptExpr(const SubscriptExpr *n)
    {
        printExprHeader("SubscriptExpr", *n);
        ++indent;
        printChild("lvalue", n->lvalue.get());
        printChild("index", n->index.get());
        --indent;
    }

    void visitMemberExpr(const MemberExpr *n)
    {
        std::ostringstream f;
        f << "field='" << n->field << "' isArrow=" << boolStr(n->isArrow);
        printExprHeader("MemberExpr", *n, f.str());
        ++indent;
        printChild("object", n->object.get());
        --indent;
    }

    void visitSizeOfExpr(const SizeOfExpr *n)
    {
        std::ostringstream f;
        f << "type=" << typeStr(n->type);
        printExprHeader("SizeOfExpr", *n, f.str());
        ++indent;
        printChild("expr", n->expr.get());
        --indent;
    }

    void visitInitExpr(const InitExpr *n)
    {
        printExprHeader("InitExpr", *n);
        ++indent;
        writeIndent();
        out << "elements [" << n->elements.size() << "]:\n";
        ++indent;
        for (const auto &e : n->elements)
            dispatch(e.get());
        --indent;
        --indent;
    }

  public:
    explicit ASTDebugPrinter(std::ostream &out_) : out(out_) {}

    void print(const Program &prog) { dispatch(&prog); }
    void print(const ASTNode *node) { dispatch(node); }
};

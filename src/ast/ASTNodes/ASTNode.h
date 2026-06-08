#pragma once

#include <iostream>
#include <memory>

struct Symbol;

enum class NodeKind
{
    // Expression range
    IntLiterals,
    FloatingLiterals,
    StringLiterals,
    VariableExpr,
    BinaryExpr,
    UnaryExpr,
    AssignExpr,
    TernaryExpr,
    CastExpr,
    FunctionCallExpr,
    SubscriptExpr,
    MemberExpr,
    SizeOfExpr,
    InitExpr,
    // Statement range
    ExprStmt,
    DeclareStmt,
    BlockStmt,
    IfStmt,
    WhileStmt,
    DoWhileStmt,
    ForStmt,
    ReturnStmt,
    FunctionDeclStmt,
    BreakStmt,
    ContinueStmt,
    // TopLevel range
    Function,
    VarDecl,
    StructDecl,
    // Program
    Program,
    // Sentinels
    _FirstExpr = IntLiterals,
    _LastExpr = InitExpr,
    _FirstStmt = ExprStmt,
    _LastStmt = ContinueStmt,
    _FirstTop = Function,
    _LastTop = StructDecl,
};

static_assert(static_cast<int>(NodeKind::_FirstExpr) < static_cast<int>(NodeKind::_LastExpr),
              "Expression range must be non-empty");
static_assert(static_cast<int>(NodeKind::_LastExpr) + 1 == static_cast<int>(NodeKind::_FirstStmt),
              "Statement range must follow Expression range contiguously");
static_assert(static_cast<int>(NodeKind::_FirstStmt) < static_cast<int>(NodeKind::_LastStmt),
              "Statement range must be non-empty");
static_assert(static_cast<int>(NodeKind::_LastStmt) + 1 == static_cast<int>(NodeKind::_FirstTop),
              "TopLevel range must follow Statement range contiguously");
static_assert(static_cast<int>(NodeKind::_FirstTop) < static_cast<int>(NodeKind::_LastTop),
              "TopLevel range must be non-empty");

class ASTNode
{
  public:
    const NodeKind kind;
    int line;
    int col;
    std::shared_ptr<Symbol> symbol;

    ASTNode(NodeKind k, int line_, int col_) : kind(k), line(line_), col(col_) {}

    virtual ~ASTNode() = default;

    virtual void print(std::ostream &out, int tab) const = 0;

    NodeKind getKind() const { return kind; }
    int getLine() const { return line; }
    int getCol() const { return col; }
};

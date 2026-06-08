#pragma once

#include "../ast/ASTNodes/ASTNode.h"
#include "../types/types.h"
#include <optional>
#include <unordered_map>
#include <vector>

enum class Kind
{
    VARIABLE,
    FUNCTION,
    PARAMETER,
    STRUCT_TAG,
};

inline std::string kindToString(Kind kind)
{
    switch (kind)
    {
    case Kind::VARIABLE:
        return "variable";
    case Kind::FUNCTION:
        return "function";
    case Kind::PARAMETER:
        return "parameter";
    case Kind::STRUCT_TAG:
        return "struct";
    }
    return "unknown";
}

// Linkage and storage duration, decided by semantic analysis from the
// declaration's scope and storage-class specifier (static/extern).
enum class Linkage
{
    None,
    Internal,
    External
};

enum class StorageDuration
{
    Automatic,
    Static
};

struct Symbol
{
    std::string name;
    std::string uniqueName;
    std::shared_ptr<Type> type;
    Kind kind;
    int line, column;
    std::weak_ptr<ASTNode> node;

    // Filled in by semantic analysis for storage-class handling / codegen.
    Linkage linkage = Linkage::None;
    StorageDuration duration = StorageDuration::Automatic;
    bool defined = false;   // function body, or variable with an initializer
    bool tentative = false; // file-scope variable, no init, no extern
    // Folded static-initializer image for static-duration vars (scalars: one
    // entry; arrays: a flat sequence). Empty means "zero the whole object".
    std::vector<StaticInit> staticInits;
    Symbol(std::string name, std::shared_ptr<Type> type, int line, int column, Kind kind)
        : name(std::move(name)), type(std::move(type)), kind(kind), line(line), column(column)
    {
    }
};

class Scope
{
    std::unordered_map<std::string, std::shared_ptr<Symbol>> ordinary, tags;

  public:
    std::shared_ptr<Symbol> find(const std::string &name, Kind kind) const
    {
        if (kind == Kind::STRUCT_TAG)
        {
            auto it = tags.find(name);
            return it == tags.end() ? nullptr : it->second;
        }
        auto it = ordinary.find(name);
        return it == ordinary.end() ? nullptr : it->second;
    }

    bool insert(const std::string &name, const std::shared_ptr<Symbol> &symbol, Kind kind)
    {
        if (find(name, kind) != nullptr)
            return false;
        if (kind == Kind::STRUCT_TAG)
        {
            tags[name] = symbol;
        }
        else
        {
            ordinary[name] = symbol;
        }
        return true;
    }
};

class SymbolTable
{
    std::vector<Scope> scopes;
    std::unordered_map<std::string, int> count;
    // Linkage registry: one entry per name with linkage, TU-wide. Separate from
    // `scopes` because linkage spans the translation unit while name *visibility*
    // is block-scoped (a block-scope `extern` is one entity but only visible in
    // its block).
    std::unordered_map<std::string, std::shared_ptr<Symbol>> linked;

  public:
    SymbolTable() { scopes.emplace_back(); }

    bool insert(const std::string &name, std::shared_ptr<Symbol> symbol, Kind kind)
    {
        if (!scopes[scopes.size() - 1].insert(name, symbol, kind))
            return false;
        // No-linkage locals (block-scope variables, including block `static`) and
        // parameters get a freshly mangled name so they never alias a file-scope
        // object of the same spelling; anything with linkage — file-scope vars and
        // `extern` — keeps its source name so the linker can resolve it.
        if (kind == Kind::PARAMETER || (kind == Kind::VARIABLE && symbol->linkage == Linkage::None))
            symbol->uniqueName = name + "." + std::to_string(++count[name]);
        else
            symbol->uniqueName = name;
        return true;
    }

    std::shared_ptr<Symbol> findSameScope(const std::string &name, Kind kind) const
    {
        return scopes.back().find(name, kind);
    }

    std::shared_ptr<Symbol> find(const std::string &name, Kind kind) const
    {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
        {
            auto sym = it->find(name, kind);
            if (sym)
                return sym;
        }
        return nullptr;
    }

    // Linkage registry (functions and any extern/file-scope variable) — one
    // entity per name across the whole translation unit, regardless of scope.
    std::shared_ptr<Symbol> findLinked(const std::string &name) const
    {
        auto it = linked.find(name);
        return it == linked.end() ? nullptr : it->second;
    }

    void insertLinked(const std::string &name, const std::shared_ptr<Symbol> &symbol)
    {
        linked[name] = symbol;
        symbol->uniqueName = name; // linked → source name
    }

    // Bind an already-built symbol into the current scope without renaming it,
    // so a linked declaration is *visible* for name resolution (file-scope vars
    // file-wide, a block-scope extern only within its block) and a same-scope
    // no-linkage redeclaration conflicts. False if the name is already in scope.
    bool bindCurrent(const std::string &name, const std::shared_ptr<Symbol> &symbol, Kind kind)
    {
        return scopes.back().insert(name, symbol, kind);
    }

    void enterScope() { scopes.emplace_back(); }

    void exitScope() { scopes.pop_back(); }
};
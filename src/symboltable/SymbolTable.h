#pragma once

#include "../types/types.h"
#include "../ast/ASTNodes/ASTNode.h"
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
        case Kind::VARIABLE:   return "variable";
        case Kind::FUNCTION:   return "function";
        case Kind::PARAMETER:  return "parameter";
        case Kind::STRUCT_TAG: return "struct";
    }
    return "unknown";
}

struct Symbol
{
    std::string name;
    std::shared_ptr<Type> type;
    Kind kind;
    int line, column;
    std::weak_ptr<ASTNode> node;
    Symbol(std::string name, std::shared_ptr<Type> type, int line, int column, Kind kind) :
    name(std::move(name)), type(std::move(type)), kind(kind), line(line), column(column)  {}
};

class Scope
{
    std::unordered_map<std::string, std::shared_ptr<Symbol>> ordinary, tags;
    public:

    std::shared_ptr<Symbol> find(const std::string &name, Kind kind) const {
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
        if (find(name, kind) != nullptr) return false;
        if (kind == Kind::STRUCT_TAG)
        {
            tags[name] = symbol;
        }else
        {
            ordinary[name] = symbol;
        }
        return true;
    }
};

class SymbolTable
{
    std::vector<Scope> scopes;
    public:
    SymbolTable()
    {
        scopes.emplace_back();
    }

    bool insert(const std::string &name, std::shared_ptr<Symbol> symbol, Kind kind)
    {
        return scopes[scopes.size()-1].insert(name, symbol, kind);
    }

    std::shared_ptr<Symbol> find(const std::string &name, Kind kind) const
    {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
        {
            auto sym = it->find(name, kind);
            if (sym) return sym;
        }
        return nullptr;
    }

    void enterScope()
    {
        scopes.emplace_back();
    }

    void exitScope()
    {
        scopes.pop_back();
    }
};
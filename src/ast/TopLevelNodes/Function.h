#pragma once

#include "../../types/types.h"
#include "../Statements/BlockStmt.h"
#include "../StorageClass.h"
#include "./TopLevelNode.h"
#include <algorithm>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>
struct Parameter
{
    std::shared_ptr<Type> type;
    std::string name;
    int line;
    int col;

    Parameter(std::shared_ptr<Type> type_, std::string name_, int line_, int col_)
        : type(std::move(type_)), name(std::move(name_)), line(line_), col(col_)
    {
    }
};

class Function : public TopLevelNode
{
  public:
    std::string name;
    std::shared_ptr<Type> type;
    std::vector<Parameter> parameters;
    std::shared_ptr<BlockStmt> statements;
    bool variadic;
    std::optional<StorageClass> storageClass;

    Function(int line_, int col_, std::string name_, std::shared_ptr<Type> type_,
             std::vector<Parameter> parameters_, std::shared_ptr<BlockStmt> statements_,
             bool variadic = false, std::optional<StorageClass> storageClass_ = std::nullopt)
        : TopLevelNode(NodeKind::Function, line_, col_), name(std::move(name_)),
          type(std::move(type_)), parameters(std::move(parameters_)),
          statements(std::move(statements_)), variadic(variadic), storageClass(storageClass_)
    {
    }

    static bool classof(NodeKind k) { return k == NodeKind::Function; }

    void print(std::ostream &out, int tab) const override
    {
        out << type->toString() << " ";
        if (storageClass != std::nullopt)
            out << toString(*storageClass) << " ";
        out << name << "(";
        for (int i = 0; i < (int)parameters.size(); i++)
        {
            out << parameters[i].type->toString() << " " << parameters[i].name;
            if (i != (int)parameters.size() - 1)
                out << ", ";
        }
        if (variadic)
        {
            out << ", ... ";
        }
        out << " ) ";
        if (statements != nullptr)
        {
            out << "{\n";
            statements->print(out, tab + 1);
            out << "}";
        }
        else
        {
            out << ";";
        }
    }
};

#pragma once

#include "../../types/types.h"
#include "../Expressions/Expressions.h"
#include "../StorageClass.h"
#include "./TopLevelNode.h"
#include <iostream>
#include <memory>
#include <optional>
#include <string>

class VarDecl : public TopLevelNode
{
  public:
    std::string name;
    std::shared_ptr<Type> type;
    std::shared_ptr<Expression> initialization;
    bool global;
    std::optional<StorageClass> storageClass;

    VarDecl(int line_, int col_, std::string name_, std::shared_ptr<Type> type_,
            std::shared_ptr<Expression> initialization_, bool global = false,
            std::optional<StorageClass> storageClass_ = std::nullopt)
        : TopLevelNode(line_, col_), name(name_), type(type_),
          initialization(std::move(initialization_)), global(global), storageClass(storageClass_)
    {
    }

    void print(std::ostream &out, int tab) const override
    {
        out << type->toString() << " ";
        if (storageClass != std::nullopt)
            out << toString(*storageClass) << " ";
        out << name;
        if (initialization)
        {
            out << " = ";
            initialization->print(out, tab);
        }
    }
};
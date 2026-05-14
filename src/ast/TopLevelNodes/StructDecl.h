#pragma once

#include "TopLevelNode.h"
#include "types/types.h"

#include <vector>

struct StructField
{
    std::shared_ptr<Type> type;
    std::string name;
    int line;
    int column;
    std::string toString() const
    {
        return type->toString() + " " + name;
    }
};

class StructDecl : public TopLevelNode
{
public:
    std::string name;
    std::vector<StructField> fields;
    std::shared_ptr<Type> baseType;

    StructDecl(std::string name, std::vector<StructField> fields, int line, int col, std::shared_ptr<Type> baseType) :
    TopLevelNode(line, col), name(std::move(name)), fields(std::move(fields)), baseType(std::move(baseType)) {}

    void print(std::ostream& out, int tab) const override
    {
        out<<"struct "<<name<<" {\n";
        for (auto field : fields)
        {
            for (int i = 0; i<tab; i++) out<<" ";
            out<<"  "<<field.toString()<<";\n";
        }
        for (int i = 0; i<tab; i++) out<<" ";
        out<<"};";
    }
};
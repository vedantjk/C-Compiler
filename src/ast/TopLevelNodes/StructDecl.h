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
    std::string name;
    std::vector<StructField> fields;
    public:
    StructDecl(std::string name, std::vector<StructField> fields, int line, int col) : TopLevelNode(line, col), name(std::move(name)), fields(std::move(fields)) {}

    void print(std::ostream& out, int tab) const override
    {
        out<<name<<" {\n";
        for (auto field : fields)
        {
            for (int i = 0; i<tab; i++) out<<" ";
            out<<"  "<<field.toString()<<";\n";
        }
        for (int i = 0; i<tab; i++) out<<" ";
        out<<"};";
    }
};
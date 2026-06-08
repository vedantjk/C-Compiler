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
    std::string toString() const { return type->toString() + " " + name; }
};

class StructDecl : public TopLevelNode
{
  public:
    std::string name;
    std::vector<StructField> fields;
    std::shared_ptr<Type> baseType;
    // A definition `struct s { ... };` is complete; a forward declaration
    // `struct s;` only introduces the tag and is incomplete (empty fields).
    bool isComplete;

    StructDecl(std::string name, std::vector<StructField> fields, int line, int col,
               std::shared_ptr<Type> baseType, bool isComplete = true)
        : TopLevelNode(line, col), name(std::move(name)), fields(std::move(fields)),
          baseType(std::move(baseType)), isComplete(isComplete)
    {
    }

    void print(std::ostream &out, int tab) const override
    {
        if (!isComplete)
        {
            out << "struct " << name << ";";
            return;
        }
        out << "struct " << name << " {\n";
        for (auto field : fields)
        {
            for (int i = 0; i < tab; i++)
                out << " ";
            out << "  " << field.toString() << ";\n";
        }
        for (int i = 0; i < tab; i++)
            out << " ";
        out << "};";
    }
};
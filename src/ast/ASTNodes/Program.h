#pragma once

#include "../TopLevelNodes/TopLevelNode.h"
#include "./ASTNode.h"
#include <iostream>
#include <memory>
#include <vector>

class Program : public ASTNode
{
  public:
    std::vector<std::shared_ptr<TopLevelNode>> nodes;

    explicit Program(std::vector<std::shared_ptr<TopLevelNode>> nodes)
        : ASTNode(NodeKind::Program, 0, 0), nodes(std::move(nodes))
    {
    }

    static bool classof(NodeKind k) { return k == NodeKind::Program; }

    void print(std::ostream &out, int tab) const override
    {
        for (auto &node : nodes)
        {
            node->print(out, tab);
            out << "\n";
        }
    }
};

#pragma once
#include <memory>
#include <string>

class Type
{
public:
    [[nodiscard]] virtual std::string toString() const = 0;
    virtual ~Type() = default;
};

class IntType : public Type
{
    IntType() = default;
    public:
    static std::shared_ptr<Type> getInstance()
    {
        static std::shared_ptr<Type> instance(new IntType());
        return instance;
    }

    [[nodiscard]] std::string toString() const override
    {
        return "int";
    }
};

class CharType : public Type
{
    CharType() = default;
public:
    static std::shared_ptr<Type> getInstance()
    {
        static std::shared_ptr<Type> instance(new CharType());
        return instance;
    }

    [[nodiscard]] std::string toString() const override
    {
        return "char";
    }
};

class VoidType : public Type
{
    VoidType() = default;
public:
    static std::shared_ptr<Type> getInstance()
    {
        static std::shared_ptr<Type> instance(new VoidType());
        return instance;
    }

    [[nodiscard]] std::string toString() const override
    {
        return "void";
    }
};

class StructType : public Type {
    std::string name;
public:
    explicit StructType(std::string n) : name(std::move(n)) {}
    [[nodiscard]] std::string toString() const override { return "struct " + name; }
    [[nodiscard]] const std::string& getName() const { return name; }
};

class PointerType : public Type
{
    std::shared_ptr<Type> inner;
    public:
    explicit PointerType(std::shared_ptr<Type> inner) : inner(std::move(inner)) {}
    [[nodiscard]] std::string toString() const override
    {
        return inner->toString() + "*";
    }
    [[nodiscard]] std::shared_ptr<Type> getInner() const { return inner; }
};

class ArrayType : public Type
{
    std::shared_ptr<Type> inner;
    size_t size;
public:
    ArrayType(std::shared_ptr<Type> inner, size_t size) : inner(std::move(inner)), size(size) {}
    [[nodiscard]] std::string toString() const override
    {
        return "[" + std::to_string(size) + "]" + inner->toString();
    }
    [[nodiscard]] std::shared_ptr<Type> getInner() const { return inner; }
};

class FunctionType : public Type
{
public:

    std::shared_ptr<Type> returnType;
    std::vector<std::shared_ptr<Type>> paramTypes;
    bool isVariadic;
    FunctionType(std::shared_ptr<Type> returnType, std::vector<std::shared_ptr<Type>> paramTypes, bool isVariadic = false):

    returnType(std::move(returnType)), paramTypes(std::move(paramTypes)), isVariadic(isVariadic) {}
    [[nodiscard]] std::string toString() const override
    {
        std::string functionTypeString;
        functionTypeString += returnType->toString();
        functionTypeString += "(";
        for (int i = 0; i < paramTypes.size(); i++)
        {
            functionTypeString += paramTypes[i]->toString();
            if (i != paramTypes.size() - 1)
            {
                functionTypeString += ", ";
            }
        }
        if (isVariadic)
        {
            functionTypeString += "...";
        }
        functionTypeString += ")";
        return functionTypeString;
    }
    [[nodiscard]] std::shared_ptr<Type> getInner() const { return returnType; }

};
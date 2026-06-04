#pragma once
#include <memory>
#include <string>

class Type
{
  public:
    [[nodiscard]] virtual std::string toString() const = 0;
    virtual ~Type() = default;
    virtual bool equals(const Type &other) const = 0;
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

    [[nodiscard]] std::string toString() const override { return "int"; }

    [[nodiscard]] bool equals(const Type &other) const override { return this == &other; }
};

class LongType : public Type
{
    LongType() = default;

  public:
    static std::shared_ptr<Type> getInstance()
    {
        static std::shared_ptr<Type> instance(new LongType());
        return instance;
    }

    [[nodiscard]] std::string toString() const override { return "long"; }

    [[nodiscard]] bool equals(const Type &other) const override { return this == &other; }
};

class UnsignedIntType : public Type
{
    UnsignedIntType() = default;

  public:
    static std::shared_ptr<Type> getInstance()
    {
        static std::shared_ptr<Type> instance(new UnsignedIntType());
        return instance;
    }

    [[nodiscard]] std::string toString() const override { return "unsigned int"; }

    [[nodiscard]] bool equals(const Type &other) const override { return this == &other; }
};

class UnsignedLongType : public Type
{
    UnsignedLongType() = default;

  public:
    static std::shared_ptr<Type> getInstance()
    {
        static std::shared_ptr<Type> instance(new UnsignedLongType());
        return instance;
    }

    [[nodiscard]] std::string toString() const override { return "unsigned long"; }

    [[nodiscard]] bool equals(const Type &other) const override { return this == &other; }
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

    [[nodiscard]] std::string toString() const override { return "char"; }

    [[nodiscard]] bool equals(const Type &other) const override { return this == &other; }
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

    [[nodiscard]] std::string toString() const override { return "void"; }

    [[nodiscard]] bool equals(const Type &other) const override { return this == &other; }
};

class StructType : public Type
{
    std::string name;

  public:
    explicit StructType(std::string n) : name(std::move(n)) {}
    [[nodiscard]] std::string toString() const override { return "struct " + name; }
    [[nodiscard]] const std::string &getName() const { return name; }

    [[nodiscard]] bool equals(const Type &other) const override
    {
        auto s = dynamic_cast<const StructType *>(&other);
        if (!s)
            return false;

        return name == s->getName();
    }
};

class PointerType : public Type
{
    std::shared_ptr<Type> inner;

  public:
    explicit PointerType(std::shared_ptr<Type> inner) : inner(std::move(inner)) {}
    [[nodiscard]] std::string toString() const override { return inner->toString() + "*"; }
    [[nodiscard]] std::shared_ptr<Type> getInner() const { return inner; }

    [[nodiscard]] bool equals(const Type &other) const override
    {
        auto p = dynamic_cast<const PointerType *>(&other);
        if (!p)
            return false;

        return inner->equals(*p->inner);
    }
};

class ArrayType : public Type
{
    std::shared_ptr<Type> inner;
    size_t size;

  public:
    ArrayType(std::shared_ptr<Type> inner, size_t size) : inner(std::move(inner)), size(size) {}
    [[nodiscard]] std::string toString() const override
    {
        return inner->toString() + "[" + std::to_string(size) + "]";
    }
    [[nodiscard]] std::shared_ptr<Type> getInner() const { return inner; }

    [[nodiscard]] bool equals(const Type &other) const override
    {
        auto a = dynamic_cast<const ArrayType *>(&other);
        if (!a)
            return false;
        if (size != a->size)
            return false;
        return inner->equals(*a->inner);
    }
    [[nodiscard]] size_t getSize() const { return size; }
};

class FunctionType : public Type
{
  public:
    std::shared_ptr<Type> returnType;
    std::vector<std::shared_ptr<Type>> paramTypes;
    bool isVariadic;
    FunctionType(std::shared_ptr<Type> returnType, std::vector<std::shared_ptr<Type>> paramTypes,
                 bool isVariadic = false)
        :

          returnType(std::move(returnType)), paramTypes(std::move(paramTypes)),
          isVariadic(isVariadic)
    {
    }
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

    [[nodiscard]] bool equals(const Type &other) const override
    {
        auto f = dynamic_cast<const FunctionType *>(&other);
        if (!f)
            return false;
        if (isVariadic != f->isVariadic)
            return false;
        if (paramTypes.size() != f->paramTypes.size())
            return false;
        if (!returnType->equals(*f->returnType))
            return false;
        for (size_t i = 0; i < paramTypes.size(); ++i)
            if (!paramTypes[i]->equals(*f->paramTypes[i]))
                return false;
        return true;
    }
};

inline bool canDecayTo(std::shared_ptr<Type> &from, const std::shared_ptr<Type> &to)
{
    if (from->equals(*to))
        return true;
    auto p = std::dynamic_pointer_cast<ArrayType>(from);
    if (!p)
        return false;
    auto q = std::dynamic_pointer_cast<PointerType>(to);
    if (!q)
        return false;
    if (!p->getInner()->equals(*q->getInner()))
        return false;
    return true;
}
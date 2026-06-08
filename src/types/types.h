#pragma once
#include <memory>
#include <string>

// One entry in a static initializer's data image. A scalar is a single entry; an
// array is a flat sequence of entries with a trailing Zero for any uninitialized
// tail. Emitted as .long / .quad / .double / .zero.
struct StaticInit
{
    enum class Kind
    {
        Char,   // 1-byte integer
        Int,    // 4-byte integer
        Long,   // 8-byte integer (also pointers)
        Double, // 8-byte IEEE double
        Zero,   // `zeroBytes` bytes of zero
        String  // raw bytes of a string literal (`strNull` adds a trailing null)
    } kind;
    long long intVal = 0;
    double dblVal = 0.0;
    long long zeroBytes = 0;
    std::string strVal;
    bool strNull = false;

    static StaticInit i8(long long v) { return {Kind::Char, v, 0.0, 0}; }
    static StaticInit i32(long long v) { return {Kind::Int, v, 0.0, 0}; }
    static StaticInit i64(long long v) { return {Kind::Long, v, 0.0, 0}; }
    static StaticInit dbl(double d) { return {Kind::Double, 0, d, 0}; }
    static StaticInit zero(long long n) { return {Kind::Zero, 0, 0.0, n}; }
    static StaticInit str(std::string bytes, bool nullTerminated)
    {
        StaticInit s{Kind::String, 0, 0.0, 0};
        s.strVal = std::move(bytes);
        s.strNull = nullTerminated;
        return s;
    }
};

// Decode a string-literal lexeme (surrounding quotes included, C escape sequences
// intact) into its raw bytes. Shared by semantic analysis (static images) and the
// TACKY/codegen string handling.
inline std::string decodeStringLiteral(const std::string &lit)
{
    std::string out;
    for (size_t i = 1; i + 1 < lit.size(); ++i)
    {
        char c = lit[i];
        if (c != '\\')
        {
            out += c;
            continue;
        }
        char e = lit[++i];
        switch (e)
        {
        case 'n':
            out += '\n';
            break;
        case 't':
            out += '\t';
            break;
        case 'r':
            out += '\r';
            break;
        case 'a':
            out += '\a';
            break;
        case 'b':
            out += '\b';
            break;
        case 'f':
            out += '\f';
            break;
        case 'v':
            out += '\v';
            break;
        case '\\':
            out += '\\';
            break;
        case '\'':
            out += '\'';
            break;
        case '"':
            out += '"';
            break;
        case '?':
            out += '\?';
            break;
        case '0':
            out += '\0';
            break;
        default:
            out += e;
            break;
        }
    }
    return out;
}

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

class DoubleType : public Type
{
    DoubleType() = default;

  public:
    static std::shared_ptr<Type> getInstance()
    {
        static std::shared_ptr<Type> instance(new DoubleType());
        return instance;
    }

    [[nodiscard]] std::string toString() const override { return "double"; }

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

class SignedCharType : public Type
{
    SignedCharType() = default;

  public:
    static std::shared_ptr<Type> getInstance()
    {
        static std::shared_ptr<Type> instance(new SignedCharType());
        return instance;
    }

    [[nodiscard]] std::string toString() const override { return "signed char"; }

    [[nodiscard]] bool equals(const Type &other) const override { return this == &other; }
};

class UnsignedCharType : public Type
{
    UnsignedCharType() = default;

  public:
    static std::shared_ptr<Type> getInstance()
    {
        static std::shared_ptr<Type> instance(new UnsignedCharType());
        return instance;
    }

    [[nodiscard]] std::string toString() const override { return "unsigned char"; }

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
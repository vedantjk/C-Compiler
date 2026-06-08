#pragma once
#include <memory>
#include <string>

// Kind tag for every concrete Type subclass. ORDER matters: characters are
// Char..UChar (contiguous), integers are Char..ULong (contiguous superset).
enum class TypeKind
{
    Char,
    SChar,
    UChar,
    Int,
    UInt,
    Long,
    ULong,
    Double,
    Void,
    Struct,
    Pointer,
    Array,
    Function
};

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
        String, // raw bytes of a string literal (`strNull` adds a trailing null)
        // An 8-byte pointer to a string literal. `PointerString` carries the
        // decoded bytes and is produced by semantic analysis; TACKY interns the
        // string into a labeled constant and rewrites it to `PointerLabel` (the
        // label in `strVal`), which the emitter writes as `.quad <label>`.
        PointerString,
        PointerLabel
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
    static StaticInit ptrString(std::string bytes)
    {
        StaticInit s{Kind::PointerString, 0, 0.0, 0};
        s.strVal = std::move(bytes);
        return s;
    }
    static StaticInit ptrLabel(std::string label)
    {
        StaticInit s{Kind::PointerLabel, 0, 0.0, 0};
        s.strVal = std::move(label);
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
    const TypeKind kind;
    TypeKind getKind() const { return kind; }

    [[nodiscard]] virtual std::string toString() const = 0;
    virtual ~Type() = default;
    virtual bool equals(const Type &other) const = 0;

  protected:
    explicit Type(TypeKind k) : kind(k) {}
};

class IntType : public Type
{
    IntType() : Type(TypeKind::Int) {}

  public:
    static std::shared_ptr<Type> getInstance()
    {
        static std::shared_ptr<Type> instance(new IntType());
        return instance;
    }

    [[nodiscard]] std::string toString() const override { return "int"; }

    [[nodiscard]] bool equals(const Type &other) const override { return this == &other; }

    static bool classof(TypeKind k) { return k == TypeKind::Int; }
};

class LongType : public Type
{
    LongType() : Type(TypeKind::Long) {}

  public:
    static std::shared_ptr<Type> getInstance()
    {
        static std::shared_ptr<Type> instance(new LongType());
        return instance;
    }

    [[nodiscard]] std::string toString() const override { return "long"; }

    [[nodiscard]] bool equals(const Type &other) const override { return this == &other; }

    static bool classof(TypeKind k) { return k == TypeKind::Long; }
};

class UnsignedIntType : public Type
{
    UnsignedIntType() : Type(TypeKind::UInt) {}

  public:
    static std::shared_ptr<Type> getInstance()
    {
        static std::shared_ptr<Type> instance(new UnsignedIntType());
        return instance;
    }

    [[nodiscard]] std::string toString() const override { return "unsigned int"; }

    [[nodiscard]] bool equals(const Type &other) const override { return this == &other; }

    static bool classof(TypeKind k) { return k == TypeKind::UInt; }
};

class UnsignedLongType : public Type
{
    UnsignedLongType() : Type(TypeKind::ULong) {}

  public:
    static std::shared_ptr<Type> getInstance()
    {
        static std::shared_ptr<Type> instance(new UnsignedLongType());
        return instance;
    }

    [[nodiscard]] std::string toString() const override { return "unsigned long"; }

    [[nodiscard]] bool equals(const Type &other) const override { return this == &other; }

    static bool classof(TypeKind k) { return k == TypeKind::ULong; }
};

class DoubleType : public Type
{
    DoubleType() : Type(TypeKind::Double) {}

  public:
    static std::shared_ptr<Type> getInstance()
    {
        static std::shared_ptr<Type> instance(new DoubleType());
        return instance;
    }

    [[nodiscard]] std::string toString() const override { return "double"; }

    [[nodiscard]] bool equals(const Type &other) const override { return this == &other; }

    static bool classof(TypeKind k) { return k == TypeKind::Double; }
};

class CharType : public Type
{
    CharType() : Type(TypeKind::Char) {}

  public:
    static std::shared_ptr<Type> getInstance()
    {
        static std::shared_ptr<Type> instance(new CharType());
        return instance;
    }

    [[nodiscard]] std::string toString() const override { return "char"; }

    [[nodiscard]] bool equals(const Type &other) const override { return this == &other; }

    static bool classof(TypeKind k) { return k == TypeKind::Char; }
};

class SignedCharType : public Type
{
    SignedCharType() : Type(TypeKind::SChar) {}

  public:
    static std::shared_ptr<Type> getInstance()
    {
        static std::shared_ptr<Type> instance(new SignedCharType());
        return instance;
    }

    [[nodiscard]] std::string toString() const override { return "signed char"; }

    [[nodiscard]] bool equals(const Type &other) const override { return this == &other; }

    static bool classof(TypeKind k) { return k == TypeKind::SChar; }
};

class UnsignedCharType : public Type
{
    UnsignedCharType() : Type(TypeKind::UChar) {}

  public:
    static std::shared_ptr<Type> getInstance()
    {
        static std::shared_ptr<Type> instance(new UnsignedCharType());
        return instance;
    }

    [[nodiscard]] std::string toString() const override { return "unsigned char"; }

    [[nodiscard]] bool equals(const Type &other) const override { return this == &other; }

    static bool classof(TypeKind k) { return k == TypeKind::UChar; }
};

class VoidType : public Type
{
    VoidType() : Type(TypeKind::Void) {}

  public:
    static std::shared_ptr<Type> getInstance()
    {
        static std::shared_ptr<Type> instance(new VoidType());
        return instance;
    }

    [[nodiscard]] std::string toString() const override { return "void"; }

    [[nodiscard]] bool equals(const Type &other) const override { return this == &other; }

    static bool classof(TypeKind k) { return k == TypeKind::Void; }
};

class StructType : public Type
{
    std::string name;

  public:
    explicit StructType(std::string n) : Type(TypeKind::Struct), name(std::move(n)) {}
    [[nodiscard]] std::string toString() const override { return "struct " + name; }
    [[nodiscard]] const std::string &getName() const { return name; }
    // Semantic analysis rewrites the source tag to its resolved (mangled) name so
    // that shadowed same-named structs in nested scopes stay distinct downstream.
    void setName(std::string n) { name = std::move(n); }

    [[nodiscard]] bool equals(const Type &other) const override
    {
        auto s = dynamic_cast<const StructType *>(&other);
        if (!s)
            return false;

        return name == s->getName();
    }

    static bool classof(TypeKind k) { return k == TypeKind::Struct; }
};

class PointerType : public Type
{
    std::shared_ptr<Type> inner;

  public:
    explicit PointerType(std::shared_ptr<Type> inner)
        : Type(TypeKind::Pointer), inner(std::move(inner))
    {
    }
    [[nodiscard]] std::string toString() const override { return inner->toString() + "*"; }
    [[nodiscard]] std::shared_ptr<Type> getInner() const { return inner; }

    [[nodiscard]] bool equals(const Type &other) const override
    {
        auto p = dynamic_cast<const PointerType *>(&other);
        if (!p)
            return false;

        return inner->equals(*p->inner);
    }

    static bool classof(TypeKind k) { return k == TypeKind::Pointer; }
};

class ArrayType : public Type
{
    std::shared_ptr<Type> inner;
    size_t size;

  public:
    ArrayType(std::shared_ptr<Type> inner, size_t size)
        : Type(TypeKind::Array), inner(std::move(inner)), size(size)
    {
    }
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

    static bool classof(TypeKind k) { return k == TypeKind::Array; }
};

class FunctionType : public Type
{
  public:
    std::shared_ptr<Type> returnType;
    std::vector<std::shared_ptr<Type>> paramTypes;
    bool isVariadic;
    FunctionType(std::shared_ptr<Type> returnType, std::vector<std::shared_ptr<Type>> paramTypes,
                 bool isVariadic = false)
        : Type(TypeKind::Function), returnType(std::move(returnType)),
          paramTypes(std::move(paramTypes)), isVariadic(isVariadic)
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

    static bool classof(TypeKind k) { return k == TypeKind::Function; }
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
#pragma once
#include <cassert>

template <class To, class From> bool isa(const From *p) { return p && To::classof(p->getKind()); }

template <class To, class From> To *dyn_cast(From *p)
{
    return (p && To::classof(p->getKind())) ? static_cast<To *>(p) : nullptr;
}

template <class To, class From> const To *dyn_cast(const From *p)
{
    return (p && To::classof(p->getKind())) ? static_cast<const To *>(p) : nullptr;
}

template <class To, class From> To *cast(From *p)
{
    assert(p && isa<To>(p));
    return static_cast<To *>(p);
}

template <class To, class From> const To *cast(const From *p)
{
    assert(p && isa<To>(p));
    return static_cast<const To *>(p);
}

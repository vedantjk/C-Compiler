#pragma once
#include <string>

enum class StorageClass
{
    Static,
    Extern
};

inline std::string toString(const StorageClass sc)
{
    switch (sc)
    {
    case StorageClass::Extern:
        return "extern";
    case StorageClass::Static:
        return "static";
    }
    return "";
}
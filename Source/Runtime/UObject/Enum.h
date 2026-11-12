#pragma once

#include <charconv>
#include <cstring>

#include "Core/Types.h"
#include "Core/EngineString.h"
#include "Containers/Array.h"

// UENUM 항목 하나
struct FEnumEntry
{
    FString Name;
    int64 Value = 0;
    bool bHidden = false; // UMETA(Hidden): 디테일 패널 목록에서 뺀다 (Max, Count 같은 항목)
};

// UENUM 하나의 이름 표 (UE의 UEnum)
struct FEnumInfo
{
    FString Name;
    TArray<FEnumEntry> Entries;

    const FEnumEntry* FindByValue(int64 Value) const
    {
        for (const FEnumEntry& Entry : Entries)
        {
            if (Entry.Value == Value) return &Entry;
        }
        return nullptr;
    }

    const FEnumEntry* FindByName(const FString& InName) const
    {
        for (const FEnumEntry& Entry : Entries)
        {
            if (Entry.Name == InName) return &Entry;
        }
        return nullptr;
    }

    // 저장용 문자열. 표에 없는 값이면 숫자를 그대로 쓴다.
    FString ToName(int64 Value) const
    {
        const FEnumEntry* Entry = FindByValue(Value);
        return Entry ? Entry->Name : std::to_string(Value);
    }

    // ToName의 반대. 이름이 없으면 숫자로도 읽어 본다.
    bool FromName(const FString& InName, int64& OutValue) const
    {
        if (const FEnumEntry* Entry = FindByName(InName))
        {
            OutValue = Entry->Value;
            return true;
        }
        const auto Result = std::from_chars(InName.data(), InName.data() + InName.size(), OutValue);
        return Result.ec == std::errc() && Result.ptr == InName.data() + InName.size();
    }
};

// UENUM이 붙은 enum만 HHT가 특수화한다. 특수화가 없는 enum을 UPROPERTY로 쓰면 UClass::AddProperty의 static_assert가 막는다.
template <typename T>
struct TEnumInfo
{
    static constexpr bool bReflected = false;
};

// enum 크기(1/2/4/8바이트)에 맞춰 값을 읽고 쓴다. FEnumProperty::ElementSize를 넘긴다.
inline int64 ReadEnumValue(const void* Ptr, size_t Size)
{
    switch (Size)
    {
    case 1: { int8 V;  std::memcpy(&V, Ptr, 1); return V; }
    case 2: { int16 V; std::memcpy(&V, Ptr, 2); return V; }
    case 4: { int32 V; std::memcpy(&V, Ptr, 4); return V; }
    default: { int64 V; std::memcpy(&V, Ptr, 8); return V; }
    }
}

inline void WriteEnumValue(void* Ptr, size_t Size, int64 Value)
{
    switch (Size)
    {
    case 1: { const int8 V = static_cast<int8>(Value);   std::memcpy(Ptr, &V, 1); break; }
    case 2: { const int16 V = static_cast<int16>(Value); std::memcpy(Ptr, &V, 2); break; }
    case 4: { const int32 V = static_cast<int32>(Value); std::memcpy(Ptr, &V, 4); break; }
    default: { std::memcpy(Ptr, &Value, 8); break; }
    }
}
#pragma once

#include "Core/EngineString.h"

class UStruct;

// FField의 타입 정보 (UE: Field.h의 FFieldClass)
// UObject는 UClass로 타입을 구분하듯, FField는 FFieldClass로 구분한다. FField는 UObject가 아니라서 UClass를 쓸 수 없다.
class FFieldClass
{
public:
    FFieldClass(const char* InName, const FFieldClass* InSuper)
        : Name(InName), Super(InSuper)
    {
    }
     
    const char* GetName() const { return Name; }
    const FFieldClass* GetSuperClass() const { return Super; }

    bool IsChildOf(const FFieldClass* Other) const
    {
        for (const FFieldClass* Class = this; Class; Class = Class->Super)
        {
            if (Class == Other)
            {
                return true;
            }
        }
        return false;
    }

private:
    const char* Name;
    const FFieldClass* Super;
};

// FField 서브클래스마다 넣는다 (UE: Field.h의 DECLARE_FIELD)
// 템플릿 중간 클래스(TProperty<...>)는 StaticClass를 따로 두지 않으므로, 부모를 따라가면 템플릿을 건너뛰고 실제 클래스만 이어진다.
#define DECLARE_FIELD(TClass, TSuperClass)                                         \
public:                                                                            \
    using Super = TSuperClass;                                                     \
    static FFieldClass* StaticClass()                                              \
    {                                                                              \
        static FFieldClass Instance(#TClass, TSuperClass::StaticClass());          \
        return &Instance;                                                          \
    }                                                                              \
    virtual FFieldClass* GetClass() const override { return StaticClass(); }

// 리플렉션 정보의 최상위 클래스 (UE: Field.h의 FField)
// UE5는 프로퍼티를 UObject(UProperty)에서 가벼운 FField로 옮겼다. GC 대상이 아니고 UClass가 소유한다.
class FField
{
public:
    virtual ~FField() = default;

    static FFieldClass* StaticClass()
    {
        static FFieldClass Instance("Field", nullptr);
        return &Instance;
    }
    virtual FFieldClass* GetClass() const { return StaticClass(); }

    bool IsA(const FFieldClass* FieldClass) const { return GetClass()->IsChildOf(FieldClass); }

    template <typename T>
    bool IsA() const { return IsA(T::StaticClass()); }

    const FString& GetName() const { return Name; }
    UStruct* GetOwnerStruct() const { return Owner; }

    FString Name;
    UStruct* Owner = nullptr; // 이 필드를 선언한 클래스나 구조체 (UE: FField::Owner)
};

// UObject의 Cast<T>에 해당 (UE: Field.h의 CastField). 타입이 다르면 nullptr
template <typename T>
T* CastField(FField* Field)
{
    return Field && Field->IsA<T>() ? static_cast<T*>(Field) : nullptr;
}

template <typename T>
const T* CastField(const FField* Field)
{
    return Field && Field->IsA<T>() ? static_cast<const T*>(Field) : nullptr;
}

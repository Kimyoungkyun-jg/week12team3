#pragma once

#include "Core/EngineString.h"
#include "Math/EngineMath.h"
#include "Math/Color.h"
#include "Containers/Array.h"
#include "Math/Transform.h"
#include "Math/Color.h"
#include <cstddef>

class UMaterial;
class UFont;

enum class EPropertyType { Unknown, Float, Int, String, Bool, Vector, Vector4, Color, LinearColor, Rotator, Transform, Object, Array, Struct};

struct FStructPropertyField
{
	const char* Name;
	EPropertyType Type;
	size_t Offset;
};

template <typename T> struct TStructPropertyType { static constexpr bool Value = false; };

template <typename T> struct TArrayPropertyType { static constexpr bool Value = false; };
template <typename T> struct TArrayPropertyType<TArray<T>>
{
    static constexpr bool Value = true;
    using Element = T;
};

template <typename T>
constexpr EPropertyType GetPropertyType()
{
    if constexpr (TArrayPropertyType<T>::Value)
    {
        return EPropertyType::Array;
    }
    else if constexpr (TStructPropertyType<T>::Value)
    {
        return EPropertyType::Struct;
    }
    else if constexpr (std::is_pointer_v<T> &&
        std::is_base_of_v<UObject, std::remove_pointer_t<T>>)
    {
        return EPropertyType::Object;
    }
    else
    {
        static_assert(sizeof(T) == 0, "Not Valid Type");
        return EPropertyType::Unknown;
    }
}

#define DEFINE_PROPERTY_TYPE(CppType, EnumValue)                    \
    template <> constexpr EPropertyType GetPropertyType<CppType>()  \
    { return EPropertyType::EnumValue; }

DEFINE_PROPERTY_TYPE(int, Int)
DEFINE_PROPERTY_TYPE(uint32, Int)
DEFINE_PROPERTY_TYPE(float, Float)
DEFINE_PROPERTY_TYPE(bool, Bool)
DEFINE_PROPERTY_TYPE(FString, String)
DEFINE_PROPERTY_TYPE(FVector, Vector)
DEFINE_PROPERTY_TYPE(FVector4, Vector4)
DEFINE_PROPERTY_TYPE(FTransform, Transform)
DEFINE_PROPERTY_TYPE(FRotator, Rotator)
DEFINE_PROPERTY_TYPE(FColor, Color)
DEFINE_PROPERTY_TYPE(FLinearColor, LinearColor)

//DragFloat, DragInt, DragFloat3, DragFloat4, ColorEdit4 등 ImGui 위젯과 관련된 메타데이터를 저장하는 구조체
struct FProPertyWidgetMeta
{
	float MinValue;
	float MaxValue;
    float delta;
};

// 프로퍼티 지정자 (UE의 UPROPERTY 지정자에 대응)
enum EPropertyFlags : uint32
{
    CPF_None      = 0,
    CPF_Edit      = 1 << 0, // 디테일 패널에 노출
    CPF_EditConst = 1 << 1, // 노출하되 수정 불가
    CPF_Transient = 1 << 2, // 직렬화에서 제외
	CPF_Instanced = 1 << 3, // 소유한 UObject를 참조 대신 내용과 함께 저장

    CPF_EditAnywhere    = CPF_Edit,
    CPF_VisibleAnywhere = CPF_Edit | CPF_EditConst,
    CPF_Default         = CPF_EditAnywhere,
};

struct FProperty
{
    FString Name;
    EPropertyType Type;
    size_t Offset;
    size_t Size;
    UClass* Class = nullptr;
    uint32 Flags = CPF_Default;
	EPropertyType ElementType = EPropertyType::Unknown;
	UClass* ElementClass = nullptr;
	bool bUnsignedElement = false;
	int32 (*ArrayNum)(const void*) = nullptr;
	void (*ArraySetNum)(void*, int32) = nullptr;
	void* (*ArrayAt)(void*, int32) = nullptr;
	const FStructPropertyField* (*StructFields)(int32&) = nullptr;

    bool HasAnyFlags(uint32 InFlags) const { return (Flags & InFlags) != 0; }
};

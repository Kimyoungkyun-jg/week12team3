#pragma once

#include <cfloat>

#include "Field.h"
#include "Enum.h"
#include "Core/EngineString.h"
#include "Math/EngineMath.h"
#include "Math/Color.h"
#include "Math/Transform.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/MathSerialization.h"
#include "Serialization/TypeSerializer.h"

class UObject;
class UScriptStruct;

// 프로퍼티 지정자 (UE: ObjectMacros.h의 EPropertyFlags)
enum EPropertyFlags : uint32
{
    CPF_None = 0,
    CPF_Edit = 1 << 0, // 디테일 패널에 노출
    CPF_EditConst = 1 << 1, // 노출하되 수정 불가
    CPF_Transient = 1 << 2, // 직렬화에서 제외

    CPF_EditAnywhere = CPF_Edit,
    CPF_VisibleAnywhere = CPF_Edit | CPF_EditConst,
    CPF_Default = CPF_EditAnywhere,
};

// UPROPERTY(meta=(...))로 정하는 디테일 패널 숫자 위젯 설정 (UE의 ClampMin/ClampMax/UIMin/UIMax/Delta)
// 필드 순서를 바꾸면 HHT가 만드는 지정 초기화(.ClampMin = ...) 순서도 바꿔야 한다.
struct FPropertyMeta
{
    float ClampMin = -FLT_MAX; // 값 자체의 범위. Ctrl+클릭으로 직접 입력해도 이 범위로 자른다
    float ClampMax = FLT_MAX;
    float UIMin = -FLT_MAX;    // 드래그로 갈 수 있는 범위. 직접 입력은 막지 않는다
    float UIMax = FLT_MAX;
    float Delta = 0.0f;        // 드래그 1픽셀당 변화량. 0이면 위젯 기본값

    bool HasClamp() const { return ClampMin > -FLT_MAX || ClampMax < FLT_MAX; }
};

// 멤버 변수 하나의 리플렉션 정보 (UE: UnrealType.h의 FProperty)
// 값을 저장·불러오는 방법은 타입마다 다르므로 서브클래스가 가상 함수로 구현한다.
// 디테일 패널 위젯은 여기 두지 않는다. UE도 위젯은 에디터 모듈(PropertyEditor)이 CastField로 골라 그린다.
class FProperty : public FField
{
    DECLARE_FIELD(FProperty, FField)

public:
    uint32 Offset = 0;      // 객체 시작에서 멤버까지 거리 (UE: Offset_Internal)
    uint32 ElementSize = 0; // sizeof(멤버)
    uint32 PropertyFlags = CPF_Default;

    bool HasAnyFlags(uint32 InFlags) const { return (PropertyFlags & InFlags) != 0; }

    // 파일로 저장하거나 복제할 때(Persistent) Transient는 건너뛴다 (UE: FProperty::ShouldSerializeValue)
    bool ShouldSerializeValue(const FArchive& Ar) const { return !(HasAnyFlags(CPF_Transient) && Ar.IsPersistent()); }

    // 객체 주소 → 이 멤버의 주소 (UE: FProperty::ContainerPtrToValuePtr)
    template <typename ValueType = void>
    ValueType* ContainerPtrToValuePtr(void* Container) const
    {
        return reinterpret_cast<ValueType*>(static_cast<uint8*>(Container) + Offset);
    }
    template <typename ValueType = void>
    const ValueType* ContainerPtrToValuePtr(const void* Container) const
    {
        return reinterpret_cast<const ValueType*>(static_cast<const uint8*>(Container) + Offset);
    }

    // FStructuredArchive 저장·불러오기 (UE: FProperty::SerializeItem)
    // Value는 객체가 아니라 멤버의 주소다.
    virtual void SerializeItem(FStructuredArchive::FSlot Slot, void* Value) const = 0;
};

// 숫자 위젯을 쓰는 프로퍼티의 공통 부모 (UE: FNumericProperty)
// UE의 FNumericProperty는 정수·실수만이고 FVector는 FStructProperty다. Hitori는 벡터도 숫자 드래그 위젯을 쓰므로 여기 묶는다.
// UE는 meta를 FField의 문자열 맵(GetMetaData("ClampMin"))에 두지만, Hitori는 HHT가 검사한 값을 float로 바로 넣는다.
class FNumericProperty : public FProperty
{
    DECLARE_FIELD(FNumericProperty, FProperty)

public:
    FPropertyMeta Meta;
};

// C++ 타입 하나를 다루는 프로퍼티의 공통 구현 (UE: UnrealType.h의 TProperty)
template <typename InTCppType, class TInPropertyBaseClass>
class TProperty : public TInPropertyBaseClass
{
public:
    using TCppType = InTCppType;

    TProperty() { this->ElementSize = sizeof(TCppType); }

    static TCppType& GetPropertyValue(void* Value) { return *static_cast<TCppType*>(Value); }
    static const TCppType& GetPropertyValue(const void* Value) { return *static_cast<const TCppType*>(Value); }

    TCppType& GetPropertyValue_InContainer(void* Container) const
    {
        return GetPropertyValue(this->ContainerPtrToValuePtr(Container));
    }
};

// << 연산자가 있는 타입은 저장 코드를 새로 쓸 필요가 없다 (UE: TProperty_WithEqualityAndSerializer)
template <typename InTCppType, class TInPropertyBaseClass>
class TProperty_WithSerializer : public TProperty<InTCppType, TInPropertyBaseClass>
{
public:
    void SerializeItem(FStructuredArchive::FSlot Slot, void* Value) const override { Slot << this->GetPropertyValue(Value); }
};

// UE: TProperty_Numeric
template <typename InTCppType>
using TProperty_Numeric = TProperty_WithSerializer<InTCppType, FNumericProperty>;

// ---- 숫자 ----

class FFloatProperty : public TProperty_Numeric<float>
{
    DECLARE_FIELD(FFloatProperty, TProperty_Numeric<float>)
};

class FIntProperty : public TProperty_Numeric<int32>
{
    DECLARE_FIELD(FIntProperty, TProperty_Numeric<int32>)
};

class FUInt32Property : public TProperty_Numeric<uint32>
{
    DECLARE_FIELD(FUInt32Property, TProperty_Numeric<uint32>)
};

// UE에서는 셋 다 FStructProperty(Struct = TBaseStructure<FVector>) 하나로 처리한다
class FVectorProperty : public TProperty_Numeric<FVector>
{
    DECLARE_FIELD(FVectorProperty, TProperty_Numeric<FVector>)
};

class FVector4Property : public TProperty_Numeric<FVector4>
{
    DECLARE_FIELD(FVector4Property, TProperty_Numeric<FVector4>)
};

class FRotatorProperty : public TProperty_Numeric<FRotator>
{
    DECLARE_FIELD(FRotatorProperty, TProperty_Numeric<FRotator>)
};

// ---- 숫자가 아닌 것 ----

using FBoolProperty_Super = TProperty_WithSerializer<bool, FProperty>;
class FBoolProperty : public FBoolProperty_Super
{
    DECLARE_FIELD(FBoolProperty, FBoolProperty_Super)
};

// UE 이름 그대로 FStrProperty (FString 프로퍼티)
using FStrProperty_Super = TProperty_WithSerializer<FString, FProperty>;
class FStrProperty : public FStrProperty_Super
{
    DECLARE_FIELD(FStrProperty, FStrProperty_Super)
};

using FTransformProperty_Super = TProperty_WithSerializer<FTransform, FProperty>;
class FTransformProperty : public FTransformProperty_Super
{
    DECLARE_FIELD(FTransformProperty, FTransformProperty_Super)
};

// 아래는 저장 형식이 C++ 타입 그대로가 아니라서 Property.cpp에서 직접 구현한다.

// FColor(4바이트)는 RGBA를 묶은 uint32로 저장한다
using FColorProperty_Super = TProperty<FColor, FProperty>;
class FColorProperty : public FColorProperty_Super
{
    DECLARE_FIELD(FColorProperty, FColorProperty_Super)

public:
    void SerializeItem(FStructuredArchive::FSlot Slot, void* Value) const override;
};

// FLinearColor, 또는 UPROPERTY(Type=LinearColor)를 붙인 FVector4. json은 [R, G, B, A]
using FLinearColorProperty_Super = TProperty<FLinearColor, FProperty>;
class FLinearColorProperty : public FLinearColorProperty_Super
{
    DECLARE_FIELD(FLinearColorProperty, FLinearColorProperty_Super)

public:
    void SerializeItem(FStructuredArchive::FSlot Slot, void* Value) const override;
};

// UObject 포인터 (UE: FObjectProperty). json에는 에셋 경로를 쓴다.
using FObjectProperty_Super = TProperty<UObject*, FProperty>;
class FObjectProperty : public FObjectProperty_Super
{
    DECLARE_FIELD(FObjectProperty, FObjectProperty_Super)

public:
    UClass* PropertyClass = nullptr; // 멤버 선언의 클래스 (UMaterial* 이면 UMaterial)

    void SerializeItem(FStructuredArchive::FSlot Slot, void* Value) const override;
};

// UENUM을 붙인 enum class (UE: EnumProperty.h의 FEnumProperty)
// UE는 기반 정수용 FNumericProperty를 하나 품지만, Hitori는 ElementSize(1/2/4/8)로 읽고 쓴다. 저장은 이름으로 한다.
class FEnumProperty : public FProperty
{
    DECLARE_FIELD(FEnumProperty, FProperty)

public:
    const FEnumInfo* Enum = nullptr;

    int64 GetValue(const void* Value) const { return ReadEnumValue(Value, ElementSize); }
    void SetValue(void* Value, int64 NewValue) const { WriteEnumValue(Value, ElementSize, NewValue); }

    void SerializeItem(FStructuredArchive::FSlot Slot, void* Value) const override;
};

// USTRUCT()를 붙인 구조체 멤버 (UE: UnrealType.h의 FStructProperty)
// 안쪽 프로퍼티는 Struct가 가지고 있다. 저장은 Struct에게 맡기고, 컨테이너로 객체 대신 이 멤버의 주소를 넘긴다.
class FStructProperty : public FProperty
{
    DECLARE_FIELD(FStructProperty, FProperty)

public:
    UScriptStruct* Struct = nullptr;

    void SerializeItem(FStructuredArchive::FSlot Slot, void* Value) const override;
};

// ---- C++ 타입 → 프로퍼티 클래스 ----
// UE는 UHT가 멤버 타입을 읽고 .gen.cpp에 FFloatPropertyParams 같은 표를 쓴다.
// Hitori의 HHT는 타입을 읽지 않으므로 decltype(멤버)를 받아 여기서 고른다.

template <typename T>
struct TPropertyTypeFor
{
    using Type = void; // void면 UClass::AddProperty가 static_assert로 막는다
};

#define DEFINE_PROPERTY_TYPE(CppType, PropertyClass) \
    template <> struct TPropertyTypeFor<CppType> { using Type = PropertyClass; };

DEFINE_PROPERTY_TYPE(float, FFloatProperty)
DEFINE_PROPERTY_TYPE(int32, FIntProperty)
DEFINE_PROPERTY_TYPE(uint32, FUInt32Property)
DEFINE_PROPERTY_TYPE(FVector, FVectorProperty)
DEFINE_PROPERTY_TYPE(FVector4, FVector4Property)
DEFINE_PROPERTY_TYPE(FRotator, FRotatorProperty)
DEFINE_PROPERTY_TYPE(bool, FBoolProperty)
DEFINE_PROPERTY_TYPE(FString, FStrProperty)
DEFINE_PROPERTY_TYPE(FTransform, FTransformProperty)
DEFINE_PROPERTY_TYPE(FColor, FColorProperty)
DEFINE_PROPERTY_TYPE(FLinearColor, FLinearColorProperty)

template <typename T>
    requires std::is_enum_v<T>
struct TPropertyTypeFor<T>
{
    using Type = FEnumProperty;
};

template <typename T>
    requires std::is_base_of_v<UObject, T>
struct TPropertyTypeFor<T*>
{
    using Type = FObjectProperty;
};

// USTRUCT의 GENERATED_BODY()가 StaticStruct()를 만든다. FVector처럼 위 표에 직접 적은 타입은 그쪽이 먼저 걸린다.
template <typename T>
    requires requires { { T::StaticStruct() } -> std::same_as<UScriptStruct*>; }
struct TPropertyTypeFor<T>
{
    using Type = FStructProperty;
};

// meta를 쓸 수 있는 타입. HHT가 만든 static_assert가 이걸로 검사한다.
template <typename T>
constexpr bool TSupportsNumericMeta = std::is_base_of_v<FNumericProperty, typename TPropertyTypeFor<T>::Type>;
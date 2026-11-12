#pragma once

#include "Property.h"
#include "Function.h"
#include "Object.h"

using ClassConstructor = UObject * (*)(void* Memory);

// 프로퍼티 목록을 가진 타입의 공통 부모 (UE: Class.h의 UStruct)
// UE 계층: UField → UStruct → UClass(UObject 클래스) / UScriptStruct(USTRUCT 구조체) / UFunction
// 프로퍼티 등록·직렬화는 "메모리 블록 + 프로퍼티 목록"만 있으면 되므로 UObject든 구조체든 여기서 같이 처리한다.
class UStruct : public UObject
{
public:
	UStruct();
	~UStruct() override;

	FString Name;
	uint64 Size = 0; // sizeof(타입)

	TArray<FProperty*> Properties; // 이 타입에서 선언한 것만. UStruct가 소유한다 (UE: UStruct::ChildProperties)

	inline const TArray<FProperty*>& GetProperties() const { return Properties; }

	// 부모 타입 (UE: UStruct::GetSuperStruct). UClass는 부모 클래스, UScriptStruct는 부모 USTRUCT
	virtual UStruct* GetSuperStruct() const { return nullptr; }

	// 이 타입과 부모 타입에서 이름으로 찾는다. 없으면 nullptr (UE: UStruct::FindPropertyByName)
	FProperty* FindPropertyByName(const FString& InName) const;

	// Data는 이 타입의 인스턴스 주소 (UObject면 this, 구조체면 멤버의 주소)
	// FStructuredArchive (UE: UStruct::SerializeTaggedProperties)
	void SerializeTaggedProperties(FStructuredArchive::FSlot Slot, void* Data) const;

	// HHT가 만드는 코드: InClass->AddProperty<decltype(ThisClass::Speed)>("Speed", offsetof(ThisClass, Speed), CPF_Edit);
	// 프로퍼티 클래스는 멤버 타입으로 고른다(TPropertyTypeFor). UPROPERTY(Type=LinearColor)면 HHT가 두 번째 인자로 직접 넘긴다.
	// 만든 프로퍼티를 돌려주므로 HHT는 meta를 ->Meta = ...로 이어서 넣는다.
	template <typename T, typename TPropertyClass = typename TPropertyTypeFor<T>::Type>
	TPropertyClass* AddProperty(const FString& InName, uint32 InOffset, uint32 InFlags = CPF_Default)
	{
		if constexpr (std::is_void_v<TPropertyClass>)
		{
			static_assert(sizeof(T) == 0, "This type cannot be a UPROPERTY. See TPropertyTypeFor in Property.h.");
			return nullptr;
		}
		else
		{
			if constexpr (requires { typename TPropertyClass::TCppType; })
			{
				static_assert(sizeof(T) == sizeof(typename TPropertyClass::TCppType), "UPROPERTY Type= does not match the size of the member type.");
			}

			TPropertyClass* Property = new TPropertyClass();
			Property->Name = InName;
			Property->Owner = this;
			Property->Offset = InOffset;
			Property->PropertyFlags = InFlags;

			if constexpr (std::is_same_v<TPropertyClass, FObjectProperty>)
			{
				Property->PropertyClass = std::remove_pointer_t<T>::StaticClass();
			}
			else if constexpr (std::is_same_v<TPropertyClass, FEnumProperty>)
			{
				static_assert(TEnumInfo<T>::bReflected, "An enum used as a UPROPERTY needs UENUM().");
				if constexpr (TEnumInfo<T>::bReflected)
				{
					// Get이 템플릿인 이유는 HHT가 만든 TEnumInfo의 주석 참고
					Property->Enum = TEnumInfo<T>::template Get<>();
				}
				Property->ElementSize = sizeof(T);
			}
			else if constexpr (std::is_same_v<TPropertyClass, FStructProperty>)
			{
				Property->Struct = T::StaticStruct();
				Property->ElementSize = sizeof(T);
			}

			Properties.Add(Property);
			return Property;
		}
	}
};

// USTRUCT()를 붙인 구조체의 타입 정보 (UE: Class.h의 UScriptStruct)
// UClass와 달리 생성자·CDO·UFUNCTION이 없다. 구조체는 혼자 존재하지 않고 다른 객체의 멤버(FStructProperty)로만 쓰인다.
class UScriptStruct : public UStruct
{
public:
	UScriptStruct* SuperStruct = nullptr; // USTRUCT() struct FB : public FA 이면 FA

	UStruct* GetSuperStruct() const override { return SuperStruct; }
	bool IsChildOf(const UScriptStruct* BaseStruct) const;
};

// UObject 클래스의 타입 정보 (UE: Class.h의 UClass)
class UClass : public UStruct
{
public:
	UClass* Super = nullptr;
	ClassConstructor Constructor = nullptr;

	TArray<FFunction> Functions;
	UObject* ClassDefaultObject = nullptr;

	UStruct* GetSuperStruct() const override { return Super; }

	bool IsChildOf(const UClass* BaseClass) const;
	UObject* GetDefaultObject(bool bCreateIfNeeded = true) const;
	FName GetDefaultObjectName() const { return FName(FString("Default__") + Name); }

	inline const TArray<FFunction>& GetFunctions() const { return Functions; }

	// 이 클래스와 부모 클래스에서 이름으로 찾는다. 없으면 nullptr
	const FFunction* FindFunction(const FString& InName) const;

	template<class T>
	T* GetDefaultObject() const
	{
		UObject* Ret = GetDefaultObject();
		assert(Ret && Ret->IsA(T::StaticClass()) && "추상 클래스이거나 T의 자식이 아닌 클래스");
		return static_cast<T*>(Ret);
	}

	// HHT가 만드는 코드: InClass->AddFunction<&ThisClass::Foo>("Foo", FUNC_ScriptCallable);
	template <auto Func>
	void AddFunction(const FString& InName, uint32 InFlags)
	{
		Functions.Add(TNativeFunction<Func>::Make(InName, InFlags));
	}

protected:
	virtual UObject* CreateDefaultObject();
private:
	void InternalCreateDefaultObjectWrapper() const;

	mutable int32 ClassUnique = 0;
	friend FName MakeUniqueObjectName(UObject*, const UClass*, FName);
};

// USTRUCT 부모가 없으면 Super가 void다. void::StaticStruct()는 쓸 수 없으니 템플릿으로 nullptr을 돌려준다.
template <typename T>
UScriptStruct* GetSuperStaticStruct()
{
	if constexpr (std::is_void_v<T>) { return nullptr; }
	else { return T::StaticStruct(); }
}

// USTRUCT의 GENERATED_BODY()가 이걸로 바뀐다 (UCLASS의 DECLARE_CLASS에 해당)
// 구조체의 기본 접근 지정자는 public이므로 끝에 public:으로 돌려놓는다. 안 그러면 GENERATED_BODY() 뒤 멤버가 private이 된다.
#define DECLARE_STRUCT(StructName, SuperStructName)                     \
public:                                                                 \
	using Super = SuperStructName;                                      \
	using ThisClass = StructName;                                       \
	static UScriptStruct* StaticStruct()                                \
	{                                                                   \
		static UScriptStruct s;                                         \
		static bool bIsInit = false;                                    \
		if (!bIsInit)                                                   \
		{                                                               \
			bIsInit = true;                                             \
			s.Name = #StructName;                                       \
			s.Size = sizeof(StructName);                                \
			s.SuperStruct = GetSuperStaticStruct<Super>();              \
			StructName::RegisterProperties(&s);                         \
		}                                                               \
		return &s;                                                      \
	}                                                                   \
public:

//보류
inline void CopyProperties(UObject* Src, UObject* Dst, UClass* FromClass)
{
	for (UClass* c = FromClass; c; c = c->Super)
		for (const FProperty* p : c->Properties)
			memcpy((char*)Dst + p->Offset, (char*)Src + p->Offset, p->ElementSize);
}

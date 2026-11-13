#pragma once

#include "Property.h"
#include "Object.h"
#include <type_traits>
#include <utility>

using ClassConstructor = UObject * (*)(void* Memory);

class UClass : public UObject
{
public:
	UClass();

	FString Name;
	UClass* Super = nullptr;
	uint64 ClassSize = 0;
	ClassConstructor Constructor = nullptr;

	TArray<FProperty> Properties;
	UObject* DefaultObject = nullptr;

	bool IsChildOf(const UClass* BaseClass) const;
	UObject* GetDefaultObject();

	inline const TArray<FProperty>& GetProperties() const { return Properties; }

	template<class T>
	T* GetDefaultObject()
	{
		return CastChecked<T>(GetDefaultObject());
	}

	template <typename T>
	void AddProperty(const FString& InName, uint64 InOffset, uint32 InFlags = CPF_Default)
	{
		UClass* ObjClass = nullptr;
		if constexpr (std::is_pointer_v<T> &&
			std::is_base_of_v<UObject, std::remove_pointer_t<T>>)
		{
			ObjClass = std::remove_pointer_t<T>::StaticClass();
		}

		FProperty Property{ InName, GetPropertyType<T>(), InOffset, sizeof(T), ObjClass, InFlags };
		if constexpr (TArrayPropertyType<T>::Value)
		{
			using Element = typename TArrayPropertyType<T>::Element;
			static_assert(!TArrayPropertyType<Element>::Value, "Nested TArray properties are not supported");
			static_assert(!std::is_same_v<Element, bool>, "TArray<bool> is not supported by this TArray implementation");
			Property.ElementType = GetPropertyType<Element>();
			Property.bUnsignedElement = std::is_same_v<Element, uint32>;
			if constexpr (TStructPropertyType<Element>::Value)
				Property.StructFields = &TStructPropertyType<Element>::GetFields;
			if constexpr (std::is_pointer_v<Element> && std::is_base_of_v<UObject, std::remove_pointer_t<Element>>)
				Property.ElementClass = std::remove_pointer_t<Element>::StaticClass();
			Property.ArrayNum = [](const void* Ptr) { return static_cast<const T*>(Ptr)->Num(); };
			Property.ArraySetNum = [](void* Ptr, int32 Num) { static_cast<T*>(Ptr)->SetNum(Num); };
			Property.ArrayAt = [](void* Ptr, int32 Index) -> void* { return &(*static_cast<T*>(Ptr))[Index]; };
		}
		Properties.Add(std::move(Property));
	}

	template <typename T>
	void AddProperty(const FString& InName, uint64 InOffset, EPropertyType InType, uint32 InFlags = CPF_Default)
	{
		Properties.Add({ InName, InType, InOffset, sizeof(T), nullptr, InFlags });
	}
private:
	mutable int32 ClassUnique = 0;
	friend FName MakeUniqueObjectName(UObject*, const UClass*, FName);
};

//보류
inline void CopyProperties(UObject* Src, UObject* Dst, UClass* FromClass)
{
	for (UClass* c = FromClass; c; c = c->Super)
		for (const FProperty& p : c->Properties)
			memcpy((char*)Dst + p.Offset, (char*)Src + p.Offset, p.Size);
}

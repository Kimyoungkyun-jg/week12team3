#include "EnginePCH.h"
#include "Class.h"

#include "Object.h"

// ---- UStruct ----

UStruct::UStruct()
	:UObject(false)
{
	//HTR_LOG(Info, "Create RTTI UClass");
}

UStruct::~UStruct()
{
	for (FProperty* Property : Properties)
	{
		delete Property;
	}
}

FProperty* UStruct::FindPropertyByName(const FString& InName) const
{
	for (const UStruct* s = this; s; s = s->GetSuperStruct())
	{
		for (FProperty* Property : s->Properties)
		{
			if (Property->Name == InName)
			{
				return Property;
			}
		}
	}
	return nullptr;
}

// 타입별 처리는 각 FProperty 서브클래스가 한다 (Property.h)
void UStruct::SerializeTaggedProperties(FStructuredArchive::FSlot Slot, void* Data) const
{
	FArchive& Ar = Slot.GetUnderlyingArchive();
	FStructuredArchive::FRecord Record = Slot.EnterRecord();

	for (const UStruct* s = this; s; s = s->GetSuperStruct())
	{
		for (const FProperty* Property : s->Properties)
		{
			if (!Property->ShouldSerializeValue(Ar))
				continue;

			TOptional<FStructuredArchive::FSlot> Field = Record.TryEnterField(Property->Name.c_str(), true);
			if (!Field.IsSet()) continue;

			Property->SerializeItem(Field.GetValue(), Property->ContainerPtrToValuePtr(Data));
		}
	}
}

// ---- UScriptStruct ----

bool UScriptStruct::IsChildOf(const UScriptStruct* BaseStruct) const
{
	if (!BaseStruct) return false;
	for (const UScriptStruct* s = this; s; s = s->SuperStruct)
	{
		if (s == BaseStruct) return true;
	}
	return false;
}

// ---- UClass ----

const FFunction* UClass::FindFunction(const FString& InName) const
{
	for (const UClass* c = this; c; c = c->Super)
	{
		for (const FFunction& Function : c->Functions)
		{
			if (Function.Name == InName)
			{
				return &Function;
			}
		}
	}
	return nullptr;
}

UObject* UClass::CreateDefaultObject()
{
	if (ClassDefaultObject == nullptr)
	{
		UClass* ParentClass = Super;
		UObject* ParentDefaultObject = nullptr;
		if (ParentClass != nullptr)
		{
			ParentDefaultObject = ParentClass->GetDefaultObject(); // 부모 CDO 먼저 생성
		}

		// C++ 추상 부모는 CDO를 만들 수 없으므로 UE 조건을 완화한다.
		const bool bParentReady =
			(ParentDefaultObject != nullptr) ||
			(this == UObject::StaticClass()) ||
			(ParentClass != nullptr && ParentClass->Constructor == nullptr);

		if (bParentReady && ClassDefaultObject == nullptr)
		{
			// 추상 클래스는 생성자가 없음
			if (Constructor == nullptr)
			{
				return nullptr;
			}

			const FName CDOName = GetDefaultObjectName();
			const EObjectFlags CDOFlags = EObjectFlags::RF_Public | EObjectFlags::RF_ClassDefaultObject | EObjectFlags::RF_ArchetypeObject;

			UObject* NewClassDefaultObject = StaticAllocateObject(this, nullptr, CDOName, CDOFlags, false, nullptr);

			// 생성자 안에서 GetDefaultObject()가 재진입해도 무한 재귀하지 않도록 생성자 전에 대입
			ClassDefaultObject = NewClassDefaultObject;
			assert(ClassDefaultObject);

			Constructor(ClassDefaultObject);
			HashObject(ClassDefaultObject, this);
		}
	}
	return ClassDefaultObject;
}

void UClass::InternalCreateDefaultObjectWrapper() const
{
	const_cast<UClass*>(this)->CreateDefaultObject();
}

bool UClass::IsChildOf(const UClass* BaseClass) const
{
	if (!BaseClass) return false;
	for (const UClass* c = this; c; c = c->Super)
	{
		if (c == BaseClass) return true;
	}
	return false;
}

UObject* UClass::GetDefaultObject(bool bCreateIfNeeded) const
{
	if (ClassDefaultObject == nullptr && bCreateIfNeeded)
	{
		InternalCreateDefaultObjectWrapper();
	}
	return ClassDefaultObject;
}
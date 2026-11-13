#include "EnginePCH.h"
#include "Object.h"

#include "Core/EngineStatics.h"
#include "UObject/Class.h"

#include "Serialization/TypeSerializer.h"
#include "Serialization/MathSerialization.h"
#include "Serialization/Formatter/BinaryArchiveFormatter.h"
#include "Asset/AssetManager.h" //??? 의존성 제거 필요

namespace
{
	void SerializeStructJson(const FProperty& Property, void* StructPtr, json& Value, bool bIsLoading)
	{
		if (bIsLoading && !Value.is_object()) return;
		if (!bIsLoading) Value = json::object();
		int32 FieldCount = 0;
		const FStructPropertyField* Fields = Property.StructFields(FieldCount);
		for (int32 Index = 0; Index < FieldCount; ++Index)
		{
			const FStructPropertyField& Field = Fields[Index];
			if (Field.Flags & CPF_Transient) continue;
			if (bIsLoading && !Value.contains(Field.Name)) continue;
			void* Ptr = static_cast<uint8*>(StructPtr) + Field.Offset;
			json& FieldValue = Value[Field.Name];
			switch (Field.Type)
			{
			case EPropertyType::Float:
				if (bIsLoading) *static_cast<float*>(Ptr) = FieldValue.get<float>();
				else FieldValue = *static_cast<float*>(Ptr);
				break;
			case EPropertyType::Int:
				if (Field.bUnsigned)
				{
					if (bIsLoading) *static_cast<uint32*>(Ptr) = FieldValue.get<uint32>();
					else FieldValue = *static_cast<uint32*>(Ptr);
				}
				else if (bIsLoading) *static_cast<int32*>(Ptr) = FieldValue.get<int32>();
				else FieldValue = *static_cast<int32*>(Ptr);
				break;
			case EPropertyType::Bool:
				if (bIsLoading) *static_cast<bool*>(Ptr) = FieldValue.get<bool>();
				else FieldValue = *static_cast<bool*>(Ptr);
				break;
			case EPropertyType::String:
				if (bIsLoading) *static_cast<FString*>(Ptr) = FieldValue.get<FString>();
				else FieldValue = *static_cast<FString*>(Ptr);
				break;
			case EPropertyType::Vector:
				if (bIsLoading) *static_cast<FVector*>(Ptr) = FieldValue.get<FVector>();
				else FieldValue = *static_cast<FVector*>(Ptr);
				break;
			case EPropertyType::Vector4:
			case EPropertyType::LinearColor:
				if (bIsLoading) *static_cast<FVector4*>(Ptr) = FieldValue.get<FVector4>();
				else FieldValue = *static_cast<FVector4*>(Ptr);
				break;
			case EPropertyType::Color:
				if (bIsLoading) static_cast<FColor*>(Ptr)->DWColor() = FieldValue.get<uint32>();
				else FieldValue = static_cast<FColor*>(Ptr)->DWColor();
				break;
			case EPropertyType::Rotator:
				if (bIsLoading) *static_cast<FRotator*>(Ptr) = FieldValue.get<FRotator>();
				else FieldValue = *static_cast<FRotator*>(Ptr);
				break;
			case EPropertyType::Transform:
				if (bIsLoading) *static_cast<FTransform*>(Ptr) = FieldValue.get<FTransform>();
				else FieldValue = *static_cast<FTransform*>(Ptr);
				break;
			default: break;
			}
		}
	}

	void SerializeStructStructured(const FProperty& Property, void* StructPtr, FStructuredArchive::FSlot Slot)
	{
		FStructuredArchive::FRecord Record = Slot.EnterRecord();
		int32 FieldCount = 0;
		const FStructPropertyField* Fields = Property.StructFields(FieldCount);
		for (int32 Index = 0; Index < FieldCount; ++Index)
		{
			const FStructPropertyField& Field = Fields[Index];
			if (Field.Flags & CPF_Transient) continue;
			TOptional<FStructuredArchive::FSlot> FieldSlot = Record.TryEnterField(Field.Name, true);
			if (!FieldSlot.IsSet()) continue;
			void* Ptr = static_cast<uint8*>(StructPtr) + Field.Offset;
			switch (Field.Type)
			{
			case EPropertyType::Float: FieldSlot.GetValue() << *static_cast<float*>(Ptr); break;
			case EPropertyType::Int:
				if (Field.bUnsigned) FieldSlot.GetValue() << *static_cast<uint32*>(Ptr);
				else FieldSlot.GetValue() << *static_cast<int32*>(Ptr);
				break;
			case EPropertyType::Bool: FieldSlot.GetValue() << *static_cast<bool*>(Ptr); break;
			case EPropertyType::String: FieldSlot.GetValue() << *static_cast<FString*>(Ptr); break;
			case EPropertyType::Vector: FieldSlot.GetValue() << *static_cast<FVector*>(Ptr); break;
			case EPropertyType::Vector4:
			case EPropertyType::LinearColor: FieldSlot.GetValue() << *static_cast<FVector4*>(Ptr); break;
			case EPropertyType::Color: FieldSlot.GetValue() << *static_cast<FColor*>(Ptr); break;
			case EPropertyType::Rotator: FieldSlot.GetValue() << *static_cast<FRotator*>(Ptr); break;
			case EPropertyType::Transform: FieldSlot.GetValue() << *static_cast<FTransform*>(Ptr); break;
			default: break;
			}
		}
	}

	void SerializeArrayJson(UObject* Owner, const FProperty& Property, void* ArrayPtr, json& Field, bool bIsLoading)
	{
		if (bIsLoading)
		{
			if (!Field.is_array()) return;
			Property.ArraySetNum(ArrayPtr, static_cast<int32>(std::min<size_t>(Field.size(), 1000000)));
		}
		else Field = json::array();

		const int32 Num = Property.ArrayNum(ArrayPtr);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			void* Element = Property.ArrayAt(ArrayPtr, Index);
			json Value = bIsLoading ? Field[Index] : json();
			switch (Property.ElementType)
			{
			case EPropertyType::Struct:
				SerializeStructJson(Property, Element, Value, bIsLoading);
				break;
			case EPropertyType::Float:
				if (bIsLoading) *static_cast<float*>(Element) = Value.get<float>();
				else Value = *static_cast<float*>(Element);
				break;
			case EPropertyType::Int:
				if (Property.bUnsignedElement)
				{
					if (bIsLoading) *static_cast<uint32*>(Element) = Value.get<uint32>();
					else Value = *static_cast<uint32*>(Element);
				}
				else
				{
					if (bIsLoading) *static_cast<int32*>(Element) = Value.get<int32>();
					else Value = *static_cast<int32*>(Element);
				}
				break;
			case EPropertyType::String:
				if (bIsLoading) *static_cast<FString*>(Element) = Value.get<FString>();
				else Value = *static_cast<FString*>(Element);
				break;
			case EPropertyType::Vector:
				if (bIsLoading) *static_cast<FVector*>(Element) = Value.get<FVector>();
				else Value = *static_cast<FVector*>(Element);
				break;
			case EPropertyType::Vector4:
			case EPropertyType::LinearColor:
				if (bIsLoading) *static_cast<FVector4*>(Element) = Value.get<FVector4>();
				else Value = *static_cast<FVector4*>(Element);
				break;
			case EPropertyType::Color:
				if (bIsLoading) static_cast<FColor*>(Element)->DWColor() = Value.get<uint32>();
				else Value = static_cast<FColor*>(Element)->DWColor();
				break;
			case EPropertyType::Rotator:
				if (bIsLoading) *static_cast<FRotator*>(Element) = Value.get<FRotator>();
				else Value = *static_cast<FRotator*>(Element);
				break;
			case EPropertyType::Transform:
				if (bIsLoading) *static_cast<FTransform*>(Element) = Value.get<FTransform>();
				else Value = *static_cast<FTransform*>(Element);
				break;
			case EPropertyType::Object:
			{
				UObject*& Ref = *static_cast<UObject**>(Element);
				if (Property.HasAnyFlags(CPF_Instanced))
				{
					if (bIsLoading)
					{
						Ref = nullptr;
						if (!Value.is_object() || !Value.contains("Class") || !Value["Class"].is_string()) break;
						UClass* Class = FindClass(Value["Class"].get<FString>());
						if (!Class || !Class->Constructor || (Property.ElementClass && !Class->IsChildOf(Property.ElementClass))) break;
						Ref = NewObject<UObject>(Owner, Class);
						if (Value.contains("Properties") && Value["Properties"].is_object())
						{
							json Properties = Value["Properties"];
							Ref->Serialize(Properties, true);
						}
					}
					else if (Ref)
					{
					json Properties = json::object();
						Ref->Serialize(Properties, false);
						Value = { { "Class", Ref->GetClass()->Name }, { "Properties", std::move(Properties) } };
					}
				}
				else if (bIsLoading)
				{
					Ref = nullptr;
					if (Value.is_string())
					{
						URenderAsset* Asset = UAssetManager::GetAssetByPath<URenderAsset>(Value.get<FString>());
						if (Asset && (!Property.ElementClass || Asset->IsA(Property.ElementClass))) Ref = Asset;
					}
				}
				else
				{
					URenderAsset* Asset = Cast<URenderAsset>(Ref);
					if (Asset && !Asset->GetPath().empty()) Value = Asset->GetPath();
				}
				break;
			}
			default: break;
			}
			if (!bIsLoading) Field.push_back(std::move(Value));
		}
	}

	void SerializeArrayStructured(UObject* Owner, const FProperty& Property, void* ArrayPtr, FStructuredArchive::FSlot Slot)
	{
		FArchive& Ar = Slot.GetUnderlyingArchive();
		int32 Num = Property.ArrayNum(ArrayPtr);
		FStructuredArchive::FArray Array = Slot.EnterArray(Num);
		if (Ar.IsLoading()) Property.ArraySetNum(ArrayPtr, std::clamp(Num, 0, 1000000));
		for (int32 Index = 0; Index < Property.ArrayNum(ArrayPtr); ++Index)
		{
			FStructuredArchive::FSlot ElementSlot = Array.EnterElement();
			void* Element = Property.ArrayAt(ArrayPtr, Index);
			switch (Property.ElementType)
			{
			case EPropertyType::Struct: SerializeStructStructured(Property, Element, ElementSlot); break;
			case EPropertyType::Float: ElementSlot << *static_cast<float*>(Element); break;
			case EPropertyType::Int:
				if (Property.bUnsignedElement) ElementSlot << *static_cast<uint32*>(Element);
				else ElementSlot << *static_cast<int32*>(Element);
				break;
			case EPropertyType::String: ElementSlot << *static_cast<FString*>(Element); break;
			case EPropertyType::Vector: ElementSlot << *static_cast<FVector*>(Element); break;
			case EPropertyType::Vector4:
			case EPropertyType::LinearColor: ElementSlot << *static_cast<FVector4*>(Element); break;
			case EPropertyType::Color: ElementSlot << *static_cast<FColor*>(Element); break;
			case EPropertyType::Rotator: ElementSlot << *static_cast<FRotator*>(Element); break;
			case EPropertyType::Transform: ElementSlot << *static_cast<FTransform*>(Element); break;
			case EPropertyType::Object:
			{
				UObject*& Ref = *static_cast<UObject**>(Element);
				if (Property.HasAnyFlags(CPF_Instanced) && !Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
				{
					FStructuredArchive::FRecord Item = ElementSlot.EnterRecord();
					FString ClassName = Ref ? Ref->GetClass()->Name : FString();
					Item << SA_VALUE("Class", ClassName);
					if (Ar.IsLoading())
					{
						UClass* Class = FindClass(ClassName);
						Ref = Class && Class->Constructor && (!Property.ElementClass || Class->IsChildOf(Property.ElementClass))
							? NewObject<UObject>(Owner, Class) : nullptr;
					}
					if (Ref) Ref->Serialize(Item.EnterRecord("Value"));
				}
				else
				{
					UObject* Previous = Ref;
					ElementSlot << Ref;
					if (Ar.IsLoading() && Ref && Property.ElementClass && !Ref->IsA(Property.ElementClass)) Ref = Previous;
				}
				break;
			}
			default: { UObject* Null = nullptr; ElementSlot << Null; } break;
			}
		}
	}
}



TArray<UObject*> GUObjectArray;

UObject::UObject()
{
	ObjectUUID = FEngineStatics::GetUUID();
	InternalIndex = GUObjectArray.Num();
	GUObjectArray.Add(this);
}

UObject::UObject(bool bRegister)
{
	bIsRegistered = bRegister;
}

UObject::~UObject()
{
	if (bIsRegistered)
	{
		if (ClassPrivate)
		{
			UnhashObject(this, ClassPrivate);
		}
		int32 LastIndex = GUObjectArray.Num() - 1;
		if (InternalIndex != LastIndex)
		{
			UObject* MovedObject = GUObjectArray[LastIndex];
			GUObjectArray[InternalIndex] = MovedObject;
			MovedObject->InternalIndex = InternalIndex; // 옮겨간 오브젝트 인덱스 갱신
		}
		GUObjectArray.RemoveLast();
	}
}

UClass* UObject::StaticClass()
{
	static UClass c;
	static bool bIsInit = false;
	if (!bIsInit)
	{
		c.Name = "Object";
		c.Super = nullptr;
		c.ClassSize = sizeof(UObject);
		c.Constructor = [](void* Memory) -> UObject*
			{
				return ::new (Memory) UObject();
			};
		bIsInit = true;
	}
	return &c;
}

bool UObject::IsA(const UClass* Class) const
{
	const UClass* ThisClass = GetClass();
	return ThisClass && ThisClass->IsChildOf(Class);
}

void UObject::Serialize(json& Handle, bool bIsLoading)
{
	for (UClass* c = GetClass(); c; c = c->Super)
	{
		for (FProperty Property : c->GetProperties())
		{
			if (Property.HasAnyFlags(CPF_Transient))
				continue;

			if (bIsLoading && !Handle.contains(Property.Name))
				continue;

			void* Ptr = reinterpret_cast<uint8*>(this) + Property.Offset;

			switch (Property.Type)
			{
			case EPropertyType::Array:
				SerializeArrayJson(this, Property, Ptr, Handle[Property.Name], bIsLoading);
				break;
			case EPropertyType::Float:
			{
				float& Value = *static_cast<float*>(Ptr);
				if (bIsLoading) Value = Handle[Property.Name].get<float>();
				else Handle[Property.Name] = Value;
				break;
			}
			case EPropertyType::Int:
			{
				int32& Value = *static_cast<int32*>(Ptr);
				if (bIsLoading) Value = Handle[Property.Name].get<int32>();
				else Handle[Property.Name] = Value;
				break;
			}
			case EPropertyType::String:
			{
				FString& Value = *static_cast<FString*>(Ptr);
				if (bIsLoading) Value = Handle[Property.Name].get<std::string>();
				else Handle[Property.Name] = Value;
				break;
			}
			case EPropertyType::Bool:
			{
				bool& Value = *static_cast<bool*>(Ptr);
				if (bIsLoading) Value = Handle[Property.Name].get<bool>();
				else Handle[Property.Name] = Value;
				break;
			}
			case EPropertyType::Vector:
			{
				FVector& Value = *static_cast<FVector*>(Ptr);
				if (bIsLoading) Value = Handle[Property.Name].get<FVector>();
				else Handle[Property.Name] = Value;
				break;
			}
			case EPropertyType::Vector4:
			{
				FVector4& Value = *static_cast<FVector4*>(Ptr);
				if (bIsLoading) Value = Handle[Property.Name].get<FVector4>();
				else Handle[Property.Name] = Value;
				break;
			}
			case EPropertyType::Color:
			{
				// FColor(4바이트)는 RGBA를 묶은 uint32로 저장한다
				FColor& Value = *static_cast<FColor*>(Ptr);
				if (bIsLoading) Value.DWColor() = Handle[Property.Name].get<uint32>();
				else Handle[Property.Name] = Value.DWColor();
				break;
			}
			case EPropertyType::LinearColor:
			{
				FVector4& Value = *static_cast<FVector4*>(Ptr);
				if (bIsLoading) Value = Handle[Property.Name].get<FVector4>();
				else Handle[Property.Name] = Value;
				break;
			}
			case EPropertyType::Transform:
			{
				FTransform& Value = *static_cast<FTransform*>(Ptr);
				if (bIsLoading) Value = Handle[Property.Name].get<FTransform>();
				else Handle[Property.Name] = Value;
				break;
			}
			case EPropertyType::Object:
			{
				UObject*& Value = *static_cast<UObject**>(Ptr);
				if (Property.HasAnyFlags(CPF_Instanced))
				{
					json& Field = Handle[Property.Name];
					if (bIsLoading)
					{
						Value = nullptr;
						if (Field.is_object() && Field.contains("Class") && Field["Class"].is_string())
						{
							UClass* Class = FindClass(Field["Class"].get<FString>());
							if (Class && Class->Constructor && (!Property.Class || Class->IsChildOf(Property.Class)))
							{
								Value = NewObject<UObject>(this, Class);
								if (Field.contains("Properties") && Field["Properties"].is_object())
								{
									json Properties = Field["Properties"];
									Value->Serialize(Properties, true);
								}
							}
						}
					}
					else if (Value)
					{
						json Properties = json::object();
						Value->Serialize(Properties, false);
						Field = { { "Class", Value->GetClass()->Name }, { "Properties", std::move(Properties) } };
					}
					else Field = nullptr;
					break;
				}

				if (bIsLoading)
				{
					// null로 저장된 건 "되찾을 수 없는 값"이었으므로 기본값을 유지한다
					if (Handle[Property.Name].is_null())
					{
						break;
					}

					const FString AssetPath = Handle[Property.Name].get<FString>();
					URenderAsset* Asset = nullptr;

					if (Property.Class && Property.Class->IsChildOf(UStaticMesh::StaticClass()))
					{
						Asset = UAssetManager::GetAssetByPath<UStaticMesh>(AssetPath);
					}
					else
					{
						Asset = UAssetManager::GetAssetByPath<URenderAsset>(AssetPath);
					}

					// 못 찾으면 생성자가 넣어둔 기본값을 그대로 둔다.
					// (파일 하나가 없어져도 씬은 열리고, 액터가 사라지지 않고 기본 모양으로 보인다)
					if (!Asset || (Property.Class && !Asset->IsA(Property.Class)))
					{
						HTR_LOG(Warning, "Load: asset '{}' not found for {}, keeping default", AssetPath, Property.Name);
						break;
					}

					Value = Asset;
				}
				else
				{
					// 경로가 없는 에셋(런타임에 만든 인스턴스 등)은 되찾을 방법이 없으니 null로 쓴다
					URenderAsset* Asset = Cast<URenderAsset>(Value);
					if (Asset && !Asset->GetPath().empty())
						Handle[Property.Name] = Asset->GetPath();
					else
						Handle[Property.Name] = nullptr;
				}
				break;
			}
			default:
				break;
			}
		}
	}
}

void UObject::Serialize(FArchive& Ar)
{
	FBinaryArchiveFormatter Formatter(Ar);
	FStructuredArchive StructuredAr(Formatter);
	Serialize(StructuredAr.Open().EnterRecord());
	StructuredAr.Close();
}

void UObject::Serialize(FStructuredArchive::FRecord Record)
{
	SerializeScriptProperties(Record.EnterField("Properties"));
}

void UObject::SerializeScriptProperties(FStructuredArchive::FSlot Slot)
{
	FStructuredArchive::FRecord Properties = Slot.EnterRecord();

	const bool bIsLoading = Properties.GetUnderlyingArchive().IsLoading();

	for (UClass* c = GetClass(); c; c = c->Super)
	{
		for (const FProperty& Property : c->GetProperties())
		{
			if (Property.HasAnyFlags(CPF_Transient))
				continue;

			TOptional<FStructuredArchive::FSlot> Slot = Properties.TryEnterField(Property.Name.c_str(), true);
			if (!Slot.IsSet()) continue;

			void* Ptr = reinterpret_cast<uint8*>(this) + Property.Offset;
			FStructuredArchive::FSlot Value = Slot.GetValue();

			switch (Property.Type)
			{
			case EPropertyType::Array:     SerializeArrayStructured(this, Property, Ptr, Value); break;
			case EPropertyType::Float:     Value << *static_cast<float*>(Ptr); break;
			case EPropertyType::Int:       Value << *static_cast<int32*>(Ptr); break;
			case EPropertyType::String:    Value << *static_cast<FString*>(Ptr); break;
			case EPropertyType::Bool:      Value << *static_cast<bool*>(Ptr); break;
			case EPropertyType::Vector:    Value << *static_cast<FVector*>(Ptr); break;
			case EPropertyType::Vector4:
			case EPropertyType::LinearColor: Value << *static_cast<FVector4*>(Ptr); break;
			case EPropertyType::Color:     Value << *static_cast<FColor*>(Ptr); break;
			case EPropertyType::Rotator:   Value << *static_cast<FRotator*>(Ptr); break;
			case EPropertyType::Transform: Value << *static_cast<FTransform*>(Ptr); break;
			case EPropertyType::Object:
			{
				UObject*& Ref = *static_cast<UObject**>(Ptr);
				if (Property.HasAnyFlags(CPF_Instanced) && !Properties.GetUnderlyingArchive().HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
				{
					FStructuredArchive::FRecord Item = Value.EnterRecord();
					FString ClassName = Ref ? Ref->GetClass()->Name : FString();
					Item << SA_VALUE("Class", ClassName);
					if (bIsLoading)
					{
						UClass* Class = FindClass(ClassName);
						Ref = Class && Class->Constructor && (!Property.Class || Class->IsChildOf(Property.Class))
							? NewObject<UObject>(this, Class) : nullptr;
					}
					if (Ref) Ref->Serialize(Item.EnterRecord("Value"));
					break;
				}
				UObject* Previous = Ref;
				Value << Ref;

				// 불러온 객체가 프로퍼티가 기대하는 클래스가 아니면 되돌린다 (기존 Asset->IsA(Property.Class) 검사)
				if (bIsLoading && Ref && Property.Class && !Ref->IsA(Property.Class))
				{
					HTR_LOG(Warning, "Load: '{}' is not a {}, keeping default", Property.Name, Property.Class->Name);
					Ref = Previous;
				}
				break;
			}
			default:
				// 슬롯을 열었으면 반드시 값을 써야 한다. 모르는 타입은 저장 시 null로 둔다.
				HTR_LOG(Warning, "Serialize: unsupported property type for '{}'", Property.Name);
				{ UObject* Null = nullptr; Value << Null; }
				break;
			}
		}
	}
}




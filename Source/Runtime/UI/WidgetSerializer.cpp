#include "EnginePCH.h"
#include "UI/WidgetSerializer.h"

#include "UObject/UObjectHash.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/MathSerialization.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"

#include <fstream>

namespace
{
	constexpr int32 WidgetAssetVersion = 2;   // 2: FStructuredArchive 형식 (벡터·색이 {X,Y,Z,W} / {R,G,B,A} 레코드)
}

void FWidgetSerializer::SaveWidget(FStructuredArchive::FRecord Record, UWidget* Widget)
{
	FString ClassName = Widget->GetClass()->Name;
	Record << SA_VALUE("Class", ClassName);
	Widget->Serialize(Record);   // "Properties", 패널이면 "Children"까지
}

UWidget* FWidgetSerializer::LoadWidget(FStructuredArchive::FRecord Record)
{
	FString ClassName;
	Record << SA_VALUE("Class", ClassName);   // 없으면 빈 문자열

	UClass* Class = FindClass(ClassName);
	if (!Class || !Class->IsChildOf(UWidget::StaticClass()) || !Class->Constructor)
	{
		HTR_LOG(Warning, "Unknown widget class: '{}'", ClassName);
		return nullptr;
	}

	UWidget* Widget = NewObject<UWidget>(nullptr, Class);
	Widget->Serialize(Record);
	Widget->OnLoaded();
	return Widget;
}

bool FWidgetSerializer::SaveAssetToBytes(FWidgetAssetData& Asset, TArray<uint8>& OutBytes)
{
	FMemoryWriter Writer(OutBytes);
	Writer.SetIsPersistent(true);   // 파일에는 Transient를 쓰지 않는다
	{
		// 선언 순서 = 소멸 역순: Ar(Close) → Formatter(dump → Writer)
		FJsonArchiveOutputFormatter Formatter(Writer);
		FStructuredArchive Ar(Formatter);

		FStructuredArchive::FRecord File = Ar.Open().EnterRecord();
		int32 Version = WidgetAssetVersion;
		File << SA_VALUE("Version", Version);
		File << SA_VALUE("ParentClass", Asset.ParentClass);
		File << SA_VALUE("DesignSize", Asset.DesignSize);
		if (Asset.Root)
			SaveWidget(File.EnterRecord("Root"), Asset.Root);

		Ar.Close();
	}
	return !Writer.IsError();
}

bool FWidgetSerializer::LoadAssetFromBytes(const TArray<uint8>& Bytes, FWidgetAssetData& OutAsset)
{
	FMemoryReader Reader(Bytes, true);
	FJsonArchiveInputFormatter Formatter(Reader);
	if (Reader.IsError())
	{
		HTR_LOG(Error, "Load: invalid widget data");
		return false;
	}

	FStructuredArchive Ar(Formatter);
	FStructuredArchive::FRecord File = Ar.Open().EnterRecord();

	int32 Version = 0;
	File << SA_VALUE("Version", Version);
	if (Version != WidgetAssetVersion)
	{
		HTR_LOG(Error, "Load: widget version {} is not supported (expected {})", Version, WidgetAssetVersion);
		return false;
	}

	File << SA_VALUE("ParentClass", OutAsset.ParentClass);
	File << SA_VALUE("DesignSize", OutAsset.DesignSize);

	UWidget* Root = LoadWidget(File.EnterRecord("Root"));
	OutAsset.Root = Cast<UPanelWidget>(Root);
	if (Root && !OutAsset.Root)
	{
		HTR_LOG(Warning, "Widget asset root must be a panel");
		delete Root;
	}
	Ar.Close();

	return !Reader.IsError();
}

bool FWidgetSerializer::SaveAsset(FWidgetAssetData& Asset, const FString& Path)
{
	TArray<uint8> Bytes;
	if (!SaveAssetToBytes(Asset, Bytes))
		return false;

	std::ofstream File(Path, std::ios::binary);
	if (!File.is_open())
		return false;
	File.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
	return !File.fail();
}

bool FWidgetSerializer::ReadFileBytes(const FString& Path, TArray<uint8>& OutBytes)
{
	std::ifstream File(Path, std::ios::binary | std::ios::ate);
	if (!File.is_open())
		return false;

	OutBytes.SetNum(static_cast<int32>(File.tellg()));
	File.seekg(0);
	File.read(reinterpret_cast<char*>(OutBytes.GetData()), OutBytes.Num());
	return !File.fail();
}

TMap<FString, TArray<uint8>>& FWidgetAssetCache::GetMap()
{
	static TMap<FString, TArray<uint8>> Map;
	return Map;
}

const TArray<uint8>* FWidgetAssetCache::Find(const FString& Path)
{
	if (const TArray<uint8>* Cached = GetMap().Find(Path))
		return Cached;

	TArray<uint8> Bytes;
	if (!FWidgetSerializer::ReadFileBytes(Path, Bytes))
	{
		HTR_LOG(Error, "Cannot open widget asset: {}", Path);
		return nullptr;
	}
	GetMap().Add(Path, std::move(Bytes));
	return GetMap().Find(Path);
}

void FWidgetAssetCache::Invalidate(const FString& Path)
{
	GetMap().Remove(Path);
}

UUserWidget* CreateWidget(UWorld* World, const FString& AssetPath)
{
	const TArray<uint8>* Bytes = FWidgetAssetCache::Find(AssetPath);
	if (!Bytes) return nullptr;

	FWidgetAssetData Asset;
	if (!FWidgetSerializer::LoadAssetFromBytes(*Bytes, Asset))
	{
		HTR_LOG(Error, "Invalid widget asset: {}", AssetPath);
		return nullptr;
	}

	UClass* Class = FindClass(Asset.ParentClass);
	if (!Class || !Class->IsChildOf(UUserWidget::StaticClass()))
		Class = UUserWidget::StaticClass();

	UUserWidget* Widget = NewObject<UUserWidget>(nullptr, Class);
	Widget->SetOwningWorld(World);
	Widget->AssetPath = AssetPath;

	if (!Asset.Root)
		HTR_LOG(Warning, "Widget asset root must be a panel: {}", AssetPath);
	Widget->SetRootWidget(Asset.Root);
	return Widget;
}

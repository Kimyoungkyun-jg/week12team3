#pragma once

#include "UI/UserWidget.h"
#include "Serialization/StructuredArchive.h"

// .widget 파일 하나의 내용 (UE: UWidgetBlueprint의 ParentClass + WidgetTree)
struct FWidgetAssetData
{
	FString ParentClass = "UUserWidget";
	FVector2 DesignSize = FVector2(1920.0f, 1080.0f);
	UPanelWidget* Root = nullptr;   // 불러오면 새로 만든다. 지우는 건 받은 쪽 책임
};

namespace FWidgetSerializer
{
	// 위젯 하나와 그 자손을 { Class, Properties, Children } 레코드로 쓴다.
	void SaveWidget(FStructuredArchive::FRecord Record, UWidget* Widget);
	// 레코드에서 위젯 트리를 새로 만든다. 클래스를 못 찾으면 nullptr.
	UWidget* LoadWidget(FStructuredArchive::FRecord Record);

	// .widget 파일 전체 (FJsonArchive의 SaveWorldToBytes / LoadWorldFromBytes와 같은 방식)
	bool SaveAssetToBytes(FWidgetAssetData& Asset, TArray<uint8>& OutBytes);
	bool LoadAssetFromBytes(const TArray<uint8>& Bytes, FWidgetAssetData& OutAsset);
	bool SaveAsset(FWidgetAssetData& Asset, const FString& Path);
	bool ReadFileBytes(const FString& Path, TArray<uint8>& OutBytes);
}

// .widget 파일을 경로당 한 번만 읽어 둔다. 여기 든 바이트가 "에셋(템플릿)"이다.
class FWidgetAssetCache
{
public:
	static const TArray<uint8>* Find(const FString& Path);
	static void Invalidate(const FString& Path);   // 에디터가 저장한 뒤 부른다

private:
	static TMap<FString, TArray<uint8>>& GetMap();
};

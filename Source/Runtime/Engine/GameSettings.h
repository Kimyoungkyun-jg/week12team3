#pragma once

#include <string>

#include "Core/Types.h"
#include "Core/EngineString.h"
#include "Render/SceneRenderOptions.h"

// 패키징된 게임의 실행 설정. GameConfig/DefaultGame.ini에서 읽고, 파일이나 항목이 없으면 기본값을 쓴다.
struct FGameSettings
{
	FString StartupMap = "Assets/Scenes/Default.scene";   // [Game] GameDefaultMap
	FString SkyPanorama = "Resources/SkySphere/UnrealDefaultSky.hdr";

	std::wstring WindowTitle = L"Hitori";
	uint32 Width = 1600;
	uint32 Height = 900;
	bool bBorderless = false;
	bool bVSync = true;

	FSceneRenderOptions RenderOptions;

	FGameSettings()
	{
		// 캐스케이드 한 장 / Spot·Point 한 장의 최대 크기 (테두리 포함)
		RenderOptions.DirectionalShadowMapSize = 2048;
		RenderOptions.PointShadowMapSize = 2048;
		RenderOptions.SpotShadowMapSize = 2048;
	}

	static FGameSettings Load(const FString& Path);
};

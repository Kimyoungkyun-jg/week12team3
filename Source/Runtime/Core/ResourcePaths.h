#pragma once

// 파일 경로 상수 (TEAM5 규칙을 따른다).
// Resources = 엔진·에디터 내장 파일(셰이더, 폰트, 아이콘, 스플래시, 기본 하늘). 스캔하지 않고 여기 적힌 경로로만 불러온다.
//             Assets 폴더가 없어도 엔진이 떠야 하므로 ResourcePaths는 Assets를 가리키면 안 된다.
// Assets    = 사용자 콘텐츠(모델, 텍스처, 씬, 스크립트). 시작할 때 통째로 스캔하고 Content Drawer에 보인다.
// Settings  = 사용자별 에디터 설정. 저장소에서 제외한다.
// Saved     = 실행 로그(Saved/Logs), 작업 백업과 기타 임시 산출물. 저장소에서 제외한다.
// 에디터 전용 경로는 Editor/HitoriEd/EditorResourcePaths.h에 둔다.
namespace ResourcePaths
{
	constexpr const char* ShaderFolder = "Resources/Shader";

	constexpr const char* SplashImage = "Resources/Splash/Splash.png";

	// 엔진이 직접 쓰는 기본 폰트 (에디터 UUID 표시 등). 콘텐츠용 폰트는 Assets/Fonts에 있다.
	constexpr const char* DefaultFont = "Resources/Fonts/Pretendard.json";
	constexpr const char* DefaultFontAtlas = "Resources/Fonts/Pretendard.png";

	constexpr const char* DefaultSkyPanorama = "Resources/SkySphere/UnrealDefaultSky.hdr";
	constexpr const char* DefaultParticleTexture = "Resources/SubUV/StarParticle.png";

	// 라이트·안개·데칼·PlayerStart 액터의 에디터 빌보드 아이콘
	constexpr const char* DirectionalLightIcon = "Resources/Icons/Directional.png";
	constexpr const char* PointLightIcon = "Resources/Icons/Point.png";
	constexpr const char* SpotLightIcon = "Resources/Icons/Spot.png";
	constexpr const char* RectLightIcon = "Resources/Icons/Rect.png";
	constexpr const char* SkyLightIcon = "Resources/Icons/Sky.png";
	constexpr const char* FogIcon = "Resources/Icons/Fog.png";
	constexpr const char* DecalIcon = "Resources/Icons/Decal.png";
	constexpr const char* PlayerStartIcon = "Resources/Icons/Toolbar/Play.png";

	// Fake Spot Light가 바닥에 더하는 빛 모양과 빌보드
	constexpr const char* FakeSpotLightDecal = "Resources/Decal/FakeSpotLight/fake_spot_decal.png";
	constexpr const char* FakeSpotLightBillboard = "Resources/Decal/FakeSpotLight/fake_spot_billboard.png";
}

// 사용자 콘텐츠와 실행 설정의 폴더 경로
namespace ContentPaths
{
	constexpr const char* AssetRoot = "Assets";
	constexpr const char* SceneFolder = "Assets/Scenes";
	constexpr const char* ScriptFolder = "Assets/Scripts";
	constexpr const char* ScriptTemplate = "Assets/Scripts/template.lua";

	// 패키징된 게임의 실행 설정. 실행 파일 옆 GameConfig 폴더에서 읽는다.
	constexpr const char* GameConfigFile = "GameConfig/DefaultGame.ini";
}

#pragma once

// 뷰마다 켜고 끄는 표시 항목 (TEAM5 Show Flags 중 이 렌더러가 지원하는 것).
// 뷰포트 툴바의 Show 메뉴가 유일한 편집 위치이고, editor.ini에 뷰별로 저장된다.
struct FViewShowFlags
{
	bool bPrimitives = true;
	bool bFog = true;
	bool bDecals = true;
	bool bSkeletalMeshes = true;
	// 원근 뷰는 픽셀 셰이더 그리드, 직교 뷰는 라인 그리드로 그린다 (UE 방식).
	bool bGrid = true;
	bool bBounds = false;
	bool bCascadeBounds = false;
	bool bUUID = false;
	bool bFXAA = false;
	bool bVignette = true;
	bool bCameraOverlay = true;
};

// Show 메뉴 표시와 editor.ini 저장에 같이 쓰는 항목 표.
// Key는 ini 키 접미사(View0Show<Key>)이고 Group은 메뉴에서 묶는 구역 이름이다.
struct FViewShowFlagInfo
{
	const char* Key;
	const char* Label;
	const char* Group;
	bool FViewShowFlags::* Member;
};

inline constexpr FViewShowFlagInfo ViewShowFlagInfos[] = {
	{ "Primitives",    "Primitives",              "Common",       &FViewShowFlags::bPrimitives },
	{ "Fog",           "Exponential Height Fog",  "Common",       &FViewShowFlags::bFog },
	{ "Decals",        "Decals",                  "Common",       &FViewShowFlags::bDecals },
	{ "SkeletalMeshes", "Skeletal Meshes",        "Common",       &FViewShowFlags::bSkeletalMeshes },
	{ "Grid",          "Grid",                    "Editor",       &FViewShowFlags::bGrid },
	{ "Bounds",        "Bounding Box",            "Editor",       &FViewShowFlags::bBounds },
	{ "CascadeBounds", "Shadow Cascade Bounds",   "Editor",       &FViewShowFlags::bCascadeBounds },
	{ "UUID",          "Object UUID",             "Editor",       &FViewShowFlags::bUUID },
	{ "FXAA",          "Anti-Aliasing (FXAA)",    "Post Process", &FViewShowFlags::bFXAA },
	{ "Vignette",      "Vignette",                "Post Process", &FViewShowFlags::bVignette },
	{ "CameraOverlay", "Camera Fade / Letterbox", "Post Process", &FViewShowFlags::bCameraOverlay },
};

#pragma once

// 에디터 UI가 쓰는 파일 경로. 읽기 전용 리소스는 Resources, 실행 중 저장하는 파일은 Saved 아래에 둔다.
// 엔진 공용 경로는 Runtime/Core/ResourcePaths.h에 있다.
namespace EditorResourcePaths
{
	// ImGui 한글 UI 폰트
	constexpr const char* UIFont = "Resources/Fonts/HMKMRHD.TTF";

	// Content Drawer 아이콘
	constexpr const char* FolderIcon = "Resources/Icons/Folder.png";
	constexpr const char* FileIcon = "Resources/Icons/File.png";
	constexpr const char* VisibleIcon = "Resources/Icons/Visible.png";

	constexpr const char* ToolbarSaveIcon = "Resources/Icons/Toolbar/Save.png";
	constexpr const char* ToolbarPlayIcon = "Resources/Icons/Toolbar/Play.png";
	constexpr const char* ToolbarPauseIcon = "Resources/Icons/Toolbar/Pause.png";
	constexpr const char* ToolbarStepIcon = "Resources/Icons/Toolbar/Step.png";
	constexpr const char* ToolbarStopIcon = "Resources/Icons/Toolbar/Stop.png";
	constexpr const char* ToolbarEjectIcon = "Resources/Icons/Toolbar/Eject.png";

	// 스켈레탈 메시 뷰어와 Content Drawer 썸네일의 배경 하늘
	constexpr const char* AssetViewerSkyPanorama = "Resources/SkySphere/AssetViewerSky.hdr";

	// 리타깃 프로필은 사용자가 만드는 콘텐츠라 Assets 아래에 둔다.
	constexpr const char* RetargetProfileFolder = "Assets/RetargetProfiles";

	// 사용자별 설정은 프로젝트 루트 Settings에, 실행 로그는 Saved/Logs에 저장한다.
	constexpr const char* SettingsFolder = "Settings";
	constexpr const char* LogFolder = "Saved/Logs";
	constexpr const char* EditorSettingsFile = "Settings/editor.ini";
	constexpr const char* ImGuiLayoutFile = "Settings/imgui.ini";
	constexpr const char* EditorLogFile = "Saved/Logs/HitoriEditor.log";
	constexpr int EditorLogBackupCount = 10;
}

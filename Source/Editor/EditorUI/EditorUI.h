#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/LevelEditor/LevelEditorToolbar.h"

#include <functional>
#include "Asset/FbxImporter/FbxAssetData.h"

class FEditorUI
{
public:
	bool Init(bool bInUseDockSpace = true, bool bInPassthruCentralNode = false);
	void Tick(float DeltaTime);
	void OnRender();
	// 모든 패널과 기본 열림 상태를 만든 뒤, 첫 ImGui 프레임 전에 호출한다.
	void RegisterPanelSettings();
	// 경로를 직접 고르는 빈 창을 연다 (File 메뉴).
	void OpenFbxImportDialog();
	// 파일마다 임포트 창을 차례로 띄운다. Output이 비면 FBX 폴더, 있으면 그 폴더로 고정한다.
	void QueueFbxImports(const TArray<FString>& Sources, const FString& LockedOutput = {});
	// 기존 바이너리를 저장된 옵션으로 다시 만든다. Owner는 파일 이름의 기준이 되는 소유 FBX다.
	void QueueFbxReimport(const FString& Source, const FString& Output, const FFbxImportOptions& Options, const FString& Owner);
	// 파일 선택 창을 연다. 여러 파일을 고를 수 있고 결과는 Output 폴더로 고정된다.
	void BrowseFbxImportFiles(const FString& Output);
	// 다른 FBX를 골라 기존 바이너리에 덮어쓴다. 취소하면 아무것도 하지 않는다.
	void BrowseFbxReimportFile(const FString& Output, const FFbxImportOptions& Options, const FString& Owner);
	void SetFbxImportedCallback(std::function<void()> Callback) { OnFbxImported = std::move(Callback); }
	void SetFbxImportBlockedQuery(std::function<bool()> Query) { IsFbxImportBlocked = std::move(Query); }

	template <typename T>
	T* AddEditorPanel()
	{
		TUniquePtr<T> newPanel = MakeUnique<T>();
		T* Ret = newPanel.get();
		Ret->Init();
		Panels.Add(std::move(newPanel));

		return Ret;
	}

	FLevelEditorToolbar* GetToolbar() const { return Toolbar.get(); }

	void SetNewSceneCallback(std::function<void()> InCallback) { OnNewScene = InCallback; }
	void SetOpenSceneCallback(std::function<void()> InCallback) { OnOpenScene = InCallback; }
	void SetSaveSceneCallback(std::function<void()> InCallback) { OnSaveScene = InCallback; }
	void SetSaveSceneAsCallback(std::function<void()> InCallback) { OnSaveSceneAs = InCallback; }
	void SetPackageGameCallback(std::function<void()> InCallback) { OnPackageGame = InCallback; }
	// 패키징 중이면 true. Build 메뉴 항목을 비활성화하는 데 쓴다.
	void SetIsPackagingQuery(std::function<bool()> InQuery) { IsPackaging = InQuery; }

private:
	bool bUseDockSpace = true;
	bool bPassthruCentralNode = false;
	bool bLevelEditorOpen = true;
	bool bResetLevelLayout = false;

	void DrawMainMenuBar();
	void DrawFbxImportDialog();
	void BrowseFbxSource();
	void BrowseFbxOutputDirectory();
	void UpdateFbxOutputDirectory();

	struct FFbxImportRequest
	{
		FString Source;
		FString Output;
		FString Owner;
		FFbxImportOptions Options;
		bool bLockOutput = false;
		bool bReimport = false;
		bool bDeferred = false; // 메시 없는 FBX를 대상 Skeleton이 생길 때까지 한 번 미뤘다
	};
	void EnqueueFbxRequest(FFbxImportRequest Request);
	void ApplyFbxRequest(const FFbxImportRequest& Request, bool bKeepOptions);
	void AdvanceFbxQueue(bool bKeepOptions);
	void RunFbxImport();
	bool TryFbxImportWithLoadedSkeleton();
	void DrawFbxImportBody();
	void DrawFbxFailures();
	TArray<FFbxImportRequest> FbxQueue;   // 0번이 지금 창에 표시 중인 요청
	TArray<FString> FbxFailures;          // Import All 중 실패한 파일
	int32 FbxProcessedCount = 0;
	bool bFbxDialogActive = false;
	bool bFbxPopupOpen = true;            // 창의 X 버튼이 false로 만든다
	bool bFbxImporting = false;
	bool bFbxImportAll = false;
	bool bFbxImportTextDrawn = false;     // "Importing" 표시를 한 프레임 보여 준 뒤 실행한다
	std::function<bool()> IsFbxImportBlocked;

	char FbxSourcePath[2048]{};
	char FbxOutputDirectory[2048]{};
	bool bFbxOutputDirectoryEdited = false;
	FFbxImportOptions FbxOptions;
	FFbxImportResult FbxResult;
	std::function<void()> OnFbxImported;

	TArray<TUniquePtr<IEditorPanel>> Panels;
	TUniquePtr<FLevelEditorToolbar> Toolbar;

	std::function<void()> OnNewScene;
	std::function<void()> OnOpenScene;
	std::function<void()> OnSaveScene;
	std::function<void()> OnSaveSceneAs;
	std::function<void()> OnPackageGame;
	std::function<bool()> IsPackaging;
};

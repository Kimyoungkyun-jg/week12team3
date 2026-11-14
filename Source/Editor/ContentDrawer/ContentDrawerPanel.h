#pragma once
#include "Editor/HitoriEd/EditorResourcePaths.h"

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "GameFramework/Actor/SkeletalMeshActor.h"

#include <filesystem>
#include <functional>
#include <set>
#include <unordered_map>

namespace fs = std::filesystem;

class AStaticMeshActor;
class UMaterial;

class FContentDrawerPanel : public IEditorPanel
{
public:
	FContentDrawerPanel();
	virtual ~FContentDrawerPanel() override;
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Content Drawer"; }
	void RenderFolderTree(const fs::path& Path, fs::path& SelectedPath);
	void Release();
	void RequestThumbnailRefresh() { bRefreshThumbnails = true; AnimSequenceDisplayNames.clear(); }
	static ID3D11ShaderResourceView* GetMaterialThumbnailSRV(UMaterial* Material);
	static void InvalidateMaterialThumbnail(const FString& Path);

	// .widget 파일을 더블클릭하면 불린다 (UI 에디터가 연다).
	void SetOpenWidgetCallback(std::function<void(const FString&)> InCallback) { OnOpenWidget = std::move(InCallback); }
	void SetCreateWidgetCallback(std::function<void(const FString&)> InCallback) { OnCreateWidget = std::move(InCallback); }
	void SetCreateAnimGraphCallback(std::function<void(const FString&, const FString&)> InCallback) { OnCreateAnimGraph = std::move(InCallback); }
	void SetOpenAnimGraphCallback(std::function<void(const FString&)> InCallback) { OnOpenAnimGraph = std::move(InCallback); }
	void SetCreateBlendSpaceCallback(std::function<void(const FString&, bool)> InCallback) { OnCreateBlendSpace = std::move(InCallback); }
	void SetOpenBlendSpaceCallback(std::function<void(const FString&)> InCallback) { OnOpenBlendSpace = std::move(InCallback); }

	// .fbx 파일을 더블클릭 시
	void SetOpenSkeletalMeshCallback(std::function<void(const FString&)> InCallback) { OnOpenSkeletalMesh = std::move(InCallback); }
	void SetOpenAssetCallback(std::function<void(const FString&)> InCallback) { OnOpenAsset = std::move(InCallback); }
	void SetOpenAnimSequenceCallback(std::function<void(const FString&)> InCallback) { OnOpenAnimSequence = std::move(InCallback); }
	void SetImportFbxCallback(std::function<void(const FString&, bool)> InCallback) { OnImportFbx = std::move(InCallback); }
	void SetReimportFromFileCallback(std::function<void(const FString&)> InCallback) { OnReimportFromFile = std::move(InCallback); }
	// Import 버튼. 인자는 지금 보고 있는 폴더이며 바이너리가 여기에 만들어진다.
	void SetImportFbxFilesCallback(std::function<void(const FString&)> InCallback) { OnImportFbxFiles = std::move(InCallback); }

private:
	void OpenItem(const fs::path& Path);
	void RefreshFbxFolderCache();
	void DrawFbxBinaryMenu(const FString& Path);
	void RefreshSkeletonKeys();
	std::function<void(const FString&)> OnReimportFromFile;
	std::function<void(const FString&)> OnImportFbxFiles;
	// 매 프레임 파일을 열지 않도록 현재 폴더의 FBX 상태를 잠시 들고 있는다.
	fs::path FbxCachePath;
	double FbxCacheTime = 0.0;
	std::set<fs::path> HiddenFbxFiles;      // 같은 폴더에 바이너리가 있다
	std::set<fs::path> UnimportedFbxFiles;  // 어느 폴더에도 바이너리가 없다
	FString FbxMenuStatus;
	bool bFbxMenuHasSource = false;
	std::function<void(const FString&)> OnOpenAsset;
	std::function<void(const FString&)> OnOpenAnimSequence;
	std::function<void(const FString&, bool)> OnImportFbx;
	std::function<void(const FString&)> OnOpenWidget;
	std::function<void(const FString&)> OnCreateWidget;
	std::function<void(const FString&, const FString&)> OnCreateAnimGraph;   // (대상 경로, 스켈레톤 키)
	std::function<void(const FString&)> OnOpenAnimGraph;
	std::function<void(const FString&, bool)> OnCreateBlendSpace;
	std::function<void(const FString&)> OnOpenBlendSpace;
	std::function<void(const FString&)> OnOpenSkeletalMesh;
	char NewAssetName[64] = "NewWidget";
	char SearchText[256] = "";
	bool bOpenCreateWidget = false;
	bool bOpenCreateAnimGraph = false;
	FString NewAnimGraphSkeleton;          // 생성 모달에서 선택된 스켈레톤 키 (Assets 기준 상대 경로)
	TArray<FString> SkeletonKeys;          // 모달을 열 때 Assets에서 수집한 SKEL_*.fbxbin
	bool bOpenCreateBlendSpace = false;
	bool bNewBlendSpace2D = true;
	bool bOpenCreateAnimationAsset = false;
	bool bOpenCreateParticleSystem = false;
	bool bNewAnimationAssetMontage = true;

	fs::path AssetRootPath = "Assets";
	fs::path CurrentPath = "Assets";
	fs::path SelectedAssetPath;

	const FString FolderIconPath = EditorResourcePaths::FolderIcon;
	const FString FileIconPath = EditorResourcePaths::FileIcon;
	
	std::unordered_map<FString, TUniquePtr<FTexture2D>> ThumbnailCache;
	std::unordered_map<FString, FString> AnimSequenceDisplayNames;
	UWorld* ThumbnailWorld = nullptr;
	ASkeletalMeshActor* ThumbnailActor = nullptr;
	AStaticMeshActor* MaterialThumbnailActor = nullptr;
	TUniquePtr<class FSkyboxRenderer> ThumbnailSkybox;
	FEditorViewportClient ThumbnailClient;
	bool bStudioInitialized = false;
	bool bRefreshThumbnails = false;
	static FContentDrawerPanel* ActiveInstance;
	void InitThumbnailStudio();
	FTexture2D* GetOrCreateSkeletalMeshThumbnail(const FString& FbxPath);
	FTexture2D* GetOrCreateMaterialThumbnail(UMaterial* Material);
};

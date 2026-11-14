#include "EnginePCH.h"
// 아래 파일 스코프 헬퍼의 선언 (정의는 RefreshSkeletonKeys 위)
#include <filesystem>
namespace
{
FString SkeletonKeyFromPath(const std::filesystem::path& Path);
}
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"
#include "Editor/Animation/AnimSequenceDisplayName.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Asset/FbxImporter/FbxAssetLoader.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Text/Font.h"
#include "Editor/HitoriEd/EditorDragDrop.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Editor/HitoriEd/EditorEngine.h"

#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "Components/BillboardComponent.h"
#include "Components/SkeletalMeshComponent.h"

#include "Render/SkeletalMesh.h"
#include "Render/RenderCommand.h"
#include "Math/MathUtility.h"
#include "Components/LightComponent.h"
#include "Launch/LaunchEngineLoop.h"
#include "Render/Swapchain.h"
#include "Render/SkyboxRenderer.h"
#include <cctype>

FContentDrawerPanel::FContentDrawerPanel() = default;

void FContentDrawerPanel::Release()
{
	ThumbnailClient.SetWorld(nullptr);
	if (ThumbnailWorld)
	{
		ThumbnailWorld->ClearWorld();
		ThumbnailWorld->FlushPendingDestruction();
		if (GEngine)
		{
			GEngine->DestroyWorld(ThumbnailWorld);
		}
		ThumbnailWorld = nullptr;
	}
	ThumbnailActor = nullptr;
	ThumbnailSkybox.reset();
	ThumbnailCache.clear();
	bStudioInitialized = false;
}

FContentDrawerPanel::~FContentDrawerPanel()
{
	Release();
}

bool FContentDrawerPanel::Init()
{
	return true;
}

void FContentDrawerPanel::Tick(float DeltaTime)
{
	if (bRefreshThumbnails)
	{
		ThumbnailCache.clear();
		FbxCachePath.clear();
		bRefreshThumbnails = false;
	}
}

void FContentDrawerPanel::OnRender()
{
	if (!BeginPanel())
	{
		ImGui::End();
		return;
	}
	std::error_code Error;

	if (!fs::is_directory(CurrentPath, Error))
	{
		CurrentPath = AssetRootPath;
	}

	Error.clear();

	if (!fs::is_directory(AssetRootPath, Error))
	{
		ImGui::TextUnformatted("Asset folder is unavailable.");
		ImGui::End();
		return;
	}

	float treeViewWidth = EditorUIScale::Px(200.0f);
	ImGui::BeginChild("FolderTreeView", ImVec2(treeViewWidth, 0));
	{
		// 루트 노드
		if (ImGui::TreeNodeEx(AssetRootPath.string().c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | (AssetRootPath == CurrentPath ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_DrawLinesToNodes))
		{
			if (ImGui::IsItemClicked())
			{
				CurrentPath = AssetRootPath;
			}
			RenderFolderTree(AssetRootPath, CurrentPath);
			ImGui::TreePop();
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("AssetGridView");
	{
		ImGui::BeginDisabled(CurrentPath == AssetRootPath);
		if (ImGui::Button(".."))
		{
			CurrentPath = CurrentPath.parent_path();
			SelectedAssetPath.clear();
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Go to parent folder");
		ImGui::SameLine();
		if (ImGui::Button("Import") && OnImportFbxFiles)
		{
			OnImportFbxFiles(FbxPath::ToString(CurrentPath));
		}
		ImGui::SetItemTooltip("Import FBX files into this folder");
		ImGui::SameLine();
		ImGui::TextUnformatted(FbxPath::ToString(CurrentPath).c_str());
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		ImGui::InputTextWithHint("##AssetSearch", "Search files in this folder...", SearchText, sizeof(SearchText));
		ImGui::Separator();
		FString SearchQuery = SearchText;
		std::transform(SearchQuery.begin(), SearchQuery.end(), SearchQuery.begin(),
			[](unsigned char C) { return static_cast<char>(std::tolower(C)); });

		// 빈 곳 우클릭 (항목 위 우클릭은 제외)
		if (ImGui::BeginPopupContextWindow("##DrawerContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::MenuItem("Create Widget"))
			{
				strcpy_s(NewAssetName, "NewWidget");
				bOpenCreateWidget = true; // 메뉴 팝업 안에서 바로 다른 팝업을 열 수 없어 다음 줄에서 연다
			}
			if (ImGui::MenuItem("Create AnimGraph"))
			{
				strcpy_s(NewAssetName, "NewAnimGraph");
				NewAnimGraphSkeleton.clear();
				RefreshSkeletonKeys();
				bOpenCreateAnimGraph = true;
			}
			if (ImGui::MenuItem("Create BlendSpace 1D"))
			{
				strcpy_s(NewAssetName, "NewBlendSpace1D");
				bNewBlendSpace2D = false;
				bOpenCreateBlendSpace = true;
			}
			if (ImGui::MenuItem("Create BlendSpace 2D"))
			{
				strcpy_s(NewAssetName, "NewBlendSpace2D");
				bNewBlendSpace2D = true;
				bOpenCreateBlendSpace = true;
			}
			if (ImGui::MenuItem("Create Particle System"))
			{
				strcpy_s(NewAssetName, "NewParticleSystem");
				bOpenCreateParticleSystem = true;
			}
			if (ImGui::MenuItem("Create AnimMontage") && OnOpenAsset)
			{
				strcpy_s(NewAssetName, "NewMontage");
				bNewAnimationAssetMontage = true; bOpenCreateAnimationAsset = true;
			}
			if (ImGui::MenuItem("Create AimOffset") && OnOpenAsset)
			{
				strcpy_s(NewAssetName, "NewAimOffset");
				bNewAnimationAssetMontage = false; bOpenCreateAnimationAsset = true;
			}
			ImGui::EndPopup();
		}
		if (bOpenCreateWidget) { ImGui::OpenPopup("Create Widget"); bOpenCreateWidget = false; }
		if (bOpenCreateAnimGraph) { ImGui::OpenPopup("Create AnimGraph"); bOpenCreateAnimGraph = false; }
		if (bOpenCreateBlendSpace) { ImGui::OpenPopup("Create BlendSpace"); bOpenCreateBlendSpace = false; }
		if (bOpenCreateParticleSystem) { ImGui::OpenPopup("Create Particle System"); bOpenCreateParticleSystem = false; }
		if (bOpenCreateAnimationAsset) { ImGui::OpenPopup("Create Animation Asset"); bOpenCreateAnimationAsset = false; }
		if (ImGui::BeginPopupModal("Create Animation Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::InputText("Name", NewAssetName, sizeof(NewAssetName));
			const FString Name = NewAssetName;
			const fs::path Target = CurrentPath / (Name + (bNewAnimationAssetMontage ? ".animmontage" : ".aimoffset"));
			const bool bExists = fs::exists(Target);
			const bool bInvalid = Name.empty() || Name == "." || Name == ".." ||
				Name.find_first_of("\\/:*?\"<>|") != FString::npos;
			if (bExists) ImGui::TextUnformatted("An asset with this name already exists.");
			if (bInvalid) ImGui::TextUnformatted("Enter a valid asset name.");
			ImGui::BeginDisabled(bExists || bInvalid);
			if (ImGui::Button("Create") && OnOpenAsset)
			{
				OnOpenAsset(FbxPath::ToString(Target));
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopupModal("Create BlendSpace", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::InputText("Name", NewAssetName, sizeof(NewAssetName));
			const FString Name = NewAssetName;
			const fs::path Target = CurrentPath / (FString(NewAssetName) + ".blendspace");
			const bool bExists = fs::exists(Target);
			const bool bInvalid = Name.empty() || Name == "." || Name == ".." ||
				Name.find_first_of("\\/:*?\"<>|") != FString::npos;
			if (bExists)
			{
				ImGui::TextUnformatted("An asset with this name already exists.");
			}
			if (bInvalid)
			{
				ImGui::TextUnformatted("Enter a valid asset name.");
			}
			ImGui::BeginDisabled(bExists || bInvalid);
			if (ImGui::Button("Create") && OnCreateBlendSpace)
			{
				OnCreateBlendSpace(FbxPath::ToString(Target), bNewBlendSpace2D);
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopupModal("Create Particle System", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::InputText("Name", NewAssetName, sizeof(NewAssetName));

			const FString Name = NewAssetName;
			const fs::path Target = CurrentPath / (Name + ".particlesystem");

			const bool bExists = fs::exists(Target);
			const bool bInvalid = Name.empty() || Name == "." || Name == ".." ||
				Name.find_first_of("\\/:*?\"<>|") != FString::npos;

			if (bExists)
			{
				ImGui::TextUnformatted("An asset with this name already exists.");
			}
			if (bInvalid)
			{
				ImGui::TextUnformatted("Enter a valid asset name.");
			}

			ImGui::BeginDisabled(bExists || bInvalid || !OnCreateParticleSystem);

			if (ImGui::Button("Create"))
			{
				OnCreateParticleSystem(FbxPath::ToString(Target));
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndDisabled();

			ImGui::SameLine();

			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
		if (ImGui::BeginPopupModal("Create AnimGraph", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::InputText("Name", NewAssetName, sizeof(NewAssetName));
			const fs::path Target = CurrentPath / (FString(NewAssetName) + ".animgraph");
			const bool bExists = fs::exists(Target);
			if (bExists)
			{
				ImGui::TextUnformatted("An asset with this name already exists.");
			}
			// AnimGraph는 하나의 스켈레톤에 묶이므로 생성할 때 직접 고른다.
			if (ImGui::BeginCombo("Skeleton", NewAnimGraphSkeleton.empty() ? "Select skeleton" : NewAnimGraphSkeleton.c_str()))
			{
				for (const FString& Key : SkeletonKeys)
				{
					if (ImGui::Selectable(Key.c_str(), Key == NewAnimGraphSkeleton))
					{
						NewAnimGraphSkeleton = Key;
					}
				}
				ImGui::EndCombo();
			}
			if (SkeletonKeys.IsEmpty())
			{
				ImGui::TextUnformatted("No skeleton (SKEL_*.fbxbin) found under Assets.");
			}
			ImGui::BeginDisabled(bExists || NewAssetName[0] == '\0' || NewAnimGraphSkeleton.empty());
			if (ImGui::Button("Create") && OnCreateAnimGraph)
			{
				OnCreateAnimGraph(FbxPath::ToString(Target), NewAnimGraphSkeleton);
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal("Create Widget", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::InputText("Name", NewAssetName, sizeof(NewAssetName));
			const fs::path Target = CurrentPath / (FString(NewAssetName) + ".widget");
			const bool bExists = fs::exists(Target);
			if (bExists)
			{
				ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "A file with this name already exists.");
			}

			ImGui::BeginDisabled(bExists || NewAssetName[0] == '\0');
			if (ImGui::Button("Create") && OnCreateWidget)
			{
				OnCreateWidget(FbxPath::ToString(Target)); // "Assets/UI/GameOver.widget" 형식
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// 그리드 레이아웃 계산
		float padding = EditorUIScale::Px(24.0f);
		float thumbnailSize = EditorUIScale::Px(80.0f);
		float cellSize = thumbnailSize + padding;
		float panelWidth = ImGui::GetContentRegionAvail().x;
		int ColumnCount = static_cast<int>(panelWidth / cellSize);
		if (ColumnCount < 1)
		{
			ColumnCount = 1;
		}

		// 테이블 기반 그리드 시작
		fs::path PendingOpen;
		if (ImGui::BeginTable("AssetGridTable", ColumnCount))
		{
			RefreshFbxFolderCache();
			std::error_code IterateError;
			fs::directory_iterator It(CurrentPath, IterateError);
			const fs::directory_iterator End;

			for (; !IterateError && It != End; It.increment(IterateError))
			{
				const fs::path Path = It->path();
				const FString EntryName = FbxPath::ToString(Path.filename());
				// .bin은 OBJ 캐시와 예전 .animseq.bin이다. 애셋은 .fbxbin 하나로 통일했으므로 보이지 않는다.
				if (Path.extension() == ".bin" || EntryName.starts_with('.') || HiddenFbxFiles.contains(Path.filename())) continue;
				if (!SearchQuery.empty())
				{
					FString LowerName = EntryName;
					std::transform(LowerName.begin(), LowerName.end(), LowerName.begin(),
						[](unsigned char C) { return static_cast<char>(std::tolower(C)); });
					if (LowerName.find(SearchQuery) == FString::npos) continue;
				}

				std::error_code TypeError;
				const bool bIsDirectory = fs::is_directory(Path, TypeError);

				if (TypeError)
				{
					continue;
				}

				ImGui::TableNextColumn();

				std::string PathString = FbxPath::ToString(Path);
				std::string FilenameString = FbxPath::ToString(Path.filename());
				FString DisplayName = FilenameString;

				ImGui::PushID(PathString.c_str()); // 각 위젯에 고유 ID 부여
				UTexture2D* Thumbnail = nullptr;

				// 실제 에셋일 때만 채워진다.
				// 폴더/파일 아이콘은 끌 수 없어야 하므로 썸네일과 구분해서 들고 있는다.
				UTexture2D* TextureAsset = nullptr;
				UFont* FontAsset = nullptr;
				// 3D 모델용 아이콘
				ID3D11ShaderResourceView* ThumbnailSRV = nullptr;

				// 폴더 또는 파일 아이콘 표시
				if (bIsDirectory)
				{
					Thumbnail = UAssetManager::Get().LoadTexture(FolderIconPath);
				}
				else // 파일인 경우
				{
					URenderAsset* Asset = UAssetManager::GetAssetByPath<URenderAsset>(PathString);
					if (Asset && Asset->IsA<UTexture2D>())
					{
						TextureAsset = Cast<UTexture2D>(Asset);
						Thumbnail = TextureAsset;
					}
					else if (Asset && Asset->IsA<UFont>())
					{
						FontAsset = Cast<UFont>(Asset);

						// 폰트는 글리프 아틀라스를 그대로 미리보기로 쓴다
						Thumbnail = FontAsset->AtlasTexture;
					}
					else if (Asset && Asset->IsA<USkeletalMesh>())
					{
						if (FTexture2D* FbxThumb = GetOrCreateSkeletalMeshThumbnail(PathString))
						{
							ThumbnailSRV = FbxThumb->GetSRV();
						}
					}
				}

				if (Thumbnail && Thumbnail->GetResource())
				{
					ThumbnailSRV = Thumbnail->GetResource()->GetSRV();
				}
				else if (!ThumbnailSRV)
				{
					Thumbnail = UAssetManager::Get().LoadTexture(FileIconPath);
					if (Thumbnail && Thumbnail->GetResource())
					{
						ThumbnailSRV = Thumbnail->GetResource()->GetSRV();
					}
				}

				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EditorUIScale::Px(4.0f), EditorUIScale::Px(4.0f)));

				const float availWidth = ImGui::GetContentRegionAvail().x;
				const float totalButtonWidth = thumbnailSize + EditorUIScale::Px(8.0f);
				if (availWidth > totalButtonWidth)
				{
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availWidth - totalButtonWidth) * 0.5f);
				}

				if (ThumbnailSRV)
				{
					ImGui::ImageButton(PathString.c_str(), (ImTextureID)ThumbnailSRV, { thumbnailSize, thumbnailSize });
				}
				else
				{
					ImGui::Button(DisplayName.c_str(), { thumbnailSize, thumbnailSize });
				}

				ImGui::PopStyleVar();
				auto OpenOnClick = [&]()
				{
					if (!ImGui::IsItemHovered() || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						return false;
					}
					const bool Open = SelectedAssetPath == Path && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
					SelectedAssetPath = Path;
					return Open;
				};
				bool bOpenRequested = OpenOnClick();
				if (ImGui::BeginPopupContextItem("AssetActions"))
				{
					FString Extension = Path.extension().string();
					std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char C)
						{ return static_cast<char>(std::tolower(C)); });
					if (Extension == ".fbx" && OnImportFbx && ImGui::MenuItem("Import FBX..."))
					{
						OnImportFbx(PathString, false);
					}
					if (Extension == ".fbxbin")
					{
						DrawFbxBinaryMenu(PathString);
					}
					if (Extension == ".fbxbin" && Path.filename().string().starts_with("SKEL_") &&
						ImGui::MenuItem("Create AnimGraph"))
					{
						// 선택한 스켈레톤을 미리 채운 채 생성 모달을 연다. 파일은 현재 폴더에 만들어진다.
						strcpy_s(NewAssetName, "NewAnimGraph");
						RefreshSkeletonKeys();
						NewAnimGraphSkeleton = SkeletonKeyFromPath(Path);
						bOpenCreateAnimGraph = true;
					}
					if (Extension != ".fbx" && ImGui::MenuItem("Open"))
					{
						bOpenRequested = true;
					}
					ImGui::EndPopup();
				}

				// 페이로드에 포인터를 그대로 실어서 받는 쪽이 AssetMap을 다시 뒤지지 않게 한다.
				if (TextureAsset && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload(EditorDragDrop::Texture, &TextureAsset, sizeof(UTexture2D*));

					// 끌고 다니는 동안 커서를 따라다니는 미리보기
					ImGui::Image((ImTextureID)TextureAsset->GetResource()->GetSRV(), { thumbnailSize, thumbnailSize });
					ImGui::TextUnformatted(FilenameString.c_str());

					ImGui::EndDragDropSource();
				}
				if (FontAsset && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload(EditorDragDrop::Font, &FontAsset, sizeof(UFont*));

					ImGui::TextUnformatted(FilenameString.c_str());

					ImGui::EndDragDropSource();
				}
				// 스크립트는 에셋 객체가 없으므로 파일 경로를 그대로 싣는다 (TEAM5).
				if (!bIsDirectory && Path.extension() == ".lua" && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload(EditorDragDrop::ScriptFile, PathString.c_str(), PathString.size() + 1);
					ImGui::TextUnformatted(FilenameString.c_str());
					ImGui::EndDragDropSource();
				}
				// 파일 이름 표시

				// 이름 영역은 두 줄로 고정하고, 남은 이름은 두 번째 줄에서 말줄임한다.
				const ImVec2 NamePos = ImGui::GetCursorScreenPos();
				const float NameWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
				const float LineHeight = ImGui::GetTextLineHeight();
				const char* NameBegin = DisplayName.c_str();
				const char* NameEnd = NameBegin + DisplayName.size();
				const char* FirstLineEnd = ImGui::GetFont()->CalcWordWrapPosition(ImGui::GetFontSize(), NameBegin, NameEnd, NameWidth);
				ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), NamePos,
					ImVec2(NamePos.x + NameWidth, NamePos.y + LineHeight), NamePos.x + NameWidth,
					NameBegin, FirstLineEnd, nullptr);
				const char* SecondLineBegin = FirstLineEnd;
				while (SecondLineBegin < NameEnd && (*SecondLineBegin == ' ' || *SecondLineBegin == '\t'))
				{
					++SecondLineBegin;
				}
				if (SecondLineBegin < NameEnd)
				{
					ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), ImVec2(NamePos.x, NamePos.y + LineHeight),
						ImVec2(NamePos.x + NameWidth, NamePos.y + LineHeight * 2.0f), NamePos.x + NameWidth,
						SecondLineBegin, NameEnd, nullptr);
				}
				ImGui::Dummy(ImVec2(NameWidth, LineHeight * 2.0f));
				ImGui::SetItemTooltip("%s", FilenameString.c_str());

				bOpenRequested |= OpenOnClick();
				if (bOpenRequested)
				{
					PendingOpen = Path;
				}

				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (!PendingOpen.empty())
		{
			OpenItem(PendingOpen);
		}
	}
	ImGui::EndChild();
	ImGui::End();
}

void FContentDrawerPanel::OpenItem(const fs::path& Path)
{
	std::error_code Error;
	if (fs::is_directory(Path, Error))
	{
		CurrentPath = Path;
		return;
	}
	FString Extension = Path.extension().string();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char C)
		{ return static_cast<char>(std::tolower(C)); });
	if (Extension == ".widget" && OnOpenWidget)
	{
		OnOpenWidget(FbxPath::ToString(Path));
	}
	else if (Extension == ".animgraph" && OnOpenAnimGraph)
	{
		OnOpenAnimGraph(FbxPath::ToString(Path));
	}
	else if (Extension == ".blendspace" && OnOpenBlendSpace)
	{
		OnOpenBlendSpace(FbxPath::ToString(Path));
	}
	else if (Extension == ".particlesystem" && OnOpenParticleSystem)
	{
		OnOpenParticleSystem(FbxPath::ToString(Path));
	}
	else if (Extension == ".fbxbin")
	{
		FString Error;
		auto* Asset = FFbxAssetLoader::LoadAsset(FbxPath::ToString(Path), Error);
		if (!Asset)
		{
			HTR_LOG(Warning, "[FBX] {}", Error);
		}
		else if (Asset->IsA<UAnimSequence>() && OnOpenAnimSequence)
		{
			OnOpenAnimSequence(FbxPath::ToString(Path));
		}
		else if (Asset->IsA<USkeletalMesh>() && OnOpenSkeletalMesh)
		{
			OnOpenSkeletalMesh(FbxPath::ToString(Path));
		}
		else if (OnOpenAsset)
		{
			OnOpenAsset(FbxPath::ToString(Path));
		}
	}
	// .fbx는 메시로 열지 않는다. 바이너리가 이 폴더에 있으면 .fbx가 숨겨지므로 여기 오는 것은 임포트 요청이다.
	else if (Extension == ".fbx")
	{
		if (OnImportFbx)
		{
			OnImportFbx(FbxPath::ToString(Path), false);
		}
	}
	else if (OnOpenAsset)
	{
		OnOpenAsset(FbxPath::ToString(Path));
	}
}

void FContentDrawerPanel::RefreshFbxFolderCache()
{
	const double Now = ImGui::GetTime();
	if (FbxCachePath == CurrentPath && Now < FbxCacheTime)
	{
		return;
	}
	FbxCachePath = CurrentPath;
	FbxCacheTime = Now + 1.0; // 탐색기에서 파일을 옮겨도 곧 반영된다
	HiddenFbxFiles.clear();
	UnimportedFbxFiles.clear();

	const auto Lower = [](FString Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char C)
			{ return static_cast<char>(std::tolower(C)); });
		return Value;
	};
	std::set<FString> FolderSources;
	std::vector<fs::path> FbxFiles;
	std::error_code Error;
	for (fs::directory_iterator It(CurrentPath, Error), End; !Error && It != End; It.increment(Error))
	{
		const fs::path Path = It->path();
		const FString Extension = Lower(FbxPath::ToString(Path.extension()));
		if (Extension == ".fbx")
		{
			FbxFiles.push_back(Path);
		}
		else if (Extension == ".fbxbin")
		{
			FFbxAssetData Header;
			FString HeaderError;
			// 애니메이션만 든 FBX도 AS_*.fbxbin이 있으면 임포트된 것으로 보고 숨긴다.
			if (FFbxAssetBake::ReadHeader(FbxPath::ToString(Path), Header, HeaderError) &&
				(Header.Type == EFbxAssetType::StaticMesh || Header.Type == EFbxAssetType::SkeletalMesh ||
					Header.Type == EFbxAssetType::AnimSequence))
			{
				FolderSources.insert(Lower(Header.SourcePath));
			}
		}
	}
	for (const fs::path& Path : FbxFiles)
	{
		FString Key, KeyError;
		if (!FbxPath::GetAssetKey(FbxPath::ToString(Path), Key, KeyError))
		{
			continue;
		}
		if (FolderSources.contains(Lower(Key)))
		{
			HiddenFbxFiles.insert(Path.filename());
		}
		else if (!UAssetManager::Get().IsFbxImported(Key))
		{
			UnimportedFbxFiles.insert(Path.filename());
		}
	}
}

namespace
{
// 스켈레톤 키는 Assets 폴더 기준 상대 경로다. AnimGraph 에셋과 USkeleton::GetPath()가 같은 형식을 쓴다.
FString SkeletonKeyFromPath(const fs::path& Path)
{
	std::error_code Ec;
	fs::path Relative = fs::relative(Path, "Assets", Ec);
	if (Ec || Relative.empty() || *Relative.begin() == "..")
	{
		Relative = Path;
	}
	return FbxPath::ToString(Relative);
}
}

void FContentDrawerPanel::RefreshSkeletonKeys()
{
	SkeletonKeys.Reset();
	std::error_code Ec;
	for (fs::recursive_directory_iterator It(AssetRootPath, Ec), End; !Ec && It != End; It.increment(Ec))
	{
		if (!It->is_regular_file() || It->path().extension() != ".fbxbin" ||
			!It->path().filename().string().starts_with("SKEL_"))
		{
			continue;
		}
		SkeletonKeys.Push(SkeletonKeyFromPath(It->path()));
	}
	std::sort(SkeletonKeys.begin(), SkeletonKeys.end());
}

void FContentDrawerPanel::DrawFbxBinaryMenu(const FString& Path)
{
	if (ImGui::IsWindowAppearing())
	{
		FFbxAssetData Header;
		FString Error;
		bFbxMenuHasSource = false;
		if (!FFbxAssetBake::ReadHeader(Path, Header, Error))
		{
			FbxMenuStatus = "Binary is outdated or unreadable";
		}
		else
		{
			const fs::path Source = FbxPath::SourceFile(Header.ActualSourcePath);
			std::error_code FileError;
			bFbxMenuHasSource = fs::is_regular_file(Source, FileError);
			FbxMenuStatus = !bFbxMenuHasSource ? "Source not found: " + Header.ActualSourcePath : FbxPath::FileHash(Source) == Header.SourceHash ? "Source unchanged: " + Header.ActualSourcePath
																																				 : "Source changed: " + Header.ActualSourcePath;
		}
	}
	ImGui::TextDisabled("%s", FbxMenuStatus.c_str());
	ImGui::Separator();
	if (OnImportFbx && ImGui::MenuItem("Reimport...", nullptr, false, bFbxMenuHasSource))
	{
		OnImportFbx(Path, true);
	}
	if (OnReimportFromFile && ImGui::MenuItem("Reimport From File..."))
	{
		OnReimportFromFile(Path);
	}
	ImGui::Separator();
}

void FContentDrawerPanel::RenderFolderTree(const fs::path& Path, fs::path& SelectedPath)
{
	std::error_code IterateError;
	fs::directory_iterator It(Path, IterateError);
	const fs::directory_iterator End;

	for (; !IterateError && It != End; It.increment(IterateError))
	{
		const fs::path Entry = It->path();

		std::error_code TypeError;
		const bool bIsDirectory =
			fs::is_directory(Entry, TypeError);

		if (TypeError || !bIsDirectory || FbxPath::ToString(Entry.filename()).starts_with('.'))
		{
			continue;
		}

		const FString FilenameString =
			FbxPath::ToString(Entry.filename());

		ImGuiTreeNodeFlags Flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_DrawLinesToNodes;

		if (Entry == SelectedPath)
		{
			Flags |= ImGuiTreeNodeFlags_Selected;
		}

		const bool bIsOpen =
			ImGui::TreeNodeEx(FilenameString.c_str(), Flags);

		if (ImGui::IsItemClicked())
		{
			SelectedPath = Entry;
		}

		if (bIsOpen)
		{
			RenderFolderTree(Entry, SelectedPath);
			ImGui::TreePop();
		}
	}

	if (IterateError)
	{
		ImGui::TextUnformatted("Could not read this folder.");
	}
}

void FContentDrawerPanel::InitThumbnailStudio()
{
	if (bStudioInitialized)
	{
		return;
	}

	ThumbnailWorld = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!ThumbnailWorld)
	{
		return;
	}

	ThumbnailActor = ThumbnailWorld->SpawnActor<ASkeletalMeshActor>();

	if (ASkyLightActor* SkyLight = ThumbnailWorld->SpawnActor<ASkyLightActor>())
	{
		SkyLight->GetBillboardComponent()->SetVisibility(false);
		SkyLight->GetLightComponent()->SetIntensity(0.3f);
	}
	if (ADirectionalLightActor* Sun = ThumbnailWorld->SpawnActor<ADirectionalLightActor>())
	{
		Sun->GetBillboardComponent()->SetVisibility(false);
		Sun->GetArrowComponent()->SetVisibility(false);
		Sun->GetLightComponent()->SetIntensity(1.0f);
		Sun->GetLightComponent()->SetRelativeRotation(FRotator(40.0f, 160.0f, 0.0f));
	}

	ThumbnailSkybox = MakeUnique<FSkyboxRenderer>();
	if (!ThumbnailSkybox->Init(EditorResourcePaths::AssetViewerSkyPanorama))
	{
		ThumbnailSkybox.reset();
	}

	ThumbnailClient.SetWorld(ThumbnailWorld);
	ThumbnailClient.SetViewportType(ELevelViewportType::Perspective);
	ThumbnailClient.Resize(128, 128);
	ThumbnailClient.SetRect(FRect{ 0.0f, 0.0f, 128.0f, 128.0f });

	bStudioInitialized = true;
}

FTexture2D* FContentDrawerPanel::GetOrCreateSkeletalMeshThumbnail(const FString& FbxPath)
{
	auto It = ThumbnailCache.find(FbxPath);
	if (It != ThumbnailCache.end())
	{
		return It->second.get();
	}

	InitThumbnailStudio();
	if (!ThumbnailActor || !ThumbnailWorld)
	{
		ThumbnailCache[FbxPath] = nullptr;
		return nullptr;
	}

	USkeletalMesh* Mesh = UAssetManager::LoadFbxSkeletalMesh(FbxPath);
	if (!Mesh)
	{
		ThumbnailCache[FbxPath] = nullptr;
		return nullptr;
	}
	ThumbnailActor->GetSkeletalMeshComponent()->SetSkeletalMesh(Mesh);

	const FBox Bounds = ThumbnailActor->GetSkeletalMeshComponent()->CalcBounds();
	const FVector Extent = Bounds.Max - Bounds.Min;
	const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;

	const float HalfDim = std::max(Extent.Z * 0.5f, std::max(Extent.X, Extent.Y) * 0.5f);
	const float HalfFov = FMath::DegreesToRadians(ThumbnailClient.GetViewFOV()) * 0.5f;
	const float Distance = (HalfDim / std::max(0.2f, std::sin(HalfFov))) * 0.8f;

	ThumbnailClient.SetViewRotation(FRotator(10.0f, 165.0f, 0.0f));
	const FVector TargetPos = Center + FVector(0.0f, 0.0f, Extent.Z * 0.04f);
	ThumbnailClient.SetViewLocation(TargetPos - ThumbnailClient.GetViewForward() * Distance);

	if (UEditorEngine* EdEngine = Cast<UEditorEngine>(GEngine))
	{
		EdEngine->RenderPreviewViewport(&ThumbnailClient, ThumbnailWorld, nullptr, ThumbnailSkybox.get(), false);
		FEngineLoop& Loop = GEngine->GetEngineLoop();
		if (FSwapchain* SC = Loop.GetSwapchain())
		{
			if (FWindow* Win = Loop.GetMainWindow())
			{
				RenderCommand::BindRenderPassNoClear(SC->GetBackbuffer(), nullptr,
					Win->GetWidth(), Win->GetHeight());
			}
		}
	}

	FTexture2D* RenderedTarget = ThumbnailClient.GetColorTarget();
	if (!RenderedTarget)
	{
		ThumbnailCache[FbxPath] = nullptr;
		return nullptr;
	}

	FTextureDesc Desc{};
	Desc.Width = RenderedTarget->GetWidth();
	Desc.Height = RenderedTarget->GetHeight();
	Desc.Format = RenderedTarget->GetTextureFormat();
	Desc.Usage = ETextureUsage::Default;
	Desc.BindFlags = ETextureBindFlags::ShaderResource;

	TUniquePtr<FTexture2D> CachedTex = RenderCommand::CreateTexture2D(Desc);
	if (CachedTex)
	{
		RenderCommand::CopyTexture(CachedTex.get(), RenderedTarget);
		ThumbnailCache[FbxPath] = std::move(CachedTex);
		return ThumbnailCache[FbxPath].get();
	}

	ThumbnailCache[FbxPath] = nullptr;
	return nullptr;
}

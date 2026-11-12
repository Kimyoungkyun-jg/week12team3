#include "EnginePCH.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"

#include "Render/SkeletalMesh.h"
#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxMeshBake.h"
#include "Text/Font.h"
#include "Editor/HitoriEd/EditorDragDrop.h"
#include "Animation/AnimSequenceFile.h"
#include "Animation/BlendSpaceFile.h"
#include "Animation/AnimBlueprintFile.h"

bool FContentDrawerPanel::Init()
{
	return false;
}

void FContentDrawerPanel::Tick(float DeltaTime)
{
}

void FContentDrawerPanel::OnRender()
{
	ImGui::Begin("Content Drawer");
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

	float treeViewWidth = 200.0f;
	ImGui::BeginChild("FolderTreeView", ImVec2(treeViewWidth, 0));
	{
		// 루트 노드
		if (ImGui::TreeNodeEx(AssetRootPath.string().c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
			(AssetRootPath == CurrentPath ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_DrawLinesToNodes))
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
		// 빈 곳 우클릭 (항목 위 우클릭은 제외)
		if (ImGui::BeginPopupContextWindow("##DrawerContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::MenuItem("Create Widget"))
			{
				strcpy_s(NewAssetName, "NewWidget");
				bOpenCreateWidget = true;   // 메뉴 팝업 안에서 바로 다른 팝업을 열 수 없어 다음 줄에서 연다
			}
			ImGui::EndPopup();
		}
		if (bOpenCreateWidget) { ImGui::OpenPopup("Create Widget"); bOpenCreateWidget = false; }

		if (ImGui::BeginPopupModal("Create Widget", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::InputText("Name", NewAssetName, sizeof(NewAssetName));
			const fs::path Target = CurrentPath / (FString(NewAssetName) + ".widget");
			const bool bExists = fs::exists(Target);
			if (bExists) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "A file with this name already exists.");

			ImGui::BeginDisabled(bExists || NewAssetName[0] == '\0');
			if (ImGui::Button("Create") && OnCreateWidget)
			{
				OnCreateWidget(Target.generic_string());   // "Assets/UI/GameOver.widget" 형식
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		// 그리드 레이아웃 계산
		float padding = 16.0f;
		float thumbnailSize = 80.0f;
		float cellSize = thumbnailSize + padding;
		float panelWidth = ImGui::GetContentRegionAvail().x;
		int ColumnCount = static_cast<int>(panelWidth / cellSize);
		if (ColumnCount < 1) ColumnCount = 1;

		// 테이블 기반 그리드 시작
		if (ImGui::BeginTable("AssetGridTable", ColumnCount))
		{
			std::error_code IterateError;
			fs::directory_iterator It(CurrentPath, IterateError);
			const fs::directory_iterator End;

			// 폴더를 열 때 멈추지 않도록 썸네일은 프레임당 하나씩 로드한다. 그동안은 파일 아이콘을 보여 준다.
			bool bLoadedThumbnail = false;

			for (; !IterateError && It != End; It.increment(IterateError))
			{
				const fs::path Path = It->path();

				std::error_code TypeError;
				const bool bIsDirectory = fs::is_directory(Path, TypeError);

				if (TypeError)
					continue;

				ImGui::TableNextColumn();

				std::string PathString = Path.generic_string();
				std::string FilenameString = Path.filename().string();

				ImGui::PushID(PathString.c_str()); // 각 위젯에 고유 ID 부여
				UTexture2D* Thumbnail = nullptr;

				// 실제 에셋일 때만 채워진다.
				// 폴더/파일 아이콘은 끌 수 없어야 하므로 썸네일과 구분해서 들고 있는다.
				UTexture2D* TextureAsset = nullptr;
				UFont* FontAsset = nullptr;

				// 폴더 또는 파일 아이콘 표시
				if (bIsDirectory)
				{
					Thumbnail = UAssetManager::GetAssetByPath<UTexture2D>(FolderIconPath);
				}
				else // 파일인 경우
				{
					URenderAsset* Asset = UAssetManager::FindLoadedAsset(PathString);
					if (!Asset && !bLoadedThumbnail)
					{
						// 메시는 썸네일로 쓰지 않으므로 드로어를 연 것만으로 로드하지 않는다
						const EAssetType UnloadedType = UAssetManager::GetUnloadedAssetType(PathString);
						if (UnloadedType == EAssetType::Texture || UnloadedType == EAssetType::Font)
						{
							Asset = UAssetManager::GetAssetByPath<URenderAsset>(PathString);
							bLoadedThumbnail = true;
						}
					}

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

					if (Thumbnail == nullptr)
					{
						Thumbnail = UAssetManager::GetAssetByPath<UTexture2D>(FileIconPath);
					}
				}
				if (Thumbnail)
				{
					ImGui::ImageButton(PathString.c_str(), (ImTextureID)Thumbnail->GetResource()->GetSRV(), { thumbnailSize, thumbnailSize });

					if (!bIsDirectory)
					{
                        URenderAsset* ContextAsset = UAssetManager::FindLoadedAsset(PathString);
                        const bool bSkeletalMesh = (ContextAsset && ContextAsset->IsA<USkeletalMesh>()) || UAssetManager::GetUnloadedAssetType(PathString) == EAssetType::SkeletalMesh;

                        if (bSkeletalMesh && ImGui::BeginPopupContextItem("##AssetContext"))
                        {
                            BlendSpaceCreator.DrawMenuItems(PathString, CurrentPath);
                            AnimBlueprintCreator.DrawMenuItems(PathString, CurrentPath);
                            ImGui::EndPopup();
                        }
					}
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
				// 더블 클릭 이벤트 처리
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					if (bIsDirectory)
					{
						CurrentPath /= Path.filename(); // 하위 폴더로 이동
					}

					// TODO: UIEditor를 FAssetEditor 계열로 통합
					else if (Path.extension() == ".widget" && OnOpenWidget)
					{
						OnOpenWidget(PathString);
					}

					else if ((Path.extension() == ".obj" || FFbxMeshBake::IsBakedPath(PathString)
						|| AnimSequenceFile::IsAnimSequencePath(PathString)) && OnOpenMesh)
					{
						OnOpenMesh(PathString);
					}

					else if ((BlendSpaceFile::IsBlendSpacePath(PathString) || AnimBlueprintFile::IsAnimBlueprintPath(PathString)) && OnOpenMesh)
					{
						OnOpenMesh(PathString);
					}

				}
				// 파일 이름 표시
				ImGui::TextWrapped("%s", FilenameString.c_str());

				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}
	BlendSpaceCreator.ProcessCreateRequest();
    AnimBlueprintCreator.ProcessCreateRequest();

	ImGui::EndChild();
	ImGui::End();
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

		if (TypeError || !bIsDirectory)
			continue;

		const FString FilenameString =
			Entry.filename().string();

		ImGuiTreeNodeFlags Flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_DrawLinesToNodes;

		if (Entry == SelectedPath)
			Flags |= ImGuiTreeNodeFlags_Selected;

		const bool bIsOpen =
			ImGui::TreeNodeEx(FilenameString.c_str(), Flags);

		if (ImGui::IsItemClicked())
			SelectedPath = Entry;

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

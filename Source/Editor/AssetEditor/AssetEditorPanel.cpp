#include "EnginePCH.h"
#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Editor/HitoriEd/EditorDragDrop.h"
#include "Editor/EditorUI/EditorSort.h"
#include "UObject/UObjectIterator.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "Asset/FbxImporter/FbxAssetLoader.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Asset/AssetManager.h"
#include "Animation/Skeleton.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/Texture2D.h"
#include "Render/SkyboxRenderer.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/BillboardComponent.h"
#include "Engine/World.h"

namespace
{
bool EditAssetColor(const char* Label, float* Linear, bool Alpha = false)
{
	float Display[4] = { std::pow(std::max(Linear[0], 0.0f), 1.0f / 2.2f),
		std::pow(std::max(Linear[1], 0.0f), 1.0f / 2.2f), std::pow(std::max(Linear[2], 0.0f), 1.0f / 2.2f), Alpha ? Linear[3] : 1.0f };
	const float Previous[3] = { Display[0], Display[1], Display[2] };
	const auto Flags = ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float;
	if (!(Alpha ? ImGui::ColorEdit4(Label, Display, Flags) : ImGui::ColorEdit3(Label, Display, Flags))) return false;
	for (int I = 0; I < 3; ++I) if (Display[I] != Previous[I]) Linear[I] = std::pow(std::max(Display[I], 0.0f), 2.2f);
	if (Alpha) Linear[3] = Display[3];
	return true;
}

FString GetTextureDisplayName(const FString& InPath)
{
	const fs::path P = FbxPath::FromString(InPath);
	const FString Name = FbxPath::ToString(P.filename());
	return Name.empty() ? InPath : Name;
}

void SetMaterialTextureSlot(UMaterial* Material, int32 SlotIndex, UTexture2D* NewTexture)
{
	if (!Material || SlotIndex < 0) return;
	while (static_cast<int32>(Material->Textures.Num()) <= SlotIndex)
		Material->Textures.Add(nullptr);
	Material->Textures[SlotIndex] = NewTexture;
}

void DrawAssetMaterialTextureSlot(UMaterial* Material, int32 SlotIndex, const char* Label)
{
	if (!Material) return;
	const float ThumbSize = EditorUIScale::Px(48.0f);
	const float ItemThumbSize = EditorUIScale::Px(24.0f);
	UTexture2D* Texture = Material->Textures.IsValidIndex(SlotIndex) ? Material->Textures[SlotIndex] : nullptr;

	ImGui::PushID(SlotIndex);
	if (Texture && Texture->GetResource())
	{
		ImGui::Image(Texture->GetResource()->GetSRV(), ImVec2(ThumbSize, ThumbSize));
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Texture->GetPath().c_str());
	}
	else
	{
		ImGui::Button("Empty", ImVec2(ThumbSize, ThumbSize));
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Texture))
		{
			UTexture2D* Dropped = *static_cast<UTexture2D**>(Payload->Data);
			SetMaterialTextureSlot(Material, SlotIndex, Dropped);
			Texture = Dropped;
		}
		ImGui::EndDragDropTarget();
	}

	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::TextUnformatted(Label);
	const FString CurrentName = Texture ? GetTextureDisplayName(Texture->GetPath()) : FString("None");
	ImGui::SetNextItemWidth(-1.0f);
	EditorUIScale::LimitNextComboPopupWidth();
	const bool bOpen = ImGui::BeginCombo("##TexSlot", CurrentName.c_str());
	if (ImGui::IsItemHovered() && Texture) ImGui::SetTooltip("%s", Texture->GetPath().c_str());
	if (bOpen)
	{
		if (ImGui::Selectable("None", Texture == nullptr))
			SetMaterialTextureSlot(Material, SlotIndex, nullptr);

		const TArray<UTexture2D*> Candidates = EditorSort::CollectSorted<UTexture2D>(
			TObjectIterator<UTexture2D>(),
			[](const UTexture2D* T) { return GetTextureDisplayName(T->GetPath()); },
			[](UTexture2D* T) { return !T->IsRetired() && T->GetResource() && !T->GetPath().empty(); });

		for (UTexture2D* Candidate : Candidates)
		{
			const bool bSelected = (Candidate == Texture);
			const FString CandName = GetTextureDisplayName(Candidate->GetPath());
			ImGui::PushID(Candidate);
			ImGui::Image(Candidate->GetResource()->GetSRV(), ImVec2(ItemThumbSize, ItemThumbSize));
			ImGui::SameLine();
			if (ImGui::Selectable(CandName.c_str(), bSelected, ImGuiSelectableFlags_None, ImVec2(0.0f, ItemThumbSize)))
				SetMaterialTextureSlot(Material, SlotIndex, Candidate);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Candidate->GetPath().c_str());
			if (bSelected) ImGui::SetItemDefaultFocus();
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	ImGui::EndGroup();
	ImGui::PopID();
}
}

FAssetEditorPanel::FAssetEditorPanel() = default;
FAssetEditorPanel::~FAssetEditorPanel() { Release(); }

void FAssetEditorPanel::Release()
{
	Client.SetWorld(nullptr);
	Skybox.reset();
	if (PreviewWorld)
	{
		PreviewWorld->ClearWorld(); PreviewWorld->FlushPendingDestruction();
		if (GEngine) GEngine->DestroyWorld(PreviewWorld);
		PreviewWorld = nullptr;
	}
	Asset = nullptr;
}

void FAssetEditorPanel::OpenAsset(const FString& InPath)
{
	FString Error;
	auto* Loaded = UAssetManager::GetAssetByPath<URenderAsset>(InPath);
	if (!Loaded && FbxPath::FromString(InPath).extension() == ".fbxbin") Loaded = FFbxAssetLoader::LoadAsset(InPath, Error);
	if (!Loaded) { HTR_LOG(Warning, "Cannot preview asset {}: {}", InPath, Error); return; }
	Release(); Path = InPath; Asset = Loaded; Status.clear(); SetOpen(true);
	UStaticMesh* Mesh = Cast<UStaticMesh>(Asset);
	UMaterial* Material = Cast<UMaterial>(Asset);
	if (Material) Mesh = UAssetManager::GetAssetByPath<UStaticMesh>("Sphere");
	if (!Mesh) return;
	PreviewWorld = UWorld::CreateWorld(EWorldType::Editor, false);
	auto* Actor = PreviewWorld->SpawnActor<AStaticMeshActor>();
	Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
	if (Material) Actor->GetStaticMeshComponent()->SetMaterial(0, Material);
	auto* Sun = PreviewWorld->SpawnActor<ADirectionalLightActor>();
	Sun->GetBillboardComponent()->SetVisibility(false);
	Sun->GetArrowComponent()->SetVisibility(false);
	Sun->GetLightComponent()->SetRelativeRotation(FRotator(40, 150, 0));
	Sun->GetLightComponent()->SetIntensity(1.0f);
	auto* Sky = PreviewWorld->SpawnActor<ASkyLightActor>();
	Sky->GetBillboardComponent()->SetVisibility(false); Sky->GetLightComponent()->SetIntensity(.3f);
	Client.SetWorld(PreviewWorld); Client.Resize(512, 512); Client.SetActive(true);
	Client.SetViewRotation(FRotator(15, 160, 0));
	const auto Bounds = Mesh->GetRenderBounds();
	Client.FocusOnBounds((Bounds.Min + Bounds.Max) * .5f, std::max(.1f, (Bounds.Max - Bounds.Min).Length() * .5f));
	Skybox = MakeUnique<FSkyboxRenderer>();
	if (!Skybox->Init(EditorResourcePaths::AssetViewerSkyPanorama)) Skybox.reset();
}

void FAssetEditorPanel::Tick(float DeltaTime)
{
	if (Asset && Asset->IsRetired())
	{
		const bool bWasOpen = IsOpen();
		const auto Reopen = Path;
		OpenAsset(Reopen);
		SetOpen(bWasOpen);
	}
	if (PreviewWorld) Client.TickInput(DeltaTime, false, false, 0, 4, 1);
}

void FAssetEditorPanel::RenderPreview(UEditorEngine* Engine)
{
	if (Engine && IsOpen() && bVisible && PreviewWorld)
		Engine->RenderPreviewViewport(&Client, PreviewWorld, nullptr, Skybox.get(), true);
}

void FAssetEditorPanel::SaveMaterial()
{
	auto* Material = Cast<UMaterial>(Asset);
	if (!Material || FbxPath::FromString(Path).extension() != ".fbxbin") return;
	FFbxAssetData Data;
	if (!FFbxAssetBake::ReadBaked(Path, Data, Status)) return;
	Data.Material.DiffuseOrFactor = Material->BaseColor;
	Data.Material.SpecularColor = Material->SpecularColor; Data.Material.SpecularFactor = Material->SpecularFactor;
	Data.Material.EmissiveColor = Material->EmissiveColor; Data.Material.EmissiveFactor = Material->EmissiveFactor;
	Data.Material.Shininess = Material->Shininess; Data.Material.ShadingModel = Material->ShadingModel;
	Data.Material.SamplerState = Material->SamplerState; Data.Material.BlendState = Material->BlendState;
	Data.Material.DiffuseTexture = (Material->Textures.IsValidIndex(0) && Material->Textures[0]) ? Material->Textures[0]->GetPath() : FString();
	Data.Material.NormalTexture = (Material->Textures.IsValidIndex(1) && Material->Textures[1]) ? Material->Textures[1]->GetPath() : FString();
	const auto Temporary = Path + ".editing";
	if (!FFbxAssetBake::WriteBaked(Temporary, Data, Status)) return;
	if (!MoveFileExW(FbxPath::FromString(Temporary).c_str(), FbxPath::FromString(Path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{ Status = "Could not replace material file. The previous asset is intact."; std::error_code E; fs::remove(FbxPath::FromString(Temporary), E); return; }
	FContentDrawerPanel::InvalidateMaterialThumbnail(Path);
	Status = "Saved";
}

void FAssetEditorPanel::OnRender()
{
	const FString Title = (Asset ? FbxPath::ToString(FbxPath::FromString(Path).filename()) : FString(GetPanelName())) + "###ImportedAssetEditor";
	bVisible = BeginPanel(Title.c_str());
	if (bVisible && !Asset)
	{
		ImGui::TextUnformatted("No asset is open.");
		ImGui::TextWrapped("Double-click a static mesh, texture, material, or skeleton in the Content Drawer to open it here.");
		ImGui::End();
		return;
	}
	if (bVisible)
	{
		ImGui::TextWrapped("%s", Path.c_str());
		if (auto* Material = Cast<UMaterial>(Asset))
		{
			if (ImGui::Button("Save Material")) SaveMaterial();
			ImGui::SameLine(); ImGui::TextUnformatted(Status.c_str());
			EditAssetColor("Diffuse / Factor", &Material->BaseColor.X, true);
			EditAssetColor("Specular", &Material->SpecularColor.X);
			ImGui::DragFloat("Specular Factor", &Material->SpecularFactor, .02f, 0, 100);
			EditAssetColor("Emissive", &Material->EmissiveColor.X);
			ImGui::DragFloat("Emissive Factor", &Material->EmissiveFactor, .02f, 0, 100);
			ImGui::DragFloat("Shininess", &Material->Shininess, .5f, 1, 512);
			int Model = static_cast<int>(Material->ShadingModel);
			const char* Models[] = {"Gouraud", "Lambert", "Blinn-Phong", "Unlit"};
			if (ImGui::Combo("Shading", &Model, Models, 4)) { Material->ShadingModel = static_cast<ELightingModel>(Model); Material->bOverrideShadingModel = true; }
			int Blend = Material->BlendState == EBlendState::AlphaBlend ? 1 : 0;
			if (ImGui::Combo("Blend", &Blend, "Opaque\0Alpha Blend\0")) Material->SetBlendState(Blend ? EBlendState::AlphaBlend : EBlendState::Opaque);
			ImGui::Separator();
			const int32 SlotCount = std::max(2, static_cast<int32>(Material->Textures.Num()));
			for (int32 Slot = 0; Slot < SlotCount; ++Slot)
			{
				const FString SlotLabel = (Slot == 0) ? FString("Diffuse Texture")
					: (Slot == 1) ? FString("Normal Texture")
					: ("Texture Slot " + std::to_string(Slot));
				DrawAssetMaterialTextureSlot(Material, Slot, SlotLabel.c_str());
			}
		}
		if (auto* Skeleton = Cast<USkeleton>(Asset))
		{
			ImGui::Text("Bones: %d", Skeleton->RefSkeleton.GetNum());
			for (int I = 0; I < Skeleton->RefSkeleton.GetNum(); ++I)
				ImGui::Text("%d  %s  (parent %d)", I, Skeleton->RefSkeleton.RefBoneInfo[I].Name.c_str(), Skeleton->RefSkeleton.RefBoneInfo[I].ParentIndex);
		}
		if (auto* Texture = Cast<UTexture2D>(Asset))
		{
			ImGui::Text("%u x %u", Texture->GetWidth(), Texture->GetHeight());
			if (Texture->GetResource())
			{
				const float Width = std::max(1.0f, std::min(ImGui::GetContentRegionAvail().x, EditorUIScale::Px(800.0f)));
				ImGui::Image(Texture->GetResource()->GetSRV(), ImVec2(Width, Width * Texture->GetHeight() / std::max(1u, Texture->GetWidth())));
			}
		}
		if (PreviewWorld)
		{
			ImVec2 Size = ImGui::GetContentRegionAvail(); Size.x = std::max(Size.x, 1.0f); Size.y = std::max(Size.y, EditorUIScale::Px(100.0f));
			Client.Resize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));
			if (Client.GetColorTarget()) ImGui::Image(Client.GetColorTarget()->GetSRV(), Size);
			const bool Hovered = ImGui::IsItemHovered();
			const bool bCaptured = Hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);
			float Wheel = ImGui::GetIO().MouseWheel;
			// 우클릭으로 카메라를 잡은 동안 휠은 이동 속도를 바꾼다.
			if (bCaptured && Wheel != 0.0f)
			{
				CameraSpeed = std::clamp(CameraSpeed + Wheel * 0.5f, 0.1f, 20.0f);
				Wheel = 0.0f;
			}
			Client.TickInputFromImGui(ImGui::GetIO().DeltaTime, bCaptured, Hovered,
				static_cast<int32>(Wheel * 120), 4.0f * CameraSpeed, 0.2f);
		}
	}
	ImGui::End();
}

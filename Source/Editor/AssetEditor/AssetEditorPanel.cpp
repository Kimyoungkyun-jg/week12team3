#include "EnginePCH.h"
#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/EditorUI/EditorUIScale.h"
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
#include "UObject/UObjectIterator.h"
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

void FAssetEditorPanel::NewMaterial(const FString& InPath, bool bParticleSprite)
{
	if (bDirty && Asset && !Asset->IsRetired())
	{
		SaveMaterial();
		if (bDirty)
		{
			return;
		}
	}

	Release();
	bDirty = false;
	Path = InPath;
	Status.clear();

	UMaterial* Template = UAssetManager::GetAssetByPath<UMaterial>(bParticleSprite ? "ParticleSpriteMaterial" : "DefaultMaterial");

	if (!Template)
	{
		Status = "Material template not found.";
		SetOpen(true);
		return;
	}

	UMaterial* NewMaterial = NewObject<UMaterial>();
	if (!NewMaterial)
	{
		Status = "Failed to create Material.";
		SetOpen(true);
		return;
	}

	NewMaterial->CopySettingsFrom(*Template);
	NewMaterial->bIsInstance = false;
	NewMaterial->Parent = nullptr;

	Asset = NewMaterial;
	bDirty = true;

	SaveMaterial();

	if (Status == "Saved")
	{
		OpenAsset(InPath);
	}
	else
	{
		SetOpen(true);
	}
}

void FAssetEditorPanel::NewMaterialInstance(const FString& InPath, const FString& ParentPath)
{
	if (bDirty && Asset && !Asset->IsRetired())
	{
		SaveMaterial();
		if (bDirty)
		{
			return;
		}
	}

	Release();
	bDirty = false;
	Path = InPath;
	Status.clear();

	UMaterial* Parent = UAssetManager::LoadMaterialAsset(ParentPath);

	if (!Parent)
	{
		Status = "Parent Material not found.";
		SetOpen(true);
		return;
	}

	Asset = UMaterial::CreateInstance(Parent);

	if (!Asset)
	{
		Status = "Failed to create Material Instance.";
		SetOpen(true);
		return;
	}

	bDirty = true;

	SaveMaterial();

	if (Status == "Saved")
	{
		OpenAsset(InPath);
	}
	else
	{
		SetOpen(true);
	}
}

void FAssetEditorPanel::SaveIfDirty()
{
	if (IsOpen() && bDirty)
	{
		SaveMaterial();
	}
}

void FAssetEditorPanel::OpenAsset(const FString& InPath)
{
	FString Error;
	auto* Loaded = UAssetManager::GetAssetByPath<URenderAsset>(InPath);

	if (!Loaded && FbxPath::FromString(InPath).extension() == ".material")
	{
		Loaded = UAssetManager::LoadMaterialAsset(InPath);
	}

	if (!Loaded && FbxPath::FromString(InPath).extension() == ".fbxbin")
	{
		Loaded = FFbxAssetLoader::LoadAsset(InPath, Error);
	}
	if (!Loaded) { HTR_LOG(Warning, "Cannot preview asset {}: {}", InPath, Error); return; }
	if (bDirty && Asset && Path != InPath)
	{
		SaveMaterial();
		if (bDirty)
		{
			return;
		}
	}
	Release();
	Path = InPath;
	Asset = Loaded;
	Status.clear();
	bDirty = false;
	SetOpen(true);
	UStaticMesh* Mesh = Cast<UStaticMesh>(Asset);
	UMaterial* Material = Cast<UMaterial>(Asset);

	if (Material)
	{
		if (Material->PSOType == EPSOType::Particle_Sprite ||
			Material->PSOType == EPSOType::Particle_AlphaBlend ||
			Material->PSOType == EPSOType::Particle_Additive)
		{
			return;
		}

		Mesh = UAssetManager::GetAssetByPath<UStaticMesh>("Sphere");
	}

	if (!Mesh)
	{
		return;
	}
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
	UMaterial* Material = Cast<UMaterial>(Asset);
	if (!Material)
	{
		return;
	}

	const FString Extension = FbxPath::ToString(FbxPath::FromString(Path).extension());

	if (Extension == ".material")
	{
		if (UAssetManager::SaveMaterialAsset(Path, *Material))
		{
			bDirty = false;
			Status = "Saved";
		}
		else
		{
			Status = "Failed to save Material.";
		}

		return;
	}

	if (Extension != ".fbxbin")
	{
		return;
	}

	FFbxAssetData Data;
	if (!FFbxAssetBake::ReadBaked(Path, Data, Status))
	{
		return;
	}

	Data.Material.DiffuseOrFactor = Material->BaseColor;
	Data.Material.SpecularColor = Material->SpecularColor;
	Data.Material.SpecularFactor = Material->SpecularFactor;
	Data.Material.EmissiveColor = Material->EmissiveColor;
	Data.Material.EmissiveFactor = Material->EmissiveFactor;
	Data.Material.Shininess = Material->Shininess;
	Data.Material.ShadingModel = Material->ShadingModel;
	Data.Material.SamplerState = Material->SamplerState;
	Data.Material.BlendState = Material->BlendState;

	const auto Temporary = Path + ".editing";

	if (!FFbxAssetBake::WriteBaked(Temporary, Data, Status))
	{
		return;
	}

	if (!MoveFileExW(FbxPath::FromString(Temporary).c_str(), FbxPath::FromString(Path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		Status = "Could not replace material file. The previous asset is intact.";
		std::error_code E;
		fs::remove(FbxPath::FromString(Temporary), E);
		return;
	}

	bDirty = false;
	Status = "Saved";
}

void FAssetEditorPanel::OnRender()
{
	const FString Title = (Asset ? FbxPath::ToString(FbxPath::FromString(Path).filename()) : FString(GetPanelName())) + "###ImportedAssetEditor";
	bVisible = BeginPanel(Title.c_str());
	bFocused = bVisible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	if (bFocused && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_S, false))
	{
		SaveMaterial();
	}
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
			UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);

			if (ImGui::Button("Save Material"))
			{
				SaveMaterial();
			}

			ImGui::SameLine();
			ImGui::TextUnformatted(bDirty ? "* Modified" : Status.c_str());

			ImGui::Separator();

			ImGui::TextUnformatted("Base Color");

			if (Instance)
			{
				bool bOverride = Instance->bOverrideBaseColor;

				if (ImGui::Checkbox("Override Base Color", &bOverride))
				{
					Instance->bOverrideBaseColor = bOverride;

					if (bOverride)
					{
						Instance->BaseColorOverride = Material->BaseColor;
					}
					else
					{
						Instance->RefreshFromParent();
					}

					bDirty = true;
				}

				ImGui::BeginDisabled(!Instance->bOverrideBaseColor);
			}

			if (EditAssetColor("Base Color", &Material->BaseColor.X, true))
			{
				bDirty = true;

				if (Instance)
				{
					Instance->BaseColorOverride = Material->BaseColor;
				}
			}

			if (Instance)
			{
				ImGui::EndDisabled();
			}

			if (!Instance)
			{
				ImGui::Separator();
				ImGui::TextUnformatted("Surface");

				if (EditAssetColor("Specular", &Material->SpecularColor.X))
				{
					bDirty = true;
				}

				if (ImGui::DragFloat("Specular Factor", &Material->SpecularFactor, 0.02f, 0.0f, 100.0f))
				{
					bDirty = true;
				}

				if (EditAssetColor("Emissive", &Material->EmissiveColor.X))
				{
					bDirty = true;
				}

				if (ImGui::DragFloat("Emissive Factor", &Material->EmissiveFactor, 0.02f, 0.0f, 100.0f))
				{
					bDirty = true;
				}

				if (ImGui::DragFloat("Shininess", &Material->Shininess, 0.5f, 1.0f, 512.0f))
				{
					bDirty = true;
				}

				int Model = static_cast<int>(Material->ShadingModel);
				const char* Models[] = { "Gouraud", "Lambert", "Blinn-Phong", "Unlit" };

				if (ImGui::Combo("Shading", &Model, Models, 4))
				{
					Material->ShadingModel = static_cast<ELightingModel>(Model);
					Material->bOverrideShadingModel = true;
					bDirty = true;
				}

				int Blend = Material->BlendState == EBlendState::AlphaBlend ? 1 : 0;

				if (ImGui::Combo("Blend", &Blend, "Opaque\0Alpha Blend\0"))
				{
					Material->SetBlendState(Blend ? EBlendState::AlphaBlend : EBlendState::Opaque);
					bDirty = true;
				}
			}

			if (FbxPath::FromString(Path).extension() == ".material")
			{
				ImGui::Separator();
				ImGui::TextUnformatted("Textures");

				if (Instance)
				{
					bool bOverride = Instance->bOverrideTexture0;

					if (ImGui::Checkbox("Override Texture 0", &bOverride))
					{
						Instance->bOverrideTexture0 = bOverride;

						if (bOverride)
						{
							Instance->Texture0Override = Material->Textures.IsEmpty() ? nullptr : Material->Textures[0];
						}
						else
						{
							Instance->RefreshFromParent();
						}

						bDirty = true;
					}

					ImGui::BeginDisabled(!Instance->bOverrideTexture0);
				}

				UTexture2D* CurrentTexture = Material->Textures.IsEmpty() ? nullptr : Material->Textures[0];
				const FString CurrentTexturePath = CurrentTexture ? CurrentTexture->GetPath() : "None";

				if (ImGui::BeginCombo("Texture Slot 0", CurrentTexturePath.c_str()))
				{
					if (ImGui::Selectable("None", CurrentTexture == nullptr))
					{
						if (Material->Textures.IsEmpty())
						{
							Material->Textures.Add(nullptr);
						}
						else
						{
							Material->Textures[0] = nullptr;
						}

						if (Instance)
						{
							Instance->Texture0Override = nullptr;
						}

						bDirty = true;
					}

					for (TObjectIterator<UTexture2D> It; It; ++It)
					{
						UTexture2D* Texture = *It;

						if (!Texture || Texture->IsRetired() || Texture->GetPath().empty())
						{
							continue;
						}

						const FString TexturePath = Texture->GetPath();

						if (ImGui::Selectable(TexturePath.c_str(), CurrentTexture == Texture))
						{
							if (Material->Textures.IsEmpty())
							{
								Material->Textures.Add(Texture);
							}
							else
							{
								Material->Textures[0] = Texture;
							}

							if (Instance)
							{
								Instance->Texture0Override = Texture;
							}

							bDirty = true;
						}
					}

					ImGui::EndCombo();
				}

				if (Instance)
				{
					ImGui::EndDisabled();
				}
			}
		}

		if (auto* Material = Cast<UMaterial>(Asset))
		{
			if (Material->PSOType == EPSOType::Particle_Sprite ||
				Material->PSOType == EPSOType::Particle_AlphaBlend ||
				Material->PSOType == EPSOType::Particle_Additive)
			{
				ImGui::TextDisabled("Particle Material preview is available through Particle System Editor.");
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

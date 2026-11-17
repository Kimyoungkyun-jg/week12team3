#include "EnginePCH.h"
#include "Editor/ParticleEditor/ParticleSystemEditorPanel.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Editor/EditorUI/EditorSort.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Particles/ParticleSystem.h"
#include "UObject/Casts.h"
#include "UObject/UObjectIterator.h"
#include "Components/ParticleSystemComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/BillboardComponent.h"
#include "GameFramework/Actor/ParticleSystemActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "Render/SkyboxRenderer.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Engine/World.h"
#include <implot.h>
#include <algorithm>
#include <filesystem>
#include <cctype>

namespace
{
	UParticleEmitter* AddDefaultEmitter(UParticleSystem* System)
	{
		UParticleEmitter* Emitter = System ? System->AddEmitter() : nullptr;
		if (!Emitter)
		{
			return nullptr;
		}

		constexpr float DefaultSpawnRates[] = { 10.0f, 6.0f, 2.0f };
		for (float SpawnRate : DefaultSpawnRates)
		{
			UParticleLODLevel* LOD = Emitter->AddLODLevel();
			LOD->GetOrCreateRequired()->Material = UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");
			LOD->GetOrCreateSpawn()->Rate = SpawnRate;

			LOD->AddModule(UParticleModuleLifetime::StaticClass());
			LOD->AddModule(UParticleModuleSize::StaticClass());
			LOD->AddModule(UParticleModuleVelocity::StaticClass());
			LOD->AddModule(UParticleModuleColor::StaticClass());
		}
		return Emitter;
	}
}

FParticleSystemEditorPanel::FParticleSystemEditorPanel() = default;
FParticleSystemEditorPanel::~FParticleSystemEditorPanel() { Release(); }

void FParticleSystemEditorPanel::Release()
{
	Client.SetWorld(nullptr);
	Skybox.reset();
	PreviewActor = nullptr;
	if (PreviewWorld)
	{
		PreviewWorld->ClearWorld();
		PreviewWorld->FlushPendingDestruction();
		if (GEngine) GEngine->DestroyWorld(PreviewWorld);
		PreviewWorld = nullptr;
	}
	SelectedModule = nullptr;
	SelectedEmitter = nullptr;
	EditingSystem = nullptr;
}

void FParticleSystemEditorPanel::CreateDefaultSystem()
{
	if (!AddDefaultEmitter(EditingSystem))
	{
		return;
	}

	EditingSystem->LODDistances.Add(0.0f);
	EditingSystem->LODDistances.Add(20.0f);
	EditingSystem->LODDistances.Add(50.0f);
}

void FParticleSystemEditorPanel::OpenAsset(const FString& InPath)
{
	Release();
	AssetPath = InPath;
	StatusMessage.clear();

	if (std::filesystem::exists(FbxPath::FromString(InPath)))
	{
		EditingSystem = UParticleSystem::LoadFromFile(InPath);
		if (!EditingSystem)
		{
			StatusMessage = "Load failed";
			HTR_LOG(Warning, "Failed to load particle system: {}", InPath);
			SetOpen(true);
			bFocusRequested = true;
			return;
		}
	}
	else
	{
		EditingSystem = NewObject<UParticleSystem>();
		CreateDefaultSystem();
		EditingSystem->SaveToFile(InPath);
		StatusMessage = "Created";
	}

	PreviewWorld = UWorld::CreateWorld(EWorldType::Editor, false);
	PreviewActor = PreviewWorld->SpawnActor<AParticleSystemActor>();
	if (PreviewActor && PreviewActor->GetParticleSystemComponent())
		PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);

	auto* Sun = PreviewWorld->SpawnActor<ADirectionalLightActor>();
	Sun->GetBillboardComponent()->SetVisibility(false);
	Sun->GetArrowComponent()->SetVisibility(false);
	Sun->GetLightComponent()->SetRelativeRotation(FRotator(40.0f, 150.0f, 0.0f));
	Sun->GetLightComponent()->SetIntensity(1.0f);

	auto* Sky = PreviewWorld->SpawnActor<ASkyLightActor>();
	Sky->GetBillboardComponent()->SetVisibility(false);
	Sky->GetLightComponent()->SetIntensity(0.3f);

	Client.SetWorld(PreviewWorld);
	Client.Resize(512, 512);
	Client.SetActive(true);
	Client.SetViewLocation(FVector(-3.0f, 0.0f, 1.5f));
	Client.SetViewRotation(FRotator(-15.0f, 0.0f, 0.0f));

	Skybox = MakeUnique<FSkyboxRenderer>();
	if (!Skybox->Init(EditorResourcePaths::AssetViewerSkyPanorama))
		Skybox.reset();

	SetOpen(true);
	bFocusRequested = true;
}

void FParticleSystemEditorPanel::SaveAsset()
{
	if (!EditingSystem || AssetPath.empty()) return;
	StatusMessage = EditingSystem->SaveToFile(AssetPath) ? "Saved" : "Save failed";
}

void FParticleSystemEditorPanel::Tick(float DeltaTime)
{
	if (!IsOpen() || !PreviewWorld) return;
	Client.TickInput(DeltaTime, false, false, 0, 4, 1);

	if (!bPaused && PreviewActor && PreviewActor->GetParticleSystemComponent())
	{
		const float ScaledDelta = std::max(0.0f, DeltaTime * SimSpeed);
		PreviewActor->GetParticleSystemComponent()->TickComponent(ScaledDelta);
	}
}

void FParticleSystemEditorPanel::RenderPreview(UEditorEngine* Engine)
{
	if (Engine && IsOpen() && bVisible && PreviewWorld && bShowViewport)
	{
		if (bViewportResizePending && PendingViewportWidth > 0 && PendingViewportHeight > 0)
		{
			if (Client.GetWidth() != PendingViewportWidth || Client.GetHeight() != PendingViewportHeight)
			{
				Client.Resize(PendingViewportWidth, PendingViewportHeight);
			}

			bViewportResizePending = false;
		}

		UParticleSystemComponent* PreviewComponent = PreviewActor ? PreviewActor->GetParticleSystemComponent() : nullptr;
		Engine->RenderPreviewViewport(&Client, PreviewWorld, nullptr, bShowSkybox ? Skybox.get() : nullptr, bShowGrid, bShowBounds ? PreviewComponent : nullptr, bShowOriginAxis);
	}
}

void FParticleSystemEditorPanel::RenderMenuBar()
{
	if (!ImGui::BeginMenuBar()) return;

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save", "Ctrl+S", false, EditingSystem != nullptr))
			SaveAsset();
		if (ImGui::MenuItem("Reload", nullptr, false, !AssetPath.empty()))
			OpenAsset(AssetPath);
		if (ImGui::MenuItem("Find in Content Drawer", nullptr, false, !AssetPath.empty()))
		{
			if (!FContentDrawerPanel::FocusAsset(AssetPath))
			{
				StatusMessage = "Asset not found in Content Drawer";
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Close"))
			SetOpen(false);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Edit"))
	{
		if (ImGui::MenuItem("Add Emitter", nullptr, false, EditingSystem != nullptr))
		{
			if (UParticleEmitter* Emitter = EditingSystem->AddEmitter())
			{
				const int32 DesiredLODCount = std::max(1, static_cast<int32>(EditingSystem->LODDistances.Num()));
				for (int32 Index = 0; Index < DesiredLODCount; ++Index)
				{
					Emitter->AddLODLevel();
				}
				if (PreviewActor && PreviewActor->GetParticleSystemComponent())
					PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
			}
		}
		if (ImGui::MenuItem("Restart Sim", nullptr, false, PreviewActor != nullptr))
		{
			if (PreviewActor && PreviewActor->GetParticleSystemComponent())
				PreviewActor->GetParticleSystemComponent()->ResetParticles();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Window"))
	{
		ImGui::MenuItem("Preview", nullptr, &bShowViewport);
		ImGui::MenuItem("Emitters", nullptr, &bShowEmitters);
		ImGui::MenuItem("Details", nullptr, &bShowDetails);
		ImGui::MenuItem("Curve Editor", nullptr, &bShowCurveEditor);
		ImGui::EndMenu();
	}

	ImGui::EndMenuBar();
}

void FParticleSystemEditorPanel::RenderToolbar()
{
	ImGui::BeginDisabled(EditingSystem == nullptr);
	if (ImGui::Button("Save"))
		SaveAsset();
	ImGui::EndDisabled();

	ImGui::SameLine();
	if (ImGui::Button("Restart Sim"))
	{
		if (PreviewActor && PreviewActor->GetParticleSystemComponent())
			PreviewActor->GetParticleSystemComponent()->ResetParticles();
	}

	ImGui::SameLine();
	if (ImGui::Button(bPaused ? "Play" : "Pause"))
		bPaused = !bPaused;

	ImGui::SameLine();
	ImGui::SetNextItemWidth(EditorUIScale::Px(90.0f));
	ImGui::SliderFloat("##SimSpeed", &SimSpeed, 0.1f, 2.0f, "%.2fx");

	ImGui::SameLine();
	ImGui::Checkbox("Bounds", &bShowBounds);
	ImGui::SameLine();
	ImGui::Checkbox("Origin", &bShowOriginAxis);
	ImGui::SameLine();
	ImGui::Checkbox("Grid", &bShowGrid);
	ImGui::SameLine();
	ImGui::Checkbox("Sky", &bShowSkybox);
	ImGui::SameLine();
	ImGui::ColorEdit3("##BgColor", BackgroundColor, ImGuiColorEditFlags_NoInputs);

	if (!StatusMessage.empty())
	{
		ImGui::SameLine();
		ImGui::TextDisabled("%s", StatusMessage.c_str());
	}
	ImGui::Separator();
}

void FParticleSystemEditorPanel::RenderViewportPanel(float Width, float Height)
{
	if (!ImGui::BeginChild("ParticleViewport", ImVec2(Width, Height), ImGuiChildFlags_Borders))
	{
		ImGui::EndChild();
		return;
	}

	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	const ImVec2 Size(std::max(1.0f, Avail.x), std::max(1.0f, Avail.y));
	const ImVec2 ImagePos = ImGui::GetCursorScreenPos();

	if (!bShowSkybox)
	{
		const ImU32 BgCol = ImGui::ColorConvertFloat4ToU32(
			ImVec4(BackgroundColor[0], BackgroundColor[1], BackgroundColor[2], 1.0f));
		ImGui::GetWindowDrawList()->AddRectFilled(
			ImagePos, ImVec2(ImagePos.x + Size.x, ImagePos.y + Size.y), BgCol);
	}

	PendingViewportWidth = static_cast<uint32>(Size.x);
	PendingViewportHeight = static_cast<uint32>(Size.y);

	if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		bViewportResizePending = true;
	}

	if (Client.GetColorTarget())
	{
		ImGui::Image(Client.GetColorTarget()->GetSRV(), Size);
	}

	const bool Hovered = ImGui::IsItemHovered();
	const bool bCaptured = Hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);
	float Wheel = ImGui::GetIO().MouseWheel;
	if (bCaptured && Wheel != 0.0f)
	{
		CameraSpeed = std::clamp(CameraSpeed + Wheel * 0.5f, 0.1f, 20.0f);
		Wheel = 0.0f;
	}
	Client.TickInputFromImGui(ImGui::GetIO().DeltaTime, bCaptured, Hovered,
		static_cast<int32>(Wheel * 120), 4.0f * CameraSpeed, 0.2f);

	// 뷰포트 우하단에 이미터별 활성 파티클 수를 표시한다.
	if (PreviewActor && PreviewActor->GetParticleSystemComponent())
	{
		const TArray<FParticleEmitterInstance>& Instances =
			PreviewActor->GetParticleSystemComponent()->GetEmitterInstances();
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const float LineH = ImGui::GetTextLineHeightWithSpacing();
		float Y = ImagePos.y + Size.y - LineH * static_cast<float>(Instances.Num()) - 6.0f;
		for (int32 Index = 0; Index < static_cast<int32>(Instances.Num()); ++Index)
		{
			const FParticleEmitterInstance& Instance = Instances[Index];
			const char* EmitterName = Instance.SpriteTemplate ? Instance.SpriteTemplate->EmitterName.c_str() : "Emitter";

			char Buf[128];
			const bool bGpuEmitter = Instance.CurrentLODLevel && Instance.CurrentLODLevel->TypeDataModule && Instance.CurrentLODLevel->TypeDataModule->GetEmitterType() == EParticleEmitterType::GpuSprite;

			if (bGpuEmitter)
			{
				snprintf(Buf, sizeof(Buf), "%s | GPU Capacity: %d", EmitterName, Instance.MaxActiveParticles);
			}
			else
			{
				snprintf(Buf, sizeof(Buf), "%s | %d / %d", EmitterName, Instance.ActiveParticles, Instance.MaxActiveParticles);
			}

			const ImVec2 TextSize = ImGui::CalcTextSize(Buf);
			DrawList->AddText(ImVec2(ImagePos.x + Size.x - TextSize.x - 10.0f, Y), IM_COL32(255, 160, 120, 255), Buf);
			Y += LineH;
		}
	}

	ImGui::EndChild();
}

void FParticleSystemEditorPanel::RenderEmittersPanel(float Width, float Height)
{
	if (!ImGui::BeginChild("ParticleEmitters", ImVec2(Width, Height), ImGuiChildFlags_Borders))
	{
		ImGui::EndChild();
		return;
	}

	ImGui::TextUnformatted("EMITTERS");

	if (ImGui::Button("+ Add Emitter", ImVec2(-1.0f, 0.0f)) && EditingSystem)
	{
		if (UParticleEmitter* NewEmitter = EditingSystem->AddEmitter())
		{
			const int32 DesiredLODCount = std::max(1, static_cast<int32>(EditingSystem->LODDistances.Num()));
			for (int32 Index = 0; Index < DesiredLODCount; ++Index)
			{
				NewEmitter->AddLODLevel();
			}

			SelectedEmitter = NewEmitter;
			SelectedModule = nullptr;
			SelectedLODIndex = 0;
			SelectedCurveTrackIndex = 0;
			SelectedCurveKeyId = -1;

			if (PreviewActor && PreviewActor->GetParticleSystemComponent())
			{
				PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
			}
		}
	}

	ImGui::Separator();

	if (!EditingSystem)
	{
		ImGui::TextDisabled("No Particle System");
		ImGui::EndChild();
		return;
	}

	const bool bSystemSelected = SelectedEmitter == nullptr && SelectedModule == nullptr;

	if (ImGui::Selectable("Particle System", bSystemSelected, ImGuiSelectableFlags_SpanAvailWidth))
	{
		SelectedEmitter = nullptr;
		SelectedModule = nullptr;
		SelectedLODIndex = 0;
		SelectedCurveTrackIndex = 0;
		SelectedCurveKeyId = -1;
	}

	ImGui::Separator();

	const TArray<UParticleEmitter*>& Emitters = EditingSystem->GetEmitters();
	UParticleEmitter* EmitterToRemove = nullptr;

	if (Emitters.IsEmpty())
	{
		ImGui::TextDisabled("No emitters. Add an emitter to get started.");
	}

	for (int32 EmitterIdx = 0; EmitterIdx < static_cast<int32>(Emitters.Num()); ++EmitterIdx)
	{
		UParticleEmitter* Emitter = Emitters[EmitterIdx];
		if (!Emitter)
		{
			continue;
		}

		ImGui::PushID(Emitter);

		const bool bEmitterSelected = (SelectedEmitter == Emitter);
		const ImGuiTreeNodeFlags EmitterFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | (bEmitterSelected ? ImGuiTreeNodeFlags_Selected : 0);

		const bool bEmitterExpanded = ImGui::TreeNodeEx("EmitterNode", EmitterFlags, "%s", Emitter->EmitterName.c_str());

		if (ImGui::IsItemClicked() && SelectedEmitter != Emitter)
		{
			SelectedEmitter = Emitter;
			SelectedModule = nullptr;
			SelectedLODIndex = 0;
			SelectedCurveTrackIndex = 0;
			SelectedCurveKeyId = -1;
		}

		if (bEmitterExpanded)
		{
			const TArray<UParticleLODLevel*>& LODLevels = Emitter->GetLODLevels();

			if (!LODLevels.IsEmpty())
			{
				SelectedLODIndex = std::clamp(SelectedLODIndex, 0, static_cast<int32>(LODLevels.Num()) - 1);

				char LODLabel[32];
				snprintf(LODLabel, sizeof(LODLabel), "LOD %d", SelectedLODIndex);

				ImGui::TextDisabled("LOD LEVEL");
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::BeginCombo("##EmitterLODLevel", LODLabel))
				{
					for (int32 Index = 0; Index < static_cast<int32>(LODLevels.Num()); ++Index)
					{
						char OptionLabel[32];
						snprintf(OptionLabel, sizeof(OptionLabel), "LOD %d", Index);

						if (ImGui::Selectable(OptionLabel, SelectedLODIndex == Index))
						{
							SelectedEmitter = Emitter;
							SelectedModule = nullptr;
							SelectedLODIndex = Index;
							SelectedCurveTrackIndex = 0;
							SelectedCurveKeyId = -1;
						}
					}

					ImGui::EndCombo();
				}
			}

			UParticleLODLevel* LOD = LODLevels.IsEmpty() ? nullptr : LODLevels[SelectedLODIndex];

			if (ImGui::Button("Remove Emitter", ImVec2(-1.0f, 0.0f)))
			{
				EmitterToRemove = Emitter;
			}

			if (LOD)
			{
				ImGui::Checkbox("Enabled", &LOD->bEnabled);
				ImGui::SameLine();
				ImGui::Checkbox("Solo", &Emitter->bIsSolo);

				ImGui::Separator();

				int TypeIdx = 0;
				UParticleModuleTypeDataBase* TypeData = LOD->GetOrCreateTypeData();
				const EParticleEmitterType CurType = TypeData->GetEmitterType();

				if (CurType == EParticleEmitterType::GpuSprite)
				{
					TypeIdx = 1;
				}
				else if (CurType == EParticleEmitterType::Mesh)
				{
					TypeIdx = 2;
				}

				ImGui::TextDisabled("EMITTER TYPE");
				ImGui::SetNextItemWidth(-1.0f);

				if (ImGui::Combo("##EmitterType", &TypeIdx, "Sprite\0GPU Sprite\0Mesh\0"))
				{
					UClass* TargetClass = UParticleModuleTypeDataSprite::StaticClass();

					if (TypeIdx == 1)
					{
						TargetClass = UParticleModuleTypeDataGpuSprite::StaticClass();
					}
					else if (TypeIdx == 2)
					{
						TargetClass = UParticleModuleTypeDataMesh::StaticClass();
					}

					const bool bWasTypeDataSelected = (SelectedModule == TypeData);
					UParticleModuleTypeDataBase* NewTypeData = LOD->SetTypeData(TargetClass);

					if (auto* NewMeshType = Cast<UParticleModuleTypeDataMesh>(NewTypeData))
					{
						if (!NewMeshType->Mesh)
						{
							NewMeshType->Mesh = UAssetManager::GetAssetByPath<UStaticMesh>("Cube");
						}

						LOD->GetOrCreateRequired()->Material = nullptr;
					}

					if (bWasTypeDataSelected)
					{
						SelectedModule = NewTypeData;
					}

					if (PreviewActor && PreviewActor->GetParticleSystemComponent())
					{
						PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
					}
				}

				ImGui::Spacing();

				UParticleModule* ModuleToRemove = nullptr;
				UParticleModule* ModuleToMove = nullptr;
				UParticleModule* MoveTargetModule = nullptr;
				bool bMoveAfterTarget = false;

				auto DrawModuleRow = [&](UParticleModule* Mod, const char* Stage, bool bRemovable)
				{
					if (!Mod)
					{
						return;
					}

					ImGui::PushID(Mod);

					if (ImGui::Checkbox("##Enabled", &Mod->bEnabled))
					{
						if (PreviewActor && PreviewActor->GetParticleSystemComponent())
						{
							PreviewActor->GetParticleSystemComponent()->UpdateEmitterStrides();
						}
					}

					ImGui::SameLine();

					const float RowWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
					const char* DisplayName = Mod->GetDisplayName();
					const float TextWidth = std::max(1.0f, RowWidth - EditorUIScale::Px(12.0f));

					const ImVec2 NameSize = ImGui::CalcTextSize(DisplayName, nullptr, false, TextWidth);
					const ImVec2 StageSize = ImGui::CalcTextSize(Stage, nullptr, false, TextWidth);
					const float RowHeight = NameSize.y + StageSize.y + EditorUIScale::Px(10.0f);

					const bool bSelected = (SelectedModule == Mod);

					if (ImGui::Selectable("##ModuleRow", bSelected, 0, ImVec2(RowWidth, RowHeight)))
					{
						if (SelectedModule != Mod)
						{
							SelectedCurveTrackIndex = 0;
							SelectedCurveKeyId = -1;
						}

						SelectedEmitter = Emitter;
						SelectedModule = Mod;
					}

					const ImVec2 RowMin = ImGui::GetItemRectMin();
					const ImVec2 TextPosition(RowMin.x + EditorUIScale::Px(6.0f), RowMin.y + EditorUIScale::Px(3.0f));

					ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), TextPosition, ImGui::GetColorU32(ImGuiCol_Text), DisplayName, nullptr, TextWidth);
					ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(TextPosition.x, TextPosition.y + NameSize.y + EditorUIScale::Px(2.0f)), ImGui::GetColorU32(ImGuiCol_TextDisabled), Stage, nullptr, TextWidth);

					const ImVec2 RowMax = ImGui::GetItemRectMax();

					ImGui::PopID();
				};

				if (ImGui::CollapsingHeader("Emitter Setup", ImGuiTreeNodeFlags_DefaultOpen))
				{
					DrawModuleRow(LOD->GetOrCreateTypeData(), "RENDER", false);
					DrawModuleRow(LOD->GetOrCreateRequired(), "SETTINGS", false);
				}

				if (ImGui::CollapsingHeader("Spawn", ImGuiTreeNodeFlags_DefaultOpen))
				{
					DrawModuleRow(LOD->GetOrCreateSpawn(), "SPAWN", false);
				}

				if (ImGui::CollapsingHeader("Modules", ImGuiTreeNodeFlags_DefaultOpen))
				{
					for (UParticleModule* Mod : LOD->Modules)
					{
						if (!Mod)
						{
							continue;
						}

						const char* Stage = Mod->RunsOnSpawn()
							? (Mod->RunsOnUpdate() ? "SPAWN + UPDATE" : "SPAWN")
							: (Mod->RunsOnUpdate() ? "UPDATE" : "CUSTOM");

						DrawModuleRow(Mod, Stage, true);
					}

					if (ImGui::Button("+ Add Module", ImVec2(-1.0f, 0.0f)))
					{
						ModuleSearchText[0] = '\0';
						ImGui::OpenPopup("AddModulePopup");
					}

					if (ImGui::BeginPopup("AddModulePopup"))
					{
						ImGui::SetNextItemWidth(-1.0f);
						ImGui::InputTextWithHint("##ModuleSearch", "Search modules...", ModuleSearchText, sizeof(ModuleSearchText));

						ImGui::Separator();

						struct FModuleOption
						{
							const char* Name;
							const char* Category;
							UClass* ModuleClass;
						};

						const FModuleOption Options[] = {
							{ "Lifetime", "Particle Initialization", UParticleModuleLifetime::StaticClass() },
							{ "Initial Location", "Particle Initialization", UParticleModuleLocation::StaticClass() },
							{ "Initial Velocity", "Particle Initialization", UParticleModuleVelocity::StaticClass() },
							{ "Initial Rotation", "Particle Initialization", UParticleModuleRotation::StaticClass() },
							{ "Initial Color", "Appearance", UParticleModuleColor::StaticClass() },
							{ "Initial Size", "Appearance", UParticleModuleSize::StaticClass() },
							{ "Initial Size (Random)", "Appearance", UParticleModuleInitialSize::StaticClass() },
							{ "Color Over Life", "Particle Update", UParticleModuleColorOverLife::StaticClass() },
							{ "Size By Life", "Particle Update", UParticleModuleSizeByLife::StaticClass() },
							{ "SubImage Index", "Particle Update", UParticleModuleSubUV::StaticClass() }
						};

						FString SearchQuery = ModuleSearchText;
						std::transform(SearchQuery.begin(), SearchQuery.end(), SearchQuery.begin(), [](unsigned char C)
							{ return static_cast<char>(std::tolower(C)); });

						FString CurrentCategory;
						bool bFoundModule = false;

						for (const FModuleOption& Option : Options)
						{
							FString SearchTarget = Option.Name;
							SearchTarget += " ";
							SearchTarget += Option.Category;

							std::transform(SearchTarget.begin(), SearchTarget.end(), SearchTarget.begin(), [](unsigned char C)
								{ return static_cast<char>(std::tolower(C)); });

							if (!SearchQuery.empty() && SearchTarget.find(SearchQuery) == FString::npos)
							{
								continue;
							}

							if (CurrentCategory != Option.Category)
							{
								if (!CurrentCategory.empty())
								{
									ImGui::Separator();
								}

								CurrentCategory = Option.Category;
								ImGui::TextDisabled("%s", CurrentCategory.c_str());
							}

							bFoundModule = true;

							if (ImGui::Selectable(Option.Name))
							{
								if (UParticleModule* Added = LOD->AddModule(Option.ModuleClass))
								{
									SelectedEmitter = Emitter;
									SelectedModule = Added;
									SelectedCurveTrackIndex = 0;
									SelectedCurveKeyId = -1;

									if (PreviewActor && PreviewActor->GetParticleSystemComponent())
									{
										PreviewActor->GetParticleSystemComponent()->UpdateEmitterStrides();
									}
								}

								ImGui::CloseCurrentPopup();
							}
						}

						if (!bFoundModule)
						{
							ImGui::TextDisabled("No matching modules.");
						}

						ImGui::EndPopup();
					}
				}

				if (ModuleToRemove)
				{
					if (SelectedModule == ModuleToRemove)
					{
						SelectedModule = nullptr;
					}

					LOD->RemoveModule(ModuleToRemove);

					if (PreviewActor && PreviewActor->GetParticleSystemComponent())
					{
						PreviewActor->GetParticleSystemComponent()->UpdateEmitterStrides();
					}
				}
				else if (ModuleToMove && MoveTargetModule && ModuleToMove != MoveTargetModule)
				{
					int32 FromIndex = -1;
					int32 ToIndex = -1;

					for (int32 Index = 0; Index < static_cast<int32>(LOD->Modules.Num()); ++Index)
					{
						if (LOD->Modules[Index] == ModuleToMove)
						{
							FromIndex = Index;
						}

						if (LOD->Modules[Index] == MoveTargetModule)
						{
							ToIndex = Index;
						}
					}

					if (FromIndex >= 0 && ToIndex >= 0)
					{
						int32 FinalIndex = ToIndex + (bMoveAfterTarget ? 1 : 0);
						if (FromIndex < FinalIndex)
						{
							--FinalIndex;
						}

						if (LOD->MoveModule(FromIndex, FinalIndex))
						{
							if (PreviewActor && PreviewActor->GetParticleSystemComponent())
							{
								PreviewActor->GetParticleSystemComponent()->UpdateEmitterStrides();
							}
						}
					}
				}
			}

			ImGui::TreePop();
		}

		ImGui::PopID();
	}

	if (EmitterToRemove)
	{
		if (SelectedEmitter == EmitterToRemove)
		{
			SelectedEmitter = nullptr;
			SelectedModule = nullptr;
		}

		EditingSystem->RemoveEmitter(EmitterToRemove);

		if (PreviewActor && PreviewActor->GetParticleSystemComponent())
		{
			PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
		}
	}

	ImGui::EndChild();
}

void FParticleSystemEditorPanel::RenderDetailsPanel(float Width, float Height)
{
	if (!ImGui::BeginChild("ParticleDetails", ImVec2(Width, Height), ImGuiChildFlags_Borders))
	{
		ImGui::EndChild();
		return;
	}

	ImGui::TextUnformatted("Properties");
	ImGui::Separator();

	auto DrawSection = [](const char* Name)
	{
		ImGui::Spacing();
		ImGui::TextDisabled("%s", Name);
		ImGui::Separator();
	};

	auto DrawFloatProperty = [](const char* Label, const char* Id, float* Value, float Speed, float Min, float Max)
	{
		ImGui::TextUnformatted(Label);
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::DragFloat(Id, Value, Speed, Min, Max, "%.3f");
	};

	auto DrawTypeDataMeshPicker = [&](UParticleLODLevel* LOD, UParticleModuleTypeDataMesh* MeshType)
	{
		if (!LOD || !MeshType) return;
		if (FContentDrawerPanel::DrawStaticMeshPicker("Static Mesh", MeshType->Mesh, "(None)"))
		{
			LOD->GetOrCreateRequired()->Material = nullptr;
			if (PreviewActor && PreviewActor->GetParticleSystemComponent())
				PreviewActor->GetParticleSystemComponent()->MarkRenderStateDirty();
		}
	};

	if (SelectedModule)
	{
		ImGui::Text("Module: %s", SelectedModule->GetDisplayName());
		if (ImGui::Checkbox("Enabled", &SelectedModule->bEnabled))
		{
			if (PreviewActor && PreviewActor->GetParticleSystemComponent())
				PreviewActor->GetParticleSystemComponent()->UpdateEmitterStrides();
		}
		ImGui::Separator();

		if (auto* MeshType = Cast<UParticleModuleTypeDataMesh>(SelectedModule))
		{
			DrawSection("RENDERER");
			UParticleLODLevel* OwnerLOD = nullptr;

			if (EditingSystem)
			{
				for (UParticleEmitter* Emitter : EditingSystem->GetEmitters())
				{
					if (!Emitter)
					{
						continue;
					}

					for (UParticleLODLevel* CandidateLOD : Emitter->GetLODLevels())
					{
						if (CandidateLOD && CandidateLOD->TypeDataModule == MeshType)
						{
							OwnerLOD = CandidateLOD;
							break;
						}
					}

					if (OwnerLOD)
					{
						break;
					}
				}
			}

			DrawTypeDataMeshPicker(OwnerLOD, MeshType);
		}
		else if (Cast<UParticleModuleTypeDataSprite>(SelectedModule))
		{
			DrawSection("RENDERER");
			ImGui::TextDisabled("SPRITE RENDERER");
			ImGui::TextWrapped("Renders particles as camera-facing sprites.");
		}
		else if (Cast<UParticleModuleTypeDataGpuSprite>(SelectedModule))
		{
			DrawSection("RENDERER");
			ImGui::TextDisabled("GPU SPRITE RENDERER");
			ImGui::TextWrapped("Uses the GPU sprite rendering and simulation path.");
		}
		else if (auto* Req = Cast<UParticleModuleRequired>(SelectedModule))
		{
			DrawSection("EMITTER SETTINGS");
			ImGui::DragInt("Max Particles", &Req->MaxParticles, 1.0f, 1, 100000);
			ImGui::DragFloat("Emitter Duration", &Req->EmitterDuration, 0.05f, 0.0f, 300.0f);
			ImGui::Checkbox("Looping", &Req->bLooping);
			ImGui::Checkbox("Use Local Space", &Req->bUseLocalSpace);

			UParticleLODLevel* OwnerLOD = nullptr;
			if (EditingSystem)
			{
				for (UParticleEmitter* Emitter : EditingSystem->GetEmitters())
				{
					if (!Emitter)
						continue;
					for (UParticleLODLevel* LOD : Emitter->GetLODLevels())
					{
						if (LOD && LOD->RequiredModule == Req)
						{
							OwnerLOD = LOD;
							break;
						}
					}
					if (OwnerLOD)
						break;
				}
			}

			DrawSection("RENDERING");
			const bool bMeshEmitter = OwnerLOD && Cast<UParticleModuleTypeDataMesh>(OwnerLOD->TypeDataModule);
			const uint32 UsageFlag = static_cast<uint32>(bMeshEmitter ? EMaterialUsage::ParticleMesh : EMaterialUsage::ParticleSprite);
			UMaterial* FallbackMaterial = (!Req->Material && !bMeshEmitter)
				? UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial")
				: nullptr;
			if (FContentDrawerPanel::DrawMaterialPicker("Material", Req->Material, "(Default)", FallbackMaterial, UsageFlag))
			{
				if (PreviewActor && PreviewActor->GetParticleSystemComponent())
					PreviewActor->GetParticleSystemComponent()->MarkRenderStateDirty();
			}
		}
		else if (auto* Spawn = Cast<UParticleModuleSpawn>(SelectedModule))
		{
			DrawSection("SPAWN");
			ImGui::DragFloat("Spawn Rate", &Spawn->Rate, 0.5f, 0.0f, 10000.0f);
			if (ImGui::SmallButton("+ Burst"))
				Spawn->Bursts.Add(FParticleBurst{});
			for (int32 i = 0; i < static_cast<int32>(Spawn->Bursts.Num()); ++i)
			{
				ImGui::PushID(i);
				ImGui::DragFloat("Time", &Spawn->Bursts[i].Time, 0.01f, 0.0f, 60.0f);
				ImGui::DragInt("Count", &Spawn->Bursts[i].Count, 1.0f, 0, 10000);
				if (ImGui::SmallButton("Remove Burst"))
				{
					Spawn->Bursts.RemoveAt(static_cast<uint32>(i));
					ImGui::PopID();
					break;
				}
				ImGui::PopID();
			}
		}
		else if (auto* Life = Cast<UParticleModuleLifetime>(SelectedModule))
		{
			DrawSection("LIFETIME");
			ImGui::DragFloat("Min", &Life->Min, 0.05f, 0.01f, 60.0f);
			ImGui::DragFloat("Max", &Life->Max, 0.05f, 0.01f, 60.0f);
		}
		else if (auto* Loc = Cast<UParticleModuleLocation>(SelectedModule))
		{
			DrawSection("INITIAL LOCATION");
			char ParamBuf[128] = {};
			strncpy_s(ParamBuf, Loc->ParameterName.c_str(), _TRUNCATE);
			if (ImGui::InputText("Parameter Name", ParamBuf, sizeof(ParamBuf)))
				Loc->ParameterName = ParamBuf;
			ImGui::DragFloat3("Min", &Loc->Min.X, 0.05f);
			ImGui::DragFloat3("Max", &Loc->Max.X, 0.05f);
		}
		else if (auto* Vel = Cast<UParticleModuleVelocity>(SelectedModule))
		{
			DrawSection("INITIAL VELOCITY");
			char ParamBuf[128] = {};
			strncpy_s(ParamBuf, Vel->ParameterName.c_str(), _TRUNCATE);
			if (ImGui::InputText("Parameter Name", ParamBuf, sizeof(ParamBuf)))
				Vel->ParameterName = ParamBuf;
			ImGui::DragFloat3("Min", &Vel->Min.X, 0.05f);
			ImGui::DragFloat3("Max", &Vel->Max.X, 0.05f);
		}
		else if (auto* Col = Cast<UParticleModuleColor>(SelectedModule))
		{
			DrawSection("INITIAL COLOR");
			char ParamBuf[128] = {};
			strncpy_s(ParamBuf, Col->ParameterName.c_str(), _TRUNCATE);
			if (ImGui::InputText("Parameter Name", ParamBuf, sizeof(ParamBuf)))
				Col->ParameterName = ParamBuf;
			float S[4] = { Col->Start.R / 255.0f, Col->Start.G / 255.0f, Col->Start.B / 255.0f, Col->Start.A / 255.0f };
			float E[4] = { Col->End.R / 255.0f, Col->End.G / 255.0f, Col->End.B / 255.0f, Col->End.A / 255.0f };
			if (ImGui::ColorEdit4("Start Color", S))
				Col->Start = FColor(static_cast<uint8>(S[0] * 255.0f), static_cast<uint8>(S[1] * 255.0f), static_cast<uint8>(S[2] * 255.0f), static_cast<uint8>(S[3] * 255.0f));
			if (ImGui::ColorEdit4("End Color", E))
				Col->End = FColor(static_cast<uint8>(E[0] * 255.0f), static_cast<uint8>(E[1] * 255.0f), static_cast<uint8>(E[2] * 255.0f), static_cast<uint8>(E[3] * 255.0f));
		}
		else if (auto* ColOverLife = Cast<UParticleModuleColorOverLife>(SelectedModule))
		{
			DrawSection("COLOR OVER LIFE");
			char ParamBuf[128] = {};
			strncpy_s(ParamBuf, ColOverLife->ParameterName.c_str(), _TRUNCATE);
			if (ImGui::InputText("Parameter Name", ParamBuf, sizeof(ParamBuf)))
				ColOverLife->ParameterName = ParamBuf;
			ImGui::Checkbox("Use Fade In/Out", &ColOverLife->bUseFadeInOut);
			if (ColOverLife->bUseFadeInOut)
			{
				ImGui::SliderFloat("Fade In End", &ColOverLife->FadeInEnd, 0.0f, 1.0f);
				ImGui::SliderFloat("Fade Out Start", &ColOverLife->FadeOutStart, 0.0f, 1.0f);
			}
		}
		else if (auto* Sz = Cast<UParticleModuleSize>(SelectedModule))
		{
			DrawSection("INITIAL SIZE");
			ImGui::DragFloat3("Start Size", &Sz->Start.X, 0.05f, 0.0f, 100.0f);
			ImGui::DragFloat3("End Size", &Sz->End.X, 0.05f, 0.0f, 100.0f);
		}
		else if (auto* InitSz = Cast<UParticleModuleInitialSize>(SelectedModule))
		{
			DrawSection("RANDOM SIZE");
			ImGui::Checkbox("Uniform Scale", &InitSz->bUniformScale);
			if (InitSz->bUniformScale)
			{
				if (ImGui::DragFloat("Min Size", &InitSz->MinSize.X, 0.05f, 0.0f, 100.0f))
				{
					InitSz->MinSize.Y = InitSz->MinSize.X;
					InitSz->MinSize.Z = InitSz->MinSize.X;
				}
				if (ImGui::DragFloat("Max Size", &InitSz->MaxSize.X, 0.05f, 0.0f, 100.0f))
				{
					InitSz->MaxSize.Y = InitSz->MaxSize.X;
					InitSz->MaxSize.Z = InitSz->MaxSize.X;
				}
			}
			else
			{
				ImGui::DragFloat3("Min Size", &InitSz->MinSize.X, 0.05f, 0.0f, 100.0f);
				ImGui::DragFloat3("Max Size", &InitSz->MaxSize.X, 0.05f, 0.0f, 100.0f);
			}
		}
		else if (auto* Rot = Cast<UParticleModuleRotation>(SelectedModule))
		{
			DrawSection("INITIAL ROTATION");
			ImGui::DragFloat("Min", &Rot->Min, 0.05f);
			ImGui::DragFloat("Max", &Rot->Max, 0.05f);
			ImGui::DragFloat("Rate Min", &Rot->RateMin, 0.05f);
			ImGui::DragFloat("Rate Max", &Rot->RateMax, 0.05f);
		}
		else if (auto* Sub = Cast<UParticleModuleSubUV>(SelectedModule))
		{
			DrawSection("SUB UV");
			ImGui::DragInt("Horizontal", &Sub->SubImagesHorizontal, 1.0f, 1, 64);
			ImGui::DragInt("Vertical", &Sub->SubImagesVertical, 1.0f, 1, 64);
			ImGui::Checkbox("Linear Over Life", &Sub->bUseLinearOverLife);
			ImGui::DragFloat("Play Rate", &Sub->PlayRate, 0.05f, 0.0f, 20.0f);
		}
	}
	else if (SelectedEmitter)
	{
		char NameBuf[128] = {};
		strncpy_s(NameBuf, SelectedEmitter->EmitterName.c_str(), _TRUNCATE);
		if (ImGui::InputText("Emitter Name", NameBuf, sizeof(NameBuf)))
			SelectedEmitter->EmitterName = NameBuf;
		ImGui::Checkbox("Solo", &SelectedEmitter->bIsSolo);
		ImGui::DragFloat("Play Rate", &SelectedEmitter->PlayRate, 0.05f, 0.0f, 20.0f);
		const TArray<UParticleLODLevel*>& LODLevels = SelectedEmitter->GetLODLevels();

		if (!LODLevels.IsEmpty())
		{
			SelectedLODIndex = std::clamp(SelectedLODIndex, 0, static_cast<int32>(LODLevels.Num()) - 1);

			UParticleLODLevel* LOD = LODLevels[SelectedLODIndex];

			if (auto* MeshType = LOD ? Cast<UParticleModuleTypeDataMesh>(LOD->TypeDataModule) : nullptr)
			{
				DrawSection("RENDERER");
				DrawTypeDataMeshPicker(LOD, MeshType);
			}
		}
	}
	else if (EditingSystem)
	{
		DrawSection("SYSTEM");
		DrawFloatProperty("Update Time FPS", "##SystemUpdateTimeFPS", &EditingSystem->UpdateTimeFPS, 1.0f, 1.0f, 240.0f);
		DrawFloatProperty("Warmup Time", "##SystemWarmupTime", &EditingSystem->WarmupTime, 0.05f, 0.0f, 60.0f);
		DrawFloatProperty("Warmup Tick Rate", "##SystemWarmupTickRate", &EditingSystem->WarmupTickRate, 0.01f, 0.0f, 1.0f);
		DrawFloatProperty("Seconds Before Inactive", "##SystemSecondsBeforeInactive", &EditingSystem->SecondsBeforeInactive, 0.1f, 0.0f, 60.0f);
		DrawFloatProperty("Play Rate", "##SystemPlayRate", &EditingSystem->PlayRate, 0.05f, 0.0f, 20.0f);

		DrawSection("BOUNDS");

		bool bBoundsChanged = ImGui::Checkbox("Use Fixed Relative Bounds", &EditingSystem->bUseFixedRelativeBoundingBox);
		if (EditingSystem->bUseFixedRelativeBoundingBox)
		{
			ImGui::TextUnformatted("Bounds Min");
			ImGui::SetNextItemWidth(-1.0f);
			bBoundsChanged |= ImGui::DragFloat3("##SystemBoundsMin", &EditingSystem->FixedBoundsMin.X, 0.5f);

			ImGui::TextUnformatted("Bounds Max");
			ImGui::SetNextItemWidth(-1.0f);
			bBoundsChanged |= ImGui::DragFloat3("##SystemBoundsMax", &EditingSystem->FixedBoundsMax.X, 0.5f);
		}

		if (bBoundsChanged && PreviewActor && PreviewActor->GetParticleSystemComponent())
		{
			PreviewActor->GetParticleSystemComponent()->MarkRenderStateDirty();
		}

		DrawSection("LOD DISTANCES");

		for (int32 Index = 0; Index < static_cast<int32>(EditingSystem->LODDistances.Num()); ++Index)
		{
			char Label[32];
			snprintf(Label, sizeof(Label), "LOD %d Distance", Index);

			const float MinDistance = Index == 0 ? 0.0f : EditingSystem->LODDistances[Index - 1] + 0.01f;
			const float MaxDistance = Index + 1 < static_cast<int32>(EditingSystem->LODDistances.Num()) ? EditingSystem->LODDistances[Index + 1] - 0.01f : 100000.0f;

			if (MaxDistance >= MinDistance)
			{
				ImGui::PushID(Index);
				DrawFloatProperty(Label, "##LODDistance", &EditingSystem->LODDistances[Index], 0.5f, MinDistance, MaxDistance);
				ImGui::PopID();
			}
		}
	}

	ImGui::EndChild();
}

void FParticleSystemEditorPanel::RenderCurveEditorPanel(float Width, float Height)
{
	if (ImGui::BeginChild("ParticleCurveEditor", ImVec2(Width, Height), ImGuiChildFlags_Borders))
	{
		TArray<FParticleCurveTrack> Tracks;
		if (SelectedModule)
			SelectedModule->CollectCurves(Tracks);

		if (!SelectedModule || Tracks.IsEmpty())
		{
			ImGui::TextUnformatted("Curve Editor");
			ImGui::Separator();
			ImGui::TextDisabled("Select a module with curves (SizeByLife, ColorOverLife, SubUV).");
			ImGui::EndChild();
			return;
		}

		SelectedCurveTrackIndex = std::clamp(SelectedCurveTrackIndex, 0, static_cast<int32>(Tracks.Num()) - 1);
		for (int32 Index = 0; Index < static_cast<int32>(Tracks.Num()); ++Index)
		{
			if (Index > 0) ImGui::SameLine();
			const FParticleCurveTrack& Track = Tracks[Index];
			const bool bActive = (Index == SelectedCurveTrackIndex);
			const ImVec4 Col(Track.Color.R / 255.0f, Track.Color.G / 255.0f, Track.Color.B / 255.0f, 1.0f);
			if (bActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(Col.x * 0.45f, Col.y * 0.45f, Col.z * 0.45f, 1.0f));
			if (ImGui::SmallButton(Track.Name.empty() ? "Curve" : Track.Name.c_str()))
			{
				SelectedCurveTrackIndex = Index;
				SelectedCurveKeyId = -1;
			}
			if (bActive) ImGui::PopStyleColor();
		}

		UCurveFloat* ActiveCurve = Tracks[SelectedCurveTrackIndex].Curve;
		if (ActiveCurve)
		{
			float DefaultValue = ActiveCurve->DefaultValue;

			if (ImGui::DragFloat("Default Value", &DefaultValue, 0.01f))
			{
				ActiveCurve->DefaultValue = DefaultValue;
			}
		}
		if (ActiveCurve)
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("+ Add Key"))
				SelectedCurveKeyId = ActiveCurve->AddKey(0.5f, ActiveCurve->GetFloatValue(0.5f));

			const FFloatCurveKey* SelectedKey = SelectedCurveKeyId >= 0 ? ActiveCurve->FindKey(SelectedCurveKeyId) : nullptr;
			if (!SelectedKey && !ActiveCurve->GetKeys().IsEmpty())
			{
				SelectedCurveKeyId = ActiveCurve->GetKeys()[0].KeyId;
				SelectedKey = ActiveCurve->FindKey(SelectedCurveKeyId);
			}
			if (SelectedKey)
			{
				const int32 SelectedKeyId = SelectedKey->KeyId;
				const FFloatCurveKey SelectedKeyData = *SelectedKey;

				float KeyTime = SelectedKeyData.Time;
				float KeyValue = SelectedKeyData.Value;
				int ModeIdx = static_cast<int>(SelectedKeyData.InterpMode);
				float ArriveT = SelectedKeyData.ArriveTangent;
				float LeaveT = SelectedKeyData.LeaveTangent;

				ImGui::SameLine();
				ImGui::SetNextItemWidth(EditorUIScale::Px(68.0f));
				if (ImGui::DragFloat("T", &KeyTime, 0.01f, 0.0f, 1.0f))
				{
					ActiveCurve->SetKeyTime(SelectedKeyId, KeyTime);
				}

				ImGui::SameLine();
				ImGui::SetNextItemWidth(EditorUIScale::Px(68.0f));
				if (ImGui::DragFloat("V", &KeyValue, 0.02f))
				{
					ActiveCurve->SetKeyValue(SelectedKeyId, KeyValue);
				}

				ImGui::SameLine();
				ImGui::SetNextItemWidth(EditorUIScale::Px(82.0f));
				if (ImGui::Combo("##Mode", &ModeIdx, "Constant\0Linear\0Cubic\0"))
				{
					ActiveCurve->SetKeyInterpMode(SelectedKeyId, static_cast<ECurveInterpMode>(ModeIdx));
				}

				if (SelectedKeyData.InterpMode == ECurveInterpMode::Cubic)
				{
					ImGui::SameLine();
					ImGui::SetNextItemWidth(EditorUIScale::Px(56.0f));
					bool bTanChanged = ImGui::DragFloat("In", &ArriveT, 0.05f);
					ImGui::SameLine();
					ImGui::SetNextItemWidth(EditorUIScale::Px(56.0f));
					bTanChanged |= ImGui::DragFloat("Out", &LeaveT, 0.05f);
					if (bTanChanged)
					{
						ActiveCurve->SetKeyTangents(SelectedKeyId, ArriveT, LeaveT);
					}
				}

				ImGui::SameLine();
				if (ImGui::SmallButton("Remove"))
				{
					ActiveCurve->RemoveKey(SelectedKeyId);
					SelectedCurveKeyId = -1;
				}
			}
		}

		if (ImPlot::BeginPlot("##ParticleCurvePlot", ImVec2(-1.0f, -1.0f)))
		{
			ImPlot::SetupAxes("Time", "Value");
			ImPlot::SetupAxisLimits(ImAxis_X1, -0.05, 1.05, ImPlotCond_Once);
			float MinValue = ActiveCurve ? ActiveCurve->DefaultValue : 0.0f;
			float MaxValue = ActiveCurve ? ActiveCurve->DefaultValue : 1.0f;

			if (ActiveCurve)
			{
				for (const FFloatCurveKey& Key : ActiveCurve->GetKeys())
				{
					MinValue = std::min(MinValue, Key.Value);
					MaxValue = std::max(MaxValue, Key.Value);
				}
			}

			const float ValueRange = std::max(0.1f, MaxValue - MinValue);
			ImPlot::SetupAxisLimits(ImAxis_Y1, MinValue - ValueRange * 0.15f, MaxValue + ValueRange * 0.15f, ImPlotCond_Always);

			constexpr int SampleCount = 64;
			double Xs[SampleCount];
			double Ys[SampleCount];
			for (int32 TrackIdx = 0; TrackIdx < static_cast<int32>(Tracks.Num()); ++TrackIdx)
			{
				const FParticleCurveTrack& Track = Tracks[TrackIdx];
				if (!Track.Curve) continue;
				for (int i = 0; i < SampleCount; ++i)
				{
					const float T = static_cast<float>(i) / static_cast<float>(SampleCount - 1);
					Xs[i] = T;
					Ys[i] = Track.Curve->GetFloatValue(T);
				}
				const ImVec4 Col(Track.Color.R / 255.0f, Track.Color.G / 255.0f, Track.Color.B / 255.0f,
					TrackIdx == SelectedCurveTrackIndex ? 1.0f : 0.45f);
				ImPlotSpec Spec;
				Spec.LineColor = Col;
				Spec.LineWeight = TrackIdx == SelectedCurveTrackIndex ? 2.5f : 1.25f;
				ImPlot::PlotLine(Track.Name.empty() ? "Curve" : Track.Name.c_str(), Xs, Ys, SampleCount, Spec);
			}

			if (ActiveCurve)
			{
				const FParticleCurveTrack& ActiveTrack = Tracks[SelectedCurveTrackIndex];
				const ImVec4 ActiveCol(ActiveTrack.Color.R / 255.0f, ActiveTrack.Color.G / 255.0f, ActiveTrack.Color.B / 255.0f, 1.0f);
				const TArray<FFloatCurveKey> Keys = ActiveCurve->GetKeys();
				for (const FFloatCurveKey& Key : Keys)
				{
					double KX = Key.Time;
					double KY = Key.Value;
					bool bClicked = false;
					const bool bSelectedKey = (Key.KeyId == SelectedCurveKeyId);
					const ImVec4 PtCol = bSelectedKey ? ImVec4(1.0f, 1.0f, 0.3f, 1.0f) : ActiveCol;
					if (ImPlot::DragPoint(Key.KeyId, &KX, &KY, PtCol, bSelectedKey ? 6.0f : 4.5f, 0, &bClicked))
					{
						SelectedCurveKeyId = Key.KeyId;
						ActiveCurve->SetKeyTime(Key.KeyId, std::clamp(static_cast<float>(KX), 0.0f, 1.0f));
						ActiveCurve->SetKeyValue(Key.KeyId, static_cast<float>(KY));
					}
					else if (bClicked)
					{
						SelectedCurveKeyId = Key.KeyId;
					}
				}

				if (ImPlot::IsPlotHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					const ImPlotPoint Pt = ImPlot::GetPlotMousePos();
					SelectedCurveKeyId = ActiveCurve->AddKey(std::clamp(static_cast<float>(Pt.x), 0.0f, 1.0f), static_cast<float>(Pt.y));
				}
			}
			ImPlot::EndPlot();
		}
	}
	ImGui::EndChild();
}

void FParticleSystemEditorPanel::OnRender()
{
	if (bFocusRequested)
	{
		ImGui::SetNextWindowFocus();
		bFocusRequested = false;
	}

	const FString Title = (EditingSystem ? FbxPath::ToString(FbxPath::FromString(AssetPath).filename()) : FString(GetPanelName())) + "###ParticleSystemEditor";
	bVisible = BeginPanel(Title.c_str(), ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	if (!bVisible)
	{
		ImGui::End();
		return;
	}

	RenderMenuBar();
	RenderToolbar();
	if (!EditingSystem)
	{
		ImGui::TextUnformatted("No Particle System is open.");
		ImGui::TextWrapped("Create or double-click a .particle asset in the Content Drawer.");
		ImGui::End();
		return;
	}

	const ImVec2 Available = ImGui::GetContentRegionAvail();

	const float SplitterThickness = EditorUIScale::Px(6.0f);
	const float MinMainHeight = EditorUIScale::Px(220.0f);
	const float MinCurveHeight = EditorUIScale::Px(130.0f);

	const float MaximumCurveHeight = bShowCurveEditor
		? std::max(0.0f, Available.y - MinMainHeight - SplitterThickness)
		: 0.0f;

	const float CurveHeight = bShowCurveEditor
		? std::clamp(Available.y * CurvePanelRatio, std::min(MinCurveHeight, MaximumCurveHeight), MaximumCurveHeight)
		: 0.0f;

	const bool bCanResizeCurve = bShowCurveEditor && MaximumCurveHeight > 0.0f;
	const float ActiveSplitterHeight = bCanResizeCurve ? SplitterThickness : 0.0f;
	const float MainHeight = std::max(0.0f, Available.y - CurveHeight - ActiveSplitterHeight);

	if (EmitterPanelWidth <= 0.0f)
	{
		EmitterPanelWidth = EditorUIScale::Px(285.0f);
	}

	if (DetailsPanelWidth <= 0.0f)
	{
		DetailsPanelWidth = EditorUIScale::Px(360.0f);
	}

	const int32 VisibleColumnCount = (bShowEmitters ? 1 : 0) + (bShowViewport ? 1 : 0) + (bShowDetails ? 1 : 0);
	const int32 SplitterCount = std::max(0, VisibleColumnCount - 1);
	const float PanelAreaWidth = std::max(1.0f, Available.x - SplitterThickness * SplitterCount);

	const float MinEmitterWidth = EditorUIScale::Px(180.0f);
	const float MinDetailsWidth = EditorUIScale::Px(220.0f);
	const float MinViewportWidth = EditorUIScale::Px(220.0f);

	auto ClampPanelWidth = [](float Value, float MinWidth, float MaxWidth)
	{
		const float SafeMax = std::max(1.0f, MaxWidth);
		const float SafeMin = std::min(MinWidth, SafeMax);
		return std::clamp(Value, SafeMin, SafeMax);
	};

	float CurrentEmitterWidth = 0.0f;
	float CurrentViewportWidth = 0.0f;
	float CurrentDetailsWidth = 0.0f;

	if (bShowEmitters && bShowViewport && bShowDetails)
	{
		CurrentEmitterWidth = ClampPanelWidth(EmitterPanelWidth, MinEmitterWidth, PanelAreaWidth - MinViewportWidth - MinDetailsWidth);
		CurrentDetailsWidth = ClampPanelWidth(DetailsPanelWidth, MinDetailsWidth, PanelAreaWidth - CurrentEmitterWidth - MinViewportWidth);
		CurrentViewportWidth = std::max(1.0f, PanelAreaWidth - CurrentEmitterWidth - CurrentDetailsWidth);
	}
	else if (bShowEmitters && bShowViewport)
	{
		CurrentEmitterWidth = ClampPanelWidth(EmitterPanelWidth, MinEmitterWidth, PanelAreaWidth - MinViewportWidth);
		CurrentViewportWidth = std::max(1.0f, PanelAreaWidth - CurrentEmitterWidth);
	}
	else if (bShowViewport && bShowDetails)
	{
		CurrentDetailsWidth = ClampPanelWidth(DetailsPanelWidth, MinDetailsWidth, PanelAreaWidth - MinViewportWidth);
		CurrentViewportWidth = std::max(1.0f, PanelAreaWidth - CurrentDetailsWidth);
	}
	else if (bShowEmitters && bShowDetails)
	{
		CurrentEmitterWidth = ClampPanelWidth(EmitterPanelWidth, MinEmitterWidth, PanelAreaWidth - MinDetailsWidth);
		CurrentDetailsWidth = std::max(1.0f, PanelAreaWidth - CurrentEmitterWidth);
	}
	else if (bShowEmitters)
	{
		CurrentEmitterWidth = PanelAreaWidth;
	}
	else if (bShowViewport)
	{
		CurrentViewportWidth = PanelAreaWidth;
	}
	else if (bShowDetails)
	{
		CurrentDetailsWidth = PanelAreaWidth;
	}

	const float MaxEmitterDragWidth = PanelAreaWidth - (bShowViewport ? MinViewportWidth : (bShowDetails ? MinDetailsWidth : 0.0f)) - (bShowViewport && bShowDetails ? CurrentDetailsWidth : 0.0f);
	const float MaxDetailsDragWidth = PanelAreaWidth - (bShowViewport ? MinViewportWidth : 0.0f) - (bShowEmitters ? CurrentEmitterWidth : 0.0f);

	auto DrawVerticalSplitter = [&](const char* Id, float* Width, float Direction, float MinWidth, float MaxWidth)
	{
		ImGui::InvisibleButton(Id, ImVec2(SplitterThickness, MainHeight));

		const bool bHovered = ImGui::IsItemHovered();
		const bool bActive = ImGui::IsItemActive();

		if (bHovered || bActive)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		}

		if (bActive)
		{
			*Width = ClampPanelWidth(*Width + ImGui::GetIO().MouseDelta.x * Direction, MinWidth, MaxWidth);
		}

		const ImVec2 SplitterMin = ImGui::GetItemRectMin();
		const float CenterX = SplitterMin.x + SplitterThickness * 0.5f;
		const ImU32 SplitterColor = ImGui::GetColorU32(bHovered || bActive ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);

		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(CenterX - 1.0f, SplitterMin.y), ImVec2(CenterX + 1.0f, SplitterMin.y + MainHeight), SplitterColor);
	};

	bool bHasPreviousPanel = false;

	if (bShowEmitters)
	{
		RenderEmittersPanel(CurrentEmitterWidth, MainHeight);
		bHasPreviousPanel = true;
	}

	if (bShowViewport)
	{
		if (bHasPreviousPanel)
		{
			ImGui::SameLine(0.0f, 0.0f);

			if (bShowEmitters)
			{
				DrawVerticalSplitter("##EmitterPanelSplitter", &EmitterPanelWidth, 1.0f, MinEmitterWidth, MaxEmitterDragWidth);
			}

			ImGui::SameLine(0.0f, 0.0f);
		}

		RenderViewportPanel(CurrentViewportWidth, MainHeight);
		bHasPreviousPanel = true;
	}

	if (bShowDetails)
	{
		if (bHasPreviousPanel)
		{
			ImGui::SameLine(0.0f, 0.0f);

			if (bShowViewport)
			{
				DrawVerticalSplitter("##DetailsPanelSplitter", &DetailsPanelWidth, -1.0f, MinDetailsWidth, MaxDetailsDragWidth);
			}
			else if (bShowEmitters)
			{
				DrawVerticalSplitter("##EmitterDetailsSplitter", &EmitterPanelWidth, 1.0f, MinEmitterWidth, MaxEmitterDragWidth);
			}

			ImGui::SameLine(0.0f, 0.0f);
		}

		RenderDetailsPanel(CurrentDetailsWidth, MainHeight);
	}

	if (bCanResizeCurve)
	{
		ImGui::InvisibleButton("##ParticleCurveSplitter", ImVec2(-1.0f, SplitterThickness));

		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		}

		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			CurvePanelRatio = std::clamp(CurvePanelRatio - ImGui::GetIO().MouseDelta.y / std::max(1.0f, Available.y), 0.15f, 0.50f);
		}
	}

	if (bShowCurveEditor && CurveHeight > 0.0f)
	{
		RenderCurveEditorPanel(0.0f, CurveHeight);
	}

	ImGui::End();
}

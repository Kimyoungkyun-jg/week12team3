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
#include <cmath>
#include <cstdio>

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

void FParticleSystemEditorPanel::CopyModuleProperties(UParticleModule* Destination, const UParticleModule* Source)
{
	if (!Destination || !Source)
	{
		return;
	}

	Destination->bEnabled = Source->bEnabled;

	if (const auto* Src = Cast<UParticleModuleRequired>(Source))
	{
		auto* Dst = Cast<UParticleModuleRequired>(Destination);
		if (!Dst)
		{
			return;
		}

		Dst->MaxParticles = Src->MaxParticles;
		Dst->EmitterDuration = Src->EmitterDuration;
		Dst->bLooping = Src->bLooping;
		Dst->bUseLocalSpace = Src->bUseLocalSpace;
		Dst->Material = Src->Material;
		return;
	}

	if (const auto* Src = Cast<UParticleModuleSpawn>(Source))
	{
		auto* Dst = Cast<UParticleModuleSpawn>(Destination);
		if (!Dst)
		{
			return;
		}

		Dst->Rate = Src->Rate;
		Dst->Bursts = Src->Bursts;
		return;
	}

	if (const auto* Src = Cast<UParticleModuleTypeDataMesh>(Source))
	{
		auto* Dst = Cast<UParticleModuleTypeDataMesh>(Destination);
		if (Dst)
		{
			Dst->Mesh = Src->Mesh;
		}
		return;
	}

	if (Cast<UParticleModuleTypeDataSprite>(Source) || Cast<UParticleModuleTypeDataGpuSprite>(Source))
	{
		return;
	}

	if (const auto* Src = Cast<UParticleModuleLifetime>(Source))
	{
		auto* Dst = Cast<UParticleModuleLifetime>(Destination);
		if (!Dst)
		{
			return;
		}

		Dst->Min = Src->Min;
		Dst->Max = Src->Max;
		return;
	}

	if (const auto* Src = Cast<UParticleModuleLocation>(Source))
	{
		auto* Dst = Cast<UParticleModuleLocation>(Destination);
		if (!Dst)
		{
			return;
		}

		Dst->Min = Src->Min;
		Dst->Max = Src->Max;
		Dst->ParameterName = Src->ParameterName;
		return;
	}

	if (const auto* Src = Cast<UParticleModuleVelocity>(Source))
	{
		auto* Dst = Cast<UParticleModuleVelocity>(Destination);
		if (!Dst)
		{
			return;
		}

		Dst->Min = Src->Min;
		Dst->Max = Src->Max;
		Dst->ParameterName = Src->ParameterName;
		return;
	}

	if (const auto* Src = Cast<UParticleModuleColor>(Source))
	{
		auto* Dst = Cast<UParticleModuleColor>(Destination);
		if (!Dst)
		{
			return;
		}

		Dst->Start = Src->Start;
		Dst->End = Src->End;
		Dst->ParameterName = Src->ParameterName;
		return;
	}

	if (const auto* Src = Cast<UParticleModuleSize>(Source))
	{
		auto* Dst = Cast<UParticleModuleSize>(Destination);
		if (!Dst)
		{
			return;
		}

		Dst->Start = Src->Start;
		Dst->End = Src->End;
		return;
	}

	if (const auto* Src = Cast<UParticleModuleInitialSize>(Source))
	{
		auto* Dst = Cast<UParticleModuleInitialSize>(Destination);
		if (!Dst)
		{
			return;
		}

		Dst->MinSize = Src->MinSize;
		Dst->MaxSize = Src->MaxSize;
		Dst->bUniformScale = Src->bUniformScale;
		return;
	}

	if (const auto* Src = Cast<UParticleModuleSizeByLife>(Source))
	{
		auto* Dst = Cast<UParticleModuleSizeByLife>(Destination);
		if (!Dst)
		{
			return;
		}

		if (Src->ScaleX && Dst->ScaleX)
		{
			Dst->ScaleX->ImportFromJson(Src->ScaleX->ExportToJson());
		}
		if (Src->ScaleY && Dst->ScaleY)
		{
			Dst->ScaleY->ImportFromJson(Src->ScaleY->ExportToJson());
		}
		if (Src->ScaleZ && Dst->ScaleZ)
		{
			Dst->ScaleZ->ImportFromJson(Src->ScaleZ->ExportToJson());
		}
		return;
	}

	if (const auto* Src = Cast<UParticleModuleColorOverLife>(Source))
	{
		auto* Dst = Cast<UParticleModuleColorOverLife>(Destination);
		if (!Dst)
		{
			return;
		}

		if (Src->ColorR && Dst->ColorR)
		{
			Dst->ColorR->ImportFromJson(Src->ColorR->ExportToJson());
		}
		if (Src->ColorG && Dst->ColorG)
		{
			Dst->ColorG->ImportFromJson(Src->ColorG->ExportToJson());
		}
		if (Src->ColorB && Dst->ColorB)
		{
			Dst->ColorB->ImportFromJson(Src->ColorB->ExportToJson());
		}
		if (Src->Alpha && Dst->Alpha)
		{
			Dst->Alpha->ImportFromJson(Src->Alpha->ExportToJson());
		}

		Dst->ParameterName = Src->ParameterName;
		Dst->bUseFadeInOut = Src->bUseFadeInOut;
		Dst->FadeInEnd = Src->FadeInEnd;
		Dst->FadeOutStart = Src->FadeOutStart;
		return;
	}

	if (const auto* Src = Cast<UParticleModuleRotation>(Source))
	{
		auto* Dst = Cast<UParticleModuleRotation>(Destination);
		if (!Dst)
		{
			return;
		}

		Dst->Min = Src->Min;
		Dst->Max = Src->Max;
		Dst->RateMin = Src->RateMin;
		Dst->RateMax = Src->RateMax;
		return;
	}

	if (const auto* Src = Cast<UParticleModuleSubUV>(Source))
	{
		auto* Dst = Cast<UParticleModuleSubUV>(Destination);
		if (!Dst)
		{
			return;
		}

		Dst->SubImagesHorizontal = Src->SubImagesHorizontal;
		Dst->SubImagesVertical = Src->SubImagesVertical;
		Dst->bUseLinearOverLife = Src->bUseLinearOverLife;
		Dst->PlayRate = Src->PlayRate;

		if (Src->SubImageIndex && Dst->SubImageIndex)
		{
			Dst->SubImageIndex->ImportFromJson(Src->SubImageIndex->ExportToJson());
		}

		return;
	}
}

UParticleModule* FParticleSystemEditorPanel::CopyModuleToLOD(UParticleLODLevel* Destination, const UParticleModule* Source)
{
	if (!Destination || !Source)
	{
		return nullptr;
	}

	UParticleModule* NewModule = Destination->AddModule(Source->GetClass());
	if (!NewModule)
	{
		return nullptr;
	}

	CopyModuleProperties(NewModule, Source);
	return NewModule;
}

bool FParticleSystemEditorPanel::CopyLODData(UParticleLODLevel* Destination, const UParticleLODLevel* Source)
{
	if (!Destination || !Source)
	{
		return false;
	}

	Destination->bEnabled = Source->bEnabled;

	if (Source->RequiredModule)
	{
		CopyModuleProperties(Destination->GetOrCreateRequired(), Source->RequiredModule);
	}

	if (Source->SpawnModule)
	{
		CopyModuleProperties(Destination->GetOrCreateSpawn(), Source->SpawnModule);
	}

	if (Source->TypeDataModule)
	{
		UParticleModuleTypeDataBase* NewTypeData = Destination->SetTypeData(Source->TypeDataModule->GetClass());
		if (!NewTypeData)
		{
			return false;
		}

		CopyModuleProperties(NewTypeData, Source->TypeDataModule);
	}

	for (UParticleModule* SourceModule : Source->Modules)
	{
		if (!SourceModule || !CopyModuleToLOD(Destination, SourceModule))
		{
			return false;
		}
	}

	return true;
}

void FParticleSystemEditorPanel::ApplyPreviewLOD()
{
	if (!PreviewActor || !PreviewActor->GetParticleSystemComponent())
	{
		return;
	}

	UParticleSystemComponent* Component = PreviewActor->GetParticleSystemComponent();
	TArray<FParticleEmitterInstance>& Instances = Component->GetEmitterInstancesMutable();

	for (FParticleEmitterInstance& Instance : Instances)
	{
		Instance.SetLODLevel(SelectedLODIndex);
	}

	Component->UpdateEmitterStrides();
	Component->MarkRenderStateDirty();
}

bool FParticleSystemEditorPanel::AddLowestLOD(bool bCopyPrevious)
{
	if (!EditingSystem || EditingSystem->GetEmitters().IsEmpty())
	{
		StatusMessage = "Cannot add LOD without emitters.";
		return false;
	}

	const int32 ExistingLODCount = static_cast<int32>(EditingSystem->LODDistances.Num());

	if (ExistingLODCount <= 0)
	{
		StatusMessage = "LOD distance settings are missing.";
		return false;
	}

	for (UParticleEmitter* Emitter : EditingSystem->GetEmitters())
	{
		if (!Emitter || static_cast<int32>(Emitter->GetLODLevels().Num()) != ExistingLODCount)
		{
			StatusMessage = "Emitter LOD counts are inconsistent.";
			return false;
		}
	}

	const float LastDistance = EditingSystem->LODDistances.Last();
	const float NewDistance = LastDistance + std::max(50.0f, std::abs(LastDistance) * 0.5f);

	for (UParticleEmitter* Emitter : EditingSystem->GetEmitters())
	{
		const TArray<UParticleLODLevel*>& Levels = Emitter->GetLODLevels();
		const UParticleLODLevel* SourceLOD = Levels.Last();

		UParticleLODLevel* NewLOD = Emitter->AddLODLevel();
		if (!NewLOD)
		{
			StatusMessage = "Failed to create LOD.";
			return false;
		}

		if (bCopyPrevious && !CopyLODData(NewLOD, SourceLOD))
		{
			StatusMessage = "Failed to copy previous LOD.";
			return false;
		}
	}

	EditingSystem->LODDistances.Add(NewDistance);
	SelectedLODIndex = ExistingLODCount;

	if (PreviewActor && PreviewActor->GetParticleSystemComponent())
	{
		PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
		ApplyPreviewLOD();
	}

	StatusMessage = "LOD added";
	return true;
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

void FParticleSystemEditorPanel::AddEmitter()
{
	if (!EditingSystem)
	{
		return;
	}

	const int32 DesiredLODCount = static_cast<int32>(EditingSystem->LODDistances.Num());

	if (DesiredLODCount <= 0)
	{
		StatusMessage = "Create LOD distances before adding an emitter.";
		return;
	}

	for (UParticleEmitter* Existing : EditingSystem->GetEmitters())
	{
		if (!Existing || static_cast<int32>(Existing->GetLODLevels().Num()) != DesiredLODCount)
		{
			StatusMessage = "Emitter LOD counts are inconsistent.";
			return;
		}
	}

	UParticleEmitter* NewEmitter = EditingSystem->AddEmitter();
	if (!NewEmitter)
	{
		return;
	}

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
		ApplyPreviewLOD();
	}

	StatusMessage = "Emitter added";
}

void FParticleSystemEditorPanel::DuplicateEmitter(UParticleEmitter* Source)
{
	if (!EditingSystem || !Source)
	{
		return;
	}

	UParticleEmitter* NewEmitter = EditingSystem->AddEmitter();
	if (!NewEmitter)
	{
		return;
	}

	NewEmitter->EmitterName = Source->EmitterName + " Copy";
	NewEmitter->bIsSolo = Source->bIsSolo;
	NewEmitter->PlayRate = Source->PlayRate;

	for (const UParticleLODLevel* SourceLOD : Source->GetLODLevels())
	{
		UParticleLODLevel* NewLOD = NewEmitter->AddLODLevel();
		if (!CopyLODData(NewLOD, SourceLOD))
		{
			StatusMessage = "Emitter duplicated with incomplete LOD data.";
			break;
		}
	}

	SelectedEmitter = NewEmitter;
	SelectedModule = nullptr;
	SelectedLODIndex = 0;

	if (PreviewActor && PreviewActor->GetParticleSystemComponent())
	{
		PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
		ApplyPreviewLOD();
	}

	StatusMessage = "Emitter duplicated";
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
		Engine->RenderPreviewViewport(&Client, PreviewWorld, nullptr, bShowSkybox ? Skybox.get() : nullptr, bShowGrid, bShowBounds ? PreviewComponent : nullptr, bShowOriginAxis,
			{ BackgroundColor[0], BackgroundColor[1], BackgroundColor[2], 1.0f });
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
			AddEmitter();
		}

		if (ImGui::MenuItem("Add Lowest LOD", nullptr, false, EditingSystem != nullptr))
		{
			AddLowestLOD(bCopyPreviousLOD);
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
	{
		SaveAsset();
	}

	ImGui::EndDisabled();

	ImGui::SameLine();

	if (ImGui::Button("Restart"))
	{
		if (PreviewActor && PreviewActor->GetParticleSystemComponent())
		{
			PreviewActor->GetParticleSystemComponent()->ResetParticles();
		}
	}

	ImGui::SameLine();

	if (ImGui::Button(bPaused ? "Play" : "Pause"))
	{
		bPaused = !bPaused;
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(EditorUIScale::Px(90.0f));
	ImGui::SliderFloat("##SimSpeed", &SimSpeed, 0.1f, 2.0f, "%.2fx");

	ImGui::SameLine();

	const char* ViewModes[] = {
		"Solid",
		"Wireframe",
		"Scene Depth",
		"World Normal",
		"Light Culling",
		"Cascade",
		"Shadow Mask",
		"Unlit"
	};

	int32 ViewModeIndex = static_cast<int32>(Client.GetViewportMode());

	ImGui::SetNextItemWidth(EditorUIScale::Px(125.0f));

	if (ImGui::Combo("##ParticleViewMode", &ViewModeIndex, ViewModes, IM_ARRAYSIZE(ViewModes)))
	{
		Client.SetViewportMode(static_cast<EViewportMode>(ViewModeIndex));
	}

	ImGui::SameLine();

	if (ImGui::Button("Focus"))
	{
		if (PreviewActor && PreviewActor->GetParticleSystemComponent())
		{
			const FBox Bounds = PreviewActor->GetParticleSystemComponent()->CalcBounds();
			const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
			const float Radius = std::max(1.0f, (Bounds.Max - Bounds.Min).Length() * 0.5f);
			Client.FocusOnBounds(Center, Radius);
		}
	}

	ImGui::SameLine();
	ImGui::Checkbox("Bounds", &bShowBounds);
	ImGui::SameLine();
	ImGui::Checkbox("Origin", &bShowOriginAxis);
	ImGui::SameLine();
	ImGui::Checkbox("Grid", &bShowGrid);
	ImGui::SameLine();
	ImGui::Checkbox("Sky", &bShowSkybox);
	ImGui::SameLine();
	ImGui::Checkbox("Stats", &bShowParticleStats);
	ImGui::SameLine();
	ImGui::ColorEdit3("##BgColor", BackgroundColor, ImGuiColorEditFlags_NoInputs);

	if (!StatusMessage.empty())
	{
		ImGui::SameLine();
		ImGui::TextDisabled("%s", StatusMessage.c_str());
	}

	ImGui::Separator();
	RenderLODControls();
	ImGui::Separator();
}

void FParticleSystemEditorPanel::RenderLODControls()
{
	if (!EditingSystem)
	{
		return;
	}

	const int32 LODCount = static_cast<int32>(EditingSystem->LODDistances.Num());
	bool bConsistent = LODCount > 0;

	for (UParticleEmitter* Emitter : EditingSystem->GetEmitters())
	{
		if (!Emitter || static_cast<int32>(Emitter->GetLODLevels().Num()) != LODCount)
		{
			bConsistent = false;
			break;
		}
	}

	ImGui::BeginDisabled(!bConsistent || LODCount <= 0);

	if (!bConsistent)
	{
		ImGui::TextDisabled("LOD configuration mismatch");
	}
	else
	{
		SelectedLODIndex = std::clamp(SelectedLODIndex, 0, LODCount - 1);

		if (ImGui::Button("<"))
		{
			SelectedLODIndex = std::max(0, SelectedLODIndex - 1);
			ApplyPreviewLOD();
		}

		ImGui::SameLine();

		char LODLabel[64];
		snprintf(LODLabel, sizeof(LODLabel), "LOD %d / %d", SelectedLODIndex, LODCount);

		ImGui::SetNextItemWidth(EditorUIScale::Px(120.0f));

		if (ImGui::BeginCombo("##GlobalLOD", LODLabel))
		{
			for (int32 Index = 0; Index < LODCount; ++Index)
			{
				char OptionLabel[32];
				snprintf(OptionLabel, sizeof(OptionLabel), "LOD %d", Index);

				if (ImGui::Selectable(OptionLabel, SelectedLODIndex == Index))
				{
					SelectedLODIndex = Index;
					SelectedModule = nullptr;
					SelectedCurveTrackIndex = 0;
					SelectedCurveKeyId = -1;
					ApplyPreviewLOD();
				}
			}

			ImGui::EndCombo();
		}

		ImGui::SameLine();

		if (ImGui::Button(">"))
		{
			SelectedLODIndex = std::min(LODCount - 1, SelectedLODIndex + 1);
			ApplyPreviewLOD();
		}
	}

	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::Checkbox("Copy Previous LOD", &bCopyPreviousLOD);

	ImGui::SameLine();

	if (ImGui::Button("+ Add Lowest LOD"))
	{
		AddLowestLOD(bCopyPreviousLOD);
	}
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
	if (bShowParticleStats && PreviewActor && PreviewActor->GetParticleSystemComponent())
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

	if (!EditingSystem)
	{
		ImGui::TextDisabled("No Particle System");
		ImGui::EndChild();
		return;
	}

	ImGui::TextUnformatted("EMITTERS");
	ImGui::SameLine();

	if (ImGui::SmallButton("+ Add Emitter"))
	{
		AddEmitter();
	}

	ImGui::SameLine();

	const bool bSystemSelected = SelectedEmitter == nullptr && SelectedModule == nullptr;

	if (ImGui::SmallButton(bSystemSelected ? "[Particle System]" : "Particle System"))
	{
		SelectedEmitter = nullptr;
		SelectedModule = nullptr;
		SelectedCurveTrackIndex = 0;
		SelectedCurveKeyId = -1;
	}

	ImGui::Separator();

	UParticleEmitter* EmitterToRemove = nullptr;
	UParticleEmitter* EmitterToDuplicate = nullptr;

	if (ImGui::BeginChild("EmitterColumns", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar))
	{
		const TArray<UParticleEmitter*>& Emitters = EditingSystem->GetEmitters();

		if (Emitters.IsEmpty())
		{
			ImGui::TextDisabled("No emitters. Add an emitter to get started.");
		}

		for (int32 EmitterIndex = 0; EmitterIndex < static_cast<int32>(Emitters.Num()); ++EmitterIndex)
		{
			UParticleEmitter* Emitter = Emitters[EmitterIndex];
			if (!Emitter)
			{
				continue;
			}

			ImGui::PushID(Emitter);

			if (ImGui::BeginChild("EmitterColumn", ImVec2(EmitterColumnWidth, 0.0f), ImGuiChildFlags_Borders))
			{
				char Header[64];
				snprintf(Header, sizeof(Header), "Emitter %d", EmitterIndex + 1);

				if (ImGui::Selectable(Header, SelectedEmitter == Emitter, ImGuiSelectableFlags_SpanAvailWidth))
				{
					SelectedEmitter = Emitter;
					SelectedModule = nullptr;
					SelectedCurveTrackIndex = 0;
					SelectedCurveKeyId = -1;
				}

				char NameBuffer[128] = {};
				strncpy_s(NameBuffer, Emitter->EmitterName.c_str(), _TRUNCATE);

				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::InputText("##EmitterName", NameBuffer, sizeof(NameBuffer)))
				{
					Emitter->EmitterName = NameBuffer;
				}

				if (ImGui::Button("Duplicate Emitter", ImVec2(-1.0f, 0.0f)))
				{
					EmitterToDuplicate = Emitter;
				}

				if (ImGui::Button("Remove Emitter", ImVec2(-1.0f, 0.0f)))
				{
					EmitterToRemove = Emitter;
				}

				const TArray<UParticleLODLevel*>& LODLevels = Emitter->GetLODLevels();

				if (SelectedLODIndex < 0 || SelectedLODIndex >= static_cast<int32>(LODLevels.Num()))
				{
					ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Missing LOD %d", SelectedLODIndex);
					ImGui::EndChild();
					ImGui::PopID();
					ImGui::SameLine(0.0f, EditorUIScale::Px(6.0f));
					continue;
				}

				UParticleLODLevel* LOD = LODLevels[SelectedLODIndex];

				ImGui::Separator();

				ImGui::Checkbox("Enabled", &LOD->bEnabled);
				ImGui::SameLine();
				ImGui::Checkbox("Solo", &Emitter->bIsSolo);

				ImGui::TextDisabled("Emitter Play Rate");
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::DragFloat("##EmitterPlayRate", &Emitter->PlayRate, 0.05f, 0.0f, 20.0f);

				ImGui::Separator();
				ImGui::TextDisabled("EMITTER TYPE");

				int32 TypeIndex = 0;
				UParticleModuleTypeDataBase* TypeData = LOD->GetOrCreateTypeData();

				if (TypeData->GetEmitterType() == EParticleEmitterType::GpuSprite)
				{
					TypeIndex = 1;
				}
				else if (TypeData->GetEmitterType() == EParticleEmitterType::Mesh)
				{
					TypeIndex = 2;
				}

				ImGui::SetNextItemWidth(-1.0f);

				if (ImGui::Combo("##EmitterType", &TypeIndex, "Sprite\0GPU Sprite\0Mesh\0"))
				{
					UClass* TargetClass = UParticleModuleTypeDataSprite::StaticClass();

					if (TypeIndex == 1)
					{
						TargetClass = UParticleModuleTypeDataGpuSprite::StaticClass();
					}
					else if (TypeIndex == 2)
					{
						TargetClass = UParticleModuleTypeDataMesh::StaticClass();
					}

					const bool bWasSelected = SelectedModule == TypeData;
					UParticleModuleTypeDataBase* NewTypeData = LOD->SetTypeData(TargetClass);

					if (auto* MeshType = Cast<UParticleModuleTypeDataMesh>(NewTypeData))
					{
						if (!MeshType->Mesh)
						{
							MeshType->Mesh = UAssetManager::GetAssetByPath<UStaticMesh>("Cube");
						}

						LOD->GetOrCreateRequired()->Material = nullptr;
					}

					if (bWasSelected)
					{
						SelectedModule = NewTypeData;
					}

					if (PreviewActor && PreviewActor->GetParticleSystemComponent())
					{
						PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
						ApplyPreviewLOD();
					}
				}

				UParticleModule* ModuleToRemove = nullptr;
				UParticleModule* ModuleToMove = nullptr;
				UParticleModule* MoveTargetModule = nullptr;
				UParticleModule* ModuleToDuplicate = nullptr;
				UParticleModule* ModuleToCopyToOtherEmitter = nullptr;
				UParticleEmitter* CopyTargetEmitter = nullptr;
				bool bMoveAfterTarget = false;

				auto DrawModuleRow = [&](UParticleModule* Module, const char* Stage, bool bRemovable)
				{
					if (!Module)
					{
						return;
					}

					ImGui::PushID(Module);

					if (ImGui::Checkbox("##ModuleEnabled", &Module->bEnabled))
					{
						if (PreviewActor && PreviewActor->GetParticleSystemComponent())
						{
							PreviewActor->GetParticleSystemComponent()->UpdateEmitterStrides();
						}
					}

					ImGui::SameLine();

					const float RowWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
					const float TextWidth = std::max(1.0f, RowWidth - EditorUIScale::Px(12.0f));
					const char* DisplayName = Module->GetDisplayName();

					const ImVec2 NameSize = ImGui::CalcTextSize(DisplayName, nullptr, false, TextWidth);
					const ImVec2 StageSize = ImGui::CalcTextSize(Stage, nullptr, false, TextWidth);
					const float RowHeight = NameSize.y + StageSize.y + EditorUIScale::Px(10.0f);

					if (ImGui::Selectable("##ModuleRow", SelectedModule == Module, 0, ImVec2(RowWidth, RowHeight)))
					{
						if (SelectedModule != Module)
						{
							SelectedCurveTrackIndex = 0;
							SelectedCurveKeyId = -1;
						}

						SelectedEmitter = Emitter;
						SelectedModule = Module;
					}

					const ImVec2 RowMin = ImGui::GetItemRectMin();
					const ImVec2 RowMax = ImGui::GetItemRectMax();
					const ImVec2 TextPosition(RowMin.x + EditorUIScale::Px(6.0f), RowMin.y + EditorUIScale::Px(3.0f));

					ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), TextPosition, ImGui::GetColorU32(ImGuiCol_Text), DisplayName, nullptr, TextWidth);
					ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(TextPosition.x, TextPosition.y + NameSize.y + EditorUIScale::Px(2.0f)), ImGui::GetColorU32(ImGuiCol_TextDisabled), Stage, nullptr, TextWidth);

					if (bRemovable && ImGui::BeginPopupContextItem("ModuleContext"))
					{
						if (ImGui::MenuItem("Duplicate Module"))
						{
							ModuleToDuplicate = Module;
						}

						if (ImGui::MenuItem("Remove Module"))
						{
							ModuleToRemove = Module;
						}

						if (ImGui::BeginMenu("Copy To Emitter"))
						{
							for (UParticleEmitter* TargetEmitter : Emitters)
							{
								if (!TargetEmitter || TargetEmitter == Emitter)
								{
									continue;
								}

								if (ImGui::MenuItem(TargetEmitter->EmitterName.c_str()))
								{
									ModuleToCopyToOtherEmitter = Module;
									CopyTargetEmitter = TargetEmitter;
								}
							}

							ImGui::EndMenu();
						}

						ImGui::EndPopup();
					}

					if (bRemovable && ImGui::BeginDragDropSource())
					{
						UParticleModule* DraggedModule = Module;
						ImGui::SetDragDropPayload("PARTICLE_MODULE", &DraggedModule, sizeof(DraggedModule));
						ImGui::TextUnformatted(DisplayName);
						ImGui::EndDragDropSource();
					}

					if (bRemovable && ImGui::BeginDragDropTarget())
					{
						if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("PARTICLE_MODULE"))
						{
							UParticleModule* DroppedModule = *static_cast<UParticleModule* const*>(Payload->Data);

							const bool bSameLOD = std::find(LOD->Modules.begin(), LOD->Modules.end(), DroppedModule) != LOD->Modules.end();

							if (bSameLOD && DroppedModule != Module)
							{
								ModuleToMove = DroppedModule;
								MoveTargetModule = Module;
								bMoveAfterTarget = ImGui::GetIO().MousePos.y > (RowMin.y + RowMax.y) * 0.5f;
							}
						}

						ImGui::EndDragDropTarget();
					}

					ImGui::PopID();
				};

				if (ImGui::CollapsingHeader("Emitter Setup", ImGuiTreeNodeFlags_DefaultOpen))
				{
					DrawModuleRow(LOD->GetOrCreateTypeData(), "RENDERER", false);
					DrawModuleRow(LOD->GetOrCreateRequired(), "REQUIRED", false);
				}

				if (ImGui::CollapsingHeader("Spawn", ImGuiTreeNodeFlags_DefaultOpen))
				{
					DrawModuleRow(LOD->GetOrCreateSpawn(), "SPAWN", false);
				}

				if (ImGui::CollapsingHeader("Modules", ImGuiTreeNodeFlags_DefaultOpen))
				{
					for (UParticleModule* Module : LOD->Modules)
					{
						if (!Module)
						{
							continue;
						}

						const char* Stage = Module->RunsOnSpawn()
							? (Module->RunsOnUpdate() ? "SPAWN + UPDATE" : "SPAWN")
							: (Module->RunsOnUpdate() ? "UPDATE" : "CUSTOM");

						DrawModuleRow(Module, Stage, true);
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
						SelectedCurveTrackIndex = 0;
						SelectedCurveKeyId = -1;
					}

					LOD->RemoveModule(ModuleToRemove);

					if (PreviewActor && PreviewActor->GetParticleSystemComponent())
					{
						PreviewActor->GetParticleSystemComponent()->UpdateEmitterStrides();
					}
				}
				else if (ModuleToDuplicate)
				{
					if (UParticleModule* Duplicated = CopyModuleToLOD(LOD, ModuleToDuplicate))
					{
						SelectedEmitter = Emitter;
						SelectedModule = Duplicated;
						SelectedCurveTrackIndex = 0;
						SelectedCurveKeyId = -1;

						if (PreviewActor && PreviewActor->GetParticleSystemComponent())
						{
							PreviewActor->GetParticleSystemComponent()->UpdateEmitterStrides();
						}
					}
				}
				else if (ModuleToMove && MoveTargetModule)
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

				if (ModuleToCopyToOtherEmitter && CopyTargetEmitter)
				{
					const TArray<UParticleLODLevel*>& TargetLevels = CopyTargetEmitter->GetLODLevels();

					if (SelectedLODIndex < static_cast<int32>(TargetLevels.Num()))
					{
						UParticleLODLevel* TargetLOD = TargetLevels[SelectedLODIndex];

						if (UParticleModule* Copied = CopyModuleToLOD(TargetLOD, ModuleToCopyToOtherEmitter))
						{
							SelectedEmitter = CopyTargetEmitter;
							SelectedModule = Copied;
							SelectedCurveTrackIndex = 0;
							SelectedCurveKeyId = -1;

							if (PreviewActor && PreviewActor->GetParticleSystemComponent())
							{
								PreviewActor->GetParticleSystemComponent()->UpdateEmitterStrides();
							}
						}
					}
				}
			}

			ImGui::EndChild();
			ImGui::PopID();

			if (EmitterIndex + 1 < static_cast<int32>(Emitters.Num()))
			{
				ImGui::SameLine(0.0f, EditorUIScale::Px(6.0f));
			}
		}
	}

	ImGui::EndChild();

	if (EmitterToRemove)
	{
		if (SelectedEmitter == EmitterToRemove)
		{
			SelectedEmitter = nullptr;
			SelectedModule = nullptr;
			SelectedCurveTrackIndex = 0;
			SelectedCurveKeyId = -1;
		}

		EditingSystem->RemoveEmitter(EmitterToRemove);

		if (PreviewActor && PreviewActor->GetParticleSystemComponent())
		{
			PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
			ApplyPreviewLOD();
		}
	}
	else if (EmitterToDuplicate)
	{
		DuplicateEmitter(EmitterToDuplicate);
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
	if (!ImGui::BeginChild("ParticleCurveEditor", ImVec2(Width, Height), ImGuiChildFlags_Borders))
	{
		ImGui::EndChild();
		return;
	}

	struct FEditorCurveEntry
	{
		UParticleEmitter* Emitter = nullptr;
		UParticleModule* Module = nullptr;
		FParticleCurveTrack Track;
		FString Label;
	};

	TArray<FEditorCurveEntry> Tracks;

	if (EditingSystem)
	{
		for (UParticleEmitter* Emitter : EditingSystem->GetEmitters())
		{
			if (!Emitter)
			{
				continue;
			}

			const TArray<UParticleLODLevel*>& Levels = Emitter->GetLODLevels();

			if (SelectedLODIndex < 0 || SelectedLODIndex >= static_cast<int32>(Levels.Num()))
			{
				continue;
			}

			UParticleLODLevel* LOD = Levels[SelectedLODIndex];
			if (!LOD)
			{
				continue;
			}

			for (UParticleModule* Module : LOD->Modules)
			{
				if (!Module)
				{
					continue;
				}

				TArray<FParticleCurveTrack> ModuleTracks;
				Module->CollectCurves(ModuleTracks);

				for (const FParticleCurveTrack& Track : ModuleTracks)
				{
					if (!Track.Curve)
					{
						continue;
					}

					FEditorCurveEntry Entry;
					Entry.Emitter = Emitter;
					Entry.Module = Module;
					Entry.Track = Track;
					Entry.Label = Emitter->EmitterName + " / " + Module->GetDisplayName() + " / " + Track.Name;
					Tracks.Add(Entry);
				}
			}
		}
	}

	ImGui::TextUnformatted("CURVE TRACKS");
	ImGui::Separator();

	if (Tracks.IsEmpty())
	{
		ImGui::TextDisabled("No curves in the selected LOD.");
		ImGui::EndChild();
		return;
	}

	SelectedCurveTrackIndex = std::clamp(SelectedCurveTrackIndex, 0, static_cast<int32>(Tracks.Num()) - 1);

	for (int32 Index = 0; Index < static_cast<int32>(Tracks.Num()); ++Index)
	{
		const FEditorCurveEntry& Entry = Tracks[Index];
		const bool bActive = SelectedCurveTrackIndex == Index;
		bool bVisible = std::find(HiddenCurveTracks.begin(), HiddenCurveTracks.end(), Entry.Track.Curve) == HiddenCurveTracks.end();

		ImGui::PushID(Entry.Track.Curve);

		if (ImGui::Checkbox("##CurveVisible", &bVisible))
		{
			auto It = std::find(HiddenCurveTracks.begin(), HiddenCurveTracks.end(), Entry.Track.Curve);

			if (bVisible && It != HiddenCurveTracks.end())
			{
				HiddenCurveTracks.RemoveAt(static_cast<int32>(std::distance(HiddenCurveTracks.begin(), It)));
			}
			else if (!bVisible && It == HiddenCurveTracks.end())
			{
				HiddenCurveTracks.Add(Entry.Track.Curve);
			}
		}

		ImGui::SameLine();

		if (ImGui::Selectable(Entry.Label.c_str(), bActive, ImGuiSelectableFlags_SpanAvailWidth))
		{
			SelectedCurveTrackIndex = Index;
			SelectedEmitter = Entry.Emitter;
			SelectedModule = Entry.Module;
			SelectedCurveKeyId = -1;
		}

		ImGui::PopID();
	}

	FEditorCurveEntry& ActiveEntry = Tracks[SelectedCurveTrackIndex];
	UCurveFloat* ActiveCurve = ActiveEntry.Track.Curve;

	if (ActiveCurve)
	{
		float DefaultValue = ActiveCurve->DefaultValue;

		ImGui::TextUnformatted("Default Value");
		ImGui::SetNextItemWidth(EditorUIScale::Px(120.0f));

		if (ImGui::DragFloat("##CurveDefaultValue", &DefaultValue, 0.01f))
		{
			ActiveCurve->DefaultValue = DefaultValue;
		}

		ImGui::SameLine();

		if (ImGui::Button("+ Add Key"))
		{
			SelectedCurveKeyId = ActiveCurve->AddKey(0.5f, ActiveCurve->GetFloatValue(0.5f));
		}

		const FFloatCurveKey* SelectedKey = SelectedCurveKeyId >= 0 ? ActiveCurve->FindKey(SelectedCurveKeyId) : nullptr;

		if (!SelectedKey && !ActiveCurve->GetKeys().IsEmpty())
		{
			SelectedCurveKeyId = ActiveCurve->GetKeys()[0].KeyId;
			SelectedKey = ActiveCurve->FindKey(SelectedCurveKeyId);
		}

		if (SelectedKey)
		{
			const int32 KeyId = SelectedKey->KeyId;
			const FFloatCurveKey KeyData = *SelectedKey;

			float KeyTime = KeyData.Time;
			float KeyValue = KeyData.Value;
			int32 ModeIndex = static_cast<int32>(KeyData.InterpMode);
			float ArriveTangent = KeyData.ArriveTangent;
			float LeaveTangent = KeyData.LeaveTangent;

			ImGui::SetNextItemWidth(EditorUIScale::Px(80.0f));

			if (ImGui::DragFloat("Time", &KeyTime, 0.01f, 0.0f, 1.0f))
			{
				ActiveCurve->SetKeyTime(KeyId, KeyTime);
			}

			ImGui::SameLine();
			ImGui::SetNextItemWidth(EditorUIScale::Px(80.0f));

			if (ImGui::DragFloat("Value", &KeyValue, 0.02f))
			{
				ActiveCurve->SetKeyValue(KeyId, KeyValue);
			}

			ImGui::SameLine();
			ImGui::SetNextItemWidth(EditorUIScale::Px(100.0f));

			if (ImGui::Combo("##CurveInterpMode", &ModeIndex, "Constant\0Linear\0Cubic\0"))
			{
				ActiveCurve->SetKeyInterpMode(KeyId, static_cast<ECurveInterpMode>(ModeIndex));
			}

			if (KeyData.InterpMode == ECurveInterpMode::Cubic)
			{
				ImGui::SetNextItemWidth(EditorUIScale::Px(70.0f));

				bool bChanged = ImGui::DragFloat("Arrive", &ArriveTangent, 0.05f);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(EditorUIScale::Px(70.0f));
				bChanged |= ImGui::DragFloat("Leave", &LeaveTangent, 0.05f);

				if (bChanged)
				{
					ActiveCurve->SetKeyTangents(KeyId, ArriveTangent, LeaveTangent);
				}
			}

			ImGui::SameLine();

			if (ImGui::Button("Remove Key"))
			{
				ActiveCurve->RemoveKey(KeyId);
				SelectedCurveKeyId = -1;
			}
		}
	}

	if (ImPlot::BeginPlot("##ParticleCurvePlot", ImVec2(-1.0f, -1.0f)))
	{
		ImPlot::SetupAxes("Relative Time", "Value");
		ImPlot::SetupAxisLimits(ImAxis_X1, -0.05, 1.05, ImPlotCond_Once);

		float MinValue = 0.0f;
		float MaxValue = 1.0f;
		bool bHasValue = false;

		for (const FEditorCurveEntry& Entry : Tracks)
		{
			if (!Entry.Track.Curve)
			{
				continue;
			}

			if (std::find(HiddenCurveTracks.begin(), HiddenCurveTracks.end(), Entry.Track.Curve) != HiddenCurveTracks.end())
			{
				continue;
			}

			UCurveFloat* Curve = Entry.Track.Curve;

			if (!bHasValue)
			{
				MinValue = Curve->DefaultValue;
				MaxValue = Curve->DefaultValue;
				bHasValue = true;
			}
			else
			{
				MinValue = std::min(MinValue, Curve->DefaultValue);
				MaxValue = std::max(MaxValue, Curve->DefaultValue);
			}

			for (const FFloatCurveKey& Key : Curve->GetKeys())
			{
				MinValue = std::min(MinValue, Key.Value);
				MaxValue = std::max(MaxValue, Key.Value);
			}
		}

		const float ValueRange = std::max(0.1f, MaxValue - MinValue);
		ImPlot::SetupAxisLimits(ImAxis_Y1, MinValue - ValueRange * 0.15f, MaxValue + ValueRange * 0.15f, ImPlotCond_Always);

		constexpr int SampleCount = 64;
		double XValues[SampleCount];
		double YValues[SampleCount];

		for (int32 TrackIndex = 0; TrackIndex < static_cast<int32>(Tracks.Num()); ++TrackIndex)
		{
			const FEditorCurveEntry& Entry = Tracks[TrackIndex];

			if (!Entry.Track.Curve || std::find(HiddenCurveTracks.begin(), HiddenCurveTracks.end(), Entry.Track.Curve) != HiddenCurveTracks.end())
			{
				continue;
			}

			for (int32 Sample = 0; Sample < SampleCount; ++Sample)
			{
				const float Time = static_cast<float>(Sample) / static_cast<float>(SampleCount - 1);
				XValues[Sample] = Time;
				YValues[Sample] = Entry.Track.Curve->GetFloatValue(Time);
			}

			const FColor& TrackColor = Entry.Track.Color;
			const bool bActive = TrackIndex == SelectedCurveTrackIndex;
			ImPlotSpec Spec;
			Spec.LineColor = ImVec4(TrackColor.R / 255.0f, TrackColor.G / 255.0f, TrackColor.B / 255.0f, bActive ? 1.0f : 0.45f);
			Spec.LineWeight = bActive ? 2.5f : 1.25f;

			ImPlot::PlotLine(Entry.Label.c_str(), XValues, YValues, SampleCount, Spec);
		}

		if (ActiveCurve && std::find(HiddenCurveTracks.begin(), HiddenCurveTracks.end(), ActiveCurve) == HiddenCurveTracks.end())
		{
			const FColor& ActiveColor = ActiveEntry.Track.Color;
			const ImVec4 PointColor(ActiveColor.R / 255.0f, ActiveColor.G / 255.0f, ActiveColor.B / 255.0f, 1.0f);

			for (const FFloatCurveKey& Key : ActiveCurve->GetKeys())
			{
				double X = Key.Time;
				double Y = Key.Value;
				bool bClicked = false;
				const bool bSelected = Key.KeyId == SelectedCurveKeyId;

				if (ImPlot::DragPoint(Key.KeyId, &X, &Y, bSelected ? ImVec4(1.0f, 1.0f, 0.3f, 1.0f) : PointColor, bSelected ? 6.0f : 4.5f, 0, &bClicked))
				{
					SelectedCurveKeyId = Key.KeyId;
					ActiveCurve->SetKeyTime(Key.KeyId, std::clamp(static_cast<float>(X), 0.0f, 1.0f));
					ActiveCurve->SetKeyValue(Key.KeyId, static_cast<float>(Y));
				}
				else if (bClicked)
				{
					SelectedCurveKeyId = Key.KeyId;
				}
			}

			if (ImPlot::IsPlotHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				const ImPlotPoint Point = ImPlot::GetPlotMousePos();
				SelectedCurveKeyId = ActiveCurve->AddKey(std::clamp(static_cast<float>(Point.x), 0.0f, 1.0f), static_cast<float>(Point.y));
			}
		}

		ImPlot::EndPlot();
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
	const float AvailableHeight = std::max(1.0f, Available.y);
	const float Splitter = EditorUIScale::Px(6.0f);

	const bool bLeftVisible = bShowViewport || bShowDetails;
	const bool bRightVisible = bShowEmitters || bShowCurveEditor;

	const float ColumnSplitterWidth = bLeftVisible && bRightVisible ? Splitter : 0.0f;
	const float LayoutWidth = std::max(1.0f, Available.x - ColumnSplitterWidth);

	float LeftWidth = LayoutWidth;
	float RightWidth = LayoutWidth;

	if (bLeftVisible && bRightVisible)
	{
		const float MinColumnWidth = std::min(EditorUIScale::Px(240.0f), LayoutWidth * 0.5f);
		LeftWidth = std::clamp(LayoutWidth * MainColumnRatio, MinColumnWidth, LayoutWidth - MinColumnWidth);
		RightWidth = LayoutWidth - LeftWidth;
	}

	const bool bLeftTopVisible = bShowViewport;
	const bool bLeftBottomVisible = bShowDetails;
	const bool bRightTopVisible = bShowEmitters;
	const bool bRightBottomVisible = bShowCurveEditor;

	const bool bHasTwoRows = (bLeftTopVisible && bLeftBottomVisible) || (bRightTopVisible && bRightBottomVisible);
	const float RowSplitterHeight = bHasTwoRows ? Splitter : 0.0f;
	const float LayoutHeight = std::max(1.0f, Available.y - RowSplitterHeight);
	const float MinRowHeight = std::min(EditorUIScale::Px(130.0f), LayoutHeight * 0.5f);

	const float TopHeight = bHasTwoRows
		? std::clamp(LayoutHeight * TopRowRatio, MinRowHeight, LayoutHeight - MinRowHeight)
		: LayoutHeight;

	const float BottomHeight = bHasTwoRows ? LayoutHeight - TopHeight : LayoutHeight;

	auto DrawHorizontalSplitter = [&](const char* Id, float PanelWidth)
	{
		const ImVec2 ButtonSize(PanelWidth, Splitter);
		ImGui::InvisibleButton(Id, ButtonSize);

		const bool bHovered = ImGui::IsItemHovered();
		const bool bActive = ImGui::IsItemActive();

		if (bHovered || bActive)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		}

		if (bActive)
		{
			TopRowRatio = std::clamp(TopRowRatio + ImGui::GetIO().MouseDelta.y / AvailableHeight, 0.2f, 0.8f);
		}

		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max(Min.x + PanelWidth, Min.y + Splitter);
		const ImU32 Color = ImGui::GetColorU32(bHovered || bActive ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		DrawList->AddRectFilled(Min, Max, Color);
	};

	auto DrawVerticalSplitter = [&]()
	{
		const ImVec2 ButtonSize(Splitter, AvailableHeight);
		ImGui::InvisibleButton("##CascadeColumnSplitter", ButtonSize);

		const bool bHovered = ImGui::IsItemHovered();
		const bool bActive = ImGui::IsItemActive();

		if (bHovered || bActive)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		}

		if (bActive)
		{
			MainColumnRatio = std::clamp(MainColumnRatio + ImGui::GetIO().MouseDelta.x / std::max(1.0f, Available.x), 0.2f, 0.8f);
		}

		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max(Min.x + Splitter, Min.y + AvailableHeight);
		const ImU32 Color = ImGui::GetColorU32(bHovered || bActive ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		DrawList->AddRectFilled(Min, Max, Color);
	};

	if (bLeftVisible)
	{
		ImGui::BeginGroup();

		if (bLeftTopVisible && bLeftBottomVisible)
		{
			RenderViewportPanel(LeftWidth, TopHeight);
			DrawHorizontalSplitter("##LeftPanelSplitter", LeftWidth);
			RenderDetailsPanel(LeftWidth, BottomHeight);
		}
		else if (bLeftTopVisible)
		{
			RenderViewportPanel(LeftWidth, AvailableHeight);
		}
		else if (bLeftBottomVisible)
		{
			RenderDetailsPanel(LeftWidth, AvailableHeight);
		}

		ImGui::EndGroup();
	}

	if (bLeftVisible && bRightVisible)
	{
		ImGui::SameLine(0.0f, 0.0f);
		DrawVerticalSplitter();
		ImGui::SameLine(0.0f, 0.0f);
	}

	if (bRightVisible)
	{
		ImGui::BeginGroup();

		if (bRightTopVisible && bRightBottomVisible)
		{
			RenderEmittersPanel(RightWidth, TopHeight);
			DrawHorizontalSplitter("##RightPanelSplitter", RightWidth);
			RenderCurveEditorPanel(RightWidth, BottomHeight);
		}
		else if (bRightTopVisible)
		{
			RenderEmittersPanel(RightWidth, AvailableHeight);
		}
		else if (bRightBottomVisible)
		{
			RenderCurveEditorPanel(RightWidth, AvailableHeight);
		}

		ImGui::EndGroup();
	}

	ImGui::End();
}

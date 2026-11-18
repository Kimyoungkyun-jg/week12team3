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
#include "UObject/UObjectGlobals.h"
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
#include "Render/MeshBatch.h"
#include "Engine/World.h"
#include "Serialization/JsonArchive.h"
#include "Core/StatOverlay.h"
#include "Core/Stats/LightweightStats.h"
#include <implot.h>
#include <commdlg.h>
#include <algorithm>
#include <filesystem>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace
{
	UParticleEmitter* AddDefaultEmitter(UParticleSystem* System, int32 LODCount)
	{
		UParticleEmitter* Emitter = System ? System->AddEmitter() : nullptr;
		if (!Emitter)
		{
			return nullptr;
		}

		Emitter->EnableModuleInheritance();
		for (int32 Index = 0; Index < LODCount; ++Index)
		{
			UParticleLODLevel* LOD = Emitter->AddLODLevel();
			LOD->GetOrCreateSpawn()->Rate = Index == 0 ? 10.0f : (Index == 1 ? 6.0f : 2.0f);
		}
		UParticleLODLevel* BaseLOD = Emitter->GetLODLevels()[0];
		BaseLOD->GetOrCreateRequired()->Material = UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");
		BaseLOD->AddModule(UParticleModuleLifetime::StaticClass());
		BaseLOD->AddModule(UParticleModuleSize::StaticClass());
		BaseLOD->AddModule(UParticleModuleVelocity::StaticClass());
		BaseLOD->AddModule(UParticleModuleColor::StaticClass());
		Emitter->CacheEmitterModuleInfo();
		return Emitter;
	}
}

bool AreValidParticleLODDistances(const TArray<float>& Distances)
{
	if (Distances.IsEmpty())
	{
		return false;
	}

	for (int32 Index = 0; Index < static_cast<int32>(Distances.Num()); ++Index)
	{
		if (!std::isfinite(Distances[Index]) || Distances[Index] < 0.0f)
		{
			return false;
		}

		if (Index > 0 && Distances[Index] <= Distances[Index - 1])
		{
			return false;
		}
	}

	return true;
}

namespace
{
	UParticleLODLevel* GetParticleEditorLOD(UParticleEmitter* Emitter, int32 Index)
	{
		if (!Emitter || Index < 0 || static_cast<uint32>(Index) >= Emitter->GetLODLevels().Num())
			return nullptr;
		return Emitter->GetLODLevels()[Index];
	}
}

UParticleModule* FParticleSystemEditorPanel::CopyModuleToLOD(UParticleLODLevel* Destination, const UParticleModule* Source)
{
	if (!Destination || !Source)
	{
		return nullptr;
	}

	UParticleModule* Copy = Cast<UParticleModule>(StaticDuplicateObject(Source, Destination));
	if (!Copy) return nullptr;
	int32 SlotId = 1;
	for (const UParticleModule* Existing : Destination->Modules)
		if (Existing) SlotId = std::max(SlotId, Existing->LODSlotId + 1);
	Copy->LODSlotId = SlotId;
	Destination->Modules.Add(Copy);
	Destination->MarkBuildDirty();
	return Copy;
}

void FParticleSystemEditorPanel::ApplyPreviewLOD()
{
	if (PreviewActor && PreviewActor->GetParticleSystemComponent())
		PreviewActor->GetParticleSystemComponent()->SetForcedLOD(SelectedLODIndex);
}

bool FParticleSystemEditorPanel::InsertLOD(int32 InsertIndex, bool bCopyPrevious)
{
	if (!EditingSystem || !EditingSystem->InsertLOD(InsertIndex, bCopyPrevious))
	{
		StatusMessage = "Could not insert LOD.";
		return false;
	}

	SelectedLODIndex = InsertIndex;
	SelectedModule = nullptr;
	SelectedCurve = nullptr;
	SelectedCurveTrackIndex = 0;
	SelectedCurveKeyId = -1;
	if (PreviewActor && PreviewActor->GetParticleSystemComponent())
	{
		PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
		ApplyPreviewLOD();
	}
	MarkAssetModified();
	StatusMessage = "LOD inserted";
	return true;
}

bool FParticleSystemEditorPanel::RemoveLOD(int32 Index)
{
	if (!EditingSystem || !EditingSystem->RemoveLOD(Index))
	{
		StatusMessage = "Cannot remove this LOD.";
		return false;
	}

	SelectedLODIndex = std::clamp(SelectedLODIndex, 0, EditingSystem->GetLODCount() - 1);
	SelectedModule = nullptr;
	SelectedCurve = nullptr;
	SelectedCurveTrackIndex = 0;
	SelectedCurveKeyId = -1;
	HiddenCurveTracks.Reset();
	if (PreviewActor && PreviewActor->GetParticleSystemComponent())
	{
		PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
		ApplyPreviewLOD();
	}
	MarkAssetModified();
	StatusMessage = "LOD removed";
	return true;
}

bool FParticleSystemEditorPanel::MoveLODContent(int32 FromIndex, int32 ToIndex)
{
	if (!EditingSystem || FromIndex == 0 || FromIndex == ToIndex)
	{
		return false;
	}

	const TArray<UParticleEmitter*>& Emitters = EditingSystem->GetEmitters();
	const int32 LODCount = static_cast<int32>(EditingSystem->LODDistances.Num());

	if (FromIndex <= 0 || FromIndex >= LODCount || ToIndex <= 0 || ToIndex >= LODCount || FromIndex == ToIndex)
	{
		return false;
	}

	for (UParticleEmitter* Emitter : Emitters)
	{
		if (!Emitter || static_cast<int32>(Emitter->GetLODLevels().Num()) != LODCount)
		{
			StatusMessage = "Emitter LOD counts are inconsistent.";
			return false;
		}
	}

	for (UParticleEmitter* Emitter : Emitters)
	{
		if (!Emitter->MoveLODLevel(FromIndex, ToIndex))
		{
			StatusMessage = "Failed to move LOD.";
			return false;
		}
	}

	SelectedLODIndex = ToIndex;
	SelectedModule = nullptr;
	SelectedCurveTrackIndex = 0;
	SelectedCurveKeyId = -1;

	if (PreviewActor && PreviewActor->GetParticleSystemComponent())
	{
		PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
		ApplyPreviewLOD();
	}

	MarkAssetModified();
	StatusMessage = "LOD data moved";
	return true;
}

bool FParticleSystemEditorPanel::AddLowestLOD(bool bCopyPrevious)
{
	if (!EditingSystem)
	{
		return false;
	}

	return InsertLOD(static_cast<int32>(EditingSystem->LODDistances.Num()), bCopyPrevious);
}

FParticleSystemEditorPanel::FParticleSystemEditorPanel() = default;

bool FParticleSystemEditorPanel::Init()
{
	SetOpen(false);
	LoadEditorSettings();
	return true;
}

void FParticleSystemEditorPanel::LoadEditorSettings()
{
	const std::filesystem::path Path = std::filesystem::path(EditorResourcePaths::SettingsFolder) / "particle_editor.ini";
	std::ifstream File(Path);

	if (!File.is_open())
	{
		return;
	}

	std::string Line;

	while (std::getline(File, Line))
	{
		const size_t Separator = Line.find('=');

		if (Separator == std::string::npos)
		{
			continue;
		}

		const std::string Key = Line.substr(0, Separator);
		const std::string Value = Line.substr(Separator + 1);

		try
		{
			if (Key == "MainColumnRatio")
			{
				MainColumnRatio = std::stof(Value);
			}
			else if (Key == "TopRowRatio")
			{
				TopRowRatio = std::stof(Value);
			}
			else if (Key == "EmitterColumnWidth")
			{
				EmitterColumnWidth = std::stof(Value);
			}
			else if (Key == "CameraSpeed")
			{
				CameraSpeed = std::stof(Value);
			}
			else if (Key == "SimSpeed")
			{
				SimSpeed = std::stof(Value);
			}
			else if (Key == "ShowBounds")
			{
				bShowBounds = std::stoi(Value) != 0;
			}
			else if (Key == "ShowOriginAxis")
			{
				bShowOriginAxis = std::stoi(Value) != 0;
			}
			else if (Key == "ShowGrid")
			{
				bShowGrid = std::stoi(Value) != 0;
			}
			else if (Key == "ShowSkybox")
			{
				bShowSkybox = std::stoi(Value) != 0;
			}
			else if (Key == "ShowParticleStats")
			{
				bShowParticleStats = std::stoi(Value) != 0;
			}
			else if (Key == "ShowViewport")
			{
				bShowViewport = std::stoi(Value) != 0;
			}
			else if (Key == "ShowEmitters")
			{
				bShowEmitters = std::stoi(Value) != 0;
			}
			else if (Key == "ShowDetails")
			{
				bShowDetails = std::stoi(Value) != 0;
			}
			else if (Key == "ShowCurveEditor")
			{
				bShowCurveEditor = std::stoi(Value) != 0;
			}
			else if (Key == "BackgroundR")
			{
				BackgroundColor[0] = std::stof(Value);
			}
			else if (Key == "BackgroundG")
			{
				BackgroundColor[1] = std::stof(Value);
			}
			else if (Key == "BackgroundB")
			{
				BackgroundColor[2] = std::stof(Value);
			}
		}
		catch (...)
		{
			// 잘못된 설정값은 기본값을 유지한다.
		}
	}

	MainColumnRatio = std::clamp(MainColumnRatio, 0.2f, 0.8f);
	TopRowRatio = std::clamp(TopRowRatio, 0.2f, 0.8f);
	EmitterColumnWidth = std::clamp(EmitterColumnWidth, 180.0f, 420.0f);
	CameraSpeed = std::clamp(CameraSpeed, 0.1f, 20.0f);
	SimSpeed = std::clamp(SimSpeed, 0.1f, 2.0f);

	for (int32 Index = 0; Index < 3; ++Index)
	{
		BackgroundColor[Index] = std::clamp(BackgroundColor[Index], 0.0f, 1.0f);
	}
}

void FParticleSystemEditorPanel::SaveEditorSettings() const
{
	std::error_code Error;
	std::filesystem::create_directories(EditorResourcePaths::SettingsFolder, Error);

	const std::filesystem::path Path = std::filesystem::path(EditorResourcePaths::SettingsFolder) / "particle_editor.ini";
	std::ofstream File(Path);

	if (!File.is_open())
	{
		return;
	}

	File << "MainColumnRatio=" << MainColumnRatio << "\n";
	File << "TopRowRatio=" << TopRowRatio << "\n";
	File << "EmitterColumnWidth=" << EmitterColumnWidth << "\n";
	File << "CameraSpeed=" << CameraSpeed << "\n";
	File << "SimSpeed=" << SimSpeed << "\n";

	File << "ShowBounds=" << bShowBounds << "\n";
	File << "ShowOriginAxis=" << bShowOriginAxis << "\n";
	File << "ShowGrid=" << bShowGrid << "\n";
	File << "ShowSkybox=" << bShowSkybox << "\n";
	File << "ShowParticleStats=" << bShowParticleStats << "\n";

	File << "ShowViewport=" << bShowViewport << "\n";
	File << "ShowEmitters=" << bShowEmitters << "\n";
	File << "ShowDetails=" << bShowDetails << "\n";
	File << "ShowCurveEditor=" << bShowCurveEditor << "\n";

	File << "BackgroundR=" << BackgroundColor[0] << "\n";
	File << "BackgroundG=" << BackgroundColor[1] << "\n";
	File << "BackgroundB=" << BackgroundColor[2] << "\n";
}

FParticleSystemEditorPanel::~FParticleSystemEditorPanel()
{
	SaveEditorSettings();
	Release();
}

void FParticleSystemEditorPanel::Release()
{
	FStatOverlay::ClearParticleStats();

	Client.SetWorld(nullptr);
	Skybox.reset();
	PreviewActor = nullptr;

	if (PreviewWorld)
	{
		PreviewWorld->ClearWorld();
		PreviewWorld->FlushPendingDestruction();
		if (GEngine)
		{
			GEngine->DestroyWorld(PreviewWorld);
		}
		PreviewWorld = nullptr;
	}

	SelectedModule = nullptr;
	SelectedEmitter = nullptr;
	EditingSystem = nullptr;

	SelectedLODIndex = 0;
	SelectedCurveTrackIndex = 0;
	SelectedCurve = nullptr;
	SelectedCurveKeyId = -1;
	bCurveFitRequested = false;
	bAssetDirty = false;
	HiddenCurveTracks.Reset();

	ModuleSearchText[0] = '\0';
	bPaused = false;
	bFocused = false;
	bVisible = false;
	bPreviewRenderStateDirty = false;
	bViewportResizePending = false;
	PendingViewportWidth = 0;
	PendingViewportHeight = 0;

	AssetPath.clear();
	StatusMessage.clear();
}

void FParticleSystemEditorPanel::CreateDefaultSystem()
{
	if (!AddDefaultEmitter(EditingSystem, 3)) return;
	EditingSystem->LODDistances.Add(0.0f);
	EditingSystem->LODDistances.Add(20.0f);
	EditingSystem->LODDistances.Add(30.0f);
}

bool FParticleSystemEditorPanel::NormalizeLODDistances()
{
	if (!EditingSystem)
	{
		return false;
	}

	const TArray<UParticleEmitter*>& Emitters = EditingSystem->GetEmitters();

	if (Emitters.IsEmpty())
	{
		return EditingSystem->LODDistances.IsEmpty() || AreValidParticleLODDistances(EditingSystem->LODDistances);
	}

	int32 CommonLODCount = -1;

	for (UParticleEmitter* Emitter : Emitters)
	{
		if (!Emitter)
		{
			return false;
		}

		const TArray<UParticleLODLevel*>& Levels = Emitter->GetLODLevels();
		const int32 LODCount = static_cast<int32>(Levels.Num());

		if (LODCount <= 0)
		{
			return false;
		}

		for (UParticleLODLevel* LOD : Levels)
		{
			if (!LOD || !LOD->EffectiveRequired || !LOD->EffectiveSpawn || !LOD->EffectiveTypeData)
			{
				return false;
			}
		}

		if (CommonLODCount < 0)
		{
			CommonLODCount = LODCount;
		}
		else if (CommonLODCount != LODCount)
		{
			return false;
		}
	}

	if (EditingSystem->LODDistances.IsEmpty())
	{
		for (int32 Index = 0; Index < CommonLODCount; ++Index)
		{
			EditingSystem->LODDistances.Add(static_cast<float>(Index) * 50.0f);
		}

		return true;
	}

	return static_cast<int32>(EditingSystem->LODDistances.Num()) == CommonLODCount && AreValidParticleLODDistances(EditingSystem->LODDistances);
}

void FParticleSystemEditorPanel::OpenAsset(const FString& InPath)
{
	if (bAssetDirty && !ResolveUnsavedChanges())
	{
		return;
	}

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

		if (EditingSystem->SaveToFile(InPath))
		{
			StatusMessage = "Created";
		}
		else
		{
			StatusMessage = "Created in memory; initial save failed.";
		}
	}

	const bool bHadMissingLODDistances = EditingSystem && EditingSystem->LODDistances.IsEmpty() && !EditingSystem->GetEmitters().IsEmpty();

	if (EditingSystem && !NormalizeLODDistances())
	{
		StatusMessage = "LOD configuration mismatch. Check emitter LOD counts and distances.";
	}
	else if (bHadMissingLODDistances)
	{
		bAssetDirty = true;
		StatusMessage = "Legacy LOD distances initialized. Save to keep the repair.";
	}

	PreviewWorld = UWorld::CreateWorld(EWorldType::Editor, false);
	PreviewActor = PreviewWorld->SpawnActor<AParticleSystemActor>();
	if (PreviewActor && PreviewActor->GetParticleSystemComponent())
	{
		PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
		ApplyPreviewLOD();
	}

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

	InitializeUndoHistory();
	SetOpen(true);
	bFocusRequested = true;
}

bool FParticleSystemEditorPanel::ValidateForSave() const
{
	if (!EditingSystem)
	{
		return false;
	}

	const TArray<UParticleEmitter*>& Emitters = EditingSystem->GetEmitters();

	if (Emitters.IsEmpty())
	{
		return EditingSystem->LODDistances.IsEmpty() || AreValidParticleLODDistances(EditingSystem->LODDistances);
	}

	const int32 ExpectedLODCount = static_cast<int32>(EditingSystem->LODDistances.Num());

	if (ExpectedLODCount <= 0 || !AreValidParticleLODDistances(EditingSystem->LODDistances))
	{
		return false;
	}

	for (UParticleEmitter* Emitter : Emitters)
	{
		if (!Emitter || static_cast<int32>(Emitter->GetLODLevels().Num()) != ExpectedLODCount)
		{
			return false;
		}

		for (UParticleLODLevel* LOD : Emitter->GetLODLevels())
		{
			if (!LOD || !LOD->EffectiveRequired || !LOD->EffectiveSpawn || !LOD->EffectiveTypeData)
			{
				return false;
			}
		}
	}

	return true;
}

bool FParticleSystemEditorPanel::SaveAsset()
{
	if (!EditingSystem || AssetPath.empty())
	{
		return false;
	}

	if (!ValidateForSave())
	{
		StatusMessage = "Cannot save: invalid Particle System configuration.";
		return false;
	}

	if (!EditingSystem->SaveToFile(AssetPath))
	{
		StatusMessage = "Save failed";
		return false;
	}

	bAssetDirty = false;
	StatusMessage = "Saved";
	return true;
}

bool FParticleSystemEditorPanel::SaveAssetAs()
{
	if (!EditingSystem || !ValidateForSave())
	{
		StatusMessage = "Cannot save: invalid Particle System configuration.";
		return false;
	}

	char FilePath[32768] = {};

	OPENFILENAMEA Dialog = {};
	Dialog.lStructSize = sizeof(OPENFILENAMEA);
	Dialog.hwndOwner = nullptr;
	Dialog.lpstrFile = FilePath;
	Dialog.nMaxFile = static_cast<DWORD>(sizeof(FilePath));
	Dialog.lpstrFilter = "Particle System (*.particle)\0*.particle\0All Files (*.*)\0*.*\0";
	Dialog.lpstrDefExt = "particle";

	const std::string InitialDirectory = std::filesystem::absolute("Assets/Particles").make_preferred().string();
	Dialog.lpstrInitialDir = InitialDirectory.c_str();
	Dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

	if (!GetSaveFileNameA(&Dialog))
	{
		return false;
	}

	std::filesystem::path SavePath(FilePath);

	if (SavePath.extension() != ".particle")
	{
		SavePath.replace_extension(".particle");
	}

	const FString NewPath = std::filesystem::absolute(SavePath).lexically_normal().string();

	if (!EditingSystem->SaveToFile(NewPath))
	{
		StatusMessage = "Save As failed";
		return false;
	}

	AssetPath = NewPath;
	bAssetDirty = false;
	StatusMessage = "Saved As";
	InitializeUndoHistory();

	return true;
}

bool FParticleSystemEditorPanel::ResolveUnsavedChanges()
{
	if (!bAssetDirty)
	{
		return true;
	}

	const int32 Result = MessageBoxA(nullptr, "Save changes before continuing?", "Unsaved Changes", MB_YESNOCANCEL | MB_ICONWARNING);

	if (Result == IDYES)
	{
		return SaveAsset();
	}

	return Result == IDNO;
}

void FParticleSystemEditorPanel::Save()
{
	if (bAssetDirty)
	{
		SaveAsset();
	}
}

void FParticleSystemEditorPanel::AddEmitter()
{
	if (!EditingSystem)
	{
		return;
	}

	int32 DesiredLODCount = static_cast<int32>(EditingSystem->LODDistances.Num());

	if (DesiredLODCount <= 0 && EditingSystem->GetEmitters().IsEmpty())
	{
		EditingSystem->LODDistances.Add(0.0f);
		DesiredLODCount = 1;
	}

	if (DesiredLODCount <= 0 || !AreValidParticleLODDistances(EditingSystem->LODDistances))
	{
		StatusMessage = "Create valid LOD distances before adding an emitter.";
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

	UParticleEmitter* NewEmitter = AddDefaultEmitter(EditingSystem, DesiredLODCount);
	if (!NewEmitter) return;

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

	MarkAssetModified();
	StatusMessage = "Emitter added";
}

void FParticleSystemEditorPanel::DuplicateEmitter(UParticleEmitter* Source)
{
	if (!EditingSystem || !Source)
	{
		return;
	}

	if (!AreValidParticleLODDistances(EditingSystem->LODDistances) || static_cast<int32>(Source->GetLODLevels().Num()) != static_cast<int32>(EditingSystem->LODDistances.Num()))
	{
		StatusMessage = "Cannot duplicate emitter: LOD configuration mismatch.";
		return;
	}

	UParticleEmitter* NewEmitter = EditingSystem->DuplicateEmitter(Source);
	if (!NewEmitter)
	{
		StatusMessage = "Failed to duplicate emitter.";
		return;
	}
	NewEmitter->EmitterName = Source->EmitterName + " Copy";

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

	MarkAssetModified();
	StatusMessage = "Emitter duplicated";
}

void FParticleSystemEditorPanel::MarkPreviewRenderStateDirty()
{
	bPreviewRenderStateDirty = true;
}

void FParticleSystemEditorPanel::MarkAssetModified(bool bRebuildModuleInfo)
{
	if (!bUndoRedoInProgress && !bEditTransactionActive && !LastUndoSnapshot.IsEmpty())
	{
		UndoStack.Add(LastUndoSnapshot);

		if (UndoStack.Num() > 64)
		{
			UndoStack.RemoveAt(0);
		}

		RedoStack.Reset();
	}

	bAssetDirty = true;
	MarkPreviewRenderStateDirty();

	if (bRebuildModuleInfo && PreviewActor && PreviewActor->GetParticleSystemComponent())
	{
		PreviewActor->GetParticleSystemComponent()->UpdateEmitterStrides();
	}

	if (EditingSystem)
	{
		FJsonArchive::SaveObjectToBytes(EditingSystem, LastUndoSnapshot);
	}

	bEditTransactionActive = ImGui::IsAnyItemActive();
}

void FParticleSystemEditorPanel::InitializeUndoHistory()
{
	UndoStack.Reset();
	RedoStack.Reset();
	LastUndoSnapshot.Reset();
	bEditTransactionActive = false;

	if (EditingSystem)
	{
		FJsonArchive::SaveObjectToBytes(EditingSystem, LastUndoSnapshot);
	}
}

bool FParticleSystemEditorPanel::RestoreSnapshot(const TArray<uint8>& Snapshot)
{
	UParticleSystem* RestoredSystem = NewObject<UParticleSystem>();

	if (!RestoredSystem || !FJsonArchive::LoadObjectFromBytes(RestoredSystem, Snapshot))
	{
		StatusMessage = "Failed to restore Particle System snapshot.";
		return false;
	}

	RestoredSystem->PrepareLODModules();
	EditingSystem = RestoredSystem;
	SelectedLODIndex = std::clamp(SelectedLODIndex, 0, EditingSystem->GetLODCount() - 1);
	SelectedEmitter = nullptr;
	SelectedModule = nullptr;
	SelectedCurve = nullptr;
	SelectedCurveTrackIndex = 0;
	SelectedCurveKeyId = -1;
	HiddenCurveTracks.Reset();
	bAssetDirty = true;

	if (PreviewActor && PreviewActor->GetParticleSystemComponent())
	{
		PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
		ApplyPreviewLOD();
	}

	FJsonArchive::SaveObjectToBytes(EditingSystem, LastUndoSnapshot);
	MarkPreviewRenderStateDirty();

	return true;
}

void FParticleSystemEditorPanel::Undo()
{
	if (UndoStack.IsEmpty() || !EditingSystem)
	{
		return;
	}

	TArray<uint8> CurrentSnapshot;

	if (!FJsonArchive::SaveObjectToBytes(EditingSystem, CurrentSnapshot))
	{
		return;
	}

	const TArray<uint8> TargetSnapshot = UndoStack.Last();

	bUndoRedoInProgress = true;
	const bool bRestored = RestoreSnapshot(TargetSnapshot);
	bUndoRedoInProgress = false;

	if (!bRestored)
	{
		return;
	}

	UndoStack.RemoveAt(static_cast<int32>(UndoStack.Num()) - 1);
	RedoStack.Add(CurrentSnapshot);

	StatusMessage = "Undo";
}

void FParticleSystemEditorPanel::Redo()
{
	if (RedoStack.IsEmpty() || !EditingSystem)
	{
		return;
	}

	TArray<uint8> CurrentSnapshot;

	if (!FJsonArchive::SaveObjectToBytes(EditingSystem, CurrentSnapshot))
	{
		return;
	}

	const TArray<uint8> TargetSnapshot = RedoStack.Last();

	bUndoRedoInProgress = true;
	const bool bRestored = RestoreSnapshot(TargetSnapshot);
	bUndoRedoInProgress = false;

	if (!bRestored)
	{
		return;
	}

	RedoStack.RemoveAt(static_cast<int32>(RedoStack.Num()) - 1);
	UndoStack.Add(CurrentSnapshot);

	StatusMessage = "Redo";
}

void FParticleSystemEditorPanel::Tick(float DeltaTime)
{
	if (!IsOpen() || !PreviewWorld || !bVisible)
	{
		return;
	}

	Client.TickInput(DeltaTime, false, false, 0, 4, 1);

	if (!PreviewActor || !PreviewActor->GetParticleSystemComponent())
	{
		return;
	}

	UParticleSystemComponent* Component = PreviewActor->GetParticleSystemComponent();

	if (bPreviewRenderStateDirty)
	{
		Component->MarkRenderStateDirty();
		bPreviewRenderStateDirty = false;
	}

	if (!bPaused)
	{
		const float ScaledDelta = std::max(0.0f, DeltaTime * SimSpeed);
		Component->TickComponent(ScaledDelta);
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

		Engine->RenderPreviewViewport(&Client, PreviewWorld, nullptr, bShowSkybox ? Skybox.get() : nullptr, bShowGrid, bShowBounds ? PreviewComponent : nullptr, bShowOriginAxis, { BackgroundColor[0], BackgroundColor[1], BackgroundColor[2], 1.0f });

		FParticleStatSnapshot ParticleStats{};

		if (PreviewComponent)
		{
			ParticleStats.bValid = true;

			const TArray<FParticleEmitterInstance>& Instances = PreviewComponent->GetEmitterInstances();

			for (const FParticleEmitterInstance& Instance : Instances)
			{
				if (Instance.IsGpuEmitter())
				{
					continue;
				}

				ParticleStats.CpuActiveParticles += static_cast<uint32>(std::max(0, Instance.ActiveParticles));
				ParticleStats.CpuCapacity += static_cast<uint32>(std::max(0, Instance.MaxActiveParticles));

				if (Instance.ParticleData && Instance.ParticleIndices)
				{
					ParticleStats.CpuBufferBytes += static_cast<uint64>(Instance.MaxActiveParticles) * (static_cast<uint64>(Instance.ParticleStride) + sizeof(uint16));
				}
			}

			const float StatsDeltaTime = bPaused ? 0.0f : PreviewComponent->GetStatsDeltaTime();

			if (StatsDeltaTime > 0.0f)
			{
				ParticleStats.SpawnRequestsPerSecond = static_cast<float>(PreviewComponent->GetStatsSpawnedThisTick()) / StatsDeltaTime;
				ParticleStats.CpuExpiredPerSecond = static_cast<float>(PreviewComponent->GetStatsExpiredThisTick()) / StatsDeltaTime;
			}

			const FSceneRenderData& RenderData = Client.GetRenderData();
			ParticleStats.DrawCalls = RenderData.RenderStats.ParticleDrawCalls;

			for (const FMeshBatch& Batch : RenderData.Collector.GetMeshBatches())
			{
				if (Batch.bIsParticle)
				{
					++ParticleStats.MeshBatches;
				}
			}
		}

		FStatOverlay::SetParticleStats(ParticleStats);

	}
	else
	{
		FStatOverlay::ClearParticleStats();
	}
}

void FParticleSystemEditorPanel::RenderMenuBar()
{
	if (!ImGui::BeginMenuBar()) return;

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save", "Ctrl+S", false, EditingSystem != nullptr))
		{
			SaveAsset();
		}

		if (ImGui::MenuItem("Save As...", nullptr, false, EditingSystem != nullptr))
		{
			SaveAssetAs();
		}

		if (ImGui::MenuItem("Reload", nullptr, false, !AssetPath.empty()))
		{
			OpenAsset(AssetPath);
		}
		if (ImGui::MenuItem("Find in Content Drawer", nullptr, false, !AssetPath.empty()))
		{
			if (!FContentDrawerPanel::FocusAsset(AssetPath))
			{
				StatusMessage = "Asset not found in Content Drawer";
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Close"))
		{
			if (ResolveUnsavedChanges())
			{
				Release();
				SetOpen(false);
			}
		}
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

	if (bAssetDirty)
	{
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "* Modified");
	}
	else
	{
		ImGui::SameLine();
		ImGui::TextDisabled("Saved");
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
	bool bConsistent = LODCount > 0 && !EditingSystem->GetEmitters().IsEmpty() && AreValidParticleLODDistances(EditingSystem->LODDistances);

	for (UParticleEmitter* Emitter : EditingSystem->GetEmitters())
	{
		if (!Emitter || static_cast<int32>(Emitter->GetLODLevels().Num()) != LODCount)
		{
			bConsistent = false;
			break;
		}
	}

	if (!bConsistent)
	{
		ImGui::TextDisabled("LOD configuration mismatch");
		return;
	}

	SelectedLODIndex = std::clamp(SelectedLODIndex, 0, LODCount - 1);

	if (ImGui::Button("<"))
	{
		SelectedLODIndex = std::max(0, SelectedLODIndex - 1);
		SelectedModule = nullptr;
		SelectedCurveTrackIndex = 0;
		SelectedCurveKeyId = -1;
		ApplyPreviewLOD();
	}

	ImGui::SameLine();

	char LODLabel[64];
	snprintf(LODLabel, sizeof(LODLabel), "LOD %d / %d", SelectedLODIndex, LODCount - 1);
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
		SelectedModule = nullptr;
		SelectedCurveTrackIndex = 0;
		SelectedCurveKeyId = -1;
		ApplyPreviewLOD();
	}

	ImGui::SameLine();
	ImGui::TextDisabled("Distance: %.1f", EditingSystem->LODDistances[SelectedLODIndex]);

	ImGui::SameLine();
	ImGui::Checkbox("Copy Previous LOD", &bCopyPreviousLOD);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Create independent copies of the previous LOD modules. Leave unchecked to inherit them.");

	ImGui::SameLine();

	ImGui::BeginDisabled(SelectedLODIndex == 0);
	if (ImGui::Button("+ Insert Before"))
	{
		InsertLOD(SelectedLODIndex, bCopyPreviousLOD);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();

	if (ImGui::Button("+ Add Lowest LOD"))
	{
		AddLowestLOD(bCopyPreviousLOD);
	}

	ImGui::SameLine();

	ImGui::BeginDisabled(SelectedLODIndex <= 0 || EditingSystem->LODDistances.Num() <= 1);

	if (ImGui::Button("Remove LOD"))
	{
		RemoveLOD(SelectedLODIndex);
	}

	ImGui::EndDisabled();

	ImGui::SameLine();

	ImGui::BeginDisabled(SelectedLODIndex <= 1);
	if (ImGui::Button("Move Data Up"))
	{
		MoveLODContent(SelectedLODIndex, SelectedLODIndex - 1);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();

	ImGui::BeginDisabled(SelectedLODIndex == 0 || SelectedLODIndex >= LODCount - 1);
	if (ImGui::Button("Move Data Down"))
	{
		MoveLODContent(SelectedLODIndex, SelectedLODIndex + 1);
	}
	ImGui::EndDisabled();
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
		uint64 CpuMemoryBytes = 0;
		int32 CpuActiveParticles = 0;
		int32 CpuCapacity = 0;

		for (const FParticleEmitterInstance& Instance : Instances)
		{
			if (Instance.IsGpuEmitter())
			{
				continue;
			}

			CpuActiveParticles += Instance.ActiveParticles;
			CpuCapacity += Instance.MaxActiveParticles;

			if (Instance.ParticleData && Instance.ParticleIndices)
			{
				CpuMemoryBytes += static_cast<uint64>(Instance.MaxActiveParticles) *
					(Instance.ParticleStride + sizeof(uint16));
			}
		}
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const float LineH = ImGui::GetTextLineHeightWithSpacing();
		float Y = ImagePos.y + Size.y - LineH * static_cast<float>(Instances.Num()) - 6.0f;
		for (int32 Index = 0; Index < static_cast<int32>(Instances.Num()); ++Index)
		{
			const FParticleEmitterInstance& Instance = Instances[Index];
			const char* EmitterName = Instance.SpriteTemplate ? Instance.SpriteTemplate->EmitterName.c_str() : "Emitter";

			char Buf[160];

			const UParticleLODLevel* LOD = Instance.CurrentLODLevel;
			const EParticleEmitterType Type = LOD && LOD->EffectiveTypeData ? LOD->EffectiveTypeData->GetEmitterType() : EParticleEmitterType::Sprite;

			const char* TypeName = "Sprite";

			if (Type == EParticleEmitterType::GpuSprite)
			{
				TypeName = "GPU Sprite";
			}
			else if (Type == EParticleEmitterType::Mesh)
			{
				TypeName = "Mesh";
			}

			if (Type == EParticleEmitterType::GpuSprite)
			{
				snprintf(Buf, sizeof(Buf), "%s | %s | LOD %d | Capacity %d", EmitterName, TypeName, Instance.CurrentLODLevelIndex, Instance.MaxActiveParticles);
			}
			else
			{
				snprintf(Buf, sizeof(Buf), "%s | %s | LOD %d | Active %d / %d", EmitterName, TypeName, Instance.CurrentLODLevelIndex, Instance.ActiveParticles, Instance.MaxActiveParticles);
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
	int32 EmitterMoveFromIndex = -1;
	int32 EmitterMoveToIndex = -1;

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

			const bool bEmitterColumnVisible = ImGui::BeginChild("EmitterColumn", ImVec2(EmitterColumnWidth, 0.0f), ImGuiChildFlags_Borders);

			if (bEmitterColumnVisible)
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

				ImGui::SameLine();

				ImGui::BeginDisabled(EmitterIndex <= 0);
				if (ImGui::SmallButton("<"))
				{
					EmitterMoveFromIndex = EmitterIndex;
					EmitterMoveToIndex = EmitterIndex - 1;
				}
				ImGui::EndDisabled();

				ImGui::SameLine();

				ImGui::BeginDisabled(EmitterIndex >= static_cast<int32>(Emitters.Num()) - 1);
				if (ImGui::SmallButton(">"))
				{
					EmitterMoveFromIndex = EmitterIndex;
					EmitterMoveToIndex = EmitterIndex + 1;
				}
				ImGui::EndDisabled();

				char NameBuffer[128] = {};
				strncpy_s(NameBuffer, Emitter->EmitterName.c_str(), _TRUNCATE);

				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::InputText("##EmitterName", NameBuffer, sizeof(NameBuffer)))
				{
					Emitter->EmitterName = NameBuffer;
					MarkAssetModified();
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
				const bool bInheritedLayout = SelectedLODIndex > 0 && Emitter->UsesModuleInheritance();

				ImGui::Separator();

				if (ImGui::Checkbox("Enabled", &LOD->bEnabled))
				{
					MarkAssetModified();
				}
				ImGui::SameLine();
				ImGui::BeginDisabled(true);
				ImGui::Checkbox("Solo (Unsupported)", &Emitter->bIsSolo);
				ImGui::EndDisabled();

				ImGui::TextDisabled("Emitter Play Rate");
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::DragFloat("##EmitterPlayRate", &Emitter->PlayRate, 0.05f, 0.0f, 20.0f))
				{
					MarkAssetModified();
				}

				ImGui::Separator();
				ImGui::TextDisabled("EMITTER TYPE");

				int32 TypeIndex = 0;
				UParticleModuleTypeDataBase* TypeData = LOD->EffectiveTypeData;

				if (TypeData && TypeData->GetEmitterType() == EParticleEmitterType::GpuSprite)
				{
					TypeIndex = 1;
				}
				else if (TypeData && TypeData->GetEmitterType() == EParticleEmitterType::Mesh)
				{
					TypeIndex = 2;
				}
				else if (TypeData && TypeData->GetEmitterType() == EParticleEmitterType::Beam)
				{
					TypeIndex = 3;
				}
				else if (TypeData && TypeData->GetEmitterType() == EParticleEmitterType::Ribbon)
				{
					TypeIndex = 4;
				}
				else if (TypeData && TypeData->GetEmitterType() == EParticleEmitterType::Afterimage)
				{
					TypeIndex = 5;
				}

				ImGui::SetNextItemWidth(-1.0f);

				ImGui::BeginDisabled(!LOD->OwnsModule(TypeData));
				if (ImGui::Combo("##EmitterType", &TypeIndex, "Sprite\0GPU Sprite\0Mesh\0Beam\0Ribbon\0Afterimage\0"))
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
					else if (TypeIndex == 3)
					{
						TargetClass = UParticleModuleTypeDataBeam::StaticClass();
					}
					else if (TypeIndex == 4)
					{
						TargetClass = UParticleModuleTypeDataRibbon::StaticClass();
					}
					else if (TypeIndex == 5)
					{
						TargetClass = UParticleModuleTypeDataAfterimage::StaticClass();
					}

					const bool bWasSelected = SelectedEmitter == Emitter && SelectedModule == TypeData;
					UParticleModuleTypeDataBase* NewTypeData = LOD->SetTypeData(TargetClass);

					if (auto* MeshType = Cast<UParticleModuleTypeDataMesh>(NewTypeData))
					{
						if (!MeshType->Mesh)
						{
							MeshType->Mesh = UAssetManager::GetAssetByPath<UStaticMesh>("Cube");
						}

						if (LOD->RequiredModule) LOD->RequiredModule->Material = nullptr;
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

					MarkAssetModified();
				}

				ImGui::EndDisabled();

				UParticleModule* LODModuleToDuplicate = nullptr;
				UParticleModule* LODModuleToInherit = nullptr;
				bool bFromHighest = false;
				UParticleModule* ModuleToRemove = nullptr;
				UParticleModule* ModuleToMove = nullptr;
				UParticleModule* MoveTargetModule = nullptr;
				UParticleModule* ModuleToDuplicate = nullptr;
				UParticleModule* ModuleToCopyToOtherEmitter = nullptr;
				UParticleEmitter* CopyTargetEmitter = nullptr;
				bool bMoveAfterTarget = false;
				bool bModuleEnabledChanged = false;

				auto DrawModuleRow = [&](UParticleModule* Module, const char* Stage, bool bRemovable)
				{
					if (!Module)
					{
						return;
					}

					ImGui::PushID(Module);

					const bool bCanToggleEnabled = bRemovable || Cast<UParticleModuleSpawn>(Module) != nullptr;

					if (bCanToggleEnabled)
					{
						ImGui::BeginDisabled(!LOD->OwnsModule(Module));
						bModuleEnabledChanged |= ImGui::Checkbox("##ModuleEnabled", &Module->bEnabled);
						ImGui::EndDisabled();
					}
					else
					{
						ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
					}

					ImGui::SameLine();

					const float RowWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
					const float TextWidth = std::max(1.0f, RowWidth - EditorUIScale::Px(12.0f));
					const char* DisplayName = Module->GetDisplayName();

					const ImVec2 NameSize = ImGui::CalcTextSize(DisplayName, nullptr, false, TextWidth);
					const FString StageLabel = FString(Stage) + (LOD->OwnsModule(Module) ? "" : " / Inherited");
					const ImVec2 StageSize = ImGui::CalcTextSize(StageLabel.c_str(), nullptr, false, TextWidth);
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
					ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(TextPosition.x, TextPosition.y + NameSize.y + EditorUIScale::Px(2.0f)), ImGui::GetColorU32(ImGuiCol_TextDisabled), StageLabel.c_str(), nullptr, TextWidth);

					if (bInheritedLayout && ImGui::BeginPopupContextItem("ModuleLODContext"))
					{
						if (ImGui::MenuItem("Duplicate from Highest"))
						{
							LODModuleToDuplicate = Module;
							bFromHighest = true;
						}
						if (ImGui::MenuItem("Duplicate from Higher"))
						{
							LODModuleToDuplicate = Module;
							bFromHighest = false;
						}
						if (ImGui::MenuItem("Inherit from Higher", nullptr, false, LOD->OwnsModule(Module)))
							LODModuleToInherit = Module;
						ImGui::EndPopup();
					}

					if (bRemovable && !bInheritedLayout && ImGui::BeginPopupContextItem("ModuleContext"))
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
								if (!TargetEmitter || TargetEmitter == Emitter || (SelectedLODIndex > 0 && TargetEmitter->UsesModuleInheritance()))
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

					if (bRemovable && !bInheritedLayout && ImGui::BeginDragDropSource())
					{
						UParticleModule* DraggedModule = Module;
						ImGui::SetDragDropPayload("PARTICLE_MODULE", &DraggedModule, sizeof(DraggedModule));
						ImGui::TextUnformatted(DisplayName);
						ImGui::EndDragDropSource();
					}

					if (bRemovable && !bInheritedLayout && ImGui::BeginDragDropTarget())
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
					DrawModuleRow(LOD->EffectiveTypeData, "RENDERER", false);
					DrawModuleRow(LOD->EffectiveRequired, "REQUIRED", false);
				}

				if (ImGui::CollapsingHeader("Spawn", ImGuiTreeNodeFlags_DefaultOpen))
				{
					DrawModuleRow(LOD->EffectiveSpawn, "SPAWN", false);
				}

				if (ImGui::CollapsingHeader("Modules", ImGuiTreeNodeFlags_DefaultOpen))
				{
					for (UParticleModule* Module : LOD->EffectiveModules)
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

					ImGui::BeginDisabled(bInheritedLayout);
					if (ImGui::Button("+ Add Module", ImVec2(-1.0f, 0.0f)))
					{
						ModuleSearchText[0] = '\0';
						ImGui::OpenPopup("AddModulePopup");
					}

					ImGui::EndDisabled();
					if (!bInheritedLayout && ImGui::BeginPopup("AddModulePopup"))
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
							{ "Color Over Life", "Particle Update", UParticleModuleColorOverLife::StaticClass() },
							{ "Size By Life", "Particle Update", UParticleModuleSizeByLife::StaticClass() },
							{ "Velocity Over Life", "Particle Update", UParticleModuleVelocityOverLife::StaticClass() },
							{ "Rotation Rate Over Life", "Particle Update", UParticleModuleRotationRateOverLife::StaticClass() },
							{ "SubImage Index", "Particle Update", UParticleModuleSubUV::StaticClass() },
							{ "Beam Source", "Beam", UParticleModuleBeamSource::StaticClass() },
							{ "Beam Target", "Beam", UParticleModuleBeamTarget::StaticClass() },
							{ "Beam Noise", "Beam", UParticleModuleBeamNoise::StaticClass() },
							{ "Spawn Per Unit", "Ribbon", UParticleModuleSpawnPerUnit::StaticClass() },
							{ "Collision", "Collision", UParticleModuleCollision::StaticClass() },
							{ "Event Generator", "Event", UParticleModuleEventGenerator::StaticClass() }
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

									MarkAssetModified(true);
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

				if (bModuleEnabledChanged) MarkAssetModified(true);

				if (LODModuleToDuplicate)
				{
					if (UParticleModule* Copy = Emitter->DuplicateLODModule(SelectedLODIndex, LODModuleToDuplicate, bFromHighest))
					{
						SelectedEmitter = Emitter;
						SelectedModule = Copy;
						SelectedCurve = nullptr;
						SelectedCurveTrackIndex = 0;
						SelectedCurveKeyId = -1;
						if (PreviewActor && PreviewActor->GetParticleSystemComponent())
							PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
						MarkAssetModified(true);
					}
				}
				if (LODModuleToInherit && Emitter->InheritLODModule(SelectedLODIndex, LODModuleToInherit))
				{
					SelectedModule = nullptr;
					SelectedCurve = nullptr;
					SelectedCurveTrackIndex = 0;
					SelectedCurveKeyId = -1;
					HiddenCurveTracks.Reset();
					if (PreviewActor && PreviewActor->GetParticleSystemComponent())
						PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
					MarkAssetModified(true);
				}
				if (ModuleToRemove)
				{
					if (SelectedModule == ModuleToRemove)
					{
						SelectedModule = nullptr;
						SelectedCurveTrackIndex = 0;
						SelectedCurveKeyId = -1;
					}

					if (SelectedLODIndex == 0) Emitter->RemoveBaseModule(ModuleToRemove);
					else LOD->RemoveModule(ModuleToRemove);

					MarkAssetModified(true);
				}
				else if (ModuleToDuplicate)
				{
					if (UParticleModule* Duplicated = CopyModuleToLOD(LOD, ModuleToDuplicate))
					{
						SelectedEmitter = Emitter;
						SelectedModule = Duplicated;
						SelectedCurveTrackIndex = 0;
						SelectedCurveKeyId = -1;

						MarkAssetModified(true);
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
							MarkAssetModified(true);
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

							MarkAssetModified(true);
						}
					}
				}
			}

			ImGui::EndChild();
			ImGui::PopID();

			if (EmitterIndex + 1 < static_cast<int32>(Emitters.Num()))
			{
				ImGui::SameLine(0.0f, EditorUIScale::Px(2.0f));

				ImGui::PushID(EmitterIndex);
				ImGui::InvisibleButton("##EmitterColumnResize", ImVec2(EditorUIScale::Px(6.0f), ImGui::GetContentRegionAvail().y));

				if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				{
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
				}

				if (ImGui::IsItemActive())
				{
					EmitterColumnWidth = std::clamp(EmitterColumnWidth + ImGui::GetIO().MouseDelta.x, 180.0f, 420.0f);
				}

				ImGui::PopID();
				ImGui::SameLine(0.0f, EditorUIScale::Px(2.0f));
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

		MarkAssetModified();
	}
	else if (EmitterToDuplicate)
	{
		DuplicateEmitter(EmitterToDuplicate);
	}
	else if (EmitterMoveFromIndex >= 0 && EmitterMoveToIndex >= 0)
	{
		if (EditingSystem->MoveEmitter(EmitterMoveFromIndex, EmitterMoveToIndex))
		{
			if (PreviewActor && PreviewActor->GetParticleSystemComponent())
			{
				PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
				ApplyPreviewLOD();
			}

			MarkAssetModified();
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

	auto DrawFloatProperty = [this](const char* Label, const char* Id, float* Value, float Speed, float Min, float Max)
	{
		ImGui::TextUnformatted(Label);
		ImGui::SetNextItemWidth(-1.0f);

		if (ImGui::DragFloat(Id, Value, Speed, Min, Max, "%.3f"))
		{
			MarkAssetModified();
		}
	};

	auto DrawTypeDataMeshPicker = [&](UParticleLODLevel* LOD, UParticleModuleTypeDataMesh* MeshType)
	{
		if (!LOD || !MeshType)
		{
			return;
		}
		const bool bEditable = LOD->OwnsModule(MeshType);
		UStaticMesh* Mesh = MeshType->Mesh;
		ImGui::BeginDisabled(!bEditable);
		if (FContentDrawerPanel::DrawStaticMeshPicker("Static Mesh", Mesh, "(None)") && bEditable)
		{
			MeshType->Mesh = Mesh;
			if (LOD->RequiredModule) LOD->RequiredModule->Material = nullptr;
			MarkAssetModified();
		}
		ImGui::EndDisabled();
	};

	UParticleLODLevel* SelectedLOD = (SelectedEmitter && !SelectedEmitter->GetLODLevels().IsEmpty())
		? SelectedEmitter->GetLODLevels()[0]
		: nullptr;

	if (SelectedModule)
	{
		ImGui::Text("Module: %s", SelectedModule->GetDisplayName());
		UParticleLODLevel* OwnerLOD = GetParticleEditorLOD(SelectedEmitter, SelectedLODIndex);
		const bool bEditable = OwnerLOD && OwnerLOD->OwnsModule(SelectedModule);
		if (!bEditable) ImGui::TextDisabled("Inherited from a higher LOD");
		ImGui::BeginDisabled(!bEditable);

		const bool bCanToggleEnabled =
			!Cast<UParticleModuleRequired>(SelectedModule) &&
			!Cast<UParticleModuleTypeDataBase>(SelectedModule);

		if (bCanToggleEnabled)
		{
			if (ImGui::Checkbox("Enabled", &SelectedModule->bEnabled))
			{
				MarkAssetModified(true);
			}
		}
		else
		{
			ImGui::TextDisabled("Always Enabled");
		}

		ImGui::Separator();

		if (auto* MeshType = Cast<UParticleModuleTypeDataMesh>(SelectedModule))
		{
			DrawSection("RENDERER");
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
		else if (auto* BeamType = Cast<UParticleModuleTypeDataBeam>(SelectedModule))
		{
			DrawSection("BEAM");
			bool bChanged = false;
			bChanged |= ImGui::Combo("Beam Method", &BeamType->BeamMethod, "Distance\0Target\0");
			bChanged |= ImGui::DragInt("Max Beam Count", &BeamType->MaxBeamCount, 1.0f, 1, 64);
			bChanged |= ImGui::DragInt("Segments", &BeamType->Segments, 1.0f, 1, 512);
			bChanged |= ImGui::DragInt("Sheets", &BeamType->Sheets, 1.0f, 1, 8);
			bChanged |= ImGui::DragFloat("Distance", &BeamType->Distance, 0.1f, 0.01f, 1000.0f);
			bChanged |= ImGui::DragFloat("Speed", &BeamType->Speed, 0.5f, 0.0f, 1000.0f);
			bChanged |= ImGui::DragFloat("Texture Tile", &BeamType->TextureTile, 0.1f, 0.01f, 64.0f);
			bChanged |= ImGui::DragFloat("Texture Tile Distance", &BeamType->TextureTileDistance, 0.1f, 0.0f, 100.0f);
			bChanged |= ImGui::Combo("Taper Method", &BeamType->TaperMethod, "None\0Full\0Partial\0");
			if (bChanged) MarkAssetModified();
			if (BeamType->TaperMethod != 0)
			{
				DrawDistributionFloat("Taper Factor", BeamType->TaperFactor, BeamType, 1.0f, 0.05f, 0.0f, 10.0f);
				DrawDistributionFloat("Taper Scale", BeamType->TaperScale, BeamType, 1.0f, 0.05f, 0.0f, 10.0f);
			}
		}
		else if (auto* RibbonType = Cast<UParticleModuleTypeDataRibbon>(SelectedModule))
		{
			DrawSection("RIBBON");
			bool bChanged = false;
			bChanged |= ImGui::DragInt("Max Trail Count", &RibbonType->MaxTrailCount, 1.0f, 1, 64);
			bChanged |= ImGui::DragInt("Max Particles Per Trail", &RibbonType->MaxParticleInTrailCount, 1.0f, 2, 4096);
			bChanged |= ImGui::DragInt("Sheets Per Trail", &RibbonType->SheetsPerTrail, 1.0f, 1, 8);
			bChanged |= ImGui::Combo("Facing Mode", &RibbonType->FacingMode, "Camera\0Source Up\0World Up\0");
			bChanged |= ImGui::DragFloat("Tiling Distance", &RibbonType->TilingDistance, 0.1f, 0.0f, 100.0f);
			bChanged |= ImGui::Checkbox("Clip Source Segment", &RibbonType->bClipSourceSegment);
			if (bChanged) MarkAssetModified();
		}
		else if (Cast<UParticleModuleTypeDataAfterimage>(SelectedModule))
		{
			DrawSection("AFTERIMAGE");
			ImGui::TextWrapped("Captures the owning character's skeletal mesh pose and world transform when each particle spawns.");
		}
		else if (auto* Req = Cast<UParticleModuleRequired>(SelectedModule))
		{
			DrawSection("EMITTER SETTINGS");
			if (ImGui::DragInt("Max Particles", &Req->MaxParticles, 1.0f, 1, 65535))
			{
				MarkAssetModified();
			}
			if (ImGui::DragFloat("Emitter Duration", &Req->EmitterDuration, 0.05f, 0.0f, 300.0f))
			{
				MarkAssetModified();
			}
			if (ImGui::Checkbox("Looping", &Req->bLooping))
			{
				MarkAssetModified();
			}
			if (ImGui::Checkbox("Use Local Space", &Req->bUseLocalSpace))
			{
				MarkAssetModified();
			}

			DrawSection("RENDERING");
			const bool bMeshEmitter = OwnerLOD && Cast<UParticleModuleTypeDataMesh>(OwnerLOD->EffectiveTypeData);
			const bool bAfterimageEmitter = OwnerLOD && Cast<UParticleModuleTypeDataAfterimage>(OwnerLOD->EffectiveTypeData);
			const bool bBeamTrailEmitter = OwnerLOD && (Cast<UParticleModuleTypeDataBeam>(OwnerLOD->EffectiveTypeData) || Cast<UParticleModuleTypeDataRibbon>(OwnerLOD->EffectiveTypeData));
			const uint32 UsageFlag = static_cast<uint32>(bAfterimageEmitter ? EMaterialUsage::StaticMesh : (bMeshEmitter ? EMaterialUsage::ParticleMesh : (bBeamTrailEmitter ? EMaterialUsage::ParticleBeamTrail : EMaterialUsage::ParticleSprite)));
			UMaterial* FallbackMaterial = (!Req->Material && !bMeshEmitter)
				? UAssetManager::GetAssetByPath<UMaterial>(bAfterimageEmitter ? "Materials/AfterimageMaterial.material" : (bBeamTrailEmitter ? "BeamTrailMaterial" : "SubUVMaterial"))
				: nullptr;
			UMaterialInterface* Material = Req->Material;
			if (FContentDrawerPanel::DrawMaterialPicker("Material", Material, "(Default)", FallbackMaterial, UsageFlag) && bEditable)
			{
				Req->Material = Material;
				MarkAssetModified();
			}
		}
		else if (auto* Spawn = Cast<UParticleModuleSpawn>(SelectedModule))
		{
			DrawSection("SPAWN");

			if (ImGui::DragFloat("Spawn Rate", &Spawn->Rate, 0.5f, 0.0f, 10000.0f))
			{
				MarkAssetModified();
			}

			if (ImGui::SmallButton("+ Burst"))
			{
				Spawn->Bursts.Add(FParticleBurst{});
				MarkAssetModified();
			}

			for (int32 i = 0; i < static_cast<int32>(Spawn->Bursts.Num()); ++i)
			{
				ImGui::PushID(i);

				if (ImGui::DragFloat("Time", &Spawn->Bursts[i].Time, 0.01f, 0.0f, 60.0f))
				{
					MarkAssetModified();
				}
				if (ImGui::DragInt("Count", &Spawn->Bursts[i].Count, 1.0f, 0, 10000))
				{
					MarkAssetModified();
				}

				if (ImGui::SmallButton("Remove Burst"))
				{
					Spawn->Bursts.RemoveAt(static_cast<uint32>(i));
					MarkAssetModified();
					ImGui::PopID();
					break;
				}

				ImGui::PopID();
			}
		}
		else if (UParticleModuleLifetime* Life = Cast<UParticleModuleLifetime>(SelectedModule))
		{
			DrawSection("LIFETIME");
			DrawDistributionFloat("Lifetime", Life->Lifetime, Life, 1.0f, 0.05f, 0.01f, 60.0f);
		}
		else if (UParticleModuleLocation* Loc = Cast<UParticleModuleLocation>(SelectedModule))
		{
			DrawSection("INITIAL LOCATION");
			DrawDistributionVector("Start Location", Loc->StartLocation, Loc, FVector::ZeroVector, false, 0.05f);
		}
		else if (UParticleModuleVelocity* Vel = Cast<UParticleModuleVelocity>(SelectedModule))
		{
			DrawSection("INITIAL VELOCITY");
			DrawDistributionVector("Start Velocity", Vel->StartVelocity, Vel, FVector::ZeroVector, false, 0.05f);
		}
		else if (UParticleModuleColor* Col = Cast<UParticleModuleColor>(SelectedModule))
		{
			DrawSection("INITIAL COLOR");
			DrawDistributionVector("Start Color", Col->StartColor, Col, FVector::OneVector, true, 0.01f);
			DrawDistributionFloat("Start Alpha", Col->StartAlpha, Col, 1.0f, 0.01f, 0.0f, 1.0f);
		}
		else if (UParticleModuleColorOverLife* ColOverLife = Cast<UParticleModuleColorOverLife>(SelectedModule))
		{
			DrawSection("COLOR OVER LIFE");
			DrawDistributionVector("Color Over Life", ColOverLife->ColorScale, ColOverLife, FVector::OneVector, true, 0.01f);
			bool bFadeChanged = ImGui::Checkbox("Use Fade In/Out", &ColOverLife->bUseFadeInOut);
			if (ColOverLife->bUseFadeInOut)
			{
				bFadeChanged |= ImGui::SliderFloat("Fade In End", &ColOverLife->FadeInEnd, 0.0f, 1.0f);
				bFadeChanged |= ImGui::SliderFloat("Fade Out Start", &ColOverLife->FadeOutStart, 0.0f, 1.0f);
			}
			else
				DrawDistributionFloat("Alpha Over Life", ColOverLife->AlphaScale, ColOverLife, 1.0f, 0.01f, 0.0f, 1.0f);
			if (bFadeChanged) { MarkSelectedLODBuildDirty(); MarkAssetModified(); }
		}
		else if (auto* Sz = Cast<UParticleModuleSize>(SelectedModule))
		{
			DrawSection("INITIAL SIZE");
			DrawDistributionVector("Start Size", Sz->StartSize, Sz, FVector::OneVector, false, 0.05f);
		}
		else if (auto* SzLife = Cast<UParticleModuleSizeByLife>(SelectedModule))
		{
			DrawSection("SIZE BY LIFE");
			DrawDistributionVector("Size Scale", SzLife->LifeScale, SzLife, FVector::OneVector, false, 0.05f);
		}
		else if (auto* VelLife = Cast<UParticleModuleVelocityOverLife>(SelectedModule))
		{
			DrawSection("VELOCITY OVER LIFE");
			DrawDistributionVector("Velocity Scale", VelLife->VelocityScale, VelLife, FVector::OneVector, false, 0.05f);
		}
		else if (auto* Rot = Cast<UParticleModuleRotation>(SelectedModule))
		{
			DrawSection("INITIAL ROTATION");
			DrawDistributionFloat("Start Rotation", Rot->StartRotation, Rot, 0.0f, 0.05f, -360.0f, 360.0f);
			DrawDistributionFloat("Rotation Rate", Rot->StartRotationRate, Rot, 0.0f, 0.05f, -360.0f, 360.0f);
		}
		else if (auto* RotLife = Cast<UParticleModuleRotationRateOverLife>(SelectedModule))
		{
			DrawSection("ROTATION RATE OVER LIFE");
			DrawDistributionFloat("Rotation Rate Scale", RotLife->RotationRateScale, RotLife, 1.0f, 0.05f, -10.0f, 10.0f);
		}
		else if (auto* Sub = Cast<UParticleModuleSubUV>(SelectedModule))
		{
			DrawSection("SUB UV");
			bool bChanged = false;
			bChanged |= ImGui::DragInt("Horizontal", &Sub->SubImagesHorizontal, 1.0f, 1, 64);
			bChanged |= ImGui::DragInt("Vertical", &Sub->SubImagesVertical, 1.0f, 1, 64);
			bChanged |= ImGui::Checkbox("Linear Over Life", &Sub->bUseLinearOverLife);
			bChanged |= ImGui::DragFloat("Play Rate", &Sub->PlayRate, 0.05f, 0.0f, 20.0f);
			if (!Sub->bUseLinearOverLife)
				DrawDistributionFloat("SubImage Index", Sub->SubImageIndex, Sub, 0.0f, 0.1f, 0.0f, 256.0f);
			if (bChanged) { MarkSelectedLODBuildDirty(); MarkAssetModified(); }
		}
		else if (auto* Collision = Cast<UParticleModuleCollision>(SelectedModule))
		{
			ImGui::DragFloat("Restitution Min", &Collision->RestitutionMin, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat("Restitution Max", &Collision->RestitutionMax, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat("Friction Min", &Collision->FrictionMin, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat("Friction Max", &Collision->FrictionMax, 0.01f, 0.0f, 1.0f);
			ImGui::DragInt("Max Collisions", &Collision->MaxCollisions, 1.0f, 0, 1000);
			ImGui::DragFloat("Collision Radius", &Collision->CollisionRadius, 0.01f, 0.0f, 100.0f);
		}
		else if (auto* EventGen = Cast<UParticleModuleEventGenerator>(SelectedModule))
		{
			char EventNameBuf[128] = {};
			strncpy_s(EventNameBuf, EventGen->EventName.c_str(), _TRUNCATE);
			if (ImGui::InputText("Event Name", EventNameBuf, sizeof(EventNameBuf)))
				EventGen->EventName = EventNameBuf;
			// 0이면 TickCollision의 나머지 연산이 0으로 나누게 되므로 하한을 1로 막는다.
			ImGui::DragInt("Event Frequency", &EventGen->EventFrequency, 1.0f, 1, 1000);
		}
		else if (auto* SrcMod = Cast<UParticleModuleBeamSource>(SelectedModule))
		{
			DrawSection("BEAM SOURCE");
			bool bChanged = ImGui::Combo("Source Method", &SrcMod->SourceMethod, "Emitter\0User Set\0Parameter\0");
			if (SrcMod->SourceMethod == 2)
			{
				char ParamBuf[128] = {};
				strncpy_s(ParamBuf, SrcMod->SourceParameterName.c_str(), _TRUNCATE);
				if (ImGui::InputText("Parameter Name", ParamBuf, sizeof(ParamBuf)))
				{
					SrcMod->SourceParameterName = ParamBuf;
					bChanged = true;
				}
			}
			if (SrcMod->SourceMethod != 0)
				DrawDistributionVector("Source", SrcMod->Source, SrcMod, FVector::ZeroVector, false, 0.05f);
			bChanged |= ImGui::Checkbox("Use Source Tangent", &SrcMod->bUseSourceTangent);
			if (SrcMod->bUseSourceTangent)
			{
				DrawDistributionVector("Source Tangent", SrcMod->SourceTangent, SrcMod, FVector(1.0f, 0.0f, 0.0f), false, 0.05f);
				DrawDistributionFloat("Source Strength", SrcMod->SourceStrength, SrcMod, 1.0f, 0.05f, 0.0f, 100.0f);
			}
			if (bChanged) MarkAssetModified();
		}
		else if (auto* DstMod = Cast<UParticleModuleBeamTarget>(SelectedModule))
		{
			DrawSection("BEAM TARGET");
			bool bChanged = ImGui::Combo("Target Method", &DstMod->TargetMethod, "Emitter\0User Set\0Parameter\0");
			if (DstMod->TargetMethod == 2)
			{
				char ParamBuf[128] = {};
				strncpy_s(ParamBuf, DstMod->TargetParameterName.c_str(), _TRUNCATE);
				if (ImGui::InputText("Parameter Name", ParamBuf, sizeof(ParamBuf)))
				{
					DstMod->TargetParameterName = ParamBuf;
					bChanged = true;
				}
			}
			if (DstMod->TargetMethod != 0)
				DrawDistributionVector("Target", DstMod->Target, DstMod, FVector(3.0f, 0.0f, 0.0f), false, 0.05f);
			bChanged |= ImGui::Checkbox("Use Target Tangent", &DstMod->bUseTargetTangent);
			if (DstMod->bUseTargetTangent)
			{
				DrawDistributionVector("Target Tangent", DstMod->TargetTangent, DstMod, FVector(-1.0f, 0.0f, 0.0f), false, 0.05f);
				DrawDistributionFloat("Target Strength", DstMod->TargetStrength, DstMod, 1.0f, 0.05f, 0.0f, 100.0f);
			}
			if (bChanged) MarkAssetModified();
		}
		else if (auto* NoiseMod = Cast<UParticleModuleBeamNoise>(SelectedModule))
		{
			DrawSection("BEAM NOISE");
			bool bChanged = ImGui::DragInt("Frequency", &NoiseMod->Frequency, 1.0f, 1, 256);
			DrawDistributionVector("Noise Range", NoiseMod->NoiseRange, NoiseMod, FVector(0.25f, 0.25f, 0.25f), false, 0.02f);
			bChanged |= ImGui::DragFloat("Noise Lock Time", &NoiseMod->NoiseLockTime, 0.005f, 0.0f, 5.0f);
			bChanged |= ImGui::Checkbox("Smooth", &NoiseMod->bSmooth);
			if (!NoiseMod->bSmooth)
				bChanged |= ImGui::SliderFloat("Noise Tension", &NoiseMod->NoiseTension, 0.0f, 1.0f);
			bChanged |= ImGui::Checkbox("Lock Endpoints", &NoiseMod->bLockEndpoints);
			if (bChanged) MarkAssetModified();
		}
		else if (auto* UnitMod = Cast<UParticleModuleSpawnPerUnit>(SelectedModule))
		{
			DrawSection("SPAWN PER UNIT");
			bool bChanged = false;
			bChanged |= ImGui::DragFloat("Spawn Per Unit", &UnitMod->SpawnPerUnit, 0.5f, 0.0f, 500.0f);
			bChanged |= ImGui::DragFloat("Movement Tolerance", &UnitMod->MovementTolerance, 0.005f, 0.0f, 10.0f);
			bChanged |= ImGui::DragFloat("Max Frame Distance", &UnitMod->MaxFrameDistance, 0.5f, 0.1f, 1000.0f);
			bChanged |= ImGui::Checkbox("Ignore Spawn Rate When Moving", &UnitMod->bIgnoreSpawnRateWhenMoving);
			if (bChanged) MarkAssetModified();
		}
		ImGui::EndDisabled();
	}
	else if (SelectedEmitter)
	{
		char NameBuf[128] = {};
		strncpy_s(NameBuf, SelectedEmitter->EmitterName.c_str(), _TRUNCATE);
		if (ImGui::InputText("Emitter Name", NameBuf, sizeof(NameBuf)))
		{
			SelectedEmitter->EmitterName = NameBuf;
			MarkAssetModified();
		}

		ImGui::BeginDisabled(true);
		ImGui::Checkbox("Solo (Unsupported)", &SelectedEmitter->bIsSolo);
		ImGui::EndDisabled();

		if (ImGui::DragFloat("Play Rate", &SelectedEmitter->PlayRate, 0.05f, 0.0f, 20.0f))
		{
			MarkAssetModified();
		}
		const TArray<UParticleLODLevel*>& LODLevels = SelectedEmitter->GetLODLevels();

		if (SelectedLODIndex >= 0 && SelectedLODIndex < static_cast<int32>(LODLevels.Num()))
		{

			UParticleLODLevel* LOD = LODLevels[SelectedLODIndex];

			if (auto* MeshType = LOD ? Cast<UParticleModuleTypeDataMesh>(LOD->EffectiveTypeData) : nullptr)
			{
				DrawSection("RENDERER");
				DrawTypeDataMeshPicker(LOD, MeshType);
			}
		}
	}
	else if (EditingSystem)
	{
		DrawSection("SYSTEM");
		DrawFloatProperty("Play Rate", "##SystemPlayRate", &EditingSystem->PlayRate, 0.05f, 0.0f, 20.0f);

		ImGui::TextDisabled("Warmup and inactive timeout settings are not supported by the current runtime.");

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

		if (bBoundsChanged)
		{
			MarkAssetModified();
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
				float Distance = EditingSystem->GetLODDistance(Index);
				ImGui::TextUnformatted(Label);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::BeginDisabled(Index == 0);
				if (ImGui::DragFloat("##LODDistance", &Distance, 0.5f, MinDistance, MaxDistance, "%.3f") &&
					EditingSystem->SetLODDistance(Index, Distance))
					MarkAssetModified();
				ImGui::EndDisabled();
				ImGui::PopID();
			}
		}
	}

	ImGui::EndChild();
}

void FParticleSystemEditorPanel::MarkSelectedLODBuildDirty()
{
	if (!SelectedEmitter) return;
	for (UParticleLODLevel* LOD : SelectedEmitter->GetLODLevels())
		if (LOD) LOD->MarkBuildDirty();
}

// 스칼라 분포 속성을 모드별로 편집하는 공용 위젯
void FParticleSystemEditorPanel::DrawDistributionFloat(
	const char* Label,
	UDistributionFloat*& InOutDist,
	UObject* Outer,
	float DefaultValue,
	float DragSpeed,
	float MinVal,
	float MaxVal)
{
	bool bChanged = false;
	ImGui::PushID(Label);
	ImGui::TextUnformatted(Label);
	int ModeIdx = InOutDist ? InOutDist->GetModeIndex() : 0;
	ImGui::SameLine();
	ImGui::SetNextItemWidth(EditorUIScale::Px(130.0f));
	if (ImGui::Combo("##DistMode", &ModeIdx, "Constant\0Uniform\0Constant Curve\0Uniform Curve\0Parameter\0"))
	{
		InOutDist = ConvertFloatDistributionMode(Outer, InOutDist, ModeIdx);
		bChanged = true;
	}
	if (!InOutDist)
	{
		InOutDist = CreateFloatDistributionByMode(Outer, ModeIdx, DefaultValue);
		bChanged = true;
	}
	if (UDistributionFloatConstant* C = Cast<UDistributionFloatConstant>(InOutDist))
	{
		if (ImGui::DragFloat("Value", &C->Constant, DragSpeed, MinVal, MaxVal))
		{
			C->InvalidateLookupTable();
			bChanged = true;
		}
	}
	else if (UDistributionFloatUniform* U = Cast<UDistributionFloatUniform>(InOutDist))
	{
		if (ImGui::DragFloat("Min", &U->Min, DragSpeed, MinVal, MaxVal))
		{
			U->InvalidateLookupTable();
			bChanged = true;
		}
		if (ImGui::DragFloat("Max", &U->Max, DragSpeed, MinVal, MaxVal))
		{
			U->InvalidateLookupTable();
			bChanged = true;
		}
	}
	else if (Cast<UDistributionFloatConstantCurve>(InOutDist) || Cast<UDistributionFloatUniformCurve>(InOutDist))
	{
		ImGui::TextDisabled("Edit curve in Curve Editor.");
	}
	else if (UDistributionFloatParticleParameter* P = Cast<UDistributionFloatParticleParameter>(InOutDist))
	{
		char ParamBuf[128] = {};
		strncpy_s(ParamBuf, P->ParameterName.c_str(), _TRUNCATE);
		if (ImGui::InputText("Parameter Name", ParamBuf, sizeof(ParamBuf)))
		{
			P->ParameterName = ParamBuf;
			bChanged = true;
		}
		if (ImGui::DragFloat("Default", &P->Constant, DragSpeed, MinVal, MaxVal))
		{
			P->InvalidateLookupTable();
			bChanged = true;
		}
	}
	if (bChanged)
	{
		MarkSelectedLODBuildDirty();
		MarkAssetModified();
	}
	ImGui::Separator();
	ImGui::PopID();
}

// 벡터 또는 색상 분포 속성을 모드별로 편집하는 공용 위젯
void FParticleSystemEditorPanel::DrawDistributionVector(
	const char* Label,
	UDistributionVector*& InOutDist,
	UObject* Outer,
	const FVector& DefaultValue,
	bool bAsColor,
	float DragSpeed)
{
	bool bChanged = false;
	ImGui::PushID(Label);
	ImGui::TextUnformatted(Label);
	int ModeIdx = InOutDist ? InOutDist->GetModeIndex() : 0;
	ImGui::SameLine();
	ImGui::SetNextItemWidth(EditorUIScale::Px(130.0f));
	if (ImGui::Combo("##DistMode", &ModeIdx, "Constant\0Uniform\0Constant Curve\0Uniform Curve\0Parameter\0"))
	{
		InOutDist = ConvertVectorDistributionMode(Outer, InOutDist, ModeIdx);
		bChanged = true;
	}
	if (!InOutDist)
	{
		InOutDist = CreateVectorDistributionByMode(Outer, ModeIdx, DefaultValue);
		bChanged = true;
	}
	if (UDistributionVectorConstant* C = Cast<UDistributionVectorConstant>(InOutDist))
	{
		const bool bEdited = bAsColor
			? ImGui::ColorEdit3("Value", &C->Constant.X)
			: ImGui::DragFloat3("Value", &C->Constant.X, DragSpeed);
		if (bEdited)
		{
			C->InvalidateLookupTable();
			bChanged = true;
		}
	}
	else if (UDistributionVectorUniform* U = Cast<UDistributionVectorUniform>(InOutDist))
	{
		if (ImGui::Checkbox("Lock Axes", &U->bLockAxes))
		{
			if (U->bLockAxes)
			{
				U->Min.Y = U->Min.X;
				U->Min.Z = U->Min.X;
				U->Max.Y = U->Max.X;
				U->Max.Z = U->Max.X;
			}
			U->InvalidateLookupTable();
			bChanged = true;
		}
		if (U->bLockAxes && !bAsColor)
		{
			if (ImGui::DragFloat("Min", &U->Min.X, DragSpeed))
			{
				U->Min.Y = U->Min.X;
				U->Min.Z = U->Min.X;
				U->InvalidateLookupTable();
				bChanged = true;
			}
			if (ImGui::DragFloat("Max", &U->Max.X, DragSpeed))
			{
				U->Max.Y = U->Max.X;
				U->Max.Z = U->Max.X;
				U->InvalidateLookupTable();
				bChanged = true;
			}
		}
		else
		{
			const bool bMinEdited = bAsColor
				? ImGui::ColorEdit3("Min", &U->Min.X)
				: ImGui::DragFloat3("Min", &U->Min.X, DragSpeed);
			if (bMinEdited)
			{
				U->InvalidateLookupTable();
				bChanged = true;
			}
			const bool bMaxEdited = bAsColor
				? ImGui::ColorEdit3("Max", &U->Max.X)
				: ImGui::DragFloat3("Max", &U->Max.X, DragSpeed);
			if (bMaxEdited)
			{
				U->InvalidateLookupTable();
				bChanged = true;
			}
		}
	}
	else if (UDistributionVectorUniformCurve* UC = Cast<UDistributionVectorUniformCurve>(InOutDist))
	{
		if (ImGui::Checkbox("Lock Axes", &UC->bLockAxes))
		{
			UC->InvalidateLookupTable();
			bChanged = true;
		}
		ImGui::TextDisabled("Edit curves in Curve Editor.");
	}
	else if (Cast<UDistributionVectorConstantCurve>(InOutDist))
	{
		ImGui::TextDisabled("Edit curves in Curve Editor.");
	}
	else if (UDistributionVectorParticleParameter* P = Cast<UDistributionVectorParticleParameter>(InOutDist))
	{
		char ParamBuf[128] = {};
		strncpy_s(ParamBuf, P->ParameterName.c_str(), _TRUNCATE);
		if (ImGui::InputText("Parameter Name", ParamBuf, sizeof(ParamBuf)))
		{
			P->ParameterName = ParamBuf;
			bChanged = true;
		}
		const bool bEdited = bAsColor
			? ImGui::ColorEdit3("Default", &P->Constant.X)
			: ImGui::DragFloat3("Default", &P->Constant.X, DragSpeed);
		if (bEdited)
		{
			P->InvalidateLookupTable();
			bChanged = true;
		}
	}
	if (bChanged)
	{
		MarkSelectedLODBuildDirty();
		MarkAssetModified();
	}
	ImGui::Separator();
	ImGui::PopID();
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

			for (UParticleModule* Module : LOD->EffectiveModules)
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

					const UParticleModuleColorOverLife* ColorOverLife = Cast<UParticleModuleColorOverLife>(Module);

					if (ColorOverLife && ColorOverLife->bUseFadeInOut && Track.Name == "Alpha")
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
	ImGui::SameLine();

	if (ImGui::SmallButton("Auto Fit"))
	{
		bCurveFitRequested = true;
	}
	ImGui::SameLine();
	if (ImGui::Checkbox("Show All Curves", &bShowAllCurves))
		bCurveFitRequested = true;

	ImGui::Separator();

	if (Tracks.IsEmpty())
	{
		ImGui::TextDisabled("No curves in the selected LOD.");
		ImGui::EndChild();
		return;
	}

	int32 ResolvedCurveIndex = INDEX_NONE;

	for (int32 Index = 0; Index < static_cast<int32>(Tracks.Num()); ++Index)
	{
		if (Tracks[Index].Track.Curve == SelectedCurve)
		{
			ResolvedCurveIndex = Index;
			break;
		}
	}

	if (ResolvedCurveIndex == INDEX_NONE)
	{
		ResolvedCurveIndex = 0;
		SelectedCurve = Tracks[0].Track.Curve;
	}

	SelectedCurveTrackIndex = ResolvedCurveIndex;

	ImGui::BeginChild("##CurveTrackList", ImVec2(0.0f, std::min(EditorUIScale::Px(140.0f), Height * 0.25f)), ImGuiChildFlags_Borders);
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
			SelectedCurve = Entry.Track.Curve;
			SelectedEmitter = Entry.Emitter;
			SelectedModule = Entry.Module;
			SelectedCurveKeyId = -1;
			bCurveFitRequested = true;
		}

		ImGui::PopID();
	}
	ImGui::EndChild();

	FEditorCurveEntry& ActiveEntry = Tracks[SelectedCurveTrackIndex];
	const UParticleLODLevel* ActiveLOD = GetParticleEditorLOD(ActiveEntry.Emitter, SelectedLODIndex);
	const bool bCanEditCurve = ActiveLOD && ActiveLOD->OwnsModule(ActiveEntry.Module);
	if (!bCanEditCurve) ImGui::TextDisabled("Inherited curve");
	UCurveFloat* ActiveCurve = bCanEditCurve ? ActiveEntry.Track.Curve : nullptr;

	if (ActiveCurve)
	{
		float DefaultValue = ActiveCurve->DefaultValue;

		ImGui::TextUnformatted("Default Value");
		ImGui::SetNextItemWidth(EditorUIScale::Px(120.0f));

		if (ImGui::DragFloat("##CurveDefaultValue", &DefaultValue, 0.01f))
		{
			ActiveCurve->DefaultValue = DefaultValue;
			MarkAssetModified();
		}

		ImGui::SameLine();

		if (ImGui::Button("+ Add Key"))
		{
			SelectedCurveKeyId = ActiveCurve->AddKey(0.5f, ActiveCurve->GetFloatValue(0.5f));
			MarkAssetModified();
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
				MarkAssetModified();
			}

			ImGui::SameLine();
			ImGui::SetNextItemWidth(EditorUIScale::Px(80.0f));

			if (ImGui::DragFloat("Value", &KeyValue, 0.02f))
			{
				ActiveCurve->SetKeyValue(KeyId, KeyValue);
				MarkAssetModified();
			}

			ImGui::SameLine();
			ImGui::SetNextItemWidth(EditorUIScale::Px(100.0f));

			if (ImGui::Combo("##CurveInterpMode", &ModeIndex, "Constant\0Linear\0Cubic\0"))
			{
				ActiveCurve->SetKeyInterpMode(KeyId, static_cast<ECurveInterpMode>(ModeIndex));
				MarkAssetModified();
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
					MarkAssetModified();
				}
			}

			ImGui::SameLine();

			if (ImGui::Button("Remove Key"))
			{
				ActiveCurve->RemoveKey(KeyId);
				SelectedCurveKeyId = -1;
				MarkAssetModified();
			}
		}
	}

	if (ImPlot::BeginPlot("##ParticleCurvePlot", ImVec2(-1.0f, -1.0f), ImPlotFlags_NoLegend))
	{
		ImPlot::SetupAxes("Relative Time", "Value");
		ImPlot::SetupAxisLimits(ImAxis_X1, -0.05, 1.05, ImPlotCond_Once);

		float MinValue = 0.0f;
		float MaxValue = 1.0f;
		bool bHasValue = false;

		for (int32 TrackIndex = 0; TrackIndex < static_cast<int32>(Tracks.Num()); ++TrackIndex)
		{
			if (!bShowAllCurves && TrackIndex != SelectedCurveTrackIndex) continue;
			const FEditorCurveEntry& Entry = Tracks[TrackIndex];
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

		if (bCurveFitRequested)
		{
			ImPlot::SetupAxisLimits(ImAxis_Y1, MinValue - ValueRange * 0.15f, MaxValue + ValueRange * 0.15f, ImPlotCond_Always);
		}
		else
		{
			ImPlot::SetupAxisLimits(ImAxis_Y1, MinValue - ValueRange * 0.15f, MaxValue + ValueRange * 0.15f, ImPlotCond_Once);
		}

		constexpr int SampleCount = 64;
		double XValues[SampleCount];
		double YValues[SampleCount];

		for (int32 TrackIndex = 0; TrackIndex < static_cast<int32>(Tracks.Num()); ++TrackIndex)
		{
			if (!bShowAllCurves && TrackIndex != SelectedCurveTrackIndex) continue;
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

			const FString PlotLabel = Entry.Label + "##Curve_" + std::to_string(TrackIndex);
			ImPlot::PlotLine(PlotLabel.c_str(), XValues, YValues, SampleCount, Spec);
		}

		if (ActiveCurve && std::find(HiddenCurveTracks.begin(), HiddenCurveTracks.end(), ActiveCurve) == HiddenCurveTracks.end())
		{
			const FColor& ActiveColor = ActiveEntry.Track.Color;
			const ImVec4 PointColor(ActiveColor.R / 255.0f, ActiveColor.G / 255.0f, ActiveColor.B / 255.0f, 1.0f);

			const TArray<FFloatCurveKey> Keys = ActiveCurve->GetKeys();
			for (const FFloatCurveKey& Key : Keys)
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
					MarkAssetModified();
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
				MarkAssetModified();
			}
		}

		ImPlot::EndPlot();
		bCurveFitRequested = false;
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
	bFocused = bVisible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	if (!bVisible)
	{
		ImGui::End();
		return;
	}

	const ImGuiIO& Io = ImGui::GetIO();

	if (bFocused && EditingSystem && !Io.WantTextInput && Io.KeyCtrl && !Io.KeyShift && !Io.KeyAlt)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_S, false))
		{
			SaveAsset();
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
		{
			Undo();
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
		{
			Redo();
		}
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

	if (!ImGui::IsAnyItemActive())
	{
		bEditTransactionActive = false;
	}

	ImGui::End();
}

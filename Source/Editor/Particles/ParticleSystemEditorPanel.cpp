#include "EnginePCH.h"
#include "Editor/Particles/ParticleSystemEditorPanel.h"

#include "Editor/HitoriEd/EditorEngine.h"
#include "Editor/Details/PropertyEditor.h"

#include "Asset/AssetManager.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleModule.h"
#include "Components/ParticleSystemComponent.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"

#include "Render/Material.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

#include <algorithm>

void FParticleSystemEditorPanel::ReleasePreview()
{
	Client.SetWorld(nullptr);
	PreviewComponent = nullptr;

	if (!PreviewWorld)
	{
		return;
	}

	if (GEngine)
	{
		GEngine->DestroyWorld(PreviewWorld);
	}
	else
	{
		PreviewWorld->ClearWorld();
		PreviewWorld->FlushPendingDestruction();
	}

	PreviewWorld = nullptr;
}

void FParticleSystemEditorPanel::Release()
{
	ReleasePreview();

	ParticleSystem = nullptr;
	SelectedObject = nullptr;
	SelectedEmitter = nullptr;
	SelectedModule = nullptr;

	Path.clear();
	Status.clear();

	bDirty = false;
	bVisible = false;
	bFocused = false;
}

void FParticleSystemEditorPanel::SelectObject(UObject* Object, UParticleEmitter* Emitter)
{
	SelectedObject = Object;
	SelectedEmitter = Emitter;
	SelectedModule = Cast<UParticleModule>(Object);
}

void FParticleSystemEditorPanel::NewAsset(const FString& InPath)
{
	Release();

	Path = InPath;
	ParticleSystem = NewObject<UParticleSystem>();

	if (!ParticleSystem)
	{
		Status = "Failed to create Particle System.";
		SetOpen(true);
		return;
	}

	ParticleSystem->CreateDefaultSetup();

	SelectObject(ParticleSystem, nullptr);

	bPlaying = true;
	bDirty = true;

	StartPreview();
	SetOpen(true);
	Save();
}

void FParticleSystemEditorPanel::OpenAsset(const FString& InPath)
{
	if (IsOpen() && Path == InPath)
	{
		SetOpen(true);
		return;
	}

	Release();

	Path = InPath;
	ParticleSystem = UAssetManager::LoadParticleSystem(Path);

	if (!ParticleSystem)
	{
		Status = "Failed to load Particle System.";
		SetOpen(true);
		return;
	}

	SelectObject(ParticleSystem, nullptr);

	bPlaying = true;
	bDirty = false;
	Status.clear();

	StartPreview();
	SetOpen(true);
}

void FParticleSystemEditorPanel::StartPreview()
{
	ReleasePreview();

	if (!ParticleSystem)
	{
		return;
	}

	PreviewWorld = UWorld::CreateWorld(EWorldType::Editor, false);

	if (!PreviewWorld)
	{
		Status = "Failed to create Preview World.";
		return;
	}

	AActor* PreviewActor = PreviewWorld->SpawnActor<AActor>();

	if (!PreviewActor)
	{
		ReleasePreview();
		Status = "Failed to create Preview Actor.";
		return;
	}

	PreviewComponent = Cast<UParticleSystemComponent>(
		PreviewActor->AddComponent(UParticleSystemComponent::StaticClass(), FName("ParticlePreview")));

	if (!PreviewComponent || !PreviewActor->SetRootComponent(PreviewComponent))
	{
		ReleasePreview();
		Status = "Failed to create Particle System Component.";
		return;
	}

	PreviewComponent->SetTemplate(ParticleSystem);

	Client.SetWorld(PreviewWorld);
	Client.SetViewportType(ELevelViewportType::Perspective);
	Client.Resize(800, 600);
	Client.SetActive(true);

	Client.SetViewRotation(FRotator(10.0f, 165.0f, 0.0f));
	Client.SetViewLocation(-Client.GetViewForward() * 450.0f);
}

void FParticleSystemEditorPanel::ResetPreview()
{
	if (PreviewComponent)
	{
		PreviewComponent->ResetSystem();
	}
}

void FParticleSystemEditorPanel::Tick(float DeltaTime)
{
	if (!IsOpen())
	{
		bFocused = false;
		bVisible = false;
		return;
	}

	if (!bVisible || !PreviewWorld || !bPlaying)
	{
		return;
	}

	PreviewWorld->Tick(std::max(0.0f, DeltaTime) * SimulationSpeed);
}

void FParticleSystemEditorPanel::RenderPreview(UEditorEngine* Engine)
{
	if (Engine && IsOpen() && bVisible && PreviewWorld)
	{
		Engine->RenderPreviewViewport(&Client, PreviewWorld, nullptr, nullptr, true);
	}
}

void FParticleSystemEditorPanel::Save()
{
	if (!ParticleSystem || Path.empty())
	{
		Status = "No Particle System is open.";
		return;
	}

	if (std::filesystem::path(Path).extension() != ".particlesystem")
	{
		Status = "Invalid Particle System extension.";
		return;
	}

	if (!UAssetManager::SaveParticleSystem(Path, *ParticleSystem))
	{
		Status = "Failed to save Particle System.";
		return;
	}

	bDirty = false;
	Status = "Saved";
}

void FParticleSystemEditorPanel::AddModule(UClass* ModuleClass, const FName& ModuleName)
{
	if (!SelectedEmitter || !ModuleClass)
	{
		return;
	}

	UParticleLODLevel* LODLevel = SelectedEmitter->GetLODLevel(0);

	if (!LODLevel)
	{
		return;
	}

	UParticleModule* Module = LODLevel->AddModule(ModuleClass, ModuleName);

	if (Module)
	{
		SelectObject(Module, SelectedEmitter);
		bDirty = true;
	}
}

void FParticleSystemEditorPanel::DrawToolbar()
{
	if (!ParticleSystem)
	{
		return;
	}

	if (ImGui::Button("Save"))
	{
		Save();
	}

	ImGui::SameLine();

	if (ImGui::Button(bPlaying ? "Pause" : "Play"))
	{
		bPlaying = !bPlaying;
	}

	ImGui::SameLine();

	if (ImGui::Button("Reset"))
	{
		ResetPreview();
	}

	ImGui::SameLine();

	ImGui::SetNextItemWidth(120.0f);
	ImGui::DragFloat("Sim Speed", &SimulationSpeed, 0.05f, 0.0f, 4.0f, "%.2fx");

	ImGui::SameLine();

	if (ImGui::Button("Add Emitter"))
	{
		const FName Name = MakeUniqueObjectName(ParticleSystem, UParticleEmitter::StaticClass(), FName("Emitter"));
		SelectedEmitter = ParticleSystem->CreateEmitter(Name);

		if (SelectedEmitter)
		{
			SelectObject(SelectedEmitter, SelectedEmitter);
			bDirty = true;
			ResetPreview();
		}
	}

	ImGui::SameLine();

	ImGui::BeginDisabled(!SelectedEmitter || SelectedObject != SelectedEmitter);

	if (ImGui::Button("Remove Emitter"))
	{
		const TArray<UParticleEmitter*>& Emitters = ParticleSystem->GetEmitters();

		for (int32 Index = 0; Index < Emitters.Num(); ++Index)
		{
			if (Emitters[Index] == SelectedEmitter)
			{
				ParticleSystem->RemoveEmitter(Index);
				break;
			}
		}

		SelectedEmitter = ParticleSystem->GetEmitter(0);
		SelectObject(SelectedEmitter ? static_cast<UObject*>(SelectedEmitter) : static_cast<UObject*>(ParticleSystem), SelectedEmitter);

		bDirty = true;
		ResetPreview();
	}

	ImGui::EndDisabled();

	ImGui::SameLine();

	ImGui::BeginDisabled(!SelectedEmitter);

	if (ImGui::Button("Add Module"))
	{
		ImGui::OpenPopup("AddParticleModule");
	}

	if (ImGui::BeginPopup("AddParticleModule"))
	{
		UParticleLODLevel* LODLevel = SelectedEmitter->GetLODLevel(0);

		const auto HasModule = [LODLevel](UClass* Class)
		{
			if (!LODLevel)
			{
				return false;
			}

			for (UParticleModule* Module : LODLevel->GetModules())
			{
				if (Module && Module->IsA(Class))
				{
					return true;
				}
			}

			return false;
		};

		if (!HasModule(UParticleModuleLifetime::StaticClass()) && ImGui::MenuItem("Lifetime"))
		{
			AddModule(UParticleModuleLifetime::StaticClass(), FName("Lifetime"));
		}

		if (!HasModule(UParticleModuleLocation::StaticClass()) && ImGui::MenuItem("Location"))
		{
			AddModule(UParticleModuleLocation::StaticClass(), FName("Location"));
		}

		if (!HasModule(UParticleModuleVelocity::StaticClass()) && ImGui::MenuItem("Velocity"))
		{
			AddModule(UParticleModuleVelocity::StaticClass(), FName("Velocity"));
		}

		if (!HasModule(UParticleModuleColor::StaticClass()) && ImGui::MenuItem("Color"))
		{
			AddModule(UParticleModuleColor::StaticClass(), FName("Color"));
		}

		if (!HasModule(UParticleModuleSize::StaticClass()) && ImGui::MenuItem("Size"))
		{
			AddModule(UParticleModuleSize::StaticClass(), FName("Size"));
		}

		ImGui::EndPopup();
	}

	ImGui::EndDisabled();

	ImGui::SameLine();

	bool bCanRemoveModule = false;

	if (SelectedEmitter && SelectedModule)
	{
		if (UParticleLODLevel* LODLevel = SelectedEmitter->GetLODLevel(0))
		{
			for (UParticleModule* Module : LODLevel->GetModules())
			{
				if (Module == SelectedModule)
				{
					bCanRemoveModule = true;
					break;
				}
			}
		}
	}

	ImGui::BeginDisabled(!bCanRemoveModule);

	if (ImGui::Button("Remove Module"))
	{
		UParticleLODLevel* LODLevel = SelectedEmitter->GetLODLevel(0);

		if (LODLevel && LODLevel->RemoveModule(SelectedModule))
		{
			SelectObject(SelectedEmitter, SelectedEmitter);
			bDirty = true;
			ResetPreview();
		}
	}

	ImGui::EndDisabled();

	if (SelectedEmitter)
	{
		ImGui::SameLine();

		bool bMeshEmitter = SelectedEmitter->GetMeshTypeDataModule() != nullptr;

		if (ImGui::Checkbox("Mesh Emitter", &bMeshEmitter))
		{
			SelectedEmitter->SetMeshEmitterEnabled(bMeshEmitter);

			if (bMeshEmitter)
			{
				SelectObject(SelectedEmitter->GetMeshTypeDataModule(), SelectedEmitter);
			}
			else
			{
				SelectObject(SelectedEmitter, SelectedEmitter);
			}

			bDirty = true;
			ResetPreview();
		}
	}
}

void FParticleSystemEditorPanel::DrawEmitterPanel()
{
	ImGui::BeginChild("ParticleEmitterList", ImVec2(0, 0), true);

	if (ImGui::Selectable("Particle System", SelectedObject == ParticleSystem))
	{
		SelectObject(ParticleSystem, nullptr);
	}

	if (!ParticleSystem)
	{
		ImGui::EndChild();
		return;
	}

	const TArray<UParticleEmitter*>& Emitters = ParticleSystem->GetEmitters();

	for (UParticleEmitter* Emitter : Emitters)
	{
		if (!Emitter)
		{
			continue;
		}

		ImGui::PushID(Emitter);

		const bool bOpen = ImGui::TreeNodeEx("Emitter", ImGuiTreeNodeFlags_DefaultOpen, "%s", Emitter->GetName().c_str());

		if (ImGui::IsItemClicked())
		{
			SelectObject(Emitter, Emitter);
		}

		if (bOpen)
		{
			UParticleLODLevel* LODLevel = Emitter->GetLODLevel(0);

			if (LODLevel)
			{
				if (ImGui::Selectable("Required", SelectedObject == LODLevel->GetRequiredModule()))
				{
					SelectObject(LODLevel->GetRequiredModule(), Emitter);
				}

				if (ImGui::Selectable("Spawn", SelectedObject == LODLevel->GetSpawnModule()))
				{
					SelectObject(LODLevel->GetSpawnModule(), Emitter);
				}

				if (Emitter->GetMeshTypeDataModule())
				{
					if (ImGui::Selectable("Mesh Type Data", SelectedObject == Emitter->GetMeshTypeDataModule()))
					{
						SelectObject(Emitter->GetMeshTypeDataModule(), Emitter);
					}
				}

				for (UParticleModule* Module : LODLevel->GetModules())
				{
					if (!Module)
					{
						continue;
					}

					ImGui::PushID(Module);

					const FString Label = Module->GetClass()->Name;

					if (ImGui::Selectable(Label.c_str(), SelectedObject == Module))
					{
						SelectObject(Module, Emitter);
					}

					ImGui::PopID();
				}
			}

			ImGui::TreePop();
		}

		ImGui::PopID();
	}

	ImGui::EndChild();
}

void FParticleSystemEditorPanel::DrawPreviewViewport()
{
	ImGui::BeginChild("ParticlePreviewViewport", ImVec2(0, 0), true);

	if (!PreviewWorld || !PreviewComponent)
	{
		ImGui::TextUnformatted("Preview is not available.");
		ImGui::EndChild();
		return;
	}

	ImVec2 Size = ImGui::GetContentRegionAvail();
	Size.x = std::max(Size.x, 1.0f);
	Size.y = std::max(Size.y, 1.0f);

	Client.Resize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));

	if (Client.GetColorTarget())
	{
		ImGui::Image(Client.GetColorTarget()->GetSRV(), Size);

		const bool bHovered = ImGui::IsItemHovered();
		const bool bCaptured = bHovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);

		float Wheel = ImGui::GetIO().MouseWheel;

		if (bCaptured && Wheel != 0.0f)
		{
			CameraSpeed = std::clamp(CameraSpeed + Wheel * 0.5f, 0.1f, 20.0f);
			Wheel = 0.0f;
		}

		Client.TickInputFromImGui(ImGui::GetIO().DeltaTime, bCaptured, bHovered, static_cast<int32>(Wheel * 120), 4.0f * CameraSpeed, 0.2f);
	}
	else
	{
		ImGui::TextUnformatted("Waiting for preview render target...");
	}

	ImGui::EndChild();
}

void FParticleSystemEditorPanel::DrawDetailsPanel()
{
	ImGui::BeginChild("ParticleDetails", ImVec2(0, 0), true);

	if (!SelectedObject)
	{
		ImGui::TextDisabled("Select an Emitter or Module.");
		ImGui::EndChild();
		return;
	}

	if (SelectedObject == ParticleSystem)
	{
		ImGui::TextUnformatted("Particle System");
		ImGui::Separator();
		ImGui::TextWrapped("Path: %s", Path.c_str());
		ImGui::Text("Emitters: %d", ParticleSystem ? ParticleSystem->GetEmitters().Num() : 0);
	}
	else if (UParticleModuleRequired* Required = Cast<UParticleModuleRequired>(SelectedObject))
	{
		ImGui::TextUnformatted("Required Module");
		ImGui::Separator();

		const FString CurrentPath = Required->Material ? Required->Material->GetPath() : FString("None");

		if (ImGui::BeginCombo("Material", CurrentPath.c_str()))
		{
			if (ImGui::Selectable("None", Required->Material == nullptr))
			{
				Required->Material = nullptr;
				bDirty = true;
				ResetPreview();
			}

			for (TObjectIterator<UMaterial> It; It; ++It)
			{
				UMaterial* Material = *It;

				if (!Material || Material->IsRetired() || Material->GetPath().empty())
				{
					continue;
				}

				if (Material->PSOType != EPSOType::Particle_Sprite)
				{
					continue;
				}

				const FString MaterialPath = Material->GetPath();

				if (ImGui::Selectable(MaterialPath.c_str(), Required->Material == Material))
				{
					Required->Material = Material;
					bDirty = true;
					ResetPreview();
				}
			}

			ImGui::EndCombo();
		}
	}
	else
	{
		ImGui::TextUnformatted(SelectedObject->GetClass()->Name.c_str());
		ImGui::Separator();

		if (FPropertyEditor::DrawObjectProperties(SelectedObject))
		{
			bDirty = true;

			if (SelectedObject->IsA<UParticleEmitter>())
			{
				ResetPreview();
			}
		}
	}

	ImGui::EndChild();
}

void FParticleSystemEditorPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(1250.0f, 750.0f), ImGuiCond_FirstUseEver);

	bVisible = BeginPanel();
	bFocused = bVisible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	if (bFocused && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_S, false))
	{
		Save();
	}

	if (!bVisible)
	{
		ImGui::End();
		return;
	}

	ImGui::TextUnformatted(Path.c_str());

	ImGui::SameLine();

	if (bDirty)
	{
		ImGui::TextUnformatted("*");
	}

	DrawToolbar();

	if (ImGui::BeginTable("ParticleSystemEditorLayout", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
	{
		ImGui::TableSetupColumn("Emitters", ImGuiTableColumnFlags_WidthFixed, 220.0f);
		ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 320.0f);

		ImGui::TableNextColumn();
		DrawEmitterPanel();

		ImGui::TableNextColumn();
		DrawPreviewViewport();

		ImGui::TableNextColumn();
		DrawDetailsPanel();

		ImGui::EndTable();
	}

	if (!Status.empty())
	{
		ImGui::TextWrapped("%s", Status.c_str());
	}

	ImGui::End();
}
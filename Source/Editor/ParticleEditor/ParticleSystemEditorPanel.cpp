#include "EnginePCH.h"
#include "Editor/ParticleEditor/ParticleSystemEditorPanel.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Editor/EditorUI/EditorSort.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
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
#include "Engine/World.h"
#include <implot.h>
#include <algorithm>
#include <filesystem>

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
	if (!EditingSystem) return;
	UParticleEmitter* Emitter = EditingSystem->AddEmitter();
	if (!Emitter) return;
	UParticleLODLevel* LOD = Emitter->AddLODLevel();
	if (!LOD) return;
	LOD->AddModule(UParticleModuleLifetime::StaticClass());
	LOD->AddModule(UParticleModuleSize::StaticClass());
	LOD->AddModule(UParticleModuleVelocity::StaticClass());
	LOD->AddModule(UParticleModuleColor::StaticClass());
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
		Engine->RenderPreviewViewport(&Client, PreviewWorld, nullptr,
			bShowSkybox ? Skybox.get() : nullptr, bShowGrid);
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
				Emitter->AddLODLevel();
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

	Client.Resize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));
	if (Client.GetColorTarget())
		ImGui::Image(Client.GetColorTarget()->GetSRV(), Size);

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
			char Buf[64];
			snprintf(Buf, sizeof(Buf), "%d / %d", Instances[Index].ActiveParticles, Instances[Index].MaxActiveParticles);
			const ImVec2 TextSize = ImGui::CalcTextSize(Buf);
			DrawList->AddText(ImVec2(ImagePos.x + Size.x - TextSize.x - 10.0f, Y), IM_COL32(255, 160, 120, 255), Buf);
			Y += LineH;
		}
	}

	ImGui::EndChild();
}

void FParticleSystemEditorPanel::RenderEmittersPanel(float Width, float Height)
{
	if (!ImGui::BeginChild("ParticleEmitters", ImVec2(Width, Height), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
	{
		ImGui::EndChild();
		return;
	}

	ImGui::TextUnformatted("Emitters");
	ImGui::SameLine();
	if (ImGui::SmallButton("+ Emitter") && EditingSystem)
	{
		if (UParticleEmitter* NewEmitter = EditingSystem->AddEmitter())
		{
			NewEmitter->AddLODLevel();
			SelectedEmitter = NewEmitter;
			SelectedModule = nullptr;
			if (PreviewActor && PreviewActor->GetParticleSystemComponent())
				PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
		}
	}
	ImGui::Separator();

	if (!EditingSystem)
	{
		ImGui::EndChild();
		return;
	}

	const TArray<UParticleEmitter*>& Emitters = EditingSystem->GetEmitters();
	const float ColumnWidth = EditorUIScale::Px(190.0f);
	const float ColumnHeight = std::max(EditorUIScale::Px(80.0f), ImGui::GetContentRegionAvail().y);

	UParticleEmitter* EmitterToRemove = nullptr;
	for (int32 EmitterIdx = 0; EmitterIdx < static_cast<int32>(Emitters.Num()); ++EmitterIdx)
	{
		UParticleEmitter* Emitter = Emitters[EmitterIdx];
		if (!Emitter) continue;

		if (EmitterIdx > 0) ImGui::SameLine();
		ImGui::PushID(Emitter);

		const bool bEmitterSelected = (SelectedEmitter == Emitter && SelectedModule == nullptr);
		if (ImGui::BeginChild("EmitterCol", ImVec2(ColumnWidth, ColumnHeight), ImGuiChildFlags_Borders))
		{
			UParticleLODLevel* LOD = !Emitter->GetLODLevels().IsEmpty() ? Emitter->GetLODLevels()[0] : nullptr;
			if (ImGui::Selectable(Emitter->EmitterName.c_str(), bEmitterSelected, 0, ImVec2(ColumnWidth - EditorUIScale::Px(44.0f), 0.0f)))
			{
				SelectedEmitter = Emitter;
				SelectedModule = nullptr;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("X"))
				EmitterToRemove = Emitter;

			if (LOD)
			{
				ImGui::Checkbox("Enabled", &LOD->bEnabled);
				ImGui::SameLine();
				ImGui::Checkbox("S", &Emitter->bIsSolo);
				UParticleModuleTypeDataBase* TypeData = LOD->GetOrCreateTypeData();
				int TypeIdx = (TypeData && TypeData->GetEmitterType() == EParticleEmitterType::GpuSprite) ? 1 : 0;
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::Combo("##EmitterType", &TypeIdx, "Sprite\0GPU Sprite\0"))
				{
					UClass* TargetClass = (TypeIdx == 1)
						? UParticleModuleTypeDataGpuSprite::StaticClass()
						: UParticleModuleTypeDataSprite::StaticClass();
					LOD->SetTypeData(TargetClass);
					if (PreviewActor && PreviewActor->GetParticleSystemComponent())
						PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
				}
				ImGui::Separator();

				auto DrawModuleRow = [&](UParticleModule* Mod, const ImVec4& TintColor, bool bRemovable)
				{
					if (!Mod) return false;
					bool bDeleteRequested = false;
					ImGui::PushID(Mod);
					ImGui::PushStyleColor(ImGuiCol_Header, TintColor);
					const bool bSelected = (SelectedModule == Mod);
					if (ImGui::Checkbox("##En", &Mod->bEnabled))
					{
						if (PreviewActor && PreviewActor->GetParticleSystemComponent())
							PreviewActor->GetParticleSystemComponent()->UpdateEmitterStrides();
					}
					ImGui::SameLine();
					if (ImGui::Selectable(Mod->GetDisplayName(), bSelected, 0, ImVec2(ColumnWidth - EditorUIScale::Px(78.0f), 0.0f)))
					{
						SelectedEmitter = Emitter;
						SelectedModule = Mod;
					}
					if (bRemovable)
					{
						ImGui::SameLine();
						if (ImGui::SmallButton("-"))
							bDeleteRequested = true;
					}
					ImGui::PopStyleColor();
					ImGui::PopID();
					return bDeleteRequested;
				};

				DrawModuleRow(LOD->GetOrCreateRequired(), ImVec4(0.55f, 0.50f, 0.20f, 0.65f), false);
				DrawModuleRow(LOD->GetOrCreateSpawn(), ImVec4(0.60f, 0.25f, 0.25f, 0.65f), false);

				UParticleModule* ModuleToRemove = nullptr;
				for (UParticleModule* Mod : LOD->Modules)
				{
					if (DrawModuleRow(Mod, ImVec4(0.22f, 0.35f, 0.50f, 0.65f), true))
						ModuleToRemove = Mod;
				}
				if (ModuleToRemove)
				{
					if (SelectedModule == ModuleToRemove) SelectedModule = nullptr;
					LOD->RemoveModule(ModuleToRemove);
					if (PreviewActor && PreviewActor->GetParticleSystemComponent())
						PreviewActor->GetParticleSystemComponent()->UpdateEmitterStrides();
				}

				if (ImGui::Button("+ Add Module", ImVec2(-1.0f, 0.0f)))
					ImGui::OpenPopup("AddModulePopup");

				if (ImGui::BeginPopup("AddModulePopup"))
				{
					auto AddModItem = [&](const char* Label, UClass* ModClass)
					{
						if (ImGui::MenuItem(Label))
						{
							if (UParticleModule* Added = LOD->AddModule(ModClass))
							{
								SelectedEmitter = Emitter;
								SelectedModule = Added;
								if (PreviewActor && PreviewActor->GetParticleSystemComponent())
									PreviewActor->GetParticleSystemComponent()->UpdateEmitterStrides();
							}
						}
					};
					AddModItem("Lifetime", UParticleModuleLifetime::StaticClass());
					AddModItem("Initial Location", UParticleModuleLocation::StaticClass());
					AddModItem("Initial Velocity", UParticleModuleVelocity::StaticClass());
					AddModItem("Initial Color", UParticleModuleColor::StaticClass());
					AddModItem("Color Over Life", UParticleModuleColorOverLife::StaticClass());
					AddModItem("Initial Size", UParticleModuleSize::StaticClass());
					AddModItem("Size By Life", UParticleModuleSizeByLife::StaticClass());
					AddModItem("Initial Rotation", UParticleModuleRotation::StaticClass());
					AddModItem("SubImage Index", UParticleModuleSubUV::StaticClass());
					ImGui::EndPopup();
				}
			}
		}
		ImGui::EndChild();
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
			PreviewActor->GetParticleSystemComponent()->SetTemplate(EditingSystem);
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

	if (SelectedModule)
	{
		ImGui::Text("Module: %s", SelectedModule->GetDisplayName());
		if (ImGui::Checkbox("Enabled", &SelectedModule->bEnabled))
		{
			if (PreviewActor && PreviewActor->GetParticleSystemComponent())
				PreviewActor->GetParticleSystemComponent()->UpdateEmitterStrides();
		}
		ImGui::Separator();

		if (auto* Req = Cast<UParticleModuleRequired>(SelectedModule))
		{
			ImGui::DragInt("Max Particles", &Req->MaxParticles, 1.0f, 1, 100000);
			ImGui::DragFloat("Emitter Duration", &Req->EmitterDuration, 0.05f, 0.0f, 300.0f);
			ImGui::Checkbox("Looping", &Req->bLooping);
			ImGui::Checkbox("Use Local Space", &Req->bUseLocalSpace);

			if (FContentDrawerPanel::DrawMaterialPicker("Material", Req->Material, "(Default)"))
			{
				if (PreviewActor && PreviewActor->GetParticleSystemComponent())
					PreviewActor->GetParticleSystemComponent()->MarkRenderStateDirty();
			}
		}
		else if (auto* Spawn = Cast<UParticleModuleSpawn>(SelectedModule))
		{
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
			ImGui::DragFloat("Min", &Life->Min, 0.05f, 0.01f, 60.0f);
			ImGui::DragFloat("Max", &Life->Max, 0.05f, 0.01f, 60.0f);
		}
		else if (auto* Loc = Cast<UParticleModuleLocation>(SelectedModule))
		{
			char ParamBuf[128] = {};
			strncpy_s(ParamBuf, Loc->ParameterName.c_str(), _TRUNCATE);
			if (ImGui::InputText("Parameter Name", ParamBuf, sizeof(ParamBuf)))
				Loc->ParameterName = ParamBuf;
			ImGui::DragFloat3("Min", &Loc->Min.X, 0.05f);
			ImGui::DragFloat3("Max", &Loc->Max.X, 0.05f);
		}
		else if (auto* Vel = Cast<UParticleModuleVelocity>(SelectedModule))
		{
			char ParamBuf[128] = {};
			strncpy_s(ParamBuf, Vel->ParameterName.c_str(), _TRUNCATE);
			if (ImGui::InputText("Parameter Name", ParamBuf, sizeof(ParamBuf)))
				Vel->ParameterName = ParamBuf;
			ImGui::DragFloat3("Min", &Vel->Min.X, 0.05f);
			ImGui::DragFloat3("Max", &Vel->Max.X, 0.05f);
		}
		else if (auto* Col = Cast<UParticleModuleColor>(SelectedModule))
		{
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
			char ParamBuf[128] = {};
			strncpy_s(ParamBuf, ColOverLife->ParameterName.c_str(), _TRUNCATE);
			if (ImGui::InputText("Parameter Name", ParamBuf, sizeof(ParamBuf)))
				ColOverLife->ParameterName = ParamBuf;
		}
		else if (auto* Sz = Cast<UParticleModuleSize>(SelectedModule))
		{
			ImGui::DragFloat3("Start Size", &Sz->Start.X, 0.05f, 0.0f, 100.0f);
			ImGui::DragFloat3("End Size", &Sz->End.X, 0.05f, 0.0f, 100.0f);
		}
		else if (auto* Rot = Cast<UParticleModuleRotation>(SelectedModule))
		{
			ImGui::DragFloat("Min", &Rot->Min, 0.05f);
			ImGui::DragFloat("Max", &Rot->Max, 0.05f);
			ImGui::DragFloat("Rate Min", &Rot->RateMin, 0.05f);
			ImGui::DragFloat("Rate Max", &Rot->RateMax, 0.05f);
		}
		else if (auto* Sub = Cast<UParticleModuleSubUV>(SelectedModule))
		{
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
	}
	else if (EditingSystem)
	{
		ImGui::DragFloat("Update Time FPS", &EditingSystem->UpdateTimeFPS, 1.0f, 1.0f, 240.0f);
		ImGui::DragFloat("Warmup Time", &EditingSystem->WarmupTime, 0.05f, 0.0f, 60.0f);
		ImGui::DragFloat("Warmup Tick Rate", &EditingSystem->WarmupTickRate, 0.01f, 0.0f, 1.0f);
		ImGui::DragFloat("Seconds Before Inactive", &EditingSystem->SecondsBeforeInactive, 0.1f, 0.0f, 60.0f);
		ImGui::DragFloat("Play Rate", &EditingSystem->PlayRate, 0.05f, 0.0f, 20.0f);
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
				float KeyTime = SelectedKey->Time;
				float KeyValue = SelectedKey->Value;
				int ModeIdx = static_cast<int>(SelectedKey->InterpMode);
				float ArriveT = SelectedKey->ArriveTangent;
				float LeaveT = SelectedKey->LeaveTangent;

				ImGui::SameLine();
				ImGui::SetNextItemWidth(EditorUIScale::Px(68.0f));
				if (ImGui::DragFloat("T", &KeyTime, 0.01f, 0.0f, 1.0f))
					ActiveCurve->SetKeyTime(SelectedKey->KeyId, KeyTime);

				ImGui::SameLine();
				ImGui::SetNextItemWidth(EditorUIScale::Px(68.0f));
				if (ImGui::DragFloat("V", &KeyValue, 0.02f))
					ActiveCurve->SetKeyValue(SelectedKey->KeyId, KeyValue);

				ImGui::SameLine();
				ImGui::SetNextItemWidth(EditorUIScale::Px(82.0f));
				if (ImGui::Combo("##Mode", &ModeIdx, "Constant\0Linear\0Cubic\0"))
					ActiveCurve->SetKeyInterpMode(SelectedKey->KeyId, static_cast<ECurveInterpMode>(ModeIdx));

				if (SelectedKey->InterpMode == ECurveInterpMode::Cubic)
				{
					ImGui::SameLine();
					ImGui::SetNextItemWidth(EditorUIScale::Px(56.0f));
					bool bTanChanged = ImGui::DragFloat("In", &ArriveT, 0.05f);
					ImGui::SameLine();
					ImGui::SetNextItemWidth(EditorUIScale::Px(56.0f));
					bTanChanged |= ImGui::DragFloat("Out", &LeaveT, 0.05f);
					if (bTanChanged)
						ActiveCurve->SetKeyTangents(SelectedKey->KeyId, ArriveT, LeaveT);
				}

				ImGui::SameLine();
				if (ImGui::SmallButton("Remove"))
				{
					ActiveCurve->RemoveKey(SelectedKey->KeyId);
					SelectedCurveKeyId = -1;
				}
			}
		}

		if (ImPlot::BeginPlot("##ParticleCurvePlot", ImVec2(-1.0f, -1.0f)))
		{
			ImPlot::SetupAxes("Time", "Value");
			ImPlot::SetupAxisLimits(ImAxis_X1, -0.05, 1.05, ImPlotCond_Once);
			ImPlot::SetupAxisLimits(ImAxis_Y1, -0.1, 1.5, ImPlotCond_Once);

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
	bVisible = BeginPanel(Title.c_str(), ImGuiWindowFlags_MenuBar);
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

	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	const float SplitterThickness = EditorUIScale::Px(6.0f);
	const float UsableHeight = std::max(EditorUIScale::Px(260.0f), Avail.y - SplitterThickness);
	const float LeftWidth = std::max(EditorUIScale::Px(240.0f), Avail.x * 0.36f);
	const float MinPaneHeight = EditorUIScale::Px(120.0f);
	const float TopHeight = std::clamp(UsableHeight * TopSplitRatio, MinPaneHeight, UsableHeight - MinPaneHeight);
	const float BottomHeight = std::max(MinPaneHeight, UsableHeight - TopHeight);

	if (ImGui::BeginTable("ParticleEditorTop", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
	{
		ImGui::TableSetupColumn("Left", ImGuiTableColumnFlags_WidthFixed, LeftWidth);
		ImGui::TableSetupColumn("Right", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		if (bShowViewport) RenderViewportPanel(0.0f, TopHeight);

		ImGui::TableSetColumnIndex(1);
		if (bShowEmitters) RenderEmittersPanel(0.0f, TopHeight);

		ImGui::EndTable();
	}

	ImGui::InvisibleButton("##HSplitter", ImVec2(-1.0f, SplitterThickness));
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		TopSplitRatio = std::clamp(TopSplitRatio + ImGui::GetIO().MouseDelta.y / UsableHeight, 0.2f, 0.8f);
	}

	if (ImGui::BeginTable("ParticleEditorBottom", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
	{
		ImGui::TableSetupColumn("Left", ImGuiTableColumnFlags_WidthFixed, LeftWidth);
		ImGui::TableSetupColumn("Right", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		if (bShowDetails) RenderDetailsPanel(0.0f, BottomHeight);

		ImGui::TableSetColumnIndex(1);
		if (bShowCurveEditor) RenderCurveEditorPanel(0.0f, BottomHeight);

		ImGui::EndTable();
	}

	ImGui::End();
}

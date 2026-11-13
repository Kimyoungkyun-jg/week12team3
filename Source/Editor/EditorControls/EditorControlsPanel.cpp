#include "EnginePCH.h"

#include "Editor/EditorControls/EditorControlsPanel.h"

#include "Engine/World.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"

#include "Input/InputSystem.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Editor/LevelEditor/LevelEditorToolbar.h"

#include <algorithm>
#include <cstring>

#include "GameFramework/Actor/ParticleActor.h"
#include "GameFramework/Actor/ParticleSystemActor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "GameFramework/Actor/PointLightActor.h"
#include "GameFramework/Actor/SpotLightActor.h"
#include "GameFramework/Actor/RectLightActor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "GameFramework/Actor/TextRenderActor.h"
#include "LevelSequence/LevelSequenceActor.h"
#include "GameFramework/Actor/ExponentialHeightFogActor.h"
#include "GameFramework/Actor/ProjectileActor.h"
#include "GameFramework/Actor/RifleActor.h"
#include "GameFramework/Actor/ZombieActor.h"
#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "GameFramework/Actor/DecalActor.h"
#include "GameFramework/Actor/FakeSpotLightActor.h"
#include "GameFramework/Actor/PlayerStart.h"
#include "GameFramework/Character.h"

namespace
{
	// Add 메뉴 항목. 같은 Category가 이어지는 동안 한 구획으로 묶어 그린다.
	struct FSpawnEntry
	{
		const char* Category;
		const char* Label;
		UClass* (*GetClass)();
	};

	const FSpawnEntry SpawnEntries[] = {
		{ "Basic",    "Empty Actor",       &AActor::StaticClass },
		{ "Basic",    "Empty Character",   &ACharacter::StaticClass },
		{ "Basic",    "Static Mesh",       &AStaticMeshActor::StaticClass },
		{ "Basic",    "Skeletal Mesh",     &ASkeletalMeshActor::StaticClass },
		{ "Basic",    "Text",              &ATextRenderActor::StaticClass },
		{ "Lights",   "Directional Light", &ADirectionalLightActor::StaticClass },
		{ "Lights",   "Point Light",       &APointLightActor::StaticClass },
		{ "Lights",   "Spot Light",        &ASpotLightActor::StaticClass },
		{ "Lights",   "Rect Light",        &ARectLightActor::StaticClass },
		{ "Lights",   "Sky Light",         &ASkyLightActor::StaticClass },
		{ "Lights",   "Fake Spot Light",   &AFakeSpotLightActor::StaticClass },
		{ "Effects",  "Decal",             &ADecalActor::StaticClass },
		{ "Effects",  "Particle",          &AParticleActor::StaticClass },
		{ "Effects",  "Particle System",   &AParticleSystemActor::StaticClass },
		{ "Effects",  "Height Fog",        &AExponentialHeightFogActor::StaticClass },
		{ "Gameplay", "Projectile",        &AProjectileActor::StaticClass },
		{ "Gameplay", "Rifle",             &ARifleActor::StaticClass },
		{ "Gameplay", "Zombie",            &AZombieActor::StaticClass },
		{ "Gameplay", "Player Start",      &APlayerStart::StaticClass },
		{ "Gameplay", "Level Sequence",    &ALevelSequenceActor::StaticClass },
	};

	constexpr const char* GizmoModeLabels[] = { "Move", "Rotate", "Scale" };
}


// 패널 초기화 성공을 반환한다.
bool FEditorControlsPanel::Init()
{
	return true;
}

// Space 입력으로 기즈모 편집 모드를 순환한다.
// 언리얼처럼 레벨 에디터 전체 단축키로 다룬다. 뷰포트 포커스만 보면 Add Actor 메뉴나 아웃라이너를 누른 직후
// 포커스가 그 창에 남아 뷰포트를 다시 클릭할 때까지 Space가 먹지 않는다.
void FEditorControlsPanel::Tick(float DeltaTime)
{
	const ImGuiIO& IO = ImGui::GetIO();
	// 이름·값 입력 중이거나, ImGui 키보드 내비게이션 커서가 보일 때(Space = 항목 활성화)는 양보한다.
	if (IO.WantTextInput || IO.NavVisible)
		return;
	// Add 메뉴·우클릭 메뉴 같은 팝업이 열려 있으면 그 메뉴의 입력이다.
	if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup))
		return;

	// 다른 창 (fbx 뷰어)이 Spacebar를 쓰는 중이면 Level Gizmo는 건드리지 않음
	if (SkipGizmoHotkeyQuery && SkipGizmoHotkeyQuery())
		return;
	if (IsPlayingQuery && IsPlayingQuery())
		return;

	if (Gizmo && FInputSystem::IsKeyPressed(EKeyCode::Space))
	{
		const int NextMode = (static_cast<int>(Gizmo->GetMode()) + 1) % 3;
		Gizmo->SetMode(static_cast<EGizmoMode>(NextMode));
	}
}

AActor* FEditorControlsPanel::SpawnInFrontOfCamera(UClass* Class)
{
	if (!World || !Class || (IsPlayingQuery && IsPlayingQuery())) return nullptr;

	// 화면을 그리는 활성 편집 View의 시점을 쓴다. 메인 카메라 컴포넌트의 월드 회전은 View와 다를 수 있다.
	constexpr float SpawnDistance = 10.0f;
	const FEditorViewportClient* Client = ViewportsPanel ? ViewportsPanel->GetViewportClient(ViewportsPanel->GetActiveViewIndex()) : nullptr;
	FTransform SpawnTransform;
	if (Client)
		SpawnTransform.Location = Client->GetViewLocation() + Client->GetViewForward() * SpawnDistance;
	AActor* Actor = World->SpawnActor(Class, NAME_None, &SpawnTransform);
	// Blueprint/SCS가 없는 이 엔진에서는 에디터의 빈 액터 배치에서만 기본 루트를 제공한다.
	// 런타임 SpawnActor<AActor>()는 컴포넌트 없는 네이티브 액터로 남는다.
	if (Actor && Class == AActor::StaticClass())
	{
		if (USceneComponent* Root = Cast<USceneComponent>(Actor->AddComponent(USceneComponent::StaticClass(), "DefaultSceneRoot")))
		{
			Root->MarkUserCreated();
			Root->SetTransform(SpawnTransform);
		}
	}
	if (Actor && OnActorSpawned)
		OnActorSpawned(Actor);
	return Actor;
}

// 툴바의 글자 버튼은 모두 같은 폭이다. 가장 긴 라벨에 맞춰서 라벨이 바뀌어도(Local/World) 크기가 변하지 않는다.
float FEditorControlsPanel::GetToolbarButtonWidth()
{
	float Width = EditorUIScale::ButtonWidth("+ Add");
	for (const char* Label : GizmoModeLabels)
		Width = std::max(Width, EditorUIScale::ButtonWidth(Label));
	Width = std::max(Width, EditorUIScale::ButtonWidth("Local"));
	Width = std::max(Width, EditorUIScale::ButtonWidth("World"));
	return Width;
}

// Play 그룹과 같은 규칙(배경 상자, 4px 간격, 그룹 사이 구분선)으로 그린다.
void FEditorControlsPanel::DrawToolbar()
{
	const ImVec2 ButtonSize{ GetToolbarButtonWidth(), ImGui::GetFrameHeight() };
	LevelEditorToolbarUI::BeginGroup();
	DrawAddActorMenu(ButtonSize);
	ImGui::SameLine();
	ImGui::BeginDisabled(IsPlayingQuery && IsPlayingQuery());
	if (ImGui::Button("Player Start", ImVec2(EditorUIScale::ButtonWidth("Player Start"), ButtonSize.y)))
		SpawnInFrontOfCamera(APlayerStart::StaticClass());
	ImGui::EndDisabled();
	LevelEditorToolbarUI::EndGroup();
	if (Gizmo)
	{
		LevelEditorToolbarUI::Separator();
		LevelEditorToolbarUI::BeginGroup();
		DrawGizmoControls(ButtonSize);
		LevelEditorToolbarUI::EndGroup();
	}
}

void FEditorControlsPanel::DrawAddActorMenu(const ImVec2& ButtonSize)
{
	ImGui::BeginDisabled(IsPlayingQuery && IsPlayingQuery());
	if (ImGui::Button("+ Add", ButtonSize)) ImGui::OpenPopup("AddActorMenu");
	ImGui::SetItemTooltip("Add an actor in front of the active viewport camera.");
	// 버튼의 어느 지점을 눌러도 목록은 버튼 바로 아래 왼쪽 끝에서 열린다.
	LevelEditorToolbarUI::PlaceNextPopupBelowLastItem();
	if (ImGui::BeginPopup("AddActorMenu"))
	{
		const char* CurrentCategory = nullptr;
		for (const FSpawnEntry& Entry : SpawnEntries)
		{
			if (!CurrentCategory || std::strcmp(CurrentCategory, Entry.Category) != 0)
			{
				CurrentCategory = Entry.Category;
				ImGui::SeparatorText(CurrentCategory);
			}
			if (ImGui::MenuItem(Entry.Label))
				SpawnInFrontOfCamera(Entry.GetClass());
		}
		ImGui::EndPopup();
	}
	ImGui::EndDisabled();
}

// 모드는 토글 버튼 세 개, 좌표계는 누를 때마다 바뀌는 버튼 하나로 그린다.
void FEditorControlsPanel::DrawGizmoControls(const ImVec2& ButtonSize)
{
	const int CurrentMode = static_cast<int>(Gizmo->GetMode());
	// 선택된 모드만 강조색을 쓰고 나머지는 입력 칸과 같은 중립색으로 둬서 현재 모드가 한눈에 보이게 한다.
	const ImVec4 UnselectedColor = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
	const ImVec4 UnselectedHoverColor = ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered);
	for (int Mode = 0; Mode < IM_ARRAYSIZE(GizmoModeLabels); ++Mode)
	{
		if (Mode > 0) ImGui::SameLine();
		const bool bSelected = Mode == CurrentMode;
		if (!bSelected)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, UnselectedColor);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UnselectedHoverColor);
		}
		if (ImGui::Button(GizmoModeLabels[Mode], ButtonSize))
			Gizmo->SetMode(static_cast<EGizmoMode>(Mode));
		if (!bSelected) ImGui::PopStyleColor(2);
		ImGui::SetItemTooltip("%s gizmo. Space cycles modes in the viewport.", GizmoModeLabels[Mode]);
	}

	ImGui::SameLine();
	const bool bWorld = Gizmo->GetSpace() == EGizmoSpace::World;
	if (ImGui::Button(bWorld ? "World" : "Local", ButtonSize))
		Gizmo->SetSpace(bWorld ? EGizmoSpace::Local : EGizmoSpace::World);
	ImGui::SetItemTooltip("Gizmo space. Click to switch to %s.", bWorld ? "Local" : "World");
}

// 프레임 시간과 월드의 액터 수를 보여 준다. 액터 추가와 기즈모는 상단 툴바에 있다.
void FEditorControlsPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(400.0f), EditorUIScale::Px(120.0f)), ImGuiCond_FirstUseEver);
	if (!BeginPanel())
	{
		ImGui::End();
		return;
	}
	ImGui::Spacing();
	ImGui::Text("FPS %.1f", 1.0f / DeltaTime);
	ImGui::SameLine();
	ImGui::TextDisabled("%.1f ms", DeltaTime * 1000.0f);
	ImGui::TextDisabled("%d actors", World ? World->GetActorNum() : 0);
	ImGui::End();
}

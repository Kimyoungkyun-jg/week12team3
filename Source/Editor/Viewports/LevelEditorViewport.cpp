#include "EnginePCH.h"
#include "Editor/Viewports/LevelEditorViewport.h"
#include "Editor/HitoriEd/Editor.h"
#include "Launch/LaunchEngineLoop.h"
#include "Render/Renderer.h"
#include "Render/SceneRenderer.h"
#include "Components/ShapeComponent.h"
#include "Components/DecalComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Actor/DecalActor.h"
#include "GameFramework/Actor/PointLightActor.h"
#include "GameFramework/Actor/SpotLightActor.h"
#include "GameFramework/Actor/RectLightActor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Asset/AssetManager.h"

FLevelEditorViewportClient* GCurrentLevelEditingViewportClient = nullptr;

FLevelEditorViewportClient::FLevelEditorViewportClient()
{
	// 그리드 셰이더와 버퍼를 준비한다.
	DrawHelper.Init(GEditor ? GEditor->GetEngineLoop().GetRenderer() : nullptr);

	// 깊이 시각화와 UUID 글자를 그리는 도구를 준비한다. 두 Init의 렌더러 인자는 쓰이지 않는다.
	SceneDepthRenderer.Init(nullptr);
	CascadeFloorRenderer.Init();
	TextRenderer.Init();
	SystemFont = UAssetManager::GetAssetByPath<UFont>("Engine/Fonts/Pretendard.json");
}

// 사라지는 뷰포트를 전역이 계속 가리키지 않게 한다. UE도 소멸자에서 같은 일을 한다.
FLevelEditorViewportClient::~FLevelEditorViewportClient()
{
	if (GCurrentLevelEditingViewportClient == this)
	{
		GCurrentLevelEditingViewportClient = nullptr;
	}
}

// 이번 프레임의 표시 설정을 채운 뒤 기반 클래스의 Draw로 씬을 그린다.
// 에디터 설정 창의 값과 이 뷰포트의 뷰 모드를 EngineShowFlags에 옮긴다.
// UE의 FLevelEditorViewportClient::Draw(FViewport*, FCanvas*)도 준비를 한 뒤 FEditorViewportClient::Draw를 부른다.
void FLevelEditorViewportClient::Draw(FRenderer* Renderer)
{
	if (const FEditorSettings* EditorSettings = GEditor ? GEditor->GetEditorSettings() : nullptr)
	{
		const FEditorSettings& S = *EditorSettings;

		// 모든 뷰포트에 똑같이 적용되는 에디터 설정
		EngineShowFlags.bEnableFXAA = S.bEnableFXAA;
		EngineShowFlags.bCacheShadows = S.bCacheShadows;
		EngineShowFlags.PcfMode = S.PcfMode;
		EngineShowFlags.RenderingPath = S.RenderingPath;
		EngineShowFlags.LightingModel = S.LightingModel;
		EngineShowFlags.LightCullingMode = S.LightCullingMode;
		EngineShowFlags.DirectionalShadowMapSize = S.GetDirectionalShadowMapSize();
		EngineShowFlags.PointShadowMapSize = S.GetPointShadowMapSize();
		EngineShowFlags.SpotShadowMapSize = S.GetSpotShadowMapSize();
		EngineShowFlags.bGammaCorrection = S.bGammaCorrection;
		EngineShowFlags.DisplayGamma = S.DisplayGamma;
		EngineShowFlags.bShowVignette = S.bShowVignette;
		EngineShowFlags.bShowCameraOverlay = S.bShowCameraOverlay;
		EngineShowFlags.bShowSkeletalMesh = S.bShowSkeletalMesh;
		EngineShowFlags.bDrawPrimitives = S.bDrawPrimitives;
		EngineShowFlags.bDrawDecals = S.bDrawDecals;
		EngineShowFlags.bDecalCulling = S.bDecalCulling;
		EngineShowFlags.bDecalNarrowCulling = S.bDecalNarrowCulling;
		EngineShowFlags.bDecalBVHCulling = S.bDecalBVHCulling;
		EngineShowFlags.bDecalOBBTree = S.bDecalOBBTree;

		// 이 뷰포트의 뷰 모드에서 나오는 설정
		EngineShowFlags.bEnableFog = ViewportMode == EViewportMode::Solid;
		EngineShowFlags.bShowSceneDepth = IsSceneDepth();
		EngineShowFlags.bShowCascade = IsCascade();
		EngineShowFlags.bShowShadowMask = IsShadowMask();
		EngineShowFlags.bShowWorldNormal = IsWorldNormal();
		EngineShowFlags.bShowLightCulling = IsLightCulling();
		EngineShowFlags.SceneDepthRange = MaxRange;
	}

	FEditorViewportClient::Draw(Renderer);
}

// 레벨 에디터 뷰포트의 씬 위에 선, 그리드, 외곽선, 기즈모를 올린다. 씬은 기반 클래스의 Draw가 그린다.
// 여기서는 그릴 시점에 불릴 함수를 등록만 하고, 등록한 함수는 FSceneRenderer::Render 안에서 불린다.
void FLevelEditorViewportClient::Draw(const FSceneView* View, FScenePassCallbacks& Callbacks)
{
	if (!GEditor || !GEditor->GetEditorSettings()) return;
	const FSceneView& SceneView = *View;

	// 디버그 선은 이 뷰포트가 보는 월드의 LineBatcher로 그린다.
	// 무엇이 선택됐는지(Gizmo, Outline)와 에디터 설정은 에디터 전체가 공유하므로 엔진(GEditor)에서 읽는다.
	const FEditorSettings& Settings = *GEditor->GetEditorSettings();
	FLineBatcher* LineBatcher = GetWorld()->LineBatcher.get();
	FGizmo* Gizmo = GEditor->GetGizmo();
	FOutline* Outline = GEditor->GetOutline();

	// 뷰 모드가 Scene Depth, Cascade일 때 깊이를 색으로 바꿔 보여 준다.
	FTexture2D* ColorTarget = ViewTargets.SceneColor.get();
	FTexture2D* DepthTarget = ViewTargets.Depth.get();
	Callbacks.DepthVisualization = [this, &SceneView, DepthTarget, ColorTarget]
		{
			SceneDepthRenderer.OnRender(SceneView, DepthTarget, ColorTarget, MaxRange);
		};
	if (IsCascade())
		Callbacks.CascadeVisualization = [this, &SceneView, DepthTarget, ColorTarget]
		{
			CascadeFloorRenderer.OnRender(SceneView, RenderData, DepthTarget, ColorTarget);
		};

	const FViewportSettings ViewportSetting{ 0, 0, ViewTargets.SceneColor->GetWidth(), ViewTargets.SceneColor->GetHeight(), 0.0f, 1.0f };
	const bool bIsActiveViewport = (GCurrentLevelEditingViewportClient == this);

	const bool bDrawSelectedLight = bIsActiveViewport && Gizmo->GetTarget() &&
		(Cast<ASpotLightActor>(Gizmo->GetTarget()->GetOwner()) ||
			Cast<APointLightActor>(Gizmo->GetTarget()->GetOwner()) ||
			Cast<ARectLightActor>(Gizmo->GetTarget()->GetOwner()) ||
			Cast<ADecalActor>(Gizmo->GetTarget()->GetOwner()));

	const bool bDrawCascadeBounds = Settings.bDrawCascadeBounds;
	{
		Callbacks.Overlays.EditorLines = [this, &SceneView, &Settings, LineBatcher, Gizmo, bDrawSelectedLight, bDrawCascadeBounds]
			{
				LineBatcher->BeginFrame();
				if (bDrawCascadeBounds)
				{
					FSceneRenderData& Data = RenderData;
					const FVector4 Colors[4] = {
						FVector4(1.f, 0.2f, 0.2f, 1.f),
						FVector4(1.f, 0.8f, 0.2f, 1.f),
						FVector4(0.2f, 1.f, 0.4f, 1.f),
						FVector4(0.2f, 0.6f, 1.f, 1.f)
					};
					const FBox ClipBox{ FVector(-1.f, -1.f, 0.f), FVector(1.f, 1.f, 1.f) };
					// 광원의 첫 그림자 뷰(VisibleLightShadowViews)부터 cascade 순서로 들어 있다. 그림자가 없는 방향광은 건너뛴다.
					const uint32 DirectionalOffset = Data.GetDirectionalLightOffset();
					for (uint32 LightIndex = 0; LightIndex < Data.GetDirectionalLightCount(); ++LightIndex)
					{
						const uint32 VisibleIndex = DirectionalOffset + LightIndex;
						if (VisibleIndex >= static_cast<uint32>(Data.VisibleLightShadowViews.Num())) continue;
						const uint32 FirstCascade = Data.VisibleLightShadowViews[VisibleIndex];
						if (FirstCascade == FSceneRenderData::InvalidShadowView) continue;
						for (uint32 CascadeIndex = 0; CascadeIndex < Data.CascadeShadowCount; ++CascadeIndex)
						{
							const uint32 ShadowIndex = FirstCascade + CascadeIndex;
							if (ShadowIndex >= static_cast<uint32>(Data.LightShadows.Num())) break;
							LineBatcher->AddOrientedBox(ClipBox, Data.LightShadows[ShadowIndex].ViewProjection.Inverse(), Colors[CascadeIndex]);
						}
					}
				}
				if (Settings.bDrawBatchLine && Settings.bDrawBoundingBox)
				{
					LineBatcher->BuildVertexBuffer();
				}
				
				if (bDrawSelectedLight)
				{
					if (ASpotLightActor* Light = Cast<ASpotLightActor>(Gizmo->GetTarget()->GetOwner()))
						Light->GetSpotLightComponent()->DrawDebug(LineBatcher);
					else if (APointLightActor* Light = Cast<APointLightActor>(Gizmo->GetTarget()->GetOwner()))
						Light->GetPointLightComponent()->DrawDebug(LineBatcher);
					else if (ARectLightActor* Light = Cast<ARectLightActor>(Gizmo->GetTarget()->GetOwner()))
						Light->GetRectLightComponent()->DrawDebug(LineBatcher);
					else if (ADecalActor* Decal = Cast<ADecalActor>(Gizmo->GetTarget()->GetOwner()))
						Decal->GetDecalComponent()->DrawDebug(LineBatcher);
				}

				const AActor* SelectedActor =
					Gizmo->GetTarget() ? Gizmo->GetTarget()->GetOwner() : nullptr;

				// 이 뷰포트가 보고 있는 월드의 것을 그린다. PIE와 SIE 중에는 PIE 월드다.
				DrawCollisionShapes(GetWorld(), LineBatcher, SelectedActor);
				DrawSpringArms(GetWorld(), LineBatcher, SelectedActor);

				LineBatcher->OnRender(SceneView.ViewProjectionMatrix);
			};
	}
	if (Settings.bDrawBatchLine)
	{
		Callbacks.Overlays.EditorGrid = [this, &SceneView, &Settings, ViewportSetting]
			{
				EGridPlane GridPlane = EGridPlane::XY;
				switch (ViewportType)
				{
				case ELevelViewportType::Front: GridPlane = EGridPlane::YZ; break;
				case ELevelViewportType::Right: GridPlane = EGridPlane::XZ; break;
				default: break;
				}
				if (Settings.bDrawPSGrid && SceneView.bIsPerspective)
					DrawHelper.OnRenderPSGrid(SceneView.ViewProjectionMatrix, SceneView.ViewLocation,
						Settings, ViewportSetting);
				else
					DrawHelper.OnRenderBatchGrid(SceneView.ViewProjectionMatrix, SceneView.ViewLocation,
						SceneView.ViewForward, GridPlane,
						static_cast<float>(Settings.GridSpacing),
						SceneView.bIsPerspective, ViewportSetting);
			};
	}
	if (bIsActiveViewport && Outline->GetTarget())
		Callbacks.Overlays.EditorOutline = [&SceneView, Outline, ViewportSetting]
		{
			Outline->Render(&SceneView, ViewportSetting);
		};
	if (bIsActiveViewport && Gizmo->GetTarget())
		Callbacks.Overlays.EditorGizmo = [&SceneView, Gizmo]
		{
			Gizmo->Render(&SceneView);
		};
	Callbacks.Overlays.bClearEditorDepth = true;
	if (Settings.bShowUUID)
		Callbacks.Overlays.EditorUUID = [this, &SceneView] { RenderActorUUIDs(SceneView); };
}

// 이 뷰포트가 보는 월드의 액터마다, 경계 상자 위에 카메라를 향한 UUID 글자를 그린다.
void FLevelEditorViewportClient::RenderActorUUIDs(const FSceneView& SceneView)
{
	const bool bOrtho = !SceneView.bIsPerspective;
	UWorld* TargetWorld = GetWorld();
	if (!TargetWorld || !SystemFont) return;

	for (AActor* Actor : TargetWorld->GetPersistentLevel()->GetActors())
	{
		if (!Actor)
			continue;
		USceneComponent* Root = Actor->GetRootComponent();

		if (!Root)
			continue;

		FBox Box = Root->CalcBounds();
		FVector UUIDLoc((Box.Min.X + Box.Max.X) * 0.5f,
			(Box.Min.Y + Box.Max.Y) * 0.5f, Box.Max.Z + 0.5f);

		FString Text = "UUID : " + std::to_string(Actor->GetUUID());
		TextRenderer.BuildTextMesh(Text, 0.5f, *SystemFont);

		FVector Facing = bOrtho ? (SceneView.ViewForward * -1.0f)
			: (SceneView.ViewLocation - UUIDLoc).Normalized();
		FVector Up = FVector(0.0f, 0.0f, 1.0f);
		FVector Right = FVector::Cross(Up, Facing).Normalized();
		FVector RealUp = FVector::Cross(Facing, Right).Normalized();
		FMatrix BillboardWorld = FMatrix::Identity;
		BillboardWorld.M[0][0] = Facing.X;
		BillboardWorld.M[0][1] = Facing.Y;
		BillboardWorld.M[0][2] = Facing.Z;
		BillboardWorld.M[1][0] = Right.X;
		BillboardWorld.M[1][1] = Right.Y;
		BillboardWorld.M[1][2] = Right.Z;
		BillboardWorld.M[2][0] = RealUp.X;
		BillboardWorld.M[2][1] = RealUp.Y;
		BillboardWorld.M[2][2] = RealUp.Z;
		BillboardWorld.M[3][0] = UUIDLoc.X;
		BillboardWorld.M[3][1] = UUIDLoc.Y;
		BillboardWorld.M[3][2] = UUIDLoc.Z;

		TextRenderer.OnRender(Text, BillboardWorld, 0.5f, *SystemFont,
			SceneView.ViewProjectionMatrix);
	}
}

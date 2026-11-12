#include "EnginePCH.h"

#include "Editor/HitoriEd/EditorEngine.h"

#include "Core/EngineStatics.h"
#include "Core/EngineTimer.h"
#include "Launch/LaunchEngineLoop.h"
#include "Core/StatOverlay.h"
#include "Input/InputSystem.h"

#include "Render/GeometryGenerator.h"

#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"

#include "Render/Renderer.h"
#include "Render/SceneRenderer.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Actor/LightActorBase.h"
#include "GameFramework/Actor/PointLightActor.h"
#include "GameFramework/Actor/SpotLightActor.h"
#include "GameFramework/Actor/RectLightActor.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/TextRenderComponent.h"

#include "Asset/AssetManager.h"
#include "Render/RenderResourceManager.h"

#include "Render/RenderCommand.h"
#include "Editor/Outliner/OutlinerPanel.h"
#include "Editor/HitoriEd/EditorFileUtils.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "UObject/UObjectIterator.h"

#include "Core/EngineLog.h"
#include "Core/Stats/LightweightStats.h"

#include "Script/LuaScriptManager.h"
#include <sol/sol.hpp>

#include "Components/ShapeComponent.h"
#include "Components/SkeletalMeshComponent.h"

#include "Physics/Collision.h"

#include "Audio/AudioDevice.h"
#include "Core/SplashScreen.h"
#include "Core/Misc/Paths.h"
#include "Editor/EditorUI/EditorTitleBar.h"

// UEditorEngine의 PIE(Play In Editor) 부분. UE도 같은 클래스의 PIE 함수들을 PlayLevel.cpp에 둔다.
// 세션 시작과 종료, PIE 입력과 마우스 캡처, 별도 PIE 창을 다룬다.

void UEditorEngine::UpdatePIEViewCamera()
{
	if (!PlayWorld || PIEViewIndex == InvalidViewIndex)
		return;
	FWorldContext* Context = GetPIEWorldContext();
	if (!Context || !Context->GameViewport)
		return;
	FGameViewportClient& GameViewport = *Context->GameViewport;
	if (PIEViewIndex >= 0 && PIEViewIndex < AllViewportClients.Num())
	{
		if (FEditorViewportClient* View = AllViewportClients[PIEViewIndex])
		{
			const FRect& GameRect = View->GetRect();
			if (GameRect.Width <= 0.0f || GameRect.Height <= 0.0f) return;
			const FSceneView GameView = GameViewport.CalcSceneView(GameRect);
			View->SetViewLocation(GameView.ViewLocation);
			// 에디터 카메라는 Roll 없이 Pitch와 Yaw만으로 움직인다. 게임 카메라의 회전값에는 부모의 회전이나
			// 카메라 효과로 Roll이 섞여 있을 수 있어서, 보는 방향만 가져와 Pitch와 Yaw로 다시 만든다.
			FRotator ViewRotation;
			ViewRotation.LookDirection(GameView.ViewForward);
			View->SetViewRotation(ViewRotation);
			View->SetViewFOV(GameView.FieldOfView);
			View->SetOrthoWidth(GameView.OrthoWidth);
			View->SetIsPerspective(GameView.bIsPerspective);
		}
	}
}

// PIE와 SIE 사이를 오간다. UE처럼 게임 화면과 레벨 에디터 뷰포트가 자리를 바꾼다.
// SIE에서는 PIE 월드를 레벨 에디터 뷰포트 클라이언트가 그리고, 카메라 이동과 선택도 편집할 때와 같다.
void UEditorEngine::ToggleBetweenPIEandSIE()
{
	// UE처럼 레벨 에디터 뷰포트 안에서 도는 PIE만 전환할 수 있다. 별도 창 PIE에는 바꿔 끼울 뷰포트가 없다.
	if (!PlayWorld || IsPIEInWindow()) return;
	FWorldContext* Context = GetPIEWorldContext();
	if (!Context || !Context->GameViewport) return;
	FGameViewportClient& GameViewport = *Context->GameViewport;

	if (bIsSimulatingInEditor)
	{
		// 게임으로 돌아간다. 뷰포트 패널이 다시 게임 화면을 보여 주게 한다.
		if (ViewportsPanel)
			ViewportsPanel->SetGameViewportClient(PIEStartEditorViewIndex, &GameViewport);
		bIsSimulatingInEditor = false;
		bPendingPIEMouseCapture = true;   // 게임으로 돌아오면 다시 잡는다
	}
	else
	{
		EndPIEMouseCapture();
		// 누르고 있던 게임 입력을 지운다.
		GameViewport.LostFocus();
		// 에디터 카메라를 플레이어가 보던 위치로 옮겨, 보던 화면 그대로 SIE가 시작되게 한다.
		UpdatePIEViewCamera();
		// 뷰포트 패널이 게임 화면 대신 에디터 뷰포트의 화면을 보여 주게 한다.
		if (ViewportsPanel)
			ViewportsPanel->ClearGameViewportClient();
		bIsSimulatingInEditor = true;
	}
	OnActiveWorldChanged();
}

void UEditorEngine::TickPIEInput(float DeltaTime)
{
	FWorldContext* Context = GetPIEWorldContext();
	if (!Context || !Context->GameViewport) return;
	FGameViewportClient& GameViewport = *Context->GameViewport;
	const FVector2 Mouse = ViewportsPanel->GetLocalMousePosition();
	const FRect PIEViewRect = (PIEViewIndex >= 0 && PIEViewIndex < AllViewportClients.Num() && AllViewportClients[PIEViewIndex])
		? AllViewportClients[PIEViewIndex]->GetRect() : FRect{};
	const bool bMouseOverPIE = Mouse.X >= PIEViewRect.X && Mouse.Y >= PIEViewRect.Y &&
		Mouse.X < PIEViewRect.X + PIEViewRect.Width && Mouse.Y < PIEViewRect.Y + PIEViewRect.Height;
	const int32 ActiveViewIdx = ViewportsPanel ? ViewportsPanel->GetActiveViewIndex() : -1;
	const bool bClickPressed =
		FInputSystem::IsMousePressed(EMouseButton::Left) || FInputSystem::IsMousePressed(EMouseButton::Right);
	const HWND InputWindow = IsPIEInWindow() ? PIEWindow->GetHandle() : MainWindow->GetHandle();
	const bool bInputWindowForeground = ::GetForegroundWindow() == InputWindow;

	// 마우스 캡처: 캡처 중에는 커서가 PIE 영역 밖으로 못 나가므로 hover 판정 없이 입력을 유지한다.
	if (bGameMouseCaptured)
	{
		const bool bReleaseKey = FInputSystem::IsKeyDown(EKeyCode::Shift) && FInputSystem::IsKeyPressed(EKeyCode::F1);
		if (bReleaseKey || bIsSimulatingInEditor || !bInputWindowForeground)
			EndPIEMouseCapture();
	}
	bool bCaptureStartedThisFrame = false;
	if (!bGameMouseCaptured && !bIsSimulatingInEditor && bInputWindowForeground)
	{
		// 별도 PIE 창은 창이 포그라운드면 클릭 위치가 곧 PIE 영역이다
		const bool bClickOnPIE = bClickPressed && (IsPIEInWindow() ||
			(bMouseOverPIE && ViewportsPanel->IsHovered() &&
			 !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)));
		if (bPendingPIEMouseCapture || bClickOnPIE)
		{
			BeginPIEMouseCapture();
			// 캡처를 시작한 클릭은 게임에 넘기지 않고, 이번 프레임 이동량도 버린다
			bCaptureStartedThisFrame = bGameMouseCaptured;
		}
	}

	const bool bFocused = bGameMouseCaptured || (IsPIEInWindow()
		? bInputWindowForeground
		: bInputWindowForeground &&
		ActiveViewIdx == PIEViewIndex &&
		ViewportsPanel->IsHovered() &&
		!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup));
	if (!bFocused) { GameViewport.LostFocus(); return; }
	if (!bGameMouseCaptured && !IsPIEInWindow() && bClickPressed && !bMouseOverPIE)
	{
		GameViewport.LostFocus();
		return;
	}

	if (FInputSystem::IsKeyPressed(EKeyCode::F8))
		ToggleBetweenPIEandSIE();
	if (FInputSystem::IsKeyPressed(EKeyCode::Escape))
		GameViewport.InputKey(nullptr, static_cast<int32>(EKeyCode::Escape), true);
	if (GameViewport.ConsumeExitRequest())
	{
		RequestEndPlayMap();
		return;
	}
	// SIE에서는 게임에 입력을 넘기지 않는다. 카메라 이동과 선택은 에디터 뷰포트가 처리한다.
	// 캡처 전(Shift+F1로 풀었거나 아직 클릭 전)에도 F8/Esc만 받고 게임 입력은 막는다 (UE와 동일)
	if (bIsSimulatingInEditor || !bGameMouseCaptured)
	{
		GameViewport.LostFocus();
		return;
	}
	constexpr EKeyCode Keys[] = { EKeyCode::W, EKeyCode::A, EKeyCode::S, EKeyCode::D,
		EKeyCode::Space, EKeyCode::Shift };
	for (EKeyCode Key : Keys)
	{
		if (FInputSystem::IsKeyPressed(Key))
			GameViewport.InputKey(nullptr, static_cast<int32>(Key), true);
		if (FInputSystem::IsKeyReleased(Key))
			GameViewport.InputKey(nullptr, static_cast<int32>(Key), false);
	}

	// UI 히트 테스트용 마우스 좌표 → 게임 렌더 타깃 기준
	if (IsPIEInWindow())
	{
		// PIE 창은 렌더 타깃과 크기가 같고, FInputSystem 좌표가 그 창의 클라이언트 좌표다.
		GameViewport.MouseMove(nullptr, FInputSystem::GetMouseX(), FInputSystem::GetMouseY());
	}
	else if (PIEViewRect.Width > 0.0f && PIEViewRect.Height > 0.0f)
	{
		// Mouse는 뷰포트 패널 기준, PIEViewRect는 그 안의 게임 뷰 영역이다.
		const float U = (Mouse.X - PIEViewRect.X) / PIEViewRect.Width;
		const float V = (Mouse.Y - PIEViewRect.Y) / PIEViewRect.Height;
		GameViewport.MouseMove(nullptr,
			static_cast<int32>(U * GameViewport.GetWidth()),
			static_cast<int32>(V * GameViewport.GetHeight()));
	}

	constexpr EMouseButton MouseButtons[] = { EMouseButton::Left, EMouseButton::Right };
	for (EMouseButton Button : MouseButtons)
	{
		const int32 Key = Button == EMouseButton::Left ? static_cast<int32>(EKeyCode::LButton) : static_cast<int32>(EKeyCode::RButton);
		if (FInputSystem::IsMousePressed(Button) && !bCaptureStartedThisFrame)
			GameViewport.InputKey(nullptr, Key, true);
		if (FInputSystem::IsMouseReleased(Button))
			GameViewport.InputKey(nullptr, Key, false);
	}
	float MouseX = static_cast<float>(FInputSystem::GetMouseDeltaX());
	float MouseY = static_cast<float>(FInputSystem::GetMouseDeltaY());
	if (bGameMouseCaptured)
	{
		// 캡처 중에는 WM_MOUSEMOVE 좌표 차 대신 "지난 프레임에 놓아 둔 중앙에서 얼마나 움직였나"를 쓴다.
		// 커서가 가장자리에 닿아 이동량이 0이 되는 일이 없고, 어느 창이 메시지를 받든 상관없다.
		POINT Cursor{};
		if (::GetCursorPos(&Cursor))
		{
			MouseX = static_cast<float>(Cursor.x - PIECaptureCenterX);
			MouseY = static_cast<float>(Cursor.y - PIECaptureCenterY);
		}
		if (bCaptureStartedThisFrame || !RecenterPIEMouseCapture())
			MouseX = MouseY = 0.0f;
	}
	GameViewport.InputAxis(nullptr, 0, EGameInputAxis::MouseX, MouseX, DeltaTime);
	GameViewport.InputAxis(nullptr, 0, EGameInputAxis::MouseY, MouseY, DeltaTime);
}

bool UEditorEngine::GetPIECaptureScreenRect(int32& OutLeft, int32& OutTop, int32& OutRight, int32& OutBottom) const
{
	if (IsPIEInWindow())
	{
		RECT Client{};
		if (!::GetClientRect(PIEWindow->GetHandle(), &Client)) return false;
		POINT TopLeft{ Client.left, Client.top };
		POINT BottomRight{ Client.right, Client.bottom };
		::ClientToScreen(PIEWindow->GetHandle(), &TopLeft);
		::ClientToScreen(PIEWindow->GetHandle(), &BottomRight);
		OutLeft = TopLeft.x; OutTop = TopLeft.y; OutRight = BottomRight.x; OutBottom = BottomRight.y;
	}
	else
	{
		if (!ViewportsPanel || PIEViewIndex < 0 || PIEViewIndex >= AllViewportClients.Num() || !AllViewportClients[PIEViewIndex])
			return false;
		// PIE View Rect는 Viewports 패널 내용 기준, 패널 원점은 화면 좌표
		const FRect ViewRect = AllViewportClients[PIEViewIndex]->GetRect();
		const FVector2 Origin = ViewportsPanel->GetContentOrigin();
		OutLeft = static_cast<int32>(Origin.X + ViewRect.X);
		OutTop = static_cast<int32>(Origin.Y + ViewRect.Y);
		OutRight = static_cast<int32>(Origin.X + ViewRect.X + ViewRect.Width);
		OutBottom = static_cast<int32>(Origin.Y + ViewRect.Y + ViewRect.Height);
	}
	return OutRight - OutLeft > 2 && OutBottom - OutTop > 2;
}

bool UEditorEngine::RecenterPIEMouseCapture()
{
	int32 Left, Top, Right, Bottom;
	if (!GetPIECaptureScreenRect(Left, Top, Right, Bottom)) return false;
	// 패널 이동·크기 변경을 따라가도록 매 프레임 다시 가둔다
	const RECT Clip{ Left, Top, Right, Bottom };
	::ClipCursor(&Clip);
	PIECaptureCenterX = (Left + Right) / 2;
	PIECaptureCenterY = (Top + Bottom) / 2;
	::SetCursorPos(PIECaptureCenterX, PIECaptureCenterY);
	return true;
}

void UEditorEngine::BeginPIEMouseCapture()
{
	bPendingPIEMouseCapture = false;
	if (bGameMouseCaptured || !RecenterPIEMouseCapture()) return;
	bGameMouseCaptured = true;
	::ShowCursor(FALSE);
	// 숨은 커서가 에디터 UI(툴바, 패널)를 누르거나 hover하지 않게 한다
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
}

void UEditorEngine::EndPIEMouseCapture()
{
	bPendingPIEMouseCapture = false;
	if (!bGameMouseCaptured) return;
	bGameMouseCaptured = false;
	::ClipCursor(nullptr);
	::ShowCursor(TRUE);
	ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;

	// 캡처 중 SetCursorPos가 만든 이동이 풀린 직후 델타로 튀지 않게 현재 위치로 맞춘다
	POINT Cursor{};
	const HWND InputWindow = IsPIEInWindow() ? PIEWindow->GetHandle() : MainWindow->GetHandle();
	if (::GetCursorPos(&Cursor) && ::ScreenToClient(InputWindow, &Cursor))
		FInputSystem::SyncMousePosition(Cursor.x, Cursor.y);
}

// Play 버튼은 요청만 남긴다. 실제 시작은 다음 프레임의 시작에서 한다. UE의 같은 이름 함수와 같은 역할이다.
void UEditorEngine::StartQueuedPlaySessionRequest()
{
	if (!bPlaySessionRequested) return;
	bPlaySessionRequested = false;

	if (!PlayWorld)
	{
		StartPlayInEditorSession();
	}
}

void UEditorEngine::StartPlayInEditorSession()
{
	// 별도 창 모드면 패널 View는 모두 에디터 월드로 두고 창 하나에만 PIE 월드를 그린다.
	// 창을 만들지 못하면 선택한 View에서 실행한다.
	const bool bNewWindow = (PlayMode == EPlayModeType::InEditorFloating);

	PIEStartEditorViewIndex = ViewportsPanel ? ViewportsPanel->GetActiveViewIndex() : 0;
	if (bNewWindow && CreatePIEWindow())
		PIEViewIndex = -2;
	else
		// 지금 편집 중인 View 하나만 PIE 월드를 보여준다.
		PIEViewIndex = PIEStartEditorViewIndex;

	FWorldContext& PIEContext = CreateNewWorldContext(EWorldType::PIE);
	PIEContext.PIEInstance = 0;
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	GameInstance->SetWorldContext(&PIEContext);
	if (!GameInstance->InitializeForPlayInEditor(0) ||
		!GameInstance->StartPlayInEditorGameInstance())
	{
		UWorld* FailedWorld = PIEContext.World();
		DestroyWorldContext(FailedWorld);
		PIEViewIndex = InvalidViewIndex;
		PIEStartEditorViewIndex = InvalidViewIndex;
		DestroyPIEWindow();
		return;
	}
	PlayWorld = GameInstance->GetWorld();

	// PIE 게임 화면은 게임 뷰포트 클라이언트의 Draw가 그린다. 그 위에 에디터가 올릴 디버그 표시를 여기서 등록해 둔다.
	// Draw가 패스를 등록할 때마다 이 함수를 부른다. SIE 화면은 레벨 에디터 뷰포트가 그리므로 여기를 거치지 않는다.
	if (FGameViewportClient* GameClient = PIEContext.GameViewport.get())
	{
		GameClient->DebugDrawDelegate = [this, GameClient](const FSceneView& SceneView, FSceneOverlayCallbacks& Overlays)
		{
			// Settings > Debug Draw (PIE)
			Overlays.EditorLines = [this, &SceneView, GameClient]
				{
					DrawPIEDebug(GameClient->GetWorld(), SceneView.ViewProjectionMatrix);
				};
		};
	}

	if (!bNewWindow && PIEContext.GameViewport && ViewportsPanel)
	{
		ViewportsPanel->SetGameViewportClient(PIEStartEditorViewIndex, PIEContext.GameViewport.get());
	}
	// UE 기본값처럼 시작하자마자 마우스를 잡는다 (Shift+F1로 해제)
	bPendingPIEMouseCapture = true;

	OnActiveWorldChanged();
}

UWorld* UEditorEngine::CreatePIEWorldByDuplication(FWorldContext& PIEContext, UWorld* InEditorWorld)
{
	UWorld* NewPIEWorld = UWorld::GetDuplicatedWorldForPIE(InEditorWorld);
	if (!NewPIEWorld) return nullptr;
	const int32 StartView = PIEStartEditorViewIndex;
	if (StartView >= 0 && StartView < AllViewportClients.Num() && AllViewportClients[StartView] && NewPIEWorld->GetMainCamera())
	{
		FEditorViewportClient* Source = AllViewportClients[StartView];
		UCameraComponent* Camera = NewPIEWorld->GetMainCamera()->GetCameraComponent();
		Camera->SetWorldLocationAndRotation(Source->GetViewLocation(), Source->GetViewRotation(), false);
		Camera->SetFieldOfView(Source->GetViewFOV());
		Camera->SetOrthoWidth(Source->GetOrthoWidth());
		Camera->SetIsOrthogonal(!Source->IsPerspective());
	}
	PIEContext.SetCurrentWorld(NewPIEWorld);
	return NewPIEWorld;
}

void UEditorEngine::EndPlayMap()
{
	if (!PlayWorld)
		return;
	EndPIEMouseCapture();
	bIsPaused = false;
	bStepRequested = false;
	bIsSimulatingInEditor = false;

	ResetSceneSelection();
	if (ViewportsPanel)
		ViewportsPanel->ClearGameViewportClient();
	DestroyWorldContext(PlayWorld);
	PlayWorld = nullptr;
	PIEViewIndex = InvalidViewIndex;
	PIEStartEditorViewIndex = InvalidViewIndex;
	DestroyPIEWindow();
	OnActiveWorldChanged();
}

bool UEditorEngine::CreatePIEWindow()
{
	constexpr int32 DefaultWidth = 1280;
	constexpr int32 DefaultHeight = 720;

	TUniquePtr<FWindow> NewWindow = MakeUnique<FWindow>();
	if (!NewWindow->Create(::GetModuleHandleW(nullptr), DefaultWidth, DefaultHeight, L"Hitori Engine - PIE"))
	{
		HTR_LOG(Error, "Failed to create PIE window. Falling back to the selected viewport.");
		return false;
	}
	// 닫기 버튼은 PIE만 끝내고, ImGui는 메인 창 입력만 받도록 훅을 끊는다.
	NewWindow->SetQuitOnClose(false);
	NewWindow->SetReceivesWndProcHook(false);

	PIEWindow = std::move(NewWindow);
	PIESwapchain = MakeUnique<FSwapchain>(GetEngineLoop().GetRenderDevice(), PIEWindow.get());
	PIETargetWidth = 0;
	PIETargetHeight = 0;
	PIEWindow->Show();
	return true;
}

void UEditorEngine::DestroyPIEWindow()
{
	if (!PIEWindow)
		return;

	PIETargetWidth = 0;
	PIETargetHeight = 0;
	bPIEWindowRendered = false;

	// Swapchain이 창 핸들을 출력 대상으로 쥐고 있으므로 창보다 먼저 놓는다.
	PIESwapchain.reset();
	PIEWindow->Destroy();
	PIEWindow.reset();

	// PIE 창이 포커스를 갖고 있었다면 에디터로 돌려준다.
	::SetForegroundWindow(MainWindow->GetHandle());
}

bool UEditorEngine::UpdatePIEWindowTargets()
{
	const uint32 Width = PIEWindow->GetWidth();
	const uint32 Height = PIEWindow->GetHeight();
	// 최소화 중에는 그릴 대상이 없다.
	if (Width == 0 || Height == 0 || ::IsIconic(PIEWindow->GetHandle()))
	{
		return false;
	}

	if (PIEWindow->CheckResized())
		PIESwapchain->Resize(Width, Height);

	if (Width != PIETargetWidth || Height != PIETargetHeight)
	{
		FWorldContext* Context = GetPIEWorldContext();
		if (!Context || !Context->GameViewport) return false;
		Context->GameViewport->Resize(Width, Height);
		if (!Context->GameViewport->GetViewTargets().IsValidFor(Width, Height))
		{
			return false;
		}

		PIETargetWidth = Width;
		PIETargetHeight = Height;
	}

	return true;
}

void UEditorEngine::CopyPIEWindowToBackbuffer()
{
	if (!PIESwapchain)
		return;

	FTexture2D* Backbuffer = PIESwapchain->GetBackbuffer();
	FWorldContext* Context = GetPIEWorldContext();
	FTexture2D* PIEColorTarget = Context && Context->GameViewport
		? Context->GameViewport->GetColorTarget() : nullptr;
	if (!Backbuffer || !PIEColorTarget ||
		Backbuffer->GetWidth() != PIETargetWidth ||
		Backbuffer->GetHeight() != PIETargetHeight)
		return;

	RenderCommand::CopyTexture(Backbuffer, PIEColorTarget);
	bPIEWindowRendered = true;
}

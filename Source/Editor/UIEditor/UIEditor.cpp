#include "EnginePCH.h"
#include "Editor/UIEditor/UIEditor.h"
#include "Editor/UIEditor/UIEditorTab.h"

#include "Editor/HitoriEd/EditorEngine.h"
#include "Asset/AssetUtils.h"
#include "Render/RenderCommand.h"
#include "UI/UIRenderer.h"
#include "UI/UserWidget.h"
#include "UI/WidgetSerializer.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

namespace
{
	UPanelWidget* MakeDefaultRoot()
	{
		UCanvasPanel* NewRoot = NewObject<UCanvasPanel>();
		NewRoot->WidgetName = "Root";
		NewRoot->Anchors = FVector4(0.0f, 0.0f, 1.0f, 1.0f);   // 화면 전체
		NewRoot->Offsets = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		NewRoot->Visibility = 0;   // HitTestInvisible이면 자식 버튼이 클릭을 못 받는다
		return NewRoot;
	}
}

FUIEditor::FUIEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager, const FString& InAssetPath)
	: FAssetEditor(InEditorEngine, InEditorManager, InAssetPath)
{
	// 에디터 키는 Assets 상대 경로다. 파일 입출력과 런타임 캐시는 "Assets/UI/Foo.widget" 형식을 쓰므로 프로젝트 상대 경로로 되돌린다.
	const FString Absolute = FAssetUtils::ResolveAssetFilePath(InAssetPath);
	if (!Absolute.empty())
	{
		FilePath = std::filesystem::path(Absolute)
			.lexically_relative(std::filesystem::current_path()).generic_string();
	}
	if (FilePath.empty())
	{
		FilePath = Absolute.empty() ? InAssetPath : Absolute;
	}
}

bool FUIEditor::IsWidgetPath(const FString& Path)
{
	FString Extension = std::filesystem::path(Path).extension().string();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(),
		[](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
	return Extension == ".widget";
}

bool FUIEditor::CreateDefaultAsset(const FString& InFilePath)
{
	FWidgetAssetData Asset;
	Asset.Root = MakeDefaultRoot();
	const bool bSaved = FWidgetSerializer::SaveAsset(Asset, InFilePath);
	delete Asset.Root;

	if (!bSaved)
	{
		HTR_LOG(Error, "Cannot create widget asset: {}", InFilePath);
		return false;
	}

	FWidgetAssetCache::Invalidate(InFilePath);
	return true;
}

TUniquePtr<IEditorTab> FUIEditor::CreateTab()
{
	return MakeUnique<FUIEditorTab>(*this);
}

bool FUIEditor::InitializeResources()
{
	TArray<uint8> Bytes;
	FWidgetAssetData Asset;
	if (!FWidgetSerializer::ReadFileBytes(FilePath, Bytes) || !FWidgetSerializer::LoadAssetFromBytes(Bytes, Asset) || !Asset.Root)
	{
		HTR_LOG(Error, "Cannot open widget asset: {}", FilePath);
		delete Asset.Root;
		return false;
	}

	Root = Asset.Root;
	Selected = nullptr;
	ParentClassName = Asset.ParentClass;
	PreviewSize = Asset.DesignSize;

	UndoStack.Reset();
	RedoStack.Reset();
	LastSnapshot = Snapshot();
	bDirty = false;
	return true;
}

void FUIEditor::ShutdownResources()
{
	delete Root;   // 패널이면 자식도 함께 지워진다
	Root = nullptr;
	Selected = nullptr;
	PendingReparent = {};
	UndoStack.Reset();
	RedoStack.Reset();

	FAssetEditor::ShutdownResources();
}

void FUIEditor::OnRenderPreview()
{
	// 캔버스가 보이지 않은 프레임은 그리지 않는다
	const bool bRender = bCanvasVisible;
	bCanvasVisible = false;
	if (!bRender || !Root)
	{
		return;
	}

	FUIRenderer* Renderer = EditorEngine.GetUIRenderer();
	if (!Renderer)
	{
		return;
	}

	const uint32 Width = static_cast<uint32>(PreviewSize.X);
	const uint32 Height = static_cast<uint32>(PreviewSize.Y);
	if (!PreviewTargets.Resize(Width, Height))
	{
		return;
	}

	DrawList.Reset();
	Root->Arrange({ FUIRect{ 0.0f, 0.0f, PreviewSize.X, PreviewSize.Y }, 1.0f });
	Root->Paint(DrawList, 1.0f);

	FTexture2D* Color = PreviewTargets.SceneColor.get();
	RenderCommand::BeginRenderPass(Color, PreviewTargets.Depth.get(), Width, Height);
	const float Clear[4] = { 0.10f, 0.12f, 0.15f, 1.0f };
	RenderCommand::ClearRenderTarget(Color, Clear);
	Renderer->Render(DrawList, Width, Height);
	RenderCommand::EndRenderPass();
}

void FUIEditor::Save()
{
	if (!Root || FilePath.empty())
	{
		return;
	}

	FWidgetAssetData Asset;
	Asset.ParentClass = ParentClassName;
	Asset.DesignSize = FVector2(1920.0f, 1080.0f);   // 레이아웃 기준은 항상 1080 높이
	Asset.Root = Root;
	if (!FWidgetSerializer::SaveAsset(Asset, FilePath))
	{
		HTR_LOG(Error, "Cannot save widget asset: {}", FilePath);
		return;
	}

	FWidgetAssetCache::Invalidate(FilePath);   // 다음 CreateWidget이 새 파일을 읽는다
	bDirty = false;
}

void FUIEditor::SetParentClassName(const FString& Name)
{
	if (ParentClassName != Name)
	{
		ParentClassName = Name;
		bDirty = true;
	}
}

UWidget* FUIEditor::AddWidget(UClass* WidgetClass)
{
	if (!Root || !WidgetClass)
	{
		return nullptr;
	}

	UPanelWidget* Target = Cast<UPanelWidget>(Selected);
	if (!Target)
	{
		Target = (Selected && Selected->GetParent()) ? Selected->GetParent() : Root;
	}

	UWidget* NewWidget = NewObject<UWidget>(nullptr, WidgetClass);
	NewWidget->WidgetName = WidgetClass->Name.substr(1) + std::to_string(NewWidget->GetUUID());
	NewWidget->Offsets.X = 40.0f;
	NewWidget->Offsets.Y = 40.0f;
	Target->AddChild(NewWidget);
	Selected = NewWidget;
	return NewWidget;
}

void FUIEditor::SetAnchorsKeepRect(UWidget* Widget, const FVector4& NewAnchors)
{
	const FUIRect R = Widget->GetCachedRect();
	const FUIRect P = Widget->GetParent() ? Widget->GetParent()->GetCachedRect()
		: FUIRect{ 0.0f, 0.0f, PreviewSize.X, PreviewSize.Y };

	Widget->Anchors = NewAnchors;
	const float A0X = P.X + P.W * NewAnchors.X, A1X = P.X + P.W * NewAnchors.Z;
	const float A0Y = P.Y + P.H * NewAnchors.Y, A1Y = P.Y + P.H * NewAnchors.W;

	if (NewAnchors.X == NewAnchors.Z) { Widget->Offsets.X = R.X - A0X + R.W * Widget->AlignmentX; Widget->Offsets.Z = R.W; }
	else { Widget->Offsets.X = R.X - A0X; Widget->Offsets.Z = A1X - (R.X + R.W); }
	if (NewAnchors.Y == NewAnchors.W) { Widget->Offsets.Y = R.Y - A0Y + R.H * Widget->AlignmentY; Widget->Offsets.W = R.H; }
	else { Widget->Offsets.Y = R.Y - A0Y; Widget->Offsets.W = A1Y - (R.Y + R.H); }
}

void FUIEditor::DeleteSelected()
{
	if (!Selected || Selected == Root)
	{
		return;
	}

	UWidget* Target = Selected;
	Selected = Target->GetParent();
	PendingReparent = {};   // 지워질 위젯을 가리키고 있을 수 있다
	Target->RemoveFromParent();
	delete Target;   // 패널이면 자식도 함께 지워진다
}

void FUIEditor::HandleShortcuts()
{
	if (!Root || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsAnyItemActive())
	{
		return;
	}

	const ImGuiIO& IO = ImGui::GetIO();
	if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) Save();
	if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
	if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) Redo();
	if (ImGui::IsKeyPressed(ImGuiKey_Delete)) DeleteSelected();
}

void FUIEditor::FlushPendingReparent()
{
	if (PendingReparent.Widget && PendingReparent.NewParent)
	{
		PendingReparent.NewParent->AddChild(PendingReparent.Widget);
	}
	PendingReparent = {};
}

FString FUIEditor::Snapshot() const
{
	// 되돌리기는 위젯 트리만 다룬다. 부모 클래스와 미리보기 크기는 기본값으로 둔다
	FWidgetAssetData Asset;
	Asset.Root = Root;
	TArray<uint8> Bytes;
	FWidgetSerializer::SaveAssetToBytes(Asset, Bytes);
	return FString(Bytes.begin(), Bytes.end());
}

void FUIEditor::RestoreSnapshot(const FString& Data)
{
	const FString SelectedName = Selected ? Selected->WidgetName : FString();
	PendingReparent = {};
	delete Root;

	TArray<uint8> Bytes;
	Bytes.SetNum(static_cast<int32>(Data.size()));
	std::memcpy(Bytes.GetData(), Data.data(), Data.size());

	FWidgetAssetData Asset;
	FWidgetSerializer::LoadAssetFromBytes(Bytes, Asset);
	Root = Asset.Root;
	Selected = Root ? (Root->WidgetName == SelectedName ? Root : Root->FindWidget(SelectedName)) : nullptr;
}

void FUIEditor::CommitUndoIfChanged()
{
	// 편집이 끝날 때까지 기다린다. 캔버스 패널이 드래그 도중 닫혀도 멈추지 않도록 마우스 상태도 함께 본다.
	const bool bDragging = bCanvasDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left);
	if (!Root || ImGui::IsAnyItemActive() || bDragging)
	{
		return;
	}

	FString Now = Snapshot();
	if (Now == LastSnapshot)
	{
		return;
	}

	UndoStack.Add(LastSnapshot);
	RedoStack.Reset();
	LastSnapshot = std::move(Now);
	bDirty = true;
}

void FUIEditor::Undo()
{
	if (UndoStack.IsEmpty())
	{
		return;
	}

	RedoStack.Add(LastSnapshot);
	LastSnapshot = UndoStack.Last();
	UndoStack.RemoveLast();
	RestoreSnapshot(LastSnapshot);
	bDirty = true;
}

void FUIEditor::Redo()
{
	if (RedoStack.IsEmpty())
	{
		return;
	}

	UndoStack.Add(LastSnapshot);
	LastSnapshot = RedoStack.Last();
	RedoStack.RemoveLast();
	RestoreSnapshot(LastSnapshot);
	bDirty = true;
}

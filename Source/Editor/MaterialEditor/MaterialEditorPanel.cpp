#include "EnginePCH.h"
#include "Editor/MaterialEditor/MaterialEditorPanel.h"

#include "Editor/EditorUI/EditorUIScale.h"
#include "Editor/HitoriEd/EditorDragDrop.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"

#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxPath.h"

#include "Render/Material.h"
#include "Render/MaterialInstance.h"
#include "Render/MaterialRenderProxy.h"
#include "Render/Mesh.h"
#include "Render/Texture2D.h"
#include "Render/SkyboxRenderer.h"

#include "UObject/UObjectIterator.h"

#include "Engine/World.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "GameFramework/Actor/SkyLightActor.h"

#include "Components/StaticMeshComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace
{
	const FMaterialExpressionNode* FindMaterialNode(const UMaterial& Material, int32 NodeId)
	{
		for (const FMaterialExpressionNode& Node : Material.ExpressionNodes)
		{
			if (Node.Id == NodeId)
			{
				return &Node;
			}
		}

		return nullptr;
	}

	const char* GetMaterialExpressionName(EMaterialExpressionType Type)
	{
		switch (Type)
		{
		case EMaterialExpressionType::Constant:
			return "Constant";
		case EMaterialExpressionType::Constant2Vector:
			return "Constant2Vector";
		case EMaterialExpressionType::Constant3Vector:
			return "Constant3Vector";
		case EMaterialExpressionType::Constant4Vector:
			return "Constant4Vector";
		case EMaterialExpressionType::ScalarParameter:
			return "Scalar Parameter";
		case EMaterialExpressionType::VectorParameter:
			return "Vector Parameter";
		case EMaterialExpressionType::TextureSample:
			return "Texture Sample";
		case EMaterialExpressionType::TextureCoordinate:
			return "Texture Coordinate";
		case EMaterialExpressionType::Add:
			return "Add";
		case EMaterialExpressionType::Multiply:
			return "Multiply";
		case EMaterialExpressionType::Lerp:
			return "Lerp";
		case EMaterialExpressionType::OneMinus:
			return "One Minus";
		case EMaterialExpressionType::MaterialOutput:
			return "Material Output";
		default:
			return "Unknown";
		}
	}

	ImVec2 GetMaterialNodeSize(const UMaterial& Material, const FMaterialExpressionNode& Node)
	{
		const float Height = std::max(76.0f, 48.0f + Material.GetExpressionInputPinCount(Node.Type) * 25.0f);
		return ImVec2(205.0f, Height);
	}

	ImVec2 GetMaterialInputPinPosition(ImVec2 Min, int32 PinIndex, float Scale)
	{
		return ImVec2(Min.x, Min.y + (43.0f + PinIndex * 25.0f) * Scale);
	}

	ImVec2 GetMaterialOutputPinPosition(ImVec2 Min, ImVec2 Size, float Scale)
	{
		return ImVec2(Min.x + Size.x * Scale, Min.y + 43.0f * Scale);
	}

	bool EditMaterialColor(const char* Label, float* Value, bool bAlpha = false)
	{
		float Display[4] = {
			std::pow(std::max(Value[0], 0.0f), 1.0f / 2.2f),
			std::pow(std::max(Value[1], 0.0f), 1.0f / 2.2f),
			std::pow(std::max(Value[2], 0.0f), 1.0f / 2.2f),
			bAlpha ? Value[3] : 1.0f
		};

		const bool bChanged = bAlpha
			? ImGui::ColorEdit4(Label, Display, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float)
			: ImGui::ColorEdit3(Label, Display, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);

		if (!bChanged)
		{
			return false;
		}

		for (int32 Index = 0; Index < 3; ++Index)
		{
			Value[Index] = std::pow(std::max(Display[Index], 0.0f), 2.2f);
		}

		if (bAlpha)
		{
			Value[3] = Display[3];
		}
		return true;
	}

	float DistanceSquaredToSegment(const ImVec2& Point, const ImVec2& Start, const ImVec2& End)
	{
		const float DX = End.x - Start.x;
		const float DY = End.y - Start.y;
		const float LengthSquared = DX * DX + DY * DY;
		const float T = LengthSquared > 0.0f ? std::clamp(((Point.x - Start.x) * DX + (Point.y - Start.y) * DY) / LengthSquared, 0.0f, 1.0f) : 0.0f;
		const float ClosestX = Start.x + DX * T;
		const float ClosestY = Start.y + DY * T;
		const float DiffX = Point.x - ClosestX;
		const float DiffY = Point.y - ClosestY;
		return DiffX * DiffX + DiffY * DiffY;
	}

	bool IsPointNearBezier(const ImVec2& Point, const ImVec2& Start, const ImVec2& Control1, const ImVec2& Control2, const ImVec2& End, float Radius)
	{
		ImVec2 Previous = Start;

		for (int32 Step = 1; Step <= 24; ++Step)
		{
			const float T = static_cast<float>(Step) / 24.0f;
			const float U = 1.0f - T;
			const ImVec2 Current(
				U * U * U * Start.x + 3.0f * U * U * T * Control1.x + 3.0f * U * T * T * Control2.x + T * T * T * End.x,
				U * U * U * Start.y + 3.0f * U * U * T * Control1.y + 3.0f * U * T * T * Control2.y + T * T * T * End.y);

			if (DistanceSquaredToSegment(Point, Previous, Current) <= Radius * Radius)
			{
				return true;
			}

			Previous = Current;
		}

		return false;
	}
}

FMaterialEditorPanel::FMaterialEditorPanel() = default;

FMaterialEditorPanel::~FMaterialEditorPanel()
{
	Release();
}

void FMaterialEditorPanel::Release()
{
	Client.SetWorld(nullptr);
	Skybox.reset();

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

	Asset = nullptr;
}

void FMaterialEditorPanel::NewMaterial(const FString& InPath, bool bParticleSprite)
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

	UMaterial* New = NewObject<UMaterial>();

	if (!New)
	{
		Status = "Failed to create Material.";
		SetOpen(true);
		return;
	}

	New->CopySettingsFrom(*Template);
	New->EnsureDefaultGraph();

	Asset = New;
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

void FMaterialEditorPanel::NewMaterialInstance(const FString& InPath, const FString& ParentPath)
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

	UMaterialInterface* Parent = UAssetManager::LoadMaterialAsset(ParentPath);

	if (!Parent)
	{
		Status = "Parent Material not found.";
		SetOpen(true);
		return;
	}

	UMaterialInstanceConstant* New = NewObject<UMaterialInstanceConstant>();

	if (!New)
	{
		Status = "Failed to create Material Instance.";
		SetOpen(true);
		return;
	}

	New->SetParent(Parent);
	Asset = New;
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

void FMaterialEditorPanel::OpenAsset(const FString& InPath)
{
	if (bDirty && Asset && Path != InPath)
	{
		SaveMaterial();
		if (bDirty)
		{
			return;
		}
	}

	UMaterialInterface* Loaded = UAssetManager::LoadMaterialAsset(InPath);

	if (!Loaded)
	{
		Status = "Cannot load Material: " + InPath;
		SetOpen(true);
		return;
	}

	Release();

	Path = InPath;
	Asset = Loaded;
	bDirty = false;
	Status.clear();
	SetOpen(true);

	if (UMaterial* Material = Asset->GetBaseMaterial())
	{
		Material->EnsureDefaultGraph();
		SelectedNodeId = Material->OutputNodeId;
	}

	const EPSOType Type = Asset->GetPSOType();

	if (Type == EPSOType::Particle_Sprite || Type == EPSOType::Particle_AlphaBlend || Type == EPSOType::Particle_Additive)
	{
		return;
	}

	UStaticMesh* Mesh = UAssetManager::GetAssetByPath<UStaticMesh>("Sphere");

	if (!Mesh)
	{
		Status = "Preview Sphere mesh not found.";
		return;
	}

	PreviewWorld = UWorld::CreateWorld(EWorldType::Editor, false);

	if (!PreviewWorld)
	{
		return;
	}

	AStaticMeshActor* Actor = PreviewWorld->SpawnActor<AStaticMeshActor>();

	if (!Actor)
	{
		return;
	}

	Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
	Actor->GetStaticMeshComponent()->SetMaterial(0, Asset);

	auto* Sun = PreviewWorld->SpawnActor<ADirectionalLightActor>();

	if (Sun)
	{
		Sun->GetBillboardComponent()->SetVisibility(false);
		Sun->GetArrowComponent()->SetVisibility(false);
		Sun->GetLightComponent()->SetRelativeRotation(FRotator(40.0f, 150.0f, 0.0f));
		Sun->GetLightComponent()->SetIntensity(1.0f);
	}

	auto* Sky = PreviewWorld->SpawnActor<ASkyLightActor>();

	if (Sky)
	{
		Sky->GetBillboardComponent()->SetVisibility(false);
		Sky->GetLightComponent()->SetIntensity(0.3f);
	}

	Client.SetWorld(PreviewWorld);
	Client.Resize(512, 512);
	Client.SetActive(true);
	Client.SetViewRotation(FRotator(15.0f, 160.0f, 0.0f));

	const auto Bounds = Mesh->GetRenderBounds();

	Client.FocusOnBounds((Bounds.Min + Bounds.Max) * 0.5f, std::max(0.1f, (Bounds.Max - Bounds.Min).Length() * 0.5f));

	Skybox = MakeUnique<FSkyboxRenderer>();

	if (!Skybox->Init(EditorResourcePaths::AssetViewerSkyPanorama))
	{
		Skybox.reset();
	}
}

void FMaterialEditorPanel::SaveMaterial()
{
	if (!Asset)
	{
		Status = "No Material is open.";
		return;
	}

	FString ValidationError;

	if (UMaterial* BaseMaterial = Asset->GetBaseMaterial())
	{
		if (!BaseMaterial->ValidateExpressionGraph(ValidationError))
		{
			Status = "Cannot save: " + ValidationError;
			return;
		}
	}

	if (!UAssetManager::SaveMaterialAsset(Path, *Asset))
	{
		Status = "Failed to save Material.";
		return;
	}

	bDirty = false;
	Status = "Saved";

	FContentDrawerPanel::InvalidateMaterialThumbnail(Path);
}

void FMaterialEditorPanel::SaveIfDirty()
{
	if (IsOpen() && bDirty)
	{
		SaveMaterial();
	}
}

void FMaterialEditorPanel::Tick(float DeltaTime)
{
	if (Asset && Asset->IsRetired())
	{
		const bool bWasOpen = IsOpen();
		const FString ReopenPath = Path;
		OpenAsset(ReopenPath);
		SetOpen(bWasOpen);
	}

	if (PreviewWorld)
	{
		Client.TickInput(DeltaTime, false, false, 0, 4.0f * CameraSpeed, 1.0f);
	}
}

void FMaterialEditorPanel::RenderPreview(UEditorEngine* Engine)
{
	if (Engine && IsOpen() && bVisible && PreviewWorld)
	{
		Engine->RenderPreviewViewport(&Client, PreviewWorld, nullptr, Skybox.get(), true);
	}
}

void FMaterialEditorPanel::AddMaterialExpression(UMaterial& Material, EMaterialExpressionType Type, float X, float Y)
{
	if (Type == EMaterialExpressionType::MaterialOutput)
	{
		return;
	}

	int32 NewId = 1;

	for (const FMaterialExpressionNode& Node : Material.ExpressionNodes)
	{
		NewId = std::max(NewId, Node.Id + 1);
	}

	FMaterialExpressionNode Node;
	Node.Id = NewId;
	Node.Type = Type;
	Node.Position = FVector2(X, Y);

	if (Type == EMaterialExpressionType::ScalarParameter)
	{
		Node.ParameterName = "ScalarParameter_" + std::to_string(NewId);
	}

	if (Type == EMaterialExpressionType::VectorParameter)
	{
		Node.ParameterName = "VectorParameter_" + std::to_string(NewId);
	}

	Material.ExpressionNodes.Add(Node);

	SelectedNodeId = NewId;
	bDirty = true;
}

void FMaterialEditorPanel::DrawMaterialGraphPalette(UMaterial& Material)
{
	ImGui::TextUnformatted("Palette");
	ImGui::Separator();

	ImGui::InputText("Search", SearchText, sizeof(SearchText));

	const auto AddItem = [&](const char* Name, EMaterialExpressionType Type)
	{
		if (SearchText[0] && std::strstr(Name, SearchText) == nullptr)
		{
			return;
		}

		ImGui::PushID(static_cast<int32>(Type));

		if (ImGui::Selectable(Name))
		{
			const int32 Count = Material.ExpressionNodes.Num();
			AddMaterialExpression(Material, Type, 100.0f + (Count % 3) * 220.0f, 100.0f + (Count / 3) * 130.0f);
		}

		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload("MATERIAL_EXPRESSION", &Type, sizeof(Type));
			ImGui::TextUnformatted(Name);
			ImGui::EndDragDropSource();
		}

		ImGui::PopID();
	};

	if (ImGui::TreeNodeEx("Constants", ImGuiTreeNodeFlags_DefaultOpen))
	{
		AddItem("Constant", EMaterialExpressionType::Constant);
		AddItem("Constant2Vector", EMaterialExpressionType::Constant2Vector);
		AddItem("Constant3Vector", EMaterialExpressionType::Constant3Vector);
		AddItem("Constant4Vector", EMaterialExpressionType::Constant4Vector);
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
	{
		AddItem("Scalar Parameter", EMaterialExpressionType::ScalarParameter);
		AddItem("Vector Parameter", EMaterialExpressionType::VectorParameter);
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Textures", ImGuiTreeNodeFlags_DefaultOpen))
	{
		AddItem("Texture Sample", EMaterialExpressionType::TextureSample);
		AddItem("Texture Coordinate", EMaterialExpressionType::TextureCoordinate);
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Math", ImGuiTreeNodeFlags_DefaultOpen))
	{
		AddItem("Add", EMaterialExpressionType::Add);
		AddItem("Multiply", EMaterialExpressionType::Multiply);
		AddItem("Lerp", EMaterialExpressionType::Lerp);
		AddItem("One Minus", EMaterialExpressionType::OneMinus);
		ImGui::TreePop();
	}

	ImGui::TextDisabled("Material Output is created automatically.");
}

void FMaterialEditorPanel::DrawMaterialGraphCanvas(UMaterial& Material)
{
	ImGui::TextUnformatted("Material Graph");

	const ImVec2 Size = ImGui::GetContentRegionAvail();
	const ImVec2 Origin = ImGui::GetCursorScreenPos();

	ImGui::InvisibleButton("##MaterialGraphCanvas", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

	const bool bHovered = ImGui::IsItemHovered();
	const ImVec2 Mouse = ImGui::GetMousePos();
	const float Scale = EditorUIScale::Px(1.0f) * GraphZoom;

	ImDrawList* Draw = ImGui::GetWindowDrawList();
	Draw->PushClipRect(Origin, { Origin.x + Size.x, Origin.y + Size.y }, true);

	Draw->AddRectFilled(Origin, { Origin.x + Size.x, Origin.y + Size.y }, IM_COL32(27, 29, 33, 255));

	const float Grid = 60.0f * GraphZoom;

	for (float X = std::fmod(GraphPanX, Grid); X < Size.x; X += Grid)
	{
		Draw->AddLine({ Origin.x + X, Origin.y }, { Origin.x + X, Origin.y + Size.y }, IM_COL32(43, 45, 50, 255));
	}

	for (float Y = std::fmod(GraphPanY, Grid); Y < Size.y; Y += Grid)
	{
		Draw->AddLine({ Origin.x, Origin.y + Y }, { Origin.x + Size.x, Origin.y + Y }, IM_COL32(43, 45, 50, 255));
	}

	const auto ToScreen = [&](float X, float Y)
	{
		return ImVec2(Origin.x + GraphPanX + X * Scale, Origin.y + GraphPanY + Y * Scale);
	};

	const auto FindNodePosition = [&](int32 Id) -> const FMaterialExpressionNode*
	{
		return FindMaterialNode(Material, Id);
	};

	bool bHandledClick = false;

	for (const FMaterialExpressionConnection& Connection : Material.ExpressionConnections)
	{
		const FMaterialExpressionNode* Output = FindNodePosition(Connection.OutputNodeId);
		const FMaterialExpressionNode* Input = FindNodePosition(Connection.InputNodeId);

		if (!Output || !Input)
		{
			continue;
		}

		const ImVec2 OutputMin = ToScreen(Output->Position.X, Output->Position.Y);
		const ImVec2 InputMin = ToScreen(Input->Position.X, Input->Position.Y);

		const ImVec2 OutputSize = GetMaterialNodeSize(Material, *Output);
		const ImVec2 InputSize = GetMaterialNodeSize(Material, *Input);

		const ImVec2 A = GetMaterialOutputPinPosition(OutputMin, OutputSize, Scale);
		const ImVec2 B = GetMaterialInputPinPosition(InputMin, Connection.InputPin, Scale);
		const ImVec2 Control1(A.x + 70.0f * Scale, A.y);
		const ImVec2 Control2(B.x - 70.0f * Scale, B.y);

		const bool bSelectedConnection =
			SelectedConnectionOutputNodeId == Connection.OutputNodeId &&
			SelectedConnectionOutputPin == Connection.OutputPin &&
			SelectedConnectionInputNodeId == Connection.InputNodeId &&
			SelectedConnectionInputPin == Connection.InputPin;

		Draw->AddBezierCubic(A, Control1, Control2, B,
			bSelectedConnection ? IM_COL32(255, 190, 90, 255) : IM_COL32(180, 190, 205, 255),
			bSelectedConnection ? 3.0f : 2.0f);

		if (bHovered && !bHandledClick && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
			IsPointNearBezier(Mouse, A, Control1, Control2, B, EditorUIScale::Px(8.0f)))
		{
			SelectedConnectionOutputNodeId = Connection.OutputNodeId;
			SelectedConnectionOutputPin = Connection.OutputPin;
			SelectedConnectionInputNodeId = Connection.InputNodeId;
			SelectedConnectionInputPin = Connection.InputPin;

			SelectedNodeId = 0;
			DraggingNodeId = 0;
			bHandledClick = true;
		}
	}

	for (FMaterialExpressionNode& Node : Material.ExpressionNodes)
	{
		const ImVec2 Min = ToScreen(Node.Position.X, Node.Position.Y);
		const ImVec2 NodeSize = GetMaterialNodeSize(Material, Node);
		const ImVec2 Max(Min.x + NodeSize.x * Scale, Min.y + NodeSize.y * Scale);

		const bool bSelected = SelectedNodeId == Node.Id;

		Draw->AddRectFilled(Min, Max, IM_COL32(48, 53, 62, 255), 7.0f);
		Draw->AddRect(Min, Max, bSelected ? IM_COL32(255, 190, 90, 255) : IM_COL32(105, 112, 124, 255), 7.0f, ImDrawFlags_None, bSelected ? 2.0f : 1.0f);

		Draw->AddText({ Min.x + 10.0f, Min.y + 10.0f }, IM_COL32_WHITE, GetMaterialExpressionName(Node.Type));

		const int32 InputCount = Material.GetExpressionInputPinCount(Node.Type);

		for (int32 Pin = 0; Pin < InputCount; ++Pin)
		{
			const ImVec2 PinPosition = GetMaterialInputPinPosition(Min, Pin, Scale);
			const char* PinName = Material.GetExpressionInputPinName(Node.Type, Pin);

			Draw->AddCircleFilled(PinPosition, 5.0f * Scale, IM_COL32(130, 190, 230, 255));

			if (PinName)
			{
				Draw->AddText({ PinPosition.x + 10.0f * Scale, PinPosition.y - 7.0f * Scale }, IM_COL32(220, 220, 225, 255), PinName);
			}

			if (bDraggingConnection && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
				Mouse.x >= PinPosition.x - 10.0f * Scale && Mouse.x <= PinPosition.x + 10.0f * Scale &&
				Mouse.y >= PinPosition.y - 10.0f * Scale && Mouse.y <= PinPosition.y + 10.0f * Scale)
			{
				FString Error;

				if (Material.ConnectExpressionPins(PendingOutputNodeId, PendingOutputPin, Node.Id, Pin, Error))
				{
					SelectedConnectionOutputNodeId = PendingOutputNodeId;
					SelectedConnectionOutputPin = PendingOutputPin;
					SelectedConnectionInputNodeId = Node.Id;
					SelectedConnectionInputPin = Pin;
					SelectedNodeId = 0;

					bDirty = true;
					Status = "Connected.";
				}
				else
				{
					Status = Error;
				}

				bDraggingConnection = false;
				PendingOutputNodeId = 0;
				bHandledClick = true;
			}
		}

		if (Material.GetExpressionOutputPinCount(Node.Type) > 0)
		{
			const ImVec2 PinPosition = GetMaterialOutputPinPosition(Min, NodeSize, Scale);

			Draw->AddCircleFilled(PinPosition, 5.0f * Scale, IM_COL32(130, 190, 230, 255));

			if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
				Mouse.x >= PinPosition.x - 10.0f * Scale && Mouse.x <= PinPosition.x + 10.0f * Scale &&
				Mouse.y >= PinPosition.y - 10.0f * Scale && Mouse.y <= PinPosition.y + 10.0f * Scale)
			{
				PendingOutputNodeId = Node.Id;
				PendingOutputPin = 0;
				bDraggingConnection = true;
				bHandledClick = true;
			}
		}

		if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !bHandledClick &&
			Mouse.x >= Min.x && Mouse.x <= Max.x && Mouse.y >= Min.y && Mouse.y <= Max.y)
		{
			SelectedNodeId = Node.Id;
			DraggingNodeId = Node.Id;

			SelectedConnectionOutputNodeId = 0;
			SelectedConnectionOutputPin = 0;
			SelectedConnectionInputNodeId = 0;
			SelectedConnectionInputPin = 0;

			bHandledClick = true;
		}

		if (DraggingNodeId == Node.Id && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			Node.Position.X += ImGui::GetIO().MouseDelta.x / Scale;
			Node.Position.Y += ImGui::GetIO().MouseDelta.y / Scale;
			bDirty = true;
		}
	}

	if (DraggingNodeId != 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		DraggingNodeId = 0;
	}

	if (bDraggingConnection && ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		const FMaterialExpressionNode* Output = FindNodePosition(PendingOutputNodeId);

		if (Output)
		{
			const ImVec2 Min = ToScreen(Output->Position.X, Output->Position.Y);
			const ImVec2 NodeSize = GetMaterialNodeSize(Material, *Output);
			const ImVec2 Start = GetMaterialOutputPinPosition(Min, NodeSize, Scale);

			Draw->AddBezierCubic(Start, { Start.x + 70.0f, Start.y }, { Mouse.x - 70.0f, Mouse.y }, Mouse,
				IM_COL32(255, 205, 115, 255), 2.0f);
		}
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && bDraggingConnection)
	{
		bDraggingConnection = false;
		PendingOutputNodeId = 0;
	}

	if (bHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right))
	{
		GraphPanX += ImGui::GetIO().MouseDelta.x;
		GraphPanY += ImGui::GetIO().MouseDelta.y;
	}

	if (bHovered && ImGui::GetIO().MouseWheel != 0.0f)
	{
		GraphZoom = std::clamp(GraphZoom + ImGui::GetIO().MouseWheel * 0.1f, 0.5f, 2.0f);
	}

	if (bHovered && ImGui::IsKeyPressed(ImGuiKey_Escape))
	{
		bDraggingConnection = false;
		PendingOutputNodeId = 0;
	}

	if (bHovered && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete))
	{
		if (SelectedConnectionOutputNodeId != 0)
		{
			for (int32 Index = Material.ExpressionConnections.Num() - 1; Index >= 0; --Index)
			{
				const FMaterialExpressionConnection& Connection = Material.ExpressionConnections[Index];

				if (Connection.OutputNodeId == SelectedConnectionOutputNodeId &&
					Connection.OutputPin == SelectedConnectionOutputPin &&
					Connection.InputNodeId == SelectedConnectionInputNodeId &&
					Connection.InputPin == SelectedConnectionInputPin)
				{
					Material.ExpressionConnections.RemoveAt(Index);
					break;
				}
			}

			SelectedConnectionOutputNodeId = 0;
			SelectedConnectionOutputPin = 0;
			SelectedConnectionInputNodeId = 0;
			SelectedConnectionInputPin = 0;

			bDirty = true;
		}
		else if (SelectedNodeId != 0 && SelectedNodeId != Material.OutputNodeId)
		{
			for (int32 Index = Material.ExpressionConnections.Num() - 1; Index >= 0; --Index)
			{
				const FMaterialExpressionConnection& Connection = Material.ExpressionConnections[Index];

				if (Connection.OutputNodeId == SelectedNodeId || Connection.InputNodeId == SelectedNodeId)
				{
					Material.ExpressionConnections.RemoveAt(Index);
				}
			}

			for (int32 Index = Material.ExpressionNodes.Num() - 1; Index >= 0; --Index)
			{
				if (Material.ExpressionNodes[Index].Id == SelectedNodeId)
				{
					Material.ExpressionNodes.RemoveAt(Index);
				}
			}

			SelectedNodeId = 0;
			bDirty = true;
		}
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("MATERIAL_EXPRESSION"))
		{
			const auto Type = *static_cast<const EMaterialExpressionType*>(Payload->Data);

			const float X = (Mouse.x - Origin.x - GraphPanX) / Scale;
			const float Y = (Mouse.y - Origin.y - GraphPanY) / Scale;

			AddMaterialExpression(Material, Type, X, Y);
		}

		ImGui::EndDragDropTarget();
	}

	Draw->PopClipRect();
}

void FMaterialEditorPanel::DrawMaterialGraphDetails(UMaterial& Material)
{
	ImGui::TextUnformatted("Details");
	ImGui::Separator();

	FMaterialExpressionNode* Selected = nullptr;

	for (FMaterialExpressionNode& Node : Material.ExpressionNodes)
	{
		if (Node.Id == SelectedNodeId)
		{
			Selected = &Node;
		}
	}

	if (!Selected)
	{
		ImGui::TextUnformatted("Material Settings");

		if (EditMaterialColor("Base Color", &Material.BaseColor.X, true))
		{
			bDirty = true;
		}
		if (EditMaterialColor("Specular", &Material.SpecularColor.X))
		{
			bDirty = true;
		}
		if (ImGui::DragFloat("Specular Factor", &Material.SpecularFactor, 0.02f, 0.0f, 100.0f))
		{
			bDirty = true;
		}
		if (EditMaterialColor("Emissive", &Material.EmissiveColor.X))
		{
			bDirty = true;
		}
		if (ImGui::DragFloat("Emissive Factor", &Material.EmissiveFactor, 0.02f, 0.0f, 100.0f))
		{
			bDirty = true;
		}
		if (ImGui::DragFloat("Shininess", &Material.Shininess, 0.5f, 1.0f, 512.0f))
		{
			bDirty = true;
		}

		int Blend = Material.BlendState == EBlendState::AlphaBlend ? 1 : 0;

		if (ImGui::Combo("Blend", &Blend, "Opaque\0Alpha Blend\0"))
		{
			Material.SetBlendState(Blend ? EBlendState::AlphaBlend : EBlendState::Opaque);
			bDirty = true;
		}

		return;
	}

	ImGui::TextUnformatted(GetMaterialExpressionName(Selected->Type));
	ImGui::Separator();

	switch (Selected->Type)
	{
	case EMaterialExpressionType::Constant:
		if (ImGui::DragFloat("Value", &Selected->ScalarValue, 0.01f))
		{
			bDirty = true;
		}
		break;

	case EMaterialExpressionType::Constant2Vector:
		if (ImGui::DragFloat2("Value", &Selected->VectorValue.X, 0.01f))
		{
			bDirty = true;
		}
		break;

	case EMaterialExpressionType::Constant3Vector:
		if (EditMaterialColor("Value", &Selected->VectorValue.X))
		{
			bDirty = true;
		}
		break;

	case EMaterialExpressionType::Constant4Vector:
		if (EditMaterialColor("Value", &Selected->VectorValue.X, true))
		{
			bDirty = true;
		}
		break;

	case EMaterialExpressionType::ScalarParameter:
	case EMaterialExpressionType::VectorParameter:
	{
		char Name[128]{};
		strncpy_s(Name, sizeof(Name), Selected->ParameterName.c_str(), _TRUNCATE);

		if (ImGui::InputText("Parameter Name", Name, sizeof(Name)))
		{
			Selected->ParameterName = Name;
			bDirty = true;
		}

		char Group[128]{};
		strncpy_s(Group, sizeof(Group), Selected->ParameterGroup.c_str(), _TRUNCATE);

		if (ImGui::InputText("Group", Group, sizeof(Group)))
		{
			Selected->ParameterGroup = Group;
			bDirty = true;
		}

		if (Selected->Type == EMaterialExpressionType::ScalarParameter)
		{
			if (ImGui::DragFloat("Default Value", &Selected->ScalarValue, 0.01f))
			{
				bDirty = true;
			}
		}
		else if (EditMaterialColor("Default Value", &Selected->VectorValue.X, true))
		{
			bDirty = true;
		}

		break;
	}

	case EMaterialExpressionType::TextureSample:
	{
		UTexture2D* Current = Selected->TexturePath.empty() ? nullptr : UAssetManager::Get().LoadTexture(Selected->TexturePath);
		const FString CurrentName = Current ? Current->GetPath() : FString("None");

		if (ImGui::BeginCombo("Texture", CurrentName.c_str()))
		{
			if (ImGui::Selectable("None", Current == nullptr))
			{
				Selected->TexturePath.clear();
				bDirty = true;
			}

			for (TObjectIterator<UTexture2D> It; It; ++It)
			{
				UTexture2D* Texture = *It;

				if (!Texture || Texture->IsRetired() || Texture->GetPath().empty())
				{
					continue;
				}

				if (ImGui::Selectable(Texture->GetPath().c_str(), Current == Texture))
				{
					Selected->TexturePath = Texture->GetPath();
					bDirty = true;
				}
			}

			ImGui::EndCombo();
		}
		break;
	}

	case EMaterialExpressionType::TextureCoordinate:
		if (ImGui::DragInt("Coordinate Index", &Selected->CoordinateIndex, 1.0f, 0, 7))
		{
			bDirty = true;
		}
		break;

	default:
		ImGui::TextDisabled("This node has no editable properties.");
		break;
	}
}

void FMaterialEditorPanel::DrawPreview()
{
	ImGui::TextUnformatted("Preview");

	if (!PreviewWorld || !Client.GetColorTarget())
	{
		ImGui::TextDisabled("Preview is unavailable.");
		return;
	}

	const ImVec2 Size = ImGui::GetContentRegionAvail();

	Client.Resize(static_cast<uint32>(std::max(1.0f, Size.x)), static_cast<uint32>(std::max(1.0f, Size.y)));

	ImGui::Image(Client.GetColorTarget()->GetSRV(), Size);

	const bool Hovered = ImGui::IsItemHovered();
	const bool Captured = Hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);

	float Wheel = ImGui::GetIO().MouseWheel;

	if (Captured && Wheel != 0.0f)
	{
		CameraSpeed = std::clamp(CameraSpeed + Wheel * 0.5f, 0.1f, 20.0f);
		Wheel = 0.0f;
	}

	Client.TickInputFromImGui(ImGui::GetIO().DeltaTime, Captured, Hovered,
		static_cast<int32>(Wheel * 120), 4.0f * CameraSpeed, 0.2f);
}

void FMaterialEditorPanel::DrawMaterialGraph(UMaterial& Material)
{
	const ImVec2 Available = ImGui::GetContentRegionAvail();
	const float PaletteWidth = EditorUIScale::Px(190.0f);
	const float DetailsWidth = EditorUIScale::Px(280.0f);

	ImGui::BeginChild("MaterialPalette", { PaletteWidth, Available.y }, ImGuiChildFlags_Borders);
	DrawMaterialGraphPalette(Material);
	ImGui::EndChild();

	ImGui::SameLine(0, 0);

	ImGui::BeginChild("MaterialGraphCanvas", { std::max(1.0f, Available.x - PaletteWidth - DetailsWidth), Available.y }, ImGuiChildFlags_Borders);
	DrawMaterialGraphCanvas(Material);
	ImGui::EndChild();

	ImGui::SameLine(0, 0);

	ImGui::BeginChild("MaterialRightPanel", { DetailsWidth, Available.y }, ImGuiChildFlags_Borders);

	const float PreviewHeight = Available.y * 0.45f;

	ImGui::BeginChild("MaterialPreview", { 0.0f, PreviewHeight }, ImGuiChildFlags_Borders);
	DrawPreview();
	ImGui::EndChild();

	ImGui::BeginChild("MaterialNodeDetails", { 0.0f, 0.0f }, ImGuiChildFlags_Borders);
	DrawMaterialGraphDetails(Material);
	ImGui::EndChild();

	ImGui::EndChild();
}

void FMaterialEditorPanel::OnRender()
{
	const FString Title = (Asset ? FbxPath::ToString(FbxPath::FromString(Path).filename()) : FString(GetPanelName())) + "###MaterialEditor";

	bVisible = BeginPanel(Title.c_str());
	bFocused = bVisible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	if (!bVisible)
	{
		ImGui::End();
		return;
	}

	if (!Asset)
	{
		ImGui::TextUnformatted("No Material is open.");
		ImGui::End();
		return;
	}

	if (bFocused && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_S, false))
	{
		SaveMaterial();
	}

	ImGui::TextWrapped("%s", Path.c_str());

	if (ImGui::Button("Save"))
	{
		SaveMaterial();
	}

	ImGui::SameLine();

	if (ImGui::Button("Validate"))
	{
		if (UMaterial* Material = Cast<UMaterial>(Asset))
		{
			if (Material->ValidateExpressionGraph(Status))
			{
				Status = "Material Graph is valid.";
			}
		}
	}

	ImGui::SameLine();
	ImGui::TextUnformatted(bDirty ? "* Modified" : Status.c_str());

	ImGui::Separator();

	if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
		DrawMaterialGraph(*Material);
	}
	else if (UMaterialInstance* Instance = Cast<UMaterialInstance>(Asset))
	{
		ImGui::TextUnformatted("Material Instance");
		ImGui::Text("Parent: %s", Instance->GetParent() ? Instance->GetParent()->GetPath().c_str() : "None");

		FMaterialRenderProxy Proxy{};
		Asset->GetRenderProxy(Proxy);

		bool bOverrideColor = Instance->HasBaseColorOverride();

		if (ImGui::Checkbox("Override Base Color", &bOverrideColor))
		{
			if (bOverrideColor)
			{
				Instance->SetBaseColorOverride(Proxy.BaseColor);
			}
			else
			{
				Instance->ClearParameterOverride(FName("BaseColor"));
			}

			bDirty = true;
		}

		ImGui::BeginDisabled(!Instance->HasBaseColorOverride());

		FVector4 Color = Proxy.BaseColor;

		if (EditMaterialColor("Base Color", &Color.X, true))
		{
			Instance->SetBaseColorOverride(Color);
			bDirty = true;
		}

		ImGui::EndDisabled();

		UMaterial* ParentMaterial = Instance->GetBaseMaterial();

		if (ParentMaterial)
		{
			ImGui::Separator();
			ImGui::TextUnformatted("Parameters");

			std::set<FString> DrawnGroups;

			for (const FMaterialExpressionNode& ParameterNode : ParentMaterial->ExpressionNodes)
			{
				if (ParameterNode.Type != EMaterialExpressionType::ScalarParameter &&
					ParameterNode.Type != EMaterialExpressionType::VectorParameter)
				{
					continue;
				}

				const FString GroupName = ParameterNode.ParameterGroup.empty() ? FString("Default") : ParameterNode.ParameterGroup;

				if (!DrawnGroups.insert(GroupName).second)
				{
					continue;
				}

				if (!ImGui::TreeNodeEx(GroupName.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					continue;
				}

				for (const FMaterialExpressionNode& Node : ParentMaterial->ExpressionNodes)
				{
					if (Node.ParameterGroup != ParameterNode.ParameterGroup ||
						(Node.Type != EMaterialExpressionType::ScalarParameter && Node.Type != EMaterialExpressionType::VectorParameter))
					{
						continue;
					}

					const FName ParameterName(Node.ParameterName);
					const FMaterialParameterOverride* Existing = Instance->GetParameterOverride(ParameterName);
					const bool bScalar = Node.Type == EMaterialExpressionType::ScalarParameter;

					bool bOverride = Existing && Existing->Type == (bScalar ? EMaterialParameterType::Scalar : EMaterialParameterType::Vector);

					ImGui::PushID(Node.ParameterName.c_str());

					ImGui::TextUnformatted(Node.ParameterName.c_str());
					ImGui::SameLine();

					if (ImGui::Checkbox("Override", &bOverride))
					{
						if (bOverride)
						{
							if (bScalar)
							{
								Instance->SetScalarParameterValue(ParameterName, Node.ScalarValue);
							}
							else
							{
								Instance->SetVectorParameterValue(ParameterName, Node.VectorValue);
							}
						}
						else
						{
							Instance->ClearParameterOverride(ParameterName);
						}

						bDirty = true;
					}

					const FMaterialParameterOverride* CurrentOverride = Instance->GetParameterOverride(ParameterName);

					ImGui::BeginDisabled(!bOverride);

					if (bScalar)
					{
						float Value = CurrentOverride && CurrentOverride->Type == EMaterialParameterType::Scalar
							? CurrentOverride->ScalarValue
							: Node.ScalarValue;

						if (ImGui::DragFloat("Value", &Value, 0.01f))
						{
							Instance->SetScalarParameterValue(ParameterName, Value);
							bDirty = true;
						}
					}
					else
					{
						FVector4 Value = CurrentOverride && CurrentOverride->Type == EMaterialParameterType::Vector
							? CurrentOverride->VectorValue
							: Node.VectorValue;

						if (EditMaterialColor("Value", &Value.X, true))
						{
							Instance->SetVectorParameterValue(ParameterName, Value);
							bDirty = true;
						}
					}

					ImGui::EndDisabled();
					ImGui::PopID();
				}

				ImGui::TreePop();
			}
		}

		DrawPreview();
	}

	ImGui::End();
}
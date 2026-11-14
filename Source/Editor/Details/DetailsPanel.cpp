#include "EnginePCH.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Editor/EditorUI/EditorSort.h"
#include "Editor/Details/DetailsPanel.h"
#include "Editor/Details/PropertyEditor.h"

#include "imgui_internal.h"
#include "Editor/HitoriEd/EditorDragDrop.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "Components/TextRenderComponent.h"
#include "Components/ParticleSubUVComponent.h"
#include "Components/SpotLightComponent.h"
#include "Editor/EditorUI/ImGuiRenderer.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"

#include "Components/RotatingMovementComponent.h"
#include "Components/ProjectileMovementComponent.h"
#include "Components/DecalComponent.h"
#include "Components/ScriptComponent.h"
#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Render/Material.h"
#include "Render/MaterialInstance.h"
#include "Render/MaterialRenderProxy.h"
#include "Render/Texture2D.h"
#include "Text/Font.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/Actor.h"
#include "Core/EngineLog.h"

#include "Editor/HitoriEd/EditorFileUtils.h"
#include "Scripting/LuaScriptManager.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimGraphInstance.h"
#include "Asset/AnimGraphAsset.h"
#include "UObject/UObjectHash.h"


namespace
{
	bool EditLinearColor(const char* Label, float* Value, bool Alpha = false)
	{
		float Display[4] = { std::pow(std::max(Value[0], 0.0f), 1.0f / 2.2f),
			std::pow(std::max(Value[1], 0.0f), 1.0f / 2.2f), std::pow(std::max(Value[2], 0.0f), 1.0f / 2.2f), Alpha ? Value[3] : 1.0f };
		const float Previous[3] = { Display[0], Display[1], Display[2] };
		const auto Flags = ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float;
		if (!(Alpha ? ImGui::ColorEdit4(Label, Display, Flags) : ImGui::ColorEdit3(Label, Display, Flags))) return false;
		for (int I = 0; I < 3; ++I)
			if (Display[I] != Previous[I]) Value[I] = std::pow(std::max(Display[I], 0.0f), 2.2f);
		if (Alpha) Value[3] = Display[3];
		return true;
	}
	// Details 패널 드래그의 마우스 1픽셀당 변화량. 헤더의 Step 콤보로 바꾼다 (TEAM1).
	// 드래그 중 Shift는 10배, Alt는 1/100배(ImGui 기본)이고 Ctrl+클릭은 값을 직접 입력한다.
	float DragStep = 0.1f;
	constexpr float DragStepOptions[] = { 1.0f, 0.1f, 0.01f, 0.001f };
	constexpr const char* DragStepLabels[] = { "1", "0.1", "0.01", "0.001" };

	// 단계보다 한 자리 더 표시해 단계만큼 바뀐 값이 표시 반올림으로 사라지지 않게 한다.
	const char* GetDragFormat(const int32 MinDecimals = 2)
	{
		static constexpr const char* Formats[] = { "%.0f", "%.1f", "%.2f", "%.3f", "%.4f", "%.5f" };
		const int32 StepDecimals = static_cast<int32>(std::lround(-std::log10(DragStep)));
		const int32 Decimals = std::clamp(std::max(MinDecimals, StepDecimals + 1), 0, 5);
		return Formats[Decimals];
	}

	// 헤더 오른쪽의 드래그 단계 콤보
	void DrawDragStepCombo()
	{
		int32 Current = 1;
		for (int32 Index = 0; Index < IM_ARRAYSIZE(DragStepOptions); ++Index)
			if (std::abs(DragStepOptions[Index] - DragStep) < 1.0e-6f) Current = Index;
		ImGui::SetNextItemWidth(ImGui::CalcTextSize("0.001").x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f);
		if (ImGui::Combo("Drag Step", &Current, DragStepLabels, IM_ARRAYSIZE(DragStepLabels)))
			DragStep = DragStepOptions[Current];
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Value change per pixel while dragging. Shift = x10, Alt = x0.01, Ctrl+Click = type a value");
	}
	// 슬래시(/) 경로의 UTF-8 문자열. 씬에 저장되는 ScriptPath 형식(MakeScriptPath)과 맞춘다.
	FString ToGenericUtf8(const std::filesystem::path& Path)
	{
		const std::u8string U8 = Path.generic_u8string();
		return FString(U8.begin(), U8.end());
	}

	// 파일 이름 또는 경로를 Assets/Scripts 아래 경로로 바꾼다. 밖의 파일은 받지 않는다 (TEAM5).
	// 폴더 없이 이름만 주면 Assets/Scripts 안에서 같은 이름을 찾고, 없으면 Assets/Scripts 바로 아래로 정한다.
	FString ResolveScriptPath(const FString& Input)
	{
		namespace fs = std::filesystem;
		if (Input.empty())
		{
			HTR_LOG(Warning, "[Lua] Script file name cannot be empty");
			return {};
		}

		const fs::path ScriptRoot = FLuaScriptManager::ToPath(ContentPaths::ScriptFolder);
		fs::path Requested = FLuaScriptManager::ToPath(Input);
		if (Requested.extension() != ".lua")
			Requested.replace_extension(".lua");
		Requested = Requested.lexically_normal();

		std::error_code Ec;
		if (!Requested.has_parent_path())
		{
			for (fs::recursive_directory_iterator It(ScriptRoot, Ec), End; !Ec && It != End; It.increment(Ec))
			{
				if (It->is_regular_file(Ec) && It->path().filename() == Requested)
					return ToGenericUtf8(It->path().lexically_normal());
			}
			return ToGenericUtf8((ScriptRoot / Requested));
		}

		const fs::path AbsoluteRoot = fs::weakly_canonical(ScriptRoot, Ec);
		const fs::path AbsoluteFile = fs::weakly_canonical(Requested.is_absolute() ? Requested : fs::absolute(Requested, Ec), Ec);
		const fs::path Relative = AbsoluteFile.lexically_relative(AbsoluteRoot);
		if (Ec || Relative.empty() || *Relative.begin() == "..")
		{
			HTR_LOG(Warning, "[Lua] Script must be inside {}: {}", ContentPaths::ScriptFolder, Input);
			return {};
		}
		return ToGenericUtf8((ScriptRoot / Relative));
	}

	// 클래스별 접이식 헤더 안에 표시하는 Lua 스크립트 연결 UI.
	void DrawScriptSection(UScriptComponent* Script)
	{
		static UScriptComponent* EditingScript = nullptr;
		static FString EditingPath;
		static char FileNameBuffer[256] = {};

		// 다른 스크립트 컴포넌트를 고르거나 경로가 바뀌었을 때만 입력칸을 적용된 파일 이름으로 맞춘다.
		if (EditingScript != Script || EditingPath != Script->GetScriptPath())
		{
			EditingScript = Script;
			EditingPath = Script->GetScriptPath();
			const FString FileName = FLuaScriptManager::FromPath(FLuaScriptManager::ToPath(Script->GetScriptPath()).filename());
			strncpy_s(FileNameBuffer, sizeof(FileNameBuffer), FileName.c_str(), _TRUNCATE);
		}

		const auto Assign = [Script](const FString& Input)
		{
			const FString Resolved = ResolveScriptPath(Input);
			if (!Resolved.empty())
				Script->SetScriptPath(Resolved);   // UPROPERTY라 씬 저장 때 같이 저장된다
		};

		if (!Script->GetScriptPath().empty())
			ImGui::TextWrapped("File: %s", Script->GetScriptPath().c_str());
		else
			ImGui::TextDisabled("No script assigned.");

		const float ButtonsWidth = EditorUIScale::ButtonWidth("Apply") + EditorUIScale::ButtonWidth("Browse...") + ImGui::GetStyle().ItemSpacing.x * 2.0f;
		ImGui::SetNextItemWidth(-ButtonsWidth);
		const bool bEnterPressed = ImGui::InputTextWithHint("##ScriptFileName", "script name", FileNameBuffer, sizeof(FileNameBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue);

		// Content Drawer에서 끌어 온 .lua 파일을 받는다.
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::ScriptFile))
				Assign(static_cast<const char*>(Payload->Data));
			ImGui::EndDragDropTarget();
		}
		if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
			ImGui::SetTooltip("Type a file name, or drag a .lua file from the Content Drawer.");

		ImGui::SameLine();
		const bool bApplyClicked = ImGui::Button("Apply");
		ImGui::SameLine();
		if (ImGui::Button("Browse..."))
		{
			std::error_code Ec;
			const FString Initial = std::filesystem::absolute(ContentPaths::ScriptFolder, Ec).string();
			const FString Selected = FEditorFileUtils::OpenScriptDialog(Initial);
			// 대화상자는 ANSI 경로를 돌려주므로 path로 받아 UTF-8로 바꾼다.
			if (!Selected.empty())
				Assign(FLuaScriptManager::FromPath(std::filesystem::path(Selected)));
		}

		if (bEnterPressed || bApplyClicked)
			Assign(FileNameBuffer);

		// Edit Script(남은 폭 전부) | Detach
		const bool bAssigned = !Script->GetScriptPath().empty();
		const float EditWidth = bAssigned
			? -(EditorUIScale::ButtonWidth("Detach") + ImGui::GetStyle().ItemSpacing.x)
			: -FLT_MIN;
		if (ImGui::Button("Edit Script", ImVec2(EditWidth, 0.0f)))
		{
			// 아직 연결된 스크립트가 없으면 "씬이름_액터이름_컴포넌트이름.lua"로 정한다 (한 액터에 여러 개가 붙을 수 있어서).
			if (!bAssigned)
			{
				const FString& ScenePath = FEditorFileUtils::CurrentScenePath;
				if (ScenePath.empty())
				{
					HTR_LOG(Warning, "[Lua] Save the scene first. The scene name will be included in the script name.");
				}
				else
				{
					const FString SceneName = FLuaScriptManager::FromPath(FLuaScriptManager::ToPath(ScenePath).stem());
					AActor* Owner = Script->GetOwner();
					const FString OwnerName = Owner ? Owner->GetName() : FString("Actor");
					Script->SetScriptPath(FLuaScriptManager::MakeScriptPath(SceneName, OwnerName + "_" + Script->GetName()));
				}
			}
			// 파일이 없으면 template.lua로 만들고, 있으면 그대로 연다.
			if (!Script->GetScriptPath().empty() && FLuaScriptManager::Get().CreateScriptFromTemplate(Script->GetScriptPath()))
				FLuaScriptManager::Get().OpenScriptInEditor(Script->GetScriptPath());
		}
		if (ImGui::IsItemHovered() && !bAssigned)
			ImGui::SetTooltip("Creates <Scene>_<Actor>_<Component>.lua from the template and opens it.");

		if (bAssigned)
		{
			ImGui::SameLine();
			if (ImGui::Button("Detach"))
				Script->SetScriptPath(FString());   // 연결만 끊고 파일은 지우지 않는다
		}
	}

	bool DrawAxisControl(const FString& _label, float& _value, float _speed, float _minValue, float _maxValue, float _resetValue, FVector4 _color)
	{
		bool isValueChanged = false;

		// 배율이 적용된 프레임 높이를 써야 입력칸과 버튼 높이가 맞는다 (TEAM5).
		float lineHeight = ImGui::GetFrameHeight();
		ImVec4 color(_color.X, _color.Y, _color.Z, _color.W);
		ImVec2 buttonSize = { lineHeight + 2.0f , lineHeight };

		// 각 버튼의 색상 설정
		ImGui::PushStyleColor(ImGuiCol_Button, color);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ color.x + 0.1f, color.y + 0.1f, color.z + 0.1f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ color.x - 0.1f, color.y - 0.1f, color.z - 0.1f, 1.0f });

		// 굵은 폰트 적용
		if (ImGui::Button(_label.c_str(), buttonSize))
		{
			_value = _resetValue;
			isValueChanged = true;
		}
		ImGui::PopStyleColor(3);

		// 드래그 슬라이더
		ImGui::SameLine();
		std::string dragID = "##" + _label;
		isValueChanged |= ImGui::DragFloat(dragID.c_str(), &_value, _speed, _minValue, _maxValue, GetDragFormat());

		return isValueChanged;

	}

	bool DrawVector3Controller(const FString& _label, float* _values, float _resetValue)
	{
		ImGuiIO& io = ImGui::GetIO();
		auto boldFont = io.Fonts->Fonts[0];

		// 저장된 열 폭이 있으면 초기 폭을 무시하므로 저장하지 않는다 (TEAM5).
		ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings;

		bool isVectorChanged = false;

		ImGui::PushID(_label.c_str());
		if (ImGui::BeginTable(_label.c_str(), 2, flags)) // 고유 ID, 열 2개, 플래그
		{
			// ImGuiTableColumnFlags_WidthFixed: 초기 너비 고정
			// ImGuiTableColumnFlags_WidthStretch: 창 크기에 따라 너비 조절 (기본값)
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Location").x);
			ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(_label.c_str()); // 왼쪽 열: 레이블

			ImGui::TableSetColumnIndex(1);
			// [컨트롤러 UI 코드] // 오른쪽 열: 컨트롤러
			// 축 버튼 3개와 그 사이 간격(5칸)을 뺀 폭을 입력칸 3개가 나눠 쓴다 (TEAM5).
			const float AxisButtonWidth = ImGui::GetFrameHeight() + 2.0f;
			const float AxisSpacing = EditorUIScale::Px(2.0f);
			ImGui::PushMultiItemsWidths(3, ImGui::GetContentRegionAvail().x - AxisButtonWidth * 3.0f - AxisSpacing * 5.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ AxisSpacing, 0 });
			ImGui::PushFont(boldFont);

			isVectorChanged |= DrawAxisControl("X", _values[0], DragStep, 0.0f, 0.0f, _resetValue, { 0.8f, 0.1f, 0.1f, 1.0f });

			ImGui::PopItemWidth();

			ImGui::SameLine();
			isVectorChanged |= DrawAxisControl("Y", _values[1], DragStep, 0.0f, 0.0f, _resetValue, { 0.1f, 0.8f, 0.1f, 1.0f });

			ImGui::PopItemWidth();

			ImGui::SameLine();
			isVectorChanged |= DrawAxisControl("Z", _values[2], DragStep, 0.0f, 0.0f, _resetValue, { 0.1f, 0.1f, 0.8f, 1.0f });

			ImGui::PopItemWidth();

			ImGui::PopFont();
			ImGui::PopStyleVar();
			ImGui::EndTable();
		}

		ImGui::PopID();

		return isVectorChanged;
	}

	// FRotator의 V는 (Pitch, Yaw, Roll) 순서지만 엔진은 Roll=X, Pitch=Y, Yaw=Z축이므로
	// X/Y/Z 칸이 실제 회전축과 맞도록 재배열해 편집한다.
	bool DrawRotatorAsXYZ(const FString& _label, FRotator& _rotator)
	{
		float Euler[3] = { _rotator.Roll, _rotator.Pitch, _rotator.Yaw };
		const bool isChanged = DrawVector3Controller(_label, Euler, 0.0f);
		if (isChanged)
		{
			_rotator.Roll = Euler[0];
			_rotator.Pitch = Euler[1];
			_rotator.Yaw = Euler[2];
		}
		return isChanged;
	}

	void DrawTextureSlot(UMaterial** MaterialPtr, int32 SlotIndex)
	{
		const float ThumbnailSize = EditorUIScale::Px(64.0f);

		UMaterial* Material = *MaterialPtr;
		UTexture2D* Texture = nullptr;
		if (Material && SlotIndex < static_cast<int32>(Material->Textures.Num()))
		{
			Texture = Material->Textures[SlotIndex];
		}

		ImGui::PushID(SlotIndex);

		if (Texture && Texture->GetResource())
		{
			ImGui::Image(
				(ImTextureID)Texture->GetResource()->GetSRV(),
				{ ThumbnailSize, ThumbnailSize });
		}
		else
		{
			ImGui::Button("Empty", { ThumbnailSize, ThumbnailSize });
		}

		// 바로 위 위젯이 드롭을 받는다
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Texture))
			{
				UTexture2D* DroppedTexture = *static_cast<UTexture2D**>(Payload->Data);

				if (!Material)
				{
					return;
				}

				if (Material)
				{
					// 슬롯이 배열 범위 밖이면 그 자리까지 늘려 준다
					while (static_cast<int32>(Material->Textures.Num()) <= SlotIndex)
					{
						Material->Textures.Add(nullptr);
					}

					Material->Textures[SlotIndex] = DroppedTexture;
					Texture = DroppedTexture;
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine();

		ImGui::BeginGroup();
		ImGui::Text("Slot %d", SlotIndex);

		if (Texture)
		{
			ImGui::TextWrapped("%s", Texture->GetPath().c_str());
		}
		else
		{
			ImGui::TextDisabled("(비어 있음)");
		}
		ImGui::EndGroup();

		ImGui::PopID();
	}

	FString GetAssetDisplayName(const FString& Path)
	{
		fs::path AssetPath = FbxPath::FromString(Path);
		if (AssetPath.stem() == "Atlas")
		{
			return FbxPath::ToString(AssetPath.parent_path().filename());
		}
		return FbxPath::ToString(AssetPath.stem());
	}

	void DrawFontSlot(UObject* Owner, UFont** FontPtr)
	{
		UFont* Current = *FontPtr;

		FString CurrentPath = Current ? Current->GetPath() : "";
		FString CurrentFileName = Current ? GetAssetDisplayName(CurrentPath) : "None";

		ImGui::SetNextItemWidth(-1.0f);

		EditorUIScale::LimitNextComboPopupWidth();
		bool bOpen = ImGui::BeginCombo("##Font", CurrentFileName.c_str());
		if (ImGui::IsItemHovered() && Current)
		{
			ImGui::SetTooltip("%s", CurrentPath.c_str());
		}

		if (bOpen)
		{
			const TArray<UFont*> Fonts = EditorSort::CollectSorted<UFont>(
				TObjectIterator<UFont>(),
				[](const UFont* F) { return GetAssetDisplayName(F->GetPath()); },
				[](const UFont*) { return true; });

			for (UFont* Item : Fonts)
			{
				bool bSelected = (Item == Current);

				FString ItemPath = Item->GetPath();
				FString ItemFileName = GetAssetDisplayName(ItemPath);

				ImGui::PushID(Item);

				if (ImGui::Selectable(ItemFileName.c_str(), bSelected))
				{
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", ItemPath.c_str());
					}

					// 메모리를 직접 바꾸지 않고 SetFont를 거친다
					if (UTextRenderComponent* TextComponent = Cast<UTextRenderComponent>(Owner))
						TextComponent->SetFont(Item);
					else
						*FontPtr = Item;
				}
				ImGui::PopID();
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	}

	void DrawMeshSlot(UObject* Owner, UStaticMesh** StaticMeshPtr)
	{
		UStaticMesh* Current = *StaticMeshPtr;

		FString CurrentPath = Current ? Current->GetPath() : "";
		FString CurrentFileName = Current ? GetAssetDisplayName(CurrentPath) : "None";

		ImGui::SetNextItemWidth(-1.0f);

		EditorUIScale::LimitNextComboPopupWidth();
		bool bOpen = ImGui::BeginCombo("##StaticMesh", CurrentFileName.c_str());
		if (ImGui::IsItemHovered() && Current)
		{
			ImGui::SetTooltip("%s", CurrentPath.c_str());
		}

		if (bOpen)
		{
			const TArray<UStaticMesh*> Meshes = EditorSort::CollectSorted<UStaticMesh>(
				TObjectIterator<UStaticMesh>(),
				[](const UStaticMesh* M) { return GetAssetDisplayName(M->GetPath()); },
				[](const UStaticMesh* M) { return !M->IsRetired(); });

			for (UStaticMesh* Item : Meshes)
			{
				bool bSelected = (Item == Current);

				FString ItemPath = Item->GetPath();
				FString ItemFileName = GetAssetDisplayName(ItemPath);

				ImGui::PushID(Item);

				if (ImGui::Selectable(ItemFileName.c_str(), bSelected))
				{
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", ItemPath.c_str());
					}

					// 메모리를 직접 바꾸지 않고 SetFont를 거친다
					if (UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Owner))
						MeshComponent->SetStaticMesh(Item);
					else
						*StaticMeshPtr = Item;
				}
				ImGui::PopID();
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	}

	void DrawSkeletalMeshSlot(UObject* Owner, USkeletalMesh** SkeletalMeshPtr)
	{
		USkeletalMesh* Current = *SkeletalMeshPtr;

		FString CurrentPath = Current ? Current->GetPath() : "";
		FString CurrentFileName = Current ? (CurrentPath.empty() ? Current->GetName() : GetAssetDisplayName(CurrentPath)) : "None";

		ImGui::SetNextItemWidth(-1.0f);

		EditorUIScale::LimitNextComboPopupWidth();
		bool bOpen = ImGui::BeginCombo("##SkeletalMesh", CurrentFileName.c_str());
		if (ImGui::IsItemHovered() && Current)
		{
			ImGui::SetTooltip("%s", CurrentPath.c_str());
		}

		if (bOpen)
		{
			const TArray<USkeletalMesh*> Meshes = EditorSort::CollectSorted<USkeletalMesh>(
				TObjectIterator<USkeletalMesh>(),
				[](const USkeletalMesh* M) { return M->GetPath().empty() ? M->GetName() : GetAssetDisplayName(M->GetPath()); },
				[](const USkeletalMesh* M) { return !M->IsRetired(); });

			for (USkeletalMesh* Item : Meshes)
			{
				bool bSelected = (Item == Current);

				FString ItemPath = Item->GetPath();
				FString ItemFileName = ItemPath.empty() ? Item->GetName() : GetAssetDisplayName(ItemPath);

				ImGui::PushID(Item);

				if (ImGui::Selectable(ItemFileName.c_str(), bSelected))
				{
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", ItemPath.c_str());
					}

					if (USkinnedMeshComponent* SkinnedComponent = Cast<USkinnedMeshComponent>(Owner))
						SkinnedComponent->SetSkeletalMesh(Item);
					else
						*SkeletalMeshPtr = Item;
				}
				ImGui::PopID();
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	}

	UMaterialInstanceDynamic* EnsureMaterialOverride(UMeshComponent* MeshComponent, int32 Slot, UMaterialInterface*& Effective, UMaterialInterface*& Override)
	{
		if (UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Override))
		{
			return Dynamic;
		}

		UMaterialInterface* Parent = Effective ? Effective : UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
		if (!Parent)
		{
			return nullptr;
		}

		UMaterialInstanceDynamic* Dynamic = UMaterial::CreateDynamicInstance(Parent);
		if (!Dynamic)
		{
			return nullptr;
		}

		Override = Dynamic;
		Effective = Dynamic;
		MeshComponent->SetMaterial(Slot, Dynamic);

		return Dynamic;
	}

	void DrawParameterOverrideReset(UMaterialInterface* Override, const FName& ParameterName)
	{
		UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Override);
		if (!Dynamic || !Dynamic->HasParameterOverride(ParameterName))
		{
			return;
		}

		const FString ID = ParameterName.ToString();
		ImGui::SameLine();
		ImGui::PushID(ID.c_str());

		if (ImGui::SmallButton("Reset"))
		{
			Dynamic->ClearParameterOverride(ParameterName);
		}

		ImGui::PopID();
	}

	template<typename TEnsureOverride>
	void DrawMaterialTextureSlot(int32 MaterialSlot, int32 TextureSlot, const FName& ParameterName, const char* Label, UMaterialInterface*& Effective, UMaterialInterface*& Override, TEnsureOverride&& EnsureOverride)
	{
		const float ThumbnailSize = EditorUIScale::Px(64.0f);
		const float ItemThumbnailSize = EditorUIScale::Px(24.0f);

		FMaterialRenderProxy Proxy{};
		if (Effective)
		{
			Effective->GetRenderProxy(Proxy);
		}

		UTexture2D* Texture = Proxy.Textures.IsValidIndex(TextureSlot) ? Proxy.Textures[TextureSlot] : nullptr;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(Label);
		ImGui::TableSetColumnIndex(1);
		ImGui::PushID(MaterialSlot);
		ImGui::PushID(TextureSlot);

		if (Texture && Texture->GetResource())
		{
			ImGui::Image((ImTextureID)Texture->GetResource()->GetSRV(), { ThumbnailSize, ThumbnailSize });
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", Texture->GetPath().c_str());
			}
		}
		else
		{
			ImGui::Button("Empty", { ThumbnailSize, ThumbnailSize });
		}

		auto ApplyTexture = [&](UTexture2D* NewTexture)
		{
			if (UMaterialInstanceDynamic* Dynamic = EnsureOverride())
			{
				Dynamic->SetTextureParameterValue(ParameterName, NewTexture);
				Effective = Dynamic;
				Override = Dynamic;
				Texture = NewTexture;
			}
		};

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Texture))
			{
				ApplyTexture(*static_cast<UTexture2D**>(Payload->Data));
			}

			ImGui::EndDragDropTarget();
		}

		const FString CurrentPath = Texture ? Texture->GetPath() : "";
		const FString CurrentFileName = Texture ? GetAssetDisplayName(CurrentPath) : "None";

		ImGui::SetNextItemWidth(-1.0f);
		EditorUIScale::LimitNextComboPopupWidth();

		if (ImGui::BeginCombo("##Texture", CurrentFileName.c_str()))
		{
			if (ImGui::Selectable("None", Texture == nullptr))
			{
				ApplyTexture(nullptr);
			}

			const TArray<UTexture2D*> Textures = EditorSort::CollectSorted<UTexture2D>(
				TObjectIterator<UTexture2D>(),
				[](const UTexture2D* T)
				{ return GetAssetDisplayName(T->GetPath()); },
				[](UTexture2D* T)
				{ return !T->IsRetired() && T->GetResource() && !T->GetPath().empty(); });

			for (UTexture2D* ItemTexture : Textures)
			{
				const bool Selected = ItemTexture == Texture;
				const FString ItemPath = ItemTexture->GetPath();
				const FString ItemFileName = GetAssetDisplayName(ItemPath);

				ImGui::PushID(ItemTexture);
				ImGui::Image((ImTextureID)ItemTexture->GetResource()->GetSRV(), { ItemThumbnailSize, ItemThumbnailSize });
				ImGui::SameLine();

				if (ImGui::Selectable(ItemFileName.c_str(), Selected, ImGuiSelectableFlags_None, { 0.0f, ItemThumbnailSize }))
				{
					ApplyTexture(ItemTexture);
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", ItemPath.c_str());
				}
				if (Selected)
				{
					ImGui::SetItemDefaultFocus();
				}

				ImGui::PopID();
			}

			ImGui::EndCombo();
		}

		DrawParameterOverrideReset(Override, ParameterName);

		ImGui::PopID();
		ImGui::PopID();
	}

	void DrawDecalMaterial(UDecalComponent* Decal)
	{
		if (!Decal)
		{
			return;
		}

		ImGui::PushID(Decal);

		if (ImGui::CollapsingHeader("Decal Material", ImGuiTreeNodeFlags_DefaultOpen))
		{
			UMaterialInterface* Material = Decal->GetDecalMaterial();
			UMaterial* Base = Material ? Material->GetBaseMaterial() : nullptr;

			const FString Preview = Material ? (Base ? Base->GetPath() : FString("(Instance)")) : FString("(None)");

			EditorUIScale::LimitNextComboPopupWidth();

			if (ImGui::BeginCombo("Material", Preview.c_str()))
			{
				if (ImGui::Selectable("(None)", Material == nullptr))
				{
					Decal->SetDecalMaterial(nullptr);
				}

				const TArray<UMaterial*> Candidates = EditorSort::CollectSorted<UMaterial>(
					TObjectIterator<UMaterial>(),
					[](const UMaterial* M)
					{ return M->GetPath(); },
					[](const UMaterial* M)
					{ return !M->IsRetired() && !M->GetPath().empty(); });

				for (UMaterial* Candidate : Candidates)
				{
					const bool bSelected = Candidate == Material || Candidate == Base;

					if (ImGui::Selectable(Candidate->GetPath().c_str(), bSelected))
					{
						Decal->SetDecalMaterial(Candidate);
					}
				}

				ImGui::EndCombo();
			}

			Material = Decal->GetDecalMaterial();

			UMaterialInterface* Effective = Material;
			UMaterialInterface* Override = Material;

			auto EnsureOverride = [&]() -> UMaterialInstanceDynamic*
			{
				if (UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Override))
				{
					return Dynamic;
				}

				UMaterialInterface* Parent = Effective ? Effective : UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
				if (!Parent)
				{
					return nullptr;
				}

				UMaterialInstanceDynamic* Dynamic = UMaterial::CreateDynamicInstance(Parent);
				if (!Dynamic)
				{
					return nullptr;
				}

				Effective = Dynamic;
				Override = Dynamic;
				Decal->SetDecalMaterial(Dynamic);
				return Dynamic;
			};

			DrawMaterialTextureSlot(0, 0, FName("Texture0"), "Base Color", Effective, Override, EnsureOverride);
			DrawMaterialTextureSlot(0, 1, FName("Texture1"), "Normal Map", Effective, Override, EnsureOverride);

			if (Effective)
			{
				FMaterialRenderProxy Proxy{};
				Effective->GetRenderProxy(Proxy);

				FVector4 Color = Proxy.BaseColor;

				if (ImGui::ColorEdit4("Material BaseColor", &Color.X))
				{
					if (UMaterialInstanceDynamic* Dynamic = EnsureOverride())
					{
						Dynamic->SetVectorParameterValue(FName("BaseColor"), Color);
						Effective = Dynamic;
						Override = Dynamic;
						Decal->SetDecalMaterial(Dynamic);
					}
				}

				DrawParameterOverrideReset(Override, FName("BaseColor"));

				if (ImGui::Button("Clear Material"))
				{
					Decal->SetDecalMaterial(nullptr);
				}
			}
		}

		ImGui::PopID();
	}

	// 메시 컴포넌트의 머티리얼 슬롯을 한 줄씩 그린다.
	// 텍스처를 드롭하면 그 슬롯의 덮어쓰기에 반영하고, 메시 에셋의 머티리얼은 건드리지 않는다.
	void DrawMaterialSlots(UMeshComponent* MeshComponent)
	{
		const int32 NumSlots = MeshComponent->GetNumMaterials();
		if (NumSlots <= 0)
		{
			return;
		}

		if (!ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		// 메쉬 내의 매터리얼 별 Slot 순회
		for (int32 Slot = 0; Slot < NumSlots; ++Slot)
		{
			ImGui::PushID(Slot);

			FString SlotLabel = std::format("[{}] {}", Slot, MeshComponent->GetMaterialSlotName(Slot));
			bool bOpen = ImGui::TreeNode(SlotLabel.c_str());

			UMaterialInterface* Override = MeshComponent->GetOverrideMaterial(Slot);
			UMaterialInterface* Effective = MeshComponent->GetMaterial(Slot);

			auto EnsureOverride = [&]() -> UMaterialInstanceDynamic*
			{
				return EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
			};

			auto ReadProxy = [&]() -> FMaterialRenderProxy
			{
				FMaterialRenderProxy Proxy{};
				if (Effective)
				{
					Effective->GetRenderProxy(Proxy);
				}
				return Proxy;
			};

			ImGui::SameLine();

			if (Override)
			{
				ImGui::TextDisabled("Override");
				ImGui::SameLine();

				if (ImGui::SmallButton("Reset"))
				{
					MeshComponent->SetMaterial(Slot, nullptr);
					Override = nullptr;
					Effective = MeshComponent->GetMaterial(Slot);
				}
			}
			else
			{
				ImGui::TextDisabled("Default");
			}

			if (bOpen)
			{
				if (ImGui::BeginTable("MaterialProperties", 2))
				{
					ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Override Shading").x);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

					DrawMaterialTextureSlot(Slot, 0, FName("Texture0"), "Texture", Effective, Override, EnsureOverride);
					DrawMaterialTextureSlot(Slot, 1, FName("Texture1"), "Normal Map", Effective, Override, EnsureOverride);

					ImGui::TableNextRow();

					// 3: Base Color
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Base Color");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					FMaterialRenderProxy Proxy = ReadProxy();
					FVector4 TempColor = Proxy.BaseColor;

					if (EditLinearColor("##BaseColor", &TempColor.X, true))
					{
						if (UMaterialInstanceDynamic* Dynamic = EnsureOverride())
						{
							Dynamic->SetVectorParameterValue(FName("BaseColor"), TempColor);
						}
					}

					DrawParameterOverrideReset(Override, FName("BaseColor"));

					ImGui::TableNextRow();
					
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Shininess");
					
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);
					
					FMaterialRenderProxy Proxy = ReadProxy();
					float TempShininess = Proxy.Shininess;

					if (ImGui::DragFloat("##Shininess", &TempShininess, 0.5f, 1.0f, 256.0f))
					{
						if (UMaterialInstanceDynamic* Dynamic = EnsureOverride())
						{
							Dynamic->SetScalarParameterValue(FName("Shininess"), TempShininess);
						}
					}

					DrawParameterOverrideReset(Override, FName("Shininess"));

					ImGui::TableNextRow();
					
					// 4: UV Scroll Speed
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("UV Scroll");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					FMaterialRenderProxy Proxy = ReadProxy();
					FVector2 TempUVScrollSpeed = Proxy.UVScrollSpeed;

					if (ImGui::DragFloat2("##UVScrollSpeed", &TempUVScrollSpeed.X, 0.01f))
					{
						if (UMaterialInstanceDynamic* Dynamic = EnsureOverride())
						{
							Dynamic->SetVectorParameterValue(FName("UVScrollSpeed"), FVector4(TempUVScrollSpeed.X, TempUVScrollSpeed.Y, 0.0f, 0.0f));
						}
					}

					DrawParameterOverrideReset(Override, FName("UVScrollSpeed"));

					ImGui::TableNextRow();

					// 5: Sampler State
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Sampler");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					const char* SamplerItems[] = { "Linear Clamp", "Linear Wrap" };
					int SamplerIndex = Effective ? static_cast<int>(Effective->SamplerState) : static_cast<int>(ESamplerState::LinearClamp);
					EditorUIScale::LimitNextComboPopupWidth();
					if (ImGui::BeginCombo("##SamplerState", SamplerItems[SamplerIndex]))
					{
						for (int i = 0; i < IM_ARRAYSIZE(SamplerItems); ++i)
						{
							if (ImGui::Selectable(SamplerItems[i], SamplerIndex == i))
							{
								Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
								if (Override)
								{
									Override->SamplerState = static_cast<ESamplerState>(i);
									Effective = Override;
								}
							}
						}
						ImGui::EndCombo();
					}

					ImGui::TableNextRow();

					// 6: Blend State
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Blend Mode");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					const char* BlendItems[] = { "Opaque", "Alpha Blend" };
					int BlendIndex = Effective ? static_cast<int>(Effective->BlendState) : static_cast<int>(EBlendState::Opaque);
					EditorUIScale::LimitNextComboPopupWidth();
					if (ImGui::BeginCombo("##BlendState", BlendItems[BlendIndex]))
					{
						for (int i = 0; i < IM_ARRAYSIZE(BlendItems); ++i)
						{
							if (ImGui::Selectable(BlendItems[i], BlendIndex == i))
							{
								Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
								if (Override)
								{
									Override->SetBlendState(static_cast<EBlendState>(i));
									Effective = Override;
								}
							}
						}
						ImGui::EndCombo();
					}

					// TEAM5 material controls share the same instance-on-edit policy.
					auto Row = [](const char* Label)
					{
						ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(Label);
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1.0f);
					};
					auto Commit = [&]() -> UMaterialInstanceDynamic*
					{
						return EnsureOverride();
					};

					auto ScalarParameter = [&](const char* Label, const FName& Name, float Value)
					{
						Row(Label);

						if (ImGui::DragFloat(Label, &Value, 0.02f, 0.0f, 100.0f))
						{
							if (UMaterialInstanceDynamic* Dynamic = Commit())
							{
								Dynamic->SetScalarParameterValue(Name, Value);
							}
						}

						DrawParameterOverrideReset(Override, Name);
					};

					auto ColorParameter = [&](const char* Label, const FName& Name, const FVector& Value)
					{
						Row(Label);

						FVector Color = Value;

						if (EditLinearColor(Label, &Color.X))
						{
							if (UMaterialInstanceDynamic* Dynamic = Commit())
							{
								Dynamic->SetVectorParameterValue(Name, FVector4(Color.X, Color.Y, Color.Z, 1.0f));
							}
						}

						DrawParameterOverrideReset(Override, Name);
					};
					FMaterialRenderProxy Proxy = ReadProxy();

					ColorParameter("Specular Color", FName("SpecularColor"), Proxy.SpecularColor);
					ScalarParameter("Specular Factor", FName("SpecularFactor"), Proxy.SpecularFactor);
					ColorParameter("Emissive Color", FName("EmissiveColor"), Proxy.EmissiveColor);
					ScalarParameter("Emissive Factor", FName("EmissiveFactor"), Proxy.EmissiveFactor);
					Row("UV Tiling");
					FMaterialRenderProxy TilingProxy = ReadProxy();
					FVector2 Tiling = TilingProxy.UVTiling;

					if (ImGui::DragFloat2("##UVTiling", &Tiling.X, 0.05f, 0.01f, 1000.0f))
					{
						if (UMaterialInstanceDynamic* Dynamic = Commit())
						{
							Dynamic->SetVectorParameterValue(FName("UVTiling"), FVector4(Tiling.X, Tiling.Y, 0.0f, 0.0f));
						}
					}

					DrawParameterOverrideReset(Override, FName("UVTiling"));
					Row("World Tile Size");
					FMaterialRenderProxy WorldTileProxy = ReadProxy();
					FVector2 Tile = WorldTileProxy.WorldUVTileSize;

					if (ImGui::DragFloat2("##WorldTileSize", &Tile.X, 0.05f, 0.0f, 10000.0f))
					{
						if (UMaterialInstanceDynamic* Dynamic = Commit())
						{
							Dynamic->SetVectorParameterValue(FName("WorldUVTileSize"), FVector4(Tile.X, Tile.Y, 0.0f, 0.0f));
						}
					}

					DrawParameterOverrideReset(Override, FName("WorldUVTileSize"));
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("X=0 disables world tiling. Y=0 uses X for both axes. Use Linear Wrap to repeat.");
					Row("Local Tile Axes");
					bool Local = Effective && Effective->bWorldUVLocalSpace;
					if (ImGui::Checkbox("##LocalTileAxes", &Local)) if (UMaterial* M = Commit()) M->bWorldUVLocalSpace = Local;
					Row("Override Shading");
					bool OverrideShading = Effective && Effective->bOverrideShadingModel;
					if (ImGui::Checkbox("##OverrideShading", &OverrideShading)) if (UMaterial* M = Commit()) M->bOverrideShadingModel = OverrideShading;
					Row("Shading Model");
					int Model = static_cast<int>(Effective ? Effective->ShadingModel : ELightingModel::Phong);
					const char* Models[] = { "Gouraud", "Lambert", "Blinn-Phong", "Unlit" };
					ImGui::BeginDisabled(!OverrideShading);
					if (ImGui::Combo("##MaterialShadingModel", &Model, Models, IM_ARRAYSIZE(Models)))
						if (UMaterial* M = Commit()) M->ShadingModel = static_cast<ELightingModel>(Model);
					ImGui::EndDisabled();
					ImGui::EndTable();
				}
				ImGui::TreePop();
			}
			ImGui::PopID();

			ImGui::Separator();
		}
	}

	// UClass에 등록된 프로퍼티를 타입에 맞는 위젯으로 그린다
	bool DrawProperty(UObject* Object, const FProperty& Property, ImFont* CustomFont)
	{
		void* ValuePtr = reinterpret_cast<char*>(Object) + Property.Offset;
		const FString Label = "##" + Property.Name;

		const char* DisplayName = Property.Name.c_str();

		// 라벨 | 값 배치를 유지하고 긴 라벨은 라벨 영역에서 잘라낸다.
		const float StartX = ImGui::GetCursorPosX();
		const float AvailableWidth = ImGui::GetContentRegionAvail().x;
		// 체크박스는 오른쪽 끝에 맞추고 나머지 공간을 라벨에 배정한다.
		const float LabelWidth = Property.Type == EPropertyType::Bool
			? std::max(0.0f, AvailableWidth - ImGui::GetFrameHeight())
			: AvailableWidth * 0.45f;

		const bool bInline = Property.Type != EPropertyType::Transform;

		ImGui::AlignTextToFramePadding();
		if (bInline)
		{
			const ImVec2 LabelStart = ImGui::GetCursorScreenPos();
			const float ClipWidth = std::max(0.0f, LabelWidth - ImGui::GetStyle().ItemSpacing.x);
			ImGui::PushClipRect(LabelStart, ImVec2(LabelStart.x + ClipWidth, LabelStart.y + ImGui::GetFrameHeight()), true);
			ImGui::PushTextWrapPos(-1.0f);
			ImGui::TextUnformatted(DisplayName);
			ImGui::PopTextWrapPos();
			ImGui::PopClipRect();
			ImGui::SameLine();
			ImGui::SetCursorPosX(StartX + LabelWidth);
		}
		else
		{
			ImGui::TextWrapped("%s", DisplayName);
		}

		ImGui::SetNextItemWidth(-1.0f);

		bool bChanged = false;

		switch (Property.Type)
		{
		case EPropertyType::Float:
			bChanged = ImGui::DragFloat(Label.c_str(), static_cast<float*>(ValuePtr), DragStep, 0.0f, 0.0f, GetDragFormat());
			break;
		case EPropertyType::Int:
			bChanged = ImGui::DragInt(Label.c_str(), static_cast<int*>(ValuePtr), 1.0f);
			break;
		case EPropertyType::Bool:
			bChanged = ImGui::Checkbox(Label.c_str(), static_cast<bool*>(ValuePtr));
			break;
		case EPropertyType::Vector:
		{
			FVector* Value = static_cast<FVector*>(ValuePtr);
			bChanged = ImGui::DragFloat3(Label.c_str(), Value->V, DragStep, 0.0f, 0.0f, GetDragFormat());
			break;
		}
		case EPropertyType::Rotator:
		{
			FRotator* Value = static_cast<FRotator*>(ValuePtr);
			// Transform의 Rotation과 같은 X/Y/Z 축 순서로 보여준다.
			float Euler[3] = { Value->Roll, Value->Pitch, Value->Yaw };
			if (ImGui::DragFloat3(Label.c_str(), Euler, DragStep, 0.0f, 0.0f, GetDragFormat()))
			{
				Value->Roll = Euler[0];
				Value->Pitch = Euler[1];
				Value->Yaw = Euler[2];
			}
			break;
		}


		case EPropertyType::Vector4:
		{
			FVector4* Value = static_cast<FVector4*>(ValuePtr);
			bChanged = ImGui::DragFloat4(Label.c_str(), &Value->X, DragStep, 0.0f, 0.0f, GetDragFormat());
			break;
		}

		case EPropertyType::Color:
		{
			FColor* C = static_cast<FColor*>(ValuePtr);
			float V[4] = { C->R / 255.f, C->G / 255.f, C->B / 255.f, C->A / 255.f };
			if (ImGui::ColorEdit4(Label.c_str(), V))
			{
				*C = FColor(uint8(V[0] * 255.f + 0.5f), uint8(V[1] * 255.f + 0.5f),
					uint8(V[2] * 255.f + 0.5f), uint8(V[3] * 255.f + 0.5f));
				bChanged = true;
			}
			break;
		}

		case EPropertyType::LinearColor:
		{
			// 타입은 Vector4와 같고 위젯만 색상 선택기다
			FLinearColor* Value = static_cast<FLinearColor*>(ValuePtr);
			bChanged = ImGui::ColorEdit4(Label.c_str(), &Value->R);
			break;
		}
		case EPropertyType::String:
		{
			FString* Value = static_cast<FString*>(ValuePtr);
			if (Property.Name == "AnimGraphPath" && Object->IsA(USkeletalMeshComponent::StaticClass()))
			{
				auto* Component = static_cast<USkeletalMeshComponent*>(Object);
				const FString& Current = Component->GetAnimGraphPath();
				if (ImGui::BeginCombo(Label.c_str(), Current.empty() ? "None" : Current.c_str()))
				{
					if (ImGui::Selectable("None", Current.empty())) Component->SetAnimGraphPath({});
					std::error_code Ec;
					for (fs::recursive_directory_iterator It("Assets", Ec), End; !Ec && It != End; It.increment(Ec))
					{
						if (It->path().extension() != ".animgraph") continue;
						const FString Candidate = FbxPath::ToString(It->path());
						FAnimGraphAsset Graph; FString Error;
						if (!FAnimGraphAsset::Load(Candidate, Graph, Error)) continue;
						USkeletalMesh* Mesh = Component->GetSkeletalMesh();
						if (Mesh && Mesh->Skeleton && !Graph.SkeletonPath.empty() &&
							Graph.SkeletonPath != Mesh->Skeleton->GetPath()) continue;
						if (ImGui::Selectable(Candidate.c_str(), Candidate == Current))
							Component->SetAnimGraphPath(Candidate);
					}
					ImGui::EndCombo();
				}
				break;
			}

			char Buffer[256] = {};
			strncpy_s(Buffer, Value->c_str(), sizeof(Buffer) - 1);
			if (CustomFont) ImGui::PushFont(CustomFont);
			if (ImGui::InputText(Label.c_str(), Buffer, sizeof(Buffer)))
			{
				*Value = Buffer;
				bChanged = true;
			}
			if (CustomFont) ImGui::PopFont();
			break;
		}
		case EPropertyType::Transform:
		{
			FTransform* Value = static_cast<FTransform*>(ValuePtr);

			FTransform Transform = *Value;
			bool bChange = false;

			bChanged |= DrawVector3Controller("Location", Value->Location.V, 0.0f);
			bChanged |= DrawRotatorAsXYZ("Rotation", Value->Rotation);
			bChanged |= DrawVector3Controller("Scale", Value->Scale.V, 1.0f);

			//if (bChange)
			//{
			//	// 씬 컴포넌트면 SetTransform을 거치고, 아니면 값을 직접 쓴다
			//	if (USceneComponent* SceneComponent = Cast<USceneComponent>(Object))
			//	{
			//		SceneComponent->SetTransform(Transform);
			//	}
			//	else
			//	{
			//		*Value = Transform;
			//	}
			//}
			break;
		}
		case EPropertyType::Object:
		{
			UObject** ObjPtr = static_cast<UObject**>(ValuePtr);

			if (Property.Name == "AnimClass" && Object->IsA(USkeletalMeshComponent::StaticClass()))
			{
				USkeletalMeshComponent* Component = static_cast<USkeletalMeshComponent*>(Object);
				UClass* Current = Component->GetAnimInstanceClass();
				if (ImGui::BeginCombo(Label.c_str(), Current ? Current->Name.c_str() : "None"))
				{
					if (ImGui::Selectable("None", !Current)) { Component->SetAnimGraphPath({}); Component->SetAnimInstanceClass(nullptr); }
					TArray<UClass*> Classes;
					Classes.Add(UAnimInstance::StaticClass());
					GetDerivedClasses(UAnimInstance::StaticClass(), Classes);
					for (UClass* Class : Classes)
						if (Class && Class != UAnimGraphInstance::StaticClass() && Class->Constructor && ImGui::Selectable(Class->Name.c_str(), Current == Class))
						{
							Component->SetAnimGraphPath({});
							Component->SetAnimInstanceClass(Class);
						}
					ImGui::EndCombo();
				}
			}
			else if (Property.Name == "AnimSequence" && Object->IsA(USkeletalMeshComponent::StaticClass()))
			{
				USkeletalMeshComponent* Component = static_cast<USkeletalMeshComponent*>(Object);
				UAnimSequence* Current = Component->GetAnimationSequence();
				// 테이크 이름(예: "Unreal Take")은 파일마다 같을 수 있어 애셋 이름(AS_...)을 보여 준다.
				const auto SequenceLabel = [](const UAnimSequence* Sequence) { return FbxPath::ToString(FbxPath::FromString(Sequence->GetPath()).stem()); };
				const FString Preview = Current ? SequenceLabel(Current) : FString("None");
				if (ImGui::BeginCombo(Label.c_str(), Preview.c_str()))
				{
					if (ImGui::Selectable("None", !Current)) Component->SetAnimationSequence(nullptr);
					TArray<UAnimSequence*> Sequences;
					USkeletalMesh* Mesh = Component->GetSkeletalMesh();
					UAssetManager::GetAnimSequencesForSkeleton(Mesh ? Mesh->Skeleton : nullptr, Sequences);
					for (UAnimSequence* Sequence : Sequences)
					{
						const FString Name = SequenceLabel(Sequence);
						if (ImGui::Selectable(Name.c_str(), Current == Sequence))
							Component->SetAnimationSequence(Sequence);
					}
					ImGui::EndCombo();
				}
					if (Current && ImGui::TreeNode("Root Motion"))
					{
						const bool OldEnable = Current->bEnableRootMotion;
						const bool OldForce = Current->bForceRootLock;
						const bool OldScale = Current->bUseNormalizedRootMotionScale;
						const ERootMotionRootLock OldLock = Current->RootMotionRootLock;
						bool Changed = ImGui::Checkbox("Enable Root Motion", &Current->bEnableRootMotion);
						int Lock = static_cast<int>(Current->RootMotionRootLock);
						if (ImGui::Combo("Root Motion Root Lock", &Lock, "Ref Pose\0Anim First Frame\0Zero\0"))
						{
							Current->RootMotionRootLock = static_cast<ERootMotionRootLock>(Lock);
							Changed = true;
						}
						Changed |= ImGui::Checkbox("Force Root Lock", &Current->bForceRootLock);
						Changed |= ImGui::Checkbox("Use Normalized Root Motion Scale", &Current->bUseNormalizedRootMotionScale);
						if (Changed && !UAssetManager::SaveAnimSequence(*Current))
						{
							Current->bEnableRootMotion = OldEnable;
							Current->bForceRootLock = OldForce;
							Current->bUseNormalizedRootMotionScale = OldScale;
							Current->RootMotionRootLock = OldLock;
							HTR_LOG(Error, "Failed to save animation root motion settings: {}", Current->GetPath());
						}
						ImGui::TreePop();
					}
			}
			else if (Property.Class == UMaterial::StaticClass())
			{
				DrawTextureSlot(reinterpret_cast<UMaterial**>(ObjPtr), 0);
			}
			else if (Property.Class == UFont::StaticClass())
			{
				DrawFontSlot(Object, reinterpret_cast<UFont**>(ObjPtr));
			}
			else if (Property.Class == UStaticMesh::StaticClass())
			{
				DrawMeshSlot(Object, reinterpret_cast<UStaticMesh**>(ObjPtr));
			}
			else if (Property.Class == USkeletalMesh::StaticClass())
			{
				DrawSkeletalMeshSlot(Object, reinterpret_cast<USkeletalMesh**>(ObjPtr));
			}
			break;
		}
		default:
			ImGui::TextDisabled("(Unsupported)");
			break;
		}

		if (bChanged)
		{
			Object->PostEditChangeProperty(Property);
		}
		return bChanged;
	}

	// 클래스 계층을 따라 올라가며 각 단계의 프로퍼티를 표시
	bool DrawProperties(UObject* Object, ImFont* CustomFont)
	{
		if (!Object)
		{
			return false;
		}

		bool bAnyChanged = false;

		TArray<UClass*> ClassChain;
		for (UClass* Class = Object->GetClass(); Class; Class = Class->Super)
		{
			ClassChain.Add(Class);
		}

		// 기반 클래스부터 표시
		for (auto It = ClassChain.rbegin(); It != ClassChain.rend(); ++It)
		{
			UClass* Class = *It;

			// 스크립트는 경로 검증을 위한 전용 UI를 같은 클래스 헤더 안에 표시한다.
			UScriptComponent* ScriptComponent = Class == UScriptComponent::StaticClass()
				? Cast<UScriptComponent>(Object) : nullptr;

			// 노출할 프로퍼티나 전용 UI가 없으면 헤더도 그리지 않는다.
			bool bHasVisibleProperty = false;
			for (const FProperty& Property : Class->GetProperties())
			{
				if (Property.HasAnyFlags(CPF_Edit))
				{
					bHasVisibleProperty = true;
					break;
				}
			}
			if (!bHasVisibleProperty && !ScriptComponent)
			{
				continue;
			}

			ImGui::PushID(Class->Name.c_str());
			if (ImGui::CollapsingHeader(Class->Name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				for (const FProperty& Property : Class->GetProperties())
				{
					if (!Property.HasAnyFlags(CPF_Edit))
					{
						continue;
					}
					const bool bReadOnly = Property.HasAnyFlags(CPF_EditConst);
					if (bReadOnly) ImGui::BeginDisabled();
					bAnyChanged |= DrawProperty(Object, Property, CustomFont);
					if (bReadOnly) ImGui::EndDisabled();
				}
				if (ScriptComponent)
				{
					DrawScriptSection(ScriptComponent);
				}
			}
			ImGui::PopID();
		}
		return bAnyChanged;
	}

	void DrawActorHeader(AActor* Actor, UActorComponent*& OutSelectedComponent)
	{
		if (!Actor) { return; }

		static char ComponentNameInputBuf[256] = {};

		ImGui::Text("Actor: %s", Actor->GetName().c_str());
		ImGui::Text("UUID: %u", Actor->GetUUID());
		DrawDragStepCombo();

		if (ImGui::Button("Add Component"))
		{
			ImGui::OpenPopup("AddComponentPopup");
		}

		if (ImGui::BeginPopup("AddComponentPopup"))
		{
			ImGui::Text("Component Name:");
			ImGui::SameLine();
			ImGui::InputText("##Component Name", ComponentNameInputBuf, sizeof(ComponentNameInputBuf));
			TArray<UClass*> Classes;
			GetDerivedClasses(UActorComponent::StaticClass(), Classes);
			// 이름순 정렬하면 메뉴가 안정적
			EditorSort::SortByName(Classes, [](const UClass* Class) { return Class->Name; });
			for (UClass* Class : Classes)
			{
				//if (!IsSpawnableComponent(Class)) continue;
				if (ImGui::MenuItem(Class->Name.c_str()))
				{
					OutSelectedComponent = Actor->AddComponent(Class, FString(ComponentNameInputBuf));
					if (OutSelectedComponent)
						OutSelectedComponent->MarkUserCreated();
					ComponentNameInputBuf[0] = '\0';
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}
	}
}

// UI 에디터처럼 다른 패널도 같은 속성 편집 UI를 쓰도록 위 함수들을 공개한다.
// 보조 함수들이 이 파일의 익명 네임스페이스에 있어 구현은 옮기지 않고 넘겨 준다.
namespace FPropertyEditor
{
	bool DrawProperty(UObject* Object, const FProperty& Property, ImFont* CustomFont)
	{
		return ::DrawProperty(Object, Property, CustomFont);
	}

	bool DrawObjectProperties(UObject* Object, ImFont* CustomFont)
	{
		return DrawProperties(Object, CustomFont);
	}
}

bool FDetailsPanel::Init()
{
	ImGuiIO& io = ImGui::GetIO();

	io.Fonts->AddFontDefault();

	const char* fontPath = EditorResourcePaths::UIFont;
	float fontSize = 15.0f;
	const ImWchar* koreanRanges = io.Fonts->GetGlyphRangesKorean();

	// 기본 폰트(영문 전용)에 한글 글리프만 합친다. PushFont 없이 그리는 툴팁·콤보 같은 곳에서도 한글이 나오게 한다 (TEAM5).
	// 기본 폰트에 이미 있는 영문은 그대로 쓰고, 없는 글자만 이 폰트에서 가져온다. 크기 0 = 기본 폰트 크기에 맞춤.
	ImFontConfig KoreanMergeConfig;
	KoreanMergeConfig.MergeMode = true;
	io.Fonts->AddFontFromFileTTF(fontPath, 0.0f, &KoreanMergeConfig, koreanRanges);

	CustomFont = io.Fonts->AddFontFromFileTTF(fontPath, fontSize, nullptr, koreanRanges);
	assert(CustomFont != nullptr);

	return true;
}

void FDetailsPanel::Tick(float DeltaTime)
{
}

const ULightComponent* FDetailsPanel::GetSelectedLight() const
{
	UActorComponent* Component = TargetComponent;
	while (Component && Component->IsHiddenInDetails())
	{
		USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
		Component = SceneComponent ? SceneComponent->GetAttachParent() : nullptr;
	}
	return Cast<ULightComponent>(Component);
}

void FDetailsPanel::DrawShadowPreview()
{
	const ULightComponent* Light = GetSelectedLight();
	if (!Light || !QueryShadowPreview || !ImGuiRenderer) return;
	if (!ImGui::CollapsingHeader("Shadow Maps", ImGuiTreeNodeFlags_DefaultOpen)) return;
	FTexture2DArray* Atlas = nullptr;
	TArray<FLightShadowRenderData> Shadows;
	if (!QueryShadowPreview(Light, Atlas, Shadows) || !Atlas || !Atlas->GetWidth() || !Atlas->GetHeight())
	{
		ImGui::TextDisabled("No shadow map available for this light in the active view.");
		return;
	}
	ImGui::SliderFloat("Preview Contrast", &ShadowPreviewContrast, 1.0f, 256.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
	const float Width = float(Atlas->GetWidth());
	const float Height = float(Atlas->GetHeight());
	const float DisplayWidth = ImGui::GetContentRegionAvail().x;
	if (DisplayWidth <= 0) return;
	static const char* PointFaces[] = { "+X", "-X", "+Z", "-Z", "+Y", "-Y" };
	for (int32 i = 0; i < Shadows.Num(); ++i)
	{
		const auto& Shadow = Shadows[i];
		const FRect& Rect = Shadow.AtlasRect;
		if (Rect.Width <= 0 || Rect.Height <= 0) continue;
		ImGui::PushID(i);
		if (Light->GetLightType() == ELightType::Point)
			ImGui::Text("Face %s", PointFaces[i % 6]);
		else if (Light->GetLightType() == ELightType::Directional)
			ImGui::Text("Cascade %d", i + 1);
		ImGui::TextDisabled("Slice %u | %.0f x %.0f", Shadow.AtlasIndex, Rect.Width, Rect.Height);
		
		const ImVec2 UV0(Rect.X / Width, Rect.Y / Height);
		const ImVec2 UV1((Rect.X + Rect.Width) / Width, (Rect.Y + Rect.Height) / Height);
		
		if (!ImGuiRenderer->DrawShadowMap(Atlas->GetSRV(), Shadow.AtlasIndex,
			ImVec2(DisplayWidth, DisplayWidth * Rect.Height / Rect.Width), UV0, UV1, ShadowPreviewContrast))
			ImGui::TextDisabled("Shadow preview shader is unavailable.");
		ImGui::PopID();
	}
}


void FDetailsPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(400.0f), EditorUIScale::Px(200.0f)), ImGuiCond_FirstUseEver);

	if (!BeginPanel())
	{
		ImGui::End();
		return;
	}

	if (TargetActor)
	{
		// 빌보드처럼 디테일에 숨겨진 컴포넌트가 선택되면 노출되는 부모로 올린다.
		// (예: 라이트는 빌보드를 클릭해서 고르지만 수치는 SpotLight 쪽에 있다)
		while (TargetComponent && TargetComponent->IsHiddenInDetails())
		{
			USceneComponent* HiddenComponent = Cast<USceneComponent>(TargetComponent);
			TargetComponent = HiddenComponent ? HiddenComponent->GetAttachParent() : nullptr;
		}

		UActorComponent* SelectCandidateComponent = TargetComponent;

		DrawActorHeader(TargetActor, TargetComponent);
		DrawCompoenetList(TargetActor, TargetComponent);				
		ImGui::Separator();

		// 액터 -> 컴포넌트 순으로, 클래스별 프로퍼티 표시
		ImGui::PushID(TargetActor);
		DrawProperties(TargetActor, CustomFont);
		ImGui::PopID();

		if (TargetComponent)
		{
			ImGui::PushID(TargetComponent);
			ImGui::SeparatorText("Component Properties");
			DrawProperties(TargetComponent, CustomFont);
			DrawShadowPreview();

			if(SelectCandidateComponent != TargetComponent)
			{
				SelectComponent(TargetComponent);
			}
			if (UMeshComponent* MeshComponent = Cast<UMeshComponent>(TargetComponent))
			{
				DrawMaterialSlots(MeshComponent);
			}
			if (UDecalComponent* DecalComponent = Cast<UDecalComponent>(TargetComponent))
			{
				DrawDecalMaterial(DecalComponent);
			}
			ImGui::PopID();

			if (PendingDeleteComponent)
			{
				if (DeleteCallback)
				{
					DeleteCallback(PendingDeleteComponent);
				}

				PendingDeleteComponent = nullptr;
			}			
		}		
	}	
	ImGui::End();
}


void FDetailsPanel::SelectComponent(UActorComponent* Component)
{
	if (!Component)
	{
		TargetComponent = nullptr;
		EditingNameComponent = nullptr;
		bFocusNameEdit = false;

		if (Callback)
			Callback(nullptr);

		return;
	}

	TargetComponent = Component;

	if (Callback)
		Callback(TargetComponent);
}

void FDetailsPanel::DrawCompoenetList(AActor* SelectedActor, UActorComponent*& OutSelectedComponent)
{
	if (!SelectedActor) { return; }

	// 트리를 순회하는 도중에는 계층을 바꾸지 않는다.
	if (PendingRootComponent)
	{
		if (SelectedActor->ReplaceRootComponent(PendingRootComponent))
		{
			OutSelectedComponent = PendingRootComponent;
			SelectComponent(PendingRootComponent);
		}
		PendingRootComponent = nullptr;
	}

	if (PendingDragComponent && PendingDroppedComponent)
	{
		TryReparent(PendingDroppedComponent, PendingDragComponent);
		PendingDragComponent = nullptr;
		PendingDroppedComponent = nullptr;
	}

	USceneComponent* Root = SelectedActor->GetRootComponent();

	if (Root && ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen))
	{
		DrawSceneComponentNode(Root, Root, OutSelectedComponent);
	}

	// Draw ActorComponent
	bool bHasDrawnActorComponent = false;
	for (UActorComponent* ActorComp : SelectedActor->GetComponents())
	{
		if (!ActorComp || Cast<USceneComponent>(ActorComp))
		{
			continue;
		}

		if (!bHasDrawnActorComponent)
		{
			ImGui::Separator();
			bHasDrawnActorComponent = true;
		}

		ImGui::PushID(ActorComp);
		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
			ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
		if (ActorComp == OutSelectedComponent)
		{
			Flags |= ImGuiTreeNodeFlags_Selected;
		}
		
		FString Label = ActorComp->GetName();
		const ImVec2 RowStart = ImGui::GetCursorScreenPos();
		const float RowWidth = ImGui::GetContentRegionAvail().x;

		const float RowPadding = std::max(ImGui::GetStyle().FramePadding.y, EditorUIScale::Px(6.0f));
		ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, RowPadding);
		ImGui::TreeNodeEx("ActorComponentNode", Flags, "%s", Label.c_str());
		ImGui::PopStyleVar();

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			OutSelectedComponent = ActorComp;
		}

		if (ImGui::BeginPopupContextItem("Component Context"))
		{
			if (ImGui::MenuItem("Delete", nullptr, false, ActorComp != Root))
				PendingDeleteComponent = ActorComp;
			ImGui::EndPopup();
		}

		const FString ClassName = std::format("({})", ActorComp->GetClass()->Name);
		const float TextX = RowStart.x + RowWidth - ImGui::CalcTextSize(ClassName.c_str()).x;
		ImGui::GetWindowDrawList()->AddText(ImVec2(TextX, RowStart.y + RowPadding), IM_COL32(200, 200, 200, 255), ClassName.c_str());

		ImGui::PopID();
	}
}

void FDetailsPanel::DrawSceneComponentNode(USceneComponent* Component, USceneComponent* Root, UActorComponent*& OutSelectedComponent)
{
	// 빌보드 같은 에디터 표시용 컴포넌트는 트리에 노출하지 않는다
	if (!Component || Component->IsHiddenInDetails())
		return;

	// 숨겨진 자식만 있는 노드가 펼침 화살표를 갖지 않도록 노출되는 자식만 센다
	TArray<USceneComponent*> Children;
	for (USceneComponent* Child : Component->GetAttachChildren())
	{
		if (Child && !Child->IsHiddenInDetails())
		{
			Children.Add(Child);
		}
	}

	const bool bIsLeaf = Children.IsEmpty();
	bIsEditingName = (EditingNameComponent == Component);

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding;
	if (Component == OutSelectedComponent)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (bIsLeaf)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	FString Label = Component->GetName();
	if (Component == Root)
		Label += " (Root)";

	ImGui::PushID(Component);
	const ImVec2 RowStart = ImGui::GetCursorScreenPos();
	const float RowWidth = ImGui::GetContentRegionAvail().x;
	// 위아래 여백도 클릭/드롭 영역에 포함해 작은 컴포넌트 행을 고르기 쉽게 한다.
	const float RowPadding = std::max(ImGui::GetStyle().FramePadding.y, EditorUIScale::Px(6.0f));
	ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, RowPadding);
	const bool bOpen = ImGui::TreeNodeEx("ComponentNode", Flags, "%s", bIsEditingName ? "" : Label.c_str());
	ImGui::PopStyleVar();

	USceneComponent* DragComponent = Component;

	// source: Root 제외, 현재 Component를 payload에 담음
	if (Component != Root && ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload(EditorDragDrop::Component, &Component, sizeof(USceneComponent*));
		ImGui::EndDragDropSource();
	}
	// target: 모든 행이 가능, payload를 받아 현재 Component를 부모로 사용
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Component))
		{
			USceneComponent* DroppedComponent = *static_cast<USceneComponent**>(Payload->Data);
			PendingDroppedComponent = DroppedComponent;
			PendingDragComponent = DragComponent;
		}
		ImGui::EndDragDropTarget();
	}

	if (!bIsEditingName)
	{
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			OutSelectedComponent = Component;

		// 이름 변경은 사용자가 추가한 컴포넌트만 (TEAM5). 더블 클릭, F2, 우클릭 메뉴로 시작한다.
		const bool bCanRename = Component != Root && Component->IsUserCreated();
		const auto BeginRename = [&]()
		{
			EditingNameComponent = Component;
			strncpy_s(ComponentNameEditBuffer, sizeof(ComponentNameEditBuffer), Component->GetName().c_str(), _TRUNCATE);
			bFocusNameEdit = true;
		};
		if (bCanRename && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			BeginRename();
		if (bCanRename && Component == OutSelectedComponent && !ImGui::GetIO().WantTextInput &&
			ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F2, false))
			BeginRename();

		if (ImGui::BeginPopupContextItem("Component Context"))
		{
			if (ImGui::MenuItem("Rename", "F2", false, bCanRename))
				BeginRename();
			if (!bCanRename && Component != Root && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Only components added in the editor can be renamed.\nNative components are matched by name when the scene loads.");
			if (ImGui::MenuItem("Make Root", nullptr, false, Component != Root && Component->GetOwner() == TargetActor))
				PendingRootComponent = Component;
			if (ImGui::MenuItem("Delete", nullptr, false, Component != Root))
				PendingDeleteComponent = Component;
			ImGui::EndPopup();
		}

		const FString ClassName = std::format("({})", Component->GetClass()->Name);
		const float TextX = RowStart.x + RowWidth - ImGui::CalcTextSize(ClassName.c_str()).x;
		ImGui::GetWindowDrawList()->AddText(ImVec2(TextX, RowStart.y + RowPadding), IM_COL32(200, 200, 200, 255), ClassName.c_str());
	}
	else
	{
		ImGui::SameLine();
		if (bFocusNameEdit)
		{
			ImGui::SetKeyboardFocusHere();
			bFocusNameEdit = false;
		}

		// 액터 이름 변경과 같이, 편집 없이 다른 곳을 눌러도(포커스 해제) 편집을 끝낸다.
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EditorUIScale::Px(6.0f), EditorUIScale::Px(3.0f)));
		ImGui::SetNextItemWidth(-FLT_MIN);
		const bool bConfirmed = ImGui::InputText("##RenameComponent", ComponentNameEditBuffer,
			sizeof(ComponentNameEditBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		const bool bDeactivated = ImGui::IsItemDeactivated();
		ImGui::PopStyleVar();
		const bool bCancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
		if (bCancelled || bConfirmed || bDeactivated)
		{
			const FName NewName(ComponentNameEditBuffer);
			if (!bCancelled && ComponentNameEditBuffer[0] != '\0' && NewName != NAME_None && NewName != Component->GetFName())
			{
				AActor* Owner = Component->GetOwner();

				UActorComponent* Existing = Owner->FindComponentByName(NewName);
				if (!Existing)
				{
					Component->SetName(FName(ComponentNameEditBuffer));
				}
				else
				{
					HTR_LOG(Warning, "A component with this name already exists in this Actor.");
				}
			}
			EditingNameComponent = nullptr;
		}
	}

	if (bOpen && !bIsLeaf)
	{
		for (USceneComponent* Child : Children)
		{
			DrawSceneComponentNode(Child, Root, OutSelectedComponent);
		}
		ImGui::TreePop();
	}

	ImGui::PopID();
}

void FDetailsPanel::TryReparent(USceneComponent* DroppedComponent, USceneComponent* DragComponent)
{
	if (DroppedComponent != nullptr 
		&& DroppedComponent->GetAttachParent() != DragComponent
		&& DroppedComponent != DragComponent)
	{
		DroppedComponent->SetupAttachment(DragComponent, EAttachmentRule::KeepWorld);
	}
}


FDetailsPanel::~FDetailsPanel()
{
	//delete transform;
}

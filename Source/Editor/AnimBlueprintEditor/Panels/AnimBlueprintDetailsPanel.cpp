#include "EnginePCH.h"
#include "AnimBlueprintDetailsPanel.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "Editor/AnimBlueprintEditor/AnimBlueprintEditor.h"
#include "Animation/AnimSequenceFile.h"
#include "Animation/BlendSpaceFile.h"
#include "Asset/AssetUtils.h"

FAnimBlueprintDetailsPanel::FAnimBlueprintDetailsPanel(FAnimBlueprintEditor& InEditor)
    : TAssetEditorPanel<FAnimBlueprintEditor>(InEditor, "Details", "AnimSequencePanel")
{
}

void FAnimBlueprintDetailsPanel::DrawContent()
{
    FAnimBlueprintNode* Node = Editor.GetSelectedNode();

    if (Node)
    {
        ImGui::PushID(Node->Id);
        ImGui::Text("Node: %s", Node->Name.c_str());
        ImGui::Text("ID: %d", Node->Id);
        ImGui::Separator();

        switch (Node->Type)
        {
        case EAnimBlueprintNodeType::SequencePlayer:
            DrawAssetSelector(Node->SequencePath, false);
            break;

        case EAnimBlueprintNodeType::BlendSpacePlayer:
            DrawAssetSelector(Node->BlendSpacePath, true);
            DrawFloatInput("Blend Input X", Node->FloatInput, false);
            break;

        case EAnimBlueprintNodeType::TwoWayBlend:
            DrawFloatInput("Alpha", Node->FloatInput, true);
            break;

        case EAnimBlueprintNodeType::StateMachine:
        case EAnimBlueprintNodeType::State:
            if (Node->Type == EAnimBlueprintNodeType::State)
            {
                ImGui::Checkbox("Reset On Entry", &Node->bResetOnEntry);
            }

            if (NamingNodeId != Node->Id)
            {
                NamingNodeId = Node->Id;
                std::snprintf(NodeNameBuffer, sizeof(NodeNameBuffer), "%s", Node->Name.c_str());
            }

            if (ImGui::InputText("Name", NodeNameBuffer, sizeof(NodeNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                Editor.RenameNode(Node->Id, NodeNameBuffer);
            }

            ImGui::TextUnformatted("Press Enter to rename.");

            if (ImGui::Button("Open Graph"))
            {
                Editor.OpenNodeGraph(Node->Id);
            }

            ImGui::TextWrapped(Node->Type == EAnimBlueprintNodeType::State ? "Connect Entry to choose the initial state. Connect State Out to another State In to create a transition." : "Double-click this node to edit states and transitions.");

            if (Node->Type == EAnimBlueprintNodeType::State)
            {
                DrawStateEvent("On Enter", Node->OnEnter);
                DrawStateEvent("On Exit", Node->OnExit);
            }
            break;

        case EAnimBlueprintNodeType::Entry:
            ImGui::TextWrapped("Connect this output to a State input to choose the initial state. A new connection replaces the previous initial state.");
            break;

        case EAnimBlueprintNodeType::AnyState:
            ImGui::TextWrapped("Connect to a State to create a transition from any current State. It will not transition to itself.");
            break;

        default:
            ImGui::TextUnformatted("Connect a pose to this output.");
            break;
        }

        if (Node->Type == EAnimBlueprintNodeType::SequencePlayer || Node->Type == EAnimBlueprintNodeType::BlendSpacePlayer)
        {
            ImGui::Checkbox("Loop", &Node->bLooping);
            ImGui::DragFloat("Play Rate", &Node->PlayRate, 0.01f, 0.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        }

        ImGui::TextWrapped("Apply Preview updates node settings and restarts playback.");
        ImGui::PopID();
    }
    else
    {
        NamingNodeId = INDEX_NONE;
        FAnimBlueprintTransition* Transition = Editor.GetSelectedTransition();

        if (Transition)
        {
            const FAnimBlueprintNode* From = Editor.FindNode(Transition->FromStateId);
            const FAnimBlueprintNode* To = Editor.FindNode(Transition->ToStateId);
            ImGui::TextUnformatted("State Transition");
            ImGui::Text("%s -> %s", From ? From->Name.c_str() : "Missing State", To ? To->Name.c_str() : "Missing State");
            DrawTransitionDetails(*Transition);
            ImGui::TextWrapped("Select the transition and press Delete to remove it. Each direction is a separate transition.");
        }
        else
        {
            ImGui::TextUnformatted("Select a node or state transition.");
        }
    }

    ImGui::Separator();
    DrawClassDefaults();
    ImGui::Separator();
    DrawVariables();
    ImGui::Separator();
    DrawPreviewVariables();
}

void FAnimBlueprintDetailsPanel::DrawAssetSelector(FString& AssetPath, bool bBlendSpace)
{
    const FString SelectedName = AssetPath.empty() ? "None" : std::filesystem::path(AssetPath).filename().generic_string();
    const bool bOpen = ImGui::BeginCombo(bBlendSpace ? "BlendSpace" : "Sequence", SelectedName.c_str());

    if (ImGui::IsItemHovered() && !AssetPath.empty())
    {
        ImGui::SetTooltip("%s", AssetPath.c_str());
    }

    if (!bOpen)
    {
        return;
    }

    if (ImGui::IsWindowAppearing())
    {
        RefreshAssetList(bBlendSpace);
    }

    if (ImGui::Selectable("None", AssetPath.empty()))
    {
        AssetPath.clear();
    }

    for (const FString& Path : AssetPaths)
    {
        const FString Name = std::filesystem::path(Path).filename().generic_string();
        const bool bSelected = AssetPath == Path;
        ImGui::PushID(Path.c_str());

        if (ImGui::Selectable(Name.c_str(), bSelected))
        {
            AssetPath = Path;
        }

        if (bSelected)
        {
            ImGui::SetItemDefaultFocus();
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", Path.c_str());
        }

        ImGui::PopID();
    }

    if (!AssetListMessage.empty())
    {
        ImGui::TextWrapped("%s", AssetListMessage.c_str());
    }
    else if (AssetPaths.empty())
    {
        ImGui::TextUnformatted(bBlendSpace ? "No .blendspace files in the preview mesh folder." : "No .anim.bin files in the preview mesh folder.");
    }

    ImGui::EndCombo();
}

void FAnimBlueprintDetailsPanel::RefreshAssetList(bool bBlendSpace)
{
    AssetPaths.clear();
    AssetListMessage.clear();

    const FString& PreviewMeshPath = Editor.GetBlueprintData().PreviewMeshPath;
    FString FilePath = FAssetUtils::ResolveAssetFilePath(PreviewMeshPath);

    if (FilePath.empty())
    {
        FilePath = PreviewMeshPath;
    }

    const std::filesystem::path Directory = std::filesystem::path(FilePath).parent_path();

    if (Directory.empty())
    {
        AssetListMessage = "Preview mesh folder is not available.";
        return;
    }

    std::error_code Error;
    std::filesystem::directory_iterator It(Directory, Error);
    const std::filesystem::directory_iterator End;

    while (!Error && It != End)
    {
        if (It->is_regular_file(Error))
        {
            const FString Path = It->path().generic_string();
            const bool bMatches = bBlendSpace ? BlendSpaceFile::IsBlendSpacePath(Path) : AnimSequenceFile::IsAnimSequencePath(Path);

            if (bMatches)
            {
                const FString AssetPath = FAssetUtils::GetAssetKey(Path);

                if (!AssetPath.empty())
                {
                    AssetPaths.push_back(AssetPath);
                }
            }
        }

        if (!Error)
        {
            It.increment(Error);
        }
    }

    std::sort(AssetPaths.begin(), AssetPaths.end());

    if (Error)
    {
        AssetListMessage = "Cannot read asset folder: " + Error.message();
    }
}

void FAnimBlueprintDetailsPanel::DrawFloatInput(const char* Label, FAnimBlueprintFloatInput& Input, bool bAlpha)
{
    ImGui::PushID(Label);
    ImGui::SeparatorText(Label);
    ImGui::Checkbox("Use Float Variable", &Input.bUseVariable);

    if (Input.bUseVariable)
    {
        DrawVariableSelector("Variable", Input.Variable, EAnimBlueprintVariableType::Float);
    }
    else if (bAlpha)
    {
        ImGui::SliderFloat("Constant", &Input.Constant, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    }
    else
    {
        ImGui::DragFloat("Constant", &Input.Constant, 1.0f);
    }

    ImGui::PopID();
}

void FAnimBlueprintDetailsPanel::DrawClassDefaults()
{
    // Unreal: AnimBP 에디터 툴바 Class Defaults > Details > Root Motion > Root Motion Mode
    if (!ImGui::CollapsingHeader("Class Defaults", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    ImGui::SeparatorText("Root Motion");
    static const char* const ModeNames[] = { "No Root Motion Extraction", "Ignore Root Motion", "Root Motion From Everything" };
    int Mode = static_cast<int>(Editor.GetRootMotionMode());
    if (ImGui::Combo("Root Motion Mode", &Mode, ModeNames, IM_ARRAYSIZE(ModeNames)))
    {
        Editor.SetRootMotionMode(static_cast<ERootMotionMode>(Mode));
        Editor.RequestApplyPreview();
    }
}

void FAnimBlueprintDetailsPanel::DrawVariables()
{
    if (!ImGui::CollapsingHeader("Variable Defaults", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    ImGui::InputText("Name", NewVariableName, sizeof(NewVariableName));
    ImGui::Combo("Type", &NewVariableType, "Float\0Bool\0");

    if (ImGui::Button("Add Variable"))
    {
        const EAnimBlueprintVariableType Type = NewVariableType == 0 ? EAnimBlueprintVariableType::Float : EAnimBlueprintVariableType::Bool;

        if (Editor.AddVariable(NewVariableName, Type))
        {
            NewVariableName[0] = '\0';
        }
    }

    FString RemoveName;
    bool bOpenRename = false;

    for (FAnimBlueprintVariable& Variable : Editor.GetVariables())
    {
        ImGui::PushID(Variable.Name.c_str());

        if (ImGui::TreeNode("Variable", "%s (%s)", Variable.Name.c_str(), Variable.Type == EAnimBlueprintVariableType::Float ? "Float" : "Bool"))
        {
            if (Variable.Type == EAnimBlueprintVariableType::Float)
            {
                ImGui::DragFloat("Default", &Variable.DefaultFloat, 0.1f);
            }
            else
            {
                ImGui::Checkbox("Default", &Variable.bDefaultBool);
            }

            if (ImGui::SmallButton("Rename"))
            {
                RenamingVariable = Variable.Name;
                std::snprintf(RenameBuffer, sizeof(RenameBuffer), "%s", Variable.Name.c_str());
                bOpenRename = true;
            }

            ImGui::SameLine();

            if (ImGui::SmallButton("Remove"))
            {
                RemoveName = Variable.Name;
            }

            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    if (!RemoveName.empty())
    {
        Editor.RemoveVariable(RemoveName);
    }

    if (bOpenRename)
    {
        ImGui::OpenPopup("Rename Variable");
    }

    if (ImGui::BeginPopup("Rename Variable"))
    {
        ImGui::InputText("New Name", RenameBuffer, sizeof(RenameBuffer));

        if (ImGui::Button("Rename"))
        {
            if (Editor.RenameVariable(RenamingVariable, RenameBuffer))
            {
                ImGui::CloseCurrentPopup();
            }
        }

        const FString& Message = Editor.GetVariableMessage();

        if (!Message.empty())
        {
            ImGui::TextWrapped("%s", Message.c_str());
        }

        ImGui::EndPopup();
    }

    const FString& Message = Editor.GetVariableMessage();

    if (!Message.empty())
    {
        ImGui::TextWrapped("%s", Message.c_str());
    }

    ImGui::TextWrapped("Defaults and bindings take effect when Apply Preview is pressed.");
}

void FAnimBlueprintDetailsPanel::DrawPreviewVariables()
{
    if (!ImGui::CollapsingHeader("Preview Values", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    const TArray<FAnimBlueprintVariable>& Variables = Editor.GetAppliedVariables();

    if (Variables.IsEmpty())
    {
        ImGui::TextUnformatted("Apply Preview to edit runtime values.");
        return;
    }

    ImGui::TextWrapped("These values update playback immediately and do not change defaults.");

    for (const FAnimBlueprintVariable& Variable : Variables)
    {
        ImGui::PushID(Variable.Name.c_str());

        if (Variable.Type == EAnimBlueprintVariableType::Float)
        {
            float Value = Editor.GetPreviewFloat(Variable.Name);

            if (ImGui::DragFloat(Variable.Name.c_str(), &Value, 0.1f))
            {
                Editor.SetPreviewFloat(Variable.Name, Value);
            }
        }
        else
        {
            bool bValue = Editor.GetPreviewBool(Variable.Name);

            if (ImGui::Checkbox(Variable.Name.c_str(), &bValue))
            {
                Editor.SetPreviewBool(Variable.Name, bValue);
            }
        }

        ImGui::PopID();
    }
}

void FAnimBlueprintDetailsPanel::DrawVariableSelector(const char* Label, FString& Name, EAnimBlueprintVariableType Type)
{
    const char* Selected = Name.empty() ? "Select variable" : Name.c_str();

    if (ImGui::BeginCombo(Label, Selected))
    {
        for (const FAnimBlueprintVariable& Variable : Editor.GetBlueprintData().Variables)
        {
            if (Variable.Type != Type)
            {
                continue;
            }

            if (ImGui::Selectable(Variable.Name.c_str(), Name == Variable.Name))
            {
                Name = Variable.Name;
            }
        }

        ImGui::EndCombo();
    }
}

void FAnimBlueprintDetailsPanel::DrawStateEvent(const char* Label, FAnimBlueprintStateEvent& Event)
{
    ImGui::PushID(Label);
    ImGui::SeparatorText(Label);

    char EventBuffer[128] = {};
    std::snprintf(EventBuffer, sizeof(EventBuffer), "%s", Event.EventName.c_str());

    if (ImGui::InputText("Event Name", EventBuffer, sizeof(EventBuffer)))
    {
        Event.EventName = EventBuffer;
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Sent as a named notify. Bind it with BindNotify (Lua) or override HandleNamedNotify (C++).");
    }

    const TArray<FAnimBlueprintVariable>& Variables = Editor.GetBlueprintData().Variables;
    int32 RemoveIndex = INDEX_NONE;

    for (int32 Index = 0; Index < Event.Assignments.Num(); ++Index)
    {
        FAnimBlueprintVariableAssignment& Assignment = Event.Assignments[Index];
        ImGui::PushID(Index);

        const FAnimBlueprintVariable* Variable = nullptr;
        for (const FAnimBlueprintVariable& Candidate : Variables)
        {
            if (Candidate.Name == Assignment.Variable)
            {
                Variable = &Candidate;
                break;
            }
        }

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
        if (ImGui::BeginCombo("##Variable", Assignment.Variable.empty() ? "Select variable" : Assignment.Variable.c_str()))
        {
            for (const FAnimBlueprintVariable& Candidate : Variables)
            {
                const FString ItemLabel = Candidate.Name + (Candidate.Type == EAnimBlueprintVariableType::Float ? " (Float)" : " (Bool)");

                if (ImGui::Selectable(ItemLabel.c_str(), Assignment.Variable == Candidate.Name))
                {
                    Assignment.Variable = Candidate.Name;
                }
            }

            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("=");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30.0f);

        if (!Variable)
        {
            ImGui::TextDisabled("-");
        }
        else if (Variable->Type == EAnimBlueprintVariableType::Bool)
        {
            ImGui::Checkbox("##Value", &Assignment.bBoolValue);
        }
        else
        {
            ImGui::DragFloat("##Value", &Assignment.FloatValue, 0.1f);
        }

        ImGui::SameLine();

        if (ImGui::SmallButton("X"))
        {
            RemoveIndex = Index;
        }

        ImGui::PopID();
    }

    if (RemoveIndex != INDEX_NONE)
    {
        Event.Assignments.RemoveAt(RemoveIndex);
    }

    if (ImGui::SmallButton("+ Set Variable"))
    {
        Event.Assignments.Add(FAnimBlueprintVariableAssignment());
    }

    ImGui::PopID();
}

void FAnimBlueprintDetailsPanel::DrawTransitionDetails(FAnimBlueprintTransition& Transition)
{
    ImGui::PushID(Transition.Id);
    int Rule = static_cast<int>(Transition.Rule);

    if (ImGui::Combo("Condition", &Rule, "Disabled\0Bool\0Float Compare\0Sequence Finished\0"))
    {
        Transition.Rule = static_cast<EAnimBlueprintTransitionRule>(Rule);
        Transition.Variable.clear();
    }

    switch (Transition.Rule)
    {
    case EAnimBlueprintTransitionRule::Disabled:
        ImGui::TextWrapped("This transition is disabled until a condition is selected.");
        break;

    case EAnimBlueprintTransitionRule::Bool:
        DrawVariableSelector("Variable", Transition.Variable, EAnimBlueprintVariableType::Bool);
        ImGui::Checkbox("Expected Value", &Transition.bExpected);
        break;

    case EAnimBlueprintTransitionRule::Float:
    {
        DrawVariableSelector("Variable", Transition.Variable, EAnimBlueprintVariableType::Float);
        int Compare = static_cast<int>(Transition.Compare);

        if (ImGui::Combo("Compare", &Compare, "<\0<=\0>\0>=\0==\0!=\0"))
        {
            Transition.Compare = static_cast<EAnimBlueprintCompare>(Compare);
        }

        ImGui::DragFloat("Value", &Transition.Value, 0.1f);
        break;
    }

    case EAnimBlueprintTransitionRule::Finished:
        ImGui::TextWrapped("The source State must output a non-looping Sequence Player directly. This condition cannot be used with Any State.");
        break;
    }

    ImGui::DragFloat("Crossfade (seconds)", &Transition.CrossfadeDuration, 0.01f, 0.0f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::InputInt("Priority", &Transition.Priority);
    ImGui::TextWrapped("Smaller priority values are checked first. Apply Preview to update transitions.");
    ImGui::PopID();
}

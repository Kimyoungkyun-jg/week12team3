#include "EnginePCH.h"
#include "AnimBlueprintFile.h"
#include "Core/Misc/Paths.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace
{
    constexpr int32 FileVersion = 1;
    constexpr std::streamoff MaxFileSize = 16 * 1024 * 1024;

    int32 ReadInt(const json& Object, const char* Key)
    {
        const json& Value = Object.at(Key);

        if (!Value.is_number_integer())
        {
            throw std::runtime_error(FString("Expected an integer: ") + Key);
        }

        if (Value.is_number_unsigned() && Value.get<uint64>() > static_cast<uint64>((std::numeric_limits<int32>::max)()))
        {
            throw std::runtime_error(FString("Integer is out of range: ") + Key);
        }

        const int64 Number = Value.get<int64>();

        if (Number < (std::numeric_limits<int32>::min)() || Number > (std::numeric_limits<int32>::max)())
        {
            throw std::runtime_error(FString("Integer is out of range: ") + Key);
        }

        return static_cast<int32>(Number);
    }

    template <typename T>
    T ReadEnum(const json& Object, const char* Key, int32 Maximum)
    {
        const int32 Value = ReadInt(Object, Key);

        if (Value < 0 || Value > Maximum)
        {
            throw std::runtime_error(FString("Unknown enum value: ") + Key);
        }

        return static_cast<T>(Value);
    }

    const json& ReadArray(const json& Object, const char* Key)
    {
        const json& Array = Object.at(Key);

        if (!Array.is_array())
        {
            throw std::runtime_error(FString("Expected an array: ") + Key);
        }

        return Array;
    }

    json WriteStateEvent(const FAnimBlueprintStateEvent& Value)
    {
        json Object;
        Object["EventName"] = Value.EventName;
        Object["Assignments"] = json::array();

        for (const FAnimBlueprintVariableAssignment& Assignment : Value.Assignments)
        {
            Object["Assignments"].push_back({ { "Variable", Assignment.Variable }, { "bBoolValue", Assignment.bBoolValue }, { "FloatValue", Assignment.FloatValue } });
        }

        return Object;
    }

    // OnEnter/OnExit가 생기기 전 파일에는 키가 없다. 없으면 빈 이벤트로 둔다.
    FAnimBlueprintStateEvent ReadStateEvent(const json& Node, const char* Key)
    {
        FAnimBlueprintStateEvent Value;
        const auto Found = Node.find(Key);

        if (Found == Node.end())
        {
            return Value;
        }

        if (!Found->is_object())
        {
            throw std::runtime_error(FString("Expected an object: ") + Key);
        }

        Value.EventName = Found->at("EventName").get<FString>();

        for (const json& Object : ReadArray(*Found, "Assignments"))
        {
            FAnimBlueprintVariableAssignment Assignment;
            Assignment.Variable = Object.at("Variable").get<FString>();
            Assignment.bBoolValue = Object.at("bBoolValue").get<bool>();
            Assignment.FloatValue = Object.at("FloatValue").get<float>();
            Value.Assignments.Add(std::move(Assignment));
        }

        return Value;
    }

    json WriteNode(const FAnimBlueprintNode& Value)
    {
        json Object;
        Object["Id"] = Value.Id;
        Object["Type"] = static_cast<int32>(Value.Type);
        Object["Name"] = Value.Name;
        Object["InputPosePinId"] = Value.InputPosePinId;
        Object["SecondInputPosePinId"] = Value.SecondInputPosePinId;
        Object["OutputPosePinId"] = Value.OutputPosePinId;
        Object["TransitionInputPinId"] = Value.TransitionInputPinId;
        Object["TransitionOutputPinId"] = Value.TransitionOutputPinId;
        Object["SequencePath"] = Value.SequencePath;
        Object["BlendSpacePath"] = Value.BlendSpacePath;
        Object["bLooping"] = Value.bLooping;
        Object["bResetOnEntry"] = Value.bResetOnEntry;
        Object["PlayRate"] = Value.PlayRate;
        Object["ChildGraphId"] = Value.ChildGraphId;
        Object["Position"] = { { "X", Value.Position.X }, { "Y", Value.Position.Y } };
        Object["FloatInput"] = { { "bUseVariable", Value.FloatInput.bUseVariable }, { "Variable", Value.FloatInput.Variable }, { "Constant", Value.FloatInput.Constant } };

        if (Value.Type == EAnimBlueprintNodeType::State)
        {
            Object["OnEnter"] = WriteStateEvent(Value.OnEnter);
            Object["OnExit"] = WriteStateEvent(Value.OnExit);
        }

        return Object;
    }

    FAnimBlueprintNode ReadNode(const json& Object)
    {
        FAnimBlueprintNode Value;
        Value.Id = ReadInt(Object, "Id");
        Value.Type = ReadEnum<EAnimBlueprintNodeType>(Object, "Type", 8);
        Value.Name = Object.at("Name").get<FString>();
        Value.InputPosePinId = ReadInt(Object, "InputPosePinId");
        Value.SecondInputPosePinId = ReadInt(Object, "SecondInputPosePinId");
        Value.OutputPosePinId = ReadInt(Object, "OutputPosePinId");
        Value.TransitionInputPinId = ReadInt(Object, "TransitionInputPinId");
        Value.TransitionOutputPinId = ReadInt(Object, "TransitionOutputPinId");
        Value.SequencePath = Object.at("SequencePath").get<FString>();
        Value.BlendSpacePath = Object.at("BlendSpacePath").get<FString>();
        Value.bLooping = Object.at("bLooping").get<bool>();
        Value.bResetOnEntry = Object.at("bResetOnEntry").get<bool>();
        Value.PlayRate = Object.at("PlayRate").get<float>();
        Value.ChildGraphId = ReadInt(Object, "ChildGraphId");
        const json& Position = Object.at("Position");
        Value.Position.X = Position.at("X").get<float>();
        Value.Position.Y = Position.at("Y").get<float>();
        const json& Input = Object.at("FloatInput");
        Value.FloatInput.bUseVariable = Input.at("bUseVariable").get<bool>();
        Value.FloatInput.Variable = Input.at("Variable").get<FString>();
        Value.FloatInput.Constant = Input.at("Constant").get<float>();
        Value.OnEnter = ReadStateEvent(Object, "OnEnter");
        Value.OnExit = ReadStateEvent(Object, "OnExit");
        return Value;
    }

    json WriteLink(const FAnimBlueprintLink& Value)
    {
        json Object;
        Object["Id"] = Value.Id;
        Object["FromNodeId"] = Value.FromNodeId;
        Object["FromPinId"] = Value.FromPinId;
        Object["ToNodeId"] = Value.ToNodeId;
        Object["ToPinId"] = Value.ToPinId;
        return Object;
    }

    FAnimBlueprintLink ReadLink(const json& Object)
    {
        FAnimBlueprintLink Value;
        Value.Id = ReadInt(Object, "Id");
        Value.FromNodeId = ReadInt(Object, "FromNodeId");
        Value.FromPinId = ReadInt(Object, "FromPinId");
        Value.ToNodeId = ReadInt(Object, "ToNodeId");
        Value.ToPinId = ReadInt(Object, "ToPinId");
        return Value;
    }

    json WriteTransition(const FAnimBlueprintTransition& Value)
    {
        json Object;
        Object["Id"] = Value.Id;
        Object["FromStateId"] = Value.FromStateId;
        Object["ToStateId"] = Value.ToStateId;
        Object["FromPinId"] = Value.FromPinId;
        Object["ToPinId"] = Value.ToPinId;
        Object["Rule"] = static_cast<int32>(Value.Rule);
        Object["Variable"] = Value.Variable;
        Object["bExpected"] = Value.bExpected;
        Object["Compare"] = static_cast<int32>(Value.Compare);
        Object["Value"] = Value.Value;
        Object["CrossfadeDuration"] = Value.CrossfadeDuration;
        Object["Priority"] = Value.Priority;
        return Object;
    }

    FAnimBlueprintTransition ReadTransition(const json& Object)
    {
        FAnimBlueprintTransition Value;
        Value.Id = ReadInt(Object, "Id");
        Value.FromStateId = ReadInt(Object, "FromStateId");
        Value.ToStateId = ReadInt(Object, "ToStateId");
        Value.FromPinId = ReadInt(Object, "FromPinId");
        Value.ToPinId = ReadInt(Object, "ToPinId");
        Value.Rule = ReadEnum<EAnimBlueprintTransitionRule>(Object, "Rule", 3);
        Value.Variable = Object.at("Variable").get<FString>();
        Value.bExpected = Object.at("bExpected").get<bool>();
        Value.Compare = ReadEnum<EAnimBlueprintCompare>(Object, "Compare", 5);
        Value.Value = Object.at("Value").get<float>();
        Value.CrossfadeDuration = Object.at("CrossfadeDuration").get<float>();
        Value.Priority = ReadInt(Object, "Priority");
        return Value;
    }

    json WriteVariable(const FAnimBlueprintVariable& Value)
    {
        json Object;
        Object["Name"] = Value.Name;
        Object["Type"] = static_cast<int32>(Value.Type);
        Object["DefaultFloat"] = Value.DefaultFloat;
        Object["bDefaultBool"] = Value.bDefaultBool;
        return Object;
    }

    FAnimBlueprintVariable ReadVariable(const json& Object)
    {
        FAnimBlueprintVariable Value;
        Value.Name = Object.at("Name").get<FString>();
        Value.Type = ReadEnum<EAnimBlueprintVariableType>(Object, "Type", 1);
        Value.DefaultFloat = Object.at("DefaultFloat").get<float>();
        Value.bDefaultBool = Object.at("bDefaultBool").get<bool>();
        return Value;
    }

    json WriteDocument(const FAnimBlueprintData& Data)
    {
        json Root;
        Root["Format"] = "HitoriAnimBlueprint";
        Root["Version"] = FileVersion;
        Root["PreviewMeshPath"] = Data.PreviewMeshPath;
        Root["RootMotionMode"] = static_cast<int32>(Data.RootMotionMode);
        Root["RootGraphId"] = Data.RootGraphId;
        Root["NextId"] = Data.NextId;
        Root["Variables"] = json::array();
        Root["Graphs"] = json::array();

        for (const FAnimBlueprintVariable& Variable : Data.Variables)
        {
            Root["Variables"].push_back(WriteVariable(Variable));
        }

        for (const FAnimBlueprintGraph& Graph : Data.Graphs)
        {
            json Object;
            Object["Id"] = Graph.Id;
            Object["Name"] = Graph.Name;
            Object["Type"] = static_cast<int32>(Graph.Type);
            Object["ParentGraphId"] = Graph.ParentGraphId;
            Object["OwnerNodeId"] = Graph.OwnerNodeId;
            Object["EditorSettings"] = Graph.EditorSettings;
            Object["Nodes"] = json::array();
            Object["Links"] = json::array();
            Object["Transitions"] = json::array();

            for (const FAnimBlueprintNode& Node : Graph.Nodes)
            {
                Object["Nodes"].push_back(WriteNode(Node));
            }

            for (const FAnimBlueprintLink& Link : Graph.Links)
            {
                Object["Links"].push_back(WriteLink(Link));
            }

            for (const FAnimBlueprintTransition& Transition : Graph.Transitions)
            {
                Object["Transitions"].push_back(WriteTransition(Transition));
            }

            Root["Graphs"].push_back(std::move(Object));
        }

        return Root;
    }

    FAnimBlueprintData ReadDocument(const json& Root)
    {
        if (Root.at("Format").get<FString>() != "HitoriAnimBlueprint" || ReadInt(Root, "Version") != FileVersion)
        {
            throw std::runtime_error("Unsupported AnimBlueprint format or version.");
        }

        FAnimBlueprintData Data;
        Data.PreviewMeshPath = Root.at("PreviewMeshPath").get<FString>();
        // 없으면 기본값 (이 필드가 생기기 전 파일)
        if (Root.contains("RootMotionMode"))
        {
            Data.RootMotionMode = ReadEnum<ERootMotionMode>(Root, "RootMotionMode", static_cast<int32>(ERootMotionMode::RootMotionFromEverything));
        }
        Data.RootGraphId = ReadInt(Root, "RootGraphId");
        Data.NextId = ReadInt(Root, "NextId");

        for (const json& Object : ReadArray(Root, "Variables"))
        {
            Data.Variables.Add(ReadVariable(Object));
        }

        for (const json& Object : ReadArray(Root, "Graphs"))
        {
            FAnimBlueprintGraph Graph;
            Graph.Id = ReadInt(Object, "Id");
            Graph.Name = Object.at("Name").get<FString>();
            Graph.Type = ReadEnum<EAnimBlueprintGraphType>(Object, "Type", 1);
            Graph.ParentGraphId = ReadInt(Object, "ParentGraphId");
            Graph.OwnerNodeId = ReadInt(Object, "OwnerNodeId");
            Graph.EditorSettings = Object.value("EditorSettings", FString());

            for (const json& Node : ReadArray(Object, "Nodes"))
            {
                Graph.Nodes.Add(ReadNode(Node));
            }

            for (const json& Link : ReadArray(Object, "Links"))
            {
                Graph.Links.Add(ReadLink(Link));
            }

            for (const json& Transition : ReadArray(Object, "Transitions"))
            {
                Graph.Transitions.Add(ReadTransition(Transition));
            }

            Data.Graphs.Add(std::move(Graph));
        }

        return Data;
    }

    const FAnimBlueprintNode* FindNode(const FAnimBlueprintGraph& Graph, int32 Id)
    {
        for (const FAnimBlueprintNode& Node : Graph.Nodes)
        {
            if (Node.Id == Id)
            {
                return &Node;
            }
        }

        return nullptr;
    }
}

bool AnimBlueprintFile::IsAnimBlueprintPath(const FString& Path)
{
    FString Extension = FPaths::FromUtf8(Path).extension().string();
    std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
    return Extension == ".animblueprint";
}

FAnimBlueprintData AnimBlueprintFile::CreateDefault(const FString& PreviewMeshPath)
{
    FAnimBlueprintData Data;
    Data.PreviewMeshPath = PreviewMeshPath;
    FAnimBlueprintGraph Graph;
    Graph.Id = Data.AllocateId();
    Graph.Name = "AnimGraph";

    FAnimBlueprintNode Output;
    Output.Id = Data.AllocateId();
    Output.Type = EAnimBlueprintNodeType::OutputPose;
    Output.Name = "Output Pose";
    Output.InputPosePinId = Data.AllocateId();
    Output.Position.X = 360.0f;
    Output.Position.Y = 100.0f;
    Graph.Nodes.Add(Output);

    Data.RootGraphId = Graph.Id;
    Data.Graphs.Add(Graph);
    FAnimBlueprintVariable Speed;
    Speed.Name = "GroundSpeed";
    Data.Variables.Add(Speed);
    return Data;
}

bool AnimBlueprintFile::ToJson(const FAnimBlueprintData& Data, FString& OutText, FString& OutError, bool bPretty)
{
    try
    {
        OutText = WriteDocument(Data).dump(bPretty ? 4 : -1);
        OutError.clear();
        return true;
    }
    catch (const std::exception& Error)
    {
        OutError = Error.what();
        return false;
    }
}

bool AnimBlueprintFile::Read(const std::filesystem::path& Path, FAnimBlueprintData& OutData, FString& OutError)
{
    try
    {
        std::ifstream File(Path, std::ios::binary | std::ios::ate);
        const std::streamoff Size = File.tellg();

        if (!File || Size <= 0 || Size > MaxFileSize)
        {
            OutError = "Cannot open AnimBlueprint or file size is invalid.";
            return false;
        }

        File.seekg(0);
        FAnimBlueprintData Data = ReadDocument(json::parse(File));

        if (!Validate(Data, OutError))
        {
            return false;
        }

        OutData = std::move(Data);
        return true;
    }
    catch (const std::exception& Error)
    {
        OutError = FString("Cannot read AnimBlueprint: ") + Error.what();
        return false;
    }
}

bool AnimBlueprintFile::Write(const std::filesystem::path& Path, const FAnimBlueprintData& Data, FString& OutError)
{
    if (!IsAnimBlueprintPath(FPaths::ToUtf8(Path)))
    {
        OutError = "Use the .animblueprint extension.";
        return false;
    }

    FString Text;

    if (!Validate(Data, OutError) || !ToJson(Data, Text, OutError, true))
    {
        return false;
    }

    if (Text.size() > static_cast<size_t>(MaxFileSize))
    {
        OutError = "AnimBlueprint exceeds the file size limit.";
        return false;
    }

    std::filesystem::path Temporary = Path;
    Temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
    std::ofstream File(Temporary, std::ios::binary | std::ios::trunc);
    File.write(Text.data(), static_cast<std::streamsize>(Text.size()));
    File.flush();
    const bool bWritten = static_cast<bool>(File);
    File.close();

    if (!bWritten || File.fail() || !MoveFileExW(Temporary.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::error_code Error;
        std::filesystem::remove(Temporary, Error);
        OutError = "Cannot replace AnimBlueprint file. The previous file was kept.";
        return false;
    }

    OutError.clear();
    return true;
}

bool AnimBlueprintFile::Validate(const FAnimBlueprintData& Data, FString& OutError)
{
    OutError.clear();
    std::unordered_set<int32> Ids;
    std::unordered_map<int32, const FAnimBlueprintGraph*> Graphs;
    int32 MaximumId = 0;

    auto Fail = [&OutError](const FString& Message)
    {
        OutError = Message;
        return false;
    };

    auto AddId = [&Ids, &MaximumId](int32 Id)
    {
        if (Id <= 0 || !Ids.insert(Id).second)
        {
            return false;
        }

        MaximumId = (std::max)(MaximumId, Id);
        return true;
    };

    if (Data.PreviewMeshPath.empty() || Data.Graphs.IsEmpty())
    {
        return Fail("A preview mesh and root graph are required.");
    }

    for (const FAnimBlueprintGraph& Graph : Data.Graphs)
    {
        if (!AddId(Graph.Id) || static_cast<int32>(Graph.Type) > 1)
        {
            return Fail("Graph ID or type is invalid.");
        }

        Graphs.emplace(Graph.Id, &Graph);

        for (const FAnimBlueprintNode& Node : Graph.Nodes)
        {
            if (!AddId(Node.Id) || !std::isfinite(Node.Position.X) || !std::isfinite(Node.Position.Y) || !std::isfinite(Node.PlayRate) || Node.PlayRate < 0.0f || !std::isfinite(Node.FloatInput.Constant))
            {
                return Fail("Node ID, position or playback setting is invalid.");
            }

            std::array<bool, 5> Required = { false, false, false, false, false };

            switch (Node.Type)
            {
            case EAnimBlueprintNodeType::OutputPose:
            case EAnimBlueprintNodeType::StateResult:
                Required[0] = true;
                break;
            case EAnimBlueprintNodeType::SequencePlayer:
            case EAnimBlueprintNodeType::BlendSpacePlayer:
            case EAnimBlueprintNodeType::StateMachine:
                Required[2] = true;
                break;
            case EAnimBlueprintNodeType::TwoWayBlend:
                Required[0] = true;
                Required[1] = true;
                Required[2] = true;
                break;
            case EAnimBlueprintNodeType::State:
                Required[3] = true;
                Required[4] = true;
                break;
            case EAnimBlueprintNodeType::Entry:
            case EAnimBlueprintNodeType::AnyState:
                Required[4] = true;
                break;
            default:
                return Fail("Unknown node type.");
            }

            const std::array<int32, 5> Pins = { Node.InputPosePinId, Node.SecondInputPosePinId, Node.OutputPosePinId, Node.TransitionInputPinId, Node.TransitionOutputPinId };

            for (size_t Index = 0; Index < Pins.size(); ++Index)
            {
                if ((Required[Index] && !AddId(Pins[Index])) || (!Required[Index] && Pins[Index] != INDEX_NONE))
                {
                    return Fail("A node has missing, duplicate or unexpected pins.");
                }
            }

            if (Node.Type != EAnimBlueprintNodeType::State && (!Node.OnEnter.IsEmpty() || !Node.OnExit.IsEmpty()))
            {
                return Fail("Only State nodes can have OnEnter/OnExit events.");
            }

            for (const FAnimBlueprintStateEvent* Event : { &Node.OnEnter, &Node.OnExit })
            {
                if (Event->EventName.size() >= 128)
                {
                    return Fail("A State event name is too long.");
                }

                for (const FAnimBlueprintVariableAssignment& Assignment : Event->Assignments)
                {
                    if (!std::isfinite(Assignment.FloatValue))
                    {
                        return Fail("A State event assigns an invalid value.");
                    }
                }
            }

            const bool bStateNode = Node.Type == EAnimBlueprintNodeType::Entry || Node.Type == EAnimBlueprintNodeType::State || Node.Type == EAnimBlueprintNodeType::AnyState;

            if (bStateNode != (Graph.Type == EAnimBlueprintGraphType::StateMachine))
            {
                return Fail("A node is in the wrong graph type.");
            }
        }
    }

    const auto Root = Graphs.find(Data.RootGraphId);

    if (Root == Graphs.end() || Root->second->Type != EAnimBlueprintGraphType::Pose || Root->second->ParentGraphId != INDEX_NONE || Root->second->OwnerNodeId != INDEX_NONE)
    {
        return Fail("The root graph reference is invalid.");
    }

    for (const FAnimBlueprintGraph& Graph : Data.Graphs)
    {
        if (Graph.Id != Data.RootGraphId)
        {
            const auto Parent = Graphs.find(Graph.ParentGraphId);
            const FAnimBlueprintNode* Owner = Parent == Graphs.end() ? nullptr : FindNode(*Parent->second, Graph.OwnerNodeId);

            if (!Owner || Owner->ChildGraphId != Graph.Id || (Owner->Type != EAnimBlueprintNodeType::State && Owner->Type != EAnimBlueprintNodeType::StateMachine))
            {
                return Fail("A child graph has an invalid owner.");
            }

            const EAnimBlueprintGraphType ExpectedType = Owner->Type == EAnimBlueprintNodeType::State ? EAnimBlueprintGraphType::Pose : EAnimBlueprintGraphType::StateMachine;

            if (Graph.Type != ExpectedType)
            {
                return Fail("A child graph type does not match its owner.");
            }
        }

        const FAnimBlueprintGraph* Ancestor = &Graph;
        int32 Depth = 0;

        while (Ancestor->Id != Data.RootGraphId)
        {
            const auto Parent = Graphs.find(Ancestor->ParentGraphId);

            if (Parent == Graphs.end() || ++Depth > Data.Graphs.Num())
            {
                return Fail("The graph hierarchy is disconnected or cyclic.");
            }

            Ancestor = Parent->second;
        }

        int32 Results = 0;
        int32 Entries = 0;
        int32 AnyStates = 0;
        std::unordered_map<int32, int32> Incoming;
        std::unordered_set<int32> ConnectedInputs;

        for (const FAnimBlueprintNode& Node : Graph.Nodes)
        {
            Incoming.emplace(Node.Id, 0);

            if (Node.Type == EAnimBlueprintNodeType::StateMachine || Node.Type == EAnimBlueprintNodeType::State)
            {
                const auto Child = Graphs.find(Node.ChildGraphId);

                if (Child == Graphs.end() || Child->second->OwnerNodeId != Node.Id || Child->second->ParentGraphId != Graph.Id)
                {
                    return Fail("A node references a missing or unrelated child graph.");
                }
            }
            else if (Node.ChildGraphId != INDEX_NONE)
            {
                return Fail("This node cannot own a child graph.");
            }

            if (Node.Type == EAnimBlueprintNodeType::OutputPose || Node.Type == EAnimBlueprintNodeType::StateResult)
            {
                const EAnimBlueprintNodeType Expected = Graph.Id == Data.RootGraphId ? EAnimBlueprintNodeType::OutputPose : EAnimBlueprintNodeType::StateResult;

                if (Node.Type != Expected)
                {
                    return Fail("The pose graph has the wrong result type.");
                }

                ++Results;
            }

            Entries += Node.Type == EAnimBlueprintNodeType::Entry ? 1 : 0;
            AnyStates += Node.Type == EAnimBlueprintNodeType::AnyState ? 1 : 0;
        }

        if ((Graph.Type == EAnimBlueprintGraphType::Pose && Results != 1) || (Graph.Type == EAnimBlueprintGraphType::StateMachine && (Entries != 1 || AnyStates > 1 || Graph.Links.Num() > 1)))
        {
            return Fail("The graph has invalid result, Entry or Any State nodes.");
        }

        for (const FAnimBlueprintLink& Link : Graph.Links)
        {
            const FAnimBlueprintNode* From = FindNode(Graph, Link.FromNodeId);
            const FAnimBlueprintNode* To = FindNode(Graph, Link.ToNodeId);

            if (!AddId(Link.Id) || !From || !To || From == To || !ConnectedInputs.insert(Link.ToPinId).second)
            {
                return Fail("A graph link has invalid endpoints or a duplicate input.");
            }

            if (Graph.Type == EAnimBlueprintGraphType::Pose)
            {
                if (From->OutputPosePinId != Link.FromPinId || (To->InputPosePinId != Link.ToPinId && To->SecondInputPosePinId != Link.ToPinId) || Link.FromPinId <= 0 || Link.ToPinId <= 0)
                {
                    return Fail("A pose link references invalid pins.");
                }

                ++Incoming[To->Id];
            }
            else if (From->Type != EAnimBlueprintNodeType::Entry || To->Type != EAnimBlueprintNodeType::State || From->TransitionOutputPinId != Link.FromPinId || To->TransitionInputPinId != Link.ToPinId)
            {
                return Fail("The initial State link is invalid.");
            }
        }

        if (Graph.Type == EAnimBlueprintGraphType::Pose)
        {
            TArray<int32> Pending;
            int32 Processed = 0;

            for (const auto& Item : Incoming)
            {
                if (Item.second == 0)
                {
                    Pending.Add(Item.first);
                }
            }

            while (!Pending.IsEmpty())
            {
                const int32 Id = Pending.Last();
                Pending.RemoveLast();
                ++Processed;

                for (const FAnimBlueprintLink& Link : Graph.Links)
                {
                    if (Link.FromNodeId == Id && --Incoming[Link.ToNodeId] == 0)
                    {
                        Pending.Add(Link.ToNodeId);
                    }
                }
            }

            if (Processed != Graph.Nodes.Num() || !Graph.Transitions.IsEmpty())
            {
                return Fail("A pose graph has a cycle or State transitions.");
            }
        }

        std::unordered_set<FString> TransitionPairs;

        for (const FAnimBlueprintTransition& Transition : Graph.Transitions)
        {
            const FAnimBlueprintNode* From = FindNode(Graph, Transition.FromStateId);
            const FAnimBlueprintNode* To = FindNode(Graph, Transition.ToStateId);
            const FString Pair = std::to_string(Transition.FromStateId) + ":" + std::to_string(Transition.ToStateId);

            if (!AddId(Transition.Id) || !From || !To || From == To || !TransitionPairs.insert(Pair).second || (From->Type != EAnimBlueprintNodeType::State && From->Type != EAnimBlueprintNodeType::AnyState) || To->Type != EAnimBlueprintNodeType::State || From->TransitionOutputPinId != Transition.FromPinId || To->TransitionInputPinId != Transition.ToPinId)
            {
                return Fail("A State transition is invalid or duplicated.");
            }

            if (static_cast<int32>(Transition.Rule) > 3 || static_cast<int32>(Transition.Compare) > 5 || !std::isfinite(Transition.Value) || !std::isfinite(Transition.CrossfadeDuration) || Transition.CrossfadeDuration < 0.0f)
            {
                return Fail("A transition condition or duration is invalid.");
            }
        }

        if (!Graph.EditorSettings.empty())
        {
            const json Settings = json::parse(Graph.EditorSettings, nullptr, false);

            if (Settings.is_discarded() || !Settings.is_object())
            {
                return Fail("Graph view settings are invalid JSON.");
            }
        }
    }

    std::unordered_set<FString> VariableNames;

    for (const FAnimBlueprintVariable& Variable : Data.Variables)
    {
        if (Variable.Name.empty() || Variable.Name.size() >= 128 || !VariableNames.insert(Variable.Name).second || static_cast<int32>(Variable.Type) > 1 || !std::isfinite(Variable.DefaultFloat))
        {
            return Fail("A variable name, type or default is invalid.");
        }
    }

    if (Data.NextId <= MaximumId || Data.NextId <= 0 || Data.NextId == (std::numeric_limits<int32>::max)())
    {
        return Fail("The next ID must be greater than all existing IDs.");
    }

    return true;
}

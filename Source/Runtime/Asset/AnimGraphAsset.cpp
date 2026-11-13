#include "EnginePCH.h"
#include "Asset/AnimGraphAsset.h"
#include "Core/ResourcePaths.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

namespace
{
	// 시퀀스는 AS_*.fbxbin이다. 예전 .animseq 키도 로더가 AS_ 키로 바꿔 읽으므로 받는다.
	bool HasPoseAssetExtension(EAnimGraphPose Pose, const FString& Path)
	{
		if (Pose == EAnimGraphPose::Sequence) return Path.ends_with(".fbxbin") || Path.ends_with(".animseq");
		return Path.ends_with(".blendspace");
	}

	fs::path ResolveAssetPathWithExt(const FString& Path, const char* Ext)
	{
		fs::path Relative(Path);
		if (Relative.extension() != Ext || Relative.is_absolute()) return {};
		if (Relative.begin() != Relative.end() && *Relative.begin() == "Assets") Relative = Relative.lexically_relative("Assets");
		for (const auto& Part : Relative) if (Part == ".." || Part == ".") return {};
		return Relative.empty() ? fs::path{} : fs::path(ContentPaths::AssetRoot) / Relative;
	}

	fs::path ResolvePath(const FString& Path)
	{
		return ResolveAssetPathWithExt(Path, ".animgraph");
	}

	bool ReadInt(const json& Object, const char* Key, int32& Out)
	{
		if (!Object.contains(Key) || !Object[Key].is_number_integer()) return false;
		Out = Object[Key].get<int32>();
		return true;
	}
	bool ReadFloat(const json& Object, const char* Key, float& Out)
	{
		if (!Object.contains(Key) || !Object[Key].is_number()) return false;
		Out = Object[Key].get<float>();
		return std::isfinite(Out);
	}
}

void FAnimGraphAsset::EnsureDefaultRootGraph()
{
	if (!RootNodes.empty())
	{
		SyncRootLinksFromNodes();
		return;
	}
	FRootAnimGraphNode SmNode;
	SmNode.Id = 1;
	SmNode.Type = ERootAnimNodeType::StateMachine;
	SmNode.Name = "Locomotion";
	SmNode.X = 60.0f;
	SmNode.Y = 120.0f;

	FRootAnimGraphNode OutNode;
	OutNode.Id = 2;
	OutNode.Type = ERootAnimNodeType::OutputPose;
	OutNode.Name = "Output Pose";
	OutNode.X = 440.0f;
	OutNode.Y = 120.0f;
	OutNode.SourceNodeId = bRootPoseConnected ? SmNode.Id : 0;

	RootNodes.push_back(std::move(SmNode));
	RootNodes.push_back(std::move(OutNode));
	SyncRootLinksFromNodes();
}

void FAnimGraphAsset::SyncRootLinksFromNodes()
{
	RootLinks.clear();
	for (const auto& Node : RootNodes)
	{
		if (Node.SourceNodeId > 0)
		{
			FRootAnimGraphLink Link;
			Link.FromNodeId = Node.SourceNodeId;
			Link.ToNodeId = Node.Id;
			Link.ToPinIndex = 0;
			RootLinks.push_back(Link);
		}
		if (Node.Type == ERootAnimNodeType::LayeredBlendPerBone && Node.SecondarySourceNodeId > 0)
		{
			FRootAnimGraphLink Link;
			Link.FromNodeId = Node.SecondarySourceNodeId;
			Link.ToNodeId = Node.Id;
			Link.ToPinIndex = 1;
			RootLinks.push_back(Link);
		}
	}
	for (const auto& Node : RootNodes)
	{
		if (Node.Type == ERootAnimNodeType::OutputPose)
		{
			bRootPoseConnected = (Node.SourceNodeId > 0);
			break;
		}
	}
}

void FAnimGraphAsset::ApplyRootLinksToNodes()
{
	for (auto& Node : RootNodes)
	{
		Node.SourceNodeId = 0;
		Node.SecondarySourceNodeId = 0;
	}
	for (const auto& Link : RootLinks)
	{
		auto It = std::find_if(RootNodes.begin(), RootNodes.end(),
			[&](const FRootAnimGraphNode& Item) { return Item.Id == Link.ToNodeId; });
		if (It == RootNodes.end()) continue;
		if (Link.ToPinIndex == 0) It->SourceNodeId = Link.FromNodeId;
		else if (Link.ToPinIndex == 1) It->SecondarySourceNodeId = Link.FromNodeId;
	}
	SyncRootLinksFromNodes();
}

bool FAnimGraphAsset::Validate(FString& Error) const
{
	if (States.empty()) { Error = "Graph needs at least one state."; return false; }
	if (!std::isfinite(AnyStateX) || !std::isfinite(AnyStateY)) { Error = "Invalid Any State position."; return false; }
	if (States.size() > 256 || Transitions.size() > 1024) { Error = "Graph is too large."; return false; }
	std::set<int32> StateIds, TransitionIds;
	for (const auto& State : States)
	{
		if (State.Id <= 0 || !StateIds.insert(State.Id).second || State.Name.empty() ||
			!std::isfinite(State.X) || !std::isfinite(State.Y) || !std::isfinite(State.PlayRate) || State.PlayRate <= 0 ||
			!std::isfinite(State.ConstantX) || !std::isfinite(State.ConstantY) ||
			!std::isfinite(State.PlayerOffsetX) || !std::isfinite(State.PlayerOffsetY) ||
			!std::isfinite(State.OutputOffsetX) || !std::isfinite(State.OutputOffsetY) ||
			!std::isfinite(State.SpeedOffsetX) || !std::isfinite(State.SpeedOffsetY) ||
			!std::isfinite(State.DirectionOffsetX) || !std::isfinite(State.DirectionOffsetY) ||
			static_cast<int>(State.Pose) < 0 || static_cast<int>(State.Pose) > 2 ||
			static_cast<int>(State.InputX) < 0 || static_cast<int>(State.InputX) > 2 ||
			static_cast<int>(State.InputY) < 0 || static_cast<int>(State.InputY) > 2)
		{ Error = "Invalid state."; return false; }
		if (State.AssetPath.empty() && State.OutputPlayerId == 0) { Error = "State has no animation asset: " + State.Name; return false; }
		if (!State.AssetPath.empty() && !HasPoseAssetExtension(State.Pose, State.AssetPath)) { Error = "Wrong asset type in state: " + State.Name; return false; }
		if (State.Players.size() > 256) { Error = "Too many player nodes in state: " + State.Name; return false; }
		std::set<int32> PlayerIds;
		for (const auto& Player : State.Players)
		{
			if (Player.Id <= 0 || !PlayerIds.insert(Player.Id).second ||
				static_cast<int>(Player.Pose) < 0 || static_cast<int>(Player.Pose) > 2 ||
				static_cast<int>(Player.InputX) < 0 || static_cast<int>(Player.InputX) > 2 ||
				static_cast<int>(Player.InputY) < 0 || static_cast<int>(Player.InputY) > 2 ||
				!std::isfinite(Player.X) || !std::isfinite(Player.Y) ||
				!std::isfinite(Player.ConstantX) || !std::isfinite(Player.ConstantY) ||
				!std::isfinite(Player.PlayRate) || Player.PlayRate <= 0 ||
				(!Player.AssetPath.empty() && !HasPoseAssetExtension(Player.Pose, Player.AssetPath)))
			{ Error = "Invalid player node in state: " + State.Name; return false; }
		}
		if (State.OutputPlayerId != 0)
		{
			const auto Player = std::find_if(State.Players.begin(), State.Players.end(),
				[&](const auto& Item) { return Item.Id == State.OutputPlayerId; });
			if (Player == State.Players.end() || (State.bPoseConnected && Player->AssetPath.empty()))
			{ Error = "Invalid Output Pose source in state: " + State.Name; return false; }
		}
	}
	if (!StateIds.contains(EntryState)) { Error = "Entry state is missing."; return false; }
	bool bHasAnyHitTransition = false;
	bool bHasAnyDeathTransition = false;
	for (const auto& Transition : Transitions)
	{
		if (Transition.Id <= 0 || !TransitionIds.insert(Transition.Id).second ||
			(Transition.From != -1 && !StateIds.contains(Transition.From)) || !StateIds.contains(Transition.To) || Transition.From == Transition.To ||
			static_cast<int>(Transition.Input) < 0 || static_cast<int>(Transition.Input) > static_cast<int>(EAnimGraphInput::DeathRequested) ||
			(Transition.From == -1 && ((Transition.Input != EAnimGraphInput::HitRequested && Transition.Input != EAnimGraphInput::DeathRequested) ||
				Transition.Compare != EAnimGraphCompare::Greater || Transition.Threshold != 0.0f ||
				(Transition.Input == EAnimGraphInput::HitRequested && bHasAnyHitTransition) ||
				(Transition.Input == EAnimGraphInput::DeathRequested && bHasAnyDeathTransition))) ||
			(Transition.Compare != EAnimGraphCompare::Always && Transition.Input == EAnimGraphInput::Constant) ||
			static_cast<int>(Transition.Compare) < 0 || static_cast<int>(Transition.Compare) > 4 ||
			!std::isfinite(Transition.Threshold) || !std::isfinite(Transition.BlendTime) || Transition.BlendTime < 0)
		{ Error = "Invalid transition."; return false; }
		if (Transition.From == -1 && Transition.Input == EAnimGraphInput::HitRequested) bHasAnyHitTransition = true;
		if (Transition.From == -1 && Transition.Input == EAnimGraphInput::DeathRequested) bHasAnyDeathTransition = true;
		if (Transition.Input == EAnimGraphInput::SequenceEnding ||
			Transition.Input == EAnimGraphInput::SequenceEndingGrounded ||
			Transition.Input == EAnimGraphInput::SequenceEndingAirborne)
		{
			const auto From = std::find_if(States.begin(), States.end(),
				[&](const FAnimGraphState& State) { return State.Id == Transition.From; });
			EAnimGraphPose FromPose = EAnimGraphPose::Sequence;
			if (From != States.end())
			{
				FromPose = From->Pose;
				if (From->OutputPlayerId != 0)
				{
					const auto Player = std::find_if(From->Players.begin(), From->Players.end(),
						[&](const auto& Item) { return Item.Id == From->OutputPlayerId; });
					if (Player != From->Players.end()) FromPose = Player->Pose;
				}
			}
			if (Transition.Threshold < 0.0f || From == States.end() || FromPose != EAnimGraphPose::Sequence)
			{ Error = "Sequence Ending requires a sequence state."; return false; }
		}
	}
	if (!RootNodes.empty())
	{
		std::set<int32> RootIds;
		int32 SmCount = 0;
		int32 OutCount = 0;
		for (const auto& Node : RootNodes)
		{
			if (Node.Id <= 0 || !RootIds.insert(Node.Id).second ||
				static_cast<int>(Node.Type) < 0 || static_cast<int>(Node.Type) > 5 ||
				!std::isfinite(Node.X) || !std::isfinite(Node.Y) ||
				!std::isfinite(Node.Alpha) || !std::isfinite(Node.ConstantPitch) ||
				!std::isfinite(Node.PlayRate) || Node.PlayRate <= 0.0f ||
				(Node.AlphaInput != EAnimGraphInput::Constant && Node.AlphaInput != EAnimGraphInput::IsAiming) ||
				(Node.Type == ERootAnimNodeType::SequencePlayer &&
				(Node.SequencePath.empty() || !HasPoseAssetExtension(EAnimGraphPose::Sequence, Node.SequencePath))))
			{ Error = "Invalid root graph node."; return false; }
			if (Node.Type == ERootAnimNodeType::StateMachine) ++SmCount;
			if (Node.Type == ERootAnimNodeType::OutputPose) ++OutCount;
		}
		if (SmCount != 1 || OutCount != 1)
		{ Error = "Root graph requires State Machine and Output Pose."; return false; }
	}
	Error.clear();
	return true;
}

bool FAnimGraphAsset::Load(const FString& Path, FAnimGraphAsset& Out, FString& Error)
{
	const fs::path File = ResolvePath(Path);
	if (File.empty()) { Error = "Invalid AnimGraph path."; return false; }
	std::ifstream In(File);
	if (!In) { Error = "Cannot open AnimGraph."; return false; }
	json Data = json::parse(In, nullptr, false);
	if (!Data.is_object() || !Data.contains("Version") || Data["Version"] != 1 ||
		!Data.contains("States") || !Data["States"].is_array() ||
		!Data.contains("Transitions") || !Data["Transitions"].is_array() ||
		!Data.contains("Skeleton") || !Data["Skeleton"].is_string())
	{ Error = "Invalid AnimGraph format."; return false; }
	FAnimGraphAsset Loaded;
	Loaded.SkeletonPath = Data["Skeleton"].get<FString>();
	if ((Data.contains("AnyStateX") && !ReadFloat(Data, "AnyStateX", Loaded.AnyStateX)) ||
		(Data.contains("AnyStateY") && !ReadFloat(Data, "AnyStateY", Loaded.AnyStateY)))
	{ Error = "Invalid Any State position."; return false; }
	if (Data.contains("RootPoseConnected"))
	{
		if (!Data["RootPoseConnected"].is_boolean()) { Error = "Invalid root pose connection."; return false; }
		Loaded.bRootPoseConnected = Data["RootPoseConnected"].get<bool>();
	}
	if (Data.contains("InvertDirection"))
	{
		if (!Data["InvertDirection"].is_boolean()) { Error = "Invalid direction setting."; return false; }
		Loaded.bInvertDirection = Data["InvertDirection"].get<bool>();
	}
	if (!ReadInt(Data, "EntryState", Loaded.EntryState)) { Error = "Missing entry state."; return false; }
	for (const auto& Item : Data["States"])
	{
		FAnimGraphState State;
		int32 Pose, InputX, InputY;
		if (!Item.is_object() || !ReadInt(Item, "Id", State.Id) || !Item.contains("Name") || !Item["Name"].is_string() ||
			!Item.contains("Asset") || !Item["Asset"].is_string() || !ReadFloat(Item, "X", State.X) ||
			!ReadFloat(Item, "Y", State.Y) || !ReadInt(Item, "Pose", Pose) || !ReadInt(Item, "InputX", InputX) ||
			!ReadInt(Item, "InputY", InputY) || !ReadFloat(Item, "ConstantX", State.ConstantX) ||
			!ReadFloat(Item, "ConstantY", State.ConstantY) || !ReadFloat(Item, "PlayRate", State.PlayRate) ||
			!Item.contains("Loop") || !Item["Loop"].is_boolean())
		{ Error = "Invalid state data."; return false; }
		State.Name = Item["Name"].get<FString>(); State.AssetPath = Item["Asset"].get<FString>();
		State.Pose = static_cast<EAnimGraphPose>(Pose); State.InputX = static_cast<EAnimGraphInput>(InputX);
		State.InputY = static_cast<EAnimGraphInput>(InputY); State.bLoop = Item["Loop"].get<bool>();
		if (Item.contains("PoseConnected"))
		{
			if (!Item["PoseConnected"].is_boolean()) { Error = "Invalid pose connection."; return false; }
			State.bPoseConnected = Item["PoseConnected"].get<bool>();
		}
		if (Item.contains("OutputPlayerId") && !ReadInt(Item, "OutputPlayerId", State.OutputPlayerId))
		{ Error = "Invalid Output Pose source."; return false; }
		if (Item.contains("Players"))
		{
			if (!Item["Players"].is_array()) { Error = "Invalid player nodes."; return false; }
			for (const auto& Node : Item["Players"])
			{
				FAnimGraphPlayerNode Player;
				int32 NodePose, NodeInputX, NodeInputY;
				if (!Node.is_object() || !ReadInt(Node, "Id", Player.Id) ||
					!ReadInt(Node, "Pose", NodePose) || !ReadInt(Node, "InputX", NodeInputX) ||
					!ReadInt(Node, "InputY", NodeInputY) || !ReadFloat(Node, "X", Player.X) ||
					!ReadFloat(Node, "Y", Player.Y) || !ReadFloat(Node, "ConstantX", Player.ConstantX) ||
					!ReadFloat(Node, "ConstantY", Player.ConstantY) || !ReadFloat(Node, "PlayRate", Player.PlayRate) ||
					!Node.contains("Asset") || !Node["Asset"].is_string() ||
					!Node.contains("Loop") || !Node["Loop"].is_boolean())
				{ Error = "Invalid player node data."; return false; }
				Player.Pose = static_cast<EAnimGraphPose>(NodePose);
				Player.InputX = static_cast<EAnimGraphInput>(NodeInputX);
				Player.InputY = static_cast<EAnimGraphInput>(NodeInputY);
				Player.AssetPath = Node["Asset"].get<FString>();
				Player.bLoop = Node["Loop"].get<bool>();
				State.Players.push_back(std::move(Player));
			}
		}
		struct FPositionField { const char* Key; float* Value; };
		const FPositionField Positions[] = {
			{ "PlayerOffsetX", &State.PlayerOffsetX }, { "PlayerOffsetY", &State.PlayerOffsetY },
			{ "OutputOffsetX", &State.OutputOffsetX }, { "OutputOffsetY", &State.OutputOffsetY },
			{ "SpeedOffsetX", &State.SpeedOffsetX }, { "SpeedOffsetY", &State.SpeedOffsetY },
			{ "DirectionOffsetX", &State.DirectionOffsetX }, { "DirectionOffsetY", &State.DirectionOffsetY }
		};
		for (const auto& Position : Positions)
			if (Item.contains(Position.Key) && !ReadFloat(Item, Position.Key, *Position.Value))
			{ Error = "Invalid node position."; return false; }
		Loaded.States.push_back(std::move(State));
	}
	for (const auto& Item : Data["Transitions"])
	{
		FAnimGraphTransition Transition;
		int32 Input, Compare;
		if (!Item.is_object() || !ReadInt(Item, "Id", Transition.Id) || !ReadInt(Item, "From", Transition.From) ||
			!ReadInt(Item, "To", Transition.To) || !ReadInt(Item, "Input", Input) || !ReadInt(Item, "Compare", Compare) ||
			!ReadFloat(Item, "Threshold", Transition.Threshold) || !ReadFloat(Item, "BlendTime", Transition.BlendTime))
		{ Error = "Invalid transition data."; return false; }
		Transition.Input = static_cast<EAnimGraphInput>(Input);
		Transition.Compare = static_cast<EAnimGraphCompare>(Compare);
		Loaded.Transitions.push_back(Transition);
	}
	if (Data.contains("RootNodes") && Data["RootNodes"].is_array())
	{
		for (const auto& Item : Data["RootNodes"])
		{
			FRootAnimGraphNode Node;
			int32 TypeInt = 0;
			if (!Item.is_object() || !ReadInt(Item, "Id", Node.Id) || !ReadInt(Item, "Type", TypeInt) ||
				!ReadFloat(Item, "X", Node.X) || !ReadFloat(Item, "Y", Node.Y))
			{ Error = "Invalid root node data."; return false; }
			Node.Type = static_cast<ERootAnimNodeType>(TypeInt);
			if (Item.contains("Name") && Item["Name"].is_string()) Node.Name = Item["Name"].get<FString>();
			if (Item.contains("SourceNodeId")) ReadInt(Item, "SourceNodeId", Node.SourceNodeId);
			if (Item.contains("SecondarySourceNodeId")) ReadInt(Item, "SecondarySourceNodeId", Node.SecondarySourceNodeId);
			if (Item.contains("SlotName") && Item["SlotName"].is_string()) Node.SlotName = Item["SlotName"].get<FString>();
			if (Node.Type == ERootAnimNodeType::LayeredBlendPerBone && Item.contains("StartBoneName") && Item["StartBoneName"].is_string())
				Node.StartBoneName = Item["StartBoneName"].get<FString>();
			if (Node.Type == ERootAnimNodeType::LayeredBlendPerBone && Item.contains("MeshSpaceRotationBlend") && Item["MeshSpaceRotationBlend"].is_boolean())
				Node.bMeshSpaceRotationBlend = Item["MeshSpaceRotationBlend"].get<bool>();
			if (Item.contains("AimOffsetPath") && Item["AimOffsetPath"].is_string()) Node.AimOffsetPath = Item["AimOffsetPath"].get<FString>();
			if (Item.contains("SequencePath") && Item["SequencePath"].is_string()) Node.SequencePath = Item["SequencePath"].get<FString>();
			if (Item.contains("Alpha")) ReadFloat(Item, "Alpha", Node.Alpha);
			int32 AlphaInputInt = static_cast<int32>(EAnimGraphInput::Constant);
			if (Item.contains("AlphaInput") && ReadInt(Item, "AlphaInput", AlphaInputInt))
				Node.AlphaInput = static_cast<EAnimGraphInput>(AlphaInputInt);
			if (Item.contains("PlayRate")) ReadFloat(Item, "PlayRate", Node.PlayRate);
			int32 PitchInputInt = static_cast<int32>(EAnimGraphInput::AimPitch);
			if (Item.contains("PitchInput") && ReadInt(Item, "PitchInput", PitchInputInt))
				Node.PitchInput = static_cast<EAnimGraphInput>(PitchInputInt);
			if (Item.contains("ConstantPitch")) ReadFloat(Item, "ConstantPitch", Node.ConstantPitch);
			Loaded.RootNodes.push_back(std::move(Node));
		}
		if (Data.contains("RootLinks") && Data["RootLinks"].is_array())
		{
			for (const auto& Item : Data["RootLinks"])
			{
				FRootAnimGraphLink Link;
				if (Item.is_object() && ReadInt(Item, "FromNodeId", Link.FromNodeId) &&
					ReadInt(Item, "ToNodeId", Link.ToNodeId) && ReadInt(Item, "ToPinIndex", Link.ToPinIndex))
				{
					Loaded.RootLinks.push_back(Link);
				}
			}
			Loaded.ApplyRootLinksToNodes();
		}
		else
		{
			Loaded.SyncRootLinksFromNodes();
		}
	}
	else
	{
		Loaded.EnsureDefaultRootGraph();
	}
	if (!Loaded.Validate(Error)) return false;
	Out = std::move(Loaded);
	return true;
}

bool FAnimGraphAsset::Save(const FString& Path, FString& Error) const
{
	if (!Validate(Error)) return false;
	const fs::path File = ResolvePath(Path);
	if (File.empty()) { Error = "Invalid AnimGraph path."; return false; }
	json Data = { { "Version", 1 }, { "Skeleton", SkeletonPath }, { "AnyStateX", AnyStateX }, { "AnyStateY", AnyStateY }, { "RootPoseConnected", bRootPoseConnected },
		{ "InvertDirection", bInvertDirection }, { "EntryState", EntryState },
		{ "States", json::array() }, { "Transitions", json::array() },
		{ "RootNodes", json::array() }, { "RootLinks", json::array() } };
	for (const auto& Node : RootNodes)
	{
		json NodeData = {
			{ "Id", Node.Id }, { "Type", static_cast<int>(Node.Type) }, { "Name", Node.Name },
			{ "X", Node.X }, { "Y", Node.Y },
			{ "SourceNodeId", Node.SourceNodeId }, { "SecondarySourceNodeId", Node.SecondarySourceNodeId },
			{ "SlotName", Node.SlotName },
			{ "AimOffsetPath", Node.AimOffsetPath }, { "SequencePath", Node.SequencePath },
			{ "Alpha", Node.Alpha }, { "AlphaInput", static_cast<int>(Node.AlphaInput) },
			{ "PlayRate", Node.PlayRate }, { "PitchInput", static_cast<int>(Node.PitchInput) },
			{ "ConstantPitch", Node.ConstantPitch }
		};
		if (Node.Type == ERootAnimNodeType::LayeredBlendPerBone)
		{
			NodeData["StartBoneName"] = Node.StartBoneName;
			NodeData["MeshSpaceRotationBlend"] = Node.bMeshSpaceRotationBlend;
		}
		Data["RootNodes"].push_back(std::move(NodeData));
	}
	for (const auto& Link : RootLinks)
	{
		Data["RootLinks"].push_back({
			{ "FromNodeId", Link.FromNodeId }, { "ToNodeId", Link.ToNodeId }, { "ToPinIndex", Link.ToPinIndex }
		});
	}
	for (const auto& State : States)
	{
		json Players = json::array();
		for (const auto& Player : State.Players)
			Players.push_back({ { "Id", Player.Id }, { "Pose", static_cast<int>(Player.Pose) },
				{ "Asset", Player.AssetPath }, { "InputX", static_cast<int>(Player.InputX) },
				{ "InputY", static_cast<int>(Player.InputY) }, { "ConstantX", Player.ConstantX },
				{ "ConstantY", Player.ConstantY }, { "PlayRate", Player.PlayRate },
				{ "Loop", Player.bLoop }, { "X", Player.X }, { "Y", Player.Y } });
		Data["States"].push_back({ { "Id", State.Id }, { "Name", State.Name }, { "X", State.X }, { "Y", State.Y },
			{ "Pose", static_cast<int>(State.Pose) }, { "Asset", State.AssetPath },
			{ "InputX", static_cast<int>(State.InputX) }, { "InputY", static_cast<int>(State.InputY) },
			{ "ConstantX", State.ConstantX }, { "ConstantY", State.ConstantY },
			{ "PlayRate", State.PlayRate }, { "Loop", State.bLoop }, { "PoseConnected", State.bPoseConnected },
			{ "OutputPlayerId", State.OutputPlayerId }, { "Players", std::move(Players) },
			{ "PlayerOffsetX", State.PlayerOffsetX }, { "PlayerOffsetY", State.PlayerOffsetY },
			{ "OutputOffsetX", State.OutputOffsetX }, { "OutputOffsetY", State.OutputOffsetY },
			{ "SpeedOffsetX", State.SpeedOffsetX }, { "SpeedOffsetY", State.SpeedOffsetY },
			{ "DirectionOffsetX", State.DirectionOffsetX }, { "DirectionOffsetY", State.DirectionOffsetY } });
	}
	for (const auto& Transition : Transitions)
		Data["Transitions"].push_back({ { "Id", Transition.Id }, { "From", Transition.From }, { "To", Transition.To },
			{ "Input", static_cast<int>(Transition.Input) }, { "Compare", static_cast<int>(Transition.Compare) },
			{ "Threshold", Transition.Threshold }, { "BlendTime", Transition.BlendTime } });
	std::error_code Ec;
	fs::create_directories(File.parent_path(), Ec);
	if (Ec) { Error = "Cannot create AnimGraph directory."; return false; }
	const fs::path Temp = File.string() + ".editing";
	std::ofstream Out(Temp, std::ios::trunc);
	if (!Out) { Error = "Cannot write AnimGraph."; return false; }
	Out << Data.dump(2);
	Out.close();
	if (!Out) { Error = "Cannot finish writing AnimGraph."; fs::remove(Temp, Ec); return false; }
	if (!MoveFileExW(Temp.c_str(), File.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{ Error = "Cannot replace AnimGraph."; fs::remove(Temp, Ec); return false; }
	Error.clear();
	return true;
}

bool FAnimMontageAsset::Validate(FString& Error) const
{
	if (SlotName.empty()) { Error = "Montage requires a slot name."; return false; }
	if (!std::isfinite(PlayRate) || PlayRate <= 0.0f) { Error = "Invalid montage play rate."; return false; }
	if (!std::isfinite(BlendInTime) || BlendInTime < 0.0f || !std::isfinite(BlendOutTime) || BlendOutTime < 0.0f)
	{ Error = "Invalid montage blend time."; return false; }
	for (const auto& Sec : Sections)
	{
		if (Sec.Name.empty() || !std::isfinite(Sec.StartTime) || !std::isfinite(Sec.EndTime) ||
			Sec.StartTime < 0.0f || Sec.EndTime < Sec.StartTime)
		{ Error = "Invalid montage section."; return false; }
	}
	for (const auto& Notify : Notifies)
	{
		if (Notify.Name.empty() || !std::isfinite(Notify.Time) || Notify.Time < 0.0f)
		{ Error = "Invalid montage notify."; return false; }
	}
	Error.clear();
	return true;
}

bool FAnimMontageAsset::Load(const FString& Path, FAnimMontageAsset& Out, FString& Error)
{
	const fs::path File = ResolveAssetPathWithExt(Path, ".animmontage");
	if (File.empty()) { Error = "Invalid AnimMontage path."; return false; }
	std::ifstream In(File);
	if (!In) { Error = "Cannot open AnimMontage."; return false; }
	json Data = json::parse(In, nullptr, false);
	if (!Data.is_object() || !Data.contains("Version") || Data["Version"] != 1)
	{ Error = "Invalid AnimMontage format."; return false; }
	FAnimMontageAsset Loaded;
	if (Data.contains("Skeleton") && Data["Skeleton"].is_string()) Loaded.SkeletonPath = Data["Skeleton"].get<FString>();
	if (Data.contains("Sequence") && Data["Sequence"].is_string()) Loaded.SequencePath = Data["Sequence"].get<FString>();
	if (Data.contains("Slot") && Data["Slot"].is_string()) Loaded.SlotName = Data["Slot"].get<FString>();
	if (Data.contains("PlayRate") && !ReadFloat(Data, "PlayRate", Loaded.PlayRate)) { Error = "Invalid PlayRate."; return false; }
	if (Data.contains("BlendInTime") && !ReadFloat(Data, "BlendInTime", Loaded.BlendInTime)) { Error = "Invalid BlendInTime."; return false; }
	if (Data.contains("BlendOutTime") && !ReadFloat(Data, "BlendOutTime", Loaded.BlendOutTime)) { Error = "Invalid BlendOutTime."; return false; }
	if (Data.contains("Loop") && Data["Loop"].is_boolean()) Loaded.bLoop = Data["Loop"].get<bool>();
	if (Data.contains("Sections") && Data["Sections"].is_array())
	{
		for (const auto& Item : Data["Sections"])
		{
			FAnimMontageSection Sec;
			if (!Item.is_object() || !Item.contains("Name") || !Item["Name"].is_string() ||
				!ReadFloat(Item, "StartTime", Sec.StartTime) || !ReadFloat(Item, "EndTime", Sec.EndTime))
			{ Error = "Invalid section entry."; return false; }
			Sec.Name = Item["Name"].get<FString>();
			if (Item.contains("NextSection") && Item["NextSection"].is_string())
				Sec.NextSection = Item["NextSection"].get<FString>();
			Loaded.Sections.push_back(std::move(Sec));
		}
	}
	if (Data.contains("Notifies") && Data["Notifies"].is_array())
	{
		for (const auto& Item : Data["Notifies"])
		{
			FAnimMontageNotify Notify;
			if (!Item.is_object() || !Item.contains("Name") || !Item["Name"].is_string() ||
				!ReadFloat(Item, "Time", Notify.Time))
			{ Error = "Invalid notify entry."; return false; }
			Notify.Name = Item["Name"].get<FString>();
			Loaded.Notifies.push_back(std::move(Notify));
		}
	}
	if (!Loaded.Validate(Error)) return false;
	Out = std::move(Loaded);
	return true;
}

bool FAnimMontageAsset::Save(const FString& Path, FString& Error) const
{
	if (!Validate(Error)) return false;
	const fs::path File = ResolveAssetPathWithExt(Path, ".animmontage");
	if (File.empty()) { Error = "Invalid AnimMontage path."; return false; }
	json Data = {
		{ "Version", 1 },
		{ "Skeleton", SkeletonPath },
		{ "Sequence", SequencePath },
		{ "Slot", SlotName },
		{ "PlayRate", PlayRate },
		{ "BlendInTime", BlendInTime },
		{ "BlendOutTime", BlendOutTime },
		{ "Loop", bLoop },
		{ "Sections", json::array() },
		{ "Notifies", json::array() }
	};
	for (const auto& Sec : Sections)
	{
		Data["Sections"].push_back({
			{ "Name", Sec.Name },
			{ "StartTime", Sec.StartTime },
			{ "EndTime", Sec.EndTime },
			{ "NextSection", Sec.NextSection }
		});
	}
	for (const auto& Notify : Notifies)
	{
		Data["Notifies"].push_back({
			{ "Name", Notify.Name },
			{ "Time", Notify.Time }
		});
	}
	std::error_code Ec;
	fs::create_directories(File.parent_path(), Ec);
	if (Ec) { Error = "Cannot create AnimMontage directory."; return false; }
	const fs::path Temp = File.string() + ".editing";
	std::ofstream Out(Temp, std::ios::trunc);
	if (!Out) { Error = "Cannot write AnimMontage."; return false; }
	Out << Data.dump(2);
	Out.close();
	if (!Out) { Error = "Cannot finish writing AnimMontage."; fs::remove(Temp, Ec); return false; }
	if (!MoveFileExW(Temp.c_str(), File.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{ Error = "Cannot replace AnimMontage."; fs::remove(Temp, Ec); return false; }
	Error.clear();
	return true;
}

bool FAimOffsetAsset::Validate(FString& Error) const
{
	if (!std::isfinite(MinPitch) || !std::isfinite(MaxPitch) || MinPitch >= MaxPitch)
	{ Error = "Invalid Pitch range."; return false; }
	for (const auto& Sample : Samples)
	{
		if (!std::isfinite(Sample.Pitch) || Sample.FrameIndex < 0)
		{ Error = "Invalid AimOffset sample."; return false; }
	}
	Error.clear();
	return true;
}

bool FAimOffsetAsset::Load(const FString& Path, FAimOffsetAsset& Out, FString& Error)
{
	const fs::path File = ResolveAssetPathWithExt(Path, ".aimoffset");
	if (File.empty()) { Error = "Invalid AimOffset path."; return false; }
	std::ifstream In(File);
	if (!In) { Error = "Cannot open AimOffset."; return false; }
	json Data = json::parse(In, nullptr, false);
	if (!Data.is_object() || !Data.contains("Version") || Data["Version"] != 1)
	{ Error = "Invalid AimOffset format."; return false; }
	FAimOffsetAsset Loaded;
	if (Data.contains("Skeleton") && Data["Skeleton"].is_string()) Loaded.SkeletonPath = Data["Skeleton"].get<FString>();
	if (Data.contains("BaseSequence") && Data["BaseSequence"].is_string()) Loaded.BaseSequencePath = Data["BaseSequence"].get<FString>();
	if (Data.contains("BaseFrameIndex")) ReadInt(Data, "BaseFrameIndex", Loaded.BaseFrameIndex);
	if (Data.contains("MinPitch") && !ReadFloat(Data, "MinPitch", Loaded.MinPitch)) { Error = "Invalid MinPitch."; return false; }
	if (Data.contains("MaxPitch") && !ReadFloat(Data, "MaxPitch", Loaded.MaxPitch)) { Error = "Invalid MaxPitch."; return false; }
	if (Data.contains("Samples") && Data["Samples"].is_array())
	{
		for (const auto& Item : Data["Samples"])
		{
			FAimOffsetSample Sample;
			if (!Item.is_object() || !ReadFloat(Item, "Pitch", Sample.Pitch) ||
				!Item.contains("Sequence") || !Item["Sequence"].is_string())
			{ Error = "Invalid AimOffset sample."; return false; }
			Sample.SequencePath = Item["Sequence"].get<FString>();
			if (Item.contains("FrameIndex")) ReadInt(Item, "FrameIndex", Sample.FrameIndex);
			Loaded.Samples.push_back(std::move(Sample));
		}
	}
	if (!Loaded.Validate(Error)) return false;
	Out = std::move(Loaded);
	return true;
}

bool FAimOffsetAsset::Save(const FString& Path, FString& Error) const
{
	if (!Validate(Error)) return false;
	const fs::path File = ResolveAssetPathWithExt(Path, ".aimoffset");
	if (File.empty()) { Error = "Invalid AimOffset path."; return false; }
	json Data = {
		{ "Version", 1 },
		{ "Skeleton", SkeletonPath },
		{ "BaseSequence", BaseSequencePath },
		{ "BaseFrameIndex", BaseFrameIndex },
		{ "MinPitch", MinPitch },
		{ "MaxPitch", MaxPitch },
		{ "Samples", json::array() }
	};
	for (const auto& Sample : Samples)
	{
		Data["Samples"].push_back({
			{ "Pitch", Sample.Pitch },
			{ "Sequence", Sample.SequencePath },
			{ "FrameIndex", Sample.FrameIndex }
		});
	}
	std::error_code Ec;
	fs::create_directories(File.parent_path(), Ec);
	if (Ec) { Error = "Cannot create AimOffset directory."; return false; }
	const fs::path Temp = File.string() + ".editing";
	std::ofstream Out(Temp, std::ios::trunc);
	if (!Out) { Error = "Cannot write AimOffset."; return false; }
	Out << Data.dump(2);
	Out.close();
	if (!Out) { Error = "Cannot finish writing AimOffset."; fs::remove(Temp, Ec); return false; }
	if (!MoveFileExW(Temp.c_str(), File.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{ Error = "Cannot replace AimOffset."; fs::remove(Temp, Ec); return false; }
	Error.clear();
	return true;
}

#include "EnginePCH.h"
#include "Editor/OutputLog/OutputLogPanel.h"

#include "Core/StatOverlay.h"
#include "Render/ShadowRendering.h"

#include "Editor/Settings/SettingsPanel.h"
#include "Components/SkeletalMeshComponent.h"

#include <algorithm>
#include <charconv>
#include <sstream>
#include "Editor/EditorUI/EditorUIScale.h"

static int TextEditCallbackStub(ImGuiInputTextCallbackData* data)
{
	FOutputLogPanel* OutputLog = (FOutputLogPanel*)data->UserData;
	return OutputLog->TextEditCallback(data);
}

static int   Strnicmp(const char* s1, const char* s2, int n) { int d = 0; while (n > 0 && (d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; n--; } return d; }

// 콘솔 명령은 입력 대소문자를 가리지 않는다. "stat fps"와 "STAT FPS"가 같아야 한다.
static bool EqualsIgnoreCase(const FString& A, const char* B)
{
	return Strnicmp(A.c_str(), B, static_cast<int>(A.size()) + 1) == 0;
}

namespace
{
	// 렌더러가 매 프레임 읽는 그림자 콘솔 변수 (ShadowRendering.h GShadowCVars). 이름은 언리얼과 같다.
	// MaxResolution·MaxCSMResolution은 Settings 패널의 ShadowMap 크기가 정하므로 여기 두지 않는다.
	struct FConsoleVariable
	{
		const char* Name;
		const char* Help;
		float* FloatValue;
		uint32* UIntValue;
		float Min;
		float Max;
	};

	const TArray<FConsoleVariable>& GetConsoleVariables()
	{
		FShadowConsoleVariables& V = GShadowCVars;
		static const TArray<FConsoleVariable> Variables = {
			{ "r.ShadowQuality", "0 = shadows off, 1~5 = max PCF level (1 tap, 3x3, 5x5, 7x7)", nullptr, &V.ShadowQuality, 0.0f, 5.0f },
			{ "r.Shadow.FilterMethod", "0 = PCF, 1 = PCSS (Directional/Spot)", nullptr, &V.FilterMethod, 0.0f, 1.0f },
			{ "r.Shadow.MaxSoftKernelSize", "PCSS max kernel radius in texels", nullptr, &V.MaxSoftKernelSize, 1.0f, 128.0f },
			{ "r.Shadow.CSM.MaxCascades", "Max cascades for directional lights", nullptr, &V.CSMMaxCascades, 1.0f, 4.0f },
			{ "r.Shadow.TexelsPerPixel", "Spot/Point shadow texels per screen pixel", &V.TexelsPerPixel, nullptr, 0.1f, 8.0f },
			{ "r.Shadow.MinResolution", "Spot/Point shadows below this size are not drawn", nullptr, &V.MinResolution, 8.0f, 4096.0f },
			{ "r.Shadow.FadeResolution", "Spot/Point shadows fade out below this size", nullptr, &V.FadeResolution, 8.0f, 4096.0f },
			{ "r.Shadow.FadeExponent", "Fade curve exponent", &V.FadeExponent, nullptr, 0.01f, 4.0f },
			{ "r.Shadow.RadiusThreshold", "Cull casters smaller than this screen radius", &V.RadiusThreshold, nullptr, 0.0f, 1.0f },
			{ "r.Shadow.CSMDepthBias", "Directional constant depth bias", &V.CSMDepthBias, nullptr, 0.0f, 100.0f },
			{ "r.Shadow.CSMSlopeScaleDepthBias", "Directional slope depth bias scale", &V.CSMSlopeScaleDepthBias, nullptr, 0.0f, 100.0f },
			{ "r.Shadow.SpotLightDepthBias", "Spot constant depth bias", &V.SpotLightDepthBias, nullptr, 0.0f, 100.0f },
			{ "r.Shadow.SpotLightSlopeDepthBias", "Spot slope depth bias scale", &V.SpotLightSlopeDepthBias, nullptr, 0.0f, 100.0f },
			{ "r.Shadow.PointLightDepthBias", "Point constant depth bias", &V.PointLightDepthBias, nullptr, 0.0f, 100.0f },
			{ "r.Shadow.PointLightSlopeScaleDepthBias", "Point slope depth bias scale", &V.PointLightSlopeScaleDepthBias, nullptr, 0.0f, 100.0f },
			{ "r.Shadow.ShadowMaxSlopeScaleDepthBias", "Max slope used by slope depth bias", &V.ShadowMaxSlopeScaleDepthBias, nullptr, 0.0f, 10.0f },
		};
		return Variables;
	}

	const FConsoleVariable* FindConsoleVariable(const FString& Name)
	{
		for (const FConsoleVariable& Variable : GetConsoleVariables())
			if (EqualsIgnoreCase(Name, Variable.Name))
				return &Variable;
		return nullptr;
	}

	FString FormatConsoleVariable(const FConsoleVariable& Variable)
	{
		return Variable.FloatValue ? std::format("{} = {}", Variable.Name, *Variable.FloatValue)
			: std::format("{} = {}", Variable.Name, *Variable.UIntValue);
	}

	// "r.X" 만 입력하면 현재 값, "r.X <값>"이면 범위로 자른 뒤 바꾼다.
	void ExecConsoleVariable(const FConsoleVariable& Variable, const TArray<FString>& Tokens)
	{
		if (Tokens.Num() == 1)
		{
			FLog::Log(ELogVerbosity::Info, "{}  ({}, range {} ~ {})", FormatConsoleVariable(Variable), Variable.Help, Variable.Min, Variable.Max);
			return;
		}

		const FString& ValueStr = Tokens[1];
		float Value = 0.0f;
		const auto [End, Error] = std::from_chars(ValueStr.data(), ValueStr.data() + ValueStr.size(), Value);
		if (Tokens.Num() > 2 || Error != std::errc{} || End != ValueStr.data() + ValueStr.size() || !std::isfinite(Value))
		{
			FLog::Log(ELogVerbosity::Warning, "Usage: {} <{} ~ {}>", Variable.Name, Variable.Min, Variable.Max);
			return;
		}

		Value = std::clamp(Value, Variable.Min, Variable.Max);
		if (Variable.FloatValue)
			*Variable.FloatValue = Value;
		else
			*Variable.UIntValue = static_cast<uint32>(Value + 0.5f);
		FLog::Log(ELogVerbosity::Info, "{}", FormatConsoleVariable(Variable));
	}

	void Tokenize(const FString& Line, TArray<FString>& OutTokens)
	{
		OutTokens.Reset();
		std::istringstream Stream(Line);
		FString Token;
		while (Stream >> Token)
			OutTokens.Add(Token);
	}
}

bool FOutputLogPanel::Init()
{
	ClearLog();

	// Tab 자동 완성 후보. HELP가 이 목록과 사용법을 함께 보여 준다.
	Commands.Add("HELP");
	Commands.Add("HISTORY");
	Commands.Add("CLEAR");
	Commands.Add("STAT");
	Commands.Add("CSM");
	Commands.Add("r.shadow.CacheWholeSceneShadows");
	Commands.Add("r.SkinningMode");
	Commands.Add("r.GPUSkinning");
	Commands.Add("r.Skinning.ForceUpdate");
	for (const FConsoleVariable& Variable : GetConsoleVariables())
		Commands.Add(Variable.Name);

	HistoryPos = -1;
	InputBuf[0] = '\0';

	AutoScroll = true;
	ScrollToBottom = false;

	// MultiSelect는 표시 순번(필터 적용 후)을 넘겨주므로, 저장할 때는 Items 인덱스로 바꾼다.
	Selection.UserData = this;
	Selection.AdapterIndexToStorageId = [](ImGuiSelectionBasicStorage* Self, int Index)
	{
		const FOutputLogPanel* Panel = static_cast<const FOutputLogPanel*>(Self->UserData);
		return static_cast<ImGuiID>(Panel->VisibleItems[Index]);
	};

	return true;
}

void FOutputLogPanel::Tick(float DeltaTime)
{
}

void FOutputLogPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(520.0f), EditorUIScale::Px(600.0f)), ImGuiCond_FirstUseEver);

	if (!BeginPanel())
	{
		ImGui::End();
		return;
	}

	//////////////////////////////////////////////////////////

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EditorUIScale::Px(10.0f), EditorUIScale::Px(5.0f)));
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("Filter");
	ImGui::SameLine();

	Filter.Draw("##Filter", EditorUIScale::Px(180.0f));

	ImGui::SameLine();
	ImGui::TextDisabled("[?]");

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip(
			"Filter syntax:\n"
			"error        Include matching lines\n"
			"-warning     Exclude matching lines\n"
			"error,-warn  Combine filters"
		);
	}

	EditorUIScale::SameLineIfFits(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize("Auto-scroll").x);
	ImGui::Checkbox("Auto-scroll", &AutoScroll);

	const float ButtonWidth = std::max(EditorUIScale::Px(72.0f), EditorUIScale::ButtonWidth("Clear"));
	EditorUIScale::SameLineIfFits(ButtonWidth);
	if (ImGui::Button("Clear", ImVec2(ButtonWidth, 0.0f)))
		ClearLog();

	EditorUIScale::SameLineIfFits(ButtonWidth);
	if (ImGui::Button("Copy", ImVec2(ButtonWidth, 0.0f)))
		ImGui::SetClipboardText(BuildClipboardText(false).c_str());
	ImGui::SetItemTooltip("Copy all lines that pass the filter.\nTo copy only some lines, select them and press Ctrl+C.");
	ImGui::PopStyleVar();

	ImGui::Separator();

	//////////////////////////////////////////////////////////

	ImGuiStyle& style = ImGui::GetStyle();
	const float footer_height_to_reserve = style.SeparatorSize + style.ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_HorizontalScrollbar)) // ImGui Window 안에 Child Window를 생성
	{
		RenderLogLines();

		if (ImGui::BeginPopupContextWindow())
		{
			if (ImGui::Selectable("Copy Selected", false, Selection.Size > 0 ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled))
				ImGui::SetClipboardText(BuildClipboardText(true).c_str());
			if (ImGui::Selectable("Copy All"))
				ImGui::SetClipboardText(BuildClipboardText(false).c_str());
			ImGui::Separator();
			if (ImGui::Selectable("Clear"))
				ClearLog();
			ImGui::EndPopup();
		}

		if (ScrollToBottom || (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
			ImGui::SetScrollHereY(1.0f);
		ScrollToBottom = false;
	}
	ImGui::EndChild();
	ImGui::Separator();

	//////////////////////////////////////////////////////////

	bool reclaim_focus = false;
	ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
	ImGui::TextDisabled(">");

	ImGui::SameLine();

	ImGui::SetNextItemWidth(-1.0f);

	if (ImGui::InputTextWithHint("##OutputLogInput", "Enter command...  (HELP for commands)", InputBuf, IM_COUNTOF(InputBuf), input_text_flags, &TextEditCallbackStub, this))
	{
		char* s = InputBuf;
		if (s[0])
		{
			ExecCommand(s);
		}
		s[0] = 0;
		reclaim_focus = true;
	}

	ImGui::SetItemDefaultFocus();
	if (reclaim_focus)
		ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget

	//////////////////////////////////////////////////////////

	ImGui::End();
}

// 줄마다 Selectable을 두고 MultiSelect로 묶는다. 클릭, Shift/Ctrl 클릭, 드래그(Box Select), Ctrl+A로 고르고 Ctrl+C로 복사한다.
// Selectable 라벨에 메시지를 그대로 넣으면 "##"이 ID 구분자로 해석되므로, 라벨은 비우고 글자는 따로 그린다.
void FOutputLogPanel::RenderLogLines()
{
	VisibleItems.Reset();
	for (int32 Index = 0; Index < Items.Num(); ++Index)
		if (Filter.PassFilter(Items[Index].message.c_str()))
			VisibleItems.Add(Index);

	const ImGuiMultiSelectFlags Flags = ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_ClearOnClickVoid | ImGuiMultiSelectFlags_BoxSelect1d;
	ImGuiMultiSelectIO* MultiSelectIO = ImGui::BeginMultiSelect(Flags, Selection.Size, VisibleItems.Num());
	Selection.ApplyRequests(MultiSelectIO);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(EditorUIScale::Px(4.0f), EditorUIScale::Px(1.0f))); // Tighten spacing
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const float AvailableWidth = ImGui::GetContentRegionAvail().x;

	ImGuiListClipper Clipper;
	Clipper.Begin(VisibleItems.Num());
	if (MultiSelectIO->RangeSrcItem != -1)
		Clipper.IncludeItemByIndex(static_cast<int>(MultiSelectIO->RangeSrcItem)); // Shift 범위 선택의 시작 줄이 잘려 나가지 않게
	while (Clipper.Step())
	{
		for (int Row = Clipper.DisplayStart; Row < Clipper.DisplayEnd; ++Row)
		{
			const int32 ItemIndex = VisibleItems[Row];
			const FLogData& Item = Items[ItemIndex];

			// 명령 출력처럼 끝에 줄바꿈을 붙인 메시지가 빈 줄을 만들지 않게 잘라서 그린다.
			const char* TextBegin = Item.message.c_str();
			const char* TextEnd = TextBegin + Item.message.size();
			while (TextEnd > TextBegin && (TextEnd[-1] == '\n' || TextEnd[-1] == '\r'))
				--TextEnd;
			const ImVec2 TextSize = ImGui::CalcTextSize(TextBegin, TextEnd);

			ImGui::PushID(ItemIndex);
			ImGui::SetNextItemSelectionUserData(Row);
			// Selectable은 판정 영역을 ItemSpacing 절반만큼 왼쪽으로 넓히므로 호출 전 커서 위치에 글자를 그린다.
			const ImVec2 TextPos = ImGui::GetCursorScreenPos();
			// 가로 스크롤이 생기도록 긴 줄은 글자 폭만큼 넓힌다.
			ImGui::Selectable("##LogLine", Selection.Contains(static_cast<ImGuiID>(ItemIndex)), ImGuiSelectableFlags_None,
				ImVec2(std::max(AvailableWidth, TextSize.x), std::max(TextSize.y, ImGui::GetTextLineHeight())));
			DrawList->AddText(TextPos, ImGui::ColorConvertFloat4ToU32(Item.Color), TextBegin, TextEnd);
			ImGui::PopID();
		}
	}
	ImGui::PopStyleVar();

	MultiSelectIO = ImGui::EndMultiSelect();
	Selection.ApplyRequests(MultiSelectIO);

	if (Selection.Size > 0 && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C))
		ImGui::SetClipboardText(BuildClipboardText(true).c_str());
}

FString FOutputLogPanel::BuildClipboardText(bool bSelectedOnly) const
{
	FString Text;
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		const FLogData& Item = Items[Index];
		if (!Filter.PassFilter(Item.message.c_str()))
			continue;
		if (bSelectedOnly && !Selection.Contains(static_cast<ImGuiID>(Index)))
			continue;

		size_t Length = Item.message.size();
		while (Length > 0 && (Item.message[Length - 1] == '\n' || Item.message[Length - 1] == '\r'))
			--Length;
		Text.append(Item.message, 0, Length);
		Text.push_back('\n');
	}
	return Text;
}

void FOutputLogPanel::ClearLog()
{
	Items.Reset();
	Selection.Clear();
}

void FOutputLogPanel::ExecCommand(const FString& CommandLine)
{
	FLog::Log(ELogVerbosity::Input, "# {}\n", CommandLine);

	// Insert into history. First find match and delete it so it can be pushed to the back.
	// This isn't trying to be smart or optimal.
	HistoryPos = -1;
	for (int i = History.Num() - 1; i >= 0; i--)
		if (History[i] == CommandLine)
		{
			//History.erase(History.begin() + i);
			History.RemoveAt(i, 1);
			break;
		}
	History.Add(CommandLine);

	// On command input, we scroll to bottom even if AutoScroll==false
	ScrollToBottom = true;

	// Process command
	// 인자를 받는 명령은 문자열 전체 비교로는 걸러지지 않으므로 먼저 위임한다.
	FString StatMessage;
	if (FStatOverlay::ExecCommand(CommandLine, StatMessage))
	{
		FLog::Log(ELogVerbosity::Info, "{}", StatMessage);
		return;
	}

	TArray<FString> Tokens;
	Tokenize(CommandLine, Tokens);
	if (Tokens.IsEmpty())
		return;
	const FString& Command = Tokens[0];

	if (EqualsIgnoreCase(Command, "CLEAR"))
	{
		ClearLog();
		return;
	}

	if (EqualsIgnoreCase(Command, "HELP"))
	{
		FLog::Log(ELogVerbosity::Info, "Commands:");
		FLog::Log(ELogVerbosity::Info, "- HELP | HISTORY | CLEAR");
		FLog::Log(ELogVerbosity::Info, "- STAT <fps|memory|profile|light|decal|skinning|all|none> [on|off]");
		FLog::Log(ELogVerbosity::Info, "- r.SkinningMode [cpu|gpu|cs|toggle]   (GPU selects VS skinning and needs Forward)");
		FLog::Log(ELogVerbosity::Info, "- r.Skinning.ForceUpdate [0|1]   (re-skin every frame for CPU/GPU comparison)");
		FLog::Log(ELogVerbosity::Info, "- CSM Count <1-4>   (= r.Shadow.CSM.MaxCascades)");
		FLog::Log(ELogVerbosity::Info, "- r.shadow.CacheWholeSceneShadows [0|1]   (not implemented yet)");
		FLog::Log(ELogVerbosity::Info, "Console variables (type the name alone to see the value):");
		for (const FConsoleVariable& Variable : GetConsoleVariables())
			FLog::Log(ELogVerbosity::Info, "- {}  {}", FormatConsoleVariable(Variable), Variable.Help);
		return;
	}

	if (EqualsIgnoreCase(Command, "HISTORY"))
	{
		const int32 First = std::max(History.Num() - 10, 0);
		for (int32 i = First; i < History.Num(); i++)
			FLog::Log(ELogVerbosity::Info, "{:03d}: {}", i, History[i]);
		return;
	}

	// 예전 명령. 캐스케이드 수는 라이트마다 정하고, 이 값은 그 상한이다.
	if (EqualsIgnoreCase(Command, "CSM"))
	{
		if (Tokens.Num() == 3 && EqualsIgnoreCase(Tokens[1], "COUNT"))
		{
			if (const FConsoleVariable* MaxCascades = FindConsoleVariable("r.Shadow.CSM.MaxCascades"))
				ExecConsoleVariable(*MaxCascades, { MaxCascades->Name, Tokens[2] });
			return;
		}
		if (Tokens.Num() >= 2 && EqualsIgnoreCase(Tokens[1], "LOGRATIO"))
		{
			FLog::Log(ELogVerbosity::Warning, "CSM LogRatio was removed. Set CascadeDistributionExponent on the Directional Light instead.");
			return;
		}
		FLog::Log(ELogVerbosity::Info, "Usage: CSM Count <1-4>");
		return;
	}

	if (EqualsIgnoreCase(Command, "r.shadow.CacheWholeSceneShadows"))
	{
		if (!EditorSettings)
		{
			FLog::Log(ELogVerbosity::Info, "Settings not available");
			return;
		}
		const FString ValueStr = Tokens.Num() > 1 ? Tokens[1] : FString();
		if (ValueStr.empty())                        EditorSettings->bCacheShadows = !EditorSettings->bCacheShadows;
		else if (ValueStr == "0" || ValueStr == "1") EditorSettings->bCacheShadows = (ValueStr == "1");
		else FLog::Log(ELogVerbosity::Info, "Usage: r.shadow.CacheWholeSceneShadows [0|1]");
		FLog::Log(ELogVerbosity::Info, "r.shadow.CacheWholeSceneShadows = {}", EditorSettings->bCacheShadows ? 1 : 0);
		// 그림자 맵은 지금 매 프레임 다시 그린다. 설정 값만 저장되고 렌더링에는 아직 영향이 없다.
		FLog::Log(ELogVerbosity::Warning, "Shadow caching is not implemented yet; this setting has no effect.");
		return;
	}

	// CPU/GPU 스키닝 전환. Settings 패널의 Skinning Mode와 같은 값을 바꾸므로 editor.ini에도 저장된다.
	// 이름만 입력하면 현재 값을 보여 준다 (콘솔 변수와 같은 규칙).
	if (EqualsIgnoreCase(Command, "r.SkinningMode") || EqualsIgnoreCase(Command, "r.GPUSkinning"))
	{
		if (!EditorSettings)
		{
			FLog::Log(ELogVerbosity::Info, "Settings not available");
			return;
		}
		constexpr const char* Usage = "Usage: r.SkinningMode [cpu|gpu|cs|toggle]  or  r.GPUSkinning [0|1]";
		if (Tokens.Num() > 2)
		{
			FLog::Log(ELogVerbosity::Info, "{}", Usage);
			return;
		}
		if (Tokens.Num() == 2)
		{
			const FString& Value = Tokens[1];
			if (EqualsIgnoreCase(Value, "gpu") || Value == "1")
				EditorSettings->SkinningMode = ESkinningMode::GPU;
			else if (EqualsIgnoreCase(Value, "cpu") || Value == "0")
				EditorSettings->SkinningMode = ESkinningMode::CPU;
			else if (EqualsIgnoreCase(Command, "r.SkinningMode") && EqualsIgnoreCase(Value, "cs"))
				EditorSettings->SkinningMode = ESkinningMode::Compute;
			else if (EqualsIgnoreCase(Value, "toggle"))
				EditorSettings->SkinningMode = EditorSettings->SkinningMode == ESkinningMode::GPU ? ESkinningMode::CPU : ESkinningMode::GPU;
			else
			{
				FLog::Log(ELogVerbosity::Info, "{}", Usage);
				return;
			}
		}

		const bool bGPU = EditorSettings->SkinningMode == ESkinningMode::GPU;
		FLog::Log(ELogVerbosity::Info, "r.SkinningMode = {}", GetSkinningModeName(EditorSettings->SkinningMode));
		if (bGPU && EditorSettings->RenderingPath != ERenderingPath::Forward)
			FLog::Log(ELogVerbosity::Warning, "GPU skinning needs the Forward rendering path. CPU skinning stays active until Rendering Path is Forward.");
		return;
	}

	// 아직 런타임 애니메이션이 없어 포즈가 바뀌지 않으면 스키닝은 처음 한 번만 돈다.
	// 비교용으로 매 프레임 스키닝을 다시 시켜 애니메이션 재생과 같은 부하를 만든다.
	if (EqualsIgnoreCase(Command, "r.Skinning.ForceUpdate"))
	{
		if (Tokens.Num() == 2 && (Tokens[1] == "0" || Tokens[1] == "1"))
			USkeletalMeshComponent::SetForceSkinningUpdate(Tokens[1] == "1");
		else if (Tokens.Num() != 1)
		{
			FLog::Log(ELogVerbosity::Info, "Usage: r.Skinning.ForceUpdate [0|1]");
			return;
		}
		FLog::Log(ELogVerbosity::Info, "r.Skinning.ForceUpdate = {}", USkeletalMeshComponent::IsForceSkinningUpdate() ? 1 : 0);
		return;
	}

	if (const FConsoleVariable* Variable = FindConsoleVariable(Command))
	{
		ExecConsoleVariable(*Variable, Tokens);
		return;
	}

	FLog::Log(ELogVerbosity::Warning, "Unknown command: '{}'. Type HELP for commands.", CommandLine);
}

int FOutputLogPanel::TextEditCallback(ImGuiInputTextCallbackData* data)
{
	//AddLog("cursor: %d, selection: %d-%d", data->CursorPos, data->SelectionStart, data->SelectionEnd);
	switch (data->EventFlag)
	{
	case ImGuiInputTextFlags_CallbackCompletion:
	{
		// Example of TEXT COMPLETION

		// Locate beginning of current word
		const char* word_end = data->Buf + data->CursorPos;
		const char* word_start = word_end;
		while (word_start > data->Buf)
		{
			const char c = word_start[-1];
			if (c == ' ' || c == '\t' || c == ',' || c == ';')
				break;
			word_start--;
		}

		// Build a list of candidates
		ImVector<FString> candidates;
		for (int i = 0; i < Commands.Num(); i++)
			if (Strnicmp(Commands[i].c_str(), word_start, (int)(word_end - word_start)) == 0)
				candidates.push_back(Commands[i]);

		if (candidates.Size == 0)
		{
			// No match
			FLog::Log(ELogVerbosity::Input, "No match for \"{}\"!\n", word_start);
		}
		else if (candidates.Size == 1)
		{
			// Single match. Delete the beginning of the word and replace it entirely so we've got nice casing.
			data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
			data->InsertChars(data->CursorPos, candidates[0].c_str());
			data->InsertChars(data->CursorPos, " ");
		}
		else
		{
			// Multiple matches. Complete as much as we can..
			// So inputting "C"+Tab will complete to "CL" then display "CLEAR" and "CLASSIFY" as matches.
			int match_len = (int)(word_end - word_start);
			for (;;)
			{
				int c = 0;
				bool all_candidates_matches = true;
				for (int i = 0; i < candidates.Size && all_candidates_matches; i++)
					if (i == 0)
						c = toupper(candidates[i][match_len]);
					else if (c == 0 || c != toupper(candidates[i][match_len]))
						all_candidates_matches = false;
				if (!all_candidates_matches)
					break;
				match_len++;
			}

			if (match_len > 0)
			{
				data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
				data->InsertChars(data->CursorPos, candidates[0].c_str(), candidates[0].c_str() + match_len);
			}

			// List matches
			FLog::Log(ELogVerbosity::Info, "Possible matches:\n");
			for (int i = 0; i < candidates.Size; i++)
				FLog::Log(ELogVerbosity::Info, "- {}\n", candidates[i]);
		}

		break;
	}
	case ImGuiInputTextFlags_CallbackHistory:
	{
		// Example of HISTORY
		const int prev_history_pos = HistoryPos;
		if (data->EventKey == ImGuiKey_UpArrow)
		{
			if (HistoryPos == -1)
				HistoryPos = History.Num() - 1;
			else if (HistoryPos > 0)
				HistoryPos--;
		}
		else if (data->EventKey == ImGuiKey_DownArrow)
		{
			if (HistoryPos != -1)
				if (++HistoryPos >= History.Num())
					HistoryPos = -1;
		}

		// A better implementation would preserve the data on the current input line along with cursor position.
		if (prev_history_pos != HistoryPos)
		{
			const char* history_str = (HistoryPos >= 0) ? History[HistoryPos].c_str() : "";
			data->DeleteChars(0, data->BufTextLen);
			data->InsertChars(0, history_str);
		}
	}
	}
	return 0;
}

void FOutputLogPanel::OnLog(ELogVerbosity Verbosity, const FString& Message)
{
	FLogData LogData;

	switch (Verbosity)
	{
	case ELogVerbosity::Input:
		LogData.Color = ImVec4(1.0f, 0.8f, 0.6f, 1.0f);
		break;
	case ELogVerbosity::Info:
		LogData.Color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		break;
	case ELogVerbosity::Warning:
		LogData.Color = ImVec4(1.0f, 0.75f, 0.2f, 1.0f);
		break;
	case ELogVerbosity::Error:
		LogData.Color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
		break;
	}
	LogData.message = Message;
	Items.Add(LogData);
	ScrollToBottom = true;
}



//struct ExampleAppConsole
//{
//    char                  InputBuf[256];
//    ImVector<char*>       Items;
//    ImVector<const char*> Commands;
//    ImVector<char*>       History;
//    int                   HistoryPos;    // -1: new line, 0..History.Size-1 browsing history.
//    ImGuiTextFilter       Filter;
//    bool                  AutoScroll;
//    bool                  ScrollToBottom;
//
//    ExampleAppConsole()
//    {
//        ClearLog();
//        memset(InputBuf, 0, sizeof(InputBuf));
//        HistoryPos = -1;
//
//        // "CLASSIFY" is here to provide the test case where "C"+[tab] completes to "CL" and display multiple matches.
//        Commands.push_back("HELP");
//        Commands.push_back("HISTORY");
//        Commands.push_back("CLEAR");
//        Commands.push_back("CLASSIFY");
//        AutoScroll = true;
//        ScrollToBottom = false;
//        AddLog("Welcome to Dear ImGui!");
//    }
//    ~ExampleAppConsole()
//    {
//        ClearLog();
//        for (int i = 0; i < History.Size; i++)
//            ImGui::MemFree(History[i]);
//    }
//
//    // Portable helpers
//    static int   Stricmp(const char* s1, const char* s2) { int d; while ((d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; } return d; }
//    static int   Strnicmp(const char* s1, const char* s2, int n) { int d = 0; while (n > 0 && (d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; n--; } return d; }
//    static char* Strdup(const char* s) { IM_ASSERT(s); size_t len = strlen(s) + 1; void* buf = ImGui::MemAlloc(len); IM_ASSERT(buf); return (char*)memcpy(buf, (const void*)s, len); }
//    static void  Strtrimblanks(char* s) { char* str_end = s + strlen(s); while (str_end > s && str_end[-1] == ' ') str_end--; *str_end = 0; }
//
//    void    ClearLog()
//    {
//        for (int i = 0; i < Items.Size; i++)
//            ImGui::MemFree(Items[i]);
//        Items.clear();
//    }
//
//    void    AddLog(const char* fmt, ...) IM_FMTARGS(2)
//    {
//        // FIXME-OPT
//        char buf[1024];
//        va_list args;
//        va_start(args, fmt);
//        vsnprintf(buf, IM_COUNTOF(buf), fmt, args);
//        buf[IM_COUNTOF(buf) - 1] = 0;
//        va_end(args);
//        Items.push_back(Strdup(buf));
//    }
//
//    void    Draw(const char* title, bool* p_open)
//    {
//        ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(520.0f), EditorUIScale::Px(600.0f)), ImGuiCond_FirstUseEver);
//        if (!ImGui::Begin(title, p_open))
//        {
//            ImGui::End();
//            return;
//        }
//        IMGUI_DEMO_MARKER("Examples/Console");
//
//        // As a specific feature guaranteed by the library, after calling Begin() the last Item represent the title bar.
//        // So e.g. IsItemHovered() will return true when hovering the title bar.
//        // Here we create a context menu only available from the title bar.
//        if (ImGui::BeginPopupContextItem())
//        {
//            if (ImGui::MenuItem("Close Console"))
//                *p_open = false;
//            ImGui::EndPopup();
//        }
//
//        ImGui::TextWrapped(
//            "This example implements a console with basic coloring, completion (TAB key) and history (Up/Down keys). A more elaborate "
//            "implementation may want to store entries along with extra data such as timestamp, emitter, etc.");
//        ImGui::TextWrapped("Enter 'HELP' for help.");
//
//        // TODO: display items starting from the bottom
//
//        if (ImGui::SmallButton("Add Debug Text")) { AddLog("%d some text", Items.Size); AddLog("some more text"); AddLog("display very important message here!"); }
//        ImGui::SameLine();
//        if (ImGui::SmallButton("Add Debug Error")) { AddLog("[error] something went wrong"); }
//        ImGui::SameLine();
//        if (ImGui::SmallButton("Clear")) { ClearLog(); }
//        ImGui::SameLine();
//        bool copy_to_clipboard = ImGui::SmallButton("Copy");
//        //static float t = 0.0f; if (ImGui::GetTime() - t > 0.02f) { t = ImGui::GetTime(); AddLog("Spam %f", t); }
//
//        ImGui::Separator();
//
//        // Options menu
//        if (ImGui::BeginPopup("Options"))
//        {
//            ImGui::Checkbox("Auto-scroll", &AutoScroll);
//            ImGui::EndPopup();
//        }
//
//        // Options, Filter
//        ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_Tooltip);
//        if (ImGui::Button("Options"))
//            ImGui::OpenPopup("Options");
//        ImGui::SameLine();
//        Filter.Draw("Filter (\"incl,-excl\") (\"error\")", 180);
//        ImGui::Separator();
//
//        // Reserve enough left-over height for 1 separator + 1 input text
//        ImGuiStyle& style = ImGui::GetStyle();
//        const float footer_height_to_reserve = style.SeparatorSize + style.ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
//        if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_HorizontalScrollbar))
//        {
//            if (ImGui::BeginPopupContextWindow())
//            {
//                if (ImGui::Selectable("Clear")) ClearLog();
//                ImGui::EndPopup();
//            }
//
//            // Display every line as a separate entry so we can change their color or add custom widgets.
//            // If you only want raw text you can use ImGui::TextUnformatted(log.begin(), log.end());
//            // NB- if you have thousands of entries this approach may be too inefficient and may require user-side clipping
//            // to only process visible items. The clipper will automatically measure the height of your first item and then
//            // "seek" to display only items in the visible area.
//            // To use the clipper we can replace your standard loop:
//            //      for (int i = 0; i < Items.Size; i++)
//            //   With:
//            //      ImGuiListClipper clipper;
//            //      clipper.Begin(Items.Size);
//            //      while (clipper.Step())
//            //         for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
//            // - That your items are evenly spaced (same height)
//            // - That you have cheap random access to your elements (you can access them given their index,
//            //   without processing all the ones before)
//            // You cannot this code as-is if a filter is active because it breaks the 'cheap random-access' property.
//            // We would need random-access on the post-filtered list.
//            // A typical application wanting coarse clipping and filtering may want to pre-compute an array of indices
//            // or offsets of items that passed the filtering test, recomputing this array when user changes the filter,
//            // and appending newly elements as they are inserted. This is left as a task to the user until we can manage
//            // to improve this example code!
//            // If your items are of variable height:
//            // - Split them into same height items would be simpler and facilitate random-seeking into your list.
//            // - Consider using manual call to IsRectVisible() and skipping extraneous decoration from your items.
//            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing
//            if (copy_to_clipboard)
//                ImGui::LogToClipboard();
//            for (const char* item : Items)
//            {
//                if (!Filter.PassFilter(item))
//                    continue;
//
//                // Normally you would store more information in your item than just a string.
//                // (e.g. make Items[] an array of structure, store color/type etc.)
//                ImVec4 color;
//                bool has_color = false;
//                if (strstr(item, "[error]")) { color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); has_color = true; }
//                else if (strncmp(item, "# ", 2) == 0) { color = ImVec4(1.0f, 0.8f, 0.6f, 1.0f); has_color = true; }
//                if (has_color)
//                    ImGui::PushStyleColor(ImGuiCol_Text, color);
//                ImGui::TextUnformatted(item);
//                if (has_color)
//                    ImGui::PopStyleColor();
//            }
//            if (copy_to_clipboard)
//                ImGui::LogFinish();
//
//            // Keep up at the bottom of the scroll region if we were already at the bottom at the beginning of the frame.
//            // Using a scrollbar or mouse-wheel will take away from the bottom edge.
//            if (ScrollToBottom || (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
//                ImGui::SetScrollHereY(1.0f);
//            ScrollToBottom = false;
//
//            ImGui::PopStyleVar();
//        }
//        ImGui::EndChild();
//        ImGui::Separator();
//
//        // Command-line
//        bool reclaim_focus = false;
//        ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
//        if (ImGui::InputText("Input", InputBuf, IM_COUNTOF(InputBuf), input_text_flags, &TextEditCallbackStub, (void*)this))
//        {
//            char* s = InputBuf;
//            Strtrimblanks(s);
//            if (s[0])
//                ExecCommand(s);
//            s[0] = 0;
//            reclaim_focus = true;
//        }
//
//        // Auto-focus on window apparition
//        ImGui::SetItemDefaultFocus();
//        if (reclaim_focus)
//            ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget
//
//        ImGui::End();
//    }
//
//    void    ExecCommand(const char* command_line)
//    {
//        AddLog("# %s\n", command_line);
//
//        // Insert into history. First find match and delete it so it can be pushed to the back.
//        // This isn't trying to be smart or optimal.
//        HistoryPos = -1;
//        for (int i = History.Size - 1; i >= 0; i--)
//            if (Stricmp(History[i], command_line) == 0)
//            {
//                ImGui::MemFree(History[i]);
//                History.erase(History.begin() + i);
//                break;
//            }
//        History.push_back(Strdup(command_line));
//
//        // Process command
//        if (Stricmp(command_line, "CLEAR") == 0)
//        {
//            ClearLog();
//        }
//        else if (Stricmp(command_line, "HELP") == 0)
//        {
//            AddLog("Commands:");
//            for (int i = 0; i < Commands.Size; i++)
//                AddLog("- %s", Commands[i]);
//        }
//        else if (Stricmp(command_line, "HISTORY") == 0)
//        {
//            int first = History.Size - 10;
//            for (int i = first > 0 ? first : 0; i < History.Size; i++)
//                AddLog("%3d: %s\n", i, History[i]);
//        }
//        else
//        {
//            AddLog("Unknown command: '%s'\n", command_line);
//        }
//
//        // On command input, we scroll to bottom even if AutoScroll==false
//        ScrollToBottom = true;
//    }
//
//    // In C++11 you'd be better off using lambdas for this sort of forwarding callbacks
//    static int TextEditCallbackStub(ImGuiInputTextCallbackData* data)
	//{
	//    ExampleAppConsole* console = (ExampleAppConsole*)data->UserData;
	//    return console->TextEditCallback(data);
	//}

	//int     TextEditCallback(ImGuiInputTextCallbackData* data)
	//{
	//    //AddLog("cursor: %d, selection: %d-%d", data->CursorPos, data->SelectionStart, data->SelectionEnd);
	//    switch (data->EventFlag)
	//    {
	//    case ImGuiInputTextFlags_CallbackCompletion:
	//    {
	//        // Example of TEXT COMPLETION

	//        // Locate beginning of current word
	//        const char* word_end = data->Buf + data->CursorPos;
	//        const char* word_start = word_end;
	//        while (word_start > data->Buf)
	//        {
	//            const char c = word_start[-1];
	//            if (c == ' ' || c == '\t' || c == ',' || c == ';')
	//                break;
	//            word_start--;
	//        }

	//        // Build a list of candidates
	//        ImVector<const char*> candidates;
	//        for (int i = 0; i < Commands.Size; i++)
	//            if (Strnicmp(Commands[i], word_start, (int)(word_end - word_start)) == 0)
	//                candidates.push_back(Commands[i]);

	//        if (candidates.Size == 0)
	//        {
	//            // No match
	//            AddLog("No match for \"%.*s\"!\n", (int)(word_end - word_start), word_start);
	//        }
	//        else if (candidates.Size == 1)
	//        {
	//            // Single match. Delete the beginning of the word and replace it entirely so we've got nice casing.
	//            data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
	//            data->InsertChars(data->CursorPos, candidates[0]);
	//            data->InsertChars(data->CursorPos, " ");
	//        }
	//        else
	//        {
	//            // Multiple matches. Complete as much as we can..
	//            // So inputting "C"+Tab will complete to "CL" then display "CLEAR" and "CLASSIFY" as matches.
	//            int match_len = (int)(word_end - word_start);
	//            for (;;)
	//            {
	//                int c = 0;
	//                bool all_candidates_matches = true;
	//                for (int i = 0; i < candidates.Size && all_candidates_matches; i++)
	//                    if (i == 0)
	//                        c = toupper(candidates[i][match_len]);
	//                    else if (c == 0 || c != toupper(candidates[i][match_len]))
	//                        all_candidates_matches = false;
	//                if (!all_candidates_matches)
	//                    break;
	//                match_len++;
	//            }

	//            if (match_len > 0)
	//            {
	//                data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
	//                data->InsertChars(data->CursorPos, candidates[0], candidates[0] + match_len);
	//            }

	//            // List matches
	//            AddLog("Possible matches:\n");
	//            for (int i = 0; i < candidates.Size; i++)
	//                AddLog("- %s\n", candidates[i]);
	//        }

	//        break;
	//    }
	//    case ImGuiInputTextFlags_CallbackHistory:
	//    {
	//        // Example of HISTORY
	//        const int prev_history_pos = HistoryPos;
	//        if (data->EventKey == ImGuiKey_UpArrow)
	//        {
	//            if (HistoryPos == -1)
	//                HistoryPos = History.Size - 1;
	//            else if (HistoryPos > 0)
	//                HistoryPos--;
	//        }
	//        else if (data->EventKey == ImGuiKey_DownArrow)
	//        {
	//            if (HistoryPos != -1)
	//                if (++HistoryPos >= History.Size)
	//                    HistoryPos = -1;
	//        }

	//        // A better implementation would preserve the data on the current input line along with cursor position.
	//        if (prev_history_pos != HistoryPos)
	//        {
	//            const char* history_str = (HistoryPos >= 0) ? History[HistoryPos] : "";
	//            data->DeleteChars(0, data->BufTextLen);
	//            data->InsertChars(0, history_str);
	//        }
	//    }
	//    }
	//    return 0;
	//}
//};

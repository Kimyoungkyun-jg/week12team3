#include "EnginePCH.h"
#include "Editor/OutputLog/OutputLogPanel.h"

#include "Core/StatOverlay.h"
#include "Render/SceneRenderer.h"

#include "Editor/Settings/SettingsPanel.h"

#include "Components/SkinnedMeshComponent.h"

#include <charconv>
#include <sstream>

namespace
{
	TArray<FString> TokenizeCommand(const FString& Line)
	{
		TArray<FString> Tokens;
		std::istringstream Stream(Line);
		for (FString Token; Stream >> Token;)
			Tokens.Add(Token);
		return Tokens;
	}

	template <typename T>
	bool ParseNumber(const FString& Text, T& Out)
	{
		const auto [End, Error] = std::from_chars(Text.data(), Text.data() + Text.size(), Out);

		return Error == std::errc{} && End == Text.data() + Text.size();
	}

	bool ParseToggle(const TArray<FString>& Args, bool bCurrent, bool& bOut)
	{
		if (Args.Num() == 1) { bOut = !bCurrent; return true; }
		if (Args.Num() == 2 && (Args[1] == "0" || Args[1] == "1")) { bOut = (Args[1] == "1"); return true; }
		return false;
	}
}

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

bool FOutputLogPanel::Init()
{
	ClearLog();
	RegisterCommands();   
	

	HistoryPos = -1;
	InputBuf[0] = '\0';

	AutoScroll = true;
	ScrollToBottom = false;

	return true;
}

void FOutputLogPanel::Tick(float DeltaTime)
{
}

void FOutputLogPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);

	ImGui::Begin("Output Log");

	//////////////////////////////////////////////////////////

	ImGui::TextDisabled("Filter");
	ImGui::SameLine();

	Filter.Draw("##Filter", 180.0f);

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

	ImGui::SameLine();
	ImGui::Checkbox("Auto-scroll", &AutoScroll);

	ImGui::SameLine();
	if (ImGui::SmallButton("Clear"))
		ClearLog();

	ImGui::SameLine();
	bool CopyToClipboard = ImGui::SmallButton("Copy");

	ImGui::Separator();

	//////////////////////////////////////////////////////////

	ImGuiStyle& style = ImGui::GetStyle();
	const float footer_height_to_reserve = style.SeparatorSize + style.ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_HorizontalScrollbar)) // ImGui Window 안에 Child Window를 생성
	{
		if (ImGui::BeginPopupContextWindow())
		{
			if (ImGui::Selectable("Clear"))
				ClearLog();
			ImGui::EndPopup();
		}

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing
		if (CopyToClipboard)
			ImGui::LogToClipboard();
		for (const FLogData& item : Items)
		{
			if (!Filter.PassFilter(item.message.c_str()))
				continue;

			ImVec4 color = item.Color;
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::TextUnformatted(item.message.c_str());
			ImGui::PopStyleColor();
		}
		if (CopyToClipboard)
			ImGui::LogFinish();
		if (ScrollToBottom || (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
			ImGui::SetScrollHereY(1.0f);
		ScrollToBottom = false;

		ImGui::PopStyleVar();
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

void FOutputLogPanel::ClearLog()
{
	Items.Reset();
}

void FOutputLogPanel::ExecCommand(const FString& CommandLine)
{
	FLog::Log(ELogVerbosity::Input, "# {}\n", CommandLine);
	AddToHistory(CommandLine);
	ScrollToBottom = true;

	const TArray<FString> Args = TokenizeCommand(CommandLine);
	if (Args.Num() == 0) return;

	for (const FConsoleCommand& Cmd : ConsoleCommands)
	{
		if (!EqualsIgnoreCase(Args[0], Cmd.Name.c_str())) continue;
		if(!Cmd.Handler(Args, CommandLine))
			FLog::Log(ELogVerbosity::Info, "Usage: {}", Cmd.Usage);
		return;
	}
	FLog::Log(ELogVerbosity::Info, "Unknown command: '{}'", CommandLine);
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

void FOutputLogPanel::RegisterCommands()
{
	auto Add = [this](const char* Name, const char* Usage, FConsoleHandler Handler)
		{
			ConsoleCommands.Add({ Name, Usage, std::move(Handler) });
			Commands.Add(Name);   // 자동완성용 목록도 여기서 채운다
		};

	Add("HELP", "HELP", [this](const TArray<FString>&, const FString&)
		{
			FLog::Log(ELogVerbosity::Info, "Commands:");
			for (const FConsoleCommand& Cmd : ConsoleCommands)
				FLog::Log(ELogVerbosity::Info, "- {}", Cmd.Usage);
			return true;
		});

	Add("CLEAR", "CLEAR", [this](const TArray<FString>&, const FString&)
		{
			ClearLog();
			return true;
		});

	Add("HISTORY", "HISTORY", [this](const TArray<FString>&, const FString&)
		{
			const int32 First = std::max(History.Num() - 10, 0);
			for (int32 i = First; i < History.Num(); ++i)
				FLog::Log(ELogVerbosity::Info, "{:03d}: {}", i, History[i]);
			return true;
		});

	Add("STAT", "STAT <fps|memory|profile|light|decal|all|none>", [](const TArray<FString>&, const FString& Line)
		{
			FString Message;
			FStatOverlay::ExecCommand(Line, Message);   // 해석은 엔진 쪽에 맡긴다
			FLog::Log(ELogVerbosity::Info, "{}", Message);
			return true;
		});

	Add("CSM", "CSM Count <1-4> | CSM LogRatio <0.0-1.0>", [](const TArray<FString>& Args, const FString&)
		{
			if (Args.Num() != 3) return false;
			if (EqualsIgnoreCase(Args[1], "COUNT"))
			{
				uint32 Count = 0;
				if (!ParseNumber(Args[2], Count) || !FSceneRenderer::SetCascadeCount(Count)) return false;
				FLog::Log(ELogVerbosity::Info, "CSM Count: {}", Count);
				return true;
			}
			if (EqualsIgnoreCase(Args[1], "LOGRATIO"))
			{
				float Ratio = 0.0f;
				if (!ParseNumber(Args[2], Ratio) || !FSceneRenderer::SetCascadeLogRatio(Ratio)) return false;
				FLog::Log(ELogVerbosity::Info, "CSM LogRatio: {}", Ratio);
				return true;
			}
			return false;
		});

	Add("r.shadow.CacheWholeSceneShadows", "r.shadow.CacheWholeSceneShadows [0|1]",
		[this](const TArray<FString>& Args, const FString&)
		{
			if (!EditorSettings)
			{
				FLog::Log(ELogVerbosity::Info, "Settings not available");
				return true;
			}
			bool bOn = false;
			if (!ParseToggle(Args, EditorSettings->bCacheShadows, bOn)) return false;
			EditorSettings->bCacheShadows = bOn;
			FLog::Log(ELogVerbosity::Info, "r.shadow.CacheWholeSceneShadows = {}", bOn ? 1 : 0);
			return true;
		});

	Add("r.GPUSkinning", "r.GPUSkinning [0|1]", [](const TArray<FString>& Args, const FString&)
		{
			bool bOn = false;
			if (!ParseToggle(Args, USkinnedMeshComponent::bUseGPUSkinning, bOn)) return false;
			USkinnedMeshComponent::SetGPUSkinningEnabled(bOn);
			FLog::Log(ELogVerbosity::Info, "r.GPUSkinning = {}", bOn ? 1 : 0);
			return true;
		});

		Add("r.Skinning.ForceUpdate", "r.Skinning.ForceUpdate [0|1]", [](const TArray<FString>& Args, const FString&)
			{
				bool bOn = false;
				if (!ParseToggle(Args, USkinnedMeshComponent::bForceSkinUpdate, bOn)) return false;
				USkinnedMeshComponent::bForceSkinUpdate = bOn;
				FLog::Log(ELogVerbosity::Info, "r.Skinning.ForceUpdate = {}", bOn ? 1 : 0);
				return true;
			});
}

void FOutputLogPanel::AddToHistory(const FString& CommandLine)
{
	HistoryPos = -1;
	for (int32 i = History.Num() - 1; i >= 0; --i)
	{
		if (History[i] == CommandLine)
		{
			History.RemoveAt(i, 1);
			break;
		}
	}
	History.Add(CommandLine);
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
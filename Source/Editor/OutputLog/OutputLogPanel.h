#pragma once

#include <format>
#include "Editor/EditorUI/EditorPanel.h"
#include "Core/EngineLog.h"

struct FEditorSettings;

struct FLogData
{
	FString message;
	ImVec4 Color;
};

class FOutputLogPanel : public IEditorPanel, public ILogSink
{
public:
	bool Init() override;
	void Tick(float DeltaTime)override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Output Log"; }

	void ClearLog();

	//template<typename... Args>
	//void AddLog(ELogVerbosity Verbosity, std::format_string<Args...> fmt, Args&&... args)
	//{

	//}

	void ExecCommand(const FString& command_line);

	int TextEditCallback(ImGuiInputTextCallbackData* data);
	void SetEditorSettings(FEditorSettings* InSettings) { EditorSettings = InSettings; }
private:
	void RenderLogLines();
	// 필터를 통과한 줄만 순서대로 이어 붙인다. bSelectedOnly면 선택한 줄만.
	FString BuildClipboardText(bool bSelectedOnly) const;

	char                  InputBuf[256];
	TArray<FLogData>       Items;
	TArray<FString>		  Commands;
	TArray<FString>       History;
	int                   HistoryPos;
	ImGuiTextFilter       Filter;
	// 줄 선택. 저장하는 ID는 Items의 인덱스라서 필터를 바꿔도 선택이 유지된다. Clear 때 같이 비운다.
	ImGuiSelectionBasicStorage Selection;
	// 이번 프레임에 필터를 통과한 Items 인덱스. MultiSelect의 표시 순번을 Items 인덱스로 바꿀 때 쓴다.
	TArray<int32>         VisibleItems;
	bool                  AutoScroll;
	bool                  ScrollToBottom;

	// Inherited via ILogSink
	void OnLog(ELogVerbosity, const FString& Message) override;

	FEditorSettings* EditorSettings = nullptr;
};
#include "EnginePCH.h"
#include "Editor/EditorUI/EditorUI.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Asset/FbxImporter/FbxAssetImporter.h"
#include "Asset/FbxImporter/FbxAssetLoader.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Animation/Skeleton.h"
#include "UObject/UObjectIterator.h"
#include "Editor/EditorUI/EditorSort.h"
#include <commdlg.h>
#include <cwchar>
#include <shobjidl.h>
#include <thread>
#include <vector>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "comdlg32.lib")
namespace
{
HRESULT ChooseFbxOutputFolder(HWND Owner, const fs::path& InitialDirectory, FString& Selected)
{
	HRESULT Result = E_FAIL;
	// 오디오 등이 메인 스레드를 MTA로 초기화할 수 있어 Shell UI는 별도의 STA에서 연다.
	std::thread Browser([&]
		{
		const HRESULT Init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		if (FAILED(Init)) { Result = Init; return; }
		{
			try
			{
				ComPtr<IFileOpenDialog> Dialog;
				Result = CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Dialog));
				if (SUCCEEDED(Result))
				{
					FILEOPENDIALOGOPTIONS Flags{};
					Result = Dialog->GetOptions(&Flags);
					if (SUCCEEDED(Result)){ Result = Dialog->SetOptions(Flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
}
					Dialog->SetTitle(L"Select FBX output folder inside Assets");
					fs::path Initial = InitialDirectory;
					while (!Initial.empty() && !fs::is_directory(Initial))
					{
						const fs::path Parent = Initial.parent_path();
						if (Parent == Initial) { Initial.clear(); break; }
						Initial = Parent;
					}
					ComPtr<IShellItem> Start;
					if (!Initial.empty() && SUCCEEDED(SHCreateItemFromParsingName(Initial.c_str(), nullptr, IID_PPV_ARGS(&Start)))){
						Dialog->SetFolder(Start.Get());
}
					if (SUCCEEDED(Result)){ Result = Dialog->Show(Owner);
}
					if (SUCCEEDED(Result))
					{
						ComPtr<IShellItem> Item;
						Result = Dialog->GetResult(&Item);
						PWSTR Path = nullptr;
						if (SUCCEEDED(Result)){ Result = Item->GetDisplayName(SIGDN_FILESYSPATH, &Path);
}
						if (SUCCEEDED(Result)){ Selected = FbxPath::ToString(fs::path(Path));
}
						CoTaskMemFree(Path);
					}
				}
			}
			catch (...) { Result = E_FAIL; }
		}
		CoUninitialize(); });
	// 소유 창으로 오는 메시지도 처리해야 다른 스레드의 모달 대화상자가 정상 동작한다.
	const HANDLE Thread = Browser.native_handle();
	bool Quit = false;
	int QuitCode = 0;
	while (MsgWaitForMultipleObjects(1, &Thread, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
	{
		MSG Message;
		while (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
		{
			if (Message.message == WM_QUIT)
			{
				Quit = true;
				QuitCode = static_cast<int>(Message.wParam);
			}
			else
			{
				TranslateMessage(&Message);
				DispatchMessageW(&Message);
			}
		}
	}
	Browser.join();
	if (Quit)
	{
		PostQuitMessage(QuitCode);
	}
	return Result;
}
}
namespace
{
// 선택한 파일을 UTF-8 경로로 돌려준다. 취소하면 빈 배열이다.
TArray<FString> PickFbxFiles(HWND Owner, bool bMultiSelect, FString& Error)
{
	TArray<FString> Result;
	std::vector<wchar_t> Buffer(bMultiSelect ? 262144 : 32768, L'\0');
	const std::wstring InitialDirectory = fs::absolute("Assets").wstring();
	OPENFILENAMEW Dialog{};
	Dialog.lStructSize = sizeof(Dialog);
	Dialog.hwndOwner = Owner;
	Dialog.lpstrFilter = L"FBX files (*.fbx)\0*.fbx\0\0";
	Dialog.lpstrDefExt = L"fbx";
	Dialog.lpstrFile = Buffer.data();
	Dialog.nMaxFile = static_cast<DWORD>(Buffer.size());
	Dialog.lpstrInitialDir = InitialDirectory.c_str();
	Dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (bMultiSelect ? OFN_ALLOWMULTISELECT : 0);
	if (!GetOpenFileNameW(&Dialog))
	{
		if (const DWORD Code = CommDlgExtendedError())
		{
			Error = "Could not open the file browser.";
			HTR_LOG(Error, "FBX file dialog failed: {}", Code);
		}
		return Result;
	}
	// 한 개면 전체 경로, 여러 개면 폴더 다음에 파일 이름이 널 문자로 이어진다.
	const fs::path First(Buffer.data());
	const wchar_t* Next = Buffer.data() + std::wcslen(Buffer.data()) + 1;
	if (!bMultiSelect || *Next == L'\0')
	{
		Result.Add(FbxPath::ToString(First));
		return Result;
	}
	for (; *Next != L'\0'; Next += std::wcslen(Next) + 1)
	{
		Result.Add(FbxPath::ToString(First / Next));
	}
	return Result;
}
}

void FEditorUI::EnqueueFbxRequest(FFbxImportRequest Request)
{
	for (const FFbxImportRequest& Queued : FbxQueue)
	{
		if (Queued.Source == Request.Source && Queued.Output == Request.Output)
		{
			return;
		}
	}
	FbxQueue.Add(std::move(Request));
}

void FEditorUI::OpenFbxImportDialog()
{
	EnqueueFbxRequest({});
}

void FEditorUI::QueueFbxImports(const TArray<FString>& Sources, const FString& LockedOutput)
{
	for (const FString& Source : Sources)
	{
		FFbxImportRequest Request;
		Request.Source = Source;
		Request.Output = LockedOutput;
		Request.bLockOutput = !LockedOutput.empty();
		EnqueueFbxRequest(std::move(Request));
	}
}

void FEditorUI::QueueFbxReimport(const FString& Source, const FString& Output, const FFbxImportOptions& Options, const FString& Owner)
{
	FFbxImportRequest Request;
	Request.Source = Source;
	Request.Output = Output;
	Request.Owner = Owner;
	Request.Options = Options;
	Request.bLockOutput = true;
	Request.bReimport = true;
	EnqueueFbxRequest(std::move(Request));
}

void FEditorUI::BrowseFbxImportFiles(const FString& Output)
{
	FString Error;
	const TArray<FString> Files = PickFbxFiles(static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw), true, Error);
	if (!Error.empty())
	{
		HTR_LOG(Error, "[FBX] {}", Error);
	}
	QueueFbxImports(Files, Output);
}

void FEditorUI::BrowseFbxReimportFile(const FString& Output, const FFbxImportOptions& Options, const FString& Owner)
{
	FString Error;
	const TArray<FString> Files = PickFbxFiles(static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw), false, Error);
	if (!Error.empty())
	{
		HTR_LOG(Error, "[FBX] {}", Error);
	}
	if (!Files.IsEmpty())
	{
		QueueFbxReimport(Files[0], Output, Options, Owner);
	}
}

void FEditorUI::ApplyFbxRequest(const FFbxImportRequest& Request, bool bKeepOptions)
{
	FbxResult = {};
	if (Request.Source.size() >= sizeof(FbxSourcePath) || Request.Output.size() >= sizeof(FbxOutputDirectory))
	{
		FbxSourcePath[0] = FbxOutputDirectory[0] = '\0';
		FbxResult.Error = "The selected path is too long.";
		return;
	}
	strcpy_s(FbxSourcePath, Request.Source.c_str());
	strcpy_s(FbxOutputDirectory, Request.Output.c_str());
	bFbxOutputDirectoryEdited = !Request.Output.empty();
	if (!bKeepOptions)
	{
		FbxOptions = Request.Options;
	}
	else
	{
		// Import All은 첫 파일의 옵션을 이어 쓰지만 대상 Skeleton은 파일마다 다르다.
		// 이어 쓰면 뒤의 메시 FBX가 애니메이션만 만들고 메시를 만들지 않는다. 메시 없는 FBX는 자동 대상 선택이 처리한다.
		FbxOptions.TargetSkeletonKey = Request.Options.TargetSkeletonKey;
	}
	UpdateFbxOutputDirectory();
}

void FEditorUI::AdvanceFbxQueue(bool bKeepOptions)
{
	FbxQueue.RemoveAt(0);
	++FbxProcessedCount;
	if (FbxQueue.IsEmpty())
	{
		bFbxDialogActive = bFbxImporting = bFbxImportAll = false;
		FbxProcessedCount = 0;
		return;
	}
	ApplyFbxRequest(FbxQueue[0], bKeepOptions);
	if (!bKeepOptions)
	{
		bFbxImporting = false;
	}
}

// 메시가 없는 FBX를 Target Skeleton 없이 받으면 실패한다. 이때 로드된 Skeleton을 차례로 대상으로 시도한다.
// 같은 폴더의 Skeleton을 먼저 본다. 본 이름과 부모가 맞지 않으면 임포터가 거절하므로 엉뚱한 Skeleton에 붙지 않는다.
bool FEditorUI::TryFbxImportWithLoadedSkeleton()
{
	FString SourceKey, Ignored;
	const FString SourceFolder = FbxPath::GetAssetKey(FbxSourcePath, SourceKey, Ignored, false)
		? FbxPath::ToString(FbxPath::FromString(SourceKey).parent_path()) : FString();
	TArray<USkeleton*> Candidates;
	for (TObjectIterator<USkeleton> It; It; ++It)
	{
		USkeleton* Skeleton = *It;
		if (Skeleton && !Skeleton->IsRetired() && !Skeleton->GetPath().empty())
			Candidates.Add(Skeleton);
	}
	std::stable_sort(Candidates.begin(), Candidates.end(), [&](const USkeleton* A, const USkeleton* B)
	{
		const bool ASame = FbxPath::ToString(FbxPath::FromString(A->GetPath()).parent_path()) == SourceFolder;
		const bool BSame = FbxPath::ToString(FbxPath::FromString(B->GetPath()).parent_path()) == SourceFolder;
		return ASame != BSame ? ASame : A->GetPath() < B->GetPath();
	});
	// FBX를 매번 다시 읽으므로 시도 횟수를 제한한다.
	constexpr int32 MaxAttempts = 8;
	for (int32 I = 0; I < Candidates.Num() && I < MaxAttempts; ++I)
	{
		FFbxImportOptions Options = FbxOptions;
		Options.TargetSkeletonKey = Candidates[I]->GetPath();
		// 이름이 몇 개만 같은 다른 캐릭터의 Skeleton에 붙지 않도록 본 대부분이 맞아야 받는다.
		Options.MinTargetBoneMatch = 0.9f;
		FFbxImportResult Result = FFbxAssetLoader::Import(FbxSourcePath, Options, FbxOutputDirectory, FbxQueue[0].Owner);
		if (Result.bSuccess)
		{
			HTR_LOG(Info, "[FBX] No mesh in {}; imported animations for target skeleton {}", FbxSourcePath, Options.TargetSkeletonKey);
			FbxResult = std::move(Result);
			return true;
		}
	}
	return false;
}

void FEditorUI::RunFbxImport()
{
	bFbxImportTextDrawn = false;
	const FString Source = FbxSourcePath;
	FbxResult = FFbxAssetLoader::Import(FbxSourcePath, FbxOptions, FbxOutputDirectory, FbxQueue[0].Owner);
	if (!FbxResult.bSuccess && FbxOptions.TargetSkeletonKey.empty() && FbxResult.Error == "No visible supported mesh geometry in FBX" &&
		!TryFbxImportWithLoadedSkeleton())
	{
		// 아직 대상 Skeleton을 만드는 메시 FBX가 큐 뒤에 있을 수 있다(새로 받은 저장소). 한 번만 맨 뒤로 미룬다.
		if (!FbxQueue[0].bDeferred && FbxQueue.Num() > 1)
		{
			FFbxImportRequest Deferred = FbxQueue[0];
			Deferred.bDeferred = true;
			Deferred.Options = FbxOptions;
			FbxQueue.Add(std::move(Deferred));
			--FbxProcessedCount;
			HTR_LOG(Info, "[FBX] No mesh and no matching skeleton yet; retried after the other files: {}", Source);
			FbxResult = {};
			AdvanceFbxQueue(bFbxImportAll);
			return;
		}
		FbxResult.Error += ". Import the FBX that owns the skeleton first, or choose a Target Skeleton.";
	}
	for (const auto& Warning : FbxResult.Warnings)
	{
		HTR_LOG(Warning, "FBX: {}", Warning);
	}
	if (FbxResult.bSuccess)
	{
		HTR_LOG(Info, "FBX import completed: {} ({} assets)", Source, FbxResult.AssetKeys.Num());
		if (OnFbxImported)
		{
			OnFbxImported();
		}
	}
	else
	{
		HTR_LOG(Error, "FBX import failed: {}: {}", Source, FbxResult.Error);
		// 한 개씩 임포트할 때는 창에 남겨 옵션을 바꿔 다시 시도할 수 있게 한다.
		if (!bFbxImportAll)
		{
			bFbxImporting = false;
			return;
		}
		FbxFailures.Add(Source + ": " + FbxResult.Error);
	}
	AdvanceFbxQueue(bFbxImportAll);
}

void FEditorUI::UpdateFbxOutputDirectory()
{
	if (!bFbxOutputDirectoryEdited)
	{
		const FString Default = FbxPath::DefaultOutputDirectory(FbxSourcePath);
		strcpy_s(FbxOutputDirectory, Default.c_str());
	}
}

void FEditorUI::BrowseFbxSource()
{
	FString Error;
	const TArray<FString> Files = PickFbxFiles(static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw), false, Error);
	if (!Error.empty())
	{
		FbxResult.Error = Error;
		return;
	}
	if (Files.IsEmpty())
	{
		return;
	}
	if (Files[0].size() >= sizeof(FbxSourcePath))
	{
		FbxResult.Error = "The selected file path is too long.";
		return;
	}
	strcpy_s(FbxSourcePath, Files[0].c_str());
	UpdateFbxOutputDirectory();
	FbxResult = {};
}

void FEditorUI::BrowseFbxOutputDirectory()
{
	FString Selected;
	fs::path Initial;
	FString Ignored;
	if (!FbxPath::GetOutputDirectory(FbxOutputDirectory, Initial, Ignored))
	{
		Initial = fs::absolute("Assets");
	}
	const HRESULT Result = ChooseFbxOutputFolder(static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw), Initial, Selected);
	if (Result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
	{
		return;
	}
	if (FAILED(Result))
	{
		FbxResult.Error = "Could not open the output folder browser.";
		HTR_LOG(Error, "FBX output folder dialog failed: {}", static_cast<uint32>(Result));
		return;
	}
	fs::path Directory;
	FString Error;
	if (!FbxPath::GetOutputDirectory(Selected, Directory, Error))
	{
		FbxResult.Error = Error;
		return;
	}
	const FString Path = FbxPath::ToString(Directory);
	if (Path.size() >= sizeof(FbxOutputDirectory))
	{
		FbxResult.Error = "The selected folder path is too long.";
		return;
	}
	strcpy_s(FbxOutputDirectory, Path.c_str());
	bFbxOutputDirectoryEdited = true;
	FbxResult = {};
}

void FEditorUI::DrawFbxImportBody()
{
	const FFbxImportRequest& Request = FbxQueue[0];
	const int32 Total = FbxProcessedCount + FbxQueue.Num();

	if (bFbxImporting)
	{
		ImGui::Text("Importing %s  (%d / %d)", FbxPath::ToString(FbxPath::FromString(FbxSourcePath).filename()).c_str(), FbxProcessedCount + 1, Total);
		ImGui::ProgressBar(static_cast<float>(FbxProcessedCount) / static_cast<float>(Total), ImVec2(EditorUIScale::Px(580.0f), 0));
		bFbxImportTextDrawn = true;
		return;
	}

	if (Total > 1)
	{
		ImGui::Text("File %d / %d", FbxProcessedCount + 1, Total);
	}

	ImGui::TextUnformatted(Request.bReimport ? "Reimport from" : "Source FBX");
	const float PathWidth = ImGui::GetContentRegionAvail().x - EditorUIScale::ButtonWidth("Browse...") - ImGui::GetStyle().ItemSpacing.x;
	ImGui::SetNextItemWidth(PathWidth);
	// 대기열로 들어온 파일은 정해져 있다. 비어 있을 때만 (File 메뉴) 직접 고른다.
	ImGui::BeginDisabled(!Request.Source.empty());
	if (ImGui::InputText("##FbxSource", FbxSourcePath, sizeof(FbxSourcePath)))
	{
		UpdateFbxOutputDirectory();
		FbxResult = {};
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse..."))
	{
		BrowseFbxSource();
	}
	ImGui::EndDisabled();

	ImGui::TextUnformatted("Output folder");
	ImGui::SetNextItemWidth(PathWidth);
	ImGui::BeginDisabled(Request.bLockOutput);
	if (ImGui::InputText("##FbxOutput", FbxOutputDirectory, sizeof(FbxOutputDirectory)))
	{
		bFbxOutputDirectoryEdited = FbxOutputDirectory[0] != '\0';
		FbxResult = {};
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse...##FbxOutput"))
	{
		BrowseFbxOutputDirectory();
	}
	ImGui::EndDisabled();

	ImGui::SeparatorText("Transform");
	// 대상 Skeleton을 고르면 Skeleton을 만들 때의 변환 설정을 따른다.
	ImGui::BeginDisabled(!FbxOptions.TargetSkeletonKey.empty());
	ImGui::SetNextItemWidth(EditorUIScale::Px(160.0f));
	ImGui::DragFloat("Uniform Scale", &FbxOptions.UniformScale, 0.01f, 0.0001f, 100000.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("Multiplies vertices and bone translations.");
	ImGui::Checkbox("Force Front X Axis", &FbxOptions.bForceFrontX);
	ImGui::SetItemTooltip("On: the model faces +X (actor forward).\nOff: the model keeps the Unreal default front.");
	ImGui::EndDisabled();

	// 대상 Skeleton을 고르면 메시와 새 Skeleton은 만들지 않고, 그 Skeleton에 맞춘 AnimSequence만 만든다.
	const bool bTargetSkeleton = !FbxOptions.TargetSkeletonKey.empty();
	ImGui::SeparatorText("Target Skeleton");
	ImGui::PushID("TargetSkeleton");
	// 다시 가져오기는 원래 메시를 되살리는 작업이라 대상 Skeleton을 쓰지 않는다.
	ImGui::BeginDisabled(Request.bReimport);
	{
		const auto DisplayName = [](const FString& Key)
		{
			return FbxPath::ToString(FbxPath::FromString(Key).stem());
		};
		const FString CurrentLabel = bTargetSkeleton ? DisplayName(FbxOptions.TargetSkeletonKey) : FString("None");
		const float ClearWidth = EditorUIScale::ButtonWidth("Clear") + ImGui::GetStyle().ItemSpacing.x;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ClearWidth);
		EditorUIScale::LimitNextComboPopupWidth();
		const bool bOpen = ImGui::BeginCombo("##TargetSkeletonCombo", CurrentLabel.c_str());
		if (ImGui::IsItemHovered() && bTargetSkeleton)
		{
			ImGui::SetTooltip("%s", FbxOptions.TargetSkeletonKey.c_str());
		}
		if (bOpen)
		{
			// 로드된 Skeleton 애셋(Content Drawer에 보이는 .fbxbin)을 이름순으로 보여 준다.
			const TArray<USkeleton*> Skeletons = EditorSort::CollectSorted<USkeleton>(
				TObjectIterator<USkeleton>(),
				[&](const USkeleton* S) { return DisplayName(S->GetPath()); },
				[](const USkeleton* S) { return !S->IsRetired() && !S->GetPath().empty(); });
			if (Skeletons.IsEmpty())
			{
				ImGui::TextDisabled("No Skeleton asset");
			}
			for (USkeleton* Item : Skeletons)
			{
				ImGui::PushID(Item);
				if (ImGui::Selectable(DisplayName(Item->GetPath()).c_str(), Item->GetPath() == FbxOptions.TargetSkeletonKey))
				{
					FbxOptions.TargetSkeletonKey = Item->GetPath();
					FbxResult = {};
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", Item->GetPath().c_str());
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!bTargetSkeleton);
		if (ImGui::Button("Clear"))
		{
			FbxOptions.TargetSkeletonKey.clear();
			FbxResult = {};
		}
		ImGui::EndDisabled();
	}
	ImGui::EndDisabled();
	if (bTargetSkeleton)
	{
		ImGui::TextDisabled("Only animations are imported for this skeleton. Meshes and a new skeleton are not created.");
	}
	ImGui::PopID();

	ImGui::BeginDisabled(bTargetSkeleton);
	ImGui::SeparatorText("Static Meshes");
	ImGui::PushID("StaticFbx");
	// 메시 종류를 바꾸면 파일 이름이 달라져 기존 바이너리를 덮어쓸 수 없다.
	ImGui::BeginDisabled(Request.bReimport);
	ImGui::Checkbox("Import Static Meshes", &FbxOptions.bImportStaticMeshes);
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("On with Skeletal: meshes without a skin become static meshes unless transform keys move them.\nOn alone: skins are ignored and every mesh becomes static.");
	ImGui::Checkbox("Bake Meshes", &FbxOptions.bBakeStaticMeshes);
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("On: bake the mesh node and all parent transforms into geometry.\nOff: import local geometry without the mesh node transform.\nCombine does not restore relative placement when Bake is off.");
	}
	ImGui::Checkbox("Combine Meshes", &FbxOptions.bCombineStaticMeshes);
	ImGui::PopID();

	ImGui::SeparatorText("Skeletal Meshes");
	ImGui::PushID("SkeletalFbx");
	ImGui::BeginDisabled(Request.bReimport);
	ImGui::Checkbox("Import Skeletal Meshes", &FbxOptions.bImportSkeletalMeshes);
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Meshes without a skin use their own node as a bone.\nOn with Static: only meshes moved by transform keys become skeletal.\nOn alone: every mesh without a skin becomes skeletal.");
	ImGui::Checkbox("Bake Meshes", &FbxOptions.bBakeSkeletalMeshes);
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("On: include placement outside the rig in geometry and bind transforms.\nOff: remove the root bone parent's global bind transform; retain the root bone's own transform.");
	}
	ImGui::Checkbox("Combine Meshes", &FbxOptions.bCombineSkeletalMeshes);
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Combine meshes using the same source rig root. An LOD group remains one asset when disabled.");
	}
	ImGui::BeginDisabled(!FbxOptions.bImportSkeletalMeshes);
	ImGui::Checkbox("Import Animations", &FbxOptions.bImportAnimations);
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Each animation take becomes an AnimSequence for the imported skeleton, sampled every frame.\nTakes in which no bone moves are skipped. Requires a skeletal mesh in the same FBX, or a Target Skeleton.");
	ImGui::PopID();

	ImGui::EndDisabled();

	// Target Skeleton으로 애니메이션만 받는 재임포트에서도 쓰도록 위의 비활성 범위 밖에 둔다.
	if (Request.bReimport)
	{
		ImGui::Checkbox("Keep Animation Notifies", &FbxOptions.bKeepNotifies);
		ImGui::SetItemTooltip("On: copy the Notifies of the existing AnimSequence files into the reimported ones.\nOff: reimported AnimSequences start with no Notifies.");
	}

	if (!FFbxAssetImporter::IsAvailable())
	{
		ImGui::TextWrapped("Run Setup/SlnGenerator.bat and rebuild the editor to enable FBX import.");
	}
	if (!FbxResult.Error.empty())
	{
		ImGui::Separator();
		ImGui::TextWrapped("Import failed: %s", FbxResult.Error.c_str());
	}

	ImGui::Separator();
	const ImVec2 ButtonSize(EditorUIScale::Px(100.0f), 0);
	const bool bBlocked = IsFbxImportBlocked && IsFbxImportBlocked();
	const bool bHasOthers = FbxQueue.Num() > 1;
	ImGui::BeginDisabled(bBlocked || !FFbxAssetImporter::IsAvailable() || FbxSourcePath[0] == '\0' || FbxOutputDirectory[0] == '\0' ||
		(!FbxOptions.bImportStaticMeshes && !FbxOptions.bImportSkeletalMeshes && !bTargetSkeleton));
	if (ImGui::Button(Request.bReimport ? "Reimport" : "Import", ButtonSize))
	{
		bFbxImporting = true;
		bFbxImportAll = false;
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!bHasOthers || Request.bReimport);
	if (ImGui::Button("Import All", ButtonSize))
	{
		bFbxImporting = true;
		bFbxImportAll = true;
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Imports this and the remaining files with these options.");
	ImGui::EndDisabled();
	ImGui::SameLine();
	bool bAdvance = false;
	if (ImGui::Button("Cancel", ButtonSize))
	{
		bAdvance = true;
	}
	ImGui::SetItemTooltip("Skips this file and moves to the next one.\nFiles without a binary are asked again on the next start.");
	ImGui::SameLine();
	bool bCancelAll = false;
	ImGui::BeginDisabled(!bHasOthers);
	if (ImGui::Button("Cancel All", ButtonSize))
	{
		bCancelAll = true;
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Stops asking for now.\nFiles without a binary are asked again on the next start.");

	// Request가 가리키는 대기열을 바꾸므로 그리기가 끝난 뒤에 처리한다.
	if (bCancelAll)
	{
		FbxQueue.Reset();
		FbxProcessedCount = 0;
		bFbxDialogActive = false;
	}
	else if (bAdvance)
	{
		AdvanceFbxQueue(false);
	}
}

void FEditorUI::DrawFbxFailures()
{
	const char* PopupName = "FBX Import Failures";
	if (FbxFailures.IsEmpty() || bFbxDialogActive)
	{
		return;
	}
	if (!ImGui::IsPopupOpen(PopupName))
	{
		ImGui::OpenPopup(PopupName);
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
	if (ImGui::BeginPopupModal(PopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
	{
		ImGui::TextUnformatted("These files could not be imported:");
		for (const FString& Failure : FbxFailures)
		{
			ImGui::BulletText("%s", Failure.c_str());
		}
		ImGui::TextDisabled("See the Output Log for details.");
		ImGui::Separator();
		if (ImGui::Button("OK", ImVec2(EditorUIScale::Px(100.0f), 0)))
		{
			FbxFailures.Reset();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void FEditorUI::DrawFbxImportDialog()
{
	// 앞 프레임에 진행 표시를 그렸으므로 이번 프레임에 실행한다. 임포트 동안 화면은 그 표시로 멈춰 있다.
	if (bFbxImporting && bFbxImportTextDrawn)
	{
		RunFbxImport();
	}

	const char* PopupName = "Import FBX";
	if (!bFbxDialogActive && !FbxQueue.IsEmpty() && !(IsFbxImportBlocked && IsFbxImportBlocked()))
	{
		bFbxDialogActive = true;
		ApplyFbxRequest(FbxQueue[0], false);
	}
	const bool bPopupOpen = ImGui::IsPopupOpen(PopupName);
	if (bFbxDialogActive && !bPopupOpen)
	{
		ImGui::OpenPopup(PopupName);
	}
	if (bFbxDialogActive || bPopupOpen)
	{
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
		ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(600.0f), 0), ImGuiCond_Appearing);
		bFbxPopupOpen = true;
		if (ImGui::BeginPopupModal(PopupName, bFbxImporting ? nullptr : &bFbxPopupOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
		{
			if (bFbxDialogActive)
			{
				DrawFbxImportBody();
			}
			if (!bFbxDialogActive)
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		// X 버튼은 Cancel과 같다.
		if (!bFbxPopupOpen && bFbxDialogActive)
		{
			AdvanceFbxQueue(false);
		}
	}
	DrawFbxFailures();
}

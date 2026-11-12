#include "EnginePCH.h"
#include "Editor/ProjectBrowser/ProjectBrowser.h"

#include <cctype>
#include <fstream>

#include <d3d11.h>
#include <dxgi.h>
#include <ShlObj.h>
#include <wrl/client.h>

#include "imgui.h"
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include "Core/Misc/Paths.h"
#include "Core/Window.h"
#include "Editor/EditorUI/EditorTitleBar.h"
#include "Render/ImageLoader.h"

#pragma comment(lib, "d3d11.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// 최근 프로젝트 목록
// ---------------------------------------------------------------------------

namespace
{
	constexpr int32 MaxRecentProjects = 16;

	fs::path RecentProjectsFile()
	{
		return FPaths::EngineSavedDir() / L"RecentProjects.json";
	}

	bool SamePath(const fs::path& A, const fs::path& B)
	{
		std::error_code Ec;
		if (fs::equivalent(A, B, Ec)) return true;
		return _wcsicmp(A.lexically_normal().c_str(), B.lexically_normal().c_str()) == 0;
	}

	TArray<fs::path> ReadRecentFile()
	{
		TArray<fs::path> Result;
		std::ifstream In(RecentProjectsFile());
		if (!In) return Result;
		const json Root = json::parse(In, nullptr, false);
		if (!Root.is_array()) return Result;
		for (const json& Item : Root)
		{
			if (Item.is_string()) Result.Add(FPaths::FromUtf8(Item.get<FString>()));
		}
		return Result;
	}

	void WriteRecentFile(const TArray<fs::path>& Projects)
	{
		std::error_code Ec;
		fs::create_directories(FPaths::EngineSavedDir(), Ec);
		json Root = json::array();
		for (int32 i = 0; i < Projects.Num() && i < MaxRecentProjects; ++i)
		{
			Root.push_back(FPaths::ToUtf8(Projects[i]));
		}
		std::ofstream Out(RecentProjectsFile());
		if (Out) Out << Root.dump(4) << '\n';
	}
}

TArray<fs::path> RecentProjects::Load()
{
	TArray<fs::path> Result;
	std::error_code Ec;
	for (const fs::path& Path : ReadRecentFile())
	{
		if (fs::exists(Path, Ec)) Result.Add(Path);
	}
	return Result;
}

void RecentProjects::Add(const fs::path& ProjectFile)
{
	std::error_code Ec;
	const fs::path Absolute = fs::absolute(ProjectFile, Ec).lexically_normal();
	TArray<fs::path> Projects;
	Projects.Add(Absolute);
	for (const fs::path& Path : ReadRecentFile())
	{
		if (!SamePath(Path, Absolute)) Projects.Add(Path);
	}
	WriteRecentFile(Projects);
}

void RecentProjects::Remove(const fs::path& ProjectFile)
{
	TArray<fs::path> Projects;
	for (const fs::path& Path : ReadRecentFile())
	{
		if (!SamePath(Path, ProjectFile)) Projects.Add(Path);
	}
	WriteRecentFile(Projects);
}

// ---------------------------------------------------------------------------
// 새 프로젝트 만들기
// ---------------------------------------------------------------------------

namespace
{
	FString MakeDefaultGameIni(const FString& Name)
	{
		return
			"; HitoriGame 실행 설정. 프로젝트 폴더의 Config에서 읽는다.\n"
			"; 항목이 없거나 값이 잘못되면 코드의 기본값을 쓴다.\n"
			"\n"
			"[Game]\n"
			"; 시작할 씬. 프로젝트 폴더 기준 상대 경로. 에디터에서 이 경로로 씬을 저장한다.\n"
			"GameDefaultMap=Scenes/Main.scene\n"
			"; 비워 두면 하늘을 그리지 않는다\n"
			"SkyPanorama=\n"
			"\n"
			"[Window]\n"
			"Title=" + Name + "\n"
			"ResolutionSizeX=1600\n"
			"ResolutionSizeY=900\n"
			"Borderless=0\n"
			"VSync=1\n";
	}
}

FString ProjectCreator::ValidateName(const FString& Name)
{
	if (Name.empty()) return "Enter a project name.";
	if (Name.size() > 32) return "Project name must be 32 characters or fewer.";
	if (!std::isalpha(static_cast<unsigned char>(Name[0]))) return "Project name must start with a letter.";
	for (const char C : Name)
	{
		const unsigned char U = static_cast<unsigned char>(C);
		if (!(std::isalnum(U) || C == '_' || C == '-'))
			return "Use only letters, digits, '_' and '-' in the project name.";
	}
	return {};
}

bool ProjectCreator::Create(const FString& Name, const fs::path& ParentDir, fs::path& OutProjectFile, FString& OutError)
{
	OutError = ValidateName(Name);
	if (!OutError.empty()) return false;

	std::error_code Ec;
	if (ParentDir.empty() || !fs::is_directory(ParentDir, Ec))
	{
		OutError = "Location folder does not exist.";
		return false;
	}

	const fs::path ProjectDir = ParentDir / FPaths::FromUtf8(Name);
	if (fs::exists(ProjectDir, Ec) && !fs::is_empty(ProjectDir, Ec))
	{
		OutError = "Folder already exists and is not empty: " + FPaths::ToUtf8(ProjectDir);
		return false;
	}

	for (const wchar_t* Folder : { L"Assets", L"Scenes", L"Scripts", L"Config" })
	{
		fs::create_directories(ProjectDir / Folder, Ec);
		if (Ec)
		{
			OutError = "Cannot create folder: " + FPaths::ToUtf8(ProjectDir / Folder) + " (" + Ec.message() + ")";
			return false;
		}
	}

	// 아이콘, 폰트, Lua 템플릿 같은 엔진용 파일은 엔진 콘텐츠(Engine/...)에서 바로 읽으므로 복사하지 않는다.

	{
		std::ofstream Ini(ProjectDir / L"Config" / L"DefaultGame.ini");
		Ini << MakeDefaultGameIni(Name);
	}

	FProjectDescriptor Descriptor;
	Descriptor.Name = Name;
	OutProjectFile = ProjectDir / (FPaths::FromUtf8(Name).native() + FPaths::ProjectExtension);
	if (!Descriptor.Save(OutProjectFile))
	{
		OutError = "Cannot write project file: " + FPaths::ToUtf8(OutProjectFile);
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// 프로젝트 브라우저 창
// ---------------------------------------------------------------------------

namespace
{
	// 창 자체는 FWindow가 맡고, 여기에는 브라우저만 쓰는 D3D 자원을 둔다.
	struct FBrowserWindow
	{
		HWND Hwnd = nullptr;
		ComPtr<ID3D11Device> Device;
		ComPtr<ID3D11DeviceContext> Context;
		ComPtr<IDXGISwapChain> SwapChain;
		ComPtr<ID3D11RenderTargetView> RenderTarget;
	};

	void CreateRenderTarget(FBrowserWindow& Window)
	{
		ComPtr<ID3D11Texture2D> BackBuffer;
		if (SUCCEEDED(Window.SwapChain->GetBuffer(0, IID_PPV_ARGS(&BackBuffer))))
			Window.Device->CreateRenderTargetView(BackBuffer.Get(), nullptr, &Window.RenderTarget);
	}

	// 엔진 콘텐츠의 로고 이미지를 텍스처로 만든다. 파일이 없거나 읽지 못하면 nullptr이고, 제목 표시줄은 빈 칸으로 남는다.
	ComPtr<ID3D11ShaderResourceView> CreateLogoTexture(ID3D11Device* Device)
	{
		const FImageData Image = ImageLoader::Load(FPaths::ResolveContentPath("Engine/Icons/Logo.png"));
		if (!Image.IsValid()) return nullptr;

		D3D11_TEXTURE2D_DESC Desc{};
		Desc.Width = Image.Width;
		Desc.Height = Image.Height;
		Desc.MipLevels = 1;
		Desc.ArraySize = 1;
		Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		Desc.SampleDesc.Count = 1;
		Desc.Usage = D3D11_USAGE_IMMUTABLE;
		Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA Data{};
		Data.pSysMem = Image.Pixels.GetData();
		Data.SysMemPitch = Image.GetRowPitch();

		ComPtr<ID3D11Texture2D> Texture;
		ComPtr<ID3D11ShaderResourceView> View;
		if (FAILED(Device->CreateTexture2D(&Desc, &Data, &Texture))) return nullptr;
		if (FAILED(Device->CreateShaderResourceView(Texture.Get(), nullptr, &View))) return nullptr;
		return View;
	}

	bool CreateDevice(FBrowserWindow& Window)
	{
		DXGI_SWAP_CHAIN_DESC Desc{};
		Desc.BufferCount = 2;
		Desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		Desc.OutputWindow = Window.Hwnd;
		Desc.SampleDesc.Count = 1;
		Desc.Windowed = TRUE;
		Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		const D3D_FEATURE_LEVEL Levels[] = { D3D_FEATURE_LEVEL_11_0 };
		const HRESULT Hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
			Levels, 1, D3D11_SDK_VERSION, &Desc, &Window.SwapChain, &Window.Device, nullptr, &Window.Context);
		if (FAILED(Hr)) return false;
		CreateRenderTarget(Window);
		return true;
	}

	// 브라우저가 떠 있는 동안만 FWindow의 WndProc 훅으로 등록한다. 에디터의 ImGui 훅은 엔진 초기화 때 따로 등록된다.
	bool BrowserWndProcHook(HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam)
	{
		return ImGui_ImplWin32_WndProcHandler(Hwnd, Msg, WParam, LParam) != 0;
	}

	// IFileOpenDialog로 폴더나 .hproject 파일을 고른다. 취소하면 빈 경로.
	fs::path ShowOpenDialog(HWND Owner, bool bPickFolder, const fs::path& InitialDir)
	{
		ComPtr<IFileOpenDialog> Dialog;
		if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Dialog))))
			return {};

		DWORD Options = 0;
		Dialog->GetOptions(&Options);
		Options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
		if (bPickFolder)
		{
			Options |= FOS_PICKFOLDERS;
			Dialog->SetTitle(L"Select Project Location");
		}
		else
		{
			Options |= FOS_FILEMUSTEXIST;
			const COMDLG_FILTERSPEC Filter[] = { { L"Hitori Project (*.hproject)", L"*.hproject" } };
			Dialog->SetFileTypes(1, Filter);
			Dialog->SetTitle(L"Open Project");
		}
		Dialog->SetOptions(Options);

		std::error_code Ec;
		if (!InitialDir.empty() && fs::is_directory(InitialDir, Ec))
		{
			ComPtr<IShellItem> Folder;
			if (SUCCEEDED(SHCreateItemFromParsingName(InitialDir.c_str(), nullptr, IID_PPV_ARGS(&Folder))))
				Dialog->SetFolder(Folder.Get());
		}

		if (FAILED(Dialog->Show(Owner))) return {};

		ComPtr<IShellItem> Result;
		if (FAILED(Dialog->GetResult(&Result))) return {};
		PWSTR Path = nullptr;
		if (FAILED(Result->GetDisplayName(SIGDN_FILESYSPATH, &Path))) return {};
		fs::path Out(Path);
		CoTaskMemFree(Path);
		return Out;
	}

	fs::path DefaultProjectLocation()
	{
		PWSTR Documents = nullptr;
		fs::path Result;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &Documents)))
			Result = fs::path(Documents) / L"HitoriProjects";
		CoTaskMemFree(Documents);
		return Result;
	}

	FString NextFreeProjectName(const fs::path& Location)
	{
		std::error_code Ec;
		for (int32 i = 1; i < 1000; ++i)
		{
			const FString Name = "MyProject" + std::to_string(i);
			if (!fs::exists(Location / FPaths::FromUtf8(Name), Ec)) return Name;
		}
		return "MyProject";
	}

	void CopyToBuffer(const FString& Text, char* Buffer, size_t Size)
	{
		strncpy_s(Buffer, Size, Text.c_str(), _TRUNCATE);
	}

	// 창 안의 화면 상태와 그리기.
	struct FBrowserUI
	{
		TArray<fs::path> Recent;
		int32 SelectedRecent = -1;
		char NameBuffer[64] = {};
		char LocationBuffer[1024] = {};
		FString Error;
		fs::path Chosen;

		void Init()
		{
			Recent = RecentProjects::Load();
			SelectedRecent = Recent.Num() > 0 ? 0 : -1;
			const fs::path Location = DefaultProjectLocation();
			CopyToBuffer(FPaths::ToUtf8(fs::path(Location).make_preferred()), LocationBuffer, sizeof(LocationBuffer));
			CopyToBuffer(NextFreeProjectName(Location), NameBuffer, sizeof(NameBuffer));
		}

		void TryOpen(const fs::path& ProjectFile)
		{
			FProjectDescriptor Descriptor;
			FString LoadError;
			if (!Descriptor.Load(ProjectFile, &LoadError))
			{
				Error = LoadError;
				return;
			}
			Chosen = ProjectFile;
		}

		void DrawRecent(HWND Hwnd, float Height)
		{
			ImGui::BeginChild("Recent", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, Height), ImGuiChildFlags_Borders);
			ImGui::SeparatorText("Recent Projects");

			const float ButtonRow = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
			ImGui::BeginChild("RecentList", ImVec2(0, -ButtonRow));
			if (Recent.IsEmpty())
				ImGui::TextDisabled("No recent projects.");

			for (int32 i = 0; i < Recent.Num(); ++i)
			{
				ImGui::PushID(i);
				const FString Name = FPaths::ToUtf8(Recent[i].stem());
				const FString Path = FPaths::ToUtf8(Recent[i].parent_path().make_preferred());
				const float RowHeight = ImGui::GetTextLineHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
				// Selectable은 칸 사이 간격을 메우려고 사각형을 왼쪽으로 넓힌다.
				// GetItemRectMin()에서 글자를 그리면 첫 글자가 잘리므로 그리기 전 커서 위치를 쓴다.
				const ImVec2 RowStart = ImGui::GetCursorScreenPos();
				if (ImGui::Selectable("##row", SelectedRecent == i, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, RowHeight)))
				{
					SelectedRecent = i;
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) TryOpen(Recent[i]);
				}
				ImGui::SetCursorScreenPos(RowStart);
				ImGui::TextUnformatted(Name.c_str());
				ImGui::TextDisabled("%s", Path.c_str());
				ImGui::PopID();
			}
			ImGui::EndChild();

			const bool bHasSelection = SelectedRecent >= 0 && SelectedRecent < Recent.Num();
			ImGui::BeginDisabled(!bHasSelection);
			if (ImGui::Button("Open")) TryOpen(Recent[SelectedRecent]);
			ImGui::SameLine();
			if (ImGui::Button("Remove from List"))
			{
				RecentProjects::Remove(Recent[SelectedRecent]);
				Recent = RecentProjects::Load();
				SelectedRecent = Recent.Num() > 0 ? 0 : -1;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Browse..."))
			{
				const fs::path File = ShowOpenDialog(Hwnd, false, {});
				if (!File.empty()) TryOpen(File);
			}
			ImGui::EndChild();
		}

		void DrawCreate(HWND Hwnd, float Height)
		{
			ImGui::BeginChild("Create", ImVec2(0, Height), ImGuiChildFlags_Borders);
			ImGui::SeparatorText("New Project");

			ImGui::TextUnformatted("Name");
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputText("##Name", NameBuffer, sizeof(NameBuffer));

			ImGui::Spacing();
			ImGui::TextUnformatted("Location");
			const float BrowseWidth = ImGui::CalcTextSize("...").x + ImGui::GetStyle().FramePadding.x * 4.0f;
			ImGui::SetNextItemWidth(-(BrowseWidth + ImGui::GetStyle().ItemSpacing.x));
			ImGui::InputText("##Location", LocationBuffer, sizeof(LocationBuffer));
			ImGui::SameLine();
			if (ImGui::Button("...", ImVec2(BrowseWidth, 0)))
			{
				const fs::path Folder = ShowOpenDialog(Hwnd, true, FPaths::FromUtf8(LocationBuffer));
				if (!Folder.empty()) CopyToBuffer(FPaths::ToUtf8(Folder), LocationBuffer, sizeof(LocationBuffer));
			}

			const FString Name = NameBuffer;
			const fs::path Location = FPaths::FromUtf8(LocationBuffer);
			const FString NameError = ProjectCreator::ValidateName(Name);

			ImGui::Spacing();
			ImGui::TextDisabled("Project file");
			if (NameError.empty())
			{
				fs::path Preview = Location / FPaths::FromUtf8(Name) / (FPaths::FromUtf8(Name).native() + FPaths::ProjectExtension);
				ImGui::TextWrapped("%s", FPaths::ToUtf8(Preview.make_preferred()).c_str());
			}
			else
			{
				ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s", NameError.c_str());
			}

			ImGui::Spacing();
			ImGui::TextDisabled("Creates Assets, Scenes, Scripts and Config folders.\nEngine icons and fonts are shared from the engine folder.");

			ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetFrameHeightWithSpacing() - ImGui::GetStyle().WindowPadding.y);
			ImGui::BeginDisabled(!NameError.empty() || Location.empty());
			if (ImGui::Button("Create Project", ImVec2(-FLT_MIN, 0)))
			{
				std::error_code Ec;
				fs::create_directories(Location, Ec);
				fs::path ProjectFile;
				FString CreateError;
				if (ProjectCreator::Create(Name, Location, ProjectFile, CreateError))
					Chosen = ProjectFile;
				else
					Error = CreateError;
			}
			ImGui::EndDisabled();
			ImGui::EndChild();
		}

		void Draw(HWND Hwnd)
		{
			// 에디터와 같은 제목 표시줄을 맨 위에 그린다. 그 높이만큼 WorkPos가 내려가 본문이 그 아래에 온다.
			EditorTitleBar::Draw();

			const ImGuiViewport* Viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(Viewport->WorkPos);
			ImGui::SetNextWindowSize(Viewport->WorkSize);
			ImGui::Begin("Project Browser", nullptr,
				ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

			ImGui::TextDisabled("Engine: %s", FPaths::ToUtf8(FPaths::EngineDir()).c_str());
			ImGui::Spacing();

			const float ErrorHeight = Error.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing() * 2.0f;
			const float Height = ImGui::GetContentRegionAvail().y - ErrorHeight;
			DrawRecent(Hwnd, Height);
			ImGui::SameLine();
			DrawCreate(Hwnd, Height);

			if (!Error.empty())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", Error.c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton("Dismiss")) Error.clear();
			}
			ImGui::End();
		}
	};

	void ApplyStyle(float Scale)
	{
		ImGui::StyleColorsDark();
		ImGuiStyle& Style = ImGui::GetStyle();
		Style.WindowRounding = 0.0f;
		Style.ChildRounding = 4.0f;
		Style.FrameRounding = 4.0f;
		Style.FramePadding = ImVec2(8.0f, 5.0f);
		Style.ItemSpacing = ImVec2(8.0f, 6.0f);
		// 에디터와 같은 강조 색
		ImVec4* Colors = Style.Colors;
		Colors[ImGuiCol_Button] = ImVec4(0.65f, 0.30f, 0.45f, 1.00f);
		Colors[ImGuiCol_ButtonHovered] = ImVec4(0.75f, 0.40f, 0.55f, 1.00f);
		Colors[ImGuiCol_ButtonActive] = ImVec4(0.55f, 0.20f, 0.35f, 1.00f);
		Colors[ImGuiCol_Header] = ImVec4(0.65f, 0.30f, 0.45f, 0.55f);
		Colors[ImGuiCol_HeaderHovered] = ImVec4(0.75f, 0.40f, 0.55f, 0.70f);
		Colors[ImGuiCol_HeaderActive] = ImVec4(0.55f, 0.20f, 0.35f, 1.00f);
		Style.ScaleAllSizes(Scale);
	}
}

fs::path ProjectBrowser::Run(HINSTANCE hInstance)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	const HRESULT ComResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

	// 주 모니터 DPI에 맞춰 창 크기와 글자 크기를 키운다.
	const float Scale = static_cast<float>(GetDpiForSystem()) / 96.0f;
	const int32 Width = static_cast<int32>(1000 * Scale);
	const int32 Height = static_cast<int32>(600 * Scale);

	// 에디터 메인 창과 같은 FWindow를 쓴다. 기본 제목 표시줄 제거와 히트 테스트도 그대로 가져온다.
	FWindow MainWindow;
	FBrowserWindow Window;
	if (MainWindow.Create(hInstance, Width, Height, L"Hitori Project Browser"))
	{
		Window.Hwnd = MainWindow.GetHandle();
		// 닫기는 앱 종료(WM_QUIT)가 아니라 요청만 남긴다. WM_QUIT이 엔진 메인 루프까지 남으면 안 된다.
		MainWindow.SetQuitOnClose(false);

		RECT Rect;
		GetWindowRect(Window.Hwnd, &Rect);
		const int32 X = (GetSystemMetrics(SM_CXSCREEN) - (Rect.right - Rect.left)) / 2;
		const int32 Y = (GetSystemMetrics(SM_CYSCREEN) - (Rect.bottom - Rect.top)) / 2;
		SetWindowPos(Window.Hwnd, nullptr, X, Y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	fs::path Result;
	if (Window.Hwnd && CreateDevice(Window))
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& IO = ImGui::GetIO();
		// 에디터의 imgui.ini와 섞이지 않게 창 배치를 저장하지 않는다.
		IO.IniFilename = nullptr;
		IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		ApplyStyle(Scale);
		const FString FontPath = FPaths::EngineFileUtf8(L"NanumSquareR.TTF");
		std::error_code Ec;
		if (!fs::exists(FPaths::EngineFile(L"NanumSquareR.TTF"), Ec) ||
			!IO.Fonts->AddFontFromFileTTF(FontPath.c_str(), 17.0f * Scale, nullptr, IO.Fonts->GetGlyphRangesKorean()))
		{
			ImFontConfig Config;
			Config.SizePixels = 15.0f * Scale;
			IO.Fonts->AddFontDefault(&Config);
		}

		ImGui_ImplWin32_Init(Window.Hwnd);
		ImGui_ImplDX11_Init(Window.Device.Get(), Window.Context.Get());
		FWindow::SetWndProcHook(&BrowserWndProcHook);

		// 기본 제목 표시줄을 없애고 메뉴 바를 제목 표시줄로 쓴다. 클라이언트 크기가 바뀌므로 아래 루프에서 스왑체인을 맞춘다.
		EditorTitleBar::Install(MainWindow);
		// 에디터와 같은 로고를 왼쪽 위에 그린다. 엔진(UAssetManager)이 아직 없으므로 이 창의 장치로 직접 텍스처를 만든다.
		const ComPtr<ID3D11ShaderResourceView> Logo = CreateLogoTexture(Window.Device.Get());
		if (Logo) EditorTitleBar::SetLogo((ImTextureID)Logo.Get());
		MainWindow.Show();

		FBrowserUI UI;
		UI.Init();

		bool bRunning = true;
		while (bRunning && UI.Chosen.empty())
		{
			MainWindow.ProcessMessage(bRunning);
			if (MainWindow.ConsumeCloseRequest()) break;

			if (MainWindow.CheckResized() && MainWindow.GetWidth() != 0 && MainWindow.GetHeight() != 0)
			{
				Window.RenderTarget.Reset();
				Window.SwapChain->ResizeBuffers(0, MainWindow.GetWidth(), MainWindow.GetHeight(), DXGI_FORMAT_UNKNOWN, 0);
				CreateRenderTarget(Window);
			}

			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			UI.Draw(Window.Hwnd);
			ImGui::Render();

			const float Clear[4] = { 0.08f, 0.08f, 0.09f, 1.0f };
			ID3D11RenderTargetView* Target = Window.RenderTarget.Get();
			Window.Context->OMSetRenderTargets(1, &Target, nullptr);
			Window.Context->ClearRenderTargetView(Target, Clear);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			Window.SwapChain->Present(1, 0);
		}

		Result = UI.Chosen;

		// 에디터가 자기 창으로 다시 Install하고 자기 훅을 등록하기 전까지 브라우저 것을 남기지 않는다.
		EditorTitleBar::Uninstall();
		FWindow::SetWndProcHook(nullptr);
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}
	else
	{
		MessageBoxW(nullptr, L"Cannot create the project browser window.", L"Hitori", MB_OK | MB_ICONERROR);
	}

	Window.RenderTarget.Reset();
	Window.SwapChain.Reset();
	Window.Context.Reset();
	Window.Device.Reset();
	MainWindow.Destroy();

	if (SUCCEEDED(ComResult)) CoUninitialize();
	return Result;
}

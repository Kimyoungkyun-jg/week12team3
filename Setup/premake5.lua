-- TEAM5와 동일하게 빌드 설정은 Setup/, 솔루션과 콘텐츠 기준은 저장소 루트다.
local RepoRoot = path.getabsolute(path.join(_MAIN_SCRIPT_DIR, ".."))
os.chdir(RepoRoot)

workspace "Hitori"
	location(RepoRoot)
	architecture "x64"
	startproject "HitoriEditor"

	configurations
	{
		"Debug",
		"Release",
		"Shipping",
	}


	multiprocessorcompile "On"

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

IncludeDir = {}
IncludeDir["ImGui"] = "Source/ThirdParty/ImGui"
IncludeDir["ImPlot"] = "Source/ThirdParty/ImPlot"
IncludeDir["ImGuizmo"] = "Source/ThirdParty/ImGuizmo/src"
IncludeDir["ImGuiColorGradient"] = "Source/ThirdParty/ImGuiColorGradient"
IncludeDir["stb"]   = "Source/ThirdParty/stb"
IncludeDir["json"]  = "Source/ThirdParty/json"
IncludeDir["DDSLoader"] = "Source/ThirdParty/DDSLoader"
IncludeDir["lua"]   = "Source/ThirdParty/lua/src"
IncludeDir["sol2"]  = "Source/ThirdParty/sol2/include"
IncludeDir["miniaudio"]  = "Source/ThirdParty/miniaudio"
IncludeDir["FBX"]        = "Source/ThirdParty/FBX/include"
FbxLibDir = "Source/ThirdParty/FBX/lib/x64"

-- premake의 filter는 project()를 만나면 초기화된다.
-- 두 프로젝트가 같은 런타임(/MDd vs /MD)으로 컴파일되지 않으면 링크가 실패하므로
-- 공통 설정을 함수로 묶어 각 프로젝트에서 호출한다.
function CommonSettings()
	language   "C++"
	cppdialect "C++20"
	staticruntime "off"
	characterset  "Unicode"

	targetdir ("Build/Bin/" .. outputdir)
	objdir    ("Build/Intermediate/" .. outputdir)
	-- 에셋·쉐이더를 상대 경로로 읽으므로 작업 디렉터리는 저장소 루트다.
	debugdir  "%{wks.location}"

	defines
	{
		"WIN32_LEAN_AND_MEAN",
		"NOMINMAX",
		"UNICODE",
		"_UNICODE",
		"SOL_ALL_SAFETIES_ON=1", 
		"FBXSDK_SHARED",   -- FBX SDK를 DLL로 링크한다
	}

	filter "files:**.hlsl"
		excludefrombuild "On"

	-- exe에만 공용 매니페스트(UTF-8 코드페이지 + 긴 경로)를 넣는다. 정적 라이브러리에는 의미가 없다.
	filter "kind:ConsoleApp or WindowedApp"
		files { "Source/Runtime/Launch/Hitori.manifest" }

	filter "system:windows"
		systemversion "latest"
		buildoptions { "/utf-8" }
		defines { "ENGINE_PLATFORM_WINDOWS" }

	filter "configurations:Debug"
		defines { "ENGINE_DEBUG", "_DEBUG" }
		runtime  "Debug"
		symbols  "on"

	filter "configurations:Release"
		defines  { "ENGINE_RELEASE", "NDEBUG" }
		runtime  "Release"
		optimize "on"
		symbols  "on"

	filter "configurations:Shipping"
		defines  { "ENGINE_RELEASE", "ENGINE_SHIPPING", "NDEBUG" }
		runtime  "Release"
		optimize "speed"
		symbols  "on"


	filter {}
end


-- 외부 라이브러리는 별도 프로젝트 없이 쓰는 쪽에 소스를 직접 넣는다.
-- Lua는 HitoriEngine(스크립팅), ImGui는 HitoriEditor에 들어간다. 둘 다 미리 컴파일된 헤더를 쓰지 않는다.
-- ImGui 소스는 서브모듈 대신 이 저장소에 직접 포함한다. 출처는 ImGui/README.vendor.md 참고.

-- 헤더 툴 (UE의 UnrealHeaderTool 역할). 엔진 코드에 의존하지 않고 std만 쓴다.
-- HitoriEngine / HitoriEditor가 빌드 전에 이 exe로 *.generated.h를 만든다.
HeaderTool = '"%{wks.location}/Build/Bin/' .. outputdir .. '/HitoriHeaderTool.exe"'
-- 빌드 중 생기는 파일은 모두 Build/ 아래에 둔다: Bin(결과물), Intermediate(obj·pch), Generated(헤더 툴), ProjectFiles(vcxproj)
GeneratedDir = "%{wks.location}/Build/Generated"
ProjectDir = "Build/ProjectFiles"

project "HitoriHeaderTool"
	location(ProjectDir)
	vpaths { ["*"] = "Source/Programs/HitoriHeaderTool/**" }
	kind     "ConsoleApp"
	CommonSettings()

	files
	{
		"Source/Programs/HitoriHeaderTool/**.h",
		"Source/Programs/HitoriHeaderTool/**.cpp",
	}

-- 런타임. 에디터를 모른다. include 경로에 Source/Editor가 없는 것이 그 방벽이다.
-- 프로젝트 파일은 Build/ProjectFiles에 두고, 솔루션 탐색기 폴더는 vpaths로 소스 폴더 구조를 그대로 보여 준다.
project "HitoriEngine"
	location(ProjectDir)
	kind     "StaticLib"
	CommonSettings()

	pchheader "EnginePCH.h"
	pchsource "Source/Runtime/EnginePCH.cpp"

	files
	{
		"Source/Runtime/**.h",
		"Source/Runtime/**.hpp",
		"Source/Runtime/**.cpp",
		"%{IncludeDir.DDSLoader}/*.h",
		"%{IncludeDir.DDSLoader}/*.cpp",
		"%{IncludeDir.lua}/*.h",
		"%{IncludeDir.lua}/*.c",
	}
	-- lua.c·luac.c는 main이 있는 단독 실행 파일용이라 뺀다.
	removefiles { "%{IncludeDir.lua}/lua.c", "%{IncludeDir.lua}/luac.c" }
	vpaths
	{
		["ThirdParty/DDSLoader"] = "%{IncludeDir.DDSLoader}/*",
		["ThirdParty/Lua"] = "%{IncludeDir.lua}/*",
		["*"] = "Source/Runtime/**",
	}

	includedirs
	{
		"Source/Runtime",
		"Build/Generated/Runtime",   -- 추가
		"%{IncludeDir.stb}",
		"%{IncludeDir.json}",
		"%{IncludeDir.DDSLoader}",
		"%{IncludeDir.miniaudio}",
		"%{IncludeDir.lua}",
		"%{IncludeDir.sol2}",
		"%{IncludeDir.FBX}",
	}

	dependson { "HitoriHeaderTool" }
	prebuildcommands { HeaderTool .. ' Runtime "' .. GeneratedDir .. '/Runtime" "%{wks.location}/Source/Runtime"' }

	-- sol2 템플릿이 섹션 수 제한을 넘긴다
	filter "files:Source/Runtime/Scripting/**.cpp"
		buildoptions { "/bigobj" }
	filter {}

	-- d3d11·dxgi 등 시스템 라이브러리는 여기서 링크하지 않는다. exe 프로젝트들이 링크한다.
	-- 정적 라이브러리가 임포트 라이브러리를 품고 있으면 exe의 /WHOLEARCHIVE가 그것까지 끌어와
	-- __NULL_IMPORT_DESCRIPTOR 중복 정의(LNK2005)가 난다.

	filter "files:Source/ThirdParty/DDSLoader/**.cpp"
		enablepch "Off"
	filter {}

	-- Lua 5.4는 C로 컴파일한다.
	filter "files:Source/ThirdParty/lua/**.c"
		compileas "C"
		enablepch "Off"
	filter {}

	filter "files:Source/Runtime/Audio/MiniaudioImpl.cpp"
		enablepch "Off"
	filter {}

-- 에디터 애플리케이션.
project "HitoriEditor"
	location(ProjectDir)
	kind     "WindowedApp"
	CommonSettings()

	pchheader "EnginePCH.h"
	pchsource "Source/Editor/EditorPCH.cpp"

	links { "HitoriEngine" }

	-- UClass는 헤더의 정적 객체가 등록하므로, exe가 직접 참조하지 않는 클래스의 .obj를 링커가 버리면
	-- 그 클래스가 등록되지 않아 씬 로드 때 액터가 사라진다. 엔진 .obj를 전부 링크해 막는다.
	linkoptions { "/WHOLEARCHIVE:HitoriEngine", "/ignore:4099" }

	files
	{
		"Source/Editor/**.h",
		"Source/Editor/**.cpp",
		"Source/Editor/**.rc",   -- 창·exe 아이콘
		"%{IncludeDir.ImGui}/*.h",
		"%{IncludeDir.ImGui}/*.cpp",
		"%{IncludeDir.ImGui}/backends/imgui_impl_win32.*",
		"%{IncludeDir.ImGui}/backends/imgui_impl_dx11.*",
		"%{IncludeDir.ImPlot}/implot.h",
		"%{IncludeDir.ImPlot}/implot_internal.h",
		"%{IncludeDir.ImPlot}/implot.cpp",
		"%{IncludeDir.ImPlot}/implot_items.cpp",
		"%{IncludeDir.ImGuizmo}/ImCurveEdit.h",
		"%{IncludeDir.ImGuizmo}/ImCurveEdit.cpp",
		"%{IncludeDir.ImGuizmo}/ImSequencer.h",
		"%{IncludeDir.ImGuizmo}/ImSequencer.cpp",
		"%{IncludeDir.ImGuiColorGradient}/*.h",
		"%{IncludeDir.ImGuiColorGradient}/*.cpp",
	}
	vpaths
	{
		["ThirdParty/ImGui/*"] = "%{IncludeDir.ImGui}/**",
		["ThirdParty/ImPlot/*"] = "%{IncludeDir.ImPlot}/**",
		["ThirdParty/ImGuizmo/*"] = "%{IncludeDir.ImGuizmo}/**",
		["ThirdParty/ImGuiColorGradient/*"] = "%{IncludeDir.ImGuiColorGradient}/**",
		["*"] = "Source/Editor/**",
	}

	filter "files:Source/ThirdParty/**.cpp"
		enablepch "Off"
	filter {}

	dependson { "HitoriHeaderTool" }
	prebuildcommands { HeaderTool .. ' Editor "' .. GeneratedDir .. '/Editor" "%{wks.location}/Source/Editor"' }

	includedirs
	{
		"Source",             -- "Editor/OutputLog/ConsolePanel.h"처럼 Source 기준 include를 쓴다
		"Source/Runtime",
		"Build/Generated/Runtime",   -- 추가: 엔진 헤더가 include하는 generated.h
		"Build/Generated/Editor",    -- 추가
		"%{IncludeDir.ImGui}",
		"%{IncludeDir.ImGui}/backends",
		"%{IncludeDir.ImPlot}",
		"%{IncludeDir.ImGuizmo}",
		"%{IncludeDir.ImGuiColorGradient}",
		"%{IncludeDir.stb}",
		"%{IncludeDir.json}",
		"%{IncludeDir.lua}",
		"%{IncludeDir.sol2}",
		"%{IncludeDir.FBX}",
	}

	links
	{
		"d3d11.lib",
		"dxgi.lib",
		"dxguid.lib",
		"d3dcompiler.lib",
		"ws2_32.lib",
		"wininet.lib",
		"bcrypt.lib",
	}

	-- FBX SDK 2020.3.11 DLL 판. 정적 라이브러리(libfbxsdk-md 등)는 저장소에 없어 DLL 판을 쓴다.
	links { "libfbxsdk.lib" }

	filter "configurations:Debug"
		libdirs { FbxLibDir .. "/debug" }
		postbuildcommands { "{COPYFILE} %[%{wks.location}/" .. FbxLibDir .. "/debug/libfbxsdk.dll] %[%{wks.location}/Build/Bin/" .. outputdir .. "]" }

	filter "configurations:Release or configurations:Shipping"
		libdirs { FbxLibDir .. "/release" }
		postbuildcommands { "{COPYFILE} %[%{wks.location}/" .. FbxLibDir .. "/release/libfbxsdk.dll] %[%{wks.location}/Build/Bin/" .. outputdir .. "]" }

	filter {}

-- 패키징용 게임 실행 파일. 에디터 소스와 ImGui를 링크하지 않는다.
-- 에셋과 셰이더는 실행 중에 읽으므로 배포 시 Setup/Package.bat으로 콘텐츠를 함께 복사한다.
project "HitoriGame"
	location(ProjectDir)
	vpaths { ["*"] = "Source/Game/**" }
	kind     "WindowedApp"
	CommonSettings()
	targetname "HitoriGame"

	pchheader "EnginePCH.h"
	pchsource "Source/Game/GamePCH.cpp"

	links { "HitoriEngine" }

	-- 에디터와 같은 이유. 게임 코드는 라이트 액터 등을 직접 참조하지 않아 특히 빠지기 쉽다.
	linkoptions { "/WHOLEARCHIVE:HitoriEngine", "/ignore:4099" }

	files
	{
		"Source/Game/**.h",
		"Source/Game/**.cpp",
		"Source/Game/**.rc",   -- 아이콘을 넣을 때
	}

	includedirs
	{
		"Source",
		"Source/Runtime",
		"Build/Generated/Runtime",
		"%{IncludeDir.stb}",
		"%{IncludeDir.json}",
		"%{IncludeDir.lua}",
		"%{IncludeDir.sol2}",
		"%{IncludeDir.FBX}",
	}

	links
	{
		"d3d11.lib",
		"dxgi.lib",
		"dxguid.lib",
		"d3dcompiler.lib",
		"ws2_32.lib",
		"wininet.lib",
		"bcrypt.lib",
	}

	-- FBX SDK 2020.3.11 DLL 판. 정적 라이브러리(libfbxsdk-md 등)는 저장소에 없어 DLL 판을 쓴다.
	links { "libfbxsdk.lib" }

	filter "configurations:Debug"
		libdirs { FbxLibDir .. "/debug" }
		postbuildcommands { "{COPYFILE} %[%{wks.location}/" .. FbxLibDir .. "/debug/libfbxsdk.dll] %[%{wks.location}/Build/Bin/" .. outputdir .. "]" }

	filter "configurations:Release or configurations:Shipping"
		libdirs { FbxLibDir .. "/release" }
		postbuildcommands { "{COPYFILE} %[%{wks.location}/" .. FbxLibDir .. "/release/libfbxsdk.dll] %[%{wks.location}/Build/Bin/" .. outputdir .. "]" }

	filter {}


-- 셰이더를 VS에서 편집하기 위한 보기 전용 프로젝트 (언리얼의 Shaders와 같은 역할).
-- 에셋(Assets)은 에디터 Content Drawer에서 다루므로 솔루션에 넣지 않는다.
-- 새 셰이더 파일은 Setup/SlnGenerator.bat을 다시 돌려야 목록에 반영된다.
project "Shaders"
	location(ProjectDir)
	kind     "Utility"

	files { "Resources/Shader/**.hlsl" }
	files { "Resources/Shader/**.hlsli" }
	vpaths {
		["Include"] = "Resources/Shader/**.hlsli",
		["*"] = "Resources/Shader/**.hlsl"
	}

	filter "files:**.hlsl"
		excludefrombuild "On"

	filter "files:**.hlsli"
		excludefrombuild "On"

	filter {}



#include "EnginePCH.h"
#include "Core/Misc/Paths.h"

#include <fstream>
#include <Windows.h>

namespace fs = std::filesystem;

namespace
{
	fs::path GEngineDir;
	fs::path GProjectDir;
	fs::path GProjectFile;
	FString GProjectName;

	bool StartsWithFolder(const FString& Path, const char* Folder)
	{
		const size_t Length = std::strlen(Folder);
		if (Path.size() <= Length) return false;
		if (_strnicmp(Path.c_str(), Folder, Length) != 0) return false;
		return Path[Length] == '/' || Path[Length] == '\\';
	}
}

bool FPaths::InitEngineDir()
{
	wchar_t ExePath[MAX_PATH] = {};
	if (!GetModuleFileNameW(nullptr, ExePath, MAX_PATH)) return false;

	std::error_code Ec;
	fs::path Dir = fs::path(ExePath).parent_path();
	while (!Dir.empty())
	{
		if (fs::exists(Dir / L"Resources" / L"Shader", Ec))
		{
			GEngineDir = Dir;
			return true;
		}
		const fs::path Parent = Dir.parent_path();
		if (Parent == Dir) break;
		Dir = Parent;
	}

	// exe 위쪽에 엔진 폴더가 없으면 예전처럼 작업 디렉터리(디버거의 debugdir)를 엔진 폴더로 본다.
	GEngineDir = fs::current_path(Ec);
	return fs::exists(GEngineDir / L"Resources" / L"Shader", Ec);
}

const fs::path& FPaths::EngineDir() { return GEngineDir; }
const fs::path& FPaths::ProjectDir() { return GProjectDir; }
const fs::path& FPaths::ProjectFilePath() { return GProjectFile; }
const FString& FPaths::ProjectName() { return GProjectName; }
bool FPaths::HasProject() { return !GProjectFile.empty(); }

bool FPaths::SetProject(const fs::path& ProjectFile, FString* OutError)
{
	std::error_code Ec;
	const fs::path Absolute = fs::absolute(ProjectFile, Ec).lexically_normal();
	FProjectDescriptor Descriptor;
	if (Ec || !Descriptor.Load(Absolute, OutError)) return false;

	const fs::path Dir = Absolute.parent_path();
	if (!SetCurrentDirectoryW(Dir.c_str()))
	{
		if (OutError) *OutError = "Cannot enter project folder: " + ToUtf8(Dir);
		return false;
	}

	GProjectFile = Absolute;
	GProjectDir = Dir;
	GProjectName = Descriptor.Name.empty() ? ToUtf8(Absolute.stem()) : Descriptor.Name;
	return true;
}

fs::path FPaths::EngineFile(const fs::path& Relative)
{
	if (Relative.is_absolute()) return Relative;
	return (GEngineDir / Relative).lexically_normal();
}

fs::path FPaths::ResolveEngineRelative(const FString& Path)
{
	if (StartsWithFolder(Path, "Resources") || StartsWithFolder(Path, "Intermediate"))
		return EngineFile(FromUtf8(Path));
	return FromUtf8(Path);
}

FString FPaths::EngineFileUtf8(const fs::path& Relative)
{
	return ToUtf8(EngineFile(Relative));
}

fs::path FPaths::EngineContentDir()
{
	return GEngineDir / L"EngineContent";
}

bool FPaths::IsEngineContentPath(const FString& Path)
{
	return StartsWithFolder(Path, "Engine");
}

FString FPaths::ResolveContentPath(const FString& Path)
{
	if (!IsEngineContentPath(Path)) return Path;
	const size_t PrefixLength = std::strlen(EngineContentPrefix);
	return ToUtf8(EngineContentDir() / FromUtf8(Path.substr(PrefixLength)));
}

fs::path FPaths::EngineSavedDir()
{
	return GEngineDir / L"Saved";
}

FString FPaths::ToUtf8(const fs::path& Path)
{
	const std::u8string Utf8 = Path.generic_u8string();
	return FString(Utf8.begin(), Utf8.end());
}

fs::path FPaths::FromUtf8(const FString& Utf8)
{
	return fs::path(std::u8string(Utf8.begin(), Utf8.end()));
}

fs::path FPaths::FindProjectFileIn(const fs::path& Dir)
{
	std::error_code Ec;
	for (const fs::directory_entry& Entry : fs::directory_iterator(Dir, Ec))
	{
		if (Entry.is_regular_file(Ec) && _wcsicmp(Entry.path().extension().c_str(), ProjectExtension) == 0)
			return Entry.path();
	}
	return {};
}

bool FProjectDescriptor::Load(const fs::path& File, FString* OutError)
{
	std::ifstream In(File);
	if (!In)
	{
		if (OutError) *OutError = "Cannot open project file: " + FPaths::ToUtf8(File);
		return false;
	}

	const json Root = json::parse(In, nullptr, false);
	if (Root.is_discarded() || !Root.is_object())
	{
		if (OutError) *OutError = "Project file is not valid JSON: " + FPaths::ToUtf8(File);
		return false;
	}

	FileVersion = Root.value("FileVersion", CurrentFileVersion);
	Name = Root.value("Name", FString());
	Description = Root.value("Description", FString());
	return true;
}

bool FProjectDescriptor::Save(const fs::path& File) const
{
	json Root;
	Root["FileVersion"] = FileVersion;
	Root["Name"] = Name;
	Root["Description"] = Description;

	std::ofstream Out(File);
	if (!Out) return false;
	Out << Root.dump(4) << '\n';
	return static_cast<bool>(Out);
}

#include "EnginePCH.h"
#include "FbxPath.h"
#include <cwchar>
#include <algorithm>
#include <cctype>
#include <fstream>

FString FbxPath::ToString(const fs::path& Path)
{
	const auto Value = Path.generic_u8string();
	return FString(Value.begin(), Value.end());
}

bool FbxPath::GetAssetKey(const FString& Input, FString& Key, FString& Error, bool bMustExist)
{
	std::error_code Ec;
	const fs::path Root = fs::canonical("Assets", Ec);
	if (Ec) { Error = "Assets directory is not available"; return false; }
	const fs::path Path = bMustExist ? fs::canonical(FbxPath::FromString(Input), Ec) : fs::weakly_canonical(FbxPath::FromString(Input), Ec);
	if (Ec || Path.empty() || (bMustExist && !fs::is_regular_file(Path, Ec)))
	{ Error = "Asset file does not exist: " + Input; return false; }
	auto R = Root.begin(); auto P = Path.begin();
	for (; R != Root.end(); ++R, ++P)
	{
		if (P == Path.end() || _wcsicmp(R->c_str(), P->c_str()) != 0)
		{ Error = "Asset must be inside Assets: " + Input; return false; }
	}
	if (P == Path.end()) { Error = "Expected a file inside Assets"; return false; }
	fs::path Relative;
	for (; P != Path.end(); ++P) Relative /= *P;
	Key = ToString(Relative);
	return true;
}

bool FbxPath::ResolveKey(const FString& Key, FString& Path, FString& Error)
{
	const fs::path Relative = FbxPath::FromString(Key);
	if (Relative.is_absolute() || Relative.has_root_name() || Key.empty())
	{ Error = "Invalid FBX asset key: " + Key; return false; }
	for (const auto& Part : Relative) if (Part == "..") { Error = "Invalid FBX asset key: " + Key; return false; }
	Path = ToString(fs::path("Assets") / Relative);
	FString Verified;
	return GetAssetKey(Path, Verified, Error, false);
}

bool FbxPath::GetSourcePath(const FString& Input, fs::path& Path, FString& Identity, FString& Error)
{
	std::error_code Ec;
	Path = fs::canonical(FromString(Input), Ec);
	if (Ec || !fs::is_regular_file(Path, Ec))
	{ Error = "FBX source file does not exist: " + Input; return false; }
	FString Extension = ToString(Path.extension());
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
	if (Extension != ".fbx") { Error = "Select an FBX file"; return false; }
	// 출처 식별자: Assets 내부는 상대 경로, 외부는 정규화한 절대 경로다.
	FString Ignored;
	if (!GetAssetKey(ToString(Path), Identity, Ignored)) Identity = ToString(Path);
	return true;
}

FString FbxPath::DefaultOutputDirectory(const FString& Source)
{
	const fs::path Root = fs::path("Assets") / "Imported";
	if (Source.empty()) return ToString(Root);
	// Assets 안의 FBX는 바로 옆에 바이너리를 만든다. 밖의 FBX는 Assets로 모은다.
	FString Key, Ignored;
	if (GetAssetKey(Source, Key, Ignored)) return ToString((fs::path("Assets") / FromString(Key)).parent_path());
	return ToString(Root / FromString(FileName(ToString(FromString(Source).stem()))));
}

bool FbxPath::GetOutputDirectory(const FString& Input, fs::path& Path, FString& Error)
{
	std::error_code Ec;
	const fs::path Root = fs::canonical("Assets", Ec);
	if (Ec) { Error = "Assets directory is not available"; return false; }
	if (Input.empty()) { Error = "Select an output folder inside Assets"; return false; }
	Path = fs::weakly_canonical(FromString(Input), Ec);
	if (Ec || Path.empty()) { Error = "Invalid FBX output directory: " + Input; return false; }
	auto P = Path.begin();
	for (auto R = Root.begin(); R != Root.end(); ++R, ++P)
	{
		if (P == Path.end() || _wcsicmp(R->c_str(), P->c_str()) != 0)
		{ Error = "FBX output directory must be inside Assets: " + Input; return false; }
	}
	if (fs::exists(Path, Ec) && !fs::is_directory(Path, Ec))
	{ Error = "FBX output path is not a directory: " + Input; return false; }
	if (Ec) { Error = "Cannot access FBX output directory: " + Input; return false; }
	return true;
}

uint64 FbxPath::StableHash(const FString& Value)
{
	uint64 Hash = 14695981039346656037ull;
	for (unsigned char Ch : Value) { Hash ^= Ch; Hash *= 1099511628211ull; }
	return Hash;
}

uint64 FbxPath::FileHash(const fs::path& Path)
{
	std::ifstream In(Path, std::ios::binary);
	if (!In) return 0;
	uint64 Hash = 14695981039346656037ull;
	char Buffer[65536];
	while (In.read(Buffer, sizeof(Buffer)) || In.gcount() > 0)
	{
		const std::streamsize Count = In.gcount();
		for (std::streamsize I = 0; I < Count; ++I) { Hash ^= static_cast<unsigned char>(Buffer[I]); Hash *= 1099511628211ull; }
		if (Count < static_cast<std::streamsize>(sizeof(Buffer))) break;
	}
	return Hash;
}

fs::path FbxPath::SourceFile(const FString& Identity)
{
	const fs::path Path = FromString(Identity);
	return Path.is_absolute() ? Path : fs::path("Assets") / Path;
}

bool FbxPath::ResolveKey(const FString& Key, fs::path& Path, FString& Error)
{
	FString Value;
	if (!ResolveKey(Key, Value, Error)) return false;
	Path = FbxPath::FromString(Value);
	return true;
}

FString FbxPath::FileName(const FString& Input)
{
	FString Name = Input;
	for (char& C : Name)
		if (static_cast<unsigned char>(C) < 32 || FString("<>:\"/\\|?*").find(C) != FString::npos) C = '_';
	while (!Name.empty() && (Name.back() == ' ' || Name.back() == '.')) Name.pop_back();
	if (Name.size() > 100)
	{
		size_t End = 100;
		while (End > 0 && (static_cast<unsigned char>(Name[End]) & 0xc0) == 0x80) --End;
		Name.resize(End);
	}
	return Name.empty() ? FString("Unnamed") : Name;
}

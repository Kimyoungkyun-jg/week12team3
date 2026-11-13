#pragma once

class UWorld;
class AActor;
class UObject;

class FJsonArchive
{
public:
	static bool SaveObject(UObject* Object, const FString& Path);
	static bool LoadObject(UObject* Object, const FString& Path);
	static bool SaveWorld(UWorld* World, const FString& Path);
	static bool LoadWorld(UWorld* World, const FString& Path);
	static bool LoadWorldFromBytes(UWorld* World, const TArray<uint8>& Bytes);
	static bool SaveWorldToBytes(UWorld* World, TArray<uint8>& OutBytes);

	// 고른 액터만 씬 형식으로 쓴다 (에디터 복사, TEAM1). Transient 액터는 뺀다. 쓸 액터가 없으면 false
	static bool SaveActorsToBytes(const TArray<AActor*>& Actors, TArray<uint8>& OutBytes);
	// SaveActorsToBytes 결과로 World에 새 액터를 만든다 (에디터 붙여넣기). 기존 액터는 건드리지 않는다.
	static bool SpawnActorsFromBytes(UWorld* World, const TArray<uint8>& Bytes, TArray<AActor*>& OutActors);
};

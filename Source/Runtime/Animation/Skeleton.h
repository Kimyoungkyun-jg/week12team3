#pragma once
#include "Asset/RenderAsset.h"
#include "Animation/ReferenceSkeleton.h"
#include "Math/Transform.h"
#include "Skeleton.generated.h"

struct FSkeletalMeshSocket
{
	FString SocketName;
	FString BoneName;
	FVector RelativeLocation = FVector(0.0f, 0.0f, 0.0f);
	FRotator RelativeRotation = FRotator(0.0f, 0.0f, 0.0f);
	FVector RelativeScale = FVector(1.0f, 1.0f, 1.0f);

	FMatrix GetSocketLocalMatrix() const;
};

UCLASS()
class USkeleton : public URenderAsset
{
	GENERATED_BODY()
public:
	FReferenceSkeleton RefSkeleton;
	TArray<FSkeletalMeshSocket> Sockets;

	int32 FindSocketIndex(const FString& SocketName) const;
	const FSkeletalMeshSocket* FindSocket(const FString& SocketName) const;
	FSkeletalMeshSocket* FindMutableSocket(const FString& SocketName);
	bool HasSocket(const FString& SocketName) const;
	bool AddSocket(const FSkeletalMeshSocket& NewSocket);
	bool RemoveSocket(const FString& SocketName);
};

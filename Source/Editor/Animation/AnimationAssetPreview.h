#pragma once

#include "Editor/Viewports/EditorViewportClient.h"
#include "Asset/AnimTransform.h"

class UWorld;
class USkeletalMeshComponent;
class UEditorEngine;

class FAnimationAssetPreview
{
public:
	~FAnimationAssetPreview() { Release(); }
	bool Start(const FString& SkeletonPath, FString& Error);
	void ApplyPose(const TArray<FAnimBoneTransform>& Pose);
	void Tick();
	void Render(UEditorEngine* Engine);
	bool Draw();
	void Release();
	USkeletalMeshComponent* GetMeshComponent() const { return MeshComponent; }

private:
	UWorld* World = nullptr;
	USkeletalMeshComponent* MeshComponent = nullptr;
	FEditorViewportClient Client;
};

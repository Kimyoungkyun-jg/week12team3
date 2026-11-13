#include "EnginePCH.h"
#include "Editor/Animation/AnimationAssetPreview.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "Components/SkyLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/BillboardComponent.h"
#include "Render/SkeletalMesh.h"
#include "Render/Texture2D.h"
#include <algorithm>

void FAnimationAssetPreview::Release()
{
	Client.SetWorld(nullptr);
	MeshComponent = nullptr;
	if (World)
	{
		World->ClearWorld();
		World->FlushPendingDestruction();
		if (GEngine) GEngine->DestroyWorld(World);
		World = nullptr;
	}
}

bool FAnimationAssetPreview::Start(const FString& SkeletonPath, FString& Error)
{
	Release();
	USkeleton* Skeleton = UAssetManager::LoadSkeleton(SkeletonPath);
	if (!Skeleton) { Error = "Select a valid skeleton."; return false; }
	TArray<USkeletalMesh*> Meshes;
	UAssetManager::GetSkeletalMeshesForSkeleton(Skeleton, Meshes);
	if (Meshes.IsEmpty())
	{
		fs::path SkeletonFile;
		FString ResolveError;
		if (FbxPath::ResolveKey(SkeletonPath, SkeletonFile, ResolveError))
		{
			std::error_code Ec;
			for (fs::directory_iterator It(SkeletonFile.parent_path(), Ec), End; !Ec && It != End; It.increment(Ec))
			{
				if (!It->is_regular_file() || It->path().extension() != ".fbxbin" ||
					!It->path().filename().string().starts_with("SK_")) continue;
				UAssetManager::LoadFbxSkeletalMesh(FbxPath::ToString(It->path()));
				UAssetManager::GetSkeletalMeshesForSkeleton(Skeleton, Meshes);
				if (!Meshes.IsEmpty()) break;
			}
		}
	}
	if (Meshes.IsEmpty()) { Error = "No skeletal mesh matches this skeleton."; return false; }
	World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!World) { Error = "Cannot create preview world."; return false; }
	AActor* Actor = World->SpawnActor<AActor>();
	if (!Actor) { Error = "Cannot create preview actor."; Release(); return false; }
	MeshComponent = Cast<USkeletalMeshComponent>(Actor->AddComponent(USkeletalMeshComponent::StaticClass(), "AnimationAssetPreview"));
	if (!MeshComponent) { Error = "Cannot create preview mesh."; Release(); return false; }
	Actor->SetRootComponent(MeshComponent);
	MeshComponent->SetSkeletalMesh(Meshes[0]);
	if (auto* Sky = World->SpawnActor<ASkyLightActor>())
	{ Sky->GetBillboardComponent()->SetVisibility(false); Sky->GetLightComponent()->SetIntensity(0.35f); }
	if (auto* Sun = World->SpawnActor<ADirectionalLightActor>())
	{
		Sun->GetBillboardComponent()->SetVisibility(false);
		Sun->GetArrowComponent()->SetVisibility(false);
		Sun->GetLightComponent()->SetRelativeRotation(FRotator(40.0f, 160.0f, 0.0f));
		Sun->GetLightComponent()->SetIntensity(2.0f);
	}
	Client.SetWorld(World);
	Client.SetViewportType(ELevelViewportType::Perspective);
	Client.Resize(800, 600);
	const FBox Bounds = MeshComponent->CalcBounds();
	const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
	const float Radius = std::max(0.5f, (Bounds.Max - Bounds.Min).Size() * 0.5f);
	Client.SetViewRotation(FRotator(10.0f, 165.0f, 0.0f));
	Client.SetViewLocation(Center - Client.GetViewForward() * (Radius * 2.5f));
	Error.clear();
	return true;
}

void FAnimationAssetPreview::ApplyPose(const TArray<FAnimBoneTransform>& Pose)
{
	if (!MeshComponent || Pose.Num() != MeshComponent->GetGlobalBoneTransforms().Num()) return;
	for (int32 Index = 0; Index < Pose.Num(); ++Index)
		MeshComponent->SetBoneTransform(Index, MakeAnimMatrix(Pose[Index].Position, Pose[Index].Rotation, Pose[Index].Scale));
}

void FAnimationAssetPreview::Tick()
{
	if (!World || !MeshComponent) return;
	World->Tick(0.0f);
	MeshComponent->TickComponent(0.0f);
}

void FAnimationAssetPreview::Render(UEditorEngine* Engine)
{
	if (Engine && World) Engine->RenderPreviewViewport(&Client, World, nullptr, nullptr, true);
}

bool FAnimationAssetPreview::Draw()
{
	if (!World || !Client.GetColorTarget())
	{ ImGui::TextWrapped("Select a skeleton with a compatible skeletal mesh to preview."); return false; }
	const ImVec2 Size = ImGui::GetContentRegionAvail();
	Client.Resize(static_cast<uint32>(std::max(1.0f, Size.x)), static_cast<uint32>(std::max(1.0f, Size.y)));
	ImGui::Image(Client.GetColorTarget()->GetSRV(), Size);
	Client.TickInput(ImGui::GetIO().DeltaTime, ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Right),
		ImGui::IsItemHovered(), static_cast<int32>(ImGui::GetIO().MouseWheel * 120), 4, 1);
	return true;
}

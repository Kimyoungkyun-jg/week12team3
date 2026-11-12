#include "EnginePCH.h"
#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"
#include "Editor/Viewports/EditorViewportClient.h"   
#include "Editor/HitoriEd/EditorEngine.h"
#include "Launch/LaunchEngineLoop.h"            
#include "Components/SkeletalMeshComponent.h"        
#include "Render/SkeletalMesh.h"                     
#include "Render/LineBatcher.h"                      
#include "Math/BoxSphereBounds.h"                    
#include "Collision/Ray.h"                           


namespace
{
	// 원근 나눗셈 전에 선분을 깊이 범위(0 <= Z <= W)로 잘라냅니다.
	bool ClipBoneSegmentToView(FVector4& Start, FVector4& End)
	{
		const auto ClipPlane = [&](float StartDistance, float EndDistance)
			{
				if (StartDistance < 0.0f && EndDistance < 0.0f)
				{
					return false;
				}

				if (StartDistance < 0.0f || EndDistance < 0.0f)
				{
					const float Alpha = StartDistance / (StartDistance - EndDistance);
					const FVector4 Intersection = Start + (End - Start) * Alpha;
					(StartDistance < 0.0f ? Start : End) = Intersection;
				}

				return true;
			};

		return ClipPlane(Start.Z, End.Z) &&
			ClipPlane(Start.W - Start.Z, End.W - End.Z) &&
			Start.W >= 0.0001f && End.W >= 0.0001f;
	}

	FString GetBoneDisplayName(const FString& BoneName)
	{
		const auto Separator = BoneName.find_last_of(':');

		if (Separator == FString::npos)
		{
			return BoneName;
		}

		return BoneName.substr(Separator + 1);
	}
}

void FSkeletalPreviewEditor::Tick(float DeltaTime)
{
	if (!ViewportClient || !ViewportClient->GetWorld()) return;
	// 뷰포트가 보이면 카메라 이동을 적용
	if (bViewportVisible && !IsViewportInteractionBusy() && !PointerInput.bMouseDown)
	{
		FViewportCameraState Camera{
			.Location = ViewportClient->GetViewLocation(),
			.Rotation = ViewportClient->GetViewRotation(),
			.OrthoWidth = ViewportClient->GetOrthoWidth(),
			.ViewWidth = static_cast<float>(ViewportWidth),
			.bPerspective = true
		};
		ApplyViewportCameraMovement(Camera, CameraInput, DeltaTime, CameraSpeed, 0.2f);
		ViewportClient->SetViewLocation(Camera.Location);
		ViewportClient->SetViewRotation(Camera.Rotation);
	}

	TickBeforeWorld(DeltaTime);              // Animation 에디터: 재생 시간 진행 후 포즈 적용
	FAssetEditor::Tick(DeltaTime);           // 프리뷰 월드 틱 (스킨 컴포넌트도 여기서 틱)
	TickAfterWorld(DeltaTime);               // 기본: 피킹 / 메시 에디터: 기즈모, 피킹, SyncBoneGizmo 순

	CameraInput = {};
	PointerInput = {};
}

void FSkeletalPreviewEditor::SetWeightHeatmap(bool bEnable)
{
	bShowWeightHeatmap = bEnable;
	RefreshHeatmap();
}

void FSkeletalPreviewEditor::RefreshHeatmap()
{
	if (MeshComponent)
	{
		MeshComponent->SetDebugWeightBone(bShowWeightHeatmap ? SelectedBoneIndex : -1);
	}
}

// 기존 SelectBone에 RefreshHeatmap() 한 줄을 더한다
void FSkeletalPreviewEditor::SelectBone(int32 BoneIndex)
{
	if (BoneIndex < -1 || BoneIndex >= BoneItems.Num()) return;
	SelectedBoneIndex = BoneIndex;
	RefreshHeatmap();
	OnBoneSelected();
}

void FSkeletalPreviewEditor::SetViewportState(uint32 Width, uint32 Height, const FViewportCameraInput& Input,
	const FViewportPointerInput& InPointer)
{
	ViewportWidth = Width;
	ViewportHeight = Height;
	bViewportVisible = Width > 0 && Height > 0;
	CameraInput = Input;
	PointerInput = InPointer;
}

void FSkeletalPreviewEditor::FocusMesh()
{
	CancelViewportInteraction();

	if (!ViewportClient)
	{
		return;
	}

	if (!Mesh)
	{
		return;
	}

	// 메시의 AABB를 가져와서 BoxSphereBounds로 변환
	const FBox& Box = Mesh->GetRenderData().LODRenderData[0].BindAABB;
	const FVector Extent = (Box.Max - Box.Min) * 0.5f;
	FBoxSphereBounds Bound
	{
		.Origin = (Box.Min + Box.Max) * 0.5f,
		.BoxExtent = Extent,
		.SphereRadius = Extent.Size()
	};

	// AABB로 ViewportClient의 카메라를 메시에 포커싱
	ViewportClient->FocusOnBounds(Bound);
}

bool FSkeletalPreviewEditor::InitializePreview()
{
	if (!Mesh) return false;
	const auto& LODs = Mesh->GetRenderData().LODRenderData;
	if (LODs.IsEmpty() || LODs[0].BindVertices.IsEmpty() || LODs[0].Indices.IsEmpty()) return false;

	if (!FAssetEditor::InitializeResources()) return false;      // 프리뷰 씬·뷰포트 클라이언트

	MeshComponent = Cast<USkeletalMeshComponent>(PreviewScene.AddComponent(USkeletalMeshComponent::StaticClass()));
	if (!MeshComponent) return false;
	MeshComponent->SetSkeletalMesh(Mesh);
	MeshComponent->RefreshBoneTransformsNow();

	const FBox& MeshBox = LODs[0].BindAABB;                       // 멤버였던 MeshBox
	BoneSphereSize = FMath::Max((MeshBox.Max - MeshBox.Min).Size() * 0.0025f, 0.001f);

	BuildBoneItems();
	FocusMesh();
	return true;
}

void FSkeletalPreviewEditor::ShutdownResources()
{
	SelectedBoneIndex = -1;
	MeshComponent = nullptr;
	Mesh = nullptr;
	FAssetEditor::ShutdownResources();
}


bool FSkeletalPreviewEditor::CanProcessViewportInput() const
{
	if (// 뷰포트 클라이언트가 없으면 입력을 무시
		!ViewportClient ||
		// 뷰포트가 보이지 않으면 입력을 무시
		!bViewportVisible ||
		// 뷰포트가 포커싱되지 않으면 입력을 무시
		!PointerInput.bFocused ||
		// 뷰포트 너비가 변경되면 입력을 무시
		ViewportClient->GetWidth() != ViewportWidth ||
		// 뷰포트 높이가 변경되면 입력을 무시
		ViewportClient->GetHeight() != ViewportHeight)
	{
		return false;
	}

	return true;
}

void FSkeletalPreviewEditor::OnRenderPreview()
{
	// Viewport가 보이지 않으면 렌더링하지 않음
	const bool bRender = bViewportVisible;
	bViewportVisible = false;
	if (!bRender)
	{
		return;
	}

	if (!ViewportClient)
	{
		return;
	}

	if (!ViewportClient->GetWorld())
	{
		return;
	}

	// ViewportClient 크기 변경
	ViewportClient->Resize(ViewportWidth, ViewportHeight);

	// ViewportClient의 RTV가 없다면 렌더링하지 않음
	if (!ViewportClient->GetViewTargets().IsValidFor(ViewportWidth, ViewportHeight))
	{
		return;
	}

	// FocusMesh가 아직 호출되지 않았다면 호출
	if (bInitialFocusPending)
	{
		FocusMesh();
		bInitialFocusPending = false;
	}

	// 뷰포트 클라이언트에게 그리게 한다.
	ViewportClient->Draw(EditorEngine.GetEngineLoop().GetRenderer());
}

void FSkeletalPreviewEditor::UpdateBonePicking()
{
	if (!CanProcessViewportInput() || // 공통 뷰포트 입력 조건을 만족하지 않으면 입력을 무시
		!Mesh || // 스켈레탈 메시가 없으면 입력을 무시
		!MeshComponent || // 프리뷰 메시 컴포넌트가 없으면 입력을 무시
		!bShowBoneVisualizer || // 본 시각화가 꺼져 있으면 입력을 무시
		!PointerInput.bHovered || // 마우스가 뷰포트 위에 없으면 입력을 무시
		!PointerInput.bMousePressed || // 좌클릭이 시작된 프레임이 아니면 입력을 무시
		PointerInput.bCancel || // 취소 입력이 들어오면 입력을 무시
		CameraInput.bCaptured || // 카메라를 조작 중이면 입력을 무시
		IsViewportInteractionBusy() || // 다른 뷰포트 조작(메시 에디터의 기즈모 드래그 등) 중이면 입력을 무시
		ViewportWidth == 0 || // 뷰포트 너비가 0이면 입력을 무시
		ViewportHeight == 0) // 뷰포트 높이가 0이면 입력을 무시
	{
		return;
	}

	const FVector2 MousePosition(PointerInput.MouseUV.X * ViewportWidth,
		PointerInput.MouseUV.Y * ViewportHeight);
	SelectBone(PickBone(MousePosition));
}

int32 FSkeletalPreviewEditor::PickBone(const FVector2& MousePosition) const
{
	constexpr float HitPixels = 12.0f;
	const FMatrix ViewProjection = ViewportClient->GetViewProjectionMatrix();
	const FMatrix InverseViewProjection = ViewProjection.Inverse();
	const FVector CameraPosition = ViewportClient->GetViewLocation();
	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();

	TArray<FVector4> ClipPositions;
	ClipPositions.SetNum(Bones.Num());
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		const FVector Position = MeshComponent->GetBoneWorldMatrix(Index)
			.TransformPosition(FVector::ZeroVector);
		ClipPositions[Index] = FVector4(Position, 1.0f) * ViewProjection;
	}

	const auto ToScreen = [&](const FVector4& Clip)
		{
			return FVector2((Clip.X / Clip.W * 0.5f + 0.5f) * ViewportWidth,
				(0.5f - Clip.Y / Clip.W * 0.5f) * ViewportHeight);
		};

	int32 BestBoneIndex = -1;
	float BestCameraDistanceSquared = FLT_MAX;
	const auto ConsiderCandidate = [&](int32 Index, float ScreenDistance, const FVector4& ClipPosition)
		{
			if (ScreenDistance > HitPixels)
			{
				return;
			}

			const FVector4 World = ClipPosition * InverseViewProjection;
			const FVector Position(World.X / World.W, World.Y / World.W, World.Z / World.W);
			const float CameraDistanceSquared = (Position - CameraPosition).SizeSquared();
			if (CameraDistanceSquared < BestCameraDistanceSquared)
			{
				BestBoneIndex = Index;
				BestCameraDistanceSquared = CameraDistanceSquared;
			}
		};

	// Joint 후보 중 카메라에 가장 가까운 Joint를 선택합니다.
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		const FVector4& Clip = ClipPositions[Index];
		if (Clip.W < 0.0001f || Clip.Z < 0.0f || Clip.Z > Clip.W)
		{
			continue;
		}

		const FVector2 Delta = MousePosition - ToScreen(Clip);
		ConsiderCandidate(Index, sqrtf(Delta.X * Delta.X + Delta.Y * Delta.Y), Clip);
	}

	if (BestBoneIndex >= 0)
	{
		return BestBoneIndex;
	}

	// Joint 후보가 없을 때만 Bone을 검사합니다. 연결부는 부모 본을 선택합니다.
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		const int32 ParentIndex = Bones[Index].ParentIndex;
		if (ParentIndex < 0 || ParentIndex >= Bones.Num())
		{
			continue;
		}

		FVector4 Start = ClipPositions[ParentIndex];
		FVector4 End = ClipPositions[Index];
		if (!ClipBoneSegmentToView(Start, End))
		{
			continue;
		}

		const FVector2 ScreenStart = ToScreen(Start);
		const FVector2 ScreenEnd = ToScreen(End);
		const FVector2 Segment = ScreenEnd - ScreenStart;
		const FVector2 ToMouse = MousePosition - ScreenStart;
		const float LengthSquared = Segment.X * Segment.X + Segment.Y * Segment.Y;
		const float Alpha = LengthSquared > 1e-6f
			? FMath::Clamp((ToMouse.X * Segment.X + ToMouse.Y * Segment.Y) / LengthSquared, 0.0f, 1.0f)
			: 0.0f;

		// 화면에서 마우스에 가장 가까운 점을 보간하고 월드 공간의 카메라 거리로 비교합니다.
		const FVector4 ClipPosition = (Start / Start.W) * (1.0f - Alpha) + (End / End.W) * Alpha;
		ConsiderCandidate(ParentIndex, DistanceToSegment(MousePosition, ScreenStart, ScreenEnd), ClipPosition);
	}

	return BestBoneIndex;
}

void FSkeletalPreviewEditor::OnRenderLine(FLineBatcher& LineBatcher)
{
	if (!bShowBoneVisualizer)
	{
		return;
	}

	if (!Mesh)
	{
		return;
	}

	if (!MeshComponent)
	{
		return;
	}

	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();

	TArray<FVector> Positions;
	Positions.SetNum(Bones.Num());

	// 각 Bone의 원점을 월드 공간으로 변환하여 Positions에 저장
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		const FVector4 Position = FVector4(0.0f, 0.0f, 0.0f, 1.0f) * MeshComponent->GetBoneWorldMatrix(Index);
		Positions[Index] = FVector(Position.X, Position.Y, Position.Z);
	}

	// Bone 시각화 그리기
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		const FVector4 Color = Index == SelectedBoneIndex
			? FVector4(1.0f, 1.0f, 0.0f, 1.0f)
			: BoneVisualizerColor;

		// Joint 먼저 그리기
		LineBatcher.AddSphere(Positions[Index], BoneSphereSize, Color);

		const int32 ParentIndex = Bones[Index].ParentIndex;

		// 부모가 있다면, 부모에서 자식으로 이어지는 Bone을 그리기
		if (ParentIndex >= 0 && ParentIndex < Bones.Num())
		{
			FVector4 BoneColor = BoneVisualizerColor;
			if (Index == SelectedBoneIndex)
			{
				// 현재 본이 선택한 본이면 부모 본을 노란색으로 표시
				BoneColor = FVector4(1.0f, 0.5f, 0.0f, 1.0f);
			}
			else if (ParentIndex == SelectedBoneIndex)
			{
				// 부모 Index가 선택된 본이면 부모 본을 주황색으로 표시
				BoneColor = FVector4(1.0f, 1.0f, 0.0f, 1.0f);
			}

			// ParentIndex -> Index Direction
			const FVector Direction = (Positions[Index] - Positions[ParentIndex]).Normalized();

			const FVector Start = Positions[ParentIndex] + Direction * BoneSphereSize;
			const FVector End = Positions[Index] - Direction * BoneSphereSize;

			const float Length = (Positions[Index] - Positions[ParentIndex]).Size();
			const float HalfWidth = FMath::Min(BoneSphereSize * 1.5f, Length * 0.1f);
			LineBatcher.AddPyramid(Start, End, HalfWidth, BoneColor);
		}
	}
}

void FSkeletalPreviewEditor::BuildBoneItems()
{
	if (!Mesh)
	{
		return;
	}

	const TArray<FBoneInfo>& BoneData = Mesh->GetRefSkeleton();
	BoneItems.Empty();
	RootBoneIndices.Empty();
	SelectBone(-1);

	for (int32 BoneIndex = 0; BoneIndex < BoneData.Num(); ++BoneIndex)
	{
		const FBoneInfo& Bone = BoneData[BoneIndex];

		BoneItems.Add(FEditorBoneItem{
			.BoneIndex = BoneIndex,
			.Name = GetBoneDisplayName(Bone.Name),
			.ChildBoneIndices = {},
			.InverseBindPose = Bone.InverseBindPose
			});

		if (Bone.ParentIndex == -1)
		{
			RootBoneIndices.Add(BoneIndex);
		}
		else if (Bone.ParentIndex >= 0)
		{
			BoneItems[Bone.ParentIndex].ChildBoneIndices.Add(BoneIndex);
		}
	}
}
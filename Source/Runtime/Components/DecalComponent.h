#pragma once

#include "Components/SceneComponent.h"
#include "Render/Decal/ProjectedDecal.h"
#include "Render/Material.h"

#include "DecalComponent.generated.h"

class FLineBatcher;
void DrawDecalVolume(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected);

UCLASS()
class UDecalComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDecalComponent();
	~UDecalComponent() override = default;

	bool IsDecalEnabled() const
	{
		return bDecalEnabled;
	}

	void SetDecalEnabled(bool bInEnabled)
	{
		bDecalEnabled = bInEnabled;
	}

	//void SetTintOpacity(const FVector4& InTintOpacity)
	//{
	//	TintOpacity = InTintOpacity;
	//}

	int32 GetSortOrder() const
	{
		return SortOrder;
	}

	void SetSortOrder(int32 InSortOrder)
	{
		SortOrder = InSortOrder;
	}

	UMaterialInterface* GetDecalMaterial() const
	{
		return DecalMaterial;
	}

	void SetDecalMaterial(UMaterialInterface* InMaterial)
	{
		DecalMaterial = InMaterial;
	}

	FBox CalcLocalBounds() const override
	{
		return FBox{
			FVector(-0.5f, -0.5f, -0.5f),
			FVector(0.5f, 0.5f, 0.5f)
		};
	}

	void SetUnlit(bool bInUnlit)
	{
		bUnlit = bInUnlit;
	}

	bool IsUnlit() const
	{
		return bUnlit;
	}

	// 현재 컴포넌트 값으로 렌더 데이터를 생성한다.
	// 표시 여부와 World 일치는 수집하는 쪽에서 확인한다.
	bool TryGetRenderData(
		FProjectedDecalRenderData& OutRenderData) const;

	using Super::Serialize;
	void Serialize(FStructuredArchive::FRecord Record) override;

private:
	UPROPERTY(EditAnywhere)
	bool bDecalEnabled = true;

	//UPROPERTY(EditAnywhere, Type=LinearColor)
	//FVector4 TintOpacity =
	//	FVector4(1.0f, 1.0f, 1.0f, 0.8f);

	UPROPERTY(EditAnywhere)
	int32 SortOrder = 0;

	UMaterialInterface* DecalMaterial = nullptr;

	UPROPERTY(EditAnywhere)
	bool bUnlit = false;

	UPROPERTY(EditAnywhere)
	float NormalStrength = 1.0f;

	UPROPERTY(EditAnywhere)
	float NormalOpacity = 1.0f;
};


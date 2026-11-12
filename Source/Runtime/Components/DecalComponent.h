#pragma once

#include "SceneComponent.h"
#include "Render/Texture2D.h"
#include "Math/Frustum.h"
#include "DecalComponent.generated.h"

UCLASS()
class UDecalComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	void TickComponent(float DeltaTime) override;
	void UpdateInEditor() { Fade = 1.0f; }

	UDecalComponent() { PrimaryComponentTick.bCanEverTick = true; }
	
	UTexture2D* GetDecalTexture() const { return DecalTexture; }
	void SetDecalTexture(UTexture2D* InTexture) { DecalTexture = InTexture; }
	
	FVector GetExtent() const { return Extent; }
	void SetExtent(FVector InExtent) { Extent = InExtent; }

	int32 GetSortOrder() const { return SortOrder; }
	void SetSortOrder(int32 InSortOrder) { SortOrder = InSortOrder; }

	void SetDecalViewProj();
	FMatrix GetDecalVP() { return DecalViewProj; }

	float GetFade() const { return Fade; }

	FAABB GetWorldAABB() const;
	FOBB GetWorldOBB() const;

	void GetWorldCorners(FVector OutCorners[8]) const;
	void DrawDebug(class FLineBatcher* LineBatcher) const;

	bool IsAdditive() const { return bAdditive; }

	void SetPerspective(bool bInPerspective) { bPerspective = bInPerspective; }
	void SetAdditive(bool bInAdditive) { bAdditive = bInAdditive; }
	void SetFadeTimes(float InDelay, float InFadeIn, float InVisible, float InFadeOut)
	{
		Delay = InDelay; FadeIn = InFadeIn; Visible = InVisible; FadeOut = InFadeOut;
	}

protected:
	virtual bool ShouldCreateRenderState() const override
	{
		return true;
	}

	virtual void CreateRenderState() override;
	virtual void DestroyRenderState() override;

private:
	float Ease(float t) const;


private:
	UPROPERTY(EditAnywhere) UTexture2D* DecalTexture = nullptr;
	UPROPERTY(EditAnywhere) FVector Extent = FVector(50, 50, 50);
	UPROPERTY(EditAnywhere) int32 SortOrder = 0;

	UPROPERTY(EditAnywhere) bool bPerspective = false;
	UPROPERTY(EditAnywhere) bool bAdditive = false;

	FMatrix DecalViewProj;

	float Fade = 0.0f;
	UPROPERTY(EditAnywhere) float Delay = 1.0f;
	UPROPERTY(EditAnywhere) float FadeIn = 1.0f;
	UPROPERTY(EditAnywhere) float Visible = 1.0f;
	UPROPERTY(EditAnywhere) float FadeOut = 1.0f;
	float CurrentTime = 0.0f;

};
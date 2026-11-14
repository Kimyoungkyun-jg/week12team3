#pragma once

#include "BillboardComponent.h"
#include "Particles/ParticleHelper.h"
#include "ParticleSystemComponent.generated.h"


UCLASS()
class UParticleSystemComponent : public UBillboardComponent
{
	GENERATED_BODY()

public:
	UParticleSystemComponent();
	virtual ~UParticleSystemComponent() override;
	
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;
	virtual FBox CalcLocalBounds() const override;

	void InitParticle(FBaseParticle& Particle);

	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext) override;
	
public:
	UPROPERTY(EditAnywhere)
	int32 ParticleCount = 20;

	UPROPERTY(EditAnywhere)
	float MinLifetime = 2.0f;
	UPROPERTY(EditAnywhere)
	float MaxLifetime = 4.0f;

	UPROPERTY(EditAnywhere)
	float MinSize = 0.5f;
	UPROPERTY(EditAnywhere)
	float MaxSize = 1.5f;

	UPROPERTY(EditAnywhere)
	float MinRiseSpeed = 1.0f;
	UPROPERTY(EditAnywhere)
	float MaxRiseSpeed = 2.0f;

	UPROPERTY(EditAnywhere)
	float StartScale = 1.0f;
	UPROPERTY(EditAnywhere)
	float EndScale = 2.0f;

	UPROPERTY(EditAnywhere)
	float SpreadRadius = 0.3f;

	UPROPERTY(EditAnywhere)
	FLinearColor StartColor{ 1.0f, 0.9f, 0.3f, 1.0f };

	UPROPERTY(EditAnywhere)
	FLinearColor EndColor{ 1.0f, 0.1f, 0.0f, 1.0f };

	UPROPERTY(EditAnywhere)
	float FadeInEnd = 0.1f;
	UPROPERTY(EditAnywhere)
	float FadeOutStart = 0.7f;

	UPROPERTY(EditAnywhere)
	int32 AtlasCols = 8;
	UPROPERTY(EditAnywhere)
	int32 AtlasRows = 8;

private:
	TArray<FSubUVConstants> Constants;

	TArray<FBaseParticle> Particles;
};

#pragma once

#include "PrimitiveComponent.h"
#include "BillboardComponent.h"
#include "Particles/ParticleEmitterInstance.h"
#include "ParticleSystemComponent.generated.h"

class UParticleSystem;
class UParticleModuleTypeDataMesh;

UCLASS()
class UParticleSystemComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UParticleSystemComponent();

	void SetTemplate(UParticleSystem* InTemplate);
	void TickComponent(float DeltaTime) override;

	FPrimitiveSceneProxy* CreateSceneProxy() override;
	FBox CalcLocalBounds() const override;

	void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext) override;

	bool ShouldPreviewInEditor() const { return bPreviewInEditor; }
	bool HasTemplate() const { return Template != nullptr; }

private:
	void SubmitSpriteEmitter(
		const FParticleEmitterInstance& Instance,
		FRenderQueue& RenderQueue,
		const FViewContext& ViewContext,
		uint32& ConstantIndex);

	void SubmitMeshEmitter(
		const FParticleEmitterInstance& Instance,
		const UParticleModuleTypeDataMesh& TypeData,
		FRenderQueue& RenderQueue,
		const FViewContext& ViewContext);

private:
	UParticleSystem* Template = nullptr;
	TArray<std::unique_ptr<FParticleEmitterInstance>> EmitterInstances;
	TArray<FSubUVConstants> SpriteConstants;

	UPROPERTY(EditAnywhere)
	bool bPreviewInEditor = false;
};
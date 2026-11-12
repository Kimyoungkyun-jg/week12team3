#pragma once

#include "PrimitiveSceneProxy.h"

class USkinnedMeshComponent;

class FSkeletalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FSkeletalMeshSceneProxy(USkinnedMeshComponent* InComponent);

	void UpdateRenderState() override;
	bool HasDynamicMeshElements() const override { return true; }

	void GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View) override;
private:
	
};
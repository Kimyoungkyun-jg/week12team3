#pragma once

#include "MeshComponent.h"
#include "StaticMeshComponent.generated.h"

UCLASS()
class UStaticMeshComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	UStaticMeshComponent();
	virtual ~UStaticMeshComponent();

	virtual void BeginPlay();
	virtual void TickComponent(float DeltaTime);

	virtual const FStaticMeshData* GetMeshData() const override { return StaticMesh ? &StaticMesh->GetMeshData() : nullptr; }
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	// 메시가 바뀌면 슬롯 구성이 달라지므로 덮어쓰기를 비운다
	void SetStaticMesh(UStaticMesh* InStaticMesh);
	UStaticMesh* GetStaticMesh() const { return StaticMesh; }
	virtual UStaticMesh* GetRenderMesh() const override { return StaticMesh; }

	virtual int32 GetNumMaterials() const override;
	virtual FString GetMaterialSlotName(int32 SlotIndex) const override;
	virtual UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;

	void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext) override;
	void SubmitToRenderQueue(FRenderQueue& Queue, uint32 LODIndex);

	bool IsEditorIcon() const { return bEditorIcon; }
	void SetEditorIcon(bool IsEditorIcon) { bEditorIcon = IsEditorIcon; }

	// 에디터 전용 표시(조명 방향 화살표 등)는 게임 뷰에서 항상 숨긴다. 씬에 저장된 bHiddenInGame 값과 상관없는 규칙이다.
	// 자식 쪽만 영향을 받는다. 부모인 라이트 컴포넌트의 IsShown은 부모 방향으로만 검사하므로 빛 계산에는 영향이 없다.
	virtual bool IsShown(bool bGameView) const override { return !(bGameView && bEditorIcon) && Super::IsShown(bGameView); }

private:
	UPROPERTY(EditAnywhere)
	UStaticMesh* StaticMesh = nullptr;
	// Mesh의 Section별 Material과 Texture를 보존해 각각의 렌더 패킷으로 제출한다.
	//virtual void SubmitToRenderQueue(TQueue<FRenderPacket>& RenderQueue) override;

	// 조명 Icon Debug arrow용
	bool bEditorIcon = false;
};

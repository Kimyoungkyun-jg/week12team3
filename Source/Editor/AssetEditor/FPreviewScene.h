#pragma once

// FAssetEditor 내부에서 어떤 장면을 그려낼지를 관리하는 클래스입니다.
class FPreviewScene
{

public:
	FPreviewScene() = default;
	~FPreviewScene() = default;
	FPreviewScene(const FPreviewScene&) = delete;
	FPreviewScene& operator=(const FPreviewScene&) = delete;

	// PreviewScene을 초기화합니다. 
	bool Initialize(class UEditorEngine* InEditorEngine);

	void Tick(float DeltaTime);
	class UWorld* GetWorld() const;

	// PreviewScene을 정리합니다.
	void Release();

	// PreviewScene에 액터를 추가합니다.
	class AActor* AddActor(class UClass* ActorClass);

	// PreviewScene에 빈 액터를 만들고 컴포넌트를 추가합니다.
	class UActorComponent* AddComponent(class UClass* ComponentClass);

	// 기초 장면을 세팅합니다.
	// Skybox, Directional Light, SkyLight, 바닥 Plane등을 세팅합니다.
	void Setup();

private:

	class UEditorEngine* EditorEngine = nullptr;
	struct FWorldContext* WorldContext = nullptr;
	bool bInitialized = false;
	bool bEnvironmentCreated = false;

};

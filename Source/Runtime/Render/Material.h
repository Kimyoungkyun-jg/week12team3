#pragma once

#include "MaterialInterface.h"
#include "Render/Buffer.h"
#include "Render/ImportedMaterialProperties.h"
#include "RenderResourceManager.h"
#include "Render/RenderStates.h"
#include "Render/Texture2D.h"
#include "Material.generated.h"

class FShader;
class UMaterialInstanceDynamic;

enum class EMaterialUsage : uint32
{
	None = 0,
	StaticMesh = 1u << 0,
	SkeletalMesh = 1u << 1,
	ParticleSprite = 1u << 2,
	ParticleMesh = 1u << 3
};

enum class EMaterialExpressionType : uint8
{
	Constant,
	Constant2Vector,
	Constant3Vector,
	Constant4Vector,
	ScalarParameter,
	VectorParameter,
	TextureSample,
	TextureCoordinate,
	Add,
	Multiply,
	Lerp,
	OneMinus,
	MaterialOutput
};

enum class EMaterialValueType : uint8
{
	Scalar,
	Vector2,
	Vector3,
	Vector4,
	Texture2D,
	Unknown
};

struct FMaterialExpressionNode
{
	int32 Id = 0;
	EMaterialExpressionType Type = EMaterialExpressionType::Constant;
	FVector2 Position = FVector2(0.0f, 0.0f);
	float ScalarValue = 0.0f;
	FVector4 VectorValue = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	FString ParameterName;
	FString ParameterGroup = "Default";
	FString TexturePath;
	int32 CoordinateIndex = 0;
};

struct FMaterialExpressionConnection
{
	int32 OutputNodeId = 0;
	int32 OutputPin = 0;
	int32 InputNodeId = 0;
	int32 InputPin = 0;
};

inline constexpr int32 MaxMaterialGraphParameters = 16;
inline constexpr int32 MaxMaterialGraphTextures = 6;

struct FStaticMeshMaterialParams
{
	FVector4 BaseColor;
	FVector2 UVOffset;
	FVector2 TextureFlags; // 텍스처 플래그 및 노멀 반전
	FVector2 UVTiling;
	FVector2 WorldUVTileSize;
	FVector4 Emissive;
	FVector4 SpecularColorShininess;
	FVector4 WorldUVParams; // x: local axes, y: opaque, z: shading override, w: model
};
static_assert(sizeof(FStaticMeshMaterialParams) == 96);

UCLASS()
class UMaterial : public UMaterialInterface
{
	GENERATED_BODY()

public:
	UMaterial() :SortID(NextSortID++) {};
	virtual ~UMaterial() override = default;

	uint32 UsageFlags = static_cast<uint32>(EMaterialUsage::StaticMesh);

	bool HasUsage(EMaterialUsage Usage) const
	{
		return (UsageFlags & static_cast<uint32>(Usage)) != 0;
	}

	void SetUsage(EMaterialUsage Usage, bool bEnabled)
	{
		const uint32 Flag = static_cast<uint32>(Usage);
		if (bEnabled)
		{
			UsageFlags |= Flag;
		}
		else
		{
			UsageFlags &= ~Flag;
		}
	}
	FShaderProgram* Shader = nullptr;
	TArray<UTexture2D*> Textures;
	TArray<FMaterialExpressionNode> ExpressionNodes;
	TArray<FMaterialExpressionConnection> ExpressionConnections;
	int32 OutputNodeId = 0;

	void EnsureDefaultGraph();

	int32 GetExpressionInputPinCount(EMaterialExpressionType Type) const;
	int32 GetExpressionOutputPinCount(EMaterialExpressionType Type) const;
	const char* GetExpressionInputPinName(EMaterialExpressionType Type, int32 PinIndex) const;
	const char* GetExpressionOutputPinName(EMaterialExpressionType Type, int32 PinIndex) const;

	EMaterialValueType GetExpressionOutputType(int32 NodeId, int32 OutputPin = 0) const;

	bool ConnectExpressionPins(int32 OutputNodeId, int32 OutputPin, int32 InputNodeId, int32 InputPin, FString& OutError);
	bool ValidateExpressionGraph(FString& OutError) const;

	TUniquePtr<FConstantBuffer> ParamBuffer;
	EPSOType PSOType = EPSOType::StaticMesh_Opaque;
	EBlendState BlendState = EBlendState::Opaque;
	EDepthStencilState DepthStencilState = EDepthStencilState::Default;
	ESamplerState SamplerState = ESamplerState::LinearClamp;

	FVector4 BaseColor = FVector4(1, 1, 1, 1);
	FVector SpecularColor = FVector(1.0f, 1.0f, 1.0f);
	float Shininess = 32.0f;
	FVector2 UVScrollSpeed = FVector2(0.0f, 0.0f);
	bool bFlipNormalY = false;
	// TEAM5 material controls. BaseColor keeps the existing TEAM6 scene key;
	// its RGB is the linear diffuse color / texture factor (DiffuseOrFactor).
	float SpecularFactor = 1.0f;
	FVector EmissiveColor = FVector(0.0f, 0.0f, 0.0f);
	float EmissiveFactor = 1.0f;
	FVector2 UVTiling = FVector2(1.0f, 1.0f);
	FVector2 WorldUVTileSize = FVector2(0.0f, 0.0f);
	bool bWorldUVLocalSpace = false;
	ELightingModel ShadingModel = ELightingModel::Phong;
	bool bOverrideShadingModel = false;
	void SetBlendState(EBlendState InBlendState);
	UMaterial* GetBaseMaterial() const override { return const_cast<UMaterial*>(this); }
	void GetRenderProxy(FMaterialRenderProxy& OutProxy) const override;

	EPSOType GetPSOType() const override { return PSOType; }
	EBlendState GetBlendState() const override { return BlendState; }
	ESamplerState GetSamplerState() const override { return SamplerState; }
	uint16 GetSortID() const override { return SortID; }
	void ApplyImportedProperties(const FImportedMaterialProperties& Properties);

	static UMaterialInstanceDynamic* CreateDynamicInstance(UMaterialInterface* Parent);

	static json SaveMaterial(const UMaterialInterface* Material);
	static UMaterialInterface* LoadMaterial(const json& In);

	const uint16 SortID;

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
	static void SerializeMaterialReference(FStructuredArchive::FSlot Slot, UMaterialInterface*& Material);

	void CopySettingsFrom(const UMaterial& Source);
	void InitializeRenderResources(const UMaterial& Template);

private:
	inline static uint16 NextSortID = 0;
};

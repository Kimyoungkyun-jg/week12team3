#include "EnginePCH.h"
#include "Components/DecalComponent.h"

#include <algorithm>
#include <cmath>

#include "Render/LineBatcher.h"
#include "Render/Material.h"
#include "Render/Texture2D.h"
#include "Render/NormalMapUtils.h"
#include "Render/MaterialRenderProxy.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/Actor.h"

namespace
{
	bool IsSRGBView(FTexture2D* Texture)
	{
		if (!Texture || !Texture->GetSRV())
			return false;

		D3D11_SHADER_RESOURCE_VIEW_DESC Desc{};
		Texture->GetSRV()->GetDesc(&Desc);

		switch (Desc.Format)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return true;

		default:
			return false;
		}
	}
}

UDecalComponent::UDecalComponent()
{
	// 로컬 박스: XYZ 모두 [-0.5, +0.5]
	//
	// X: 투영 깊이
	// Y: 이미지 너비
	// Z: 이미지 높이
	SetRelativeScale3D(FVector(5.0f, 10.0f, 10.0f));
}

bool UDecalComponent::TryGetRenderData(
	FProjectedDecalRenderData& OutRenderData) const
{
	if (!bDecalEnabled || !DecalMaterial)
		return false;

	FMaterialRenderProxy MaterialProxy{};
	DecalMaterial->GetRenderProxy(MaterialProxy);

	if (MaterialProxy.Textures.IsEmpty())
	{
		return false;
	}

	UTexture2D* TextureAsset = MaterialProxy.Textures[0];

	FTexture2D* Texture =
		TextureAsset ? TextureAsset->GetResource() : nullptr;
	if (!Texture || !Texture->GetSRV())
		return false;

	const FVector4& MaterialColor =
		MaterialProxy.BaseColor;

	if (!std::isfinite(MaterialColor.X) ||
		!std::isfinite(MaterialColor.Y) ||
		!std::isfinite(MaterialColor.Z) ||
		!std::isfinite(MaterialColor.W))
	{
		return false;
	}

	const float Opacity =
		std::clamp(MaterialColor.W, 0.0f, 1.0f);

	if (Opacity <= 0.0f)
		return false;

	// 양수 월드 스케일만 지원한다.
	const FVector WorldScale = GetWorldScale3D();

	constexpr float MinScale = 1e-4f;

	if (!std::isfinite(WorldScale.X) ||
		!std::isfinite(WorldScale.Y) ||
		!std::isfinite(WorldScale.Z) ||
		WorldScale.X < MinScale ||
		WorldScale.Y < MinScale ||
		WorldScale.Z < MinScale)
	{
		return false;
	}

	FProjectedDecalRenderData Data{};

	// 상대 Transform이 아닌 부모까지 반영된 월드 행렬.
	Data.DecalToWorld = GetWorldMatrix();

	Data.TintOpacity = FVector4(
		std::max(0.0f, MaterialColor.X),
		std::max(0.0f, MaterialColor.Y),
		std::max(0.0f, MaterialColor.Z),
		Opacity);

	Data.ColorTexture = Texture;

	UTexture2D* NormalAsset =
		MaterialProxy.Textures.Num() > 1
		? MaterialProxy.Textures[1]
		: nullptr;

	FTexture2D* NormalResource =
		NormalAsset ? NormalAsset->GetResource() : nullptr;

	Data.NormalEncoding =
		NormalMapUtils::GetEncoding(NormalResource);

	Data.NormalTexture =
		Data.NormalEncoding > 0.0f
		? NormalResource
		: nullptr;

	Data.NormalStrength =
		std::isfinite(NormalStrength)
		? std::max(0.0f, NormalStrength)
		: 0.0f;

	Data.NormalOpacity =
		std::isfinite(NormalOpacity)
		? std::clamp(NormalOpacity, 0.0f, 1.0f)
		: 0.0f;

	Data.bFlipNormalY = MaterialProxy.bFlipNormalY;

	Data.Sampler = MaterialProxy.SamplerState;

	// 일반 색상 샘플링에 비교 샘플러는 사용하지 않는다.
	if (Data.Sampler == ESamplerState::ShadowComparison)
	{
		Data.Sampler = ESamplerState::LinearClamp;
	}

	Data.bDecodeSRGB = !IsSRGBView(Texture);
	Data.bUnlit = bUnlit;

	Data.SortOrder = SortOrder;
	Data.ComponentID = GetUUID();

	if (!IsValidProjectedDecalRenderData(Data))
		return false;

	OutRenderData = Data;
	return true;
}

void DrawDecalVolume(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected)
{
	if (!World || !LineBatcher) return;

	for (TObjectIterator<UDecalComponent> It; It; ++It)
	{
		UDecalComponent* Decal = *It;

		if (!Decal ||
			Decal->GetWorld() != World ||
			!Decal->IsRegistered())
		{
			continue;
		}

		LineBatcher->AddOrientedBox(
			Decal->CalcLocalBounds(),
			Decal->GetWorldMatrix(),
			FVector4(0.0f, 1.0f, 1.0f, 1.0f));
	}
}

void UDecalComponent::Serialize(
	FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	// 기존 데칼이 저장된 씬에 이 필드강 없어도 읽을 수 있게 한다.
	auto MaterialSlot = Record.TryEnterField("DecalMaterial", true);

	if (MaterialSlot.IsSet())
	{
		UMaterial::SerializeMaterialReference(
			MaterialSlot.GetValue(),
			DecalMaterial);
	}
}
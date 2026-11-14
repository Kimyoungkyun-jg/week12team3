#pragma once

#include "Render/Material.h"

struct FMaterialRenderProxy
{
	const UMaterial* BaseMaterial = nullptr;

	TArray<UTexture2D*> Textures;

	FVector4 BaseColor = FVector4(1, 1, 1, 1);
	FVector SpecularColor = FVector(1, 1, 1);
	float Shininess = 32.0f;
	FVector2 UVScrollSpeed = FVector2(0, 0);
	bool bFlipNormalY = false;
	float SpecularFactor = 1.0f;
	FVector EmissiveColor = FVector(0, 0, 0);
	float EmissiveFactor = 1.0f;
	FVector2 UVTiling = FVector2(1, 1);
	FVector2 WorldUVTileSize = FVector2(0, 0);
	bool bWorldUVLocalSpace = false;
	ELightingModel ShadingModel = ELightingModel::Phong;
	bool bOverrideShadingModel = false;

	EPSOType PSOType = EPSOType::StaticMesh_Opaque;
	EBlendState BlendState = EBlendState::Opaque;
	EDepthStencilState DepthStencilState = EDepthStencilState::Default;
	ESamplerState SamplerState = ESamplerState::LinearClamp;
};
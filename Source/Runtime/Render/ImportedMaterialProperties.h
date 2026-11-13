#pragma once
#include "Math/Vector.h"
#include "Render/RenderEnums.h"

// Shared material properties carried by OBJ/FBX import and their bake formats.
// Texture paths and material slot names remain on the mesh slot.
struct FImportedMaterialProperties
{
    FVector SpecularColor = FVector(1, 1, 1);
    float SpecularFactor = 1.0f;
    FVector EmissiveColor = FVector(0, 0, 0);
    float EmissiveFactor = 1.0f;
    float Shininess = 32.0f;
    ELightingModel ShadingModel = ELightingModel::Phong;
    bool bOverrideShadingModel = false;
};

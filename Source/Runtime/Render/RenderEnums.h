#pragma once

#include <cstdint>

enum class EShaderType
{
	Vertex,
	Pixel,
	Compute,
};

enum class ERenderingPath
{
    Forward,
    Deferred
};

enum class ESkinningMode : uint8_t
{
	CPU,
	GPU,
	Compute
};

inline const char* GetSkinningModeName(ESkinningMode Mode)
{
	switch (Mode)
	{
	case ESkinningMode::GPU: return "GPU";
	case ESkinningMode::Compute: return "CS";
	default: return "CPU";
	}
}

// Reflection determines the lighting equation, independently of interpolation.
enum class EReflectionModel
{
    Phong,
    BlinnPhong,
    Lambert
};

// Gouraud interpolates vertex lighting; Phong evaluates interpolated normals per pixel.
enum class EShadingModel
{
    Gouraud,
    Phong
};

// Legacy material/config values. Preserve numeric values for existing assets.
enum class ELightingModel
{
    Gouraud,
    Lambert,
    Phong,
    Unlit // 조명과 그림자 없이 Base Color만 출력한다
};

enum class ELightCullingMode
{
    Off = 0,
    Tiled = 1,
    Clustered = 2
};

enum class EPcfMode : uint32_t
{
    Off = 0,
    Sample8 = 1,
    Sample16 = 2,
    Sample32 = 3,
    PCSS = 4
};

enum class EDecalRenderMethod
{
    MeshRerendering,
    DepthReconstruction
};

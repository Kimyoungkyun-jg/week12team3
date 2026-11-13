#ifndef NORMAL_MAP_COMMON_HLSLI
#define NORMAL_MAP_COMMON_HLSLI

float3 DecodeMapNormal(float3 Sample, float Encoding)
{
    if (Encoding > 1.5f)
    {
        float2 XY =
            Encoding > 2.5f
            ? Sample.xy
            : Sample.xy * 2.0f - 1.0f;

        return float3(
            XY,
            sqrt(saturate(1.0f - dot(XY, XY))));
    }

    return Sample * 2.0f - 1.0f;
}

#endif
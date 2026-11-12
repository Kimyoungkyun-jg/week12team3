#include "EnginePCH.h"
#include "Transform.h"

FTransform::FTransform(FVector InL, FVector InS, FRotator InR)
{
	Location = InL;
	Scale = InS;
	Rotation = InR.Quaternion();
}

FTransform::FTransform(FVector InL, FVector InS, const FQuat& InR)
{
	Location = InL;
	Scale = InS;
	Rotation = InR;
}

FMatrix FTransform::GetLocalMatrix() const
{
    FMatrix RotationMatrix = Rotation.ToFMatrix();

    FMatrix WorldMatrix = FMatrix(
        RotationMatrix[0][0] * Scale.X, RotationMatrix[0][1] * Scale.X, RotationMatrix[0][2] * Scale.X, 0.0f,
        RotationMatrix[1][0] * Scale.Y, RotationMatrix[1][1] * Scale.Y, RotationMatrix[1][2] * Scale.Y, 0.0f,
        RotationMatrix[2][0] * Scale.Z, RotationMatrix[2][1] * Scale.Z, RotationMatrix[2][2] * Scale.Z, 0.0f,
        Location.X, Location.Y, Location.Z, 1.0f
    );

    return WorldMatrix;
}

FVector FTransform::GetForward() const
{
    FQuat Quat = GetOrientation();
    return Quat.RotateVector(FVector(1.0f, 0.0f, 0.0f));
}

FVector FTransform::GetUp() const
{
    FQuat Quat = GetOrientation();
    return Quat.RotateVector(FVector(0.0f, 0.0f, 1.0f));
}

FVector FTransform::GetRight() const
{
    return GetOrientation().RotateVector(FVector(0.0f, 1.0f, 0.0f));
}

FQuat FTransform::GetOrientation() const
{
    return Rotation;
}

FTransform FTransform::Identity = FTransform(FVector(0, 0, 0), FVector(1, 1, 1), FRotator(0, 0, 0));
FTransform FTransform::GetRelativeTransform(const FTransform& Other) const
{
	// Unreal FTransform::GetRelativeTransform
	// Scale = this.Scale / Other.Scale
	// Rotation = Other.Rotation⁻¹ * this.Rotation
	// Location = (Other.Rotation⁻¹ 로 돌린 (this.Location - Other.Location)) / Other.Scale
	auto SafeReciprocal = [](float V) { return std::fabs(V) > 1.e-8f ? 1.0f / V : 0.0f; };
	const FVector InvScale(SafeReciprocal(Other.Scale.X), SafeReciprocal(Other.Scale.Y), SafeReciprocal(Other.Scale.Z));

	const FQuat InvRotation = Other.Rotation.Inverse();
	const FVector Delta = InvRotation.RotateVector(Location - Other.Location);

	FTransform Result;
	Result.Scale    = FVector(Scale.X * InvScale.X, Scale.Y * InvScale.Y, Scale.Z * InvScale.Z);
	Result.Rotation = (InvRotation * Rotation).Normalized();
	Result.Location = FVector(Delta.X * InvScale.X, Delta.Y * InvScale.Y, Delta.Z * InvScale.Z);
	return Result;
}

#pragma once

#include "UObject/Object.h"
#include "Math/Vector.h"
#include "Math/Color.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Curves/CurveFloat.h"
#include "ParticleDistribution.generated.h"

struct FParticleInstanceParameters
{
	TMap<FString, FColor> ColorParams;
	TMap<FString, FVector> VectorParams;
	TMap<FString, float> FloatParams;

	FColor GetColor(const FString& Name, const FColor& Default) const;
	FVector GetVector(const FString& Name, const FVector& Default) const;
	float GetFloat(const FString& Name, float Default) const;
};

struct FParticleRandom
{
	uint32 State = 1;
	const FParticleInstanceParameters* InstanceParams = nullptr;
	float Range(float Min, float Max);
};

struct FParticleCurveTrack
{
	FString Name;
	UCurveFloat* Curve = nullptr;
	FColor Color = FColor(255, 255, 255, 255);
};

// 분포 연산 방식 정의
enum class EDistributionMode : uint8
{
	Constant = 0,
	Uniform = 1,
	ConstantCurve = 2,
	UniformCurve = 3,
	Parameter = 4
};

// 런타임 고속 평가용 룩업 테이블
struct FParticleDistributionLookupTable
{
	enum class EDistributionOp : uint8
	{
		None = 0,
		Constant = 1,
		Random = 2,
		ConstantCurve = 3,
		UniformCurve = 4
	};

	EDistributionOp Op = EDistributionOp::None;
	uint32 EntryCount = 0;
	uint32 EntryStride = 0;
	uint32 SubEntryStride = 0;
	float TimeScale = 0.0f;
	float TimeBias = 0.0f;
	TArray<float> Values;

	float EvalFloat(float Time, FParticleRandom* Random) const;
	FVector EvalVector(float Time, FParticleRandom* Random, bool bLockAxes) const;
};

UCLASS()
class UDistributionFloat : public UObject
{
	GENERATED_BODY()
public:
	virtual float GetValue(float Time, const FParticleInstanceParameters* Params, FParticleRandom* Random) const;
	virtual void GetOutRange(float& OutMin, float& OutMax) const;
	virtual void CollectCurves(const FString& Prefix, const FColor& Color, TArray<FParticleCurveTrack>& OutTracks) const {}
	virtual void SampleCurveValues(uint32 SampleCount, TArray<float>& OutValues) const;
	virtual int32 GetModeIndex() const { return static_cast<int32>(EDistributionMode::Constant); }
	virtual bool UsesCurveAtlas() const { return false; }

	void InvalidateLookupTable() const { bLookupDirty = true; }
	const FParticleDistributionLookupTable& EnsureLookupTable() const;

protected:
	virtual void BuildLookupTable(FParticleDistributionLookupTable& OutTable) const;

	mutable FParticleDistributionLookupTable LookupTable;
	mutable bool bLookupDirty = true;
};

UCLASS()
class UDistributionFloatConstant : public UDistributionFloat
{
	GENERATED_BODY()
public:
	float GetValue(float Time, const FParticleInstanceParameters* Params, FParticleRandom* Random) const override;
	void GetOutRange(float& OutMin, float& OutMax) const override;
	void SampleCurveValues(uint32 SampleCount, TArray<float>& OutValues) const override;
	int32 GetModeIndex() const override { return static_cast<int32>(EDistributionMode::Constant); }

	UPROPERTY(EditAnywhere)
	float Constant = 1.0f;

protected:
	void BuildLookupTable(FParticleDistributionLookupTable& OutTable) const override;
};

UCLASS()
class UDistributionFloatUniform : public UDistributionFloat
{
	GENERATED_BODY()
public:
	float GetValue(float Time, const FParticleInstanceParameters* Params, FParticleRandom* Random) const override;
	void GetOutRange(float& OutMin, float& OutMax) const override;
	void SampleCurveValues(uint32 SampleCount, TArray<float>& OutValues) const override;
	int32 GetModeIndex() const override { return static_cast<int32>(EDistributionMode::Uniform); }

	UPROPERTY(EditAnywhere)
	float Min = 0.0f;
	UPROPERTY(EditAnywhere)
	float Max = 1.0f;

protected:
	void BuildLookupTable(FParticleDistributionLookupTable& OutTable) const override;
};

UCLASS()
class UDistributionFloatConstantCurve : public UDistributionFloat
{
	GENERATED_BODY()
public:
	UDistributionFloatConstantCurve();
	float GetValue(float Time, const FParticleInstanceParameters* Params, FParticleRandom* Random) const override;
	void GetOutRange(float& OutMin, float& OutMax) const override;
	void CollectCurves(const FString& Prefix, const FColor& Color, TArray<FParticleCurveTrack>& OutTracks) const override;
	void SampleCurveValues(uint32 SampleCount, TArray<float>& OutValues) const override;
	int32 GetModeIndex() const override { return static_cast<int32>(EDistributionMode::ConstantCurve); }
	bool UsesCurveAtlas() const override { return true; }

	UPROPERTY(Instanced)
	UCurveFloat* Curve = nullptr;

protected:
	void BuildLookupTable(FParticleDistributionLookupTable& OutTable) const override;
};

UCLASS()
class UDistributionFloatUniformCurve : public UDistributionFloat
{
	GENERATED_BODY()
public:
	UDistributionFloatUniformCurve();
	float GetValue(float Time, const FParticleInstanceParameters* Params, FParticleRandom* Random) const override;
	void GetOutRange(float& OutMin, float& OutMax) const override;
	void CollectCurves(const FString& Prefix, const FColor& Color, TArray<FParticleCurveTrack>& OutTracks) const override;
	void SampleCurveValues(uint32 SampleCount, TArray<float>& OutValues) const override;
	int32 GetModeIndex() const override { return static_cast<int32>(EDistributionMode::UniformCurve); }
	bool UsesCurveAtlas() const override { return true; }

	UPROPERTY(Instanced)
	UCurveFloat* MinCurve = nullptr;
	UPROPERTY(Instanced)
	UCurveFloat* MaxCurve = nullptr;

protected:
	void BuildLookupTable(FParticleDistributionLookupTable& OutTable) const override;
};

UCLASS()
class UDistributionFloatParticleParameter : public UDistributionFloat
{
	GENERATED_BODY()
public:
	float GetValue(float Time, const FParticleInstanceParameters* Params, FParticleRandom* Random) const override;
	void GetOutRange(float& OutMin, float& OutMax) const override;
	void SampleCurveValues(uint32 SampleCount, TArray<float>& OutValues) const override;
	int32 GetModeIndex() const override { return static_cast<int32>(EDistributionMode::Parameter); }

	UPROPERTY(EditAnywhere)
	FString ParameterName;
	UPROPERTY(EditAnywhere)
	float Constant = 1.0f;

protected:
	void BuildLookupTable(FParticleDistributionLookupTable& OutTable) const override;
};

UCLASS()
class UDistributionVector : public UObject
{
	GENERATED_BODY()
public:
	virtual FVector GetValue(float Time, const FParticleInstanceParameters* Params, FParticleRandom* Random) const;
	virtual void GetOutRange(FVector& OutMin, FVector& OutMax) const;
	virtual void CollectCurves(const FString& Prefix, const FColor& Color, TArray<FParticleCurveTrack>& OutTracks) const {}
	virtual void SampleCurveVectors(uint32 SampleCount, TArray<FVector>& OutValues) const;
	virtual int32 GetModeIndex() const { return static_cast<int32>(EDistributionMode::Constant); }
	virtual bool UsesCurveAtlas() const { return false; }

	void InvalidateLookupTable() const { bLookupDirty = true; }
	const FParticleDistributionLookupTable& EnsureLookupTable() const;

protected:
	virtual void BuildLookupTable(FParticleDistributionLookupTable& OutTable) const;

	mutable FParticleDistributionLookupTable LookupTable;
	mutable bool bLookupDirty = true;
};

UCLASS()
class UDistributionVectorConstant : public UDistributionVector
{
	GENERATED_BODY()
public:
	FVector GetValue(float Time, const FParticleInstanceParameters* Params, FParticleRandom* Random) const override;
	void GetOutRange(FVector& OutMin, FVector& OutMax) const override;
	void SampleCurveVectors(uint32 SampleCount, TArray<FVector>& OutValues) const override;
	int32 GetModeIndex() const override { return static_cast<int32>(EDistributionMode::Constant); }

	UPROPERTY(EditAnywhere)
	FVector Constant = FVector::OneVector;

protected:
	void BuildLookupTable(FParticleDistributionLookupTable& OutTable) const override;
};

UCLASS()
class UDistributionVectorUniform : public UDistributionVector
{
	GENERATED_BODY()
public:
	FVector GetValue(float Time, const FParticleInstanceParameters* Params, FParticleRandom* Random) const override;
	void GetOutRange(FVector& OutMin, FVector& OutMax) const override;
	void SampleCurveVectors(uint32 SampleCount, TArray<FVector>& OutValues) const override;
	int32 GetModeIndex() const override { return static_cast<int32>(EDistributionMode::Uniform); }

	UPROPERTY(EditAnywhere)
	FVector Min = FVector::ZeroVector;
	UPROPERTY(EditAnywhere)
	FVector Max = FVector::ZeroVector;
	UPROPERTY(EditAnywhere)
	bool bLockAxes = false;

protected:
	void BuildLookupTable(FParticleDistributionLookupTable& OutTable) const override;
};

UCLASS()
class UDistributionVectorConstantCurve : public UDistributionVector
{
	GENERATED_BODY()
public:
	UDistributionVectorConstantCurve();
	FVector GetValue(float Time, const FParticleInstanceParameters* Params, FParticleRandom* Random) const override;
	void GetOutRange(FVector& OutMin, FVector& OutMax) const override;
	void CollectCurves(const FString& Prefix, const FColor& Color, TArray<FParticleCurveTrack>& OutTracks) const override;
	void SampleCurveVectors(uint32 SampleCount, TArray<FVector>& OutValues) const override;
	int32 GetModeIndex() const override { return static_cast<int32>(EDistributionMode::ConstantCurve); }
	bool UsesCurveAtlas() const override { return true; }

	UPROPERTY(Instanced)
	UCurveFloat* CurveX = nullptr;
	UPROPERTY(Instanced)
	UCurveFloat* CurveY = nullptr;
	UPROPERTY(Instanced)
	UCurveFloat* CurveZ = nullptr;

protected:
	void BuildLookupTable(FParticleDistributionLookupTable& OutTable) const override;
};

UCLASS()
class UDistributionVectorUniformCurve : public UDistributionVector
{
	GENERATED_BODY()
public:
	UDistributionVectorUniformCurve();
	FVector GetValue(float Time, const FParticleInstanceParameters* Params, FParticleRandom* Random) const override;
	void GetOutRange(FVector& OutMin, FVector& OutMax) const override;
	void CollectCurves(const FString& Prefix, const FColor& Color, TArray<FParticleCurveTrack>& OutTracks) const override;
	void SampleCurveVectors(uint32 SampleCount, TArray<FVector>& OutValues) const override;
	int32 GetModeIndex() const override { return static_cast<int32>(EDistributionMode::UniformCurve); }
	bool UsesCurveAtlas() const override { return true; }

	UPROPERTY(Instanced)
	UCurveFloat* MinCurveX = nullptr;
	UPROPERTY(Instanced)
	UCurveFloat* MinCurveY = nullptr;
	UPROPERTY(Instanced)
	UCurveFloat* MinCurveZ = nullptr;
	UPROPERTY(Instanced)
	UCurveFloat* MaxCurveX = nullptr;
	UPROPERTY(Instanced)
	UCurveFloat* MaxCurveY = nullptr;
	UPROPERTY(Instanced)
	UCurveFloat* MaxCurveZ = nullptr;
	UPROPERTY(EditAnywhere)
	bool bLockAxes = false;

protected:
	void BuildLookupTable(FParticleDistributionLookupTable& OutTable) const override;
};

UCLASS()
class UDistributionVectorParticleParameter : public UDistributionVector
{
	GENERATED_BODY()
public:
	FVector GetValue(float Time, const FParticleInstanceParameters* Params, FParticleRandom* Random) const override;
	void GetOutRange(FVector& OutMin, FVector& OutMax) const override;
	void SampleCurveVectors(uint32 SampleCount, TArray<FVector>& OutValues) const override;
	int32 GetModeIndex() const override { return static_cast<int32>(EDistributionMode::Parameter); }

	UPROPERTY(EditAnywhere)
	FString ParameterName;
	UPROPERTY(EditAnywhere)
	FVector Constant = FVector::ZeroVector;

protected:
	void BuildLookupTable(FParticleDistributionLookupTable& OutTable) const override;
};

// 분포 생성과 모드 전환 유틸리티
UDistributionFloat* CreateFloatDistributionByMode(UObject* Outer, int32 ModeIndex, float InitialValue);
UDistributionVector* CreateVectorDistributionByMode(UObject* Outer, int32 ModeIndex, const FVector& InitialValue);
UDistributionFloat* ConvertFloatDistributionMode(UObject* Outer, UDistributionFloat* Current, int32 NewModeIndex);
UDistributionVector* ConvertVectorDistributionMode(UObject* Outer, UDistributionVector* Current, int32 NewModeIndex);

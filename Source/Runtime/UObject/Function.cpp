#include "EnginePCH.h"
#include "UObject/Function.h"

#include <format>

namespace
{
	const char* GetTypeName(EScriptValueType Type)
	{
		switch (Type)
		{
		case EScriptValueType::Bool:    return "bool";
		case EScriptValueType::Int:     return "int";
		case EScriptValueType::Float:   return "float";
		case EScriptValueType::String:  return "string";
		case EScriptValueType::Vector:  return "FVector";
		case EScriptValueType::Rotator: return "FRotator";
		default:                     return "none";
		}
	}
}

EScriptValueType GetScriptValueType(const FScriptValue& Value)
{
	switch (Value.index())
	{
	case 1: return EScriptValueType::Bool;
	case 2: return EScriptValueType::Int;
	case 3: return EScriptValueType::Float;
	case 4: return EScriptValueType::String;
	case 5: return EScriptValueType::Vector;
	case 6: return EScriptValueType::Rotator;
	default: return EScriptValueType::None;
	}
}

bool FFunction::Invoke(UObject* Self, const TArray<FScriptValue>& Args, FScriptValue& OutReturn, FString& OutError) const
{
	if (!Self || !Thunk)
	{
		OutError = std::format("{}: no object to call on", Name);
		return false;
	}
	if (Args.Num() != ParamTypes.Num())
	{
		OutError = std::format("{}: expected {} argument(s), got {}", Name, ParamTypes.Num(), Args.Num());
		return false;
	}
	for (int32 Index = 0; Index < ParamTypes.Num(); ++Index)
	{
		if (GetScriptValueType(Args[Index]) != ParamTypes[Index])
		{
			OutError = std::format("{}: argument {} must be {}", Name, Index + 1, GetTypeName(ParamTypes[Index]));
			return false;
		}
	}

	OutReturn = Thunk(Self, Args.Num() > 0 ? &Args[0] : nullptr);
	return true;
}

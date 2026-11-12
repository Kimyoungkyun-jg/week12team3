#pragma once

#include "Core/Types.h"

enum class EObjectFlags : uint32
{
	RF_NoFlags = 0x00000000, // 플래그 없음
	RF_Public = 0x00000001,
	RF_Transactional = 0x00000008, // Undo Redo 관리 대상
	RF_ClassDefaultObject = 0x00000010, // 이 Object가 CDO인가?
	RF_ArchetypeObject = 0x00000020,
	RF_Transient = 0x00000040, // 직렬화 제외 플래그
	RF_DefaultSubObject = 0x00040000, // DefaultSubObject인가?
	RF_AllFlags = 0xffffffff, // 모든 플래그
};

DEFINE_ENUM_OPERATORS(EObjectFlags)

#define USTRUCT(...)
#define UCLASS(...)
#define UPROPERTY(...)
#define UFUNCTION(...)
#define UENUM(...)
#define UMETA(...)

#define BODY_MACRO_COMBINE_INNER(A,B,C,D) A##B##C##D
#define BODY_MACRO_COMBINE(A,B,C,D) BODY_MACRO_COMBINE_INNER(A,B,C,D)
#define GENERATED_BODY(...) BODY_MACRO_COMBINE(CURRENT_FILE_ID, _, __LINE__, _GENERATED_BODY)
#pragma once

#include "Core/Types.h"

enum class EObjectFlags : uint32
{
	RF_NoFlags = 0x00000000, // 플래그 없음
	RF_Transactional = 0x00000008, // Undo Redo 관리 대상
	RF_Transient = 0x00000040, // 직렬화 제외 플래그
	RF_DefaultSubObject = 0x00040000,
	RF_AllFlags = 0xffffffff, // 모든 플래그
};

DEFINE_ENUM_OPERATORS(EObjectFlags)

#define USTRUCT(...)
#define UCLASS(...)
#define UPROPERTY(...)
// HitoriHeaderTool이 인식하는 UPROPERTY 지정자 (쉼표로 조합 가능):
//   EditAnywhere       디테일 패널에서 표시하고 수정할 수 있다.
//   VisibleAnywhere    디테일 패널에 표시하되 수정할 수 없다.
//   Transient          저장/불러오기에서 제외한다.
//   Instanced          UObject 포인터 또는 배열의 하위 객체를 참조 대신 내용과 함께 저장한다.
//   Type=Color         디테일 패널의 표시 타입을 지정한다. 허용값: Float, Int, String,
//                      Bool, Vector, Vector4, Color, LinearColor, Rotator, Transform, Object.
//   meta=(ClampMin=0, ClampMax=1, UIMin=0, UIMax=1, Delta=0.1)
//                      숫자 입력의 값 범위, UI 범위, 조절 간격을 지정한다.
// EditAnywhere와 VisibleAnywhere는 함께 쓸 수 없다. 예: UPROPERTY(EditAnywhere, Transient)
// TArray<구조체>는 해당 구조체의 TStructPropertyType 등록이 필요하다.
#define UFUNCTION(...)
#define UENUM(...)
#define UMETA(...)

#define BODY_MACRO_COMBINE_INNER(A,B,C,D) A##B##C##D
#define BODY_MACRO_COMBINE(A,B,C,D) BODY_MACRO_COMBINE_INNER(A,B,C,D)
#define GENERATED_BODY(...) BODY_MACRO_COMBINE(CURRENT_FILE_ID, _, __LINE__, _GENERATED_BODY)

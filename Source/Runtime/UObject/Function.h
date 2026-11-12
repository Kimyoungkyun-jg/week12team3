#pragma once

#include <tuple>
#include <utility>
#include <variant>

#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Math/EngineMath.h"

class UObject;

// 함수 지정자 (UE의 UFUNCTION 지정자에 대응)
enum EFunctionFlags : uint32
{
    FUNC_None           = 0,
    FUNC_CallInEditor   = 1 << 0, // 디테일 패널에 버튼으로 표시. 인자가 없어야 한다
    FUNC_ScriptCallable = 1 << 1, // Lua에서 actor:함수이름(...)으로 호출
};

// UFUNCTION 인자·반환값 타입. UE는 UFunction의 인자도 FProperty 자식으로 두지만,
// Hitori는 Lua와 주고받는 몇 가지 타입만 쓰므로 FScriptValue의 종류를 그대로 쓴다. 순서는 FScriptValue와 같다.
enum class EScriptValueType { None, Bool, Int, Float, String, Vector, Rotator };

// 리플렉션으로 주고받는 값 하나. UE는 FFrame 스택에 인자를 쌓지만, Hitori는 지원 타입이 적어서 variant로 충분하다.
using FScriptValue = std::variant<std::monostate, bool, int32, float, FString, FVector, FRotator>;

// C++ 타입 → FScriptValue 저장 타입과 EScriptValueType. 여기 없는 타입을 UFUNCTION 인자/반환값에 쓰면 static_assert로 막는다.
template <typename T>
struct TScriptType
{
    static constexpr bool bSupported = false;
};

#define DEFINE_SCRIPT_TYPE(CppType, StorageType, EnumValue)                 \
    template <> struct TScriptType<CppType>                                 \
    {                                                                       \
        using Storage = StorageType;                                        \
        static constexpr bool bSupported = true;                            \
        static constexpr EScriptValueType Type = EScriptValueType::EnumValue;     \
    };

DEFINE_SCRIPT_TYPE(bool, bool, Bool)
DEFINE_SCRIPT_TYPE(int32, int32, Int)
DEFINE_SCRIPT_TYPE(uint32, int32, Int)
DEFINE_SCRIPT_TYPE(float, float, Float)
DEFINE_SCRIPT_TYPE(FString, FString, String)
DEFINE_SCRIPT_TYPE(FVector, FVector, Vector)
DEFINE_SCRIPT_TYPE(FRotator, FRotator, Rotator)

// 이 값이 담고 있는 타입. 비어 있으면 None
EScriptValueType GetScriptValueType(const FScriptValue& Value);

// 썽크: 타입이 지워진 인자 배열을 받아 진짜 C++ 멤버 함수를 부른다 (UE의 execFoo / DECLARE_FUNCTION).
using FNativeFunctionThunk = FScriptValue(*)(UObject* Self, const FScriptValue* Args);

struct FFunction
{
    FString Name;
    uint32 Flags = FUNC_None;
    TArray<EScriptValueType> ParamTypes;
    EScriptValueType ReturnType = EScriptValueType::None; // None이면 void
    FNativeFunctionThunk Thunk = nullptr;

    bool HasAnyFlags(uint32 InFlags) const { return (Flags & InFlags) != 0; }
    bool ReturnsValue() const { return ReturnType != EScriptValueType::None; }

    // 인자 개수와 타입을 확인한 뒤 호출한다. 맞지 않으면 false와 OutError
    bool Invoke(UObject* Self, const TArray<FScriptValue>& Args, FScriptValue& OutReturn, FString& OutError) const;
};

// 멤버 함수 포인터에서 클래스·반환 타입·인자 타입을 꺼낸다.
template <typename T>
struct TMemberFunctionTraits;

template <typename C, typename R, typename... A>
struct TMemberFunctionTraits<R (C::*)(A...)>
{
    using Class = C;
    using Return = R;
    using Args = std::tuple<A...>;
};

template <typename C, typename R, typename... A>
struct TMemberFunctionTraits<R (C::*)(A...) const> : TMemberFunctionTraits<R (C::*)(A...)>
{
};

// Func마다 썽크 하나가 컴파일 시간에 만들어진다. HHT는 함수 이름만 알면 되고 시그니처는 컴파일러가 알아낸다.
template <auto Func>
struct TNativeFunction
{
    using Traits = TMemberFunctionTraits<decltype(Func)>;
    using Class = typename Traits::Class;
    using Return = std::remove_cvref_t<typename Traits::Return>;
    using Args = typename Traits::Args;
    static constexpr size_t ArgCount = std::tuple_size_v<Args>;

    template <size_t I>
    using TArg = std::tuple_element_t<I, Args>;

    template <size_t... I>
    static constexpr bool AreArgsSupported(std::index_sequence<I...>)
    {
        return (TScriptType<std::remove_cvref_t<TArg<I>>>::bSupported && ...);
    }

    template <size_t... I>
    static constexpr bool HasOutParam(std::index_sequence<I...>)
    {
        return ((std::is_lvalue_reference_v<TArg<I>> && !std::is_const_v<std::remove_reference_t<TArg<I>>>) || ...);
    }

    template <size_t I>
    static std::remove_cvref_t<TArg<I>> GetArg(const FScriptValue* InArgs)
    {
        using ArgType = std::remove_cvref_t<TArg<I>>;
        return static_cast<ArgType>(std::get<typename TScriptType<ArgType>::Storage>(InArgs[I]));
    }

    template <size_t... I>
    static FScriptValue CallWith(Class* Object, const FScriptValue* InArgs, std::index_sequence<I...>)
    {
        if constexpr (std::is_void_v<Return>)
        {
            (Object->*Func)(GetArg<I>(InArgs)...);
            return {};
        }
        else
        {
            return FScriptValue(static_cast<typename TScriptType<Return>::Storage>((Object->*Func)(GetArg<I>(InArgs)...)));
        }
    }

    static FScriptValue Call(UObject* Self, const FScriptValue* InArgs)
    {
        return CallWith(static_cast<Class*>(Self), InArgs, std::make_index_sequence<ArgCount>{});
    }

    template <size_t... I>
    static TArray<EScriptValueType> GetParamTypes(std::index_sequence<I...>)
    {
        TArray<EScriptValueType> Types;
        (Types.Add(TScriptType<std::remove_cvref_t<TArg<I>>>::Type), ...);
        return Types;
    }

    static FFunction Make(const FString& Name, uint32 Flags)
    {
        // 클래스 본문의 static_assert에서는 아직 정의가 끝나지 않은 멤버 함수를 쓸 수 없어서 여기서 검사한다.
        static_assert(AreArgsSupported(std::make_index_sequence<ArgCount>{}),
            "UFUNCTION parameters must be bool, int32, uint32, float, FString, FVector or FRotator.");
        static_assert(!HasOutParam(std::make_index_sequence<ArgCount>{}),
            "UFUNCTION parameters must be by value or const reference. Use the return value for output.");
        static_assert(std::is_void_v<Return> || TScriptType<Return>::bSupported,
            "UFUNCTION return type must be void, bool, int32, uint32, float, FString, FVector or FRotator.");

        FFunction Function;
        Function.Name = Name;
        Function.Flags = Flags;
        Function.ParamTypes = GetParamTypes(std::make_index_sequence<ArgCount>{});
        if constexpr (!std::is_void_v<Return>)
        {
            Function.ReturnType = TScriptType<Return>::Type;
        }
        Function.Thunk = &Call;
        return Function;
    }
};

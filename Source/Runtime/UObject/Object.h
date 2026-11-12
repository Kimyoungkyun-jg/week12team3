#pragma once

#include "Core/Types.h"
#include "Core/EngineStatics.h"
#include "Core/NameTypes.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"
#include "Serialization/Archive.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"

class UClass;
class FProperty;
// Property Reflection

#define INVALID_OBJECT ((UObject*)-1)

// 추상 클래스는 생성자를 등록하지 않는다.
// 템플릿이어야 버려진 if constexpr 분기의 new T()가 컴파일되지 않고,
// 멤버여야 private 생성자(싱글턴 등)에 접근할 수 있다.
#define DECLARE_CLASS(ClassName, SuperClassName)                        \
public:                                                                 \
    using Super = SuperClassName;                                       \
    using ThisClass = ClassName;		                                \
    template <typename T = ClassName>                                   \
	static UObject* InternalConstructInstance(void* Memory)				\
	{																	\
		if constexpr (std::is_abstract_v<T>) { return nullptr; }		\
		else { return new (Memory) T; }									\
	}																	\
    static UClass* StaticClass()                                        \
    {                                                                   \
        static UClass c;                                                \
        static bool bIsInit = false;                                    \
        if (!bIsInit)                                                   \
        {                                                               \
            c.Name  = #ClassName;                                       \
            c.Super = Super::StaticClass();								\
            c.Size = sizeof(ClassName);							\
			c.Constructor = std::is_abstract_v<ClassName> ? nullptr : &InternalConstructInstance<ClassName>;\
			if (&ClassName::RegisterProperties != &Super::RegisterProperties) \
			{															\
				ClassName::RegisterProperties(&c);						\
			}															\
            if (&ClassName::RegisterFunctions != &Super::RegisterFunctions) \
            {															\
                ClassName::RegisterFunctions(&c);                       \
            }															\
            RegisterClass(&c);                                          \
            bIsInit = true;                                             \
        }                                                               \
        return &c;                                                      \
    }                                                                   \
    struct FAutoRegister {FAutoRegister() { ThisClass::StaticClass();}};\
    inline static FAutoRegister AutoRegister;							\
private:																


// UObject의 정체(Class, Outer, Name, Flags). UE의 UObjectBase.
class UObjectBase
{
	friend UObject* StaticAllocateObject(const UClass*, UObject*, FName, EObjectFlags, bool, bool*);
public:
	// vptr이 맨 앞에 오게 한다. 없으면 UObject 안에서 멤버 위치가 8바이트 밀린다.
	virtual ~UObjectBase() = default;

	UClass* GetClass() const { return ClassPrivate; }

	const FName& GetFName() const { return Name; }         // FName 비교용
	FString GetName() const { return Name.ToString(); }    // Name 출력용

	UObject* GetOuter() const { return Outer; }
	void SetOuter(UObject* InOuter) { Outer = InOuter; }

	void SetName(const FName& InName) { Name = FName(InName); }

	inline void SetFlags(EObjectFlags NewFlags) { Flags |= NewFlags; }
	inline void ClearFlags(EObjectFlags FlagsToClear) { Flags &= ~FlagsToClear; }
	inline bool HasAnyFlags(EObjectFlags FlagsToCheck) const { return HasFlag(Flags, FlagsToCheck); }
	inline bool HasAllFlags(EObjectFlags FlagsToCheck) const { return (Flags & FlagsToCheck) == FlagsToCheck; }
	inline EObjectFlags GetFlags() const { return Flags; }
	EObjectFlags GetMaskedFlags(EObjectFlags Mask = EObjectFlags::RF_AllFlags) const { return EObjectFlags(GetFlags() & Mask); }

protected:
	// StaticAllocateObject가 채운 값을 그대로 둔다. UE의 NoInit 생성자.
	UObjectBase() : Name(NoInit) {}

private:
	UObjectBase(UClass* InClass, EObjectFlags InFlags, UObject* InOuter, FName InName)
		: ClassPrivate(InClass), Outer(InOuter), Name(InName), Flags(InFlags) {
	}

	// 기본값을 두지 않는다. 있으면 생성자가 할당 때 넣은 값을 덮어쓴다.
	UClass* ClassPrivate;
	UObject* Outer;
	FName Name;
	EObjectFlags Flags;
};

class UObject : public UObjectBase
{
public:
	UObject();
	UObject(bool bRegister);
	virtual ~UObject();

	static UClass* StaticClass();

	template <typename T>
	bool IsA() const { return IsA(T::StaticClass()); }
	bool IsA(const UClass* const Class) const;

	uint32 GetUUID() const { return ObjectUUID; }
	void SetUUID(uint32 Uid) { ObjectUUID = Uid; }

	inline static void RegisterProperties(UClass* InClass) {};
	inline static void RegisterFunctions(UClass* InClass) {};   // 추가

	virtual void Serialize(FArchive& Ar);
	virtual void Serialize(FStructuredArchive::FRecord Record);

	virtual void PostEditChangeProperty(const FProperty& Property) {};

	void* operator new(uint64 Size)
	{
		void* Ptr = malloc(Size);
		if (!Ptr)
			throw std::bad_alloc();

		FEngineStatics::TotalAllocationBytes += static_cast<uint64>(Size);
		FEngineStatics::TotalAllocationCount += 1;
		return Ptr;
	}

	void operator delete(void* Ptr, uint64 Size)
	{
		FEngineStatics::TotalAllocationBytes -= static_cast<uint64>(Size);
		FEngineStatics::TotalAllocationCount -= 1;
		free(Ptr);
	}

	void* operator new(uint64, void* Ptr) { return Ptr; }
	void operator delete(void*, void*) {}

	template <typename T>
	T* CreateDefaultSubobject(FName SubobjectName)
	{
		T* Subobject = NewObject<T>(this, SubobjectName, EObjectFlags::RF_DefaultSubObject);
		OnDefaultSubobjectCreated(Subobject);
		return Subobject;
	}

	virtual void PostDuplicate(bool bDuplicateForPIE) {}
protected:
	virtual void OnDefaultSubobjectCreated(UObject* Subobject) {}
private:
	void SerializeScriptProperties(FStructuredArchive::FSlot Slot);

	uint32 ObjectUUID;
	uint32 InternalIndex;

	bool bIsRegistered = true;
};

extern TArray<UObject*> GUObjectArray;

inline bool IsValid(const UObject* Test)
{
	return Test != nullptr;
}
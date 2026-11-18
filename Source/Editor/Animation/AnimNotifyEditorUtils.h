#pragma once

#include "Animation/AnimNotify.h"
#include "UObject/UObjectHash.h"
#include <algorithm>

namespace AnimNotifyEditorUtils
{
	inline void GetClasses(const UClass* BaseClass, TArray<UClass*>& OutClasses)
	{
		OutClasses.Reset();
		TArray<UClass*> Derived;
		GetDerivedClasses(BaseClass, Derived);
		for (UClass* Class : Derived)
			if (Class && Class->Constructor) OutClasses.Add(Class);
		std::sort(OutClasses.begin(), OutClasses.end(), [](const UClass* A, const UClass* B) { return A->Name < B->Name; });
	}

	inline FString GetDisplayName(const UClass* Class)
	{
		for (const char* Prefix : { "UAnimNotifyState_", "UAnimNotify_" })
		{
			const FString Text = Prefix;
			if (Class->Name.starts_with(Text)) return Class->Name.substr(Text.size());
		}
		return Class->Name;
	}
}

#include "EnginePCH.h"
#include "LuaBindings.h"

#include <sol/sol.hpp>

#include "Components/MovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/ParticleSubUVComponent.h"
#include "Core/EngineTimer.h"
#include "GameFramework/Actor.h"
#include "GameFramework/SpringArmComponent.h"
#include "Input/InputSystem.h"
#include "Script/LuaActorRef.h"
#include "Script/LuaWidgetRef.h"
#include "Script/CoroutineScheduler.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "UI/UserWidget.h"
#include "UI/Button.h"
#include "UI/TextBlock.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "Asset/AssetManager.h"
#include "Components/AudioComponent.h"
#include "Engine/Classes/Sound/SoundWave.h"
#include "Camera/CameraShakeBase.h"

#include "LevelSequence/LevelSequenceActor.h"
#include "LevelSequence/LevelSequencePlayer.h"

#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AnimNotify.h"
#include "Animation/AnimNotifyState.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "UObject/UObjectHash.h"
#include "UObject/Property.h"
#include <memory>

namespace
{
	struct FLuaClickBinding { AActor* Owner; FLuaWidgetRef Button; };
	TArray<FLuaClickBinding> ClickBindings;

	TArray<FLuaActorRef> PendingDestroies;

	AActor* OwnerFromEnv(sol::this_environment& ThisEnv)
	{
		if (!ThisEnv)
			return nullptr;

		sol::environment& Env = ThisEnv;
		sol::optional<FLuaActorRef> Ref = Env["obj"];
		return Ref
			? Ref->Get()
			: nullptr;
	}


	UWorld* WorldFromEnv(sol::this_environment& ThisEnv, const char* FuncName)
	{
		AActor* Owner = OwnerFromEnv(ThisEnv);
		UWorld* World = Owner ? Owner->GetWorld() : nullptr;
		if (!World)
			throw sol::error(FString(FuncName) + " must be called from an actor script.");
		return World;
	}

	static ULevelSequencePlayer* SequencePlayerOf(const FLuaActorRef& Ref)
	{
		ALevelSequenceActor* SeqActor = Cast<ALevelSequenceActor>(&Deref(Ref));
		if (!SeqActor) throw sol::error("Actor is not a LevelSequenceActor.");
		if (!SeqActor->GetSequencePlayer()) SeqActor->InitializePlayer();
		return SeqActor->GetSequencePlayer();
	}

	void RegisterAudio(sol::state& Lua)
	{
		Lua.set_function("PlaySound2D", [](sol::this_environment ThisEnv, const FString& Path,
			sol::optional<float> Volume, sol::optional<float> Pitch)
			{
				if (USoundWave* Sound = UAssetManager::Get().LoadSound(Path))
					UGameplayStatics::PlaySound2D(WorldFromEnv(ThisEnv, "PlaySound2D"), Sound,
						Volume.value_or(1.0f), Pitch.value_or(1.0f));
			});

		Lua.set_function("PlaySoundAtLocation", [](sol::this_environment ThisEnv, const FString& Path,
			const FVector& Location, sol::optional<float> Volume, sol::optional<float> Pitch)
			{
				if (USoundWave* Sound = UAssetManager::Get().LoadSound(Path))
					UGameplayStatics::PlaySoundAtLocation(WorldFromEnv(ThisEnv, "PlaySoundAtLocation"), Sound,
						Location, Volume.value_or(1.0f), Pitch.value_or(1.0f));
			});
	}

	void RegisterGameplay(sol::state& Lua)
	{
		// 실제 전환은 다음 Tick 시작. 호출한 뒤의 코드도 이번 프레임에는 그대로 실행된다.
		Lua.set_function("OpenLevel", [](sol::this_environment ThisEnv, const FString& LevelName) {
			UGameplayStatics::OpenLevel(WorldFromEnv(ThisEnv, "OpenLevel"), LevelName);
			});
		Lua.set_function("SetGameValue", [](sol::this_environment ThisEnv, const FString& Key, double Value) {
			if (UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldFromEnv(ThisEnv, "SetGameValue")))
				GameInstance->SetValue(Key, Value);
			});
		Lua.set_function("GetGameValue", [](sol::this_environment ThisEnv, const FString& Key, sol::optional<double> Default) {
			const double Fallback = Default.value_or(0.0);
			UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldFromEnv(ThisEnv, "GetGameValue"));
			return GameInstance ? GameInstance->GetValue(Key, Fallback) : Fallback;
			});
	}

	void RegisterWidget(sol::state& Lua)
	{
		Lua.new_usertype<FLuaWidgetRef>("Widget", sol::no_constructor,
			"IsValid", [](const FLuaWidgetRef& R) { return R.Get() != nullptr; },
			"Name", sol::property([](const FLuaWidgetRef& R) { return DerefWidget<UWidget>(R).WidgetName; }),
			"Visible", sol::property(
				[](const FLuaWidgetRef& R) { return DerefWidget<UWidget>(R).IsVisible(); },
				[](const FLuaWidgetRef& R, bool b) { DerefWidget<UWidget>(R).Visibility = b ? 0 : 1; }),
			"Find", [](const FLuaWidgetRef& R, const std::string& Name) -> sol::optional<FLuaWidgetRef>
			{
				UWidget* Found = DerefWidget<UUserWidget>(R).FindWidget(Name);
				return Found ? sol::optional(FLuaWidgetRef::Make(Found)) : sol::nullopt;
			},
			"AddToViewport", [](const FLuaWidgetRef& R, sol::optional<int> Z) { DerefWidget<UUserWidget>(R).AddToViewport(Z.value_or(0)); },
			"RemoveFromViewport", [](const FLuaWidgetRef& R) { DerefWidget<UUserWidget>(R).RemoveFromViewport(); },
			"Destroy", [](const FLuaWidgetRef& R) { DerefWidget<UUserWidget>(R).Destroy(); },
			"SetText", [](const FLuaWidgetRef& R, const std::string& T) { DerefWidget<UTextBlock>(R).SetText(T); },
			"BindClick", [](sol::this_environment Env, const FLuaWidgetRef& R, sol::protected_function Fn)
			{
				AActor* Owner = OwnerFromEnv(Env);
				if (!Owner) throw sol::error("BindClick needs an Actor script context.");
				UButton& Button = DerefWidget<UButton>(R);
				const FLuaActorRef OwnerRef = FLuaActorRef::Make(Owner);
				Button.OnClicked = [OwnerRef, Fn]
					{
						if (!OwnerRef.Get()) return;              // 액터가 이미 지워졌다
						sol::protected_function_result Result = Fn();
						if (!Result.valid())
						{
							sol::error Err = Result;
							HTR_LOG(Error, "[Lua] OnClicked: {}", Err.what());
						}
					};
				ClickBindings.Add({ Owner, R });
			});

		Lua.set_function("CreateWidget", [](sol::this_environment Env, const std::string& Path) -> sol::optional<FLuaWidgetRef>
			{
				AActor* Owner = OwnerFromEnv(Env);
				UUserWidget* W = Owner ? ::CreateWidget(Owner->GetWorld(), Path) : nullptr;
				return W ? sol::optional(FLuaWidgetRef::Make(W)) : sol::nullopt;
			});
	}

	// FVector, FRotator
	void RegisterMath(sol::state& Lua)
	{
		Lua.new_usertype<FVector>("FVector",
			sol::call_constructor,
			sol::constructors<FVector(), FVector(float, float, float)>(),
			"x", &FVector::X,
			"y", &FVector::Y,
			"z", &FVector::Z,
			sol::meta_function::addition, [](const FVector& A, const FVector& B) { return A + B; },
			sol::meta_function::subtraction, [](const FVector& A, const FVector& B) { return A - B; },
			sol::meta_function::unary_minus, [](const FVector& A) { return -A; },

			sol::meta_function::multiplication, sol::overload(
				[](const FVector& V, float Scalar) { return V * Scalar; },
				[](const float Scalar, const FVector& V) { return V * Scalar; }
			),
			sol::meta_function::division, [](const FVector& V, float Scalar) { return V / Scalar; },
			sol::meta_function::equal_to, [](const FVector& A, const FVector& B) { return A == B; },
			sol::meta_function::to_string, [](const FVector& V)
			{ return std::format("({:.2f}, {:.2f}, {:.2f})", V.X, V.Y, V.Z); },

			"Length", &FVector::Length,
			"Normalize", [](const FVector& V) { return V.GetSafeNormal(); },
			"Dot", [](const FVector& A, const FVector& B) { return A.Dot(B); },
			"Cross", [](const FVector& A, const FVector& B) { return A.Cross(B); }
		);

		Lua.new_usertype<FRotator>("FRotator",
			sol::call_constructor,
			sol::constructors<FRotator(), FRotator(float, float, float)>(),
			"pitch", &FRotator::Pitch,
			"yaw", &FRotator::Yaw,
			"roll", &FRotator::Roll,

			"LookDirection", &FRotator::LookDirection,
			sol::meta_function::to_string, [](const FRotator& R)
			{ return std::format("(P={:.1f}, Y={:.1f}, R={:.1f})", R.Pitch, R.Yaw, R.Roll); }
		);
	}

	UMovementComponent* FindFirstMovement(AActor& Actor)
	{
		for (UActorComponent* Comp : Actor.GetComponents())
		{
			if (UMovementComponent* MoveComp = Cast<UMovementComponent>(Comp))
			{
				return MoveComp;
			}
		}
		return nullptr;
	}

	UParticleSubUVComponent& ParticleComponent(const FLuaActorRef& Ref)
	{
		for (UActorComponent* Comp : Deref(Ref).GetComponents())
			if (auto* Particle = Cast<UParticleSubUVComponent>(Comp)) return *Particle;
		throw sol::error("Actor does not have a ParticleSubUVComponent.");
	}

	USpringArmComponent& CameraArm(const FLuaActorRef& Ref)
	{
		for (UActorComponent* Comp : Deref(Ref).GetComponents())
			if (auto* Arm = Cast<USpringArmComponent>(Comp)) return *Arm;
		throw sol::error("Actor does not have a SpringArmComponent.");
	}

	void RegisterActor(sol::state& Lua)
	{
		Lua.new_usertype<FLuaActorRef>("Actor",
			sol::no_constructor, // 액터는 Lua에서 생성할 수 없음

			"PlaySequence", [](const FLuaActorRef& Ref) { if (ULevelSequencePlayer* P = SequencePlayerOf(Ref)) P->Play(); },
			"PauseSequence", [](const FLuaActorRef& Ref) { if (ULevelSequencePlayer* P = SequencePlayerOf(Ref)) P->Pause(); },
			"StopSequence", [](const FLuaActorRef& Ref) { if (ULevelSequencePlayer* P = SequencePlayerOf(Ref)) P->Stop(); },
			"IsSequencePlaying", [](const FLuaActorRef& Ref)
			{
				ULevelSequencePlayer* P = SequencePlayerOf(Ref);
				return P && P->IsPlaying();
			},
			"PlayAudio", [](const FLuaActorRef& Ref)
			{
				for (UActorComponent* Component : Deref(Ref).GetComponents())
					if (UAudioComponent* Audio = Cast<UAudioComponent>(Component)) { Audio->Play(); return; }
			},
			"StopAudio", [](const FLuaActorRef& Ref)
			{
				for (UActorComponent* Component : Deref(Ref).GetComponents())
					if (UAudioComponent* Audio = Cast<UAudioComponent>(Component)) { Audio->Stop(); return; }
			},
			"UUID", sol::property([](const FLuaActorRef& Ref) { return Ref.UUID; }),
			"IsValid", [](const FLuaActorRef& Ref) { return Ref.Get() != nullptr; },
			"Name", sol::property([](const FLuaActorRef& Ref) { return Deref(Ref).GetName(); }),
			"HasComponent", [](const FLuaActorRef& Ref, const FString& ClassName)
			{
				for (UActorComponent* Component : Deref(Ref).GetComponents())
				{
					if (!Component || !Component->IsRegistered()) continue;
					for (UClass* Class = Component->GetClass(); Class; Class = Class->Super)
						if (Class->Name == ClassName) return true;
				}
				return false;
			},
			"GetComponentRelativeScale", [](const FLuaActorRef& Ref, const FString& Name) -> sol::optional<FVector>
			{
				for (UActorComponent* Component : Deref(Ref).GetComponents())
					if (Component && Component->GetName() == Name)
						if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
							return SceneComponent->GetRelativeScale3D();
				return sol::nullopt;
			},
			"SetComponentRelativeScale", [](const FLuaActorRef& Ref, const FString& Name, const FVector& Scale)
			{
				for (UActorComponent* Component : Deref(Ref).GetComponents())
					if (Component && Component->GetName() == Name)
						if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
						{
							SceneComponent->SetRelativeScale3D(Scale);
							return true;
						}
				return false;
			},
			"GetOverlappingActors", [](const FLuaActorRef& Ref, sol::this_state State)
			{
				TArray<AActor*> Actors;
				Deref(Ref).GetOverlappingActors(Actors);
				sol::state_view Lua(State);
				sol::table Result = Lua.create_table();
				for (int32 Index = 0; Index < Actors.Num(); ++Index)
					Result[Index + 1] = FLuaActorRef::Make(Actors[Index]);
				return Result;
			},
			"SetParticleTexture", [](const FLuaActorRef& Ref, const FString& Path) { return ParticleComponent(Ref).SetParticleTexture(Path); },
			"SetSubUVSize", [](const FLuaActorRef& Ref, int32 Cols, int32 Rows)
			{
				if (Cols < 1 || Rows < 1 || Cols > 4096 || Rows > 4096)
					throw sol::error("SubUV columns and rows must be between 1 and 4096.");
				ParticleComponent(Ref).SetSubUVSize(Cols, Rows);
			},
			"SetParticleFrameRate", [](const FLuaActorRef& Ref, float FPS) { ParticleComponent(Ref).SetFrameRate(FPS); },
			"PlayParticle", [](const FLuaActorRef& Ref, sol::optional<float> Scale) { ParticleComponent(Ref).PlayOnce(Scale.value_or(1.0f)); },

			"Location", sol::property(
				// Getter
				[](const FLuaActorRef& Ref) { return Deref(Ref).GetActorLocation(); },
				// Setter
				[](const FLuaActorRef& Ref, const FVector& V)
				{
					if (USceneComponent* RootComp = Deref(Ref).GetRootComponent())
					{
						RootComp->SetWorldLocation(V, false);
					}
				}),
			"Rotation", sol::property(
				// Getter
				[](const FLuaActorRef& Ref) { return Deref(Ref).GetActorRotation(); },
				// Setter
				[](const FLuaActorRef& Ref, const FRotator& R)
				{
					if (USceneComponent* RootComp = Deref(Ref).GetRootComponent())
					{
						RootComp->SetWorldRotation(R, false);
					}
				}),
			// Velocity는 MovementComponent가 있는 경우에만 제공
			"Velocity", sol::property(
				// Getter
				[](const FLuaActorRef& Ref)
				{
					if (UMovementComponent* MoveComp = FindFirstMovement(Deref(Ref)))
					{
						return MoveComp->Velocity;
					}
					return FVector::ZeroVector;
				},
				// Setter
				[](const FLuaActorRef& Ref, const FVector& V)
				{
					if (UMovementComponent* MoveComp = FindFirstMovement(Deref(Ref)))
					{
						MoveComp->Velocity = V;
					}
					else
					{
						throw sol::error("Actor does not have a MovementComponent to set Velocity.");
					}
				}),
			"PrintLocation", [](const FLuaActorRef& Ref)
			{
				const FVector Location = Deref(Ref).GetActorLocation();
				HTR_LOG(Info, "[Lua] {} ({:.2f}, {:.2f}, {:.2f})", Deref(Ref).GetName(), Location.X, Location.Y, Location.Z);
			},

			"Destroy", [](const FLuaActorRef& Ref)
			{
				// 이미 파괴되지 않은 액터만 PendingDestroies에 추가
				if (Ref.Get())
				{
					PendingDestroies.Add(Ref);
				}
			},

			sol::meta_function::equal_to, [](const FLuaActorRef& A, const FLuaActorRef& B)
			{
				return A.UUID == B.UUID;
			},
			sol::meta_function::to_string, [](const FLuaActorRef& Ref)
			{
				return std::format("Actor#{}", Ref.UUID);
			},

			"CameraLagEnabled", sol::property(
				[](const FLuaActorRef& Ref)
				{
					return CameraArm(Ref).bEnableCameraLag;
				},
				[](const FLuaActorRef& Ref, bool b)
				{
					CameraArm(Ref).bEnableCameraLag = b;
				}
			),
			"ResetCameraLag", [](const FLuaActorRef& Ref)
			{
				CameraArm(Ref).ResetCameraLag();
			}
		);

		Lua.set_function("GetActorByName", [](sol::this_environment Env, const std::string& Name) -> sol::optional<FLuaActorRef>
			{
				AActor* Owner = OwnerFromEnv(Env);
				if (!Owner) throw sol::error("GetActorByName needs an Actor script context.");
				UWorld* World = Owner->GetWorld();
				if (!World) throw sol::error("GetActorByName needs an Actor script context.");
				for (AActor* Actor : World->GetCurrentLevel()->GetActors())
				{
					if (Actor && Actor->GetName() == Name)
					{
						return FLuaActorRef::Make(Actor);
					}
				}
				return sol::nullopt;
			});
		Lua.set_function("GetComponentByName", [](sol::this_environment Env, const std::string& Name) -> sol::optional<FLuaComponentRef>
			{
				AActor* Owner = OwnerFromEnv(Env);
				if (!Owner) throw sol::error("GetComponentByName needs an Actor script context.");
				for (UActorComponent* Component : Owner->GetComponents())
				{
					if (Component && Component->GetName() == Name)
					{
						return FLuaComponentRef::Make(Component);
					}
				}
				return sol::nullopt;
			});

		Lua.set_function("SetViewTargetWithBlend",
			[](sol::this_environment Env,
				const FLuaActorRef& Target,
				sol::optional<float> Time,
				sol::optional<std::string> Function,
				sol::optional<float> Exp) -> bool
			{
				AActor* Owner = OwnerFromEnv(Env);
				UWorld* World = Owner ? Owner->GetWorld() : nullptr;
				APlayerController* PC = World ? World->GetPlayerController() : nullptr;
				APlayerCameraManager* PCM = PC ? PC->GetCameraManager() : nullptr;

				AActor* TargetActor = Target.Get();
				if (!PCM || !TargetActor || TargetActor->GetWorld() != World)
					return false;

				FViewTargetTransitionParams Params;
				Params.BlendTime = Time.value_or(0.0f);
				Params.BlendExp = Exp.value_or(2.0f);

				const std::string Mode = Function.value_or("Cubic");

				if (Mode == "Linear")
					Params.BlendFunction = EViewTargetBlendFunction::Linear;
				else if (Mode == "EaseIn")
					Params.BlendFunction = EViewTargetBlendFunction::EaseIn;
				else if (Mode == "EaseOut")
					Params.BlendFunction = EViewTargetBlendFunction::EaseOut;
				else if (Mode == "EaseInOut")
					Params.BlendFunction = EViewTargetBlendFunction::EaseInOut;
				else if (Mode != "Cubic")
					return false;

				PCM->SetViewTarget(TargetActor, Params);
				return true;
			});

		Lua.set_function("IsCameraBlending",
			[](sol::this_environment Env) -> bool
			{
				AActor* Owner = OwnerFromEnv(Env);
				UWorld* World = Owner ? Owner->GetWorld() : nullptr;
				APlayerController* PC = World ? World->GetPlayerController() : nullptr;
				APlayerCameraManager* PCM = PC ? PC->GetCameraManager() : nullptr;

				if (!PCM)
					throw sol::error("IsCameraBlending: camera manager is unavailable.");

				return PCM->IsBlending();
			});
		Lua.set_function("SetCameraRelativeTransform",
			[](const FLuaActorRef& Ref,
				const FVector& RelativeLocation,
				const FRotator& RelativeRotation)
			{
				AActor& Actor = Deref(Ref);

				for (UActorComponent* Component : Actor.GetComponents())
				{
					if (UCameraComponent* Camera = Cast<UCameraComponent>(Component))
					{
						Camera->SetRelativeLocationAndRotation(
							RelativeLocation,
							RelativeRotation
						);
						return;
					}
				}

				throw sol::error(
					"SetCameraRelativeTransform: Actor has no camera component."
				);
			});
	}

	void RegisterComponent(sol::state& Lua)
	{
		Lua.new_usertype<FLuaComponentRef>("Component",
			sol::no_constructor,
			"IsValid", [](const FLuaComponentRef& R) { return R.Get() != nullptr; },
			"Name", sol::property([](const FLuaComponentRef& R) { return Deref(R).GetName(); }),
			"Owner", sol::property([](const FLuaComponentRef& R) { return FLuaActorRef::Make(Deref(R).GetOwner()); }),
			"Location", sol::property(
				// Getter
				[](const FLuaComponentRef& Ref)
				{
					if (auto* SceneComp = Cast<USceneComponent>(&Deref(Ref)))
					{
						return SceneComp->GetRelativeLocation();
					}
					else
					{
						throw sol::error("Component is not a scene component.");
					}
				},
				// Setter
				[](const FLuaComponentRef& Ref, const FVector& V)
				{
					if (auto* SceneComp = Cast<USceneComponent>(&Deref(Ref)))
					{
						SceneComp->SetRelativeLocation(V);
					}
					else
					{
						throw sol::error("Component is not a scene component.");
					}
				}),
			"Rotation", sol::property(
				// Getter
				[](const FLuaComponentRef& Ref) {
					if (auto* SceneComp = Cast<USceneComponent>(&Deref(Ref)))
					{
						return SceneComp->GetRelativeRotation();
					}
					else
					{
						throw sol::error("Component is not a scene component.");
					}
				},
				// Setter
				[](const FLuaComponentRef& Ref, const FRotator& R)
				{
					if (auto* SceneComp = Cast<USceneComponent>(&Deref(Ref)))
					{
						SceneComp->SetRelativeRotation(R);
					}
					else
					{
						throw sol::error("Component is not a scene component.");
					}
				})
		);
	}

	EKeyCode ToKeyCode(const std::string& Name)
	{
		if (Name.size() == 1 && std::isalnum(static_cast<unsigned char>(Name[0])))
		{
			// 알파벳 또는 숫자 키를 EKeyCode로 변환
			return static_cast<EKeyCode>(std::toupper(static_cast<unsigned char>(Name[0])));
		}

		static const TMap<std::string, EKeyCode> KeyMap =
		{
			{ "space", EKeyCode::Space },
			{ "enter", EKeyCode::Enter },
			{ "escape", EKeyCode::Escape },
			{ "tab", EKeyCode::Tab },
			{ "shift", EKeyCode::Shift },
			{ "backspace", EKeyCode::Backspace },
			{ "ctrl", EKeyCode::Control },
			{ "alt", EKeyCode::Alt },
			{ "left", EKeyCode::Left },
			{ "right", EKeyCode::Right },
			{ "up", EKeyCode::Up },
			{ "down", EKeyCode::Down },
		};

		std::string Lower = Name;
		std::transform(Lower.begin(), Lower.end(), Lower.begin(), [](unsigned char c) { return std::tolower(c); });
		if (const EKeyCode* Code = KeyMap.Find(Lower))
		{
			return *Code;
		}
		throw sol::error("Unknown key name: " + Name);
	}

	void RegisterInput(sol::state& Lua)
	{
		Lua.set_function("IsKeyDown", [](const std::string& K) {
			return FInputSystem::IsKeyDown(ToKeyCode(K));
			});
		Lua.set_function("IsKeyPressed", [](const std::string& K) {
			return FInputSystem::IsKeyPressed(ToKeyCode(K));
			});
		Lua.set_function("IsKeyReleased", [](const std::string& K) {
			return FInputSystem::IsKeyReleased(ToKeyCode(K));
			});

		Lua.set_function("IsLeftMouseButtonPressed", []() {
			return FInputSystem::IsMousePressed(EMouseButton::Left);
			});

		Lua.set_function("IsRightMouseButtonPressed", []() {
			return FInputSystem::IsMousePressed(EMouseButton::Right);
			});

		// Viewport 포지션을 반환한다.
		Lua.set_function("GetMousePosition", [](sol::this_environment ThisEnv) {
			AActor* Owner = OwnerFromEnv(ThisEnv);
			if (!Owner)
			{
				throw sol::error("GetMousePosition must be called from a coroutine with an Actor context.");
			}

			UWorld* World = Owner->GetWorld();
			if (!World)
			{
				throw sol::error("GetMousePosition must be called from a coroutine with an Actor context.");
			}

			FWorldContext* Context =
				GEngine ? GEngine->GetWorldContextFromWorld(World) : nullptr;

			if (!Context || !Context->GameViewport)
			{
				throw sol::error("GetMousePosition must be called from a coroutine with an Actor context.");
			}

			FGameViewportClient* Viewport = Context->GameViewport.get();

			const FRect& ViewportRect = Viewport->GetRect();

			//const float MouseX = FInputSystem::GetMouseX();
			//const float MouseY = FInputSystem::GetMouseY();
			POINT Mouse{};
			if (!::GetCursorPos(&Mouse))
			{
				throw sol::error("Failed to get mouse cursor position.");
			}

			const float LocalX = static_cast<float>(Mouse.x) - ViewportRect.X;
			const float LocalY = static_cast<float>(Mouse.y) - ViewportRect.Y;

			return std::make_tuple(
				LocalX,
				LocalY);
			});
		Lua.set_function("GetViewportSize",
			[](sol::this_environment ThisEnv) -> std::tuple<float, float>
			{
				AActor* Owner = OwnerFromEnv(ThisEnv);
				if (!Owner)
				{
					throw sol::error("GetMousePosition must be called from a coroutine with an Actor context.");
				}

				UWorld* World = Owner->GetWorld();
				if (!World)
				{
					throw sol::error("GetMousePosition must be called from a coroutine with an Actor context.");
				}

				FWorldContext* Context =
					GEngine ? GEngine->GetWorldContextFromWorld(World) : nullptr;

				if (!Context || !Context->GameViewport)
				{
					throw sol::error("GetMousePosition must be called from a coroutine with an Actor context.");
				}

				FGameViewportClient* Viewport = Context->GameViewport.get();

				const FRect& ViewportRect = Viewport->GetRect();

				return  std::make_tuple(
					static_cast<float>(ViewportRect.Width),
					static_cast<float>(ViewportRect.Height));
			}
		);

		Lua.set_function("ScreenToWorldRay",
			[](sol::this_environment Env, float X, float Y)
			-> std::tuple<sol::optional<FVector>, sol::optional<FVector>>
			{
				AActor* Owner = OwnerFromEnv(Env);
				UWorld* World = Owner ? Owner->GetWorld() : nullptr;

				if (!GEngine || !World)
				{
					return {};
				}

				FWorldContext* Context =
					GEngine->GetWorldContextFromWorld(World);

				if (!Context || !Context->GameViewport)
				{
					return {};
				}

				FRay Ray;
				if (!Context->GameViewport->ScreenToWorldRay(X, Y, &Ray))
				{
					return {};
				}

				return {
					sol::optional<FVector>(Ray.Origin),
					sol::optional<FVector>(Ray.Direction)
				};
			}
		);

		Lua.set_function("Raycast",
			[](sol::this_environment Env,
				const FVector& Origin,
				const FVector& Direction) -> sol::optional<FVector>
			{
				AActor* Owner = OwnerFromEnv(Env);
				UWorld* World = Owner ? Owner->GetWorld() : nullptr;

				if (!World || Direction.Dot(Direction) <= 0.0000001f)
				{
					return sol::nullopt;
				}

				FRay Ray{
					Origin,
					Direction.Normalized()
				};

				FHitResult Hit;
				if (!World->LineTraceSingle(Ray, Hit))
				{
					return sol::nullopt;
				}

				return Hit.ImpactPoint;
			});

		// Tick(dt)의 dt를 쓰는 게 원칙. 코루틴처럼 dt를 못 받는 곳에서만 쓴다
		Lua.set_function("GetDeltaTime", []() {
			return EngineTimer::GetDeltaTime();
			});
		Lua.set_function("GetTime", []() {
			return EngineTimer::GetTotalTime();
			});
	}

	void RegisterCoroutine(sol::state& Lua)
	{
		Lua.set_function("StartCoroutine",
			[](sol::this_environment ThisEnv, sol::function Fn)
			{
				UWorld* World = WorldFromEnv(ThisEnv, "StartCoroutine");
				World->GetCoroutineScheduler().Start(OwnerFromEnv(ThisEnv), Fn);
			});

		// C++ 함수가 반환값을 yield 값으로 남기고 멈춘다. 코루틴 밖에서 부르면 Lua 에러
		Lua.set_function("wait", sol::yielding(
			[](float Seconds)
			{
				return std::make_tuple("time", Seconds);
			}));
		Lua.set_function("wait_frame", sol::yielding(
			[]()
			{
				return;
			}));
		Lua.set_function("wait_until", sol::yielding(
			[](sol::function Cond)
			{
				return std::make_tuple("until", Cond);
			}));
		Lua.set_function("wait_until_move_done", sol::yielding(
			[]()
			{
				return "move";
			}));

		Lua.set_function("move_to",
			[](sol::this_environment ThisEnv, const FVector& Target, float Speed)
			{
				UWorld* World = WorldFromEnv(ThisEnv, "move_to");
				World->GetCoroutineScheduler().MoveTo(OwnerFromEnv(ThisEnv), Target, Speed);
			});
	}

	void RegisterCameraAction(sol::state& Lua)
	{
		Lua.set_function("StartHitStop",
			[](sol::this_environment Env, float Duration, float Dilation)
			{
				WorldFromEnv(Env, "StartHitStop")->StartHitStop(Duration, Dilation);
			});

		Lua.set_function("StartCameraShake",
			[](sol::this_environment Env, float Scale) -> bool
			{
				UWorld* World = WorldFromEnv(Env, "StartCameraShake");
				APlayerController* PC = World->GetPlayerController();
				APlayerCameraManager* PCM = PC ? PC->GetCameraManager() : nullptr;

				if (!PCM) return false;

				UClass* ShakeClass = UCameraShakeBase::StaticClass();
				auto* Settings = static_cast<UCameraShakeBase*>(
					ShakeClass->GetDefaultObject());

				if (!Settings) return false;

				// Lua에서는 Scale로 세기를 조절한다.
				Settings->Duration = 1.0f;
				Settings->BlendInTime = 0.05f;
				Settings->BlendOutTime = 0.3f;
				Settings->RotationPitch.Amplitude = 2.0f;
				Settings->RotationPitch.Frequency = 10.0f;
				Settings->RotationYaw.Amplitude = 1.0f;
				Settings->RotationYaw.Frequency = 13.0f;

				return PCM->StartCameraShake(ShakeClass, Scale) != nullptr;
			});

		Lua.set_function("StopAllCameraShakes",
			[](sol::this_environment Env, bool bImmediately)
			{
				UWorld* World = WorldFromEnv(Env, "StopAllCameraShakes");
				APlayerController* PC = World->GetPlayerController();
				APlayerCameraManager* PCM = PC ? PC->GetCameraManager() : nullptr;

				if (PCM)
				{
					PCM->StopAllCameraShakes(bImmediately);
				}
			});
	}

	// ======================== Animation ========================
	// Lua는 UAnimGraphInstance를 조립한다. 같은 API를 이후 AnimGraph 노드 에디터가 쓴다.
	// 모든 핸들은 Generation을 들고 있어, ResetGraph 뒤의 오래된 핸들은 에러를 낸다.

	struct FLuaAnimGraphRef
	{
		FLuaComponentRef Mesh;
		UAnimGraphInstance* Ptr = nullptr;
		uint32 Generation = 0;

		static FLuaAnimGraphRef Make(USkeletalMeshComponent* MeshComp, UAnimGraphInstance* Graph)
		{
			return { FLuaComponentRef::Make(MeshComp), Graph, Graph ? Graph->GetGeneration() : 0 };
		}

		// 메시가 살아 있고, 여전히 이 그래프를 쓰고 있고, 리셋되지 않았을 때만 유효
		UAnimGraphInstance* Get() const
		{
			USkeletalMeshComponent* MeshComp = Cast<USkeletalMeshComponent>(Mesh.Get());
			if (!MeshComp || !Ptr || MeshComp->GetAnimInstance() != Ptr || Ptr->GetGeneration() != Generation)
				return nullptr;
			return Ptr;
		}
	};

	struct FLuaAnimSequenceRef { FLuaAnimGraphRef Graph; UAnimSequence* Ptr = nullptr; };
	struct FLuaBlendSpaceRef { FLuaAnimGraphRef Graph; UBlendSpace1D* Ptr = nullptr; };
	struct FLuaAnimNodeRef { FLuaAnimGraphRef Graph; int32 Index = INDEX_NONE; };

	//  Lua가 만든 그래프. ReleaseCallbacks에서 리셋해 Lua 함수를 잡은 규칙/콜백을 놓는다.
	struct FLuaAnimGraphBinding { AActor* Owner; FLuaAnimGraphRef Graph; };
	TArray<FLuaAnimGraphBinding> AnimGraphBindings;

    void TrackAnimGraph(AActor& Owner, const FLuaAnimGraphRef& Ref)
    {
        for (int32 Index = AnimGraphBindings.Num() - 1; Index >= 0; --Index)
        {
            if (AnimGraphBindings[Index].Owner != &Owner)
            {
                continue;
            }

            UAnimGraphInstance* Existing = AnimGraphBindings[Index].Graph.Get();

            if (!Existing)
            {
                AnimGraphBindings.RemoveAt(Index);
            }
            else if (Existing == Ref.Get())
            {
                return;
            }
        }

        AnimGraphBindings.Add({ &Owner, Ref });
    }


	UAnimGraphInstance& DerefGraph(const FLuaAnimGraphRef& Ref)
	{
		if (UAnimGraphInstance* Graph = Ref.Get()) return *Graph;
		throw sol::error("AnimGraph is no longer valid (reset, replaced, or mesh destroyed).");
	}

    UAnimGraphInstance& DerefEditableGraph(const FLuaAnimGraphRef& Ref)
    {
        USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(Ref.Mesh.Get());

        if (Component && !Component->GetAnimBlueprintPath().empty())
        {
            throw sol::error("Edit Animation Blueprint graph structure in its editor. Lua can update variables and bind named notifications.");
        }

        return DerefGraph(Ref);
    }

	UAnimSequence& DerefSequence(const FLuaAnimSequenceRef& Ref)
	{
		UAnimGraphInstance& Graph = DerefGraph(Ref.Graph);
		if (!Graph.OwnsSequence(Ref.Ptr)) throw sol::error("AnimSequence is no longer valid.");
		return *Ref.Ptr;
	}

	UBlendSpace1D& DerefBlendSpace(const FLuaBlendSpaceRef& Ref)
	{
		UAnimGraphInstance& Graph = DerefGraph(Ref.Graph);
		if (!Graph.OwnsBlendSpace(Ref.Ptr)) throw sol::error("BlendSpace is no longer valid.");
		return *Ref.Ptr;
	}

	// 노드가 이 그래프 소속인지 확인하고 Index를 돌려준다
	int32 NodeIndexIn(const UAnimGraphInstance& Graph, const FLuaAnimNodeRef& Node)
	{
		if (Node.Graph.Ptr != &Graph || Node.Graph.Generation != Graph.GetGeneration() || !Graph.IsValidNode(Node.Index))
			throw sol::error("AnimNode does not belong to this AnimGraph.");
		return Node.Index;
	}

	USkeletalMeshComponent& FirstSkeletalMesh(const FLuaActorRef& Ref)
	{
		for (UActorComponent* Component : Deref(Ref).GetComponents())
			if (auto* MeshComp = Cast<USkeletalMeshComponent>(Component)) return *MeshComp;
		throw sol::error("Actor does not have a SkeletalMeshComponent.");
	}

	// Lua 함수를 애니메이션 콜백으로 감싼다. 에러가 나면 한 번만 로그를 남기고 그 뒤로는 부르지 않는다 (매 프레임 로그 폭주 방지).
	template <typename TResult, typename... TArgs>
	sol::optional<TResult> CallAnimLua(const sol::protected_function& Fn, const std::shared_ptr<bool>& bBroken, const char* What, TArgs&&... Args)
	{
		if (*bBroken) return sol::nullopt;
		sol::protected_function_result Result = Fn(std::forward<TArgs>(Args)...);
		if (!Result.valid())
		{
			sol::error Err = Result;
			HTR_LOG(Error, "[Lua] AnimGraph {} : {} (disabled until the graph is rebuilt)", What, Err.what());
			*bBroken = true;
			return sol::nullopt;
		}
		return Result.get<sol::optional<TResult>>();
	}

	template <typename TUnused, typename... TArgs>
	void CallAnimLuaVoid(const sol::protected_function& Fn, const std::shared_ptr<bool>& bBroken, const char* What, TArgs&&... Args)
	{
		if (*bBroken) return;
		sol::protected_function_result Result = Fn(std::forward<TArgs>(Args)...);
		if (!Result.valid())
		{
			sol::error Err = Result;
			HTR_LOG(Error, "[Lua] AnimGraph {} : {} (disabled until the graph is rebuilt)", What, Err.what());
			*bBroken = true;
		}
	}

	EAnimGraphCompare ParseCompare(const std::string& Op)
	{
		if (Op == "<")  return EAnimGraphCompare::Less;
		if (Op == "<=") return EAnimGraphCompare::LessEqual;
		if (Op == ">")  return EAnimGraphCompare::Greater;
		if (Op == ">=") return EAnimGraphCompare::GreaterEqual;
		if (Op == "==") return EAnimGraphCompare::Equal;
		if (Op == "~=" || Op == "!=") return EAnimGraphCompare::NotEqual;
		throw sol::error("Unknown compare operator: " + Op);
	}

	// 전환 규칙: Lua 함수 또는 데이터 테이블
	//   function(anim, elapsed) return bool end
	//   { Bool = "IsAttacking" [, Value = true] }
	//   { Float = "GroundSpeed", Op = ">", Value = 10 }
	//   { Finished = true }   -- From 상태의 SequencePlayer가 끝까지 재생됐을 때
	FAnimTransitionRule ParseTransitionRule(UAnimGraphInstance& Graph, const FLuaAnimGraphRef& GraphRef,
		int32 MachineNode, int32 FromState, const sol::object& RuleObject)
	{
		if (RuleObject.get_type() == sol::type::function)
		{
			sol::protected_function Fn = RuleObject;
			auto bBroken = std::make_shared<bool>(false);
			return [Fn, bBroken, GraphRef](const FAnimTransitionContext& Context)
				{
					return CallAnimLua<bool>(Fn, bBroken, "transition rule", GraphRef, Context.StateElapsedTime).value_or(false);
				};
		}

		if (RuleObject.get_type() != sol::type::table)
			throw sol::error("Transition rule must be a function or a table.");

		sol::table Rule = RuleObject;
		if (sol::optional<std::string> Var = Rule["Bool"])
			return Graph.MakeBoolRule(*Var, Rule.get_or("Value", true));

		if (sol::optional<std::string> Var = Rule["Float"])
		{
			const std::string Op = Rule.get_or<std::string>("Op", ">");
			return Graph.MakeFloatRule(*Var, ParseCompare(Op), Rule.get_or("Value", 0.0f));
		}

		if (Rule.get_or("Finished", false))
		{
			const int32 PoseNode = Graph.GetStatePoseNode(MachineNode, FromState);
			if (!Graph.GetNode<FAnimNode_SequencePlayer>(PoseNode))
				throw sol::error("{ Finished = true } needs a From state whose pose is a SequencePlayer.");
			return Graph.MakeFinishedRule(PoseNode);
		}

		throw sol::error("Transition rule table needs Bool, Float, or Finished.");
	}

	// 상태 콜백: Lua 함수 또는 { Bool = "IsAttacking", Value = false }
	UAnimGraphInstance::FStateCallback ParseStateCallback(UAnimGraphInstance& Graph, const FLuaAnimGraphRef& GraphRef,
		const sol::object& Object, const char* What)
	{
		if (!Object.valid() || Object.get_type() == sol::type::lua_nil)
			return {};

		if (Object.get_type() == sol::type::function)
		{
			sol::protected_function Fn = Object;
			auto bBroken = std::make_shared<bool>(false);
			return [Fn, bBroken, GraphRef, What](UAnimInstance*)
				{
					CallAnimLuaVoid<void>(Fn, bBroken, What, GraphRef);
				};
		}

		if (Object.get_type() == sol::type::table)
		{
			sol::table Action = Object;
			if (sol::optional<std::string> Var = Action["Bool"])
				return Graph.MakeSetBoolAction(*Var, Action.get_or("Value", false));
		}

		throw sol::error(FString(What) + " must be a function or { Bool = name, Value = bool }.");
	}

	UAnimGraphInstance::FNotifyStateCallback WrapNotifyStateCallback(const sol::object& Object, const FLuaAnimGraphRef& GraphRef, const char* What)
	{
		if (Object.get_type() != sol::type::function)
			return {};
		sol::protected_function Fn = Object;
		auto bBroken = std::make_shared<bool>(false);
		return [Fn, bBroken, GraphRef, What](float Value)
			{
				CallAnimLuaVoid<void>(Fn, bBroken, What, GraphRef, Value);
			};
	}

	// 리플렉션(UPROPERTY)으로 Notify 객체의 파라미터를 채운다. 노드 에디터의 디테일 패널과 같은 경로다.
	void ApplyReflectedProperties(UObject* Object, const sol::table& Props)
	{
		UClass* Class = Object->GetClass();
		for (const auto& [Key, Value] : Props)
		{
			if (Key.get_type() != sol::type::string)
				continue;

			const std::string Name = Key.as<std::string>();
			FProperty* Property = Class->FindPropertyByName(Name);
			if (!Property)
				throw sol::error(Class->Name + " has no property '" + Name + "'.");

			void* Address = Property->ContainerPtrToValuePtr<void>(Object);
			if (CastField<FFloatProperty>(Property))       *static_cast<float*>(Address) = Value.as<float>();
			else if (CastField<FIntProperty>(Property))    *static_cast<int32*>(Address) = Value.as<int32>();
			else if (CastField<FBoolProperty>(Property))   *static_cast<bool*>(Address) = Value.as<bool>();
			else if (CastField<FStrProperty>(Property))    *static_cast<FString*>(Address) = Value.as<std::string>();
			else if (CastField<FVectorProperty>(Property)) *static_cast<FVector*>(Address) = Value.as<FVector>();
			else throw sol::error("Property '" + Name + "' has a type Lua cannot set yet.");
		}
	}

	UClass* FindNotifyClass(const std::string& ClassName)
	{
		UClass* Class = FindClass(ClassName);
		if (!Class) Class = FindClass("U" + ClassName);   // "AnimNotify_PlaySound"처럼 접두사를 빼도 찾는다
		if (!Class) throw sol::error("Unknown notify class: " + ClassName);
		return Class;
	}

	FLuaAnimNodeRef MakeNodeRef(const FLuaAnimGraphRef& GraphRef, int32 Index, const char* What)
	{
		if (Index == INDEX_NONE) throw sol::error(FString(What) + " failed.");
		return { GraphRef, Index };
	}

	// AnimGraph 핸들을 다시 만든다 (Generation이 바뀐 뒤에도 같은 메시를 가리키도록)
	FLuaAnimGraphRef RefOf(const FLuaAnimGraphRef& Ref, UAnimGraphInstance& Graph)
	{
		return { Ref.Mesh, &Graph, Graph.GetGeneration() };
	}

	void RegisterAnimation(sol::state& Lua)
	{
		Lua.new_usertype<FLuaAnimSequenceRef>("AnimSequence", sol::no_constructor,
			"IsValid", [](const FLuaAnimSequenceRef& R) { return R.Graph.Get() && R.Graph.Get()->OwnsSequence(R.Ptr); },
			"Length", sol::property([](const FLuaAnimSequenceRef& R)
				{
					const UAnimDataModel* Model = DerefSequence(R).GetDataModel();
					return Model ? Model->GetPlayLength() : 0.0f;
				}),
			"FrameRate", sol::property([](const FLuaAnimSequenceRef& R)
				{
					const UAnimDataModel* Model = DerefSequence(R).GetDataModel();
					if (!Model) return 0.0f;
					const FFrameRate Rate = Model->GetFrameRate();
					return Rate.Denominator ? static_cast<float>(Rate.Numerator) / static_cast<float>(Rate.Denominator) : 0.0f;
				}),
			"FrameToTime", [](const FLuaAnimSequenceRef& R, float Frame)
				{
					const UAnimDataModel* Model = DerefSequence(R).GetDataModel();
					if (!Model) return 0.0f;
					const FFrameRate Rate = Model->GetFrameRate();
					return Rate.Numerator ? Frame * static_cast<float>(Rate.Denominator) / static_cast<float>(Rate.Numerator) : 0.0f;
				},
			// 이름 Notify. Duration > 0이면 NotifyState가 되어 OnNotifyState의 Begin/Tick/End를 받는다.
			"AddNotify", [](const FLuaAnimSequenceRef& R, float Time, const std::string& Name,
				sol::optional<float> Duration, sol::optional<float> WeightThreshold)
				{
					UAnimSequence& Sequence = DerefSequence(R);
					FGuid Guid;
					if (Duration.value_or(0.0f) > 0.0f)
					{
						auto* State = Sequence.AddNotify<UAnimNotifyState_Named>(Time, *Duration, &Guid);
						State->NotifyName = Name;
					}
					else
					{
						Guid = Sequence.AddNotify(Time, FName(Name));
					}
					if (WeightThreshold) Sequence.SetNotifyTriggerWeightThreshold(Guid, *WeightThreshold);
				},
			// 클래스 Notify. Props는 UPROPERTY 이름 = 값, Options는 { Duration =, WeightThreshold = }
			"AddNotifyClass", [](const FLuaAnimSequenceRef& R, const std::string& ClassName, float Time,
				sol::optional<sol::table> Props, sol::optional<sol::table> Options)
				{
					UAnimSequence& Sequence = DerefSequence(R);
					const float Duration = Options ? Options->get_or("Duration", 0.0f) : 0.0f;

					FGuid Guid;
					UObject* Notify = Sequence.AddNotifyOfClass(FindNotifyClass(ClassName), Time, Duration, &Guid);
					if (!Notify) throw sol::error(ClassName + " is not a UAnimNotify or UAnimNotifyState class.");

					if (Props) ApplyReflectedProperties(Notify, *Props);
					if (Options)
						if (sol::optional<float> Threshold = (*Options)["WeightThreshold"])
							Sequence.SetNotifyTriggerWeightThreshold(Guid, *Threshold);
				},
			"AddPlaySound", [](const FLuaAnimSequenceRef& R, float Time, const std::string& SoundPath,
				sol::optional<float> Volume, sol::optional<float> WeightThreshold)
				{
					UAnimSequence& Sequence = DerefSequence(R);
					FGuid Guid;
					auto* Notify = Sequence.AddNotify<UAnimNotify_PlaySound>(Time, 0.0f, &Guid);
					Notify->SoundPath = SoundPath;
					Notify->Volume = Volume.value_or(1.0f);
					if (WeightThreshold) Sequence.SetNotifyTriggerWeightThreshold(Guid, *WeightThreshold);
				},
			"NumNotifies", sol::property([](const FLuaAnimSequenceRef& R) { return DerefSequence(R).GetNotifies().Num(); })
		);

		Lua.new_usertype<FLuaBlendSpaceRef>("BlendSpace", sol::no_constructor,
			"IsValid", [](const FLuaBlendSpaceRef& R) { return R.Graph.Get() && R.Graph.Get()->OwnsBlendSpace(R.Ptr); },
			"AddSample", [](const FLuaBlendSpaceRef& R, const FLuaAnimSequenceRef& Sequence, float X)
				{
					return DerefBlendSpace(R).AddSample(&DerefSequence(Sequence), FVector(X, 0.0f, 0.0f));
				},
			"NumSamples", sol::property([](const FLuaBlendSpaceRef& R) { return DerefBlendSpace(R).GetSampleData().Num(); })
		);

		Lua.new_usertype<FLuaAnimNodeRef>("AnimNode", sol::no_constructor,
			"Index", sol::property([](const FLuaAnimNodeRef& R) { return R.Index; }),
			"IsValid", [](const FLuaAnimNodeRef& R) { UAnimGraphInstance* G = R.Graph.Get(); return G && G->IsValidNode(R.Index); },

			// ---- SequencePlayer ----
			"IsFinished", sol::property([](const FLuaAnimNodeRef& R)
				{
					auto* Player = DerefGraph(R.Graph).GetNode<FAnimNode_SequencePlayer>(R.Index);
					if (!Player) throw sol::error("IsFinished is only for SequencePlayer nodes.");
					return Player->IsFinished();
				}),

			// ---- StateMachine ----
			// 반환값은 상태 Id (AddTransition / SetInitialState에 그대로 넘긴다)
			"AddState", [](const FLuaAnimNodeRef& R, const std::string& Name, const FLuaAnimNodeRef& Pose, sol::optional<sol::table> Options)
				{
					UAnimGraphInstance& Graph = DerefGraph(R.Graph);
					const int32 Machine = NodeIndexIn(Graph, R);
					const int32 PoseNode = NodeIndexIn(Graph, Pose);

					bool bResetOnEntry = true;
					UAnimGraphInstance::FStateCallback OnEnter, OnExit;
					if (Options)
					{
						bResetOnEntry = Options->get_or("ResetOnEntry", true);
						OnEnter = ParseStateCallback(Graph, R.Graph, (*Options)["OnEnter"], "OnEnter");
						OnExit = ParseStateCallback(Graph, R.Graph, (*Options)["OnExit"], "OnExit");
					}

					const int32 State = Graph.AddState(Machine, FName(Name), PoseNode, bResetOnEntry, std::move(OnEnter), std::move(OnExit));
					if (State == INDEX_NONE) throw sol::error("AddState failed (is this a StateMachine node?).");
					return State;
				},
			// From이 nil이면 Any State. Options = { Crossfade =, Priority = }
			"AddTransition", [](const FLuaAnimNodeRef& R, sol::object From, int32 To, sol::object Rule, sol::optional<sol::table> Options)
				{
					UAnimGraphInstance& Graph = DerefGraph(R.Graph);
					const int32 Machine = NodeIndexIn(Graph, R);
					const int32 FromState = (From.get_type() == sol::type::number) ? From.as<int32>() : INDEX_NONE;

					FAnimTransitionRule TransitionRule = ParseTransitionRule(Graph, R.Graph, Machine, FromState, Rule);
					const float Crossfade = Options ? Options->get_or("Crossfade", 0.2f) : 0.2f;
					const int32 Priority = Options ? Options->get_or("Priority", 0) : 0;

					if (Graph.AddTransition(Machine, FromState, To, std::move(TransitionRule), Crossfade, Priority) == INDEX_NONE)
						throw sol::error("AddTransition failed (check state ids).");
				},
			"SetInitialState", [](const FLuaAnimNodeRef& R, int32 State)
				{
					UAnimGraphInstance& Graph = DerefGraph(R.Graph);
					if (!Graph.SetInitialState(NodeIndexIn(Graph, R), State))
						throw sol::error("SetInitialState failed.");
				},
			"CurrentState", sol::property([](const FLuaAnimNodeRef& R)
				{
					UAnimGraphInstance& Graph = DerefGraph(R.Graph);
					return Graph.GetCurrentStateName(NodeIndexIn(Graph, R)).ToString();
				})
		);

		Lua.new_usertype<FLuaAnimGraphRef>("AnimGraph", sol::no_constructor,
			"IsValid", [](const FLuaAnimGraphRef& R) { return R.Get() != nullptr; },
			// 그래프를 비운다. 이 그래프에서 받은 모든 핸들이 무효가 되므로 새 핸들을 돌려준다.
			"Reset", [](const FLuaAnimGraphRef& R)
				{
					USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(R.Mesh.Get());
                    if (Component && !Component->GetAnimBlueprintPath().empty())
                    {
                        throw sol::error("Use ReloadAnimBlueprint to rebuild an asset-backed graph.");
                    }

                    UAnimGraphInstance& Graph = DerefGraph(R);
					Graph.ResetGraph();
					const FLuaAnimGraphRef Updated = RefOf(R, Graph);
                    if (Component && Component->GetOwner())
                    {
                        TrackAnimGraph(*Component->GetOwner(), Updated);
                    }
                    return Updated;
				},

			// ---- 에셋 ----
			"LoadSequence", [](const FLuaAnimGraphRef& R, const std::string& Path) -> sol::optional<FLuaAnimSequenceRef>
				{
					UAnimSequence* Sequence = DerefEditableGraph(R).LoadSequence(Path);
					return Sequence ? sol::optional<FLuaAnimSequenceRef>(FLuaAnimSequenceRef{ R, Sequence }) : sol::nullopt;
				},
			"CreateBlendSpace", [](const FLuaAnimGraphRef& R)
				{
					return FLuaBlendSpaceRef{ R, DerefEditableGraph(R).CreateBlendSpace() };
				},
			"LoadBlendSpace", [](const FLuaAnimGraphRef& R, const std::string& Path) -> sol::optional<FLuaBlendSpaceRef>
				{
					UBlendSpace1D* BlendSpace = DerefEditableGraph(R).LoadBlendSpace(Path);
					return BlendSpace ? sol::optional<FLuaBlendSpaceRef>(FLuaBlendSpaceRef{ R, BlendSpace }) : sol::nullopt;
				},

			// ---- 노드 ----
			// Options = { Loop = false, PlayRate = 1 }
			"SequencePlayer", [](const FLuaAnimGraphRef& R, const FLuaAnimSequenceRef& Sequence, sol::optional<sol::table> Options)
				{
					UAnimGraphInstance& Graph = DerefEditableGraph(R);
					const bool bLoop = Options ? Options->get_or("Loop", false) : false;
					const float PlayRate = Options ? Options->get_or("PlayRate", 1.0f) : 1.0f;
					return MakeNodeRef(R, Graph.AddSequencePlayer(&DerefSequence(Sequence), bLoop, PlayRate), "SequencePlayer");
				},
			// X 입력을 Float 변수에 바인딩한다. Options = { PlayRate = 1 }
			"BlendSpacePlayer", [](const FLuaAnimGraphRef& R, const FLuaBlendSpaceRef& BlendSpace, const std::string& XVariable, sol::optional<sol::table> Options)
				{
					UAnimGraphInstance& Graph = DerefEditableGraph(R);
					const float PlayRate = Options ? Options->get_or("PlayRate", 1.0f) : 1.0f;
					return MakeNodeRef(R, Graph.AddBlendSpacePlayer(&DerefBlendSpace(BlendSpace), XVariable, PlayRate), "BlendSpacePlayer");
				},
			// Alpha 입력을 Float 변수에 바인딩한다 (0 = A, 1 = B)
			"TwoWayBlend", [](const FLuaAnimGraphRef& R, const FLuaAnimNodeRef& A, const FLuaAnimNodeRef& B, const std::string& AlphaVariable)
				{
					UAnimGraphInstance& Graph = DerefEditableGraph(R);
					return MakeNodeRef(R, Graph.AddTwoWayBlend(NodeIndexIn(Graph, A), NodeIndexIn(Graph, B), AlphaVariable), "TwoWayBlend");
				},
			"StateMachine", [](const FLuaAnimGraphRef& R)
				{
					return MakeNodeRef(R, DerefEditableGraph(R).AddStateMachine(), "StateMachine");
				},
			// 그래프 출력. 조립을 마친 뒤 마지막에 부른다.
			"SetRoot", [](const FLuaAnimGraphRef& R, const FLuaAnimNodeRef& Node)
				{
					UAnimGraphInstance& Graph = DerefEditableGraph(R);
					Graph.SetRoot(NodeIndexIn(Graph, Node));
				},

			// ---- 변수 ----
			"SetFloat", [](const FLuaAnimGraphRef& R, const std::string& Name, float Value) { DerefGraph(R).SetFloat(Name, Value); },
			"GetFloat", [](const FLuaAnimGraphRef& R, const std::string& Name, sol::optional<float> Default) { return DerefGraph(R).GetFloat(Name, Default.value_or(0.0f)); },
			"SetBool", [](const FLuaAnimGraphRef& R, const std::string& Name, bool bValue) { DerefGraph(R).SetBool(Name, bValue); },
			"GetBool", [](const FLuaAnimGraphRef& R, const std::string& Name, sol::optional<bool> Default) { return DerefGraph(R).GetBool(Name, Default.value_or(false)); },

			// ---- 이름 Notify ----
			"OnNotify", [](const FLuaAnimGraphRef& R, const std::string& Name, sol::protected_function Fn)
				{
					auto bBroken = std::make_shared<bool>(false);
					DerefGraph(R).BindNotify(FName(Name), [Fn, bBroken, R]() { CallAnimLuaVoid<void>(Fn, bBroken, "OnNotify", R); });
				},
			// Handlers = { Begin = function(anim, totalDuration), Tick = function(anim, dt), End = function(anim) }
			"OnNotifyState", [](const FLuaAnimGraphRef& R, const std::string& Name, const sol::table& Handlers)
				{
					DerefGraph(R).BindNotifyState(FName(Name),
						WrapNotifyStateCallback(Handlers["Begin"], R, "OnNotifyState.Begin"),
						WrapNotifyStateCallback(Handlers["Tick"], R, "OnNotifyState.Tick"),
						WrapNotifyStateCallback(Handlers["End"], R, "OnNotifyState.End"));
				}
		);

		// RegisterActor가 만든 Actor 타입에 애니메이션 메서드를 덧붙인다
		sol::usertype<FLuaActorRef> ActorType = Lua["Actor"];

        // Manual graph creation remains explicit; asset-backed graphs use GetAnimGraph.
        ActorType["CreateAnimGraph"] = [](const FLuaActorRef& Ref)
        {
            AActor& Owner = Deref(Ref);
            USkeletalMeshComponent& Component = FirstSkeletalMesh(Ref);
            UAnimGraphInstance* Graph = Component.CreateAnimGraph();

            if (!Graph)
            {
                throw sol::error(Component.GetAnimBlueprintError());
            }

            const FLuaAnimGraphRef GraphRef = FLuaAnimGraphRef::Make(&Component, Graph);
            TrackAnimGraph(Owner, GraphRef);
            return GraphRef;
        };

        ActorType["GetAnimGraph"] = [](const FLuaActorRef& Ref) -> sol::optional<FLuaAnimGraphRef>
        {
            USkeletalMeshComponent& Component = FirstSkeletalMesh(Ref);
            UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(Component.GetAnimInstance());

            if (!Graph)
            {
                return sol::nullopt;
            }

            const FLuaAnimGraphRef GraphRef = FLuaAnimGraphRef::Make(&Component, Graph);
            TrackAnimGraph(Deref(Ref), GraphRef);
            return GraphRef;
        };

        ActorType["LoadAnimBlueprint"] = [](const FLuaActorRef& Ref, const std::string& Path) -> sol::optional<FLuaAnimGraphRef>
        {
            USkeletalMeshComponent& Component = FirstSkeletalMesh(Ref);

            if (!Component.SetAnimBlueprint(Path))
            {
                throw sol::error(Component.GetAnimBlueprintError());
            }

            UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(Component.GetAnimInstance());

            if (!Graph)
            {
                return sol::nullopt;
            }

            const FLuaAnimGraphRef GraphRef = FLuaAnimGraphRef::Make(&Component, Graph);
            TrackAnimGraph(Deref(Ref), GraphRef);
            return GraphRef;
        };

        ActorType["ReloadAnimBlueprint"] = [](const FLuaActorRef& Ref) -> sol::optional<FLuaAnimGraphRef>
        {
            USkeletalMeshComponent& Component = FirstSkeletalMesh(Ref);

            if (!Component.ReloadAnimBlueprint())
            {
                throw sol::error(Component.GetAnimBlueprintError().empty() ? "No Animation Blueprint is assigned." : Component.GetAnimBlueprintError());
            }

            UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(Component.GetAnimInstance());
            const FLuaAnimGraphRef GraphRef = FLuaAnimGraphRef::Make(&Component, Graph);
            TrackAnimGraph(Deref(Ref), GraphRef);
            return GraphRef;
        };

	}
}

namespace FLuaBindings
{
	void Register(sol::state& Lua)
	{
		// TODO(B): wait, wait_until, wait_until_move_done, StartCoroutine
		RegisterMath(Lua);
		RegisterActor(Lua);
		RegisterComponent(Lua);
		RegisterInput(Lua);
		RegisterCoroutine(Lua);
		RegisterWidget(Lua);
		RegisterGameplay(Lua);
		RegisterAudio(Lua);
		RegisterCameraAction(Lua);
		RegisterAnimation(Lua);
	}

	void FlushPendingDestroy(UWorld* World)
	{
		TArray<FLuaActorRef> ToDestroy = std::move(PendingDestroies);
		PendingDestroies = TArray<FLuaActorRef>();

		for (const FLuaActorRef& Ref : ToDestroy)
		{
			if (Ref.World != World)
			{
				continue;
			}
			if (AActor* Actor = Ref.Get()) // 살아 있는 액터만 Destroy
			{
				Actor->Destroy();
			}
		}
	}

	void ReleaseCallbacks(AActor* Owner)
	{
		for (int32 i = ClickBindings.Num() - 1; i >= 0; --i)
		{
			if (ClickBindings[i].Owner != Owner) continue;
			if (UButton* B = Cast<UButton>(ClickBindings[i].Button.Get()))
				B->OnClicked = nullptr;
			ClickBindings.RemoveAt(i);
		}

		// Lua 함수를 잡고 있는 전환 규칙/상태 콜백/Notify 핸들러를 놓는다.
		// Manual graphs rebuild in OnReload; asset graphs only rebind notification callbacks.
		for (int32 i = AnimGraphBindings.Num() - 1; i >= 0; --i)
		{
			if (AnimGraphBindings[i].Owner != Owner) continue;
            if (UAnimGraphInstance* Graph = AnimGraphBindings[i].Graph.Get())
            {
                USkeletalMeshComponent* Component = Graph->GetSkelMeshComponent();

                if (Component && !Component->GetAnimBlueprintPath().empty())
                {
                    Graph->ClearNotifyBindings();
                }
                else
                {
                    Graph->ResetGraph();
                }
            }
			AnimGraphBindings.RemoveAt(i);
		}
	}
}

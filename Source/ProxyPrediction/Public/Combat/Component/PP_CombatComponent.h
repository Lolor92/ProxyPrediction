#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "Abilities/GameplayAbility.h"
#include "Components/ActorComponent.h"
#include "Combat/Data/PP_CombatTagReactionData.h"
#include "Prediction/Data/PP_ReactionData.h"
#include "GameplayTagContainer.h"
#include "PP_CombatComponent.generated.h"

class FBoolProperty;
class UAbilitySystemComponent;
class UAnimInstance;
class UPP_CombatAbilityData;
class UGameplayEffect;

USTRUCT(BlueprintType)
struct FPP_CombatAnimBoolBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim Bool Binding", meta=(Categories="Tags", DisplayName="Tags"))
	FGameplayTagContainer Tags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim Bool Binding", meta=(DisplayName="Anim Bool Name"))
	FName AnimBoolName;

	FBoolProperty* CachedBoolProperty = nullptr;

	FString GetDisplayNameString() const
	{
		if (!AnimBoolName.IsNone()) return AnimBoolName.ToString();
		if (!Tags.IsEmpty()) return Tags.ToStringSimple();
		return TEXT("Anim Bool Binding");
	}
};

template<>
struct TStructOpsTypeTraits<FPP_CombatAnimBoolBinding> : public TStructOpsTypeTraitsBase2<FPP_CombatAnimBoolBinding>
{
	enum { WithDisplayName = true };
};

USTRUCT()
struct FPP_ReplicatedCombatAnimBool
{
	GENERATED_BODY()

	UPROPERTY()
	FName AnimBoolName;

	UPROPERTY()
	bool bValue = false;
};

/** Local-only animation tags predicted on a remote proxy while server confirmation is in flight. */
struct FPP_PredictedProxyAnimTags
{
	FGameplayTagContainer Tags;
	FTimerHandle ExpireTimer;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="ActorComponent (Combat)"))
class PROXYPREDICTION_API UPP_CombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPP_CombatComponent();

	void GrantAbilities();
	void RegisterTagListeners();

	void SetAnimBool(const FPP_CombatAnimBoolBinding& Binding, bool bValue);
	bool IsAnimBoolActive(const FPP_CombatAnimBoolBinding& Binding) const;

	/** Adds temporary cosmetic tags used only by this proxy's animation bindings. */
	int32 BeginPredictedProxyAnimTags(const FGameplayTagContainer& Tags, float TimeoutSeconds);
	/** Removes one temporary cosmetic prediction, for example after server rejection. */
	void EndPredictedProxyAnimTags(int32 PredictionHandle);

	void OnTagChanged(FGameplayTag Tag, int32 NewCount);

	/** Returns true when GAS currently marks this actor as blocking. */
	UFUNCTION(BlueprintPure, Category="Combat|Defense")
	bool IsBlockingActive() const;

	/** Tests the replicated/local block state and the defender-facing angle. */
	bool CanBlockAttackFrom(const AActor* AttackerActor, float BlockAngleDegrees) const;
	bool IsParryingActive() const;
	bool IsDodgingActive() const;
	EPP_SuperArmorLevel GetSuperArmorLevel() const;
	bool DoesCurrentSuperArmorIgnoreDamage() const;

	/** Applies both sides of a successful block. Authority only. */
	void ApplySuccessfulBlockEffects(AActor* AttackerActor) const;
	void ApplySuccessfulParryEffects(AActor* AttackerActor) const;
	void ApplySuccessfulDodgeEffects(AActor* AttackerActor) const;
	void ApplySuccessfulSuperArmorEffects(AActor* AttackerActor) const;

	UPROPERTY(EditDefaultsOnly, Category="Combat|Anim", meta=(TitleProperty="AnimBoolName", ShowOnlyInnerProperties, DisplayAfter="TagReactionData"))
	TArray<FPP_CombatAnimBoolBinding> AnimBoolBindings;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Abilities")
	TObjectPtr<UPP_CombatAbilityData> AbilityData = nullptr;

	/**
	 * Effects applied in array order when this character's ability system is initialized.
	 * Use these for initial vitals and persistent stats such as health, defense, and critical damage.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Startup",
		meta=(DisplayName="Startup Gameplay Effects"))
	TArray<TSubclassOf<UGameplayEffect>> StartupGameplayEffects;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Tags", meta=(DisplayName="Tag Reaction Data"))
	TObjectPtr<UPP_CombatTagReactionData> TagReactionData = nullptr;

	/** Parent tag whose presence suppresses voluntary movement without blocking camera input. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Crowd Control")
	FGameplayTag CrowdControlTag;

	UFUNCTION(BlueprintPure, Category="Combat|Crowd Control")
	bool IsCrowdControlActive() const;

	/** Tag that temporarily makes the character capsule ignore the Pawn channel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Collision")
	FGameplayTag IgnorePawnCollisionTag;

	UFUNCTION(BlueprintPure, Category="Combat|Collision")
	bool IsIgnoringPawnCollision() const;

	/** Tag granted while this actor is actively blocking. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Block")
	FGameplayTag BlockingTag;

	/** Effect applied to the attacker when this actor blocks successfully. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Block")
	TSubclassOf<UGameplayEffect> AttackerBlockedEffectClass;

	/** Effect applied to this actor when it blocks successfully. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Block")
	TSubclassOf<UGameplayEffect> DefenderBlockSuccessEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Parry")
	FGameplayTag ParryingTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Parry")
	TSubclassOf<UGameplayEffect> AttackerParriedEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Parry")
	TSubclassOf<UGameplayEffect> DefenderParrySuccessEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Dodge")
	FGameplayTag DodgingTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Dodge")
	TSubclassOf<UGameplayEffect> AttackerDodgedEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Dodge")
	TSubclassOf<UGameplayEffect> DefenderDodgeSuccessEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Super Armor")
	FGameplayTag SuperArmorTag1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Super Armor")
	FGameplayTag SuperArmorTag2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Super Armor")
	FGameplayTag SuperArmorTag3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Super Armor")
	TSubclassOf<UGameplayEffect> AttackerSuperArmoredEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Super Armor")
	TSubclassOf<UGameplayEffect> DefenderSuperArmorSuccessEffectClass;

	/** First super-armor level that completely ignores damage. None disables immunity. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Super Armor")
	EPP_SuperArmorLevel MinimumSuperArmorLevelToIgnoreDamage = EPP_SuperArmorLevel::SuperArmor3;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent = nullptr;

	UPROPERTY()
	TObjectPtr<UAnimInstance> AnimInstance = nullptr;

	void RefreshAnimInstance();
	bool ResolveAbilitySystemComponent();
	void InitializeAbilityActorInfoIfNeeded() const;
	void TryInitializeAbilitySystem();
	void ApplyStartupGameplayEffects();
	void ScheduleAbilitySystemRetry();
	void ClearTagListeners();
	void ClearReactionTimers();
	void ApplyCrowdControlState(bool bShouldEnforce);
	void ApplyPawnCollisionIgnoreState(bool bShouldIgnore);
	void ConfirmPredictedProxyAnimTag(FGameplayTag Tag);
	void RefreshAnimBoolBindings();
	bool HasAbility(const TSubclassOf<UGameplayAbility>& AbilityClass) const;
	void ApplyGameplayEffectToActor(AActor* RecipientActor,
		const TSubclassOf<UGameplayEffect>& EffectClass, AActor* InstigatorActor) const;

	template<typename Func>
	void ExecuteDelayed(Func InFunction, float DelaySeconds, FTimerHandle& TimerHandle);

	void QueueAbilityActivation(const FPP_CombatTagReactionBinding& Binding, FGameplayTag TriggeredTag, UAbilitySystemComponent* ASC);
	void QueueEffectRemove(const FPP_CombatTagReactionBinding& Binding, FGameplayTag TriggeredTag, UAbilitySystemComponent* ASC);
	void QueueEffectApply(const FPP_CombatTagReactionBinding& Binding, FGameplayTag TriggeredTag, UAbilitySystemComponent* ASC);
	FName GetRemoveTimerKey(const FPP_CombatTagReactionBinding& Binding, const FGameplayTag& TriggeredTag) const;

	TMap<FGameplayTag, FTimerHandle> AbilityTimers;
	TMap<FName, FTimerHandle> RemoveEffectTimers;
	TMap<FGameplayTag, FTimerHandle> ApplyEffectTimers;
	TMap<FName, TArray<FActiveGameplayEffectHandle>> AppliedEffectHandles;
	TMap<FGameplayTag, FDelegateHandle> TagListenerHandles;
	TMap<int32, FPP_PredictedProxyAnimTags> PredictedProxyAnimTags;
	FTimerHandle AbilitySystemRetryTimer;
	int32 NextPredictedProxyAnimTagHandle = 0;
	int32 AbilitySystemRetryCount = 0;
	bool bStartupGameplayEffectsApplied = false;
	bool bCrowdControlEnforced = false;
	bool bPawnCollisionIgnoreEnforced = false;
	TEnumAsByte<ECollisionResponse> SavedPawnCollisionResponse = ECR_Block;
};

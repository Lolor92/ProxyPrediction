#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbility.h"
#include "Data/SCTagReactionData.h"
#include "SyncCombatComponent.generated.h"

class USCAbilityData;
class UAbilitySystemComponent;
class UAnimInstance;
class FBoolProperty;

/**
 * Maps GameplayTags -> AnimInstance bool variable name.
 * When any tag in Tags is present on the ASC, the bool becomes true.
 */
USTRUCT(BlueprintType)
struct FAnimBoolBinding
{
	GENERATED_BODY()

	/** Tags that should drive this bool */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim Bool Binding", meta=(Categories="Tags", DisplayName="Tags"))
	FGameplayTagContainer Tags;

	/** Bool property name on the AnimInstance (BP or C++) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim Bool Binding", meta=(DisplayName="Anim Bool Name"))
	FName AnimBoolName;

	/** Runtime cache (not exposed to editor) */
	FBoolProperty* CachedBoolProperty = nullptr;

	/** Pretty header per array element */
	FString GetDisplayNameString() const
	{
		if (!AnimBoolName.IsNone()) return AnimBoolName.ToString();
		if (!Tags.IsEmpty()) return Tags.ToStringSimple();
		return TEXT("Anim Bool Binding");
	}
};

template<>
struct TStructOpsTypeTraits<FAnimBoolBinding> : public TStructOpsTypeTraitsBase2<FAnimBoolBinding>
{
	enum { WithDisplayName = true };
};

USTRUCT()
struct FReplicatedAnimBool
{
	GENERATED_BODY()

	UPROPERTY()
	FName AnimBoolName;

	UPROPERTY()
	bool bValue = false;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="ActorComponent (SyncCombat)"))
class SYNCCOMBAT_API USyncCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USyncCombatComponent();

	// --- Setup ---------------------------------------------------------------
	void GrantAbilities();
	void RegisterTagListeners();

	// --- Anim Bool Binding ---------------------------------------------------
	void SetAnimBool(const FAnimBoolBinding& Binding, bool bValue);
	bool IsAnimBoolActive(const FAnimBoolBinding& Binding) const;

	/** Tag change callback registered on the ASC */
	void OnTagChanged(FGameplayTag Tag, int32 NewCount);

	// --- Config --------------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, Category="SyncCombat|Anim",
		meta=(TitleProperty="AnimBoolName", ShowOnlyInnerProperties, DisplayAfter="TagReactionData"))
	TArray<FAnimBoolBinding> AnimBoolBindings;

	/** Ability data asset used to grant abilities */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SyncCombat|Abilities")
	USCAbilityData* AbilityData = nullptr;

	/** Data asset defining tag reactions (effects/abilities) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SyncCombat|Tags", meta=(DisplayName="Tag Reaction Data"))
	USCTagReactionData* TagReactionData = nullptr;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// --- Cached runtime pointers --------------------------------------------
	UPROPERTY()
	UAbilitySystemComponent* AbilitySystemComponent = nullptr;

	UPROPERTY()
	UAnimInstance* AnimInstance = nullptr;

	// --- Internal helpers ----------------------------------------------------
	void RefreshAnimInstance();
	bool ResolveAbilitySystemComponent();
	void InitializeAbilityActorInfoIfNeeded() const;
	void TryInitializeAbilitySystem();
	void ScheduleAbilitySystemRetry();
	void ClearTagListeners();
	void ClearReactionTimers();
	bool HasAbility(const TSubclassOf<UGameplayAbility>& AbilityClass) const;

	// --- Reactions (delayed execution) --------------------------------------
	template<typename Func>
	void ExecuteDelayed(Func InFunction, float DelaySeconds, FTimerHandle& TimerHandle);

	void QueueAbilityActivation(const FTagReactionBinding& Binding, const FGameplayTag TriggeredTag, UAbilitySystemComponent* ASC);
	void QueueEffectRemove(const FTagReactionBinding& Binding, const FGameplayTag TriggeredTag, UAbilitySystemComponent* ASC);
	void QueueEffectApply(const FTagReactionBinding& Binding, const FGameplayTag TriggeredTag, UAbilitySystemComponent* ASC);
	FName GetRemoveTimerKey(const FTagReactionBinding& Binding, const FGameplayTag& TriggeredTag) const;

	// --- Per-trigger timers --------------------------------------------------
	TMap<FGameplayTag, FTimerHandle> AbilityTimers;
	TMap<FName, FTimerHandle> RemoveEffectTimers;
	TMap<FGameplayTag, FTimerHandle> ApplyEffectTimers;
	TMap<FGameplayTag, FDelegateHandle> TagListenerHandles;
	FTimerHandle AbilitySystemRetryTimer;
	int32 AbilitySystemRetryCount = 0;
};

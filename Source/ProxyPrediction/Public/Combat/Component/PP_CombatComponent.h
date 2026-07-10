#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Components/ActorComponent.h"
#include "Combat/Data/PP_CombatTagReactionData.h"
#include "GameplayTagContainer.h"
#include "PP_CombatComponent.generated.h"

class FBoolProperty;
class UAbilitySystemComponent;
class UAnimInstance;
class UPP_CombatAbilityData;

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

	void OnTagChanged(FGameplayTag Tag, int32 NewCount);

	UPROPERTY(EditDefaultsOnly, Category="Combat|Anim", meta=(TitleProperty="AnimBoolName", ShowOnlyInnerProperties, DisplayAfter="TagReactionData"))
	TArray<FPP_CombatAnimBoolBinding> AnimBoolBindings;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Abilities")
	TObjectPtr<UPP_CombatAbilityData> AbilityData = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Tags", meta=(DisplayName="Tag Reaction Data"))
	TObjectPtr<UPP_CombatTagReactionData> TagReactionData = nullptr;

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
	void ScheduleAbilitySystemRetry();
	void ClearTagListeners();
	void ClearReactionTimers();
	bool HasAbility(const TSubclassOf<UGameplayAbility>& AbilityClass) const;

	template<typename Func>
	void ExecuteDelayed(Func InFunction, float DelaySeconds, FTimerHandle& TimerHandle);

	void QueueAbilityActivation(const FPP_CombatTagReactionBinding& Binding, FGameplayTag TriggeredTag, UAbilitySystemComponent* ASC);
	void QueueEffectRemove(const FPP_CombatTagReactionBinding& Binding, FGameplayTag TriggeredTag, UAbilitySystemComponent* ASC);
	void QueueEffectApply(const FPP_CombatTagReactionBinding& Binding, FGameplayTag TriggeredTag, UAbilitySystemComponent* ASC);
	FName GetRemoveTimerKey(const FPP_CombatTagReactionBinding& Binding, const FGameplayTag& TriggeredTag) const;

	TMap<FGameplayTag, FTimerHandle> AbilityTimers;
	TMap<FName, FTimerHandle> RemoveEffectTimers;
	TMap<FGameplayTag, FTimerHandle> ApplyEffectTimers;
	TMap<FGameplayTag, FDelegateHandle> TagListenerHandles;
	FTimerHandle AbilitySystemRetryTimer;
	int32 AbilitySystemRetryCount = 0;
};

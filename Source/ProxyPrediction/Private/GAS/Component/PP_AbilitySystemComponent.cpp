#include "ProxyPrediction/Public/GAS/Component/PP_AbilitySystemComponent.h"

UPP_AbilitySystemComponent::UPP_AbilitySystemComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPP_AbilitySystemComponent::BeginPlay()
{
	Super::BeginPlay();
	GrantAbilitySets();
}

void UPP_AbilitySystemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveGrantedAbilitySets();
	Super::EndPlay(EndPlayReason);
}

void UPP_AbilitySystemComponent::GrantAbilitySets()
{
	if (bAbilitySetsGranted) return;
	if (!IsOwnerActorAuthoritative()) return;
	
	for (const UPP_AbilitySet* AbilitySet : AbilitySetsToGrant)
	{
		if (!AbilitySet) continue;
		
		AbilitySet->GiveToAbilitySystem(this, &GrantedHandles, GetOwner());
	}
	
	bAbilitySetsGranted = true;
}

void UPP_AbilitySystemComponent::RemoveGrantedAbilitySets()
{
	if (!bAbilitySetsGranted) return;
	if (!IsOwnerActorAuthoritative()) return;
	
	GrantedHandles.ClearAbilities(this);
}

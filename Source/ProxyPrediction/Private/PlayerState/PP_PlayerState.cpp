#include "ProxyPrediction/Public/PlayerState/PP_PlayerState.h"
#include "GAS/Attribute/PP_AttributeSet.h"
#include "GAS/Component/PP_AbilitySystemComponent.h"

APP_PlayerState::APP_PlayerState()
{
	SetNetUpdateFrequency(100.f);

	AbilitySystemComponent = CreateDefaultSubobject<UPP_AbilitySystemComponent>("PP_AbilitySystemComponent");
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	AttributeSet = CreateDefaultSubobject<UPP_AttributeSet>("AttributeSet");
}


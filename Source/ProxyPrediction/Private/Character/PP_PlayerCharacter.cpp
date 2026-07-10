#include "ProxyPrediction/Public/Character/PP_PlayerCharacter.h"
#include "AbilitySystemComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "PlayerState/PP_PlayerState.h"


APP_PlayerCharacter::APP_PlayerCharacter()
{
	SpringArm = CreateDefaultSubobject<USpringArmComponent>("Spring Arm");
	SpringArm->SetupAttachment(GetRootComponent());
	SpringArm->TargetArmLength = 750.f;
	SpringArm->SetRelativeLocation(FVector(0.f, 25.f, 50.f));
	SpringArm->bUsePawnControlRotation = true;

	Camera = CreateDefaultSubobject<UCameraComponent>("Camera");
	Camera->SetupAttachment(SpringArm);
	Camera->bUsePawnControlRotation = false;
}

void APP_PlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	
	// Server-side ASC initialization.
	InitializeAbilitySystem();
}

void APP_PlayerCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	
	// Client-side ASC initialization.
	InitializeAbilitySystem();
}

void APP_PlayerCharacter::InitializeAbilitySystem()
{
	APP_PlayerState* PP_PlayerState = GetPlayerState<APP_PlayerState>();
	if (!PP_PlayerState) return;

	// Player characters use the PlayerState-owned ASC.
	AbilitySystemComponent = PP_PlayerState->GetAbilitySystemComponent();
	AttributeSet = PP_PlayerState->GetAttributeSet();

	if (!AbilitySystemComponent) return;

	AbilitySystemComponent->InitAbilityActorInfo(PP_PlayerState, this);
}


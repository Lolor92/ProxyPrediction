#include "ProxyPrediction/Public/Character/PP_BaseCharacter.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PlayerState/PP_PlayerState.h"


APP_BaseCharacter::APP_BaseCharacter()
{
	bReplicates = true;

	// Collision defaults.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetCapsuleComponent()->SetGenerateOverlapEvents(true);

	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetGenerateOverlapEvents(true);

	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Base movement tuning.
	GetCharacterMovement()->MaxWalkSpeed = 400.0f;
	GetCharacterMovement()->MaxCustomMovementSpeed = 400.0f;

	GetCharacterMovement()->MaxJumpApexAttemptsPerSimulation = 1;
	GetCharacterMovement()->JumpZVelocity = 850.f;
	GetCharacterMovement()->AirControl = 0.5f;
	GetCharacterMovement()->GravityScale = 2.5f;
}

UAbilitySystemComponent* APP_BaseCharacter::GetAbilitySystemComponent() const
{
	if (AbilitySystemComponent) return AbilitySystemComponent;

	// Fallback for actors that still own an ASC component directly.
	if (UAbilitySystemComponent* CharacterAbilitySystem = FindComponentByClass<UAbilitySystemComponent>())
	{
		return CharacterAbilitySystem;
	}

	APP_PlayerState* PP_PlayerState = GetPlayerState<APP_PlayerState>();
	return PP_PlayerState ? PP_PlayerState->GetAbilitySystemComponent() : nullptr;
}

UAttributeSet* APP_BaseCharacter::GetAttributeSet() const
{
	if (AttributeSet) return AttributeSet;

	const APP_PlayerState* PP_PlayerState = GetPlayerState<APP_PlayerState>();
	return PP_PlayerState ? PP_PlayerState->GetAttributeSet() : nullptr;
}

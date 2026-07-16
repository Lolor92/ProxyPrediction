#include "UI/PP_CombatTextComponent.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UI/PP_CombatTextWidget.h"

UPP_CombatTextComponent::UPP_CombatTextComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPP_CombatTextComponent::ShowCombatTextToOwner(
	const float Amount,
	const FVector& WorldLocation,
	const EPP_CombatTextType Type,
	const bool bCritical)
{
	AActor* OwnerActor = GetOwner();
	const bool bNumericType =
		Type == EPP_CombatTextType::DamageDealt ||
		Type == EPP_CombatTextType::DamageReceived ||
		Type == EPP_CombatTextType::HealingDealt ||
		Type == EPP_CombatTextType::HealingReceived;
	if (!OwnerActor || (bNumericType && Amount <= 0.0f) || WorldLocation.ContainsNaN()) return;

	if (OwnerActor->HasAuthority())
	{
		ClientShowCombatText(Amount, WorldLocation, Type, bCritical);
	}
	else if (const APawn* OwnerPawn = Cast<APawn>(OwnerActor);
		OwnerPawn && OwnerPawn->IsLocallyControlled())
	{
		ClientShowCombatText_Implementation(Amount, WorldLocation, Type, bCritical);
	}
}

void UPP_CombatTextComponent::ClientShowCombatText_Implementation(
	const float Amount,
	const FVector WorldLocation,
	const EPP_CombatTextType Type,
	const bool bCritical)
{
	if (!ShouldShowType(Type)) return;

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	APlayerController* PlayerController =
		OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
	if (!PlayerController || !PlayerController->IsLocalController()) return;

	FVector2D ScreenPosition;
	if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
		PlayerController, WorldLocation, ScreenPosition, true))
	{
		return;
	}

	TSubclassOf<UPP_CombatTextWidget> WidgetClass = CombatTextWidgetClass;
	if (!WidgetClass)
	{
		WidgetClass = UPP_CombatTextWidget::StaticClass();
	}
	UPP_CombatTextWidget* Widget =
		CreateWidget<UPP_CombatTextWidget>(PlayerController, WidgetClass);
	if (!Widget) return;

	Widget->AddToViewport(100);
	Widget->SetAlignmentInViewport(FVector2D(0.5f, 0.5f));
	ScreenPosition.X += FMath::FRandRange(-RandomScreenOffset.X, RandomScreenOffset.X);
	ScreenPosition.Y += FMath::FRandRange(-RandomScreenOffset.Y, RandomScreenOffset.Y);
	Widget->SetPositionInViewport(ScreenPosition, false);
	Widget->ConfigureCombatText(Amount, Type, bCritical);
}

bool UPP_CombatTextComponent::ShouldShowType(const EPP_CombatTextType Type) const
{
	switch (Type)
	{
	case EPP_CombatTextType::DamageDealt:
		return bShowDamageDealt;
	case EPP_CombatTextType::DamageReceived:
		return bShowDamageReceived;
	case EPP_CombatTextType::HealingDealt:
		return bShowHealingDealt;
	case EPP_CombatTextType::HealingReceived:
		return bShowHealingReceived;
	case EPP_CombatTextType::AttackBlocked:
	case EPP_CombatTextType::AttackDodged:
	case EPP_CombatTextType::AttackSuperArmored:
		return bShowAttackDefenseOutcomes;
	default:
		return false;
	}
}

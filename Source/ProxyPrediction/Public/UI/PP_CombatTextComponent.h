#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UI/PP_CombatTextTypes.h"
#include "PP_CombatTextComponent.generated.h"

class UPP_CombatTextWidget;

/**
 * Sends server-authoritative combat results only to the owning client and
 * creates a viewport widget without depending on a custom HUD class.
 */
UCLASS(ClassGroup=(UI), meta=(BlueprintSpawnableComponent))
class PROXYPREDICTION_API UPP_CombatTextComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPP_CombatTextComponent();

	void ShowCombatTextToOwner(
		float Amount,
		const FVector& WorldLocation,
		EPP_CombatTextType Type,
		bool bCritical = false);

protected:
	UFUNCTION(Client, Unreliable)
	void ClientShowCombatText(
		float Amount,
		FVector WorldLocation,
		EPP_CombatTextType Type,
		bool bCritical);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Text")
	TSubclassOf<UPP_CombatTextWidget> CombatTextWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Text")
	bool bShowDamageDealt = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Text")
	bool bShowDamageReceived = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Text")
	bool bShowHealingDealt = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Text")
	bool bShowHealingReceived = true;

	/** Shows Blocked, Dodged, and Super Armor only to the owner whose attack was defended. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Text")
	bool bShowAttackDefenseOutcomes = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Text")
	FVector2D RandomScreenOffset = FVector2D(18.0f, 8.0f);

private:
	bool ShouldShowType(EPP_CombatTextType Type) const;
};

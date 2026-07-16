#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/PP_CombatTextTypes.h"
#include "PP_CombatTextWidget.generated.h"

class UTextBlock;

/**
 * Native fallback combat-text widget. A Blueprint subclass can bind a TextBlock
 * named CombatText and override the presentation without requiring a custom HUD.
 */
UCLASS(Blueprintable)
class PROXYPREDICTION_API UPP_CombatTextWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Combat Text")
	void ConfigureCombatText(float Amount, EPP_CombatTextType Type, bool bCritical);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION(BlueprintImplementableEvent, Category="Combat Text")
	void OnCombatTextConfigured(float Amount, EPP_CombatTextType Type, bool bCritical);

	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> CombatText;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat Text|Style")
	FLinearColor DamageDealtColor = FLinearColor::White;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat Text|Style")
	FLinearColor DamageReceivedColor = FLinearColor(1.0f, 0.08f, 0.05f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat Text|Style")
	FLinearColor HealingColor = FLinearColor(0.1f, 1.0f, 0.2f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat Text|Style")
	FLinearColor DefenseOutcomeColor = FLinearColor(1.0f, 0.65f, 0.05f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat Text|Style", meta=(ClampMin="1"))
	int32 FontSize = 30;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat Text|Style", meta=(ClampMin="1"))
	int32 CriticalFontSize = 42;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat Text|Style", meta=(ClampMin="1"))
	int32 DefenseOutcomeFontSize = 26;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat Text|Style")
	FName DefenseOutcomeTypeface = TEXT("Italic");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat Text|Style",
		meta=(ClampMin="0.0", ClampMax="1.0"))
	float DefenseOutcomeOpacity = 0.75f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat Text|Style",
		meta=(ClampMin="0.1", ClampMax="1.0"))
	float DamageReceivedFontScale = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat Text|Animation", meta=(ClampMin="0.05"))
	float LifetimeSeconds = 0.9f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat Text|Animation")
	float RiseDistance = 70.0f;

	/**
	 * Native fallback rise/fade/removal. Disable this in a Widget Blueprint when
	 * its own UMG animations handle movement, fading, and removal.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat Text|Animation")
	bool bUseNativeLifetimeAnimation = true;

private:
	void ApplyPresentation();

	float DisplayAmount = 0.0f;
	float ElapsedSeconds = 0.0f;
	EPP_CombatTextType DisplayType = EPP_CombatTextType::DamageDealt;
	bool bDisplayCritical = false;
};

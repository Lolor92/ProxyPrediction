#include "UI/PP_CombatTextWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"

void UPP_CombatTextWidget::ConfigureCombatText(
	const float Amount,
	const EPP_CombatTextType Type,
	const bool bCritical)
{
	DisplayAmount = FMath::Max(0.0f, Amount);
	DisplayType = Type;
	bDisplayCritical = bCritical;
	ElapsedSeconds = 0.0f;

	ApplyPresentation();
	OnCombatTextConfigured(DisplayAmount, DisplayType, bDisplayCritical);
}

TSharedRef<SWidget> UPP_CombatTextWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		CombatText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("CombatText"));
		WidgetTree->RootWidget = CombatText;
	}

	return Super::RebuildWidget();
}

void UPP_CombatTextWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetRenderOpacity(1.0f);
	SetRenderTranslation(FVector2D::ZeroVector);
	ApplyPresentation();
}

void UPP_CombatTextWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bUseNativeLifetimeAnimation) return;

	ElapsedSeconds += InDeltaTime;
	const float SafeLifetime = FMath::Max(LifetimeSeconds, 0.05f);
	const float Alpha = FMath::Clamp(ElapsedSeconds / SafeLifetime, 0.0f, 1.0f);

	SetRenderTranslation(FVector2D(0.0f, -RiseDistance * Alpha));
	SetRenderOpacity(1.0f - FMath::Square(Alpha));

	if (Alpha >= 1.0f)
	{
		RemoveFromParent();
	}
}

void UPP_CombatTextWidget::ApplyPresentation()
{
	if (!CombatText) return;

	const bool bHealing =
		DisplayType == EPP_CombatTextType::HealingDealt ||
		DisplayType == EPP_CombatTextType::HealingReceived;
	const bool bDamageReceived = DisplayType == EPP_CombatTextType::DamageReceived;
	const bool bDefenseOutcome =
		DisplayType == EPP_CombatTextType::AttackBlocked ||
		DisplayType == EPP_CombatTextType::AttackDodged ||
		DisplayType == EPP_CombatTextType::AttackSuperArmored;

	if (bDefenseOutcome)
	{
		FText OutcomeText;
		switch (DisplayType)
		{
		case EPP_CombatTextType::AttackBlocked:
			OutcomeText = NSLOCTEXT("PPCombatText", "Blocked", "Blocked");
			break;
		case EPP_CombatTextType::AttackDodged:
			OutcomeText = NSLOCTEXT("PPCombatText", "Dodged", "Dodged");
			break;
		case EPP_CombatTextType::AttackSuperArmored:
			OutcomeText = NSLOCTEXT("PPCombatText", "SuperArmor", "Super Armor");
			break;
		default:
			break;
		}
		CombatText->SetText(OutcomeText);
	}
	else
	{
		const int32 RoundedAmount = FMath::Max(1, FMath::RoundToInt(DisplayAmount));
		const FString Prefix = bHealing ? TEXT("+") : (bDamageReceived ? TEXT("-") : TEXT(""));
		CombatText->SetText(FText::FromString(FString::Printf(TEXT("%s%d"), *Prefix, RoundedAmount)));
	}

	FLinearColor Color = DamageDealtColor;
	if (bDamageReceived)
	{
		Color = DamageReceivedColor;
	}
	else if (bHealing)
	{
		Color = HealingColor;
	}
	else if (bDefenseOutcome)
	{
		Color = DefenseOutcomeColor;
		Color.A *= DefenseOutcomeOpacity;
	}
	CombatText->SetColorAndOpacity(FSlateColor(Color));
	CombatText->SetJustification(ETextJustify::Center);

	FSlateFontInfo Font = CombatText->GetFont();
	const int32 BaseFontSize =
		bDefenseOutcome ? DefenseOutcomeFontSize : (bDisplayCritical ? CriticalFontSize : FontSize);
	Font.Size = bDamageReceived && !bDefenseOutcome
		? FMath::Max(1, FMath::RoundToInt(BaseFontSize * DamageReceivedFontScale))
		: BaseFontSize;
	if (bDefenseOutcome)
	{
		Font.TypefaceFontName = DefenseOutcomeTypeface;
	}
	CombatText->SetFont(Font);
}

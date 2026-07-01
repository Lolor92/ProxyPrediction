#pragma once

#include "CoreMinimal.h"
#include "PP_BaseCharacter.h"
#include "PP_PlayerCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;

UCLASS()
class PROXYPREDICTION_API APP_PlayerCharacter : public APP_BaseCharacter
{
	GENERATED_BODY()

public:
	APP_PlayerCharacter();

	// Player-state ASC setup.
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	
	FORCEINLINE USpringArmComponent* GetSpringArm() const { return SpringArm.Get(); }
	FORCEINLINE UCameraComponent* GetCamera() const { return Camera.Get(); }
	
private:
	void InitializeAbilitySystem();
	
	// Camera components.
	UPROPERTY(VisibleAnywhere, Category="Camera")
	TObjectPtr<USpringArmComponent> SpringArm = nullptr;

	UPROPERTY(VisibleAnywhere, Category="Camera")
	TObjectPtr<UCameraComponent> Camera = nullptr;
};

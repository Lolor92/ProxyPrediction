#include "Input/Data/PP_InputConfig.h"

const UInputAction* UPP_InputConfig::FindInputActionByTag(const FGameplayTag& InputTag) const
{
	for (const FPP_InputAction& InputAction : InputActions)
	{
		if (InputAction.InputAction && InputAction.InputTag == InputTag)
		{
			return InputAction.InputAction;
		}
	}

	return nullptr;
}


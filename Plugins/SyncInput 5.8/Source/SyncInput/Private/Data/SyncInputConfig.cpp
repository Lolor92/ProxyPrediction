#include "Data/SyncInputConfig.h"

const UInputAction* USyncInputConfig::FindInputActionByTag(const FGameplayTag& InputTag) const
{
	for (const FSyncInputAction& SyncInputAction : SyncInputActions)
	{
		if (SyncInputAction.InputAction && SyncInputAction.InputTag == InputTag)
		{
			return SyncInputAction.InputAction;
		}
	}
	return nullptr;
}

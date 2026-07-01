#include "Data/PP_ReactionData.h"

const FPP_ReactionDataEntry& UPP_ReactionData::FindReactionChecked(FGameplayTag ReactionTag) const
{
	const FPP_ReactionDataEntry* Reaction = FindReactionPtr(ReactionTag);
	check(Reaction);
	return *Reaction;
}

bool UPP_ReactionData::FindReaction(FGameplayTag ReactionTag, FPP_ReactionDataEntry& OutReaction) const
{
	const FPP_ReactionDataEntry* Reaction = FindReactionPtr(ReactionTag);
	if (!Reaction) return false;

	OutReaction = *Reaction;
	return true;
}

const FPP_ReactionDataEntry* UPP_ReactionData::FindReactionPtr(FGameplayTag ReactionTag) const
{
	if (!ReactionTag.IsValid()) return nullptr;

	for (const FPP_ReactionDataEntry& Entry : Reactions)
	{
		if (Entry.ReactionTag.IsValid() && ReactionTag.MatchesTag(Entry.ReactionTag))
		{
			return &Entry;
		}
	}

	return nullptr;
}

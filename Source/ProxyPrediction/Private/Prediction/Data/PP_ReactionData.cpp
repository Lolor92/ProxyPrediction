#include "Prediction/Data/PP_ReactionData.h"

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

	// Prefer an exact child reaction (for example Trigger.Flinch.Blocking) before
	// falling back to a parent definition such as Trigger.Flinch. This keeps the
	// result independent of the order of entries in the data asset.
	for (const FPP_ReactionDataEntry& Entry : Reactions)
	{
		if (Entry.ReactionTag == ReactionTag)
		{
			return &Entry;
		}
	}

	for (const FPP_ReactionDataEntry& Entry : Reactions)
	{
		if (Entry.ReactionTag.IsValid() && ReactionTag.MatchesTag(Entry.ReactionTag))
		{
			return &Entry;
		}
	}

	return nullptr;
}

bool UPP_ReactionData::ResolveReaction(
	FGameplayTag RequestedReactionTag,
	const FGameplayTagContainer& TargetOwnedTags,
	FGameplayTag& OutResolvedReactionTag,
	FPP_ReactionDataEntry& OutReaction,
	bool& bOutPreserveOriginalTransform) const
{
	OutResolvedReactionTag = RequestedReactionTag;
	bOutPreserveOriginalTransform = true;

	for (const FPP_ReactionTargetTagOverride& Override : TargetTagOverrides)
	{
		if (!Override.ReplacementReactionTag.IsValid() ||
			Override.IncomingReactionTags.IsEmpty() ||
			!Override.IncomingReactionTags.HasTag(RequestedReactionTag) ||
			!TargetOwnedTags.HasAll(Override.RequiredTargetTags) ||
			TargetOwnedTags.HasAny(Override.BlockedTargetTags))
		{
			continue;
		}

		OutResolvedReactionTag = Override.ReplacementReactionTag;
		bOutPreserveOriginalTransform = Override.bPreserveOriginalTransform;
		break;
	}

	return FindReaction(OutResolvedReactionTag, OutReaction);
}


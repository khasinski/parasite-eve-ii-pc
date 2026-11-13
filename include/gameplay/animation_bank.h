#ifndef GAMEPLAY_ANIMATION_BANK_H
#define GAMEPLAY_ANIMATION_BANK_H
#include <stddef.h>
#include "gameplay/animation_types.h"

/* A borrowed runtime directory, separate from its original disc extent. */
typedef struct {
    AnimationSet** sets;
    size_t count;
} AnimationBankView;

#define ANIMATION_BANK_VIEW(array) { (array), sizeof(array) / sizeof((array)[0]) }

static inline int animationBankReplace(AnimationBankView bank, size_t slot, AnimationSet* set)
{
    if (slot >= bank.count) return 0;
    bank.sets[slot] = set;
    return 1;
}
#endif

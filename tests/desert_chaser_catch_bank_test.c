#include "gameplay/animation.h"
#include "gameplay/animation_bank.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Clip identities stand in for disc data; the production directories are real. */
static AnimationSet _gStingerPuppetPlayerKnockbackFrontClip;
static AnimationSet _gStingerPuppetPlayerKnockbackRecoverFrontClip;
static AnimationSet _gStingerPuppetPlayerKnockbackBackClip;
static AnimationSet _gStingerPuppetPlayerKnockbackRecoverBackClip;
static AnimationSet _gAyaHitHeadClip;
static AnimationSet _gAyaHitBodyClip;
#include "assets/rooms/dryfield/water_tower/desert_chaser_player_front_catch_bank.inc"
#include "assets/rooms/dryfield/water_tower/desert_chaser_player_rear_catch_bank.inc"

int main(void)
{
    AnimationBankView banks[] = {
        ANIMATION_BANK_VIEW(_gDesertChaserFrontCatchPlayerAnimSets),
        ANIMATION_BANK_VIEW(_gDesertChaserRearCatchPlayerAnimSets),
    };
    AnimationSet recovery = {0}, release = {0};
    for (unsigned i = 0; i < 2; ++i) {
        AnimationBankView bank = banks[i];
        assert(bank.count == 6 && bank.sets[0] == NULL);
        AnimationSet* initial[6];
        memcpy(initial, bank.sets, sizeof initial);
        assert(animationBankReplace(bank, 4, &recovery));
        assert(animationBankReplace(bank, 5, &release));
        assert(bank.sets[4] == &recovery && bank.sets[5] == &release);
        assert(!memcmp(initial, bank.sets, 4 * sizeof(*initial)));
        assert(!animationBankReplace(bank, bank.count, &release));
        assert(!animationBankReplace(bank, (size_t)-1, &release));
        memcpy(bank.sets, initial, sizeof initial);
    }
    assert(_gDesertChaserFrontCatchPlayerAnimSets[1] == &_gStingerPuppetPlayerKnockbackFrontClip);
    assert(_gDesertChaserRearCatchPlayerAnimSets[1] == &_gStingerPuppetPlayerKnockbackBackClip);
    puts("Desert Chaser: front/rear recovery and release slots remain inside independent banks");
    return 0;
}

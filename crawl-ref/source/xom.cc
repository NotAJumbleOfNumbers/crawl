/*
 *  File:       xom.cc
 *  Summary:    All things Xomly
 *  Written by: Zooko
 *
 *  Modified for Crawl Reference by $Author$ on $Date$
 */

#include "AppHdr.h"

#include "beam.h"
#include "branch.h"
#include "effects.h"
#include "it_use2.h"
#include "items.h"
#include "makeitem.h"
#include "message.h"
#include "misc.h"
#include "mon-util.h"
#include "monplace.h"
#include "monstuff.h"
#include "mutation.h"
#include "ouch.h"
#include "player.h"
#include "randart.h"
#include "religion.h"
#include "spells3.h"
#include "spl-cast.h"
#include "spl-util.h"
#include "state.h"
#include "stuff.h"
#include "view.h"
#include "xom.h"

#if DEBUG_RELIGION
#    define DEBUG_DIAGNOSTICS 1
#    define DEBUG_GIFTS       1
#endif

#if DEBUG_XOM
#    define DEBUG_DIAGNOSTICS 1
#    define DEBUG_RELIGION    1
#    define DEBUG_GIFTS       1
#endif

// Which spell? First I copied all spells from you_spells(), then I
// filtered some out (especially conjurations). Then I sorted them in
// roughly ascending order of power. ([ds] Removed SUMMON_GUARDIAN and
// SUMMON_DAEVA which are inappropriate for a god of chaos; need to
// investigate substitutes).
// 
static const spell_type xom_spells[] =
{
    SPELL_BLINK, SPELL_CONFUSING_TOUCH, SPELL_MAGIC_MAPPING,
    SPELL_DETECT_ITEMS, SPELL_DETECT_CREATURES, SPELL_MASS_CONFUSION,
    SPELL_MASS_SLEEP, SPELL_CONTROLLED_BLINK, SPELL_STONESKIN,
    SPELL_RING_OF_FLAMES, SPELL_OLGREBS_TOXIC_RADIANCE,
    SPELL_TUKIMAS_VORPAL_BLADE, SPELL_FIRE_BRAND, SPELL_FREEZING_AURA,
    SPELL_POISON_WEAPON, SPELL_STONEMAIL, SPELL_WARP_BRAND, SPELL_ALTER_SELF,
    SPELL_TUKIMAS_DANCE, SPELL_SUMMON_BUTTERFLIES, SPELL_SUMMON_SMALL_MAMMAL,
    SPELL_CALL_IMP, SPELL_SUMMON_SCORPIONS, SPELL_FLY, SPELL_SPIDER_FORM,
    SPELL_STATUE_FORM, SPELL_ICE_FORM, SPELL_DRAGON_FORM, SPELL_SWARM,
    SPELL_SUMMON_WRAITHS, SPELL_SHADOW_CREATURES, SPELL_SUMMON_ELEMENTAL,
    SPELL_SUMMON_HORRIBLE_THINGS, SPELL_SUMMON_LARGE_MAMMAL,
    SPELL_CONJURE_BALL_LIGHTNING, SPELL_SUMMON_DRAGON, SPELL_DEATH_CHANNEL,
    SPELL_NECROMUTATION
};

static const char *xom_try_this[] =
{
    "\"Perhaps you should try this instead.\"",
    "\"Maybe this would work better.\"",
    "\"Catch!\""
};

static const char *xom_try_this_ring[] =
{
    "\"Try this.\"",
    "\"Catch!\"",
    "\"Take this!\""
};

static const char *xom_try_this_other_thing[] =
{
    "\"Perhaps you should try this instead.\"",
    "\"Have you considered using one of these?\"",
    "\"How about this?\""
};

static const char *xom_try_these_duds[] =
{
    "\"Perhaps you should try this instead.\"",
    "\"Have you considered wearing one of these?\"",
    "\"Here you go.\""
};

static const char *xom_generic_beneficence[] =
{
    "Xom grants you a gift!",
    "\"Here.\"",
    "Xom's generous nature manifests itself.",
    "Xom grants you an implement of some kind.",
    "\"Take this instrument of something!\"",
    "\"Take this token of my esteem.\"",
    "Xom smiles on you."
};

const char *describe_xom_favour()
{
    return (you.piety > 160) ? "A beloved toy of Xom." : 
        (you.piety > 145) ? "A favourite toy of Xom." : 
        (you.piety > 130) ? "A very special toy of Xom." : 
        (you.piety > 115) ? "A special toy of Xom." : 
        (you.piety > 100) ? "A toy of Xom." : 
        (you.piety >  85) ? "A plaything of Xom." : 
        (you.piety >  70) ? "A special plaything of Xom." : 
        (you.piety >  55) ? "A very special plaything of Xom." : 
        (you.piety >  40) ? "A favourite plaything of Xom." : 
                            "A beloved plaything of Xom.";
}

bool xom_is_nice()
{
    // If you.gift_timeout was == 0, then Xom was BORED.
    // He HATES that.
    return (you.gift_timeout > 0 && you.piety > 100) || coinflip();
}

static const char* xom_message_arrays[NUM_XOM_MESSAGE_TYPES][6] =
{
    // XM_NORMAL
    {
        "Xom roars with laughter!",
        "Xom thinks this is hilarious!",
        "Xom is highly amused!",
        "Xom is amused.",
        "Xom is mildly amused.",
        "Xom is interested."
    },

    // XM_INTRIGUED
    {
        "Xom is fascinated!",
        "Xom is very intrigued!",
        "Xom is intrigued!",
        "Xom is extremely interested.",
        "Xom is very interested.",
        "Xom is interested."
    }
};

static void _xom_is_stimulated(int maxinterestingness,
                               const char* message_array[],
                               bool force_message)
{
    if (you.religion != GOD_XOM || maxinterestingness <= 0)
        return;

    // Xom is not stimulated by his own acts, at least not directly.
    if (crawl_state.which_god_acting() == GOD_XOM)
        return;

    int interestingness = random2(maxinterestingness);

#if DEBUG_RELIGION || DEBUG_GIFTS || DEBUG_XOM
    mprf(MSGCH_DIAGNOSTICS,
         "Xom: maxinterestingness = %d, interestingness = %d",
         maxinterestingness, interestingness);
#endif

    if (interestingness > 255)
        interestingness = 255;

    bool was_stimulated = false;
    if (interestingness > you.gift_timeout && interestingness >= 12)
    {
        you.gift_timeout = interestingness;
        was_stimulated = true;
    }

    if (was_stimulated || force_message)
        god_speaks(GOD_XOM,
                   ((interestingness > 200) ? message_array[5] :
                    (interestingness > 100) ? message_array[4] :
                    (interestingness > 75) ? message_array[3] :
                    (interestingness > 50) ? message_array[2] :
                    (interestingness > 25) ? message_array[1] :
                    message_array[0]));
}

void xom_is_stimulated(int maxinterestingness, xom_message_type message_type,
                       bool force_message)
{
    _xom_is_stimulated(maxinterestingness, xom_message_arrays[message_type],
                       force_message);
}

void xom_is_stimulated(int maxinterestingness, const std::string& message,
                       bool force_message)
{
    const char* message_array[6];

    for (int i = 0; i < 6; i++)
        message_array[i] = message.c_str();

    _xom_is_stimulated(maxinterestingness, message_array, force_message);
}

void xom_makes_you_cast_random_spell(int sever)
{
    int spellenum = sever;

    god_acting gdact(GOD_XOM);

    const int nxomspells = ARRAYSIZE(xom_spells);
    if (spellenum >= nxomspells)
        spellenum = nxomspells - 1;
    
    const spell_type spell = xom_spells[random2(spellenum)];

    god_speaks(GOD_XOM, "Xom's power flows through you!");
    
#if DEBUG_DIAGNOSTICS || DEBUG_RELIGION || DEBUG_XOM
    mprf(MSGCH_DIAGNOSTICS,
         "Xom_acts();spell: %d, spellenum: %d", spell, spellenum);
#endif

    your_spells(spell, sever, false);
}

static void xom_make_item(object_class_type base,
                          int subtype,
                          int power,
                          const char *failmsg = "\"No, never mind.\"")
{
    int thing_created =
        items(true, base, subtype, true, power, MAKE_ITEM_RANDOM_RACE);
                
    if (thing_created == NON_ITEM)
    {
        god_speaks(GOD_XOM, failmsg);
        return;
    }

    god_acting gdact(GOD_XOM);

    move_item_to_grid( &thing_created, you.x_pos, you.y_pos );
    canned_msg(MSG_SOMETHING_APPEARS);
    stop_running();

    origin_acquired(mitm[thing_created], GOD_XOM);
}

static object_class_type get_unrelated_wield_class(object_class_type ref)
{
    object_class_type objtype = OBJ_WEAPONS;
    if (ref == OBJ_WEAPONS)
    {
        if (random2(10))
            objtype = OBJ_STAVES;
        else
            objtype = OBJ_MISCELLANY;
    }
    else if (ref == OBJ_STAVES)
    {
        if (random2(10))
            objtype = OBJ_WEAPONS;
        else
            objtype = OBJ_MISCELLANY;
    }
    else
    {
        const int temp_rand = random2(3);
        objtype =
            (temp_rand == 0) ? OBJ_WEAPONS :
            (temp_rand == 1) ? OBJ_STAVES :
            OBJ_MISCELLANY;
    }

    return (objtype);
}

static bool xom_annoyance_gift(int power)
{
    god_acting gdact(GOD_XOM);

    if (coinflip() && player_in_a_dangerous_place())
    {
        const item_def *weapon = you.weapon();
        // Xom has a sense of humour.
        if (coinflip() && weapon && weapon->cursed())
        {
            // If you are wielding a cursed item then Xom will give
            // you an item of that same type. Ha ha!
            god_speaks(GOD_XOM, RANDOM_ELEMENT(xom_try_this));
            if (coinflip())
                // For added humour, give the same sub-type.
                xom_make_item(weapon->base_type, weapon->sub_type, power * 3);
            else
                acquirement(weapon->base_type, GOD_XOM);
            return (true);
        }

        const item_def *gloves = you.slot_item(EQ_GLOVES);
        if (coinflip() && gloves && gloves->cursed())
        {
            // If you are wearing cursed gloves then Xom will give you
            // a ring. Ha ha!
            // 
            // A random ring.  (Not necessarily a good one.)
            god_speaks(GOD_XOM, RANDOM_ELEMENT(xom_try_this));
            xom_make_item(OBJ_JEWELLERY, get_random_ring_type(), power * 3);
            return (true);
        };

        const item_def *amulet = you.slot_item(EQ_AMULET);
        if (coinflip() && amulet && amulet->cursed())
        {
            // If you are wearing a cursed amulet then Xom will give
            // you an amulet. Ha ha!
            god_speaks(GOD_XOM, RANDOM_ELEMENT(xom_try_this));
            xom_make_item(OBJ_JEWELLERY, get_random_amulet_type(), power * 3);
            return (true);
        };

        const item_def *left_ring = you.slot_item(EQ_LEFT_RING);
        const item_def *right_ring = you.slot_item(EQ_RIGHT_RING);
        if (coinflip() && ((left_ring && left_ring->cursed())
                           || (right_ring && right_ring->cursed())))
        {
            // If you are wearing a cursed ring then Xom will give you
            // a ring. Ha ha!
            god_speaks(GOD_XOM, RANDOM_ELEMENT(xom_try_this_ring));
            xom_make_item(OBJ_JEWELLERY, get_random_ring_type(), power * 3);
            return (true);
        }

        if (one_chance_in(5) && weapon)
        {
            // Xom will give you a wielded item of a type different
            // than what you are currently wielding.
            god_speaks(GOD_XOM, RANDOM_ELEMENT(xom_try_this_other_thing));

            const object_class_type objtype =
                get_unrelated_wield_class(weapon->base_type);
            
            if (power > random2(256))
                acquirement(objtype, GOD_XOM);
            else
                xom_make_item(objtype, OBJ_RANDOM, power * 3);
            return (true);
        }
    }

    return (false);
}

bool xom_gives_item(int power)
{
    if (xom_annoyance_gift(power))
        return (true);

    const item_def *cloak = you.slot_item(EQ_CLOAK);
    if (coinflip() && cloak && cloak->cursed())
    {
        // If you are wearing a cursed cloak then Xom will give you a
        // cloak or body armour . Ha ha!
        god_speaks(GOD_XOM, RANDOM_ELEMENT(xom_try_these_duds));
        xom_make_item(OBJ_ARMOUR,
                      random2(10)?
                          get_random_body_armour_type(you.your_level * 2)
                        : ARM_CLOAK,
                      power * 3);
        return (true);
    }

    god_speaks(GOD_XOM, RANDOM_ELEMENT(xom_generic_beneficence));

    // There are two kinds of Xom gifts: acquirement and random
    // object. The result from acquirement is very good (usually as
    // good or better than random object), and it is sometimes tuned
    // to the player's skills and nature. Being tuned to the player's
    // skills and nature is not very Xomlike...
    if (power > random2(256))
    {
        // random-type acquirement
        const int r = random2(7);
        const object_class_type objtype = (r == 0) ? OBJ_WEAPONS :
            (r == 1) ? OBJ_ARMOUR :
            (r == 2) ? OBJ_JEWELLERY :
            (r == 3) ? OBJ_BOOKS :
            (r == 4) ? OBJ_STAVES :
            (r == 5) ? OBJ_FOOD :
            (r == 6) ? OBJ_MISCELLANY :
                       OBJ_GOLD;

        god_acting gdact(GOD_XOM);
        acquirement(objtype, GOD_XOM);
    }
    else
    {
        // random-type random object
        xom_make_item(OBJ_RANDOM, OBJ_RANDOM, power * 3);
    }
    more();

    return (true);
}

bool there_are_monsters_nearby()
{
    int ystart = you.y_pos - 9, xstart = you.x_pos - 9;
    int yend = you.y_pos + 9, xend = you.x_pos + 9;
    if (xstart < 0) xstart = 0;
    if (ystart < 0) ystart = 0;
    if (xend >= GXM) xend = GXM;
    if (ystart >= GYM) yend = GYM;

    /* monster check */
    for ( int y = ystart; y < yend; ++y )
    {
        for ( int x = xstart; x < xend; ++x )
        {
            /* if you can see a nonfriendly monster then you feel
               unsafe */
            if ( see_grid(x,y) )
            {
                const int targ_monst = mgrd[x][y];
                if ( targ_monst != NON_MONSTER )
                {
                    const monsters *mon = &menv[targ_monst];
                    if (!mons_is_submerged(mon))
                        return (true);
                }
            }
        }
    }
    return (false);
}

monsters *get_random_nearby_monster()
{
    monsters *monster = NULL;
    /* not particular efficient, but oh well */
    for (int it = 0, num = 0; it < MAX_MONSTERS; it++)
    {
        monsters *mons = &menv[it];
        if (mons->alive() && mons_near(mons)
            && !mons_is_submerged(mons)
            && one_chance_in(++num))
        {
            monster = mons;
        }
    }
    return (monster);
}

static monster_type xom_random_demon(int sever, bool use_greater_demons = true)
{
    const int roll = random2(1000 - (27 - you.experience_level) * 10);
#ifdef DEBUG_DIAGNOSTICS
    mprf(MSGCH_DIAGNOSTICS, "xom_random_demon: sever = %d, roll: %d",
         sever, roll);
#endif    
    const demon_class_type dct =
        roll >= 850 ? DEMON_GREATER :
        roll >= 340 ? DEMON_COMMON  :
        DEMON_LESSER;
    const monster_type demontype =
        summon_any_demon(
            use_greater_demons || dct != DEMON_GREATER? dct : DEMON_COMMON);
    return (demontype);
}

// Returns a demon suitable for use in Xom's punishments, filtering out the
// really nasty ones early on.
static monster_type xom_random_punishment_demon(int sever)
{
    monster_type demon = MONS_PROGRAM_BUG;
    do
        demon = xom_random_demon(sever);
    while ((demon == MONS_HELLION
            && you.experience_level < 12
            && !one_chance_in(3 + (12 - you.experience_level) / 2)));
    return (demon);
}

static bool xom_is_good(int sever)
{
    // niceness = false - bad, true - nice
    int temp_rand;              // probability determination {dlb}
    bool done = false;

    bolt beam;
    
    // Okay, now for the nicer stuff (note: these things are not
    // necessarily nice):
    god_acting gdact(GOD_XOM);
    if (random2(sever) <= 1)
    {
        temp_rand = random2(4);
                
        god_speaks(GOD_XOM,
                   (temp_rand == 0) ? "\"Go forth and destroy!\"" :
                   (temp_rand == 1) ? "\"Go forth and cause havoc, mortal!\"" :
                   (temp_rand == 2) ? "Xom grants you a minor favour."
                   : "Xom smiles on you.");
                
        switch (random2(7))
        {
        case 0:
            potion_effect(POT_HEALING, 150);
            break;
        case 1:
            potion_effect(POT_HEAL_WOUNDS, 150);
            break;
        case 2:
            potion_effect(POT_SPEED, 150);
            break;
        case 3:
            potion_effect(POT_MIGHT, 150);
            break;
        case 4:
            potion_effect(POT_INVISIBILITY, 150);
            break;
        case 5:
            if (one_chance_in(6))
                potion_effect(POT_EXPERIENCE, 150);
            else
            {
                you.berserk_penalty = NO_BERSERK_PENALTY;
                potion_effect(POT_BERSERK_RAGE, 150);
            }
            break;
        case 6:
            you.berserk_penalty = NO_BERSERK_PENALTY;
            potion_effect(POT_BERSERK_RAGE, 150);
            break;
        }
                
        done = true;
    }
    else if (random2(sever) <= 2)
    {
        xom_makes_you_cast_random_spell(sever);
        done = true;
    }
    else if (random2(sever) <= 3)
    {
        temp_rand = random2(3);

        god_speaks(GOD_XOM,
                   (temp_rand == 0) ? "\"Serve the mortal, my children!\"" :
                   (temp_rand == 1) ? "Xom grants you some temporary aid."
                   : "Xom momentarily opens a gate.");
                
        int numdemons = std::min(random2(random2(random2(sever+1)+1)+1)+2, 16);
        for (int i = 0; i < numdemons; i++)
        {
            create_monster(xom_random_demon(sever), 3, BEH_GOD_GIFT,
                           you.x_pos, you.y_pos, you.pet_target, 250 );
        }
                
        done = true;
    }
    else if (random2(sever) <= 4)
    {
        xom_gives_item(sever);
        done = true;
    }
    else if (random2(sever) <= 5)
    {
        if (create_monster(xom_random_demon(sever), 6, BEH_GOD_GIFT,
                           you.x_pos, you.y_pos, you.pet_target, 250) != -1)
        {
            temp_rand = random2(3);
                    
            god_speaks(GOD_XOM,
                       (temp_rand == 0) ? "\"Serve the mortal, my child!\"" :
                       (temp_rand == 1) ? "\"Serve the toy, my child!\"" :
                       "Xom opens a gate.");
            done = true;
        }
    }
    else if ((random2(sever) <= 6) && there_are_monsters_nearby())
    {
        monsters* mon = get_random_nearby_monster();
        if (mon && mon->holiness() == MH_NATURAL)
        {
            temp_rand = random2(4);
                
            god_speaks(GOD_XOM,
                       (temp_rand == 0) ? "\"This might be better!\"" :
                       (temp_rand == 1) ? "\"Hum-dee-hum-dee-hum...\"" :
                       (temp_rand == 2) ?
                                     "Xom's power touches on a nearby monster."
                       : "You hear Xom's avuncular chuckle.");

            if (mons_friendly(mon))
                monster_polymorph(mon, RANDOM_MONSTER, PPT_MORE);
            else
                monster_polymorph(mon, RANDOM_MONSTER, PPT_LESS);
            done = true;
        }
    }
    else if (random2(sever) <= 7)
    {
        xom_gives_item(sever);
        done = true;
    }
    else if (!you.is_undead && random2(sever) <= 8)
    {
        temp_rand = random2(4);
        god_speaks(GOD_XOM,
                   (temp_rand == 0) ? "\"You need some minor adjustments, mortal!\"" :
                   (temp_rand == 1) ? "\"Let me alter your pitiful body.\"" :
                   (temp_rand == 2) ? "Xom's power touches on you for a moment."
                   : "You hear Xom's maniacal cackling.");
        mpr("Your body is suffused with distortional energy.");
                
        set_hp(1 + random2(you.hp), false);
        deflate_hp(you.hp_max / 2, true);
                
        bool failMsg = true;
        int i = 0;
        int j = random2(4);
        while (i < j)
        {
            if (mutate(RANDOM_GOOD_MUTATION, failMsg, true))
                done = true;
            else
                failMsg = false;
            i++;
        }
    }
    else if (random2(sever) <= 9)
    {
        if (create_monster( xom_random_demon(sever, one_chance_in(8)),
                            0, BEH_GOD_GIFT,
                            you.x_pos, you.y_pos, you.pet_target, 250 ) != -1)
        {
            temp_rand = random2(3);
            god_speaks(GOD_XOM,
                       (temp_rand == 0) ? "Xom grants you a demonic assistant."
                       : (temp_rand == 1) ? "Xom grants you a demonic servitor."
                       : "Xom opens a gate.");
            done = true;
        }
    }
    else if ((random2(sever) <= 10) && player_in_a_dangerous_place())
    {
        if (you.hp <= random2(201))
            you.attribute[ATTR_DIVINE_LIGHTNING_PROTECTION] = 1;

        mpr("The area is suffused with divine lightning!");
                
        beam.beam_source = NON_MONSTER;
        beam.type = SYM_BURST;
        beam.damage = dice_def( 3, 30 );
        beam.flavour = BEAM_ELECTRICITY;
        beam.target_x = you.x_pos;
        beam.target_y = you.y_pos;
        beam.name = "blast of lightning";
        beam.colour = LIGHTCYAN;
        beam.thrower = KILL_MISC;
        beam.aux_source = "Xom's lightning strike";
        beam.ex_size = 2;
        beam.is_tracer = false;
        beam.is_explosion = true;
        explosion(beam);
                
        if (you.attribute[ATTR_DIVINE_LIGHTNING_PROTECTION])
        {
            mpr("Your divine protection wanes.");
            you.attribute[ATTR_DIVINE_LIGHTNING_PROTECTION] = 0;
        }
                
        done = true;
    }

    return (done);
}

static bool xom_is_bad(int sever)
{
    // niceness = false - bad, true - nice
    int temp_rand;              // probability determination {dlb}
    bool done = false;

    bolt beam;

    god_acting gdact(GOD_XOM);

    // begin "Bad Things"
    while (!done)
    {
        if (random2(sever) <= 2)
        {
            temp_rand = random2(4);
                
            god_speaks(GOD_XOM,
                       (temp_rand == 0) ? "Xom almost notices you." :
                       (temp_rand == 1) ? "Xom's attention almost turns to you for a moment.":
                       (temp_rand == 2) ? "Xom's power almost touches on you for a moment."
                       : "You almost hear Xom's maniacal laughter.");
                
            miscast_effect( SPTYP_RANDOM, 0, 0, 0, "the mischief of Xom" );
                
            done = true;
        }
        else if (random2(sever) <= 3)
        {
            temp_rand = random2(4);
                
            god_speaks(GOD_XOM,
                       (temp_rand == 0) ? "Xom notices you." :
                       (temp_rand == 1) ? "Xom's attention turns to you for a moment.":
                       (temp_rand == 2) ? "Xom's power touches on you for a moment."
                       : "You hear Xom's maniacal laughter.");
                
            miscast_effect( SPTYP_RANDOM, 0, 0, random2(2), 
                            "the capriciousness of Xom" );
                
            done = true;
        }
        else if (random2(sever) <= 4)
        {
            temp_rand = random2(4);
                
            god_speaks(GOD_XOM,
                       (temp_rand == 0) ? "\"Suffer!\"" :
                       (temp_rand == 1) ? "Xom's malign attention turns to you for a moment." :
                       (temp_rand == 2) ? "Xom's power touches on you for a moment."
                       : "You hear Xom's maniacal laughter.");
                
            lose_stat(STAT_RANDOM, 1 + random2(3), true,
                      "the capriciousness of Xom" );
                
            done = true;
        }
        else if (random2(sever) <= 5)
        {
            temp_rand = random2(4);
                
            god_speaks(GOD_XOM,
                       (temp_rand == 0) ? "Xom notices you." :
                       (temp_rand == 1) ? "Xom's attention turns to you for a moment.":
                       (temp_rand == 2) ? "Xom's power touches on you for a moment."
                       : "You hear Xom's maniacal laughter.");
                
            miscast_effect( SPTYP_RANDOM, 0, 0, random2(3), 
                            "the capriciousness of Xom" );
                
            done = true;
        }
        else if (!you.is_undead && random2(sever) <= 6)
        {
            temp_rand = random2(4);
            god_speaks(GOD_XOM,
                       (temp_rand == 0) ? "\"You need some minor improvements, mortal!\"" :
                       (temp_rand == 1) ? "\"Let me alter your body.\"" :
                       (temp_rand == 2) ? "Xom's power brushes against you for a moment."
                       : "You hear Xom's avuncular chuckle.");
            mpr("Your body is suffused with distortional energy.");
                
            set_hp(1 + random2(you.hp), false);
            deflate_hp(you.hp_max / 2, true);
                
            bool failMsg = true;
            for (int i = 0; i < random2(4)+1; i++)
            {
                if (mutate(RANDOM_XOM_MUTATION, failMsg, true))
                    done = true;
                else
                    failMsg = false;
            }
        }
        else if ((random2(sever) <= 7) && there_are_monsters_nearby())
        {
            monsters* mon = get_random_nearby_monster();
            ASSERT (mon != NULL);
            if ( mon->holiness() == MH_NATURAL )
            {
                temp_rand = random2(4);
                
                god_speaks(GOD_XOM,
                           (temp_rand == 0) ? "\"This might be better!\"" :
                           (temp_rand == 1) ? "\"Hum-dee-hum-dee-hum...\"" :
                           (temp_rand == 2) ? "Xom's power touches on a nearby monster."
                           : "You hear Xom's avuncular chuckle.");

                if (mons_friendly(mon))
                    monster_polymorph(mon, RANDOM_MONSTER, PPT_LESS);
                else
                    monster_polymorph(mon, RANDOM_MONSTER, PPT_MORE);
                done = true;
            }
        }
        else if (!you.is_undead && random2(sever) <= 8)
        {
            temp_rand = random2(4);
                
            god_speaks(GOD_XOM,
                       (temp_rand == 0) ? "\"You have displeased me, mortal.\"" :
                       (temp_rand == 1) ? "\"You have grown too confident for your meagre worth.\"" :
                       (temp_rand == 2) ? "Xom's power touches on you for a moment."
                       : "You hear Xom's maniacal laughter.");
                
            if (one_chance_in(4))
            {
                drain_exp();
                if (random2(sever) > 3)
                    drain_exp();
                if (random2(sever) > 3)
                    drain_exp();
            }
            else
            {
                mpr("A wave of agony tears through your body!");
                set_hp(1 + (you.hp / 2), false);
            }
                
            done = true;
        }
        else if (random2(sever) <= 9)
        {
            temp_rand = random2(4);
                
            god_speaks(GOD_XOM,
                       (temp_rand == 0) ? "\"Time to have some fun!\"" :
                       (temp_rand == 1) ? "\"Fight to survive, mortal.\"" :
                       (temp_rand == 2) ? "\"Let's see if it's strong enough to survive yet.\""
                       : "You hear Xom's maniacal laughter.");
                
            if (one_chance_in(4))
                dancing_weapon(100, true);      // nasty, but fun
            else
            {
                const int numdemons =
                    std::min(random2(random2(random2(sever+1)+1)+1)+1, 14);
                for (int i = 0; i < numdemons; i++)
                {
                    create_monster(
                        xom_random_punishment_demon(sever),
                        4, BEH_HOSTILE, you.x_pos, you.y_pos,
                        MHITNOT, 250);
                }
            }
                
            done = true;
        }
        else if (random2(sever) <= 10)
        {
            temp_rand = random2(4);
                
            god_speaks(GOD_XOM,
                       (temp_rand == 0) ? "\"Try this!\"" :
                       (temp_rand == 1) ? "Xom's attention turns to you.":
                       (temp_rand == 2) ? "Xom's power touches on you."
                       : "Xom giggles.");
                
            miscast_effect( SPTYP_RANDOM, 0, 0, random2(4), 
                            "the severe capriciousness of Xom" );
                
            done = true;
        }
        else if (one_chance_in(sever) && (you.level_type != LEVEL_ABYSS))
        {
            temp_rand = random2(3);
                
            god_speaks(GOD_XOM,
                       (temp_rand == 0) ? "\"You have grown too comfortable in your little world, mortal!\"" :
                       (temp_rand == 1) ? "Xom casts you into the Abyss!"
                       : "The world seems to spin as Xom's maniacal laughter rings in your ears.");
                
            banished(DNGN_ENTER_ABYSS, "Xom");
                
            done = true;
        }
    }

    return (done);
}

void xom_acts(bool niceness, int sever)
{
#if DEBUG_DIAGNOSTICS || DEBUG_RELIGION || DEBUG_XOM
    mprf(MSGCH_DIAGNOSTICS, "Xom_acts(%u, %d); piety: %u, interest: %u\n",
         niceness, sever, you.piety, you.gift_timeout);
#endif

    entry_cause_type old_entry_cause = you.entry_cause;

    if (sever < 1)
        sever = 1;

    // Nemelex's deck of punishment drawing the Xom card
    if (crawl_state.is_god_acting()
        && crawl_state.which_god_acting() != GOD_XOM)
    {
        god_type which_god = crawl_state.which_god_acting();

        if (crawl_state.is_god_retribution())
        {
            niceness = false;
            mprf(MSGCH_GOD, which_god,
                 "%s asks Xom for help in punishing you, and Xom happily "
                 "agrees.", god_name(which_god));
        }
        else
        {
            niceness = true;
            mprf(MSGCH_GOD, which_god,
                 "%s calls in a favour from Xom.", god_name(which_god));
        }
    }

    if (niceness && !one_chance_in(5))
    {
        // Good stuff.
        while (!xom_is_good(sever))
            ;
    }
    else
    {
        // Bad mojo.
        while (!xom_is_bad(sever))
            ;
    }

    // Nemelex's deck of punishment drawing the Xom card
    if (crawl_state.is_god_acting()
        && crawl_state.which_god_acting() != GOD_XOM)
    {
        if (old_entry_cause != you.entry_cause
            && you.entry_cause_god == GOD_XOM)
        {
            you.entry_cause_god = crawl_state.which_god_acting();
        }
    }
    
    if (you.religion == GOD_XOM && coinflip())
        you.piety = 200 - you.piety;
}

static void xom_check_less_runes(int runes_gones)
{
    if (player_in_branch(BRANCH_HALL_OF_ZOT) ||
        !(branches[BRANCH_HALL_OF_ZOT].branch_flags & BFLAG_HAS_ORB))
    {
        return;
    }

    int runes_avail = you.attribute[ATTR_UNIQUE_RUNES]
        + you.attribute[ATTR_DEMONIC_RUNES]
        + you.attribute[ATTR_ABYSSAL_RUNES]
        - you.attribute[ATTR_RUNES_IN_ZOT];
    int was_avail = runes_avail + runes_gones;

    // No longer enough available runes to get into Zot
    if (was_avail >= NUMBER_OF_RUNES_NEEDED &&
        runes_avail < NUMBER_OF_RUNES_NEEDED)
    {
        xom_is_stimulated(128, "Xom snickers.", true);
    }
}

void xom_check_lost_item(const item_def& item)
{
    if (item.base_type == OBJ_ORBS)
        xom_is_stimulated(255, "Xom laughs nastily.", true);
    else if (is_fixed_artefact(item))
        xom_is_stimulated(128, "Xom snickers.", true);
    else if (is_rune(item))
    {
        // If you'd dropped it, check if that means you'd dropped your
        // third rune and now don't have enough to get into Zot.
        if (item.flags & ISFLAG_BEEN_IN_INV)
            xom_check_less_runes(item.quantity);

        if (is_unique_rune(item))
            xom_is_stimulated(255, "Xom snickers loudly.", true);
        else if (you.entry_cause == EC_SELF_EXPLICIT &&
                 !(item.flags & ISFLAG_BEEN_IN_INV))
        {
            // Player voluntarily entered Pan or the Abyss looking for
            // runes, yet never found it.
            if (item.plus == RUNE_ABYSSAL &&
                you.attribute[ATTR_ABYSSAL_RUNES] == 0)
            {
                // Ignore Abyss area shifts.
                if (you.level_type != LEVEL_ABYSS)
                    // Abyssal runes are a lot more trouble to find
                    // than demonic runes, so it gets twice the
                    // stimulation.
                    xom_is_stimulated(128, "Xom snickers.", true);
            }
            else if (item.plus == RUNE_DEMONIC &&
                     you.attribute[ATTR_DEMONIC_RUNES] == 0)
            {
                xom_is_stimulated(64, "Xom snickers softly.", true);
            }
        }
    }
}

void xom_check_destroyed_item(const item_def& item, int cause)
{
    int amusement = 0;

    if (item.base_type == OBJ_ORBS)
    {
        xom_is_stimulated(255, "Xom laughs nastily.", true);
        return;
    }
    else if (is_fixed_artefact(item))
        xom_is_stimulated(128, "Xom snickers.", true);
    else if (is_rune(item))
    {
        xom_check_less_runes(item.quantity);

        if (is_unique_rune(item) || item.plus == RUNE_ABYSSAL)
            amusement = 255;
        else
            amusement = 64 * item.quantity;
    }

    xom_is_stimulated(amusement,
                      amusement > 128 ? "Xom snickers loudly." :
                      amusement > 64  ? "Xom snickers." :
                      "Xom snickers softly.",
                      true);
}

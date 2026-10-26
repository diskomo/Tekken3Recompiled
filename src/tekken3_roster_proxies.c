/* Test fighters for the extra roster. Each borrows a stock character's models,
 * moves and art under its own character ID, so the roster, selectors and
 * fights can be exercised without any imported assets. */
#include "mod_plugins.h"
#include "tekken3_roster.h"
#include <stdlib.h>

static void tekken3_roster_proxies_activate(void)
{
    /* Paul, Law, Lei, King, Nina, Hwoarang, Jin, Julia, Bryan. */
    static const unsigned donors[TEKKEN3_ROSTER_MAX_EXTRA] = {0, 1, 2, 3, 5, 6, 9, 10, 12};
    char value[16];
    unsigned count = 7;
    if (psx_mod_option_value("tekken3.dev.roster-proxies", "roster-proxies", "count",
                             value, sizeof value))
        count = (unsigned)strtoul(value, NULL, 10);
    if (count > TEKKEN3_ROSTER_MAX_EXTRA) count = TEKKEN3_ROSTER_MAX_EXTRA;
    for (unsigned i = 0; i < count; i++)
        (void)tekken3_roster_add_proxy(donors[i]);
}

PSX_MOD_CONSTRUCTOR(tekken3_register_roster_proxies)
{
    (void)psx_mod_register_activation_plugin("tekken3.roster-proxies",
                                             tekken3_roster_proxies_activate);
}

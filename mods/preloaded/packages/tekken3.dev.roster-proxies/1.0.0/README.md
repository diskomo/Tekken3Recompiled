# Roster Test Fighters

A developer package, disabled by default, for the verified Tekken 3 USA
executable (SLUS-00402). It adds up to nine stand-in fighters through the
shared extra-roster layer (`src/tekken3_roster.c`). Each stand-in has its own
character ID from 23 upwards but borrows a stock fighter's models, moves,
portraits, name and tuning: Paul, Law, Lei, King, Nina, Hwoarang, Jin, Julia
and Bryan, in that order. No imported assets are needed.

The game keeps character availability in 32-bit masks, so IDs 23 to 31 are
the most that fit.

- **Arcade, Practice and the other cabinet-style modes** keep two rows. At
  4:3 they have eleven columns, which leaves room for one stand-in; with True
  Widescreen they grow to fourteen columns, so seven stand-ins are visible.
- **VS and Team Battle** keep three rows and grow to eight, nine or ten
  columns, so every stand-in fits at either aspect.

Stand-ins open the costume carousel with their donor's original outfits.
They count towards their donor's saved usage statistics, because the
memory-card format has only 22 rows, and they are not yet chosen as CPU
opponents.

#ifndef QOL_ENCOUNTERS_H
#define QOL_ENCOUNTERS_H

/*
 * Final Fantasy VI — quality of life: random-encounter switch.
 *
 * The game already owns this switch. Encounter rate is chosen from a 4-row
 * table of step incrementors, indexed by bits 0-1 of $7E11DF:
 *
 *   $C0:C29F (world map) / $C0:C2BF (dungeons), 4 rows x 4 random variants
 *     row 0 (bits 00)  normal      192,  96, 384,   0 / 112, 64, 352, 512
 *     row 1 (bit  0)   Charm Bangle 96,  48, 192,   0 /  56, 32, 176, 256
 *     row 2 (bit  1)   ALL ZERO  -> encounters do not happen
 *     row 3 (bits 11)  ALL ZERO  -> encounters do not happen
 *
 * Bit 1 is the Moogle Charm's flag: vanilla sets it while the relic is in the
 * party. Writing it from the host disables encounters without patching a
 * single ROM byte, so it composes with any hack that keeps the vanilla check,
 * and it is host state, not guest state, so rewind and save states cannot
 * lose the setting.
 *
 * Measured: same battery save and route, 7,751 frames. The control reached a
 * Narshe battle; with bit 1 forced, the run stayed on the field throughout.
 * 0 abandons, 0 crashes in both.
 */

void QolAfterConfig(void);   /* read qol.ini beside the executable */
void QolBeforeFrame(void);   /* one guest frame about to run: apply + hotkey */
int QolEncountersDisabled(void);

#endif /* QOL_ENCOUNTERS_H */

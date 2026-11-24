/*
 * Final Fantasy VI — quality of life settings file (qol.ini).
 *
 * One file, several features. Encounters owned it outright until widescreen
 * arrived, and its writer rewrote the whole file: flipping the encounter
 * switch would have deleted the widescreen setting next to it. So the file
 * has one owner now, and features ask it for their keys.
 *
 * Format is the same flat `Key = value` list under `[QoL]` that players
 * already have. Edits preserve every other line -- comments, unknown keys,
 * and keys belonging to features this build does not have -- because a
 * settings file that silently loses lines is worse than no settings file.
 */

#ifndef QOL_INI_H
#define QOL_INI_H

#include <stddef.h>

/* Absolute path of qol.ini beside the executable (falls back to the CWD). */
void QolIniPath(char *out, size_t cap);

/* Write the documented default file if none exists. Never overwrites. */
void QolIniEnsureDefaults(void);

/* Read a key. Returns 1 when the key was present (out holds its value). */
int QolIniGet(const char *key, char *out, size_t cap);

/* Write a key, replacing its value in place or appending it to [QoL]. */
void QolIniSet(const char *key, const char *value);

#endif /* QOL_INI_H */

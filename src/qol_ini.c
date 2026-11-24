#include "qol_ini.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desktop/sdl_compat.h" /* SDL_GetBasePath / SDL_free */

#define QOL_INI_NAME "qol.ini"
#define QOL_INI_MAX  (16 * 1024) /* the file is a dozen lines */

#if defined(_WIN32) && defined(_MSC_VER)
#define strcasecmp _stricmp
#endif

static const char kDefaultText[] =
    "# Final Fantasy VI - quality of life settings.\n"
    "#\n"
    "# Encounters   on = vanilla random battles, off = disabled.\n"
    "#              EncountersKey flips it in game.\n"
    "# Widescreen   off = authentic 4:3 (the verified baseline),\n"
    "#              on = 384x224 field view. Menus, battles, the world map and\n"
    "#              cutscenes stay 4:3 and are pillarboxed inside the wider\n"
    "#              frame; only the field widens.\n"
    "# Keys: any SDL key name (Grave, F10, ...), or None.\n"
    "\n"
    "[QoL]\n"
    "Encounters    = on\n"
    "EncountersKey = Grave\n"
    "Widescreen    = off\n"
    "WidescreenKey = F10\n";

/* File-scope scratch: the file is a few hundred bytes, and an edit path with
 * no allocation cannot fail on the one run where memory is short. */
static char s_text[QOL_INI_MAX];
static char s_next[QOL_INI_MAX];

/* Resolved once. SDL_GetBasePath allocates through SDL's allocator, and this
 * runs from after_config -- before SDL_Init -- so asking it repeatedly and
 * freeing each answer is asking for an uninitialised allocator to be used
 * twice. One call, one cached string. */
void QolIniPath(char *out, size_t cap)
{
  static char resolved[1024];
  if (!resolved[0]) {
    char *base = SDL_GetBasePath();
    if (base) {
      snprintf(resolved, sizeof resolved, "%s%s", base, QOL_INI_NAME);
      SDL_free(base);
    } else {
      snprintf(resolved, sizeof resolved, "%s", QOL_INI_NAME);
    }
  }
  snprintf(out, cap, "%s", resolved);
}

static char *Trim(char *s)
{
  while (*s == ' ' || *s == '\t' || *s == '\r') s++;
  char *end = s + strlen(s);
  while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                     end[-1] == '\r' || end[-1] == '\n'))
    *--end = '\0';
  return s;
}

/* "key = value" -> 1 with *key/*val set, 0 for blanks, comments, sections. */
static int SplitKey(char *line, char **key, char **val)
{
  char *s = Trim(line);
  if (!*s || *s == '#' || *s == ';' || *s == '[') return 0;
  char *eq = strchr(s, '=');
  if (!eq) return 0;
  *eq = '\0';
  *key = Trim(s);
  *val = Trim(eq + 1);
  return 1;
}

void QolIniEnsureDefaults(void)
{
  char path[1024];
  QolIniPath(path, sizeof path);

  FILE *probe = fopen(path, "rb");
  if (probe) { fclose(probe); return; }   /* exists: never touch */

  FILE *f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "[qol] cannot write %s\n", path);
    return;
  }
  fwrite(kDefaultText, 1, sizeof kDefaultText - 1, f);
  fclose(f);
}

int QolIniGet(const char *key, char *out, size_t cap)
{
  char path[1024];
  QolIniPath(path, sizeof path);

  FILE *f = fopen(path, "rb");
  if (!f) return 0;

  char line[512];
  while (fgets(line, sizeof line, f)) {
    char *k = NULL, *v = NULL;
    if (!SplitKey(line, &k, &v)) continue;
    if (strcasecmp(k, key) != 0) continue;
    snprintf(out, cap, "%s", v);
    fclose(f);
    return 1;
  }
  fclose(f);
  return 0;
}

void QolIniSet(const char *key, const char *value)
{
  char path[1024];
  QolIniPath(path, sizeof path);

  size_t used = 0;
  s_text[0] = '\0';
  FILE *f = fopen(path, "rb");
  if (f) {
    used = fread(s_text, 1, sizeof s_text - 1, f);
    fclose(f);
  }
  s_text[used] = '\0';

  /* Copy line by line, rewriting the one matching key. */
  size_t out = 0;
  int wrote = 0;
  size_t section_end = 0;
  int saw_section = 0;

  char *p = s_text;
  while (*p) {
    char *nl = strchr(p, '\n');
    size_t len = nl ? (size_t)(nl - p) : strlen(p);

    char line[512];
    size_t n = len < sizeof line - 1 ? len : sizeof line - 2;
    memcpy(line, p, n);
    line[n] = '\0';

    char probe[512];
    snprintf(probe, sizeof probe, "%s", line);
    char *s = Trim(probe);
    int is_section = (*s == '[');

    int match = 0;
    if (!is_section) {
      char tmp[512];
      char *k = NULL, *v = NULL;
      snprintf(tmp, sizeof tmp, "%s", line);
      if (SplitKey(tmp, &k, &v) && strcasecmp(k, key) == 0) match = 1;
    }

    int w;
    if (match) {
      w = snprintf(s_next + out, sizeof s_next - out, "%s = %s\n", key, value);
      wrote = 1;
    } else {
      w = snprintf(s_next + out, sizeof s_next - out, "%s\n", line);
    }
    if (w <= 0) break;
    if (out + (size_t)w >= sizeof s_next) { out = sizeof s_next - 1; break; }
    out += (size_t)w;

    if (is_section && strcasecmp(Trim(s + 1), "QoL]") == 0) {
      saw_section = 1;
      section_end = out;
    }

    if (!nl) break;
    p = nl + 1;
  }

  if (!wrote) {
    if (!saw_section) {
      int w = snprintf(s_next + out, sizeof s_next - out,
                       "\n[QoL]\n%s = %s\n", key, value);
      if (w > 0 && out + (size_t)w < sizeof s_next) out += (size_t)w;
    } else {
      /* Keep the new key with its section instead of at the end of the file. */
      static char tail[QOL_INI_MAX];
      size_t tail_len = out - section_end;
      memcpy(tail, s_next + section_end, tail_len);
      out = section_end;
      int w = snprintf(s_next + out, sizeof s_next - out, "%s = %s\n", key, value);
      if (w > 0 && out + (size_t)w < sizeof s_next) out += (size_t)w;
      if (tail_len > sizeof s_next - out) tail_len = sizeof s_next - out;
      memcpy(s_next + out, tail, tail_len);
      out += tail_len;
    }
  }

  FILE *w2 = fopen(path, "wb");
  if (!w2) {
    fprintf(stderr, "[qol] cannot write %s\n", path);
    return;
  }
  fwrite(s_next, 1, out, w2);
  fclose(w2);
}

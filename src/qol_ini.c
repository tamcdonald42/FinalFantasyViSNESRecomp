#include "qol_ini.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

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
    "#\n"
    "# Bezel           fills the widescreen margins instead of leaving them\n"
    "#                 black. none | slate | dim | ink | custom.\n"
    "# BezelImage      your own picture for custom (PNG/JPG/BMP), relative to\n"
    "#                 the exe folder: both panels, mirrored on the right\n"
    "#                 unless BezelMirror = 0.\n"
    "# BezelImageLeft  art for the left panel only, never mirrored.\n"
    "# BezelImageRight art for the right panel only, never mirrored.\n"
    "#                 Per-side art beats BezelImage for its own side; a\n"
    "#                 missing file falls back to BezelImage, then slate.\n"
    "# BezelOpacity    0..100. Only visible while Widescreen is on.\n"
    "#                 Panels are 64x224 each; see docs/BEZEL_ART.md.\n"
    "#\n"
    "# Keys: any SDL key name (Grave, F10, ...), or None.\n"
    "\n"
    "[QoL]\n"
    "Encounters    = on\n"
    "EncountersKey = Grave\n"
    "Widescreen    = off\n"
    "WidescreenKey = F10\n"
    "Bezel           = none\n"
    "BezelImage      = bezels/panel.png\n"
    "BezelImageLeft  = bezels/left.png\n"
    "BezelImageRight = bezels/right.png\n"
    "BezelOpacity    = 100\n"
    "BezelMirror     = 1\n";

/* File-scope scratch: the file is a few hundred bytes, and an edit path with
 * no allocation cannot fail on the one run where memory is short. */
static char s_text[QOL_INI_MAX];
static char s_next[QOL_INI_MAX];

/* The directory the executable lives in, without touching SDL.
 *
 * after_config runs before SDL_Init, and SDL_GetBasePath allocates through the
 * allocator SDL installs when it initialises -- calling it that early and then
 * freeing the answer corrupted the heap, which surfaced later as 0xC0000374
 * anywhere from the launcher's first allocation to shutdown. A path needs
 * neither SDL nor its allocator. Resolved once, then cached. */
static void ExeDir(char *out, size_t cap)
{
  out[0] = '\0';
#if defined(_WIN32)
  char buf[MAX_PATH * 4];
  DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)sizeof buf);
  if (n == 0 || n >= sizeof buf) return;
  char *slash = strrchr(buf, '\\');
  if (!slash) slash = strrchr(buf, '/');
  if (!slash) return;
  *slash = '\0';
  snprintf(out, cap, "%s", buf);
#elif defined(__linux__)
  char buf[1024];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n <= 0) return;
  buf[n] = '\0';
  char *slash = strrchr(buf, '/');
  if (!slash) return;
  *slash = '\0';
  snprintf(out, cap, "%s", buf);
#else
  (void)out; (void)cap;
#endif
}

void QolIniPath(char *out, size_t cap)
{
  static char resolved[1024];
  if (!resolved[0]) {
    char dir[1024];
    ExeDir(dir, sizeof dir);
#if defined(_WIN32)
    if (dir[0]) snprintf(resolved, sizeof resolved, "%s\\%s", dir, QOL_INI_NAME);
#else
    if (dir[0]) snprintf(resolved, sizeof resolved, "%s/%s", dir, QOL_INI_NAME);
#endif
    else snprintf(resolved, sizeof resolved, "%s", QOL_INI_NAME);
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

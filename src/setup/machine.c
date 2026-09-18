/* machine.c — what the machine is, as against how its tube looks.
 *
 * The tube's settings are floats the shader reads every frame, so moving
 * one shows at once and there is nothing to remember.  These are not: how
 * much memory the emulated PC has, which processor core runs it, which
 * keyboard DOS thinks is attached - all of it is read once, when the core
 * starts, and cannot be changed under a running DOS any more than a real
 * one could be re-chipped while it was on.  So each says so on screen, and
 * they are kept in a file of their own that the machine reads before it
 * boots (main.c), not applied from here.
 *
 * The file is plain: one `name = value` a line, the values being what the
 * core itself wants to be told, so a line can be read without a table. */
#include "internal.h"
#include "log.h"
#include "processors.h"
#include <stdio.h>
#include <string.h>

/* The sections, in the order SETUP lists them (setup.c). */
enum { SEC_MONITOR, SEC_KEYBOARD, SEC_MACHINE, SEC_VIDEO, SEC_SOUND, SEC_BOOT };

/* What DOS can be told the keyboard is.  Not the whole of DOSBox's set -
 * the rest are reachable with --keyboard - but the boards the machine can
 * recognise on its own, so the list and the guess agree. */
static const char *const KB_CODE[] = {"auto", "us", "uk", "fr", "gr", "it", "sp",
                                      "po",   "br", "nl", "sv", "dk", "no"};
static const char *const KB_NAME[] = {
    "Auto (the host's)", "United States", "United Kingdom", "France", "Germany", "Italy", "Spain",
    "Portugal",          "Brazil",        "Netherlands",    "Sweden", "Denmark", "Norway"};

/* The processor is the one setting the machine itself can change while
 * it is up - the turbo display's - and + step it - so it is kept the
 * moment it changes, not only when SETUP saves.  SETUP lists the chips
 * by name; the file keeps the code. */
static const char *proc_name[DXM_NPROCESSORS];

static const char *const MEM_CODE[] = {"4", "8", "16", "32", "64"};
static const char *const MEM_NAME[] = {"4 MB", "8 MB", "16 MB", "32 MB", "64 MB"};

static const char *const CPU_CODE[] = {"auto", "dynamic", "normal", "simple"};
static const char *const CPU_NAME[] = {"Auto", "Dynamic (fast)", "Normal (interpreter)",
                                       "Simple (real mode)"};

/* The 3dfx card the core emulates, by its memory: 4 MB is the low-resolution
 * board, 12 MB the one with two texture units.  The codes are the core's. */
static const char *const VOODOO_CODE[] = {"off", "4mb", "8mb", "12mb"};
static const char *const VOODOO_NAME[] = {"None", "4 MB", "8 MB", "12 MB, dual texture"};

static const char *const BOOT_NAME[] = {"The DOS prompt", "The catalogue"};

/* What can be offered for MIDI depends on what is on C:, so the list is
 * built when SETUP asks rather than written out here.  Auto and None are
 * always there; a device appears when its ROMs do. */
#define MIDI_MAX 5
static const char *midi_code[MIDI_MAX], *midi_name[MIDI_MAX];
static int midi_n;

/* The recompiler needs to write instructions and then run them, which Apple
 * Silicon allows only a program signed with the JIT entitlement
 * (packaging/macos/dxm.entitlements).  Asking for one page the way the
 * core maps its cache is the only honest way to find out: a build that was
 * not signed with it would otherwise offer a setting that takes DOS down
 * when it is chosen.  Everywhere else the core is simply there. */
#if defined(__APPLE__) && defined(__aarch64__)
#    include <sys/mman.h>
#    include <unistd.h>
static int dynamic_core_ok(void) {
    static int known = -1;
    if (known < 0) {
        size_t page = (size_t)getpagesize();
        void *p = mmap(NULL, page, PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
        known = p != MAP_FAILED;
        if (known)
            munmap(p, page);
        dxm_log("setup: the dynamic core is %s",
                known ? "entitled" : "not entitled, so the machine interprets");
    }
    return known;
}
#else
static int dynamic_core_ok(void) {
    return 1;
}
#endif

/* what it is set to: an index into each list above */
static struct {
    int kb, mem, cpu, boot, midi, voodoo, proc;
    char c_drive[1024];
} M = {.mem = 2, .voodoo = 2, .proc = DXM_PROCESSOR_DEFAULT};
/* 16 MB, Auto - the fastest core the host allows - the 8 MB Voodoo, as the
 * core itself defaults to, and the 486DX2 at 66 */

static int on_c(const char *name) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", M.c_drive, name);
    FILE *f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

/* Auto, None, and whichever of the three the ROMs on C: allow. */
static void midi_scan(void) {
    midi_n = 0;
    midi_code[midi_n] = "auto";
    midi_name[midi_n++] = "Auto (what is on C:)";
    midi_code[midi_n] = "off";
    midi_name[midi_n++] = "None";
    if (on_c("MT32_CONTROL.ROM") && on_c("MT32_PCM.ROM")) {
        midi_code[midi_n] = "mt32";
        midi_name[midi_n++] = "Roland MT-32";
    }
    if (on_c("ROM1.BIN")) {
        midi_code[midi_n] = "sc55";
        midi_name[midi_n++] = "Roland SC-55";
    }
    if (on_c("DOSBOX.SF2")) {
        midi_code[midi_n] = "sf2";
        midi_name[midi_n++] = "SoundFont";
    }
    if (M.midi >= midi_n)
        M.midi = 0;
}

const char *machine_midi(void) {
    if (!midi_n)
        midi_scan();
    return midi_code[M.midi];
}

void machine_where(const char *c_drive) {
    snprintf(M.c_drive, sizeof M.c_drive, "%s", c_drive ? c_drive : "");
}

const char *machine_keyboard(void) {
    return KB_CODE[M.kb];
}
const char *machine_memory(void) {
    return MEM_CODE[M.mem];
}
const char *machine_cpu_core(void) {
    /* Whatever the file says, this machine answers with what it can
     * actually run: a core told "auto" on a build the host will not let
     * generate code would try the recompiler, fail to get memory it may
     * execute, and take DOS down with it. */
    return dynamic_core_ok() ? CPU_CODE[M.cpu] : "normal";
}
int machine_boot_catalogue(void) {
    return M.boot == 1;
}
const char *machine_voodoo(void) {
    return VOODOO_CODE[M.voodoo];
}
int machine_processor(void) {
    return M.proc;
}
void machine_set_processor(int stop) {
    if (stop >= 0 && stop < DXM_NPROCESSORS)
        M.proc = stop;
}

int machine_settings(int section, setting *out, int max) {
    int n = 0;
#define PUT(s)                                                                                     \
    do {                                                                                           \
        if (n < max)                                                                               \
            out[n++] = (s);                                                                        \
    } while (0)
    if (section == SEC_KEYBOARD) {
        PUT(((setting){.name = "Layout",
                       .kind = SET_CHOICE,
                       .pick = &M.kb,
                       .opts = KB_NAME,
                       .nopts = (int)(sizeof KB_NAME / sizeof KB_NAME[0]),
                       .next_boot = 1,
                       .note = "Auto reads what the host's own keys produce."}));
    } else if (section == SEC_MACHINE) {
        if (!proc_name[0])
            for (int i = 0; i < DXM_NPROCESSORS; i++)
                proc_name[i] = dxm_processors[i].name;
        PUT(((setting){.name = "Processor",
                       .kind = SET_CHOICE,
                       .pick = &M.proc,
                       .opts = proc_name,
                       .nopts = DXM_NPROCESSORS,
                       .next_boot = 1,
                       .note = "The turbo display's keys step it too, and it is kept."}));
        PUT(((setting){.name = "Memory",
                       .kind = SET_CHOICE,
                       .pick = &M.mem,
                       .opts = MEM_NAME,
                       .nopts = (int)(sizeof MEM_NAME / sizeof MEM_NAME[0]),
                       .next_boot = 1,
                       .note = "A few old programs dislike large extended memory."}));
        PUT(((setting){.name = "Processor core",
                       .kind = SET_CHOICE,
                       .pick = &M.cpu,
                       .opts = CPU_NAME,
                       .nopts = (int)(sizeof CPU_NAME / sizeof CPU_NAME[0]),
                       .next_boot = 1,
                       .fixed = !dynamic_core_ok(),
                       .note = !dynamic_core_ok()
                                   ? "This machine interprets: the recompiler cannot run here."
                                   : "Normal is slower and keeps better time."}));
    } else if (section == SEC_VIDEO) {
        PUT(((setting){.name = "3dfx Voodoo",
                       .kind = SET_CHOICE,
                       .pick = &M.voodoo,
                       .opts = VOODOO_NAME,
                       .nopts = (int)(sizeof VOODOO_NAME / sizeof VOODOO_NAME[0]),
                       .next_boot = 1,
                       .note = "Its triangles are drawn on the processor, on every setting."}));
    } else if (section == SEC_SOUND) {
        midi_scan(); /* the ROMs may have arrived since it was last looked at */
        PUT(((setting){.name = "MIDI device",
                       .kind = SET_CHOICE,
                       .pick = &M.midi,
                       .opts = midi_name,
                       .nopts = midi_n,
                       .next_boot = 1,
                       .note = !strcmp(midi_code[M.midi], "sc55")
                                   ? "The SC-55 emulates its own processor, playing or not."
                                   : (midi_n > 2
                                          ? "Put the ROMs in the root of C: to add a device."
                                          : "No ROMs on C:, so there is nothing to play them.")}));
    } else if (section == SEC_BOOT) {
        PUT(((setting){.name = "Boot to",
                       .kind = SET_CHOICE,
                       .pick = &M.boot,
                       .opts = BOOT_NAME,
                       .nopts = 2,
                       .next_boot = 1,
                       .note = "Leaving the catalogue drops to the DOS prompt."}));
    }
#undef PUT
    return n;
}

/* ---- kept between runs ------------------------------------------------ */

static int find(const char *const *codes, int n, const char *v) {
    for (int i = 0; i < n; i++)
        if (!strcmp(codes[i], v))
            return i;
    return -1;
}

void machine_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char name[64], value[64];
        if (sscanf(line, " %63[^= ] = %63s", name, value) != 2 || name[0] == '#')
            continue;
        int at = -1;
        if (!strcmp(name, "keyboard"))
            at = find(KB_CODE, (int)(sizeof KB_CODE / sizeof KB_CODE[0]), value),
            M.kb = at < 0 ? M.kb : at;
        else if (!strcmp(name, "memory"))
            at = find(MEM_CODE, (int)(sizeof MEM_CODE / sizeof MEM_CODE[0]), value),
            M.mem = at < 0 ? M.mem : at;
        else if (!strcmp(name, "cpu_core"))
            at = find(CPU_CODE, (int)(sizeof CPU_CODE / sizeof CPU_CODE[0]), value),
            M.cpu = at < 0 ? M.cpu : at;
        else if (!strcmp(name, "midi")) {
            midi_scan();
            at = find(midi_code, midi_n, value);
            M.midi = at < 0 ? M.midi : at;
        } else if (!strcmp(name, "boot"))
            M.boot = !strcmp(value, "catalogue");
        else if (!strcmp(name, "voodoo"))
            at = find(VOODOO_CODE, (int)(sizeof VOODOO_CODE / sizeof VOODOO_CODE[0]), value),
            M.voodoo = at < 0 ? M.voodoo : at;
        else if (!strcmp(name, "processor"))
            at = dxm_processor_find(value), M.proc = at < 0 ? M.proc : at;
    }
    fclose(f);
    dxm_log("setup: %s, keyboard %s, memory %s MB, core %s, midi %s, voodoo %s",
            dxm_processors[M.proc].name, machine_keyboard(), machine_memory(), machine_cpu_core(),
            machine_midi(), machine_voodoo());
}

void machine_save(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        dxm_log("setup: cannot write %s", path);
        return;
    }
    fprintf(f, "# DOS ex Machina: what the machine is set to.  Read when it\n"
               "# powers on; the tube's own settings are in crt.cfg.\n");
    fprintf(f, "processor = %s\n", dxm_processors[M.proc].code);
    fprintf(f, "keyboard = %s\n", machine_keyboard());
    fprintf(f, "memory = %s\n", machine_memory());
    fprintf(f, "cpu_core = %s\n", machine_cpu_core());
    fprintf(f, "midi = %s\n", machine_midi());
    fprintf(f, "boot = %s\n", M.boot ? "catalogue" : "dos");
    fprintf(f, "voodoo = %s\n", machine_voodoo());
    fclose(f);
    dxm_log("setup: wrote %s", path);
}

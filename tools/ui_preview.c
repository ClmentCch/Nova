/*
 * NOVA UI preview.
 *
 * Compiles the shipping drawing code (payload/main.c + payload/ui/ui.c) on
 * the host, renders a frame with representative status data and writes it as
 * a raw BGRA buffer. tools/preview_png.py converts it to PNG.
 */
#define NOVA_UI_PREVIEW 1

#include "../payload/main.c"

#include <stdio.h>
#include <stdlib.h>

/* Platform stubs: the preview only exercises the drawing code. */
void nova_platform_init(void) {}
uint32_t *nova_platform_fb(void) { return NULL; }
void nova_platform_backlight(uint32_t p) { (void)p; }
void nova_platform_delay(uint32_t ms) { (void)ms; }
uint32_t nova_platform_ms(void) { return 0; }
bool nova_platform_sd_ready(void) { return true; }
const nova_fs_t *nova_platform_fs(void) { return NULL; }
void nova_platform_input(nova_input_t *out) { nova_memset(out, 0, sizeof(*out)); }
bool nova_platform_write_text(const char *p, const char *t, uint32_t l) { (void)p; (void)t; (void)l; return false; }
void nova_platform_chainload(void *image, uint32_t size) { (void)image; (void)size; for (;;) {} }
void nova_platform_halt(void) { for (;;) {} }
void nova_platform_power_off(void) { for (;;) {} }

static nova_status_t make_status(int scenario)
{
    nova_status_t st;
    nova_memset(&st, 0, sizeof(st));

    st.sd_mounted = true;
    nova_cstr_copy(st.sd_detail, sizeof(st.sd_detail), "mounted");

    st.atmosphere = NOVA_ST_OK;
    nova_cstr_copy(st.atm_detail, sizeof(st.atm_detail), "package3 verified");
    st.package3_size = 1300000;

    st.hekate = NOVA_ST_OK;
    nova_cstr_copy(st.hek_detail, sizeof(st.hek_detail), "payload 6.5.5 verified");
    nova_cstr_copy(st.hekate_path, sizeof(st.hekate_path), "hekate_ctcaer_6.5.5.bin");
    nova_cstr_copy(st.hekate_version, sizeof(st.hekate_version), "6.5.5");

    st.bootcfg = NOVA_ST_OK;
    nova_cstr_copy(st.cfg_detail, sizeof(st.cfg_detail), "entries verified");

    if (scenario == 0)
    {
        st.emummc = NOVA_ST_OK;
        st.emu_mode = NOVA_EMU_MODE_RAW;
        st.emu_sector = 0x200000;
        nova_cstr_copy(st.emu_path, sizeof(st.emu_path), "emuMMC/RAW1");
        nova_cstr_copy(st.emu_nintendo, sizeof(st.emu_nintendo), "emuMMC/RAW1/Nintendo");
        nova_cstr_copy(st.emu_detail, sizeof(st.emu_detail), "raw emuMMC: emuMMC/RAW1");
        st.can_stock = true;
        st.can_hekate = true;
        st.can_emummc = true;
    }
    else if (scenario == 1)
    {
        st.emummc = NOVA_ST_MISSING;
        nova_cstr_copy(st.emu_detail, sizeof(st.emu_detail), "disabled in emummc.ini");
        st.can_stock = true;
        st.can_hekate = true;
    }
    else
    {
        st.emummc = NOVA_ST_ERROR;
        nova_cstr_copy(st.emu_detail, sizeof(st.emu_detail), "Nintendo folder missing");
        st.atmosphere = NOVA_ST_ERROR;
        nova_cstr_copy(st.atm_detail, sizeof(st.atm_detail), "package3 missing");
        st.can_stock = false;
        st.can_hekate = true;
    }

    nova_cstr_copy(st.stock_detail, sizeof(st.stock_detail),
                   st.can_stock ? "SysNAND OFW ready" : "needs atmosphere/package3");
    return st;
}

int main(int argc, char **argv)
{
    int scenario = argc > 1 ? atoi(argv[1]) : 0;
    int selected = argc > 2 ? atoi(argv[2]) : 0;
    const char *out = argc > 3 ? argv[3] : "build/preview.raw";

    uint32_t *fb = (uint32_t *)calloc(SCREEN_W * SCREEN_H, 4);
    uint32_t *bg = (uint32_t *)calloc(SCREEN_W * SCREEN_H, 4);
    if (!fb || !bg)
    {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    nova_status_t st = make_status(scenario);
    nova_preview_compose(&st, selected, 1200, fb, bg);

    FILE *f = fopen(out, "wb");
    if (!f)
    {
        fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    fwrite(fb, 4, SCREEN_W * SCREEN_H, f);
    fclose(f);

    printf("wrote %s (%dx%d, scenario %d, selected %d, items %d)\n",
           out, SCREEN_W, SCREEN_H, scenario, selected, g_item_count);
    return 0;
}

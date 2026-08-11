/* vabparse_main.c -- ROUND 58: standalone CLI for the RR.VH/RR.VB
 * sound bank (format writeup in vab.h). Lists programs/tones/VAGs and
 * optionally decodes one VAG to a mono 16-bit WAV:
 *
 *   vabparse_tool <RR.VH> [<RR.VB> --dump-vag N --out out.wav [--rate HZ]]
 *
 * Game data comes from the user's own extraction and is never
 * committed to this repo -- same convention as every other tool. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vab.h"

static unsigned char *slurp(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb");
    unsigned char *b;
    long s;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); s = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc((size_t)s);
    if (!b || fread(b, 1, (size_t)s, f) != (size_t)s) { fclose(f); free(b); return NULL; }
    fclose(f);
    *n = (size_t)s;
    return b;
}

static void wr32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f); }
static void wr16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

int main(int argc, char **argv)
{
    const char *vh_path = NULL, *vb_path = NULL, *out_path = NULL;
    long dump = -1, rate = 22050;
    size_t vhn, vbn;
    unsigned char *vh, *vb = NULL;
    VabHeader h;
    int i, p;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-vag") == 0 && i + 1 < argc) dump = strtol(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out_path = argv[++i];
        else if (strcmp(argv[i], "--rate") == 0 && i + 1 < argc) rate = strtol(argv[++i], NULL, 10);
        else if (!vh_path) vh_path = argv[i];
        else vb_path = argv[i];
    }
    if (!vh_path) {
        fprintf(stderr, "usage: vabparse_tool <RR.VH> [<RR.VB> --dump-vag N --out f.wav]\n");
        return 2;
    }
    vh = slurp(vh_path, &vhn);
    if (!vh) { fprintf(stderr, "cannot read %s\n", vh_path); return 1; }
    if (vab_parse(vh, vhn, &h) != 0) { fprintf(stderr, "not a VAB header\n"); return 1; }
    printf("VAB: %d active programs, %d tones, %d VAGs\n", h.programs, h.tones, h.vags);
    for (p = 0; p < 128; p++) {
        int t;
        if (h.prog_tone_count[p] == 0) continue;
        printf("  prog %3d:", p);
        for (t = 0; t < h.prog_tone_count[p] && t < 16; t++)
            printf(" [vag %d c%d s%d n%d-%d]", h.tone[p][t].vag,
                   h.tone[p][t].center, h.tone[p][t].shift,
                   h.tone[p][t].note_min, h.tone[p][t].note_max);
        printf("\n");
    }
    if (dump >= 1 && vb_path && out_path) {
        vb = slurp(vb_path, &vbn);
        if (!vb) { fprintf(stderr, "cannot read %s\n", vb_path); return 1; }
        {
            size_t cap = (size_t)h.vag_len[dump] / 16 * 28 + 28;
            int16_t *pcm = malloc(cap * sizeof(int16_t));
            size_t n = vab_decode_vag(&h, (int)dump, vb, vbn, pcm, cap);
            FILE *f = fopen(out_path, "wb");
            if (!f) { fprintf(stderr, "cannot write %s\n", out_path); return 1; }
            fwrite("RIFF", 1, 4, f); wr32(f, 36 + (uint32_t)n * 2);
            fwrite("WAVEfmt ", 1, 8, f); wr32(f, 16); wr16(f, 1); wr16(f, 1);
            wr32(f, (uint32_t)rate); wr32(f, (uint32_t)rate * 2); wr16(f, 2); wr16(f, 16);
            fwrite("data", 1, 4, f); wr32(f, (uint32_t)n * 2);
            fwrite(pcm, 2, n, f);
            fclose(f);
            printf("VAG %ld: %zu samples -> %s (%ld Hz)\n", dump, n, out_path, rate);
            free(pcm);
        }
    }
    free(vh); free(vb);
    return 0;
}

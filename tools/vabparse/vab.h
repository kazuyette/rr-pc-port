/* vab.h -- ROUND 58: Ridge Racer's sound bank (RR.VH / RR.VB), the
 * standard PSY-Q VAB format, CONFIRMED against the user's own local
 * files (never committed to this repo):
 *
 *   RR.VH (header, 32288 bytes):
 *     +0x00  "pBAV" magic, version 6, vab id 0
 *     +0x0C  total waveform size
 *     +0x12  program count = 58, tone count = 79, VAG count = 50
 *     +0x20  128 x 16-byte program entries (byte 0 = tone count;
 *            58 are active: 0..57)
 *     then   one 16x32-byte tone table PER ACTIVE PROGRAM (packed):
 *            tone = {prio, mode, vol, pan, center, shift, note_min,
 *            note_max, ..., vag_index @ +0x16}
 *     then   (VAG count + 1) uint16 sizes in 8-byte units; entry 0 is
 *            0 and the rest SUM EXACTLY to RR.VB's 491056 bytes --
 *            byte-exact close, same standard as our other parsers.
 *
 *   RR.VB = the 50 VAG sample bodies back to back, standard PS1 SPU
 *   ADPCM (16-byte blocks: shift/filter, flags, 28 nibbles; filter
 *   pairs {0,0},{60,0},{115,-52},{98,-55},{122,-60} / 64).
 *
 *   Programs 0..16 are single-tone (VAGs 1..17, center 89 shift 70,
 *   full note range) -- the per-car engine loop family. Round-58
 *   python decode of VAGs 1/2/17/18 produced clean dense waveforms
 *   (~6.8k zero-crossings/s), consistent with engine loops.
 */
#ifndef RR_VAB_H
#define RR_VAB_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t prio, mode, vol, pan, center, shift, note_min, note_max;
    uint16_t vag;
} VabTone;

typedef struct {
    int programs;   /* active count */
    int tones;
    int vags;
    int prog_tone_count[128];
    VabTone tone[128][16];
    uint32_t vag_off[64];  /* byte offsets into the VB body */
    uint32_t vag_len[64];  /* byte lengths */
} VabHeader;

/* Parses a raw RR.VH buffer. Returns 0 on success. */
int vab_parse(const uint8_t *vh, size_t vh_size, VabHeader *out);

/* Decodes one VAG (SPU ADPCM) from a raw RR.VB buffer into 16-bit
 * PCM. Returns sample count written (up to max_samples). */
size_t vab_decode_vag(const VabHeader *h, int vag_index,
                      const uint8_t *vb, size_t vb_size,
                      int16_t *pcm, size_t max_samples);

#endif /* RR_VAB_H */

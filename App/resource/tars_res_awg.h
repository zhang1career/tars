#ifndef TARS_RES_AWG_H
#define TARS_RES_AWG_H

#include <stdint.h>

/* Arbitrary waveform generator built on the STM32F429 dual DAC.
 *
 * dac0 (PA4) and dac1 (PA5) are independent AWG channels. Each has its own
 * sample table in external SDRAM and circular DMA (Stream5/Stream6). Both are
 * clocked from a shared TIM7 TRGO so samples stay phase-locked when both run.
 *
 * Output frequency per channel = sample_rate / points. With both channels active
 * the sample clock is max(freq_i * points_i) across running channels. */

typedef enum {
  TARS_AWG_WAVE_SINE = 0,
  TARS_AWG_WAVE_SQUARE,
  TARS_AWG_WAVE_TRIANGLE,
  TARS_AWG_WAVE_SAWTOOTH,
  TARS_AWG_WAVE_DC,
  TARS_AWG_WAVE_NOISE,
  TARS_AWG_WAVE_CUSTOM   /* uploaded sample table */
} tars_awg_wave_t;

/* Parse a wave name ("sin"/"sine","square","tri"/"triangle","saw"/"sawtooth",
 * "dc","noise") into the enum. Returns 0 on success, -1 on unknown name. */
int TarsResAwg_ParseWave(const char *name, tars_awg_wave_t *out);

/* Compute a sample table into this channel's SDRAM region and remember the
 * output frequency. ampl_pct is peak-to-peak as a percentage of full scale,
 * offset_pct is the DC midpoint (0-100%), duty_pct only applies to square.
 * Does not start output; call TarsResAwg_Enable to drive it. */
int TarsResAwg_Generate(const char *channel,
                        tars_awg_wave_t wave,
                        uint32_t points,
                        uint32_t freq_hz,
                        float ampl_pct,
                        float offset_pct,
                        float duty_pct);

/* Change output frequency (recomputes the TIM7 sample rate). Applies live if
 * the channel is currently running. */
int TarsResAwg_SetFreq(const char *channel, uint32_t freq_hz);

/* Custom waveform upload (Phase 2). The transport (USB shell binary mode)
 * streams `points` raw little-endian uint16 samples (DAC codes 0-4095)
 * directly into this channel's SDRAM table.
 *
 *   Begin    -> validate + hand back the SDRAM buffer to stream into.
 *   Complete -> mask samples to 12 bits and mark the table ready to enable.
 *
 * The channel must be stopped. No tenant grant is needed to upload (nothing
 * touches hardware); grant + Enable drive it as usual. */
int TarsResAwg_UploadBegin(const char *channel,
                           uint32_t points,
                           uint8_t **buf_out,
                           uint32_t *bytes_out);
int TarsResAwg_UploadComplete(const char *channel);

/* Start (enable=1) or stop (enable=0) DMA-driven output on the channel. */
int TarsResAwg_Enable(const char *channel, int enable);

int TarsResAwg_IsRunning(const char *channel);
int TarsResAwg_GetStatus(const char *channel, char *out, uint32_t out_size);

/* dac1 follows dac0: on enable/resync, rotate dac1's table so its output index
 * tracks dac0 plus offset_samples (in sample points, mod dac1.points). */
int TarsResAwg_LinkSet(int enable, int32_t offset_samples);
int TarsResAwg_LinkEnable(int enable);
int TarsResAwg_LinkSetOffset(int32_t offset_samples);
int TarsResAwg_LinkResync(void);
int TarsResAwg_LinkGetStatus(char *out, uint32_t out_size);

#endif /* TARS_RES_AWG_H */

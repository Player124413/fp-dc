/* Android audio backend (AAudio mixer): the fp.h sound API plus the platform's own controls.
 *
 * The Dreamcast played AICA 4-bit ADPCM in hardware from 2 MB of sound RAM; a phone has neither, so
 * this mixer decodes the same ADPCM (plain YMZ/AICA step decoder, the one the hardware uses) into
 * 16-bit PCM and resamples to the device rate.  Sound effects are decoded once at load (they are
 * short); music is a 32 kHz stereo ADPCM stream decoded by a worker thread into a one-second ring,
 * so the audio callback never touches the filesystem. */
#ifndef FP_ANDROID_SND_H
#define FP_ANDROID_SND_H

/* master volumes 0..100 (port settings, multiplied with the game's own volume options) */
void snd_master(int music_pct, int sfx_pct);
int  snd_init_state(void);        /* 0: the AAudio stream is running, <0: no audio (silent game) */
int  snd_out_rate(void);          /* device sample rate (0 when silent) */
void snd_free_all(void);          /* engine quit: release every resident sample (the launcher stays up) */
void snd_recover(void);           /* main loop: rebuild a lost stream, measure the delivery rate once */
void snd_trace(int on);           /* log every voice start (sys.sndtrace: which sound is that?) */
void snd_stop_mixer(void);        /* app exit: stop the stream and the music thread */
void snd_suspend(void);           /* app paused: stop the stream (keeps its place in the music) */
void snd_resume(void);
/* the audio callback's load, for the perf overlay: voices mixed and ring fill in frames */
int  snd_active_voices(void);
int  snd_underruns(void);
int  snd_steals(void);                     /* one-shot voices a newer sound took over (pool full) */
int  snd_music_name(char *dst, int cap);   /* the track playing now, "" when silent */

#endif

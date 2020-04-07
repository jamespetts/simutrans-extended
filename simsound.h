/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef SIMSOUND_H
#define SIMSOUND_H


#include "simtypes.h"

/// sound can be selectively muted (but volume is not touched)
void sound_set_mute(bool on);
bool sound_get_mute();

/// @param volume in range 0..255
void sound_set_global_volume(int volume);

/// @returns volume in range 0..255
int sound_get_global_volume();


/**
 * Play a sound.
 *
 * @param idx    Index of the sound
 * @param volume in range 0..255
 */
void sound_play(uint16 idx, uint8 volume = 255);


/// shuffle enable/disable for midis
bool sound_get_shuffle_midi();
void sound_set_shuffle_midi( bool shuffle );

/// @param volume in range 0..255
void sound_set_midi_volume(int volume);

/// @returns volume in range 0..255
int sound_get_midi_volume();

/// gets midi title
const char *sound_get_midi_title(int index);


/**
 * gets current midi number
 */
int get_current_midi();

// when muted, midi is not used
void midi_set_mute(bool on);
bool midi_get_mute();

/* OWEN: MIDI routines */
extern int midi_init(const char *path);
extern void midi_play(const int no);
extern void check_midi();


/**
 * shuts down midi playing
 */
extern void close_midi();

extern void midi_next_track();
extern void midi_last_track();
extern void midi_stop();

#endif

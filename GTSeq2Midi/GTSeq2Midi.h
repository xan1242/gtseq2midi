// GT Seq2Midi
// Converts Gran Turismo sequences to MIDI!
// by Xanvier / xan1242

#pragma once
#define SEQ_LOOPMARKER 0x1
#define SEQ_ENDTRACK 0x02
#define SEQ_INSTRUMENT 0x03
#define SEQ_VOLUME 0x04
#define SEQ_PAN 0x05
#define SEQ_TEMPO 0x06 // never used... I think at least...

#define GTSEQ_TRACKCOUNT 16 // GT2 has a fixed track count of 16, other games untested
#define GTSEQ_DEFAULT_BENDRANGE 3
#define GTSEQ_PPQN 120

// MIDI stuff
#define MIDI_HEADER 0x6468544D // "MThd" in little endian
#define MIDI_TRACK_HEADER 0x6B72544D // "MTrk" in little endian
#define MIDI_VOLUME_CC 7
#define MIDI_PAN_CC 10
#define MIDI_RPN_1_CC 101
#define MIDI_RPN_2_CC 100
#define MIDI_BENDRANGE_RPN 0
#define MIDI_BENDRANGE2_RPN 6
#define MIDI_BENDRANGE3_RPN 38

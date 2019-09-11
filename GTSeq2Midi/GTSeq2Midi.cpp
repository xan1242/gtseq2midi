// GT Seq2Midi
// Converts Gran Turismo sequences to MIDI!
// by Xanvier / xan1242

#include "stdafx.h"
#include <stdlib.h>
#include <string.h>
#include "GTSeq2Midi.h"
#include "include\MidiFile.h"
#ifdef _DEBUG
#pragma comment(lib, "midifile_debug.lib")
#else
#pragma comment(lib, "midifile.lib")
#endif
using namespace smf;


struct GTSeqStruct
{
	unsigned int MasterVolume;
	unsigned int TempoMS;
	unsigned int TrackPointers[GTSEQ_TRACKCOUNT];
}*GTSeqHead;

struct GTSeqMainStruct
{
	unsigned int Magic;
	void* unkpointer;
	unsigned int SeqCount;
}GTSeqMain;

void* GTSeqData;

char TrackName[64];
char MultiSeqName[255];
char OutFileName[255];

bool bSetLoopStart = false;
bool bSetLoopEnd = false;
bool bParsedMain = false;

unsigned int VerbosityLevel = 1;
unsigned int FileSize = 0;
unsigned int TrackNums[GTSEQ_TRACKCOUNT + 1];
unsigned int CurrentSequence = 0;

// my code from NFS projects...
int __stdcall XNFS_printf(unsigned int Level, const char* Format, ...)
{
	va_list ArgList;
	int Result = 0;
	if ((Level <= VerbosityLevel) && VerbosityLevel)
	{
		__crt_va_start(ArgList, Format);
		Result = vfprintf(stdout, Format, ArgList);
		__crt_va_end(ArgList);
	}
	return Result;
}

unsigned int GetFileSize(const char* FileName)
{
	struct stat st;
	stat(FileName, &st);
	return st.st_size;
}

void ParseMainSeqData(FILE* finput)
{
	fread(&GTSeqMain, sizeof(GTSeqMainStruct), 1, finput);
	GTSeqHead = (GTSeqStruct*)calloc(GTSeqMain.SeqCount, sizeof(GTSeqStruct));
	fread(GTSeqHead, sizeof(GTSeqStruct), GTSeqMain.SeqCount, finput);

	// allocate
	GTSeqData = malloc(FileSize);

	// read
	fseek(finput, 0, SEEK_SET);
	fread(GTSeqData, FileSize, 1, finput);
}

void PrintInfo()
{
	XNFS_printf(2, "GT Sequence info:\n");
	XNFS_printf(2, "Master Volume: 0x%X\n", GTSeqHead[CurrentSequence].MasterVolume);
	XNFS_printf(2, "Tempo in BPms: %d\n", GTSeqHead[CurrentSequence].TempoMS);
	XNFS_printf(2, "Tempo in BPM: %.2f\n", (240000000.0 / GTSeqHead[CurrentSequence].TempoMS));
}

float ConvertGTTempo(int InTempoBPms)
{
	return (240000000.0 / GTSeqHead[CurrentSequence].TempoMS);
}

unsigned int VLVDecoder(unsigned int vlv, unsigned int length)
{
	unsigned int output = 0;
	unsigned char input[4];

	input[0] = vlv & 0xFF;
	input[1] = (vlv & 0xFF00) >> 8;
	input[2] = (vlv & 0xFF0000) >> 16;
	input[3] = (vlv & 0xFF000000) >> 24;

	for (int i = 0; i < length; i++)
	{
		output = output << 7;
		output |= input[i] & 0x7f;
	}
	// DEBUG
	//printf("IN DELTA: %X %X %X %X\tOUT DELTA: %X\n", input[0], input[1], input[2], input[3], output);
	//getchar();
	return output;
}

unsigned int ReadDelta(void* point, unsigned int* out_vlv_length)
{
	unsigned int delta;
	if (*(unsigned char*)((unsigned int)(point)) >= 0x80)
	{
		// read next VLV byte (doing this up to 4 times for 32 bits of data), hacky and dirty, I don't care
		if (*(unsigned char*)((unsigned int)(point)+ 1) >= 0x80)
		{
			if (*(unsigned char*)((unsigned int)(point) + 2) >= 0x80)
			{
				delta = *(unsigned int*)((unsigned int)(point));
				*out_vlv_length = 4;
			}
			else
			{
				delta = *(unsigned int*)((unsigned int)(point));
				delta &= 0x00FFFFFF;
				*out_vlv_length = 3;
			}
		}
		else
		{
			delta = *(short int*)((unsigned int)(point));
			*out_vlv_length = 2;
		}

	}
	else
	{
		delta = *(unsigned char*)((unsigned int)(point));
		*out_vlv_length = 1;
	}
	return delta;
}

void SetBendRange(MidiFile* midifile, unsigned int Track, unsigned char Channel, unsigned char value)
{
	(*midifile).addController(Track, 0, Channel, MIDI_RPN_1_CC, 0);
	(*midifile).addController(Track, 1, Channel, MIDI_RPN_2_CC, MIDI_BENDRANGE_RPN);
	(*midifile).addController(Track, 2, Channel, MIDI_BENDRANGE2_RPN, value);
}

void ConvertEvents(MidiFile* midifile, unsigned int Track, unsigned char Channel, unsigned int Start)
{
	unsigned int Cursor = Start;
	unsigned int delta = 0;
	unsigned int delta2 = 0;
	unsigned int pitchbend = 0;
	unsigned int absolutetime = 0; // we use this one to track the time of the events
	unsigned char cmd = 0;
	signed char cmd_param1 = 0;
	unsigned char cmd_param2 = 0;
	unsigned int vlv_length = 1;
	unsigned int vlv_length2 = 1;
	bool bTrackEnd = false;

	// set the default pitch bend range for GT midi (which is 3)
	SetBendRange(midifile, Track, Channel, GTSEQ_DEFAULT_BENDRANGE);

	while (!bTrackEnd)
	{
		delta = ReadDelta((void*)((int)GTSeqData + Cursor), &vlv_length);
		delta = VLVDecoder(delta, vlv_length);
		absolutetime += delta;

		Cursor += vlv_length;
		cmd = *(unsigned char*)((unsigned int)(GTSeqData) + Cursor);

		switch (cmd)
		{
		case 0:
			Cursor += 1;
			break;
		case SEQ_LOOPMARKER:
			XNFS_printf(3, "delta: %X\ttrack loop start\n", delta);
			if (!bSetLoopStart) // we assume all tracks are looping exactly the same
			{
				bSetLoopStart = true;
				(*midifile).addMarker(0, absolutetime, "loopStart");
			}
			Cursor += 2;
			break;
		case SEQ_ENDTRACK:
			cmd_param1 = *(unsigned char*)((unsigned int)(GTSeqData) + Cursor + 1);
			XNFS_printf(3, "delta: %X\ttrack end: %X\n", delta, cmd_param1);
			if (!bSetLoopEnd)
			{
				bSetLoopEnd = true;
				(*midifile).addMarker(0, absolutetime, "loopEnd");
			}
			Cursor += 3;
			bTrackEnd = true;
			return;
			break;
		case SEQ_VOLUME:
			cmd_param1 = *(unsigned char*)((unsigned int)(GTSeqData) + Cursor + 1);
			XNFS_printf(3, "delta: %X\ttrack volume: %d\t\n", delta, cmd_param1);
			(*midifile).addController(Track, absolutetime, Channel, MIDI_VOLUME_CC, cmd_param1);
			Cursor += 2;
			break;
		case SEQ_PAN:
			cmd_param1 = *(unsigned char*)((unsigned int)(GTSeqData) + Cursor + 1);
			XNFS_printf(3, "delta: %X\ttrack pan: %d\n", delta, cmd_param1); 
			(*midifile).addController(Track, absolutetime, Channel, MIDI_PAN_CC, cmd_param1);
			Cursor += 2;
			break;
		case SEQ_INSTRUMENT:
			cmd_param1 = *(unsigned char*)((unsigned int)(GTSeqData) + Cursor + 1);
			XNFS_printf(3, "delta: %X\tinstrument change: %d\n", delta, cmd_param1);
			(*midifile).addPatchChange(Track, absolutetime, Channel, cmd_param1);
			Cursor += 2;
			break;
		default:
			if (cmd <= 0x7F) // pitch bends // TODO: this might catch some unwanted bytes...
			{
				cmd_param1 = *(unsigned char*)((unsigned int)(GTSeqData)+Cursor + 1);
				pitchbend = (cmd_param1 << 8) + cmd;
				pitchbend = VLVDecoder(pitchbend, sizeof(short int));
				pitchbend -= 0x1000;
				XNFS_printf(3, "delta: %X\tpitch bend: 0x%X\n", delta, pitchbend);
				// we have to calculate the pitch bend percentage due to midifile library...
				(*midifile).addPitchBend(Track, absolutetime, Channel, ((double)pitchbend / 8192.0) - 1.0);
				Cursor += 2;
				break;
			}

			if (cmd >= 0x80 && cmd <= 0xEC) // it's probably a note, the game defines notes as events themselves
			{
				// get note data first
				cmd_param1 = *(unsigned char*)((unsigned int)(GTSeqData)+Cursor);
				cmd_param2 = *(unsigned char*)((unsigned int)(GTSeqData)+Cursor + 1);

				// get note length vlv delta second
				delta2 = ReadDelta((void*)((int)GTSeqData + Cursor + 2), &vlv_length2);
				delta2 = VLVDecoder(delta2, vlv_length2);
				// end delta

				XNFS_printf(3, "delta: %X\tnote on: 0x%hhX\tvel: %.3d\tlength: %X\n", delta, cmd_param1, cmd_param2, delta2);
				Cursor += 2 + vlv_length2;
				(*midifile).addNoteOn(Track, absolutetime, Channel, cmd_param1, cmd_param2);
				(*midifile).addNoteOff(Track, absolutetime + delta2, Channel, cmd_param1);
			}
			else
			{
				XNFS_printf(1, "GTSeqData: %X\n", GTSeqData);
				XNFS_printf(1, "PANIC! UNKNOWN COMMAND: %X\nPAUSING HERE!\noffset: %X\n", cmd, Cursor);
				getchar();
			}
			break;
		}
	}
}

void ConvertToMidi(const char* OutFileName)
{
	MidiFile midifile;
	midifile.absoluteTicks();

	unsigned int CurChannel = 0;

	// endianness kicks in...
	unsigned int TrackSizeOffset = 0;
	unsigned int CurMelodicChannel = 0;

	midifile.setTicksPerQuarterNote(GTSEQ_PPQN);

	// start master track for tempo shenanigans
	TrackNums[0] = midifile.addTrack();
	midifile.addTempo(TrackNums[0], 0, ConvertGTTempo(GTSeqHead[CurrentSequence].TempoMS));
	// end master track

	// start deciphering GT sequence tracks and convert them to MIDI

	for (unsigned int i = 0; i < GTSEQ_TRACKCOUNT; i++)
	{
		TrackNums[i + 1] = midifile.addTrack();
		sprintf(TrackName, "Track 0x%x", GTSeqHead[CurrentSequence].TrackPointers[i]);
		midifile.addTrackName(TrackNums[i + 1], 0, TrackName);

		XNFS_printf(1, "Converting: %s\n", TrackName);
		ConvertEvents(&midifile, TrackNums[i + 1], CurChannel, GTSeqHead[CurrentSequence].TrackPointers[i]);
		
		CurChannel++;
		CurChannel %= 16;
	}

	midifile.sortTracks();
	midifile.write(OutFileName);
}

int main(int argc, char *argv[])
{
	printf("Polyphony GT sequence converter\n");
	if (argc < 2)
	{
		printf("ERROR: Too few arguments.\nUSAGE: %s GTSeqFile [-v/-vv]\n", argv[0]);
		return -1;
	}

	printf("Opening %s\n", argv[1]);
	FILE *fin = fopen(argv[1], "rb");
	if (fin == NULL)
	{
		perror("ERROR");
		return -1;
	}

	// printout verbosity level check
	// TODO: add more options
	if (argv[2] != NULL)
	{
		if (argv[2][0] == '-')
		{
			if (argv[2][1] == 'v')
			{
				VerbosityLevel = 2;
				if (argv[2][2] == 'v')
					VerbosityLevel = 3;
			}
		}
	}
	FileSize = GetFileSize(argv[1]);
	ParseMainSeqData(fin);
	PrintInfo();
	strcpy(OutFileName, argv[1]);
	strcpy(&OutFileName[strlen(OutFileName) - 3], "mid");

	if (GTSeqMain.SeqCount >= 2)
	{
		for (unsigned int i = 0; i < GTSeqMain.SeqCount; i++)
		{
			XNFS_printf(1, "Converting sequence %d tracks...\n", i+1);
			CurrentSequence = i;
			strcpy(MultiSeqName, OutFileName);
			sprintf(&MultiSeqName[strlen(MultiSeqName) - 4], "_Seq%d.mid", i);
			ConvertToMidi(MultiSeqName);
		}
	}
	else
	{
		XNFS_printf(1, "Converting main tracks...\n");
		ConvertToMidi(OutFileName);
	}


	fclose(fin);
    return 0;
}


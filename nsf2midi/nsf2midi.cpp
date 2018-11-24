// Tool that converts NSF music to MIDI sequence with support for noise-channel and DMC-channel mapping
// to MIDI channels and notes.

#include "gme/Music_Emu.h"
#include "gme/Nsf_Emu.h"
#include "gme/Nes_Apu.h"
#include "gme/Nes_Oscs.h"

#include "Wave_Writer.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void handle_error( const char* str );

void fwrite_be_32(unsigned int n, FILE *m)
{
	int h = htonl(n);
	fwrite(&h, 1, 4, m);
}

void fwrite_be_16(unsigned short n, FILE *m)
{
	int h = htons(n);
	fwrite(&h, 1, 2, m);
}

int main(int argc, char **argv)
{
	long sample_rate = 48000; // number of samples per second

	const char *filename;
	int track = 0; // index of track to play (0 = first)

	argc--;
	if (argc == 0) {
		fprintf(stderr, "nsf2midi <file.nsf> <track>\n");
		return -1;
	}

	if (argc >= 1) {
		filename = argv[1];
	}
	if (argc >= 2) {
		track = atoi(argv[2]);
	}

	// replace '.nsf' extension with '.n2m' and try to open that file:
	char *support_filename = (char *)malloc(strlen(filename)+1);
	strcpy(support_filename, filename);
	strcpy(strrchr(support_filename, '.')+1, "n2m");

	// Determine file type
	gme_type_t file_type;
	handle_error( gme_identify_file( filename, &file_type ) );
	if ( !file_type )
		handle_error( "Unsupported music type" );
	
	// Create emulator and set sample rate
	Music_Emu* emu = file_type->new_emu();
	if ( !emu )
		handle_error( "Out of memory" );
	handle_error( emu->set_sample_rate( sample_rate ) );
	
	// Load music file into emulator
	handle_error( emu->load_file( filename ) );
	
	bool midi_supported = emu->midi_supported();
	if (midi_supported) {
		emu->midi_load_support_file(support_filename);
	}

	// Start track
	handle_error( emu->start_track( track ) );

	// replace '.nsf' extension with '.wav':
	char *wav_filename = (char *)malloc(strlen(filename)+4+1);
	strcpy(wav_filename, filename);
	sprintf(strrchr(wav_filename, '.'), " %d.wav", track);

	// Begin writing to wave file
	Wave_Writer wave( sample_rate, wav_filename );
	wave.enable_stereo();
	
	// Record 10 seconds of track
	while ( emu->tell() < (60 + 60 + 60) * 1000L )
	{
		// Sample buffer
		const long size = 1024; // can be any multiple of 2
		short buf [size];
		
		// Fill buffer
		handle_error( emu->play( size, buf ) );
		
		// Write samples to wave file
		wave.write( buf, size );
	}

	if (midi_supported) {
		// replace '.nsf' extension with '.mid':
		char *mid_filename = (char *)malloc(strlen(filename)+4+1);
		strcpy(mid_filename, filename);
		sprintf(strrchr(mid_filename, '.'), " %d.mid", track);

		// Write MIDI file, format 1:
		FILE *m = fopen(mid_filename, "wb");
		fwrite("MThd", 1, 4, m);
		// MThd length:
		fwrite_be_32(6, m);
		// format 1:
		fwrite_be_16(1, m);
		// track count:
		fwrite_be_16(emu->midi_track_count(), m);
		// division:
		fwrite_be_16(0x8000 | (((0x80 - Nes_Osc::frames_per_second) & 0x7F) << 8) | (Nes_Osc::ticks_per_frame & 0xFF), m);

		for (int i = 0; i < emu->midi_track_count(); i++) {
			const blargg_vector<unsigned char>& mtrk = emu->midi_track_mtrk(i);
			fwrite("MTrk", 1, 4, m);
			// MTrk length:
			fwrite_be_32(mtrk.size(), m);
			fwrite(mtrk.begin(), 1, mtrk.size(), m);
		}

		fclose(m);

		// Write supporting n2m file if it didn't exist before:
		emu->midi_write_support_file(support_filename);
	}

	// Cleanup
	delete emu;
	
	return 0;
}

void handle_error( const char* str )
{
	if ( str )
	{
		printf( "Error: %s\n", str ); getchar();
		exit( EXIT_FAILURE );
	}
}

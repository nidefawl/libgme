// C++ example that opens a game music file and records 10 seconds to "out.wav"

static char filename [] = "solstice.nsf"; /* opens this file (can be any music type) */

#include "gme/Music_Emu.h"
#include "gme/Nsf_Emu.h"
#include "gme/Nes_Apu.h"
#include "gme/Nes_Oscs.h"

#include "Wave_Writer.h"
#include <stdlib.h>
#include <stdio.h>

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

int main()
{
	long sample_rate = 48000; // number of samples per second
	int track = 5; // index of track to play (0 = first)

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
	
	// Start track
	handle_error( emu->start_track( track ) );
	
	// Begin writing to wave file
	Wave_Writer wave( sample_rate, "out.wav" );
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

	// Write MIDI file:
	FILE *m = fopen("out.mid", "wb");
	fwrite("MThd", 1, 4, m);
	// MThd length:
	fwrite_be_32(6, m);
	// format 1
	fwrite_be_16(1, m);
	// 3 tracks
	fwrite_be_16(3, m);
	// division:
	fwrite_be_16(0x8000 | (((0x80 - Nes_Osc::frames_per_second) & 0x7F) << 8) | (Nes_Osc::ticks_per_frame & 0xFF), m);

	Nes_Apu *apu = ((Nsf_Emu*)emu)->apu_();

	// apu->osc_count
	for (int i = 0; i < 3; i++) {
		Nes_Osc *osc = apu->get_osc(i);
		fwrite("MTrk", 1, 4, m);
		// MTrk length:
		fwrite_be_32(osc->mtrk_p, m);
		fwrite(osc->mtrk.begin(), 1, osc->mtrk_p, m);
	}

	fclose(m);

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

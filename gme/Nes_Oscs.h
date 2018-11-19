// Private oscillators used by Nes_Apu

// Nes_Snd_Emu 0.1.8
#ifndef NES_OSCS_H
#define NES_OSCS_H

#include "blargg_common.h"
#include "Blip_Buffer.h"
#include <stdio.h>
#include <math.h>

class Nes_Apu;

struct MIDICommand
{
	int tick;
	unsigned char cmd;
	unsigned char channel;
	unsigned char d1;
	unsigned char d2;
};

struct Nes_Osc
{
	int index;
	unsigned char regs [4];
	bool reg_written [4];
	Blip_Buffer* output;
	int length_counter;// length counter (0 if unused by oscillator)
	int delay;      // delay until next (potential) transition
	int last_amp;   // last amplitude oscillator was outputting

	// MIDI state:
	nes_time_t abs_time;
	double clock_rate_;
	unsigned char period_midi[0x800];
	short period_cents[0x800];

	void set_clock_rate(double clock_rate) {
		clock_rate_ = clock_rate;
		// Pre-compute period->MIDI calculation:
		// Concert A# = 116.540940
		//     NES A# = 116.521662
		// Concert A  = 110
		//     NES A  = 109.981803632778603
		for (int p = 0; p < 0x800; ++p) {
			double f = clock_rate_ / (16 * (p + 1));
			double n = (log(f / 109.981803632778603) / log(2)) * 12;
			// 21 = MIDI A1
			int m = round(n) + 21;
			period_midi[p] = m;
			period_cents[p] = (short)((n - round(n)) * 8191);
		}
	}

	virtual unsigned char midi_note() const { return period_midi[period()]; }
	virtual unsigned char midi_volume() const = 0;

	blargg_vector<MIDICommand> midi_buf;
	int last_period;
	unsigned char last_midi_note;

	double seconds(nes_time_t time) {
		return (double)(abs_time + time) / clock_rate_;
	}

	void midi_note_on(nes_time_t time) {
		printf("%11f %*s %3d %3d\n", seconds(time), index * 8, "", midi_note(), midi_volume());
	}

	void midi_note_off(nes_time_t time) {
		if (last_midi_note == 0) {
			return;
		}

		printf("%11f %*s %3d %3d\n", seconds(time), index * 8, "", last_midi_note, 0);

		last_period = 0;
		last_midi_note = 0;
	}

	void note_on(nes_time_t time) {
		int p = period();
		unsigned char m = period_midi[p];

		if (m != last_midi_note) {
			midi_note_off(abs_time + time);
			midi_note_on(abs_time + time);
		}

		last_period = p;
		last_midi_note = m;
	}
	void note_off(nes_time_t time) {
		midi_note_off(abs_time + time);
	}

	virtual void reg_write(int reg, unsigned char value) {
		regs [reg] = value;
		reg_written [reg] = true;
	}

	void clock_length( int halt_mask );
	int period() const {
		return (regs [3] & 7) * 0x100 + (regs [2] & 0xFF);
	}
	void reset() {
		delay = 0;
		last_amp = 0;
		last_period = 0;
		last_midi_note = 0;
		abs_time = 0;
	}
	int update_amp( int amp ) {
		int delta = amp - last_amp;
		last_amp = amp;
		return delta;
	}
};

struct Nes_Envelope : Nes_Osc
{
	int envelope;
	int env_delay;
	
	void clock_envelope();
	int volume() const;
	unsigned char midi_volume() const { return volume() * 16; }
	void reset() {
		envelope = 0;
		env_delay = 0;
		Nes_Osc::reset();
	}
};

// Nes_Square
struct Nes_Square : Nes_Envelope
{
	enum { negate_flag = 0x08 };
	enum { shift_mask = 0x07 };
	enum { phase_range = 8 };
	int phase;
	int sweep_delay;
	
	typedef Blip_Synth<blip_good_quality,1> Synth;
	Synth const& synth; // shared between squares
	
	Nes_Square( Synth const* s ) : synth( *s ) { }
	
	void clock_sweep( int adjust );
	void run( nes_time_t, nes_time_t );
	void reset() {
		sweep_delay = 0;
		Nes_Envelope::reset();
	}
	nes_time_t maintain_phase( nes_time_t time, nes_time_t end_time,
			nes_time_t timer_period );
};

// Nes_Triangle
struct Nes_Triangle : Nes_Osc
{
	enum { phase_range = 16 };
	int phase;
	int linear_counter;
	Blip_Synth<blip_med_quality,1> synth;
	
	int calc_amp() const;
	unsigned char midi_volume() const { return 4 * 16; }
	void run( nes_time_t, nes_time_t );
	void clock_linear_counter();
	void reset() {
		linear_counter = 0;
		phase = 1;
		Nes_Osc::reset();
	}
	nes_time_t maintain_phase( nes_time_t time, nes_time_t end_time,
			nes_time_t timer_period );
};

// Nes_Noise
struct Nes_Noise : Nes_Envelope
{
	int noise;
	Blip_Synth<blip_med_quality,1> synth;
	
	void run( nes_time_t, nes_time_t );
	void reset() {
		noise = 1 << 14;
		Nes_Envelope::reset();
	}
};

// Nes_Dmc
struct Nes_Dmc : Nes_Osc
{
	int address;    // address of next byte to read
	int period;
	//int length_counter; // bytes remaining to play (already defined in Nes_Osc)
	int buf;
	int bits_remain;
	int bits;
	bool buf_full;
	bool silence;
	
	enum { loop_flag = 0x40 };
	
	int dac;
	
	nes_time_t next_irq;
	bool irq_enabled;
	bool irq_flag;
	bool pal_mode;
	bool nonlinear;
	
	int (*prg_reader)( void*, nes_addr_t ); // needs to be initialized to prg read function
	void* prg_reader_data;
	
	Nes_Apu* apu;
	
	Blip_Synth<blip_med_quality,1> synth;
	
	void start();
	void write_register( int, int );
	void run( nes_time_t, nes_time_t );
	void recalc_irq();
	void fill_buffer();
	void reload_sample();
	void reset();
	int count_reads( nes_time_t, nes_time_t* ) const;
	nes_time_t next_read_time() const;

	virtual unsigned char midi_volume() const { return 7 * 16; }
};

#endif

// Private oscillators used by Nes_Apu

// Nes_Snd_Emu 0.1.8
#ifndef NES_OSCS_H
#define NES_OSCS_H

#include "blargg_common.h"
#include "Blip_Buffer.h"
#include <stdio.h>
#include <math.h>

class Nes_Apu;

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

	virtual unsigned char midi_note_a() const = 0;

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
			int m = round(n) + midi_note_a();
			period_midi[p] = m;
			period_cents[p] = (short)((n - round(n)) * 8191);
		}
	}

	virtual unsigned char midi_note() const { return period_midi[period()]; }
	virtual unsigned char midi_volume() const = 0;

	blargg_vector<unsigned char> mtrk;
	size_t mtrk_p;
	int last_tick;
	int last_period;
	unsigned char last_midi_note;

	static const int frames_per_second = 30;
	static const int ticks_per_frame = 80;

	double seconds(nes_time_t time) {
		return (double)(abs_time + time) / clock_rate_;
	}

	void midi_ensure(size_t n) {
		size_t new_size = mtrk.size();
		while ((mtrk_p + n) > new_size) {
			new_size *= 2;
		}
		if (new_size > mtrk.size()) {
			mtrk.resize(new_size);
		}
	}

	void midi_write_time(nes_time_t time) {
		double s = seconds(time);
		int abs_tick = (int)(s * frames_per_second * ticks_per_frame);
		int ticks = abs_tick - last_tick;
		last_tick = abs_tick;

		unsigned char chr1 = (unsigned char)(ticks & 0x7F);
		ticks >>= 7;
		if (ticks > 0) {
		    unsigned char chr2 = (unsigned char)((ticks & 0x7F) | 0x80);
		    ticks >>= 7;
		    if (ticks > 0) {
		        unsigned char chr3 = (unsigned char)((ticks & 0x7F) | 0x80);
		        ticks >>= 7;
		        if (ticks > 0) {
		            unsigned char chr4 = (unsigned char)((ticks & 0x7F) | 0x80);

					midi_ensure(4);
					unsigned char *p = mtrk.begin();
					p[mtrk_p++] = chr4;
					p[mtrk_p++] = chr3;
					p[mtrk_p++] = chr2;
					p[mtrk_p++] = chr1;
		        } else {
					midi_ensure(3);
					unsigned char *p = mtrk.begin();
					p[mtrk_p++] = chr3;
					p[mtrk_p++] = chr2;
					p[mtrk_p++] = chr1;
		        }
		    } else {
				midi_ensure(2);
				unsigned char *p = mtrk.begin();
				p[mtrk_p++] = chr2;
				p[mtrk_p++] = chr1;
		    }
		} else {
			midi_ensure(1);
			unsigned char *p = mtrk.begin();
			p[mtrk_p++] = chr1;
		}
	}

	void midi_write_2(unsigned char cmd, unsigned char data1) {
		midi_ensure(2);
		unsigned char *p = mtrk.begin();
		p[mtrk_p++] = cmd;
		p[mtrk_p++] = data1;
	}

	void midi_write_3(unsigned char cmd, unsigned char data1, unsigned char data2) {
		midi_ensure(3);
		unsigned char *p = mtrk.begin();
		p[mtrk_p++] = cmd;
		p[mtrk_p++] = data1;
		p[mtrk_p++] = data2;
	}

	void midi_note_on(nes_time_t time) {
		printf("%11f %*s %3d %3d\n", seconds(time), index * 8, "", midi_note(), midi_volume());
		midi_write_time(time);
		midi_write_3(
			(0x90 | (index & 0x0F)),
			midi_note(),
			midi_volume()
		);
	}

	void midi_note_off(nes_time_t time) {
		if (last_midi_note == 0) {
			return;
		}

		printf("%11f %*s %3d %3d\n", seconds(time), index * 8, "", last_midi_note, 0);
		midi_write_time(time);
		midi_write_3(
			(0x80 | (index & 0x0F)),
			last_midi_note,
			0
		);

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

		mtrk.resize(30000);
		mtrk_p = 0;
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

	// 45 = MIDI A3 (110 Hz)
	unsigned char midi_note_a() const { return 45; }

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
	// 33 = MIDI A2 (110 Hz)
	unsigned char midi_note_a() const { return 33; }
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

	// 45 = MIDI A3 (110 Hz)
	unsigned char midi_note_a() const { return 45; }

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

	// 45 = MIDI A3 (110 Hz)
	unsigned char midi_note_a() const { return 45; }

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

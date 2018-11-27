// Fast SNES SPC-700 DSP emulator (about 3x speed of accurate one)

// Game_Music_Emu 0.5.5
#ifndef SPC_DSP_H
#define SPC_DSP_H

#include "blargg_common.h"
#include "blargg_endian.h"
#include "Music_Emu.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

struct Spc_Dsp {
public:
	typedef BOOST::uint8_t uint8_t;
	
// Setup
	
	// Initializes DSP and has it use the 64K RAM provided
	void init( void* ram_64k );

	// Sets destination for output samples. If out is NULL or out_size is 0,
	// doesn't generate any.
	typedef short sample_t;
	void set_output( sample_t* out, int out_size );

	// Number of samples written to output since it was last set, always
	// a multiple of 2. Undefined if more samples were generated than
	// output buffer could hold.
	int sample_count() const;

// Emulation
	
	// Resets DSP to power-on state
	void reset();

	// Emulates pressing reset switch on SNES
	void soft_reset();
	
	// Reads/writes DSP registers. For accuracy, you must first call spc_run_dsp()
	// to catch the DSP up to present.
	int  read ( int addr ) const;
	void write( int addr, int data );

	// Runs DSP for specified number of clocks (~1024000 per second). Every 32 clocks
	// a pair of samples is be generated.
	void run( int clock_count );

// Sound control

	// Mutes voices corresponding to non-zero bits in mask (overrides VxVOL with 0).
	// Reduces emulation accuracy.
	enum { voice_count = 8 };
	void mute_voices( int mask );

	// If true, prevents channels and global volumes from being phase-negated
	void disable_surround( bool disable = true );

// State
	
	// Resets DSP and uses supplied values to initialize registers
	enum { register_count = 128 };
	void load( uint8_t const regs [register_count] );

// DSP register addresses

	// Global registers
	enum {
	    r_mvoll = 0x0C, r_mvolr = 0x1C,
	    r_evoll = 0x2C, r_evolr = 0x3C,
	    r_kon   = 0x4C, r_koff  = 0x5C,
	    r_flg   = 0x6C, r_endx  = 0x7C,
	    r_efb   = 0x0D, r_pmon  = 0x2D,
	    r_non   = 0x3D, r_eon   = 0x4D,
	    r_dir   = 0x5D, r_esa   = 0x6D,
	    r_edl   = 0x7D,
	    r_fir   = 0x0F // 8 coefficients at 0x0F, 0x1F ... 0x7F
	};

	// Voice registers
	enum {
		v_voll   = 0x00, v_volr   = 0x01,
		v_pitchl = 0x02, v_pitchh = 0x03,
		v_srcn   = 0x04, v_adsr0  = 0x05,
		v_adsr1  = 0x06, v_gain   = 0x07,
		v_envx   = 0x08, v_outx   = 0x09
	};

public:
	enum { extra_size = 16 };
	sample_t* extra()               { return m.extra; }
	sample_t const* out_pos() const { return m.out; }
public:
	BLARGG_DISABLE_NOTHROW
	
	typedef BOOST::int8_t   int8_t;
	typedef BOOST::int16_t int16_t;
	
	enum { echo_hist_size = 8 };
	
	enum env_mode_t { env_release, env_attack, env_decay, env_sustain };
	enum { brr_buf_size = 12 };
	struct voice_t
	{
		int buf [brr_buf_size*2];// decoded samples (twice the size to simplify wrap handling)
		int* buf_pos;           // place in buffer where next samples will be decoded
		int interp_pos;         // relative fractional position in sample (0x1000 = 1.0)
		int brr_addr;           // address of current BRR block
		int brr_offset;         // current decoding offset in BRR block
		int kon_delay;          // KON delay/current setup phase
		env_mode_t env_mode;
		int env;                // current envelope level
		int hidden_env;         // used by GAIN mode 7, very obscure quirk
		int volume [2];         // copy of volume from DSP registers, with surround disabled
		int enabled;            // -1 if enabled, 0 if muted
	};
private:
	struct state_t
	{
		uint8_t regs [register_count];
		
		// Echo history keeps most recent 8 samples (twice the size to simplify wrap handling)
		int echo_hist [echo_hist_size * 2] [2];
		int (*echo_hist_pos) [2]; // &echo_hist [0 to 7]
		
		int every_other_sample; // toggles every sample
		int kon;                // KON value when last checked
		int noise;
		int echo_offset;        // offset from ESA in echo buffer
		int echo_length;        // number of bytes that echo_offset will stop at
		int phase;              // next clock cycle to run (0-31)
		unsigned counters [4];
		
		int new_kon;
		int t_koff;
		
		voice_t voices [voice_count];
		
		unsigned* counter_select [32];
		
		// non-emulation state
		uint8_t* ram; // 64K shared RAM between DSP and SMP
		int mute_mask;
		int surround_threshold;
		sample_t* out;
		sample_t* out_end;
		sample_t* out_begin;
		sample_t extra [extra_size];
	};
	state_t m;
	
	void init_counter();
	void run_counter( int );
	void soft_reset_common();
	void write_outline( int addr, int data );
	void update_voice_vol( int addr );

public:
// MIDI conversion support:
	MidiTrack midi[voice_count];
	int abs_sample;
	midi_tick_t abs_tick() { return (midi_tick_t)(abs_sample * 3.590664272890485); }

	struct voice_midi_state {
		int sample;
		int pitch;
		int gain;

		int midi_channel;
	};
	voice_midi_state voice_midi[voice_count];

	struct midi_channel_state {
		int note;
		int patch;

		int pan;
		int volume;
	};
	midi_channel_state midi_channel[16];

	struct sample_midi_config {
		bool used;	// whether or not this sample number is actually used in the song

		// melodic_transpose || percussion_note are mutually exclusive
		int melodic_patch;	 // MIDI patch number (GM) that represents this sample best
		int melodic_transpose;    // MIDI note number this sample was recorded at
		int percussion_note; // MIDI percussion note on channel 10

		double base_pitch;
		double gain;

		int midi_channel(int voice) {
			if (percussion_note > 0) {
				return 9;
			}
			return voice;
		}
		int midi_patch() {
			if (percussion_note > 0) {
				return 0;
			}
			return melodic_patch;
		}
		double midi_note(int pitch) {
			if (percussion_note > 0) {
				return percussion_note;
			}

			double scale = pitch / (double)0x1000;
			double m = ((log(base_pitch * scale) / log(2)) * 12) - 36 + melodic_transpose;

			return m;
		}
	};
	sample_midi_config sample_midi[256];

	int voice_pitch(int voice) {
		int pitch = GET_LE16( &m.regs[voice * 0x10 + v_pitchl] ) & 0x3FFF;
		return pitch;
	}
	int voice_sample(int voice) {
		int sample = m.regs[voice * 0x10 + v_srcn];
		return sample;
	}

	double midi_note(int voice) {
		int sample = voice_sample(voice);
		int pitch = voice_pitch(voice);
		return sample_midi[sample].midi_note(pitch);
	}

	void decode_sample(int dir, int sample, short *buf, size_t buf_size, size_t *loop_pos);

	void note_on(voice_t *v)
	{
		int voice = v - m.voices;
		voice_midi_state &vm = voice_midi[voice];
		midi_tick_t tick = abs_tick();
		char sample_s[10];

		int directory = m.regs[r_dir];
		int sample = voice_sample(voice);
		sample_midi_config &spl = sample_midi[sample];

		// Mark sample as used:
		if (!spl.used) {
			// Decode a bit of the sample to be used:
			const size_t n = 1024;
			const size_t buf_size = 16384;
			size_t loop_pos = 0;
			short *buf = (short *)malloc(sizeof(short) * (buf_size + brr_buf_size));
			double real[n];
			double imag[n];

			printf("%02X:%02X sample:\n", directory, sample);

			for (int i = 0; i < buf_size + brr_buf_size; i++) {
				buf[i] = 0;
			}

			decode_sample(directory, sample, buf, buf_size, &loop_pos);

			// Calculate cheap peak gain to normalize samples against one another for MIDI conversion:
			int max = 0;
			for (int i = 0; i < buf_size; i++) {
				int sp = abs(buf[i]);
				if (max < sp) {
					max = sp;
				}
			}

			spl.gain = (double)max / 32768.0;

			char fname[14];
			sprintf(fname, "sample%02X.wav", sample);
			write_wave_file(fname, buf, buf_size, 32000);

			// Determine base frequency of sample using FFT over buf:
			if (loop_pos+n < buf_size) {
				// Take last N samples, assuming it's all looped:
				for (int i = 0; i < n; i++)
				{
					real[i] = buf[16384-n+i] / 32768.0;
					imag[i] = 0;
				}
			} else {
				// Just take first N samples:
				for (int i = 0; i < n; i++)
				{
					real[i] = buf[i] / 32768.0;
					imag[i] = 0;
				}
			}

			// Take FFT:
			fft(real, imag, n);

			// Take abs magnitude of FFT result:
			fft_mag(real, imag, n);

		#if 0
			for (int i = 1; i < n/2; i++)
			{
				printf("  [%3d] %9.5f\n", i, real[i]);
			}
		#endif

			const int peak_count = 8;
			int peaks[peak_count];
			fft_peaks(real, n, peaks, peak_count);

			int k = fft_min_peak(peaks, peak_count, 4);

			printf(
				"  min peak is FFT bin #%4d of possible peaks [%4d, %4d, %4d, %4d, %4d, %4d, %4d, %4d]\n",
				k,
				peaks[0],
				peaks[1],
				peaks[2],
				peaks[3],
				peaks[4],
				peaks[5],
				peaks[6],
				peaks[7]
			);

			// Interpolate FFT bins to find more exact frequency:
			double y1 = real[k-1];
			double y2 = real[k];
			double y3 = real[k+1];
			double kp;
			if (y1 > y3) {
				if (y1 > 0) {
					double a = y2 / y1;
					double d = a / (1 + a);
					kp = k - 1 + d;
				} else {
					kp = k;
				}
			} else {
				if (y2 > 0) {
					double a = y3 / y2;
					double d = a / (1 + a);
					kp = k + d;
				} else {
					kp = k;
				}
			}

			spl.base_pitch = kp * 32000.0 / (double)n;

			const char note_names[12][3] = {
				"A ",
				"A#",
				"B ",
				"C ",
				"C#",
				"D ",
				"D#",
				"E ",
				"F ",
				"F#",
				"G ",
				"G#"
			};

			int x = (int)round((log(spl.base_pitch / 55.0) / log(2)) * 12);
			const char *note_name = note_names[x % 12];
			int note_oct = (x / 12) + 1;

			spl.used = true;
			char loopmsg[15 + 5 + 1];
			if (loop_pos < 16384)
			{
				sprintf(loopmsg, "loop starts at %5ld", loop_pos);
			}
			else
			{
				sprintf(loopmsg, "no looping");
			}
			printf("  f = %9.3f (%s%1d), gain = %9.8f, %s\n", spl.base_pitch, note_name, note_oct, spl.gain, loopmsg);

			free(buf);
		}

		// Get MIDI note:
		int m = (int)round(midi_note(voice));
		vm.pitch = voice_pitch(voice);

		int ch = vm.midi_channel;

		if (vm.sample != sample)
		{
			ch = spl.midi_channel(voice);

			// Write a text event describing sample number:
			sprintf(sample_s, "sample %02X", sample);
			midi[voice].write_meta(tick, 0x01, strlen(sample_s), sample_s);
			vm.sample = sample;
			vm.midi_channel = ch;

			// Write a patch change:
			int new_patch = spl.midi_patch();
			if (midi_channel[ch].patch != new_patch)
			{
				midi[voice].write_2(
					tick,
					0xC0 | ch,
					new_patch
				);
				midi_channel[ch].patch = new_patch;
			}
		}

		int v0 = v->volume[0] * 3;
		int v1 = v->volume[1] * 3;

		int pan = 64 - (v0 >> 1) + (v1 >> 1);
		if (pan != midi_channel[ch].pan)
		{
			midi[voice].write_3(
				tick,
				0xB0 | ch,
				10, // Pan
				pan
			);
			midi_channel[ch].pan = pan;
		}

		int vel = (int)(spl.gain * ((v0 + v1) >> 1));

		midi[voice].write_3(
			tick,
			0x90 | ch,
			m,
			vel
		);
		midi_channel[ch].note = m;
	}

	void note_gain(voice_t *v, int env)
	{
		int voice = v - m.voices;
		voice_midi_state &vm = voice_midi[voice];
		int ch = vm.midi_channel;
		if (midi_channel[ch].note == 0) return;
		if (vm.gain == env) return;

		midi_tick_t tick = abs_tick();

		int directory = m.regs[r_dir];
		int sample = voice_sample(voice);
		sample_midi_config &spl = sample_midi[sample];

		// printf("%d gain=%02X\n", voice, env);
		vm.gain = env;
	}

	void note_pitch(voice_t *v, int pitch)
	{
		int voice = v - m.voices;
		voice_midi_state &vm = voice_midi[voice];
		int ch = vm.midi_channel;
		if (midi_channel[ch].note == 0) return;
		if (vm.pitch == pitch) return;

		midi_tick_t tick = abs_tick();

		int directory = m.regs[r_dir];
		int sample = voice_sample(voice);
		sample_midi_config &spl = sample_midi[sample];

		// printf("%d pitch=%04X\n", voice, pitch);
		vm.pitch = pitch;
	}

	void note_off(voice_t *v)
	{
		int voice = v - m.voices;
		voice_midi_state &vm = voice_midi[voice];
		int ch = vm.midi_channel;
		midi_tick_t tick = abs_tick();

		midi[voice].write_3(
			tick,
			0x80 | ch,
			midi_channel[ch].note,
			0x00
		);
		midi_channel[ch].note = 0;
		vm.pitch = 0;
	}
};

#include <assert.h>

inline int Spc_Dsp::sample_count() const { return m.out - m.out_begin; }

inline int Spc_Dsp::read( int addr ) const
{
	assert( (unsigned) addr < register_count );
	return m.regs [addr];
}

inline void Spc_Dsp::update_voice_vol( int addr )
{
	int l = (int8_t) m.regs [addr + v_voll];
	int r = (int8_t) m.regs [addr + v_volr];
	
	if ( l * r < m.surround_threshold )
	{
		// signs differ, so negate those that are negative
		l ^= l >> 7;
		r ^= r >> 7;
	}
	
	voice_t& v = m.voices [addr >> 4];
	int enabled = v.enabled;
	v.volume [0] = l & enabled;
	v.volume [1] = r & enabled;
}

inline void Spc_Dsp::write( int addr, int data )
{
	assert( (unsigned) addr < register_count );
	
	m.regs [addr] = (uint8_t) data;
	int low = addr & 0x0F;
	if ( low < 0x2 ) // voice volumes
	{
		update_voice_vol( low ^ addr );
	}
	else if ( low == 0xC )
	{
		if ( addr == r_kon )
			m.new_kon = (uint8_t) data;
		
		if ( addr == r_endx ) // always cleared, regardless of data written
			m.regs [r_endx] = 0;
	}
}

inline void Spc_Dsp::disable_surround( bool disable )
{
	m.surround_threshold = disable ? 0 : -0x4000;
}

#define SPC_NO_COPY_STATE_FUNCS 1

#define SPC_LESS_ACCURATE 1

#endif

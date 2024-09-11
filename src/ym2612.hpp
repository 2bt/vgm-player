#pragma once

#include <cstdint>
#include <cassert>


namespace foobar {

template<typename ArrayType, int ArraySize>
constexpr uint32_t array_size(ArrayType (&array)[ArraySize])
{
    return ArraySize;
}

inline uint32_t bitfield(uint32_t value, int start, int length = 1)
{
	return (value >> start) & ((1 << length) - 1);
}


inline int32_t clamp(int32_t value, int32_t minval, int32_t maxval)
{
	if (value < minval)
		return minval;
	if (value > maxval)
		return maxval;
	return value;
}

enum envelope_state : uint32_t
{
    EG_DEPRESS = 0,		// OPLL only; set EG_HAS_DEPRESS to enable
    EG_ATTACK = 1,
    EG_DECAY = 2,
    EG_SUSTAIN = 3,
    EG_RELEASE = 4,
    EG_STATES = 5
};


// this class holds data that is computed once at the start of clocking
// and remains static during subsequent sound generation
struct opdata_cache
{
	// set phase_step to this value to recalculate it each sample; needed
	// in the case of PM LFO changes
	static constexpr uint32_t PHASE_STEP_DYNAMIC = 1;

	uint16_t const *waveform;         // base of sine table
	uint32_t phase_step;              // phase step, or PHASE_STEP_DYNAMIC if PM is active
	uint32_t total_level;             // total level * 8 + KSL
	uint32_t block_freq;              // raw block frequency value (used to compute phase_step)
	int32_t detune;                   // detuning value (used to compute phase_step)
	uint32_t multiple;                // multiple value (x.1, used to compute phase_step)
	uint32_t eg_sustain;              // sustain level, shifted up to envelope values
    uint8_t eg_rate[EG_STATES];       // envelope rate, including KSR, [0;63]
};

// OPN register map:
//
//      System-wide registers:
//           21 xxxxxxxx Test register
//           22 ----x--- LFO enable [OPNA+ only]
//              -----xxx LFO rate [OPNA+ only]
//           27 xx------ CSM/Multi-frequency mode for channel #2
//           28 x------- Key on/off operator 4
//              -x------ Key on/off operator 3
//              --x----- Key on/off operator 2
//              ---x---- Key on/off operator 1
//              ------xx Channel select
//
//     Per-channel registers (channel in address bits 0-1)
//     Note that all these apply to address+100 as well on OPNA+
//        A0-A3 xxxxxxxx Frequency number lower 8 bits
//        A4-A7 --xxx--- Block (0-7)
//              -----xxx Frequency number upper 3 bits
//        B0-B3 --xxx--- Feedback level for operator 1 (0-7)
//              -----xxx Operator connection algorithm (0-7)
//        B4-B7 x------- Pan left [OPNA]
//              -x------ Pan right [OPNA]
//              --xx---- LFO AM shift (0-3) [OPNA+ only]
//              -----xxx LFO PM depth (0-7) [OPNA+ only]
//
//     Per-operator registers (channel in address bits 0-1, operator in bits 2-3)
//     Note that all these apply to address+100 as well on OPNA+
//        30-3F -xxx---- Detune value (0-7)
//              ----xxxx Multiple value (0-15)
//        40-4F -xxxxxxx Total level (0-127)
//        50-5F xx------ Key scale rate (0-3)
//              ---xxxxx Attack rate (0-31)
//        60-6F x------- LFO AM enable [OPNA]
//              ---xxxxx Decay rate (0-31)
//        70-7F ---xxxxx Sustain rate (0-31)
//        80-8F xxxx---- Sustain level (0-15)
//              ----xxxx Release rate (0-15)
//        90-9F ----x--- SSG-EG enable
//              -----xxx SSG-EG envelope (0-7)
//
//     Special multi-frequency registers (channel implicitly #2; operator in address bits 0-1)
//        A8-AB xxxxxxxx Frequency number lower 8 bits
//        AC-AF --xxx--- Block (0-7)
//              -----xxx Frequency number upper 3 bits
//
//     Internal (fake) registers:
//        B8-BB --xxxxxx Latched frequency number upper bits (from A4-A7)
//        BC-BF --xxxxxx Latched frequency number upper bits (from AC-AF)
//



class opn_registers_base
{
public:
	static constexpr uint32_t WAVEFORM_LENGTH = 0x400;
    static constexpr uint32_t OUTPUTS = 2;
    static constexpr uint32_t CHANNELS = 6;
    static constexpr uint32_t ALL_CHANNELS = (1 << CHANNELS) - 1;
    static constexpr uint32_t OPERATORS = CHANNELS * 4;
    static constexpr uint32_t WAVEFORMS = 1;
    static constexpr uint32_t REGISTERS = 0x200;
    static constexpr uint32_t REG_MODE = 0x27;
    static constexpr uint32_t DEFAULT_PRESCALE = 6;
    static constexpr uint32_t EG_CLOCK_DIVIDER = 3;
    static constexpr bool     EG_HAS_SSG = true;
    static constexpr uint32_t CSM_TRIGGER_MASK = 1 << 2;

    // constructor
    opn_registers_base();

    // reset to initial state
    void reset();


    // map channel number to register offset
    static constexpr uint32_t channel_offset(uint32_t chnum)
    {
        assert(chnum < CHANNELS);
        return (chnum % 3) + 0x100 * (chnum / 3);
    }

    // map operator number to register offset
    static constexpr uint32_t operator_offset(uint32_t opnum)
    {
        assert(opnum < OPERATORS);
        return (opnum % 12) + ((opnum % 12) / 3) + 0x100 * (opnum / 12);
    }

    // return an array of operator indices for each channel
    struct operator_mapping { uint32_t chan[CHANNELS]; };
    void operator_map(operator_mapping &dest) const;

    // handle writes to the register array
    bool write(uint16_t index, uint8_t data, uint32_t &chan, uint32_t &opmask);

    // clock the noise and LFO, if present, returning LFO PM value
    int32_t clock_noise_and_lfo();

    // reset the LFO
    void reset_lfo() { m_lfo_counter = 0; }

    // return the AM offset from LFO for the given channel
    uint32_t lfo_am_offset(uint32_t choffs) const;

    // return LFO/noise states
    uint32_t noise_state() const { return 0; }

    // caching helpers
    void cache_operator_data(uint32_t choffs, uint32_t opoffs, opdata_cache &cache);

    // compute the phase step, given a PM value
    uint32_t compute_phase_step(uint32_t choffs, uint32_t opoffs, opdata_cache const &cache, int32_t lfo_raw_pm);

    // system-wide registers
    uint32_t test() const                       { return byte(0x21, 0, 8); }
    uint32_t lfo_enable() const                 { return byte(0x22, 3, 1); }
    uint32_t lfo_rate() const                   { return byte(0x22, 0, 3); }
    uint32_t csm() const                        { return (byte(0x27, 6, 2) == 2); }
    uint32_t multi_freq() const                 { return (byte(0x27, 6, 2) != 0); }
    uint32_t multi_block_freq(uint32_t num) const    { return word(0xac, 0, 6, 0xa8, 0, 8, num); }

    // per-channel registers
    uint32_t ch_block_freq(uint32_t choffs) const    { return word(0xa4, 0, 6, 0xa0, 0, 8, choffs); }
    uint32_t ch_feedback(uint32_t choffs) const      { return byte(0xb0, 3, 3, choffs); }
    uint32_t ch_algorithm(uint32_t choffs) const     { return byte(0xb0, 0, 3, choffs); }
    uint32_t ch_output_any(uint32_t choffs) const    { return byte(0xb4, 6, 2, choffs); }
    uint32_t ch_output_0(uint32_t choffs) const      { return byte(0xb4, 7, 1, choffs); }
    uint32_t ch_output_1(uint32_t choffs) const      { return byte(0xb4, 6, 1, choffs); }
    uint32_t ch_output_2(uint32_t choffs) const      { return 0; }
    uint32_t ch_output_3(uint32_t choffs) const      { return 0; }
    uint32_t ch_lfo_am_sens(uint32_t choffs) const   { return byte(0xb4, 4, 2, choffs); }
    uint32_t ch_lfo_pm_sens(uint32_t choffs) const   { return byte(0xb4, 0, 3, choffs); }

    // per-operator registers
    uint32_t op_detune(uint32_t opoffs) const        { return byte(0x30, 4, 3, opoffs); }
    uint32_t op_multiple(uint32_t opoffs) const      { return byte(0x30, 0, 4, opoffs); }
    uint32_t op_total_level(uint32_t opoffs) const   { return byte(0x40, 0, 7, opoffs); }
    uint32_t op_ksr(uint32_t opoffs) const           { return byte(0x50, 6, 2, opoffs); }
    uint32_t op_attack_rate(uint32_t opoffs) const   { return byte(0x50, 0, 5, opoffs); }
    uint32_t op_decay_rate(uint32_t opoffs) const    { return byte(0x60, 0, 5, opoffs); }
    uint32_t op_lfo_am_enable(uint32_t opoffs) const { return byte(0x60, 7, 1, opoffs); }
    uint32_t op_sustain_rate(uint32_t opoffs) const  { return byte(0x70, 0, 5, opoffs); }
    uint32_t op_sustain_level(uint32_t opoffs) const { return byte(0x80, 4, 4, opoffs); }
    uint32_t op_release_rate(uint32_t opoffs) const  { return byte(0x80, 0, 4, opoffs); }
    uint32_t op_ssg_eg_enable(uint32_t opoffs) const { return byte(0x90, 3, 1, opoffs); }
    uint32_t op_ssg_eg_mode(uint32_t opoffs) const   { return byte(0x90, 0, 3, opoffs); }

private:
	// helper to encode four operator numbers into a 32-bit value in the
	// operator maps for each register class
	static constexpr uint32_t operator_list(uint8_t o1 = 0xff, uint8_t o2 = 0xff, uint8_t o3 = 0xff, uint8_t o4 = 0xff)
	{
		return o1 | (o2 << 8) | (o3 << 16) | (o4 << 24);
	}

	// helper to apply KSR to the raw ADSR rate, ignoring ksr if the
	// raw value is 0, and clamping to 63
	static constexpr uint32_t effective_rate(uint32_t rawrate, uint32_t ksr)
	{
		return (rawrate == 0) ? 0 : std::min<uint32_t>(rawrate + ksr, 63);
	}
    // return a bitfield extracted from a byte
    uint32_t byte(uint32_t offset, uint32_t start, uint32_t count, uint32_t extra_offset = 0) const
    {
        return bitfield(m_regdata[offset + extra_offset], start, count);
    }

    // return a bitfield extracted from a pair of bytes, MSBs listed first
    uint32_t word(uint32_t offset1, uint32_t start1, uint32_t count1, uint32_t offset2, uint32_t start2, uint32_t count2, uint32_t extra_offset = 0) const
    {
        return (byte(offset1, start1, count1, extra_offset) << count2) | byte(offset2, start2, count2, extra_offset);
    }

    // internal state
    uint32_t m_lfo_counter;               // LFO counter
    uint8_t m_lfo_am;                     // current LFO AM value
    uint8_t m_regdata[REGISTERS];         // register data
    uint16_t m_waveform[WAVEFORMS][WAVEFORM_LENGTH]; // waveforms
};


//-------------------------------------------------
//  abs_sin_attenuation - given a sin (phase) input
//  where the range 0-2*PI is mapped onto 10 bits,
//  return the absolute value of sin(input),
//  logarithmically-adjusted and treated as an
//  attenuation value, in 4.8 fixed point format
//-------------------------------------------------

inline uint32_t abs_sin_attenuation(uint32_t input)
{
	// the values here are stored as 4.8 logarithmic values for 1/4 phase
	// this matches the internal format of the OPN chip, extracted from the die
	static uint16_t const s_sin_table[256] =
	{
		0x859,0x6c3,0x607,0x58b,0x52e,0x4e4,0x4a6,0x471,0x443,0x41a,0x3f5,0x3d3,0x3b5,0x398,0x37e,0x365,
		0x34e,0x339,0x324,0x311,0x2ff,0x2ed,0x2dc,0x2cd,0x2bd,0x2af,0x2a0,0x293,0x286,0x279,0x26d,0x261,
		0x256,0x24b,0x240,0x236,0x22c,0x222,0x218,0x20f,0x206,0x1fd,0x1f5,0x1ec,0x1e4,0x1dc,0x1d4,0x1cd,
		0x1c5,0x1be,0x1b7,0x1b0,0x1a9,0x1a2,0x19b,0x195,0x18f,0x188,0x182,0x17c,0x177,0x171,0x16b,0x166,
		0x160,0x15b,0x155,0x150,0x14b,0x146,0x141,0x13c,0x137,0x133,0x12e,0x129,0x125,0x121,0x11c,0x118,
		0x114,0x10f,0x10b,0x107,0x103,0x0ff,0x0fb,0x0f8,0x0f4,0x0f0,0x0ec,0x0e9,0x0e5,0x0e2,0x0de,0x0db,
		0x0d7,0x0d4,0x0d1,0x0cd,0x0ca,0x0c7,0x0c4,0x0c1,0x0be,0x0bb,0x0b8,0x0b5,0x0b2,0x0af,0x0ac,0x0a9,
		0x0a7,0x0a4,0x0a1,0x09f,0x09c,0x099,0x097,0x094,0x092,0x08f,0x08d,0x08a,0x088,0x086,0x083,0x081,
		0x07f,0x07d,0x07a,0x078,0x076,0x074,0x072,0x070,0x06e,0x06c,0x06a,0x068,0x066,0x064,0x062,0x060,
		0x05e,0x05c,0x05b,0x059,0x057,0x055,0x053,0x052,0x050,0x04e,0x04d,0x04b,0x04a,0x048,0x046,0x045,
		0x043,0x042,0x040,0x03f,0x03e,0x03c,0x03b,0x039,0x038,0x037,0x035,0x034,0x033,0x031,0x030,0x02f,
		0x02e,0x02d,0x02b,0x02a,0x029,0x028,0x027,0x026,0x025,0x024,0x023,0x022,0x021,0x020,0x01f,0x01e,
		0x01d,0x01c,0x01b,0x01a,0x019,0x018,0x017,0x017,0x016,0x015,0x014,0x014,0x013,0x012,0x011,0x011,
		0x010,0x00f,0x00f,0x00e,0x00d,0x00d,0x00c,0x00c,0x00b,0x00a,0x00a,0x009,0x009,0x008,0x008,0x007,
		0x007,0x007,0x006,0x006,0x005,0x005,0x005,0x004,0x004,0x004,0x003,0x003,0x003,0x002,0x002,0x002,
		0x002,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000
	};

	// if the top bit is set, we're in the second half of the curve
	// which is a mirror image, so invert the index
	if (bitfield(input, 8))
		input = ~input;

	// return the value from the table
	return s_sin_table[input & 0xff];
}


opn_registers_base::opn_registers_base() :
    m_lfo_counter(0),
    m_lfo_am(0)
{
    // create the waveforms
    for (uint32_t index = 0; index < WAVEFORM_LENGTH; index++)
        m_waveform[0][index] = abs_sin_attenuation(index) | (bitfield(index, 9) << 15);
}

//-------------------------------------------------
//  reset - reset to initial state
//-------------------------------------------------

void opn_registers_base::reset()
{
    std::fill_n(&m_regdata[0], REGISTERS, 0);
    // enable output on both channels by default
    m_regdata[0xb4]  = m_regdata[0xb5]  = m_regdata[0xb6]  = 0xc0;
    m_regdata[0x1b4] = m_regdata[0x1b5] = m_regdata[0x1b6] = 0xc0;
}


//-------------------------------------------------
//  operator_map - return an array of operator
//  indices for each channel; for OPN this is fixed
//-------------------------------------------------

void opn_registers_base::operator_map(operator_mapping &dest) const
{
    // Note that the channel index order is 0,2,1,3, so we bitswap the index.
    //
    // This is because the order in the map is:
    //    carrier 1, carrier 2, modulator 1, modulator 2
    //
    // But when wiring up the connections, the more natural order is:
    //    carrier 1, modulator 1, carrier 2, modulator 2
    static const operator_mapping s_fixed_map =
    { {
        operator_list(  0,  6,  3,  9 ),  // Channel 0 operators
        operator_list(  1,  7,  4, 10 ),  // Channel 1 operators
        operator_list(  2,  8,  5, 11 ),  // Channel 2 operators
        operator_list( 12, 18, 15, 21 ),  // Channel 3 operators
        operator_list( 13, 19, 16, 22 ),  // Channel 4 operators
        operator_list( 14, 20, 17, 23 ),  // Channel 5 operators
    } };
    dest = s_fixed_map;
}


//-------------------------------------------------
//  write - handle writes to the register array
//-------------------------------------------------

bool opn_registers_base::write(uint16_t index, uint8_t data, uint32_t &channel, uint32_t &opmask)
{
    assert(index < REGISTERS);

    // writes in the 0xa0-af/0x1a0-af region are handled as latched pairs
    // borrow unused registers 0xb8-bf/0x1b8-bf as temporary holding locations
    if ((index & 0xf0) == 0xa0)
    {
        if (bitfield(index, 0, 2) == 3)
            return false;

        uint32_t latchindex = 0xb8 | bitfield(index, 3);
        latchindex |= index & 0x100;

        // writes to the upper half just latch (only low 6 bits matter)
        if (bitfield(index, 2))
            m_regdata[latchindex] = data | 0x80;

        // writes to the lower half only commit if the latch is there
        else if (bitfield(m_regdata[latchindex], 7))
        {
            m_regdata[index] = data;
            m_regdata[index | 4] = m_regdata[latchindex] & 0x3f;
            m_regdata[latchindex] = 0;
        }
        return false;
    }
    else if ((index & 0xf8) == 0xb8)
    {
        // registers 0xb8-0xbf are used internally
        return false;
    }

    // everything else is normal
    m_regdata[index] = data;

    // handle writes to the key on index
    if (index == 0x28)
    {
        channel = bitfield(data, 0, 2);
        if (channel == 3)
            return false;
        channel += bitfield(data, 2, 1) * 3;
        opmask = bitfield(data, 4, 4);
        return true;
    }
    return false;
}


//-------------------------------------------------
//  clock_noise_and_lfo - clock the noise and LFO,
//  handling clock division, depth, and waveform
//  computations
//-------------------------------------------------

int32_t opn_registers_base::clock_noise_and_lfo()
{
    // OPN has no noise generation

    // this table is based on converting the frequencies in the applications
    // manual to clock dividers, based on the assumption of a 7-bit LFO value
    static uint8_t const lfo_max_count[8] = { 109, 78, 72, 68, 63, 45, 9, 6 };
    uint32_t subcount = uint8_t(m_lfo_counter++);

    // when we cross the divider count, add enough to zero it and cause an
    // increment at bit 8; the 7-bit value lives from bits 8-14
    if (subcount >= lfo_max_count[lfo_rate()])
    {
        // note: to match the published values this should be 0x100 - subcount;
        // however, tests on the hardware and nuked bear out an off-by-one
        // error exists that causes the max LFO rate to be faster than published
        m_lfo_counter += 0x101 - subcount;
    }

    // AM value is 7 bits, staring at bit 8; grab the low 6 directly
    m_lfo_am = bitfield(m_lfo_counter, 8, 6);

    // first half of the AM period (bit 6 == 0) is inverted
    if (bitfield(m_lfo_counter, 8+6) == 0)
        m_lfo_am ^= 0x3f;

    // PM value is 5 bits, starting at bit 10; grab the low 3 directly
    int32_t pm = bitfield(m_lfo_counter, 10, 3);

    // PM is reflected based on bit 3
    if (bitfield(m_lfo_counter, 10+3))
        pm ^= 7;

    // PM is negated based on bit 4
    return bitfield(m_lfo_counter, 10+4) ? -pm : pm;
}


//-------------------------------------------------
//  lfo_am_offset - return the AM offset from LFO
//  for the given channel
//-------------------------------------------------

uint32_t opn_registers_base::lfo_am_offset(uint32_t choffs) const
{
    // shift value for AM sensitivity is [7, 3, 1, 0],
    // mapping to values of [0, 1.4, 5.9, and 11.8dB]
    uint32_t am_shift = (1 << (ch_lfo_am_sens(choffs) ^ 3)) - 1;

    // QUESTION: max sensitivity should give 11.8dB range, but this value
    // is directly added to an x.8 attenuation value, which will only give
    // 126/256 or ~4.9dB range -- what am I missing? The calculation below
    // matches several other emulators, including the Nuked implemenation.

    // raw LFO AM value on OPN is 0-3F, scale that up by a factor of 2
    // (giving 7 bits) before applying the final shift
    return (m_lfo_am << 1) >> am_shift;
}

//-------------------------------------------------
//  detune_adjustment - given a 5-bit key code
//  value and a 3-bit detune parameter, return a
//  6-bit signed phase displacement; this table
//  has been verified against Nuked's equations,
//  but the equations are rather complicated, so
//  we'll keep the simplicity of the table
//-------------------------------------------------

inline int32_t detune_adjustment(uint32_t detune, uint32_t keycode)
{
	static uint8_t const s_detune_adjustment[32][4] =
	{
		{ 0,  0,  1,  2 },  { 0,  0,  1,  2 },  { 0,  0,  1,  2 },  { 0,  0,  1,  2 },
		{ 0,  1,  2,  2 },  { 0,  1,  2,  3 },  { 0,  1,  2,  3 },  { 0,  1,  2,  3 },
		{ 0,  1,  2,  4 },  { 0,  1,  3,  4 },  { 0,  1,  3,  4 },  { 0,  1,  3,  5 },
		{ 0,  2,  4,  5 },  { 0,  2,  4,  6 },  { 0,  2,  4,  6 },  { 0,  2,  5,  7 },
		{ 0,  2,  5,  8 },  { 0,  3,  6,  8 },  { 0,  3,  6,  9 },  { 0,  3,  7, 10 },
		{ 0,  4,  8, 11 },  { 0,  4,  8, 12 },  { 0,  4,  9, 13 },  { 0,  5, 10, 14 },
		{ 0,  5, 11, 16 },  { 0,  6, 12, 17 },  { 0,  6, 13, 19 },  { 0,  7, 14, 20 },
		{ 0,  8, 16, 22 },  { 0,  8, 16, 22 },  { 0,  8, 16, 22 },  { 0,  8, 16, 22 }
	};
	int32_t result = s_detune_adjustment[keycode][detune & 3];
	return bitfield(detune, 2) ? -result : result;
}



//-------------------------------------------------
//  cache_operator_data - fill the operator cache
//  with prefetched data
//-------------------------------------------------

void opn_registers_base::cache_operator_data(uint32_t choffs, uint32_t opoffs, opdata_cache &cache)
{
    // set up the easy stuff
    cache.waveform = &m_waveform[0][0];

    // get frequency from the channel
    uint32_t block_freq = cache.block_freq = ch_block_freq(choffs);

    // if multi-frequency mode is enabled and this is channel 2,
    // fetch one of the special frequencies
    if (multi_freq() && choffs == 2)
    {
        if (opoffs == 2)
            block_freq = cache.block_freq = multi_block_freq(1);
        else if (opoffs == 10)
            block_freq = cache.block_freq = multi_block_freq(2);
        else if (opoffs == 6)
            block_freq = cache.block_freq = multi_block_freq(0);
    }

    // compute the keycode: block_freq is:
    //
    //     BBBFFFFFFFFFFF
    //     ^^^^???
    //
    // the 5-bit keycode uses the top 4 bits plus a magic formula
    // for the final bit
    uint32_t keycode = bitfield(block_freq, 10, 4) << 1;

    // lowest bit is determined by a mix of next lower FNUM bits
    // according to this equation from the YM2608 manual:
    //
    //   (F11 & (F10 | F9 | F8)) | (!F11 & F10 & F9 & F8)
    //
    // for speed, we just look it up in a 16-bit constant
    keycode |= bitfield(0xfe80, bitfield(block_freq, 7, 4));

    // detune adjustment
    cache.detune = detune_adjustment(op_detune(opoffs), keycode);

    // multiple value, as an x.1 value (0 means 0.5)
    cache.multiple = op_multiple(opoffs) * 2;
    if (cache.multiple == 0)
        cache.multiple = 1;

    // phase step, or PHASE_STEP_DYNAMIC if PM is active; this depends on
    // block_freq, detune, and multiple, so compute it after we've done those
    cache.phase_step = opdata_cache::PHASE_STEP_DYNAMIC;

    // total level, scaled by 8
    cache.total_level = op_total_level(opoffs) << 3;

    // 4-bit sustain level, but 15 means 31 so effectively 5 bits
    cache.eg_sustain = op_sustain_level(opoffs);
    cache.eg_sustain |= (cache.eg_sustain + 1) & 0x10;
    cache.eg_sustain <<= 5;

    // determine KSR adjustment for enevlope rates
    uint32_t ksrval = keycode >> (op_ksr(opoffs) ^ 3);
    cache.eg_rate[EG_ATTACK ] = effective_rate(op_attack_rate(opoffs) * 2, ksrval);
    cache.eg_rate[EG_DECAY  ] = effective_rate(op_decay_rate(opoffs) * 2, ksrval);
    cache.eg_rate[EG_SUSTAIN] = effective_rate(op_sustain_rate(opoffs) * 2, ksrval);
    cache.eg_rate[EG_RELEASE] = effective_rate(op_release_rate(opoffs) * 4 + 2, ksrval);
}

//-------------------------------------------------
//  opn_lfo_pm_phase_adjustment - given the 7 most
//  significant frequency number bits, plus a 3-bit
//  PM depth value and a signed 5-bit raw PM value,
//  return a signed PM adjustment to the frequency;
//  algorithm written to match Nuked behavior
//-------------------------------------------------

inline int32_t opn_lfo_pm_phase_adjustment(uint32_t fnum_bits, uint32_t pm_sensitivity, int32_t lfo_raw_pm)
{
	// this table encodes 2 shift values to apply to the top 7 bits
	// of fnum; it is effectively a cheap multiply by a constant
	// value containing 0-2 bits
	static uint8_t const s_lfo_pm_shifts[8][8] =
	{
		{ 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77 },
		{ 0x77, 0x77, 0x77, 0x77, 0x72, 0x72, 0x72, 0x72 },
		{ 0x77, 0x77, 0x77, 0x72, 0x72, 0x72, 0x17, 0x17 },
		{ 0x77, 0x77, 0x72, 0x72, 0x17, 0x17, 0x12, 0x12 },
		{ 0x77, 0x77, 0x72, 0x17, 0x17, 0x17, 0x12, 0x07 },
		{ 0x77, 0x77, 0x17, 0x12, 0x07, 0x07, 0x02, 0x01 },
		{ 0x77, 0x77, 0x17, 0x12, 0x07, 0x07, 0x02, 0x01 },
		{ 0x77, 0x77, 0x17, 0x12, 0x07, 0x07, 0x02, 0x01 }
	};

	// look up the relevant shifts
	int32_t abs_pm = (lfo_raw_pm < 0) ? -lfo_raw_pm : lfo_raw_pm;
	uint32_t const shifts = s_lfo_pm_shifts[pm_sensitivity][bitfield(abs_pm, 0, 3)];

	// compute the adjustment
	int32_t adjust = (fnum_bits >> bitfield(shifts, 0, 4)) + (fnum_bits >> bitfield(shifts, 4, 4));
	if (pm_sensitivity > 5)
		adjust <<= pm_sensitivity - 5;
	adjust >>= 2;

	// every 16 cycles it inverts sign
	return (lfo_raw_pm < 0) ? -adjust : adjust;
}



//-------------------------------------------------
//  compute_phase_step - compute the phase step
//-------------------------------------------------

uint32_t opn_registers_base::compute_phase_step(uint32_t choffs, uint32_t opoffs, opdata_cache const &cache, int32_t lfo_raw_pm)
{
    // OPN phase calculation has only a single detune parameter
    // and uses FNUMs instead of keycodes

    // extract frequency number (low 11 bits of block_freq)
    uint32_t fnum = bitfield(cache.block_freq, 0, 11) << 1;

    // if there's a non-zero PM sensitivity, compute the adjustment
    uint32_t pm_sensitivity = ch_lfo_pm_sens(choffs);
    if (pm_sensitivity != 0)
    {
        // apply the phase adjustment based on the upper 7 bits
        // of FNUM and the PM depth parameters
        fnum += opn_lfo_pm_phase_adjustment(bitfield(cache.block_freq, 4, 7), pm_sensitivity, lfo_raw_pm);

        // keep fnum to 12 bits
        fnum &= 0xfff;
    }

    // apply block shift to compute phase step
    uint32_t block = bitfield(cache.block_freq, 11, 3);
    uint32_t phase_step = (fnum << block) >> 2;

    // apply detune based on the keycode
    phase_step += cache.detune;

    // clamp to 17 bits in case detune overflows
    // QUESTION: is this specific to the YM2612/3438?
    phase_step &= 0x1ffff;

    // apply frequency multiplier (which is cached as an x.1 value)
    return (phase_step * cache.multiple) >> 1;
}


class fm_engine;

// ======================> fm_operator

// fm_operator represents an FM operator (or "slot" in FM parlance), which
// produces an output sine wave modulated by an envelope
class fm_operator
{
	// "quiet" value, used to optimize when we can skip doing work
	static constexpr uint32_t EG_QUIET = 0x380;

public:
	// constructor
	fm_operator(fm_engine &owner, uint32_t opoffs);


	// reset the operator state
	void reset();

	// return the operator/channel offset
	uint32_t opoffs() const { return m_opoffs; }
	uint32_t choffs() const { return m_choffs; }

	// set the current channel
	void set_choffs(uint32_t choffs) { m_choffs = choffs; }

	// prepare prior to clocking
	bool prepare();

	// master clocking function
	void clock(uint32_t env_counter, int32_t lfo_raw_pm);

	// return the current phase value
	uint32_t phase() const { return m_phase >> 10; }

	// compute operator volume
	int32_t compute_volume(uint32_t phase, uint32_t am_offset) const;

	// key state control
	void keyonoff(bool on);

	// return a reference to our registers
	opn_registers_base &regs() const { return m_regs; }

private:
	void start_attack(bool is_restart = false);

	// clock phases
	void clock_ssg_eg_state();
	void clock_envelope(uint32_t env_counter);
	void clock_phase(int32_t lfo_raw_pm);

	// return effective attenuation of the envelope
	uint32_t envelope_attenuation(uint32_t am_offset) const;

	// internal state
	uint32_t m_choffs;                     // channel offset in registers
	uint32_t m_opoffs;                     // operator offset in registers
	uint32_t m_phase;                      // current phase value (10.10 format)
	uint16_t m_env_attenuation;            // computed envelope attenuation (4.6 format)
	envelope_state m_env_state;            // current envelope state
	uint8_t m_ssg_inverted;                // non-zero if the output should be inverted (bit 0)
	bool    m_key_state;                   // current key state: on or off (bit 0)
	bool    m_keyon_live;                  // live key on state (bit 0 = direct, bit 1 = rhythm, bit 2 = CSM)
	opdata_cache m_cache;                  // cached values for performance
	opn_registers_base &m_regs;                  // direct reference to registers
	fm_engine &m_owner; // reference to the owning engine
};

// struct containing an array of output values
template<int NumOutputs>
struct ymfm_output
{
    // clear all outputs to 0
    ymfm_output &clear()
    {
        for (uint32_t index = 0; index < NumOutputs; index++)
            data[index] = 0;
        return *this;
    }

    // clamp all outputs to a 16-bit signed value
    ymfm_output &clamp16()
    {
        for (uint32_t index = 0; index < NumOutputs; index++)
            data[index] = clamp(data[index], -32768, 32767);
        return *this;
    }

    // run each output value through the floating-point processor
    ymfm_output &roundtrip_fp()
    {
        for (uint32_t index = 0; index < NumOutputs; index++)
            data[index] = ymfm::roundtrip_fp(data[index]);
        return *this;
    }

    // internal state
    int32_t data[NumOutputs];
};


// fm_channel represents an FM channel which combines the output of 2 or 4
// operators into a final result
class fm_channel
{
    using output_data = ymfm_output<opn_registers_base::OUTPUTS>;

public:
    // constructor
    fm_channel(fm_engine &owner, uint32_t choffs);


    // reset the channel state
    void reset();

    // return the channel offset
    uint32_t choffs() const { return m_choffs; }

    // assign operators
    void assign(uint32_t index, fm_operator *op)
    {
        assert(index < array_size(m_op));
        m_op[index] = op;
        if (op != nullptr)
            op->set_choffs(m_choffs);
    }

    // signal key on/off to our operators
    void keyonoff(uint32_t states);

    // prepare prior to clocking
    bool prepare();

    // master clocking function
    void clock(uint32_t env_counter, int32_t lfo_raw_pm);

    // specific 4-operator output handlers
    void output_4op(output_data &output, uint32_t rshift, int32_t clipmax) const;

    // return a reference to our registers
    opn_registers_base &regs() const { return m_regs; }

private:
    // helper to add values to the outputs based on channel enables
    void add_to_output(uint32_t choffs, output_data &output, int32_t value) const
    {
        // create these constants to appease overzealous compilers checking array
        // bounds in unreachable code (looking at you, clang)
        constexpr int out0_index = 0;
        constexpr int out1_index = 1 % opn_registers_base::OUTPUTS;
        constexpr int out2_index = 2 % opn_registers_base::OUTPUTS;
        constexpr int out3_index = 3 % opn_registers_base::OUTPUTS;

        if (opn_registers_base::OUTPUTS == 1 || m_regs.ch_output_0(choffs))
            output.data[out0_index] += value;
        if (opn_registers_base::OUTPUTS >= 2 && m_regs.ch_output_1(choffs))
            output.data[out1_index] += value;
        if (opn_registers_base::OUTPUTS >= 3 && m_regs.ch_output_2(choffs))
            output.data[out2_index] += value;
        if (opn_registers_base::OUTPUTS >= 4 && m_regs.ch_output_3(choffs))
            output.data[out3_index] += value;
    }

    // internal state
    uint32_t m_choffs;                     // channel offset in registers
    int16_t m_feedback[2];                 // feedback memory for operator 1
    mutable int16_t m_feedback_in;         // next input value for op 1 feedback (set in output)
    fm_operator *m_op[4];    // up to 4 operators
    opn_registers_base &m_regs;                  // direct reference to registers
    fm_engine &m_owner; // reference to the owning engine
};






class fm_engine
{
public:
    fm_engine();

    // expose some constants from the registers
    static constexpr uint32_t OUTPUTS = opn_registers_base::OUTPUTS;
    static constexpr uint32_t CHANNELS = opn_registers_base::CHANNELS;
    static constexpr uint32_t ALL_CHANNELS = opn_registers_base::ALL_CHANNELS;
    static constexpr uint32_t OPERATORS = opn_registers_base::OPERATORS;

    // expose the correct output class
    using output_data = ymfm_output<OUTPUTS>;

    // reset the overall state
    void reset();

    // master clocking function
    uint32_t clock(uint32_t chanmask);

    // compute sum of channel outputs
    void output(output_data &output, uint32_t rshift, int32_t clipmax, uint32_t chanmask) const;

    // write to the OPN registers
    void write(uint16_t regnum, uint8_t data);

    // return the current clock prescale
    uint32_t clock_prescale() const { return m_clock_prescale; }

    // set prescale factor (2/3/6)
    void set_clock_prescale(uint32_t prescale) { m_clock_prescale = prescale; }

    // compute sample rate
    uint32_t sample_rate(uint32_t baseclock) const
    {
#if (YMFM_DEBUG_LOG_WAVFILES)
        for (uint32_t chnum = 0; chnum < CHANNELS; chnum++)
            m_wavfile[chnum].set_samplerate(baseclock / (m_clock_prescale * OPERATORS));
#endif
        return baseclock / (m_clock_prescale * OPERATORS);
    }

    // return a reference to our registers
    opn_registers_base &regs() { return m_regs; }

    // invalidate any caches
    void invalidate_caches() { m_modified_channels = opn_registers_base::ALL_CHANNELS; }

private:
    // assign the current set of operators to channels
    void assign_operators();

    // internal state
    uint32_t m_env_counter;          // envelope counter; low 2 bits are sub-counter
    uint8_t m_clock_prescale;        // prescale factor (2/3/6)
    uint8_t m_total_clocks;          // low 8 bits of the total number of clocks processed
    uint32_t m_active_channels;      // mask of active channels (computed by prepare)
    uint32_t m_modified_channels;    // mask of channels that have been modified
    uint32_t m_prepare_count;        // counter to do periodic prepare sweeps
    opn_registers_base m_regs;             // register accessor
    std::unique_ptr<fm_channel> m_channel[CHANNELS]; // channel pointers
    std::unique_ptr<fm_operator> m_operator[OPERATORS]; // operator pointers
};


class ym2612
{
public:
	static constexpr uint32_t OUTPUTS = fm_engine::OUTPUTS;
	using output_data = fm_engine::output_data;


	// reset
	void reset();


	// pass-through helpers
	uint32_t sample_rate(uint32_t input_clock) const { return m_fm.sample_rate(input_clock); }
	void invalidate_caches() { m_fm.invalidate_caches(); }

	// write access
	void write_address(uint8_t data);
	void write_data(uint8_t data);
	void write_address_hi(uint8_t data);
	void write_data_hi(uint8_t data);
	void write(uint32_t offset, uint8_t data);

	// generate one sample of sound
	void generate(output_data *output, uint32_t numsamples = 1);

protected:

	// internal state
	uint16_t m_address;              // address register
	uint16_t m_dac_data;             // 9-bit DAC data
	uint8_t m_dac_enable;            // DAC enabled?
	fm_engine m_fm;                  // core FM engine
};



/////////////////////////////


//-------------------------------------------------
//  attenuation_to_volume - given a 5.8 fixed point
//  logarithmic attenuation value, return a 13-bit
//  linear volume
//-------------------------------------------------

inline uint32_t attenuation_to_volume(uint32_t input)
{
	// the values here are 10-bit mantissas with an implied leading bit
	// this matches the internal format of the OPN chip, extracted from the die

	// as a nod to performance, the implicit 0x400 bit is pre-incorporated, and
	// the values are left-shifted by 2 so that a simple right shift is all that
	// is needed; also the order is reversed to save a NOT on the input
#define X(a) (((a) | 0x400) << 2)
	static uint16_t const s_power_table[256] =
	{
		X(0x3fa),X(0x3f5),X(0x3ef),X(0x3ea),X(0x3e4),X(0x3df),X(0x3da),X(0x3d4),
		X(0x3cf),X(0x3c9),X(0x3c4),X(0x3bf),X(0x3b9),X(0x3b4),X(0x3ae),X(0x3a9),
		X(0x3a4),X(0x39f),X(0x399),X(0x394),X(0x38f),X(0x38a),X(0x384),X(0x37f),
		X(0x37a),X(0x375),X(0x370),X(0x36a),X(0x365),X(0x360),X(0x35b),X(0x356),
		X(0x351),X(0x34c),X(0x347),X(0x342),X(0x33d),X(0x338),X(0x333),X(0x32e),
		X(0x329),X(0x324),X(0x31f),X(0x31a),X(0x315),X(0x310),X(0x30b),X(0x306),
		X(0x302),X(0x2fd),X(0x2f8),X(0x2f3),X(0x2ee),X(0x2e9),X(0x2e5),X(0x2e0),
		X(0x2db),X(0x2d6),X(0x2d2),X(0x2cd),X(0x2c8),X(0x2c4),X(0x2bf),X(0x2ba),
		X(0x2b5),X(0x2b1),X(0x2ac),X(0x2a8),X(0x2a3),X(0x29e),X(0x29a),X(0x295),
		X(0x291),X(0x28c),X(0x288),X(0x283),X(0x27f),X(0x27a),X(0x276),X(0x271),
		X(0x26d),X(0x268),X(0x264),X(0x25f),X(0x25b),X(0x257),X(0x252),X(0x24e),
		X(0x249),X(0x245),X(0x241),X(0x23c),X(0x238),X(0x234),X(0x230),X(0x22b),
		X(0x227),X(0x223),X(0x21e),X(0x21a),X(0x216),X(0x212),X(0x20e),X(0x209),
		X(0x205),X(0x201),X(0x1fd),X(0x1f9),X(0x1f5),X(0x1f0),X(0x1ec),X(0x1e8),
		X(0x1e4),X(0x1e0),X(0x1dc),X(0x1d8),X(0x1d4),X(0x1d0),X(0x1cc),X(0x1c8),
		X(0x1c4),X(0x1c0),X(0x1bc),X(0x1b8),X(0x1b4),X(0x1b0),X(0x1ac),X(0x1a8),
		X(0x1a4),X(0x1a0),X(0x19c),X(0x199),X(0x195),X(0x191),X(0x18d),X(0x189),
		X(0x185),X(0x181),X(0x17e),X(0x17a),X(0x176),X(0x172),X(0x16f),X(0x16b),
		X(0x167),X(0x163),X(0x160),X(0x15c),X(0x158),X(0x154),X(0x151),X(0x14d),
		X(0x149),X(0x146),X(0x142),X(0x13e),X(0x13b),X(0x137),X(0x134),X(0x130),
		X(0x12c),X(0x129),X(0x125),X(0x122),X(0x11e),X(0x11b),X(0x117),X(0x114),
		X(0x110),X(0x10c),X(0x109),X(0x106),X(0x102),X(0x0ff),X(0x0fb),X(0x0f8),
		X(0x0f4),X(0x0f1),X(0x0ed),X(0x0ea),X(0x0e7),X(0x0e3),X(0x0e0),X(0x0dc),
		X(0x0d9),X(0x0d6),X(0x0d2),X(0x0cf),X(0x0cc),X(0x0c8),X(0x0c5),X(0x0c2),
		X(0x0be),X(0x0bb),X(0x0b8),X(0x0b5),X(0x0b1),X(0x0ae),X(0x0ab),X(0x0a8),
		X(0x0a4),X(0x0a1),X(0x09e),X(0x09b),X(0x098),X(0x094),X(0x091),X(0x08e),
		X(0x08b),X(0x088),X(0x085),X(0x082),X(0x07e),X(0x07b),X(0x078),X(0x075),
		X(0x072),X(0x06f),X(0x06c),X(0x069),X(0x066),X(0x063),X(0x060),X(0x05d),
		X(0x05a),X(0x057),X(0x054),X(0x051),X(0x04e),X(0x04b),X(0x048),X(0x045),
		X(0x042),X(0x03f),X(0x03c),X(0x039),X(0x036),X(0x033),X(0x030),X(0x02d),
		X(0x02a),X(0x028),X(0x025),X(0x022),X(0x01f),X(0x01c),X(0x019),X(0x016),
		X(0x014),X(0x011),X(0x00e),X(0x00b),X(0x008),X(0x006),X(0x003),X(0x000)
	};
#undef X

	// look up the fractional part, then shift by the whole
	return s_power_table[input & 0xff] >> (input >> 8);
}


//-------------------------------------------------
//  attenuation_increment - given a 6-bit ADSR
//  rate value and a 3-bit stepping index,
//  return a 4-bit increment to the attenutaion
//  for this step (or for the attack case, the
//  fractional scale factor to decrease by)
//-------------------------------------------------

inline uint32_t attenuation_increment(uint32_t rate, uint32_t index)
{
	static uint32_t const s_increment_table[64] =
	{
		0x00000000, 0x00000000, 0x10101010, 0x10101010,  // 0-3    (0x00-0x03)
		0x10101010, 0x10101010, 0x11101110, 0x11101110,  // 4-7    (0x04-0x07)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 8-11   (0x08-0x0B)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 12-15  (0x0C-0x0F)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 16-19  (0x10-0x13)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 20-23  (0x14-0x17)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 24-27  (0x18-0x1B)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 28-31  (0x1C-0x1F)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 32-35  (0x20-0x23)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 36-39  (0x24-0x27)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 40-43  (0x28-0x2B)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 44-47  (0x2C-0x2F)
		0x11111111, 0x21112111, 0x21212121, 0x22212221,  // 48-51  (0x30-0x33)
		0x22222222, 0x42224222, 0x42424242, 0x44424442,  // 52-55  (0x34-0x37)
		0x44444444, 0x84448444, 0x84848484, 0x88848884,  // 56-59  (0x38-0x3B)
		0x88888888, 0x88888888, 0x88888888, 0x88888888   // 60-63  (0x3C-0x3F)
	};
	return bitfield(s_increment_table[rate], 4*index, 4);
}




//*********************************************************
//  FM OPERATOR
//*********************************************************

//-------------------------------------------------
//  fm_operator - constructor
//-------------------------------------------------

fm_operator::fm_operator(fm_engine &owner, uint32_t opoffs) :
	m_choffs(0),
	m_opoffs(opoffs),
	m_phase(0),
	m_env_attenuation(0x3ff),
	m_env_state(EG_RELEASE),
	m_ssg_inverted(false),
	m_key_state(false),
	m_keyon_live(false),
	m_regs(owner.regs()),
	m_owner(owner)
{
}


//-------------------------------------------------
//  reset - reset the channel state
//-------------------------------------------------

void fm_operator::reset()
{
	// reset our data
	m_phase = 0;
	m_env_attenuation = 0x3ff;
	m_env_state = EG_RELEASE;
	m_ssg_inverted = 0;
	m_key_state = false;
	m_keyon_live = false;
}


//-------------------------------------------------
//  prepare - prepare for clocking
//-------------------------------------------------

bool fm_operator::prepare()
{
	// cache the data
	m_regs.cache_operator_data(m_choffs, m_opoffs, m_cache);

	// clock the key state
	if (m_keyon_live != m_key_state) {
		m_key_state = m_keyon_live;
		if (m_key_state) start_attack();
		else {
            m_env_state = EG_RELEASE;
        }
	}

	// we're active until we're quiet after the release
    return (m_env_state != EG_RELEASE || m_env_attenuation < EG_QUIET);
}


//-------------------------------------------------
//  clock - master clocking function
//-------------------------------------------------

void fm_operator::clock(uint32_t env_counter, int32_t lfo_raw_pm)
{
	// clock the SSG-EG state (OPN/OPNA)
	if (m_regs.op_ssg_eg_enable(m_opoffs))
		clock_ssg_eg_state();
	else
		m_ssg_inverted = false;

	// clock the envelope if on an envelope cycle; env_counter is a x.2 value
	if (bitfield(env_counter, 0, 2) == 0)
		clock_envelope(env_counter >> 2);

	// clock the phase
	clock_phase(lfo_raw_pm);
}


//-------------------------------------------------
//  compute_volume - compute the 14-bit signed
//  volume of this operator, given a phase
//  modulation and an AM LFO offset
//-------------------------------------------------

int32_t fm_operator::compute_volume(uint32_t phase, uint32_t am_offset) const
{
	// the low 10 bits of phase represents a full 2*PI period over
	// the full sin wave

	// early out if the envelope is effectively off
	if (m_env_attenuation > EG_QUIET)
		return 0;

	// get the absolute value of the sin, as attenuation, as a 4.8 fixed point value
	uint32_t sin_attenuation = m_cache.waveform[phase & (opn_registers_base::WAVEFORM_LENGTH - 1)];

	// get the attenuation from the evelope generator as a 4.6 value, shifted up to 4.8
	uint32_t env_attenuation = envelope_attenuation(am_offset) << 2;

	// combine into a 5.8 value, then convert from attenuation to 13-bit linear volume
	int32_t result = attenuation_to_volume((sin_attenuation & 0x7fff) + env_attenuation);

	// negate if in the negative part of the sin wave (sign bit gives 14 bits)
	return bitfield(sin_attenuation, 15) ? -result : result;
}



//-------------------------------------------------
//  keyonoff - signal a key on/off event
//-------------------------------------------------

void fm_operator::keyonoff(bool on)
{
	m_keyon_live = on;
}


//-------------------------------------------------
//  start_attack - start the attack phase; called
//  when a keyon happens or when an SSG-EG cycle
//  is complete and restarts
//-------------------------------------------------

void fm_operator::start_attack(bool is_restart)
{
	// don't change anything if already in attack state
	if (m_env_state == EG_ATTACK)
		return;
	m_env_state = EG_ATTACK;

	// generally not inverted at start, except if SSG-EG is enabled and
	// one of the inverted modes is specified; leave this alone on a
	// restart, as it is managed by the clock_ssg_eg_state() code
	if (opn_registers_base::EG_HAS_SSG && !is_restart)
		m_ssg_inverted = m_regs.op_ssg_eg_enable(m_opoffs) & bitfield(m_regs.op_ssg_eg_mode(m_opoffs), 2);

	// reset the phase when we start an attack due to a key on
	// (but not when due to an SSG-EG restart except in certain cases
	// managed directly by the SSG-EG code)
	if (!is_restart)
		m_phase = 0;

	// if the attack rate >= 62 then immediately go to max attenuation
	if (m_cache.eg_rate[EG_ATTACK] >= 62)
		m_env_attenuation = 0;
}


//-------------------------------------------------
//  clock_ssg_eg_state - clock the SSG-EG state;
//  should only be called if SSG-EG is enabled
//-------------------------------------------------

void fm_operator::clock_ssg_eg_state()
{
	// work only happens once the attenuation crosses above 0x200
	if (!bitfield(m_env_attenuation, 9))
		return;

	// 8 SSG-EG modes:
	//    000: repeat normally
	//    001: run once, hold low
	//    010: repeat, alternating between inverted/non-inverted
	//    011: run once, hold high
	//    100: inverted repeat normally
	//    101: inverted run once, hold low
	//    110: inverted repeat, alternating between inverted/non-inverted
	//    111: inverted run once, hold high
	uint32_t mode = m_regs.op_ssg_eg_mode(m_opoffs);

	// hold modes (1/3/5/7)
	if (bitfield(mode, 0))
	{
		// set the inverted flag to the end state (0 for modes 1/7, 1 for modes 3/5)
		m_ssg_inverted = bitfield(mode, 2) ^ bitfield(mode, 1);

		// if holding, force the attenuation to the expected value once we're
		// past the attack phase
		if (m_env_state != EG_ATTACK)
			m_env_attenuation = m_ssg_inverted ? 0x200 : 0x3ff;
	}

	// continuous modes (0/2/4/6)
	else
	{
		// toggle invert in alternating mode (even in attack state)
		m_ssg_inverted ^= bitfield(mode, 1);

		// restart attack if in decay/sustain states
		if (m_env_state == EG_DECAY || m_env_state == EG_SUSTAIN)
			start_attack(true);

		// phase is reset to 0 in modes 0/4
		if (bitfield(mode, 1) == 0)
			m_phase = 0;
	}

	// in all modes, once we hit release state, attenuation is forced to maximum
	if (m_env_state == EG_RELEASE)
		m_env_attenuation = 0x3ff;
}


//-------------------------------------------------
//  clock_envelope - clock the envelope state
//  according to the given count
//-------------------------------------------------

void fm_operator::clock_envelope(uint32_t env_counter)
{
//    if (m_choffs != 2) {
//        m_env_attenuation += 1;
//        return;
//    }

    // handle attack->decay transitions
	if (m_env_state == EG_ATTACK && m_env_attenuation == 0)
		m_env_state = EG_DECAY;

	// handle decay->sustain transitions; it is important to do this immediately
	// after the attack->decay transition above in the event that the sustain level
	// is set to 0 (in which case we will skip right to sustain without doing any
	// decay); as an example where this can be heard, check the cymbals sound
	// in channel 0 of shinobi's test mode sound #5
	if (m_env_state == EG_DECAY && m_env_attenuation >= m_cache.eg_sustain)
		m_env_state = EG_SUSTAIN;

	// fetch the appropriate 6-bit rate value from the cache
	uint32_t rate = m_cache.eg_rate[m_env_state];

	// compute the rate shift value; this is the shift needed to
	// apply to the env_counter such that it becomes a 5.11 fixed
	// point number
	uint32_t rate_shift = rate >> 2;
	env_counter <<= rate_shift;

	// see if the fractional part is 0; if not, it's not time to clock
	if (bitfield(env_counter, 0, 11) != 0)
		return;

	// determine the increment based on the non-fractional part of env_counter
	uint32_t relevant_bits = bitfield(env_counter, (rate_shift <= 11) ? 11 : rate_shift, 3);
	uint32_t increment = attenuation_increment(rate, relevant_bits);

//    if (m_opoffs == 10) {
//        printf("%d %d %8x %8x\n", m_env_state, increment, (~m_env_attenuation * increment) >> 4, m_env_attenuation );
//    }

	// attack is the only one that increases
	if (m_env_state == EG_ATTACK)
    {
        // glitch means that attack rates of 62/63 don't increment if
		// changed after the initial key on (where they are handled
		// specially); nukeykt confirms this happens on OPM, OPN, OPL/OPLL
		// at least so assuming it is true for everyone
		if (rate < 62)
			m_env_attenuation += (~m_env_attenuation * increment) >> 4;

    }

	// all other cases are similar
	else
	{
		// non-SSG-EG cases just apply the increment
		if (!m_regs.op_ssg_eg_enable(m_opoffs))
			m_env_attenuation += increment;

		// SSG-EG only applies if less than mid-point, and then at 4x
		else if (m_env_attenuation < 0x200)
			m_env_attenuation += 4 * increment;

		// clamp the final attenuation
		if (m_env_attenuation >= 0x400)
			m_env_attenuation = 0x3ff;
	}
}


//-------------------------------------------------
//  clock_phase - clock the 10.10 phase value; the
//  OPN version of the logic has been verified
//  against the Nuked phase generator
//-------------------------------------------------

void fm_operator::clock_phase(int32_t lfo_raw_pm)
{
	// read from the cache, or recalculate if PM active
	uint32_t phase_step = m_cache.phase_step;
	if (phase_step == opdata_cache::PHASE_STEP_DYNAMIC)
		phase_step = m_regs.compute_phase_step(m_choffs, m_opoffs, m_cache, lfo_raw_pm);

	// finally apply the step to the current phase value
	m_phase += phase_step;
}


//-------------------------------------------------
//  envelope_attenuation - return the effective
//  attenuation of the envelope
//-------------------------------------------------

uint32_t fm_operator::envelope_attenuation(uint32_t am_offset) const
{
	uint32_t result = m_env_attenuation;

	// invert if necessary due to SSG-EG
	if (opn_registers_base::EG_HAS_SSG && m_ssg_inverted)
		result = (0x200 - result) & 0x3ff;

	// add in LFO AM modulation
	if (m_regs.op_lfo_am_enable(m_opoffs))
		result += am_offset;

	// add in total level and KSL from the cache
	result += m_cache.total_level;

	// clamp to max, apply shift, and return
	return std::min<uint32_t>(result, 0x3ff);
}



//*********************************************************
//  FM CHANNEL
//*********************************************************

//-------------------------------------------------
//  fm_channel - constructor
//-------------------------------------------------

fm_channel::fm_channel(fm_engine &owner, uint32_t choffs) :
	m_choffs(choffs),
	m_feedback{ 0, 0 },
	m_feedback_in(0),
	m_op{ nullptr, nullptr, nullptr, nullptr },
	m_regs(owner.regs()),
	m_owner(owner)
{
}


//-------------------------------------------------
//  reset - reset the channel state
//-------------------------------------------------

void fm_channel::reset()
{
	// reset our data
	m_feedback[0] = m_feedback[1] = 0;
	m_feedback_in = 0;
}



//-------------------------------------------------
//  keyonoff - signal key on/off to our operators
//-------------------------------------------------

void fm_channel::keyonoff(uint32_t states)
{
	for (uint32_t opnum = 0; opnum < array_size(m_op); opnum++)
		if (m_op[opnum] != nullptr)
			m_op[opnum]->keyonoff(bitfield(states, opnum));

}


//-------------------------------------------------
//  prepare - prepare for clocking
//-------------------------------------------------

bool fm_channel::prepare()
{
	uint32_t active_mask = 0;

	// prepare all operators and determine if they are active
	for (uint32_t opnum = 0; opnum < array_size(m_op); opnum++)
		if (m_op[opnum] != nullptr)
			if (m_op[opnum]->prepare())
				active_mask |= 1 << opnum;

	return (active_mask != 0);
}


//-------------------------------------------------
//  clock - master clock of all operators
//-------------------------------------------------

void fm_channel::clock(uint32_t env_counter, int32_t lfo_raw_pm)
{
	// clock the feedback through
	m_feedback[0] = m_feedback[1];
	m_feedback[1] = m_feedback_in;

	for (uint32_t opnum = 0; opnum < array_size(m_op); opnum++)
		if (m_op[opnum] != nullptr)
			m_op[opnum]->clock(env_counter, lfo_raw_pm);

}



//-------------------------------------------------
//  output_4op - combine 4 operators according to
//  the specified algorithm, returning a sum
//  according to the rshift and clipmax parameters,
//  which vary between different implementations
//-------------------------------------------------

void fm_channel::output_4op(output_data &output, uint32_t rshift, int32_t clipmax) const
{
	// all 4 operators should be populated
	assert(m_op[0] != nullptr);
	assert(m_op[1] != nullptr);
	assert(m_op[2] != nullptr);
	assert(m_op[3] != nullptr);

	// AM amount is the same across all operators; compute it once
	uint32_t am_offset = m_regs.lfo_am_offset(m_choffs);

	// operator 1 has optional self-feedback
	int32_t opmod = 0;
	uint32_t feedback = m_regs.ch_feedback(m_choffs);
	if (feedback != 0)
		opmod = (m_feedback[0] + m_feedback[1]) >> (10 - feedback);

	// compute the 14-bit volume/value of operator 1 and update the feedback
	int32_t op1value = m_feedback_in = m_op[0]->compute_volume(m_op[0]->phase() + opmod, am_offset);

	// now that the feedback has been computed, skip the rest if all volumes
	// are clear; no need to do all this work for nothing
	if (m_regs.ch_output_any(m_choffs) == 0)
		return;

	// OPM/OPN offer 8 different connection algorithms for 4 operators,
	// and OPL3 offers 4 more, which we designate here as 8-11.
	//
	// The operators are computed in order, with the inputs pulled from
	// an array of values (opout) that is populated as we go:
	//    0 = 0
	//    1 = O1
	//    2 = O2
	//    3 = O3
	//    4 = (O4)
	//    5 = O1+O2
	//    6 = O1+O3
	//    7 = O2+O3
	//
	// The s_algorithm_ops table describes the inputs and outputs of each
	// algorithm as follows:
	//
	//      ---------x use opout[x] as operator 2 input
	//      ------xxx- use opout[x] as operator 3 input
	//      ---xxx---- use opout[x] as operator 4 input
	//      --x------- include opout[1] in final sum
	//      -x-------- include opout[2] in final sum
	//      x--------- include opout[3] in final sum
	#define ALGORITHM(op2in, op3in, op4in, op1out, op2out, op3out) \
		((op2in) | ((op3in) << 1) | ((op4in) << 4) | ((op1out) << 7) | ((op2out) << 8) | ((op3out) << 9))
	static uint16_t const s_algorithm_ops[8+4] =
	{
		ALGORITHM(1,2,3, 0,0,0),    //  0: O1 -> O2 -> O3 -> O4 -> out (O4)
		ALGORITHM(0,5,3, 0,0,0),    //  1: (O1 + O2) -> O3 -> O4 -> out (O4)
		ALGORITHM(0,2,6, 0,0,0),    //  2: (O1 + (O2 -> O3)) -> O4 -> out (O4)
		ALGORITHM(1,0,7, 0,0,0),    //  3: ((O1 -> O2) + O3) -> O4 -> out (O4)
		ALGORITHM(1,0,3, 0,1,0),    //  4: ((O1 -> O2) + (O3 -> O4)) -> out (O2+O4)
		ALGORITHM(1,1,1, 0,1,1),    //  5: ((O1 -> O2) + (O1 -> O3) + (O1 -> O4)) -> out (O2+O3+O4)
		ALGORITHM(1,0,0, 0,1,1),    //  6: ((O1 -> O2) + O3 + O4) -> out (O2+O3+O4)
		ALGORITHM(0,0,0, 1,1,1),    //  7: (O1 + O2 + O3 + O4) -> out (O1+O2+O3+O4)
		ALGORITHM(1,2,3, 0,0,0),    //  8: O1 -> O2 -> O3 -> O4 -> out (O4)         [same as 0]
		ALGORITHM(0,2,3, 1,0,0),    //  9: (O1 + (O2 -> O3 -> O4)) -> out (O1+O4)   [unique]
		ALGORITHM(1,0,3, 0,1,0),    // 10: ((O1 -> O2) + (O3 -> O4)) -> out (O2+O4) [same as 4]
		ALGORITHM(0,2,0, 1,0,1)     // 11: (O1 + (O2 -> O3) + O4) -> out (O1+O3+O4) [unique]
	};
	uint32_t algorithm_ops = s_algorithm_ops[m_regs.ch_algorithm(m_choffs)];

	// populate the opout table
	int16_t opout[8];
	opout[0] = 0;
	opout[1] = op1value;

	// compute the 14-bit volume/value of operator 2
	opmod = opout[bitfield(algorithm_ops, 0, 1)] >> 1;
	opout[2] = m_op[1]->compute_volume(m_op[1]->phase() + opmod, am_offset);
	opout[5] = opout[1] + opout[2];

	// compute the 14-bit volume/value of operator 3
	opmod = opout[bitfield(algorithm_ops, 1, 3)] >> 1;
	opout[3] = m_op[2]->compute_volume(m_op[2]->phase() + opmod, am_offset);
	opout[6] = opout[1] + opout[3];
	opout[7] = opout[2] + opout[3];

	// compute the 14-bit volume/value of operator 4; all algorithms consume OP4 output at a minimum
    opmod = opout[bitfield(algorithm_ops, 4, 3)] >> 1;
	int32_t result = m_op[3]->compute_volume(m_op[3]->phase() + opmod, am_offset);
	result >>= rshift;

	// optionally add OP1, OP2, OP3
	int32_t clipmin = -clipmax - 1;
	if (bitfield(algorithm_ops, 7) != 0)
		result = clamp(result + (opout[1] >> rshift), clipmin, clipmax);
	if (bitfield(algorithm_ops, 8) != 0)
		result = clamp(result + (opout[2] >> rshift), clipmin, clipmax);
	if (bitfield(algorithm_ops, 9) != 0)
		result = clamp(result + (opout[3] >> rshift), clipmin, clipmax);

	// add to the output
	add_to_output(m_choffs, output, result);
}


//*********************************************************
//  FM ENGINE
//*********************************************************

fm_engine::fm_engine() :
	m_env_counter(0),
	m_clock_prescale(opn_registers_base::DEFAULT_PRESCALE),
	m_total_clocks(0),
	m_active_channels(ALL_CHANNELS),
	m_modified_channels(ALL_CHANNELS),
	m_prepare_count(0)
{
	// create the channels
	for (uint32_t chnum = 0; chnum < CHANNELS; chnum++)
        m_channel[chnum] = std::make_unique<fm_channel>(*this, opn_registers_base::channel_offset(chnum));

	// create the operators
	for (uint32_t opnum = 0; opnum < OPERATORS; opnum++)
        m_operator[opnum] = std::make_unique<fm_operator>(*this, opn_registers_base::operator_offset(opnum));

#if (YMFM_DEBUG_LOG_WAVFILES)
	for (uint32_t chnum = 0; chnum < CHANNELS; chnum++)
		m_wavfile[chnum].set_index(chnum);
#endif

	// do the initial operator assignment
	assign_operators();
}


//-------------------------------------------------
//  reset - reset the overall state
//-------------------------------------------------

void fm_engine::reset()
{
	// register type-specific initialization
	m_regs.reset();

	// explicitly write to the mode register since it has side-effects
	// QUESTION: old cores initialize this to 0x30 -- who is right?
	write(opn_registers_base::REG_MODE, 0);

	// reset the channels
	for (auto &chan : m_channel)
		chan->reset();

	// reset the operators
	for (auto &op : m_operator)
		op->reset();
}



//-------------------------------------------------
//  clock - iterate over all channels, clocking
//  them forward one step
//-------------------------------------------------

uint32_t fm_engine::clock(uint32_t chanmask)
{
	// update the clock counter
	m_total_clocks++;

	// if something was modified, prepare
	// also prepare every 4k samples to catch ending notes
	if (m_modified_channels != 0 || m_prepare_count++ >= 4096)
	{
		// call each channel to prepare
		m_active_channels = 0;
		for (uint32_t chnum = 0; chnum < CHANNELS; chnum++)
			if (bitfield(chanmask, chnum))
				if (m_channel[chnum]->prepare())
					m_active_channels |= 1 << chnum;

		// reset the modified channels and prepare count
		m_modified_channels = m_prepare_count = 0;
	}

	// if the envelope clock divider is 1, just increment by 4;
	// otherwise, increment by 1 and manually wrap when we reach the divide count
    if (bitfield(++m_env_counter, 0, 2) == opn_registers_base::EG_CLOCK_DIVIDER)
		m_env_counter += 4 - opn_registers_base::EG_CLOCK_DIVIDER;

	// clock the noise generator
	int32_t lfo_raw_pm = m_regs.clock_noise_and_lfo();

	// now update the state of all the channels and operators
	for (uint32_t chnum = 0; chnum < CHANNELS; chnum++)
		if (bitfield(chanmask, chnum))
			m_channel[chnum]->clock(m_env_counter, lfo_raw_pm);

	// return the envelope counter as it is used to clock ADPCM-A
	return m_env_counter;
}


//-------------------------------------------------
//  output - compute a sum over the relevant
//  channels
//-------------------------------------------------

void fm_engine::output(output_data &output, uint32_t rshift, int32_t clipmax, uint32_t chanmask) const
{
    // mask out inactive channels
    if (!YMFM_DEBUG_LOG_WAVFILES)
        chanmask &= m_active_channels;

    // sum over all the desired channels
    for (uint32_t chnum = 0; chnum < CHANNELS; chnum++)
        if (bitfield(chanmask, chnum))
        {
            m_channel[chnum]->output_4op(output, rshift, clipmax);
        }
}


//-------------------------------------------------
//  write - handle writes to the OPN registers
//-------------------------------------------------

void fm_engine::write(uint16_t regnum, uint8_t data)
{
    // special case: writes to the mode register can impact IRQs;
	// schedule these writes to ensure ordering with timers
	if (regnum == opn_registers_base::REG_MODE)
	{
		return;
	}

	// for now just mark all channels as modified
	m_modified_channels = ALL_CHANNELS;

	// most writes are passive, consumed only when needed
	uint32_t keyon_channel;
	uint32_t keyon_opmask;
	if (m_regs.write(regnum, data, keyon_channel, keyon_opmask))
	{
		// handle writes to the keyon register(s)
		if (keyon_channel < CHANNELS)
		{
			// normal channel on/off
            m_channel[keyon_channel]->keyonoff(keyon_opmask);
		}
    }
}



//-------------------------------------------------
//  assign_operators - get the current mapping of
//  operators to channels and assign them all
//-------------------------------------------------

void fm_engine::assign_operators()
{
	typename opn_registers_base::operator_mapping map;
	m_regs.operator_map(map);

	for (uint32_t chnum = 0; chnum < CHANNELS; chnum++)
		for (uint32_t index = 0; index < 4; index++)
		{
			uint32_t opnum = bitfield(map.chan[chnum], 8 * index, 8);
			m_channel[chnum]->assign(index, (opnum == 0xff) ? nullptr : m_operator[opnum].get());
		}
}



//-------------------------------------------------
//  reset - reset the system
//-------------------------------------------------

void ym2612::reset()
{
	// reset the engines
	m_fm.reset();
}




//-------------------------------------------------
//  write_address - handle a write to the address
//  register
//-------------------------------------------------

void ym2612::write_address(uint8_t data)
{
	// just set the address
	m_address = data;
}


//-------------------------------------------------
//  write_data - handle a write to the data
//  register
//-------------------------------------------------

void ym2612::write_data(uint8_t data)
{
	// ignore if paired with upper address
	if (bitfield(m_address, 8))
		return;

	if (m_address == 0x2a)
	{
		// 2A: DAC data (most significant 8 bits)
		m_dac_data = (m_dac_data & ~0x1fe) | ((data ^ 0x80) << 1);
	}
	else if (m_address == 0x2b)
	{
		// 2B: DAC enable (bit 7)
		m_dac_enable = bitfield(data, 7);
	}
	else if (m_address == 0x2c)
	{
		// 2C: test/low DAC bit
		m_dac_data = (m_dac_data & ~1) | bitfield(data, 3);
	}
	else
	{
		// 00-29, 2D-FF: write to FM
		m_fm.write(m_address, data);
	}

}


//-------------------------------------------------
//  write_address_hi - handle a write to the upper
//  address register
//-------------------------------------------------

void ym2612::write_address_hi(uint8_t data)
{
	// just set the address
	m_address = 0x100 | data;
}


//-------------------------------------------------
//  write_data_hi - handle a write to the upper
//  data register
//-------------------------------------------------

void ym2612::write_data_hi(uint8_t data)
{
	// ignore if paired with upper address
	if (!bitfield(m_address, 8))
		return;

	// 100-1FF: write to FM
	m_fm.write(m_address, data);

}


//-------------------------------------------------
//  write - handle a write to the register
//  interface
//-------------------------------------------------

void ym2612::write(uint32_t offset, uint8_t data)
{
	switch (offset & 3)
	{
		case 0: // address port
			write_address(data);
			break;

		case 1: // data port
			write_data(data);
			break;

		case 2: // upper address port
			write_address_hi(data);
			break;

		case 3: // upper data port
			write_data_hi(data);
			break;
	}
}


//-------------------------------------------------
//  generate - generate one sample of sound
//-------------------------------------------------

void ym2612::generate(output_data *output, uint32_t numsamples)
{
	for (uint32_t samp = 0; samp < numsamples; samp++, output++)
	{
		// clock the system
		m_fm.clock(fm_engine::ALL_CHANNELS);

		// sum individual channels to apply DAC discontinuity on each
		output->clear();
		output_data temp;

		// first do FM-only channels; OPN2 is 9-bit with intermediate clipping
		int const last_fm_channel = m_dac_enable ? 5 : 6;
		for (int chan = 0; chan < last_fm_channel; chan++)
		{
			m_fm.output(temp.clear(), 5, 256, 1 << chan);
            output->data[0] += temp.data[0];
            output->data[1] += temp.data[1];
		}

		// add in DAC
		if (m_dac_enable)
		{
			// DAC enabled: start with DAC value then add the first 5 channels only
            int32_t dacval = int16_t(m_dac_data << 7) >> 7;
            output->data[0] += m_fm.regs().ch_output_0(0x102) ? dacval : 0;
            output->data[1] += m_fm.regs().ch_output_1(0x102) ? dacval : 0;
		}

		// output is technically multiplexed rather than mixed, but that requires
		// a better sound mixer than we usually have, so just average over the six
		// channels; also apply a 64/65 factor to account for the discontinuity
		// adjustment above
        output->data[0] = (output->data[0] * 128) * 64 / (6 * 65);
        output->data[1] = (output->data[1] * 128) * 64 / (6 * 65);
	}
}



} // namespace

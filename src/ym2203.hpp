#pragma once
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <algorithm>


class YM2203 {
public:
    enum { MIXRATE = 44100 };

    void set_clock(uint32_t clock) { m_cps = clock * (1.0f / MIXRATE); }

    void write_reg(uint8_t a, uint8_t v) {
        m_reg[a] = v;
        if (a < 0x6) m_ssg_chans[a >> 1].period = m_reg[a & ~1] | ((m_reg[a | 1] & 0xf) << 8);
        if (a == 0x6) m_noise_period = v & 0x1f;
        if (a == 0x7) {
            for (int i = 0; i < 3; ++i) {
                m_ssg_chans[i].tone_en  = !((v >> i) & 1);
                m_ssg_chans[i].noise_en = !((v >> (i + 3)) & 1);
            }
        }
        if (a >= 0x8 && a < 0xb) {
            constexpr float N = 140.0f;
            m_ssg_chans[a - 0x8].volume = (std::pow(N, (v & 0xf) * (1.0f / 15.0f)) - 1.0f) * (0.5f / (N - 1.0f));
        }
        if (a == 0x27) m_ch3_special = (v & 0xc0) != 0;
        if (a == 0x28 && (v & 3) != 3) {
            int op_mask = v >> 4;
            FmChan& chan = m_fm_chans[v & 3];
            for (int o = 0; o < 4; ++o) {
                int m = 1 << o;
                if ((op_mask & m) == (chan.op_mask & m)) continue;
                if (op_mask & m) {
                    chan.ops[o].state = Op::ATTACK;
                    chan.ops[o].level = 0.0f;
                    chan.ops[o].phase = 0.0f;
                }
                else chan.ops[o].state = Op::RELEASE;
            }
            chan.op_mask = op_mask;
        }
        int c = a & 3;
        if (c == 3) return; // only 3 channels
        FmChan& chan = m_fm_chans[c];
        if (a >= 0xa0 && a < 0xa8) chan.freq = m_reg[0xa0 + c] | ((m_reg[0xa4 + c] & 0x3f) << 8);
        if (a >= 0xa8 && a < 0xb0) m_ch3_freq[(c + 2) % 3] = m_reg[0xa8 + c] | ((m_reg[0xac + c] & 0x3f) << 8);
        if (a >= 0xb0 && a < 0xb3) {
            chan.connect  = 1 << (v & 0x7);
            int fbl = (v >> 3) & 0x7;
            chan.fb_scale = ((1 << fbl) & ~1) * (1.0f / 280.0f);
        }
        // operator parameters
        if (a >= 0x30 && a < 0x90) {
            static constexpr int k_to_o[] = { 0, 2, 1, 3 };
            Op& op = chan.ops[k_to_o[(a >> 2) & 0x3]];
            if (a < 0x40) {
                int mul = v & 0xf;
                int det = (v >> 4) & 0x7;
                op.pitch_mul = (mul * 2 | (mul == 0)) + (det & 3) * (det & 4 ? -1 : 1) * 0.003f;
            }
            if (a >= 0x40 && a < 0x50) op.vol = std::exp2f((v & 0x7f) * -0.125f);
            if (a >= 0x50 && a < 0x60) {
                op.ks                = (v >> 6) ^ 3;
                op.rates[Op::ATTACK] = (v & 0x1f) * 2;
            }
            if (a >= 0x60 && a < 0x70) op.rates[Op::DECAY]   = (v & 0x1f) * 2;
            if (a >= 0x70 && a < 0x80) op.rates[Op::SUSTAIN] = (v & 0x1f) * 2;
            if (a >= 0x80 && a < 0x90) {
                op.rates[Op::RELEASE] = (v & 0x0f) * 4 + 2;
                int sus = (v >> 4) & 0xf;
                op.sus_level = std::pow(0.707f, sus < 15 ? sus : 31);
            }
            if (a >= 0x90 && (v & 8)) printf("warning: SSG EG not supported (%02x:%02x)\n", a, v);
        }
    }

    void render(float out[2]) {
        if (m_cps == 0.0f) return;

        static constexpr float PAN_SSG[] = {
            0.4f * std::sqrt(0.3f),
            0.4f * std::sqrt(0.5f),
            0.4f * std::sqrt(0.7f),
        };
        static constexpr float PAN_FM[] = {
            0.5f * std::sqrt(0.6f),
            0.5f * std::sqrt(0.5f),
            0.5f * std::sqrt(0.4f),
        };

        // ssg
        if (m_noise_period > 0) {
            m_noise_count = std::fmod(m_noise_count + m_cps / 32.0f, m_noise_period);
            if (m_noise_count < m_cps / 32.0f) {
                m_noise_state ^= ((m_noise_state & 1) ^ ((m_noise_state >> 3) & 1)) << 17;
                m_noise_state >>= 1;
            }
        }
        for (int c = 0; c < 3; ++c) {
            SsgChan& chan = m_ssg_chans[c];
            if (chan.period > 0) chan.count = std::fmod(chan.count + m_cps / 32.0f, chan.period);
            int   tone  = chan.tone_en  & (chan.count * 2 >= chan.period);
            int   noise = chan.noise_en & !(m_noise_state & 1);
            float x     = (chan.tone_en | chan.noise_en) ? (tone | noise ? 1.0f : -1.0f) : 0.0f;
            out[0] += x * chan.volume * PAN_SSG[c];
            out[1] += x * chan.volume * PAN_SSG[2 - c];
        }

        // fm
        for (int c = 0; c < 3; ++c) {
            FmChan& chan = m_fm_chans[c];
            for (int o = 0; o < 4; ++o) {
                Op& op = chan.ops[o];

                // phase
                int freq  = (c == 2 && m_ch3_special && o < 3) ? m_ch3_freq[o] : chan.freq;
                int pitch = ((freq & 0x7ff) << (freq >> 11));
                op.phase += pitch * op.pitch_mul * m_cps * (1.0f / 0x12000000);
                op.phase -= int(op.phase);

                // envelope
                int rate    = op.rates[op.state];
                int keycode = ((freq >> 9) & 0x1e) | ((0xfe80 >> ((freq >> 7) & 0xf)) & 1);
                rate = std::min(rate + (keycode >> op.ks), 63);
                if (rate > 0) {
                    // float f = std::exp2f(rate * 0.25f);
                    int f = ((4 | (rate & 3)) << (rate >> 2)) >> 2;
                    if (op.state == Op::ATTACK) {
                        op.level += f * (1.0f / 16.06f / MIXRATE);
                        if (op.level >= 1.0f) { op.level = 1.0f; op.state = Op::DECAY; }
                    }
                    else {
                        // op.level *= std::exp2f(f * (-0.07f / MIXRATE)); // fitted empirically
                        op.level *= std::exp2f(f * (-0.07f / MIXRATE)); // fitted empirically
                    }
                }
                if (op.state == Op::DECAY && op.level <= op.sus_level) op.state = Op::SUSTAIN;
            }
            // algorithm
            float fb = chan.feedback * chan.fb_scale;
            float o = chan.feedback = chan.ops[0].sample(fb);
            float a[4] = {};
            if (chan.connect & 0b01111001) a[0] = o;
            if (chan.connect & 0b00100010) a[1] = o;
            if (chan.connect & 0b00100100) a[2] = o;
            if (chan.connect & 0b10000000) a[3] = o;
            o = chan.ops[1].sample(a[0]);
            if (chan.connect & 0b00000111) a[1] += o;
            if (chan.connect & 0b00001000) a[2] += o;
            if (chan.connect & 0b11110000) a[3] += o;
            o = chan.ops[2].sample(a[1]);
            if (chan.connect & 0b00011111) a[2] += o;
            if (chan.connect & 0b11100000) a[3] += o;
            a[3] += chan.ops[3].sample(a[2]);
            out[0] += a[3] * PAN_FM[c];
            out[1] += a[3] * PAN_FM[2 - c];
        }
    }
private:
    struct SsgChan {
        float volume   = 0.0f;
        float count    = 0.0f;
        int   period   = 0;
        bool  tone_en  = false;
        bool  noise_en = false;
    };
    struct Op {
        enum State { ATTACK, DECAY, SUSTAIN, RELEASE };
        float phase     = 0.0f;
        float level     = 0.0f;
        float pitch_mul = 1.0f; // precomputed from DT/MUL (reg 0x30)
        float vol       = 1.0f; // precomputed from TL (reg 0x40)
        float sus_level = 1.0f; // precomputed from SL (reg 0x80 high nibble)
        int   rates[4]  = { 0, 0, 0, 2 }; // pre-decoded AR/DR/SR/RR (regs 0x50-0x80)
        int   ks        = 3;    // key-scale shift, pre-decoded from reg 0x50 high bits
        State state     = RELEASE;
        float sample(float shift) const {
            return std::sin((phase + shift * 4.0f) * (2.0f * float(M_PI))) * level * vol;
        }
    };
    struct FmChan {
        int   op_mask  = 0;
        float feedback = 0.0f;
        int   freq     = 0;
        int   connect  = 1; // precomputed from algo (reg 0xb0 low bits)
        float fb_scale = 0.0f; // precomputed from reg 0xb0 high bits
        Op    ops[4]   = {};
    };
    float    m_cps          = 0.0f; // cycles per sample
    uint8_t  m_reg[256]     = {};
    int      m_noise_period = 0;
    float    m_noise_count  = 0;
    uint32_t m_noise_state  = 1;
    SsgChan  m_ssg_chans[3] = {};
    FmChan   m_fm_chans[3]  = {};
    int      m_ch3_freq[3]  = {}; // per-op freqs for ch2 3-op special mode
    bool     m_ch3_special  = false;
};

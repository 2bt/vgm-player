#pragma once

#include <cstdint>
#include <cstdio>
#include <cmath>


class YM2203 {
public:
    enum { MIXRATE = 44100 };

    void set_clock(uint32_t clock) { m_cps = clock * (1.0f / MIXRATE); }

    void write_reg(uint8_t a, uint8_t v) {
        constexpr float N = 140.0f;
        m_reg[a] = v;
        switch (a) {
        case 0x8:
        case 0x9:
        case 0xa:
            m_ssg_chans[a - 8].volume = (std::pow(N, (v & 0xf) * (1.0f / 15.0f)) - 1.0f) * (0.5f / (N - 1.0f));
            break;
        case 0x28:
            if ((v & 3) == 3) break;
            key_onoff(v & 3, v >> 4);
            break;
        default: break;
        }
        if (a >= 0x90 && a < 0xa0) {
            if (v & 8) {
                printf("warning: SSG EG not not supported (%02x:%02x)\n", a, v);
            }
        }
    }

    void render(float out[2]) {
        constexpr float PAN_SSG[] = {
            0.4f * std::sqrt(0.3f),
            0.4f * std::sqrt(0.5f),
            0.4f * std::sqrt(0.7f),
        };
        constexpr float PAN_FM[] = {
            0.5f * std::sqrt(0.6f),
            0.5f * std::sqrt(0.5f),
            0.5f * std::sqrt(0.4f),
        };

        // ssg
        m_noise_count += m_cps / 32.0f;
        int noise_period = m_reg[6] & 0x1f;
        if (m_noise_count >= noise_period) {
            m_noise_count -= noise_period;
            m_noise_state ^= ((m_noise_state & 1) ^ ((m_noise_state >> 3) & 1)) << 17;
            m_noise_state >>= 1;
        }
        for (int c = 0; c < 3; ++c) {
            SsgChan& chan = m_ssg_chans[c];
            chan.count += m_cps / 32.0f;
            int period = m_reg[c * 2] | ((m_reg[c * 2 + 1] & 0xf) << 8);
            if (chan.count >= period) {
                chan.count -= period;
            }
            // tone
            uint8_t ctrl = m_reg[7] >> c;
            float   amp  = 0.0f;
            if (!(ctrl & 1)) {
                amp = chan.count * 2 < period ? -1.0f : 1.0f;
            }
            // noise
            if (!(ctrl & 8)) {
                amp = m_noise_state & 1 ? -1.0f : 1.0f;
            }
            amp *= chan.volume;
            out[0] += amp * PAN_SSG[c];
            out[1] += amp * PAN_SSG[2 - c];
        }

        // fm
        for (int c = 0; c < 3; ++c) {
            FmChan& chan = m_fm_chans[c];
            for (int o = 0; o < 4; ++o) {
                Op&     op = chan.ops[o];
                uint8_t oo = c + OP_OFFSET[o];
                // calculate pitch & keycode
                uint8_t freq_addr = 0xa0 + c;
                if (c == 2 && (m_reg[0x27] & 0xc0) && o < 3) {
                    freq_addr = 0xa8 + (o + 1) % 3;
                }
                uint32_t freq    = m_reg[freq_addr] | ((m_reg[freq_addr + 4] & 0x3f) << 8);
                float    pitch   = ((freq & 0x7ff) << (freq >> 11)) * m_cps * (1.0f / 0x12000000);
                uint8_t  keycode = ((freq >> 9) & 0x1e) | ((0xfe80 >> ((freq >> 7) & 3)) & 1);
                // multiple
                uint8_t multiple = m_reg[0x30 + oo] & 0xf;
                multiple         = multiple * 2 | (multiple == 0);
                // TODO: detune
                op.phase += pitch * multiple;
                op.phase -= int(op.phase);

                // envelope
                int adsr[4] = {
                    m_reg[0x50 + oo] & 0x1f,
                    m_reg[0x60 + oo] & 0x1f,
                    m_reg[0x70 + oo] & 0x1f,
                    (m_reg[0x80 + oo] & 0x0f) * 2 + 1,
                };
                int rate = adsr[op.state] * 2;
                uint8_t scaling = keycode >> ((m_reg[0x50] >> 6) ^ 3);
                if (rate > 0) std::min<int>(rate + scaling, 63);
                uint32_t f = rate <= 1 ? 0 : ((4 | (rate & 3)) << (rate >> 2)) >> 2; // magic
                if (op.state == Op::ATTACK) {
                    if (f > 0) op.level += f * (1.0f / 16.06f / MIXRATE);
                    if (op.level >= 1.0f) {
                        op.level = 1.0f;
                        op.state = Op::DECAY;
                    }
                }
                else {
                    if (f > 0) op.level *= std::pow(0.9524f, f * (1.0f / MIXRATE));
                    if (op.state == Op::DECAY) {
                        uint8_t sustain = m_reg[0x80 + oo] >> 4;
                        if (sustain == 15) sustain = 31;
                        float sus_level = std::pow(0.707f, sustain);
                        if (op.level <= sus_level) op.state = Op::SUSTAIN;
                    }
                }
            }
            // algorithm
            uint8_t connect  = 1 << (m_reg[0xb0 + c] & 0x7);
            uint8_t feedback = (m_reg[0xb0 + c] >> 3) & 0x7;
            float fb = 0.0f;
            // XXX: is the feedback strength correct?
            if (feedback) fb = chan.feedback * (1 << feedback) * (1.0f / 280.0f);
            float o = chan.feedback = op_amp(c, 0, fb);
            float a[4] = {};
            if (connect & 0b01111001) a[0] = o;
            if (connect & 0b00100010) a[1] = o;
            if (connect & 0b00100100) a[2] = o;
            if (connect & 0b10000000) a[3] = o;
            o = op_amp(c, 1, a[0]);
            if (connect & 0b00000111) a[1] += o;
            if (connect & 0b00001000) a[2] += o;
            if (connect & 0b11110000) a[3] += o;
            o = op_amp(c, 2, a[1]);
            if (connect & 0b00011111) a[2] += o;
            if (connect & 0b11100000) a[3] += o;
            a[3] += op_amp(c, 3, a[2]);
            out[0] += a[3] * PAN_FM[c];
            out[1] += a[3] * PAN_FM[2 - c];
        }
    }
private:
    static constexpr uint8_t OP_OFFSET[4] = { 0, 8, 4, 12 };

    float op_amp(uint8_t c, uint8_t o, float shift) const {
        Op const& op = m_fm_chans[c].ops[o];
        uint8_t total = m_reg[0x40 + c + OP_OFFSET[o]] & 0x7f;
        float   vol   = std::exp2f(total * -0.125f);
        return std::sin((op.phase + shift * 4.0f) * 2.0f * M_PI) * op.level * vol;
    }

    void key_onoff(uint8_t c, uint8_t op_mask) {
        FmChan& chan = m_fm_chans[c];
        for (int o = 0; o < 4; ++o) {
            uint8_t m = 1 << o;
            if ((op_mask & m) == (chan.op_mask & m)) continue;
            if (op_mask & m) {
                chan.ops[o].state = Op::ATTACK;
                chan.ops[o].level = 0.0f;
                chan.ops[o].phase = 0.0f;
            }
            else {
                chan.ops[o].state = Op::RELEASE;
            }
        }
        chan.op_mask = op_mask;
    }

    struct SsgChan {
        float volume;
        float count;
    };
    struct Op {
        enum State { ATTACK, DECAY, SUSTAIN, RELEASE };
        float phase;
        float level;
        State state = RELEASE;
    };
    struct FmChan {
        uint8_t op_mask;
        float   pitch;
        float   feedback;
        Op      ops[4];
    };
    float    m_cps          = 0.0f; // cycles per sample
    uint8_t  m_reg[256]     = {};
    float    m_noise_count  = 0;
    uint32_t m_noise_state  = 1;
    SsgChan  m_ssg_chans[3] = {};
    FmChan   m_fm_chans[3]  = {};
};


#pragma once
#include <cstdint>
#include <algorithm>

class LR35902 {
public:
    double sample_rate(uint32_t clock) { return clock / 4; }
    void write_reg(uint8_t a, uint8_t v) {
        if (a == 20) {
            m_vol[0] = ((v >> 0) & 0x7) + 1;
            m_vol[1] = ((v >> 4) & 0x7) + 1;
        }
        if (a == 21) {
            for (int i = 0; i < 4; ++i) {
                m_chans[i].pan[0] = (v >> (i + 0)) & 0x1;
                m_chans[i].pan[1] = (v >> (i + 4)) & 0x1;
            }
        }
        if (a >= 32 && a < 48) m_wave_ram[a - 32] = v;
        if (a >= 20) return;
        int i = a / 5;
        Channel& chan = m_chans[i];
        switch (a) {
        case 0:
            // TODO
            break;
        case 1: case 6:
            m_duty[i] = v >> 6;
            chan.length  = v & 0x3f;
            break;
        case 11:
            chan.length = v;
            break;
        case 16:
            chan.length = v & 0x3f;
            break;
        case 12:
            m_wave_vol = (v >> 5) & 0x3;
            break;
        case 2: case 7: case 17:
            chan.vol_init = v >> 4;
            chan.vol_dir  = (v >> 3) & 1 ?: -1;
            chan.vol_pace = v & 0x7;
            break;
        case 3: case 8: case 13:
            m_freq[i] = (m_freq[i] & 0xff00) | v;
            break;
        case 18:
            m_noise_div   = v & 0x7;
            m_noise_shift = (v >> 4) & 0xf;
            m_noise_width = (v >> 3) & 1;
            break;
        case 4: case 9: case 14:
            m_freq[i] = (m_freq[i] & 0x00ff) | ((v & 0x7) << 8);
            if (v & 0x80) {
                chan.vol      = chan.vol_init;
                chan.env_timer = chan.vol_pace;
                m_freq_timer[i]  = m_freq[i];
                if (i == 2) m_wave_pos = 0;
            }
            break;
        case 19:
            if (v & 0x80) {
                chan.vol       = chan.vol_init;
                chan.env_timer = chan.vol_pace;
                m_noise_timer  = 0;
                m_noise_lfsr   = 0x7fff;
            }
            break;
        default: break;
        }
    }

    void generate(int* out) {
        // update pulse phases
        for (int i = 0; i < 2; ++i) {
            if (++m_freq_timer[i] >= 0x800) {
                m_freq_timer[i] = m_freq[i];
                m_duty_step[i]  = (m_duty_step[i] + 1) & 0x7;
            }
        }

        // update wave phase (clocks at 2x pulse rate)
        m_freq_timer[2] += 2;
        if (m_freq_timer[2] >= 0x800) {
            m_freq_timer[2] = m_freq[2];
            m_wave_pos = (m_wave_pos + 1) & 0x1f;
        }

        // update noise LFSR
        int noise_period = m_noise_div ? m_noise_div << (m_noise_shift + 1) : 1 << m_noise_shift;
        if (++m_noise_timer >= noise_period) {
            m_noise_timer = 0;
            int xb = (m_noise_lfsr ^ (m_noise_lfsr >> 1)) & 1;
            m_noise_lfsr = (m_noise_lfsr >> 1) | (xb << 14);
            if (m_noise_width) m_noise_lfsr = (m_noise_lfsr & ~(1 << 6)) | (xb << 6);
        }

        if ((m_cycle & 0xfff) == 0) {
            // TODO update length
        }
        if ((m_cycle & 0x1fff) == 0x1000) {
            // TODO update sweep
        }
        if ((m_cycle & 0x3fff) == 0x3800) {
            // update volume
            for (int i = 0; i < 4; ++i) {
                Channel& chan = m_chans[i];
                if (chan.vol_pace == 0) continue;
                if (--chan.env_timer <= 0) {
                    chan.env_timer = chan.vol_pace ?: 8;
                    chan.vol       = std::clamp(chan.vol + chan.vol_dir, 0, 15);
                }
            }
        }

        out[0] = 0;
        out[1] = 0;

        // pulse
        for (int i = 0; i < 2; ++i) {
            static constexpr uint8_t PULSE[4] = { 0b00000001, 0b10000001, 0b10000111, 0b01111110 };
            Channel& chan = m_chans[i];
            int ch = (PULSE[m_duty[i]] >> m_duty_step[i]) & 1;
            ch = (ch * 2 - 1) * chan.vol;
            out[0] += ch * chan.pan[0];
            out[1] += ch * chan.pan[1];
        }

        // wave
        {
            static constexpr int SCALE[] = {0, 4, 2, 1};
            Channel& chan = m_chans[2];
            int nibble = (m_wave_ram[m_wave_pos >> 1] >> (m_wave_pos & 1 ? 0 : 4)) & 0xf;
            int ch = (nibble - 8) * SCALE[m_wave_vol];
            out[0] += ch * chan.pan[0];
            out[1] += ch * chan.pan[1];
        }

        // noise
        {
            Channel& chan = m_chans[3];
            int ch = ((~m_noise_lfsr & 1) * 2 - 1) * chan.vol;
            out[0] += ch * chan.pan[0];
            out[1] += ch * chan.pan[1];
        }

        out[0] *= m_vol[0] * 20;
        out[1] *= m_vol[1] * 20;

        ++m_cycle;
    }
private:

    struct Channel {
        int vol_init  = 0;
        int vol_dir   = 0;
        int vol_pace  = 0;
        int vol       = 0;
        int env_timer = 0;
        int length    = 0;
        int pan[2]    = {};
    };

    // pulse channels
    int m_duty[2]       = {};
    int m_duty_step[2]  = {};

    // pulse + wave channels
    int m_freq[3]       = {};
    int m_freq_timer[3] = {};

    // wave channel
    uint8_t m_wave_ram[16] = {};
    int     m_wave_pos     = 0;
    int     m_wave_vol     = 0;

    // noise channel
    int m_noise_div   = 0;
    int m_noise_shift = 0;
    int m_noise_width = 0;
    int m_noise_lfsr  = 0;
    int m_noise_timer = 0;
    
    int     m_cycle    = 0;
    int     m_vol[2]   = {8, 8};
    Channel m_chans[4] = {};
};

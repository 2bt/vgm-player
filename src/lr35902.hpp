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
            // TODO: sweep
            break;
        case 1: case 6:
            m_pulse_duty[i] = v >> 6;
            // fallthrough
        case 16:
            chan.length = v & 0x3f;
            chan.length_counter = 64 - chan.length;
            break;
        case 11:
            chan.length = v;
            chan.length_counter = 256 - v;
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
        case 4: case 9: case 14:
            m_freq[i] = (m_freq[i] & 0x00ff) | ((v & 0x7) << 8);
            chan.length_enable = (v >> 6) & 1;
            if (v & 0x80) {
                chan.vol        = chan.vol_init;
                chan.env_timer  = chan.vol_pace;
                chan.active     = true;
                m_freq_timer[i] = m_freq[i];
                m_phase[i]      = 0;
                if (chan.length_counter == 0) chan.length_counter = (i == 2) ? 256 : 64;
            }
            break;
        case 18:
            m_noise_div   = v & 0x7;
            m_noise_shift = (v >> 4) & 0xf;
            m_noise_width = (v >> 3) & 1;
            break;
        case 19:
            chan.length_enable = (v >> 6) & 1;
            if (v & 0x80) {
                chan.vol       = chan.vol_init;
                chan.env_timer = chan.vol_pace;
                chan.active    = true;
                m_noise_timer  = 0;
                m_noise_lfsr   = 0x7fff;
                if (chan.length_counter == 0) chan.length_counter = 64;
            }
            break;
        default: break;
        }
    }

    void generate(int* out) {
        // update pulse/wave phases
        for (int i = 0; i < 3; ++i) {
            m_freq_timer[i] += (i == 2) ? 2 : 1; // wave channel runs at twice the speed
            if (m_freq_timer[i] >= 0x800) {
                m_freq_timer[i] = m_freq[i];
                ++m_phase[i];
            }
        }

        // update noise LFSR
        int noise_period = m_noise_div ? m_noise_div << (m_noise_shift + 1) : 1 << m_noise_shift;
        if (++m_noise_timer >= noise_period) {
            m_noise_timer = 0;
            int xb = (m_noise_lfsr ^ (m_noise_lfsr >> 1)) & 1;
            m_noise_lfsr = (m_noise_lfsr >> 1) | (xb << 14);
            if (m_noise_width) m_noise_lfsr = (m_noise_lfsr & ~0x40) | (xb << 6);
        }

        // update length counter
        if ((m_cycle & 0xfff) == 0x000) {
            for (Channel& chan : m_chans) {
                if (chan.length_enable && chan.length_counter > 0)
                    if (--chan.length_counter == 0) chan.active = false;
            }
        }
        if ((m_cycle & 0x1fff) == 0x1000) {
            // TODO update sweep
        }
        if ((m_cycle & 0x3fff) == 0x3800) {
            // update volume
            for (Channel& chan : m_chans) {
                if (chan.vol_pace == 0) continue;
                if (--chan.env_timer <= 0) {
                    chan.env_timer = chan.vol_pace ?: 8;
                    chan.vol       = std::clamp(chan.vol + chan.vol_dir, 0, 15);
                }
            }
        }

        out[0] = out[1] = 0;

        // pulse
        for (int i = 0; i < 2; ++i) {
            static constexpr uint8_t PULSE[] = {0b00000001, 0b10000001, 0b10000111, 0b01111110};
            Channel& chan = m_chans[i];
            if (!chan.active) continue;
            int x = (PULSE[m_pulse_duty[i]] >> (m_phase[i] & 7)) & 1;
            x = (x * 8 - 4) * chan.vol;
            out[0] += x * chan.pan[0];
            out[1] += x * chan.pan[1];
        }

        // wave
        Channel& wave = m_chans[2];
        if (wave.active && m_wave_vol > 0) {
            int nibble = (m_wave_ram[(m_phase[2] >> 1) & 15] >> (m_phase[2] & 1 ? 0 : 4)) & 0xf;
            int x = (nibble * 2 - 15) << (3 - m_wave_vol);
            out[0] += x * wave.pan[0];
            out[1] += x * wave.pan[1];
        }

        // noise
        Channel& noise = m_chans[3];
        if (noise.active) {
            int x = ((~m_noise_lfsr & 1) * 8 - 4) * noise.vol;
            out[0] += x * noise.pan[0];
            out[1] += x * noise.pan[1];
        }

        out[0] *= m_vol[0] * 4;
        out[1] *= m_vol[1] * 4;
        ++m_cycle;
    }
private:

    struct Channel {
        int  vol_init       = 0;
        int  vol_dir        = 0;
        int  vol_pace       = 0;
        int  vol            = 0;
        int  env_timer      = 0;
        int  length         = 0;
        int  length_counter = 0;
        bool length_enable  = false;
        bool active         = false;
        int  pan[2]         = {};
    };

    // pulse + wave channels
    int     m_pulse_duty[2] = {};
    int     m_freq[3]       = {};
    int     m_freq_timer[3] = {};
    int     m_phase[3]      = {};
    uint8_t m_wave_ram[16]  = {};
    int     m_wave_vol      = 0;
    // noise channel
    int     m_noise_div   = 0;
    int     m_noise_shift = 0;
    int     m_noise_width = 0;
    int     m_noise_lfsr  = 0;
    int     m_noise_timer = 0;
    // global
    int     m_cycle    = 0;
    int     m_vol[2]   = {8, 8};
    Channel m_chans[4] = {};
};

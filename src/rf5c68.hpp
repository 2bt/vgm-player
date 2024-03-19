#pragma once

#include <array>
#include <cstdint>


class RF5C68 {
public:
    void write_mem(uint16_t addr, uint8_t data) {
        m_data[addr] = data;
    }
    void write_reg(uint8_t addr, uint8_t data) {
        Channel& chan = m_channels[m_cbank];
        switch (addr) {
        case 0x00:
            chan.vol = data;
            break;
        case 0x01:
            chan.pan = data;
            break;
        case 0x02:
            chan.step = (chan.step & 0xff00) | data;
            break;
        case 0x03:
            chan.step = (chan.step & 0x00ff) | (data << 8);
            break;
        case 0x04:
            chan.loopst = (chan.loopst & 0xff00) | data;
            break;
        case 0x05:
            chan.loopst = (chan.loopst & 0x00ff) | (data << 8);
            break;
        case 0x06:
            chan.start = data;
            if (!chan.enabled) chan.addr = chan.start << (8 + 11);
            break;
        case 0x07:
            m_enable = (data >> 7) & 1;
            if (data & 0x40) m_cbank = data & 7;
            else             m_wbank = data & 15;
            break;
        case 0x08:
            for (size_t i = 0; i < m_channels.size(); i++) {
                m_channels[i].enabled = (~data >> i) & 1;
                if (!m_channels[i].enabled) m_channels[i].addr = m_channels[i].start << (8 + 11);
            }
            break;
        }
    }
    void generate(int* out) {
        out[0] = 0;
        out[1] = 0;
        if (!m_enable) return;
        for (Channel& chan : m_channels) {
            if (!chan.enabled) continue;

            int sample = m_data[(chan.addr >> 11) & 0xffff];
            if (sample == 0xff) {
                chan.addr = chan.loopst << 11;
                sample = m_data[(chan.addr >> 11) & 0xffff];
                if (sample == 0xff) break;
            }
            chan.addr += chan.step;

            int lv = (chan.pan & 0xf) * chan.vol;
            int rv = (chan.pan >>  4) * chan.vol;
            if (sample & 0x80) {
                sample &= 0x7f;
                out[0] += (sample * lv) >> 5;
                out[1] += (sample * rv) >> 5;
            }
            else {
                out[0] -= (sample * lv) >> 5;
                out[1] -= (sample * rv) >> 5;
            }
        }
    }

private:
    struct Channel {
        uint8_t  enabled;
        uint8_t  vol;
        uint8_t  pan;
        uint8_t  start;
        uint32_t addr;
        uint16_t step;
        uint16_t loopst;
    };
    std::array<Channel, 8>       m_channels = {};
    uint8_t                      m_cbank    = 0;
    uint8_t                      m_wbank    = 0;
    bool                         m_enable   = false;
    std::array<uint8_t, 1 << 16> m_data     = {};
};



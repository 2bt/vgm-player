#pragma once

#include <array>
#include <cstdint>


class GA20 {
public:
    void write_mem(uint32_t addr, uint8_t data) {
        if (addr < m_data.size()) m_data[addr] = data;
    }
    void write_reg(uint8_t addr, uint8_t data) {
        Channel& chan = m_channels[(addr >> 3) & 3];
        switch (addr & 7) {
        case 0:
            chan.start = (chan.start & 0xff000) | (data << 4);
            break;
        case 1:
            chan.start = (chan.start & 0x00ff0) | (data << 12);
            break;
        case 4:
            chan.rate = data;
            break;
        case 5:
            chan.volume = (data * 256) / (data + 10);
            break;
        case 6:
            chan.enabled = true;
            chan.pos = chan.start;
            chan.counter = 0x100;
            break;
        default: break;
        }
    }
    void generate(int* out) {
        int acc = 0;
        for (Channel& chan : m_channels) {
            if (!chan.enabled) continue;
            uint8_t s = m_data[chan.pos & (m_data.size() - 1)];
            if (s == 0) {
                chan.enabled = false;
                continue;
            }
            acc += (s - 0x80) * chan.volume;
            if (--chan.counter <= chan.rate) {
                ++chan.pos;
                chan.counter = 0x100;
            }
        }
        out[0] = acc / 4;
        out[1] = acc / 4;
    }

private:
    struct Channel {
        uint32_t pos;
        uint32_t start;
        uint16_t counter;
        uint8_t  rate;
        uint8_t  volume;
        bool     enabled;
    };
    std::array<Channel, 4>       m_channels = {};
    std::array<uint8_t, 1 << 20> m_data     = {};
};



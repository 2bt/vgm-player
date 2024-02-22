#include "ymfm_opn.h"

#include <SDL.h>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <zlib.h>



class RF5C68 {
public:
    enum {
        NUM_CHANNELS = 8,
    };

    void write_mem(uint16_t addr, uint8_t data) {
        m_data[addr] = data;
    }
    void write_reg(uint8_t addr, uint8_t data) {
        Channel& chan = m_chan[m_cbank];
        switch (addr) {
        case 0x00: // envelope
            chan.env = data;
            break;
        case 0x01: // pan
            chan.pan = data;
            break;
        case 0x02: // FDL
            chan.step = (chan.step & 0xff00) | data;
            break;
        case 0x03: // FDH
            chan.step = (chan.step & 0x00ff) | (data << 8);
            break;
        case 0x04: // LSL
            chan.loopst = (chan.loopst & 0xff00) | data;
            break;
        case 0x05: // LSH
            chan.loopst = (chan.loopst & 0x00ff) | (data << 8);
            break;
        case 0x06: // ST
            chan.start = data;
            if (!chan.enable) chan.addr = chan.start << (8 + 11);
            break;
        case 0x07: // control reg
            m_enable = (data >> 7) & 1;
            if (data & 0x40) m_cbank = data & 7;
            else             m_wbank = data & 15;
            break;
        case 0x08: // channel on/off reg
            for (size_t i = 0; i < NUM_CHANNELS; i++) {
                m_chan[i].enable = (~data >> i) & 1;
                if (!m_chan[i].enable) m_chan[i].addr = m_chan[i].start << (8 + 11);
            }
            break;
        }
    }
    void generate(int* out) {
        out[0] = 0;
        out[1] = 0;
        if (!m_enable) return;
        for (uint32_t i = 0; i < NUM_CHANNELS; i++) {
            Channel& chan = m_chan[i];
            if (!chan.enable) continue;

            int sample = m_data[(chan.addr >> 11) & 0xffff];
            if (sample == 0xff) {
                chan.addr = chan.loopst << 11;
                sample = m_data[(chan.addr >> 11) & 0xffff];
                if (sample == 0xff) break;
            }
            chan.addr += chan.step;

            int lv = ((chan.pan >> 0) & 0x0f) * chan.env;
            int rv = ((chan.pan >> 4) & 0x0f) * chan.env;
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
        uint8_t  enable;
        uint8_t  env;
        uint8_t  pan;
        uint8_t  start;
        uint32_t addr;
        uint16_t step;
        uint16_t loopst;
    };

    Channel m_chan[NUM_CHANNELS] = {};
    uint8_t m_cbank              = 0;
    uint8_t m_wbank              = 0;
    bool    m_enable             = false;
    uint8_t m_data[0x10000]      = {};
};



enum {
    MIXRATE     = 44100,
    BUFFER_SIZE = 1024 * 2,
};


#pragma pack(push, 1)
struct VGMHeader {
    // 00
    uint32_t magic;
    uint32_t eof_offset;
    uint32_t version;
    uint32_t sn76489_clock;
    // 10
    uint32_t ym2413_clock;
    uint32_t gd3_offset;
    uint32_t total_samples;
    uint32_t loop_offset;
    // 20
    uint32_t loop_samples;
    uint32_t rate;
    uint16_t sn76489_feedback;
    uint8_t  sn76489_shift;
    uint8_t  sn76489_flags;
    uint32_t ym2612_clock;
    // 30
    uint32_t ym2151_clock;
    uint32_t data_offset;
    uint32_t _dummy_30[2];
    // 40
    uint32_t rf5c68_clock;
    uint32_t ym2203_clock;
    uint32_t ym2608_clock;
    uint32_t YM2610_clock;
    // 50
    uint32_t _dummy_50[4];
    // 60
    uint32_t _dummy_60[4];
    // 70
    uint32_t _dummy_70[3];
    uint8_t  volume_mod;
    uint8_t  _dummy_7d;
    uint8_t  loop_base;
    uint8_t  loop_mod;
    // 80
    uint32_t _dummy_80[4];
    // 90
    uint32_t _dummy_90[4];
    // a0
    uint32_t _dummy_a0[4];
    // b0
    uint32_t _dummy_b0[4];
    // c0
    uint32_t _dummy_c0[4];
    // d0
    uint32_t _dummy_d0[4];
    // e0
    uint32_t ga20_clock;
    uint32_t _dummy_d4[3];
    // f0
    uint32_t _dummy_f0[4];
};
#pragma pack(pop)



class VGM {
public:
    bool init(char const* filename);
    bool done() const { return m_done; }
    void render(float* buffer, uint32_t sample_count);

private:
    uint8_t next() {
        if (m_pos >= m_data.size()) return 0;
        return m_data[m_pos++];
    }

    void command();

    bool                 m_done;
    std::vector<uint8_t> m_data;
    uint32_t             m_pos;
    uint32_t             m_samples_left;
    float                m_volume;
};


RF5C68 rf5c68;

ymfm::ymfm_interface ym2612_interface;
ymfm::ym3438 ym2612(ym2612_interface);
uint32_t     ym2612_rate;
ymfm::ymfm_output<2> ym2612_out;
float ym2612_sample_pos;

ymfm::ymfm_interface ym2203_interface;
ymfm::ym2203 ym2203(ym2203_interface);
uint32_t     ym2203_rate;
ymfm::ymfm_output<4> ym2203_out;
float ym2203_sample_pos;

float rf5c68_sample_pos = 0;
float rf5c68_step;
int   rf5c68_out[2];


bool VGM::init(char const* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        printf("error: can't open file\n");
        return false;
    }
    fseek(f, 0, SEEK_END);
    m_data.resize(ftell(f));
    rewind(f);
    fread(m_data.data(), 1, m_data.size(), f);
    fclose(f);

    if (m_data.size() < 4) {
        printf("error: file too small\n");
        return false;
    }

    // check for gzip header
    if (m_data[0] == 0x1f && m_data[1] == 0x8b) {

        // inflate
        constexpr size_t CHUNK = 1 << 10;

        std::vector<uint8_t> compressed;
        std::swap(m_data, compressed);

        z_stream zs = {};
        if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) {
            printf("error: inflateInit\n");
            return false;
        }
        zs.next_in  = compressed.data();
        zs.avail_in = compressed.size();

        size_t pos = 0;
        do {
            m_data.resize(pos + CHUNK);
            zs.next_out  = m_data.data() + pos;
            zs.avail_out = CHUNK;
            pos += CHUNK;
            int ret = inflate(&zs, Z_NO_FLUSH);
            if (ret != Z_OK && ret != Z_STREAM_END) {
                printf("error: inflate %d\n", ret);
                inflateEnd(&zs);
                return false;
            }
        } while (zs.avail_out == 0);
        m_data.resize(pos - zs.avail_out);
    }

    // parse header
    if (m_data.size() < sizeof(VGMHeader)) {
        printf("error: file too small\n");
        return false;
    }
    VGMHeader const& header = *(VGMHeader const*)m_data.data();
    if (header.magic != 0x206d6756) { // "Vgm "
        printf("error: wrong magic\n");
        return false;
    }
    if (header.version < 0x151) {
        printf("error: version %x too old\n", header.version);
        return false;
    }

    m_pos          = 0x34 + header.data_offset;
    m_done         = false;
    m_samples_left = 0;

    // volume mod
    int v = header.volume_mod;
    if (v > 192) v = v - 192 - 63;
    if (v == -63) --v;
    m_volume = exp2(v / 64.0);
    m_volume *= 0.00005;

    // init chips
    if (header.ym2612_clock) {
        printf("chip ym2612 @ %u\n", header.ym2612_clock & 0x7fffffff);
        ym2612.reset();
        ym2612_rate = ym2612.sample_rate(header.ym2612_clock & 0x7fffffff);
    }
    if (header.ym2203_clock) {
        printf("chip ym2203 @ %u\n", header.ym2203_clock);
        ym2203.reset();
        ym2203.set_fidelity(ymfm::OPN_FIDELITY_MIN);
        ym2203_rate = ym2203.sample_rate(header.ym2203_clock);
    }
    if (header.ym2151_clock) {
        printf("chip ym2151 @ %u\n", header.ym2151_clock);
        printf("TODO\n");
    }
    if (header.rf5c68_clock) {
        printf("chip rf5c68 @ %u\n", header.rf5c68_clock);
        rf5c68_step = header.rf5c68_clock / 384.0 / MIXRATE;
    }
    if (header.version >= 0x171 && header.ga20_clock) {
        printf("chip ga20  @ %u\n", header.ga20_clock);
        printf("TODO\n");
    }

    return true;
}


void VGM::command() {
    uint8_t cmd = next();
    uint8_t  b = 0;
    uint32_t n = 0;
    switch (cmd) {
    case 0xb0: // RF5C68, write value dd to register aa
        b = next();
        rf5c68.write_reg(b, next());
        break;

    case 0x67: // data block
        next();
        b = next();
        n = next();
        n |= next() << 8;
        n |= next() << 16;
        n |= next() << 24;
        if (b == 0xc0) {
            uint16_t addr = next();
            addr |= next() << 8;
            for (; n > 2; ++addr, --n) rf5c68.write_mem(addr, next());
        }
        else {
            printf("warning: unknown data block %02x %04x\n", b, n);
            m_pos += n;
        }
        break;

    case 0x52: // YM2612 port 0, write value dd to register aa
        ym2612.write_address(next());
        ym2612.write_data(next());
        break;
    case 0x53: // YM2612 port 1, write value dd to register aa
        ym2612.write_address_hi(next());
        ym2612.write_data_hi(next());
        break;

    case 0x55: // YM2203, write value dd to register aa
        ym2203.write_address(next());
        ym2203.write_data(next());
        break;


    case 0x61: // Wait n samples
        m_samples_left = next();
        m_samples_left |= next() << 8;
        break;
    case 0x62:
        m_samples_left = MIXRATE / 60;
        break;
    case 0x63:
        m_samples_left = MIXRATE / 50;
        break;
    case 0x70: case 0x71: case 0x72: case 0x73:
    case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7a: case 0x7b:
    case 0x7c: case 0x7d: case 0x7e: case 0x7f:
        m_samples_left = (cmd & 0xf) + 1;
        break;

    case 0x66: // end of sound data
        printf("EOF\n");
        m_done = true;
        break;

    default:
        printf("error: unknown command %02x\n", cmd);
        m_done = true;
        break;
    }
}

void VGM::render(float* buffer, uint32_t sample_count) {
    while (sample_count > 0) {
        while (!m_done && m_samples_left == 0) command();
        if (m_done) {
            while (sample_count > 0) {
                *buffer++ = 0;
                *buffer++ = 0;
                --sample_count;
            }
            return;
        }

        constexpr float PAN[] = {
            std::sqrt(0.5),
            std::sqrt(0.5 + 0.1),
            std::sqrt(0.5 - 0.1),
        };

        uint32_t samples = std::min(sample_count, m_samples_left);
        for (uint32_t i = 0; i < samples; ++i) {
            buffer[0] = 0;
            buffer[1] = 0;

            // ym2612
            ym2612_sample_pos += ym2612_rate / float(MIXRATE);
            while (ym2612_sample_pos >= 1) {
                ym2612.generate(&ym2612_out);
                ym2612_sample_pos -= 1;
            }
            buffer[0] += ym2612_out.data[0] * m_volume;
            buffer[1] += ym2612_out.data[1] * m_volume;

            // ym2203
            ym2203_sample_pos += ym2203_rate / float(MIXRATE);
            while (ym2203_sample_pos >= 1) {
                ym2203.generate(&ym2203_out);
                ym2203_sample_pos -= 1;
            }
            buffer[0] += ym2203_out.data[0] * m_volume;
            buffer[1] += ym2203_out.data[0] * m_volume;

            buffer[0] += ym2203_out.data[1] * m_volume * PAN[0];
            buffer[1] += ym2203_out.data[1] * m_volume * PAN[0];

            buffer[0] += ym2203_out.data[2] * m_volume * PAN[1];
            buffer[1] += ym2203_out.data[2] * m_volume * PAN[2];

            buffer[0] += ym2203_out.data[3] * m_volume * PAN[2];
            buffer[1] += ym2203_out.data[3] * m_volume * PAN[1];

            // rf5c68
            rf5c68_sample_pos += rf5c68_step ;
            while (rf5c68_sample_pos >= 1) {
                rf5c68.generate(rf5c68_out);
                rf5c68_sample_pos -= 1;
            }
            buffer[0] += rf5c68_out[0] * m_volume;
            buffer[1] += rf5c68_out[1] * m_volume;

            buffer += 2;
        }

        m_samples_left -= samples;
        sample_count -= samples;
    }
}


void usage(char const* app) {
    printf("Usage: %s vgm-file\n", app);
    exit(1);
}


VGM vgm;


void audio_callback(void* u, Uint8* stream, int bytes) {
    vgm.render((float*)stream, bytes / sizeof(float) / 2);
}


int main(int argc, char** argv) {
    if (argc != 2) usage(argv[0]);
    char const* filename = argv[1];

    if (!vgm.init(filename)) return 1;

    SDL_AudioSpec spec = {
        MIXRATE, AUDIO_F32, 2, 0, BUFFER_SIZE, 0, 0, &audio_callback, nullptr,
    };
    SDL_Init(SDL_INIT_AUDIO);
    SDL_OpenAudio(&spec, nullptr);
    SDL_PauseAudio(0);
    while (!vgm.done()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) break;
        }
        if (event.type == SDL_QUIT) break;
        SDL_Delay(100);
    }
    SDL_CloseAudio();
    SDL_Quit();
    return 0;
}

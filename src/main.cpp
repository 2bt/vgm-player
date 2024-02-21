#include <SDL.h>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <zlib.h>
#include "ymfm_opn.h"


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
    uint32_t _dummy_40[3];
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


enum {
    MIXRATE     = 44100,
    BUFFER_SIZE = 1024 * 2,
};

ymfm::ymfm_interface interface;
//ymfm::ym2612         ym2612(interface);
ymfm::ym3438         ym2612(interface);
uint32_t             ym2612_rate;

double sample_pos;


class VGM {
public:
    bool init(char const* filename);

    void render(float* buffer, uint32_t sample_count) {

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

            uint32_t samples = std::min(sample_count, m_samples_left);
            for (uint32_t i = 0; i < samples; ++i) {
                buffer[0] = 0;
                buffer[1] = 0;

                sample_pos += double(ym2612_rate) / MIXRATE;
                ymfm::ymfm_output<2> out;
                while (sample_pos >= 1) {
                    ym2612.generate(&out);
                    sample_pos -= 1;
                }

                buffer[0] = out.data[0] * 0.00005 * m_volume;
                buffer[1] = out.data[1] * 0.00005 * m_volume;
                buffer += 2;
            }

            m_samples_left -= samples;
            sample_count -= samples;
        }
    }

    bool done() const { return m_done; }

private:

    uint8_t next() {
        if (m_pos >= m_data.size()) return 0;
        return m_data[m_pos++];
    }

    void command();

    bool m_done;
    std::vector<uint8_t> m_data;
    uint32_t m_pos;
    uint32_t m_samples_left;
    float m_volume;
};



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

    m_pos = 0x34 + header.data_offset;
    m_done = false;
    m_samples_left = 0;

    // volume mod
    int v = header.volume_mod;
    if (v > 192) v = v - 192 - 63;
    if (v == -63) --v;
    m_volume = exp2(v / 64.0);

    // init chips
    if (header.ym2612_clock) {
        printf("ym2612 %u\n", header.ym2612_clock & 0x7fffffff);
        ym2612.reset();
        ym2612_rate = ym2612.sample_rate(header.ym2612_clock & 0x7fffffff);
    }
    if (header.ym2151_clock) {
        printf("ym2151 %u\n", header.ym2151_clock & 0x7fffffff);
    }
    if (header.rf5c68_clock) {
        printf("rf5c68 %u\n", header.rf5c68_clock);
    }
    if (header.version >= 0x171 && header.ga20_clock) {
        printf("ga20   %u\n", header.ga20_clock);
    }

    return true;
}


void VGM::command() {
    uint8_t cmd = next();
//    printf("%04x | cmd %02x\n", m_pos - 1, cmd);
    uint8_t  b;
    uint32_t n;
    switch (cmd) {
    case 0xb0: // RF5C68, write value dd to register aa
        next();
        next();
        break;

    case 0x52: // YM2612 port 0, write value dd to register aa
        ym2612.write_address(next());
        ym2612.write_data(next());
        break;
    case 0x53: // YM2612 port 1, write value dd to register aa
        ym2612.write_address_hi(next());
        ym2612.write_data_hi(next());
        break;

    case 0x67: // data block
        next();
        b = next();
        n = next();
        n |= next() << 8;
        n |= next() << 16;
        n |= next() << 24;
        printf("data block %02x %04x\n", b, n);
        m_pos += n;
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
        printf("error: unknown command\n");
        m_done = true;
        break;
    }
}


void usage(char const* app) {
    printf("Usage: %s vgm-file\n", app);
    exit(1);
}

VGM vgm;

void audio_callback(void* u, Uint8* stream, int bytes) {
    vgm.render((float*) stream, bytes / sizeof(float) / 2);
}


int main(int argc, char** argv) {
    if (argc != 2) usage(argv[0]);
    char const* filename = argv[1];

    if (!vgm.init(filename)) return 1;

    SDL_AudioSpec spec = {
        MIXRATE, AUDIO_F32, 2, 0,
        BUFFER_SIZE, 0, 0,
        &audio_callback,
        nullptr,
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
    printf("bye\n");
    SDL_CloseAudio();
    SDL_Quit();
    return 0;
}

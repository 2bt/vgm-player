#include <cstdio>
#include <vector>
#include <fstream>
#include <SDL.h>
#include <sndfile.h>
#include <zlib.h>

#include "ymfm_opm.h"
#include "ymfm_opn.h"

#include "ga20.hpp"
#include "rf5c68.hpp"
#include "ym2203.hpp"
#include "lr35902.hpp"


enum { MIXRATE = 44100 };

template<class Chip>
struct IntResampler {
    Chip  chip;
    int   out[2] = {};
    float pos    = 0;
    float step   = 0;
    void advance() {
        for (pos += step; pos >= 1; pos -= 1) chip.generate(out);
    }
    void tick(int* buf) {
        advance();
        buf[0] += out[0];
        buf[1] += out[1];
    }
};

template<class Chip, int N = 2>
struct YmfmResampler {
    ymfm::ymfm_interface iface;
    ymfm::ymfm_output<N> out;
    Chip                 chip{iface};
    float                pos  = 0;
    float                step = 0;
    void init(uint32_t clock) {
        chip.reset();
        step = chip.sample_rate(clock) / float(MIXRATE);
    }
    void advance() {
        for (pos += step; pos >= 1; pos -= 1) chip.generate(&out);
    }
    void tick(int* buf) {
        advance();
        buf[0] += out.data[0];
        buf[1] += out.data[1];
    }
};

class VGM {
public:
    bool init(char const* filename, int loop_count);
    void use_simple_ym2203() { m_use_simple_ym2203 = true; }
    bool done() const { return m_done; }
    void render(float* buffer, uint32_t sample_count);

private:
    uint8_t next() {
        if (m_pos >= m_data.size()) return 0;
        return m_data[m_pos++];
    }

    void command();

    bool                 m_done;
    bool                 m_use_simple_ym2203 = false;
    std::vector<uint8_t> m_data;
    uint32_t             m_pos;
    uint32_t             m_loop_pos;
    uint32_t             m_samples_left;
    float                m_volume;
    int                  m_loop_counter;

    // chips
    YmfmResampler<ymfm::ym3438>    ym2612;
    YmfmResampler<ymfm::ym2151>    ym2151;
    YmfmResampler<ymfm::ym2203, 4> ym2203;
    YM2203                         ym2203_simple;
    IntResampler<RF5C68>           rf5c68;
    IntResampler<GA20>             ga20;
    IntResampler<LR35902>          lr35902;
};

#pragma pack(push, 1)
struct VGMHeader {
    uint32_t magic;
    uint32_t eof_offset;
    uint32_t version;
    uint32_t sn76489_clock;
    uint32_t ym2413_clock;
    uint32_t gd3_offset;
    uint32_t total_samples;
    uint32_t loop_offset;
    uint32_t loop_samples;
    uint32_t rate;
    uint16_t sn76489_feedback;
    uint8_t  sn76489_shift;
    uint8_t  sn76489_flags;
    uint32_t ym2612_clock;
    uint32_t ym2151_clock;
    uint32_t data_offset;
    uint32_t _dummy_30[2];
    uint32_t rf5c68_clock;
    uint32_t ym2203_clock;
    uint32_t ym2608_clock;
    uint32_t YM2610_clock;
    uint32_t _dummy_50[4];
    uint32_t _dummy_60[4];
    uint32_t _dummy_70[3];
    uint8_t  volume_mod;
    uint8_t  _dummy_7d;
    uint8_t  loop_base;
    uint8_t  loop_mod;
    uint32_t lr35902_clock;
    uint32_t _dummy_81[3];
    uint32_t _dummy_90[4];
    uint32_t _dummy_a0[4];
    uint32_t _dummy_b0[4];
    uint32_t _dummy_c0[4];
    uint32_t _dummy_d0[4];
    uint32_t ga20_clock;
    uint32_t _dummy_e4[3];
    uint32_t _dummy_f0[4];
};
#pragma pack(pop)


bool inflate_gzip(std::vector<uint8_t>& data) {
    if (data.size() < 2 || data[0] != 0x1f || data[1] != 0x8b) return true;

    constexpr size_t CHUNK = 1 << 10;
    std::vector<uint8_t> compressed;
    std::swap(data, compressed);

    z_stream zs = {};
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) {
        printf("error: inflateInit\n");
        return false;
    }
    zs.next_in  = compressed.data();
    zs.avail_in = compressed.size();

    size_t pos = 0;
    do {
        data.resize(pos + CHUNK);
        zs.next_out  = data.data() + pos;
        zs.avail_out = CHUNK;
        pos += CHUNK;
        int ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            printf("error: inflate %d\n", ret);
            inflateEnd(&zs);
            return false;
        }
    } while (zs.avail_out == 0);
    data.resize(pos - zs.avail_out);
    inflateEnd(&zs);
    return true;
}

bool VGM::init(char const* filename, int loop_count) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        printf("error: couldn't open file\n");
        return false;
    }
    m_data.resize(file.tellg());
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(m_data.data()), m_data.size())) {
        printf("error: couldn't read file\n");
        return false;
    }
    if (!inflate_gzip(m_data)) return false;

    // parse header
    if (m_data.size() < sizeof(VGMHeader)) {
        printf("error: file too small\n");
        return false;
    }
    VGMHeader& header = *(VGMHeader*)m_data.data();
    if (header.magic != 0x206d6756) { // "Vgm "
        printf("error: wrong magic\n");
        return false;
    }
    printf("version = %x\n", header.version);
    if (header.version < 0x151) {
        printf("error: version %x too old\n", header.version);
        return false;
    }

    m_loop_counter = loop_count;
    m_done         = false;
    m_pos          = 0x34 + header.data_offset;
    m_samples_left = 0;
    m_loop_pos     = header.loop_offset + 0x1c;
    if (m_loop_pos == 0x1c || m_loop_pos >= m_data.size()) m_loop_pos = 0;


    // volume mod
    int v = header.volume_mod;
    if (v > 192) v = v - 192 - 63;
    if (v == -63) --v;
    m_volume = exp2(v / 64.0);
    printf("volume = %f\n", m_volume);
    m_volume *= 0.00005;

    // init chips
    if (header.ym2612_clock) {
        header.ym2612_clock &= 0x7fffffff;
        printf("ym2612 clock = %u\n", header.ym2612_clock);
        ym2612.init(header.ym2612_clock);
    }
    if (header.ym2203_clock) {
        header.ym2203_clock &= 0x3fffffff;
        printf("ym2203 clock = %u\n", header.ym2203_clock);
        ym2203.chip.set_fidelity(ymfm::OPN_FIDELITY_MIN);
        ym2203.init(header.ym2203_clock);
        ym2203_simple.set_clock(header.ym2203_clock);
    }
    if (header.ym2151_clock) {
        printf("ym2151 clock = %u\n", header.ym2151_clock);
        ym2151.init(header.ym2151_clock);
    }
    if (header.rf5c68_clock) {
        printf("rf5c68 clock = %u\n", header.rf5c68_clock);
        rf5c68.step = header.rf5c68_clock / 384.0 / float(MIXRATE);
    }
    if (header.version >= 0x161 && header.lr35902_clock) {
        printf("lr35902 clock = %u\n", header.lr35902_clock);
        lr35902.step = lr35902.chip.sample_rate(header.lr35902_clock) / float(MIXRATE);
    }
    if (header.version >= 0x171 && header.ga20_clock) {
        printf("ga20 clock = %u\n", header.ga20_clock);
        ga20.step = ga20.chip.sample_rate(header.ga20_clock) / float(MIXRATE);
    }

    return true;
}


void VGM::command() {
    uint8_t  cmd = next();
    uint8_t  b   = 0;
    uint32_t n   = 0;
    switch (cmd) {
    case 0xb0: // RF5C68, write value dd to register aa
        b = next();
        rf5c68.chip.write_reg(b, next());
        break;
    case 0xb3: // LR35902, write value dd to register aa
        b = next();
        lr35902.chip.write_reg(b, next());
        break;
    case 0xbf: // GA20, write value dd to register aa
        b = next();
        ga20.chip.write_reg(b, next());
        break;
    case 0x52: // YM2612 port 0, write value dd to register aa
        ym2612.chip.write_address(next());
        ym2612.chip.write_data(next());
        break;
    case 0x53: // YM2612 port 1, write value dd to register aa
        ym2612.chip.write_address_hi(next());
        ym2612.chip.write_data_hi(next());
        break;
    case 0x54: // YM2151, write value dd to register aa
        ym2151.chip.write_address(next());
        ym2151.chip.write_data(next());
        break;
    case 0x55: { // YM2203, write value dd to register aa
        uint8_t a = next();
        uint8_t v = next();
        // XXX: only fm voice #0
        //if (a < 16 || (a == 0x28 && (v & 3) != 0)) break;
        ym2203.chip.write_address(a);
        ym2203.chip.write_data(v);
        ym2203_simple.write_reg(a, v);
        break;
    }
    case 0x67: // data block
        next();
        b = next();
        n = next();
        n |= next() << 8;
        n |= next() << 16;
        n |= next() << 24;
        if (b == 0xc0) { // rf5c68
            uint16_t addr = next();
            addr |= next() << 8;
            for (; n > 2; --n) rf5c68.chip.write_mem(addr++, next());
        }
        else if (b == 0x93) { // ga20 rom
            uint32_t rom = next();
            rom |= next() << 8;
            rom |= next() << 16;
            rom |= next() << 24;
            (void)rom;

            uint32_t addr = next();
            addr |= next() << 8;
            addr |= next() << 16;
            addr |= next() << 24;
            for (; n > 8; --n) ga20.chip.write_mem(addr++, next());
        }
        else {
            printf("warning: unknown data block %02x %04x\n", b, n);
            m_pos += n;
        }
        break;

    case 0x61: // wait n samples
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
        if (m_loop_pos) {
            m_pos = m_loop_pos;
            if (--m_loop_counter > 0) {
                printf("looping\n");
                break;
            }
        }
        printf("done\n");
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

        uint32_t samples = std::min(sample_count, m_samples_left);
        for (uint32_t i = 0; i < samples; ++i) {
            int ibuf[2] = {};

            ym2612.tick(ibuf);
            ym2151.tick(ibuf);
            rf5c68.tick(ibuf);
            ga20.tick(ibuf);
            lr35902.tick(ibuf);

            buffer[0] = ibuf[0] * m_volume;
            buffer[1] = ibuf[1] * m_volume;

            if (m_use_simple_ym2203) {
                ym2203_simple.render(buffer);
            }
            else {
                // handle ym2203 separately to apply panning
                ym2203.advance();
                static const float PAN[] = {
                    0.5f * std::sqrt(0.5f),
                    0.5f * std::sqrt(0.5f + 0.2f),
                    0.5f * std::sqrt(0.5f - 0.2f),
                };
                buffer[0] += ym2203.out.data[0] * m_volume;
                buffer[1] += ym2203.out.data[0] * m_volume;
                buffer[0] += ym2203.out.data[1] * PAN[0] * m_volume;
                buffer[1] += ym2203.out.data[1] * PAN[0] * m_volume;
                buffer[0] -= ym2203.out.data[2] * PAN[1] * m_volume;
                buffer[1] -= ym2203.out.data[2] * PAN[2] * m_volume;
                buffer[0] += ym2203.out.data[3] * PAN[2] * m_volume;
                buffer[1] += ym2203.out.data[3] * PAN[1] * m_volume;
            }

            buffer += 2;
        }

        m_samples_left -= samples;
        sample_count -= samples;
    }
}



VGM vgm;


void audio_callback(void* u, Uint8* stream, int bytes) {
    vgm.render((float*)stream, bytes / sizeof(float) / 2);
}


int main(int argc, char** argv) {
    bool wave = false;
    bool usage = false;
    int  loop_count = 0;
    int  opt;
    while ((opt = getopt(argc, argv, "wsl:")) != -1) {
        switch (opt) {
        case 'w': wave = true; break;
        case 's': vgm.use_simple_ym2203(); break;
        case 'l': loop_count = atoi(optarg); break;
        default: usage = true; break;
        }
    }
    if (optind >= argc || usage) {
        printf("Usage: %s [-w] [-s] [-l loop_count] vgm-file...\n", argv[0]);
        return 1;
    }

    if (wave) {
        SF_INFO info = { 0, MIXRATE, 2, SF_FORMAT_WAV | SF_FORMAT_FLOAT, 0, 0 };
        SNDFILE* f = sf_open("out.wav", SFM_WRITE, &info);
        for (; optind < argc; optind++) {
            printf(">>> %s\n", argv[optind]);
            if (!vgm.init(argv[optind], loop_count)) return 1;
            while (!vgm.done()) {
                float buffer[2];
                vgm.render(buffer, 1);
                sf_writef_float(f, buffer, 1);
            }
        }
        sf_close(f);
        return 0;
    }

    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_Init(SDL_INIT_AUDIO);
    SDL_AudioSpec spec = { MIXRATE, AUDIO_F32, 2, 0, 1024, 0, 0, &audio_callback, nullptr };
    SDL_OpenAudio(&spec, nullptr);
    for (; optind < argc; optind++) {
        printf(">>> %s\n", argv[optind]);
        if (!vgm.init(argv[optind], loop_count)) return 1;
        SDL_PauseAudio(0);
        while (!vgm.done()) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) goto exit;
            }
            SDL_Delay(100);
        }
        SDL_PauseAudio(1);
    }
exit:
    SDL_CloseAudio();
    SDL_Quit();
    return 0;
}

#include "ga20.hpp"
#include "rf5c68.hpp"

#include "ymfm_opm.h"
#include "ymfm_opn.h"
#include "ym2612.hpp"

#include <SDL.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <sndfile.h>
#include <vector>
#include <fstream>
#include <zlib.h>


enum {
    MIXRATE = 44100,
};


class Simple2203 {
public:
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
    }

    void render(float* out) {
        constexpr float PAN_SSG[] = {
            0.5f * std::sqrt(0.3f),
            0.5f * std::sqrt(0.5f),
            0.5f * std::sqrt(0.7f),
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
            uint32_t block_freq = m_reg[0xa0 + c] | ((m_reg[0xa4 + c] & 0x3f) << 8);

            for (int o = 0; o < 4; ++o) {
                Op&     op = chan.ops[o];
                uint8_t oo = c + OP_OFFSET[o];

                // multi-freq
                if (c == 2 && (m_reg[0x27] & 0xc0) && o < 3) {
                    int q = (o + 1) % 3;
                    block_freq = m_reg[0xa8 + q] | ((m_reg[0xac + q] & 0x3f) << 8);
                }

                // calculate pitch and keycode
                uint32_t step  = block_freq & 0x7ff;
                uint8_t  block = block_freq >> 11;
                float    pitch = (step << block) * m_cps / (0x6000000 * 3);
                uint8_t  keycode = (m_reg[0xa4 + c] >> 1) & 0b11110;
                keycode |= (0xfe80 >> (step >> 7)) & 1;


                // multiple
                uint8_t multiple = m_reg[0x30 + oo] & 0xf;
                multiple         = multiple * 2 | (multiple == 0);
                // TODO: detune
                op.phase += pitch * multiple;
                op.phase -= int(op.phase);

                uint8_t scaling = m_reg[0x50] >> 6;
                scaling         = keycode >> (scaling ^ 3);
                int adsr[4] = {
                    m_reg[0x50 + oo] & 0x1f,
                    m_reg[0x60 + oo] & 0x1f,
                    m_reg[0x70 + oo] & 0x1f,
                    ((m_reg[0x80 + oo] & 0x0f) << 1) | 1,
                };
                int rate = adsr[op.state] * 2;
                if (rate > 0) rate += scaling;
                rate       = std::min<int>(rate, op.state == Op::ATTACK ? 63 : 60);
                uint32_t f = rate <= 1 ? 0 : ((4 | (rate & 3)) << (rate >> 2)) >> 2;

                uint8_t sustain = m_reg[0x80 + oo] >> 4;
                if (sustain == 15) sustain = 31;
                float sus_level = std::pow(0.707f, sustain);

                if (op.state == Op::ATTACK) {
                    if (f > 0) op.level += f * (1.0f / 16.06f / MIXRATE);
                    if (op.level >= 1.0f) {
                        op.level = 1.0f;
                        op.state = Op::DECAY;
                    }
                }
                else {
                    if (f > 0) op.level *= std::pow(0.9524f, f * (1.0f / MIXRATE));
                    if (op.state == Op::DECAY && op.level <= sus_level) {
                        op.level = sus_level;
                        op.state = Op::SUSTAIN;
                    }
                }
            }

            uint8_t connect  = 1 << (m_reg[0xb0 + c] & 0x7);
            uint8_t feedback = (m_reg[0xb0 + c] >> 3) & 0x7;
            // algorithm
            float a[4] = {};
            float o = chan.feedback = op_amp(c, 0, chan.feedback * feedback * (3.0f / 32.0f));
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
        shift *= 4.0f; // sounds ok but is this correct?
        float s = std::sin((op.phase + shift) * 2.0f * M_PI);
        uint8_t total_level = m_reg[0x40 + c + OP_OFFSET[o]] & 0x7f;
        float   vol         = std::exp2f(total_level * -0.125f);
        return s * op.level * vol;
    }

    void key_onoff(uint8_t c, uint8_t op_mask) {
        FmChan& chan = m_fm_chans[c];
        for (int o = 0; o < 4; ++o) {
            uint8_t m = 1 << o;
            if ((op_mask & m) == (chan.op_mask & m)) continue;
            if (op_mask & m) {
                chan.ops[o].state = Op::ATTACK;
                chan.ops[o].level = 0;
                chan.ops[o].phase = 0;
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
        float pitch;
        float feedback;
        Op    ops[4];
    };
    float    m_cps          = 0.0f; // cycles per sample
    uint8_t  m_reg[256]     = {};
    float    m_noise_count  = 0;
    uint32_t m_noise_state  = 1;
    SsgChan  m_ssg_chans[3] = {};
    FmChan   m_fm_chans[3]  = {};
};

Simple2203 simple2203;


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
    bool init(char const* filename, int loop_count);
    void use_simple2203() { m_use_simple2203 = true; }
    bool done() const { return m_done; }
    void render(float* buffer, uint32_t sample_count);

private:
    uint8_t next() {
        if (m_pos >= m_data.size()) return 0;
        return m_data[m_pos++];
    }

    void command();

    bool                 m_done;
    bool                 m_use_simple2203 = false;
    std::vector<uint8_t> m_data;
    uint32_t             m_pos;
    uint32_t             m_loop_pos;
    uint32_t             m_samples_left;
    float                m_volume;
    int                  m_loop_counter;
};

// chips
//ymfm::ymfm_interface ym2612_interface;
//ymfm::ym3438         ym2612(ym2612_interface);
//uint32_t             ym2612_rate;
//ymfm::ymfm_output<2> ym2612_out;
//float                ym2612_sample_pos;
foobar::ym2612       ym2612;
uint32_t             ym2612_rate;
foobar::ymfm_output<2> ym2612_out;
float                ym2612_sample_pos;

ymfm::ymfm_interface ym2151_interface;
ymfm::ym2151         ym2151(ym2151_interface);
uint32_t             ym2151_rate;
ymfm::ymfm_output<2> ym2151_out;
float                ym2151_sample_pos;

ymfm::ymfm_interface ym2203_interface;
ymfm::ym2203         ym2203(ym2203_interface);
uint32_t             ym2203_rate;
ymfm::ymfm_output<4> ym2203_out;
float                ym2203_sample_pos;

RF5C68 rf5c68;
float  rf5c68_sample_pos = 0;
float  rf5c68_step;
int    rf5c68_out[2];

GA20   ga20;
float  ga20_sample_pos = 0;
float  ga20_step;
int    ga20_out[2];


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
        printf("ym2612 clock = %u\n", header.ym2612_clock & 0x7fffffff);
        ym2612.reset();
        ym2612_rate = ym2612.sample_rate(header.ym2612_clock & 0x7fffffff);
    }
    if (header.ym2203_clock) {
        printf("ym2203 clock = %u\n", header.ym2203_clock);
        ym2203.reset();
        ym2203.set_fidelity(ymfm::OPN_FIDELITY_MIN);
        ym2203_rate = ym2203.sample_rate(header.ym2203_clock);
        simple2203.set_clock(header.ym2203_clock);
    }
    if (header.ym2151_clock) {
        printf("ym2151 clock = %u\n", header.ym2151_clock);
        ym2151.reset();
        ym2151_rate = ym2151.sample_rate(header.ym2151_clock);
    }
    if (header.rf5c68_clock) {
        printf("rf5c68 clock = %u\n", header.rf5c68_clock);
        rf5c68_step = header.rf5c68_clock / 384.0 / MIXRATE;
    }
    if (header.version >= 0x171 && header.ga20_clock) {
        printf("ga20 clock = %u\n", header.ga20_clock);
        ga20_step = ga20.sample_rate(header.ga20_clock) / MIXRATE;
    }

    return true;
}


std::array<uint8_t, 256> reg_val;
std::array<uint8_t, 256> reg_set;

void VGM::command() {
    uint8_t  cmd = next();
    uint8_t  b   = 0;
    uint32_t n   = 0;
    switch (cmd) {

    case 0xb0: // RF5C68, write value dd to register aa
        b = next();
        rf5c68.write_reg(b, next());
        break;

    case 0xbf: // GA20, write value dd to register aa
        b = next();
        ga20.write_reg(b, next());
        break;

    case 0x52: // YM2612 port 0, write value dd to register aa
        ym2612.write_address(next());
        ym2612.write_data(next());
        break;
    case 0x53: // YM2612 port 1, write value dd to register aa
        ym2612.write_address_hi(next());
        ym2612.write_data_hi(next());
        break;
    case 0x54: // YM2151, write value dd to register aa
        ym2151.write_address(next());
        ym2151.write_data(next());
        break;
    case 0x55: // YM2203, write value dd to register aa
    {
        uint8_t a = next();
        uint8_t v = next();
        reg_val[a] = v;
        ++reg_set[a];
        ym2203.write_address(a);
        ym2203.write_data(v);
        simple2203.write_reg(a, v);
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
            for (; n > 2; --n) rf5c68.write_mem(addr++, next());
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
            for (; n > 8; --n) ga20.write_mem(addr++, next());
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

            // ym2151
            ym2151_sample_pos += ym2151_rate / float(MIXRATE);
            while (ym2151_sample_pos >= 1) {
                ym2151.generate(&ym2151_out);
                ym2151_sample_pos -= 1;
            }
            buffer[0] += ym2151_out.data[0] * m_volume;
            buffer[1] += ym2151_out.data[1] * m_volume;

            // ym2203
            if (m_use_simple2203) {
                float f[2] = {};
                simple2203.render(f);
                buffer[0] += f[0];
                buffer[1] += f[1];
            }
            else {
                ym2203_sample_pos += ym2203_rate / float(MIXRATE);
                while (ym2203_sample_pos >= 1) {
                    ym2203.generate(&ym2203_out);
                    ym2203_sample_pos -= 1;
                }
                buffer[0] += ym2203_out.data[0] * m_volume;
                buffer[1] += ym2203_out.data[0] * m_volume;
                // ssg voice panning
                constexpr float PAN[] = {
                    0.5 * std::sqrt(0.5),
                    0.5 * std::sqrt(0.5 + 0.2),
                    0.5 * std::sqrt(0.5 - 0.2),
                };
                buffer[0] += ym2203_out.data[1] * m_volume * PAN[0];
                buffer[1] += ym2203_out.data[1] * m_volume * PAN[0];
                buffer[0] += ym2203_out.data[2] * m_volume * PAN[1];
                buffer[1] += ym2203_out.data[2] * m_volume * PAN[2];
                buffer[0] += ym2203_out.data[3] * m_volume * PAN[2];
                buffer[1] += ym2203_out.data[3] * m_volume * PAN[1];
            }

            // rf5c68
            rf5c68_sample_pos += rf5c68_step;
            while (rf5c68_sample_pos >= 1) {
                rf5c68.generate(rf5c68_out);
                rf5c68_sample_pos -= 1;
            }
            buffer[0] += rf5c68_out[0] * m_volume;
            buffer[1] += rf5c68_out[1] * m_volume;

            // rf5c68
            ga20_sample_pos += ga20_step;
            while (ga20_sample_pos >= 1) {
                ga20.generate(ga20_out);
                ga20_sample_pos -= 1;
            }
            buffer[0] += ga20_out[0] * m_volume;
            buffer[1] += ga20_out[1] * m_volume;

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
    bool use_simple2203 = false;
    int  loop_count = 0;
    int  opt;
    while ((opt = getopt(argc, argv, "wsl:")) != -1) {
        switch (opt) {
        case 'w': wave = true; break;
        case 's': use_simple2203 = true; break;
        case 'l': loop_count = atoi(optarg); break;
        default: usage = true; break;
        }
    }
    if (optind >= argc || usage) {
        printf("Usage: %s [-w] [-s] [-l loop_count] vgm-file...\n", argv[0]);
        return 1;
    }

    if (use_simple2203) vgm.use_simple2203();

    if (wave) {
        SF_INFO info = {
            0, MIXRATE, 1, SF_FORMAT_WAV | SF_FORMAT_FLOAT, 0, 0,
        };
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
    SDL_AudioSpec spec = {
        MIXRATE, AUDIO_F32, 2, 0, 1024, 0, 0, &audio_callback, nullptr,
    };
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

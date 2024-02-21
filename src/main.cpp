#include <SDL.h>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <zlib.h>

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


class VGM {
public:
    bool init(char const* filename);
    bool run();

private:

    uint8_t next() {
        if (m_pos >= m_data.size()) return 0;
        return m_data[m_pos++];
    }

    std::vector<uint8_t> m_data;
    uint32_t m_pos;
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

    if (m_data.size() < sizeof(VGMHeader)) {
        printf("error: file too small\n");
        return false;
    }

    return true;
}



bool VGM::run() {

    VGMHeader const& header = *(VGMHeader const*)m_data.data();
    if (header.magic != 0x206d6756) { // "Vgm "
        printf("error: wrong magic\n");
        return false;
    }

    printf("ym2612 %u\n", header.ym2612_clock);
    printf("ym2151 %u\n", header.ym2151_clock);
    printf("rf5c68 %u\n", header.rf5c68_clock);
    if (header.version >= 0x171) {
        printf("ga20   %u\n", header.ga20_clock);
    }

    m_pos = 0x34 + header.data_offset;

    for (;;) {
        uint32_t p = m_pos;
        uint8_t cmd = next();
        printf("%04x | cmd %02x\n", p, cmd);

        uint8_t addr;
        uint8_t val;
        uint32_t n;

        switch (cmd) {
        case 0xb0: // RF5C68, write value dd to register aa
            addr = next();
            val = next();
            break;
        case 0x52: // YM2612 port 0, write value dd to register aa
            addr = next();
            val = next();
            break;
        case 0x53: // YM2612 port 1, write value dd to register aa
            addr = next();
            val = next();
            break;

        case 0x61: // Wait n samples, n can range from 0 to 65535
            n = next();
            n |= next() << 8;
            printf("wait %d samples\n", n);
            break;
        case 0x70: case 0x71: case 0x72: case 0x73:
        case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7a: case 0x7b:
        case 0x7c: case 0x7d: case 0x7e: case 0x7f:
            printf("wait %d samples\n", (cmd & 0xf) + 1);
            break;

        case 0x66: // end of sound data
            return true;
            break;

        default:
            printf("error: unknown command\n");
            return false;
        }
    }
}


void usage(char const* app) {
    printf("Usage: %s vgm-file\n", app);
    exit(1);
}

int main(int argc, char** argv) {
    if (argc != 2) usage(argv[0]);
    char const* filename = argv[1];

    VGM vgm;
    if (!vgm.init(filename)) return 1;
    if (!vgm.run()) return 1;

    return 0;
}

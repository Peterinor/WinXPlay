#include "../../tools/mrn/av_clip.h"
#include <fstream>
#include <iostream>
using namespace cp;
Bytes read(const char *name) {
    std::ifstream f(name, std::ios::binary);
    require(f.good(), "Missing fixture");
    f.seekg(0, std::ios::end);
    size_t size = size_t(f.tellg());
    f.seekg(0);
    require(size < 16 * 1024 * 1024, "Fixture too large");
    Bytes b(size);
    f.read((char *)&b[0], size);
    require(f.good(), "Short fixture read");
    return b;
}
void rejected(const Bytes &b) {
    bool failed = false;
    try {
        AvClip clip(b);
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "Malformed fixture accepted");
}
int main(int argc, char **argv) {
    try {
        require(argc == 3, "Expected H264 and RGB fixtures");
        Bytes h = read(argv[1]), r = read(argv[2]);
        AvClip video(h), rgb(r);
        require(video.frames.size() == 180 && video.width == 800 && video.height == 480,
                "H264 fixture mismatch");
        require(rgb.frames.size() == 60 && rgb.codec == 2, "RGB fixture mismatch");
        rejected(Bytes());
        Bytes b = h;
        b.resize(23);
        rejected(b);
        b = h;
        b[0] = 0;
        rejected(b);
        b = h;
        b[8] = 0;
        b[9] = 0;
        rejected(b);
        b = h;
        b[16] = 0;
        rejected(b);
        b = h;
        b[20] = 255;
        b[21] = 255;
        rejected(b);
        b = h;
        b[24] = 255;
        b[25] = 255;
        b[26] = 255;
        b[27] = 127;
        rejected(b);
        b = h;
        b[28] = 0;
        rejected(b);
        b = h;
        b[32] = 1;
        rejected(b);
        b = h;
        b.pop_back();
        rejected(b);
        b = h;
        b.push_back(0);
        rejected(b);
        b = r;
        b[24] -= 2;
        rejected(b);
        std::cout << "2 valid fixtures and 12 malformed fixture scenarios passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}

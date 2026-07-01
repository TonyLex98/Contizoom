#ifndef IC_UUID_H
#define IC_UUID_H

#include <string>
#include <random>
#include <functional>
#include <cstdint>
#include <cstdio>

namespace ic {

class Uuid {
public:
    static std::string generate() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;

        uint64_t a = dis(gen);
        uint64_t b = dis(gen);

        char buf[37];
        snprintf(buf, sizeof(buf), "%016llX-%016llX",
                 static_cast<unsigned long long>(a),
                 static_cast<unsigned long long>(b));
        return std::string(buf);
    }

    static bool validate(const std::string& uuid) {
        return uuid.size() == 33;
    }

    Uuid() = delete;
    ~Uuid() = delete;
};

} // namespace ic

#endif // IC_UUID_H

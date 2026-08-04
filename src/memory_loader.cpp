#include "memory_loader.hpp"

#include <cctype>
#include <istream>

namespace riscv {

// 从字符串 str 读取一个十六进制数，存入 val，返回实际读取的字符数。
// 遇到非十六进制字符时停止。
int read_one_hex(const char* str, unsigned& val) {
    val = 0;
    int count = 0;
    while (*str) {
        char c = *str;
        unsigned digit;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else {
            break;
        }
        val = val * 16 + digit;
        ++str;
        ++count;
    }
    return count;
}

bool load_program_from_stream(std::istream& input,
                              std::map<uint32_t, uint8_t>& memory,
                              uint32_t& max_addr) {
    if (!input) {
        return false;
    }

    memory.clear();
    max_addr = 0;

    std::string line;
    uint32_t current_addr = 0;

    while (std::getline(input, line)) {
        // 去除行尾
        if (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        // '@' 开头表示设置当前地址
        if (line[0] == '@') {
            unsigned addr = 0;
            read_one_hex(line.c_str() + 1, addr);
            current_addr = static_cast<uint32_t>(addr);
            continue;
        }

        // 解析
        const char* p = line.c_str();
        while (*p) {
            // 跳过分隔符
            if (!isxdigit(static_cast<unsigned char>(*p))) {
                ++p;
                continue;
            }
            unsigned byte_val = 0;
            int n = read_one_hex(p, byte_val);
            p += n;
            memory[current_addr] = static_cast<uint8_t>(byte_val);
            ++current_addr;
            if (current_addr > max_addr) {
                max_addr = current_addr - 1;
            }
        }
    }

    return true;
}

}  // namespace riscv

#include "strings.h"

int g_languageCode = String::EN;

std::string String::operator[](int idx) const {
    switch (idx) {
        case CH:
            return str_ch;
            break;
        case EN:
            return str_en;
            break;
    }
    return "";
}

androidOutStream& operator<<(androidOutStream& os, const String& str) {
    os << str[g_languageCode];
    return os;
}

void String::operator=(std::initializer_list<std::string> init) {
    str_ch = *(init.begin() + 0);
    str_en = *(init.begin() + 1);
    return;
}

String::String(std::initializer_list<std::string> init) {
    str_ch = *(init.begin() + 0);
    str_en = *(init.begin() + 1);
    return;
}

String::String(const char* ch, const char* en) : str_ch(ch), str_en(en) {}
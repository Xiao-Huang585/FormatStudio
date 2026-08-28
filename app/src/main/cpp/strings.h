#pragma once

#include <string>
#include <initializer_list>

// 向前声明...
class androidOutStream;

// 用于储存从Java端获取的语言
extern int g_languageCode;

// 多语言字符串
class String {
private:
    std::string str_en;
    std::string str_ch;
public:
    static constexpr size_t CH = 0;
    static constexpr size_t EN = 1;
    static constexpr size_t size = 2;

    std::string operator[](int idx) const;
    friend androidOutStream& operator<<(androidOutStream& os, const String& str);

    friend std::ostream& operator<<(std::ostream& os, const String& str);

    /**
     * @brief 使用一系列字符串赋值多语言字符串
     * @param init 语言表  [0]: 中文  [1]: 英文
     */
    void operator=(std::initializer_list<std::string> init);

    /**
     * @brief 使用一系列字符串初始化多语言字符串
     * @param init 语言表  [0]: 中文  [1]: 英文
     */
    String(std::initializer_list<std::string> init);

    /**
     *  @brief 使用统一的字符串初始化多语言字符串
     *  @param str 所有语言统一使用的字符串
     */
    // String(const std::string& str);

    String(const char* ch, const char* en);
};

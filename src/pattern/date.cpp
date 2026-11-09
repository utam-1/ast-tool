#include "pattern/date.hpp"

#include "util/strings.hpp"

#include <array>

namespace docmerge::pattern {

namespace {

constexpr std::array<std::string_view, 12> kShortMonths = {
    "jan", "feb", "mar", "apr", "may", "jun",
    "jul", "aug", "sep", "oct", "nov", "dec"};

constexpr std::array<std::string_view, 12> kFullMonths = {
    "january", "february", "march",     "april",   "may",      "june",
    "july",    "august",   "september", "october", "november", "december"};

bool isDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}

bool isLeapYear(int y) noexcept {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

bool isValidCalendarDate(int year, int month, int day) noexcept {
    if (year < 1000 || year > 9999) {
        return false;
    }
    if (month < 1 || month > 12) {
        return false;
    }
    constexpr int kDaysInMonth[12] = {31, 28, 31, 30, 31, 30,
                                      31, 31, 30, 31, 30, 31};
    int maxDay = kDaysInMonth[month - 1];
    if (month == 2 && isLeapYear(year)) {
        maxDay = 29;
    }
    return day >= 1 && day <= maxDay;
}

}  // namespace

bool compileDateFormat(std::string_view formatStr, DateFormat& outFormat, std::string& outError) {
    outFormat.tokens.clear();
    outFormat.rawFormat = std::string(formatStr);
    if (formatStr.empty()) {
        outError = "empty date() format";
        return false;
    }

    size_t i = 0;
    while (i < formatStr.size()) {
        const std::string_view rem = formatStr.substr(i);
        if (util::startsWith(rem, "YYYY")) {
            outFormat.tokens.push_back(DateToken{DateTokenKind::Year4, ""});
            i += 4;
        } else if (util::startsWith(rem, "Month")) {
            outFormat.tokens.push_back(DateToken{DateTokenKind::MonthFull, ""});
            i += 5;
        } else if (util::startsWith(rem, "Mon")) {
            outFormat.tokens.push_back(DateToken{DateTokenKind::MonthShort, ""});
            i += 3;
        } else if (util::startsWith(rem, "MM")) {
            outFormat.tokens.push_back(DateToken{DateTokenKind::Month2, ""});
            i += 2;
        } else if (util::startsWith(rem, "DD")) {
            outFormat.tokens.push_back(DateToken{DateTokenKind::Day2, ""});
            i += 2;
        } else if (rem[0] == 'D') {
            outFormat.tokens.push_back(DateToken{DateTokenKind::DayFlex, ""});
            i += 1;
        } else {
            if (!outFormat.tokens.empty() &&
                outFormat.tokens.back().kind == DateTokenKind::Literal) {
                outFormat.tokens.back().literal.push_back(rem[0]);
            } else {
                outFormat.tokens.push_back(
                    DateToken{DateTokenKind::Literal, std::string(1, rem[0])});
            }
            ++i;
        }
    }
    return true;
}

bool matchDateAt(const DateFormat& fmt,
                 std::string_view line,
                 size_t& pos,
                 ParsedDate& outDate) {
    size_t cur = pos;
    int year = 1970;
    int month = 1;
    int day = 1;

    for (const auto& tok : fmt.tokens) {
        switch (tok.kind) {
            case DateTokenKind::Year4: {
                if (cur + 4 > line.size()) {
                    return false;
                }
                int val = 0;
                for (size_t k = 0; k < 4; ++k) {
                    if (!isDigit(line[cur + k])) {
                        return false;
                    }
                    val = val * 10 + (line[cur + k] - '0');
                }
                year = val;
                cur += 4;
                break;
            }
            case DateTokenKind::Month2: {
                if (cur + 2 > line.size() || !isDigit(line[cur]) || !isDigit(line[cur + 1])) {
                    return false;
                }
                month = (line[cur] - '0') * 10 + (line[cur + 1] - '0');
                cur += 2;
                break;
            }
            case DateTokenKind::Day2: {
                if (cur + 2 > line.size() || !isDigit(line[cur]) || !isDigit(line[cur + 1])) {
                    return false;
                }
                day = (line[cur] - '0') * 10 + (line[cur + 1] - '0');
                cur += 2;
                break;
            }
            case DateTokenKind::DayFlex: {
                if (cur >= line.size() || !isDigit(line[cur])) {
                    return false;
                }
                day = line[cur] - '0';
                ++cur;
                if (cur < line.size() && isDigit(line[cur])) {
                    day = day * 10 + (line[cur] - '0');
                    ++cur;
                }
                break;
            }
            case DateTokenKind::MonthShort: {
                if (cur + 3 > line.size()) {
                    return false;
                }
                const std::string sub = util::toLowerAscii(line.substr(cur, 3));
                int found = 0;
                for (size_t m = 0; m < kShortMonths.size(); ++m) {
                    if (sub == kShortMonths[m]) {
                        found = static_cast<int>(m + 1);
                        break;
                    }
                }
                if (found == 0) {
                    return false;
                }
                month = found;
                cur += 3;
                break;
            }
            case DateTokenKind::MonthFull: {
                int found = 0;
                size_t matchedLen = 0;
                for (size_t m = 0; m < kFullMonths.size(); ++m) {
                    const auto name = kFullMonths[m];
                    if (cur + name.size() <= line.size() &&
                        util::toLowerAscii(line.substr(cur, name.size())) == name) {
                        found = static_cast<int>(m + 1);
                        matchedLen = name.size();
                        break;
                    }
                }
                if (found == 0) {
                    return false;
                }
                month = found;
                cur += matchedLen;
                break;
            }
            case DateTokenKind::Literal: {
                if (cur + tok.literal.size() > line.size() ||
                    line.substr(cur, tok.literal.size()) != tok.literal) {
                    return false;
                }
                cur += tok.literal.size();
                break;
            }
        }
    }

    if (!isValidCalendarDate(year, month, day)) {
        return false;
    }

    outDate = ParsedDate{year, month, day};
    pos = cur;
    return true;
}

int parseDateStringToInt(std::string_view text) {
    static const std::array<std::string_view, 5> kCandidateFormats = {
        "YYYY-MM-DD",
        "YYYY/MM/DD",
        "YYYY.MM.DD",
        "Mon D, YYYY",
        "Month D, YYYY"};

    for (auto fmtStr : kCandidateFormats) {
        DateFormat fmt;
        std::string err;
        if (!compileDateFormat(fmtStr, fmt, err)) {
            continue;
        }
        size_t pos = 0;
        ParsedDate pd;
        if (matchDateAt(fmt, text, pos, pd) && pos == text.size()) {
            return pd.toSortableInt();
        }
    }
    return -1;
}

}  // namespace docmerge::pattern

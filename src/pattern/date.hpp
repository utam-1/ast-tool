#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace docmerge::pattern {

enum class DateTokenKind {
    Year4,      // YYYY
    Month2,     // MM (01..12)
    Day2,       // DD (01..31)
    DayFlex,    // D  (1..31, 1 or 2 digits)
    MonthShort, // Mon (Jan..Dec)
    MonthFull,  // Month (January..December)
    Literal     // Exact literal characters (e.g. "-", "/", ", ")
};

struct DateToken {
    DateTokenKind kind = DateTokenKind::Literal;
    std::string literal;
};

struct DateFormat {
    std::vector<DateToken> tokens;
    std::string rawFormat;
};

struct ParsedDate {
    int year = 0;
    int month = 0;
    int day = 0;

    int toSortableInt() const noexcept {
        return year * 10000 + month * 100 + day;
    }
};

// Compiles a format string like "YYYY-MM-DD" or "Mon D, YYYY".
bool compileDateFormat(std::string_view formatStr, DateFormat& outFormat, std::string& outError);

// Attempts to match a date at `line[pos..]`. On success, advances `pos` and populates `outDate`.
bool matchDateAt(const DateFormat& fmt,
                 std::string_view line,
                 size_t& pos,
                 ParsedDate& outDate);

// Parses a previously captured date string into a sortable integer (YYYYMMDD), or returns -1 on failure.
int parseDateStringToInt(std::string_view text);

}  // namespace docmerge::pattern

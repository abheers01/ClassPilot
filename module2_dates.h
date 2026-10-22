#ifndef CLASSPILOT_MODULE2_DATES_H
#define CLASSPILOT_MODULE2_DATES_H

#include "module1_utilities.h"

// ============================================================================
// 2. DATE SUPPORT
// ============================================================================

// A simple date structure used throughout the application.
struct Date
{
    int day = 1;
    int month = 1;
    int year = 2026;
};

// Two dates are equal when their day, month, and year are all equal.
bool operator==(const Date& left, const Date& right)
{
    return left.day == right.day
        && left.month == right.month
        && left.year == right.year;
}

// Dates are compared chronologically, starting with the year.
bool operator<(const Date& left, const Date& right)
{
    if (left.year != right.year)
    {
        return left.year < right.year;
    }

    if (left.month != right.month)
    {
        return left.month < right.month;
    }

    return left.day < right.day;
}

bool operator<=(const Date& left, const Date& right)
{
    return left < right || left == right;
}

bool operator>(const Date& left, const Date& right)
{
    return right < left;
}

bool operator>=(const Date& left, const Date& right)
{
    return right < left || left == right;
}

// Leap years have an extra day in February.
bool isLeapYear(int year)
{
    return (year % 400 == 0)
        || (year % 4 == 0 && year % 100 != 0);
}

// Return the number of days in a month, or zero for an invalid month.
int daysInMonth(int month, int year)
{
    static const int days[] =
    {
        0, 31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (month < 1 || month > 12)
    {
        return 0;
    }

    if (month == 2 && isLeapYear(year))
    {
        return 29;
    }

    return days[month];
}

// Validate the full calendar date, including leap years.
bool validDate(const Date& date)
{
    if (date.year < 2000 || date.year > 9999)
    {
        return false;
    }

    if (date.month < 1 || date.month > 12)
    {
        return false;
    // Keep the return value explicit so the caller can handle this result.
    }

    if (date.day < 1 || date.day > daysInMonth(date.month, date.year))
    {
        return false;
    }

    return true;
}

// Return the next calendar day. The caller should pass a valid date.
Date nextDate(Date date)
{
    ++date.day;

    if (date.day > daysInMonth(date.month, date.year))
    {
        date.day = 1;
        ++date.month;

        if (date.month > 12)
        {
            date.month = 1;
            ++date.year;
        }
    }

    return date;
}

// Format a date for people to read: DD/MM/YYYY.
string dateText(const Date& date)
{
    ostringstream output;

    output << setfill('0')
           << setw(2) << date.day << '/'
           << setw(2) << date.month << '/'
           << setw(4) << date.year;

    return output.str();
}

// Format a date as YYYY-MM-DD so dates sort chronologically.
string dateSql(const Date& date)
{
    ostringstream output;

    output << setfill('0')
           << setw(4) << date.year << '-'
           << setw(2) << date.month << '-'
           << setw(2) << date.day;

    return output.str();
    // Keep the return value explicit so the caller can handle this result.
}

// Parse DD/MM/YYYY strictly. Invalid separators or trailing text are rejected.
bool parseDate(const string& rawText, Date& date)
{
    const string text = trim(rawText);

    if (text.size() != 10)
    {
        return false;
    }

    if (text[2] != '/' || text[5] != '/')
    {
        return false;
    // Keep the return value explicit so the caller can handle this result.
    }

    for (size_t i = 0; i < text.size(); ++i)
    {
        if (i == 2 || i == 5)
        {
            continue;
        }

        if (!isdigit(static_cast<unsigned char>(text[i])))
        {
            return false;
    // Keep the return value explicit so the caller can handle this result.
        }
    }

    try
    {
        date.day = stoi(text.substr(0, 2));
        date.month = stoi(text.substr(3, 2));
        date.year = stoi(text.substr(6, 4));
    }
    catch (...)
    {
        return false;
    // Keep the return value explicit so the caller can handle this result.
    }

    return validDate(date);
}

// Read and validate a date from the keyboard.
Date inputDate(const string& message)
{
    while (true)
    {
        cout << message << " (DD/MM/YYYY): ";

        string value;
        if (!(cin >> value))
        {
            cout << "\nNo input received. Using the default date.\n";
            return Date();
        }

        Date date;

        if (parseDate(value, date))
        {
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            return date;
        }

        cout << "That date does not look valid. Please try again.\n";
    }
}

// Return Sunday = 0, Monday = 1, ..., Saturday = 6.
int dayOfWeek(const Date& date)
{
    int month = date.month;
    int year = date.year;

    if (month < 3)
    {
        month += 12;
        --year;
    }

    const int yearPart = year % 100;
    const int century = year / 100;

    const int value =
        (date.day
         + (13 * (month + 1)) / 5
         + yearPart
         + yearPart / 4
         + century / 4
         + 5 * century) % 7;

    return (value + 6) % 7;
}

string weekdayName(int value)
{
    static const string names[] =
    {
        "Sunday", "Monday", "Tuesday", "Wednesday",
        "Thursday", "Friday", "Saturday"
    };

    if (value < 0 || value > 6)
    {
        return "Unknown";
    }

    return names[value];
    // Keep the return value explicit so the caller can handle this result.
}

// Convert the database's ISO date back into a Date object.
bool parseSqlDate(const string& value, Date& date)
{
    if (value.size() != 10 || value[4] != '-' || value[7] != '-')
    {
        return false;
    }

    try
    {
        date.year = stoi(value.substr(0, 4));
        date.month = stoi(value.substr(5, 2));
        date.day = stoi(value.substr(8, 2));
    }
    catch (...)
    {
        return false;
    }

    return validDate(date);
}


#endif // CLASSPILOT_MODULE2_DATES_H

#ifndef CLASSPILOT_MODULE1_UTILITIES_H
#define CLASSPILOT_MODULE1_UTILITIES_H

/*
    CLASS PILOT
    Your Smart Class Attendance Assistant

    Project Based Learning - Data Structures and C++
    Academic Session: 2026-27

    This program helps a student plan attendance by recording subjects,
    creating a weekly timetable, marking holidays, and calculating the
    likely effect of future absences.

    Data structures demonstrated:
      vector         - stores activities, timetable entries, holidays,
                       and attendance history in memory
      unordered_map  - finds an activity quickly by its code
      queue          - processes upcoming sessions in date/time order
      priority_queue - lists activities from highest attendance risk to lowest
      sort           - orders timetable entries and reports

    A standard C++ text file stores records so they can be loaded after restarting.

    Build with a standard C++11 or newer compiler (no third-party library required):
      g++ main.cpp -std=c++11 -Wall -Wextra -pedantic -o class_pilot
*/

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include <sstream>
#include <limits>
#include <cmath>
#include <ctime>
#include <cctype>
#include <fstream>

using namespace std;

// ============================================================================
// 1. SMALL UTILITY FUNCTIONS
// ============================================================================

// Lightweight quoted-string helpers keep the file format compatible with
// C++11 compilers, where std::quoted is not available.
struct QuotedOutput
{
    const string& value;
    explicit QuotedOutput(const string& text) : value(text) {}
};

struct QuotedInput
{
    string& value;
    explicit QuotedInput(string& text) : value(text) {}
};

QuotedOutput quoted(const string& value)
{
    return QuotedOutput(value);
}

QuotedInput quoted(string& value)
{
    return QuotedInput(value);
}

ostream& operator<<(ostream& out, const QuotedOutput& text)
{
    out << '"';
    for (string::const_iterator it = text.value.begin(); it != text.value.end(); ++it)
    {
        if (*it == '\\' || *it == '"') out << '\\';
        out << *it;
    }
    out << '"';
    return out;
}

istream& operator>>(istream& in, const QuotedInput& text)
{
    in >> ws;
    if (!in) return in;

    text.value.clear();
    if (in.peek() != '"')
    {
        in >> text.value;
        return in;
    }

    in.get(); // opening quote
    char ch;
    bool escaped = false;
    while (in.get(ch))
    {
        if (escaped)
        {
            text.value += ch;
            escaped = false;
        }
        else if (ch == '\\')
        {
            escaped = true;
        }
        else if (ch == '"')
        {
            return in;
        }
        else
        {
            text.value += ch;
        }
    }

    in.setstate(ios::failbit); // closing quote was missing
    return in;
}

// Remove spaces, tabs, and line breaks from the beginning and end of text.
string trim(const string& text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");

    if (first == string::npos)
    {
        return "";
    }

    const size_t last = text.find_last_not_of(" \t\r\n");

    return text.substr(first, last - first + 1);
}

// Convert a copy of the text to lowercase. This is useful for comparisons.
string lowerCopy(string text)
{
    for (char& ch : text)
    {
        ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    }

    return text;
}

// Clear a failed input operation and discard the rest of the current line.
void clearInputLine()
{
    cin.clear();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
}

// Wait for the user to press Enter before showing the menu again.
void pauseScreen()
{
    cout << "\nPress ENTER to continue...";

    string ignoredLine;
    getline(cin, ignoredLine);
}

// Print a horizontal separator for reports and menus.
void line(char ch = '-', int count = 72)
{
    cout << string(count, ch) << '\n';
}

// Print a clear title at the top of a screen.
void title(const string& text)
{
    cout << '\n';
    line('=');
    cout << ' ' << text << '\n';
    line('=');
}

// Read an integer safely and repeat until it is inside the requested range.
int readInteger(const string& message, int minimum, int maximum)
{
    while (true)
    {
        cout << message;

        int value = 0;

        if (cin >> value)
        {
            if (value >= minimum && value <= maximum)
            {
                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                return value;
            }

            cout << "Please enter a whole number from "
                 << minimum << " to " << maximum << ".\n";
            clearInputLine();
            continue;
        }

        // Online compilers may provide no interactive input. Treat EOF as
        // a request to exit instead of retrying forever on a failed stream.
        if (cin.eof())
        {
            cout << "\nNo input received. Exiting safely.\n";
            return 0;
        }

        cout << "Please enter a valid whole number.\n";
        clearInputLine();
    }
}

// Read a decimal value safely and keep it inside the requested range.
double readDecimal(const string& message, double minimum, double maximum)
{
    while (true)
    {
        cout << message;

        double value = 0.0;

        if (cin >> value)
        {
            if (value >= minimum && value <= maximum)
            {
                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                return value;
            }

            cout << "Please enter a number from "
                 << minimum << " to " << maximum << ".\n";
            clearInputLine();
            continue;
        }

        if (cin.eof())
        {
            cout << "\nNo input received. Using the minimum value.\n";
            return minimum;
        }

        cout << "Please enter a valid number.\n";
        clearInputLine();
    }
}

// Read a complete line, including spaces, and remove outside whitespace.
string readText(const string& message)
{
    cout << message;

    string value;

    if (!getline(cin >> ws, value))
    {
        cout << "\nNo input received.\n";
        return string();
    }

    return trim(value);
}


#endif // CLASSPILOT_MODULE1_UTILITIES_H

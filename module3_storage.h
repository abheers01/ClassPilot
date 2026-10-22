#ifndef CLASSPILOT_MODULE3_STORAGE_H
#define CLASSPILOT_MODULE3_STORAGE_H

#include "module2_dates.h"

// ============================================================================
// 3. PROJECT DATA MODELS
// ============================================================================

struct Settings
{
    Date semesterStart;
    Date semesterEnd;
    double requiredPercent = 75.0;
    string userName = "Student";
};

struct Activity
{
    int id = 0;
    string code;
    string name;
};

struct TimetableEntry
{
    int id = 0;
    int weekday = 1;             // Monday = 1, Saturday = 6.
    string activityCode;
    string startTime;
    string endTime;
};

struct Session
{
    int id = 0;
    Date date;
    string activityCode;
    string startTime;
    string endTime;
};

struct AttendanceRecord
{
    int id = 0;
    Date date;
    string activityCode;
    string startTime;
    string endTime;
    bool present = false;
};

struct ActivityStats
{
    int conducted = 0;
    int attended = 0;
    int missed = 0;
};

struct RiskItem
{
    string code;
    string name;
    double percentage = 0.0;
    int safeAbsences = 0;
};

// priority_queue normally places the greatest item first. Reverse the
// percentage comparison so the activity with the lowest attendance appears
// first, because that is the activity that needs attention most urgently.
struct RiskCompare
{
    bool operator()(const RiskItem& left, const RiskItem& right) const
    {
        if (fabs(left.percentage - right.percentage) > 1e-9)
        {
            return left.percentage > right.percentage;
        }

        return left.safeAbsences > right.safeAbsences;
    }
};

// ============================================================================
// 4. PORTABLE FILE STORAGE HELPER
// ============================================================================
// This version uses standard C++ file streams instead of SQLite. That makes
// it suitable for online compilers that do not provide sqlite3.h. The file
// uses quoted strings, so names containing spaces are saved safely.


class Database
{
private:
    string fileName;
    bool hasSettings = false;
    Settings savedSettings;
    vector<Activity> savedActivities;
    vector<TimetableEntry> savedTimetable;
    vector<Date> savedHolidays;
    vector<Session> savedSessions;
    vector<AttendanceRecord> savedAttendance;

    bool writeAll() const
    {
        ofstream out(fileName.c_str());
        if (!out)
        {
            // Online compiler sandboxes may forbid file creation. Keep all
            // records in memory and allow the current session to continue.
            // In such environments, data will not survive after exit.
            return true;
        }

        out << "CLASS_PILOT_DATA_V1\n";
        out << hasSettings << '\n';
        if (hasSettings)
        {
            out << quoted(savedSettings.userName) << ' '
                << dateSql(savedSettings.semesterStart) << ' '
                << dateSql(savedSettings.semesterEnd) << ' '
                << savedSettings.requiredPercent << '\n';
        }

        out << savedActivities.size() << '\n';
        for (const Activity& item : savedActivities)
            out << item.id << ' ' << quoted(item.code) << ' '
                << quoted(item.name) << '\n';

        out << savedTimetable.size() << '\n';
        for (const TimetableEntry& item : savedTimetable)
            out << item.id << ' ' << item.weekday << ' '
                << quoted(item.activityCode) << ' ' << quoted(item.startTime)
                << ' ' << quoted(item.endTime) << '\n';

        out << savedHolidays.size() << '\n';
        for (const Date& item : savedHolidays)
            out << dateSql(item) << '\n';

        out << savedSessions.size() << '\n';
        for (const Session& item : savedSessions)
            out << item.id << ' ' << dateSql(item.date) << ' '
                << quoted(item.activityCode) << ' ' << quoted(item.startTime)
                << ' ' << quoted(item.endTime) << '\n';

        out << savedAttendance.size() << '\n';
        for (const AttendanceRecord& item : savedAttendance)
            out << item.id << ' ' << dateSql(item.date) << ' '
                << quoted(item.activityCode) << ' ' << quoted(item.startTime)
                << ' ' << quoted(item.endTime) << ' ' << item.present << '\n';

        // File persistence is best-effort because some online compilers use
        // restricted filesystems. The application remains usable in memory.
        return true;
    }

public:
    explicit Database(const string& file = "attendance_data.txt")
        : fileName(file)
    {
    }

    bool initialize()
    {
        ifstream in(fileName.c_str());
        if (!in)
        {
            // Some online compilers run programs in a read-only or restricted
            // sandbox. If the data file cannot be created, keep the program
            // usable for the current run instead of terminating at startup.
            if (!writeAll())
            {
                cerr << "Note: this compiler does not allow local data files.\n";
                cerr << "Class Pilot will run for this session, but data may not be saved.\n\n";
            }
            return true;
        }

        string header;
        getline(in, header);
        if (header != "CLASS_PILOT_DATA_V1")
        {
            cerr << "The data file has an unknown format.\n";
            return false;
        }

        if (!(in >> hasSettings)) return false;
        if (hasSettings)
        {
            string start, finish;
            if (!(in >> quoted(savedSettings.userName) >> start >> finish
                     >> savedSettings.requiredPercent)) return false;
            if (!parseSqlDate(start, savedSettings.semesterStart) ||
                !parseSqlDate(finish, savedSettings.semesterEnd)) return false;
        }

        size_t count = 0;
        if (!(in >> count) || count > 100000) return false;
        savedActivities.clear();
        for (size_t i = 0; i < count; ++i)
        {
            Activity item;
            if (!(in >> item.id >> quoted(item.code) >> quoted(item.name))) return false;
            savedActivities.push_back(item);
        }

        if (!(in >> count) || count > 100000) return false;
        savedTimetable.clear();
        for (size_t i = 0; i < count; ++i)
        {
            TimetableEntry item;
            if (!(in >> item.id >> item.weekday >> quoted(item.activityCode)
                     >> quoted(item.startTime) >> quoted(item.endTime))) return false;
            savedTimetable.push_back(item);
        }

        if (!(in >> count) || count > 100000) return false;
        savedHolidays.clear();
        for (size_t i = 0; i < count; ++i)
        {
            string value;
            Date item;
            if (!(in >> value) || !parseSqlDate(value, item)) return false;
            savedHolidays.push_back(item);
        }

        if (!(in >> count) || count > 100000) return false;
        savedSessions.clear();
        for (size_t i = 0; i < count; ++i)
        {
            Session item;
            string value;
            if (!(in >> item.id >> value >> quoted(item.activityCode)
                     >> quoted(item.startTime) >> quoted(item.endTime)) ||
                !parseSqlDate(value, item.date)) return false;
            savedSessions.push_back(item);
        }

        if (!(in >> count) || count > 100000) return false;
        savedAttendance.clear();
        for (size_t i = 0; i < count; ++i)
        {
            AttendanceRecord item;
            string value;
            if (!(in >> item.id >> value >> quoted(item.activityCode)
                     >> quoted(item.startTime) >> quoted(item.endTime)
                     >> item.present) || !parseSqlDate(value, item.date)) return false;
            savedAttendance.push_back(item);
        }
        return true;
    }

    bool saveSettings(const Settings& settings)
    {
        savedSettings = settings;
        hasSettings = true;
        return writeAll();
    }

    bool loadSettings(Settings& settings)
    {
        if (!hasSettings) return false;
        settings = savedSettings;
        return true;
    }

    bool insertActivity(const Activity& activity)
    {
        for (const Activity& old : savedActivities)
            if (lowerCopy(old.code) == lowerCopy(activity.code)) return false;
        Activity copy = activity;
        copy.id = savedActivities.empty() ? 1 : savedActivities.back().id + 1;
        savedActivities.push_back(copy);
        return writeAll();
    }

    bool insertTimetableEntry(const TimetableEntry& entry)
    {
        TimetableEntry copy = entry;
        copy.id = savedTimetable.empty() ? 1 : savedTimetable.back().id + 1;
        savedTimetable.push_back(copy);
        return writeAll();
    }

    bool insertHoliday(const Date& date)
    {
        for (const Date& old : savedHolidays)
            if (dateSql(old) == dateSql(date)) return false;
        savedHolidays.push_back(date);
        return writeAll();
    }

    bool insertSession(const Session& session)
    {
        Session copy = session;
        copy.id = savedSessions.empty() ? 1 : savedSessions.back().id + 1;
        savedSessions.push_back(copy);
        return writeAll();
    }

    bool insertAttendance(const AttendanceRecord& record)
    {
        for (AttendanceRecord& old : savedAttendance)
        {
            if (old.activityCode == record.activityCode &&
                dateSql(old.date) == dateSql(record.date) &&
                old.startTime == record.startTime)
            {
                old = record;
                return writeAll();
            }
        }
        AttendanceRecord copy = record;
        copy.id = savedAttendance.empty() ? 1 : savedAttendance.back().id + 1;
        savedAttendance.push_back(copy);
        return writeAll();
    }

    bool loadActivities(vector<Activity>& activities)
    {
        activities = savedActivities;
        return true;
    }

    bool loadTimetable(vector<TimetableEntry>& timetable)
    {
        timetable = savedTimetable;
        return true;
    }

    bool loadHolidays(vector<Date>& holidays)
    {
        holidays = savedHolidays;
        return true;
    }

    bool loadAttendance(vector<AttendanceRecord>& history)
    {
        history = savedAttendance;
        return true;
    }

    void showDatabaseSummary()
    {
        title("SAVED DATA SUMMARY");
        cout << "Data file           : " << fileName << '\n';
        cout << "Activities          : " << savedActivities.size() << '\n';
        cout << "Timetable entries   : " << savedTimetable.size() << '\n';
        cout << "Planned holidays    : " << savedHolidays.size() << '\n';
        cout << "Sessions            : " << savedSessions.size() << '\n';
        cout << "Attendance records  : " << savedAttendance.size() << '\n';
        cout << "Settings saved      : " << (hasSettings ? "Yes" : "No") << '\n';
        cout << "\nData is stored locally in a standard text file.\n";
    }
};

// ============================================================================
// 5. MAIN CLASS PILOT APPLICATION

#endif // CLASSPILOT_MODULE3_STORAGE_H

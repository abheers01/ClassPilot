#include "module3_storage.h"

// ============================================================================

class ClassPilot
{
private:
    Settings settings;
    bool configured = false;

    vector<Activity> activities;
    unordered_map<string, int> activityIndex;
    vector<TimetableEntry> timetable;
    vector<Date> holidays;
    vector<AttendanceRecord> attendanceHistory;

    queue<Session> upcomingQueue;
    priority_queue<RiskItem, vector<RiskItem>, RiskCompare> riskQueue;

    Database database;

    // Rebuild the lookup map after activities have been loaded from storage.
    void rebuildActivityIndex()
    {
        activityIndex.clear();

        for (size_t i = 0; i < activities.size(); ++i)
        {
            activityIndex[activities[i].code] = static_cast<int>(i);
        }
    }

    Activity* findActivity(const string& code)
    {
        const auto found = activityIndex.find(code);

        if (found == activityIndex.end())
        {
            return nullptr;
        }

        const int index = found->second;

        if (index < 0 || index >= static_cast<int>(activities.size()))
        {
            return nullptr;
        }

        return &activities[index];
    }

    const Activity* findActivity(const string& code) const
    {
        const auto found = activityIndex.find(code);

        if (found == activityIndex.end())
        {
            return nullptr;
        }

        const int index = found->second;

        if (index < 0 || index >= static_cast<int>(activities.size()))
        {
            return nullptr;
        }

        return &activities[index];
    }

    bool isHoliday(const Date& date) const
    {
        return find(holidays.begin(), holidays.end(), date) != holidays.end();
    }

    bool insideSemester(const Date& date) const
    {
        return configured
            && date >= settings.semesterStart
            && date <= settings.semesterEnd;
    }

    ActivityStats statsFor(const string& code) const
    {
        ActivityStats stats;

        for (const AttendanceRecord& record : attendanceHistory)
        {
            if (record.activityCode != code)
            {
                continue;
            }

            ++stats.conducted;

            if (record.present)
            {
                ++stats.attended;
            }
            else
            {
                ++stats.missed;
            }
        }

        return stats;
    }

    double percentage(const ActivityStats& stats) const
    {
        if (stats.conducted == 0)
        {
            return 0.0;
        }

        return 100.0 * static_cast<double>(stats.attended) / stats.conducted;
    }

    // Calculate how many additional classes can be missed without dropping
    // below the minimum required percentage. This is a planning estimate.
    int safeAbsences(const ActivityStats& stats) const
    {
        if (stats.conducted == 0)
        {
            return 0;
        }

        const double required = settings.requiredPercent / 100.0;
        int extra = 0;

        while (true)
        {
            const double futurePercent =
                static_cast<double>(stats.attended) / (stats.conducted + extra + 1);

            if (futurePercent + 1e-12 >= required)
            {
                ++extra;
            }
            else
            {
                break;
            }
        }

        return extra;
    }

    // Calculate how many consecutive attended classes are needed to reach
    // the minimum percentage again after falling below it.
    int recoveryClasses(const ActivityStats& stats) const
    {
        if (stats.conducted == 0 || percentage(stats) >= settings.requiredPercent)
        {
            return 0;
        }

        const double required = settings.requiredPercent / 100.0;

        for (int attendedNext = 1; attendedNext <= 100000; ++attendedNext)
        {
            const double futurePercent =
                static_cast<double>(stats.attended + attendedNext)
                / (stats.conducted + attendedNext);

            if (futurePercent + 1e-12 >= required)
            {
                return attendedNext;
            }
        }

        return -1;
    }

    string statusFor(const ActivityStats& stats) const
    {
        if (stats.conducted == 0)
        {
            return "NO DATA";
        }

        const double currentPercent = percentage(stats);

        if (currentPercent < settings.requiredPercent)
        {
            return "BELOW REQUIREMENT";
        }

        const int safe = safeAbsences(stats);

        if (safe == 0)
        {
            return "NO SAFE ABSENCE";
        }

        if (safe <= 2)
        {
            return "HIGH RISK";
        }

        if (currentPercent < settings.requiredPercent + 5.0)
        {
            return "CAUTION";
        }

        return "SAFE";
    }

    vector<TimetableEntry> entriesForWeekday(int weekday) const
    {
        vector<TimetableEntry> result;

        for (const TimetableEntry& entry : timetable)
        {
            if (entry.weekday == weekday)
            {
                result.push_back(entry);
            }
        }

        sort(
            result.begin(),
            result.end(),
            [](const TimetableEntry& left, const TimetableEntry& right)
            {
                if (left.startTime == right.startTime)
                {
                    return left.endTime < right.endTime;
                }

                return left.startTime < right.startTime;
            }
        );

        return result;
    }

    // Generate the classes that would occur in the requested date range.
    // Sundays and planned holidays are skipped in this version.
    vector<Session> generateSessions(const Date& from, const Date& to) const
    {
        vector<Session> result;

        if (!configured || to < from)
        {
            return result;
    // Keep the return value explicit so the caller can handle this result.
        }

        Date current = from;

        while (current <= to)
        {
            if (!isHoliday(current))
            {
                const int weekday = dayOfWeek(current);

                if (weekday != 0)
                {
                    const vector<TimetableEntry> dayEntries = entriesForWeekday(weekday);

                    for (const TimetableEntry& entry : dayEntries)
                    {
                        Session session;
                        session.date = current;
                        session.activityCode = entry.activityCode;
                        session.startTime = entry.startTime;
                        session.endTime = entry.endTime;

                        result.push_back(session);
                    }
                }
            }

            current = nextDate(current);
        }

        sort(
            result.begin(),
            result.end(),
            [](const Session& left, const Session& right)
            {
                if (left.date == right.date)
                {
                    return left.startTime < right.startTime;
                }

                return left.date < right.date;
            }
        );

        return result;
    // Keep the return value explicit so the caller can handle this result.
    }

    bool attendanceAlreadyRecorded(const Session& session) const
    {
        for (const AttendanceRecord& record : attendanceHistory)
        {
            if (record.activityCode == session.activityCode
                && record.date == session.date
                && record.startTime == session.startTime)
            {
                return true;
            }
        }

        return false;
    // Keep the return value explicit so the caller can handle this result.
    }

    bool validTime(const string& time) const
    {
        if (time.size() != 5 || time[2] != ':')
        {
            return false;
        }

        if (!isdigit(static_cast<unsigned char>(time[0]))
            || !isdigit(static_cast<unsigned char>(time[1]))
            || !isdigit(static_cast<unsigned char>(time[3]))
            || !isdigit(static_cast<unsigned char>(time[4])))
        {
            return false;
    // Keep the return value explicit so the caller can handle this result.
        }

        const int hour = stoi(time.substr(0, 2));
        const int minute = stoi(time.substr(3, 2));

        return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
    }

    // Load every saved collection at startup, not just the settings row.
    void loadSavedData()
    {
        Settings loadedSettings;

        if (database.loadSettings(loadedSettings))
        {
            settings = loadedSettings;
            configured = validDate(settings.semesterStart)
                && validDate(settings.semesterEnd)
                && settings.semesterEnd >= settings.semesterStart;
        }

        database.loadActivities(activities);
        database.loadTimetable(timetable);
        database.loadHolidays(holidays);
        database.loadAttendance(attendanceHistory);

        rebuildActivityIndex();
    }

    void configureProject()
    {
        title("SEMESTER SETUP");

        settings.userName = readText("Student name: ");
        settings.semesterStart = inputDate("Semester start date");
        settings.semesterEnd = inputDate("Semester end date");

        while (settings.semesterEnd < settings.semesterStart)
        {
            cout << "End date cannot be before start date.\n";
            settings.semesterEnd = inputDate("Semester end date");
        }

        settings.requiredPercent = readDecimal(
            "Minimum required attendance (%): ",
            1.0,
            100.0
        );

        configured = true;

        if (database.saveSettings(settings))
        {
            cout << "\nSetup saved successfully.\n";
        }
        else
        {
            cout << "\nSetup was changed in memory but could not be saved.\n";
        }

        cout << "The app will use this requirement when it calculates safe absences\n";
        cout << "and future attendance predictions.\n";
    }

    void addActivity()
    {
        title("ADD ACTIVITY / SUBJECT");

        string code = readText("Short code (example CSE201): ");

        if (code.empty())
        {
            cout << "Code cannot be empty.\n";
            return;
        }

        // Activity codes are treated without regard to letter case.
        for (Activity& existing : activities)
        {
            if (lowerCopy(existing.code) == lowerCopy(code))
            {
                cout << "That activity code already exists.\n";
                return;
            }
        }

        string name = readText("Activity name: ");

        if (name.empty())
        {
            cout << "Activity name cannot be empty.\n";
            return;
        }

        Activity activity;
        activity.code = code;
        activity.name = name;

        if (!database.insertActivity(activity))
        {
            cout << "The activity could not be saved to the local data file.\n";
            cout << "Please check whether the code already exists.\n";
            return;
        }

        activity.id = static_cast<int>(activities.size()) + 1;
        activityIndex[activity.code] = static_cast<int>(activities.size());
        activities.push_back(activity);

        cout << "Activity added successfully.\n";
    }

    void listActivities() const
    {
        title("ACTIVITIES");

        if (activities.empty())
        {
            cout << "No activities have been added yet.\n";
            return;
        }

        cout << left << setw(6) << "ID"
             << setw(15) << "Code"
             << "Name\n";

        line();

        for (const Activity& activity : activities)
        {
            cout << left << setw(6) << activity.id
                 << setw(15) << activity.code
                 << activity.name << '\n';
        }
    }

    void addTimetableEntry()
    {
        if (activities.empty())
        {
            cout << "Add at least one activity first.\n";
            return;
        }

        title("ADD TIMETABLE ENTRY");

        cout << "1. Monday\n";
        cout << "2. Tuesday\n";
        cout << "3. Wednesday\n";
        cout << "4. Thursday\n";
        cout << "5. Friday\n";
        cout << "6. Saturday\n";

        const int weekday = readInteger("Day number: ", 1, 6);

        listActivities();

        const string code = readText("Activity code: ");

        if (findActivity(code) == nullptr)
        {
            cout << "Activity code was not found.\n";
            return;
        }

        const string start = readText("Start time (HH:MM): ");
        const string end = readText("End time (HH:MM): ");

        if (!validTime(start) || !validTime(end) || end <= start)
        {
            cout << "Please enter valid times, with the end after the start.\n";
            return;
        }

        TimetableEntry entry;
        entry.weekday = weekday;
        entry.activityCode = code;
        entry.startTime = start;
        entry.endTime = end;

        if (!database.insertTimetableEntry(entry))
        {
            cout << "The timetable entry could not be saved.\n";
            return;
        }

        entry.id = static_cast<int>(timetable.size()) + 1;
        timetable.push_back(entry);

        cout << "Timetable entry added and saved.\n";
        cout << "It will be used when upcoming sessions are generated.\n";
    }

    void showTimetable() const
    {
        title("WEEKLY TIMETABLE");

        if (timetable.empty())
        {
            cout << "No timetable entries yet.\n";
            return;
        }

        for (int day = 1; day <= 6; ++day)
        {
            cout << '\n' << weekdayName(day) << '\n';
            line();

            const vector<TimetableEntry> entries = entriesForWeekday(day);

            if (entries.empty())
            {
                cout << "No class planned.\n";
                continue;
            }

            for (const TimetableEntry& entry : entries)
            {
                const Activity* activity = findActivity(entry.activityCode);

                cout << entry.startTime << " - " << entry.endTime
                     << " | " << entry.activityCode;

                if (activity != nullptr)
                {
                    cout << " (" << activity->name << ')';
                }

                cout << '\n';
            }
        }
    }

    void addHoliday()
    {
        if (!configured)
        {
            cout << "Set the semester dates first.\n";
            return;
        }

        title("ADD HOLIDAY");

        const Date date = inputDate("Holiday date");

        if (!insideSemester(date))
        {
            cout << "That date is outside the configured semester.\n";
            return;
        }

        if (isHoliday(date))
        {
            cout << "That date is already marked as a holiday.\n";
            return;
        }

        if (!database.insertHoliday(date))
        {
            cout << "The holiday could not be saved.\n";
            return;
        }

        holidays.push_back(date);
        sort(holidays.begin(), holidays.end());

        cout << "Holiday added and saved.\n";
        cout << "Upcoming-session generation will skip it.\n";
    }

    void showHolidays() const
    {
        title("PLANNED HOLIDAYS");

        if (holidays.empty())
        {
            cout << "No holidays have been added.\n";
            return;
        }

        for (const Date& date : holidays)
        {
            cout << dateText(date)
                 << " - " << weekdayName(dayOfWeek(date)) << '\n';
        }
    }

    void recordAttendance()
    {
        if (!configured || activities.empty() || timetable.empty())
        {
            cout << "Please configure the semester, activities and timetable first.\n";
            return;
        }

        title("RECORD ATTENDANCE");

        const Date date = inputDate("Class date");

        if (!insideSemester(date))
        {
            cout << "That date is outside the semester.\n";
            return;
        }

        if (isHoliday(date))
        {
            cout << "This date is marked as a holiday. No regular class is recorded.\n";
            return;
        }

        const int weekday = dayOfWeek(date);

        if (weekday == 0)
        {
            cout << "Sunday has no regular timetable entry in this version.\n";
            return;
        }

        const vector<TimetableEntry> entries = entriesForWeekday(weekday);

        if (entries.empty())
        {
            cout << "There are no classes in the timetable for this day.\n";
            return;
        }

        for (const TimetableEntry& entry : entries)
        {
            Session session;
            session.date = date;
            session.activityCode = entry.activityCode;
            session.startTime = entry.startTime;
            session.endTime = entry.endTime;

            const Activity* activity = findActivity(entry.activityCode);

            cout << '\n' << entry.startTime << " - " << entry.endTime
                 << " | " << entry.activityCode;

            if (activity != nullptr)
            {
                cout << " | " << activity->name;
            }

            cout << '\n';

            if (attendanceAlreadyRecorded(session))
            {
                cout << "Already recorded. Skipping this entry.\n";
                continue;
            }

            cout << "1. Present\n";
            cout << "2. Absent\n";

            const int choice = readInteger("Your status: ", 1, 2);

            AttendanceRecord record;
            record.date = date;
            record.activityCode = entry.activityCode;
            record.startTime = entry.startTime;
            record.endTime = entry.endTime;
            record.present = (choice == 1);

            if (!database.insertAttendance(record))
            {
                cout << "Could not save this attendance record.\n";
                continue;
            }

            attendanceHistory.push_back(record);

            cout << (record.present ? "Marked PRESENT.\n" : "Marked ABSENT.\n");
        }

        cout << "\nAttendance update complete.\n";
    }

    void dashboard() const
    {
        title("ATTENDANCE DASHBOARD");

        if (!configured)
        {
            cout << "Semester is not configured yet.\n";
            return;
        }

        cout << "Student : " << settings.userName << '\n';
        cout << "Semester : " << dateText(settings.semesterStart)
             << " to " << dateText(settings.semesterEnd) << '\n';
        cout << "Minimum required : " << fixed << setprecision(2)
             << settings.requiredPercent << "%\n";

        line();

        if (activities.empty())
        {
            cout << "No activities added.\n";
            return;
        }

        cout << left << setw(12) << "Code"
             << setw(24) << "Activity"
             << setw(9) << "Present"
             << setw(9) << "Total"
             << setw(12) << "Percent"
             << setw(10) << "Can Miss"
             << "Status\n";

        line();

        for (const Activity& activity : activities)
        {
            const ActivityStats stats = statsFor(activity.code);

            cout << left << setw(12) << activity.code
                 << setw(24) << activity.name.substr(0, 22)
                 << setw(9) << stats.attended
                 << setw(9) << stats.conducted
                 << setw(12) << fixed << setprecision(2) << percentage(stats)
                 << setw(10) << safeAbsences(stats)
                 << statusFor(stats) << '\n';
        }
    }

    void activityDetails() const
    {
        if (activities.empty())
        {
            cout << "No activities added.\n";
            return;
        }

        title("ACTIVITY DETAILS");
        listActivities();

        const string code = readText("Activity code: ");
        const Activity* activity = findActivity(code);

        if (activity == nullptr)
        {
            cout << "Activity not found.\n";
            return;
        }

        const ActivityStats stats = statsFor(code);
        const double currentPercent = percentage(stats);

        cout << "\nActivity : " << activity->name << '\n';
        cout << "Classes conducted : " << stats.conducted << '\n';
        cout << "Classes attended  : " << stats.attended << '\n';
        cout << "Classes missed    : " << stats.missed << '\n';
        cout << "Attendance        : " << fixed << setprecision(2)
             << currentPercent << "%\n";
        cout << "Minimum required  : " << settings.requiredPercent << "%\n";
        cout << "Status            : " << statusFor(stats) << '\n';

        if (currentPercent >= settings.requiredPercent)
        {
            cout << "Safe absences     : " << safeAbsences(stats) << '\n';
        }
        else
        {
            cout << "Recovery classes  : " << recoveryClasses(stats) << '\n';
        }
    }

    void whatIfSimulator() const
    {
        if (activities.empty())
        {
            cout << "Add activities first.\n";
            return;
        }

        title("WHAT-IF SIMULATOR");
        listActivities();

        const string code = readText("Activity code: ");
        const Activity* activity = findActivity(code);

        if (activity == nullptr)
        {
            cout << "Activity not found.\n";
            return;
        }

        const ActivityStats stats = statsFor(code);

        if (stats.conducted == 0)
        {
            cout << "There is no attendance history for this activity yet.\n";
            return;
        }

        cout << "Current attendance: " << fixed << setprecision(2)
             << percentage(stats) << "%\n";
        cout << "1. Suppose I miss upcoming classes\n";
        cout << "2. Suppose I attend upcoming classes\n";

        const int choice = readInteger("Choose a scenario: ", 1, 2);
        const int count = readInteger("Number of upcoming classes: ", 0, 500);

        int newAttended = stats.attended;
        const int newConducted = stats.conducted + count;

        if (choice == 2)
        {
            newAttended += count;
        }

        double futurePercent = 0.0;

        if (newConducted > 0)
        {
            futurePercent = 100.0 * newAttended / newConducted;
        }

        cout << "\nExpected attendance: " << fixed << setprecision(2)
             << futurePercent << "%\n";

        if (futurePercent >= settings.requiredPercent)
        {
            cout << "Result: the plan stays at or above the requirement.\n";
        }
        else
        {
            cout << "Result: the plan goes below the requirement.\n";

            ActivityStats futureStats;
            futureStats.conducted = newConducted;
            futureStats.attended = newAttended;
            futureStats.missed = newConducted - newAttended;

            cout << "Estimated recovery classes: "
                 << recoveryClasses(futureStats) << '\n';
        }
    }

    void futurePrediction() const
    {
        if (!configured || activities.empty())
        {
            cout << "Configure the project and add activities first.\n";
            return;
        }

        title("FUTURE ATTENDANCE PREDICTION");
        listActivities();

        const string code = readText("Activity code: ");

        if (findActivity(code) == nullptr)
        {
            cout << "Activity not found.\n";
            return;
        }

        const Date from = inputDate("Prediction start");
        const Date to = inputDate("Prediction end");

        if (to < from)
        {
            cout << "The end date cannot be before the start date.\n";
            return;
        }

        const vector<Session> sessions = generateSessions(from, to);
        int futureSessions = 0;

        for (const Session& session : sessions)
        {
            if (session.activityCode == code)
            {
                ++futureSessions;
            }
        }

        const ActivityStats current = statsFor(code);

        cout << "Upcoming sessions for " << code << ": "
             << futureSessions << '\n';
        cout << "1. Attend all of them\n";
        cout << "2. Miss all of them\n";
        cout << "3. Enter how many will be missed\n";

        const int choice = readInteger("Scenario: ", 1, 3);
        int missed = 0;

        if (choice == 2)
        {
            missed = futureSessions;
        }
        else if (choice == 3)
        {
            missed = readInteger("How many will be missed: ", 0, futureSessions);
        }

        const int futurePresent = futureSessions - missed;
        const int total = current.conducted + futureSessions;
        const int present = current.attended + futurePresent;

        const double predicted = total == 0
            ? 0.0
            : 100.0 * present / total;

        cout << "\nCurrent attendance : " << fixed << setprecision(2)
             << percentage(current) << "%\n";
        cout << "Future sessions    : " << futureSessions << '\n';
        cout << "Planned absences   : " << missed << '\n';
        cout << "Expected attendance: " << predicted << "%\n";

        if (predicted >= settings.requiredPercent)
        {
            cout << "Result: ABOVE MINIMUM REQUIREMENT\n";
        }
        else
        {
            cout << "Result: BELOW MINIMUM REQUIREMENT\n";
        }
    }

    void upcomingSessions()
    {
        if (!configured || timetable.empty())
        {
            cout << "Configure the semester and timetable first.\n";
            return;
        }

        title("UPCOMING SESSIONS");

        const Date from = inputDate("Show from");
        const Date to = inputDate("Show until");

        if (to < from)
        {
            cout << "Invalid date range.\n";
            return;
        }

        const vector<Session> sessions = generateSessions(from, to);

        if (sessions.empty())
        {
            cout << "No sessions were found in that range.\n";
            return;
        }

        while (!upcomingQueue.empty())
        {
            upcomingQueue.pop();
        }

        // Sessions are already sorted by date and start time. queue preserves
        // the order in which they are inserted and removed.
        for (const Session& session : sessions)
        {
            upcomingQueue.push(session);
        }

        cout << "\nThe queue contains " << upcomingQueue.size()
             << " upcoming sessions.\n\n";

        while (!upcomingQueue.empty())
        {
            const Session session = upcomingQueue.front();
            upcomingQueue.pop();

            const Activity* activity = findActivity(session.activityCode);

            cout << dateText(session.date)
                 << " | " << session.startTime << '-' << session.endTime
                 << " | " << session.activityCode;

            if (activity != nullptr)
            {
                cout << " | " << activity->name;
            }

            cout << '\n';
        }
    }

    void buildRiskQueue()
    {
        while (!riskQueue.empty())
        {
            riskQueue.pop();
        }

        for (const Activity& activity : activities)
        {
            const ActivityStats stats = statsFor(activity.code);

            if (stats.conducted == 0)
            {
                continue;
            }

            RiskItem item;
            item.code = activity.code;
            item.name = activity.name;
            item.percentage = percentage(stats);
            item.safeAbsences = safeAbsences(stats);

            riskQueue.push(item);
        }
    }

    void riskRanking()
    {
        title("ATTENDANCE RISK RANKING");
        buildRiskQueue();

        if (riskQueue.empty())
        {
            cout << "There is not enough attendance data to rank risk.\n";
            return;
        }

        cout << "The highest-risk activity is shown first.\n\n";

        int rank = 1;

        while (!riskQueue.empty())
        {
            const RiskItem item = riskQueue.top();
            riskQueue.pop();

            cout << rank++ << ". " << item.code
                 << " - " << item.name << '\n';
            cout << "   Attendance : " << fixed << setprecision(2)
                 << item.percentage << "%\n";
            cout << "   Can safely miss: " << item.safeAbsences << "\n\n";
        }
    }

    void sortAttendance() const
    {
        title("SORTED ATTENDANCE");

        vector<pair<string, double>> values;

        for (const Activity& activity : activities)
        {
            values.push_back({activity.code, percentage(statsFor(activity.code))});
        }

        sort(
            values.begin(),
            values.end(),
            [](const pair<string, double>& left, const pair<string, double>& right)
            {
                if (left.second == right.second)
                {
                    return left.first < right.first;
                }

                return left.second < right.second;
            }
        );

        if (values.empty())
        {
            cout << "No activities are available to sort.\n";
            return;
        }

        for (size_t i = 0; i < values.size(); ++i)
        {
            const Activity* activity = findActivity(values[i].first);

            cout << setw(2) << i + 1 << ". " << values[i].first;

            if (activity != nullptr)
            {
                cout << " - " << activity->name;
            }

            cout << " : " << fixed << setprecision(2)
                 << values[i].second << "%\n";
        }
    }

    void searchAttendanceByDate() const
    {
        title("SEARCH ATTENDANCE BY DATE");

        const Date date = inputDate("Date to search");
        bool found = false;

        for (const AttendanceRecord& record : attendanceHistory)
        {
            if (record.date == date)
            {
                found = true;

                cout << record.startTime << '-' << record.endTime
                     << " | " << record.activityCode
                     << " | " << (record.present ? "PRESENT" : "ABSENT")
                     << '\n';
            }
        }

        if (!found)
        {
            cout << "No attendance record was found for that date.\n";
        }
    }

    void searchByActivity() const
    {
        title("SEARCH BY ACTIVITY");

        const string code = readText("Activity code: ");

        if (findActivity(code) == nullptr)
        {
            cout << "Activity not found.\n";
            return;
        }

        bool found = false;

        for (const AttendanceRecord& record : attendanceHistory)
        {
            if (record.activityCode == code)
            {
                found = true;

                cout << dateText(record.date)
                     << " | " << record.startTime << '-' << record.endTime
                     << " | " << (record.present ? "PRESENT" : "ABSENT")
                     << '\n';
            }
        }

        if (!found)
        {
            cout << "No history found for this activity.\n";
        }
    }

    void showHistory() const
    {
        title("ATTENDANCE HISTORY");

        if (attendanceHistory.empty())
        {
            cout << "No attendance has been recorded yet.\n";
            return;
        }

        vector<AttendanceRecord> sortedHistory = attendanceHistory;

        sort(
            sortedHistory.begin(),
            sortedHistory.end(),
            [](const AttendanceRecord& left, const AttendanceRecord& right)
            {
                if (left.date == right.date)
                {
                    return left.startTime < right.startTime;
                }

                return left.date < right.date;
            }
        );

        for (const AttendanceRecord& record : sortedHistory)
        {
            cout << dateText(record.date)
                 << " | " << record.startTime << '-' << record.endTime
                 << " | " << record.activityCode
                 << " | " << (record.present ? "PRESENT" : "ABSENT")
                 << '\n';
        }
    }

    void dailyRealityCheck() const
    {
        if (!configured)
        {
            cout << "Configure the semester first.\n";
            return;
        }

        title("DAILY REALITY CHECK");

        const Date date = inputDate("Date");

        if (!insideSemester(date))
        {
            cout << "That date is outside the semester.\n";
            return;
        }

        if (isHoliday(date))
        {
            cout << "It is a planned holiday, so there are no regular classes.\n";
            return;
        }

        const int weekday = dayOfWeek(date);

        if (weekday == 0)
        {
            cout << "Sunday - no regular classes in this version.\n";
            return;
        }

        const vector<TimetableEntry> entries = entriesForWeekday(weekday);

        if (entries.empty())
        {
            cout << "No scheduled class for this day.\n";
            return;
        }

        cout << dateText(date) << " (" << weekdayName(weekday) << ")\n\n";

        for (const TimetableEntry& entry : entries)
        {
            const ActivityStats stats = statsFor(entry.activityCode);
            const Activity* activity = findActivity(entry.activityCode);

            cout << entry.startTime << '-' << entry.endTime
                 << " | " << entry.activityCode;

            if (activity != nullptr)
            {
                cout << " - " << activity->name;
            }

            cout << '\n';
            cout << "Attendance: " << fixed << setprecision(2)
                 << percentage(stats) << "%\n";

            if (stats.conducted == 0)
            {
                cout << "Suggestion: attend, because there is no history yet.\n";
            }
            else if (percentage(stats) < settings.requiredPercent)
            {
                cout << "Suggestion: attend, because attendance is below the requirement.\n";
                cout << "Recovery classes needed: " << recoveryClasses(stats) << '\n';
            }
            else if (safeAbsences(stats) == 0)
            {
                cout << "Suggestion: attend, because no safe absence remains.\n";
            }
            else
            {
                cout << "Safe absences currently available: "
                     << safeAbsences(stats) << '\n';
            }

            cout << '\n';
        }

        cout << "This is only a planning estimate.\n";
        cout << "The official institutional record remains final.\n";
    }

    void recoveryPlanner() const
    {
        title("RECOVERY PLANNER");

        if (activities.empty())
        {
            cout << "No activities available.\n";
            return;
        }

        listActivities();

        const string code = readText("Activity code: ");

        if (findActivity(code) == nullptr)
        {
            cout << "Activity not found.\n";
            return;
        }

        const ActivityStats stats = statsFor(code);

        if (stats.conducted == 0)
        {
            cout << "No attendance data yet.\n";
            return;
        }

        const double currentPercent = percentage(stats);

        cout << "Current attendance: " << fixed << setprecision(2)
             << currentPercent << "%\n";

        if (currentPercent >= settings.requiredPercent)
        {
            cout << "You are currently at or above the requirement.\n";
            cout << "There is no recovery target right now.\n";
            return;
        }

        const int needed = recoveryClasses(stats);

        cout << "Classes that need to be attended consecutively to reach "
             << settings.requiredPercent << "%: " << needed << '\n';
        cout << "This assumes every one of those future classes is attended.\n";
    }

    void databaseInfo()
    {
        database.showDatabaseSummary();

        cout << "\nTables used by the application:\n";
        cout << " Users       - basic user information\n";
        cout << " Activities  - subjects / training activities\n";
        cout << " Timetable   - weekly class times\n";
        cout << " Holidays    - planned holidays\n";
        cout << " Sessions    - generated class sessions\n";
        cout << " Attendance  - present / absent records\n";
        cout << " Settings    - semester and attendance requirement\n";
    }

    void projectInfo() const
    {
        title("ABOUT CLASS PILOT");

        cout << "Class Pilot is a C++ attendance-planning application.\n\n";
        cout << "It does not replace an official attendance system.\n";
        cout << "Instead, it helps answer a practical question:\n";
        cout << "If I miss a future session, what may happen to my attendance?\n\n";

        cout << "Data structures used in the application:\n";
        cout << " vector         -> attendance, activities and timetable records\n";
        cout << " unordered_map  -> quick activity lookup by code\n";
        cout << " queue          -> upcoming session processing\n";
        cout << " priority_queue -> attendance-risk ranking\n";
        cout << " sort           -> ordered reports\n";
        cout << " loops/search   -> record lookup and statistics\n";
        cout << " text file      -> persistent local storage\n";
    }

    void saveCurrentState()
    {
        bool success = true;

        if (configured && !database.saveSettings(settings))
        {
            success = false;
        }

        // Activities, timetable entries, holidays and attendance are written
        // to the data file when the user adds or records them.
        if (success)
        {
            cout << "Current important records are saved in attendance_data.txt.\n";
        }
        else
        {
            cout << "One or more settings could not be saved.\n";
        }
    }

    void showMenu() const
    {
        cout << "\nMAIN MENU\n";
        line();

        cout << " 1. Configure semester and requirement\n";
        cout << " 2. Add activity / subject\n";
        cout << " 3. Add timetable entry\n";
        cout << " 4. Add planned holiday\n";
        cout << " 5. Record attendance\n";
        cout << " 6. Attendance dashboard\n";
        cout << " 7. Activity details\n";
        cout << " 8. Daily reality check\n";
        cout << " 9. What-if simulator\n";
        cout << "10. Future attendance prediction\n";
        cout << "11. Upcoming sessions\n";
        cout << "12. Attendance-risk priority\n";
        cout << "13. Sort activities by attendance\n";
        cout << "14. Attendance history\n";
        cout << "15. Search history by date\n";
        cout << "16. Search history by activity\n";
        cout << "17. Recovery planner\n";
        cout << "18. View timetable\n";
        cout << "19. View holidays\n";
        cout << "20. View activities\n";
        cout << "21. Database information\n";
        cout << "22. Project information\n";
        cout << "23. Save current state\n";
        cout << " 0. Exit\n";

        line();
    }

public:
    ClassPilot()
        : database("attendance_data.txt")
    {
    }

    void run()
    {
        // File storage is optional in restricted online compiler sandboxes.
        // initialize() loads saved data when available and otherwise starts
        // with a fresh in-memory session.
        database.initialize();

        // Restore all records so saved data is available after a restart.
        loadSavedData();

        cout << '\n';
        line('=');
        cout << " CLASS PILOT\n";
        cout << " Your Smart Class Attendance Assistant\n";
        line('=');
        cout << "A planner for attendance, future classes and what-if checks.\n";

        int choice = -1;

        while (choice != 0)
        {
            if (configured)
            {
                cout << "\nStudent: " << settings.userName
                     << " | Required: " << fixed << setprecision(2)
                     << settings.requiredPercent << "%\n";
            }
            else
            {
                cout << "\nSemester setup is not completed yet.\n";
            }

            showMenu();

            choice = readInteger("Enter your choice: ", 0, 23);

            cout << '\n';

            switch (choice)
            {
                case 1:
                    configureProject();
                    break;

                case 2:
                    addActivity();
                    break;

                case 3:
                    addTimetableEntry();
                    break;

                case 4:
                    addHoliday();
                    break;

                case 5:
                    recordAttendance();
                    break;

                case 6:
                    dashboard();
                    break;

                case 7:
                    activityDetails();
                    break;

                case 8:
                    dailyRealityCheck();
                    break;

                case 9:
                    whatIfSimulator();
                    break;

                case 10:
                    futurePrediction();
                    break;

                case 11:
                    upcomingSessions();
                    break;

                case 12:
                    riskRanking();
                    break;

                case 13:
                    sortAttendance();
                    break;

                case 14:
                    showHistory();
                    break;

                case 15:
                    searchAttendanceByDate();
                    break;

                case 16:
                    searchByActivity();
                    break;

                case 17:
                    recoveryPlanner();
                    break;

                case 18:
                    showTimetable();
                    break;

                case 19:
                    showHolidays();
                    break;

                case 20:
                    listActivities();
                    break;

                case 21:
                    databaseInfo();
                    break;

                case 22:
                    projectInfo();
                    break;

                case 23:
                    saveCurrentState();
                    break;

                case 0:
                    saveCurrentState();
                    cout << "\nThanks for using Class Pilot.\n";
                    cout << "Good luck with your classes!\n";
                    break;
            }

            if (choice != 0)
            {
                pauseScreen();
            }
        }
    }
};

// ============================================================================
// 6. MAIN FUNCTION
// ============================================================================

int main()
{
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    ClassPilot app;

    app.run();

    return 0;
}

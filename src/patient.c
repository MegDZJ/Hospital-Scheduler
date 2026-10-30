#include "patient.h"
#include "conditions.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

void trim_name(char *name)
{
    size_t len = strlen(name);
    size_t start = 0;

    while (len > 0 && isspace((unsigned char)name[len - 1]))
        len--;
    while (start < len && isspace((unsigned char)name[start]))
        start++;
    memmove(name, name + start, len - start);
    name[len - start] = '\0';
}

int validate_patient_input(const PatientInput *in, int earliest_arrival,
                           char *err, size_t errlen)
{
    size_t i, len;
    const Condition *cond;

    len = strnlen(in->name, MAX_NAME_LEN + 1);
    if (len == 0) {
        snprintf(err, errlen, "Patient name is required");
        return 0;
    }
    if (len > MAX_NAME_LEN) {
        snprintf(err, errlen, "Patient name must be at most %d characters",
                 MAX_NAME_LEN);
        return 0;
    }
    for (i = 0; i < len; i++) {
        if ((unsigned char)in->name[i] < 0x20) {
            snprintf(err, errlen, "Patient name contains control characters");
            return 0;
        }
    }
    if (in->age < 0 || in->age > MAX_AGE) {
        snprintf(err, errlen, "Age must be a whole number from 0 to %d",
                 MAX_AGE);
        return 0;
    }
    if (in->gender != GENDER_MALE && in->gender != GENDER_FEMALE &&
        in->gender != GENDER_OTHER) {
        snprintf(err, errlen, "Invalid gender selection");
        return 0;
    }
    cond = get_condition(in->condition_id);
    if (cond == NULL) {
        snprintf(err, errlen, "Unsupported condition");
        return 0;
    }
    if (!check_condition_eligibility(cond, in->age, (int)in->gender,
                                     in->context_confirmed,
                                     err, errlen))
        return 0;
    if (in->arrival_time < 0 || in->arrival_time > LAST_MINUTE_OF_DAY) {
        snprintf(err, errlen,
                 "Arrival time must be within the modeled day (00:00-23:59)");
        return 0;
    }
    if (in->arrival_time < earliest_arrival) {
        char t[8];
        format_time(earliest_arrival, t);
        snprintf(err, errlen,
                 "Arrival time must be at least %d minutes after the current "
                 "simulated time (%s or later)", MIN_ARRIVAL_LEAD_MINUTES, t);
        return 0;
    }
    return 1;
}

int can_transition(PatientStatus from, PatientStatus to)
{
    return (from == PATIENT_SCHEDULED && to == PATIENT_WAITING) ||
           (from == PATIENT_WAITING   && to == PATIENT_IN_BED) ||
           (from == PATIENT_IN_BED    && to == PATIENT_COMPLETED);
}

const char *gender_name(Gender g)
{
    switch (g) {
    case GENDER_MALE:   return "Male";
    case GENDER_FEMALE: return "Female";
    default:            return "Other / Prefer not to say";
    }
}

const char *status_name(PatientStatus s)
{
    switch (s) {
    case PATIENT_SCHEDULED: return "scheduled";
    case PATIENT_WAITING:   return "waiting";
    case PATIENT_IN_BED:    return "in_bed";
    default:                return "completed";
    }
}

const char *status_label(PatientStatus s)
{
    switch (s) {
    case PATIENT_SCHEDULED: return "Scheduled";
    case PATIENT_WAITING:   return "Waiting";
    case PATIENT_IN_BED:    return "In Bed";
    default:                return "Completed";
    }
}

void format_time(int minutes, char *out)
{
    if (minutes < 0 || minutes > LAST_MINUTE_OF_DAY) {
        snprintf(out, 6, "--:--");
        return;
    }
    snprintf(out, 6, "%02d:%02d", minutes / 60, minutes % 60);
}

int parse_time(const char *text, int *minutes)
{
    int h, m;

    if (text == NULL || strlen(text) != 5 || text[2] != ':')
        return 0;
    if (!isdigit((unsigned char)text[0]) || !isdigit((unsigned char)text[1]) ||
        !isdigit((unsigned char)text[3]) || !isdigit((unsigned char)text[4]))
        return 0;
    h = (text[0] - '0') * 10 + (text[1] - '0');
    m = (text[3] - '0') * 10 + (text[4] - '0');
    if (h > 23 || m > 59)
        return 0;
    *minutes = h * 60 + m;
    return 1;
}

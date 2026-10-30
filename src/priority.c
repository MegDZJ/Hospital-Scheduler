#include "priority.h"

/* Age bands: below 5 or 65 and above get +2, everyone else +0.
 * Edit the constants in config.h to change the boundaries or the points. */
int calculate_age_adjustment(int age)
{
    if (age < AGE_YOUNG_LIMIT)
        return AGE_ADJUSTMENT_POINTS;
    if (age >= AGE_OLD_LIMIT)
        return AGE_ADJUSTMENT_POINTS;
    return 0;
}

int calculate_priority(int severity, int age)
{
    return severity + calculate_age_adjustment(age);
}

int calculate_effective_priority(int original_priority, int waiting_minutes,
                                 int aging_enabled)
{
    if (!aging_enabled || waiting_minutes <= 0)
        return original_priority;
    return original_priority + waiting_minutes / AGING_INTERVAL_MINUTES;
}

int patient_effective_priority(const Patient *p, int now, int aging_enabled)
{
    int waited = now - p->arrival_time;
    return calculate_effective_priority(p->priority, waited, aging_enabled);
}

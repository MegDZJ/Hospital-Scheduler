#include "queue.h"
#include "priority.h"

int patient_has_higher_priority(const Patient *a, const Patient *b,
                                int now, int aging_enabled)
{
    int pa = patient_effective_priority(a, now, aging_enabled);
    int pb = patient_effective_priority(b, now, aging_enabled);

    if (pa != pb)
        return pa > pb;                              /* rule 1 */
    if (a->arrival_time != b->arrival_time)
        return a->arrival_time < b->arrival_time;    /* rule 2: FCFS */
    return a->registration_seq < b->registration_seq; /* rule 3 */
}

int patient_is_eligible(const Patient *p, int now)
{
    return p->status == PATIENT_WAITING && p->arrival_time <= now;
}

int get_next_waiting_patient(const Patient patients[], int count,
                             int now, int aging_enabled)
{
    int best = -1;
    int i;

    for (i = 0; i < count; i++) {
        if (!patient_is_eligible(&patients[i], now))
            continue;
        if (best < 0 ||
            patient_has_higher_priority(&patients[i], &patients[best],
                                        now, aging_enabled))
            best = i;
    }
    return best;
}

int build_waiting_queue(const Patient patients[], int count,
                        int now, int aging_enabled, int order[])
{
    int n = 0;
    int i, j;

    for (i = 0; i < count; i++) {
        if (patient_is_eligible(&patients[i], now))
            order[n++] = i;
    }
    /* insertion sort: simple, stable and fine for <= 200 patients */
    for (i = 1; i < n; i++) {
        int key = order[i];
        j = i - 1;
        while (j >= 0 &&
               patient_has_higher_priority(&patients[key], &patients[order[j]],
                                           now, aging_enabled)) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
    return n;
}

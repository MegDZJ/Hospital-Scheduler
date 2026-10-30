#include "statistics.h"

int calculate_waiting_time(const Patient *p, int now)
{
    switch (p->status) {
    case PATIENT_SCHEDULED:
        return -1;                              /* has not arrived yet */
    case PATIENT_WAITING:
        return now - p->arrival_time;           /* still waiting       */
    default:
        return p->start_time - p->arrival_time; /* final value         */
    }
}

int calculate_turnaround_time(const Patient *p)
{
    if (p->status != PATIENT_COMPLETED)
        return -1;
    return p->completion_time - p->arrival_time;
}

int calculate_response_time(const Patient *p)
{
    if (p->start_time < 0)
        return -1;
    return p->start_time - p->arrival_time;
}

double calculate_bed_utilization(int occupied, int total)
{
    if (total <= 0)
        return 0.0;
    return (double)occupied / (double)total * 100.0;
}

void compute_statistics(const Simulation *sim, Statistics *out)
{
    long sum_wait = 0, sum_turn = 0, sum_resp = 0;
    int i;

    out->registered = sim->patient_count;
    out->waiting = out->scheduled = out->in_bed = out->completed = 0;
    out->has_waiting = 0;
    out->max_waiting = 0;

    for (i = 0; i < sim->patient_count; i++) {
        const Patient *p = &sim->patients[i];
        switch (p->status) {
        case PATIENT_SCHEDULED:
            out->scheduled++;
            break;
        case PATIENT_WAITING: {
            int w = calculate_waiting_time(p, sim->now);
            out->waiting++;
            if (!out->has_waiting || w > out->max_waiting)
                out->max_waiting = w;
            out->has_waiting = 1;
            break;
        }
        case PATIENT_IN_BED:
            out->in_bed++;
            break;
        case PATIENT_COMPLETED:
            out->completed++;
            sum_wait += calculate_waiting_time(p, sim->now);
            sum_turn += calculate_turnaround_time(p);
            sum_resp += calculate_response_time(p);
            break;
        }
    }

    out->occupied_beds = count_occupied_beds(sim->beds);
    out->available_beds = NUM_BEDS - out->occupied_beds;
    out->utilization = calculate_bed_utilization(out->occupied_beds, NUM_BEDS);

    out->has_completed = out->completed > 0;
    if (out->has_completed) {
        out->avg_waiting = (double)sum_wait / out->completed;
        out->avg_turnaround = (double)sum_turn / out->completed;
        out->avg_response = (double)sum_resp / out->completed;
    } else {
        out->avg_waiting = out->avg_turnaround = out->avg_response = 0.0;
    }
}

/*
 * statistics.h - scheduling metrics.
 *
 *   Waiting Time    = Occupancy Start - Arrival   (so far: Now - Arrival)
 *   Turnaround Time = Completion - Arrival
 *   Response Time   = First Bed Assignment - Arrival
 *   Bed Utilization = Occupied Beds / Total Beds * 100
 *
 * In this non-preemptive model a patient gets a bed exactly once, so
 * response time equals waiting time.
 */
#ifndef STATISTICS_H
#define STATISTICS_H

#include "simulation.h"

typedef struct {
    int    registered;
    int    waiting;          /* arrived, no bed yet                 */
    int    scheduled;        /* future arrivals                     */
    int    in_bed;
    int    completed;
    int    occupied_beds;
    int    available_beds;
    double utilization;      /* percent                             */

    int    has_completed;    /* 0 => the three averages are N/A     */
    double avg_waiting;
    double avg_turnaround;
    double avg_response;

    int    has_waiting;      /* 0 => max_waiting is N/A             */
    int    max_waiting;      /* longest current wait, minutes       */
} Statistics;

/* Each returns -1 when the value is not defined yet. */
int calculate_waiting_time(const Patient *p, int now);
int calculate_turnaround_time(const Patient *p);
int calculate_response_time(const Patient *p);

double calculate_bed_utilization(int occupied, int total);

void compute_statistics(const Simulation *sim, Statistics *out);

#endif

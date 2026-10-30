/*
 * queue.h - the priority queue (array based).
 *
 * The "queue" is the set of patients whose status is WAITING.  Ordering:
 *   1. higher (effective) priority first
 *   2. earlier arrival time first            (FCFS)
 *   3. earlier registration sequence first   (deterministic final tiebreak)
 */
#ifndef QUEUE_H
#define QUEUE_H

#include "patient.h"

/* 1 if a must be served before b at time `now`. */
int patient_has_higher_priority(const Patient *a, const Patient *b,
                                int now, int aging_enabled);

/* A patient may be given a bed only if WAITING and already arrived. */
int patient_is_eligible(const Patient *p, int now);

/* Index of the next patient to serve, or -1 if nobody is eligible. */
int get_next_waiting_patient(const Patient patients[], int count,
                             int now, int aging_enabled);

/* Fills order[] with the indices of all eligible waiting patients, best
 * first, and returns how many there are. */
int build_waiting_queue(const Patient patients[], int count,
                        int now, int aging_enabled, int order[]);

#endif

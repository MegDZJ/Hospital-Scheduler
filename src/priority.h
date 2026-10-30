/*
 * priority.h - the priority formulas, kept in one place.
 *
 *   Priority Score     = Disease Severity + Age Adjustment
 *   Effective Priority = Priority Score + floor(WaitingTime / AgingInterval)
 *                        (second term only when aging is enabled)
 *
 * Gender is NOT part of either formula.
 * These are academic scheduling rules, not clinical triage guidance.
 */
#ifndef PRIORITY_H
#define PRIORITY_H

#include "patient.h"

int calculate_age_adjustment(int age);
int calculate_priority(int severity, int age);
int calculate_effective_priority(int original_priority, int waiting_minutes,
                                 int aging_enabled);

/* Effective priority of a patient at simulated time `now`. */
int patient_effective_priority(const Patient *p, int now, int aging_enabled);

#endif

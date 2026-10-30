/*
 * conditions.h - the central, predefined registry of conditions.
 * Severity scores and occupancy durations are ILLUSTRATIVE demonstration
 * parameters, not medical triage standards.
 */
#ifndef CONDITIONS_H
#define CONDITIONS_H

#include <stddef.h>

typedef struct {
    int         id;
    const char *name;
    int         severity;          /* 1 (low) .. 5 (high)                    */
    int         duration_minutes;  /* modeled bed occupancy                  */
    int         requires_context;  /* needs the "clinical context" tick box  */
    const char *context_label;     /* text shown next to that tick box       */
    int         min_age;           /* demo eligibility bounds (inclusive)    */
    int         max_age;
    int         gender;            /* -1 any, 0 male only, 1 female only     */
} Condition;

int              condition_count(void);
const Condition *get_condition(int id);   /* NULL if id is unknown */

/*
 * Eligibility rules: the confirmed clinical context, an age range and, for
 * sex-specific conditions, the patient's gender (a Gender value).
 * Returns 1 if eligible, otherwise 0 and a message in err.
 */
int check_condition_eligibility(const Condition *c, int age, int gender,
                                int context_confirmed,
                                char *err, size_t errlen);

#endif

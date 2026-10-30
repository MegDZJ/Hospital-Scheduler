#include "conditions.h"
#include "config.h"
#include <stdio.h>

static const Condition CONDITIONS[] = {
    /* id, name, severity, minutes, needs-context, context label, minAge, maxAge, gender */
    { 0, "Mild fever",                 1,  20, 0, NULL, 0, MAX_AGE, -1 },
    { 1, "Minor injury",               2,  30, 0, NULL, 0, MAX_AGE, -1 },
    { 2, "Asthma observation",         3,  60, 0, NULL, 0, MAX_AGE, -1 },
    { 3, "Diabetes complication",      4,  90, 0, NULL, 0, MAX_AGE, -1 },
    { 4, "Fracture observation",       4, 120, 0, NULL, 0, MAX_AGE, -1 },
    { 5, "Serious cardiac condition",  5, 180, 0, NULL, 0, MAX_AGE, -1 },
    { 6, "Pregnancy-related care",     3,  60, 1,
      "A clinician has confirmed pregnancy-related care applies to this patient",
      10, 60, 1 },
    { 7, "Prostate-related condition", 2,  45, 1,
      "A clinician has confirmed prostate-related care applies to this patient",
      12, MAX_AGE, 0 },
    { 8, "General consultation",       1,  15, 0, NULL, 0, MAX_AGE, -1 },
};

#define CONDITION_TOTAL ((int)(sizeof(CONDITIONS) / sizeof(CONDITIONS[0])))

int condition_count(void)
{
    return CONDITION_TOTAL;
}

const Condition *get_condition(int id)
{
    if (id < 0 || id >= CONDITION_TOTAL)
        return NULL;
    return &CONDITIONS[id];
}

int check_condition_eligibility(const Condition *c, int age, int gender,
                                int context_confirmed,
                                char *err, size_t errlen)
{
    if (c == NULL) {
        snprintf(err, errlen, "Unknown condition");
        return 0;
    }
    if (c->gender >= 0 && gender != c->gender) {
        snprintf(err, errlen,
                 "\"%s\" applies to %s patients only. "
                 "Choose a condition that fits the patient",
                 c->name, c->gender == 0 ? "male" : "female");
        return 0;
    }
    if (c->requires_context && !context_confirmed) {
        snprintf(err, errlen,
                 "\"%s\" requires the clinical-context confirmation. "
                 "Confirm it or choose a different condition",
                 c->name);
        return 0;
    }
    if (age < c->min_age || age > c->max_age) {
        snprintf(err, errlen,
                 "\"%s\" is modeled for ages %d-%d in this demo. "
                 "Choose a condition that fits the patient",
                 c->name, c->min_age, c->max_age);
        return 0;
    }
    return 1;
}

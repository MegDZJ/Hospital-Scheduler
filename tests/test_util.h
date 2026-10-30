/* test_util.h - a deliberately tiny assertion helper for the test programs. */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>
#include <string.h>
#include "simulation.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        g_checks++;                                                        \
        if (!(cond)) {                                                     \
            g_failures++;                                                  \
            printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
        }                                                                  \
    } while (0)

#define TEST(name) printf("  %s\n", name)

#define TEST_DONE(suite)                                                   \
    do {                                                                   \
        printf("%s: %d checks, %d failed\n", suite, g_checks, g_failures); \
        return g_failures == 0 ? 0 : 1;                                    \
    } while (0)

/* Condition ids from the registry */
enum {
    COND_FEVER = 0,      /* severity 1, 20 min  */
    COND_INJURY = 1,     /* severity 2, 30 min  */
    COND_ASTHMA = 2,     /* severity 3, 60 min  */
    COND_DIABETES = 3,   /* severity 4, 90 min  */
    COND_FRACTURE = 4,   /* severity 4, 120 min */
    COND_CARDIAC = 5,    /* severity 5, 180 min */
    COND_PREGNANCY = 6,  /* severity 3, 60 min  */
    COND_PROSTATE = 7,   /* severity 2, 45 min  */
    COND_CONSULT = 8     /* severity 1, 15 min  */
};

#define T(h, m) ((h) * 60 + (m))

/* Register a patient; returns its index, or -1 if registration failed. */
static inline int reg(Simulation *sim, const char *name, int age, int cond, int arrival)
{
    PatientInput in;
    char err[200];
    int idx;

    memset(&in, 0, sizeof in);
    snprintf(in.name, sizeof in.name, "%s", name);
    in.age = age;
    in.gender = GENDER_OTHER;
    in.condition_id = cond;
    in.context_confirmed = 1;
    in.arrival_time = arrival;
    if (!register_patient(sim, &in, &idx, err, sizeof err))
        return -1;
    return idx;
}

#endif

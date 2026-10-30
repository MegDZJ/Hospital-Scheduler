/*
 * config.h - central place for every tunable constant of the simulation.
 * Change a value here and the whole program (scheduler, validation, API,
 * tests) follows.  All values are illustrative demonstration parameters.
 */
#ifndef CONFIG_H
#define CONFIG_H

/* ---- Hospital capacity ---- */
#ifndef NUM_BEDS
#define NUM_BEDS             3      /* exactly 3 beds by default             */
#endif
#define MAX_PATIENTS         200    /* patient records per simulation        */
#define MAX_NAME_LEN         80     /* bytes of UTF-8, excluding the '\0'    */
#define MAX_AGE              120

/* ---- Simulated clock (integer minutes after midnight) ---- */
#define MINUTES_PER_DAY      1440
#define LAST_MINUTE_OF_DAY   1439   /* 23:59 - the model covers ONE day      */
#define DEFAULT_START_TIME   540    /* 09:00                                 */
#define MAX_ADVANCE_MINUTES  480    /* longest single "advance" request      */
#ifndef MIN_ARRIVAL_LEAD_MINUTES
#define MIN_ARRIVAL_LEAD_MINUTES 1  /* arrival must be >= now + 1 minute     */
#endif

/* ---- Priority score: severity + age adjustment ---- */
#define AGE_YOUNG_LIMIT      5      /* age <  5  gets the adjustment         */
#define AGE_OLD_LIMIT        65     /* age >= 65 gets the adjustment         */
#define AGE_ADJUSTMENT_POINTS 2

/* ---- Priority aging (optional, OFF by default) ---- */
#define AGING_INTERVAL_MINUTES 30   /* +1 effective priority per 30 min wait */
#define AGING_DEFAULT_ENABLED  0

/* ---- Notification feed ---- */
#define MAX_EVENTS           64
#define EVENT_TEXT_LEN       120

/* ---- Automatic progression (only while status == Running) ---- */
#define DEFAULT_TICK_SECONDS 2      /* real seconds per simulated minute     */

#endif

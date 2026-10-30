#include "simulation.h"
#include "conditions.h"
#include "priority.h"
#include "queue.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Event log (feeds the UI notifications)                              */
/* ------------------------------------------------------------------ */

static void log_event(Simulation *sim, const char *fmt, ...)
{
    SimEvent *e;
    va_list ap;

    if (sim->event_count == MAX_EVENTS) {
        memmove(&sim->events[0], &sim->events[1],
                sizeof(SimEvent) * (MAX_EVENTS - 1));
        sim->event_count--;
    }
    e = &sim->events[sim->event_count++];
    e->id = ++sim->next_event_id;
    e->time = sim->now;
    va_start(ap, fmt);
    vsnprintf(e->text, sizeof e->text, fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------------------ */
/* Setup / reset                                                       */
/* ------------------------------------------------------------------ */

void init_simulation(Simulation *sim)
{
    memset(sim, 0, sizeof *sim);
    sim->start_time = DEFAULT_START_TIME;
    sim->aging_enabled = AGING_DEFAULT_ENABLED;
    sim->tick_seconds = DEFAULT_TICK_SECONDS;
    reset_simulation(sim);
}

void reset_simulation(Simulation *sim)
{
    int aging = sim->aging_enabled;
    int tick = sim->tick_seconds;
    int start = sim->start_time;
    int event_id = sim->next_event_id;   /* ids keep growing across resets */

    memset(sim, 0, sizeof *sim);
    sim->next_event_id = event_id;
    sim->start_time = start;
    sim->aging_enabled = aging;
    sim->tick_seconds = tick;
    sim->now = start;
    sim->status = SIM_NOT_STARTED;
    sim->next_sequence = 1;
    init_beds(sim->beds);
    log_event(sim, "Simulation reset to %02d:%02d", start / 60, start % 60);
}

void set_aging(Simulation *sim, int enabled)
{
    sim->aging_enabled = enabled ? 1 : 0;
    log_event(sim, "Priority aging %s", enabled ? "enabled" : "disabled");
}

/* ------------------------------------------------------------------ */
/* The three kinds of event processing                                 */
/* ------------------------------------------------------------------ */

/* Step 1 of the event policy: release every bed whose time has come. */
static void release_due_beds(Simulation *sim, int t)
{
    char err[96];
    int i;

    for (i = 0; i < NUM_BEDS; i++) {
        Bed *bed = &sim->beds[i];
        if (bed->status != BED_OCCUPIED || bed->expected_completion > t)
            continue;
        {
            Patient *p = &sim->patients[bed->patient_index];
            int bed_number = bed->id;
            if (release_bed(sim->beds, i, p, t, err, sizeof err))
                log_event(sim, "%s completed; bed %d released", p->id,
                          bed_number);
        }
    }
}

/* Step 2: scheduled patients whose arrival time has been reached. */
static void admit_arrivals(Simulation *sim, int t)
{
    int i;
    for (i = 0; i < sim->patient_count; i++) {
        Patient *p = &sim->patients[i];
        if (p->status == PATIENT_SCHEDULED && p->arrival_time <= t &&
            can_transition(p->status, PATIENT_WAITING)) {
            p->status = PATIENT_WAITING;
            log_event(sim, "%s has arrived and is waiting", p->id);
        }
    }
}

/* Step 3: while a bed is free and somebody is eligible, serve the best. */
static void allocate_free_beds(Simulation *sim, int t)
{
    char err[96];

    for (;;) {
        int bed_index = find_free_bed(sim->beds);
        int next;
        if (bed_index < 0)
            break;
        next = get_next_waiting_patient(sim->patients, sim->patient_count,
                                        t, sim->aging_enabled);
        if (next < 0)
            break;
        if (!assign_patient_to_bed(sim->beds, bed_index, &sim->patients[next],
                                   next, t, err, sizeof err))
            break;      /* cannot happen; avoids an endless loop */
        log_event(sim, "Bed %d assigned to %s", bed_index + 1,
                  sim->patients[next].id);
    }
}

static void process_events_at(Simulation *sim, int t)
{
    release_due_beds(sim, t);
    admit_arrivals(sim, t);
    allocate_free_beds(sim, t);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

/* Validates and stores a patient but does NOT allocate beds. */
static int add_patient(Simulation *sim, const PatientInput *in, int lead,
                       int *out_index, char *err, size_t errlen)
{
    const Condition *cond;
    Patient *p;

    if (!validate_patient_input(in, sim->now + lead, err, errlen))
        return 0;
    if (sim->patient_count >= MAX_PATIENTS) {
        snprintf(err, errlen,
                 "No capacity for additional patient records (maximum %d)",
                 MAX_PATIENTS);
        return 0;
    }
    cond = get_condition(in->condition_id);

    p = &sim->patients[sim->patient_count];
    memset(p, 0, sizeof *p);
    p->registration_seq = sim->next_sequence++;
    snprintf(p->id, sizeof p->id, "PAT-%04d", p->registration_seq);
    snprintf(p->name, sizeof p->name, "%s", in->name);
    p->age = in->age;
    p->gender = in->gender;
    p->condition_id = in->condition_id;
    p->context_confirmed = in->context_confirmed ? 1 : 0;
    p->arrival_time = in->arrival_time;
    p->severity = cond->severity;
    p->age_adjustment = calculate_age_adjustment(in->age);
    p->priority = calculate_priority(cond->severity, in->age);
    p->status = (p->arrival_time <= sim->now) ? PATIENT_WAITING
                                              : PATIENT_SCHEDULED;
    p->bed_id = 0;
    p->start_time = -1;
    p->expected_completion = -1;
    p->completion_time = -1;

    *out_index = sim->patient_count;
    sim->patient_count++;
    log_event(sim, "%s registered (priority %d)", p->id, p->priority);
    return 1;
}

int register_patient(Simulation *sim, const PatientInput *in,
                     int *out_index, char *err, size_t errlen)
{
    if (!add_patient(sim, in, MIN_ARRIVAL_LEAD_MINUTES, out_index, err, errlen))
        return 0;
    allocate_free_beds(sim, sim->now);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Advancing the clock                                                 */
/* ------------------------------------------------------------------ */

/* The next simulated minute (> now) at which something happens. */
static int next_event_time(const Simulation *sim, int limit)
{
    int t = limit;
    int i;

    for (i = 0; i < NUM_BEDS; i++) {
        const Bed *b = &sim->beds[i];
        if (b->status == BED_OCCUPIED && b->expected_completion > sim->now &&
            b->expected_completion < t)
            t = b->expected_completion;
    }
    for (i = 0; i < sim->patient_count; i++) {
        const Patient *p = &sim->patients[i];
        if (p->status == PATIENT_SCHEDULED && p->arrival_time > sim->now &&
            p->arrival_time < t)
            t = p->arrival_time;
    }
    return t;
}

int advance_simulation(Simulation *sim, int minutes, char *err, size_t errlen)
{
    int target;

    if (minutes <= 0) {
        snprintf(err, errlen, "Minutes must be a positive whole number");
        return 0;
    }
    if (minutes > MAX_ADVANCE_MINUTES) {
        snprintf(err, errlen, "Cannot advance more than %d minutes at once",
                 MAX_ADVANCE_MINUTES);
        return 0;
    }
    target = sim->now + minutes;
    if (target > LAST_MINUTE_OF_DAY) {
        snprintf(err, errlen,
                 "Advance would leave the modeled day (ends at 23:59)");
        return 0;
    }

    process_events_at(sim, sim->now);   /* make sure we start settled */
    while (sim->now < target) {
        int t = next_event_time(sim, target);
        sim->now = t;
        process_events_at(sim, t);
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Start / pause / resume                                              */
/* ------------------------------------------------------------------ */

int start_simulation(Simulation *sim, char *err, size_t errlen)
{
    if (sim->status != SIM_NOT_STARTED) {
        snprintf(err, errlen, "Simulation is already started");
        return 0;
    }
    sim->status = SIM_RUNNING;
    log_event(sim, "Simulation started");
    return 1;
}

int pause_simulation(Simulation *sim, char *err, size_t errlen)
{
    if (sim->status != SIM_RUNNING) {
        snprintf(err, errlen, "Simulation is not running");
        return 0;
    }
    sim->status = SIM_PAUSED;
    log_event(sim, "Simulation paused");
    return 1;
}

int resume_simulation(Simulation *sim, char *err, size_t errlen)
{
    if (sim->status != SIM_PAUSED) {
        snprintf(err, errlen, "Simulation is not paused");
        return 0;
    }
    sim->status = SIM_RUNNING;
    log_event(sim, "Simulation resumed");
    return 1;
}

void auto_tick(Simulation *sim)
{
    char err[96];

    if (sim->status != SIM_RUNNING)
        return;
    if (sim->now >= LAST_MINUTE_OF_DAY) {
        sim->status = SIM_PAUSED;
        log_event(sim, "End of the modeled day reached; paused");
        return;
    }
    advance_simulation(sim, 1, err, sizeof err);
}

/* ------------------------------------------------------------------ */
/* Clear and demo                                                      */
/* ------------------------------------------------------------------ */

int clear_patients(Simulation *sim, char *err, size_t errlen)
{
    if (count_occupied_beds(sim->beds) > 0) {
        snprintf(err, errlen,
                 "Beds are occupied; clearing would leave dangling bed "
                 "references. Use Reset Simulation instead");
        return 0;
    }
    sim->patient_count = 0;
    log_event(sim, "Patient records cleared");
    return 1;
}

typedef struct {
    const char *name;
    int         age;
    Gender      gender;
    int         condition_id;
    int         context;
    const char *arrival;
} DemoPatient;

/* Condition ids: 0 fever, 1 injury, 2 asthma, 3 diabetes, 4 fracture,
 * 5 cardiac, 6 pregnancy, 7 prostate, 8 consultation.
 * Priorities (severity + age adjustment) are in the comment on each line.  */
static const DemoPatient DEMO[] = {
    { "Ravi Kumar",    72, GENDER_MALE,   5, 0, "09:00" }, /* 5+2 = 7 */
    { "Anita Sharma",   3, GENDER_FEMALE, 2, 0, "09:00" }, /* 3+2 = 5 */
    { "John Miller",   45, GENDER_MALE,   4, 0, "09:00" }, /* 4+0 = 4 */
    { "Sara Khan",     28, GENDER_FEMALE, 6, 1, "09:00" }, /* 3+0 = 3 */
    { "Li Wei",        68, GENDER_MALE,   3, 0, "09:00" }, /* 4+2 = 6 */
    { "Priya Nair",    34, GENDER_FEMALE, 1, 0, "09:00" }, /* 2+0 = 2 */
    { "Omar Ali",       2, GENDER_MALE,   0, 0, "09:00" }, /* 1+2 = 3 */
    { "Maria Garcia",  80, GENDER_FEMALE, 8, 0, "09:00" }, /* 1+2 = 3 */
    { "Tom Baker",     55, GENDER_MALE,   7, 1, "09:00" }, /* 2+0 = 2 */
    { "Neha Gupta",    30, GENDER_FEMALE, 0, 0, "09:00" }, /* 1+0 = 1 */
    { "Karan Singh",   50, GENDER_MALE,   8, 0, "09:00" }, /* 1+0 = 1 */
    { "Emily Chen",    66, GENDER_FEMALE, 3, 0, "09:00" }, /* 4+2 = 6 */
    { "Fatima Noor",    4, GENDER_FEMALE, 1, 0, "09:10" }, /* 2+2 = 4 */
    { "David Lee",     60, GENDER_MALE,   0, 0, "09:20" }, /* 1+0 = 1 */
    { "Grace Okafor",  75, GENDER_FEMALE, 2, 0, "09:30" }, /* 3+2 = 5 */
    { "Hassan Ahmed",   1, GENDER_MALE,   5, 0, "09:45" }, /* 5+2 = 7 */
    { "Alex Morgan",   29, GENDER_OTHER,  1, 0, "10:00" }, /* 2+0 = 2 */
};

#define DEMO_TOTAL ((int)(sizeof(DEMO) / sizeof(DEMO[0])))

int demo_patient_count(void)
{
    return DEMO_TOTAL;
}

int load_demo(Simulation *sim, char *err, size_t errlen)
{
    int i;

    reset_simulation(sim);          /* never produces duplicate records */
    for (i = 0; i < DEMO_TOTAL; i++) {
        PatientInput in;
        int index;

        memset(&in, 0, sizeof in);
        snprintf(in.name, sizeof in.name, "%s", DEMO[i].name);
        in.age = DEMO[i].age;
        in.gender = DEMO[i].gender;
        in.condition_id = DEMO[i].condition_id;
        in.context_confirmed = DEMO[i].context;
        if (!parse_time(DEMO[i].arrival, &in.arrival_time) ||
            !add_patient(sim, &in, 0, &index, err, errlen))
            return -1;
    }
    /* Allocate once, after everyone is registered, so beds go to the
     * highest priorities rather than to whoever was typed first. */
    allocate_free_beds(sim, sim->now);
    log_event(sim, "Demo scenario loaded (%d patients)", DEMO_TOTAL);
    return DEMO_TOTAL;
}

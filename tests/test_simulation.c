#include "test_util.h"
#include "statistics.h"

static Simulation sim;
static char err[200];

/* Occupy every bed except `free_count` of them with long (fracture, 120 min)
 * patients so later patients have to wait. */
static void fill_beds_long(int how_many)
{
    int i;
    for (i = 0; i < how_many; i++) {
        char name[32];
        snprintf(name, sizeof name, "Filler %d", i);
        reg(&sim, name, 30, COND_FRACTURE, sim.now);
    }
}

int main(void)
{
    int a, b, i;

    printf("Simulation tests\n");

    TEST("clock starts at 09:00 and advances correctly");
    init_simulation(&sim);
    CHECK(sim.now == T(9, 0));
    CHECK(sim.status == SIM_NOT_STARTED);
    CHECK(advance_simulation(&sim, 15, err, sizeof err));
    CHECK(sim.now == T(9, 15));

    TEST("a future arrival stays ineligible until its time");
    init_simulation(&sim);
    a = reg(&sim, "Later", 30, COND_FEVER, T(9, 30));
    CHECK(a >= 0 && sim.patients[a].status == PATIENT_SCHEDULED);
    CHECK(sim.patients[a].bed_id == 0);
    advance_simulation(&sim, 29, err, sizeof err);
    CHECK(sim.patients[a].status == PATIENT_SCHEDULED);
    CHECK(count_occupied_beds(sim.beds) == 0);
    advance_simulation(&sim, 1, err, sizeof err);
    CHECK(sim.patients[a].status == PATIENT_IN_BED);
    CHECK(sim.patients[a].start_time == T(9, 30));

    TEST("a bed releases at its exact completion time");
    init_simulation(&sim);
    a = reg(&sim, "Fever", 30, COND_FEVER, T(9, 0));     /* 20 minutes */
    CHECK(sim.patients[a].expected_completion == T(9, 20));
    advance_simulation(&sim, 19, err, sizeof err);
    CHECK(sim.patients[a].status == PATIENT_IN_BED);
    CHECK(sim.beds[0].status == BED_OCCUPIED);
    advance_simulation(&sim, 1, err, sizeof err);
    CHECK(sim.patients[a].status == PATIENT_COMPLETED);
    CHECK(sim.patients[a].completion_time == T(9, 20));
    CHECK(sim.beds[0].status == BED_AVAILABLE);

    TEST("one big advance processes the intermediate events in order");
    init_simulation(&sim);
    /* beds 1-8 busy for 2h; bed 9 = consultation (15), bed 10 = fever (20) */
    fill_beds_long(8);
    reg(&sim, "Consult", 30, COND_CONSULT, T(9, 0));
    reg(&sim, "Fever", 30, COND_FEVER, T(9, 0));
    a = reg(&sim, "Wait A", 30, COND_CONSULT, T(9, 0));    /* waits */
    b = reg(&sim, "Wait B", 30, COND_CONSULT, T(9, 0));    /* waits */
    CHECK(sim.patients[a].status == PATIENT_WAITING);
    CHECK(sim.patients[b].status == PATIENT_WAITING);
    CHECK(advance_simulation(&sim, 30, err, sizeof err));
    CHECK(sim.now == T(9, 30));
    CHECK(sim.patients[a].start_time == T(9, 15));         /* bed 9 freed   */
    CHECK(sim.patients[b].start_time == T(9, 20));         /* bed 10 freed  */
    CHECK(sim.patients[a].bed_id == 9 && sim.patients[b].bed_id == 10);
    CHECK(sim.patients[a].completion_time == T(9, 30));    /* 9:15 + 15     */
    CHECK(sim.patients[a].status == PATIENT_COMPLETED);
    CHECK(sim.patients[b].status == PATIENT_IN_BED);

    TEST("a freed bed goes to the highest-priority waiting patient");
    init_simulation(&sim);
    fill_beds_long(9);
    reg(&sim, "Fever", 30, COND_FEVER, T(9, 0));            /* bed 10, 20 min */
    a = reg(&sim, "Low", 30, COND_CONSULT, T(9, 0));        /* priority 1 */
    b = reg(&sim, "High", 30, COND_CARDIAC, T(9, 0));       /* priority 5 */
    CHECK(sim.patients[a].status == PATIENT_WAITING);
    CHECK(sim.patients[b].status == PATIENT_WAITING);
    advance_simulation(&sim, 20, err, sizeof err);
    CHECK(sim.patients[b].status == PATIENT_IN_BED);
    CHECK(sim.patients[b].start_time == T(9, 20));
    CHECK(sim.patients[a].status == PATIENT_WAITING);

    TEST("simultaneous completions are all handled at that minute");
    init_simulation(&sim);
    reg(&sim, "Fever 1", 30, COND_FEVER, T(9, 0));          /* bed 1 -> 9:20 */
    reg(&sim, "Fever 2", 30, COND_FEVER, T(9, 0));          /* bed 2 -> 9:20 */
    fill_beds_long(8);
    a = reg(&sim, "Mid", 30, COND_ASTHMA, T(9, 0));         /* priority 3 */
    b = reg(&sim, "Top", 30, COND_CARDIAC, T(9, 0));        /* priority 5 */
    advance_simulation(&sim, 20, err, sizeof err);
    CHECK(sim.patients[b].start_time == T(9, 20));
    CHECK(sim.patients[a].start_time == T(9, 20));
    CHECK(sim.patients[b].bed_id == 1);        /* best patient, lowest bed */
    CHECK(sim.patients[a].bed_id == 2);
    CHECK(count_occupied_beds(sim.beds) == 10);

    TEST("an arrival and a completion at the same minute: completion first");
    init_simulation(&sim);
    fill_beds_long(9);
    reg(&sim, "Fever", 30, COND_FEVER, T(9, 0));            /* frees 9:20 */
    a = reg(&sim, "Arrives", 30, COND_CARDIAC, T(9, 20));   /* arrives 9:20 */
    advance_simulation(&sim, 20, err, sizeof err);
    CHECK(sim.patients[a].status == PATIENT_IN_BED);
    CHECK(sim.patients[a].start_time == T(9, 20));
    CHECK(sim.patients[a].bed_id == 10);

    TEST("the clock never moves backward");
    init_simulation(&sim);
    advance_simulation(&sim, 10, err, sizeof err);
    CHECK(!advance_simulation(&sim, -5, err, sizeof err));
    CHECK(!advance_simulation(&sim, 0, err, sizeof err));
    CHECK(sim.now == T(9, 10));
    CHECK(reg(&sim, "Past", 30, COND_FEVER, T(9, 0)) == -1);
    CHECK(sim.patient_count == 0);

    TEST("invalid advances are rejected");
    init_simulation(&sim);
    CHECK(!advance_simulation(&sim, MAX_ADVANCE_MINUTES + 1, err, sizeof err));
    sim.now = T(23, 40);
    CHECK(!advance_simulation(&sim, 30, err, sizeof err));
    CHECK(sim.now == T(23, 40));
    CHECK(advance_simulation(&sim, 19, err, sizeof err));
    CHECK(sim.now == LAST_MINUTE_OF_DAY);

    TEST("never more than 10 beds occupied; extra patients wait");
    init_simulation(&sim);
    for (i = 0; i < 15; i++)
        reg(&sim, "P", 30, COND_ASTHMA, T(9, 0));
    CHECK(count_occupied_beds(sim.beds) == 10);
    {
        Statistics s;
        compute_statistics(&sim, &s);
        CHECK(s.in_bed == 10 && s.waiting == 5);
    }

    TEST("start / pause / resume state machine");
    init_simulation(&sim);
    CHECK(!pause_simulation(&sim, err, sizeof err));
    CHECK(!resume_simulation(&sim, err, sizeof err));
    CHECK(start_simulation(&sim, err, sizeof err) && sim.status == SIM_RUNNING);
    CHECK(!start_simulation(&sim, err, sizeof err));
    CHECK(pause_simulation(&sim, err, sizeof err) && sim.status == SIM_PAUSED);
    auto_tick(&sim);                       /* paused: must not move */
    CHECK(sim.now == T(9, 0));
    CHECK(resume_simulation(&sim, err, sizeof err));
    auto_tick(&sim);
    CHECK(sim.now == T(9, 1));

    TEST("clear is refused while beds are occupied; IDs are never reused");
    init_simulation(&sim);
    a = reg(&sim, "One", 30, COND_FEVER, T(9, 0));
    CHECK(!clear_patients(&sim, err, sizeof err));
    CHECK(sim.patient_count == 1);
    advance_simulation(&sim, 20, err, sizeof err);
    CHECK(clear_patients(&sim, err, sizeof err));
    CHECK(sim.patient_count == 0);
    b = reg(&sim, "Two", 30, COND_FEVER, sim.now);
    CHECK(strcmp(sim.patients[b].id, "PAT-0002") == 0);

    TEST("capacity limit is handled gracefully");
    init_simulation(&sim);
    for (i = 0; i < MAX_PATIENTS; i++)
        CHECK(reg(&sim, "Cap", 30, COND_FEVER, T(23, 0)) >= 0);
    CHECK(reg(&sim, "Overflow", 30, COND_FEVER, T(23, 0)) == -1);
    CHECK(sim.patient_count == MAX_PATIENTS);

    TEST("reset returns to the documented initial state");
    init_simulation(&sim);
    fill_beds_long(5);
    start_simulation(&sim, err, sizeof err);
    set_aging(&sim, 1);
    advance_simulation(&sim, 45, err, sizeof err);
    reset_simulation(&sim);
    CHECK(sim.now == T(9, 0));
    CHECK(sim.status == SIM_NOT_STARTED);
    CHECK(sim.patient_count == 0);
    CHECK(count_occupied_beds(sim.beds) == 0);
    CHECK(sim.next_sequence == 1);
    CHECK(sim.aging_enabled == 1);          /* configuration is kept */

    TEST("demo load is repeatable and respects the 10-bed limit");
    init_simulation(&sim);
    CHECK(load_demo(&sim, err, sizeof err) == demo_patient_count());
    CHECK(load_demo(&sim, err, sizeof err) == demo_patient_count());
    CHECK(sim.patient_count == demo_patient_count());
    CHECK(count_occupied_beds(sim.beds) == 10);
    CHECK(demo_patient_count() >= 12);

    TEST_DONE("test_simulation");
}

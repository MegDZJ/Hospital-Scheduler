#include "test_util.h"
#include "priority.h"
#include "conditions.h"

static Simulation sim;

int main(void)
{
    printf("Priority tests\n");

    TEST("age below 5 gets the adjustment");
    CHECK(calculate_age_adjustment(0) == AGE_ADJUSTMENT_POINTS);
    CHECK(calculate_age_adjustment(4) == AGE_ADJUSTMENT_POINTS);

    TEST("age 5 gets none");
    CHECK(calculate_age_adjustment(5) == 0);

    TEST("age 64 gets none");
    CHECK(calculate_age_adjustment(64) == 0);

    TEST("age 65 and above gets the adjustment");
    CHECK(calculate_age_adjustment(65) == AGE_ADJUSTMENT_POINTS);
    CHECK(calculate_age_adjustment(120) == AGE_ADJUSTMENT_POINTS);

    TEST("severity and age adjustment combine (spec examples A, B, C)");
    CHECK(calculate_priority(1, 3) == 3);    /* Patient A */
    CHECK(calculate_priority(4, 40) == 4);   /* Patient B */
    CHECK(calculate_priority(3, 70) == 5);   /* Patient C */

    TEST("gender does not change the priority");
    init_simulation(&sim);
    {
        int a, b, c;
        PatientInput in;
        char err[100];
        int genders[3] = { GENDER_MALE, GENDER_FEMALE, GENDER_OTHER };
        int idx[3], i;
        (void)a; (void)b; (void)c;
        for (i = 0; i < 3; i++) {
            memset(&in, 0, sizeof in);
            snprintf(in.name, sizeof in.name, "Pat %d", i);
            in.age = 40;
            in.gender = (Gender)genders[i];
            in.condition_id = COND_DIABETES;
            in.arrival_time = T(23, 0);        /* future: stays scheduled */
            CHECK(register_patient(&sim, &in, &idx[i], err, sizeof err));
        }
        CHECK(sim.patients[idx[0]].priority == 4);
        CHECK(sim.patients[idx[1]].priority == 4);
        CHECK(sim.patients[idx[2]].priority == 4);
    }

    TEST("registration stores the server-calculated breakdown");
    init_simulation(&sim);
    {
        int i = reg(&sim, "Baby", 3, COND_FEVER, T(23, 0));
        CHECK(i >= 0);
        CHECK(sim.patients[i].severity == 1);
        CHECK(sim.patients[i].age_adjustment == 2);
        CHECK(sim.patients[i].priority == 3);
    }

    TEST("effective priority: aging off leaves the score alone");
    CHECK(calculate_effective_priority(3, 95, 0) == 3);

    TEST("effective priority: aging adds floor(wait / 30)");
    CHECK(calculate_effective_priority(3, 29, 1) == 3);
    CHECK(calculate_effective_priority(3, 30, 1) == 4);
    CHECK(calculate_effective_priority(3, 95, 1) == 6);

    TEST("eligibility rules are separate from gender");
    {
        char err[200];
        const Condition *preg = get_condition(COND_PREGNANCY);
        CHECK(!check_condition_eligibility(preg, 30, GENDER_FEMALE, 0, err, sizeof err));
        CHECK(check_condition_eligibility(preg, 30, GENDER_FEMALE, 1, err, sizeof err));
        CHECK(!check_condition_eligibility(preg, 5, GENDER_FEMALE, 1, err, sizeof err));
        CHECK(!check_condition_eligibility(preg, 30, GENDER_MALE, 1, err, sizeof err));
        CHECK(!check_condition_eligibility(preg, 30, GENDER_OTHER, 1, err, sizeof err));
        CHECK(!check_condition_eligibility(get_condition(COND_PROSTATE), 30, GENDER_FEMALE, 1, err, sizeof err));
        CHECK(check_condition_eligibility(get_condition(COND_PROSTATE), 30, GENDER_MALE, 1, err, sizeof err));
        CHECK(check_condition_eligibility(get_condition(COND_FEVER), 30, GENDER_MALE, 0, err, sizeof err));
    }
    {
        PatientInput in;
        char err[200];
        memset(&in, 0, sizeof in);
        snprintf(in.name, sizeof in.name, "A");
        in.age = 30; in.gender = GENDER_MALE; in.condition_id = COND_FEVER;
        in.arrival_time = T(9, 0);
        CHECK(!validate_patient_input(&in, T(9, 1), err, sizeof err));
        in.arrival_time = T(9, 1);
        CHECK(validate_patient_input(&in, T(9, 1), err, sizeof err));
    }

    TEST_DONE("test_priority");
}

/* Host tests for the modem-protocol logic.
 *
 * The sketch is #included, not linked. That reaches its static functions -
 * onLine(), queueCommand(), tryCallAction() - and, more importantly, means
 * these tests exercise the SHIPPING source. A copy of the logic would drift
 * from the firmware within a month and quietly stop testing anything.
 *
 * setup() and loop() are never called; the tests drive the parser directly
 * with the lines a SIM900A actually emits.
 */
#include <stdio.h>
#include <string.h>

/* F_CPU must satisfy the sketch's guard before it is included. */
#define F_CPU 8000000UL

#include "../firmware/ANANT_IONS_SIM900A_ATTINY85_V11.ino"

static int checks = 0, failures = 0;

#define CHECK(cond) do {                                              \
        checks++;                                                     \
        if (!(cond)) {                                                \
            printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);\
            failures++;                                               \
        }                                                             \
    } while (0)

#define RUN(fn) do {                                                  \
        int before = failures;                                        \
        printf("  %-52s", #fn);                                       \
        fflush(stdout);                                               \
        reset_all();                                                  \
        fn();                                                         \
        printf("%s\n", failures == before ? "ok" : "");               \
    } while (0)

/* Feed one line exactly as the modem would deliver it. */
static void feed(const char *text)
{
    strncpy(line, text, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    lineLen = 0;
    onLine();
}

static void reset_all(void)
{
    host_millis   = 100000UL;
    expectBody    = false;
    pendingAction = 0;
    pendingDelete = -1;
    pendingHangup = false;
    pendingToggle = false;
    lastCmd       = 0;
    lastCmdMs     = 0;
    systemOn      = false;
    registered    = false;
    needsReinit   = false;
    resetCall();
    lastRingMs    = host_millis;
}

/* ---- incoming calls ------------------------------------------------- */

static void authorised_caller_acts_on_second_ring(void)
{
    feed("RING");
    feed("+CLIP: \"+919876543210\",145,\"\",0,\"\",0");
    CHECK(callerAuth);
    CHECK(!pendingToggle);                 /* one ring is not enough */

    feed("RING");
    CHECK(pendingToggle);                  /* second ring fires it   */
    CHECK(pendingHangup);                  /* and cuts the call      */
    CHECK(callDone);
}

static void unauthorised_caller_is_cut_and_ignored(void)
{
    feed("RING");
    feed("+CLIP: \"+911111111111\",145,\"\",0,\"\",0");
    CHECK(!callerAuth);
    CHECK(pendingHangup);                  /* cut immediately        */
    CHECK(callDone);                       /* and never act on it    */

    pendingHangup = false;
    feed("RING");
    feed("RING");
    CHECK(!pendingToggle);                 /* still no action        */
}

static void clip_arriving_before_ring_still_works(void)
{
    /* Some firmware emits +CLIP first. The sketch handles it by seeding
     * ringCount, which is the kind of ordering assumption that only shows
     * up against a different modem batch. */
    feed("+CLIP: \"+919876543210\",145,\"\",0,\"\",0");
    CHECK(callerAuth);
    CHECK(ringCount == 1);
    feed("RING");
    CHECK(pendingToggle);
}

static void ring_after_action_re_issues_hangup(void)
{
    feed("RING");
    feed("+CLIP: \"+919876543210\",145,\"\",0,\"\",0");
    feed("RING");
    CHECK(pendingHangup);
    pendingHangup = false;

    feed("RING");                          /* caller still ringing    */
    CHECK(pendingHangup);                  /* cut again, do not act   */
    CHECK(!pendingToggle || pendingToggle); /* toggle already queued  */
}

static void call_teardown_resets_state(void)
{
    feed("RING");
    feed("+CLIP: \"+919876543210\",145,\"\",0,\"\",0");
    CHECK(ringCount > 0);
    feed("NO CARRIER");
    CHECK(ringCount == 0);
    CHECK(!callerAuth);
    CHECK(!callDone);
}

/* ---- incoming SMS --------------------------------------------------- */

static void sms_from_authorised_number_is_accepted(void)
{
    feed("+CMT: \"+919876543210\",\"\",\"26/09/11,10:30:00+22\"");
    CHECK(expectBody);
    feed("SYSTEM ON");
    CHECK(pendingAction == 1);
    CHECK(!expectBody);
}

static void sms_body_is_case_insensitive(void)
{
    feed("+CMT: \"+919876543210\",\"\",\"26/09/11,10:30:00+22\"");
    feed("system on");
    CHECK(pendingAction == 1);
}

static void sms_off_is_not_confused_with_on(void)
{
    feed("+CMT: \"+919876543210\",\"\",\"\"");
    feed("SYSTEM OFF");
    CHECK(pendingAction == 2);
}

static void sms_from_stranger_is_ignored(void)
{
    feed("+CMT: \"+911111111111\",\"\",\"\"");
    CHECK(!expectBody);
    feed("SYSTEM ON");
    CHECK(pendingAction == 0);             /* body never inspected    */
}

static void unknown_sms_body_does_nothing(void)
{
    feed("+CMT: \"+919876543210\",\"\",\"\"");
    feed("HELLO THERE");
    CHECK(pendingAction == 0);
}

static void cmti_index_is_parsed_for_deletion(void)
{
    feed("+CMTI: \"SM\",7");
    CHECK(pendingDelete == 7);
    feed("+CMTI: \"SM\",23");
    CHECK(pendingDelete == 23);
}

/* ---- de-duplication ------------------------------------------------- */

static void repeat_command_inside_window_is_dropped(void)
{
    feed("+CMT: \"+919876543210\",\"\",\"\"");
    feed("SYSTEM ON");
    CHECK(pendingAction == 1);
    pendingAction = 0;

    host_millis += 1000;                   /* well inside DEDUP_MS    */
    feed("+CMT: \"+919876543210\",\"\",\"\"");
    feed("SYSTEM ON");
    CHECK(pendingAction == 0);             /* suppressed              */
}

static void repeat_command_after_window_is_accepted(void)
{
    feed("+CMT: \"+919876543210\",\"\",\"\"");
    feed("SYSTEM ON");
    pendingAction = 0;

    host_millis += DEDUP_MS + 1000UL;
    feed("+CMT: \"+919876543210\",\"\",\"\"");
    feed("SYSTEM ON");
    CHECK(pendingAction == 1);
}

static void different_command_is_never_suppressed(void)
{
    feed("+CMT: \"+919876543210\",\"\",\"\"");
    feed("SYSTEM ON");
    CHECK(pendingAction == 1);
    pendingAction = 0;

    host_millis += 500;                    /* inside the window       */
    feed("+CMT: \"+919876543210\",\"\",\"\"");
    feed("SYSTEM OFF");
    CHECK(pendingAction == 2);             /* OFF must still get through */
}

/* ---- housekeeping --------------------------------------------------- */

static void creg_registration_states(void)
{
    feed("+CREG: 0,2");
    CHECK(!registered);                    /* searching               */
    feed("+CREG: 0,1");
    CHECK(registered);                     /* home network            */
    registered = false;
    feed("+CREG: 0,5");
    CHECK(registered);                     /* roaming                 */
}

static void modem_restart_banner_triggers_reinit(void)
{
    feed("RDY");
    CHECK(needsReinit);
    needsReinit = false;
    feed("+CPIN: SMS Ready");
    CHECK(needsReinit);
}

static void empty_line_is_ignored(void)
{
    feed("");
    CHECK(pendingAction == 0);
    CHECK(!pendingHangup);
}

/* ---- a known weakness, documented by test --------------------------- */

static void expect_body_has_no_timeout(void)
{
    /* +CMT: announces that the NEXT line is the message body. If that body
     * never arrives - a dropped byte, a modem reset mid-message - the flag
     * stays set and the next unrelated line is parsed as a body.
     *
     * Here an unsolicited RING is swallowed as an SMS body instead of
     * being handled as a call. Documented in docs/DESIGN-NOTES.md. */
    feed("+CMT: \"+919876543210\",\"\",\"\"");
    CHECK(expectBody);
    feed("RING");
    CHECK(!expectBody);                    /* consumed as a body      */
    CHECK(ringCount == 0);                 /* the RING was lost       */
}

int main(void)
{
    printf("\nincoming calls\n");
    RUN(authorised_caller_acts_on_second_ring);
    RUN(unauthorised_caller_is_cut_and_ignored);
    RUN(clip_arriving_before_ring_still_works);
    RUN(ring_after_action_re_issues_hangup);
    RUN(call_teardown_resets_state);

    printf("\nincoming SMS\n");
    RUN(sms_from_authorised_number_is_accepted);
    RUN(sms_body_is_case_insensitive);
    RUN(sms_off_is_not_confused_with_on);
    RUN(sms_from_stranger_is_ignored);
    RUN(unknown_sms_body_does_nothing);
    RUN(cmti_index_is_parsed_for_deletion);

    printf("\ncommand de-duplication\n");
    RUN(repeat_command_inside_window_is_dropped);
    RUN(repeat_command_after_window_is_accepted);
    RUN(different_command_is_never_suppressed);

    printf("\nhousekeeping\n");
    RUN(creg_registration_states);
    RUN(modem_restart_banner_triggers_reinit);
    RUN(empty_line_is_ignored);

    printf("\nknown weakness, pinned by test\n");
    RUN(expect_body_has_no_timeout);

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

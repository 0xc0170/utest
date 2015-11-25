/****************************************************************************
 * Copyright (c) 2015, ARM Limited, All Rights Reserved
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ****************************************************************************
 */

 #include "utest/harness.h"
 #include "minar/minar.h"
 #include "core-util/CriticalSectionLock.h"

using namespace utest::v1;


namespace
{
    const Case *test_cases = NULL;
    size_t test_length = 0;

    size_t test_index_of_case = 0;

    size_t test_passed = 0;
    size_t test_failed = 0;

    const Case *case_current = NULL;
    control_t case_control = control_t();
    size_t case_repeat_count = 0;

    minar::callback_handle_t case_timeout_handle = NULL;

    size_t case_passed = 0;
    size_t case_failed = 0;
    size_t case_failed_before = 0;

    handlers_t defaults = default_handlers;
    handlers_t handlers = defaults;
}

static void die() {
    while(1) ;
}

void Harness::run(const Specification specification)
{
    mbed::util::CriticalSectionLock lock;

    test_cases = specification.cases;
    test_length = specification.length;
    defaults = specification.defaults;
    handlers.test_setup = defaults.get_handler(specification.setup_handler);
    handlers.test_teardown = defaults.get_handler(specification.teardown_handler);

    test_index_of_case = 0;
    test_passed = 0;
    test_failed = 0;

    case_passed = 0;
    case_failed = 0;
    case_failed_before = 0;
    case_current = test_cases;

    if (handlers.test_setup && (handlers.test_setup(test_length) != STATUS_CONTINUE)) {
        if (handlers.test_teardown) handlers.test_teardown(0, 0, FAILURE_SETUP);
        die();
    }

    minar::Scheduler::postCallback(run_next_case);
}

void Harness::raise_failure(failure_t reason)
{
    mbed::util::CriticalSectionLock lock;

    case_failed++;
    status_t fail_status = STATUS_ABORT;

    if (handlers.case_failure) fail_status = handlers.case_failure(case_current, reason);

    if (fail_status != STATUS_CONTINUE || reason == FAILURE_SETUP) {
        if (handlers.case_teardown && reason != FAILURE_TEARDOWN) {
            status_t teardown_status = handlers.case_teardown(case_current, case_passed, case_failed, reason);
            if (teardown_status != STATUS_CONTINUE) {
                raise_failure(FAILURE_TEARDOWN);
            }
            else handlers.case_teardown = NULL;
        }
    }
    if (fail_status != STATUS_CONTINUE) {
        test_failed++;
        if (handlers.test_teardown) handlers.test_teardown(test_passed, test_failed, reason);
        die();
    }
}

void Harness::schedule_next_case()
{
    if (case_failed_before == case_failed) case_passed++;

    if(case_control.repeat != REPEAT_CASE_ONLY) {
        if (handlers.case_teardown &&
            (handlers.case_teardown(case_current, case_passed, case_failed,
                                     case_failed ? FAILURE_CASES : FAILURE_NONE) != STATUS_CONTINUE)) {
            raise_failure(FAILURE_TEARDOWN);
        }
    }

    if(case_control.repeat == REPEAT_NO_REPEAT) {
        if (case_failed > 0) test_failed++;
        else test_passed++;

        case_control = control_t();
        case_current++;
        case_passed = 0;
        case_failed = 0;
        case_failed_before = 0;
        case_repeat_count = 0;
    }
    minar::Scheduler::postCallback(run_next_case);
}

void Harness::handle_timeout()
{
    mbed::util::CriticalSectionLock lock;

    if (case_timeout_handle != NULL)
    {
        raise_failure(FAILURE_TIMEOUT);
        case_timeout_handle = NULL;
        schedule_next_case();
    }
}

void Harness::validate_callback()
{
    mbed::util::CriticalSectionLock lock;

    if (case_timeout_handle != NULL)
    {
        minar::Scheduler::cancelCallback(case_timeout_handle);
        case_timeout_handle = NULL;
        schedule_next_case();
    }
}

bool Harness::is_busy()
{
    mbed::util::CriticalSectionLock lock;
    if (!test_cases)   return false;
    if (!case_current) return false;

    return (case_current < (test_cases + test_length));
}

void Harness::run_next_case()
{
    mbed::util::CriticalSectionLock lock;

    if(case_current < (test_cases + test_length))
    {
        handlers.case_setup    = defaults.get_handler(case_current->setup_handler);
        handlers.case_teardown = defaults.get_handler(case_current->teardown_handler);
        handlers.case_failure  = defaults.get_handler(case_current->failure_handler);

        if (case_current->is_empty()) {
            raise_failure(FAILURE_EMPTY_CASE);
            schedule_next_case();
            return;
        }

        if ((!case_failed && !case_passed) || case_control.repeat == REPEAT_ALL) {
            uint32_t index_of_case = test_index_of_case;
            if (case_control.repeat == REPEAT_NO_REPEAT) test_index_of_case++;
            if (handlers.case_setup && (handlers.case_setup(case_current, index_of_case) != STATUS_CONTINUE)) {
                raise_failure(FAILURE_SETUP);
                schedule_next_case();
                return;
            }
        }

        case_failed_before = case_failed;

        if (case_current->handler) {
            case_current->handler();
        } else if (case_current->control_handler) {
            case_control = case_current->control_handler();
        } else if (case_current->repeat_count_handler) {
            case_control = case_current->repeat_count_handler(case_repeat_count);
        }
        case_repeat_count++;

        if (case_control.timeout != uint32_t(-1)) {
            case_timeout_handle = minar::Scheduler::postCallback(handle_timeout)
                                            .delay(minar::milliseconds(case_control.timeout))
                                            .getHandle();
        }
        else {
            schedule_next_case();
        }
    }
    else if (handlers.test_teardown) {
        handlers.test_teardown(test_passed, test_failed, test_failed ? FAILURE_CASES : FAILURE_NONE);
        die();
    }
}
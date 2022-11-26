#include "signals.h"
#include "Commands.h"
#include <iostream>
#include <signal.h>

using namespace std;

void ctrlZHandler(int sig_num)
{
    SmallShell& smash = SmallShell::getInstance();
    if (smash.wait_job_pid != -1) {
        kill(smash.wait_job_pid, SIGSTOP);
    }
}

void ctrlCHandler(int sig_num)
{
    // exit for now TODO: delete later
    exit(0);
}

void alarmHandler(int sig_num)
{
    // TODO: Add your implementation
}

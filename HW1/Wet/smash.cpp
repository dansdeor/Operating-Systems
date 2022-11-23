#include "Commands.h"
#include "signals.h"
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
    if (signal(SIGTSTP, ctrlZHandler) == SIG_ERR) {
        perror("smash error: failed to set ctrl-Z handler");
    }
    if (signal(SIGINT, ctrlCHandler) == SIG_ERR) {
        perror("smash error: failed to set ctrl-C handler");
    }
    // TODO: setup sig alarm handler

    SmallShell& smash = SmallShell::getInstance();
    while (true) {
        smash.printPrompt();
        std::string cmd_line;
        std::getline(std::cin, cmd_line);
        smash.executeCommand(cmd_line.c_str());
        if (smash.waitpid != -1) {
            waitpid(smash.waitpid, NULL, 0);
            smash.waitpid = -1;
        }
    }
    return 0;
}
#include "Commands.h"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace std;

const std::string WHITESPACE = " \n\r\t\f\v";

#if 0
#define FUNC_ENTRY() \
    cout << __PRETTY_FUNCTION__ << " --> " << endl;

#define FUNC_EXIT() \
    cout << __PRETTY_FUNCTION__ << " <-- " << endl;
#else
#define FUNC_ENTRY()
#define FUNC_EXIT()
#endif

string _ltrim(const std::string& s)
{
    size_t start = s.find_first_not_of(WHITESPACE);
    return (start == std::string::npos) ? "" : s.substr(start);
}

string _rtrim(const std::string& s)
{
    size_t end = s.find_last_not_of(WHITESPACE);
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

string _trim(const std::string& s)
{
    return _rtrim(_ltrim(s));
}

int _parseCommandLine(const char* cmd_line, char** args)
{
    FUNC_ENTRY()
    int i = 0;
    std::istringstream iss(_trim(string(cmd_line)).c_str());
    for (std::string s; iss >> s;) {
        args[i] = (char*)malloc(s.length() + 1);
        memset(args[i], 0, s.length() + 1);
        strcpy(args[i], s.c_str());
        args[++i] = NULL;
    }
    return i;
    FUNC_EXIT()
}

void _freeArgs(char** args)
{
    for (size_t i = 0; i < COMMAND_MAX_ARGS && args[i]; i++) {
        free(args[i]);
    }
}

bool _isBackgroundComamnd(const char* cmd_line)
{
    const string str(cmd_line);
    return str[str.find_last_not_of(WHITESPACE)] == '&';
}

void _removeBackgroundSign(char* cmd_line)
{
    const string str(cmd_line);
    // find last character other than spaces
    unsigned int idx = str.find_last_not_of(WHITESPACE);
    // if all characters are spaces then return
    if (idx == string::npos) {
        return;
    }
    // if the command line does not end with & then return
    if (cmd_line[idx] != '&') {
        return;
    }
    // replace the & (background sign) with space and then remove all tailing spaces.
    cmd_line[idx] = ' ';
    // truncate the command line string up to the last non-space character
    cmd_line[str.find_last_not_of(WHITESPACE, idx) + 1] = 0;
}

char* _cmd_line_copy(const char* cmd_line)
{
    size_t len = strlen(cmd_line) + 1;
    char* copied_cmd_line = (char*)malloc(len);
    if (copied_cmd_line) {
        memcpy(copied_cmd_line, cmd_line, len);
    } else {
        perror("smash error: malloc failed");
    }
    return copied_cmd_line;
}

Command::Command(const char* cmd_line)
{
    m_args_num = _parseCommandLine(cmd_line, m_args);
}

Command::~Command()
{
    _freeArgs(m_args);
}

/**
 * BuiltInCommands implementations
 */

void ChangePromptCommand::execute()
{
    SmallShell& smash = SmallShell::getInstance();
    if (m_args_num >= 2)
        smash.changePromptName(m_args[1]);
    else
        // default change to "smash"
        smash.changePromptName();
}

void ShowPidCommand::execute()
{
    cout << "smash pid is " << getpid() << endl;
}

void GetCurrDirCommand::execute()
{
    char* cwd = get_current_dir_name();
    if (cwd) {
        cout << cwd << endl;
        free(cwd);
    } else {
        perror("smash error: getcwd failed");
    }
}

void ChangeDirCommand::execute()
{
    if (m_args_num != 2) {
        cerr << "smash error: cd: too many arguments" << endl;
        return;
    }
    SmallShell& smash = SmallShell::getInstance();
    string new_path;
    if (strcmp(m_args[1], "-") == 0) {
        if (!smash.old_pwd) {
            cerr << "smash error: cd: OLDPWD not set" << endl;
            return;
        }
        new_path = smash.old_path;
    } else {
        new_path = m_args[1];
    }
    char* cwd = get_current_dir_name();
    if (cwd == nullptr) {
        perror("smash error: getcwd failed");
    }
    smash.old_path = cwd;
    free(cwd);

    if (chdir(new_path.c_str()) == -1) {
        perror("smash error: chdir failed");
        return;
    }
    smash.old_pwd = true;
}

/**
 * ExternalCommand implementation
 */
void ExternalCommand::execute()
{
    SmallShell& smash = SmallShell::getInstance();
    if (_isBackgroundComamnd(m_cmd_line.c_str())) {
        char* cmd_line = _cmd_line_copy(m_cmd_line.c_str());
        _removeBackgroundSign(cmd_line);
        char* args[COMMAND_MAX_ARGS];
        _parseCommandLine(cmd_line, args);
        free(cmd_line);
        size_t pid = fork();
        if (pid == 0) {
            // child
            setpgrp();
            execvp(args[0], args);
            perror("smash error: execvp failed");
        } else if (pid > 0) {
            // parent
            smash.jobs_list.addJob(JobEntry(m_cmd_line, pid));
            _freeArgs(args);
        } else {
            perror("smash error: fork failed");
        }
    } else {
        size_t pid = fork();
        if (pid == 0) {
            // child
            setpgrp();
            execvp(m_args[0], m_args);
            perror("smash error: execvp failed");
        } else if (pid > 0) {
            // parent
            smash.foreground_job = JobEntry(m_cmd_line, pid);
            smash.waitpid = pid;
        } else {
            perror("smash error: fork failed");
        }
    }
}

/**
 * Creates and returns a pointer to Command class which matches the given command line (cmd_line)
 */
Command* SmallShell::CreateCommand(const char* cmd_line)
{
    string cmd_s = _trim(string(cmd_line));
    string firstWord = cmd_s.substr(0, cmd_s.find_first_of(" \n"));

    if (firstWord.compare("chprompt") == 0) {
        return new ChangePromptCommand(cmd_line);
    } else if (firstWord.compare("showpid") == 0) {
        return new ShowPidCommand(cmd_line);
    } else if (firstWord.compare("pwd") == 0) {
        return new GetCurrDirCommand(cmd_line);
    } else if (firstWord.compare("cd") == 0) {
        return new ChangeDirCommand(cmd_line);
    } else if (firstWord.length()) {
        return new ExternalCommand(cmd_line);
    }
    return nullptr;
}

/**
 * JobsList Implementation
 */
JobEntry::JobEntry(string cmd_line, pid_t pid)
    : cmd_line(cmd_line)
    , job_id(-1) // TODO: change the job id later after inserting to the list
    , pid(pid)
    , stopped(false)
{
    time(&time_epoch);
}

void JobsList::addJob(const JobEntry& job)
{
    // gets the job id of the last member in the map and append by 1
    int job_id = (m_job_list.size() == 0) ? 1 : m_job_list.rbegin()->first + 1;
    m_job_list[job_id] = job;
    if (job.job_id == -1) {
        m_job_list[job_id].job_id = job_id;
    }
}

void ::JobsList::printJobsList()
{
    removeFinishedJobs();
    time_t present;
    time(&present);
    for (auto& it : m_job_list) {
        const JobEntry& job = it.second;
        int passed_time = static_cast<int>(difftime(present, job.time_epoch));
        cout << "[" << job.job_id << "]" << job.cmd_line << " : " << job.pid << " " << passed_time << " secs";
        cout << ((job.stopped) ? " (stopped)" : "") << endl;
    }
}

void JobsList::removeFinishedJobs()
{
    int status;
    for (auto it = m_job_list.begin(); it != m_job_list.end(); it++) {
        waitpid(it->second.pid, &status, WNOHANG);
        if (WIFEXITED(status)) {
            it = m_job_list.erase(it);
        }
    }
}

void SmallShell::executeCommand(const char* cmd_line)
{
    Command* cmd = CreateCommand(cmd_line);
    if (cmd) {
        cmd->execute();
        delete cmd;
    }
}

void SmallShell::changePromptName(const std::string&& prompt_name)
{
    m_prompt_name = prompt_name;
}

void SmallShell::printPrompt()
{
    std::cout << m_prompt_name << "> ";
}

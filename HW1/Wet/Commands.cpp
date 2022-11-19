#include <unistd.h>
#include <string.h>
#include <iostream>
#include <vector>
#include <sstream>
#include <sys/wait.h>
#include <iomanip>
#include "Commands.h"

using namespace std;

const std::string WHITESPACE = " \n\r\t\f\v";

#if 0
#define FUNC_ENTRY()  \
  cout << __PRETTY_FUNCTION__ << " --> " << endl;

#define FUNC_EXIT()  \
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

int _parseCommandLine(const char* cmd_line, char** args) {
  FUNC_ENTRY()
  int i = 0;
  std::istringstream iss(_trim(string(cmd_line)).c_str());
  for(std::string s; iss >> s; ) {
    args[i] = (char*)malloc(s.length()+1);
    memset(args[i], 0, s.length()+1);
    strcpy(args[i], s.c_str());
    args[++i] = NULL;
  }
  return i;
  FUNC_EXIT()
}

bool _isBackgroundComamnd(const char* cmd_line) {
  const string str(cmd_line);
  return str[str.find_last_not_of(WHITESPACE)] == '&';
}

void _removeBackgroundSign(char* cmd_line) {
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

/**
* BuiltInCommands implementations 
*/
BuiltInCommand::BuiltInCommand(const char* cmd_line):Command(cmd_line){
  m_args_num = _parseCommandLine(cmd_line, m_args);
}

void ChangePromptCommand::execute(){
  SmallShell& smash = SmallShell::getInstance();
  if(m_args_num >=2)
    smash.changePromptName(m_args[1]);
  else
    // default change to "smash"
    smash.changePromptName();
}

void ShowPidCommand::execute(){
  cout << "smash pid is " << getpid() << endl;
}

void GetCurrDirCommand::execute(){
  char* cwd = get_current_dir_name();
  if(cwd){
  cout << cwd << endl;
  free(cwd);
  }
  else{
    perror("smash error: getcwd failed");
  }
}

void ChangeDirCommand::execute(){
  if (m_args_num != 2){
    cerr << "smash error: cd: too many arguments" << endl;
    return;
  }
  SmallShell& smash = SmallShell::getInstance();
  string new_path;
  if(strcmp(m_args[1], "-") == 0){
    if(!smash.old_pwd){
      cerr << "smash error: cd: OLDPWD not set" << endl;
    return;
    }
    new_path = smash.old_path;
  }
  else{
    new_path = m_args[1];
  }
  char* cwd = get_current_dir_name();
  if(cwd==nullptr){
    perror("smash error: getcwd failed");
  }
  smash.old_path = cwd;
  free(cwd);

  if(chdir(new_path.c_str())==-1){
    perror("smash error: chdir failed");
    return;
  }
  smash.old_pwd = true;
}

/**
* Creates and returns a pointer to Command class which matches the given command line (cmd_line)
*/
Command * SmallShell::CreateCommand(const char* cmd_line) {
  string cmd_s = _trim(string(cmd_line));
  string firstWord = cmd_s.substr(0, cmd_s.find_first_of(" \n"));

  if(firstWord.compare("chprompt") == 0){
    return new ChangePromptCommand(cmd_line);
  }
  else if (firstWord.compare("showpid") == 0) {
    return new ShowPidCommand(cmd_line);
  }
  else if (firstWord.compare("pwd") == 0) {
    return new GetCurrDirCommand(cmd_line);
  }
  else if (firstWord.compare("cd") == 0) {
    return new ChangeDirCommand(cmd_line);
  }
  /*
  else {
    return new ExternalCommand(cmd_line);
  }
  */
  return nullptr;
}

void SmallShell::executeCommand(const char *cmd_line) {
  Command* cmd = CreateCommand(cmd_line);
  if(cmd){
    cmd->execute();
    delete cmd;
  }
}
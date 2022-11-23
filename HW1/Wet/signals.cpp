#include <iostream>
#include <signal.h>
#include "signals.h"
#include "Commands.h"

using namespace std;

void ctrlZHandler(int sig_num) {
	// TODO: Add your implementation
  cout<< "bla bla" <<endl;
}

void ctrlCHandler(int sig_num) {
  // TODO: Add your implementation
  //exit for now TODO: delete later
  exit(0);
}

void alarmHandler(int sig_num) {
  // TODO: Add your implementation
}


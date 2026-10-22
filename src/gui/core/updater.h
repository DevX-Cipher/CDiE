#ifndef CDIE_UPDATER_H
#define CDIE_UPDATER_H

enum { UPD_IDLE, UPD_CHECKING, UPD_READY, UPD_UPTODATE, UPD_RUNNING, UPD_DONE, UPD_ERROR };

typedef struct Updater {
  volatile int phase, pct, cancel, busy, db_changed;
  char installed[32], latest[32], url[512], label[48], error[200];
  int added, changed;

  char main_db[1024], extra_db[1024], custom_db[1024];
  void (*notify)(void* user);
  void* user;
  void* thread;
  int job;
} Updater;

void updater_init(Updater* u, void (*notify)(void*), void* user);
void updater_check(Updater* u);     
void updater_install(Updater* u);    
void updater_cancel(Updater* u);
void updater_shutdown(Updater* u); 

#endif
#ifndef CDIE_CONFIG_H
#define CDIE_CONFIG_H

typedef struct {
  char main_db[1024];
  char extra_db[1024];
  char custom_db[1024];
  char db_version[32];
} CdieConfig;

int config_set_db_version(const char* version);
int config_user_dir(char* out, size_t cap);
int config_is_portable(void);
int config_db_required(void);
int config_load(CdieConfig* cfg);

int config_save(const CdieConfig* cfg);

const char* config_file_path(void);


#endif
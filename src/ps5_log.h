#ifndef TSP5_LOG_H
#define TSP5_LOG_H

/* A failed log file open does not stop the payload; stderr stays available. */
int ps5_log_init(void);
int ps5_log_init_error(void);
int ps5_log_remote_dup(void);
long long ps5_log_file_size(void);
void ps5_log_close(void);
void ps5_log(const char *level, const char *format, ...);

#endif

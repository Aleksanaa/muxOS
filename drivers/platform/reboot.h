typedef void (*power_off)(void);
typedef void (*shutdown)(void);
typedef void (*halt)(void);
typedef void (*restart)(void);
struct machine_ops {
  power_off power_off;
  shutdown shutdown;
  halt halt;
  restart restart;
};
void machine_power_off(void);
void machine_halt(void);
void machine_restart(void);
void machine_shutdown(void);
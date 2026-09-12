#include "reboot.h"
#include "io.h"
#include "vga.h"
void native_machine_power_off(void) { asm volatile("cli"); }

void native_machine_halt(void) {
  for (;;)
    asm volatile("hlt");
}

void native_machine_restart(void) {
  print("restart load.\n", 0);
  outb(0xCF9, 0x0E);
}

void native_machine_shutdown(void) {
  asm volatile("cli");

  outw(0x604, 0x2000);

  // Stop here if the shutdown request is not handled.
  for (;;) {
    asm volatile("hlt");
  }
}

struct machine_ops machine_ops = {.halt = native_machine_halt,
                                  .restart = native_machine_restart,
                                  .power_off = native_machine_power_off,
                                  .shutdown = native_machine_shutdown};
void machine_power_off(void) {
  print("machine power off.", 0);
  machine_ops.power_off();
}
void machine_shutdown(void) {
  print("machine shutdown.", 0);
  machine_ops.shutdown();
}
void machine_halt(void) {
  print("machine halt.", 0);
  machine_ops.halt();
}
void machine_restart(void) {
  print("machine restart.", 0);
  machine_ops.restart();
}

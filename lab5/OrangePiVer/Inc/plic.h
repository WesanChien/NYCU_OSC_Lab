#ifndef PLIC_H
#define PLIC_H

void plic_init(unsigned long hart_id);
unsigned int plic_claim(void);
void plic_complete(unsigned int irq);
void enable_external_interrupt(void);

#endif
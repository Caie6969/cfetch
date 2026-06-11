#ifndef CFETCH_SETUP_H
#define CFETCH_SETUP_H

#include "config.h"

/* Run the interactive first-launch setup wizard.
 * Populates cfg and saves it. Returns 0 on success. */
int setup_wizard(cfetch_config_t *cfg);

#endif /* CFETCH_SETUP_H */

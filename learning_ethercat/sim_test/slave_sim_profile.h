#ifndef SLAVE_SIM_PROFILE_H
#define SLAVE_SIM_PROFILE_H

/*
 * Bridges the drive-profile registry to the virtual slave, so the simulated bus
 * can impersonate any supported drive family.
 *
 * Kept separate from slave_sim.c on purpose: the virtual slave itself must stay
 * free of dependencies on src/drive, so the low-level stack tests can link it
 * on its own.
 */

int slavesim_impersonate(const char *profile_id);

#endif /* SLAVE_SIM_PROFILE_H */

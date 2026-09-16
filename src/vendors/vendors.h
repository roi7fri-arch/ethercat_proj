#ifndef VENDORS_H
#define VENDORS_H

/*
 * Single entry point that pulls every supported drive family into the build.
 *
 * Adding a family is: create src/vendors/<vendor>/, write one .c that defines a
 * drive_profile_t and a <vendor>_register_profiles() function, then add one
 * line below. The Makefile needs no change - src/vendors/vendors.mk globs the
 * directory.
 *
 * This file is the only place where the core is allowed to learn that specific
 * vendors exist, and even here it only learns their registration functions.
 */

/* Register all built-in drive families. Call once at start-up, before the
 * configuration is applied. Returns 0 on success. */
int vendors_register_all(void);

#endif /* VENDORS_H */

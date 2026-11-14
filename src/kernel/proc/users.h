#ifndef KERNEL_USERS_H
#define KERNEL_USERS_H

/* Switches the running process to the account. password 0 means none given,
 * which only root may do; a given password is always checked. */
int users_login(const char *name, const char *password);
/* Root may set anyone's password; others only their own, with the old one. */
int users_set_password(const char *name, const char *old_password, const char *new_password);

#endif

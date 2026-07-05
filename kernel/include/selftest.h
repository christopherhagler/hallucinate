/*
 * selftest.h - in-kernel boot-time self-tests.
 */
#ifndef HL_SELFTEST_H
#define HL_SELFTEST_H

/*
 * Run the kernel self-test suite. Panics on the first failure; on success
 * prints "selftest: passed (N assertions)", which the QEMU integration
 * harness asserts on. Cheap enough to run on every boot.
 */
void selftest_run(void);

#endif /* HL_SELFTEST_H */

#ifndef SBI_H
#define SBI_H

#define SBI_EXT_BASE 0x10
#define SBI_EXT_TIME 0x54494D45
#define SBI_EXT_LEGACY_SET_TIMER 0x00

#define SBI_EXT_TIME_SET_TIMER 0

#define SBI_EXT_BASE_GET_SPEC_VERSION 0
#define SBI_EXT_BASE_GET_IMP_ID 1
#define SBI_EXT_BASE_GET_IMP_VERSION 2
#define SBI_EXT_BASE_PROBE_EXT 3

struct sbiret {
    long error;
    long value;
};

struct sbiret sbi_ecall(long ext,
                        long fid,
                        unsigned long arg0,
                        unsigned long arg1,
                        unsigned long arg2,
                        unsigned long arg3,
                        unsigned long arg4,
                        unsigned long arg5);

long sbi_get_spec_version(void);
long sbi_get_impl_id(void);
long sbi_get_impl_version(void);
long sbi_probe_extension(long extid);

long sbi_set_timer(unsigned long stime_value);
void print_info(void);

#endif
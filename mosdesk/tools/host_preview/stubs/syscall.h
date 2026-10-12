#pragma once
/* Host stub of the Minimal-OS SDK syscall header: only what MOSdesk uses. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define SYS_SUCCESS 0ull
#define SYS_ERR_AGAIN ((uint64_t)-2)
#define SYS_ERR_BUSY ((uint64_t)-3)
#define SYS_OPEN 10u
#define SYS_READ 11u
#define SYS_CLOSE 12u
#define SYS_SIZE 13u
#define SYS_SEEK 14u
#define SYS_FWRITE 15u
#define SYS_GETTIME 16u
#define SYS_UPTIME 4u
#define SYS_HEAP 17u
#define SYS_O_RDONLY 0u
uint64_t mos_syscall(uint64_t n, uint64_t a, uint64_t b, uint64_t c);
uint64_t mos_syscall1(uint64_t n, uint64_t a);
uint64_t mos_syscall3(uint64_t n, uint64_t a, uint64_t b, uint64_t c);

/* kj-httpd: syscall layer for freestanding x86_64 linux */
/* function args: rdi, rsi, rdx, rcx, r8, r9 */
/* syscall args:  rdi, rsi, rdx, r10, r8, r9 */
/*                              ^^^         */

.intel_syntax noprefix

/* generic wrapper: moves 4th arg rcx into r10, sets nr, enters kernel */
.macro SC name, nr
.global \name
\name:
  mov r10, rcx
  mov eax, \nr
  syscall
  ret
.endm

SC k_read, 0
SC k_write, 1
SC k_open, 2
SC k_close, 3
SC k_stat, 4
SC k_fstat, 5
SC k_poll, 7
SC k_lseek, 8
SC k_rt_sigaction, 13
SC k_access, 21
SC k_pipe, 22
SC k_dup2, 33
SC k_alarm, 37
SC k_socket, 41
SC k_connect, 42
SC k_accept, 43
SC k_sendto, 44
SC k_recvfrom, 45
SC k_shutdown, 48
SC k_bind, 49
SC k_listen, 50
SC k_getpeername, 52
SC k_setsockopt, 54
SC k_fork, 57
SC k_execve, 59
SC k_exit, 60
SC k_wait4, 61
SC k_getcwd, 79
SC k_chdir, 80
SC k_gettimeofday, 96
SC k_setuid, 105
SC k_setgid, 106
SC k_setsid, 112
SC k_setgroups, 116
SC k_clock_gettime, 228
SC k_getrandom, 318

/* trampoline the kernel returns to after a handled signal */
.global k_sigreturn
k_sigreturn:
  mov eax, 15
  syscall

.global _start
_start:
  xor rbp, rbp
  pop rdi                     /* argc */
  mov rsi, rsp                /* argv */
  lea rdx, [rsi + rdi*8 + 8]  /* envp */
  sub rsp, 8                  /* align the stack for the call */
  call main
  mov edi, eax
  call k_exit

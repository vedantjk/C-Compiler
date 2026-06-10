# Helper for infinite_loop.c: exit_wrapper(status) forwards to libc exit().
# Committed as .s (not .c) so the test runner does not pick it up as a program.
    .text
    .globl exit_wrapper
exit_wrapper:
    subq $8, %rsp          # realign the stack to 16 before the call
    call exit@PLT          # status is already in %edi; exit() never returns

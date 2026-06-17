.equ TF_RA,       0
.equ TF_SP,       8
.equ TF_GP,       16
.equ TF_TP,       24
.equ TF_T0,       32
.equ TF_T1,       40
.equ TF_T2,       48
.equ TF_S0,       56
.equ TF_S1,       64
.equ TF_A0,       72
.equ TF_A1,       80
.equ TF_A2,       88
.equ TF_A3,       96
.equ TF_A4,       104
.equ TF_A5,       112
.equ TF_A6,       120
.equ TF_A7,       128
.equ TF_S2,       136
.equ TF_S3,       144
.equ TF_S4,       152
.equ TF_S5,       160
.equ TF_S6,       168
.equ TF_S7,       176
.equ TF_S8,       184
.equ TF_S9,       192
.equ TF_S10,      200
.equ TF_S11,      208
.equ TF_T3,       216
.equ TF_T4,       224
.equ TF_T5,       232
.equ TF_T6,       240
.equ TF_SEPC,     248
.equ TF_SSTATUS,  256
.equ TF_SCAUSE,   264
.equ TF_STVAL,    272
.equ TF_SIZE,     280

.global return_to_user
return_to_user:
    /*
     * a0 = struct trap_frame *
     *     ↓
     * 把 trap_frame 裡的 sepc 寫回 CSR sepc
     *     ↓
     * 把 trap_frame 裡的 sstatus 寫回 CSR sstatus
     *     ↓
     * 把 user registers restore 回 CPU
     *     ↓
     * sret
     */

    mv t6, a0

    ld t0, TF_SSTATUS(t6)
    csrw sstatus, t0

    ld t0, TF_SEPC(t6)
    csrw sepc, t0

    ld ra, TF_RA(t6)
    ld gp, TF_GP(t6)
    ld tp, TF_TP(t6)

    ld t0, TF_T0(t6)
    ld t1, TF_T1(t6)
    ld t2, TF_T2(t6)

    ld s0, TF_S0(t6)
    ld s1, TF_S1(t6)

    ld a0, TF_A0(t6)
    ld a1, TF_A1(t6)
    ld a2, TF_A2(t6)
    ld a3, TF_A3(t6)
    ld a4, TF_A4(t6)
    ld a5, TF_A5(t6)
    ld a6, TF_A6(t6)
    ld a7, TF_A7(t6)

    ld s2, TF_S2(t6)
    ld s3, TF_S3(t6)
    ld s4, TF_S4(t6)
    ld s5, TF_S5(t6)
    ld s6, TF_S6(t6)
    ld s7, TF_S7(t6)
    ld s8, TF_S8(t6)
    ld s9, TF_S9(t6)
    ld s10, TF_S10(t6)
    ld s11, TF_S11(t6)

    ld t3, TF_T3(t6)
    ld t4, TF_T4(t6)
    ld t5, TF_T5(t6)

    ld sp, TF_SP(t6)
    ld t6, TF_T6(t6)

    sret
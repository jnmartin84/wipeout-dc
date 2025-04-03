! KallistiOS ##version##
!
! kernel/libc/koslib/memcpy.s
!
! Copyright (C) 2025 Falco Girgis
!
! Optimized assembly code for specialized memcpy()-like routines.

.text
    .globl _memcpy32

!
! void *memcpy32(void *restrict dst, const void *restrict src, size_t bytes)
!
! r4: dst   (should be 32-byte aligned destination address)
! r5: src   (should be 8-byte aligned source address)
! r6: bytes (number of bytes to copy (should be evenly divisible by 32))
!
! Function entry point + loop prolog
    .align 5
_memcpy32:
    mov     r4, r0              ! Return dst pointer     
    mov     #-5, r3
    shad    r3, r6              ! Right-shift r6 by 5 (dividing by 32)             
    tst     r6, r6
    bt/s    .memcpy32_exit      ! Exit loop if no 32-byte blocks given
    fschg                       ! Swap to double FMOV mode
    mov      #1, r3  
    cmp/eq   r3, r6             
    bt/s     .memcpy32_final    ! Go to final iteration if only 1 block left  
    pref     @r5                ! Prefetch src start

! Middle iterations: at least one more iteration left (so we can prefetch the next)
    .align 4
.memcpy32_middle:  
    movca.l   r0, @r4           ! Preallocate cache line (overwriting existing)  
    dt        r6                ! Decrement + test if r6 is zero       
    fmov.d    @r5+, dr4
    fmov.d    @r5+, dr6   
    fmov.d    @r5+, dr8         ! Load 4 8-byte doubles into FP regs,
    fmov.d    @r5+, dr10        !   incrementing src by 8 bytes each    
    pref      @r5               ! Prefetch next src iteration    
    add       #32, r4           ! Pre-increment dst pointer by 4 doubles    
    fmov.d    dr10, @-r4  
    fmov.d    dr8,  @-r4
    cmp/eq    r3, r6            ! Compare remaining iterations to 1      
    fmov.d    dr6,  @-r4        ! Store 4 8-byte doubles from FP regs,          
    fmov.d    dr4,  @-r4        !   decrementing dst by 8 bytes each
    bf/s     .memcpy32_middle   ! Continue looping until we only have 1 iteration
    add       #32, r4           ! Increment dst to next block

! Final iteration: Just a direct copy, since no prefetching for the next iteration
    .align 4
.memcpy32_final:
    movca.l   r0, @r4           ! Preallocate cache line (overwriting existing)
    fmov.d    @r5+, dr4
    fmov.d    @r5+, dr6
    add       #32,  r4          ! Pre-increment dst pointer by 4 doubles
    fmov.d    @r5+, dr8         ! Load 4 8-byte doubles into FP regs,
    fmov.d    @r5+, dr10        !   incrementing src by 8 bytes each
    fmov.d    dr10, @-r4
    fmov.d    dr8,  @-r4
    fmov.d    dr6,  @-r4        ! Store 4 8-byte doubles from FP regs,
    fmov.d    dr4,  @-r4        !   decrementing dst by 8 bytes each

.memcpy32_exit:
    rts                         
    fschg                       ! Swap back to float FMOVs

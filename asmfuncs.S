! semblies
.text
    .align 5
.globl _xform_all_verts
    .type _xform_all_verts, @function
_xform_all_verts:
!int xform_all_verts(vector_t *output, vector_t *input, int c) {
! arguments:
! r4 = & output[0]
! r5 = & input [0]
! r6 = count
! returns:
! r0 = wtest (1 for all in front of near-z, 0 otherwise)

	! pref first input
! 0
	pref	@r5
! 2
	fmov	fr12, @-r15
! 4
    add     #16, r4
! 6
	fmov	fr13, @-r15
! 8
    mov     #1, r0
! 10
	fmov	fr14, @-r15
! 12
	fmov	@r5+, fr0
! 14
	fmov	@r5+, fr1
! 16
	fmov	@r5+, fr2
! 18
	fldi1	fr3
!20
	add	#4, r5
!22
	dt	r6
!24
	pref	@r5

	! first transform
! 26
	ftrv	xmtrx, fv0
! 28
	bt	.only1Vertex
! 30
	nop

! 32
	fmov	@r5+, fr4
! 34
	fmov	@r5+, fr5
! 36
	fmov	@r5+, fr6
! 38
	fldi1	fr7
! 40
	add	#4, r5
!42
	pref	@r5
!44
	dt	r6
!46
	ftrv	xmtrx, fv4
! 48
	bt	.loopEnd
! 50
	nop
! 52
	bra .loop
! 54
! 56
! 58
! 60
! 62
	nop
	nop
	nop
	nop
	nop
! 64
.loop:
	pref	@r4

	! Load a vector.
! 68
	fmov	@r5+, fr8
! 70
	fmov	@r5+, fr9
!72
	fmov	@r5+, fr10
!74
	fldi1	fr11
!76
	add	#4, r5
!78
	pref	@r5
!80
	! Store a vector.
	ftrv	xmtrx, fv8

    ! w = fr3
!82
	fmov    fr3, @-r4
    ! z = fr2
!84
	fmov    fr2,@-r4
!86
    fneg fr3
!88
	fmov	fr1, @-r4
!90
    fcmp/gt fr3, fr2
!92
	fmov	fr0, @-r4
!94
    movt r7
!96
    add #32,r4
!98
	dt	r6
!100
    and r7, r0
! 102
	bt/s        .firstInLoop
	pref        @r4

	! load another vect
    fmov        @r5+, fr0
    fmov        @r5+, fr1
    fmov        @r5+, fr2
	add			#4, r5
    fldi1       fr3

    pref        @r5

	! Store another vector.
    ftrv        xmtrx, fv0

    ! w
	fmov		fr7, @-r4
    ! z
    fmov        fr6, @-r4
    fneg fr7
    fmov        fr5, @-r4
    fcmp/gt     fr7, fr6
    fmov        fr4, @-r4
    movt r7
    add #32,r4
    dt          r6
    and r7, r0

    bt/s        .secondInLoop
    pref        @r4

   ! Load a vector.
    fmov        @r5+, fr4
    fmov        @r5+, fr5
    fmov        @r5+, fr6
    add         #4, r5
    fldi1       fr7

    pref        @r5

    ! Store a vector.
    ftrv        xmtrx, fv4

	fmov		fr11, @-r4
    fmov        fr10, @-r4
    fneg        fr11
    fmov        fr9, @-r4
    fcmp/gt     fr11, fr10
    fmov        fr8, @-r4
    movt        r7
    add #32,r4
    dt          r6
    and r7, r0

    bf/s        .loop

.loopEnd:
    pref        @r4

	fmov		fr3, @-r4
    fmov        fr2, @-r4
    fneg        fr3
    fmov        fr1, @-r4
    fcmp/gt     fr3, fr2
    fmov        fr0, @-r4
    movt        r7
    add #32,r4
    and r7, r0
	fmov		fr7, @-r4

    fmov        fr6, @-r4
    fneg        fr7
    fmov        fr5, @-r4
    fcmp/gt     fr7, fr6
    fmov       fr4, @-r4
    movt        r7

    fmov        @r15+, fr14
    and r7, r0
    fmov        @r15+, fr13
    rts
    fmov        @r15+, fr12
		
.only1Vertex:
	fmov		fr3, @-r4
    fmov        fr2, @-r4
    fneg        fr3
    fmov        fr1, @-r4
    fcmp/gt     fr3,fr2
    fmov        fr0, @-r4
    movt        r7
    fmov        @r15+, fr14
    and          r7,r0
    fmov        @r15+, fr13
    rts
    fmov        @r15+, fr12

.firstInLoop:
    pref        @r4

	fmov		fr7, @-r4
    fmov        fr6, @-r4
    fneg    fr7
    fmov        fr5, @-r4
    fcmp/gt fr7, fr6
    fmov       fr4, @-r4
    movt    r7
    add #32,r4
    and r7, r0
	fmov		fr11, @-r4
    fmov        fr10, @-r4
    fneg    fr11
    fmov        fr9, @-r4
    fcmp/gt fr11, fr10
    fmov        fr8, @-r4
    movt       r7
    fmov        @r15+, fr14
    and r7,r0
    fmov        @r15+, fr13
    rts
    fmov        @r15+, fr12
	

.secondInLoop:
    pref        @r4

	fmov		fr11, @-r4
    fmov        fr10, @-r4
    fneg    fr11
    fmov        fr9, @-r4
    fcmp/gt fr11, fr10
    fmov        fr8, @-r4
    movt    r7
    add #32,r4
    and r7, r0

	fmov		fr3, @-r4
    fmov        fr2, @-r4
    fneg    fr3
    fmov        fr1, @-r4
    fcmp/gt fr3, fr2
    fmov        fr0, @-r4
    movt    r7
    fmov        @r15+, fr14
    and r7, r0
    fmov        @r15+, fr13
    rts
    fmov        @r15+, fr12
.xform_all_verts_end:
        .size    _xform_all_verts,.xform_all_verts_end - _xform_all_verts


! KallistiOS ##version##
!
! kernel/libc/koslib/memcpy.s
!
! Copyright (C) 2025 Falco Girgis
!
! Optimized assembly code for specialized memcpy()-like routines.

!
! void *memcpy32(void *restrict dst, const void *restrict src, size_t bytes)
!
! r4: dst   (should be 32-byte aligned destination address)
! r5: src   (should be 8-byte aligned source address)
! r6: bytes (number of bytes to copy (should be evenly divisible by 32))
!
! Function entry point + loop prolog
    .align 5
.globl _memcpy32
    .type _memcpy32, @function
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
.memcpy32_end:
        .size    _memcpy32,.memcpy32_end - _memcpy32


! thanks @FalcoGirgis
    .align 5
.globl _fast_mat_apply
    .type _fast_mat_apply, @function

_fast_mat_apply:
            mov        r15, r0
            pref    @r4
            or        #0x0f, r0
            xor     #0x0f, r0
            mov     r15, r7
            fschg
            mov     r0, r15
    
            fmov.d    dr14, @-r15
            fmov.d    dr12, @-r15
    
            fmov.d    @r4, dr0
            add        #32, r4
            pref    @r4
            add        #-(32-8), r4
            fmov.d    @r4+, dr2
            fmov.d    @r4+, dr4
            fmov.d    @r4+, dr6

            ftrv    xmtrx, fv0

            fmov.d    @r4+, dr8
            fmov.d    @r4+, dr10

            ftrv    xmtrx, fv4

            fmov.d    @r4+, dr12
            fmov.d    @r4+, dr14
    
            ftrv    xmtrx, fv8
            ftrv    xmtrx, fv12  

            frchg
            fmov.d    @r15+, dr12  
            fmov.d    @r15, dr14

            mov        r7, r15
            rts
            fschg
.fast_mat_apply_end:
        .size    _fast_mat_apply,.fast_mat_apply_end - _fast_mat_apply

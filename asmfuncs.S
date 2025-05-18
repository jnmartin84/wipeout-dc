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
	fmov	fr12,	@-r15
! 4
	add	#16,	r4
! 6
	fmov	fr13,	@-r15
! 8
	mov	#1,	r0
! 10
	fmov	fr14,	@-r15
! 12
	fmov	@r5+,	fr0
! 14
	fmov	@r5+,	fr1
! 16
	fmov	@r5+,	fr2
! 18
	fldi1	fr3
!20
	add	#4,	r5
!22
	dt	r6
!24
	pref	@r5

	! first transform
! 26
	ftrv	xmtrx,	fv0
! 28
	fmov	@r5+, fr4
! 30
	fmov	@r5+, fr5
! 32
	fmov	@r5+, fr6
! 34
	fldi1	fr7
! 36
	add	#4, r5
! 38
	pref	@r5
! 40
	dt	r6
! 42
	ftrv	xmtrx,	fv4
! 44
	bt	.loopEnd
! 46
	mov     #0, r1
! 48
	bra	.loop
! 50
! 52
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
	nop
	nop
! 64
.loop:
	pref	@r4

! 66
	! Load a vector.
! 68
	fmov	@r5+,	fr8
! 70
	fmov	@r5+,	fr9
! 72
	fmov	@r5+,	fr10
! 74
	fldi1	fr11
! 76
	add	#4,	r5
! 78
	pref	@r5
! 80
! Store a vector.
	ftrv	xmtrx,	fv8

! 82
! w = fr3
	fmov    fr3,	@-r4
! 84
! z = fr2
	fmov    fr2,	@-r4
! 86
	fneg fr3
! 88
	fmov	fr1,	@-r4
! 90
	fcmp/gt	fr3,	fr2
! 92
	fmov	fr0,	@-r4
! 94
	movt	r7
! 96
	add	#32,	r4
! 98
	dt	r6
! 100
	and	r7,	r0
! 102
	bt/s	.firstInLoop
! 104
	pref	@r4

! 106
! load another vect
	fmov	@r5+,	fr0
! 108
	fmov	@r5+,	fr1
! 110
	fmov	@r5+,	fr2
! 112
	add	#4,	r5
! 114
	fldi1	fr3
! 116
	pref	@r5
! 118
! Store another vector.
	ftrv	xmtrx,	fv0
! 120
! w
	fmov	fr7,	@-r4
! 122
! z
	fmov	fr6,	@-r4
! 124
	fneg	fr7
! 126
	fmov	fr5,	@-r4
! 128
	fcmp/gt	fr7,	fr6
! 130
	fmov	fr4,	@-r4
! 132
	movt	r7
! 134
	add	#32,	r4
! 136
	dt	r6
! 138
	and	r7,	r0
! 140
	bt/s	.secondInLoop
! 142
	pref	@r4
! 144
! Load a vector.
	fmov	@r5+,	fr4
! 146
	fmov	@r5+,	fr5
! 148
	fmov	@r5+,	fr6
! 150
	add	#4,	r5
! 152
	fldi1	fr7
	pref	@r5
! 154
! Store a vector.
	ftrv	xmtrx,	fv4
! 156
	fmov	fr11,	@-r4
! 158
	fmov	fr10,	@-r4
! 160
	fneg	fr11
! 162
	fmov	fr9,	@-r4
! 164
	fcmp/gt	fr11,	fr10
! 166
	fmov	fr8,	@-r4
! 168
	movt	r7
! 170
	add	#32,	r4
! 172
	dt	r6
! 174
	and	r7,	r0
! 176
	bf/s	.loop
! 178
.loopEnd:
	pref	@r4
! 180
	fmov	fr3,	@-r4
! 182
	fmov	fr2,	@-r4
! 184
	fneg	fr3
! 186
	fmov	fr1,	@-r4
! 188
	fcmp/gt	fr3,	fr2
! 190
	fmov	fr0,	@-r4
! 192
	movt	r7
! 194
	add	#32,	r4
! 196
	and	r7,	r0
! 198
	fmov	fr7,	@-r4
! 200
	fmov	fr6,	@-r4
! 202
	fneg	fr7
! 204
	fmov	fr5,	@-r4
! 206
	fcmp/gt	fr7,	fr6
! 208
	fmov	fr4,	@-r4
! 210
	movt	r7
! 212
	fmov	@r15+,	fr14
! 214
	and	r7,	r0
! 216
	fmov	@r15+,	fr13
! 218
	rts
! 220
	fmov	@r15+,	fr12
! 222
.only1Vertex:
	fmov	fr3,	@-r4
! 224
	fmov	fr2,	@-r4
! 226
	fneg	fr3
! 228
	fmov	fr1,	@-r4
! 230
	fcmp/gt	fr3,	fr2
! 232
	fmov	fr0,	@-r4
! 234
	movt	r7
! 236
	fmov	@r15+,	fr14
! 238
	and	r7,	r0
! 240
	fmov	@r15+,	fr13
! 244
	rts
! 246
	fmov	@r15+,	fr12
! 248
.firstInLoop:
	pref	@r4
! 250
	fmov	fr7,	@-r4
! 252
	fmov	fr6,	@-r4
! 254
	fneg	fr7
! 256
	fmov	fr5,	@-r4
! 258
	fcmp/gt	fr7,	fr6
! 260
	fmov	fr4,	@-r4
! 262
	movt	r7
! 264
	add	#32,	r4
! 268
	and	r7,	r0
! 270
	fmov	fr11,	@-r4
! 272
	fmov	fr10,	@-r4
! 274
	fneg	fr11
! 276
	fmov	fr9,	@-r4
! 278
	fcmp/gt	fr11,	fr10
! 280
	fmov	fr8,	@-r4
! 282
	movt	r7
! 284
	fmov	@r15+,	fr14
! 286
	and	r7,	r0
! 288
	fmov	@r15+,	fr13
! 290
	rts
! 292
	fmov	@r15+,	fr12
! 294
.secondInLoop:
        or      r4, r1
	pref	@r4
        add     #32, r1
! 296
	fmov	fr11,	@-r4
        pref    @r1
	mov	#0, r1
! 298
	fmov	fr10,	@-r4
! 300
	fneg	fr11
! 302
	fmov	fr9,	@-r4
! 304
	fcmp/gt	fr11,	fr10
! 306
	fmov	fr8,	@-r4
! 308
	movt	r7
! 310
!	add	#32,	r4
	add r4,r1
	add #32, r1
! 312
	and	r7,	r0
! 314
	fmov	fr3,	@-r1
! 316
	fmov	fr2,	@-r1
! 318
	fneg	fr3
! 320
	fmov	fr1,	@-r1
! 322
	fcmp/gt	fr3,	fr2
! 324
	fmov	fr0,	@-r1
! 326
	movt	r7
! 328
	fmov	@r15+,	fr14
! 330
	and	r7,	r0
! 332
	fmov	@r15+,	fr13
! 334
	rts
! 336
	fmov	@r15+,	fr12
! 338
.xform_all_verts_end:
	.size	_xform_all_verts,.xform_all_verts_end - _xform_all_verts


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


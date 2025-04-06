! this is really fast and got me a 40+ FPS boost in wipeout-rewrite
! at +8 draw distance
.text
    .global mat_transform_vec_array_to_vec_array_nomod_nodivw

    .align 5
mat_transform_vec_array_to_vec_array_nomod_nodivw:
!void mat_transform_vec_array_to_vec_array_nomod_nodivw(vector_t *output, vector_t *input, int c)
! r4 = & output[0]
! r5 = & input [0]
! r6 = count

	! pref first input
	pref	@r5

	fmov	fr12, @-r15
    add     #16, r4
	fmov	fr13, @-r15
	fmov	fr14, @-r15

	fmov	@r5+, fr0
	fmov	@r5+, fr1
	fmov	@r5+, fr2
	fldi1	fr3
	add	#4, r5
	dt	r6
	pref	@r5


	! first transform
	ftrv	xmtrx, fv0
	bt	.only1Vertex

	fmov	@r5+, fr4
	fmov	@r5+, fr5
	fmov	@r5+, fr6
	fldi1	fr7
	add	#4, r5
	pref	@r5

	dt	r6
	ftrv	xmtrx, fv4
	bt	.loopEnd
.loop:
	pref	@r4

	! Load a vector.
	fmov	@r5+, fr8
	fmov	@r5+, fr9
	fmov	@r5+, fr10
	fldi1	fr11
	add	#4, r5
	dt	r6
	pref	@r5

	! Store a vector.
	ftrv	xmtrx, fv8

	fmov    fr3, @-r4
	fmov    fr2,@-r4
	fmov	fr1, @-r4
	fmov	fr0, @-r4
    add #32,r4

	bt/s        .firstInLoop
	pref        @r4

	! load another vect
    fmov        @r5+, fr0
    fmov        @r5+, fr1
    fmov        @r5+, fr2
    fldi1       fr3
	add			#4, r5
    dt          r6

    pref        @r5

	! Store another vector.
    ftrv        xmtrx, fv0

	fmov		fr7, @-r4
    fmov        fr6, @-r4
    fmov        fr5, @-r4
    fmov        fr4, @-r4
    add #32,r4

    bt/s        .secondInLoop
    pref        @r4

   ! Load a vector.
    fmov        @r5+, fr4
    fmov        @r5+, fr5
    fmov        @r5+, fr6
    fldi1       fr7
    add         #4, r5
    dt          r6

    pref        @r5

    ! Store a vector.
    ftrv        xmtrx, fv4

	fmov		fr11, @-r4
    fmov        fr10, @-r4
    fmov        fr9, @-r4
    fmov        fr8, @-r4
    add #32,r4

    bf/s        .loop

.loopEnd:
    pref        @r4

	fmov		fr3, @-r4
    fmov        fr2, @-r4
    fmov        fr1, @-r4
    fmov        fr0, @-r4
    add #32,r4

	fmov		fr7, @-r4
    fmov        fr6, @-r4
    fmov        fr5, @-r4
    fmov       fr4, @-r4

    fmov        @r15+, fr14
    fmov        @r15+, fr13
    rts
    fmov        @r15+, fr12
		
.only1Vertex:
	fmov		fr3, @-r4
    fmov        fr2, @-r4
    fmov        fr1, @-r4
    fmov        fr0, @-r4
    fmov        @r15+, fr14
    fmov        @r15+, fr13
    rts
    fmov        @r15+, fr12

.firstInLoop:
    pref        @r4

	fmov		fr7, @-r4
    fmov        fr6, @-r4
    fmov        fr5, @-r4
    fmov       fr4, @-r4
    add #32,r4

	fmov		fr11, @-r4
    fmov        fr10, @-r4
    fmov        fr9, @-r4
    fmov        fr8, @-r4

    fmov        @r15+, fr14
    fmov        @r15+, fr13
    rts
    fmov        @r15+, fr12
	

.secondInLoop:
    pref        @r4

	fmov		fr11, @-r4
    fmov        fr10, @-r4
    fmov        fr9, @-r4
    fmov        fr8, @-r4
    add #32,r4

	fmov		fr3, @-r4
    fmov        fr2, @-r4
    fmov        fr1, @-r4
    fmov        fr0, @-r4

    fmov        @r15+, fr14
    fmov        @r15+, fr13
    rts
    fmov        @r15+, fr12